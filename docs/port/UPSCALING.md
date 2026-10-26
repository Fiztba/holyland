# Upscaling: FSR and DLSS (plan)

What it would take to draw the world below the window's resolution and bring it up with AMD's FSR or NVIDIA's DLSS.
Nothing here is built yet. It follows RENDERER.md's rules: the PS2's picture stays the default, and this is one
more opt-in switch.

## What it buys, and what it does not

- **Mostly picture quality, some speed.** A temporal upscaler is also an antialiaser. It would take over from MSAA
  and `--native-cutout-aa`, and would smooth what those miss: draws without Z, far fences and grass, and
  shimmering texture edges.
- **The speed gain is smaller than in a modern game.** Most of a picture's GPU time does not scale with its pixels.
  At the pond, 1x without MSAA still took 2.75 ms against 4.26 ms at 3x with MSAA 2 (NATIVE_DRAW_P8.md, "GPU cost
  at the owner's spots"). The upscaler's own pass runs once for every picture shown, pictures between included.
  At 240 Hz a 0.5-1 ms pass is 12-24% of the GPU on its own. So the win is a sharper 1440p or 4K picture for the
  cost of today's 2x, not more pictures a second than today's 2x gives.
- **No frame generation.** The port's pictures between (`--frame-rate` up to 240) already move every model and the
  camera to the picture's time, from the game's own data. DLSS-FG and FSR 3's frame interpolation guess the same
  thing from the picture, and they add latency and artefacts. Only the super-resolution parts are wanted.
- **The modern renderer only** (`--renderer modern`). Upscalers need per-pixel depth and motion. Only the scene
  (`gsn_standalone.inc`) has real depth and knows where each vertex came from. The PS2 path and hosted mode draw
  through the GS's integer Z, so they have neither.

## Where it goes: the scene before the composite

T2 already splits the picture the right way (NATIVE_DRAW_P8.md, T2; `gsn_composite.inc`). The scene, meaning the
world, is drawn at a scale and sample count of its own. The GS draws the HUD, messages, menus and fades at its own
scale. `sa_composite` makes the picture shown from both at the scanout. An upscaler goes between the scene's
picture (`SaHeld::hi`) and the composite:

```
scene (jittered, render size)  --colour, depth, motion-->  upscaler  -->  hi at output size  -->  sa_composite
GS 2D (its own scale)  --------------------------------------------------------------------->  sa_composite
```

So the HUD and text are never upscaled, and the GS's 2D arithmetic is unchanged.

## The inputs, and where each comes from

| input | today | work |
|---|---|---|
| colour at the render size | the scene's frame image | the scene's size must be free of the GS's integer scale (below) |
| depth | the scene's D32F, reversed Z, in the GS's Z units (Z / 2^24) | hand it over with the upscaler's inverted-depth flag. FSR also wants near, far and the field of view, from the game's view fields (`frame_interp.c` has the camera) |
| sub-pixel jitter | none | an offset added to `ndc` in each scene vertex shader (`nd_000.vert`, `nd_280.vert`, `nd_700.vert`, `nd_water.vert`, the `ND_STANDALONE` blocks), and the same offset in the ties' Z-first draws. A Halton(2,3) sequence, one step a picture |
| motion vectors | none | an RG16F attachment for the scene. Each vertex shader projects its vertex twice, at this picture's time and at the last picture's, and writes the difference. Models already have their pose at any time between two frames (`nd_between.h`, `between_move(b, p, u)`), and the camera at t 0 is `move0`/`mid0`. Static map records move with the camera only |
| reactive / transparency mask | none | effects (program 0x700, additive), the water's surface and shader-blended runs, flagged by kind as the scene draws them. They have no reliable motion of their own, so they should not leave trails |
| exposure | none | LDR. The GS's 8-bit colour goes in as it is, with the LDR flags |

Motion across a frame boundary: a picture between at time t of the pair (frame N-1, frame N) is followed by
pictures of the pair (N, N+1). Positions are continuous there (t 1 of one pair is t 0 of the next), but the
motion changes. The first version can take the last picture's position from the current pair at t minus one
picture's step. That is slightly wrong at the first picture of each frame, and the upscaler's reactive handling
covers it. Exact would keep the previous pair's `Between` data for one more picture.

## The upscale slot (done, branch `upscale/scene-size`)

`--upscaler bilinear` with `--renderer modern` and a `--scene-scale` below the GS's (both single-sampled). The
scene is brought up to the GS's scale as `sa_give` hands it to the GS's frame target (`sa_upscale`, a recorded
linear blit). It replaces `sa_resample_pass`'s pixel under the middle, and there is then no composite: the GS draws
the HUD, messages and fades over the result at its own scale. Without the upscaler, a scene below the GS's scale goes
through the composite at the scene's scale, which brings the 2D down to it too: the HUD at 2x under a 4x GS.
- **Whole-number scales, for now.** On a 4K screen the GS is at 4x (3840 x 1792 with native widescreen), and the
  scene at 2x or 3x. With native widescreen the ratios differ across and down at 3x (4 to 6 across, 3 to 4 down:
  1.5 and 1.33), and are the same at 2x (2.0 both). The FSR 3.1 dispatch takes the render and output sizes
  apart. Whether it, and DLSS, take unequal ratios is not settled from their docs yet. If not, the scene's size
  below is needed for 3x, or the output stops at one ratio and the presenter takes the rest.
- **The temporal upscalers go in this slot.** At `sa_give` the scene's colour (`sa.color`), depth (`sa.depth`) and
  the GS's target to write are all to hand. Only the release (`keep` false) is a picture's last. A turn in the middle
  of a frame gives the scene to the GS and takes it back, so a temporal upscaler should run at the release only,
  with the turns' gives bilinear (or nearest) as now.
- Checks: with `--upscaler off`, every picture of 6 vblanks on 000 and 009, and on 000 with `--scene-scale 2`,
  is byte for byte the build before (72 pictures). With it on, `--scene-scale 2` under `--native-scale 4`: 3840 x
  1792 pictures, the HUD the GS's at 4x, the world within 0.3 levels of native 4x on average. Turning with the
  d-pad, the pictures between follow native's motion picture for picture, so the recording makes the blit again.

## Jitter (done, branch `upscale/scene-size`)

Each picture shown takes the next step of the Halton (2, 3) sequence (eight steps): an offset within half a scene
pixel each way, added in NDC to the positions of the scene's draws into its frame (`sa_000`, `sa_280`, `sa_700`,
`sa_water`: `Picture.jitter`, where `GSN_M_JITTER` is set). A picture between has its own in its Picture
(`InterpPictureHeader::jitter`, `sa_jitter_picture`), so the recording's replays each draw at their own. The frame's
own picture binds a Picture of its own (`sa_jitter_frame`, at the scene's clear) instead of the buffer of zeros. On
with a temporal upscaler, or `KF4_UPSCALE_JITTER=1` to look at it.
- Not jittered: draws into a named target (the reflection, the scrolling textures: textures the frame reads), the
  trail strip, and the GS's own 3D the scene draws (`gs.vert`, a few in a hundred of the scene's batches). They stay
  still under the upscaler's jitter.
