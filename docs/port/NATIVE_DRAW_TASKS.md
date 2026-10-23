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

*Done (nd/p1-6, cb9b0ad); measured again on the merged tree (nd2/b-sweep, native/draw 95d27ec, 2026-10-23).*
Setup: 1x, `--fast --headless --no-audio --memcard none`, `tests/newgame.pad`, `--teleport 1600:N:0`,
`--stats`, a dump at the last vblank. 3 runs a case, interleaved, given as median [min-max]. Each run's data
folder was fresh but seeded with a warm pipeline cache, one per mode, built by a 4,000-vblank run on each
map; the measured runs did not grow it. Up to 8 other agents shared the CPU and GPU, so the timings are
noisy. A second set of 3 runs, taken while one other game of this task's was running, agrees to within
about 30%. The counts were the same in every run. There were no timers for the
plan, the hash and the arena this time; P1-6's were a local patch.

**The garbage and the thrash P1-6 found are gone.** P1-6 measured before P1-5's fixed-block packet. There,
`map` without compare mode drew a black opening and noise. The GS rebuilt the frame buffers at 640x448:
1,566 write-backs and 1,520-1,554 texture decodes, 40-47 s of a 2,000-vblank run. On the merged tree
`map,items` has the same 5 write-backs as `off`, and 137 decodes against 135 on 000 (170 and 170 on 009). A
2,000-vblank run costs about what `off` does. The B-sweep's onoff runs (ND_SWEEP.md) show that the
pictures are right.

The whole run (2,000 vblanks: the boot, the opening, then about 400 vblanks on the map):

| | wall s | GS GIF s | VU1 s (HLE) | write-backs / decodes | MB hashed |
|---|---|---|---|---|---|
| 000 off | 3.66 [2.90-3.81] | 2.60 [2.20-2.85] | 1.14 (1.10) | 5 / 135 | |
| 000 map,items | 4.33 [3.96-5.77] | 3.56 [3.29-4.56] | 0.55 (0.51) | 5 / 137 | 680 |
| 000 map,items, compare | 4.39 [4.06-5.96] | 3.57 [3.41-4.87] | 1.09 (1.05) | 5 / 137 | 680 |
| 009 off | 3.50 [3.14-4.06] | 2.43 [2.20-2.90] | 1.07 (1.04) | 5 / 170 | |
| 009 map,items | 4.97 [4.28-5.54] | 3.91 [3.31-3.93] | 0.59 (0.54) | 5 / 170 | 581 |
| 009 map,items, compare | 4.71 [4.39-5.46] | 3.66 [3.46-4.25] | 1.09 (1.04) | 5 / 170 | 581 |

(P1-6 on 4d35c35: 000 `map,items` 48.89 s, 009 46.60 s.)

The map alone: each 4,000-vblank run minus the same case's 2,000-vblank run, paired by run. That gives
2,000 vblanks standing on the map (33.3 s of real time), with one picture a vblank:

| 2,000 vblanks on the map | wall s | GS GIF s | VU1 s | VU1 HLE s | hand-off s | MB hashed |
|---|---|---|---|---|---|---|
| 000 off | 8.89 [8.74-11.68] | 8.15 [8.11-10.95] | 4.33 | 4.26 [4.24-5.34] | 3.09 | |
| 000 map,items | 14.30 [12.28-14.69] | 13.54 [11.69-13.70] | 2.62 | 2.53 [2.32-2.63] | 10.41 | 2,323 |
| 000 map,items, compare | 17.31 [16.77-23.41] | 16.02 [15.75-16.88] | 5.11 | 4.97 [4.88-5.04] | 9.48 | 2,323 |
| 009 off | 7.94 [7.36-8.62] | 6.56 [6.14-6.79] | 4.09 | 3.99 [3.85-4.16] | 1.71 | |
| 009 map,items | 10.54 [7.33-11.34] | 9.89 [7.50-10.70] | 2.61 | 2.49 [2.06-2.67] | 6.44 | 1,710 |
| 009 map,items, compare | 8.46 [7.85-12.78] | 8.23 [7.45-11.82] | 3.98 | 3.85 [3.59-4.56] | 3.21 | 1,710 |

