"""For each given address, report the enclosing function (from disasm_full.txt markers), its size,
its callers, and the strings it references.  usage: funcinfo.py out/disasm_full.txt addr [addr...]"""
import sys, re, bisect
lines = open(sys.argv[1], encoding='utf-8').read().split('\n')
starts = []      # (addr, line index)
for i, l in enumerate(lines):
    m = re.match(r'; ===== function ([0-9a-f]{8})', l)
    if m: starts.append((int(m.group(1), 16), i))
start_addrs = [s[0] for s in starts]
callers = {}
for i, l in enumerate(lines):
    m = re.match(r'([0-9a-f]{8})  call    0x([0-9a-f]+)$', l)
    if m:
        callers.setdefault(int(m.group(2), 16), []).append(int(m.group(1), 16))
def enclosing(a):
    k = bisect.bisect_right(start_addrs, a) - 1
    return starts[k], (starts[k + 1] if k + 1 < len(starts) else (None, len(lines)))
for arg in sys.argv[2:]:
    a = int(arg, 16)
    (fa, fi), (na, ni) = enclosing(a)
    body = lines[fi:ni]
    strs = [l.split(';', 1)[1].strip() for l in body if '  ; ' in l]
    calls = sorted(set(int(m.group(1), 16) for l in body for m in [re.search(r'call    0x([0-9a-f]+)$', l)] if m))
    print("=== %08x is in function %08x (lines %d-%d, %d instr) ===" % (a, fa, fi, ni, ni - fi))
    cs = callers.get(fa, [])
    print("  callers (%d):" % len(cs), " ".join("%08x" % c for c in cs[:20]))
    def enc(c):
        return "%08x" % enclosing(c)[0][0]
    print("  caller functions:", " ".join(sorted(set(enc(c) for c in cs))[:20]))
    print("  calls:", " ".join("%08x" % c for c in calls[:40]))
    for s in strs[:25]: print("   ", s)
