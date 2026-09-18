import pefile, re, sys, struct, uuid
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1])
img = pe.OPTIONAL_HEADER.ImageBase
d = pe.__data__
def rva2off(rva):
    return pe.get_offset_from_rva(rva)
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
tstart = img + text.VirtualAddress; tend = tstart + text.Misc_VirtualSize
code = text.get_data()

print("=== GUIDs in file (any 16-byte chunk with DCE variant, version 1/4, and plausible timestamp hi) ===")
known = {
 '15e65ec0-3b9c-11d2-b92f-00609797ea5b': 'IID_IDirectDraw7', 'f5049e77-4861-11d2-a407-00a0c90629a8': 'IID_IDirect3D7',
 'f5049e79-4861-11d2-a407-00a0c90629a8': 'IID_IDirect3DDevice7', 'f5049e78-4861-11d2-a407-00a0c90629a8': 'IID_IDirect3DTnLHalDevice',
 'f5049e7c-4861-11d2-a407-00a0c90629a8': 'IID_IDirect3DHALDevice', 'f5049e7d-4861-11d2-a407-00a0c90629a8': 'IID_IDirect3DRGBDevice',
 '06675a80-3b9b-11d2-b92f-00609797ea5b': 'IID_IDirectDrawSurface7', 'b2b8630-ad35-11d0-8ea6-00609797ea5b': 'IID_IDirectDraw4',
 '9c59509a-39bd-11d1-8c4a-00c04fd930c5': 'IID_IDirectDraw4', 'bb223240-e72b-11d0-a9b4-00aa00c0993e': 'IID_IDirect3D3',
 '5d3ee3f9-b6b5-11d2-b0ba-00c04fc5d0c6': 'IID_IDirectSound8?', '279afa83-4981-11ce-a521-0020af0be560': 'IID_IDirectSound',
 '279afa84-4981-11ce-a521-0020af0be560': 'IID_IDirectSound3DListener', '279afa85-4981-11ce-a521-0020af0be560': 'IID_IDirectSound3DBuffer',
 '5b21b8f0-e52d-11d2-4f4e-00c04ff70cc9': 'IID_IDirectInput7?', '9a4cb684-236d-11d3-8e9d-00c04f6844ae': 'IID_IDirectInput7A',
 '5944e662-aa8a-11cf-bfc7-444553540000': 'IID_IDirectInput2A', '57d7c6bc-2356-11d3-8e9d-00c04f6844ae': 'IID_IDirectInputDevice7A',
 '4cb6d1a4-1d9f-11d0-9b3a-0020af5ef5f4': 'GUID_SysMouse? (6f1d2b60-...)',
 '6f1d2b60-d5a0-11cf-bfc7-444553540000': 'GUID_SysMouse', '6f1d2b61-d5a0-11cf-bfc7-444553540000': 'GUID_SysKeyboard',
 'c8a2d4a4-3b3b-11d2-8a63-00c04fd6f5f2': 'IID_IDirect3DDevice7?', '4ab0e3d4-c17a-11d2-8e12-00c04fd8a2a1': '?',
 '4c5b49d0-c5b4-11d0-8e2c-00c04fb0a6b5': '?', '15e65ec1-3b9c-11d2-b92f-00609797ea5b': 'IID_IDirect3D7?',
}
found = {}
for o in range(0, len(d) - 16, 4):
    if d[o + 7] in (0x11,) and d[o + 8] & 0xc0 == 0x80:
        g = str(uuid.UUID(bytes_le=d[o:o + 16]))
        found[g] = o
print(len(found), "candidate GUIDs")
for g, o in sorted(found.items(), key=lambda x: x[1]):
    print("  %s @%06x %s" % (g, o, known.get(g, '')))

print("=== indirect jmp tables (switch statements) in .text, biggest first ===")
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True; md.skipdata = True
tables = []
for ins in md.disasm(code, tstart):
    if ins.mnemonic == 'jmp' and '*4 +' in ins.op_str and ins.op_str.startswith('dword ptr ['):
        m = re.search(r'\*4 \+ (0x[0-9a-f]+)\]', ins.op_str)
        if not m: continue
        tbl = int(m.group(1), 16)
        try:
            off = rva2off(tbl - img)
        except Exception:
            continue
        n = 0
        while off + 4 * n + 4 <= len(d):
            t = struct.unpack_from('<I', d, off + 4 * n)[0]
            if not (tstart <= t < tend): break
            n += 1
            if n > 1024: break
        tables.append((n, ins.address, tbl))
tables.sort(reverse=True)
for n, a, t in tables[:25]:
    print("  switch at %08x  table %08x  %4d cases" % (a, t, n))
print("total switch tables:", len(tables))

print("=== who calls the most-called helpers? (likely CRT/new/delete) ===")
