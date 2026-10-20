# Step 3: the native renderer (plan)

Step 2 draws the game accurately but slowly: the CPU interprets the VU1 programs cycle by cycle, and
paraLLEl-GS emulates the GS. Step 3 replaces both with a renderer of our own that draws the same
pictures on the GPU, and that later carries the opt-in improvements (higher resolution, widescreen
without the 2D squeeze, higher frame rates).

This page is the plan: what the renderer is, what the game asks of it, the order to build it in,
how each stage is checked, and the risks. It is written from four surveys (October 2026): the
game's draw code, a census of GS captures of all 64 maps and of the menus, title, movies and spells,
timings of the step 2 path, and how ico-pc built its renderer.

## Progress (October 2026)

- **Stage 0, mostly done.** `--renderer gs|native|both`, the Debug page's switch and
  `settings.json`; with `both`, the displayed frame buffers compared every vblank (the overlay and
  `--stats`). `kf4-gsreplay --renderer` and `--compare-renderers` (draw by draw, and each vblank).
  Not done: the presenter on Granite's WSI and ImGui on Vulkan (the native GS's picture still goes
  through the CPU to SDL, as paraLLEl-GS's does). Interlace settled: in play and the in-game menus
  draw one 448-line frame (SMODE2 FFMD 0); the title, the memory card screens and the movies draw
  224-line fields, FBP 0 and 0x46 in turn.
- **Stages 1 and 2, working.** The native GS (`port/gs/native/`) draws the whole game from the VU1
  interpreter's packets: VRAM on the CPU in the GS's layout (`gsn_vram.c`, byte for byte against
  paraLLEl-GS for all 13 formats, `tests/gsn_vram_test.c`), render targets on the GPU, textures as
  raw texels looked up and filtered in the shader, Vulkan's blending where the GS equation maps onto
  it and the shader's (from a copy of the target) elsewhere, the feedback passes, DATE, AFAIL,
  MTBA mipmaps, SCANMSK, save states. The 64 map tour captures: no crash, and at the end of each
  (the frame finished) the displayed frame buffer is paraLLEl-GS's exactly, every pixel; single
  draws differ in a few edge pixels that later draws cover (PLAN.md, "Where the native GS
  differs"). Title, memory card, menus, journal, magic and movie captures: within 5 a channel. Live,
  with both renderers through 600 vblanks on eight maps: no crash, frames within a mean of 0.2 a
  channel (a few edge pixels that the frame-to-frame smear carries on). A 2,000-vblank run takes
  8.0 s against paraLLEl-GS's 8.2 s.
- **Stage 3, started**: the VU1 programs, first as C (bit for bit against the interpreter), then as
  shaders.
- Open: the renderers' comparison of field-mode menu buffers (paraLLEl-GS's read-back is stale
  there; the scanouts agree); a draw that samples a target as another format (the scrolling textures,
  T4/T8 over CT32) writes the target back through the CPU once a frame; `--renderer both` aborts in
  Mesa's lavapipe (each renderer alone runs there).

## Why

- **Speed.** The VU1 interpreter is 65-70% of the run's time with the GS on. The busiest maps (000,
  116) run at 70-77 vblanks a second with `--fast` on a Ryzen 9 9900X3D, where 60 are needed: slower
  machines cannot keep up there. VU1 costs 9-18 ms per game frame; paraLLEl-GS's hand-off 2-7 ms.
- **Improvements.** paraLLEl-GS draws the PS2's 640x448. Higher resolution, a true widescreen frame
  and interpolated frames need a renderer that knows what it is drawing.
- **Shipping.** paraLLEl-GS is LGPLv3; a renderer of our own leaves it as an optional reference.

## The one thing we have that ico-pc did not

ico-pc drew through its native renderer from its first commit: it never had an accurate path, and
checks its renderer against its own earlier renders and CPU models. We have step 2. The same packet
stream can go to both renderers in the same process, on the same Vulkan device, and the two
pictures can be compared on the GPU every frame, live, with a key to switch between them. Every
stage below is checked that way. This is the plan's main advantage, and stage 0 builds it first.

