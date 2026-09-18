import struct, sys
from collections import Counter
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
d = open(sys.argv[1], 'rb').read()
print("file", sys.argv[1], "size", len(d))
print("hdr:", [u32(d, i * 4) for i in range(16)])
pos = 0x3c
kinds = Counter(); n = 0; shown = 0
while pos + 4 <= len(d) and n < 100000:
    sz = u32(d, pos)
    if sz == 0 or pos + 4 + sz > len(d):
        print("stop at", hex(pos), "sz", sz); break
    e = d[pos + 4:pos + 4 + sz]
    h = [u32(e, i * 4) for i in range(min(8, len(e) // 4))]
    # classify
    if len(e) >= 16 and u32(e, 4) == 0 and u32(e, 8) in (11025, 22050, 44100):
        kind = "sound %dHz %dbit ch%d len=%d" % (u32(e, 8), u32(e, 16) if len(e) > 16 else 0, u32(e, 24) if len(e) > 24 else 0, u32(e, 0))
        kinds['sound'] += 1
    elif len(e) >= 12 and u16(e, 0) in (8, 16, 32, 64, 128, 256, 512) and u16(e, 2) in (8, 16, 32, 64, 128, 256, 512):
        kind = "image %dx%d bpp?=%d" % (u16(e, 0), u16(e, 2), u16(e, 4)); kinds['image'] += 1
    else:
        kind = "other"; kinds['other'] += 1
    if shown < 40 or kind == 'other' and shown < 80:
        print("  @%08x size=%8d %s  head=%s" % (pos, sz, kind, h[:6])); shown += 1
    pos += 4 + sz; n += 1
print("entries", n, kinds, "end pos", hex(pos), "of", hex(len(d)))
