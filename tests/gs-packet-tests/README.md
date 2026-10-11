# GS packet depth regression

Builds the actual GIF packet decoder and CPU rasterizer. Generated packets
exercise the ordinary and native PACKED paths, XYZ2 and non-drawing XYZ3,
including 32-bit depths above single-precision float's exact integer range.
It checks decoded vertices, equal-depth visibility and the stored depth value.
No game assets, generated retail code, window or GPU are required.

```sh
cmake -S tests/gs-packet-tests -B build/gs-packet -DPS2RECOMP_DIR=/path/to/PS2Recomp
cmake --build build/gs-packet --config Release
ctest --test-dir build/gs-packet -C Release --output-on-failure
```

`GS_PACKET_FRONTEND_SOURCE` can select a saved prior frontend source for a
control build. The frontend before the depth fix fails 30 of 77 checks on
native Windows and Ubuntu; the corrected frontend passes all 77. These are
packet/raster checks, not a retail menu or full-game validation.
