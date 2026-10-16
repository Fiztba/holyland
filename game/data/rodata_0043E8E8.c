/* rodata_0043E8E8.c: the game's data from the decomp's rodata_0043E8E8.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "entity/entity.h"

/* 0x0043E8E8 */
EntityFrameTable5 ancient_king_near_shot_frames = { .frames = { 30, 33, 35, 38, 40 } };

/* 0x0043E900 */
EntityFrameTable ancient_king_bone_shot_frames __attribute__((aligned(16))) = { .frames = { 22, 23, 24, 25 } };
