/* The parts of the PS2's C and compiler runtime whose results the game depends on, where the host's
   differ: rand (the SDK's newlib, RAND_MAX 0x7FFFFFFF, seed 1) and fptoui (libgcc's soft-float
   conversion, which saturates where a C cast is undefined). */
#include <stdint.h>

static int32_t rand_next = 1;

void srand(unsigned int seed) {
    rand_next = (int32_t)seed;
}

/* newlib's rand as the SDK built it: next = next * 1103515245 + 12345, the low 31 bits */
int rand(void) {
    rand_next = (int32_t)((uint32_t)rand_next * 0x41C64E6Du + 0x3039u);
    return rand_next & 0x7FFFFFFF;
}

/* libgcc's float -> unsigned int: 0 for NaN, negatives and values below 1, all ones from 2^32 up */
unsigned int fptoui(float value) {
    if (!(value >= 1.0f)) {
        return 0;
    }
    if (value >= 4294967296.0f) {
        return 0xFFFFFFFFu;
    }
    return (unsigned int)value;
}
