"""Watch the DirectInput mouse object of the ORIGINAL Woody.exe (docs/INPUT.md 1.3).

The mouse ([0x5e6190], ctor 0x467b70, vtable 0x4ab944) keeps a DIMOUSESTATE at +8..+0x17 that only its poll vt[1] 0x467cc0
(GetDeviceState) writes, and the look-around input 0x459346 reads lX / lY from there (vt[2] 0x467d00, vt[3] 0x467d10).
This logs: every call of the poll 0x467cc0 (there should be none), every read by 0x459346 with the values it gets, and a dump of
the object at VM init and every 5 s after.
  python tools/wmouse.py game --seconds 110 --out out/trace/mouse.txt
"""
import argparse, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtrace

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('--seconds', type=float, default=110); ap.add_argument('--out', default='-')
    a = ap.parse_args()
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    dbg = wtrace.Debugger(os.path.join(a.gamedir, 'Woody.exe'), a.gamedir, out)
    st = {'polls': 0, 'reads': 0, 'last': 0.0}

    def dump(tag):
        m = dbg.u32(0x5e6190)
        if not m: dbg.log('%s mouse [0x5e6190] = NULL' % tag); return
        vt, dev, lx, ly, lz, btn = struct.unpack('<IIiiiI', dbg.read(m, 0x18))
        dbg.log('%s mouse %08x: vtable %08x device %08x lX %d lY %d lZ %d buttons %08x (polls so far %d)' % (tag, m, vt, dev, lx, ly, lz, btn, st['polls']))
    def on_poll(ctx):
        st['polls'] += 1
        if st['polls'] <= 5: dbg.log('POLL 0x467cc0 from %08x' % dbg.u32(ctx.Esp))
    def on_look(ctx):
        st['reads'] += 1
        if st['reads'] <= 20: dump('LOOK 0x459346 read')
    def on_init(ctx): dump('INIT'); st['last'] = time.perf_counter()
    def on_tick(ctx):
        if st['last'] and time.perf_counter() - st['last'] >= 5.0: dump('TICK'); st['last'] = time.perf_counter()

    dbg.pre_bps = {0x467cc0: on_poll, 0x459346: on_look, 0x4427e0: on_init, 0x442240: on_tick}
    dbg.iat_bps = {}
    try: dbg.run(a.seconds)
    except KeyboardInterrupt: wtrace.k32.TerminateProcess(dbg.hproc, 0)
    finally: dbg.log('# polls %d, look reads %d' % (st['polls'], st['reads'])); out.flush()

if __name__ == '__main__':
    main()
