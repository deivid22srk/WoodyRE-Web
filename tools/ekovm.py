"""Python emulator for the EKO CODE VM, mirroring Woody.exe (interpreter 0x4429f0, init 0x4427e0,
tick 0x442240). Engine-side effects (messages, volumes) are stubbed and logged.

usage: ekovm.py <Data dir or code file> [--trace]
Runs the level init for each level and prints message statistics.
"""
import struct, sys, os, random
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(__file__))
from ekodisasm import OPS

class EkoVM:
    def __init__(self, path, log=None):
        d = open(path, 'rb').read()
        n = len(d) // 4
        self.w = list(struct.unpack('<%dI' % n, d[:n * 4]))
        w = self.w
        self.nobj = w[2]; tab = w[3]
        self.objs = [w[tab + i] for i in range(self.nobj)]
        self.base = tab + self.nobj
        self.nvars, vars_off = w[4], w[5]
        self.nvol, self.vol_off = w[6], w[7]
        self.nstr, self.str_off = w[8], w[9]
        self.ncol, self.col_off = w[10], w[11]
        # variables: value + watcher list
        self.vars = [0] * self.nvars
        self.watchers = []
        pos = vars_off + self.nvars * 2
        for i in range(self.nvars):
            cnt = w[pos]; self.watchers.append(w[pos + 1:pos + 1 + cnt]); pos += 1 + cnt
        self.strings = []
        if self.nstr:
            raw = struct.pack('<%dI' % (len(w) - (self.str_off + self.nstr)), *w[self.str_off + self.nstr:])
            self.strings = [p.decode('latin1') for p in raw.split(b'\0')[:self.nstr]]
        # runtime state
        self.stack = []; self.bstack = []
        self.globals = [0, 0]
        self.time = 0; self.frame = 1
        self.stop = False
        self.msgs = []           # outgoing message records (id, args)
        self.delays = []         # (time, target, owner)
        self.durings = []
        self.wake = []           # object ids to run next tick
        self.stamp = [0] * self.nobj
        self.msgmask = [0] * (self.nobj + 1)
        self.vol_flags = [0] * self.nvol; self.vol_count = [0] * self.nvol; self.vol_list = [[] for _ in range(self.nvol)]
        self.col_flags = [0] * self.ncol; self.col_list = [[] for _ in range(self.ncol)]
        self.log = log
        self.seed = 1
        self.stats = Counter(); self.opstats = Counter()

    # ---- helpers ----
    def code(self, i): return self.w[self.base + i]
    def obj_of(self, pc):
        """object owning code index pc (0x444060)"""
        for i in range(self.nobj - 1):
            if self.objs[i] <= pc < self.objs[i + 1]: return i
        return self.nobj - 1
    def wake_obj(self, o):
        if len(self.wake) <= 1000: self.wake.append(o)
    def setvar(self, idx, v):
        idx &= 0xffffff
        for o in self.watchers[idx]: self.wake_obj(o)
        self.vars[idx] = v
    def bpush(self, b): self.bstack.append(1 if b else 0)
    def rand(self):
        """MSVC CRT rand(): seed = seed*214013+2531011; return (seed>>16)&0x7fff"""
        self.seed = (self.seed * 214013 + 2531011) & 0xffffffff
        return (self.seed >> 16) & 0x7fff
    def delay_add(self, t, target, pc):
        """0x444290/0x4442d0: insert before the first entry whose time >= t"""
        i = 0
        while i < len(self.delays) and self.delays[i][0] < t: i += 1
        self.delays.insert(i, (t, target, self.obj_of(pc)))

    # ---- interpreter ----
    def run(self, pc):
        self.stop = False
        w = self.w; b = self.base
        steps = 0
        while not self.stop:
            op = w[b + pc]
            self.opstats[op] += 1
            steps += 1
            if steps > 200000: raise RuntimeError("runaway at pc %d" % pc)
            if op >= 0x3f: self.stop = True; break
            a1 = w[b + pc + 1] if b + pc + 1 < len(w) else 0
            a2 = w[b + pc + 2] if b + pc + 2 < len(w) else 0
            s = self.stack; bs = self.bstack
            if op == 0: pc += 1
            elif op == 1: raise RuntimeError("HANG opcode")
            elif op == 2: self.stop = True; pc += 1
            elif op == 3: s.append(a1); pc += 2
            elif op == 4: s.append(a1); pc += 2
            elif op == 5: s.append(self.vars[a1]); pc += 2
            elif op == 6: self.setvar(a1, s.pop()); pc += 2
            elif op == 7: v = s.pop(); s[-1] = (s[-1] + v) & 0xffffffff; pc += 1
            elif op == 8: v = s.pop(); s[-1] = (s[-1] - v) & 0xffffffff; pc += 1
            elif op == 9: v = s.pop(); s[-1] = (self.sg(s[-1]) * self.sg(v)) & 0xffffffff; pc += 1
            elif op == 10:
                v = self.sg(s.pop()); a = self.sg(s[-1])
                s[-1] = (int(a / v) if v else 0) & 0xffffffff; pc += 1
            elif op == 11: s[-1] = (-self.sg(s[-1])) & 0xffffffff; pc += 1
            elif op == 12: self.bpush(s.pop() != 0); pc += 1
            elif 13 <= op <= 18:
                v = self.sg(s.pop()); a = self.sg(s.pop())
                self.bpush([a == v, a != v, a > v, a >= v, a < v, a <= v][op - 13]); pc += 1
            elif op == 19: v = bs.pop(); bs[-1] = 1 if (bs[-1] or v) else 0; pc += 1
            elif op == 20: v = bs.pop(); bs[-1] = 1 if (bs[-1] and v) else 0; pc += 1
            elif op == 21: bs[-1] = 0 if bs[-1] else 1; pc += 1
            elif op == 22: pc = a1
            elif op == 23: pc = pc + 2 if bs.pop() else a1
            elif op == 24: self.delay_add(self.time + a1, a2, pc); self.stats['delay'] += 1; pc += 3
            elif op == 25: pc += 2
            elif op == 26: self.durings.append((self.time + a1, a2, self.obj_of(pc))); self.stats['during'] += 1; pc += 3
            elif op == 27: s.append(self.time); pc += 1
            elif op == 28:
                n = a1; vals = s[len(s) - n:]; del s[len(s) - n:]
                self.msgs.append((vals[0], vals[1:])); self.stats['send'] += 1
                if self.log: self.log("  SEND %d %s" % (vals[0], [self.fmt(v) for v in vals[1:]]))
                pc += 2
            elif op in (29, 31, 32, 48, 49):
                bit = {29: 5, 31: 4, 32: 3, 48: 6, 49: 2}[op]
                self.bpush((self.vol_flags[a1] >> bit) & 1); pc += 2
            elif op == 30: f = self.vol_flags[a1]; self.bpush(f == 0 or (f & 9)); pc += 2
            elif op == 33: s.append(self.vol_count[a1]); pc += 2
            elif op == 34:
                for actor, flags in self.vol_list[a1 & 0xffffff]:
                    if not flags & 1:
                        self.globals[0] = actor; self.run(pc + 3)
                pc = a2
            elif op == 35: s.append(self.globals[a1]); pc += 2
            elif op == 36: self.globals[a1] = s.pop(); pc += 2
            elif op == 37: self.bpush(any(a == a2 for a, f in self.vol_list[a1])); pc += 3
            elif op == 38: self.bpush(not any(a == a2 for a, f in self.vol_list[a1])); pc += 3
            elif op in (39, 40, 41, 42):
                bit = {39: 5, 40: 4, 41: 6, 42: 3}[op]
                self.bpush((self.col_flags[a1] >> bit) & 1); pc += 2
            elif op == 43: pc = s.pop()
            elif op == 44: self.bpush(False); pc += 3
            elif op in (45, 46): fl = 2 if op == 45 else 1; self.bpush(any(a == a2 and f & fl for a, f in self.vol_list[a1])); pc += 3
            elif op == 47: self.stop = True; pc += 1
            elif op == 50: self.bpush(all(f & 1 for a, f in self.vol_list[a1]) and self.vol_list[a1]); pc += 2
            elif op in (51, 52): self.bpush(False); pc += 2
            elif op == 53: self.bpush(bool(self.col_list[a1]) and all(f & 4 for a, f in self.col_list[a1])); pc += 2
            elif op in (54, 55, 56): fl = {54: 2, 55: 4, 56: 1}[op]; self.bpush(any(a == a2 and f & fl for a, f in self.col_list[a1])); pc += 3
            elif op == 57: self.bpush(False); self.stats['cut'] += 1; pc += 2
            elif op == 58: v = s.pop(); self.bpush(v & self.msgmask[a1]); pc += 2
            elif op == 59: self.msgmask[a1] = 0; pc += 2
            elif op == 60: self.bpush(False); pc += 4
            elif op == 61: v = s.pop(); self.delay_add(self.time + v, a1, pc); pc += 2
            elif op == 62: v = s.pop(); s.append(self.rand() % (v if v > 0 else 1)); pc += 1
            else: raise RuntimeError("bad opcode %d at %d" % (op, pc))
        return pc

    @staticmethod
    def sg(v): return v - 0x100000000 if isinstance(v, int) and v >= 0x80000000 else v
    def fmt(self, v):
        if v >= 0x1000000: return "0x%x" % v
        return str(v)

    def run_object_init(self, o):
        """as 0x442819: NOP the leading JMP, run from start, restore"""
        start = self.objs[o]
        saved = (self.w[self.base + start], self.w[self.base + start + 1])
        self.w[self.base + start] = 0; self.w[self.base + start + 1] = 0
        self.globals[0] = 0  # engine sets 'this'? (0x5ce444 zeroed at init)
        self.run(start)
        self.wake = []
        self.w[self.base + start], self.w[self.base + start + 1] = saved

    def init(self):
        """0x4427e0: two init passes; messages of the first pass are discarded"""
        saved_log, self.log = self.log, None          # the exe discards (and never routes) pass-1 messages
        for o in range(self.nobj): self.run_object_init(o)
        self.log = saved_log
        pass1 = self.msgs; self.msgs = []
        self.delays = []; self.durings = []
        for o in range(self.nobj): self.run_object_init(o)
        return pass1, self.msgs

    def tick(self, dt=1):
        """0x442240"""
        self.time += dt
        ran = 0
        # delays (kept sorted by insertion)
        while self.delays and self.delays[0][0] <= self.time:
            t, target, owner = self.delays.pop(0); self.run(target); ran += 1
        # durings
        keep = []
        for t, target, owner in self.durings:
            if t < self.time: self.run(target); ran += 1
            else: keep.append((t, target, owner))
        self.durings = keep
        wake, self.wake = self.wake, []
        for o in reversed(wake):
            if self.stamp[o] == self.frame: continue
            self.stamp[o] = self.frame
            self.run(self.objs[o]); ran += 1
        self.frame += 1
        return ran

