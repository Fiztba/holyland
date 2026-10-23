# Native draw P8: standalone

P8 of [NATIVE_DRAW.md](NATIVE_DRAW.md) (3.4 and "P8: standalone"): the scene stops being drawn into the
GS's targets with the GS's Z. This page covers what the first round built (branch `nd5/p8`, from
`native/draw` 73887b93), its numbers, how to work on it, and the remaining P8 tasks split for up to 8
agents. Read [NATIVE_DRAW_TASKS.md](NATIVE_DRAW_TASKS.md) first: how to build, the switches, the
thresholds and the rules all still apply.

## What is built

`--renderer modern` (the native GS, plus `gsn_set_standalone`). Without `--native-draw` it means
`--native-draw map,sky,items`. Nothing changes without it: `--native-draw off` is byte for byte the PS2
path, and hosted mode (`--renderer native --native-draw KINDS`) is unchanged.

| Path | What |
|---|---|
| `port/gs/native/gsn_standalone.inc` | the core: the page-ownership table, textures from their source, the scene's targets and its turns with the GS's, a batch drawn into the scene. Included by `gsn.cpp` at the end of its namespace. |
| `port/gs/native/shaders/nd_000.vert` (`ND_STANDALONE`, built again as `sa_000.vert`) | program 0x000 with the scene's own depth: the GS's Z before any rounding (`p.zf`, the moved `zf` in a picture between), plus `pc.zbias` and the layer slope (`pc.zslope`, the corner's alpha) |
| `shaders/sa_depth.frag` (and `sa_depth_ms.frag`), `sa_copy_ms.frag`, `sa_sample.frag` | the scene's depth and colour given back to the GS's Z buffer and frame (sample for sample), and the cut-outs shaded at every sample |
| hooks in `gsn.cpp` (each one line, marked "standalone") | `sa_flush()` in `flush_batch`; `sa_drawn` in `mark_dirty`; `sa_uploaded` in `complete_upload` and `upload_done`; `sa_upload_begins` / `sa_copied` / `sa_sync` in `begin_transfer`; `sa_upload_replayed` in `replay_upload`; `sa_sync` in `take_snapshot`, `vsync_now`, `gsn_read_rect*`, `gsn_write_rect`, `gsn_vram`; `sa_forget` in `gsn_state` loading and `gsn_restore`; `gsn_set_standalone`, `gsn_print_standalone_stats` |
| hooks in `gsn_native_draw.inc` | `sa_draw_begin`/`sa_draw_end` around `nd_draw` in `nd_register`; `sa_waiting()` in `nd_kick` |
| `port/platform/main_host.c`, `gs.c`, `platform.h` | `--renderer modern` (`port_options.renderer_modern`), its default kinds, its `--stats` line |
| `tools/native_draw_compare.py --mode modern` | the same game hosted, then modern; the `[stats] standalone` line in `report.txt` |

### How a scene batch goes

1. **Which draws.** `nd_register` meets a draw. `sa_draw_begin` fetches it, and `sa_kind` says whether
   it goes to the scene: program 0x000 and a kind in `sa.kinds` (`RD_KIND_MAP_RECORD`, which includes the
   sky, and `RD_KIND_ITEM`; `KF4_SA_KINDS=0xMASK` of `1 << RD_KIND_*` to try others). A batch holds the
   scene's corners or none. A change either way flushes the batch first, and so does a PS2-path kick
   while the scene's corners wait (`nd_kick`). `nd_draw` then runs as in hosted mode: the A+D registers go
   through `gs_write` (the GS's state stays the PS2 path's), and `nd_plan` makes the corners (VU1's
   clipping, the arena, the records).
2. **The flush.** `flush_batch` calls `sa_flush()`. A batch of the scene's corners goes to
   `sa_draw_batch`. Anything else first ends the scene's turn (`sa_release`) and is drawn by the GS as
   before.
3. **`sa_draw_batch`** works out the batch's state as `flush_batch` does (FRAME, ZBUF, TEST, ALPHA,
   the texture, the alpha test's passes, cut-outs) and draws it into the scene's targets, with
   `sa_000.vert` and the GS's own fragment shaders (`gs.frag`; `sa_sample.frag` for cut-outs), so the
   colours are the GS's arithmetic.
   - **Depth.** The scene's depth is D32F holding the GS's Z unrounded (Z / 2^24, the same numbers the
     GS's D32F Z buffer holds), larger nearer: reversed Z, since the GS's Z is a + b/w. It is not kept
     within the Z format at the vertex: a vertex past it, under the eye, tilted the lava's plane.
   - **Ties.** The data counts on a later draw over the same surface winning (layers, overlays,
     signs, the hills' edge on the lava). A draw tested GEQUAL draws its Z first, exactly, then its colour
     within a slack, with no Z written: one Z for a draw that blends by an As that is not 0x80
     everywhere, or adds; half a Z for the others; and for a layer (the first kind writing Z), half its
     triangle's Z change across a GS pixel more, at most one Z (the corner's alpha carries it). This is
     `--native-layer-z tolerant`'s rule, without the GS's rounding, so there is no sawtooth where surfaces
     cross and no lace where two meshes' triangles differ. `KF4_SA_SLACK=LAYER[,TIE]` (tests) changes the
     slacks.
   - **Blending.** Vulkan's where it can do it. Where it cannot (DATE, a partial mask, PABE, no COLCLAMP,
     FBA with As, As above 1.0, Ad, FIX above 1.0) the shader blends, from a copy of the scene's colour
     (resolved if multisampled) taken before each run of triangles that do not overlap. A texture that is
     a render target is blended by Vulkan when it can be, not in the shader as `flush_batch` does for the
     frame passes' feedback (the lava over its scrolling target: a level off at most).
   - **Declined** (`[stats] standalone ... declined`). The batch goes back to the GS whole: another target
     (FRAME not 32 or 24-bit, or narrower than 512: the reflection's 256 x 256, the scrolling textures'),
     the frame and Z buffer that cannot be had together, a texture that is the frame itself, and
     multisampling on a GPU without sample-rate shading.
4. **The turns.** `sa_acquire` takes the frame target's picture and Z buffer into the scene's targets
   (copies: the multisampled images when `--native-msaa` is above 1), the first time a scene batch comes
   after the GS has drawn. `sa_release` gives them back before anything else of the GS touches them: a
   batch it draws, an upload or a copy, a read-back, a picture, a save state. Release copies only the rows
   the scene drew into: the colour sample for sample, and the depth as the GS keeps it (with its fraction
   under `tolerant`, rounded to whole Z under `ps2`). It marks those rows dirty. So the GS draws
   everything not in the scene yet (entities, effects, the arm, shadows, water, the passes, the HUD, the
   menus) over the scene's picture, in the chain's order: the 2D and the passes after the world are
   composited over it as the PS2 drew them. With map records and items in the scene there are about 2.3
   turns a frame on map 000.
5. **Pictures between.** The scene's draws, copies and passes go through `Cmd` like the GS's own, so the
   recording replays them. `sa_000.vert` moves the corners as `nd_000.vert` does (the same code), and the
   depth is the moved Z unrounded.

### Textures from their source: the page-ownership table

- **What it records.** For each 256-byte block of VRAM: the uploads that wrote it since one covered it
  whole (up to 4, oldest first), and the table's count at the last of them. For each 8 KB page: the
  count when a render target was last drawn into it (`mark_dirty`) or a copy wrote it (`sa_copied`). A
  block whose page was drawn into after its upload is not the upload's any more.
- **The chain's order.** The uploads are recorded on the GS's thread as the transfers complete
  (`complete_upload`; `upload_done` for one written as it came, read back from VRAM). That is the order
  of the CPU's chain, so the owner a draw sees is the one the PS2 would have sampled. A picture between's
  uploads asked for again (`replay_upload`) give their blocks back to the frame's uploads (`sa.uploads_frame`
  mirrors `gsn.cpp`'s `uploads_frame`).
- **Why not the upload functions.** The plan had `TextureTable_Queue*`, `Map_QueueSlotPacket`,
  `Effect_UploadTextures` and the rest tell the table. The transfers they queue reach the GS in exactly
  that order and carry the same bytes (the game RAM chunk the DMA REF sends), and the GS sees every
  writer, including those no hook would know. This is P0-3's rule for meshes ("the hash catches every
  writer"), applied to textures. No game code changed.
- **A chunk** is an upload's bytes and registers (BITBLTBUF, TRXPOS, TRXREG), kept once: found again by
  `upload_hash` and a byte compare, so an upload sent every frame is one chunk. The oldest unused for two
  frames are let go beyond 256 MB.
- **Decoding.** A texture all of whose blocks (every level's: the part the batch reads, as
  `flush_batch` works it out) belong to uploads is written into a scratch VRAM (4 MB, `gsn_vram.c`'s
  model): its blocks made zero, then its chunks oldest first. It is then read back as `decode_texture`
  reads VRAM, into the same raw image (indices for PSMT8 and PSMT4) that `gs_frag.h` samples, and found
  again by what it is plus its chunks' ids. So a texture is decoded once per source, however VRAM is
  shared. `KF4_SA_NO_TABLE=1` (tests) uses the GS's textures instead. The pictures were the same on the
  herb run: the table is exact.
- **What still comes from the GS:** a texture that is a render target (the scrolling textures' 128 x 128
  targets, the reflection, the frame copies), one on a page drawn into or copied over since its upload
  (`MapScroll_UpdateCopy`'s in-place scroll), and the CLUT. The CLUT is the GS's CLUT buffer, loaded by
  TEX0's CLD from VRAM, whose blocks are the same uploads; making it the table's is a task below.

## The numbers (first round)

Every run headless, `--no-audio`, a fresh data folder, at most 2 games of mine at a time. Other agents'
games were running on the PC, so the timings are noisy. Hosted means `--renderer native` with the same
kinds. Pixels differing, hosted against modern; the evidence (sheets: hosted | modern | heat x8, and the
reports) is in the session's scratchpad, `nd5_p8/evidence/`.

| scene | kinds | scale | result |
|---|---|---|---|
| herb (`herb.pad`, 1800-1840/20) | map,sky,items | 1x | 33 pixels in 3 pictures (worst 47: edges) |
| 000 walking at 240 (`walk.pad`, 1900-1906, every picture: 7 frames and 21 between) | map,sky,items | 1x | about 530 a picture, frames and pictures between alike: silhouette edges against the sky; the pictures between move correctly |
| 009 start (`newgame.pad`, `--teleport 1600:9:0`) | every kind | 1x | 4 pixels |
| 009's water and the miners (`m9w`) | every kind | 4x | 1,679 in 2 pictures |
| 016's bats | every kind | 1x | 0 |
| the fight on 000 (`strong0`, `strong3.pad`) | every kind | 1x | about 1,270 a picture, all but about 150 by 1 level (worst 40) |
| bugs1022 `quick_teeth` (the lava's horizon on 000) | map,sky,items | 4x | 2,004: **hosted has notches of black along the lava's far edge, modern a straight line** |
| the same, 3x, MSAA 2, native widescreen, cut-out AA | map,sky,items | 3x | 2,547: the same notches (hosted's) |
| `quick_tower` (the tower's top seen from below) | map,sky,items | 4x | 7,990: the rock layer over the tower's top a little different where the meshes are far apart in Z (hosted's slope rule kept) |
| `quick_brick` | map,sky,items | 4x | 14,072: a seam line at the floor and a patch on a tower's top |
| `quick_lacing` | map,sky,items | 3x | 61,685, of which 6,509 more than 1 (worst 33): blend rounding over the arches; no lace in either |
| bugs1024 `quick_bridge` | map,sky,items | 4x | 1,027 (worst 24) |
| blink `door.kf4quick` | map,sky,items | 4x | 432 (worst 23). The blink is in motion; one still picture cannot show it |

- **The tie-break artefacts.** The ones the GS's rounding made (lace, teeth, hatching, the lava's
  sawtooth) cannot occur in the scene's draws: their Z is never rounded. What is left of the ties is the
  slack rule. Before the slope term and the lava's blending were in, the first tries showed what goes wrong
  without them: stripes along a wall's overlay (no slack), and black teeth along the lava's far edge (the
  lava drawn by the GS from integer vertex Z against the scene's exact hills).
- **The off check passes:** boot_test; frame_test `--renderer gs` and `native`; gsn_draw_test 0 of 300;
  vu1_test 0 of 90; herb with `--native-draw off` against play-932801f: the same `--log`, PATH1 hash
  204e443a9fc49731, VU1 cycles 460424273, pictures 1800, 1820 and 1840 byte for byte.
- **GPU time** (the far save, 4x, MSAA 2, native widescreen, at 30, 1,000 vblanks, `KF4_GSN_GPU_TIME=1`):
  hosted 2.05 ms a picture, modern 2.80. The difference is the turns' copies (copy 0.47 ms, multisampled
  loads and depth 0.29 ms). At 240 both runs fell behind the game on this busy PC (two other games
  running), so the 240 numbers are not comparable yet. Turns cost about 0.33 ms each at 4x MSAA 2: they
  are the price of islands, and they go away as kinds move into the scene.
- **Not seen yet:** save states loaded across modes (a quick load works in modern; a `--state-load` taken
  hosted and loaded modern was not tried), Linux and lavapipe, the six wave-UV maps in modern, the
  under-water and MP-drain passes over the scene, the King's Map (its own loop, not tried).

## How to work on P8

- Everything in NATIVE_DRAW_TASKS.md, "How to work on native draw".
- **Compare:** `python tools/native_draw_compare.py --mode modern --kinds K ...` (hosted against
  modern, the same kinds). For a quick save, use `--input` with a pad that presses CROSS on the title
  (the review's `title.pad`: `100 CROSS 4`), then `-- --quick-load FILE`. Pictures between are dumped
  as `frame_N_250.png` etc.; the tool compares only the frames.
- **Debug switches** (read at the vblank that turns modern on): `KF4_SA_KINDS=MASK`, `KF4_SA_LOG=1` (each
  batch's FRAME, ZBUF, TEST, ALPHA and Z range, and why one is declined), `KF4_SA_PIXEL=X,Y` (the scene's
  batches over a GS pixel: the triangle's Z, TEX0, the blend, the slack), `KF4_SA_SLACK=LAYER[,TIE]`,
  `KF4_SA_NO_TABLE=1`.
- **Threshold.** Modern is not the PS2's picture bit for bit: the ties are the slack rule, not the GS's
  rounding, and some blends round differently. The bar is the owner's eye on the screenshot sets: no
  blobs, no missing or extra surfaces, no stripes or teeth. Differences are expected along silhouettes,
  at seams, and where hosted itself shows a GS artefact.
- **Ownership.** `gsn_standalone.inc` and the `sa_*` shaders are P8's. Hooks in `gsn.cpp` and
  `gsn_native_draw.inc` stay one line each; a task that needs a new hook says so in its report.

## The remaining P8 tasks (for up to 8 agents)

Serial points: **T1** (the scene draws every program) comes before T2, and T3 (the reflection) needs
T1's 0x280 for the entities in it. T4, T5, T6 and T8 can start at once. T7 reviews at the end.

| # | task | owns | acceptance |
|---|---|---|---|
| T1 | **Every program in the scene.** 0x280 (entities, the arm, gold, jointed objects: `nd_280.vert` with `ND_STANDALONE`), 0x700 (effects, with their TEX0 swap), 0x5A0 (shadows: a stencil, first write wins, instead of DATE/FBA, A17), the trail strip, posed objects and the picked item (0x000: `KF4_SA_KINDS` already draws them). The depth clear before the picked item and the arm becomes a scene clear (`GsDraw_ClearDepth` as a draw kind, or the GS's sprite met as a scene op). | `gsn_standalone.inc` (the batch path: one writer), `shaders/nd_280.vert`, `nd_700.vert` (their `ND_STANDALONE` blocks), `gsn_nd_280.inc` and `gsn_nd_700.inc` hooks | every ported kind in the scene: turns down to one or two a frame on 000 and 009; hosted against modern on the review's sets (000, 009, 016, the fight, the far save, spells 3, 36 and 66, `m8w`/`m9w`) at 1x and 4x: no blobs, only edges; shadows each pixel darkened once; GPU time at the far save no worse than hosted |
| T2 | **The scene's own end of the world.** With every kind in the scene, one turn a frame: the scene clears itself to the background colour (no acquire), and is given to the GS once, after `rd_end_world` and before the passes and the 2D. Then the composite becomes a real one: the GS's 2D drawn into a transparent layer and blended over the scene (premultiplied), so the scene can be at a resolution and sample count of its own (`--scene-scale`, `--scene-msaa`, independent of the GS's). The passes (P7's `rd_pass`) become scene shaders reading the scene's colour (and depth for haze). | `gsn_standalone.inc` (the turns), a new `gsn_composite.inc` | modern at 4x MSAA 4 with the GS at 1x: the HUD, the messages, the menus and the title the GS's, the world 4x; every P7 pass (under water, aiming, fades, brightness 0x40 and 0xFF, the MP drain) matching hosted within levels; GPU time below hosted's |
| T3 | **Render targets by name.** The reflection (mode 2's 256 x 256 CT16S at 0x2BC0, at 256 x scale or full resolution: A9), the scrolling textures (`MapScroll_UpdateDraw`'s 128 x 128 targets, `ScrollTexture_DrawAll`'s four at 0x3A40) and the frame copies (`GsDraw_CopyFrame` 0x2BC0, `GsDraw_CopyFrameToTexture` 0x3540, the menus' backdrop 0x3240) as scene images that own their pages in the table, and a texture on those pages read from them. `MapScroll_UpdateCopy`'s in-place scroll (a local copy) as a texture offset, or a scene copy. Mode 1's mirror (041) in the scene. | `gsn_standalone.inc` (the table's target side), a new `gsn_scene_targets.inc` | `[stats] standalone` shows no "a render target" or "drawn or copied over" textures on 000, 008, 009, 014, 041; the reflection on 009 at 240 walking and turning; the clouds and the lava scroll seamless with `--seamless-scroll` |
| T4 | **The CLUT and the rest of the table.** CLUTs from the table (mirror the GS's CLUT buffer: CLD 0-5, CSM1 and CSM2, CSA, from the chunks), PSMT8H/4HL/4HH textures, the 24-bit uploads' blocks (they own part of each word), and a block shared by more than 4 uploads. `--stats` counters per map of what is still the GS's. Texture packs keyed by chunk hash (a dump and a load folder) as the mods' groundwork. | `gsn_standalone.inc` (the table: "Who owns VRAM" and "Textures from their source") | the 64-map tour in modern: every scene texture from the table except render targets; the font's two sheets by CLUT (0x3FE0/0x3FE1) right when items' names show; a texture pack that recolours one wall texture |
| T5 | **The sky modern (owner's decision 3) in the scene.** `--sky modern`: the sky's records drawn first at the far plane (depth 0, reversed Z), centred on the eye, with their own fog. Before making it the default, measure P0-3's four maps (002, 004, 013, 027) where geometry behind the dome would show (3.6%, 0.7%, 2.8% and 0.3% of the corners sampled), and look at them. If that geometry looks wrong when shown, that is the solid reason to report; a fix is to draw it with the sky's depth (cut by the dome's radius). | `game/native_draw/nd_sky.c` (the flag), the sky path in `gsn_standalone.inc` | the sky sets at 240 (walking, turning, looking up and down) on 000 and 013; pictures of 002, 004, 013, 027 hosted against `--sky modern`, for the owner |
| T6 | **Save states, captures and replay.** The scene's state is derived (rebuilt from the chain), but the table is not: after a state load the textures come from the GS until they are uploaded again (`sa_forget`). Check states taken hosted and loaded modern and back; carry the table in captures (`.kf4gs`) so `kf4-gsreplay --renderer modern` replays the scene; the map tour in modern. | `port/debug/gs_capture.c` (records), `tools/gsreplay/`, `gsn_standalone.inc` (`sa_forget`, the table's save) | 20 states across modes with no wrong textures after the load; the 64 tour captures replayed modern, their scanouts compared with hosted |
| T7 | **Review and the owner's sets.** An independent review of `gsn_standalone.inc` and the hooks: races (the turns against every path that touches a target; `ring_idle` callers from other threads), the pictures between (recordings with turns in them), MSAA and colour-high, widescreen, `--native-layer-z ps2` (rounded release). The screenshot set for the owner at his settings (3x, native widescreen, MSAA 2, cut-out AA, 240): the spots in bugs1022 and bugs1024, hosted and modern, still and in motion (strips of the pictures between). | read-only | the review's findings ranked; the sets in the owner's index |
| T8 | **Performance, and Linux.** Time at the owner's settings at 240 on an idle PC (hosted against modern, `KF4_GSN_GPU_TIME`, `KF4_PRESENT_SIM=240`). The turns: copy only the rows drawn on acquire too, or draw the scene straight into the GS's images while they are the scene's (no copies), until T2 removes them. The tie rule's Z-first passes: batch them, or use `gs_zonly`-style programs. Linux build with gcc and lavapipe: `--renderer modern` on the herb run. | `gsn_standalone.inc` (the turns' copies, with T1's agreement), CI | modern no slower than hosted at 240 at the owner's settings; the herb run on lavapipe, its pictures against Windows' within edges |

### T5: the sky in the scene (done, branch p8/t5)

- **Far-plane skies (002, 013, 021, 027, 098, 099).** `sa_kind` now takes `RD_DRAW_FAR_Z` draws, so the modern
  sky is drawn into the scene, not by the GS between turns. `sa_000.vert` gives a far corner a depth of 0
  (`zf = 0`, the scene's clear value). Before this, only the GS's Z was 0. The dome's TEST stays Z ALWAYS, so it
  lays down the far plane wherever it is drawn (013's frame is not cleared under the sky).
- **000 and 004, `--renderer modern` only** (`nd_sky.c`). The dome is put at the eye, uses its own Z (the scene's,
  unrounded) and keeps the game's TEST (GEQUAL, Z written). It is drawn where the game draws it. Hosted mode
  keeps the PS2's dome, unchanged. On 000 the dome adds its clouds (ALPHA `0x48`, Cs * As + Cd) over the canyon's
  silhouettes beyond it. With real depth those stay soft dark shapes under the clouds, as on the PS2. Only the far
  plane turns them into black cut-outs. So no stencil or second pass is needed for them. The note in
  NATIVE_DRAW_TASKS.md that the eye with real Z also gives black shapes did not reproduce: hosted and modern
  at the eye both keep the shapes soft.
- **What the dome still hides.** It hides what the PS2's dome hides: pieces of `Map_DrawLateRecords`' records
  (drawn after objects, entities and items) that lie beyond the dome. On 000 that is 6 of 64 views, all at
  2260,-5200 looking up, up to 4,163 pixels at 1x. On 004 nothing hides in 64 views (the eye against the far
  plane: 0 pixels). Showing those pieces under the clouds needs the dome drawn after them. A try at that (the
  sky's batches held until the end of the scene's turn) did not help yet. A PS2-path draw (an additive object,
  Z written) comes between the sky and the late records and ends the turn, and an additive Z-writing draw
  must see the sky first anyway. It becomes possible once T1/T2 leave one turn a frame.
- **Numbers** (1x, 64 views a map: 8 places x 4 headings x pitch 0 and 0.6). Hosted `--sky modern` against
  modern: 002 3.1M pixels, 013 5,283, 027 4.4M. The far-plane maps' large counts are 1-2 levels all over
  the sky (on average +0.5 a channel), because Vulkan's blending rounds to nearest where the GS truncates (P8's
  known blend rounding, in the batch path). The hosted PS2 dome against modern on 000 is 7.7M (the clouds from
  220 units higher) and on 004 0.76M. At 240 (walking, turning, strafing, looking up and down, 128 pictures a
  map on 000 and 013), modern's step from picture to picture matches hosted's to within 2% in every view: no
  jumps.
- **Checks.** The off check is the same as before. Hosted `--native-draw on` and `--sky ps2` on the herb run
  are byte for byte e8a89c00's. Modern's herb pictures are the same too (no sky in them). Long modern runs with
  many quick loads still exit with 0xC0000005 or 0xC0000374 after printing their stats. e8a89c00 does the
  same (T8's open item).

**What the owner decides for P8:**
- The tie rule's look against the PS2's. Modern draws ties by the slack rule, so seams and layer edges
  can sit a fraction of a pixel from hosted. This is the cost of having no tie-break artefacts.
- The scene's resolution and samples apart from the GS's (T2): whether the HUD and menus stay at the
  GS's scale, and what `--native-scale` means in modern.
- A9 (the reflection's resolution and widescreen projection) when T3 makes it a scene target.
- The sky (decision 3), after T5's pictures of the four maps.
