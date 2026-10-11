# Generic bounded VU loop regression tests

This source-only package derives the existing native/performance verifier in external build scratch. It uses the actual candidate generator, its pinned ELF/MPG input loader, generated decoder assertions and an independently hash-checked patch-39 reference. It contains no retail instructions, assets, recordings or generated catalogs.

The authored catalog contains a stationary repeated P/flag loop, a nonstationary EFU loop that must remain single-iteration, a CLIP/memory-alias loop, and a loop whose upper instruction temporarily writes VF0 before a paired store. The last shape must remain on ordinary canonical instruction dispatch: the canonical interpreter permits that temporary same-pair value even though its dependency descriptor discards VF0 writes.

Tests cover generated admission and its negative gate, complete/partial iteration budgets, exact register/flag/memory/cycle state against patch 39, repeated-loop counters, and pending P with an exact deadline corruption control. The cooperative test compares with native dispatch disabled in the current interpreter at every host return, including normalized pending queues/readiness, while changing data identically between resumptions. Test-only access is inserted consistently into derived runtime copies; production sources are read-only.

Configure this directory with explicit `VU_CANDIDATE_RUNTIME_DIR` and `VU_REFERENCE_RUNTIME_DIR`, build RelWithDebInfo, then run CTest with that configuration. Windows uses strict FP and optional candidate-only `/Ob2 /GL`; the reference keeps its existing settings. Linux requires an x86 compiler and the real allocator's SSE4.1 option, as in the native fixture. Generated files and build output must remain outside all source trees.

Only the files in this test directory are new test sources. The adjacent `vu-performance-tests` package is an unchanged dependency.

The generated companion is enabled only for the existing SSE2 target predicate
with direct float evaluation. Other targets emit an empty bounded region table,
so ordinary native/canonical dispatch remains available. A second test executable
changes only that emitted conditional to false in build scratch, then compares
the authored programs against the canonical reference with zero bounded calls.
This checks the fallback on the tested x86 hosts; it does not claim full-project
support for another architecture.
