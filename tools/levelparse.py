"""levelparse.py - parsers voor de levelbestanden .tex / .col / .vis / .lit van
"Woody Woodpecker: Escape from Buzz Buzzard Park" (Eko Software, 2001).

Alle layouts zijn afgeleid uit de disassembly van Woody.exe (zie
docs/FORMAT_TEX_COL_VIS_LIT.md).  Dit bestand heeft geen dependencies buiten de
standaardbibliotheek; alleen `--dump-tex` gebruikt Pillow.

API
    parse_tex(data) -> dict
    parse_col(data) -> dict
    parse_vis(data) -> dict
    parse_lit(data) -> dict
    parse_gel_counts(data) -> dict   (hulpfunctie: telt objecten/cellen in .gel
                                      voor kruisvalidatie van .col/.vis)

Elke parser geeft in het resultaat 'consumed' (aantal verwerkte bytes) terug; de
__main__ controleert dat dit gelijk is aan de bestandsgrootte.

Gebruik:
    python tools/levelparse.py                      # valideer alle levels
    python tools/levelparse.py --dump-tex W1A out/  # schrijf textures als PNG
"""
import os
import struct
import sys

LIT_MAGIC = 0x20010822          # 2001-08-22, versiedatum van de light-builder


class _Reader:
    """Sequentiele little-endian lezer, spiegelt de fread-aanroepen van de engine."""

    __slots__ = ("data", "pos")

    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u32(self):
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.data, self.pos)[0]
        self.pos += 4
        return v

    def u32s(self, n):
        v = struct.unpack_from("<%dI" % n, self.data, self.pos)
        self.pos += 4 * n
        return list(v)

    def i32s(self, n):
        v = struct.unpack_from("<%di" % n, self.data, self.pos)
        self.pos += 4 * n
        return list(v)

    def f32s(self, n):
        v = struct.unpack_from("<%df" % n, self.data, self.pos)
        self.pos += 4 * n
        return list(v)

    def skip(self, n):
        if self.pos + n > len(self.data):
            raise ValueError("read past end of data (pos=%d, need=%d, size=%d)"
                             % (self.pos, n, len(self.data)))
        self.pos += n

    def remaining(self):
        return len(self.data) - self.pos


# ---------------------------------------------------------------------------
# .tex  (loader: 0x426c90..0x427066 in de level-loader 0x426bd0)
# ---------------------------------------------------------------------------

def parse_tex(data):
    """Parseer een .tex-bestand.

    Retourneert::

        {
          'group_count':   aantal texture-groepen (1e u32),
          'texture_count': totaal aantal frames/surfaces (2e u32),
          'groups': [ {
              'index', 'width', 'height',
              'flags'          : u32  (bit0 = colour-key alpha (magenta), bits1-2 -> face flags,
                                        byte1 == 2 -> speciale rendermodus, byte2 = 0xff meestal),
              'scroll_u', 'scroll_v' : float (3e/4e dword; 0.0 = statisch),
              'anim_duration'  : float (animatieduur, alleen relevant als frame_count > 1),
              'frame_count'    : int,
              'dword8', 'dword9' : u32 (geen gebruiker gevonden; vermoedelijk editor-restdata),
              'frames'         : [byte-offset van de RGB565-pixels van elk frame],
              'frame_size'     : width*height*2,
          } ],
          'materials': [ {
              'group'  : index in groups (u32),
              'matrix' : 12 floats zoals in het bestand (4 rijen van 3: 3x3 + translatie),
              'u'      : [m[0], m[3], m[6], m[9]]   (kolom 0 -> u = ax + by + cz + d),
              'v'      : [m[1], m[4], m[7], m[10]]  (kolom 1 -> v),
          } ],
          'consumed': aantal verwerkte bytes,
        }
    """
    r = _Reader(data)
    group_count = r.u32()
    texture_count = r.u32()
    groups = []
    total_frames = 0
    for g in range(group_count):
        w, h, flags = r.u32(), r.u32(), r.u32()
        scroll_u, scroll_v = r.f32(), r.f32()
        anim_duration_raw = r.u32()
        frame_count = r.u32()
        dword8, dword9 = r.u32(), r.u32()
        anim_duration = struct.unpack("<f", struct.pack("<I", anim_duration_raw))[0]
        frame_size = w * h * 2
        frames = []
        for _ in range(frame_count):
            frames.append(r.pos)
            r.skip(frame_size)
        total_frames += frame_count
        groups.append({
            "index": g, "width": w, "height": h, "flags": flags,
            "scroll_u": scroll_u, "scroll_v": scroll_v,
            "anim_duration": anim_duration, "anim_duration_raw": anim_duration_raw,
            "frame_count": frame_count, "dword8": dword8, "dword9": dword9,
            "frames": frames, "frame_size": frame_size,
        })
    if total_frames != texture_count:
        raise ValueError("frame total %d != texture_count %d" % (total_frames, texture_count))
    material_count = r.u32()
    materials = []
    for _ in range(material_count):
        grp = r.u32()
        m = r.f32s(12)
        materials.append({
            "group": grp, "matrix": m,
            "u": [m[0], m[3], m[6], m[9]],
            "v": [m[1], m[4], m[7], m[10]],
        })
    return {
        "group_count": group_count, "texture_count": texture_count,
        "groups": groups, "materials": materials, "consumed": r.pos,
    }


