# The automap mod

A map of where the player has been, filled in as he explores, in the look of the KF3 port's automap
(kfii-recomp, `patches/Map/`): walkable floors shaded by their height against the player's (lower is
darker), a light band where they meet walls, water, lava, poison and deadly drops coloured, markers with
icons for what is in the map, and a label for the marker under the mouse. Like every mod it is off by
default (the Mods page's "Map" section, `--automap`, `settings.json` `"mods"`), and with it off nothing of
it runs.

- **Full map**: the Map key (M in both layouts; rebind it on the Controls page, "Map") opens it in play.
  The game waits while it is open. The wheel zooms toward the cursor, a left drag moves, a right click
  fits the map again; PgUp and PgDn (or Shift with the wheel) show the levels above and below the
  player's, Home his own; M or Escape closes it. Clicking a way into another map shows that map (the map
  browser, below); Backspace goes back to the map shown before, Home to the player's own map at his own
  level (Home did only the level before the browser).
- **Minimap**: a corner of the picture while playing (hidden in menus): corner, size, reach and opacity
  on the Mods page. The extended stats mod's timers (ailments and timed effects) sit above it in a bottom
  corner and below it in a top one (EXTENDED_STATS.md, "The timers on screen").
- **Cheats** (off by default, they stand in for an online guide): reveal the whole map; items (on the
  ground, what chests, containers and bones hold, what enemies may drop); live enemies with their HP;
  secrets and traps (shooters, pitfalls, walls that appear or fade); locks and hints (what each lock needs,
  whether you have it, and the hand-written guide notes, a blue "?" on the map).

Everything shown comes from the player's disc and the game's memory as it runs, except the hand-written
tables of `game/automap_guide.c` (the creatures' names, the guide notes). Nothing of the game is in the
repository.

## How it works

Three parts (`port/include/port/automap.h`):

- `game/automap.c`, the game's side. `Automap_Poll` runs once a game frame from `Quick_PollGame` (the
  end of `GameLoop_Run`'s frame, on the game's thread), reads the game's structures and builds what is
  shown. It only reads the game.
- `port/platform/automap.c`: the settings and command line, whether the full map is open, and the files.
- `port/overlay/automap_view.cpp`: the drawing (Dear ImGui, inside the overlay's frame, so it works with
  every renderer and presenter), the hover labels and the Mods page's section.

The hooks into shared files are one call each: `Quick_PollGame`/`Quick_PollTitle` (`game/quicksave.c`),
the settings' "mods" object (`port/overlay/settings.c`), the overlay's events, visibility, pause, draw,
Mods page and test pictures (`port/overlay/overlay.cpp`), `ACTION_MAP` (`port/sdk/controls.c`), the
command line (`port/platform/main_host.c`), and the save files (`port/platform/quicksave.c`,
`port/sdk/libmc.c`).

### The floor plan

A map is made of records (`MapSlot.records`, `MapRecordBounds`), each with a model and a collision mesh:
strips of triangles in six lists (`CollisionMesh`): walls, floors, more walls, entity barriers, hazards
and water. The game tests the player against exactly these, so they make the cleanest floor plan there
is. They are put in the world with each record model's `collision_matrix` (`Map_PlaceRecordModels`; the
current map always lies in its own file's frame) and drawn from above into cells of 50 units:

- **Floors, water, hazards**: each cell's centre is dropped onto the triangles over it. A cell keeps up
  to four heights (a bridge over a river, a balcony over a hall); heights less than 150 apart are one
  layer, the more dangerous kind winning (a lava floor is lava). Hazards are sorted by their effect
  (`Collision_GetSurfaceEffect`, `Player_UpdateStatus`): 0 kills on contact (the void under the map, deadly
  pits), 1-3 poison, 4 takes 10 HP a frame (lava; the sea in the maps `game/automap_guide.c` lists),
  5 acid (1 HP a frame, wears what is worn).
