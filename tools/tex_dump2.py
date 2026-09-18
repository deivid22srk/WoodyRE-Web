import struct, sys, os
from PIL import Image
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
path, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
d = open(path, 'rb').read()
n = u32(d, 0); pos = 4
POW2 = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024}
ONE = struct.pack('<f', 1.0)
texs = []
for i in range(n):
    w, h = u32(d, pos), u32(d, pos + 4)
    if w not in POW2 or h not in POW2:
        print("desync at tex", i, hex(pos), w, h); break
    # header ends with: 00000000 0000803f 01000000 00000000 0000803f  (5 dwords)
    marker = b'\x00\x00\x00\x00' + ONE + b'\x01\x00\x00\x00' + b'\x00\x00\x00\x00' + ONE
    m = d.find(marker, pos + 8, pos + 96)
    if m < 0:
        print("no marker at tex", i, hex(pos)); break
    hdr = d[pos:m + 20]
    data = m + 20
    texs.append((i, pos, w, h, hdr, data))
    pos = data + w * h * 2
print("parsed", len(texs), "of", n, "textures; pos after pixels", hex(pos), "file", hex(len(d)), "remaining", len(d) - pos)
from collections import Counter
print("header lengths:", Counter(len(t[4]) for t in texs))
print("dims:", Counter((t[2], t[3]) for t in texs))
for t in texs[:6]:
    print("  tex %3d hdr=%s" % (t[0], t[4].hex()))
print("distinct 3rd/4th dwords:", Counter(t[4][8:16].hex() for t in texs).most_common(8))
# tail
tail = d[pos:]
print("tail dwords:", [u32(tail, i * 4) for i in range(0, min(len(tail) // 4, 16))])
print("tail floats:", [round(f32(tail, i * 4), 3) for i in range(0, min(len(tail) // 4, 16))])
if len(tail) % n == 0:
    print("tail record size per texture:", len(tail) // n)
# export a few as RGB565 and ARGB1555 (decide by alpha-bit usage)
for t in texs[:10] + texs[-3:]:
    i, p, w, h, hdr, data = t
    vals = struct.unpack('<%dH' % (w * h), d[data:data + w * h * 2])
    flag = u32(hdr, 8) if len(hdr) >= 12 else 0
    img = Image.new('RGB', (w, h))
    img.putdata([((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31) for v in vals])
    img.save(os.path.join(outdir, "tex%03d_565.png" % i))
    img = Image.new('RGBA', (w, h))
    img.putdata([((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255 if v >> 15 else 0) for v in vals])
    img.save(os.path.join(outdir, "tex%03d_1555.png" % i))
    img = Image.new('RGBA', (w, h))
    img.putdata([((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12) * 17) for v in vals])
    img.save(os.path.join(outdir, "tex%03d_4444.png" % i))
    print("  exported tex", i, w, h, "hdr[8:16]=", hdr[8:16].hex())
