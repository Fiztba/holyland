#!/usr/bin/env python3
"""Derive the function map and jump tables from a kfiv-decomp build.

Usage: decomp-map.py <decomp-elf> <boot-elf> [--repo <holyland>]

<decomp-elf> is build/kfiv.elf from a built kfiv-decomp checkout
(github.com/Voicedrew11/kfiv-decomp). It links to the retail ROM byte for
byte, so its symbol table gives every function's exact start and size; splat
fixes those boundaries, not how much of the game is matched in C. <boot-elf>
is the user's SLUS_203.18, checked against kfiv/SLUS_203.18.sha256.

Writes kfiv/SLUS_203.18.functions.csv and replaces the jump-table block at
the end of kfiv/config.toml. Functions keep neutral FUN_<address> names: the
generator binds runtime HLE handlers by name, so real SDK names would turn
recompiled library code into stubs.

Jump tables: for every JR through a register other than $ra, the table is
traced back through LW <- ADDU <- ADDIU/LUI (the LUI may be hoisted anywhere
earlier in the function) and read while its words land inside the function.
The script fails if any such JR is left unresolved, or if tables overlap.
"""

import argparse
import hashlib
import os
import struct
import sys

TEXT_END = 0x00249668


def read_elf(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        sys.exit(f'{path}: not a 32-bit little-endian ELF')
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
    sections = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
            '<10I', data, shoff + i * shentsize)
        sections.append(dict(name=name, type=typ, addr=addr, off=off, size=size, link=link, entsize=entsize))
    strtab = sections[shstrndx]
    for s in sections:
        end = data.index(b'\0', strtab['off'] + s['name'])
        s['name'] = data[strtab['off'] + s['name']:end].decode()
    return data, sections


def section(sections, name):
    for s in sections:
        if s['name'] == name:
            return s
    sys.exit(f'missing section {name}')


