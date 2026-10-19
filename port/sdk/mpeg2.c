/* An MPEG-2 video decoder for the movies (port/sdk/mpeg2.h). Section and table numbers are ISO/IEC
   13818-2's. */
#include <stdlib.h>
#include <string.h>

#include "mpeg2.h"
#include "port/host.h"

/* ---- Variable-length codes (annex B) ----------------------------------------------------------
   Each table is written as its codes, and looked up by the next `bits` bits of the stream: an entry
   is the code's length << 24 | its value, 0 where no code starts. */
typedef struct {
    const char *code;
    int value;
} Vlc;

typedef struct {
    int bits;
    uint32_t *lookup;
} VlcTable;

/* B.1 macroblock_address_increment */
#define MBA_ESCAPE 34
static const Vlc mba_codes[] = {
    {"1", 1}, {"011", 2}, {"010", 3}, {"0011", 4}, {"0010", 5}, {"00011", 6}, {"00010", 7},
    {"0000111", 8}, {"0000110", 9}, {"00001011", 10}, {"00001010", 11}, {"00001001", 12},
    {"00001000", 13}, {"00000111", 14}, {"00000110", 15}, {"0000010111", 16}, {"0000010110", 17},
    {"0000010101", 18}, {"0000010100", 19}, {"0000010011", 20}, {"0000010010", 21},
    {"00000100011", 22}, {"00000100010", 23}, {"00000100001", 24}, {"00000100000", 25},
    {"00000011111", 26}, {"00000011110", 27}, {"00000011101", 28}, {"00000011100", 29},
    {"00000011011", 30}, {"00000011010", 31}, {"00000011001", 32}, {"00000011000", 33},
    {"00000001000", MBA_ESCAPE}, {NULL, 0}};

/* B.2 to B.4 macroblock_type, as these flags */
#define MB_QUANT 1
#define MB_FORWARD 2
#define MB_BACKWARD 4
#define MB_PATTERN 8
#define MB_INTRA 16
static const Vlc mb_type_i[] = {{"1", MB_INTRA}, {"01", MB_INTRA | MB_QUANT}, {NULL, 0}};
static const Vlc mb_type_p[] = {
    {"1", MB_FORWARD | MB_PATTERN}, {"01", MB_PATTERN}, {"001", MB_FORWARD}, {"00011", MB_INTRA},
    {"00010", MB_QUANT | MB_FORWARD | MB_PATTERN}, {"00001", MB_QUANT | MB_PATTERN},
    {"000001", MB_QUANT | MB_INTRA}, {NULL, 0}};
static const Vlc mb_type_b[] = {
    {"10", MB_FORWARD | MB_BACKWARD}, {"11", MB_FORWARD | MB_BACKWARD | MB_PATTERN},
    {"010", MB_BACKWARD}, {"011", MB_BACKWARD | MB_PATTERN}, {"0010", MB_FORWARD},
    {"0011", MB_FORWARD | MB_PATTERN}, {"00011", MB_INTRA},
    {"00010", MB_QUANT | MB_FORWARD | MB_BACKWARD | MB_PATTERN},
    {"000011", MB_QUANT | MB_FORWARD | MB_PATTERN}, {"000010", MB_QUANT | MB_BACKWARD | MB_PATTERN},
    {"000001", MB_QUANT | MB_INTRA}, {NULL, 0}};

/* B.9 coded_block_pattern */
static const Vlc cbp_codes[] = {
    {"111", 60}, {"1101", 4}, {"1100", 8}, {"1011", 16}, {"1010", 32}, {"10011", 12},
    {"10010", 48}, {"10001", 20}, {"10000", 40}, {"01111", 28}, {"01110", 44}, {"01101", 52},
    {"01100", 56}, {"01011", 1}, {"01010", 61}, {"01001", 2}, {"01000", 62}, {"001111", 24},
    {"001110", 36}, {"001101", 3}, {"001100", 63}, {"0010111", 5}, {"0010110", 9},
    {"0010101", 17}, {"0010100", 33}, {"0010011", 6}, {"0010010", 10}, {"0010001", 18},
    {"0010000", 34}, {"00011111", 7}, {"00011110", 11}, {"00011101", 19}, {"00011100", 35},
    {"00011011", 13}, {"00011010", 49}, {"00011001", 21}, {"00011000", 41}, {"00010111", 14},
    {"00010110", 50}, {"00010101", 22}, {"00010100", 42}, {"00010011", 15}, {"00010010", 51},
    {"00010001", 23}, {"00010000", 43}, {"00001111", 25}, {"00001110", 37}, {"00001101", 26},
    {"00001100", 38}, {"00001011", 29}, {"00001010", 45}, {"00001001", 53}, {"00001000", 57},
    {"00000111", 30}, {"00000110", 46}, {"00000101", 54}, {"00000100", 58}, {"000000111", 31},
    {"000000110", 47}, {"000000101", 55}, {"000000100", 59}, {"000000011", 27},
    {"000000010", 39}, {"000000001", 0}, {NULL, 0}};

