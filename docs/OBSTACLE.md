# OBSTACLE.md — the endless world ray `0x497a30` and the two obstacle sensors

Two sensors look ahead for obstacles, and both use the same world query, the endless ray `0x497a30`:

* the **ledge sensor of the Perso** `0x44b2e0` (`Perso+0x234`), read only by the charge run (attack sub-states 9/10,
  PERSO_JUMP.md §2.3);
* the **obstacle sensor of the enemies** `Enemy+0x124` (ctor `0x41cfc0`, tick `0x41d4a0`), read by the behaviours Chase and
  Wander (ENEMY.md §5.3/5.4) of every enemy class, the shooters (ENEMY.md §8) and the bosses included.

Ported: `player_ray_endless` and `check_steep` in `src/player.c`, the `enemy_sensor_*` functions in `src/enemy.c` (used by
`src/enemy.c` and `src/boss.c`).

## 1. The endless ray `0x497a30(const vec3 *a, const vec3 *dir, int cell)`

Callers: `0x44b2e0` (Perso ledge sensor, §2) and `0x435810` (the "endless ray" wrapper), which in turn is called by
`0x41d010` (enemy sensor probe, §3) and `0x4510c0` (laser class 50, OBJECTS.md). The segment version `0x497ed0` (world part
`0x497fb0`, instance slot `vtbl[5]` = `0x432ab0`) has the same layout; it is the one behind `0x4359b0`.

```c
void WorldRay(const vec3 *a, const vec3 *dir, int cell)     /* 0x497a30 */
{
    WorldRay_Cells(a, dir, cell);                             /* 0x497b10: [0x4c4bd0] = 1 or 3 */
    if (!g_visited /*0x4c4be8*/ && !g_ndyn /*0x4c4bec*/) return;
    g_instStamp /*0x4c4c08*/++;
    for (k = 0; k < g_nvisited /*0x4c4be4*/; k++) {           /* every instance linked into a visited cell */
        Cell *c = World->cells /*[0x4c4c0c]+0x1c*/[g_visited[k]];
        for (j = 0; j < c->ninst /*+0x40*/; j++) {
            u32 e = c->inst /*+0x44*/[j];                     /* low 16 bits: instance index, high 16: collision mask */
            World->inst /*+0x40*/[e & 0xffff]->vtbl[6](a, dir, e);    /* 0x431de0 for the instance base class */
        }
    }
    for (j = 0; j < g_ndyn; j++)                              /* the dynamic list 0x4c3bb4 (EVENTS.md 3.1) */
        g_dyn[j]->vtbl[6](a, dir, g_dyn[j]->index /*+4*/ | 0xffff0000);
}
```

`dir` is **not normalised**; the answer `t` ([0x4c4bd4]) is in units of `dir` (hit point = `a + t·dir`).

### 1.1 World part `0x497b10`

* `cell == -1` ⇒ `cell = 0x408180(a)` (kd descent, `gel_cell` in the port). `[0x4c4c24]++` is the polygon frame stamp,
  `[0x4c4be4] = 0` the list of visited cells, which starts with `cell`.
* Per cell (record `World+0x1c`): of the six faces of its box (`0x406ee0(dir, face)` = denominator,
  `0x406e50(a, face)` = numerator) the one the ray leaves through first. No such face ⇒ **type 1**, `[0x4b1770] = -1`.
* Then its polygons (`cell+8` count, `cell+0xc` indices into `World+0x10`), each once per query (`poly+4` = stamp).
  A polygon counts only if **a lies on its front side** (`n·a + d ≥ 0`, `0x497c73`) and the ray goes **into** it
  (`n·dir ≤ 0`, `0x497ca2`), it is nearer than the best so far and than the exit face (0.001 slack, `0x4a94c4`), and the
  crossing point lies inside it (`0x408430`). Polygon index → `[0x4c4bd8]`.
