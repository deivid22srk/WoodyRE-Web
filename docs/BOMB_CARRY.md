# BOMB_CARRY.md — Woody and the bomb: picking up, carrying, throwing (Perso state 6), the bomb dispenser (message 1090), crates (type 120/121) and class 17

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone). Every claim has an address; floats are read from
`.rdata` of the exe. "Uncertain" = not read line by line or not measured in the original.

This document covers what the **player** does with bombs and what bombs **break**. The bomb class itself (type 40: fuse, states 1..6, flight,
bouncing, explosion effect, blinking, launchers/bomb throwers, the bomb cannon type 21) is in **BOMB.md**; here only what is needed to
understand the player side. Also see: PROJECTILES.md (projectile pool, template 0, §2.4 carried instance), ROCKET.md §6-7 (Perso state 8,
bomb cannon), ENEMY2.md §4 (bomb thrower type 12, throwing back), BOSS14.md §9 (class 17), PERSO_JUMP.md / PERSO_MOVE.md (Jumper, Mover, keys).

## 0. Summary

* **Picking up** (`0x463430`, every frame in Perso state 0): attack key (action 6) *just pressed*, on the ground, no attack in progress, and a bomb
  with `+0x131` (in use) and without `+0x133` (ridden) within **269** units (3D, `P+4 + 200` = 69 + 200) of the feet → state **6**.
  No facing-direction test, no test on the bomb state: even a flying bomb (from a launcher or the bomb thrower) can be caught.
* **Carrying**: the bomb sits every frame at the position of the **top-level node with flags 0x80** (the "camera node") of Woody's model in the
  current `.ins` animation (`0x42feb0`/`0x42fa40`, the same track as camera mode 0x80), and takes over Woody's rotation. Carry pose: ± 117
  above the feet, 45 sideways, 7 forward (model 21, anim 47). Walking and jumping work **exactly as in state 0** (same `0x44bb20(1)` +
  `0x4624f0`); there is no speed difference and no jump restriction. However: no pecking/charge run (the attack trigger requires state 0).
* **Throwing**: attack *just pressed* in sub-state 2 and not crouching. On the ground: anim 0x43 (0.933 s, movement blocked), the bomb releases
  **0.467 s** after the start; in the air: anim 0x44 (0.6 s), releases after **0.3 s**. Throw = the bomb's running projectile restarted with
  direction `normalize(look.x, 1, look.z)` (45° upward) and speed **1000**, gravity from template 0 (3000 u/s²), owner = Woody,
  target = the previous owner (a bomb thrown back by the bomb thrower thus seeks its thrower). Flight distance on flat ground ≈ 340.
* **Dropping without throwing** (`0x463c90`, speed 0, straight down): on any state switch away from 6 while holding the bomb (death, look-around,
  climbing wall, script), on Reset, and during a **long fall** (anim .ins 66).
* **Fuse in his hands**: the fuse just keeps running (`bomb+0x114 += dt`, `0x44d886`); if it explodes, the explosion (radius 400) hits Woody
  (`0x44d040`: `Kill(6)`), unless he is invincible. Hits (`Hit`) do **not** make him drop the bomb.