- The shaders were compiled with glslang 16.6.0. It builds 47 of the 49 committed shaders word for word; for
  `nd_water`/`sa_water` it differs by 8 words. Only the shaders this change touches take the new SPIR-V (the
  scene's and hosted nd_* vertex shaders, their oracles, `gs_replay*.comp`). With jitter off, every picture of 6
  vblanks on 000 and 009 (the pond's water too), and on 000 at `--scene-scale 2`, is byte for byte the build before.
- With it on, standing still at `--scene-scale 2 --upscaler bilinear`, each picture differs from the one before by
  0.65-1.07 levels on average where it was 0.06-0.19, and the HUD not.
- The pipelines' shaders changed, so the shipped list (`gsn_pipeline_list.h`) has stale lines for them until it is
  made again from a map tour. The data folder's list fills in meanwhile, with "Building shaders" at the first start.

## Later: the scene's size free of the GS's scale

`--scene-scale` is a whole number, 1 to 4 (`sa.sx`, `sa.sy`, `snt.frame_sx`). An upscaler wants any size, for
example 1707 x 960 under a 2560 x 1440 window (FSR's and DLSS's "quality" ratio, 1.5). The scene's vertex
shaders already work in NDC, so the scene itself can draw at any size. The integer scale shows up only where the
scene meets the GS:
- `sa_give`, the scene's frame handed to the GS at the GS's scale, averages whole blocks of scene pixels. With an
  upscaler it can sample the upscaled picture instead.
- The turns' copies between the scene and the GS's targets: with T2 they happen about once a frame (0.26 a
  picture on map 000).
- Scissor and `flat_2d`, in GS pixels times the scale.

Named targets (the reflection, scrolling textures) stay at the GS's scale. They are textures, not the picture.

## Libraries

- **FSR 3.1 upscaler** (AMD FidelityFX SDK, MIT). It has a Vulkan backend and can be vendored in `third_party/` and
  built with the port on Windows and Linux, on any GPU. It is called with raw Vulkan handles, which Granite exposes
  (`Device::get_device()`, `CommandBuffer::get_command_buffer()`, `Image::get_image()`). Layout transitions around
  the call go through Granite's barriers, so its layout tracking stays right. This should be the first backend.
- **FSR 1** (EASU + RCAS, MIT, two GLSL headers): spatial only, with no jitter or motion. It can sit beside
  `present_scale.frag` and `present_sharpen.frag` (whose sharpening is already CAS-like) as a cheap stopgap,
  though it upscales the HUD with everything else.
- **DLSS super resolution** (NVIDIA DLSS SDK, NGX). NGX names Vulkan instance and device extensions that must be
  turned on when Granite's context is made, so it needs a hook there. `nvngx_dlss.dll` may ship beside the
  program under the SDK's licence (with its attribution), or the user supplies it. The SDK is not open source, so
  build it only when its path is given (`-DKF4_DLSS_SDK=...`) and keep it out of the default build.
  Streamline is the other way in. It hooks Vulkan's creation calls, which suits Granite less well.
- **XeSS** has a Vulkan path too. With an interface shared by all three it is a small addition later.

One interface (`gsn_upscale.inc`) would cover all of them: colour, depth, motion, mask, jitter, render and output
sizes, and a reset when the camera cuts (`pending.cut`, a teleport, a map load, a state load). The choice would
be `--upscaler off|fsr1|fsr3|dlss` with `--upscale-quality`, and the settings and overlay entries beside them.

## Steps

1. **The upscale slot** (done, above): whole-number scene scales below the GS's, brought up by a bilinear blit at
   `sa_give`. The scene at any size (`--scene-size W,H` or a ratio) is for later, if the upscalers want equal
   ratios or finer steps.
2. **Jitter and motion vectors**, with a debug view of the motion attachment (`KF4_UPSCALE_SHOW=motion`). Check:
   standing still, the motion is zero everywhere but the effects. Turning, the static map's motion matches the
   camera's. At 240 the motion of a picture between is a quarter of the frame's.
3. **FSR 3.1** behind the interface. Check: the owner's spots (pond, fortress, canyon, under water) at 240 for
   ghosting behind the zombies and bats, the water and the effects. Then pictures a second and GPU time against
   today's 4x (`pace_test.py`, `KF4_GSN_GPU_TIME=2`).
4. **DLSS**, the same interface, built only with the SDK.
5. **Masks and tuning**: reactive masks per kind, sharpening (RCAS, or our CAS), and resets at cuts.

Gameplay is untouched. Every step keeps `--upscaler off` byte for byte as before (the off check, the four spots,
the map tour).