* A hit in this cell ⇒ **type 3**, `t = num/den` → `[0x4c4bd4]`, plane `poly+0xc..0x18` → `[0x4c4bc0..0x4c4bcc]`,
  `[0x4b1770] = cell`. Otherwise to the neighbour behind the exit face (`cell+0x10 + 4·face`): `≥ 0` ⇒ kd node, descended
  with the exit point (`0x40ab60`); `0x80000000` ⇒ nothing behind it, **type 1**; other negative values ⇒ cell `−1 − link`.
  The new cell is appended to the visited list and the loop repeats.

**Type 1 does not write `[0x4c4bd4]`**: the instance part below then compares against whatever the previous query left
there (see §1.2). The segment version `0x497fb0` also knows **type 0** (no hit, `0x498424`).

### 1.2 Instance part `vtbl[6]` = `0x431de0` (instance base vtable `0x4a9034`)

Skipped: an instance not in any cell (`+0x1c == -1`, hidden by message 6), already tested in this query (`+0x20 ==
[0x4c4c08]`), flag `+8 & 0x40` (fading / not collidable), a model without press nodes (`model+0x58` = count), or
`(inst+0xd0 & entry) & 0xffff0000 == 0` (collision mask). The transforms are refreshed (`vtbl[2](1)`) when `inst+0x58` is
not this frame's (`[0x509adc]`). Then per press node (kind 1, list `model+0x5c`):

* bounding-sphere reject: `b² − 4ac < 0` of the ray against the node's sphere (`0x4a94c0` = 4);
* the ray is taken into node space (`0x440fc0`, the inverse node matrix);
* per polygon: **one-sided** again: `plane(a) < 0` ⇒ skipped; the edge tests (every edge's triple product with `dir`
  positive) put the crossing inside; `t = −plane(a) / (n·dir)`;
* `t < [0x4c4bd4]` ⇒ **type 4**: `[0x4c4bd4] = t`, the plane in world space → `[0x4c4bc0..cc]`, node → `[0x4c4be0]`,
  polygon → `[0x4c4bd8]`, instance index (`entry & 0xffff`) → `[0x4c4bdc]`.

Actors (Perso, enemies) have no press nodes and are never hit.

### 1.3 Result types

| `[0x4c4bd0]` | meaning | `t` | set by |
|---|---|---|---|
| 0 | segment only: nothing hit (the exact exit paths of `0x497fb0` were not traced) | – | `0x498424` |
| 1 | the ray left the world (or started in a cell without an exit) | **stale** | `0x497c1d`, `0x497eaf` |
| 2 | segment only: hit at `t = 0` by an instance (`0x4330f4`, `[0x53a554] = 3` directly) | 0 | `0x432ab0` |
| **3** | **world polygon** | fraction of `dir` | `0x497e6a` |
| **4** | **press-node polygon of an instance** (nearer than any world polygon) | fraction of `dir` | `0x431de0`, `0x432ab0`, … |

`0x435810(a, b, cell)` wraps it as `0x497a30(a, b − a, −1)` (its own `cell` argument is **ignored**, `0x43581f`) and
translates like `0x4359b0` does: 3 ⇒ hit kind 1 (`[0x53a554] = 1`, `[0x53a558] = t`, plane → `[0x4b3108..14]`,
`[0x53a568] = a.y − t`), 4 ⇒ kind 2 (node `[0x53a58c]`, instance `[0x53a560]`, hit point → `[0x53a57c..84]`, `0x431700`),
1 ⇒ `[0x53a554] = 0` and **`[0x53a558]` untouched**.

### 1.4 Port

`player_ray_endless(p, a, dir, &t)` casts a segment from `a` to where `a + k·dir` leaves the level's bounding box, first
against the world polygons **from the front only** (`gel_ray_front`, same rule as `0x497c73/0x497ca2`), then against the
press nodes of the instances up to that hit (`inst_ray_press`, both sides). It answers 1, 3 or 4 and `t` in units of
`dir`. Differences: the instance side is two-sided; type 1 answers "no hit" instead of the stale `t`.

## 2. The Perso's ledge sensor `0x44b2e0` (`Perso+0x234`)

Called once per Perso update at `0x44b8e3` (PERSO_FRAME.md §2.1), after the move/collide and before Orient `0x44bd00`;
so the attack controller `0x457a50`, which runs earlier in the frame, reads the **previous frame's** answer.

