# PERSO_SPECIAL.md — Woody's special attack (action 11, charge `Perso+0x254`, `0x458bf0`)

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone, `tools/disasm.py` where the linear
disassembly is shifted). Every claim has an address; floats are read from the exe's `.rdata` (pefile). **Certain** = read instruction
by instruction; **uncertain** = derived or not measured in the original. None of this has been traced live in the original yet.

Related: PERSO_MOVE.md §3.4 (input actions), BONUS.md §2.3 (type 35 gives the charge), PERSO_JUMP.md §2-4 (attack controller,
target loop, animation controller), PERSO_FRAME.md (order of the Perso update), ENEMY.md / ENEMY2.md / BOSS14.md (`vtbl[39]`/`vtbl[38]`
per class), BONUS.md §2.4 (effect pool), HUD_TEXT.md §4.6 (HUD animator), CAMERA.md §6.3 (camera shake), SOUND.md (SoundFx table).

Notation: `p` = Perso, `P` = parameter block `p+0x110`, `dt` = `p+0x2f8`, `tab[i]` = the 512-float cosine table at `[0x5e823c]`
(`tab[i] = cos(2π·i/512)`, `tab[i+128] = −sin(2π·i/512)`; `[0x4b798c]` = int 128), `rand01()` = `0x43ff40` = `rand()/32767` ([0,1] inclusive),
`ftol` = `0x499580` (truncate).

## 0. Summary

* **Input**: action **11** (default **RCtrl**), on **release** (`0x467440(0xb)` = bit 31 of the action state, exactly one frame).
* **Conditions** (all four, `0x458c21..0x458c4c`): on the ground (`+0x22c`), Perso state `+0x21c == 0`, no special attack in progress
  (`+0x750 == 0`), charge `+0x254 > 0`. The attack controller (`+0x5b4`), ducking (`+0x694`) and death (`+0x26c`) are **not** tested.
* **Start**: charge −1, `+0x750 = 1`, timer `+0x74c = 0`, movement lock and invulnerability for `AnimLen(0x13,0)` = **4.0667 s**
  (`.ins` anim 86: 49971/4096 s = 12.2 s divided by speed 3.0), logical animation **0x13** requested, effect emitter `0x47ab90` started.
* **Failed** (button released but a condition fails): **SoundFx 9** ("can't do that", ref 107, vol 50, 2D), except for subtype 4/5 (race Persos).
* **Hit**: at a **fixed time**, not on an animation event: the first frame where the timer **≥ 1.5 s** (`[0x4aa184]`):
  camera shake **2.0 s**, rumble (1.0, 1.0), `+0x750 = 2`, and **one** pass over the actor list `0x4c5258[0x4c5324]` with
  `t->vtbl[39](p, P+0x94 = 3.0, &(0,0,0), &p->pos, kind 2)`; on `true` `t->vtbl[38](3)`, which for **every** enemy/boss class is `ret 4`.
  **There is no range test**: the target's position (`vtbl[34]`) is fetched and copied but never read (dead code).
  Who is on the list = every living enemy/boss whose Think ran last frame (visible sectors), max. 32.
* **End**: timer ≥ `AnimLen(0x13,0)` ⇒ `+0x750 = 0`. Reset (`0x44ab20`, respawn) clears `+0x74c`/`+0x750`.
* **Effect** (all in the particle pool `[0x5e823c]+0xdb8`): for 1.25 s, **250 light streaks per second** that shoot from a sphere of
  radius 400 toward Woody's waist (feet + 143) (`0x47a790`, texture bank 0 image 0), and at 1.75 / 2.0 / 2.25 s after the start three
  **fire rings** flat on the ground that expand from radius 0 to 4000 over 0.5 s (`0x47a4c0`, bank 0 image 33). No screen flash, no
  stars in the emitter itself.
* **Per hit ordinary enemy** (types 4..9, 13 and boss 16) the hit star `0x4750e0` — **at Woody's feet**, not at the enemy, because
  the hit point the Perso passes is its own position. 3.0 damage kills every ordinary enemy (max hp 3). Boss 14 (Buzz) takes 1 point (only
  in the low phase), boss 15 and type 12 are immune, boss 16 takes 3 points in state 3/4.
* **Sound**: only the animation events of `.ins` 86: **ref 114** at t = 0 (swelling, 2.06 s) and **ref 115** at t = 1.667 s (thud, 1.84 s),
  both vol 50, 2D. No SoundFx call on success.
* **HUD**: the drop of `+0x254` starts the "minus 1" animation of the charge counter (`0x448300` → `0x462380` / tick `0x462020`), ≈ 2.4 s.
* **Script**: **no** message is sent to the VM, no variable is set; switches, crates, bombs and other world objects do not react.

## 1. Trigger and state machine

### 1.1 Where it runs

`0x458bf0` is only called from the Perso update `0x44b530` (`0x44b7b9`), every frame, as the last in the sequence
`0x462c60, 0x464ef0, 0x465e50, 0x457a50, 0x44ba70, 0x465b10, 0x44b980, 0x458bf0`, **before** the state dispatch (moving, `0x44b7f9`).
Every step is skipped once `+0x690` (state changed this frame) is set. There is **no** test on the Perso state, on death or on
cinematic: in every state the input is checked and the running automaton is ticked further. The movement lock `+0x238` has already
been decremented by `dt` that frame (`0x44b638`).

### 1.2 Decompilation (certain)

```c
void Perso_Special(Perso *p)                                   /* 0x458bf0, 119 instructions */
{
    if (Input_Released(p->input /*+0x2f4*/, 11)) {             /* 0x467440: (state[11] >> 31) & 1 */
        if (p->onGround /*+0x22c, 0x44bcf0*/ && p->state /*+0x21c*/ == 0 &&
            p->special /*+0x750*/ == 0 && p->charges /*+0x254*/ > 0) {
            p->charges--;                                      /* 0x458c5b */
            p->special = 1;  p->specialT /*+0x74c*/ = 0;
            LockMove(p, AnimLen(p->anim /*+0x494*/, 0x13, 0), 0);   /* 0x44cce0: +0x238 = max(+0x238, T) */
            SetInvulnerable(p, AnimLen(p->anim, 0x13, 0));     /* 0x44cd10: +0x270 = max(+0x270, T) */
            p->anim->vtbl[2](0x13);                            /* 0x436b70 Request(0x13), once */
            SpecialFx_Start();                                 /* 0x47ab90 */
        } else if (Subtype(p) != 4 && Subtype(p) != 5)         /* 0x40c350: (typeword >> 5) & 0x1f */
            SoundFx(9, 0);                                     /* 0x468a00 on [0x5e48c8], 2D */
    }
    switch (p->special) {
    case 1:
        p->specialT += dt;                                     /* 0x458d37 */
        if (p->specialT < 1.5f /*[0x4aa184]*/) return;
        CameraShake([0x4c737c], 2.0f);                         /* 0x41fbb0: cam+0x67c = 2.0, cam+0x694 = 0 */
        Rumble(p, P->+0xb4 /*1.0*/, P->+0xb0 /*1.0*/);         /* 0x44d1b0: [0x5e618c]->vtbl[2], not if +0x21c == 2 */
        p->special = 2;
        for (i = 0; i < [0x4c5324]; i++) {                     /* actor list from the previous frame, 0x4c5258[] */
            Actor *t = [0x4c5258][i];
            if (t == p) continue;
            vec3 tp  = *t->vtbl[34]();                         /* position – copied to a local and NEVER used */
            vec3 dir = { 0, 0, 0 };                            /* 0x43ff80(0,0,0) */
            if (t->vtbl[39](p, P->+0x94 /*3.0*/, &dir, &p->pos /*+0x1f4*/, 2))
                t->vtbl[38](3);
        }
        return;
    case 2:
        p->specialT += dt;
        if (AnimLen(p->anim, 0x13, 0) <= p->specialT) p->special = 0;   /* 0x458d1c */
        return;
    }
}
```

