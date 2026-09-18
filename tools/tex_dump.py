import struct, sys, os
from PIL import Image
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
f32 = lambda b, o: struct.unpack_from('<f', b, o)[0]
path, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
d = open(path, 'rb').read()
n = u32(d, 0)
pos = 4
print("textures:", n, "file size", len(d))
POW2 = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024}
saved = 0
for i in range(n):
    w, h, c = u32(d, pos), u32(d, pos + 4), u32(d, pos + 8)
    hdr = [u32(d, pos + 4 * k) for k in range(3, 10)]
    fl = [round(f32(d, pos + 4 * k), 3) for k in range(3, 10)]
    if w not in POW2 or h not in POW2:
        print("  desync at texture", i, "pos", hex(pos), "w,h=", w, h); break
    # count mip levels: sum of w*h*2 over levels down to 1x1?
    data_start = pos + 40
    # Try to detect next header: find how many bytes until a plausible (w2,h2,w2) triple where w2,h2 in POW2 and third==w2
    candidates = []
    total_full = 0
    mw, mh = w, h
    levels = []
    while True:
        levels.append((mw, mh)); total_full += mw * mh * 2
        cand = data_start + total_full
        if cand + 12 <= len(d):
            a, b, c2 = u32(d, cand), u32(d, cand + 4), u32(d, cand + 8)
            if a in POW2 and b in POW2 and c2 == a:
                candidates.append((len(levels), cand))
        if mw == 1 and mh == 1: break
        mw = max(1, mw // 2); mh = max(1, mh // 2)
    if i == n - 1:
        # last texture: match against end of file
        for k in range(1, len(levels) + 1):
            if data_start + sum(a * b * 2 for a, b in levels[:k]) == len(d):
                candidates = [(k, len(d))]
    if not candidates:
        print("  no candidate end for texture", i, "at", hex(pos), "w,h,c", w, h, c, hdr); break
    nlev, nxt = candidates[0]
    if i < 5 or i % 25 == 0:
        print("  tex %3d @%07x %4dx%-4d c=%d hdr=%s f=%s mips=%d" % (i, pos, w, h, c, hdr, fl, nlev))
    # decode top level as RGB565 and as ARGB1555/4444 for comparison
    px = d[data_start:data_start + w * h * 2]
    if saved < 12:
        vals = struct.unpack('<%dH' % (w * h), px)
        for mode in ('565', '1555', '4444'):
            img = Image.new('RGBA', (w, h))
            out = []
            for v in vals:
                if mode == '565':
                    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
                    out.append((r * 255 // 31, g * 255 // 63, b * 255 // 31, 255))
                elif mode == '1555':
                    a = (v >> 15) & 1; r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31
                    out.append((r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if a else 0))
                else:
                    a = (v >> 12) & 15; r = (v >> 8) & 15; g = (v >> 4) & 15; b = v & 15
                    out.append((r * 17, g * 17, b * 17, a * 17))
            img.putdata(out)
            img.save(os.path.join(outdir, "tex%03d_%s.png" % (i, mode)))
        saved += 1
    pos = nxt
print("parsed", i + 1, "textures; end pos", hex(pos), "of", hex(len(d)))
