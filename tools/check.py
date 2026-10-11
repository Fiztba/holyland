#!/usr/bin/env python3
"""ROM view and match check for the built ELF.

    tools/check.py rom build/kfiv.elf build/kfiv.rom   write the PT_LOAD bytes
    tools/check.py verify build/kfiv.rom               SHA-1 against config/sha1sums.txt
                                                       (prints the first differences)
"""
import hashlib
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent


def rom_view(elf_path: str) -> bytes:
    with open(elf_path, "rb") as f:
        e = ELFFile(f)
        loads = [s for s in e.iter_segments() if s["p_type"] == "PT_LOAD"]
        if len(loads) != 1:
            sys.exit(f"{elf_path}: expected one PT_LOAD segment, got {len(loads)}")
        return loads[0].data()


def expected_sha1(name: str) -> str:
    for line in (ROOT / "config/sha1sums.txt").read_text().splitlines():
        parts = line.split("#", 1)[0].split()
        if len(parts) == 2 and parts[1] == name:
            return parts[0]
    sys.exit(f"no SHA-1 for {name} in config/sha1sums.txt")


def section_of(off: int) -> str:
    base = ROOT / "baserom/us/SLUS_203.18"
    with open(base, "rb") as f:
        for s in ELFFile(f).iter_sections():
            if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS":
                start = s["sh_addr"] - 0x100000
                if start <= off < start + s["sh_size"]:
                    return f"{s.name}+{off - start:#x}"
    return "?"


def verify(rom_path: str) -> None:
    got = Path(rom_path).read_bytes()
    want = expected_sha1("kfiv.rom")
    if hashlib.sha1(got).hexdigest() == want:
        print(f"{rom_path}: OK (matches retail)")
        return
    base = (ROOT / "baserom/us/kfiv.rom").read_bytes()
    print(f"{rom_path}: MISMATCH (size {len(got):#x}, retail {len(base):#x})")
    shown = 0
    for off in range(0, min(len(got), len(base)), 4):
        if got[off:off + 4] != base[off:off + 4]:
            print(f"  vma {0x100000 + off:#010x} ({section_of(off)}): "
                  f"built {got[off:off + 4].hex()} retail {base[off:off + 4].hex()}")
            shown += 1
            if shown == 20:
                break
    sys.exit(1)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "rom":
        Path(sys.argv[3]).write_bytes(rom_view(sys.argv[2]))
    elif cmd == "verify":
        verify(sys.argv[2])
    else:
        sys.exit(__doc__)
