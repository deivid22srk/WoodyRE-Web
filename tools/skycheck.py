"""skycheck.py - numerieke controle van het sky-box-recept uit docs/SKY.md.

De engine (0x42ac10, deel 0x42ad35..0x42b373) tekent de hemel NIET met de .gel-faces van de
sky-groep (texture-vlaggen byte 1 == 2, `cmp byte [tex+0x45], 2` op 0x42acea); die faces
worden overgeslagen en dienen alleen als schakelaar.  In plaats daarvan worden vijf quads van
een kubus met halve ribbe 50000 ([0x4aa2f8]) rond de camerapositie getekend, elk met een
eigen texture:

  quad  vlak    frame in de groep   levelbank-afbeelding (als de level-.rck >= 5 afbeeldingen heeft)
  A     z=+S    0                   3
  B     x=+S    1                   0
  C     z=-S    2                   1
  D     x=-S    3                   2
  E     y=+S    4                   4

UV's per quad (vertexvolgorde v0..v3): (hu,hv) (hu,1-hv) (1-hu,1-hv) (1-hu,hv) met
hu = 0.5/breedte, hv = 0.5/hoogte.

Dit script
  * zoekt de sky-groep (byte 1 van de vlaggen == 2) en telt de .gel-faces die hem gebruiken,
  * laat zien wat de gewone material_uv-projectie voor die faces zou geven (ter vergelijking),
  * bouwt de kubus, drukt per quad de texture, de UV-range en de hoekpunten af,
  * controleert de naden numeriek: RMS-kleurverschil tussen de twee texelrijen langs elke
    gedeelde kubusribbe, vergeleken met dezelfde rijen in de verkeerde richting (gespiegeld),
  * schrijft out/sky_<LVL>_equirect.png (360 x 180 graden) en out/sky_<LVL>_cross.png.

gebruik:  python tools/skycheck.py [LEVEL ...]      (standaard: House W2D W3A)
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levelparse  # noqa: E402
import gelparse    # noqa: E402
import rckparse    # noqa: E402

from PIL import Image  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
S = 50000.0   # [0x4aa2f8]

# quad -> (naam, frame in de groep, index in de levelbank, 4 hoekpunten als tekens van (x,y,z))
# hoekpunten exact zoals 0x42ae54..0x42b33f ze opbouwt; UV-volgorde v0=(0,0) v1=(0,1) v2=(1,1) v3=(1,0)
QUADS = [
    ('A z=+S', 0, 3, [(-1, -1, +1), (-1, +1, +1), (+1, +1, +1), (+1, -1, +1)]),   # 0x42ae54
    ('B x=+S', 1, 0, [(+1, -1, +1), (+1, +1, +1), (+1, +1, -1), (+1, -1, -1)]),   # 0x42af58
    ('C z=-S', 2, 1, [(+1, -1, -1), (+1, +1, -1), (-1, +1, -1), (-1, -1, -1)]),   # 0x42b05b
    ('D x=-S', 3, 2, [(-1, -1, -1), (-1, +1, -1), (-1, +1, +1), (-1, -1, +1)]),   # 0x42b164
    ('E y=+S', 4, 4, [(-1, +1, +1), (-1, +1, -1), (+1, +1, -1), (+1, +1, +1)]),   # 0x42b26e
]
UV01 = [(0, 0), (0, 1), (1, 1), (1, 0)]


def load_level(lv):
    d = os.path.join(ROOT, 'extract', 'Data', lv)
    texdata = open(os.path.join(d, lv + '.tex'), 'rb').read()
    tex = levelparse.parse_tex(texdata)
    gel = gelparse.parse_gel(open(os.path.join(d, lv + '.gel'), 'rb').read())
    rck = rckparse.parse_rck(open(os.path.join(d, lv + '.rck'), 'rb').read())
    return texdata, tex, gel, rck


def frame_image(texdata, g, i):
    px = levelparse.decode_rgb565(texdata, g['frames'][i], g['width'], g['height'])
    im = Image.new('RGB', (g['width'], g['height']))
    im.putdata([p[:3] for p in px])
    return im


def rck_image(item):
    w, h = item['w'], item['h']
    im = Image.frombytes('RGBA', (w, h), item['pixels'][:w * h * 4], 'raw', 'BGRA')
    return im.convert('RGB')


def quad_uv(quad, d):
    """Richting d -> (u,v) in 0..1 op deze quad, of None als d de quad niet raakt.
    u,v zijn bilineair in de hoekpunten; omdat elke quad asvlak-uitgelijnd is volstaat een
    lineaire oplossing uit v0, v1 (v-as) en v3 (u-as)."""
    _, _, _, c = quad
    # vlak: de coordinaat die voor alle vier hoekpunten gelijk is
    for ax in range(3):
        if len({p[ax] for p in c}) == 1:
            break
    sgn = c[0][ax]
    if d[ax] * sgn <= 1e-12:
        return None
    t = 1.0 / (d[ax] * sgn)
    p = [d[0] * t, d[1] * t, d[2] * t]              # punt op de eenheidskubus
    eu = [c[3][k] - c[0][k] for k in range(3)]      # v0 -> v3 : u 0 -> 1
    ev = [c[1][k] - c[0][k] for k in range(3)]      # v0 -> v1 : v 0 -> 1
    rel = [p[k] - c[0][k] for k in range(3)]
    u = sum(rel[k] * eu[k] for k in range(3)) / 4.0
    v = sum(rel[k] * ev[k] for k in range(3)) / 4.0
    if -1e-9 <= u <= 1 + 1e-9 and -1e-9 <= v <= 1 + 1e-9:
        return u, v
    return None


def sample(im, u, v):
    """Bilineair, met de halve-texel-inzet van de engine (u in 0..1 -> hu..1-hu)."""
    w, h = im.size
    x = (0.5 + u * (w - 1))
    y = (0.5 + v * (h - 1))
    x = min(max(x - 0.5, 0.0), w - 1.0)
    y = min(max(y - 0.5, 0.0), h - 1.0)
    x0, y0 = int(x), int(y)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    fx, fy = x - x0, y - y0
    px = im.load()
    a, b, c, d = px[x0, y0], px[x1, y0], px[x0, y1], px[x1, y1]
    return tuple(int((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy + 0.5)
                 for k in range(3))


def edge_texels(im, quad, pa, pb, n=64):
    """Texels van `im` langs de kubusribbe pa->pb (hoekpunten als tekens), n monsters."""
    out = []
    for i in range(n):
        t = (i + 0.5) / n
        d = [pa[k] + (pb[k] - pa[k]) * t for k in range(3)]
        uv = quad_uv(quad, d)
        out.append(sample(im, *uv))
    return out


def rms(a, b):
    s = 0.0
    for p, q in zip(a, b):
        s += sum((p[k] - q[k]) ** 2 for k in range(3))
    return math.sqrt(s / (3 * len(a)))


def check(lv):
    texdata, tex, gel, rck = load_level(lv)
    print('=' * 78)
    print(lv)
    sky = [g for g in tex['groups'] if (g['flags'] >> 8) & 0xff == 2]
    odd = [g for g in tex['groups'] if (g['flags'] >> 8) & 0xff not in (0, 2)]
    for g in odd:
        print('  groep %d: vlaggen 0x%08x (byte1 = %d, GEEN sky; de engine test alleen == 2), %dx%d, %d frames, duur %g'
              % (g['index'], g['flags'], (g['flags'] >> 8) & 0xff, g['width'], g['height'],
                 g['frame_count'], g['anim_duration']))
        mats = {i for i, m in enumerate(tex['materials']) if m['group'] == g['index']}
        n = sum(1 for p in gel['polys'] if not p['no_material'] and (p['material'] & 0x7fff) in mats)
        print('    materialen %s, .gel-faces: %d' % (sorted(mats), n))
    if not sky:
        print('  geen groep met byte1 == 2 -> geen sky box')
        return
    nimg = len(rck['items']['image'])
    for g in sky:
        print('  sky-groep %d: vlaggen 0x%08x, %dx%d, %d frames, anim_duration %g, scroll (%g, %g)'
              % (g['index'], g['flags'], g['width'], g['height'], g['frame_count'],
                 g['anim_duration'], g['scroll_u'], g['scroll_v']))
    g = sky[-1]
    mats = {i for i, m in enumerate(tex['materials']) if m['group'] in {s['index'] for s in sky}}
    faces = [p for p in gel['polys'] if not p['no_material'] and (p['material'] & 0x7fff) in mats]
    ml = sorted(mats)
    print('  materialen van de sky-groep: %d stuks (%s%s); .gel-faces die ze gebruiken: %d (worden NIET getekend)'
          % (len(ml), ', '.join(map(str, ml[:8])), ' ...' if len(ml) > 8 else '', len(faces)))
    if faces:
        vs = [gel['verts'][i] for p in faces for i in p['indices']]
        print('  bbox van die faces: x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f'
              % (min(v[0] for v in vs), max(v[0] for v in vs), min(v[1] for v in vs),
                 max(v[1] for v in vs), min(v[2] for v in vs), max(v[2] for v in vs)))
        us, vv = [], []
        for p in faces:
            m = tex['materials'][p['material'] & 0x7fff]
            for i in p['indices']:
                x, y, z, _ = gel['verts'][i]
                u, v = levelparse.material_uv(m, x, y, z)
                us.append(u)
                vv.append(v)
        print('  ter vergelijking, material_uv op die faces: u %.1f..%.1f  v %.1f..%.1f  (onbruikbaar)'
              % (min(us), max(us), min(vv), max(vv)))

    use_rck = nimg >= 5                                   # cmp [0x5e8670], 5 ; jge
    print('  levelbank (.rck) afbeeldingen: %d -> bron = %s' % (nimg, 'LEVELBANK-afbeeldingen 3,0,1,2,4'
          if use_rck else 'frames 0..4 van de groep'))
    if not use_rck and g['frame_count'] < 5:
        print('  !! groep heeft maar %d frame(s) en de bank < 5 afbeeldingen: de engine zou buiten de groep lezen'
              % g['frame_count'])
        return
    imgs = []
    for q in QUADS:
        if use_rck:
            imgs.append(rck_image(rck['items']['image'][q[2]]))
        else:
            imgs.append(frame_image(texdata, g, q[1]))

    print('  quad      texture        grootte   u-range            v-range            hoekpunten v0..v3 (x,y,z)/S')
    for q, im in zip(QUADS, imgs):
        w, h = im.size
        hu, hv = 0.5 / w, 0.5 / h
        src = ('rck[%d]' % q[2]) if use_rck else ('frame %d' % q[1])
        print('  %-8s  %-13s  %3dx%-3d   %.5f..%.5f   %.5f..%.5f   %s'
              % (q[0], src, w, h, hu, 1 - hu, hv, 1 - hv, ' '.join('(%+d,%+d,%+d)' % c for c in q[3])))

    # naden: elke ribbe die twee quads delen
    print('  naden (RMS 0..255 tussen de randtexels van beide quads langs de gedeelde ribbe;')
    print('         "gespiegeld" = dezelfde randen in tegengestelde richting, moet duidelijk slechter zijn):')
    worst = 0.0
    for i in range(len(QUADS)):
        for j in range(i + 1, len(QUADS)):
            shared = [c for c in QUADS[i][3] if c in QUADS[j][3]]
            if len(shared) != 2:
                continue
            pa, pb = shared
            ea = edge_texels(imgs[i], QUADS[i], pa, pb)
            eb = edge_texels(imgs[j], QUADS[j], pa, pb)
            r, rf = rms(ea, eb), rms(ea, eb[::-1])
            worst = max(worst, r)
            print('    %s | %s   ribbe %s-%s   RMS %.1f   gespiegeld %.1f' % (QUADS[i][0], QUADS[j][0], pa, pb, r, rf))
    print('  slechtste naad: RMS %.1f' % worst)

    # equirect-preview: yaw 0 = +z, yaw loopt naar +x toe; rechtshandig y-up en van binnenuit
    # gezien ligt +x LINKS van +z, dus yaw naar rechts in beeld = richting -x.
    W, H = 720, 360
    out = Image.new('RGB', (W, H), (255, 0, 255))
    op = out.load()
    for py in range(H):
        pitch = (0.5 - (py + 0.5) / H) * math.pi
        cy, sy = math.cos(pitch), math.sin(pitch)
        for px_ in range(W):
            yaw = ((px_ + 0.5) / W - 0.5) * 2 * math.pi
            d = (-math.sin(yaw) * cy, sy, math.cos(yaw) * cy)
            for q, im in zip(QUADS, imgs):
                uv = quad_uv(q, d)
                if uv:
                    op[px_, py] = sample(im, *uv)
                    break
            else:
                op[px_, py] = (0, 0, 0)          # onderkant: de engine tekent geen bodemquad
    os.makedirs(os.path.join(ROOT, 'out'), exist_ok=True)
    p1 = os.path.join(ROOT, 'out', 'sky_%s_equirect.png' % lv)
    out.save(p1)

    # kruis: bovenaan E, daaronder D A B C naast elkaar, elk zoals van binnenuit gezien
    # (rechtop: wereld +y boven).  Van binnenuit: v=0 ligt op y=-S, dus het plaatje staat
    # 180 graden gedraaid t.o.v. het bestand.
    n = 128
    cross = Image.new('RGB', (4 * n, 2 * n), (0, 0, 0))
    order = [1, 0, 3, 2]         # B A D C: +x links van +z, -x rechts van +z
    for k, qi in enumerate(order):
        cross.paste(imgs[qi].resize((n, n)).rotate(180), (k * n, n))
    # E boven A (kolom 1): E's v=0-rand ligt op z=+S (grenst aan A); u loopt x=-S -> +S, in beeld is +x links
    top = imgs[4].resize((n, n)).rotate(180)
    cross.paste(top, (n, 0))
    p2 = os.path.join(ROOT, 'out', 'sky_%s_cross.png' % lv)
    cross.save(p2)
    print('  geschreven: %s, %s' % (os.path.relpath(p1, ROOT), os.path.relpath(p2, ROOT)))


def dump_frames(lv, gi):
    texdata, tex, gel, rck = load_level(lv)
    g = tex['groups'][gi]
    n = g['frame_count']
    sc = max(1, 64 // g['width'])
    sheet = Image.new('RGB', (n * (g['width'] * sc + 2), g['height'] * sc), (255, 0, 255))
    for i in range(n):
        im = frame_image(texdata, g, i).resize((g['width'] * sc, g['height'] * sc), Image.NEAREST)
        sheet.paste(im, (i * (g['width'] * sc + 2), 0))
    p = os.path.join(ROOT, 'out', 'frames_%s_g%d.png' % (lv, gi))
    sheet.save(p)
    print('  frames van %s groep %d -> %s' % (lv, gi, os.path.relpath(p, ROOT)))


if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == '--frames':
        dump_frames(args[1], int(args[2]))
    else:
        for lv in (args or ['House', 'W2D', 'W3A']):
            check(lv)
