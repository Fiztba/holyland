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

- **Stage 0, done.** `--renderer gs|native|both`, the Debug page's switch and
  `settings.json`; with `both`, the displayed frame buffers compared every vblank (the overlay and
  `--stats`), and shown as a heat map (the renderer section's "Show where they differ",
  `--renderer-shown heatmap`; computed from the pixels the comparison reads on the CPU).
  `kf4-gsreplay --renderer` and `--compare-renderers` (draw by draw, and each vblank). The presenter
  on Vulkan, opt-in: `--presenter vulkan` (the Debug page, `settings.json` `display.presenter`)
  makes the window's swapchain (through SDL_Vulkan_CreateSurface, not Granite's WSI) on the native
  GS's device; the native GS copies the displayed buffer on the GPU into an image (field mode woven
  there) which is blitted into the window, and the overlay and the mouse indicator are drawn with
  ImGui's Vulkan backend (`port/platform/present_vk.cpp`). No read-back: 2,000 vblanks windowed at
  4x went from 16-22 s to 8.1-8.2 s, at 1x from 7.6 s to 7.2-7.6 s. paraLLEl-GS keeps
  SDL_Renderer (`--renderer gs` falls back to it); with `both` the paraLLEl-GS picture is uploaded.
  The Vulkan presenter is the default since stage 4 (below). Interlace settled: in play and the in-game menus
  draw one 448-line frame (SMODE2 FFMD 0); the title, the memory card screens and the movies draw
  224-line fields, FBP 0 and 0x46 in turn.
- **Stages 1 and 2, working.** The native GS (`port/gs/native/`) draws the whole game from the VU1
  interpreter's packets: VRAM on the CPU in the GS's layout (`gsn_vram.c`, byte for byte against
  paraLLEl-GS for all 13 formats, `tests/gsn_vram_test.c`), render targets on the GPU, textures as
  raw texels looked up and filtered in the shader, Vulkan's blending where the GS equation maps onto
  it and the shader's (from a copy of the target) elsewhere, the feedback passes, DATE, AFAIL,
  MTBA mipmaps, SCANMSK, save states. `tests/gsn_draw_test.c` draws random batches (every primitive,
  texture and frame buffer format, CLUT, blend, alpha and Z test, DATE, FBMSK, SCANMSK, fog) on both
  renderers and compares their VRAM: 300 cases a run pass, and paraLLEl-GS's own quirks the game
  does not reach are listed in PLAN.md; then as many rounds of buffers that share memory (128-wide
  buffers page rows apart drawn into, uploaded into in their shape and in others, copied and read
  as textures, as the scrolling textures do), which must leave VRAM the same byte for byte. The 64 map tour captures, each frame drawn from
  paraLLEl-GS's state: no crash; on average 2% of the frame's pixels are more than 1 a channel
  from paraLLEl-GS's (edges, alpha-test cut-offs, flames, 3D blending rounded by Vulkan; worst 118);
  PLAN.md, "Where the native GS differs". Title, memory card, menus, journal, magic and movie
  captures: within 5 a channel. Live, with both renderers through 600 vblanks on all 64 maps: no
  crash; at the end typically 2-3% of pixels more than 1 apart (mean 0.1 a channel). The passes
  that carry a frame into the next (the dimming each frame, the previous frame blended in, the
  smear) blend in the shader exactly, so they do not drift. Uploads seen before come back as page
  copies instead of being swizzled again.
