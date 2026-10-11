#!/usr/bin/env python3
"""Work on one game function.

    tools/match.py asm func_XXXXXXXX     the retail function as splat wrote it
                                         (paste it into decomp.me or m2c)
    tools/match.py diff src/game/F.c     compile F.c and compare every function
                                         in it with the retail bytes

`diff` compiles with tools/compile_c.sh (the game's flags) into build/match/
and lines each function up with the retail code at its address: the one in
config/symbol_addrs.txt, or the one in its name (func_XXXXXXXX). Relocated
fields are not compared: the linker fills them. Exit status 0 means every
function matches.

Needs ./build.sh to have run once (asm/ and baserom/us/).
"""
import glob
import os
import re
import subprocess
import sys

import rabbitizer
from elftools.elf.elffile import ELFFile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib_layout import ROOT, Obj, load_retail, load_symbols  # noqa: E402

rabbitizer.config.regNames_gprAbiNames = rabbitizer.Abi.NUMERIC
rabbitizer.config.regNames_fprAbiNames = rabbitizer.Abi.NUMERIC


def find_asm(name):
    """splat's lines for function `name`, from asm/."""
    files = glob.glob(os.path.join(ROOT, "asm/text*.s"))
    files += glob.glob(os.path.join(ROOT, f"asm/matchings/**/{name}.s"), recursive=True)
    for fn in files:
        lines, keep = [], False
        for line in open(fn):
            if line.startswith(f"nonmatching {name},") or line.startswith(f"glabel {name}"):
                keep = True
            if keep:
                lines.append(line.rstrip("\n"))
                if line.startswith(f"endlabel {name}"):
                    return "\n".join(lines)
    return None


def dis(word, vram):
    return rabbitizer.Instruction(word, vram=vram,
                                  category=rabbitizer.InstrCategory.R5900).disassemble()


def diff(src):
    rel = os.path.relpath(os.path.abspath(src), ROOT)
    out = os.path.join("build/match", rel + ".o")
    r = subprocess.run([os.path.join(ROOT, "tools/compile_c.sh"), rel, out], cwd=ROOT)
    if r.returncode:
        return 2
    obj = Obj(os.path.join(ROOT, out), rel)
    if ".text" not in obj.secs:
        print("no .text in the object")
        return 1
    _, size, _, data = obj.secs[".text"]
    mask = obj.mask(".text")
    tbase, tdata = load_retail(os.path.join(ROOT, "baserom/us/SLUS_203.18"))[".text"]
    n2a, _ = load_symbols()
    with open(os.path.join(ROOT, out), "rb") as f:
        sizes = {s.name: s["st_size"] for s in ELFFile(f).get_section_by_name(".symtab").iter_symbols()}
    bad = 0
    for off, name in obj.funcs:
        addr = n2a.get(name)
        if addr is None and re.fullmatch(r"func_[0-9A-F]{8}", name):
            addr = int(name[5:], 16)
        if addr is None:
            print(f"{name}: no address (name it func_XXXXXXXX or add it to config/symbol_addrs.txt)")
            bad += 1
            continue
        n = (sizes.get(name) or size - off) // 4
        rows, diffs = [], 0
        for k in range(n):
            o = off + 4 * k
            ours = int.from_bytes(data[o:o + 4], "little")
            t = addr - tbase + 4 * k
            theirs = int.from_bytes(tdata[t:t + 4], "little")
            same = (ours ^ theirs) & mask[o // 4] == 0
            diffs += not same
            rows.append((addr + 4 * k, dis(ours, addr + 4 * k), dis(theirs, addr + 4 * k), same))
        if diffs == 0:
            print(f"{name}: OK ({n} instructions)")
            continue
        bad += 1
        print(f"{name}: {diffs} of {n} instructions differ")
        print(f"  {'address':10} {'yours':40} retail")
        for a, mine, ret, same in rows:
            print(f"{' ' if same else '*'} {a:08X}   {mine:40} {ret}")
    return 1 if bad else 0


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("asm", "diff"):
        sys.exit(__doc__)
    if sys.argv[1] == "asm":
        text = find_asm(sys.argv[2])
        if text is None:
            sys.exit(f"{sys.argv[2]}: not found in asm/ (run ./build.sh)")
        print(text)
        return
    sys.exit(diff(sys.argv[2]))


if __name__ == "__main__":
    main()
