import struct, sys, os
from collections import Counter
from PIL import Image
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
path, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
d = open(path, 'rb').read()
pos = 0x38; entries = []
while pos + 8 <= len(d):
    sz = u32(d, pos); tag = u32(d, pos + 4)
    if sz == 0 or pos + 8 + sz > len(d): break
    entries.append((pos, sz, tag, d[pos + 8:pos + 8 + sz])); pos += 8 + sz
print(os.path.basename(path), "entries", len(entries), "tags", Counter(hex(e[2]) for e in entries))
imgs = 0; others = 0
for p, sz, tag, e in entries:
    is_sound = len(e) >= 16 and u32(e, 4) in (11025, 22050, 44100) and u32(e, 8) in (8, 16)
    is_img = (not is_sound) and len(e) >= 8 and u16(e, 0) in (8, 16, 32, 64, 128, 256, 512) and u16(e, 2) in (8, 16, 32, 64, 128, 256, 512)
    if is_img and imgs < 3:
        w, h, bpp = u16(e, 0), u16(e, 2), u16(e, 4)
        px = e[8:8 + w * h * 4]
        img = Image.frombytes('RGBA', (w, h), px, 'raw', 'BGRA')
        img.save(os.path.join(outdir, "%s_img%d_%dx%d_bpp%d.png" % (os.path.basename(path), imgs, w, h, bpp))); imgs += 1
        print("  image @%08x %dx%d bpp=%d size=%d extra_hdr=%s" % (p, w, h, bpp, sz, e[4:8].hex()))
    elif not is_sound and not is_img:
        others += 1
        if others <= 30:
            print("  other @%08x tag=%08x size=%7d u32=%s" % (p, tag, sz, [u32(e, i * 4) for i in range(min(10, len(e) // 4))]))
            print("           f32=%s" % [round(f32(e, i * 4), 3) for i in range(min(10, len(e) // 4))])
print("others total", others)
