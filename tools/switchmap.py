"""Map a compiled switch statement back to (key -> handler block) with the strings and calls in each block.

usage: switchmap.py Woody.exe JUMPTABLE NCASES EXIT_ADDR BASEKEY [BYTETABLE NKEYS]
  JUMPTABLE : address of the dword jump table
  NCASES    : number of entries in the jump table
  EXIT_ADDR : common exit label (block ends at 'jmp EXIT' or 'ret')
  BASEKEY   : key value of index 0 (message id)
  BYTETABLE : optional byte index table (MSVC compresses sparse switches: key -> case byte)
  NKEYS     : number of keys in the byte table
"""
import pefile, sys, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
def rd32(va): return int.from_bytes(d[pe.get_offset_from_rva(va - img):][:4], 'little')
def rd8(va): return d[pe.get_offset_from_rva(va - img)]
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
jt, ncases, exit_addr, basekey = int(sys.argv[2], 16), int(sys.argv[3]), int(sys.argv[4], 16), int(sys.argv[5])
bt = int(sys.argv[6], 16) if len(sys.argv) > 6 else None
nkeys = int(sys.argv[7]) if len(sys.argv) > 7 else ncases
iat = {}
for imp in pe.DIRECTORY_ENTRY_IMPORT:
    for i in imp.imports:
        iat[i.address] = i.name.decode() if i.name else "ord%d" % i.ordinal
def str_at(va):
    try: off = pe.get_offset_from_rva(va - img)
    except Exception: return None
    m = re.match(rb'[\x20-\x7e\xe0-\xff]{4,}', d[off:off + 90])
    return m.group(0).decode('latin1') if m else None
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
def block(addr, limit=400):
    """linear sweep from addr until jmp exit / ret / jmp back into table area"""
    out = []; strs = []; calls = []
    for ins in md.disasm(code[addr - tstart:addr - tstart + 4000], addr):
        out.append(ins)
        for m in re.findall(r'0x([0-9a-f]{6,8})', ins.op_str):
            v = int(m, 16)
            if v in iat: calls.append(iat[v])
            elif img <= v < img + pe.OPTIONAL_HEADER.SizeOfImage and not (tstart <= v < tend):
                s = str_at(v)
                if s and ins.mnemonic in ('push', 'mov'): strs.append(s)
        if ins.mnemonic == 'call' and ins.op_str.startswith('0x'): calls.append(ins.op_str)
        if ins.mnemonic == 'ret' or (ins.mnemonic == 'jmp' and ins.op_str == '0x%x' % exit_addr):
            break
        if ins.mnemonic == 'jmp' and ins.op_str.startswith('0x') and int(ins.op_str, 16) > addr + 0x800:
            break
        if len(out) >= limit: break
    return out, strs, calls
targets = [rd32(jt + 4 * i) for i in range(ncases)]
keys = {}
for k in range(nkeys):
    case = rd8(bt + k) if bt else k
    if case >= ncases: continue
    keys.setdefault(targets[case], []).append(basekey + k)
default = None
for t in targets:
    if len(keys.get(t, [])) > 8: default = t
for t in targets:
    if t == default:
        print("default case (%d keys) -> %08x" % (len(keys[t]), t)); continue
    if t not in keys: continue
    ins, strs, calls = block(t)
    ids = keys[t]
    print("msg %-14s -> %08x  (%d instr) calls=%s" % (",".join(map(str, ids)), t, len(ins), " ".join(dict.fromkeys(calls))[:110]))
    for s in dict.fromkeys(strs): print("        %r" % s[:100])
