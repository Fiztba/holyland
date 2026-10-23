# Working on the renderer

Start here if you are changing anything that draws: the native GS, the VU1 programs, the window, or
a picture switch. STEP3.md has the design and the history; PLAN.md lists every way the picture
differs from the PS2 and the switch for each; this page is the map and the checks.

## Where things are (October 2026)

The defaults are the native GS (`--renderer native`), the Vulkan presenter (`--presenter vulkan`), the
VU1 programs in C (`--vu1 hle`) and whole frames (the turn comb and the movies' fields left out;
`--no-whole-frame` brings them back). paraLLEl-GS stays behind `--renderer gs` as the reference, and
draws instead when the native GS cannot start.

| Path | What |
|---|---|
| `port/gs/native/gsn.cpp` | the native GS: the GIF front end, registers, primitives, render targets on the GPU, textures, blending, the scanout, save states, and its own thread (a ring of packets and vblanks) |
| `port/gs/native/gsn_vram.c` | VRAM on the CPU in the GS's layout, every format's swizzle |
| `port/gs/native/shaders/` | GLSL; `python tools/build_shaders.py --glslang PATH` writes the committed `gsn_shaders.h` (only needed when a shader changes) |
| `port/gs/gs_parallel.cpp` | paraLLEl-GS behind the same C interface |
| `port/platform/gs.c` | picks the renderer(s), compares them with `--renderer both`, hands pictures to the window |
| `port/platform/present_vk.cpp` | the Vulkan presenter: a swapchain on the native GS's device, the overlay through ImGui's Vulkan backend; the picture is scaled to the window by the native GS (`present_scale`, `shaders/present_scale.frag`, `present_sharpen.frag`) and copied a pixel a pixel, or blitted (sharp, smooth) |
| `port/platform/video.c` | the window, SDL_Renderer, frame pacing and dumps |
| `port/platform/vu1.c`, `vu1_hle*.c` | the VU1 interpreter and the four programs in C (bit for bit the interpreter's) |
| `port/platform/frame_interp.c`, `game/frame_interp.c` | `--frame-rate 60` to 240: a frame's packets drawn again for each picture between two frames, the camera, the models, the 2D and the reflection moved to that time; `port/platform/video.c` shows them at their times. With the native GS alone the pictures are drawn on the GPU: the frame walked once, its draws recorded once, each picture the recorded calls again with its vertices moved by compute (`shaders/gs_replay*.comp`; STEP3.md, "On the GPU") |
| `port/render/`, `game/native_draw/`, `port/gs/native/gsn_native_draw.inc` | native draw (`--native-draw`, in work on branch `native/draw`): the game's draws handed to a renderer core and drawn by the native GS with a vertex shader doing VU1's maths (NATIVE_DRAW.md, NATIVE_DRAW_TASKS.md) |
| `tools/gsreplay/` | `kf4-gsreplay`: a GS capture (F9) drawn again by either renderer, compared draw by draw and picture by picture |

## The rules

- **Never touch the real data folder in tests.** Every run sets `KF4_DATA_DIR` to a temporary folder
  (settings, saves, the pipeline cache and the memory card live there).
- **A picture difference from the PS2 gets a switch**, listed in PLAN.md. For artefacts that only
  made sense on an interlaced TV (field shifts, half-picture movies) the owner chose the modern
  picture as the default, fixed at the source, with the switch bringing the PS2's back. Gameplay
  stays as the PS2 has it.
- **paraLLEl-GS is LGPL and PCSX2 is GPL:** read them to learn what the GS does, but copy no code.
- **Compare what is shown, not only VRAM.** The native GS went out with a comb while turning that
  every VRAM comparison passed, because the difference was in how the picture was read for the
  screen. `--compare-renderers` now compares the scanouts too (`scanout N`, `scanouts:`).
- **In play the frame is drawn into the buffer not shown.** Comparisons at the end of a capture use
  `end-other` (the buffer being drawn), not `end` (the one on screen).
- **Don't judge interlace from shrunken pictures.** Halving a picture by nearest-neighbour drops
  every other row: exactly the rows a comb is in.

## The checks before a renderer change goes in

Build with `cmake --build out/windows-clang` (or `out/linux-gcc`), then:

1. **`gsn_draw_test`** (ctest `gsn_draw`): random draws on both renderers, compared in VRAM, then
   rounds of buffers sharing memory. 0 failures of 300 cases and 300 rounds; try a few seeds (its
   header comment lists the switches: `GSN_CASE`, `GSN_DUMP`, `GSN_CHUNK` …).
2. **`gsn_vram_test`**, **`vu1_test`**.
3. **`tools/boot_test.py`** and **`tools/frame_test.py`**, also with `--renderer native`; frame_test
   also checks that the last picture dumped alone is the same as from a run dumping every picture
   (reading a picture must never change later ones).
4. **The map tour comparison.** `tools/map_tour.py --out DIR` makes a capture per map (once); then
   for each, `kf4-gsreplay DIR/NNN/capture.kf4gs --compare-renderers --no-resync --draws
   999999-999999 --limit 0` and read the `end-other` and `scanouts:` lines. Keep the sum of
   differing pixels over the 64 maps from before your change; no map should get worse without a
   reason you can name. Captures made with today's defaults (the game's flicker filter off): 414,901
   pixels, worst difference 53 x 2 = 106, all scanouts identical; with `--layer-z tolerant` (the game's
   default, not the comparison's) 416,255, worst 130. Captures with `--flicker-filter`
   (or older ones, such as October 2026's): 362,601 pixels, worst 53. The filter blends every row
   half over the row above, which halves any difference between the renderers.
5. **Live:** a few maps (`--teleport 1900:N:0` after `tests/newgame.pad`) with `--renderer both
   --vu1 check --stats`, 2,500 vblanks: no crash, VU1 0 differ, the pictures' differences as before.
6. **Speed**, if the change could cost: 2,000 vblanks `--fast --headless --dump-frames 1999` on map
   000 and on 009 (the fire and the reflection: the worst map once); `--stats` splits the native
   GS's time.
7. **Linux builds** (gcc). On Mesa's lavapipe, `--renderer both` needs `LP_NUM_THREADS=1`, and
   gsn_draw_test runs a few cases a process (`GSN_CHUNK`, set by ctest) because lavapipe keeps
   memory until exit.

## Things that were hard to find

- **A target is as tall as its draws reach, not its scissor, for draws with Z.** The menus' scissor
  runs a row past their 224-line field buffers; a colour target made 225 rows tall ran into the Z
  buffer's first page, the two could not be targets together (they are made as big as each other),
  and the next field's clear and backdrop were drawn without Z: every other line of the Equipment and
  Journal screens black (October 2026).
- **Z is a whole number per pixel.** The GS rounds the interpolated Z; keeping the fraction hid the
  lava's glow behind the lava it is drawn on (with a GEQUAL test). The rounding is also what makes the
  hatching, teeth and lace on the game's layered walls and the sawtooth where surfaces meet:
  `--native-layer-z tolerant` (the default) keeps the fraction, lets a layer's triangles (a corner's
  alpha below 0x80, blended (Cs - Cd) As + Cd, GEQUAL) pass within one Z, Z written after in a pass of
  its own; other draws that write Z draw their Z first and their colour within half a Z (the GS's ties on average: signs on walls), and a draw without Z writes within half a Z (PLAN.md; `gsn.cpp`, "Z and layers"); `ps2` is
  the GS's. A test pass with no Z written must not see its own batch's triangles as a pass with Z
  would: drawn as one, a layer mesh folded over itself was blended twice (flat olive patches).
- **A buffer that starts mid-page** reaches the page after its last only where its blocks run past the
  page's 32 (`buffer_pages`). Counted as one page more whatever it covered, a CLUT uploaded to VRAM's last
  page (0x3FE1, 8 x 2: the "N Gold" message's) reached round to page 0, the frame buffer, which was written
  back and dropped every frame: the picture shown from VRAM at 1x every other frame, the pictures between
  drawn from the packets again, some black (October 2026).
- **A texture decoded from VRAM watches only the pages its draws read** (without mipmaps: the rows and
  columns from its vertices, to whole pages). The menus read the play's frame kept at 0x3240 as a
  1024 x 512 texture, which runs on round VRAM's end into their field buffers: taken whole, every field
  sent both field buffers back to VRAM and decoded the texture again, 30 ms a vblank, the menus at 20 to 40
  vblanks a second (at every frame rate, since before the render passes).
- **The menus' fields at a scale are woven a row at a time** (`weave_fine`, whole frames on): field p's row
  r of h x sy is the picture's line 2 r / sy + p, so its rows go to rows 2 r + p sy. Woven a line (sy rows)
  at a time, each band held a line drawn over twice the height, the other field's half a line off: the
  menus' text in stripes of three rows at 3x. `--no-whole-frame` (two moments) keeps the lines.
- **The 2D at a larger scale** (`--native-2d smooth`, the default): magnified 2D textures read with a
  cubic at the pixel's own place, moved half a GS pixel on (a filter centres a texel where nearest at the
  GS pixel's integer point has its left edge: unmoved, the letters sat half a GS pixel right). Read
  nearest at the integer point, each texel was a block and the letters differed from scale to scale.
- **A picture between recorded on the GPU must not be submitted half drawn without its moves.** Its
  compute passes (the kicks moved, the corners made) are recorded at its start and read what the stream
  writes into mapped buffers as it goes, the corners' count last. A write-back in the middle (an upload
  asked for again over a scrolling texture's target: a coin on the ground, the shop on map 060) submitted
  the draws so far with the count of the picture before and kicks not yet written: pictures between with
  the HUD gone, black, or textures across the screen, in main since the pictures between went to the GPU.
  `submit` now writes the count so far, flushes, and runs the passes again after (`--stats`: "submits
  while recording").
- **ZTE 0 means no Z at all**: no test, no write, no depth target, whatever ZBUF says. The movie's
  sprite has ZBUF pointing at the decoded picture.
- **Texel coordinates** are snapped as `floor(tc * 16 + 1/64)`; bilinear ties round up; fog is a
  rounded lerp; MTBA works out mipmap addresses at TEX0's write.
- **Blending:** Vulkan's fixed function where the equation maps onto it exactly, the shader (from a
  copy of the target) elsewhere: sprites, draws reading their own target, DATE, 16-bit targets,
  As above 0x80. Overlapping primitives in such a batch are drawn in runs that don't overlap, each
  with its own copy: kept in the render pass (an input attachment), or with MSAA resolved by the run's
  pass into the copy the next run reads (STEP3.md, "Render passes"). A copy outside the pass costs a
  pass's end and beginning: the shadows' silhouettes are ~240 runs a picture.
- **NVIDIA's MSAA resolve is not a function of the samples.** Its mean of two 8-bit samples rounds a
  half up or down depending on more than the two values, so no shader reproduces `vkCmdResolveImage`;
  a render pass's resolve attachment does (8-bit; not 16-bit). Exactness at MSAA means resolving.
- **Barriers are merged** (`full_barrier`): a full barrier right after another is left out, which
  `cmd()` tracks. Anything that records into the native GS's command buffer must go through `cmd()`,
  or a barrier it needs can be left out.
- **Measuring the GPU:** `KF4_GSN_GPU_TIME=1` with `--stats` prints the GPU's time a picture by kind
  of work, and `--stats` says why render passes ended and what resolves were for. Another program on
  the GPU (a second run of the game) shows up in those times: run alone.
- **A larger `--native-scale` is not just more pixels.** A GS pixel is drawn when its integer point
  is inside; a target pixel at scale N samples the middle of its own area, half of which is left of
  that point for the GS pixel's first pixels. So 2D (sprites from 0, or ending at 127.5) loses or gains
  a column, and nearest reads with coordinates on texel edges take the texel before: `flat_2d` and
  `sample_level`'s scaled path put both back on the GS's integer points. `kf4-gsreplay --scale X[,Y]`
  draws a capture at a scale (`--draws` then writes the targets at their own pixels). `flat_2d` knows
  the 2D by its triangles (one Z and colour, corners on their bounding box) and leaves out batches
  textured in perspective (ST with a Q not 1: `perspective_batch`): a far billboard of program 0x700
  (a brazier's glow) is such a triangle pair once its Z rounds the same at its corners, and its halves,
  each moved out to whole GS pixels, overlapped along the diagonal: a bright line across the glow.
- **Granite:** `update_image` with row length 0 means the image's width; there is no SPIRV-Cross,
  so resource layouts are written by hand; its render pass cache ignores the sample count (MSAA
  passes name their subpass).
- **The VU1 programs in C must stay bit for bit.** `--vu1 check` runs both and counts differences;
  a program the C code does not know (by its microcode hash) falls back to the interpreter.

## What could come next

- **VU1 on the GPU.** The owner wants it eventually. The C programs already take VU1 from two thirds
  of the run's time to a sixth; shaders would be a second implementation to keep bit for bit
  against the interpreter (tests/vu1_test.c and `--vu1 check` are the oracle).
- **paraLLEl-GS's licence before release builds**: the native GS and paraLLEl-GS are one library
  (`kf4_gs`) on paraLLEl-GS's copy of Granite; a release without paraLLEl-GS needs them split.
- **Higher frame rates:** flipbook animations and pictures the CPU makes anew (the HUD orb) change
  once a frame; two look-alike models crossing can swap for a few pictures. The pictures between are
  drawn from recorded commands now (STEP3.md, "On the GPU"); what is left of their cost is the GPU's
  (the shadows' runs of shader blending) and the recorded calls made again through Granite (0.2 to 0.8
  ms a picture): re-submitting command buffers would take Granite's per-frame descriptors and staging
  out of them.
- **MSAA:** draws without Z are not smoothed. The cut-outs are (`--native-cutout-aa`, shaded at every
  sample), but only as many samples as MSAA has: far fences and grass still shimmer some at 2x;
  mipmaps made for cut-out textures would be the next step.
- STEP3.md, "Progress" and "What could come next", has the rest (write-backs left, uploads on the
  GPU, exact blending by fragment shader interlock).
