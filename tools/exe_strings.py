import re, sys
d = open(sys.argv[1], 'rb').read()
ss = [s.decode('latin1') for s in re.findall(rb'[\x20-\x7e]{5,}', d)]
print(len(ss), "strings total")
kw = ['.tex', '.gel', '.ins', '.lit', '.col', '.vis', '.rck', '.bf', '.hnm', 'code', 'CM6', 'Direct', 'D3D', 'd3d',
      'ddraw', 'dsound', 'Glide', 'glide', 'OpenGL', '%s', '%d', 'Data', 'Common', 'Music', 'Logo', 'Game', 'mask',
      'Eko', 'EKO', 'Cryo', 'CRYO', 'error', 'Error', 'Erreur', 'RKET', 'level', 'Level', 'script', 'Script',
      'opcode', '.pdb', '.cpp', '.c', '.h', 'src', ':\\', '.dll', '.exe', 'Software', 'pdb', 'Assert', 'assert',
      '.ini', '.cfg', '.sav', 'Save', 'save', 'Version', 'version', 'Woody', 'woody', 'Buzz', 'Splinter', 'Knothead']
seen = set()
out = []
for s in ss:
    if s in seen:
        continue
    seen.add(s)
    if any(k in s for k in kw):
        out.append(s)
print(len(out), "matching strings")
for s in out:
    print(repr(s))
