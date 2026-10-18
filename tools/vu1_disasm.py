#!/usr/bin/env python3
"""Disassembles the game's VU1 microprograms (game/data/vudata.c).

The five arrays in vudata.c are DMA chains that the game sends to VIF1 with TTE set
(gfx/packet_buffer.c, Packet_UploadVu1Programs). This walks each chain, follows its VIF codes and
prints the microcode each MPG uploads, one line per instruction pair, with its VU1 address.

    python3 tools/vu1_disasm.py                # every program
    python3 tools/vu1_disasm.py --stats        # which instructions are used, and how often
"""
import argparse
import collections
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VUDATA = ROOT / "game" / "data" / "vudata.c"


def load_arrays(path):
    text = path.read_text()
    arrays = {}
    for m in re.finditer(r"u32 (\w+)\[(\d+)\][^=]*=\s*\{(.*?)\};", text, re.S):
        words = [int(w, 16) for w in re.findall(r"0x[0-9A-Fa-f]+", m.group(3))]
        words += [0] * (int(m.group(2)) - len(words))
        arrays[m.group(1)] = words
    return arrays


def vif_stream(words):
    """The words VIF1 sees from a chain sent with TTE: each tag's upper half, then its data."""
    out = []
    pos = 0
    while pos + 4 <= len(words):
        tag = words[pos]
        qwc = tag & 0xFFFF
        tag_id = (tag >> 28) & 7
        out += words[pos + 2 : pos + 4]
        if tag_id == 1:  # CNT: the data follows the tag
            out += words[pos + 4 : pos + 4 + qwc * 4]
            pos += 4 + qwc * 4
        elif tag_id == 7:  # END
            out += words[pos + 4 : pos + 4 + qwc * 4]
            break
        else:
            raise SystemExit("tag id %d not handled" % tag_id)
    return out


def mpg_uploads(stream):
    """(VU1 address in instructions, [64-bit pairs]) for each MPG in a VIF stream."""
    uploads = []
    i = 0
    while i < len(stream):
        code = stream[i]
        cmd = (code >> 24) & 0x7F
        num = (code >> 16) & 0xFF
        imm = code & 0xFFFF
        i += 1
        if cmd == 0x20:  # STMASK
            i += 1
        elif cmd == 0x30 or cmd == 0x31:  # STROW, STCOL
            i += 4
        elif cmd == 0x4A:  # MPG
            n = num or 256
            pairs = [stream[i + 2 * k] | (stream[i + 2 * k + 1] << 32) for k in range(n)]
            uploads.append((imm, pairs))
            i += 2 * n
        elif cmd in (0x50, 0x51):
            raise SystemExit("DIRECT in a program upload")
        elif cmd >= 0x60:
            raise SystemExit("UNPACK in a program upload")
    return uploads


DEST = lambda d: "".join(c for c, b in zip("xyzw", (8, 4, 2, 1)) if d & b)
BC = "xyzw"


def f(n):
    return "vf%02d" % n


def vi(n):
    return "vi%02d" % n


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


UPPER_BC = {0x00: "add", 0x04: "sub", 0x08: "madd", 0x0C: "msub", 0x10: "max", 0x14: "mini", 0x18: "mul"}
UPPER_QI = {0x1C: "mulq", 0x1D: "maxi", 0x1E: "muli", 0x1F: "minii", 0x20: "addq", 0x21: "maddq",
            0x22: "addi", 0x23: "maddi", 0x24: "subq", 0x25: "msubq", 0x26: "subi", 0x27: "msubi"}
UPPER_3 = {0x28: "add", 0x29: "madd", 0x2A: "mul", 0x2B: "max", 0x2C: "sub", 0x2D: "msub",
           0x2E: "opmsub", 0x2F: "mini"}
USPEC_BC = {0x00: "adda", 0x04: "suba", 0x08: "madda", 0x0C: "msuba", 0x18: "mula"}
USPEC = {0x10: "itof0", 0x11: "itof4", 0x12: "itof12", 0x13: "itof15", 0x14: "ftoi0", 0x15: "ftoi4",
         0x16: "ftoi12", 0x17: "ftoi15", 0x1C: "mulaq", 0x1D: "abs", 0x1E: "mulai", 0x1F: "clip",
         0x20: "addaq", 0x21: "maddaq", 0x22: "addai", 0x23: "maddai", 0x24: "subaq", 0x25: "msubaq",
         0x26: "subai", 0x27: "msubai", 0x28: "adda", 0x29: "madda", 0x2A: "mula", 0x2C: "suba",
         0x2D: "msuba", 0x2E: "opmula", 0x2F: "nop"}


