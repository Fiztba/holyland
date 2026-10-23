# Native draw: what is built, and the tasks next

Branch `native/draw` (long-lived; main stays safe). Read [NATIVE_DRAW.md](NATIVE_DRAW.md) first: the survey,
the assumptions A1-A20 and the plan P0-P9. This page is the state of the work and the task list for the
agents doing the rest of P0 and P1. October 2026.

## What P0's core is

| Path | What |
|---|---|
| `port/include/port/render.h` | the renderer core's C API: meshes from blocks, cameras, draws, the chain's marks, stats |
| `port/render/render.cpp`, `render_internal.hpp` | the core: the VIF block parser, the mesh cache, the side buffer (a ring of 128 frames) |
| `game/native_draw/native_draw.c` | the adapter: game structs to draws; queues the marks; `NativeDraw_Posed`, `NativeDraw_PosedDepthByPart`, `NativeDraw_BeginFrame` |
| `port/gs/native/gsn_native_draw.inc` | the native GS's side: `PORT_GS_REG_NATIVE_DRAW`, the CPU plan, the arena and draw records, the ALT marks |
| `port/gs/native/shaders/nd_000.vert` | VU1 program 0x000 as a vertex shader, in VU1's arithmetic; pairs with every fragment shader of the native GS |
| `tools/native_draw_compare.py`, `tests/herb.pad` | the same-game comparison, and a pad script that walks up to an item |

The hooks in `gsn.cpp` are kept to a few lines, each marked "native draw": the include, the register
case `0x7B` and the ALT skip at the top of `gs_write`, `NdFlush` at the top of `flush_batch` and five
one-line conditions in it (`nd.active`), `nd_kick()` at the top of `vertex_kick`, `Cmd::set_storage_buffer`
(`OP_STORAGE`) and `gsn_set_native_draw`.

### How a draw goes