- **The real mode (no compare) can now be measured, and the CPU plan is still the cost.** On the map,
  `map,items` adds 5.4 s of GS thread time per 2,000 vblanks on 000 and 3.3 s on 009, about 2.7 and 1.7 ms
  a vblank. It saves 1.7 s and 1.5 s of VU1 HLE. The GS thread goes from 24% to 41% of real time on 000,
  and from 20% to 30% on 009. The game thread waits longer for it: the hand-off is 3.1 to 10.4 s on 000
  and 1.7 to 6.4 s on 009. Wall time on the map is 1.6 times `off`'s on 000 and 1.3 times on 009. P1-6's
  plan figure (2.7-3.5 s of `nd_draw` per 2,000 vblanks, about 145 ns a vertex) fits this; it was not timed
  again. So the follow-up stands: plan only what `flush_batch` needs, in native floats where nothing
  depends on the exact bits, and keep VU1's arithmetic in `nd_000.vert`.
- **Compare mode** costs a little more than the real mode on 000, because VU1 runs the map's packets too.
  On 009 its numbers spread too much to rank it against the real mode.
- **The hash** is 2,323 MB per 2,000 vblanks on 000 (1.16 MB a vblank) and 1,710 MB on 009 (0.86 MB). On
  000 that is 1.6 times P1-6's 1,417 MB, because P1-5 raised `ND_MAX_MESHES` to 2,048 and the big records
  (the lava, the large floors) are native now. At P1-6's 5.9 GB/s it is about 0.4 s and 0.3 s per 2,000
  vblanks, on the game thread.
- **The arena** and the draw records were not measured again; see P1-6's notes (32 MB first size, about 20
  times what these maps use). **GPU** time was not measured either: `--stats` has no GPU timers headless.

**P1-7 Review and the owner's gate** (independent agent; **not done**): the screenshot set (maps 000, 004,
009, 013, 021, 035, 112, 116, and the mirror maps 008, 014, 041 from made-up saves, at 1x, 4x, widescreen)
side by side, compare **and** onoff, and the switch to accept `map` into `PORT_ND_ACCEPTED`.

### What is left of P1 (follow-ups)

- **The sweep again** (P1-2's method, no code) on the merged tree, compare and onoff, 1x and 4x; and P1-6's
  timings again (the opening's thrash should be gone).
- ~~**The CPU plan's cost**~~ (*done, nd2/a-exact, below*): about twice the VU1 HLE time it saved (145 ns a
  vertex, 2.7-3.5 s per 2,000 vblanks on the GS thread).
- ~~**Mixed batches**~~ (*done, nd2/a-exact, below*): decide once for a batch of native and PS2-path corners
  and draw it in a call a kind, instead of splitting it (P1-1's note). It was the review's blocker.
- **Wave UVs**: the 039/040 frame lag (the owner's call: the PS2 path's late read or the native
  frame-consistent one), the noise in wave-UV water on 027, 042, 114 and 034, and the 030 scanline at 4x
  (the oracle or a corner-order check tells the two candidate causes apart).
- ~~**Counting fallbacks**~~ (*done, nd2/a-exact*): `--stats`' native draw line ends "N models declined
  (drawn the PS2 way)" (`rd_stats.declined`: a `NativeDraw_*` call that returned 0; `KF4_ND_LOG=1` names the
  first 32 and why). 0 on 000, 009, 099 and the mirror maps' views, the opening included. (The gotcha above,
  "0 refused does not mean every record went native", is answered by it.)