```c
void Perso_CheckLedge(Perso *p)                          /* 0x44b2e0 */
{
    p->ledge /*+0x234*/ = 0;
    if (!p->onGround /*0x44bcf0: +0x22c*/) return;
    vec3 f = normalize(p->M.look /*Mover+0x10, y = 0*/);   /* 0x445780 copies it; |f| > 0 (0x4a9004) else unchanged */
    vec3 d = f * p->P[0x80/4] /*Perso+0x190 = 40*/;
    vec3 a = p->pos /*+0x1f4*/ + (0, P[0] /*Perso+0x110 = 43*/, 0);   /* the collision centre */
    vec3 b = p->pos + d;                                 /* the floor point 40 ahead */
    WorldRay(&a, &(b − a), -1);                          /* dir = (40 f.x, −43, 40 f.z) */
    if ((g_rayType == 3 || g_rayType == 4) && g_rayT /*0x4c4bd4*/ > 3.0f /*0x4a988c*/) p->ledge = 1;
}
```

The ray goes steeply down (47°) from the chest to the floor point 40 ahead. Flat floor answers `t = 1`, a wall or a rise
`t < 1`. Only a first hit **beyond `t = 3`** sets the flag: ground more than `2·43 = 86` below the feet at 120 or more
ahead, i.e. a drop-off or a downhill slope steeper than about 35.6° (`tan θ ≥ 1.075 − 43/120`). A ray that finds nothing
(type 1) does **not** set it.

**It is a ledge sensor only.** A wall can never answer `t > 3`, and it cannot even be seen: the collision radius (69)
keeps Woody further from a wall than the 40 the sensor looks ahead, so the ray always reaches the floor (`t ≈ 1`). The
charge run into a wall ends by the collision (and, with charge left, keeps pushing). Getting "half inside" an instance is
therefore a matter of the instance collision (PERSO_MOVE.md §6.5), not of this sensor.

Readers (the only two, `0x457abe` / `0x457c20`; `+0x234` is otherwise only cleared by the ctor at `0x44abe6`):

* sub-state 9 (windup) `0x457abe`: flag set ⇒ `+0x5fc = 0`, `+0x5f8 = 0`; action 4 held (`0x467400(4)`) ⇒
  `LockMove(0, 1)`, AnimCtrl reset, `atk = 0` (the jump follows); otherwise the brake `0x457b0a` (charge 0,
  `T = AnimLen(0x12,0) + AnimLen(0x12,1)`, `LockMove(T, 0)`, anim 0x12, → 11). This comes **before** the auto-aim and the
  windup timer.
* sub-state 10 (run) `0x457c02`: runs on only while `charge > 0 && !ledge && !Held(4)`; otherwise the same two exits.

### 2.1 Port and test

`check_steep()` in `src/player.c` (called after the collision, `p->steep_edge`), read in `attack_update` cases 9/10;
`WOODY_EDGELOG=1` prints the sensor every frame of a charge run.

W1A, charge run from the start pad to the right edge of the walkway:
`out/woody.exe <Data> W1A --newgame --pos 537 -1977 -2148 --yaw 77.5 --peck 1.0 0.6`.
Before: Woody runs off the edge at x ≈ 1130 and dies in the pit (kill kind 1). Now: at x = 1024 the sensor answers type 3,
`t = 15.3` ⇒ brake, the peck animation 0x12 plays on the edge and he stays there. The same run towards the wall behind
the pad (`--yaw 167.5`) is unchanged: `t = 1.00` all along, he slides along the wall until the charge runs out.

## 3. The enemies' obstacle sensor `Enemy+0x124`

Created in PostLoad `0x419e30` (`0x419f63`, 0xb8 bytes), for every class derived from the Enemy base.