* The frame the attack starts already counts: the switch runs in the same frame, so `specialT = dt` after the start frame.
* `0x436b90(n, k)` = AnimLen = `anim[sub[k]].duration(1/4096 s) / speed` (PERSO_JUMP §4). For `(0x13, 0)`: sub[0] = `.ins` 86 ⇒ 49971/4096/3 = **4.06665 s**
  in all three character models (W1A model 0, K1A model 45, S1A model 43: 2440 frames, 49971/4096 s).
* `P+0x94`, `P+0xb0`, `P+0xb4` come from the default values `0x4631b0` (`0x4631d6`: 3.0; `0x463227`/`0x46322d`: 1.0), not from the column table
  `0x4b5f14` (`0x463280` only copies P+0x00..0x78). They are thus the same for all five subtypes.
* Subtype 1..5 = script type 1→1, 2→3, 3→2, 18→5, 19→4 (PERSO_MOVE §1): only the race Persos (18/19) stay silent on failure.

### 1.3 Fields

| field | type | meaning | writers / readers |
|---|---|---|---|
| `+0x254` | int | charge (number of special attacks), HUD counter, savegame `slot+0x14` | `0x44c800` (+1, bonus 35), `0x458c5b` (−1), `0x44ae60` → HUD |
| `+0x74c` | f32 | timer since the start (s) | `0x458c6b` = 0, `0x458d00`/`0x458d43` += dt, Reset `0x44ad5e` = 0 |
| `+0x750` | int | 0 = free, 1 = charging (up to 1.5 s), 2 = after the hit (up to AnimLen) | only `0x458bf0` and Reset `0x44ad64` (grep on all `+ 0x750]`) |
| `+0x238` | f32 | movement lock (LockMove, max) | PERSO_JUMP §0 |
| `+0x270` | f32 | invulnerability (max) | PERSO_MOVE §2 |

### 1.4 Timeline (from the frame of the release, t = accumulated `dt`)

| t (s) | event | source |
|---|---|---|
| 0 | charge −1; anim 0x13 (.ins 86) starts (restart flag 1); lock + invulnerable until 4.067; sound **114**; emitter starts | `0x458c4e..0x458cac`, anim event frame 0 |
| 0 .. 1.25 | 250 streaks/s, each alive 0.5 s, shooting toward Woody | `0x47a8d0` (emitter-u < 0.5) |
| ≥ 1.5 (first frame) | camera shake 2.0 s, rumble, **hit**, `+0x750 = 2` | `0x458d49..0x458e28` |
| 1.667 | sound **115** (anim event frame 1000 of 2440) | `.ins` 86 root node |
| 1.75 / 2.0 / 2.25 | fire ring 1 / 2 / 3 (each 0.5 s) | `0x47aa3c..0x47ab68` |
| 2.5 | emitter off | `0x47a912` |
| ≈ 2.75 | last ring gone | |
| 3.5 | camera shake over | CAMERA.md §6.3 |
| 4.067 | `+0x750 = 0`, lock and invulnerability off (same) — anim 86 is done | `0x458d1c` |

### 1.5 What the lock and invulnerability do (from existing docs, certain)

* `+0x238 > 0`: `0x44bb20` gives the Mover no input ⇒ no walking, no turning, **no jumping** (`0x462d70(a, 0)`); the attack trigger
  `0x457330` returns immediately (no peck, no charge run, no air attack). Ducking sets `+0x238` itself, but its trigger (`0x465b10`) does not
  check `+0x750` — ducking during the attack is not excluded (uncertain what that looks like; anim priority 5500 beats ducking).
* `+0x270 > 0`: `Hit` (`vtbl[39]` `0x44ca00`) and `Kill` kinds 2, 3, 4, 5, 6, 8, 9 are ignored. **Kill 1 (pit) and 7 (water) are not**: in a
  pit or water he just dies; the automaton then keeps running (only Reset clears it) and the hit may still land at 1.5 s.
* The condition `+0x21c == 0` excludes: death (state 2), looking around (3), climbing wall (4), script action (5), carrying a bomb (6), rocket (8),
  results screen (9), race (1). Releasing in those states = sound 9 (except race).
* **Not** excluded (edge cases, as coded): the attack during an ongoing ground attack (`+0x5b4 ≠ 0`, e.g. charge run 9/10 or the
  rebound 6/7), and releasing during an already running special attack ⇒ sound 9.

### 1.6 Animation 0x13

Record `0x4b6180 + 0x13·0x1c` = `{sub {86, 0, 0, 0}, prio 5500, speed 3.0, restart 1}` (certain, from the exe; the port has it in `log_anim`).
After `.ins` 86 the chain follows with `.ins` 0 (idle). The Request happens **once**; the controller `0x436a50` holds 0x13 because the requests
from the normal animation choice in the following frames have a lower priority (idle 1100, walk 1500/1501, fall 5000, attacks 1600..1700). Only
the death animations (6000) and the script actions (6000) beat it. Invulnerable ⇒ no hit animation. `.ins` 86 has no camera-node track (flag 0x80)
in any of the three models, so no camera movement from the animation.

Animation events of the root node of `.ins` 86 (same in W1A/K1A/S1A):

| frame | type | ref | chance | vol | pitch | dmin |
|---|---|---|---|---|---|---|
| 0 | 4 (sound) | 114 (bank 0 = `Common/<character>.rck`, 22050 Hz mono, 2.056 s, swelling) | 0..100 % | 50 | 100 % | 200 |
| 1000 | 4 (sound) | 115 (same, 1.842 s, hard low thud that decays) | 0..100 % | 50 | 100 % | 200 |

