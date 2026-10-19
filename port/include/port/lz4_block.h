/* LZ4's block format (port/platform/lz4_block.c): a small encoder and a checked decoder. */
#ifndef PORT_LZ4_BLOCK_H
#define PORT_LZ4_BLOCK_H

#include <stddef.h>
#include <stdint.h>

/* the compressor's hash table: (sizeof(uint32_t) << LZ_HASH_BITS) bytes */
#define LZ_HASH_BITS 16

/* Compresses n bytes into out (room for n + n / 255 + 16); returns the size */
size_t lz_compress(const uint8_t *in, size_t n, uint8_t *out, uint32_t *table);
/* 1 when `in` decompresses to exactly `n` bytes at out */
int lz_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t n);

#endif
