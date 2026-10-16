/* rodata_0043F588.c: the game's data from the decomp's rodata_0043F588.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "model/model.h"

/* 0x0043F588 */
char arm_phong_texture_path[24] = "item_arm/arm_phong.tx2";

/* 0x0043F5A0 */
char item_arm_path_format[24] = "item_arm/i%4.4d.arm";

/* 0x0043F5B8 */
char default_held_item_path[24] = "item_arm/i1001.arm";

/* 0x0043F5D0 */
char default_arm_path[24] = "item_arm/i5001.arm";

/* 0x0043F5E8 */
char hit_sphere_pitfall_message[40] = "Hit HitSpherePitfallObj()";

/* 0x0043F610 */
const Vector4Arg enemy_drop_scale __attribute__((aligned(16))) = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f };
