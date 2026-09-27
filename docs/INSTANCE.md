# Instance base class: animation system, generic messages, movement

Source: disassembly of `game/Woody.exe` (image base 0x400000). All addresses are VAs.
Base class: ctor `0x42e1a0`, vtable `0x4aa31c`, message handler `0x42d5e0` (vtable[22]).
Global time `now` = float `[[0x509adc]+0x30]` (seconds); frame number = `[[0x509adc]+0]`.
Message record `rec` (edi in the handler): `rec[0]` = id, `rec+8` = instance, `rec+0xc` = arg1,
`rec+0x10` = arg2, `rec+0x14` = arg3, `rec+0x18` = arg4, `rec+0x1c` = arg5 (all i32).
Handler return value: `al = 0` = handled, `al = 1` = "not yet, offer again later" (12/13 only).

Constants (read from the exe): `0x4aa138` = 1/4096 (0.000244140625), `0x4a94f8` = `0x4aa0ac` = 0.01,
`0x4a94c4` = 0.001, `0x4a9004` = 0.0, `0x4a900c` = 1.0, `0x4a9864` = 15.0, `0x4a9014` = 0.5, `0x4aa38c` = 0.98.

Model's animation table: `S = inst+0xf8`, `S+4` = highest valid anim index (test `anim > S[4]` → error
"L'anim %d n'existe pas dans cet objet..." `0x42df0e`), `S+8` → array of 8 bytes per animation:
`{i32 nframes, i32 duration}`. Below: `L(a) = S->anim[a].duration / 4096.0` (seconds at speed 1).

## 1. Animation system

### 1.1 Fields
| field | type | meaning | evidence |
|---|---|---|---|
| +0x58 | u32 | frame number of the last clock update (clock ticks at most once per frame) | `0x43eeee` |
| +0x5c | u32 | index of this instance's first world matrix in `[0x509adc]+0xa0` | `0x43f2bc` |
| +0x60..0x68 | vec3 | world position of the skeleton root after animation: start at the first top-level node (`S+0x6c`, 1-based; **not** a node count), follow `N+0x80` (next_sibling) as long as the node is a 0x80 dummy, take the translation of its world matrix. Used for cell re-fixing `0x4077f0(&+0x60)`, light selection and the outline distance. Port: `ins_anim_centre()` (level.c) | `0x43f2f1..0x43f34e` |
| +0x88 | i32 | status; the clock sets it to 0 as soon as speed ≠ 0, unless it's 2 | `0x43ef8c`, `0x42d64d` |
| +0x9c | i32 | "end reached in this update": 0 at the start of every running update, 1 as soon as the phase goes outside [0,1) (wrap or end). Messages 1/2/3(n>10)/4 set it to 0. 12/13 wait until it's ≠ 0 | `0x43efb7`, `0x43f0c9`, `0x42d619` |
| +0xa0 | f32 | speed (sign = direction; 0 = stationary at `+0xac`) | `0x42e290` |
| +0xa4 | f32 | base speed; `0x42e290(v)` sets +0xa0 = +0xa4 = v; on a slot switch, +0xa0 = +0xa4 | `0x43f191` |
| +0xa8 | f32 | start time (s) of the current pass | |
| +0xac | f32 | current position in the animation in seconds-at-speed-1 = phase · L(slot0); at speed 0 this is the SOURCE of the phase | `0x43f208`, `0x43f227` |
| +0xb0 | i32 | slot0 = current animation | |
| +0xb4/+0xb8/+0xbc | i32 | slot1..3 = queue (−1 = empty) | |
| +0xc0 | i32 | 1 if slot0 == slot1 (i.e. "running in a loop"), otherwise 0; ctor −1. NOT a "done" flag | `0x43f246` |
| +0xd0 | u32 | `(1 << (int)(min(phase,1)·15 + 0.5)) << 16` (`_ftol` `0x499580` truncates: round half up): the phase bit that every collision test ANDs with the `.col` ref of the cell (FORMAT_TEX_COL_VIS_LIT.md 2, PERSO_MOVE.md 6.7) | `0x43f285` |

Ctor init `0x42e250`: speed 0, +0xc0 = −1, slot0 = 0, slot1..3 = −1. `0x42e210`: +0x84 = −1,
`+0xd8 &= 0xc0`, +0x6c = 0, +0x7c = +0x80 = 0, +0xc4 = −1, +0xd4 = 0.

### 1.2 Clock `0x43eee0(this, ext)` — pseudo-C
```c
void anim_clock(Instance *I, void *ext) {
    if (I->last_frame == g->frame) return;  I->last_frame = g->frame;
    if (I->traj) traj_update(I->traj, I);              // 0x437da0, see §3
    else if (ext) { /* pose from outside: +0xd0 = ext[0]; 0x43f360 copies matrices */ return; }
    M = diag(scale +0x4c..0x54) * rot(+0x28..0x48); T = pos(+0xc..0x14);
    float phase;
    if (I->speed != 0) {
        if (I->status88 != 2) I->status88 = 0;
        I->ended = 0;                                   // +0x9c
        float L = S->anim[I->slot[0]].duration / 4096.0f;
        if (I->speed > 0) phase = (now - I->start) / (L / I->speed);
        else              phase = 1.0f - (now - I->start) / -(L / I->speed);
        if (!(phase >= 0 && phase < 1)) {               // 0x43f0c3
            I->ended = 1;
            if (I->slot[1] != -1) {                     // switching to the next queued anim
                float ratio = (float)dur[slot0] / (float)dur[slot1];
                float n = 0;
                if (phase < 0)  { n = -floor(phase); phase += n; }
                if (phase >= 1) { n =  floor(phase); phase -= n; }
                phase *= ratio;                          // remaining time converted to the new animation
                if (n != 0) I->start += n * (L / fabs(I->speed));
                I->speed = I->base_speed;                // +0xa0 = +0xa4
                slot[0]=slot[1]; slot[1]=slot[2]; slot[2]=slot[3];   // slot[3] stays as is
            } else if (I->speed < 0) { if (phase < 0) { set_speed(I,0); phase = 0; } }
            else                     { if (phase > 1) { set_speed(I,0); phase = 1; } }
        }
        I->pos_ac = (S->anim[I->slot[0]].duration / 4096.0f) * phase;
    } else {
        phase = I->pos_ac / (S->anim[I->slot[0]].duration / 4096.0f);
    }
    I->c0 = (I->slot[0] == I->slot[1]);
    float frame = S->anim[I->slot[0]].nframes * phase;   // NOT clamped, goes to 0x43a2b0
    I->d0 = (1 << (int)(min(phase,1) * 15 + 0.5)) << 16;
    I->mat_index = g->nmat;  eval_tracks(S->nodes, S->nnodes, &M, frame, I->slot[0]);  // 0x43a2b0
    g->nmat += S->nmat;                                  // S+0x64
    if (!(I->flags8 & 0x20)) { I->center = world position of the last non-0x80 node; recell(I, &I->center); } // 0x4077f0
}
```
Consequences:
- **End of a one-shot animation** (slot1 == −1): phase is clamped to 1 (forward) or 0 (backward), speed → 0,
  `+0xac` = L resp. 0. So the last (first) frame stays put; there is no fallback to another animation.
- **Loop** = the same animation in slot0 and slot1 (message 4 fills all four). When switching over, `start`
  is shifted by a whole number of passes, so the loop is seamless and time-exact.
- There is **no** ping-pong: backward = negative speed (phase runs 1 → 0).
- The clock is called from render/update `0x42e2b0` if bit 0 of its argument is set (`0x42e305`).

### 1.3 PlayAnim messages (all in `0x42d5e0`)
`spd(a, dur) = L(a) / (dur·0.01)`: `dur` is the desired duration of one pass in 1/100 s; speed is thus
"animation length / desired duration" and one pass takes `L/spd = dur·0.01` seconds.

