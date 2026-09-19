"""export_gltf.py - export a level (world geometry from .gel, textures/materials from .tex, later instances
from .ins) to a single .glb for viewer/index.html or Blender.

usage: python tools/export_gltf.py <LVL> [--out out/gltf/<LVL>.glb] [--no-tex] [--all]
"""
import sys, os, argparse, io, struct
sys.path.insert(0, os.path.dirname(__file__))
from gltfwriter import GltfBuilder, quat_from_matrix
from gelparse import parse_gel
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

def export(lvl, out, datadir='extract/Data', want_tex=True):
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
            pr['positions'].append((x, y, -z))                          # D3D left-handed -> glTF right-handed
            pr['colors'].append(((c >> 16) & 255, (c >> 8) & 255, c & 255, 255))
            pr['uvs'].append((uvf[0] * x + uvf[3] * y + uvf[6] * z + uvf[9], uvf[1] * x + uvf[4] * y + uvf[7] * z + uvf[10]) if uvf else (0.0, 0.0))
        n = len(p['indices'])
        for k in range(1, n - 1):                                          # fan-triangulate, flipped winding
            pr['indices'].extend((basei, basei + k + 1, basei + k))
    plist = [prims[k] for k in sorted(prims)]
    world_mesh = b.add_mesh('world', plist)
    b.add_node('world', mesh=world_mesh, extras={'level': lvl})
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    size = b.save(out)
    print('%s: %d polys, %d verts, %d texture groups, %d materials -> %s (%.1f MB)' % (
        lvl, len(gel['polys']), len(verts), len(groups), len(mats), out, size / 1e6))

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('level', nargs='?'); ap.add_argument('--out'); ap.add_argument('--no-tex', action='store_true'); ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    levels = sorted(os.listdir('extract/Data')) if a.all else [a.level]
    for lvl in levels:
        export(lvl, a.out or os.path.join('out', 'gltf', lvl + '.glb'), want_tex=not a.no_tex)