def upper(w):
    dest = DEST((w >> 21) & 15)
    ft, fs, fd, op = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    flags = "".join(c for c, b in (("[I]", 31), ("[E]", 30), ("[M]", 29), ("[D]", 28), ("[T]", 27)) if w >> b & 1)
    if op < 0x1C:
        base = op & 0x3C
        return "%s%s.%s %s, %s, %s%s" % (UPPER_BC[base], BC[op & 3], dest, f(fd), f(fs), f(ft), BC[op & 3]), flags, UPPER_BC[base] + "bc"
    if op in UPPER_QI:
        return "%s.%s %s, %s" % (UPPER_QI[op], dest, f(fd), f(fs)), flags, UPPER_QI[op]
    if op in UPPER_3:
        return "%s.%s %s, %s, %s" % (UPPER_3[op], dest, f(fd), f(fs), f(ft)), flags, UPPER_3[op]
    if op >= 0x3C:
        sop = (fd << 2) | (op & 3)
        if sop < 0x10 or 0x18 <= sop < 0x1C:
            base = sop & 0x3C
            name = USPEC_BC[base]
            return "%s%s.%s ACC, %s, %s%s" % (name, BC[sop & 3], dest, f(fs), f(ft), BC[sop & 3]), flags, name + "bc"
        name = USPEC.get(sop, "?uspec%02x" % sop)
        if name == "nop":
            return "nop", flags, name
        if name.startswith(("itof", "ftoi", "abs")):
            return "%s.%s %s, %s" % (name, dest, f(ft), f(fs)), flags, name
        if name == "clip":
            return "clipw.xyz %s, %sw" % (f(fs), f(ft)), flags, name
        return "%s.%s ACC, %s, %s" % (name, dest, f(fs), f(ft)), flags, name
    return "?upper%02x" % op, flags, "?"


LOWER_TOP = {0x00: "lq", 0x01: "sq", 0x04: "ilw", 0x05: "isw", 0x08: "iaddiu", 0x09: "isubiu",
             0x10: "fceq", 0x11: "fcset", 0x12: "fcand", 0x13: "fcor", 0x14: "fseq", 0x15: "fsset",
             0x16: "fsand", 0x17: "fsor", 0x18: "fmeq", 0x1A: "fmand", 0x1B: "fmor", 0x1C: "fcget",
             0x20: "b", 0x21: "bal", 0x24: "jr", 0x25: "jalr", 0x28: "ibeq", 0x29: "ibne",
             0x2C: "ibltz", 0x2D: "ibgtz", 0x2E: "iblez", 0x2F: "ibgez"}
LOWER_OP = {0x30: "iadd", 0x31: "isub", 0x32: "iaddi", 0x34: "iand", 0x35: "ior"}
LSPEC = {0x30: "move", 0x31: "mr32", 0x34: "lqi", 0x35: "sqi", 0x36: "lqd", 0x37: "sqd", 0x38: "div",
         0x39: "sqrt", 0x3A: "rsqrt", 0x3B: "waitq", 0x3C: "mtir", 0x3D: "mfir", 0x3E: "ilwr",
         0x3F: "iswr", 0x40: "rnext", 0x41: "rget", 0x42: "rinit", 0x43: "rxor", 0x64: "mfp",
         0x68: "xtop", 0x69: "xitop", 0x6C: "xgkick", 0x70: "esadd", 0x71: "ersadd", 0x72: "eleng",
         0x73: "erleng", 0x74: "eatanxy", 0x75: "eatanxz", 0x76: "esum", 0x78: "esqrt", 0x79: "ersqrt",
         0x7A: "ercpr", 0x7B: "waitp", 0x7C: "esin", 0x7D: "eatan", 0x7E: "eexp"}


