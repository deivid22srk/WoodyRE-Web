"""Draws the port's own icon res/woodyre.ico (a red crest over a pecked tree trunk; no art of the game) at 16..256 px.
Usage: python tools/mkicon.py   (needs Pillow)"""
import math, os
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024                                                    # drawn large, scaled down per icon size

def feather(bx, by, ang, length, width, bend=-0.35, n=40):
    """a pointed leaf shape from (bx, by) along angle ang (degrees, screen y down), its tip bent back (negative = counter-clockwise)"""
    a = math.radians(ang); dx, dy = math.cos(a), math.sin(a); px, py = -dy, dx
    left, right = [], []
    for i in range(n + 1):
        t = i / n
        cx = bx + length * (t * dx + bend * t * t * px); cy = by + length * (t * dy + bend * t * t * py)
        w = width * math.sin(math.pi * min(1, t * 1.15)) ** 0.7 * (1 - t) ** 0.3
        left.append((cx + w * px, cy + w * py)); right.append((cx - w * px, cy - w * py))
    return left + right[::-1]

def draw():
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # rounded tile, warm wood gradient top to bottom
    tile = Image.new('RGBA', (S, S))
    td = ImageDraw.Draw(tile)
    for y in range(S):
        t = y / S
        td.line([(0, y), (S, y)], fill=(int(176 - 70 * t), int(112 - 50 * t), int(62 - 30 * t), 255))
    for x, w in ((180, 10), (330, 7), (690, 9), (840, 6)):  # grain
        td.line([(x, 0), (x + 30, S)], fill=(90, 52, 26, 255), width=w * 3)
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([16, 16, S - 16, S - 16], radius=200, fill=255)
    im.paste(tile, (0, 0), mask)
    # pecked hole, lower right
    d.ellipse([560, 600, 820, 860], fill=(48, 26, 12, 255))
    d.ellipse([590, 630, 790, 830], fill=(24, 12, 6, 255))
    # crest: three curved feathers fanning back from one base, darker copy behind each as a shadow
    red, dark = (228, 40, 46, 255), (120, 16, 20, 255)
    for ang, length, width in ((-165, 560, 78), (-132, 620, 84), (-98, 540, 78)):
        for col, off in ((dark, 18), (red, 0)):
            d.polygon(feather(500 + off, 700 + off, ang, length, width), fill=col)
    # flying wood chips
    for cx, cy, r in ((860, 520, 34), (930, 610, 24), (900, 430, 18)):
        d.rectangle([cx - r, cy - r // 2, cx + r, cy + r // 2], fill=(236, 196, 140, 255))
    return im

big = draw()
sizes = [256, 128, 64, 48, 32, 24, 16]
imgs = [big.resize((s, s), Image.LANCZOS) for s in sizes]
os.makedirs(os.path.join(root, 'res'), exist_ok=True)
out = os.path.join(root, 'res', 'woodyre.ico')
imgs[0].save(out, format='ICO', sizes=[(s, s) for s in sizes], append_images=imgs[1:])
print(out)
