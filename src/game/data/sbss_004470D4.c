/* sbss_004470D4.c: the game's data from the decomp's sbss_004470D4.sbss.s, defined in C for the PC (splat had it in asm) */
#include "common.h"

/* 0x004470D4 */
int heap_file_handle;

/* 0x004470DC */
s32 map_bank_file_handle;

/* 0x004470E0 */
s8 map_reverb_set;

/* 0x004470E1 */
char map_sounds_enabled;

/* 0x004470E2 */
char reverb_change_pending;

/* 0x004470E3 */
s8 map_reverb_volume;

/* 0x004470E4 */
u8 loaded_bgm;

/* 0x004470E5 */
u8 bgm_load_running;

/* 0x004470E6 */
s8 positional_sounds_muted;

/* 0x004470E7 */
u8 loading_bgm;

/* 0x004470E8 */
int bgm_bd_size;

/* 0x004470EC */
int bgm_hd_size;

/* 0x004470F0 */
int bgm_sq_size;

/* 0x004470F4 */
int sound_heap_next;
