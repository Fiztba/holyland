# The plan for the PC port

holyland is a native PC port built from the kfiv-decomp source. This page lists the steps in
order: what each one delivers, and how we check it. Each step ends in something we can run and
compare against the PS2.

The approach follows [ico-pc](https://github.com/nathanialf/ico-pc), the PC port of the ICO decomp:
- the game's own C is compiled for the PC, with no emulator or recompiler;
- `port/` stands in for the PS2 under it;
- the PC behaves like the PS2 by default, and every improvement is opt-in.

Windows and Linux are both first-class. Every step has to build and run on both.

## Step 0: native 64-bit build (done, branch `port/step0`)

The game code compiles and links as a 64-bit program with clang on Windows and gcc or clang on
Linux. It runs its start-up (system, display, sound driver, timers) until it asks for the disc.

What changed:
- The game code is converted to its PC form by the rules in [RULES.md](RULES.md):
  - addresses kept in integers;
  - structs read from files or saves keep their PS2 layout, and the others may grow;
  - inline asm and VU0 macro code are rewritten in C;
  - SDK calls go through PC headers.
- About 680 data symbols that only existed in the asm data files are now defined in C, in
  `game/data/`.
- Four independent reviews of the whole conversion found about 35 bugs the compiler cannot see.
  All are fixed. The worst were:
  - calls that only worked because the EE leaves unused arguments in registers;
  - local prototypes that pass arguments in the wrong positions on Windows;
  - globals that were one object on the PS2 but separate objects on the PC;
  - structs that outgrew their PS2-sized buffers.
- Two tools keep these from coming back:
  - `tools/port_census.py` counts what stops each folder compiling;
  - `tools/port_abi_check.py` finds declarations whose calling convention differs from the
    definition.

Still open:
- No tree-wide check yet for integer division by zero (x86 traps, the EE does not).
- Model work areas are sized for the PS2's worst case, not for the real part counts.
- PS2 float rounding is not modelled beyond the division and square-root edge cases.

## Step 1: boot to a new game, headless (done, branch `port/step1`)

Get from boot to the first map of a new game with nothing drawn, by making the stand-ins in
`port/sdk` real enough:

| Area | What it needs to do |
|---|---|
| Disc and files | `sceCd*` and `sceOpen`/`sceRead`, served from the extracted disc folder or the ISO, so `KF4.DAT`, the font, the menus and the maps load. |
| Threads and timing | Fibers with the EE kernel's rules: strict priority, switching only in kernel calls. Includes the game's two threads, semaphores, the vblank interrupt handler, `sceGsSyncV` pacing and timer 0. |
| DMA and the GS | DMA channels that finish, so the game's wait loops on `D1_CHCR`/`D2_CHCR`/`GIF_STAT` end. `sceDmaGetChan` has to use the same registers as `eeregs.h`. |
| IOP | Module loads and the IOP heap work; the sound driver's RPC accepts every command and reports done. |
| Memory card | "No card inserted". |
| Movies | Each movie ends at once. |
| Pad | SDL, plus scripted input for tests. |

**Check:** a headless run that is deterministic (the same input gives the same run) and logs the
game's state each frame: game status, current map and player position. It must show title →
New Game → first map. The same run then checks every later change.

Also to verify here: map scripts read and write the player, game-state and view-ray blocks by PS2
byte offsets. These are now translated to the PC fields (`Script_GameVarAt`).

Result:
- `tools/boot_test.py` runs the boot twice with `tests/newgame.pad` and checks it: the title loads at
  vblank 122, New Game's opening movie ends at 714, `099.map` loads at 716, and from frame 1 the
  player stands in map 0x63 with 50 HP. The opening walk runs, and the player has control at frame
  370. The two runs give the same log, and so do gcc and clang. 6000 vblanks take about 0.4 s.
- Every map's script was scanned for game-variable operands. The game-state block is used only
  below 0x40 and the player block only below 0x200, which are the same offsets on both. The
  view-ray block is never used. So no retail script reaches a translated field, and the translation
  is a safeguard only.
- Three game bugs the PC exposed, fixed in `patches/`:
  - `File_Read` reads whole sectors, so a file read into a static table runs past its end. Retail
    lays the tables out so that is harmless. Into the program's globals the PC reads only the bytes
    asked for, the last sector through a buffer of its own (`port_cd_read_bytes`, `libcdvd.h`); into
    game RAM, laid out as on the PS2, whole sectors as before. (Before, the save icon buffer was
    enlarged and GCC's `-fno-toplevel-reorder` kept `item_nam.prm`'s overrun in `item_common_defs`;
    the flag stays, so gcc and clang lay globals out alike.)
  - An effect with no magic reads `fx_table[0xFFFF]`, 17 MB past the table: that is game RAM on
    the PS2, and is read there (`EE_RAM_AT`).
  - `ModelInstance.found_record` takes an aligned 128-bit store, so it is 16-byte aligned.

Still open:
- Movies are not played: each one ends at once (`sceMpeg*`).
- The memory card is always empty, so saving and loading are untested.
- The sound driver's RPC answers every command with "done" and plays nothing.

## Step 2: graphics, stage 1 (the first picture)

Hand the game's GS packets to paraLLEl-GS, the accurate GS rasteriser. The packets come from:
- one VIF1 chain per frame, sent from `gfx/packet_buffer.c` through the four VU1 programs;
- the menus' GIF packets on channel 2.

The VU1 programs run on the CPU for now. This gives an accurate picture quickly, and it becomes
the reference the native renderer is compared against.

Result:
- The game is drawn from boot through the logos, the title menu, New Game, the brightness screen and
  the opening, to the first map with its HUD, in an SDL2 window at 59.94 vblanks a second, with a
  pad or the keyboard. `tools/frame_test.py` dumps five of those pictures, and every picture of
  vblanks 990 to 1010, twice and checks they are the same byte for byte; gcc and clang give the same
  pictures. `tools/boot_test.py`'s log is the same as step 1's.
- `port/platform/dmac.c` reads channel 1 and 2 transfers, in normal mode and as source chains (all
  eight tag IDs, TTE, TIE). A chain started with QWC set sends those qwords first and goes on only
  if CHCR's TAG says so; the game starts some with TAG 0, a REFE. Channel 2 ignores DIR:
  `ui/title.c` starts it with STR alone. A tag's address keeps all 32 bits: the program's sections
  have EE addresses from 0x40000000, and the menus' font is sent from one of them.
- A transfer takes no EE time, so the GS gets the transfers whole, in the order the game started
  them, whatever the controller's thread's timing: each start goes into a work queue, claimed by the
  game's thread at its next call into the host or by the controller if the game spins on STR first.
  PATH3 waits while it is masked (VIF1's MSKPATH3, GIF_MODE's M3R) and then goes between the other
  paths' GIF packets. A channel's end interrupt runs at the first kernel call after its start.
- `port/platform/vif1.c` reads every VIF code the game sends, and every UNPACK format, with masks,
  modes, skipping writes and double buffering. `port/platform/vu1.c` interprets VU1 cycle by cycle,
  because the programs are software-pipelined and rely on when each result lands: FMAC results 4
  cycles on (with stalls), Q and P late with no stall, the clip flag 4 cycles on, and a branch that
  reads the VI register the instruction before it wrote. `tools/vu1_disasm.py` prints the programs.
- `vu1.c` knows the whole VU1 instruction set, with the MAC, status and clip flags and their delay,
  each EFU op's latency and VU floats (exponent 255 is a number, overflow and division by zero give
  0x7FFFFFFF); an encoding that is no instruction stops the program. The game's programs use about
  forty of the instructions. `tests/vu1_test.c` (the target `vu1_test`) checks it against
  hand-written microcode.
