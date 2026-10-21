# holyland

The PC port of King's Field IV, built from the kfiv-decomp source. Start with
[docs/port/README.md](docs/port/README.md) and [docs/port/RULES.md](docs/port/RULES.md).

## The decomp is read-only

- Never edit `decomp/` (the kfiv-decomp submodule) or anything under `out/<preset>/decomp_src/` (the
  patched copy that is compiled; compiler errors point into it).
- Change game code only in `work/`: `python3 tools/patches.py apply`, edit `work/src/game`, then
  `python3 tools/patches.py save` and commit `patches/`. The same path below `work/` as below
  `decomp_src/` is the file to edit.
- Never hand-edit the files in `patches/`; `patches.py save` writes them.
- Fixes to the reconstruction itself (names, types, structs) go to kfiv-decomp, not here.
- `game/` and `port/` are ordinary source, edited in place.

## Before changing anything that draws

Read [docs/port/RENDERER.md](docs/port/RENDERER.md): where the native GS, the VU1 programs in C and
the presenters are, and the checks a renderer change must pass (gsn_draw_test, the map tour
comparison including the scanouts, frame_test with `--renderer native`).

## Tests never touch the player's data

Every run of the game in a test sets `KF4_DATA_DIR` to a temporary folder.
