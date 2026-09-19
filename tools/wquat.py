"""wquat.py - log the original's quaternion->matrix conversions (0x440370) to settle the
rotation convention: input quaternion, caller and resulting 3x3 matrix (row-major, 9 floats).
usage: python tools/wquat.py game [--seconds 130] [--max 60] [--level W1A]
"""
import sys, os, struct, argparse
sys.path.insert(0, os.path.dirname(__file__))
import wtrace
from wtrace import Debugger, k32

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('gamedir'); ap.add_argument('--seconds', type=float, default=130)
    ap.add_argument('--max', type=int, default=60); ap.add_argument('--out', default='-'); ap.add_argument('--level')
    ap.add_argument('--caller', type=lambda v: int(v, 16), help='only log calls from this call site (hex), e.g. 43a472 = pose evaluation')
    a = ap.parse_args()
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    exe = os.path.join(a.gamedir, 'Woody.exe')
    dbg = Debugger(exe, a.gamedir, out)
    state = {'n': 0, 'pending': {}}
    def on_ret(ctx):
        info = state['pending'].pop(ctx.Eip - 1, None)   # Eip is past the int3
        if not info: return
        q, outp, caller = info
        m = struct.unpack('<9f', dbg.read(outp, 36))
        dbg.log('Q caller=%08x q=(%.5f %.5f %.5f %.5f) M=[%s]' % (caller, *q, ' '.join('%.4f' % v for v in m)))
        state['n'] += 1
        if state['n'] >= a.max: dbg.log('# done'); k32.TerminateProcess(dbg.hproc, 0)
    def on_quat(ctx):
        ret = dbg.u32(ctx.Esp); qp = dbg.u32(ctx.Esp + 4); outp = dbg.u32(ctx.Esp + 8)
        q = struct.unpack('<4f', dbg.read(qp, 16))
        if a.caller and ret - 5 != a.caller: return
        state['pending'][ret] = (q, outp, ret - 5)
        if ret not in dbg.bps: dbg.add_bp(ret, on_ret)
    def on_load(ctx): dbg.log('LOAD %s' % dbg.cstr(dbg.u32(ctx.Esp + 4)))
    def on_msgbox(ctx):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4 + 16; ctx.Eax = 1; return 'skip'
    dbg.pre_bps = {0x440370: on_quat, 0x4424b0: on_load}
    dbg.iat_bps = {0x5eb5ec: on_msgbox}
    if a.level:
        import pefile
        pe = pefile.PE(exe); img = pe.get_memory_mapped_image()
        table = struct.unpack_from('<28I', img, 0x4b12a0 - 0x400000)
        want = (r'\Data\%s\%s.gel' % (a.level, a.level)).encode()
        match = [p for p in table if img[p - 0x400000:p - 0x400000 + len(want) + 1] == want + bytes(1)]
        if not match: sys.exit('unknown level %s' % a.level)
        dbg.post_arm = lambda ctx: dbg.write(0x4b12a0, struct.pack('<I', match[0]))
    try: dbg.run(a.seconds)
    except KeyboardInterrupt: k32.TerminateProcess(dbg.hproc, 0)

if __name__ == '__main__': main()
