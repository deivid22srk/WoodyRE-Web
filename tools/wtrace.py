"""wtrace.py - run Woody.exe (32-bit) under a minimal Win32 debugger and log the
script->engine message stream, VM ticks and level loads, in the same format as
tools/ekovm.py --trace / out/ekorun.exe so the traces can be diffed.

usage: python tools/wtrace.py <game dir> [--seconds N] [--out FILE]

Breakpoints (all addresses are fixed: the exe has no relocations / ASLR):
  0x401370  bool __stdcall RouteMessage(EkoMsg *rec)   -> "  SEND id [args]"
  0x442240  VmTick()                                   -> "TICK time=<1/100 s> frame=N"
  0x4424b0  VmLoad(const char *path)                   -> "LOAD path"
  0x4427e0  VmInit()                                   -> "INIT"
Also logs OutputDebugStringA output and unhandled exceptions.
"""
import ctypes, ctypes.wintypes as wt, sys, os, struct, time, argparse

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

# --- constants -------------------------------------------------------------
DEBUG_ONLY_THIS_PROCESS = 0x2
CREATE_NEW_CONSOLE = 0x10
INFINITE = 0xFFFFFFFF
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION_DEBUG_EVENT, CREATE_THREAD_DEBUG_EVENT, CREATE_PROCESS_DEBUG_EVENT, EXIT_THREAD_DEBUG_EVENT, \
    EXIT_PROCESS_DEBUG_EVENT, LOAD_DLL_DEBUG_EVENT, UNLOAD_DLL_DEBUG_EVENT, OUTPUT_DEBUG_STRING_EVENT, RIP_EVENT = range(1, 10)
STATUS_BREAKPOINT = 0x80000003
STATUS_SINGLE_STEP = 0x80000004
STATUS_WX86_BREAKPOINT = 0x4000001F
STATUS_WX86_SINGLE_STEP = 0x4000001E
CONTEXT_i386 = 0x10000
CONTEXT_CONTROL_INTEGER = CONTEXT_i386 | 0x1 | 0x2
TF = 0x100

# --- structures ------------------------------------------------------------
class STARTUPINFOA(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('lpReserved', wt.LPSTR), ('lpDesktop', wt.LPSTR), ('lpTitle', wt.LPSTR),
                ('dwX', wt.DWORD), ('dwY', wt.DWORD), ('dwXSize', wt.DWORD), ('dwYSize', wt.DWORD),
                ('dwXCountChars', wt.DWORD), ('dwYCountChars', wt.DWORD), ('dwFillAttribute', wt.DWORD),
                ('dwFlags', wt.DWORD), ('wShowWindow', wt.WORD), ('cbReserved2', wt.WORD),
                ('lpReserved2', ctypes.c_void_p), ('hStdInput', wt.HANDLE), ('hStdOutput', wt.HANDLE), ('hStdError', wt.HANDLE)]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wt.HANDLE), ('hThread', wt.HANDLE), ('dwProcessId', wt.DWORD), ('dwThreadId', wt.DWORD)]

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [('dwDebugEventCode', wt.DWORD), ('dwProcessId', wt.DWORD), ('dwThreadId', wt.DWORD), ('_pad', wt.DWORD),
                ('u', ctypes.c_byte * 176)]   # union is 8-byte aligned on x64 -> starts at offset 16   # large enough for every member of the union on x64

class WOW64_FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [('ControlWord', wt.DWORD), ('StatusWord', wt.DWORD), ('TagWord', wt.DWORD), ('ErrorOffset', wt.DWORD),
                ('ErrorSelector', wt.DWORD), ('DataOffset', wt.DWORD), ('DataSelector', wt.DWORD),
                ('RegisterArea', ctypes.c_byte * 80), ('Cr0NpxState', wt.DWORD)]