- ~~**The drop rule of pictures between**~~ (*done, nd2/a-exact*): `nd_000.vert` dropped a triangle when its
  corner's own vertex was dropped as well as for the kick window; it now tests the window only (kick and the
  two before it), as `gs_replay_corners.comp`. Only a fan's first vertex is outside the window, and program
  0x000's data has no fans: the tour's 94,593 batches of 0x000 are all strips (so nothing changed: 000
  walking at 240, compare and onoff, is P1-3's 613 pixels in the sky, picture for picture).
- **The arena's first size**: 32 MB, about 20 times what these maps use (4 MB holds them).
- **The CPU walk** (`KF4_INTERP_CPU`, `--renderer both`): native draws are not moved in pictures between;
  it needs the camera move handed to the native GS outside the GPU recording (`frame_interp.c`, `gsn.cpp`).
- **Docs**: tools/README.md does not yet list `kf4-gsreplay --native-draw` and `--nd-oracle`.
- **Not native draw's**: no replays from the recording on 000, 013, 099 and 116 (a target gone by the next
  picture, P0-4); the PS2 path's own `KF4_INTERP_STILL` is 52-87 pixels off while turning on 099 (P1-3).

*A-exact (nd2/a-exact, 2026-10-23): map records exact in onoff mode, and the plan's cost.*

- **The blocker: map 009 at 2x-4x in onoff mode** (one pixel at 4x, (1072,49) from vblank 1696 on, 1-2
  levels, a seam of the ceiling). Not VU1 state: a batch split. `nd_kick` flushed the pending native corners
  at the PS2 path's next vertex kick, so a native record's corners and the PS2-path triangles after it
  (the near cut's pieces, other records) were two batches where the PS2 path's was one, and `flush_batch`
  decided the runs and blending of each part on its own: a pixel of a seam where two triangles of the
  ceiling meet came out a level apart. Compare mode hid it (`RD_ND_ALT_BEGIN` flushes in both of its runs),
  and so did a capture's replay (it starts from VRAM at 1x). Found with a local log of every batch
  (size, state, a hash of the corners) in an off and an onoff run: 3 batches of 009's frame were split
  (135 = 129 native + 6, 810 = 60 + 750, 867 = 60 + 807); the GS's own stats showed it too (onoff made 61
  more draws and 368 more shader blends than off by vblank 1500, all in the opening).