| id | args | address | behavior |
|---|---|---|---|
| 1 | anim, t | `0x42de97` | range check; `set_speed(0)`; `+0xac = L(old slot0) · t·0.001`; slot0 = anim; slot1..3 = −1; +0x9c = 0. So: set a still frame at position t (note: L of the OLD slot0 is used, and the scale is 0.001) |
| 2 | anim, flag, dur, off | `0x42de16` | slot0 = anim; `set_speed(spd(anim,dur))`; `start = now − off·0.001·dur·0.01` (start at fraction off/1000); flag == 0 → `set_speed(−speed)` (backward); slot1..3 = −1; +0x9c = 0. One-shot |
| 3 | anim, flag, dur | `0x42d62d` | see below |
| 4 | anim, flag, dur | `0x42d8e7` | slot0..3 = anim (loop); `set_speed(spd(anim,dur))`; start = now; flag == 0 → speed negative; +0x9c = 0 |
| 5 | – | `0x42d606` | `set_speed(0)`: freezes at the current `+0xac` |
| 12 | as 3 | `0x42d619` | if +0x9c == 0 → return 1 (retry); otherwise as 3 |
| 13 | as 4 | `0x42d8d3` | if +0x9c == 0 → return 1; otherwise as 4 |

`flag`: **1 = forward, 0 = backward** (no loop flag: whether it's a loop or one-shot is determined by the message id: 4/13 = loop, 2/3/12 = one-shot).

Message 3 (`0x42d62d`), pseudo-C:
```c
if (anim < 0 || anim > S->max_anim) { warn; return 0; }
if (dur <= 10) {                       // "jump straight to start/end" — the anim argument is NOT written to slot0
    if (I->status88 != 2) I->status88 = 0;
    set_speed(I, 0);
    I->pos_ac = flag ? L(I->slot[0]) : 0;          // flag=1 → last frame, flag=0 → first frame
    return 0;                                       // slots and +0x9c unchanged
}
if (I->slot[0] == anim && I->speed != 0) {         // same animation already playing: reverse / continue from the current phase
    float L = L(anim);
    if (I->speed > 0) {                            // running forward
        if (flag != 0) return 0;                   // already forward → do nothing
        float p = (now - I->start) / (L / I->speed);  p = 1 - (p - floor(p));   // remaining fraction
        set_speed(I, L / (dur*0.01f));
        I->start = now - (L / I->speed) * p;
        set_speed(I, -(L / (dur*0.01f)));          // now backward from the same pose
    } else {                                       // running backward
        if (flag != 1) return 0;
        float p = (now - I->start) / -(L / I->speed);  p = p - floor(p);
        set_speed(I, L / (dur*0.01f));
        I->start = now - (L / I->speed) * (1 - p);
    }
    return 0;                                      // slots unchanged
}
I->slot[0] = anim; set_speed(I, L(anim)/(dur*0.01f)); I->start = now;
if (flag == 0) set_speed(I, -I->speed);
I->slot[1] = I->slot[2] = I->slot[3] = -1;  I->ended = 0;
```
So message 3 is the "door" variant: play once, and if sent again with the flag reversed while it's still playing, it
reverses from the current pose (a door that closes again halfway).

## 2. Per-instance texture overrides: messages 15..19 (struct at `inst+0xd8`)

The 15..18 "transitions" from MESSAGES.md are **texture animation overrides**. The polygon renderer `0x43b3f0`
calls `0x47f290(this = material->texture, &inst+0xd8, out, material, now)` per textured polygon (`0x43c339`).

| field | set by | meaning |
|---|---|---|
| +0xd8 bits 0-2 | 16 / 18 | frame mode B (0 = off) |
| +0xd8 bits 3-5 | 15 / 17 | UV scroll mode A (0 = off) |
| +0xd8 bits 6-7 | – | preserved (19 and `0x42e210` do `&= 0xc0`) |
| +0xd9 | 16/18 arg1 (byte) | always 0xff in W1A; not read in `0x47f290` (open) |
| +0xda | 15/17 arg1 (byte) | same |
| +0xdc | 16/18 | start time B = now |
| +0xe0 | 16/18 arg3·0.01 | factor on the texture duration: period `P = tex+0x54 · (+0xe0)` |
| +0xe4 | 15/17 | start time A = now |
| +0xe8 | 15/17 arg3·0.01 | factor on the scroll speed (`tex+0x48`, `tex+0x4c`) |
| +0xec | 15 arg4·0.01 | duration T2 (s) of the finite scroll |

Frame mode B (only if `tex+0x58` (frame_count n) ≠ 1; t = now − (+0xdc); jump table `0x47f604`):
| mode | message (arg2) | frame |
|---|---|---|
| 1 | 16 (1) | one-shot forward: t ≥ P → n−1, otherwise `(int)(t/P·n)` |
| 2 | 16 (0) | one-shot backward: t ≥ P → 0, otherwise `(int)((1−t/P)·n)` |
| 3 | 16 (2) | one-shot back-and-forth: t ≥ P → 0, otherwise k = `(int)(t/P·(2n−1))`, k ≥ n → k = 2n−1−k |
| 4 | 18 (1) | loop forward: `(int)(frac(t/P)·n)` |
| 5 | 18 (0) | loop backward: `(int)((1−frac(t/P))·n)` |
| 6 | 18 (2) | loop back-and-forth: k = `(int)(frac(t/P)·(2n−1))`, mirrored if k ≥ n |
(`(int)` = `0x499580` ftol with FPU rounding; frames are 0x74 bytes each starting from the texture.)
Without an override (bits 0-2 = 0), the normal global texture animation applies.

UV scroll mode A (only if `tex+0x48` or `tex+0x4c` ≠ 0; su/sv = tex speed · (+0xe8); t = now − (+0xe4)):
mode 1 (15, arg2=1): offset += frac(su·min(t,T2)); mode 2 (15, arg2=0): offset −= …; mode 4 (17, arg2=1): endless
forward offset += frac(su·t); mode 5 (17, arg2=0): endless backward. The offset goes into `material+0xc` (u) and `+0x1c` (v);
the caller restores the old values after drawing (`0x43c369`).
Message 19 (`0x42db86`): `+0xd8 &= 0xc0` = both overrides off.

**Handlers 15..18, decompiled** (`0x42d9c3`, `0x42da32`, `0x42daae`, `0x42db0f`; `edi` = message, `+0xc` = arg1 …; `[0x4aa0ac]` = 0.01):
```c
case 15: inst->b_da = arg1;                                   /* byte, never read */
         if (arg2 == 1) d8 = (d8 & 0xcf) | 0x08;              /* bits 3-5 = 1 */
         else if (arg2 == 0) d8 = (d8 & 0xd7) | 0x10;         /* bits 3-5 = 2; any other arg2: bits unchanged */
         inst->t_a = now;  inst->f_a = arg3 * 0.01f;  inst->T2 = arg4 * 0.01f;       /* +0xe4, +0xe8, +0xec */
case 17: same with (d8 & 0xe7) | 0x20 (mode 4) and (d8 & 0xef) | 0x28 (mode 5); +0xec is left alone
case 16: inst->b_d9 = arg1; arg2 1/0/2 -> (d8 & 0xf9)|1, (d8 & 0xfa)|2, (d8 & 0xfb)|3 (modes 1/2/3); +0xdc = now; +0xe0 = arg3 * 0.01f
case 18: arg2 1/0/2 -> (d8 & 0xfc)|4, (d8 & 0xfd)|5, (d8 & 0xfe)|6 (modes 4/5/6); +0xdc, +0xe0 as 16
```
(`now` = `[0x509adc]+0x30`.) The and/or masks do not clear the whole field, but whatever it held before, it ends in the mode named, because each
mask clears exactly the bits of the field that its own pattern leaves 0. The scroll part of `0x47f290` (`0x47f495`..`0x47f5f7`, jump table
`0x47f61c` = `0x47f50a`, `0x47f530`, `0x47f5f7`, `0x47f556`, `0x47f5ad`): mode 3 (unreachable) does nothing; `frac` is `x − floor(x)` (`0x499ede`);
the function returns 1 whenever the scroll part is entered, so the caller restores `material+0xc/+0x1c`. The material in memory is 0x24 bytes:
`+0x00..+0x0c` = file floats f0, f3, f6, f9 (the u row, +0xc = the constant term f9), `+0x10..+0x1c` = f1, f4, f7, f10 (+0x1c = f10),
`+0x20` = texture (FORMAT_TEX_COL_VIS_LIT.md §1).

**Where it applies.** `0x47f290` has one caller, `0x43c341` in the model renderer `0x43b3f0`, in the loop over the collected **node polygons**
(`0x43c303`, drawn by `0x43d790`). The skinned triangles (`0x43e0f0`) take the texture straight from `material+0x20` (`0x43e13f`): neither
override reaches them (the port used to apply the frame override there too; corrected). World polygons never go through it. Without an
override bit, **nothing scrolls a model texture**: the scroll speed of the `.tex` group (`tex+0x48/+0x4c`, which FORMAT_TEX_COL_VIS_LIT.md
could not confirm) has no other reader (a scan of the float reads of `+0x48/+0x4c` in the renderer range found only camera and instance fields; uncertain). Only three groups have a speed at all: K3A 76, S3A 75, W3A 77 (32×32, 17 frames, 0.05/0.05), each on
one material used by model 34 / 31 / 32 (two textured triangles of a box, 4 instances, at y −1489 and −901/−916, under the slime surface).

**Who sends 15/17.** Nobody: every `SEND` of all 28 level scripts starts with a constant id (`tools/ekodisasm.py`), and none is 15 or 17
(16: 156× in K1A/S1A/W1A, K1R/K2R/K3R/S1R/S2R/S3R and KWS/SWS/WWS; 18: 68× in K1A/S1A/W1A/W1B and the three WS levels; 19: 14× in the WS levels). There is no other writer of `+0xda/+0xe4/+0xec`
in the instance base. So 15/17 are dead in the shipped game; ported for completeness.

**Port** (`src/instance.c` `inst_msg`, `src/render_gl.c` `tex_scroll`/`draw_node_polys`): `uv_mode/uv_t0/uv_fac/uv_t2` = bits 3-5, +0xe4, +0xe8,
+0xec; the offset is added to the planar UV of every vertex of a node polygon whose group has a speed (not to the helper-projected eye UVs,
whose reader of `material+0xc` has not been checked). Verification: as no level sends the message and the only scrolling textures sit under the
slime, a scratch build that forced `scroll_u = 0.05` on every group and `uv_mode 4`, factor 20 on every instance showed the textures of the W1A
start instances (the start pad, the green lamp bases) moving by a quarter tile between two shots 0.25 s apart, and the world unchanged. Test hook
for any message: `WOODY_MSGAT="T id a0 a1 …[; T id …]"`, e.g. `WOODY_MSGAT="1 17 216 255 1 100"` in W3A.

W1A example: `16 [inst, 0xffff, 1, 0x28]` = play the texture frames once forward over 0.4 × the texture duration;
`18 [inst, 0xffff, 1, 0x64]` = loop forward at normal speed.

## 3. Path follower (TRAJ at `inst+0x78`): messages 42, 43, 44, 46 — the only engine-driven movement in the base class

TRAJ object `T` (see FORMAT_INS §3): `T+0` = flags|npoints, `T+4` = start time, `T+8` = loop time (s), `T+0xc` = total
path length (`0x437ca0`, sum of the npoints−1 segments; that function also clears bits 17, 21, 22), `T+0x10` → points (16 B: unk, x, y, z).

| bit in `T+0` | meaning | set by |
|---|---|---|
| 0-15 | npoints | loader |
| 16 | "closed" from the file (NOT used by the path follower: there is no closing segment) | loader |
| 17 (0x20000) | active | 42/43 set, 44 (`0x437d90`) clears, end of a non-loop clears |
| 18 (0x40000) | backward (arg `a ≠ 1`) | 42/43 |
| 19 (0x80000) | loop | 43 always; 42 clears it |
| 20 (0x100000) | ping-pong (direction reverses on an odd loop count) | 43 if `c == 1`; 42 leaves it alone |
| 21 (0x200000) | orient instance along the path | 46 `a == 1` (`0x4381e0`) |
| 22 (0x400000) | orientation reversed | 46 `b == 1` |

| id | args | address | behavior |
|---|---|---|---|
| 42 | a, f | `0x42dcb2` → `0x437d10(a≠1, f·0.01)` | one-shot pass over the path in f/100 s; start = now; afterward `inst.pos` = the first point (a == 1) or last point (a ≠ 1) and `0x4077f0(0)` (re-determine cell) |
| 43 | a, f, c | `0x42dcdd` → `0x437d50(a≠1, f·0.01, c==1)` | like 42 but in a loop (loop time f/100 s), c == 1 = back-and-forth |
| 44 | – | `0x42dd9a` → `0x437d90` | stop (clears bit 17); position stays put |
| 46 | a, b | `0x42dc7d` → `0x4381e0` | orientation flags: bit 21 = (a == 1), bit 22 = (b == 1); 42 (`0x437d10`, clears bits 18-19) and 43 (`0x437d50`, clears 18 and 20) keep them. Sent only to the flying launchers (type 42) of S1R (172, 175, 178, 179, 181) and S3R (152, 156), always `[., 1, 1]`, followed by 42 `[., 0, 1]` (to the path end at once) and 42 `[., 1, T]` (fly the path). Ported (instance.c) |
All four do nothing if `inst+0x78 == NULL`.

Update `0x437da0(T, inst)`, called at the start of the animation clock (`0x43ef05`), so once per frame the instance
is updated:
```c
if (!(T->flags & ACTIVE)) return;
float t = now - T->start, frac; double ip = 0;
if (!(T->flags & LOOP) && t > T->dur) { frac = 1; T->flags &= ~ACTIVE; }
else frac = modf(t / T->dur, &ip);                       // 0x49a23b
bool fwd = !(T->flags & REVERSE);
if ((T->flags & PINGPONG) && ((int)ip & 1)) fwd = !fwd;
float d = frac * T->length;  int i0 = -1, i1 = -1;
if (fwd) for (i = 0, acc = 0, rem = d; i < n-1; i++) { len = |P[i+1]-P[i]|; acc += len; if (d <= acc) { pos = P[i] + unit(P[i+1]-P[i]) * rem; i0=i; i1=i+1; break; } rem -= len; }
else     for (i = n-1, acc = 0, rem = d; i > 0; i--) { len = |P[i-1]-P[i]|; acc += len; if (d <= acc) { pos = P[i] + unit(P[i-1]-P[i]) * rem; i0=i; i1=i-1; break; } rem -= len; }
// no hit (d past the end due to rounding) -> position unchanged, i0 = i1 = -1
inst->pos = pos;                                          // +0xc..0x14
if ((T->flags & ORIENT) && i0 != -1) {
    vec3 dir = (T->flags & ORIENT_REV) ? P[i1]-P[i0] : P[i0]-P[i1];  dir.y = 0; normalize(dir);
    row0 = cross(dir, (0,1,0)); row1 = dir; row2 = (0,1,0);           // -> inst+0x28, +0x34, +0x40 (0x41af10)
}
```
Units: time in 1/100 s, positions are the path points from the `.ins` (world coordinates). No acceleration/easing.
The test is `d <= cumulative length` (`0x437ecb` / `0x438037`); the remainder `rem` is tracked separately on the FPU
stack. Net effect: constant speed along the arc length, linear per segment. `inst->pos` is only written on a hit.

W1A: `43 [inst, 1, 2500, 0]` = loop of 25 s forward; type 51: `43 [inst, 1, 550, 1]` = back-and-forth, 5.5 s per one-way trip.

## 4. Message 6 (show/hide) and the world cell

`0x42d985`: `on ≠ 0` and `inst+0x1c < 0` → `0x407790(0)`; `on == 0` and `inst+0x1c ≥ 0` → `0x407850()`.
- `0x407850`: removes the instance from the singly linked list of its cell (`cell+0x44` = head, `inst+0x24` = next),
  sets `+0x24 = 0`, `+0x1c = −1`, `+0x18 = −1`.
- `0x407790(p)`: only if `+0x1c == −1`: `+0x1c = 0x4081c0(world, p ? p : &inst.pos)` (find cell), `+0x18 = 0x40a0c0(p, −1)`,
  and if the cell ≠ −1: link it in at the front of the cell list.
- `0x4077f0(p)` = hide + show again (re-cell after movement).
The renderer/updater `0x42e2b0` returns immediately if `+0x1c == −1` (`0x42e2c3`): no clock, no drawing, no volume/press update.
Everything that iterates `cell+0x44` (drawing, player collision against press nodes) no longer sees a hidden instance.
Hidden = invisible AND without collision. Animation time keeps running (the clock uses absolute time).

### 4.1 The per-frame instance list `world+0x60/+0x64` (`0x42a980` → `0x42a840`)

World = `[0x509adc]` = `App+0x28`. `+0x64` = array of `Instance*`, `+0x60` = count, allocated once as `malloc(0x400)` =
**256 pointers** (`0x42a4f9..0x42a50c`); `0x42a931` appends **without a bound** (a frame with more than 256 would overwrite the
heap; the port grows its array and `WOODY_VISLOG` reports such a frame - none was seen in the levels tried, max ≈ 120 in W2D).
Built once per frame at frame step 9 (`0x401c06..0x401c63`, PERSO_FRAME.md §1) from the camera position `CamMgr+0x1d0`
(= the camera of the previous frame's camera update), before every Think; `0x42a980(&campos, race)` with `race` =
`World+0xc0` (the region list of SetRaceInfo, RACE.md §2.1) when the Perso's subtype is 4 or 5, otherwise NULL.

```c
void World_BuildList(World *w, vec3 *cam, int *race)                    /* 0x42a980 */
{
    w->nsec /*+0x48*/ = w->ngrp /*+0x54*/ = w->n /*+0x60*/ = 0;          /* the list is emptied here and nowhere else (and in the reset 0x42a5e0) */
    g_secStamp /*[0x4c3bac]*/++; g_grpStamp /*[0x4c4ca8]*/++; g_instStamp /*[0x4c4c08]*/++;
    Cell *leaf = kd->cells[0x408180(cam)];
    int flag = race && race[0] != -1;                                   /* 0x42a9d3..0x42a9f8: flag for 0x42a840 */
    if (!flag) race = NULL;
    for (k < leaf->nids /*+0x40*/) {                                    /* message 34 (§10.1): volume instances of the camera's leaf */
        I = kd->obj[leaf->ids[k] & 0xffff];
        if ((I->flags8 & 0x1f) != 1) continue;
        I->vtbl[2](I, 1);                                               /* clock / pose */
        if (0x4300c0(I, cam)) for (l = I->links /*+0xd4*/; l; l = l->next) l->inst->stamp20 = g_instStamp;
    }
    VisEntry *e = 0x408210(cam);                                        /* sector s = 0x4081c0(cam), g = 0x40a0c0(cam, -1): the entry of s
                                                                         * whose id == g, else the first entry (RACE.md 2.1) */
    if (!race) for (pair in e) if (kd->groups[pair.grp].stamp /*+0*/ != g_grpStamp) {  /* 0x42aa9b: every pair's GROUP */
        w->grp[w->ngrp++] = pair.grp; kd->groups[pair.grp].stamp = g_grpStamp; }
    else { g = 0x40a0c0(cam, -1); if (g != -1) {                         /* 0x42aadf: the camera's group and the list entry after it */
        p = race; while (*p != g) p++;                                  /* no bound: runs past the -1 terminator (+0xd4) if g is not in the list */
        mark(g); if (p[1] != -1) mark(p[1]); } }
    for (pair in e) { S = kd->sectors[pair.sec];                        /* 0x42ab60: every pair's SECTOR */
        if (S->stamp /*+4*/ == g_secStamp) continue;
        w->sec[w->nsec++] = pair.sec; S->stamp = g_secStamp;
        0x42a840(S, flag); }
}
void Sector_ListInstances(Sector *S, int flag)                          /* 0x42a840 */
{
    for (I = S->head /*+0x44*/; I; I = I->next /*+0x24*/) {             /* the sector's chain: instances in the world (+0x1c = S) */
        if (I->grp18 == -1 || kd->groups[I->grp18].stamp != g_grpStamp) continue;   /* 0x42a858..0x42a87d: floor group marked */
        if ((I->flags8 & 0x1f) == 1 && I->status88 == 1) {             /* kind 1 with a cached bounding sphere (§6) */
            if (!0x437b00(camRepere /*[0x509adc]+8*/, &I->sphere /*+0x8c*/, I->sphere.r /*+0x98*/)) I->stamp20 = g_instStamp;   /* frustum */
            else if (flag && |camPos - I->sphere.c|^2 + r^2 > 1.21e8f /*0x4aa2f4, 11000^2*/) I->stamp20 = g_instStamp;             /* race only */
        }
        if (I->stamp20 == g_instStamp) continue;                        /* 0x42a92f: frustum / distance / message-34 link / already in */
        w->list[w->n++] = I;                                            /* 0x42a931, no bound */
        if (!(I->flags8 & 0x20)) I->vtbl[2](I, 0x81);                    /* clock (the Perso, enemies and boards have 0x20: drawn elsewhere / below);
                                                                         * its re-cell puts I in front of this chain unless the pose cache hits
                                                                         * (MODEL_RENDER.md 9.1: stationary opaque instances keep their place) */
        I->stamp20 = g_instStamp;
    }
}
```
* **Floor group** `+0x18` = `0x40a0c0(p, −1)` (RACE.md §2.1): the GEL section-3 group of the floor polygon under the point, taken by
  `0x407790` together with the sector `+0x1c` = `0x4081c0(p)` when the instance enters the world (loader `0x4288cf`: `p` = `inst.pos`)
  and by every re-cell `0x4077f0` - the clock after each pose (`0x43f2ed`: the animated root `+0x60`, unless flag 0x20), an enemy after a
  move (its collision centre `pos + (0, h/2, 0)`, ENEMY.md §3.3/§5.1; PostLoad `0x419e4d` sets flag 0x20 on enemies). An instance with
  **no floor under that point** (`+0x18 = −1`) is **never listed**. See §4.2 for when which point is used and the instances concerned.

* **Sphere test** `0x437b00(repere, c, r)`: camera-space `x', y', z'` of the rows of `Repere+0x30..0x5c` (x/sx, y/sy, z·zoom,
  CAMERA.md §5.2), visible ⇔ `|x'| ≤ z' + 1.4142·r/sx` and `|y'| ≤ z' + 1.4142·r/sy` (`0x4aa3d4`); no near/far plane. It only
  concerns **stationary** instances: `+0x88 = 1` is set by the draw `0x42eec9..0x42f008` when the clock speed `+0xa0` is 0,
  flag 0x20 is off, `+0x84 ≠ 0` and SetFlags bit 1 is off (§6); a path (`.ins` TRAJ: loader `+0x88 = 2`), classes 50-52 (`0x450db2`)
  and the launcher 42 (`0x45223e`) are 2 = never cached. Sphere = centre ⅛ × the sum of the bounding-box node's points (model
  `+0x24`, 1-based), radius = distance to its last point. The clock (`0x43ef95`) or an animation message (`0x42d656`) resets 1 → 0.
  Moving instances and actors are therefore never frustum-tested.
* The **11000 test** (`dist² + r² > 1.21e8`) only runs in the races (flag = race list given and not empty).

**Readers** (all of `[0x509adc]+0x60/+0x64`; `0x42b450` is a plain count getter):
1. `0x401c68..0x401cbc`: the skeleton list `0x4c3bb4` (kind 1, flag 0x20, `+0xf8→+0x58`).
2. **`0x42b400(dt)`** (frame step 17, `0x401d78`, runs while paused too): `vtbl[3]` = **Think** of every listed instance, and for
   kind 2 (`.lit` light records) the flare `0x474a90`. This is the **only** caller of `vtbl[3]`: enemies and all three bosses
   (`0x41a320`, ENEMY.md §3.1), the ambient volume 90 (`0x472560`), the bonus halos (BONUS.md §3.1), the carousel figures (110), the
   classes of OBJECTS.md §2 ... A hidden (§4) or unlisted instance does not think. The count is read once before the loop.
3. `0x42b380(World, Perso)`: `vtbl[2](5 or 7)` for every listed instance except the Perso (which it draws first): draw + shadow pass.
4. The sound Update `0x401ee7` → `mgr->vt[4](list, n, 0)` (SOUND.md §2.2 step 3): 3D voices of unlisted owners fade out / are cut.

**Port** (`rnd_instance_list`, `src/render_gl.c`; called at the start of the frame from `main_engine.c` with the current camera,
race list `Renderer.race` while `race_char`): sectors and groups from `vis_entry` (`0x408210`), per instance the sector and
floor group (`gel_floor_group` = `0x40a0c0`) of its cell point (`Instance.cell_*`), celled exactly as §4.2: the `.ins` position
at load, on a show and on a 1200 relink (`load_point`), the animated root `ins_anim_centre` (`+0x60`) on each clock run that
misses the pose cache (`chain_clock`), and for the actors (`ins_flag20`: the Perso, enemies with `cell_dy = h/2` from
`enemy_place`, bosses, the race board, rocket, cannon and bombs) their mover's point whenever it moved; the flag-0x20 links of
messages 61/62 (`Instance.cell_fixed`) keep the `.ins` position. The clock runs for every listed instance, the camera leaf's
`.col` objects, and every instance a collision query tests (`gel_col_clock`, installed by `rnd_instance_list`, called from
`level.c` `col_candidate` before the phase-mask test); the pose cache is stored only by the list's draw. The list ORDER of the
original (sector chains walked in `.vis` pair order, every clock run re-links the instance in front of its chain; MODEL_RENDER.md
§9.1); message-34 links (`link_inside`); the stationary sphere test on the port's frustum (aspect of the
window, not 4:3) and the race 11000 test. Results: `Instance.listed` (Think / sound) and `Instance.in_zone` (sector + group + link:
the draw gate of base-class instances and enemies in `rnd_frame`, whose own cone test stays on this frame's camera).
Readers switched: enemy / boss Updates and the actor list 1 (`game_enemy_thinks`), `snd_owner_active`, `ambient_update`, the
bonus halos, the Perso's special attack target list. Port simplifications: counts a sphere as cached from the first
stationary frame, when there is no `.vis` (or the camera is outside every sector) lists everything, and a show (message 6 on)
of an instance that is still visible in the port's terms but out of the world (its clock found no sector) does not re-cell it.
`WOODY_VISLOG=1`: once a second camera sector / floor group / `.vis` entry, list size and max, and why the others are out
(sector, group, no floor, link, frustum, race distance); `=2` also the ids and every actor; `=3` the instances without a floor
group; `=4` also the whole list every frame; `=5` once per level the instances whose `.ins` position or root has no floor
group (§4.2).

### 4.2 Which point an instance is celled at, and the instances without a floor group

`0x40a0c0(p)` (`0x40a0fc..0x40a233`): in the kd leaf of `p`, every face with `n.y ≥ 0` (`0x40a141`), `d = n·p + D > 0` strictly
(`0x40a16a`: a point exactly on the plane is not above it), `d ≤ n.y·(p.y − cell.y0) + 0.001` (`0x40a17b`, `0x4a94c4`) and `p`
inside the face in xz (all edge cross products ≥ 0, `0x40a1b3..0x40a1f8`); the LAST such face of the leaf wins (`0x40a202`); none →
the leaf below (`+0x18`, `0x40ab60`), none left (`0x80000000`) → −1. Then `0x40a26a` maps the face to its group.

The cell point, by writer (all ten callers of `0x407790` and the 29 of `0x4077f0` were checked):
* **`inst.pos` (`+0xc`)**: `0x407790(NULL)` / `0x4077f0(NULL)` (`0x4077a2`, `0x40780d`): the loader `0x4288cf` (in file order;
  the `.ins` cameras `0x428a4a`), a show (message 6 on, `0x42d99b`, only when `+0x1c == −1`), the delayed shows of `0x42d2e0` (`0x42d5a0`),
  SetTypeInstance 1200 (`0x403e7a`, `ebx` = 0 from `0x403524`), the loop `0x4048d0` over the records of `[0x4c4cac]`, the bonus respawns `0x44f595` / `0x44f8f5`, the
  enemy PostLoad `0x41a074`.
* **The animated root `+0x60`**: the clock `0x43eee0` at its end (`0x43f2f1..0x43f351`), unless flag 0x20 or the pose cache hit
  (MODEL_RENDER.md §9.1: then it returns before, `0x43f06e`). This is the ONLY later re-cell of a non-actor. The clock runs:
  (a) for every instance the list build appends (`0x42a94b`, `vtbl[2](0x81)`), (b) for the `.col` objects of the camera's kd
  leaf (`0x42aa0b`, `vtbl[2](1)`), (c) in every collision query, for each instance it tests before the phase-mask test
  (`0x4324c9..0x4324dc` in vt[7]; the same prologue in vt[5], vt[6], vt[8], vt[9]; PERSO_MOVE.md §6.7), (d) in the draw
  `0x42b380` (already clocked by (a)). All of them only when `+0x1c ≠ −1` (`0x42e2c3`, `0x43248e`).
* **An actor's own point** (flag 0x20: `0x41abf6`/`0x41b2c0` enemies, `0x44d55c` bombs, `0x452b6f`/`0x452dee` rocket and cannon,
  `0x449bac` launcher shots, the bosses `0x40cb41..0x40e496`, the Perso `0x428ce0`).

