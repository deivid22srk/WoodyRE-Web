"""Which functions of Woody.exe are named nowhere in docs/, src/ or tools/ ("not analysed yet").

A function counts as covered when any address inside it (0x4xxxxx) appears in a doc, a source file or a tool.
Markers in disasm_full.txt that are only jump targets (no direct caller, no pointer to them anywhere in the image)
are merged into the preceding real function, so a switch case of a covered function is covered too.

usage: coverage.py out/disasm_full.txt game/Woody.exe [--min N] [--all]
  prints the totals for the game code (< 0x480000) and every uncovered real function of at least N instructions
  (default 40) with its callers, callees (with the docs that name them) and its strings; --all includes the
  library code above 0x480000 (Cryo sound/BigFile/HNM, window framework, CRT)."""
import sys, re, glob, bisect, collections
import pefile

args = [a for a in sys.argv[1:] if not a.startswith('--')]
min_n = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 40
if '--min' in sys.argv: args.remove(str(min_n))
show_all = '--all' in sys.argv
L = open(args[0], encoding='utf-8').read().split('\n')

starts = [(int(m.group(1), 16), i) for i, l in enumerate(L) for m in [re.match(r'; ===== function ([0-9a-f]{8})', l)] if m]
F = {}
for k, (a, i) in enumerate(starts):
    j = starts[k + 1][1] if k + 1 < len(starts) else len(L)
    ins = [l for l in L[i:j] if re.match(r'[0-9a-f]{8}  ', l)]
    F[a] = dict(n=len(ins), end=int(ins[-1][:8], 16) if ins else a,
                calls={int(m.group(1), 16) for l in ins for m in [re.search(r'call    0x([0-9a-f]+)$', l)] if m},
                strs=[l.split('  ; ', 1)[1].strip() for l in ins if '  ; ' in l])
A = sorted(F)
callers = collections.defaultdict(set)
for a, f in F.items():
    for c in f['calls']: callers[c].add(a)

pe = pefile.PE(args[1]); raw = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
def dw(i): return raw[i] | raw[i + 1] << 8 | raw[i + 2] << 16 | raw[i + 3] << 24
# switch jump tables (jmp dword ptr [reg*4 + T]): their slots point at case labels of the function that owns the jmp,
# so a case label referenced only from such a table is a jump target like any other, not a function of its own
jt_slots = set(); jt_data = set()   # jt_data: table addresses (a table the disassembler took for code is data, not a function)
for l in L:
    m = re.match(r'[0-9a-f]{8}  mov\s+\w+, byte ptr \[\w+ \+ 0x([0-9a-f]+)\]', l)
    if m: jt_data.add(int(m.group(1), 16))
    m = re.match(r'([0-9a-f]{8})  jmp     dword ptr \[\w+\*4 \+ 0x([0-9a-f]+)\]', l)
    if not m: continue
    at, t = int(m.group(1), 16), int(m.group(2), 16)
    jt_data.add(t); i = t - base
    while 0 <= i < len(raw) - 3:
        v = dw(i)
        if not at - 0x4000 < v < at + 0x4000: break   # case labels are code next to the jmp
        jt_slots.add(i); i += 4
ptrs = set()
for i in range(len(raw) - 3):
    if i in jt_slots: continue
    v = dw(i)
    if v in F: ptrs.add(v)
ptrs -= jt_data
real = sorted(a for a in A if a in callers or a in ptrs or a == A[0])
def owner(x): return real[bisect.bisect_right(real, x) - 1]

named = collections.defaultdict(set)
for fn in glob.glob('docs/*.md') + glob.glob('src/*.[ch]') + glob.glob('tools/*.py') + glob.glob('tools/native/*.c'):
    if fn.replace('\\', '/').endswith('tools/coverage.py'): continue
    for m in re.finditer(r'\b0x(4[0-9a-f]{5})\b', open(fn, encoding='utf-8', errors='ignore').read(), re.I):
        a = int(m.group(1), 16)
        k = bisect.bisect_right(A, a) - 1
        if k >= 0 and a <= F[A[k]]['end'] + 16: named[owner(a)].add(fn.replace('\\', '/').split('/')[-1])

size = collections.Counter()
for a in A: size[owner(a)] += F[a]['n']
game = [o for o in real if o < 0x480000]
tot = sum(size[o] for o in game); cov = sum(size[o] for o in game if o in named)
print('game code (< 0x480000): %d instr, named %d (%.1f%%), %d functions not named (%d instr)'
      % (tot, cov, 100.0 * cov / tot, sum(1 for o in game if o not in named), tot - cov))
lib = [o for o in real if o >= 0x480000]
print('library code (>= 0x480000): %d instr, named %d' % (sum(size[o] for o in lib), sum(size[o] for o in lib if o in named)))

def doc(a): return ','.join(sorted(named.get(a, ()))[:3])   # .get: indexing the defaultdict would mark a as named and hide it from the list
for o in (real if show_all else game):
    if o in named or size[o] < min_n: continue
    k = A.index(o); body = [o]
    for a in A[k + 1:]:
        if a in real: break
        body.append(a)
    cs = sorted({owner(c) for c in callers.get(o, ())})
    cl = sorted({c for a in body for c in F[a]['calls']})
    ss = [s for a in body for s in F[a]['strs'] if not s.startswith('0x')]
    print('%06x %4d  from: %s  to: %s  %s' % (o, size[o], ' '.join('%x(%s)' % (c, doc(c)) for c in cs[:3]) or ('(pointer)' if o in ptrs else '-'),
          ' '.join('%x(%s)' % (c, doc(c)) for c in cl[:5]), ' | '.join(dict.fromkeys(ss))[:120]))
