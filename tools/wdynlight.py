"""Trace the dynamic light table of the ORIGINAL (docs/LIGHTING.md 7.4): boot W3D, SetVar(316, 433) + SetVar(315, 9) (class 16
fight), teleport Woody into the arena, log every 0x498790 call (caller, kind, pos, colour, radius), the slot it got
(0x4987bc: eax == count = table full), the appends of the dead list builder 0x42f0bf, and every 2 s the in-use flags.
  python tools/wdynlight.py game out/trace/dyn_w3d.txt [seconds]      (ISO mounted; INIT after ~70 s)"""
import os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtrace

GADGET = 0x41a4bd                    # add esp, 8 ; ret  (see wsetvar.py)
SETS = [(316, 433), (315, 9)]
TELE = (6400.0, -2400.0, -19918.0)

def main():
    game = sys.argv[1]
    out = open(sys.argv[2], 'w', encoding='utf-8', buffering=1)
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 150
    exe = os.path.join(game, 'Woody.exe')
    dbg = wtrace.Debugger(exe, game, out)
    st = {'init': None, 'k': 0, 'vm': 0, 'calls': 0, 'app': 0, 'tele': 0, 'lastdump': 0}
    def T(): return 'vt=%.2f' % (st['vm'] / 100.0)
    def on_tick(ctx):
        st['vm'] = dbg.u32(0x5d0514)
        if st['init'] is None: return
        el = time.perf_counter() - st['init']
        if st['k'] < len(SETS) and el >= 3 + st['k'] * 0.5:
            var, val = SETS[st['k']]; st['k'] += 1
            esp = ctx.Esp - 16
            dbg.write(esp, struct.pack('<4I', GADGET, var, val, 0x442240))
            ctx.Esp = esp; ctx.Eip = 0x443ca0
            dbg.log('%s # SetVar(%d, %d)' % (T(), var, val))
            return 'skip'
        if st['k'] == len(SETS) and st['tele'] < 30 and el >= 5:
            p = dbg.u32(0x53a34c)
            if p:
                dbg.write(p + 0x1f4, struct.pack('<3f', *TELE)); dbg.write(p + 0xc, struct.pack('<3f', *TELE)); st['tele'] += 1
        if el - st['lastdump'] >= 2.0:
            st['lastdump'] = el
            ls = dbg.u32(0x4c4cac)
            if ls:
                n = dbg.u32(ls + 0x14); recs = dbg.u32(ls + 0x18)
                act = [dbg.u32(recs + i * 0x2c) for i in range(n)]
                p = dbg.u32(0x53a34c)
                pos = struct.unpack('<3f', dbg.read(p + 0x1f4, 12)) if p else (0, 0, 0)
                dbg.log('%s table n=%d active=%s calls=%d listappends=%d woody=(%.0f %.0f %.0f)' % (T(), n, ''.join(str(a) for a in act), st['calls'], st['app'], *pos))
    def on_init(ctx):
        st['init'] = time.perf_counter(); dbg.log('INIT')
    def on_reg(ctx):
        st['calls'] += 1
        if st['calls'] <= 40 or st['calls'] % 200 == 0:
            e = ctx.Esp; ret = dbg.u32(e); kind = dbg.u32(e + 4); pp = dbg.u32(e + 8); cp = dbg.u32(e + 12)
            rad = struct.unpack('<f', dbg.read(e + 16, 4))[0]
            pos = struct.unpack('<3f', dbg.read(pp, 12)); col = struct.unpack('<3f', dbg.read(cp, 12))
            dbg.log('%s REG #%d caller %08x kind %d pos (%.0f %.0f %.0f) col (%.0f %.0f %.0f) r %.1f' % (T(), st['calls'], ret, kind, *pos, *col, rad))
    def on_regslot(ctx):
        if st['calls'] <= 40 or st['calls'] % 200 == 0:
            dbg.log('%s   slot eax=%d count=%d -> %s' % (T(), ctx.Eax, dbg.u32(ctx.Ecx + 0x14), 'FULL, returns 0' if ctx.Eax == dbg.u32(ctx.Ecx + 0x14) else 'stored'))
    def on_append(ctx):
        st['app'] += 1
        if st['app'] <= 10: dbg.log('%s LIST append light %d' % (T(), ctx.Ecx))
    def skip_logo(ctx):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4
        return 'skip'
    def on_msgbox(ctx):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 20; ctx.Eax = 1
        return 'skip'
    import pefile
    pe = pefile.PE(exe); img = pe.get_memory_mapped_image()
    table = struct.unpack_from('<28I', img, 0x4b12a0 - 0x400000)
    want = (r'\Data\W3D\W3D.gel').encode()
    match = [p for p in table if img[p - 0x400000:p - 0x400000 + len(want) + 1] == want + bytes(1)]
    def redirect(ctx): dbg.write(0x4b12a0, struct.pack('<I', match[0])); dbg.log('# level slot 0 -> W3D')
    dbg.post_arm = redirect
    dbg.pre_bps = {0x442240: on_tick, 0x4427e0: on_init, 0x446b00: skip_logo, 0x498790: on_reg, 0x4987bc: on_regslot,
                   0x42f0bf: on_append}
    dbg.iat_bps = {0x5eb5ec: on_msgbox}
    try: dbg.run(secs)
    except KeyboardInterrupt: wtrace.k32.TerminateProcess(dbg.hproc, 0)
    finally: out.flush()

if __name__ == '__main__':
    main()
