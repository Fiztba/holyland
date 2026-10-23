# Native draw: what is built, and the tasks next

Branch `native/draw` (long-lived; main stays safe). Read [NATIVE_DRAW.md](NATIVE_DRAW.md) first: the survey,
the assumptions A1-A20 and the plan P0-P9. This page is the state of the work and the task list for the
agents: P0-P7 so far (hosted mode), next P8. October 2026.

## Where it stands (2026-10-23, after merging perf/gpu-vram)

**The owner's decisions of 2026-10-23 (morning), on the review of the third workflow** (scratchpad nd3_review;
the owner's own numbering, which is not the list below):
1. **The 2D snap at 4x on the entities' sphere passes:** no visible difference to the owner (a spot in a
   creature's mouth, the tips of the bats' wings); native's picture is accepted. Native draws need not
   reproduce the PS2 path's `flat_2d` snap of 3D batches. *Applied (nd5/dec):* a batch holding native corners
   is left out of `flat_2d` (`nd_batch_3d`, gsn_native_draw.inc; a hook at both of flush_batch's calls), so a
   PS2-path corner sharing a batch with native ones is not snapped either; native corners never were (their
   RGB is their encoding). The PS2 path's own picture is unchanged (off, nothing is native). `entities` joins
   the accepted kinds with its 4x numbers as they are (000: 54, 016: 6, 009's water: 28, the far save: 9, the
   arm scene: 24, spells 3 and 66: 55 and 54; every other kind 0 at 4x).
2. **The wave UVs** (039's view is a rippling flesh cavern near the end of the game, not water): undecided, the
   owner will judge in motion. `--native-wave-uvs ps2` (the PS2 path's late picture) stays the default; `gpu`,
   `cpu` and `late` stay selectable.
3. **The sky:** a native/modern sky unless there is a solid reason not to (`--sky modern`, its own task; the
   default if it is clean). Until it lands, `sky` is accepted as the PS2's dome (0 pixels everywhere).
4. **The miners' idle sway at 240:** the new smooth movement is preferred: native's interpolation of models
   between frames (P2-5's slerp) stays the default.
5. **The spell bursts in the pictures between:** undecided, the owner will look in play (`effects` is in `on`
   so that it can be seen there).
6. **"Passes" at 240 with non-default switches** (under water at 4x; the view shift with `--no-whole-frame`):
   the heat map shows heavy corruption: must be fixed. Until then `passes` stays out of `on`.
7. **P8, standalone: start it** (the native renderer's own targets and modern depth, so that the Z tie-break
   bugs go away).

**`--native-draw on` is now every accepted kind** (nd5/dec; `PORT_ND_ACCEPTED`, port/include/port/host.h):
`map`, `sky`, `items`, `objects`, `picked`, `entities`, `arm`, `gold`, `effects`, `shadows`, `water`. Out of it:
`passes` (decision 6 above) and `reflection` (no call site). The evidence: the review's table (every kind 0 in
the game's frames, compare and onoff, at 1x, 4x and wide, but the entities' snap at 4x, now accepted), the
numbers below, and GPU time: `shadows` costs nothing since perf's whole silhouettes; `map` at 240 still costs
about 1.4 ms a picture (open, below). The whole path is one switch for the owner: the overlay's Debug page,
Renderer section, "Native draw (experimental)" (on = `PORT_ND_ACCEPTED`, off = the PS2 path; live), saved as
`settings.json`'s `display.native_draw` (`"on"`/`"off"`, default off; `--native-draw` on the command line wins
for the run, `native_draw_forced`; headless and pad-script runs read no settings, so tests are unchanged). The
Debug page's per-kind boxes stay (not saved), with "Accepted kinds (on)" and "None (off)" buttons.
Checked: m000 at 4x onoff (`newgame.pad`, 1900, 2000): 54 and 0, both runs byte for byte the merged tree's
(native-73887b9) with the same kinds; 000 walking at 240, 4x, MSAA 2, widescreen (`walk.pad`, 1912-1915, 32
pictures, game frames and pictures between, off and on): byte for byte the merged tree's. The off check passes
(below's list: boot_test, frame_test gs and native, gsn_draw_test 0 of 300 for seeds 1, 7 and 1234, vu1_test 0
of 90; herb with `--native-draw off` against play-932801f: the same `--log`, PATH1 hash 204e443a9fc49731, VU1
cycles 460424273, pictures 1800, 1820 and 1840 byte for byte).

**perf/gpu-vram merged** (9e9c2a12; the branch the owner plays: fix/underwater, fix/wall-blink, perf/hotspot's
ties drawn whole, replays over targets the recording made, silhouettes drawn whole, 16-bit reflection blends in
the pass, depth bounds on two-phase ties, and the write-backs put off). It is now the PS2-path reference: with
`--native-draw off` this tree is perf/gpu-vram's build (C:\code\kf4-builds\play-932801f) byte for byte. The
merge's conflicts were in gsn.cpp only (OpKind: `OP_STORAGE` and `OP_DEPTH_BOUNDS` both; the draw call: the
depth bounds set around `nd_draw_range`); `gsn_shaders.h` was made again (tools/build_shaders.py). Where the
two lines meet:
- **Native shadows drawn whole.** perf draws a silhouette (0x5A0: DATE, FBA, one colour, no texture, no Z
  written) whole instead of in runs when its corners are one colour; a native corner's RGB is its encoding,
  so P5's native shadows stayed in runs (about 3x the GPU at 240). `nd_corner_colour` (gsn_native_draw.inc)
  gives the colour a corner is drawn with where the CPU knows it: the PS2 path's own, and a native corner of
  a 0x5A0 record (RGB 0 exactly, the plan's alpha; the records flagged in `nd.of5a0`, cleared with the
  buffer). Native and PS2-path silhouettes are drawn whole in the same batches: the far save at 4x MSAA 2,
  off against `--native-draw shadows`, byte for byte (vblanks 300-420), 101 silhouettes whole in both.
- **Ties and native batches.** The batch decisions are perf's, over native corners as over the PS2 path's
  (their places, Z and alpha are the plan's): a two-phase tie's passes go through `nd_draw_range`, a call a
  kind of corner, as before, and the batches are joined as before. The depth bounds of a two-phase tie are
  not used for a native batch in a picture between (its corners' Z at t 0 is the GPU's; perf's
  `interp_z_at_0` is the PS2 path's): the picture is the same either way, only the first phase costs more.
- **Pictures between.** fix/underwater's `replay_only` targets, the pools' screen-made S and T and perf's
  replays apply to the PS2 path's draws; native draws are untouched by them (their moves are the shaders').
  The PS2 path's black blocks over 009's water at 240 are gone with fix/underwater (decision 3 below).

**The review's latent findings 1-3 fixed** (cf3aa186): 0x700's (and 0x5A0's) record flags are dropped when the
records buffer is replaced (`nd700_records_gone`; Granite reuses `Buffer` addresses); a wave snapshot keeps the
frame it was applied for and an older one is not used (`NativeDraw_WaveFrame`; the record is then drawn from
the game's coordinates, as `cpu`); `--native-wave-uvs` defaults to `ps2`: `late`, or `gpu` under the test wait
(the PS2 path's picture either way). Map 039 view 0, compare 1x without the wait: 771 pixels, worst 3 (`gpu`:
55k-130k); onoff 0; with the wait 0 and 0. The default still draws the offsets on the GPU (78 meshes parsed in
600 vblanks of 039; `cpu` 18,642).

**Every ported kind at once** (`map,sky,items,objects,picked,entities,arm,gold,effects,shadows,water,passes`),
pixels differing; headless, `--no-audio`, a fresh data folder, at most 2 games at a time:

| | compare 1x | compare 4x | onoff 1x | onoff 4x |
|---|---|---|---|---|
| 000 start (`newgame.pad`, `--teleport 1600:0:0`, 1900, 2000) | 0 | 54 (the 2D snap) | 0 | 54 |
| 009 start (`1600:9:0`, 1900-2000/50) | 0 | 0 | 0 | 0 |
| 016's bats (`1600:16:0`, 1900-2100/100) | 0 | 6 (the 2D snap) | 0 | 6 |
| 099 herb (`herb.pad`, 1800-1840/20) | 0 | 0 | 0 | 0 |
| 008's water, a mirror map (`m8w`, 300, 400) | 0 | 0 | 0 | 0 |
| the far save (an NPC's shadow, `walkfar.pad`, 240-420/60) | 0 | 9 (the 2D snap) | 0 | 9 |
| the fight on 000 (`strong3.pad`: hit flashes, shadows, the death fade) | 0 | 0 | 0 | 0 |

**At 240**, every kind, 1x (the game's frames; the pictures between):

| | compare | onoff |
|---|---|---|
| 000 walking and turning (`walk.pad`, 1900-1906) | 0; 17,286 in 24 (10,409 by more than 1), worst 54 | the same |
| the far save (300-306) | 0; 11,997 in 24, worst 135 (the shadow's and the NPC's outline) | the same |
| 009's water, turning (`turn9.pad`, 300-306) | 0; 764,999 in 24, worst 109 (was 1,127,017: the PS2 path's black blocks are gone; left is the ripple's texture, native's ST rebuilt against perf's screen-made ST) | the same |
| 009's fire (1900-1907) | 0; 248,873 in 28, worst 95 (as before) | |
| the fight's death fade (1926-1934) | 0; 85 in 31, worst 17 | |

**GPU time at the owner's settings** (`--native-scale 4 --native-msaa 2 --widescreen --native-widescreen`, 240,
`KF4_PRESENT_SIM=240`, `KF4_GSN_GPU_TIME=1`, 1,400 vblanks real time from bugs1022's `quick_far.kf4quick`, the
GPU otherwise idle, two runs each, the same to 0.01 ms; RTX 5080):

| `--native-draw` | GPU ms a picture | render passes a picture | pictures | pictures between left out |
|---|---|---|---|---|
| off | 2.94 | 5.8 | 5,234 | 3-6 |
| shadows | 2.98 | 5.8 | 5,234 | 6 |
| shadows, `KF4_GSN_SILHOUETTE_RUNS=1` (P5's runs) | 3.22 | 94.0 | 1,754 (pictures between given way for 1,166 vblanks) | 78 |
| every kind | 4.88 | 14.2 | 4,259 | 981 |
| map alone | 4.38 | 12.8 | 4,712 | 528 |
| sky, or passes, alone | 2.94 | 5.8-5.9 | 5,231-5,237 | 3-9 |

So the shadows cost nothing now (P5's "about 3x" was the runs). **Open:** `map` at 240 costs about 1.4 ms a
picture more and falls behind. At 30 the two make the same draws and passes (10.4 a picture; native 2.69 ms
against 2.40, the vertex shader), so it is the pictures between: there native draws make 9.0M draws against
5.2M and end 43,923 render passes for shader blending against 12,823 (and a replay costs 1,626 us against 518
on the GS's thread). Likely causes, not yet measured apart: the PS2 path's corners do not join native ones in
a picture between (decision 9), and a native batch in a picture between keeps its triangles outside the
scissor (in a blended batch, runs of their own). Probably not the merge (perf changed neither), but not
checked against a pre-merge build at these settings.

**The off check** passes: boot_test, frame_test gs and native, gsn_draw_test 0 of 300 (seeds 1, 7, 1234),
gsn_vram_test (20 rounds, all agree), vu1_test 0 of 90; against perf/gpu-vram's build (play-932801f): herb with
`--native-draw off`, the same `--log`, PATH1 hash 204e443a9fc49731, VU1 cycles 460424273, pictures 1800, 1820
and 1840 byte for byte; the far save at 240, 4x, MSAA 2, widescreen: the same `--log`, PATH1 hash
0f244d52ae8941bd, and all 12 pictures (game frames and pictures between) byte for byte.

**Left**, as after the third workflow (below), with these changes: decision 5 (`shadows`) has its answer for
the cost (the stencil question stays); decision 2's default is now the PS2 path's (`ps2`); decision 3's PS2
side no longer has black blocks; and the open item above (`map` at 240).

### Before: after the third workflow (2026-10-23)

Merged into `native/draw` on top of the second workflow (below), each with a merge commit: **nd3/f-013**
(F-013: map 013's ledge is the reference's DMA race, `KF4_ND_DMA_WAIT=1`; F-mixed: 0x000's and 0x280's
corners in one batch, records buffers replaced between batches), **nd3/f-skyrim** (the rim band fixed: a
native run's box at t 0, `nd_box_at_0`), **nd3/g-gold** (gold and the jointed map objects, bit8 1, through
the skinned adapter), **nd3/p4** (program 0x700 as a shader with its oracle; the effects), **nd3/p5** (the
shadows, program 0x5A0 drawn by `nd_000.vert`), **nd3/p6** (the reflection's rippled water; the wave UVs on
the GPU, `--native-wave-uvs`, `--test-dma-wait`) and **nd3/p7** (the screen passes). The per-task notes are
under each task. What the integration itself changed (in the merge commits, and one after):
- every native program's draws follow F-mixed's buffers (`nd_buffer_room`, `pending_000`): 0x700 shares
  0x000's records and arena, so `nd_draw_range` tells 0x700's stretches inside 0x000's kind (`nd700_corner`);
  the water's corners stay a batch of their own (`ndw_pending()` makes `nd_draw`, `nd280_draw` and
  `nd700_draw` draw them first; a water batch is drawn with `nd_water.vert` and its records); a screen pass's
  corners are of the PS2 path's kind, drawn with `nd_pass.vert`;
- `nd_box_at_0` (f-skyrim) no longer calls `nd280_batch()` (gone since F-mixed): a 0x280 or water corner, a
  record without a box, or the strip takes the whole target, and a screen pass's batch goes to
  `interp_box_at_0`; 0x700's records fill `nd.boxes` too;
- `nd_700.vert`'s `Draw` gained `wave[4]` (`NdConst` is 50 qwords since P6-2), and 0x700's records zero it;
- `RD_KIND_PASS` is 11 (P6's `RD_KIND_WATER` took 10);
- `KF4_ND_DMA_WAIT=1` (F-013, in the game loop) and `--test-dma-wait` (P6-2, in `Packet_SendChain`) are the
  same wait; `NativeDraw_TestDmaWait()` is on with either.

Kinds: `items` is accepted (`--native-draw on`). Ported and in work: `map`, `sky`, `objects` (posed, and since
G-gold the jointed ones), `picked`, `entities`, `arm`, `gold`, `effects`, `shadows`, `water`, `passes`. Every
ported kind at once: `map,sky,items,objects,picked,entities,arm,gold,effects,shadows,water,passes`. Not
ported: type 0x47 entities and their frame copy, `ItemEffect_Draw`, the passes the player code draws, the
frame copies and the depth clear (P7-1's notes).

**Every ported kind at once** against the PS2 path on the merged tree, pixels differing; headless,
`--no-audio`, a fresh data folder, at most 2 games at a time; up to 7 other agents shared the PC, so nothing
is timed:

| | compare 1x | compare 4x | onoff 1x | onoff 4x |
|---|---|---|---|---|
| 000 start (`newgame.pad`, `--teleport 1600:0:0`, 1900, 2000) | 0 | 54 (the 2D snap) | 0 | 54 |
| 009 start (`1600:9:0`) | 0 | 0 | 0 | 0 |
| 016's bats (`1600:16:0`, 1900-2100/50) | 0 | 26 (the 2D snap) | 0 | 26 |
| 099 herb (`herb.pad`, 1800-1840/10) | 0 | 0 | 0 | 0 |
| 008's water, a mirror map (`m8w`, `title.pad`, 300, 400) | 0 | 0 | 0 | 0 |
| 009's water and the miners (`m9w`, 300-360/30) | 0 | 28 (the 2D snap) | 0 | 28 |
| 013, P3-1's 8 still views (220-920/100), `KF4_ND_DMA_WAIT=1` | 0 | 0 | 0 | 0 |
| 013, the same without the wait | 8,397, worst 2 (the reference's race, F-013) | | | |
| the fight on 000 (`strong0`, `strong3.pad`: the arm, hit flashes, shadows, the death fade at 1930) | 0 | 0 | 0 | 0 |

The 4x numbers are the second workflow's exactly (the entities' 2D snap; nothing new).

**At 240** (`walk.pad`: `newgame.pad`, then walking from 1880 and walking and turning from 1920, after the
teleport; every picture of 1900-1931 compared: 16 game frames and 112 pictures between; at 4x 1912-1919):

| | game frames | pictures between |
|---|---|---|
| 000, compare 1x | 0 | 111,453 pixels (65,948 by more than 1), worst 143: the enemies (P2-5's slerp against the PS2 path's straight lines), the braziers, 1-2 level speckle |
| 000, onoff 1x | 0 | 336,618 (86,040 by more than 1), worst 143: the same places and more speckle (about 3,000 a picture, mostly 1 level, across the screen; a faint row near the horizon) |
| 000, compare 4x | 68 in 4 frames, worst 14: an enemy; `entities` alone gives it (the 2D snap), every other kind 0 | 459,049 in 28, worst 61; without `entities` 1,328 in 14, worst 8 |
| 016, compare 1x and onoff 1x | 0 | 26,694 (14,059 by more than 1), worst 103: the bats (top of the view) |
| 016, compare 4x | 1 (worst 1) | 106,749 in 28, worst 85 |

**The oracle** over the 64 tour captures (`kf4-gsreplay --nd-oracle`), `--vu1 hle` and `--vu1 interp` the same:
0x000 (0x5A0's batches included) 89,527 batches, 7,055 MSCALs, 1,555,504 vertices: 0 lane differences on drawn
vertices, the plan 0, the set-up 0; 0x280 19,528 runs: 0 lane differences, kicks 0, the parser equal on all
19,528; 0x700 2,341 runs (2,137 MSCALs, 21,306 vertices): 0 lane, kick, A+D and set-up differences, the parser
equal on all 2,341; 0x5A0 3,123 batches (1,404 MSCALs, 67,346 vertices): 0 on drawn vertices (39 on undrawn
ones, VU1's overflow), the set-up 0, 0 refused.

**The off check** passes on the merged tree: boot_test, frame_test gs and native, gsn_draw_test 0 of 300,
vu1_test 0 of 90, and herb with `--native-draw off` against main's 91c890e: the same `--log`, PATH1 hash
204e443a9fc49731, VU1 cycles 460424273, byte-identical pictures at 1800, 1820 and 1840.

**Left** (details under each task):
- The reviews and the owner's gates: P0-5, P1-7, P2-7 and the P4-P7 reviews; which kinds go into
  `PORT_ND_ACCEPTED` (`sky` is free of the rim band now; `shadows` costs GPU time at 240 until perf's whole
  silhouettes are merged; `passes` has the two cases in decision 6).
- Not native yet: type 0x47 entities and `GsDraw_CopyFrameToTexture`; `ItemEffect_Draw` (the draw list's
  `Model_DrawPosedDepthByPart`); `GsDraw_CopyFrame`, `GsDraw_ClearDepth`, `MapChange_FadeOutCapture`,
  `Player_DrawScreenFade` and `Player_DrawScreenTints`; the view shift's and the wobble's register lists.
- Not seen yet: an on-top effect (layer 3) that leaves pixels, and layer 2; an enemy's shadow on a slope; a
  jointed object animating in a picture between; real under-water, MP-drain and bit-40 scenes (P7-1 forced
  them); the six wave-UV maps with every kind and the wait (P6-2 ran `map`-side kinds on them).
- Pictures between: the effects' billboards are moved as matrices, not rebuilt for each picture's camera
  (NATIVE_DRAW.md 3.5); the spell bursts differ by up to 19 levels inside their polygons; under water at 4x
  (the wobble cells, about 1M pixels a picture, worst 25); the view shift at 240 with `--no-whole-frame`; the
  PS2 path's own black blocks over 009's water (its moving path, not native draw's); PS2-path corners still
  do not join native ones in a picture between (gsn.cpp's interp paths: F-mixed's note).
- Code: the draw_parts wrappers of gold, the jointed objects and entities are set with the switch off too;
  `native_draw.c`'s `posed_model` fills `RdDraw.colour` wrongly for flag 0x800 (no effect uses it); captures
  with `RD_VF_SILHOUETTE` meshes are refused by older kf4-gsreplay builds; the arena and records buffers are
  replaced at three quarters full, with no `--stats` counter of buffer splits; a 0x000+0x280 mixed batch
  never happens in the views tried (F-mixed's code is in place, unexercised); the follow-ups under "What is
  left of P1".

**The owner's decisions, collected** (where implemented at all, behind a switch that defaults to the PS2
picture; the answers of 2026-10-23 are at the top of "Where it stands"):
1. ~~The 2D snap at 4x~~ (*decided 2026-10-23: native's picture accepted; nd5/dec keeps native batches out of
   `flat_2d`*): `flush_batch`'s `flat_2d` moves the PS2 path's corners of an entity's sphere pass, never native
   ones. It is every 4x difference above (000: 54, 016: 26, 009's water: 28) and the entities' share at 240.
2. Blocks the game changes in place (wave UVs, object fades): copy the PS2 path's late read, or keep the
   frame-consistent one (*2026-10-23: undecided, the owner judges 039's flesh cavern in motion; the default
   stays `ps2`*). `--native-wave-uvs gpu` (the default until the merge of perf/gpu-vram: the frame the game drew, equal to the PS2 path
   with `--test-dma-wait`) or `late` (the PS2 path's race; 27 of 28 views equal to the racing reference).
   P1-2's 039/040 question; 013 joins it (F-013).
3. The water in pictures between: native recomputes the ripple's ST with the camera between (the reflection
   stays under the water); the PS2 path keeps the frame's ST and jumps at each frame.
4. A9: the reflection target's resolution and widescreen projection (untouched: 256x256).
5. ~~`shadows` into the accepted kinds~~ (*accepted, nd5/dec: perf's whole silhouettes are merged, the cost
   is gone; the stencil question stays*) before or after perf's whole silhouettes (about 3x the GPU time at 240
   without them); how perf's one-colour silhouette test treats native corners at that merge (their RGB is the
   corner encoding); whether the stencil (first write wins) is still wanted after it.
6. `passes`: the under-water pictures between at 4x, and the view shift at 240 with `--no-whole-frame`
   (*2026-10-23: wrong, to be fixed; `passes` stays out of `on` until it is*).
7. Whether the spell bursts' pictures between (whole translucent polygons, up to 19 levels) count as "edges"
   (*2026-10-23: undecided, the owner looks in play; `effects` is in `on`*).
8. The draw_parts wrappers set with native draw off (gold, jointed objects, entities): accept them, or one
   hook in `DrawList_Draw` (gfx.patch).
9. Whether PS2-path corners should join native ones in pictures between (for gsn.cpp's owner).
10. From before: P3-2's modern sky (A1; *2026-10-23: a modern sky unless there is a solid reason not to*), and
   the A-items' defaults (NATIVE_DRAW.md 5). And (2026-10-23) the miners' sway at 240: native's smooth movement
   is preferred (the default); P8: start it.

**Next: P8, standalone** (NATIVE_DRAW.md 3.4, and P8 in 4), once the owner says it is wanted (NATIVE_DRAW.md 5)
and the gates above are decided. Hosted mode now draws the play's 3D and the screen passes from draws; P8
takes the scene off the GS's targets: the page-ownership table and textures decoded from their source, the
scene's own targets (D32F reversed Z, float colour, real MSAA), the layer meshes as decals (A3), the GS's 2D
composited after `rd_end_world` and the passes (which become shaders of their own: P7-1's note), and
`--renderer modern`; checked against hybrid on the whole screenshot set, save states across modes, speed,
and Linux on lavapipe. Before it, the hosted leftovers (type 0x47 and its frame copy, the other frame copies
and the depth clear) must be native too, since standalone has no GS targets to read from.

### Before: after the second workflow (2026-10-23)

Merged into `native/draw`, each task's branch with a merge commit: the first workflow's P0-1 to P0-4 and
P1-1 to P1-6 (`nd/<task>`), then the second's (`nd2/<task>`): **A-exact** (map records exact in onoff mode:
mixed batches, the plan's cost, declines counted), **B-sweep** (the 64-map sweep again, ND_SWEEP.md, and
P1-6's numbers), **P2-1** (map objects, the picked item) with **P2-6** (the mutators measured; objects hash
a block once), **P2-2** (program 0x280 as a shader, its parser and oracle), **P2-3** (entities), **P2-4**
(the arm, the held weapon, the trail) and **P3-1** (the sky as a kind of its own). After the merges, one
commit made 0x280's corners use A-exact's corner encoding (below). The per-task notes are under each task.

Kinds: `items` is accepted (`--native-draw on`); `map`, `sky`, `objects`, `picked`, `entities` and `arm` are
ported and in work. Not ported: gold (it is skinned: P2-1's note), map objects with definition bit8 1 or 2
(`Model_DrawPosed` straight from `MapObject_DrawAll`), type 0x47 entities, effects (P4), shadows (P5), the screen
passes (P7). P6 (nd3/p6) added the kind `water` (the reflection's rippled water) and the wave UVs on the GPU
(`--native-wave-uvs`, for `map`), under their tasks below.

**Every ported kind at once** (`--kinds map,sky,items,objects,picked,entities,arm`) against the PS2 path, on
the merged tree, pixels differing (1x and 4x, compare and onoff; headless, `--no-audio`, a fresh data folder;
up to 8 other agents shared the PC, so nothing here is timed):

| | compare 1x | compare 4x | onoff 1x | onoff 4x |
|---|---|---|---|---|
| 000 start (`newgame.pad`, `--teleport 1600:0:0`, 1900, 2000) | 0 | 54 (an enemy at 1900: the 2D snap) | 0 | 54 (the same) |
| 009 start (the same, `1600:9:0`) | 0 | 0 | 0 | 0 |
| 016's bats (`1600:16:0`, 1900-2100/50) | 0 | 26 (the 2D snap) | 0 | 26 |
| 099 herb (`herb.pad`, 1800-1840/10) | 0 | 0 | 0 | 0 |
| 008's water (P1-4's `m8w` save, `title.pad`, 300, 400) | 0 | 0 | 0 | 0 |
| 009's water, the miners and their reflection (`m9w`, 300-360/30) | 0 | 28 (the 2D snap) | 0 | 28 |
| a fight on 000 (P2-3's `strong3.pad`, `strong0` save: the arm swinging, hit flashes at 1392, 1394, 1592, 1792, the death fade at 1930) | 0 | 0 | 0 | 0 |
| 037, B-sweep's 8 views (`map,sky,items`) | | | 0 (was 20,666) | 0 (was 331,946) |
| 028 and 109, B-sweep's 8 views (`map,sky,items`) | | | 0 (were 1 and 2) | 0 (were 55 and 40) |
| 042, B-sweep's 4 views (`map,sky,items`) | | | 4,374, as compare mode's (was 4,378, worst 18) | 70,450 in 4 views, as compare mode's (was 70,535, worst 42) |
| 037 and 013, B-sweep's 8 views, every kind | 0 | | 0 | |

- **The 2D snap** (000, 016 and 009's water at 4x only, the same in both modes): `flush_batch`'s `flat_2d`
  takes some tiny triangles of an entity's sphere pass (Q 1, white, one Z) for 2D rectangles and moves the
  PS2 path's corners to pixel edges; native corners are never moved. P2-3 found it on 016 and 009's water
  (26 and 28: the same numbers); on 000 it is an enemy at the right of the 1900 picture, `entities` alone
  gives the 54, P2-3's own build gives them too, and with the snap switched off (a local test) it is 0.
  The owner's call (keep 3D out of `flat_2d`, or snap native corners too); it is not native draw's maths.
- **B-sweep's onoff-only differences are gone**: they were a native draw splitting the PS2 path's batch,
  which A-exact's mixed batches fixed. 042's onoff numbers are now its compare numbers: the wave-UV
  difference, which P2-6 traced to the PS2 path racing its own DMA (not native draw's).
- **After the merges** (135bc96): A-exact tells a native corner by its fog below 0 and lets the PS2 path's
  triangles join a batch of native corners in a frame. P2-2's 0x280 corners still had the old encoding (the
  fog the arena slot), so a PS2-path kick after them would have been taken for one of theirs. Their fog is
  now `nd_corner_vertex` too (the 0x280 oracle's as well), `nd_280.vert` decodes it, and `nd280_draw` joins
  the batch as `nd_draw` does. All the numbers above are with it.
- **The oracle** over the 64 tour captures (C:/code/kf4-builds/maptour-09925c1, `kf4-gsreplay --nd-oracle`),
  with `--vu1 hle` and with `--vu1 interp`, the same in both: 0x000, 86,404 batches (5,651 MSCALs,
  1,488,158 vertices), 0 lane differences on drawn vertices, 0 in the plan's lanes, the set-up 0; 0x280,
  19,528 runs (2,913 MSCALs; 6,739 with a second pass, 4,666 with point lights, 10,742 all clipped),
  103,474 first-pass and 52,924 second-pass vertices drawn, 0 lane differences, the kicks and both sets'
  set-up the same in every run, the parser equal to all 19,528 of the game's blocks. (Undrawn vertices still
  differ where VU1 saturates and the GPU goes to inf/NaN, as before.)
- **The off check** passes on the merged tree: boot_test, frame_test (gs and native), gsn_draw_test (0 of
  300), vu1_test (0 of 90), and a herb run with `--native-draw off` against main's build 91c890e: the same
  `--log`, PATH1 hash 204e443a9fc49731, VU1 cycles 460424273, and byte-identical pictures at 1800, 1820 and
  1840.

**Left** (the details are in each task's notes):
- **P0-5**, **P1-7** and **P2-7**: the reviews and the owner's gates (`map`, `sky`, `objects`, `picked`,
  `entities`, `arm` into `PORT_ND_ACCEPTED`).
- ~~**P2-5**~~ (*done, nd2/p2-5, under its task*): pictures between for models (objects, the picked item,
  entities, the arm, the trail).
- From P2: gold (skinned: a call site at `Gold_Draw` through `NativeDraw_Skinned`, a `gold` kind and a gold
  drop to check); map objects with definition bit8 1 or 2; type 0x47 entities; the Fire Lord's afterimages
  not seen yet; a breaking object's fade not caught; `nd_arm.c` and `nd_entities.c` each hold a copy of
  `Model_DrawSkinned` without the data blocks (to share); `native_draw.c`'s `posed_model` still hashes each
  block twice (P2-6's change would save about 40% of the rest); declines of the newer adapters are not
  counted; ~~a 0x280 batch is never one of 0x000's~~ (*F-mixed, nd3/f-013: they join now; no view of the
  64 maps, the fight, the arm or the miners has such a batch; and the records buffers no longer split a
  batch: under P2-2*).
- From P3-1: the rim band in pictures between while pitching (native's camera move of the dome's lowest rows
  differs from `gs_replay.comp`'s: the moving path's owner; it keeps `sky` out of the accepted kinds); ~~013
  looking down at a ledge differs in `map`~~ (*F-013, nd3/f-013: not native draw's; the reference's DMA race on
  a wave-UV record, 0 with `KF4_ND_DMA_WAIT=1`: under P3-1*); P3-2 waits for the owner (A1).
- The 2D snap at 4x (above), the owner's call; ~~a test-only DMA wait~~ (*done, F-013: `KF4_ND_DMA_WAIT=1`*;
  the wave-UV maps can now be required to be 0 with it: not run yet on all six); the follow-ups under "What is left of P1".
- From P6 (nd3/p6): the owner's calls, the wave UVs' default (`gpu`, the frame drawn, or `late`, the PS2 path's
  race) and the water's pictures between (made by the camera between, not the PS2 path's); the PS2 path's
  black blocks over 009's water in pictures between (its moving path); type 0x47's frame copy; A9.


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
| `docs/port/ND_SWEEP.md` | the 64-map sweep (P1-2, again by B-sweep): a table per map, every difference with its cause |
| `port/gs/native/shaders/nd_280.vert`, `gsn_nd_280.inc` | VU1 program 0x280 (skinned models: entities, the arm, the held weapon) as a vertex shader, its CPU plan and records (P2-2) |
| `port/gs/native/gsn_nd_strip.inc` | a PATH2 strip the game built on the CPU (the weapon's trail), written to the GS as `gif()` would, with a draw's identity (P2-4) |
| `tools/gsreplay/nd_oracle_280.c`, `gsn_nd280_oracle.h` | the oracle for 0x280, both passes, its kicks, set-up and the parser on the game's own blocks (P2-2) |
| `game/native_draw/nd_objects.c` | map objects (`Model_DrawPosedWithAlpha`) and the picked item (P2-1, P2-6) |
| `game/native_draw/nd_entities.c` | entities (`Model_DrawSkinned`: `Entity_DrawAll`, the draw list, the Fire Lord's afterimages) (P2-3) |
| `game/native_draw/nd_arm.c` | the arm, the held weapon and the trail (`Player_DrawArm`) (P2-4) |
| `game/native_draw/nd_sky.c` | the map records' call site, the sky (flags bit 31) sent by `sky` and the rest by `map` (P3-1) |
| `port/gs/native/shaders/nd_700.vert`, `gsn_nd_700.inc` | VU1 program 0x700 (the effects) as a vertex shader on 0x000's records and arena, its CPU plan (the TEX0 swap, vertex n's kick rule) (P4-1) |
| `tools/gsreplay/nd_oracle_700.c`, `gsn_nd700_oracle.h` | the oracle for 0x700: lanes, kicks, the A+D packet (TEX0 swap), set-up, the parser on the game's blocks (P4-1) |
| `game/native_draw/nd_effects.c` | the effects (`Model_DrawPosed` with model flag 0x800: the draw list's sfx.dat models, every layer) (P4-2) |
| `game/native_draw/nd_shadows.c`, `port/gs/native/gsn_nd_5a0.inc` | the shadows (`Entity_DrawPoolModels`, VU1 program 0x5A0 drawn by `nd_000.vert`: P5-1) |
| `game/native_draw/nd_skinned.h` | `NativeDraw_Skinned` shared: gold and the jointed map objects through `nd_objects.c` (G-gold) |
| `game/native_draw/nd_water.c`, `port/gs/native/gsn_nd_water.inc`, `shaders/nd_water.vert` | the reflection's rippled water (`Wobble_DrawRippledModel`, `RD_PROGRAM_RIPPLE`, records of its own: P6-1); the wave UVs on the GPU are `nd_000.vert`'s `ND_C_WAVE` (P6-2) |
| `game/native_draw/nd_passes.c`, `port/gs/native/gsn_nd_pass.inc`, `shaders/nd_pass.vert` | the screen passes (`rd_pass`, `RD_KIND_PASS`, `RD_PROGRAM_PASS`: P7-1) |

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
   changes, as on the PS2 path, and the PS2 path's triangles that come next join the same batch (mixed
   batches, A-exact; in a picture between the native corners are still drawn first, `nd_kick`).
   `flush_batch` decides everything from them as it always has (targets, texture rows, blending, the alpha
   test's passes, Z and layer rules, runs). Only the draw call differs: `nd_draw_range` draws each stretch
   of one kind in its own call, a native stretch with the GS's own fragment shader and `nd_000.vert`
   (`nd_280.vert` for 0x280's corners), and each native corner says which arena vertex (its fog, -1 less
   it: below 0, where the PS2 path's are 0-255; 0x280's a further 2^22 less, so a corner says its
   program too), which kick (flat shading) and which draw record it is. Program 0x280's corners have a
   records buffer of their own, bound a stretch at a time, so a batch holds the PS2 path's, 0x000's and
   0x280's corners together (F-mixed); a records buffer or the arena is replaced early, between
   batches (`nd_buffer_room`), so that it seldom splits one. A weapon trail (`RD_PROGRAM_GS_STRIP`) is
   written register by register and its corners are the PS2 path's.
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
   and is moved t of the way with its model, as `frame_interp.c`'s `object_between` (P1-3). The models'
   kinds (objects, picked, entities, arm, gold) are paired by identity and section and move by their own
   matrices instead (`ND_C_MODEL`, P2-5, under its task).

### The switches

- `--native-draw off|on|KINDS` (`port_options.native_draw`, `port_native_draw(kind)`): `on` is
  `PORT_ND_ACCEPTED`, the kinds that have passed their checks and the owner's review (since nd5/dec: `map`,
  `sky` (P3-1), `items`, `objects` and `picked` (P2-1; `models` is both), `entities` (P2-3; an alias of
  `skinned`), `arm` (P2-4), `gold` (G-gold), `effects` (P4-2), `shadows` (P5-1) and `water` (P6-1)); others by
  name. In work: `passes` (P7-1; decision 6). Every ported kind at once:
  `map,sky,items,objects,picked,entities,arm,gold,effects,shadows,water,passes` (`on,passes`).
  `settings.json`'s `display.native_draw` (`on`/`off`) and the overlay's Debug > Renderer "Native draw
  (experimental)" switch the whole path (`PORT_ND_ACCEPTED` or nothing); `--native-draw` wins for the run.
- `--native-wave-uvs ps2|gpu|cpu|late` (P6-2): how a native map record's wave UVs are drawn; `ps2` (the
  default since the merge of perf/gpu-vram: `late`, or `gpu` under the test wait); `gpu` and `cpu` give the
  same picture, `late` the PS2 path's racing one. `--test-dma-wait` (tests only): each
  frame's chain waited for as it is sent, so the PS2 path draws the blocks the game changes as the game drew
  them (P2-6's race gone): the reference for the wave maps. `KF4_ND_DMA_WAIT=1` (F-013) is the same wait from
  the environment (the game loop waits after `Packet_SendChain`); either turns on both.
  Since P3-1 the sky (the map record with flag bit 31) is its own kind, `sky` (`game/native_draw/nd_sky.c`):
  `map` is every other map record, so what `map` meant before is now `map,sky`.
  The overlay's Debug page, "Native draw (experimental)", has a checkbox per ported kind, "Accepted kinds
  (on)" and "None (off)", and the compare mode, live; never saved (the Renderer section's switch is).
  Needs the native GS: off with `--renderer gs`.
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
    or before P1-5 a part over 64 blocks) falls back to the PS2 path. `--stats` now counts what
    `NativeDraw_Posed` declines ("models declined", A-exact); the newer adapters (`nd_objects.c`,
    `nd_entities.c`, `nd_arm.c`) still fall back uncounted;
  - **compare mode hides batch splits** too: `RD_ND_ALT_BEGIN` flushes in both of its runs, so a native
    draw that splits the PS2 path's batch shows only in onoff mode (B-sweep, A-exact);
  - at 4x, `flush_batch`'s 2D snap (`flat_2d`, scale above 1) moves the PS2 path's corners of a sphere pass
    (Q 1, white, one Z) taken for a rectangle, never the native ones: a few pixels on enemies at 4x (P2-3;
    below). Judge 0x280 kinds at 4x knowing it. The owner accepted native's picture (decision 1 of
    2026-10-23); since nd5/dec a batch with native corners is not passed to `flat_2d` at all (`nd_batch_3d`);
  - the PS2 path races its own mutators (P2-6): the DMA thread reads REF'd blocks after the game has gone
    on to the next update, so on the wave-UV maps (027, 034, 039, 040, 042, 114) and with fading objects
    the reference itself can be a frame late and differ run to run. `KF4_ND_DMA_WAIT=1` (a test switch, in
    both runs: the environment reaches both) has the game loop wait for the chain it has just sent before
    the update, so the PS2 path reads every block as the native draw does (F-013). Map 013 has a wave-UV
    record too (a ledge's water, out of the tour's view): with `map` it differs only through this race;
  - **compare the batches, not only the pictures**: a batch split where the PS2 path's goes on can leave
    the dumped pictures 0 and still decide runs and blending otherwise elsewhere. `--stats`' "native GS"
    line (draws, render passes, shader blends) off against onoff shows a split; a local log of every
    flush (size, the corners' kinds, a hash of X, Y, Z and alpha, the draws it made: a local
    patch, F-mixed) finds it. At 4x the 2D snap changes the draws too;
  - map 000's band needs `--input tests/newgame.pad --frames 1900,2000 -- --teleport 1600:0:0` (herb.pad
    did not show it); the mirror maps are 008, 009, 014 (reflection, flag 0x200) and 041 (the only mode-1
    mirror, flag 0x2), and their water is out of the teleport's view: use made-up saves placed by the water
    record's box (`tools/map_tour.write_made_up`);
  - the GPU's `1/x` is not correctly rounded and fused multiply-adds change results: the shader's
    `vu_*` helpers and `precise` are what make it exact; keep new maths in them;
  - **the game's own float code rounds toward zero** (its threads run with the EE's MXCSR, PORT_MXCSR_EE:
    port/platform/fiber.c; `--fpu-round nearest` to compare), the GS's thread to nearest: maths the game does
    in C floats (not VU0) and the native side redoes must round toward zero too (P6-1: the ripple's
    `/ 10240` was an ulp off until it did);
  - records can share their model's data blocks (the same model of the map file set up for each): a
    mutator's writes are the last writer's for all of them (P6-2: 042's wave records).

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
scanline on 030 at 4x. *Run again on the merged tree in both modes by B-sweep: ND_SWEEP.md (and the
wave-UV differences explained by P2-6: the PS2 path's DMA race).*

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

- ~~**The sweep again**~~ (*done, nd2/b-sweep: ND_SWEEP.md, and P1-6's numbers above*). Its onoff-only
  differences (037, 028, 109, 009, 042 v0: a native draw splitting the PS2 path's batch) are A-exact's mixed
  batches, measured on the merged tree under "Where it stands".
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

*Done for objects and picked (nd2/p2-1); gold is not posed.* Kinds `objects` (0x2, was `models`) and
`picked` (0x800); `models` is both. `game/native_draw/nd_objects.c` draws `Model_DrawPosedWithAlpha`
(its GS registers per part: context 1 solid, or PRMODE context 2 with TEX1_2/TEX0_2 and `ALPHA_2` FIX = the
fade; the trailer; the fixed block in onoff). The call site is the head of `Model_DrawPosedWithAlpha`
(`model.patch`), the one function every map object with definition bit8 0 draws through, both straight from
`MapObject_DrawAll` and from `DrawList_Draw` (the translucent and fading ones); a guard lets the compare
mode's own call through. Model flag 0x800 is left to the PS2 path (the function allocates 5 quadwords it
never writes). The picked item: `MapItem_DrawPicked` through `NativeDraw_PosedDepthByPart`, kind
`RD_KIND_PICKED` (`item.patch`). `native_draw.c` exports its helpers for nd_*.c files (`NativeDraw_PartMeshes`
...). **Gold is skinned** (`Gold_Draw` calls `Model_DrawSkinned`, program 0x280): it waits for P2-2, and its
call site goes with P2-3. Not native: map objects with bit8 1 or 2 (`Model_DrawPosed` straight from
`MapObject_DrawAll`; they go with the call sites when someone needs them).

*G-gold (nd3/g-gold, 2026-10-23): gold and the remaining map objects, kinds `gold` and `objects`.* A correction:
definition bit8 is a 1-bit field (bit 0x100), so "2" never happens, and bit8 1 is exactly the jointed objects,
which `Map_BuildModels` sets up with `Model_SetUpSkinned`: they draw through `Model_DrawSkinned` (0x280), as
gold does, not `Model_DrawPosed` (a probe over the 64 maps: 9,881 objects with bit8 0, 718 with bit8 1, in view
on about 58 maps). Both go through `nd_entities.c`'s `NativeDraw_Skinned` (exported by `nd_skinned.h`):
`nd_objects.c`'s `NativeDraw_Gold` (`RD_KIND_GOLD`, `--native-draw gold`, `PORT_ND_GOLD` 0x1000, an overlay
checkbox) and `NativeDraw_SkinnedObject` (`RD_KIND_MAP_OBJECT`, kind `objects`); declines counted. Call sites:
`Gold_Draw`'s one condition and a static `Gold_DrawSkinnedParts` as the draw_parts of a pile (a fading pile goes
through the draw list; `item.patch`), and `MapObject_DrawSkinnedParts` set in `MapObject_InitMap`'s case 1
(`map_object.patch`). Results: B-sweep's 64 maps x 8 views, kinds `objects,gold`, compare and onoff, 1x and 4x:
0 pixels on 63 maps, 0 refused, 0 declined (039 differs with 0 native draws: P2-6's race); made-up saves with
16 gold marks (4 views and a pickup with its fade): 0 in all 16 configurations, alone and with every kind; every
kind at once on 005, 012, 022, 100, 108: 0; at 240 the game's frames and the pictures between 0. **The
draw_parts wrapper is set with the switch off too** (as entities', the second review's finding 3): it breaks
NATIVE_DRAW.md 3.6's "the patches do nothing when off" though the game is byte-identical; the clean alternative
is a hook in `DrawList_Draw` (gfx.patch). Not seen: a jointed object animating (a door opening) in a picture
between; one of the 16 made-up gold marks never shows (not drawing's).

Results (1x unless said; 8 sweep views a map, made-up saves as P1-2): `--kinds objects,picked` 0 pixels on
all 64 maps in compare 1x, compare 4x and onoff 1x, but 039, which differs with **0** native draws in both
runs (its wave-UV timing; not this). 2,000-130,000 object draws queued a map. With every ported kind
(`map,items,objects,picked`), the differences are exactly `map,items`' own: 027, 034, 039, 040, 042, 114
(P1-2's wave UVs) and in onoff 028 (1 pixel), 037 (20,666, worst 2) and 109 (2), the same with objects
off: **the merged tree's map kind is not 0 in onoff on 028, 037 and 109** (for the sweep again). Fading:
only map 013's views have fading objects (pulsing glints, fade 0-32, through the draw list); 41 pictures
720-920 0 pixels in compare and onoff, and hiding them shows where they are (up to 61 levels). The picked
herb (`herb.pad` and CROSS at 1825-1885): 1840-1930 0 pixels in compare and onoff at 1x and 4x; at 240 the
pictures between differ at the moving herb's edges (1,100-2,600 pixels, worst 82: P2-5's moving path),
the game's frames 0. `--log` with `objects,picked` equals off's.

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
("Mixed batches"). Pictures between are not handled for 0x280 yet (P2-5). *(Both since: F-mixed below, P2-5.)*

*F-mixed (nd3/f-013, 2026-10-23): 0x000's and 0x280's corners in one batch, and batches the records split.*
- **The change.** A 0x280 corner's fog is -1 less 2^22 less its slot (`nd280_corner_vertex`; `nd_280.vert`
  and the 0x280 oracle take it back), so each corner says its kind: the PS2 path's (0-255), 0x000's (-1 to
  -2^22) or 0x280's (below). `nd_draw` and `nd280_draw` no longer flush for the other program's corners,
  only for another arena or another records buffer of their own program (`nd.pending_000`,
  `nd280.pending`, `nd280.batch_draws`). The batch is decided once over all of it; `nd_draw_range` draws
  each stretch with its program (`nd_program`, `nd280_program`, the GS's) and binds the stretch's records
  itself (set 2, binding 1; `nd_bind` binds the arena). In a picture between 0x000's and 0x280's corners
  join too (both native); the PS2 path's still do not (below). A later program (0x700, P4-1) takes the
  next 2^22 of the fog (2^24 is still exact).
- **Not seen in the game.** A local log of every flush (`flush_batch`'s batch: its size, corners of each
  kind, a hash of X, Y, Z and alpha, its draws and shader blends; a local patch, kept with the task's
  evidence) over B-sweep's 64 maps, 8 views each, with every kind: 0 batches with both. Nor on 016's
  bats, 009's start, the 000 fight, the arm saves (w5, w46) or 009's miners. An object's and an entity's
  A+D packets (TEX0 at least) differ, so the PS2 path's batch ends between them in every view tried.
- **What did split: the records buffers.** With every kind, onoff made 8 more draws than off on 016 and
  on 009 by 2,000 vblanks (and 1 more shader blend on 016). The logs showed 8 batches split in two, all
  of one kind: `nd.draws` (4,096 records) or `nd280.draws` filled up in the middle of a batch and
  `nd_new_buffer` drew it first (map records 318 = 48 + 270, an entity's 144 = 120 + 24). Now
  `nd_buffer_room` replaces a records buffer or the arena early, when a quarter of its room is left and no
  pending corner names it, and flushes only when it is full while one does. The logs of off and onoff
  are now the same batch for batch (size, state, hash, draws, shader blends) on 016 (130,127 batches),
  009 (157,344), the fight (631,033), arm05, arm46 and 009's miners.
- **Numbers** (every kind; headless, `--no-audio`, a fresh data folder, two games at a time, other agents on
  the PC: nothing timed). Pixels, compare 1x / 4x and onoff 1x / 4x: 016's bats 0 / 26 / 0 / 26; 009 start
  0 / 0 / 0 / 0; the 000 fight 0 / 0 / 0 / 0; 009's miners 0 / 28 / 0 / 28; 000 start 0 / 54 / 0 / 54; herb
  0 / 0 / 0 / 0; arm05 0 / 24 / 0 / 24. GS stats (draws, triangles, render passes, shader blends, target
  copies) off against onoff, the same at 1x on all seven (016: 832,854 draws, 7,998 shader blends; 009:
  466,517 and 69,698) and at 4x on 009 and the herb. At 4x the others' draws still differ (016: 832,980
  off, 832,854 on; the fight 3,140,193 and 3,132,149; 000 975,628 and 974,676) with the same passes and
  blends: the 2D snap. With `flat_2d` switched off in a local build, those five are 0 pixels in onoff at
  4x and their GS stats are the same: the snap moves the PS2 path's sphere-pass triangles, which changes
  the runs. It stays the owner's call (above). The 0x280 oracle over the 64 tour captures (HLE and
  interpreter) is still 0 lanes, 19,528 runs, kicks and parser the same.
- **Pictures between, the PS2 path's corners with native ones: not done.** There the PS2 path's corners
  are made on the GPU (`gs_replay_corners.comp`) into `interp.vertices`, a slot of the corner map per
  kick (sized by the frame's kicks: native corners have none), and `flush_batch`'s picture-between paths
  decide for the whole batch (the moves of `interp_batch`, the scissor cut skipped for native corners,
  `corners_at`, the vertex binding). Joining them needs those four places in `gsn.cpp` to work by
  stretch, more than a hook. Measured on 009 at 240 (onoff, 2,000 vblanks): 332,848 batches against
  off's 329,824, but native batches in a picture between already draw otherwise (the scissor cut is
  skipped for them: 2,663,384 draws against 1,358,196), so its GS stats cannot be equal either way. 016 at
  240 (1900-1939/3) is the same as before this change: the game's frames 0, the pictures between 3,137
  pixels at the top of the view, as with the base build.

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

*Done (nd2/p2-5).* What moves, and how (`gsn_native_draw.inc`, "Pictures between: the models' own moves";
`shaders/nd_between.h`):
- **Pairing.** A draw's key is its identity (kind, id), the section it is drawn in (world, reflection, arm)
  and which draw of both it is in the frame (`nd_key`: the held weapon 0x2E draws its parts twice, a miner
  is drawn in the reflection and in the world). The frame's own draw remembers each part's matrix, its
  parent's (0x280's set B) and the camera's view (`nd_remember`); a picture between finds the frame
  before's by the key.
- **The move.** Both matrices are split into a rotation R and the rest S (R S: scale, or shear) and the
  translation T (`nd_between`, in doubles). At the picture's time the part is R_C Q(u) S(u) p + T(u), u = 1 - t:
  the rotation by quaternion slerp from the frame before's, S and T along the line (NATIVE_DRAW.md 3.5). The
  shader adds R_C (Q(u) S(u) - S_C) p + (T_P - T_C) u p.w to where VU1 put the vertex, which is exactly 0 at
  t 1. Not paired: a jump of 400 or more, a turn of more than 3 radians, a matrix with no rotation in it.
  For 0x280 each matrix set has its own move (a part with no parent sends a slot of no matrix: set B then
  does not move; before that fix, such parts went unpaired and jumped a frame at each frame).
- **Where.** World (`objects`, `picked`, `entities`, `gold`): with the camera (P1-3's `moved`) and its own
  move seen from the camera between. Reflection: with the camera only, as the PS2 path's. The arm and the
  held item (the `PORT_FRAME_INTERP_ARM` section, now `ND_SECTION_ARM`; it was left where the frame had it):
  in the eye's space (view times model at both frames), the camera's move left out (`ND_C_EYE`). Map
  records and items keep P1-3's move (the sky by its translation); nothing changes for them.
- **0x280** (`nd_280.vert`) had no pictures between at all (entities and the arm stood where the frame had
  them while the world moved): it now has `nd_000.vert`'s moving path, its record the camera's projection
  and both sets' moves, its S, T and Q (first pass) by the change of depth, the same drop window.
- **The trail** (`gsn_nd_strip.inc`, `shaders/nd_strip.vert`): P2-4's native strip counted its vertex kicks
  among the frame's in a picture between, where `frame_interp.c` never saw them: every later kick read the
  wrong move and the recording broke ("65361 vertex kicks where the frame has 65357"). In a picture between
  the strip's triangles are now made in the include and drawn by `nd_strip.vert`, each corner t of the way
  from its partner in the frame before's strip: the points both share stay (they are screen points the
  game keeps), the new pair grows out of the old head. The PS2 path pairs only a strip of as many points,
  so its trail jumps a pair ahead at each frame while it grows; the native one does not.
- **`objects_pair`** (`frame_interp.c`): runs between `RD_ND_ALT_BEGIN` and `_END` while the native GS draws
  the native set (`--native-draw-compare native`, native renderer) are not paired, by batch or by order:
  the GS leaves them out, and the native draws are paired by identity. So compare mode's pictures between
  are onoff mode's (the arm's swing: the same 738,003 pixels in both).
- `--stats`' native draw line ends with the model parts and strips moved between their frames.

Results at 240 (1x unless said; every picture of the vblanks given: the game's own and the pictures between;
headless, `--no-audio`, fresh data folders; pixels differing from the PS2 path in compare mode, or from off in
onoff mode; the base is native/draw 893a881's build on the same runs):

| | the game's frames | pictures between (>1) | base |
|---|---|---|---|
| the arm's swing, weapon 5 (P2-4's save, 330-372), compare | 0 | 738,003 (274,738), worst 246 | 10,042,057 (6,223,424), and the recording broken |
| the same, onoff | 0 | 738,003 (274,738) | |
| 016's bats standing (1900-1940), compare | 0 | 48,670 (26,510) | 108,217 (98,268) |
| 016's bats turning (`lx` held from 1890), onoff | 0 | 26,712 (13,568) | |
| 009's miners and their reflection (`m9w`, 300-340), compare | 0 | 266,176 (147,129) | 175,275 (152,394) |
| the picked herb (`herb.pad`, CROSS at 1825, 1820-1880), `objects,picked`, compare | 0 | 176,114 (83,645) | 282,247 (225,700) |
| a fight on 000 (`strong3.pad`, `strong0`, 1388-1398), every kind, onoff | 0 | 132,135 (26,527) | 2,898,010 (1,657,308) |
| 000 walking (`newgame.pad`, `--teleport 1600:0:0`, `ly` from 1890, 1900-1930), every kind, compare | 0 | 123,585 (71,415) | 168,247 (134,013) |
| the swing at 4x (356-359), compare | 0 | 1,856,164 (259,847) | |
| 016 turning at 4x (1904-1906), onoff | 3 (the 2D snap) | 46,841 (25,144) | |

- **No jumps.** The pixels changing from each picture to the next, in the arm's half of the screen, rise and
  fall smoothly through the swing as the PS2 path's do; before the trail's fix both had a step at each frame
  of the swing (the trail drawn at the next frame's length), now neither the arm nor the trail steps while
  the trail grows. What still steps: the trail's tail dying at a frame (its points have no partner in the
  next frame), as on the PS2 path. In the fight, before set B's fix, the arm stepped at every frame (7,000
  against 2,500 a picture); now it does not.
- **What differs is edges and texels.** The arm and the weapon: their edges, a few pixels where the hand
  turns fastest (the PS2 path moves each vertex along the line between its two places; native turns the part).
  The fight's near sleeve: native keeps it fixed to the eye while the player walks and turns, the PS2 path
  moves it with the world and the camera, and its texture shifts by a texel. The bats, the herb, the
  enemies on 000: speckle on edges and inside, worst 1-2 levels for most. The miners on 009 differ more
  than the base in count (fewer by more than 1): the PS2 path leaves a model whose parts move less than a
  500th of their distance where the frame has it (`run_motion`), native moves the miners' idle sway by its
  sub-pixel amount, which shifts their texels (speckle inside the body, no edges moving). Not copied:
  that threshold is there for the PS2 path's places from the screen, which native does not have.
- **`KF4_INTERP_STILL`**: exact, the pictures between equal the next frame (the swing: 112 pictures, 0 pixels,
  as off; 016 turning: 84 pixels in 28 pictures, worst 2, the same as off: the PS2 path's own). It runs the
  CPU walk, where native draws are not moved, so a local test also drew it on the GPU with t 1 and the frame's
  camera: the models' moves vanish there (the arm: 14 pixels more than off's 735 over 112 pictures, two ground
  pixels at 1 level; 016: 77 as off).
- **Checks**: the oracle on the tour's 000, 009 and 016 captures, 0 lane differences on drawn vertices (0x000
  and 0x280, both passes); the off check: boot_test, frame_test gs and native, gsn_draw_test (0 of 300),
  vu1_test (0 of 90), and the herb with `--native-draw off` against 893a881's build (the same `--log`, PATH1
  hash 204e443a9fc49731, VU1 cycles 460424273, pictures at 1800, 1820 and 1840).
- **Left**: the trail's tail dying at a frame; the PS2 path's threshold for still models (above), the
  owner's call; the CPU walk (`KF4_INTERP_CPU`, `--renderer both`) still does not move native draws; type 0x47
  entities, gold and effects get their moves when they are native (`nd_model_kind` lists gold already).

**P2-6 The mutators** (owns the call sites of `Model_Set*`, `Model_FadeVertexColoursNear`,
`Model_SetUvsFromScreen` and `Model_Save/RestoreGsWordsAndUvs`). Measure them with `KF4_ND_DUMP` as P0-3
did for the map (it could not: those kinds were not native yet), and wire `rd_mesh_touch` only where the
hash's cost shows.

*Done (nd2/p2-1, after P2-1): `rd_mesh_touch` stays unwired; each object block is now hashed once a draw,
not twice.* The numbers are in NATIVE_DRAW.md, "P2-6's measurements". In short:
- **The mutators run at load, not in play.** A temporary probe (not committed) noted every `Model_Set*`,
  `Model_FadeVertexColoursNear`, `Model_SetUvsFromScreen` and `Model_Save/RestoreGsWordsAndUvs` call in the
  `KF4_ND_DUMP` of all 64 maps (P1-2's 8 views each, `map,items,objects,picked`). `SetPrimBits`,
  `SetAlphaBlend`, `SetTexture`, `SetVertexColour` and `SetTextureBase` ran only in the 2 or 3 frames
  after each view's load (4 to 21 frames of about 530 a run), and caused no re-parse in play. The
  screen-UV entities (type 0x47) are skinned: not native until P2-2/P2-3.
- **`Model_FadeVertexColoursNear` is the fading map objects' (trigger kind 31).** They are on 039 and 040
  only, and fade only when signalled. A probe that signals them showed the hash re-parsing just the blocks
  the growing radius reached (on 040, 2,044 re-parses over 208 fading frames, about 10 of the model's 45
  blocks a frame).
- **The writers are not all known.** On 015 and 101, object blocks changed at a view's load with no mutator
  call (the load wrote them), and the hash caught it. A touch scheme would have missed it. The touch would
  also save nothing on a block that is being mutated, since that block has to be parsed again anyway.
- **The cost was the hash done twice.** With objects native, hashing was 329 us a frame on average over the
  64 maps, and 747 us at worst (107). Objects added 111 us on average and 465 us at worst (116). Most of
  that was waste: `NativeDraw_PosedWithAlpha` hashed each block in its check pass and then again for the
  draw. It now keeps the meshes from the check (`nd_objects.c`). Objects' hashing went from 629 to
  339 KB a frame on average (2,667 to 1,346 at worst), and from 111 to 39 us (465 to 222). The same draws
  and the same re-parses were seen on all 64 maps. `native_draw.c`'s `posed_model` (map records and items)
  hashes twice the same way. The same change there would save about 40% of the rest; not done here
  (not mine).
- **Vertex fades in compare and onoff.** A probe faded the colours of every object drawn through
  `Model_DrawPosedWithAlpha` on 013, down to 0x10 and back. It ran in the update, as
  `MapObject_UpdateAll` runs the real fades. Result: compare 1x, compare 4x and onoff 1x 0 pixels. Without
  the fade, the pictures differ by 86,747 pixels (worst 67) at 220, 974 at 420 and 98 at 820, so the test
  sees it. With a fixed gen instead of the hash, the native pictures go stale there (87,874 pixels,
  worst 67).
- **The PS2 path races its own mutators.** This matters for every comparison on a map whose blocks change.
  The port's DMA thread (`dmac.c`) reads the chain's REF'd blocks after `Packet_SendChain` returns, while
  the game goes on to the next frame's update (`func_00112BD0`'s wave UVs, `MapObject_UpdateAll`'s fades).
  So the PS2 path can draw a block as it is one update later. The native draw takes the block at the draw
  call and does not race. With the fade probe in the update, compare differed by 86,840 pixels (one fade
  step, worst 24). With a `Packet_WaitForSend()` before the update's writes, it was 0.
- **This is P1-2's wave-UV difference.** The same wait in a probe build, before `func_00112BD0` in the
  game loop, takes compare 1x with all kinds to 0 pixels on 027, 034, 039, 040, 042 and 114. Without it
  they differ by 2,478, 3, 141,243, 145,556, 4,374 and 1,234 pixels. Onoff on 039 with the wait: 1 pixel,
  worst 1.
- ~~**Proposal**~~ (*done by F-013*): the test-only switch with that wait is `KF4_ND_DMA_WAIT=1`
  (`NativeDraw_TestDmaWait`, one condition after `Packet_SendChain` in the game loop, `core.patch`).
- `--kinds objects,picked` with the single hash: compare 1x and onoff 1x 0 pixels on 63 of 64 maps. 039
  differs as before (141,243 / 35,967 pixels: the race above).

**P2-7 Review** (independent agent): P2's screenshot set side by side, compare and onoff, and the switch to
accept each kind.

### P3, the sky (can run beside P2)

**P3-1 `--sky ps2`** (owns the sky's records in `game/native_draw/` and its switch). The sky records (flags
bit 31) are already native as map records, with their Z and fog; make the sky a kind of its own (`sky`)
so it can be switched, and check it on all 8 sky maps (000, 002, 004, 013, 021, 027, 098, 099) at 1x, 4x
and 240 (walking, turning, strafing, looking up and down, from made-up saves). Compare
`FrameInterp_MarkSky`'s sky with the native one (the 1-2 level speckle P1-3 measured). Accepted: the game's
frames 0 pixels; pictures between only that speckle, explained.

*Done (nd2/p3-1).* `game/native_draw/nd_sky.c`: `NativeDraw_MapRecord` is the call site of `Map_DrawRecords` and
`Map_DrawLateRecords` now (behind `port_native_draw(PORT_ND_MAP | PORT_ND_SKY)`; `map.patch` changed in those two
lines and the prototype only); it sends the sky record (flags < 0) by `sky` and the others by `map`, each drawn
as before by `NativeDraw_Posed` (identity unchanged: `RD_KIND_MAP_RECORD` and the record, so pictures between
pair it as before). `--sky ps2` is that; `--sky modern` would go in `sky_record`. The overlay has a "Sky"
checkbox. No `--sky` option yet (it has one value until P3-2).

Checked on the 8 sky maps, 1x headless, `--fast`, from made-up quick saves at places where the sky shows (found by
a scan of 12 places x 2 headings x 2 pitches a map: the sky's pixels counted by hiding it with a local-only switch;
places in the task's report). 8 still views a map: 4 headings at the place with most sky, 2 looking up (pitch
0.6) at the place with most sky overhead, 1 looking down, 1 more; 8 moving views at 240 (walk, turn, strafe,
look up with the right stick, look down, walk and turn), 4 vblanks each with their pictures between:

| | sky | map,sky,items | map,items (sky on the PS2 path) |
|---|---|---|---|
| still, compare 1x | 0 on all 8 maps | 0 but 013 and 027 (below) | the same as map,sky,items |
| still, compare 4x | 0 on all 8 maps | | |
| still, onoff 1x | 0 on all 8 maps | the same as compare | |
| 240 compare and onoff 1x, the game's frames (even vblanks) | 0 on all 8 maps | 0 | |
| 240 compare 4x, the game's frames | 0 on all 8 maps | | |
| 240, pictures between | speckle, 1-2 levels, but the rim band (below) | the same | |

- **The speckle** is P1-3's: worst 2 everywhere, in the sky while moving; at 1x a median of 8 pixels a
  differing picture, up to 3,765 (027, turning with the dome filling the view), at 4x up to 64,577 (the same). The PS2 path moves its sky in a picture between by one move for the whole dome,
  the median of its strips' mean moves found from their vertices' screen places (`objects_sky_move`); native
  moves the record by its own model's move (`ND_C_OBJ`, exact). The two moves differ by the error of the
  median's estimate, a fraction of a unit at 4,000-24,000 away, which moves an edge pixel's sample by a texel.
  With `KF4_INTERP_STILL=1` (the frame's camera) it drops to 90 pixels in 96 pictures, worst 1.
- ~~**The rim band**~~ (*fixed, nd3/f-skyrim: below*). While the camera pitches (the right stick up
  or down, view "look up") with the dome's lower edge on screen, native pictures between draw the bottom 3-10
  rows of the sky (where it fades into the fog colour) near black with scattered texels of wild colour, worst
  240, across the whole width (002: about 1,000-5,000 pixels a picture at 1x, 25,000-31,000 at 4x; 099 the
  same; the game's frames 0). It is the same with `map,sky,items` (so it was there when `map` held the sky),
  it is gone with `KF4_INTERP_STILL=1`, and it stays without `ND_C_OBJ` (a local test): the camera's move of
  those rim triangles in `nd_000.vert` differs from `gs_replay.comp`'s. Turning, walking and strafing do not
  show it. Needs the moving path's owner; until then `sky` should not go into `PORT_ND_ACCEPTED`.
  *F-skyrim (nd3/f-skyrim, 2026-10-23): fixed; it was not the shader's maths.* In a picture between,
  `flush_batch`'s `run_box` works out each run's rectangle (what shader blending copies, `COPY_IN_PASS` /
  `COPY_OUTSIDE`, and the render pass's area, `COPY_BY_PASS`). For the PS2 path's kicks `interp_box_at_0`
  widens it to where they are at t 0; native corners (vsrc `GSN_IM_NONE`) had only the frame's box, and
  `nd_000.vert` moves them out of it, so the sky's shader-blended rows blended with a copy that held nothing.
  `nd_box_at_0` (gsn_native_draw.inc; `run_box` calls it first, a hook in gsn.cpp) adds each native 0x000
  corner's place at t 0 (`moved()` at the frame's t 0 camera, `ND_C_OBJ` back at the frame before's) and the
  same margin; runs with `ND_C_MODEL`, 0x280's or the water's corners and the strip take the whole target
  (correct, not tight). Numbers (1x unless said): the 002 look-up repro 30,159 pixels / worst 240 -> 17 /
  worst 2; the sky's pictures between on 002 34,196 -> 8,232 (worst 2), 099 23,117 -> 12,145 (worst 2), the
  other six maps unchanged; 4x 002 568,229 -> 138,991 (worst 2), 099 380,174 -> 210,523 (worst 3, sparse
  speckle); the game's frames 0 on all 8 maps; the arm's swing at 240 unchanged in pixels and blend copies.
  What is left in the sky's pictures between is P3-1's 1-2 level speckle: `sky` can be considered for
  `PORT_ND_ACCEPTED` (P2-7/P1-7).
- **Not the sky's:** with `map` native, 013 (looking down at a ledge's floor near the camera, up to 22,512
  pixels, worst 3) and 027 (wave-UV water, P1-2's noise, worst 3) differ in the game's frames, identically in
  compare and onoff and with the sky on either path. 013's was not in P1-2's views (new; for P1's follow-ups).
  *F-013 (nd3/f-013): 013's is P2-6's race, not a decision of the native GS.* The review's capture
  (`--native-draw map --native-draw-compare native`, 805-809) replayed with a log of every batch: the same
  batches, state and decisions (blending, passes, runs, layers) in both sets; only one batch of 69 corners
  differs, a record of flag 0x100 (wave UVs, `Wobble_ApplyWaveUvs` from `func_00112BD0`: alpha 0-128, ALPHA
  0x44), and only in S. Its PS2-path corners in a frame are the native ones of the frame after: the DMA
  thread read the block after the next update rewrote it. (The oracle passed because it runs both sides
  from the VIF data.) With `KF4_ND_DMA_WAIT=1`, P3-1's 013 still views, every kind, are 0 pixels in compare
  and onoff at 1x and 4x (the review's 8,397 at 1x and 135,213 at 4x; without the wait this build still gives the 8,397), and 800-830 at
  1x 0 in both modes. Matching the late read in the native draw instead is the owner's call (P1-2's 039/040
  question): the late read is the port's DMA timing.

**What `--sky modern` would show (for the owner, before P3-2).** Simulated with a local-only switch: the sky
drawn natively with Z not written (and on 002, 013 and 027, where it is the map's first record, Z ALWAYS: the
same as a sky drawn first at the far plane), against the PS2's sky, `--draw-distance 1` and `max`, 64 views a map
(14-16 places x 4 headings, or 8 places x 4 headings x 2 pitches):
- 002: a hill's top behind the walls rises above where the dome cut it (about 300 pixels at -4683,-2696 heading
  1.57; 170 at 2850,-315 heading -1.57).
- 027: the far hill's ridge is taller (1,200 pixels, a strip along its top, at -12155,3128 heading 1.57).
- 013: a sliver of a far tree trunk between the trees (50-70 pixels); 004: nothing from 14 places (the 27 corners
  P0-3 counted lie behind the canyon walls from where one stands).
- So the geometry behind the dome shows only as hilltops and trunks a little taller, at the draw distances tried.
- Two more things a modern sky must keep: on 013 the frame is not cleared under the sky (where it is not drawn the
  last frame's pixels stay: the dome must still cover everything it covers now), and on 004 the sky is drawn after
  the trees, whose leaves' soft edges blend onto it (drawn first, they still would; left out, they blend onto the
  clear colour).
The pictures (side by side: PS2, modern, the difference in magenta) are outside the repository, with the task's
report.

**P3-2 `--sky modern`** (owns its shader path; the owner decides A1 first). Centred on the eye, depth at
the far plane, drawn first. P0-3 found geometry behind the dome from the eye on 002, 004, 013 and 027: list
what appears when the dome goes to the far plane, with pictures, before anything is made the default. The
scroll targets stay the GS's (hosted). Accepted: the owner's look at the 8 maps and the draw-distance
settings.

### P4-P7 (the third workflow: merged; what is left is in "Where it stands")

The plan is NATIVE_DRAW.md 4 (P4-P7) and the survey in its 1.2.5-1.2.8 and 1.3. The same rules as P1 and P2:
a call site is one condition, the rest in `game/native_draw/`; draws join the GS's batch as the PS2 path's
would (mixed batches: a native corner's fog below 0, each program its own 2^22 of it, `nd_draw_range`
binding each stretch's records); a new VU1 program is a shader in VU1's
arithmetic with its oracle, as P2-2 did for 0x280; the game's own frames 0 pixels in compare **and** onoff
mode, at 1x and 4x (knowing the 2D snap); every kind at once checked again before a merge.

**P4-1 Program 0x700 as a shader** (owns `shaders/nd_700.vert`, `gsn_nd_700.inc`, the parser's 0x700 batches,
the oracle's 0x700 part). One directional light, one model colour (0x64), the TEX0 substitution (0x65.w the
TBP to replace, 0x66 the new TEX0: the flipbook frame), Q `1/(16 w)`; `vu1_hle_700.c` is the specification.
Accepted: 0 lane differences on the tour's 0x700 runs, against the HLE and the interpreter.

*Done (nd3/p4).* `shaders/nd_700.vert` (nd_000.vert's sibling: no vertex colour but the model's, VU1 0x64,
which the record carries in `point_col[0]`; no point lights), `gsn_nd_700.inc` (`nd700_draw`, hooked in
`nd_draw` for `RdDraw.program` 0x700), the oracle's part in `tools/gsreplay/nd_oracle_700.c` and
`gsn_nd700_oracle.h`. What 0x700 does beyond 0x000 (all in `vu1_hle_700.c`):
- the texture animation is the CPU's: when the A+D packet's second write (TOP+0xA2) has the TBP0 that 0x65.w
  names, 0x66's qword is written instead (`nd700_ad`);
- the alpha is the draw's (lit.w 1 by the colour's w, MINI 255, FTOI0), so a batch's alpha range is one value;
- the loop runs n + 1 times: a "vertex n" (the position qword at TOP+1+4n) is clip-tested too, and the strip
  (PRIM and its triangles) is kicked only when fewer than n of the n + 1 are clipped (the A+D packet always
  is). It decides only when one vertex of the strip is unclipped, and then the strip draws nothing anyway
  (only whether PRIM is written changes). The parser keeps that qword when the block wrote it
  (`RdBatch::past`); no block of the game's effects does, and an unknown vertex n is taken as clipped (the
  one such run in the tour, on 037, had it clipped).
- Its records and arena are 0x000's (`nd.draws`, `NdConst`), so a batch holds 0x000's, 0x700's and the PS2
  path's corners together as the PS2 path's would; `nd_draw_range` draws each stretch with its own program
  (`nd700_corner` looks the record up in `nd700.of`).
- Pictures between: the position's w is 16 in the game's effects, so c.w is 16 times the depth (Q is
  1/(16 w): STEP3.md's fire fix). `nd_700.vert`'s `moved` takes the depth as 1 / (q w) and divides the
  model's own move (`between_move`) by w.
`rd_mesh_runs_program` (render.h) says whether every MSCAL of a mesh is one program.

The oracle over the 64 tour captures (`kf4-gsreplay --nd-oracle`, `--vu1 hle` and `--vu1 interp`, the same
in both): 2,341 runs of 0x700 on 59 maps (2,137 MSCALs, 21,306 vertices; 1,128 with TEX0 replaced, 1,088
without a strip), 9,114 vertices drawn and 48 not: 0 lane differences (X Y Z F w R G B A S T Q ADC and the
plan's X Y Z S T Q A), the kicks the same in every run, the A+D packets (the swap) the same, the set-up
(vertex matrix, light directions) the same in every MSCAL, and the parser on the game's blocks equal to
all 2,341 runs' batches. 0x000's and 0x280's numbers are unchanged (86,404 and 19,528 batches, 0).

**P4-2 Effects** (owns `effect.patch`, `game/native_draw/nd_effects.c`; after P4-1). `Effect_DrawAll` and
the draw list's layers 0-3 (`Model_DrawPosed` with `model_flags 0x800`), the blend hooks as they are (the
GS registers stay the game's), the billboards from `ModelMatrix_Build`, `ItemEffect_Draw`. Kind `effects`.
Accepted: the braziers on 000 and 009, spells cast (a made-up save with magic), the on-top layer after the
depth clear; 0 pixels in compare and onoff; pictures between for effects are P2-5's kind of work (sparks and
flames by their own matrices, flipbooks stepping with the frame).

*Done (nd3/p4).* `game/native_draw/nd_effects.c`, `NativeDraw_Effect`. The effects reach the GS through the
draw list (`Effect_DrawAll` adds them, `DrawList_Draw` calls the blend hook, `model->draw`, then
`draw_parts`, which is `Model_DrawPosed`), so the call site is the head of `Model_DrawPosed` for models with
flag 0x800 (only `Effect_SetupModel` sets it): one condition in `model.patch` for every layer and pass;
`effect.patch` is unchanged. The adapter queues `Model_DrawPosed`'s fixed block (with 0x64-0x66) outside
compare mode, then a draw per part of program 0x700 (or 0x000, if a part's blocks run that), kind
`RD_KIND_EFFECT`, identity the effect's model and the part (a freed effect's slot is reserved 4 frames, so the
next frame never holds another effect in it); `RdDraw.colour` is 0x64 (the colour), 0x65.w (the record id) and
0x66 (the flipbook's TEX0). A model that cannot go native is declined and counted. `--native-draw effects`
and an overlay checkbox. (`native_draw.c`'s `posed_model` still fills `colour` for flag 0x800 the old way,
0x65 and 0x66 wrong; no effect goes through it now.)

Checked (headless, `--no-audio`, fresh data folders; up to 7 other agents on the PC, so nothing timed):
- `--kinds effects`, 0 pixels at 1x and 4x, compare and onoff: 000's braziers and 009's fire
  (`newgame.pad`, `--teleport 1600:N:0`, 1900, 1950, 2000); 14 spells cast from a made-up save at 000's start
  (P2-4's `w5` with `PlayerState.equipped_magic`, 0x3D, set to 3, 6, 9, 12, 16, 21, 31, 36, 46, 51, 61, 66, 76,
  81; `--cheats spells,magic`, TRIANGLE at 330, 450, 570; pictures every 20th vblank 330-650) at 1x compare,
  spell 3 (a fire burst) also at 4x and onoff. Control: the native colour halved, 009's fire differed by
  95,967 pixels.
- Layers: 0 in all of these; 1 (view-locked, the on-top blend hooks) in spells 36, 51 and 66; 3 (after the
  passes and the depth clear: effect definitions 0x280, 0x282 and 0x284, the player's weapon kinds) only as 6
  draws with the bows and the crossbow (57-62, `--cheats items`), which leave no pixel (hidden, the pictures
  are the same); 0 pixels there. Layer 2 was not met.
- Every kind at once (`map,sky,items,objects,picked,entities,arm,effects`), compare and onoff, 1x and 4x: 000
  and 009 starts, 009's water (`m9w`), the fight on 000 (`strong3.pad`), weapon 5's swing, the herb, spells 3
  and 66: 0 at 1x everywhere; at 4x only the 2D snap (000 54, the swing 24, 009's water 3, spell 3 144,
  spell 66 71, the same in both modes; spell 3 and 66 without `entities` 0, `entities` alone 144).
- `--log` with `effects` equals `off`'s through a spell (801 lines); the oracle above; the off check
  (boot_test, frame_test gs and native, gsn_draw_test 0 of 300, vu1_test 0 of 90, the herb with
  `--native-draw off` against 66ffde6's build: the same `--log`, PATH1 hash 204e443a9fc49731, VU1 cycles
  460424273, pictures at 1800, 1820 and 1840).

Pictures between at 240 (`--kinds effects`, compare; the game's frames all 0):

| | pictures between | differing (>1), worst |
|---|---|---|
| 009's fire, standing (1900-1910) | 38 | 348,719 (178,636), 95 |
| 009's fire, turning (`lx=20` from 1890) | 38 | 289,516 (145,757), 80 |
| 000's braziers, turning | 38 | 0 |
| spell 3, the burst and its flight (340-370) | 108 | 1,215,044 (433,565), 72 |

- The fire: 1-2 level speckle over the flames (about 13,000 pixels a picture just after a frame, falling to
  2,000 before the next: 0 at t 1), and a few dozen pixels above 16 on one edge of a log. Turning, the
  pictures themselves move by about 270,000 pixels each; native and the PS2 path stay within the same speckle,
  so the depth from the position's w is right (taken as 1/q the fire would stand still between frames).
- The spell's burst (340-343): large faint additive polygons differ inside by 9-19 levels while the burst
  spins and grows: native turns and scales each part (slerp, lerp), the PS2 path moves each vertex along the
  line between its two screen places. It fades to speckle and edge pixels from 344 on. For the review: the
  same kind of difference as P2-5's arm, but over areas, because the parts are big and translucent.
- Flipbooks step with the frame (a picture between draws the frame's TEX0); billboards are moved as matrices
  between the two frames', not rebuilt for the picture's camera (NATIVE_DRAW.md 3.5 wants them rebuilt).

Left: `ItemEffect_Draw` (the item-use effect) is not native: its `draw_parts` is `Model_DrawPosedDepthByPart`,
called from the draw list, so it needs a hook in `Model_DrawPosedDepthByPart` (model.patch) or `DrawList_Draw`
(gfx.patch) and a scripted item use to check it. Billboards rebuilt per picture. Layer 2 and a visible layer 3
effect not met.

**P5-1 Shadows** (owns the call site in `entity.patch` after P2-3's, `nd_shadows.c`, program 0x5A0 as a
shader). `Entity_DrawPoolModels`: the flattened pool model, context 2 with DATE and FBA, `(Cs-Cd)·FIX + Cd`,
colour 0 and fog 0 (the area's fog colour). First exact as the PS2 path draws it (kind `shadows`); then, as
its own switch, the stencil (first write wins) that removes the ~240 runs a batch the GS cuts today (STEP3.md,
"Render passes"). Accepted: the statues on 000, enemies on slopes, 0 pixels against the DATE path in
compare and onoff, and the GPU time at 240 measured before and after.

*Done (nd3/p5), the exact part; the stencil is not done (below).* The call site is `Entity_DrawPoolModels`'
`model->draw_parts(model)` (`entity.patch`, one condition): `NativeDraw_Shadow(model, ENTITY_MODEL(entity))`
(`game/native_draw/nd_shadows.c`, kind `shadows`, `PORT_ND_SHADOWS`, an overlay checkbox) queues what
`Model_DrawPosed` would: its fixed block without the parts (not in compare mode), then a draw per part of
program 0x5A0 (identity `RD_KIND_SHADOW`, the entity's own model, the part). The GS registers around it
(PRMODE 0x248, TEST_2 with DATE, FBA_2, ALPHA_2's FIX) stay the caller's. Every entity has bit 17 unless its
enemy file clears it, so any entity with a pool model has a shadow.
- **Program 0x5A0 is 0x000 without the lights and the fog** (`vu1_hle_5a0.c`): the same set-up's vertex
  matrix, transform, 1/w, FTOI4, guard band, Z clip, ADC and ST by q; but no A+D kick, RGBAQ 0, the fog clamped
  to 0..0, and ST's z 1.0 for every vertex but the last two. So it has no shader of its own: the parser stores
  its batches (`RD_VF_SILHOUETTE`, `take_batch`'s `silhouette`: no A+D, each vertex's colour qword 0, ST's z
  4096 but the last two's) and its record has no lights, point lights or fog (`nd5a0_const`,
  `gsn_nd_5a0.inc`), and `nd_000.vert` and the CPU plan then give 0x5A0's numbers exactly (every light term is
  0 times a finite number; the fog clamp(0 + 0 q) = 0). A batch of 0x5A0 corners is one of 0x000's (the same
  records and program), so mixed batches hold.
- **The oracle** (`kf4-gsreplay --nd-oracle` takes 0x5A0's runs too, its vertices made by the parser's own
  code, `gsn_nd_oracle_5a0_vertices`; its own line in the summary): the 64 tour captures, 3,123 batches (1,404
  MSCALs), 67,346 vertices (22,642 drawn), **0 lane differences on drawn vertices** with `--vu1 hle` and with
  `--vu1 interp`, the set-up's vertex matrix (VU1's vf01-vf04) 0, no run refused; 39 lanes differ on vertices
  never drawn (VU1's saturation, as for 0x000).
- **Pixels** (shadows alone, and every kind at once `map,sky,items,objects,picked,entities,arm,shadows`;
  compare and onoff, 1x and 4x, all 0 but the known 2D snap): the owner's far quick save (map 000, an NPC's
  shadow) walked up to (`ly` from 250, pictures 240-420/20; the shadow grows to 348 pixels at 1x); the fight on
  000 (P2-3's `strong0`, `strong3.pad`: the enemy's shadow as it dies and fades, 1900 and 1930, about 6,000
  pixels each; its alpha is ALPHA_2's FIX); map 000's start (`newgame.pad`, `--teleport 1600:0:0`, 1900 and
  2000: 124-132 shadow pixels, a figure's by the gate and an enemy's at the left edge). Every kind at 4x: 000's start 54 and the far walk 13, both `entities` alone
  (the 2D snap), shadows 0. Where the shadows are was found with a local-only switch that left them out
  (the sweep's 8 views a map show none: the enemies' shadows on those maps are behind walls). `--log` with
  `shadows` (compare and onoff) is off's. Not caught: an enemy on a slope.
- **Pictures between** (240, compare): the game's frames 0; between them 1-28 pixels a picture at the
  shadows' outlines (the far walk 9 in all over 21 pictures; the dying enemy 232 over 39 pictures, worst 134
  where the alpha bit FBA left on one pixel feeds a later blend). Shadows are moved by their own matrices
  (`nd_model_kind`: the flattening matrix has rank 2, which `nd_split` takes).
- **The GPU time at 240** (headless, `KF4_INTERP_DRAW_ALL=1 KF4_GSN_GPU_TIME=1`, the far save walked up to,
  1,000 vblanks, `--native-scale 4 --native-msaa 2 --widescreen`, 3,730 pictures; other agents on the GPU,
  so noisy): off 4.3 and 5.1 ms a picture, `shadows` 12.7 and 11.7 ms, the same 56 render passes and 53
  resolves a picture. At 30 (no pictures between) off 1.54 and `shadows` 2.01 ms. The runs are the cause: each
  run of a silhouette is its own render pass and resolve, so the GPU does one run at a time, and a native
  corner's vertex shader is long (VU1's arithmetic, and in a picture between `moved()` for the corner's kick
  and the two before it): its latency, about 40 us a run, is paid 200 times a picture. With the perf branch's
  silhouettes drawn whole (`flush_batch`, "A silhouette"; 3d267b3, tried in a throwaway build, not here): off
  1.84 and 1.76 ms, `shadows` 2.57 and 2.41 ms, 5.4 render passes a picture, and the pictures still 0 pixels
  (the far walk compare 1x, the fight onoff 4x). **For that merge**: its test that a silhouette is one colour
  (`gs->verts[i].rgba == gs->verts[0].rgba`) fails on native corners, whose RGB is the corner's encoding
  (kick, IIP, record): the throwaway build took native corners of the same alpha as one colour.
- **The stencil** is not done: perf's whole silhouettes are the same idea in the GS (one draw a batch, each
  pixel darkened once), for both paths; a native stencil on top would change nothing more. Left to that merge.
- Also: the mesh cache checks the format a block is asked for (the review's finding 4); `declined` counts
  `NativeDraw_Shadow`'s declines.

**P6-1 The reflection's target and the rippled water** (owns `map.patch` then, `nd_water.c`). Mode 2's
256x256 CT16S target at 0x2BC0 (hosted: the GS's target, as today; at `256 x scale` only by the owner's A9),
`Wobble_DrawRippledModel`'s screen-ST strip as a draw with an identity (as P2-4's trail) and then on the GPU
from the sine phase, the refracting entity's frame copy (type 0x47's `GsDraw_CopyFrameToTexture`).
Accepted: 008 and 009 at 1x and 4x, 0 pixels, and 009 walking and turning at 240 (pictures between: the
water moves with the camera).

*Done (nd3/p6).* Kind `water` (`PORT_ND_WATER`, an overlay checkbox), identity `RD_KIND_WATER` (the model, the
part), program `RD_PROGRAM_RIPPLE`. The call site is `Wobble_DrawRippledModel`'s last line, where it appends its
packets (`gfx.patch`; `map.patch` unchanged): the function still runs, so VU0's registers and the packet buffer
are the PS2 path's. `game/native_draw/nd_water.c` queues the strip's GS registers as the packets write them
(TEX0_1, TEX1_1, ALPHA_1, CLAMP_1, RGBAQ; CLAMP_1 after), and a draw a part: its matrix (Matrix_Multiply in VU
arithmetic, without VU0) and the 32 sines (in `point_pos`/`point_col`). The meshes are the record's own blocks
(cached, `RD_VF_PACKED16`: only the positions and the data's ADC are read). `port/gs/native/gsn_nd_water.inc`
plans the strips on the CPU and `shaders/nd_water.vert` makes each corner on the GPU, both as the EE does:
places in VU0's arithmetic, the bounds test's ADC window, the sine `|(x+z)^3 % 32|`, S and T in the game's
floats, which round toward zero (the GPU's quotient made the nearest by Markstein's step, then moved toward
zero by its residual; checked against exact quotients). Records of their own, so a water batch is never one of
0x000's or 0x280's (the PS2 path's strips are a batch of their own too). `KF4_ND_WATER_CHECK=1` compares the
game's own packets with the same maths, vertex by vertex (0 of 824 differ on 009).
- The game's frames, every kind and `water` (and `water` alone), compare and onoff: 0 pixels on 008 (`m8w`),
  009 (`m9w`) and 014 (`m14c`) at 1x, on 008 and 009 at 4x but 009's 3 pixels of the 2D snap (an entity, as
  before). `--log` with `map,water` equals off's on 009 (600 vblanks, turning).
- Pictures between (009, 240, turning and walking, onoff, every kind): the native water's ST is made from the
  moved place, so the reflection stays under the water; the change from picture to picture in the water is
  even (1.6-2.3 a pixel, a step at each frame from the sines). The PS2 path's strip keeps the frame's ST and
  jumps at each frame (3.7-7.9). And **the PS2 path's pictures between draw black blocks over the water** while
  turning or walking (`--native-draw off` too: a PS2-path bug of the moving path, not native draw's; evidence
  `009_turn240_off_top_water_bottom.png`). So the pictures between differ from the PS2 path's over the water
  (about 47,000-80,000 pixels a picture at 1x); that is the design (the owner's look).
- Not done: the refracting entity's frame copy (type 0x47's `GsDraw_CopyFrameToTexture`): type 0x47 entities
  are still the PS2 path's (P2-3's leftover); the copy is the GS's and stays hosted. The 0x2BC0 target stays
  the GS's at 256 x 256 (A9 is the owner's).

**P6-2 Wave UVs on the GPU** (owns `gfx.patch` then, for `gfx/wobble.c`; after P6-1). The 16 oscillators as a per-vertex
offset in the shader instead of writes into the file's UVs, behind its own switch; then no block re-parses on
the six wave maps (P0-3) and no DMA race (P2-6). Accepted: 027, 034, 039, 040, 042 and 114 at 0 pixels
against the PS2 path with P2-6's DMA wait (the test switch first), and the hash's cost on 042 gone.

*Done (nd3/p6).* `--test-dma-wait`: `Packet_SendChain` waits for its DMA before the game goes on (`gfx.patch`).
`--native-wave-uvs gpu` (the default): a wave record's meshes are made from copies of its blocks holding the
vertices' own coordinates and each vertex's wave in the ST's w (`nd_water.c`, `NativeDraw_WaveMeshes`; a hook
in `native_draw.c`'s `posed_model`); the draw carries the 16 offsets (`RD_DRAW_WAVE_UVS`, in `RdDraw.parent`),
`NdConst.wave` (50 qwords now) and `ND_C_WAVE`; `nd_plan` and `nd_000.vert` add them in 16 bits as
`Wobble_ApplyWaveUvs` does. The offsets are kept at `Wobble_ApplyWaveUvs` (`gfx.patch`, behind
`port_native_draw(PORT_ND_MAP)`) by the model's data, with the writer's table: records sharing a model's
blocks show the last writer's coordinates (042, view 3: 1,504 pixels until it was so). The game still writes
the coordinates (the PS2 path, compare mode and the CPU's near cut read them).
- Against the PS2 path with `--test-dma-wait`, every kind and `water`, the 4 sweep views of 027, 029, 034, 039,
  040, 042 and 114: compare 1x, onoff 1x and compare 4x, all 28 views 0 pixels.
- Cost (042, view 0, 700 vblanks, onoff): blocks parsed 58,823 with `cpu`, 935 with `gpu` (put on the GPU
  59,356 against 935); the plan 0.159 s against 0.101 s. Hashing is the same (528.8 MB: the copies are hashed,
  so a writer we do not know of is still seen; cheap, P0-3). `gpu` against `cpu` at 240 (042 turning): the
  same 28 pictures, between too.
- **The reference (the owner's call).** Without the wait the PS2 path draws most wave blocks a frame late (its
  DMA reads them after the next update). `--native-wave-uvs late` uses the next step's offsets (worked out as
  `Wobble_StepOscillators` would): against the racing PS2 path, 27 of the 28 views are 0 pixels, and 039 v1
  19,958 (part of that frame read before the update); `gpu` against it: 039 87,000-130,000 a view, 040 v2
  156,018, 042 287-3,410, 034 and 114 a few. The default here is the frame the game drew (`gpu`, as native
  `map` already was); `late` is the PS2 path's picture, as far as a race can be had.

**P7-1 The screen passes** (owns `core.patch`'s pass call sites, `nd_passes.c`, `rd_pass`). Brightness, the
tint, fades and fills, the previous frame (aiming), the wobble grid, the depth clear, haze from depth and the
map change's capture as a short post chain instead of the GS's 64-pixel strips. Kind `passes`. Accepted:
integer-exact with `--native-colour exact` against the strips, aiming, underwater and the MP drain, the
brightness option's range; with the effects' on-top layer and the 2D still drawn after them.

*Done in part (nd3/p7): the eight passes of the task's text, as kind `passes`.* `game/native_draw/nd_passes.c`
(the adapter), `rd_pass` (render.h, render.cpp: a pass is a draw of the side buffer of `RD_KIND_PASS`, program
`RD_PROGRAM_PASS`, its words in `colour` and the six matrices a capture keeps, its identity the pass, its call
site and the site's count in the frame), `port/gs/native/gsn_nd_pass.inc` and `shaders/nd_pass.vert`.
- **What it is.** The brightness and the under-water tint (`RD_PASS_SCALE`), `GameLoop_DrawFrame`'s fills (the
  guardian's water, `map_draw_state`'s fill, the message dim, the pause), `Game_DrawFade`, the flicker filter (both),
  the frame shown before (bit 40 and aiming), the view-turn shift, haze and the wobble grid's cells. The native GS
  draws each into its own targets as the packets would have been drawn: the same register writes in the same order
  (so its state, batches and what follows are the PS2 path's) and the same primitives through its own rasteriser and
  `gs.frag`, but no VU1 run (0x276) and no packets. The brightness, the tint and the fills are **one sprite** instead
  of ten 64-pixel strips (each strip reads its own pixels, nearest, a texel a pixel; the registers left are the last
  strip's). The flicker filter's strips (63.5 pixels over 64 texels: a column of each is never drawn), the view-turn
  shift's 20 SCANMSK strips and the wobble's 70 cells keep the game's primitives.
- **Call sites** (`core.patch`): whole calls in `GameLoop_DrawFrame` (`if (!port_native_draw(PORT_ND_PASSES) ||
  !NativeDraw_X(...)) { Game_X(...); }`); inside `Game_DrawFade`, `Game_ShiftScreenByViewTurn` and
  `Game_DrawWobblingScreen`, around the drawing only (the fade's colour, the turn's offset, the grid's springs and
  its `rand()` stay the game's), followed by `NativeDraw_PassEnd()` (it closes `--native-draw-compare`'s PS2
  packets). The view-turn shift's and the wobble's register lists before and after stay the game's packets.
  `--flicker-filter` and `--no-whole-frame` work as before (the filter's strips only with the switch, its registers
  always; the shift only without whole frames).
- **Pictures between.** A pass is paired with the frame before's of the same identity and drawn by `nd_pass.vert`
  from a record (`NdPassBetween`), as frame_interp.c moves the PS2 path's 2D: a fill's or fade's colour t of the way,
  the frame shown before by A^t (reading the GS's copy, as marks 0xF4/0xF5 do), the wobble's texels t of the way.
  Its kicks are queued uncounted (`nd.pass_kicking`: frame_interp.c never saw them) and it is a batch of its own with
  the CPU's corners (`nd.pass_pending`). Its TEX0 of the frame drawn is moved to the buffer the picture is drawn
  into, as frame_interp.c's `ad_write` moves the PS2 path's (without it every picture between was the game's frame).
  The adapter queues `RD_ND_READS_SHOWN` before the previous-frame pass, so that frame_interp.c keeps the frame shown
  (it looked for a packet reading it; without it aiming's pictures between differed everywhere in onoff mode).
- **Hooks outside the task's files** (each a line or two): `gsn.cpp` (`vertex_kick`'s kick number; one condition in
  `flush_batch`: a pass batch does not move with the camera), `gsn_native_draw.inc` (the state's fields, `NdFlush`,
  `nd_kick`, `nd_draw_range`'s program, `nd_draw`'s dispatch, the frame's swap, the include), `frame_interp.c`
  (`RD_ND_READS_SHOWN`), `gs.c` (`--stats` counts the passes), `overlay.cpp` (a checkbox), CMakeLists.txt.

Checked headless, `--fast`, `--no-audio`, a fresh data folder each run; up to 7 other agents shared the PC, so
nothing is timed. The passes that `herb.pad` does not show were forced by a **local-only** switch (not committed):
under water with the tint and both wobbles, the MP drain's tint changing, bit 40 with its alpha changing, aiming,
bit 43, haze at three depths, the brightness option at 0x40 and 0xFF, a fill with its alpha changing, a fade every
120 frames, the message dim, the pause. `--native-draw passes`, pixels differing:

| | compare 1x | onoff 1x | compare 4x | onoff 4x |
|---|---|---|---|---|
| `herb.pad` 1800-1840 (brightness 0x88, the flicker filter's registers, the view-turn shift with `--no-whole-frame`) | 0 | 0 | 0 | 0 |
| under water: the copy, both wobbles, the tint (forced) | 0 | 0 | 0 | 0 |
| bit 40 and aiming (forced) | 0 | 0 | 0 | 0 |
| bit 43 and haze, `--flicker-filter` (forced) | 0 (and bit 43 alone) | 0 | 0 | 0 |
| a fill, a fade, the dim, the pause (forced) | 0 | 0 | 0 | 0 |
| the brightness at 0xFF, 0x40 (forced) | 0 (0xFF) | 0 (0x40) | | |
| a fight on 000 (`strong0`, `strong3.pad`: knock-back's previous frame, the death fade at 1930) | 0 | 0 | | 0 |
| the herb picked up, its message (`herbpick.pad`) | | 0 | | |
| every kind at once and `passes`: the fight, under water, 000 at 1900 and 2000 | 0 (fight) | 0 (fight, water) | 54 (000: the 2D snap) | |

At 240 (`--frame-rate 240`, every picture): the game's frames 0 in every case below; pictures between 0 for the
brightness, a fill changing, bit 40 changing (compare and onoff), aiming (onoff), the fight (onoff), haze with the
flicker filter, and the fills with `--realtime` (pictures drawn again from the recording). **Not 0:** under water,
compare 1x 1,322 pixels in 24 pictures (worst 251: a few edge pixels where the wobble's texel rounds the other way;
onoff 875 in 17); compare 4x about 1M pixels a picture, worst 25 (the 2D snap at 4x moves the cells' corners and
their texels by the frame's slope, the PS2 path's by the picture's: `nd_pass.vert` adds the frame's); and the
view-turn shift with `--no-whole-frame` at 240 (about 13,000 pixels a picture, worst 22, the game's frames too:
frame_interp.c's replay moves the PS2 path's strips by the turn between, the native ones are not moved; only with
both switches, which are not the defaults).

The off check passes: boot_test, frame_test gs and native, gsn_draw_test (0 of 300), vu1_test (0 of 90), and
`herb.pad` with `--native-draw off` against native/draw 66ffde6: the same `--log`, PATH1 hash 204e443a9fc49731,
VU1 cycles 460424273, byte-identical pictures at 1800, 1820 and 1840.

Left: `GsDraw_CopyFrame` (before the wobble, and the map change's), `GsDraw_ClearDepth`, `MapChange_FadeOutCapture`,
`GsDraw_CopyFrameToTexture` (P6-1's), and the passes the player code draws (`Player_DrawScreenFade`,
`Player_DrawScreenTints`: `player.patch`) are still the PS2 path's; the view-turn shift's and the wobble's register
lists are still packets; the under-water 4x pictures between and the view shift at 240 (above); real under-water,
MP-drain and bit-40 scenes from saves (only forced ones were seen); the standalone post chain (P8: the passes as
shaders of their own, once the targets are not the GS's). Nothing here changes the picture from the PS2's.

**P4-P7 reviews** (independent agents, one a phase): each phase's screenshot set side by side, compare and
onoff, and the switch to accept its kinds.

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
