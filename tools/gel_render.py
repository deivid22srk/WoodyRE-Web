import struct, sys, os
from PIL import Image, ImageDraw
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
path, out = sys.argv[1], sys.argv[2]
d = open(path, 'rb').read()
nf = u32(d, 0)
faces = []
pos = 16
for i in range(nf):
    idx = [u32(d, pos + 16 + 4 * k) for k in range(3)]
    faces.append(idx); pos += 36
ngroups = u32(d, pos)
groups = [u32(d, pos + 4 + 4 * i) for i in range(ngroups)]
pos += 4 + 4 * ngroups
# skip zero padding until vertex count
while u32(d, pos) == 0: pos += 4
nv = u32(d, pos); pos += 4
print("faces", nf, "groups", ngroups, groups, "verts", nv, "verts at", hex(pos))
verts = []
cols = []
for i in range(nv):
    x, y, z = f32(d, pos), f32(d, pos + 4), f32(d, pos + 8); c = u32(d, pos + 12)
    verts.append((x, y, z)); cols.append(c); pos += 16
print("vertex section ends at", hex(pos), "next dwords:", [u32(d, pos + 4 * i) for i in range(12)])
print("next floats:", [round(f32(d, pos + 4 * i), 3) for i in range(12)])
xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
print("bounds x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f" % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
W = 1600
sx = (max(xs) - min(xs)) or 1; sz = (max(zs) - min(zs)) or 1
scale = (W - 40) / max(sx, sz)
H = int(sz * scale) + 40
img = Image.new('RGB', (W, H), (12, 12, 16)); dr = ImageDraw.Draw(img)
def P(v): return (20 + (v[0] - min(xs)) * scale, 20 + (v[2] - min(zs)) * scale)
ymin, ymax = min(ys), max(ys)
for a, b, c in faces:
    if max(a, b, c) >= nv: continue
    va, vb, vc = verts[a], verts[b], verts[c]
    h = ((va[1] + vb[1] + vc[1]) / 3 - ymin) / ((ymax - ymin) or 1)
    col = (int(40 + 200 * h), int(120 + 100 * (1 - h)), int(220 - 180 * h))
    dr.polygon([P(va), P(vb), P(vc)], outline=col)
img.save(out)
print("saved", out, img.size)
# also a side view (x,y)
H2 = int((max(ys) - min(ys)) * scale) + 40
img2 = Image.new('RGB', (W, max(H2, 100)), (12, 12, 16)); dr2 = ImageDraw.Draw(img2)
def P2(v): return (20 + (v[0] - min(xs)) * scale, 20 + (max(ys) - v[1]) * scale)
for a, b, c in faces:
    if max(a, b, c) >= nv: continue
    dr2.polygon([P2(verts[a]), P2(verts[b]), P2(verts[c])], outline=(90, 160, 220))
img2.save(out.replace('.png', '_side.png'))
