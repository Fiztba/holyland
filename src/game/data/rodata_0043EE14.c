/* rodata_0043EE14.c: the game's data from the decomp's rodata_0043EE14.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "sound/sound.h"
#include "sound/sound_game.h"

/* 0x0043EE18 */
char rev_para_path[24] = "snd/rev_para.dat";

/* 0x0043EE30 */
int (*falloff_sound_players[18])(int, int) = {
    (int (*)(int, int))Sound_PlayLinear,
    (int (*)(int, int))Sound_PlaySteep,
    (int (*)(int, int))Sound_PlayRound,
    (int (*)(int, int))Sound_PlayLinearSteady,
    (int (*)(int, int))Sound_PlaySteepSteady,
    (int (*)(int, int))Sound_PlayRoundSteady,
    (int (*)(int, int))Sound_PlaySfxLinear,
    (int (*)(int, int))Sound_PlaySfxSteep,
    (int (*)(int, int))Sound_PlaySfxRound,
    0,
    0,
    0,
    (int (*)(int, int))Sound_PlayMapLinear,
    (int (*)(int, int))Sound_PlayMapSteep,
    (int (*)(int, int))Sound_PlayMapRound,
};

/* 0x0043EE78 */
SoundScriptState map_reverb_cue_indices = { .unk00 = 3, .pad02 = {
    3, 0, 2, 0, 1, 1, 0, 0, 0, 1, 1, 3, 0, 1, 3, 1,
    1, 1, 0, 1, 0, 0, 1, 1, 1, 3, 1, 1, 2, 3, 3, 1,
    2, 1, 2, 2, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 3, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 1,
} };
