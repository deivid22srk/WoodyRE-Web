"""Disassembler for Eko Software "EKO CODE" bytecode (Woody Woodpecker: Escape from Buzz Buzzard Park, PC 2001).

Format (all little-endian uint32 "words"; offsets below are word indices from file start):
  0-1   "EKO CODE"
  2     nobj              number of script objects
  3     obj_table_off     word index of object table (always 12); table has nobj entries, each the
                          index (relative to code base) of the object's code. Code base = word 12 + nobj.
  4,5   nvars, vars_off   variables: 8-byte entries at vars_off (value, ptr), followed by per-variable
                          watcher lists [count][obj ids...] (objects re-run when the variable changes)
  6,7   nvol, vol_off     world_volume slots: 16-byte entries + per-slot lists
  8,9   nstr, str_off     string table (pointer array, then packed C strings)
  10,11 ncol, col_off     world_collision slots: 12-byte entries + per-slot lists
  ...   code, tables, then 0xFADEFADE marker, then compiler version (6) as last word.

Every object's code starts with "JMP <end>" so that jumping into it does nothing; at load time
the emulator temporarily replaces those two words with NOPs and runs the body once (init).

VM: integer stack, separate boolean stack, globals (0 = this). Handler table recovered from
Woody.exe at 0x442a30 (63 slots), interpreter loop at 0x4429f0.
"""
import struct, sys, os

# opcode: (mnemonic, number of operand words, description)
OPS = {
    0:  ("NOP", 0, ""),
    1:  ("HANG", 0, "returns same pc (unused)"),
    2:  ("END", 0, "stop execution"),
    3:  ("PUSH", 1, "push immediate"),
    4:  ("PUSHSTR", 1, "push string[n]"),
    5:  ("PUSHVAR", 1, "push var[n]"),
    6:  ("STOREVAR", 1, "var[n] = pop (wakes watchers)"),
    7:  ("ADD", 0, ""),
    8:  ("SUB", 0, ""),
    9:  ("MUL", 0, ""),
    10: ("DIV", 0, ""),
    11: ("NEG", 0, ""),
    12: ("TOBOOL", 0, "bpush(pop != 0)"),
    13: ("EQ", 0, "bpush(a == b)"),
    14: ("NE", 0, ""),
    15: ("GT", 0, ""),
    16: ("GE", 0, ""),
    17: ("LT", 0, ""),
    18: ("LE", 0, ""),
    19: ("OR", 0, "bool or"),
    20: ("AND", 0, "bool and"),
    21: ("NOT", 0, "bool not"),
    22: ("JMP", 1, "pc = base + n"),
    23: ("JF", 1, "if !bpop: pc = base + n"),
    24: ("DELAY", 2, "schedule jump to base+b after a ticks"),
    25: ("SKIP1", 1, "nop with one operand"),
    26: ("DURING", 2, "schedule (a ticks, target b) on second timer list"),
    27: ("PUSHTIME", 0, "push current time"),
    28: ("SEND", 1, "pop n values; queue message {id=first, args=rest} to engine"),
    29: ("VOL_FLAG5", 1, "bpush(volume[n].flags bit5)"),
    30: ("VOL_STATE", 1, "bpush(volume[n].flags == 0 || flags & 9)"),
    31: ("VOL_FLAG4", 1, ""),
    32: ("VOL_FLAG3", 1, ""),
    33: ("VOL_COUNT", 1, "push volume[n].count"),
    34: ("FOREACH", 2, "for each actor in volume[a]: this = actor; run body; end at base+b"),
    35: ("PUSHGLOBAL", 1, "push global[n] (0 = this)"),
    36: ("STOREGLOBAL", 1, ""),
    37: ("VOL_HAS", 2, "bpush(actor b in volume[a] list, flags&4/0x24)"),
    38: ("VOL_HASNOT", 2, "bpush(actor b not in volume[a] list)"),
    39: ("COL_FLAG5", 1, "collision[n] flags bit5"),
    40: ("COL_FLAG4", 1, ""),
    41: ("COL_FLAG6", 1, ""),
    42: ("COL_FLAG3", 1, ""),
    43: ("JMPPOP", 0, "pc = base + pop"),
    44: ("VOL_SEQ", 2, "bpush(volume[b].time - volume[a].time == 1)"),
    45: ("VOL_ACTOR_F2", 2, "bpush(actor b in volume[a] with flag 2)"),
    46: ("VOL_ACTOR_F1", 2, "bpush(actor b in volume[a] with flag 1)"),
    47: ("INVALID", 0, "default handler: stop"),
    48: ("VOL_FLAG6", 1, ""),
    49: ("VOL_FLAG2", 1, ""),
    50: ("VOL_ALL_F1", 1, "bpush(all actors in volume[n] have flag 1)"),
    51: ("COL_B3_BIT0", 1, ""),
    52: ("COL_B2_BIT0", 1, ""),
    53: ("COL_ALL_F4", 1, ""),
    54: ("COL_ACTOR_F2", 2, ""),
    55: ("COL_ACTOR_F4", 2, ""),
    56: ("COL_ACTOR_F1", 2, ""),
    57: ("CUT", 1, "keyword 'cut' (unimplemented): bpush(0)"),
    58: ("MSGTEST", 1, "bpush(pop & msgmask[obj n])"),
    59: ("MSGCLEAR", 1, "msgmask[obj n] = 0"),
    60: ("VOL_PAIR", 3, "bpush(actor c in volume[a] f1 and in volume[b] f4)"),
    61: ("DELAYPOP", 1, "schedule jump to base+n after pop ticks"),
    62: ("RANDOM", 0, "push rand() % pop"),
}

