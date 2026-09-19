#!/usr/bin/env python3
"""Parser voor de .ins-bestanden (level-instances) van
Woody Woodpecker: Escape from Buzz Buzzard Park (Eko Software, 2001).

Afgeleid uit de disassembly van Woody.exe:
  0x427290  level::LoadIns (hoofdlezer)      0x428bc0  trajectorie-lezer
  0x42e1a0  instance-ctor                    0x440370  quaternion -> 3x3 matrix
  0x407790  wereldcel-zoeker                 0x437ca0  padlengte trajectorie
  0x43a3a0  pose-evaluatie (hierarchie)      0x43a590 / 0x43a9c0 / 0x43a7b0  pos-/rot-/event-tracks
  0x430210 / 0x4303e0  volume- en collision-tests (VM world_volume / world_collision)
Zie docs/FORMAT_INS.md voor de volledige layout.

Gebruik als module:  parse_ins(data: bytes) -> dict
Gebruik als script:  python tools/insparse.py [bestand.ins ...]
                     (zonder argumenten: valideert alle extract/Data/*/*.ins)

Geen externe afhankelijkheden.
"""
import glob
import os
import struct
import sys

# Lage byte van het node-flagwoord (N+0). Elke waarde correspondeert 1:1 met een
# van de node-lijsten in de modelheader (zie docs/FORMAT_INS.md).
NODE_MESH = 0x00        # gewone mesh-node                 -> lijst S+0x30
NODE_PRESS = 0x01       # interactieve (collision-)node    -> lijst S+0x58
NODE_BBOX = 0x02        # bounding-box node (8 pt, 6 poly) -> S+0x24
NODE_HULL = 0x04        # mesh-node met hull-driehoeklijst -> lijst S+0x38
NODE_VOLUME = 0x08      # triggervolume (convex)           -> lijst S+0x48
NODE_HELPER = 0x10      # renderer-hulpnode (mode + 2 schalen) -> lijst S+0x40
NODE_MARKER = 0x20      # marker/attachment (2 punten, float) -> lijst S+0x50
NODE_LIGHT = 0x40       # lichtbron                        -> per instance inst+0x74
NODE_DUMMY = 0x80       # dummy/groepsnode, wordt overgeslagen in pose-evaluatie
NODE_NONMESH_MASK = 0x70

# Event-record groottes (in dwords) per eventtype, uit 0x43a7b0/0x43a820.
EVENT_SIZES = {3: 15, 4: 9, 5: 6}

POS_FRAME_DWORDS = 4    # {float t, x, y, z}
ROT_FRAME_DWORDS = 5    # {float t, qx, qy, qz, qw}


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

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        if len(v) != n:
            raise EOFError("read past end of file")
        self.pos += n
        return v


def _read_anims(r, nanims):
    """Animatietabel S+8: nanims * {u32 nframes, u32 duration} (duration in 1/4096 s)."""
    out = []
    for _ in range(nanims):
        nframes = r.u32()
        dur = r.u32()
        out.append({"nframes": nframes, "duration_4096": dur, "duration_s": dur / 4096.0})
    return out


def _read_track_refs(r, nanims):
    """Per animatie een verwijzing in de track-pool: nanims * {u32 offset(dwords), u32 count}."""
    return [(r.u32(), r.u32()) for _ in range(nanims)]


def _decode_pos(pool, refs):
    out = []
    for off, cnt in refs:
        frames = []
        for i in range(cnt):
            frames.append(struct.unpack_from("<4f", pool, (off + i * POS_FRAME_DWORDS) * 4))
        out.append(frames)
    return out


def _decode_rot(pool, base, refs, dummy):
    out = []
    for off, cnt in refs:
        frames = []
        for i in range(cnt):
            if dummy:
                # Dummy-nodes (flags 0x80) hebben 3 dwords per frame; de engine
                # evalueert ze nooit (0x43a3f2 slaat ze over). Ruw teruggeven.
                frames.append(struct.unpack_from("<3I", pool, (base + off + i * 3) * 4))
            else:
                frames.append(struct.unpack_from("<5f", pool, (base + off + i * ROT_FRAME_DWORDS) * 4))
        out.append(frames)
    return out


def _decode_events(pool, base, refs):
    out = []
    for off, cnt in refs:
        recs = []
        p = (base + off) * 4
        for _ in range(cnt):
            kind = struct.unpack_from("<I", pool, p)[0]
            size = EVENT_SIZES.get(kind)
            if size is None:
                raise ValueError("unknown event type %d" % kind)
            words = struct.unpack_from("<%dI" % size, pool, p)
            t = struct.unpack_from("<f", pool, p + 4)[0]
            recs.append({"type": kind, "time": t, "raw": words})
            p += size * 4
        out.append(recs)
    return out


