/* LZ4's block format (port/include/port/lz4_block.h), for save states (port/platform/savestate.c) and
   the debugging tools' captures (port/debug/gs_capture.c, tools/gsreplay). */
#include <string.h>

#include "port/lz4_block.h"

/* ---- LZ4's block format ---------------------------------------------------------------------------
   Sequences of a token (literals' length, match length - 4), the literals, a 16-bit offset back and
   the match; lengths of 15 go on in bytes of 255. The last 5 bytes are literals, and no match starts
   in the last 12. */
#define LZ_MIN_MATCH 4
#define LZ_LAST_LITERALS 5
#define LZ_MATCH_LIMIT 12

static uint32_t read32(const uint8_t *p) {
    uint32_t v;

    memcpy(&v, p, 4);
    return v;
}

static uint8_t *lz_length(uint8_t *out, size_t length) {
    while (length >= 255) {
        *out++ = 255;
        length -= 255;
    }
    *out++ = (uint8_t)length;
    return out;
}

static uint8_t *lz_sequence(uint8_t *out, const uint8_t *literals, size_t literal_length, size_t offset,
                            size_t match_length) {
    uint8_t *token = out++;
    size_t m = match_length - LZ_MIN_MATCH;

    *token = (uint8_t)((literal_length < 15 ? literal_length : 15) << 4);
    if (literal_length >= 15) {
        out = lz_length(out, literal_length - 15);
    }
    memcpy(out, literals, literal_length);
    out += literal_length;
    if (match_length == 0) {
        return out;
    }
    *token |= (uint8_t)(m < 15 ? m : 15);
    *out++ = (uint8_t)offset;
    *out++ = (uint8_t)(offset >> 8);
    if (m >= 15) {
        out = lz_length(out, m - 15);
    }
    return out;
}

/* Compresses n bytes into out (room for n + n / 255 + 16); returns the size */
size_t lz_compress(const uint8_t *in, size_t n, uint8_t *out, uint32_t *table) {
    uint8_t *start = out;
    size_t at = 0, anchor = 0;

    memset(table, 0, sizeof(uint32_t) << LZ_HASH_BITS);
    if (n > LZ_MATCH_LIMIT) {
        size_t limit = n - LZ_MATCH_LIMIT;
        size_t match_end = n - LZ_LAST_LITERALS;

        while (at < limit) {
            uint32_t word = read32(in + at);
            uint32_t h = (word * 2654435761u) >> (32 - LZ_HASH_BITS);
            size_t candidate = table[h];

            table[h] = (uint32_t)at;
            if (candidate < at && at - candidate <= 0xFFFF && read32(in + candidate) == word) {
                size_t length = LZ_MIN_MATCH;

                while (at + length + 8 <= match_end) {
                    uint64_t a, b;

                    memcpy(&a, in + at + length, 8);
                    memcpy(&b, in + candidate + length, 8);
                    if (a != b) {
                        break;
                    }
                    length += 8;
                }
                while (at + length < match_end && in[at + length] == in[candidate + length]) {
                    length++;
                }
                out = lz_sequence(out, in + anchor, at - anchor, at - candidate, length);
                at += length;
                anchor = at;
            } else {
                at++;
            }
        }
    }
    out = lz_sequence(out, in + anchor, n - anchor, 0, 0);
    return (size_t)(out - start);
}

/* 1 when `in` decompresses to exactly `n` bytes at out */
int lz_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t n) {
    const uint8_t *in_end = in + in_size;
    size_t at = 0;

    while (in < in_end) {
        unsigned token = *in++;
        size_t literals = token >> 4;
        size_t length = token & 15;
        size_t offset;

        if (literals == 15) {
            unsigned more;

            do {
                if (in >= in_end) {
                    return 0;
                }
                more = *in++;
                literals += more;
            } while (more == 255);
        }
        if (literals > (size_t)(in_end - in) || literals > n - at) {
            return 0;
        }
        memcpy(out + at, in, literals);
        in += literals;
        at += literals;
        if (in == in_end) {
            break;
        }
        if (in_end - in < 2) {
            return 0;
        }
        offset = (size_t)in[0] | ((size_t)in[1] << 8);
        in += 2;
        if (length == 15) {
            unsigned more;

            do {
                if (in >= in_end) {
                    return 0;
                }
                more = *in++;
                length += more;
            } while (more == 255);
        }
        length += LZ_MIN_MATCH;
        if (offset == 0 || offset > at || length > n - at) {
            return 0;
        }
        while (length-- > 0) {
            out[at] = out[at - offset];
            at++;
        }
    }
    return at == n;
}
