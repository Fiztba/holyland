/* sdata_00446854.c: the game's data from the decomp's sdata_00446854.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"

/* 0x00446858 */
int bgm_load_step;

/* 0x0044685C */
int bgm_load_can_yield;

/* 0x00446860 */
int bgm_fade_level;

/* 0x00446864 */
int entity_bank_load_step;

/* 0x00446868 */
int map_bank_load_step;

/* 0x00446880 */
u8 voice_range_cursors[8] = { 3, 5, 17, 57, 83, 99 };

/* 0x00446888 */
int tagged_bend;

/* 0x0044688C */
int tagged_bend_high = 2730;

/* 0x00446890 */
int tagged_bend_low = -2730;

/* 0x00446894 */
char tagged_bend_falling;

/* 0x00446895 */
u8 map_reverb_slot = 255;

/* 0x00446896 */
u8 bgm_reverb_slot = 255;

/* 0x004468A4 */
int heap_file_read_step;

/* 0x004468A8 */
s32 map_bank_file_read_step;

/* 0x004468AC */
s32 sound_file_load_step;

/* 0x004468B4 */
int D_004468B4;

/* 0x004468BC */
int D_004468BC;

/* 0x004468C0 */
float D_004468C0;

/* 0x004468C4 */
int D_004468C4;
