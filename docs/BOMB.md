# BOMB.md — the bomb (class type 40), the bomb thrower (launcher kind 0), and the bomb cannon (type 21)

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, `tools/drange.py`, `tools/funcinfo.py`). Every fact has an address;
floats were read from the exe (PE sections parsed directly). "Uncertain" = not read line by line or not checked against the running original.
Connects to PROJECTILES.md (projectile pool, template `T`), ROCKET.md §7 (class 21), BONUS.md §7 (first summary, corrected here),
PERSO_DEATH.md (Kill kinds), SOUND.md §5. Picking up/carrying/throwing by Woody (Perso state 6), the 1090 dispenser, chests 120/121 and class 17
are in **BOMB_CARRY.md**; here only where the bomb itself is affected.

## 0. Summary

- A bomb is a **regular instance** (class 40, 0x134 B) that during its flight is **carried** by a projectile from the 200 pool
  (`T+0x5c = bomb`, PROJECTILES §2.4). There are at most **16 bombs per level** (pool `0x5e4880`); W2A has exactly 16 (model 26). A bomb
  is never created or destroyed: `0x44d5d0` grabs the first free one from the pool, places it at the start location, and after the explosion it goes back
  into the pool (removed from the world cells = invisible, at the explosion location).
- **Fuse** = `T.life` of the template: bomb thrower (template 0) **2.0 s**, bomb cannon = `+0x114` of the cannon (W2A: **3.2 s**). Detonation
  **2 frames after** the fuse expires (states 4 → 5 → explosion), or 2 frames after the projectile disappears (flew into an **instance hull**,
  `0x44d800`), or **immediately** on hitting an enemy of subtype 8/12, on leaving the world, or triggered by Boss2.
- **Explosion** `0x44d6e0`: script var := 1, then **radius 400** (`0x43c80000`) on **all Npcs of category 1 and 2** (Npc table `0x4c4e00`: the Perso
  and all enemies) via `vtbl[40]` and on all chests (120/121) via `vtbl[28]`; SoundFx **6**; effect `0x477060(kind = bomb+0x128, pos + (0,20,0), normal)`:
  **kind 0** for bomb thrower/enemies, **kind 1** (the big rocket explosion) for the cannon; bomb becomes invisible at once; back in the pool 0.5 s later.
  **Type-17 objects are NOT hit directly by the bomb** (correction to BONUS.md/OBJECTS.md, §10).
- **What kills Woody**: only the explosion. The Perso is not within the hit range of the bomb projectile (`T.hits_all = 0`, only enemy subtype 8/12),
  so **a bomb does not explode on contact with Woody**, not even from the bomb thrower. Within **400** (3D, bomb centre to Perso position) at the explosion:
  `Hit(0 damage, knockback away from the bomb)` + **`Kill(6)`** (`0x44d040`), except during the invulnerability after a respawn (`+0x270 > 0`).
- The bomb **bounces indefinitely** off terrain and instance press-nodes (`max_bounce = −1`, reflection without energy loss except for air drag 0.99
  per 1/60 s), falls at 3000 u/s² (max. 800 u/s downward), and only "lies" once it has been **5 frames in a row** within 1 unit above the ground; then it rolls
  to a stop with drag 0.95 per 1/60 s. Every new ground contact gives SoundFx **12**.
