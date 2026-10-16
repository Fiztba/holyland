/* rodata.c: the game's data from the decomp's rodata.rodata.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/game.h"
#include "gfx/gs.h"
#include "player/player.h"

/* 0x0043D9B0 */
const DefinitionByteSet initial_trade_stock_1 = { .bytes = {
    0, 1, 0, 0, 0, 0, 255, 0, 0, 255, 0, 0, 5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
} };

/* 0x0043DB30 */
const DefinitionByteSet initial_trade_stock_2 = { .bytes = {
    0, 0, 0, 0, 0, 0, 30, 30, 0, 5, 20, 0, 10, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1,
} };

/* 0x0043DCB0 */
const DefinitionByteSet initial_trade_stock_3 = { .bytes = {
    0, 0, 0, 0, 0, 0, 255, 100, 0, 255, 50, 0, 50, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1,
} };

/* 0x0043DE30 */
const DefinitionByteSet initial_trade_stock_4 = { .bytes = {
    0, 0, 0, 0, 0, 0, 255, 100, 0, 255, 50, 50, 50, 1, 1, 0,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 1,
} };

/* 0x0043DFB0 */
const DefinitionByteSet initial_trade_stock_5 = { .bytes = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 255,
} };

/* 0x0043E130 */
const PlayerLevelStep player_level_steps[100] __attribute__((aligned(16))) = {
    { 0 },
    { 0 },
    { .experience = 50, .hp_gain = 8, .mp_gain = 1, .power_gain = 3, .magic_gain = 2 },
    { .experience = 110, .hp_gain = 9, .mp_gain = 1, .power_gain = 3, .magic_gain = 2 },
    { .experience = 185, .hp_gain = 8, .mp_gain = 1, .power_gain = 3, .magic_gain = 3 },
    { .experience = 280, .hp_gain = 7, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 400, .hp_gain = 9, .mp_gain = 1, .power_gain = 4, .magic_gain = 3 },
    { .experience = 550, .hp_gain = 8, .mp_gain = 1, .power_gain = 2, .magic_gain = 3 },
    { .experience = 735, .hp_gain = 10, .mp_gain = 1, .power_gain = 3, .magic_gain = 4 },
    { .experience = 960, .hp_gain = 10, .mp_gain = 1, .power_gain = 4, .magic_gain = 2 },
    { .experience = 1230, .hp_gain = 8, .mp_gain = 1, .power_gain = 3, .magic_gain = 3 },
    { .experience = 1550, .hp_gain = 10, .mp_gain = 1, .power_gain = 3, .magic_gain = 2 },
    { .experience = 1925, .hp_gain = 8, .mp_gain = 1, .magic_gain = 3 },
    { .experience = 2360, .hp_gain = 9, .mp_gain = 1, .power_gain = 1 },
    { .experience = 2860, .hp_gain = 10, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 3430, .hp_gain = 9, .mp_gain = 1, .magic_gain = 3 },
    { .experience = 4075, .hp_gain = 10, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 4800, .hp_gain = 11, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 5610, .hp_gain = 9, .mp_gain = 1, .power_gain = 1 },
    { .experience = 6510, .hp_gain = 9, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 7505, .hp_gain = 12, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 8600, .hp_gain = 13, .mp_gain = 1, .power_gain = 2, .magic_gain = 1 },
    { .experience = 9800, .hp_gain = 10, .mp_gain = 1, .power_gain = 1, .magic_gain = 3 },
    { .experience = 11110, .hp_gain = 11, .mp_gain = 1, .power_gain = 2 },
    { .experience = 12535, .hp_gain = 11, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 14080, .hp_gain = 12, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 15750, .hp_gain = 12, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 17550, .hp_gain = 13, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 19485, .hp_gain = 14, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 21560, .hp_gain = 13, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 23780, .hp_gain = 13, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 26150, .hp_gain = 14, .mp_gain = 1, .power_gain = 2, .magic_gain = 3 },
    { .experience = 28675, .hp_gain = 15, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 31360, .hp_gain = 14, .mp_gain = 1, .power_gain = 1 },
    { .experience = 34210, .hp_gain = 15, .mp_gain = 1, .power_gain = 1, .magic_gain = 3 },
    { .experience = 37230, .hp_gain = 14, .mp_gain = 1, .power_gain = 3, .magic_gain = 2 },
    { .experience = 40425, .hp_gain = 15, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 43800, .hp_gain = 16, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 47360, .hp_gain = 16, .mp_gain = 1, .power_gain = 2 },
    { .experience = 51110, .hp_gain = 18, .mp_gain = 1, .power_gain = 1, .magic_gain = 3 },
    { .experience = 55055, .hp_gain = 18, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 59200, .hp_gain = 20, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 63555, .hp_gain = 19, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 0x10A22, .hp_gain = 19, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x11CE7, .hp_gain = 21, .mp_gain = 1, .power_gain = 2, .magic_gain = 3 },
    { .experience = 0x1309C, .hp_gain = 20, .mp_gain = 1, .power_gain = 3, .magic_gain = 3 },
    { .experience = 0x1454B, .hp_gain = 18, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x15AFE, .hp_gain = 16, .mp_gain = 1, .power_gain = 1, .magic_gain = 3 },
    { .experience = 0x171BF, .hp_gain = 16, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 0x18998, .hp_gain = 14, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x1A293, .hp_gain = 13, .mp_gain = 1, .power_gain = 2 },
    { .experience = 0x1BCBA, .hp_gain = 13, .mp_gain = 1, .magic_gain = 3 },
    { .experience = 0x1D821, .hp_gain = 12, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x1F4DC, .hp_gain = 13, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x212FF, .hp_gain = 11, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x2329E, .hp_gain = 12, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x253CD, .hp_gain = 10, .mp_gain = 1, .power_gain = 2, .magic_gain = 1 },
    { .experience = 0x276A0, .hp_gain = 9, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 0x29B2B, .hp_gain = 11, .mp_gain = 1, .power_gain = 2 },
    { .experience = 0x2C182, .hp_gain = 12, .mp_gain = 1, .power_gain = 2, .magic_gain = 1 },
    { .experience = 0x2E9B9, .hp_gain = 10, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x313E4, .hp_gain = 9, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 0x34003, .hp_gain = 10, .mp_gain = 1, .power_gain = 3, .magic_gain = 1 },
    { .experience = 0x36E16, .hp_gain = 9, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 0x39E1D, .hp_gain = 9, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x3D018, .hp_gain = 8, .mp_gain = 1, .power_gain = 2, .magic_gain = 1 },
    { .experience = 0x40407, .hp_gain = 7, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x439EA, .hp_gain = 8, .mp_gain = 1, .power_gain = 2, .magic_gain = 1 },
    { .experience = 0x471C1, .hp_gain = 9, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 0x4AB8C, .hp_gain = 7, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 0x4E74B, .hp_gain = 8, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x524FE, .hp_gain = 7, .mp_gain = 1, .power_gain = 2, .magic_gain = 2 },
    { .experience = 0x564A5, .hp_gain = 7, .mp_gain = 1, .magic_gain = 2 },
    { .experience = 0x5A640, .hp_gain = 8, .mp_gain = 1, .power_gain = 2 },
    { .experience = 0x5E9CF, .hp_gain = 6, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0x62F52, .hp_gain = 7, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 0x676C9, .hp_gain = 6, .mp_gain = 1, .power_gain = 2 },
    { .experience = 0x6C034, .hp_gain = 6, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x70B93, .hp_gain = 7, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x758E6, .hp_gain = 5, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0x7A82D, .hp_gain = 5, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x7F968, .hp_gain = 6, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x84C97, .hp_gain = 5, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0x8A1BA, .hp_gain = 6, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x8F8D1, .hp_gain = 5, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0x951DC, .hp_gain = 6, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0x9ACDB, .hp_gain = 5, .mp_gain = 1, .power_gain = 1, .magic_gain = 1 },
    { .experience = 0xA09CE, .hp_gain = 5, .mp_gain = 1 },
    { .experience = 0xA68B5, .hp_gain = 4, .mp_gain = 1, .power_gain = 1, .magic_gain = 2 },
    { .experience = 0xAC990, .hp_gain = 4, .mp_gain = 1 },
    { .experience = 0xB2C5F, .hp_gain = 3, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0xB9122, .hp_gain = 4, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0xBF7D9, .hp_gain = 3, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0xC6084, .hp_gain = 2, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0xCCB23, .hp_gain = 2, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0xD37B6, .hp_gain = 1, .mp_gain = 1, .magic_gain = 1 },
    { .experience = 0xDA63D, .mp_gain = 1, .power_gain = 1 },
    { .experience = 0xE16B8, .mp_gain = 1 },
    { .experience = 0xE8927, .mp_gain = 1 },
    { .experience = 0xEFD8A, .mp_gain = 1 },
};

