"""glyphmatch.py - read the text of another language build by matching its font glyphs against the English font.

Each release generates its own font (glyph order = first occurrence in that language's text, docs/HUD_TEXT.md 1.5),
so the u16 codes in its strings mean different letters. Glyphs that are pixel-equal (or nearly) to an English glyph
get that letter; the rest (accented letters etc.) print as {n}.

usage: python tools/glyphmatch.py <en font bank.rck> <other font bank.rck> [<en text bank> <other text bank>]
  e.g. python tools/glyphmatch.py extract/Data/W1A/W1A.rck out/builds/br/cd/Data/W1A/W1A.rck \
           extract/Common/Woody.rck out/builds/br/cd/Common/Woody.rck
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rckparse import parse_rck
from fontrender import Font, GLYPHS

def font_of(path):
    return Font(parse_rck(open(path, 'rb').read())['items']['font'][0]['raw'])

def crops(f):
    out = []
    for (w, adv, x, y, wpx, hpx, page, junk) in f.g:
        im = f.img[page].crop((x, y, x + wpx, y + hpx)).convert('LA') if wpx and hpx else None
        out.append((wpx, hpx, im.tobytes() if im else b''))
    return out

def match(en, other):
    """code (1-based) in `other` -> English code (1-based) or None"""
    ce, co = crops(en), crops(other)
    m = {}
    for i, (w, hh, px) in enumerate(co):
        best, bd = None, None
        for j, (w2, h2, px2) in enumerate(ce):
            if (w2, h2) != (w, hh) or not px: continue
            d = sum(abs(a - b) for a, b in zip(px, px2)) / max(1, len(px))
            if bd is None or d < bd: best, bd = j, d
        m[i + 1] = best + 1 if best is not None and bd < 2.0 else None
    return m

def decode(codes, m, glyphs=GLYPHS):
    s = ''
    for c in codes:
        if c == 0: break
        if c == 4000: s += '|'; continue
        e = m.get(c) if m is not None else c
        s += glyphs[e - 1] if e and e - 1 < len(glyphs) else '{%d}' % c
    return s

if __name__ == '__main__':
    en, other = font_of(sys.argv[1]), font_of(sys.argv[2])
    m = match(en, other)
    unk = [c for c, e in m.items() if e is None]
    print('glyphs: english %d, other %d, matched %d, unmatched %s' % (en.n, other.n, len(m) - len(unk), unk))
    if len(sys.argv) > 4:
        ta = parse_rck(open(sys.argv[3], 'rb').read())['items']['string']
        tb = parse_rck(open(sys.argv[4], 'rb').read())['items']['string']
        for i in range(max(len(ta), len(tb))):
            a = decode(ta[i]['glyphs'], None) if i < len(ta) else '-'
            b = decode(tb[i]['glyphs'], m) if i < len(tb) else '-'
            print('%3d  %-40s | %s' % (i, a, b))
