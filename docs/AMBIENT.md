# AMBIENT.md — instance class 90, the "environment instance": motes (mode 1) and rain (mode 2)

Static analysis of `game/Woody.exe` (image base 0x400000; all addresses are VAs). Mode 0 (the butterflies) is described
in TITLE.md §3.1 / OBJECTS.md §2.4 / INSTANCE.md §7 and stays as it is; this document covers the class as a whole and
modes 1 and 2. Port: `src/ambient.c` (modes 1/2), `env_update` / `env_draw` in `src/main_engine.c` (mode 0).

Notation as in PARTICLES.md: `rnd` = `0x43ff40` = `rand()/32767` ∈ [0, 1]; `dt` = `[[0x509adc]+0x38]`; `frame` =
`[[0x509adc]]` (the frame counter); `ftol` = `0x499580` (truncates).

## 1. The class

* **Ctor**: case `0x403d2c` of the SetTypeInstance factory (1200 [inst, 90]): `new(0x150)`, base ctor `0x42e1a0`, then
  `[esi] = 0x4a90ac`. No field of its own is initialised there: the messages of §2 do that.
* **Vtable `0x4a90ac`** (RTTI pointer at `0x4a90a8`): `vt[3]` (+0x0c) = **think `0x472560`**, `vt[2]` (+0x08) = the
  ordinary instance draw `0x42e2b0` (the model is a bare volume node, so nothing shows), `vt[0x70/4]` = `0x472b30`
  (clears `+0x120` and `+0x121`), `vt[0x74/4]` = `0x472b40` (rain set-up), the rest are base-class entries.
* **Dtor `0x472e30`**: in mode 2 it frees the start-point grid `+0x124` and the float array `+0x128`, then the base dtor
  `0x42ff30`.
* The think runs for the instances of the frame's list world+0x64 (`0x42a980` → `0x42a840`, INSTANCE.md §4.1), like every
  instance think (`0x42b400`); a hidden instance (`0x407850` takes it out of the world) does not think, nor one whose sector /
  floor group is not in the camera's `.vis` entry. The volume is stationary (clock speed 0), so its bounding sphere is cached
  (`+0x88 = 1`) and it also drops out while that sphere is outside the view frustum (`0x437b00`; in a race also beyond 11000).

| offset | type | meaning | written by |
|---|---|---|---|
| +0xfc | int | mode: 0 butterflies, 1 motes, 2 rain | 1501 |
| +0x100 | int | wanted particle count (modes 0 and 1) | 1502, 1504 |
| +0x104 | u32 | frame counter of the last think (`0x472576`) | think |
| +0x108 | int | particles alive / spawned; a mote that dies decrements it (`0x47ddf2`) | 1502, 1504 (= 0), think |
| +0x10c | float | world y of the local minY (`0x472911`); only the butterflies read it (`0x47d6dc`, their floor) | think modes 0/1 |
| +0x110..0x118 | float | mote colour r, g, b = arg / 255 (`0x4abc5c` = 1/255) | 1502 |
| +0x11c | float | mote base lifetime in s = arg × 0.01 (`0x4aa0ac`) | 1502 |
| +0x120 | byte | **off**: modes 0/1 spawn nothing while set (`0x4727d3`) | 1511, cleared by 1502 (`0x472b30`) |
| +0x121 | byte | "first fill": set when the think finds `+0x108 == 0`, cleared at its end; new motes then start at a random age | think |
| +0x124 | ptr | rain: 400 start points (12 B) | `0x472b40` |
| +0x128 | ptr | rain: 400 floats `rnd·45 + 90` (`0x4ab27c`, `0x4abc68`) — **never read** | `0x472b40` |
| +0x12c..0x134 | float3 | rain direction in node-local space = (0.05, 0.05, −0.8) | `0x472b40` |
| +0x138 | int | rain: which of the 16 points of each cell is used next (0..15) | think mode 2 |
| +0x13c | float | rain: time accumulator | think mode 2 |
| +0x140 | float | rain: spawn interval (s) from the force | `0x472b40` |
| +0x148 | float | rain: local height of the box, `maxZ − minZ` | `0x472b40` |
| +0x14c | int | rain force 1..4 | 1503 |

### 1.1 The volume

Every mode works on the instance's **first volume node**: `model+0x48` count, `model+0x4c` list (1-based), the node record
`model+0x68 + 0x90·(i−1)`, its world matrix `[0x509adc]+0xa0 + 0x30·(inst+0x5c + i − 1)` (3×4: rows +0x00/+0x0c/+0x18
are the images of the local x/y/z axes, +0x24 the origin; a point is `p.x·R0 + p.y·R1 + p.z·R2 + T`). No volume node =
assert `"une instance d'environnement n'a pas de volume"` (`0x4b7aa4`) and garbage after it.