## What the game asks for (the survey)

### The draw paths

| Path | What goes through it |
|---|---|
| PATH1 (VU1, `XGKICK`) | all 3D (map records, models, characters, effects), and the in-game 2D: HUD, splash, full-screen passes, register lists (program 0x276 passes a GIF packet through) |
| PATH2 (VIF1 `DIRECT` in the frame's chain) | texture and CLUT uploads, local-to-local copies (scrolling textures, the frame copy), the water ripple model, message text |
| PATH3 (DMA channel 2) | menus, title, movies, boot-time uploads, libgraph's clear and swap sprites |

One VIF1 chain per frame (`Packet_SendChain`, `gfx/packet_buffer.c`), plus the menus' and movies'
channel 2 packets.

### The VU1 programs

Five entry points, compiled into the program (`game/data/vudata.c`), uploaded once at boot. Which one
runs is mostly chosen by the model data (each part's block ends in `MSCAL addr`).

| Entry | Draws | Share (64 maps) | What it does |
|---|---|---|---|
| 0x000 | map records, posed models, items, gold | 86,404 draws, every map | packed 16-bit vertices; transform, perspective ST, fog, three directional lights + ambient, up to four point lights |
| 0x280 | skinned models (enemies, arm, held weapon), the CPU-cut float triangles | 11,557 draws, 49 maps | float vertices, two-matrix rigid skinning, an optional second pass with sphere-mapped ST |
| 0x276 | GIF packets passed straight to the GS | 4,264 | `xtop; xgkick`: no maths |
| 0x5A0 | translucent silhouettes of pool entities | 3,123, 26 maps | transform and fog, constant colour |
| 0x700 | effects (`sfx.dat` models) | 1,253, 54 maps | directional light, per-frame TEX0 substitution for texture animation |

Every program outputs triangle strips (ADC restarts) of ST, RGBAQ, XYZF2. There is no real clipping:
a vertex outside the guard band, or behind the camera, sets ADC and drops its triangles; the CPU cuts
the near-plane triangles of map records itself (`ModelScreen_CutClippedTriangles`). Fixed state sits
at VU qwords 0x00-0x7F (matrices, light matrices, fog at 0x3C, FOGCOL packet 0x3D, point lights
0x40-0x48, tint 0x64-0x66); each batch is double-buffered at 0x80 and 0x240 (`xtop`).

### The GS state

| | What appears |
|---|---|
| Primitives | triangle strips and sprites only (no points, lines or fans); STQ and UV; flat and Gouraud; no AA1 |
| Textures | PSMT8 (56k draws) and PSMT4 (42k) with CT32 CLUTs (CSM1), CT32, CT24 (title), CT16S (the water reflection, sampled as a render target); MODULATE and DECAL, TCC 0/1; bilinear, nearest for 2D, trilinear mipmaps (MTBA) in 30 maps; REPEAT and CLAMP |
| Blending | 11 equations: (Cs-Cd)As+Cd, off, (Cs-Cd)FIX+Cd, Cs+Cd·As (0x280's second pass), Cs·As+Cd, Cs·FIX+Cd, Cd-Cs·As, Cs·FIX, Cs, and two rarer ones; FIX takes about 25 values (a dynamic blend constant); COLCLAMP 1, no dithering |
| Alpha test | GREATER 0 (98k draws), LESS 0 (64), and NEVER with AFAIL as a write mask (colour only, RGB only, Z only) |
| Depth | Z16S, GEQUAL or ALWAYS, with or without writes; GREATER for some 2D |
| DATE / FBA | DATE with FBA for the pool silhouettes (one write per pixel); DATM 1 for 64 HUD draws |
| Contexts | context 1 (100.7k draws) and context 2 (6.0k, via PRMODECONT 0), both on the same buffers |
| Frame buffers | in play: two 640x448 CT32 buffers (TBP 0 and 0x1180), Z at 0x2300; 128x128 CT32 targets for scrolling textures; a 256x256 CT16S reflection (maps 008, 009). Menus and title: 640x224 field buffers with a Z32 buffer over the in-play frame buffer |

**Pipelines.** The 64 maps use 38 distinct Vulkan pipeline states (topology, blend equation, depth
test and write, colour mask with AFAIL folded in, DATE, target format); with the menus, title,
movies and spells, 47. About 15 cover 99% of the draws. Fragment shader variants: 25 (36 with the
extras), switching on texture format, TFX, TCC, fog and alpha test. All can be created at start-up.

### The hard parts

1. **Textures are uploaded as raw VRAM.** 1.5-7 MB of IMAGE uploads a frame (median 3.2 MB, about 54
   uploads), mostly CT32 blocks that are then sampled as PSMT4 and PSMT8: the GS's swizzled memory
   layout is part of the data. The renderer needs a model of VRAM (4 MB, with the GS's block and
   page swizzles) and a texture cache that decodes a TBP/PSM/CLUT on first use and is invalidated by
   uploads that overlap it. CLUT loads happen about once per draw (1,500 a frame) but there are few
   distinct CLUTs.
2. **The frame is read back as a texture, every frame.** About 40 draws a frame sample the buffer
   being drawn: `Game_BlendScreenOffsetDown` (on by default), `Game_ShiftScreenByViewTurn` (on by
   default), `Game_ApplyBrightness` (every frame), `Game_DrawHazeLayers`, the under-water tint;
   `Game_BlendPreviousFrame` reads the other buffer; `GsDraw_CopyFrame` copies the frame to 0x2BC0
   for the screen wobble and the map-change capture; `GsDraw_CopyFrameToTexture` makes a 256x256
   screen copy for a refracting entity; menus copy the game frame in as their backdrop. Each needs the
   colour target copied (or ping-ponged) before it reads. ico-pc found that float blending drifts
   when a pass feeds back on itself frame after frame; it runs those passes in integer arithmetic.
3. **Render targets alias textures.** The scroll targets and the reflection are drawn into and then
   sampled at the same TBP; menus reuse in-play VRAM for other buffers.
4. **Rare blends that fixed-function blending cannot do:** As or FIX above 0x80 (2.0), the
   destination-alpha mode, DATE. Each is a copy of the region and a blend in the shader; the census
   says they are a few draws a frame.
5. **The VU1 programs are software-pipelined** and rely on result latencies (step 2's interpreter is
   cycle-accurate for that reason). Shaders compute what each vertex should come out as, not how the
   pipeline gets there; the interpreter is the oracle that says what that is.

Not needed: per-frame readbacks (the one `sceGsExecStoreImage` runs once at boot), points, lines,
dithering, TEXA (never written), REGION clamp modes.

## Design

### Where it hooks: the packet stream

The renderer takes the same packets step 2 does, from `port/platform/dmac.c` and `vif1.c`: the VIF1
chain (UNPACKs, MSCALs, DIRECT), and PATH3. The game is not patched for it. That is what lets both
renderers run on the same frame, keeps every effect the game builds from disc data working without
touching it, and keeps save states and the GS frame capture (`.kf4gs`) valid: a capture replays
through either renderer.

ico-pc hooked one level higher (its packet builders), which suited ICO; KF4's model and texture
packets are prebuilt in the disc data, so the stream is the natural place.

### The parts

| Part | Does |
|---|---|
| GS state decoder | follows the GIF stream (PACKED, REGLIST, IMAGE) and keeps the GS registers, as `port/platform/gs.c` half does today; turns each primitive run into a draw record |
| VRAM model | the 4 MB GS memory in GS layout, on the CPU and the GPU: IMAGE uploads, local-to-local copies; the swizzle code can follow paraLLEl-GS's or PCSX2's |
| Texture cache | (TBP, TBW, PSM, CLUT, CPSM, CSA, size, mip levels) to a decoded GPU texture, invalidated by page when VRAM under it changes; palette textures decoded with their CLUT, or as index textures with the CLUT looked up in the shader |
| Targets | named render targets for the GS buffers the game uses (the two frame buffers, Z, the scroll targets, the reflection, the menu field buffers), each mapped from its FBP; anything else as a temporary target; a draw that samples its own target samples a copy taken just before it |
| VU1 shaders | one vertex shader per program (0x000, 0x280 with its second pass, 0x5A0, 0x700), reading the batch's UNPACKed data and the fixed VU block as constants; 0x276's packets go to the decoder |
| Pipelines | the reachable set from the census, created at start-up; GS blend, alpha test, depth and AFAIL mapped as ico-pc's `rd_state` does; the few blends fixed-function cannot do through a copy and the shader |
| Presenter | the window's swapchain through Granite's WSI on an SDL Vulkan surface (no more CPU readback into an SDL texture); the scanout of the displayed buffer with the step 2 switches (deinterlace, progressive...); the ImGui overlay on Vulkan |

### The API: Vulkan, through Granite

paraLLEl-GS already runs on Granite's Vulkan layer (MIT), which comes with volk, glslang and
RenderDoc capture support. The native renderer uses the same `Vulkan::Device`:
- the two renderers share textures and targets, so the comparison is a GPU image diff, no readback;
- no new dependency; shaders in GLSL, compiled to SPIR-V at build time as paraLLEl-GS's are;
- Windows and Linux alike (llvm-mingw clang and gcc build Granite today).

Fallback if Vulkan through Granite turns out painful: SDL3's GPU API (Vulkan/D3D12/Metal; no
dual-source blend, which the design does not need). The move from SDL2 to SDL3 (about a day: five
files) is worth doing on its own and is separate from this.

Depth: GS Z (16 bits in play, 32 in the menus) fits a D32_SFLOAT target as z/2^24 exactly, so
GEQUAL stays GEQUAL.

### Interlace

The code reads: in play, one 448-line frame drawn at 30 Hz into frame-mode buffers; the title, menus
and movies draw each field as its own 224-line picture (FFMD 1, `sceGsSetHalfOffset`). PLAN.md says
in-play is two fields; stage 0 settles which, from captures. The native renderer draws the 448-line
picture (or more) and weaves menu fields, as step 2's default switches do.

## The stages

Each stage ends with something runnable, checked against step 2 frame by frame. Sizes are rough,
for one person working steadily with agents; they will move.

| Stage | Delivers | Checked by | Size |
|---|---|---|---|
| **0. Foundations** | `--renderer gs\|native\|both` and an overlay switch; the presenter on Granite's WSI (drops the CPU readback, so step 2 gets faster too); ImGui on Vulkan; the comparison: a per-frame GPU diff of the two renderers (max and mean difference, a heat map in the overlay), and `kf4-gsreplay --native` / `--compare-renderers` on captures; the interlace question settled | the existing tests; frame_test unchanged | 1-2 weeks |
| **1. Native GS, 2D** | the decoder, the VRAM model, the texture cache and the pipelines for sprites and strips; PATH3 and 0x276 drawn natively: title, menus, journal, HUD, message text, movies | the diff on the title, every menu page, a movie and the HUD: within 1 per channel | 2-3 weeks |
| **2. Native GS, 3D from the interpreter** | the VU1 interpreter still runs on the CPU, and its GIF output is drawn natively: the whole game on the native GS. Fog, mipmaps, contexts, DATE/FBA, the frame copies and feedback passes (integer arithmetic where they feed back), the scroll targets, the reflection, the wobble | the diff over the 64 map tour views and the step 2 checklist's spots; feedback passes exact over 600 frames | 3-5 weeks |
| **3. VU1 on the GPU** | the programs as vertex shaders, 0x000 first (86% of draws), then 0x280, 0x5A0, 0x700; the interpreter stays as a test mode | per program: shader output against the interpreter's (X/Y within 1/16 pixel, Z and ST within a small relative error, colour within 1, ADC exact); then the picture diff | 3-5 weeks |
| **4. Default and speed** | batching, pipeline cache, frame pacing; native becomes the default; paraLLEl-GS stays behind `--renderer gs` for comparison (and its LGPL question is settled before release builds ship it) | timings: the busiest maps at full speed with room to spare | 1-2 weeks |
| **5. Improvements, opt-in** | resolution scale (scene targets scaled, work buffers kept at PS2 size), a native widescreen frame instead of the 2D squeeze, MSAA, higher frame rates with interpolation | each off by default; with all off, the picture equals stage 4's | open-ended |

In all, roughly 3 to 4 months to a native default (stages 0-4), then improvements as wanted. ico-pc's
renderer grew to about 155,000 lines with a long tail of driver fixes; KF4 needs less (fewer GS
states, five small programs, no context tricks beyond PRMODECONT), and we can borrow from it.

Stage 2 before stage 3 is deliberate: with the CPU's VU1 output drawn natively, every difference in
the picture is the native GS's fault; once that is clean, every difference after stage 3 is the
shaders'.

## Testing

- **The live diff** (stage 0): both renderers on, the difference per frame in the overlay and the
  log; `--compare-renderers` in headless runs, failing above a threshold.
- **Captures**: `kf4-gsreplay` replays a `.kf4gs` through either renderer, with the draw-by-draw
  bisecting it has today (`--draws`, `--pixel`); the map tour runs both renderers and reports the
  difference per map.
- **Per program**: the VU1 interpreter is the oracle for the shaders, vertex by vertex
  (`tests/vu1_test.c` grows a GPU mode, as ico-pc's `VU_F_PROBE`).
- **Exact where it matters**: feedback passes and the scroll targets must match exactly, or they
  drift over frames; single passes may differ by 1 per channel (fixed-function blending), recorded as
  divergences.
- **Divergences table**, as ico-pc keeps: everything the native renderer knowingly does differently,
  with its switch, in PLAN.md beside the step 2 table.
- CI: the decoder, VRAM model and texture decoding are CPU code with unit tests; GPU tests run on a
  software Vulkan driver (lavapipe) on Linux, as ico-pc does.

## What we can borrow

ico-pc (MIT): the GS integer arithmetic headers (`gs_math.hlsli`, `fx_sprite.hlsl`, `blend_int.hlsl`;
HLSL, to port to GLSL), its GS state to pipeline mapping (`rd_state.h`, `rd_pipeline.c`), the
texture/CLUT decoder (`rd_tex.c`), the frame dump and replay tool pattern, `check_spirv.py` (a safe
SPIR-V opcode set that avoids driver compiler crashes), and its lessons: depth that grows with GS Z,
dual-source factors clamping at 1.0 on UNORM targets, NaN UVs, pipelines precreated with a disk
cache. paraLLEl-GS and PCSX2 for the swizzle tables (mind their licences: paraLLEl-GS LGPLv3, PCSX2
GPLv3: read for understanding, write our own).

## Risks

| Risk | Answer |
|---|---|
| VRAM aliasing and swizzles get texture decoding subtly wrong | the CPU VRAM model is unit-tested against paraLLEl-GS's VRAM on captures, block by block |
| Feedback passes drift | integer arithmetic for them, exact over 600 frames |
| The menus' VRAM layout overlaps the in-play buffers | targets keyed by (FBP, PSM, mode); the decoder resets them when the game switches modes |
| Texture upload cost (3+ MB a frame) | the cache only decodes what changed; most uploads re-send the same bytes, which a hash per upload can skip |
| `func_00112BD0` (map culling, fog and light blending) is the decomp's one non-matching function | it feeds the renderer through VU memory, so both renderers see the same input; a mismatch there shows in step 2 too |
| Driver differences | Granite's validation layers, RenderDoc, lavapipe in CI, ico-pc's shader envelope |
| Granite has no upstream support | pinned (it already is), and only its `vulkan/` layer is used |

## Before stage 0

- Settle the interlace question (in-play frame or fields) from captures, and fix PLAN.md.
- Agree on stage 0's comparison numbers (what difference fails a run).
- Decide whether the SDL3 move comes first (it would also change the presenter work).
