# PROJECTILES.md — projectile system (launcher type 42, missile type 41, templates, visuals)

Static analysis of `game/Woody.exe` (disassembly `out/disasm_full.txt`, `tools/drange.py`, `tools/funcinfo.py`). Every fact has an address;
"uncertain" = not read line by line, or no reader/writer found. Ties into OBJECTS.md §2.2 (launcher), EVENTS.md §3.2 (press events),
ENEMY.md §8 (shooting enemy), BONUS.md §7 (bomb), SOUND.md §5 (SoundFx).

## 0. Summary (what the port needs to know)

- There is one global pool of **200 projectiles** (`0x5d7d48`, 0x104 B each, active byte `+0xe4`; OBJECTS.md said 50: wrong). A projectile is
  not an instance and not an actor: a point with a velocity. Update `0x4490f0(dt)` from the frame function (`0x401e7e`, right after the VM tick `0x4019c0`, not during pause `app+0xf4 & 8`).
- A projectile is created from a **0x68-byte parameter block** (template). Four templates live at `0x5d7ba8 + kind·0x68`, filled by `0x448c70`
  (once, from `0x404360`). The launcher keeps its own copy at `this+0x108`; message 1002 writes into it.
- **Message 1002 parameter 2 = lifetime in seconds (×0.01)**. W1A: 1.40 s (launcher 27: 1.50 s) at a speed of 1000 → the shot travels 1400 (1500)
  units and then vanishes. No gravity, no target (target −1 → NULL) → **straight line along the typecode-0 marker**.
- **The W1A launchers do NOT use the missile model (type 41).** Template 1 has visual kind 2 = `0x46f8a0`: an **energy orb** (two oppositely
  spinning additive sprites, bank 0 image 6) with a **400-unit ribbon** (bank 0 image 0, width 60) and a flash at the muzzle and on impact.
  The missile model + smoke (`0x4700e0`) belongs to visual kind 0/1, which no script can select via 1002 (parameter 18 only gives 2, 3, 4 or 0 — see §3;
  0 does not occur in any level); it is only reached through parameter blocks the code fills itself (enemies `P+0x74`, class 20/21). The 8 type-41 instances in W1A
  are thus irrelevant to the launchers, but the shooters' model pool matters: enemy type 7 (kind 0) and 8 (kind 1) shoot with it (ENEMY.md §8.1).
- Hitting: a swept sphere (radius 5) against the cylinder of every actor in actor list 1 → `actor->vtbl[39](0, damage = 1.0, &direction, &pos, 0)` = `Perso::Hit`
  (1 heart, knockback 500 u/s along the flight direction, 0.6 s invulnerable; PERSO_MOVE.md §4.4). The projectile then disappears. Hitting the world/an instance
  (swept sphere `0x4359b0` from old to new position) → projectile gone (max bounces = 0). The player's attack **cannot** destroy a projectile
  (no external caller of the deactivation besides the bomb code `0x44d339`/`0x44d7c2`).
- Sound: at the projectile's **creation** (`0x449130`), SoundFx 17/18/19/20 depending on the visual kind, 3D on the owner (the launcher).
  Visual kind 2 → **SoundFx 19** (ref 74, vol 25). No impact sound for ordinary projectiles (SoundFx 12 is only for a carried bomb landing).

## 1. Data structures

### 1.1 Parameter block / template `T` (0x68 B)

Copy in the projectile at `P+0x48`, in the launcher at `L+0x108`. Default constructor `0x44a260` (used by class ctors; the launcher ctor
`0x452140` and the shooter `0x418820` set the same values inline) and the four templates from `0x448c70`:

