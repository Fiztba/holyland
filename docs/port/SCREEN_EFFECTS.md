# The game's full-screen and screen-reading effects

Every effect the game draws over the whole screen, or draws from the screen's own picture, found in the
decompilation (`decomp/src/game`, the PC changes in `patches/game/`), with what turns it on, what it reads and
writes in GS memory, and how the PC renderers treat it: `port/platform/frame_interp.c` (the pictures between
at 60-240), native draw's passes (`game/native_draw/nd_passes.c`, `gsn_nd_pass.inc`) and `--renderer modern`'s
scene (`gsn_standalone.inc`, `gsn_composite.inc`, `gsn_scene_targets.inc`). The audit was made after the Queen's
Palace's purple wobble dropped the game to 14 frames a second at 240 (October 2026), to find the others before
play does. `tools/screen_fx_test.py` makes each one happen headless and measures it ("Tests" below);
`KF4_FX` (`game/screen_fx.c`, debugging only) forces the ones play reaches rarely.

## GS memory the effects use

| block (TBP/FBP x 32) | what | size |
|---|---|---|
| 0x0000, 0x1180 | the two frame buffers (`context1_buffer_half` picks the one drawn; the other is shown) | 640 x 448 PSMCT32 each (FBW 10), read as a 1024 x 512 texture (TW 10, TH 9) |
| 0x2BC0 | **shared**: the copy of the frame (`GsDraw_CopyFrame`, 640 x 448 PSMCT32, to 0x3D3F); the reflection's view (`Map_SetOffscreenTarget`, FRAME 0x0A04015E: 256 x 256 PSMCT16S, 0x2BC0-0x2DBF); the pause's and the map viewers' textures (`TextureTable_SetBasePointers(.., 0x2BC0, 0x3FE0)`) | |
| 0x3240 | the menus' work buffer (FBP 0x192): the shown frame copied there as the menu's backdrop, shown again after (`GameLoop_ShowGrabbedFrame`, TEX0 0x26802B240) | 640 x 448 PSMCT32 |
| 0x3540 | the Dark Succubus' body texture (`GsDraw_CopyFrameToTexture(0x3540, 0, 256, 256)`; inside the frame copy's range) | 256 x 256 PSMCT32 |
| 0x37E0 and others in 0x2BC0-0x3D3F | effect and model textures uploaded each frame (the frame copy overwrites them; `GameLoop_DrawFrame` re-uploads the effects' with `Effect_UploadTextures` after a copy) | |
| scrolling textures' targets | `MapScroll_UpdateDraw`/`UpdateCopy` (lava, water, the flesh cavern), `GsDraw_DrawScrollTexture` | 128 x 128 mostly |

## The effects

Order: as `GameLoop_DrawFrame` (`core/game_loop.c`) draws them after the world, then the ones drawn elsewhere.
"Reads the frame" means a texture whose TBP is the buffer being drawn (a feedback draw); "reads the shown
frame" the other buffer.

### 1. Under water: the copy, the wobbling screen, the tint

- **Trigger:** `PlayerState.status_bits` bit 39 (set by the collision code each frame the eye is below a water
  surface, cleared at the start of each played frame). Real: deep water (map 029 the Underground Canal:
  `bugs1024/quick_underwater`), and the deadly water's death screen (`bugs_modern/quick_deathwater`).
- **Draws:** `GsDraw_CopyFrame(0x2BC0)` (a local-to-local BITBLT, the frame drawn to 0x2BC0, 640 x 448 PSMCT32);
  `Game_DrawWobblingScreen(0x80, 0x80, 0x80, 0x80, 1.0)`: a 7 x 10 grid of 64 x 64 cells whose corners move as
  springs (`wobble_grid_points`, rand), 70 textured quads (GIF packets, TEX0 0x26802ABC0 = TBP 0x2BC0 TBW 10
  PSMCT32 1024 x 512, TEX1 0: nearest, no mipmaps, ALPHA 0x44, Z writes off) over the screen; then
  `Effect_UploadTextures` (the copy overwrote them); after the arm `Game_TintScreen(0x5E, 0x87, 0x92)`: the frame
  over itself in ten 64-pixel strips, modulated (reads the frame, TEX0 TBP = the frame drawn, TFX modulate).
  **On map 035** (the Guardian's Room) before its water is purified (`guardians_room_water_purified`):
  `GsDraw_FillScreen(0x40, 0x80, 0x40, 0x78, 0x44)` (a green fill) instead of the tint.
- **frame_interp:** the copy is one of the frame's copies (`Uploads.copies`); `flat_copies` makes it again in
  each picture between from that picture's own frame when the frame reads it after the copy and not before
  (the wobble does); the death-water fix (ce05fff9) counts a mipmapped texture's reads over its real levels. The
  wobble's quads are 2D items paired with the frame before's (`flat_pair`), moved t of the way. The tint is a
  pass reading the frame (TEX0 moved to the picture's buffer by `ad_write`).
- **Native draw / modern:** the tint is `NativeDraw_TintScreen` (a screen pass); the copy and the wobble stay
  the PS2 path's packets. The scene gives the GS the frame at the copy and keeps its own picture as copied
  (`sa_give(true)`, `sa.fcopy`), read as the texture at 0x2BC0 until those pages are written. A GPU copy of
  the frame over whole pages drops the targets wholly under it without writing them back (`overwrite_lo`,
  perf240).

### 2. The MP drain (the Queen's Palace, the Giant Leader): the purple wobbling screen

- **Trigger:** `PlayerState.mp_drain_tint` 1-8: `Player_Update` raises it each frame `pending_mp_drain` is set
  (`Attack_DrainPlayerMp`: the Watchers of map 022 the Queen's Palace, `enemy/watcher.c`; the Giant Leader of map
  034, `enemy/giant_leader.c`) and lowers it by one a frame after.
- **Draws:** the same copy to 0x2BC0 and `Game_DrawWobblingScreen(r, g, b, 0x80, tint)` with
  r = 128 - 58t, g = 128 - 78t, b = 128 + 12t (t = mp_drain_tint / 8): purple, the wobble scaled by t. With
  under water at once, two copies and two wobbles.
- **PC:** as under water. **At 240 in modern it dropped the game to 14 frames a second** (the exact restore drew
  the whole frame again for the scrolling textures, and the wobble read the copy from VRAM: fixed on
  `p8/queen`, see "Fixes").

### 3. Player_DrawScreenFade, the haze, the debug depth sprite

- `Player_DrawScreenFade`: `PlayerState.screen_fade` > 0 a white fill (ALPHA 0x48), < 0 black (0x44), Z writes
  off. No read. (Set by `Player_UpdateScreenFade`: spells and scripted flashes.)
- `Game_DrawHazeLayers` (MapDrawState bit 44): a full-screen sprite of the frame over itself at each haze
  layer's depth (`haze_layer_depths`, projected once at boot), ALPHA 0x80000000A8, TEST 0x51001 (Z test), CLAMP
  5: reads the frame. **No code of the game sets bit 44** (only `Map_ResetGameState`'s defaults: bits 42, 45,
  46): unreachable in play; `NativeDraw_HazeLayers` exists. `Game_DrawHazeLayersShifted`: never called.
- `Game_DrawDepthTestSprite` (bit 41): a debug sprite, pad 1's L1/L2 move its depth. Never set by the game.

### 4. The one-row blends (bits 42 and 43): the flicker filter

- `Game_BlendScreenOffsetDown` (bit 42, **set at every map reset**): the frame over itself one pixel lower at
  half strength, ten 64-pixel strips, TEXFLUSH first, Z writes off: reads the frame. The PC turns it off by
  default (`--flicker-filter` keeps it; `NativeDraw_FlickerFilter(0)`). Also drawn by the King's Map
  (`MapViewer_DrawKingsMap`).
- `Game_BlendScreenOffsetUpDown` (bit 43): down then up. Never set by the game (`NativeDraw_FlickerFilter(1)`).

### 5. The frame before (bit 40, the knock-back)

- `Game_BlendPreviousFrame(alpha)`: the shown buffer (the frame before) over the whole screen at alpha, one
  sprite, ALPHA 0x44, TEST 0x51001: **reads the shown frame**. Drawn for MapDrawState bit 40
  (`previous_frame_alpha`, 0x40 by default; never set by the game) and **while `PlayerState.aim_speed` > 0**:
  the knock-back of a hit (`Player_KnockBackFromHit`, `Player_KnockBack`), every fight.
  `Game_BlendPreviousFrameZoomed`: never called.
- **frame_interp:** a picture between reads the native GS's copy of the frame shown, by a^t (`replay_pending`,
  `move_flat_vertex`); native draw's pass says so with `RD_ND_READS_SHOWN`. **Modern:** reads the GS's other
  buffer (at the GS's scale).

### 6. The view-turn shift (bit 45)

- `Game_ShiftScreenByViewTurn` (bit 45, set at every map reset; the main loop's draw only): the frame drawn again
  moved by the view's turn since the frame before (at most 8 pixels), over every other line (SCANMSK 3), 20
  strips: reads the frame. A picture for the other field of an interlaced TV. The PC skips it with whole frames
  (the default; `--no-whole-frame` keeps it). `Game_ShiftScreenByFocusTurn`: never called.

### 7. Screen tints and fills (no reads)

All `GsDraw_FillScreen` (an untextured sprite over the screen, ALPHA 0x44 or 0x48) or the same through
`NativeDraw_FillScreen`; in modern the composite's chain of "ops over the screen" (`gsn_composite.inc`).
- `Player_DrawScreenTints` (`player_screen_tint`, set by `Player_UpdateScreenTints`): green (kind 0), dark
  yellow (1), purple (3), white (4), a cycling colour (5), darkness (subtracted), the red damage flash
  (`Player_FlashDamage`, dark red while purple shows), the death fade (black). The status ailments and the
  light spells: no screen reads.
- `Map_CoverScreenWithMapTint` (draw mode 1, between the mirror view and the world: the map header's colour).
- MapDrawState's screen fill (script command 197, `ScriptCmd_SetScreenColor`).
- `Game_DrawFade` (the scripted fades), `Splash_Draw`'s darkening, the message dim (`message_dim_alpha`), the
  pause's shade (START: `game_status.paused`).

### 8. The brightness option (every frame)

- `Game_ApplyBrightness(game_options[10])`: the frame over itself in ten strips, modulated by brightness / 128,
  last of every frame (over the HUD): reads the frame. `NativeDraw_Brightness`; modern's composite chain.

### 9. The Dark Succubus: a model drawn with the screen's picture

- **Trigger:** an entity whose spawn record's chr record is 0x47 (the Dark Succubus, map 038 the Dark Sanctuary,
  once the story has woken it): `EntityPool` gives its model `MODEL_FLAG_800` and the draw hook
  `Entity_PrepareScreenTexturedDraw`.
- **Draws (inside the world, before the model):** `Model_SetUvsFromScreen` (each vertex's UV from where it
  lands on the screen), `GsDraw_CopyFrameToTexture(0x3540, 0, 256, 256)`: a draw into a 256 x 256 PSMCT32 target
  at 0x3540 of one sprite reading the frame drawn (TEX0 TBP = the frame, 1024 x 512, TCC 1, ALPHA
  0x80000000A8, TEST 0x51001, CLAMP 5), then `GsDraw_ResetDrawTarget` and TEXFLUSH; the model then reads 0x3540
  (TEX0 0x220013540). The screen as it is up to that model, refracted through its body.
- **frame_interp:** a draw into another buffer from the frame (`Uploads.copies`, "the screen's picture on a
  model"), made again in a picture between under the same rule as the copy. **Modern:** a draw into a target
  other than the scene's reading the frame: the scene gives the GS the frame there.
- Not reachable headless without a late save: `KF4_FX=copytex` draws the same copy and a 256 x 256 sprite from
  it after the world instead (the same GS work, not in the world).

### 10. The reflecting pools (the reflection's view, draw mode 2)

- **Trigger:** maps with mirror water (mode 2, e.g. 009's pond: `bugs1024/quick_pond`).
- **Draws:** the world again from below into 0x2BC0 (256 x 256 PSMCT16S, `Map_SetOffscreenTarget`), then the
  water's map records (flag 0x200) with `Wobble_DrawRippledModel`: their vertices' UVs from their screen
  places moved by `wobble_sines`, reading 0x2BC0 (TEX0 0x220A12BC0). P6 (`nd_water.c`); modern: a named
  render target (T3), the reflection kept exactly when the camera is still (pond240).
- **The same pages as the frame copy:** with the MP drain or under water at a pool, the copy of the frame
  overwrites the reflection's target in the same frame.

### 11. The King's Map and the picture maps

- `MapViewer_RunKingsMap` (item 5; not usable on maps 000, 002, 004, 008, 009, 098, 099): its own loop
  (`Display_SwapBuffers`), the map's records drawn from above, then `Game_BlendScreenOffsetDown` and
  `Game_TintScreen(0xEA, 0xB2, 0x46)` (both read the frame), its panel textures at 0x2BC0. The picture maps
  (items 1-4, `MapViewer_RunPictureMap`): 2D pictures. Before a viewer, `GameLoop_ShowGrabbedFrame` or
  `GameLoop_RedrawFrame`. No pictures between (a loop of its own).

### 12. The menus' backdrops

- `Menu_CopyFrameToTexture` (CIRCLE: the game's menu), `Trade_CopyFrameToTexture`, `Smith_CopyFrameToTexture`,
  `SaveScreen_DrawBackground`: the shown buffer drawn into the work buffer (FRAME FBP 0x192, 0x3240) and back
  into the frame at half brightness (feedback through another target); after the menu
  `GameLoop_ShowGrabbedFrame` draws 0x3240 into both buffers. Their own loops, 60 a second, no pictures between.
  Modern: the scene gives the GS the frame ("the menus' backdrop", `sa_give`).

### 13. The map change

- `MapChange_CaptureFrame` (a door into another map): one frame drawn and `GsDraw_CopyFrame(0x2BC0)`; then
  `MapChange_FadeOutCapture` draws 0x2BC0 (TEX0 0x26802ABC0) darker each frame while the map loads (its own
  loop). frame_interp leaves this copy as the game made it (`flat_copies`: the frame does not read it after).
  Not in the tests: no headless way through a door yet (pad scripts reach none from the saves at hand).

### 14. Other copies and targets

- The scrolling textures (`MapScroll_UpdateDraw`/`UpdateCopy`, `GsDraw_DrawScrollTexture`): 128 x 128 targets the
  map's records read (lava, water, the flesh cavern's walls), drawn each frame; frame_interp draws them again in
  each picture between from the same packets and `port_interp_end_vblank` restores them exactly. The wave UVs
  (`Wobble_ApplyWaveUvs`, the flesh cavern 039/040/042) move texture coordinates only.
- Debug only: `ScreenDump_*` (local-to-host transfers of a buffer), `debug.c`'s fills.
