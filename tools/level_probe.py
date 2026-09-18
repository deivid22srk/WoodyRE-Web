import struct, sys, os
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
i32 = lambda b, o: struct.unpack_from('<i', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
root = sys.argv[1]
lvl = sys.argv[2] if len(sys.argv) > 2 else 'W1A'
base = os.path.join(root, 'Data', lvl, lvl)

def dump(name, n=24):
    d = open(base + name, 'rb').read()
    print("=== %s size=%d ===" % (name, len(d)))
    print(" u32:", [u32(d, i * 4) for i in range(n)])
    print(" f32:", [round(f32(d, i * 4), 3) for i in range(n)])
    print(" u16:", [u16(d, i * 2) for i in range(n * 2)])
    return d

d = dump('.tex')
n = u32(d, 0)
print("tex count?", n)
# Try to find the header size: assume header dwords then w*h*2 bytes of 16-bit texels, then next texture starts with plausible w,h
for hdr in range(3, 12):
    pos = 4
    ok = True
    dims = []
    for i in range(n):
        if pos + hdr * 4 > len(d):
            ok = False; break
        w, h = u32(d, pos), u32(d, pos + 4)
        if w not in (8, 16, 32, 64, 128, 256, 512) or h not in (8, 16, 32, 64, 128, 256, 512):
            ok = False; break
        dims.append((w, h))
        pos += hdr * 4 + w * h * 2
    if ok:
        print("  header of %d dwords fits: end pos %d of %d, dims sample %s" % (hdr, pos, len(d), dims[:8]))

d = dump('.vis')
d = dump('.col')
d = dump('.lit')
d = dump('.gel')
d = dump('.ins')
