#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>

#include "rbpi.h"


/*
 * TODO:
 *      Implement error string
*/
// Memory Map stuff
static uint32_t* _mem = NULL;
static int mem_fd;
static uint32_t peri_size;

void write_control_reg(SPIRegisters spi_registers, ControlReg values) {
    // All bit positions here come from the BCM2835 datasheet page 22-25 and the errata
    // https://elinux.org/BCM2835_datasheet_errata
    uint32_t control1 = 0;
    uint32_t control2 = 0;
    control1 |= values.speed << 20;
    control1 |= values.chip_select_pattern << 17;
    control1 |= values.variable_width ? 1<<14 : 0;
    control1 |= values.dout_hold_time << 12;
    control1 |= values.enable ? 1<<11 : 0;
    control1 |= values.in_rising ? 1<<10: 0;
    control1 |= values.clear_fifos ? 1<<9: 0;
    control1 |= values.out_rising ? 1<<8: 0;
    control1 |= values.invert_clk ? 1<<7: 0;
    control1 |= values.msb_out_first ? 1<<6 : 0;
    control1 |= values.shift_length;

    control2 |= values.cs_high_time << 8;
    control2 |= values.tx_empty_irq ? 1<<7 : 0;
    control2 |= values.done_irq ? 1<<6 : 0;
    control2 |= values.msb_in_first ? 1<<1 : 0;
    control2 |= values.keep_input ? 1 : 0;

    *spi_registers.control1 = control1;
    *spi_registers.control2 = control2;
}

ControlReg interpret_control_reg(uint32_t control1, uint32_t control2) {
    // All bit positions here come from the BCM2835 datasheet page 22-25 and the errata
    // https://elinux.org/BCM2835_datasheet_errata
    ControlReg ret;
    ret.speed |= control1 >> 20;
    ret.chip_select_pattern = (control1 >> 17) & 0x7;
    ret.variable_width = (control1 >> 14) & 0x1;
    ret.dout_hold_time = (control1 >> 12) & 0x1;
    ret.enable = (control1 >> 11) & 0x1;
    ret.in_rising = (control1 >> 10) & 0x1;
    ret.clear_fifos = (control1 >> 9) & 0x1;
    ret.out_rising = (control1 >> 8) & 0x1;
    ret.invert_clk = (control1 >> 7) & 0x1;
    ret.msb_out_first = (control1 >> 6) & 0x1;
    ret.shift_length = control1 & 0x3F;

    ret.cs_high_time = (control2 >> 8) & 0x7;
    ret.tx_empty_irq = (control2>> 7) & 0x1;
    ret.done_irq = (control2 >>6) & 0x1;
    ret.msb_in_first = (control2 >>1) & 0x1;
    ret.keep_input = control2 & 0x1;

    return ret;

}
// Neither the BCM2835 datasheet nor the elinux BCM2835 errata get this register
// correct. I'm 99% certain the rx_fifo_level bits start at bit 20.
// NOTE TO SELF, propagate these changes back to the BCM2835 lib
StatReg interpret_stat_word(uint32_t word) {
    StatReg ret;
    ret.tx_fifo_level = (word >> 28) & 0xF;
    ret.rx_fifo_level = (word >> 20) & 0xF;
    ret.tx_full = (word >> 10) & 1;
    ret.tx_empty = (word >> 9) & 1;
    ret.rx_full = (word >> 8) & 1;
    ret.rx_empty = (word >> 7) & 1;
    ret.busy = (word >> 6) & 1;
    ret.bit_count = word & 0x3F;
    return ret;
}

