#!/usr/bin/env python3
"""What a thread of the game spends its time in, from kf4-sampler's stacks (tools/sampler/sampler.c; profile.py
runs the game with it, docs/port/RENDERER.md "Profiling").

    python tools/sampler/stacks.py SAMPLES EXE [--thread FUNC|TID] [--threads] [--top N]
                                   [--callers FUNC | --callees FUNC] [--depth D] [--exports]

EXE is the program that was sampled: its DWARF (a RelWithDebInfo build) names the frames, through llvm-symbolizer
(llvm-mingw's bin on PATH), inlined functions as frames of their own. A thread's busy samples are those not waiting
in ntdll or win32u. For each function, of the thread's busy samples: inclusive, the share whose stack has it anywhere
(so the shares overlap: a caller's covers its callees'), and self, the share it was the innermost frame of.

--thread FUNC|TID  the thread: a thread id, or a function, the thread whose busy stacks have it most (ring_thread:
                   the native GS's thread; backend_thread: its back end's); by default the busiest
--threads          every thread first: its CPU time while sampled, its busy share of the samples, and what it runs
                   (its stacks' commonest outermost function of the program's own)
--callers FUNC     the stacks FUNC is called from, --depth (3) frames up, by share of the thread's busy samples
--callees FUNC     what FUNC's time goes into, --depth (1) frames down ("(self)": FUNC's own)
--exports          a DLL's frames by the export at or before them ([ucrtbase.dll!memcmp]), not the DLL alone: right
                   for an exported function, a guess inside a DLL's own functions (a driver's)
"""
import argparse
import bisect
import collections
import struct
import subprocess
import sys

IDLE = ("ntdll.dll", "win32u.dll")


def load(path):
    """kf4-sampler's file: the modules' paths by name, each thread's CPU ms, and each thread's stacks as
    (count, frames innermost first)"""
    mods, cpu, stacks = {}, {}, collections.defaultdict(list)
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "M":
                mods[p[1].lower()] = line.rstrip("\n").split(" ", 2)[2]  # (a path may have spaces)
            elif p[0] == "T":
                cpu[int(p[1])] = float(p[2])
            elif p[0] == "K":
                stacks[int(p[1])].append((int(p[2]), p[3:]))
    return mods, cpu, stacks


def symbolize(exe, addrs):
    """Each address's function names, innermost (inlined) first"""
    addrs = sorted(addrs)
    try:
        out = subprocess.run(["llvm-symbolizer", "--obj=" + exe, "--functions=short"],
                             input="".join("0x%x\n" % a for a in addrs), capture_output=True, text=True).stdout
    except FileNotFoundError:
        sys.exit("stacks: no llvm-symbolizer: put llvm-mingw's bin on PATH")
    return {a: block.split("\n")[0::2] for a, block in zip(addrs, out.strip("\n").split("\n\n"))}


def exports(path):
    """A DLL's exported functions as sorted (rva, name), from its file's export table (no PDB); [] if unread"""
    try:
        data = open(path, "rb").read()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec, optsize = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        export_rva, export_size = struct.unpack_from("<II", data, pe + 24 + 112)
        secs = [struct.unpack_from("<IIII", data, pe + 24 + optsize + i * 40 + 8) for i in range(nsec)]

        def off(rva):
            for vsize, va, rsize, raw in secs:
                if va <= rva < va + max(vsize, rsize):
                    return raw + rva - va
            raise ValueError(rva)

        e = off(export_rva)
        nfuncs, nnames, afuncs, anames, aords = struct.unpack_from("<IIIII", data, e + 20)
        syms = []
        for i in range(nnames):
            name_at = off(struct.unpack_from("<I", data, off(anames) + 4 * i)[0])
            rva = struct.unpack_from("<I", data, off(afuncs) + 4 * struct.unpack_from("<H", data, off(aords) + 2 * i)[0])[0]
            if not export_rva <= rva < export_rva + export_size:  # (not a forwarder)
                syms.append((rva, data[name_at:data.index(b"\0", name_at)].decode()))
        return sorted(syms)
    except (OSError, ValueError, struct.error):
        return []


