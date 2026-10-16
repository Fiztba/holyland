/* rodata_0043E88C.c: the game's data from the decomp's rodata_0043E88C.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "entity/entity.h"
#include "model/model.h"

/* 0x0043E890 */
EntityIdTable stone_guardian_spawn_ids = { .ids = { 10, 11, 12, 13, 14 } };

/* 0x0043E8B0 */
Qword living_armor_look_limits __attribute__((aligned(16))) = { .unk00 = 0x3F060A92BF060A92ULL, .unk08 = 0x3C8EFA35BC8EFA35ULL };

/* 0x0043E8C0 */
char golden_statue_arrived_message[16] = "tuitayo\n";