| T+ | P+ | L+ | type | ctor `0x44a260` | kind 0 | **kind 1** | kind 2 | kind 3 | meaning (reader) |
|---|---|---|---|---|---|---|---|---|---|
| 0x00 | 0x48 | 0x108 | vec3 | – | 0 | 0 | 0 | 0 | start position (set by `Fire`) → `P+0xb0` |
| 0x0c | 0x54 | 0x114 | vec3 | – | 0 | 0 | 0 | 0 | start direction `dir0` (normalized by `Fire`) → `P+0xbc`, `P+0xc8 = dir0·speed` |
| 0x18 | 0x60 | 0x120 | f32 | 5 | 30 | **5** | 5 | 5 | **radius** of the projectile (hit test `0x44a0a0`; ×2 with a carried instance; also the tolerance of the press probe) |
| 0x1c | 0x64 | 0x124 | f32 | 0 | 15 | **0** | 0 | 0 | **gravity**: `vel.y −= dt · g · 200` (`0x44945f`), floor −800 |
| 0x20 | 0x68 | 0x128 | f32 | 1000 | 1500 | **1000** | 1000 | 1000 | **initial speed** (units/s) → `P+0xd8` |
| 0x24 | 0x6c | 0x12c | f32 | 1 | 0.95 | **1** | 1 | 1 | damping per 1/60 s **on the ground** (`P+0xec`): `vel *= pow(d, dt·60)` |
| 0x28 | 0x70 | 0x130 | f32 | 1 | 0.99 | **1** | 1 | 1 | damping per 1/60 s **in the air** |
| 0x2c | 0x74 | 0x134 | f32 | 5 | 2 | **15** | 15 | 15 | **lifetime** (s): `age ≥ T+0x2c` and no carried instance → gone (`0x4493ec`) |
| 0x30 | 0x78 | 0x138 | f32 | 20 | 1000 | **1** | 1 | 1 | **damage** (2nd argument of `vtbl[39]`; hearts) |
| 0x34 | 0x7c | 0x13c | i32 | 0 | −1 | **0** | 0 | 0 | **max number of bounces** against world/instances; −1 = unlimited (`0x449dff`) |
| 0x38 | 0x80 | 0x140 | ptr | 0 | 0 | 0 | 0 | 0 | **target instance** for target-seeking (set by `Fire` = `L+0x178`) |
| 0x3c | 0x84 | 0x144 | f32 | 150 | 125 | **125** | 150 | 25 | **aim height**: target point = `target.pos + (0, h, 0)` (also in `Fire` when the aim flag is set) |
| 0x40 | 0x88 | 0x148 | f32 | 0 | 0.025 | **0.025** | 0.5 | 0.5 | **xz steering factor** per 1/60 s: `k = pow(1 − s, dt·60)`, `dir.xz = target.xz·(1−k) + dir.xz·k` |
| 0x44 | 0x8c | 0x14c | f32 | 0 | 0 | **2.5** | 100 | 100 | **vertical steering speed** (units per 1/60 s, see §2.2) |
| 0x48 | 0x90 | 0x150 | f32 | 0 | 2 | **15** | 15 | 15 | steer xz only while `age < T+0x48` |
| 0x4c | 0x94 | 0x154 | f32 | 0 | 0 | **15** | 15 | 15 | steer vertically only while `age < T+0x4c` |
| 0x50 | 0x98 | 0x158 | f32 | 0.7 | 0.7 | **0** | 0 | 0 | xz clamp: if `dot(vel.xz, dir0.xz) < T+0x50` → revert to the xz speed from before steering (`0x4499cf`) |
| 0x54 | 0x9c | 0x15c | f32 | 0.7 | 0.7 | **0** | −1 | −1 | y clamp: if `1 + vel.y·dir0.y < T+0x54` → revert `vel.y` to before steering (`0x4498ec`; literally so, presumably a bug in the original) |
| 0x58 | 0xa0 | 0x160 | ptr | 0 | 0 | 0 | 0 | 0 | **owner** (instance): sound source, skipped in the hit test; launcher: `this` (`0x452352`) |
| 0x5c | 0xa4 | 0x164 | ptr | 0 | 0 | 0 | 0 | 0 | **carried instance** (bomb type 40, §2.4); launcher: always 0 |
| 0x60 | 0xa8 | 0x168 | i32 | 4 | 4 | **2** | 2 | 2 | **visual kind** 0..3, ≥ 4 = none (jump table `0x4492b8`, §5) |
| 0x64 | 0xac | 0x16c | u8 | 1 | 0 | **1** | 1 | 1 | 1 = hits **all** actors; 0 = only category 2 with subtype 8 or 12 (`0x44a0bd`) |

Kind 0 is the "thrown" template (gravity 15·200 = 3000, bounces indefinitely, 2 s, damage 1000, invisible) used by the bomb code; a launcher with
kind 0 throws real bombs (`Fire`, §4.3). Kinds 2 and 3 differ from 1 only in aim height and steering values (aggressive target-seeking).

### 1.2 Projectile `P` (0x104 B, pool `0x5d7d48[200]`)

| off | contents |
|---|---|
| +0x00, +0x24 | two press probes (ctor `0x436cf0`), only used with a carried instance (§2.4) |
| +0x48..0xaf | copy of `T` |
| +0xb0 | vec3 **position** |
| +0xbc | vec3 **direction** (normalized velocity; the visuals and the hit callback read this) |
| +0xc8 | vec3 **velocity** (units/s) |
| +0xd4 | f32 **age** (s) |
| +0xd8 | f32 current speed `|vel|` |
| +0xdc | i32 bounce count |
| +0xe0 | i32 slot **generation counter** (`++` on every init; the visuals compare it to detect reuse) |
| +0xe4 | u8 **active** |
| +0xe8 / +0xec | i32 / u8: frames on a press node / "resting on the ground" (§2.4 only) |
| +0xf0..0xfc, +0x100 | plane (n, d) of the last bounce, u8 "has bounced" (no reader found: uncertain) |

API: `0x449070(kind, T* out)` copies a template; `0x4490a0(T*)` = first free slot → `0x449130` init, **NULL when all 200 are taken**
(nothing happens then); `0x4490e0(P*)`/`0x449120` = deactivate (`+0xe4 = 0`, nothing else: the visual notices on its own, §5);
`0x4492d0(T*)` = re-init without sound/visual (bomb, `0x44d4ad`); `0x448c70` also deactivates all slots.

Callers of `0x4490a0`: launcher `0x45268a`, shooter enemy `0x41898c` (ENEMY.md §8), `0x40d128` (class-17 family `0x40cb80`), `0x414e99`, `0x4169ae`
(other enemy classes), bomb `0x44d5ae`.

## 2. Projectile update

### 2.1 Init `0x449130(this = P, T* src)`

```c
probe_ctor(P+0); probe_ctor(P+0x24);
P->age = 0; P->bounces = 0; P->press_frames = 0; P->grounded = 0; P->bounced = 0; P->active = 1;
memcpy(P+0x48, src, 0x68);
P->pos = T.pos;  P->dir = normalize(T.dir0);  P->speed = T.speed;  P->vel = P->dir * T.speed;
P->generation++;
switch (T.visual) {                                   /* jump table 0x4492b8 */
case 0: SoundFx(17, T.owner); Visual_Missile(P); break;   /* 0x44924f -> 0x4700e0 */
case 1: SoundFx(18, T.owner); Visual_Missile(P); break;   /* 0x449257 */
case 2: SoundFx(19, T.owner); Visual_Bolt(P);    break;   /* 0x449277 -> 0x46f8a0 */
case 3: SoundFx(20, T.owner); Visual_Fireball(P);break;   /* 0x449297 -> 0x470af0 */
}                                                     /* >= 4: no sound, no visual */
```
`SoundFx` = `0x468a00(id, inst)` on `[0x5e48c8]`: with the instance in 3D (SOUND.md §5: 17 = ref 69, 18 = ref 70, vol 50; 19 = ref 74, 20 = ref 75, vol 25).

### 2.2 Per frame `0x4493c0(this = P, dt)`

