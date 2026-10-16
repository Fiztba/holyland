#!/usr/bin/env python3
"""Compare one section of an unlinked object with a ROM range, skipping
relocated words. Prints the first differences.

    tools/objcmp.py file.o .section rom_offset [count]
"""
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
obj, secname, romoff = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
limit = int(sys.argv[4]) if len(sys.argv) > 4 else 10
rom = (ROOT / "baserom/us/kfiv.rom").read_bytes()
with open(obj, "rb") as f:
    e = ELFFile(f)
    idx = e.get_section_index(secname)
    data = e.get_section(idx).data()
    reloc = set()
    for s in e.iter_sections():
        if isinstance(s, RelocationSection) and s["sh_info"] == idx:
            reloc |= {r["r_offset"] & ~3 for r in s.iter_relocations()}
print(f"{secname}: object {len(data):#x} bytes, {len(reloc)} relocated words")
bad = 0
for off in range(0, len(data), 4):
    if off in reloc:
        continue
    a, b = data[off:off + 4], rom[romoff + off:romoff + off + 4]
    if a != b:
        print(f"  +{off:#07x} (rom {romoff + off:#08x}): obj {a.hex()} rom {b.hex()}")
        bad += 1
        if bad >= limit:
            break
print("match" if bad == 0 else f"{bad}+ differences")
