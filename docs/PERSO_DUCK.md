# PERSO_DUCK.md — ducking / lying flat (Perso `+0x694`, `0x465b10`), hitbox, camera, sound and port recipe

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`). All addresses are VAs; floats and tables were pulled from the
exe with a PE reader, animation durations from `extract/Data/W1A/W1A.ins` model 0 (Woody; W2A model 21 and K1A model 45 give the same durations).
Naming as in PERSO_MOVE.md / PERSO_FRAME.md: `p` = Perso, `P` = parameter block `p+0x110`, `M` = Mover `p+0x388`, `J` = jumper `p+0x334`,
`dt` = `p+0x2f8`, `A` = anim controller `p+0x494`, `B` = second controller `p+0x498` (surfboard, state 1 only).
`AnimLen(n)` = `0x436b90(n, 0)` = `.ins duration(rec[n].sub[0]) / rec[n].speed` (PERSO_DEATH.md heading).

Confidence: **certain** = read instruction by instruction; **derived** = follows from the code read and the order of the frame steps, but not
replayed in the original; **uncertain** = not (fully) read or not measured.

## 0. Summary (what the port needs to know)

- "Lying flat" = **ducking**: action 5 (default Space; in state 1 = race action 8). One function `0x465b10`, run each frame as a pre-step of
  Perso::Update, with a sub state machine `+0x694` (0 standing, 1 going down, 2 lying, 3 getting up) and a timer `+0x698`.
- Only starts **on the ground** with the key **held down** (not "just pressed"), not during an attack (`+0x5b4 ≠ 0`) and not dead. Lying lasts at
  least `AnimLen(0x31)` = **0.375 s**, getting up `AnimLen(0x33)` = **0.19995 s**; releasing the key only matters starting in sub 2.
- As long as `+0x694 ≠ 0`: every frame `LockMove(dt)` ⇒ `Perso_Move` gets no input and **all horizontal movement** (walking, sliding,
  knockback) is zeroed. No turning, no jumping, no pecking; but falling, being carried by a platform, and being pushed by actors still work.
- Body height `+0x118` = **61** instead of 193 (Woody): the collision cylinder for walls, **projectiles**, **lasers**, Boss14's cone, crushing
  and actor-pushing becomes 61 tall. So Woody can **duck under shots (> feet+66) and lasers (> feet+61)**. Enemy attacks (Touch,
  3D distance from the feet) and explosions (3D radius) do not take the height into account.
- Standing back up only happens if the vertical segment **feet+61 → feet+132** (not +193) is free of world and instance polygons; otherwise he
  keeps lying.
- Camera: the follow camera does **not** change; only the side view (mode 0x20) drops to "height on ↓" as long as the **key** 5 is held.
- Sound only via animation events (ref 32 when lying down, 33 when getting up). No code-driven sound.
- Port: `PlayerInput.duck` (proposed key **X**), `duck_update()` after `attack_trigger()`, `player_body_height()` for all height tests (§5).

## 1. The state machine `0x465b10`

### 1.1 Call site and order (certain)

Only caller: `0x44b797` in Perso::Update `0x44b530` (PERSO_FRAME §2.1). Order within a frame (each step only if `+0x690 == 0`):

1. `0x44b620..0x44b644`: if `+0x238 > 0` ⇒ `+0x238 -= dt` (no lower bound; can become slightly negative).
2. Pre-steps: `0x464ef0` (grab climbing wall) → `0x465e50` → `0x457a50` (attack controller) → `0x44ba70` (in state 0 `0x463430`
   pick up bomb, then `0x457330` attack trigger) → **`0x465b10` ducking** → `0x44b980` (action 7, look around) → `0x458bf0` (action 11, special attack).
3. `0x459c70` (side view, if `+0x4ec`), `0x45b0a0`, dispatch on state: 0/6 ⇒ `Perso_Move(p, 1)` `0x44bb20`; 2/3 ⇒ `Perso_Move(p, 0)`; …
4. `Perso_MoveCollide` `0x4624f0` (starts with `0x462490` = body height, §3.1), …, `Perso_AnimState` `0x463e60`.

So ducking sees **this** frame's attack state (trigger just ran), and its LockMove already applies to the `Perso_Move` of the **same** frame.

### 1.2 Pseudo-C (certain; jump table `0x465de4` = `{0x465b8e, 0x465c11, 0x465c43, 0x465d8d}`)

```c
void Perso_Duck(Perso *p)                                            /* 0x465b10 */
{
    if (p->state /*+0x21c*/ == 2 || p->atk /*+0x5b4*/ != 0) return;  /* 0x465b35, 0x465b43: dead or attacking ⇒ nothing, no LockMove either */
    int key;
    if (p->state == 1) { if (p->raceStartT /*+0x4e4*/ > 0) return;   /* 0x465b4e: during the race start animation */
                         key = Held(8); }                            /* 0x465b65 */
    else                 key = Held(5);                              /* 0x465b69; Held = 0x467400 (held down, not "just pressed") */
    switch (p->duck /*+0x694*/) {
    case 0:                                                          /* 0x465b8e */
        if (!key || !p->onGround /*+0x22c via 0x44bcf0*/) break;
        p->duck = 1;
        if      (p->state == 6) { A->Request(0x4e); p->duckT = AnimLen(0x4e); }                  /* carrying a bomb */
        else if (p->state == 1) { A->Request(0x68); B->Request(0x68); p->duckT = AnimLen(0x68); } /* race */
        else                    { A->Request(0x31); p->duckT = AnimLen(0x31); }                  /* 0x465c06: all other states */
        break;
    case 1:                                                          /* 0x465c11 */
        if ((p->duckT -= dt) <= 0) p->duck = 2;                      /* the key is NOT read here */
        break;
    case 2:                                                          /* 0x465c43 */
        if      (p->state == 6) A->Request(0x4f);
        else if (p->state == 1) { A->Request(0x69); B->Request(0x69); p->duckT = AnimLen(0x69); } /* timer unused */
        else                    A->Request(0x32);                    /* requested again every frame */
        if (key) break;                                              /* 0x465c98 */
        vec3 a = p->pos /*+0x1f4*/, b = p->pos;
        a.y += P[0x10];                   /* 61 (Woody) */           /* 0x465cd8..0x465cf5 */
        b.y += P[0x0c] - P[0x10];         /* 193 − 61 = 132 */       /* 0x465cf9..0x465d05 */
        Ray(&a, &b, -1);                                             /* 0x4359b0, §4.3 */
        if (g_hitKind /*[0x53a554]*/ != 0) break;                    /* blocked: stays down, tested again next frame */
        if      (p->state == 6) { A->Request(0x50); p->duckT = AnimLen(0x50); }
        else if (p->state == 1) { A->Request(0x6a); B->Request(0x6a); p->duckT = AnimLen(0x6a); }
        else                    { A->Request(0x33); p->duckT = AnimLen(0x33); }
        p->duck = 3;
        break;
    case 3:                                                          /* 0x465d8d */
        if ((p->duckT -= dt) <= 0) p->duck = 0;                      /* the key is NOT read here */
        break;
    }
    if (p->duck != 0) LockMove(p, dt, 0);                            /* 0x465db6: 0x44cce0(+0x2f8, 0) */
}
```

Notable points (certain):
* The switch uses the value on entry: the 0→1 transition and the first countdown step never fall in the same frame; the LockMove test at the
  bottom reads the **new** value (so LockMove already applies in the start frame, and no longer in the frame where 3→0 happens).
* Sub 1 and 3 only end **on the timer** (= duration of the first `.ins` segment of the chain), not on the end of the animation or the key.
  Sub 2 ends on **key released + segment clear**. If the segment is blocked, it is retested every frame as long as the key is released.
* Timers: counted down with `fsub dt; fcomp 0.0 (0x4a9004); test ah,0x41` ⇒ transition when `T ≤ 0`.
* The state-6 choice is made per frame: if the state changes during lying down (bomb picked up, see §2.6), the set changes along with it.

### 1.3 Entry conditions per situation

| situation | can he go down? | why |
|---|---|---|
| standing / walking / running (state 0) | **yes**, immediately; he stops **in one frame** (no run-out) | `onGround`, and LockMove zeroes horizontal movement (§2.2) |
| in the air (jumping, falling) | no | `+0x22c == 0`. If the key stays held, he goes down on the first frame with `onGround` (landing) |
| attack / peck-dash / charge run / knockback (`+0x5b4 ≠ 0`) | no (the function is skipped) | `0x465b43` |
| charging an attack (key 6 held, `+0x5b4 == 0`) | yes; the charge then drains (−dt in `0x457a50`) and releasing does nothing (`0x457388`) | |
| carrying a bomb (state 6) | yes, anims 0x4e/0x4f/0x50 (also during pick-up/ground throw: nothing tests `+0x58c`) | |
| looking around (state 3), climbing wall (4), scripted (5), 7, rocket (8), results (9) | not excluded: the state machine runs, with the 0x31 set; only the key and `onGround` decide. In 3, `Perso_Move(p,0)` is used, in 4/5/7/8/9 no `Perso_Move` | edge case, **derived** (not replayed) |
| dead (state 2) | no; `+0x694` **freezes** at its value | `0x465b35` |
| race (state 1) | yes with action 8, not while `+0x4e4 > 0` | RACE.md §4.8 (already ported) |

### 1.4 Animations (table `0x4b6180`, record `{sub[4], prio, speed, restart}`; certain)

| log. | chain (.ins) | prio | speed | restart | duration part 0 | loop | use | event sound (§4.2) |
|---|---|---|---|---|---|---|---|---|
| **0x31** | 24 → 25 loop | 1750 | 4.0 | 1 | 1.5/4 = **0.375 s** | 25: 5.0/4 = 1.25 s | going down (sub 0→1) | .ins 24: ref 32 |
| **0x32** | 25 loop | 1750 | 3.0 | 0 | – | 5.0/3 = 1.667 s | lying down (sub 2, every frame) | – |
| **0x33** | 23 → 0 | 1750 | 4.0 | 1 | 0.8/4 = **0.19995 s** | (0 = idle) | getting up (sub 2→3) | .ins 23: ref 33 |
| 0x4e | 58 → 60 loop | 1750 | 4.0 | 1 | 1.5/4 = 0.375 s | 60: 5.0 s | lying down with bomb | .ins 58: ref 32 |
| 0x4f | 60 loop | 1750 | 3.0 | 0 | – | 1.667 s | lying down with bomb | – |
| 0x50 | 59 → 47 | 1750 | 4.0 | 1 | 0.8/4 = 0.19995 s | 47 = carry idle | getting up with bomb | .ins 59: ref 33 |
| 0x68/0x69/0x6a | 5→6 / 6 / 7→0 (race model) | 1750 | 5/3/5 | 1 | model-dependent | | race (RACE.md) | |
| 0x23 | 26 → 25 loop | 5110 | 3.0 | 1 | 0.8/3 = 0.267 s | | hit while ducking (`0x464bfd`) | .ins 26: 55/56/57 |
| 0x24 | 64 → 60 loop | 5110 | 3.0 | 1 | 0.267 s | | hit while ducking with bomb (`0x464ba3`) | .ins 64: 55/56/57 |
| 0x2c | 36, one-shot | 6000 | 3.0 | 1 | 5.1/3 = 1.7 s | | died 3/4/5 on the ground with `+0x694 == 2` (`0x4647f9`) | .ins 36: ref 34 |

(`.ins` durations in 1/4096 s: 23 = 3276, 24 = 6144, 25 = 20480, 26 = 3276, 36 = 20889, 58 = 6144, 59 = 3276, 60 = 20480, 64 = 3276.)

Priority/controller (Tick `0x436a50`, certain): the request with the highest priority wins; if the current priority is higher than the new one,
the switch only happens once `inst+0xc0 == 1` = the chain is in its last (loop) part (`0x43f246`: slot0 == slot1, INSTANCE.md). Consequences:
* 0x31 (24) cannot be interrupted by anything with prio < 1750; after 0.375 s it is in loop 25 and 0x32 (same prio, restart 0) takes over seamlessly.
* After 0x33, .ins 0 plays as the loop part ⇒ the normal ground anims (idle 0 prio 1100, start-walk 2 prio 1501, walk 3) take over immediately once sub 3 ends.
* While lying down, `Perso_AnimState` in state 0/9 does **nothing** (`0x464630`: `+0x694 ≠ 0` ⇒ return, no idle counter either, no idle reset).
  In state 6, `0x4646b0` still requests its carry anims (0x40..0x45, prio ≤ 1501): those lose to 0x4e/0x4f/0x50 (1750). In state 1
  `0x464c74` only sets `+0x4bc = 0` (no leaning).

### 1.5 Timeline at 60 fps (derived from §1.2)

| frame | happens |
|---|---|
| 0 | key held and on the ground: sub 1, anim 0x31, `T = 0.375`, LockMove, height 61 (this frame's MoveCollide) |
| 1..23 | `T -= 1/60`; in frame 23, `T ≤ 0` ⇒ sub 2 |
| 24.. | sub 2: 0x32 every frame; key released ⇒ segment test |
| F | key released and clear: 0x33, `T = 0.19995`, sub 3 |
| F+1..F+12 | counting down; in F+12 ⇒ sub 0, **no more LockMove in F+12**: movement can happen in that same frame (and `+0x238 = dt_prev − dt ≈ 0`) |

Minimum (a tap on the key): 36 frames = 0.6 s blocked. Height 61 applies from frame 0 through F+11.

## 2. Moving, turning, jumping, attacking while lying down

### 2.1 `0x44cce0(t, force)` LockMove (certain)
`if (t > p->t238 || force) p->t238 = t;` (ret 8). Ducking calls `(dt, 0)`: a longer-running block (hard landing, special attack)
stays in effect. The port function `lock_move()` always overwrites and sets speed 0 – so for ducking, use `max` (§5.3).

### 2.2 `Perso_Move` `0x44bb20` with `+0x238 > 0` (certain; **correction to PERSO_MOVE §4**)
```c
if (p->M.pushT /*+0x474*/ > 0 || p->t238 > 0 || p->atk != 0) input = false;   /* 0x44bb48..0x44bb78 */
Mover_Update(&p->M, input);                     /* 0x45b110 */
vec3 h = M.velDir * M.dist;                     /* 0x44d1e0 → [esp+0x10] */
vec3 v = 0;                                     /* 0x43ff80 → [esp+4] */
if (p->t240 > 0) p->t240 -= dt; else { Jumper_Update(&J, Held(4), input); v = J.disp; }
if (p->t238 > 0) h = 0;                         /* 0x44bc16..0x44bc2f: 0x43ffa0 on [esp+0x10] = h (NOT v) */
p->disp = (use5bc ? v5bc : use69c ? v69c : h + v);  p->disp.y += dt * p->vy244;
```
PERSO_MOVE.md §4 writes `if (p->t238 > 0) v = 0;` – that is wrong: the stack slot after `pop edi; pop ebp` is the **horizontal**
vector (PERSO_FRAME §2.4 "d = 0" is correct). So while lying down:
* **no** walking, sliding (RampB, steep slope) or knockback movement (RampC): it's all in `h`;
* **but** vertical still works: the jumper still supplies `v` as normal (falling if the ground disappears);
* **but** everything added to `disp` **after** `Perso_Move` still applies: actor-pushing `0x4627d0` and platform movement `0x436d20` in `Perso_MoveCollide`.

### 2.3 Mover with no input (certain)
`0x45b110` with arg 0: flag 2 off ⇒ `0x45a1f0` (clears flags 8/0x10/0x20) and phase `M+0xc = 0` (`0x45b2bf`). `0x45a4b0` (input, turning)
does not run ⇒ **facing direction `M+0x10` does not change: he does not turn** (exceptions: the follow target `+0x68c`, `0x45b1b4..0x45b26a`, and
`Hit`, which sets the direction directly). RampA is not decelerated (no `0x467180`), but without flag 2 it doesn't count in `0x45ae80`;
RampB/RampC still tick but are discarded via `h = 0`. A leftover knockback (`M+0xec` 0.2 s + 0.5 s run-out) can therefore still take effect
after getting up (derived). Speed after getting up: phase 0 → 1 via `0x467130` (start accelerating) as soon as a direction is held; whether
RampA starts from its old value or from 0 there is **uncertain** (not read).

### 2.4 Jumping (certain)
`Jumper_Update(J, Held(4), input = 0)`: `pressed := false` and no re-arming (`input` is required). No jump, no coyote jump. Since the
jumper reads **Held(4)** (not "just pressed"): anyone holding the jump key while getting up jumps on the first free frame (F+12,
§1.5) — derived.

### 2.5 What happens if the ground disappears / on a slope / on a platform (derived)
* He can't walk off an edge (no horizontal movement). If the ground disappears (moving platform, pushed out by an actor): Jumper phase 2
  → 3 (`J_StartFall`), he **falls while ducking** (anim stays 0x32; `0x4642f0` is not called). Releasing in the air is allowed: sub 2 only
  tests key + segment, not the ground ⇒ he stands up in mid-air, then the air anims take over again.
* Steep slope (`n.y < 0.71`): he does **not** slide while lying down (RampB is inside `h`).
* Landing with the key held: he goes down on the first frame with `onGround`; on that frame or the next, Jumper phase 6 could apply falling,
  but `0x464630` is skipped ⇒ **no** landing anim 8/0xa, **no** LockMove(len 0xa), **no** shock/bounce `0x478980`. Fall damage
  (`0x44b220`, phase 6 and `fallen ≥ 1500`) still applies (it does not read `+0x694`).

### 2.6 Attacks and other actions (certain, per code location)
| action | while lying down | address |
|---|---|---|
| pecking / charge run / air attack / chain out of knockback | **blocked** (`+0x694 ≠ 0` ⇒ return, before anything else) | `0x457388` in `0x457330` |
| throwing a bomb (state 6, sub 2) | **blocked** | `0x463963` |
| picking up a bomb | **not** blocked: `0x463430` does not test `+0x694`/`+0x238` ⇒ state 6, he stays down (set 0x4f), the pick-up anim 0x45 (1500) loses to 0x4f (1750) | `0x46344e` |
| grabbing the climbing wall | not blocked (`0x464f42..0x464f72` only test `+0x5b4 ∈ {0,10}` and `+0x50c`) | `0x464ef0` |
| special attack (action 11) | not blocked (tests on the ground, state 0, `+0x750`, supply): anim 0x13 (prio 5500) and LockMove(len 0x13) | `0x458c0c..0x458c80` |
| looking around (action 7) | not blocked ⇒ state 3; ducking keeps running; eye height `0x44c080` = feet + 0.9·H = **54.9** while ducking | `0x44b980`, `0x44c0a4` |

Edge cases in the "not blocked" rows are derived, not replayed.

### 2.7 Being hit while lying down (certain for the code, order derived)
`Hit 0x44ca00` (PERSO_MOVE §4.4): knockback direction, **facing turned toward the attacker** (M+0x10/M+0x1c/RampA.dir), `0x463170(J,0)`,
`+0x280 = 0.6 s`, `+0x238 = 0` (`0x44cce0(0, 1)`), `+0x5b4 = 0`, hit anim `0x464b70`: on the ground + `+0x694 ≠ 0` (1, 2 or 3) ⇒ **0x23** (state 6 with
bomb: **0x24**, and `+0x58c = 2`). `+0x694` is not changed. Shots (step 32), enemies and bombs run after the Perso update, so the next
frame sets ducking's `+0x238` back to dt before `Perso_Move` ⇒ **no knockback movement** while lying down; he stays down. After 0x23
(0.267 s) the chain is in loop 25, which 0x32 continues seamlessly.

### 2.8 Dying while lying down (certain)
`Kill` sets state 2 and leaves `+0x694` as is; `0x465b10` no longer runs (frozen), `+0x238` is not refreshed (after one frame ≤ 0 ⇒ the
death movement of state 2 applies normally) and the body height stays 61. Animation (`0x4647c7`, PERSO_DEATH §3.2): kind 3/4/5 on the ground with
`+0x694 == 2` ⇒ **0x2c** (.ins 36, 1.7 s, one-shot) and **no** 0x29 after it (`0x46486e`); with 1 or 3 ⇒ the normal 0x26 → 0x29; in the air 0x25.
Other kinds (1, 2/9, 6, 7, 8) ignore `+0x694`. Reset `0x44ab20` sets `+0x694 = 0` (`0x44ad28`); `+0x698` is not cleared (harmless).

## 3. Collision and hitbox

### 3.1 Body height `0x462490` (certain)
First called in `Perso_MoveCollide 0x4624f0` (states 0/1/2/3/4/6): `p+0x118 (P+0x08) = p->duck ? P+0x10 : P+0x0c`. Table `0x4b5f14`:

| P+ | column 0 (Woody, type 1) | column 1/2 (type 3/2) | column 3/4 (race) | meaning |
|---|---|---|---|---|
| 0x0c | **193** | 143 | 160 | standing height |
| 0x10 | **61** | 61 | 81 | ducking height |
| 0x14 / 0x18 | 193 / 61 | 143 / 61 | 143 / 81 | no reader found in `0x44a000..0x466fff` (uncertain what it's for) |
| 0x00 | 43 | 43 | 43 | ground level above the feet – **unchanged** while ducking |
| 0x04 | 69 | 69 | 69 | radius – unchanged (only state 1 halves it, `0x462517`) |

The height is always read via **`0x4624c0` = `p+0x118 · inst+0x54`** (z-scale of the model, normally 1; the crusher scale `+0x2e8`),
or as Perso vtable slot 33 (`0x4624e0` → `0x4624c0`). `P+0x0c` directly: only the effect of Kill 2/9 (`0x44c40e`, `0x44c58c`: lightning on
feet + 193, even while ducking).

### 3.2 What changes with the height (certain, all callers of `0x4624c0` and of vtable slots 24/33 traced)

| consumer | test | effect of ducking |
|---|---|---|
| wall sweep `0x437180` (`0x462663`) | cylinder band `[feet+margin, feet+H]`, margin 41 on the ground / 5 in the air; midpoint feet+H/2 | band 41..61 on the ground: ceilings and overhangs above 61 don't touch him. Ground test with level +43 (`0x436f00`) and volume test at +71 (`0x462760`) unchanged |
| **projectiles** `0x44a0a0` (`0x44a102`: `vtbl[24]` = `0x44cd60`) | swept sphere r (5) against cylinder `{pos + (0,H/2,0), r = 69, half height H/2}` ⇒ y range `[feet − 5, feet + H + 5]` | **only hits below feet + 66** (standing 198) |
| **lasers** 50/51/52 `0x450f80` (`vtbl[24]`, `0x433de0`) | segment against cylinder radius 69·0.85 = 58.65, y range `[feet + 0.1, feet + H − 0.1]` (`0x433f84..0x433fb2`, `0x4a9008` = 0.1) | **only hits below feet + 60.9** (standing 192.9) |
| Boss14 cone `0x410cf0` (`vtbl[33]` on `0x410d95/0x410dce`) | cone 250 tall under the boss; `o = A.y − bot + H` | smaller `o` ⇒ smaller radius; anyone lying low enough falls under it (BOSS14 §6.1) |
| actor pushing `0x4627d0` (`vtbl[33]` of both) | `0x433d40` with both heights | pushing only on vertical overlap with the 61 cylinder (`0x433d40`: both ranges `[y, y + h]` must overlap, PERSO_MOVE §6.6) |
| crushing `0x462a40` (`0x462aa8`) | radius feet+1 → feet+H−1; scale = free/H, < 0.3 ⇒ `Kill(4)` | while ducking, only dies at free height < 18.3 (standing < 57.9) |
| first person `0x44c080` (state 3) | eye = feet + 0.9·H | 54.9 instead of 173.7 |
| bomb throw space test `0x463a26` | sphere at feet + H | n/a: throwing while ducking is blocked |
| **not** height-dependent | enemy Touch `0x40c1e0` (3D distance feet–position < r+r), so charge/bite/dive of all enemy types; bomb/rocket explosion `vtbl[40]` `0x44d040` (3D distance to `inst.pos` < 400); Boss2 | ducking does **not** help |

Conclusion: **yes, Woody can duck under shots and lasers**, as long as they pass higher than 66 resp. 61 above his feet. Shooter enemies
fire horizontally from their muzzle height (PROJECTILES §6, aim height 0), launchers along their marker; whether a given shot flies high
enough depends on the level (not measured per level).

### 3.3 All readers and writers of Perso `+0x694` (full grep of `0x694]`; certain)

| address | function | what |
|---|---|---|
| `0x44ad28` | Reset `0x44ab20` | `= 0` |
| `0x45656d` | race `0x456210` | turn speed 1.2 instead of 1.8 rad/s (`0x4aa39c` / `0x4ab290`) |
| `0x457388` | attack trigger `0x457330` | `≠ 0` ⇒ return |
| `0x462490` | body height | 61 / 193 |
| `0x462520` | `0x4624f0` | state 1: wall radius ×0.5 (`0x4a9014`) |
| `0x463963` | bomb sub 2 `0x46394e` | no throw |
| `0x46463d` | anims state 0/9 `0x464630` | `≠ 0` ⇒ nothing |
| `0x4647e7`, `0x46486e` | death anim `0x464790` | `== 2` ⇒ 0x2c, no 0x29 |
| `0x464b93`, `0x464bed` | hit anim `0x464b70` | `≠ 0` ⇒ 0x24 / 0x23 |
| `0x464c74` | race anims `0x464c20` | `≠ 0` ⇒ `+0x4bc = 0` |
| `0x465b78..0x465dbc` | `0x465b10` | the state machine (writes 1, 2, 3, 0) |
| (`0x41dfc5`, `0x41fbb4`) | camera manager | **different object** (CamMgr+0x694, CAMERA.md) |

`+0x698` is only read and written in `0x465b10`.

## 4. Camera, sound, the stand-up test

### 4.1 Camera (certain for the read locations)
* **Follow camera** (mode 0/1, `0x459090`/CAMERA.md): no read of `+0x694`, `+0x118` or vtable slot 33; the look target stays player + 140. No change.
* **Side view** (mode 0x20, `0x459c70` → `CamMgr+0x61c`): `0x459d51..0x459dbe`: ↑ (action 2) ⇒ index 0 (`h_up`, 500); otherwise ↓ (action 3) **or
  action 5 held** ⇒ index 2 (`h_down`, default 0); otherwise 1 (340). It reads the **key**, not `+0x694` (also while airborne or blocked).
  Height moves linearly at 400 u/s (CAMERA_SCRIPT §4.2).
* **First person** (state 3): eye at 0.9·H (§3.2).

### 4.2 Sound (certain)
`0x465b10` does not trigger any sound. Everything comes from type-4 events on the root node (SOUND.md §3), W1A model 0:
.ins 24 and 58: `t = 0`, ref **32** (bank 0 = `Common/<character>.rck`), chance [0,100), volume 50, pitch 100 %, dmin 200;
.ins 23 and 59: same but ref **33**; .ins 26 and 64 (hit while ducking): refs 55 [0,30) / 56 [30,60) / 57 [60,100); .ins 36 (died while ducking): ref 34.
The Perso plays them 2D. In the port, `src/main_engine.c` (line 2060, "animation events of type 4") already plays them automatically.

### 4.3 The stand-up test `0x4359b0(&a, &b, −1)` (certain)
* `a = pos + (0, P+0x10, 0)`, `b = pos + (0, P+0x0c − P+0x10, 0)`; `pos` = `+0x1f4` (feet, before this frame's movement).
  Woody: **feet+61 → feet+132** (length 71); column 1/2: +61 → +82; race: +81 → +79 (2 units, RACE §4.8). PERSO_MOVE §3.4 said
  "y+61 … y+193": wrong (FPU stack `0x465cd8..0x465d09` line by line: `fld P10; fld y; fadd st1` ⇒ a.y; `fld P0c; fsub st1; fadd y` ⇒ b.y).
* `0x4359b0` clears `[0x53a554]`, `[0x53a560]`, `[0x53a55c]` and calls `0x497ed0(a, b, −1)`; raw result `[0x4c4bd0]`: 3 (world polygon) ⇒
  `[0x53a554] = 1`; 4 (instance polygon, node `[0x4c4be0]`: the press nodes, BOMB.md §5.2 correction) ⇒ 2; 2 ⇒ 3; otherwise 0. `[0x53a558]` = t.
  **Any** value ≠ 0 blocks standing up. The −1 is the start cell (`0x497fb0` looks it up with `0x408180`, `0x49802b`), not an instance to skip (OBJECTS.md §2.1).
* Uncertain: whether `0x497ed0` hits polygons from both sides (the ray goes upward and so would hit the underside of a ceiling); the port
  functions `gel_ray_frac` and `player_ray_instances` are two-sided, which gives the expected behavior for this test.
* A ceiling between 132 and 193 does not block: he then ends up standing "inside" the ceiling (the wall sweep only pushes in xz). Derived.

## 5. Port recipe (`src/`; none of this is built yet)

### 5.1 `src/player.h`
* `PlayerInput`: `int forward, back, left, right, jump, action, duck;` (duck = action 5, current key state).
* `Player`: `int duck, duck_anim; float duck_t;` (= `+0x694`, the requested logical anim, `+0x698`).
* `float player_body_height(const Player *p);   /* 0x462490 → P+0x08: 193/61 (Woody), race 160/81 */`
* Update the header comment "Not ported yet: … ducking".

### 5.2 `src/player.c`
1. Constants: `#define P_DUCK_H 61.0f  /* P+0x10 */`.
2. Height:
   ```c
   float player_body_height(const Player *p)                   /* 0x462490 */
   {
       if (p->race_char) return p->race_crouch ? 81.0f : 160.0f;
       return p->duck ? P_DUCK_H : P_BODY_H;
   }
   ```
   and in `player_update` (l. 1413): `const float body_h = player_body_height(p), radius = …;` (radius unchanged).
