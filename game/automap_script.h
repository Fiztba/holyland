/* automap_script.h: PC-only. The automap mod's reading of a map's start script for a map the player is not in
   (game/automap_script.c), for the map browser of game/automap.c. */
#ifndef GAME_AUTOMAP_SCRIPT_H
#define GAME_AUTOMAP_SCRIPT_H

#include "common.h"

/* What a map's start script (label 0x7FFF, which Map_MakeCurrent runs each time the map becomes the current
   one) works on, as copies the run changes: nothing of the game's is written. */
typedef struct {
    const u32 *script;     /* the map's script words (MapSlot.script: the file at MapSlotUnk08.script_offset) */
    const u32 *script_end; /* the end of the file: no word is read from here on */
    int map;               /* the map's number (MapSlot.tag) */
    /* the spawns: Entity.flags of each (as Entity_InitFromMapSpawns leaves them, or the live ones), and the
       spawn by its id (EntitySpawnFlags.bytes[4], entity_by_spawn_id), -1 none */
    int spawn_count;
    u32 *entity_flags;
    s16 spawn_by_id[0x100];
    /* the map objects: MapObject.state_flags of each, and the object by its id (spawn_flags.bytes[2],
       objects_by_id: the first with it), -1 none */
    int object_count;
    u32 *object_state;
    s16 object_by_id[0x100];
    /* the fixed items: placed (MapItem_PlaceFixed) and where (x, y, z), and the item by its id
       (MapItemDef.slot_id, fixed_item_by_id), -1 none */
    int item_count;
    u8 *item_placed;
    const u8 *item_taken; /* placed and taken as often as it may be: MapItem_PlaceFixed puts it nowhere */
    f32 (*item_position)[3];
    s16 item_by_id[0x100];
    /* the map's records: left out of the collision (record_flags 0x40000000), and the record by its prop id
       (MapRecord.prop_id, map_slot_props: the first with it), -1 none */
    int record_count;
    u8 *record_hidden;
    s16 record_by_prop[0x100];
} AutomapScriptWorld;

/* Runs the start script of the map as the game would as the player walks in (with the game's variables as
   they are now, read only: what the script writes goes into the run's own copy), and what it does to the
   map's spawns, objects and fixed items into `world`. Returns the number of words stepped (0: no start
   script). */
int Automap_RunStartScript(AutomapScriptWorld *world);

#endif
