"""export_gltf.py - export a level (world geometry from .gel, textures/materials from .tex, later instances
from .ins) to a single .glb for viewer/index.html or Blender.

usage: python tools/export_gltf.py <LVL> [--out out/gltf/<LVL>.glb] [--no-tex] [--all]
"""
import sys, os, argparse, io, struct
sys.path.insert(0, os.path.dirname(__file__))
from gltfwriter import GltfBuilder, quat_from_matrix
from gelparse import parse_gel
from insparse import parse_ins
import levelparse

def rgb565_png(frame, w, h, color_key):
    """RGB565 frame -> PNG bytes (RGBA). Magenta (R,B high, G low) is transparent when color_key is set."""
    from PIL import Image
    px = struct.unpack('<%dH' % (w * h), frame)
    out = bytearray(w * h * 4)
    for i, v in enumerate(px):
        r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2)
        a = 0 if (color_key and (r & 0xF0) == 0xF0 and (b & 0xF0) == 0xF0 and (g & 0xF0) == 0) else 255
        out[4 * i:4 * i + 4] = bytes((r, g, b, a))
    buf = io.BytesIO(); Image.frombytes('RGBA', (w, h), bytes(out)).save(buf, 'PNG'); return buf.getvalue()

def export(lvl, out, datadir='extract/Data', want_tex=True, anim_index=0):
    base = os.path.join(datadir, lvl, lvl)
    gel = parse_gel(open(base + '.gel', 'rb').read())
    texdata = open(base + '.tex', 'rb').read()
    tex = levelparse.parse_tex(texdata)
    b = GltfBuilder()
    verts = gel['verts']
    groups = tex['groups']; mats = tex['materials']

    # glTF material per texture group (frame 0)
    gmat = {}
    for gi, g in enumerate(groups):
        if want_tex:
            off = g['frames'][0]; fsz = g['width'] * g['height'] * 2
            png = rgb565_png(texdata[off:off + fsz], g['width'], g['height'], g['flags'] & 1)
            gmat[gi] = b.add_texture_material(png, 'tex%d' % gi, alpha=bool(g['flags'] & 1))
        else:
            gmat[gi] = b.add_flat_material((1, 1, 1, 1), 'tex%d' % gi)
    flat = b.add_flat_material((0.8, 0.8, 0.8, 1), 'nomaterial')

    # one primitive per texture group; vertices are split per polygon corner because UVs are a per-polygon
    # planar projection (u = m0*x + m3*y + m6*z + m9, v = m1*x + m4*y + m7*z + m10)
    prims = {}
    for p in gel['polys']:
        if p['no_material']:
            key, uvf = -1, None
        else:
            m = mats[p['material']]; key = m['group']; uvf = m['matrix']
        pr = prims.setdefault(key, {'positions': [], 'uvs': [], 'colors': [], 'indices': [], 'material': gmat.get(key, flat)})
        basei = len(pr['positions'])
        for vi in p['indices']:
            x, y, z, c = verts[vi]
            pr['positions'].append((x, y, z))                           # world is right-handed already (3ds Max export)
            pr['colors'].append((c2x(c), c2x(c >> 8), c2x(c >> 16), 255))    # bytes R,G,B; 128 = neutral
            pr['uvs'].append((uvf[0] * x + uvf[3] * y + uvf[6] * z + uvf[9], uvf[1] * x + uvf[4] * y + uvf[7] * z + uvf[10]) if uvf else (0.0, 0.0))
        n = len(p['indices'])
        for k in range(1, n - 1):                                          # fan-triangulate
            pr['indices'].extend((basei, basei + k, basei + k + 1))
    plist = [prims[k] for k in sorted(prims)]
    world_mesh = b.add_mesh('world', plist)
    b.add_node('world', mesh=world_mesh, extras={'level': lvl})

    # models + instances + animations from the .ins
    ins = parse_ins(open(base + '.ins', 'rb').read())
    ninst = export_models(b, ins, mats, gmat, flat, anim_index)
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    size = b.save(out)
    print('%s: %d polys, %d verts, %d texture groups, %d materials, %d models, %d instances -> %s (%.1f MB)' % (
        lvl, len(gel['polys']), len(verts), len(groups), len(mats), len(ins['models']), ninst, out, size / 1e6))

