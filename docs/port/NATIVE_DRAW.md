# Drawing at the game's level ("option 4"): a proposal

Status: approved; P0's core is built on branch `native/draw` (October 2026): the renderer core, hosted
draws in the native GS, the switches, and MapItem_DrawAll's items drawn natively, bit for bit as the PS2
path. [NATIVE_DRAW_TASKS.md](NATIVE_DRAW_TASKS.md) has what is built, the numbers, how to work on it, the
tasks next, and where the build changed this plan. Written for the owner, Andrew and Fizban, and for the
agents doing the work.

Today the port draws the PS2's way. The game builds its DMA chain, VIF1 unpacks the data, the VU1
programs (C versions of them, `port/platform/vu1_hle*.c`) transform it, and the native GS
(`port/gs/native/`) rasterises the GIF packets at the register level. Pictures between frames are made
by drawing those packets again with their vertices moved (`port/platform/frame_interp.c`). This path
gives the PS2's picture, and STEP3.md and RENDERER.md record how much work that took. It also brings a
long tail of bugs that come from working one level too low:
- Z rounding ties at a larger scale (`--native-layer-z`, the lace and the teeth);
- interpolation that has to guess which batch, run or 2D item is which (`objects_pair`, the sky moving
  as one, two look-alike models crossing);
- VRAM coherence (write-backs, read-backs, a CLUT at 0x3FE1 counted as reaching page 0);
- field and interlace tricks.

Option 4 changes the level: we patch the game's own draw code (we have all of its source) so that it
describes what it draws (map geometry, models, textures, lights, fog, effects) to a renderer that knows
those things, as the SM64 and Zelda PC ports do. The PS2 path stays for everything not yet moved, and
for comparison.

This page covers:
1. the survey: every way the game draws, with the functions, the data, and what each one asks of VU1
   and the GS;
2. the developers' assumptions that a modern renderer has to keep (Andrew's skybox warning);
3. the architecture;
4. a phased plan that agents can work through;
5. the risks and the decisions the owner has to make.

Paths are relative to the repository. Game code is under `decomp/src/game/` (read-only; the PC edits
are in `patches/game/<folder>.patch`). Line numbers are those of the pinned decomp, 88b0978.

## In short

- **Almost all of the 3D goes through six model draw functions** (`model/model.c`: `Model_DrawSkinned`,
  `Model_DrawPosed`, `Model_DrawPosedDepthByPart`, `Model_DrawPosedWithAlpha`, plus two that nothing in
  C calls). Their callers are a dozen functions: map records, map objects, entities, items, gold, the
  arm, effects, shadows and the King's Map. **None of the 78 enemy files draws anything itself.** They
  set alpha, tint and flags, and `Entity_DrawAll` draws them all. The surface we would patch for the 3D
  is small, about 3,000 lines of draw code.
- **The geometry and the textures are prebuilt PS2 packets inside the disc files**, sent by DMA REF.
  The C code supplies only matrices, light and fog blocks, tint and GS registers. So the new path reads
  the same bytes, parses each block once (the packed 16-bit and float vertex formats are known) and
  keeps it on the GPU. It does not rebuild anything from scratch.
- **The proposal has two levels.**
  - *Hosted* comes first and does most of the work. The patched draw functions queue a "native draw"
    command into the same chain, at the same place as the packets they replace. The native GS's thread
    runs it in order, into the same render targets and Z buffer, with the same VRAM textures and GS
    state. Anything not ported still goes the PS2 way in the same frame. Depth stays GS-compatible, so
    the two kinds of draw test against each other correctly.
  - *Standalone* comes later, once all of the play's 3D and its screen passes are moved. The renderer
    then owns its targets (float depth, high-precision colour, MSAA, any resolution) and resolves
    textures from their source bytes instead of from VRAM. The native GS keeps the menus, the title, the
    movies and the memory-card screens, and their picture is composited.
- **Frame interpolation becomes exact for what is ported.** Every draw has an identity (a record, or
  a model instance and part) and its transform at two game frames, and the camera comes from the
  game's own camera state. No more pairing by batch hash.
- **The order:**
  - P0: the renderer core and the comparison harness.
  - P1: map records (86% of the 3D draws).
  - P2: posed and skinned models, then the arm.
  - P3: the sky.
  - P4: effects.
  - P5: shadows.
  - P6: the reflection and water.
  - P7: the screen passes.
  - P8: standalone.
  - P9: the HUD and text at output resolution (optional).

  The menus, title and movies stay on the native GS, possibly for good. Rough effort: four to six months
  of steady work with agents to the end of P8; P1 alone is useful within the first six to eight weeks.
- **Exact or modern?** The owner decides that per item. Each assumption in the catalogue below has a
  "PS2" behaviour and a "modern" one. The default follows the rule already in PLAN.md: the PS2's picture,
  with improvements opt-in, except where an artefact only made sense on a TV.

## 1. The survey: every drawing path

### 1.1 The frame

`GameLoop_Run` (`core/game_loop.c:224`) runs once every two vblanks. Each pass does the following, in
order:
1. `Display_SwapBuffers`;
2. `Packet_SendChain` (`gfx/packet_buffer.c:116`) sends the chain built by the pass before, so there is
   a frame of latency;
3. the game's update;
4. `GameLoop_UpdateCamera` and `GameLoop_SetCameraMatrices`;
5. `GameLoop_DrawFrame` (`core/game_loop.c:335-541`) builds the next chain.

The order of the chain is the order on the screen, and the one sorted list is `gfx/draw_list.c`.

`GameLoop_DrawFrame`, mode 0 (no mirror):

| # | Call | What | VU1 / GS |
|---|---|---|---|
| 1 | `GameLoop_SetCameraMatrices(0,0)`, `GsDraw_QueueVu1Matrix` | world-to-screen into VU 0x00-0x03 | |
| 2 | `Map_DrawRecords(0)` (`map/map.c:146`) | the slot's texture packets, `MapScroll_UpdateDraw`/`UpdateCopy`, each visible record (`draw_parts`), the water's ripple model, the CPU's near cut, then `MapObject_DrawAll(mode,1,map)` for the slot's own objects | 0x000; scroll render-to-texture; PATH2 water |
| 3 | `MapObject_DrawAll(0,0,0)` (`map_object/map_object.c:447`) | doors, chests, levers, pedestals; Myu's Necklace glow | 0x000 (posed), 0x280 (jointed) |
| 4 | `Entity_DrawPoolModels` (`entity/entity_ai.c:1943`) | the entities' shadows | 0x5A0, DATE, FBA, context 2 |
| 5 | `Entity_DrawAll(0)` (`entity/entity_world.c:413`) | enemies and NPCs; translucent ones to the draw list | 0x280 (+ sphere pass) |
| 6 | `MapItem_DrawAll`, `ItemEffect_Draw`, `Gold_Draw` | items on the ground, the item-use effect, gold | 0x000, 0x280 |
| 7 | `Map_DrawLateRecords(0)` (`map/map.c:226`) | translucent records, Z write per record (flag 0x80) | 0x000 |
| 8 | `Effect_DrawAll(0)` (`effect/effect.c:418`) | effects into draw-list layers by placement | 0x700 |
| 9 | `DrawList_Draw(0,0)` | layer 0, far to near by distance from the *player* | |
| 10 | `SetCameraMatrices(0,2)`, `DrawList_Draw(1)`, `(2)` | layers 1-2 in a view-locked space (`view_screen * T(0,220,0)`) | |
| 11 | `MapItem_DrawPicked` | the item being picked up, after a depth clear | 0x000 |
| 12 | screen passes (1.3) | underwater and MP-drain wobble, fades, haze, the flicker filter, the previous frame, the view-turn shift | 0x276 sprites, frame as texture |
| 13 | `GsDraw_ClearDepth`, `DrawList_Draw(3)`, `Player_DrawArm` (`player/held_weapon.c:342`) | on-top effects, then the arm, the weapon and the trail | 0x280, PATH2 trail |
| 14 | `Player_DrawScreenTints`, underwater tint, `Hud_Draw`, fills, `Game_DrawFade`, `Splash_Draw`, message dim, `Message_Update`, pause, `Game_ApplyBrightness` | 2D and full-screen passes, brightness last | 0x276 and PATH2 |

Mode 1 (a mirror drawn on the screen) and mode 2 (the reflection into a 256x256 target, maps 008 and
009) draw a mirrored subset of steps 2 to 11 first (1.2.7).

The menus, the title, the movies and the memory card screens do not use this loop. They have loops of
their own on PATH3.

### 1.2 The 3D paths

The sizes below are lines of draw-related code in the decomp, not whole files.

#### 1.2.1 Map records: the level geometry

- **Data** (the map file, as `Map_ParseFile` reads it at `map/map.c:324`):
  - a header (0x80): offsets, counts, four background colours at 0x50, the screen tint at 0x78, and a
    draw distance at 0x7C that is 0 in every map;
  - 32 fog blocks (0x20 each) at 0x80;
  - 32 light blocks (0x80 each: three directions, three colours and an ambient row) at 0x480;
  - 16 scroll copies at 0x1480 and 16 scroll draws at 0x1540;
  - records at 0x17C0 (0x80 each: position, rotation, scale, model index, fog and light block indices,
    flags at 0x3C, boxes);
  - then the visibility tables (a 256-bit row per record), the neighbours' origins, the objects, the
    spawns and the items;
  - each record's model is a `PosedModelFile` (`model/model.h:221`), and the packet table holds six
    prebuilt texture and CLUT upload packets for the slot.
- **Record flags** (0x3C):
  - 0x1: late (translucent) list;
  - 0x2 and 0x200: water for mirror modes 1 and 2;
  - 0x10: follow the player's blended fog and light;
  - 0x20: drawn in the reflection;
  - 0x40: no near cut;
  - 0x80: late, but writes Z;
  - 0x100: wave UVs;
  - 0x40000000: hidden;
  - bit 31: **the sky, which follows the player** (1.2.3).
