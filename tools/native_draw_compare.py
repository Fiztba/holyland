#!/usr/bin/env python3
"""Native draw's same-game comparison (docs/port/NATIVE_DRAW.md, NATIVE_DRAW_TASKS.md): the game played
twice from a pad script, drawn once the PS2's way and once natively, and the pictures compared.

    tools/native_draw_compare.py --out DIR [--disc ISO] [--exe PATH] [--input PAD] [--frames A-B[/STEP],N,...]
                                 [--kinds items] [--mode compare|onoff] [--scale N] [--frame-rate R]
                                 [--crop X0,Y0,X1,Y1] [--realtime] [-- more options for both runs]

--mode compare (the default) runs with --native-draw KINDS and --native-draw-compare ps2, then native: both
runs queue both kinds of draw (VU1 runs the PS2 path's packets either way), so everything the game does is
the same and only what the native GS draws differs. --mode onoff runs --native-draw off, then KINDS (VU1
does not run for the native draws: the game's timing could differ, though it should not). --mode modern runs
KINDS hosted, then with --renderer modern (P8: the scene's kinds drawn into the scene's own targets).

Each run is headless, at full speed with a fixed clock, no sound, no memory card, a fresh KF4_DATA_DIR,
and writes the pictures at --frames (--dump-frames: at --native-scale N they are N times as large). Then
for each picture: the pixels that differ at all, by more than 1 (any channel), the worst difference and
the box the differences are in; a heat map (red: the difference x 8, over the picture darkened) and, with
--crop, the box of both pictures and the heat map side by side, 4x. DIR/report.txt has the numbers.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLOCK = "2026-01-01T12:00:00"


def find_exe():
    for preset in ("linux-gcc", "linux-clang", "windows-clang"):
        for name in ("kf4", "kf4.exe"):
            path = os.path.join(ROOT, "out", preset, name)
            if os.path.exists(path):
                return path
    sys.exit("native_draw_compare: no built program under out/; build one or pass --exe")


def frame_list(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            span, _, step = part.partition("/")
            a, b = (int(v) for v in span.split("-"))
            out += list(range(a, b + 1, int(step) if step else 1))
        else:
            out.append(int(part))
    return sorted(set(out))


def run(args, folder, extra):
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(os.path.join(folder, "data"))
    frames = frame_list(args.frames)
    cmd = [args.exe, "--disc", args.disc] + ([] if args.realtime else ["--fast"]) + ["--headless", "--frames", str(max(frames) + 1), "--clock", CLOCK,
           "--memcard", "none", "--no-audio", "--input", args.input, "--stats", "--native-scale", str(args.scale),
           "--frame-rate", str(args.frame_rate), "--dump-frames", ",".join(str(f) for f in frames),
           "--dump-dir", folder] + extra + args.rest
    env = dict(os.environ, KF4_DATA_DIR=os.path.join(folder, "data"))
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", env=env)
    with open(os.path.join(folder, "log.txt"), "w") as f:
        f.write(" ".join(cmd) + "\n" + result.stdout)
    if result.returncode != 0:
        sys.stdout.write(result.stdout[-3000:])
        sys.exit("native_draw_compare: the run exited with %d" % result.returncode)
    shutil.rmtree(os.path.join(folder, "data"), ignore_errors=True)
    return [l for l in result.stdout.splitlines() if l.startswith("[stats] native draw") or l.startswith("[stats] standalone")]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", required=True)
    p.add_argument("--disc", default=os.environ.get("KF4_DISC"))
    p.add_argument("--exe")
    p.add_argument("--input", default=os.path.join(ROOT, "tests", "herb.pad"))
    p.add_argument("--frames", default="1800-1840/5")
    p.add_argument("--kinds", default="items")
    p.add_argument("--mode", choices=("compare", "onoff", "modern"), default="compare")
    p.add_argument("--scale", type=int, default=1)
    p.add_argument("--frame-rate", default="30")
    p.add_argument("--crop")
    p.add_argument("--realtime", action="store_true",
                   help="at the game's own speed, not --fast: the pictures between are drawn as on a screen (a "
                        "frame's recording drawn again for each), not only the ones dumped")
    p.add_argument("rest", nargs="*")
    args = p.parse_args()
    args.exe = args.exe or find_exe()
    if not args.disc:
        sys.exit("native_draw_compare: no disc (--disc or KF4_DISC)")
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        sys.exit("native_draw_compare: needs numpy and Pillow")
    os.makedirs(args.out, exist_ok=True)
    if args.mode == "compare":
        runs = [("ps2", ["--native-draw", args.kinds, "--native-draw-compare", "ps2"]),
                ("native", ["--native-draw", args.kinds, "--native-draw-compare", "native"])]
    elif args.mode == "modern":
        # (P8: hosted against standalone, the same kinds: docs/port/NATIVE_DRAW_P8.md)
        runs = [("hosted", ["--native-draw", args.kinds]), ("modern", ["--renderer", "modern", "--native-draw", args.kinds])]
    else:
        runs = [("off", ["--native-draw", "off"]), ("on", ["--native-draw", args.kinds])]
    stats = {}
    for name, extra in runs:
        stats[name] = run(args, os.path.join(args.out, name), extra)
    a_dir, b_dir = (os.path.join(args.out, name) for name, _ in runs)
    lines = ["%s vs %s, %s, scale %d, frame rate %s, %s" % (runs[0][0], runs[1][0], args.kinds, args.scale,
                                                        args.frame_rate, os.path.basename(args.input))]
    for name, s in stats.items():
        lines += ["  %s: %s" % (name, line) for line in s]
    total = total1 = worst = 0
    crop = [int(v) for v in args.crop.split(",")] if args.crop else None
    for f in frame_list(args.frames):
        name = "frame_%06d.png" % f
        pa, pb = os.path.join(a_dir, name), os.path.join(b_dir, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            lines.append("%d: missing" % f)
            continue
        a = np.asarray(Image.open(pa).convert("RGB")).astype(int)
        b = np.asarray(Image.open(pb).convert("RGB")).astype(int)
        if a.shape != b.shape:
            lines.append("%d: sizes differ %s %s" % (f, a.shape, b.shape))
            continue
        d = np.abs(a - b).max(axis=2)
        n0, n1, w = int((d > 0).sum()), int((d > 1).sum()), int(d.max())
        total, total1, worst = total + n0, total1 + n1, max(worst, w)
        box = ""
        if n0:
            ys, xs = np.nonzero(d)
            box = " in x %d-%d, y %d-%d" % (xs.min(), xs.max(), ys.min(), ys.max())
        lines.append("%d: %d pixels differ, %d by more than 1, worst %d%s" % (f, n0, n1, w, box))
        heat = (a * 0.35).astype(np.uint8)
        heat[..., 0] = np.maximum(heat[..., 0], np.clip(d * 8, 0, 255).astype(np.uint8))
        Image.fromarray(heat).save(os.path.join(args.out, "heat_%06d.png" % f))
        if crop:
            s = args.scale
            x0, y0, x1, y1 = (v * s for v in crop)
            parts = [Image.fromarray(np.uint8(x[y0:y1, x0:x1])) for x in (a, b, heat)]
            zoom = 4 if s == 1 else 1
            w_, h_ = (x1 - x0) * zoom, (y1 - y0) * zoom
            sheet = Image.new("RGB", (3 * w_ + 16, h_), (255, 0, 255))
            for i, im in enumerate(parts):
                sheet.paste(im.resize((w_, h_), Image.NEAREST), (i * (w_ + 8), 0))
            sheet.save(os.path.join(args.out, "crop_%06d.png" % f))
    lines.append("all: %d pixels differ, %d by more than 1, worst %d" % (total, total1, worst))
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