```c
P->age += dt;
if (P->age >= T.life && !T.carried) { P->active = 0; return; }             /* 0x4493ec -> 0x449c9c */
if (T.carried) { carried->vtbl[2](1); if (carried is category 3 && carried->byte[0x132]) return; }   /* bomb held: idle */
float f = dt * 60.0f;                                                       /* 0x4aabbc */
float damp;
if (!P->grounded) { P->vel.y -= dt * T.gravity * 200.0f;                    /* 0x4aa164 */
                    if (P->vel.y < -800.0f) P->vel.y = -800.0f;             /* 0x4aabb8 */
                    damp = T.damp_air; }
else              { P->vel.y = 0; damp = T.damp_ground; }
P->vel *= pow(damp, f);                                                     /* 0x4995c0 = pow(st1, st0) */

if (T.target) {                                                             /* 0x4494df: target-seeking */
    Vec3 old = P->vel, tgt = T.target->pos /*+0xc*/ + (0, T.aim_h, 0);
    if (P->age < T.t_xz) {                                                  /* 0x44950d */
        float s = |P->vel|;  Vec3 v = normalize(P->vel);
        Vec2 d = normalize_xz(tgt - P->pos);
        float k = pow(1.0f - T.steer, f);
        v.x = d.x*(1-k) + v.x*k;  v.z = d.z*(1-k) + v.z*k;                  /* y unchanged */
        P->vel = normalize(v) * s;
    }
    if (P->age < T.t_y && T.vsteer != 0 && P->pos.y != tgt.y) {             /* 0x44969d */
        float dy = P->pos.y - tgt.y, step = f * T.vsteer;  if (dy > 0) step = -step;
        Vec3 q = P->pos + P->vel;  q.y += step;                             /* point one second ahead */
        int reached = (dy > 0) ? (q.y < tgt.y) : (q.y > tgt.y);  if (reached) q.y = tgt.y;
        float s = |P->vel|;  Vec3 v = normalize(q - P->pos);
        if (reached) { v.y *= 4.0f /*0x4a94c0*/; v = normalize(v); }
        P->vel = v * s;
    }
    if (1.0f + P->vel.y * T.dir0.y < T.lim_y)  { s = |vel|; P->vel.y = old.y;                 P->vel = normalize(P->vel) * s; }   /* 0x4498ec */
    if (P->vel.x*T.dir0.x + P->vel.z*T.dir0.z < T.lim_xz) { s = |vel|; P->vel.x = old.x; P->vel.z = old.z; P->vel = normalize(P->vel) * s; }
}
P->dir = normalize(P->vel);  P->speed = |P->vel|;
Move(P, P->vel * dt);                                                       /* 0x449cc0, §2.3 */
if (T.carried && P->active) { ... §2.4 ... }
```

Without a target (W1A): `pos += dir0 · 1000 · dt` until `age ≥ life`.

### 2.3 Moving and colliding `0x449cc0(this = P, Vec3* disp)`

```c
Vec3 old = P->pos;  P->pos += *disp;
HitActors(P, &old, &P->pos);                                                /* 0x44a0a0, §2.5 -- before the world test, even if that later finds a wall */
if (T.carried) { carry the platform along via probe P+0x24 (0x437040): pos.xz += delta; carried->flags |= 0x40 during the ray (skip its own hulls) }
[0x53a560] = 0;  Ray(&old, &P->pos, -1);                                    /* 0x4359b0: world + instances, same function as the laser (OBJECTS.md §2.1) */
switch ([0x53a554]) {                                                       /* hit kind, jump table 0x449ea0 */
case 0: break;                                                              /* nothing */
case 2: if (hit_inst /*[0x53a560]*/ == T.carried && T.carried) break;       /* own bomb: ignore; otherwise fall through to 1 */
case 1: if (T.max_bounce != -1 && P->bounces >= T.max_bounce) { P->active = 0; break; }      /* 0x449dff */
        P->bounces++;  P->plane = [0x4b3108..0x4b3114];  P->bounced = 1;
        Bounce(P, disp, &old, &plane, t /*[0x53a558]*/, 1);                 /* 0x449eb0 */
        break;
case 3: if (hit_inst == T.carried && T.carried) break;
default: P->active = 0;                                                     /* 0x449e82 */
}
```
`Bounce` `0x449eb0`: hit point `h = old + normalize(disp)·max(|disp|·t − 0.01, 0)`; end point mirrored in the plane `pos += −2·(n·pos + d)·n` (`0x4a9504`);
`dir = normalize(pos − h)`, `vel = dir · P->speed`; `pos = h` (flag 1). No energy loss besides the damping `T+0x24/0x28`.

For the W1A launcher (`max_bounce = 0`): **the first hit against the world or an instance ends the projectile** (the visual then shows its impact flash, §5.2).
Note: the ray starts at the marker's beginning; if that lies inside a hull of the launcher itself, the shot dies instantly. In W1A this apparently does not happen
(marker = "muzzle", outside the housing); not verified: uncertain.

### 2.4 Carried instance (`T+0x5c`, bombs only; BONUS.md §7)

After `Move` (`0x449b35`): `carried->pos = P->pos + (0, 1, 0)`, `carried+0x60 = P->pos`, `0x4077f0` (re-cell); outside the world (`cell < 0`) → if it is a bomb
(type word category 3, subtype 1) `Bom::Explode 0x44d6e0`, projectile gone. Otherwise the **press probe** `0x436dc0(P, −1, &(pos + (0, r, 0)), r, carried->id)` (EVENTS.md §3.2):
if the projectile is on a press node, the first time SoundFx 12 (3D on the bomb), counter `P+0xe8++`; after **5 frames** `grounded = 1`, `vel.y = 0`, `pos.y = [0x53a568]`
(ground height). This is the only source of "press/unpress from projectiles": **ordinary (launcher) projectiles never send press events**.

### 2.5 Hitting actors `0x44a0a0(this = P, Vec3* old, Vec3* new)`