/* B.10 motion_code, its magnitude: a sign bit follows all but 0 */
static const Vlc motion_codes[] = {
    {"1", 0}, {"01", 1}, {"001", 2}, {"0001", 3}, {"000011", 4}, {"0000101", 5}, {"0000100", 6},
    {"0000011", 7}, {"000001011", 8}, {"000001010", 9}, {"000001001", 10}, {"0000010001", 11},
    {"0000010000", 12}, {"0000001111", 13}, {"0000001110", 14}, {"0000001101", 15},
    {"0000001100", 16}, {NULL, 0}};

/* B.12 and B.13 dct_dc_size_luminance and _chrominance */
static const Vlc dc_luma_codes[] = {
    {"100", 0}, {"00", 1}, {"01", 2}, {"101", 3}, {"110", 4}, {"1110", 5}, {"11110", 6},
    {"111110", 7}, {"1111110", 8}, {"11111110", 9}, {"111111110", 10}, {"111111111", 11},
    {NULL, 0}};
static const Vlc dc_chroma_codes[] = {
    {"00", 0}, {"01", 1}, {"10", 2}, {"110", 3}, {"1110", 4}, {"11110", 5}, {"111110", 6},
    {"1111110", 7}, {"11111110", 8}, {"111111110", 9}, {"1111111110", 10}, {"1111111111", 11},
    {NULL, 0}};

/* B.14 and B.15, the DCT coefficients: run | level << 8 (a sign bit follows), EOB and escape */
#define RL(run, level) ((run) | ((level) << 8))
#define DCT_EOB 0xFFFF
#define DCT_ESCAPE 0xFFFE
/* the codes of 12 bits and more, the same in both tables but for the six B.14 alone has */
#define DCT_LONG_CODES                                                                                 \
    {"000000010001", RL(8, 2)}, {"000000010010", RL(4, 3)}, {"000000010101", RL(7, 2)},              \
    {"000000010110", RL(21, 1)}, {"000000010111", RL(20, 1)}, {"000000011001", RL(19, 1)},           \
    {"000000011010", RL(18, 1)}, {"000000011100", RL(3, 3)}, {"000000011110", RL(6, 2)},             \
    {"000000011111", RL(17, 1)}, {"0000000010000", RL(10, 2)}, {"0000000010001", RL(9, 2)},          \
    {"0000000010010", RL(5, 3)}, {"0000000010011", RL(3, 4)}, {"0000000010100", RL(2, 5)},           \
    {"0000000010101", RL(1, 7)}, {"0000000010110", RL(1, 6)}, {"0000000011011", RL(26, 1)},          \
    {"0000000011100", RL(25, 1)}, {"0000000011101", RL(24, 1)}, {"0000000011110", RL(23, 1)},        \
    {"0000000011111", RL(22, 1)}, {"00000000010000", RL(0, 31)}, {"00000000010001", RL(0, 30)},      \
    {"00000000010010", RL(0, 29)}, {"00000000010011", RL(0, 28)}, {"00000000010100", RL(0, 27)},     \
    {"00000000010101", RL(0, 26)}, {"00000000010110", RL(0, 25)}, {"00000000010111", RL(0, 24)},     \
    {"00000000011000", RL(0, 23)}, {"00000000011001", RL(0, 22)}, {"00000000011010", RL(0, 21)},     \
    {"00000000011011", RL(0, 20)}, {"00000000011100", RL(0, 19)}, {"00000000011101", RL(0, 18)},     \
    {"00000000011110", RL(0, 17)}, {"00000000011111", RL(0, 16)}, {"000000000010000", RL(0, 40)},    \
    {"000000000010001", RL(0, 39)}, {"000000000010010", RL(0, 38)}, {"000000000010011", RL(0, 37)},  \
    {"000000000010100", RL(0, 36)}, {"000000000010101", RL(0, 35)}, {"000000000010110", RL(0, 34)},  \
    {"000000000010111", RL(0, 33)}, {"000000000011000", RL(0, 32)}, {"000000000011001", RL(1, 14)},  \
    {"000000000011010", RL(1, 13)}, {"000000000011011", RL(1, 12)}, {"000000000011100", RL(1, 11)},  \
    {"000000000011101", RL(1, 10)}, {"000000000011110", RL(1, 9)}, {"000000000011111", RL(1, 8)},    \
    {"0000000000010000", RL(1, 18)}, {"0000000000010001", RL(1, 17)},                                \
    {"0000000000010010", RL(1, 16)}, {"0000000000010011", RL(1, 15)},                                \
    {"0000000000010100", RL(6, 3)}, {"0000000000010101", RL(16, 2)},                                 \
    {"0000000000010110", RL(15, 2)}, {"0000000000010111", RL(14, 2)},                                \
    {"0000000000011000", RL(13, 2)}, {"0000000000011001", RL(12, 2)},                                \
    {"0000000000011010", RL(11, 2)}, {"0000000000011011", RL(31, 1)},                                \
    {"0000000000011100", RL(30, 1)}, {"0000000000011101", RL(29, 1)},                                \
    {"0000000000011110", RL(28, 1)}, {"0000000000011111", RL(27, 1)}