def lower(w, addr):
    top = (w >> 25) & 0x7F
    dest = DEST((w >> 21) & 15)
    it, is_, id_ = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    imm11 = sext(w & 0x7FF, 11)
    if top == 0x40:
        op = w & 63
        if op in LOWER_OP:
            name = LOWER_OP[op]
            if name == "iaddi":
                return "iaddi %s, %s, %d" % (vi(it), vi(is_), sext(id_, 5)), name
            return "%s %s, %s, %s" % (name, vi(id_), vi(is_), vi(it)), name
        if op >= 0x3C:
            sop = (id_ << 2) | (op & 3)
            name = LSPEC.get(sop, "?lspec%02x" % sop)
            fsf, ftf = (w >> 21) & 3, (w >> 23) & 3
            if name == "move" and w == 0x8000033C:
                return "nop", "nop"
            if name in ("move", "mr32"):
                return "%s.%s %s, %s" % (name, dest, f(it), f(is_)), name
            if name in ("lqi", "lqd"):
                return "%s.%s %s, (%s%s)" % (name, dest, f(it), vi(is_), "++" if name == "lqi" else "--"), name
            if name in ("sqi", "sqd"):
                return "%s.%s %s, (%s%s)" % (name, dest, f(is_), vi(it), "++" if name == "sqi" else "--"), name
            if name == "div":
                return "div Q, %s%s, %s%s" % (f(is_), BC[fsf], f(it), BC[ftf]), name
            if name == "sqrt":
                return "sqrt Q, %s%s" % (f(it), BC[ftf]), name
            if name == "rsqrt":
                return "rsqrt Q, %s%s, %s%s" % (f(is_), BC[fsf], f(it), BC[ftf]), name
            if name == "mtir":
                return "mtir %s, %s%s" % (vi(it), f(is_), BC[fsf]), name
            if name == "mfir":
                return "mfir.%s %s, %s" % (dest, f(it), vi(is_)), name
            if name in ("ilwr", "iswr"):
                return "%s.%s %s, (%s)" % (name, dest, vi(it), vi(is_)), name
            if name in ("xtop", "xitop"):
                return "%s %s" % (name, vi(it)), name
            if name == "xgkick":
                return "xgkick %s" % vi(is_), name
            if name == "mfp":
                return "mfp.%s %s, P" % (dest, f(it)), name
            if name.startswith("e"):
                return "%s P, %s%s" % (name, f(is_), BC[fsf] if name in ("esqrt", "ersqrt", "ercpr", "esin", "eatan", "eexp") else ""), name
            return name, name
        return "?lop%02x" % op, "?"
    name = LOWER_TOP.get(top, "?ltop%02x" % top)
    if name in ("lq", "sq"):
        reg = f(it) if name == "lq" else f(is_)
        base = vi(is_) if name == "lq" else vi(it)
        return "%s.%s %s, %d(%s)" % (name, dest, reg, imm11, base), name
    if name in ("ilw", "isw"):
        return "%s.%s %s, %d(%s)" % (name, dest, vi(it), imm11, vi(is_)), name
    if name in ("iaddiu", "isubiu"):
        imm15 = ((w >> 10) & 0x7800) | (w & 0x7FF)
        return "%s %s, %s, 0x%x" % (name, vi(it), vi(is_), imm15), name
    if name in ("b", "bal", "ibeq", "ibne", "ibltz", "ibgtz", "iblez", "ibgez"):
        target = addr + 1 + imm11
        if name == "b":
            return "b 0x%03x" % target, name
        if name == "bal":
            return "bal %s, 0x%03x" % (vi(it), target), name
        if name in ("ibeq", "ibne"):
            return "%s %s, %s, 0x%03x" % (name, vi(it), vi(is_), target), name
        return "%s %s, 0x%03x" % (name, vi(is_), target), name
    if name in ("jr", "jalr"):
        return "%s %s%s" % (name, vi(is_), (", " + vi(it)) if name == "jalr" else ""), name
    if name.startswith(("fs", "fm", "fc")):
        if name in ("fsand", "fseq", "fsor", "fsset"):
            imm12 = ((w >> 10) & 0x800) | (w & 0x7FF)
            return "%s %s, 0x%x" % (name, vi(it), imm12), name
        if name in ("fmand", "fmeq", "fmor"):
            return "%s %s, %s" % (name, vi(it), vi(is_)), name
        if name == "fcget":
            return "fcget %s" % vi(it), name
        return "%s vi01, 0x%06x" % (name, w & 0xFFFFFF), name
    return name, name


def disassemble(pairs, base):
    lines = []
    for k, pair in enumerate(pairs):
        addr = base + k
        lo, hi = pair & 0xFFFFFFFF, pair >> 32
        utext, uflags, _ = upper(hi)
        if hi >> 31 & 1:
            ltext = "loi %g" % struct.unpack("<f", struct.pack("<I", lo))[0]
        else:
            ltext, _ = lower(lo, addr)
        lines.append("%03x: %-36s %-40s %s" % (addr, utext, ltext, uflags))
    return lines


def stats(uploads):
    count = collections.Counter()
    for base, pairs in uploads:
        for pair in pairs:
            lo, hi = pair & 0xFFFFFFFF, pair >> 32
            count["U " + upper(hi)[2]] += 1
            for name, bit in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)):
                if hi >> bit & 1:
                    count["bit " + name] += 1
            if not hi >> 31 & 1:
                count["L " + lower(lo, 0)[1]] += 1
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()
    arrays = load_arrays(VUDATA)
    all_uploads = []
    for name, words in arrays.items():
        uploads = mpg_uploads(vif_stream(words))
        all_uploads += uploads
        if not args.stats:
            for base, pairs in uploads:
                print("; %s: MPG at 0x%03x, %d instructions" % (name, base, len(pairs)))
                print("\n".join(disassemble(pairs, base)))
                print()
    if args.stats:
        for key, n in sorted(stats(all_uploads).items()):
            print("%-14s %d" % (key, n))


if __name__ == "__main__":
    sys.exit(main())