```c
for (i = 0; i < [0x4c531c]; i++) {                       /* actor list 1 of the previous frame, 8 B per entry (OBJECTS.md §2.1; max 8) */
    Actor *a = list[i].actor;
    if (!T.hits_all && !(cat(a) == 2 && (subtype(a) == 8 || subtype(a) == 12))) continue;   /* 0x40c340 / 0x40c350 */
    if (a == T.owner) continue;
    Cyl c; a->vtbl[24](&c);                              /* Perso 0x44cd60: centre = pos + (0, h/2, 0), radius 69, half-height h/2 */
    float r = T.radius; if (T.carried) r *= 2;
    if (!SweepSphereCyl(old, new, r, &(c.x, c.y - c.hh, c.z), c.r, 2*c.hh)) continue;       /* 0x433920: xz quadratic comparison with R = r + c.r, y range, sphere caps 0x433bc0 */
    if (T.owner && cat(T.owner) in {1, 2}) {             /* fired by the player or an enemy */
        bool res = 1;
        if (!T.carried) res = a->vtbl[39](T.owner, T.damage, &P->dir, new, 0);
        T.owner->vtbl[41](res);                          /* enemy: "player defeated" -> state 8 (ENEMY.md §8) */
    } else
        a->vtbl[39](0, T.damage, &P->dir, new, 0);       /* launcher (category 6): result ignored */
    if (T.carried is a bomb) Bom::Explode(T.carried);
    P->active = 0;  return;                              /* the first hit ends the loop */
}
```
`Perso::vtbl[39]` = `0x44ca00` (PERSO_MOVE.md §4.4): ignored while invulnerable; knockback 500 u/s for 0.2 s along `P->dir` (i.e. **with the shot**),
hit animation, 0.6 s invulnerable, `health −= 1.0`. The result is discarded here, but the Perso update itself kills at `health ≤ 0` (`vtbl[38](3)`, PERSO_FRAME.md line 168).
In the port: `if (player_hit(p, dmg, dir)) player_kill(p, 3);`. Effective hit radius against the player: 5 + 69 = **74** in xz, y from feet − 5 to feet + 193 + 5.

## 3. Message 1002: all 20 parameters (`0x452360`, jump table `0x4524fc`)

| n | field | scale | W-usage (all levels) | meaning |
|---|---|---|---|---|
| 0 | `L+0x128` = T+0x20 | raw | 500, 2500, 3000 | speed (u/s) |
| 1 | `L+0x124` = T+0x1c | raw | 2, 10 | gravity (×200 u/s²) |
| **2** | `L+0x134` = T+0x2c **and** `L+0x184` | ×0.01 | 4..1500 (220×) | **lifetime in s**; `L+0x184` has no reader in the class (uncertain) |
| 3 | `L+0x13c` = T+0x34 | int | – | max bounces |
| 4 | `L+0x138` = T+0x30 | raw | 100 | damage |
| 5 | `L+0x144` = T+0x3c | raw | 40, 100 | aim height |
| 6 | `L+0x188` | ×0.01 | – | no reader found (uncertain) |
| 7 | `L+0x17c` | int | 49× | firing animation (index into the model) |
| 8 | `L+0x180` | ×0.01 | 49× | duration of that animation in s |
| 9 | `L+0x120` = T+0x18 | raw | – | radius |
| 10 / 11 | `L+0x12c` / `L+0x130` = T+0x24 / 0x28 | ×0.01 | – | ground / air damping |
| 12 | `L+0x148` = T+0x40 | ×0.001 | 0, 50 | xz steering factor |
| 13 | `L+0x14c` = T+0x44 | ×0.1 | 0, 100 | vertical steering speed |
| 14 / 15 | `L+0x150` / `L+0x154` = T+0x48 / 0x4c | ×0.01 | 6, 50 | steering time xz / y (s) |
| 16 | `L+0x15c` = T+0x54 | ×0.01 | 90, 95 | y clamp |
| 17 | `L+0x158` = T+0x50 | ×0.01 | 95, 100 | xz clamp |
| 18 | `L+0x168` = T+0x60 | table `0x45254c`: 0→2, 1→3, 2→4, 3→0; > 3 ignored | 1 (90×), 8, 9 (ignored) | visual kind: 0 = orb, 1 = fireball, 2 = invisible, 3 = missile |
| 19 | `L+0x199` | bool | 1 (9×) | `Fire` aims at the target instead of along the marker |

Note the order: **1001 overwrites the whole block** (template copy + reset), so scripts send 1001 and then 1002. W1A only sends `1002 [inst, 2, 140|150]`.

## 4. Launcher (type 42)

### 4.1 Fields outside the block
`+0x170` i32 remaining count (−1 = endless), `+0x174` f32 interval T, `+0x178` target, `+0x17c` firing anim (−1), `+0x180` anim duration,
`+0x18c` kind (1001), `+0x190` f32 t0, `+0x194` f32 time of the last shot, `+0x198` u8 active, `+0x199` u8 aim flag. Reset `vtbl[17]` `0x452260`: anim −1, `+0x180 = 0`,
`+0x184 = +0x188 = −1`, target 0, count −1, T 0, flags 0. Ctor `0x452140`: block = values from `0x44a260`, then `0x452330(1)` → **template 1**, `L+0x160 (owner) = this`.

