"""Dump an annotated disassembly of Woody.exe for an address range.

usage: disasm.py Woody.exe START END [out.txt]
Annotations: string references, jump-target labels, function boundaries (after ret + padding),
import thunks, and data-table references.
"""
import pefile, sys, re, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
out = open(sys.argv[4], 'w', encoding='utf-8') if len(sys.argv) > 4 else sys.stdout

# imports by IAT address
iat = {}
for imp in pe.DIRECTORY_ENTRY_IMPORT:
    for i in imp.imports:
        iat[i.address] = "%s!%s" % (imp.dll.decode().split('.')[0], i.name.decode() if i.name else "ord%d" % i.ordinal)

def va_ok(v):
    return img <= v < img + pe.OPTIONAL_HEADER.SizeOfImage

def str_at(va):
    try:
        off = pe.get_offset_from_rva(va - img)
    except Exception:
        return None
    s = d[off:off + 80]
    m = re.match(rb'[\x20-\x7e\r\n\t\xe0-\xff]{4,}', s)
    if m and s[len(m.group(0)):len(m.group(0)) + 1] in (b'\0', b''):
        return m.group(0).decode('latin1')
    return None

md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
ins_list = list(md.disasm(code[lo - tstart:hi - tstart], lo))
targets = set()
for ins in ins_list:
    if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
        m = re.match(r'0x([0-9a-f]+)$', ins.op_str)
        if m: targets.add(int(m.group(1), 16))
prev_ret = True
for k, ins in enumerate(ins_list):
    a = ins.address
    if prev_ret and ins.mnemonic not in ('nop', 'int3') and not (ins.mnemonic == 'int' and ins.op_str == '3'):
        out.write("\n; ===== function %08x =====\n" % a)
        prev_ret = False
    if a in targets:
        out.write("loc_%08x:\n" % a)
    note = ""
    for m in re.findall(r'0x([0-9a-f]{6,8})', ins.op_str):
        v = int(m, 16)
        if v in iat:
            note += "  ; " + iat[v]
        elif va_ok(v) and not (tstart <= v < tend):
            s = str_at(v)
            if s: note += "  ; %r" % s
    out.write("%08x  %-7s %s%s\n" % (a, ins.mnemonic, ins.op_str, note))
    if ins.mnemonic == 'ret' or (ins.mnemonic == 'jmp' and re.match(r'0x', ins.op_str) and ins_list[k + 1].mnemonic in ('nop', 'int3') if k + 1 < len(ins_list) else False):
        prev_ret = True
if out is not sys.stdout:
    out.close()
    print("written", sys.argv[4], len(ins_list), "instructions")