3. The state machine (next to `race_crouch`):
   ```c
   /* ducking 0x465b10 (docs/PERSO_DUCK.md): duck = +0x694, duck_t = +0x698 */
   static void duck_update(Player *p, const PlayerInput *in, float dt)
   {
       if (p->dead_kind || p->atk) return;                         /* state 2 / +0x5b4: nothing, no LockMove either */
       int b = p->bomb != NULL;                                    /* state 6: 0x4e/0x4f/0x50 */
       switch (p->duck) {
       case 0: if (in->duck && p->on_ground) { p->duck = 1; p->duck_anim = b ? 0x4e : 0x31; p->duck_t = anim_len(p, p->duck_anim, 0); } break;
       case 1: if ((p->duck_t -= dt) <= 0) p->duck = 2; break;
       case 2: p->duck_anim = b ? 0x4f : 0x32;
               if (!in->duck) {
                   Vec3 a = { p->pos.x, p->pos.y + P_DUCK_H, p->pos.z }, e = { p->pos.x, p->pos.y + (P_BODY_H - P_DUCK_H), p->pos.z }, n; float f;
                   int blocked = gel_ray_frac(p->gel, a, e) <= 1.0f || (player_ray_instances(p, NULL, a, e, &f, &n, NULL) && f <= 1.0f);   /* n_out must not be NULL */
                   if (!blocked) { p->duck_anim = b ? 0x50 : 0x33; p->duck_t = anim_len(p, p->duck_anim, 0); p->duck = 3; }
               }
               break;
       case 3: if ((p->duck_t -= dt) <= 0) p->duck = 0; break;
       }
       if (p->duck && p->move_lock < dt) p->move_lock = dt;        /* 0x44cce0(dt, 0) = max; speed already zeroed via !allow */
   }
   ```
   `anim_len(0x31)` = 0.375 and `anim_len(0x33)` = 0.19995 come out of the model automatically (§1.4).
