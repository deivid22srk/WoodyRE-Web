"""Draws the port's own icon res/woodyre.ico at 16..256 px: a woodpecker (a generic black-and-white bird with a red crest,
no art or character of the game) pecking at a tree trunk, wood chips flying.
Usage: python tools/mkicon.py [preview.png]   (needs Pillow)"""
import math, os, sys
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024                                                    # drawn large, scaled down per icon size

SKY_TOP, SKY_BOT = (120, 190, 235), (200, 232, 250)
BARK, BARK_DARK, BARK_LIGHT = (122, 78, 44), (82, 50, 26), (160, 108, 64)
BLACK, WHITE, RED, RED_DARK, BEAK = (28, 28, 34), (246, 246, 240), (226, 36, 40), (160, 20, 26), (70, 70, 78)
CHIP = (236, 196, 140)

def curve(p0, p1, p2, n=24):
    """quadratic Bezier points p0 -> p2 with control point p1"""
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]

def draw():
    tile = Image.new('RGBA', (S, S))
    d = ImageDraw.Draw(tile)
    for y in range(S):                                      # sky
        t = y / S
        d.line([(0, y), (S, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(SKY_TOP, SKY_BOT)) + (255,))
    # tree trunk on the right, bark lines, the pecked hole where the beak hits
    d.rectangle([700, 0, S, S], fill=BARK)
    d.rectangle([700, 0, 728, S], fill=BARK_DARK)
    for x, y0, y1 in ((800, 40, 330), (900, 120, 470), (840, 660, 990), (950, 560, 900)):
        d.line([(x, y0), (x + 12, y1)], fill=BARK_DARK, width=18)
    d.line([(760, 0), (760, S)], fill=BARK_LIGHT, width=10)
    d.ellipse([686, 410, 760, 500], fill=BARK_DARK)

    # body / neck: black, from the head down to the bottom left
    d.polygon(curve((210, 1024), (190, 700), (330, 560)) + curve((560, 600), (520, 800), (600, 1024)), fill=BLACK)
    # head
    d.ellipse([250, 290, 590, 630], fill=BLACK)
    # red crest on the back of the head, swept back to a point
    crest = curve((300, 420), (250, 230), (110, 180)) + curve((110, 180), (330, 160), (520, 330)) + curve((520, 330), (420, 300), (300, 420))
    d.polygon([(x + 10, y + 12) for x, y in crest], fill=RED_DARK)
    d.polygon(crest, fill=RED)
    # white stripe from the base of the beak back along the cheek and down the neck
    stripe = curve((565, 490), (390, 520), (340, 840))
    d.line(stripe, fill=WHITE, width=58, joint='curve')
    d.ellipse([536, 461, 594, 519], fill=WHITE)
    # eye
    d.ellipse([440, 380, 520, 460], fill=WHITE)
    d.ellipse([468, 400, 512, 444], fill=BLACK)
    # beak: a long wedge into the trunk
    d.polygon([(560, 395), (720, 452), (560, 505)], fill=BEAK)
    d.line([(568, 452), (712, 452)], fill=(40, 40, 46), width=8)

    # wood chips flying off the impact
    for cx, cy, w, h, a in ((640, 330, 46, 20, -30), (610, 560, 40, 18, 25), (680, 250, 30, 14, -55), (660, 620, 28, 12, 50)):
        r = math.radians(a); c, s = math.cos(r), math.sin(r)
        d.polygon([(cx + c * dx - s * dy, cy + s * dx + c * dy) for dx, dy in ((-w, -h), (w, -h), (w, h), (-w, h))], fill=CHIP)

    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([16, 16, S - 16, S - 16], radius=190, fill=255)
    out.paste(tile, (0, 0), mask)
    return out

big = draw()
sizes = [256, 128, 64, 48, 32, 24, 16]
imgs = [big.resize((s, s), Image.LANCZOS) for s in sizes]
os.makedirs(os.path.join(root, 'res'), exist_ok=True)
out = os.path.join(root, 'res', 'woodyre.ico')
imgs[0].save(out, format='ICO', sizes=[(s, s) for s in sizes], append_images=imgs[1:])
if len(sys.argv) > 1:                                       # preview: the sizes side by side
    sheet = Image.new('RGBA', (256 + 64 + 32 + 16 + 40, 256), (255, 255, 255, 255)); x = 0
    for s in (256, 64, 32, 16): sheet.paste(big.resize((s, s), Image.LANCZOS), (x, 0)); x += s + 10
    sheet.save(sys.argv[1])
print(out)
