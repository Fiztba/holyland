# src/sce: Sony SDK and compiler runtime libraries

Sony's EE libraries (libkernl, libgraph, libdma, libpad, libcdvd, libmpeg,
libipu, libmc, ...) and the compiler install's newlib libc, libm and libgcc,
as linked into `SLUS_203.18`.

The sources come from the ICO decompilation
([nathanialf/ico](https://github.com/nathanialf/ico), MIT, © the ico
decompilation contributors; notice in [`tools/LICENSE.ico`](../../tools/LICENSE.ico)).
KFIV and ICO were built with the same ee-gcc 2.9-ee-991111-01 and the same
SDK 2.2.x release, so these files compile to KFIV's bytes. Comments that give
addresses or link positions describe ICO's PAL build, not KFIV's.
KFIV-specific changes are marked `KFIV:`. Files ICO does not have (libsdr,
libdev, the double-precision libm members, mktime, strcasecmp, strchr and a
few others) were written for KFIV and say so in their first comment.

Every `.c` and `.s` here is a translation unit of the build. `tools/carve.py`
compiles them, finds where each one sits in the retail ELF and writes the
splat subsegments, the link order and `config/symbol_addrs.carve.txt` from
that. A file that does not match the retail bytes as a whole stops it. Headers
for members not in KFIV stay, since the other members include them.
