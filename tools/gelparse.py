"""Parser voor het .gel-levelgeometrieformaat van Woody Woodpecker: Escape from
Buzz Buzzard Park (Eko Software, 2001).

Afgeleid uit de loader op 0x407ae0 in Woody.exe (zie docs/FORMAT_GEL.md).
Alleen de Python-standaardbibliotheek wordt gebruikt.

    from gelparse import parse_gel
    gel = parse_gel(open('W1A.gel', 'rb').read())

Bestandsvolgorde:
  1. polygonen        (u32 count, u32 total_indices, records)
  2. portaalpolygonen (u32 count, u32 total_indices, records zonder materiaal)
  3. groepen/zones    (u32 count, count x u32 end, per groep portaallijst)
  4. vertices         (u32 count, count x 16 bytes)
  5. kd-bladcellen    (u32 count, cel-records)
  6. kd-boom          (u32 count, count x 16 bytes)
  7. sectoren         (u32 count, cel-records, zelfde opbouw als 5)

Als __main__: parseert alle extract/Data/*/*.gel, controleert dat precies het
hele bestand wordt geconsumeerd en drukt per level een samenvatting af.
"""
import struct
import glob
import os
import sys

INT_MIN = -0x80000000


class _Reader:
    __slots__ = ('data', 'pos')

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def u32(self):
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v

    def f32s(self, n):
        v = struct.unpack_from('<%df' % n, self.data, self.pos)
        self.pos += 4 * n
        return v

    def u32s(self, n):
        v = struct.unpack_from('<%dI' % n, self.data, self.pos)
        self.pos += 4 * n
        return v

    def i32s(self, n):
        v = struct.unpack_from('<%di' % n, self.data, self.pos)
        self.pos += 4 * n
        return v


def _read_polygon(r, with_material):
    """Polygoonrecord (engine: 0x1c + 4*n bytes, zie loader 0x407bee / 0x407cca).

    bestand: u32 nverts, [u32 material], float plane[4], u32 index[nverts]
    """
    n = r.u32()
    material = r.u32() if with_material else 0xFFFFFFFF
    plane = r.f32s(4)
    indices = r.u32s(n)
    return {
        'nverts': n,
        # laag 15 bits = index in materiaaltabel van <LVL>.tex; bit 15 = geen materiaal
        'material': material & 0x7FFF,
        'no_material': bool(material & 0x8000),
        'material_raw': material,
        'plane': plane,          # (nx, ny, nz, d): nx*x + ny*y + nz*z + d
        'indices': indices,
    }


def _read_polygon_section(r, with_material):
    count = r.u32()
    total_indices = r.u32()
    polys = [_read_polygon(r, with_material) for _ in range(count)]
    got = sum(p['nverts'] for p in polys)
    if got != total_indices:
        raise ValueError('index total mismatch: header %d, sum %d' % (total_indices, got))
    return polys


def _read_kd_node(r):
    """16-byte kd-knoop (evaluator 0x40ab10, lookup 0x408180/0x4081c0).

    i32 type: laag 16 bits = as (0=x,1=y,2=z); hoog 16 bits = -1 voor gewone
    knoop, anders sectorindex (alleen zinvol in de hoofdboom; in de lokale
    buurbomen van cellen/sectoren staat hier vaak 0 of een rommelwaarde die
    de engine nooit leest).
    float d: test is  p[as] + d <= 0  -> child_le, anders child_gt.
    i32 child_le / child_gt: >= 0 knoopindex, < 0 blad = ~waarde.
    """
    t = r.i32()
    d = r.f32s(1)[0]
    c0, c1 = r.i32s(2)
    axis = struct.unpack('<h', struct.pack('<H', t & 0xFFFF))[0]
    hi = t >> 16
    return {'axis': axis, 'sector': hi, 'raw_type': t, 'split': -d, 'd': d,
            'child_le': c0, 'child_gt': c1}