So an instance is celled at its `.ins` position until its clock first runs, and at its animated root as of its last clock run
afterwards. `0x4077f0` always re-links (`0x4077f3..0x407802`: unlink, then cell even when it was out of the world); a point in
no sector leaves `+0x1c = −1`, and such an instance is never clocked, listed or tested again until a show or its own mover.

Consequences for an instance whose position and root disagree (`WOODY_VISLOG=5` lists them per level):
* **Position has a floor group, root not** (the common case: a volume box whose root node sits at the bottom of the box, below
  the floor): listed while its load group is marked, drawn and clocked in that frame, and from then on `+0x18 = −1`: at most
  ONE frame in the list, then never again. The camera-leaf clock (b) or a query (c) can end it before it was ever listed.
* **Position without a floor group, root with one**: not listed until something clocks it through (b) or (c); after that it is
  listed like any other instance.
* **Neither**: never listed (Think never runs, never drawn through the list).

The instances concerned (shown at level start; the hidden missile pools of W1A 496-503 and W2B 539-543, W2B 216 and W3D 809-822
(models 50/51) are hidden anyway):

| level | instances | model | what | position / root group | original |
|---|---|---|---|---|---|
| W1A | 163, 281, 283, 385 | 29 | volume box (one kind-8 node, no mesh, no press node) | yes / −1 | ≤ 1 frame listed; nothing to draw |
| W1A | 317 | 29 | volume box | −1 / −1 | never listed |
| W3D | 200, 206, 234, 236, 239, 240, 241, 243, 295, 298, 302, 333, 365, 370, 372-374, 425, 426, 522, 525, 561, 583, 774, 843, 845 | 28 | volume boxes | yes / −1 | ≤ 1 frame; nothing to draw |
| W3D | 242, 244 | 28 | volume boxes | −1 / −1 | never |
| W3D | 148, 149 | 3 | decor mesh (194 polygons, 1 press node), root 25 units off the position (x), over no floor | 3 / −1 | drawn for ONE frame when first listed, then gone |
| W3D | 125 | 22 | mesh (246 polygons, 2 press nodes, 62 `.col` refs), position just below the floor | −1 / 10 | not drawn until the camera leaf or a query clocks it, then normal |
| W3D | 791-798, 879-886 | 48 | the bomb pool (type 40, flag 0x20), parked where no floor is below | −1 / −1 | never listed until the bomb class moves one (`0x44d55c`) |
| W2B | 215, 235, 298, 308, 310, 311, 317, 321, 341, 344, 394, 425, 438, 480, 501 | 29 | volume boxes | yes / −1 | ≤ 1 frame; nothing to draw |
| W2B | 398, 408-412, 447-455 | 45 | the node layout of the W3D bomb model 48 (80 polygons, 1 press node), a plain type-0 instance at y 342 / 1152 in sector 52 | −1 / −1 | never listed, never drawn |
| W2B | 526 | 51 | collision-only object (3 press nodes, no mesh) at (57, 1, 17) | −1 / 1 | listed once a query clocks it; nothing to draw |
| W2B | 229 | 38 | enemy (type 8, flag 0x20) | −1 / 0 | celled by its mover |

