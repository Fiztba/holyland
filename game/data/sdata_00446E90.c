/* sdata_00446E90.c: the game's data from the decomp's sdata_00446E90.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "map/map.h"

/* 0x00446E90 */
int opening_movie_skipped;

/* 0x00446ED4 */
u32 frame_counter;

/* 0x00446EF0 */
int D_00446EF0;

/* 0x00446F08 */
MapSlotMemory *map_slot_area;

/* 0x00446F0C */
int packet_area_size;

/* 0x00446F10 */
int effect_area_size;

/* 0x00446F14 */
int entity_pool_area_size;

/* 0x00446F18 */
int held_weapon_area_size;

/* 0x00446F1C */
u32 map_work_area_size;

/* 0x00446F20 */
int map_file_area_size;

/* 0x00446F24 */
int hud_area_size;

/* 0x00446F28 */
int item_effect_area_size;

/* 0x00446F2C */
int D_00446F2C;

/* 0x00446F38 */
sptr menu_heap;

/* 0x00446F50 */
char *map_file_area;

/* 0x00446F54 */
char *map_work_area;

/* 0x00446F58 */
sptr packet_area;

/* 0x00446F5C */
char *effect_area;

/* 0x00446F60 */
char *entity_pool_area;

/* 0x00446F64 */
char *held_weapon_area;

/* 0x00446F68 */
sptr trade_menu_arena;

/* 0x00446F6C */
char *hud_area;

/* 0x00446F70 */
char *item_effect_area;

/* 0x00446F74 */
char *sound_bank_area;

/* 0x00446F78 */
int context1_buffer_half;

/* 0x00446F7C */
int context2_buffer_half;

/* 0x00446F80 */
int D_00446F80;

/* 0x00446F84 */
int timer_overflow_count;

/* 0x00446F90 */
char empty_module_args[4];

/* 0x00446FA0 */
int vsync_even_field;

/* 0x00446FA4 */
f32 D_00446FA4[1] = { -0.0f };

/* 0x00446FA8 */
char tex0_tw_format[8] = "TW[%u] ";

/* 0x00446FB0 */
char tex0_th_format[8] = "TH[%u] ";

/* 0x00446FB8 */
const f32 D_00446FB8[1] = { -0.0f };

/* 0x00446FBC */
const f32 D_00446FBC[1] = { -0.0f };

/* 0x00446FC0 */
const f32 D_00446FC0[1] = { -0.0f };

/* 0x00446FC4 */
const f32 D_00446FC4[1] = { -0.0f };

/* 0x00446FC8 */
const f32 D_00446FC8[1] = { -0.0f };

/* 0x00446FCC */
const f32 D_00446FCC[1] = { -0.0f };