/* B.14, without the first coefficient's "1" (decode_block reads that one itself) */
static const Vlc dct_zero_codes[] = {
    {"10", DCT_EOB}, {"11", RL(0, 1)}, {"011", RL(1, 1)}, {"0100", RL(0, 2)}, {"0101", RL(2, 1)},
    {"00101", RL(0, 3)}, {"00111", RL(3, 1)}, {"00110", RL(4, 1)}, {"000110", RL(1, 2)},
    {"000111", RL(5, 1)}, {"000101", RL(6, 1)}, {"000100", RL(7, 1)}, {"0000110", RL(0, 4)},
    {"0000100", RL(2, 2)}, {"0000111", RL(8, 1)}, {"0000101", RL(9, 1)}, {"000001", DCT_ESCAPE},
    {"00100110", RL(0, 5)}, {"00100001", RL(0, 6)}, {"00100101", RL(1, 3)}, {"00100100", RL(3, 2)},
    {"00100111", RL(10, 1)}, {"00100011", RL(11, 1)}, {"00100010", RL(12, 1)},
    {"00100000", RL(13, 1)}, {"0000001010", RL(0, 7)}, {"0000001100", RL(1, 4)},
    {"0000001011", RL(2, 3)}, {"0000001111", RL(4, 2)}, {"0000001001", RL(5, 2)},
    {"0000001110", RL(14, 1)}, {"0000001101", RL(15, 1)}, {"0000001000", RL(16, 1)},
    {"000000011101", RL(0, 8)}, {"000000011000", RL(0, 9)}, {"000000010011", RL(0, 10)},
    {"000000010000", RL(0, 11)}, {"000000011011", RL(1, 5)}, {"000000010100", RL(2, 4)},
    {"0000000011010", RL(0, 12)}, {"0000000011001", RL(0, 13)}, {"0000000011000", RL(0, 14)},
    {"0000000010111", RL(0, 15)}, DCT_LONG_CODES, {NULL, 0}};

/* B.15 (intra_vlc_format 1) */
static const Vlc dct_one_codes[] = {
    {"0110", DCT_EOB}, {"10", RL(0, 1)}, {"010", RL(1, 1)}, {"110", RL(0, 2)}, {"00101", RL(2, 1)},
    {"0111", RL(0, 3)}, {"00111", RL(3, 1)}, {"000110", RL(4, 1)}, {"00110", RL(1, 2)},
    {"000111", RL(5, 1)}, {"0000110", RL(6, 1)}, {"0000100", RL(7, 1)}, {"11100", RL(0, 4)},
    {"0000111", RL(2, 2)}, {"0000101", RL(8, 1)}, {"1111000", RL(9, 1)}, {"000001", DCT_ESCAPE},
    {"11101", RL(0, 5)}, {"000101", RL(0, 6)}, {"1111001", RL(1, 3)}, {"00100110", RL(3, 2)},
    {"1111010", RL(10, 1)}, {"00100001", RL(11, 1)}, {"00100101", RL(12, 1)},
    {"00100100", RL(13, 1)}, {"000100", RL(0, 7)}, {"00100111", RL(1, 4)},
    {"11111100", RL(2, 3)}, {"11111101", RL(4, 2)}, {"000000100", RL(5, 2)},
    {"000000101", RL(14, 1)}, {"000000111", RL(15, 1)}, {"0000001101", RL(16, 1)},
    {"1111011", RL(0, 8)}, {"1111100", RL(0, 9)}, {"00100011", RL(0, 10)}, {"00100010", RL(0, 11)},
    {"00100000", RL(1, 5)}, {"0000001100", RL(2, 4)}, {"11111010", RL(0, 12)},
    {"11111011", RL(0, 13)}, {"11111110", RL(0, 14)}, {"11111111", RL(0, 15)}, DCT_LONG_CODES,
    {NULL, 0}};

static VlcTable mba_table, mb_i_table, mb_p_table, mb_b_table, cbp_table, motion_table;
static VlcTable dc_luma_table, dc_chroma_table, dct_zero_table, dct_one_table;

static void build_table(VlcTable *table, const Vlc *codes, int bits) {
    int i;

    table->bits = bits;
    table->lookup = calloc((size_t)1 << bits, sizeof(uint32_t));
    if (table->lookup == NULL) {
        PORT_FATAL("movie: no memory for the decoder's tables");
    }
    for (i = 0; codes[i].code != NULL; i++) {
        int length = (int)strlen(codes[i].code);
        uint32_t prefix = 0;
        uint32_t fill;
        int j;

        for (j = 0; j < length; j++) {
            prefix = prefix << 1 | (uint32_t)(codes[i].code[j] - '0');
        }
        for (fill = 0; fill < (1u << (bits - length)); fill++) {
            uint32_t *entry = &table->lookup[prefix << (bits - length) | fill];

            if (*entry != 0) {
                PORT_FATAL("movie: the code %s is a prefix of another", codes[i].code);
            }
            *entry = (uint32_t)length << 24 | (uint32_t)codes[i].value;
        }
    }
}

