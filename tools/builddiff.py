"""builddiff.py - what differs between two copies of the game data (e.g. the English 1.00 CD and a later release).

usage: python tools/builddiff.py <dirA> <dirB> [outdir]
  dirA/dirB = CD roots (with Common/, Data/, Logo/, Music.bf). Prints a per-file summary; for changed files it goes one
  level deeper with the project's parsers: .rck item by item, .ins model by model / instance by instance, .col sector
  lists, level scripts (code) as a unified diff of the ekodisasm listings (written to outdir when given).
"""
import difflib, hashlib, io, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rckparse, insparse, levelparse, ekodisasm

def files(root):
    out = {}
    for top in ("Common", "Data", "Game", "Logo"):
        for dp, _, fs in os.walk(os.path.join(root, top)):
            for f in fs:
                p = os.path.join(dp, f); out[os.path.relpath(p, root).replace("\\", "/")] = p
    if os.path.exists(os.path.join(root, "Music.bf")): out["Music.bf"] = os.path.join(root, "Music.bf")
    return out

def sha1(p):
    h = hashlib.sha1()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

def h(b): return hashlib.sha1(b if isinstance(b, bytes) else repr(b).encode()).hexdigest()[:10]

def diff_rck(a, b):
    A = rckparse.parse_rck(open(a, "rb").read()); B = rckparse.parse_rck(open(b, "rb").read())
    out = ["  stamp %08x -> %08x, counts %s -> %s" % (A["stamp"], B["stamp"], A["counts"], B["counts"])]
    for t in rckparse.TYPES:
        ia, ib = A["items"][t], B["items"][t]
        strip = lambda d: {k: v for k, v in d.items() if k != "reserved"}   # reserved = a stale pointer of the packing tool
        ch = [i for i in range(min(len(ia), len(ib))) if strip(ia[i]) != strip(ib[i])]
        if ch or len(ia) != len(ib):
            extra = ""
            if t == "string": extra = " (lengths " + ", ".join("%d:%d->%d" % (i, len(ia[i]["glyphs"]), len(ib[i]["glyphs"])) for i in ch[:12]) + (" ..." if len(ch) > 12 else "") + ")"
            if t == "image": extra = " (" + ", ".join("%d:%dx%d->%dx%d" % (i, ia[i]["w"], ia[i]["h"], ib[i]["w"], ib[i]["h"]) for i in ch[:12]) + ")"
            out.append("  %-6s %d -> %d items, changed: %s%s" % (t, len(ia), len(ib), ch[:40], extra))
    return out

def walk(x, y, path, out):
    """structural diff: appends (path, description) for every leaf that differs; byte blobs are compared as floats"""
    if isinstance(x, dict):
        for k in x:
            if k != "reserved": walk(x[k], y.get(k) if isinstance(y, dict) else None, path + "." + str(k), out)
    elif isinstance(x, (list, tuple)):
        if not isinstance(y, (list, tuple)) or len(x) != len(y):
            out.append((path, "length %d -> %s" % (len(x), len(y) if isinstance(y, (list, tuple)) else y))); return
        for i, (a, b) in enumerate(zip(x, y)): walk(a, b, "%s[%d]" % (path, i), out)
    elif isinstance(x, bytes):
        if x == y: return
        if not isinstance(y, bytes) or len(x) != len(y): out.append((path, "blob %d -> %s bytes" % (len(x), len(y) if isinstance(y, bytes) else y))); return
        n = len(x) // 4; fa = struct.unpack("<%df" % n, x[:4 * n]); fb = struct.unpack("<%df" % n, y[:4 * n])
        d = [(i, a, b) for i, (a, b) in enumerate(zip(fa, fb)) if a != b]
        i, a, b = max(d, key=lambda t: abs(t[1] - t[2]) if abs(t[1]) < 1e30 and abs(t[2]) < 1e30 else 0)
        out.append((path, "blob: %d of %d words differ, largest at word %d: %g -> %g" % (len(d), n, i, a, b)))
    elif isinstance(x, float):
        if x != y: out.append((path, "%g -> %g (delta %.3g)" % (x, y, y - x)))
    elif x != y: out.append((path, "%r -> %r" % (x, y)))

def inst_key(i): return i["id"]

def fmt_inst(i):
    return "pos (%.1f %.1f %.1f) q (%.3f %.3f %.3f %.3f) s (%.2f %.2f %.2f) traj %s vol %s col %s" % (
        *i["position"], *i["quat"], *i["scale"], "yes" if i["trajectory"] else "no",
        [hex(v) for v in i["volume_ids"]], [hex(v) for v in i["collision_ids"]])

