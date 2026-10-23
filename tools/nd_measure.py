#!/usr/bin/env python3
"""nd_measure.py: reads a KF4_ND_DUMP file (port/render/render.cpp; the adapter's "R" notes from
game/native_draw/native_draw.c) and prints the measurements of NATIVE_DRAW.md's open questions (P0-3):

  sky     the sky records (flags bit 31) of the last whole frame: their radius around the player's feet
          and eye, and the map geometry drawn that frame beyond it;
  layers  triangles of map records with a corner's alpha below 0x80 and the triangles they lie on
          (coplanar: its middle within --tolerance of another's plane, inside it, the same facing line),
          in draw order: the last opaque one drawn before it is what its Z GEQUAL meets;
  parses  meshes parsed per frame (new and re-parsed: a block whose bytes changed), the bytes hashed and
          the time the hash took, and which records' blocks were re-parsed.

    KF4_ND_DUMP=dump.txt kf4 --native-draw map,items ... ; python tools/nd_measure.py dump.txt [--what sky,layers,parses]

Debug only: the dump is written only when KF4_ND_DUMP is set."""
import argparse
import math
from collections import defaultdict, Counter


VERBOSE = False


class Batch:
    __slots__ = ("start", "prim", "alpha_lo", "alpha_hi", "ad", "verts", "alphas", "tris")


def read(path):
    meshes = {}      # id -> (key, [Batch])
    frames = []      # [{"draws": [...], "counts": (parsed, reparsed, hashed, ns)}]
    reparsed_keys = []  # (frame index, key)
    current = None
    record = None
    mesh = None
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            c = t[0]
            if c == "F":
                if current is not None:
                    current["counts"] = tuple(int(v) for v in t[2:7]) + ((0,) if len(t) < 7 else ())
                current = {"draws": [], "counts": None, "serial": int(t[1])}
                frames.append(current)
                record = None
            elif c == "M":
                mesh = (int(t[2], 16), [])
                meshes[int(t[1])] = mesh
            elif c == "B":
                b = Batch()
                b.start, b.prim, b.alpha_lo, b.alpha_hi = int(t[1], 16), int(t[2], 16), int(t[3]), int(t[4])
                mesh[1].append(b)
            elif c == "A":
                v = [int(x, 16) for x in t[1:]]
                mesh[1][-1].ad = list(zip(v[0::2], v[1::2]))
            elif c == "V":
                v = [int(x) for x in t[1:]]
                mesh[1][-1].verts = [tuple(v[i:i + 4]) for i in range(0, len(v), 5)]
                mesh[1][-1].alphas = v[4::5]
            elif c == "T":
                v = [int(x) for x in t[1:]]
                mesh[1][-1].tris = [tuple(v[i:i + 3]) for i in range(0, len(v), 3)]
            elif c == "R":
                vals = [float(x) for x in t[3:]]
                record = {"owner": int(t[1], 16), "flags": int(t[2], 16), "view": vals[:16], "place": vals[16:19]}
            elif c == "D":
                kind = int(t[1])
                m = [float(x) for x in t[5:21]]
                d = {"kind": kind, "owner": int(t[2], 16), "part": int(t[3]), "matrix": m,
                     "meshes": [int(x) for x in t[21:]], "record": record if kind == 1 else None}
                if current is not None:
                    current["draws"].append(d)
            elif c == "P":
                reparsed_keys.append((len(frames) - 1, int(t[1], 16)))
    return meshes, frames, reparsed_keys


def transform(m, v):
    x, y, z, w = v
    return tuple(m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r] * w for r in range(3))


def eye_of(view):
    # view: VU layout (columns); p_view = R p + t; eye = -R^T t
    t = view[12:15]
    return tuple(-(view[4 * c + 0] * t[0] + view[4 * c + 1] * t[1] + view[4 * c + 2] * t[2]) for c in range(3))


def last_whole_frame(frames):
    whole = [f for f in frames if f["counts"] is not None and f["draws"]]
    return whole[-1] if whole else None


def world_triangles(meshes, d):
    for mi, mesh_id in enumerate(d["meshes"]):
        for bi, b in enumerate(meshes[mesh_id][1]):
            w = [transform(d["matrix"], v) for v in b.verts]
            yield mi, bi, b, [(w[i], w[j], w[k]) for i, j, k in b.tris]