def material_uv(material, x, y, z):
    """UV van een vertex (objectruimte) volgens de .ins-loader (0x4280e4..0x428110):
    u = f0*x + f3*y + f6*z + f9, v = f1*x + f4*y + f7*z + f10 (in texture-herhalingen)."""
    a, b = material["u"], material["v"]
    return (a[0] * x + a[1] * y + a[2] * z + a[3],
            b[0] * x + b[1] * y + b[2] * z + b[3])


def decode_rgb565(data, offset, width, height, colour_key=False):
    """Decodeer een RGB565-frame naar een lijst (r,g,b,a) tuples.

    De engine (0x47f090 case 0) expandeert 5/6/5 bits door links te schuiven
    (geen replicatie) en test daarna, als de alpha-vlag aan staat,
    (argb & 0xF0F0F0) == 0xF000F0 (magenta) -> volledig transparant.
    Hier wordt wel netjes naar 0..255 geschaald.
    """
    n = width * height
    vals = struct.unpack_from("<%dH" % n, data, offset)
    out = []
    for v in vals:
        r5, g6, b5 = v >> 11, (v >> 5) & 63, v & 31
        a = 255
        if colour_key:
            # engine-test op de met <<3/<<2/<<3 geexpandeerde waarden
            if (r5 << 3) & 0xF0 == 0xF0 and (g6 << 2) & 0xF0 == 0 and (b5 << 3) & 0xF0 == 0xF0:
                a = 0
        out.append((r5 * 255 // 31, g6 * 255 // 63, b5 * 255 // 31, a))
    return out


# ---------------------------------------------------------------------------
# .col  (loader 0x4271e0)
# ---------------------------------------------------------------------------

def parse_col(data):
    """Parseer een .col-bestand.

    Het bestand bevat voor elk 'sector'-object van de wereld (world+0x18 objecten
    uit .gel, de bladeren van de ruimtelijke boom) een u32 aantal gevolgd door
    zoveel u32's.  Elke u32 = (mask << 16) | object_index; object_index indexeert
    de objecttabel world+0x40 (de .ins-objecten), mask (meestal 0xFFFF) wordt als
    hoge 16 bits meegegeven aan de collision-callback (vtable+0x20 van het object).

    Retourneert {'sectors': [[{'object','mask','raw'}, ...], ...], 'consumed'}.
    """
    r = _Reader(data)
    sectors = []
    while r.remaining() > 0:
        n = r.u32()
        vals = r.u32s(n)
        sectors.append([{"object": v & 0xFFFF, "mask": v >> 16, "raw": v} for v in vals])
    return {"sectors": sectors, "consumed": r.pos}


# ---------------------------------------------------------------------------
# .vis  (loader 0x408260, this = wereld/level-object, resultaat in world+0x28)
# ---------------------------------------------------------------------------

def parse_vis(data):
    """Parseer een .vis-bestand.

    Per cel (world+0x20 cellen uit .gel): u32 entry_count, u32 total_pairs, dan
    entry_count entries {u32 id, u32 pair_count, pair_count x (u32 cell, u32 flag)}.
    total_pairs wordt door de engine alleen voor de allocatie gebruikt
    ((entry_count + total_pairs) * 8 + 4 bytes).

    Retourneert {'cells': [{'entry_count','total_pairs','entries':[{'id','pairs':[(cell,flag)]}]}],
                 'consumed'}.
    """
    r = _Reader(data)
    cells = []
    while r.remaining() > 0:
        a, b = r.u32(), r.u32()
        entries = []
        s = 0
        for _ in range(a):
            idv, cnt = r.u32(), r.u32()
            flat = r.u32s(2 * cnt)
            pairs = list(zip(flat[0::2], flat[1::2]))
            s += cnt
            entries.append({"id": idv, "pairs": pairs})
        if s != b:
            raise ValueError("vis cell %d: pair sum %d != total_pairs %d" % (len(cells), s, b))
        cells.append({"entry_count": a, "total_pairs": b, "entries": entries})
    return {"cells": cells, "consumed": r.pos}


# ---------------------------------------------------------------------------
# .lit  (loader 0x40ac30, lightsystem-object 0x2c bytes, globaal 0x4c4cac)
# ---------------------------------------------------------------------------

def _parse_polygon(r):
    """Polygoon zoals in .gel en .lit: u32 n, 4 floats vlak (nx,ny,nz,d), u32 face, n int32 indices."""
    n = r.u32()
    plane = r.f32s(4)
    face = r.u32()
    indices = r.i32s(n)
    return {"plane": plane, "face": face, "indices": indices}


def parse_lit(data, cell_count=None):
    """Parseer een .lit-bestand.

    Als cell_count (aantal cellen, world+0x20) bekend is wordt de optionele
    trailer daarmee in per-cel lijsten gesplitst; anders wordt het aantal
    lijsten afgeleid door de u32-reeks te doorlopen.

    Retourneert::

        {
          'magic': 0x20010822,
          'lights': [ {
              'object_id'   : u32  (light+0x04; laag 24 bits = index in objecttabel world+0x40,
                                    hoogste byte = type (altijd 1 in de data)),
              'object_index', 'type',
              'field_28'    : u32  (light+0x28; 2 of 3),
              'position'    : (x,y,z)  (light+0x0c..0x14),
              'color'       : (r,g,b) floats 0..255  (light+0x30..0x38),
              'radius'      : float (light+0x2c),
              'faces'       : [u32]  lijst A (S+0x08/S+0x0c): gesorteerde face-indices (.gel faces),
              'faces_b'     : [u32]  lijst B (S+0x10/S+0x14): gesorteerde face-indices,
              'polygons'    : [ {'plane','face','indices'} ]  lijst C (S+0x18/S+0x1c),
                              negatieve indices = extra (geclipte) vertices, positieve = .gel vertex,
              'ranges'      : [ {'cell','n_faces','n_faces_b','n_polygons','off_faces','off_faces_b','off_polygons'} ]
                              (S+0x00/S+0x04, 0x1c-byte records; offsets zijn cumulatief, door de engine berekend),
              'bsp_nodes'   : [ (plane_index, front, back) ]  (S+0x20; kind: (v & 0xF) == 0 -> node v>>4,
                              == 1 -> blad met face v>>4, anders leeg),
              'bsp_planes'  : [ (nx,ny,nz,d) ]  (S+0x24),
          } ],
          'probes'      : [ {'position':(x,y,z), 'color': u32} ]  (lightsys+0x08/+0x0c, 0x30-byte records),
          'cell_lights' : [[light_index,...] per cel] of None als de trailer ontbreekt,
          'consumed'
        }
    """
    r = _Reader(data)
    magic = r.u32()
    if magic != LIT_MAGIC:
        raise ValueError("bad .lit magic 0x%08x (expected 0x%08x)" % (magic, LIT_MAGIC))
    n_lights = r.u32()
    lights = []
    for _ in range(n_lights):
        object_id = r.u32()
        field_28 = r.u32()
        position = tuple(r.f32s(3))
        color = tuple(r.f32s(3))
        radius = r.f32()
        # sub-struct S (light+0x3c, 0x28 bytes)
        na = r.u32()
        faces = r.u32s(na)
        nb = r.u32()
        faces_b = r.u32s(nb)
        nc = r.u32()
        total_idx = r.u32()
        polygons = [_parse_polygon(r) for _ in range(nc)]
        if sum(len(p["indices"]) for p in polygons) != total_idx:
            raise ValueError("lit: polygon index total mismatch")
        nd = r.u32()
        ranges = []
        off_a = off_b = off_c = 0
        for _ in range(nd):
            cell, n1, n2, n3 = r.u32(), r.u32(), r.u32(), r.u32()
            ranges.append({"cell": cell, "n_faces": n1, "n_faces_b": n2, "n_polygons": n3,
                           "off_faces": off_a, "off_faces_b": off_b, "off_polygons": off_c})
            off_a += n1
            off_b += n2
            off_c += n3
        ne = r.u32()
        flat = r.u32s(3 * ne)
        bsp_nodes = list(zip(flat[0::3], flat[1::3], flat[2::3]))
        nf = r.u32()
        flat = r.f32s(4 * nf)
        bsp_planes = list(zip(flat[0::4], flat[1::4], flat[2::4], flat[3::4]))
        lights.append({
            "object_id": object_id, "object_index": object_id & 0xFFFFFF, "type": object_id >> 24,
            "field_28": field_28, "position": position, "color": color, "radius": radius,
            "faces": faces, "faces_b": faces_b, "polygons": polygons, "ranges": ranges,
            "bsp_nodes": bsp_nodes, "bsp_planes": bsp_planes,
        })
    n_probes = r.u32()
    probes = []
    for _ in range(n_probes):
        pos = tuple(r.f32s(3))
        col = r.u32()
        probes.append({"position": pos, "color": col})
    cell_lights = None
    if r.remaining() >= 4:
        total = r.u32()
        flat = r.u32s(total)
        cell_lights = []
        i = 0
        while i < len(flat) and (cell_count is None or len(cell_lights) < cell_count):
            n = flat[i]
            cell_lights.append(flat[i + 1:i + 1 + n])
            i += 1 + n
        if i != len(flat):
            raise ValueError("lit trailer: cell lists do not cover the dword array")
    return {"magic": magic, "lights": lights, "probes": probes,
            "cell_lights": cell_lights, "consumed": r.pos}


# ---------------------------------------------------------------------------
# .gel  (alleen tellingen; loader 0x407ae0) - voor kruisvalidatie
# ---------------------------------------------------------------------------

def parse_gel_counts(data):
    """Loop het .gel-bestand door en geef de tellingen terug die .col/.vis nodig hebben.

    Layout (allemaal u32/float32):
      faces        : u32 n, u32 total_indices, n x polygoon {u32 cnt, u32 attr, 4f vlak, cnt u32}
      polys2       : u32 n, u32 total_indices, n x {u32 cnt, 4f vlak, cnt u32}   (world+0x2c/0x30)
      groups       : u32 n, n u32, dan n x {u32 cnt, cnt x (u32,u32)}             (world+0x34/0x38)
      vertices     : u32 n, n x {3f pos, u32 kleur}                                (world+0x04/0x08)
      objects      : u32 n, n x {u32 cnt, cnt u32 faces, 6 dw, 6 dw, u32 m, m x 16 B}  (world+0x18/0x1c)
      planes       : u32 n, n x 16 B                                               (world+0x14)
      cells        : u32 n, zelfde record als objects                              (world+0x20/0x24)
    """
    r = _Reader(data)
    n_faces = r.u32()
    r.u32()
    for _ in range(n_faces):
        cnt = r.u32()
        r.skip(4 + 16 + 4 * cnt)
    n2 = r.u32()
    r.u32()
    for _ in range(n2):
        cnt = r.u32()
        r.skip(16 + 4 * cnt)
    n3 = r.u32()
    r.skip(4 * n3)
    for _ in range(n3):
        cnt = r.u32()
        r.skip(8 * cnt)
    n_verts = r.u32()
    r.skip(16 * n_verts)

    def zone_list():
        n = r.u32()
        for _ in range(n):
            cnt = r.u32()
            r.skip(4 * cnt + 48)
            m = r.u32()
            r.skip(16 * m)
        return n

    n_objects = zone_list()
    n_planes = r.u32()
    r.skip(16 * n_planes)
    n_cells = zone_list()
    return {"faces": n_faces, "polys2": n2, "groups": n3, "vertices": n_verts,
            "objects": n_objects, "planes": n_planes, "cells": n_cells, "consumed": r.pos}


# ---------------------------------------------------------------------------
# __main__
# ---------------------------------------------------------------------------

def _level_dirs(root):
    for name in sorted(os.listdir(root)):
        base = os.path.join(root, name, name)
        if os.path.exists(base + ".tex"):
            yield name, base


def _validate_all(root):
    ok = True
    for name, base in _level_dirs(root):
        parts = []
        try:
            tex_data = open(base + ".tex", "rb").read()
            col_data = open(base + ".col", "rb").read()
            vis_data = open(base + ".vis", "rb").read()
            lit_data = open(base + ".lit", "rb").read()
            gel = parse_gel_counts(open(base + ".gel", "rb").read()) if os.path.exists(base + ".gel") else None

            tex = parse_tex(tex_data)
            assert tex["consumed"] == len(tex_data), ".tex consumed %d of %d" % (tex["consumed"], len(tex_data))
            col = parse_col(col_data)
            assert col["consumed"] == len(col_data), ".col consumed %d of %d" % (col["consumed"], len(col_data))
            vis = parse_vis(vis_data)
            assert vis["consumed"] == len(vis_data), ".vis consumed %d of %d" % (vis["consumed"], len(vis_data))
            lit = parse_lit(lit_data, cell_count=len(vis["cells"]))
            assert lit["consumed"] == len(lit_data), ".lit consumed %d of %d" % (lit["consumed"], len(lit_data))

            anim = sum(1 for g in tex["groups"] if g["frame_count"] > 1)
            parts.append("tex: %d groups (%d animated) %d frames %d materials" %
                         (tex["group_count"], anim, tex["texture_count"], len(tex["materials"])))
            parts.append("col: %d sectors %d refs" % (len(col["sectors"]), sum(len(s) for s in col["sectors"])))
            parts.append("vis: %d cells" % len(vis["cells"]))
            parts.append("lit: %d lights %d probes%s" % (
                len(lit["lights"]), len(lit["probes"]),
                "" if lit["cell_lights"] is None else " %d cell-lists" % len(lit["cell_lights"])))
            if gel is not None:
                assert gel["objects"] == len(col["sectors"]), "col sectors %d != gel objects %d" % (len(col["sectors"]), gel["objects"])
                assert gel["cells"] == len(vis["cells"]), "vis cells %d != gel cells %d" % (len(vis["cells"]), gel["cells"])
                if lit["cell_lights"] is not None:
                    assert len(lit["cell_lights"]) == gel["cells"], "lit cell-lists != gel cells"
                for l in lit["lights"]:
                    assert all(f < gel["faces"] for f in l["faces"]), "lit face index out of range"
                    assert all(p["face"] < gel["faces"] for p in l["polygons"]), "lit polygon face out of range"
                    assert all(rg["cell"] < gel["cells"] for rg in l["ranges"]), "lit range cell out of range"
                for m in tex["materials"]:
                    assert m["group"] < tex["group_count"], "material group out of range"
                parts.append("[gel cross-check ok: %d objects, %d cells, %d faces]" %
                             (gel["objects"], gel["cells"], gel["faces"]))
            print("%-9s OK  %s" % (name, " | ".join(parts)))
        except Exception as e:  # noqa: BLE001 - report and continue
            ok = False
            print("%-9s FAIL %s: %s" % (name, type(e).__name__, e))
    return ok


def _dump_tex(root, level, outdir):
    from PIL import Image  # alleen hier nodig
    base = os.path.join(root, level, level)
    data = open(base + ".tex", "rb").read()
    tex = parse_tex(data)
    os.makedirs(outdir, exist_ok=True)
    for g in tex["groups"]:
        keyed = bool(g["flags"] & 1)
        px = decode_rgb565(data, g["frames"][0], g["width"], g["height"], colour_key=keyed)
        img = Image.new("RGBA", (g["width"], g["height"]))
        img.putdata(px)
        name = "%s_tex%03d_%dx%d_f%d_%08x.png" % (level, g["index"], g["width"], g["height"],
                                                   g["frame_count"], g["flags"])
        img.save(os.path.join(outdir, name))
    print("wrote %d textures to %s" % (len(tex["groups"]), outdir))


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(os.path.dirname(here), "extract", "Data")
    if len(argv) >= 4 and argv[1] == "--dump-tex":
        _dump_tex(root, argv[2], argv[3])
        return 0
    if len(argv) >= 2 and argv[1] == "--root":
        root = argv[2]
    return 0 if _validate_all(root) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
