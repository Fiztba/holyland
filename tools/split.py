#!/usr/bin/env python3
"""Run splat on build/kfiv.us.yaml (tools/carve.py writes it), then adapt its output for ee-as 2.9.

    tools/split.py [config.yaml ...]

The build assembles splat's output with the period assembler, ee-as
2.9-991111, which predates several things spimdisasm writes:

  * Floats/doubles. spimdisasm prints them with 10 significant digits and
    some values (denormals) do not survive the conversion; every word is
    emitted as `.word` instead (patched below, before splat runs).
  * `%gp_rel(sym)`. Old gas has no explicit gp-relative operator. It emits a
    single $gp-relative instruction with an R_MIPS_GPREL16 reloc for
    `lw $3, sym` when it knows sym is small data, which `.extern sym, 4`
    tells it (the build assembles with -G 8). So
        lw    $3, %gp_rel(sym)($28)   ->  lw $3, sym
        addiu $3, $28, %gp_rel(sym)   ->  la $3, sym
    and each file gets an `.extern` line per gp-relative symbol.
  * VU0 macro-mode special registers are bare words: `ACC`, `Q`, `I`, `R`.

.vudata is VIF/DMA packet data: spimdisasm reads some of its words as
pointers (into .bss, where C units now own the space), so its `.word sym`
operands are written back as numbers.

Every asm unit also gets an `.align` after its `.section` line: the largest
power of two (up to 16) that divides its start address. The unit before it
in the link may end short of that address (a C object's section is not
padded to its successor), and ld reproduces the fill only from the next
input section's alignment.
"""
import re
import sys
from pathlib import Path

from spimdisasm.mips.symbols.MipsSymbolBase import SymbolBase

SymbolBase.isFloat = lambda self, index: False
SymbolBase.isDouble = lambda self, index: False

from splat.scripts import split  # noqa: E402  (import after the patch)

ROOT = Path(__file__).resolve().parent.parent

# `op reg, %gp_rel(sym)($28)` and `addiu reg, $28, %gp_rel(sym)`
GP_MEM = re.compile(r"^(\s*(?:/\*.*?\*/)?\s*)(\S+)(\s+)(\$\w+), %gp_rel\(([^)]+)\)\(\$28\)")
GP_ADDIU = re.compile(r"^(\s*(?:/\*.*?\*/)?\s*)addiu(\s+)(\$\w+), \$28, %gp_rel\(([^)]+)\)")
VU_SPECIAL = re.compile(r"\$(ACC|Q|I|R)\b")
# first address comment: `/* 12AC50 0022AC50 ... */` (code/data) or `/* 00447480 */` (bss)
FIRST_ADDR = re.compile(r"^\s*/\* (?:[0-9A-F]+ )?([0-9A-F]{8})\b", re.M)
SECTION = re.compile(r"^\.section [^\n]*\n", re.M)


def sym_base(expr: str) -> str:
    return re.split(r"[+-]", expr, maxsplit=1)[0].strip()


WORD_SYM = re.compile(r"\.word (?:D_|func_)([0-9A-F]{8})(?: \+ (0x[0-9A-F]+))?$")


def unsymbolize(path: Path) -> None:
    def num(m):
        return f".word 0x{int(m.group(1), 16) + int(m.group(2) or '0', 16):08X}"
    lines = [WORD_SYM.sub(num, l) for l in path.read_text().splitlines()]
    path.write_text("\n".join(lines) + "\n")


def align_unit(path: Path) -> None:
    text = path.read_text()
    m, s = FIRST_ADDR.search(text), SECTION.search(text)
    if not m or not s:
        return
    vram = int(m.group(1), 16)
    log2 = min(4, (vram & -vram).bit_length() - 1)
    path.write_text(text[:s.end()] + f".align {log2}\n" + text[s.end():])


def fix_file(path: Path) -> bool:
    text = path.read_text()
    if "%gp_rel" not in text and not VU_SPECIAL.search(text):
        return False
    externs: set[str] = set()
    out = []
    for line in text.splitlines():
        if "%gp_rel" in line:
            m = GP_ADDIU.match(line)
            if m:
                pre, ws, reg, expr = m.groups()
                line = f"{pre}la{ws}{reg}, {expr}" + line[m.end():]
                externs.add(sym_base(expr))
            else:
                m = GP_MEM.match(line)
                if not m:
                    raise SystemExit(f"{path}: unhandled %gp_rel form: {line.strip()}")
                pre, op, ws, reg, expr = m.groups()
                line = f"{pre}{op}{ws}{reg}, {expr}" + line[m.end():]
                externs.add(sym_base(expr))
        if "$" in line and ("vu" in line or "$v" in line):
            line = VU_SPECIAL.sub(r"\1", line)
        out.append(line)
    # .extern goes right after the macro include; symbols this file defines
    # itself need none (gas sizes those from their own section).
    defined = set(re.findall(r"^\s*[gd]label (\S+)", text, re.M))
    decl = [f".extern {s}, 4" for s in sorted(externs - defined)]
    for i, line in enumerate(out):
        if line.startswith(".include"):
            out[i + 1:i + 1] = [""] + decl
            break
    else:
        out[0:0] = decl
    path.write_text("\n".join(out) + "\n")
    return True


def main() -> None:
    configs = [Path(a) for a in sys.argv[1:]] or [ROOT / "build" / "kfiv.us.yaml"]
    split.main(configs, modes=["all"], verbose=False, use_cache=False)
    units = sorted((ROOT / "asm").rglob("*.s"))
    fixed = sum(fix_file(p) for p in units)
    for p in units:
        align_unit(p)
    unsymbolize(ROOT / "asm/data/vudata.data.s")
    print(f"tools/split.py: adapted {fixed} asm files for ee-as 2.9")


if __name__ == "__main__":
    main()
