/* The IOP's memory (port/include/port/audio.h). Only the sound driver uses it: the game uploads
   sound banks, sequences and the driver's command lists into it by SIF DMA. */
#include "port/audio.h"

static uint8_t iop_ram[IOP_RAM_SIZE];

uint8_t *port_iop_ram(void) {
    return iop_ram;
}

void *port_iop_ptr(uint32_t iop_address) {
    return &iop_ram[iop_address & (IOP_RAM_SIZE - 1)];
}