- **Walls**: a steep wall triangle marks the cells its edges cross with its height range; a sloped one
  (a cave's rock) marks the cells under it at its height there. A floor layer is drawn as wall where a
  wall rises from it (its bottom within 150 of the floor, its top above it): ceilings and lintels are
  not walls. A cell with no floor keeps the wall's top and bottom, and is drawn as wall beside a floor.
- **The void**: a deadly layer more than 14 cells from any floor is not drawn.

**Levels.** Passages at different heights overlap on a plan seen from above (the Upper Mines, map 008,
have three). The view draws the player's level, his slice, in full and the others greyed and darkened the
further they are from his height (a hazard keeps more of its colour), and the slice's edges where it
meets another level a step lower or higher cast a dark band (10-20 pixels on the full map, a few on the
minimap) onto it, with a wall-coloured line along the edge, so that a passage running under another does
not look joined to it. The slice is the floor connected to the player's: from the layers under his feet
to the layers of the cells beside them within a step of 150 (as the reveal's rays walk), staying within
700 of his height, walls and the void bounding it. In each cell the view draws the slice's layer when the
cell has one that has been seen; else the layer he would see at his height (the highest that is not more
than 400 above his feet, else the lowest). Floors are shaded in steps of 350 against his height. On the
full map PgUp/PgDn step to the nearest level above or below that is not part of the slice shown and is
more than a scrap (60 layers), grown from that floor; the minimap always shows his own. Markers on
another level are drawn faint, and a way into another map is hatched only on the cells at the join's
height (all of its cells when none is drawn at that height). Screen up is the world's +z, as on the game's
own picture maps (`MapViewer_LocateOnCaretakersMap`); the game's King's Map turns with the player instead.

### What has been seen

Every few frames (when the player moves a cell or turns), 720 rays go out from him across the cells, up
to 3,400 units, following the floor from cell to cell (rising or falling at most 150 a cell); a ray
reveals each layer it crosses and stops at a wall. Off a ledge it reveals the floor below and goes on for
at most 24 cells over the drop (or over nothing), so a gap's far side is seen. The cell the player stands
on is marked walked. Markers show once a cell near them has been seen; the cheats' markers show anyway.

The seen layers are kept per map as runs of cells with their mask of layers, with the map's grid and a
hash of its layers (`Automap_Serialize`). A file from a build that cuts the layers differently still
loads: its cells count as seen at every height.

### Per save

- `automap/live.bin` in the data folder is the playthrough's state: written 5 s after a change, when the
  full map closes, on a save, and at exit.
- A quick save also writes `automap/quick_<file>.bin`; a memory card save (the game's file
  `BASLUS-20318Gnn` opened for writing, `port/sdk/libmc.c`) writes `automap/card_nn.bin`.
- Loading a quick save or a memory card save brings its copy back (a save made without the mod has none:
  the map is kept as it is). A new game (play reached from the title without a load) starts with nothing
  explored.

### Markers

- **Map objects** (`map_slot_objects`, kind from `param/obj.prm`, `MapObjectUnk04.kind`; only those in use,
  `MAP_OBJECT_IN_USE`, which the map's set-up and scripts turn on and off with the story): doors (1, 2, 6,
  0x11; locked while `kind_param_0` names a key not yet used; the leaf drawn from the door's collision mesh,
  so an open door shows open), chests (4; key `kind_param_1`, item `kind_param_0`, gold `kind_param_2`),
  bones and graves (3) and barrels, pots and boxes (21) with their item, things to look at (11: a sign, or
  the message examining shows), symbols of passage (12), pedestals and markers (13, 0x13), lifts (14),
  ladders (15), switches (7, 8, 30), save points (20, with the area's name), and with the secrets cheat
  shooters and pitfalls (16, 18) and fading objects (31). Names are the game's own (the definition's
  message, `Message_GetText`; items from `item_name_defs`).
- **What they hold** (the items cheat), only what the game will hand over: the item comes out of bones and
  graves when used (`MapObject_UpdateItemHolder`), of containers when broken (definition flag 0x800), of
  chests when opened, through `MapItem_PutOutForObject`, which puts out nothing for a number that is not an
  item or one the map has no model for, and nothing for now for an item of a kind the player may have only
  one of while he has it, a shop has it in stock or one lies dropped ("stays: ..."). An object with a use
  script (`MapObjectUnk08.use_script`) runs it instead when used (`Interact_FindTarget` takes the press
  before the objects' updates), so its `kind_param_0` can be anything: the gravestone on map 097 holds
  1026, the message its script shows. What such a script gives (command 101, a fixed item) is shown as
  "Gives (by its script)", taken once picked up as often as it may be (`map_item_pickup_counts`), without
  the ring (the story's flags decide when): the Icon of Healing on that grave (it also lies on it, its own
  item marker), the smith's work in the Earth Folk's chests of maps 017-019. Bones' and containers'
  scripts' first message is shown in quotes. `KF4_AUTOMAP_DUMP` lists every object in use with its
  scripts' words.
- **People** (entity type 15, and 81 the dog, with a talk script): the name is the last line of their page
  of the Recollection screen (message 3000 + 50 × (record − 100) + 49). A map's spawn records hold every
  place a person stands at any time in the story (Sharyl Genne has two on map 004, Lynn Genne a third by
  her father's grave), so only the ones present now are shown, by the game's own test: a person in the
  world (live, not dying, on the active list) where he stands; one out of reach where he will appear only
  if `EntityAi_ShouldRespawn` would bring him back when the player comes near (flag bit 7, which the
  spawn record and the map's scripts set as the story moves on, command 253; not taken away for good;
  deaths under the spawn's limit). With the secrets cheat, the others are a faint ring, "not here now".
  Markers are made again every game frame, so someone who walks off or leaves after a talk moves or goes.
- **Enemies** (the enemies cheat): live entities, their HP, and with the items cheat what they may drop
  (`EntityParams.drop_item`, `drop_chance`). Not the bodies lying as set dressing: spawns whose type's
  set-up gives them only a pose to hold, no on_hit and no death handler, and flags them out of targeting
  (`ENTITY_FLAG_NOT_TARGETABLE`): the dead Expedition Soldiers (chr 93, spawn variant 1 and 2,
  `ExpeditionSoldier_ThinkHoldAnim18Or19`) on maps 012, 015 and 101, which a "Corpse" map object marks
  already. Statues and guardians that hold a pose until woken keep their on_hit handler and stay enemies.
- **Items** (the items cheat): `lying_items` of the current map, not inside a container. Some of a map's
  fixed items lie out of sight until a script gives them: script command 101 (`MapItem_StartPickupById`)
  takes a fixed item by its id (`MapItemDef.slot_id`) and flies it to the player's eyes. The map's script
  is read as the map is built. An item is shown as handed over (a faint ring, "not on the ground") when a
  script gives it that way, but not the script the game runs when the player takes it where it lies
  (`MapItemDef.pickup_script`: the crystals on altars, Lord Myu's armour, which stay ordinary items), and it
  lies under the floor (more than 60 below the plan's nearest floor, or with none near). The label names the
  person whose talk script (or the script run when an item is used on him) reaches the command, directly or
  through command 2, else "given by an event": the Fire Crystal on map 004 is Lynn Genne's, after her mother
  is healed; the Craftsman's Key on map 020 Harmurah Forgefist's; the Icon of Fire on map 026 Joshua
  Griffin's; the smith's work on maps 017-019 lies 10,000 below the world.
- **Ways into other maps**: the maps join without a load screen. A record that names one of the map's
  origins (`MapRecordInfo.neighbour_origin_index`, `MapSlot.origins`: the neighbour's number and where it
  lies) has the game load that neighbour into its other slot while the player stands on it, moved by minus
  the origin's position (`MapLoad_Update`, `MapLoad_SetUpMap`); those records are where the loading starts,
  often whole rooms, not the way across. The player crosses when the floor under him is the other map's
  (`Player_FindFloor`, `Map_MakeCurrent`). So a way is drawn where this map's floor meets the neighbour's:
  the neighbour's file is read from the disc as the map is built (`KF4.DAT`'s `NNN.map`, its records'
  floor, hazard and water strips placed by each record's matrix as `Map_PlaceRecordModels` builds it, then
  moved as the game moves it: checked cell for cell against the neighbour the game itself loads, on maps
  000, 005 and 022), and the cells of this map's floor with the neighbour's floor in them or beside them
  within a step (150) are the way; also a gap in this map's floor over the neighbour's floor, beside a floor
  of a record that loads it (a drop). Each connected stretch is one way: its cells hatched, a small white
  diamond in its middle and, on the full map, its name (the Teleport table's, `port_teleport_map_name`).

## Browsing other maps

On the full map, a way into another map (its hatched cells or its diamond) can be clicked: the full map then
shows that map, titled with its name, its number and "(browsing)", seen from where the way comes in (its level
there; PgUp and PgDn as on the player's map). Its own ways are clickable in turn, so the world can be walked on
the map. Back (the button in the top right corner, or Backspace) returns to the map shown before, the last one
to the player's own; Home (the button, or the Home key) to the player's own map, with his arrow; closing the
full map does too. A way into the player's own map goes home. The minimap always shows the player's map.

Only a map the player has explored some of can be shown (its explored part is kept in `automap/live.bin`;
`map_explored`), any with the reveal cheat; the hover label of a way into one says "Click: show this map",
of one not explored yet "(not visited yet)", and the click does nothing. The fog of war is the map's own
explored state, kept per map (the chunks of `Automap_Serialize`, by map number; checked: each map's chunk
holds its cells only, and a browsed map built in the game's float mode has the same layout hash as when the
player stands in it, so its explored cells are exactly his).

**Strictly read only.** A browsed map is built from its file on the disc (`map_file_read`: KF4.DAT's
`NNN.map` into memory of its own, through `port_disc_read`, which takes the disc's lock and seeks for every
read) and the save's tables, which are only read. It is built on the game's thread as the click is drawn,
while the full map holds the game, so no load of the game's is under way then; nothing of the game's memory,
streaming or state is touched, and with the mod off none of it runs.

What is shown, and from where (game/automap.c, "The map browser"):

- **Floor plan, walls, water, hazards, levels**: the records' collision meshes from the file
  (`file_triangles`), as `Map_PlaceRecordModels` places them, with the records the story leaves out or moves
  as the game would on the way in (below).
- **People**: whether each is there now is the game's own test (`spawn_flags_can_appear`, as for the
  player's map) on the flags the spawn would have as the player walks in. Those flags are not kept anywhere
  while a map is not loaded: `Entity_InitFromMapSpawns` makes them again from the spawn records each time the
  map is loaded (bit 7 unless the spawn's bits 59-60 are 2; bit 4 cleared by `Entity_ClearFlag10InSlot`), then
  `MapSetup_RunForMap` and the map's start script (label 0x7FFF, which `Map_MakeCurrent` and the loads run as
  the map becomes the current one) set them from the story's variables (command 253,
  `ScriptCmd_SetEntityFlags`). game/automap_script.c runs that start script as the interpreter would, on
  copies: the game's variables (`script_game_var_blocks`) are read, what the script writes goes into the run's
  own list, the threads' and shared variables are its own, and only the commands that change or read what is
  shown are carried out (253 and 254 the spawns' flags, 42 their deaths, 110, 111 and 115 the objects, 100
  and 102 the fixed items, 150 the records, 123 the items carried, 2 and 5 the threads); a command that never
  finishes in the game ends its thread there. The deaths are the save's (`Game_GetSpawnDeathCount`). So a
  person is shown exactly when the game would bring him on the way in, and with the secrets cheat the others
  are the faint "not here now" ring, as on the player's map.
- **Objects** (doors, chests, containers, switches, save points...): their state as the map keeps it while it
  is not loaded (`map_entry_flags`: `Game_GetObjectActive`, `Game_GetObjectState`, `Game_GetObjectFlag`, the
  trigger items; as `MapObject_StartMap` reads them back, or from the placement for a map never visited), then
  `MapSetup_RunForMap`'s and the start script's changes; labels, "(stays: ...)", "Gives (by its script)",
  locks and hints as on the player's map. A door's leaf comes from its collision mesh in the file.
- **Items** (the items cheat): the fixed items not taken as often as they may be (`map_item_pickup_counts`)
  that the map has a model for; one that lies there only by `MapItem_RollFixedItems`' rolls is drawn faint,
  "(there by chance as you walk in: N%)"; one the start script puts out is there for sure; the items dropped
  there (`dropped_item_records`). "Given by ..." as on the player's map (the map's script is read from the
  file).
- **Enemies** (the enemies cheat): not in the world while the player is away; the places their spawns come
  into the world, as faint rings, for the spawns that would come.
- **A map the game still holds in its other slot** (the neighbour it streamed in): its live objects' and
  spawns' flags, records and lying items (moved back into the map's own frame by the slot's offset), with
  the start script run on them, as the game runs it again when the player walks back in.

Checked against the player's map (`KF4_AUTOMAP_BROWSE`'s `self`, below: the map he stands in built as a
browsed one, the save's tables holding it as the quick save wrote them) on the maps of seven of the owner's
saves (012, 013, 017, 096, 097, 104, 105) and, through teleports, on 000, 004, 011, 020, 041 and 100: the
same people (there, or not here now), objects and their states, labels and floor plan; the differences are
only the items there by chance (the game rolled them), a door's leaf drawn from its other end (105), and
the enemies (live ones against spawn places).

## Adding area names, labels and hints

Everything hand-written is in `game/automap_guide.c`; no other file needs to change.

- **Hints**: add a line to `hints[]`: `{AUTOMAP_OBJECT, map, object, 0, "text"}` for a map object, or
  `{AUTOMAP_SPAWN, map, spawn, 0, "text"}` for an entity (or `{AUTOMAP_SPAWN, map, -1, record, "text"}`
  for every spawn of a chr.prm record on that map). With the hints cheat on, the full map's hover label
  of every object ends with its number ("(object #45)"), and a person's with his spawn and chr.prm record
  ("(spawn #2, chr 105)"), so: turn the cheat on, point at the thing, note
  the map number (beside the title) and the object number. Write what the player needs to know in your own
  words, not the game's text.
- **Creature names**: `creature_names[]`, by chr.prm record (the full map shows "Creature (#n)" for one
  not listed).
- **People's names**: `person_pages[]`, for a person's variant record that has neither a Recollection page
  of its own nor its model's (the full map shows "Person"): the record whose page is his.
- **Map names**: the full map's title is the Teleport's name of the map (`port/platform/teleport.c`,
  `map_names`, from the decomp's `docs/GAME_DATA.md`).
- **Hazards**: `sea_maps[]` lists the maps where hazard effect 4 is the sea rather than lava.

## Testing

The overlay's test pictures take the map too: `KF4_OVERLAY_SHOT=map@V` opens the full map at vblank V
(overlay closed) and writes `overlay_map_V.png`; `mini@V` writes the picture with the minimap,
`overlay_mini_V.png`. `KF4_AUTOMAP_BROWSE=step,step,...` takes a step of the map browser as each test picture
of the full map opens: a map's number clicks the way into it on the map shown, `back` and `home` the buttons,
`self` shows the player's own map built as a browsed map is (to check the two against each other), an empty
step nothing; `KF4_AUTOMAP_DUMP` then also writes each browsed map's markers, spawns and objects to
`DIR/automap_browse_NNN.txt`. `KF4_AUTOMAP_HOVER=x,z` puts the mouse over that world point on the full map (for
the hover label), and `KF4_AUTOMAP_DUMP=DIR` writes each map's markers' labels to
`DIR/automap_NNN.txt` a second after it is built (and `KF4_AUTOMAP_LEVEL=n` makes the test picture of the
full map show the level n above or below the player's), then every spawn with what decides whether it is
shown, the script commands that give items, and the fixed items lying about. `--automap-cheats` turns the mod and every cheat on for
a run. With `tools/map_tour.py`'s made-up quick saves:

```sh
KF4_DATA_DIR=/tmp/kf4-data KF4_OVERLAY_SHOT=map@270 \
  out/windows-clang/kf4 --fast --no-audio --memcard none --input title.pad --quick-load view.kf4quick \
  --automap-cheats --frames 290 --dump-dir /tmp/shots
```

With the mod off the game is the same: the poll returns at once, nothing is drawn, and no file is written.

## Known limits

- The map browser follows the joins of the floors only, as the ways are marked; a browsed map is what the
  player would find walking in now, so a person a talk sends away until the player leaves, or an object a
  script moves later, shows as the next visit will have it. The start script is run once, at once: what it
  does only after waiting for the player (a timer, his position) is not seen. An item made to lie in a map by
  a script other than its start script, or by `MapSetup_RunForMap` for maps 027-038 (fog, lights), is not
  modelled.
- `--teleport` writes the map it leaves's live object flags under the map it goes to (game/quicksave.c's
  `teleport` sets the slot's tag before `Game_WriteSaveImage`, whose `MapObject_SaveAllFlags` writes them by
  tag): the objects of the map teleported to start in the wrong state. Not the browser's; seen while checking
  it.

- The full map and the minimap are drawn with ImGui on the game's thread: while the minimap shows, the
  higher frame rates' pictures between are shown by the game's thread too (as while the overlay is open),
  which can make them less even. Turn the minimap off for the smoothest 120-240.
- A floor plan of collision only: a decoration with no collision (a statue walked through) is not on it,
  and a map object's own collision (a lift, a door's frame) is not part of the floor.
- Hazard effect 4 is the same in the game whether lava or the sea; which one a map has is the hand-written
  list.
- Ways into other maps made by a script (doors that change the map, `MapChange_StartToMap`; wands) are not
  marked, only the joins of the floors.
