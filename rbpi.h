#ifndef RASBERRY_PINE_RBPI_H
#define RASBERRY_PINE_RBPI_H
#include <inttypes.h> // For uint32_t
#include "common_utils.h"
#define AUX_SPI_FIFO_DEPTH 4

typedef struct ControlReg {
    uint32_t speed;
    uint32_t chip_select_pattern;
    uint32_t post_input_mode;
    uint32_t variable_cs;
    uint32_t variable_width;
    uint32_t dout_hold_time;
    uint32_t enable;
    uint32_t in_rising;
    uint32_t clear_fifos;
    uint32_t out_rising;
    uint32_t invert_clk;
    uint32_t msb_out_first;
    uint32_t shift_length;
    uint32_t cs_high_time;
    uint32_t tx_empty_irq;
    uint32_t done_irq;
    uint32_t msb_in_first;
    uint32_t keep_input;
} ControlReg;

typedef struct StatReg {
    uint8_t tx_fifo_level;
    uint16_t rx_fifo_level;
    int tx_full;
    int tx_empty;
    int rx_empty;
    int rx_full;
    int busy;
    uint8_t bit_count;
} StatReg;


// I'm only like 75% sure volatile is useful/recommendable here
typedef struct SPIRegisters {
    volatile uint32_t* base;
    volatile uint32_t* enable;
    volatile uint32_t* control1;
    volatile uint32_t* control2;
    volatile uint32_t* stat;
    volatile uint32_t* io;
    volatile uint32_t* peek;
} SPIRegisters;

uint32_t* create_gpio_mmap();
SPIRegisters init_aux_spi(uint32_t* local_mem);
StatReg interpret_stat_word(uint32_t word);
void write_control_reg(SPIRegisters spi_registers, ControlReg values) ;
ControlReg interpret_control_reg(uint32_t control1, uint32_t control2);
// IO operations
uint32_t spi_read(SPIRegisters spi_registers);
void spi_write(SPIRegisters spi_registers, uint32_t data, unsigned int n);
void clear_rx_reg(SPIRegisters spi_registers);
int spi_io(SPIRegisters spi_registers, SPI_Data* data);
void wait_for_spi_transaction_to_finish(SPIRegisters spi_registers);
void clean_up_mmap();
#endif
