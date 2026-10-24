#!/usr/bin/env python3
"""Find the "one picture off" pictures in a run of presented pictures: a flash recorder's folder (F8,
docs/port/README.md "Debugging") or a folder of --dump-frames pictures.

    python tools/flash_find.py FOLDER [--threshold X] [--span N] [--width W] [--top N] [--sheet FILE]
                               [--frames] [--level L]

The pictures are taken in the order of the numbers in their names (the recorder's 0000.png, 0001.png, ...;
--dump-frames' frame_000410.png, frame_000410_250.png, ...). Each picture is compared with the pictures k
before and k after it, for k from 1 to --span (8 by default: a flash can last all the pictures between of one
game frame, 7 at 240), at a small size (--width, 320 across):

- a pixel is off where those two neighbours agree (within 24 levels, every channel) and the picture is more
  than 24 levels from both;
- only off pixels whose whole 5 x 5 neighbourhood is off count (a shard or a hole is a solid shape; the
  scattered edges a turn or a step leaves are not);
- "off" is the share of the picture counted, the largest over k.

--frames compares each picture between with its game frames instead (the recorder's pictures.txt gives each
picture's vblank and phase; --dump-frames' names do too): the game frames are the vblanks' own pictures (phase
0) of one parity, the one whose blends fit best, and a picture between is off where it differs by more than
--level (24) from the game frames before and after it and from their blend at its time. That finds what lasts
all the pictures between of one game frame (their neighbours then share it) and, with a lower --level and
--width 0 (full size), a faint patch: a shadow's quarter darker on a lit wall is 15-30 levels.

A picture whose off is at least --threshold (0.2% of the picture by default) is flagged. A cut or a fast
turn changes the picture before and after alike and scores low. Pictures the same as the one before (the
game or the presenter fell behind) are counted apart.

The flagged pictures go into a contact sheet (FOLDER/flashes.png, or --sheet): each row the neighbour before,
the flagged picture, the neighbour after, and where it is off (white). The list is printed, the worst first,
and runs of flagged pictures one after another are named once each as a "flash". Needs Pillow and numpy.
"""
import argparse
import os
import re
import sys

try:
    import numpy as np
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("flash_find.py needs Pillow and numpy (pip install pillow numpy)")

LEVEL = 24
SOLID = 1  # the neighbourhood's half size: 3 x 3


def order_key(name):
    return [int(n) for n in re.findall(r"\d+", name)] or [0]


def load_small(path, width):
    im = Image.open(path).convert("RGB")
    if width <= 0:
        return np.asarray(im, dtype=np.int16)
    h = max(1, round(im.height * width / im.width))
    return np.asarray(im.resize((width, h), Image.BILINEAR), dtype=np.int16)


def erode(mask, r):
    """mask where every pixel within r (a square) is set"""
    h, w = mask.shape
    pad = np.zeros((h + 2 * r, w + 2 * r), dtype=bool)
    pad[r:r + h, r:r + w] = mask
    out = np.ones_like(mask)
    for dy in range(2 * r + 1):
        for dx in range(2 * r + 1):
            out &= pad[dy:dy + h, dx:dx + w]
    return out


def compare(prev, cur, nxt):
    """(off share, mask) of cur against two neighbours"""
    d_pc = np.abs(cur - prev).max(axis=2)
    d_cn = np.abs(cur - nxt).max(axis=2)
    d_pn = np.abs(prev - nxt).max(axis=2)
    off = erode((d_pn <= LEVEL) & (d_pc > LEVEL) & (d_cn > LEVEL), SOLID)
    return float(off.mean()), off


def times(folder, names):
    """each picture's time in vblanks (the recorder's pictures.txt, or frame_V[_PPP] names), or None"""
    listed = {}
    path = os.path.join(folder, "pictures.txt")
    if os.path.exists(path):
        for line in open(path):
            f = line.split()
            if len(f) >= 3 and not line.startswith("#"):
                listed[f[0]] = int(f[1]) + float(f[2])
    out = []
    for n in names:
        m = re.match(r"frame_(\d+)(?:_(\d+))?\.png$", n)
        out.append(listed.get(n, int(m.group(1)) + (int(m.group(2)) / 1000.0 if m and m.group(2) else 0.0) if m else None))
    return None if None in out else out