- **Culling and lists:** `func_00112BD0` (`decomp/src/nonmatching/`, 457 lines; the PC compiles it
  through `patches/nonmatching.patch`) runs once a frame.
  - It blends the fog and light (1.2.9), then culls by a half-plane from the player's position and
    heading (no frustum) and by the visibility row of the record under the player.
  - It builds `record_draw_lists` and `late_record_draw_lists` **in file order**, and picks mirror
    mode 1 or 2.
- **Drawing:** `Map_DrawRecords` / `Map_DrawLateRecords` (about 160 lines) call
  `model->draw_parts` = `Model_DrawPosed`.
  - The near plane: `Map_ClassifyRecordOnScreen` (`map/map.c:1192`) projects the record's box. A
    record that crosses the guard band goes through `ModelScreen_CutClippedTriangles`
    (`model/model_screen.c:67`), which projects every vertex on the CPU and sends each triangle with a
    corner out of range again as 16 smaller ones.
  - Nothing is clipped exactly. A sub-triangle still out of range is dropped by VU1's ADC.
- **VU1 0x000:** packed 16-bit vertices (0x20 bytes: xyz, normal, flag with ADC 0x8000, uv in 4.12,
  q 0x1000, rgba 0..255).
  - Transform by VU 0x00 (world to screen) times 0x04 (part to world), with perspective ST.
  - Fog: `F = A + B/w` from VU 0x3C, FOGCOL from 0x3D.
  - Three directional lights and an ambient from 0x0C-0x13, and up to four point lights from 0x40-0x48
    (`colour * max(1 - d² w, 0)`).
  - Output clamped at 255.
  - The spec is `port/platform/vu1_hle.c` (597 lines).
- **GS:**
  - PSMT8 and PSMT4 with CT32 CLUTs, trilinear MTBA mipmaps on 30 maps, FGE;
  - TEST 0x5000D (alpha > 0, Z GEQUAL);
  - water records additive (`ALPHA 0x48`, set in `Map_BuildModels`, `map/map.c:429`);
  - coplanar layer meshes blended `(Cs-Cd)As+Cd` with GEQUAL.
- **Size:** map/ 3,196 lines in all, of which about 500 draw; `func_00112BD0` about 250 of its 457;
  `model_screen.c` 639.

#### 1.2.2 Models: posed and skinned

- **Posed** (`PosedModelFile`; `Model_SetUpPosed`, `model/model.c:750`): parts with position, rotation
  and scale. The hierarchy comes only from a loaded motion. Drawn by:
  - `Model_DrawPosed` (`model.c:835`): the CPU multiplies model x part, and the result goes to VU 0x04;
  - `Model_DrawPosedDepthByPart` (`:1041`, items): Z write per part;
  - `Model_DrawPosedWithAlpha` (`:1153`, map objects): fades in context 2 with
    `ALPHA_2 = (A,B,D, C=FIX) | fade << 32`;
  - `Model_DrawPosedPrebuilt` (`:936`): no caller in C.

  The VU1 program comes from the data: the MSCAL at the end of each part's first block.
- **Skinned** (`ModelFile`, `model.h:329`; `Model_SetUpSkinned`, `model.c:138`): float vertices (0x40
  bytes; ST.w picks the part's matrix or its parent's: two-matrix rigid skinning, no weights).
  - `Model_DrawSkinned` (`model.c:225`) sends the part and parent matrices (0x04, 0x08), the rotation
    matrices for the sphere map (0x24, 0x28), and the colour scale and bias at 0x64-0x65 (the tint;
    scale 0 by default, so the vertex colours are ignored and `u = tint`).
  - VU1 0x280 (`port/platform/vu1_hle_280.c`, 808 lines). It has an optional second pass, gated per block
    by TOP+0xA4.z: the same positions again, ST from the sphere matrix times the normal, white, blended
    `Cs + Cd·As`. This is the "phong" shine (`arm_phong.tx2`).
  - `Model_DrawSkinnedTwoPass` (`:361`): no caller in C.
- **Per model** (`ModelMatrix_BuildDrawMatrices`, `model/model_matrix.c:311`):
  - the light matrix divided by the model's scale;
  - unlit (`model_flags 0x400`): colour times alpha as ambient;
  - the fog block;
  - up to four point lights from `LinkedPoint_FindHitsForModel` (`model/linked_point.c:109`).
- **The data changes in place.** Several functions rewrite the loaded file bytes that the draws
  reference: `Model_SetPrimBits`, `Model_SetTexture`, `Model_SetAlphaBlend`, `Model_SetTextureBase`,
  `Model_SetVertexColour`, `Model_FadeVertexColoursNear`, `Model_SetUvsFromScreen` and
  `Model_Save/RestoreGsWordsAndUvs` (`model.c:553-1543`), and `Wobble_ApplyWaveUvs` every frame. A GPU
  cache has to be told about these writes.
- **Animation is all on the CPU** (`model/motion.c`, `model/model_motion.c`: Hermite keys). It writes
  part position, rotation and scale; the GPU sees only matrices. This is what makes interpolation
  exact: the game has the parts' PRS.
- **Callers:**
  - entities: `Entity_DrawAll` (`entity/entity_world.c:413`) draws opaque ones at once and translucent
    ones (alpha < 128, model flags 0x200, type 0x10) through the draw list, with
    `Entity_PrepareModelDraw` (`entity/entity_pool.c:966`). The tint is the hit flash (255, 64, 64),
    ailment pulses, or 128;
  - map objects: `MapObject_DrawAll`;
  - items and gold: `MapItem_DrawAll` (`item/map_item.c:578`), `Gold_Draw` (`item/gold.c:234`), and the
    picked item after a depth clear;
  - the Fire Lord's afterimages: `FireLord_DrawAfterimages` (`enemy/fire_lord.c:868`), the only enemy
    with draw code of its own (two copies in the draw list);
  - the refracting entity, type 0x47: `Entity_PrepareScreenTexturedDraw` (`entity_pool.c:999`) copies
    the frame into a 256x256 texture at 0x3540 (`GsDraw_CopyFrameToTexture`) and maps it by screen UVs.
- **Enemies:** 78 files, 38,250 lines, and none of them draws. They set `model_alpha` (all 78; fades in
  28), the shadow bit 17 (5 files) and hidden parts (onyx, high skeleton, larva).
- **Size:**
  - model/: 4,457 lines, of which about 1,450 in `model.c` draw;
  - entity/: about 330 lines of draw code;
  - item/: about 300;
  - map_object/: about 250.

#### 1.2.3 The sky

- **No separate pass.** The sky is map records with flag bit 31. They are domes on maps 000 and 013,
  and backdrops on 002, 004, 021, 027, 098 and 099 (README.md, "Draw distance").
- **Position.** Each visible frame `func_00112BD0` sets the record's model position to
  **`player_state`'s position: the feet, not the eye.** The eye is 220 units up, with the head bob,
  landing dip and shakes on top. Rotation and scale stay the file's. The culling box does not move with
  it.
- **Depth.** The sky is drawn in its list's file order with the normal state: Z GEQUAL, Z written, fog
  on. **Its Z is its real distance.** Whatever lies beyond its radius is behind it, and the fog and the
  clear colour hide the rest. The dome's radius is in the data, not the code; it has to be measured
  (P0 task).
- **The clouds** are scrolling textures (1.2.8) drawn into a 128x128 target each frame and sampled by
  the dome.
- When both slots have a sky, only the current slot's is drawn (`Map_DrawRecords`).
- PC: `FrameInterp_MarkSky` marks it for interpolation, and `DrawDistance_SkyFog`
  (`game/draw_distance.c`) keeps its PS2 fog.

#### 1.2.4 The arm and the held weapon

- **Loading and placement.** `Player_LoadArmModels` (`player/held_weapon.c:132`) loads skinned models
  for the arm and the weapon. `Player_UpdateArmModels` (`:234`) puts them **in world space, a few units
  in front of the eye.** They have no projection of their own; they use the world's `world_screen_matrix`.
- **Light.** They use `held_item_light_blocks` (effectively unlit: ambient 1, light colours 0) and the
  current fog. They get no point lights.
- **Depth.** `GsDraw_ClearDepth` (`gfx/gs_draw.c:305`: a sprite that fails the alpha test and writes
  Z 0) comes first. The arm is then drawn with GEQUAL and Z writes, so **it never goes into a wall: the
  depth is cleared, not separate.**
- **Textures.** Its tables are queued again every frame: `arm_phong.tx2`, the arm's table and the
  weapon's.
- **Weapon 0x2E:** part 5 is drawn additive.
- **The trail.** `WeaponTrail_Update` (`:295`) projects the blade's bone points on the CPU, and
  `Player_DrawArm` sends a PATH2 strip of colours and XYZ2: additive, Z ALWAYS.
- **Size:** `held_weapon.c` 536 lines.

#### 1.2.5 Effects (sfx, VU1 0x700)

- **Data** (`Effect_Init`, `effect/effect.c:81`):
  - `param/sfx.prm`: 0x400 `EffectDef`s of 0x120 bytes;
  - `sfx/sfx.dat`: 0x200 posed models;
  - `sfx/sfx.tmx`: the textures. They are uploaded once, read back from VRAM 0x2E40 into a packet
    (`Effect_CaptureTextures`), and that packet is sent again (`Effect_UploadTextures`, `:401`) every
    frame, and after any frame copy that overwrote them.
- **Drawing.** `Effect_DrawAll` works out each effect's texture frame and matrix, then calls
  `DrawList_Add` with a layer from its placement:
  - 0: in the world, sorted;
  - 1-2: view-locked;
  - 3: on top, after the screen passes and the depth clear.

  The rest goes through `Model_DrawPosed` with `model_flags 0x800`, which UNPACKs a colour (0x64) and a
  TEX0 substitution (0x65.w the TBP to replace, 0x66 the new TEX0): **flipbooks are a TBP step of
  `base + n * stride`.**
- **Billboards** are made in `ModelMatrix_Build` (`model_matrix.c:173`) from draw flags 0x100, 0x200
  and 0x400.
- **Blend hooks** (`effect.c:995-1089`):
  - additive `0x48`, subtractive `0x42` and blended `0x44`, each with no Z write and Z GEQUAL;
  - cut-out, with Z written;
  - an "on top" version of each, with Z ALWAYS.