The box is the **node-local** aabb of the node's own points (vertex list `+0x1c`, count `+0x14`, stride 0x28; loops
`0x47285b..0x47290f` and `0x472bde..0x472c8e`). The code treats local **z as up** (the 3ds Max axis): rain starts on the
plane z = maxZ − minZ and falls along local −z, motes start on z = minZ. Every class-90 volume model checked (W1A model 29,
WWS 18, W3D 28) is the same 400×400×400 cube, local x, y ∈ [−200, 200], z ∈ [0, 400], whose node matrix turns local z
into world +y and carries the instance's scale (W1A 490: ×3.87; WWS 140: ×4.76; W3D 586: ×12; W3D 823..829: ×0.32).

## 2. Messages (subsystem handler `0x46cca0`)

| id | args | handler | effect |
|---|---|---|---|
| 1501 | [inst, mode] | `0x46cd07` | `+0xfc = mode` (0 butterflies, 1 motes, 2 rain) |
| 1502 | [inst, r, g, b, life×100, count] | `0x46cd2e` | `+0x110..0x118 = rgb/255`, `+0x11c = life×0.01` s, `+0x108 = 0`, `+0x100 = count`, then `vt[0x70/4]` = `0x472b30`: `+0x120 = +0x121 = 0` (switches the motes **on**) |
| 1503 | [inst, force] | `0x46cda0` | `+0x14c = force`, then `vt[0x74/4]` = `0x472b40`: builds the rain grid (§4.1) |
| 1504 | [inst, count] | `0x46cdcc` | `+0x100 = count`, `+0x108 = 0` (the butterfly count) |
| 1511 | [inst, b] | `0x46cf44` | `byte +0x120 = (b != 0)`: 1 = motes **off**, 0 = on (modes 0 and 1; mode 2 ignores it) |

The fifth argument of 1502 is therefore a **lifetime** (hundredths of a second), not a size: the sprite size is fixed.

## 3. Mode 1 — motes

### 3.1 Think `0x4727d3` (shared with mode 0)

```c
if (off /*+0x120*/) return;                                 /* +0x104 was already stamped at 0x472576 */
if (live /*+0x108*/ == 0) fill /*+0x121*/ = 1;
while (live < count) {
    box = local aabb of the volume node (§1.1);   +0x10c = minY·M[4] + M[10];
    x = rnd·(maxX − minX) + minX;
    z = (mode == 1) ? minZ : rnd·(maxY − minY) + minZ;      /* 0x472936: mode 0 mixes the y extent into z */
    y = rnd·(maxZ − minZ) + minY;                           /* 0x47296a: the z EXTENT added to minY */
    P0 = first polygon of the node (+0x10), plane (nx +8, ny +0xc, nz +0x10, d +0x14);
    if (P0·p + d > 0) p −= 2(P0·p + d)·n;                  /* 0x472985: mirrored into the half-space of polygon 0 only;
                                                               the loop never steps past polygon 0 */
    w = M·p + T;
    if (mode == 0) Butterfly(this, w);                      /* 0x47e050 */
    else {
        if (!normal_done) { n = the node polygon with the largest local nz (0x472a47, first of equals);
                            N = M3x3·n (no normalisation); normal_done = 1; }
        Mote(this, w, N);                                   /* 0x47e160 */
    }
    live++;                                                 /* counted even when the pool is full */
}
fill = 0;                                                   /* 0x472b13 */
```

The y/z mix-up is harmless for the shipped cube (y extent = z extent = 400) and kept in the port.

### 3.2 Creation `0x47e160(inst, &w, &N)` — pool record

`a = fill ? rnd : 0`; `+4` life `= rnd·2 + inst+0x11c`; `+0` age `= life·a` (the first fill starts spread over the
whole lifetime, so the stream is full at once); `+8` inst; `+0x18` N; `+0x24` speed `= rnd·20 + 40` (`0x4a9994`,
`0x4ab294`); `+0x0c` pos `= w + N·age·speed`; callback `0x47dca0`.

### 3.3 Callback `0x47dca0` (every frame, draws too)