def report(path, exe, thread=None, threads=False, top=40, callers_of=None, callees_of=None, depth=None,
           by_export=False):
    mods, cpu, stacks = load(path)
    addrs = set()
    for tid in stacks:
        for n, f in stacks[tid]:
            for i, a in enumerate(f):
                if a.startswith("0x"):
                    addrs.add(int(a, 16) - (1 if i else 0))  # (a return address: the call before it)
    sym = symbolize(exe, addrs)
    tables = {}

    def dll_frame(a):
        mod, _, rva = a.rpartition("+")
        if not by_export or mod.lower() not in mods:
            return "[%s]" % mod
        if mod.lower() not in tables:
            tables[mod.lower()] = exports(mods[mod.lower()])
        table = tables[mod.lower()]
        k = bisect.bisect_right(table, (int(rva, 16), "￿")) - 1
        return "[%s!%s]" % (mod, table[k][1]) if k >= 0 else "[%s]" % mod

    def names(f):
        """A stack as function names, innermost first, inlined frames expanded"""
        out = []
        for i, a in enumerate(f):
            if a.startswith("0x"):
                out.extend(sym.get(int(a, 16) - (1 if i else 0), ["?"]))
            else:
                out.append(dll_frame(a))
        return out

    # each thread's busy stacks, named
    busy = {tid: [(n, names(f)) for n, f in stacks[tid] if f[0].rpartition("+")[0].lower() not in IDLE] for tid in stacks}
    if threads:
        print("-- threads: CPU ms while sampled, busy share of the samples, what it runs")
        for tid in sorted(stacks, key=lambda t: -cpu.get(t, 0)):
            samples = sum(n for n, f in stacks[tid])
            outer = collections.Counter()
            for n, fr in busy[tid]:
                # (the outermost of the program's own, past libc++'s std::thread wrappers and a fiber's start; else the DLL)
                own = [x for x in fr if not x.startswith(("[", "__", "??"))]
                outer[own[-1] if own else fr[0]] += n
            print("%7d %9.0f ms %5.1f%%  %s" % (tid, cpu.get(tid, 0), 100.0 * sum(outer.values()) / samples,
                                                outer.most_common(1)[0][0] if outer else "-"))
    if thread is None:
        tid = max(busy, key=lambda t: sum(n for n, fr in busy[t]))
    elif str(thread).isdigit():
        tid = int(thread)
    else:
        tid = max(busy, key=lambda t: sum(n for n, fr in busy[t] if thread in fr))
        if not any(thread in fr for n, fr in busy[tid]):
            sys.exit("stacks: no thread's stacks have %s" % thread)
    total = 0
    incl = collections.Counter()
    selfc = collections.Counter()
    callers = collections.Counter()
    callees = collections.Counter()
    for n, fr in busy.get(tid, []):
        total += n
        selfc[fr[0]] += n
        for name in set(fr):
            incl[name] += n
        if callers_of and callers_of in fr:
            k = fr.index(callers_of)
            callers[" < ".join(fr[k + 1:k + 1 + (depth or 3)])] += n
        if callees_of and callees_of in fr:
            k = fr.index(callees_of)
            callees[" > ".join(reversed(fr[max(0, k - (depth or 1)):k]))] += n
    if total == 0:
        sys.exit("stacks: thread %d has no busy samples" % tid)
    print("thread %d: %.0f ms of CPU; %d busy samples" % (tid, cpu.get(tid, 0), total))
    if callers_of:
        print("-- callers of %s (%.1f%% inclusive)" % (callers_of, 100.0 * incl[callers_of] / total))
        for k, n in callers.most_common(top):
            print("%5.1f%%  %s" % (100.0 * n / total, k or "(top)"))
    elif callees_of:
        print("-- callees of %s (%.1f%% inclusive)" % (callees_of, 100.0 * incl[callees_of] / total))
        for k, n in callees.most_common(top):
            print("%5.1f%%  %s" % (100.0 * n / total, k or "(self)"))
    else:
        print("-- inclusive            self")
        for name, n in incl.most_common(top):
            print("%5.1f%%  %5.1f%%  %s" % (100.0 * n / total, 100.0 * selfc[name] / total, name))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("samples")
    parser.add_argument("exe")
    parser.add_argument("--thread")
    parser.add_argument("--threads", action="store_true")
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--callers")
    parser.add_argument("--callees")
    parser.add_argument("--depth", type=int)
    parser.add_argument("--exports", action="store_true")
    args = parser.parse_args()
    report(args.samples, args.exe, args.thread, args.threads, args.top, args.callers, args.callees, args.depth,
           args.exports)


if __name__ == "__main__":
    main()
