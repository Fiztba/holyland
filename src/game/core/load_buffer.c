/* The area the game loads its files into, which is also its sound heap. */
#include "common.h"

/* 0x249680 the file-load buffer; the sound heap starts at its 64-byte boundary */
char file_load_buffer[0x100000] = { 0 };
