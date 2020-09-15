#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>

#include "common_utils.h"
#include "rbpi.h" 
#include "swd.h" 



// NTS, SPI-SWD is LSB first.
// So technically the Start bit is the least significant, but obviously thats not what I have here.
// It's fine for the header, but I'll have to be careful with the data payloads.
#define SWD_START_BIT  0x80
#define SWD_APnDP      0x40  // APnDP = Acess Port (NOT Debug Port) (should never be set I think)
#define SWD_RnW        0x20  // RnW = Read (NOT Write)
#define SWD_ADDR_A2    0x10
#define SWD_ADDR_A3    0x08
#define SWD_PARITY_BIT 0x04
#define SWD_STOP_BIT   0x02
#define SWD_PARK_BIT   0x01

// MEM-AP stuff (todo need to make MEM-AP accesses and such its own module probably)
#define CSW_OFFSET 0x0
#define TAR_OFFSET 0x4
#define DRW_OFFSET 0xC
#define NVMC_OFFSET 0x4001E000
#define NVMC_CONFIG_OFFSET 0x504
#define NVMC_ERASEALL 0x50C


int perform_swd_io(SPIRegisters spi_registers, SWD_Packet* packet_data) {
    /* This function uses the data in 'packet_data' to create an SPI_Data packet which
     * is then sent out to the SPI interface where the actual "on the wire" stuff happens.
     *
     * If the "packet_data" is a read operation, then the response is packed into the "data"
     * field of the packet_data. For both a read and a write operation the "ack" field
     * of packet_data is filled in.
     */

    //First thing is to send out the header and read back the response (which should include the ACK)
    uint32_t header_word = create_header_word(packet_data->header);
    int parity_bit;


    //printf("%s", packet_data->debug_string);
    SPI_Data spi_data;
    spi_data.n_writes = 1;
    spi_data.mosi[0] = header_word;

    // Need to pad the header word with some extra bits so that the ACK gets sent back.
    // If this is a read operation the padding is 4 bits (1 turnaround + 3 ACK bits).
    // If this is a write operation the padding is 5 bits (1 turnaround + 3 ACK bits + 1 turnaround)
    // TODO, logically this should be done in SWD.c but that's a pain so we'll just do it here
    spi_data.mosi[0] |= 0b1111 << 8;
    spi_data.lengths[0] = 12;
    if(!packet_data->header.RnW) {
        spi_data.mosi[0] |= 1<<12;
        spi_data.lengths[0] += 1;
    }
    spi_io(spi_registers, &spi_data);
    packet_data->ack = (spi_data.miso[0] >> 9) & 0b111;


    // TODO, implement something here.
    switch(packet_data->ack) {
        case ACK_OK:
            break;
        case ACK_WAIT:
            return SWD_ACK_WAIT;
        case ACK_FAULT:
            return SWD_ACK_FAULT;
        default:
            printf("Invalid ACK from slave device ACK = 0x%x\n", packet_data->ack);
            return SWD_ACK_UNKNOWN;
    }

    // If here we can continue with the transfer
    if(packet_data->header.RnW) {
        // Read data is all 1s to provide a pull-up.
        // The slave device will do the actual work
        spi_data.mosi[0] = 0xFFFF;
        spi_data.lengths[0] = 16;
        spi_data.mosi[1] = 0x1FFFF;
        spi_data.lengths[1] = 17;

    } else {
        // I could rely on the user to send in the correct parity bit...
        // but why not just do it here.
        parity_bit = has_even_parity(packet_data->data, 32) ? 0 : 1;
        spi_data.mosi[0] = packet_data->data & 0xFFFF;
        spi_data.lengths[0] = 16;
        spi_data.mosi[1] = ((packet_data->data >> 16) & 0xFFFF);
        spi_data.mosi[1] |= parity_bit ? 1 << 16 : 0; // Add the parity bit
        spi_data.lengths[1] = 17;
    }

    // Need to "close" the transaction with at least 8 "idles".
    spi_data.mosi[2] = 0x0;
    spi_data.lengths[2] = 16;
    spi_data.n_writes = 3;


    spi_io(spi_registers, &spi_data); // Send it

    // If this was a read-op then get the data back and stuff in "packet_data"
    if(packet_data->header.RnW) {
        packet_data->data = spi_data.miso[0] | (spi_data.miso[1] << 16);
        packet_data->parity = (spi_data.miso[1] >> 16) & 0x1;

        int expected_parity = !has_even_parity(packet_data->data, 32);

        if(!packet_data->parity != !expected_parity) {
            printf("Parity mismatch 0x%x %i\n", packet_data->data, packet_data->parity);
            return SWD_PARITY_MISMATCH;
        }
    }
    return SWD_OK; 
}

