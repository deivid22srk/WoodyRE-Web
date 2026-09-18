"""Dump the disassembly of every message-handler case block (from docs/messages_raw.txt) plus the
inline-handled ids of the per-class handlers, into one file for reading.
usage: msgblocks.py out/disasm_full.txt docs/messages_raw.txt out/msg_blocks.txt"""
import sys, re, bisect
full, raw, outp = sys.argv[1:4]
lines = open(full, encoding='utf-8').read().split('\n')
addr_line = {}
for i, l in enumerate(lines):
    m = re.match(r'([0-9a-f]{8})  ', l)
    if m: addr_line[int(m.group(1), 16)] = i
starts = sorted(int(m.group(1), 16) for l in lines for m in [re.match(r'; ===== function ([0-9a-f]{8})', l)] if m)

def block(addr, exit_addr, maxlines=400):
    i = addr_line.get(addr)
    if i is None: return ["  (address %08x not at instruction boundary)" % addr]
    out = []
    for l in lines[i:i + maxlines]:
        if l.startswith('; ====='): break
        out.append(l)
        m = re.match(r'([0-9a-f]{8})  (\w+)\s*(.*)', l)
        if not m: continue
        mn, ops = m.group(2), m.group(3)
        if mn == 'ret': break
        if mn == 'jmp' and ops.startswith('0x'):
            t = int(ops, 16)
            if t == exit_addr or abs(t - addr) > 0x800: break
    return out

sections = []
cur = None
for l in open(raw, encoding='utf-8'):
    m = re.match(r'##### (.*) #####', l)
    if m: cur = {'title': m.group(1), 'cases': [], 'exit': None}; sections.append(cur); continue
    m = re.match(r'msg (\S+)\s+-> ([0-9a-f]{8})', l)
    if m and cur: cur['cases'].append((m.group(1), int(m.group(2), 16)))
exits = {'0x444870': 0x4455c6, '0x46cca0': 0x46cffa, '0x467fa0': 0x4686aa, '0x42d5e0': 0x42df1c}
with open(outp, 'w', encoding='utf-8') as f:
    for sec in sections:
        ex = next((v for k, v in exits.items() if k in sec['title']), None)
        f.write("\n\n############################ %s ############################\n" % sec['title'])
        for ids, addr in sec['cases']:
            f.write("\n=== msg %s @ %08x ===\n" % (ids, addr))
            f.write("\n".join(block(addr, ex)) + "\n")
    # inline class-handler cases: (handler, id-check address list) -> just dump those functions fully
    inline = [0x41a740, 0x44e8f0, 0x410070, 0x40e730, 0x40d530, 0x44cda0, 0x453730, 0x44f350, 0x451820, 0x451040, 0x451b50, 0x40c5e0, 0x403440]
    f.write("\n\n############################ per-class inline handlers (full functions) ############################\n")
    for fa in inline:
        k = bisect.bisect_right(starts, fa) - 1
        a0 = starts[k]; a1 = starts[k + 1] if k + 1 < len(starts) else None
        i0 = addr_line.get(a0); i1 = addr_line.get(a1, len(lines)) if a1 else len(lines)
        f.write("\n=== function %08x ===\n" % a0)
        f.write("\n".join(lines[i0:i1]) + "\n")
print("written", outp)
