"""tracecmp.py - compare a live trace of the original game (tools/wtrace.py) with the C VM.

usage: python tools/tracecmp.py <live trace> [--data extract/Data] [--ekorun out/ekorun.exe]

For every level the live trace loaded, the tick times observed in the original are replayed in
ekorun (`@times` mode) and the two SEND streams are compared tick by tick. Messages 1200..1300
(SetTypeInstance and flags) never pass through the routing function that wtrace hooks, so they are
dropped from the emulator stream. Everything after the first engine->VM event we cannot reproduce
(volume enter/leave, collisions: they come from the player moving) is expected to diverge; the script
reports where.
"""
import sys, re, os, subprocess, argparse, tempfile

def parse_live(path):
    """-> list of (level_path, init_msgs, [(time, [msgs])...])"""
    levels = []; cur = None; phase = None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('LOAD '):
            cur = {'path': line[5:], 'init': [], 'ticks': []}; levels.append(cur); phase = 'init'
        elif line == 'INIT' and cur:
            phase = 'init'
        elif line.startswith('TICK ') and cur:
            m = re.search(r'time=(\d+)', line); cur['ticks'].append((int(m.group(1)), [])); phase = 'tick'
        elif line.startswith('  SEND ') and cur:
            (cur['init'] if phase == 'init' else cur['ticks'][-1][1]).append(line.strip())
    return levels

def run_emulator(ekorun, code, times):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as tf:
        tf.write('\n'.join(str(t) for t in times) + '\n'); tname = tf.name
    try:
        out = subprocess.run([os.path.abspath(ekorun), code, '@' + tname], capture_output=True, text=True).stdout
    finally:
        os.unlink(tname)
    # The original routes the init messages together with those of the first tick (the pump runs
    # after the VM tick of each frame), so the emulator's init messages are merged into tick 1.
    pre, ticks = [], []
    for line in out.splitlines():
        if line.startswith('TICK'):
            ticks.append([])
        elif line.startswith('  SEND'):
            mid = int(line.split()[1])
            if 1200 <= mid <= 1300: continue
            (ticks[-1] if ticks else pre).append(line.strip())
    if ticks: ticks[0] = pre + ticks[0]
    return [], ticks

def diff_lists(a, b, label):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            print('  %s: first difference at message %d:\n    live: %s\n    emu : %s' % (label, i, a[i], b[i])); return False
    if len(a) != len(b):
        print('  %s: live has %d messages, emulator %d (common prefix identical)' % (label, len(a), len(b)))
        extra = a[n:] if len(a) > n else b[n:]
        for e in extra[:5]: print('    %s: %s' % ('live' if len(a) > n else 'emu', e))
        return False
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace'); ap.add_argument('--data', default='extract/Data'); ap.add_argument('--ekorun', default='out/ekorun.exe')
    a = ap.parse_args()
    for lv in parse_live(a.trace):
        name = lv['path'].replace('\\', '/').split('/')[-2]
        code = os.path.join(a.data, name, 'code')
        times = [t for t, _ in lv['ticks']]
        print('== %s: live init %d msgs, %d ticks, %d tick msgs' % (name, len(lv['init']), len(times), sum(len(m) for _, m in lv['ticks'])))
        einit, eticks = run_emulator(a.ekorun, code, times)
        ok = diff_lists(lv['init'], einit, 'init')
        if ok: print('  init: identical (%d messages)' % len(einit))
        same = 0
        for i, ((t, lm), em) in enumerate(zip(lv['ticks'], eticks)):
            if lm == em: same += 1; continue
            print('  tick %d (time=%d) differs:' % (i, t)); diff_lists(lm, em, 'tick %d' % i)
            break
        else:
            print('  ticks: all %d identical (%d messages)' % (same, sum(len(m) for m in eticks)))
            continue
        print('  ticks identical before divergence: %d' % same)

if __name__ == '__main__':
    main()
