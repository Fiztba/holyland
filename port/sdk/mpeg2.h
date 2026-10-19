/* An MPEG-2 video decoder for the movies (port/sdk/libmpeg.c), written for the port.

   It decodes what the PS2's IPU and libmpeg decode together, for the streams the game's PSS files
   carry: MPEG-2 Main Profile, 4:2:0, frame pictures with frame prediction and frame DCT
   (frame_pred_frame_dct), I, P and B pictures. Field pictures and field prediction stop the program
   with a message: none of KF4's movies has them.

   The decoder only parses and reconstructs. libmpeg.c puts the pictures in display order, as libmpeg
   does, and converts them to RGB as the IPU's CSC does. The stream comes in 16-byte quadwords, as DMA
   channel 4 feeds the IPU; everything is integer arithmetic, so a picture is the same on every
   compiler and host. */
#ifndef PORT_SDK_MPEG2_H
#define PORT_SDK_MPEG2_H

#include <stdint.h>

#define MPEG2_MAX_WIDTH 720
#define MPEG2_MAX_HEIGHT 576

typedef enum { MPEG2_I = 1, MPEG2_P = 2, MPEG2_B = 3 } Mpeg2PictureType;

/* One decoded picture: 4:2:0 planes, each row `width` (luma) or `width / 2` (chroma) bytes. */
typedef struct {
    uint8_t y[MPEG2_MAX_WIDTH * MPEG2_MAX_HEIGHT];
    uint8_t cb[MPEG2_MAX_WIDTH * MPEG2_MAX_HEIGHT / 4];
    uint8_t cr[MPEG2_MAX_WIDTH * MPEG2_MAX_HEIGHT / 4];
} Mpeg2Frame;

/* Gives the next 16 bytes of the stream. It may wait for them (libmpeg's "no data" callback). */
typedef void (*Mpeg2ReadQword)(void *ctx, uint8_t qword[16]);

typedef struct {
    Mpeg2ReadQword read_qword;
    void *read_ctx;
    /* the bit reader: `bits` valid bits at the top of `cache`, then what is left of the quadword */
    uint64_t cache;
    int bits;
    uint8_t qword[16];
    int qword_pos;

    /* the sequence */
    int have_sequence;
    int width, height, mb_width, mb_height;
    uint8_t intra_matrix[64], non_intra_matrix[64]; /* in raster order */

    /* the picture */
    int type;
    int f_code[2][2];
    int intra_dc_precision, picture_structure, frame_pred_frame_dct, concealment_vectors;
    int q_scale_type, intra_vlc_format, alternate_scan;

    /* the slice and macroblock */
    int quantiser_scale;
    int dc_pred[3];
    int pmv[2][2][2]; /* [r first/second][s forward/backward][t x/y] */
    int last_mb_flags;

    /* the three picture buffers: the forward and backward references and the picture decoded now */
    Mpeg2Frame *frames[3];
    int forward, backward, current;

    int16_t block[64];
} Mpeg2Decoder;

/* Allocates the buffers; `read_qword` is the stream. */
void mpeg2_init(Mpeg2Decoder *d, Mpeg2ReadQword read_qword, void *ctx);
void mpeg2_free(Mpeg2Decoder *d);
/* Forgets the stream: the bit reader, the sequence and the references (sceMpegReset). */
void mpeg2_reset(Mpeg2Decoder *d);
/* Reads the headers up to the next picture's slices. Returns the picture's type, or 0 at a
   sequence end code. */
int mpeg2_next_picture(Mpeg2Decoder *d);
/* Decodes the picture whose header mpeg2_next_picture read, into d->frames[d->current]. For an I or
   P picture, the backward reference before the call is the picture to show next (libmpeg.c). */
void mpeg2_decode_picture(Mpeg2Decoder *d);
/* The bits read from the stream that the decoder has not used yet. */
int mpeg2_unused_bits(const Mpeg2Decoder *d);

#endif
