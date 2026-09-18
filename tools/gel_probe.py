import struct, sys
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
d = open(sys.argv[1], 'rb').read()
print("size", len(d))
def row(o, n=16):
    return " ".join("%08x" % u32(d, o + 4 * i) for i in range(n))
def frow(o, n=16):
    return " ".join("%9.3f" % f32(d, o + 4 * i) for i in range(n))
print("--- first 0x200 bytes as u32/f32 ---")
for o in range(0, 0x200, 64):
    print("%06x: %s" % (o, row(o)))
    print("        %s" % frow(o))
# find long runs of plausible floats (vertex arrays)
def plausible(v):
    return v == 0.0 or (1e-3 < abs(v) < 5e5)
runs = []; start = None; cnt = 0
for o in range(0, len(d) - 4, 4):
    v = f32(d, o)
    if plausible(v) and not (u32(d, o) < 0x10000 and u32(d, o) != 0):
        if start is None: start = o
        cnt += 1
    else:
        if start is not None and cnt >= 300: runs.append((start, cnt))
        start = None; cnt = 0
if start is not None and cnt >= 300: runs.append((start, cnt))
print("--- float runs (offset, dwords) ---")
for s, c in runs[:40]:
    print("  @%07x %7d dwords (%d bytes)  sample: %s" % (s, c, c * 4, frow(s, 9)))
print("total runs", len(runs))
