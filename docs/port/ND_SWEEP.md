# Native draw: the 64-map sweep (P1-2)

`--native-draw map,items` on every map of the disc, at 1x and 4x, against the PS2 path. No code changed;
this is the measurement and where each difference comes from. Branch `nd/p1-2` from `native/draw` 4d35c35,
October 2026. The fixes go to P1-1's owner (and P1-5's for the near cut), as the task list says.

## How it was run

- **Views:** the map tour's places (`tools/map_tour.py`: `map_places(data, 2)`, the map's start and one more
  place, at 4 headings each = 8 views; map 042 has only its start, 4 views). Each view is a made-up quick
  save (`write_made_up` from a base save taken with `tests/newgame.pad --quick-save-at 1900`), loaded one
  after another in one run with `--quick-load` (first on the title screen, then every 100 vblanks); the
  picture is taken 90 vblanks after each load (vblanks 220, 320 ... 920 = views v0 ... v7).
- **Compare:** per map and scale, `tools/native_draw_compare.py --kinds map,items --scale 1|4 --input
  title.pad --frames 220,320,...,920 -- --quick-load ...` (`--mode compare`: both runs queue both kinds of
  draw, the native GS draws one set), with `KF4_ND_LOG=1`. Then the same at 1x with `--mode onoff`
  (`--native-draw off` against `map,items`: the PS2 packets of native draws are not queued at all, which is
  what a player gets).
- 64 maps x (1x, 4x, onoff 1x) = 192 comparisons, 384 headless runs with `--no-audio` and a fresh
  `KF4_DATA_DIR`, two games at a time. A run took 3-33 s; other agents shared the PC and GPU, so the
  times are noisy and are not measurements.
- The outputs (pictures, heat maps, logs: game content) stay outside the repository, in
  `D:/hl-nd/p1-2/sweep/NNN/{s1,s4,s1_onoff}/` (`report.txt`, `heat_*.png`, `ps2|native|off|on/log.txt`).

## In short

- **Refusals: none.** Every block of every map parses (`meshes parsed (0 refused)` on all 192 comparisons;
  `KF4_ND_LOG=1` printed no `[nd] block ... refused` line). No draw lost.
- **Compare mode, 1x and 4x:** 56 of 64 maps are 0 pixels in all their views at both scales. The 8 that
  differ, by cause:
  1. **Map 000's floor band** (P1-1's, known): v0 only, 1-2 levels, native always brighter.
  2. **Wave-UV blocks a game frame apart** (039, and 040 through its neighbour 039): the whole screen,
     the native picture of vblank f is the PS2 path's of f-2, exactly.
  3. **Wave-UV water near the camera** (027, 042, 114; 034 at its horizon): sparse noise of a few levels
     inside the water's triangles, the same frame, nothing else.
  4. **One scanline at 4x** (030): a floor/wall junction row, 4x only.
- **Onoff mode (what a player sees) has a bug the compare mode hides:** the CPU near cut of a native record
  draws with VU1 memory the record's own packet no longer sets (lights, fog, colour scale). Bright walls at
  screen edges on 016, 022, 099, 102, 106; whole views black (worst 255) on 027 and 037 after a second quick
  load. See "Onoff" below. This one matters most before `map` can be accepted.

## The table

Views: how many, and the places. Parsed / refused: meshes parsed (1x compare, the native run; re-parses of
blocks whose bytes change count each time) / blocks refused in any run. Drawn: draws drawn / queued in the
1x compare's native run (the rest belong to frames a headless run never draws; lost is 0 everywhere).
Then the pixels that differ over all views (the views that differ; the worst difference in any channel).