static void build_tables(void) {
    static int built;

    if (built) {
        return;
    }
    built = 1;
    build_table(&mba_table, mba_codes, 11);
    build_table(&mb_i_table, mb_type_i, 2);
    build_table(&mb_p_table, mb_type_p, 6);
    build_table(&mb_b_table, mb_type_b, 6);
    build_table(&cbp_table, cbp_codes, 9);
    build_table(&motion_table, motion_codes, 10);
    build_table(&dc_luma_table, dc_luma_codes, 9);
    build_table(&dc_chroma_table, dc_chroma_codes, 10);
    build_table(&dct_zero_table, dct_zero_codes, 16);
    build_table(&dct_one_table, dct_one_codes, 16);
}

/* ---- Tables of the standard ----------------------------------------------------------------- */
/* 7.3 the zigzag and alternate scans: scan position -> raster index */
static const uint8_t zigzag_scan[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const uint8_t alternate_scan[64] = {
    0,  8,  16, 24, 1,  9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49, 41, 33, 26, 18, 3,  11,
    4,  12, 19, 27, 34, 42, 50, 58, 35, 43, 51, 59, 20, 28, 5,  13, 6,  14, 21, 29, 36, 44,
    52, 60, 37, 45, 53, 61, 22, 30, 7,  15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};
/* 6.3.11 the default intra matrix, in raster order */
static const uint8_t default_intra_matrix[64] = {
    8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
    34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
    35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};
/* 7.4.2.2 quantiser_scale for q_scale_type 1 */
static const uint8_t non_linear_scale[32] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12, 14, 16, 18, 20,  22,
    24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};

/* The inverse DCT's basis: c(u)/2 * cos((2x+1)u pi/16), c(0) = 1/sqrt(2), times 8192 and rounded
   (row u, column x). */
static const int32_t idct_basis[8][8] = {
    {2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896},
    {4017, 3406, 2276, 799, -799, -2276, -3406, -4017},
    {3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784},
    {3406, -799, -4017, -2276, 2276, 4017, 799, -3406},
    {2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896},
    {2276, -4017, 799, 3406, -3406, -799, 4017, -2276},
    {1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567},
    {799, -2276, 3406, -4017, 4017, -3406, 2276, -799},
};

/* ---- The bit reader --------------------------------------------------------------------------- */
static void fill(Mpeg2Decoder *d, int n) {
    while (d->bits < n) {
        if (d->qword_pos == 16) {
            d->read_qword(d->read_ctx, d->qword);
            d->qword_pos = 0;
        }
        d->cache |= (uint64_t)d->qword[d->qword_pos++] << (56 - d->bits);
        d->bits += 8;
    }
}

/* the next n bits (1 to 32), not used */
static uint32_t peek(Mpeg2Decoder *d, int n) {
    fill(d, n);
    return (uint32_t)(d->cache >> (64 - n));
}

static void skip(Mpeg2Decoder *d, int n) {
    if (n == 0) {
        return;
    }
    fill(d, n);
    d->cache <<= n;
    d->bits -= n;
}

static uint32_t get(Mpeg2Decoder *d, int n) {
    uint32_t value;

    if (n == 0) {
        return 0;
    }
    value = peek(d, n);
    d->cache <<= n;
    d->bits -= n;
    return value;
}

/* A code of the table; -1 where none starts. */
static int get_vlc(Mpeg2Decoder *d, const VlcTable *table) {
    uint32_t entry = table->lookup[peek(d, table->bits)];

    if (entry == 0) {
        return -1;
    }
    skip(d, (int)(entry >> 24));
    return (int)(entry & 0xFFFFFF);
}

/* 5.2.3 next_start_code: to a byte boundary, then over the zero bytes before the next 00 00 01. */
static void next_start_code(Mpeg2Decoder *d) {
    skip(d, d->bits & 7);
    while (peek(d, 24) != 1) {
        skip(d, 8);
    }
}

int mpeg2_unused_bits(const Mpeg2Decoder *d) {
    return d->bits + (16 - d->qword_pos) * 8;
}

/* ---- Set-up -------------------------------------------------------------------------------- */
void mpeg2_init(Mpeg2Decoder *d, Mpeg2ReadQword read_qword, void *ctx) {
    int i;

    build_tables();
    memset(d, 0, sizeof(*d));
    d->read_qword = read_qword;
    d->read_ctx = ctx;
    for (i = 0; i < 3; i++) {
        d->frames[i] = calloc(1, sizeof(Mpeg2Frame));
        if (d->frames[i] == NULL) {
            PORT_FATAL("movie: no memory for the decoder's pictures");
        }
    }
    mpeg2_reset(d);
}