def against_frames(small, at, level):
    """[(off, i, a, b, mask)] of each picture between against its game frames a and b (the parity that fits best)"""
    best = None
    for parity in (0, 1):
        keys = [i for i, t in enumerate(at) if t == int(t) and int(t) % 2 == parity]
        found, err = [], 0.0
        for a, b in zip(keys, keys[1:]):
            A, B = small[a], small[b]
            for i in range(a + 1, b):
                u = (at[i] - at[a]) / (at[b] - at[a])
                P = small[i]
                d = np.abs(P - (A * (1 - u) + B * u)).max(axis=2)
                err += float(d.mean())
                off = erode((d > level) & (np.abs(P - A).max(axis=2) > level) & (np.abs(P - B).max(axis=2) > level), SOLID)
                found.append((float(off.mean()), i, a, b, off))
        if found and (best is None or err < best[0]):
            best = (err, found)
    return best[1] if best else []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("folder")
    ap.add_argument("--threshold", type=float, default=0.002, help="share of the picture off to flag it (0.002)")
    ap.add_argument("--span", type=int, default=8, help="neighbours up to this many pictures away (8)")
    ap.add_argument("--width", type=int, default=320, help="compare at this width (320)")
    ap.add_argument("--top", type=int, default=24, help="at most this many rows in the sheet (24)")
    ap.add_argument("--sheet", help="the contact sheet (FOLDER/flashes.png)")
    ap.add_argument("--frames", action="store_true", help="compare each picture between with its game frames")
    ap.add_argument("--level", type=int, default=LEVEL, help="levels off to count a pixel, with --frames (24)")
    args = ap.parse_args()

    names = [n for n in os.listdir(args.folder) if n.lower().endswith(".png") and not n.startswith("flashes")]
    listed = os.path.join(args.folder, "pictures.txt")
    if os.path.exists(listed):
        # (a recorder's folder: its pictures only, not other PNGs put there since)
        keep = {line.split()[0] for line in open(listed) if line.strip() and not line.startswith("#")}
        names = [n for n in names if n in keep]
    names.sort(key=order_key)
    if len(names) < 3:
        sys.exit("fewer than 3 pictures in %s" % args.folder)
    small = [load_small(os.path.join(args.folder, n), args.width) for n in names]
    if len({a.shape for a in small}) != 1:
        sys.exit("the pictures are not all the same size")

    repeats = sum(1 for i in range(1, len(small)) if np.array_equal(small[i], small[i - 1]))
    found = []
    if args.frames:
        at = times(args.folder, names)
        if at is None:
            sys.exit("--frames needs each picture's vblank and phase: pictures.txt, or frame_V[_PPP].png names")
        found = [(off, i, (a, b), mask) for off, i, a, b, mask in against_frames(small, at, args.level)
                 if off >= args.threshold]
    for i in range(1, len(names) - 1) if not args.frames else ():
        best = None
        for k in range(1, args.span + 1):
            if i - k < 0 or i + k >= len(names):
                break
            off, mask = compare(small[i - k], small[i], small[i + k])
            if best is None or off > best[0]:
                best = (off, i, k, mask)
        if best[0] >= args.threshold:
            found.append(best)

    flagged = sorted(f[1] for f in found)
    flashes = []
    for i in flagged:
        if flashes and i == flashes[-1][1] + 1:
            flashes[-1][1] = i
        else:
            flashes.append([i, i])
    print("%d pictures, %d the same as the one before, %d flagged (off >= %.2f%%) in %d flashes" %
          (len(names), repeats, len(found), args.threshold * 100, len(flashes)))
    for a, b in flashes:
        print("  flash: %s%s (%d pictures)" % (names[a], "" if a == b else " to " + names[b], b - a + 1))
    found.sort(key=lambda f: -f[0])
    def pair(i, k):
        return k if isinstance(k, tuple) else (i - k, i + k)
    for off, i, k, _ in found:
        a, b = pair(i, k)
        print("  %-28s off %6.2f%%  (against %s and %s)" % (names[i], off * 100, names[a], names[b]))
    if not found:
        return

    shown = found[: args.top]
    h, w = small[0].shape[:2]
    sheet = Image.new("RGB", (w * 4, (h + 14) * len(shown)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for row, (off, i, k, mask) in enumerate(shown):
        y = row * (h + 14)
        a_, b_ = pair(i, k)
        for col, a in enumerate((small[a_], small[i], small[b_])):
            sheet.paste(Image.fromarray(a.astype("uint8"), "RGB"), (col * w, y + 14))
        sheet.paste(Image.fromarray((mask * 255).astype("uint8"), "L").convert("RGB"), (3 * w, y + 14))
        draw.text((2, y + 1), "%s  |  %s (off %.2f%%)  |  %s  |  where" % (names[a_], names[i], off * 100, names[b_]),
                  fill=(255, 220, 0))
    out = args.sheet or os.path.join(args.folder, "flashes.png")
    sheet.save(out)
    print("sheet: %s" % out)


if __name__ == "__main__":
    main()