class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [('ContextFlags', wt.DWORD), ('Dr0', wt.DWORD), ('Dr1', wt.DWORD), ('Dr2', wt.DWORD), ('Dr3', wt.DWORD),
                ('Dr6', wt.DWORD), ('Dr7', wt.DWORD), ('FloatSave', WOW64_FLOATING_SAVE_AREA),
                ('SegGs', wt.DWORD), ('SegFs', wt.DWORD), ('SegEs', wt.DWORD), ('SegDs', wt.DWORD),
                ('Edi', wt.DWORD), ('Esi', wt.DWORD), ('Ebx', wt.DWORD), ('Edx', wt.DWORD), ('Ecx', wt.DWORD), ('Eax', wt.DWORD),
                ('Ebp', wt.DWORD), ('Eip', wt.DWORD), ('SegCs', wt.DWORD), ('EFlags', wt.DWORD), ('Esp', wt.DWORD), ('SegSs', wt.DWORD),
                ('ExtendedRegisters', ctypes.c_byte * 512)]
assert ctypes.sizeof(WOW64_CONTEXT) == 716

k32.CreateProcessA.argtypes = [wt.LPCSTR, wt.LPSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD, ctypes.c_void_p, wt.LPCSTR,
                               ctypes.POINTER(STARTUPINFOA), ctypes.POINTER(PROCESS_INFORMATION)]
k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wt.DWORD]
k32.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.FlushInstructionCache.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
k32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenThread.restype = wt.HANDLE
k32.Wow64GetThreadContext.argtypes = [wt.HANDLE, ctypes.POINTER(WOW64_CONTEXT)]
k32.Wow64SetThreadContext.argtypes = [wt.HANDLE, ctypes.POINTER(WOW64_CONTEXT)]
k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
k32.CloseHandle.argtypes = [wt.HANDLE]
THREAD_ALL_ACCESS = 0x1FFFFF

