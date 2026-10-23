#!/usr/bin/env python3
"""Texture packs for --renderer modern (docs/port/NATIVE_DRAW_P8.md, "Texture packs").

The scene's textures come from the uploads that own their VRAM (gsn_standalone.inc, the page-ownership
table). A pack replaces an upload's bytes: the game sends the same upload, the scene decodes the pack's
bytes in its place (the PS2 path and VRAM keep the game's). An upload is named by its key, a hash of its
format, its rectangle's size and its bytes (not where it went), so a pack follows a texture wherever the
game puts it.

  KF4_SA_PACK_DUMP=DIR   the game writes each upload a scene texture or CLUT came from, once:
                         DIR/<key>.bin (its bytes as sent) and DIR/<key>.txt (format, size, where it went)
  KF4_SA_PACK=DIR        the game reads DIR/<key>.bin in place of an upload's bytes (same size only)

This tool turns the dumped uploads into pictures and back:

  texture_pack.py show DUMP            a <key>.png beside each <key>.bin (see below)
  texture_pack.py pack DUMP PACK       each <key>.png in DUMP that differs from its .bin's picture into
                                       PACK/<key>.bin
  texture_pack.py tint DUMP PACK KEY R,G,B [--rect X,Y,W,H]
                                       a colour upload (a CLUT, a 16 or 32-bit texture) with each colour
                                       multiplied by R, G and B (0.0-2.0) into PACK/<key>.bin; --rect only
                                       that part of it (one CLUT of an upload that holds a map's CLUTs: the
                                       one at CBP is the 16 x 16 at the pixel CBP's block is, as the .txt's
                                       bp and bw place the upload)

The pictures: PSMCT32 RGBA as the GS has it (A 0x80 is 1.0); PSMCT24 RGB; PSMCT16/16S RGBA (5 bits a
colour, the A bit 0 or 255); PSMT8 and PSMT8H the indices as grey; the 4-bit formats the indices times 17.
A CLUT is a colour upload: PSMCT32 16 x 16 (8 x 2 for 16 colours), its entries 8-15 and 16-23 of each 32
swapped as the GS reads them (CSM1). Recolouring a texture means editing its CLUT's picture.
"""
import argparse
import os
import sys

from PIL import Image

PSM_BITS = {0x00: 32, 0x01: 24, 0x02: 16, 0x0A: 16, 0x13: 8, 0x14: 4, 0x1B: 8, 0x24: 4, 0x2C: 4,
            0x30: 32, 0x31: 24, 0x32: 16, 0x3A: 16}


def read_info(path):
    info = {}
    with open(path) as f:
        for word in f.read().split():
            if "=" in word:
                k, v = word.split("=", 1)
                info[k] = int(v, 0)
    return info


def uploads(folder):
    for name in sorted(os.listdir(folder)):
        if name.endswith(".bin") and os.path.exists(os.path.join(folder, name[:-4] + ".txt")):
            key = name[:-4]
            yield key, read_info(os.path.join(folder, key + ".txt")), os.path.join(folder, name)


def to_picture(info, data):
    psm, w, h = info["psm"], info["w"], info["h"]
    bits = PSM_BITS[psm]
    pixels = []
    for i in range(w * h):
        if bits == 32:
            pixels.append(tuple(data[i * 4:i * 4 + 4]))
        elif bits == 24:
            pixels.append(tuple(data[i * 3:i * 3 + 3]))
        elif bits == 16:
            v = data[i * 2] | data[i * 2 + 1] << 8
            pixels.append(((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3, 255 if v & 0x8000 else 0))
        elif bits == 8:
            pixels.append(data[i])
        else:
            pixels.append((data[i // 2] >> (4 * (i & 1)) & 15) * 17)
    mode = {32: "RGBA", 24: "RGB", 16: "RGBA", 8: "L", 4: "L"}[bits]
    picture = Image.new(mode, (w, h))
    picture.putdata(pixels)
    return picture


def from_picture(info, picture, data):
    """The upload's bytes with the picture's pixels (the bytes past the rectangle as they were)"""
    psm, w, h = info["psm"], info["w"], info["h"]
    bits = PSM_BITS[psm]
    mode = {32: "RGBA", 24: "RGB", 16: "RGBA", 8: "L", 4: "L"}[bits]
    converted = picture.convert(mode)
    pixels = list(converted.get_flattened_data() if hasattr(converted, "get_flattened_data") else converted.getdata())
    if picture.size != (w, h):
        raise ValueError("the picture is %dx%d, the upload %dx%d" % (picture.size + (w, h)))
    out = bytearray(data)
    for i, p in enumerate(pixels):
        if bits == 32:
            out[i * 4:i * 4 + 4] = bytes(p)
        elif bits == 24:
            out[i * 3:i * 3 + 3] = bytes(p)
        elif bits == 16:
            v = (p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10 | (0x8000 if p[3] >= 128 else 0)
            out[i * 2:i * 2 + 2] = bytes((v & 255, v >> 8))
        elif bits == 8:
            out[i] = p
        else:
            n = (p + 8) // 17
            shift = 4 * (i & 1)
            out[i // 2] = (out[i // 2] & ~(15 << shift)) | (n << shift)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="what", required=True)
    show = sub.add_parser("show")
    show.add_argument("dump")
    pack = sub.add_parser("pack")
    pack.add_argument("dump")
    pack.add_argument("pack")
    tint = sub.add_parser("tint")
    tint.add_argument("dump")
    tint.add_argument("pack")
    tint.add_argument("key")
    tint.add_argument("rgb")
    tint.add_argument("--rect", help="X,Y,W,H: only these pixels of the upload")
    args = parser.parse_args()

    if args.what == "show":
        n = 0
        for key, info, path in uploads(args.dump):
            with open(path, "rb") as f:
                to_picture(info, f.read()).save(os.path.join(args.dump, key + ".png"))
            n += 1
        print("%d pictures in %s" % (n, args.dump))
    elif args.what == "pack":
        os.makedirs(args.pack, exist_ok=True)
        n = 0
        for key, info, path in uploads(args.dump):
            picture_path = os.path.join(args.dump, key + ".png")
            if not os.path.exists(picture_path):
                continue
            with open(path, "rb") as f:
                data = f.read()
            made = from_picture(info, Image.open(picture_path), data)
            if made != data:
                with open(os.path.join(args.pack, key + ".bin"), "wb") as f:
                    f.write(made)
                n += 1
        print("%d uploads changed into %s" % (n, args.pack))
    else:
        info = read_info(os.path.join(args.dump, args.key + ".txt"))
        if PSM_BITS[info["psm"]] < 16:
            sys.exit("%s is an upload of indices (psm 0x%02x): tint its CLUT" % (args.key, info["psm"]))
        with open(os.path.join(args.dump, args.key + ".bin"), "rb") as f:
            data = f.read()
        k = [float(v) for v in args.rgb.split(",")]
        picture = to_picture(info, data)
        x, y, w, h = [int(v) for v in args.rect.split(",")] if args.rect else (0, 0) + picture.size
        part = picture.crop((x, y, x + w, y + h))
        bands = list(part.split())
        for c in range(3):
            bands[c] = bands[c].point(lambda v, m=k[c]: min(255, int(v * m + 0.5)))
        picture.paste(Image.merge(picture.mode, bands), (x, y))
        os.makedirs(args.pack, exist_ok=True)
        with open(os.path.join(args.pack, args.key + ".bin"), "wb") as f:
            f.write(from_picture(info, picture, data))
        print("%s tinted by %s into %s" % (args.key, args.rgb, args.pack))


if __name__ == "__main__":
    main()