(The Perso's `.ins` position lies on the floor, `d ≤ 0`, so it has no group there either (0.5 higher it has); it is an actor.) So the
only visible effects in these three levels are W3D 148/149 (one frame) and W3D 125 (appears once clocked).
## 5. Messages 56 / 57: transparency fade (`+0x6c`)

`+0x6c` = **transparency** (0 = normal, 1 = gone). Readers:
- `0x43b504` (polygon renderer): `alpha = (1 − [+0x6c]) · 255`; if alpha < 252 → transparent draw mode.
- `0x42e374` (`0x42e2b0`): `+0x6c > 0.98` → the instance is not drawn at all.
- `0x42e2cc`: only at `+0x6c < 0.01` is the early visibility test `0x42f3d0` done; `0x42e69a`/`0x42eb7a`: at `> 0.01`,
  in the extra draw pass (see §6, bit 1) the face color is multiplied by `+0x6c` (path `0x4388e0` instead of `0x4385f0`).

Base class (`0x42de00`, also types without SetTypeInstance): **56: `+0x6c = v·0.01` directly**; 57 doesn't exist (default, ignored).

Classes with the intermediate handler `0x44e8f0` (type 70 = vtable `0x4a9124`, and all classes that fall through to `0x44e8f0`: 17, 20/21, 30-38, 40,
42, 50-52, 80, 120/121) have two extra fields; init `0x44e7c0`: `+0x100 = 100.0`, `+0xfc = 0`, `+0x6c = 0`:
- **56** (`0x44e91b` → `0x44e7f0(v·0.01, 0)`): `+0xfc` (target) = v·0.01; `+0x6c` itself stays put (2nd arg 0; with 1 it would be set directly).
- **57** (`0x44e907`): `+0x100` (fade speed per second) = v·0.01.
- per frame (vtable[3] of type 70 = `0x44e810`; not if byte `[0x5e48cc]` ≠ 0):
```c
float old = I->fade;                                    // +0x6c
if (I->fade < I->target)      { I->fade += dt * I->rate; if (I->fade > I->target) I->fade = I->target; }   // dt = [0x509adc]+0x38
else if (I->fade > I->target) { I->fade -= dt * I->rate; if (I->fade < I->target) I->fade = I->target; }
if (I->fade > 0.9f && old <= 0.9f) I->flags8 |= 0x40;   // no longer collidable (see PERSO_MOVE: +8 & 0x40)
if (I->fade < 0.9f && old >= 0.9f) I->flags8 &= ~0x40;
anim_events(I);                                         // 0x42f5e0
```
Note: the other classes have their own vtable[3]; whether they call `0x44e810` needs to be checked per class (open). For type 70 it's proven.
The default speed of 100/s makes a 56 without a 57 practically instant. W1A: `56 [inst,100]` + `57 [inst,800]` = fade out to
invisible over 8/s (⅛ s); the 180 messages 56 within 6 s are the script fading type-70 objects in and out at a distance.

Type table correction vs. classmap_raw.txt (ctor inlining in `0x403440`): type 41 → vtable `0x4a9360` (`0x403b5d`), type 60 →
`0x4a9194` with its own handler `0x474a40` (`0x403ca3`), **type 70 → `0x4a9124`, handler `0x44e8f0`, update `0x44e810`** (`0x403cea`),
type 90 → `0x4a90ac`, base handler, own vtable[3] `0x472560` (`0x403d56`).

## 6. Message 45: SetFlags (`+0xf0 |= bits & 0x23`, `0x42ddb4`; only sets, never clears; loader sets +0xf0 = 0 in `0x428711`)

| bit | readers | effect |
|---|---|---|
| 1 | `0x42b3d6` (world draw loop `0x42b380`) | an instance of kind `(+8 & 0x1f) == 1` is drawn with `vtable[2](7)` instead of `(5)` if the detail option `[0x4c2c0c]` ≠ 0: extra pass (arg bit 2) in `0x42e2b0` against the face table `[0x4c4cac]+4` (64 B/entry: plane, color +0x30, object +0x3c) – shadow or reflection, not further investigated |
| 1 | `0x42efd7` | forbids caching the bounding sphere (`+0x88 = 1`, `+0x8c..+0x98`) for stationary instances |
| 2 | no reader found | – |
| 0x20 | `0x43b423` (polygon renderer) | **the black outline**: only if `[0x4c2c0c] == 2` (detail option from `Woody.cfg`, 2 in the shipped cfg), the back side is drawn once more after the model, each vertex pushed outward along its normal by `w = distance/300`, fading off above 2.5 as `5 − distance/300` and fully gone above 1500 units (`0x43b4ce..0x43b4f3`). See MODEL_RENDER.md §11 |
All three are purely visual; for a reimplementation, just storing them is enough. Class type 40 sets bit 1 itself (`0x44d304`).

`+0x88` status: 0 = recompute the bounding sphere, 1 = cached in `+0x8c..0x94` (center) / `+0x98` (radius) (`0x42efe0`),
2 = special state set by `0x45f39a` and by classes 50-52 (`0x450db2`); persists through the clock and message 3.

## 7. Class-specific messages from the W1A list (short)

Corrected type table (the vtable is overwritten in `0x403440` after the ctor; classmap_raw.txt misses this):
50 → `0x4a92ec` (handler `0x451040`), 51 → `0x4a9278` (`0x4514d0`), 52 → `0x4a9204` (`0x451610`), 35 → `0x4a9444`, 38 → `0x4a93d0`
(handler `0x44f9b0`), 121 → `0x4a9034`. Chain: `0x4514d0`/`0x451610` → `0x451040` → `0x44e8f0` → `0x42d5e0`.

| id | class | address | behavior |
|---|---|---|---|
| 50 | 50-52 | `0x45108a` | byte `+0x110 = (v == 1)`: zone on/off |
| 51 | 50-52 | `0x451059` | for all `+0x10c` records (0x30 B at `+0x108`, one per marker node of type code 0, `0x450dd0`): float `rec+0x18 = v` (raw, no ×0.01) = radius |
| 52 | 51 | `0x4514e2` | float `+0x114 = v` (raw; default 400.0, `0x45129e`) = length of the beam from the marker |
| 53 | 52 | `0x451622` | `+0x114` = instance pointer of arg1 (target instance) |
| 55 | 20/21 | `0x45374e` | mode 1: `+0x114 = v·0.01` (s); mode 2: `+0x11c = v` (raw; upper bound of the accumulated speed `+0x12c`, `0x452d75`) |
| 1501 | 90 | `0x46cd07` | `+0xfc = v` (mode 0/1/2 of the "instance d'environnement", update `0x472560`) |
| 1502 | 90 | `0x46cd2e` | **color**, not a target point: `+0x110..0x118 = (r,g,b)/255` (`0x4abc5c` = 1/255), `+0x11c = w·0.01`, `+0x100 = flag`, `+0x108 = 0`, then `vtable[0x1c]` = `0x472b30` (reinit) |

Classes 50-52 are danger zones: the update `0x450f20` (→ fade `0x44e810`, then only if `+0x110` is set) tests every record
against all actors from `0x4c52d8` against the record's segment (`0x433de0`, radius `rec+0x18 · 0.85`) and, on a hit, calls
`actor->vtable[0x98/4](2)`. Type 90 is an environment/particle volume (string "une instance d'environnement n'a pas de volume", `0x4725c5`); 1501/1502 thus move nothing.

## 8. Movement of instances: overview

1. **Animation tracks** (clock §1): all node movement within the model; the instance position itself does not change, but `+0x60..0x68`
   (world position of the skeleton root, see §1.1) is used to re-cell the instance. Rotating fans,
   doors and elevators are thus just animations started with 3/4 (loop = 4, open/close = 3 with flag 1/0).
2. **Path follower** (§3): the only generic movement of `inst.pos`; constant speed over the arc length, time in 1/100 s for the whole path,
   options loop / back-and-forth / orient. Started with 42/43, stopped with 44.
3. The base class has **no** "linear to target" or "rotate at constant speed": no message in `0x42d5e0` writes
   `+0xc..0x14` or `+0x28..0x48` except 42/43 (path start/end). 1502 is a color (§7). Other movement is in classes
   (enemies 4-16 message 11, Perso, type 20/21, type 60).

The clock (and thus the path follower) only runs for instances with a cell (`+0x1c ≠ −1`), once per frame, from the world draw loop
`0x42b380` → `vtable[2](5 or 7)` = `0x42e2b0` for ALL instances in `world+0x64`. `vtable[3]` (think step; base = empty
`0x462c60`) is called by `0x42b400` for the instances of that list only (§4.1): not for hidden ones, nor for those outside the
camera's `.vis` sectors / floor groups or (stationary) outside the view frustum.

## 9. Base class field table (0x104 bytes)

| field | meaning |
|---|---|
| +0x00 | vtable (`0x4aa31c`; [2] = update/draw `0x42e2b0`, [3] = think step, [5]..[9] = the collision tests segment `0x432ab0`, endless ray `0x431de0`, floor `0x432480`, cylinder `0x433140`, sphere `0x433ff0` (PERSO_MOVE.md §6.5-6.8), [11] `0x4305c0` / [12] `0x430af0` = swept / static sphere tests that nothing calls (§6.9), [17] = anim reset `0x42e250`, [22] = messages) |
| +0x04 | id `0x01000000 + slot` |
| +0x08 | flags: bits 0-4 kind (1 = instance, **2 = a `.lit` light record** – set by the `.lit` loader at `0x40ae60` (`and edx, 0xffffffe2 / or edx, 2`), vtable `0x4a9508`, Update = the empty `0x462c60`; not "type 60" as previously stated here –, 3 = camera); 0x20 = don't re-cell on animated position (`0x43f2ed`); 0x40 = non-collidable |
| +0x0c..0x14 | position |
| +0x18 | result of `0x40a0c0(pos, −1)` (−1 if hidden) |
| +0x1c | world cell (−1 = hidden/outside the world) |
| +0x24 | next instance in the cell list (`cell+0x44`) |
| +0x28..0x48 | 3×3 rotation (rows) |
| +0x4c..0x54 | scale x, y, z |
| +0x58 | frame number of the last clock |
| +0x5c | first world matrix index |
| +0x60..0x68 | animated center (cell determination) |
| +0x6c | transparency 0..1 (§5) |
| +0x70 | volume/collision VM ids |
| +0x74 | 16-byte records per light/subpart (message 14) |
| +0x78 | TRAJ / path follower (§3) |
| +0x7c, +0x80 | runtime (0 on init; +0x7c = result of `0x42f490`) |
| +0x84 | −1 on init; ≠ 0 required for the sphere cache |
| +0x88 | sphere cache status (§6) |
| +0x8c..0x98 | cached bounding sphere (xyz, r) |
| +0x9c | animation end flag (§1) |
| +0xa0 / +0xa4 | speed / base speed |
| +0xa8 | start time |
| +0xac | position in animation (s) |
| +0xb0..0xbc | slots 0..3 |
| +0xc0 | slot0 == slot1 |
| +0xc4 / +0xc8 / +0xcc | frame number / animation / frame position of the previous event scan (`0x42f5e0`: events between previous and current frame position → `0x43a880` → `0x4695f0`) |
| +0xd0 | phase bit `(1<<round(phase·15))<<16`; the collision tests vt[5..9] return at once when `inst+0xd0 & id & 0xffff0000 == 0`, id = the `.col` ref of the cell being walked (phase mask << 16 \| slot) or `slot \| 0xffff0000` for the dynamic list: an instance counts in a cell only at the animation phases at which the level tool registered it there (PERSO_MOVE.md 6.7) |
| +0xd4 | head of the link list (message 34): instances not drawn while the camera is in this one's volume (§10.1) |
| +0xd8..0xec | texture override (§2) |
| +0xf0 | SetFlags bits (§6) |
| +0xf4 | runtime state per mesh node |
| +0xf8 | model `S` |
| +0xfc / +0x100 | (derived classes via `0x44e8f0`) fade target / fade speed per s |

## 10. Message table (base `0x42d5e0` + intermediate handler `0x44e8f0`)

| id | args | address | exact behavior |
|---|---|---|---|
| 1 | anim, t | `0x42de97` | still frame: speed 0, `+0xac = L(old slot0)·t/1000`, slot0 = anim, queue empty |
| 2 | anim, dir, dur, off | `0x42de16` | one-shot, pass duration dur/100 s, start at fraction off/1000, dir 0 = backward |
| 3 | anim, dir, dur | `0x42d62d` | one-shot; dur ≤ 10 → jump to end (dir≠0) / start (dir=0) of the CURRENT animation; same animation already playing → reverse from current pose or ignore |
| 4 | anim, dir, dur | `0x42d8e7` | loop (all 4 slots), pass duration dur/100 s |
| 5 | – | `0x42d606` | pause (speed 0) |
| 6 | on | `0x42d985` | attach/detach cell = show/hide incl. collision (§4) |
| 7 | – | dispatcher `0x4012f0` | cancel pending 12/13 |
| 12 / 13 | as 3 / 4 | `0x42d619` / `0x42d8d3` | wait (return 1 → retry list `0x401250`) until `+0x9c ≠ 0`, then as 3 / 4 |
| 14 | b0,b1,b2,f,d | `0x42db9e` | per 16-byte record at +0x74: bytes 0..2, float +4, dword +8; 0xffff = unchanged |
| 15 / 17 | x, mode, f, t2 | `0x42d9c3` / `0x42daae` | UV scroll finite / endless (§2) |
| 16 / 18 | x, mode, f | `0x42da32` / `0x42db0f` | texture frames one-shot / loop (§2) |
| 19 | – | `0x42db86` | texture overrides off |
| 34 | other | `0x42dc21` | add a link pair (table `[0x50944c]+0x50`, counter +0x4c), `+0xd4` = new head; read by `0x42aa0b`: `other` is not drawn while the camera is inside a volume node of this instance (§10.1) |
| 42 / 43 / 44 / 46 | see §3 | | path follower |
| 45 | bits | `0x42ddb4` | `+0xf0` gets the bits `& 0x23` added |
| 56 | v | `0x42de00` / `0x44e91b` | base: `+0x6c = v/100`; derived: fade target `+0xfc = v/100` |
| 57 | v | `0x44e907` | derived only: fade speed `+0x100 = v/100` per s |

### 10.1 Message 34 `[inst, other]`: hide `other` while the camera is inside `inst`'s volume

`0x42dc21` (base handler, so every class that falls through to `0x42d5e0`):
```c
Level *L = [0x50944c];
L->pairs[L->npairs] = (Pair){ L->inst[other & 0xffffff] /*+0x6c*/, inst->links /*+0xd4*/ };   /* table +0x50, 8 B a pair */
inst->links = &L->pairs[L->npairs++];                                                          /* counter +0x4c */
```
The ctor sets `+0xd4 = 0` (`0x42e243`), the copy at `0x42e140` passes it on, and the **only reader** is the visibility
pass `0x42a980` (`0x42aa0b..0x42aa5a`), which runs every frame before the sector walk:
```c
cell = World->cells[0x408180(camera_pos)];                  /* the kd leaf the camera is in */
for (k = 0; k < cell->n /*+0x40*/; k++) {                   /* .col list: every instance whose geometry touches the leaf */
    I = World->obj[cell->ids[k] & 0xffff];
    if ((I->flags8 & 0x1f) != 1) continue;                  /* plain instances */
    I->vtbl[2](I, 1);                                        /* clock / pose */
    if (0x4300c0(I, camera_pos))                            /* the point is inside one of I's volume nodes (model +0x48/+0x4c) */
        for (p = I->links; p; p = p->next) p->inst->stamp20 = [0x4c4c08];
}
```
`0x42a840` (the per-sector instance walk that fills the draw list `+0x60/+0x64` and calls `vtbl[2](0x81)`) skips an
instance whose `+0x20` already equals this frame's stamp (`0x42a925..0x42a92f`). So a linked instance is **neither drawn
nor updated** while the camera stands inside the volume; nothing else changes (collision queries use their own stamps:
`0x407171`/`0x4074ca` bump `[0x4c4c08]` per query). `0x4300c0` transforms the point into each volume node's space
(`0x440fc0`) and requires `plane·p ≤ 0` for all its planes, the same convex test as the trigger volumes (`0x430210`).

Use: an occlusion hint of the level designers. 340 sends, all in init code: K1R object 346 → slot 346 (model 15, one volume
box scaled ×38/×17.6/×10.6, world box x −44070..−29970, y 3273..7525, z −11273..5318) hides 57 instances; K2R 499/500 → 53
and 30, S2R 482/483 → 94 and 41 (K2R and S2R share the track geometry and the two volume boxes); W2D 709 (model 41, box
x −12512..−7202) hides 65 and **holds the level start**: 31 instances that the camera would otherwise draw at the start are
dropped. The linked instances are decor behind walls/rock seen from inside the volume, so the picture does not change (a
W2D start frame with and without: only the bobbing tyres differ); it only saves drawing. The volume instances are listed
in every leaf their box touches (`.col`: K1R 346 in 1592 of 7381 leaves), so the leaf condition adds nothing to the
volume test.

Port: `rnd_link` / `links_hide` in `src/render_gl.c` (pairs kept in the Renderer, test with `volume_contains`, a
linked instance's `drawn` is cleared after the frustum pass; a volume instance hidden by message 6 hides nothing, as
it is in no cell list then). `WOODY_LINKLOG=1` prints each volume box, and every entry/exit of the camera.

## 11. Recipe for reimplementation in C

```c
typedef struct {
    int   slot[4];            // init {0,-1,-1,-1}
    float speed, base_speed;  // 0
    float start, pos;         // +0xa8, +0xac
    int   ended;              // +0x9c
    float fade, fade_target, fade_rate;   // 0, 0, 100; target/rate only for classes behind 0x44e8f0 (e.g. type 70)
    int   has_fader;          // class != base
    int   hidden;             // cell == -1
    int   noncollide;         // +8 & 0x40
    unsigned setflags;        // +0xf0
    unsigned phase_mask;      // +0xd0
    struct { unsigned char mode; float t0B, facB, t0A, facA, t2A; } tex;   // +0xd8..0xec
    struct { unsigned flags; float t0, dur; } traj;   // bits 17..22 as in §3; points/length from the .ins
} InstAnim;
```
- `on_msg`: implement 1, 2, 3, 4, 5, 12, 13 exactly per §1.3 (note `dur·0.01`, `off·0.001`, `dur ≤ 10`, the reverse logic of 3);
  12/13: if `!ended` → put the message on a retry list (max 32 in the original) and offer it again every frame; 7 clears that list for the instance.
- Per frame per non-hidden instance: first `traj_update`, then the clock from §1.2 (phase → `frame = nframes·phase`), then evaluate tracks.
  The formula "phase = (now−start)/(duration/4096/speed)" alone is incomplete: add clamping (one-shot, speed → 0), slot shifting (loop),
  negative speed (phase = 1 − …) and the path "speed 0 → phase = pos/L".
- 6: hide = don't draw, no collision, no volumes; show = re-determine the cell at `inst.pos`.
- 56/57: base instances directly, type 70 (and other derived classes) via target + speed; draw with alpha = 1 − fade, skip if fade > 0.98,
  collision off if fade > 0.9 (edge-triggered, see §5).
- 16/18/15/17/19: per-instance texture frame/UV override (§2).
- 42/43/44/46: path follower (§3). 45: just store it. 50/51/52/55/1501/1502: class-specific, no movement.

## 12. Open questions
1. `+0xd9`/`+0xda` (arg1 of 15..18, 0xffff in W1A): no reader found in `0x47f290`; possibly a texture filter elsewhere in `0x43b3f0`.
2. SetFlags bit 2: no reader found. Bit 1: is the extra pass a shadow or a reflection (table `[0x4c4cac]+4`)? Bit 0x20: nature of the effect pass.
3. ~~Exact color blending in `0x4388e0` at fade > 0.01 in the extra pass (the shadow)~~: MODEL_RENDER.md §8.1 (AMB + light texture × light colour × k × fade, bucket 2). Drawing the model itself while fading: MODEL_RENDER.md §8.
4. `+0x88 == 2`: meaning of this state in `0x42e2b0`.
5. ~~Rounding mode of `0x499580` (ftol) for the texture frame index and the phase mask~~: `_ftol` sets RC = 11 around its
   `fistp` and so always truncates; the game's control word is `0x007F` (24-bit, nearest-even; verified live,
   `tools/wverify.py --probe fpu`), which only matters for inline `fistp`s. The phase mask adds 0.5 before its `_ftol` (`0x43f28b`), so it rounds half up.
6. Class 20/21 (message 55), type 60 (`0x474a40`, 1503/1506) and enemy message 11 are not worked out here.
7. `0x40a0c0` (`+0x18`): what exactly this second cell-like field is.
8. The path follower doesn't use the "closed" bit (16); whether closed paths repeat the first point in the data has not been checked.

