# ico-audio

The software SPU2 of [ico-pc](https://github.com/nathanialf/ico-pc), the PC port of the ICO
decompilation, copied from `port/audio/` at commit `4c29ac4c121a357092dea6ee12f6d715ce6cf1f9`
(2026-10-18). MIT licence: see `LICENSE`.

ico-pc wrote it clean-room from psx-spx and ps2sdk's register layout. KFIV runs the same SPU2 and
the same libsd version (its `LIBSD.IRX` is 26285 bytes, the size ico-pc checks for), so it is used
here unchanged.

| file | what it is |
| --- | --- |
| `spu2.h`, `spu2.c`, `spu2_internal.h`, `spu2_tables.c` | the SPU2: voices, envelopes, interpolation, mixing, reverb, transfers, AutoDMA input, IRQ |
| `adpcm.h`, `adpcm.c` | the SPU ADPCM block decoder |
| `spu2_sd.h`, `spu2_sd.c` | libsd's calls and encodings as SPU2 register writes |
| `test/` | ico-pc's unit tests and the render CRC check (`spu2_bench --check`) |

Not copied:
- ico-pc's SNDN2DRV host server, streams, output and mixing. KFIV's driver is FromSoftware's
  FSIOPSND, implemented in `port/audio/`.
- `libsd_irx.c`, which reads the disc through ico-pc's file layer.

Keep these files as they are upstream, so they can be updated by copying them again. KFIV-specific
code goes in `port/audio/`.
