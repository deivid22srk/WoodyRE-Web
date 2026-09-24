"""Trace the Game iris / transition state of the ORIGINAL Woody.exe (docs/PERSO_FRAME.md 4.1).

Boots straight into a level (wtrace --level), optionally teleports the Perso after the level has run for a while
(--pos x y z --at seconds after INIT, writes Perso+0x1f4 and the pre-move position +0x28c), and logs:
  GAME ctor 0x445850, respawn 0x445930, life lost 0x44c730, every iris set 0x4776b0 (from, to, dur, caller),
  every change of Game+0x14 seen by 0x4459c0, iris draws 0x4776d0 NOT coming from the Game iris (menu panels),
  and all routed script messages (SEND, as wtrace).
  python tools/wiris.py game --level W1B --pos -7191 1400 -8939 --at 8 --seconds 200 --out out/trace/iris_W1B.txt
"""
import argparse, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtrace

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('--level', required=True)
    ap.add_argument('--seconds', type=float, default=200); ap.add_argument('--out', default='-')
    ap.add_argument('--pos', type=float, nargs=3); ap.add_argument('--at', type=float, default=8.0)
    a = ap.parse_args()
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    exe = os.path.join(a.gamedir, 'Woody.exe')
    dbg = wtrace.Debugger(exe, a.gamedir, out)
    st = {'init': None, 'moved': False, 'gstate': None, 'vmtime': 0}
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
        if a.pos and st['init'] is not None and not st['moved'] and time.perf_counter() - st['init'] >= a.at:
            p = dbg.u32(0x53a34c)
            if p:
                v = struct.pack('<3f', *a.pos); dbg.write(p + 0x1f4, v); dbg.write(p + 0x28c, v); st['moved'] = True
                dbg.log('%s # Perso %08x teleported to %s' % (T(), p, a.pos))
    def on_init(ctx):
        st['init'] = time.perf_counter(); dbg.log('INIT')
    def on_iris_set(ctx):
        fr, to, du = struct.unpack('<3f', dbg.read(ctx.Esp + 4, 12))
        dbg.log('%s IRIS set %.2f -> %.2f in %.2f s (this %08x, caller %08x)' % (T(), fr, to, du, ctx.Ecx, dbg.u32(ctx.Esp)))
    def on_iris_draw(ctx):
        ret = dbg.u32(ctx.Esp)
        if ret != 0x47795e: dbg.log('%s IRIS draw %.3f from caller %08x' % (T(), f32(ctx.Esp + 4), ret))
    def on_game_tick(ctx):
        s = dbg.u32(ctx.Ecx + 0x14)
        if s != st['gstate']:
            st['gstate'] = s; dbg.log('%s GAME state %d (timer %.2f)' % (T(), s, f32(ctx.Ecx + 0xc)))
    def on_ctor(ctx): dbg.log('%s GAME ctor' % T())
    def on_respawn(ctx): dbg.log('%s GAME respawn 0x445930 (caller %08x)' % (T(), dbg.u32(ctx.Esp)))
    def on_life(ctx): dbg.log('%s PERSO life lost 0x44c730' % T())
    def skip_logo(ctx):           # 0x446b00 draws the title logo = level bank image 1, which only House has: a non-House boot crashes on it
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
    dbg.pre_bps = {0x401370: on_route, 0x442240: on_tick, 0x4427e0: on_init, 0x4776b0: on_iris_set, 0x4776d0: on_iris_draw,
                   0x4459c0: on_game_tick, 0x445850: on_ctor, 0x445930: on_respawn, 0x44c730: on_life, 0x446b00: skip_logo}
    dbg.iat_bps = {0x5eb5ec: on_msgbox}
    try: dbg.run(a.seconds)
    except KeyboardInterrupt: wtrace.k32.TerminateProcess(dbg.hproc, 0)
    finally: out.flush()

if __name__ == '__main__':
    main()
