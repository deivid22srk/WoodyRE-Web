# Race levels (K1R K2R K3R S1R S2R S3R) – board riding, Perso state 1 – Woody.exe

Working document. All addresses are VAs in `Woody.exe` (image base 0x400000); float constants were read from
`game/Woody.exe`. Notation as in PERSO_FRAME.md / PERSO_JUMP.md: `p` = Perso, `P` = parameter block `p+0x110`,
`M` = Mover `p+0x388`, `J` = Jumper `p+0x334`, `dt` = `p+0x2f8`, `cam` = camera manager `[0x4c737c]`,
`A` = rider anim controller `p+0x494`, `B` = board anim controller `p+0x498`.
Anim controller vtable (class `0x4ab7b4`): `[1](1)` delete, `[2](n)` = **Request(n)** (+8), `[3](dt)` = Tick (+0xc),
`[4]()` = **Reset** (+0x10: clears the pending request list, current = −1, hold timer = 0), `0x436b90(n, k)` = AnimLen
of sub-anim k of logical anim n (PERSO_JUMP.md §4). Tick picks the pending request with the highest prio; on a tie the
**last** request wins (`0x436a84` scans from the end and only replaces on `>`).

## 0. Summary

* Script types 18/19 → Perso subtype 5/4 → parameter column 3/4 of table `0x4b5f14`. **Columns 3 and 4 are identical in
  all 31 rows**, so Knothead and Splinter ride exactly the same.
* Reset `0x44ab20` for subtype 4/5 calls **SurfEnter `0x456150`** and `SetState(1)`. State 1 = riding. It is entered at
  level start (Game ctor → `0x445930` → `0x44a810` → Reset), after every respawn and after "restart race" in the pause menu.
* Script message **1120 SetRaceInfo `[board, camera]`** (`0x444b27` → `0x455dc0`) links the board instance, the polyline
  (TRAJ of the camera object) and creates the board anim controller and the board FX emitter. Until 1120 arrives the rider
  stands still in state 1 (sub-state 0 returns immediately when there is no polyline). Note: at level start SurfEnter
  runs **before** 1120, so at that moment `B` and the FX emitter are still NULL. (The Game ctor `0x445850` comes after the
  init tick that delivers the init messages, GAMEFLOW.md §4, but all six race scripts send 1120 from the trigger volume
  at the start position, i.e. from the volume events of the first game frame - checked in the port for K1R..S3R.)
* Riding (`0x456210`): **constant speed `P+0x1c` = 1250 units/s**, no acceleration, no braking. Actions 0/1 (left/right)
  rotate the direction around world-up at **1.8 rad/s** (1.2 while crouched), with a veto that stops you from steering
  to face backwards along the nearest polyline segment. Action 4 = jump (the normal Jumper, H = 400). Gravity and ground
  following are the normal Jumper + MoveCollide (PERSO_JUMP.md §1, PERSO_MOVE.md §6).
* Boost (message **1121 StartBoostSurf `[inst, speed, t]`**, `0x456000`): for `t/100` s move at `speed` straight at a
  target point on the boost instance's marker line, no steering.
* Crash (`0x4567f0`): if the move this frame was < 70 % of the requested displacement for more than 5 frames in a row,
  **or** two rays straight ahead (length 109) at two heights both hit something → sub-state 2: crash anim 0x70, then
  Kill(8) → race death → normal death/fade/respawn sequence (`0x4459c0`), respawn at the last checkpoint (1030).
* The polyline is used for (a) the start direction (first segment) when there is no checkpoint, (b) the "don't face
  backwards" steering veto, (c) a render region list. It is **not** used for track progress, ranking, respawn or camera.
* Every rider anim request in race code also goes to the board controller `B`; both are ticked with `dt`.

## 1. New Perso fields (+0x4a4 .. +0x4e4) and related

| offset | type | meaning | written | read |
|---|---|---|---|---|
| 0x4a4 | u8 | sound source flags for sound 60 (bit 0 = marked active this frame, bit 1 = playing) | `0x468e50`, `0x468e40`, `0x468e20` | `0x456246` |
| 0x4a8 | int | **race sub-state**: 0 = init, 1 = riding, 2 = crash | `0x456172` (0), `0x456369` (1), `0x4568ed`/`0x456b4d` (2) | `0x45624b`, `0x456811`, `0x464c25` |
| 0x4ac | int | stuck-frame counter | `0x456168`, `0x456363`, `0x4568a5`, `0x456904` | `0x45689c` |
| 0x4b0 | ptr | polyline = TRAJ of the camera object (`cam_obj+0x28`) | `0x455df7`; 0 in PostLoad `0x44a572` | `0x4562b1`, `0x456339`, `0x45637f`, `0x456398`, `0x455f10` |
| 0x4b4 | ptr | **board instance** (flags `|= 0x20`) | `0x455de4`; 0 in `0x44a56c` | `0x44bf90..0x44bfe7`, `0x455e13`, `0x455e58` |
| 0x4b8 | ptr | **board FX emitter** (0x34 B, §2.2) | `0x455ee9`; 0 in `0x44a578` | `0x456193`, `0x4563fe`, `0x456506`, `0x4567c7` |
| 0x4bc | int | **lean state** 0 = neutral, 1 = half left, 2 = full left, 3 = half right, 4 = full right | `0x456373` (0), `0x464c7e..0x464de9` | `0x464cd1` |
| 0x4c0 | f32 | lean anim timer (= AnimLen(anim, 0) of the last lean request) | `0x456379`, `0x464c98`, `0x464d0f` | `0x464c8c` |
| 0x4c4 | f32 | **boost timer** (s) | `0x456020`, `0x45640a`, 0 in `0x455f16`/`0x456199` | `0x4563db`, `0x456594` |
| 0x4c8 | f32 | boost speed (units/s) | `0x456026` | `0x456410` |
| 0x4cc..0x4d4 | vec3 | boost target point | `0x456094..0x4560bf` | `0x45642d`, `0x45643b` |
| 0x4d8 | u8 | race death (death anims via `0x464a00`; HUD race mode) | `0x44c6d9` (1), `0x44ab60`/`0x44c459` (0) | `0x463e8f`, `0x44af18` |
| 0x4dc | f32 | crash timer (s since crash) | `0x4568f3`, `0x456b58`, `0x456c1b` | `0x456bbb..0x456cc4` |
| 0x4e0 | int | race bonus count stored at the last checkpoint | `0x44aaef`/`0x44ab02` (= `+0x264`, checkpoint 1030), 0 in `0x44a55a`, `0x456108` | `0x456181` |
| 0x4e4 | f32 | **start-anim timer** = AnimLen(0x5d, 0); while > 0: no steering, no lean anims, no crouch | `0x4561d5`, `0x4565e3` | `0x4565bf`, `0x464c40`, `0x465b4e` |

Related existing fields with race meaning:

| offset | meaning in state 1 |
|---|---|
| 0x118 (P+0x08) | current body height, rewritten every frame by `0x462490`: crouched `P+0x10` (81) else `P+0x0c` (160) (all states) |
| 0x204..0x20c | displacement of this frame, built by `0x456210` |
| 0x210..0x218 | low-passed up vector (orientation, §4.7) |
| 0x264, 0x71c | race bonus counter (bonus type 37, BONUS.md); restored from `+0x4e0` in SurfEnter |
| 0x318 / 0x324 / 0x330 | respawn position / respawn direction / "checkpoint set" (1030, `0x44aa10`) |
| 0x398 (M+0x10), 0x3a4 (M+0x1c), 0x3bc (M+0x34 = RampA.dir) | facing / velocity dir / ramp dir: all three set to the ride direction at the end of `0x456210` |
| 0x458 (M+0xd0) | ground normal: set at the end of MoveCollide by `0x4628e0` from the last ray-hit normal `[0x4b3108..0x4b3110]` (the floor query), or (0,1,0) if that normal has y < 0; reset to (0,1,0) by `0x459ff0` |
| 0x694 / 0x698 | crouch sub-state 0..3 / crouch timer (§4.8) |

