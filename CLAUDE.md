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