### 4.2 Starting and the think step
```c
void Start(L, int count, float T, Inst* target) {        /* 0x4522b0; 1000 -> (1, 1.0, target), 1003 -> (count, t*0.01, target); target -1 -> NULL (0x444d65) */
    L->target = target;  L->T = (T < 0.2f) ? 0.2f : T;  L->count = count;      /* 0x4a9760 */
    L->t0 = now + dt;  L->last = now - L->T;  L->active = 1;                   /* now = [0x509adc]+0x30, dt = +0x38 */
}
void Think(L) {                                          /* 0x452780, vtbl[3] */
    FadeThink(L);                                        /* 0x44e810 */
    if (!L->active) return;
    float t = now - L->t0;
    if (floor(t / L->T) <= floor((t - dt) / L->T)) return;                     /* 0x499ede: only on an interval boundary */
    if (now - L->last < L->T - 0.2f) return;
    L->count--;  L->last = now;  Fire(L);
    if (L->count == 0) L->active = 0;                                          /* -1 keeps counting down forever */
}
```
Result: `t0 = now + dt`, so at the next think step `t ≈ 0` → `floor` jumps from −1 to 0 → **the first shot fires immediately (1–2 frames after the message), then every
T s**, aligned to the start moment (not to the last shot). All W1A launchers start at level start and thus fire **simultaneously at t = 0, 3, 6, … s**.
Uncertain: whether the think step also runs when the launcher's sector is not visible (OBJECTS.md §2 says yes, BONUS.md §3.1 says `0x42b400` only walks visible sectors;
the `floor`-based boundary logic is in any case robust against skipped frames: at most one shot per missed period).

### 4.3 `Fire` `0x452560`
```c
Vec3 v[2];  GetVector(L, 0, v, 0);                       /* 0x42f6b0: first marker node of typecode 0, world space */
Vec3 d = (L->aim && L->target) ? L->target->pos + (0, L->T.aim_h, 0) - v[0] : v[1] - v[0];
d = normalize(d);
L->T.target = L->target;  L->T.pos = v[0];  L->T.dir0 = d;
if (L->kind != 0) Projectile_Alloc(&L->T);               /* 0x4490a0: sound + visual in 0x449130 */
else { Bomb* b = Bomb_Throw(&L->T, 0, -1, 0);            /* 0x44d5d0 */
       if (b) { b->+0x120 = L;  SoundFx(14, L); } }      /* 0x4526f6: only the bomb thrower has its own firing sound */
if (L->anim != -1 && L->anim < L->model->nanims) {       /* 0x4526fb */
    L->+0xb0 = L->anim;  L->+0xb4 = L->+0xb8 = L->+0xbc = -1;  L->+0xa8 = now;      /* animation chain of the base class (INSTANCE.md) */
    SetAnimSpeed(L, model->anim[L->anim].len /*int, +4 of the 8-byte record*/ * (1/4096.0f) / L->anim_dur);   /* 0x42e290, 0x4aa138 */
}
```
Firing position = marker start, direction = marker (or target + aim height). The animation is scaled so it lasts exactly `param 8 · 0.01` s. W1A uses no animation.

## 5. Visuals

All visuals are records (80 B) in the general effect pool `[0x5e823c]+0xdb8` (max 2000; `+0` age, `+4` lifetime (−1 = free it), `+0x4c` update function;
the same pool as BONUS.md §2.4). They draw via the sprite primitive `0x470f10(S = [0x5e823c]+0xb00, flags)` (`S+0x208` pos, `+0x214..0x21c` RGB, `+0x220` alpha,
`+0x224` rotation in 1/512 turns, `+0x228` image ref `0x1000N` = bank 0 image N, `+0x260` mode 0x12, `+0x264` size = full width like the pickups) and the
line primitive `0x471a10` (OBJECTS.md §2.1). Sprite flags: bit 2 (4) = use rotation, bit 3 (8) = non-additive; **without bit 3 → submit flag 4 = additive ONE/ONE**
(`0x4719b7`). All sprites below use flag 7 or 6 → additive. `costab[i] = cos(2π·i/512)` (`[0x5e823c][i]`), so `costab[(int)(u·128)] = cos(u·π/2)`.

### 5.1 Visual kind 2 — energy orb `0x46f8a0` (**this is what W1A uses**)

On creation, two records:
1. **Flash** `0x46f180`, 0.4 s, at the start position (see 5.2).
2. **Orb + ribbon** `0x46f2d0` (lifetime 10000 s, ends itself): `fx+0xc = P`, `fx+0x40 = P->generation`, ribbon from pool `0x5e82c8` (`0x47d380`; N = 11 points, all
   initialized at the start position `0x47d160`), `fx+0x3c = 400 / ((N−1)·speed)` = 0.04 s between ribbon points, `fx+0x10 = 400 / (speed·10)` = 0.04 s per drawn segment
   (`0x4a964c` = 400, `0x4a9750` = 10) → **10-segment ribbon of 40 = 400 units** trailing the orb (grows from the muzzle).

Per frame while `P->active && P->generation == fx+0x40` (`0x46f428`): `t += dt` (modulo 2.0 s), every 0.04 s a ribbon point `(P->pos, P->dir)` (`0x47d090`), `fx+0x24 = P->pos`. Drawing:
```c
/* head (0x46f67a), at P->pos, image 6, white */
sprite(img 6, rgb 1,1,1, alpha 1.0, size 50, rot = (int)(t * 255.5),              flags 7);   /* 0x4abc94 */
sprite(img 6, rgb 1,1,1, alpha 0.5, size 80, rot = (int)((1 - t*0.5) * 511.0),    flags 7);   /* 0x4abc90: spins the opposite way */
/* ribbon (0x46f73a): 10 segments, i = 0 at the head, u0 = i*0.1, u1 = (i+1)*0.1 */
line(p[i], p[i+1], half_width 30, tex bank 0 image 0 (ref 0x10000),
     rgba0 = (0.25*(1-u0), 0.4*(1-u0), 0.45, cos(u0*pi/2)),                        /* 0x4a9ca0, 0x4aa394, 0x3ee66666 */
     rgba1 = (0.25*(1-u1), 0.4*(1-u1), 0.45, cos(u1*pi/2)), flags 0xe00);          /* textured + own width + own colors; additive */
```
The points `p[i]` are the ribbon sampled at `i · fx+0x10` s in the past (interpolation `0x46d0d0`). For a straight shot: `p[i] = pos − dir · min(i·40, distance travelled)`.

