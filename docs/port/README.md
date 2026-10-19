# The PC port

holyland is a native PC port of King's Field IV built from the decomp's source. The game code is the
decomp's, pinned in `decomp/` and never edited here; the PC edits to it are kept as patches in
`patches/`. Under it, `port/` stands in for the PlayStation 2. The approach follows
[ico-pc](https://github.com/nathanialf/ico-pc), the PC port of the ICO decomp:
- The game code is compiled for the PC as 64-bit C, with nothing emulated or recompiled.
- Where the code relies on the PS2, it is changed to the PC form, by the rules in
  [RULES.md](RULES.md).
- `port/` provides what the PS2 did, keeping the SDK's names so the game code reads as before:
  - game RAM;
  - the hardware registers;
  - the Sony libraries;
  - later the renderer, the sound driver and the platform layer.

## The decomp and the patches

`decomp/` is a git submodule of [kfiv-decomp](https://github.com/Voicedrew11/kfiv-decomp.git), pinned
to one commit (currently `88b0978`).

> **The decomp is read-only here.** Never edit `decomp/` or the compiled copy in
> `out/<preset>/decomp_src/`; this goes for people and agents alike. Configure fails if `decomp/` has
> local changes, and `decomp_src/` is made read-only (an edit there would be lost at the next
> configure). Edit game code in `work/` and save it into `patches/` (below).

Clone with submodules, but not recursively: `third_party/parallel-gs` (the GS renderer) has a
Granite submodule with twenty more of its own, and the port needs only two of them.

```sh
git clone <this repository> && cd holyland
git submodule update --init
git -C third_party/parallel-gs submodule update --init Granite
git -C third_party/parallel-gs/Granite submodule update --init third_party/volk third_party/khronos/vulkan-headers
```

Without the last two lines the port still builds, but draws nothing.

The PC edits are in `patches/`, as diffs against the decomp with paths relative to its root:
- `patches/game/<folder>.patch`: one per top-level folder of `src/game`.
- `patches/game/_top.patch`: the files directly in `src/game`.
- `patches/nonmatching.patch`: the map draw `src/nonmatching/func_00112BD0.c`, which the PC build
  compiles.

Game code that exists only on the PC, with nothing in the decomp to patch, is ordinary files in
`game/`: `game/data/` holds the data sections that the decomp still has in asm, defined in C. It is
compiled with the patched decomp and edited in place.

The decomp's own tooling and docs (its PS2 build, `build.sh`, `tools/match.py`, the splat config) live
in kfiv-decomp. They do not apply to the patched game code here.

Fixes that are not PC-specific (names, types, structs, matching) go to kfiv-decomp first, and come
here with a pin move (see below).

### Changing game code

1. `python3 tools/patches.py apply`: makes `work/` (git-ignored) a fresh copy of the decomp with the
   patches applied. `work/` is its own git repository, and its `pristine` tag is the unpatched decomp.
   If `work/` has changes that were not saved, `apply` refuses; `apply --force` discards them.
2. Edit the game code in `work/src/game`, by the rules in [RULES.md](RULES.md).
3. `python3 tools/patches.py save`: regenerates `patches/` from `work/`. Commit `patches/`.
4. `cmake --build out/<preset>`: since the patches changed, configure runs again on its own. It
   copies `decomp/src/game` and `decomp/src/nonmatching`, applies `patches/` to the copy, and updates
   `out/<preset>/decomp_src/` with the files that changed, so only those recompile. The build compiles
   `decomp_src/` with `game/` and `port/`. Configure fails if `decomp/` has local changes.

### Moving the pin

`python3 tools/patches.py upgrade <commit>` moves the submodule to a newer kfiv-decomp commit:
- it follows file renames (`git diff -M` between the two pins) and function and global renames (from
  `config/symbol_addrs.txt`, whose addresses are stable);
- it re-applies the patches with `git apply -3` and reports conflicts.

It does not follow renames of struct fields or types: `symbol_addrs.txt` has only functions and
globals. Those come up as conflicts, or as compile errors after a clean upgrade, and are fixed by
hand.

If that is clean, it moves the submodule and rewrites `patches/`. Then commit `decomp` and `patches/`.
If not, it keeps the scratch copy so the conflicts can be resolved there, and then
`python3 tools/patches.py save --from <that dir>` writes the result to `patches/`.

## Building

The game code is GNU C, so build it with clang or gcc (not MSVC's cl). You need CMake 3.21 or newer
and Ninja. The renderer, paraLLEl-GS, is C++ (clang++ or g++, which the presets name). It runs on
desktop Vulkan GPUs (its README lists the features it needs); running it needs a Vulkan driver,
building it does not.

**Windows:** use [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), or LLVM's clang, on the PATH:

```sh
cmake --preset windows-clang -DSDL2_DIR=C:/path/to/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build out/windows-clang
```

SDL2 on Windows comes from its mingw development package (`SDL2-devel-2.x.y-mingw.tar.gz` from
[SDL's releases](https://github.com/libsdl-org/SDL/releases)); `SDL2_DIR` points into it. The build
copies `SDL2.dll` and llvm-mingw's `libc++.dll` and `libunwind.dll` next to `kf4.exe`.

**Linux:**

```sh
cmake --preset linux-gcc        # or linux-clang
cmake --build out/linux-gcc
```

The program is `out/<preset>/kf4`. SDL2 is used for the window and the pad if CMake finds it
(`-DKF4_USE_SDL=OFF` turns it off). paraLLEl-GS is built when it is checked out
(`-DKF4_USE_PARALLEL_GS=OFF` turns it off); without it the game runs and draws nothing.

## Running

```sh
out/linux-gcc/kf4 --disc kfiv_us.iso
```

| Option | What |
|---|---|
| `--disc PATH` | the game's disc image (or set `KF4_DISC`) |
| `--fast` | run as fast as possible instead of at 60 vblanks a second |
| `--headless` | no window, no SDL; the GS draws only for `--dump-frames` |
| `--no-gs` | draw nothing (VIF1 and VU1 still run) |
| `--no-deinterlace` | show each field as the GS scans it out (224 lines), not the two fields woven into one picture |
| `--no-progressive` | read a frame-mode interlaced picture a field at a time |
| `--no-anti-blur` | keep the blur of a game that blends its two read circuits |
| `--no-adapt-width` | scan out at the TV's width, not the game's |
| `--ps2-scanout` | all four of the above: the GS's raw output, for comparing with the PS2 or PCSX2 |
| `--dump-frames N,N,...` | write the picture shown at these vblanks as `frame_NNNNNN.png` |
| `--dump-dir DIR` | where `--dump-frames` writes (the current directory by default) |
| `--frames N` | stop after N vblanks |
| `--input FILE` | play the pad from a script instead of SDL |
| `--log FILE` | the per-frame log (`-` for stdout) |
| `--clock YYYY-MM-DDTHH:MM:SS` | the disc drive's clock at boot, in Japan's time, instead of the host's; for repeatable runs |
| `--host-dir DIR` | the folder `host0:` paths are read from |
| `--stats` | at exit, print where the DMA controller's thread spent its time: VU1 (its runs and instructions too), handing GIF packets to the GS, and vsyncs; and a hash of every packet VU1 sent, which two runs that drew the same share. For A/B timings. It counts from the last save state saved or loaded |
| `--state-save-at VBLANK[:SLOT\|:FILE],...` | save a state at these vblanks, to a slot (0 when left out) or a file (see "Save states") |
| `--state-load [VBLANK:]SLOT\|FILE` | load a state before the game starts, or once the run reaches that vblank |

The window shows the game at 4:3. A game controller is the pad; so is the keyboard:

| Keys | Pad |
|---|---|
| arrows | d-pad |
| W A S D, I J K L | left stick, right stick |
| Z or space, X, C, V | cross, circle, square, triangle |
| Q, E, 1, 3 | L1, R1, L2, R2 |
| Enter, Backspace | start, select |

A pad script has one press per line: `<vblank> <buttons> [vblanks held, default 6]`. The buttons are
names such as `CROSS`, `START`, `UP`, joined with `+`, or `-` for none. A line `<vblank> lx=N ...` (or `ly`, `rx`,
`ry`) sets a stick (0x80 is the middle). Text from `#` on is a comment. `tests/newgame.pad` boots to
a new game.

The log has one line per vblank event (`<vblank> event: ...`: files read, movies, the memory card),
and one per game frame with the game's status, the map and the player's position.

`tools/boot_test.py --disc kfiv_us.iso` is step 1's check: it boots to a new game twice and checks
the two logs match and reach the first map. It runs headless with no GS, so it needs no GPU.

`tools/frame_test.py --disc kfiv_us.iso` is step 2's: the same boot with the GS, dumping the logo,
the title menu, the brightness screen, the opening and the first map, and every picture of vblanks 990
to 1010 (`--dense`). It checks two runs (`--runs N` for more) give the same pictures byte for byte and
that none is blank (`--keep DIR` keeps them to look at).

Each folder of the game code is its own target, so `cmake --build out/windows-clang --target game_ui`
compiles just the `ui` folder (`src/game/ui`, with the patches applied).

## Save states

A save state is the whole running game in a file: loaded, the game goes on exactly where it was.
They are for testing and debugging (the game's own saves are its memory card's). The settings
overlay's keys save and load slot 0; the command line drives them for tests:
- `--state-save-at 900:1` saves slot 1 at vblank 900; `--state-save-at 400:a.kf4state,2000:b.kf4state`
  saves two files in one run.
- `--state-load 1` loads slot 1 before the game's first instruction; `--state-load 300:b.kf4state` loads
  a file once the run reaches vblank 300, while it runs (in a window too).

Slots are `slot<N>.kf4state` in `%APPDATA%\holyland\states` on Windows, and in
`$XDG_DATA_HOME/holyland/states` (or `~/.local/share/holyland/states`) elsewhere. A file is about 5 to
15 MB (LZ4-compressed; 80 MB in memory).

How it works (`port/platform/savestate.c`):
- A state is taken at a vblank, where every EE thread is parked in a kernel call. It is not taken
  while a GIF packet or an image transfer to the GS is half sent, or a `host0:` file is open: the
  request then waits for the next vblank, and says so.
- It holds:
  - game RAM, the scratchpad, VU0 and the register pages;
  - the globals of the game and of ico-audio's SPU2, which are linked as one block between two
    markers (`port/platform/state_begin.c`, `state_end.c`);
  - the EE threads' fibers and their stacks, and the kernel, DMA controller, VIF1, VU1 and GIF;
  - the GS's VRAM and registers, and what paraLLEl-GS's scanout keeps from the last pictures;
  - the libraries, the IOP's memory, libsd, FSIOPSND, modmidi, modhsyn and the sound's clock.
- Not in a state: what is the host's (the window, the sound device, the log, files, the options) and
  `--stats`' counts.

**Same build only.** The stacks hold return addresses into the program, and the game keeps pointers
in game RAM and its globals. So:
- game RAM and the stacks are at fixed addresses;
- the program is linked without ASLR;
- a state carries a hash of the program's code, and another build refuses it.

A state loads into a run with other options:
- On the device's clock or the game's: the sound goes on from where it was.
- Without the GS (`--headless` with no `--dump-frames`, or `--no-gs`): a state taken without it has no
  VRAM, and one loaded without it skips its VRAM.

The same input gives the same run after a load as without one. Checked on `tests/newgame.pad`, with
states at vblanks 400, 900 and 2000, the GS and the sound on, to vblank 2300. A run that saves
against one that loads the state in a new process:
- the per-frame log after the "state saved" line is the same as after the "state loaded" line;
- the `--audio-dump` WAV is the same from the state's SPU2 frame on;
- `--stats`' VU1 runs, instructions and cycles and the PATH1 hash are the same;
- the frames dumped after the state's vblank are the same, byte for byte;
- the run that saved is the same as one that does not (log, WAV, frames);
- a state saved again right after it is loaded is the same file (but the time in its header).

## Status

The steps, in order: [PLAN.md](PLAN.md).

**Step 0** (done): the game code compiles and links as a 64-bit program on Windows and Linux,
and `main` runs until it needs something the stand-ins do not do yet.

- `tools/port_census.py` counts, per folder, the errors and the inline asm still standing between
  each folder and that goal.
- `tools/port_census.py --show ui/menu_draw.c` prints one file's errors.
- `tools/port_census.py --show ui` prints a folder's.

**Step 1** (done): the stand-ins (thread scheduler, files, DMA, IOP, memory card, pad) get the game
from boot to a new game with nothing drawn, deterministically (`tools/boot_test.py`).

**Step 2** (done): the game is drawn, in a window, from boot to the first map, and played with a
pad or the keyboard. The DMA controller reads the VIF1 and GIF chains; VIF1 and an interpreter of
VU1 run the game's microprograms; the GS is paraLLEl-GS (`tools/frame_test.py`).

Next:
1. **Graphics, stage 2:** the VU1 programs become native renderers.
2. **Sound.**

## Layout

| Path | What |
|---|---|
| `decomp/` | submodule: kfiv-decomp at the pinned commit. Never edited here |
| `patches/` | the PC edits to `decomp/`, as diffs (`game/<folder>.patch`, `game/_top.patch`, `nonmatching.patch`) |
| `game/` | PC-only game code, compiled with the patched decomp (`game/data/`: the data sections in C; `state_log.c`: the per-frame log) |
| `work/` | git-ignored: the decomp with the patches applied, for editing (`tools/patches.py apply`) |
| `port/include/` | the SDK headers the game includes (`libgraph.h`, `eekernel.h` …) for the PC, and `port/host.h` |
| `port/platform/` | the program's entry and options, game RAM, EE addresses, the register pages, the EE kernel (fibers, virtual time), the DMAC, VIF1, VU1, the GS's front end, the window and frame dumps, the disc, save states |
| `port/gs/` | the GS backend: paraLLEl-GS behind a small C interface |
| `port/sdk/` | the Sony libraries' PC versions: files and the IOP over SIF, the sound driver's RPC, libcdvd, libdma, libgraph, libpad, libmc; stand-ins for libmpeg and libsdr |
| `port/math/` | libvu0 in C |
| `third_party/parallel-gs/` | submodule: [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) (LGPL-3.0+), the GS in Vulkan compute |
| `tools/` | `patches.py`, `port_census.py`, `port_abi_check.py`, `boot_test.py`, `frame_test.py`, `vu1_disasm.py` (the VU1 programs as text) |
| `tests/` | pad scripts for headless runs (`newgame.pad`); `vu1_test.c`, the VU1 interpreter's self-test (`out/<preset>/vu1_test`) |
| `.github/workflows/ci.yml` | CI: configure (the patches apply), build with paraLLEl-GS, `port_census.py` reports 0 errors |
| `CMakeLists.txt`, `CMakePresets.json` | the build; it compiles the patched game code from `out/<preset>/decomp_src/`, with `game/` |
| `docs/port/` | these notes |
