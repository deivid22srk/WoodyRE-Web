import pefile, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
target = int(sys.argv[2], 16); before = int(sys.argv[3]); after = int(sys.argv[4])
# find function start: scan backwards for a 'ret' followed by int3/nop padding or push ebp
start = target - 0x400
lines = []
for ins in md.disasm(code[start - tstart:start - tstart + 0x400 + after], start):
    lines.append((ins.address, "%-8s %s" % (ins.mnemonic, ins.op_str)))
idx = [i for i, (a, _) in enumerate(lines) if a == target]
if not idx:
    # resync: disassemble from a bit earlier is fine; just print window by address
    sel = [l for l in lines if target - before <= l[0] <= target + after]
else:
    i = idx[0]; sel = lines[max(0, i - before):i + after]
for a, t in sel:
    print("%08x  %s" % (a, t))
