# Native draw P8: standalone

P8 of [NATIVE_DRAW.md](NATIVE_DRAW.md) (3.4 and "P8: standalone"): the scene stops being drawn into the
GS's targets with the GS's Z. This page covers what the first round built (branch `nd5/p8`, from
`native/draw` 73887b93), its numbers, how to work on it, and the remaining P8 tasks split for up to 8
agents. Read [NATIVE_DRAW_TASKS.md](NATIVE_DRAW_TASKS.md) first: how to build, the switches, the
thresholds and the rules all still apply.

## What is built

`--renderer modern` (the native GS, plus `gsn_set_standalone`). Without `--native-draw` it means
`--native-draw on` (since T1; the first round's: `map,sky,items`). Nothing changes without it: `--native-draw off` is byte for byte the PS2
path, and hosted mode (`--renderer native --native-draw KINDS`) is unchanged.

| Path | What |
|---|---|
| `port/gs/native/gsn_standalone.inc` | the core: the page-ownership table, textures from their source, the scene's targets and its turns with the GS's, a batch drawn into the scene. Included by `gsn.cpp` at the end of its namespace. |
| `port/gs/native/shaders/nd_000.vert` (`ND_STANDALONE`, built again as `sa_000.vert`) | program 0x000 with the scene's own depth: the GS's Z before any rounding (`p.zf`, the moved `zf` in a picture between), plus `pc.zbias` and the layer slope (`pc.zslope`, the corner's alpha) |
| `shaders/sa_depth.frag` (and `sa_depth_ms.frag`), `sa_copy_ms.frag`, `sa_sample.frag` | the scene's depth and colour given back to the GS's Z buffer and frame (sample for sample), and the cut-outs shaded at every sample |
| hooks in `gsn.cpp` (each one line, marked "standalone") | `sa_flush()` in `flush_batch`; `sa_drawn` in `mark_dirty`; `sa_uploaded` in `complete_upload` and `upload_done`; `sa_upload_begins` / `sa_copied` / `sa_sync` in `begin_transfer`; `sa_upload_replayed` in `replay_upload`; `sa_sync` in `take_snapshot`, `vsync_now`, `gsn_read_rect*`, `gsn_write_rect`, `gsn_vram`; `sa_forget` in `gsn_state` loading and `gsn_restore`; `sa_clut_load` in `gs_write` (TEX0, after the GS's own CLUT load and flush); `sa_verify` in `flush_batch` (after `get_texture`: `KF4_SA_VERIFY` only); `gsn_set_standalone`, `gsn_set_scene_map`, `gsn_print_standalone_stats` |
| hooks in `gsn_native_draw.inc` | `sa_draw_begin`/`sa_draw_end` around `nd_draw` in `nd_register`; `sa_waiting()` in `nd_kick` |
| `port/platform/main_host.c`, `gs.c`, `platform.h` | `--renderer modern` (`port_options.renderer_modern`), its default kinds, its `--stats` lines; `gs.c` tells the GS the map at each vblank (`gsn_set_scene_map`, for the counts a map) |
| `tools/texture_pack.py` | texture packs: the dumped uploads as pictures and back, a CLUT tinted |
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

- **What it records.** For each 256-byte block of VRAM: the uploads that own part of it, in the order they
  came, each with the nibbles of the block's 64 words that are still its own (`SaCover`, 512 bits) and the
  table's count when it was written. An upload takes its nibbles from those before it, which keep the
  rest; one left with none is dropped, so there is no limit on the owners (the most seen on the 64-map
  tour: 2). The nibbles are what make the formats that own part of each word exact: PSMCT24 and Z24 the
  low 24 bits, PSMT8H the top byte, PSMT4HL and PSMT4HH a nibble of it; a CLUT's 16 entries, a 4-bit
  texture's half a byte, a rectangle's edge own their pixels and no more. For each 8 KB page: the count
  when a render target was last drawn into it (`mark_dirty`) or a copy wrote it (`sa_copied`), kept for
  its low 24 bits and its top byte apart (a 24-bit frame drawn over a page leaves PSMT8H and the 4-bit
  high textures in its top byte theirs). An owner whose page was drawn into after it in the nibbles a
  texture reads is not the upload's any more, and a texture's nibble that no upload owns is not the
  table's either (`sa_owners`: every nibble the texture reads, of every block).
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
- **The CLUT** (T4) is the table's too: `sa_clut_load` (TEX0 written, after the GS's own load and flush)
  mirrors the GS's CLUT buffer (`clut_load`): CLD 0-5 with the mirror's own CBP0/CBP1, the 8-bit and 4-bit
  formats, CSM1 (the 16 x 16 or 8 x 2 rectangle, entries 8-15 and 16-23 of each 32 swapped) and CSM2
  (TEXCLUT's line), CSA, the 32 and 16-bit CLUT formats. A load whose nibbles all belong to uploads is
  read from those uploads written into the scratch VRAM (found again by what it is and its uploads); one
  that is not is read from the GS's VRAM, its halfwords marked. A scene batch takes its colours from the
  mirror (`sa_clut_colours`, `clut_colours`' arithmetic), or the GS's buffer whole when one halfword it
  reads is marked. A scene batch waiting keeps the colours it was made with (the mirror changing alone, a
  texture pack's upload in it, flushes it).
- **What still comes from the GS:** a texture that is a render target (the scrolling textures' 128 x 128
  targets, the reflection, the frame copies) and one on a page drawn into or copied over since its upload
  (`MapScroll_UpdateCopy`'s in-place scroll: a local copy from 0x3C40 back over the texture, on 008, 018
  and 097): T3's. A TEX0 of a format the GS does not have (psm 0x10, on 103) is the GS's (its VRAM).
- **Checking it** (`KF4_SA_VERIFY=1`): every batch's texture, the GS's batches (`flush_batch`, after
  `get_texture`) and the scene's, as the table has it against the GS's VRAM, every row of every level, and
  the CLUT the mirror gives against the GS's buffer; `--stats` counts them (2 logs each that differs, 3
  also each not the table's, 4 each TEX0 once). On the 64-map tour in modern: 1,971,324 textures the same
  and 0 different (49 more before the psm-0x10 fix), 1,970,336 CLUTs the same and 0 different; the GS's
  batches' textures not the table's: 598, all the in-place scroll's. The font (PSMT4 at 0x3B40, its two
  sheets by CLUT at 0x3FE0 and 0x3FE1, CLD 4) is among them: the same wherever text was drawn on these
  runs (the title, the opening's messages, PAUSE). No run showed an item's name yet (picking up the herb
  by pad did not take); that check is still open.
- **`--stats` a map.** `[stats] standalone table` (CLUT loads by where they came from, the scene's CLUTs,
  the most owners of a block, what `KF4_SA_VERIFY` found, the texture pack), and `[stats] standalone map
  NNN` for each map the game was in (`gsn_set_scene_map` from `gs.c` at each vblank, the counts that grew
  since the last vblank given to it; -1: no map, the title): the scene's batches, the declined, its
  textures by where they came from, its CLUTs, and the verification.
- **Texture packs** (the mods' groundwork). An upload is known to a pack by its key: a hash of its format,
  its rectangle's size and its bytes, not where it went, so a pack follows a texture wherever the game puts
  it. `KF4_SA_PACK_DUMP=DIR` writes each upload a scene texture or CLUT came from, once: `DIR/<key>.bin`
  (its bytes as sent) and `DIR/<key>.txt` (format, size, where it went, the map). `KF4_SA_PACK=DIR` reads
  `DIR/<key>.bin` in place of an upload's bytes when the game sends it (the same size only): the scene
  decodes the pack's bytes (textures and the CLUT mirror), the GS's VRAM keeps the game's, so only the
  scene changes (the GS's batches, and hosted mode, draw the game's). `tools/texture_pack.py` makes
  pictures of the dumped uploads (`show`; PSMCT32 RGBA as the GS has it, the indices as grey), puts edited
  pictures back (`pack`; exact both ways: the 42 uploads of the herb run come back byte for byte), and
  tints a colour upload or part of it (`tint`). The game uploads its textures as PSMCT32 rectangles read
  as PSMT8 and PSMT4, and a map's CLUTs as one PSMCT32 upload (099's: 64 x 320 at 0x3D40, 80 CLUTs of 16
  x 16): to recolour a texture, tint its CLUT's 16 x 16 in that upload. The proof: the rock wall of 099
  (TEX0 0x8007a80265423140, CBP 0x3D40) tinted blue on the herb run, the floor, the herb and the HUD
  as they were (`texture_pack.py tint DUMP PACK 7347638491fdb72e 0.4,1.3,1.9 --rect 0,0,16,16`).

## T1: every program in the scene (branch `p8/t1`)

`--renderer modern` now means `--native-draw on` (`PORT_ND_ACCEPTED`), and the scene takes every ported kind:
map records and the sky, objects, items, gold, entities, the arm and the held weapon, its trail, effects,
shadows and the picked item (`sa.kinds`; not the reflection's water, T3, nor the screen passes, the GS's 2D).

- **Programs.** `nd_280.vert` and `nd_700.vert` have `ND_STANDALONE` blocks (built again as `sa_280.vert` and
  `sa_700.vert`): Z unrounded, with the tie slack and the slope term, as `sa_000.vert`. `sa_draw_range` draws a
  batch a stretch a kind, as `nd_draw_range`: 0x000's and 0x5A0's with `sa_000.vert`, 0x700's with
  `sa_700.vert`, 0x280's with `sa_280.vert` and their records, the PS2 path's corners (the trail; the scene's
  ops) with `gs.vert` (`nd_strip.vert` in a picture between).
- **A skinned model's second pass** (0x280's sphere-mapped shine) is drawn at its first pass's very corners,
  the same depth to the bit: no slack and no slope. With the layer slack it shone onto a part a Z or two in
  front (the seated man's thigh under his coat at the far save, 4x: a light band).
- **The shadows (A17).** The scene's depth is D32F S8 where the GPU has it. A run of shadow batches (0x5A0's
  records, DATE with DATM 0) draws where the stencil is not the run's number and writes it there: first
  write wins. The blending stays the GS's, in the shader, from one copy of the scene's colour a batch (no
  runs of triangles that do not overlap: the stencil keeps a pixel to one). The stencil is cleared when the
  scene takes the frame (its depth is then copied in by a pass, `sa_depth.frag`, as a depth with a stencil
  cannot be copied from the GS's D32F). `KF4_SA_NO_STENCIL=1` (tests): DATE in the shader as before. The
  fight's pictures are the same either way.
- **The scene's ops** (`sa_scene_op`): the GS's sprites the scene draws itself, so that it keeps the frame
  across them: `GsDraw_ClearDepth` (Z 0, every pixel failing the alpha test into Z only) before the picked
  item and the arm, and, while the scene has the frame, `GsDraw_SetScreenAlpha` (black, failing into the frame
  only, blended as Cd) before the shadows.
- **Fewer turns.** An upload or copy ends the scene's turn only when it touches the frame or Z buffer the
  scene has (`sa_transfer`, the `begin_transfer` hook; before, every texture upload between the world's draws
  did), and a batch of the GS's elsewhere in VRAM (the scrolling textures' sprites) is drawn while the scene
  keeps the frame (`sa_gs_touches_scene`).
- **Hooks** (reported): `gsn.cpp`: `begin_transfer` calls `sa_transfer(dir)` in place of `sa_sync()`, and `Cmd`
  has `set_stencil` (`OP_STENCIL`, recorded for the pictures between). `main_host.c`: the modern default
  kinds. `tools/build_shaders.py`: the two variants.
- **Debug:** `KF4_SA_LOG=2` prints only what ends each turn (1: also every batch and each decline).

**Numbers** (hosted against modern, `--kinds on`, the same exe; the first round's build, e8a89c00, in brackets:
its modern drew map, sky and items; headless, other agents' games running):

| set | 1x | 4x |
|---|---|---|
| herb | 53 (40) | |
| 000 start | 1,600 (1,233) | 22,688 (16,959) |
| 009 start | 195 (6) | 2,047 (32) |
| 016's bats | 299 (0) | 3,062 (102) |
| the fight (`strong3`) | 6,337 (6,297) | 60,701 (60,413) |
| the far save | 603 (48) | 4,327 (215) |
| spells 3, 36, 66 | 3,146 each (2,443) | 22,385 each (16,870) |
| 008's water (`m8w`) | 2 (4) | 73 (94) |
| 009's water and the miners (`m9w`) | 398 (121) | 5,866 (1,679) |

What the new pixels are: the edges of the models now in the scene (silhouettes, where parts meet or cross:
the torch stands' legs, a side face a pixel wide at a grazing angle, the bats' wings, the seated man's hand on
his thigh). No 8 x 8 block anywhere has more than half its pixels off by more than 8 (the far save's thigh,
before the second-pass fix, was the one). The shadows match hosted (each pixel darkened once), the spell
bursts too. At 240 (game frames; pictures between, a picture): walk on 000 3,159 in 4; 791 (base 556); the far
save 618; 155 (27); spell 3 6,303 in 8; 752 (546); 009's water 744; 219 (29); the fight 6,424; 96 (84).

**Turns** (`[stats] standalone ... turns a picture`): 000 0.93, 009 0.99, 016 0.89, herb 0.88, the spells 0.97,
the far save 1.05-1.26, `m9w` 1.57, the fight 2.16 (was 3.3 on herb before the transfer and elsewhere rules).
What still ends a turn mid-world: **map-record batches the PS2 path draws** (textured, fogged strips of 27 to
375 vertices, `KF4_SA_LOG=2`: prim 0x7c, ALPHA 0x44 and 0x48, at the map records' Z: draws the adapter
declines), the reflection's draws (declined: another target) and the HUD.

**GPU time** (the far save, `quick_far`, 4x, MSAA 2, native widescreen, 240, `KF4_PRESENT_SIM=240`,
`KF4_GSN_GPU_TIME=1`, 1,400 vblanks, two runs each, other agents' games running): hosted 3.72-3.89 ms a
picture; modern 2.73-2.76 (the first round's modern 2.69-2.79). Modern still falls behind at 240 (about 1,580
pictures against 5,230: T8).

**Not done:** water (its records and the reflection's texture: T3); the PS2 path's leftover map batches
in the scene (with `gs.vert` their Z would be the GS's whole Z, below the scene's fraction: a layer over a
native surface would lose its tie; T2 or a P1 follow-up). Every modern run that reaches the end exits with
127, or 0xC0000005 / 0xC0000374 at 240, after printing its stats: the first round's build does the same
(open, T8).

## T2: one turn a frame, the scene's own scale, the composite (branch `p8/t2`)

**One turn a frame.** The scene now keeps the frame from the world's first draw to the 2D:
- **The GS's clear** (libgraph's at the swap: untextured sprites of one colour, Z 0, the whole frame) is
  noted (`sa_note_gs_batch`, `sa.clear`). While nothing else of the GS touches that frame and Z buffer, the
  scene takes the frame by clearing its own targets the same: no copy (`sa_acquire`). Not on the maps whose
  modern sky (`RD_DRAW_FAR_Z`) the GS still draws first (002, 013, 021, 027, 098, 099: T5's).
- **The GS's own batches the scene draws** (`sa_gs_batch`, while it has the frame): the PS2 path's 3D (the
  map records native draw does not take, prim 0x7c: the turns T1 left), with `gs_zclamp.frag`, Z kept fine
  and the tie's slack plus half a Z (their Z is VU1's whole one, the scene's has its fraction); the reflection's
  water (`RD_KIND_WATER`, hosted's `nd_water.vert` with the same fragment program); flat fills over the whole
  screen (`GsDraw_FillScreen` from the player code); and **the screen passes** (`nd_pass_draw` marks them,
  `sa_pass`). In a picture between the GS's 3D and fills take their corners from `interp.vertices`, made on the
  GPU from the frame's kicks as `flush_batch` has them; a pass's are the CPU's (`nd_pass.vert`).
- **A pass that reads the frame** (the brightness, a tint, the flicker filter, haze) reads a copy of the
  scene's colour, at the scene's scale. **A copy of the frame elsewhere** (`GsDraw_CopyFrame` to 0x2BC0, which the
  wobble under water and in the MP drain reads) gives the GS the frame and keeps it (`sa_give(true)`), and the
  scene keeps its own picture as copied (`sa.fcopy`), read for a texture there until those pages are written.
- The GS's sprites and target-reading draws blend in the shader where Vulkan's blending would round
  `(Cs - Cd) C + Cd` (as `flush_batch` does), so the passes come out as hosted's.
- What ends the turn is then the 2D (the HUD, messages) or the vblank. Turns a picture (`--stats`), 1x:
  herb 0.86 to 0.21, 009's water (`m9w`) 0.81 to 0.27; 000, 016, the far save, the spells, the fight 0.21-0.44
  (one a frame; a picture is two vblanks).

**The scene's own scale** (`--scene-scale 1-4`, `--scene-msaa 1|2|4|8`; by default the GS's, and then
nothing below applies). `sa_shape`: the scene's targets are the frame target's size times the scene's
scale, with the scene's samples; its draws set the viewport and `M_SCALE` to it. Where it differs from the GS
target's, the copies resample (`shaders/sa_resample.frag`): taking a frame from the GS repeats each GS pixel
(colour and depth, under its middle), giving it back makes each GS pixel the mean of the scene's pixels and
samples over it (depth: the one under its middle, rounded as `sa_depth.frag`).

**The composite** (`gsn_composite.inc`, `shaders/sa_composite.frag`). When the scene gives the frame back for
good (`sa_hold`) it keeps its picture (hi: its samples resolved) and the GS's frame as given (lo). The GS draws
the 2D over its frame at its own scale as always. Of what it draws over that frame, the batches that are one
colour over the whole screen are kept in order, the chain: a flat colour blended (fills, fades, the dim, the
pause's shade; ALPHA with As or FIX), set, or the frame read a texel a pixel and modulated (the brightness,
the tint). At the scanout (`scanout_to_image`, a hook) the picture is made at the scene's resolution: a pixel
whose GS pixel is what the chain makes of lo (within 2 levels: Vulkan's blending rounds) has nothing of the 2D
over it and is the scene's, through the chain in the GS's integer arithmetic; any other is the GS's, its pixel
repeated (the HUD, messages, menus and title at the GS's scale). Why not the plan's transparent layer: the 2D
needs the frame's own alpha (the MP orb's mask: FBA, then DATE), and passes after the world read the whole frame
(the brightness, last, over the HUD too), which a layer's colour and coverage cannot carry. A frame kept is good
for the vblanks that show it: the GS's clear of that buffer, or a fourth vblank, lets it go. Headless dumps
of such a frame are the composite's (`--dump-frames` pictures at the scene's size).

**Numbers.** Same scale (the defaults): hosted against modern, the same exe, every set of T1's at 1x and 4x
and at 240 (herb; 000; 009; 016; the fight; the far save; spells 3, 36, 66; `m8w`; `m9w`; 240: walk on 000, the
far save, spell 3, 009's fire and water, the fight, the owner's settings on 000) is **the same pixel count as T1's
to the pixel**, with the water, the PS2 path's batches and the passes now in the scene.

Modern with the GS at 1x and the scene at 4x MSAA 4, against hosted at 4x MSAA 4 (`jobc.sh`; the HUD differs
by design, its corner left out; pixels a picture off by more than 8 / 32):

| set | outside the HUD corner, > 8 | > 32 | what |
|---|---|---|---|
| herb | 96, 38 | 10, 1 | edges |
| 000 start | 2,804, 2,904 | 271, 275 | edges (T1's 4x) |
| 009, 016, the far save, the spells, the fight | 170-2,900 | 3-278 | edges |
| 008's, 009's water | 650-7,000 | 4-470 | the reflection, its scrolling textures at the GS's 1x (T3) |
| under water (forced: the copy, the wobbles, the tint) | 4,598, 770 | 15, 0 | the lava's scrolling texture at 1x |
| the frame before (bit 40), aiming | 465-1,070 | 0-2 | it reads the GS's other buffer, at 1x |
| the brightness 0x40, 0xFF | 36, 0; 166, 12 | 0; 38, 0 | edges |
| a fill, the fade, haze | 0-96 | 0-10 | edges |
| the dim and the pause | 12,096 | 7,050 | the PAUSE letters, the GS's 2D at 1x |
| the flicker filter (`--flicker-filter`) | 590, 411 | 0 | a level or two over the frame |

At 240 the pictures between are composited too (the far save: 12 of 12; between pictures as their frames).

**GPU time** (`quick_far`, 1,400 vblanks, `KF4_GSN_GPU_TIME=1`, no other games running; ms a picture):

| | 30 | 240 (`KF4_PRESENT_SIM`) |
|---|---|---|
| hosted 4x | 2.16-2.17 | |
| modern, GS 1x, scene 4x | **0.83-0.85** | |
| hosted 4x MSAA 4 | 2.20-2.29 | 2.63 (5,234-5,237 pictures) |
| modern, GS 1x, scene 4x MSAA 4 | 2.82-3.11 | 5.54 (3,581 pictures) |
| modern 4x MSAA 4 (same scale) | 2.12-2.26 | 5.91 (3,287 pictures; T1's build 2.41 for 1,577) |

Without MSAA the composite is well below hosted. With the scene's MSAA it is not: its render passes
(`pass msaa`) cost about 0.7 ms more than the same scene at the GS's MSAA, not found yet (T8). At 240 modern
draws twice T1's pictures but still falls behind (T8).

**Not done:** the modern sky (`RD_DRAW_FAR_Z`) stays the GS's (T5): at another scale it is the GS's resolution,
and the frame is taken by a copy, not the clear. The reflection, the scrolling textures and the menus'
backdrop are the GS's targets at its scale (T3). The frame before (bit 40, aiming) reads the GS's buffer. 2D
drawn see-through over the world shows the world at the GS's scale under it. A fill or brightness whose colour
moves in a picture between (the GPU's) is not in the chain: those pictures are the GS's where they differ.
The overlay has no switch for the scene's scale yet. Every modern run may still crash at teardown (T8): at 240 with the
composite, 3 of 5 timing runs did, before printing their stats.

## T3: render targets by name (branch `p8/t3`, from `p8/t1`)

`port/gs/native/gsn_scene_targets.inc` (included at the end of `gsn_standalone.inc`) is the table's target side.

- **Named targets.** A colour buffer narrower than the frame (under 512 pixels: the reflection, 256 x 256 CT16S at
  0x2BC0; the scrolling textures' 128 x 128 targets; 0x3A40; the refracting entity's copy at 0x3540; ...) is a
  scene target named by FRAME's base, width and format, with images of the scene's own (the GS's colour format and
  scale). Its pages are its own in the table (`sat.page_named`, with `page_drawn`): a texture all of whose blocks are
  on its pages, drawn into after their upload, is read from its image (`sat_texture`, as flush_batch reads a
  target: `SRC_TARGET_*` at the offset of its first page). `[stats] standalone targets` counts them.
- **Who draws it.** The scene, for the 3D it draws there: `sa_draw_batch` no longer declines "another target" for a
  named one; `sat_acquire` binds it as the scene's frame (its images in place of the frame's, its own D32F S8 depth;
  no copies) and `sat_release` lets it go. The reflection's clear (`GsDraw_ClearFrame`'s black sprite) is the
  scene's (`sat_flush`): colour and depth cleared. **The depth is cleared to the far plane**: the PS2 tested the
  reflection against the main Z buffer's last frame read 256 wide, which can only make holes (hosted shows them as
  dark notches along 014's water edge; modern has none). The GS, for what it draws there (the scroll sprites, a
  sprite copying the frame, the PS2 path's map strips the scene does not take): the scene's image is made again
  from the GS's target when the scene next reads it (`sat_refresh`, with the GS's Z when the scene's was given
  back). The GS's target is given the scene's picture only when the GS wants it (`sat_give_back`): a batch of the
  GS's that draws there (with the depth when it uses Z) or reads it, a transfer over it, and `sa_sync` (colour only:
  a vblank, a read-back, a save state). A batch all outside its scissor gives nothing back.
- **The water in the scene.** `RD_KIND_WATER` (`RD_PROGRAM_RIPPLE`) is in `sa.kinds`: `nd_water.vert` has an
  `ND_STANDALONE` block (built again as `sa_water.vert`: Z unrounded, the tie slack), and `sa_draw_range` takes it
  for a batch of water corners. It reads the reflection the scene drew, so the reflection never goes back to the
  GS within the frame.
- **Copies from the table** (`sat_copied`). A local copy whose source is all the table's uploads
  (`MapScroll_UpdateCopy`'s in-place scroll through 0x3C40, on 000, 008, 009) is made in the scratch VRAM
  (`gsn_vram_copy` over the source's uploads) and its destination kept as an upload of its own: the scrolled
  texture is the table's, exact, and found again when the scroll comes round. The GS's VRAM is not read.
- **Mode 1's mirror** (041). A fill over the frame the scene has (`GsDraw_FillScreen`: untextured, failing into the
  frame only, Z not tested; `Map_CoverScreenWithMapTint` between the mirror and the real view) is the scene's op
  (`sat_fill`, `SA_OP_FILL`), blended in the shader from a copy, as flush_batch blends a sprite (the GS's
  arithmetic). So the mirror, the tint, the depth clear and the real view are one turn.
- **Hooks** (in `gsn_standalone.inc`, one line each): `sa_drawn`, `sa_copied`, `sa_texture`, `sa_acquire`,
  `sa_release`, `sa_draw_batch` (the decline test; `sat_blend_exact` in `shader_blend`), `sa_draw_range` (the water
  program), `sa_flush` (`sat_flush`, `sat_fill`, `sat_gs_batch`), `sa_transfer`, `sa_sync`, `sa_switch`,
  `sa_forget`, `sa_print_stats`; `sa_kind` takes `RD_PROGRAM_RIPPLE`. None in `gsn.cpp`.
- **Switches:** `KF4_SA_NO_NAMED=1` (tests): none of this. `KF4_SA_LOG=1`/`2` also say why a named target is given
  back; `3` also each GS draw into one.

**Numbers** (hosted against modern, `--kinds on`, 1x unless said; T1's build in brackets):

| set | `a render target` / `drawn or copied over` | declined "other target" | turns a picture | pixels |
|---|---|---|---|---|
| 000 start | 0 / 0 (622 / 0) | 0 (0) | 0.93 (0.93) | 1,600 (1,600) |
| 008 `m8w` | 0 / 0 (0 / 136) | 0 (16,957) | 0.92 | 2,320 (2) |
| 009 `m9w` | 0 / 0 | 0 (43,813) | 1.03 (1.57) | 97,208 (398) |
| 014 `m14c` | 0 / 0 | 0 (17,463) | 1.08 (1.35) | 33,592 |
| 041 `m41d` | 0 / 0 | 0 | 0.54 (0.81) | 3 |

- The review's other sets are unchanged (herb 53, 009 start 195, 016 299, the fight 6,337, the far save 603, the
  spells 3,146-3,147); `quick_lava` 48 and `quick_sky2` 0 as T1's build (the scroll targets are the GS's picture,
  so `--seamless-scroll` is the same), `quick_teeth` 242 and `quick_underwater` 134,708 as T1's.
- **What differs on the mirror maps is the reflection's colour.** The GS keeps it at 16 bits, rounding every blend
  into it to five bits a channel; the scene keeps 8 and rounds once, when the water reads it (`SRC_TARGET_CT16`).
  The water comes out 1 to 4 levels lighter (009: of 158,720 water pixels, 26,905 more than 2 off, 19 more than 8).
  This is A9's "CT16-like quantisation"; the geometry is the same (the figure's reflection, the pillars' on 014).
  Where hosted differs in shape it is the PS2's holes (014's water edge).
- At 240 (`turn9.pad`, `walk9.pad`, 009, every picture): the reflection moves with the camera in the pictures between
  as in the frames, the same lighter water (about 46,000 pixels a picture, frames and pictures between alike).
- Named targets are single-sampled (the GS draws the reflection multisampled under `--native-msaa`).

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
  `KF4_SA_NO_TABLE=1`; the table's (read once): `KF4_SA_VERIFY=1..4`, `KF4_SA_PACK=DIR`,
  `KF4_SA_PACK_DUMP=DIR`. `KF4_SA_LOG=3` (T2): the GS's batches the scene draws, and those it draws elsewhere.
- **The scene at its own scale** (T2): `native_draw_compare.py --mode modern --scale 1 --first-args='--native-scale 4
  --native-msaa 4' --second-args='--scene-scale 4 --scene-msaa 4'`.
- **The map tour in modern.** `map_tour.py` has no `--renderer modern` yet (T6's); the T4 runs changed its
  commands on the way (`--renderer modern`, `--no-audio`). A modern run ended in a broken heap at its exit
  (0xC0000374 or 0xC0000005, after the last picture, 009 every time from a quick load): `sa_draw_begin`'s
  fetched meshes were a function's static, made on the GS's thread after main's `atexit(print_stats)`, so
  destroyed before it while the GS's thread still drew. They are `sa.fetched` now (T4). Native/draw
  e8a89c00's tour in modern: 19 of 64 maps ended so; T4's: none.
- **T4's tour in modern** (64 maps, 8 views each, 1x): its 508 pictures byte for byte those of e8a89c00
  (the CLUT and textures from the table change nothing where the table is exact). The scene's textures:
  900,486 from the table, 11,604 render targets (T3), 495 drawn or copied over (the in-place scroll on
  008, 018 and 097: T3), 49 not uploaded (103's psm 0x10); its CLUTs: 900,295 the table's, 0 the GS's.
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

**What the owner decides for P8:**
- The tie rule's look against the PS2's. Modern draws ties by the slack rule, so seams and layer edges
  can sit a fraction of a pixel from hosted. This is the cost of having no tie-break artefacts.
- The scene's resolution and samples apart from the GS's (T2): whether the HUD and menus stay at the
  GS's scale, and what `--native-scale` means in modern.
- A9 (the reflection's resolution and widescreen projection) when T3 makes it a scene target.
- The sky (decision 3), after T5's pictures of the four maps.