def _read_trajectory(r):
    """Sub-lezer 0x428bc0. None als het aantal punten 0 is.

    Engine-object (0x14 bytes): +0 low16 = npunten, bit 16 = gesloten-vlag,
    +0xc = totale lengte (berekend door 0x437ca0), +0x10 = punten (npunten * 16 bytes).
    Per punt: {u32 unk (altijd 0), float x, y, z}; 0x437ca0 gebruikt alleen +4..+0xc.
    """
    npts = r.u32()
    if npts == 0:
        return None
    pts = []
    for _ in range(npts):
        unk, x, y, z = struct.unpack_from("<I3f", r.data, r.pos)
        r.pos += 16
        pts.append({"unk": unk, "pos": (x, y, z)})
    closed = r.u32()
    return {"points": pts, "closed": closed}


def _read_node(r, nanims):
    n = {}
    n["flags"] = flags = r.u32()                    # N+0
    n["kind"] = flags & 0xff
    n["type_code"] = (flags >> 8) & 0xff            # bits 8-15: typecode (0x42f6b0, 0x436dc0)
    n["sub_index"] = flags >> 16                    # bits 16-31: index in inst+0x70 (type 1)
    n["light"] = None
    n["helper"] = None
    n["marker_value"] = None
    n["npolys"] = 0
    if flags & NODE_MARKER:
        raw = r.u32()                               # N+4 (float, 45.0 in de data)
        n["marker_value"] = struct.unpack("<f", struct.pack("<I", raw))[0]
    elif flags & NODE_LIGHT:
        intensity = r.f32()                         # engine: * 3.0 (const 0x4a988c)
        color = r.u32()                             # 0x00RRGGBB
        extra = r.u32()
        n["light"] = {
            "intensity": intensity,
            "color": color,
            "rgb": ((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff),
            "extra": extra,
        }
    elif flags & NODE_HELPER:
        v_c = r.f32()                               # N+0xc  (engine slaat 1/v_c op)
        v_8 = r.f32()                               # N+8    (engine slaat 1/v_8 op)
        mode = r.u32()                              # N+4    (0/1/2, switch in renderer 0x43b7a3)
        n["helper"] = {"scale_c": v_c, "scale_8": v_8, "mode": mode}
    else:
        n["npolys"] = r.u32()                       # N+4
    n["npoints"] = r.u32()                          # N+0x14
    n["pivot"] = r.vec3()                           # N+0x20..0x28
    a = r.u32()
    b = r.u32()
    c = r.u32()
    n["track_sizes"] = (a, b, c)                    # a,b in dwords; c in bytes
    n["track_pool"] = None
    n["pos_refs"] = n["rot_refs"] = n["event_refs"] = None
    n["pos_tracks"] = n["rot_tracks"] = n["event_tracks"] = None
    if a + b + c != 0:
        pool = r.raw((a + b + c // 4) * 4)          # N+0x88
        n["track_pool"] = pool
        if a:
            n["pos_refs"] = _read_track_refs(r, nanims)      # N+0x70
            n["pos_tracks"] = _decode_pos(pool, n["pos_refs"])
        if b:
            n["rot_refs"] = _read_track_refs(r, nanims)      # N+0x74 (engine: offset += a)
            n["rot_tracks"] = _decode_rot(pool, a, n["rot_refs"], (flags & 0xff) == NODE_DUMMY)
        if c:
            n["event_refs"] = _read_track_refs(r, nanims)    # N+0x78 (engine: offset += a+b)
            n["event_tracks"] = _decode_events(pool, a + b, n["event_refs"])
    n["first_child"] = r.i32()                      # N+0x7c (1-based, -1 = geen)
    n["next_sibling"] = r.i32()                     # N+0x80 (1-based, -1 = geen)
    n["parent"] = -1                                # N+0x84, afgeleid (1-based, -1 = root)
    n["polys"] = []
    n["point_base"] = 0                             # N+0x18, afgeleid
    n["hull_list"] = None                           # N+0x8c (ruwe u32-lijst)
    return n


def _read_model(r):
    m = {}
    nnodes_total = r.u32()             # B: aantal nodes incl. root (root = node 0, niet opgeslagen)
    nanims = r.u32()                   # S+4
    m["nnodes_total"] = nnodes_total
    m["nanims"] = nanims
    m["npolys_hdr"] = r.u32()          # S+0xc  (== totaal aantal polygonen)
    m["npoints_hdr"] = r.u32()         # S+0x1c (== aantal punten; wordt later opnieuw gelezen)
    m["anims"] = _read_anims(r, nanims)          # S+8
    m["first_top_node"] = r.u32()      # S+0x6c: eerste top-level node (1-based); broers via next_sibling
    m["bbox_node"] = r.u32()           # S+0x24: bounding-box node (1-based; n+1 als er geen is)
    m["marker_nodes"] = r.u32s(r.u32())   # S+0x50 / S+0x54  (flags 0x20)
    m["light_count_list"] = r.u32s(r.u32())  # S+0x28 / S+0x2c  (aantal lichtnodes)
    m["volume_nodes"] = r.u32s(r.u32())   # S+0x48 / S+0x4c  (flags 0x08)
    m["hull_nodes"] = r.u32s(r.u32())     # S+0x38 / S+0x3c  (flags 0x04)
    m["helper_nodes"] = r.u32s(r.u32())   # S+0x40 / S+0x44  (flags 0x10)
    n58 = r.u32()                      # S+0x58
    m["ncollision_ids"] = r.u32()      # S+0x60: aantal collision-ids per instance (0 of 1)
    m["press_nodes"] = r.u32s(n58)     # S+0x5c  (flags 0x01)
    m["mesh_nodes"] = r.u32s(r.u32())  # S+0x30 / S+0x34  (flags 0x00)

    nodes = [_read_node(r, nanims) for _ in range(nnodes_total - 1)]
    m["nodes"] = nodes

    # Hull-indexlijsten (node+0x8c): een per hull-node (flags 0x04), plus een extra
    # (in de data: die van de bbox-node) als het model mesh-/hull-/press-nodes heeft.
    nextra = len(m["hull_nodes"])
    if m["hull_nodes"] or m["mesh_nodes"] or m["press_nodes"]:
        nextra += 1
    hulls = []
    for _ in range(nextra):
        idx = r.u32()
        cnt = r.u32()
        vals = r.u32s(cnt)
        if not (1 <= idx <= len(nodes)):
            raise ValueError("hull-list node index %d out of range" % idx)
        # Ruwe u32-lijst (puntindices binnen de node); de loader legt geen structuur op.
        nodes[idx - 1]["hull_list"] = vals
        hulls.append((idx, vals))
    m["hull_lists"] = hulls

    npoints = r.u32()                  # S+0x1c / S+0x20 (40-byte punten in de engine)
    m["npoints"] = npoints
    pos = [r.vec3() for _ in range(npoints)]
    nrm = [r.vec3() for _ in range(npoints)]
    col = [r.vec3() for _ in range(npoints)]     # vertexkleur 0..255 (engine verdubbelt evt.)
    m["points"] = [{"pos": pos[i], "normal": nrm[i], "color": col[i]} for i in range(npoints)]

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
                mat = r.u32()          # low16 -> poly+0
                pflags = r.u32()       # low8 | nodeflags -> poly+4
                nverts = r.u32()       # low16 -> poly+2
                idx = r.u32s(nverts)   # low16 elk -> poly+0x18.. (index in puntentabel model)
                polys.append({
                    "material": mat & 0xffff,
                    "flat_color": bool(mat & 0x8000),       # bit 15: RGB565-kleur i.p.v. textuur
                    "material_index": None if mat & 0x8000 else (mat & 0xffff),
                    "rgb565": (mat & 0xffff) if mat & 0x8000 else None,
                    "flags": pflags,
                    "indices": idx,
                })
                sum_idx += nverts
            n["polys"] = polys
            sum_polys += n["npolys"]
        point_base += n["npoints"]
        c = n["first_child"]
        while c != -1:
            if not (1 <= c <= len(nodes)):
                raise ValueError("child index %d out of range" % c)
            nodes[c - 1]["parent"] = j + 1
            c = nodes[c - 1]["next_sibling"]
    if point_base != npoints:
        raise ValueError("sum of node points %d != npoints %d" % (point_base, npoints))
    if sum_polys != m["npolys_total"] or sum_idx != m["nindices_total"]:
        raise ValueError("polygon totals mismatch: %d/%d vs %d/%d" %
                         (sum_polys, sum_idx, m["npolys_total"], m["nindices_total"]))
    if m["npolys_hdr"] != m["npolys_total"] or m["npoints_hdr"] != npoints:
        raise ValueError("header counts mismatch")

    ntris = r.u32()                    # S+0x14 / S+0x18 (32-byte polygonen in de engine)
    tris = []
    for _ in range(ntris):
        i2, i1, i0, mat = r.u32s(4)    # file: idx2, idx1, idx0, materiaal
        tris.append({"indices": (i0, i1, i2), "material": mat & 0xffff,
                     "flat_color": bool(mat & 0x8000)})
    m["tris"] = tris

    ninst = r.u32()                    # S+0
    insts = []
    nvol = len(m["volume_nodes"])
    per_inst_ids = m["ncollision_ids"] + nvol
    for _ in range(ninst):
        inst = {}
        inst["unk0"] = r.u32()         # gelezen maar niet gebruikt door de loader
        inst["trajectory"] = _read_trajectory(r)      # inst+0x78
        inst["position"] = r.vec3()                   # inst+0xc
        inst["quat"] = struct.unpack_from("<4f", r.data, r.pos)  # x,y,z,w -> 3x3 matrix inst+0x28
        r.pos += 16
        inst["scale"] = struct.unpack_from("<3f", r.data, r.pos)  # inst+0x4c..0x54
        r.pos += 12
        inst["id"] = r.u32()           # inst+4 ; level[0x6c][id & 0xffffff] = inst
        inst["index"] = inst["id"] & 0xffffff
        inst["type_tag"] = inst["id"] >> 24
        ids = r.u32s(per_inst_ids) if per_inst_ids else []   # inst+0x70
        inst["volume_ids"] = ids[:nvol]           # 0x03000000 | world_volume-index
        inst["collision_ids"] = ids[nvol:]        # 0x07000000 | world_collision-index
        insts.append(inst)
    m["instances"] = insts
    return m


def parse_ins(data: bytes) -> dict:
    r = Reader(data)
    out = {}
    out["nslots"] = r.u32()            # level+0x68 (aantal script-objecten; +16 reserve)
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
    out["trailer"] = r.u32()           # laatste u32 (altijd 0), gelezen maar niet gebruikt
    out["consumed"] = r.pos
    out["size"] = len(data)
    return out


def summary(name, p):
    models = p["models"]
    nodes = [n for m in models for n in m["nodes"]]
    ninst = sum(len(m["instances"]) for m in models)
    kinds = {}
    for n in nodes:
        kinds[n["kind"]] = kinds.get(n["kind"], 0) + 1
    nanims = sum(m["nanims"] for m in models)
    nframes = sum(len(f) for n in nodes if n["pos_tracks"] for f in n["pos_tracks"]) + \
        sum(len(f) for n in nodes if n["rot_tracks"] for f in n["rot_tracks"])
    nevents = sum(len(e) for n in nodes if n["event_tracks"] for e in n["event_tracks"])
    nvol = sum(len(i["volume_ids"]) for m in models for i in m["instances"])
    ncol = sum(len(i["collision_ids"]) for m in models for i in m["instances"])
    npts = sum(m["npoints"] for m in models)
    npoly = sum(m["npolys_total"] for m in models)
    ntris = sum(len(m["tris"]) for m in models)
    traj_i = sum(1 for m in models for i in m["instances"] if i["trajectory"])
    traj_c = sum(1 for c in p["cameras"] if c["trajectory"])
    kstr = " ".join("%02x:%d" % (k, v) for k, v in sorted(kinds.items()))
    return ("%-8s slots=%4d models=%3d inst=%4d cams=%2d nodes=%4d [%s] anims=%4d "
            "keyframes=%6d events=%5d points=%6d polys=%5d tris=%4d volumes=%3d "
            "collisions=%3d traj(inst/cam)=%3d/%2d" %
            (name, p["nslots"], len(models), ninst, len(p["cameras"]), len(nodes), kstr,
             nanims, nframes, nevents, npts, npoly, ntris, nvol, ncol, traj_i, traj_c))


def main(argv):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extract", "Data")
    files = argv[1:] or sorted(glob.glob(os.path.join(root, "*", "*.ins")))
    ok = True
    for f in files:
        data = open(f, "rb").read()
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            p = parse_ins(data)
        except Exception as e:  # noqa: BLE001
            ok = False
            print("%-8s FAILED: %s" % (name, e))
            continue
        if p["consumed"] != p["size"]:
            ok = False
            print("%-8s FAILED: consumed %d of %d bytes" % (name, p["consumed"], p["size"]))
            continue
        assert p["consumed"] == p["size"]
        print(summary(name, p))
    print("ALL OK (%d files, every byte consumed)" % len(files) if ok else "ERRORS")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
