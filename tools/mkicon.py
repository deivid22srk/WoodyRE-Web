"""Draws the port's own icon res/woodyre.ico at 16..256 px: a gold coin with a chunky red W, in the spirit of the game's
W pickups but drawn here from plain shapes (no art of the game is used or traced).
Usage: python tools/mkicon.py [preview.png]   (needs Pillow)"""
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024                                                    # drawn large, scaled down per icon size
C = S / 2

RIM_DARK, RIM, FACE_IN, FACE_OUT = (150, 92, 10), (214, 150, 24), (255, 226, 92), (240, 182, 36)
RED, RED_DARK, SHINE = (222, 32, 36), (110, 8, 12), (255, 248, 210)

def w_mask(stroke=140, top=300, mid=462, bottom=730, xs=(236, 380, 512, 644, 788)):
    """the W as four slanted strokes with horizontal ends, on an L mask"""
    m = Image.new('L', (S, S), 0); d = ImageDraw.Draw(m); h = stroke / 2
    tops = [(xs[0], top), (xs[2], mid), (xs[2], mid), (xs[4], top)]
    bots = [(xs[1], bottom), (xs[1], bottom), (xs[3], bottom), (xs[3], bottom)]
    for (x0, y0), (x1, y1) in zip(tops, bots):
        d.polygon([(x0 - h, y0), (x0 + h, y0), (x1 + h, y1), (x1 - h, y1)], fill=255)
    d.rectangle([xs[2] - h * 0.9, mid, xs[2] + h * 0.9, mid + 40], fill=255)   # a flat cap on the middle peak
    return m

def draw():
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    R = 488
    d.ellipse([C - R, C - R, C + R, C + R], fill=RIM_DARK)                # rim, darker at the edge
    d.ellipse([C - R + 22, C - R + 22, C + R - 22, C + R - 22], fill=RIM)
    face = Image.new('RGBA', (S, S), (0, 0, 0, 0)); fd = ImageDraw.Draw(face)
    r0 = R - 70
    for i in range(r0, 0, -4):                                            # face: radial gradient, lighter top left
        t = i / r0
        col = tuple(int(a + (b - a) * t) for a, b in zip(FACE_IN, FACE_OUT)) + (255,)
        off = (1 - t) * 60
        fd.ellipse([C - i - off, C - i - off, C + i - off, C + i - off], fill=col)
    fmask = Image.new('L', (S, S), 0); ImageDraw.Draw(fmask).ellipse([C - r0, C - r0, C + r0, C + r0], fill=255)
    im.paste(face, (0, 0), fmask)
    d.arc([C - r0 - 8, C - r0 - 8, C + r0 + 8, C + r0 + 8], 200, 290, fill=SHINE, width=14)   # glint on the rim

    w = w_mask()
    outline = w.filter(ImageFilter.MaxFilter(41))
    im.paste(Image.new('RGBA', (S, S), RED_DARK + (255,)), (20, 26), outline)   # drop shadow
    im.paste(Image.new('RGBA', (S, S), RED_DARK + (255,)), (0, 0), outline)
    im.paste(Image.new('RGBA', (S, S), RED + (255,)), (0, 0), w)
    hi = w_mask(stroke=36, top=322, mid=486, bottom=690, xs=(236, 380, 512, 644, 788)).filter(ImageFilter.GaussianBlur(4))
    hi = Image.composite(hi, Image.new('L', (S, S), 0), w.filter(ImageFilter.MinFilter(41)))   # thin highlight inside the strokes
    im.paste(Image.new('RGBA', (S, S), (255, 120, 110, 255)), (-14, -10), hi.point(lambda v: v * 0.55))
    return im

big = draw()
sizes = [256, 128, 64, 48, 32, 24, 16]
imgs = [big.resize((s, s), Image.LANCZOS) for s in sizes]
os.makedirs(os.path.join(root, 'res'), exist_ok=True)
out = os.path.join(root, 'res', 'woodyre.ico')
imgs[0].save(out, format='ICO', sizes=[(s, s) for s in sizes], append_images=imgs[1:])
if len(sys.argv) > 1:                                       # preview: the sizes side by side, on light and dark
    sheet = Image.new('RGBA', (2 * (256 + 64 + 32 + 16 + 40), 256), (255, 255, 255, 255))
    ImageDraw.Draw(sheet).rectangle([sheet.width // 2, 0, sheet.width, 256], fill=(40, 40, 46, 255))
    for base in (0, sheet.width // 2):
        x = base
        for s in (256, 64, 32, 16): ic = big.resize((s, s), Image.LANCZOS); sheet.paste(ic, (x, 0), ic); x += s + 10
    sheet.save(sys.argv[1])
print(out)
