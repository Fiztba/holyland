#!/usr/bin/env python3
"""The PS2's own math library, run on a model of the EE: the reference for port/sdk/libm_ps2.c.

    tools/ee_libm_ref.py --elf SLUS_203.18|--disc ISO [--count N] [--seed S] [--out FILE]

The game calls newlib's sinf, cosf, atan2f, atanf, asinf and sqrtf (float, on the EE's FPU) and sin,
cos, sqrt, atan2, atan and acos (double, in software: libgcc's dp-bit). This runs those functions as
the retail program has them, instruction by instruction, on a small interpreter of the EE's integer
unit and FPU, for inputs from a fixed generator, and writes each input and result as hex: the table
tests/libm_test.c checks the PC's versions against.

The FPU is modelled as port/include/port/vu_float.h models the VUs: every result rounded toward zero,
denormals read as zero, an exponent of 255 an ordinary number, a result too large +-0x7FFFFFFF and too
small +-0, a division by zero +-0x7FFFFFFF, the square root of |x|; trunc.w.s saturates. The double
functions use the EE's integer unit only, so their results are exact.

The program comes from --elf, or from the disc image (--disc or KF4_DISC). Nothing of the disc is
written: only the functions' results.
"""
import argparse
import os
import random
import struct
import sys
from fractions import Fraction

MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
RETURN_MAGIC = 0x00000F00
STACK_TOP = 0x01FFF000
FMAX = 0x7FFFFFFF


