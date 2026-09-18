#!/usr/bin/env python3
"""Parser voor de .ins-bestanden (level-instances) van
Woody Woodpecker: Escape from Buzz Buzzard Park (Eko Software, 2001).

Afgeleid uit de disassembly van Woody.exe: loader 0x427290 (level::LoadIns),
trajectorie-lezer 0x428bc0, instance-ctor 0x42e1a0, quaternion->matrix 0x440370,
wereldcel-zoeker 0x407790 en de padlengte-berekening 0x437ca0.
Zie docs/FORMAT_INS.md voor de layout.

Gebruik als module:  parse_ins(data: bytes) -> dict
Gebruik als script:  python tools/insparse.py  (valideert alle extract/Data/*/*.ins)

Geen externe afhankelijkheden.
"""
import glob
import os
import struct
import sys

# Bitmasker in het node-flagwoord (N+0) dat het type van de node bepaalt.
NODE_MESH_ALT = 0x20   # gewone mesh-node (zelfde pad als flags==0)
NODE_LIGHT = 0x40      # lichtbron: kleur + intensiteit + extra dword
NODE_SPECIAL = 0x10    # node met twee reciproke waarden (geen mesh)
NODE_NONMESH_MASK = 0x70


class Reader:
    __slots__ = ("data", "pos")

    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u32(self):
        (v,) = struct.unpack_from("<I", self.data, self.pos)
        self.pos += 4
        return v

    def i32(self):
        (v,) = struct.unpack_from("<i", self.data, self.pos)
        self.pos += 4
        return v

    def f32(self):
        (v,) = struct.unpack_from("<f", self.data, self.pos)
        self.pos += 4
        return v

    def vec3(self):
        v = struct.unpack_from("<3f", self.data, self.pos)
        self.pos += 12
        return v

    def u32s(self, n):
        v = list(struct.unpack_from("<%dI" % n, self.data, self.pos))
        self.pos += 4 * n
        return v

    def i32s(self, n):
        v = list(struct.unpack_from("<%di" % n, self.data, self.pos))
        self.pos += 4 * n
        return v

    def f32s(self, n):
        v = list(struct.unpack_from("<%df" % n, self.data, self.pos))
        self.pos += 4 * n
        return v

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        if len(v) != n:
            raise EOFError("read past end of file")
        self.pos += n
        return v

    def remaining(self):
        return len(self.data) - self.pos


def _read_keys(r, nkeys):
    """Keyframe-tijdtabel S+8: nkeys * {float time, u32 aux}."""
    keys = []
    for _ in range(nkeys):
        t = r.f32()
        aux = r.u32()
        keys.append((t, aux))
    return keys


def _read_track_refs(r, nkeys):
    """Per-key trackverwijzing (N+0x70/0x74/0x78): nkeys * {u32 offset, u32 count}."""
    out = []
    for _ in range(nkeys):
        off = r.u32()
        cnt = r.u32()
        out.append((off, cnt))
    return out


def _read_trajectory(r):
    """Sub-lezer 0x428bc0. Geeft None terug als het aantal punten 0 is.

    Engine-object (0x14 bytes): +0 low16 = npunten, bit16 = gesloten-vlag,
    +0xc = totale lengte (berekend), +0x10 = punten (npunten * 16 bytes).
    """
    npts = r.u32()
    if npts == 0:
        return None
    pts = []
    for _ in range(npts):
        x, y, z, w = struct.unpack_from("<4f", r.data, r.pos)
        r.pos += 16
        pts.append((x, y, z, w))
    closed = r.u32()
    return {"points": pts, "closed": closed}