Once the projectile is gone (or the slot is reused): once (`fx+0x44`) an **impact flash** `0x46f180` at the last position `fx+0x24` (`0x46f36d`) — no explosion, no sound —
the head stops being drawn and the ribbon shrinks: `fx+0x10 −= dt · 3.0 · 0.1` (`0x4a988c`, `0x4a974c`), i.e. from 0.04 to 0 over **0.133 s**; at `≤ 0.0001` (`0x4abc98`) release the ribbon
(`0x47d3f0`) and free the record (`0x46f87c`). This also happens when the projectile just runs out of lifetime normally: **every shot ends with a flash**.

### 5.2 Flash `0x46f180` (muzzle and impact of kind 2), 0.4 s, `u = t/0.4`
```c
size = 200 * cos(u*pi/2);                                                          /* 0x4aa164 */
sprite(img 6, white, alpha 1 - u,       size, rot = (int)(t * 256),           flags 7);   /* 0x4aa3ac */
sprite(img 4, white, alpha 0.5 - 0.5*u, size, rot = (int)((1 - t*0.5) * 512), flags 7);   /* 0x4a9874 */
```

### 5.3 Visual kind 0/1 — missile `0x4700e0` (not used by the W1A launchers; **ported**, §7.1)

Two records: muzzle flash `0x46fa40` (0.4 s: image 32, white, `size = 200·cos(u·π/2)`, `alpha = 0.5 − 0.5u`, `rot = (int)((1 − t/2)·512)`, flag 7) and the missile `0x46fb30`:
ribbon from pool `0x5e82e8` (N = 21; `fx+0x3c = 500/((N−1)·speed)`, `fx+0x10 = 500/(speed·20)` → **20 segments × 25 = 500 units**, `0x4a9998` = 500, `0x4a9994` = 20),
`fx+0x48 = Missile_Take(P)` (`0x4722f0`) and on success `smoke->state (+8) = 2`.

Per frame (alive): ribbon points as in 5.1; head: `sprite(img 4, rgb (1, 0.58, 0), alpha 1 − frac(t), size 40, rot (int)(t·255.5), flags 7)` at `P->pos + P->dir·55` (`0x4abc9c`);
ribbon: 20 segments, half-width 7, image 0, `rgb = 1 − u`, `alpha = cos(u·π/2)`, `u = i·0.05`, flags 0xe00. The model itself follows the projectile via its own think step (5.4).
End (`0x46fbca`): **explosion `0x477060(2, &last_pos, &(0,1,0))`**, `smoke->state = 0`, `Missile_Release(inst)` (`0x472370`), ribbon shrinks `dt·3·0.05` per s (0.025 → 0 over 0.167 s).

Explosion kind 2 (`0x47717a`): one record `0x4762e0`, 0.3 s, `R = 400`: nine **plane-oriented** quads (sprite flag 2 = normal taken from `S+0x230`, not billboarded) with normals
(1,0,0), (0,1,0), (0,0,1), (.7,0,.7), (−.7,0,.7), (.7,.7,0), (−.7,.7,0), (0,.7,.7), (0,−.7,.7); image 12, white, `size = R·(0.3 + 0.7·sin(u·π/2))` (120 → 400), `alpha = 0.3·cos(u·π/2)`
(`0x4abd0c` = −128, `0x4abd10` = −0.7, `0x4abd14` = −0.3, `0x4aab98` = 0.3). Kind 1 = big explosion (`0x476b50`, `0x476cd0`, `0x4762e0` with R = 1400 and 400), kind 0 = with a normal (`0x4765f0`, `0x476710`,
`0x476cd0`, shards `0x476140`): not read. The explosion does **no** damage (damage only comes from §2.5).

### 5.4 Type 41 (missile instance) and the pool (**ported**, §7.1)

- Registration: `0x403b5d` (SetTypeInstance 41) sets vtable `0x4a9360` and adds the instance to `0x5e8344[]`, total `[0x5e840c]` (max 0x32 = 50, otherwise the
  French error message); `[0x5e8410]` = number in use. Both counters at 0 in `0x404360` (`0x4043c1`) and `0x46d1f3` (clearing effects).
- Init `0x4723f0`: exhaust object (0x34 B) `inst+0x100`: `+0` inst, `+4` number of typecode-9 marker nodes (counted with `GetVector(inst, 9, ·, n)`), `+8` state
  (0 off, 1 starting up 1 s, 2 on, 3 double), `+0xc` u8 "draw me this frame", `+0x10/0x14/0x18` phases, `+0x20` smoke accumulator, `+0x24` previous marker positions, `+0x28 = 3`
  (size-table index), `+0x2c` per marker "has previous position"; linked into list `[0x5e8564]`; `inst->flags |= 0x20`.
- `Missile_Take(P)` `0x4722f0`: `if (used >= total) return NULL;` otherwise `inst = pool[used]`, `inst+0xfc = P`, position it immediately (`0x4724e0`), all `+0x2c[i] = 0`, `used++`.
  **Empty pool → no model**, the rest of the visual (ribbon, head, explosion) still runs fine (`fx+0x48 = NULL`).
- `Missile_Release(inst)` `0x472370`: find it in `pool[0..used)`, swap with `pool[used−1]`, `used−−`, `0x407850(inst)` (out of the cells = invisible).
- Think step `0x4723d0` → `0x4724e0`: `inst->pos (+0xc) = P->pos`, `inst+0x60 = P->pos`, **orientation `0x46d320(&inst->rot /*+0x28, 3 rows*/, &P->dir)`**, `0x4077f0(inst, 0)`; then `smoke->draw = 1`.
- `0x472530` (from `0x46d120`, level start, after clearing the effect pool `0x470d50`): `0x407850` on **all** registered missiles → invisible until `Take`.

