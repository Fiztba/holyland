#!/usr/bin/env python3
"""Import build/kfiv.elf into a Ghidra project for the Ghidra MCP server.

build/kfiv.elf is byte-identical to the retail code and carries the full
symbol table (every func_/D_ name with its size), so Ghidra starts from the
same names and function boundaries as splat. Needs Ghidra 12.x with the
ghidra-emotionengine-reloaded extension (language r5900:LE:32:default).

Run it with the pyghidra-mcp tool's Python (see tools/ghidra_mcp.sh, which
does that for you). Re-running replaces the program, discarding any renames
or retypes made in Ghidra.
"""
import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGUAGE = "r5900:LE:32:default"
PROGRAM = "kfiv.elf"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--elf", type=Path, default=ROOT / "build" / "kfiv.elf")
    ap.add_argument("--project", type=Path, required=True,
                    help="path to the project's .gpr file (created if missing)")
    args = ap.parse_args()

    if not args.elf.is_file():
        sys.exit(f"{args.elf} not found; run ./build.sh first")

    import pyghidra
    pyghidra.start(install_dir=Path(os.environ["GHIDRA_INSTALL_DIR"]))

    from ghidra.program.util import GhidraProgramUtilities

    gpr = args.project.resolve()
    gpr.parent.mkdir(parents=True, exist_ok=True)
    project = pyghidra.open_project(gpr.parent, gpr.stem, create=True)
    try:
        old = project.getProjectData().getFile("/" + PROGRAM)
        if old is not None:
            print(f"replacing existing /{PROGRAM}")
            old.delete()

        results = (pyghidra.program_loader()
                   .source(str(args.elf))
                   .project(project)
                   .name(PROGRAM)
                   .language(LANGUAGE)
                   .compiler("default")
                   .load())
        try:
            results.save(pyghidra.task_monitor())
        finally:
            results.close()

        with pyghidra.program_context(project, "/" + PROGRAM) as program:
            print("analyzing (a few minutes)...")
            pyghidra.analyze(program)
            GhidraProgramUtilities.markProgramAnalyzed(program)
            program.save("import + analysis", pyghidra.task_monitor())
            fm = program.getFunctionManager()
            print(f"{fm.getFunctionCount()} functions in {gpr}")
    finally:
        project.close()


if __name__ == "__main__":
    main()
