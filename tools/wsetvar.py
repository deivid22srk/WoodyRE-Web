"""Live trace of the ORIGINAL: boot a level, SetVar(var, val) at --at seconds after INIT (by calling 0x443ca0 through a
fake stack frame from the VM tick entry), then log every VM tick: Perso pos (+0x1f4), state (+0x21c), instance slot 0 anim,
camera mode index (CamMgr+0x138) and the cinematic state (Game+0x64+0x120).
  python tools/wsetvar.py game --level W2B --var 1 --val 1 --at 3 --seconds 60 --out out.txt"""
import argparse, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtrace

GADGET = 0x41a4bd   # add esp, 8 ; ret

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('--level', required=True)
    ap.add_argument('--seconds', type=float, default=60); ap.add_argument('--out', default='-')
    ap.add_argument('--var', type=int, default=1); ap.add_argument('--val', type=int, default=1); ap.add_argument('--at', type=float, default=3.0)
    ap.add_argument('--from', dest='frm', type=float, default=14.0, help='log per tick from this many s after the SetVar')
    a = ap.parse_args()
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    exe = os.path.join(a.gamedir, 'Woody.exe')
    dbg = wtrace.Debugger(exe, a.gamedir, out)
    st = {'init': None, 'set': None, 'vmtime': 0, 'last': None}
    f32 = lambda addr: struct.unpack('<f', dbg.read(addr, 4))[0]
    def T(): return 'vt=%.2f' % (st['vmtime'] / 100.0)

    def on_route(ctx):
        rec = dbg.u32(ctx.Esp + 4); b = dbg.read(rec, 48)
        if len(b) < 48: return
        w = struct.unpack('<12I', b); mid, n = w[0], min(w[1], 10)
        args = ', '.join(('0x%x' % x) if x >= 0x1000000 else str(struct.unpack('<i', struct.pack('<I', x))[0]) for x in w[2:2 + n])
        dbg.log('%s SEND %u [%s]' % (T(), mid, args))
    def on_tick(ctx):
        st['vmtime'] = dbg.u32(0x5d0514)
        if st['init'] is not None and st['set'] is None and time.perf_counter() - st['init'] >= a.at:
            st['set'] = time.perf_counter()
            esp = ctx.Esp - 16
            dbg.write(esp, struct.pack('<4I', GADGET, a.var, a.val, 0x442240))
            ctx.Esp = esp; ctx.Eip = 0x443ca0
            dbg.log('%s # SetVar(%d, %d) injected' % (T(), a.var, a.val))
            return 'skip'
        if st['set'] is not None and time.perf_counter() - st['set'] >= a.frm:
            p = dbg.u32(0x53a34c)
            if p:
                pos = struct.unpack('<3f', dbg.read(p + 0x1f4, 12)); ipos = struct.unpack('<3f', dbg.read(p + 0xc, 12))
                state = dbg.u32(p + 0x21c); slot0 = dbg.u32(p + 0xb0); cam = dbg.u32(0x4c737c)
                cmode = dbg.u32(cam + 0x138) if cam else -1
                line = 'pos %.0f %.0f %.0f inst %.0f %.0f %.0f state %d anim %d cam %d onground %d' % (pos + ipos + (state, slot0, cmode, dbg.read(p + 0x22c, 1)[0]))
                if line != st['last']: dbg.log('%s %s' % (T(), line)); st['last'] = line
    def on_init(ctx):
        st['init'] = time.perf_counter(); dbg.log('INIT')
    def on_cin_end(ctx): dbg.log('%s CIN end 0x44edb0' % T())
    def on_setpos(ctx):
        v = struct.unpack('<3f', dbg.read(dbg.u32(ctx.Esp + 4), 12)); dbg.log('%s SetPos 0x44a650 (%.1f %.1f %.1f) caller %08x' % (T(), v[0], v[1], v[2], dbg.u32(ctx.Esp)))
    def on_snap_after(ctx):
        p = dbg.u32(0x53a34c); v = struct.unpack('<3f', dbg.read(p + 0x1f4, 12)); dbg.log('%s  after snap pos %.1f %.1f %.1f  GetHeight type %d y %.1f' % (T(), v[0], v[1], v[2], dbg.u32(0x53a554), f32(0x53a568)))
    def skip_logo(ctx):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4
        return 'skip'
    def on_msgbox(ctx):
        dbg.log('# MessageBoxA %r -> auto OK' % dbg.cstr(dbg.u32(ctx.Esp + 8)))
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 20; ctx.Eax = 1
        return 'skip'

    import pefile
    pe = pefile.PE(exe); img = pe.get_memory_mapped_image()
    table = struct.unpack_from('<28I', img, 0x4b12a0 - 0x400000)
    want = (r'\Data\%s\%s.gel' % (a.level, a.level)).encode()
    match = [p for p in table if img[p - 0x400000:p - 0x400000 + len(want) + 1] == want + bytes(1)]
    if not match: sys.exit('unknown level %s' % a.level)
    def redirect(ctx): dbg.write(0x4b12a0, struct.pack('<I', match[0])); dbg.log('# level slot 0 -> %s' % a.level)
    dbg.post_arm = redirect
    dbg.pre_bps = {0x401370: on_route, 0x442240: on_tick, 0x4427e0: on_init, 0x446b00: skip_logo, 0x44edb0: on_cin_end,
                   0x44a650: on_setpos, 0x445b46: on_snap_after}
    dbg.iat_bps = {0x5eb5ec: on_msgbox}
    try: dbg.run(a.seconds)
    except KeyboardInterrupt: wtrace.k32.TerminateProcess(dbg.hproc, 0)
    finally: out.flush()

if __name__ == '__main__':
    main()
