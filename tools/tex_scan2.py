import struct, sys
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
d = open(sys.argv[1], 'rb').read()
POW2 = {8, 16, 32, 64, 128, 256, 512, 1024}
hits = []
for o in range(4, len(d) - 12, 2):
    a, b, c = u32(d, o), u32(d, o + 4), u32(d, o + 8)
    if a in POW2 and b in POW2 and c == a:
        hits.append((o, a, b, d[o + 12:o + 44].hex()))
print("triple hits:", len(hits), "declared:", u32(d, 0))
prev = None
for h in hits[:14]:
    o, a, b, hx = h
    if prev:
        gap = o - prev[0]; top = prev[1] * prev[2] * 2
        print("  @%07x %4dx%-4d gap=%d top=%d gap-40-top=%d  hdr=%s" % (o, a, b, gap, top, gap - 40 - top, hx))
    else:
        print("  @%07x %4dx%-4d hdr=%s" % (o, a, b, hx))
    prev = h