uint32_t read_tar(SPIRegisters spi_registers) {
    SWD_Packet read_tar_reg = swd_read_ap_addr(TAR_OFFSET);
    perform_swd_io(spi_registers, &read_tar_reg);
    perform_swd_io(spi_registers, &read_tar_reg);
    return read_tar_reg.data;
}

int write_tar(SPIRegisters spi_registers, uint32_t addr) {
    SWD_Packet write_tar_reg = swd_write_ap_addr(TAR_OFFSET, addr);
    return perform_swd_io(spi_registers, &write_tar_reg);
}

int write_drw(SPIRegisters spi_registers, uint32_t data) {
    SWD_Packet write_tar_reg = swd_write_ap_addr(DRW_OFFSET, data);
    return perform_swd_io(spi_registers, &write_tar_reg);
}

uint32_t read_drw(SPIRegisters spi_registers) {
    SWD_Packet read_drw_reg = swd_read_ap_addr(DRW_OFFSET);
    perform_swd_io(spi_registers, &read_drw_reg);
    perform_swd_io(spi_registers, &read_drw_reg);
    return read_drw_reg.data;
}

uint32_t mem_ap_read(SPIRegisters spi_registers, uint32_t addr) {
    write_tar(spi_registers, addr);
    return read_drw(spi_registers);
}

int mem_ap_write (SPIRegisters spi_registers, uint32_t addr, uint32_t data){
    write_tar(spi_registers, addr);
    return write_drw(spi_registers, data);
}

int nvmc_config(SPIRegisters spi_registers, int write, int erase) {
    assert(!(write && erase)); // Can't set both at the same time
    int err = 0;

    uint32_t value = write ? 1 : 0;
    value = erase ? 2 : value;

    // TODO really need to overhaul the error stuff here
    err = mem_ap_write(spi_registers, NVMC_OFFSET + NVMC_CONFIG_OFFSET, value);
    return err;
}

int nvmc_erase_all(SPIRegisters spi_registers) {
    int err;

    if((err = nvmc_config(spi_registers, 0, 1))) {
        return err;
    }

    err = mem_ap_write(spi_registers, NVMC_OFFSET + NVMC_ERASEALL, 1);
    return err;
}

// Why does this return an SWD_Packet? TODO
SWD_Packet debug_power(SPIRegisters spi_registers, int powerup) {
    // Now read the CNTRL/STAT reg
    SWD_Packet read_ctrlstat_reg = swd_read_cntrl_stat_reg();
    // I need to write to the cntrl/stat register to power up
    // the various debug subsystems.
    perform_swd_io(spi_registers, &read_ctrlstat_reg);
    SWD_CNTRL_STAT_Reg ctrlstat_reg = interpret_ctrlstat_reg(read_ctrlstat_reg.data);

    if(ctrlstat_reg.WDATAERR || ctrlstat_reg.STICKYERR || ctrlstat_reg.STICKYCMP || ctrlstat_reg.STICKYORUN) {
        printf("Error bits in CTRL/STAT register set. You should clear those errors. Aborting\n");
        exit(1); // TODO shouldn't exit like this without cleaning shit up
    }

    // Check to see if the debug power state is equal to the desired debug power state
    // https://stackoverflow.com/questions/1596668/logical-xor-operator-in-c
    if( (!ctrlstat_reg.CSYSPWRUPACK == !powerup) && (!ctrlstat_reg.CDBGPWRUOACK == !powerup)) {
        // Debug is already powered as desired
        return read_ctrlstat_reg;
    }

    ctrlstat_reg.CSYSPWRUPREQ = powerup;
    ctrlstat_reg.CDBGPWRUPREQ = powerup;


    SWD_Packet write_cntrlstat_packet = swd_write_cntrl_stat_reg(ctrlstat_reg);
    perform_swd_io(spi_registers, &write_cntrlstat_packet);
    perform_swd_io(spi_registers, &read_ctrlstat_reg);
    printf("CNTRL_STAT = 0x%x\n", read_ctrlstat_reg.data);
    return read_ctrlstat_reg;
}

FILE* setup_source(const char* fn) {
    FILE* ret = fopen(fn, "rb");
    return ret;
}

