import pefile, sys, re, struct
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
data = [s for s in pe.sections if s.Name.startswith(b'.data')][0]
dstart = img + data.VirtualAddress; dend = dstart + data.Misc_VirtualSize
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
# find "mov dword ptr [abs], imm" where abs in .data and imm in .text  -> function pointer table init
stores = []
for ins in md.disasm(code, tstart):
    if ins.mnemonic == 'mov' and ins.op_str.startswith('dword ptr [0x'):
        m = re.match(r'dword ptr \[0x([0-9a-f]+)\], 0x([0-9a-f]+)$', ins.op_str)
        if m:
            addr, imm = int(m.group(1), 16), int(m.group(2), 16)
            if dstart <= addr < dend and tstart <= imm < tend:
                stores.append((ins.address, addr, imm))
print("function-pointer stores into .data:", len(stores))
# group by contiguous table (addresses within 4 bytes of each other when sorted)
stores.sort(key=lambda s: s[1])
groups = []
for s in stores:
    if groups and s[1] - groups[-1][-1][1] <= 8:
        groups[-1].append(s)
    else:
        groups.append([s])
groups.sort(key=len, reverse=True)
for g in groups[:8]:
    addrs = [s[1] for s in g]
    print("  table %08x..%08x  %d entries  (init code around %08x..%08x)" % (min(addrs), max(addrs), len(g), min(s[0] for s in g), max(s[0] for s in g)))
    idx = sorted(set((s[1] - min(addrs)) // 4 for s in g))
    print("     slot indices used: %d distinct, min %d max %d" % (len(idx), idx[0], idx[-1]))
    # show a few handlers
    for s in sorted(g, key=lambda s: s[1])[:6]:
        print("     slot %3d -> %08x" % ((s[1] - min(addrs)) // 4, s[2]))
for tbl in (0x5cc360, 0x5d0418):
    hits = [s for s in stores if abs(s[1] - tbl) < 0x2000]
    print("stores near %08x: %d" % (tbl, len(hits)))