The Perso plays them **2D** (SOUND.md §3). Frame 1000 of 2440 = 5.0 s of 12.2 s ⇒ 1.667 s at speed 3. The "description" of the sounds
comes from an RMS/zero-crossing measurement of the samples (uncertain, I can't hear them).

## 2. The hit

### 2.1 Who is in `0x4c5258[]` (certain)

The list is the previous frame's copy (`0x40bf60` → `0x4c5258`/`0x4c5324`) of the registration list `0x4c4d80` (max **32**, `0x40c0b0`:
full list ⇒ silently not added). `0x40c0b0` is only called by the Perso itself (`0x44b731`, skipped by `t != p`) and by
`vtbl[47]` of the enemy classes, from `Enemy_Think` (`vtbl[3]` `0x41a320`, **before** the 3000-activation test, but after "not cinematic").
`vtbl[3]` only runs for instances in the per-frame-built list `world+0x64` of the **visible sectors** (`0x42a980`, BONUS §3.1).

| class | `vtbl[47]` | registered in states |
|---|---|---|
| 4/5/6 | `0x419460` | 0..11 (not 12 = dead) |
| 7/8/9 | `0x417f20` | all except 10 (dead) |
| 10 (no level) | `0x416030` | unknown |
| 11 (no level) | `0x412d50` | not 10 |
| 12 (W2B boss) | `0x411970` | not 1 and 13 |
| 13 (ghost) | `0x414470` | 0..10 and 13 |
| 14/15/16 (bosses) | `0x41a4d0` | always |

**Not** on the list: bonuses, crates 120/121, bombs (40), class 17, lasers, launchers, rockets, switches, base instances
(OBJECTS.md §0 item 1; BOMB_CARRY.md). During a cinematic no enemy registers itself ⇒ empty list.

### 2.2 No range, no cone, no height (certain)

The pass (`0x458d9f..0x458e22`) calls `vtbl[34]` and copies the result to `[esp+0x10..0x18]`; afterward that local is never
read again. The only arguments of `vtbl[39]` are `(p, 3.0, &zerovec, &p+0x1f4, 2)`. The special attack thus hits **every**
registered actor, wherever it is — in practice everything alive in the sectors the camera can see (and at most 32).

### 2.3 `vtbl[39]` / `vtbl[38]` per class (certain, read per function)

`vtbl[38]` is the same `0x4078a0` = `ret 4` for **all** classes 4..16 (vtable dump of `0x4a9ec0`, `0x4a9dc0`, `0x4a9cb8`, `0x4a9ab0`,
`0x4a99b0`, `0x4a9bb0`, `0x4a98a8`, `0x4a9778`, `0x4a9658`, slot 38). So `vtbl[38](3)` after a lethal hit does nothing.

