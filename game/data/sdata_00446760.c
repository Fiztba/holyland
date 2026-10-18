/* sdata_00446760.c: the game's data from the decomp's sdata_00446760.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "gfx/screen.h"
#include "gfx/texture.h"
#include "map/map.h"

/* 0x00446760 */
int screen_uv_frame;

/* 0x00446764 */
int cut_triangle_count;

/* 0x00446768 */
f32 D_00446768[1] = { 3.4028235e+38f };

/* 0x0044676C */
f32 D_0044676C[1] = { 3.4028235e+38f };

/* 0x00446770 */
int current_map_slot;

/* 0x00446778 */
MapDrawRecord *record_draw_lists[2];

/* 0x00446780 */
MapDrawRecord *late_record_draw_lists[2];

/* 0x00446788 */
AreaNode *record_boxes;

/* 0x0044678C */
AreaNode *backdrop_record_boxes;

/* 0x00446790 */
TextureTableHeader *D_00446790;

/* 0x00446794 */
int map_load_committed;

/* 0x00446798 */
f32 D_00446798[1] = { 3.4028235e+38f };

/* 0x0044679C */
f32 D_0044679C[1] = { -3.4028235e+38f };