def sub(a, c): return (a[0] - c[0], a[1] - c[1], a[2] - c[2])
def c2x(v): return min(255, (v & 255) * 2)
def flipz(v): return (v[0], v[1], v[2])                # historical name: the game world turned out to be right-handed, no mirroring needed
def conjq(q):
    """track key quaternion -> the rotation the engine applies: 0x440370 negates x,y,z and the loader does not pre-negate track keys"""
    q = flipq(q); return (-q[0], -q[1], -q[2], q[3])
def flipq(q):
    """quaternion (x,y,z,w) from the file (stored with norm 2) -> unit quaternion"""
    n = (q[0] ** 2 + q[1] ** 2 + q[2] ** 2 + q[3] ** 2) ** 0.5 or 1.0
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)

def rgb565_rgba(v):
    """flat model colour: ARGB1555 (bit 15 = 'flat colour' flag doubles as alpha), converted to linear RGB for glTF"""
    r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31
    srgb = (((r << 3) | (r >> 2)) / 255.0, ((g << 3) | (g >> 2)) / 255.0, ((b << 3) | (b >> 2)) / 255.0)
    lin = tuple(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb)
    return lin + (1.0,)

def export_models(b, ins, mats, gmat, flat, anim_index):
    """One glTF node tree per instance (nodes cannot be shared in glTF; meshes are). Returns instance count."""
    flat_mats = {}
    def material_for(poly):
        if poly['flat_color']:
            key = poly['material'] & 0xffff            # whole 16 bits are the RGB565 colour (0xFFFF = white)
            if key not in flat_mats: flat_mats[key] = b.add_flat_material(rgb565_rgba(key), 'rgb565_%04x' % key)
            return flat_mats[key], None
        m = mats[poly['material_index'] if poly.get('material_index') is not None else poly['material']]
        return gmat.get(m['group'], flat), m['matrix']

    total = 0
    for mi, model in enumerate(ins['models']):
        if not model['instances']: continue
        nodes = model['nodes']; pts = model['points']
        # build one mesh per mesh node (positions relative to the node pivot)
        node_mesh = {}
        for ni, nd in enumerate(nodes):
            if nd['kind'] != 0 or not nd['polys']: continue
            prims = {}
            for poly in nd['polys']:
                matidx, uvf = material_for(poly)
                pr = prims.setdefault(matidx, {'positions': [], 'uvs': [], 'colors': [], 'indices': [], 'material': matidx})
                basei = len(pr['positions'])
                for pi in poly['indices']:
                    p = pts[pi]; x, y, z = p['pos']
                    pr['positions'].append(flipz(sub(p['pos'], nd['pivot'])))
                    c = p['color']; pr['colors'].append((min(255, int(c[0] * 2)), min(255, int(c[1] * 2)), min(255, int(c[2] * 2)), 255))   # 128 = neutral
                    pr['uvs'].append((uvf[0] * x + uvf[3] * y + uvf[6] * z + uvf[9], uvf[1] * x + uvf[4] * y + uvf[7] * z + uvf[10]) if uvf else (0.0, 0.0))
                n = len(poly['indices'])
                for k in range(1, n - 1): pr['indices'].extend((basei, basei + k, basei + k + 1))
            node_mesh[ni] = b.add_mesh('m%d_n%d' % (mi, ni), list(prims.values()))
        owner = {}
        for ni, nd in enumerate(nodes):
            for k in range(nd['point_base'], nd['point_base'] + nd['npoints']): owner[k] = ni
        tri_mesh = None
        if model['tris']:
            # the model-level triangles form a skin: every point is transformed by the node that owns it
            prims = {}
            for t in model['tris']:
                matidx, uvf = material_for(t)
                pr = prims.setdefault(matidx, {'positions': [], 'uvs': [], 'colors': [], 'indices': [], 'joints': [], 'material': matidx})
                basei = len(pr['positions'])
                for pi in t['indices']:
                    p = pts[pi]; x, y, z = p['pos']; o = owner[pi]
                    pr['positions'].append(flipz(sub(p['pos'], nodes[o]['pivot']))); pr['joints'].append(o)
                    c = p['color']; pr['colors'].append((min(255, int(c[0] * 2)), min(255, int(c[1] * 2)), min(255, int(c[2] * 2)), 255))   # 128 = neutral
                    pr['uvs'].append((uvf[0] * x + uvf[3] * y + uvf[6] * z + uvf[9], uvf[1] * x + uvf[4] * y + uvf[7] * z + uvf[10]) if uvf else (0.0, 0.0))
                pr['indices'].extend((basei, basei + 1, basei + 2))       # file order is i2,i1,i0
            tri_mesh = b.add_mesh('m%d_tris' % mi, list(prims.values()))

        for inst in model['instances']:
            total += 1
            root = b.add_node('inst%d_m%d' % (inst['index'], mi), translation=flipz(inst['position']), rotation=flipq(inst['quat']),
                              scale=inst['scale'], extras={'instance': inst['index'], 'model': mi, 'volumes': inst['volume_ids']})
            gl = {}
            for ni, nd in enumerate(nodes):
                par = nd['parent'] - 1 if nd['parent'] > 0 else -1
                ppiv = nodes[par]['pivot'] if par >= 0 else (0.0, 0.0, 0.0)
                gl[ni] = b.add_node('n%d_k%02x' % (ni, nd['kind']), mesh=node_mesh.get(ni), translation=flipz(sub(nd['pivot'], ppiv)), root=False,
                                    extras={'kind': nd['kind']})
            children = {}
            for ni, nd in enumerate(nodes):
                par = nd['parent'] - 1 if nd['parent'] > 0 else -1
                children.setdefault(par, []).append(gl[ni])
            for par, ch in children.items():
                if par >= 0: b.set_children(gl[par], ch)
            top = list(children.get(-1, []))
            if tri_mesh is not None:
                skin = b.add_skin([gl[ni] for ni in range(len(nodes))], skeleton=root)
                top.append(b.add_node('skin', mesh=tri_mesh, root=False, skin=skin))
            b.set_children(root, top)
            # animation (only for the first instance of a model, to keep the file small)
            if anim_index is not None and inst is model['instances'][0] and model['nanims'] > anim_index:
                an = model['anims'][anim_index]; scale = an['duration_s'] / max(an['nframes'], 1)
                chans = []
                for ni, nd in enumerate(nodes):
                    pt = nd['pos_tracks'][anim_index] if nd['pos_tracks'] else None
                    rt = nd['rot_tracks'][anim_index] if nd['rot_tracks'] else None
                    if pt and len(pt[0]) == 4:
                        chans.append({'node': gl[ni], 'path': 'translation', 'times': [f[0] * scale for f in pt], 'values': [flipz(f[1:4]) for f in pt]})
                    if rt and len(rt[0]) == 5:
                        chans.append({'node': gl[ni], 'path': 'rotation', 'times': [f[0] * scale for f in rt], 'values': [conjq(f[1:5]) for f in rt]})   # track keys are applied conjugated (0x440370)
                if chans: b.add_animation('m%d_anim%d' % (mi, anim_index), chans)
    return total

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('level', nargs='?'); ap.add_argument('--out'); ap.add_argument('--no-tex', action='store_true'); ap.add_argument('--all', action='store_true'); ap.add_argument('--anim', type=int, default=0, help='animation index to export per model (-1 = none)')
    a = ap.parse_args()
    levels = sorted(os.listdir('extract/Data')) if a.all else [a.level]
    for lvl in levels:
        export(lvl, a.out or os.path.join('out', 'gltf', lvl + '.glb'), want_tex=not a.no_tex, anim_index=None if a.anim < 0 else a.anim)
