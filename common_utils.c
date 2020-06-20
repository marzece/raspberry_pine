#include <inttypes.h>
#include <assert.h>
#include "common_utils.h"


// Mostly copied from
// https://stackoverflow.com/questions/21617970/how-to-check-if-value-has-even-parity-of-bits-or-odd
int has_even_parity(uint32_t x, unsigned int n) {
    unsigned int count = 0;
    unsigned int i;
    assert(n <= 32);

    for(i = 0; i < n; i++){
        count += x & (1<<i) ? 1 : 0;
    }
    return !(count % 2);
}

uint32_t reverse_bits(const uint32_t word, const unsigned int n) {
    assert(n<=32);
    uint32_t ret=0;
    int i;
    for(i=0; i<n; i++) {
        ret = ret << 1;
        ret |= (word >> i) & 1;
    }
    return ret;
}
