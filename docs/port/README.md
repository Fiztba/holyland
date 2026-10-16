# The PC port

holyland is a native PC port of King's Field IV built from the decomp's source. The game code in
`src/game` is the decomp's, edited where the PC needs it. Under it, `port/` stands in for the
PlayStation 2. The approach follows [ico-pc](https://github.com/nathanialf/ico-pc), the PC port of
the ICO decomp:
- The game code is compiled for the PC as 64-bit C, with nothing emulated or recompiled.
- Where the code relies on the PS2, it is changed to the PC form in place, by the rules in
  [RULES.md](RULES.md).
- `port/` provides what the PS2 did, keeping the SDK's names so the game code reads as before:
  - game RAM;
  - the hardware registers;
  - the Sony libraries;
  - later the renderer, the sound driver and the platform layer.

## Relation to the decomp

This repository is a fork of [kfiv-decomp](https://github.com/Voicedrew11/kfiv-decomp), kept up to
date with:

```sh
git fetch decomp && git merge decomp/main
```

The `decomp` remote is `https://github.com/Voicedrew11/kfiv-decomp.git`.
- Fixes to the reconstruction (names, types, structs, matching) go to the decomp first and come here
  in a merge.
- Changes for the PC stay here.
- Never squash a merge from the decomp: later merges need its history.
- The decomp's PS2 build and checks (`build.sh`, `tools/match.py`) still describe the decomp. They do
  not apply to this tree's edited game code.

## Building

The game code is GNU C, so build it with clang or gcc (not MSVC's cl). You need CMake 3.20 or newer
and Ninja.

**Windows:** use [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), or LLVM's clang, on the PATH:

```sh
cmake --preset windows-clang
cmake --build out/windows-clang
```

**Linux:**

```sh
cmake --preset linux-gcc        # or linux-clang
cmake --build out/linux-gcc
```

The program is `out/<preset>/kf4`.

Each folder of `src/game` is its own target, so `cmake --build out/windows-clang --target game_ui`
compiles just `src/game/ui`.

## Status

The steps, in order: [PLAN.md](PLAN.md).

**Step 0** (in progress): the game code compiles and links as a 64-bit program on Windows and Linux,
and `main` runs until it needs something the stand-ins do not do yet.

- `tools/port_census.py` counts, per folder, the errors and the inline asm still standing between
  each folder and that goal.
- `tools/port_census.py --show ui/menu_draw.c` prints one file's errors.
- `tools/port_census.py --show ui` prints a folder's.

Next:
1. **Step 1:** the stand-ins (thread scheduler, files, pad) get the game from boot to a new game with
   nothing drawn.
2. **Graphics:**
   - first the game's packets go to paraLLEl-GS;
   - then the VU1 programs become native renderers.
3. **Sound.**

## Layout

| Path | What |
|---|---|
| `src/game/` | the game (the decomp's, in its PC form) |
| `port/include/` | the SDK headers the game includes (`libgraph.h`, `eekernel.h` …) for the PC, and `port/host.h` |
| `port/platform/` | the program's entry, game RAM, EE addresses, the register pages |
| `port/sdk/` | the Sony libraries' PC versions (step 0: stand-ins) |
| `port/math/` | libvu0 in C |
| `docs/port/` | these notes |
