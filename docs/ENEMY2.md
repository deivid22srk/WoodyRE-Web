# Enemy classes part 2 (types 10, 11, 12, 13) – Woody.exe

Status: static analysis of `game/Woody.exe` (`out/disasm_full.txt`, `tools/drange.py`; floats and tables read from the exe with a PE reader).
Continuation of ENEMY.md: the base class `Enemy`, the parameter block **P**, the behaviours (Wander / Chase / Stand / Path), the
angle controller **H**, `Enemy::Update 0x41a3e0`, `Enemy_TakeDamage 0x41adc0` and messages 6/11 apply here unchanged and are not
repeated. "uncertain" = not read instruction by instruction, or no reader found. Types 12 and 13 are fully read; types 10 and 11
**do not occur in any level** (§1) and have only been followed at a high level.

## 0. Summary

| type | ctor(subtype) | vtable | size | what it is | used in |
|---|---|---|---|---|---|
| 10 | `0x415190(10)` (`0x40379f`) | `0x4a9cb8` | 0x1ec | **flying gunner**: hovers 180 above the player, keeps 800 distance, homing orb, dives within 800 | nowhere |
| 11 | `0x4120e0(7)` (`0x403811`) | `0x4a9ab0` | 0x200 | **ground bomber**: a type-7 variant (same Reset `0x416ed0`), throws bombs instead of projectiles, charges within 300 | nowhere |
| 12 | `0x410e90(8)` (`0x40384a`) | `0x4a99b0` | 0x204 | **stationary bomb thrower / W2B end boss**: only the Stand behaviour, turns in place, throws bombs, melees within 300; **immune to the peck**, only bomb explosions do damage (5 hp); death ⇒ msgmask 0x10 ⇒ script sends 1083 (EndLevel) | W2B ×1 |
| 13 | `0x413780(9)` (`0x4037d8`) | `0x4a9bb0` | 0x1f8 | **ghost**: half transparent (fade target 0.5), flies (no gravity, follows the player's height), fires a fireball alternately from two hands, dives within 300 | K3A, S3A, W3A, W3B, W3D ×19 |

Note the order in the factory `0x403502` (byte table `0x403f3c`, jump table `0x403e94`): case 9 → `0x40377e` = type 10, case 10 → `0x4037f0` = type 11,
case 11 → `0x403829` = type 12, case 12 → `0x4037b7` = **type 13** (type 13's code is thus placed before that of 11/12). All four: category 2
(`0x40c360(2)`), `+0x1c4 = 0`, PostLoad increments `[0x4c5330]++`.

Bosses (not analyzed): **type 14** = `0x40eb50(11)` (size 0x24c), **type 15** = `0x40d850(12)` (0x258), **type 16** = `0x40c730(13)` (0x2a8).

Vtable differences from the Enemy base (`0x4a9fbc`; slots as in ENEMY.md §1.1):

| slot | type 10 | type 11 | type 12 | type 13 | meaning |
|---|---|---|---|---|---|
| 1 PostLoad | `0x415280` | `0x4121d0` | `0x410f80` | `0x413870` | |
| 17 Reset | `0x4153c0` | **`0x416ed0`** (= type 7) | `0x411020` | `0x4139b0` | |
| 31 Touch | = | = | **`0x411840`** | = | type 12: enlarged range (§4.4) |
| 39 TakeDamage | `0x416070` | `0x412d90` | `0x411ab0` | `0x414490` | |
| 40 Blast | = | = | **`0x4119b0`** | = | type 12: 1 damage instead of death |
| 41 (bool) | `0x4160d0` | `0x417fd0` (= type 7) | `0x411b10` | `0x414510` | "my projectile killed the player" (called by `0x44a1eb`, PROJECTILES §2.5) |
| 43 ground | **`0x416a10`** | = | = | **`0x414f10`** | flyers: own height control |
| 45 animation | `0x4160f0` | `0x412df0` | `0x411b30` | `0x414540` | |
| 47 RegisterActor2 | `0x416030` | `0x412d50` | `0x411970` | `0x414470` | |
| 51 death duration | `0x4164e0` = AnimLen(10)+1 | `0x4149e0` = AnimLen(13)+1 | `0x411d30` = AnimLen(8)+1 | `0x4149e0` = AnimLen(13)+1 | |
| 52 Update | `0x415490` | `0x412310` | `0x4110c0` | `0x413ab0` | |
| 53 duration | `0x416500` | `0x413290` | `0x411d50` | `0x414a00` | |
| 58 Fire | `0x416800` | `0x413580` | `0x411e80` | `0x414d10` | |
| AnimCtrl vtable / record getter / table | `0x4a9da4` / `0x4169f0` / `0x4b2478` | `0x4a9b9c` / `0x413760` / `0x4b1ed0` | `0x4a9a9c` / `0x4120c0` / `0x4b1d10` | `0x4a9ca4` / `0x414ef0` / `0x4b2190` | records of 0x1c B `{int sub[4]; int prio; float speed; u8 restart}` |

## 1. Usage per level (message 1200 = SetTypeInstance)

Counted in `out/ekoasm/*.ekoasm` on the pattern `PUSH 1200; PUSH 0x1000000|inst; PUSH type; SEND 3` (all 1200 calls with a
constant; the 26 deviating forms are `SEND 4/5` of other messages). Positions from the `.ins` (`tools/insparse.py`).

| type | K1A | K2A | K3A | S1A | S2A | S3A | W1A | W1B | W2A | W2B | W2D | W3A | W3B | W3C | W3D | WWS | total |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 4 | 8 | | | 7 | | | 5 | 3 | | | | | | | | | 23 |
| 5 | 2 | 10 | | | 8 | | 1 | | 7 | 13 | 8 | | | | | | 49 |
| 6 | | | 12 | | | 10 | | | | | | 14 | 7 | 18 | 21 | | 82 |
| 7 | 7 | | | 8 | | | 1 | 13 | | | | | | | | | 29 |
| 8 | | 16 | | | 13 | | | | 18 | 10 | 17 | | | | | | 74 |
| 9 | | | 5 | | | 12 | | | | | | 2 | 6 | | 8 | | 33 |
| **10** | | | | | | | | | | | | | | | | | **0** |
| **11** | | | | | | | | | | | | | | | | | **0** |
| **12** | | | | | | | | | | 1 | | | | | | | **1** |
| **13** | | | 3 | | | 4 | | | | | | 2 | 6 | | 4 | | **19** |
| 14 (boss `0x40eb50`) | | | | | | | | 1 | | | 1 | | | | 1 | 1 | 4 |
| 15 (boss `0x40d850`) | | | | | | | | | | | 1 | | | | 1 | | 2 |
| 16 (boss `0x40c730`) | | | | | | | | | | | | | | | 1 | | 1 |

The remaining levels (Blackbox, Credits, House, K1R..S3R, KWS, SWS, Lang) have no types 4..16 (WWS has only type 14). Port priority:
type 8 (74) and 7/9 (ENEMY.md §8) > **type 13 (19)** > **type 12 (1, but it is the closer for W2B)** > 10/11 (dead code).

Test spots (level, instance index, position):

| type | level | inst | position | note |
|---|---|---|---|---|
| 4 | W1A | 282 | (652, −1985, −433) | |
| 5 | W1A | 494 | (9395, 1006, −3515) | |
| 6 | W3A | 267 | (−2533, −389, 2626) | |
| 7 | W1A | 312 | (3701, 4, 2671) | |
| 8 | W2A | 357 | (9575, 157, −16400) | |
| 9 | W3A | 549 | (−10241, −254, 11011) | |
| 12 | W2B | 533 | (10953, −3082, 11031) | model 52, 17 animations; starts deactivated (see below) |
| 13 | W3B | 509 | (933, −560, 974) | model 39, 20 animations; sight 1500, default reload time ⇒ fires |
| 13 | W3A | 446 | (−2924, −308, 4312) | sight 600, walk speed 150 |
| 13 | W3A | 485 | (9911, 1563, 2860) | with `11/33 = 200` ⇒ practically never fires (see below) |
| 14 | W1B | 405 | (−7031, 2430, −8863) | boss, model with 43 animations |
| 15 | W2D | 762 | (−3928, 2206, 9596) | |
| 16 | W3D | 801 | (5591, −2415, −19918) | |

All type 12/13 instances have **no TRAJ** (the "patrol" state thus does not occur).

Script messages to the type-13 instances (all message 11, ENEMY.md §7; the port must apply them per instance to P):

| level: inst | messages 11 `(a, b)` |
|---|---|
| K3A 375 / S3A 381 / W3A 446 | (5,1) leash on, (0,400) leash 400, (1,600) sight 600, (7,150) walk speed 150 |
| K3A 387 / S3A 399 / W3A 485 | **(33,200)**, (8,350) run speed 350, (5,1), (0,400), (1,500), (7,200) |
| K3A 388 / S3A 400 | (33,200), (8,350), (5,1), (0,400), (1,400), (7,200) |
| S3A 435 | (5,1), (0,400), (1,400), (7,100) |
| W3B 504 | (5,1), (0,50), (1,300), (7,150) |
| W3B 509 / 510 | (5,1), (0,800), (1,1500) / (5,1), (0,800), (1,500), (7,250) |
| W3B 759, 760, 761 | message 6 (on/off) and (4,0) = Reset |
| W3D 869..872 | (0,100), (1,400); 869 also (5,1), (7,150) |

`11/33` writes the raw int to `P+0x4c` (`0x41ab74`, jump table `0x41ac40[33]`) = **reload time in seconds**: 200 s. Those ghosts thus
effectively never fire (the timer starts at `P+0x4c` when the player is noticed). The designer presumably thought in 1/100 s; the code
does not rescale.

Type 12 in W2B (object 533): `1200 12`, after 1 s `6 [inst, 0]` (deactivate); object 529 later sends `6 [533, 1]` and sets `var 2 = 1`;
533's script tests `MSGTEST 16` on itself every tick ⇒ `1083` (EndLevel). The same level uses message 1090 (start bomb) elsewhere.

## 2. Parameter block P for subtypes 7..10 (`0x41d510`, jump table `0x41dc38`; cases `0x41d95c`, `0x41da36`, `0x41d9c0`, `0x41d8f3`)

Only the fields the case overrides; the rest = the "default" column of ENEMY.md §2.3 (e.g. `P+0x10 = π/2`, `P+0x14 = 2π`, `P+0x44 = 600`, `P+0xc0 = 3000`).

| off | default | sub 7 (type 11) | sub 8 (type 12) | sub 9 (type 13) | sub 10 (type 10) | meaning (verified in these classes) |
|---|---|---|---|---|---|---|
| 0x04 | 50 | 50 | **60** | **30** | **80** | radius |
| 0x08 | 200 | 200 | | **100** | **400** | walk speed (Wander); type 13: also vertical speed with no target |
| 0x0c | 600 | 600 | | **300** | **700** | run speed (Chase); type 13: vertical speed with a target |
| 0x1c | 800 | | | **1000** | 1000 | leash distance (3D for subtype ≥ 9) |
| 0x20 | 800 | 2500 | **2400** | **1500** | 2500 | sight distance |
| 0x24 | 600 | 800 | **1000** | **10000** | 10000 | max. \|dy\| seen |
| 0x28 | 140 | 180 | **280** | **140** | 175 | height |
| 0x2c / 0x30 | 10 / 10 | | | **10000 / 10000** | 10000 / 10000 | max. step-down / step-up (flyers: no edge test) |
| 0x34 | 1 | 2 | **5** | **3** | 2 | hit points |
| 0x38 | 3.0 | 0.1 | | **0.5** | | cooldown after a bite |
| 0x3c | 1 | 2 | **3** | **2** | 1 | damage of the bite / melee |
| 0x40 | 1 | 10 | 4 | **2** | 1 | damage of the projectile (type 11/12: unused, the bomb has its own damage) |
| 0x48 | 500 | 300 | 300 | 1 | 1 | no reader found in these classes |
| 0x4c | 3.0 | 0.7 | **3.5** | **1.0** | 1.2 | **reload time** (s) |
| 0x50 | 800 | | | | 800 | type 10: distance it keeps |
| 0x54 | 400 | 300 | | **300** | | xz distance within which the charge/dive may start |
| 0x5c | 1000 | 1000 | | **500** | | charge/dive speed |
| 0x60 | 1000 | 1500 | **800** | **1000** | 1500 | projectile speed (type 12: throw speed of the bomb) |
| 0x74 | – | | | **3** (int) | 2 (int) | visual kind of the projectile (3 = fireball, 2 = energy orb; PROJECTILES §5) |
| 0x8c | 300 | | **300** | | | type 12: melee distance (3D) |
| 0x90 | 0.8 | | **1.3** | | | type 12: short fuse (s) |
| 0x94 | 7 | | **7.5** | | | type 12: long fuse (s) |
| 0x98 | – | | **4** (int) | | | type 12: number of long fuses between two short ones |

## 3. Type 13 – the ghost (fully read)

### 3.1 Fields (size 0x1f8) and Reset

| off | type | meaning |
|---|---|---|
| 0x1c0 | int | state 0..13 |
| 0x1c4 | AnimCtrl* | `0x414ec0` (vtable `0x4a9ca4`) |
| 0x1c8 | float | cooldown timer `cool` (≥ 0 ⇒ `-= dt`; the player is only noticed when `< 0`) |
| 0x1cc | float | "player defeated" timer = AnimLen(11) |
| 0x1d0 | float | **reload timer** (≥ 0 ⇒ `-= dt`) |
| 0x1d4 | float | remaining dive time |
| 0x1d8 | float | brake timer = AnimLen(9) |
| 0x1dc | float | timer after the bite = AnimLen(10) |
| 0x1e0 | float | `AnimLen(4, 1)` (path-animation blending `0x4148a5`) |
| 0x1e4, 0x1e8, 0x1ec | | 0 on Reset, no reader found |
| 0x1f0, 0x1f1 | u8 | 1 / 0 on Reset, no reader found |
| 0x1f4 | int | **hand** 0/1 for the next shot; Reset: `rand() & 1` |

PostLoad `0x413870` = as type 4: AnimCtrl, Path (only with TRAJ), `Wander(this, 1, &home)`, `Chase(this, 0)`, `Stand(this)`.
Reset `0x4139b0`: `Enemy::Reset`; with TRAJ ⇒ state 0 (Path, `P+0x1c = 10`), otherwise state **5** (Wander, `0x41c160`); timers 0;
**`deathT (+0x15c) = vtbl[51]() · 0.5`** – the death counter thus starts at half, so the fade-out of `Enemy::Update` (ENEMY.md §4.2)
begins immediately at death and the ghost disappears after only **½·(AnimLen(13) + 1)** s.

### 3.2 Update `0x413ab0` (jump table `0x414430`)

```c
void Ghost_Update(Ghost *e) {
    Enemy_Update(e);                                  /* 0x41a3e0: behaviour tick, fade on death, vtbl[43] = height (§3.3), rotation, animation */
    FadeInst_SetTarget(e, 0.5f, 0);                   /* 0x44e7f0: +0xfc = 0.5 -> half transparent (fade speed 100/s = instant) */
    if (e->cool   >= 0) e->cool   -= dt;              /* 0x413ae2 */
    if (e->reload >= 0) e->reload -= dt;              /* 0x413b07 */
    if (e->hitT   >  0) e->hitT   -= dt;
    Actor *t;
    switch (e->state) {
    case 0:  /* PATROL 0x413c41 (only with TRAJ) */
        if (e->cool < 0 && FindTarget(1,0)) { e->state = 1; e->home = e->pos; Wander_SetLeash(e->wander, 1, &e->home); }   /* 0x41c140 */
        break;
    case 1:  /* NOTICED 0x413cb8 */
        if ((t = FindTarget(1,0))) { behav = Chase; Init(); Chase_Start(t);   /* 0x41bc80: speed P+0xc (300) instant, turn speed P+0x14 (2π) */
                                     e->state = 2; e->reload = P->reload /*+0x4c = 1.0*/; }
        else e->state = 4;
        /* fallthrough to 2 */
    case 2:  /* CHASE + SHOOT 0x413d0d */
        if (!(t = FindTarget(1,0))) { e->state = 4; break; }
        vec3 d3 = normalize(t->pos - e->pos);                                    /* 3D, 0x440040 */
        if (dist_xz(t, e) <= P->dashDist /*+0x54 = 300*/ && dot(H_moveDir(), d3) > 0.95f /*0x4a9c9c*/) {
            e->atkT = AnimLen(8) + 0.5f * AnimLen(10);                           /* +0x1d4 */
            H_SetSpeed(P->dash /*+0x5c = 500*/, instant);  e->state = 7;  break;
        }
        if (e->reload > 0) break;
        int n = (e->hand == 1) ? 0 : 1;                                           /* hand 1 -> marker 0, hand 0 -> marker 1 */
        Seg m;  if (!GetVector(e, /*typecode*/1, &m, n)) break;                   /* 0x42f6b0: two markers with typecode 1 (flags 0x120) = the hands */
        m.end = t->pos;                                                          /* vtbl[34] of the player = foot position */
        Ray(e->pos + (0, h/2, 0), m.start, -1);                                  /* 0x497ed0: does the hand poke through a wall? */
        if ([0x4c4bd0] != 3 && [0x4c4bd0] != 4)
            e->vtbl[58](t, &m, (e->hand == 1) ? 130.0f : 30.0f);                  /* Fire 0x414d10, §3.5 */
        e->reload += P->reload;  e->hand ^= 1;                                   /* even if the shot was blocked */
        break;
    case 3:  /* AFTER THE BITE 0x41422f */
        if (e->t_bite > 0) e->t_bite -= dt; else { e->cool = P->cool /*0.5*/; e->state = 4; }
        break;
    case 4:  /* TO WANDER 0x413b6b */  behav = Wander; Init(); Wander_Start(); /*0x41c160*/  e->state = 5;  break;
    case 5:  /* WANDER 0x413ba4 */
        if (e->cool < 0 && FindTarget(1,0)) e->state = 1;
        if (e->traj && dist_xz(e->pos, e->home) < 10.0f) { behav = Path; Path_Resume(); /*0x41cfb0*/ e->state = 0; }
        break;
    case 6:  /* HIT 0x4143be */
        if (e->hp <= 0) e->state = 11;                                           /* no vtbl[57] (no hit stars) */
        else if (e->hitT <= 0) { e->reload = 2 * P->reload; e->state = 4; }
        break;
    case 7:  /* DIVE 0x414085 (behaviour stays Chase, speed 500) */
        if (!(t = FindTarget(1,0)) || e->atkT <= 0) { e->state = 13; break; }
        e->atkT -= dt;
        if (!Touch(1,0)) break;                                                  /* 3D distance < 30 + player radius */
        dir = normalize_xz(t->pos - e->pos);  pt = e->pos + dir * P->radius + (0, h/2, 0);
        if (t->vtbl[39](e, P->damage /*+0x3c = 2*/, &dir, &pt, 0)) e->state = 9;
        else { e->t_bite = AnimLen(10); behav = Stand; H_SetSpeed(P->walk, instant); e->state = 3; }
        break;
    case 13: /* BRAKE start 0x41427b */
        behav = Stand; H_SetSpeed(P->dash, instant); e->t_brake = AnimLen(9); e->state = 8;   /* fallthrough */
    case 8:  /* BRAKE 0x4142bc */
        if (e->t_brake < 0) e->state = 4; else e->t_brake -= dt;                 /* no cooldown (unlike type 4) */
        break;
    case 9:  /* PLAYER DEFEATED start 0x4142f8 */
        e->t_win = AnimLen(11); e->state = 10; behav = Stand; H_SetSpeed(P->walk, instant);   /* fallthrough */
    case 10: if ((e->t_win -= dt) <= 0) e->state = 4;  break;                    /* 0x414339 */
    case 11: /* DEAD 0x414361 */
        e->deathT += dt; behav = Stand;
        if (e->vtbl[51]() <= e->deathT) e->flags10c |= 1;                        /* remove; counter [0x4c532c]++ via vtbl[29] */
        *e->vtbl[4]() &= ~0x400;                                                 /* no longer attackable */
        break;
    case 12: break;                                                              /* 0x41441c: empty, never set anywhere */
    }
}
```
State 1 and 2 run through in the same frame (`0x413d01 → 0x413d0d`). There is no "can I see the player" test in state 2 besides FindTarget itself
(sight 1500, |dy| < 10000 ⇒ height plays no role). The ghost **shoots while chasing** (every `P+0x4c` s, alternating left/right) and switches to
the dive within 300 (xz) and ±18°. Dive range = 500 · (AnimLen(8) + ½AnimLen(10)); with the W3B model (anim 13 = 1.1 s / 3, anim 14 = 0.8 s / 2)
that is 0.57 s ≈ 283 units.

### 3.3 Height control `vtbl[43]` = `0x414f10` (replaces gravity/ground following)

```c
void Ghost_Height(Ghost *e) {
    if (e->state == 11 || e->state == 6) return;               /* dead / hit: height freezes, msgmask stays */
    h2 = P->height * 0.5f;
    hit = Probe_Test(&e->probe, -1, &(pos + (0,h2,0)), h2, e->id);   /* 0x436dc0: press events + ground height [0x53a568] */
    flags.onGround = hit != 0;   groundY = [0x53a568];
    Actor *t = FindTarget(1,0);
    if (t) { dy = t->pos.y - pos.y;  step = dt * P->run /*+0xc = 300*/;
             if (dy > 0 && t->vtbl[35]()) step *= 0.2f; }      /* 0x44c940: player is rising in a jump (jumper 0/1/7) -> 5x slower upward */
    else   { dy = e->home.y /*+0x138*/ - pos.y;  step = dt * P->walk /*+8 = 100*/; }
    if (fabs(dy) > 0.01f) {                                    /* 0x4a94f8 */
        if (dy < 0) { dy = max(dy, -step);  pos.y += dy;
                      if (pos.y < groundY) { pos.y = groundY; flags.onGround = 1; } }
        else        { dy = min(dy, step);
                      Ray((x, y + h, z), (x, y + h + dy, z), -1);                /* 0x4359b0: ceiling above the head? */
                      if ([0x53a554] == 0) pos.y += dy; }
    }
    msgmask(e->id, 0x200) = flags.onGround ? set : clear;      /* 0x443e50 / 0x443e90 */
}
```
The ghost thus hovers with its **feet at the player's foot height** (or, with no target, at the y of the home point) and never
deliberately touches the ground. Horizontally: the ordinary behaviours; for subtype ≥ 9 the shared movement routine `0x41b2c0` keeps y fixed
(`res.y = from.y`), `P+0x2c/0x30 = 10000` disables the edge/step test, the sweep against walls remains. The walk speed from message 11/7 is
also the vertical return speed.

### 3.4 Damage

* **Ghost hits player**: only in state 7 (dive) via `Touch(1,0)`: `Perso->vtbl[39](this, 2.0, dir_xz, point, 0)`; and via the fireball (2.0).
  If the projectile kills the player, it calls `vtbl[41](true)` = `0x414510`: state ≠ 11 ⇒ state 9.
* **Player hits ghost** `vtbl[39]` = `0x414490` (logs `'TimeHit : %f'`): if `hitT ≤ 0`: state 6, AnimCtrl reset, `hitT = vtbl[53](0)` = **0.25 s**
  (`0x4a9ca0`), `return Enemy_TakeDamage(att, dmg, dir, pt, 0)` (knockback `600·t` for 0.25 s if `dir ≠ 0`, `hp −= dmg`). Otherwise `false`.
  3 hp ⇒ three hits; vulnerable in every state except during the 0.25 s of "hit". After "hit": reload timer = 2·`P+0x4c`.
* `vtbl[47]` `0x414470`: registers as an actor in states 0..10 and 13 (not 11/12). `vtbl[40]` Blast = base class (bomb within 400 ⇒ death).

### 3.5 Fire `vtbl[58](target, Seg *m, float aim_height)` = `0x414d10`

`T` = default ctor inline + **template 1** (`0x449070(1, &T)`), then: `T.pos = m.start`, `T.dir0 = normalize(m.end − m.start)` (3D, straight
at the player's feet), `T.damage (+0x30) = P+0x40 = 2`, `T.target (+0x38) = target`, `T.aim_h (+0x3c) = 130 / 30`, **`T.steer (+0x40) = 0`,
`T.vsteer (+0x44) = 0`**, `T.lim_y (+0x54) = 0`, `T.speed (+0x20) = P+0x60 = 1000`, `T.owner (+0x58) = this`, `T.visual (+0x60) = P+0x74 = 3`
⇒ `0x4490a0(&T)`. With steering factors 0 the **fireball flies dead straight** (target and aim height then have no effect), lifetime 15 s
(template 1), radius 5, SoundFx 20 (3D on the ghost) when created. Return value = slot found.

### 3.6 Animations (`vtbl[45]` = `0x414540`, table `0x41468c`; records `0x4b2190`; model: 20 animations)

| state | logical anim | sub[] (.ins animation) | speed | |
|---|---|---|---|---|
| 0 | `0x4147d0` to path substate: 0 ⇒ 0x15/0x16/0x17 blended (`0x436bd0`), 1/7 ⇒ 0x18/0x19, 2..6 ⇒ 0xe..0x12 | | | only with TRAJ |
| 1, 2 | Chase running (`+0x30 == 1`) ⇒ **3**; otherwise turning: `H+0xc > 0` ⇒ 1, otherwise 2 | 3: 1,2,2,2; 1/2: 0 | 3 | turning = idle animation 0 |
| 3 | **10** | 14 | 2 (prio 1500) | bite |
| 4, 5 | `0x4146d0` to wander action: 0..4 ⇒ **14..18**; 5 ⇒ 0x18/0x19; 6 ⇒ **4**; 7 ⇒ **5**; 9 ⇒ 6; 10 ⇒ 7 | 14..18: 6,7,8,9,10 (speed 2); 4/5: 3,4,5,0; 6: 4,4,5,0; 7: 4 | 3 | hovering/idle |
| 6 | **12** | 11 | 4 | hit |
| 7 | **8** | 13 | 3 | dive |
| 8, 13 | **9** | 15 | 3 | braking |
| 9, 10 | **11** | 16 | 3 | player defeated |
| 11 | **13** | 12 | 3 (prio 2000) | dead; `vtbl[51]` = AnimLen(13) + 1.0 |

Then always `AnimCtrl->Tick(dt)`. Records 19/20 (sub 20/21) exist in the table but the model only has 20 animations and the class does not use them.
The **patrol animations** of state 0 (records 21..23 = walk start/loop/stop `3,4,4,4` / `4×4` / `5`, 24/25 = sub 0 for turning, 14..18 for the pauses)
never play in the shipped game: state 0 needs a TRAJ and **no ghost (and no other enemy) of the 28 levels has one** (`.ins` check, ENEMY.md §5.4). The
wander animations (states 4/5) are ported with the Wander behaviour (ENEMY.md §5.4: idles 14..18 = .ins 6..10 at speed 2, turn 24/25 = .ins 0, walk chains 4..7).
Duration function `vtbl[53]` `0x414a00` (table `0x414c98`): state 6 ⇒ 0.25; 3 ⇒ AnimLen(10); 7 ⇒ AnimLen(8); 8/13 ⇒ AnimLen(9); 9/10 ⇒ AnimLen(11); 11 ⇒ AnimLen(13);
4/5 ⇒ per wander action (0..4 ⇒ AnimLen(14..18), 5 ⇒ turn animation, 6 ⇒ ΣAnimLen(4,0..2), 7 ⇒ ΣAnimLen(5,0..2), 9 ⇒ ΣAnimLen(6,0..2) − `inst+0xac/inst+0xa0`).
W3B model 39, duration in s of animation 0..19: 5.7, 0.7, 1.5, 1.1, 6.5, 1.2, 17.9, 16.4, 11.4, 11.4, 5.6, 1.8, 5.7, 1.1, 0.8, 2.9, 8.9, 36, 36, 36 (AnimLen = duration / speed).

### 3.7 Sound, messages, events
No `SoundFx` call in the class (only SoundFx 20 of the projectile). Messages: only `Enemy::HandleMsg` (6 and 11). Events: msgmask **0x200**
(§3.3); Reset clears 0x10 (base class). No SetVar, no bonus; `vtbl[29]` increments `[0x4c532c]++`.

## 4. Type 12 – stationary bomb thrower (fully read)

Ported in `src/enemy.c` (`bomber_update`, `bomber_peck`, `bomber_blast`, `enemies_bomb_contact`); the bomb ownership and the throwing of a
returned bomb live in `bombs_fly` (`src/main_engine.c`). Tested with the W2B start (`WOODY_SETVAR="1 1 1"`): throwing, picking up,
throwing back, 1 hp per explosion, death ⇒ msgmask 0x10 ⇒ `1083`.

### 4.1 Fields (size 0x204) and Reset

| off | type | meaning |
|---|---|---|
| 0x1c0 | int | state 0..14 |
| 0x1c4 | AnimCtrl* | `0x4120a0` (vtable `0x4a9a9c`) |
| 0x1cc | float | "player defeated" timer = AnimLen(6) |
| 0x1d0 | float | **reload/throw timer** (> 0 ⇒ `-= dt`) |
| 0x1d4 | float | total melee duration = AnimLen(3) + AnimLen(5) |
| 0x1d8 | float | state-8 timer = AnimLen(4) |
| 0x1e4 | float | state-12 timer = AnimLen(15) |
| 0x1ec | float | timer after a successful melee hit = AnimLen(5) |
| 0x1f0 | float | melee wind-up = AnimLen(3) |
| 0x1f4, 0x1f8 | float, int | idle: remaining time and chosen idle animation 9..13 (`0x411c80`) |
| 0x1fc | u8 | "next Touch with enlarged range" (§4.4) |
| 0x200 | int | number of long fuses until the next short one; Reset: `P+0x98` = 4 |

PostLoad `0x410f80`: only AnimCtrl and **Stand** (`+0x168`) – no Wander/Chase/Path: it never moves (only knockback/platform).
Reset `0x411020`: `Enemy::Reset`, behaviour = Stand, all timers 0, `+0x1f8 = 9`, state **0**, **clear msgmask 0x10** (`0x4110ab`).

### 4.2 Update `0x4110c0` (jump table `0x4117fc`)

```c
void Bomber_Update(Bomber *e) {
    Enemy_Update(e);
    RegisterActor(e, 10);                             /* 0x40c080: actor list 1 -> projectiles (thrown-back bombs) can hit him, PROJECTILES §2.5 */
    Actor *t = FindTarget(1, 0);                      /* sight 2400, |dy| < 1000 */
    if (e->reload > 0) e->reload -= dt;   if (e->hitT > 0) e->hitT -= dt;
    if (t && dist3(t->pos, e->pos) < P->meleeDist /*+0x8c = 300*/)       /* byte table 0x4117f0 */
        if (e->state != 4 && e->state != 5 && e->state != 13 && e->state != 14) e->state = 4;
    switch (e->state) {
    case 0:  if (t) e->state = 2;  break;                                /* IDLE 0x4111db */
    case 1:  break;                                                      /* empty */
    case 2:  /* AIMING 0x4111ff */
        if (!t) { e->state = 0; break; }
        H_SetTurnSpeed(P+0x14 /*2π*/);  H_Tick(dt);  H_TurnTo(e->pos, t->pos, /*snap*/0);   /* 0x41b940, 0x41ba90, 0x41ba60 */
        if (e->reload > 0 || AngDiff(H.target, H.cur) > 0) break;        /* only throw once fully turned */
        Seg m;  if (!GetVector(e, /*typecode*/1, &m, 0)) break;          /* one marker = throwing hand */
        m.end = t->pos;
        if (!e->vtbl[58](t, &m)) break;                                  /* Fire 0x411e80, §4.3: false = no free bomb */
        e->reload = AnimLen(14);  e->state = 3;  SoundFx(50, 0);
        break;
    case 3:  /* THROW ANIMATION 0x411341 */
        if (e->reload > 0) break;
        e->reload += P->reload /*3.5*/ - 0.5f * floor((P->hpMax - e->hp) * 0.333333f);   /* 0x4a9880; from 3 damage on, 0.5 s faster */
        e->state = 2;  break;
    case 4:  /* MELEE start 0x4113b2 */
        if (!t) { e->state = 0; break; }
        e->windup = AnimLen(3);  e->meleeT = AnimLen(3) + AnimLen(5);  e->state = 5;  SoundFx(51, 0);  break;
    case 5:  /* MELEE 0x411412 */
        if (!t || e->meleeT < 0) { e->state = 7; break; }
        e->meleeT -= dt;  e->windup -= dt;
        if (e->windup > 0) break;
        e->bigTouch = 1;                                                 /* +0x1fc */
        if (!Touch(1, 0)) { e->state = 0; break; }                       /* §4.4: range = 60 + player radius + 300 */
        dir = normalize_xz(t->pos - e->pos);  pt = e->pos + dir * 60 + (0, 140, 0);
        if (t->vtbl[39](e, P->damage /*3.0*/, &dir, &pt, 0)) e->state = 9;
        else { e->t_after = AnimLen(5); e->state = 6; }
        break;
    case 6:  if (e->t_after > 0) e->t_after -= dt; else e->state = 11;  break;      /* 0x4115b2 */
    case 7:  e->t7 = AnimLen(4); e->state = 8;  break;                               /* 0x411605 */
    case 8:  if ((e->t7 -= dt) <= 0) e->state = 0;  break;
    case 9:  e->t_win = AnimLen(6); e->state = 10;  break;                           /* player defeated */
    case 10: if ((e->t_win -= dt) <= 0) e->state = 0;  break;
    case 11: e->t_stun = AnimLen(15); e->state = 12;  break;                         /* 0x41169f */
    case 12: if ((e->t_stun -= dt) <= 0) e->state = 0;  break;
    case 13: /* DEAD 0x4116ec */
        e->deathT += dt;
        if (e->vtbl[51]() /*AnimLen(8) + 1.0*/ <= e->deathT) { e->flags10c |= 1;  MsgMask_Set(e->id, 0x10); }   /* 0x411729 -> script: EndLevel */
        *e->vtbl[4]() &= ~0x400;  break;
    case 14: /* HIT BY EXPLOSION 0x411759 */
        if (e->hp <= 0) { e->deathT = 0; e->vtbl[57](); /*hit stars*/ e->state = 13; SoundFx(53, 0); }
        else if (e->hitT <= 0) e->state = 0;
        break;
    }
}
```
Because states 6..12 return to 4 as soon as the player stays within 300, he keeps meleeing as long as the player stays close (wind-up AnimLen(3) ≈ 0.23 s with the W2B model).

### 4.3 Fire `vtbl[58](target, Seg *m)` = `0x411e80` – throws a **bomb** (type 40, BONUS.md §7)

`T` = default ctor inline + **template 0** (`0x449070(0, &T)`: gravity 15·200, bounces indefinitely, damping 0.95/0.99, damage 1000, invisible,
`hits_all = 0` ⇒ only hits category 2 subtype 8/12), then `T.pos = m.start`, `T.dir0 = normalize(m.end − m.start)` (3D), `T.speed = P+0x60 = 800`,
`T.target = 0`, `T.owner = this`, **fuse `T.life (+0x2c)`**: if `+0x200 ≠ 0` ⇒ `P+0x94 = 7.5 s` and `+0x200--`; otherwise `P+0x90 = 1.3 s` and `+0x200 = P+0x98 = 4`
(so 4 long, 1 short, 4 long, …). Then `0x44d5d0(&T, 0, −1, 0)`: first free bomb from the pool `0x5e4880` (`+0x131 == 0`) → `0x44d4d0` (fuse `+0x10c = T.life`,
projectile with `T.carried = bomb`); no free bomb ⇒ NULL ⇒ Fire returns `false` and the state stays 2.
The long fuse gives the player time to pick up the bomb (Perso state 6, OBJECTS.md §3) and throw it back. Own bombs skip the owner in the
hit test (`a == T.owner`); a thrown-back bomb (owner = player) explodes on contact with type 12 (subtype 8). Every explosion (`0x44d650`, radius **400**) calls
`vtbl[40]` on **all** npcs with category 1 or 2.

### 4.4 Damage and Touch

* `vtbl[39]` = `0x411ab0` (player peck/charge): if `kind` is 0 or 1 and `hitT ≤ 0`: `hitT = AnimLen(15)`, SoundFx 54, state 11. **Always returns `false`,
  hp stays the same**: the player cannot peck it to death, only knock it briefly out of its throwing rhythm (and if the player is within 300, 11 immediately becomes 4 = counter-melee).
* `vtbl[40](&pos, r)` = `0x4119b0` (explosion): if state ≠ 13, `hitT ≤ 0` and `|pos − own pos|² < r²`: SoundFx 52, state 14, `hitT = AnimLen(15)`,
  `Enemy_TakeDamage(0, 1.0, &(0,0,0), pos, 0)` ⇒ **1 hp per explosion, 5 explosions**. During `hitT` (≈ 3.6 s) invulnerable.
* `vtbl[31]` Touch = `0x411840`: if `+0x1fc == 0` ⇒ base class `0x40c1e0`; otherwise (the flag is cleared) the same loop over `0x4c5258` but with
  range `own radius + other radius + P+0x8c` (60 + 69 + 300).
* `vtbl[41](true)` = `0x411b10`: state ≠ 13 ⇒ 9. `vtbl[47]` `0x411970` (byte table `0x411998`): not an actor in state 1 and 13.
* Read again (round 33). **Touch**: both `0x40c1e0` and `0x411840` measure the 3D distance between the position pointers `vtbl[34]` (his feet,
  the Perso's `0x44c030` = feet `+0x1f4`, or the root position `+0x544` while `+0x550` is set) against `vtbl[32]` + `vtbl[32]` (the Perso's radius =
  `+0x114` via `0x4624d0`), over list 2 `0x4c5258`; the thrower adds `P+0x8c` once after `+0x1fc`. So the port's 3D distance was already the
  original's; it now takes the Perso's position pointer (`perso_pos`, also in FindTarget and the other enemies' Touch). **Knockback**: the blast
  `0x4119b0` (state ≠ 13, `hitT ≤ 0`, `dist² < r²`): SoundFx 52, state 14, `hitT = AnimLen(15)`, then `Enemy_TakeDamage(0, 1.0, &(0,0,0), &pos, 0)`,
  whose `Behav_Knock 0x41b6b0` refuses while Stand's knock timer runs and otherwise starts it with a **zero** direction for `vtbl[53](−1)` =
  `0x411d50` in state 14 = `AnimLen(7)` (jump table `0x411e44`; 0.4 s with model 52) - always shorter than `hitT` (3.6 s), so the refusal
  cannot happen and the knockback never moves him. The peck `0x411ab0` never calls TakeDamage. **Platform / sweep**: his only behaviour
  is Stand, whose Tick is the common move `0x41b2c0` (ENEMY.md §5.1): every frame a zero step through the sphere sweep (r 60, centre 71 above
  the feet), the platform delta of the ground probe and the free test. In W2B he stands on world floor and touches nothing, so it changes
  nothing there (checked: position identical to the old port through the whole fight). **Boss bar**: none - the only callers of `0x4484d0`
  are `0x40cbf1`, `0x40dd80`, `0x40fd82` (classes 16, 15, 14). **Death**: the ordinary remove path (§4.6), no class code of its own. All ported.

### 4.5 Animations (`vtbl[45]` = `0x411b30`, table `0x411c40`; records `0x4b1d10`; model: 17 animations)

| state | logical anim | sub[0] | speed | |
|---|---|---|---|---|
| 0 | idle `0x411c80`: if `+0x1f4 ≤ 0` pick `9 + rand() % 5` (equal to the previous ⇒ `9 + (n − 8) % 5`), play it, `+0x1f4 = AnimLen(n)` | 9..13: 11,12,13,14,15 | 2 | |
| 1 | – | | | |
| 2 | **0** | 0 | 3 | aiming (standing) |
| 3 | **14** | 6 | 3 | throwing |
| 4, 5 | **3** | 3 | 3 | melee wind-up |
| 6 | **5** | 4 | 2 (prio 1500) | after the melee |
| 7, 8 | **4** | 5 | 3 | |
| 9, 10 | **6** | 7 | 3 | player defeated |
| 11, 12 | **15** | 8 | 3 | pecked / after successful melee |
| 13 | **8** | 10 | 1 (prio 2000) | dead |
| 14 | **7** | 9 | 3 | hit by explosion |

Records 1/2 (sub 1/2, turning) are only used by the duration function `vtbl[53]` `0x411d50` (state 2), never played; 16 = sub 0, 17 = sub 16 unused.
W2B model 52, duration in s of animation 0..16: 5.8, 1.6, 1.6, 0.7, 1.1, 1.1, 0.8, 8.1, 10.8, 1.2, 2.5, 11.8, 9.7, 9.8, 3.7, 4.9, 49.1.

### 4.6 Sound, messages, events
SoundFx (all 2D, `inst = 0`; SOUND.md §5 refs 86..90): **50** throw (`0x41132a`), **51** melee start (`0x4113fb`), **52** hit by explosion (`0x411a56`),
**53** dead (`0x41178f`), **54** pecked (`0x411af2`). Messages: only 6/11. Events: **set msgmask 0x10 when removed** (`0x411729`), cleared in Reset;
`flags10c |= 1` is the ordinary remove flag: `0x40bf60` (start of the next frame) calls `vtbl[29]` = the base `0x41aff0` = `[0x4c532c]++` (killed-enemies
counter) and `0x407850` = out of the world, so **he disappears**, after fading out over the second half of `AnimLen(8) + 1.0` (`Enemy::Update 0x41a3e0`).
The flag is set again every frame of state 13 once the time is up, but an instance that is out of the world gets no more Think (it is not in the visible
list world+0x64 walked by `0x42b400`). **He has no boss bar**: the only callers of the HUD bar setter `0x4484d0` are `0x40cbf1` (class 16), `0x40dd80`
(Boss2) and `0x40fd82` (Buzz). Ported (`bomber_update` state 13: fade, then msgmask 0x10 and removed).
0x200 via the base class's ground following (it just falls with gravity).

## 5. Type 11 – ground bomber (unused; high level)

Same layout and state numbers as type 7 (ENEMY.md §8): PostLoad `0x4121d0` creates all four behaviours, Reset and `vtbl[41]` are literally those of type 7
(`0x416ed0`, `0x417fd0`). Update `0x412310`, jump table `0x412d14`; timers `+0x1d0` reload, `+0x1c8` cooldown, `+0x1e8` (only counted down), `+0x158`.

| # | code | core |
|---|---|---|
| 0 | `0x4124d8` | patrol; `cool < 0` and target ⇒ home = pos, leash on, → 11 |
| 2 → 3 | `0x4123e6`, `0x412434` | to wander (turn speed `P+0x10`), wander; target ⇒ 11; TRAJ and < 10 from home ⇒ 0 |
| 11 | `0x412550` | aim start: Stand, turn speed `P+0x14`, target angle at the player; `+0x1e0 = angle / P+0x14`, `+0x1e4 = AnimLen(19) − reload`; animations 1/2 scaled; → 12 |
| 12 | `0x412639` | not yet turned ⇒ keep turning; otherwise xz distance `≤ P+0x54` (300) ⇒ **5**; otherwise if `reload ≤ 0`: count down `+0x1e4`, ≤ 0 ⇒ **13**; no target ⇒ 2 |
| 13 | `0x4127a5` | throw: marker **typecode 0** n 0, `m.end = player`, `vtbl[58]` (`0x413580`); success ⇒ `reload += P+0x4c` (0.7), → 11 |
| 5 | `0x41283d` | charge start (only if xz distance ≤ 300; otherwise it stays here – uncertain whether this is intended): direction in `+0x1ec`, Chase, `+0x1d4 = distance / P+0xc`, animation 8 scaled, → 6 |
| 6 | `0x41297f` | charge: no target / time up ⇒ 14; Touch ⇒ `Perso->vtbl[39](this, P+0x3c = 2, …)`: killed ⇒ 8, otherwise `+0x1f8 = AnimLen(10)`, → 1 |
| 1 | `0x412b08` | after the bite; then `cool = P+0x38` (0.1), → 2 |
| 14 → 7 | `0x412b54`, `0x412b96` | brake AnimLen(9), speed `P+8`; then `cool`, → 2 |
| 8 → 9 | `0x412be2`, `0x412c24` | player defeated AnimLen(11), → 2 |
| 4 | `0x412ca9` | hit: `hp ≤ 0` ⇒ `vtbl[57]`, → 10; otherwise after `hitT` → 2 |
| 10 | `0x412c4c` | dead (as type 4) |

Fire `0x413580`: direction = `normalize_xz(m.end − m.start)` (y = 0), **template 0** unchanged (speed 1500, lifetime/fuse 2 s), `T.target = player`
(steering factor 0.025), `T.owner = this` ⇒ `0x44d5d0` (bomb). `vtbl[39]` `0x412d90` = as type 7 (state 4, `hitT = vtbl[53](0)`). `vtbl[47]`: not an actor in state 10.
Animations (table `0x412f44`, records `0x4b1ed0`): 0 ⇒ path; 2/3 ⇒ wander; 11 ⇒ turning 1/2 (sub 16/17); 12, 13 ⇒ **19** (sub 19, throwing); 5/6 ⇒ 8 (sub 13);
7/14 ⇒ 9 (sub 15); 1 ⇒ 10 (sub 14); 8/9 ⇒ 11 (sub 18); 4 ⇒ 12 (sub 11); 10 ⇒ 13 (sub 12). The model would thus need ≥ 20 animations; no level has one.

## 6. Type 10 – flying gunner (unused; high level)

Update `0x415490`, jump table `0x415fe8`; Reset `0x4153c0` (TRAJ ⇒ 0, otherwise 7); timers `+0x1c8` cooldown, `+0x1d0` reload, `+0x1d4` dive, `+0x1dc`, `+0x1cc`.

| # | code | core |
|---|---|---|
| 0 | `0x41561a` | patrol; target ⇒ home = pos, leash on, → 1 |
| 1 | `0x415692` | Stand, turn speed `P+0x14`, target angle at the player; 3D distance `> P+0x50` (800) ⇒ **2**, otherwise **3**; no target ⇒ 6 |
| 2 | `0x415798` | **shoot at range**: keep turning; fully turned and `reload ≤ 0` ⇒ marker typecode 1 n 0, `vtbl[58]`, `reload += P+0x4c` (1.2); distance `< 800` ⇒ 3; distance `> 810` ⇒ drifts at **100 units/s** (`0x4a9010`) straight at the player (two rays `0x4359b0`: body and vertical, on collision don't move) |
| 3 → 4 | `0x415afb`, `0x415b3e` | start Chase; in 4: xz distance `≤ P+0x5c · T` (T from AnimLen(7)) and `dot > 0.95` ⇒ speed `P+0x5c`, → **9**; otherwise shoot once `reload ≤ 0`; no target ⇒ 6 |
| 9 | `0x415d4c` | dive: time up / no target ⇒ 6; Touch ⇒ damage `P+0x3c` (1): killed ⇒ 12, otherwise `+0x1dc = AnimLen(7)`, Stand, → 6 |
| 12 → 13 | `0x415eb6`, `0x415ef8` | player defeated AnimLen(8), → 6 |
| 6 → 7 | `0x41553d`, `0x415576` | to wander, wander (target ⇒ 1; TRAJ ⇒ 0) |
| 8 | `0x415f7d` | hit: `hp ≤ 0` ⇒ `vtbl[57]`, → 14; otherwise after `hitT` → 6 |
| 14 | `0x415f20` | dead; `vtbl[43]` then falls back to the base class `0x41a4e0` ⇒ **it falls down** |
| 5, 10, 11 | – | empty |

Height `0x416a10`: target height = `player.y + 180` (`0x4a9dbc`), or `home.y` with no target **and in state 4** (literally so; uncertain whether intended); step = `dt · P+0xc` (700) in
state 4, otherwise `dt · P+8 · 0.125` (50/s); descends to the ground, rises with a ceiling ray (on collision `dy · [0x53a558]`); msgmask 0x200.
Fire `0x416800`: default `T` + damage `P+0x40` (1), speed `P+0x60` (1500), target = player, aim height 100, **steering factor 0.02 / vertical 0.01, 50 s**, clamp 0.8/0.8,
lifetime 50 s, visual `P+0x74` = 2 (energy orb) ⇒ `0x4490a0`. `vtbl[39]` `0x416070`: state 8, `hitT = vtbl[53](0)`, `Enemy_TakeDamage(…, kind 1)`.
`vtbl[41]`: ≠ 14 ⇒ 12. Animations (byte table `0x416230`, records `0x4b2478`): 1/2 ⇒ 1; 3/4 ⇒ 2 (sub 1,2,2,2) if Chase is running, otherwise 1; 6/7 ⇒ wander; 8 ⇒ 9 (sub 11);
9 ⇒ 7; 12/13 ⇒ 8 (sub 14); 14 ⇒ 10 (sub 12).

## 7. Port recipe (building on `src/enemy.c`)

General (all new types): also call `enemies_add` for the type (`src/main_engine.c` line 441: currently only 4..6); `Enemy` gets a per-instance P copy
(`radius, height, walk, run, dash, see, see_dy, hp, damage, cool, reload, leash`) which **message 11** can override (§1: a = 0 leash, 1 sight, 7 walk-, 8 run speed,
33 reload time in whole seconds, 4 = reset, 5 = leash on) and message 6 (on/off). `E_RADIUS/E_HEIGHT/...` become fields; `enemy_radius/enemy_height` read them.

### 7.1 Type 13 (ghost) – reuses almost all of type 4

Constants: radius 30, height 140, wander 100 u/s, chase 300 u/s (turning 2π rad/s, first turning in place like type 4 state 2), dive 500 u/s,
sight 1500 (|dy| unlimited), hp 3, bite 2 hearts, fireball 2 hearts / 1000 u/s / straight / 15 s / radius 5, reload 1.0 s, after hit 2.0 s, cooldown after bite 0.5 s,
dive condition xz ≤ 300 and dot > 0.95, dive time = len(anim 13)/3 + ½·len(anim 14)/2, leash 1000 (3D), hit 0.25 s, acceleration 1000, knockback 600.

1. `enemies_add`: `hp = 3`, `st = 5` (wander; with TRAJ 0), `hand = rand() & 1`, `dead_t = 0`, `inst->fade_target = 0.5` (set again every frame).
2. States → existing code: 5 = type 4's 8 (wander), 1/2 = 2/1 (noticed/chase, speed 300), 7 = 4 (charge, speed 500), 13/8 = 6 (brake, **no** `cool`),
   3 = 3 (after the bite, then `cool = 0.5`), 9/10 = 11, 6 = 9 (hit; on recovery `reload = 2·P.reload`), 11 = 12 (dead).
3. New in chase (state 2): first the dive test; otherwise if `reload <= 0`: muzzle point = `hand ? marker0 : marker1` (marker nodes with typecode 1 from the model,
   world position of the first point), direction = `normalize(player.pos − muzzle)`, projectile with visual kind 3 (fireball, PROJECTILES.md §5.5; ported in
   `src/main_engine.c` `fireball_draw`), damage 2, owner = ghost, SoundFx 20 (3D); `reload += P.reload; hand ^= 1`. If the projectile kills the player ⇒ `st = 9`.
4. Vertical: `enemy_move` must do **no ground/edge test** for this type (walls only; y stays) and the gravity block at the bottom of `enemy_update` is skipped; instead, per §3.3:
   target y = player's feet (or `home.y`), step `300·dt` (× 0.2 while the player is rising in a jump) resp. `walk·dt`, not below ground, not through the ceiling (ray from y + 140);
   not while hit/dead.
5. Death: `dead_t` starts at `L/2` with `L = len(anim 12)/3 + 1.0`; fade = `(dead_t − L/2)/(L/2)`; gone at `dead_t ≥ L`; no particles.
6. Animations (`g_ea` style, .ins index / speed): running 2/3 (chain 1,2,2,2), hover-walk 4/3 (chain 3,4,5,0), idle 6..10 / 2, turning 0/3, dive 13/3, brake 15/3, bite 14/2,
   won 16/3, hit 11/4, dead 12/3.

### 7.2 Type 12 (bomb thrower, W2B inst 533)

Constants: radius 60, height 280, sight 2400 (|dy| < 1000), hp 5, melee 3 hearts, melee zone 300 (3D, pos–pos), melee range 60 + 69 + 300, turning 2π rad/s, reload 3.5 s
(3.0 s once he has ≥ 3 damage), throw speed 800, fuses 7.5 s ×4 then 1.3 s ×1, explosion radius 400, 1 hp per explosion, invulnerable AnimLen(15) = len(anim 8)/3 after every hit/peck.

1. Stands still (no wander/chase): only knockback + gravity from the base class. Starts off (message 6,0 after 1 s) until the script sends 6,1 ⇒ message 6 must work.
2. State machine §4.2 verbatim (15 states, one of which is empty). `enemy_take_damage` for this type: no hp loss, `hit_t = len(8)/3`, `st = 11`, SoundFx 54, return 0.
3. Requires the **bomb system** (ported since: BOMB.md, BOMB_CARRY.md; BONUS.md §7, PROJECTILES §2.4, Perso state 6): projectile template 0 (g = 3000, bounces, damping 0.95 ground / 0.99 air per 1/60 s,
   speed 800) with a carried bomb instance (type 40, W2B has them), fuse from `T.life`, pickup/throw-back, explosion r 400 ⇒ `blast()` on all enemies and the player.
   `enemy_blast(e, pos, r)`: types 4..11, 13 ⇒ dead (`take_damage(hp)`); type 12 ⇒ §4.4 (1 hp, `st = 14`, SoundFx 52). Without bombs the level cannot be finished:
   set msgmask 0x10 on the instance when it is removed (script ⇒ 1083).
4. Animations (.ins index / speed): idle 11..15 / 2 random, aiming 0/3, throwing 6/3, melee 3/3, after melee 4/2, 5/3, won 7/3, pecked 8/3, hit by explosion 9/3, dead 10/1 (+1.0 s, fade in the second half).
5. SoundFx 50..54 (2D) at the moments in §4.6.

### 7.3 Types 10 and 11
Do not port: no level creates them (§1). If it should ever be needed: type 11 = type 7's state machine (ENEMY.md §8) with `Fire` = bomb (template 0, target = player) and
P column sub 7; type 10 = §6 with the ghost's height control (+180, 50/s).

## 8. Open questions
* Type 13: `+0x1e4/+0x1e8/+0x1ec/+0x1f0/+0x1f1` have no reader; state 12 is never set anywhere. The 130/30 aim height of Fire has no effect (steering factor 0) – a leftover of an earlier homing version.
* Type 13 on death: `Enemy::Update` writes `+0x6c` directly (0 → 1) while the fade target stays 0.5; the first dead frame is thus briefly opaque (uncertain whether visible).
* Type 12: state 1 is empty and never set anywhere; `P+0x40 = 4` and `P+0x48 = 300` have no reader. The meaning of animation 8 ("pecked"/laughing) is guessed.
* `[0x4c4bd0]` = 3 or 4 after `0x497ed0` (hit kind of the body-to-hand ray) is not identified; presumably world / instance.
* The bomb side (`0x44d4d0`, states 1..6, `+0x132` "held", Perso `0x44db50`) has only been read from the caller side.
* Types 10/11: duration functions `0x416500`/`0x413290`, the path/wander-animation choice, and type 11's `+0x1e8`/`+0x1ec` have not been followed.
