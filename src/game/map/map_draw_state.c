/* The haze layers the maps are drawn with, and the map draw state. */
#include "map/map.h"

/* 0x38A158 the distances of the haze layers, nearest last */
u32 haze_layer_distances[8] = { 0x800, 0x600, 0x400, 0, 0, 0, 0, 0 };

/* 0x38A178 the screen depth of each haze layer, written as they are projected */
int haze_layer_depths[8] = { 0 };

/* 0x38A198 six words the game's code never refers to */
int D_0038A198[6] = { 0x20, 0x18, 0x10, 0x8, 0x4, 0 };

/* 0x38A1B0 the map draw state (MapDrawState). On the PS2 it was a run of separate globals that the code also
   reads as one struct; the PC defines the struct, and the old names are its fields:
     0x38A218 hud_visible                   MapDrawState.hud_shown (and the words after it)
     0x38A250 current_fog_block             MapDrawState.unkA0: the current map's first block
     0x38A270 current_light_block           MapDrawState.unkC0, first half; current_map_light_colors its second
     0x38A2F0 D_0038A2F0                    MapDrawState.pad140
     0x38A430 current_map_fog_color         MapDrawState.unk280
     0x38A440 map_draw_slot_blocks          MapDrawState.fog_blocks
     0x38AC40 map_draw_slot_draw_blocks     MapDrawState.light_blocks
     0x38CC40 map_draw_screen_tint          MapDrawState.screen_tint_amount to pad2A9C */
MapDrawState map_draw_state = { 0 };
LAYOUT_SIZE(MapDrawState, 0x2AA0);
LAYOUT_OFFSET(MapDrawState, draw_flags, 0x60);
LAYOUT_OFFSET(MapDrawState, hud_shown, 0x68);
LAYOUT_OFFSET(MapDrawState, screen_fill_blend, 0x80);
LAYOUT_OFFSET(MapDrawState, unkA0, 0xA0);
LAYOUT_OFFSET(MapDrawState, unkC0, 0xC0);
LAYOUT_OFFSET(MapDrawState, unk280, 0x280);
LAYOUT_OFFSET(MapDrawState, fog_blocks, 0x290);
LAYOUT_OFFSET(MapDrawState, light_blocks, 0xA90);
LAYOUT_OFFSET(MapDrawState, screen_tint_amount, 0x2A90);
_Static_assert(offsetof(MapDrawStateMirror, mirror_height) == offsetof(MapDrawState, draw_flags), "MapDrawStateMirror is not MapDrawState");
