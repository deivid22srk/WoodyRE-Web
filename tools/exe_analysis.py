import pefile, re, sys, struct
from collections import Counter
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

pe = pefile.PE(sys.argv[1])
d = pe.__data__
img = pe.OPTIONAL_HEADER.ImageBase
print("=== RTTI class names (.?AV...) ===")
names = sorted(set(m.decode('latin1') for m in re.findall(rb'\.\?AV[A-Za-z0-9_@]+@@', d)))
print(len(names), "classes")
for n in names:
    print("  ", n[4:-2])

print("=== other identifier-like strings (m_..., Set..., Get...) ===")
ss = set(s.decode('latin1') for s in re.findall(rb'[\x20-\x7e]{4,}', d))
ids = sorted(s for s in ss if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{3,}', s) and (s.startswith(('m_', 'Set', 'Get', 'Is', 'On', 'Level', 'Save', 'Play', 'Load', 'C')) ))
print(len(ids)); print(ids[:400])

text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
code = text.get_data()
base = img + text.VirtualAddress
print("=== function prologue count (push ebp; mov ebp,esp) in .text ===")
print(" 55 8B EC :", code.count(b'\x55\x8b\xec'))
print(" size .text bytes:", len(code))

print("=== quick disasm stats ===")
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.skipdata = True
calls = Counter(); n = 0; fpu = 0; sse = 0; mmx = 0
for ins in md.disasm(code, base):
    n += 1
    if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
        calls[ins.op_str] += 1
    if ins.mnemonic.startswith('f'):
        fpu += 1
    if ins.mnemonic.endswith(('ps', 'ss')) and ins.mnemonic not in ('pushps',):
        sse += 1
    if ins.mnemonic.startswith(('p', 'emms')) and 'mm' in ins.op_str:
        mmx += 1
print(" instructions:", n, " distinct direct call targets:", len(calls), " fpu:", fpu, " sse-ish:", sse, " mmx:", mmx)
print(" most-called targets:", calls.most_common(12))

print("=== data section: import thunks for D3D via QueryInterface? search IID_IDirect3D7 GUID ===")
# IID_IDirect3D7 = {F5049E77-4861-11d2-A407-00A0C90629A8}
g = struct.pack('<IHH', 0xF5049E77, 0x4861, 0x11d2) + bytes.fromhex('A40700A0C90629A8')
print(" IID_IDirect3D7 present:", g in d)
# IID_IDirect3DDevice7 {f5049e79-4861-11d2-a407-00a0c90629a8}
g = struct.pack('<IHH', 0xF5049E79, 0x4861, 0x11d2) + bytes.fromhex('A40700A0C90629A8')
print(" IID_IDirect3DDevice7 present:", g in d)
# IID_IDirect3DTnLHalDevice {f5049e78-...}
g = struct.pack('<IHH', 0xF5049E78, 0x4861, 0x11d2) + bytes.fromhex('A40700A0C90629A8')
print(" IID_IDirect3DTnLHalDevice present:", g in d)
g = struct.pack('<IHH', 0xF5049E7C, 0x4861, 0x11d2) + bytes.fromhex('A40700A0C90629A8')
print(" IID_IDirect3DHALDevice present:", g in d)
g = struct.pack('<IHH', 0xF5049E7D, 0x4861, 0x11d2) + bytes.fromhex('A40700A0C90629A8')
print(" IID_IDirect3DRGBDevice (software) present:", g in d)
# IID_IDirectDraw7 {15e65ec0-3b9c-11d2-b92f-00609797ea5b}
g = struct.pack('<IHH', 0x15e65ec0, 0x3b9c, 0x11d2) + bytes.fromhex('B92F00609797EA5B')
print(" IID_IDirectDraw7 present:", g in d)
