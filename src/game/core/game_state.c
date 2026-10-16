/* The game status block and the loaded maps' state. */
#include "core/game.h"
#include "map/map.h"

GameStatus game_status = { 0 }; /* 0x38CC50 the status of the running game */
/* The names other files used for its fields (asm aliases on the PS2 build): player_can_walk is
   movement_input_enabled (0x38CC57), game_status_flags the flag word at 0x24 (0x38CC74),
   suspended_updates update_hold_flags (0x38CC80: 0x1 the player, 0x4 the entities, 0x8 the map objects'
   sway, 0x10 falling items, ...), save_area_name_id save_area. */

/* 0x38CCA0 default light block for map objects: one light (125, 110, 83) */
MapSlotUnk10Lights map_object_default_lights __attribute__((aligned(16))) = {
    { { 0 } },
    { { 0 }, { 0 }, { 0 }, { 125.0f / 255.0f, 110.0f / 255.0f, 83.0f / 255.0f, 0.0f } },
};

MapSlot map_slots[2] = { 0 }; /* 0x38CD20 the two loaded maps */

/* 0x38CDE0 the prop of each prop id, per map slot */
MapProp *map_slot_props[2][0x100] = { 0 };