```c
if (inst->stamp /*+0x104*/ != frame) { inst->live--; life = -1; return; }   /* owner did not think: gone at once */
age += dt; u = age / life;
if (age >= life)                    { inst->live--; life = -1; return; }   /* its place is refilled next think */
pos += dt·N·speed;
S.mode = 0x12; S.size = 25; S.image = 0x1001d;              /* bank 0 image 29: a four-point star sparkle */
S.rgb = inst+0x110..0x118;
S.alpha = u < 0.7 ? 1 : 1 − (u − 0.7)·3.33;                 /* 0x4aa1d8, 0x4abd30 */
S.rot = fistp(age·250);                                     /* 0x4ab144: 1/512 turn -> ~0.49 turn/s */
Sprite(S, 7);                                               /* camera facing, own colour, rotation, additive */
```

So a mode-1 instance keeps `count` sparkles alive that rise out of the bottom of its box along the box's up axis (world
up for every shipped instance) at `(40..60)·scale` u/s for `life0..life0+2` s, spinning slowly and fading over the last
30 % of their life. When the camera leaves (the sector is no longer drawn) they all vanish in one frame; when it comes
back the first think refills the stream at random ages.

## 4. Mode 2 — rain ("la force du vent")

### 4.1 Set-up `0x472b40` (message 1503)

Allocates `+0x124` (400 × 12 B, `0x499e53` array ctor) and fills it cell by cell: for row r = 0..4, column c = 0..4,
16 points each: `x = (rnd + c)·(maxX − minX)·0.2 + minX`, `y = (rnd + r)·(maxY − minY)·0.2 + minY`, `z = maxZ − minZ`
(`0x4aab94` = 0.2) — point index `80r + 16c + k`. Then `+0x128` = 400 floats `rnd·45 + 90` (unused), direction
(0.05, 0.05, −0.8), `+0x148 = maxZ − minZ`, `+0x138 = 0`, `+0x13c = 0`, and the interval `+0x140` by force (jump
table `0x472e18`):

| force | 1 | 2 | 3 | 4 | other |
|---|---|---|---|---|---|
| interval | 0.2 s | 0.15 s | 0.1 s | 0.05 s | assert `"La force du vent doit etre comprise entre 1 et 4"` (`0x4b7ad4`), unchanged |

### 4.2 Think `0x472596`

```c
acc += dt;
while (acc > interval) {
    acc -= interval;
    D = normalize(M3x3 · (0.05, 0.05, −0.8));               /* 0x47260d..0x4726bc */
    L = |R2| · height;                                       /* world length of the local height (0x47273b) */
    for (cell = 0; cell < 25; cell++)
        Drop(this, M·grid[rot + 16·cell] + T, D, L);         /* 0x47e230 */
    rot = (rot + ftol(rnd·15 + 1)) & 15;                     /* 0x4a9864 = 15 */
}
```

25 drops per interval, one per cell, i.e. **125 / 167 / 250 / 500 drops a second** for force 1..4. The off flag +0x120
is not tested.

### 4.3 The drop `0x47e230` → callback `0x47de10`

Record: `+0` age 0, `+4` life 1.0 (never tested), `+0x0c` pos, `+0x18` D, `+0x24` fallen = 0, `+0x28` L,
`+0x2c` speed **4000** (`0x457a0000`).

```c
age += dt;
s = dt·speed·D.y;  fallen' = fallen − s;                    /* D.y < 0: counts the height fallen */
if (fallen' < L) {
    pos += dt·speed·D;
    Line: p0 = pos − D·250 (rgba 0.4, 0.4, 0.4, 0), p1 = pos (rgba 0.4, 0.4, 0.4, 0.5),
          half width 4, image 0x10039 (bank 0 image 57), flags 0xe00;   /* 0x471a10: textured, own colours, own width; additive */
} else {                                                    /* 0x47df30 */
    pos += dt·(L − fallen)/|s| · speed·D;                   /* exactly onto the bottom of the box */
    Ripple(pos);  life = −1;                                /* 0x47e310 */
}
```

### 4.4 The ripple `0x47e310` → callback `0x47df80`

Life **0.2 s**; `u = age/0.2`; sprite mode 0x12, **flags 2** (own colour, flat in the plane with normal `+0x230` =
(0, 1, 0), additive), image 0x10003 (bank 0 image 3), colour (1, 1, 1), size `u·30 + 5`, alpha `0.5 − 0.5u`.

So rain is a slanted curtain of 250-unit grey streaks at 4000 u/s from the top of the box to its bottom, where each drop
leaves a small expanding ring. It does not collide with anything: the "ground" is the box's bottom face, so the level
designers sit the boxes on the floor. The rain keeps falling (drops already spawned finish their fall) when the camera
leaves; only new drops need the think.