static SPIRegisters create_aux_spi_registers(uint32_t* local_mem) {
    /*
     Page 8 of the BCM2835 peripherals data sheet  (also pages 6, sec 1.2.3)
     https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
     gives the memory locations of the various AUX registers.
     In the datasheet they all start at 0x7E....
     That physical address gets mapped to something else in by the OS/Memory management shit
     That new address is what's read from the device tree.
     With that address I still need the offset for the auxillary IO stuff,
     they all have the address 0x7E215XXX, so the offset is 0x215000.

     Further note, the table on page 8 is riddle with errors...see errata here
     https://elinux.org/BCM2835_datasheet_errata
    */
    SPIRegisters spi_registers;

    // Initializing spi_register with appropriate offsets
    const uint32_t bcm_aux_offset = 0x215000;
    const uint32_t aux_enable_offset = 0x04;
    const uint32_t aux_spi1_cntrl0_offset = 0x80;
    const uint32_t aux_spi1_cntrl1_offset = 0x84;
    const uint32_t aux_spi1_stat_offset = 0x88;
    const uint32_t aux_spi1_io_offset = 0xA0; // Note this address is WRONG(!) on page 8 of datasheet
    const uint32_t aux_spi1_peek_offset = 0x94;

    // The div by 4 is b/c "mem" is uint32*, aka 4 bytes
    // I could just be using uint8_t but this is how the BCM2835 lib does it
    // so I'll just keep things like them for now.
    spi_registers.base = local_mem + bcm_aux_offset/4;;
    spi_registers.enable = spi_registers.base + aux_enable_offset/4;
    spi_registers.control1 = spi_registers.base + aux_spi1_cntrl0_offset/4;;
    spi_registers.control2 = spi_registers.base + aux_spi1_cntrl1_offset/4;;
    spi_registers.stat = spi_registers.base + aux_spi1_stat_offset/4;;
    spi_registers.peek = spi_registers.base + aux_spi1_peek_offset/4;;
    spi_registers.io = spi_registers.base + aux_spi1_io_offset/4;;

    return spi_registers;

}

SPIRegisters init_aux_spi(uint32_t* local_mem) {
    /*
      First thing is to set GPIOs for the SPI1 AUX interface to their ALT4 function
      (see table 6.2 of BCM2835 datasheet).
      The relevant GPIO outputs are 16-21 (inclusive)
      Pin 16 = GPIO_FSEL1{20:18}
      Pin 17 = GPIO_FSEL1{23:21}
      Pin 18 = GPIO_FSEL1{26:24}
      Pin 19 = GPIO_FSEL1{29:27}
      Pin 20 = GPIO_FSEL2{2:0}
      Pin 20 = GPIO_FSEL2{5:3}

      For each of these select ALT-4 by writing the relevant bits as 011.
      For future reference I'll copy the whole GPIO selection bit-map
      000 = Pin is input
      001 = Pin is output
      100 = Alt0
      101 = Alt1
      110 = Alt2
      111 = Alt3
      011 = Alt4
      010 = Alt5


      Once that's done this function returns a struct with pointers to all the
      AUX_SPI registers.
    */

    // See Section 6.1 of BCM2835 datasheet for these offsets
    const uint32_t gpio_fsel_bank_offset = 0x200000;
    const uint32_t gpio_fsel1_offset = 0x4;
    const uint32_t gpio_fsel2_offset = 0x8;

    uint32_t current_val = 0;
    uint32_t *fsel_base = local_mem + gpio_fsel_bank_offset/4;
    uint32_t *p;
    p = fsel_base + gpio_fsel1_offset/4;
    current_val = *p;
    current_val |= (0b011 << 18); // Pin 16
    current_val |= (0b011 << 21); // Pin 17
    current_val |= (0b011 << 24); // Pin 18
    current_val |= (0b011 << 27); // Pin 19
    *p = current_val;

    p = fsel_base + gpio_fsel2_offset/4;
    current_val = *p;
    current_val |= (0b011 << 0); // Pin 20
    current_val |= (0b011 << 3); // Pin 21
    *p = current_val;

    return create_aux_spi_registers(local_mem);


}

void clean_up_mmap() {
    if(_mem != NULL) {
        munmap(_mem, peri_size);
        _mem = NULL;
        close(mem_fd);
    }
}