* **Message 1090 `[inst, var, t]`** (`0x444e00`): takes the first free bomb from the pool of 16, places it at the type-code-0 marker of `inst`
  (set on the ground), fuse `t · 0.01` s, initial speed **100** along the marker; `var := 1` as soon as it explodes (or immediately, if there's no free bomb).
* **Crates** (type 121; type 120 doesn't occur in any level): a bomb explosion within **400** of the crate's origin → animation 0 once,
  explosion effect kind 1, fades out over **2 s** (from 1.8 s no longer collidable), **msgmask 0x20**. They hide a unique item (type 36),
  an extra life (30), or a type-35 bonus. 120 and 121 differ only in the subtype of the typeword (1 resp. 2); no reader found.
* **Class 17 is NOT a "breakable object for bombs"**: it is the towed instance of a boss (W1B 404 = Buzz's vehicle).
  Bombs don't affect it. OBJECTS.md §3, BONUS.md §7 and PROJECTILES.md §1.2 had this wrong (§7).
* What a bomb explosion hits (`0x44d650`): **all actors of category 1 or 2** (Woody and enemies, `vtbl[40](pos, 400)`) and **all crates**
  (`vtbl[28](pos, 400)`). Nothing else; there are no destructible walls in the world geometry.

## 1. Perso state 6 (carrying a bomb)

### 1.1 Fields

| field | type | meaning | writers / readers |
|---|---|---|---|
| Perso+0x590 | Bomb* | the carried bomb (0 = none) | `0x463502` (pickup), `0x4638cb`, `0x463bd8`, `0x463dd6` (0), `0x44a50c` (ctor 0) |
| Perso+0x594 | u8 | "bomb in hands" (follow position) | `0x463512` (1), `0x4638a7`, `0x463dc7`, `0x44acaa` (0) |
| Perso+0x58c | int | sub-state 0..8 (§1.3) | `0x463518` (0), jump table `0x463c28` |
| Perso+0x598 | f32 | timer of the sub-state | `0x46390e`, `0x4639a5`, `0x463a84`, `0x463b96` |
| bomb+0x131 | u8 | in use (started by 1090 / a thrower) | `0x44d575` (1), `0x44d34f` (0, Reset) |
| bomb+0x132 | u8 | held by Woody | `0x463508` (1), `0x44d3ca` (0 on drop) |
| bomb+0x133 | u8 | ridden (bomb cannon type 21); such a bomb cannot be picked up | ROCKET.md §7, test `0x4634ab` |
| bomb+0x124 | Proj* | the bomb's running projectile (PROJECTILES.md §1.2) | `0x44d5b8` |

### 1.2 Picking up `0x463430(Perso)` (only caller `0x44bae1` in `0x44ba70`, only if state == 0)

```c
bool Perso_TryPickBomb(Perso *p) {                                   /* 0x463430 */
    if (p->state == 6) return true;                                  /* 0x463436 (dead branch: the caller requires state 0) */
    if (!JustPressed(6) || !OnGround(p) || p->atk /*+0x5b4*/) return false;   /* 0x46344e, 0x44bcf0, 0x46346a */
    float r = p->P.radius /*+0x114 = 69*/ + 200.0f;  r *= r;         /* 0x463478..0x463492, 0x4aa164 = 200.0 */
    for (int i = 0; i < g_nbombs /*0x5e487c*/; i++) {
        Bomb *b = g_bombs[i];                                         /* 0x5e4880[16] */
        if (!b->in_use /*+0x131*/ || b->ridden /*+0x133*/) continue;  /* 0x4634a1, 0x4634ab */
        if (dist2(p->pos /*+0x1f4*/, b->pos /*+0xc*/) < r) {          /* 3D, 0x4634b5..0x4634ef */
            p->bomb = b; b->held = 1;                                 /* 0x463502, 0x463508 */
            p->carrying = 1; p->bsub = 0;                             /* 0x463512, 0x463518 */
            SetState(p, 6);  return true;                             /* 0x463522 */
        }
    }
    return false;
}
```
The result is ignored. Right after, `0x44ba70` calls the attack trigger `0x457330`, which requires state 0 (`0x4573ad`): the same
key press thus doesn't start a peck. Note: `0x464ef0` (grabbing a climbing wall, runs earlier in the frame) has no state test at its
start (`0x464ef0..0x464f5f` read): if Woody's beak faces a peckable wall (radius 169), the climbing wall wins.

No test on the bomb's state: even a bomb in state 6 (exploded, 0.5 s fade-out, `+0x131` is still 1 then) can be "picked up"; its
projectile is already gone by then (`+0x124 = 0`, `0x44d7ca`) and throwing does nothing (`0x44d3bd`). Edge case, uncertain whether this occurs in practice.

### 1.3 Per frame: `0x463530(Perso)`

Called via `0x44b480` (only state 6 and `Perso+0x690 == 0`) from the main loop at **`0x401d69`**, after the animation/matrices of the
Perso instance (`0x401d5e`) and before the instance updates `0x42b400` and the bomb updates `0x44d820` (PERSO_FRAME.md §1, step 16).

**Part A: the bomb in his hands** (only if `+0x594`):

```c
int ins = p->inst.slot0;  /* +0xb0: current .ins animation */       bool release = false;
switch (ins) {                                                       /* jump table 0x463bfc, index ins - 0x3d */
case 61: release = p->bt < 0.46667f; break;   /* 0x4635cb, 0x4ab7b0   ground throw (log. 0x43) */
case 62: release = p->bt < 0.3f;     break;   /* 0x4636d5, 0x4aab98   air throw (0x44) */
case 65: release = p->bt < 1.9f;     break;   /* 0x463595, 0x4ab7ac   log. 0x4d: death kind 1 in state 6 - dead code (§1.8) */
case 66: release = p->bt < 0.16667f; break;   /* 0x4635b0, 0x4ab7a8   log. 0x4b: long fall */
case 69: case 70: release = p->bt < 0.46667f; break;   /* 0x46357a       log. 0x27/0x28: death in state 6 - dead code */
case 71: release = p->bt < 0.23333f; break;   /* 0x4636c4, 0x4ab7a4   put down (0x46) - unreachable, §1.3 part B */
}                                                                    /* 63, 64, 67, 68 and everything else: keep holding */
if (release) { Throw(p); continue_with_part_B; }                    /* 0x4635e2, below */
else if (Inst_HasCameraTrack(&p->inst, ins)) {                       /* 0x42feb0: top-level node flags 0x80 has a pos track for ins */
    Vec3 hand;  Inst_CameraEval(&p->inst, &view, &hand, NULL);       /* 0x42fa40 -> 0x42fa80 (CINEMATIC.md §4): track position * instance matrix */
    b->pos = hand;                                                   /* 0x46371c..0x463736 */
    if (b->proj) b->proj->pos = hand;                                /* 0x46374c..0x463776: the (frozen) projectile follows */
    Bomb_Recell(b, hand + (0, 30, 0));                               /* 0x463c50: 0x4077f0, only if bomb+0x1c != -1 (0x4a9740 = 30.0) */
    b->rot = p->rot;                                                 /* 0x463794: 9 floats +0x28 */
    b->clockFrame /*+0x58*/ = -1;  b->vtbl[2](1);                    /* clock/matrices redone this frame */
}                                                                    /* no track: the bomb stays where it was */
```

The node 0x80 has positions in Woody's model (W2A model 21, nodes 141 = 0x80 and 142 = 0x180) for all carry animations (46, 47, 49, 50,
53..60, 61, 62, 64..71). A few local values (model space: +z = up, −y = forward, see `ins_root_at` in `src/level.c`):

| .ins | use | local bomb position |
|---|---|---|
| 46 frame 0 → end | pickup | (0, −100, 0) = 100 in front of him on the ground → (−53, 63, 19) … ends at the carry position |
| 47 | standing with bomb | ≈ (−45, −7, 117) (slight sway) |
| 49 | walking with bomb | (−44..−52, −12..−54, 95..115) |
| 60 | crouching with bomb | (−53, −69, 12) = in front of his feet on the ground |
| 61 (key 280 = release point) | ground throw | (−45,−7,117) → behind him (−15, 85, 82) … (−62, −8, 89) |
| 62 (key 179) | air throw | … (−53, 51, 168) |

**Throw** (`0x4635e2..0x4638cb`):

```c
ProjT T = ProjT_Default();                                   /* 0x4635e2..0x46368d = the values of 0x44a260 */
Vec3 dir = (0, -1, 0);                                       /* 0x4635e4: default straight down */
if (b->proj) T = b->proj->T;                                 /* 0x4636b1: copy of P+0x48 (the block the bomb was started with) */
else         CopyTemplate(0, &T);                            /* 0x4637bb: template 0 (in practice not: without a projectile 0x44d3a0 does nothing) */
T.speed = 0;                                                 /* 0x4637c9 */
if (p->bsub == 4 || p->bsub == 6) {                          /* throwing (ground / air) */
    Vec3 f = Mover_GetDir(&p->M);                            /* 0x445780 */
    dir = normalize((f.x, 1.0f, f.z));                       /* 0x46380f: y = 1.0 before normalizing => 45 degrees for a unit-length look direction */
    T.speed = 1000.0f;                                       /* 0x46384e, 0x447a0000 */
}
T.pos = b->pos;  T.dir0 = dir;                               /* 0x463856..0x463894 */
T.target = T.owner;                                          /* 0x463894: the target becomes the PREVIOUS owner (0 for a 1090 bomb, the thrower for a caught bomb) */
T.owner  = p;                                                /* 0x463899 */
Bomb_Launch(b, &T);                                          /* 0x44d3a0: b->held = 0, T.pos = b->pos, Proj_Reinit(b->proj, &T) (0x4492d0, no sound/visual) */
p->carrying = 0;  Bomb_Recell(b, b->pos + (0,30,0));         /* 0x4638a7, 0x463c50 */
SetState(p, 0);  p->bomb = NULL;                             /* 0x4638c6, 0x4638cb: immediately, even though the throw animation is still playing */
```

Everything not overridden comes from the bomb's running block. For a 1090 bomb that's template 0 (PROJECTILES.md §1.1: radius 30,
**gravity 15 → 3000 u/s²**, damping 0.95 on the ground / 0.99 in the air per 1/60 s, unlimited bouncing, `hits_all = 0` = only hits
category 2 with subtype 8 or 12, no visual) with a lifetime = the fuse (doesn't run down while there's a carried instance, PROJECTILES §2.2).
Since the owner is now the Perso, the hit test `0x44a0a0` skips Woody; if the bomb hits an enemy of subtype 8 (type 12, the bomb thrower)
or 12 (type 15), it explodes immediately (PROJECTILES §2.5, ENEMY2.md §4.3). With `T.target` = the thrower, a thrown-back bomb steers for
2 s in xz towards its thrower (template 0: steering factor 0.025, `T+0x48` = 2 s, no vertical steering) — **uncertain** how noticeable that
is in practice.

Flight path (own simulation with the constants above, 60 Hz, flat ground, without bouncing; uncertain ±10%): starts ≈ 89 high, peak ≈ 170,
lands after ≈ 0.57 s at ≈ **340** units; after that it bounces/rolls further (BOMB.md). For the air throw (starting ≈ 170 above the feet) ≈ 390.

**Part B: sub-states** `+0x58c` (jump table `0x463c28`), every frame after part A:

| sub | code | what | to |
|---|---|---|---|
| 0 | `0x4638eb` | `bt = AnimLen(0x45, 0)` (**1.2 s**), `LockMove(bt)` (`0x44cce0(t, 0)`: no walking) | 1 (falls through) |
| 1 | `0x46391c` | `bt -= dt`; `bt ≤ 0` | 2 |
| 2 | `0x46394e` | **carrying**. Attack just pressed (`0x467420(6)`) and not crouching (`+0x694 == 0`) | 3 on the ground, 5 in the air (`0x463978..0x463981`) |
| 3 | `0x4639b0` | space test: sphere r 60 (`0x42700000`) at `pos + look·10` (xz), `y = pos.y + 193·scale` (`0x4624c0` = `P+8 · inst+0x54`). Free → `bt = AnimLen(0x43)` (**0.933 s**), `LockMove(bt)` | 4; occupied → 2 |
| 4 | `0x463aa2` | `bt -= dt`; `bt ≤ 0` → `SetState(0)`, `bomb = 0` | (0) |
| 5 | `0x46398c` | `bt = AnimLen(0x44)` (**0.6 s**), no LockMove | 6 |
| 6 | `0x463aa2` | as 4 | (0) |
| 7 | `0x463aca` | put down: sphere r 24 (`0x41c00000`) at `pos + look·140` (`0x4aa1b4`), `y + 60` (`0x4ab284`); free → `bt = AnimLen(0x46)` (1.0 s), LockMove | 8; occupied → 2 |
| 8 | `0x463bb0` | `bt -= dt`; `bt ≤ 0` → `SetState(0)`, `bomb = 0` | (0) |

* The sphere test `0x434820(r)` + `0x434830(&pt, −1)` is a **stub** in this build: `0x434830` only sets `[0x53a560] = [0x53a55c] = [0x53a554] = 0`
  and returns. The result is thus always "free": sub 3 always goes to 4. (ENEMY.md line 356 describes `0x434830` as a real sphere test at
  `0x41cd68`: that also seems wrong then — not checked.)
* **Sub 7/8 (put down) is unreachable**: nothing writes `+0x58c = 7` (all writers are in the table of §1.1; `0x463981` writes 3 or 5).
  Putting a bomb down without throwing it thus doesn't exist as a player action.
* After the release point, the state is already 0. The rest of the throw animation keeps playing because LockMove is still running and
  the throw animation (priority 1500, chain `[61, 0]` resp. `[62, 7]`) is not interrupted by standing still (priority 1100). Sub 4/6 no
  longer run in state 0 (`0x44b480`).
* If Woody lands during an air throw before the release point, the animation choice in sub 6 on the ground requests anim **0x40**
  (`0x464764[6]` → `0x464752`) instead of 0x44: .ins 62 stops, no release point occurs, and after `bt`, sub 6 sets state 0 ⇒ `SetState`
  drops the bomb (§1.6). Uncertain whether this is ever noticeable (the air throw only lasts 0.3 s until the release point).

### 1.4 Animations (`0x4646b0` = state 6 in `0x463e60`; table `0x4b6180`, all speed 3.0)

Duration = `.ins` duration / 3 (Woody model W2A model 21; W1A gave the same durations for other animations in ROCKET.md).

| log. | chain (.ins) | prio | duration sub 0 | when (address) |
|---|---|---|---|---|
| **0x45** | 46 → 47 | 1500 | 3.6/3 = **1.2 s** | sub 0/1 on the ground: pickup (`0x4646df`) |
| **0x40** | 47 (loop) | 1100 | 4.0/3 = 1.333 s | standing with bomb (`0x464758`; Mover phase 0 via `0x463f40(1)` or sub 5/6/7 on the ground) |
| **0x41** | 50 → 49 | 1501 | 0.5/3 = 0.167 s | starting to walk with bomb (Mover phase 1, `0x463f86`) |
| **0x42** | 49 (loop) | 1500 | 1.8/3 = **0.6 s** | walking with bomb (Mover phase 2, `0x46401a`); cycle duration `len(0x42) / max(0.5, clamp(v/vmax))` like anim 3 (`+0x4a0`, `0x44ae00`) |
| **0x43** | 61 → 0 | 1500 | 2.8/3 = **0.933 s** | ground throw (sub 4, `0x4646f9`) |
| **0x44** | 62 → 7 | 1500 | 1.8/3 = **0.6 s** | air throw (sub 6 in the air, `0x46473f`) |
| 0x46 | 71 → 0 | 1500 | 3.0/3 = 1.0 s | put down (sub 8, `0x46472b`) — unreachable |
| 0x47 | 53 → 54 | 1501 | 1.8/3 = 0.6 s | air: rising and peak (Jumper 0/1/7; `0x464333`, ebx = ebp = 0x47) |
| 0x49 | 55 → 56 | 5000 | 0.8/3 = 0.267 s | air: fell off an edge (Jumper 3 with `J+0x4d`) |
| 0x4a | 57 → 47 | 1500 | 1.6/3 = 0.533 s | air: jump cut short (Jumper 3 with `J+0x4f`) and landing (Jumper 4 on the ground) |
| **0x4b** | 56 → 66 → 33 | 1600 | 0.1/3, then 0.8/3 | air: **long fall** (Jumper 5) ⇒ .ins 66 ⇒ **bomb is dropped** (part A) |
| 0x4c | 47 (loop) | 5200 | – | hard landing (Jumper 6 with a hard fall) |
| 0x4e / 0x4f / 0x50 | 58→60 / 60 / 59→47 | 1750 | – | crouching down / holding / standing up with bomb (`0x465bc0`, `0x465c54`, `0x465d2d`, only if state == 6) |
| 0x21 / 0x22 / 0x24 | 68→47 / 67→47 / 64→60 | 5110 | – | hit while carrying bomb: ground / air / crouched (`0x464b70`, also sets `+0x58c = 2`) |
| 0x27 / 0x28 / 0x4d | 69→30 / 70→30 / 65→33 | 6000 | – | death with bomb (`0x46481b`, `0x464840`, `0x46493e`): **dead code**, `Kill` sets state 2 first |
| 0x48 | 55 → 56 | 1000 | – | no requester found |

The air set is the ordinary `0x4642f0` with different numbers (`0x464333..0x46435c` vs `0x46435e..0x464388`): 4→0x47, 5→0x47, 6→0x4a, 7→0x49,
8→0x4a, 9→0x4b, 0xa→0x4c. Sound on pickup and throw comes exclusively from the **event tracks** of the animations (node 1: .ins 46 sound
29/30 on frame 6, .ins 61 and 62 sound 31 on frame 0 — bank 0 refs, SOUND.md §1); `0x463430`/`0x463530` themselves never call `0x468a00`.

### 1.5 Movement, jumping, crouching, camera

* State 6 runs `0x44bb20(1)` + `0x4624f0`, exactly like state 0 (PERSO_MOVE.md §5). In the Mover (`0x45b110`), the Jumper (`0x462d70`) and
  `0x44bb20` there is no reading of `+0x21c` anywhere (grep over all `+0x21c` readers): **same walking speed (600), same jump, same fall
  damage**. Only during pickup (1.2 s) and the ground throw (0.933 s) is movement blocked (LockMove).
* No peck/charge run/air attack: `0x457330` requires state 0 (`0x4573ad`), `0x44ba70` still calls the trigger.
* Crouching is allowed (`0x465b10`, action 5, anims 0x4e/0x4f/0x50); throwing while crouched is not (`+0x694 == 0` test at `0x463963`).
* Looking around (action 7, `0x44b980`) is allowed in state 6 with `+0x58c == 2` (`0x44b9e0..0x44b9ee`): `SetState(3)` ⇒ **the bomb drops** (§1.6). After
  looking around, `0x44c9f0` restores the previous state (6), but without the bomb (`+0x590 = 0`, `+0x594 = 0`): Woody then walks in carrying
  animations without a bomb until he presses attack (sub 2 → 3 → 4 → state 0). Presumably a bug in the original; not replayed in the original.
  Worked out (and ported, `Player.state6` without a bomb) in [PERSO_LOOK.md](PERSO_LOOK.md) §5: no hit animation in that state (`0x464b84`),
  bomb duck set 0x4e..0x50, and the attack press plays an empty throw.
* Camera: no special handling. The behind-mode of the follow camera applies to states 1, 4 and 8 (`0x4591ec..0x4591fd`), not 6.
  Other camera paths were not checked against state 6 (uncertain, but there's no `cmp …, 6` on `+0x21c` outside the five spots of §1.6).

### 1.6 Dropping without throwing: `0x463c90(Perso)`

```c
void Perso_DropBomb(Perso *p) {                               /* 0x463c90 */
    ProjT T = ProjT_Default();                                /* 0x463cc7..0x463d3f */
    if (p->bomb->proj) T = p->bomb->proj->T; else CopyTemplate(0, &T);
    T.speed = 0;  T.pos = p->bomb->pos;  T.dir0 = (0, -1, 0);  /* 0x463d7e, 0x43ff80(0,-1,0) */
    T.owner = 0;  T.target = 0;                               /* 0x463db8, 0x463dbc */
    Bomb_Launch(p->bomb, &T);                                 /* 0x44d3a0: held = 0 */
    p->carrying = 0;  Bomb_Recell(...);  p->bomb = NULL;      /* 0x463dc7, 0x463dcd, 0x463dd6 */
}
```
Callers (all five `cmp [..+0x21c], 6` spots have been checked):

| where | condition | typical |
|---|---|---|
| `0x44c9ad` in **SetState** `0x44c980` | `bomb && state == 6 && new != 6 && bomb->held` | death (`Kill` → 2), looking around (3), climbing wall (4), scripted action (5), teleport etc. |
| `0x44acb4` in **Reset** `0x44ab20` | `bomb != 0` (also `+0x594 = 0`, `0x44acaa`) | respawn, level switch |
| `0x44b8dc` in **Perso::Update** `0x44b530` | `bomb && state != 6` | safety net after `0x44c9f0` (state restored without SetState) |

Dropping "via part A" (speed 0, `dir (0,−1,0)`, since sub is then 2) also happens on .ins 66 (**long fall**); afterward `SetState(0)`.
After dropping, the bomb falls with its block's gravity (3000 u/s²) and bounces (BOMB.md).

### 1.7 Being hit while holding a bomb

`Hit` (`0x44ca00`, PERSO_MOVE.md §4.4) does not change state 6 (only 4 → 0). The hit animation `0x464b70` picks in state 6 with `+0x594`
0x21/0x22/0x24 (`0x464b7c..0x464bd6`) and sets **`+0x58c = 2`**: a throw in progress before the release point is cancelled (the .ins becomes
68/67/64, no release point), a pickup animation in progress too — Woody keeps the bomb. Knockback and `health −= damage` as usual.

### 1.8 The fuse runs out in his hands / death with a bomb

* The bomb counts `+0x114 += dt` every frame, even while held (`0x44d875..0x44d88c`); held or ridden ⇒ `inst+8 |= 0x40`
  (not collidable, `0x44d8a5`). The projectile update does nothing while `held` (PROJECTILES.md §2.2: `carried->byte[0x132]` ⇒ return).
* Explosion `0x44d6e0`: `var := 1` (`0x44d70a`), `0x44d650` (§4.1) calls Perso via `vtbl[40]` = `0x44d040(pos, 400)`: if
  `|pos − Perso.inst+0xc|² < 400²` (`0x44d05c..0x44d097`): `Hit(0, 0, away_xz, 0, 0)` (`0x44d118`: knockback, 0 damage), rumble (`0x44d12e`) and
  **`Kill(6)`** (`0x44d139`). The bomb sits ≈ 125 above his feet, so always within 400. `Kill(6)` is ignored if `+0x270 > 0`
  (1 s after respawn); the hit too if `+0x280 > 0`.
* `Kill` → `SetState(2)` → `0x463c90` (since `held` is still 1): the bomb's projectile gets restarted once more (0x44d3a0), then
  deactivated by `0x44d7c2`. Net result: bomb gone, Woody dead kind 6 (anim 0x2b, fade after 2.5 s).
* A death with a bomb (any kind) drops the bomb; the "death with bomb" animations 0x27/0x28/0x4d are dead code (`Kill` sets state 2 before
  `0x464790` checks `+0x21c == 6`; also PERSO_DEATH.md §4).
* Whoever throws the bomb away in time and stands > 400 from the explosion point notices nothing. After a throw of ≈ 340 plus bouncing that's tight: run away.

## 2. Message 1090: the bomb dispenser (`0x444e00`, game messages `0x444870`)

`1090 [inst, var, t]` (arguments `[esi+8]`, `[esi+0xc]`, `[esi+0x10]`):

```c
case 1090: {
    Instance *src = world->inst[arg0 & 0xffffff];              /* 0x444e0f, [0x50944c]+0x6c */
    uint32_t var = arg1 & 0xffffff;                            /* 0x444e22 */
    float life = arg2 * 0.01f;                                 /* 0x444e00, 0x4aa0ac = 0.01 */
    ProjT T = ProjT_Default();  CopyTemplate(0, &T);           /* 0x444e28..0x444eec: template 0 */
    T.life = life;                                             /* 0x444efc: T+0x2c */
    Vec3 v[2];
    if (Inst_GetVector(src, /*typecode*/0, v, /*n*/0)) {       /* 0x42f6b0 */
        T.pos = v[0];  T.dir0 = normalize(v[1] - v[0]);  T.speed = 100.0f;   /* 0x444f15..0x444faf, 0x42c80000 */
    } else {
        T.pos = src->pos;  T.dir0 = (0, -1, 0);  T.speed = 0;    /* 0x444fc7..0x444fff */
    }
    Bomb_Spawn(&T, /*snap*/1, var, /*kind*/0);                 /* 0x444fba -> 0x44d5d0 */
}
```

`0x44d5d0(T*, snap, var, kind)`: first bomb with `+0x131 == 0` from `0x5e4880[0x5e487c]`; none ⇒ warning
**`'Pas de bombes, ou plus assez de bombes dans ce niveau...'`** (`0x4b3ad4`, via `0x462c60` = empty log function) and **`SetVar(var, 1)`**
(`0x44d612`): the script then thinks the bomb has already exploded and the dispenser can be used again immediately. Otherwise `0x44d4d0`:

| field | value | address |
|---|---|---|
| `+0x10c` fuse | `T.life` = `t · 0.01` s (W2A: **20 s**) | `0x44d4dd` |
| `+0x110` warning duration | `min([0x5e48c4] = 2.0, fuse)` | `0x44d4e3..0x44d508`, ctor `0x44d28b` |
| position | `T.pos`; with `snap`: `GetHeight(pos)` (`0x435650`) ⇒ `y = ground + 1.0` | `0x44d52c..0x44d545` |
| `+0x60` | = position, re-cell (`0x4077f0`) ⇒ **visible** | `0x44d548..0x44d55c` |
| `+0x12c` script var | `var` (−1 = none) | `0x44d569` |
| `+0x128` explosion kind | `kind` = 0 for 1090, 1 for the cannon | `0x44d5a4` |
| `+0x131/+0x132/+0x133` | 1 / 0 / 0 | `0x44d575..0x44d582` |
| `+0x108` state | 1; `+0x114 = +0x118 = +0x11c = +0x120 = 0` | `0x44d56f..0x44d59e` |
| projectile | `T.carried = bomb`; `+0x124 = 0x4490a0(&T)`; pool full ⇒ `vtbl[17]` Reset (bomb gone again) | `0x44d5ab..0x44d5c4` |

There is **no sound or effect** in 1090 itself; the script plays the dispenser's sound and animation (§5). The bomb rolls/bounces out of the
dispenser at 100 u/s (template-0 physics, BOMB.md). The script hears about the end via the variable: `0x44d6e0` sets `var := 1` on the
**explosion** (`0x44d707`), not on pickup or throwing.

The level's 16 type-40 instances are just the **pool**: 1200 `[inst, 40]` → Init `0x44d2e0` + Reset `0x44d320` (out of the world,
`0x44d361`). Their `.ins` position is a storage spot (W2A: y = 2289 / 2953 at x ≈ 2100..2660) and plays no role.

## 3. Crates, types 120 / 121 ("Exploding objects (as Chest)")

### 3.1 Class

* Factory: type 120 → `0x403dc4` (alloc 0x108, ctor `0x451650`, vtable **`0x4aafd0`**); type 121 → `0x403e02` (same ctor, then
  `mov [esi], 0x4a9034`, **vtable `0x4a9034`**).
* Ctor `0x451650`: Instance ctor, `+0x104 = 0`, in list `0x5e581c[0x5e58b0]`, max **32** (`'Too much Exploding objects (as Chest), max is %d'`,
  `0x4b3cf4`). Dtor `0x4516f0` resets the counter to 0.
* Difference 120/121 (vtables compared slot by slot): only slot 0 (dtor `0x4516d0` / `0x404070` → both `0x4516f0`) and **slot 1 Init**:
  `0x451710` sets typeword `0x29` (category 9, subtype 1), `0x451840` sets `0x49` (category 9, subtype 2); both first call the FadeInst Init
  `0x44e7c0` (fade 0, speed 100). There is **no reader** of category 9 or of subtype 1/2 found (all `0x40c340`/`0x40c350` callers and all
  `and …, 0x3e0` masks checked): behavior identical. Type 120 is not used in any level.
* Vtable (both): `[3]` Update `0x40bf10` = `jmp 0x44e810` (fade update, every frame), `[17]` Reset `0x451730`, `[22]` handler `0x451820`,
  `[28]` `0x451770` = hit test, `[29]` `0x4517d0` = open.

### 3.2 Opening

```c
void Chest_Blast(Chest *c, Vec3 *pos, float r) {             /* vtbl[28] 0x451770, called by 0x44d650 with r = 400 */
    if (MsgTest(c->id, 0x20)) return;                        /* 0x443ed0: already open */
    if (dist2(*pos, c->pos /*+0xc*/) < r*r) c->vtbl[29]();   /* 3D, from the instance's ORIGIN, not the geometry */
}
void Chest_Open(Chest *c) {                                  /* 0x4517d0 */
    Inst_PlayOnce(c, 1.0f, 0, -1, -1, -1);                   /* 0x436ca0: slot0 = anim 0, start = now, speed 1.0·3 (0x4a988c), once */
    Effect_Explosion(1, &c->pos, 0);                         /* 0x477060 kind 1 (ROCKET.md §5.3): flashes R 1400 and 400, particles */
    c->fadeSpeed = 0.5f;  SetFade(c, 1.0f, 0);               /* 0x4517f8, 0x44e7f0: target 1.0 (invisible), not instant */
    MsgSet(c->id, 0x20);                                     /* 0x443e50: the script sees MSGTEST 32 */
}
```
* Fade `0x44e810` (INSTANCE.md §5): `+0x6c` runs at 0.5/s from 0 to 1 ⇒ **after 1.8 s** past 0.9 ⇒ `inst+8 |= 0x40` = no longer collidable;
  after 2.0 s fully transparent. The instance stays in its cell (no `0x407850`).
* **No own sound** (no `0x468a00` in `0x4517d0`); the explosion sound is the bomb's (SoundFx 6, `0x44d72e`).
* Model 48 (W2A 537) has a single animation of 4.8 s ⇒ opening takes 1.6 s. Before opening, the clock is stopped: Reset → `0x42e250`
  (speed 0, slot0 = 0, slots 1..3 = −1).

### 3.3 Messages and reset

Handler `0x451820` (shared with the bomb): **29** ⇒ `vtbl[17]` (`0x451832`) = Reset `0x451730`: `0x42e250` (anim frozen on frame 0),
re-cell `0x4077f0(0)`, fade speed 100 and target 0 (so visible and collidable again in 0.01 s), **clear msgmask 0x20** (`0x45175c`).
All other messages → the FadeInst handler `0x44e8f0` (56 = fade target ×0.01, 57 = fade speed ×0.01, rest → Instance `0x42d5e0`).
In the levels, crates only get 1200 (and W2B tests MSGTEST 32 afterward); never 29.

## 4. What a bomb explosion hits, and class 17

### 4.1 `0x44d650(bomb)` — the bomb's only damage dealer

```c
for (i = 0; i < [0x4c5318]; i++) {                            /* actor list 0x4c4e00 (Npcs) */
    Actor *a = list[i];
    if (Category(a) == 2 || Category(a) == 1) a->vtbl[40](&bomb->pos, 400.0f);   /* 0x40c340; 0x43c80000 */
}
for (i = 0; i < [0x5e58b0]; i++) chests[i]->vtbl[28](&bomb->pos, 400.0f);         /* 0x44d6b3..0x44d6c9 */
```
* Category 1 = the Perso (`0x44d040`, §1.8). Category 2 = enemies: base `0x41ae20` (within r ⇒ dead, ENEMY.md), type 12 `0x4119b0`
  (1 hp per explosion, ENEMY2.md §4.4), Buzz (BOSS14.md §8).
* No test on line of sight or walls: 400 in 3D, through everything.
* BONUS.md §7 wrote "all type-17 objects with state 1/2": wrong, it's category 1/2 (`0x40c340` = `typeword & 0x1f`).

### 4.2 Class 17 (ctor `0x40c3d0`, vtable `0x4a95dc`, 0x118 B)

The vtable has **28 slots** (slot 28 is already the float 400.0 in `.rdata`); so there is no `vtbl[40]`, and class 17 is not in the actor list.
Bombs do nothing to it. What it actually is, is in BOSS14.md §9.2: an instance towed by a boss (message 59 links it;
the boss sets its position and rotation every frame, `0x40c5a0` sets the owner `+0x110` for the render color `[26]` `0x40c5c0`).

| field | meaning | address |
|---|---|---|
| `+0x108` u8 | "reset" (BOSS14 §8: condition for message 59) | Init `0x40c450` (0), Reset `0x40c491` (1) |
| `+0x10c` | smoke emitter (0x34 B, list `[0x5e8564]`, same exhaust list as the rocket) over all markers with **type code 9**, state 2 = on | Reset `0x40c4ab..0x40c548`, `0x40c563` |
| `+0x110` | owner (boss) | `0x40c5a8`, Init/Reset 0 |
| `+0x114` | 1 = no smoke (ctor `0x40c3f8`); message **63 `[inst, v]`** ⇒ `+0x114 = v`, Reset (`0x40c5f2`) | |

Reset `0x40c460` only creates the smoke if `+0x114 == 0`. W2D (746) and W3D (776) send `63 [inst, 1]`, no one sends 0 ⇒ class 17's smoke
is not used in any level. Usage: W1B 404, W2D 746, W3D 776, WWS 363 — always next to a boss.

The "0x40cb80 family" that calls `0x4490a0` at `0x40d128` does **not** belong to class 17: `0x40cb80` sits at `0x4a9728` = slot 52 of the
vtable `0x4a9658` of **type 16** (boss, ctor `0x40c730`) and fires three ordinary projectiles in a fan (`0x40d0a0..0x40d134`, loop of 3).

## 5. Usage in the levels (`tools/ekodisasm.py` over all 28 `extract/Data/<LVL>/code`)

| level | pool type 40 | dispenser: script obj → 1090 `[inst, var, t]` | trigger | crate 121 (hides) |
|---|---|---|---|---|
| **W2A** | 16 (187..194, 318..325) | 536 → `[538, 190, 2000]` (20 s) | volume 233 + attack release | 537 model 48 (13400, 256, −16847) → **539 type 36** |
| K2A | 16 | 540 → `[541, 171, 2000]` (20 s) | volume 237 + attack release | 542 model 46 (11224, 999, −23352) → 543 type 36 |
| S2A | 16 | 336 → `[384, 15, 2000]` (20 s) | volume 109 + attack release | 528, 529 model 47 → 527 type 36 and 530 type 30 |
| W2B | 15 | 509 → `[508, 172, 1000]` (10 s) | volume 200 + attack release + 1042 `[510, 500, 60]` | 506, 507, 545 model 49 (≈ (7300..7900, 204, 8500..8640)); 523 model 49 (790, 876, −5370) → 525 type 35 |
| KWS / SWS / WWS | 1 (244 / 244 / 252) | 284/288/321 → `[282/286/319, …, 1000]` (10 s) | volume 38/39/68 + attack release + 1042 `[283/287/320, 500, 90]` | – |
| S2R | 8 | – | – | – |
| W2D / W3D | 8 / 16 | – (bombs from the cannon / the bosses) | | – |

* Type 120: nowhere. Type 17: W1B 404, W2D 746, W3D 776, WWS 363 (§4.2).
* MSGTEST 32 (crate open) only occurs in W2B: 506 ⇒ var 55 := 1, 507 ⇒ var 56 := 1, 523 ⇒ var 180 := 1, `6 [525, 1]` (show bonus 525),
  var 166 := 1. W2A, K2A and S2A don't test the crate: the item inside becomes reachable because the crate stops colliding after 1.8 s.
* The hub dispensers (KWS/SWS/WWS) have a single 10 s bomb and no crate; what that bomb is for (boss mode 2 in WWS?) has not been figured out.

## 6. W2A: the bomb puzzle

Script objects (`out/w2a_code.txt`, word 16416..16510):

```
object 536 (init):  var189 = 0; var190 = 1                        ; 190 = "dispenser ready"
object 536 (body, woken by var 189/190 and volume 233):
    if VOL_FLAG5(233): SEND 1050 [var189, 2]                       ; var189 = 1 if the attack key was JUST RELEASED (0x467440)
    if var189 == 1 && var190 == 1:
        var189 = 0; var190 = 0
        SEND 1622 [538, 0x1000007, 100]                            ; 3D sound 7 from the level bank, once, at the dispenser
        SEND 3 [538, 0, 1, 200]                                    ; dispenser plays anim 0 once over 2 s
        DELAY 100 ->  SEND 1090 [538, var190, 2000]                ; after 1 s: bomb with a 20 s fuse, var190 := 1 once it explodes
object 537 (init):  SEND 1200 [537, 121]                           ; the "crate"
objects 187..194, 318..325: SEND 1200 [x, 40]                      ; the bomb pool
```

* **Dispenser**: instance 538 (model 43) at (8232, 259, −17315). Trigger volume 233 belongs to instance 536 (model 22 = just a volume node):
  a cube of 400, x 8025..8425, y 274..674, z −17522..−17122. There is no facing-direction test (no 1042): pressing and releasing the attack
  key inside the volume is enough. Pressing starts an ordinary peck in state 0 (and picks up a bomb if one is already within 269).
* **Ejection**: type-code-0 marker of model 43 (node 16, parent = node 1 ≈ identity): in rest pose ≈ **(8217, 236, −17281)** with direction
  ≈ (−0.38, 0, 0.92) (rest pose, computed with the port convention `mat4_from_trs`, not measured in the original: uncertain). The bomb is
  placed on the ground and rolls that way at 100 u/s.
* **Target**: crate 537, model 48 with its origin at (13400, 256, −16847). **Correction (seen in the port):** it is indeed a treasure chest,
  next to the palm tree on the sand island; the 2200 bbox is that of mesh node 1 over the whole open animation (planks flying around). Collision:
  press node 8 and hull node 9, ≈ 300 x 200 x 300 around the origin (the earlier "hull node 10 / press node 9" belongs to instance 127,
  model 15, the rock next to it); inside, 83 units from that origin, sits **instance 539 type 36** (unique item, savegame flag,
  BONUS.md §2.3) with volume 234. Two type-8 enemies nearby (376 at (12837, 177, −16936), 364 at (12111, 181, −17024)); the first stands
  within 575 of the crate and can thus be blown up with the same bomb if it explodes close enough.
* **Distance** dispenser → crate ≈ 5190 (mostly +x). Woody walks at 600 u/s ⇒ at least 8.7 s; the fuse is 20 s. Throwing must land the bomb
  within **400 of the origin** (13400, 256, −16847); throw range ≈ 340 + bouncing. The player then has to get themselves outside 400 of the bomb.
* If it fails (bomb too early, too far, in the water/pit ⇒ `0x44d6e0` when `cell < 0`): the bomb explodes, var 190 = 1 and the dispenser can
  be used again. There is no limit.
* The bomb cannon 411 (type 21, ROCKET.md §7) at (9882, 139, −12797) is unrelated to this: its bomb comes from the same pool, but explodes far from 537.

## 7. Corrections to other documents

1. **OBJECTS.md §3** ("17 = breakable object for bombs, `vtbl[0xa0](&pos, r)`") and **TODO.md** (same line): class 17 has no
   `vtbl[40]` and is not hit by bombs; it is the towed boss instance (§4.2, BOSS14.md §9.2).
2. **BONUS.md §7**: "`0x44d650`: all type-17 objects with state 1/2 get `vtable[+0xa0]`" → all **actors of category 1 or 2** from
   `0x4c4e00` (§4.1). ROCKET.md §7 and ENEMY2.md §4.3 already had it right.
3. **PROJECTILES.md §1.2**: "`0x40d128` (class-17 family `0x40cb80`)" → type 16 (boss, vtable `0x4a9658` slot 52).
4. **EVENTS.md §4.1 / BONUS.md §7**: the crate tests the distance from `inst+0xc` (origin), and 120/121 differ only in the typeword.
5. **ENEMY.md** (`0x41cd68`): `0x434830` is a stub in this build that zeroes the result globals; a "sphere test" there is always free (not checked
   what that means for the enemy).
6. **GAMEFLOW.md §4.6**: `1050(var, 2)` = action 6 just **released** (`0x467440`), not just pressed (GAMEFLOW §8 and OBJECTS §4 have it right).

## 8. Recipe for the port

Prerequisite: the bomb class from BOMB.md (pool of 16, `Bomb` with projectile, fuse, explosion). Below, only the interface the player
needs, and the player side. Names match `src/player.c` / `src/main_engine.c`; anything between `/* */` is the address in the original.
`v3_sub/v3_dot/v3_norm/v3_dist2` stand for the usual vector helpers (spelled out inline in the port now); `ProjT`/`Proj` is the
projectile block from PROJECTILES.md §1 as BOMB.md sets it up in the port.

### 8.1 Shared structures (BOMB.md determines the rest)

```c
/* src/bomb.h (or in main_engine.c next to Rocket) */
typedef struct Bomb {
    Instance *inst;
    int   state;                 /* +0x108: 0 off, 1..5 fuse, 6 exploded (BOMB.md) */
    float fuse, warn, t;         /* +0x10c, +0x110, +0x114 */
    int   in_use, held, ridden;  /* +0x131, +0x132, +0x133 */
    uint32_t var;                /* +0x12c, 0xffffffff = none */
    int   kind;                  /* +0x128: explosion kind 0 (1090) or 1 (cannon) */
    Proj *proj;                  /* +0x124: projectile with T.carried = this bomb */
} Bomb;
extern Bomb g_bombs[16]; extern int g_nbombs;                 /* 0x5e4880, 0x5e487c: filled by 1200 [inst, 40] */
Bomb *bomb_spawn(const ProjT *T, int snap, uint32_t var, int kind);   /* 0x44d5d0 + 0x44d4d0 */
void  bomb_launch(Bomb *b, const ProjT *T);                  /* 0x44d3a0: b->held = 0; T.pos = b->inst->position; proj_reinit(b->proj, T) */
void  bomb_explode(Bomb *b);                                  /* 0x44d6e0: var := 1, bomb_blast(), SoundFx 6, effect kind b->kind, fade */
```

### 8.2 Player (`src/player.h` / `src/player.c`)

```c
/* Player: Perso state 6 */
struct Bomb *bomb;  int carrying, bsub;  float bt;          /* +0x590, +0x594, +0x58c, +0x598 */
float throw_hold;                                           /* port: keeps holding 0x43/0x44 after the release point (the original does this with anim priorities) */

/* player_state_free: also !p->bomb (state 6 is not 0: no 1042 switch, no rocket, no peck) */

static void bomb_drop(Player *p)                            /* 0x463c90 */
{
    Bomb *b = p->bomb; if (!b) return;
    ProjT T = b->proj ? b->proj->T : proj_template(0);
    T.speed = 0; T.pos = b->inst->position; T.dir0 = (Vec3){ 0, -1, 0 }; T.owner = NULL; T.target = NULL;
    bomb_launch(b, &T); p->carrying = 0; p->bomb = NULL;
}
/* "SetState": everywhere the port leaves state 6 (player_kill, climbing wall, looking around, script_action, teleport, place/reset,
 * player_mount can't happen: it requires state_free) first:  if (p->bomb && p->bomb->held) bomb_drop(p);   (0x44c9ad, 0x44acb4, 0x44b8dc) */

static int bomb_try_pick(Player *p, int pressed)            /* 0x463430; in player_update before attack_trigger, only if state_free */
{
    if (!pressed || !p->on_ground || p->atk) return 0;
    float r2 = (69.0f + 200.0f) * (69.0f + 200.0f);
    for (int i = 0; i < g_nbombs; i++) { Bomb *b = &g_bombs[i];
        if (!b->in_use || b->ridden) continue;
        Vec3 d = v3_sub(p->pos, b->inst->position);
        if (v3_dot(d, d) < r2) { p->bomb = b; b->held = 1; p->carrying = 1; p->bsub = 0; p->atk = 0; p->charge = 0; return 1; }
    }
    return 0;
}
/* in attack_trigger: do nothing if p->bomb (0x457330 requires state 0) - but do still update action_prev */

static void bomb_substate(Player *p, int pressed, float dt)  /* 0x463530 part B */
{
    switch (p->bsub) {
    case 0: p->bt = anim_len(p, 0x45, 0); lock_move(p, p->bt); p->bsub = 1;   /* 1.2 s */  /* fallthrough */
    case 1: if ((p->bt -= dt) <= 0) p->bsub = 2; break;
    case 2: if (pressed && !crouching /* +0x694, crouching isn't ported yet: 0 */) p->bsub = p->on_ground ? 3 : 5; break;
    case 3: p->bt = anim_len(p, 0x43, 0); lock_move(p, p->bt); p->bsub = 4; break;   /* space test 0x434830 is a stub: always free */
    case 5: p->bt = anim_len(p, 0x44, 0); p->bsub = 6; break;
    case 4: case 6: if ((p->bt -= dt) <= 0) { if (p->bomb) bomb_drop(p); p->bsub = 0; } break;   /* SetState(0): still holding the bomb => it drops */
    }
}

/* animation choice (0x4646b0) in the "animations, Perso_AnimState" block: before js == 2 etc., if p->bomb || p->throw_hold > 0 */
if (p->bomb) {
    if (js == 2 || p->on_ground) {
        if (p->bsub <= 1) want = 0x45;
        else if (p->bsub == 4) want = 0x43;
        else if (p->bsub <= 3) want = p->ramp_phase == 1 ? 0x41 : p->ramp_phase == 2 ? 0x42 /* rate like anim 3 */ : 0x40;
        else want = 0x40;                                       /* sub 5/6 on the ground */
    } else want = p->bsub == 6 ? 0x44 : js <= 1 || js == 7 ? 0x47 : js == 5 ? 0x4b
                : (js == 3 || js == 4) ? (p->jumper.fell_off ? 0x49 : 0x4a) : js == 6 ? (p->jumper.hard_fall ? 0x4c : 0x4a) : p->lanim;
    if (p->hit_anim_t > 0) want = p->hit_anim;                  /* 0x21 / 0x22 / 0x24 via player_hit, see below */
}
else if (p->throw_hold > 0) { p->throw_hold -= dt; want = p->lanim; }   /* after the release point: keep playing 0x43 / 0x44 to the end (state is already 0) */
/* crouching with bomb: 0x4e / 0x4f / 0x50 instead of 0x31 / 0x32 / 0x33 (once crouching is ported) */
/* player_hit: if (p->bomb) { p->hit_anim = crouch ? 0x24 : on_ground ? 0x21 : 0x22; p->bsub = 2; }  (0x464b70) */
```

### 8.3 After the pose: bomb in hand and the release point (`src/main_engine.c`, right after `player_update`, like the rocket sync at line ~1664)

```c
void player_carry_frame(Player *p, float dt)                 /* 0x463530 part A, frame step 16: after Woody's pose */
{
    Bomb *b = p->bomb; if (!b || !p->carrying) goto sub;
    int ins = p->inst->anim; int rel = 0;
    switch (ins) { case 61: rel = p->bt < 0.46667f; break; case 62: rel = p->bt < 0.3f; break;
                   case 66: rel = p->bt < 0.16667f; break;   /* long fall: bsub is 2 => drop it */
                   case 71: rel = p->bt < 0.23333f; break; } /* 65/69/70 are dead code */
    if (rel) {
        ProjT T = b->proj ? b->proj->T : proj_template(0); Vec3 dir = { 0, -1, 0 }; T.speed = 0;
        if (p->bsub == 4 || p->bsub == 6) { Vec3 f = { sinf(p->yaw), 1.0f, cosf(p->yaw) }; dir = v3_norm(f); T.speed = 1000.0f; }
        T.pos = b->inst->position; T.dir0 = dir; T.target = T.owner; T.owner = p->inst;
        bomb_launch(b, &T); p->carrying = 0; p->bomb = NULL;           /* state 0; LockMove and the throw anim keep playing */
        p->throw_hold = p->bt;                                          /* port: keep requesting 0x43/0x44 until the chain finishes */
    } else {
        const Model *m = p->inst->model; Vec3 hand, tgt;
        float ph = (uint32_t)ins < m->nanims && m->anims[ins].duration_s > 0 ? p->inst->anim_time / m->anims[ins].duration_s : 0;
        if (ins_camera_eval(p->inst, ins, ph, &hand, &tgt)) {           /* 0x42feb0 + 0x42fa40: node 0x80 (and 0x180 must exist) */
            b->inst->position = hand; if (b->proj) b->proj->pos = hand;
            b->inst->quat = p->inst->quat; mat4_from_trs(&b->inst->world, hand, b->inst->quat, b->inst->scale);
            b->inst->noncollide = 1;                                    /* 0x44d8a5: held = not collidable */
        }
    }
sub:
    if (p->bomb || p->bsub == 4 || p->bsub == 6) bomb_substate(p, g_act_now[2] && !g_act_prev[2], dt);
}
```
The projectile update must skip a held bomb (PROJECTILES §2.2: `held` ⇒ return), and the fuse must keep running (BOMB.md).

### 8.4 Messages (`on_msg`, l. ~1860-2040)

```c
case 1200: ... if (in->type == 40) bomb_register(in);                  /* Init 0x44d2e0 + Reset 0x44d320: invisible until 1090 */
               if (in->type == 120 || in->type == 121) chest_register(in);   /* 0x5e581c[32]; anim frozen on frame 0 */
case 1090: if (in && m->nargs > 2) {                                   /* 0x444e00 */
        ProjT T = proj_template(0); T.life = (float)(int32_t)m->args[2] * 0.01f; Vec3 p0, d;
        if (inst_vector(in, 0, &p0, &d)) { T.pos = p0; T.dir0 = v3_norm(d); T.speed = 100.0f; }
        else { T.pos = in->position; T.dir0 = (Vec3){ 0, -1, 0 }; T.speed = 0; }
        if (!bomb_spawn(&T, 1, m->args[1], 0)) eko_set_var(vm, m->args[1], 1);   /* 'Pas de bombes...' */
    } break;
case 29: ... if (in && chest_of(in)) chest_reset(chest_of(in));        /* 0x451730 */
```

### 8.5 Explosion and crates

```c
void bomb_blast(Vec3 c)                                                /* 0x44d650, r = 400 */
{
    if (g_player && !g_player->dead_kind && v3_dist2(g_player->inst->position, c) < 400.0f * 400.0f) {   /* 0x44d040 */
        Vec3 d = v3_sub(g_player->inst->position, c); d.y = 0; d = v3_norm_or(d, (Vec3){ 0, 0, 1 });
        player_hit(g_player, 0, d); player_kill(g_player, 6);          /* kill drops the bomb (8.2) */
    }
    enemies_blast(&g_enemies, c, 400.0f);                             /* vtbl[40] per class (ENEMY.md, ENEMY2.md §4.4, BOSS14.md) */
    for (int i = 0; i < g_nchests; i++) {                              /* vtbl[28] 0x451770 */
        Chest *k = &g_chests[i]; Instance *in = k->inst;
        if (eko_msgmask_test(g_vm, in->id, 0x20) || v3_dist2(c, in->position) >= 400.0f * 400.0f) continue;
        inst_play_once(in, 0, 3.0f, g_now);                           /* 0x436ca0(1.0) */
        game_explosion(in->position);                                  /* 0x477060 kind 1 */
        in->fade_rate = 0.5f; in->fade_target = 1.0f;                /* > 0.9 after 1.8 s => noncollide (inst_tick already does that) */
        eko_msgmask_set(g_vm, in->id, 0x20);
    }
}
void chest_reset(Chest *k) { Instance *in = k->inst; in->anim = 0; in->anim_time = 0; in->anim_speed = 0;
                             in->visible = 1; in->fade_rate = 100.0f; in->fade_target = 0; eko_msgmask_clear(g_vm, in->id, 0x20); }
```

### 8.6 Verification

* W2A: start near the dispenser (e.g. `--pos 8225 280 -17322`), press and release attack ⇒ sound, animation of 538, after 1 s a bomb near
  ≈ (8217, 236, −17281) that slowly rolls away. Press attack again within 269 ⇒ 1.2 s pickup animation, then the bomb over his shoulder; walking and
  jumping as usual. After 20 s without throwing: explodes in his hands, dies kind 6, and the dispenser works again.
* Attack once more ⇒ throw animation, after 0.47 s the bomb flies 45° upward, ≈ 340 away. Walk to (13400, 256, −16847) (≈ 9 s) and throw there
  ⇒ 537 breaks open (1.6 s), fades over 2 s and item 539 can be collected.

## 9. Uncertain / not checked

1. The dispenser's marker ejection point (8217, 236, −17281) is computed in rest pose with the port convention, not measured in the original.
2. The flight distance (≈ 340) is an own simulation; bouncing/rolling afterward (BOMB.md) not included.
3. Whether the steering of a thrown-back bomb towards its thrower (`T.target = old owner`) is noticeable; the xz clamp `T+0x50` compares against a
   non-normalized speed (PROJECTILES §2.2).
4. Exactly how the animation controller (priorities) decides the throw animation keeps playing after `SetState(0)`: not read (`0x436b70`); the port
   holds it with `throw_hold`.
5. `vtbl[23]` of the bomb (`0x44d990`: 1 if held, else 8) — meaning unknown.
6. The look-around bug (§1.5) and picking up a bomb while grabbing a climbing wall (§1.2) are derived from the code, not replayed in the original.
7. What the hub dispensers (KWS/SWS/WWS, 1 bomb, 10 s) are for.