| class | `vtbl[39]` | with (3.0, dir 0, pt = Woody's feet, kind 2) | result |
|---|---|---|---|
| 4/5/6 | `0x419480` | `hitT (+0x158) > 0` ⇒ false. Otherwise state 9, AnimCtrl reset, `hitT = vtbl[53](0)` (0.25 s), `Enemy_TakeDamage(…, kind 0)` | hp 1..2 ⇒ **dead** |
| 7/8/9 | `0x417f60` | same with state **4** | hp 1..3 ⇒ **dead** |
| 10 | `0x416070` | state 8, `hitT`, `Enemy_TakeDamage(…, kind 1)` | (unused) |
| 11 | `0x412d90` | state 4, `hitT`, kind 0 | (unused) |
| 12 | `0x411ab0` | only kind 0 or 1 (`0x411ab5..0x411abe`) ⇒ on kind 2 **nothing**, false | **immune** |
| 13 | `0x414490` | logs `'TimeHit : %f'`, state 6, `hitT`, kind 0 | hp 3 ⇒ **dead** |
| 14 (Buzz) | `0x40fe90` | only in the low phase (`+0x224 == 0`) and `hitT ≤ 0`: state 9, `hitT`, SoundFx 42/47, smoke/explosion (BOSS14 §6.2), `Enemy_TakeDamage(…, **1.0**, kind **2** passed through)` | **1 of 5** hits, no hit star |
| 15 | `0x40e720` | `xor al,al; ret 0x14` | **immune** |
| 16 | `0x40d480` | only state 3/4: `+0x26c −= 0.2`, `+0x298 = 0`, state 5, `Enemy_TakeDamage(…, 3.0, kind 0)`, stop SoundFx 0x42 (`0x468a30`), SoundFx 0x41; hp ≤ 0 ⇒ state 7; otherwise `AnimCtrl->vtbl[2](2)`; **always returns true** | 3 damage (boss 16's hp not analyzed) |

`Enemy_TakeDamage` `0x41adc0(att, dmg, dir, pt, kind)`:
```c
if (!Behav_Knock(e->behav /*+0x11c*/, dir)) return false;   /* 0x41b6b0: knockback in progress ⇒ refused, NO damage (state/hitT are already set) */
if (kind != 2) Effect_HitStar(pt);                          /* 0x40c2d0 → 0x4750e0(&copy of *pt); pt == 0 ⇒ nothing */
e->hp /*+0x150*/ -= dmg;  return e->hp <= 0;
```
`Behav_Knock` with `dir = (0,0,0)`: `knockDir = 0`, `knockT = vtbl[53](−1)` ⇒ no displacement. Because 4..9, 11 and 13 set the kind to 0 (10: 1),
**every hit ordinary enemy gets a star at Woody's feet** (§3.4) — the hit point is `&p->pos`, not the enemy. Not for Buzz (kind 2
passed through). Death itself (stars above the head `vtbl[57]` `0x477610`, fade, removal) is handled by the enemy in its own automaton
(ENEMY.md §6.2); no script message, no SetVar (except Buzz's own mailbox in state 12, BOSS14 §6.3).

The max hp of the ordinary types is 3 (`0x41d510`, ENEMY.md §2.3: types 9 and 13), so 3.0 kills all of them, unless the script has raised their
hp via message 11.

### 2.4 World objects

Nothing. Peck switches (1050/1042) read action **6**, not 11; crates only react to bomb explosions (`0x44d650`); bombs, lasers, climbing walls and
bonuses are not in the actor list. There is no other reader of `+0x750` or `+0x74c` than `0x458bf0` and Reset.

## 3. Effects and sound

All particles use the pool `[0x5e823c]+0xdb8` (records of 0x50 B, max **2000** = `0x7d0`, counter `+0x27eb8` = `0xdb8 + 0x27100`, no
free list: full ⇒ the record is silently skipped). Record: `+0` age, `+4` lifetime (−1.0 = gone), `+8..` data, `+0x4c` callback.
The driver `0x470c70` calls every callback once per frame (after the world, before the HUD); each callback adds `dt = [[0x509adc]+0x38]`
itself. `[0x5e823c]+0x27ec0` = **the actor with category 1** (the Perso), set every frame by `0x46d120` (`0x46d151`).

### 3.1 The emitter `0x47ab90` → callback `0x47a8d0` (certain)

`0x47ab90`: record `{age 0, lifetime 2.5 (0x40200000), +8 acc = 0, +0xc flags = 0, callback 0x47a8d0}`. **No position**: the children
read Woody's position themselves.

```c
void SpecialEmitter(Rec *r)                                    /* 0x47a8d0 */
{
    r->age += dt;  float acc = r->acc /*+8*/ + dt;  r->acc = acc;
    float u = r->age / r->life;                                /* life 2.5 */
    if (u >= 1.0f) { r->life = -1.0f; return; }                /* 0x47ab75 */
    if (u < 0.5f /*[0x4a9014]*/) {                             /* first 1.25 s */
        int n = ftol(acc * 250.0f /*[0x4ab144]*/);  r->acc = acc - n * 0.004f /*[0x4abd80]*/;   /* 250 per second */
        while (n-- > 0) {
            Rec *s = PoolNew(); if (!s) continue;              /* full: skip */
            s->age = 0; s->life = 0.5f; s->cb = 0x47a790;
            int a = ftol(rand01() * 512.0f /*[0x4a9874]*/), b = ftol(rand01() * 512.0f);
            s->v.x =  400.0f /*[0x4a964c]*/ * tab[a & 511] * tab[b & 511];                 /* 400·cos a·cos b */
            s->v.y = -400.0f /*[0x4abd7c]*/ * tab[(a + 128) & 511];                        /* 400·sin a */
            s->v.z = -(400.0f * tab[a & 511]) * tab[(b + 128) & 511];                      /* 400·cos a·sin b */
        }
    }
    /* one ring per window, at most one per frame (flags +0xc bit 0/1/2): */
    int w = u >= 0.9f ? 4 : u >= 0.8f ? 2 : u >= 0.7f ? 1 : 0;  /* [0x4a94b8] 0.9, [0x4a987c] 0.8, [0x4aa1d8] 0.7 */
    if (w && !(r->flags & w)) {
        Rec *g = PoolNew();
        if (g) { g->age = 0; g->life = 0.5f; g->cb = 0x47a4c0; g->c = Perso->+0xc; }      /* the feet, captured NOW */
        r->flags |= w;                                         /* even if the pool was full */
    }
}
```
So: streaks for t ∈ [0, 1.25) s, rings at t = 1.75, 2.0, 2.25 s (emitter-u 0.7/0.8/0.9). The streaks are **time based** (not per
frame); on average ~125 alive at once.

### 3.2 Streak `0x47a790` (certain)

```c
void SpecialStreak(Rec *s)                                     /* 0x47a790, sprite object S = [0x5e823c]+0xb00 */
{
    s->age += dt;  float u = s->age / s->life;                 /* life 0.5 */
    if (u >= 1.0f) { s->life = -1.0f; return; }
    vec3 C = Perso->+0x60 + (0, 100.0f /*[0x4a9010]*/, 0);     /* +0x60 = feet + (0, P+0 = 43, 0) (0x44bf10) ⇒ C = feet + (0,143,0), recomputed EVERY FRAME */
    float k = 1.0f - u * u;
    S.p0 /*+0x278*/ = C + s->v;                                /* fixed point on the sphere, r = 400 */
    S.p1 /*+0x284*/ = C + k * s->v;                             /* head: from the sphere toward C, accelerating */
    S.rgba0 /*+0x290*/ = (1, 1, 1, 0);
    S.rgba1 /*+0x2a0*/ = (1, 1, 1, u <= 0.7f ? 1.0f : 1.0f - (u - 0.7f) * 3.33333f /*[0x4aa3a8]*/);
    S.halfWidth /*+0x2b0*/ = 2.0f;  S.tex /*+0x2b4*/ = 0x10000;   /* bank 0 image 0 (32×32 horizontal soft band) */
    Line(&S, 0xe00);                                           /* 0x471a10: own width 0x400, own colours 0x800, texture 0x200 */
}
```
The line primitive `0x471a10` (OBJECTS.md §2.1) builds a camera-facing quad from p0 to p1 with half-width 2.0 **world units**
(the perpendicular is determined in 2D projection and applied in view space), skips it if the projected length < 0.01 (`[0x4a94f8]`) and
submits it with `0x481560(…, 4, verts, tex, 0x24)` = **additive**. Visual: for 0.5 s a white streak shoots from a point 400 from
Woody's waist toward him; the tail (p0) is black (alpha 0), the head white, the last 30 % fades the head out.

### 3.3 Fire ring `0x47a4c0` (certain)

```c
void SpecialRing(Rec *g)                                       /* 0x47a4c0 */
{
    g->age += dt;  float u = g->age / g->life;                 /* life 0.5 */
    if (u >= 1.0f) { g->life = -1.0f; return; }
    float r0 = 4000.0f /*[0x4aa318]*/ * u, r1 = 4000.0f * (u + 0.2f /*[0x4a9760]*/);
    for (int i = 0; i < 16; i++) {                             /* edi = 0..0x2000 step 0x200, angle = edi/16 ⇒ 32/512 = 22.5° per segment */
        int a0 = 32 * i, a1 = a0 + 32;
        Vtx v[4];                                              /* 0x40 B per vertex: xyz +0, rgba +0x24..+0x30, uv +0x34 */
        v[0] = { c.x + r0*tab[a0], c.y, c.z - r0*tab[a0+128],  rgba (0.5, 0.5, 0.5, 0),     uv (0,0) };   /* inner */
        v[1] = { c.x + r1*tab[a0], c.y, c.z - r1*tab[a0+128],  rgba (0.5, 0.5, 0.4, 1 − u), uv (1,0) };   /* outer */
        v[2] = { c.x + r1*tab[a1], c.y, c.z - r1*tab[a1+128],  rgba (0.5, 0.5, 0.4, 1 − u), uv (1,1) };
        v[3] = { c.x + r0*tab[a1], c.y, c.z - r0*tab[a1+128],  rgba (0.5, 0.5, 0.5, 0),     uv (0,1) };
        Submit([0x5e86ac], 4, v, [0x5e866c] + 0xef4 /*bank 0 image 33*/, 0x44);   /* 0x481560, TWICE with the same arguments */
        Submit([0x5e86ac], 4, v, [0x5e866c] + 0xef4, 0x44);
    }
}
```
`c` = Perso`+0xc` (= the feet) at the moment of spawning; the ring does not follow Woody. Flat at foot height (y = c.y), no
ground following. Flag 0x40 = world vertices (transformed with `renderer+0x114`), 4 = additive, bit 1 not set ⇒ no culling (visible
from below too). Image 33 = 64×64 fire gradient: u = 0 black, via red/orange to white-yellow at u = 1, with horizontal streaks; u runs from the
inner edge (black) to the outer edge (white-hot). Inner radius 0 → 4000, outer radius 800 → 4800 in 0.5 s (8000 units/s), band 800 wide.

### 3.4 Hit star `0x4750e0(&pt)` (certain; per hit ordinary enemy, at `pt` = Woody's feet)

This is the general hit effect (also used by the peck and by message 1507). Ported as `game_hit_star` (main_engine.c);
since round 29 the ordinary peck hits get it too (`attack_hit_loop` → `enemy_hit` with the hit point of PERSO_JUMP.md §3:
the beak vector `p+0x59c` = marker typecode 0 of Woody's mesh, `beak_vector` in player.c).

* **Flash** (1 record, lifetime **0.1 s**, callback `0x475040`, pos = pt): billboard, image `0x10009` = **bank 0 image 9** (64×64 white
  flash), half diagonal `u · 250` (`[0x4ab144]`), rgb (1,1,1), alpha **0.4**, sprite mode 0x12, draw flags **3** (billboard, own colour,
  additive, no rotation).
* **8 sparks** (callback `0x474e00`, lifetime **0.4 s**): base `U, V` = `0x46d320(normalize(camera position − pt))` (two unit vectors perpendicular
  to the view direction). For spark j = 0..7 (`ebp = 64·j`):
  `a1 = ftol(rand01()·50 + 64j − 25)`, `a2 = ftol(rand01()·50 + 64j − 25)` (two **independent** draws, `[0x4a9030]` = 50, `[0x4abc64]` = 25),
  `dir = normalize(U·tab[a1] + V·(−tab[a2+128]))` ⇒ 8 spokes every 45° ± 17.6° in screen space;
  `spin = rand01()·2 > 1.0` (`[0x4abcc0]` double 1.0); `size = rand01()·20 − 5 + 25` = **20..40** (`[0x4a9994]` 20, `[0x4a9884]` 5).
  Per frame (u = age/0.4):
  `i = ftol(u · 128)` (`[0x4a9020]`), `head = pt + dir · (−150)·tab[(i+128) & 511]` = `pt + dir · 150 · sin(π/2 · i/128)` (`[0x4abcb8]` = −150);
  line `0x471a10(0xe00)` from pt (rgba (1,1,1,0)) to head (rgba (1,1,1,**0.1**)), half-width **10** (`0x41200000`), image `0x10007` =
  **bank 0 image 7** (speed streaks); then a sprite at the head: image `0x10008` = **bank 0 image 8** (yellow star), half diagonal = size,
  rgb (1,1,1), alpha `1 − u`, rotation `spin ? ftol(256u) : ftol(511 − 256u)` in 1/512 turns (`[0x4aa3ac]` 256, `[0x4abc90]` 511), mode 0x12,
  flags **7** (billboard, own colour, rotation, additive).

### 3.5 Colour convention of the additive path (certain; important for the port)

`0x481560` with flag 4 (additive, `0x481d8c`) sets the vertex colour as `byte = ftol(a · c · 128)` per channel (`[0x4a9020]` = 128,
`0x481e5e..0x481f31`), without an alpha byte; the transparent/opaque path (flag 8/2) uses `c · 255` and `a · 255` (`[0x4aa308]`). With
COLOROP MODULATE2X (LIGHTING §1.5) the effective factor is thus:

* **additive (ONE/ONE)**: `texture × c × a` — alpha acts as a pre-multiplication (and the ONE/ONE blend itself ignores alpha);
* **transparent/opaque**: `texture × 2c`, alpha `a` — here 0.5 is neutral.

The sprite route `0x470f10` copies the colour unchanged if the device supports MODULATE2X (`0x4710b7`) and doubles it otherwise
(`0x471224`: `fadd st0,st0`), with the same end result. For the port (`hud_world_fx`/`world_line` do `rgb × alpha` on ONE/ONE, without 2×):
**take c and a from the original unchanged** for additive effects. Consequences here: streak head 1.0, tail 0; ring outer edge
`(0.5, 0.5, 0.4)·(1−u)`, inner edge 0 — submitted twice ⇒ net `(1.0, 1.0, 0.8)·(1−u)`; flash 0.4; spark line 0 → 0.1; star `1 − u`.
**Side finding**: BONUS.md §2.4 ("colour (0.5,0.5,0.5) = full white") and `fx_update` in `main_engine.c` (white = {1,1,1} for the original 0.5)
compute with the 2×-rule of the transparent path; for the additive path this reading suggests a factor of 2 too bright. Not
changed in this round; verify against the original (screenshot of a pickup effect) before adjusting.

### 3.6 Camera and rumble

* `0x41fbb0([0x4c737c], 2.0)` (`0x458d60`: `push 0x40000000`): remaining shake time `cam+0x67c = 2.0` (**set**, not max), `cam+0x694 = 0`.
  The shake itself (CAMERA.md §6.3, `0x41fbd0`): only the view direction shakes, ±5·min(t, 3) per axis — with t = 2.0 that's ±10 decaying. The
  port has this as `game_cam_shake(t)`.
* `0x44d1b0(p, 1.0, 1.0)` = joystick force feedback `[0x5e618c]->vtbl[2](1.0, 1.0)` (PERSO_JUMP §0); not portable.
* No screen flash, no colour filter, no slow motion, no fade (checked: `0x458bf0` only calls `0x467440, 0x44bcf0, 0x436b90,
  0x44cce0, 0x44cd10, 0x47ab90, 0x40c350, 0x468a00, 0x41fbb0, 0x44d1b0, 0x43ff80` and the two vtable slots).

### 3.7 Sound

| when | what | source |
|---|---|---|
| start | ref 114, vol 50, 2D | anim event `.ins` 86 frame 0 |
| 1.667 s | ref 115, vol 50, 2D | anim event frame 1000 |
| failed | SoundFx 9 = ref 107 (0.40 s), vol 50, 2D | `0x458cd5` |
| Buzz hit | SoundFx 42 (W1B) / 47 | `0x40fef1` |
| boss 16 hit | stop SoundFx 0x42, play 0x41 | `0x40d4e6`, `0x40d4f5` |
| ordinary enemy hit | only their own animation events (hit animation) | |

### 3.8 HUD: "minus 1" of the charge counter (certain)

`0x44ae60(p, 0)` (every frame) → `0x448300(hud, +0x254, 0)`: if `hud+0x1c > new` ⇒ `0x462380(animator)` and flag `hud+0x41 = 1`; then
`hud+0x1c = new`. The tick `0x462020(hud+0x1c)` runs from `0x4480d0` (`0x448293`) as long as `+0x41` is set **and the charge pickup
animation (`+0x3a`) is not running**; its return value becomes `+0x41`. It uses the same objects as the pickup (IconSlide `animator+0x20`, Plate
`+0x2c`, NumPopup `+0x10`) and only shows the **old** value (`new + 1`); the new value never appears (outside the pause menu
`0x447660` doesn't draw the charge row, HUD_TEXT §4.6).

Start `0x462380`: IconSlide `(−63, 221) → (16, 221)`, sprite 6 (`0x47c1a0`; `[0x4ab6d8]` 63, `[0x4b3a30]` 16, `[0x4b3a34]` 221);
Plate `(50, 266)` from 0×0 to 34×34 (`0x47bf90`; `[0x4b3a38]` 50, `[0x4b3a3c]` 266, `[0x4ab700]`/`[0x4ab704]` 34); NumPopup on anchor A2
(`[0x5d7b60]`/`[0x5d7b64]` = (66, 282)), 2 phases, 17 → 37, 0.2 s (`[0x4b3a88]` 17, +`[0x4a9994]` 20); `+0x64 = 0`, `+0x68 = 1.0`,
bytes `+0x77` (pop), `+0x7b` (slide), `+0x7e` (plate) = 1, phase `+0x50 = 1`.

| phase | while | draws | then |
|---|---|---|---|
| 1a | slide runs (0.2 s) | the sliding-in icon (sprite 6) | |
| 1b | plate runs (0.2 s) | sprite 6 in slot 4 (16, 221) + growing circle (sprite 8) | |
| 1c | pop runs (0.4 s) | sprites 6/8 + number `old` 17→37→17 | |
| 1d | one frame | number `old` (17) + sprites 6/8; start pop (A2, **1 phase, 17 → 0**, 0.2 s), slide `(16,221) → (−63,221)`, plate 34×34 → 0×0 | phase 2 |
| 2 | `+0x64 += dt` until > 1.0 s | number `old` (size 17) + sprites 6/8 | phase 3 |
| 3a | pop runs (0.2 s) | sprites 6/8 + the shrinking number `old` | |
| 3b | plate runs (0.2 s) | sprite 6 in slot 4 + shrinking circle | |
| 3c | slide runs (0.2 s) | the sliding-out icon; end ⇒ `+0x41 = 0` | |

Total ≈ 2.4 s. All linear, colour `0xfe808080`, red number (HUD_TEXT §4.6, NumPopup).

## 4. The failure path and the script

* Releasing action 11 while a condition fails ⇒ `0x40c350(p)` (subtype); ≠ 4 and ≠ 5 ⇒ `SoundFx(9, 0)` (`0x458cc1..0x458cd5`). Nothing
  else: no animation, no HUD, charge unchanged. This also applies to releasing during an ongoing special attack, in the air, dead, while
  carrying a bomb, etc.
* **No** message goes to the VM (no `0x443e50`/`0x443e90`/`0x443ca0` in `0x458bf0` or the effect functions), no msgmask bit, no
  script variable. The only indirect script consequences are those of the targets themselves (Buzz's mailbox on his death, BOSS14 §6.3).
* The charge is stored in the savegame (`slot+0x14`, BONUS §2.3) and written out the same way as on pickup at the end of the level.

## 5. Port recipe

Goal: everything above, with the existing building blocks. Files: `src/player.h`, `src/player.c`, `src/enemy.h`, `src/enemy.c`,
`src/main_engine.c`, `src/hud.h`, `src/hud.c` (and optionally `src/render_gl.c` for RCtrl). CRLF files: `player.c`, `render_gl.c`.

### 5.1 Input

* `PlayerInput` (`player.h`): `int special;` (action 11, held).
* `main_engine.c` (around line 2520, next to `pin.action`): `pin.special = (!fly && win.keys['E']) || win.keys[VK_RCONTROL] || (special_at >= 0 && now - t0 >= special_at && now - t0 < special_at + 0.1);`
  plus a test option `--special T` (like `--peck`). E is free outside the fly camera (there = up).
* RCtrl like the original: `WM_KEYDOWN/UP` delivers `VK_CONTROL` for both Ctrl keys; in the WndProc of `render_gl.c` (line 21/22)
  extra `if (wp == VK_CONTROL) w->keys[(lp & 0x1000000) ? VK_RCONTROL : VK_LCONTROL] = down;` (bit 24 = extended key = right). To make RCtrl
  exclusive to the special attack, bind the attack to `VK_LCONTROL || VK_SHIFT` instead of `VK_CONTROL`.
* Giving a charge for tests: the existing `--pickup 35 T` (real pickup path, `special_charges++`).

### 5.2 Player fields (`player.h`)

`int special_st, special_prev; float special_t;` — `+0x750`, previous button state (for the release edge), `+0x74c`.
Clear on respawn (`player.c` ≈ line 896, Reset `0x44ab20`: `p->special_st = 0; p->special_t = 0;`) and when binding/loading a level.
**Do not** clear on `player_place` (teleport 26): the original only does that in Reset.

### 5.3 `special_update` (`player.c`, new, next to `attack_trigger`)

```c
/* ---- special attack 0x458bf0 (docs/PERSO_SPECIAL.md): action 11 on RELEASE, charge Perso+0x254 ---------------- */
static void special_hit(Player *p);
static void special_update(Player *p, const PlayerInput *in, float dt)
{
    int released = !in->special && p->special_prev; p->special_prev = in->special;   /* 0x467440(11): bit 31, one frame */
    if (released) {
        int state0 = player_state_free(p) && !p->race_char;                      /* Perso+0x21c == 0 */
        if (p->on_ground && state0 && p->special_st == 0 && p->special_charges > 0) {
            float T = anim_len(p, 0x13, 0);                                       /* .ins 86: 12.2 s / 3 = 4.067 s */
            p->special_charges--; p->special_st = 1; p->special_t = 0;
            if (p->move_lock < T) lock_move(p, T);                                /* 0x44cce0(T, 0) = max */
            if (p->invuln_respawn < T) p->invuln_respawn = T;                     /* 0x44cd10: +0x270 = max */
            game_special_fx();                                                    /* 0x47ab90 */
        } else if (!p->race_char) audio_fx(9, NULL, NULL);                        /* subtype 4/5 stay silent */
    }
    if (p->special_st == 1) {
        p->special_t += dt;                                                       /* the start frame counts too */
        if (p->special_t >= 1.5f) { game_cam_shake(2.0f); p->special_st = 2; special_hit(p); }   /* [0x4aa184]; rumble not ported */
    } else if (p->special_st == 2) {
        p->special_t += dt;
        if (anim_len(p, 0x13, 0) <= p->special_t) p->special_st = 0;
    }
}
```
Call in `player_update` **immediately after** `if (p->game_state == 0) return;` (before the early returns for script action, rocket and
climbing wall): the original ticks the automaton in every Perso state and plays sound 9 if you release in those states. `p->on_ground` is
then the previous frame's, just like `+0x22c` in the original. No test on `p->atk` (the original doesn't test `+0x5b4`).

Animation choice (`player.c` ≈ line 1476, the if-chain of `Perso_AnimState`): right after `else if (p->hit_anim_t > 0) want = p->hit_anim;`
add `else if (p->special_st) want = 0x13;` — so below the death/race animations (prio 6000) and above bomb, attack, walk and idle
(prio ≤ 5200). `anim_request` restarts .ins 86 automatically (restart 1, and 0x13 can't start twice in a row). The idle counter: in the
original, `0x464500` keeps running during the attack (the idle request only loses on priority); anyone who wants that exactly should in
this branch call `idle_anim(p, dt)` without using the result and set `idling = 1`. Otherwise the port resets the counter (small difference).

### 5.4 The hit (`player.c` + `enemy.c`)

```c
static void special_hit(Player *p)                                               /* 0x458d89..0x458e28 */
{
    if (!p->enemies) return;
    for (int i = 0, n = 0; i < p->enemies->n && n < 32; i++) {                   /* 0x4c5258[0x4c5324], max 32 */
        Enemy *e = &p->enemies->e[i];
        if (e->removed || !e->attackable || !e->inst->visible) continue;          /* "registered last frame" (vtbl[47]) */
        n++;
        enemy_hit(e, 3.0f /* P+0x94 */, (Vec3){ 0, 0, 0 }, p->pos, 2);            /* vtbl[39](p, 3.0, &0, &p->pos, 2); vtbl[38](3) = ret 4 */
    }
}
```
* **No** distance, cone or height test. The target filter is an approximation of "Think ran last frame": the port has no
  per-enemy sector list; anyone who wants it tighter can run the PVS test of `instance_visible` (`render_gl.c`, `r->sec_vis[]` of the previous
  frame) on `e->inst`. Without that, the port hits every living enemy of the level (uncertain how much that matters in practice; W1A's
  enemies are spread across a few corridors).
* `enemy.c`: new `int enemy_hit(Enemy *e, float dmg, Vec3 dir, Vec3 pt, int kind)` = `vtbl[39]` with a hit point and kind; the existing
  `enemy_take_damage(e, dmg, dir)` stays as an `enemy_hit(e, dmg, dir, pt, 0)`-style wrapper for the peck. Behaviour:
  * type 14: `boss_take_damage(e)` (always 1.0); no hit star if `kind == 2`, otherwise `game_hit_star(pt)` if the hit was accepted;
  * types 4..9 and 13: exactly the current `enemy_take_damage`, and **if the hit was accepted** (not dead, `hit_t <= 0`, `knock_t <= 0`)
    `game_hit_star(pt)` (kind becomes 0 ⇒ star also for the special attack, at Woody's feet);
  * types 12 and 15 (not yet ported): nothing; 16: state 3/4, damage 3, SoundFx 0x41 (not ported).
  * **Do not** play `audio_fx(6)` on a killed enemy: SoundFx 6 belongs to the bomb explosion (`0x44d730`, ENEMY.md §8 and BOMB.md); the peck loop
    in `player.c` (≈ line 704) currently does that — that is an existing port bug, do not carry it over.
* The peck loop can use the same `enemy_hit` with the real hit point (dash: `p+0x59c`, charge run: `lerp(a, b, 0.5)`, PERSO_JUMP §3) so that it
  also gets the hit star; the dash gives kind 1, the charge run 0.

### 5.5 Effects (`main_engine.c`, next to `game_pickup_fx`/`fx_update`)

* Extend `FxRec`: `Vec3 v; float size; int flags;` and kinds `3` = emitter `0x47a8d0`, `4` = streak `0x47a790`, `5` = ring `0x47a4c0`,
  `6` = flash `0x475040`, `7` = spark `0x474e00`. One pool of 2000 as now (full ⇒ skip; the emitter also sets its ring flag if the ring
  didn't fit).
* `void game_special_fx(void)` (`0x47ab90`): `fx_new(2.5f, (Vec3){0}, 3)` with `acc = 0`, `flags = 0`.
* `void game_hit_star(Vec3 pt)` (`0x4750e0`): one `fx_new(0.1f, pt, 6)` and 8× `fx_new(0.4f, pt, 7)` with `v = dir`, `size = 20..40`,
  `flags = spin`, per §3.4 (base from the camera position: `U`, `V` = two unit vectors ⟂ `normalize(eye − pt)`).
* `fx_update(float dt)` → `fx_update(const float *eye, float dt)` (the call at ≈ line 2649 passes `&cam.pos.x`); per kind:
  * 3: §3.1 verbatim (`acc`, 250/s while u < 0.5, `v` per streak, ring window 0.7/0.8/0.9 with `flags` bit 0/1/2, ring centre = `g_player->pos`);
  * 4: `C = g_player->pos + (0, 43 + 100, 0)` **every frame**, `k = 1 − u²`, `hud_world_line_img(0, C+v, C+k·v, eye, 2.0, white, 0.0, u <= 0.7 ? 1 : 1 − (u − 0.7)·3.3333)`;
  * 5: 16 segments per §3.3 with `hud_world_quad(33, …)`, drawn **twice** (or rgb ×2 with clamping — twice is exact);
  * 6: `hud_world_fx(9, pt, 250·u, 0, white, 0.4)`;
  * 7: `i = (int)(u·128)`, `head = pt + v·150·sinf(1.5707963f·i/128)`, `hud_world_line_img(7, pt, head, eye, 10, white, 0, 0.1)`,
    `hud_world_fx(8, head, size, (flags ? (int)(256u) : (int)(511 − 256u)) / 512.0f, white, 1 − u)`.
  * Colours **without** the 2× correction (§3.5): white = {1,1,1} here really means c = 1.0 of the original. Watch the direction of the rotation in
    `hud_world_fx` (turns = angle/512); the sense of the spin is not visually distinguishable for a random `spin`.
* Order: `fx_update` already runs between `hud_world_sprites_begin/end`, after the world — as `0x470c70` does.

### 5.6 `hud.c` / `hud.h`

* Images: extend `fx_slot` with 7, 8, 9 and 33 (the next free slots; enlarge `GLuint fx[]` in `H` if needed); they are then
  loaded automatically by `common_item`. Image 0 is already in slot 0.
* `void hud_world_line_img(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b)`
  = `world_line(…, H.fx[fx_slot(image)])` (the existing `world_line` already does additive with `rgb × alpha`).
* `void hud_world_quad(int image, const float v[4][3], const float uv[4][2], const float rgb[4][3], const float alpha[4])`: additive
  (`GL_ONE, GL_ONE`), colour per vertex `rgb × alpha`, no culling, depth test on and depth write off like the other effects.
* "Minus 1" of the charge (§3.8) in `hud_anim_tick`: `int mcharge; float mcharge_t;` and `prev_charges` in `A`. Detection next to that of the
  lives: `if (s->charges < A.prev_charges && !A.mcharge) { slide_start(2, 6, -63, 221, 16, 221); plate_start(2, 50, 266, 0, 0, 34, 34);
  pop_start(2, 2, 17.0f, 37.0f, 0.2f); A.mcharge = 1; A.mcharge_t = 0; }`. Tick only if `!A.stage[2] && !A.fly[3].on` (the pickup
  `+0x3a` takes priority), with `old = s->charges + 1`, exactly the phases of the table in §3.8 (slide → plate → pop 17→37→17 → one frame: pop
  `(2, 1, 17, 0, 0.2)` + `slide_start(2, 6, 16, 221, -63, 221)` + `plate_start(2, 50, 266, 34, 34, 0, 0)` → hold 1.0 s with
  `number_sized(k_anchor[2]…, old, 17)` → pop → plate → slide). `A.prev_charges = s->charges` every frame; `hud_anim_reset` clears it.

### 5.7 Testing

* `extract/Data W1A --pickup 35 0.5 --special 2 --pos 652 -1985 -800` with `WOODY_ANIMLOG=1` (expect `lanim … -> 19`) and
  `WOODY_SHOTSEQ="out/sp 2.0 0.15 20"` (streaks until 3.25 s, rings at 3.75/4.0/4.25 s after loading), then `--special 1` with no charge ⇒ sound 9
  (`WOODY_SNDLOG=1`). W1A enemies 282/293 should both die at 3.5 s, each with a star at Woody's feet.
* Buzz: the existing W1B boss test (`--pos -5753 1800 -8901 --yaw -90 --walk 2`) + `--pickup 35` and `--special` in his low phase:
  `BOSS … hit` with 1 point.
* Invulnerability: `WOODY_KILLAT` / an enemy charge within 4.07 s of the start should not cost a heart; a pit should.

## 6. Certainty and open points

**Certain (instruction by instruction)**: all of §1.2; the absence of a range test; `vtbl[38]` = `ret 4` for all enemy classes; all
`vtbl[39]` variants in §2.3; the effect functions `0x47ab90`, `0x47a8d0`, `0x47a790`, `0x47a4c0`, `0x4750e0`, `0x475040`, `0x474e00` with their
constants; the colour conversion of `0x481560`; the HUD functions `0x448300`, `0x462380`, `0x462020`; the animation record and the anim events.

**Uncertain / not measured**:
1. Whether the list `world+0x64` (who gets Think) intersects the frustum in addition to `.vis` (BONUS §3.1 says "visible sectors"); that
   determines how many enemies the special attack really hits in the original. Measurable live with `tools/wtrace.py` on `[0x4c5324]` at the
   moment of the hit.
2. The meaning of ONE/ONE + pre-multiplication (§3.5) is derived from the code, not visually compared; likewise the inference about
   BONUS §2.4 and `fx_update`.
3. How the original handles a charge run already in progress when the special attack starts during it (§1.5); the controller `0x457a50` and
   the lock run in parallel then.
4. The "sound" of ref 114/115 (only measured).
5. Boss 16 (W3D): hp and states 3/4/5/7 not analyzed; boss 15 is certainly immune to every `vtbl[39]`.

### 6.1 Constants

| address | value | use |
|---|---|---|
| `0x4aa184` | 1.5 | time until the hit |
| `0x40000000` (imm) | 2.0 | camera shake (s) |
| `0x4631d6` (P+0x94) | 3.0 | damage |
| `0x463227`/`0x46322d` (P+0xb0/+0xb4) | 1.0 / 1.0 | rumble |
| `0x4b6180 + 0x13·0x1c` | {86,0,0,0}, 5500, 3.0, 1 | animation record |
| `0x40200000` (imm, `0x47abc0`) | 2.5 | emitter lifetime |
| `0x4a9014` / `0x4a900c` | 0.5 / 1.0 | emitter: spawn window / end |
| `0x4ab144` / `0x4abd80` | 250 / 0.004 | streaks per s / 1/250 |
| `0x4a9874` | 512 | random angle |
| `0x4a964c` / `0x4abd7c` | 400 / −400 | radius of the streak sphere |
| `0x4aa1d8` / `0x4a987c` / `0x4a94b8` | 0.7 / 0.8 / 0.9 | ring moments (emitter-u) |
| `0x3f000000` (imm) | 0.5 | streak and ring lifetime |
| `0x4a9010` | 100 | streak centre above `Perso+0x60` (= feet + 43) |
| `0x4aa3a8` | 3.33333 | fading out of the streak head after u = 0.7 |
| `0x40000000` (imm, `0x47a8a4`) | 2.0 | streak half-width |
| `0x10000` (imm) | bank 0 image 0 | streak texture |
| `0xe00` / `0x24` | flags | line primitive / submit additive |
| `0x4aa318` | 4000 | ring radius per unit u |
| `0x4a9760` | 0.2 | ring width in u |
| `[0x5e866c] + 0xef4` | bank 0 image 33 (0xef4/0x74) | ring texture |
| `0x3ecccccd` (imm) | 0.4 | blue of the ring outer edge; alpha of the flash |
| `0x44` | flags | ring: world vertices + additive |
| `0x3dcccccd` (imm) | 0.1 | flash lifetime; alpha of the spark-line head |
| `0x3ecccccd` (imm, `0x475216`) | 0.4 | spark lifetime |
| `0x4abcc0` (double) | 1.0 | spin threshold (`rand·2 > 1`) |
| `0x4a9994` / `0x4a9884` / `0x4abc64` | 20 / 5 / 25 | spark size 20..40; angle jitter ±25/512 |
| `0x4a9030` | 50 | angle jitter width |
| `0x4a9020` | 128 | spark phase; colour scale of the additive path |
| `0x4abcb8` | −150 | spark distance |
| `0x41200000` (imm) | 10 | spark-line half-width |
| `0x10007` / `0x10008` / `0x10009` | bank 0 image 7 / 8 / 9 | speed streaks / star / flash |
| `0x4aa3ac` / `0x4abc90` | 256 / 511 | star rotation |
| `0x4ab144` | 250 | flash size per u |
| `0x4aa308` | 255 | colour scale of the transparent/opaque path |
| `0x4ab6d8`, `0x4b3a30`, `0x4b3a34` | 63, 16, 221 | HUD: slide coordinates |
| `0x4b3a38`, `0x4b3a3c`, `0x4ab700`, `0x4ab704` | 50, 266, 34, 34 | HUD: plate |
| `0x4b3a88`, `0x4a9994`, `0x3e4ccccd` | 17, 20, 0.2 | HUD: number pop 17 → 37, 0.2 s |