def _read_node(r, nkeys):
    n = {}
    n["flags"] = flags = r.u32()
    n["light"] = None
    n["special"] = None
    n["npolys"] = 0
    if flags & NODE_MESH_ALT:
        n["npolys"] = r.u32()                         # N+4
    elif flags & NODE_LIGHT:
        intensity = r.f32()                           # * const 0x4a988c
        color = r.u32()                               # 0x00RRGGBB
        extra = r.u32()
        n["light"] = {
            "intensity_raw": intensity,
            "color": color,
            "rgb": ((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff),
            "extra": extra,
        }
    elif flags & NODE_SPECIAL:
        v_c = r.f32()                                 # N+0xc  (engine: 1/v_c)
        v_8 = r.f32()                                 # N+8    (engine: 1/v_8)
        v_4 = r.u32()                                 # N+4
        n["special"] = {"val_c": v_c, "val_8": v_8, "val_4": v_4}
    else:
        n["npolys"] = r.u32()                         # N+4
    n["npoints"] = r.u32()                            # N+0x14
    n["pivot"] = r.vec3()                             # N+0x20..0x28
    a = r.u32()
    b = r.u32()
    c = r.u32()
    n["track_sizes"] = (a, b, c)
    n["track_pool"] = None
    n["track_pos"] = n["track_rot"] = n["track_c"] = None
    if a + b + c != 0:
        n["track_pool"] = r.raw((a + b + c // 4) * 4)   # N+0x88
        if a:
            n["track_pos"] = _read_track_refs(r, nkeys)  # N+0x70
        if b:
            n["track_rot"] = _read_track_refs(r, nkeys)  # N+0x74 (offset += a)
        if c:
            n["track_c"] = _read_track_refs(r, nkeys)    # N+0x78 (offset += a+b)
    n["first_child"] = r.i32()                        # N+0x7c (1-based, -1 = geen)
    n["next_sibling"] = r.i32()                       # N+0x80 (1-based, -1 = geen)
    n["parent"] = -1                                  # N+0x84, afgeleid
    n["polys"] = []
    n["point_base"] = 0                               # N+0x18, afgeleid
    n["extra_list"] = None                            # N+0x8c
    return n


def _read_model(r):
    m = {}
    nnodes_total = r.u32()             # B, incl. root (node 0)
    nkeys = r.u32()                    # S+4
    m["nnodes_total"] = nnodes_total
    m["nkeys"] = nkeys
    m["s_0c"] = r.u32()                # S+0xc
    m["s_1c_first"] = r.u32()          # S+0x1c (wordt later overschreven door npoints)
    m["keys"] = _read_keys(r, nkeys)   # S+8
    m["s_6c"] = r.u32()                # S+0x6c
    m["s_24"] = r.u32()                # S+0x24
    m["list_50"] = r.u32s(r.u32())     # S+0x50 / S+0x54
    m["list_28"] = r.u32s(r.u32())     # S+0x28 / S+0x2c  (aantal lichten)
    m["list_48"] = r.u32s(r.u32())     # S+0x48 / S+0x4c
    m["list_38"] = r.u32s(r.u32())     # S+0x38 / S+0x3c
    m["list_40"] = r.u32s(r.u32())     # S+0x40 / S+0x44
    n58 = r.u32()                      # S+0x58
    m["s_60"] = r.u32()                # S+0x60
    m["list_58"] = r.u32s(n58)         # S+0x5c
    m["list_30"] = r.u32s(r.u32())     # S+0x30 / S+0x34

    nodes = [_read_node(r, nkeys) for _ in range(nnodes_total - 1)]
    m["nodes"] = nodes

    # Extra per-node lijsten (node+0x8c); nodeindex 1-based.
    nextra = len(m["list_38"])
    if m["list_38"] or m["list_30"] or m["list_58"]:
        nextra += 1
    extra = []
    for _ in range(nextra):
        idx = r.u32()
        cnt = r.u32()
        vals = r.u32s(cnt)
        extra.append((idx, vals))
        if not (1 <= idx <= len(nodes)):
            raise ValueError("extra-list node index %d out of range" % idx)
        nodes[idx - 1]["extra_list"] = vals
    m["extra_lists"] = extra

    npoints = r.u32()                  # S+0x1c (definitief) / S+0x20
    m["npoints"] = npoints
    pos = [r.vec3() for _ in range(npoints)]
    nrm = [r.vec3() for _ in range(npoints)]
    aux = [r.vec3() for _ in range(npoints)]
    m["points"] = [{"pos": pos[i], "normal": nrm[i], "aux": aux[i]} for i in range(npoints)]

    m["npolys_total"] = r.u32()
    m["nindices_total"] = r.u32()
    point_base = 0
    sum_polys = 0
    sum_idx = 0
    for j, n in enumerate(nodes):
        n["point_base"] = point_base
        if not (n["flags"] & NODE_NONMESH_MASK):
            polys = []
            for _ in range(n["npolys"]):
                mat = r.u32()          # low16 -> poly+0 (materiaal/texture-index)
                pflags = r.u32()       # low8 | node flags -> poly+4
                nverts = r.u32()       # low16 -> poly+2
                idx = r.u32s(nverts)   # low16 elk -> poly+0x18.. (index in puntentabel model)
                polys.append({"material": mat, "flags": pflags, "indices": idx})
                sum_idx += nverts
            n["polys"] = polys
            sum_polys += n["npolys"]
        point_base += n["npoints"]
        # parent-fixup zoals de engine: eerste kind + broers/zussen krijgen parent=j
        c = n["first_child"]
        while c != -1:
            if not (1 <= c <= len(nodes)):
                raise ValueError("child index %d out of range" % c)
            nodes[c - 1]["parent"] = j
            c = nodes[c - 1]["next_sibling"]
    if point_base != npoints:
        raise ValueError("sum of node points %d != npoints %d" % (point_base, npoints))
    if sum_polys != m["npolys_total"] or sum_idx != m["nindices_total"]:
        raise ValueError("polygon totals mismatch: %d/%d vs %d/%d" %
                         (sum_polys, sum_idx, m["npolys_total"], m["nindices_total"]))

    ntris = r.u32()                    # S+0x14 / S+0x18 (32-byte entries)
    tris = []
    for _ in range(ntris):
        i2, i1, i0, mat = r.u32s(4)    # file: idx->poly+0x1c, +0x1a, +0x18, materiaal->poly+0
        tris.append({"indices": (i0, i1, i2), "material": mat})
    m["tris"] = tris

    ninst = r.u32()                    # S+0
    insts = []
    per_inst_extra = m["s_60"] + len(m["list_48"])
    for _ in range(ninst):
        inst = {}
        inst["unk0"] = r.u32()         # gelezen in [esp+0x84], niet gebruikt
        inst["trajectory"] = _read_trajectory(r)      # inst+0x78
        inst["position"] = r.vec3()                   # inst+0xc
        inst["quat"] = struct.unpack_from("<4f", r.data, r.pos)  # x,y,z,w -> matrix inst+0x28
        r.pos += 16
        inst["i_4c"] = r.u32()         # inst+0x4c
        inst["i_50"] = r.u32()         # inst+0x50
        inst["i_54"] = r.u32()         # inst+0x54
        inst["id"] = r.u32()           # inst+4 ; level[0x6c][id & 0xffffff] = inst
        inst["index"] = inst["id"] & 0xffffff
        inst["type_tag"] = inst["id"] >> 24
        inst["extra"] = r.u32s(per_inst_extra) if per_inst_extra else []   # inst+0x70
        insts.append(inst)
    m["instances"] = insts
    return m


def parse_ins(data: bytes) -> dict:
    r = Reader(data)
    out = {}
    out["ninstances"] = r.u32()        # level+0x68 (slots in level+0x6c, +16 reserve)
    nmodels = r.u32()
    out["models"] = [_read_model(r) for _ in range(nmodels)]
    ncams = r.u32()
    cams = []
    for _ in range(ncams):
        cam = {}
        cam["position"] = r.vec3()     # cam+0xc..0x14
        cam["id"] = r.u32()            # cam+4 ; level[0x6c][id & 0xffffff] = cam
        cam["index"] = cam["id"] & 0xffffff
        cam["type_tag"] = cam["id"] >> 24
        cam["trajectory"] = _read_trajectory(r)   # cam+0x28 (vtable 0x4aa224 als aanwezig)
        cams.append(cam)
    out["cameras"] = cams
    out["trailer"] = r.u32()           # laatste u32, door de loader gelezen maar niet gebruikt
    out["consumed"] = r.pos
    out["size"] = len(data)
    return out


def summary(name, p):
    models = p["models"]
    ninst = sum(len(m["instances"]) for m in models)
    nnodes = sum(len(m["nodes"]) for m in models)
    nlights = sum(1 for m in models for n in m["nodes"] if n["light"])
    nspecial = sum(1 for m in models for n in m["nodes"] if n["special"])
    anim = sum(1 for m in models if m["nkeys"] > 0)
    tracks = sum(1 for m in models for n in m["nodes"] if n["track_pool"])
    npts = sum(m["npoints"] for m in models)
    npoly = sum(m["npolys_total"] for m in models)
    ntris = sum(len(m["tris"]) for m in models)
    traj_i = sum(1 for m in models for i in m["instances"] if i["trajectory"])
    traj_c = sum(1 for c in p["cameras"] if c["trajectory"])
    tags = sorted({i["type_tag"] for m in models for i in m["instances"]} |
                  {c["type_tag"] for c in p["cameras"]})
    return ("%-9s slots=%4d models=%3d inst=%4d nodes=%4d lights=%3d special=%2d "
            "anim_models=%3d tracks=%4d points=%6d polys=%5d tris=%4d "
            "cams=%2d traj(inst/cam)=%3d/%2d tags=%s trailer=%d" %
            (name, p["ninstances"], len(models), ninst, nnodes, nlights, nspecial,
             anim, tracks, npts, npoly, ntris, len(p["cameras"]), traj_i, traj_c,
             ",".join("0x%02x" % t for t in tags), p["trailer"]))


def main(argv):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extract", "Data")
    files = sorted(glob.glob(os.path.join(root, "*", "*.ins")))
    if argv[1:]:
        files = argv[1:]
    ok = True
    for f in files:
        data = open(f, "rb").read()
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            p = parse_ins(data)
        except Exception as e:  # noqa: BLE001
            ok = False
            print("%-9s FAILED: %s" % (name, e))
            continue
        if p["consumed"] != p["size"]:
            ok = False
            print("%-9s FAILED: consumed %d of %d bytes" % (name, p["consumed"], p["size"]))
            continue
        print(summary(name, p))
    print("ALL OK" if ok else "ERRORS")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