- **Stage 5, started**: `--native-scale 1-4` (the render targets at up to 4 times the PS2's
  resolution, live from the overlay). `--native-widescreen`: with `--widescreen`, the targets are
  drawn 4/3 of that across (rounded up: 2, 3, 4, 6 for 1x to 4x), so the 16:9 picture has square
  pixels instead of 640 columns stretched; the native GS keeps a scale per axis, and nothing else
  changes (the 2D squeeze and the GS's coordinates stay `--widescreen`'s). Live from the overlay.
  `--native-msaa 2|4|8`: the 3D (every draw with a Z buffer) drawn into multisampled images, resolved
  wherever the target is read (textures, the shader's blending, write-backs, frame copies, the
  scanout), the 2D drawn with Z snapped to whole pixels so the menus and movies are unchanged; the
  alpha test's cut-outs and draws without Z stay as they are. Combines with the two above; 2% (4x)
  to 5% (8x) of a run's time at 1x, 6% to 12% at `--native-scale 4`. PLAN.md's opt-in table.
  Above 1x, the 2D (rectangles of one colour and Z) covers whole GS pixels and nearest reads take the
  GS's texel at the GS pixel's integer point (the sky's seams on map 000, the line down the screen's left
  edge); `--native-lod` (default `scale`) reads mipmaps a level finer by log2 of the scale.
  `--frame-rate 60`: a picture at every vblank in play, the camera interpolated (below, "Higher
  frame rates").
- **Stage 3, done as C.** Every VU1 program the game runs (0x000, 0x280, 0x5A0, 0x700; 0x276 passes
  packets through) has a high-level C version that gives the interpreter's results bit for bit
  (`--vu1 hle`; `--vu1 check` runs both and compares them run by run: 0 differences over all 78
  captures and live runs on several maps). That was the stage's purpose: VU1 goes from two thirds
  of the run's time to a sixth. Shaders are left as an option (below).
- **Speed** (2,000 vblanks, new game then map 000, `--fast --headless`, Ryzen 9 9900X3D):
  paraLLEl-GS with the interpreter 6.4 s (main today); paraLLEl-GS with VU1 in C 3.3-3.6 s; the
  native GS with VU1 in C 2.0 s (4.4 s before stage 4's speed work); no GS at all 1.5 s. Maps 004,
  029, 112 and 116 the same way: 1.9-2.1 s, paraLLEl-GS 3.3-3.5 s. With a picture for the window
  every vblank (the window's path made to run in a headless build): 2.2 s, paraLLEl-GS 8.8 s.
  What stage 4's speed work did (feat/gsn-speed):
  - the native GS has a thread of its own: the DMA controller's thread copies the GIF packets and
    vblanks into a ring and goes on with VU1; whatever wants something back (the FIFO read-back, a
    dumped picture, a save state, kf4-gsreplay) waits for the ring to empty; a picture for the
    window is handed to it by the GS's thread (`GSN_PRESENT`), so nothing waits for that either
    (`KF4_GSN_THREAD=0`: no thread);
  - four frame contexts, where two left a vblank waiting for the GPU's work of the one before;
  - targets keep which pages are newer than VRAM (the rows their draws reach): a write-back writes
    only those, a texture or CLUT read of the others needs none; a buffer whole page rows above a
    target, in its layout, makes the target grow upwards; uploads into a target at such an offset
    go into it, and an upload that writes whole pages of a target in another shape keeps the rest.
    Map 000's scrolling textures no longer go back through the CPU every frame (713 write-backs in
    the run to 133, those left at the title, the menus and map 008's reflection);
  - uploads: a hash four lanes wide, no copy of an upload that comes whole, 24-bit uploads (the
    title's) found again as pages; VRAM read and written a row at a time, each format's swizzle
    and row loop made on its own;
  - a Vulkan pipeline cache in the data folder (`native_gs_pipelines.bin`): where the driver keeps
    none (a new build, its own cache full) the first minutes made pipelines for 1.6 s;
  - VU1's code aligned to 64 bytes (`-falign-functions=64 -falign-loops=64` for vu1*.c): its speed
    moved by a third with where the linker put it.
- **Stage 4, done pending a play-through.** The defaults: the native GS (`--renderer native`), the
  Vulkan presenter (`--presenter vulkan`) and the VU1 programs in C (`--vu1 hle`; bit for bit, so no
  switch to keep the PS2's behaviour is needed). paraLLEl-GS stays behind `--renderer gs` (and
  `both`) for comparison; it draws instead when the native GS cannot start (no Vulkan device it can
  use), and with it alone the window is SDL_Renderer's. `settings.json` has a `"version"` (2): older
  files wrote `"gs"` and `"sdl"` out whether chosen or not, so in them those two are read as the old
  defaults (README.md, "settings.json"). `frame_test.py` and `map_tour.py` ask for `--renderer gs`;
  `frame_test.py --renderer native` passes too (the native GS's pictures the same every run). CI
  builds and runs the unit tests only, none of which depends on the default.
  Frame pacing, measured windowed at 59.94 Hz pacing over 2,300 vblanks (new game, then map 000 from
  vblank 1900; a 144 Hz monitor, vsync off; per-present timestamps in a test build):
  - the vblank wake-up: Windows' Sleep in whole milliseconds woke it 0 to 2.5 ms late (presents a
    deviation of 0.60 ms apart); a high-resolution waitable timer wakes it 0.3 ms late (p99 0.5 ms),
    0.22 ms deviation (`port_sleep`, `host_time.c`; Linux's nanosleep was already exact);
  - the picture of a vblank reaches the window from the renderer's thread about when the window
    wakes for it (on map 000: the native GS 9 ms after the vblank before, paraLLEl-GS 14-22 ms); a
    late one was shown at the next vblank, the one before twice and one dropped. In a run: the native
    GS with the Vulkan presenter 1, with SDL_Renderer 3-93, paraLLEl-GS 7-77 (it varies run to run).
    The window now waits for it up to half a vblank while pictures are coming (not with `--fast`):
    0, 1 and 0, with 0-12 presents of 2,300 waiting more than 1.5 ms;
  - pipeline compiles: no present more than 23.7 ms after the one before, with the native GS's cache
    new and NVIDIA's shader cache off (`__GL_SHADER_DISK_CACHE=0`) or with both;
  - with vsync, presents are as even (0.61 ms deviation; FIFO with 3 images never waited at
    144 Hz), but 59.94 pictures a second on a 144 Hz screen are shown 2 and 3 refreshes in turn: only
    a 60 Hz (or variable-refresh) screen shows them evenly.
  Left for the owner: the play-through; paraLLEl-GS's LGPL question before release builds ship it
  (`-DKF4_USE_PARALLEL_GS=OFF` leaves it out today only by taking the native GS with it: the two
  are one library, `kf4_gs`, on the Granite that comes with paraLLEl-GS; a build of the native GS
  alone needs them split).
- Open: the renderers' comparison of field-mode menu buffers (paraLLEl-GS's read-back is stale
  there; the scanouts agree); write-backs still left: the title's 640x224 field buffer (61 in the
  run), the menus' Z buffer over the play's frame buffers, and map 008's reflection drawn with the
  main Z buffer at 256 wide, which sends the main Z back to VRAM; at the title and in the menus each
  field is read back for the weave, wanted or not;
  the Vulkan instance and device take a fifth of a second to make at start-up, waited for by the
  boot's read-back;
  `--renderer both` on Mesa's lavapipe needs `LP_NUM_THREADS=1`.

### What could come next

- **Exact blending everywhere.** The one difference left by design is Vulkan's rounding of
  (Cs - Cd) C + Cd on 3D geometry (1 a channel, drawn again each frame). Fragment shader interlock
  (VK_EXT_fragment_shader_interlock) would let the shader read and write the target in primitive
  order: every blend exact, and DATE per pixel. Granite does not turn the feature on; the native GS
  would need its own device set-up for it.
- **Uploads on the GPU.** The swizzle runs on the CPU (and is mostly skipped now: uploads seen before
  are copied in as pages). A compute pass writing a GPU copy of VRAM, with textures read from it,
  would take the rest off the CPU and remove the write-backs.
- **paraLLEl-GS's picture on the Vulkan presenter.** The Vulkan presenter is the default (stage 4,
  Progress above), for the native GS. paraLLEl-GS's picture could go the same way
  (uploaded, or its image on a shared device) so that `--renderer gs` needs no SDL_Renderer either.
- **VU1 on the GPU.** The C programs (stage 3) take VU1 from two thirds of the run's time to a
  sixth; shaders would take the rest, at the cost of a second implementation to keep bit for bit.

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
GEQUAL stays GEQUAL. Each pixel's Z is rounded to a whole Z as the GS rounds it (`gs_zclamp.frag`
writes the depth for every draw with a Z buffer): with the fraction the GPU's interpolation keeps, a
draw over the same plane as the one before (the lava's glow, ZTST GEQUAL) came out a hair behind it
in places, which showed the lava through in dark specks (map tour: `end-other` 366,070 to 362,601
pixels, the worst difference 118 to 53). ZTE 0 (undefined on the GS) neither tests nor writes the Z buffer, as
paraLLEl-GS has it: the movies draw their fields with ZTE 0 and ZBUF over the movie's own picture
(0x1180), and writing Z there put black and cyan bands over the opening movie.

### Interlace

The code reads: in play, one 448-line frame drawn at 30 Hz into frame-mode buffers; the title, menus
and movies draw each field as its own 224-line picture (FFMD 1, `sceGsSetHalfOffset`). PLAN.md says
in-play is two fields; stage 0 settles which, from captures. The native renderer draws the 448-line
picture (or more) and weaves menu fields, as step 2's default switches do.

### The scanout

What the display registers say, and what `vsync_now` (port/gs/native/gsn.cpp) makes of them. The game
only ever enables read circuit 2 (PMODE 0x66, `sceGsPutDispEnv`): DISPFB2 at FBP 0 or 0x8C (0x1180
blocks), 640 wide, DISPLAY2 DX 636, DY 50, MAGH 3, 448 lines. SMODE2 is 3 (field mode) for the title,
menus and movies, 1 (frame mode) in play.
- **Field mode**: each vblank's buffer is one field's 224-line picture, woven into the lines of its
  parity with the field before (`--no-deinterlace`: the field alone).
- **Frame mode**: the 448 lines read whole, except for 4 vblanks after a primitive drawn with SCANMSK
  over more than 400 lines (`scanmsk_seen`), or with `--no-progressive`: then the picture is read a
  field at a time, the lines of parity (DY ^ field) & 1, as the PS2 shows them, because the game
  drew its two fields as two pictures. With `--deinterlace` the field is deinterlaced
  (`shaders/deinterlace.frag`, from the last four fields: the other field's lines woven from the
  vblank before where the picture holds still, this field's lines interpolated where it moves, as
  paraLLEl-GS's FastMAD does); without, the field alone (224 lines). paraLLEl-GS does the same with
  its `force_progressive`: the SCANMSK sign is its own (`has_complex_scanmsk_timeout`).
- The one such draw in play is `Game_ShiftScreenByViewTurn`, which the port leaves out by default
  (`--no-whole-frame`, PLAN.md): the game then draws whole frames, read whole by both renderers.
  With it drawn, both renderers show the same deinterlaced picture.
- The movies, in field mode, draw each field from the decoded picture's every other line, so the
  weave mixes two pictures whenever the picture changes; with whole frames on the movie player shows
  its pictures whole in frame mode instead (PLAN.md). The menus draw a new field every vblank (field
  mode, a half-line draw offset): woven, two moments; with whole frames on, each field's packets are
  drawn again into the other buffer for the other field and scanned out as that field first, so the
  weave is one moment (`port/platform/field_frames.c`, PLAN.md).
- The history (the countdown and the fields) is in a GS capture (`KF4GS_GS_HISTORY`, which
  paraLLEl-GS's save states and captures carry too), so a replay starts from the game's. The native
  GS's save states do not keep it: the first picture after a load in that state takes its field's
  lines twice.
- `kf4-gsreplay --compare-renderers` compares the two renderers' scanouts at each vblank, besides the
  frame buffers; the comb this replaced was in the scanout only.

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
| **5. Improvements, opt-in** | resolution scale (scene targets scaled, work buffers kept at PS2 size), a native widescreen frame instead of the 2D squeeze (done as square pixels across: `--native-widescreen`), MSAA (done for the 3D: `--native-msaa`), higher frame rates with interpolation | each off by default; with all off, the picture equals stage 4's | open-ended |

In all, roughly 3 to 4 months to a native default (stages 0-4), then improvements as wanted. ico-pc's
renderer grew to about 155,000 lines with a long tail of driver fixes; KF4 needs less (fewer GS
states, five small programs, no context tricks beyond PRMODECONT), and we can borrow from it.

Stage 2 before stage 3 is deliberate: with the CPU's VU1 output drawn natively, every difference in
the picture is the native GS's fault; once that is clean, every difference after stage 3 is the
shaders'.

### The VU1 programs without the interpreter (`--vu1 hle`)

Ahead of stage 3, program 0x000 has a CPU version that computes its results directly, a vertex at a
time, instead of interpreting its 289 software-pipelined instruction pairs:
`port/platform/vu1_hle.c` (its top comment describes the program). It is the specification of the
0x000 shader, and a speed switch now: its results are the interpreter's to the bit, so it is not a
divergence. `--vu1 hle` is the default; `--vu1 interp` keeps the interpreter; `--vu1 check` runs both and compares
them run by run.

- **Bit for bit.** Every FMAC operation is the interpreter's own helper (`v4_op`, shared through
  `port/platform/vu1_internal.h`) in the program's order, ACC and the MADD roundings included. On x86
  a vertex is first tried in plain SSE floats with flush-to-zero, which gives the same bits when no
  operand has exponent 255 and MXCSR shows no overflow, underflow or invalid operation; otherwise
  (about 0.5% of the vertices) it is done again with the interpreter's helpers.
- **Timing.** Only Q, P and the clip flag are read without a stall, and the program's timing is
  static: every DIV, ESADD and CLIP result is due by its reader, except the FCAND at 0x07E, which sees
  one of the two CLIPs before it. The cycles and instruction pairs of a run are a formula in the
  vertex count and the number of point lights.
- **What it leaves.** The interpreter can take any run after an HLE run: data memory (including what
  the pipeline writes past the batch: TOP+0xC1..0xC7, two vertices past the end in the light passes),
  the XGKICK packets and their order with the stores, every VF and VI register, ACC, Q, P, I, the MAC,
  status and clip flags (sticky bits too), VU1's clock and MSCNT's continue address are all as the
  interpreter leaves them. Not reproduced: the interpreter's scratch that it clears or rewrites before
  reading (the flag-write queue, empty after a run, and its head; the VI-write record for branches,
  cleared at each run's start; Q's and P's due cycles with nothing pending). A save state taken after an
  HLE run therefore differs from the interpreter's in those bytes only.
- **Left to the interpreter.** Batches the game does not make (fewer than 3 or more than 39
  vertices, or not between 0x80 and the end of data memory), a run starting with anything in flight,
  micro memory with a flag-reading instruction in it, `--vu-round nearest`; and every other program.
  Micro memory is recognised by a hash of 0x000-0x120 when it is decoded.
- **Checked.** `kf4-gsreplay --mode vif --vu1 check` on the 64 map tour captures and the 14 census
  captures (title, menus, journal, magic, movies): 90,798 runs, every one the same as the
  interpreter's (memory, packets, registers, flags, clock), with the fast path and with it switched
  off; `--vu1 hle`: every packet as recorded. In the game (`tests/newgame.pad`, teleported to map 0,
  2010 vblanks, `--vu1 check`): 499,185 runs, none different; with `--vu1 hle`, the per-frame log,
  the dumped frame, `--stats`' VU1 runs, instructions and cycles and PATH1 hash are those of
  `--vu1 interp`.
- **Speed** (that run, `--fast --headless --renderer native`, Ryzen 9 9900X3D): VU1 3.3 s with the
  interpreter, 1.14 s with `--vu1 hle` (0.59 s of it program 0x000's 499,185 runs: 4.7 times faster
  than interpreted); the run's wall time 7.9 s to 5.4 s.
- **Program 0x5A0** (the silhouettes) has its own: `port/platform/vu1_hle_5a0.c`, the same way
  (exact helpers only, no SSE fast path), MSCAL 0x5A0 and MSCNT 0x640, n = 3..49, recognised by a
  hash of 0x5A0-0x641; vu1.c's `hle_try` hands its runs on. Checked: the 78 captures, 94,086 runs
  (3,288 of them 0x5A0's), none different; in the game on maps 035, 112 and 115 (2500 frames,
  `--vu1 check`) none different, and `--vu1 hle`'s PATH1 hash is `--vu1 interp`'s. On map 035 its
  117,384 runs take VU1 from 2.94 s to 2.62 s with `--vu1 hle`.

### VU1 program 0x700 without the interpreter

Program 0x700 draws the effects (sfx.dat models: fire, glows, magic) and has its HLE in
`port/platform/vu1_hle_700.c` (its top comment describes the program), reached through
vu1.c's `hle_try` and recognised by a hash of 0x700-0x7D1. It is not pipelined: a vertex a loop pass,
directional light, one colour for the model (VU 0x64), and the texture animation (TEX0 replaced by
VU 0x66 when its TBP0 is 0x65.w). What it leaves past the batch is reproduced: a "vertex -1" from the
raw qword at the previous run's vi07 + 1, and an extra vertex n (the loop's IBGTZ reads its counter
before the decrement, so it runs n + 1 times). Its timing is a formula in n and its two branches.
Checked as 0x000: the 78 captures (2,423 runs of 0x700, both entries, both branches each) and the
game on maps 009, 025 and 000 with `--vu1 check`, none different.

Program 0x280 (skinned models: two matrix sets chosen per vertex by ST's w, float vertices, a colour
scale and bias, point lights in the vertex's set, and the optional sphere-mapped second pass gated
by TOP+0xA4.z) has its own, `port/platform/vu1_hle_280.c`, to the same bar: its top comment
describes the program, including where its pipeline shows (the colour's xyz carried from vertex to
vertex, vertex 0 taking the tag's w as its light set, the run ending after the A+D kick when every
vertex is clipped). The arithmetic is shared through `port/platform/vu1_hle_ops.h`. Checked: the
21,390 runs of 0x280 in the 78 captures (90,798 + 21,390 = 112,188 compared, none different, with and
without the fast path) and 160,207 in the game run above (659,392 compared with 0x000's). With both,
that run's VU1 time is 0.76 s (from 3.3 s), its wall time 5.2 s.

### Higher frame rates (`--frame-rate 60`)

Stage 5's "higher frame rates, with interpolation while the game itself keeps its 30 Hz". A first
version is in: `--frame-rate 60` (the overlay's Display page, `settings.json` `display.frame_rate`),
off by default. `port/platform/frame_interp.c`'s top comment has the details.

**What the game does.**
- In play, `GameLoop_Run`'s loop runs once every two vblanks. `Display_SwapBuffers` waits until
  `sceGsSyncV` returns field 1, so a pass that is done early waits for the second vblank; one that runs
  past 0x4B0C7 timer ticks swaps without waiting (`allow_late_swap`), which the PC does not reach.
- The swap shows the buffer drawn last time and sets the other to be drawn: libgraph's draw environment
  by PATH3, with a clear of colour and Z (`sceGsSetDefDBuff(..., 1)`), for both contexts.
- `Packet_SendChain` then sends the chain the previous pass built. It draws the whole frame: the world,
  the screen passes, the arm, the HUD. Then the game moves on by one frame (input, scripts, player,
  entities, camera: `GameLoop_UpdateCamera`, `GameLoop_SetCameraMatrices`) and builds the next chain
  (`GameLoop_DrawFrame`).
- The display registers a vblank scans out are those it found, before the game's swap after it. So
  the chain drawn after vblank 2k - 1 is shown at 2k + 2 and 2k + 3. A frame's camera is that of the
  pass before the one that sends it. From the pad to the screen: about 6 vblanks, most of it the
  game's own pipeline.
- Evidence: pictures dumped in play change at even vblanks only. The GIF packets between two vblanks
  hold a whole frame at even vblanks and nothing at odd ones.
- The menus, the title, the memory card screens and the movies have loops of their own. They draw
  fields (FFMD 1), and no world.

**What reaches VU1.**
- `GsDraw_QueueVu1Matrix(world_screen_matrix)` puts the camera's world-to-screen matrix at VU 0x00-0x03,
  where program 0x000 multiplies it with each part's model matrix (VU 0x04-0x07).
- Many other draws get matrices the CPU has already multiplied with `world_screen_matrix`
  (`Matrix_Multiply(model, world_screen_matrix, ...)` in model/, map/, player/; 0x280's skinned models,
  0x700's effects).
- The CPU also projects points itself (`Vu0_ProjectToScreen`: the turn smear) and cuts the near
  triangles of map records.
- So there is no single matrix to swap. Re-running the chain with another camera would mean finding
  every matrix in it, and it would still miss what the CPU worked out.

**The choice.** The changes are made after VU1, in the GIF packets, at the GS front end:
- Every vertex of the world reaches the GS as a screen position, a Z and (textured) Q = 1/w. That is
  enough to undo the projection and do it again from another camera: back to the camera's space, the
  camera's change, projected again, S and T scaled for the new Q, and Z as cz + k / w with the
  vertex's own k, (Z - cz) w. For the world k is the projection's az; program 0x700's flames and
  lights' glows have about 16 times that, which pulls them in front of what is near them. (Until
  October 2026 the replay moved every Z by the world's az alone: a glow's Z moved a 16th of the way,
  so in the pictures between the glows came through the walls before them, and walking toward the
  wall lights of map 000 made them flicker at 30 Hz. Checked on that walk: the pictures between now
  lie between the frames on each side in the glow's bright pixels, where they were 1,000 to 2,000
  above both, with either renderer, MSAA 1 or 2, widescreen or not.)
- It does not care which program or which matrix made the vertex. It costs no VU1 run.
- The game tells, by marks in its chain, where the world is and with which camera.
- The other way, the game drawing twice a pass, would run the draw code's side effects twice (the wobble
  sines, the message dimming, effects' state, `rand`), and the game would no longer be the PS2's.

**How it goes.** At vblank 2k:
- The packets of the last vblank (the clear and the whole chain) drew the world into the buffer not
  shown, so they are kept.
- At 2k + 1 they are sent again: the frame into the buffer shown, the world's vertices from the camera
  half way between the frame shown and the one drawn.
- The frame drawn is shown at 2k + 2 as before.
- The camera half way: the rotation by quaternion slerp, the position half way. A turn of more than
  0.8 radians or a move of more than 400 units is a cut: not interpolated.
- With the native GS on its own thread, the replay's packets go into its ring like the game's, ahead
  of the vblank's scanout, so its thread draws the picture between and hands it to the window as
  that vblank's (`GSN_PRESENT`); MSAA targets are resolved by the same draws.
- The window shows the pictures in the order they come, up to two waiting
  (`port/platform/video.c`): with every vblank's picture its own, one that the GS's thread hands over
  just after a vblank's present is shown at the next vblank instead of being replaced.
- Nothing of it is left behind:
  - the next clear wipes the buffer shown;
  - the other targets are drawn again from the same packets;
  - uploads go again in their order;
  - local-to-local copies into anything but the frame, and local-to-host transfers, are not started;
  - FRAME, TEX0 and BITBLTBUF are written back to what the game last wrote.

**Checked.**
- With the switch on, the game's own pictures (the even vblanks) are byte for byte those of a build
  without the change. Native GS, in play on maps 099, 000, 008, 009, 035 and 112, and through the title,
  the opening, a teleport and the menus; with `--widescreen` too, and with `--native-msaa 4`.
- At `--native-scale 2` (or `--native-widescreen`) the game's own pictures differ from the switch off
  in about 4,000 of 1.1 million pixels (at most 35), all in the top rows and a strip at the left edge,
  where the turn smear reads past the picture. The native GS's picture there depends on what its targets
  held before at scales above 1: with the switch off, a run loaded from a state at vblank 1990 differs
  from the run that went on in the same strips (7,398 pixels, at most 252). The pictures between move
  half way at every scale (map 000: 15.5 then 14.5 px of 30.5 at 2x).
- With `KF4_INTERP_STILL=1` (the frame's own camera) the picture between equals the next vblank's,
  within 2 on a few dozen pixels.
- Turning on map 000 the picture moves 7.5 px, then 7 px, where the game's frames move 14.5. A save
  state taken with it on loads with it on or off.
- Off: boot_test and frame_test as before (the logs and the 52 pictures the same as the build without
  the change), `--stats`' PATH1 hash and VU1 cycles the same, vu1_test passes, gsn_draw_test's 300
  cases and 300 shared-memory rounds pass, and the map tour's replay
  (`kf4-gsreplay --compare-renderers`) gives the same differences on every map (366,070 pixels in all).
- The window, not `--fast`, turning on map 000 for 410 vblanks: with either presenter 408 or 409 of the
  vblanks show a new picture one vblank after the last (16.7 ms apart, sd 1.0 to 1.7 ms), and none or two
  of the 410 pictures are replaced before they are shown. Before the window kept two, SDL's presenter
  lost 11 of them with the switch on (48 with it off). `--stats` prints these counts.
- With stage 4's wait for the vblank's picture (Progress, "Frame pacing"), turning and walking on
  map 000 (vblanks 1961-2600, 640 presents, per-present timestamps in a test build): the Vulkan
  presenter at 30 and 60 shows every picture at its vblank (none late, repeated or dropped; presents
  0.23 ms apart in deviation). SDL_Renderer and paraLLEl-GS with the two-picture queue alone were a
  vblank late at 394 to 640 of the 640 presents (the queue, once two deep, never emptied with a
  picture every vblank); with the wait and a queue that leaves its oldest out when it was late at two
  presents in turn: 0 to 26 late, at most 1 repeated and 1 left out (`--stats`: "left out to catch
  up"), the presents 0.6 to 1.2 ms apart in deviation from the wait (up to 8.7 ms, over 1 ms at 13 to
  155 of them).
- Cost: the GS draws each frame twice. 2,030 vblanks headless (new game, map 000), on the GS's thread:
  2.15 s to 2.57 s at 1x, 2.42 s to 2.80 s at 2x, 2.50 s to 3.09 s at 2x with `--native-widescreen` and
  `--native-msaa 8`.

**Latency.**
- Nothing is shown later than on the PS2: the frame the game drew is shown at the same vblank.
- The picture between shows half of the camera's next move one vblank early.
- Turning from standing (map 000): the first picture that moves is the fifth vblank after the pad's
  input with the switch on, the sixth off.
- The window: with SDL's presenter (and paraLLEl-GS) the GS's thread hands most pictures over just
  after the game's thread wakes for them (the pixels are read back first). The window waits for the
  vblank's picture up to half a vblank, so it is shown at its own vblank; one later than that is kept
  and shown at the next (the queue of two), and a queue still late at the next present leaves its
  oldest out, so the window does not stay a vblank behind. With the Vulkan presenter nearly all are
  there in time, with no wait.

**What is left.**
- **Things that move by themselves**, first version (below, "What moves by itself"): the enemies, the
  arm and held item, the effects and moving objects are drawn half way when their batch pairs with
  the frame before. Still at 30 Hz: what the CPU draws anew each frame (its batches do not pair),
  animated textures (a texture's frame is not interpolated), the HUD and messages.
- **The screen effects that copy the frame** (the wobble of status bit 39 and the MP drain's tint, both
  through `GsDraw_CopyFrame(0x2BC0)`) draw the copy the frame made, so in those states the picture
  between is the frame drawn, not interpolated. Allowing the copy into 0x2BC0 would leave the picture
  between there, where `MapChange_CaptureFrame` leaves a frame for `MapChange_FadeOutCapture` to fade
  out over the frames after.
- **`Game_BlendPreviousFrame`** (on with `map_draw_state.bit40`, and while aiming) blends nothing in the
  picture between: its "previous frame" is the buffer being drawn.
- **2D drawn in the world as sprites** (none seen; the triangles are moved) would stay where the frame
  put them. Sprites are not moved, as the full-screen clears and passes are sprites too.
- **paraLLEl-GS** (`--renderer gs`) gets the same packets and shows the same pictures. (With
  `--no-whole-frame` the game's `Game_ShiftScreenByViewTurn` draws the frame before over the even
  lines, and both renderers read each vblank's picture a field at a time and deinterlace it, mixing
  in the field before: "The scanout".)
- **More than 60.** 120 or 144 Hz needs pictures at times between vblanks, from the wall clock, more
  than one replay a frame (each a fraction of the way), and a presenter that shows them at the
  screen's rate.

### What moves by itself

The world's matrices for models are `world_screen_matrix` times the model (the bones for the skinned
ones), multiplied on the CPU, so the replay cannot take the camera out of them. It pairs what was
drawn instead, frame to frame, in the GIF packets (`port/platform/frame_interp.c`, "What moves by
itself"):
- **A run is a model part.** Each VU1 run of a 3D program draws one batch of one part. VIF1 hashes
  what it unpacks at VU 0x80 and up for the run (the batch: the part's own vertices, before any
  matrix; below 0x80 are the matrices and lights), only with the switch on (`port_vif1_run_key`).
- **Places.** Each frame's 3D vertices are kept as places in the world: unprojected from the screen
  and the depth with that frame's camera (before the eye: in the camera's space).
- **Pairs.** A run pairs with the run of the frame before with the same batch and as many vertices
  whose middle is nearest, if it is the nearest to that one in turn (the map's pieces repeat; one that
  came into view is not taken for the one beside it), and less than 400 units away.
- **The picture between.** A paired run that moved (a vertex by more than a 500th of its distance from
  the eye), or that went with the camera (in the world, every vertex moved by the camera's move: the
  sky's dome on map 000), is drawn half way between its two places, from the camera half way. (By the
  500th alone, the dome's nearer strips moved with it and the farther ones stayed in the world: thin
  dark lines along their shared edges in the pictures between when walking forward, bright ones walking
  back.) A run that did not
  move, or has no pair, moves with the camera as before (in the world), or stays as it is (the arm,
  before the eye).
- **The arm** is marked by the game (`FrameInterp_BeginArm` before `Player_DrawArm`): it is in the
  world, by the camera of the world, moving with the swing.
- **Depth.** From Q where Q is 1/w; the sphere-mapped second pass of program 0x280 has Q 1.0 and goes
  over its first pass's points, whose depth it takes (the arm's Z is pulled near the eye, so Z gives the
  wrong depth). This also mends the camera's move of such passes in the world.
- **Not paired:** a run whose half-way place cuts in front of the eye (the arm turning round the
  shoulder in one frame: the straight line between cuts the corner). A vertex with ADC in either frame
  may have been clipped there (its place wrapped round) while this frame's triangles use it: it is
  taken only when it is as far from its run's middle in both frames, else it moves with the middle.
- **Checked.** Switch off: boot_test, frame_test, `--stats`' PATH1 hash, gsn_draw_test and the map
  tour (366,070) as before. Switch on: the game's own frames byte for byte as with it off (map 000:
  walking, turning, swinging the arm). Pictures: the arm's swing on map 000 (vblanks 2034-2036,
  2046-2048: the hand half way, where the camera-only picture showed the next frame's pose), the bats
  of map 016, the plant creature and the torches of map 000.
- **Cost:** the walk keeps every frame's vertices and pairs the runs (sorted by batch): about 0.4 ms
  a frame on the DMA controller's thread (2,300 vblanks headless: 3.79 s to 4.23 s).
- **Left:** two things that share a batch and cross (both are drawn at the wrong one's place half
  way); `Game_BlendPreviousFrame` while the arm swings (the game's frames are blended with the frame
  before, the picture between is not: the arm is a little sharper there); what the CPU makes anew
  each frame.

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