def diff_ins(a, b):
    A = insparse.parse_ins(open(a, "rb").read()); B = insparse.parse_ins(open(b, "rb").read())
    out = []
    if A["nslots"] != B["nslots"]: out.append("  script slots %d -> %d" % (A["nslots"], B["nslots"]))
    ma, mb = A["models"], B["models"]
    if len(ma) != len(mb): out.append("  models %d -> %d" % (len(ma), len(mb)))
    for k in range(max(len(ma), len(mb))):
        if k >= len(ma): out.append("  model %d NEW: %d nodes, %d instances" % (k, len(mb[k]["nodes"]), len(mb[k]["instances"]))); continue
        if k >= len(mb): out.append("  model %d REMOVED" % k); continue
        x, y = ma[k], mb[k]
        d = []; walk({kk: vv for kk, vv in x.items() if kk != "instances"}, {kk: vv for kk, vv in y.items() if kk != "instances"}, "", d)
        if d:
            out.append("  model %d: %d fields differ (nodes %d->%d polys %d->%d anims %d->%d)" % (
                k, len(d), len(x["nodes"]), len(y["nodes"]), x["npolys_total"], y["npolys_total"], x["nanims"], y["nanims"]))
            for p, s in d[:6]: out.append("      %s: %s" % (p, s))
            if len(d) > 6: out.append("      ... %d more" % (len(d) - 6))
        ia = {inst_key(i): i for i in x["instances"]}; ib = {inst_key(i): i for i in y["instances"]}
        for key in sorted(set(ia) | set(ib)):
            s = "  model %3d inst slot %5d (tag %d)" % (k, key & 0xffffff, key >> 24)
            if key not in ia: out.append(s + " NEW     " + fmt_inst(ib[key]))
            elif key not in ib: out.append(s + " REMOVED " + fmt_inst(ia[key]))
            else:
                d = []; walk(ia[key], ib[key], "", d)
                if d: out.append(s + " " + "; ".join("%s: %s" % t for t in d[:4]) + (" ..." if len(d) > 4 else ""))
    ca = {c["id"]: c for c in A["cameras"]}; cb = {c["id"]: c for c in B["cameras"]}
    for key in sorted(set(ca) | set(cb)):
        if h([ca.get(key)]) != h([cb.get(key)]):
            out.append("  camera slot %d: %s -> %s" % (key & 0xffffff, ca.get(key, {}).get("position"), cb.get(key, {}).get("position")))
    return out

def diff_col(a, b):
    A = levelparse.parse_col(open(a, "rb").read())["sectors"]; B = levelparse.parse_col(open(b, "rb").read())["sectors"]
    out = ["  sectors %d -> %d" % (len(A), len(B))] if len(A) != len(B) else []
    for s in range(min(len(A), len(B))):
        sa = sorted(v["raw"] for v in A[s]); sb = sorted(v["raw"] for v in B[s])
        if sa != sb:
            add = sorted(set(sb) - set(sa)); rem = sorted(set(sa) - set(sb))
            out.append("  sector %d: +%s -%s" % (s, ["%d/%04x" % (v & 0xffff, v >> 16) for v in add][:10], ["%d/%04x" % (v & 0xffff, v >> 16) for v in rem][:10]))
    return out

def listing(p):
    s = io.StringIO(); ekodisasm.disasm(p, s); return s.getvalue().splitlines()

def diff_code(a, b, name, outdir):
    la, lb = listing(a), listing(b)
    # strip the word offsets so an insertion does not mark every later line as changed
    norm = lambda ls: [l.split(":", 1)[1] if l[:7].strip().isdigit() else l for l in ls]
    d = list(difflib.unified_diff(norm(la), norm(lb), "A/" + name, "B/" + name, lineterm="", n=4))
    if outdir:
        os.makedirs(outdir, exist_ok=True)
        open(os.path.join(outdir, name.replace("/", "_") + ".diff"), "w", encoding="utf-8").write("\n".join(d) + "\n")
    adds = sum(1 for l in d if l.startswith("+") and not l.startswith("+++")); rems = sum(1 for l in d if l.startswith("-") and not l.startswith("---"))
    out = ["  script: +%d -%d lines (%s)" % (adds, rems, la[0][2:] + " -> " + lb[0].split(";", 2)[-1].strip()), "  " + lb[1]]
    return out

def main():
    ra, rb = sys.argv[1], sys.argv[2]; outdir = sys.argv[3] if len(sys.argv) > 3 else None
    fa, fb = files(ra), files(rb)
    same = 0
    for k in sorted(set(fa) | set(fb), key=str.lower):
        if k not in fb: print("ONLY IN A", k); continue
        if k not in fa: print("ONLY IN B", k); continue
        if os.path.getsize(fa[k]) == os.path.getsize(fb[k]) and sha1(fa[k]) == sha1(fb[k]): same += 1; continue
        print("CHANGED %-28s %10d -> %10d" % (k, os.path.getsize(fa[k]), os.path.getsize(fb[k])))
        ext = os.path.splitext(k)[1].lower(); base = os.path.basename(k)
        try:
            if ext == ".rck": det = diff_rck(fa[k], fb[k])
            elif ext == ".ins": det = diff_ins(fa[k], fb[k])
            elif ext == ".col": det = diff_col(fa[k], fb[k])
            elif base == "code": det = diff_code(fa[k], fb[k], k, outdir)
            else:
                da, db = open(fa[k], "rb").read(), open(fb[k], "rb").read()
                n = min(len(da), len(db)); first = next((i for i in range(n) if da[i] != db[i]), n)
                det = ["  first differing byte at 0x%x" % first]
        except Exception as e:  # noqa: BLE001
            det = ["  (parse failed: %s)" % e]
        for l in det: print(l)
    print("identical files:", same)

if __name__ == "__main__":
    main()