if __name__ == '__main__':
    src = sys.argv[1]; trace = '--trace' in sys.argv
    paths = [os.path.join(src, l, 'code') for l in sorted(os.listdir(src)) if os.path.exists(os.path.join(src, l, 'code'))] if os.path.isdir(src) else [src]
    allmsgs = Counter(); types = Counter(); argc = defaultdict(Counter)
    for p in paths:
        lvl = os.path.basename(os.path.dirname(p))
        vm = EkoVM(p, log=(lambda s: print(s)) if trace else None)
        try:
            p1, p2 = vm.init()
        except Exception as e:
            print("%-9s INIT FAILED: %s" % (lvl, e)); continue
        # a few ticks to exercise delays
        for _ in range(60): vm.tick(1)
        ids = Counter(m[0] for m in p2)
        for mid, args in p2:
            allmsgs[mid] += 1; argc[mid][len(args)] += 1
            if mid == 1200 and len(args) >= 2: types[args[1]] += 1
        print("%-9s objects=%4d pass1=%5d pass2=%5d distinct_ids=%3d delays=%d durings=%d ticks_ran=%d stack_left=%d" %
              (lvl, vm.nobj, len(p1), len(p2), len(ids), vm.stats['delay'], vm.stats['during'], sum(1 for _ in []), len(vm.stack)))
    print("\nmessage ids used at init (id: count, arg-count histogram):")
    for mid, c in sorted(allmsgs.items()):
        print("  %5d: %6d  args=%s" % (mid, c, dict(argc[mid])))
    print("\nSetTypeInstance types:", sorted(types.items()))