def functions(data, sections):
    symtab = next((s for s in sections if s['type'] == 2), None)
    if symtab is None:
        sys.exit('decomp ELF has no symbol table')
    strs = sections[symtab['link']]
    found = {}
    for i in range(symtab['size'] // 16):
        name, value, size, info, other, shndx = struct.unpack_from('<IIIBBH', data, symtab['off'] + i * 16)
        if info & 0xF == 2 and 0x00100000 <= value < TEXT_END:
            found.setdefault(value, size)
    starts = sorted(found)
    result = []
    for i, start in enumerate(starts):
        nxt = starts[i + 1] if i + 1 < len(starts) else TEXT_END
        end = start + found[start] if found[start] else nxt
        if end > nxt:
            sys.exit(f'function 0x{start:08X} overlaps 0x{nxt:08X}')
        result.append((start, end))
    return result


class Image:
    def __init__(self, data, sections):
        self.data = data
        self.text = section(sections, '.text')
        self.rodata = section(sections, '.rodata')

    def word(self, address):
        for s in (self.text, self.rodata):
            if s['addr'] <= address < s['addr'] + s['size']:
                return struct.unpack_from('<I', self.data, s['off'] + address - s['addr'])[0]
        raise KeyError(address)

    def in_rodata(self, address):
        return self.rodata['addr'] <= address < self.rodata['addr'] + self.rodata['size']


def s16(value):
    return value - 0x10000 if value & 0x8000 else value


def table_address(img, start, jr):
    """Trace a JR's register back to its table address, or None."""
    target = (img.word(jr) >> 21) & 31
    for lw in range(jr - 4, max(start, jr - 80) - 4, -4):
        insn = img.word(lw)
        if insn >> 26 == 0x23 and (insn >> 16) & 31 == target:
            break
    else:
        return None
    base, offset = (insn >> 21) & 31, s16(insn & 0xFFFF)
    for addu in range(lw - 4, max(start, lw - 40) - 4, -4):
        insn = img.word(addu)
        if insn >> 26 == 0 and insn & 0x3F == 0x21 and (insn >> 11) & 31 == base:
            break
    else:
        return None
    for reg in ((insn >> 21) & 31, (insn >> 16) & 31):
        for low in range(addu - 4, max(start, addu - 32) - 4, -4):
            insn = img.word(low)
            if insn >> 26 == 9 and (insn >> 16) & 31 == reg:
                src = (insn >> 21) & 31
                for high in range(low if src != reg else low - 4, start - 4, -4):
                    hi = img.word(high)
                    if hi >> 26 == 0xF and (hi >> 16) & 31 == src:
                        address = ((hi & 0xFFFF) << 16) + s16(insn & 0xFFFF) + offset
                        if img.in_rodata(address & 0xFFFFFFFF):
                            return address & 0xFFFFFFFF
                        break
                break
    return None


def jump_tables(img, funcs):
    tables = {}
    for start, end in funcs:
        for jr in range(start, end, 4):
            insn = img.word(jr)
            if insn >> 26 != 0 or insn & 0x3F != 8 or (insn >> 21) & 31 == 31:
                continue
            address = table_address(img, start, jr)
            if address is None:
                sys.exit(f'unresolved JR at 0x{jr:08X} in function 0x{start:08X}')
            entries = []
            while img.in_rodata(address + 4 * len(entries)):
                target = img.word(address + 4 * len(entries))
                if not (start <= target < end and target % 4 == 0):
                    break
                entries.append(target)
            if not entries:
                sys.exit(f'empty table 0x{address:08X} for JR at 0x{jr:08X}')
            tables.setdefault(address, (jr, start, entries))
    previous_end = 0
    for address in sorted(tables):
        if address < previous_end:
            sys.exit(f'jump table 0x{address:08X} overlaps the previous one')
        previous_end = address + 4 * len(tables[address][2])
    return tables


BEGIN = '# BEGIN jump tables (scripts/maintainer/decomp-map.py)'
END = '# END jump tables'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('decomp_elf')
    parser.add_argument('boot_elf')
    parser.add_argument('--repo', default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    args = parser.parse_args()

    kfiv = os.path.join(args.repo, 'kfiv')
    expected = open(os.path.join(kfiv, 'SLUS_203.18.sha256'), encoding='utf-8').read().split()[0].lower()
    boot = open(args.boot_elf, 'rb').read()
    if hashlib.sha256(boot).hexdigest() != expected:
        sys.exit(f'{args.boot_elf}: SHA256 does not match kfiv/SLUS_203.18.sha256')

    ddata, dsections = read_elf(args.decomp_elf)
    bdata, bsections = read_elf(args.boot_elf)
    dtext, btext = section(dsections, '.text'), section(bsections, '.text')
    # The decomp's section may carry trailing alignment padding past the retail size.
    size = btext['size']
    if dtext['addr'] != btext['addr'] or dtext['size'] < size or \
            ddata[dtext['off']:dtext['off'] + size] != bdata[btext['off']:btext['off'] + size] or \
            any(ddata[dtext['off'] + size:dtext['off'] + dtext['size']]):
        sys.exit('decomp .text is not byte-identical to the boot ELF; rebuild kfiv-decomp')

    funcs = functions(ddata, dsections)
    img = Image(bdata, bsections)
    for (_, end), (start, _) in zip(funcs, funcs[1:] + [(TEXT_END, 0)]):
        for address in range(end, start, 4):
            if img.word(address):
                sys.exit(f'code at 0x{address:08X} is outside every function')
    tables = jump_tables(img, funcs)

    with open(os.path.join(kfiv, 'SLUS_203.18.functions.csv'), 'w', newline='\n') as out:
        out.write('Name,Start,End,Size\n')
        for start, end in funcs:
            out.write(f'FUN_{start:08x},0x{start:08X},0x{end:08X},{end - start}\n')

    lines = [BEGIN, '# One table per switch: the JR it serves, its function and every case target.']
    for address in sorted(tables):
        jr, start, entries = tables[address]
        lines += ['', '[[jump_tables.table]]', f'# JR at 0x{jr:08X} in FUN_{start:08x}',
                  f'address = "0x{address:08X}"', 'entries = [']
        lines += [f'  {{ index = {i}, target = "0x{t:08X}" }},' for i, t in enumerate(entries)]
        lines.append(']')
    lines.append(END)
    config_path = os.path.join(kfiv, 'config.toml')
    with open(config_path, encoding='utf-8', newline='') as f:
        config = f.read()
    eol = '\r\n' if '\r\n' in config else '\n'
    config = config.replace('\r\n', '\n')
    if BEGIN in config:
        config = config[:config.index(BEGIN)]
    config = config.rstrip('\n') + '\n\n' + '\n'.join(lines) + '\n'
    with open(config_path, 'w', encoding='utf-8', newline='') as out:
        out.write(config.replace('\n', eol))
    print(f'{len(funcs)} functions, {len(tables)} jump tables '
          f'({sum(len(t[2]) for t in tables.values())} entries)')


if __name__ == '__main__':
    main()