- The program at 0x700 is used after all. No C code starts it, so data from the disc must, and its
  loop relies on that branch timing.
- `port/sdk/libgraph.c` is Sony's libgraph from the decomp. On the PC: CSR keeps FIELD, a packet on
  the stack (`Vram_Clear`'s image) goes by DMA from a copy, and `sceGsExecStoreImage` reads the GS
  back for the screen effects that copy the frame into a texture.
- Each vblank's scanout is ordered with the packets: it goes into the controller's queue after the
  channels started before the vblank. Without that, `--fast` showed the buffer the game had already
  cleared for the next frame.

Still open:
- The opening shows no message text. That may be how it is on the PS2; it has not been checked
  against it.
- The VU1 interpreter runs on the CPU. It keeps up at full speed (3000 vblanks in about 30 s with
  `--fast`), and step 3 replaces it.
- The GIF's arbitration is modelled only as far as it decides an order with transfers that take no
  time: PATH3 never goes in the middle of a channel 1 chain unless VIF1 unmasks it there.
- The game's write-1-to-clear stores (CSR's FINISH, T0_MODE's EQUF) set the bit instead: nothing sees
  a store into the register page. The game never reads either bit where that would show.
- The VU's adder can differ from IEEE's rounding toward zero in the last bit; the interpreter does
  not model that.

## Step 3: graphics, stage 2 (native renderer)

A renderer under the game's draw layer, as ico-pc has:
- The four VU1 programs become shaders that read the game's own vertex data. Each has a CPU
  version for testing.
- The GS states the game actually uses become pipelines.
- Texture and frame-buffer addresses map to textures and render targets. This includes the feedback
  effects: the screen wobble, and the frame copied into a texture at TBP 0x1180.
- Each piece is checked frame by frame against stage 1.

### Where the PC picture differs from the GS's output

Each of these is on by default and has a switch, so the native renderer can be compared with both
the PC picture and the PS2's raw output (`--ps2-scanout` turns them all off; see the README):

| Switch | What the PC does | Why |
|---|---|---|
| `--no-deinterlace` | weaves the two fields of an interlaced picture | The game draws each field as its own 224-line picture, half a line from the other (`Display_SwapBuffers`, `sceGsSetHalfOffset`), and draws both each frame. One field alone has half the lines, which breaks up text; a TV (or PCSX2's deinterlacing) shows the two together. |
| `--no-progressive` | reads a frame-mode picture whole | paraLLEl-GS's `force_progressive`. |
| `--no-anti-blur` | undoes the blur of blended read circuits | paraLLEl-GS's `anti_blur`. |
| `--no-adapt-width` | scans out at the game's width | paraLLEl-GS's `adapt_to_internal_horizontal_resolution`. |

The native renderer has to give the same picture with each switch, and add a switch for anything
else it does differently. Its natural form is the woven one: it can draw the frame once at 448 lines
(or more) instead of two half-line-offset 224-line fields, which is what the weave reconstructs.

Then, all opt-in:
- higher resolution;
- widescreen (done ahead of the native renderer, on stage 1's path; below);
- higher frame rates, with interpolation while the game itself keeps its 30 Hz.

The opt-in switches, each off by default (the PS2's picture), which the native renderer has to keep:

| Switch | What it changes in the game code | What the PC does |
|---|---|---|
| `--widescreen` (overlay: Display, live) | Under `port_widescreen()` only: `GameLoop_SetCameraMatrices` (case 0) and `MapViewer_RunKingsMap` build the view-to-screen matrix three quarters as wide (`ax` 0.75), a 16:9 horizontal field of view in the same 640x448. `Hud_Draw`, `Hud_DrawPaused`, `Message_Draw`, `Splash_Draw`, `MapViewer_DrawPictureMap` and `MapViewer_DrawKingsMap` call `Widescreen_Mark` (`game/widescreen.c`) around their 2D: a GS register list writing `PORT_GS_REG_WIDESCREEN`, an address the GS does not have. | `port/platform/gs.c` squeezes the X of 2D vertices to three quarters about the screen's middle (the HUD about its left edge, or the middle with `--widescreen-hud-centred`): all of PATH3, and the marked 2D of the packet chain; it leaves pictures of the frame, full-screen fades and untextured fills reaching the edge at full width. The window is 16:9; `--dump-frames` keeps the GS's 640x448. No culling needed widening: the map records are culled by a half-plane, the rest in screen space. |

## Step 4: sound (first version done)

Of the two ways (run FromSoftware's IOP sound driver under an IOP emulator, or rewrite it on the PC),
the driver is rewritten, as ico-pc did for ICO's. [AUDIO.md](AUDIO.md) has the design.

What there is:
- **The driver.** FSIOPSND (`port/audio/fsiopsnd.c`) and the two Sony CSL modules it drives:
  - modmidi, the sequencer;
  - modhsyn, the synthesizer.
  All three were written from the disc's IRXs.
- **Libraries and output.** libsd, on ico-pc's software SPU2 (`third_party/ico-audio`). The SPU2 is
  rendered on EE time to a WAV dump (`--audio-dump`) and to the sound device (SDL2).
- **Determinism.** The driver's timer, replies and transfers all run on EE time, so a run is
  deterministic to the sample. `tools/boot_test.py`'s log is unchanged.

How it was checked: against the disc's own modules run on an IOP emulator, with the same input.
- **FSIOPSND's libsd calls:** the same, call for call.
- **The streamed title music:** sample-identical.
- **modmidi's output:** byte-identical.
- **modhsyn's writes:** the same once both see the same values from the SPU2.

Still open:
- **The SPU2's exponential envelopes.** They round differently from the emulator used as the
  reference: ico-audio rounds down, the reference toward zero. Neither has been checked against
  a PS2.
- **The tick and the game's commands never overlap.** On the PS2 an RPC can land in the middle of
  the driver's tick, so a message can be played a tick later here.
- **Not heard on Windows yet.** The SDL output was tested on Linux only.

## Step 5: saves and movies

- **The memory card is a folder (done).** `port/sdk/libmc.c` implements libmc over a host folder.
  - **Where it is.** By default the card in slot 1 is `memcard` in the data folder (`%APPDATA%\holyland`, beside settings.json and the save states). `--memcard DIR`
    picks another folder and `--memcard none` empties the slot; `tools/boot_test.py` and
    `tools/frame_test.py` pass `--memcard none`, so their logs are as before.
  - **Its layout.** The game's folder `/BASLUS-20318S/` holds real files: the save files, the list
    file, `icon.sys` and the icon. That is the layout of PCSX2's folder memory cards, so one can be
    used here; PCSX2's own `_pcsx2_*` files are hidden from the game.
  - **What the game sees.** It answers as Sony's libmc and mcman do: an 8 MB formatted PS2 card,
    -1 on the first check of the slot, free space in 1 KB clusters (writes past it fail with "card
    full"), `.` and `..` in listings, mcman's attributes and error codes. The dates are the file's,
    in Japan's time.
  - **The save image is the PS2's byte for byte.** `Game_WriteSaveImage` copies `PlayerState` only
    below 0x100, ahead of the pointer fields where the PC layout starts to differ (0x204). Every other
    block it writes (`EntityGroupRecord`, `LevelRecord`, `DefinitionByteSet`, `SaveInfo`, `McIconSys`)
    has its PS2 size.
  - **Checked by playing.** Saved at a save point and loaded from the title, on Windows.
  - **Not done.** `sceMcFormat` keeps the folder as it is, since a card here is always formatted.
    Importing a raw `.ps2` card image is left for a tool.
- **Quick saves (done).** F2 / F4 take and load the game's own save image anywhere in play, as a file
  any build can read (`game/quicksave.c`; README.md, "Quick saves"). Save states moved to F5 / F7.
- `--record-input FILE` records the live pad as a pad script for `--input`, to turn a played session
  into a repeatable test. While recording, the sound stays on the game's clock and mouse look is off,
  so the recording replays.
- **The movies play (done).** The opening (`KF4OP.PSS`, after New Game), the ending (`KF4ED.PSS`)
  and the staff roll (`KF4STAFF.PSS`) play with picture and sound, and Start skips the opening from
  its eleventh picture on, as on the PS2.
  - **The game's player runs as it is.** `gfx/movie.c` (FromSoftware's `fslibmpg.c`, built after the
    SDK's streaming sample) reads the file, splits it with libmpeg, feeds the video to the IPU by DMA
    channel 4 and shows each picture on two fields from its vblank handler. One patch: the decode
    thread waits for the last pictures to be shown by spinning on memory, so the loop lets EE time
    pass (`PORT_SPIN`, port/host.h).
  - **libmpeg** (`port/sdk/libmpeg.c`) is written for the port, at the level of the calls the player
    makes:
    - the PSS demultiplexer is libmpeg's (`pack.o`), packet for packet;
    - pictures come out in display order, and `frameCount`, `sceMpegIsEnd` and
      `sceMpegIsRefBuffEmpty` change as libmpeg's do;
    - the callbacks (no data, background, time stamp) run where libmpeg runs them;
    - the RGB32 picture is in libmpeg's macroblock order (down each column, then across), with the
      IPU's CSC coefficients.
    It never suspends the IPU (the stop and restart DMA callbacks): there is no IPU to suspend.
  - **The decoder** (`port/sdk/mpeg2.c`, about 900 lines) is the port's own, MIT like the rest:
    - libmpeg2 is GPL-2, which would make the program GPL;
    - FFmpeg is LGPL, and a large dependency to build with three toolchains;
    - the PSS files use a small part of MPEG-2 (Main Profile, 640x448 at 30 pictures a second,
      progressive frame pictures with frame prediction and frame DCT), which a small decoder covers.
    It is integer arithmetic only, so a picture is the same with every compiler.
  - **Checked against FFmpeg** (a development check, not in the repository), all 9432 pictures of the
    three files: the first picture is identical, and the others differ only by the IDCT's rounding
    (mostly 58 dB and up). The exception is three damaged macroblocks in the retail files, two in
    `KF4OP.PSS` and one in `KF4ED.PSS` (an invalid macroblock type, a run of coefficients past the
    64th). The decoder drops the rest of that slice, which keeps the buffer's older picture there;
    FFmpeg conceals them. What the PS2's IPU and libmpeg show there has not been checked.
  - **The stream's DMA.** Channel 4 is not the DMA controller thread's. `port/platform/ipu.c` moves it
    as the decoder reads, and keeps CHCR's TAG and `IPU_BP` as the hardware does. So the player's
    ring of blocks reads where the IPU is, as on the PS2.
  - **Sound.** The player streams the PCM through SDRDRV (`sceSdRemote`) into an IOP ring that core
    0's sound data input plays. `port/sdk/libsdr.c` and `port/audio/sdrdrv.c` run those calls on the
    port's libsd, at the EE time of the call. `sceSdBlockTransStatus` now gives where the input's DMA
    is in the ring, which the player keeps its writes behind; it gave the end of the half being
    played, so the player would have written over sound not yet played. Checked: the WAV dump of the
    opening is the PSS's PCM at a gain of 0.9998, 70.7 dB SNR over the 92 seconds.
  - **Timing.** Decoding takes no EE time. The vblank handler shows a picture every two fields, so a
    movie runs at the PS2's pace: the opening's 2772 pictures take 5545 fields, 92.5 s (the stream
    is 30 pictures a second, NTSC shows 29.97). The sound plays at the SPU2's 48 kHz.
  - **With `--widescreen`** the movies show at 4:3, pillarboxed. The player draws into a frame of
    its own, with a draw offset of 0, which the GS front end leaves alone, so while a movie plays it
    squeezes what PATH3 draws at that offset about the frame's middle (`port/platform/gs.c`); the
    black clear reaches the edges and becomes the bars.
  - **Save states** wait for the end of a movie: the decoder is the host's, not in a state.
  - **The tests.** `tests/newgame.pad` presses Start at vblank 740 and skips the opening after 17
    pictures. New Game's movie now ends at 751, `099.map` loads at 754, and the player stands in map
    0x63 from vblank 755. `tools/frame_test.py` also dumps a picture of the movie (vblank 740). The
    logs are as before up to the movie, and gcc's and clang's are the same.
  - **Still open.**
    - The player's three threads rotate the ready queue about 9600 times a field (256 EE cycles
      each), so a movie costs host time: headless with `--fast`, the opening takes 22 s on Windows
      (4 ms a field) but 72 s on Linux (13 ms a field), where each switch of the ucontext fibers is
      a system call. Both keep up with 59.94 fields a second; Linux without much room.
    - The ending and the staff roll were checked decoded on their own, not yet played in the game.
    - `--frames` stops the run at a VSync, which the player never calls: a run that reaches a movie
      plays it to its end.
    - The player's pause and resume (`MovieAudio_Pause`, `MovieAudio_Resume`) need a block transfer
      from a start address, which libsd here does not do. Nothing calls them.

## Step 6: PC features and releases

- Features: a settings menu, rebinding, mouse look.
- First run extracts the game from the user's ISO.
- Builds and releases come from CI.
- Before binaries are shipped, the game's data tables are loaded from the user's own ELF instead of
  being compiled in, as ico-pc does.

## Later (optional)

As OpenGOAL did, some things could be drawn natively straight from the game's structures, without
the GS path (maps and models first).

## Working together

- The game code comes from kfiv-decomp, a git submodule in `decomp/`, pinned to one commit. It is
  never edited here. Run `git submodule update --init` (not `--recursive`; the README says which of
  paraLLEl-GS's own submodules the port needs).
- The PC edits are patches against the decomp in `patches/`. To change game code, run
  `python3 tools/patches.py apply`, edit in `work/src/game`, then `python3 tools/patches.py save` and
  commit `patches/`. PC-only game code, such as `game/data/`, is edited in place.
- Fixes to the reconstruction itself (names, types, structs) go to kfiv-decomp first.
- To move the pin, run `python3 tools/patches.py upgrade <commit>`. It follows file, function and
  global renames (not struct-field or type renames) and re-applies the patches. Commit `decomp` and
  `patches/` if it is clean, or resolve the conflicts in the scratch copy and run
  `patches.py save --from <that dir>`.