## 5. Drawing

All three particles live in the shared effect pool (`[0x5e823c]+0xdb8`, 2000 × 0x50 B, PARTICLES.md §0) and draw from
their own callbacks through the sprite primitive `0x470f10` / the line primitive `0x471a10`. There is no distance or
frustum test: the only gating is the think (the instance list, §1) for creation and, for motes, the frame stamp.

## 6. Where it is used (level scripts, init code of the instance's own object)

| level | slots | mode | parameters |
|---|---|---|---|
| House | 60, 61, 62 | 0 | 1504 3 / 2 / 3 (title-screen butterflies) |
| KWS, SWS | 49, 121, 206 | 0 | 1504 12 / 7 / 4 |
| WWS | 50, 122, 210 | 0 | 1504 12 / 7 / 4 |
| W2D | 290, 354 | 0 | 1504 12 / 18 |
| W1A, K1A, S1A | 490..492 / 489..491 / 316..318 | 1 | 1502 (255,255,255, 700, 400), (…, 700, 150), (…, 500, 150): white sparkles, 7 / 7 / 5 s |
| K1R | 120..123, 281, 282 | 1 | (200,200,255,600,150), (150,150,200,600,75), (180,255,180,600,75), (200,200,255,600,200), (180,255,180,600,75) ×2 |
| S1R | 113..116, 144 | 1 | same set as K1R |
| W3D | 823..829 | 1 | (255,255,0, 1500, 50) + `1511 1`: yellow, 15 s, off until the Boss16 phase (object 770 sends `1511 0`, object 801 `1511 1`; BOSS15_16.md §9.2). The boxes sit under the holes of the arena floor, the sparkles rise through them |
| KWS, SWS | 139 | 2 | force 3 |
| WWS | 140 | 2 | force 3 (the dark pumpkin area) |
| K3A / S3A | 569, 570 / 572, 573 | 2 | force 4 |
| W2D | 376, 714 | 2 | force 2, 4 |
| W3A | 606, 607 | 2 | force 4 |
| W3B | 517, 518, 568, 640, 753 | 2 | force 2, 3, 2, 2, 3 |
| W3D | 586, 766 | 2 | force 1 |

(1501 always precedes 1502/1503 in the same init block, right after 1200 [inst, 90].)

## 7. Port notes (`src/ambient.c`)

* `ambient_msg` takes 1501..1504/1511 for any instance (the handler does not type-check); the existing mode-0 code in
  main_engine.c keeps its own record. `ambient_update(dt)` runs the think of every type-90 instance that is visible and in the
  frame's instance list (`game_enemy_thinks` = `Instance.listed`, `rnd_instance_list`), then the particles; `ambient_draw(eye)` draws them between
  `hud_world_sprites_begin/end`. `ambient_reset()` in `level_free`.
* The port keeps its own 2000-record pool for these particles instead of sharing the original's global one.
* Box, polygon planes (`0x4280c2`, as `render_gl.c poly_plane`) and matrices are taken node-local exactly as in §1.1;
  N is not normalised (the node scale scales the mote speed), D is.
* Ripple image 3 is drawn with hud slot 0x3a (image 58, byte-identical, as the water splash ripple does); image 29 was added to
  `k_fx_img` in hud.c.
* Port guard: a drop older than 5 s is dropped (only a box whose local −z does not point down would need it).
* Test hooks: `WOODY_AMBLOG=1` (box, scale, direction of each instance on its first think), `WOODY_AMBON=1` (ignore the
  off flag and hiding: shows the W3D arena sparkles without playing to the Boss16 phase).

## 8. Open questions

* The frame stamp `+0x104` is read as "the owner thought this frame" on the assumption that `[[0x509adc]]` is the frame
  counter and that instance thinks run before the effect pool driver `0x470c70` in a frame (otherwise every mote would
  die at once). Not traced live.
* Whether the node matrices of the original include the instance scale (they must for the volume tests; the mote speed
  then scales with it: W1A motes rise at 155..232 u/s, W3D arena ones at 13..19 u/s).
* `+0x100/+0x108/+0x120` are not initialised by the ctor (operator new memory); every shipped script sends 1502 or 1504
  before the first think, so this never matters. 1502 resets `+0x108` to 0 even while motes are alive; their later deaths
  then push it below 0 and the stream over-fills — never triggered by the scripts (1502 is only sent at init).
* The `+0x128` angle array (90..135) is dead data — maybe a planned per-drop slant.
