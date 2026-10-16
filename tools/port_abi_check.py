#!/usr/bin/env python3
"""Find functions that are declared with one signature in some files and defined with another.

    tools/port_abi_check.py [BUILD_DIR] [--target TRIPLE]

The decomp often declares a function locally, with the types the matching needed; the EE passes
integer and float arguments in separate registers, so a wrong local prototype often still worked
there. On the PC it does not: Win64 hands arguments out by position, and SysV passes small structs
in registers. This compiles every game unit to LLVM IR (which spells out each call's ABI: i32, float,
double, ptr, byval structs …) and reports every function whose declarations disagree with its
definition, as the compiler for TRIPLE lowers them (default: the build's own target; also try
--target x86_64-linux-gnu for Linux's ABI).
"""
import collections
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIG = re.compile(r"^(declare|define)\s+(?:[a-z_]+\s+)*(\S+(?: \S+)*?)\s+@\"?([\w.$]+)\"?\((.*?)\)(?:\s|$)", re.M)
ATTRS = re.compile(r"\b(noundef|nonnull|signext|zeroext|inreg|nocapture|readonly|writeonly|noalias|"
                   r"dereferenceable(?:_or_null)?\(\d+\)|align \d+|returned|nofree|immarg|%\w+|#\d+)\b")


def norm(params):
    out = []
    for p in params.split(","):
        p = re.sub(r"\s*%[\w.]+$", "", ATTRS.sub("", p).strip())
        p = re.sub(r"\s+", " ", p)
        out.append(p)
    return tuple(x for x in out if x)


def main():
    args = sys.argv[1:]
    target = None
    if "--target" in args:
        i = args.index("--target")
        target = args[i + 1]
        del args[i:i + 2]
    build = os.path.join(ROOT, args[0] if args else next(
        d for d in ("out/windows-clang", "out/linux-clang") if os.path.isdir(os.path.join(ROOT, d))))
    commands = [c for c in json.load(open(os.path.join(build, "compile_commands.json")))
                if "/src/" in c["file"].replace("\\", "/")]
    seen = collections.defaultdict(lambda: collections.defaultdict(set))
    tmp = tempfile.mkdtemp()
    for n, c in enumerate(commands):
        out = os.path.join(tmp, f"{n}.ll")
        cmd = re.sub(r" -o \S+", "", c["command"]).replace(" -c ", " ")
        cmd += f" -S -emit-llvm -O0 -w -o \"{out}\""
        if target:
            cmd += f" --target={target}"
        r = subprocess.run(cmd, cwd=c["directory"], shell=True, capture_output=True, text=True)
        if r.returncode:
            print(f"(skipped {c['file']}: {r.stderr.strip().splitlines()[-1] if r.stderr.strip() else 'failed'})")
            continue
        rel = os.path.relpath(c["file"], ROOT).replace("\\", "/")
        for m in SIG.finditer(open(out, encoding="utf-8", errors="replace").read()):
            kind, ret, name, params = m.groups()
            ret = ATTRS.sub("", ret).strip().split()[-1]
            seen[name][(ret, norm(params), kind)].add(rel)
    bad = 0
    for name, sigs in sorted(seen.items()):
        # an unprototyped declaration (`f()`, IR `(...)` or `()`) takes the definition's shape at the
        # call; only prototyped ones can disagree (see -Wdeprecated-non-prototype for the calls)
        shapes = {(r, p) for r, p, kind in sigs if kind == "define" or p not in ((), ("...",))}
        if len(shapes) > 1:
            bad += 1
            defs = [s for s in sigs if s[2] == "define"]
            print(f"{name}:")
            for (r, p, kind), files in sorted(sigs.items(), key=lambda x: x[0][2] != "define"):
                print(f"    {kind:7} {r} ({', '.join(p)})  {' '.join(sorted(files)[:4])}{' …' if len(files) > 4 else ''}")
    print(f"{bad} function(s) with disagreeing signatures")


if __name__ == "__main__":
    main()
