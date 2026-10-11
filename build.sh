#!/usr/bin/env bash
# One command from a fresh checkout to a verified build:
#   tools/setup.sh        when the venv or toolchain is missing
#   tools/extract_elf.py  when baserom/us/ is missing (disc at baserom/kfiv_us.iso)
#   tools/split.py        when asm/ is missing
#   tools/gen_ninja.py + ninja (the last step checks the ROM SHA-1)
set -euo pipefail
cd "$(dirname "$0")"
[[ -x .venv/bin/python && -x tools/cc/binutils-2.10-ee/bin/ld ]] || tools/setup.sh
[[ -f baserom/us/kfiv.rom ]] || .venv/bin/python tools/extract_elf.py "$@"
[[ -d asm ]] || .venv/bin/python tools/split.py
.venv/bin/python tools/gen_ninja.py
exec .venv/bin/ninja
