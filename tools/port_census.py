#!/usr/bin/env python3
"""Count what still stops the game code compiling for the PC, per folder and per file.

    tools/port_census.py [BUILD_DIR] [--files] [--kinds] [--show PATH...]

Compiles every game unit in BUILD_DIR (a configured CMake build of the PC port, default
out/windows-clang or out/linux-gcc, whichever exists) with no error limit and summarises the errors:
how many each folder of src/game has, and (with --files) each file, or (with --kinds) of which kind.
With --show, prints the compiler's messages for the given sources (paths under src/game, or a folder
to show every source in it) instead. Step 0 is done when every folder is at zero and the program links.
"""
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASM = re.compile(r"\b(?:__asm__|asm)\s*(?:volatile\s*|__volatile__\s*)?\(")
ERROR = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^:\n]+?\.[ch]):(?P<line>\d+):\d+: (?:fatal )?error: (?P<text>.*)$", re.M)


def kind_of(text):
    text = re.sub(r"'[^']*'", "X", text)
    text = re.sub(r" \(aka X\)", "", text)
    return text


def main():
    show = []
    if "--show" in sys.argv:
        at = sys.argv.index("--show")
        show = [s.replace("\\", "/").strip("/") for s in sys.argv[at + 1:]]
        sys.argv = sys.argv[:at]
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    build = args[0] if args else next(
        (d for d in ("out/windows-clang", "out/linux-gcc", "out/linux-clang") if os.path.isdir(os.path.join(ROOT, d))), None)
    if build is None:
        sys.exit("configure a build first: cmake --preset windows-clang (or linux-gcc)")
    build = os.path.join(ROOT, build)
    # the game code this build compiles: the patched copy that CMake makes in the build dir
    game = os.path.join(build, "decomp_src", "src", "game")
    with open(os.path.join(build, "compile_commands.json")) as f:
        commands = [c for c in json.load(f) if "/src/game/" in c["file"].replace("\\", "/")]

    if show:
        for c in commands:
            rel = os.path.relpath(c["file"], game).replace("\\", "/")
            if any(rel == s or rel.startswith(s + "/") or ("src/game/" + rel) == s for s in show):
                command = re.sub(r" -o \S+", "", c["command"]) + " -fsyntax-only"
                r = subprocess.run(command, cwd=c["directory"], shell=True, capture_output=True, text=True)
                sys.stdout.write(r.stderr)
        return

    errors = collections.defaultdict(list)
    seen = set()  # an error in a header counts once, not once per unit that includes it
    for c in commands:
        command = c["command"] + " -fsyntax-only -ferror-limit=0" if "clang" in c["command"] else \
            c["command"] + " -fsyntax-only -fmax-errors=0"
        command = re.sub(r" -o \S+", "", command)
        r = subprocess.run(command, cwd=c["directory"], shell=True, capture_output=True, text=True)
        rel = os.path.relpath(c["file"], game).replace("\\", "/")
        for m in ERROR.finditer(r.stderr):
            path = os.path.normpath(os.path.join(c["directory"], m.group("file")))
            key = (path, m.group("line"), m.group("text"))
            if key in seen:
                continue
            seen.add(key)
            where = os.path.relpath(path, game).replace("\\", "/")
            errors[where if not where.startswith("..") else rel].append(kind_of(m.group("text")))

    per_folder = collections.Counter()
    for rel, errs in errors.items():
        per_folder[rel.split("/")[0] if "/" in rel else "(top)"] += len(errs)
    # inline asm is only rejected when the code is generated, so count what is left of it
    asm_left = collections.Counter()
    for base, _, names in os.walk(game):
        for name in names:
            rel = os.path.relpath(os.path.join(base, name), game).replace("\\", "/")
            if name.endswith((".c", ".h")) and not name.startswith("func_0022"):
                with open(os.path.join(base, name), encoding="utf-8", errors="replace") as f:
                    n = len(ASM.findall(f.read()))
                if n:
                    asm_left[rel.split("/")[0] if "/" in rel else "(top)"] += n
    total = sum(per_folder.values())
    print(f"{total} error(s) in {len(errors)} files; {sum(asm_left.values())} inline asm statement(s) left")
    print(f"  {'folder':12} {'errors':>6} {'asm':>5}")
    for folder in sorted(set(per_folder) | set(asm_left), key=lambda f: -(per_folder[f] + asm_left[f])):
        print(f"  {folder:12} {per_folder[folder]:6} {asm_left[folder]:5}")
    if "--files" in sys.argv:
        for rel, errs in sorted(errors.items(), key=lambda x: -len(x[1])):
            print(f"{len(errs):5}  {rel}")
    if "--kinds" in sys.argv:
        kinds = collections.Counter(k for errs in errors.values() for k in errs)
        for k, n in kinds.most_common():
            print(f"{n:5}  {k}")


if __name__ == "__main__":
    main()