void mpeg2_free(Mpeg2Decoder *d) {
    int i;

    for (i = 0; i < 3; i++) {
        free(d->frames[i]);
        d->frames[i] = NULL;
    }
}

void mpeg2_reset(Mpeg2Decoder *d) {
    d->cache = 0;
    d->bits = 0;
    d->qword_pos = 16;
    d->have_sequence = 0;
    d->forward = -1;
    d->backward = -1;
    d->current = 0;
}

/* ---- Headers (6.2) --------------------------------------------------------------------------- */
static void read_matrix(Mpeg2Decoder *d, uint8_t *matrix) {
    int i;

    for (i = 0; i < 64; i++) {
        matrix[zigzag_scan[i]] = (uint8_t)get(d, 8);
    }
}

static void sequence_header(Mpeg2Decoder *d) {
    int i;

    d->width = (int)get(d, 12);
    d->height = (int)get(d, 12);
    skip(d, 4 + 4);       /* aspect_ratio_information, frame_rate_code */
    skip(d, 18 + 1 + 10); /* bit_rate_value, marker_bit, vbv_buffer_size_value */
    skip(d, 1);           /* constrained_parameters_flag */
    if (get(d, 1)) {
        read_matrix(d, d->intra_matrix);
    } else {
        memcpy(d->intra_matrix, default_intra_matrix, 64);
    }
    if (get(d, 1)) {
        read_matrix(d, d->non_intra_matrix);
    } else {
        for (i = 0; i < 64; i++) {
            d->non_intra_matrix[i] = 16;
        }
    }
    if (d->width > MPEG2_MAX_WIDTH || d->height > MPEG2_MAX_HEIGHT || d->width <= 0 || d->height <= 0) {
        PORT_FATAL("movie: a %dx%d picture is too big for the decoder", d->width, d->height);
    }
    d->mb_width = (d->width + 15) / 16;
    d->mb_height = (d->height + 15) / 16;
    d->have_sequence = 1;
}

static void extension(Mpeg2Decoder *d) {
    int id = (int)get(d, 4);

    switch (id) {
    case 1: /* sequence extension */
        skip(d, 8);
        if (!get(d, 1)) {
            PORT_FATAL("movie: an interlaced sequence (the decoder has frame pictures only)");
        }
        if (get(d, 2) != 1) {
            PORT_FATAL("movie: not 4:2:0");
        }
        break;
    case 3: /* quant matrix extension */
        if (get(d, 1)) {
            read_matrix(d, d->intra_matrix);
        }
        if (get(d, 1)) {
            read_matrix(d, d->non_intra_matrix);
        }
        break;
    case 8: /* picture coding extension */
        d->f_code[0][0] = (int)get(d, 4);
        d->f_code[0][1] = (int)get(d, 4);
        d->f_code[1][0] = (int)get(d, 4);
        d->f_code[1][1] = (int)get(d, 4);
        d->intra_dc_precision = (int)get(d, 2);
        d->picture_structure = (int)get(d, 2);
        skip(d, 1); /* top_field_first */
        d->frame_pred_frame_dct = (int)get(d, 1);
        d->concealment_vectors = (int)get(d, 1);
        d->q_scale_type = (int)get(d, 1);
        d->intra_vlc_format = (int)get(d, 1);
        d->alternate_scan = (int)get(d, 1);
        if (d->picture_structure != 3 || !d->frame_pred_frame_dct) {
            PORT_FATAL("movie: field pictures or field prediction (picture_structure %d, frame_pred_frame_dct %d)",
                       d->picture_structure, d->frame_pred_frame_dct);
        }
        break;
    default: /* the display extensions and the rest: nothing the picture needs */
        break;
    }
}

static void picture_header(Mpeg2Decoder *d) {
    skip(d, 10); /* temporal_reference */
    d->type = (int)get(d, 3);
    skip(d, 16); /* vbv_delay */
    if (d->type == MPEG2_P || d->type == MPEG2_B) {
        skip(d, 4); /* full_pel_forward_vector, forward_f_code: 7 in MPEG-2 */
    }
    if (d->type == MPEG2_B) {
        skip(d, 4);
    }
    while (get(d, 1)) { /* extra_bit_picture, extra_information_picture */
        skip(d, 8);
    }
    /* an MPEG-1 picture keeps these; MPEG-2's picture coding extension sets them */
    d->picture_structure = 3;
    d->frame_pred_frame_dct = 1;
}

int mpeg2_next_picture(Mpeg2Decoder *d) {
    int in_picture = 0;

    for (;;) {
        uint32_t code;

        next_start_code(d);
        code = peek(d, 32) & 0xFF;
        if (in_picture && code >= 0x01 && code <= 0xAF) {
            return d->type; /* the first slice: left for mpeg2_decode_picture */
        }
        skip(d, 32);
        switch (code) {
        case 0x00:
            picture_header(d);
            in_picture = d->have_sequence && d->type >= MPEG2_I && d->type <= MPEG2_B;
            break;
        case 0xB3:
            sequence_header(d);
            break;
        case 0xB5:
            extension(d);
            break;
        case 0xB7:
            return 0;
        default: /* group of pictures, user data, a slice of a picture skipped */
            break;
        }
    }
}

