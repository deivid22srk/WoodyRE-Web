import pefile, sys, re, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data(); tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
ins_list = list(md.disasm(code[lo - tstart:hi - tstart], lo))
print("=== switch tables in %08x-%08x ===" % (lo, hi))
for ins in ins_list:
    if ins.mnemonic == 'jmp' and '*4 +' in ins.op_str:
        m = re.search(r'\*4 \+ (0x[0-9a-f]+)\]', ins.op_str)
        if not m: continue
        tbl = int(m.group(1), 16); off = pe.get_offset_from_rva(tbl - img); n = 0
        while True:
            t = struct.unpack_from('<I', d, off + 4 * n)[0]
            if not (tstart <= t < tend) or n > 600: break
            n += 1
        print("  %08x: %s  -> %d cases" % (ins.address, ins.op_str, n))
print("=== calls to imports / interesting ops in range: 'call dword ptr [reg*4' (function tables) ===")
for ins in ins_list:
    if ins.mnemonic == 'call' and '*4' in ins.op_str:
        print("  %08x: call %s" % (ins.address, ins.op_str))
print("=== function boundaries (ret followed by padding) count:", sum(1 for i in ins_list if i.mnemonic == 'ret'))
# print disassembly window requested
if len(sys.argv) > 5:
    a, b = int(sys.argv[4], 16), int(sys.argv[5], 16)
    for ins in ins_list:
        if a <= ins.address <= b:
            print("%08x  %-7s %s" % (ins.address, ins.mnemonic, ins.op_str))
