# Native draw: the 64-map sweep

`--native-draw map,items` on every map of the disc, against the PS2 path, in compare mode **and** onoff mode,
at 1x and 4x. No code changed; this is the measurement and where each difference comes from.

This is the second run (B-sweep, branch `nd2/b-sweep` from `native/draw` 95d27ec, the tree after P1-1 to
P1-6 were merged, 2026-10-23). The first run (P1-2, `nd/p1-2` from 4d35c35, before P1-1, P1-3 and P1-5)
is summarised at the end, under "Since P1-2".

## How it was run

- **Views:** the map tour's places (`tools/map_tour.py`: `map_places(data, 2)`, the map's start and one more
  place, at 4 headings each = 8 views; map 042 has only its start, 4 views). Each view is a made-up quick
  save (`write_made_up`, from P1-2's base save, taken with `tests/newgame.pad --quick-save-at 1900`). The
  views are loaded one after another in one run with `--quick-load` (the first on the title screen, then
  every 100 vblanks). The picture is taken 90 vblanks after each load (vblanks 220, 320 ... 920 = views
  v0 ... v7). The views and saves are the same as P1-2's.
- **Compare:** per map, scale and mode, `tools/native_draw_compare.py --kinds map,items --scale 1|4 --input
  title.pad --frames 220,320,...,920 [--mode onoff] -- --quick-load ...`, with `KF4_ND_LOG=1`. Compare mode:
  both runs queue both kinds of draw, and the native GS draws one set. Onoff: `--native-draw off` against
  `map,items`, where the PS2 packets of native draws are not queued at all. Onoff is what a player gets.
- 64 maps x 4 (compare 1x, compare 4x, onoff 1x, onoff 4x) = 256 comparisons and 512 headless runs, with
  `--no-audio` and a fresh `KF4_DATA_DIR`, two games at a time. A comparison took 4-19 s. Other agents shared
  the PC and GPU, so the times are noisy and are not measurements.
- The outputs are game content, so they stay outside the repository, in
  `C:/Users/Sean/AppData/Local/Temp/claude/c--code/1b2641ae-8113-4bcf-89ab-082664956342/scratchpad/nd2_b-sweep/`: `sweep/NNN/{c1,c4,o1,o4}/report.txt`, the runs' `log.txt`, and for each view that
  differs a `look_*.png` (the box: A | B | difference x60) and `diff_*.npz`. The frame dumps were deleted once
  each comparison had been read. The diagnosis pictures are in `evidence/`; the driver is `sweep.py`
  (`python sweep.py all [compare:1,onoff:1,...]`) and `table.py` makes the table.

## In short

- **Refusals: none. Lost: none.** Every block of every map parses (`0 refused` in all 256 comparisons, and
  no `[nd] block ... refused` line). Every view loaded its map. Maps 002, 025 and 096 each have one view
  where the made-up place is moved to the floor a little; that is harmless.
- **Compare mode, 1x and 4x:** 58 of 64 maps are 0 pixels in every view at both scales. Map 000's floor band
  and map 030's 4x scanline are gone: P1-1's alpha fix covers both. The 6 maps that still differ all have
  wave-UV records, with the same views, pixel counts and boxes as P1-2:
  1. **039 and 040: the wave-UV blocks a game frame apart.** On 039 the count changes from run to run,
     because the PS2 path's picture itself depends on the DMA thread's timing.
  2. **027, 042, 114, 034: sparse noise in wave-UV water near the camera.**
- **Onoff mode, 1x and 4x:** the near-cut lighting bug that P1-2 found is fixed (P1-5's fixed-block
  packet). 016, 022, 099, 102 and 106 are 0, and so is 000's second band. 027 and 037 are no longer black.
  55 of 64 maps are 0 at 1x and 54 at 4x. Besides the wave-UV maps above, 4 maps differ in onoff only:
  037 (a band, 1-3 levels), 028, 109 and 009 (a few edge pixels, at most 15 levels). 042 differs a little
  more in onoff than in compare mode. **All of these have one cause: a native draw splits the GS's batch
  where the PS2 path's batch goes on.** This is P1-1's "mixed batches" follow-up, and it is no longer only
  theoretical (section 3 below). Compare mode cannot show it: there, `RD_ND_ALT_BEGIN` flushes the batch
  before the PS2 path's packets of every native draw, so the reference is split in the same places.

## The table

Views: how many there are, and the places. Parsed / refused: meshes parsed in the 1x compare's native run
(re-parses of blocks whose bytes change count each time) / blocks refused in any of the four comparisons.
Drawn: draws drawn / queued in that run; the rest belong to frames a headless run never draws, and lost is
0 everywhere. Then, per comparison, the pixels that differ over all views (which views differ; the worst
difference in any channel). The 4x counts are of 4x pictures.

| map | name | views | parsed / refused | drawn | compare 1x | compare 4x | onoff 1x | onoff 4x |
|---|---|---|---|---|---|---|---|---|
| 000 | Village Ruins | 8: p0 start; p1 item 2 | 1706 / 0 | 15221/15543 | 0 | 0 | 0 | 0 |
| 002 | Miner's Cemetery | 8: p0 start; p1 item 1 | 1462 / 0 | 15913/16256 | 0 | 0 | 0 | 0 |
| 004 | East Valley | 8: p0 start; p1 item 0 | 1027 / 0 | 10140/10349 | 0 | 0 | 0 | 0 |
| 005 | Catacombs (Giant Spider's Lair) | 8: p0 start; p1 item 3 | 2208 / 0 | 17715/18080 | 0 | 0 | 0 | 0 |
| 006 | The King's Throne, Upper | 8: p0 start; p1 item 3 | 935 / 0 | 21776/22220 | 0 | 0 | 0 | 0 |
| 007 | The King's Throne, Lower | 8: p0 start; p1 item 0 | 1117 / 0 | 22551/23009 | 0 | 0 | 0 | 0 |
| 008 | Upper Mines | 8: p0 start; p1 item 0 | 1243 / 0 | 19689/20084 | 0 | 0 | 0 | 0 |
| 009 | Lower Mines | 8: p0 start; p1 item 0 | 681 / 0 | 18759/19144 | 0 | 0 | 0 | 1 (v0; worst 1) |
| 011 | The Armory | 8: p0 start; p1 item 1 | 777 / 0 | 8648/8825 | 0 | 0 | 0 | 0 |
| 012 | The Ancient City Exterior | 8: p0 start; p1 item 0 | 1682 / 0 | 14547/14844 | 0 | 0 | 0 | 0 |
| 013 | The Holy Forest | 8: p0 start; p1 item 0 | 1848 / 0 | 12807/13074 | 0 | 0 | 0 | 0 |
| 014 | The Heart of the Forest | 8: p0 start; p1 item 0 | 936 / 0 | 29480/30079 | 0 | 0 | 0 | 0 |
| 015 | The Ancient City, Level 3, West | 8: p0 start; p1 item 1 | 2465 / 0 | 17609/17966 | 0 | 0 | 0 | 0 |
| 016 | South Cliffs | 8: p0 start; p1 item 0 | 1105 / 0 | 13110/13377 | 0 | 0 | 0 | 0 |
| 017 | The Forge | 8: p0 start; p1 item 1 | 1381 / 0 | 13430/13713 | 0 | 0 | 0 | 0 |
| 018 | The Forge | 8: p0 start; p1 record 0 | 1403 / 0 | 12269/12520 | 0 | 0 | 0 | 0 |
| 019 | The Forge | 8: p0 start; p1 item 1 | 1689 / 0 | 11879/12126 | 0 | 0 | 0 | 0 |
| 020 | The Earth Folks' Village | 8: p0 start; p1 item 0 | 1749 / 0 | 23882/24347 | 0 | 0 | 0 | 0 |
| 021 | The Ocean Fortress | 8: p0 start; p1 record 26 | 1682 / 0 | 16581/16919 | 0 | 0 | 0 | 0 |
| 022 | The Queen's Palace | 8: p0 start; p1 item 0 | 1557 / 0 | 18405/18775 | 0 | 0 | 0 | 0 |
| 023 | The Widda Queen's Lair | 8: p0 start; p1 item 0 | 1794 / 0 | 14349/14648 | 0 | 0 | 0 | 0 |
| 024 | The Cavern of Lava | 8: p0 start; p1 item 0 | 1412 / 0 | 11754/11986 | 0 | 0 | 0 | 0 |
| 025 | The Fire Lord's Domain | 8: p0 start; p1 item 0 | 1194 / 0 | 12123/12370 | 0 | 0 | 0 | 0 |
| 026 | Cliff Passage | 8: p0 start; p1 item 0 | 1732 / 0 | 23878/24377 | 0 | 0 | 0 | 0 |
| 027 | Ocean Shore | 8: p0 start; p1 item 6 | 11675 / 0 | 28849/29444 | 2478 (v4 v5 v7; worst 3) | 39762 (v4 v5 v7; worst 4) | 2478 (v4 v5 v7; worst 3) | 39762 (v4 v5 v7; worst 4) |
| 028 | Seabed Cave | 8: p0 start; p1 item 1 | 1361 / 0 | 12214/12463 | 0 | 0 | 1 (v0; worst 1) | 55 (v0 v3; worst 15) |
| 029 | The Underground Canal | 8: p0 start; p1 item 0 | 46865 / 0 | 5858/5985 | 0 | 0 | 0 | 0 |
| 030 | The Passage of Light | 8: p0 start; p1 record 8 | 1074 / 0 | 17868/18229 | 0 | 0 | 0 | 0 |
| 031 | North Cliffs | 8: p0 start; p1 item 0 | 1206 / 0 | 19846/20271 | 0 | 0 | 0 | 0 |
| 032 | The Egg Mines | 8: p0 start; p1 item 0 | 1992 / 0 | 17127/17496 | 0 | 0 | 0 | 0 |
| 033 | The Mansion of Howling Winds, Exterior | 8: p0 start; p1 item 0 | 880 / 0 | 13514/13789 | 0 | 0 | 0 | 0 |
| 034 | The Mansion of Howling Winds, Interior | 8: p0 start; p1 item 3 | 2243 / 0 | 14739/15050 | 3 (v1; worst 1) | 51 (v1; worst 1) | 3 (v1; worst 1) | 51 (v1; worst 1) |
| 035 | The Guardian's Room | 8: p0 start; p1 item 0 | 1119 / 0 | 14765/15068 | 0 | 0 | 0 | 0 |
| 036 | The Underground Jail | 8: p0 start; p1 item 0 | 1149 / 0 | 14798/15101 | 0 | 0 | 0 | 0 |
| 037 | The Old Battlefield | 8: p0 start; p1 item 0 | 7579 / 0 | 14704/15013 | 0 | 0 | 20666 (v4 v5 v6; worst 2) | 331946 (v4 v5 v6; worst 3) |
| 038 | The Dark Sanctuary | 8: p0 start; p1 record 17 | 2042 / 0 | 9823/10022 | 0 | 0 | 0 | 0 |
| 039 | The Dark Reality, Entrance | 8: p0 start; p1 record 39 | 141829 / 0 | 8232/8410 | 590421 (v0 v1 v2 v3 v4 v5 v6 v7; worst 8) | 10008952 (v0 v1 v2 v3 v4 v5 v6 v7; worst 11) | 592904 (v0 v1 v2 v3 v4 v5 v6 v7; worst 8) | 9705518 (v0 v1 v2 v3 v4 v5 v6 v7; worst 11) |
| 040 | The Dark Reality, Conclusion | 8: p0 start; p1 record 8 | 42728 / 0 | 6409/6542 | 145556 (v2 v6; worst 17) | 2329527 (v2 v6; worst 24) | 145556 (v2 v6; worst 17) | 2329527 (v2 v6; worst 24) |
| 041 | The King's Throne, Alcoves | 8: p0 start; p1 item 1 | 1556 / 0 | 13366/13651 | 0 | 0 | 0 | 0 |
| 042 | The Sanctuary of Light | 4: p0 start | 38935 / 0 | 7858/8019 | 4374 (v0 v2 v3; worst 13) | 70454 (v0 v2 v3; worst 14) | 4378 (v0 v2 v3; worst 18) | 70535 (v0 v2 v3; worst 42) |
| 043 | The Queen's Palace, Catacombs | 8: p0 start; p1 item 1 | 547 / 0 | 10619/10836 | 0 | 0 | 0 | 0 |
| 044 | The Snake Dens | 8: p0 start; p1 item 0 | 1324 / 0 | 12074/12320 | 0 | 0 | 0 | 0 |
| 095 | Upper and Lower Underground Graveyard | 8: p0 start; p1 item 3 | 995 / 0 | 11701/11955 | 0 | 0 | 0 | 0 |
| 096 | The Shop | 8: p0 start; p1 item 1 | 646 / 0 | 8954/9139 | 0 | 0 | 0 | 0 |
| 097 | Icon of Healing | 8: p0 start; p1 item 2 | 369 / 0 | 8108/8274 | 0 | 0 | 0 | 0 |
| 098 | Palace of Oath Interior | 8: p0 start; p1 item 1 | 1155 / 0 | 18211/18580 | 0 | 0 | 0 | 0 |
| 099 | Palace of Oath Exterior | 8: p0 start; p1 item 0 | 1310 / 0 | 15179/15490 | 0 | 0 | 0 | 0 |
| 100 | The Ancient City, Level 3, Central Tower | 8: p0 start; p1 record 26 | 1430 / 0 | 14107/14412 | 0 | 0 | 0 | 0 |
| 101 | The Ancient City, Level 3, North | 8: p0 start; p1 item 1 | 1351 / 0 | 13691/13980 | 0 | 0 | 0 | 0 |
| 102 | The Ancient City, Level 3, East | 8: p0 start; p1 item 0 | 1016 / 0 | 8675/8854 | 0 | 0 | 0 | 0 |
| 103 | The Ancient City, Level 3, East Catacombs | 8: p0 start; p1 item 0 | 605 / 0 | 3579/3651 | 0 | 0 | 0 | 0 |
| 104 | The Ancient City, Level 3, South, Craftsman's Room | 8: p0 start; p1 item 0 | 972 / 0 | 8289/8461 | 0 | 0 | 0 | 0 |
| 105 | The Ancient City, Level 3, South | 8: p0 start; p1 item 0 | 722 / 0 | 10491/10711 | 0 | 0 | 0 | 0 |
| 106 | The Ancient City, Level 3, South, Widda Areas | 8: p0 start; p1 item 1 | 764 / 0 | 10331/10540 | 0 | 0 | 0 | 0 |
| 107 | The Ancient City, Level 2, Central Tower | 8: p0 start; p1 item 2 | 1215 / 0 | 15821/16147 | 0 | 0 | 0 | 0 |
| 108 | The Ancient City, Level 2, South | 8: p0 start; p1 item 0 | 1495 / 0 | 13852/14133 | 0 | 0 | 0 | 0 |
| 109 | The Ancient City, Level 2, West | 8: p0 start; p1 item 5 | 1230 / 0 | 15491/15806 | 0 | 0 | 2 (v7; worst 1) | 40 (v7; worst 2) |
| 110 | The Ancient City, Level 2, West, Sorcerer's Area | 8: p0 start; p1 record 4 | 1076 / 0 | 14449/14736 | 0 | 0 | 0 | 0 |
| 111 | The Ancient City, Level 2, North | 8: p0 start; p1 item 0 | 1828 / 0 | 11775/12030 | 0 | 0 | 0 | 0 |
| 112 | The Ancient City, Level 1, Central Tower | 8: p0 start; p1 item 0 | 1350 / 0 | 9335/9526 | 0 | 0 | 0 | 0 |
| 113 | The Ancient City, Level 1, West | 8: p0 start; p1 item 1 | 938 / 0 | 7893/8064 | 0 | 0 | 0 | 0 |
| 114 | The Ruins of the Dark Folk | 8: p0 start; p1 item 0 | 10614 / 0 | 19955/20363 | 1234 (v2 v3 v7; worst 4) | 19226 (v2 v3 v7; worst 5) | 1234 (v2 v3 v7; worst 4) | 19226 (v2 v3 v7; worst 5) |
| 115 | The Ancient City, Level 1 and 2, East | 8: p0 start; p1 item 1 | 857 / 0 | 8200/8369 | 0 | 0 | 0 | 0 |
| 116 | The Treasury | 8: p0 start; p1 item 1 | 993 / 0 | 14545/14843 | 0 | 0 | 0 | 0 |

## Every difference: where, which draw, the likely cause

Boxes are in the picture's own pixels (1x: 640x448; 4x: 2560x1792). "Wave UV" means a map record with flag
0x100: `func_00112BD0` calls `Wobble_ApplyWaveUvs` on it every frame, which rewrites the ST of every vertex
of its blocks in place. Maps with wave-UV records: 013 (1), 027 (2), 029 (2), 034 (6), 037 (2), 039 (77),
041 (1), 042 (10), 114 (1), 116 (1).

### 1. 039 and 040: the wave-UV blocks a game frame apart (compare and onoff)

| map | views | 1x | 4x |
|---|---|---|---|
| 039 | all 8 | 4,460-110,507 a view, worst 8, whole screen | 0.52-1.77 million a view, worst 11 |
| 040 | v2 (420), v6 (820) | v2 145,066 (worst 17, whole screen); v6 490 (worst 3, x 300-347 y 189-258) | v2 2,321,150 (24); v6 8,377 (4) |

- The same as P1-2. 040's views v2 and v6 look back towards 039, whose records the quick load sets up.
  P1-2's static-view proof stands: the native picture at vblank f equals the PS2 path's at f-2.
- **New: 039 is not deterministic.** Three compare 1x runs of the same views gave 590,421, 683,093 and
  653,644 pixels. P1-2 got 527,089. Views v0, v1, v3, v4 and v5 vary, while v2, v6 and v7 repeat exactly.
  The native side reads the block when the game calls the draw, so it is fixed. The PS2 path reads the
  block when the DMA thread runs the chain (`port/platform/dmac.c`), and that races with the next frame's
  `func_00112BD0`, so the PS2 path's own picture changes from run to run. That makes 039 a poor reference
  until the owner decides which read is wanted (NATIVE_DRAW_TASKS.md, "Wave UVs").
- Likely cause, for P1-1's owner: as P1-2 said. To match the PS2 path, read a wave-UV block when the chain is
  run (at the `0x7B` write on the GS side). To keep the native read, which is consistent within a frame, the
  comparison has to leave out flag-0x100 records or compare against f-2.

### 2. 027, 042, 114, 034: sparse noise in wave-UV water near the camera (compare and onoff)

| map | view | 1x pixels (>1; worst), box | 4x pixels (worst) |
|---|---|---|---|
| 027 | v4, v5, v7 | 16 (0; 1) x 17-373 y 248-255; 55 (4; 2) x 0-12 y 421-447; 2,407 (255; 3) x 392-608 y 253-447 | 191; 871; 38,700 (4) |
| 042 | v0, v2, v3 | 287 (10; 2) x 139-217 y 422-447; 647 (58; 2) x 400-453 y 382-447; 3,440 (959; 13) x 50-603 y 301-447 | 4,862; 10,456; 55,136 (14) |
| 114 | v2, v3, v7 | 3 x 600-604 y 278-279; 7 x 127-151 y 327-340; 1,224 (240; 4) x 140-235 y 382-447 | 24; 47; 19,155 (5) |
| 034 | v1 | 3 (0; 1) x 363-391 y 254-257 | 51 (1) |

- The counts and boxes are identical to P1-2's, pixel for pixel, in compare mode. Onoff has the same
  numbers, plus 042 v0's batch-split pixels (section 3). The diagnosis stands: single pixels, as many brighter
  as darker, inside the translucent additive water of wave-UV records, mostly the part nearest the camera
  where the texture is magnified. They are not a frame apart, not the layer rules and not the blend copies;
  `--kinds items` alone gives 0.
- Likely cause, for P1-1's owner (unchanged): (a) the corner order the native batch gives the GPU for these
  strips differs from the PS2 path's, so S/T/Q interpolate and round differently. Wave UVs can wrap to near
  16.0, where this shows most. Or (b) the near cut's pieces meet the native triangles in the additive water.
  The evidence leans to (a). P0-2's oracle on these batches, or a corner-order check, would settle it.
- 030's 4x scanline (P1-2's class 4, an edge tie at a floor/wall junction) is **gone** on the merged tree:
  0 pixels at 4x in both modes.

### 3. Onoff only: a native draw splits the batch where the PS2 path's goes on

| map | view | 1x pixels (>1; worst), box | 4x pixels (>1; worst), box | where |
|---|---|---|---|---|
| 037 | v4 (620) | 1,770 (183; 2) x 0-369 y 0-378 | 27,946 (2,672; 2) | the top edge of the near wall, against the ceiling |
| 037 | v5 (720) | 10,536 (966; 2) x 0-639 y 102-418 | 169,852 (15,748; 3) | a band along the hill's top edge, under the translucent overlay |
| 037 | v6 (820) | 8,360 (832; 2) x 95-639 y 0-29 | 134,148 (13,639; 2) | the ceiling's edge at the top of the screen |
| 028 | v0 (220) | 1 (0; 1) x 636 y 148 | 48 (31; 15) x 1658-2557 y 557-735 | the top edge of the cave mouth |
| 028 | v3 (520) | 0 | 7 (4; 4) x 1629-1694 y 11-52 | an edge at the top of the screen |
| 109 | v7 (920) | 2 (0; 1) x 605-615 y 162-221 | 40 (10; 2) x 2384-2546 y 591-1697 | the edge of the wall at the right |
| 009 | v0 (220) | 0 | 1 (0; 1) x 1072 y 49 | one edge pixel |
| 042 | v0 (220) | 5 more than compare (worst 18), x 139-634 y 31-447 | 83 more (worst 42), x 557-2555 y 6-1791 | tree-trunk edges (top right), besides the water |
| 039 | several | up to 15,000 a view more or less than compare | | mixed in with section 1 |

- **What it looks like:** pixels on silhouette edges, where one map record's triangles meet another's (028,
  109, 009, 042). On 037 it is a band 5-15 pixels wide where the near walls' top edges meet the ceiling,
  under the translucent purple overlay. There the off picture is 1-3 levels *darker* in every pixel (10,291
  darker, 0 brighter on v5 alone). The edge pixels differ by more, up to 15 levels on 028 and 42 on 042 at
  4x, because the other surface's colour wins the pixel. Pictures: `evidence/onoff_037v5_band.png` (off;
  off with the onoff difference in red; off with compare's own difference in green), and
  `evidence/onoff_028v0s4.png`, `onoff_109v7s4.png`, `onoff_042v0s1.png`, `onoff_028v3s4.png`,
  `onoff_009v0s4.png` (each differing pixel circled).
- **The proof:** a local diagnostic build, not committed. `KF4_DIAG_NOALTFLUSH` left out the `flush_batch()`
  at `RD_ND_ALT_BEGIN` in `nd_register`. The 9 differing views were each run alone (037 v5 1x; 028 v0 1x and
  4x; 028 v3 4x; 109 v7 1x and 4x; 009 v0 4x; 042 v0 1x and 4x), with these results:
  - compare mode without that flush gives the onoff numbers **exactly**. For example, 037 v5 is 10,291 /
    1,059 / worst 3 both ways, 028 v0 4x is 48 / 31 / 15 both ways, and 042 v0 4x is 4,945 / 303 / 42 both
    ways;
  - its PS2-side picture equals the `--native-draw off` picture, with 0 pixels different in all 9;
  - its native-side picture equals the `map,items` onoff picture, with 0 pixels different in all 9.

  So the native draws themselves are right, and so are the PS2 path's packets drawn in the same game. The
  only difference is where the batch ends: the PS2 path's batch runs across a native record, and the native
  draw's batch does not. With the flush (the default), compare mode's PS2 reference is cut at the start of
  every native draw's packets, so it matches the native picture. On 037 v5 the default compare mode's PS2
  side differs from the true off picture by 23,504 pixels: the band, and a second contour along the floor's
  edge.
- **Which draw:** on 037 v5, `KF4_ND_DUMP` listed the view's records. A diagnostic filter in the same local
  build (`KF4_DIAG_REC`) then made only some of them native, with these results:
  - only the wave-UV records native: 0;
  - every record except those native: the full 10,291;
  - any one of records 0xe14940, 0xe149c0 or 0xe14a40 (flags 0) native on its own: the whole band (10,291,
    10,291 and 10,165);
  - any one of the 5 records before them on its own: 0, and the same for the frame's first or last record.

  So the band belongs to one PS2-path batch that runs across those three consecutive records. In the off
  run it is one batch, and making any one of the three native cuts it into pieces. That changes the
  decision `flush_batch` makes for the batch as a whole. Only translucent pixels change, by a level or two,
  so that decision is most likely Vulkan blending against the shader's, chosen from the batch's alpha range
  or overlap runs. `--native-layer-z ps2` (10,273) and `KF4_GSN_BLEND_COPY=1` (10,291) do not change it.
  On the edges (028, 109, 009, 042), the same split decides which of two touching records' triangles wins
  a pixel on the shared edge.
- **For P1-1's owner** (the "mixed batches" follow-up in NATIVE_DRAW_TASKS.md): decide once for a batch of
  PS2-path and native corners, and draw it in two calls instead of splitting it. Onoff should then be 0 on
  028, 037, 109, 009 and 042 v0. **For whoever judges acceptance (P1-7):** the default compare mode cannot
  see this class. Judge `map` in onoff mode, or run compare mode without the flush at `RD_ND_ALT_BEGIN`,
  which reproduced onoff exactly on the views above.

## Since P1-2

| | P1-2 (4d35c35) | now (95d27ec) |
|---|---|---|
| refusals, lost | 0, 0 | 0, 0 |
| compare 1x and 4x: maps not 0 | 8 (000 band, 030 4x scanline, 027, 034, 039, 040, 042, 114) | 6 (027, 034, 039, 040, 042, 114: wave UV only) |
| onoff 1x: maps not 0 | 15; whole views black on 027 and 037, edge strips up to 255 | 9: the 6 above, plus 028, 037, 109 (batch split, at most 2 levels at 1x) |
| onoff 4x | not run | 10: as 1x, plus 009 (1 pixel); up to 15 levels on 028 and 42 on 042 |

Fixed since P1-2:
- map 000's band (P1-1, the alpha wrap);
- 030's 4x scanline (also gone since P1-1);
- the near cut's VU1 memory in onoff mode (P1-5, `queue_fixed_block`).

Still open:
- the wave-UV frame lag on 039 and 040 (the owner's call);
- the noise in wave-UV water on 027, 034, 042 and 114;
- the batch split, which this run found (028, 037, 109, 009, 042 v0).

## Other notes

- Some views are dark or mostly one colour (016, 038, 041: corridors, the void past a wall). The pictures are
  still the same in both runs.
- Maps 013, 029, 037, 041 and 116 have wave-UV records, but none of their views shows the wave-UV noise or
  lag: the water is out of view, or far away. 037's onoff band does not come from its wave-UV records (see
  above).
- A second run of compare 1x and onoff 1x on 027, 028, 037, 042, 109 and 009 gave the same numbers. Only
  039 varies (section 1).