/* ---- Blocks (7.2 to 7.5) ------------------------------------------------------------------ */
/* The inverse DCT of a block of coefficients, rounded to integers (7.5). */
static void idct(int16_t *block, int16_t *out) {
    int32_t rows[64];
    int nonzero_rows = 0;
    int u, v, x, y;

    for (y = 0; y < 8; y++) {
        const int16_t *in = block + y * 8;
        int32_t *row = rows + y * 8;

        if ((in[0] | in[1] | in[2] | in[3] | in[4] | in[5] | in[6] | in[7]) == 0) {
            continue;
        }
        nonzero_rows |= 1 << y;
        for (x = 0; x < 8; x++) {
            int32_t sum = 0;

            for (u = 0; u < 8; u++) {
                sum += in[u] * idct_basis[u][x];
            }
            row[x] = (sum + (1 << 9)) >> 10;
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            int64_t sum = 0;

            for (v = 0; v < 8; v++) {
                if (nonzero_rows & (1 << v)) {
                    sum += (int64_t)rows[v * 8 + x] * idct_basis[v][y];
                }
            }
            out[y * 8 + x] = (int16_t)((sum + (1 << 15)) >> 16);
        }
    }
}

/* Reads and inverse-quantises one block's coefficients into d->block (7.2 to 7.4); 0 on a bad code. */
static int decode_block(Mpeg2Decoder *d, int intra, int component) {
    const VlcTable *table = (intra && d->intra_vlc_format) ? &dct_one_table : &dct_zero_table;
    const uint8_t *matrix = intra ? d->intra_matrix : d->non_intra_matrix;
    const uint8_t *scan = d->alternate_scan ? alternate_scan : zigzag_scan;
    int16_t *block = d->block;
    int scale = d->quantiser_scale;
    int sum = 0;
    int i = 0;

    memset(block, 0, sizeof(d->block));
    if (intra) {
        int size = get_vlc(d, component == 0 ? &dc_luma_table : &dc_chroma_table);
        int diff = 0;

        if (size < 0) {
            return 0;
        }
        if (size > 0) {
            diff = (int)get(d, size);
            if (diff < (1 << (size - 1))) {
                diff -= (1 << size) - 1;
            }
        }
        d->dc_pred[component] += diff;
        block[0] = (int16_t)(d->dc_pred[component] << (3 - d->intra_dc_precision));
        sum = block[0];
        i = 1;
    } else if (peek(d, 1)) {
        /* the first coefficient's code "1s": run 0, level 1 */
        int level = 1;
        int value;

        skip(d, 1);
        if (get(d, 1)) {
            level = -1;
        }
        value = ((2 * level + (level > 0 ? 1 : -1)) * matrix[scan[0]] * scale) / 32;
        block[scan[0]] = (int16_t)value;
        sum = value;
        i = 1;
    }
    for (;;) {
        int code = get_vlc(d, table);
        int run, level, value, position;

        if (code < 0) {
            return 0;
        }
        if (code == DCT_EOB) {
            break;
        }
        if (code == DCT_ESCAPE) {
            run = (int)get(d, 6);
            level = (int)get(d, 12);
            if (level & 0x800) {
                level -= 0x1000;
            }
            if (level == 0 || level == -2048) {
                return 0;
            }
        } else {
            run = code & 0xFF;
            level = code >> 8;
            if (get(d, 1)) {
                level = -level;
            }
        }
        i += run;
        if (i > 63) {
            return 0;
        }
        position = scan[i];
        if (intra) {
            value = (2 * level * matrix[position] * scale) / 32;
        } else {
            value = ((2 * level + (level > 0 ? 1 : -1)) * matrix[position] * scale) / 32;
        }
        if (value > 2047) {
            value = 2047;
        } else if (value < -2048) {
            value = -2048;
        }
        block[position] = (int16_t)value;
        sum += value;
        i++;
    }
    /* 7.4.4 mismatch control */
    if ((sum & 1) == 0) {
        block[63] ^= 1;
    }
    return 1;
}