- **VU1 0x700** (`port/platform/vu1_hle_700.c`, 395 lines): one directional light, one model colour,
  the texture swap. Its Q is `1/(16 w)` (STEP3.md, "Higher frame rates").
- **The kinds.** `effect_kinds_a.c` and `fx_kinds.c` (about 190 functions, 4,700 lines) only move,
  scale, spin and colour; none of them draws.
- **Size:**
  - `effect.c`: 1,089 lines, of which about 400 draw;
  - `item_effect.c`: 369 lines (the item-use effect, textures at 0x3B40, `TEST 0x5010D`).

#### 1.2.6 Shadows (VU1 0x5A0)

- **The model.** An entity's chr file can carry a second posed model, the pool model:
  `EntityPool_BuildBlockModel` (`entity/entity_pool.c:1037`) gives it program 0x5A0 and fog on.
- **Drawing.** `Entity_DrawPoolModels` draws it for each entity with bit 17 set, with these steps:
  1. it copies the entity's pose;
  2. it multiplies its matrix by `fire_lord_model_matrix` (`entity/entity_types.c:147`, misnamed: the
     matrix is diag(1, 0, 1) with y -5), **which flattens the model onto a plane: a planar drop
     shadow.** Whether the plane is in model or world space is to be confirmed from `Matrix_Multiply`'s
     order;
  3. it draws in context 2 with DATE and FBA, so each pixel is darkened once
     (`GsDraw_SetScreenAlpha(0)` first);
  4. the blend is `(Cs-Cd)·FIX + Cd` with FIX = alpha/4 (0x20, 25%);
  5. program 0x5A0 (`vu1_hle_5a0.c`) outputs a colour of 0 and a fog of 0, so the GS's fog makes the
     shadow **the area's fog colour**.
- It is drawn in the main view only, not in the reflection.
- **What it costs today.** About 1,200 overlapping triangles a batch, which the native GS has to cut
  into about 240 runs (STEP3.md, "Render passes"). A stencil does this in one pass.

#### 1.2.7 Reflections and mirrors

- **Mode 2** (record flag 0x200; maps 008 and 009):
  - `Map_SetOffscreenTarget` (`map/map.c:1082`): a 256x256 **CT16S** target at 0x2BC0, sharing the main
    Z buffer addressed 256 wide;
  - projection 1 (ax 0.4, ay 0.571), and camera mode 1: the world mirrored about `mirror_height`, the
    water record's origin y, with no clip plane and the winding flipped;
  - the records flagged 0x20, the objects, entities, items, late records and effects;
  - then the real frame. Its water record is drawn by `Wobble_DrawRippledModel` (`gfx/wobble.c:65`): the
    CPU projects each vertex and makes **screen-space ST** (Q 1) from the vertex moved in y by a sine
    (`wobble_sines`), bilinear, alpha from the map's tint, a PATH2 strip; then the water mesh itself,
    additive.
- **Mode 1** (record flag 0x2): the mirrored world is drawn straight into the frame, then covered with
  the map's tint (`Map_CoverScreenWithMapTint`, `map.c:1122`). The depth is cleared and the real world
  drawn over it. **The reflection shows only where the real world has holes.**
- **Retail quirks:**
  - in mirror frames the scroll updates run twice, so the scrolling goes twice as fast;
  - with `--widescreen`, projection 1 keeps ax 0.4.
- **The refracting entity** (1.2.2) is the other render-to-texture.

#### 1.2.8 Scrolling textures and water UVs

- **`MapScroll_UpdateDraw`** (`map/map_scroll.c:184`): every `period` frames it draws a PSMT8 or PSMT4
  source as a sprite into a 128x128 CT32 target, with each entry's own blend. Layers composite, and the
  records sample the target by TBP. The sprite ends at 0x7FF8, half a pixel short, which leaves the seam
  that `--seamless-scroll` mends.
- **`MapScroll_UpdateCopy`** (`:54`): **scrolls texels in place** by local-to-local BITBLT through 0x3C40.
  The VRAM itself holds the scroll's state. `MapScroll_CarryPairs` keeps the phase across linked maps.
- **`ScrollTexture_DrawAll`** (`gfx/scroll_texture.c:17`, def 0x91 objects): four 128x128 layers into
  0x3A40.
- **Wave UVs** (flag 0x100): `Wobble_BuildWaveUvs` and `Wobble_ApplyWaveUvs` (`wobble.c:327`, `:379`)
  write 16 oscillators into the file's UVs every frame.
- **Lava** is a coplanar glow mesh over the lava, plus a scrolling target.

#### 1.2.9 Fog and lights: the VU1 fixed block

| VU qwords | What | Filled by |
|---|---|---|
| 0x00-0x03 | world to screen | `GsDraw_QueueVu1Matrix` (`gfx/gs_draw.c:356`), after every `GameLoop_SetCameraMatrices` |
| 0x04-0x07, 0x08-0x0B | part to world, and the parent's (skinned) | the `Model_Draw*` functions |
| 0x0C-0x13 | light directions (divided by scale), and light colours + ambient | `ModelMatrix_BuildDrawMatrices` from `model->light_block` |
| 0x24-0x2B | rotation-only matrices for the sphere map | `Model_DrawSkinned` |
| 0x3C | fog `(0, 0, A, B)`: `F = A + B/w`, clamped 0..255 (255 is no fog) | from `model->fog_block` |
| 0x3D-0x3E | FOGCOL as a GIF A+D packet | the same |
| 0x40-0x48 | point lights: a count of up to 4, positions with falloff, colours | `LinkedPoint_FindHits*` (`model/linked_point.c`) |
| 0x64-0x66 | tint (skinned: scale and bias), effects' colour and TEX0 swap | the `Model_Draw*` functions |

- **The blocks change every frame.** `func_00112BD0` copies each slot's 32 fog and 32 light blocks into
  `map_draw_state` (which is how `MapSetup_*` and `ScriptCmd_SetMapFog`/`SetMapLight` take effect), and
  then:
  - multiplies the ambient by 1.3, except on some map tags;
  - adds the light spell's tint;
  - blends a "current" fog block, light block and background colour over 30 frames when the player moves
    into another record's (records with flag 0x10 and their objects use these).
- **Myu's Necklace** blends the light colours of every block towards glow colours
  (`map_object.c:447-637`).
- `DrawDistance_ScaleFog` multiplies `B` for `--draw-distance`.
- **Camera** (`GameLoop_SetCameraMatrices`, `core/game_loop.c:654`):
  - `sceVu0ViewScreenMatrix(scrz 512, ax 1 (0.75 widescreen), ay 1, cx cy 2048, zmin 1, zmax 0xFFFF0,
    near 1, far 65535)`: **larger Z is nearer**, and the 16-bit Z buffer gets Z >> 4, about 65536/w;
  - modes: 0 normal, 1 mirrored, 2 view-locked;
  - projection 1 is the reflection's.

### 1.3 Full-screen passes (`core/game.c` 823-1863, `gfx/gs_draw.c`)

Nearly all of them read the frame being drawn as a 1024x512 CT32 texture, in ten 64-pixel strips:

