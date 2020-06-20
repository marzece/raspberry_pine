#ifndef RASBERRY_PINE_COMMON_UTILS_H
#define RASBERRY_PINE_COMMON_UTILS_H
#include <inttypes.h>
//
// Struct for passing generic data read/writes to the SPI interface
// I choose no more than 4 data words here b/c the SPI interface FIFO depth
// is 4, but in principle this could be extended to greater depth by having the
// write function spool the data out 4 at a time....but whatevs
typedef struct SPI_Data {
    uint32_t mosi[4];
    uint32_t miso[4];
    unsigned int lengths[4];
    unsigned int n_writes;
} SPI_Data;

int has_even_parity(uint32_t x, unsigned int n);
uint32_t reverse_bits(const uint32_t word, const unsigned int n);
#endif