Orientation `0x46d320(out, d)` (d normalized):
```c
if (|d.x| < 0.001 && |d.z| < 0.001) {            /* vertical, 0x46d375 */
    Vec3 v = normalize((0, d.z, -d.y));  row0 = cross(v, d);  row1 = v;  row2 = d;
} else {                                          /* 0x46d417 */
    Vec3 a = normalize((d.z, 0, -d.x));   row0 = a;  row1 = cross(d, a);  row2 = d;
}
```
Rows = model axes in world space (INSTANCE.md: `+0x28..0x48` 3×3 rotation in rows), so the **model's +Z axis points in the flight direction**, +X = horizontal
`(d.z, 0, −d.x)`, +Y = `d × X` (for d = (0,0,1): identity).

Exhaust `0x475440` (list `[0x5e8564]`, loop `0x46d0ab`, only if `+0xc`), per typecode-9 marker (start `m`, direction from the marker's own motion since the previous frame, otherwise the
marker vector), table `0x4abcc8` index 3 → 45 / 40 / 35 — read on the whole but not in full detail:
glow 1 `sprite(img 32, white, alpha 1 − 0.2·sin(π·f1), size 40, rot 512 − f1·512, flags 7)` (`f1 += dt·0.05`), glow 2 same but size 35, `f2 += dt·0.15`, flag 6;
flame = three quads rotated around the axis (0, 85, 170 /512 turns + `f3·512`, `f3 += dt·3`) image 31, alpha 0.8, size `45 + rand·10 − 5`, mode 0x13, flag 0x62 (own axes `S+0x23c..0x25c`);
smoke = **200 puffs/s** (`(int)(acc·200)`, `0x4aa164`; `0x4abd08` = 0.005 subtracted per puff) spread along the distance travelled, each a record `0x475380`: 0.2 s, image 14, white,
`alpha = 0.3·(1 − u)`, `size = 15·(1 + u)`, random rotation, flag 7 (additive). State 1 scales everything with `min(1, …)·6.67` ramps, state 3 ×2 (class 20/21).

### 5.5 Visual kind 3 — fireball `0x470af0` (1002 `[18, 1]`, 90× in later levels; short)
Record `0x470420` with a ribbon from pool `0x5e8310` (ribbon length 1000 units in 32 segments: `0x4aa188` = 1000, `0x4ab5b8` = 32), half-ribbon-width 70 (image 0), head two sprites image 12 (alpha 0.7, sizes 140 and 70),
spark records `0x4702b0`, and at the end `0x477060` (explosion). Sound SoundFx 20. Not read further: uncertain.

## 6. Other users (brief)
- **Shooter enemy** `0x418820` (ENEMY.md §8): block = ctor values, then `0x449070(1, ·)`; direction = muzzle vector **horizontally** normalized (y = 0); overrides damage `= Pe+0x40`,
  target, owner = enemy, aim height and vertical steering speed `= Pe+0x70`, steering factor `= Pe+0x68`, `T+0x54 = Pe+0x64`, speed `= Pe+0x60`, visual `= Pe+0x74` (set per subtype in `0x41d510`:
  e.g. `0x41d84d` = 1 → missile; which subtypes exactly: not worked out). This is where the missile model does show up.
- **Bomb** `0x44d3a0`/`0x44d4d0`: template 0 + `T+0x5c = bomb`, §2.4.
- **Class 20/21** `0x452e10`, Perso `0x463530`/`0x463c90`, class-17 family `0x40cb80`, enemies `0x411e44`, `0x413516`, `0x414c96`, `0x41677e`: copy a template; not traced further.

## 7. Recipe for the port

### 7.1 What is in `src/main_engine.c`

Ported: the launcher (§4), the projectile (§2, without gravity/bouncing/seeking except for xz steering), the energy orb of kind 2 (§5.1-5.2) **and the missile
of kind 0/1 (§5.3-5.4)**. The missile side consists of:

| original | port |
|---|---|
| `0x403b5d` (SetTypeInstance 41) + `0x4723f0` | `missile_add`: every type-41 instance goes into `g_missiles[50]`, invisible, with the number of typecode-9 nozzles alongside it |
| `0x4722f0` / `0x472370` | `missile_take` / `missile_release`. Deviation: the original swaps the taken entry to the front of the pool, the port leaves the records in place and sets a flag, because the projectile holds a pointer to it |
| `0x4724e0` + `0x46d320` | `missile_place`: position = the projectile point, rotation = the rows (X, Y, Z) as a quaternion, so **model +Z = flight direction** |
| `0x46fb30` ribbon + head | `launchers_draw`: 20 segments of 25 (half-width 7, image 0, `rgb = 1 − u`, `alpha = cos(u·π/2)`), head image 4 in (1, 0.58, 0) at `pos + dir·55` |
| `0x46fa40` muzzle flash | `flash_add(pos, 1)`: image 32, `size = 200·cos(u·π/2)`, `alpha = 0.5 − 0.5u`, 0.4 s |
| `0x475440` exhaust | `missile_exhaust`: per nozzle two glows (image 32, 40 and 35) and a flame (image 31, `45 + rand·10 − 5`), plus `exhaust_smoke`: 200 puffs/s along the distance travelled. State always 2 (on): no startup ramp, that belongs to class 20/21 |
| `0x477060(2, …)` → `0x4762e0` | `blast_add(pos, 400)`; `fx_smoke_draw` draws the nine flat quads with `hud_world_fx_plane` |
| ribbon shrinks `dt·3·0.05` | `shot_fade_len` = 0.167 s (the orb: 0.133 s) |

Not yet ported: fireball kind 3 (§5.5, drawn like the orb), explosion kinds 0/1 beyond their two flashes, the bomb thrower (template 0), bouncing,
gravity and target-seeking via `T+0x38`. The ribbon trails straight behind the current direction instead of along the actually flown path, so for the only
target-seeking shooter (type 7, `T+0x40 = 0.2`) it drags along instead of curving.

### 7.2 Original recipe (kind 2)

