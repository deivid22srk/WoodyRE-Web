import struct, sys
from collections import Counter
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
d = open(sys.argv[1], 'rb').read()
print("size", len(d))
print("header u32:", [u32(d, i * 4) for i in range(2, 24)])
nobj = u32(d, 8)
# hypothesis: after 'EKO CODE'(8) + nobj(4) + 12(4) comes a table of nobj entries
for entsize in (4, 8, 12, 16):
    tbl = [u32(d, 16 + i * 4) for i in range(min(nobj * entsize // 4, 64))]
    print("entsize", entsize, "first entries:", tbl[:entsize * 6 // 4])
# Look for monotonically increasing offsets
vals = [u32(d, 16 + i * 4) for i in range(nobj * 4)]
for stride in (1, 2, 3, 4):
    for off in range(stride):
        seq = vals[off::stride][:nobj]
        mono = all(seq[i] <= seq[i + 1] for i in range(len(seq) - 1))
        if mono and seq[-1] < len(d):
            print("monotonic table: stride", stride, "offset", off, "range", seq[0], "..", seq[-1], "sample", seq[:10])
# byte histogram of the tail (bytecode region) to see opcode density
tail = d[len(d) // 2:]
print("byte histogram tail (top 24):", Counter(tail).most_common(24))
print("u16 histogram tail (top 16):", Counter(struct.unpack('<%dH' % (len(tail) // 2), tail[:len(tail) // 2 * 2])).most_common(16))
