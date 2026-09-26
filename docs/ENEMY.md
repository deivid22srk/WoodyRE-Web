# Enemy classes (types 4..13) – Woody.exe

Status: working document, updated incrementally. All addresses are VAs in `game/Woody.exe`
(image base 0x400000). Floats were read from the exe with a PE parser. `0x462c60` = empty log function,
`0x4995b2` = `operator new`, `0x4995a7` = `operator delete`, `0x499ed5` = `_purecall`.

## 1. Class hierarchy

```
Instance   ctor 0x42e1a0  vtable 0x4aa31c  (28 slots)   handler 0x42d5e0
  └ Npc    ctor 0x40be10  vtable 0x4a9530  (45 slots)   handler 0x40bf20 → 0x44e8f0
      │      (second derived class: Perso, ctor 0x44a2d0, vtable 0x4aabc0)
      └ Enemy (abstract) ctor 0x419cd0  vtable 0x4a9fbc (58 slots; [45] and [51] = _purecall) handler 0x41a740
          ├ type 4,5,6   ctor 0x4189f0(subtype 1,2,3)  vtable 0x4a9ec0  size 0x1fc
          ├ type 7,8,9   ctor 0x416ca0                 vtable 0x4a9dc0  size 0x20c
          ├ type 10      ctor 0x415190                 vtable 0x4a9cb8  size 0x1ec
          ├ type 11      ctor 0x4120e0                 vtable 0x4a9ab0  size 0x200
          ├ type 12      ctor 0x410e90                 vtable 0x4a99b0  size 0x204
          ├ type 13      ctor 0x413780                 vtable 0x4a9bb0  size 0x1f8
          └ (three more: ctors 0x40c730, 0x40d850, 0x40eb50 – bosses, outside this document)
```

* `Npc::Npc` (`0x40be10`): `+0x104 = 0` (type word), `+0x108 = index` in the global npc table
  `0x4c4e00[0x4c5318]` (max 0x100, "Too many npc. Max is %d"). Dtor `0x40bec0` removes it (swap with the last one).
* Type word `+0x104` (via `vtbl[4]` = `0x403fe0` = `return this+0x104`): bits 0..4 = category
  (`0x40c360(cat)`, reader `0x40c340`), bits 5..9 = **subtype** (`0x40c380(n)`, reader `0x40c350`),
  bit 0x400 = attackable target.
* `Enemy::Enemy` (`0x419cd0`): zeroes `+0x110, +0x118, +0x120, +0x124, +0x160, +0x164, +0x168, +0x16c`;
  `+0x170 = 1.0f`; `+0x174 &= ~2`.
* Type 4/5/6 ctor `0x4189f0(subtype)`: `Enemy::Enemy`, vtable, `0x40c360(2)` (category 2 = enemy),
  `0x40c380(subtype)`, `+0x1c4 = 0`. Called from the class factory `0x403502`: type 4 → subtype **1**
  (`0x403649`), type 5 → **2** (`0x403682`), type 6 → **3** (`0x4036bb`).

### 1.1 Vtable slots (Enemy base `0x4a9fbc` / type 4-6 `0x4a9ec0` / type 7-9 `0x4a9dc0`)

Slots 0..27 = Instance, 28..44 = Npc, 45..57 = Enemy. Only the relevant ones:

| slot | off | base | type 4-6 | type 7-9 | meaning |
|---|---|---|---|---|---|
| 1 | +0x04 | `0x419e30` | `0x418ae0` | `0x416d90` | **PostLoad**: creates helper objects (§2.2) |
| 3 | +0x0c | `0x41a320` | = | = | **per-frame Think** (§3.1) |
| 4 | +0x10 | `0x403fe0` | = | = | `&typeword` (`this+0x104`) |
| 17 | +0x44 | `0x41a010` | `0x418c20` | `0x416ed0` | **Reset** (back to start position, full hp, initial state) |
| 22 | +0x58 | `0x41a740` | `0x414530`→`0x41a740` | same | message handler (§6) |
| 29 | +0x74 | `0x41aff0` | = | = | "removed": `[0x4c532c]++` (killed-enemies counter) and `jmp 0x407850`; called by `0x40bf60` when `+0x10c` bit 0 is set |
| 31 | +0x7c | `0x40c1e0` | = | = (type 12: `0x411840`) | `Touch(cat, subtype)`: first actor from `0x4c5258[0x4c5324]` (≠ this) with that category/subtype (0 = any) and 3D distance `< radius_this + radius_other` |
| 32 | +0x80 | `0x41ad20` | = | = | radius = `P+0x04` |
| 33 | +0x84 | `0x41ad30` | = | = | height = `P+0x28` |
| 34 | +0x88 | `0x41ace0` | = | = | position pointer |
| 37 | +0x94 | `0x40c3b0` (`ret 4`, no-op) | = | `0x417ee0` | "player attacking" warning |
| 38 | +0x98 | `0x40c3b0` | `0x4078a0` (no-op) | `0x4078a0` | `Kill(kind)` – **empty** for enemies |
| 39 | +0x9c | `0x41adc0` | `0x419480` | `0x417f60` | **TakeDamage(attacker, damage, &dir, &point, kind) → bool dead** (§5) |
| 40 | +0xa0 | `0x41ae20` | = | = | `Blast(&pos, r)`: if 3D distance² < r² ⇒ `vtbl[39](0, hp, &normalize_xz(this−pos), 0, 0)` (always kills) |
| 43 | +0xac | `0x41a4e0` | = | = | follow ground (§3.3) |
| 44 | +0xb0 | `0x41a680` | = | = | rotation matrix from look angle (§3.4) |
| 45 | +0xb4 | purecall | `0x4194f0` | `0x418000` | animation choice per state (§4.3) |
| 46 | +0xb8 | `0x45bd30` | = | = | `return 3` (mode for obstacle sensor `+0x124`) |
| 47 | +0xbc | `0x41a4d0` → `0x40c0b0` | `0x419460` | `0x417f20` | RegisterActor2 (type 4: only if state 0..11, i.e. not dead) |
| 48 | +0xc0 | `0x41af80` | = | = | `FindTarget(cat, subtype)` (§3.2) |
| 49 | +0xc4 | `0x462c60` (empty) | = | = | |
| 51 | +0xcc | purecall | `0x4149e0` | `0x4149e0` | duration of the death animation (float) |
| 52 | +0xd0 | `0x41a3e0` | `0x418cf0` | `0x416fb0` | **Update / state machine** (§4) |
| 53 | +0xd4 | `0x40d840` | `0x419980` | `0x4184f0` | duration of hit animation (arg: int) |
| 55 | +0xdc | `0x41b1b0` | = | = | post-movement (update cell) |
| 56 | +0xe0 | `0x41b030` | = | = | move with collision (called by behaviors) |
| 57 | +0xe4 | `0x41b000` | = | = | **death effect** (5 particles): `0x477610(rand()&1 ? 0 : 1, this)` – **no bonus** (see §4.2) |
| 58 | +0xe8 | – | – | `0x418820` | (type 7-9 only) fire projectile `(target, &muzzlepoint)` |

Right after each class vtable is the vtable of the associated animation controller (5 slots,
`[0]`=record getter, `[1]`=`0x40d7f0`, `[2]`=`0x436b70` Request, `[3]`=`0x436a50` Tick, `[4]`=`0x436a40` Reset;
type 4-6: `0x4a9fa8` with `[0] = 0x419cb0`).

## 2. Struct fields

### 2.1 Enemy (base; `this`)

| off | type | default | meaning | source |
|---|---|---|---|---|
| 0x04 | u32 | | VM id of the instance (for msgmask `0x443e50/0x443e90`) | `0x41a63e` |
| 0x0c | vec3 | | position (feet) | |
| 0x28 | mat3 | | rotation (rows); set every frame by `0x41a680` | |
| 0x60 | vec3 | | collision center = pos + (0, height·0.5, 0) | `0x41a60x` |
| 0x6c | float | 0 | fade/transparency 0..1 (FadeInst; ramps up during the second half of dying, §4.2) | `0x41a47d` |
| 0x78 | ptr | | TRAJ path from the .ins (0 = none) | `0x418b2a` |
| 0x104 | u32 | | type word (cat 2, subtype, 0x400) | |
| 0x108 | int | | index in npc table `0x4c4e00` | `0x40be49` |
| 0x10c | u8 | | bit 0 = "remove me" (→ `vtbl[29]` in `0x40bf60`) | `0x419391` |
| 0x110 | Fall* (0xc) | | fall object `{t, g = P+0x00, v}` (`0x4402f0`) | `0x419ebb` |
| 0x114 | float | 0 | dt of this frame (copy of `World+0x38`) | `0x41a34c` |
| 0x118 | Params* (0xc4) | | parameter block **P** (§2.3) | `0x419e89` |
| 0x11c | Behav* | | **active behavior** (one of +0x160..0x16c) | |
| 0x120 | Heading* (0x2c) | | angle/speed controller **H** (§3.4) | `0x419f38` |
| 0x124 | Sensor* (0xb8) | | obstacle sensor, 16 directions (`0x41cfc0`, tick `0x41d4a0`) | `0x419f6c` |
| 0x128 | vec3 | pos at PostLoad | **start position** (Reset sets pos to this) | `0x419f74` |
| 0x134 | vec3 | = startpos | **home point** (center for wandering; type 4 sets it to the spot where it left the path) | `0x419f98` |
| 0x140 | vec3 | 0 | (`0x43ffa0`) | |
| 0x14c | float | 0 | | `0x41a0c3` |
| 0x150 | **float** | `P+0x34` | **hit points** | `0x41a0bd` |
| 0x154 | float | 0 | | |
| 0x158 | float | 0 | hit timer (> 0 = invulnerable, "hit" state) | `0x4194c5` |
| 0x15c | float | 0 | death timer (ramps up in dead state) | `0x419378` |
| 0x160 | Behav* | | behavior **Wander** (`0x41bf30`, 0x6c bytes) | |
| 0x164 | Behav* | | behavior **Chase** (`0x41bbf0`, 0x3c) | |
| 0x168 | Behav* | | behavior **Stand still** (`0x41b710`, 0x3c) | |
| 0x16c | Behav* | | behavior **Follow path** (`0x41c6a0`, 0x60; only if `+0x78 ≠ 0`) | |
| 0x170 | float | 1.0 | | `0x419d16` |
| 0x174 | u8 | | flags: **1** = on the ground, **2** = "guard home point" (FindTarget measures from `+0x134` instead of own pos), **4** = ground-follow on (Reset), **8** = behavior tick without sensor (§4.2), **0x10** = no gravity, **0x20** = set in PostLoad | |
| 0x178 | Probe (0x24) | | ground probe for `0x436dc0` (ctor `0x436cf0`, reset `0x436d10`) | |
| 0x19c | Probe (0x24) | | second probe (used by `0x41b030`) | |

### 2.2 Type 4/5/6 (size 0x1fc)

