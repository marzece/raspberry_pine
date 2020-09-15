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


/*
int handle_error(enum SWD_ERROR error) {
    switch error {
        case SWD_ACK_WAIT:
        case SWD_ACK_FAULT:
        case SWD_ACK_FAULT:
        case SWD_ACK_UNKNOWN:
        case SWD_PARITY_MISMATCH:
    }

    return 0;
}
*/

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
            printf("ACK_WAIT recieved\n");
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

SWD_Packet debug_power(SPIRegisters spi_registers, int powerup) {
    // Now read the CNTRL/STAT reg
    SWD_Packet read_ctrlstat_reg = swd_read_cntrl_stat_reg();
    // I need to write to the cntrl/stat register to power up
    // the various debug subsystems.
    SWD_CNTRL_STAT_Reg ctrlstat_reg = {
        .CSYSPWRUPACK = 0,
        .CSYSPWRUPREQ = powerup,
        .CDBGPWRUOACK = 0,
        .CDBGPWRUPREQ = powerup,
        .CDBGRSTACK = 0,
        .CDBGRSTREQ = 0,
        .TRNCNT = 0,
        .MASKLANE = 0,
        .WDATAERR = 0,
        .READOK = 0,
        .STICKYERR = 0,
        .STICKYCMP = 0,
        .TRNMODE = 0,
        .STICKYORUN = 0,
        .ORUNDETECT = 0 };

    SWD_Packet write_cntrlstat_packet = swd_write_cntrl_stat_reg(ctrlstat_reg);
    perform_swd_io(spi_registers, &write_cntrlstat_packet);
    perform_swd_io(spi_registers, &read_ctrlstat_reg);
    printf("CTRL_STAT = 0x%x\n", read_ctrlstat_reg.data);
    return read_ctrlstat_reg;
}

int main() {

    uint32_t* mem = create_gpio_mmap();
    if(!mem) {
        return 1;
    }
    SPIRegisters spi_registers = init_aux_spi(mem);

    // Enable the AUX SPI1 interface
    const uint32_t ENABLE_AUX_SPI1 = 0x2; // Bit 1
    *spi_registers.enable = ENABLE_AUX_SPI1;

    // Now  adjust the control reg
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

    // Perform a SWD line reset
    printf("performing reset\n");
    SPI_Data swd_to_jtag_data = swd_jtag_to_swd();
    SPI_Data reset_data = swd_protocol_reset();
    spi_io(spi_registers, &reset_data);
    spi_io(spi_registers, &swd_to_jtag_data);
    spi_io(spi_registers, &reset_data);
    spi_io(spi_registers, &reset_data);
    sleep(1);

    // Read the SWD ID register
    SWD_Packet read_idr_packet = swd_read_dpidr_reg();
    perform_swd_io(spi_registers, &read_idr_packet);

    printf("IDCode = 0x%x\n", read_idr_packet.data);
    struct SWD_DPIDR_Reg idr_reg = interpret_dp_idr_reg(read_idr_packet.data);
    printf("revision = 0x%x\n", idr_reg.revision);
    printf("part_number = 0x%x\n", idr_reg.part_number);
    printf("min = 0x%x\n", idr_reg.min);
    printf("version = 0x%x\n", idr_reg.version);
    printf("designer = 0x%x\n", idr_reg.designer);
    printf("\n");

    debug_power(spi_registers, 1);

    // AP_SEL=1 is the CTRL_AP AP_SEL=0 is the AHB MEM_AP
    // Now read the CTRL-AP ID Register Address=0xFC
    SWD_SELECT_Reg select_reg = { .APSEL = 0x1, .APBANKSEL = 0xF, .DPBANKSEL = 0x0 };
    SWD_Packet write_select_packet = swd_write_select_reg(select_reg);
    perform_swd_io(spi_registers, &write_select_packet);

    // Now write with AP =1 adn addr[3:2] = 0b11 
    SWD_Packet read_ap_id_packet = swd_read_ap_idcode();
    perform_swd_io(spi_registers, &read_ap_id_packet);
    perform_swd_io(spi_registers, &read_ap_id_packet); // For AP access need to perform a read twice
    printf("IDR = 0x%x\n", read_ap_id_packet.data);

    select_reg.DPBANKSEL = 0x0;
    select_reg.APBANKSEL = 0x0;
    select_reg.APSEL = 0x0;
    write_select_packet = swd_write_select_reg(select_reg);
    perform_swd_io(spi_registers, &write_select_packet);

    SWD_Packet read_protect_status_reg = swd_read_protect_status_reg();
    perform_swd_io(spi_registers, &read_protect_status_reg);
    perform_swd_io(spi_registers, &read_protect_status_reg);
    printf("Protect Status = 0x%x\n", read_protect_status_reg.data);
    perform_swd_io(spi_registers, &read_protect_status_reg);
    perform_swd_io(spi_registers, &read_protect_status_reg);
    printf("Protect Status = 0x%x\n", read_protect_status_reg.data);

    // Perform an "eraseall" to remove firmware lock
    //SWD_Packet write_eraseall_reg = swd_ap_write_eraseall();
    //perform_swd_io(spi_registers, &write_eraseall_reg);


    // Clean up
    clean_up_mmap();
    mem = NULL;
    return 0;
}
