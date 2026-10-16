/* rodata_0043EF58.c: the game's data from the decomp's rodata_0043EF58.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "collision/collision.h"
#include "core/game.h"
#include "model/model.h"

/* 0x0043EF60 */
s64 light_point_start_target[2] __attribute__((aligned(16))) = { 0x4300000043000000LL, 0x4300000043000000LL };

/* 0x0043EF70 */
DebugStartMenuSpacer debug_start_menu_spacer = { .text = " \n\n\n\n \n\n\n\n \n\n\n\n \n\n\n\n \n\n\n\n" };

/* 0x0043EF90 */
DebugStartMenuPrompt debug_start_menu_prompt = { .text = "                            PUSH START                              \n" };

/* 0x0043EFD8 */
DebugStartMenuItems debug_start_menu_items = { .text = {
    "                            NEW GAME                                \n",
    "                            LOAD GAME                               \n",
    "                            OPTION GAME                             \n",
    "                            KEY ASSIGN                              \n",
} };

/* 0x0043F1D8 */
char debug_map_format[24] = "MAP %3.3d";

/* 0x0043F1F0 */
CollisionListPick4 map_hit_strip_lists __attribute__((aligned(16))) = { .lists = { 0, 1, 2, 5 } };

/* 0x0043F200 */
const VuVector4 y_axis_vector __attribute__((aligned(16))) = { .y = 1.0f, .w = 1.0f };

/* 0x0043F210 */
char last_boss_hits_format[24] = "g_dmgLassBossCnt[%d]\n";

/* 0x0043F228 */
char hp_halved_format[32] = "\xA3\xC8\xA3\xD0\xC8\xBE\xCA\xAC\xA1\xAA\xA1\xAA %d / 2 = %d\n";

/* 0x0043F248 */
char magic_param_path[24] = "param/magic.prm";

/* 0x0043F260 */
const s8 yellow_tint_pulse[30] = { 16, 32, 48, 64, 64, 56, 48, 40, 32, 24, 16, 8 };
