"""Discover instance classes: SetTypeInstance(type) -> constructor -> vtable -> slot 22 (message handler)
-> its switch table -> message ids handled, with strings/calls per case.
usage: classmap.py Woody.exe"""
import pefile, sys, re, struct, subprocess
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
exe = sys.argv[1]
pe = pefile.PE(exe)
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
rdata = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
rstart = img + rdata.VirtualAddress; rend = rstart + rdata.Misc_VirtualSize
def rd32(va): return int.from_bytes(d[pe.get_offset_from_rva(va - img):][:4], 'little')
def rd8(va): return d[pe.get_offset_from_rva(va - img)]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
def str_at(va):
    try: off = pe.get_offset_from_rva(va - img)
    except Exception: return None
    m = re.match(rb'[\x20-\x7e\xe0-\xff]{4,}', d[off:off + 90])
    return m.group(0).decode('latin1') if m else None

def disasm(addr, n=4000):
    return list(md.disasm(code[addr - tstart:addr - tstart + n], addr))

def func_end(addr):
    """crude: first ret at depth 0 followed by padding, or 0x2000 bytes"""
    ins = disasm(addr, 0x3000)
    for k, i in enumerate(ins):
        if i.mnemonic == 'ret' and k + 1 < len(ins) and ins[k + 1].mnemonic in ('nop', 'int3'):
            return i.address + i.size
    return addr + 0x3000

def ctor_vtable(ctor):
    """last 'mov dword ptr [reg], imm' with imm in .rdata inside the ctor body (most-derived vtable)"""
    end = func_end(ctor)
    vt = None
    for i in disasm(ctor, end - ctor):
        m = re.match(r'dword ptr \[(e[a-z]{2})\], 0x([0-9a-f]+)$', i.op_str)
        if i.mnemonic == 'mov' and m:
            v = int(m.group(2), 16)
            if rstart <= v < rend and tstart <= rd32(v) < tend:
                vt = v
    return vt

def find_switch(fn):
    """find first 'jmp dword ptr [reg*4 + table]' in function; return (table, ncases, bytetable or None, ins list)"""
    end = func_end(fn)
    ins = disasm(fn, end - fn)
    for k, i in enumerate(ins):
        m = re.search(r'jmp\s+dword ptr \[(e[a-z]{2})\*4 \+ 0x([0-9a-f]+)\]', i.mnemonic + ' ' + i.op_str)
        if i.mnemonic == 'jmp' and '*4 +' in i.op_str:
            tbl = int(re.search(r'\+ (0x[0-9a-f]+)\]', i.op_str).group(1), 16)
            n = 0
            while tstart <= rd32(tbl + 4 * n) < tend and n < 300: n += 1
            # look back for byte table 'mov cl, byte ptr [eax + 0x...]' and 'add eax, -base' / 'sub eax, base' / cmp
            bt = None; base = 0; nkeys = n
            for j in range(k - 1, max(0, k - 12), -1):
                mm = re.match(r'(?:byte ptr \[e[a-z]{2} \+ 0x([0-9a-f]+)\])', i2 := ins[j].op_str.split(', ')[-1]) if ins[j].mnemonic == 'mov' else None
                if mm: bt = int(mm.group(1), 16)
                if ins[j].mnemonic == 'cmp' and re.match(r'e[a-z]{2}, 0x[0-9a-f]+$|e[a-z]{2}, \d+$', ins[j].op_str):
                    nkeys = int(ins[j].op_str.split(', ')[1], 0) + 1
                if ins[j].mnemonic in ('add', 'sub') and re.match(r'e[a-z]{2}, -?0x[0-9a-f]+$|e[a-z]{2}, -?\d+$', ins[j].op_str):
                    v = int(ins[j].op_str.split(', ')[1], 0)
                    if ins[j].mnemonic == 'add': v = (-v) & 0xffffffff if v > 0x7fffffff else -v
                    base = -v if ins[j].mnemonic == 'add' else v
                    base = abs(base) if ins[j].mnemonic == 'sub' else (0x100000000 - int(ins[j].op_str.split(', ')[1], 0)) if int(ins[j].op_str.split(', ')[1], 0) > 0x7fffffff else -int(ins[j].op_str.split(', ')[1], 0)
                if ins[j].mnemonic == 'dec': base += 1
            return tbl, n, bt, base, nkeys, ins
    return None