Polyline (TRAJ, CAMERA.md §6.2): `+0` low 16 bits = point count n, `+0x10` = point array, stride 0x10, xyz at `+4/+8/+0xc`.

## 2. SetRaceInfo `0x455dc0(p, board, poly)` – message 1120

Handler `0x444b27`: arg1 must be an instance of type 3 (camera object) with a TRAJ at `+0x28`, else log
`'Message SetRaceInfo : on doit envoyer une camera avec une polyline en 2eme argum…'` and nothing happens.
`0x455dc0(p, inst[arg0], cam_obj->traj)`.

```c
void SetRaceInfo(Perso *p, Inst *board, Traj *poly)          /* 0x455dc0 */
{
    p->board = board;  board->flags(+8) |= 0x20;              /* same bit the Perso sets on itself in PostLoad */
    p->poly  = poly;
    p->animB = new AnimCtl(board);                            /* 0x54 B, 0x463df0(board) – NOT reset, nothing requested */
    Emitter *e = new Emitter;                                 /* 0x34 B */
    e->inst = board;  e->n = 0;
    seg2 tmp;  while (Inst_GetMarker(board, /*typecode*/9, &tmp, e->n)) e->n++;   /* 0x42f6b0: count type-9 markers */
    e->prev = malloc(12 * e->n);  e->hasPrev = malloc(e->n);
    e->active = 0; e->mode = 2; e->t10 = e->t14 = e->t18 = e->t1c = e->acc20 = 0; e->sizeIdx = 3;
    e->next = g_emitters /*[0x5e8564]*/;  g_emitters = e;     /* global list, ticked by 0x46d040 */
    p->fx = e;  e->mode = 2;  for (i < e->n) e->hasPrev[i] = 0;
    p->boostT = 0;
    /* render region list: distinct polygon groups under the polyline points, in track order */
    int list[5] = {-1,-1,-1,-1,-1}, k = 0;
    for (i = 0; i < n; i++) {
        int g = 0x40a0c0(&pts[i].pos, -1);                    /* polygon group of the floor under the point */
        if (g == -1) continue;
        if (list[k] == -1) list[k] = g;
        else if (list[k] != g) { if (++k == 5) break; list[k] = g; }
    }
    scene = [0x509adc];  scene+0xc0..0xd0 = list[0..4];  scene+0xd4 = -1;   /* terminator */
}
```

### 2.1 The region list `renderer+0xc0` (render only)

`[0x509adc]` **is** the renderer / scene object App+0x28 (its ctor `0x42a440` stores `this` there at `0x42a451`), so
SetRaceInfo writes into the same object whose `+0xc0` the frame pipeline passes to `0x42a980`. The ctor does not write
`+0xc0..+0xd4`; only `0x455f95..0x455fed` does, so the list lives as long as the App (it survives into the next level).
(`0x42e250`, which also writes `-1` to a `+0xc0`, is the reset of the entity base class, vtable `0x4aa31c`, not this.)

**Call site** `0x401c06..0x401c63` (PERSO_FRAME §1 step 9): camera position = `0x41fa30([0x4c737c]) + 0x90` (the eye);
`0x40c350([esi+0x34])` = Perso subtype (`(vtbl[4]()->[0] >> 5) & 0x1f`); subtype 4 or 5 → `0x42a980(&eye, renderer+0xc0)`,
any other → `0x42a980(&eye, NULL)`.

**`0x42a980(pos, list)`** (this = renderer):

