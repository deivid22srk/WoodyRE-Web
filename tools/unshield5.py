# Minimal InstallShield 5.x (ISc( v5) .hdr/.cab extractor, based on the unshield format description.
import struct, sys, os, zlib
hdr = open(sys.argv[1], 'rb').read()
cab = open(sys.argv[2], 'rb').read()
out = sys.argv[3]
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
assert hdr[:4] == b'ISc('
version = u32(hdr, 4); cdo = u32(hdr, 0xc); cds = u32(hdr, 0x10)
major = (version >> 12) & 0xf if (version >> 24) == 1 else version >> 12
print("version=%08x major=%d cab_descriptor_offset=%x size=%x" % (version, major, cdo, cds))
p = cdo + 0xc
fto = u32(hdr, p); fts = u32(hdr, p + 8); fts2 = u32(hdr, p + 12); dircount = u32(hdr, p + 16)
filecount = u32(hdr, p + 0x1c); fto2 = u32(hdr, p + 0x20)
print("file_table_offset=%x dirs=%d files=%d" % (fto, dircount, filecount))
ft = cdo + fto
table = [u32(hdr, ft + 4 * i) for i in range(dircount + filecount)]
cstr = lambda o: hdr[o:hdr.index(b'\0', o)].decode('latin1')
dirs = [cstr(ft + table[i]) for i in range(dircount)]
print("dirs:", dirs)
for i in range(filecount):
    p = ft + table[dircount + i]
    name_off = u32(hdr, p); diridx = u32(hdr, p + 4); flags = u16(hdr, p + 8)
    exp = u32(hdr, p + 10); comp = u32(hdr, p + 14); data_off = u32(hdr, p + 0x26)
    name = cstr(ft + name_off)
    d = dirs[diridx] if diridx < len(dirs) else '?'
    print("%-24s dir=%-14s flags=%04x exp=%9d comp=%9d off=%08x" % (name, d, flags, exp, comp, data_off))
    if flags & 8:
        print("   (invalid/skipped)"); continue
    data = cab[data_off:data_off + comp]
    result = b''
    if flags & 4:
        try:
            pos = 0; chunks = []
            while pos < len(data):
                n = u16(data, pos); pos += 2
                chunks.append(zlib.decompressobj(-15).decompress(data[pos:pos + n])); pos += n
            result = b''.join(chunks)
        except Exception as e:
            try:
                result = zlib.decompressobj().decompress(data)
            except Exception as e2:
                print("   FAILED:", e, e2); continue
    else:
        result = data
    if len(result) != exp:
        print("   size mismatch: got %d expected %d" % (len(result), exp))
    sub = d.replace('<TARGETDIR>', '').strip('\\/').replace('\\', '/')
    od = os.path.join(out, sub)
    os.makedirs(od, exist_ok=True)
    open(os.path.join(od, name), 'wb').write(result)
