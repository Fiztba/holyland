#!/usr/bin/env python3
"""Step 2's check: the game's pictures, drawn by paraLLEl-GS, the same every run.

    tools/frame_test.py [--disc ISO] [--exe PATH] [--keep DIR] [--frames N,N,...]

Plays tests/newgame.pad as tools/boot_test.py does, headless with a fixed clock, and writes the picture
shown at a few vblanks (--dump-frames): the Agetec logo, the title menu, the brightness screen, the
opening, and the first map. Runs twice and compares the pictures byte for byte, then checks none of
them is a single colour. Needs a Vulkan GPU that paraLLEl-GS runs on. The pictures are kept with --keep,
for looking at; the disc comes from --disc or KF4_DISC.
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAD = os.path.join(ROOT, "tests", "newgame.pad")
# the logo, the title menu, the brightness screen, the opening, the first map
FRAMES = "200,500,700,1000,2000"


def find_exe():
    for preset in ("linux-gcc", "linux-clang", "windows-clang"):
        for name in ("kf4", "kf4.exe"):
            path = os.path.join(ROOT, "out", preset, name)
            if os.path.exists(path):
                return path
    sys.exit("frame_test: no built program under out/; build one or pass --exe")


def run(exe, disc, frames, folder):
    os.makedirs(folder, exist_ok=True)
    last = max(int(n) for n in frames.split(","))
    cmd = [exe, "--disc", disc, "--fast", "--headless", "--frames", str(last + 1),
           "--clock", "2026-01-01T12:00:00", "--input", PAD,
           "--dump-frames", frames, "--dump-dir", folder]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.exit("frame_test: the program exited with %d" % result.returncode)
    if "[gs] paraLLEl-GS on" not in result.stdout:
        sys.stdout.write(result.stdout)
        sys.exit("frame_test: paraLLEl-GS did not start (no Vulkan GPU, or built without it)")


def colours(path):
    """How many colours the picture has (1: a blank screen)."""
    with open(path, "rb") as f:
        data = f.read()
    width, height = struct.unpack(">II", data[16:24])
    at = data.index(b"IDAT")
    size = struct.unpack(">I", data[at - 4:at])[0]
    raw = zlib.decompress(data[at + 4:at + 4 + size])
    row = width * 3 + 1
    seen = set()
    for y in range(0, height, 4):
        line = raw[y * row + 1:(y + 1) * row]
        for x in range(0, width * 3, 12):
            seen.add(line[x:x + 3])
    return len(seen)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--disc", default=os.environ.get("KF4_DISC"))
    parser.add_argument("--exe")
    parser.add_argument("--frames", default=FRAMES, help="the vblanks to dump (default %s)" % FRAMES)
    parser.add_argument("--keep", help="write the pictures into this folder")
    args = parser.parse_args()
    if not args.disc:
        sys.exit("frame_test: pass --disc or set KF4_DISC")
    exe = args.exe or find_exe()

    folder = args.keep or tempfile.mkdtemp(prefix="kf4_frames_")
    run(exe, args.disc, args.frames, os.path.join(folder, "a"))
    run(exe, args.disc, args.frames, os.path.join(folder, "b"))

    ok = True
    for n in args.frames.split(","):
        name = "frame_%06d.png" % int(n)
        paths = [os.path.join(folder, run_name, name) for run_name in ("a", "b")]
        if not all(os.path.exists(p) for p in paths):
            ok = False
            print("vblank %5s: no picture" % n)
            continue
        digests = [hashlib.sha256(open(p, "rb").read()).hexdigest() for p in paths]
        count = colours(paths[0])
        notes = []
        if digests[0] != digests[1]:
            ok = False
            notes.append("NOT THE SAME both runs")
        if count < 2:
            ok = False
            notes.append("one colour")
        print("vblank %5s: %s %5d colours %s" % (n, digests[0][:16], count, " ".join(notes)))
    print("pictures in %s" % folder)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