1. **Game thread, at the call site.** The patched site calls the adapter instead of the `Model_Draw*`
   function. The adapter parses each data block the part would have sent (`rd_mesh_from_block`: the
   block's VIF codes run on a model of VU1 memory; each MSCAL/MSCNT is a batch: its GIF tag, the A+D packet
   at TOP+0xA0, its vertices as VU1 memory has them, its triangles with the data's ADC already left out).
   A block is cached by its EE address and the hash of its bytes, so a block the game rewrote is parsed
   again without anyone being told (`RD_GEN_HASH`; `rd_mesh_touch` and a caller's gen are there for when
   the writers are known). If any block of a model cannot be parsed (another VU1 program, a block that is
   not self-contained), the adapter returns 0 and the site draws the PS2 way: a model goes one way or the
   other, whole.
2. It runs `ModelMatrix_BuildDrawMatrices` (as the PS2 path does), takes the camera
   (`world_screen_matrix`, `world_view_matrix`, `view_screen_matrix`), fills an `RdDraw` per part (VU1's
   fixed block by value), `rd_draw` copies it into this frame's slot and returns a handle, and the adapter
   queues in the chain, per part: the GS registers the PS2 path queues itself (ZBUF, TEST for
   `DepthByPart`) and `PORT_GS_REG_NATIVE_DRAW` = the handle.
3. **The native GS's thread**, meeting the write in order: for each batch of each mesh it writes what the
   VU1 run would have kicked (FOGCOL at an MSCAL, the A+D registers, PRIM) through `gs_write`, so the GS
   state and its flushes are the PS2 path's. Then it plans the batch's triangles on the CPU in VU1's
   arithmetic (`vu_float.h`: positions, the guard-band clip that is VU1's ADC, Z, alpha, ST) and appends
   them to `gs->verts` as the PS2 path's corners would be. They stay in the batch until a register
   changes or another kind of draw comes, as on the PS2 path, and `flush_batch` decides everything from
   them as it always has (targets, texture rows, blending, the alpha test's passes, Z and layer rules,
   runs). Only the draw call differs: the program is the GS's own fragment shader with `nd_000.vert`, and
   each corner says which arena vertex, which kick (flat shading) and which draw record it is.
4. **`nd_000.vert`** does program 0x000 per corner: the vertex matrix and lights as VU1's set-up leaves
   them (computed on the CPU, `vu_float.h`), then per vertex the transform, 1/w, fog, the three lights
   and ambient, the point lights, ST, FTOI4, all rounded toward zero in the program's order. It writes
   exactly what `gs.vert` would have been given: the GS's integer Z, so the layer rules see no difference.
5. **Pictures between** (`--frame-rate`, the GPU replay): the ND draws are met again when the frame's
   stream is drawn for a picture; their records then carry `ND_C_MOVING`, and the shader moves the place
   by the picture's camera (`Picture.move`, as `gs_replay.comp` does), from the part matrix, the view and
   the projection.

### The switches

- `--native-draw off|on|KINDS` (`port_options.native_draw`, `port_native_draw(kind)`): `on` is
  `PORT_ND_ACCEPTED`, the kinds that have passed their checks (items); others by name (`map` is in work).
  The overlay's Debug page, "Native draw (experimental)", has a checkbox per ported kind and the compare
  mode, live. Never saved. Needs the native GS: off with `--renderer gs`.
- `--native-draw-compare native|ps2`: the adapter queues the PS2 path's packets too, after the native
  draws, between `RD_ND_ALT_BEGIN`/`END`; the native GS draws one set (`gsn_set_native_draw`), skipping the
  other's register writes (counting their vertex kicks for the pictures between). VU1 runs the packets
  either way, so two runs differing only in this switch play the same game. With `--renderer both`,
  paraLLEl-GS ignores the marks and draws the PS2 path: a live A/B against the reference.
- `--native-draw off` is today's game byte for byte: the same `--log`, PATH1 hash, VU1 cycles and pictures
  as main 91c890e on `tests/herb.pad` (checked); `--native-draw items` gives the same `--log` too (the game
  does not notice that VU1 no longer runs for the items: 18,016 fewer VU1 runs).

### The numbers (P0's proof)

`tools/native_draw_compare.py` (compare mode, items, `tests/herb.pad`, the herb lying near the start of
map 099, walked up to and looked down at, pictures every 5 vblanks from 1800 to 1840):

| | pixels differing | by more than 1 | worst |
|---|---|---|---|
| 1x, 30 | 0 | 0 | 0 |
| 4x, 30 | 0 | 0 | 0 |
| 1x, 240 (every 3rd vblank 1800-1839) | the game's frames 0; pictures between 290-1,070 each, edges of the herb only (4,109 in all) | 2,027 | 51 |
| 4x, 240 (the same) | the game's frames 0; pictures between 65,997 in all, the herb's edges | 32,825 | 110 |
| `--mode onoff` (no VU1 for the items) | 0 | 0 | 0 |

The pictures between differ because the PS2 path's are made by moving its packets' vertices
(`frame_interp.c`'s camera move on screen positions) and the native draw's by the camera on the part's
geometry: the herb's edges, a pixel. Before the shader did VU1's arithmetic (plain GPU floats) the 1x
frames differed by 34-341 edge pixels each.

Map records (`--native-draw map,items`, the first measurement for P1): every block of maps 000, 004, 009,
013, 021, 035, 112, 116 and 099 parses (no refusals); pictures at vblanks 1900 and 2000 after
`--teleport 1600:N:0`: 0 pixels on all of them but 000, which differs by about 2,050 a picture, 1 or 2
levels, in one band of the floor (P1-1 below). (Before the native corners joined the GS's batch as the
PS2 path's do, maps 004 and 021 differed by 2 pixels.) The map tour (`--native-draw off`, fresh
captures): 415,016 pixels, worst 106, all scanouts identical, the same per map as main's kf4-gsreplay on
the same captures (the 414,901 of RENDERER.md is older captures').

## How to work on native draw

- **Build:** `cmake --preset windows-clang` (with `-DSDL2_DIR=...` as your machine needs), `cmake --build
  out/windows-clang`. Shaders: edit `port/gs/native/shaders/`, then `python tools/build_shaders.py --glslang
  PATH` and commit `gsn_shaders.h` (check the diff touches only your shader's array).
- **Game code** only through `tools/patches.py apply` / `save` (RULES.md). A call site is one condition:
  `if (!port_native_draw(PORT_ND_X) || !NativeDraw_Foo(model, kind, owner)) { original call; }` with a
  `/* PC: --native-draw (game/native_draw/) */` comment and a prototype. Everything else goes in
  `game/native_draw/` (edited in place). One writer per `patches/game/<folder>.patch` at a time.
- **Every run:** `--no-audio` and a fresh `KF4_DATA_DIR`; headless where you can. `--headless` draws
  nothing unless `--dump-frames` asks for pictures (no native GS: no native draws).
- **Compare:** `python tools/native_draw_compare.py --out DIR --disc ISO [--kinds K] [--scale 4]
  [--frame-rate 240] [--frames A-B/STEP] [--input PAD] [--crop X0,Y0,X1,Y1] [-- --teleport 1600:MAP:0]`.
  Read `DIR/report.txt`, the heat maps (`heat_*.png`: red = difference x 8) and crops. Both runs' `--stats`
  lines say how many draws were queued, drawn, lost, and meshes parsed or refused (`KF4_ND_LOG=1` prints
  each refused block's reason).
- **The off check**, before anything goes in: `tools/boot_test.py`, `tools/frame_test.py` (gs and
  native), `gsn_draw_test`, `vu1_test`, and a herb run with `--log` and `--stats` against main's build
  (same log, PATH1 hash, VU1 cycles, pictures).
- **Thresholds:** the game's own frames of a kind that has passed: 0 pixels (we have it bit for bit; a
  difference is a bug, not rounding). Pictures between: edges of moving things only, no blobs. A kind in
  work: regions explained in its notes.
- **Rules:** copy no code from paraLLEl-GS or PCSX2; no decomp struct renaming (Andrew's); keep
  `gsn.cpp` edits to hooks (another branch, `perf`, works in it); commit as the owner with the Claude
  co-author line; do not push.
- **Gotchas found so far:**
  - a VU1 batch tag's "w" is its fourth word (`tag[1] >> 32`), not bits 48-63 of the first;
  - `--teleport` needs `VBLANK:MAP:PLACE` when a vblank is given;
  - headless pictures between are never drawn from the recording (the scroll target on map 099 is "not as
    recorded" every picture, in main too): `OP_STORAGE`'s replay is not yet exercised (P0-4);
  - the GPU's `1/x` is not correctly rounded and fused multiply-adds change results: the shader's
    `vu_*` helpers and `precise` are what make it exact; keep new maths in them.

## The tasks

Each task: what, the files it owns (one writer at a time), and how it is accepted. Sizes are rough.

### P0, what is left

**P0-1 Capture support** (owns `port/debug/gs_capture.c`, `tools/gsreplay/`, `port/include/port/gs_capture_format.h`;
the record types in render.h if needed). A `.kf4gs` capture of a run with native draws has the chain with
`0x7B` writes but not the side buffer, so `kf4-gsreplay` loses the draws (`nd_lost`). Add a record type
per frame: the slot's draws, cameras and the meshes they name (or their blocks' bytes, re-parsed on
replay); `kf4-gsreplay` feeds them back through a `rd_*` replay entry before the frame's GIF. Accepted:
a capture taken with `--native-draw items --native-draw-compare native` replays with `--compare-renderers`
(native GS drawing ND, paraLLEl-GS drawing the ALT set) and the items show; the map tour's captures
(off) unchanged; old captures still load.

**P0-2 Vertex oracle** (owns `tests/nd_oracle_test.c` or a `kf4-gsreplay --nd-oracle` mode; may add a
compute or transform-feedback path to `gsn_native_draw.inc` behind a debug switch). For batches from
captures (or the herb run): `nd_000.vert`'s outputs (X, Y, Z, RGBA, S, T, Q, F) against `vu1_hle.c`'s
XGKICK for the same VU memory. Accepted: 0 differences over the map tour's 0x000 batches, including point
lights and fog; a report of any lane that differs and why.

**P0-3 Measurements** (two agents; write into NATIVE_DRAW.md's open questions, no code in the game):
- the sky's radius and distance per sky map (000, 002, 004, 013, 021, 027, 098, 099): from the parsed
  meshes (a debug dump of `RdMeshData` bounds for records with flags bit 31) and the player's feet;
- the layer meshes per map (how many, how they pair: records whose blocks have vertex alpha below 0x80
  and GEQUAL over another), from the map tour with `--native-draw map`;
- the HUD orb's texture: re-made each frame or sent unchanged (`ui/hud.c`, the GS's uploads);
- how often the mutators run (`Model_Set*`, `Wobble_ApplyWaveUvs`, `Model_FadeVertexColoursNear`): count
  `rd_stats.meshes_parsed` per frame on water and lava maps (`RD_GEN_HASH` re-parses a block each frame
  its bytes change) and the hash's cost (`hash_bytes`); decide whether `rd_mesh_touch` is needed.

**P0-4 Replays of the recording** (owns nothing new; a test). Find a map where pictures between are drawn
from the recording (the windowed run's `--stats`: "from the recording" above 0) with items visible, and
check the native draws there at 240 (windowed, `KF4_PRESENT_SIM` does not replay). Accepted: no
re-recording caused by native draws, the items in place in a screenshot of a replayed picture.

**P0-5 Review** (an independent agent): read render.h, the adapter and `gsn_native_draw.inc` against
NATIVE_DRAW.md 3.2-3.3 and this page; run the proof; list what is wrong or missing.

### P1, map records (in this order where it says so)

The call sites are in already (`map.patch`: `Map_DrawRecords`, `Map_DrawLateRecords` through
`NativeDraw_Posed`, kind `map`). What P1 has to do is make that exact everywhere and give its pictures
between. `patches/game/map.patch` has one writer at a time: P1-4, then P1-5.

**P1-1 The band on map 000** (owns `gsn_native_draw.inc`). Map 000's start view (`--teleport 1600:0:0`,
vblank 2000) differs in a band of the floor by 1-2 levels (about 2,050 pixels; it is not the layer rules:
`--native-layer-z ps2` leaves it). Likely a batch decision that differs (Vulkan blending against shader
blending, from the batch's alpha range or overlap runs) because the batch is not exactly the PS2 path's,
or the CPU near cut's pieces (`ModelScreen_CutClippedTriangles`, program 0x280 on the PS2 path) meeting
native triangles. Find which draw (`KF4_GSN_LOG=2` prints batches), fix. Accepted: 0 pixels on map 000's
start at 1x and 4x.

**P1-2 The map sweep** (no code; then fixes go to P1-1's owner). `native_draw_compare.py --kinds map,items`
on all 64 maps (the map tour's places: `--quick-load` a made-up save from `tools/map_tour.py`'s
`write_made_up`, or `--teleport`), 1x and 4x. Accepted: a table per map; every difference explained or
fixed; refusals (`KF4_ND_LOG=1`) listed with their reason.

**P1-3 Pictures between for the world** (owns `gsn_native_draw.inc`'s moving path and `nd_000.vert`'s).
Today a moving record goes by the camera only, in plain floats; records that follow the player (the sky,
flags bit 31) and the mirror's camera are not handled. Pair with `frame_interp.c`'s camera (the sky:
`FrameInterp_MarkSky` today moves it as one); check `KF4_INTERP_STILL` (the picture equals the next
frame) and the pixel step per picture on the sky walk. Accepted: at 240, no seams or jumps between native
records and the PS2-path draws around them on maps 000, 009, 099; `KF4_INTERP_STILL` exact.

**P1-4 The mirror modes and the reflection** (owns `map.patch` then; `game/native_draw/`). Modes 1 and 2
(maps 008 and 009) call the same functions with the mirrored camera (`rd_camera`'s mode and projection
are 0 today: fill them from `GameLoop_SetCameraMatrices`' arguments, e.g. a hook in
`FrameInterp_MarkCamera`), the reflection's target is CT16S at 0x2BC0. Accepted: maps 008 and 009 at 1x
and 4x, 0 pixels in the reflection and the water; the winding flip does not matter (no culling, A7).

**P1-5 Late records and the near cut** (owns `map.patch` then). Late records (flag 0x1, Z write by 0x80)
are already native; check their order and Z against the PS2 path on lava and glass. The CPU near cut
(`ModelScreen_CutClippedTriangles`) stays the PS2 path (it draws on program 0x280); decide with the owner
whether `--clip gpu` (real clipping instead of dropping, A5) is wanted, and if so add it behind its
switch. Accepted: maps 000 (lava), 021, 112 at 1x/4x 0 pixels with `--clip ps2`.

**P1-6 Performance** (owns nothing; a measurement). `--stats` on maps 000 and 009, 2,000 vblanks
`--fast --headless --dump-frames 1999`, `--native-draw off` against `map,items`: VU1's time, the GS's GIF
and flush time, the hash's MB a frame, the arena's size. The CPU plan costs about what VU1 HLE's position
maths did; if it shows, plan only what `flush_batch` needs (boxes and alpha) and leave the rest to the GPU.

**P1-7 Review and the owner's gate** (independent agent): the screenshot set (maps 000, 004, 009, 013, 021,
035, 112, 116 at 1x, 4x, widescreen) side by side, and the switch to accept `map` into `PORT_ND_ACCEPTED`.

## What I would change in the plan

- **Hosted mode plans on the CPU.** NATIVE_DRAW.md 3.3 has the vertex shader alone make the triangles.
  `flush_batch` decides too much from the vertices (texture rows, overlap runs for the layers and shader
  blending, the scissor, the rows written, ties) to do without them, and those decisions are what make the
  native GS right. So the native GS also computes the places on the CPU in VU1's arithmetic, and the GPU
  does the vertex program. It costs about VU1 HLE's position maths; in exchange `flush_batch` is used
  unchanged and the picture is bit for bit. Standalone mode (P8) drops the plan.
- **Exact, not "within thresholds".** With VU1's rounding in the shader, the native draws are the PS2
  path's pixel for pixel. The thresholds in 4 should be 0 for the game's own frames of an accepted kind;
  differences are then bugs to find, which is much easier than judging regions.
- **Batches as the PS2 path's.** Draws must join the GS's batch as the packets would; the API keeps a
  draw per part, but the GS side merges. Any future kind (skinned, effects) must keep that.
- **The shaders live with the native GS** (`port/gs/native/shaders/nd_*.vert`) while hosted: they are
  tied to `gs_common.h`'s push constants and the fragment shaders' inputs. `port/render/shaders/` is for
  standalone.
- **The side buffer is a ring of 128 frames with a consumer mark** (`RD_ND_FRAME`), not double-buffered:
  the GS's ring holds some 50 frames of play, and the pictures between draw a frame again after it.
- **Map records went faster than planned**: every block of the maps tried parses, and the frames match
  but for one band. P1 is mostly checking, the mirror and the pictures between; its 4-5 weeks could be
  2-3.