class Debugger:
    def __init__(self, exe, cwd, out):
        self.out = out
        si = STARTUPINFOA(); si.cb = ctypes.sizeof(si)
        pi = PROCESS_INFORMATION()
        cmd = ctypes.create_string_buffer(('"%s"' % exe).encode())
        ok = k32.CreateProcessA(exe.encode(), cmd, None, None, False, DEBUG_ONLY_THIS_PROCESS, None, cwd.encode(),
                                ctypes.byref(si), ctypes.byref(pi))
        if not ok: raise OSError(ctypes.get_last_error(), 'CreateProcess')
        self.pi = pi
        self.hproc = pi.hProcess
        self.threads = {pi.dwThreadId: pi.hThread}
        self.bps = {}          # addr -> original byte
        self.pending = {}      # tid -> addr to re-arm after single step
        self.handlers = {}
        self.nmsg = 0
        self.ntick = 0
        self.t0 = time.perf_counter()

    # -- memory helpers
    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.hproc, addr, buf, n, ctypes.byref(got)): return b''
        return buf.raw[:got.value]
    def write(self, addr, data):
        buf = ctypes.create_string_buffer(data, len(data)); n = ctypes.c_size_t()
        k32.WriteProcessMemory(self.hproc, addr, buf, len(data), ctypes.byref(n))
        k32.FlushInstructionCache(self.hproc, addr, len(data))
    def u32(self, addr):
        b = self.read(addr, 4); return struct.unpack('<I', b)[0] if len(b) == 4 else None
    def cstr(self, addr, maxlen=260):
        b = self.read(addr, maxlen); i = b.find(b'\0')
        return (b if i < 0 else b[:i]).decode('latin1', 'replace')

    # -- breakpoints
    def add_bp(self, addr, fn):
        orig = self.read(addr, 1)
        if len(orig) != 1: raise RuntimeError('cannot read %08x' % addr)
        self.bps[addr] = orig; self.handlers[addr] = fn
        self.write(addr, b'\xcc')
    def thread(self, tid):
        h = self.threads.get(tid)
        if h is None:
            h = k32.OpenThread(THREAD_ALL_ACCESS, False, tid); self.threads[tid] = h
        return h
    def get_ctx(self, tid):
        ctx = WOW64_CONTEXT(); ctx.ContextFlags = CONTEXT_CONTROL_INTEGER
        if not k32.Wow64GetThreadContext(self.thread(tid), ctypes.byref(ctx)):
            raise OSError(ctypes.get_last_error(), 'Wow64GetThreadContext')
        return ctx
    def set_ctx(self, tid, ctx):
        ctx.ContextFlags = CONTEXT_CONTROL_INTEGER
        if not k32.Wow64SetThreadContext(self.thread(tid), ctypes.byref(ctx)):
            raise OSError(ctypes.get_last_error(), 'Wow64SetThreadContext')

    def log(self, s):
        self.out.write(s + '\n')

    # -- main loop
    def run(self, seconds):
        ev = DEBUG_EVENT()
        deadline = time.time() + seconds if seconds else None
        armed = False
        while True:
            if deadline and time.time() > deadline:
                self.log('# timeout, terminating'); k32.TerminateProcess(self.hproc, 0); deadline = None
            if not k32.WaitForDebugEvent(ctypes.byref(ev), 200):
                continue
            code, tid = ev.dwDebugEventCode, ev.dwThreadId
            status = DBG_CONTINUE
            u = bytes(ev.u)
            if code == CREATE_PROCESS_DEBUG_EVENT:
                hthread = struct.unpack_from('<Q', u, 16)[0]
                self.threads[tid] = hthread
                self.log('# process %d created' % ev.dwProcessId)
            elif code == CREATE_THREAD_DEBUG_EVENT:
                self.threads[tid] = struct.unpack_from('<Q', u, 0)[0]
            elif code == EXIT_PROCESS_DEBUG_EVENT:
                self.log('# process exited code=%d msgs=%d ticks=%d' % (struct.unpack_from('<I', u, 0)[0], self.nmsg, self.ntick))
                k32.ContinueDebugEvent(ev.dwProcessId, tid, DBG_CONTINUE)
                break
            elif code == OUTPUT_DEBUG_STRING_EVENT:
                ptr, uni, ln = struct.unpack_from('<QHxxH', u, 0)
                s = self.read(ptr, ln)
                self.log('# ODS ' + (s.decode('utf-16le', 'replace') if uni else s.decode('latin1', 'replace')).rstrip())
            elif code == EXCEPTION_DEBUG_EVENT:
                exc_code, flags, rec, addr, nparams = struct.unpack_from('<IIQQI', u, 0)
                first = struct.unpack_from('<I', u, 152)[0]
                if exc_code in (STATUS_BREAKPOINT, STATUS_WX86_BREAKPOINT):
                    if addr in self.bps:
                        ctx = self.get_ctx(tid)
                        r = None
                        try: r = self.handlers[addr](ctx)
                        except Exception as e: self.log('# handler error at %08x: %r' % (addr, e))
                        if r == 'skip':
                            self.set_ctx(tid, ctx)      # handler redirected Eip; breakpoint stays armed
                        else:
                            self.write(addr, self.bps[addr])
                            ctx.Eip = addr; ctx.EFlags |= TF
                            self.set_ctx(tid, ctx)
                            self.pending[tid] = addr
                    else:
                        if not armed:
                            armed = True   # first (64-bit loader) breakpoint: image is mapped now, arm ours
                            for a, fn in list(self.pre_bps.items()): self.add_bp(a, fn)
                            self.log('# breakpoints armed')
                            if getattr(self, 'post_arm', None): self.post_arm(None)
                        # IAT slots are only resolved once the 32-bit loader has run (second breakpoint)
                        for slot, fn in list(self.iat_bps.items()):
                            target = self.u32(slot)
                            if target and target >= 0x10000000 and target not in self.bps:
                                self.add_bp(target, fn); self.log('# iat hook %08x -> %08x' % (slot, target))
                elif exc_code in (STATUS_SINGLE_STEP, STATUS_WX86_SINGLE_STEP):
                    a = self.pending.pop(tid, None)
                    if a is not None: self.write(a, b'\xcc')
                else:
                    params = struct.unpack_from('<%dQ' % min(nparams, 15), u, 32)
                    self.log('# EXCEPTION %08x at %08x first=%d params=%s' % (exc_code, addr, first, [hex(p) for p in params]))
                    status = DBG_EXCEPTION_NOT_HANDLED
            k32.ContinueDebugEvent(ev.dwProcessId, tid, status)

