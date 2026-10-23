# Native draw: what is built, and the tasks next

Branch `native/draw` (long-lived; main stays safe). Read [NATIVE_DRAW.md](NATIVE_DRAW.md) first: the survey,
the assumptions A1-A20 and the plan P0-P9. This page is the state of the work and the task list for the
agents doing the rest of P0 and P1 and then P2 and P3. October 2026.

## Where it stands (2026-10-23, after the first workflow)

P0-1 to P0-4 and P1-1 to P1-6 are done and merged into `native/draw` (each task's branch `nd/<task>` with a
merge commit; the per-task notes are under each task below). Left: **P0-5** (review), **P1-7** (review and
the owner's gate), and the follow-ups under "What is left of P1". Next: **P2** (models, skinned, the arm)
and **P3** (the sky), at the end of the task list.

The merged tree, `--native-draw map,items` against the PS2 path, pixels differing (timings noisy: up to 8
agents shared the PC):

| | compare 1x | compare 4x | onoff 1x |
|---|---|---|---|
| 000 start (`newgame.pad`, `--teleport 1600:0:0`, vblanks 1900, 2000) | 0 | 0 | 0 |
| 009 start (the same, `1600:9:0`) | 0 | 0 | 0 |
| 099 herb (`herb.pad`, 1800-1840/10) | 0 | 0 | 0 (the opening at 1000 and 1400 too) |
| 099 start (`--teleport 1600:99:0`, 1900, 2000) | 0 | | |
| 000 at 240, every vblank 1990-2000 (the game's frames) | 0 | | |
| 000 at 240 walking (P1-3's `walk000.pad`), 32 pictures 1990-2004 | the game's frames 0; pictures between 613 in all, at most 2, all in the sky (P1-3's figure) | | |
| P0-4's artefact views at 240 (items; 020 place 1 and 004 place 2, turning) | 0 in all 69 pictures of each (were up to 830,000 before P1-3) | | |

The off check passes on the merged tree: boot_test, frame_test (gs and native), gsn_draw_test (0 of 300),
vu1_test (0 of 90), and a herb run with `--native-draw off` against main's build 91c890e: the same `--log`,
PATH1 hash 204e443a9fc49731, VU1 cycles 460424273, and byte-identical pictures at 1800, 1820 and 1840. The
oracle on the merged tree (tour capture 000): 0 lane differences on drawn vertices.

## What P0's core is

| Path | What |
|---|---|
| `port/include/port/render.h` | the renderer core's C API: meshes from blocks, cameras, draws, the chain's marks, stats |
| `port/render/render.cpp`, `render_internal.hpp` | the core: the VIF block parser, the mesh cache, the side buffer (a ring of 128 frames) |
| `game/native_draw/native_draw.c` | the adapter: game structs to draws; queues the marks; `NativeDraw_Posed`, `NativeDraw_PosedDepthByPart`, `NativeDraw_BeginFrame` |
| `port/gs/native/gsn_native_draw.inc` | the native GS's side: `PORT_GS_REG_NATIVE_DRAW`, the CPU plan, the arena and draw records, the ALT marks |
| `port/gs/native/shaders/nd_000.vert` | VU1 program 0x000 as a vertex shader, in VU1's arithmetic; pairs with every fragment shader of the native GS |
| `tools/native_draw_compare.py`, `tests/herb.pad` | the same-game comparison, and a pad script that walks up to an item |
| `port/debug/gs_capture.c`, `gs_capture_format.h` (ND_MESH, ND_DRAWS) | captures hold the side buffer's draws and meshes (P0-1); `kf4-gsreplay --native-draw native\|ps2` feeds them back (`rd_replay_record`) |
| `tools/gsreplay/nd_oracle.c`, `port/gs/native/gsn_nd_oracle.inc`, `shaders/nd_oracle.frag` | the vertex oracle, `kf4-gsreplay CAPTURE --nd-oracle [--vu1 interp]` (P0-2): `nd_000.vert` (built again with `-DND_ORACLE`) and the CPU plan against VU1's XGKICK, lane by lane |
| `KF4_ND_DUMP=FILE`, `tools/nd_measure.py` | a debug dump of what the core is handed, and the script that measures the sky, layers and re-parses from it (P0-3) |
| `docs/port/ND_SWEEP.md` | the 64-map sweep (P1-2): a table per map, every difference with its cause |

The hooks in `gsn.cpp` are kept to a few lines, each marked "native draw": the include, the register
case `0x7B` and the ALT skip at the top of `gs_write`, `NdFlush` at the top of `flush_batch` and five
one-line conditions in it (`nd.active`), `nd_kick()` at the top of `vertex_kick`, `Cmd::set_storage_buffer`
(`OP_STORAGE`), `gsn_set_native_draw`, and `nd_mark(value)` in the `0x7D` case (the frame-rate marks: which
section a draw is in, for the pictures between).

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
   stream is drawn for a picture. Draws in the world and the reflection sections (`nd_mark`, from the
   frame-rate marks) carry `ND_C_MOVING`, and `nd_000.vert` moves them as `gs_replay.comp`'s `moved()`
   moves the PS2 path's vertices: from VU1's exact X, Y, Z and the place in view taken from them, by the
   picture's `(R - I)v + T`, rounded the same, with the same drop rule (ADC) for each corner. A part whose
   translation moved since the frame before by less than 400 (the sky follows the feet) gets `ND_C_OBJ`
   and is moved t of the way with its model, as `frame_interp.c`'s `object_between` (P1-3).

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
  - pictures between are drawn from the recording (`OP_STORAGE`) only windowed, and never on maps 000, 013,
    099 and 116: a target the recording names (bp 0x3a80 on 099) no longer exists at the next picture, in
    main and with native draw off too (P0-4). Map 002 replays; native items were in place there;
  - **compare mode hides bugs that depend on VU1's state**: VU1 runs the PS2 packets in both runs. Run
    `--mode onoff` too (what a player gets): the near cut's lighting bug (P1-5) showed only there;
  - "0 refused" does not mean every record went native: a draw the adapter declines (another `draw_parts`,
    or before P1-5 a part over 64 blocks) falls back to the PS2 path uncounted;
  - map 000's band needs `--input tests/newgame.pad --frames 1900,2000 -- --teleport 1600:0:0` (herb.pad
    did not show it); the mirror maps are 008, 009, 014 (reflection, flag 0x200) and 041 (the only mode-1
    mirror, flag 0x2), and their water is out of the teleport's view: use made-up saves placed by the water
    record's box (`tools/map_tour.write_made_up`);
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

*Done (nd/p0-1, b87de3f).* Records `KF4GS_ND_MESH` (21: the raw block, parsed again on replay) and
`KF4GS_ND_DRAWS` (22: a frame's cameras and draws, in a file layout of their own); old readers skip them, the
version stays 1. `rd_capture_poll` runs before each GIF and VIFDATA record. `kf4-gsreplay --native-draw
native|ps2` picks the set the native GS draws. Herb capture: the native replay of the ND set equals the ALT
set's and an off capture's (0 pixels, 1x and 4x); the tour's 64 captures replay unchanged (415,016, worst
106). Note: `--compare-renderers` resyncs by default, which overwrites the native draws: judge them with
`--no-resync`. Each `RdMeshData` now keeps its block's bytes (30-50% more mesh memory).

**P0-2 Vertex oracle** (owns `tests/nd_oracle_test.c` or a `kf4-gsreplay --nd-oracle` mode; may add a
compute or transform-feedback path to `gsn_native_draw.inc` behind a debug switch). For batches from
captures (or the herb run): `nd_000.vert`'s outputs (X, Y, Z, RGBA, S, T, Q, F) against `vu1_hle.c`'s
XGKICK for the same VU memory. Accepted: 0 differences over the map tour's 0x000 batches, including point
lights and fog; a report of any lane that differs and why.

*Done (nd/p0-2, d79e05e).* `kf4-gsreplay CAPTURE --nd-oracle [--vu1 interp]`. Over the 64 tour captures
(86,404 batches, 1,488,158 vertices, 5,651 MSCALs): 0 differences in any lane of a drawn vertex, against
the HLE and the interpreter; the set-up matches in every MSCAL. Vertices never drawn differ where VU1's
arithmetic saturates and the shader goes to inf/NaN (13,832, all undrawn): the `vu_*` helpers and `ftoi`
need VU1's saturation before anything draws such vertices (`--clip gpu`). `nd_plan_vertex` and `nd_const`
are split out of `nd_plan` and `nd_draw` so the oracle checks the real code. Not covered: the flat-shading
choice and the parser's triangle lists (the pixel comparisons cover those).

**P0-3 Measurements** (two agents; write into NATIVE_DRAW.md's open questions, no code in the game):
- the sky's radius and distance per sky map (000, 002, 004, 013, 021, 027, 098, 099): from the parsed
  meshes (a debug dump of `RdMeshData` bounds for records with flags bit 31) and the player's feet;
- the layer meshes per map (how many, how they pair: records whose blocks have vertex alpha below 0x80
  and GEQUAL over another), from the map tour with `--native-draw map`;
- the HUD orb's texture: re-made each frame or sent unchanged (`ui/hud.c`, the GS's uploads);
- how often the mutators run (`Model_Set*`, `Wobble_ApplyWaveUvs`, `Model_FadeVertexColoursNear`): count
  `rd_stats.meshes_parsed` per frame on water and lava maps (`RD_GEN_HASH` re-parses a block each frame
  its bytes change) and the hash's cost (`hash_bytes`); decide whether `rd_mesh_touch` is needed.

*Done (nd/p0-3, 3bd8d3f).* The answers are in NATIVE_DRAW.md, "P0-3's measurements". In short: every sky is
a dome on the feet (rim 24,252, top 4,666 above; 013 smaller; 021 and 027 deeper), its nearest point 4,446
from the eye, and on 002, 004, 013 and 027 some geometry lies behind the dome, so A1's "sky at the far
plane" would show it. Layer meshes on 61 of 64 maps, 99.3% exactly coplanar over an earlier opaque
triangle, nearly always of another record. The HUD orb's texture is sent unchanged every frame (only the
fill level changes). Only the wave UVs (flag 0x100, 6 maps) re-parse, up to 201 blocks a frame on 042;
the hash is cheap (about 0.14 ns a byte): `rd_mesh_touch` is not needed and `RD_GEN_HASH` stays; the fix
for the wave cost is wave UVs on the GPU (P6). The debug dump: `KF4_ND_DUMP=FILE`, `tools/nd_measure.py`.

**P0-4 Replays of the recording** (owns nothing new; a test). Find a map where pictures between are drawn
from the recording (the windowed run's `--stats`: "from the recording" above 0) with items visible, and
check the native draws there at 240 (windowed, `KF4_PRESENT_SIM` does not replay). Accepted: no
re-recording caused by native draws, the items in place in a screenshot of a replayed picture.

*Done (a test; nothing committed).* Pictures between replay from the recording windowed on maps 002, 004,
009, 012, 020, 021, 027, 035 and 112, never on 000, 013, 099 and 116 (a GS issue, off too: a target the
recording names is gone by the next picture; not native draw's). Map 002, a potion bottle while turning:
items and off both replay 596 pictures, 0 re-recorded; the native bottle is in place in the replayed
pictures (edges only, as the herb). Along the way it found large artefacts in native items' pictures
between (020 place 1, 004 places 2-5); on the merged tree (P1-3's drop rule) those views are 0 pixels.

**P0-5 Review** (an independent agent; **not done**): read render.h, the adapter and `gsn_native_draw.inc`
against NATIVE_DRAW.md 3.2-3.3 and this page; run the proof; list what is wrong or missing. Now also the
capture records, the oracle, the moving path (`nd_mark`, `ND_C_OBJ`) and P1-5's fixed-block packet.

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

*Done (nd/p1-1, 21fb36a).* VU1 leaves a vertex's alpha as min(alpha, 255) through FTOI0 and the GS takes
the low 8 bits, so a negative alpha wraps (-1400 is 0x88). `nd_plan` clamped it at 0, so a layer batch's As
range was 0-128 instead of 0-218 and `flush_batch` chose Vulkan's blending where the PS2 path chose the
shader's. Map 000's start: 0 pixels at 1x and 4x (was 2,054). Left: a native batch can split where the PS2
path's does not (a PS2-path kick flushes pending native corners: one draw cannot hold both kinds); 0
pixels today, but a mixed batch whose PS2 part had alpha above 0x80 would decide its blending differently.

**P1-2 The map sweep** (no code; then fixes go to P1-1's owner). `native_draw_compare.py --kinds map,items`
on all 64 maps (the map tour's places: `--quick-load` a made-up save from `tools/map_tour.py`'s
`write_made_up`, or `--teleport`), 1x and 4x. Accepted: a table per map; every difference explained or
fixed; refusals (`KF4_ND_LOG=1`) listed with their reason.

*Done (nd/p1-2, c2285c7): [ND_SWEEP.md](ND_SWEEP.md).* 8 views a map (made-up quick saves), compare 1x and
4x and onoff 1x: 0 refusals, 0 lost; 56 of 64 maps 0 pixels in compare mode. Measured before P1-1, P1-3
and P1-5 were in: map 000's band and the onoff near-cut lighting (016, 022, 027, 037, 099, 102, 106) are
fixed since. Still open: maps 039 and 040 are a game frame apart (the native draw reads a wave-UV block
when the game calls it, VU1 reads it a frame later on the DMA thread: match the late read or keep the
native one, the owner's call); sparse noise in wave-UV water near the camera on 027, 042, 114 and 034; one
scanline on 030 at 4x. **The sweep should be run again on the merged tree, in both modes.**

**P1-3 Pictures between for the world** (owns `gsn_native_draw.inc`'s moving path and `nd_000.vert`'s).
Today a moving record goes by the camera only, in plain floats; records that follow the player (the sky,
flags bit 31) and the mirror's camera are not handled. Pair with `frame_interp.c`'s camera (the sky:
`FrameInterp_MarkSky` today moves it as one); check `KF4_INTERP_STILL` (the picture equals the next
frame) and the pixel step per picture on the sky walk. Accepted: at 240, no seams or jumps between native
records and the PS2-path draws around them on maps 000, 009, 099; `KF4_INTERP_STILL` exact.

*Done (nd/p1-1, 4ef0b50).* `nd_000.vert`'s moving path does what `gs_replay.comp`'s `moved()` does (see "How
a draw goes", 5); the sections come from the frame-rate marks (`nd_mark`, a hook in gsn.cpp's `0x7D`
case); `NdConst` grew to 35 qwords (`obj`). At 240: map 000 walking, 613 pixels over 24 pictures between
(was 1,295,167), all 1-2 levels in the sky; 009 0; 099 turning 0, walking 478-647 in the sky; herb 0;
`KF4_INTERP_STILL` 0 for native draws on 000 and 099; the sky steps as the PS2 path's does (0.01 px
between pictures). The sky's remaining speckle: the PS2 path moves its sky by the median of its runs,
native by the record's own move. Left: with the CPU walk (`KF4_INTERP_CPU`, `--renderer both`) native
draws are not moved; the mirror's pictures between are untested.

**P1-4 The mirror modes and the reflection** (owns `map.patch` then; `game/native_draw/`). Modes 1 and 2
(maps 008 and 009) call the same functions with the mirrored camera (`rd_camera`'s mode and projection
are 0 today: fill them from `GameLoop_SetCameraMatrices`' arguments, e.g. a hook in
`FrameInterp_MarkCamera`), the reflection's target is CT16S at 0x2BC0. Accepted: maps 008 and 009 at 1x
and 4x, 0 pixels in the reflection and the water; the winding flip does not matter (no culling, A7).

*Done (nd/p1-4, 23d78c6).* The draws were already right in both modes; `RdCamera.mode` and `.projection` are
now filled (`NativeDraw_MarkCamera`, a hook in `FrameInterp_MarkCamera`); `map.patch` unchanged. Made-up
saves looking at the water: 009 (lamps and an enemy reflected), 008 (the drip pool), 014 and 041, at 1x
and 4x: 0 pixels. The mirror maps are 008, 009, 014 (reflection) and 041 (mode 1). Nothing reads `mode`
or `projection` yet.

**P1-5 Late records and the near cut** (owns `map.patch` then). Late records (flag 0x1, Z write by 0x80)
are already native; check their order and Z against the PS2 path on lava and glass. The CPU near cut
(`ModelScreen_CutClippedTriangles`) stays the PS2 path (it draws on program 0x280); decide with the owner
whether `--clip gpu` (real clipping instead of dropping, A5) is wanted, and if so add it behind its
switch. Accepted: maps 000 (lava), 021, 112 at 1x/4x 0 pixels with `--clip ps2`.

*Done (nd/p1-4, b27274b).* Two bugs in `native_draw.c`. Parts over 64 blocks (`ND_MAX_MESHES`) silently went
the PS2 way (the lava and the big floors): the limit is now 2048 (`data_count`'s 11 bits). And the near
cut's pieces (`ModelScreen_CutClippedTriangles`, still the PS2 path) read the lights, fog and point lights
that a native record no longer left in VU1 memory (a bright wall on 021, 260,513 pixels, onoff only):
outside compare mode the adapter now queues `Model_DrawPosed`'s fixed-block packet without the parts. 000
lava, 021 and 112: 0 pixels at 1x and 4x (compare) and in onoff. Hashing roughly doubles on 000 now that
the big records are native. `--clip gpu` is **not** added; the design is in the task's report (a switch,
skip the near cut for native records, clip in clip space in the CPU plan, CPU-made vertices in
`nd_000.vert`; it draws what the PS2 dropped, so it needs its own review): the owner decides.

**P1-6 Performance** (owns nothing; a measurement). `--stats` on maps 000 and 009, 2,000 vblanks
`--fast --headless --dump-frames 1999`, `--native-draw off` against `map,items`: VU1's time, the GS's GIF
and flush time, the hash's MB a frame, the arena's size. The CPU plan costs about what VU1 HLE's position
maths did; if it shows, plan only what `flush_batch` needs (boxes and alpha) and leave the rest to the GPU.

*Done (nd/p1-6, cb9b0ad). Measured 2026-10-23 (native/draw 4d35c35, 1x, `--fast --headless`, `tests/newgame.pad`, `--teleport
1600:N:0`, dump at the last vblank; 3 runs a case, median [min-max]). Up to 8 other agents shared the CPU
and GPU, so the timings are noisy; the counts were the same in every run. Each run's data folder was fresh
but seeded with a warm pipeline cache: without one, 40-65 s of a run were pipeline compiles, more than
everything measured here. Timers for the plan, the hash and the arena were a local patch, not committed
(`nd_plan`, `nd_draw`, `rd_mesh_from_block`, `nd_place`).*

**First: `--native-draw map` without `--native-draw-compare` draws garbage.** Every check of `map` so far
used compare mode, where VU1 still runs the map's packets. Without compare mode, VU1 does not run them,
and the PS2-path draws that come after go wrong. The opening (vblanks about 700-1600, before control) is
black. Maps 000 and 009 at vblank 2000 and 4000 have a band of noise across rows 190-290, and the HUD and
the scene are smeared (about 210,000 pixels differ). With compare mode on, the same frames match `off`
except for P1-1's band. My guess is that VU1 or VIF1 state set by the map's packets is used by later
programs, but I have not checked it. It also costs time: the GS keeps rebuilding the frame buffers at
640x448, where `off` uses 640x224. That is 1,566 write-backs and 1,520-1,554 texture decodes (about 350M
texels), against 5 and 135-177 with `off`. All of them happen before the teleport, and they take 40-47 s
of a 2,000-vblank run. Needs fixing before `map` goes into `PORT_ND_ACCEPTED`; P1-2's sweep should also
run with compare mode off.

The task's own runs (2,000 vblanks, the whole run: the boot, the opening, then about 400 vblanks on the map):

| | wall s | GS GIF s | VU1 s (HLE) | write-backs / decodes |
|---|---|---|---|---|
| 000 off | 3.97 [3.95-5.07] | 2.95 | 1.23 (1.19) | 5 / 135 |
| 000 map,items | 48.89 [46.68-51.09] | 47.67 (decodes 41.8) | 0.72 (0.68) | 1,566 / 1,520 |
| 000 map,items, compare | 5.45 (1 run) | | | 5 / 136 |
| 009 off | 3.75 [3.02-4.10] | 2.47 | 1.07 (1.04) | 5 / 170 |
| 009 map,items | 46.60 [43.39-49.80] | 45.48 (decodes 40.1) | 0.55 (0.51) | 1,566 / 1,554 |

The opening hides the map's cost here. To measure the map alone, each 4,000-vblank run minus the same
case's 2,000-vblank run gives 2,000 vblanks standing on the map (paired by run; one picture a vblank).
Compare mode runs VU1 for the map as well, so its VU1 line is `off`'s, but its GS thread does the same work
as the real mode would without the bug:

| 2,000 vblanks on the map | wall s | GS GIF s | `nd_draw` s (plan) | VU1 HLE s | hand-off s |
|---|---|---|---|---|---|
| 000 off | 12.66 [11.05-14.92] | 11.72 [10.29-13.47] | | 5.17 [4.92-5.83] | 4.79 |
| 000 map,items, compare | 15.46 [13.74-16.56] | 14.56 [13.45-15.38] | 3.96 (2.89 [2.76-3.07]) | 5.10 | 7.90 |
| 000 map,items | 22.37 [16.01-23.87] | 20.61 [14.89-21.83] | 4.72 (2.70 [2.62-3.29]) | 3.66 [3.54-4.34] | 16.95 |
| 009 off | 8.25 [8.21-8.69] | 7.19 [6.89-7.22] | | 4.17 [4.17-4.29] | 1.62 |
| 009 map,items, compare | 12.28 [9.75-14.30] | 11.38 [9.68-11.58] | 3.96 (3.53 [3.17-3.55]) | 4.50 | 5.07 |
| 009 map,items | 13.70 [12.26-22.45] | 12.35 [11.40-21.12] | 3.89 (3.32 [3.32-3.63]) | 2.56 [2.55-2.68] | 9.13 |

- **The CPU plan shows.** It is 2.7-3.5 s per 2,000 vblanks, which is 1.35-1.75 ms a vblank or 2.7-3.5 ms
  per 30 fps game frame, on the GS thread. That is 20.0M vertices on 000 and 23.8M on 009, about 145 ns a
  vertex. The VU1 HLE time it saves is only 1.5-1.6 s per 2,000 vblanks (000: 5.17 to 3.66 s; 009: 4.17
  to 2.56 s), so the plan costs about 1.8-2.1 times the VU1 maths it replaces. It runs every `vu_float`
  operation in software. `nd_draw` outside the plan (set-up 0.07 s per run, the register writes and the
  flushes it causes) is a further 1.1 s on 000 and 0.4-0.6 s on 009. Compare mode's GS thread goes from
  35% to 44% of real time on 000 (33.3 s is 2,000 vblanks), and from 22% to 34% on 009. That is still
  far below the limit, but it is the largest new cost, and it sits on the thread that already sets the
  pace (the hand-off waits grow). It is worth doing what the plan above says: plan only what
  `flush_batch` needs, or plan with native floats where nothing depends on the exact bits (the boxes,
  rows and alpha ranges), and keep VU1's arithmetic in `nd_000.vert`.
- **The GS's other work** barely changes in compare mode. Working out state goes 0.99 to 0.98 s (000) and
  0.98 to 1.09 s (009); recording goes 5.94 to 6.03 s (000) and 0.51 to 0.60 s (009). The GS draws
  4.11M on 000 in both modes, and 166,661 off against 179,661 compare on 009.
- **The hash**: 1,417 MB per 2,000 vblanks on 000 and 1,710 MB on 009. That is 0.71 and 0.86 MB a vblank
  (1.4 and 1.7 MB per game frame). It takes 0.24 and 0.30 s per 2,000 vblanks (0.12-0.15 ms a vblank,
  about 5.9 GB/s). The whole block lookup, hash included, is 0.39-0.50 s. Parsing is 3-4 ms per run (1,255
  meshes on 000 and 1,091 on 009, none refused, about 2.0M and 2.5M found again). Cheap; it is on
  the game thread.
- **The arena**: one arena per run, never grown: 524,288 vertices of room (32 MB). The whole run used
  29,005 vertices (1.8 MB) on 000 and 23,899 (1.5 MB) on 009, the opening's map 099 included; that is
  1,047 and 883 meshes put on the GPU. The 32 MB first size is far more than needed: 4 MB would hold these
  maps. Draw records are 544 bytes each, 62,641 and 66,124 in a 4,000-vblank run (about 16 a vblank,
  8.5 KB).
- **GPU**: not measured (no GPU timers in `--stats` headless). The GS's render passes per picture are the
  same in compare mode as `off`.

*Note after the merge:* P1-6 measured on 4d35c35, before P1-5's fixed-block packet. On the merged tree
`--mode onoff` (no compare) gives 0 pixels on 000 and 009 at 1900 and 2000 and on the opening at 1000 and
1400, so the "draws garbage" above looks fixed by P1-5; the opening's frame-buffer thrash and the timings
should be measured again.

**P1-7 Review and the owner's gate** (independent agent; **not done**): the screenshot set (maps 000, 004,
009, 013, 021, 035, 112, 116, and the mirror maps 008, 014, 041 from made-up saves, at 1x, 4x, widescreen)
side by side, compare **and** onoff, and the switch to accept `map` into `PORT_ND_ACCEPTED`.

### What is left of P1 (follow-ups)

- **The sweep again** (P1-2's method, no code) on the merged tree, compare and onoff, 1x and 4x; and P1-6's
  timings again (the opening's thrash should be gone).
- **The CPU plan's cost** (`gsn_native_draw.inc`): about twice the VU1 HLE time it saves (145 ns a vertex,
  2.7-3.5 s per 2,000 vblanks on the GS thread). Plan only what `flush_batch` needs (boxes, rows, the
  alpha range) in native floats where no decision depends on the exact bits; keep VU1's arithmetic in
  `nd_000.vert`. Check with the oracle and the sweep.
- **Mixed batches** (`gsn_native_draw.inc` and `flush_batch`, beyond a hook): decide once for a batch of
  native and PS2-path corners and draw it in two calls, instead of splitting it (P1-1's note).
- **Wave UVs**: the 039/040 frame lag (the owner's call: the PS2 path's late read or the native
  frame-consistent one), the noise in wave-UV water on 027, 042, 114 and 034, and the 030 scanline at 4x
  (the oracle or a corner-order check tells the two candidate causes apart).
- **Counting fallbacks**: a stats counter for draws `NativeDraw_Posed` declines (returns 0), so that "0
  refused" means every record went native.
- **The arena's first size**: 32 MB, about 20 times what these maps use (4 MB holds them).
- **The CPU walk** (`KF4_INTERP_CPU`, `--renderer both`): native draws are not moved in pictures between;
  it needs the camera move handed to the native GS outside the GPU recording (`frame_interp.c`, `gsn.cpp`).
- **Docs**: tools/README.md does not yet list `kf4-gsreplay --native-draw` and `--nd-oracle`.
- **Not native draw's**: no replays from the recording on 000, 013, 099 and 116 (a target gone by the next
  picture, P0-4); the PS2 path's own `KF4_INTERP_STILL` is 52-87 pixels off while turning on 099 (P1-3).

### P2, models: posed, skinned, then the arm

The plan is NATIVE_DRAW.md 4, P2. The same rules as P1: a call site is one condition, the rest in
`game/native_draw/`; draws join the GS's batch as the PS2 path's would; the game's own frames 0 pixels in
compare **and** onoff mode; pictures between, edges only. `patches/game/model.patch` (model.c holds all
the draw functions) and `entity.patch` have one writer at a time.

**P2-1 Posed models: map objects, gold, the picked item** (owns `model.patch` then, `game/native_draw/`).
`NativeDraw_Posed` and `NativeDraw_PosedDepthByPart` already draw items and map records. Add the call sites
for `MapObject_DrawAll` (`Model_DrawPosedWithAlpha`: context 2, `ALPHA_2` with the fade as FIX),
`Gold_Draw` and the picked item after its depth clear, as kinds `objects`, `gold` and `picked`. Accepted:
the sweep's views with those kinds, 0 pixels in compare and onoff on the maps with map objects, a fading
object caught mid-fade, and a gold drop.

**P2-2 Program 0x280 as a shader** (owns `port/gs/native/shaders/nd_280.vert`, its part of
`gsn_native_draw.inc`, and the parser's 0x280 batches in `render.cpp`). Skinned models: float vertices,
two matrices picked by ST.w, the colour scale and bias (0x64-0x65), and the second (sphere, "phong") pass
gated by TOP+0xA4.z, blended `Cs + Cd*As`; VU1's arithmetic as `vu1_hle_280.c` does it, in `vu_*`
helpers. Extend the oracle (P0-2) to 0x280, both passes. Accepted: 0 lane differences on the tour's 0x280
batches.

*Done (nd2/p2-2).* `shaders/nd_280.vert`; the GS side in `gsn_nd_280.inc` (included at the end of
`gsn_native_draw.inc`; hooks in `NdFlush`, `nd_program`, `nd_draw`); the parser's `take_batch_280`
(`RD_VF_FLOAT_SKIN`: a vertex is eight qwords in the mesh, VU1's four and (k, the point lights' set, the
w ORed into XYZF2, n)); `RdDraw.turn` (VU1 0x24, 0x28: the sphere pass's matrices; a capture carries them
after a 0x280 draw's mesh ids, so `Kf4GsNdDraw` and old captures are unchanged). What 0x280 does beyond
0x000 (all in `vu1_hle_280.c`): the RGB of `u` carries from vertex to vertex (only vertex 0's colour is
read for it); the last vertex's normal w is not ORed into XYZF2; the run ends after the A+D kick, with no
PRIM, strip or second pass, when VU1's count of clipped vertices (vertex 0 counted twice) reaches n; the
point lights take vertex 0's set from the tag's w and the falloff from set A; the second pass is white
with vertex 1's alpha. The oracle (`kf4-gsreplay --nd-oracle`, `tools/gsreplay/nd_oracle_280.c`) over the
64 tour captures: 19,528 runs of 0x280 (2,913 MSCALs; 6,739 with a second pass, 4,666 with point lights,
10,742 ending with every vertex clipped), 103,474 first-pass and 52,924 second-pass vertices drawn: 0
lane differences, against the HLE and the interpreter; the kicks (which packets each run sends) and both
sets' set-up the same in every run; and the parser on the game's own blocks (each VIF1 DMA transfer)
equal to every run's batch (19,528 of 19,528). Left: 63 vertices never drawn differ (VU1's 1 / 0, the
largest float, against the GPU's NaN), and a tag with an odd w (every vertex ADC when vertex 0 is
clipped) is in the plan but never seen in the tour. A throwaway call site in `Model_DrawSkinned` (not
committed) drew the arm's swing on map 000 with its second pass: 0 pixels in compare mode at 1x and 4x and
in onoff mode (leaving the second pass out: up to 38,822 pixels). A batch of 0x280 corners is never one
of 0x000's (their own records buffer and program): where the PS2 path's batch holds both, the GS splits it
("Mixed batches"). Pictures between are not handled for 0x280 yet (P2-5).

**P2-3 Entities** (owns `entity.patch` then; after P2-2). `Entity_DrawAll`'s opaque and translucent draws
(the draw list's order kept: the draws go out in the game's order), the tint (hit flash, ailments), hidden
parts and fades, and the Fire Lord's afterimages. Type 0x47 (the frame copied to 0x3540 and mapped by
screen UVs) stays the PS2 path's at first. Kind `entities`. Accepted: maps with enemies (016's bats, 009's
skeleton) 0 pixels in compare and onoff, with a hit flash and a death fade caught.

**P2-4 The arm, the held weapon and the trail** (owns `held_weapon.c`'s call sites through its patch;
after P2-2). The depth clear first (`GsDraw_ClearDepth`), then the arm and the weapon on 0x280 with
`held_item_light_blocks`, weapon 0x2E's additive part 5, and the trail as a native strip (additive, Z
ALWAYS). Kind `arm`. Accepted: 0 pixels while swinging, at 1x and 4x, every weapon class.

**P2-5 Pictures between for models** (owns the moving path for the kinds `objects`, `entities` and `arm`).
Pair draws by identity (kind and id, as P1-3 does) and move the parts by their own matrices t of the way,
then remove `objects_pair` for native draws. Accepted: at 240, the arm's swing and 016's bats with no
jumps, edges only against the PS2 path; `KF4_INTERP_STILL` exact.

**P2-6 The mutators** (owns the call sites of `Model_Set*`, `Model_FadeVertexColoursNear`,
`Model_SetUvsFromScreen` and `Model_Save/RestoreGsWordsAndUvs`). Measure them with `KF4_ND_DUMP` as P0-3
did for the map (it could not: those kinds were not native yet), and wire `rd_mesh_touch` only where the
hash's cost shows.

**P2-7 Review** (independent agent): P2's screenshot set side by side, compare and onoff, and the switch to
accept each kind.

### P3, the sky (can run beside P2)

**P3-1 `--sky ps2`** (owns the sky's records in `game/native_draw/` and its switch). The sky records (flags
bit 31) are already native as map records, with their Z and fog; make the sky a kind of its own (`sky`)
so it can be switched, and check it on all 8 sky maps (000, 002, 004, 013, 021, 027, 098, 099) at 1x, 4x
and 240 (walking, turning, strafing, looking up and down, from made-up saves). Compare
`FrameInterp_MarkSky`'s sky with the native one (the 1-2 level speckle P1-3 measured). Accepted: the game's
frames 0 pixels; pictures between only that speckle, explained.

**P3-2 `--sky modern`** (owns its shader path; the owner decides A1 first). Centred on the eye, depth at
the far plane, drawn first. P0-3 found geometry behind the dome from the eye on 002, 004, 013 and 027: list
what appears when the dome goes to the far plane, with pictures, before anything is made the default. The
scroll targets stay the GS's (hosted). Accepted: the owner's look at the 8 maps and the draw-distance
settings.

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
