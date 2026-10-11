# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-authored synthetic instruction fixture; contains no game bytes."""
import struct
import hashlib
import json

UPPER_NOP = 0x2ff
LOWER_NOP = 0x8000033c


def upper(op, mask=15, ft=3, fs=2, fd=4):
    return op | mask << 21 | ft << 16 | fs << 11 | fd << 6


def special(selector, mask=0, ft=0, fs=0):
    return 0x3c | (selector & 3) | ((selector & 0x7c) << 4) | mask << 21 | ft << 16 | fs << 11


def lower_special(selector, source, target=0, mask=0):
    return 0x80000000 | special(selector, mask, target, source)


def make_image(head=0x100, mutation=None):
    """Exercise alias ordering, internal stalls, I/Q/CLIP, masked writes and backup.

    VF5 is loaded then consumed immediately, deliberately stalling issue. Store
    and load addresses can alias. Several operations are deliberately unrelated
    to the geometry transform and the head is movable without any special case.
    """
    body = [
        ((15 << 21) | (5 << 16) | (2 << 11), UPPER_NOP),  # LQ vf5,0(vi2)
        (lower_special(0x38, 3, 4), upper(0x28, fs=5, ft=6, fd=7)),  # DIV + ADD
        (0x12345678, 0x80000000 | upper(0x1e, fs=6, fd=8)),  # LOI after MULi
        ((1 << 25) | (15 << 21) | (3 << 16) | (7 << 11), special(0x1f, 15, 7, 5)),  # SQ + CLIP
        (lower_special(0x31, 8, 9, 15), special(0x1d, 5, 10, 7)),  # MR32 + ABS.yw
        (lower_special(0x3d, 2, 11, 15), upper(0x1c, fs=7, fd=12)),  # MFIR + MULq
        ((0x12 << 25) | 0x12345, special(0x14, 15, 13, 12)),  # FCAND + FTOI0
        (lower_special(0x34, 2, 14, 15), special(0x10, 15, 15, 13)),  # LQI + ITOF0
        (lower_special(0x35, 15, 3, 15), special(0x2a, 15, 7, 5)),  # SQI + MULA
        ((0x08 << 25) | (4 << 16) | (4 << 11) | 1, upper(0x29, fs=5, ft=7, fd=16)),  # IADDIU + MADD
        (LOWER_NOP, upper(0x2c, fs=16, ft=8, fd=17)),  # SUB
        ((0x09 << 25) | (4 << 16) | (4 << 11) | 2, upper(0x2b, fs=17, ft=10, fd=18)),  # ISUBIU + MAX
        (LOWER_NOP, special(0x1f, 15, 18, 7)),  # another CLIP
        (None, upper(0x2f, fs=18, ft=10, fd=19)),  # conditional branch resolved below
        ((0x08 << 25) | (6 << 16) | (6 << 11) | 1, upper(0x2a, fs=19, ft=7, fd=20)),  # delay: backup + tail FMAC
    ]
    branch_pc = head + (len(body)-2)*8
    body[-2] = ((0x29 << 25) | (4 << 11) | (((head-branch_pc-8)//8) & 0x7ff), body[-2][1])
    if mutation == 'xgkick': body[4] = (lower_special(0x6c, 2), body[4][1])
    elif mutation == 'internal_branch': body[4] = ((0x20 << 25) | 1, body[4][1])
    elif mutation == 'end': body[4] = (body[4][0], body[4][1] | 0x40000000)
    elif mutation == 'reserved': body[4] = (body[4][0], 0x31)
    elif mutation is not None: raise ValueError(mutation)
    image = bytearray(struct.pack('<II', LOWER_NOP, UPPER_NOP)*2048)
    for index,(low,high) in enumerate(body): struct.pack_into('<II',image,head+8*index,low,high)
    return bytes(image)


def make_alias_image(head=0x180):
    """Upper/lower same-pair aliases and branch's previous-VI-read behavior."""
    body=[
        ((1<<25)|(15<<21)|(3<<16)|(5<<11),upper(0x28,fs=1,ft=2,fd=5)),
        ((15<<21)|(6<<16)|(2<<11),upper(0x28,mask=8,fs=1,ft=2,fd=6)),
        (LOWER_NOP,UPPER_NOP),
        (LOWER_NOP,UPPER_NOP),
        (LOWER_NOP,UPPER_NOP),
        ((0x08<<25)|(4<<16)|(4<<11)|1,UPPER_NOP),
        ((0x29<<25)|(4<<11)|(-7&0x7ff),UPPER_NOP),
        ((0x08<<25)|(6<<16)|(6<<11)|1,special(0x1f,15,2,1)),
    ]
    image=bytearray(struct.pack('<II',LOWER_NOP,UPPER_NOP)*2048)
    for index,(low,high) in enumerate(body):struct.pack_into('<II',image,head+8*index,low,high)
    return bytes(image)


def make_pipeline_image(head=0x200, mutation=None):
    """P reads before/after commits, an EFU throughput stall, and flag prefix."""
    body=[
        (lower_special(0x64,0,5,15),UPPER_NOP),
        (lower_special(0x70,1),UPPER_NOP),
        (lower_special(0x64,0,6,15),UPPER_NOP),
        (LOWER_NOP,upper(0x28,fs=1,ft=2,fd=10)),
        ((0x12<<25)|0xffffff,special(0x1f,15,2,1)),
        (lower_special(0x70,2),UPPER_NOP),
        (lower_special(0x64,0,7,15),UPPER_NOP),
        (LOWER_NOP,UPPER_NOP),
        (lower_special(0x64,0,8,15),UPPER_NOP),
        ((0x08<<25)|(4<<16)|(4<<11)|1,UPPER_NOP),
        ((0x29<<25)|(4<<11)|(-11&0x7ff),UPPER_NOP),
        (lower_special(0x64,0,9,15),UPPER_NOP),
    ]
    if mutation=='unsupported_efu':body[1]=(lower_special(0x71,1),UPPER_NOP)
    elif mutation is not None:raise ValueError(mutation)
    image=bytearray(struct.pack('<II',LOWER_NOP,UPPER_NOP)*2048)
    for index,(low,high) in enumerate(body):struct.pack_into('<II',image,head+8*index,low,high)
    return bytes(image)


def make_repeat_image(head=0x300):
    """Stationary P/flag loop: late scalar, last-pair flags and ordered memory."""
    body=[
        (LOWER_NOP,upper(0x28,fs=1,ft=2,fd=10)),
        (LOWER_NOP,UPPER_NOP),
        ((15<<21)|(11<<16)|(2<<11),UPPER_NOP),
        (lower_special(0x64,0,12,15),UPPER_NOP),
        (lower_special(0x64,0,13,15),UPPER_NOP),
        (lower_special(0x70,3),UPPER_NOP),
        (LOWER_NOP,special(0x14,15,14,11)),
        (LOWER_NOP,special(0x1f,15,2,1)),
        ((1<<25)|(15<<21)|(3<<16)|(10<<11),UPPER_NOP),
        ((0x08<<25)|(4<<16)|(4<<11)|1,UPPER_NOP),
        ((0x29<<25)|(4<<11)|(-11&0x7ff),UPPER_NOP),
        ((0x08<<25)|(6<<16)|(6<<11)|1,upper(0x29,fs=1,ft=2,fd=15)),
    ]
    image=bytearray(struct.pack('<II',LOWER_NOP,UPPER_NOP)*2048)
    for index,(low,high) in enumerate(body):struct.pack_into('<II',image,head+8*index,low,high)
    return bytes(image)


def program():
    image=bytearray(struct.pack('<II',LOWER_NOP,UPPER_NOP)*2048)
    entries={'repeated':0x300,'pipeline':0x600,'alias':0x900,'vf0':0xc00}
    for name,fn,count in [('repeated',make_repeat_image,12),('pipeline',make_pipeline_image,12),('alias',make_alias_image,8)]:
        head=entries[name]; body=fn(head)
        image[head:head+count*8]=body[head:head+count*8]
    # Reachable canonical behavior: upper temporarily changes VF0, then the
    # same-pair lower SQ observes it before VF0 is restored at the pair end.
    head=entries['vf0']
    rows=[((1<<25)|(15<<21)|(3<<16),upper(0x28,fs=1,ft=2,fd=0))]
    rows += [(LOWER_NOP,UPPER_NOP)]*5
    rows += [((0x29<<25)|(4<<11)|(-7&0x7ff),UPPER_NOP),(LOWER_NOP,UPPER_NOP)]
    for i,row in enumerate(rows):struct.pack_into('<II',image,head+i*8,*row)
    return list(struct.iter_unpack('<II',image)),entries

SIZE=16384
def generate(directory):
    pairs, entries = program()
    image = b''.join(struct.pack('<II', *pair) for pair in pairs)
    assert len(image) == SIZE
    # Minimal source-authored ELF32/MIPS header. MPG payloads are deliberately
    # independent of loadable EE sections; this fixture exercises extraction.
    elf = bytearray(64)
    elf[:16] = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', elf, 16, 2, 8, 1, 0, 0, 0, 0, 52, 0, 0, 0, 0, 0)
    offsets = []
    for first in range(0, len(pairs), 256):
        offsets.append(len(elf))
        command = (0x4a << 24) | first  # NUM zero encodes 256 instructions.
        elf.extend(struct.pack('<I', command))
        elf.extend(image[first * 8:(first + 256) * 8])
    metadata = {'version': 1, 'unit': 'VU1', 'initialization': 'zero',
                'elf_sha256': hashlib.sha256(elf).hexdigest(), 'code_size': SIZE,
                'expected_image_sha256': hashlib.sha256(image).hexdigest(),
                'uploads': [{'header_offset': value} for value in offsets]}
    header = ['// Generated from source-authored fixture; keep in external build scratch.\n',
              'namespace authoredVU {\ninline constexpr uint32_t pairs[][2] = {\n']
    header.extend(f'{{0x{lo:08x}u,0x{hi:08x}u}},\n' for lo, hi in pairs)
    header.append('};\n')
    header.extend(f'inline constexpr uint32_t {name} = {pc}u;\n' for name, pc in entries.items())
    header.append('}\n')
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'authored.elf').write_bytes(elf)
    (directory / 'authored.mpg.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    (directory / 'authored.image').write_bytes(image)
    (directory / 'authored_catalog.inc').write_text(''.join(header), encoding='utf-8')
    return metadata