Goal: W1A launchers 26/27/28/194/195 (endless, every 3 s, straight along the marker, 1.4/1.5 s). Everything fits in `src/main_engine.c` next to the `Laser` array.

**A. Data**
```c
typedef struct { float radius, gravity, speed, damp_ground, damp_air, life, damage; int max_bounce; float aim_h; int visual, hits_all; } ProjT;   /* leave out target-seeking until a level needs it */
static const ProjT PROJ_KIND1 = { 5, 0, 1000, 1, 1, 15, 1, 0, 125, 2, 1 };
typedef struct { Instance *inst, *target; ProjT t; int kind, count, active, aim, anim; float T, t0, last, anim_dur; } Launcher;   static Launcher g_launchers[32];
typedef struct { int active; ProjT t; Vec3 pos, dir, start; float age, dead_t; const Instance *owner; } Proj;                       static Proj g_projs[200];
```
**B. Messages** (`case 1200`: type 42 → `Launcher` with `t = PROJ_KIND1`, `kind = 1`, `count = −1`, `anim = −1`; on level change `g_nlaunchers = 0` and all projectiles off):
- `1001 [inst, k]`: reset (count −1, T 0, target 0, aim 0, anim −1, active 0) + template k (k = 0: bomb thrower → nothing fired for now).
- `1002 [inst, n, v]`: table §3; at minimum n = 2 → `t.life = v·0.01`, 0 → speed, 1 → gravity, 4 → damage, 9 → radius, 18 → visual via {2,3,4,0}, 19 → aim, 7/8 → anim.
- `1003 [inst, target|−1, count, t]`: `T = max(t·0.01, 0.2)`, `t0 = now + dt`, `last = now − T`, `active = 1`. `1000` = `(1, 1.0, target)`. `1004`: `active = 0`.
- Type 41 at `1200`: make the instance invisible (`visible = 0`, `0x472530`); nothing else needed for W1A.

**C. Per frame, after the VM tick, not during pause/cinematic freeze** (original order: instance think steps → … → VM → projectiles):
1. Launchers: `Think` from §4.2, literally (floor boundary + `T − 0.2`). `Fire`: `inst_vector(inst, 0, &p0, &dir)` (already exists for the lasers), find a free slot (none → skip),
   `pos = start = p0`, `dir`, `age = 0`, `owner = inst`; `audio_fx(19, inst, &inst->position.x)` (visual 2; 17/18 for 0/1, 20 for 3); flash (E) at `p0`.
2. Projectiles: `age += dt; if (age >= life) dead;` `old = pos; pos += dir·speed·dt` (+ gravity/damping from §2.2 if `gravity != 0`);
   test against the player first: segment `old→pos` against the player cylinder with radius **69 + 5** and y range `[feet − 5, feet + 193 + 5]` (reuse the segment-cylinder code of `laser_hits_player`
   with a different radius; not ×0.85) → `if (player_hit(p, t.damage, dir)) player_kill(p, 3);` dead. Don't test if the player is dead (`dead_kind`) — actor list 1 does not contain him then.
   Then the world: `f = gel_ray_frac(gel, old, pos); if (f <= 1) { pos = old + (pos−old)·f; dead; }` (max_bounce 0; instance hulls once the port has a ray test for those).
   Enemies are also hit thanks to `hits_all = 1` (`vtbl[39]`), optionally via the existing enemy damage function in `src/enemy.c`.
3. "dead" = `active = 0`, `dead_t = 0`, impact flash (E) at `pos`; let the ribbon keep shrinking for another 0.133 s.

**D. Drawing** (after the 3D scene, next to the lasers; additive, depth test on, depth write off):
- extend `hud_world_beam` with an image parameter (currently fixed to bank 0 image 1; the ribbon uses **image 0**) and per-end RGB, or a variant `hud_world_beam_img`.
  Ribbon: `L = min(400, |pos − start|)` (after death `L ·= max(0, 1 − dead_t/0.133)`), 10 segments of `L/10` from the head backwards, half-width 30,
  color per node `(0.25(1−u), 0.4(1−u), 0.45) · cos(u·π/2)`, `u = i/10`.
- extend `hud_world_sprite` into a general additive, rotated sprite `(bank 0 image N, pos, size, rgba, rot/512 turns)` (currently only the 5 pickup images, +50 height and alpha blend).
  Head (only while alive): image 6, size 50, alpha 1, `rot = t·255.5`; image 6, size 80, alpha 0.5, `rot = (1 − t/2)·511` (`t` = age modulo 2 s).
**E. Flash** (small array of `{pos, t}`), 0.4 s: `size = 200·cos(u·π/2)`; image 6 alpha `1 − u` rot `t·256`; image 4 alpha `0.5 − 0.5u` rot `(1 − t/2)·512`.

**F. Test**: W1A, launcher 26 at (1067, 1114, 2648) fires towards −x; every 3 s a blue-white orb with a ribbon that flies 1400 units (until x ≈ −333 or the first wall) and dies with a flash;
27 at (−283, 1120, 2915) towards +x (1500 units), 28 at (1065, 1115, 3254) towards −x; all three at once. Walking into one costs 1 heart and pushes the player along with the shot.

## 8. Open questions
1. `L+0x184` (copy of parameter 2) and `L+0x188` (parameter 6): no reader found.
2. Does `Think` also run for launchers in non-visible sectors (§4.2)?
3. Does the ray `0x4359b0` hit the launcher's own hulls (§2.3)? No exclusion in the code for projectiles without a carried instance.
4. Exact meaning of sprite-flag bits 1 and 2 and mode 0x12/0x13 of `0x470f10`; color scale of the line primitive (0.5 = neutral at the laser default: is 0.45 here "almost full"?).
5. Visual kind 3 (`0x470420`), explosion kinds 0/1, and which enemy subtypes get `Pe+0x74 = 0/1` (missile): not worked out.
6. `P+0xf0..0x100` (bounce plane): no reader found.
