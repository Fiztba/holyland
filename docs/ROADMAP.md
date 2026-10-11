# Roadmap

## Where things stand (2026-10-11)

- [x] Toolchain identified: ee-gcc 2.9-ee-991111-01 and SDK 2.2.x, the same as
      ICO (ICO's compiled library objects byte-match KFIV)
- [x] Disc extraction with SHA-1 checks
- [x] splat split (one asm unit for `.text`, one per data section)
- [x] All-asm build with the period assembler and ld 2.10 → **ROM SHA-1 matches**
- [x] `tools/compile_c.sh`, tested by compiling ICO's `libdma.c` to KFIV's
      libdma bytes (0x0022A1D8, 0xA74 bytes)
- [ ] Split `.text` into translation units
- [ ] First game function matched in C (this also settles `-g` or not)
- [ ] Whole-ELF match (section headers, `.DVP` overlays, no symtab)

## Memory map (retail ELF)

| section | VMA | size | notes |
| --- | --- | --- | --- |
| .text | 0x00100000 | 0x149668 | crt0 + game ≈ 0x100000–0x216778; SDK/libc after |
| .vutext | 0x00249668 | 0 | |
| .data | 0x00249680 | 0x1F1B48 | 2 MB, much of it game tables |
| .vudata | 0x0043B1D0 | 0x2790 | VU DMA/microcode data |
| .rodata | 0x0043D980 | 0x71D8 | |
| .lit4 | 0x00444B80 | 0x1B74 | float literals |
| .sdata | 0x00446700 | 0x8E4 | |
| .sbss | 0x00447000 | 0x404 | |
| .bss | 0x00447480 | 0x1A107F0 | 26 MB |
| .DVP.overlay × 8 | 0 | | VU1 microprograms, not in the ROM view |

`_gp` = 0x0044CB70. splat finds 3,843 functions; 684 of them already have
names from the library match.

## Next steps, in order

### 1. Carve out the library translation units (largest early gain)

The SDK, libc, libm and libgcc code (≈0x216778–0x249668, about 200 KB,
roughly 15 % of `.text`) is already solved: ICO's `sce/` sources compile to
the same bytes. For each ICO object:

1. Find its function run in KFIV (`symbol_addrs.txt` has the addresses;
   `tools/find_lib_matches.py` rescans a directory of compiled objects).
2. Add splat subsegments `[start, c, sce/libdma/libdma]` and so on, and copy
   the source into `src/sce/...` (ICO is MIT; keep the attribution).
3. Carve the object's `.data`/`.rodata`/`.sdata`/`.bss` contributions out of
   the big data units as well, or the data is emitted twice. ICO's per-TU
   data carving is the model to follow.

Not covered by ICO: **libsdr** (KFIV uses it; ICO uses libsndn2) and the
handful of functions that differ (FromSoftware may have used a different
subset). These are new work.

### 2. Find the game's translation-unit boundaries

There is no link map (ICO's PAL disc shipped one; KFIV's does not). Signals:

- `.text` contributions of each object are 8-byte aligned. Runs of padding
  `nop`s after a `jr $ra` mark likely boundaries.
- `.rodata`, `.data` and `.lit4` follow the same object order as `.text`.
  Where a function's rodata or float literals start a new aligned block, a
  new object starts.
- File-static data referenced only by one run of functions.
- `__FILE__` strings and assert messages (`fslibmpg.c` survives).
- Function naming from the Holyland recomp work (`~/Documents/KFIV-PC`,
  `SLUS_203.18.functions.csv`) and the `KF_*` debug strings.

### 3. Match game functions

- Settle the game's flags with one small leaf function: `-O2 -G 8`, with or
  without `-g` (`GAME_CFLAGS` in `tools/compile_c.sh`).
- Workflow per function: m2c for a first draft → decomp.me (preset "PS2
  ee-gcc 2.9-991111-01") or a local asm-differ/objdiff → drop into `src/`.
- Start with leaf utility code (math, string, file wrappers such as
  `KF_File_Open`) and work outward.

### 4. Whole-ELF match (later)

The ROM view matches already. A full `SLUS_203.18` SHA-1 match also needs the
empty `.vutext` header, `.reginfo` placement, the eight `.DVP.overlay`
sections (rebuilt from VU1 microcode with dvp-as) and no `.symtab`. ICO's
README describes how they got there.

## Build notes and gotchas

- The checkout path must not contain spaces: binutils' autoconf trees
  (`tools/setup.sh`) and `LD_PRELOAD` (`tools/period_env.sh`) both break on
  them, and both scripts stop with a clear error. Everything the build needs
  lives inside the checkout: the toolchain and its sources under `tools/cc/`
  and the obstack shim under `build/` (all gitignored and regenerated).
- ee-as 2.9 syntax limits are handled in `tools/split.py`: numeric registers,
  no `%gp_rel` (rewritten to `.extern sym, 4` plus the macro form), bare
  `ACC`/`Q` for VU0, floats emitted as `.word`, and EUC-JP strings kept as
  bytes.
- ld 2.10 has no `INCLUDE` inside an output section, so `gen_ninja.py`
  expands `config/kfiv.us.ld.in` into `build/kfiv.ld`.
- `.splache` is ignored and `split.py` always does a full split.
