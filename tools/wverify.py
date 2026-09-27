"""wverify.py - check statically decompiled claims against the RUNNING original (Woody.exe under tools/wtrace.py's debugger).

Common options (every probe):
  --level LVL          boot straight into LVL (House slot 0 of 0x4b12a0 redirected, title logo 0x446b00 skipped);
                       without it the game boots to the title on House (menu probes)
  --pos x y z --at T   teleport the Perso T s after INIT (Perso+0x1f4 and +0x28c, as tools/wiris.py)
  --keys "T:KEY[:D] ..." synthetic keyboard: the DirectInput poll 0x467ef0 is replaced (so the real keyboard is dead
                       and the window needs no focus); KEY is held from T s after INIT for D s (default 0.1).
                       KEY = RET ESC UP DOWN LEFT RIGHT JUMP (LCtrl) ATTACK (LShift) DUCK (Space) or a hex internal code.
  --seconds N          wall-clock limit (the boot to INIT takes ~70 s)
  --sav FILE           CreateFileA of "Woody.sav" is redirected to FILE (game/ has no save; a copy of the port's woodyre.sav,
                       which has the original layout, lets the Load game chain reach the carousel without touching game/)

Probes (--probe, several allowed, comma separated):
  list      the per-frame instance list world+0x64 (0x42a980 -> 0x42a840, docs/INSTANCE.md 4.1): at the entry of 0x42a980
            (= before this frame's rebuild) print the previous frame's list as .ins slot ids, --frames frames from --from s
  fpu       the x87 control word at the camera sweep's inline fistp 0x439cb3 (docs/CAMERA.md 3.6) and the step count
            [0x5ac8ac] it produces, plus the control word at the frame's VM tick
  carousel  world-select carousel class 110 (docs/MENU_LOAD.md 4.4): args of 0x489780, the local basis inst+0x14c it builds,
            the camera matrix [0x5e86ac]+0x154 and the instance rows 0x489210 writes, for slots 105..114
  rocket    rideable rocket (docs/ROCKET.md): per frame state/t/rot rows/quaternions of the class-20 instance --inst
  cam       per frame Perso pos/state + camera mode index/position (CamMgr+0x138/+0x1d0) and the rail point (+0x3e8)
  blackbox  the BlackBox mini game (docs/BLACKBOX.md): with --level BlackBox, patches 0x4042c9 so the load creates the object
            and sets App state 3 at --at; logs the round / Woody / Buzz / pool per 0x4846d0 call; --bbpos teleports Woody
  crush     the crush test 0x462a40: every squash < 1 (0x462bd4) and every Kill(4) call (0x462bed)
  python tools/wverify.py game --level W1A --probe list,fpu --from 15 --frames 12 --seconds 110 --out out/trace/v_list.txt
"""
import argparse, ctypes, math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtrace

KEYS = {'ESC': 0x00, 'RET': 0x1b, 'UP': 0x78, 'DOWN': 0x7d, 'LEFT': 0x7a, 'RIGHT': 0x7b,
        'JUMP': 0x1c, 'ATTACK': 0x29, 'DUCK': 0x38}           # internal codes = table 0x4b6f88 (DIK - 1 below 0x54)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('--level'); ap.add_argument('--probe', default='list')
    ap.add_argument('--seconds', type=float, default=120); ap.add_argument('--out', default='-')
    ap.add_argument('--pos', type=float, nargs=3); ap.add_argument('--at', type=float, default=5.0)
    ap.add_argument('--keys', default=''); ap.add_argument('--from', dest='frm', type=float, default=10.0)
    ap.add_argument('--frames', type=int, default=12); ap.add_argument('--list-from', dest='lfrom', type=float, help='list probe start (default --from)'); ap.add_argument('--inst', type=int, default=323)
    ap.add_argument('--until', type=float, default=1e9, help='stop per-frame logging this many s after INIT')
    ap.add_argument('--sav'); ap.add_argument('--every', type=float, default=0, help='cam/rocket probes: at most one line per this many s')
    ap.add_argument('--bbpos', default='', help='blackbox probe: "T x y [T x y ...]" puts Woody on x y T s after INIT')
    ap.add_argument('--onto', type=float, nargs=2, help='INST DY: teleport at --at onto the animated root inst+0x60 of that instance, DY above it')
    a = ap.parse_args()
    probes = set(a.probe.split(','))
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    exe = os.path.join(a.gamedir, 'Woody.exe')
    dbg = wtrace.Debugger(exe, a.gamedir, out)
    st = {'init': None, 'moved': False, 'vmtime': 0, 'nlist': 0, 'objs': None, 'car': {}, 'fpu': 0, 'last': None}
    f32 = lambda addr: struct.unpack('<f', dbg.read(addr, 4))[0]
    fv = lambda addr, n: struct.unpack('<%df' % n, dbg.read(addr, 4 * n))
    def since(): return time.perf_counter() - st['init'] if st['init'] is not None else -1.0
    def T(): return 't=%.3f vt=%.2f fr=%d' % (since(), st['vmtime'] / 100.0, frame())
    def frame():
        w = dbg.u32(0x509adc); return dbg.u32(w) if w else -1
    keys = []
    for tok in a.keys.split():
        p = tok.split(':'); code = KEYS[p[1]] if p[1] in KEYS else int(p[1], 16)
        keys.append((float(p[0]), code, float(p[2]) if len(p) > 2 else 0.1))

    def objmap():                              # .ins object table [[0x4c4c0c]+0x40]: Instance* -> slot id
        kd = dbg.u32(0x4c4c0c); tab = dbg.u32(kd + 0x40) if kd else 0
        if not tab: return {}
        raw = dbg.read(tab, 4 * 4096); n = len(raw) // 4; m = {}
        for i, p in enumerate(struct.unpack('<%dI' % n, raw)):
            if p and p not in m: m[p] = i
        return m
    def slot(p):
        if st['objs'] is None or p not in st['objs']: st['objs'] = objmap()
        return st['objs'].get(p, '?%08x' % p)
    def inst_ptr(i):
        kd = dbg.u32(0x4c4c0c); tab = dbg.u32(kd + 0x40) if kd else 0
        return dbg.u32(tab + 4 * i) if tab else 0

    # --- keyboard: replace the poll 0x467ef0 (thiscall, no args): state[i] at kbd+4+i
    def on_kbpoll(ctx):
        t = since(); buf = bytearray(0x100)
        if st['init'] is not None:
            for (k0, code, d) in keys:
                if k0 <= t < k0 + d: buf[code] = 1
        dbg.write(ctx.Ecx + 4, bytes(buf))
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4
        return 'skip'
    def on_route(ctx):
        rec = dbg.u32(ctx.Esp + 4); b = dbg.read(rec, 48)
        if len(b) < 48: return
        w = struct.unpack('<12I', b); mid, n = w[0], min(w[1], 10)
        args = ', '.join(('0x%x' % x) if x >= 0x1000000 else str(struct.unpack('<i', struct.pack('<I', x))[0]) for x in w[2:2 + n])
        dbg.log('%s SEND %u [%s]' % (T(), mid, args))
    def on_tick(ctx):
        st['vmtime'] = dbg.u32(0x5d0514)
        if (a.pos or a.onto) and st['init'] is not None and not st['moved'] and since() >= a.at:
            p = dbg.u32(0x53a34c)
            if p:
                tgt = a.pos; done = True
                if a.onto:                     # the animated root is only valid once the instance has been clocked (listed): go to its
                    e = inst_ptr(int(a.onto[0])); r = fv(e + 0x60, 3)   # .ins position first, so the camera sees it, then onto the root
                    if r == (0.0, 0.0, 0.0) or not st.get('near'):
                        q = fv(e + 0xc, 3); tgt = (q[0], q[1] + a.onto[1], q[2]); done = False; st['near'] = st.get('near', 0) + 1
                        if st['near'] > 1: return
                    else: tgt = (r[0], r[1] + a.onto[1], r[2])
                v = struct.pack('<3f', *tgt); dbg.write(p + 0x1f4, v); dbg.write(p + 0x28c, v); st['moved'] = done
                dbg.log('%s # Perso %08x teleported to %.1f %.1f %.1f' % (T(), p, *tgt))
        if 'fpu' in probes and st['fpu'] < 3:
            cw = fpu_cw(ctx); dbg.log('%s FPU control word at the VM tick 0x442240: 0x%04x' % (T(), cw)); st['fpu'] += 1
        if st['init'] is not None and a.frm <= since() <= a.until and since() >= st.get('next', 0):
            st['next'] = since() + a.every
            if 'cam' in probes: cam_line()
            if 'rocket' in probes: rocket_line()
    def on_init(ctx):
        st['init'] = time.perf_counter(); st['objs'] = None; dbg.log('INIT')

    # --- FPU
    def fpu_ctx(ctx_unused):
        c = wtrace.WOW64_CONTEXT(); c.ContextFlags = 0x10008 | 0x3
        k = wtrace.k32.Wow64GetThreadContext(dbg.thread(dbg.cur_tid), ctypes.byref(c))
        return c if k else None
    def fpu_cw(ctx):
        c = fpu_ctx(ctx); return (c.FloatSave.ControlWord & 0xffff) if c else -1
    def st0(c):
        raw = bytes(c.FloatSave.RegisterArea)[0:10]
        mant = int.from_bytes(raw[0:8], 'little'); se = int.from_bytes(raw[8:10], 'little')
        sign = -1 if se & 0x8000 else 1; e = se & 0x7fff
        return 0.0 if e == 0 and mant == 0 else sign * mant / (1 << 63) * 2.0 ** (e - 16383)
    def on_sweep_fistp(ctx):                   # 0x439cb3 fistp [0x5ac8ac]: st0 = floor(len/step) + 1 + 0.5
        if st['init'] is None or since() < a.frm: return
        if st['fpu'] >= 60: return
        c = fpu_ctx(ctx)
        if not c: return
        top = (c.FloatSave.StatusWord >> 11) & 7
        st['fpu_pending'] = (c.FloatSave.ControlWord & 0xffff, st0(c)); st['fpu'] += 1
    def on_sweep_after(ctx):                   # 0x439cb9: [0x5ac8ac] written
        p = st.pop('fpu_pending', None)
        if p is None: return
        cw, v = p; n = dbg.u32(0x5ac8ac)
        rc = (cw >> 10) & 3; pc = (cw >> 8) & 3
        dbg.log('%s SWEEP fistp value %.4f -> n %d  CW 0x%04x (rounding %s, precision %s)' % (T(), v, n, cw,
                ['nearest-even', 'down', 'up', 'truncate'][rc], ['24-bit', '?', '53-bit', '64-bit'][pc]))

    # --- per-frame instance list
    def on_listbuild(ctx):
        if st['init'] is None or since() < (a.frm if a.lfrom is None else a.lfrom) or st['nlist'] >= a.frames: return
        w = dbg.u32(0x509adc); n = dbg.u32(w + 0x60); arr = dbg.u32(w + 0x64)
        ptrs = struct.unpack('<%dI' % n, dbg.read(arr, 4 * n)) if n else ()
        cam = fv(dbg.u32(ctx.Esp + 4), 3)
        dbg.log('%s LIST n=%d cam %.1f %.1f %.1f : %s' % (T(), n, cam[0], cam[1], cam[2], ' '.join(str(slot(p)) for p in ptrs)))
        st['nlist'] += 1

    # --- carousel
    def on_car_place(ctx):                     # 0x451890(pos, 90, 150, 560, 100), ecx = class-110 instance
        s = slot(ctx.Ecx); pos, sp, rad, dist, hgt = fv(ctx.Esp + 4, 5)
        if pos != int(pos) or (s, pos) in st['car']: return          # each figure once per resting carousel position
        st['car'][(s, pos)] = 1; st['car_ecx'] = ctx.Ecx
        dbg.log('%s CAR 0x451890 slot %s rank %d pos %.3f spacing %.1f radius %.1f dist %.1f height %.1f' % (T(), s, dbg.u32(ctx.Ecx + 0x18c), pos, sp, rad, dist, hgt))
    def on_car_rot(ctx):                       # 0x489780(a, b, c) (stdcall 3 floats)
        if st.get('car_ecx') != ctx.Ecx: return
        x = fv(ctx.Esp + 4, 3)
        dbg.log('    0x489780 args a %.4f (%.2f deg) b %.4f (%.2f deg) c %.4f (%.2f deg)' % (x[0], math.degrees(x[0]), x[1], math.degrees(x[1]), x[2], math.degrees(x[2])))
    def on_car_ret(ctx):                       # 0x451935: back from 0x489780
        e = st.pop('car_ecx', None)
        if e is None: return
        L = fv(e + 0x14c, 9); B = fv(e + 0x128, 9)
        dbg.log('    theta(+0x174) %.3f  off(+0x104) %.2f %.2f %.2f  local(+0x14c) rows %s  basis(+0x128) %s' % (
            f32(e + 0x174), *fv(e + 0x104, 3), ' | '.join('%.4f %.4f %.4f' % L[i:i + 3] for i in (0, 3, 6)),
            ' | '.join('%.3f %.3f %.3f' % B[i:i + 3] for i in (0, 3, 6))))
        st.setdefault('car_after', set()).add(e)
    def on_car_world(ctx):                     # 0x48964c ret of 0x489210: instance rows +0x28 and pos written
        e = ctx.Ecx
        if e not in st.get('car_after', ()): return
        st['car_after'].discard(e)
        M = fv(dbg.u32(0x5e86ac) + 0x154, 16); R = fv(e + 0x28, 9); P = fv(e + 0xc, 3); S = fv(e + 0x4c, 3)
        cp = fv(dbg.u32(dbg.u32(0x509adc) + 8) + 0x90, 3)
        dbg.log('    0x489210 slot %s: M(+0x154) rows %s ; campos %.2f %.2f %.2f' % (slot(e), ' | '.join('%.5f %.5f %.5f %.5f' % M[i:i + 4] for i in (0, 4, 8, 12)), *cp))
        dbg.log('      inst pos %.2f %.2f %.2f rows %s scale %.3f %.3f %.3f' % (*P, ' | '.join('%.5f %.5f %.5f' % R[i:i + 3] for i in (0, 3, 6)), *S))

    # --- rocket
    def rocket_line():
        e = inst_ptr(a.inst)
        if not e: return
        state = dbg.u32(e + 0x128)
        R = fv(e + 0x28, 9); P = fv(e + 0xc, 3); q0 = fv(e + 0x170, 4); q1 = fv(e + 0x180, 4); t = f32(e + 0x130)
        p = dbg.u32(0x53a34c); ps = dbg.u32(p + 0x21c) if p else -1
        line = 'ROCKET %d state %d t %.3f pos %.1f %.1f %.1f rows %s q0 %s q1 %s perso %d | speed %.2f anim %d cache %08x fade %.2f' % (a.inst, state, t, *P,
               ' | '.join('%.4f %.4f %.4f' % R[i:i + 3] for i in (0, 3, 6)), ' '.join('%.4f' % v for v in q0), ' '.join('%.4f' % v for v in q1), ps,
               f32(e + 0xa0), dbg.u32(e + 0xb0), dbg.u32(e + 0x7c), f32(e + 0x6c))
        pal = dbg.u32(dbg.u32(0x509adc) + 0xa0) + 0x30 * dbg.u32(e + 0x5c); N = fv(pal, 12)   # the drawn node matrices (palette +0xa0)
        line += ' | node1 %s T %.1f %.1f %.1f' % (' | '.join('%.4f %.4f %.4f' % N[i:i + 3] for i in (0, 3, 6)), *N[9:12])
        if line != st['last']: dbg.log('%s %s' % (T(), line)); st['last'] = line

    # --- camera
    def cam_line():
        p = dbg.u32(0x53a34c); cm = dbg.u32(0x4c737c)
        if not p or not cm: return
        pos = fv(p + 0x1f4, 3); ps = dbg.u32(p + 0x21c); idx = dbg.u32(cm + 0x138); mode = dbg.u32(cm + 0x134)
        cpos = fv(cm + 0x1d0, 3); R = fv(cm + 0x140, 9); rail = fv(cm + 0x3e8, 3); d = f32(cm + 0x3f4)
        fwd = (R[2], R[5], R[8])
        extra = ''
        if a.onto: extra = ' | inst %d root %.1f %.1f %.1f' % (int(a.onto[0]), *fv(inst_ptr(int(a.onto[0])) + 0x60, 3))
        dbg.log('%s CAM perso %.1f %.1f %.1f state %d | mode %d (0x%x) cam %.1f %.1f %.1f fwdcol %.3f %.3f %.3f | rail %.1f %.1f %.1f d %.0f side %d%s' % (
            T(), *pos, ps, idx, mode, *cpos, *fwd, *rail, d, dbg.read(p + 0x4ec, 1)[0], extra))

    # --- crush
    def on_crush_scale(ctx):                   # 0x462bd4: the squash +0x2e8 after the clamp to 1 (only reached on a ray hit)
        v = f32(ctx.Esi + 0x2e8)
        if v < 1.0: dbg.log('%s CRUSH squash %.3f perso %08x pos %.1f %.1f %.1f' % (T(), v, ctx.Esi, *fv(ctx.Esi + 0x1f4, 3)))
    def on_crush_kill(ctx):
        dbg.log('%s CRUSH Kill(4) 0x462bed' % T())

    def on_createfile(ctx):
        name = dbg.cstr(dbg.u32(ctx.Esp + 4))
        if a.sav and name.lower().endswith('woody.sav'):
            if not st.get('savbuf'):
                va = wtrace.k32.VirtualAllocEx
                va.restype = ctypes.c_void_p; va.argtypes = [wtrace.wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wtrace.wt.DWORD, wtrace.wt.DWORD]
                st['savbuf'] = va(dbg.hproc, None, 4096, 0x3000, 0x04)
                dbg.write(st['savbuf'], os.path.abspath(a.sav).encode('mbcs') + bytes(1))
            dbg.write(ctx.Esp + 4, struct.pack('<I', st['savbuf']))
            dbg.log('# CreateFileA %s -> %s' % (name, os.path.abspath(a.sav)))
    def skip_logo(ctx):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4
        return 'skip'
    def on_msgbox(ctx):
        dbg.log('# MessageBoxA %r -> auto OK' % dbg.cstr(dbg.u32(ctx.Esp + 8)))
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 20; ctx.Eax = 1
        return 'skip'

    # --- BlackBox (docs/BLACKBOX.md): with --level Blackbox, 0x4042c9 je -> jmp makes the load of slot 0 create the object
    # 0x484420 (as dev flag 0x10 would), and --at T after INIT App+0 = 3 takes the title page away (state 3 = the mini game).
    # Per call of the frame 0x4846d0 (at most one line per --every s from --from): the round, Woody, Buzz and the pool;
    # --bbpos "T x y ..." teleports Woody (+0xc0034 +0x10). (A GDI screen grab only sees the last logo: the game flips a DirectDraw surface.)
    def on_bb_frame(ctx):
        t = since()
        if t < 0: return
        app = dbg.u32(0x4c2d00)
        if not st.get('bb3') and t >= a.at and app:
            dbg.write(app, struct.pack('<I', 3)); st['bb3'] = True; dbg.log('%s # App state -> 3' % T())
        o = ctx.Ecx
        if a.frm <= t <= a.until and t >= st.get('next', 0):
            st['next'] = t + a.every
            p, b = o + 0xc0034, o + 0xc00bc
            pool = sum(1 for i in range(10) if dbg.u32(o + 0xc02e4 + 0x64 + 0x68 * i))
            dbg.log('%s BB arg %d lvl %d woody st %d pos %.1f %.1f z %.0f dir %d lives %d inv %.2f | buzz ph %d st %d pos %.0f %.0f | pool %d | cage %d t %.2f end %.2f' % (
                T(), dbg.u32(ctx.Esp + 4) & 0xff, dbg.u32(o + 0xc0764), dbg.u32(p + 0xc), *fv(p + 0x10, 3), struct.unpack('<i', dbg.read(p + 0x54, 4))[0],
                dbg.u32(p + 0x6c), f32(p + 0x74), struct.unpack('<i', dbg.read(b + 0x64, 4))[0], dbg.u32(b + 0xc), *fv(b + 0x10, 2), pool,
                dbg.read(o + 0xc0760, 1)[0], f32(o + 0xc076c), f32(o + 0xc077c)))
        tp = [float(x) for x in a.bbpos.split()] if a.bbpos else []
        k = st.get('ntp', 0)
        if 3 * k + 2 < len(tp) and t >= tp[3 * k]:
            dbg.write(o + 0xc0034 + 0x10, struct.pack('<2f', tp[3 * k + 1], tp[3 * k + 2])); st['ntp'] = k + 1
            dbg.log('%s # Woody -> %.0f %.0f' % (T(), tp[3 * k + 1], tp[3 * k + 2]))

    bps = {0x401370: on_route, 0x442240: on_tick, 0x4427e0: on_init}
    if 'blackbox' in probes: bps[0x4846d0] = on_bb_frame
    if keys: bps[0x467ef0] = on_kbpoll
    if 'list' in probes: bps[0x42a980] = on_listbuild
    if 'fpu' in probes: bps[0x439cb3] = on_sweep_fistp; bps[0x439cb9] = on_sweep_after
    if 'carousel' in probes: bps.update({0x451890: on_car_place, 0x489780: on_car_rot, 0x451935: on_car_ret, 0x48964c: on_car_world})
    if 'crush' in probes: bps.update({0x462bd4: on_crush_scale, 0x462bed: on_crush_kill})
    if a.level:
        import pefile
        pe = pefile.PE(exe); img = pe.get_memory_mapped_image()
        table = struct.unpack_from('<28I', img, 0x4b12a0 - 0x400000)
        want = (r'\Data\%s\%s.gel' % (a.level, a.level)).encode()
        match = [p for p in table if img[p - 0x400000:p - 0x400000 + len(want) + 1] == want + bytes(1)]
        if not match: sys.exit('unknown level %s' % a.level)
        def redirect(ctx):
            dbg.write(0x4b12a0, struct.pack('<I', match[0])); dbg.log('# level slot 0 -> %s' % a.level)
            if 'blackbox' in probes: dbg.write(0x4042c9, b'\xeb'); dbg.log('# 0x4042c9 je -> jmp: every level load creates the BlackBox object')
        dbg.post_arm = redirect
        bps[0x446b00] = skip_logo
    dbg.pre_bps = bps
    dbg.iat_bps = {0x5eb5ec: on_msgbox}
    if a.sav: dbg.iat_bps[0x5eb49c] = on_createfile
    # the handlers need the thread id of the current event for the FPU context
    orig_get = dbg.get_ctx
    def get_ctx(tid):
        dbg.cur_tid = tid; return orig_get(tid)
    dbg.get_ctx = get_ctx
    try: dbg.run(a.seconds)
    except KeyboardInterrupt: wtrace.k32.TerminateProcess(dbg.hproc, 0)
    finally: out.flush()

if __name__ == '__main__':
    main()