- **Appearance**: no rotation, no scale. The bomb is drawn **black** (vertex colour × 0) and **flashes red-lit** (+128 red) at a rate that
  speeds up as the fuse runs down (from 2 s remaining: every other frame). A **fuse** (a line along the object's own typecode-0 marker that burns down) with a
  **spark** (bank 0 frame 18) at the burning end. A bomb thrower emits a puff of smoke + debris at its own marker when firing.
- **Bomb thrower** (launcher type 42 after `1001 [inst, 0]`): every `T` s (1003) fires one bomb along the typecode-0 marker, **template 0**: 1500 u/s,
  gravity 15·200, fuse 2.0 s; SoundFx **14** on the launcher; firing animation 0 in `1002 [8]`·0.01 s. W2A: 20 of these (model 5), with no target
  (`1003 [inst, −1, −1, t]`), endlessly, every 1.0 .. 8.5 s; 3 run from level start, the rest only while Woody is inside a trigger volume (§8).

## 1. Class 40

### 1.1 Creation, pool, parking spot

- `1200 [inst, 40]` → SetTypeInstance `0x403440`: `new(0x134)` (`0x403afc`), ctor **`0x44d250`**: base Instance ctor `0x42e1a0`, `+0x104 = 0`,
  vtable `0x4aac74`; **if `[0x5e487c] < 16`**: `0x5e4880[n++] = this` (a 17th bomb doesn't make it into the pool and is never used); `[0x5e48c0] = 4.0`
  (**no reader** anywhere in the exe), `[0x5e48c4] = 2.0` (warning time, §2.2).
- Then (`0x403e3a..0x403e7a`, generic for all classes): take over old instance data (`0x42dfc0`), `vtbl[1]` = Init `0x44d2e0`,
  **`vtbl[17]` = Reset `0x44d320`** (removes it from the cells), and then **`0x407790(NULL)`** = back into the cell for its own position.
  **The parked bomb therefore stays visible and collidable at its `.ins` spot**, but is not "in use" (`+0x131 = 0`): no update, cannot be picked up.
  W2A parks them in sector 48 at z ≈ 2340..2425 (§8.1), at the edge of the world bbox; whether that spot is visible to the player: not verified (uncertain).
- Init `0x44d2e0`: FadeInst-Init `0x44e7c0` (`+0x100 = 100`, `+0xfc = 0`, `+0x6c = 0`), type word `(w & 0xfffffc23) | 0x23` = **category 3, subtype 1**
  (tested by projectile code `0x449c87` and `0x44a230`: `(w & 0x1f) == 3 && (w & 0x3e0) == 0x20`), `inst+8 |= 0x20` (no re-cell on animation),
  `+0xf0 |= 1` (SetFlags bit 1: extra shadow/mirror pass, INSTANCE.md §6), `+0x124 = 0`.
- Dtor `0x44d2a0` → `0x44d2c0`: vtable back, **`[0x5e487c] = 0`** (the pool disappears as soon as one bomb is destroyed, i.e. when the level is cleaned up), base dtor `0x42ff30`.

### 1.2 Vtable `0x4aac74` (28 slots; compared with FadeInst `0x4a9124`)

| slot | address | FadeInst | what |
|---|---|---|---|
| [0] | `0x44d2a0` | `0x404010` | dtor (§1.1) |
| [1] | `0x44d2e0` | `0x44e7c0` | Init (§1.1) |
| [2] | `0x42e2b0` | = | base update/draw; with arg 1 only the **animation clock** (INSTANCE.md: clock at most once per frame, `+0x58` = frame number) |
| [3] | `0x44e810` | = | FadeThink (fade + animation events) — **not** the bomb logic; that lives in `0x44d850` (§3) |
| [17] | **`0x44d320`** | `0x42e250` | **Reset** (§1.5) |
| [22] | **`0x451820`** | `0x44e8f0` | messages (§1.4) |
| [23] | **`0x44d990`** | `0x4600a0` (`return 4`) | `return +0x132 ? 1 : 8`; **no caller found** (no `call [reg+0x5c]` on an instance): uncertain |
| [26] | **`0x44d9a0`** | `0x430010` | **render colour**: black / red flashing (§3.4) |
| other | | = | base (`0x403fe0` = `return this+0x104` type word, collision `0x433140`, …) |

### 1.3 Fields (`+0x104..+0x133`; base/FadeInst fields: INSTANCE.md)

| off | type | meaning | writers / readers |
|---|---|---|---|
| +0x08 | flags | bit 0x40 **non-collidable**: set every frame as `+0x132 \|\| +0x133`, otherwise cleared (`0x44d892..0x44d8aa`); temporarily set during the projectile ray (`0x449da2`) | |
| +0x0c | vec3 | position = projectile position + (0, 1, 0) during flight (`0x449b52..0x449b7c`) | |
| +0x60 | vec3 | centre (= projectile position, for the cell) | `0x44d54b`, `0x449b8e` |
| +0x6c / +0xfc / +0x100 | f32 | FadeInst: transparency / target / speed | explosion `0x44e7f0(1.0, 1)`, end `0x44e7f0(0, 1)` |
| +0x104 | u32 | type word (category 3, subtype 1) | Init |
| **+0x108** | int | **state** 0..6 (§3.2) | |
| +0x10c | f32 | **fuse** (s) = `T.life` | `0x44d4dd` |
| +0x110 | f32 | **warning time** = `min([0x5e48c4] = 2.0, fuse)` | `0x44d4e3..0x44d508` |
| +0x114 | f32 | time since start (s); set to 0 at the explosion and then counts the 0.5 s aftermath | `0x44d588`, `0x44d886`, `0x44d7dd` |
| +0x118 | f32 | blink-phase accumulator (render colour) | `0x44d598`, `0x44da87`, `0x44daad` |
| +0x11c | int | blink frame-swap 0/1 (render colour, fast phase) | `0x44d59e`, `0x44da57` |
| +0x120 | Launcher* | the launcher that fired it (otherwise 0) | `0x44d56f` (0), `0x4526e7` (L); **only reader**: the fuse effect `0x478e90` (muzzle smoke) |
| +0x124 | Proj* | the carrying projectile (`0x4490a0`) | `0x44d5b8`; 0 after the explosion (`0x44d7ca`) and in Reset |
| +0x128 | int | **explosion kind** for `0x477060` (4th argument of `0x44d5d0`) | `0x44d5a4`; readers `0x44d754`, `0x44d785` |
| +0x12c | int | **script variable** (VM id), −1 = none; gets 1 on explosion/cleanup | `0x44d569`; `0x44d6f3`, `0x44d373` |
| +0x130 | u8 | unused (no reference in `0x44d250..0x44db50`) | |
| **+0x131** | u8 | **in use** (pool-free = 0) | `0x44d575` (1), Reset (0) |
| **+0x132** | u8 | **held** by the Perso (BOMB_CARRY.md): projectile stands still, non-collidable | `0x463508` (1), `0x44d3ca`/`0x44d57c`/Reset (0) |
| **+0x133** | u8 | **ridden** (bomb cannon): non-collidable, **cannot be picked up** (`0x4634ab`) | `0x45343a` (1), `0x44d582`/Reset (0) |

### 1.4 Messages `0x451820` (vtbl[22])

Only **29** (`0x451824`): `this->vtbl[17]()` = Reset, returns 0. All other messages → `0x44e8f0` (FadeInst 56/57, then the base handler).
**1090 is not an instance message** but a game message in the VM handler `0x444870` (`0x444fba`, BOMB_CARRY.md). In W2A the bombs only ever get `1200 [inst, 40]`.

### 1.5 Reset and cleanup

```c
void Bomb_Reset(Bomb *b) {                       /* 0x44d320 = vtbl[17]; also message 29, cannon Reset 0x452ae0, end of state 6 */
    if (b->proj) { if (b->proj->active) Proj_Kill(b->proj); b->proj = NULL; }        /* 0x4490e0 */
    b->state = 0; b->in_use = b->held = b->ridden = 0;
    Inst_RemoveFromCells(b);                     /* 0x407850: invisible, +0x1c = +0x18 = -1; position stays as is */
}
void Bomb_Discard(Bomb *b) {                     /* 0x44d370 */
    if (b->var >= 0) SetVar(b->var, 1);          /* 0x443ca0 */
    b->vtbl[17]();                               /* Reset: NO explosion */
}
void Bombs_DiscardAll(void) {                    /* 0x44db10: all bombs with +0x131 */
    for (i < [0x5e487c]) if (pool[i]->in_use) Bomb_Discard(pool[i]);
}
```
`0x44db10` is called by **Perso-Reset `0x44ab20`** (`0x44ad79`, so on every respawn: all flying/lying/carried bombs disappear without an
explosion, and their script vars become 1 so a dispenser can start again) and by Boss2 (`0x40e6dc`).

## 2. Starting a bomb

### 2.1 `Bomb* Bomb_Start(T*, u8 ground, int var, int kind)` `0x44d5d0` (cdecl)

```c
for (i = 0; i < [0x5e487c]; i++) if (!pool[i]->in_use) break;                    /* 0x44d5e6 */
if (i == n || !pool[i]) {
    Log("Pas de bombes, ou plus assez de bombes dans ce niveau...");            /* 0x4b3ad4 via 0x462c60 = empty function: nothing visible */
    if (var >= 0) SetVar(var, 1);                                               /* 0x44d612: the script does not wait forever */
    return NULL;
}
pool[i]->Launch(T, ground, var, kind);                                          /* 0x44d4d0 */
return pool[i];
```
A bomb in state 6 (exploded, still 0.5 s left) still counts as in use. Bomb throwers and the dispenser compete for the same 16.

### 2.2 `Launch(T*, ground, var, kind)` `0x44d4d0` (thiscall, `ret 0x10`)

```c
b->fuse = T->life;  b->warn = min(2.0f /*[0x5e48c4]*/, b->fuse);
b->pos = T->pos;
if (ground) { GetHeight(&b->pos, -1, 1); b->pos.y = [0x53a568] + 1.0f; }       /* 0x435650; 0x4a900c = 1.0 (only the 1090 dispenser) */
b->centre = b->pos;  Inst_Recell(b, NULL);                                      /* 0x4077f0: in the cell = visible at the start spot */
b->var = var;  b->launcher = NULL;  b->in_use = 1;  b->held = b->ridden = 0;
b->t = 0;  b->state = 1;  b->blink_acc = 0;  b->blink_n = 0;  b->kind = kind;
T->carried = b;                                                                 /* 0x44d5ab: T+0x5c, in the caller's copy */
b->proj = Proj_Alloc(T);                                                        /* 0x4490a0 -> 0x449130: visual 4 = no sound, no visual */
if (!b->proj) b->vtbl[17]();                                                    /* all 200 projectiles busy: bomb immediately freed again (return value stays b!) */
```
Rotation (`+0x28..+0x48`) is never set: the bomb keeps the orientation from the `.ins` and **does not turn**.
The projectile starts at `T.pos` with `dir0 · T.speed`; the bomb thus sits exactly at the marker's start point.

### 2.3 Callers and their template

| caller | `T` | ground | var | kind | afterwards |
|---|---|---|---|---|---|
| **launcher `Fire`** `0x4526db` | `L+0x108` = template 0 (via `1001 [inst,0]`) + 1002 parameters; `pos`/`dir0`/`target` set by Fire (§6) | 0 | −1 | **0** | `b+0x120 = L`, SoundFx 14 on L |
| **bomb cannon** type 21 `0x45342c` | template 0; `gravity = 0`, `speed = +0x11c`, `damp_air = 1.0`, `life = +0x114`, `pos`/`dir0` = marker 0 (§7) | 0 | −1 | **1** | `b+0x133 = 1`, `cannon+0x168 = b`, SoundFx 14 on the cannon |
| **1090** (VM `0x444fba`) | own block (BOMB_CARRY.md) | **1** | from the message (`edi`) | `esi`, which in the same function serves as zero for the block (`0x444e85..`) → presumably **0** (uncertain) | — |
| enemy `0x411e44` (`0x412070`) / `0x413516` (`0x413713`) | template (not traced) | 0 | −1 | 0 | returns `b != 0` (bomb-throwing enemies; not checked in the W2A script) |

**Template 0** (`0x448c70`, read line by line: `[0x5d7bc0..0x5d7c0c]`): radius **30**, gravity **15** (×200 = 3000 u/s²), speed **1500**,
drag ground **0.95** / air **0.99** per 1/60 s, lifetime **2.0**, damage **1000**, max. bounces **−1**, target 0, aim height 125, steer 0.025 / 0 /
times 2.0 / 0, bounds 0.7 / 0.7, owner 0, carried 0, visual **4** (none), `hits_all` **0**.

### 2.4 Relaunching from the hand: `0x44d3a0(T*)` and `0x4492d0`

Only if `+0x124`: `held = 0`; local block = ctor values of `0x44a260` inline; `T == NULL` → template 0 with `dir0 = (0, −1, 0)` (**no caller passes
NULL**: `0x4638a0` and `0x463dc0` both supply a block), otherwise a copy of `*T`; `T.pos = bomb.pos`; `0x4492d0(proj, &T)`.
`0x4492d0` = reinit **without** sound/visual: copy block, `press_frames = grounded = bounced = 0`, `pos = T.pos`, `dir = normalize(T.dir0)`,
`speed = T.speed`, `vel = dir·speed`. **Not** reset: age, bounce counter, generation, active flag. The passed block must contain `carried = bomb`,
otherwise the projectile releases the bomb (the Perso takes care of that; BOMB_CARRY.md). The bomb timers keep running: the fuse doesn't pause in the hand.

## 3. Per frame

### 3.1 Place in the frame (PERSO_FRAME.md §1)

Step 18 `0x401d7d`: **`0x44d820`** = for all 16 pool bombs `0x44d850` (before the world render, before the VM tick). Step 32 `0x401e7e`: projectiles
`0x4490f0` (the flight, §5), after the VM. The FadeThink `vtbl[3]` runs with the normal instance think steps.

### 3.2 `Bomb_Update` `0x44d850` (jump table `0x44d970` on `state − 1`)

```c
if (!b->in_use) return;
b->vtbl[2](1);                                   /* animation clock (model 26 has 1 animation of 10.0 s; what it does: uncertain) */
if ([0x5e48cc] /*pause*/) return;
b->t += dt;                                      /* [0x509adc]+0x38 */
if (b->held || b->ridden) b->flags8 |= 0x40; else b->flags8 &= ~0x40;
switch (b->state) {
case 1: CheckProj(b); b->state = 2; Fuse_Create(b); break;                  /* 0x44d8c4; 0x478e40 = fuse effect, §3.4 */
case 2: CheckProj(b); if (b->fuse - b->warn <= b->t) b->state = 3; break;   /* 0x44d8e0 */
case 3: CheckProj(b); if (b->t >= b->fuse) b->state = 4; break;             /* 0x44d90c */
case 4: b->state = 5; break;                                                /* 0x44d932 */
case 5: Bomb_Explode(b); break;                                             /* 0x44d93e -> 0x44d6e0, sets 6 */
case 6: if (b->t >= 0.5f /*0x4a9014*/) { Fade_Set(b, 0, 1); b->vtbl[17](); } break;   /* 0x44d947: visibility back to 0, Reset -> pool */
}
void CheckProj(Bomb *b) { if (b->proj && !b->proj->active) b->state = 4; }  /* 0x44d800: projectile gone -> ignite */
```
- State 1 lasts exactly one update; a 4 from `CheckProj` gets overwritten directly by 2 while still in state 1, and by 3 while in state 2 (one frame's delay).
- States 2 and 3 behave identically; the only difference: Boss2 only makes bombs in **state 2** explode (`0x40e9ff`, §4.5). With a 2.0 s fuse
  (bomb thrower), `fuse − warn = 0`, so state 3 is reached from the second update onward.
- **Timeline**: start (frame F) → F+1 state 2 → … → first update with `t ≥ fuse` → 4 → +1 frame 5 → +1 frame explosion. So **fuse + 2 frames**
  (at 60 fps ≈ 2.03 s for the bomb thrower, ≈ 3.23 s for the cannon). Then 0.5 s invisible, then free.

### 3.3 Collidable, pickupable

While in use and not held/ridden, the bomb is a **collidable instance** (model 26: one hull node, one press node): Woody can walk into it/
stand on it (uncertain how that plays out in practice). Can be picked up (`0x463446`) if `in_use && !ridden` and within range (BOMB_CARRY.md) — the state is
**not** tested, so in theory this also works during the invisible 0.5 s after the explosion (uncertain whether that is actually reachable in play).

### 3.4 Appearance

**a. Render colour `vtbl[26]` `0x44d9a0`** (every time the bomb is drawn; modes LIGHTING.md/ROCKET.md §5.2: `[0x5ac850]` 1 = vertex colour
**multiply** `0x43bdfc`, 2 = **add** `0x43bdd9`, then clamped to 255 `0x43be20`):

```c
if (paused) { mode = 1; col = (0,0,0); return; }                                   /* 0x44dae3 */
float rem = b->fuse - b->t;  float A, B = 0.1f;                                    /* 0x3dcccccd */
if      (rem >= 4.0f) A = 0.5f;                                                    /* 0x4a94c0, 0x4a9014 */
else if (rem >= 3.0f) A = 0.2f;                                                    /* 0x4a988c, 0x4a9760 */
else if (rem >= 2.0f) A = 0.15f;                                                   /* 0x4a9870, 0x4aa1c8 */
else if (rem >= 0.0f) A = B = 0;                                                   /* 0x44da2e */
else                  A = 0.08f;                                                   /* 0x4aace4 */
int lit;
if (A + B < dt) { lit = (++b->blink_n == 2); if (lit) b->blink_n = 0; }            /* every other frame */
else { b->blink_acc += dt;
       if (b->blink_acc >= A) { b->blink_acc -= A; lit = 1; } else lit = (b->blink_acc <= B); }
if (lit) { mode = 2; col = (128, 0, 0); }                                          /* 0x44dab5: +128 red onto the lit colour */
else     { mode = 1; col = (0, 0, 0); }                                            /* vertex colour × 0 = black */
[0x5ac860] = 0;
```
So: **black, with red flashes of 0.1 s** every 0.5 s (≥ 4 s left), every 0.2 s (3..4 s), every 0.15 s (2..3 s), and in the **last 2 s every other
frame**. Bomb-thrower bombs (fuse 2.0) therefore flicker throughout their whole flight. After the fuse (`rem < 0`, states 4/5) always red. Whether "× 0" really
produces fully black in the game (texture × vertex colour) has not been visually checked (uncertain); the code allows no other reading.

**b. Fuse and spark** (`0x478e40`, on the transition 1 → 2): one effect record (pool `[0x5e823c]+0xdb8`, lifetime 10000 s, update **`0x478b70`**, `+8 = bomb`,
`+0x34 = proj->generation`, `+0x38 = (b->launcher != 0)`, `+0x1c = normalize(v1 − v0)` and `+0x10 = v0 + dir·10` from **marker 0 of the launcher**).
Per frame `0x478b70`: stop (record freed) if the bomb is no longer in use or its clock didn't run this frame (`bomb+0x58 != frame`, `0x478ba2`);
jump table `0x478e18` on the bomb state: 0, 4, 5, 6 → only check (projectile gone or different generation → free record); 1 → nothing; **2, 3 → draw**:
```c
if (!rec->puffed && rec->from_launcher) {                                        /* once: muzzle smoke */
    SmokeRing(&rec->muzzle, &rec->dir, 0, 0.25f, 1.5f);                          /* 0x476140: 53 dark clouds on a ring of 200 round the muzzle (PARTICLES.md 3) */
    rec->puffed = 1;  FxAdd(0x478aa0, 0.15 s, rec->muzzle);                      /* smoke puff: frame 24, size 80, white, alpha 0.7·cos(u·π/2), flag 0xb (non-additive) */
}
Vec3 v[2]; GetVector(bomb, 0, v, 0);                                             /* first typecode-0 marker of model 26 (node 5 or 6: uncertain which comes first) */
float f = b->t / b->fuse;
Line(v[0], v[1] - (v[1]-v[0])·f, flags 0xc00, rgba0 (0.5,0.5,0.5,1), rgba1 (0.2,0.2,0.2,1), half width 1.0);   /* 0x471a10: the fuse burns from v1 towards v0 */
Sprite(pos = v[1] - (v[1]-v[0])·f, frame 18 (0x10012), white, alpha 0.7, size 5 + rand·10, rot rand·511, flags 7);   /* 0x478d40..0x478de3: spark, additive */
```
(`0x4a9750` = 10, `0x4a9884` = 5, `0x4abc90` = 511; `rand` = `0x43ff40`.) No muzzle smoke for cannon and dispenser bombs.

**c. Further**: no spin, no scale pulse, no alpha flicker; the model animation keeps running (clock via `vtbl[2](1)`, both in `0x44d850` and in the
projectile update `0x449417` — the clock only advances once per frame, `+0x58`); SetFlags bit 1 (shadow pass).

### 3.5 Sound (SOUND.md §5; all 3D)

| id | when | source | address |
|---|---|---|---|
| 14 | bomb fired (only bomb thrower and cannon, not the dispenser) | the launcher / the cannon | `0x4526f6`, `0x45344a` |
| 12 | every new ground contact (first frame on the ground after a frame in the air: every bounce) | the bomb | `0x449c24` |
| 6 | explosion (only if the projectile still exists, which is normally the case) | the bomb | `0x44d730` |

No fuse hiss: there is no loop sound (`0x468e50`) in the bomb code. SoundFx 13 (ref 6, loop) has no caller — possibly the intended hiss (uncertain).

## 4. Explosion

### 4.1 `Bomb_Explode` `0x44d6e0`

```c
if (b->state == 6) return;                                        /* idempotent */
b->state = 6;
if (b->var >= 0) SetVar(b->var, 1);                               /* 0x44d70a */
Bomb_Blast(b);                                                    /* 0x44d650, §4.2: before sound and effect */
if (b->proj) {
    SoundFx(6, b);                                                /* 0x44d730 */
    Vec3 p = b->proj->pos + (0, 20.0f /*0x4a9994*/, 0);
    Explosion(b->kind, &p, b->proj->bounced ? &b->proj->plane_n /*P+0xf0*/ : NULL);   /* 0x477060 */
    if (b->proj->active) Proj_Kill(b->proj);                      /* 0x4490e0 */
    b->proj = NULL;
}
b->t = 0;  Fade_Set(b, 1.0f, 1);                                  /* 0x44e7f0: transparency and target immediately 1.0 = not drawn (> 0.98) */
```

### 4.2 `Bomb_Blast` `0x44d650`: radius 400, two lists

```c
for (i = 0; i < [0x4c5318]; i++) {                                /* Npc table 0x4c4e00: ALL Npcs (Perso + all enemies), not actor list 1 */
    Npc *n = 0x4c4e00[i];
    if (cat(n) == 2 || cat(n) == 1) n->vtbl[40](&b->pos /*+0xc*/, 400.0f);   /* 0x44d68d */
}
for (i = 0; i < [0x5e58b0]; i++) chest[0x5e581c + i]->vtbl[28](&b->pos, 400.0f);   /* 0x44d6bd: class 120/121 0x451770 (BOMB_CARRY.md) */
```
- **Perso** `vtbl[40]` = `0x44d040`: `|pos − Perso+0xc|² < r²` ⇒ direction `normalize_xz(Perso+0x1f4 − pos)` (y = 0), **`Hit(0, 0, &dir, 0, 0)`**
  (0 damage: knockback/hit anim, `0x44d118`), rumble `0x44d1b0(+0x1d4, +0x1d0)`, **`Kill(6)`** (`0x44d139`) → PERSO_DEATH.md §3.1: anim 0x2b,
  fade after 2.5 s; ignored if `+0x270 > 0` (1.0 s invulnerable after respawn) or already dead. The radius is **3D** and the same **400** for the Perso.
- **Enemies** `vtbl[40]` = `0x41ae20` (ENEMY.md): within r ⇒ `vtbl[39](0, hp, …)` = instant death — here for **all** enemies in the level
  (the rocket uses actor list 1 and thus barely hits anyone; the bomb uses the full Npc table).
- **Chests** 120/121: `0x451770(pos, r)`: not yet hit (msgmask 0x20) and within r ⇒ blown open (BOMB_CARRY.md / BONUS.md §7).
- **Not affected**: class 17 (not an Npc: ctor `0x40c3d0` → base `0x42e1a0`; vtable `0x4a95dc` has 28 slots, there is no slot 40), type-42/21 objects,
  other bombs (no chain reaction), regular instances.

### 4.3 Effect `0x477060(kind, pos, normal)`

| kind | who | records (pool `[0x5e823c]+0xdb8`, 80 B, max 2000) |
|---|---|---|
| **0** | bomb thrower, enemies, (dispenser: uncertain) | `0x4765f0` 0.25 s · `0x476710` 0.3 s · `0x476cd0` 0.2 s · shrapnel `0x476140(pos, &n, 0, 0.25, 1.5)`; `n` = normal or (0, 1, 0) |
| **1** | bomb cannon | like the rocket (ROCKET.md §5.3): `0x476b50`, `0x476cd0`, `0x4762e0` R 1400, `0x4762e0` R 400 |
| 2 | (missile, not the bomb) | `0x4762e0` R 400 |

Kind 0 read in detail (`0x4771dd..0x477349`, `0x4765f0`, `0x476710`):
- `0x4765f0` (0.25 s, `u = t/0.25`): sprite at pos, **yellow** (1, 1, 0), alpha 1, frame **4** (`0x10004`), mode 0x12, flags 3 (additive);
  size `−500·c³` with `c = costab[(128 + trunc(511u)) & 511] ≈ −sin(2πu)` → `≈ 500·sin³(2πu)` (negative in the second half: how the primitive draws a
  negative size is uncertain). Plus `0x498790(…, pos, colour (255,255,255), size + 100)` (`0x4a9010` = 100): a dynamic light that the original registers but never draws
  (LIGHTING.md §7); the port registers it (`bombs_draw`) and draws it only with `WOODY_DYNLIGHT=1`.
- `0x476710` (0.3 s): **flat ring on the ground**: quad with normal `n` (`S+0x230`), grey 0.8, alpha `cos(u·π/2)`, size **`1300·u`** (`0x4a9860`),
  frame **24** (`0x10018`), flag 0xa (flat, non-additive).
- `0x476cd0` (dust burst: ~80 white clouds, 0.7 s) and `0x476140` (smoke ring kind 0: 53 dark clouds spreading to radius 200 across the normal, 1.5 s):
  decompiled in PARTICLES.md §5.1 and §3, ported (`fx_explode`).

### 4.4 After the explosion

The bomb sits invisible (`+0x6c = 1.0`) at the explosion spot; after 0.5 s (`t` counts again from 0) → transparency back to 0 and **Reset**: out of the cells,
`in_use = 0`. **It does not return to its parking spot**; it stays invisible until the next `Launch` moves and re-cells it.

### 4.5 Other triggers

- Boss2 `0x40e930` (`0x40ea47`): every pool bomb in **state 2** with `|bomb − bossPart|² < 40000` (`0x4a9890`, r = 200) → `Bomb_Explode`.
- Projectile: outside the world (§5.4) and hit on an enemy (§5.3) → direct `Bomb_Explode`.
- Respawn: `0x44db10` cleans up **without** an explosion (§1.5).

## 5. The projectile side of a bomb (PROJECTILES §2.2–2.5, here traced for `T.carried`)

### 5.1 Per frame `0x4493c0`

```c
P->age += dt;                                     /* end of lifetime does NOT apply to a carried bomb (0x4493f6) */
bomb->vtbl[2](1);                                  /* clock */
if (cat(bomb) == 3 && bomb->held) return;           /* 0x449440: held = projectile frozen (also no gravity) */
if (!P->grounded) { vel.y -= dt·T.gravity·200; if (vel.y < -800) vel.y = -800; damp = T.damp_air; }   /* template 0: 3000 u/s², 0.99 */
else              { vel.y = 0; damp = T.damp_ground; }                                               /* 0.95 */
vel *= pow(damp, dt·60);
(target seeking only if T.target: NULL for the W2A bomb throwers and the cannon)
Move(P, vel·dt);                                  /* §5.2 (calls HitActors first, §5.3) */
if (!P->active) return;
bomb->pos = P->pos + (0, 1, 0);  bomb->centre = P->pos;  Inst_Recell(bomb, &bomb->pos);                  /* 0x449b52..0x449bac */
if (bomb->cell < 0 || bomb->sector < 0) { Bomb_Explode(bomb); Proj_Kill(P); return; }                    /* 0x449bb7: out of the world */
if (Probe(&P->probe0, -1, &(P->pos + (0, r, 0)), r, bomb->id)) {                                       /* 0x436dc0, r = T.radius = 30 */
    if (P->press_frames == 0) SoundFx(12, bomb);
    P->press_frames++;
} else P->press_frames = 0;
P->grounded = (P->press_frames >= 5);             /* 0x449c37: determined AGAIN every frame — rolling off an edge = falling again */
if (P->grounded) { vel.y = 0; P->pos.y = [0x53a568]; }
```
`Probe` `0x436dc0` (EVENTS.md §3.2, **correction**): `GetHeight(point, −1, 1)` below the point, "on the ground" if `point.y − (ground + r) < 1.0` (`0x4a900c`),
i.e. **projectile point less than 1 unit above the ground surface** (terrain or instance press node; press events only with a press node that has a collision id).
Consequence: after every bounce the point is "on the ground" for one frame (SoundFx 12), it only truly comes to rest once it has stayed < 1 above the ground for 5 frames
in a row, i.e. once the bounce speed has become small (roughly < ~140 u/s at 3000 u/s²; derived, uncertain). Lying: rolls out horizontally at 0.95 per 1/60 s
(speed × 0.046 per second, roll-out distance ≈ `v / 3.08`).

### 5.2 `Move` `0x449cc0` for a bomb

`HitActors` first (§5.3); then (only when carried) the "platform" step via probe `P+0x24` (`0x437040`) — **a no-op**: both of its sphere queries are
`0x435b60`, a stub that only clears `[0x53a554]`, so the push is always 0 and the probe is reset; and no code reads the platform delta `0x436d20` of the press
probe `P+0` for a projectile (its readers are the enemy move, the path follower and the Perso). **A bomb is not carried by a moving platform**; while it
lies on one (5 frames within 1 unit) it only follows its height through `pos.y = ground`. Then the ray `0x4359b0(old, new, −1)`
with **`bomb->flags8 |= 0x40`** set while the ray runs (skip its own hull; old value restored, `0x449d97..0x449dcf`). Hit kind `[0x53a554]` (jump table `0x449ea0`):

| kind | what | bomb |
|---|---|---|
| 0 | nothing | flies on |
| 1 | **terrain** (gel) | **bounces** (`max_bounce = −1`: always), plane stored in `P+0xf0`, `bounced = 1` → normal for explosion kind 0 |
| 2 | **press node** of an instance | own bomb → ignore; otherwise bounce |
| 3 | `0x497ed0` answers **2** (`0x435b4a`): the segment **starts inside a press node** of an instance (`0x4330c0`, PROJECTILES.md §2.3), no plane, t = 0, that instance in `[0x53a560]` | own bomb → ignore; otherwise **projectile gone** (`0x449e82`) → `CheckProj` → **explosion 2 frames later** (ported, `inst_point_in_press`) |

Bounce `0x449eb0` (PROJECTILES §2.3): end point mirrored in the plane, speed preserved (no restitution loss); the only loss is the drag.
The mirror plane is the hit plane itself, not a plane through the backed-off point `h` (verified live, PROJECTILES §2.3).

**Correction (checked while porting, `0x4359b0` read line by line):** the ray sets hit kind 1 if `0x497ed0` answers 3 (world, plane from `0x4c4bc0`),
hit kind **2** if it answers 4 (an instance polygon: node `[0x4c4be0]` → `[0x53a58c]`, instance via `[0x4c4c0c]+0x40` → `[0x53a560]`) and hit kind 3
if it answers 2 — then without a plane and without an instance. Hitting an instance is always kind 2 = **bounce**; there is no separate hull kind. A bomb
therefore does not explode against a chest or a rock but bounces off it and comes to rest there. Answer 2 of `0x497ed0` = the start point lies behind every
plane of an instance's press node (see the table; a platform or crusher that moves onto a lying bomb sets it off 2 frames later). The port tests
(like all instance tests of the original) the press nodes (node kind 1) and leaves out hit kind 3. First attempt with hulls (kind 4) as "projectile gone":
every bomb-thrower bomb died instantly in its own launcher's hull.

### 5.3 `HitActors` `0x44a0a0` for a bomb

Actor list 1 from the previous frame (`0x4c52d8`, max. 8; the Perso, Boss2, enemy type 12). With `T.hits_all = 0` (`0x44a0bd`) **only actors of
category 2 with subtype 8 or 12** count — **the Perso (category 1) is never tested**. Hit radius `2·T.radius` = **60** against the actor's cylinder. On a hit:
- owner is a Perso/enemy (category 1/2): **no damage** (because `carried`), `owner->vtbl[41](1)`;
- otherwise (bomb thrower: owner = L, category 6; cannon/dispenser: owner 0): **`actor->vtbl[39](0, T.damage = 1000, &dir, pos, 0)`**;
- then always **`Bomb_Explode`** (`0x44a240`) and projectile gone.

W2A has 18 enemies of type 8 (`1200 [inst, 8]`); if subtype = enemy type (ENEMY.md §1: "subtype") a bomb thus explodes on such an enemy (uncertain: not
checked which subtype value type 8 sets in its type word).

### 5.4 When does a bomb explode — complete list

1. fuse runs out (`t ≥ fuse`) → +2 frames; 2. projectile disappears (hull hit §5.2, or — only if the bomb is no longer carried — lifetime) → +2 frames
(via `CheckProj`, only in state 2/3); 3. hit on an enemy subtype 8/12 → immediately; 4. outside the world → immediately; 5. Boss2 → immediately.
**Not** on contact with Woody, not on hitting terrain or press nodes, not via the player's attack (no caller).

## 6. The bomb thrower (launcher type 42, kind 0)

`Fire` `0x452560` (PROJECTILES §4.3), the bomb path read line by line:
```c
Vec3 v[2]; GetVector(L, 0, v, 0);                                   /* marker 0 in the CURRENT pose (before restarting the firing animation) */
Vec3 d = (L->aim && L->target) ? L->target->pos + (0, L->T.aim_h, 0) - v[0] : v[1] - v[0];   d = normalize(d);
L->T.target = L->target;  L->T.pos = v[0];  L->T.dir0 = d;          /* L+0x140, L+0x108, L+0x114 */
Bomb *b = Bomb_Start(&L->T, 0, -1, 0);                              /* 0x4526db */
if (b) { b->launcher = L; SoundFx(14, L); }                         /* 0x4526e7, 0x4526f6; no bomb = no sound */
if (L->anim != -1 && L->anim < nanims) { anim = L->anim; ...; SetAnimSpeed(L, len(anim)/4096 / L->anim_dur); }   /* also without a bomb */
```
- `L->T` = `1001 [inst, 0]` (`0x452330`: Reset `0x452260`, `+0x18c = 0`, `0x449070(0, L+0x108)` = template 0, **`L+0x160` owner = L**) plus the
  1002 parameters (PROJECTILES §3). In W2A only `[7, 0]` (firing animation 0), `[8, 50|70]` (0.5/0.7 s) and for launcher 13 `[2, 200]` (= 2.0 s,
  same as the template). Model 5 has one animation of 8.0 s (`32768/4096`); it therefore plays at speed 16 (0.5 s) or 11.4 (0.7 s).
- Target: `1003 [inst, target, count, t]` (`0x444d60..0x444d8f`: `+0xc` target, −1 → NULL; `+0x10` count; `+0x14` t·0.01). W2A: **always `[inst, −1, −1, t]`**
  (the source read "1, 1": the disassembly has `PUSH 1; NEG`) ⇒ **no target** (so no target seeking despite steer values 0.025/2 s in template 0),
  **endless**, period t. First bomb 1–2 frames after 1003, then every t (PROJECTILES §4.2).
- Direction = marker 0 of the launcher in its current pose. Model 5: marker node 9 (typecode 0), local (0, 119.6, 61.9) → (0, 234.3, 100.4), so ≈ **71°
  upward** relative to the local xz plane; node 4 (parent) has a rotation track, so the actual angle of the pose depends on it. **Measured in the port**: in the pose
  at the moment of firing, the direction is ≈ (0, 0.32, −0.95), so **≈ 19° upward** (W2A 13/14/15/339..341); the bombs fly through the ship's corridor. With 71°, 1500 u/s,
  3000 u/s², air drag 0.99 and a −800 clamp (own simulation at 60 fps): apex ≈ 270 above the muzzle, landing at start height after ≈ 0.9 s at ≈ **330**
  horizontal, then bouncing; explosion after 2.0 s, roughly 500–600 from the launcher (estimate, uncertain). A **mortar**, not a cannon.
- The launcher does nothing more with the bomb afterwards; `+0x120` is only read by the fuse effect (muzzle smoke, §3.4b).
- Empty pool → no bomb, no sound, the animation still plays.

## 7. Class 21 — bomb cannon, the rest of ROCKET.md §10.8

State 4 → 5 (`0x453279..0x45345a`, read line by line): `m = GetVector(cannon, 0)`, `dir = normalize(m[1] − m[0])`; block = ctor values inline, then
`0x449070(0, &T)` (template 0), then `T.pos = m[0]`, `T.dir0 = dir`, **`gravity = 0`**, **`speed = +0x11c`**, **`damp_air = 1.0`**, **`life = +0x114`**
(`0x4533c8..0x453425`). Unchanged from template 0: **`damp_ground = 0.95`**, radius 30, damage 1000, `max_bounce = −1`, target 0, owner 0, `hits_all = 0`.
`Bomb_Start(&T, 0, −1, 1)`; `cannon+0x168 = b`; **`b+0x133 = 1`** (crashes the original here if NULL); SoundFx 14 on the cannon.

- **Fuse** = `+0x114` = the entire flight time; warning = `min(2.0, +0x114)`. W2A 411: 3.2 s, 1300 u/s → straight line of ≈ 4160 units
  (unless it bounces). Red flashing per §3.4a: at 3.2 s first every 0.2 s, last 2 s every other frame.
- **At the same time as the cannon**: the cannon counts `t` in state 5 up to `+0x114 − 1.0` and in 6 up to 1.0 s (ROCKET §3) → state 7 at ≈ 3.2 s + 1–2 frames;
  the bomb explodes at 3.2 s + 2 frames. Which of the two comes first within the same frame depends on the order of the cannon think step vs. `0x44d820` (not
  determined, uncertain); it doesn't matter: seat position in states 5..7 = `bomb+0xc`, and that stays at the explosion spot after the explosion.
- **What the Perso sees**: if he stays seated, he is at distance ≈ 0 from the bomb centre → `Hit(0)` + **`Kill(6)`** (anim 0x2b), `SetState(2)` takes
  him out of state 8 (ROCKET §6.3). The cannon then turns itself back over 2 s (state 8 → 0) without the Perso. With invulnerability (`+0x270 > 0`,
  or the port with `WOODY_GOD`) he stays stuck in state 8: the cannon in state 8/0 no longer offers a seat position and dismounting is not allowed
  (ROCKET §0.4: port deviation "dismount when the object is in state 0").
- **Jumping off** (states 5..7, jump or attack): Woody falls; the bomb keeps flying. An explosion within 400 of wherever Woody is by then → Kill(6). At 1300 u/s the
  bomb is ≈ 0.3 s after jumping off already 400 further along (plus the fall); jumping off early enough is thus safe.
- **Bouncing during the ride**: terrain and press nodes → the bomb (and thus Woody, who follows the position but keeps the **cannon's rotation**) bounces off and
  keeps flying at 1300 u/s. An **instance hull** → projectile gone → **explosion 2 frames later** with Woody on it → Kill(6). An enemy subtype 8/12
  (radius 60) → instant explosion. Because `gravity = 0` and `damp_air = 1.0` the bomb flies exactly straight without obstacles; if it stays 5 frames within 1 unit
  above a ground surface, then **ground drag 0.95** applies and it slows down (unlikely with a cannon angled upward).
- `0x4634ab` (Perso pickup test `0x463446`): skips bombs with `+0x133` → the ridden bomb cannot be picked up. `0x44d894`: `+0x133` (or `+0x132`)
  → `flags8 |= 0x40`, the bomb is non-collidable for Perso movement while it is being ridden.
- **Cannon Reset** `0x452ae0` (next message 40 / 29) calls `bomb->vtbl[17]()` on `+0x168`. That pointer stays put after the explosion; if the bomb has meanwhile
  been reused by a bomb thrower, the cannon then resets **that one's** flying bomb (a genuine bug in the original; rare, not tested).

## 8. W2A data (`out/w2a_code.txt`, `extract/Data/W2A/W2A.ins`)

### 8.1 Bombs (16, model 26, all quat (1.414, 0, 0, −1.414), scale 1)

Objects/instances 187..194 at y 2289: (2075, 2343), (2160, 2343), (2253, 2341), (2346, 2341), (2072, 2421), (2163, 2421), (2248, 2424), (2349, 2424);
318..325 at y 2953: (2429, 2340), (2427, 2425), (2510, 2340), (2510, 2425), (2586, 2340), (2588, 2423), (2661, 2343), (2664, 2421) (x, z).
Script: only `1200 [inst, 40]`. Model 26: 1 animation (100 frames, 10.0 s), two typecode-0 markers (nodes 5 and 6, both local (13.4, 0, 56.0) → (34.8, 0, 79.0)),
one hull node, one press node.

### 8.2 Bomb throwers (20 × model 5; all `1200 [inst,42]`, `1001 [inst,0]`, `1002 [7,0]`, `1003 [inst,−1,−1,t]`, `1004` when var = 0)

| obj/inst | position | anim (s) | period t | start |
|---|---|---|---|---|
| 13 | (11961, 245, −24949) | 0.5 (+ `[2,200]`) | 2.5 | level start + 1.0 s, always |
| 14 | (11846, 250, −22702) | 0.5 | 6.5 | level start + 1.0 s, always |
| 15 | (13153, 268, −22698) | 0.5 | 8.5 | level start + 2.5 s, always |
| 25 | (−225, 746, −7030) | 0.5 | 3.0 | var 3: Woody enters/leaves volume 86 (obj 332, VOL_FLAG4/3) |
| 26 | (2686, 1345, −6993) | 0.5 | 3.0 | var 4: volume 239 (obj 548, entered/left) |
| 68 | (2318, 1346, −2989) | 0.5 | **1.0** | var 5: Woody in volume 87 (obj 333, VOL_FLAG5) |
| 69 | (5043, 747, −3152) | 0.5 | 3.0 | var 6: in volume 240 (obj 549) |
| 113 | (4738, 747, −14604) | 0.7 | 3.0 | var 7: in volume 83 (obj 328) |
| 314 | (5143, 748, −14886) | 0.5 | 3.0 | var 33: volume 83 (obj 328), 1003 1.5 s later, 1004 1.6 s later |
| 114 | (7954, 1346, −14578) | 0.5 | 3.0 | var 8: in volume 241 (obj 550), 1003 after 1.5 s, 1004 after 1.6 s |
| 315 | (7455, 1346, −14943) | 0.7 | 3.0 | var 34: volume 241 (obj 550) |
| 122 | (7411, 1346, −10787) | 0.5 | 3.0 | var 9: volume 84 (obj 329), 1.5 / 1.6 s delayed |
| 316 | (7100, 1346, −11014) | 0.5 | 3.0 | var 35: volume 84 (obj 329) |
| 123 | (10148, 746, −10780) | 0.5 | 3.0 | var 10: volume 242 (obj 551) |
| 317 | (10516, 747, −11121) | 0.5 | 3.0 | var 36: volume 242 (obj 551), 1.5 / 1.6 s delayed |
| 297 | (6476, 145, −25749) | 0.5 | 4.0 | var 32: volume 82 (obj 327), 1003 after 2.0 s |
| 326 | (6476, 145, −25394) | 0.5 | 4.0 | var 37: volume 82 (obj 327) |
| 339 | (10599, −1050, −24760) | 0.5 | 2.0 | var 47: volume 91 (obj 338), 1003 after 1.0 s |
| 340 | (8909, −1046, −24349) | 0.5 | 2.0 | var 48: volume 91, 1003 after 2.0 s |
| 341 | (10795, −1050, −23950) | 0.5 | 2.0 | var 49: volume 91, 1003 after 3.0 s |

VOL_FLAG5/4/3 = volume bit 0x20/0x10/0x08 (VM.md §3 opcode 29/31/32); in `src/ekovm.c` Perso-Enter sets 0x36, Perso-In 0x24, Perso-Leave 9, so 5 = "Woody
inside (this frame)", 4 = "entered", 3 = "left" (derived from the port VM, not re-checked in the exe). The portal objects set the var to 1 while Woody
is in the volume and to 0 when he leaves it → `1004` (for 114/122/314/317 delayed 1.6 s, so an already-started bomb still falls). Pairs in the same volume
(113+314, 114+315, 122+316, 123+317, 297+326, 339..341) fire **alternately** (offset by 1.5 s resp. 1.0 s).
Pool load: every bomb occupies a slot for ≈ 2.55 s (2.0 s fuse + 2 frames + 0.5 s). Launcher 68 alone keeps ≈ 2.6 occupied; 13/14/15 run for the whole level
(≈ 1.0 + 0.4 + 0.3). With the volume gates, W2A normally doesn't reach 16 (not simulated).

### 8.3 Other

- 12 launchers of **model 23** (246, 247, 330, 386..391, 544..546) get **no 1001** → template 1 (energy ball, PROJECTILES §5.1), only `1002 [2, 200|400]`:
  no bombs.
- Bomb cannon **411** (model 44, (9882, 139, −12797)): `55 [1, 320]` → 3.2 s, `55 [2, 1300]` → 1300 u/s, `45 [inst, 1]` (SetFlags). ROCKET §7/§8.
- Dispenser: object 536 sends `1090 [538, 0x20000be (var 190), 2000]` (BOMB_CARRY.md); chest 537 (type 121).

## 9. Recipe for the port

In `src/main_engine.c`, next to `Launcher`/`Shot` (l. ~1146–1230) and `Rocket` (l. ~1598–1700). A bomb is not a `Shot`: it has gravity, bounces, and its
own instance. The simplest approach is to embed the projectile inside the bomb (the original keeps it in the 200 pool, but nothing besides the bomb reads it).

```c
/* ---- class 40, the bomb (docs/BOMB.md) ------------------------------------------------------------------------ */
typedef struct { float radius, gravity, speed, damp_g, damp_a, life, damage; int hits_all; } BombT;
static const BombT BOMB_T0 = { 30, 15, 1500, 0.95f, 0.99f, 2.0f, 1000, 0 };          /* template 0, 0x448c70 */
typedef struct {
    Instance *inst; int state;                     /* +0x108: 0 free, 1 started, 2/3 fuse, 4/5 igniting, 6 exploded */
    float fuse, warn, t, blink_acc; int blink_n;   /* +0x10c +0x110 +0x114 +0x118 +0x11c */
    Launcher *launcher; int kind, var;             /* +0x120, +0x128 explosion kind, +0x12c script var (-1 none) */
    int in_use, held, ridden;                      /* +0x131 +0x132 +0x133 */
    BombT T; int p_active, press, grounded, bounced; Vec3 p, vel, n;   /* the carrying projectile */
    int puff_pending;                              /* fuse record 0x478e40: muzzle smoke once, only for a launcher's bomb */
} Bomb;
static Bomb g_bombs[16]; static int g_nbombs;

/* case 1200, type 40: 0x44d250 + Init + Reset + 0x407790: stays visible and solid where the .ins put it, not in use */
if (in->type == 40 && g_nbombs < 16) { Bomb *b = &g_bombs[g_nbombs++]; memset(b, 0, sizeof *b); b->inst = in; b->var = -1; }
/* case 29 on a bomb instance: bomb_reset(b). Level change: g_nbombs = 0 (dtor 0x44d2c0). Player respawn (0x44ab20): bombs_discard_all(). */

static void bomb_reset(Bomb *b)   { b->p_active = 0; b->state = 0; b->in_use = b->held = b->ridden = 0; b->inst->visible = 0; }   /* 0x44d320 */
static void bombs_discard_all(void) { for (int i = 0; i < g_nbombs; i++) if (g_bombs[i].in_use) {                                  /* 0x44db10 */
    if (g_bombs[i].var >= 0) game_var_set(g_bombs[i].var, 1); bomb_reset(&g_bombs[i]); } }

static Bomb *bomb_start(const BombT *T, Vec3 pos, Vec3 dir, int ground, int var, int kind)   /* 0x44d5d0 + 0x44d4d0 */
{
    Bomb *b = NULL; for (int i = 0; i < g_nbombs && !b; i++) if (!g_bombs[i].in_use) b = &g_bombs[i];
    if (!b) { if (var >= 0) game_var_set(var, 1); return NULL; }
    b->T = *T; b->fuse = T->life; b->warn = T->life < 2.0f ? T->life : 2.0f;
    Vec3 ip = pos;   /* `ground` snaps the INSTANCE only; the projectile starts at T.pos (verified live on the S2A dispenser) */
    if (ground) { int f; float gy = gel_floor_below(&L.gel, pos, 0, 1e5f, &f); if (f) ip.y = gy + 1.0f; }
    b->p = pos; b->vel = (Vec3){ dir.x * T->speed, dir.y * T->speed, dir.z * T->speed };   /* dir normalised by the caller */
    b->inst->position = ip; b->inst->visible = 1; b->inst->fade = b->inst->fade_target = 0;
    b->var = var; b->kind = kind; b->launcher = NULL; b->in_use = 1; b->held = b->ridden = 0;
    b->t = 0; b->state = 1; b->blink_acc = 0; b->blink_n = 0;
    b->p_active = 1; b->press = b->grounded = b->bounced = 0; b->puff_pending = 0;
    return b;
}

static void bomb_explode(Bomb *b, Player *pl)                                       /* 0x44d6e0 */
{
    if (b->state == 6) return; b->state = 6;
    if (b->var >= 0) game_var_set(b->var, 1);
    Vec3 c = b->inst->position;                                                     /* 0x44d650: radius 400 */
    if (pl && !pl->dead_kind && pl->invuln_respawn <= 0) {                          /* Perso 0x44d040 */
        Vec3 d = { pl->inst->position.x - c.x, pl->inst->position.y - c.y, pl->inst->position.z - c.z };
        if (d.x*d.x + d.y*d.y + d.z*d.z < 400.0f*400.0f) { float l = sqrtf(d.x*d.x + d.z*d.z);
            player_hit(pl, 0, l > 1e-3f ? (Vec3){ d.x/l, 0, d.z/l } : (Vec3){ 0, 0, 0 }); player_kill(pl, 6); } }
    enemies_blast(&g_enemies, c, 400.0f);                                           /* vtbl[40] 0x41ae20 of every enemy: dies inside r */
    chests_blast(c, 400.0f);                                                        /* 120/121, BOMB_CARRY.md */
    audio_fx(6, b->inst, &b->inst->position.x);                                     /* the original only while +0x124 exists: always, in practice */
    Vec3 e = { b->p.x, b->p.y + 20.0f, b->p.z };                                    /* the projectile point + 20, not the bomb */
    if (b->kind == 1) game_explosion(e); else blast_add(e, 400.0f);                 /* kind 0 = 0x4765f0 / 0x476710 / ..., section 4.3; placeholder */
    b->p_active = 0; b->t = 0; b->inst->fade = b->inst->fade_target = 1.0f;        /* invisible at once */
}

/* frame step 18, before the VM tick, not while paused */
static void bombs_update(float dt, Player *pl)                                      /* 0x44d820 / 0x44d850 */
{
    for (int i = 0; i < g_nbombs; i++) { Bomb *b = &g_bombs[i]; if (!b->in_use) continue;
        b->t += dt; b->inst->noncollide = b->held || b->ridden;
        int gone = !b->p_active;                                                    /* CheckProj 0x44d800 */
        switch (b->state) {
        case 1: b->state = 2; b->puff_pending = b->launcher != NULL; break;   /* 0x478e40: fuse record + muzzle smoke */
        case 2: if (gone) b->state = 4; if (b->fuse - b->warn <= b->t) b->state = 3; break;
        case 3: if (gone) b->state = 4; if (b->t >= b->fuse) b->state = 4; break;
        case 4: b->state = 5; break;
        case 5: bomb_explode(b, pl); break;
        case 6: if (b->t >= 0.5f) { b->inst->fade = b->inst->fade_target = 0; bomb_reset(b); } break;
        }
    }
}

/* frame step 32, after the VM tick (where launchers_update now runs) */
static void bomb_fly(Bomb *b, float dt, Player *pl, const GelFile *gel)             /* 0x4493c0 with T.carried */
{
    if (!b->p_active || b->held) return;
    float damp;
    if (!b->grounded) { b->vel.y -= dt * b->T.gravity * 200.0f; if (b->vel.y < -800.0f) b->vel.y = -800.0f; damp = b->T.damp_a; }
    else { b->vel.y = 0; damp = b->T.damp_g; }
    float k = powf(damp, dt * 60.0f); b->vel.x *= k; b->vel.y *= k; b->vel.z *= k;
    Vec3 a = b->p, e = { a.x + b->vel.x*dt, a.y + b->vel.y*dt, a.z + b->vel.z*dt };
    if (enemies_sweep_hit(&g_enemies, a, e, 2.0f * b->T.radius, /*only types 8 and 12*/ 1, b->T.damage)) { b->p = e; bomb_explode(b, pl); return; }
    /* never the player: hits_all == 0 */
    Vec3 n; float f = gel_ray_hit(gel, a, e, &n);
    if (f <= 1.0f) {                                                                /* terrain: bounce, unlimited (0x449eb0) */
        Vec3 d = { e.x-a.x, e.y-a.y, e.z-a.z }; float L = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z), s = L * f - 0.01f; if (s < 0) s = 0;
        Vec3 h = { a.x + d.x/L*s, a.y + d.y/L*s, a.z + d.z/L*s };
        float q = 2.0f * ((e.x-h.x)*n.x + (e.y-h.y)*n.y + (e.z-h.z)*n.z); Vec3 r = { e.x - q*n.x, e.y - q*n.y, e.z - q*n.z };
        Vec3 u = { r.x-h.x, r.y-h.y, r.z-h.z }; float ul = sqrtf(u.x*u.x + u.y*u.y + u.z*u.z), sp = sqrtf(b->vel.x*b->vel.x + b->vel.y*b->vel.y + b->vel.z*b->vel.z);
        if (ul > 1e-6f) b->vel = (Vec3){ u.x/ul*sp, u.y/ul*sp, u.z/ul*sp };
        b->p = h; b->bounced = 1; b->n = n;
    } else b->p = e;
    /* instance hulls (hit type 3): projectile gone -> the bomb goes off 2 frames later (b->p_active = 0); needs an instance ray in the port */
    b->inst->position = (Vec3){ b->p.x, b->p.y + 1.0f, b->p.z };
    if (gel_cell(gel, b->inst->position) < 0) { bomb_explode(b, pl); return; }
    int found; float gy = gel_floor_below(gel, (Vec3){ b->p.x, b->p.y + b->T.radius, b->p.z }, 0, 1e5f, &found);
    if (found && b->p.y - gy < 1.0f) { if (b->press == 0) audio_fx(12, b->inst, &b->inst->position.x); b->press++; } else b->press = 0;
    b->grounded = b->press >= 5; if (b->grounded) { b->vel.y = 0; b->p.y = gy; }
}
```
**Drawing** (per bomb in use, `state` 2/3, after the 3D scene like the lasers):
- instance tint (vtbl[26], §3.4a): new tint mode alongside `tint_red`: "× 0" (black) and "+ (128/255, 0, 0)" (add red), with the blink rules from §3.4a.
- fuse: `inst_vector(b->inst, 0, &v0, &dv)`, `v1 = v0 + dv`; `f = t/fuse`; line `v0 → v1 − (v1−v0)·f`, half width 1, colour 0.5 → 0.2 grey;
  spark `hud_world_fx(18, v1 − (v1−v0)·f, 5 + rand·10, rand·511/512, white, 0.7)` (additive).
- muzzle smoke once on the transition 1 → 2 if `launcher`: frame 24, 80 in size, 0.15 s, `alpha 0.7·cos(u·π/2)` at `m0 + normalize(m1−m0)·10` of the launcher.

**Bomb thrower** in `launcher_fire` and the messages:
```c
case 1001: l->kind = a1; l->life = a1 == 0 ? 2.0f : 15.0f; ...                   /* template 0 lives 2.0 s: that IS the fuse */
case 1002: n = 0 -> l->speed, 1 -> l->gravity, 4 -> l->damage (only kind 0 uses them; W2A only sends 2, 7, 8)
           n = 7 -> l->anim = v; n = 8 -> l->anim_dur = v * 0.01f;
static void launcher_fire(Launcher *l) {
    Vec3 p0, d; if (!inst_vector(l->inst, 0, &p0, &d)) return;  /* before restarting the anim */
    (aim as now) ; normalise d;
    if (l->kind == 0) { BombT t = BOMB_T0; t.life = l->life; /* + speed/gravity/damage from 1002 */
        Bomb *b = bomb_start(&t, p0, d, 0, -1, 0); if (b) { b->launcher = l; audio_fx(14, l->inst, &l->inst->position.x); } }
    else { ... existing Shot code ... }
    if (l->anim >= 0 && l->anim < nanims(l->inst)) { l->inst->anim = l->anim; l->inst->anim_time = 0;
        l->inst->anim_speed = anim_len(l->inst, l->anim) / l->anim_dur; }                /* 0x4526fb: the whole anim in anim_dur seconds */
}
```
**Bomb cannon** (type 21) in the `Rocket` state machine: same states, but in 4 → 5 no Fly/exhaust/sound 10/11 but instead
```c
Vec3 m0, md; inst_vector(in, 0, &m0, &md); normalise md;
BombT t = BOMB_T0; t.gravity = 0; t.speed = r->vmax; t.damp_a = 1.0f; t.life = r->fly_time;
r->bomb = bomb_start(&t, m0, md, 0, -1, 1); if (r->bomb) r->bomb->ridden = 1; audio_fx(14, in, &in->position.x);
```
state 5: only `t += dt` (to 6 at `t ≥ fly_time − 1`); 6: `t += dt`, no red, no explosion (to 7 at 1.0 s); 7: `q0 = quat(rot)`, `q1 = start_q`,
to 8; 8: slerp back over 2 s, then 0 (without Reset). Rocket-Reset: `if (r->bomb) { bomb_reset(r->bomb); r->bomb = NULL; }`.
Seat position for the player in states 5..7 = `r->bomb->inst->position` (otherwise marker 0), rotation = that of the cannon. The Kill(6) comes from `bomb_explode`.

**Check**: W2A, launcher 13 at (11961, 245, −24949) fires a black, red-flickering bomb with a fuse high into the air starting at 1.0 s every 2.5 s; it bounces
(a tick on every landing, SoundFx 12) and explodes 2.03 s after being fired (SoundFx 6, yellow flash + ground ring). Woody within 400 → Kill(6); walking into the bomb does
nothing. In the volumes near (2318, 1346, −2989), 68 fires every second.

## 10. Corrections to earlier documents

- **BONUS.md §7 / OBJECTS.md §3**: "all type-17 objects with state 1/2 get `vtable[+0xa0]`" is wrong: the first loop of `0x44d650` iterates over the **Npc table**
  `0x4c4e00` and the "1/2" is the **category** (Perso/enemy). Class 17 is not an Npc and has no slot 40; the bomb does not hit class 17 directly (how type 17 still
  breaks: BOMB_CARRY.md). "Update = `0x44e810` (fade)" → the bomb logic is `0x44d850` from `0x44d820` (frame step 18). `+0x10c/+0x110` = fuse / warning time.
- **ROCKET.md §7**: "`0x44d4d0`: … `+0x128 = 1`" — `+0x128` is the **explosion kind** (cannon: 1 = big explosion). "The bomb explodes … radius 400: type-17 objects" → see above.
- **EVENTS.md §4.2**: the rows `0x44d70a` and `0x44d380` are about **bombs** (`+0x12c` = script var of the bomb, pool `0x5e4880`), not about enemies; `0x44d380`
  kills nothing, it just cleans up bombs without an explosion. **EVENTS.md §3.2**: the ground test is `point.y − (ground + tol) < 1.0`, not `< 0`.
- **PROJECTILES.md §2.4**: `grounded` is redetermined every frame (not once), the probe test is "< 1 unit above a ground surface" (terrain counts too, not
  just press nodes) and SoundFx 12 sounds on every new contact. §2.5: "damage not applied because carried" only holds if the owner is a Perso/enemy;
  with a launcher or owner 0, `T.damage` (1000) IS applied.
- **SOUND.md §5**: id 6 "object/enemy destroyed (`0x44d730`)" = the bomb explosion.

## 11. Uncertain / not traced

1. Visual result of the render colour (× 0 = truly fully black?) and of sprite sizes < 0 in `0x4765f0`.
2. `0x476cd0` (dust burst) and `0x476140` (smoke ring) of explosion kind 0: resolved (PARTICLES.md §3, §5.1); `0x498790`: resolved, a dynamic light that is never drawn (LIGHTING.md §7).
3. `vtbl[23]` `0x44d990` (1/8): no caller found.
4. Which of the two typecode-0 markers of model 26 `GetVector(…, 0, …, 0)` picks (they sit at the same spot, so it has no consequence).
5. The actual firing angle of the model-5 launchers (node 4 is animated) and thus the throw distance (§6 is an estimate).
6. Order of the cannon think step relative to `0x44d820` within the same frame (§7); enemy subtype of type 8 (§5.3); the meaning of the VOL_FLAG bits comes from the port VM.
7. Whether the parked W2A bombs are ever on screen (sector 48); whether picking them up during the invisible 0.5 s after an explosion is possible (§3.3).
8. Nothing has been checked against the running original with `tools/wtrace.py`; good measurement points: SoundFx 14 → 6 (2.0 s + 2 frames), SoundFx 12 per bounce.
