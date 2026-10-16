/* sbss.c: the game's data from the decomp's sbss.sbss.s, defined in C for the PC (splat had it in asm) */
#include "common.h"

/* 0x00447000 */
sptr D_00447000[4]; /* four PS2 words: [1] and [2] hold pointers (core/file.h) */

/* 0x00447010 */
int fade_frames_left;

/* 0x00447014 */
int fade_frames;
