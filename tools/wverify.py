"""wverify.py - check statically decompiled claims against the RUNNING original (Woody.exe under tools/wtrace.py's debugger).

Common options (every probe):
  --level LVL          boot straight into LVL (House slot 0 of 0x4b12a0 redirected, title logo 0x446b00 skipped);
                       without it the game boots to the title on House (menu probes)
  --pos x y z --at T   teleport the Perso T s after INIT (Perso+0x1f4 and +0x28c, as tools/wiris.py)
  --keys "T:KEY[:D] ..." synthetic keyboard: the DirectInput poll 0x467ef0 is replaced (so the real keyboard is dead
                       and the window needs no focus); KEY is held from T s after INIT for D s (default 0.1).
                       KEY = RET ESC UP DOWN LEFT RIGHT JUMP (LCtrl) ATTACK (LShift) DUCK (Space) or a hex internal code.
  --seconds N          wall-clock limit (the boot to INIT takes ~70 s)
  --fixfps N           from INIT every frame advances exactly 1/N s (debug switch [0x5d7b89] / [0x4b3a8c] of 0x401810), as the
                       port's WOODY_FIXDT=N; --at / --from / --until stay wall-clock seconds
  --sav FILE           CreateFileA of "Woody.sav" is redirected to FILE (game/ has no save; a copy of the port's woodyre.sav,
                       which has the original layout, lets the Load game chain reach the carousel without touching game/)
  --face YAW           with --pos: turn the Perso to YAW degrees (0 = +z, 90 = +x) through Mover_SetDir 0x459ff0 (the follow
                       camera does not swing round by itself; add --keys "T:51" = Num0, camera behind, to make it)
  --shot "T f.png ..." the finished frame T s after INIT, read from the back buffer at Present 0x47ee90 through
                       IDirectDrawSurface7::Lock/Unlock called in the game's main thread; needs --windowed (in exclusive
                       fullscreen the surfaces are lost as soon as the game is not the foreground window: DDERR_SURFACELOST)
  --windowed           0x4027b8 patched so the renderer takes its unused windowed path (DISPLAY.md 1.1): a 644x504 popup,
                       no display mode switch, same 640x480 back buffer and 16-bit z (not compared with a fullscreen frame)

Probes (--probe, several allowed, comma separated):
  list      the per-frame instance list world+0x64 (0x42a980 -> 0x42a840, docs/INSTANCE.md 4.1): at the entry of 0x42a980
            (= before this frame's rebuild) print the previous frame's list as .ins slot ids, --frames frames from --from s
  fpu       the x87 control word at the camera sweep's inline fistp 0x439cb3 (docs/CAMERA.md 3.6) and the step count
            [0x5ac8ac] it produces, plus the control word at the frame's VM tick
  carousel  world-select carousel class 110 (docs/MENU_LOAD.md 4.4): args of 0x489780, the local basis inst+0x14c it builds,
            the camera matrix [0x5e86ac]+0x154 and the instance rows 0x489210 writes, for slots 105..114, plus each
            figure's animation state (speed +0xa0, clock +0xa8, position +0xac, slots +0xb0) and drawn root node
  rocket    rideable rocket (docs/ROCKET.md): per frame state/t/rot rows/quaternions of the class-20 instance --inst
  cam       per frame Perso pos/state + camera mode index/position (CamMgr+0x138/+0x1d0) and the rail point (+0x3e8)
  blackbox  the BlackBox mini game (docs/BLACKBOX.md): with --level BlackBox, patches 0x4042c9 so the load creates the object
            and sets App state 3 at --at; logs the round / Woody / Buzz / pool per 0x4846d0 call; --bbpos teleports Woody
  crush     the crush test 0x462a40: every squash < 1 (0x462bd4) and every Kill(4) call (0x462bed)
  move      per Perso_MoveCollide 0x4624f0 (--from..--until): pos/disp at the entry, platform carry 0x436d20, swept pos,
            floor attach 0x436f00 (ground y, kind, instance, plane), attach record +0x298, Mover normal/slide/RampB
  shadow    the cast shadow of instance --inst (docs/LIGHTING.md 3/4): per draw 0x42e2b0 the arg, the sector +0x1c, the
            animated root +0x60 and the light the sector list gives (0x42e573), at most one line per --every s
  bomb      per VM tick every bomb of the pool 0x5e4880 in use (+0x131): state +0x108, held +0x132, pos +0xc and its projectile
            +0x124 (age P+0xd4, position P+0xb0, velocity P+0xc8, target P+0x80, owner P+0xa0, grounded P+0xec), plus the Perso pos/state/sub-state; every Launch 0x44d4d0 prints the template it starts from (BOMBLAUNCH: T.pos, dir0, gravity, speed, life, ground flag)
  proj      launchers and projectiles (docs/PROJECTILES.md): every Fire 0x452560 (aim flag +0x199, target, interval, block),
            every projectile init 0x449130 (start, dir0, visual) and per VM tick each projectile of the pool 0x5d7d48 in use

  --setvar "T var val ..."  SetVar 0x443ca0(var, val) T s after INIT (as tools/wsetvar.py, one per VM tick)
  --tp "T x y z yaw ..."    teleport the Perso T s after INIT and turn him to yaw (Mover_SetDir); x = "bomb" puts him
                            y units off the first bomb in use, in +x, and z is ignored
  --respawn "T x y z yaw"   T s after INIT make (x y z, yaw) the respawn point (+0x318, +0x324, +0x330 = 1): the next death
                            respawns him there with the camera cut behind him (races: a ride from any point of the track)
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
    ap.add_argument('--shot', default='', help='"T file.png [T file.png ...]": the finished frame (back buffer at Present 0x47ee90) T s after INIT')
    ap.add_argument('--face', type=float, help='with --pos: turn the Perso to this yaw (degrees, 0 = +z, 90 = +x) through the Mover 0x459ff0')
    ap.add_argument('--windowed', action='store_true', help='patch 0x4027b8 so the renderer takes its (never shipped) windowed path: no exclusive mode')
    ap.add_argument('--setvar', default='', help='"T var val ...": SetVar 0x443ca0 T s after INIT')
    ap.add_argument('--fixfps', type=int, help='from INIT every frame advances exactly 1/N s (the debug switch [0x5d7b89] / [0x4b3a8c] of 0x401810), like the port\'s WOODY_FIXDT')
    ap.add_argument('--tp', default='', help='"T x y z yaw ...": teleport + face T s after INIT (x = bomb: y units off the first bomb in use)')
    ap.add_argument('--watch', default='', help='proj probe: "slot slot ..." whose presence in the instance list is logged on change')
    ap.add_argument('--respawn', default='', help='"T x y z yaw": T s after INIT set the respawn point Perso+0x318/+0x324 (+0x330 = 1), used at the next death')
    a = ap.parse_args()
    tok = a.setvar.split(); setvars = [(float(tok[i]), int(tok[i + 1]), int(tok[i + 2])) for i in range(0, len(tok) - 2, 3)]
    tok = a.respawn.split(); respawns = [tuple(float(v) for v in tok[i:i + 5]) for i in range(0, len(tok) - 4, 5)]
    tok = a.tp.split(); tps =[(float(tok[i]), tok[i + 1], float(tok[i + 2]), float(tok[i + 3]), float(tok[i + 4])) for i in range(0, len(tok) - 4, 5)]
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
        if setvars and st['init'] is not None and since() >= setvars[0][0]:
            _, var, val = setvars.pop(0); esp = ctx.Esp - 16
            dbg.write(esp, struct.pack('<4I', 0x41a4bd, var & 0xffffffff, val & 0xffffffff, 0x442240))   # gadget add esp, 8; ret -> the tick
            ctx.Esp = esp; ctx.Eip = 0x443ca0
            dbg.log('%s # SetVar(%d, %d) injected' % (T(), var, val))
            return 'skip'
        if tps and st['init'] is not None and since() >= tps[0][0]:
            _, x, y, z, yaw = tps.pop(0); p = dbg.u32(0x53a34c)
            if p:
                if x == 'bomb':
                    bs = [b for b in (dbg.u32(0x5e4880 + 4 * i) for i in range(dbg.u32(0x5e487c))) if b and dbg.read(b + 0x131, 1)[0]]
                    if not bs: dbg.log('%s # tp: no bomb in use' % T()); return
                    q = fv(bs[0] + 0xc, 3); tgt = (q[0] + y, q[1] + 30.0, q[2])
                else: tgt = (float(x), y, z)
                v = struct.pack('<3f', *tgt); dbg.write(p + 0x1f4, v); dbg.write(p + 0x28c, v)
                dbg.log('%s # Perso teleported to %.1f %.1f %.1f, yaw %.0f' % (T(), *tgt, yaw))
                return face_call(ctx, p, yaw)
        if respawns and st['init'] is not None and since() >= respawns[0][0]:
            _, x, y, z, yaw = respawns.pop(0); p = dbg.u32(0x53a34c)
            if p:                              # the next respawn 0x44a810 puts him on +0x318, facing +0x324 since +0x330 says "checkpoint taken"
                dbg.write(p + 0x318, struct.pack('<6f', x, y, z, math.sin(math.radians(yaw)), 0.0, math.cos(math.radians(yaw)))); dbg.write(p + 0x330, b'\x01')
                dbg.log('%s # respawn point set to %.1f %.1f %.1f, yaw %.0f' % (T(), x, y, z, yaw))
        if 'bomb' in probes and st['init'] is not None and a.frm <= since() <= a.until: bomb_lines()
        if 'proj' in probes and st['init'] is not None and a.frm <= since() <= a.until: proj_lines()
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
                if a.face is not None and done and not st.get('faced'):
                    st['faced'] = True; return face_call(ctx, p, a.face)
        if 'fpu' in probes and st['fpu'] < 3:
            cw = fpu_cw(ctx); dbg.log('%s FPU control word at the VM tick 0x442240: 0x%04x' % (T(), cw)); st['fpu'] += 1
        if st['init'] is not None and a.frm <= since() <= a.until and since() >= st.get('next', 0):
            st['next'] = since() + a.every
            if 'cam' in probes: cam_line()
            if 'rocket' in probes: rocket_line()
    def on_init(ctx):
        st['init'] = time.perf_counter(); st['objs'] = None; dbg.log('INIT')
        if a.fixfps:                           # the debug fixed step of the frame 0x40181e: dt = 1/[0x4b3a8c] while [0x5d7b89] != 0
            dbg.write(0x4b3a8c, struct.pack('<i', a.fixfps)); dbg.write(0x5d7b89, b'\x01'); dbg.log('# fixed dt 1/%d (0x5d7b89 = 1)' % a.fixfps)

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
        pal = dbg.u32(dbg.u32(0x509adc) + 0xa0) + 0x30 * dbg.u32(e + 0x5c); N = fv(pal, 12)   # the drawn root node matrix (palette +0xa0)
        dbg.log('      anim speed %.3f clock %.3f apos %.3f slots %d %d %d %d | node1 T %.1f %.1f %.1f' % (f32(e + 0xa0), f32(e + 0xa8), f32(e + 0xac),
                *struct.unpack('<4i', dbg.read(e + 0xb0, 16)), *N[9:12]))

    # --- cast shadow of one instance
    def on_sh_draw(ctx):                       # 0x42e2b0 entry: ecx = instance, [esp+4] = arg bits (2 = cast shadow, 4 = model)
        if ctx.Ecx != inst_ptr(a.inst): return
        st['sh_arg'] = dbg.u32(ctx.Esp + 4)
    def on_sh_light(ctx):                      # 0x42e573: the light choice is done; ebp = instance
        e = ctx.Ebp
        if e != inst_ptr(a.inst): return
        t = since()
        if a.every and t - st.get('sh_t', -1e9) < a.every: return
        st['sh_t'] = t
        have = dbg.u32(ctx.Esp + 0x1b7ac); li = dbg.u32(ctx.Esp + 0x1b7b0); argnow = dbg.u32(ctx.Esp + 0x1b7bc)
        sec = struct.unpack('<i', dbg.read(e + 0x1c, 4))[0]
        lit = dbg.u32(0x4c4cac); lst = dbg.u32(dbg.u32(lit + 0x10) + 4 * sec) if sec >= 0 else 0
        n = dbg.u32(lst) if lst else 0; ids = struct.unpack('<%dI' % min(n, 16), dbg.read(lst + 4, 4 * min(n, 16))) if n else ()
        lp = fv(dbg.u32(lit + 4) + 64 * li + 0xc, 3) if have else (0, 0, 0)
        dbg.log('%s SHADOW inst %d arg %x -> %x sector %d lights %s root %.0f %.0f %.0f pos %.0f %.0f %.0f | light %s at %.0f %.0f %.0f' % (T(), a.inst,
                st.get('sh_arg', -1), argnow, sec, list(ids), *fv(e + 0x60, 3), *fv(e + 0xc, 3), li if have else 'none', *lp))

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

    # --- bombs (docs/BOMB.md, BOMB_CARRY.md)
    def bomb_lines():
        p = dbg.u32(0x53a34c); pl = ''
        if p: pl = 'perso %.1f %.1f %.1f state %d sub %d' % (*fv(p + 0x1f4, 3), dbg.u32(p + 0x21c), dbg.u32(p + 0x58c))
        for i in range(min(dbg.u32(0x5e487c), 16)):
            b = dbg.u32(0x5e4880 + 4 * i)
            if not b or not dbg.read(b + 0x131, 1)[0]: continue
            pr = dbg.u32(b + 0x124); line = 'BOMB %s state %d held %d pos %.1f %.1f %.1f' % (slot(b), dbg.u32(b + 0x108), dbg.read(b + 0x132, 1)[0], *fv(b + 0xc, 3))
            if pr: line += ' | proj age %.3f pos %.1f %.1f %.1f vel %.1f %.1f %.1f target %s owner %s grounded %d' % (f32(pr + 0xd4), *fv(pr + 0xb0, 3), *fv(pr + 0xc8, 3),
                    slot(dbg.u32(pr + 0x80)) if dbg.u32(pr + 0x80) else '-', slot(dbg.u32(pr + 0xa0)) if dbg.u32(pr + 0xa0) else '-', dbg.read(pr + 0xec, 1)[0])
            dbg.log('%s %s | %s' % (T(), line, pl))

    def on_bomb_launch(ctx):                 # 0x44d4d0 Launch(T*, ground, var, kind), thiscall: the template the projectile starts from
        t = dbg.u32(ctx.Esp + 4); g = dbg.read(ctx.Esp + 8, 1)[0]
        dbg.log('%s BOMBLAUNCH %s T.pos %.2f %.2f %.2f dir0 %.4f %.4f %.4f grav %.2f speed %.1f life %.2f ground %d var %d kind %d' % (T(), slot(ctx.Ecx), *fv(t, 6), f32(t + 0x1c), f32(t + 0x20), f32(t + 0x2c), g, struct.unpack('<i', dbg.read(ctx.Esp + 12, 4))[0], dbg.u32(ctx.Esp + 16)))

    # --- launchers and projectiles (docs/PROJECTILES.md): every Fire 0x452560 (ecx = launcher) with its aim flag, target,
    # interval and block, and per VM tick every projectile of the pool 0x5d7d48[200] in use (+0xe4)
    def on_fire(ctx):
        if st['init'] is None or not (a.frm <= since() <= a.until): return
        e = ctx.Ecx; tg = dbg.u32(e + 0x178); p = dbg.u32(0x53a34c)
        dbg.log('%s FIRE launcher %s pos %.0f %.0f %.0f aim %d target %s T %.2f count %d visual %d aim_h %.0f life %.2f steer %.3f vsteer %.1f t_xz %.2f t_y %.2f | perso %.0f %.0f %.0f' % (
            T(), slot(e), *fv(e + 0xc, 3), dbg.read(e + 0x199, 1)[0], slot(tg) if tg else '-', f32(e + 0x174), struct.unpack('<i', dbg.read(e + 0x170, 4))[0],
            dbg.u32(e + 0x168), f32(e + 0x144), f32(e + 0x134), f32(e + 0x148), f32(e + 0x14c), f32(e + 0x150), f32(e + 0x154), *(fv(p + 0x1f4, 3) if p else (0, 0, 0))))
    def on_pinit(ctx):                         # 0x449130 entry: ecx = P, [esp+4] = the block (start pos +0, dir0 +0xc)
        if st['init'] is None or not (a.frm <= since() <= a.until): return
        b = dbg.u32(ctx.Esp + 4); ow = dbg.u32(b + 0x58)
        dbg.log('%s PINIT %d owner %s pos %.0f %.0f %.0f dir0 %.3f %.3f %.3f speed %.0f visual %d' % (T(), (ctx.Ecx - 0x5d7d48) // 0x104, slot(ow) if ow else '-',
                *fv(b, 3), *fv(b + 0xc, 3), f32(b + 0x20), dbg.u32(b + 0x60)))
    def proj_lines():
        if a.watch:                            # which of the --watch slots are in this frame's instance list world+0x64 (= think)
            w = dbg.u32(0x509adc); n = dbg.u32(w + 0x60); arr = dbg.u32(w + 0x64)
            ptrs = set(struct.unpack('<%dI' % n, dbg.read(arr, 4 * n))) if n else set()
            on = [s for s in a.watch.split() if inst_ptr(int(s)) in ptrs]
            line = 'LISTED n=%d: %s' % (n, ' '.join(on) if on else '-')
            if line != st.get('lastw'): dbg.log('%s %s' % (T(), line)); st['lastw'] = line
        raw = dbg.read(0x5d7d48, 200 * 0x104)
        for i in range(200):
            o = i * 0x104
            if not raw[o + 0xe4]: continue
            pos = struct.unpack_from('<3f', raw, o + 0xb0); vel = struct.unpack_from('<3f', raw, o + 0xc8); age = struct.unpack_from('<f', raw, o + 0xd4)[0]
            ow = struct.unpack_from('<I', raw, o + 0xa0)[0]
            dbg.log('%s PROJ %d owner %s age %.3f pos %.0f %.0f %.0f vel %.0f %.0f %.0f' % (T(), i, slot(ow) if ow else '-', age, *pos, *vel))

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
    # --- enemy (docs/ENEMY.md 3.1): Think 0x41a320 of instance --inst (position +0xc, activation distance P+0xc0, P = +0x118,
    # instance clock +0xac) at most once per --every, and every Update vtbl[52] of types 4..6 (0x418cf0) / 7..9 (0x416fb0) it gets
    def on_en_think(ctx):
        if st['init'] is None or not (a.frm <= since() <= a.until) or ctx.Ecx != inst_ptr(a.inst): return
        if since() - st.get('en_t', -9) < a.every: return
        st['en_t'] = since(); p = dbg.u32(ctx.Ecx + 0x118)
        dbg.log('%s ENEMY %d think pos %.1f %.1f %.1f active_d %.0f clock +0xac %.3f updates %d' % (T(), a.inst, *fv(ctx.Ecx + 0xc, 3), f32(p + 0xc0), f32(ctx.Ecx + 0xac), st.get('en_up', 0)))
    def on_en_update(ctx):
        if st['init'] is not None and ctx.Ecx == inst_ptr(a.inst): st['en_up'] = st.get('en_up', 0) + 1
    def on_crush_kill(ctx):
        dbg.log('%s CRUSH Kill(4) 0x462bed' % T())

    # --- Perso_MoveCollide 0x4624f0 (docs/PERSO_MOVE.md 6.1), one line per call from --from to --until: the position and
    # displacement +0x204 at the entry, the platform carry 0x436d20 returns, the swept position, the floor attach 0x436f00
    # (hit, ground y, kind [0x53a554], instance, plane [0x4b3108]) and the attach record +0x298 it leaves, plus the Mover's
    # ground normal M+0xd0, sliding M+0xdc and RampB (slide) speed / direction (M = P+0x388, RampB = M+0x68)
    def on_mc_entry(ctx):
        if st['init'] is None or not (a.frm <= since() <= a.until): st['mc'] = None; return
        p = ctx.Ecx; att = dbg.u32(p + 0x298)
        st['mc'] = '%s MC pos %.2f %.2f %.2f disp %.2f %.2f %.2f st %d g %d att %s/%d' % (T(), *fv(p + 0x1f4, 3), *fv(p + 0x204, 3),
                   dbg.u32(p + 0x21c), dbg.read(p + 0x22c, 1)[0], slot(att) if att else '-', dbg.u32(p + 0x29c) if att else -1)
        if att: st['mc'] += ' local %.2f %.2f %.2f world %.2f %.2f %.2f' % (*fv(p + 0x2a0, 3), *fv(p + 0x2ac, 3))
        st['mc'] += ' dt %.4f' % f32(p + 0x2f8)
    def on_mc_carry(ctx):
        if st.get('mc'): st['mc'] += ' | carry %.2f %.2f %.2f' % fv(ctx.Esp + 0x24, 3)
    def on_mc_swept(ctx):
        if st.get('mc'): st['mc'] += ' | swept %.2f %.2f %.2f' % fv(ctx.Esi + 0x1f4, 3)
    def on_mc_floor(ctx):
        if not st.get('mc'): return
        p = ctx.Esi; k = dbg.u32(0x53a554); gi = dbg.u32(0x53a560) if k == 2 else 0
        n = fv(0x4b3108, 4); M = p + 0x388
        st['mc'] += ' | floor hit %d gy %.2f kind %d inst %s n %.3f %.3f %.3f | M n %.3f %.3f %.3f slide %d B v %.1f dir %.3f %.3f %.3f ph %d | A v %.1f' % (
            ctx.Eax, f32(0x53a568), k, slot(gi) if gi else '-', n[0], n[1], n[2], *fv(M + 0xd0, 3), dbg.read(M + 0xdc, 1)[0],
            f32(M + 0x68 + 0xc), *fv(M + 0x68, 3), dbg.u32(M + 0x68 + 0x2c), f32(M + 0x34 + 0xc))
        dbg.log(st['mc']); st['mc'] = None

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

    # --- screenshots (--shot). The game flips a DirectDraw surface, so a GDI grab sees nothing useful. Instead, at the entry
    # of Present 0x47ee90 (ecx = renderer [0x5e8650]; +0x38 = back buffer, +0x3c = primary, DISPLAY.md 2.2) the finished
    # frame is still in the back buffer: the main thread is sent through IDirectDrawSurface7::Lock (vtable +0x64,
    # DDLOCK_WAIT | DDLOCK_READONLY) and ::Unlock (+0x80) with a return address on an int3 page of our own, the pixels are
    # read with ReadProcessMemory in between, and the registers are put back before Present runs as usual.
    shots = []
    if a.shot:
        tok = a.shot.split()
        shots = [(float(tok[i]), tok[i + 1]) for i in range(0, len(tok) - 1, 2)]
    def remote_alloc(n, prot):
        va = wtrace.k32.VirtualAllocEx
        va.restype = ctypes.c_void_p; va.argtypes = [wtrace.wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wtrace.wt.DWORD, wtrace.wt.DWORD]
        return va(dbg.hproc, None, n, 0x3000, prot)
    REGS = ('Eax', 'Ebx', 'Ecx', 'Edx', 'Esi', 'Edi', 'Ebp', 'Esp', 'Eip', 'EFlags')
    def on_present(ctx):
        if st.get('shot') or not shots or st['init'] is None or since() < shots[0][0]: return None
        if 'page' not in st:
            st['page'] = remote_alloc(4096, 0x40)
            dbg.write(st['page'], b'\xcc' * 4096); dbg.add_bp(st['page'], on_shot_ret)
            st['desc'] = st['page'] + 0x100
        back = dbg.u32(ctx.Ecx + 0x38)
        st['shot'] = {'regs': {r: getattr(ctx, r) for r in REGS}, 'back': back, 'step': 'lock'}
        st['shot']['regs']['Eip'] = 0x47ee90                 # the context of a breakpoint hit has Eip past the int3
        dbg.write(st['desc'], struct.pack('<I', 0x7c) + bytes(0x78))
        esp = ctx.Esp - 24
        dbg.write(esp, struct.pack('<6I', st['page'], back, 0, st['desc'], 0x11, 0))
        ctx.Esp = esp; ctx.Eip = dbg.u32(dbg.u32(back) + 0x64)
        return 'skip'
    # --face: Mover_SetDir 0x459ff0 (thiscall, ecx = Perso+0x388, arg = &dir, ret 4) called from the VM tick entry with the
    # return address on an int3 page, then the registers are put back and the tick runs as usual
    def face_call(ctx, p, face):
        if 'fpage' not in st:
            st['fpage'] = remote_alloc(4096, 0x40); dbg.write(st['fpage'], b'\xcc' * 4096); dbg.add_bp(st['fpage'], on_face_ret)
        yaw = math.radians(face)
        dbg.write(st['fpage'] + 0x100, struct.pack('<3f', math.sin(yaw), 0.0, math.cos(yaw)))
        st['fregs'] = {r: getattr(ctx, r) for r in REGS}; st['fregs']['Eip'] = 0x442240
        esp = ctx.Esp - 8
        dbg.write(esp, struct.pack('<2I', st['fpage'], st['fpage'] + 0x100))
        ctx.Esp = esp; ctx.Ecx = p + 0x388; ctx.Eip = 0x459ff0
        dbg.log('%s # Mover_SetDir(%.3f 0 %.3f)' % (T(), math.sin(yaw), math.cos(yaw)))
        return 'skip'
    def on_face_ret(ctx):
        for r, v in st['fregs'].items(): setattr(ctx, r, v)
        return 'skip'
    def save_png(path, raw, w, h, pitch, bpp, rm, gm, bm):
        from PIL import Image
        bpb = bpp // 8
        if bpb == 4 and (rm, gm, bm) == (0xff0000, 0xff00, 0xff):
            img = Image.frombuffer('RGB', (w, h), raw, 'raw', 'BGRX', pitch, 1)
        elif bpb == 2 and (rm, gm, bm) == (0xf800, 0x7e0, 0x1f):
            img = Image.frombuffer('RGB', (w, h), raw, 'raw', 'BGR;16', pitch, 1)
        else:
            def chan(px, m):
                if not m: return 0
                sh = (m & -m).bit_length() - 1
                return ((px & m) >> sh) * 255 // (m >> sh)
            img = Image.new('RGB', (w, h)); pix = img.load()
            for y in range(h):
                row = raw[y * pitch:y * pitch + w * bpb]
                for x, px in enumerate(struct.unpack('<%d%s' % (w, 'I' if bpb == 4 else 'H'), row)): pix[x, y] = (chan(px, rm), chan(px, gm), chan(px, bm))
        img.save(path)
    def on_shot_ret(ctx):
        s = st['shot']; t, path = shots[0]
        if s['step'] == 'lock':
            hr = ctx.Eax
            d = dbg.read(st['desc'], 0x7c)
            h, w, pitch = struct.unpack_from('<III', d, 8); surf = struct.unpack_from('<I', d, 0x24)[0]
            bpp, rm, gm, bm = struct.unpack_from('<IIII', d, 0x54)
            if hr == 0 and surf:
                try:
                    save_png(path, dbg.read(surf, pitch * h), w, h, pitch, bpp, rm, gm, bm)
                    dbg.log('%s SHOT %s (%dx%d, %d bpp, masks %x %x %x)' % (T(), path, w, h, bpp, rm, gm, bm))
                except Exception as e: dbg.log('%s SHOT %s failed: %r' % (T(), path, e))
            else: dbg.log('%s SHOT Lock failed hr=%08x' % (T(), hr))
            s['step'] = 'unlock'
            esp = ctx.Esp - 12
            dbg.write(esp, struct.pack('<3I', st['page'], s['back'], 0))
            ctx.Esp = esp; ctx.Eip = dbg.u32(dbg.u32(s['back']) + 0x80)
            return 'skip'
        for r, v in s['regs'].items(): setattr(ctx, r, v)
        shots.pop(0); st['shot'] = None
        return 'skip'

    bps = {0x401370: on_route, 0x442240: on_tick, 0x4427e0: on_init}
    if shots: bps[0x47ee90] = on_present
    if 'blackbox' in probes: bps[0x4846d0] = on_bb_frame
    if keys: bps[0x467ef0] = on_kbpoll
    if 'list' in probes: bps[0x42a980] = on_listbuild
    if 'fpu' in probes: bps[0x439cb3] = on_sweep_fistp; bps[0x439cb9] = on_sweep_after
    if 'carousel' in probes: bps.update({0x451890: on_car_place, 0x489780: on_car_rot, 0x451935: on_car_ret, 0x48964c: on_car_world})
    if 'crush' in probes: bps.update({0x462bd4: on_crush_scale, 0x462bed: on_crush_kill})
    if 'enemy' in probes: bps.update({0x41a320: on_en_think, 0x418cf0: on_en_update, 0x416fb0: on_en_update})
    if 'move' in probes: bps.update({0x4624f0: on_mc_entry, 0x4625f4: on_mc_carry, 0x46268d: on_mc_swept, 0x4626f9: on_mc_floor})
    if 'bomb' in probes: bps[0x44d4d0] = on_bomb_launch
    if 'shadow' in probes: bps.update({0x42e2b0: on_sh_draw, 0x42e573: on_sh_light})
    if 'proj' in probes: bps.update({0x452560: on_fire, 0x449130: on_pinit})
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
    if a.windowed:
        prev = getattr(dbg, 'post_arm', None)
        def win_patch(ctx):
            if prev: prev(ctx)
            dbg.write(0x4027b8, b'\xb1\x01\x90'); dbg.log('# 0x4027b8 and cl,1 -> mov cl,1: renderer+0 = windowed (DISPLAY.md 1.1)')
        dbg.post_arm = win_patch
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
