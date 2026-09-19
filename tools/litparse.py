"""Per-licht statistieken van een .lit-bestand + controles van de beweringen in docs/LIGHTING.md.

usage: python tools/litparse.py [LEVEL] [--root extract/Data]      (default W1A)

Controles:
  1. negatieve indices in lijst C vallen binnen de extra-vertextabel ('probes'): -i-1 < n_extra
  2. extra vertices liggen op het vlak van de polygoon die ze gebruikt
  3. elke C-polygoon ligt in het vlak van een B-face (B = gedeeltelijk belichte ouder-faces), A en B zijn
     disjunct; het veld 'face' van een C-polygoon is het MATERIAAL-woord van de ouder (zelfde plek als
     in een .gel-face, +0x08), geen face-index
  4. lijst-A-faces: licht ligt voor het vlak en |dist| < bereik
  5. BSP-puntquery (0x40b540): zwaartepunt (iets van het vlak af) van C-polygonen en A-faces -> belicht
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levelparse  # noqa: E402
import gelparse    # noqa: E402


def bsp_query(light, p):
    """0x40b540: geeft face-index of -1."""
    nodes, planes = light['bsp_nodes'], light['bsp_planes']
    if not nodes:
        return -1
    i = 0
    while True:
        pl, front, back = nodes[i]
        a, b, c, d = planes[pl]
        v = front if (a * p[0] + b * p[1] + c * p[2] + d) > 0.0 else back
        kind = v & 0xF
        if kind == 0:
            i = v >> 4
            continue
        return (v >> 4) if kind == 1 else -1


def point_lit(light, gel, p):
    """Zoals 0x43ba2d..0x43ba80 / 0x42f20c: belicht als query -1 geeft of p vóór het bladvlak ligt."""
    f = bsp_query(light, p)
    if f < 0:
        return True
    a, b, c, d = gel['polys'][f]['plane']
    return a * p[0] + b * p[1] + c * p[2] + d > 0.0


def main(argv):
    root = 'extract/Data'
    args = [a for a in argv[1:] if not a.startswith('--')]
    if '--root' in argv:
        root = argv[argv.index('--root') + 1]
        args = [a for a in args if a != root]
    level = args[0] if args else 'W1A'
    base = os.path.join(root, level, level)
    gel = gelparse.parse_gel(open(base + '.gel', 'rb').read())
    lit = levelparse.parse_lit(open(base + '.lit', 'rb').read(), len(gel['cells']))
    verts, polys = gel['verts'], gel['polys']
    extra = lit['probes']
    print('%s: %d lichten, %d extra vertices, %d faces, %d cellen'
          % (level, len(lit['lights']), len(extra), len(polys), len(gel['cells'])))

    def vpos(i):
        return verts[i][:3] if i >= 0 else extra[-i - 1]['position']

    def centroid(idx, plane, eps=1.0):
        n = len(idx)
        c = [sum(vpos(i)[k] for i in idx) / n for k in range(3)]
        return [c[k] + plane[k] * eps for k in range(3)]

    tot = dict(bad_neg=0, off_plane=0, c_not_b=0, c_mat=0, a_and_b=0, a_bad=0,
               q_c=0, q_c_ok=0, q_a=0, q_a_ok=0)
    for li, L in enumerate(lit['lights']):
        A, B, C = L['faces'], L['faces_b'], L['polygons']
        sa, sb = set(A), set(B)
        pos, R = L['position'], L['radius']
        nneg = 0
        tot['a_and_b'] += len(sa & sb)
        bplanes = {}
        for f in B:
            bplanes.setdefault(tuple(round(x, 3) for x in polys[f]['plane']), []).append(f)
        for P in C:
            par = bplanes.get(tuple(round(x, 3) for x in P['plane']), [])
            if not par:
                tot['c_not_b'] += 1
            if not any(polys[f]['material_raw'] == P['face'] for f in par):
                tot['c_mat'] += 1
            a, b, c, d = P['plane']
            for i in P['indices']:
                i = i - (1 << 32) if i >= (1 << 31) else i
                if i < 0:
                    nneg += 1
                    if -i - 1 >= len(extra):
                        tot['bad_neg'] += 1
                        continue
                    x, y, z = extra[-i - 1]['position']
                    if abs(a * x + b * y + c * z + d) > 0.5:
                        tot['off_plane'] += 1
            idx = [i - (1 << 32) if i >= (1 << 31) else i for i in P['indices']]
            tot['q_c'] += 1
            tot['q_c_ok'] += point_lit(L, gel, centroid(idx, P['plane']))
        for f in A:
            a, b, c, d = polys[f]['plane']
            dist = a * pos[0] + b * pos[1] + c * pos[2] + d
            if not (0.0 < dist < R):
                tot['a_bad'] += 1
            tot['q_a'] += 1
            tot['q_a_ok'] += point_lit(L, gel, centroid(polys[f]['indices'], polys[f]['plane']))
        print('licht %2d obj=%06x f28=%d pos=(%.0f,%.0f,%.0f) rgb=(%.0f,%.0f,%.0f) R=%.0f  A=%d B=%d C=%d '
              '(neg.idx %d) cel-ranges=%d bsp=%d/%d'
              % (li, L['object_id'] & 0xFFFFFF, L['field_28'], pos[0], pos[1], pos[2],
                 L['color'][0], L['color'][1], L['color'][2], R, len(A), len(B), len(C), nneg, len(L['ranges']),
                 len(L['bsp_nodes']), len(L['bsp_planes'])))
    print('controles:')
    print('  1. negatieve index buiten extra-tabel : %d' % tot['bad_neg'])
    print('  2. extra vertex niet op polygoonvlak  : %d' % tot['off_plane'])
    print('  3. C-polygoon zonder coplanaire B-face: %d   C.face != materiaal van die B-face: %d   |A en B|: %d'
          % (tot['c_not_b'], tot['c_mat'], tot['a_and_b']))
    print('  4. A-face met licht achter vlak of buiten bereik: %d' % tot['a_bad'])
    print('  5. BSP-query: C-zwaartepunt belicht %d/%d, A-zwaartepunt belicht %d/%d'
          % (tot['q_c_ok'], tot['q_c'], tot['q_a_ok'], tot['q_a']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
