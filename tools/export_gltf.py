"""export_gltf.py - export a level (world geometry from .gel, textures/materials from .tex, instances from .ins)
to a single .glb for viewer/index.html or Blender.

usage: python tools/export_gltf.py <LVL> [--out out/gltf/<LVL>.glb] [--no-tex]
"""
import sys, os, argparse, io
sys.path.insert(0, os.path.dirname(__file__))
from gltfwriter import GltfBuilder, quat_from_matrix
from gelparse import parse_gel

def load_materials(builder, lvl, datadir, want_tex):
    """Returns (material_table, gltf_material_index_per_table_entry). Uses levelparse when available."""
    try:
        import levelparse
    except ImportError:
        return None, {}
    tex = levelparse.parse_tex(open(os.path.join(datadir, lvl, lvl + '.tex'), 'rb').read())
    return tex, {}

def export(lvl, out, datadir='extract/Data', want_tex=True):
    gel = parse_gel(open(os.path.join(datadir, lvl, lvl + '.gel'), 'rb').read())
    b = GltfBuilder()
    verts = gel['verts']
    # one primitive per material id (so textures can be assigned later); vertex colours from the .gel
    by_mat = {}
    for p in gel['polys']:
        if p['no_material']: continue                # invisible polygons (bit 15): sky boxes / blockers
        key = 0     # TODO: group by texture via the .tex material table (levelparse)
        idx = p['indices']
        for k in range(1, len(idx) - 1):           # fan-triangulate n-gons
            by_mat.setdefault(key, []).extend((idx[0], idx[k], idx[k + 1]))
    prims = []
    flat = b.add_flat_material((1, 1, 1, 1), 'world')
    positions = [(v[0], v[1], -v[2]) for v in verts]                 # D3D left-handed -> glTF right-handed
    colors = [((c >> 16) & 255, (c >> 8) & 255, c & 255, 255) for (_, _, _, c) in verts]
    for mat, tris in sorted(by_mat.items()):
        # remap to a compact vertex list per primitive
        used = sorted(set(tris)); remap = {v: i for i, v in enumerate(used)}
        prims.append({'positions': [positions[v] for v in used], 'colors': [colors[v] for v in used],
                      'indices': [remap[v] for v in tris], 'material': flat, 'gel_material': mat})
    # flip winding for the handedness change
    for p in prims:
        ind = p['indices']
        for i in range(0, len(ind), 3): ind[i + 1], ind[i + 2] = ind[i + 2], ind[i + 1]
    world_mesh = b.add_mesh('world', prims)
    b.add_node('world', mesh=world_mesh, extras={'level': lvl})
    os.makedirs(os.path.dirname(out), exist_ok=True)
    size = b.save(out)
    print('%s: %d polys, %d verts, %d material groups -> %s (%d bytes)' % (lvl, len(gel['polys']), len(verts), len(prims), out, size))

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('level'); ap.add_argument('--out'); ap.add_argument('--no-tex', action='store_true')
    a = ap.parse_args()
    export(a.level, a.out or os.path.join('out', 'gltf', a.level + '.glb'), want_tex=not a.no_tex)
