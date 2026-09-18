import struct, sys
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
d = open(sys.argv[1], 'rb').read()
nf, nidx, v3, n4 = [u32(d, i * 4) for i in range(4)]
print("faces", nf, "indices", nidx, "v", v3, "n4", n4)
# verify face records of 9 dwords
pos = 16
maxidx = 0; bad = 0; nv_set = set(); seq_ok = True; mats = set()
for i in range(nf):
    idx = [u32(d, pos + 16 + 4 * k) for k in range(3)]
    nv = u32(d, pos + 28); sq = u32(d, pos + 32)
    nv_set.add(nv); maxidx = max(maxidx, *idx)
    if sq != i + 1: seq_ok = False
    pos += 36
print("after faces pos", hex(pos), "max vertex index", maxidx, "nv values", nv_set, "seq field == i+1:", seq_ok)
def row(o, n=16): return " ".join("%08x" % u32(d, o + 4 * i) for i in range(n))
def frow(o, n=16): return " ".join("%9.3f" % f32(d, o + 4 * i) for i in range(n))
for o in range(pos, pos + 0x180, 64):
    print("%06x: %s" % (o, row(o))); print("        %s" % frow(o))
# guess: vertex count dword then vertices