static uint8_t clamp_pixel(int value) {
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

/* ---- Motion compensation (7.6) ------------------------------------------------------------- */
/* One prediction of a size x size block at (x, y) of a plane, with a vector in half samples; into
   `out`, or averaged with what is there (the second of a bidirectional prediction). */
static void predict_block(const uint8_t *plane, int stride, int plane_width, int plane_height, int x, int y,
                          int vector_x, int vector_y, int size, uint8_t *out, int average) {
    int ix = x + (vector_x >> 1);
    int iy = y + (vector_y >> 1);
    int half_x = vector_x & 1;
    int half_y = vector_y & 1;
    int i, j;

    /* a vector that points out of the picture is an error in the stream: keep to the picture */
    if (ix < 0) {
        ix = 0;
    } else if (ix > plane_width - size - half_x) {
        ix = plane_width - size - half_x;
    }
    if (iy < 0) {
        iy = 0;
    } else if (iy > plane_height - size - half_y) {
        iy = plane_height - size - half_y;
    }
    for (j = 0; j < size; j++) {
        const uint8_t *a = plane + (iy + j) * stride + ix;
        const uint8_t *b = a + half_y * stride;

        for (i = 0; i < size; i++) {
            int value;

            if (half_x && half_y) {
                value = (a[i] + a[i + 1] + b[i] + b[i + 1] + 2) >> 2;
            } else if (half_x) {
                value = (a[i] + a[i + 1] + 1) >> 1;
            } else if (half_y) {
                value = (a[i] + b[i] + 1) >> 1;
            } else {
                value = a[i];
            }
            if (average) {
                value = (out[j * size + i] + value + 1) >> 1;
            }
            out[j * size + i] = (uint8_t)value;
        }
    }
}

/* A macroblock's prediction from one reference picture, luma and both chroma blocks. */
static void predict_macroblock(Mpeg2Decoder *d, const Mpeg2Frame *ref, int mb_x, int mb_y, const int *vector,
                               uint8_t *luma, uint8_t *cb, uint8_t *cr, int average) {
    int width = d->mb_width * 16;
    int height = d->mb_height * 16;
    /* 7.6.3.7: the chroma vector is half the luma one, truncated toward zero */
    int chroma_x = vector[0] / 2;
    int chroma_y = vector[1] / 2;

    predict_block(ref->y, width, width, height, mb_x * 16, mb_y * 16, vector[0], vector[1], 16, luma, average);
    predict_block(ref->cb, width / 2, width / 2, height / 2, mb_x * 8, mb_y * 8, chroma_x, chroma_y, 8, cb, average);
    predict_block(ref->cr, width / 2, width / 2, height / 2, mb_x * 8, mb_y * 8, chroma_x, chroma_y, 8, cr, average);
}

/* 7.6.3.1: one motion vector component, from its prediction */
static int motion_component(Mpeg2Decoder *d, int f_code, int prediction) {
    int r_size = f_code - 1;
    int f = 1 << r_size;
    int code = get_vlc(d, &motion_table);
    int delta;
    int vector;

    if (code < 0) {
        return prediction;
    }
    if (code != 0 && get(d, 1)) {
        code = -code;
    }
    if (f == 1 || code == 0) {
        delta = code;
    } else {
        int residual = (int)get(d, r_size);

        delta = ((code < 0 ? -code : code) - 1) * f + residual + 1;
        if (code < 0) {
            delta = -delta;
        }
    }
    vector = prediction + delta;
    if (vector < -16 * f) {
        vector += 32 * f;
    } else if (vector > 16 * f - 1) {
        vector -= 32 * f;
    }
    return vector;
}

/* A frame prediction's one vector (s: 0 forward, 1 backward); both predictions of it follow it. */
static void motion_vector(Mpeg2Decoder *d, int s) {
    int t;

    for (t = 0; t < 2; t++) {
        int vector = motion_component(d, d->f_code[s][t], d->pmv[0][s][t]);

        d->pmv[0][s][t] = vector;
        d->pmv[1][s][t] = vector;
    }
}

static void reset_dc(Mpeg2Decoder *d) {
    d->dc_pred[0] = d->dc_pred[1] = d->dc_pred[2] = 1 << (7 + d->intra_dc_precision);
}

/* Writes a macroblock: its prediction (none when intra) plus the blocks coded. */
static int reconstruct(Mpeg2Decoder *d, int mb_x, int mb_y, int flags, int pattern) {
    Mpeg2Frame *frame = d->frames[d->current];
    int width = d->mb_width * 16;
    uint8_t luma[256], cb[64], cr[64];
    int16_t residual[64];
    int intra = flags & MB_INTRA;
    int b;

    if (!intra) {
        int average = 0;

        if (flags & MB_FORWARD || !(flags & MB_BACKWARD)) {
            const int zero[2] = {0, 0};
            int ref = d->type == MPEG2_B ? d->forward : d->backward;

            if (ref < 0) {
                ref = d->backward >= 0 ? d->backward : d->current;
            }
            predict_macroblock(d, d->frames[ref], mb_x, mb_y, (flags & MB_FORWARD) ? d->pmv[0][0] : zero, luma,
                               cb, cr, 0);
            average = 1;
        }
        if (flags & MB_BACKWARD) {
            int ref = d->backward >= 0 ? d->backward : d->current;

            predict_macroblock(d, d->frames[ref], mb_x, mb_y, d->pmv[0][1], luma, cb, cr, average);
        }
    }
    for (b = 0; b < 6; b++) {
        int coded = (pattern >> (5 - b)) & 1;
        uint8_t *dest;
        const uint8_t *pred;
        int stride;
        int i, j;

        if (b < 4) {
            dest = frame->y + (mb_y * 16 + (b >> 1) * 8) * width + mb_x * 16 + (b & 1) * 8;
            pred = luma + (b >> 1) * 8 * 16 + (b & 1) * 8;
            stride = width;
        } else {
            dest = (b == 4 ? frame->cb : frame->cr) + mb_y * 8 * (width / 2) + mb_x * 8;
            pred = b == 4 ? cb : cr;
            stride = width / 2;
        }
        if (coded) {
            if (!decode_block(d, intra, b < 4 ? 0 : b - 3)) {
                return 0;
            }
            idct(d->block, residual);
        }
        for (j = 0; j < 8; j++) {
            for (i = 0; i < 8; i++) {
                int value;

                if (intra) {
                    value = residual[j * 8 + i];
                } else {
                    value = pred[j * (b < 4 ? 16 : 8) + i];
                    if (coded) {
                        int r = residual[j * 8 + i];

                        value += r < -256 ? -256 : r > 255 ? 255 : r;
                    }
                }
                dest[j * stride + i] = clamp_pixel(value);
            }
        }
    }
    return 1;
}

/* 6.2.4 slice: its macroblocks, to the next start code. */
static void decode_slice(Mpeg2Decoder *d, int row) {
    const VlcTable *type_table = d->type == MPEG2_I ? &mb_i_table : d->type == MPEG2_P ? &mb_p_table : &mb_b_table;
    int mb_count = d->mb_width * d->mb_height;
    int address;
    int first = 1;

    d->quantiser_scale = d->q_scale_type ? non_linear_scale[get(d, 5)] : (int)get(d, 5) * 2;
    if (get(d, 1)) { /* intra_slice_flag, intra_slice, reserved_bits, then extra_bit_slice */
        skip(d, 8);
        while (get(d, 1)) {
            skip(d, 8);
        }
    }
    reset_dc(d);
    memset(d->pmv, 0, sizeof(d->pmv));
    address = row * d->mb_width - 1;
    d->last_mb_flags = 0;
    for (;;) {
        int increment = 0;
        int flags, pattern;

        for (;;) {
            int code = get_vlc(d, &mba_table);

            if (code < 0) {
                return;
            }
            if (code != MBA_ESCAPE) {
                increment += code;
                break;
            }
            increment += 33;
        }
        if (!first) {
            /* 7.6.6 skipped macroblocks */
            int skipped;

            for (skipped = 1; skipped < increment; skipped++) {
                int at = address + skipped;

                if (at >= mb_count) {
                    return;
                }
                reset_dc(d);
                if (d->type == MPEG2_P) {
                    memset(d->pmv, 0, sizeof(d->pmv));
                    reconstruct(d, at % d->mb_width, at / d->mb_width, MB_FORWARD, 0);
                } else {
                    reconstruct(d, at % d->mb_width, at / d->mb_width,
                                d->last_mb_flags & (MB_FORWARD | MB_BACKWARD), 0);
                }
            }
        }
        address += increment;
        first = 0;
        if (address >= mb_count) {
            return;
        }
        flags = get_vlc(d, type_table);
        if (flags < 0) {
            return;
        }
        if (flags & MB_QUANT) {
            int code = (int)get(d, 5);

            d->quantiser_scale = d->q_scale_type ? non_linear_scale[code] : code * 2;
        }
        if ((flags & MB_FORWARD) || ((flags & MB_INTRA) && d->concealment_vectors)) {
            motion_vector(d, 0);
        }
        if (flags & MB_BACKWARD) {
            motion_vector(d, 1);
        }
        if ((flags & MB_INTRA) && d->concealment_vectors) {
            skip(d, 1); /* marker_bit */
        }
        if (flags & MB_INTRA) {
            pattern = 63;
            if (!d->concealment_vectors) {
                memset(d->pmv, 0, sizeof(d->pmv));
            }
        } else {
            reset_dc(d);
            pattern = 0;
            if (flags & MB_PATTERN) {
                pattern = get_vlc(d, &cbp_table);
                if (pattern < 0) {
                    return;
                }
            }
            if (d->type == MPEG2_P && !(flags & MB_FORWARD)) {
                memset(d->pmv, 0, sizeof(d->pmv));
            }
        }
        if (!reconstruct(d, address % d->mb_width, address / d->mb_width, flags, pattern)) {
            return;
        }
        d->last_mb_flags = flags;
        if (peek(d, 23) == 0) {
            return;
        }
    }
}

void mpeg2_decode_picture(Mpeg2Decoder *d) {
    int i;

    /* the picture goes into the buffer neither reference is in */
    for (i = 0; i < 3; i++) {
        if (i != d->forward && i != d->backward) {
            d->current = i;
            break;
        }
    }
    for (;;) {
        uint32_t code;

        next_start_code(d);
        code = peek(d, 32) & 0xFF;
        if (code < 0x01 || code > 0xAF) {
            break;
        }
        skip(d, 32);
        if ((int)code <= d->mb_height) {
            decode_slice(d, (int)code - 1);
        }
    }
    if (d->type != MPEG2_B) {
        d->forward = d->backward;
        d->backward = d->current;
    }
}
