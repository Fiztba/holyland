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
    lays the tables out so that is harmless. The save icon buffer is enlarged to whole sectors.
    GCC builds with `-fno-toplevel-reorder` so globals keep the order they are defined in.
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
- widescreen (we own the projection code);
- higher frame rates, with interpolation while the game itself keeps its 30 Hz.

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

- The memory card becomes a folder.
- Movies are decoded with libmpeg2 or FFmpeg from the PSS files, keeping the PS2's timing.

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
