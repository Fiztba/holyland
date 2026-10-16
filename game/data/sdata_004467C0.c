/* sdata_004467C0.c: the game's data from the decomp's sdata_004467C0.sdata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "entity/entity.h"

extern char character_params[]; /* entity/entity_pool.c */

/* 0x004467C0 */
char *entity_params = character_params;

/* 0x004467C4 */
int chr_file_slot = -1;

/* 0x004467C8 */
char chr_directory[8] = "chr/";

/* 0x004467D0 */
EntitySpawnInfo *map_spawn_infos[2];

/* 0x004467D8 */
s32 map_spawn_counts[2];

/* 0x004467E0 */
Entity *active_entities_head;

/* 0x004467E4 */
Entity *active_entities_tail;

/* 0x004467E8 */
int screen_textured_entity_type;

/* 0x004467F8 */
const f32 D_004467F8[1] = { 3.4028235e+38f };

/* 0x004467FC */
f32 D_004467FC[1] = { 3.4028235e+38f };

/* 0x00446800 */
f32 D_00446800[1] = { 3.4028235e+38f };

/* 0x00446804 */
const f32 D_00446804[1] = { 3.4028235e+38f };

/* 0x00446808 */
const f32 D_00446808[1] = { 3.4028235e+38f };

/* 0x0044680C */
const f32 D_0044680C[1] = { 3.4028235e+38f };

/* 0x00446810 */
f32 D_00446810[1] = { 3.4028235e+38f };

/* 0x00446814 */
f32 D_00446814[1] = { 3.4028235e+38f };

/* 0x00446818 */
f32 D_00446818[1] = { 3.4028235e+38f };

/* 0x0044681C */
f32 D_0044681C[1] = { 3.4028235e+38f };

/* 0x00446820 */
const f32 D_00446820[1] = { 3.4028235e+38f };

/* 0x00446824 */
f32 D_00446824[1] = { 3.4028235e+38f };

/* 0x00446828 */
f32 D_00446828[1] = { 3.4028235e+38f };

/* 0x0044682C */
const f32 D_0044682C[1] = { 3.4028235e+38f };

/* 0x00446830 */
f32 D_00446830[1] = { 3.4028235e+38f };

/* 0x00446834 */
f32 D_00446834[1] = { 3.4028235e+38f };