| off | type | meaning |
|---|---|---|
| 0x00 | Enemy* | owner |
| 0x04 | float | range `R`, 200 at start; swings 200 → 100 → 200 in steps of `+0x08` |
| 0x08 | float | step 10 (sign flips at the ends) |
| 0x0c | float | `S = sqrt(2 R²)` = expected distance on flat floor |
| 0x10 | int | index of the next direction to probe (0..15) |
| 0x14 | float | jitter: `+= π/40` (`0x4aa160`) per probe, back to 0 once `≥ π/8` |
| 0x18 | int | last mode |
| 0x1c + 8i | int | kind of direction i (0..15); **0x9c** = a 17th slot, always 1 |
| 0x20 + 8i | float | angle of direction i: ctor `i·π/8` (`0x4aa15c`), each probe `i·π/8 + jitter` |
| 0xa4 + i | u8 | free flag of direction i; **0xb4** = the 17th, always free (ctor sets it to 1) |

Angles follow the enemy convention: direction `(cos a, 0, sin a)`.

### 3.1 Tick `0x41d4a0(mode)`

Called by `Enemy::Update` `0x41a3e0` when `flags(+0x174) & 8` is clear and the behaviour kind (`behav->vtbl[7]`) is
neither 3 (Pad) nor 5 (Stand still), i.e. **only under Wander (0) or Chase (1)**. `mode = vtbl[46]()` = `0x45bd30`
= **3 for every class** (4..16: vtables `0x4a9ec0`, `0x4a9dc0`, `0x4a9cb8`, `0x4a9bb0`, `0x4a9ab0`, `0x4a99b0`, `0x4a98a8`,
`0x4a9778`, `0x4a9658`). Enemy::Update is called by the Updates `0x418cf0` (4/5/6), `0x416fb0` (7/8/9), `0x415490`,
`0x412310`, `0x4110c0` (12), `0x413ab0` (13), `0x40eec0` (14), `0x40dd30` (15), `0x40cb80` (16).

```c
R += step; if (R > 200) { R = 200; step = -step; } else if (R < 100 /*0x4a9010*/) { R = 100; step = -step; }
S = sqrt(R*R*2);
Probe(mode);                                   /* 0x41d010: ONE direction per frame, a full sweep takes 16 frames */
for (i = 0; i < 17; i++) free[i] = Pass(mode, kind[i]);   /* 0x41d430; then a no-op loop +5/-5 on Enemy+0x10 */
/* Pass 0x41d400: (mode & 1) && kind == 2 => blocked; (mode & 2) && kind == 3 => blocked; else free */
```

### 3.2 Probe `0x41d010`

```c
jit += PI/40; if (jit >= PI/8) jit = 0;
a = i*PI/8 + jit;  ang[i] = a;
vec3 p = *e->vtbl[34]();                       /* position = feet */
vec3 st = p + (0, R, 0);                        /* R above the feet */
vec3 en = p + (cos a·R, 0, sin a·R);            /* the floor point R ahead: 45 degrees down */
if (World_Cell(st) /*0x428cc0 -> 0x40aba0 -> 0x40ab60*/ < 0) kind[i] = 0;
else {
    Ray_Endless(&st, &en, cell);                /* 0x435810 -> 0x497a30(st, en - st, -1) */
    dist = |[0x53a558] * (en - st)|;
    if (S + sqrt(2·P[0x2c]²) < dist) kind[i] = 3;        /* the floor drops more than P+0x2c (10) */
    else if (dist < S - sqrt(2·P[0x30]²)) kind[i] = 2;   /* wall, or the floor rises more than P+0x30 (10) */
    else kind[i] = 1;
}
kind[16] = 1; if (++i >= 16) i = 0;
```

On flat floor the 45° ray hits at exactly `S`; a floor `h` lower hits `h·√2` further, `h` higher `h·√2` nearer, so the
two thresholds are exactly the step-down / step-up limits of the common move `0x41b2c0` (ENEMY.md §5.1). With
`P+0x2c = P+0x30 = 10000` (subtypes 9/10, the flyers), `15000` (boss 14, BOSS14.md) or `5000` (subtype 13, BOSS15_16.md)
nothing is ever blocked. A start point outside the world (cell < 0) answers 0 = free; a ray without a hit reads the stale
`[0x53a558]` of whatever ray ran before (unpredictable; the port answers 3, a drop).