4. Call in `player_update` (l. 1350), in the branch `if (!p->dead_kind && !racing) { … }` **after** `attack_trigger()` and the
   `climb_try` return: `duck_update(p, in, dt);`. Racing keeps `race_crouch` (action 8). The early returns (scripted action, rocket,
   climbing wall) skip ducking: deliberate port deviation (the original lets the state machine keep running there, §1.3); set
   `p->duck = 0` at their start if that looks cleaner. `move_lock` is already counted down by dt at the start of `player_update`:
   the order matches §1.1.
5. Horizontal movement zero on a block (§2.2): in the `disp` block, only apply the knockback and sliding contributions if `p->move_lock <= 0`
   (original: `h = 0` on **any** `+0x238 > 0`, so also during hard landing/bomb pick-up/throw). Conservative alternative: only `!p->duck`.
   Let the push timers keep ticking (leftover after getting up, §2.3). Don't touch vertical (`jumper.dy`).
6. Animation choice (l. 1476): after `else if (p->hit_anim_t > 0) want = p->hit_anim;` insert
   `else if (p->duck) want = p->duck_anim;` — before the bomb branch (0x4e..0x50 win over the carry anims) and before the ground/air
   branches (so also no hard-landing anim/`lock_move`/bounce while lying down, §2.5). The chain 24→25, 23→0, 58→60, 59→47 plays itself
   out via `anim_request`.
   Idle (l. 1511): `if (!p->idle_hold && !p->duck && (…)) idle_reset(p);` (`0x464630` doesn't touch the counter while lying down).
7. `player_hit` (l. 885/886): `p->hit_anim = p->on_ground ? (p->duck ? 0x23 : 0x1f) : 0x20;` and with bomb `p->on_ground ? (p->duck ? 0x24 : 0x21) : 0x22`.
   Don't change `p->duck` (he stays down; the yaw turn toward the attacker stays).
8. Death anim (l. 1473/1474, kind 3/4/5):
   `want = p->on_ground ? (p->duck == 2 ? 0x2c : 0x26) : 0x25;` on `dead_T <= dt`, and the 0x29 step only `if (!(p->dead_ground && p->duck == 2))`.
   `player_kill` leaves `duck` as is; `player_reset` sets `p->duck = 0; p->duck_t = 0;`.
9. `attack_trigger`: after the `air_win` countdown (l. 816) `if (p->duck) return;` (before the `atk == 5` chain; the pick-up above stays
   allowed, §2.6). `carry_frame` case 2: `if (p->carry_pressed && !p->duck)` (`0x463963`).

### 5.3 `src/main_engine.c`
1. Input (l. 2518): `pin.duck = (!fly && win.keys['X']) || (duck_at >= 0 && now - t0 >= duck_at && now - t0 < duck_at + duck_len);`
   with a test option `--duck T LEN` next to `--peck` (l. 2365/2379). **X** is free (in use: W A S D C P, and E/Q/Space only in fly mode);
   Space (= original ducking) stays jump and Ctrl/Shift attack, as now. The existing `memset(&pin, 0, …)` (results, title, cinematic)
   clears `duck` automatically.
2. Side view (l. 2569): `g_cam.mode == 0x20 ? (pin.forward ? 2 : (pin.back || pin.duck) ? 3 : 0) : win.keys['C']` (↑ takes priority, `0x459db3`).
3. `laser_hits_player` (l. 968): `H = player_body_height(p)` instead of 193.
4. Shots (l. 1420): `y <= player_body_height(pl) + 5.0f` instead of 198.
5. Bomb/rocket explosion, enemy bites (`src/enemy.c`): **do not** adjust (3D distance, no height).

### 5.4 `src/boss.c`
`cone_touch` (l. 220): replace `B_PL_H` with `player_body_height(pl)` (`0x410cf0` reads `vtbl[33]`).

### 5.5 Testing
* W1A start, `--duck 2 1.5`: 0.375 s going down, stays down, 0.2 s getting up; while lying down, no movement/turning with arrow keys,
  Space does nothing.
* With WOODY_ANIMLOG, check the chain `lanim 0x31 → 0x32 → 0x33 → 0`; while ducking in front of a W1A launcher or laser, verify that
  a shot/beam higher than 66 resp. 61 above the feet misses him (which ones those are: §6.6).
* Releasing under a low ceiling: stays down until pushed out from under it (only possible via a platform/actor, since walking isn't possible).

## 6. Open questions

1. Whether RampA accelerates from 0 or from its old speed after standing up (`0x467130` not read); the port starts at 0.
2. `0x497ed0`: one-sided or two-sided against polygons (§4.3); answer 2 (hit kind 3) not investigated.
3. ~~`0x433d40` (actor pushing): exact height condition~~ – decoded in PERSO_MOVE §6.6.
4. The edge cases of §1.3/§2.6 (ducking in state 3/4/5/7/8/9, picking up or grabbing while lying down) are only derived from the code.
5. `P+0x14/P+0x18` (193/61): no reader found.
6. In which levels shots or lasers actually fly between 66 and 198 above the ground (i.e. are dodgeable by ducking): not measured.
