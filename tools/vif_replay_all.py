#!/usr/bin/env python3
"""Replays the VIF1 stream of many GS captures through VU1 (kf4-gsreplay --mode vif), one line each.

For each capture: the packets made again against the recorded ones, VU1's runs, instructions and
cycles, and with --vu1 hle or check the HLE's runs or the runs compared (port/platform/vu1_hle.c).
Exits with 1 if any packet differs or any run compared differs.

    python3 tools/vif_replay_all.py --vu1 check C:/code/kf4-builds/maptour-09925c1 more/*.kf4gs
"""
import argparse
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def captures(paths):
    for p in map(pathlib.Path, paths):
        if p.is_dir():
            yield from sorted(p.glob("**/*.kf4gs"))
        else:
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="+", help="captures, or folders searched for *.kf4gs")
    ap.add_argument("--vu1", default="interp", choices=("interp", "hle", "check"))
    ap.add_argument("--exe", default=str(ROOT / "out" / "windows-clang" / "kf4-gsreplay"),
                    help="kf4-gsreplay (default: out/windows-clang's)")
    args = ap.parse_args()
    bad = 0
    total = {"same": 0, "differ": 0, "compared": 0, "compared_differ": 0}
    for cap in captures(args.paths):
        out = subprocess.run([args.exe, str(cap), "--mode", "vif", "--vu1", args.vu1, "--diff", "0"],
                             capture_output=True, text=True).stdout
        packets = re.search(r"VIF1 -> VU1: (\d+) packets as recorded, (\d+) differ, (\d+) recorded but not "
                            r"made, (\d+) made but not recorded", out)
        vu1 = re.search(r"VU1: (\d+) runs, (\d+) instructions, (\d+) cycles", out)
        extra = re.search(r"VU1 (?:HLE|check): [^\n]*", out)
        check = re.search(r"VU1 check: (\d+) runs compared, (\d+) differ", out)
        if not packets or not vu1:
            print("%s: no result" % cap)
            bad = 1
            continue
        same, differ, missing, made = map(int, packets.groups())
        total["same"] += same
        total["differ"] += differ
        if check:
            total["compared"] += int(check.group(1))
            total["compared_differ"] += int(check.group(2))
        if differ or missing or made or (check and int(check.group(2))):
            bad = 1
        print("%s: %d packets as recorded, %d differ, %d not made, %d not recorded; VU1 %s runs, %s pairs, "
              "%s cycles%s" % (cap.parent.name if cap.name == "capture.kf4gs" else cap.name, same, differ,
                               missing, made, *vu1.groups(), "; " + extra.group(0) if extra else ""))
    print("total: %(same)d packets as recorded, %(differ)d differ" % total +
          ("; %(compared)d runs compared, %(compared_differ)d differ" % total if args.vu1 == "check" else ""))
    return bad


if __name__ == "__main__":
    sys.exit(main())