| Address | What |
|---|---|
| `0x42a989..0x42a9be` | counts `+0x48` (sectors), `+0x54` (groups), `+0x60` (instances) = 0; stamps `[0x4c3bac]` (sector), `[0x4c4ca8]` (group), `[0x4c4c08]` (instance) `++` |
| `0x42a9c4..0x42a9f8` | kd leaf of pos (`0x408180`) for the type-1 volume objects (`0x42aa0b`, INSTANCE.md 10.1); `race = list && list[0] != -1` (`[esp+0x10]`) |
| `0x42aa77` | `E = 0x408210(pos)`: sector `s = 0x4081c0(pos)`, `g = 0x40a0c0(pos, -1)`, the entry of `world+0x28[s]` whose `id == g`, else the first entry (`0x40824f..0x408256`); returns `&E.pair_count` |
| `0x42aa8c..0x42aadd` | not race: every pair's **second word** = a `.gel` group (record `world+0x38 + g·0x18`); not yet stamped → appended to `+0x58`, stamped |
| `0x42aadf..0x42ab52` | race: `z = 0x40a0c0(pos, -1)`; `z == -1` → **no group at all** (`0x42aaed`); otherwise search `z` in the list **without a bound** (`0x42aaef..0x42aafb`: past the `-1` at `+0xd4` into whatever follows the renderer's `+0xd8`), append `z`, then the entry after it if `!= -1` (`0x42ab25..0x42ab52`); no "already stamped" test here |
| `0x42ab54..0x42aba7` | both paths: every pair's **first word** = a sector; not yet stamped → appended to `+0x4c`, stamped, `0x42a840(sector, race)` |

**`0x42ac10`** then stamps every polygon `[first, last]` of each group of `+0x58` (`0x42ac32..0x42ac75`) and draws, per sector
of `+0x4c`, only the polygons of its list that carry this stamp (`0x42acc7`). So a face is drawn iff it lies in a marked
group AND in the polygon list of a marked sector. In the 22 non-race levels there is one group, so the group word changes
nothing; in the six race levels (the region lists name zones 0..4; K1R/S1R, K2R/S2R, K3R/S3R share the geometry) only two track zones are drawn.

**`0x40a0c0(pos, cell)`** – the floor group under a point (the floor polygon's group, not the polygon):
start cell = `cell`, or `0x408180(pos)` when `-1`. In that cell's polygon list take every polygon with `ny >= 0`
(`0x40a141`), `d = n·pos + D > 0` (`0x40a16a`), `d <= ny·(pos.y − cell.ymin) + 0.001` (`0x40a17b`, `[0x4a94c4]`, cell
`+0x30` = bbox ymin: the plane is not below the cell's bottom) and `pos` inside its xz shadow (every edge prev→cur:
`(cur.x−p.x)(prev.z−p.z) − (prev.x−p.x)(cur.z−p.z) >= 0`, `0x40a1b3..0x40a1f8`). The loop does not stop at a hit: the
**last** hit in the list wins (`0x40a202`). Without a hit: the cell's −y link `+0x18`: `>= 0` → local subtree `0x40ab60`,
`INT_MIN` → return −1, else cell `~link`. Hit → `0x40a26a`: the first group whose last polygon `>= poly`.

**`0x42a840(sector, race)`** (the entity list of the sector, `+0x44`/`+0x24` chain): an entity is taken only if its
**`+0x18` != −1 and that group carries this frame's group stamp** (`0x42a858..0x42a87d`); `+0x18` is the entity's floor
group, written by `0x40a0c0` in the sector insert `0x4077bc` (FORMAT_GEL.md §5 called it the floor polygon). So in the
races the instances of the undrawn zones are dropped too. A type-1 entity with `+0x88 == 1` must also pass the sphere
view test `0x437b00(+0x8c, r = +0x98)`; with `race == 1` additionally `|cam − c|² + r² <= 121000000` (`[0x4aa2f4]`,
about 11000², `0x42a8c8..0x42a91a`), otherwise it is stamped as handled and not taken. **Not ported** (the visible-instance list world+0x64 is a separate item).

**The sky** (SKY.md §1): the cube is drawn only if a sky face got the stamp; with two groups stamped that can switch off.
In the data it does not on the track: at every fly-through camera position on the track (below) the sky stayed on; it
goes off only with the camera outside the track geometry, where the floor group is −1.

Region lists and zones of the data (port, `WOODY_RACEVISLOG=1`): K1R/S1R `1 2 0 3 4` (20 points), K2R/S2R `4 0 1 2 3`
(14), K3R/S3R `0 1 2 3 4` (33). So the last zone of the track is drawn alone, the others with the next one.

**Port** (round 30, `src/level.c`, `src/render_gl.c`): `gel_floor_poly` / `gel_floor_group` = `0x40a0c0` + `0x40a26a` (the
cell links and local subtrees are now kept by `read_cells`), `vis_entry` = `0x408210`, `gel_race_regions` = the tail of
SetRaceInfo; message 1120 calls `rnd_set_race` only for `race_char` (subtype 4/5, `0x401c36`). `world_visibility` takes the
race path when the list is set and the camera is in a sector with a `.vis`: sectors = the pairs of `vis_entry` (no extra
"own sector", no union of both entries), groups `z` + next, faces filtered by `FaceBatch.zone` in `add_face`, sky cube only
if a sky face of those sectors lies in `z`/next (computed before the frustum test). Port choices: `z` not in the list →
`z` alone (the original reads past the list); the port's sector frustum test stays on top. `WOODY_NORACEVIS=1` turns it
off (for comparison). Test: `python`-driven fly-through with `--cam` along the path, new vs `WOODY_NORACEVIS=1`: K3R
0 differing pixels on the track except particles, K1R/K2R the same except where the camera sits outside the track
geometry (path corners) and at the end of K1R, where the elevated start section (zone 1) above the finish is not drawn
while its instances still were (the original drops those too, `0x42a840`: now ported as the group filter of `rnd_instance_list`,
INSTANCE.md §4.1 - the start section's instances seen from the finish are gone as well).

### 2.2 Board FX emitter (0x34 B; visual only – skippable)

| off | meaning |
|---|---|
| 0x00 | instance (board) |
| 0x04 | n = number of markers with typecode 9 on the board |
| 0x08 | mode: 0 = off, 1 = fade envelope (not used by race), **2 = normal, 3 = boost** (sprites twice as big) |
| 0x0c | u8 active: set every riding frame by `0x456210` (`0x4567d1`); `0x46d040` ticks the emitter (`0x475440`) only if set, then clears it |
| 0x10/0x14/0x18 | phase accumulators (+= dt·0.05, dt·0.15, dt·3.0; wrap at 1.0) |
| 0x1c, 0x20 | mode-1 timer; emission accumulator (200 per s, `0x4aa164`) |
| 0x24 | vec3[n] previous marker positions |
| 0x28 | size index (3 → sizes 45 / 40 / 35 from tables `0x4abcc8/0x4abccc/0x4abcd0`) |
| 0x2c | u8[n] "has previous position" (cleared by SetRaceInfo and SurfEnter) |
| 0x30 | next emitter in `[0x5e8564]` |

`0x475440` (631 instructions, read in full in round 29; `S` = the shared sprite `[0x5e823c]+0xb00`, PARTICLES.md §1,
`costab`/`rnd`/`ftol` as there; the K boards have 2 type-9 markers, the S boards 1). It runs from the effect driver
`0x46d040` (after the lasers, before `0x47cea0` and the pool `0x470c70`) for every emitter of `[0x5e8564]` whose `+0xc`
was set this frame, and clears `+0xc` afterwards - so it only draws in frames where `Race_Ride` ran to its end.
```c
ph0 += dt·0.05; ph1 += dt·0.15; ph2 += dt·3.0;  each: if (≥ 1) −= 1            /* 0x4aab4c, 0x4aa1c8, 0x4a988c */
if (mode) acc += dt;  n = ftol(acc·200); acc −= n·0.005;                        /* 0x4aa164, 0x4abd08: 200 puffs/s per marker */
if (mode == 1) { t1c += dt; if (t1c ≥ 1) { mode = 2; t1c = 0; } }
if (mode == 0) return;
s = mode 1 ? (t1c in (0,0.15): t1c·6.667 | (0.3,0.45): (t1c−0.3)·6.667 | (0.85,1): (t1c−0.85)·6.667 | else 0) : –
A = [0x4abcc8 + 4k], B = [0x4abccc + 4k], C = [0x4abcd0 + 4k]  with k = +0x28 = 3  ⇒  45, 40, 35
for (i < n) seg[i] = 0x42f6b0(board, 9, i)                                      /* P0, P1 */
for (i < n) {
    d = hasPrev[i] ? prev[i] − P0 : P1 − P0;  hasPrev[i] = 1;  len = |d|;  if (len > 0) d /= len;
    X, Y = 0x46d320(d)                                                          /* rows X, Y, Z = d (PARTICLES.md §1) */
    /* 1: glow, camera facing */   S = { pos P0, rgb 1, alpha 1 − 0.2·sin512(ftol(256·ph0)), image 0x10020 (bank 0 image 32),
                                         mode 0x12, size B (mode 1: s·B, mode 3: 2B), rot 512 − ftol(512·ph0) };  0x470f10(S, 7)
    /* 2: glow in the plane ⟂ d */  S.normal = d; size C (s·C / 2C); alpha 1 − 0.2·sin512(ftol(255·ph1)); rot ftol(512·ph1);  0x470f10(S, 6)
    /* 3: three flames round d */   r = rnd·10 − 5;  size = A + r (mode 1: s·A + r, mode 3: 1.3·A + r, 0x4abcfc); mode 0x13 (mode 3: 0x12);
                                    pos = P0 + d·(size·cos512(base[mode]) − size/64)    /* base 37 for 0x13: a 2:1 quad from P0 back along d */
                                    alpha 0.8, image 0x1001f (bank 0 image 31), UV set 2
        for (j = 0; j < 255; j += 85) { a = ftol(j + 512·ph2) mod 512;
            S.R = d;  S.F = X·cos512(a) + Y·sin512(a);  S.N = X·cos512(a+128) + Y·sin512(a+128);  0x470f10(S, 0x62) }
    /* 4: puffs */ for (k = n−1 .. 0) { rec = pool record (0.2 s, callback 0x475380); if the pool is full: skip;
        rec.age = k·dt/n;  rec.pos = P0 + d·((k/n)·len + A);  rec.rot = ftol(rnd·512);
        prev[i] = (+0x28 == 6 || +0x28 == 9) ? P1 : rec.pos }                     /* not updated when n == 0 */
}
0x475380 (a puff): u = age/0.2; u > 1 ⇒ free; S = { pos, rgb 1, alpha 0.3 − 0.3u, image 0x1000e (bank 0 image 14),
                    mode 0x12, rot, size (u + 1)·15 };  0x470f10(S, 7)
```
All draw flags lack 8, so everything is additive. The table at `0x4abcc8` continues 50/70/35, 45/40/35, 100/70/60, 100/100/70
for the other size indices; only 3 is used by the race. On K1R this is the pair of jet flames with a smoke trail behind the
rocket board (the "water/snow spray" guess above was wrong: the board is a jet board).

## 3. Enter, restart, boost

### 3.1 SurfEnter `0x456150` (from Reset `0x44ac33`, subtype 4/5 only)
```c
SndSrc_Stop(&p->snd4a4, 0, 60, sfx);                         /* 0x468e20 */
p->stuck = 0;  p->raceSub = 0;
RaceBonus_ResetAll();                                        /* 0x44f8a0: [0x5e54f8] = 0, all type-37 bonuses back */
p->+0x264 = p->+0x71c = p->bonusAtCheckpoint(+0x4e0);
p->boostT = 0;
if (p->fx) for (i < fx->n) fx->hasPrev[i] = 0;
p->startT(+0x4e4) = AnimLen(A, 0x5d, 0);
A->Reset(); A->Request(0x5d);
if (B) { B->Reset(); B->Request(0x5d); }
```
Reset then calls `SetState(1)` (`0x44c980`: prev state, clears `+0x50c +0x5f0 +0x5b4 +0x5cd +0x6ac`) and continues with the
normal Reset (`+0x694 = 0`, up filter `+0x210 = (0,1,0)`, `0x459ff0(M, +0x324)`, Jumper reset, `health = 3` if ≤ 0, …).

### 3.2 Race restart `0x4560f0` (pause menu item 18, `0x40584d`)
Page 0x19 and the whole sequence: INPUT.md §5 (ported: `player_restart`).
The menu first does `cam 0x41fa80(4.0)`, `0x445930(Game)` (normal respawn), then:
```c
p->hasCheckpoint(+0x330) = 0;  p->bonusAtCheckpoint(+0x4e0) = 0;
p->respawnPos(+0x318) = p->startPos(+0x30c);
Perso_Respawn(p, 0);                                         /* 0x44a810: pos = +0x318, Reset (→ SurfEnter, state 1), ground snap */
if (B) { B->Reset(); B->Request(0); }                        /* board: logical anim 0 instead of the 0x5d start anim */
```
The request is short-lived: the menu left the Game in state 0 with 0.1 s to go (`0x445930`), and when that runs out
state 0 calls `0x445930` again (PERSO_FRAME.md §4.1) - `0x44a810` → Reset → SurfEnter gives `B` Reset + Request(0x5d)
once more. The board shows anim 0 only for those 0.1 s, behind the closed iris.

### 3.3 StartBoostSurf `0x456000(p, seg, speed, dur)` – message 1121 `[inst, a, f]`
Handler `0x444a37`: `seg` = marker typecode 0, index 0 of `inst` (`0x42f6b0(inst, 0, &seg, 0)`; if missing only a log
`'Pas de Vecteur dans l'instance du message StartBoostSurf'` and an uninitialised seg is used), `speed = (float)a`,
`dur = f · 0.01` (`0x4aa0ac`). Always applied to the Perso (`Game+0`).
```c
p->boostT = dur;  p->boostSpeed = speed;
d = xzNormalize(seg.p1 - seg.p0);                            /* only if length > 0 */
p->boostTarget(+0x4cc) = seg.p0 + (d.x, 0, d.z) * (speed * dur * 1.1f);   /* 0x4aa168 = 1.1 */
Sound2D(59);                                                 /* 0x468a00(sfx, 0x3b, 0) */
```

## 4. State 1 per frame

### 4.1 Order inside Perso_Update `0x44b530` (only what differs)
1. The common pre-steps run as for every state (PERSO_FRAME §2.1). Race-relevant: `0x465b10` crouch uses **action 8**
   in state 1 (§4.8); `0x457330` starts attacks only in state 0 (no attacks while riding); `0x44b980` action 7 in state 1
   only plays sound 9; `0x465e50` (air dash) does nothing for subtypes 4/5; `0x44b220` fall damage still applies
   (jumper "landed" with fallen height ≥ `P+0x7c` = 1500 ⇒ −1 health; health ≤ 0 ⇒ Kill(3)).
2. `0x45b0a0` M_LoadParams (harmless; the Mover itself `0x45b110` is **not** run in state 1).
3. If not frozen: **`0x456210` Race_Ride** (§4.2).
4. **`0x4624f0` MoveCollide** (also when frozen). Differences: body height `P+0x08` from `0x462490` (160 / 81); in state 1
   with `+0x694 != 0` the wall radius is `P+0x04 · 0.5` = 34.5 instead of 69 (`0x462517`).
5. **`0x4567f0` Race_CheckCrash** (§4.5) – whenever state == 1, also when frozen.
6. The rest as usual; `0x44bd00` → `0x44bd30` orientation with the up filter (§4.7) and `0x44bf10` board copy;
   `0x463e60` anims: state 1 → `0x464c20` (§4.6); `0x44ae60` HUD: `HUD+4 = 1` in state 1 or when `+0x4d8`
   (race HUD), `HUD+0x24 = +0x264` (race bonus count).

### 4.2 Race_Ride `0x456210`
```c
void Race_Ride(Perso *p)
{
    SndSrc_Update(&p->snd4a4, /*inst*/0, /*id*/60, sfx, /*vol*/-1.0f);
        /* 0x468e50: if last frame was marked active and not playing -> start 2D loop 60;
           if last frame was NOT marked and playing -> stop; clear the mark. So sound 60 plays while riding. */
    switch (p->raceSub) {
    case 2: p->disp = (0,0,0); Race_Crash(p); return;        /* 0x43ffa0, 0x456ba0 – no mark ⇒ sound stops */
    case 1: p->disp = M->velDir;  break;                     /* 0x456cf0 = copy M+0x1c (unit, set last frame) */
    case 0:
        if (!p->poly) return;                                /* before 1120: nothing, no sound */
        cam->0x41f9f0(2);  cam->+0x368 = 0;  cam->0x41f410(0, 0);                 /* hard cut, follow camera (§5) */
        cam->0x41f660(1.5f);  cam->0x41fa60(100.0f);  cam->0x41fa80(4.0f);         /* zoom, height, distance */
        if (p->hasCheckpoint) p->disp = p->respawnDir(+0x324);
        else p->disp = (pts[1].x - pts[0].x, 0, pts[1].z - pts[0].z);
        p->stuck = 0;  p->raceSub = 1;  p->lean = 0;  p->leanT = 0;
        break;                                               /* falls through: moves already in this frame */
    }
    vec3 closest;
    int i = Polyline_Nearest(p->poly, &p->pos, &closest, 0); /* 0x438400, §4.3; closest is unused */
    vec3 seg = (pts[i+1].x - pts[i].x, 0, pts[i+1].z - pts[i].z);                 /* NOT normalized */
    float step = P->speed(+0x1c /*1250*/) * dt;
    if (p->boostT > 0) {
        p->boostT -= dt;  p->fx->mode = 3;
        step = p->boostSpeed * dt;
        p->disp = (boostTarget.x - pos.x, 0, boostTarget.z - pos.z);
        M_SetRideDir(M, p->disp);                            /* see below */
    } else p->fx->mode = 2;
    p->disp.y = 0;  p->disp = xzNormalize(p->disp);          /* only if length > 0 */
    vec3 old = p->disp;
    float turn = (p->crouch(+0x694) ? 1.2f : 1.8f) * dt;     /* 0x4aa39c, 0x4ab290 (rad/s) */
    if (p->boostT > 0) turn = 0;                             /* value after the decrement above */
    vec3 up = (0, 1, 0);
    if (p->startT > 0) p->startT -= dt;                      /* start anim: no steering */
    else if (held(0)) RotateAxis(&up, value(0) * turn, &p->disp, &p->disp);   /* 0x467400/0x467460, 0x440d40 */
    else if (held(1)) RotateAxis(&up, value(1) * turn, &p->disp, &p->disp);   /* action 0 wins if both held */
    float d1 = dot(p->disp, seg), d2 = dot(old, seg);        /* 0x440070 */
    if (d1 < 0.5f && d1 < d2) p->disp = old;                 /* 0x4a9014: veto steering towards "backwards" */
    p->disp *= step;
    Jumper_Update(&p->J, held(4) /*0x44bb10*/, /*inputAllowed*/1);             /* 0x462d70 */
    p->disp.y = J->disp.y;                                   /* 0x463130 */
    M_SetRideDir(M, p->disp);
    p->fx->active = 1;
    SndSrc_Mark(&p->snd4a4);                                 /* 0x468e40: bit 0 */
}
/* M_SetRideDir (inlined twice, 0x456453.. and 0x456718..): */
    M->rampA.dir(+0x34) = (d.x, 0, d.z);  xzNormalize;  if (|rampA.dir| < 0.01f /*0x4a94f8*/) rampA.dir.x = 1.0f;
    M->velDir(+0x1c) = M->dir(+0x10) = M->rampA.dir;
```
Notes:
* `RotateAxis(axis, a, in, out)` `0x440d40` with axis (0,1,0) computes (quaternion `0x440d10` → matrix `0x440370`,
  row vector × matrix): `out.x = x·cos a − z·sin a`, `out.z = x·sin a + z·cos a`. Keyboard: `value(0) = −1`,
  `value(1) = +1` (PERSO_MOVE §3.4); joystick gives the axis value. In the right-handed world of CAMERA.md
  (`right = f × up = (−f.z, 0, f.x)`), a > 0 turns towards screen-right, so action 1 turns right. Check in-game.
* Because `seg` is not normalized, `d1 < 0.5` is in practice "the new direction points at or past perpendicular to the
  segment" (segments are hundreds of units long). The veto only undoes this frame's rotation, it never pushes back.
* The rider is never slowed down: not by crouching, not by walls (walls are handled by MoveCollide sliding + the crash
  test), not by slopes. `+0x238` (LockMove, set by crouching and hard landings) is not read in state 1.
* Before 1120 and while frozen: `disp` stays 0. See §4.5 for why that matters.

### 4.3 Polyline_Nearest `0x438400(poly, P, out, start)` (thiscall on the polyline)
```c
if (start < 0 || start > n - 2) start = 0;
best = -1;
for (i = start; i < n - 1; i++) {                            /* full 3D */
    A = pts[i].pos;  B = pts[i+1].pos;  d = B - A;
    if (dot(d,P) - dot(d,B) > 0)       c = B;                /* beyond B */
    else if (dot(d,P) - dot(d,A) < 0)  c = A;                /* before A */
    else c = A + d * ((dot(d,P) - dot(d,A)) / (dot(d,B) - dot(d,A)));
    if (best == -1 || |c - P|² < bestD2) { best = i; bestD2 = |c - P|²; *out = c; }
}
if (best == -1) { *out = pts[0].pos; return 0; }             /* n < 2 */
return best;
```
Called only from `0x456393` with start 0 (the whole polyline is searched every frame).

### 4.4 Race_Crash `0x456ba0` (sub-state 2)
```c
SndSrc_Stop(&p->snd4a4, 0, 60, sfx);
if (p->crashT == 0.0f) {                                     /* first frame */
    cam->0x41fa80(400.0f);                                   /* follow distance 400 */
    A->Request(0x70);  B->Request(0x70);
    Jumper_ForceFall(&p->J, 0);                              /* 0x463170: unless already falling (J 3/4/5) -> J state 4 */
}
p->crashT += dt;
float L = AnimLen(A, 0x70, 1) + AnimLen(A, 0x70, 0);
if (p->crashT >= L) { Jumper_Update(&p->J, 0, 1); p->disp = J->disp; }   /* before L: disp = 0, hangs in place */
if (p->crashT >= L && p->onGround) { Rumble(p, P+0xdc, P+0xd8 /*0.5, 0.5*/); p->vtbl[38](8); }   /* TakeHit(8) */
if (p->crashT >= 2.0f /*0x4a9870*/) p->vtbl[38](8);
```
TakeHit `0x44c110` first ignores the hit in cinematic mode (`0x44f2e0/0x44f2d0`) or when `App+0xcc` is set; then
subtypes 4/5 go straight to the race kill `0x44c4c0` (§4.9, `0x44c172` → `0x44c475`), before the shield scan and the
normal per-kind code. (PERSO_FRAME §2.2 says hits are ignored for subtypes 4/5; that is wrong.)

### 4.5 Race_CheckCrash `0x4567f0` (after MoveCollide, every state-1 frame)
```c
if (p->raceSub == 2) return;
/* (a) stuck test */
float moved = |p->oldPos(+0x28c) - p->pos|;                  /* 3D, 0x440040 */
float want  = |p->disp|;                                     /* 3D, after 0x4627d0 actor push-out */
if (moved / want < 0.7f /*0x4aa1d8*/) {
    if (++p->stuck > 5 && !cheat[0x5d7b8b]) {
        Effect_Star(&(inst.pos + (0, 150, 0)));              /* 0x4750e0, 0x4a9754 = 150 */
        p->raceSub = 2;  p->crashT = 0;  Rumble(p, 0.5, 0.5); /* P+0xdc, P+0xd8 */
    }
} else p->stuck = 0;
/* (b) wall ahead: two rays, crash only if BOTH hit */
vec3 F = xzNormalize(M->dir(+0x10));                         /* 0x445780 */
vec3 U = p->groundN(+0x458);
vec3 fw = U ⊗ (F ⊗ U);                                       /* 0x41af10 twice = F projected on the plane ⊥ U (times |U|²) */
vec3 end = pos + fw * (P+0x04 + 40.0f);                      /* 69 + 40 = 109; 0x4ab294 */
bool both = true;
for (h in { P+0x08 - 10.0f /*150 standing, 71 crouched*/, 40.0f }) {        /* 0x4a9750 = 10 */
    Ray(pos + (0,h,0), end + (0,h,0), -1);                   /* 0x4359b0; world or instance, any hit type */
    if ([0x53a554] == 0) both = false;
}
if (both && !cheat[0x5d7b8b]) {
    Mover_SetFacing(M, (-hitN.x, 0, -hitN.z));               /* 0x459ff0; hitN = [0x4b3108..0x4b3110] of the h=40 ray */
    p->raceSub = 2;  p->crashT = 0;
    Effect_Star(&(inst.pos + (0, 150, 0)));  Rumble(p, 0.5, 0.5);
}
```
Surprise: when `disp` is exactly 0 (frozen, or state 1 before 1120 arrived) the ratio is 0/0 = NaN, and the x87 compare
at `0x45688f` treats NaN as "less" (`C0 = 1`), so such frames count as stuck: 6 of them in a row cause a crash (and Kill(8)).
SurfEnter and sub-state 0 reset the counter, so it only matters if the script sends 1120 late or the rider is frozen.
A port should probably treat `want == 0` as "not stuck" (deviation) or reproduce it deliberately.
`Mover_SetFacing` also resets the Mover ramps and sets `M+0xd0` to (0,1,0); `M+0x1c` keeps the old ride direction.

### 4.6 Anims in state 1 – `0x463e60` → `0x464c20`
`0x463e60`: state 1 → `0x464c20`; then for all states: if `cam+0x138 == 2` (fixed camera) ⇒ `A->Request(1)`;
then `A->Tick(dt)` and, if `B`, `B->Tick(dt)`.
```c
void Race_Anims(Perso *p)                                    /* 0x464c20 */
{
    if (p->raceSub == 2 || !B || p->startT > 0) return;
    if (Jumper_State(&p->J) != 2) { JumpAnims(p, 0); return; }   /* 0x4642f0, state-1 set below */
    if (p->crouch) { p->lean = 0; return; }
    p->leanT -= dt;  if (p->leanT > 0) return;
    bool L = held(0), R = held(1) && !L;
    int a = 0;
    switch (p->lean) {
    case 0: if (L) { lean = 1; a = 0x5e; } else if (R) { lean = 3; a = 0x63; } break;
    case 1: if (L) { lean = 2; a = 0x5f; } else { lean = 0; a = 0x62; } break;
    case 2: if (!L) { lean = 1; a = 0x61; } break;
    case 3: if (R) { lean = 4; a = 0x64; } else { lean = 0; a = 0x67; } break;
    case 4: if (!R) { lean = 3; a = 0x66; } break;
    }
    if (a) { p->leanT = AnimLen(A, a, 0); A->Request(a); B->Request(a); }
}
```
In the air, `0x4642f0(p, 0)` with `state == 1` uses (request on A and B):

| Jumper state | anim |
|---|---|
| 0, 1 (rising) | **0x6b** |
| 7 (apex) | **0x6c** |
| 3 | `fellOff` ⇒ **0x6e**; else `shortHop` ⇒ **0x6d**; else none |
| 4 | onGround and Mover phase (`M+0xc`) ≠ 2 ⇒ **0x6f** (the Mover is not run in state 1, so its phase is whatever Reset left: 0); else as 3 |
| 5 | in the air ⇒ **0x6f**; on the ground ⇒ **0x6f** + LockMove(AnimLen(0x6f,0)) + `+0x524 = AnimLen` + `0x478980(p, 1, 2.0, 180, 50, 0)` |
| 6 | none (dust `0x476140` if ground type `+0x308 == 2`) |

Complete list of logical anims used by the race code (records `0x4b6180 + n·0x1c`; sub-anims are indices into the
rider's and the board's own 43 animations; chain: sub[0] → sub[1] → … last one loops; −1 = stop):

| n | subs | prio | speed | requested when |
|---|---|---|---|---|
| 0x00 | 0,0,0,0 | 1100 | 3 | board only, race restart `0x4560f0` |
| 0x01 | 0,0,0,0 | 6500 | 3 | rider only, camera mode 2 (all states) |
| 0x5d | 42,0,0,0 | 1100 | 3 | SurfEnter (start anim; its first sub = `+0x4e4`) |
| 0x5e | 1,20,8,8 | 1501 | 2 | lean 0 → 1 (left) |
| 0x5f | 20,8,8,8 | 1501 | 6 | lean 1 → 2 |
| 0x61 | 21,2,0,0 | 1501 | 6 | lean 2 → 1 |
| 0x62 | 2,0,0,0 | 1501 | 3 | lean 1 → 0 |
| 0x63 | 3,22,9,9 | 1501 | 2 | lean 0 → 3 (right) |
| 0x64 | 22,9,9,9 | 1501 | 6 | lean 3 → 4 |
| 0x66 | 23,4,0,0 | 1501 | 6 | lean 4 → 3 |
| 0x67 | 4,0,0,0 | 1501 | 3 | lean 3 → 0 |
| 0x68 | 5,6,6,6 | 1750 | 5 | crouch down (action 8) |
| 0x69 | 6,6,6,6 | 1750 | 3 | crouch hold (every frame in crouch state 2) |
| 0x6a | 7,0,0,0 | 1750 | 5 | stand up |
| 0x6b | 24,25,25,25 | 1501 | 8 | jump rising |
| 0x6c | 29,26,27,27 | 1500 | 3 | jump apex |
| 0x6d | 26,27,27,27 | 1000 | 3 | falling after short hop |
| 0x6e | 26,27,27,27 | 5000 | 3 | falling off an edge |
| 0x6f | 28,0,0,0 | 1500 | 3 | landing / long fall |
| 0x70 | 30,32,33,33 | 6000 | 3 | crash |
| 0x71 | 34,−1,−1,−1 | 6000 | 3 | death kind 8 (crash, fall damage) |
| 0x72 | 38,−1,−1,−1 | 6000 | 3 | death kind 2 (lightning/laser) |
| 0x73 | 36,−1,−1,−1 | 6000 | 3 | death kind 6 (explosion) |
| 0x74 | 37,−1,−1,−1 | 6000 | 3 | death kind 3 (health gone) |
| 0x75 / 0x76 | 39,40,40,40 / 39,−1,−1,−1 | 6000 | 3 | death kind 1 (pit): 0x75 on the rider, **0x76 on the board** |
| 0x77 | 41,−1,−1,−1 | 6000 | 3 | death kind 7 (water) |

0x60 (8 loop) and 0x65 (9 loop) are never requested; those loops are reached through the chains. Durations
(`AnimLen = frames/4096/speed`) depend on the models (K1R: rider model 16, board model 17) and were not computed here.
Death anims (`0x464a00`, table `0x464b48`) are requested every frame of state 2; kinds 4/5 request nothing.
Kind 1 additionally calls `0x41fb50(cam, inst.pos + (0,100,0), p)` every frame while `T − dt ≤ AnimLen(0x75, 0)`.

### 4.7 Orientation `0x44bd30` and board copy `0x44bf10`
```c
F = normalize(M->dir);  F.x = -F.x;  F.z = -F.z;             /* 0x445780, 0x4239f0 */
if (state == 1) { p->upF(+0x210) = p->upF * 0.9f + p->groundN(+0x458) * 0.1f;  up = p->upF; }   /* not renormalized */
else up = (0,1,0);
right = F ⊗ up;  fwd = up ⊗ right;                           /* 0x41af10 */
inst.rot rows: +0x28 = right, +0x34 = fwd, +0x40 = up;
```
So the rider/board tilt with the slope (ground normal of the floor under the rider, low-passed with 0.9/0.1 per frame –
frame-rate dependent). `0x44bf10`: position `+0xc` = `+0x1f4`, centre `+0x60 = pos + (0, P+0 (43), 0)`, re-cell
`0x4077f0`; then if `board` and not frozen: `board.centre = centre`, `board.pos(+0xc) = inst.pos`,
`0x4077f0(board, &centre)`, `board.rot(+0x28..+0x48) = inst.rot` (9 floats). Scale is not copied.

### 4.8 Crouch in state 1 (`0x465b10`)
Skipped in state 2, while `+0x5b4 != 0`, and in state 1 while `startT > 0`. Key = action **8** in state 1 (5 otherwise).
```c
switch (p->crouch) {                                         /* table 0x465de4 */
case 0: if (key && onGround) { crouch = 1; A,B->Request(0x68); crouchT = AnimLen(0x68,0); } break;
case 1: if ((crouchT -= dt) <= 0) crouch = 2; break;
case 2: A,B->Request(0x69); crouchT = AnimLen(0x69,0);
        if (!key) { Ray(pos + (0, P+0x10, 0), pos + (0, P+0x0c - P+0x10, 0), -1);   /* see note */
                    if (no hit) { A,B->Request(0x6a); crouchT = AnimLen(0x6a,0); crouch = 3; } }
        break;
case 3: if ((crouchT -= dt) <= 0) crouch = 0; break;
}
if (crouch) LockMove(p, dt, 0);                              /* 0x44cce0 – no effect in state 1 */
```
Note: the stand-up ray goes from y + P+0x10 to y + (P+0x0c − P+0x10) (`0x465cd8..0x465d05`): for the race columns
from y+81 down to y+79 (a 2-unit ray, practically never blocked); for Woody y+61 → y+132. PERSO_MOVE §3.4 describes it
as y+61 … y+193; by my reading of the FPU stack that is not what the code does.
Effects of crouching while riding: turn rate 1.2 instead of 1.8, wall radius 34.5, body height 81 (so the upper crash ray
is at 71), lean state reset to 0; speed unchanged.

### 4.9 Race kill `0x44c4c0(kind)` and respawn
```c
if (p->deathKind(+0x26c) && !(kind == 7 && p->deathKind != 7)) return;   /* only water overrides a running death */
p->deathDur(+0x288) = 3.5f;  cam->0x41fa80(200.0f);  SndSrc_Stop(&p->snd4a4, 0, 60, sfx);
switch (kind) {                                              /* table 0x44c700 */
case 1: 0x478980(p, 0, 2.5, 180, 50, 0); p->+0x240 = AnimLen(0x75,0); Jumper_ForceFall(J, 0); break;
case 2: if (cheat[0x5d7b8a] || p->+0x270 > 0) return; p->deathDur = 1.5f;
        0x477e40(&(pos + (0, P+0x0c, 0))); p->+0x240 = AnimLen(0x72,0); break;
case 3: case 6: p->deathDur = 2.5f; break;
case 8: p->deathDur = 1.5f; break;                           /* NO cheat / invulnerability test for 3/8 here */
case 7: p->deathDur = 4.0f; Splash(inst.pos + (0,110,0), |disp|/dt, 50.0); 0x459030(camFollow, &camPos);
        p->+0x5f0 = p->+0x5b4 = 0; p->+0x5cd = 0; A->Reset(); Jumper_Reset(J); colliders reset; break;
default /*4,5,9..*/: break;
}
p->health = 0; p->deathT(+0x274) = 0; p->deathKind = kind; p->raceDeath(+0x4d8) = 1; p->+0x550 = 0; SetState(p, 2);
```
Then the normal game sequence `0x4459c0` (PERSO_FRAME §4.1): fade-out at `T ≥ deathDur − 1`, life lost, `0x445930` →
`0x44a810` → `pos = +0x318` (last checkpoint or start), Reset → SurfEnter, state 1, sub-state 0 (camera cut + direction
`+0x324` if a checkpoint was taken, else the first polyline segment). Checkpoints are the normal 1030 SaveAuto
(`0x44aa10`: position, direction from the checkpoint's typecode-0 marker, `+0x330 = 1`, `+0x4e0 = +0x264`).

## 5. Camera in state 1

* Sub-state 0 (once per SurfEnter, when the polyline is known): `0x41f9f0(2)` next transition is a hard cut;
  `cam+0x368 = 0`; `0x41f410(0, 0)` = SetMode(0, 0) → mode 1 follow camera ("Center", CAMERA.md §3), start behind;
  `0x41f660(1.5)` zoom `cam+0x678 = 1.5` (default 1.2 ⇒ wider view, `hfov = 2·atan(1.5)` ≈ 112.6°);
  `0x41fa60(100)` height above the target `C+0x7d8 = 100` (default 180); `0x41fa80(4.0)` distance
  `C+0x280 = C+0x7e0 = C+0x7e4 = 4.0` (default 400).
* The follow camera forces behind-mode in Perso states 1/4/8 (`0x4591a5`, CAMERA.md §3): it moves towards
  `D = T − F̂·dist` with `move = (D − P)·3·dt`. With dist = **4** and the rider at 1250 units/s the camera trails by the
  lag of that exponential chase, about `1250/3 ≈ 417` units at steady speed (my interpretation of the tiny distance –
  unverified). Vertical: `P.y → pos.y + 120 + 100` at `6·dt`.
* Crash: distance 400 (`0x456bce`); race death: distance 200 (`0x44c508`); pause-menu restart: 4.0 (`0x40584d`).
  Nothing in race code restores zoom or height; message 700/660 or a level change would.

## 6. Parameters (columns 3/4 of `0x4b5f14`, identical) used while riding

| P+ | value | use in state 1 |
|---|---|---|
| 0x00 | 43 | centre height / floor probe / max step-up (MoveCollide, `0x44bf10`) |
| 0x04 | 69 | wall radius (34.5 crouched); crash ray length 69 + 40 = 109 |
| 0x08 | 143 in the table, overwritten every frame: 160 standing / 81 crouched | body height (MoveCollide sweep); upper crash ray at P+0x08 − 10 |
| 0x0c / 0x10 | 160 / 81 | standing / crouched height; kill kind 2 effect height |
| 0x1c | **1250** | ride speed (units/s) |
| 0x64 / 0x68 / 0x6c / 0x70 | 400 / 1250 / 1250 / 2000 | Jumper: H = 400, T_jump = ½·0.75·1250/1250 = **0.375 s** (launch ≈ 2133 u/s, rise gravity ≈ 5689 u/s²), T_fall = ½·1.25·1250/1250 = **0.625 s** (≈ 2048 u/s²), terminal 2000 u/s |
| 0x7c | 1500 | fall-damage height |
| 0x84 | 100 | jump re-arm height |
| 0xd8 / 0xdc | 0.5 / 0.5 | rumble arguments for crashes |

Other constants: turn 1.8 / 1.2 rad/s (`0x4ab290` / `0x4aa39c`); veto threshold 0.5 (`0x4a9014`); stuck ratio 0.7
(`0x4aa1d8`), > 5 frames; crash rays at +40 (`0x4ab294`) and P+0x08 − 10 (`0x4a9750`); crash timeout 2.0 s
(`0x4a9870`); boost overshoot 1.1 (`0x4aa168`); dir-fallback threshold 0.01 (`0x4a94f8`); up filter 0.9 / 0.1
(`0x4a94b8` / `0x4a9008`); star effect +150 (`0x4a9754`).

## 7. All uses of `+0x498` (board anim controller) and `+0x4b4` (board)

| address | function | what |
|---|---|---|
| `0x44ade3` | `0x44ad90` anim-controller init | `B = NULL` |
| `0x44a5f8` | `0x44a590` Perso destructor | `B->vtbl[1](1)` delete (the FX emitter is not freed here) |
| `0x44a688` | `0x44a650` SetPosition (teleport + ground snap, not in state 5) | `A->Reset()`, `B->Reset()` |
| `0x44abab` | `0x44ab20` Reset | `A->Reset()`, `B->Reset()` |
| `0x455e2f` | `0x455dc0` SetRaceInfo | `B = new AnimCtl(board)` |
| `0x456129`, `0x456138` | `0x4560f0` race restart | `B->Reset()`, `B->Request(0)` |
| `0x4561ed`, `0x4561fc` | `0x456150` SurfEnter | `B->Reset()`, `B->Request(0x5d)` |
| `0x456beb` | `0x456ba0` Race_Crash | `B->Request(0x70)` (first crash frame) |
| `0x463ef1`, `0x463f05` | `0x463e60` AnimState | `B->Tick(dt)` (all states except 7) |
| `0x4643b6` | `0x4642f0` JumpAnims | `B->Request(same anim as A)` if `B` (any state; outside state 1 the sets are 4..0xa or 0x47..0x4c) |
| `0x464a3c`, `0x464a5b`, `0x464a7a`, `0x464a99`, `0x464ab8`, `0x464b29` | `0x464a00` race death anims | `B->Request(0x71 / 0x74 / 0x73 / 0x72 / 0x76 / 0x77)` |
| `0x464c32` | `0x464c20` Race_Anims | no lean/jump anims if `B == NULL` |
| `0x464d1a` | `0x464c20` Race_Anims | `B->Request(lean anim)` |
| `0x465bdf`, `0x465c6f`, `0x465d4c` | `0x465b10` crouch | `B->Request(0x68 / 0x69 / 0x6a)` in state 1 |
| `0x44a56c` | `0x44a3d0` PostLoad | `board = NULL` (also `poly`, `fx`, `+0x4e0 = 0`) |
| `0x455de4`, `0x455e13`, `0x455e58` | `0x455dc0` SetRaceInfo | store board, flags `|= 0x20`, controller for board, count its type-9 markers |
| `0x44bf90`, `0x44bfba`, `0x44bfd4`, `0x44bfe7` | `0x44bf10` | copy centre, position, cell (`0x4077f0`), rotation to the board (skipped when frozen) |

`0x4b0` (polyline) is read only in SetRaceInfo (region list) and `0x456210`; `0x4b8` (FX) only in SetRaceInfo,
SurfEnter and `0x456210` (plus the global emitter list in `0x46d040`).

## 8. Porting notes

Can be skipped or stubbed without changing gameplay: the FX emitter (§2.2, particle system `[0x5e823c]`), the
render region list (§2.1, ported in round 30), rumble `0x44d1b0`, star effect `0x4750e0`, `0x478980` (landing effect / speech bubble),
`0x476140` dust, splash `0x478660`, sound 60 (2D engine loop) and sound 59 (boost), the race HUD flags.
Needed: state 1 ride + crash + race kill, the board transform copy, both anim controllers, the Jumper and MoveCollide
(already ported for Woody), follow camera behind-mode with the race parameters, messages 1120/1121, checkpoints 1030.

Surprises / pitfalls:
1. No acceleration: the rider is at 1250 u/s from the first riding frame (the start anim only blocks steering).
2. Zero-displacement frames count as "stuck" (NaN compare, §4.5).
3. During the crash anim `disp = 0`: a rider that crashes in mid-air hangs in the air until `AnimLen(0x70,0)+AnimLen(0x70,1)`,
   then falls; Kill(8) comes on landing or at the latest 2.0 s after the crash.
4. Board and rider always get the same logical anim, except kind-1 death (0x75 / 0x76) and restart (board 0).
5. The steering veto compares against an unnormalized segment vector (threshold 0.5 is effectively 0).
6. Camera follow distance 4.0 while riding (chase lag makes the real distance).

## 9. Open questions

* Numeric anim durations (0x5d, 0x70, lean anims) from K1R models 16/17 – need the model files.
* ~~Number and placement of the type-9 markers~~: 2 on the K boards, 1 on the S boards (port log of 1120).
* ~~Exact sprite/particle parameters of `0x475440`~~: §2.2.
* ~~Whether App+0x28 is the same object as `[0x509adc]`~~: yes, `0x42a451` (§2.1).
* Rotation direction of action 0/1 on screen – derived from the math and CAMERA.md's handedness, verify in-game.
* Who ticks the board's own skeleton/animation (`vtbl[2]`); presumably the normal instance update, since the board is
  an ordinary level instance with flag 0x20.

## 10. Port (`src/player.c`, issue #30)

* 1120 (`main_engine.c`) stores `Player.board` + `race_path`; the board becomes a non-scripted instance whose placement
  (the rider's position and tilted rotation) is copied every frame (`player_sync_board`, after `player_update`). Its
  animation is its own controller `B` (`board_request` / `board_tick` in player.c, the same chain walker `ctl_request` as
  the rider's): created by 1120 with nothing requested, Reset + 0x5d by SurfEnter (so not at the level start, §0), the
  rider's requests in the jump set, the lean and crouch anims and the crash, 0x76 instead of 0x75 in the kind-1 death,
  Reset + 0 after the pause-menu restart (§3.2), Reset by the teleport. The player's own collision ignores its board
  (`skip_inst`): standing on the board's hull lifted the board, which lifted him, every frame.
* State 1 = `race_char && !dead_kind`: `race_crouch` (0x465b10), `race_ride` (0x456210 incl. crash 0x456ba0),
  the normal collision sweep with body height 160 / 81 and radius 69 / 34.5, then `race_check_crash` (0x4567f0),
  `race_anims` (0x464c20). The logical-animation chain is ticked every frame (as the controller Tick does), otherwise the
  start anim 0x5d (= .ins 42, the rider running up to the board) never hands over to anim 0 and he keeps running behind it.
* Jumper parameters come from the column: 400 / 1250 / 1250 for the race, Woody's 380 / 650 / 600 otherwise.
* Race kill (0x44c4c0) in `player_kill`; Reset calls `race_enter` (SurfEnter, also once more at the level start after the
  init messages, `player_race_start`, as the Game ctor does). SurfEnter puts every type-37 bonus back
  (`game_race_bonus_reset` = 0x44f8a0) and the race bonus count back to the checkpoint's (`race_bonus_ckpt` = +0x4e0,
  written by 1030, cleared by the restart). Camera: sub-state 0 sets zoom 1.5, height 100, distance 4 and asks the app for
  a hard cut to the follow camera; state 1 forces behind mode. The race kind-1 death asks for the fixed camera above him
  (0x41fb50, now a smooth 100 u/s travel for every kind-1 / kind-7 death camera) from its first frame.
* Round 29: the up filter (§4.7, `race_upf` from the floor normal under him also in the air, 0.9/0.1 per frame normalised
  to 60 fps, turned into the instance quaternion in `player_apply_transform`); the ride loop SoundFx 60 through the sound
  source +0x4a4 (`race_snd_update` / `race_snd_stop`: starts on the second ride frame, stops on crash, race kill and
  SurfEnter); the hit star 0x4750e0 at `pos + (0,150,0)` on both crash kinds; the hard landing in the air set (§4.6, Jumper 6
  with a hard fall: 0x6f + the curse bubble) and the dust of ground type 2; the spray emitter of §2.2 (`Player.bfx`, drawn by
  `board_fx_draw` in main_engine.c before the effect pool, puffs = `FX_BOARD_PUFF`; the sprite primitive got mode 0x13).
* Deviations: a frame with zero displacement does not count as stuck (the NaN compare of §4.5); the crash rays test world
  polygons and press nodes; rumble is left out (the port has no force feedback); the region list of §2.1 filters the world
  faces and the sky since round 30, not yet the instances; see TODO.md for what is left out.
* Test: `woody.exe <Data> K1R --shot out/x.ppm 4` (jets + smoke trail, the rider leans with the ramp), steering with
  `WOODY_KEYS="2.5:LEFT:1.0"` + `WOODY_ANIMLOG=1` (rider `lanim` and `board lanim` lines), `WOODY_BOARDLOG=1`. Crash, star,
  Kill(8), respawn with "RACE n race bonuses back": `K2R --shot x.ppm 11` (crash ~5.5 s; since the press-node collision the
  start hurdle no longer stops him). Kind-1 death: `WOODY_KILLAT="2 1" K1R`. Hard landing: `WOODY_POSAT="2.5 -78440 3500 3120"
  WOODY_BUBLOG=1 K1R`. Pause restart: `WOODY_KEYS="3.5:ESC 4.2:DOWN 4.8:RET" K1R` (board lanim -> 0, 0.1 s later -> 93).
