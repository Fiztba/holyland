# holyland

A native PC port of **King's Field: The Ancient City** (*King's Field IV*; FromSoftware, 2001), built
from the [kfiv-decomp](https://github.com/Voicedrew11/kfiv-decomp) decompilation of the PlayStation 2
game (USA, SLUS-20318). The game's own code is compiled for Windows and Linux as a 64-bit program;
`port/` stands in for the PS2 under it.

> No game data is in this repository. When the port can load the game, you will need your own disc
> image.

**Status: step 0.** The game code compiles and links on Windows and Linux, and runs its start-up
until it asks for the disc. Nothing is drawn or heard yet. Next come the disc, the thread
scheduler and the pad (step 1: boot to a new game), then graphics and sound. See
[`docs/port/`](docs/port/).

## Building

You need CMake 3.20 or newer, Ninja, and clang or gcc. The game code is GNU C, so MSVC's `cl` cannot
build it.

**Windows:** use [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), or LLVM's clang, with it on
`PATH`:

```sh
cmake --preset windows-clang
cmake --build out/windows-clang
out/windows-clang/kf4.exe
```

**Linux:**

```sh
cmake --preset linux-gcc          # or linux-clang
cmake --build out/linux-gcc
out/linux-gcc/kf4
```

For now the program prints the PS2 calls it reaches that the PC does not do yet (`[todo] ...`), then
waits for the disc. Stop it with Ctrl+C.

## Working on it

- [`docs/port/README.md`](docs/port/README.md): how the port is laid out, and how it stays in step
  with the decomp.
- [`docs/port/RULES.md`](docs/port/RULES.md): how the game code is changed for the PC (addresses
  in integers, file and save structs, inline asm, the SDK).
- `tools/port_census.py`: what still stops each folder of `src/game` compiling.
- `tools/port_abi_check.py`: functions declared with a different signature than they are defined
  with. These break on the PC even when they compile.

This repository is a fork of kfiv-decomp, and the decomp is merged in with
`git fetch decomp && git merge decomp/main`. Fixes to the reconstruction itself go to kfiv-decomp
first. The decomp's matching PS2 build (`build.sh`) is documented there.