def load(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'EKO CODE', "not an EKO CODE file"
    n = len(d) // 4
    w = list(struct.unpack('<%dI' % n, d[:n * 4]))
    return w

def fmt_val(v):
    if v >= 0x01000000:
        return "0x%x" % v
    return str(v)

def disasm(path, out):
    w = load(path)
    nobj = w[2]; tab = w[3]
    nvars, vars_off = w[4], w[5]
    nvol, vol_off = w[6], w[7]
    nstr, str_off = w[8], w[9]
    ncol, col_off = w[10], w[11]
    base = tab + nobj
    objs = [w[tab + i] for i in range(nobj)]
    out.write("; %s\n; objects=%d vars=%d volumes=%d strings=%d collisions=%d code_base=word %d, end marker at %d, compiler v%d\n" %
              (os.path.basename(os.path.dirname(path)), nobj, nvars, nvol, nstr, ncol, base, str_off, w[-1]))
    # variables and watcher lists
    pos = vars_off + nvars * 2
    for i in range(nvars):
        cnt = w[pos]; lst = w[pos + 1:pos + 1 + cnt]; pos += 1 + cnt
        out.write("; var[%d] watchers(%d): %s\n" % (i, cnt, " ".join(map(str, lst))))
    # strings
    strings = []
    if nstr:
        raw = struct.pack('<%dI' % (len(w) - (str_off + nstr)), *w[str_off + nstr:])
        parts = raw.split(b'\0')
        strings = [p.decode('latin1') for p in parts[:nstr]]
        for i, s in enumerate(strings):
            out.write("; str[%d] = %r\n" % (i, s))
    code_end = vars_off  # code runs from base up to the variables table
    # object boundaries
    bounds = sorted(set(objs)) + [code_end - base]
    errors = 0
    for oi, start in enumerate(objs):
        end = bounds[bounds.index(start) + 1] if start in bounds else code_end - base
        out.write("\n; ---- object %d  (words %d..%d, abs %d..%d) ----\n" % (oi, start, end - 1, base + start, base + end - 1))
        pc = start
        while pc < end:
            op = w[base + pc]
            if op not in OPS:
                out.write("%6d: ??? %d\n" % (pc, op)); errors += 1; pc += 1; continue
            name, nargs, _ = OPS[op]
            args = w[base + pc + 1: base + pc + 1 + nargs]
            txt = "%6d: %-12s %s" % (pc, name, " ".join(fmt_val(a) for a in args))
            if name in ("JMP", "JF", "DELAYPOP") and args and not (0 <= args[-1] <= code_end - base):
                txt += "   ; BAD TARGET"; errors += 1
            if name in ("DELAY", "DURING", "FOREACH") and not (0 <= args[1] <= code_end - base):
                txt += "   ; BAD TARGET"; errors += 1
            if name == "PUSHSTR" and strings and args[0] < len(strings):
                txt += "   ; %r" % strings[args[0]]
            out.write(txt + "\n")
            pc += 1 + nargs
        if pc != end:
            out.write("; WARNING: object %d overran its end by %d words\n" % (oi, pc - end)); errors += 1
    return errors

if __name__ == '__main__':
    src = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    paths = []
    if os.path.isdir(src):
        for lvl in sorted(os.listdir(src)):
            p = os.path.join(src, lvl, 'code')
            if os.path.exists(p): paths.append(p)
    else:
        paths = [src]
    total = 0
    for p in paths:
        lvl = os.path.basename(os.path.dirname(p))
        if outdir:
            os.makedirs(outdir, exist_ok=True)
            f = open(os.path.join(outdir, lvl + '.ekoasm'), 'w', encoding='utf-8')
        else:
            f = sys.stdout
        e = disasm(p, f)
        total += e
        if outdir: f.close()
        print("%-10s errors=%d" % (lvl, e))
    print("total errors", total)
