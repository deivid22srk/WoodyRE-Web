"""gltfwriter.py - minimal dependency-free glTF 2.0 (.glb) writer used by the level exporter.

Supports: meshes (positions, optional uvs / vertex colours, u32 indices), PBR-less textured materials
(baseColorTexture, optional alpha), nodes with TRS or matrix, node extras, and keyframe animations
(translation/rotation/scale samplers). Output is a single binary .glb with all images embedded.
"""
import struct, json, math

def _pad(b, n=4, fill=b'\0'):
    return b + fill * ((-len(b)) % n)

class GltfBuilder:
    def __init__(self):
        self.bin = bytearray()
        self.bufferViews = []; self.accessors = []; self.images = []; self.textures = []; self.samplers = []
        self.materials = []; self.meshes = []; self.nodes = []; self.animations = []
        self.scene_nodes = []
        self.default_material = None

    # -- buffers ----------------------------------------------------------------
    def _view(self, data, target=None, stride=None):
        self.bin.extend(b'\0' * ((-len(self.bin)) % 4))
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target: v['target'] = target
        if stride: v['byteStride'] = stride
        self.bin.extend(data)
        self.bufferViews.append(v); return len(self.bufferViews) - 1

    def _accessor(self, view, ctype, count, atype, minmax=None, normalized=False):
        a = {'bufferView': view, 'componentType': ctype, 'count': count, 'type': atype}
        if minmax: a['min'], a['max'] = minmax
        if normalized: a['normalized'] = True
        self.accessors.append(a); return len(self.accessors) - 1

    def vec3_accessor(self, vecs, target=34962):
        data = struct.pack('<%df' % (3 * len(vecs)), *[c for v in vecs for c in v])
        mn = [min(v[i] for v in vecs) for i in range(3)]; mx = [max(v[i] for v in vecs) for i in range(3)]
        return self._accessor(self._view(data, target), 5126, len(vecs), 'VEC3', (mn, mx))

    def vec2_accessor(self, vecs):
        data = struct.pack('<%df' % (2 * len(vecs)), *[c for v in vecs for c in v])
        return self._accessor(self._view(data, 34962), 5126, len(vecs), 'VEC2')

    def vec4_accessor(self, vecs):
        data = struct.pack('<%df' % (4 * len(vecs)), *[c for v in vecs for c in v])
        return self._accessor(self._view(data, 34962), 5126, len(vecs), 'VEC4')

    def color_accessor(self, rgba_u8):
        data = bytes(c for v in rgba_u8 for c in v)
        return self._accessor(self._view(data, 34962), 5121, len(rgba_u8), 'VEC4', normalized=True)

    def index_accessor(self, indices):
        data = struct.pack('<%dI' % len(indices), *indices)
        return self._accessor(self._view(data, 34963), 5125, len(indices), 'SCALAR')

    def scalar_accessor(self, floats):
        data = struct.pack('<%df' % len(floats), *floats)
        return self._accessor(self._view(data), 5126, len(floats), 'SCALAR', ([min(floats)], [max(floats)]))

    # -- materials ----------------------------------------------------------------
    def add_texture_material(self, png_bytes, name=None, alpha=False, double_sided=True, nearest=False):
        img_view = self._view(png_bytes)
        self.images.append({'bufferView': img_view, 'mimeType': 'image/png'})
        if not self.samplers:
            self.samplers.append({'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497})
            self.samplers.append({'magFilter': 9728, 'minFilter': 9728, 'wrapS': 10497, 'wrapT': 10497})
        self.textures.append({'source': len(self.images) - 1, 'sampler': 1 if nearest else 0})
        m = {'pbrMetallicRoughness': {'baseColorTexture': {'index': len(self.textures) - 1}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0},
             'doubleSided': double_sided}
        if name: m['name'] = name
        if alpha: m['alphaMode'] = 'MASK'; m['alphaCutoff'] = 0.5
        self.materials.append(m); return len(self.materials) - 1

    def add_flat_material(self, rgba=(1, 1, 1, 1), name=None):
        m = {'pbrMetallicRoughness': {'baseColorFactor': list(rgba), 'metallicFactor': 0.0, 'roughnessFactor': 1.0}, 'doubleSided': True}
        if name: m['name'] = name
        self.materials.append(m); return len(self.materials) - 1

    # -- meshes ----------------------------------------------------------------------
    def add_mesh(self, name, primitives):
        """primitives: list of dicts {positions:[(x,y,z)], indices:[int], uvs:[(u,v)]?, colors:[(r,g,b,a) u8]?, material:int?}"""
        prims = []
        for p in primitives:
            if not p['indices']: continue
            attrs = {'POSITION': self.vec3_accessor(p['positions'])}
            if p.get('uvs'): attrs['TEXCOORD_0'] = self.vec2_accessor(p['uvs'])
            if p.get('colors'): attrs['COLOR_0'] = self.color_accessor(p['colors'])
            prim = {'attributes': attrs, 'indices': self.index_accessor(p['indices']), 'mode': 4}
            if p.get('material') is not None: prim['material'] = p['material']
            prims.append(prim)
        self.meshes.append({'name': name, 'primitives': prims}); return len(self.meshes) - 1

    # -- nodes -------------------------------------------------------------------------
    def add_node(self, name, mesh=None, translation=None, rotation=None, scale=None, matrix=None, extras=None, children=None, root=True):
        n = {'name': name}
        if mesh is not None: n['mesh'] = mesh
        if matrix is not None: n['matrix'] = list(matrix)
        else:
            if translation: n['translation'] = list(translation)
            if rotation: n['rotation'] = list(rotation)
            if scale: n['scale'] = list(scale)
        if extras: n['extras'] = extras
        if children: n['children'] = list(children)
        self.nodes.append(n); idx = len(self.nodes) - 1
        if root: self.scene_nodes.append(idx)
        return idx

    def set_children(self, node, children):
        self.nodes[node]['children'] = list(children)
        for c in children:
            if c in self.scene_nodes: self.scene_nodes.remove(c)

    # -- animation ------------------------------------------------------------------
    def add_animation(self, name, channels):
        """channels: list of dicts {node:int, path:'translation'|'rotation'|'scale', times:[s], values:[tuple]}"""
        anim = {'name': name, 'samplers': [], 'channels': []}
        for ch in channels:
            if len(ch['times']) < 1: continue
            t = self.scalar_accessor(ch['times'])
            v = self.vec4_accessor(ch['values']) if ch['path'] == 'rotation' else self.vec3_accessor(ch['values'], target=None)
            anim['samplers'].append({'input': t, 'output': v, 'interpolation': ch.get('interpolation', 'LINEAR')})
            anim['channels'].append({'sampler': len(anim['samplers']) - 1, 'target': {'node': ch['node'], 'path': ch['path']}})
        if anim['channels']: self.animations.append(anim)

    # -- output -----------------------------------------------------------------------
    def save(self, path):
        gltf = {'asset': {'version': '2.0', 'generator': 'WoodyRE export_gltf.py'},
                'scene': 0, 'scenes': [{'nodes': self.scene_nodes}], 'nodes': self.nodes, 'meshes': self.meshes,
                'accessors': self.accessors, 'bufferViews': self.bufferViews, 'buffers': [{'byteLength': len(self.bin)}]}
        if self.materials: gltf['materials'] = self.materials
        if self.textures: gltf['textures'] = self.textures; gltf['images'] = self.images; gltf['samplers'] = self.samplers
        if self.animations: gltf['animations'] = self.animations
        js = _pad(json.dumps(gltf, separators=(',', ':')).encode(), 4, b' ')
        bn = _pad(bytes(self.bin))
        total = 12 + 8 + len(js) + 8 + len(bn)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
            f.write(struct.pack('<II', len(bn), 0x004E4942)); f.write(bn)
        return total

def quat_from_matrix(m):
    """m: 3x3 rotation as nested lists (row-major) -> (x, y, z, w)"""
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2; return ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2; return (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2; return ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2; return ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