### 3.3 Queries

| function | does | caller |
|---|---|---|
| `0x41d240(a)` | index of the direction nearest `a` (`0x4401c0` = shortest arc) | `0x41d2a0` |
| `0x41d2a0(a)` | `free[nearest(a)]` | Chase Steer `0x41bd00` |
| `0x41d310(a)` | the free direction nearest `a`; none ⇒ `a` itself | Chase Steer `0x41bd00`, Chase OnBlocked `0x41be90` |
| `0x41d2c0()` | a random free direction (`rand() % count`); none ⇒ −1.0 (`0x4a9500`) | Wander choice `0x41c180` (actions 5/7, at `0x41c249`) |
| `0x41d390()` | the free direction with the widest free gap: from each free `i`, step `i−k` / `i+k` until either side is blocked or the right side is back at `i`; the left side wraps from 0 to the sentinel **16** (always free) and then 15 — an off-by-one of the original; ties go to the lowest `i`; none ⇒ −1.0 | Wander OnBlocked `0x41c420` |

Use by the behaviours (ENEMY.md §5):

* **Chase Steer** `0x41bd00`: `H.Tick(dt)` (turns toward the *previous* target), target := direction to the player;
  if `!free(target)` ⇒ target := `nearest_free(target)`. **OnBlocked** `0x41be90` (the common move refused the step):
  target := `nearest_free(H.angle)`, turn timer `+0x34 = 0`; the next Steer overrides it again after one frame of turning.
* **Wander**: a new action 5 or 7 takes `random_free()`, or with nothing free a random angle (`0x41ba10`); other actions
  keep the current angle. **OnBlocked** `0x41c420`: target := `widest_free()` if ≥ 0, then action 5 (turning).
* Pad (path follow) and Stand still do not tick it; the path follower reverses on its own sphere test (ENEMY.md §5.5).

### 3.4 Port and test

`enemy_sensor_init/_tick/_free/_nearest_free/_random_free/_widest_free` in `src/enemy.c`, 17-slot arrays as in the
original. Ticked for types 4/5/6/13 in states 1, 4 (Chase) and 8 (Wander), for the shooters in 3 (Wander) and 6 (Chase),
for boss 14 under Dwalen / Achtervolgen; `EnemyParams.drop/rise` = `P+0x2c/0x30` (10, type 13: 10000, boss 14: 15000).
Chase now steers with the one-frame target lag of `0x41bd00`. Where the port's simplified move refuses a step (ledge or
step over 10) the behaviour's OnBlocked runs as above; with nothing free Wander still turns round (the original keeps its
target). `WOODY_SENSLOG=1` prints the 16 kinds of each enemy once per sweep (direction 0 = +x) and every OnBlocked.

Observed (W1A / K1A / W2B, 25 s each): walls come out as runs of 2 on one side (W1A 282 at 652 −2000 −433:
`1111122211111111`), W2B 217 at the edge of its platform (−3276 −417 −8560) sees the drop as 3 in directions 15/0/1, and
K1A 247, stopped by a step at −18 −2000 −141, turned from 189° to the widest free gap at 63°. An enemy that stands under a
low press-node roof (K1A 247 at 218 −2000 −306: a node top 125 above its feet) sees 2 in most directions, since the ray
starts above the roof; the original would do the same, as that face points at the start.

## 4. Open points

* `0x40ab60` (kd descent of `0x428cc0`) is mirrored by `gel_cell`; whether it can return < 0 for a point inside solid
  geometry (and so give kind 0) is not verified.
* The stale `t` after a type-1 ray (both sensors) is not reproduced: the Perso sensor treats it as no hit, the enemy
  sensor as a drop.
* Instance press nodes are tested two-sided in the port (`inst_ray_press`); the original's `0x431de0` is one-sided with the
  stored plane, whose orientation relative to the port's vertex-order normal has not been checked.
* The port's enemy move has no swept cylinder against walls (ENEMY.md §5.1), so OnBlocked fires only at ledges and steps,
  not at walls as in the original.