| off | type | meaning |
|---|---|---|
| 0x1c0 | int | **state** 0..12 (§4.1) |
| 0x1c4 | AnimCtrl* (0x54, ctor `0x419c90`) | animation controller (same base class as the Perso's, `0x436b70/0x436a50/0x436a40`) |
| 0x1c8 | float | cooldown timer after an attack; while ≥ 0 the player is **not** re-noticed (starts at `P+0x38`) |
| 0x1cc | float | "attack missed" timer (state 3) = AnimLen(10) |
| 0x1d0 | float | state 11 timer = AnimLen(11) |
| 0x1d4 | float | remaining attack duration (state 1 computes it, state 4 counts it down) |
| 0x1d8 | float | state 6 timer = AnimLen(9) |
| 0x1dc, 0x1e0 | | 0 on Reset |
| 0x1e4 | float | `AnimLen(4, 1)` (Reset `0x418cd9`) |

PostLoad `0x418ae0`: `Enemy::PostLoad`; `+0x1c4 = new AnimCtrl(this)`; if `+0x78`: `+0x16c = new Pad(this, traj)`;
`+0x160 = new Wander(this, 1, &this+0x134)`; `+0x164 = new Chase(this, 0)`; `+0x168 = new Stand(this)`;
`[0x4c5330]++` (total number of enemies in the level).

Reset `0x418c20`: `Enemy::Reset`; with TRAJ ⇒ state **0**, behavior = Pad (`vtbl[6]()` init + `0x41c760`), `P+0x1c = 10.0`;
without TRAJ ⇒ state **8**, behavior = Wander (`vtbl[6]()` + `0x41c160`). All timers 0.

### 2.3 Parameter block P (`+0x118`, ctor `0x41d510(enemy)`, defaults thereafter per subtype via table `0x41dc38`)

| off | default | sub 1 (type 4) | sub 2 (type 5) | sub 3 (type 6) | sub 4 (type 7) | meaning (proven use) |
|---|---|---|---|---|---|---|
| 0x00 | 200 | | | | | fall acceleration g (Fall object) |
| 0x04 | 50 | **30** | 30 | 30 | 30 | **radius** (vtbl[32]); also distance of the hit point in front of the enemy |
| 0x08 | 200 | 200 | 200 | 200 | 200 | **walk speed** (wander/patrol) |
| 0x0c | 600 | 600 | 600 | 600 | 600 | (run speed? not yet seen in type 4) |
| 0x10 | π/2 | | | | π/2 | **turn speed** rad/s (H+8) |
| 0x14 | 2π | | | | 4π | fast turn speed |
| 0x18 | π | | | | π | = 2·P+0x10 |
| 0x1c | 800 | (Reset with path: 10) | | | | |
| 0x20 | 800 | **1500** | 1500 | 1500 | 1500 | **sight distance** (3D) FindTarget |
| 0x24 | 600 | | | | 800 | max. height difference |dy| FindTarget |
| 0x28 | 140 | 140 | 140 | 140 | 140 | **height** (vtbl[33]) |
| 0x34 | 1.0 | **1** | **1** | **2** | 1 | **hit points** |
| 0x38 | 3.0 | **1.5** | **1.0** | **0.5** | 1.5 | **cooldown time** (s) after an attack |
| 0x3c | 1.0 | 1 | 1 | 1 | 1 | **damage to the player** |
| 0x54 | 400 | 400 | 400 | 400 | 150 | |
| 0x5c | 1000 | **800** | 800 | 800 | 800 | **charge run speed**; attack range = `P+0x5c · T` |
| 0xc0 | 3000 | | | | | **activation distance to the camera** |

Other defaults: `+0x2c=+0x30=10, +0x40=1, +0x44=600, +0x48=500, +0x4c=3, +0x50=800, +0x58=300, +0x60=1000, +0x64=1,
+0x68=0.5, +0x6c=100, +0x70=0, +0x84=+0x94=7, +0x88=100, +0x8c=300, +0x90=0.8, +0x9c..0xa4=1, +0xa8=+0xac=10, +0xb0=1,
+0xb4=3, +0xb8=500, +0xbc=3 (int)`. Subtype 5 (type 8): hp 2, cooldown 1.0, height 130; subtype 6 (type 9): hp 3, damage 3;
subtype 7: radius 50, sight 2500, height 180, hp 2, damage 2; subtype 8: radius 60, hp 5, damage 3; subtype 9: hp 3, damage 2;
subtype 10: radius 80, hp 2; subtype 11: hp 5; subtype 12: hp 6, damage 4; subtype 13: hp 10, damage 4.

## 3. Per-frame basics

### 3.1 Think `vtbl[3]` = `0x41a320`

```c
void Enemy_Think(Enemy *e) {
    if (!g_active /*[0x4b178c]*/) return;
    Npc_Tick(e);                          /* 0x44e810 */
    if (g_cinematic /*[0x5d7b8d]*/) return;
    e->dt = World->dt;                    /* +0x114 = [0x509adc]+0x38 */
    e->vtbl[47](e);                       /* RegisterActor2 (0x4c4d80, max 32) */
    vec3 d = e->pos - cam->pos;           /* cam = 0x41fa30([0x4c737c]) + 0x90 */
    if (dot(d,d) < P->activeDist * P->activeDist /*3000²*/ || e->hp <= 0)
        e->vtbl[52](e);                   /* Update */
}
```
So enemies further than **3000** from the camera stand still (unless they're already dead: the death animation still plays out).

### 3.2 FindTarget `vtbl[48](cat, subtype)` = `0x41af80` → `0x40c0d0`
Center `c` = own position (`vtbl[34]`), or `+0x134` if flag 2. Iterates list 1 (`0x4c52d8[0x4c531c]`, pairs of
8 bytes – the Perso is in there): category must be `cat` (and subtype if ≠ 0); picks the **closest** with 3D distance
`< P+0x20` (1500) and `|dy| < P+0x24` (600). No sightline or angle test. All calls in type 4 are `(1, 0)` = the player.

### 3.3 Follow ground `vtbl[43]` = `0x41a4e0` (only if flag 4)
```c
h2 = P->height * 0.5f;  p = pos + (0,h2,0);
cell = World_FindCell([0x50944c], &p);                    /* 0x428ce0 */
hit  = Probe_Test(&e->probe /*+0x178*/, cell, &p, h2, e->id);   /* 0x436dc0 → press/in/unpress events */
flags.onGround = (hit != 0);   groundY = [0x53a568];
if (!(flags & 0x10)) {                                    /* gravity */
    if (onGround) { fall.t = 0; fall.v = 0; }             /* 0x440330 */
    else { fall.t += dt; fall.v += dt * fall.g /*200*/ - fall.v * 0.2f /*0x4a9760*/; }
    if (fall.v > 0) { pos.y -= fall.v; moved = 1; }       /* NB: v is units per FRAME */
}
if (pos.y < groundY) { pos.y = groundY; flags |= 1; moved = 1; }
if (moved) { e->colCenter = pos + (0,h2,0); Instance_SetCell(e, &colCenter); /*0x4077f0*/ }
msgmask(e->id, 0x200) = onGround ? set (0x443e50) : clear (0x443e90);
```
The terminal velocity of the fall is `dt·g/0.2` per frame (at 60 fps ≈ 16.7 units/frame ≈ 1000 units/s).

### 3.4 Angle controller H (`+0x120`, ctor `0x41b7f0(angle0, turnspeed, 0, 1000.0)`)
`+0x00` movement angle, `+0x04` target angle, `+0x08` turn speed (rad/s, = `P+0x10` = **π/2**), `+0x0c` current angular
speed (0 = done), `+0x10/+0x14/+0x18` same for the **look angle** (the model), `+0x1c` start angle, `+0x20` current speed,
`+0x24` target speed, `+0x28` acceleration (**1000 units/s²**).
* `0x41b980(angle, snap)`: snap ⇒ both angles = angle; otherwise target = angle and speed = `0x440180(cur, target)` (±1 along the
  shortest path) × turn speed.
* `0x41b9d0(v, direct)`: target speed = v (and current too if `direct`).
* Tick `0x41ba90(dt)`: per channel `step = dt·ω`; if `|step| ≥` angle difference (`0x4401c0`, shortest arc) ⇒ angle = target; otherwise
  `angle += step`, wrap to [0, 2π]. Speed moves linearly toward the target speed at 1000/s².
* Angle from a to b `0x440210(a, b)`: `2π − (atan2(b.x−a.x, b.z−a.z) + π + π/2)`, normalized to [0, 2π).
  Direction vector at angle: `(cos h, 0, sin h)` (`0x41b860` movement angle, `0x41b8b0` look angle).
* `vtbl[44]` = `0x41a680`: `f = −(cos k, 0, sin k)` (k = look angle `H+0x10`); `up = (0,1,0)`; matrix rows:
  `+0x28..0x30 = f × up` (`0x41af10`), `+0x34..0x3c = f`, `+0x40..0x48 = up`. So the enemy looks along **−row(+0x34)**;
  PostLoad also derives the initial angle from that: `angle0 = 0x440210(pos, pos − row(+0x34))` (`0x419ec1`).

## 4. State machine type 4/5/6 (`vtbl[52]` = `0x418cf0`, jump table `0x419420`)

### 4.1 States

| # | code | name | behavior (`+0x11c`) | what happens / transition |
|---|---|---|---|---|
| 0 | `0x418e69` | **PATROL** (path) | Pad | if `cool < 0` and `FindTarget(1,0)` ⇒ home point `+0x134` = own position, `Wander.0x41c140(1, &home)`, → **2** |
| 1 | `0x418f23` | **CHASE** | Chase | no target ⇒ **7**. `T = AnimLen(8) + 0.5·AnimLen(10)` → `+0x1d4`; if xz distance ≤ `P+0x5c·T` (800·T) **and** `dot(movement direction, normalize(target−pos)) > 0.95` (`0x4a9c9c`) ⇒ speed := 800 (direct), → **4** |
| 2 | `0x418ee0` | NOTICED | | target ⇒ behavior = Chase (`vtbl[6]()`, `0x41bc80(target)`), → **1** (and immediately the code of 1); no target ⇒ **7** |
| 3 | `0x419245` | ATTACK MISSED | Stand still | `+0x1cc -= dt`; ≤ 0 ⇒ `cool = P+0x38`, → **7** |
| 4 | `0x4190af` | **CHARGE RUN** | Chase | no target or `+0x1d4 ≤ 0` ⇒ **5**; `+0x1d4 -= dt`; if `Touch(1,0)` (distance < 30 + player radius): `hit = player->vtbl[39](this, P+0x3c /*1.0*/, &dir, &point, 0)` with `dir = normalize_xz(player−pos)`, `point = pos + dir·P+4 + (0, h/2, 0)`; `hit` (player dead) ⇒ **10**, otherwise `+0x1cc = AnimLen(10)`, behavior = Stand still, → **3** |
| 5 | `0x41927c` | START SLOWING | Stand still | `+0x1d8 = AnimLen(9)`, → **6** |
| 6 | `0x4192a7` | SLOWING | Stand still | `+0x1d8 < 0` ⇒ `cool = P+0x38`, → **7**; otherwise `-= dt` |
| 7 | `0x418d77` | TO WANDER | → Wander | speed := `P+0x08` (200, direct); behavior = Wander (`vtbl[6]()`, `0x41c160`), → **8** |
| 8 | `0x418dc6` | **WANDER** | Wander | if `cool < 0` and target ⇒ **2**. With TRAJ: xz distance to home point `< 10` (`0x4a9750`) ⇒ behavior = Pad (`0x41cfb0`), → **0** |
| 9 | `0x4193b8` | **HIT** | (unchanged) | `hp ≤ 0` ⇒ `vtbl[57]()` (bonus), → **12**; otherwise if hit timer `+0x158 ≤ 0` ⇒ **7** |
| 10 | `0x4192f2` | START PLAYER DEFEATED | Stand still | `+0x1d0 = AnimLen(11)`; speed := 200; → **11** |
| 11 | `0x419334` | PLAYER DEFEATED | Stand still | `+0x1d0 -= dt`; ≤ 0 ⇒ **7** |
| 12 | `0x41935c` | **DEAD** | Stand still | `+0x15c += dt`; if `vtbl[51]()` (death animation duration) ≤ `+0x15c` ⇒ `+0x10c \|= 1` (remove); type word `&= ~0x400` (no longer attackable) |

Before the switch: `Enemy::Update` (§4.2); `cool (+0x1c8)`: if ≥ 0 ⇒ `-= dt`; `+0x158` (hit timer): if > 0 ⇒ `-= dt`.
Note: there is **no look-angle/sight cone**; "seeing" = within 1500 (3D) and |dy| < 600. The 0.95 test (≈ 18°) only determines
when the charge run starts.

### 4.2 `Enemy::Update` = `0x41a3e0`
```c
if (!(flags & 8)) {
    k = behav->vtbl[7]();                                 /* kind of behavior */
    if (k != 3 && k != 5) Sensor_Tick(e->sensor, e->vtbl[46]() /*3*/);   /* 0x41d4a0 */
}
behav->vtbl[4]();                                          /* behavior tick: drives H and moves */
if (!(flags & 8)) e->vtbl[49]();                           /* empty */
half = e->vtbl[51]() * 0.5f;                               /* death animation: second half slows down */
if (e->deathT /*+0x15c*/ > half) {
    e->fade /*+0x6c*/ = (e->deathT - half) / (e->vtbl[51]() - half);  if (> 1.0f) = 1.0f;   /* fade out */
}
e->vtbl[43]();  /* ground */   e->vtbl[44]();  /* rotation */
e->vtbl[45]();  /* animation */ e->vtbl[55]();  /* cell */
```

**Corrections to §1.1/§2.1:** `inst+0x6c` is not an animation speed but the **fade/transparency** of the FadeInst layer
(`0x44e810`: ramps at `+0x100` units/s toward target `+0xfc`; between 0 and 0.9 (`0x4a94b8`) render flag 0x40 is set) – the enemy
**thus fades linearly from 0 to 1 during the second half of the death duration**. And `vtbl[57]` = `0x41b000` drops **no bonus**:
`0x477610(rand()&1 ? 0 : 1, this)` creates 5 particles (lifetime 2.5, callback `0x477350`, sprite `0x1000b` or `0x1000a`)
that hang on the enemy = death effect (sparkles/feathers). No bonus drop was found in classes 4..13.

### 4.3 Animations type 4/5/6 (`vtbl[45]` = `0x4194f0`, table `0x41963c`; afterward always `AnimCtrl->Tick(dt)`)

Logical animations = records of 0x1c bytes in `0x4b29b8` (`vtbl[0]` of the AnimCtrl = `0x419cb0`), format like the Perso's
`{int sub[4]; int prio; float speed; u8 restart}`; all prio 1000 except 13 (2000):

| n | sub[] | speed | used for |
|---|---|---|---|
| 1 / 2 | 16 / 17 | 3 | turning toward the player (sign of `H+0x0c`: > 0 ⇒ 1, otherwise 2) – state 1/2 while Chase hasn't started moving yet |
| 3 | 1,2,2,2 | 3 | **running** (chase, `Chase+0x30 == 1`) |
| 4 / 5 | 3,4,5,0 | 3 | walking (wander action 6 / 7) |
| 6 / 7 | 4,4,5,0 / 4 | 3 | wander action 9 (walking home) / 10 (turning toward home) |
| 8 | 13 | **10** | **charge run** (state 4) |
| 9 | 15 | 2 | slowing down/braking (state 5/6) |
| 10 | 14 | 3 | attack missed / bite (state 3) |
| 11 | 18 | 1.5 | player defeated (state 10/11) |
| 12 | 11 | 4 | **hit** (state 9) |
| 13 | 12 | 3 | **dead** (state 12); `vtbl[51]()` = `AnimLen(13) + 1.0` s |
| 14..18 | 6..10 | 3 | idle variations (wander action 0..4) |
| 19/20/21 | 3,4,4,4 / 4 / 5 | 3 | walking the path: `0x419770` picks 0x15/0x13/0x14 depending on the path speed (blends with `0x436bd0`) |
| 22 / 23 | 16 / 17 | 3 | turning in place (wander action 5; path substate 1 and 7) |

`AnimLen(n,k)` = `0x436b90` (see PERSO_JUMP §4). Duration "hit" `vtbl[53]` in state 9 = constant **0.25 s** (`0x4a9ca0`).
`vtbl[53](action)` otherwise returns, per state, the length of the running animation (table `0x419c18`; in state 7/8 per
wander action, table `0x419c4c`: action 6 ⇒ sum of AnimLen(4,0..2), action 7 ⇒ sum AnimLen(5,0..2), 0..4 ⇒ AnimLen(14..18)).

## 5. Behaviors (Behav; base ctor `0x41b250`, vtable `0x4aa0b0`)

Fields: `+4` enemy, `+8` H (`enemy+0x120`), `+0x10` vec3 knockback direction, `+0x1c` knockback timer, `+0x20` vec3
platform displacement. Vtable: `[0]` step length this frame (float), `[1]` direction (vec3 out), `[2]` hook after the sweep,
`[3]` OnBlocked, `[4]` **Tick**, `[6]` Init, `[7]` kind (Wander 0, Chase 1, Pad 3, Stand still 5).

### 5.1 Common movement `0x41b2c0` (= Tick of Stand still; called by all Ticks)
```c
from = pos;  h2 = P->height*0.5;  stepUp = min(P+0x30 /*10*/, h2);  maxDrop = P+0x2c /*10*/;
if (b->knockT <= 0)  to = from + b->vtbl[1]() * b->vtbl[0]();            /* normal step */
else { b->knockT = max(0, b->knockT - dt);
       to = from + b->knockDir * (dt * P+0x44 /*600*/ * b->knockT); }     /* knockback: v = 600·t_remaining */
Probe_GetPlatformDelta(&e->probe, &b->platDelta);  to += platDelta;       /* 0x436d20: move along with platform */
[0x4b3118] = P->radius;  Probe2_PushOut(&e->probe2, &from+h2, &push, -1); to.xz += push.xz;   /* 0x437040 */
Sweep(&res, &from, &to, stepUp, 30.0f);                                   /* 0x437580, sets [0x4b310c], [0x53a568] = ground height */
b->vtbl[2](&res, &from, ...);
if (subtype >= 9) res.y = from.y;                                         /* flying types */
if ([0x4b310c] >= 0.8f /*0x4a987c*/ && res.y - groundY < maxDrop) {      /* clear path and no drop > 10 */
    if (res.y - groundY < 1.0f && [0x53a554] == 2) Probe_Attach(...);     /* 0x436d80 */
    pos = res;  colCenter = res + (0,h2,0);  Instance_SetCell(e, &colCenter);
} else {                                                                  /* blocked: platform delta only */
    pos += platDelta; ...; b->vtbl[3]();                                  /* OnBlocked */
}
```
So enemies **don't walk off edges** higher than `P+0x2c` = 10 (subtypes 9/10: 10000) and step up at most 10.

### 5.2 Stand still (`0x41b710`, vtable `0x4aa0d0`): step = `dt · (+0x38 = 0)`, direction `+0x2c` = 0 ⇒ knockback/platform only.

### 5.3 Chase (`0x41bbf0(enemy, flee)`, vtable `0x4aa0f8`; `+0x2c` target, `+0x30` moving, `+0x34` turn timer, `+0x38` 1 = fleeing)
* Start `0x41bc80(target)`: target speed H := **`P+0x0c` = 600** (direct), turn speed := **`P+0x14` = 2π rad/s**;
  `a = Steer()`; `a ≠ 0` ⇒ `+0x34 = a / turnspeed`, `+0x30 = 0` (turn in place first); otherwise `+0x30 = 1`.
* `Steer` `0x41bd00`: `H.Tick(dt)`; target angle = angle(own pos → target) (reversed when fleeing) without snapping; if the sensor
  (`enemy+0x124`, `0x41d2a0(angle)`) reports that direction blocked ⇒ target angle = `0x41d310(angle)` (nearest free of
  16 directions). Returns the remaining angle difference.
* Tick `0x41bee0`: `Steer()`; timer ≤ 0 and not yet moving ⇒ `+0x30 = 1`; otherwise timer −= dt; then `0x41b2c0`.
  Step `[0]` = `+0x30 ? dt·H.speed : 0`; direction `[1]` = `(cos, 0, sin)` of the movement angle.
* Hook `[2]` (`0x41bdf0`): if the sweep displacement < 0.01 (`0x4a94f8`) ⇒ result ± random (−16..15) in x and z
  (wriggle loose). OnBlocked `[3]` (`0x41be90`): target angle = free sensor direction, timer 0.

### 5.4 Wander (`0x41bf30(enemy, leash, &home)`, vtable `0x4aa118`)
`+0x30..0x4c` = 8 actions `(kind<<16)|weight`: actions 0..5 weight **10**, 6 and 7 weight **25**, kind 1
(= duration from `enemy->vtbl[53](action)`; kind 0 would be `2.0 + (rand()&0x1fff)/4096` s). `+0x50` current action, `+0x54`
remaining duration, `+0x58/+0x5c` avoidance cooldown/object, `+0x60/+0x69` homecoming state, `+0x64` &home point, `+0x68` leash on.
* Choosing `0x41c180(−1)`: was the previous action > 5 (walking) ⇒ weighted choice among the idle actions **0..4**, otherwise among **6..7**
  (walking) – so alternating *walking* and *idle*. For action 5 and 7: new direction = `Sensor.0x41d2c0()` (random free
  direction) or, if that gives < 0, a random angle (`0x41ba10`: `(rand() % 6283)·0.001`).
* Step `[0]` (`0x41c3a0`, byte table `0x41c40c`): actions 6, 7, 9, 10 ⇒ target speed H := **`P+0x08` = 200** (with acceleration
  1000/s²), step = `dt·H.speed`; other actions 0.
* Tick `0x41c640`: homecoming `0x41c500` (only with leash: xz distance (subtype ≥ 9: 3D) to home `> P+0x1c` ⇒ target angle toward
  home, action 10 = turning (duration = angle/turnspeed), then action 9 = walking back); timer `0x41c370` (≤ 0 ⇒ new action);
  turn speed := `P+0x10` (π/2); `H.Tick(dt)` except in actions 0..4; then `0x41c470`: movement + **avoidance**:
  `Touch(0,0)` (touches a random actor) and (different object than last time or cooldown elapsed) ⇒ target angle = away from
  that actor, action 7, cooldown = duration.
* OnBlocked `0x41c420`: target angle = `Sensor.0x41d390()` (if ≥ 0), action 5 (turning).
* Type 4 with TRAJ sets `P+0x1c = 10` in Reset ⇒ after a chase it walks back (leash on via `0x41c140(1,&home)`) to
  the point where it left the path and resumes the path once it's within 10 units (state 8 → 0).

### 5.5 Follow path (`0x41c6a0(enemy, traj)`, vtable `0x4aa13c`; patrol along TRAJ)
TRAJ (`inst+0x78`): `traj[0]` low16 = number of points, bit `0x10000` = **closed loop**; `traj+0x10` → records of 16 bytes with the
position at `+4`. Fields: `+0x2c` substate (**0** = walking a segment, **1** = turning at a point, **2..6** = idle pause,
**7** = reversing after an obstacle), `+0x34/+0x38` from/to index, `+0x3c` direction ±1, `+0x40` vec3 path position, `+0x4c` last
world time, `+0x50` t within the substate, `+0x58` duration of the substate.
* Init `0x41c6e0`: speed H := `P+0x08` (direct); more than 1 point ⇒ from = 0, to = 1, angle snap toward point0→point1.
* Tick `0x41cf00`: works with the **world time** `World+0x30` (not dt); H speed 0 ⇒ only reset time. As long as the
  elapsed time exceeds the rest of the substate ⇒ `0x41c8b0` (next substate). Segment duration = `|to − from| / H.speed`.
* Substate 0 (`0x41ccfc`): `pathpos = point[from] + direction(angle from→to) · H.speed · t` (x and z only); angle snap.
* At a point (`0x41c940`): advance indices (`0x41c7b0`: loop ⇒ modulo; open path ⇒ **back and forth**, direction −1 at the
  end); then `r = rand() % 60` (`0x43ff10(60)`): if `r > 6` and `k = r % 5 + 1 ≠ 0` (and, only for paths with > 1 point, only directly
  after a segment) ⇒ **idle substate k+1 (2..6)** with duration `enemy->vtbl[53](substate)` and target angle fixed toward the next point;
  otherwise **substate 1**: turning, duration = angle difference / turn speed; the angle is interpolated with `0x440280`.
* Applying (`0x41cd68`): test point = `(pathpos.x, y + P+0x30 + h/2, pathpos.z)`; cell `0x428cc0`; sphere test `0x434820(radius − 5)` /
  `0x434830(&point, cell)`: collision (`[0x53a554] ≠ 0`, except kind 3 with itself or with an instance whose `vtbl[25]()` ≠ 0) ⇒
  **reverse** (`0x41cad0`: swap from/to, reverse direction, substate 7 = turning) and position unchanged. Otherwise
  `ground = 0x41a2a0()` (GetHeight `0x435650` under pos + h/2); if `0 < ground − y < P+0x30` ⇒ `y = ground` (step up); `pos.x/z = pathpos`;
  add the platform delta (y only); update collision center + cell. Descending happens via the gravity of §3.3.
* So there is **no collision/sweep** during patrol: the enemy slides exactly along the line segments.

### 5.6 Obstacle sensor (`+0x124`, ctor `0x41cfc0`, tick `0x41d4a0(mode)`)
Range `+4` oscillates ±10 per frame between **100 and 200**; 16 directions (multiples of π/8 = `0x4aa15c`), per direction
a ray test (`0x41d010`: cell `0x428cc0`, ray `0x435810`) whose hit kind goes into `+0x1c + i·8`; `0x41d430(mode)` sets
a free flag (`+0xa4 + i`) per direction: mode bit 1 blocks on hit kind 2, bit 2 on hit kind 3 (enemies: mode 3 =
both). Query functions: `0x41d2a0(angle)` free?, `0x41d310(angle)` nearest free direction, `0x41d2c0()` random free
direction, `0x41d390()` the free direction with the widest free gap (< 0 = none). Decompiled in full, with the probe geometry
(45° down from R above the feet; kind 3 = drop > `P+0x2c`, kind 2 = wall or rise > `P+0x30`) and the port: OBSTACLE.md §3.

## 6. Damage

### 6.1 Enemy hits player (type 4/5/6, state 4, `0x41911a..0x4191ec`)
Every frame of the charge run: `Touch(1, 0)` = 3D distance between the position pointers `< P+0x04 (30) + Perso.radius`. Then
`Perso->vtbl[39](enemy, P+0x3c = 1.0, &dir, &point, 0)` (= `0x44ca00`, PERSO_MOVE §4.4: knockback 500 units/s for 0.2 s along `dir`,
0.6 s invulnerable, health −= 1). **The enemy itself never calls `vtbl[38]`.** Result true (player dead) ⇒ state 10/11
(animation 11), otherwise state 3 (animation 10) and then `P+0x38` s of cooldown during which the player is ignored.
Outside state 4, touching does **no** damage.

### 6.2 Player hits enemy: `vtbl[39]` type 4/5/6 = `0x419480`
```c
bool Enemy4_TakeDamage(e, attacker, float dmg, vec3 *dir, vec3 *point, int kind) {
    if (e->hitT /*+0x158*/ > 0) return false;             /* still "hit": invulnerable */
    e->state = 9;  e->anim->vtbl[4]();                    /* AnimCtrl reset */
    e->hitT = e->vtbl[53](0);                             /* 0.25 s */
    return Enemy_TakeDamage(e, attacker, dmg, dir, point, 0);   /* kind becomes 0: peck and charge run identical */
}
bool Enemy_TakeDamage(...) /* 0x41adc0 */ {
    if (!Behav_Knock(e->behav, dir)) return false;        /* 0x41b6b0: knockT > 0 ⇒ false; otherwise knockDir = *dir, knockT = e->vtbl[53](-1) */
    if (kind != 2) Effect_Hit(dmg, point);                /* 0x40c2d0 → 0x4750e0(&point) (star; point == 0 ⇒ nothing) */
    e->hp -= dmg;  return e->hp <= 0;
}
```
* `isPeck` is **ignored**. The difference between peck and charge run is only in `dir`: the peck gives `dir = (0,0,0)` ⇒ no knockback; the
  charge run gives `normalize_xz` ⇒ knockback at speed `600·t_remaining` over 0.25 s (≈ 18.75 units total).
* Then state 9: `hp ≤ 0` ⇒ particle effect `vtbl[57]`, state 12: animation 13, after `AnimLen(13)+1.0` s `+0x10c |= 1`;
  in the second half fade-out via `+0x6c`. At the start of state 12 the type-word bit 0x400 is cleared and RegisterActor2
  (`0x419460`) stops ⇒ no longer hittable. The next frame `0x40bf60` calls `vtbl[29]`: `[0x4c532c]++`
  and `0x407850` (remove instance from the world). **No SetVar, no script message, no bonus.**
* `vtbl[37]` (warning) = `ret 4` and `vtbl[38]` = empty for type 4/5/6. `vtbl[36]` = `0x40c3a0` = `return 0`.
* `vtbl[40](&pos, r)` (`0x41ae20`, explosion): within r ⇒ `vtbl[39](0, hp, &away, 0, 0)` = instant death.
* `enemy+0x12c` from EVENTS §4.2 does **not** belong to these classes (`+0x12c` here is the y of the start position); `0x44d6e0` and
  the list `0x5e4880` belong to type 40/120/121 (BONUS.md §7).

## 7. Messages (`vtbl[22]` = `0x414530` → `Enemy::HandleMsg 0x41a740`)

Message = `{+0 id, +0xc a, +0x10 b}` (ints). `0x41a740` only handles id **6** and **11**; everything else goes to the
FadeInst/Instance handler `0x44e8f0` (incl. message 56 = fade `+0x6c`, MESSAGES.md). Always returns 0.

| id | a | b | effect | code |
|---|---|---|---|---|
| 6 | on ≠ 0 | – | **activate**: if not in the world (`+0x1c < 0`) ⇒ `0x407790(pos + (0, h/2, 0))` (hang in cell) | `0x41abbf` |
| 6 | 0 | – | **deactivate**: if in the world: it's on a press collision (`+0x198 ≠ −1`) ⇒ `0x442100(col, id)` (UnPress event) and `+0x198 = −1`; `0x407850` (out of the cell list; `+0x1c = +0x18 = −1`) | `0x41abfd` |
| 11 | 0 | v | `P+0x1c = v` – leash distance to home point (Wander) | `0x41a9e4` |
| 11 | 1 | v | `P+0x20 = v` – **sight distance** | `0x41a9f5` |
| 11 | 2 | v | `P+0x2c = max(v, 1)` – max. step down | `0x41aa06` |
| 11 | 3 | v | `P+0x30 = max(v, 1)` – max. step up | `0x41aa38` |
| 11 | 4 | – | **`vtbl[17]()` Reset** (back to start, full hp) | `0x41aa6a` |
| 11 | 5 | 0/1 | **"trajet aléatoire"**: 0 ⇒ `Wander.0x41c140(0,0)` (leash off) and flag 2 off; 1 ⇒ leash on around `+0x134`. Without a Wander behavior: (empty) error "Cet ennemi n'a pas de trajet aleatoire (patrouilleur…)" | `0x41aa76` |
| 11 | 6 | 0/1 | **"trajet de suivi"**: 1 ⇒ flag 2 on (FindTarget measures from the home point = guard), otherwise off. Requires the Chase behavior (`+0x164`), otherwise message "…pas de trajet de suivi (tete de turc…)" | `0x41aac8` |
| 11 | 7 | v | `P+0x08 = v` walk speed, and H speed direct := v | `0x41a95a` |
| 11 | 8 | v | `P+0x0c = v` run speed, and H speed direct := v | `0x41a982` |
| 11 | 9, 10 | degrees | `P+0x10 = v · (1/180) · π` turn speed (both cases write `P+0x10`) | `0x41a9aa`, `0x41a9c7` |
| 11 | 12 | v | `P+0x3c` damage | `0x41aaff` |
| 11 | 13 | v | `P+0x34` = hp max **and** `hp (+0x150) = v` | `0x41ab10` |
| 11 | 14 | v | `P+0x24` max. \|dy\| for seeing | `0x41ab30` |
| 11 | 15 | v | `P+0x38` cooldown time (whole seconds) | `0x41ab41` |
| 11 | 18 | v | `+0x154 = v · 0.01` | `0x41a946` |
| 11 | 19 | w | weight w for **all** 8 wander actions | `0x41a7d6` |
| 11 | 20..24 | w | weight wander action 0..4 (idles) | `0x41a856`… |
| 11 | 25 | w | weight action 5 (turning) | `0x41a928` |
| 11 | 26, 27 | w | weight action 6, 7 (walking) | `0x41a8ec`, `0x41a90a` |
| 11 | 30 | 0/1 | flag 0x20 of `+0x174` | `0x41a7b2` |
| 11 | 31..36 | v | `P+0x40, +0x48, +0x4c, +0x50, +0x54, +0x58` (class-specific, type 7+) | `0x41ab52`… |
| 11 | 37 | v | `P+0xc0` activation distance to the camera | `0x41a79e` |
| 11 | 11, 16, 17, 28, 29 | | ignored | `0x41ac2a` |

There are **no acknowledge variables** in this handler; the SetVar pairs `+0x230/+0x294/+0x24c` from EVENTS §4.2 belong to the
boss classes (`0x40c730`, `0x40d850`, `0x40eb50`, own handlers `0x40d530`, `0x40e7e3`, `0x410052`, which then
call `0x41a740`). The enemy only reports msgmask **0x200** (on the ground, every frame in `0x41a4e0`) and clears **0x10** on Reset (`0x41a167`).

## 8. Type 7/8/9 – the shooter (ctor `0x416ca0(subtype)`, vtable `0x4a9dc0`, size 0x20c)

Fully read (Update `0x416fb0`, jump table `0x417da0`, 17 states; helper functions `0x417df0..0x418820`). Same base and the same four
behaviors as type 4 (PostLoad `0x416d90` is identical to `0x418ae0` except for the AnimCtrl ctor; `[0x4c5330]++`).

### 8.1 Class factory and parameters

Factory `0x403502` (byte table `0x403f3c`, jump table `0x403e94`): **type 7 → case `0x40370c` → `push 4`**, **type 8 → `0x4036d3` → `push 5`**,
**type 9 → `0x403745` → `push 6`** (all three `new(0x20c)` + `0x416ca0(subtype)`). The ctor sets category 2 (`0x40c360(2)`), subtype
(`0x40c380`) and `+0x1c4 = 0`. The contradiction with OBJECTS.md §2.5 was a misreading there: "radius 50, sight 2500, height 180, hp 2,
damage 2" is **subtype 7** (= type 10, case `0x41d95c`); type 7 is subtype 4 (case `0x41d750`).

Final P values (`0x41d510`: defaults, then `0x41dc38[subtype−1]`; all float unless noted):

| P+ | type 7 (sub 4, `0x41d750`) | type 8 (sub 5, `0x41d7da`) | type 9 (sub 6, `0x41d864`) | meaning in this class |
|---|---|---|---|---|
| 0x04 | 30 | 30 | 30 | radius |
| 0x28 | 140 | **130** | 140 | height |
| 0x08 | 200 | 200 | 200 | walk speed (scripts: `11 [inst, 7, 150]`) |
| 0x0c | 600 | 600 | 600 | run speed = **charge-run speed** (`0x41bc80`) and base of the dodge speed (×3) |
| 0x5c | 800 | 800 | 800 | **not read** in this class (type 4 only) |
| 0x10 / 0x14 / 0x18 | π/2 / **4π** / π | same | same | turn speed wander / fast (Chase; aiming = `P+0x14 · 4` = 16π rad/s) / – |
| 0x1c | 800 | 800 | **1000** | leash (Reset with TRAJ: 10; scripts `11 [inst, 0, v]`) |
| 0x20 | 1500 | 1500 | 1500 | sight distance 3D (scripts `11 [inst, 1, v]`: W1A 800) |
| 0x24 | **800** | 800 | 800 | max. \|dy\| |
| 0x34 | **1** | **2** | **3** | hit points |
| 0x38 | 1.5 | 1.0 | 1.5 | cooldown `cool` |
| 0x3c | 1 | 1 | **3** | damage of the bite (charge run) |
| 0x40 | 1 | 1 | **2** | **damage of the projectile** → `T+0x30` |
| 0x4c | **2.0** | **1.5** | **2.0** | reload time (s); scripts `11 [inst, 33, v]` (W2D: 2 and 3) |
| 0x54 | **150** | **150** | **10** | bite range (xz): within this range charge run instead of shooting. Type 9 thus almost never bites |
| 0x58 | 300 | 300 | 300 | dodge distance (only type 9 uses it) |
| 0x60 | 1000 | 1000 | 1000 | projectile speed → `T+0x20` |
| 0x64 | 1 | 1 | 0 | → `T+0x54` (y clamp; no effect since `dir0.y = 0`) |
| 0x68 | **0.2** | **0** | **0** | → `T+0x40` steer factor xz per 1/60 s: **only type 7 is homing** |
| 0x70 | 0 | 0 | 0 | → `T+0x3c` aim height **and** `T+0x44` vertical steer speed (0 ⇒ no vertical steering) |
| 0x74 (int) | **0** | **1** | **3** | → `T+0x60` visual kind: 0/1 = **missile model + smoke** (`0x4700e0`; SoundFx **17** resp. **18**), 3 = **fireball** (`0x470af0`, SoundFx **20**) |
| 0x48 | 150 | 150 | 10 | set, **no reader** in `0x416ca0..0x4189f0` (message 11/32 writes it; W2B uses that): uncertain |
| 0x6c | 100 | 100 | 100 | set, no reader found: uncertain |
| 0x44 | 600 | 600 | 600 | knockback factor (default) |

None of the three is ballistic: gravity comes from template 1 (= 0) and the starting direction is **horizontal** (§8.2). The projectile
thus flies straight, at muzzle height, 1000 u/s, lifetime 15 s (template 1), radius 5, 0 bounces; type 7 steers in xz
(`k = pow(0.8, dt·60)`, PROJECTILES.md §2.2; clamped by `T+0x50 = 0`: never more than 90° off the starting direction), y stays constant.

### 8.2 Firing `vtbl[58](target, Vec3 m[2])` = `0x418820`

```c
bool Shooter_Fire(Enemy *e, Inst *target, Vec3 *m /* m[0] = muzzle, m[1] = second point */) {
    ProjT T = { 0x44a260 values inline };            /* 0x41883d..0x4188ba: radius 5, speed 1000, lifetime 5, damage 20, aim h. 150, visual 4, hits-everything 1 */
    Proj_Template(1, &T);                             /* 0x449070: overwrites everything with template 1 (PROJECTILES.md §1.1) */
    T.pos  = m[0];                                    /* 0x4188e6 */
    T.dir0 = (m[1].x - m[0].x, 0, m[1].z - m[0].z);   /* y = 0 (0x4188d4); normalized if length > 0 (0x418913) */
    T.target = target;  T.damage = P->+0x40;  T.owner = e;
    T.aim_h = T.vsteer = P->+0x70;  T.steer = P->+0x68;  T.lim_y = P->+0x64;
    T.speed = P->+0x60;  T.visual = P->+0x74;
    return Proj_Alloc(&T) != NULL;                    /* 0x41898c -> 0x4490a0 -> 0x449130: SoundFx 17/18/20 (3D on the enemy) + visual */
}
```
There is **no own sound** and no animation event: the shot is timed by the state timer `+0x1e4 = AnimLen(0x13)` (§8.4), the
sound comes from `0x449130`. The caller (state 13) supplies `m`: `m[0]` = start point of the first marker node with **type code 1**
(`0x42f6b0(this, 1, m, 0)`, world space, in the current animation pose; W1A model 45: node 65 on bone 9), and overwrites `m[1]` with
`m[0] + look direction(H) · 10` (`0x41b8b0`, `0x4a9750`): so the firing direction is the **enemy's look direction**, not the marker direction
and not the direction toward the player (after state 12 it does look almost exactly at the player).
If the projectile hits something, `0x44a0a0` calls `owner->vtbl[41](dead)` = `0x417fd0`: `dead` and state ≠ 10 ⇒ state **8** (cheering).

### 8.3 Fields (above the Enemy base)

| off | meaning |
|---|---|
| 0x1c0 | state 0..16 |
| 0x1c4 | AnimCtrl (0x54 B, ctor `0x4189b0` → base `0x4369f0`, vtable `0x4a9eac`, `[0]` = `0x4189d0`: record `0x4b26a8 + n·0x1c`) |
| 0x1c8 | `cool`: while ≥ 0 the player is not noticed (counted down in the prologue whenever ≥ 0) |
| 0x1cc | state 9 timer (cheering) = AnimLen(11) |
| 0x1d0 | **reload timer** (prologue: while ≥ 0 `−= dt`; after a shot `+= P+0x4c`) |
| 0x1d4 | remaining charge-run time = `distance_xz / P+0x0c` |
| 0x1d8 | braking timer (state 7) = AnimLen(9) |
| 0x1dc | `AnimLen(4, 1)` (Reset `0x416f8f`): length of one walk cycle, for the path animation |
| 0x1e0 | duration of the turning animation in state 11 |
| 0x1e4 | wind-up timer (state 12) = AnimLen(0x13); type 9: then another 0.1 s in state 13 |
| 0x1e8 | dodge timer (state 16) = `P+0x58 / (3·P+0x0c)` = 0.1667 s |
| 0x1ec | vec3 normalized 3D direction toward the player at the start of the charge run (no reader found) |
| 0x1f8 | state 1 timer (after the bite) = AnimLen(10) |
| 0x200 | vec3 player's attack direction (via `vtbl[37]`), converted in state 15 into the dodge vector |

Reset `vtbl[17]` = `0x416ed0`: `Enemy::Reset`, AnimCtrl reset; with TRAJ state **0** (behavior Pad, `P+0x1c = 10`), otherwise state **3**
(behavior Wander, `vtbl[6]()` + `0x41c160`); all timers 0 (so also reload timer 0: the **first shot follows immediately**
after noticing).

### 8.4 State machine (`vtbl[52]` = `0x416fb0`)

Prologue: `Enemy::Update` (§4.2; the animation choice `vtbl[45]` thus runs before the switch, with the previous frame's state);
`reload ≥ 0 ⇒ −= dt`; `cool ≥ 0 ⇒ −= dt`; `hitT > 0 ⇒ −= dt`. `target = FindTarget(1, 0)` (§3.2) is re-queried per state.
"→ 2*" = the shared ending `0x4178ee`: state 2 **without** setting `cool`.

| # | code | name | exact behavior |
|---|---|---|---|
| 0 | `0x417146` | PATROL | if `cool < 0` and target ⇒ home `+0x134` = own position, `Wander.0x41c140(1, &home)`, → **11** |
| 2 | `0x417057` | TO WANDER | H speed := `P+0x08` (direct), turn speed := `P+0x10` (`0x41b940`), behavior = Wander, `vtbl[6]()`, `0x41c160`, → **3** |
| 3 | `0x4170a3` | WANDER | if `cool < 0` and target ⇒ **11**. Then (even if 11 was just set): with TRAJ and xz distance to home `< 10` ⇒ behavior = Pad (`0x41cfb0`), → **0** |
| 11 | `0x4171bd` | **WAITING/RELOADING** | no target ⇒ 2*. Behavior = Stand still with `Stand+0x38 = 0`; turn speed := `P+0x14 · 4.0` (`0x4a94c0`); target angle := angle(own pos → target) without snapping (`0x41ba60(own, tgt, 0)`); `+0x1e0 = arc(H.angle, H.targetAngle) / P+0x14 · 4.0` (`0x4401c0`; literally: divide by 4π, **times** 4); `0x436bd0(2, +0x1e0, 1)` and `0x436bd0(1, +0x1e0, 1)` (scale the turn animations to last `+0x1e0` s). **`H.Tick` is not called here** (Stand-still Tick `0x41bed0` = plain `0x41b2c0`; the class's only `0x41ba90` call is in state 12): so during reloading the enemy does **not** actually turn, it just plays the turn animation. If `reload ≤ 0` ⇒ `+0x1e4 = AnimLen(0x13, 0)`, → **12** |
| 12 | `0x4172da` | **WINDING UP** | no target ⇒ 2*. xz distance `≤ P+0x54` ⇒ **5** (return immediately). Otherwise: `+0x1e4 > 0` ⇒ `−= dt`; otherwise (subtype 6: `+0x1e4 = 0.1` (`0x3dcccccd`)) → **13**. In both cases afterward target angle := toward the player and **`H.Tick(dt)`** (`0x417397`): here it does turn, at 16π rad/s ≈ within a few frames |
| 13 | `0x4173ca` | **FIRING** | no target ⇒ 2*. `0x42f6b0(this, 1, m, 0)` fails (no marker type code 1) ⇒ return (stays in 13). `m[1] = m[0] + look direction·10`. Subtype 6: `+0x1e4 −= dt`; still `> 0` ⇒ return. Sightline `0x497ed0(&(pos + (0, h/2, 0)), &m[0], −1)`: **from its own center to its own muzzle** (does the muzzle poke through a wall/instance?), not toward the player. `[0x4c4bd0]` = raw collision result (PERSO_MOVE.md: 1 = nothing, **3 = world polygon, 4 = instance**): on 3 or 4 the shot is **skipped**, otherwise `vtbl[58](target, m)`. In both cases `reload += P+0x4c`, → **11**. (Whether the ray can hit its own hulls: uncertain.) |
| 5 | `0x417523` | START CHARGE RUN | no target ⇒ 2*. xz distance `> P+0x54` ⇒ return (**stays in 5**, stands still playing animation 8 until the player is back in range or out of sight). Otherwise `+0x1ec = normalize(target − pos)`; behavior = Chase, `vtbl[6]()`, `0x41bc80(target)` (speed `P+0x0c` = 600 direct, turn speed `P+0x14`, turn in place first: §5.3); `+0x1d4 = distance / P+0x0c`; `0x436bd0(8, +0x1d4, 1)` (charge-run animation lasts exactly that long), → **6** |
| 6 | `0x417664` | CHARGE RUN | no target ⇒ **14**; `+0x1d4 < 0` ⇒ **14**; `+0x1d4 −= dt`; `Touch(1, 0)` (`vtbl[31]`) nothing ⇒ return. Otherwise exactly as type 4: `dir = normalize_xz(target − pos)`, `point = pos + dir·P+0x04 + (0, h/2, 0)`, `dead = target->vtbl[39](this, P+0x3c, &dir, &point, 0)`; dead ⇒ **8**, otherwise `+0x1f8 = AnimLen(10, 0)`, → **1** |
| 1 | `0x4177f7` | AFTER THE BITE | behavior = Stand still (`+0x38 = 0`), H speed := `P+0x08` direct; `+0x1f8 > 0` ⇒ `−= dt`; otherwise `cool = P+0x38`, → **2** |
| 14 | `0x417883` | START BRAKING | behavior = Stand still; `+0x1d8 = AnimLen(9, 0)`; state 7; H speed := `P+0x08`; falls through into 7 |
| 7 | `0x4178cc` | BRAKING | `+0x1d8 < 0` ⇒ `cool = P+0x38`, → **2**; otherwise `−= dt` |
| 8 | `0x41792e` | START CHEERING | `+0x1cc = AnimLen(11, 0)`; state 9; behavior = Stand still; H speed := `P+0x08`; falls through into 9 |
| 9 | `0x417977` | CHEERING | `+0x1cc −= dt`; `≤ 0` ⇒ **2** (no `cool`) |
| 4 | `0x417a19` | **HIT** | `hp ≤ 0` ⇒ `vtbl[57]()` (death particles, §4.2), → **10**; otherwise `hitT ≤ 0` ⇒ **2**. Behavior stays what it was |
| 10 | `0x4179b6` | **DEAD** | behavior = Stand still (`+0x38 = 0`); `+0x15c += dt`; `vtbl[51]() ≤ +0x15c` ⇒ `+0x10c \|= 1`; every frame type word `&= ~0x400` |
| 15 | `0x417a63` | **START DODGING** (reachable only for subtype 6) | see §8.5 → **16** |
| 16 | `0x417ce4` | DODGING | `+0x1e8 > 0` ⇒ `−= dt`; otherwise `Stand+0x38 = 0`, **`reload = P+0x4c`**, `cool = P+0x38`, state **3**, H speed := `P+0x08` direct, turn speed := `P+0x10`, behavior = Wander, `0x41c0c0(0)` (Wander restart with action 0), `0x41c160` |

Cycle of a shooter that sees the player: 3 → 11 (1 frame when `reload ≤ 0`) → 12 (AnimLen(0x13) = 0.53 s winding up, turns while doing so) → 13
(shot; type 9 0.1 s later) → 11 (wait `P+0x4c` s without turning) → 12 → … If the player comes within `P+0x54` while it's in **12**
⇒ charge run of at most `150/600 = 0.25 s`. Distance is not tested in 11 and 13.

Other slots: `vtbl[47]` = `0x417f20` (byte table `0x417f48`): RegisterActor2 (`0x40c0b0`) in every state except **10**.
`vtbl[51]` = `0x4149e0` = `AnimLen(13, 0) + 1.0`. `vtbl[53](x)` = `0x4184f0` (table `0x4187ac`): state 0 ⇒ `Pad+0x58`; 1 ⇒ AnimLen(10);
2/3 ⇒ per wander action (table `0x4187f0`: action −1 ⇒ 0.0; 0..4 ⇒ AnimLen(14..18); 5 ⇒ AnimLen(26 or 27); 6 ⇒ ΣAnimLen(4, 0..2); 7 ⇒ ΣAnimLen(5, 0..2);
9 ⇒ ΣAnimLen(6, 0..2) − `inst+0xac / inst+0xa0`); **4 ⇒ 0.25** (`0x4a9ca0`); 5/6 ⇒ AnimLen(8); 7/14 ⇒ AnimLen(9); 8/9 ⇒ AnimLen(11); 10 ⇒ AnimLen(13);
11 ⇒ AnimLen(1 or 2); 12 ⇒ AnimLen(0x13); 13 ⇒ AnimLen(0x14); 15 ⇒ AnimLen(0); 16 ⇒ AnimLen(0x15).

### 8.5 Dodging (type 9): `vtbl[37]`, `vtbl[42]`, state 15/16

* **Caller of `vtbl[37]`** (slot `+0x94`): only `0x457f90` in the Perso's attack code, attack state 1 = **start of the
  aerial peck dive** (`0x457eac`, PERSO_JUMP.md §2.4): nearest attackable instance within 500 (`0x4632e0`), if that's category 2
  the aim point becomes `pos + (0, 0.8·height, 0)`; only if the player is more than **50 above** that aim point: `atkDisp = aimpoint − playerpos`
  (3D, **not normalized**) and `target->vtbl[37](&atkDisp)`. So the regular peck/charge run on the ground gives no warning.
  (The other `call [r+0x94]` in the exe – `0x428eef..0x42a18a`, `0x44ea80`, `0x47ed03..` – are other classes.)
* `vtbl[37](Vec3 *d)` = `0x417ee0`: `if (subtype == 6) { +0x200 = *d; state = 15; }` – without any further test (even during hit/cheering; in
  state 10 bit 0x400 is already gone, so the target finder no longer finds it then). For type 7/8 it does nothing.
* `vtbl[42]()` = `0x417ff0`: `state = 15` unconditionally. **No caller found** (no `call [r+0xa8]` on an Npc anywhere in the exe; all other
  enemy classes have the empty `0x462c60` here); `+0x200` then keeps its old value: uncertain/dead code.
* State 15 (`0x417a63`):
```c
d = normalize_xz(e->+0x200) * P->+0x58;            /* 300; y = 0 */       e->+0x200 = d;
c[3] = pos + ( d.x, h/2,  d.z);                      /* away from the player */
c[2] = pos + (-d.z, h/2,  d.x);                      /* sideways */
c[1] = pos + ( d.z, h/2, -d.x);                      /* the other side */
c[0] = pos + (-d.x, h/2, -d.z);                      /* toward the player */
for (i = 3; i >= 0 && !Free(e, &c[i]); i--) ;  if (i < 0) i = 3;          /* 0x417bc5: first free one, else c[3] anyway */
e->+0x1e8 = P->+0x58 / (P->+0x0c * 3.0f);          /* 0x4a988c: 300/1800 = 0.1667 s */
e->behav = Stand;  Stand->+0x38 = P->+0x0c * 3.0f;    /* step speed 1800 u/s */
Stand->+0x2c = normalize_xz(c[i] - pos);            /* step direction */
e->state = 16;
H_SetAngle(H, angle(c[i] -> pos), snap = 1);       /* 0x41ba60(c[i], own, 1): looks AGAINST the jump direction (for c[3]: toward the player) */
bool Free(e, Vec3 *c) {                            /* 0x417df0 */
    Ray(&(pos + (0, h/2, 0)), c, -1);              /* 0x4359b0 */    if ([0x53a554] != 0) return false;
    GetHeight(c, -1, 1);                           /* 0x435650 -> [0x53a568] */
    float drop = pos.y - [0x53a568];
    return drop <= P->+0x2c && -drop <= P->+0x30;  /* at most 10 lower / 10 higher */
}
```
  The actual movement runs through the regular `0x41b2c0` (sweep, edges, knockback) with step `dt · 1800` ⇒ 300 units in 0.167 s. Afterward
  (state 16) it is unarmed for `P+0x4c` s and blind for `P+0x38` s. If it does get hit during 16 (state 4, behavior unchanged), then
  `Stand+0x38 = 1800` stays set until some state resets the behavior: an edge case, literally so.

### 8.6 Animations

Table `0x4b26a8` (28 records of 0x1c B, ending exactly where the type-4 table `0x4b29b8` begins), same format
`{int sub[4]; int prio; float speed; u8 restart}`; `AnimLen(n, k) = duration(sub[k]) / speed`. Note: `0x436bd0(n, T, k)` **writes**
`speed = Σ_{i<k} duration(sub[i]) / T` into the (global, shared by all enemies of the class) table.

| n | sub[] | prio | speed | restart | used for |
|---|---|---|---|---|---|
| 0 | 0,0,0,0 | 1000 | 3 | 1 | state 15 (1 frame) |
| 1 / 2 | 16×4 / 17×4 | **900** | 3 (rewritten) | 1 | state 11: turning/waiting; `H+0x0c > 0` (`0x41b900`) ⇒ 1, otherwise 2; duration `+0x1e0` |
| 3 | 1,2,2,2 | 1000 | 3 | 1 | (running; not requested in this class) |
| 4 / 5 | 3,4,5,0 | 1000 | 3 | 1 | wander action 6 / 7 (walking) |
| 6 / 7 | 4,4,5,0 / 4×4 | 1000 | 3 | **0** | wander action 9 / 10 |
| 8 | 13,−1,−1,−1 | 1000 | 3 (rewritten: `duration(13) / +0x1d4`) | 1 | state 5/6 charge run |
| 9 | 15,0,0,0 | 1000 | 3 | 1 | state 7/14 braking |
| 10 | 14,0,0,0 | **1500** | **2** | 1 | state 1 bite |
| 11 | 18,0,0,0 | 1000 | 3 | 1 | state 8/9 cheering |
| 12 | 11,0,0,0 | 1000 | 4 | 1 | state 4 hit |
| 13 | 12,−1,−1,−1 | 2000 | 3 | 1 | state 10 dead |
| 14..18 | 6..10, 0, −1, −1 | 1000 | **2** | 1 | idle variations (wander action 0..4, path substate 2..6) |
| **19** (0x13) | **21**,−1,−1,−1 | 1000 | 3 | 1 | state 12 **winding up/aiming** |
| **20** (0x14) | **22**, 0,−1,−1 | 1000 | 3 | 1 | state 13 **throwing/shooting** |
| **21** (0x15) | **19**, 0,−1,−1 | 1000 | 3 | 1 | state 16 **dodge jump** |
| 22 | 20, 0,−1,−1 | 1000 | 3 | 1 | not requested in this class (uncertain what for) |
| 23 / 24 / 25 | 3,4,4,4 / 4×4 / 5,0,−1,−1 | 1000 | 3 | 1 | walking the path: start / loop / stop |
| 26 / 27 | 16,0,−1,−1 / 17,0,−1,−1 | 1000 | 3 | 1 | turning in place (wander action 5, path substate 1 and 7) |

Differences from the type-4 table: 1/2 (sub ×4, prio 900), 8 (speed 3 instead of 10), 9 (3 instead of 2), 10 (prio 1500, speed 2), 11 (3 instead of 1.5),
14..18 (speed 2), new 19..22; the type-4 records 19..23 are here at 23..27.

`vtbl[45]` = `0x418000` (jump table `0x418184`), always followed by `AnimCtrl->Tick(dt)`:

| state | 0 | 1 | 2, 3 | 4 | 5, 6 | 7, 14 | 8, 9 | 10 | 11 | 12 | 13 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| anim | path (`0x4182e0`) | 10 | wander (`0x4181e0`) | 12 | 8 | 9 | 11 | 13 | 1 / 2 | **0x13** | **0x14** | 0 | **0x15** |

* Wander `0x4181e0` (table `0x4182ac`, action from `0x41c630`): 0..4 ⇒ 14..18; 5 ⇒ 26 (`H+0x0c > 0`) or 27; 6 ⇒ 4; 7 ⇒ 5; 8 ⇒ nothing; 9 ⇒ 6; 10 ⇒ 7.
* Path `0x4182e0` (table `0x4184d0`, substate from `0x4883c0`): 2..6 ⇒ 14..18; 1 and 7 ⇒ 26/27 with `0x436bd0(n, Pad+0x58, 1)` (turn lasts the whole substate);
  0 (walking a segment, `0x4182ff`): `rest = Pad+0x58 − Pad+0x50`; `AnimLen(25) > rest` ⇒ 25 (stop); otherwise `AnimLen(23) > Pad+0x50` ⇒ 23 (start); otherwise
  `x = Pad+0x58 − AnimLen(25) − AnimLen(23)`, `n = x / +0x1dc + 0.5`, `n > 1 ⇒ x /= floor(n)`, `x < 0.5 ⇒ 0`; `0x436bd0(24, x, 2)`; anim 24.
* Timing of the shot: state 13 lasts exactly one frame for type 7/8; the throw animation 0x14 keeps playing afterward because the wait animations 1/2
  have **prio 900** and `Tick` (`0x436a50`) only allows a lower priority once the instance animation is finished (`inst+0xc0 == 1`).
* At an angle difference of 0 in state 11, `T = 0` ⇒ `speed = duration/0` (infinite): guard against this in the port (hold pose).
* W1A model 45 (23 anims), duration in s: 11 = 1.2, 12 = 4.4, 13 = 1.2, 14 = 2.0, 15 = 2.4, 16/17 = 1.6, 18 = 12.4, 19 = 1.2, 20 = 1.2, 21 = 1.6, 22 = 1.3 ⇒
  winding up 0.533 s, throwing 0.433 s, dodging 0.4 s, bite 1.0 s, braking 0.8 s, cheering 4.13 s, hit anim 0.3 s, dead 1.467 + 1.0 = **2.467 s**.

### 8.7 Damage, death, sound

`vtbl[39]` = `0x417f60` is line-for-line `0x419480` (§6.2) with state **4** instead of 9: `hitT > 0` ⇒ false; state 4; AnimCtrl reset;
`hitT = vtbl[53](0)` = 0.25 (state is already 4 by then); `Enemy_TakeDamage(att, dmg, dir, point, 0)` (knockback 0.25 s, `hp −= dmg`). No extra
invulnerability, no difference per subtype other than hp (1/2/3). Death: state 4 → `vtbl[57]` (5 particles) → state 10, fading in the second
half of `AnimLen(13) + 1.0`, then `+0x10c |= 1` → `vtbl[29]`: `[0x4c532c]++`, `0x407850`. No message, no bonus.

**Sound:** in `0x416ca0..0x41dc80` (type 4..9 and the Enemy base) there is **not a single** `call 0x468a00` (SoundFx). All enemy sounds (hit,
death, charge run, bite, idle) are **animation events type 4** on the root node of the model (SOUND.md §3, 3D on the instance, pitch ×
`vtbl[27]()` = `0x40d830` = `enemy+0x170` = 1.0 – that's the reader of `+0x170`). W1A model 45: anim 11 (hit) t = 0: refs 63/64/65 (each ⅓),
anim 12 (dead) t = 0: 66/67/68, anim 13 (charge run) t = 230: 62, anim 14 (bite): 71 and 61, anim 8 (idle): 72, walk/turn anims: footsteps;
anims 19..22 (dodging/aiming/throwing) have **no** events – the shot sound is SoundFx 17/18/20 from `0x449130`. There is no "noticed" sound.
The same applies to type 4/5/6 (the port already plays these events via `anim_sounds` in `src/main_engine.c` as soon as the right .ins animation starts at t = 0).
SoundFx 6 (`0x44d730`) belongs to `Bom::Explode`, not to enemies.

### 8.8 Occurrences (from the `1200` messages of each level; total type 7: 29×, type 8: 74×, type 9: 33×)

Each level uses one model; the scripts usually send, after `1200`, `11 [inst, 0, leash]`, `11 [inst, 1, sight]`, sometimes `[5, 1]` (leash on),
`[6, 1]` (guard), `[7, 150]` (walk speed), `[33, s]` (reload time), `[32, v]` (`P+0x48`), `[37, v]`.

| level | type | model | instances: index (x, y, z) | 11 messages |
|---|---|---|---|---|
| W1A | 7 | 45 | 312 (3701, 4, 2671) | leash 200, sight 800 |
| K1A | 7 | 42 | 252 (3701, 4, 2671), 293 (4692, 808, 3673), 294 (3093, 808, 3750), 295 (779, −2007, −131), 436, 437, 456 | leash 100/200, sight 800 |
| W1B | 7 | 29 | 278 (−8773, 2817, −2288), 316 (−458, 4922, −3421; sight 50), 328 (−3263, 4598, −6421), 331 (−2974, 194, −5013), … 13 total | leash 100, sight 800 |
| S1A | 7 | 40 | 322 (140, −1985, 601), 323 (893, −1990, 606), 325, 379, 510, 511, 517, 518 | sight 800..1500, `[37, 10]` |
| W2A | 8 | 38 | 357 (9575, 157, −16400; guard), 359 (7425, 1369, −12682), 360 (7959, 1369, −12773), 373 (−97, 112, −5503), 375 (5422, 713, −2524; no messages), … 18 total | leash 200..400, sight 500..800, walk 150 |
| K2A / S2A | 8 | 37 / 37 | K2A 341 (9575, 157, −16400), 344 (2342, 1335, −5107), … 16; S2A 13 total | same |
| W2B | 8 | 38 | 228 (−1179, −415, −6341), 281 (1755, 778, −4806), 349 (2853, 602, 11155), … 10 total | `[32, 600]`, sight 100..500 |
| W2D | 8 | 37 | 170 (−8918, 3506, −2826), 204 (−7863, 3377, 3067), … 17 total | sight 1000, reload 2 or 3 s |
| W3A | 9 | 50 | 549 (−10241, −254, 11011), 550 (−4036, −407, 2634; sight 200) | leash 400, walk 150 |
| K3A | 9 | 52 | 485 (−10251, −254, 10870), 486 (−10241, −254, 11011), 487 (−3423, 799, 3514), 488 (9192, 1090, 3160), 489 | same |
| W3B | 9 | 42 | 500 (−2496, −3628, −72), 503 (−3466, −290, 3815), 505, 506, 513, 719 | leash 400/800, sight 600, walk 150/250 |
| W3D / S3A | 9 | 27 / 46 | W3D 193 (−5325, −9204, −23659), 317 (860, −3324, 3916), … 8; S3A 429 (−2427, 514, 864; `[33, 200]`, `[32, 800]`), … 12 total | sight 600..2000 |

The models examined (W1A 45, K2A 37, K3A 52) have 23 animations and a marker type code 1 (muzzle; W1A/K2A node 65 on bone 9, K3A node 74 on
bone 26) plus type code 0.

### 8.9 Recipe for type 7/8/9 (extending `src/enemy.c`)

`src/enemy.c` currently only knows the type-4 column. Extension:

1. **Parameters per type** instead of `#define`: `{radius 30, height 140/130/140, walk 200, run 600, see 1500, dy 800 (type 4..6: 600), hp 1/2/3,
   cool 1.5/1.0/1.5, bite 1/1/3, shot_dmg 1/1/2, reload 2.0/1.5/2.0, melee 150/150/10, dodge 300, turn π/2, turn_fast 4π (type 4..6: 2π),
   leash 800/800/1000, proj_speed 1000, steer 0.2/0/0, visual 0/1/3}`; message 11 (§7) writes into it (n = 0, 1, 5, 6, 7, 32, 33, 37 occur).
   `enemies_add`: `type 7..9` ⇒ `hp` from the table, initial state 0 (TRAJ) or **3**; own state numbers (enum below) alongside those of type 4.
2. **Animation table** alongside `g_ea` (main .ins anim, divider, hold): `WALK {4, 3, 0}`, `DASH {13, *, 0}` (divider = `duration(13) / dashT`),
   `BRAKE {15, 3, 1}`, `BITE {14, 2, 1}`, `WIN {18, 3, 1}`, `HIT {11, 4, 1}`, `DEAD {12, 3, 1}`, `IDLE {6, 2, 0}`, `TURN_L {16, *, 0}`, `TURN_R {17, *, 0}`
   (divider = `duration / T`, `T = 4·Δ/P14`; Δ = 0 ⇒ hold pose), `AIM {21, 3, 1}`, `THROW {22, 3, 1}`, `DODGE {19, 3, 1}`. The throw animation must
   finish playing while the state is already 11: hold `THROW` until `anim_time ≥ duration` (priority rule §8.6) and only then show `TURN_*`.
3. **States** (pseudo-C, `see` = FindTarget with `dy 800`, `dxz` = xz distance, `to_player` = angle):
```c
enum { S_PATH=0, S_BITE=1, S_TOWANDER=2, S_WANDER=3, S_HIT=4, S_DASH0=5, S_DASH=6, S_BRAKE=7, S_WIN=9, S_DEAD=10, S_WAIT=11, S_AIM=12, S_FIRE=13, S_DODGE0=15, S_DODGE=16 };
if (e->reload >= 0) e->reload -= dt;   /* alongside cool and hit_t */
case S_PATH:   patrol(); if (cool < 0 && see) { home = pos; st = S_WAIT; } break;
case S_WANDER: wander(); if (cool < 0 && see) st = S_WAIT;  /* TRAJ: within 10 of home -> S_PATH */ break;
case S_WAIT:   if (!see) { st = S_TOWANDER; break; }  speed = 0;  anim = turn-anim toward the sign of ang_diff(to_player, ang), duration 4*|diff|/P14;   /* does NOT actually turn */
               if (e->reload <= 0) { e->t = len(AIM); st = S_AIM; } break;
case S_AIM:    if (!see) { st = S_TOWANDER; break; }  if (dxz <= melee) { st = S_DASH0; break; }
               if (e->t > 0) e->t -= dt; else { if (type == 9) e->t = 0.1f; st = S_FIRE; }
               steer(e, to_player, 4*P14, dt);  anim = AIM;  break;
case S_FIRE:   if (!see) { st = S_TOWANDER; break; }  anim = THROW;
               if (!inst_vector(inst, 1, &m0, &unused)) break;
               if (type == 9 && (e->t -= dt) > 0) break;
               if (!segment_blocked(pos + (0,h/2,0), m0))         /* gel_ray_frac; instance hulls once available */
                   shot_spawn(m0, (cos ang, 0, sin ang), owner = inst, target = player, dmg, steer, visual);   /* audio_fx 17 / 18 / 20 on the enemy */
               e->reload += P4c;  st = S_WAIT;  break;
case S_DASH0:  if (!see) { st = S_TOWANDER; break; }  if (dxz > melee) break;
               turn_t = |ang_diff| / P14;  speed = want = run;  e->atk_t = dxz / run;  DASH-divider = duration(13) / atk_t;  st = S_DASH;  break;
case S_DASH:   if (!see || e->atk_t < 0) { e->t = len(BRAKE); want = walk; st = S_BRAKE; break; }  e->atk_t -= dt;  steer(P14); move after turn_t;
               if (dist3 < radius + 69) { hit = player_hit(pl, bite, dir_xz); if (hit) { player_kill(pl, 3); e->t = len(WIN); st = S_WIN; } else { e->t = len(BITE); st = S_BITE; } }  break;
case S_BITE:   if (e->t > 0) e->t -= dt; else { cool = P38; st = S_TOWANDER; }  break;
case S_BRAKE:  if (e->t < 0) { cool = P38; st = S_TOWANDER; } else e->t -= dt;  break;
case S_WIN:    if ((e->t -= dt) <= 0) st = S_TOWANDER;  break;                  /* also set by a shot -> owner if player_hit gave true (vtbl[41]) and st != S_DEAD */
case S_TOWANDER: speed = want = walk; wander_restart(); st = S_WANDER; break;
case S_HIT:    if (hp <= 0) { death_fx; attackable = 0; st = S_DEAD; } else if (hit_t <= 0) st = S_TOWANDER;  break;
case S_DEAD:   like type 4 state 12 (L = len(DEAD) + 1.0).
case S_DODGE0: d = norm_xz(e->warn) * 300; candidates pos+d, pos+(-d.z,d.x), pos+(d.z,-d.x), pos-d: first with a free ray at h/2 and ground within ±10 (otherwise pos+d);
               e->dodge_dir = norm_xz(c - pos); e->ang = angle(c -> pos) (snap); e->t = 300 / (3*run); st = S_DODGE;  break;
case S_DODGE:  anim = DODGE; if (e->t > 0) { e->t -= dt; enemy_move(e, pl, dodge_dir * 3*run*dt); } else { reload = P4c; cool = P38; want = speed = walk; st = S_WANDER; }  break;
```
4. **Hooks**: `enemy_take_damage` ⇒ state `S_HIT` (4) for type 7..9, `removed`/`S_DEAD` instead of 12. New `enemy_warn_dive(Enemy*, Vec3 d)` = `vtbl[37]`:
   only type 9 ⇒ `warn = d; st = S_DODGE0`; called from `player.c` at the moment the aerial peck dive starts with an enemy as the auto-aim target
   (within 500, player > 50 above `pos.y + 0.8·h`), with `d = (pos + (0, 0.8h, 0)) − playerpos`.
5. **Projectile**: extend `Shot` in `src/main_engine.c` with `speed`, `damage`, `steer`, `target`, `visual`, `owner_enemy`; per frame (only if `steer > 0`):
   `k = powf(1 − steer, dt·60)`; `dir.xz = norm_xz(target − pos)·(1 − k) + dir.xz·k` (aim height 0), renormalize, and revert if
   `dot(dir.xz, dir0.xz) < 0`; y stays 0. Hitting the player ⇒ `player_hit(dmg)`; on death, `enemy → S_WIN`. Lifetime 15 s, first world hit = gone.
   Visual 0/1 = missile (PROJECTILES.md §5.3: ribbon 20 × 25, head image 4 orange, explosion `0x477060(2, …)`, model from the type-41 pool; W1A has 8): **ported**,
   see PROJECTILES.md §7.1. Visual 3 = fireball (§5.5: spinning image-12 head, image-13 spark trail, launch glow, explosion kind 2): **ported**, with SoundFx 20.
6. **Test W1A**: instance 312 (model 45) at (3701, 4, 2671), sight through the script 800, leash 200: come within 800 ⇒ 0.53 s winding up, missile at
   muzzle height that curves toward Woody in xz, then one shot every 2.0 s; within 150 while winding up ⇒ short charge run with a bite (1 heart);
   one peck = death (hp 1), sounds 63..65 and 66..68 from the animation events. K3A/W3A 549/550 for the dodging of type 9 (aerial peck dive from above).

## 9. Recipe: simplest enemy (type 4 without TRAJ) in C

```c
enum { E_WANDER=8, E_NOTICE=2, E_CHASE=1, E_DASH=4, E_MISS=3, E_BRAKE=6, E_HIT=9, E_WIN=11, E_DEAD=12 };
#define R 30.f      /* P+4  */  #define H 140.f    /* P+0x28 */  #define WALK 200.f /* P+8  */
#define RUN 600.f   /* P+0xc*/  #define DASH 800.f /* P+0x5c */  #define SEE 1500.f /* P+0x20, |dy|<600 */
#define TURN 1.5708f /* P+0x10; chasing: 6.2832 */  #define ACC 1000.f  #define COOL 1.5f /* type5 1.0, type6 0.5 */
void enemy_update(Enemy *e, float dt) {
    if (dist2(e->pos, cam) >= 3000*3000 && e->hp > 0) return;
    if (e->cool >= 0) e->cool -= dt;   if (e->hitT > 0) e->hitT -= dt;
    Player *p = (dist(e->pos,pl->pos) < SEE && fabsf(pl->pos.y-e->pos.y) < 600) ? pl : NULL;
    switch (e->st) {
    case E_WANDER: wander_tick(e, dt);           /* alternating idle anim (duration = anim length) and 200 units/s straight ahead */
        if (e->cool < 0 && p) e->st = E_NOTICE;  break;
    case E_NOTICE: if (!p) { e->st = E_WANDER; break; }
        e->turnT = angdiff(e->ang, angle_to(e,p)) / 6.2832f; e->speed = RUN; e->st = E_CHASE;  /* fallthrough */
    case E_CHASE:  if (!p) { e->st = E_WANDER; break; }
        steer(e, angle_to(e,p), 6.2832f, dt); if ((e->turnT -= dt) <= 0) move(e, e->speed*dt);  /* sweep, no drop-off > 10 */
        e->atkT = animlen(8) + 0.5f*animlen(10);
        if (dist_xz(e,p) <= DASH*e->atkT && dot(dirvec(e->ang), norm_xz(p->pos - e->pos)) > 0.95f) { e->speed = DASH; e->st = E_DASH; }
        break;
    case E_DASH:   steer(...); move(e, e->speed*dt);
        if (!p || e->atkT <= 0) { e->t = animlen(9); e->st = E_BRAKE; break; }   e->atkT -= dt;
        if (dist(e->pos, p->pos) < R + p->radius) {
            vec3 d = norm_xz(p->pos - e->pos), pt = e->pos + d*R + (vec3){0,H/2,0};
            if (player_hit(p, e, 1.0f, &d, &pt, 0)) { e->t = animlen(11); e->st = E_WIN; }
            else { e->t = animlen(10); e->st = E_MISS; } }
        break;
    case E_MISS: case E_BRAKE: if ((e->t -= dt) <= 0) { e->cool = COOL; e->speed = WALK; e->st = E_WANDER; } break;
    case E_WIN:  if ((e->t -= dt) <= 0) { e->speed = WALK; e->st = E_WANDER; } break;
    case E_HIT:  if (e->hp <= 0) { spawn_death_fx(e); e->attackable = 0; e->st = E_DEAD; }
                 else if (e->hitT <= 0) e->st = E_WANDER;   break;
    case E_DEAD: e->deadT += dt; float L = animlen(13) + 1.0f;
                 if (e->deadT > L/2) e->fade = fminf(1, (e->deadT - L/2)/(L/2));  if (e->deadT >= L) remove(e); break;
    }
    knockback_and_gravity(e, dt);   /* knockback: v = 600·knockT (0.25 s); fall: v += 200·dt − 0.2·v per frame, y −= v */
}
bool enemy_take_damage(Enemy *e, void *att, float dmg, vec3 *dir, vec3 *pt, int isPeck) {
    if (e->hitT > 0 || e->knockT > 0) return false;
    e->st = E_HIT; e->hitT = e->knockT = 0.25f; e->knockDir = *dir; e->hp -= dmg;  return e->hp <= 0; }
```
(hp: type 4 = 1, type 5 = 1, type 6 = 2; with TRAJ, state 0 = §5.5 is added and it returns via the home point.)

## 10. Open questions
* Base slots 14..16, 23, 24 (`0x41ad80` fills `{pos.x, pos.y + h/2, pos.z, radius, h/2}` = collision cylinder), 27/28/30, 50, 54 are not
  identified; `vtbl[56]` = `0x41b030` (movement with `0x4359b0`/`0x436dc0`) has not been read – it's not called in type 4.
* `Enemy+0x14c`, `+0x154` (message 11/18, ×0.01): no reader found in type 4. `+0x170` (1.0) = pitch factor of the animation sounds (`vtbl[27]` = `0x40d830`, §8.7).
* Flag 0x20 of `+0x174` (set by PostLoad, toggled by message 11/30) and flag 8: no reader/setter found in the code read so far.
* `0x437580` (sweep) has since been read: a **sphere** of radius `P+4`, center `r + up + 1` above the feet, substeps of 30, pushed out against
  world + press nodes of instances (BOSS14.md §5.1). `[0x4b310c]` is not a fraction but the ground-normal y from GetHeight (≥ 0.8 = flat enough).
  `0x437040` (push-out relative to other actors?) has only been examined from the
  caller side; `0x436d20/0x436d80` (platform) likewise.
* The obstacle sensor (§5.6) is decompiled in OBSTACLE.md §3. Type 7/8/9: `vtbl[42]` (`0x417ff0`) has no caller found; `P+0x48` and `P+0x6c`
  have no reader in the class; animation record 22 (sub 20) is never requested (§8).
* Parameter `P+0x44` (600) is proven to be the knockback factor; `P+0x48, +0x50, +0x58, +0x84..0xbc` belong to types 10..13 (not read).
* Types 10..13 (own Update `0x415490`, `0x412310`, `0x4110c0`, `0x413ab0`; type 12 with msgmask 0x10 in `0x411729` and own
  `vtbl[31]/[40]`) have not been analyzed; the hierarchy, P table and messages of §1, §2.3 and §7 do apply to them though.
* Which AnimCtrl sub-animation indices (`sub[]` 0..18) correspond to which animation in the model files still needs to come from the W1A models.
* The player's death after an enemy hit: the enemy itself never calls `Perso->vtbl[38]`; where the Perso does that itself is in
  PERSO_FRAME (§ around `vtbl[38](3)`), not verified here.
