"""rckexport.py - dump the contents of RKET banks: sounds as WAV, images as PNG, strings as glyph-index lists.
usage: python tools/rckexport.py <bank.rck> [<outdir>]   (default outdir out/rck/<bankname>)"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from rckparse import parse_rck

def write_wav(path, pcm, rate, bits, channels):
    bps = channels * bits // 8
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * bps, bps, bits))
        f.write(b'data' + struct.pack('<I', len(pcm)) + pcm)

def main():
    src = sys.argv[1]
    name = os.path.splitext(os.path.basename(src))[0]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join('out', 'rck', name)
    os.makedirs(out, exist_ok=True)
    r = parse_rck(open(src, 'rb').read())
    for i, s in enumerate(r['items']['sound']):
        write_wav(os.path.join(out, 'sound_%03d.wav' % i), s['pcm'], s['rate'], s['bits'], s['channels'])
    from PIL import Image
    for i, im in enumerate(r['items']['image']):
        img = Image.frombytes('RGBA', (im['w'], im['h']), im['pixels'], 'raw', 'BGRA').transpose(Image.FLIP_TOP_BOTTOM)  # rows are stored bottom-up
        if im['alpha']:   # colour-keyed: magenta -> transparent
            px = img.load()
            for y in range(im['h']):
                for x in range(im['w']):
                    p = px[x, y]
                    if p[0] > 240 and p[2] > 240 and p[1] < 16: px[x, y] = (0, 0, 0, 0)
        img.save(os.path.join(out, 'image_%03d_%dx%d%s.png' % (i, im['w'], im['h'], '_a' if im['alpha'] else '')))
    with open(os.path.join(out, 'strings.txt'), 'w') as f:
        for i, s in enumerate(r['items']['string']): f.write('%3d: %s\n' % (i, ' '.join('%02x' % g for g in s['glyphs'])))
    for i, fo in enumerate(r['items']['font']):
        open(os.path.join(out, 'font_%d.bin' % i), 'wb').write(fo['raw'])
    print('%s -> %s: %d sounds, %d images, %d strings, %d fonts' % (src, out, len(r['items']['sound']), len(r['items']['image']), len(r['items']['string']), len(r['items']['font'])))

if __name__ == '__main__':
    main()
