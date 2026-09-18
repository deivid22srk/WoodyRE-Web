"""Print a range of out/disasm_full.txt by address: drange.py START END [START END ...]"""
import sys, bisect
lines = open('out/disasm_full.txt', encoding='utf-8', errors='replace').read().splitlines()
addr = []
for l in lines:
    try: addr.append(int(l[:8], 16) if l[:8].strip() and l[0] != ';' and l[0] != 'l' else -1)
    except ValueError: addr.append(-1)
# monotone index of address lines
idx = [(a, i) for i, a in enumerate(addr) if a >= 0]
keys = [a for a, i in idx]
args = [int(x, 16) for x in sys.argv[1:]]
for s, e in zip(args[::2], args[1::2]):
    i0 = idx[bisect.bisect_left(keys, s)][1]
    j = bisect.bisect_left(keys, e)
    i1 = idx[j][1] if j < len(idx) else len(lines)
    print('\n'.join(l for l in lines[i0:i1] if l.strip()))
    print('----')