| Pass | Gate | What | Modern equivalent |
|---|---|---|---|
| `Game_ApplyBrightness` (`game.c:1641`) | always, last | the frame times brightness/0x80 (can be above 1) | a colour scale at output |
| `Game_TintScreen` (`:1693`) | underwater, King's Map | the frame times a colour | a colour scale |
| `Game_BlendScreenOffsetDown` / `UpDown` (`:1140`, `:1192`) | bits 42, 43 | the flicker filter, half a row down | off (already, `--flicker-filter`) |
| `Game_BlendPreviousFrame` (`:1264`) | bit 40, aiming | the frame shown blended in at 0x40 | a history buffer |
| `Game_ShiftScreenByViewTurn` (`:1415`) | bit 45 | the other field, shifted by the turn, SCANMSK | off (already, whole frames) |
| `Game_DrawHazeLayers` (`:917`) | bit 44 (nothing sets it) | three full-screen sprites at fixed Z redraw pixels farther than each | from the depth buffer |
| `Game_DrawWobblingScreen` (`:1749`) | underwater, MP drain | an 11x8 spring grid of strips over the frame copied to 0x2BC0 | a warp pass from the same grid |
| `GsDraw_CopyFrame` (`gs_draw.c:127`) | before the wobble, map change | BITBLT of the frame to 0x2BC0 (overwrites the effects' textures) | an image copy |
| `GsDraw_CopyFrameToTexture` (`:471`) | entity type 0x47 | the frame shrunk into 256x256 at 0x3540 | an image copy |
| `GsDraw_FillScreen` (`:248`), `Game_DrawFade`, `Player_DrawScreenFade`, `Player_DrawScreenTints` | fades, flashes, tints, message dim, pause | flat full-screen sprites, `0x44`/`0x48` | quads |
| `GsDraw_ClearDepth` (`:305`) | before the picked item, layer 3 and the arm | Z 0 by an alpha-failing sprite | a depth clear |
| `MapChange_FadeOutCapture` (`map/map_change.c:184`) | map change | the captured frame faded | a quad |

### 1.4 The 2D and the screens of their own

| What | Where | Path | Notes |
|---|---|---|---|
| HUD | `Hud_Draw` (`ui/hud.c:106`), 194 lines | PATH1 0x276 sprites | `gage.tx2` sent again every frame; PSMT8, bilinear. **The MP orb** is a destination-alpha mask: the texture's alpha is compared with a GEQUAL fill level, FBA marks pixels, then DATE draws the orb (`hud.c:142-167`). STEP3.md says the orb's texture is made anew each frame; the survey found no CPU writer (open) |
| Messages | `Message_Draw` (`ui/message.c:191`), 689 lines | PATH2 | `font.tmr` 512x512 PSMT4 at 0x3B40; **two glyph sheets in one texture, chosen by CLUT** (CBP 0x3FE0 / 0x3FE1); nearest; typewriter alpha. `font.txc` is the text, not the font |
| Splash and signs | `Splash_Draw` (`ui/splash.c:234`) | PATH2 upload + sprite | `msg/mNNNN.tm2` at 0x2E40 (over the effects' textures) |
| Picture map | `MapViewer_DrawPictureMap` (`ui/map_viewer.c:305`) | PATH1 | markers in 640x448 pixels |
| King's Map | `MapViewer_RunKingsMap` / `DrawKingsMap` (`:636`, `:755`) | own loop, the world's records via `Model_DrawPosed` in context 2, additive at FIX 0x40, alpha >= 0x80 | then the flicker filter, a sepia tint, overlays blended by **destination alpha** (`0x54`) |
| Title, game over | `ui/title.c`, 545 lines | PATH3, field mode 640x224 | `y/2` into the field, half-line offset |
| Menus (pause, equipment, journal, shop, smith, save, options, brightness, memory card) | `Menu_Run` (`ui/menu_screens.c:2160`), `menu_draw.c` (4,611 lines), `menu_screens.c` (7,649), `trade.c` (6,373) | PATH3 immediate, field mode, Z32 | layouts from 36 `*_para.dat` files in GS units; the backdrop is the play's frame shrunk into 0x3240; **the item models in the menus are transformed on the EE** (`PoolPart_DrawTinted`, `menu_draw.c:2191`), not by VU1 |
| Movies | `gfx/movie.c`, 2,156 lines | PATH3 | MPEG pictures in macroblocks; whole frames on the PC |
| Loading | none | | there is no loading art; the map change fades the captured frame |

**Counted:**
- 25 kinds of drawing in play: 1.2.1-1.2.8 and the passes in 1.3;
- 9 kinds of 2D and separate screens (above);
- 4 VU1 programs that draw (plus 0x276, a pass-through), and 6 model draw functions (4 called);
- about 40 functions to patch for all of the 3D in play, and a dozen more for the passes;
- about 25,000 lines of UI code that would only matter if the menus moved too.

## 2. The developers' assumptions

Andrew's rule from Verdite2: draw the game with a modern renderer without asking what the developers
counted on, and the skybox shows up in your face, because geometrically it *is* close. KF4 has
several of these. Each needs a decision: reproduce it (the PS2's look) or replace it with what it was
standing in for (the modern look). The "PS2" column is what the hosted path does by default.

| # | Assumption | Where | What a naive modern renderer shows | PS2 (exact) | Modern option |
|---|---|---|---|---|---|
| A1 | **The sky is a real mesh at the player's feet**, drawn with Z and fog, hidden past its radius by fog and the clear colour | `func_00112BD0` copies `player_state` into the record; flag bit 31 | with a larger far plane, less fog or a free camera: the dome's edge, things beyond it hidden *behind* the sky, the dome 220 units off the eye | draw it as now: at the feet, real Z, its own fog (`DrawDistance_SkyFog`) | centred on the eye, depth at the far plane (depth range clamp), drawn first; the horizon moves by the 220-unit offset, so the owner should look at it |
| A2 | **The fog is the far clip.** The clear colour is the blended background colour; the culling is a half-plane plus a visibility table, with no distance cut | `func_00112BD0`, `Map_DrawRecords` | records the PS2 never shows (behind a turn, not in the table) leave holes in the fog colour; straight edges at a long draw distance | per-vertex fog `A + B/w`, clear to the background colour | keep the culling (gameplay); a longer draw distance stays opt-in |
| A3 | **A 16-bit Z of about 65536/w, larger nearer, GEQUAL, later wins ties** | projection; `TEST 0x5000D`; `ZBUF ... Z16S` | **coplanar layer meshes** (a wall's second texture, the darkness strips in map 000's arches, signs on walls, the lava's glow) fight or vanish with float depth; seams where surfaces meet | GS-compatible Z (the native GS's `tolerant` rules: within one Z for layers, half a Z for others) | find the layers when a map loads (meshes over the same surface) and draw them as decals (depth EQUAL against the base, or a bias); reversed-Z D32F for everything else |
| A4 | **Draw order stands in for sorting.** Records in file order; late records unsorted; the draw list sorted by distance from the *player* (1,000 buckets of 5 units, LIFO in a bucket) | `func_00112BD0`, `gfx/draw_list.c` | translucent things in another order: different overlaps of fire, glass and water | keep the order exactly: draws are submitted in the game's order | the same; the order is the look |
| A5 | **No clipping, only guard-band rejection** (ADC), plus the CPU's 16-way cut for map records | VU1, `ModelScreen_CutClippedTriangles` | real clipping draws triangles the PS2 dropped: near the eye, under the floor, models' backs close up | drop what the PS2 drops (reject a triangle with a vertex behind w = 1 or out of the guard band; cut the records as the CPU did) | real clipping; skip the CPU cut. Mostly fills holes; review on close-ups (the arm round the shoulder, doors) |
| A6 | **The arm is world geometry a few units from the eye**, kept out of walls by a depth clear before it | `GsDraw_ClearDepth`, `Player_DrawArm` | without the clear the arm goes into walls; with a near plane of more than about 1 unit it is cut | the near plane at w = 1, a depth clear before layer 3 and the arm | the same, or a view-model depth range; the FOV stays the game's |
| A7 | **No back-face culling**; the mirror flips the winding | GS | culling on hides one-sided walls and the reflection | culling off | off |
| A8 | **Mirror mode 1 counts on holes in the world**; the mirror plane is the record's origin y, with no clip plane | `GameLoop_DrawFrame` mode 1 | with a clip plane or a different order, things under the water show or the reflection is lost | the same order: mirrored, tint, depth clear, world | a clip plane is an improvement only if the owner wants it |
| A9 | **The reflection is 256x256 at 16 bits with screen-space UVs** (Q 1) and a sine in y; projection 1 is not widened for widescreen | `Map_SetOffscreenTarget`, `Wobble_DrawRippledModel` | at a higher resolution and with widescreen, the lookup does not line up | render target at 256 x scale, CT16-like quantisation optional | full-resolution reflection, projection matched to widescreen (owner) |
| A10 | **Alpha: 0x80 is 1.0.** Vertex colour MODULATE is `Cf*Ct >> 7` (0xFF almost doubles), As and FIX the same, COLCLAMP; the alpha test is GREATER 0 (cut-outs) | everywhere | colours halved, cut-outs blended, the brightness pass dark | the GS's arithmetic in the shader (from `gs_frag.h`) | the same maths in floats (`--native-colour high` already does this) |
| A11 | **Fog and Gouraud colour are interpolated linearly on the screen**, not perspective-correct; textures are | GS | per-pixel perspective-correct varyings give slightly different fog bands and shading on large triangles | `noperspective` for colour and fog | perspective-correct (owner; small) |
| A12 | **Lighting is per vertex**: three directional lights and an ambient, up to four point lights a model picked by box overlap, clamped at 255; light matrices divided by scale; skinned tint as scale and bias | VU1, `ModelMatrix_BuildDrawMatrices` | per-pixel lighting changes the look of every room | per vertex, the VU1 programs' maths | per-pixel is a mod, not a default |
| A13 | **Mip level from Q (distance)**, K and L from the data, 4-bit bilinear weights, CLUT textures (CSM1, CT32 palettes, CSA 0), PSMT4 and PSMT8 uploaded as CT32 blocks | GS, data | wrong mip levels, wrong palettes if the swizzle is skipped | the GS's LOD and filter (the native GS's shader code) | `--native-lod`/`--native-aniso` as today |
| A14 | **Textures are VRAM addresses**: slots overwrite each other within a frame, the effects' textures are overwritten and sent again, the font picks a sheet by CLUT, scroll textures are moved in VRAM, render targets are read as textures (0x2BC0, 0x3A40, 0x3540, 0x3240) | 1.2, 1.4 | a texture cache keyed by TBP shows the wrong texture; a cache keyed by source misses render-to-texture | hosted: the native GS's VRAM model as it is | standalone: textures by source (the chunk's bytes), a page table of who owns which TBP, render targets by name |
| A15 | **Half-pixel and edge conventions**: 2D ending at 0x7FF8, the HUD's 16-pixel inset, XYOFFSET 1728/1824, the movie's field UV offsets, the menus at 640x224 with half-line offsets | 2D | seams and lines at a scale; blurry text | the native GS's `flat_2d` and `sample_level` rules | 2D at output resolution (P9) |
| A16 | **Screen passes read the frame being drawn** and some feed back (the previous frame, the smear) | 1.3 | drift if done in floats each frame | integer arithmetic for feedback passes (STEP3.md) | a history buffer in floats is fine once standalone |
| A17 | **The shadow darkens each pixel once** (DATE + FBA) with the fog colour at 25% | `Entity_DrawPoolModels` | overlapping triangles darken twice | stencil: first write wins | the same |
| A18 | **Two map slots share one world.** `Map_MakeCurrent` (`map.c:586`) shifts every record, model, entity and light when the neighbour becomes current | map change | interpolation and caches see a jump | treat it as a cut, or move the previous frame by the same offset | the same |
| A19 | **30 Hz steps**: scrolls, waves, glows, fades and blends move once a game frame; mirror frames scroll twice | everywhere | nothing at 30; at 240 they step | interpolate their phase between frames where the phase is known (scroll, wave, sine); flipbooks stay frame-stepped | the same |
| A20 | **The screen is 640x448 at 4:3 and an NTSC TV**: the flicker filter, the field shift, the brightness pass after the HUD | 1.3 | blur, combs | already off where TV-only (`--flicker-filter`, whole frames) | brightness as an output scale |

Measure before relying on any of this: the sky's radius and distance per map (A1); which maps have
layer meshes and how they pair (A3, from the map tour's `--native-layer-z` work); and the HUD orb's
texture (1.4).

## 3. Architecture

### 3.1 Where it hooks

The patched game code calls a small C API from inside the draw functions it replaces. Each port is one
`if` at the call site, so the original stays in the patch as the fallback:

```c
/* map/map.c, Map_DrawRecords, as a patch */
if (port_native_draw(ND_MAP)) NativeDraw_Model(model, ND_MAP_RECORD, record_id);
else model->draw_parts(model);
```

- **`game/native_draw/`** (PC-only game code, edited in place like `game/frame_interp.c`) is the
  **adapter**. It knows the game's structs: `ModelInstance`, `ModelPart`, `MapDrawRecord`, the fog and
  light blocks, `camera_state`, `map_draw_state`. It turns them into renderer calls. This keeps the
  patches thin (RULES.md: change as little text as you can) and keeps game knowledge out of `port/`.
- **`port/render/`** is the **renderer core**. It knows nothing about KF4:
  - meshes, materials, textures, cameras, lights, fog, draw lists, 2D quads;
  - Vulkan on the native GS's Granite device, so the two share images;
  - GLSL in `port/render/shaders/`, compiled as the native GS's are (`tools/build_shaders.py`).

### 3.2 The renderer API (first version)

```c
/* port/include/port/render.h */
typedef uint32_t RdMesh, RdTexture, RdTarget;

/* Meshes: made from the game's own blocks, kept on the GPU. key = the block's address in game RAM;
   gen changes when the game writes the block (Model_Set*, Wobble_ApplyWaveUvs ...). */
RdMesh rd_mesh_from_block(uint32_t ee_addr, uint32_t gen, RdVertexFormat fmt /* PACKED16, FLOAT_SKIN */);
void   rd_mesh_touch(uint32_t ee_addr);              /* the game wrote it */

/* Per frame. id = a stable identity: (kind, record or instance address, part). */
void rd_begin_frame(uint32_t game_frame);
void rd_camera(int view /* main, reflection, view-locked */, const RdCamera *c);   /* eye, yaw, pitch, roll, projection */
void rd_draw(const RdDraw *d);   /* mesh, id, part matrices (and parents'), material, lights, fog, tint, flags */
void rd_clear_depth(void);
void rd_pass(RdPassKind k, const RdPassParams *p);   /* brightness, tint, fade, previous frame, wobble grid, haze */
void rd_end_world(void);
```

- **`RdDraw`** carries what the VU1 fixed block carries today, by value:
  - the part-to-world matrices (and the parent set for skinning);
  - the light matrix and the ambient;
  - up to four point lights;
  - the fog `A, B` and the fog colour;
  - the tint (scale and bias);
  - the effects' colour and texture swap;
  - the GS state the call site set (blend, test, Z write, context);
  - the program (0x000 / 0x280 / 0x5A0 / 0x700 maths).
- **Materials** start as "the GS state of this block" (TEX0, TEX1, ALPHA and PRIM from the block's A+D
  set-up, and the TEST/ZBUF/ALPHA registers of the current context). In hosted mode that is all the
  material needs to be. In standalone mode a material is resolved to textures and a pipeline key.
- **Data is captured at queue time.** The game builds the chain a frame before it is drawn, and changes
  its state in between, so `rd_draw` copies everything into a per-frame side buffer (double-buffered
  like `packet_buffer`). Mesh bytes are snapshotted into the cache when their gen changes.

### 3.3 Hosted mode: inside the native GS's stream (the transition)

- **In the chain.** `rd_draw` also queues a GS register list writing `PORT_GS_REG_NATIVE_DRAW` (a new
  address the GS does not have, like `PORT_GS_REG_FRAME_INTERP` 0x7D and `_WIDESCREEN` 0x7E) with the
  draw's index in the side buffer. It is queued at the very place in the chain where the replaced
  packets would have been.
- **On the GS's thread.** The native GS meets the register in order with the uploads, the register
  writes and the PS2-path draws. It flushes its batch and runs the native draw:
  - into its current frame target and Z buffer;
  - with its current VRAM textures (decoded by its texture cache from TEX0 as today);
  - with its blend, test and Z state, through its own pipelines and fragment shader (`gs_frag.h`).

  The new vertex shader does what the VU1 program did (the spec is `vu1_hle*.c`). Clipping is the GPU's,
  or a guard-band reject to match the PS2 (A5).
- **What this buys:**
  - The coexistence question answers itself: one frame, one target, one Z buffer, one order. A screen
    pass that reads the frame reads the native draws too. A PS2-path layer mesh tests against a native
    base wall in the same Z.
  - Depth is the GS's: the shader writes `Z16 = round(zmax/w) >> 4` scaled as the native GS stores it,
    and the `tolerant` layer rules apply unchanged.
  - Textures need no new work. The slot uploads, scroll targets, the reflection target and the font's
    CLUTs all behave as today.
  - `--native-scale`, MSAA, widescreen and the presenter work unchanged.
- **What it does not buy yet:** the VRAM coherence bugs (write-backs) and the 2D's half-pixel rules
  stay, because the targets and the 2D are still the GS's. Hosted mode is about the 3D's geometry and
  its motion.
- **A/B in one run.** The patched site can queue both the native draw and the original packets, each
  inside a pair of marks (`ND_ALT_BEGIN/END`). The GS front end drops one set, or, with
  `--native-draw-compare`, draws the frame twice into two target sets and compares them as
  `--renderer both` does (the heat map, `--stats`). VU1 still runs the packets that get dropped, so this
  is for testing only. This is the same trick that made step 3 safe: every phase is checked against the
  PS2 path, frame by frame, live.
- **Captures.** A `.kf4gs` capture holds the chain. It must also carry the frame's side buffer and the
  mesh blocks it references (game RAM ranges), so that `kf4-gsreplay` can replay and compare native
  draws. That is a P0 task.

### 3.4 Standalone mode: the end state

Once all of the play's 3D and the passes that read the frame are ported (P0-P7), the scene no longer
needs the GS's targets:
- **Targets.** The scene renderer draws into its own: reversed-Z D32F; RGBA16F or RGB10A2 colour;
  MSAA as a real sample count (A3's decals, not tolerance hacks).
- **Textures** are resolved from their source:
  - The upload functions (`TextureTable_Queue*`, `Map_QueueSlotPacket`, `ModelTexture_SendForModel`,
    `Effect_UploadTextures`, `Hud_Load`, splash) tell a **page-ownership table** which source chunk
    (game RAM address and hash) now owns which TBP range. They do this in the CPU's chain order, so the
    owner of a TBP at the time a draw is queued is the texture that draw sees on the PS2.
  - A texture is decoded once by uploading its chunk into a scratch VRAM and reading it back in the
    sampled format (`gsn_vram.c` does both). The swizzle stays exact without the real VRAM.
  - Render targets (scroll, reflection, frame copies) own their pages by name.
  - Hash-keyed textures also make **texture packs** possible (mods).
- **The 2D, menus, title and movies** stay on the native GS. Its picture (2D only, alpha kept) is
  composited over the scene, in the chain's order: the GS draws the HUD and the passes after the world,
  so the composite comes after `rd_end_world` and the screen passes.
- **VU1 and VRAM.** VU1 does not run for anything ported, and VRAM write-backs go away for the scene.
- **Switching.** `--renderer native` (today's: native GS for everything), `hybrid` (hosted, the default
  once P1 is solid), `modern` (standalone, after P8). `gs` (paraLLEl-GS) stays the reference.

### 3.5 Frame interpolation becomes exact

For ported draws, a picture at time t between game frames k-1 and k is drawn from the two frames' side
buffers:
- **The camera** is built from `camera_state` (eye, yaw, pitch, roll) at both frames, slerped and
  lerped, with the cut rules of today (0.8 rad, 400 units). It is not undone from the matrices.
- **Draws are paired by identity**, not by batch hash:
  - `(ND_MAP_RECORD, slot, record)`;
  - `(ND_MODEL, ModelInstance address, part)`;
  - effects by their table entry.

  Two look-alike models can no longer swap.
- **Part matrices** are interpolated as translation (lerp), rotation (quaternion slerp) and scale
  (lerp), decomposed from the part-to-world matrices; scale is uniform or axis-aligned in this game.
- **Billboards** are rebuilt for the picture's camera (from the draw flags).
- **The sky** is placed at the interpolated feet.
- **The arm** is interpolated in eye space.
- **A draw present in one frame only** appears or goes at the frame, as now.
- **Things the game makes on the CPU** (the water ripple, the wave UVs, the wobble grid, the trail)
  have their phase or points at both frames: interpolated. Flipbooks and the HUD orb step at the game's
  rate, which is their authored rate.
- **A map change** (A18) is a cut, or the previous frame shifted by the same offset.
- **What stays as today.** The PS2-path draws in the same frame keep today's replay. In hosted mode
  both are in one recorded stream: the native GS's recording ("On the GPU") records native draws as
  calls whose per-picture constants (the matrices at t) come from a buffer filled once per picture.
  This is simpler than the vertex moves it replaces.
- **Timing does not change.** The game logic stays at 30 Hz, nothing runs twice, and the game's own
  frames are its own (the rule since the first version).

### 3.6 Switches

- `--native-draw off|on` and per category: `--native-draw map,models,skinned,arm,sky,effects,shadows,
  reflection,water,passes` (the overlay's Debug page, live, `settings.json`). **Each category can be
  turned back to the PS2 path live**, which is how a bug is isolated.
- `--native-draw-compare`: both paths, compared each frame (3.3).
- Per assumption (section 2): `--sky ps2|modern`, `--clip ps2|gpu`, `--layers ps2|decal` and so on.
  Each is added to PLAN.md's table with its default, as now.
- `--renderer native` with `--native-draw off` is today's game, byte for byte: the patches do nothing
  when the switch is off (checked by boot_test, frame_test and the PATH1 hash, as for every patch).

## 4. The plan

Each phase ends with something that runs, is useful alone, and is checked against the PS2 path in the
same run. The sizes are rough, for the team working with agents.

### Verification common to every phase

- **The same-frame comparison** (`--native-draw-compare`). The pixel difference per frame and a heat
  map, on:
  - the 64 map tour captures, replayed;
  - live runs of 2,500 vblanks on a set of maps;
  - the owner's quick saves (far, sky, map 009).

  The threshold is not zero: GPU floats against VU arithmetic move edges by a pixel and colours by 1
  or 2. Each phase records its numbers like the map tour's (pixels more than 1 apart, worst). The
  attention goes to *regions*, not totals: a missing or extra object shows as a blob.
- **A vertex oracle.** For each VU1 program, the new vertex shader's output (positions, ST, colour,
  fog) is compared with `vu1_hle*.c` on the same batches, taken from captures: within 1/16 pixel, ST
  within a small relative error, colour within 1. This is STEP3.md's stage 3 bar, and it puts any
  difference down to the shader or the rasteriser.
- **Screenshot sets per feature.** A fixed list of quick saves and teleports for each category (the
  arm swinging, the dome walking and turning, fire, the reflection on 009, shadows near the statues on
  000). Dumped PS2-path and native-path at 1x, 4x and widescreen, and put side by side for the owner.
- **Interpolation checks.** At 240:
  - the game's own frames are still byte for byte the same with the native path's interpolation on or
    off;
  - `KF4_INTERP_STILL` still equals the next frame;
  - motion is even (the pixel step per picture, as STEP3.md measures);
  - no lines or jumps on the sky walk.
- **The existing checks** (RENDERER.md): gsn_draw_test, gsn_vram_test, vu1_test, boot_test,
  frame_test (with `--native-draw off` *and* on), the map tour, Linux builds, speed on 000 and 009.
- **An owner review gate** at the end of each phase: the screenshot set and the decisions for that
  phase's assumptions.

### P0: the core, the harness and the measurements (3-4 weeks)

- **Deliverables:**
  - `port/render/` with meshes from blocks (both vertex formats) and the side buffer;
  - `PORT_GS_REG_NATIVE_DRAW` in the native GS, and a pass-through material from the GS state;
  - the switches, and the A/B marks with `--native-draw-compare`;
  - capture support (side buffer and blocks in `.kf4gs`);
  - the vertex oracle harness.
  - The proof: one call site. `MapItem_DrawAll` (items: few, posed, program 0x000) drawn natively in
    hosted mode.
- **Measurements:**
  - the sky's radius and distance on each sky map;
  - the layer meshes per map (how many, how they pair);
  - the HUD orb's texture source;
  - how often the model mutators run per frame (what `rd_mesh_touch` will cost).
- **Tasks:**
  - Serial: the API header and the side buffer (1 agent, first).
  - Parallel after it:
    - the mesh decoder with unit tests against capture batches;
    - the native GS's draw command;
    - the switches and overlay;
    - capture and replay support;
    - the vertex oracle;
    - the measurements (2 agents);
    - review.

### P1: map records (4-5 weeks)

The largest share of draws, and static geometry: the best value for the risk.
- **Deliverables:**
  - `Map_DrawRecords`, `Map_DrawLateRecords` and the slot objects through `NativeDraw_Model`;
  - program 0x000's maths in a vertex shader (lights, point lights, fog, clamp);
  - the CPU near cut skipped for native records, with `--clip ps2` keeping the PS2's drops;
  - GS-compatible Z with the layer rules;
  - the mirror pass (mode 1) and the reflection's subset drawn the same way, since they call the same
    functions.
- **Checked:** the comparison over the map tour and live maps (000, 004, 009, 013, 021, 035, 112, 116);
  the oracle on 0x000; walking the lacing walk at 3x.
- **Useful alone:**
  - VU1 does not run for the world: most of its remaining time;
  - the world's pictures between come from the camera, not from replayed packets;
  - real clipping is available.
- **Tasks:**
  - Parallel:
    - the shader port (with the oracle);
    - the light, fog and point-light packing in the adapter;
    - the map patch;
    - the late-record Z-write and order rules;
    - the mirror modes;
    - review.
  - **One agent per patch file at a time** (`patches/game/map.patch`), since patches are per folder and
    conflict.

### P2: models: posed, skinned, then the arm (4-6 weeks)

- **Deliverables:**
  - `Model_DrawPosed` and its variants (map objects, items, gold, picked item, King's Map's records);
  - `Model_DrawSkinned` with program 0x280's two matrix sets, colour scale and bias, and the sphere
    pass (entities, Fire Lord, arm, held weapon);
  - the draw list's ordering kept: draws go out in the game's order;
  - the mutators wired to `rd_mesh_touch`;
  - the arm's depth clear;
  - the trail as a native strip.
- **The interpolation half:**
  - identity pairing and part-matrix interpolation;
  - remove `objects_pair` for native draws;
  - check the arm's swing and the bats of map 016.
- **Checked:**
  - the oracle on 0x280 (both passes);
  - the comparison;
  - the owner's look at the arm, the enemies, hit flashes and fades.
- **Tasks:**
  - Parallel: posed; skinned and sphere; entity call sites; items and gold; the arm and the trail;
    interpolation; review.
  - Serial: `model.patch` (model.c holds all six functions: one owner).

### P3: the sky (1-2 weeks)

- **Deliverables:**
  - the sky records with `--sky ps2` (as P1 draws them, its Z and fog kept);
  - `--sky modern`: centred on the eye, depth at the far plane, drawn first;
  - the scroll targets stay the GS's (hosted).
- **Checked:**
  - the sky quick saves at 240 (walking, turning, strafing, looking up and down);
  - a comparison with `FrameInterp_MarkSky`'s sky;
  - the draw-distance settings.
- **Owner decision:** A1.

### P4: effects (3-4 weeks)

- **Deliverables:**
  - `Effect_DrawAll` and the draw list's layers 0-3;
  - program 0x700's maths (one light, colour, the TEX0 swap as a flipbook frame);
  - billboards rebuilt per picture;
  - the blend hooks as materials;
  - `ItemEffect_Draw`.
- **Checked:**
  - fire on maps 000 and 009 (the brazier's glow);
  - spells cast on a test map;
  - the oracle on 0x700;
  - pictures between: sparks and flames move, flipbooks step.
- **Tasks:** effect.c call sites; the shader; the flipbook mapping; the on-top layer after the passes;
  review.

### P5: shadows (1-2 weeks)

- **Deliverables:** `Entity_DrawPoolModels` with a stencil (first write wins) and the fog colour at
  FIX/0x80. This removes the ~240 runs a batch that dominate the GPU's time at 240 (STEP3.md, "Render
  passes").
- **Checked:** the statues on map 000, enemies on slopes, a comparison against the DATE path.

### P6: the reflection and water (2-3 weeks)

- **Deliverables:**
  - mode 2's target as a native render target at 256 x scale (or full resolution, owner);
  - `Wobble_DrawRippledModel` on the GPU from the sine phase;
  - wave UVs as a per-vertex offset in the shader (no more writes to the file data, behind its switch);
  - the refracting entity's frame copy;
  - underwater.
- **Checked:** map 009 walking and turning at 240; map 008; the Guardian's Room tint.
- **Owner decision:** A9 (resolution, widescreen projection).

### P7: the screen passes (2 weeks)

- **Deliverables:** `rd_pass` for brightness, tint, fades and fills, the previous frame, the wobble
  grid, haze (from depth), and the map change's capture. The full-screen passes that the native GS
  draws in strips today become a short post chain.
- **Checked:** integer-exact in `--native-colour exact`; aiming (the previous frame); underwater and
  MP drain; the brightness option's range.
- **After P7, nothing in play's 3D or passes needs the GS's targets**, except the 2D drawn after them.

### P8: standalone (4-6 weeks)

- **Deliverables:**
  - the page-ownership table and source-decoded textures;
  - the scene's own targets (D32F reversed Z, float colour, real MSAA);
  - the decal treatment of layer meshes (A3, from P0's measurements);
  - the GS's 2D composited;
  - `--renderer modern`.
- **Checked:**
  - against hybrid on the whole screenshot set (differences expected only where an A-item's modern
    option is on);
  - save states loaded across modes;
  - speed;
  - Linux on lavapipe in CI.
- **Tasks:**
  - Serial: the texture resolver; the composite.
  - Parallel: targets; decals; the switch from hybrid; review; a performance pass.

### P9: the HUD, messages and splash at output resolution (optional, 2-3 weeks)

- **Deliverables:**
  - `Hud_Draw`, `Message_Draw`, `Splash_Draw` and the picture map as 2D quads in 640x448 units, drawn at
    the output resolution;
  - real widescreen placement instead of `Widescreen_Mark`'s squeeze;
  - the MP orb's mask as a shader threshold.
- **Never, unless wanted:** the menus, the title, the movies and the memory card screens. They are
  25,000 lines of PATH3 code, field mode, layouts in GS units, and EE-transformed models. The native GS
  draws them right today.

### Ordering and parallelism for a workflow (at most 10 agents)

- **A team per phase:**
  - a lead who owns the API and integration (one agent, throughout);
  - implementers, one per area in the phase lists above;
  - one independent reviewer per milestone (the team's rule: evidence first, independent review);
  - one measurer, who runs the comparison and the screenshot sets and writes the numbers into the
    phase's notes.
- **Serial points:**
  - the API before anything;
  - the shared shader header before the per-program shaders;
  - each `patches/game/<folder>.patch` has one writer at a time (map, model, entity, effect, core);
  - the switch to hybrid as the default (after P1, owner).
- **Parallel:**
  - P3, P5 and P7 can run alongside P2 and P4 once P1's core is in (different folders and patches);
  - P6 waits for P1 (records) and P2 (entities in the reflection).
- **Agent-sized tasks** are one call-site family, its adapter code, its shader part, its switch and its
  checks: 300-1,500 lines each.
- **Rough total:** P0-P2 about 3 months, P3-P7 about 2 months (partly in parallel), P8 1-1.5 months.

## 5. Risks, open questions, and what to keep

### Decisions for the owner

- **The defaults:** exact (the PS2's) or modern, per assumption A1-A20. The proposal is to keep the
  PS2's by default, as now, except where the artefact belonged to the TV or to working at the GS's level
  (layer lace, missing triangles near the eye).
- **Whether standalone (P8) is wanted**, or whether hosted (P0-P7) is enough. Hosted already gives exact
  interpolation, no VU1 and real clipping; standalone gives the depth and colour precision and the end
  of VRAM coherence.
- **Whether the 2D moves at all** (P9).

### Risks

| Risk | Answer |
|---|---|
| Not bit-exact: GPU floats against VU arithmetic | this path is exact in intent, checked within thresholds and by region; the PS2 path stays for exactness, and `--native-draw off` is today's game byte for byte |
| Mutable model data missed by `rd_mesh_touch` | a debug mode that hashes every referenced block each frame and reports a block whose bytes changed without a touch |
| Upstream decomp drift: many more patched functions | thin call-site patches (one `if`), the logic in `game/native_draw/`; names follow the decomp (the kfiv naming phase is done, so fewer renames); `patches.py upgrade` follows function renames, not struct fields, so the adapter reads structs through a few accessors in one file |
| Patches conflict between agents | one writer per patch file at a time (section 4, "Ordering and parallelism") |
| Save states | the renderer's caches are derived from game RAM and rebuilt after a load (generation reset); in hosted mode the GS's VRAM is in the state as today. In standalone mode GPU-only state must be recomputable: **`MapScroll_UpdateCopy` keeps its scroll in VRAM**, so standalone either keeps a VRAM model for those pages or saves the target's texels in the state |
| Mods | new mods become possible (texture packs by hash, model replacements); existing ones (Myu's Necklace, run key) are game logic and unaffected |
| Performance | fewer VU1 runs and cached geometry make the CPU cheaper; the GPU draws fewer, larger batches; the pictures between cost a re-submit with new constants (cheaper than today's 0.2-0.8 ms) |
| Linux | the same Vulkan code; lavapipe in CI for the shaders; gcc for the adapter |
| Licence | the repository is MIT. Granite (MIT) is still the copy inside paraLLEl-GS (LGPL-3.0+), and `kf4_gs` links both (STEP3.md); the new renderer should not deepen that, so splitting Granite out comes first or with P0. Copy no code from paraLLEl-GS or PCSX2; ico-pc's (MIT) is fine to borrow, with credit |
| Hosted mode inherits the GS's quirks for textures and targets | by design, until P8; they are already solved there |
| The two paths drift apart over time | `--native-draw-compare` in the checks list, and the map tour run with it |

### What to keep

- **The native GS:**
  - it draws the menus, title, movies and memory-card screens, and everything not yet ported;
  - it is the hosted mode's rasteriser, and the reference;
  - its fragment shader (`gs_frag.h`) is the GS's arithmetic, which hosted native draws reuse.
- **The VU1 programs in C:** the specification and the oracle for the shaders, and the fallback path.
- **`frame_interp.c`:** keeps working for the PS2-path draws; shrinks as categories move.
- **`gsn_vram.c`:** the scratch-VRAM texture decoder in standalone mode.
- **The tooling:** `kf4-gsreplay`, the map tour, the screenshot habits, `--stats`, and the "compare what
  is shown" rule.
- **The surveys in STEP3.md:** the census of pipelines tells standalone mode which materials exist
  (about 15 states cover 99% of the draws).

### Open questions

- **The sky's radius per map**, and whether any backdrop is closer than the far geometry it hides (A1).
  *Measured (P0-3).* See "P0-3's measurements" below. Every sky is a dome, not a backdrop. Its top is
  4,666 units above the feet (5,400 on 013), and its rim is 24,252 units out (21,200 on 013). On 002, 004,
  013 and 027 some map geometry that is drawn lies behind the dome, so the PS2's sky hides it.
- **The HUD orb:** is its texture made anew each frame (STEP3.md) or sent again unchanged (this survey)?
  *Measured (P0-3): sent again unchanged.* Only the fill level changes, through the alpha test's reference.
- **The layer meshes per map** (A3). *Measured (P0-3).* 61 of the 64 maps have them. Almost all are
  records of their own, laid exactly in the plane of an opaque record drawn before them.
- **The shadow's flatten:** in model space or world space? It decides whether shadows follow slopes.
- **The cost of hashing mutable blocks:** do the wave UVs and vertex fades touch many blocks a frame?
  *Measured (P0-3).* Only the wave UVs do, on 6 maps: every block of each wave record, every frame. Hashing
  costs about 0.14 ns a byte, or 0.03-0.3 ms a frame. `rd_mesh_touch` is not needed.
- **Interpolating part matrices:** decompose them, or take the parts' PRS from the motion state
  directly (cleaner, but more game structs in the adapter)?
- **SDL3 or other presenter changes:** independent of this; neither blocks the other.

### P0-3's measurements

**How.** `KF4_ND_DUMP=FILE` is a debug-only dump that writes nothing when it is unset. It records what the
renderer core is handed: every mesh once (batches, positions, vertex alpha, triangles), every draw (its
part matrix), each map record's flags with the camera and the model's place, and per frame the meshes
parsed, the bytes hashed and the time that took. `tools/nd_measure.py FILE` turns the dump into the
numbers below. There was one run per map, on all 64 maps: `--native-draw map,items`, `tests/herb.pad`,
`--teleport 1600:N:0` (the map's place 0), and the last whole frame before vblank 2000. The timings are
noisy: up to 8 other agents shared the PC.

**The sky (A1).** It is one record with flag bit 31, on exactly the 8 maps listed in 1.2.3. "Drawn" is
its place among the frame's map records. "Behind the sky" counts the drawn map corners (a sample of up
to about 4,000 per map) whose line from the eye crosses a sky triangle first. On the PS2 the sky hides
those corners, because its Z is real.

| map | flags | corners | rim from the feet | top above the feet | rim height | drawn | farthest map corner | behind the sky |
|---|---|---|---|---|---|---|---|---|
| 000 | 0x82000000 | 432 | 24,252 | 4,666 | 723 above | 32 of 35 | 11,986 | 0 of 4,048 |
| 002 | 0xA0000004 | 432 | 24,252 | 4,666 | 723 above | 1 of 43 | 26,440 | 152 of 4,192 |
| 004 | 0xA0000004 | 432 | 24,252 | 4,666 | 723 above | 3 of 6 | 13,441 | 27 of 4,019 |
| 013 | 0xA0000020 | 336 | 21,200 | 5,400 | 400 above | 1 of 4 | 19,414 | 60 of 2,175 |
| 021 | 0x80000004 | 528 | 24,252 | 4,666 | 2,000 below | 1 of 17 | 3,180 | 0 of 4,019 |
| 027 | 0x80000004 | 528 | 24,252 | 4,666 | 2,000 below | 1 of 41 | 22,881 | 14 of 4,473 |
| 098 | 0x82000064 | 432 | 24,252 | 4,666 | 723 above | 1 of 29 | 6,670 | 0 of 4,637 |
| 099 | 0x82000004 | 432 | 24,252 | 4,666 | 723 above | 1 of 21 | 7,763 | 0 of 5,313 |

- **What the table shows:**
  - All eight skies are domes centred on the feet: the same 432-corner dome on five maps, a deeper
    528-corner one on 021 and 027 that reaches 2,000 below the feet, and a smaller one on 013. None is a
    nearby backdrop; the "backdrops" of 1.2.3 are a texture on the dome.
  - The nearest point of the sky is straight up: 4,446 units from the eye (5,180 on 013).
  - Below the rim there is no sky. The clear colour and the fog show there.
- **The answer to A1.** On 002, 004, 013 and 027, map geometry that is drawn lies behind the dome: 3.6%,
  0.7%, 2.8% and 0.3% of the corners sampled from the start place. This is geometry beyond the rim (on
  002, out to 26,440) or rising through the dome. A sky put at the far plane (A1's modern option) would
  show it. Look at those four maps before choosing that option.
- **Draw order.** Map 000 draws its sky near the end of its list (file order), not first. A sky drawn
  last still hides whatever it covers, because the Z test is GEQUAL.

**The layer meshes (A3).** These are triangles of map records with a corner's alpha below 0x80, matched
to coplanar triangles: the middle within 0.5 units of the other's plane, the same facing line, and
inside it. A layer's base is the last opaque coplanar triangle drawn before it, which is the one its Z
GEQUAL meets. To see a map record by record, run `nd_measure.py --what layers --verbose`.

- **Across the 64 maps:**
  - 61 maps have such triangles (66,241 in all at the measured views), and 50 have true layers;
  - 31,539 lie over an opaque triangle drawn before them, and 31,332 of those (99.3%) lie exactly in its
    plane (within 0.01);
  - 1,308 lie only over translucent ones drawn before them;
  - 1,917 have a coplanar triangle only after them;
  - 31,477 lie over nothing: fades that blend onto whatever is behind (darkness at the end of corridors,
    the sky's clouds, water).
- **How layers pair.** The layer and its base are almost always different records (32,466 pairs), and
  rarely two batches of one record (381). The layer comes later in file order. The darkness strips and
  the second textures are records of their own, drawn after the wall they lie on.
- **What a standalone renderer needs.** It can find the pairs when a map loads (a record whose
  triangles lie in the plane of an earlier record's triangles) and draw the layer as a decal over its
  base. The layer's Z equals the base's exactly, not just within a Z.
- **Per map**, as records / such triangles / over an opaque one drawn before:

  000 24/3538/925, 002 35/4983/2416, 004 5/835/0, 005 9/1310/66, 006 2/29/28, 007 5/5/0, 008 28/2717/875,
  009 11/6024/5865, 011 0/0/0, 012 8/441/40, 013 1/1/0, 014 31/2144/1501, 015 15/360/172, 016 28/1875/1269,
  017 1/96/0, 018 3/274/5, 019 2/134/1, 020 20/1376/1168, 021 6/486/106, 022 3/6/0, 023 3/116/116,
  024 42/4131/459, 025 37/3935/2844, 026 31/2997/1947, 027 38/3312/1236, 028 7/1551/589, 029 4/117/50,
  030 21/710/331, 031 15/1029/593, 032 20/1731/1046, 033 20/1428/820, 034 8/776/250, 035 30/2565/0,
  036 11/567/346, 037 3/123/28, 038 19/1400/590, 039 5/392/392, 040 8/1584/0, 041 0/0/0, 042 13/750/100,
  043 1/196/0, 044 6/672/671, 095 5/1149/92, 096 5/375/241, 097 3/261/6, 098 14/1178/604, 099 16/1624/1432,
  100 5/57/32, 101 0/0/0, 102 10/318/240, 103 3/3/0, 104 1/2/0, 105 11/493/451, 106 12/379/319,
  107 6/421/39, 108 8/157/135, 109 16/223/119, 110 10/394/360, 111 7/194/124, 112 2/330/0, 113 2/96/60,
  114 13/1409/279, 115 5/97/96, 116 8/365/65.

  The counts are for one view per map (place 0, at vblank 2000), not the whole map.

**The HUD orb.** In every run, from the title to vblank 2000, a temporary probe hashed each frame the six
`gage.tx2` chunks in `hud_area` that `Hud_Draw` queues with `TextureTable_QueueAll`. The probe was a debug
build only and is not committed. No chunk changed after it was loaded, on any of the 64 runs.

- **It is sent again unchanged.** Nothing remakes it, and no CPU code writes it.
- **What moves once a frame is the fill level:** TEST's alpha reference `fade << 4 | 0x30005` and FBA,
  over a fixed texture.
- **Why it is sent every frame anyway.** The HUD's pages (TEX0 in `Hud_Load`: 0x3540, 0x3640, 0x36C0,
  0x3700, 0x3740, 0x3780) are scratch for other draws: `GsDraw_CopyFrameToTexture(0x3540, ...)` for type
  0x47 entities writes 256x256 CT32 there, 0x3540-0x393F.
- **What this means:**
  - For interpolation, the orb changes only by its level.
  - In standalone mode its texture is a constant keyed by its source.
  - STEP3.md's "uploaded as a new texture each frame" is right that it is uploaded every frame, and wrong
    that the texture is new.
- **Not covered:** `herb.pad` casts no magic. Casting changes only the MP level, which is the same
  register.

**How often the mutators run, and the hash's cost.** These are meshes parsed per frame over the last
100 frames (the runs above).

- **Re-parses come only from the wave UVs.** A block is re-parsed when its bytes change. On 58 of the 64
  maps that happened 0 times a frame.
- **On six maps, every block of every visible record with flags 0x100 is re-parsed every frame**
  (`Wobble_ApplyWaveUvs` from `func_00112BD0`):

  | map | wave records | blocks re-parsed a frame |
  |---|---|---|
  | 027 | 2 | 30 |
  | 029 | 1 | 38 |
  | 034 | 1 | 3 |
  | 039 | 6 | 78 |
  | 042 | 8 | 201 |
  | 114 | 1 | 26 |

- **What that costs.** A parse takes 1.8-2.5 us a block, so 042 spends about 0.37 ms a frame parsing.
  Each re-parsed block also goes to the GPU again: 33,626 meshes were put on the GPU in 042's run,
  against 1,015 in 000's.
- **New meshes as the view moves are a separate, smaller cost.** 025 averaged 8.6 a frame, with one
  frame of 634.
- **The hash:**
  - each frame hashes 154-2,017 KB (668 blocks and 1,353 KB on map 000);
  - in the game that took 27-308 us a frame (the clock reads included);
  - a stand-alone timing of `hash_bytes` gives 0.135-0.149 ns a byte, or about 190 us for 1.35 MB;
  - that is under 1% of a 33 ms game frame. It runs once a game frame, not once a picture.
- **The other mutators:**
  - `Model_SetPrimBits` and `Model_SetAlphaBlend` on map records run once, in `Map_BuildModels` at load.
  - Items' `Model_SetTextureBase` and `Model_SetVertexColour` run when an item appears; they caused 0
    re-parses.
  - `Model_FadeVertexColoursNear` (map triggers: map objects), `Model_SetUvsFromScreen` /
    `Save/RestoreGsWordsAndUvs` (type 0x47 entities) and the item effects' setters write kinds that are
    not ported yet. P2 and P4 should measure them again with this dump.
- **The decision: `rd_mesh_touch` is not needed.** Keep `RD_GEN_HASH`, for three reasons:
  - the hash is cheap and catches every writer;
  - the only writer that matters in play rewrites every block of its records every frame, so a touch
    would save nothing (the blocks change and must be parsed again);
  - a touch scheme would save at most the hash (about 0.2 ms a frame), at the price of knowing every
    writer.
- **What would help instead.** The wave records' per-frame re-parse and re-upload goes away if P6 does
  the wave UVs on the GPU: the base UVs and wave index per vertex, and the 16 oscillators per draw. The
  mesh then stays cached and only the oscillators change.

### P2-6's measurements: the mutators, with map objects native

**How.** The same `KF4_ND_DUMP` (with `tools/nd_measure.py`'s reading of it), on all 64 maps. Each map was
run with P1-2's 8 made-up views (two places, four headings, a load every 100 vblanks, then 300 vblanks
more), and `--native-draw map,items,objects,picked` against `map,items`. A temporary probe build (not
committed) added a note to the dump at every call of `Model_SetPrimBits`, `SetTexture`, `SetAlphaBlend`,
`SetVertexColour`, `SetTextureBase`, `SetBlockGsWords`, `SetUvsFromScreen`, `Save/RestoreGsWordsAndUvs`
and `FadeVertexColoursNear`. It also noted the fading objects' start and signal, and could signal them or
fade any object on demand. Timings are noisy: up to 8 agents shared the PC.

- **The mutators run at load.**
  - Every call of `Set*` came in the 2 or 3 frames after a view's load: the map's records and objects
    being set up, then the items appearing. That is 4 to 21 frames of about 530 a run.
  - They cause no re-parse in play.
  - `SetBlockGsWords`, `SetUvsFromScreen` and `Save/RestoreGsWordsAndUvs` never ran. They belong to the
    type 0x47 entities, which are skinned and are not native yet.
  - When P2-2/P2-3 make those entities native: with `MODEL_FLAG_800`, their blocks are rewritten before
    every draw (UVs from the screen position), so they are re-parsed whenever the model or the camera
    moves. A touch would save nothing there either.
- **`Model_FadeVertexColoursNear`: the fading objects.** These are trigger kind 31, on 039 and 040 only.
  They fade only when a switch signals them.
  - Signalled by the probe, the hash re-parsed only the blocks the growing radius reached. On 040 that was
    2,044 re-parses in 208 frames of fading, about 10 of the model's 45 blocks a frame. Each re-parse is
    about 2 us.
  - These objects sit above the cave ceilings at every place tried, so they were checked by the probe's
    fade below instead.
- **Writers that are not mutators.** On 015 and 101, object blocks (8 and 6) changed at a view's load with
  no mutator call, and the hash re-parsed them. A touch scheme has to know every writer, the loads included.
- **The hash's cost with objects native** (the last 300 frames of each run):
  - Hashing took 1,094-4,426 KB a frame, in 329 us on average and 747 us at worst (107). With `map,items`
    it was 220 us.
  - Objects added 629 KB and 111 us a frame on average, and 2,667 KB and 465 us at worst (116).
  - The cause was the check pass: `NativeDraw_PosedWithAlpha` hashed every block once in its check
    ("every part parses") and again for the draws.
  - It now keeps the check's meshes, up to 8,192 blocks a model; past that, parts are looked up again.
  - Objects' hashing then fell to 339 KB and 39 us a frame on average, and 1,346 KB and 222 us at worst.
    The draws and the re-parses were the same on all 64 maps.
  - `native_draw.c`'s `posed_model` (map records, items, the picked item) has the same double hash.
- **The decision: still no `rd_mesh_touch`.** These are P0-3's three reasons, now with objects:
  - the hash catches the load's writes, which no mutator reports;
  - a mutated block has to be parsed again with or without a touch;
  - what the hash cost was mostly the second pass, now gone for objects.
- **Vertex fades, checked on screen (013).** The probe faded the colours of every object drawn through
  `Model_DrawPosedWithAlpha` in the update (`MapObject_UpdateAll`, where the real fades run), down to
  0x10 and back by 6 a frame, with a `Packet_WaitForSend()` before its writes (see the next point).
  - Native against the PS2 path: 0 pixels in compare 1x, compare 4x and onoff 1x, at all 8 views.
  - Native with the fade against native without it: 86,747 pixels (worst 67) at 220, 974 at 420 and 98
    at 820. So the test sees the fade.
  - With a fixed gen in place of the hash, the native pictures go stale at those places (87,874 pixels,
    worst 67).
- **The PS2 path races the next frame's update.**
  - `Packet_SendChain` starts VIF1's DMA and returns. The port's DMA thread (`port/platform/dmac.c`) then
    reads the chain, and with it the model blocks the chain REFs.
  - Meanwhile the game runs the next frame's update: `func_00112BD0`'s wave UVs, then
    `MapObject_UpdateAll`'s fades.
  - So a block mutated in the update can reach VU1 already one step on: the PS2 path draws frame N with
    some of frame N+1's colours or UVs. The PS2 had the same race; the port keeps it.
  - The native draw parses the block at the draw call, so it draws what the game drew that frame.
  - Shown with the fade probe in the update but without the wait: compare differs by 86,840 pixels (one
    fade step, worst 24). With the wait, 0.
  - **This is the wave maps' difference.** A probe build waited for the DMA (`Packet_WaitForSend()`, under
    an environment variable) just before `func_00112BD0` in `GameLoop_Run`'s update. It ran compare 1x with
    `map,items,objects,picked`, 8 sweep views a map.
    - 027: 2,478 pixels without the wait, 0 with it.
    - 034: 3, then 0.
    - 039: 141,243 with `objects,picked`, 0 with it. That was P2-1's run-to-run map, which differs with no
      native draws at all.
    - 040: 145,556, then 0.
    - 042: 4,374, then 0.
    - 114: 1,234, then 0.
    - 029: 0 either way.
    - Onoff on 039 with the wait: 1 pixel, worst 1.
  - So P1-2's "wave-UV" differences are the PS2 path's race, not the native draw.
  - A test-only wait like this one (a switch that makes `Packet_SendChain` wait for its DMA, or a wait
    before the update) would let the sweep demand 0 pixels on those maps too. The game itself should
    keep the race: it is how the PS2 drew.