| map | name | views | parsed / refused | drawn | compare 1x | compare 4x | onoff 1x |
|---|---|---|---|---|---|---|---|
| 000 | Village Ruins | 8: p0 start; p1 item 2 | 892 / 0 | 12617/12883 | 2054 (v0; worst 2) | 35102 (v0; worst 2) | 40964 (v0 v3; worst 2) |
| 002 | Miner's Cemetery | 8: p0 start; p1 item 1 | 972 / 0 | 14875/15195 | 0 | 0 | 0 |
| 004 | East Valley | 8: p0 start; p1 item 0 | 469 / 0 | 8582/8759 | 0 | 0 | 0 |
| 005 | Catacombs (Giant Spider's Lair) | 8: p0 start; p1 item 3 | 1478 / 0 | 16775/17121 | 0 | 0 | 0 |
| 006 | The King's Throne, Upper | 8: p0 start; p1 item 3 | 935 / 0 | 21776/22220 | 0 | 0 | 0 |
| 007 | The King's Throne, Lower | 8: p0 start; p1 item 0 | 715 / 0 | 21217/21647 | 0 | 0 | 0 |
| 008 | Upper Mines | 8: p0 start; p1 item 0 | 997 / 0 | 19233/19619 | 0 | 0 | 0 |
| 009 | Lower Mines | 8: p0 start; p1 item 0 | 681 / 0 | 18759/19144 | 0 | 0 | 0 |
| 011 | The Armory | 8: p0 start; p1 item 1 | 399 / 0 | 8012/8176 | 0 | 0 | 0 |
| 012 | The Ancient City Exterior | 8: p0 start; p1 item 0 | 753 / 0 | 13233/13503 | 0 | 0 | 0 |
| 013 | The Holy Forest | 8: p0 start; p1 item 0 | 1499 / 0 | 11736/11980 | 0 | 0 | 0 |
| 014 | The Heart of the Forest | 8: p0 start; p1 item 0 | 936 / 0 | 29480/30079 | 0 | 0 | 0 |
| 015 | The Ancient City, Level 3, West | 8: p0 start; p1 item 1 | 1336 / 0 | 15827/16148 | 0 | 0 | 0 |
| 016 | South Cliffs | 8: p0 start; p1 item 0 | 764 / 0 | 12187/12435 | 0 | 0 | 27141 (v5; worst 10) |
| 017 | The Forge | 8: p0 start; p1 item 1 | 574 / 0 | 11945/12197 | 0 | 0 | 0 |
| 018 | The Forge | 8: p0 start; p1 record 0 | 623 / 0 | 10074/10280 | 0 | 0 | 0 |
| 019 | The Forge | 8: p0 start; p1 item 1 | 685 / 0 | 9557/9756 | 0 | 0 | 0 |
| 020 | The Earth Folks' Village | 8: p0 start; p1 item 0 | 1473 / 0 | 23351/23804 | 0 | 0 | 0 |
| 021 | The Ocean Fortress | 8: p0 start; p1 record 26 | 1288 / 0 | 15212/15522 | 0 | 0 | 0 |
| 022 | The Queen's Palace | 8: p0 start; p1 item 0 | 1216 / 0 | 17905/18265 | 0 | 0 | 40883 (v5 v7; worst 170) |
| 023 | The Widda Queen's Lair | 8: p0 start; p1 item 0 | 1441 / 0 | 13981/14272 | 0 | 0 | 0 |
| 024 | The Cavern of Lava | 8: p0 start; p1 item 0 | 742 / 0 | 9881/10075 | 0 | 0 | 0 |
| 025 | The Fire Lord's Domain | 8: p0 start; p1 item 0 | 1050 / 0 | 11740/11979 | 0 | 0 | 0 |
| 026 | Cliff Passage | 8: p0 start; p1 item 0 | 1399 / 0 | 22926/23405 | 0 | 0 | 0 |
| 027 | Ocean Shore | 8: p0 start; p1 item 6 | 10950 / 0 | 26699/27250 | 2478 (v4 v5 v7; worst 3) | 39762 (v4 v5 v7; worst 4) | 2270921 (v0 v1 v2 v3 v4 v5 v6 v7; worst 255) |
| 028 | Seabed Cave | 8: p0 start; p1 item 1 | 933 / 0 | 11231/11460 | 0 | 0 | 1 (v0; worst 1) |
| 029 | The Underground Canal | 8: p0 start; p1 item 0 | 7606 / 0 | 4510/4608 | 0 | 0 | 0 |
| 030 | The Passage of Light | 8: p0 start; p1 record 8 | 800 / 0 | 17069/17414 | 0 | 474 (v3 v5; worst 7) | 0 |
| 031 | North Cliffs | 8: p0 start; p1 item 0 | 865 / 0 | 19012/19419 | 0 | 0 | 0 |
| 032 | The Egg Mines | 8: p0 start; p1 item 0 | 1423 / 0 | 16070/16416 | 0 | 0 | 0 |
| 033 | The Mansion of Howling Winds, Exterior | 8: p0 start; p1 item 0 | 880 / 0 | 13514/13789 | 0 | 0 | 0 |
| 034 | The Mansion of Howling Winds, Interior | 8: p0 start; p1 item 3 | 2047 / 0 | 14073/14370 | 3 (v1; worst 1) | 51 (v1; worst 1) | 3 (v1; worst 1) |
| 035 | The Guardian's Room | 8: p0 start; p1 item 0 | 337 / 0 | 11497/11733 | 0 | 0 | 0 |
| 036 | The Underground Jail | 8: p0 start; p1 item 0 | 854 / 0 | 13772/14054 | 0 | 0 | 0 |
| 037 | The Old Battlefield | 8: p0 start; p1 item 0 | 4212 / 0 | 10454/10673 | 0 | 0 | 2271271 (v0 v1 v2 v3 v4 v5 v6 v7; worst 255) |
| 038 | The Dark Sanctuary | 8: p0 start; p1 record 17 | 533 / 0 | 7532/7685 | 0 | 0 | 0 |
| 039 | The Dark Reality, Entrance | 8: p0 start; p1 record 39 | 114361 / 0 | 7913/8084 | 527089 (v0 v1 v2 v3 v4 v5 v6 v7; worst 8) | 9245516 (v0 v1 v2 v3 v4 v5 v6 v7; worst 11) | 697537 (v0 v1 v2 v3 v4 v5 v6 v7; worst 64) |
| 040 | The Dark Reality, Conclusion | 8: p0 start; p1 record 8 | 42032 / 0 | 4539/4633 | 145556 (v2 v6; worst 17) | 2329527 (v2 v6; worst 24) | 145556 (v2 v6; worst 17) |
| 041 | The King's Throne, Alcoves | 8: p0 start; p1 item 1 | 1066 / 0 | 11272/11513 | 0 | 0 | 0 |
| 042 | The Sanctuary of Light | 4: p0 start | 38663 / 0 | 7468/7621 | 4374 (v0 v2 v3; worst 13) | 70454 (v0 v2 v3; worst 14) | 4404 (v0 v2 v3; worst 18) |
| 043 | The Queen's Palace, Catacombs | 8: p0 start; p1 item 1 | 476 / 0 | 10418/10631 | 0 | 0 | 0 |
| 044 | The Snake Dens | 8: p0 start; p1 item 0 | 817 / 0 | 10552/10767 | 0 | 0 | 0 |
| 095 | Upper and Lower Underground Graveyard | 8: p0 start; p1 item 3 | 485 / 0 | 10596/10826 | 0 | 0 | 0 |
| 096 | The Shop | 8: p0 start; p1 item 1 | 359 / 0 | 8031/8197 | 0 | 0 | 0 |
| 097 | Icon of Healing | 8: p0 start; p1 item 2 | 299 / 0 | 7956/8119 | 0 | 0 | 0 |
| 098 | Palace of Oath Interior | 8: p0 start; p1 item 1 | 1088 / 0 | 18064/18430 | 0 | 0 | 0 |
| 099 | Palace of Oath Exterior | 8: p0 start; p1 item 0 | 1064 / 0 | 14707/15008 | 0 | 0 | 18592 (v2 v7; worst 201) |
| 100 | The Ancient City, Level 3, Central Tower | 8: p0 start; p1 record 26 | 1144 / 0 | 13320/13608 | 0 | 0 | 0 |
| 101 | The Ancient City, Level 3, North | 8: p0 start; p1 item 1 | 992 / 0 | 13131/13408 | 0 | 0 | 0 |
| 102 | The Ancient City, Level 3, East | 8: p0 start; p1 item 0 | 951 / 0 | 8577/8754 | 0 | 0 | 264 (v6; worst 29) |
| 103 | The Ancient City, Level 3, East Catacombs | 8: p0 start; p1 item 0 | 521 / 0 | 3389/3457 | 0 | 0 | 0 |
| 104 | The Ancient City, Level 3, South, Craftsman's Room | 8: p0 start; p1 item 0 | 972 / 0 | 8289/8461 | 0 | 0 | 0 |
| 105 | The Ancient City, Level 3, South | 8: p0 start; p1 item 0 | 722 / 0 | 10491/10711 | 0 | 0 | 0 |
| 106 | The Ancient City, Level 3, South, Widda Areas | 8: p0 start; p1 item 1 | 764 / 0 | 10331/10540 | 0 | 0 | 8893 (v5; worst 76) |
| 107 | The Ancient City, Level 2, Central Tower | 8: p0 start; p1 item 2 | 895 / 0 | 14698/15001 | 0 | 0 | 0 |
| 108 | The Ancient City, Level 2, South | 8: p0 start; p1 item 0 | 1178 / 0 | 13314/13584 | 0 | 0 | 0 |
| 109 | The Ancient City, Level 2, West | 8: p0 start; p1 item 5 | 934 / 0 | 14899/15202 | 0 | 0 | 2 (v7; worst 1) |
| 110 | The Ancient City, Level 2, West, Sorcerer's Area | 8: p0 start; p1 record 4 | 889 / 0 | 14009/14287 | 0 | 0 | 0 |
| 111 | The Ancient City, Level 2, North | 8: p0 start; p1 item 0 | 1378 / 0 | 11191/11430 | 0 | 0 | 0 |
| 112 | The Ancient City, Level 1, Central Tower | 8: p0 start; p1 item 0 | 766 / 0 | 7912/8074 | 0 | 0 | 0 |
| 113 | The Ancient City, Level 1, West | 8: p0 start; p1 item 1 | 706 / 0 | 6978/7129 | 0 | 0 | 0 |
| 114 | The Ruins of the Dark Folk | 8: p0 start; p1 item 0 | 9574 / 0 | 17028/17376 | 1234 (v2 v3 v7; worst 4) | 19226 (v2 v3 v7; worst 5) | 1234 (v2 v3 v7; worst 4) |
| 115 | The Ancient City, Level 1 and 2, East | 8: p0 start; p1 item 1 | 564 / 0 | 7418/7571 | 0 | 0 | 0 |
| 116 | The Treasury | 8: p0 start; p1 item 1 | 523 / 0 | 13812/14095 | 0 | 0 | 0 |

## Every difference: where, which draw, the likely cause

Boxes are 1x pixels (x0-x1, y0-y1 of the 640x448 picture) unless marked 4x. "Wave UV" = a map record with
flag 0x100: `func_00112BD0` calls `Wobble_ApplyWaveUvs` on it every frame, which rewrites the ST of every
vertex of its blocks in place (the block is the same EE memory the PS2 path's packet points at with a REF
tag). Maps with wave-UV records: 013 (1), 027 (2), 029 (2), 034 (6), 037 (2), 039 (77), 041 (1), 042 (10),
114 (1), 116 (1). Every map in classes 2 and 3 below has them; no map without them (and not next to one)
has a difference of that kind.

### 1. Map 000, v0: the floor band (P1-1)

- 1x: 2,054 pixels, 220 by more than 1, worst 2, in x 0-314, y 267-317; 4x: 35,102, worst 2, same place.
  Every differing pixel is brighter in the native picture (2,054 brighter, 0 darker): systematic, not noise.
- Records: the floor of the start view, records without flag 0x40, so ones the CPU near cut works on.
  `--native-layer-z ps2` does not change it (P1-1's note). P1-1's text has the candidates (a batch
  decision, or the near cut's pieces meeting native triangles); nothing here narrows it further, except
  that the other start-area views (v1-v7) are exact.
- The onoff run has a second band on 000 (v3, 35,425 pixels, worst 2, y 289-368) that compare mode does
  not: the near cut's VU1 memory (section "Onoff").

### 2. Maps 039 and 040: the wave-UV blocks are a game frame apart

- 039, every view: 34,922-110,507 pixels a view at 1x (worst 4-8), the whole screen (walls and floor, the
  map's 77 wave-UV records cover it). 4x: 0.64-1.77 million a view, worst 11.
- 040, v2 and v6 (looking back towards 039): 145,066 pixels at 1x, worst 17; v6 only at 4x (8,377).
  040 has no wave-UV records; the quick load sets up its neighbour 039 (the `--log`: "map 28 is a dead
  end: the map it is entered from, 27, set up", tags in hex), whose records are what differs.
- The proof: a static view (039 v0 and 040 v2, a single quick load, every vblank 300-312). The native
  picture at vblank f equals the PS2 path's at f-2 to the pixel (0 pixels; f-2 is the previous game
  frame) for all 6 pairs on 040 and 3 of 6 on 039 (the other three: 741-57,153 pixels, still by far the
  closest), while consecutive PS2 pictures differ by 130,000+ pixels and the same vblank by 71,000-175,000. The rest of the scene is still, so the only thing that differs is the wave's phase.
  (`D:/hl-nd/p1-2/sweep/x039b`, `x040`, `lag.py`.)
- Likely cause: the native draw parses the block when the game calls the draw (after this frame's
  `Wobble_ApplyWaveUvs`), and its mesh holds those bytes; the PS2 path's packet only points at the block,
  and VU1 reads it when the DMA controller's thread runs the chain (`Packet_SendChain` at the top of the
  next loop pass queues it; `port/platform/dmac.c` runs it on its own thread), by which time the next
  frame's `func_00112BD0` has written the next phase. Which of the two the PS2 itself showed depends on
  how far its DMA had got; the native one is the frame's own state. Why the water of 042/114/027 (also
  wave UV) is not a frame apart is not explained yet (perhaps how much of the chain is left when the game
  reaches `func_00112BD0`: 039 is the heaviest map for parsing, 114,361 re-parses in the run).
- For P1-1: decide which is wanted. To match the PS2 path, a wave-UV block would have to be read when the
  chain is run (by the GS side, at the `0x7B` write) rather than at the call; to keep the native one, the
  comparison needs to know (exclude flag-0x100 records, or compare against f-2 there).

### 3. Maps 027, 042, 114, 034: noise in wave-UV water near the camera

| map | view | 1x pixels (>1, worst) | box | 4x pixels (worst) |
|---|---|---|---|---|
| 027 | v4, v5, v7 | 16 (0, 1); 55 (4, 2); 2,407 (255, 3) | x 17-373 y 248-255; x 0-12 y 421-447; x 392-608 y 253-447 | 191; 871; 38,700 (4) |
| 042 | v0, v2, v3 | 287 (10, 2); 647 (58, 2); 3,440 (959, 13) | x 139-217 y 422-447; x 400-453 y 382-447; x 50-603 y 301-447 | 4,862; 10,456; 55,136 (14) |
| 114 | v2, v3, v7 | 3; 7; 1,224 (240, 4) | x 600-604 y 278-279; x 127-151 y 327-340; x 140-235 y 382-447 | 24; 47; 19,155 (5) |
| 034 | v1 | 3 (0, 1) | x 363-391 y 254-257 | 51 (1) |

- What it looks like: sparse single pixels, as many brighter as darker (042 v3: 1,757 / 1,635; 114:
  617 / 619; 027: 1,149 / 1,082), all inside the water surface (the translucent, additive wave-UV
  records), mostly its part nearest the camera at the bottom of the screen; the far water of the same
  records is exact. 034's three pixels are its water at the horizon. Crops:
  `D:/hl-nd/p1-2/sweep/look_042.png`, `look_027.png`, `look_114.png`, `look_034_ctx.png` (top: PS2
  picture, x3; bottom: the difference x60 in red).
- Not a frame apart: on static views (vblanks 300-312, `x027v7-s1`, `x042v3-s1`, `x114v7-s1`) the same
  vblank is by far the closest, in the water's box too. Stable from run to run (the same pictures twice).
  Not the layer rules (`--native-layer-z ps2`: 3,398 for 3,399) nor where shader blending copies
  (`KF4_GSN_BLEND_COPY=1`: unchanged). `--kinds items` alone: 0, so it is the map records.
- Likely cause: the texture coordinates of these triangles differ in their last bits between the two
  paths, which shows only where the texture is magnified (level 0, near) and high in contrast. Candidates
  for P1-1: (a) the corner order the native batch gives the GPU for a triangle differs from the PS2 path's
  for these strips, so the interpolated S/T/Q round differently (it would show most where S and T are
  large: `Wobble_ApplyWaveUvs` adds the wave as a u16, so wave UVs can wrap to near 16.0); (b) the near
  cut: the near water is what crosses the guard band, so `ModelScreen_CutClippedTriangles`' pieces (PS2
  path, program 0x280) draw over the native triangles there, and if the native and VU1 ADC drops do not
  agree the pieces and a native triangle overlap in the additive water. The evidence leans to (a) (no
  doubled brightness, symmetric noise); P0-2's oracle on these batches would settle it.

### 4. Map 030, 4x only: one scanline

- v3: 380 pixels at 4x, worst 7, all in row y 1010, x 975-1383; v5: 94 pixels, row y 971, x 1527-1641.
  1x: 0. Crops: `D:/hl-nd/p1-2/sweep/look_030_520.png`, `look_030_ctx.png` (the row marked red).
- Where: the line where the corridor's floor meets its end wall, far away, at 4x. Pixel centres on that
  row lie on (or within a rounding of) the edge between the two surfaces, and the two paths give the row
  to different triangles. Likely cause: an edge tie, the native triangle's corners in another order or
  winding than the PS2 path's for that batch, so the rasteriser's tie rule (or the 4x positions' last
  bit) goes the other way. For P1-1, with class 3's (a): the same question, the corner order.

### Onoff: the near cut uses VU1 memory a native record no longer sets

`--mode onoff` (`--native-draw off` against `map,items`; the PS2 packets of the native draws are not
queued, so VU1 does not run them). Compare mode hides this, because there VU1 still runs every packet.

| map | views | 1x pixels (worst) | what |
|---|---|---|---|
| 027 | v1-v7 (all but v0) | 2,270,921 (255) | the whole picture black but for the HUD in alternate lines |
| 037 | v1-v7 | 2,271,271 (255) | the same |
| 022 | v5, v7 | 40,883 (170) | a bright textured strip along the right/left edge (x 593-639; x 0-78) |
| 099 | v2, v7 | 18,592 (201) | the same (x 536-639; x 602-639) |
| 016 | v5 | 27,141 (10) | the left edge, x 0-153 |
| 106 | v5 | 8,893 (76) | the left edge, x 0-19 |
| 102 | v6 | 264 (29) | x 365-375, y 0-43 |
| 000 | v3 | 35,425 (2) | a band of the floor, y 289-368 (besides class 1) |
| 028, 109 | v0; v7 | 1; 2 (1) | single pixels |

(039, 040, 042, 114 and 034 are as in compare mode, plus a little: 039 worst 64.)

- The `--log` of an off and a `map,items` run on 027 are identical (the game plays the same), and the
  first view is exact: the pictures are wrong, not the game. 027's black view repeats in every run; it
  needs both kinds native and a second quick load (`map` alone or `items` alone: right; one load: right;
  the same save loaded twice: black). Evidence: `D:/hl-nd/p1-2/sweep/g027/map/frame_000320.png` (black)
  against `g027/off/`; `look_onoff_022.png`, `look_onoff_099.png` (left off, right on).
- The strips are at the screen's edges, on walls next to the camera: geometry crossing the guard band,
  where `Map_DrawRecords` calls `ModelScreen_CutClippedTriangles` after the (native) draw.
- Likely cause: the near cut's packets (`ModelScreen_QueueCutTriangle`, program 0x280) send only the
  part's matrix (VU 0x04-0x07). Everything else they use is what `Model_DrawPosed`'s first packet left in
  VU1 memory: the light directions and colours (0x0C-0x13), fog (0x3C) and FOGCOL (0x3D), the point
  lights (0x40-0x48) and the colour scale and bias (0x64, 0x65; 0x280 multiplies every colour by them).
  `NativeDraw_Posed` replaces `draw_parts`, so with the record native that packet is never queued, and the
  cut pieces are lit and coloured by whatever model the PS2 path drew last: an item, an entity, an effect
  (a colour scale of 0 or a full fog gives black; another model's lights give the bright walls). Which
  model was last depends on what else is native, hence `map,items` and not `map` on 027.
- For P1-5 (the near cut) / P1-1: when a native record will be cut (`Map_ClassifyRecordOnScreen` returns
  0), still queue `Model_DrawPosed`'s first packet (the VU1 fixed block, without the parts' block
  references), or draw the cut natively too. Then the onoff sweep should match the compare sweep.
- The compare mode's reference is not quite the off picture either: on 042 v3, the PS2 side of
  `--kinds map,items` and of `--kinds items` differ by 11 pixels (worst 2) on the edges of the columns in
  the water, and its native GS draws 47,057 batches against 21,185 (the ALT marks around every record's
  packets split the batches the layer rules look at). Small, but a reason to judge acceptance with
  onoff once the near cut is fixed.

## Other notes

- Quick loads: maps 002, 025 and 096 have one view each where the made-up place is moved to the floor a
  little ("loaded, but the player is at ..."); harmless. 042 has one place only (4 views). Some views are
  dark or mostly one colour (016, 038, 041: corridors, the void past a wall); the pictures are still the
  same in both runs.
- Maps 013, 029, 037, 041 and 116 have wave-UV records and differ in no view: the water was not in
  view, or far.
- To run it again: `D:/hl-nd/p1-2/sweep/sweep.py` (needs the base save; `SWEEP_MODE=onoff` for onoff) and
  `table.py` for this table.
