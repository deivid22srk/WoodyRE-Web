import struct, sys
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
d = open(sys.argv[1], 'rb').read()
POW2 = {8, 16, 32, 64, 128, 256, 512, 1024}
hits = []
for o in range(4, len(d) - 12, 4):
    a, b, c = u32(d, o), u32(d, o + 4), u32(d, o + 8)
    if a in POW2 and b in POW2 and c == a and u32(d, o + 12) == 0x00FF0020:
        hits.append((o, a, b))
print("header hits:", len(hits), "declared:", u32(d, 0))
prev = None
for o, a, b in hits[:12] + hits[-3:]:
    gap = None if prev is None else o - prev[0]
    exp = None if prev is None else prev[1] * prev[2] * 2
    print("  @%07x %4dx%-4d  gap_from_prev=%s  top_level_bytes=%s  ratio=%s" % (o, a, b, gap, exp, (None if not gap else round((gap - 40) / exp, 4))))
    prev = (o, a, b)
print("raw header 0 bytes:", d[4:44].hex())
print("raw header 1 bytes:", d[hits[1][0]:hits[1][0] + 40].hex() if len(hits) > 1 else None)
