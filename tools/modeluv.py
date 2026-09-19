#!/usr/bin/env python3
"""Vergelijk UV-formules voor modelpolygonen / skinned driehoeken (zie docs/MODEL_RENDER.md).

usage: python tools/modeluv.py LEVEL [-m MODEL | -s SLOT] [-n NODE ...] [-v]
  LEVEL  bijv. W1A (leest extract/Data/LEVEL/LEVEL.ins + .tex)
  -m     modelindex (default 0 = Woody);  -s  instance-slot (id & 0xffffff) -> zoekt het model
  -n     alleen deze nodes (1-based) tonen;  -v  elke polygoon/driehoek afdrukken

Formules:
  A  = planaire projectie op het ruwe bestandspunt (modelruimte)             [oude port]
  B  = planaire projectie op punt - node-pivot (engine: 0x427daf + 0x43da37)  [polygonen]
  C  = expliciete UV's uit de materiaalentry, bestandsvertex j -> (f[3j], f[3j+1]) (0x43e39a) [driehoeken]
"""
import os, sys, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from insparse import parse_ins
from levelparse import parse_tex

def planar(mat, p):
    a, b = mat["u"], mat["v"]
    return (a[0]*p[0] + a[1]*p[1] + a[2]*p[2] + a[3], b[0]*p[0] + b[1]*p[1] + b[2]*p[2] + b[3])

def span(uvs):
    us = [u for u, v in uvs]; vs = [v for u, v in uvs]
    return max(max(us) - min(us), max(vs) - min(vs))

def rng(uvs):
    us = [u for u, v in uvs]; vs = [v for u, v in uvs]
    return "u[%8.2f %8.2f] v[%8.2f %8.2f]" % (min(us), max(us), min(vs), max(vs))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level"); ap.add_argument("-m", type=int, default=None); ap.add_argument("-s", type=int, default=None)
    ap.add_argument("-n", type=int, nargs="*", default=None); ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extract", "Data", a.level)
    ins = parse_ins(open(os.path.join(root, a.level + ".ins"), "rb").read())
    tex = parse_tex(open(os.path.join(root, a.level + ".tex"), "rb").read())
    mats, groups = tex["materials"], tex["groups"]
    mi = a.m if a.m is not None else 0
    if a.s is not None:
        mi = next(i for i, m in enumerate(ins["models"]) if any(x["index"] == a.s for x in m["instances"]))
    m = ins["models"][mi]
    nodes, pts = m["nodes"], m["points"]
    owner = [None] * len(pts)
    for j, n in enumerate(nodes):
        for k in range(n["point_base"], n["point_base"] + n["npoints"]): owner[k] = j
    print("model %d: nodes=%d points=%d polys=%d tris=%d instances=%s" % (mi, len(nodes), len(pts), m["npolys_total"],
          len(m["tris"]), [x["index"] for x in m["instances"]][:12]))
    def rel(i):
        p, pv = pts[i]["pos"], nodes[owner[i]]["pivot"]
        return (p[0]-pv[0], p[1]-pv[1], p[2]-pv[2])
    for j, n in enumerate(nodes):
        if a.n and (j + 1) not in a.n: continue
        if not n["polys"] and not a.n: continue
        tm = {}
        for p in n["polys"]: tm[(p["material"], p["flags"])] = tm.get((p["material"], p["flags"]), 0) + 1
        print("node %3d kind=%02x tc=%d pivot=(%.1f %.1f %.1f) npts=%d npolys=%d  (mat,flags)xN: %s" % (j + 1, n["kind"], n["type_code"],
              n["pivot"][0], n["pivot"][1], n["pivot"][2], n["npoints"], len(n["polys"]),
              " ".join("%04x,%x x%d" % (k[0], k[1], c) for k, c in sorted(tm.items()))))
        alla, allb, sa, sb = [], [], 0.0, 0.0
        for p in n["polys"]:
            if p["flat_color"]: continue
            mt = mats[p["material"]]
            ua = [planar(mt, pts[i]["pos"]) for i in p["indices"]]; ub = [planar(mt, rel(i)) for i in p["indices"]]
            alla += ua; allb += ub; sa = max(sa, span(ua)); sb = max(sb, span(ub))
            if a.v: print("    poly mat=%04x g=%d fl=%x  A %s | B %s" % (p["material"], mt["group"], p["flags"], rng(ua), rng(ub)))
        if alla:
            print("      A(model)  %s maxspan/poly=%.2f\n      B(-pivot) %s maxspan/poly=%.2f" % (rng(alla), sa, rng(allb), sb))
    if m["tris"] and not a.n:
        alla, allc, sa, sc, used = [], [], 0.0, 0.0, {}
        for t in m["tris"]:
            if t["flat_color"]: continue
            mt = mats[t["material"]]; f = mt["matrix"]; used[mt["group"]] = used.get(mt["group"], 0) + 1
            i0, i1, i2 = t["indices"]                      # engine-volgorde; bestand = (i2, i1, i0)
            ua = [planar(mt, pts[i]["pos"]) for i in (i2, i1, i0)]
            uc = [(f[0], f[1]), (f[3], f[4]), (f[6], f[7])]   # bestandsvertex 0,1,2
            alla += ua; allc += uc; sa = max(sa, span(ua)); sc = max(sc, span(uc))
            if a.v: print("    tri mat=%04x g=%d  A %s | C %s  row3=(%.3f %.3f %.3f) col2=(%.3f %.3f %.3f)" % (t["material"], mt["group"],
                          rng(ua), rng(uc), f[9], f[10], f[11], f[2], f[5], f[8]))
        print("tris: %d textured, groups %s" % (len(alla) // 3, used))
        if alla:
            print("      A(planar model) %s maxspan/tri=%.2f\n      C(explicit)     %s maxspan/tri=%.2f" % (rng(alla), sa, rng(allc), sc))
            # continuiteit: zelfde punt + zelfde groep -> zelfde UV?
            seen, same, diff = {}, 0, 0
            for t in m["tris"]:
                if t["flat_color"]: continue
                mt = mats[t["material"]]; f = mt["matrix"]; i0, i1, i2 = t["indices"]
                for i, uv in ((i2, (f[0], f[1])), (i1, (f[3], f[4])), (i0, (f[6], f[7]))):
                    k = (i, mt["group"])
                    if k in seen:
                        if abs(seen[k][0]-uv[0]) < 0.015 and abs(seen[k][1]-uv[1]) < 0.015: same += 1
                        else: diff += 1
                    else: seen[k] = uv
            print("      C shared-vertex check (same point+group): equal=%d different=%d" % (same, diff))

if __name__ == "__main__":
    main()