def block_info(addr, exit_candidates):
    strs = []; calls = []; cnt = 0
    for i in disasm(addr, 3000):
        cnt += 1
        for m in re.findall(r'0x([0-9a-f]{6,8})', i.op_str):
            v = int(m, 16)
            if img <= v < img + pe.OPTIONAL_HEADER.SizeOfImage and not (tstart <= v < tend):
                s = str_at(v)
                if s and i.mnemonic in ('push', 'mov'): strs.append(s)
        if i.mnemonic == 'call' and i.op_str.startswith('0x'): calls.append(i.op_str)
        if i.mnemonic == 'ret': break
        if i.mnemonic == 'jmp' and i.op_str.startswith('0x'):
            t = int(i.op_str, 16)
            if t in exit_candidates or t > addr + 0x600 or t < addr - 0x600: break
        if cnt > 300: break
    return cnt, list(dict.fromkeys(strs)), list(dict.fromkeys(calls))

# --- SetTypeInstance: type -> ctor ---
# reuse switchmap logic on 0x403e94 / byte table 0x403f3c, 121 keys, exit 0x403e3a
types = {}
for k in range(121):
    case = rd8(0x403f3c + k)
    if case >= 42: continue
    target = rd32(0x403e94 + 4 * case)
    # ctor = second call in block (after malloc 0x4995b2)
    calls = []
    for i in disasm(target, 600):
        if i.mnemonic == 'call' and i.op_str.startswith('0x'): calls.append(int(i.op_str, 16))
        if i.mnemonic == 'jmp' and i.op_str == '0x403e3a': break
        if len(calls) >= 2 and calls[0] == 0x4995b2: break
    size = None
    for i in disasm(target, 40):
        if i.mnemonic == 'push' and i.op_str.startswith('0x'): size = int(i.op_str, 16); break
    ctor = next((c for c in calls if c != 0x4995b2), None)
    types[k + 1] = (size, ctor)
seen = {}
print("type -> size, ctor, vtable, msg handler (vtable slot 22 @+0x58)")
for t, (size, ctor) in sorted(types.items()):
    if not ctor: continue
    vt = ctor_vtable(ctor)
    handler = rd32(vt + 0x58) if vt else None
    print("  type %3d  size=0x%-4x ctor=%08x vtable=%s handler=%s" % (t, size or 0, ctor, "%08x" % vt if vt else "?", "%08x" % handler if handler else "?"))
    if handler: seen.setdefault(handler, []).append(t)
print()
for handler, ts in seen.items():
    print("=" * 100)
    print("message handler %08x  (types %s)" % (handler, ts))
    sw = find_switch(handler)
    if not sw:
        cnt, strs, calls = block_info(handler, set())
        print("  no switch; %d instr; calls %s" % (cnt, " ".join(calls[:20])))
        for s in strs: print("     %r" % s)
        continue
    tbl, n, bt, base, nkeys, ins = sw
    print("  switch table %08x with %d cases; byte table %s; key base %d; nkeys %d" % (tbl, n, "%08x" % bt if bt else None, base, nkeys))
    targets = [rd32(tbl + 4 * i) for i in range(n)]
    keys = {}
    for k in range(nkeys if bt else n):
        case = rd8(bt + k) if bt else k
        if case >= n: continue
        keys.setdefault(targets[case], []).append(base + k)
    exits = set(targets)
    default = max(keys.items(), key=lambda kv: len(kv[1]))[0] if bt else None
    for tgt in targets:
        if tgt not in keys: continue
        if tgt == default and len(keys[tgt]) > 6:
            print("  default (%d keys) -> %08x" % (len(keys[tgt]), tgt)); continue
        cnt, strs, calls = block_info(tgt, exits)
        print("  msg %-16s -> %08x (%3d instr) calls=%s" % (",".join(map(str, keys[tgt])), tgt, cnt, " ".join(calls)[:100]))
        for s in strs[:6]: print("       %r" % s[:100])
