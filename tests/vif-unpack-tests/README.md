# VIF bulk UNPACK correctness

These source-authored tests exercise the actual patched VIF parser, memory implementation and GIF arbiter. The existing cooperative-DMA fixture substitutes graphics/VU execution only. No game image, assets, recorded payloads or generated retail code are included or required.

Configure `PS2RECOMP_DIR` to the patched runtime checkout, build, and run CTest. For example:

```sh
cmake -S tests/vif-unpack-tests -B build/vif-unpack -DPS2RECOMP_DIR=/path/to/runtime
cmake --build build/vif-unpack --config Release --parallel 2
ctest --test-dir build/vif-unpack -C Release --output-on-failure
```

On single-configuration generators, also configure `-DCMAKE_BUILD_TYPE=Release`. An optional `VIF_UNPACK_REFERENCE_PARSER` can point to a compatible unoptimized parser; it runs the same independent expected-state oracle. `HOLYLAND_TESTS_DIR` normally defaults to the sibling tests directory and can be overridden when validating this fixture in isolation.

The test covers V4-32 bit copies and V4-16 signed/unsigned expansion, every 16-bit input value, zero-as-256 counts, TOPS, IRQ and all VIF register values, contiguous/skip/fill cycles, destination wrapping, masks and ROW-changing modes, aliased input, consecutive commands and every byte split of a payload. Each complete case checks full VU memory and VIF registers. Source buffers ending at protected pages exercise exact-width reads for paired and odd-tail vectors.

`prepare_controls.py` reads the actual parser and writes only build scratch. A fixture-only counter proves fast-path admission and guard refusal. The forced scalar-helper variant checks the non-intrinsic source branch; it is still compiled on the host architecture and may be auto-vectorized. Marker checks fail clearly if the parser changes and the fixture needs updating.

Three negative controls deliberately corrupt sign extension, the mask guard, and skip wrapping. A wrapper requires exit 1 with the intended assertion; a crash or unrelated failure is not accepted. The renderer, VU execution, active-DMA ownership and reentrant graphics callbacks are outside this fixture's proof. Continue to run the cooperative-DMA tests and actual-game checks when changing their boundaries. This is not a frame-rate benchmark, gameplay equivalence proof, or GPU test.

The original candidate and forced scalar source branch each passed 2,142,890 checks and 3,346 observed fast-path admissions on native Windows MSVC x64 and Ubuntu WSL2 GCC. The optional original parser passed the same oracle with zero admissions. Negative controls detected the intended errors at checks 2, 7 and 16,850 respectively. Public-package verification is recorded separately by the integrating change; no non-x86 execution is claimed.
