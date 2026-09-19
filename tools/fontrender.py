"""fontrender.py - render text with the bitmap font (rck type 3) exactly like the engine (0x43f890 / 0x4419a0).

usage:
  python tools/fontrender.py chart  <bank.rck> out.png                 glyph chart (code above every glyph)
  python tools/fontrender.py string <bank.rck> <ref> out.png [textbank.rck] [size]
        <ref>  = string index in textbank (default: the same bank), or "all" for every string of that bank
  python tools/fontrender.py text   <bank.rck> "Hello 123" out.png [size]   (ASCII -> code via GLYPHMAP)

Font payload (docs/HUD_TEXT.md): 0x1c header, n*20 glyph records, pages*size*size*4 RGBA (top row first).
String: u16 codes, 0 = end, 1..n = glyph n-1, 4000 (0xfa0) = newline.
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from rckparse import parse_rck
from PIL import Image, ImageDraw

# code (1-based, as stored in the strings) -> character; derived from the chart, see docs/HUD_TEXT.md
GLYPHS = ("0123456789QuitAre yos?CnYN%=/:+"          # 1..31
          "LERD!SUTOKBHIGFagPkwmdlcvWpVXMf.h()®q'b,$zJx"  # 32..75 (level/hub fonts stop here)
          "&-éôjç\"Z°")                       # 76..84 (Credits font only)

class Font:
    def __init__(self, raw):
        self.n, self.pages, self.psize = struct.unpack_from('<III', raw, 0)
        self.height, self.f14, self.base, self.f1c = struct.unpack_from('<4f', raw, 0xc)
        self.g = [struct.unpack_from('<ffHHHHHH', raw, 0x1c + i * 20) for i in range(self.n)]
        o = 0x1c + self.n * 20
        sz = self.psize * self.psize * 4
        self.img = [Image.frombytes('RGBA', (self.psize, self.psize), raw[o + p * sz:o + (p + 1) * sz], 'raw', 'RGBA')
                    for p in range(self.pages)]
        assert o + self.pages * sz == len(raw)
        self.extra = 0.0            # font+0x28 (always 0)

    def scale(self, size):          # 0x43f850: font+0x58 = size / (height - base)
        return size / (self.height - self.base)

    def line_advance(self, size):   # 0x441960
        return (self.extra + self.height) * self.scale(size)

    def glyph_h(self, size):        # 0x441980
        return (2 * abs(self.f1c) + self.extra + self.height) * self.scale(size)

    def measure(self, codes, size):  # 0x441b30
        x = y = 0.0; w = 0.0
        for c in codes:
            if c == 0: break
            if c == 4000: x = 0.0; y += self.line_advance(size); continue
            if 1 <= c <= self.n: x += self.g[c - 1][1] * self.scale(size); w = max(w, x)
        return w, y

    def draw(self, dst, x0, y0, codes, size, colour=(255, 255, 255, 128), vw=640, vh=480):
        """dst is an RGBA image of any size; coordinates are in the vw x vh virtual screen (font mode 3)."""
        sx, sy = dst.width / vw, dst.height / vh
        s = self.scale(size)
        x, y = float(x0), float(y0)
        for c in codes:
            if c == 0: break
            if c == 4000: x = x0 * 0 ; y += self.line_advance(size); continue     # engine: x = 0 (!), y += line
            if not (1 <= c <= self.n): continue
            w, adv, gx, gy, gw, gh, page, _ = self.g[c - 1]
            px, py = x * sx, y * sy
            pw, ph = w * s * sx, self.glyph_h(size) * sy
            x += adv * s
            src = self.img[page].crop((gx, gy, gx + gw, gy + gh))
            src = src.resize((max(1, round(pw)), max(1, round(ph))), Image.BILINEAR)
            # modulate: colour * texel, alpha*2 (engine colour 0x80ffffff = "1.0")
            r, g, b, a = src.split()
            k = min(1.0, colour[3] / 128.0)
            a = a.point(lambda v: int(v * k))
            tint = Image.merge('RGBA', (r.point(lambda v: v * colour[0] // 255), g.point(lambda v: v * colour[1] // 255),
                                        b.point(lambda v: v * colour[2] // 255), a))
            dst.alpha_composite(tint, (int(px), int(py)))

def load_font(path):
    r = parse_rck(open(path, 'rb').read())
    return Font(r['items']['font'][0]['raw']), r

def text_codes(s):
    out = []
    for ch in s:
        if ch == '\n': out.append(4000)
        else: out.append(GLYPHS.index(ch) + 1)
    return out

def main():
    mode, bank = sys.argv[1], sys.argv[2]
    font, r = load_font(bank)
    if mode == 'chart':
        cols = 10; cw, ch = 64, 90
        rows = (font.n + cols - 1) // cols
        im = Image.new('RGBA', (cols * cw, rows * ch), (40, 40, 90, 255))
        d = ImageDraw.Draw(im)
        for i in range(font.n):
            x, y = (i % cols) * cw, (i // cols) * ch
            w, adv, gx, gy, gw, gh, page, _ = font.g[i]
            im.alpha_composite(font.img[page].crop((gx, gy, gx + gw, gy + gh)), (x + 4, y + 22))
            d.text((x + 4, y + 4), '%d' % (i + 1), fill=(255, 255, 0, 255))
        im.save(sys.argv[3]); return
    if mode == 'string':
        ref, out = sys.argv[3], sys.argv[4]
        tb = parse_rck(open(sys.argv[5], 'rb').read()) if len(sys.argv) > 5 and sys.argv[5] != '-' else r
        size = float(sys.argv[6]) if len(sys.argv) > 6 else 30.0
        strs = tb['items']['string']
        sel = range(len(strs)) if ref == 'all' else [int(ref, 0) & 0xffff]
        lh = int(font.glyph_h(size)) + 2
        nl = sum(1 + strs[i]['glyphs'].count(4000) for i in sel)
        im = Image.new('RGBA', (1280, max(1, nl) * lh * 2 + 8), (40, 40, 90, 255))
        y = 2
        d = ImageDraw.Draw(im)
        for i in sel:
            codes = strs[i]['glyphs']
            d.text((2, y * 2), '%d' % i, fill=(255, 255, 0, 255))
            font.draw(im, 24, y, codes, size, vw=640, vh=im.height / 2)
            y += lh * (1 + codes.count(4000))
        im.save(out); return
    if mode == 'text':
        s, out = sys.argv[3], sys.argv[4]
        size = float(sys.argv[5]) if len(sys.argv) > 5 else 30.0
        im = Image.new('RGBA', (640, 480), (40, 40, 90, 255))
        font.draw(im, 20, 20, text_codes(s), size)
        im.save(out); return

if __name__ == '__main__':
    main()
