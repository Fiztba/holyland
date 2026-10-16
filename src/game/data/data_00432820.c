/* data_00432820.c: the game's data from the decomp's data_00432820.data.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "core/game.h"
#include "gfx/gs.h"

/* 0x00432820. The decomp's labels script_error_flags lie inside it (D_00432820.script_error_flags): the code that names them must use these
   fields, so they are not defined on their own */
ClockControl D_00432820;

/* 0x00432D20. The decomp's labels game_dbuff_draw0, game_dbuff_clear_color, game_dbuff_draw1 lie inside it (game_dbuff.draw0.frame.fbp, game_dbuff.clear0.rgbaq, game_dbuff.draw1.frame.fbp): the code that names them must use these
   fields, so they are not defined on their own */
GsDoubleBuffer game_dbuff __attribute__((aligned(16)));

/* 0x00432F50. The decomp's labels game_dbuff_context2_clear_color lie inside it (game_dbuff_context2.clear0.rgbaq): the code that names them must use these
   fields, so they are not defined on their own */
GsDoubleBuffer game_dbuff_context2 __attribute__((aligned(16)));

/* 0x00433180 */
GsRegisterPair frame_dispfb_regs __attribute__((aligned(16)));

/* 0x00433190 */
GsRegisterPair frame_display_regs __attribute__((aligned(16)));