# --- Woody-specific handlers -------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir')
    ap.add_argument('--seconds', type=float, default=0)
    ap.add_argument('--out', default='-')
    ap.add_argument('--level', help='level name (W1A, K2A, ...): the House slot of the level table is redirected to it, so the game boots straight into that level')
    a = ap.parse_args()
    out = sys.stdout if a.out == '-' else open(a.out, 'w', encoding='utf-8', buffering=1)
    exe = os.path.join(a.gamedir, 'Woody.exe')
    dbg = Debugger(exe, a.gamedir, out)

    def on_route(ctx):
        rec = dbg.u32(ctx.Esp + 4)
        b = dbg.read(rec, 48)
        if len(b) < 48: return
        w = struct.unpack('<12I', b)
        mid, nargs = w[0], min(w[1], 10)
        args = ', '.join(("'0x%x'" % x) if x >= 0x1000000 else ("'%d'" % struct.unpack('<i', struct.pack('<I', x))[0]) for x in w[2:2 + nargs])
        dbg.log('  SEND %u [%s]' % (mid, args)); dbg.nmsg += 1
    def on_tick(ctx):
        dbg.ntick += 1
        dbg.log('TICK time=%s frame=%s t=%.3f' % (dbg.u32(0x5d0514), dbg.u32(0x4b3574), time.perf_counter() - dbg.t0))
    def on_load(ctx):
        dbg.log('LOAD %s' % dbg.cstr(dbg.u32(ctx.Esp + 4)))
    def on_init(ctx):
        dbg.log('INIT')
    if a.level:
        # level table at 0x4b12a0: 28 pointers to Data/LVL/LVL.gel path strings strings; slot 0 = House (the boot level)
        import pefile
        pe = pefile.PE(exe); img = pe.get_memory_mapped_image()
        table = struct.unpack_from('<28I', img, 0x4b12a0 - 0x400000)
        want = (r'\Data\%s\%s.gel' % (a.level, a.level)).encode()
        match = [p for p in table if img[p - 0x400000:p - 0x400000 + len(want) + 1] == want + bytes(1)]
        if not match: sys.exit('unknown level %s' % a.level)
        def redirect(ctx):
            dbg.write(0x4b12a0, struct.pack('<I', match[0])); dbg.log('# level slot 0 -> %s' % a.level)
        dbg.post_arm = redirect
    def on_dbgprint(ctx):          # 0x462c60: the (stubbed) internal logger; first arg is usually a format string
        p = dbg.u32(ctx.Esp + 4)
        if p and 0x400000 <= p < 0x600000:
            s = dbg.cstr(p, 200)
            if s and all(32 <= ord(c) < 127 for c in s): dbg.log('# LOG %s' % s)
    def stdcall_return(ctx, nargs, eax):
        ctx.Eip = dbg.u32(ctx.Esp); ctx.Esp += 4 + 4 * nargs; ctx.Eax = eax
        return 'skip'
    def on_createfile(ctx):
        dbg.log('# CreateFileA %s' % dbg.cstr(dbg.u32(ctx.Esp + 4)))
    def on_msgbox(ctx):
        dbg.log('# MessageBoxA text=%r caption=%r -> auto OK' % (dbg.cstr(dbg.u32(ctx.Esp + 8)), dbg.cstr(dbg.u32(ctx.Esp + 12))))
        return stdcall_return(ctx, 4, 1)
    def on_loadlib(ctx):
        dbg.log('# LoadLibraryA %s' % dbg.cstr(dbg.u32(ctx.Esp + 4)))
    def on_createproc(ctx):
        dbg.log('# CreateProcessA %s %s' % (dbg.cstr(dbg.u32(ctx.Esp + 4)) if dbg.u32(ctx.Esp + 4) else '', dbg.cstr(dbg.u32(ctx.Esp + 8)) if dbg.u32(ctx.Esp + 8) else ''))
    def on_exit(ctx):
        dbg.log('# ExitProcess %d' % dbg.u32(ctx.Esp + 4))
    dbg.pre_bps = {0x401370: on_route, 0x442240: on_tick, 0x4424b0: on_load, 0x4427e0: on_init, 0x462c60: on_dbgprint}
    dbg.iat_bps = {0x5eb49c: on_createfile, 0x5eb5ec: on_msgbox, 0x5eb400: on_loadlib, 0x5eb42c: on_createproc, 0x5eb3d0: on_exit}
    try:
        dbg.run(a.seconds)
    except KeyboardInterrupt:
        k32.TerminateProcess(dbg.hproc, 0)
    finally:
        out.flush()

if __name__ == '__main__':
    main()
