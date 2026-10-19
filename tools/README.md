# tools

| Tool | What |
|---|---|
| `patches.py` | the PC edits to the decomp: `apply`, `save`, `upgrade` (docs/port/README.md) |
| `port_census.py` | per folder of the game code, the errors and inline asm left (docs/port/RULES.md) |
| `port_abi_check.py` | declarations that disagree with the definition |
| `boot_test.py`, `frame_test.py` | step 1's and step 2's checks: a headless boot to a new game, and its pictures |
| `vu1_disasm.py` | the game's VU1 programs as text |
| `gsreplay/` | `kf4-gsreplay` (below) |

## kf4-gsreplay

Draws a GS frame capture again: the file F9 writes in the game (or `--gs-capture-at`), into the
`captures` folder of the data folder (docs/port/README.md, "Debugging"). It is built with the game
when paraLLEl-GS is checked out: `out/<preset>/kf4-gsreplay`. A capture is read by any build, so a
capture the player made is replayed in a build with more logging, a fix, or a change of the VU1
interpreter.

```
kf4-gsreplay FILE.kf4gs [options]
```

| Option | What |
|---|---|
| `--info` | the capture's vblanks, the game's state line (map, position), the build, the picture options, and its records |
| `--mode gif` | (the default) the GIF packets as the GS had them |
| `--mode vif` | the VIF1 stream through the VU1 interpreter, which makes the PATH1 and PATH2 packets again; each is compared with the recorded one, and for each that differs the first vertices that differ are printed (`--diff N`, 8 by default; 0 for none) |
| `--vu-round nearest` | with `--mode vif`: VU1's float arithmetic rounds to nearest instead of toward zero (a hypothesis to test: what moves) |
| `--frames` | each scanout's picture as `frame_NNNNNN.png`, as `--dump-frames` writes it (byte for byte the same) |
| `--out DIR` | where pictures go (the current directory) |
| `--list` | every draw, one a line (below) |
| `--draw N`, `--draws A-B` | the frame buffer the draw drew into, after draw N or after each of A to B, as `draw_NNNNN.png` |
| `--zbuf` | with them, its Z buffer too, as `draw_NNNNN_z.png`: grey from the nearest to the farthest Z in the picture, black where Z is 0 |
| `--pixel X,Y` | the draws whose box and scissor cover frame buffer pixel (X,Y), each with the pixel's colour (ABGR) and Z after it, and `changed` where either changed; `--fbp N` keeps only those drawing into frame buffer page N |
| `--vertices N` | draw N's vertices: X, Y (12.4, as sent, before XYOFFSET), Z, RGBA, STQ, UV, fog, and whether it kicked a primitive |
| `--compare OTHER` | another capture's draws against these, one by one: the GS state, and the vertices (the first that differs, and the largest Z difference) |
| `--compare-mode gif\|vif` | compare this capture with itself in that mode (with `--vu-round`: the VU1 variant against the recorded packets, draw by draw) |
| `--limit N` | how many differing draws `--compare` prints (50) |

A **draw** is a run of vertices in one GIF packet with no GS register written between them other than
a vertex's own (RGBAQ, ST, UV, FOG). `--list` gives, for each:

```
   412 rec   3120 p1 prog 000 kick   2 tstrip tex abe fog ctx0 v   30 k   10 box  -12, 180- 655, 300 z 0000001C-00FFFFC5 | FRAME fbp 0 fbw 10 psm 00 fbmsk 00000000 | ZBUF zbp 280 psm 3A zmsk 0 | TEST ate 1 atst 6 aref 00 afail 0 date 0 datm 0 zte 1 ztst 2 | ALPHA 0101 fix 80 fba 0 pabe 0 | TEX0 ... TEX1 ... CLAMP ... | SCISSOR 0,0-639,447 XYOFFSET 1728,1824
```

its number, the capture record it starts in, the path, for PATH1 the VU1 program's address (in
instructions, as `tools/vu1_disasm.py` numbers them) and which of its XGKICKs, PRIM (type, `tex`, `abe`
alpha blending, `fog`, the context), the vertices and the drawing kicks, the box in frame buffer pixels,
the Z range, and the GS state it drew with: FRAME, ZBUF, TEST (alpha test, destination alpha test, the
depth test: ZTST 0 never, 1 always, 2 greater or equal, 3 greater), ALPHA (A B C D and FIX), FBA,
PABE, TEX0, TEX1, CLAMP, SCISSOR and XYOFFSET.

Pictures need a Vulkan GPU; `--info`, `--list`, `--vertices` and `--compare` do not.

### Example: a flicker on the water

The healing spring's water flickers with dark lines as the view moves. Stand where it shows, press F9
(a few times: one may catch it and one not), then:

```sh
kf4-gsreplay gs_a.kf4gs --frames --out a              # find the capture that shows the lines
kf4-gsreplay gs_a.kf4gs --list > a.txt                # the draws: find the water's (its TEX0, its box)
kf4-gsreplay gs_a.kf4gs --pixel 300,260 --fbp 0       # which draws write a pixel on a line, their Z and ZTST
kf4-gsreplay gs_a.kf4gs --draws 600-640 --zbuf --out a    # the picture and Z after each of them
kf4-gsreplay gs_a.kf4gs --vertices 612                # the two passes' vertices: is Z the same?
kf4-gsreplay gs_a.kf4gs --compare-mode vif --vu-round nearest   # does VU rounding move their Z?
```