# ---- The program ---------------------------------------------------------------------------------
def iso_file(iso, wanted):
    """A file from the root of an ISO 9660 image, by name (without the ;1)."""
    with open(iso, "rb") as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        root = pvd[156:190]
        extent, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        f.seek(extent * 2048)
        data = f.read(size)
        at = 0
        while at < len(data):
            length = data[at]
            if length == 0:
                at = (at // 2048 + 1) * 2048
                continue
            rec = data[at:at + length]
            name = rec[33:33 + rec[32]].decode("latin-1").split(";")[0]
            if name.upper() == wanted.upper():
                ext, sz = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
                f.seek(ext * 2048)
                return f.read(sz)
            at += length
    sys.exit("ee_libm_ref: %s is not on the disc" % wanted)


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:4] != b"\x7fELF":
            sys.exit("ee_libm_ref: not an ELF file")
        e_phoff, e_shoff = struct.unpack_from("<II", data, 28)
        e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", data, 42)
        self.segments = []
        for i in range(e_phnum):
            p_type, p_offset, p_vaddr, _, p_filesz, p_memsz = struct.unpack_from("<IIIIII", data, e_phoff + i * e_phentsize)
            if p_type == 1:
                self.segments.append((p_vaddr, data[p_offset:p_offset + p_filesz], p_memsz))
        self.symbols = {}
        for i in range(e_shnum):
            sh = struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize)
            if sh[1] == 2:  # SHT_SYMTAB
                strtab = struct.unpack_from("<IIIIIIIIII", data, e_shoff + sh[6] * e_shentsize)
                for j in range(sh[5] // 16):
                    st_name, st_value = struct.unpack_from("<II", data, sh[4] + j * 16)
                    end = data.index(b"\0", strtab[4] + st_name)
                    name = data[strtab[4] + st_name:end].decode("latin-1")
                    if name:
                        self.symbols[name] = st_value


# addresses in SLUS_203.18 (kfiv-decomp's config/symbol_addrs.txt), for a program without symbols
KNOWN = {
    "sinf": 0x0023A520, "cosf": 0x0023A418, "atan2f": 0x0023AA28, "atanf": 0x0023A170, "asinf": 0x0023A928,
    "sqrtf": 0x0023AB50, "sin": 0x0023A078, "cos": 0x00239F38, "sqrt": 0x0023A818, "atan2": 0x0023A700,
    "atan": 0x00239B28, "acos": 0x0023A610, "_gp": 0x0044CB70,
}


# ---- The EE's FPU --------------------------------------------------------------------------------
def f_value(bits):
    e = (bits >> 23) & 0xFF
    if e == 0:
        return Fraction(0)
    v = Fraction((bits & 0x7FFFFF) | 0x800000) * Fraction(2) ** (e - 150)
    return -v if bits >> 31 else v


def f_round(v, zero_sign=0):
    """an exact value as an EE float: toward zero, +-0 below the smallest, +-FMAX above the largest"""
    if v == 0:
        return zero_sign << 31
    sign = 1 if v < 0 else 0
    a = -v if sign else v
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    # 2^e <= a < 2^(e+1)
    biased = e + 127
    if biased <= 0:
        return sign << 31
    if biased > 255:
        return (sign << 31) | FMAX
    mant = int(a / Fraction(2) ** (e - 23))  # 24 bits, truncated
    return (sign << 31) | (biased << 23) | (mant & 0x7FFFFF)


def f_isqrt_exact(v):
    """sqrt of a non-negative Fraction, truncated to a float"""
    if v == 0:
        return 0
    # scale to get 26+ bits of the root, then truncate: floor(sqrt(v) * 2^k) is exact with isqrt
    k = 60
    num = v.numerator * (1 << (2 * k))
    root = isqrt(num // v.denominator)  # floor(sqrt(floor(v*2^2k))) == floor(sqrt(v)*2^k)
    return f_round(Fraction(root, 1 << k))


def isqrt(n):
    if n < 0:
        raise ValueError
    if n == 0:
        return 0
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def f_add(a, b):
    # an exact zero is +0, but -0 + -0 (as IEEE's, and as SSE rounding toward zero gives)
    neg_zero = (a & 0x80000000) and (b & 0x80000000) and not (a & 0x7F800000) and not (b & 0x7F800000)
    return f_round(f_value(a) + f_value(b), 1 if neg_zero else 0)


def f_sub(a, b):
    return f_add(a, b ^ 0x80000000)


def f_mul(a, b):
    return f_round(f_value(a) * f_value(b), ((a ^ b) >> 31) & 1)


def f_div(a, b):
    if (b & 0x7F800000) == 0:
        return ((a ^ b) & 0x80000000) | FMAX
    return f_round(f_value(a) / f_value(b), ((a ^ b) >> 31) & 1)


def f_sqrt(a):
    return f_isqrt_exact(abs(f_value(a)))


def f_cvt_w(a):
    e = (a >> 23) & 0xFF
    if e >= 158:
        return 0x80000000 if a >> 31 else 0x7FFFFFFF
    v = f_value(a)
    n = int(v)  # toward zero
    return n & MASK32


def f_cvt_s(w):
    if w & 0x80000000:
        w -= 1 << 32
    return f_round(Fraction(w))


# ---- The integer unit ----------------------------------------------------------------------------
def sx32(v):
    v &= MASK32
    return v - (1 << 32) if v & 0x80000000 else v


def sx64(v):
    v &= MASK64
    return v - (1 << 64) if v & (1 << 63) else v


def s32to64(v):
    return sx32(v) & MASK64


class Halt(Exception):
    pass


class EE:
    def __init__(self, elf):
        self.mem = bytearray(0x2000000)
        for vaddr, data, memsz in elf.segments:
            self.mem[vaddr:vaddr + len(data)] = data
        self.base = bytes(self.mem)
        self.gp = elf.symbols.get("_gp")

    def reset(self):
        self.r = [0] * 32
        self.f = [0] * 32
        self.hi = self.lo = self.hi1 = self.lo1 = 0
        self.fcc = 0
        self.r[29] = STACK_TOP
        self.r[31] = RETURN_MAGIC
        if self.gp is not None:
            self.r[28] = self.gp

    # memory
    def rd(self, addr, n):
        addr &= 0x1FFFFFFF
        return int.from_bytes(self.mem[addr:addr + n], "little")

    def wr(self, addr, n, v):
        addr &= 0x1FFFFFFF
        self.mem[addr:addr + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")

    def setr(self, i, v):
        if i:
            self.r[i] = v & MASK64

    def call(self, addr, args=(), fargs=(), limit=5_000_000):
        self.reset()
        for i, a in enumerate(args):
            self.r[4 + i] = a & MASK64
        for i, a in enumerate(fargs):
            self.f[12 + i] = a & MASK32  # the EE's EABI: f12, f13, ...
        pc = addr
        npc = pc + 4
        steps = 0
        while pc != RETURN_MAGIC:
            steps += 1
            if steps > limit:
                raise Halt("too many steps")
            insn = self.rd(pc, 4)
            target = self.step(pc, insn)
            if target is None:
                pc, npc = npc, npc + 4
            elif target == "skip":  # a likely branch not taken: skip its delay slot
                pc, npc = npc + 4, npc + 8
            else:
                # the delay slot first
                dinsn = self.rd(npc, 4)
                t2 = self.step(npc, dinsn)
                if t2 is not None:
                    raise Halt("branch in a delay slot at %08x" % npc)
                pc, npc = target, target + 4
        return self.r[2], self.f[0]

    def step(self, pc, insn):
        op = insn >> 26
        rs = (insn >> 21) & 31
        rt = (insn >> 16) & 31
        rd = (insn >> 11) & 31
        sa = (insn >> 6) & 31
        fn = insn & 63
        imm = insn & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        r = self.r
        if op == 0:  # SPECIAL
            if fn == 0x00: self.setr(rd, s32to64(r[rt] << sa))
            elif fn == 0x02: self.setr(rd, s32to64((r[rt] & MASK32) >> sa))
            elif fn == 0x03: self.setr(rd, s32to64(sx32(r[rt]) >> sa))
            elif fn == 0x04: self.setr(rd, s32to64(r[rt] << (r[rs] & 31)))
            elif fn == 0x06: self.setr(rd, s32to64((r[rt] & MASK32) >> (r[rs] & 31)))
            elif fn == 0x07: self.setr(rd, s32to64(sx32(r[rt]) >> (r[rs] & 31)))
            elif fn == 0x08: return r[rs] & MASK32  # jr
            elif fn == 0x09: self.setr(rd, pc + 8); return r[rs] & MASK32  # jalr
            elif fn == 0x0A:  # movz
                if r[rt] == 0: self.setr(rd, r[rs])
            elif fn == 0x0B:  # movn
                if r[rt] != 0: self.setr(rd, r[rs])
            elif fn == 0x0D: raise Halt("break at %08x" % pc)
            elif fn == 0x0F: pass  # sync
            elif fn == 0x10: self.setr(rd, self.hi)
            elif fn == 0x11: self.hi = r[rs]
            elif fn == 0x12: self.setr(rd, self.lo)
            elif fn == 0x13: self.lo = r[rs]
            elif fn == 0x14: self.setr(rd, r[rt] << (r[rs] & 63))
            elif fn == 0x16: self.setr(rd, r[rt] >> (r[rs] & 63))
            elif fn == 0x17: self.setr(rd, sx64(r[rt]) >> (r[rs] & 63))
            elif fn in (0x18, 0x19):  # mult, multu (the EE also writes rd)
                if fn == 0x18: p = sx32(r[rs]) * sx32(r[rt])
                else: p = (r[rs] & MASK32) * (r[rt] & MASK32)
                self.lo = s32to64(p)
                self.hi = s32to64(p >> 32)
                self.setr(rd, self.lo)
            elif fn in (0x1A, 0x1B):  # div, divu
                self.lo, self.hi = self.divide(fn == 0x1A, r[rs], r[rt])
            elif fn == 0x20 or fn == 0x21: self.setr(rd, s32to64(r[rs] + r[rt]))
            elif fn == 0x22 or fn == 0x23: self.setr(rd, s32to64(r[rs] - r[rt]))
            elif fn == 0x24: self.setr(rd, r[rs] & r[rt])
            elif fn == 0x25: self.setr(rd, r[rs] | r[rt])
            elif fn == 0x26: self.setr(rd, r[rs] ^ r[rt])
            elif fn == 0x27: self.setr(rd, ~(r[rs] | r[rt]))
            elif fn == 0x2A: self.setr(rd, 1 if sx64(r[rs]) < sx64(r[rt]) else 0)
            elif fn == 0x2B: self.setr(rd, 1 if r[rs] < r[rt] else 0)
            elif fn == 0x2C or fn == 0x2D: self.setr(rd, r[rs] + r[rt])
            elif fn == 0x2E or fn == 0x2F: self.setr(rd, r[rs] - r[rt])
            elif fn == 0x38: self.setr(rd, r[rt] << sa)
            elif fn == 0x3A: self.setr(rd, r[rt] >> sa)
            elif fn == 0x3B: self.setr(rd, sx64(r[rt]) >> sa)
            elif fn == 0x3C: self.setr(rd, r[rt] << (sa + 32))
            elif fn == 0x3E: self.setr(rd, r[rt] >> (sa + 32))
            elif fn == 0x3F: self.setr(rd, sx64(r[rt]) >> (sa + 32))
            else: raise Halt("SPECIAL %02x at %08x" % (fn, pc))
            return None
        if op == 1:  # REGIMM
            v = sx64(r[rs])
            if rt in (0, 2): cond = v < 0
            elif rt in (1, 3): cond = v >= 0
            else: raise Halt("REGIMM %02x at %08x" % (rt, pc))
            likely = rt in (2, 3)
            if cond: return (pc + 4 + (simm << 2)) & MASK32
            return "skip" if likely else None
        if op == 2: return ((pc + 4) & 0xF0000000) | ((insn & 0x3FFFFFF) << 2)
        if op == 3:
            self.setr(31, pc + 8)
            return ((pc + 4) & 0xF0000000) | ((insn & 0x3FFFFFF) << 2)
        if op in (4, 5, 6, 7, 20, 21, 22, 23):
            a, b = sx64(r[rs]), sx64(r[rt])
            base = op & 3
            cond = [a == b, a != b, a <= 0, a > 0][base]
            likely = op >= 20
            if cond: return (pc + 4 + (simm << 2)) & MASK32
            return "skip" if likely else None
        if op == 8 or op == 9: self.setr(rt, s32to64(r[rs] + simm)); return None
        if op == 10: self.setr(rt, 1 if sx64(r[rs]) < simm else 0); return None
        if op == 11: self.setr(rt, 1 if r[rs] < (simm & MASK64) else 0); return None
        if op == 12: self.setr(rt, r[rs] & imm); return None
        if op == 13: self.setr(rt, r[rs] | imm); return None
        if op == 14: self.setr(rt, r[rs] ^ imm); return None
        if op == 15: self.setr(rt, s32to64(imm << 16)); return None
        if op == 24 or op == 25: self.setr(rt, r[rs] + simm); return None  # daddi(u)
        if op == 28:  # MMI
            if fn == 0x00:  # madd
                acc = ((self.hi & MASK32) << 32 | (self.lo & MASK32)) + sx32(r[rs]) * sx32(r[rt])
                self.lo = s32to64(acc)
                self.hi = s32to64(acc >> 32)
                self.setr(rd, self.lo)
            elif fn == 0x10: self.setr(rd, self.hi1)
            elif fn == 0x12: self.setr(rd, self.lo1)
            elif fn == 0x18 or fn == 0x19:
                if fn == 0x18: p = sx32(r[rs]) * sx32(r[rt])
                else: p = (r[rs] & MASK32) * (r[rt] & MASK32)
                self.lo1 = s32to64(p)
                self.hi1 = s32to64(p >> 32)
                self.setr(rd, self.lo1)
            elif fn == 0x1A or fn == 0x1B:
                self.lo1, self.hi1 = self.divide(fn == 0x1A, r[rs], r[rt])
            else: raise Halt("MMI %02x at %08x" % (fn, pc))
            return None
        addr = (r[rs] + simm) & MASK32
        if op == 32: v = self.rd(addr, 1); self.setr(rt, v - 256 if v & 0x80 else v); return None
        if op == 33: v = self.rd(addr, 2); self.setr(rt, v - 65536 if v & 0x8000 else v); return None
        if op == 35: self.setr(rt, s32to64(self.rd(addr, 4))); return None
        if op == 36: self.setr(rt, self.rd(addr, 1)); return None
        if op == 37: self.setr(rt, self.rd(addr, 2)); return None
        if op == 39: self.setr(rt, self.rd(addr, 4)); return None
        if op == 55: self.setr(rt, self.rd(addr, 8)); return None
        if op == 40: self.wr(addr, 1, r[rt]); return None
        if op == 41: self.wr(addr, 2, r[rt]); return None
        if op == 43: self.wr(addr, 4, r[rt]); return None
        if op == 63: self.wr(addr, 8, r[rt]); return None
        if op in (26, 27, 44, 45):  # ldl, ldr, sdl, sdr (little-endian)
            aligned = addr & ~7
            k = addr & 7
            word = self.rd(aligned, 8)
            if op == 26:  # ldl: the bytes from the aligned start up to addr go to the top
                shift = (7 - k) * 8
                mask = (MASK64 << shift) & MASK64
                self.setr(rt, (r[rt] & ~mask) | ((word << shift) & mask))
            elif op == 27:  # ldr: the bytes from addr to the aligned end go to the bottom
                shift = k * 8
                mask = MASK64 >> shift
                self.setr(rt, (r[rt] & ~mask) | (word >> shift))
            elif op == 44:  # sdl
                shift = (7 - k) * 8
                mask = MASK64 >> shift
                self.wr(aligned, 8, (word & ~mask) | (r[rt] >> shift))
            else:  # sdr
                shift = k * 8
                mask = (MASK64 << shift) & MASK64
                self.wr(aligned, 8, (word & ~mask) | ((r[rt] << shift) & mask))
            return None
        if op == 49: self.f[rt] = self.rd(addr, 4); return None  # lwc1
        if op == 57: self.wr(addr, 4, self.f[rt]); return None  # swc1
        if op == 17: return self.cop1(pc, insn, rs, rt, rd, sa, fn, simm)
        raise Halt("op %02x at %08x" % (op, pc))

    def divide(self, signed, a, b):
        if signed:
            a, b = sx32(a), sx32(b)
            if b == 0:
                return s32to64(1 if a < 0 else -1), s32to64(a)
            if a == -0x80000000 and b == -1:
                return s32to64(a), 0
            q = abs(a) // abs(b)
            if (a < 0) != (b < 0):
                q = -q
            return s32to64(q), s32to64(a - q * b)
        a, b = a & MASK32, b & MASK32
        if b == 0:
            return s32to64(MASK32), s32to64(a)
        return s32to64(a // b), s32to64(a % b)

    def cop1(self, pc, insn, fmt, ft, fs, fd, fn, simm):
        f = self.f
        if fmt == 0: self.setr(ft, s32to64(f[fs])); return None  # mfc1
        if fmt == 4: f[fs] = self.r[ft] & MASK32; return None  # mtc1
        if fmt == 2 or fmt == 6: return None  # cfc1/ctc1: not modelled
        if fmt == 8:  # bc1
            cond = self.fcc if ft & 1 else not self.fcc
            if cond: return (pc + 4 + (simm << 2)) & MASK32
            return "skip" if ft & 2 else None
        if fmt == 16:  # S
            if fn == 0: f[fd] = f_add(f[fs], f[ft])
            elif fn == 1: f[fd] = f_sub(f[fs], f[ft])
            elif fn == 2: f[fd] = f_mul(f[fs], f[ft])
            elif fn == 3: f[fd] = f_div(f[fs], f[ft])
            elif fn == 4: f[fd] = f_sqrt(f[ft])
            elif fn == 5: f[fd] = f[fs] & 0x7FFFFFFF
            elif fn == 6: f[fd] = f[fs]
            elif fn == 7: f[fd] = f[fs] ^ 0x80000000
            elif fn == 0x16: f[fd] = f_div(f[fs], f_sqrt(f[ft])) if (f[ft] & 0x7F800000) else (f[fs] & 0x80000000) | FMAX
            elif fn == 0x24 or fn == 0x0D: f[fd] = f_cvt_w(f[fs])  # cvt.w.s truncates
            elif fn == 0x32: self.fcc = f_value(f[fs]) == f_value(f[ft])
            elif fn == 0x34: self.fcc = f_value(f[fs]) < f_value(f[ft])
            elif fn == 0x36: self.fcc = f_value(f[fs]) <= f_value(f[ft])
            elif fn == 0x30: self.fcc = False
            else: raise Halt("COP1.S %02x at %08x" % (fn, pc))
            return None
        if fmt == 20 and fn == 0x20:  # cvt.s.w
            f[fd] = f_cvt_s(f[fs])
            return None
        raise Halt("COP1 fmt %d fn %02x at %08x" % (fmt, fn, pc))


# ---- The inputs ----------------------------------------------------------------------------------
def float_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def double_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def inputs_float(rng, n, lo, hi, special):
    out = list(special)
    while len(out) < n:
        r = rng.random()
        if r < 0.6:
            out.append(float_bits(rng.uniform(lo, hi)))
        elif r < 0.8:
            out.append(float_bits(rng.uniform(lo, hi) * 10 ** rng.uniform(-6, 0)))
        else:
            out.append(rng.getrandbits(32) & 0xBFFFFFFF if rng.random() < 0.5 else rng.getrandbits(32))
    return out[:n]


FLOAT1 = {
    "sinf": (-20.0, 20.0, [0, 0x80000000, 0x3F800000, 0x40490FDB, 0xC0490FDB, 0x3FC90FDB, 0x49742400, 0x7F7FFFFF, 0x00400000]),
    "cosf": (-20.0, 20.0, [0, 0x80000000, 0x3F800000, 0x40490FDB, 0x3FC90FDB, 0x4B000000]),
    "atanf": (-50.0, 50.0, [0, 0x80000000, 0x3F800000, 0xBF800000, 0x7F7FFFFF, 0x5F000000]),
    "asinf": (-1.0, 1.0, [0, 0x80000000, 0x3F800000, 0xBF800000, 0x3F000000, 0x3F7FFFFF, 0x3F800001, 0x31000000]),
    "sqrtf": (0.0, 100000.0, [0, 0x80000000, 0x3F800000, 0xBF800000, 0x00000001, 0x007FFFFF, 0x7F7FFFFF, 0x40000000]),
}
DOUBLE1 = {
    "sin": (-20.0, 20.0), "cos": (-20.0, 20.0), "sqrt": (0.0, 1e6), "atan": (-50.0, 50.0), "acos": (-1.0, 1.0),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf")
    parser.add_argument("--disc", default=os.environ.get("KF4_DISC"))
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=4)
    parser.add_argument("--out", default="-")
    args = parser.parse_args()
    if args.elf:
        data = open(args.elf, "rb").read()
    elif args.disc:
        data = iso_file(args.disc, "SLUS_203.18")
    else:
        sys.exit("ee_libm_ref: pass --elf or --disc (or set KF4_DISC)")
    elf = Elf(data)
    if not elf.symbols:  # the retail program: kfiv-decomp's addresses
        elf.symbols = dict(KNOWN)
    sym = elf.symbols
    ee = EE(elf)
    rng = random.Random(args.seed)
    out = sys.stdout if args.out == "-" else open(args.out, "w")
    out.write("# tools/ee_libm_ref.py --count %d --seed %d: function input(s) result, as hex bits\n" % (args.count, args.seed))
    for name, (lo, hi, special) in FLOAT1.items():
        for x in inputs_float(rng, args.count, lo, hi, special):
            ee.mem[:] = ee.base
            _, f0 = ee.call(sym[name], fargs=(x,))
            out.write("%s %08x %08x\n" % (name, x, f0))
    ys = inputs_float(rng, args.count, -2000.0, 2000.0, [0, 0x80000000, 0x3F800000, 0xBF800000])
    xs = inputs_float(rng, args.count, -2000.0, 2000.0, [0, 0, 0x80000000, 0x80000000])
    for y, x in zip(ys, xs):
        ee.mem[:] = ee.base
        _, f0 = ee.call(sym["atan2f"], fargs=(y, x))
        out.write("atan2f %08x %08x %08x\n" % (y, x, f0))
    for name, (lo, hi) in DOUBLE1.items():
        vals = [0.0, -0.0, 1.0, -1.0, 0.5] + [rng.uniform(lo, hi) for _ in range(args.count // 4)]
        vals += [float(struct.unpack("<f", struct.pack("<f", rng.uniform(lo, hi)))[0]) for _ in range(args.count // 4)]
        for v in vals:
            x = double_bits(v)
            ee.mem[:] = ee.base
            v0, _ = ee.call(sym[name], args=(x,))
            out.write("%s %016x %016x\n" % (name, x, v0))
    for _ in range(args.count // 4):
        y = double_bits(float(struct.unpack("<f", struct.pack("<f", rng.uniform(-2000, 2000)))[0]))
        x = double_bits(float(struct.unpack("<f", struct.pack("<f", rng.uniform(-2000, 2000)))[0]))
        ee.mem[:] = ee.base
        v0, _ = ee.call(sym["atan2"], args=(y, x))
        out.write("atan2 %016x %016x %016x\n" % (y, x, v0))


if __name__ == "__main__":
    main()