/* 0x0043E450 */
GsColor haze_sprite_color __attribute__((aligned(16))) = { .rgba = { 128, 128, 128, 128 } };

/* 0x0043E460 */
LayerOffsets haze_layer_x_offsets = { .x = { -16, 0, 16 } };

/* 0x0043E470 */
char z_value_format[16] = "z_value[%u]\n";

/* 0x0043E480 */
ScreenRect full_screen_rect __attribute__((aligned(16))) = { .right = 640, .bottom = 448 };

/* 0x0043E490 */
ScreenRect inset_screen_rect __attribute__((aligned(16))) = { .left = 4, .top = 3, .right = 636, .bottom = 445 };

/* 0x0043E4A0 */
char packet_overflow_message[72] = "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";

/* 0x0043E4E8 */
char obj_prm_path[16] = "param/obj.prm";

/* 0x0043E4F8 */
char chr_prm_path[16] = "param/chr.prm";

/* 0x0043E508 */
char item_nam_prm_path[24] = "param/item_nam.prm";

/* 0x0043E520 */
char item_cmn_prm_path[24] = "param/item_cmn.prm";

/* 0x0043E538 */
char item_wpn_prm_path[24] = "param/item_wpn.prm";

/* 0x0043E550 */
char item_pct_prm_path[24] = "param/item_pct.prm";