- **The fix (mixed batches)**: in a frame a batch now holds both kinds, as the PS2 path's would, and is
  decided once over all of it; `nd_draw_range` (the call in `flush_batch`'s pass loop) draws each stretch
  of one kind with its own vertex shader, in order. A native corner's fog is -1 less its arena vertex
  (`nd_corner_vertex`: below 0, where the PS2 path's are 0-255), so the corners carry their kind through
  the runs' reordering. In a picture between the native corners are still drawn as a batch of their own
  (the PS2 path's corners then come from `gs_replay_corners.comp`, from a buffer of their own). The
  hooks in `gsn.cpp`: the program is no longer swapped for the whole batch, and `c.draw` in the pass loop
  is `nd_draw_range`. Now off and onoff give the same GS stats (draws, shader blends, render passes, 466,517
  / 8,361 on 009 by 2,000 vblanks at 4x).
- **`--native-draw map` without compare** (P1-6's garbage and the opening's thrash): gone on the merged
  tree. The opening at 1000 and 1400 and 000 and 009 at 1900 and 2000 are 0 pixels in onoff mode at 1x and
  4x, and the runs make the same write-backs (9 in 1,707 vblanks) and render passes as `off`.
- **The plan's cost.** Its decisions need the exact bits (P1-1's alpha range, the runs, the layers' Z; a
  planned corner a 1/16 pixel off changes which triangles overlap), so it stays VU1's arithmetic, made
  cheaper. `nd_plan_vertex` works in doubles: a product of two 24-bit significands is exact, a sum is exact
  or rounded below the 24 bits kept, each result truncated to 24 bits (VU1's rounding toward zero); what a
  double would do otherwise (a sum that lost a term, a result VU1 makes +-0 or its largest, FTOI's limit,
  denormal or exponent-255 matrix entries) falls back to `vu_float.h` (`nd_plan_vertex_vu`). 3.2M random
  and awkward vertices: 0 differ; 140 to 27 ns a vertex. And `nd.planned` keeps each batch's last two plans
  (the world's camera and the reflection's) by mesh, batch, vertex matrix, XYOFFSET and IIP: a camera
  standing still, or a frame drawn again for a picture between, takes the corners planned already. `--stats`
  prints the plan's time and how many vertices were planned and found planned.
- **Numbers** (4,000 vblanks, 1x, `--fast --headless`, `tests/newgame.pad`, `--teleport 1600:N:0`, standing on
  the map from about vblank 1,700; 3 runs, two at a time, other agents on the PC: noisy):

  | | wall s | GS GIF s | the plan s |
  |---|---|---|---|
  | 000 off (base 95d27ec / this) | 13.36-13.83 / 13.55-13.93 | 11.43-11.70 / 11.62-11.85 | |
  | 000 map,items (base / this) | 18.21-18.99 / 14.19-15.19 | 16.26-16.96 / 12.34-13.30 | about 5.9 / 0.74-0.84 |
  | 009 off (base / this) | 10.92-11.66 / 11.23-11.79 | 8.32-8.87 / 8.34-8.85 | |
  | 009 map,items (base / this) | 12.98-13.66 / 9.74-10.10 | 11.27-11.89 / 8.13-8.50 | about 4.6 / 0.56-0.58 |

  The base's plan time is a run of this branch before the plan's change (a timer in the same place). 009
  native is now as fast as `off` on the GS thread (and VU1 is spared the map); 000 is about 1 s of GIF over
  `off` per 4,000 vblanks, most of it no longer the plan. Standing, 84-94% of the vertices are found
  planned; walking, a frame's are planned anew (about 50 ns a vertex with the corners) and its pictures
  between find them.
- **Checks**: the oracle over 64 tour captures (one view a map, `--native-draw off`, this build): 94,593
  batches of 0x000, 6,120 MSCALs, 1,607,925 vertices, 0 lane differences on drawn vertices with `--vu1 hle`
  and `--vu1 interp`, and 0 in the plan's lanes on any vertex (drawn or not); the set-up 0. Pixels, all 0:
  000 start compare 1x and 4x, onoff 1x and 4x (the opening at 1000 and 1400 too); 009 the same; 099 start
  and herb, compare and onoff, 1x and 4x; the mirror maps from P1-4's made-up saves looking at the water
  (008 `m8w`, 009 `m9w`, 014 `m14c`, 041 `m41d`; 220-360), compare and onoff, 1x and 4x. 000 walking at
  240: 613 pixels in pictures between, in the sky, as before. The off check: boot_test, frame_test gs and
  native, gsn_draw_test (0 of 300), vu1_test (0 of 90), herb with `--native-draw off` against 95d27ec's
  build (the same log, PATH1 hash 204e443a9fc49731, VU1 cycles 460424273, pictures at 1800, 1820, 1840).

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

*Done (nd2/p2-3).* `game/native_draw/nd_entities.c`, `NativeDraw_Skinned`: `Model_DrawSkinned` natively
(the same turn and part matrices into the same scratchpad, the same parts left out, a 0x280 draw per part
with `model`, `parent`, `turn`, and the bias and scale at 0x64-0x65: the tint and the fade). Outside
compare mode it queues what the PS2 path leaves in VU1's memory without the data (the first packet, and
the last part's four matrices). Call sites (`entity.patch`): `Entity_DrawAll`'s opaque call, and the
entity model's `draw_parts` (set in `Entity_TryActivate`) for the draw list's translucent entities and
the Fire Lord's afterimages (copies of the model); type 0x47 goes the PS2 way. `--native-draw entities`
(an alias of `skinned`) and an overlay checkbox. Checked, 1x, compare and onoff, 0 pixels: 016's bats; 009's
miners and their reflection (a made-up save at the water); 008, 014 and 041's mirror views; maps 004,
012, 020, 027, 035, 042, 103 and 112 at their starts; and a fight on map 000 (a made-up save with the
player's hp and power raised, `strong3.pad`): three hit flashes (255, 64, 64) and the death fade (alpha 128
to 6.4 over 36 vblanks), at 1x and 4x. No model declined. **At 4x** two views differ by a few pixels
(009's water: 28, 016: 26; never at 1x): `flush_batch`'s `flat_2d` (the 2D snap at a larger scale) takes
some tiny sphere-pass triangles (Q 1, white, one Z: not "perspective") for rectangles and moves the PS2
path's corners; native corners are never moved (the shader works out its own places). With the snap off
both views are 0. It is the snap misfiring on 3D, not native draw's: the owner's call (keep 3D out of
`flat_2d`, or snap native corners too). Not caught: the Fire Lord's afterimages and type 0x47.

**P2-4 The arm, the held weapon and the trail** (owns `held_weapon.c`'s call sites through its patch;
after P2-2). The depth clear first (`GsDraw_ClearDepth`), then the arm and the weapon on 0x280 with
`held_item_light_blocks`, weapon 0x2E's additive part 5, and the trail as a native strip (additive, Z
ALWAYS). Kind `arm`. Accepted: 0 pixels while swinging, at 1x and 4x, every weapon class.

*Done (nd2/p2-4).* The adapter is `game/native_draw/nd_arm.c`; the call sites are five conditions in
`Player_DrawArm` (`player.patch`). `NativeDraw_Arm(model, which)` replaces `draw_parts` for the arm (0)
and the held item (1), weapon 0x2E's two passes included (the site's bit40 flags, `ALPHA_1 0x48` and the
textures stay the game's): a 0x280 draw per drawn part, carrying what `Model_DrawSkinned` would have put
in VU1 (`held_item_light_blocks` reach it through the model's light block). Without compare mode VU1
still gets `Model_DrawSkinned`'s fixed block and each part's matrices, only never the data blocks, so
VU1's memory is left as the PS2 path leaves it. The trail is `NativeDraw_ArmTrail`: the game's PATH2 packet
becomes a mesh of the new `RD_VF_GS_STRIP` (`render.cpp`, `parse_gs_strip`) and a draw of
`RD_PROGRAM_GS_STRIP` (kind arm, id 2), which the native GS writes register by register as `gif()` would
(`gsn_nd_strip.inc`). So the trail has an identity of its own for P2-5, but its points are still the
screen points `WeaponTrail_Update` projected. The depth clear (`GsDraw_ClearDepth`) is a GS sprite with no
VU1 in it, and it stays as it is.

Results (made-up quick saves at map 000's start, `PlayerState.weapon` set, SQUARE at vblank 330):
- 1x, compare and onoff mode, pictures every 2 vblanks 330-410: 0 pixels for 23 weapons covering every
  class. That is weapons 0, 1, 3, 5, 8, 11, 14, 19, 27, 31, 33, 36, 40, 42, 45, 46 (0x2E), 49, 52, 54 and
  55 (one-handed and two-handed swings), plus 57, 60 and 62 (the bows and the crossbow; these draw only with
  arrows, `--cheats items`, every 4th vblank 300-496).
- 4x, compare mode, every 4th vblank: 0 pixels for weapons 5, 31, 46, 49, 54 and the crossbow (60).
- 4x, onoff mode: 0 pixels for weapons 5 and 46.
- Control: with the native trail switched off, compare mode differed by 12,307 pixels on weapon 5 and
  15,957 on weapon 46, so the trail really is drawn by the native path.
- `--log` with `--native-draw arm` is the same as with `off`, in compare and onoff mode alike.
- A capture of the swing replays with the native set equal to the PS2 set (0 pixels), and the oracle
  finds 0 lane differences in it.

Left:
- Pictures between (P2-5): 0x280 draws stay where the frame has them, as the PS2 path's arm section does.
- The trail's world points are needed before it can move (`WeaponTrail_Update` keeps only screen
  points).
- `NativeDraw_Arm` has a copy of `Model_DrawSkinned` without the data blocks. P2-3's skinned adapter
  could share it.

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