def _read_cell(r):
    """Cel-record (engine-object 0x4c bytes, vtable 0x4a94f4; loader 0x407f33 / 0x408081).

    bestand: u32 npoly, u32 poly[npoly], float bbox[6], i32 link[6],
             u32 nnodes, node[nnodes] (16 bytes elk)

    link[i] beschrijft de buur door bbox-vlak i (volgorde -x,+x,-y,+y,-z,+z):
      INT_MIN  geen buur
      < 0      direct een buurcel: ~link
      >= 0     wortel van een sub-kd-boom in 'nodes' die op basis van het
               uittredepunt de buurcel kiest.  Let op: kindindices in die
               sub-boom zijn RELATIEF t.o.v. de wortel (0x40ab60 rekent
               ecx*16 + &nodes[link]), dus kind k = nodes[link + k].
    """
    n = r.u32()
    polys = r.u32s(n)
    bbox = r.f32s(6)              # xmin, xmax, ymin, ymax, zmin, zmax
    links = r.i32s(6)             # buur door vlak -x,+x,-y,+y,-z,+z
    m = r.u32()
    nodes = [_read_kd_node(r) for _ in range(m)]
    neigh = []
    for v in links:
        if v == INT_MIN:
            neigh.append(None)                 # geen buur (wereldrand)
        elif v < 0:
            neigh.append(('cell', ~v))         # direct een buurcel
        else:
            neigh.append(('node', v))          # wortel in lokale boom 'nodes'
    return {'polys': polys, 'bbox': bbox, 'links': links, 'neighbours': neigh,
            'nodes': nodes}


def _subtree_lookup(nodes, root, point):
    """kd-afdaling in een sub-boom met relatieve kindindices (0x40ab60)."""
    k = 0
    while True:
        n = nodes[root + k]
        c = n['child_le'] if point[n['axis']] + n['d'] <= 0 else n['child_gt']
        if c < 0:
            return ~c
        k = c


def neighbour_cell(cells, cell_index, face, point):
    """Buurcel van cells[cell_index] door bbox-vlak 'face' (0..5) voor uittredepunt 'point'.

    Geeft de index in 'cells' of None (geen buur).  Werkt voor gel['cells']
    en gel['sectors'].  Dit is wat 0x40a0c0 doet met vlak 2 (-y) om de vloer te zoeken.
    """
    c = cells[cell_index]
    v = c['links'][face]
    if v == INT_MIN:
        return None
    if v < 0:
        return ~v
    return _subtree_lookup(c['nodes'], v, point)


def subtree_leaves(nodes, root):
    """Alle bladeren (celindices) onder sub-boomwortel 'root' (relatieve kindindices)."""
    out = []
    stack = [0]
    while stack:
        k = stack.pop()
        n = nodes[root + k]
        for ch in (n['child_le'], n['child_gt']):
            if ch < 0:
                out.append(~ch)
            else:
                stack.append(ch)
    return out


def parse_gel(data):
    """Parseert een volledig .gel-bestand en geeft een dict terug.

    Sleutels:
      polys        lijst polygoonrecords (nverts, material, no_material, plane, indices)
      portals      lijst portaalpolygonen (zelfde opbouw, material altijd 'geen')
      groups       lijst {first, last, end, portals:[(portal_idx, group_idx), ...]}
      verts        lijst (x, y, z, colour)
      cells        kd-bladcellen: {polys, bbox, links, neighbours, nodes}
      kdtree       hoofd-kd-boom (knopen; bladeren = ~child -> index in cells)
      sectors      sectoren, zelfde opbouw als cells (bladeren = sectorindex)
      bbox         (xmin, xmax, ymin, ymax, zmin, zmax) over alle vertices
      consumed     aantal geconsumeerde bytes
    """
    r = _Reader(data)

    polys = _read_polygon_section(r, with_material=True)
    portals = _read_polygon_section(r, with_material=False)

    ngroups = r.u32()
    ends = r.u32s(ngroups)
    groups = []
    prev = 0
    for e in ends:
        groups.append({'first': prev, 'last': e - 1, 'end': e})
        prev = e
    for g in groups:
        k = r.u32()
        pairs = [tuple(r.u32s(2)) for _ in range(k)]
        g['portals'] = pairs          # (index in portals, index in groups)

    nverts = r.u32()
    verts = [struct.unpack_from('<3fI', data, r.pos + 16 * i) for i in range(nverts)]
    r.pos += 16 * nverts

    ncells = r.u32()
    cells = [_read_cell(r) for _ in range(ncells)]

    nnodes = r.u32()
    kdtree = [_read_kd_node(r) for _ in range(nnodes)]

    nsectors = r.u32()
    sectors = [_read_cell(r) for _ in range(nsectors)]

    if verts:
        bbox = (min(v[0] for v in verts), max(v[0] for v in verts),
                min(v[1] for v in verts), max(v[1] for v in verts),
                min(v[2] for v in verts), max(v[2] for v in verts))
    else:
        bbox = None

    return {
        'polys': polys,
        'portals': portals,
        'groups': groups,
        'verts': verts,
        'cells': cells,
        'kdtree': kdtree,
        'sectors': sectors,
        'bbox': bbox,
        'consumed': r.pos,
    }