int read_word(FILE* file, uint32_t *val_p) {
    if(!file) {
        return -1;
    }
    return fread(val_p, sizeof(uint32_t), 1, file);
        // TODO should check eof here?
}

int main(int argc, char** argv) {

    int err = 0;
    if(argc !=2) {
        printf("Must specify binary code file for sending to PineTime\n");
        return 0;
    }
    const char* code_filename = argv[1];
    FILE* code_source = fopen(code_filename, "rb");
    if(!code_source) {
        printf("Could not open file '%s'\n", code_filename);
        return -1;
    }

    uint32_t* mem = create_gpio_mmap();
    if(!mem) {
        return 1;
    }
    SPIRegisters spi_registers = init_aux_spi(mem);

    // Enable the AUX SPI1 interface
    const uint32_t ENABLE_AUX_SPI1 = 0x2; // Bit 1
    *spi_registers.enable = ENABLE_AUX_SPI1;

    // Now  adjust the RB-PI AUX SPI control reg
    ControlReg control_reg = {
        .speed = 0x28,
        .chip_select_pattern = 0,
        .post_input_mode = 0,
        .variable_cs = 0,
        .variable_width = 1,
        .dout_hold_time = 4,
        .enable = 1,
        .in_rising = 1,
        .clear_fifos = 0,
        .out_rising = 1,
        .invert_clk =0,
        .msb_out_first = 0,
        .shift_length = 0,
        .cs_high_time = 0,
        .tx_empty_irq = 0,
        .done_irq = 0,
        .msb_in_first = 0,
        .keep_input = 0
        };
    write_control_reg(spi_registers, control_reg);

    // Once here we're ready to start doing SPI stuff with the PineTime
    // Here's the basic steps needed to get code into the NRF's flash memory
    // SWD_RESET, JTAG_TO_SWD, SWD_RESET
    // Read the DP-ID b/c you have to do that I guess
    // Power up the debug bits inthe CTRL/STAT
    // Read CTRL/STAT, make sure error bits are off and debug power is 0=on
    // Read the CTRL-AP to make sure no protection is turned on
    // Maybe perform a reset in the CTRL-AP (idk yet)
    // (Any other checks in the CTRL AP?)
    // Go the MEM-AP (APSEL=0)
    // Read the CSW make sure the size field is 0b010 (32-bit transfers)
    // Using the TAR/DRW enable erase in the NVMC config
    // Do an "ERASE ALL" in the NVMC
    // Enable write  & disable erase in the NVMC config
    // start squirting data in the NV memory startin at address 0x0, 32-bits at a  time.
    // Perhaps here I should consider autoincrementing the TAR.
    // Once the squirting has finished do a reset in the CTRL-AP again.
    //
    // Set the NVMC config to read-only
    // power downt the debug stuff
    // Success?

    // Perform a SWD line reset
    printf("performing reset\n");
    SPI_Data swd_to_jtag_data = swd_jtag_to_swd();
    SPI_Data reset_data = swd_protocol_reset();
    spi_io(spi_registers, &reset_data);
    spi_io(spi_registers, &swd_to_jtag_data);
    spi_io(spi_registers, &reset_data);
    spi_io(spi_registers, &reset_data);
    sleep(1);

    // Read the DP ID register
    SWD_Packet read_idr_packet = swd_read_dpidr_reg();
    if(perform_swd_io(spi_registers, &read_idr_packet)) {
        printf("SWD protocol error encountered, quitting\n");
        err = -1;
        goto done;
    }

    printf("IDCode = 0x%x\n", read_idr_packet.data);
    struct SWD_DPIDR_Reg idr_reg = interpret_dp_idr_reg(read_idr_packet.data);
    printf("revision = 0x%x\n", idr_reg.revision);
    printf("part_number = 0x%x\n", idr_reg.part_number);
    printf("min = 0x%x\n", idr_reg.min);
    printf("version = 0x%x\n", idr_reg.version);
    printf("designer = 0x%x\n", idr_reg.designer);
    printf("\n");

    debug_power(spi_registers, 1);

    // AP_SEL=1 is the CTRL_AP
    // AP_SEL=0 is the AHB MEM_AP
    // Right now want to read the CTRL_AP PROT_STATUS (Addr=0xC)
    SWD_SELECT_Reg select_reg = { .APSEL = 0x1, .APBANKSEL = 0x0, .DPBANKSEL = 0x0 };
    SWD_Packet write_select_packet = swd_write_select_reg(select_reg);
    perform_swd_io(spi_registers, &write_select_packet);

    // Now read protect status (reminder, need to do two reads b/c idk thats the way it works)
    SWD_Packet read_protect_status_reg = swd_read_protect_status_reg();
    perform_swd_io(spi_registers, &read_protect_status_reg);
    perform_swd_io(spi_registers, &read_protect_status_reg);
    if(read_protect_status_reg.data != 0x1) {
        printf("NRF data protection is ON. Must do an ERASE ALL to fix this. Aborting\n");
        err = -1;
        goto done;
        // TODO do the below instead of quitting?
        // Perform an "eraseall" to remove firmware lock
        //SWD_Packet write_eraseall_reg = swd_ap_write_eraseall();
        //perform_swd_io(spi_registers, &write_eraseall_reg);
    }
    
    // TODO could maybe check the erase status here...make sure an erase isn't going on
    // that'd be good eventually maybe

    // Now done with the CTRL-AP, lets move to the MEM-AP/AHB-AP
    select_reg.APSEL = 0x0;
    select_reg.APBANKSEL = 0x0;
    select_reg.DPBANKSEL = 0x0;
    write_select_packet = swd_write_select_reg(select_reg);
    perform_swd_io(spi_registers, &write_select_packet);

    // Read the CSW make sure the size field is 0b010 (32-bit transfers)
    SWD_Packet read_csw = swd_read_ap_addr(CSW_OFFSET);
    perform_swd_io(spi_registers, &read_csw);
    perform_swd_io(spi_registers, &read_csw);

    MEM_AP_CSW_Reg csw = interpret_ap_csw_reg(read_csw.data);
    if(csw.size != 0b010) {
        // TODO, abort
    }
    if(csw.addr_increment != 0) {
        // TODO, abort
    }
    if(csw.transfer_in_progress != 0) {
        // TODO, abort
    }

    // Next erase all the NVMC memory
    if(nvmc_erase_all(spi_registers)) {
        printf("Error encountered while doing NVMC ERASE ALL\n");
        goto done;
    }
    // Set NVMC CONFIG to write_enable
    nvmc_config(spi_registers, 1, 0);

    // Now start writing data
    uint32_t flash_addr = 0x0;
    uint32_t flash_data;
    printf("Beginning WRITE!\n");
    int write_err;
    while(fread(&flash_data, sizeof(flash_data), 1, code_source) == 1) {
        if(flash_addr >= 0x80000) {
            printf("Binary file too big to fit in FLASH. Quitting mid-write\n");
            break;
        }

        // TODO. should perhaps check the transfer in progress bit in the CSW register
        // (I think thats where it is) to make sure things don't go too fast
        printf("Writing 0x%x = 0x%x\n", flash_addr, flash_data);
        do {
            write_err = mem_ap_write(spi_registers, flash_addr, flash_data);
        } while(write_err == SWD_ACK_WAIT);
        if(write_err) {
            printf("Error encountered while writing addr=0x%x\n", flash_addr);
            goto done;
        }
        flash_addr += 0x4;
    }
    printf("Writing done, doing check now\n");
    if(ferror(code_source) || !feof(code_source)) {
        printf("Error encountered reading data from binary code source\n");
        goto done;
    }
    mem_ap_read(spi_registers, NVMC_OFFSET + NVMC_CONFIG_OFFSET);
    
    // Now that writing has finished, set the NVMC back to read only
    // then go through all the data and confirm that it's right
    nvmc_config(spi_registers, 0, 0);
    fseek(code_source, 0, SEEK_SET);
    int err_count = 0;
    flash_addr = 0;
    while(fread(&flash_data, sizeof(flash_data), 1, code_source) == 1) {
        // DOn't need to check for stepping past end of flash memory b/c we
        // won't get here if that happened above...and unless someone re-wrote the 
        // code source file between above and now we're fine to assume everything's ok
        uint32_t rb_data = mem_ap_read(spi_registers, flash_addr);
        if(flash_data != rb_data) {
            printf("Flash data mismatch at address 0x%x: Readback = 0x%x, Expected = 0x%x\n", flash_addr, rb_data, flash_data);
            err_count++;
            if(err_count > 100) {
            printf("Too many errors found quitting readback check\n");
                break;
            }
        }
        flash_addr += 0x4;
    }
    if(ferror(code_source) || !feof(code_source)) {
        printf("Error encountered checking data put in the flash\n");
        goto done;
    }

    // And finally do a system reset I guess
    // TODO

    // Clean up
done:
    clean_up_mmap();
    mem = NULL;
    return err;
}
