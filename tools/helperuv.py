#!/usr/bin/env python3
"""Helper-node (kind 0x10) UV-projectie narekenen (docs/MODEL_RENDER.md §4): renderer 0x43b74d-0x43b908.
usage: python tools/helperuv.py LEVEL MODEL MESHNODE(1-based) [ANIM]
q = (p - pivot - T_helper) geroteerd naar helper-ruimte; mode 0: (y,z) 1: (x,z) 2: (x,y);
u = 0.5 - a/v_8 ; v = b/v_c - 0.5   (N+8 = 1/v_8, N+0xc = 1/v_c)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from insparse import parse_ins

def qrot(q, v):                       # roteer v met quaternion q=(x,y,z,w)
    x, y, z, w = q; vx, vy, vz = v
    tx, ty, tz = 2*(y*vz - z*vy), 2*(z*vx - x*vz), 2*(x*vy - y*vx)
    return (vx + w*tx + y*tz - z*ty, vy + w*ty + z*tx - x*tz, vz + w*tz + x*ty - y*tx)

def main():
    lv, mi, ni = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); anim = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extract", "Data", lv)
    m = parse_ins(open(os.path.join(root, lv + ".ins"), "rb").read())["models"][mi]
    N = m["nodes"]; mesh = N[ni - 1]
    hs = [j for j, n in enumerate(N) if n["kind"] == 0x10 and n["parent"] == ni]
    if not hs: print("geen helper-kind"); return
    h = N[hs[0]]; hp = h["helper"]; print("helper node", hs[0] + 1, hp)
    for fr in range(len(h["pos_tracks"][anim])):
        T = h["pos_tracks"][anim][fr][1:4]; t = h["pos_tracks"][anim][fr][0]
        rt = h["rot_tracks"][anim]; Q = rt[min(fr, len(rt) - 1)][1:5]
        for sign, name in ((1, "q"), (-1, "conj")):
            qq = (Q[0]*sign, Q[1]*sign, Q[2]*sign, Q[3]); us, vs = [], []
            for k in range(mesh["point_base"], mesh["point_base"] + mesh["npoints"]):
                p = m["points"][k]["pos"]; pv = mesh["pivot"]
                d = (p[0]-pv[0]-T[0], p[1]-pv[1]-T[1], p[2]-pv[2]-T[2]); q = qrot(qq, d)
                a, b = {0: (q[1], q[2]), 1: (q[0], q[2]), 2: (q[0], q[1])}[hp["mode"]]
                us.append(0.5 - a / hp["scale_8"]); vs.append(b / hp["scale_c"] - 0.5)
            print("  t=%6.1f %-4s u[%6.2f %6.2f] v[%6.2f %6.2f]  depth-as %s" % (t, name, min(us), max(us), min(vs), max(vs), ""))

if __name__ == "__main__":
    main()
