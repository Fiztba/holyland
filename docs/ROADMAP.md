# Roadmap

## Where things stand (2026-10-11)

- [x] Toolchain identified: ee-gcc 2.9-ee-991111-01 and SDK 2.2.x, the same as
      ICO (ICO's compiled library objects byte-match KFIV)
- [x] Disc extraction with SHA-1 checks
- [x] splat split (one asm unit for `.text`, one per data section)
- [x] All-asm build with the period assembler and ld 2.10 → **ROM SHA-1 matches**
- [x] `tools/compile_c.sh`, tested by compiling ICO's `libdma.c` to KFIV's
      libdma bytes (0x0022A1D8, 0xA74 bytes)
- [x] Library translation units carved: 181 SDK, libc, libm and libgcc units
      in `src/sce/` (C, plus newlib's and the SDK's hand-written `.s`), 0x289DC
      bytes of `.text` (about 12 %), with their data, still **SHA-1 matching**
- [x] objdiff progress reports: `ninja progress` → `build/report.json`
      (162 of 167 library C units at 100 % matched code)
- [ ] Register on decomp.dev and upload the reports
- [ ] Split the game's `.text` into translation units
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

### 1. Carve out the library translation units (done)

`tools/carve.py` compiles every `.c`/`.s` under `src/sce/`, places each unit
and each of its sections in the retail ELF (`tools/lib_layout.py`: masked
byte search for `.text`, relocations for data, link order for what nothing
references), and writes the splat subsegments, `config/link_order.us.txt` and
`config/symbol_addrs.carve.txt`. Adding a library unit is: write the file,
check `tools/lib_layout.py` places every section with matching bytes, run
`tools/carve.py`, then `rm -rf asm && ./build.sh`.

Most units are ICO's sources. New for KFIV: libsdr (one unit for the whole
archive), libdev's VU0/VIF0/VIF1 functions that compile from C (one unit per
function, names inferred), the double-precision fdlibm members, mktime,
strcasecmp and strchr, and a few libgraph, libcdvd, libkernl and libc
members ICO did not link.

Still asm in the library range: the libdev functions that are mostly inline
asm (`sceDevVu0GetVi/GetVf/PutMem/GetMem/ChkIdle/GetCnd/GetCtx/PutCtx` and
the VIF0/VIF1 reset, pause, continue, status, register and FIFO functions).
Their hand-copied `.s` would only rename splat's output, so they wait for C.
Also asm: 0x24 bytes of unreferenced `.bss` at 0x01E548DC and the nop fill at
the end of `.text`.

### 2. Progress reporting with objdiff and decomp.dev (local part done)

`tools/gen_ninja.py` writes `objdiff.json`, one unit per entry in the link
order, in the categories `game` and `sce`:

- C units: the base object is `build/src/<path>.c.o`; the target object,
  `build/target/src/<path>.o`, is assembled from splat's per-function asm
  (`asm/matchings/`) in address order plus the unit's data files, with
  splat's fill before the next unit trimmed off.
- Hand-written `.s` units are both target and base (matching by
  construction, marked complete). splat's own `asm/` units are target only.

`tools/cc/objdiff-cli` (pinned in `tools/setup.sh`) writes `build/report.json`
from `ninja progress`. For splat to cut the target the way the C objects are
cut, `tools/carve.py` names every static function and object of the library
units (with sizes), and lists with `ignore:True` the constants the retail code
builds with lui/lo16 pairs that splat would take for addresses.

Five library functions still show a relocation-only difference (99.7 % or
more fuzzy): `_sceFs_Rcv_Intr` (a `%hi` with no `%lo` partner, which splat
cannot symbolize), `_sceCd_cd_read_intr`, `_sendDataToIPU` and
`mceIntrReadFixAlign` (an address built through `or`, which splat does not
follow) and `_copyRefImage` (`%hi/%lo(_maxval)` of a table inside the
function). Matched data is low: splat writes unlabelled string literals as
word runs, so a target `.rodata` cannot end on the C object's byte.

Still to do: **reports are generated locally and uploaded**, not built in CI,
because the build needs the user's disc and no disc-derived data may reach
the public repo. Register the project on decomp.dev, upload `report.json`
from a local build after each batch of work, and put the decomp.dev badges in
the README.

### 3. Find the game's translation-unit boundaries

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

### 4. Match the first game functions

- Settle the game's flags with one small leaf function: `-O2 -G 8`, with or
  without `-g` (`GAME_CFLAGS` in `tools/compile_c.sh`). ICO's game (same
  toolchain and studio-era SDK) was built `-g -fno-common`, so try that first.
- Workflow per function: m2c for a first draft → decomp.me (preset "PS2
  ee-gcc 2.9-991111-01") or objdiff → drop into `src/`.
- Start with leaf utility code (math, string, file wrappers such as
  `KF_File_Open`) and work outward.

### 5. Whole-ELF match (later)

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