def sky(meshes, frames):
    f = last_whole_frame(frames)
    if f is None:
        print("sky: no whole frame")
        return
    skies = defaultdict(list)
    far = []
    for d in f["draws"]:
        r = d["record"]
        if r is None:
            continue
        if r["flags"] & 0x80000000:
            skies[r["owner"]].append(d)
        else:
            for _, _, _, tris in world_triangles(meshes, d):
                far += [p for t in tris for p in t]
    if not skies:
        print("sky: none drawn in the last whole frame (serial %d)" % f["serial"])
    for owner, draws in skies.items():
        r = draws[0]["record"]
        feet = r["place"]
        eye = eye_of(r["view"])
        pts = []
        for d in draws:
            for _, _, _, tris in world_triangles(meshes, d):
                pts += [p for t in tris for p in t]
        if not pts:
            continue
        hz = [math.hypot(p[0] - feet[0], p[2] - feet[2]) for p in pts]
        d3 = [math.dist(p, feet) for p in pts]
        de = [math.dist(p, eye) for p in pts]
        dy = [p[1] - feet[1] for p in pts]
        order = [d["record"]["owner"] for d in f["draws"] if d["record"] is not None]
        order = list(dict.fromkeys(order))
        print("sky record %#x drawn %d of %d map records" % (owner, order.index(owner) + 1, len(order)))
        print("  flags %#x (%d parts drawn, %d corners): feet (%.0f, %.0f, %.0f), eye %.0f above the feet"
              % (r["flags"], len(draws), len(pts), feet[0], feet[1], feet[2], feet[1] - eye[1]))
        print("  around the feet: horizontal %.0f..%.0f, 3D %.0f..%.0f; y %.0f..%.0f (y grows down); from the eye %.0f..%.0f"
              % (min(hz), max(hz), min(d3), max(d3), min(dy), max(dy), min(de), max(de)))
        rim = [h for h, y in zip(hz, dy) if y > -0.25 * (max(dy) - min(dy)) + max(dy)]  # the lowest quarter
        print("  its lowest quarter (the rim): horizontal %.0f..%.0f" % (min(rim), max(rim)))
        if far:
            sky_tris = [t for d in draws for _, _, _, tris in world_triangles(meshes, d) for t in tris]
            fh = [math.hypot(p[0] - feet[0], p[2] - feet[2]) for p in far]
            far_eye = [math.dist(p, eye) for p in far]
            step = max(1, len(far) // 4000)
            sample = far[::step]
            hidden = sum(1 for p in sample if any(ray_hits(eye, p, t) for t in sky_tris))
            print("  map geometry drawn this frame: %d corners, farthest %.0f horizontally from the feet, %.0f from the eye; "
                  "%d of %d sampled corners lie behind the sky (seen from the eye)" % (len(fh), max(fh), max(far_eye), hidden, len(sample)))


def ray_hits(o, p, t):
    """the segment from o to p crosses triangle t before p (Moller-Trumbore, 0 < s < 0.999)"""
    (a, b, c) = t
    e1 = [b[i] - a[i] for i in range(3)]
    e2 = [c[i] - a[i] for i in range(3)]
    dvec = [p[i] - o[i] for i in range(3)]
    h = [dvec[1] * e2[2] - dvec[2] * e2[1], dvec[2] * e2[0] - dvec[0] * e2[2], dvec[0] * e2[1] - dvec[1] * e2[0]]
    det = sum(e1[i] * h[i] for i in range(3))
    if abs(det) < 1e-9:
        return False
    s_ = [o[i] - a[i] for i in range(3)]
    u = sum(s_[i] * h[i] for i in range(3)) / det
    if u < 0 or u > 1:
        return False
    q = [s_[1] * e1[2] - s_[2] * e1[1], s_[2] * e1[0] - s_[0] * e1[2], s_[0] * e1[1] - s_[1] * e1[0]]
    v = sum(dvec[i] * q[i] for i in range(3)) / det
    if v < 0 or u + v > 1:
        return False
    k = sum(e2[i] * q[i] for i in range(3)) / det
    return 0 < k < 0.999


def plane(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
    length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    if length < 1e-9:
        return None
    n = (n[0] / length, n[1] / length, n[2] / length)
    return n, n[0] * ax + n[1] * ay + n[2] * az


def inside(t, p, n):
    # barycentric in the plane's dominant projection, with a small margin
    k = max(range(3), key=lambda i: abs(n[i]))
    i, j = [a for a in range(3) if a != k]
    (a, b, c) = t
    det = (b[i] - a[i]) * (c[j] - a[j]) - (c[i] - a[i]) * (b[j] - a[j])
    if abs(det) < 1e-9:
        return False
    u = ((p[i] - a[i]) * (c[j] - a[j]) - (c[i] - a[i]) * (p[j] - a[j])) / det
    v = ((b[i] - a[i]) * (p[j] - a[j]) - (p[i] - a[i]) * (b[j] - a[j])) / det
    return u >= -1e-3 and v >= -1e-3 and u + v <= 1 + 1e-3


def layers(meshes, frames, tolerance):
    f = last_whole_frame(frames)
    if f is None:
        print("layers: no whole frame")
        return
    CELL = 512.0
    grid = defaultdict(list)
    tris_all = []  # (draw order, record owner, flags, batch key, triangle, plane, alpha_lo, alpha_hi)
    order = 0
    for d in f["draws"]:
        r = d["record"]
        if r is None:
            continue
        for mi, bi, b, tris in world_triangles(meshes, d):
            bkey = (order, mi, bi)
            for t, ijk in zip(tris, b.tris):
                pl = plane(t)
                if pl is None:
                    continue
                a = [min(max(b.alphas[i], 0), 255) for i in ijk]  # (the triangle's own corners' alpha)
                tris_all.append((order, r["owner"], r["flags"], bkey, t, pl, min(a), max(a), b))
        order += 1
    for idx, e in enumerate(tris_all):
        t = e[4]
        x0, x1 = min(p[0] for p in t), max(p[0] for p in t)
        z0, z1 = min(p[2] for p in t), max(p[2] for p in t)
        for gx in range(int(math.floor(x0 / CELL)), int(math.floor(x1 / CELL)) + 1):
            for gz in range(int(math.floor(z0 / CELL)), int(math.floor(z1 / CELL)) + 1):
                grid[(gx, gz)].append(idx)
    per_record = defaultdict(lambda: {"tris": 0, "flags": 0, "order": 0, "alpha": (255, 0), "ad": set(),
                                      "opaque_before": 0, "translucent": 0, "after": 0, "none": 0, "exact": 0,
                                      "bases": Counter()})
    layer_batches = set()
    for idx, e in enumerate(tris_all):
        order_, owner, flags, bkey, t, (n, dist), alo, ahi, b = e
        if alo >= 0x80:
            continue
        layer_batches.add(bkey)
        rec = per_record[owner]
        rec["flags"], rec["order"] = flags, order_
        rec["tris"] += 1
        rec["alpha"] = (min(rec["alpha"][0], alo), max(rec["alpha"][1], ahi))
        rec["ad"].update("%#x=%#x" % (reg, val) for reg, val in b.ad if reg in (0x42, 0x47, 0x4E))
        c = tuple(sum(p[i] for p in t) / 3 for i in range(3))
        cell = (int(math.floor(c[0] / CELL)), int(math.floor(c[2] / CELL)))
        found = []
        for j in grid[cell]:
            o = tris_all[j]
            if o[3] == bkey:
                continue
            on, od = o[5]
            off = abs(on[0] * c[0] + on[1] * c[1] + on[2] * c[2] - od)
            if off > tolerance or abs(on[0] * n[0] + on[1] * n[1] + on[2] * n[2]) < 0.999 or not inside(o[4], c, on):
                continue
            found.append((o, off))
        before = [(o, off) for o, off in found if o[3] < bkey]
        opaque_before = [(o, off) for o, off in before if o[6] >= 0x80]
        if opaque_before:
            o, off = opaque_before[-1]  # the last drawn under it: what GEQUAL meets
            rec["opaque_before"] += 1
            rec["exact"] += off < 0.01
            rec["bases"][o[1]] += 1
        elif before:
            rec["translucent"] += 1
            rec["bases"][before[-1][0][1]] += 1
        elif found:
            rec["after"] += 1
        else:
            rec["none"] += 1
    recs = per_record.values()
    tot = lambda k: sum(r[k] for r in recs)
    print("layers (last whole frame, serial %d): %d map record draws; %d batches with a triangle with a corner's alpha below 0x80 in %d records, "
          "%d such triangles: %d over an opaque batch drawn before (%d of them exactly in its plane), %d only over translucent ones "
          "drawn before, %d with a coplanar batch only after, %d on nothing"
          % (f["serial"], order, len(layer_batches), len(per_record), tot("tris"), tot("opaque_before"), tot("exact"),
             tot("translucent"), tot("after"), tot("none")))
    other = sum(k for r_owner, r in per_record.items() for o, k in r["bases"].items() if o != r_owner)
    print("  of the paired, on another record: %d, on another batch of its own record: %d" %
          (other, tot("opaque_before") + tot("translucent") - other))
    flags = Counter()
    for r in recs:
        flags[r["flags"]] += 1
    print("  their records' flags: " + ", ".join("%#x x%d" % kv for kv in sorted(flags.items())))
    if VERBOSE:
        for owner, r in sorted(per_record.items(), key=lambda kv: kv[1]["order"]):
            print("  record %#x flags %#x draw %d: %d triangles, alpha %d..%d: %d over opaque before, %d translucent before, "
                  "%d after, %d none; on %s; %s" % (owner, r["flags"], r["order"], r["tris"], r["alpha"][0], r["alpha"][1],
                                                   r["opaque_before"], r["translucent"], r["after"], r["none"],
                                                   ", ".join("%#x x%d" % kv for kv in r["bases"].most_common(3)),
                                                   " ".join(sorted(r["ad"]))))


def parses(meshes, frames, reparsed_keys, last):
    whole = [f for f in frames if f["counts"] is not None]
    if not whole:
        print("parses: no frames")
        return
    tail = whole[-last:]
    parsed = [f["counts"][0] for f in tail]
    rep = [f["counts"][1] for f in tail]
    hashed = [f["counts"][2] for f in tail]
    ns = [f["counts"][3] for f in tail]
    pns = [f["counts"][4] for f in tail]
    blocks = [sum(len(d["meshes"]) for d in f["draws"]) for f in tail]
    print("parses over the last %d frames (of %d): parsed %.2f a frame (max %d), re-parsed %.2f (max %d); "
          "hashed %.0f KB a frame (max %.0f) in %.0f us (max %.0f; %.2f ns a byte); %.0f blocks asked for a frame"
          % (len(tail), len(whole), sum(parsed) / len(tail), max(parsed), sum(rep) / len(tail), max(rep),
             sum(hashed) / len(tail) / 1024, max(hashed) / 1024, sum(ns) / len(tail) / 1000, max(ns) / 1000,
             sum(ns) / max(1, sum(hashed)), sum(blocks) / len(tail)))
    if sum(pns):
        print("  parsing took %.0f us a frame (max %.0f), %.1f us a block" % (sum(pns) / len(tail) / 1000, max(pns) / 1000,
                                                                        sum(pns) / max(1, sum(parsed)) / 1000))
    wave = {d["owner"] for f in tail for d in f["draws"] if d["record"] and d["record"]["flags"] & 0x100}
    water = {d["owner"] for f in tail for d in f["draws"] if d["record"] and d["record"]["flags"] & 0x202}
    print("  records drawn in those frames: %d with wave UVs (flags 0x100), %d water (0x2, 0x200)" % (len(wave), len(water)))
    # which records' blocks were re-parsed: key -> mesh ids -> draws
    key_owner = {}
    for f in frames:
        for d in f["draws"]:
            for m in d["meshes"]:
                key = meshes[m][0]
                key_owner[key] = (d["kind"], d["owner"], d["record"]["flags"] if d["record"] else None)
    first = frames.index(tail[0])
    by = Counter()
    for fi, key in reparsed_keys:
        if fi >= first:
            by[key_owner.get(key, (None, None, None))] += 1
    for (kind, owner, flags), k in by.most_common(12):
        print("  re-parsed %d times: kind %s owner %s flags %s" % (k, kind, "%#x" % owner if owner is not None else "?",
                                                                    "%#x" % flags if flags is not None else "-"))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("dump")
    p.add_argument("--what", default="sky,layers,parses")
    p.add_argument("--tolerance", type=float, default=0.5, help="coplanar: a middle this near another's plane (world units)")
    p.add_argument("--last", type=int, default=100, help="the parse counts over the last this many frames (after the teleport's load)")
    p.add_argument("--verbose", action="store_true", help="the layers record by record")
    args = p.parse_args()
    global VERBOSE
    VERBOSE = args.verbose
    meshes, frames, reparsed_keys = read(args.dump)
    what = args.what.split(",")
    if "sky" in what:
        sky(meshes, frames)
    if "layers" in what:
        layers(meshes, frames, args.tolerance)
    if "parses" in what:
        parses(meshes, frames, reparsed_keys, args.last)


if __name__ == "__main__":
    main()