def kd_lookup(gel, point):
    """Zoekt de kd-bladcel (index in gel['cells']) die het punt bevat (0x408180)."""
    nodes = gel['kdtree']
    i = 0
    while True:
        n = nodes[i]
        c = n['child_le'] if point[n['axis']] + n['d'] <= 0 else n['child_gt']
        if c < 0:
            return ~c
        i = c


def kd_sector(gel, point):
    """Zoekt de sector (index in gel['sectors']) van een punt (0x4081c0)."""
    nodes = gel['kdtree']
    n = nodes[0]
    while n['sector'] == -1:
        c = n['child_le'] if point[n['axis']] + n['d'] <= 0 else n['child_gt']
        n = nodes[c]
    return n['sector']


def _validate(gel):
    """Consistentiecontroles die uit de engine volgen (indexbereiken, boomstructuur)."""
    nv = len(gel['verts'])
    npoly = len(gel['polys'])
    for p in gel['polys'] + gel['portals']:
        assert all(i < nv for i in p['indices']), 'vertexindex buiten bereik'
    if gel['groups']:
        assert gel['groups'][-1]['end'] == npoly, 'groepstabel dekt niet alle polygonen'
        for g in gel['groups']:
            for a, b in g['portals']:
                assert a < len(gel['portals']) and b < len(gel['groups']), 'portaalpaar buiten bereik'
    ncells = len(gel['cells'])
    for n in gel['kdtree']:
        for c in (n['child_le'], n['child_gt']):
            assert (c >= 0 and c < len(gel['kdtree'])) or (c < 0 and ~c < ncells), 'kd-kind buiten bereik'
    roots = [n['sector'] for n in gel['kdtree'] if n['sector'] != -1]
    assert sorted(roots) == list(range(len(gel['sectors']))), 'sectorwortels kloppen niet'
    face_opposite = (1, 0, 3, 2, 5, 4)
    for arr, name in ((gel['cells'], 'cells'), (gel['sectors'], 'sectors')):
        for c in arr:
            assert all(i < npoly for i in c['polys']), name + ': polygoonindex buiten bereik'
            covered = set()
            for face, v in enumerate(c['links']):
                if v == INT_MIN:
                    continue
                if v < 0:
                    assert ~v < len(arr), name + ': buurindex buiten bereik'
                    leaves = [~v]
                else:
                    assert v < len(c['nodes']), name + ': knoopindex buiten bereik'
                    # sub-boom met relatieve indices: geen cycli, alles binnen de array
                    stack = [0]
                    seen = set()
                    leaves = []
                    while stack:
                        k = stack.pop()
                        assert k not in seen, name + ': cyclus in buurboom'
                        seen.add(k)
                        assert v + k < len(c['nodes']), name + ': buurboomkind buiten bereik'
                        n = c['nodes'][v + k]
                        for ch in (n['child_le'], n['child_gt']):
                            if ch < 0:
                                assert ~ch < len(arr), name + ': buurblad buiten bereik'
                                leaves.append(~ch)
                            else:
                                stack.append(ch)
                    covered |= {v + k for k in seen}
                # elke gevonden buur grenst exact aan dit vlak
                for L in leaves:
                    assert abs(arr[L]['bbox'][face_opposite[face]] - c['bbox'][face]) < 1e-2, name + ': buur grenst niet'
            assert len(covered) == len(c['nodes']), name + ': ongebruikte buurboomknopen'


def main(argv):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    pattern = argv[1] if len(argv) > 1 else os.path.join(root, 'extract', 'Data', '*', '*.gel')
    files = sorted(glob.glob(pattern))
    if not files:
        print('geen .gel-bestanden gevonden voor', pattern)
        return 1
    bad = 0
    for f in files:
        data = open(f, 'rb').read()
        try:
            gel = parse_gel(data)
            assert gel['consumed'] == len(data), 'consumed %d of %d bytes' % (gel['consumed'], len(data))
            _validate(gel)
        except Exception as e:  # noqa: BLE001
            bad += 1
            print('%-10s FOUT: %s' % (os.path.basename(f), e))
            continue
        b = gel['bbox']
        nport = sum(len(g['portals']) for g in gel['groups'])
        print('%-13s %8d B  polys %6d  portals %4d  groups %3d (portalrefs %3d)  verts %6d  '
              'cells %5d  kdnodes %5d  sectors %4d  bbox x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f'
              % (os.path.basename(f), len(data), len(gel['polys']), len(gel['portals']),
                 len(gel['groups']), nport, len(gel['verts']), len(gel['cells']),
                 len(gel['kdtree']), len(gel['sectors']), b[0], b[1], b[2], b[3], b[4], b[5]))
    print('%d bestanden, %d fouten' % (len(files), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
