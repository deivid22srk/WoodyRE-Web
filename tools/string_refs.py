import pefile, sys, re, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress
needles = sys.argv[2:]
addrs = {}
for n in needles:
    o = d.find(n.encode('latin1'))
    if o < 0: print("not found:", n); continue
    va = img + pe.get_rva_from_offset(o)
    addrs[va] = n
    print("string %r at VA %08x" % (n, va))
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
refs = []
for ins in md.disasm(code, tstart):
    if ins.mnemonic in ('push', 'mov', 'lea', 'cmp') and ('0x' in ins.op_str):
        for m in re.findall(r'0x([0-9a-f]+)', ins.op_str):
            v = int(m, 16)
            if v in addrs:
                refs.append((ins.address, addrs[v], ins.mnemonic + ' ' + ins.op_str))
for a, n, t in refs:
    print("  ref @%08x -> %r   (%s)" % (a, n, t))