uint32_t* create_gpio_mmap() {
    FILE* fp = NULL;
    unsigned char buf[16];
    uint32_t base;

    if(geteuid() != 0) {// Check for sudo
        printf("must be sudo\n");
        return NULL;
    }

    fp = fopen("/proc/device-tree/soc/ranges", "rb");
    if(!fp) {
        printf("error opening thing\n");
        return NULL;
        }

    // This is all more or less copied from bcm2835.c
    // Its all just for testing/understanding. Do not use in "production"
    size_t nread = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if(nread < 16) {
        printf("could not read everything!\n");
        return NULL;
    }

    base = (buf[4] << 24) |
           (buf[5]<<16) |
           (buf[6] << 8) |
           (buf[7] << 0);
    peri_size = (buf[8] << 24) |
                (buf[9]<<16) |
                (buf[10] << 8) |
                (buf[11] << 0);

    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(mem_fd <0) {
        printf("Could not open '/dev/mem'\n");
        return NULL;
    }

    _mem = mmap(NULL, (size_t)peri_size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, base);
    if(_mem == MAP_FAILED) {
        printf("error opening mmap\n");
        return NULL;
    }
    return _mem;
}

void wait_for_spi_transaction_to_finish(SPIRegisters spi_registers) {
    StatReg status = interpret_stat_word(*spi_registers.stat);
    while(status.busy) {
        usleep(10);
        status = interpret_stat_word(*spi_registers.stat);
    }
}

void spi_write(SPIRegisters spi_registers, uint32_t data, unsigned int n) {
    assert(n <= 24);
    //printf("Writing %i bits to AUX_SPI\n", n);

    uint32_t data_mask = 0; // Mask the data to be the relevant # of bits
    // Fancy way to make a data mask
    // See stackoverflow.com/questions/316488/bit-mask-in-c
    //data_mask = ~((uint32_t)~0 << n); // For MSB first only
    data_mask = (1 << n) -1;
    data &= data_mask;
    data = data  | (n << 24); // bits 24-28 (inclusive) are the length of the word
    *spi_registers.io = data;
}

uint32_t spi_read(SPIRegisters spi_registers) {
    wait_for_spi_transaction_to_finish(spi_registers);
    // TODO check the tx_not_empty flag as well before reading
    return *spi_registers.io;
}

void clear_rx_reg(SPIRegisters spi_registers) {
    StatReg stat = interpret_stat_word(*spi_registers.stat);
    while(stat.busy) {
        usleep(100);
        stat = interpret_stat_word(*spi_registers.stat);

    }
    while(!stat.rx_empty) {
        *spi_registers.io;
        stat = interpret_stat_word(*spi_registers.stat);
    }
}

int spi_io(SPIRegisters spi_registers, SPI_Data* data){
    // First check to make sure the TX & RX fifo have enough space
    int i;
    StatReg stat = interpret_stat_word(*spi_registers.stat);
    // Need to know this to handle the MISO data...
    // MOSI data is assume to be done correctly already
    // TODO this function also assumes a bunch of stuff in the control regs is set just right
    // It might be nice to generalize this function further
    int msb_in_first = interpret_control_reg(0, *spi_registers.control2).msb_in_first;

    int rx_fifo_space = AUX_SPI_FIFO_DEPTH - stat.rx_fifo_level;
    int tx_fifo_space = AUX_SPI_FIFO_DEPTH - stat.tx_fifo_level;

    if( tx_fifo_space < data->n_writes || rx_fifo_space < data->n_writes) {
        printf("Can't do IO. Not enough space in FIFO currently\n");
        return 1;
    }
    if(stat.busy) {
        wait_for_spi_transaction_to_finish(spi_registers);
    }
    for(i=0; i<data->n_writes; i++) {
        spi_write(spi_registers, data->mosi[i], data->lengths[i]);
    }
    wait_for_spi_transaction_to_finish(spi_registers);
    for(i=0; i<data->n_writes; i++) {
        data->miso[i] = spi_read(spi_registers);
        if(!msb_in_first) {
            data->miso[i] = data->miso[i] >> (32-data->lengths[i]);
        }
    }
    return 0;
}
