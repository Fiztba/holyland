#!/usr/bin/env python3
"""Carve the library translation units under src/sce/ out of splat's asm.

    tools/carve.py            place every unit, rewrite the splat subsegments
                              and config/link_order.us.txt
    tools/carve.py --check    place every unit and report, write nothing

Every .c and .s under src/sce/ is compiled with tools/compile_c.sh into
build/carve/ and placed in the retail ELF by tools/lib_layout.py. Each unit
must be placed whole: every section found, and every initialized byte equal
to the retail one. Then:

  * config/kfiv.us.yaml gets one subsegment per unit and section, and an asm
    (or data/rodata/bss) subsegment for each stretch between units that no
    unit claims. A stretch that is only the next unit's alignment fill gets
    none: ld reproduces it.
  * config/link_order.us.txt gets one order that every section agrees with:
    a merge of the per-section address orders (crt0 first, then the game's
    splat units, then the library and gap units). Units in different orders
    in different sections are an error.
  * config/symbol_addrs.carve.txt names, for splat, every global the units
    define and every symbol they reference that the asm defines (resolved
    from the retail jal targets and %hi/%lo pairs). The asm and the C then
    agree on names. A name config/symbol_addrs.txt puts at another address is
    an error: one of the two is wrong.

Run tools/split.py and the build afterwards (./build.sh does both when asm/ is
gone). The game's own asm units keep their names: text, data, rodata, ...
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib_layout import (R_26, R_HI16, R_LO16, Obj, layout, load_retail,  # noqa: E402
                        load_symbols, sext16)

ROOT = Path(__file__).resolve().parent.parent
YAML = ROOT / "config/kfiv.us.yaml"
LINK_ORDER = ROOT / "config/link_order.us.txt"
CARVE_SYMS = ROOT / "config/symbol_addrs.carve.txt"
OBJDIR = "build/carve"
ROM_BASE = 0x00100000

# Splat sections of the main segment: (section, splat type, C-owned type, base
# unit name, vram start, vram end, alignment of a gap unit's start).
SECTIONS = [
    (".text", "asm", "c", "text", 0x00100000, 0x00249668, 8),
    (".data", "data", ".data", "data", 0x00249680, 0x0043B1C8, 4),
    (".rodata", "rodata", ".rodata", "rodata", 0x0043D980, 0x00444B58, 4),
    (".bss", "bss", ".bss", "bss", 0x00447480, 0x01E57C70, 4),
]
BSS_ROM = 0x346FE4

# The subsegments carve.py does not touch, in ROM order, keyed by the section
# they follow.
FIXED = {
    ".data": ['      - { start: 0x33B1D0, type: data, name: vudata, linker_section: .vudata }'],
    ".rodata": ['      - [0x344B80, lit4, lit4]',
                '      - [0x346700, sdata, sdata]',
                '      - { start: 0x346FE4, type: sbss, vram: 0x00447000, name: sbss }'],
}
GAME_UNITS = ["asm/text.s", "asm/data/data.data.s", "asm/data/vudata.data.s",
              "asm/data/rodata.rodata.s", "asm/data/lit4.lit4.s", "asm/data/sdata.sdata.s",
              "asm/data/sbss.sbss.s", "asm/data/bss.bss.s"]
# the asm units of the sections carve.py leaves whole
FIXED_UNITS = ["asm/data/vudata.data.s", "asm/data/lit4.lit4.s", "asm/data/sdata.sdata.s",
               "asm/data/sbss.sbss.s"]
# defined by config/kfiv.us.ld.in, not by any unit
LINKER_SYMS = {"_gp", "_fbss", "_end", "_stack", "_stack_size", "_heap_size"}
BEGIN = "      # --- tools/carve.py: generated from here ---"
END = "      # --- tools/carve.py: generated up to here ---"


def align_up(v, a):
    return (v + a - 1) // a * a


def compile_all(units):
    def one(u):
        out = ROOT / OBJDIR / (u + ".o")
        r = subprocess.run(["tools/compile_c.sh", u, f"{OBJDIR}/{u}.o"], cwd=ROOT,
                           capture_output=True, text=True)
        return u, r.returncode, r.stderr
    if (ROOT / OBJDIR).exists():
        subprocess.run(["rm", "-rf", str(ROOT / OBJDIR)], check=True)
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        bad = [(u, err) for u, rc, err in ex.map(one, units) if rc]
    for u, err in bad:
        print(f"{u}: compile failed\n{err}", file=sys.stderr)
    if bad:
        sys.exit(1)


def is_pad(sec, cursor, addr, align):
    """Is [cursor, addr) only the fill ld puts before a unit of this alignment?

    The run must be what the unit's alignment explains, and zero: a unit with
    a large alignment (64) can sit after a short object no unit claims yet
    (libvu0's .data before cdvd000's). .bss has no bytes to check; a variable
    hiding in its fill would show up as an undefined symbol at link time."""
    if addr != align_up(cursor, max(align, 1)):
        return False
    if sec == ".bss":
        return True
    vbase, data = RETAIL[sec]
    return not any(data[cursor - vbase:addr - vbase])


def asm_path(sec, name):
    return {".text": f"asm/{name}.s", ".data": f"asm/data/{name}.data.s",
            ".rodata": f"asm/data/{name}.rodata.s", ".bss": f"asm/data/{name}.bss.s"}[sec]


def unit_symbols(placed):
    """name -> (addr, is_func) for globals the units define or reference."""
    import struct
    retail = load_retail(str(ROOT / "baserom/us/SLUS_203.18"))
    tbase, tdata = retail[".text"]
    out, clash = {}, []

    def add(name, addr, func, why, size=0):
        if name in LINKER_SYMS:
            return
        if name in out and out[name][0] != addr:
            clash.append(f"{name}: {out[name][0]:#x} and {addr:#x} ({why})")
        if name not in out or size:
            out[name] = (addr, func, size)

    from elftools.elf.elffile import ELFFile
    for unit, secs in placed.items():
        path = ROOT / OBJDIR / (unit + ".o")
        o = Obj(str(path), unit)
        with open(path, "rb") as f:
            e = ELFFile(f)
            names = {i: s.name for i, s in enumerate(e.iter_sections())}
            symlist = list(e.get_section_by_name(".symtab").iter_symbols())
            starts = {}   # section -> sorted symbol offsets, to size unsized asm labels
            for s in symlist:
                if isinstance(s["st_shndx"], int) and s["st_info"]["type"] != "STT_SECTION":
                    starts.setdefault(names.get(s["st_shndx"]), set()).add(s["st_value"])
            for s in symlist:
                shndx = s["st_shndx"]
                sec = names.get(shndx) if isinstance(shndx, int) else None
                if s["st_info"]["bind"] == "STB_GLOBAL" and sec in secs:
                    size = s["st_size"] or min([v for v in starts[sec] if v > s["st_value"]]
                                               + [secs[sec][1]]) - s["st_value"]
                    add(s.name, secs[sec][0] + s["st_value"],
                        s["st_info"]["type"] == "STT_FUNC", f"defined by {unit}", size)
        for sec in (".data", ".rodata"):
            if sec not in secs:
                continue
            rbase, rdata = retail[sec]
            sdata = o.secs[sec][3]
            for off, typ, tsec, sval, sname in o.relocs[sec]:
                if typ == 2 and tsec is None and sname:   # R_MIPS_32 to an undefined symbol
                    ow = struct.unpack_from("<I", sdata, off)[0]
                    rw = struct.unpack_from("<I", rdata, secs[sec][0] - rbase + off)[0]
                    add(sname, rw - ow, False, f"pointed to by {unit} {sec}")
        if ".text" not in secs:
            continue
        base = secs[".text"][0]
        data = o.secs[".text"][3]
        hi = {}
        for off, typ, tsec, sval, sname in sorted(o.relocs[".text"]):
            if tsec is not None or not sname:
                continue
            ow = struct.unpack_from("<I", data, off)[0]
            rw = struct.unpack_from("<I", tdata, base - tbase + off)[0]
            if typ == R_26:
                add(sname, ((rw & 0x3FFFFFF) << 2) | ((base + off) & 0xF0000000), True,
                    f"called from {unit}")
            elif typ == R_HI16:
                hi.setdefault(sname, []).append((ow, rw))
            elif typ == R_LO16 and sname in hi:
                for how, hrw in hi.pop(sname):
                    addend = ((how & 0xFFFF) << 16) + sext16(ow)
                    add(sname, ((hrw & 0xFFFF) << 16) + sext16(rw) - addend, False,
                        f"referenced from {unit}")
    return out, clash


RETAIL = {}


def main():
    RETAIL.update(load_retail(str(ROOT / "baserom/us/SLUS_203.18")))
    check = "--check" in sys.argv
    units = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src/sce").rglob("*")
                   if p.suffix in (".c", ".s"))
    compile_all(units)
    rows = layout(str(ROOT / OBJDIR))

    # unit -> {section: (addr, size, align)}; every section must be placed
    placed, problems = {}, []
    for unit, sec, size, align, addr, how, ok in rows:
        if addr is None or ok == "NO":
            problems.append(f"{unit} {sec}: {how}, bytes {ok}")
            continue
        placed.setdefault(unit, {})[sec] = (addr, size, align)
    if problems:
        print("not placed whole:\n  " + "\n  ".join(problems), file=sys.stderr)
        sys.exit(1)

    # per-section order; units must not overlap
    order = {}
    for sec, *_ in SECTIONS:
        seq = sorted((v[sec][0], u) for u, v in placed.items() if sec in v)
        for (a1, u1), (a2, u2) in zip(seq, seq[1:]):
            if a1 + placed[u1][sec][1] > a2:
                problems.append(f"{sec}: {u1} overlaps {u2}")
        order[sec] = [u for _, u in seq]
    if problems:
        print("\n".join(problems), file=sys.stderr)
        sys.exit(1)

    # Subsegments, and each section's sequence of link-order nodes (unit
    # paths, asm paths). The first asm stretch of a section is the game's
    # unit and keeps the section's base name; units before it (crt0) are
    # "head" units.
    yaml_lines, seqs, head, game = [], [], set(), set()
    first_addr = {}
    n_gaps = 0
    for sec, asm_type, c_type, base, start, end, gap_align in SECTIONS:
        cursor, segs, seq = start, [], []

        def emit(vram, typ, name):
            if sec == ".bss":
                segs.append(f"      - {{ start: 0x{BSS_ROM:06X}, type: {typ}, vram: 0x{vram:08X}, name: {name} }}")
            else:
                segs.append(f"      - [0x{vram - ROM_BASE:06X}, {typ}, {name}]")

        def gap(upto):
            nonlocal n_gaps
            g = start if cursor == start else align_up(cursor, gap_align)
            name = base if not any(n in game for n in seq) and g < upto else None
            if g >= upto:
                return
            if name is None:
                name = f"{base}_{g:08X}"
                n_gaps += 1
            emit(g, asm_type, name)
            path = asm_path(sec, name)
            seq.append(path)
            first_addr[path] = g
            if name == base:
                game.add(path)

        for u in order[sec]:
            addr, size, align = placed[u][sec]
            if not (cursor == start and addr == start) and not is_pad(sec, cursor, addr, align):
                gap(addr)
            if not any(n in game for n in seq):
                head.add(u)
            emit(addr, "hasm" if (sec == ".text" and u.endswith(".s")) else c_type,
                 u[len("src/"):].rsplit(".", 1)[0])
            seq.append(u)
            cursor = addr + size
        if cursor < end:
            gap(end)
        yaml_lines += segs + FIXED.get(sec, [])
        seqs.append(seq)
    for u, v in placed.items():
        first_addr[u] = min(a for a, _, _ in v.values())

    # one link order for all sections: a topological merge of the sequences.
    # Head units first, then the game's units in section order, then the rest
    # by address.
    nodes = set(first_addr) | set(FIXED_UNITS)
    succ = {n: set() for n in nodes}
    for seq in seqs:
        for x, y in zip(seq, seq[1:]):
            succ[x].add(y)
    indeg = {n: 0 for n in nodes}
    for x in succ:
        for y in succ[x]:
            indeg[y] += 1
    game_rank = {n: i for i, n in enumerate(GAME_UNITS)}

    def key(n):
        if n in head:
            return (0, first_addr[n])
        if n in game_rank:
            return (1, game_rank[n])
        return (2, first_addr[n])
    ready = sorted((n for n in nodes if not indeg[n]), key=key)
    link = []
    while ready:
        n = ready.pop(0)
        link.append(n)
        for y in succ[n]:
            indeg[y] -= 1
            if not indeg[y]:
                ready.append(y)
        ready.sort(key=key)
    if len(link) != len(nodes):
        print("units are in different orders in different sections", file=sys.stderr)
        sys.exit(1)

    print(f"tools/carve.py: {len(placed)} units placed, {n_gaps} gap units")
    for sec, *_ in SECTIONS:
        total = sum(placed[u][sec][1] for u in order[sec])
        print(f"  {sec}: {len(order[sec])} units, {total:#x} bytes")
    syms, clash = unit_symbols(placed)
    n2a, _ = load_symbols(generated=False)
    for name, (addr, *_) in syms.items():
        if name in n2a and n2a[name] != addr:
            clash.append(f"{name}: symbol_addrs.txt says {n2a[name]:#x}, the units say {addr:#x}")
    if clash:
        print("symbol clashes:\n  " + "\n  ".join(clash), file=sys.stderr)
        sys.exit(1)
    if check:
        return

    lines = ["// Generated by tools/carve.py from the units under src/sce/; do not edit.",
             "// Globals the library units define, and the symbols they reference",
             "// (placed by the retail relocated words)."]
    for name, (addr, func, size) in sorted(syms.items(), key=lambda kv: (kv[1][0], kv[0])):
        if name not in n2a:
            attrs = (["type:func"] if func else []) + ([f"size:0x{size:X}"] if size else [])
            lines.append(f"{name} = 0x{addr:08X};" + (" // " + " ".join(attrs) if attrs else ""))
    CARVE_SYMS.write_text("\n".join(lines) + "\n")

    text = YAML.read_text()
    a = text.index(BEGIN) + len(BEGIN) + 1
    b = text.index(END)
    YAML.write_text(text[:a] + "\n".join(yaml_lines) + "\n" + text[b:])

    lo = LINK_ORDER.read_text().splitlines()
    comments = [l for l in lo if l.startswith("#") or not l.strip()]
    LINK_ORDER.write_text("\n".join(comments + link) + "\n")


if __name__ == "__main__":
    main()
