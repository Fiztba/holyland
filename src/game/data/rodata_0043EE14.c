/* rodata_0043EE14.c: the game's data from the decomp's rodata_0043EE14.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "sound/sound.h"
#include "sound/sound_game.h"

/* 0x0043EE18 */
char rev_para_path[24] = "snd/rev_para.dat";

/* 0x0043EE30 */
/* entries 6 on take no bank: (u8, float, float, float); Sound_PlayFalloff calls them through that type */
void (*falloff_sound_players[18])(u8, int, float, float, float) = {
    Sound_PlayLinear,
    Sound_PlaySteep,
    Sound_PlayRound,
    Sound_PlayLinearSteady,
    Sound_PlaySteepSteady,
    Sound_PlayRoundSteady,
    (void (*)(u8, int, float, float, float))Sound_PlaySfxLinear,
    (void (*)(u8, int, float, float, float))Sound_PlaySfxSteep,
    (void (*)(u8, int, float, float, float))Sound_PlaySfxRound,
    0,
    0,
    0,
    (void (*)(u8, int, float, float, float))Sound_PlayMapLinear,
    (void (*)(u8, int, float, float, float))Sound_PlayMapSteep,
    (void (*)(u8, int, float, float, float))Sound_PlayMapRound,
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
