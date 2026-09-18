"""rckparse.py - parser for the "RKET" resource banks (Common\\*.rck, Data\\<LVL>\\<LVL>.rck).

Layout (from the loader at 0x441230 in Woody.exe, see docs/RCK.md):
  0x00  char[4] 'RKET'
  0x04  u32     version (must be 0)
  0x08  u8[0x30] bank header: +0 u32 build stamp, +5 u8 bank (0 = Common/character bank, !=0 -> slot 1 = level bank),
                 +0x18 u32 count[4] = number of items per resource type (0 sound, 1 image, 2 string, 3 font)
  0x38  items, in type order: [u32 size][u32 reserved][size bytes]

Item payloads (per registered type loader):
  sound  (0x4687a0 -> sound manager vtable+0x70): u32 nbytes, u32 rate, u32 bits, u32 channels, PCM
  image  (0x47f910 / 0x480780): i16 w, i16 h, u16 ?, u16 alpha(1 = keyed/alpha surface), pixels (32-bit BGRA)
  string (0x43f570 / 0x43f440): u16 glyph indices (copied verbatim into a string pool)
  font   (0x43fc90 / 0x43f9a0): 100-byte (0x64) font record, see levelparse/docs

usage: python tools/rckparse.py extract/Common/Woody.rck [more.rck ...]
"""
import struct, sys, glob, os

TYPES = ('sound', 'image', 'string', 'font')

def parse_rck(d: bytes) -> dict:
    assert d[:4] == b'RKET', 'not an RKET file'
    version = struct.unpack_from('<I', d, 4)[0]
    assert version == 0, 'unsupported version %d' % version
    hdr = d[8:0x38]
    bank_byte = hdr[5]
    counts = struct.unpack_from('<4I', hdr, 0x18)
    pos = 0x38
    items = {t: [] for t in TYPES}
    for t, n in zip(TYPES, counts):
        for _ in range(n):
            size, reserved = struct.unpack_from('<II', d, pos)
            payload = d[pos + 8:pos + 8 + size]
            assert len(payload) == size, 'truncated item'
            items[t].append(decode_item(t, payload, reserved))
            pos += 8 + size
    assert pos == len(d), 'trailing bytes: %d' % (len(d) - pos)
    return {'stamp': struct.unpack_from('<I', hdr, 0)[0], 'bank': 0 if bank_byte == 0 else 1,
            'counts': dict(zip(TYPES, counts)), 'items': items}

def decode_item(t, p, reserved):
    if t == 'sound':
        nbytes, rate, bits, channels = struct.unpack_from('<4I', p, 0)
        return {'rate': rate, 'bits': bits, 'channels': channels, 'pcm': p[16:16 + nbytes], 'reserved': reserved}
    if t == 'image':
        w, h, u, alpha = struct.unpack_from('<hhHH', p, 0)
        return {'w': w, 'h': h, 'unknown': u, 'alpha': alpha, 'pixels': p[8:], 'reserved': reserved}
    if t == 'string':
        return {'glyphs': list(struct.unpack_from('<%dH' % (len(p) // 2), p, 0)), 'reserved': reserved}
    return {'raw': p, 'reserved': reserved}

def resource_ref(bank, typ, index):
    """Encoding used by the engine (RckGet at 0x441580): u16 index | u8 type << 16 | u8 bank << 24."""
    return (bank << 24) | (typ << 16) | index

if __name__ == '__main__':
    paths = sys.argv[1:] or sorted(glob.glob('extract/Common/*.rck') + glob.glob('extract/Data/*/*.rck'))
    for path in paths:
        r = parse_rck(open(path, 'rb').read())
        snd = r['items']['sound']; img = r['items']['image']; strs = r['items']['string']
        rates = sorted(set(s['rate'] for s in snd))
        sizes = sorted(set((i['w'], i['h']) for i in img))
        print('%-14s bank=%d sounds=%3d (rates %s) images=%3d strings=%3d fonts=%d  img sizes %s%s' % (
            os.path.basename(path), r['bank'], len(snd), rates, len(img), len(strs), len(r['items']['font']),
            sizes[:6], '...' if len(sizes) > 6 else ''))
