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
| +0xd0 | u32 | `(1 << round(min(phase,1)·15)) << 16`: phase mask for the renderer | `0x43f285` |

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
| 46 | a, b | `0x42dc7d` → `0x4381e0` | orientation flags |
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
`0x42b380` → `vtable[2](5 or 7)` = `0x42e2b0` for ALL instances in `world+0x64` (not just visible ones). `vtable[3]` (think step; base = empty
`0x462c60`) is called by `0x42b400` for all instances, including hidden ones.

## 9. Base class field table (0x104 bytes)

| field | meaning |
|---|---|
| +0x00 | vtable (`0x4aa31c`; [2] = update/draw `0x42e2b0`, [3] = think step, [8] = collision `0x433140`, [17] = anim reset `0x42e250`, [22] = messages) |
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
| +0xd0 | phase mask `(1<<round(phase·15))<<16`; renderer and collision (`inst+0xd0 & hull-id & 0xffff0000`, see PERSO_MOVE) use it to enable/disable nodes per animation phase |
| +0xd4 | head of the link list (message 34) |
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
| 34 | other | `0x42dc21` | add a link pair (table `[0x50944c]+0x50`, counter +0x4c), `+0xd4` = new head |
| 42 / 43 / 44 / 46 | see §3 | | path follower |
| 45 | bits | `0x42ddb4` | `+0xf0` gets the bits `& 0x23` added |
| 56 | v | `0x42de00` / `0x44e91b` | base: `+0x6c = v/100`; derived: fade target `+0xfc = v/100` |
| 57 | v | `0x44e907` | derived only: fade speed `+0x100 = v/100` per s |

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
3. Exact color blending in `0x4388e0` at fade > 0.01 in the extra pass (the shadow). Drawing the model itself while fading is worked out in MODEL_RENDER.md §8.
4. `+0x88 == 2`: meaning of this state in `0x42e2b0`.
5. Rounding mode of `0x499580` (ftol) for the texture frame index and the phase mask (truncate or round).
6. Class 20/21 (message 55), type 60 (`0x474a40`, 1503/1506) and enemy message 11 are not worked out here.
7. `0x40a0c0` (`+0x18`): what exactly this second cell-like field is.
8. The path follower doesn't use the "closed" bit (16); whether closed paths repeat the first point in the data has not been checked.

