# PARTICLES.md — footsteps, the smoke ring, the peck impact, explosion debris, the skeleton flash

Static analysis of `game/Woody.exe` (image base 0x400000; all addresses are VAs). These are the effect functions that
the earlier docs only knew by their call sites (FOOTSTEPS.md, OBJECTS.md §1.6, ROCKET.md §5.3, BOMB.md §4.3,
PERSO_DEATH.md §4.2). All of them are records in the one effect pool and draw through the one sprite primitive, so
§0 and §1 are shared; §2..§6 are one function family each. §7 maps everything onto the port.

Constants: `rnd` = `0x43ff40` = `rand()/32767` ⇒ [0, 1] inclusive; `ftol` = `0x499580` (truncates);
`costab[i]` = `[0x5e823c][i & 511]` = `cos(2π·i/512)` (filled by `0x40248f`), so `-costab[i + 128]` = `sin(2π·i/512)`
(`[0x4b798c]` = 128); `dt` = `[[0x509adc]+0x38]`.

## 0. The effect pool (recap of BONUS.md §2.4 / PROJECTILES.md §5)

`[0x5e823c]+0xdb8`: 2000 records of 0x50 B, bump allocator (count at `+0x27100`), no free list; a full pool silently
drops the new record. Record: `+0` age, `+4` lifetime (−1 = free it), `+0x4c` callback, the rest is the callback's own.
The driver `0x470c70` calls `+0x4c` for every record with `+4 > 0` and swaps a dead one with the last; it re-reads the
bound, so a record created this frame is also run (and drawn) this frame. Every callback starts with
`age += dt; u = age / life` and frees itself with `+4 = −1` once `u ≥ 1` (exceptions noted). A freed record keeps its
slot until the driver visits it again, the next frame (`0x470c8f`: the life test comes before the call), so dead
records count against the 2000 for one frame; the creators do not clear the memory they take (a field a creator does
not write keeps the previous occupant's value, AMBIENT.md §3.5). Class 90 (butterflies, motes, rain) uses this pool
too (AMBIENT.md §5).

**Start age.** Some creators write `+0` ≠ 0 (§3 `t0`, §2.2 `rnd·0.2`): the record then starts part of the way through
its life.

## 1. The sprite primitive `0x470f10(S, flags)` — what these effects rely on

`S = [0x5e823c]+0xb00` is ONE shared sprite object; every callback fills it and calls `0x470f10`. Fields: `+0x208` pos,
`+0x214..0x220` rgba, `+0x224` rotation (1/512 turn), `+0x228` image `0x1000N` = bank 0 image N, `+0x22c` UV set,
`+0x230` plane normal, `+0x23c` R / `+0x248` F / `+0x254` N (explicit plane), `+0x260` mode, `+0x264` size.

| flag | meaning | address |
|---|---|---|
| 1 | camera facing: corners added in view space | `0x4714ea` |
| 0x20 (without 1) | quad in the plane spanned by R (`+0x23c`) and F (`+0x248`), in world space | `0x4715d5` |
| neither | quad in the plane with normal `+0x230`, axes from `0x471ee0` (below) | `0x4717d7` |
| 2 | own colour/alpha `+0x214..0x220`; otherwise `0x4b7a84` = (0.5, 0.5, 0.5, 1.0) | `0x4710bd` |
| 4 | rotation `+0x224`; otherwise 0 | `0x470f39` |
| 8 | submit flag 8: alpha blended, texture × 2c (MODULATE2X), alpha a; **without** it submit flag 4: additive ONE/ONE, texture × c × a | `0x4719b7` |
| 0x40 | UV set `+0x22c` via `0x470d80` (else set 0) | `0x4714b4` |
| 0x80 | submit flag \| 1 | `0x4719c2` |

* **Colour path**: see §1.1.
* **Corners** (`0x470f39..0x4710b3`): corner k sits at `size·(cos t, sin t)` in the quad's own axes with
  `t = rot + base`, `rot − base + 256`, `rot + base + 256`, `rot − base + 512` (1/512 turn). `base` comes from the table
  `[0x5e823c]+0x800[mode]`, filled at `0x4024bb`: `table[8r + c] = ftol(atan(2^(r−c)) · 81.4873)` (`0x4a9028` = 512/2π)
  for r, c = 1..7. Mode 0x12 (r = c = 2) is the square (base 64 = 45°), mode **0x1a** (r = 3, c = 2) is a 1:2 upright
  quad (base 90, `atan 2`). So `size` is the half DIAGONAL (the known BONUS.md §2.4 fact) for every mode.
* **UV sets** `0x470d80(mode, which)` (jump table `0x470ef4`), for the corners in the order above (u, v):
  0 = (1,0)(0,0)(0,1)(1,1) · 1 = v flipped · 2 = u flipped · 3 = both · 4 = (1,1)(1,0)(0,0)(0,1) · 5 = unchanged · 6 = (0,0)(0,1)(1,1)(1,0).
  Without flag 0x40 a sprite whose last set was ≠ 0 is reset to 0 (`0x4714cb..0x4714e0`).
* **The second argument of `0x470d80` picks the vertex set** (`0x470d80..0x470dca`; vertices are 0x40 B, `+0xc` xyz,
  `+0x24` rgba, `+0x34/+0x38` uv): **0** = the sprite quad `S+0x00..0xc0` (corner k0 = `+0x00`, k1 = `+0x40`, k2 = `+0x80`,
  k3 = `+0xc0`); **1** = the quad of the line primitive `0x471a10`, `S+0x100..0x1c0`, whose vertex v0 = `+0x100` gets
  the entry of k1, v1 = `+0x140` of k2, v2 = `+0x180` of k3, v3 = `+0x1c0` of k0; any other value is taken as a pointer
  to a single vertex that receives all four pairs (`0x470dbe`). Every mode but 5 also stores itself in `S+0x200` (the
  sprite's "last set", `0x470e00..0x470ee1`) — whichever set it wrote. The ctor `0x470d60` sets both sets to mode 0.
* **The line primitive `0x471a10(flags)`** (`S+0x278` p0, `+0x284` p1, `+0x290`/`+0x2a0` rgba per end, `+0x2b0` half width,
  `+0x2b4` image): both ends go to view space (rows of `[0x509adc]+8`, `0x471a13..0x471afa`); the quad is offset across
  the projected direction `(dx, dy)`: v0 = p0 + (−dy, dx)·w, v1 = p0 − (−dy, dx)·w, v2 = p1 − …, v3 = p1 + … (`0x471b80..
  0x471cd5`), so **u runs along the line (p0 → p1) and v across it** in mode 0. Flag 0x400 = own half width (else
  `[0x4b7aa0]`), 0x800 = own colours (else `0x4b7a84`), 0x200 = textured; it is always submitted with flags **0x24**
  (additive ONE/ONE, view-space vertices, `0x471e74`). Afterwards it resets its uv set with `0x470d80(0, 1)` **only if
  `S+0x204 != 0`** (`0x471eb7`) — and no instruction writes `S+0x204` (the six `mov [reg+0x204]` in the exe belong to
  other objects), so the reset never happens. Only the lightning bolt (`0x46d932`) ever sets the line's uv set, so every
  later textured line (the rod arcs `0x46da00`, the rain streaks `0x47de10`, the hit-star speed lines `0x474e00`, the other
  callers `0x46e530`, `0x46f2d0`, `0x46fb30`, `0x477fa0`, `0x478b70`, `0x479d70`, `0x47a790`) is drawn with the mirror of the **last bolt segment** until the next bolt; before the first bolt, mode 0.
* **`S+0x260` is the quad's shape** (`0x470f1e`: `edi = table[S+0x260]`, the `base` angle below). Every effect that sets it
  uses 0x12 (the square) except the ribcage (0x1a) and the race board's flames (0x13); the storm's glow sprite
  (STORM.md §5.2) is an ordinary square.
* **Plane axes `0x471ee0(out, n)`** (4×4, columns): column 0 = u, column 1 = v, column 2 = n. Generally
  `u = normalize(n.z, 0, −n.x)` (level, across the normal), `v = n × u`; for n ≈ (0, ±1, 0) (`|n.x|, |n.z| < 0.001`,
  `1 − |n.y| < 0.001`) `v = normalize(0, −n.z, n.y)`, `u = v × n`. The corner offsets go along u (x) and v (y).
* **Frame `0x46d320(out, d)`** (rows X, Y, Z = d; used by §3 and §6): generally `X = normalize(d.z, 0, −d.x)`,
  `Y = d × X`; for `|d.x|, |d.z| < 0.001`: `Y = normalize(0, d.z, −d.y)`, `X = Y × d`.

### 1.1 The colour path, traced to the device (settles the pickup-particle brightness)

1. **Vertex colour** (`0x4710b7..0x471216`): `[0x5e8650]+0x20` = the device offers MODULATE2X (LIGHTING.md §1.5 step 6). If
   set, the four corners get the sprite's rgba `+0x214..0x220` (flag 2) or `0x4b7a84` = (0.5, 0.5, 0.5, 1) as they are; if
   not (`0x47121b..0x4713a1`), every rgb is doubled (`fadd st0, st0`) and clamped to 1 (`0x4713a7..`), alpha unchanged.
2. **Submit** (`0x4719b2..0x4719f3`): `0x481560(renderer [0x5e86ac], 4 vertices, S, texture [0x5e866c] + (image & 0xffff)·0x74,
   0x20 | (flags & 8 ? 8 : 4) | (flags & 0x80 ? 1 : 0))`.
3. **`0x481560`**: flag 8 (`0x481a05..`) opens a batch of mode 2 (`0x481a82`: list `renderer+0x1c8`) with colour byte
   `fistp(c·255)` per channel and alpha byte `fistp(a·255)` (`0x481ab2..0x481b26`, `0x4aa308` = 255); flag 4 (`0x481d8c..`)
   a batch of mode 3 (`0x481e16`: list `+0x1cc`) with colour byte **`fistp(a·c·128)`** (`0x481e5e..0x481f37`, `0x4a9020` =
   128.0) and no alpha.
4. **Flush**: the frame's render step `0x4293f0` (called at `0x401756` after the game frame `0x401ab0`, so after the effect
   driver `0x470c70` of `0x401dfa`) sets stage-0 COLOROP = MODULATE2X at `0x429758` (only if `device+0x20`), then draws the
   model lists and calls `0x428d00` (`0x4299b6`), which walks `+0x1c8` (SRCALPHA/INVSRCALPHA) and `+0x1cc` (ONE/ONE,
   `0x429182`/`0x429198`). Of the SetTextureStageState calls on the device (`[[0x5e8650]+0x34]`, vtable `+0x94`), the
   stage-0 COLOROP writes are only the device setup `0x47ed23` (MODULATE2X), `0x4294be` (MODULATE, before the world) and
   `0x429758` (MODULATE2X); `0x428d00` itself only sets ADDRESS (`0x428eef`), and `0x429a30` / `0x429e20` write stage 1
   (`0x429b36`, `0x429f15`, ...), not stage 0.

Where the batches end up in the frame (one batch per sprite, depth = view z of the first clipped corner, 256 depth buckets
shared with the fade list, far to near): MODEL_RENDER.md §10.

Result, both device kinds: **alpha blended (flag 8): texture × min(1, 2c), alpha a** (0.5 = the plain texture);
**additive (no flag 8): texture × c × a × 256/255 added** (0.5 = half the texture; alpha is a brightness). The port's
`hud_world_spr_mode`, `hud_world_fx`, `hud_world_fx_plane` (additive: `glColor(c·a)`, MODULATE, ONE/ONE; blended:
`blend2x_begin` = `GL_COMBINE` with `RGB_SCALE` 2, or doubled and clamped without it) and the line/ribbon/quad helpers
(always additive) follow this rule.

The sprite callers checked against it (rgb / flags as the original writes them; all port counterparts pass the same values):

| original | image | flags | rgb, alpha | path | port |
|---|---|---|---|---|---|
| `0x4791f0` pickup particle | 4 | 7 | 0.5, 1 | additive: texture × 0.5 | `fx_update` kind 2: was rgb 1 (2× too bright), now 0.5 |
| `0x477350` death stars | 10/11 | 0x4f | 0.5 | blended: plain texture | `stars_draw`: was 1 on a 1× path, now 0.5 on the 2× path (same pixels) |
| `0x477350` their glow | 5 | 3 | (1, 1, 0.5) | additive | `stars_draw` glow |
| `0x476710` bomb smoke ring (explosion kind 0) | 24 | 0xa | 0.8 | blended: texture × 1.6 | `bombs_draw` `hud_world_fx_plane`: was ×0.8 (no ×2), now ×1.6 |
| `0x478a8c` launcher muzzle puff | 24 | 0xb | 1 | blended: texture × 2 (clamped) | `bombs_draw`: was ×1, now ×2 |
| `0x4765f0` bomb flash | 4 | 3 | (1, 1, 0) | additive | `bombs_draw` yellow |
| `0x478b70` fuse spark | 18 | 7 | 1, 0.7 | additive | `bombs_draw` |
| `0x470420` fireball head | 12 | 5 | 0.5, 0.7 | additive | `fireball_draw` 0.5 |
| `0x46f180`, `0x46f2d0`, `0x46fa40`, `0x4702b0`, `0x470370` projectile heads, muzzle flashes, sparks | 4, 6, 13, 32 | 7 | 1 | additive | `launchers_draw` 1 |
| `0x46fb30` flame | 4 | 7 | (1, 0.58, 0) | additive | `flame` |
| `0x474e00` / `0x475040` hit star spark / flash | 8 / 9 | 7 / 3 | 1 (flash alpha 0.4) | additive | 1 |
| `0x475380`, `0x475cd0`, `0x47e370` puffs | 14 | 7 | 1 | additive | 1 |
| `0x4762e0` explosion flash planes | 12 | 2 | 1 | additive | `fx_smoke_draw` 1 |
| `0x4767f0` burning head, `0x479760` peck flash | 12 / 18 | 3 | 0.8 / 1 (0.8) | additive | same |
| `0x4781b0` / `0x478290` splash ring / ripple, `0x473050` wake | 0x3a / 3 | 6 / 2 | (0.65, 0.65, 0.8) / 0.8 | additive | same |
| `0x479519` bonus halo, `0x478753` speech bubble | 0x13-0x17, 0x2e / 44-52 | 0x1b / 0x49 | 0.5 (default) | blended: plain texture | rgb 1 on a 1× path (same pixels) |

## 2. Footsteps `0x47cba0(pos, normal, dir, foot, kind)` (FOOTSTEPS.md §1)

Called by the walk cycle `0x463f40` with `foot = 1` at cycle fraction 0.38 (`0x464244`) and `foot = 0` at 0.9
(`0x464289`), kind 3 on ground type 2, else 2. `s = normal × dir` (`0x47cbe4..0x47cc1a`); `K = foot ? −15 : 15`
(`0x4abd88` / `0x4a9864`): with n = up this puts foot 0 on the LEFT of the walk direction.

* **kind bit 0** (kind 3 only): one record, **lifetime 15 s**, callback `0x47c9a0` — the print — at
  `pos + (0, 0.1, 0) + s·K` (`0x4a9008` = 0.1), `+0x14` normal, `+0x20` dir, `+0x2c` foot (never read).
* **kind bit 1** (kinds 2 and 3): `0x47e660(pos, normal, dir, foot)` — one emitter record, lifetime **0.2 s**,
  callback `0x47e460`, at `pos` itself (not offset), `+0x30` accumulator = 0.

### 2.1 The print `0x47c9a0` (15 s)

`R = dir × normal` (`S+0x23c`), `F = normal × R` (`S+0x248`: dir flattened into the ground), `N = normal`. Colour
(0.5, 0.5, 0.5). Size `0x40c350(Perso) == 1 ? 40 : 20` (Perso subtype 1 = Woody; Knothead/Splinter get the small print).
Two sprites, same place and size:

1. image **68** (`0x10044`, 24 bpp oval), alpha `(1−u)·0.1`, flags **0x22** (own colour, plane R/F, additive);
2. image **69** (`0x10045`, a crescent: dark rgb, shaped alpha), alpha `(1−u)·0.3`, UV set **1** (v flipped),
   flags **0x6a** (own colour, plane R/F, alpha blended, UV set).

So a print is a faint bright oval pressed into the snow/sand with a dark crescent rim; it fades linearly over 15 s.
It does not depend on the foot (no mirroring left/right).

### 2.2 The puffs: emitter `0x47e460` (0.2 s) → particle `0x47e370` (0.8 s)

Emitter: `acc += dt; n = ftol(acc·50); acc −= n·0.02` (`0x4a9030` = 50, `0x4aa1c0` = 0.02) ⇒ **50 puffs a second**,
about 10 per step. `A = normal × dir` (a second vector `dir × A` is computed at `0x47e4f7..0x47e535` and never used).
Per puff: `+0` age = `rnd·0.2` (`0x4a9760`), life **0.8** (`0x3f4ccccd`), side
`s = foot == 0 ? rnd·15 + 5 : −5 − rnd·15`, pos = `pos + A·s`, `+0x14` = −dir, `+0x20` rot = `ftol(rnd·512)`.

Particle `0x47e370`: position `P + (−dir)·u·100 + (0, u·30, 0)` (`0x4a9010` = 100, `0x4a9740` = 30): drifts back
where he came from and up; image **14** (the dark smoke clouds), colour (1,1,1), alpha `(1−u)·0.1`, size `u·15 + 20`
(`0x4a9864`, `0x4a9994`), rotation fixed, flags **7** (camera facing, additive). Very faint: an additive 0.1 of a dark
image is a light haze at his heels.

## 3. The smoke ring `0x476140(pos, normal, kind, t0, life)`

A ring of clouds that spreads out in the plane across `normal`. Table `0x4b7c60`, 20 B per kind:
`{float R; int image; float size; float alpha; u8 blend}`:

| kind | R | image | size | alpha | blend | callers |
|---|---|---|---|---|---|---|
| 0 | 200 | 14 (dark smoke) | 80 | 0.5 | 1 | explosion kind 0 `0x47733b` (bombs: `(pos, n or (0,1,0), 0, 0.25, 1.5)`); bomb fuse record `0x478c0e` (the launcher's muzzle, around the firing direction, same args); Buzz boss mode 2 `0x40f9c0` / `0x410a66` (`(pos − (0,50,0), (0,1,0), 0, 1.5, 6.0)`, BOSS14.md) |
| 1 | 50 | 16 (white cloud) | 60 | 0.05 | 1 | none |
| 2 | 50 | 16 | 60 | 0.025 | 1 | none |
| 3 | 100 | 16 | 60 | 0.1 | 1 | landing on ground type 2 `0x4644c6` (`(pos + (0,30,0), &ground normal, 3, 0.25, 1.5)`) |

Creation (`0x476146..0x4762d5`): `n = R·2π / (size·0.3)` (float; `0x4aa0f0` = 2π, `0x4aab98` = 0.3) — the clouds sit
0.3 of their size apart round the circle; `step = fistp(512/n)` (rounded, `0x4a9874` = 512); `(X, Y) = 0x46d320(normal)`.
For `i = 0; i < n; i++, a += step` (a full pool skips the cloud but not its place): record with `+0` age =
`t0 = min(t0, 0.9·life)` (`0x4a94b8`), `+4` life, callback `0x475fb0`, `+8` pos, `+0x14` dir = `X·cos a + Y·sin a`,
`+0x20` size jitter = `rnd·q·2 − q` with `q = size·0.25` (`0x4a9ca0`), `+0x24` = i, `+0x28` = kind.
Kind 0: n = 52.4 ⇒ 53 clouds, step 10; kind 3: n = 34.9 ⇒ 35 clouds, step 15.

**So the third argument is the kind, not a count** (FOOTSTEPS.md §3 read it as "3 particles"), `t0` is the start age
and `life` the lifetime.

Cloud `0x475fb0`: `S = sin(2π·ftol(128u)/512)` (a quarter sine, `0x4abd0c` = −128); position `P + dir·R·S`; size
`(u + 1)·size·0.5 + jitter` (`0x4a9014` = 0.5); alpha `(1 − S)·alpha`; colour (1,1,1) ⇒ texture × 2 in the blended
path; rotation `(i·5000) & 511` (`0x4760e1`); flags `blend ? 0xf : 7` (every kind has blend = 1: camera facing,
own colour, rotation, alpha blended).

## 4. The peck impact `0x479c80(kind, point, normal)` (OBJECTS.md §1.6)

| kind | caller | what |
|---|---|---|
| 1 | attack ray `0x4575b0` on every hit (`0x457690`, normal = 0) | one **flash** record `0x479760`, lifetime **0.05 s** |
| 0 | climb state 4 sub 2 every 0.3 s (`0x4655d2`, with the wall normal) | one **emitter** record `0x4798f0`, lifetime **0.2 s**, `+0x14` normal, three accumulators `+0x20/+0x24/+0x28` = 0 |
| other | – | nothing |

**Flash `0x479760`**: image **18** (`0x10012`, the yellow glow), at the point, colour (1,1,1), alpha 0.8, size 80,
flags **3** (camera facing, additive, no rotation).

**Emitter `0x4798f0`** (adds dt to all three accumulators every frame):
* `n = ftol(acc0·33); acc0 −= n·(1/33)` (`0x4abd6c`, `0x4abd68`): **33 splinters a second** (6 or 7 per peck), record
  `0x479670`, age 0, life `(rnd + 1)·0.3`; `a = ftol(rnd·511)`; start `P + (10·cos a, 0, 10·sin a)`, direction
  `normalize(cos a, 0, sin a)` — level and outward, whatever the orientation of the wall; `+0x20 = ftol(rnd·511)`.
* `n = ftol(acc1·50); acc1 −= n·0.02`: **50 flashes a second** (the 0.05 s flash above, so it stays lit for 0.2 s).
* `n = ftol(acc2·8); acc2 −= n·0.125` (`0x4aab9c`, `0x4a9db8`): **8 holes a second** — in 0.2 s exactly one per peck.
  Record `0x479800`, age 0, life `rnd·0.3 + 5`; `v = rnd·20 − 10`; position `P + u·v` with u = column 0 of
  `0x471ee0(normal)` (level along the wall); `+0x14` normal; `+0x20 = ftol(rnd·511)`.

**Splinter `0x479670`**: position `P + dir·u·80` horizontally, `y = P.y + 100·sin(2π·ftol(255u)/512)` (an arc 100 up
and back down; `0x4ab134` = 80, `0x4abd00` = −255); image **25** (`0x10019`, the wooden splinter); colour (1,1,1),
alpha 1; size `rnd·5 + 10` **re-rolled every frame** (it flickers 10..15); flags **0xf**. The callback never writes
`S+0x224`, so the splinter is drawn at whatever rotation the previous sprite of the frame left in S (its own `+0x20`
is never read) — the port uses `+0x20`.

**Hole `0x479800`**: alive while `age < life + 4` (`0x4a94c0`, the only callback that outlives its `+4`); image **26**
(`0x1001a`, a hole with a splintered rim), at its position, plane normal `+0x14` (`S+0x230`), colour **(0.8, 0.8, 0)**
(⇒ texture × (1.6, 1.6, 0): warm yellow-brown), alpha 1 while `age < life`, then `1 − (age − life)·0.25`; size **15**;
rotation `+0x20`; flags **0xe** (plane with normal, own colour, rotation, alpha blended). So a hole stays 5..5.3 s and
fades in 4 s.

## 5. Explosion particles (`0x477060(kind, pos, normal)`)

`0x477060` (BOMB.md §4.3, ROCKET.md §5.3) creates, in this order:
* kind 0 (`0x4771dd`): `0x4765f0` 0.25 s, `0x476710` 0.3 s (both ported earlier: flash and ground ring), `0x476cd0`
  0.2 s (§5.1), `0x476140(pos, n or (0,1,0), 0, 0.25, 1.5)` (§3);
* kind 1 (`0x477089`): `0x476b50` 0.2 s (§5.2), `0x476cd0` 0.2 s (§5.1), `0x4762e0` R 1400, falling through into kind 2;
* kind 2: `0x4762e0` R 400.

Callers of kind 1: rocket `0x453538`, chest `0x4517ee`, Buzz boss `0x40ff63..0x41001e`, message **1509** mode 5
(`0x46cfaa`, explosion at a vector of an instance); kind 2: missile `0x46fbec`, fireball `0x4704d7`; kind 0: bombs
`0x44d7a9`.

### 5.1 Dust burst: emitter `0x476cd0` (0.2 s) → cloud `0x4764f0` (0.7 s)

Emitter: `n = ftol(acc·400); acc −= n·0.0025` (`0x4a964c`, `0x4abd2c`): **400 clouds a second**, ~80 in all. Per
cloud: life **0.7**, `+0x24` size `rnd·50 + 80`, `+0x28` image `ftol(rnd·2) ≠ 0 ? 16 : 17` (`0x476da7`: neg/sbb),
`+0x2c` rot `ftol(rnd·511)`, `a = ftol(rnd·255)`, `b = ftol(rnd·511)`, `r = rnd·150 + 20`. While the emitter's
`u < 0.8` (`0x4a987c`): `+0x20` D = **600**, direction `(cos a, sin a, cos a·sin b)` (upper half, **not** normalised);
in its last fifth D = **40**, direction (0, 1, 0). Start `P + (cos a, sin a, cos a·sin b)·r` in both cases.

Cloud `0x4764f0`: `s = −costab[ftol(64u + 128) + 128]·D·u = D·u·cos(πu/4)`; position `start + dir·s`; image 16 or 17
(white clouds), colour (1,1,1) (⇒ texture × 2), alpha `(1 − u)·0.2`, size fixed, rotation fixed, flags **0xf**.

### 5.2 Burning debris: emitter `0x476b50` (0.2 s) → piece `0x4767f0` (2 s) → trail `0x476f00` / `0x476fb0`

Emitter: `n = ftol(acc·60); acc −= n/60` (`0x4ab284`, `0x4a9990`): **60 pieces a second**, ~12. Per piece: life 2.0,
direction `normalize(2r − 1, 2r − 0.5, 2r − 1)` (`+8`), start `P + dir·300` (`+0x14`; `0x4a986c` = 300), `+0x20`
(time not yet laid out), `+0x28` (time along the current leg) = 0.

Piece `0x4767f0` (`age` and `+0x20` both += dt; freed when `age ≥ life`): while `+0x20 > 0.025` (`0x4abd28`):
`+0x28 += 0.025`, at `Q = +0x14 + dir·(+0x28)·10·120` (`0x4abd24` = 10, `0x4abcb4` = 120; so 1200 u/s) create a
**smoke** record `0x476f00` (life 0.2, size `rnd·20 + 40`, image **15** `0x1000f`, alpha factor 0.3, rot `ftol(rnd·512)`)
and a **spark** record `0x476fb0` (life 0.15, size `rnd·20 + 60`, image **13** `0x1000d`, alpha factor 1.0, rot
random); if `+0x28 ≥ 0.1` the leg ends: `+0x14 += dir·120`, `dir.y −= 0.2` (`0x4a9760`), `dir = normalize(dir)`,
`+0x28 = 0`; then `+0x20 −= 0.025`. So every 0.1 s the piece bends 0.2 downwards — a ballistic-looking arc built
out of 120-unit legs, a puff every 30 units. The head: at `+0x14 + dir·(+0x28 + +0x20)·1200`, image **12** (`0x1000c`),
colour (0.8, 0.8, 0.8), alpha 1, size 40, flags **3** (additive).

`0x476f00`: fixed position, colour (1,1,1), alpha `(1 − u)·factor`, flags **0xf** (white smoke, blended).
`0x476fb0`: the same with flags **7** (the orange fire in it, additive).

## 6. The skeleton flash `0x477e40` / `0x477980` (PERSO_DEATH.md §4.2 has the table)

Verified against PERSO_DEATH.md §4.2; additions:
* The phases (`0x4779d1..0x477b84`): skeleton A for u in [0, 0.125) and (0.25, 0.375); B for (0.5, 0.625) and
  (0.75, 0.875); the model otherwise. A = offsets row 2 (float index 0x24 = vec3 12, `0x4b7da0`), UV row 12, head
  rotation 40; B = row 0, UV row 0, head rotation 472. The UV index is `i + (12 or 0)` into `0x4b7cb0`.
* The ribcage (i = 3) uses sprite **mode 0x1a**: a 1:2 upright quad (§1), half diagonal 75.
* Both sprites use the **default colour** (flag bit 1 off): the bone (flags 0x4d, blended) at texture × 1, the glow
  (flags 0x45, additive) at texture × 0.5; alpha 1.
* The light `0x498790(kind 0, S+0x208, white, rnd·100 + 200)` is registered every frame (`0x477d9d..0x477dc6`), also
  during the model phases, when no sprite is drawn: `S+0x208` is then whatever the last sprite handed to `0x470f10`
  was, this frame or an earlier one (in a skeleton phase it is the last bone's glow, the right leg). LIGHTING.md §7:
  never drawn. Port: `hud.c` keeps `S+0x208` (`hud_last_sprite_pos`, written by every `hud_world_spr_mode` /
  `hud_world_fx` / `hud_world_fx_plane` call before it draws or bails out) and `FX_SKELETON` registers the light there.
* Also called by the race kill `0x44c59d` (kind 2, RACE.md §4.9).

## 7. The port

All of the above live in the port's copy of the pool (`g_fx`, `fx_new`, `fx_update` in `src/main_engine.c`), one
`FxKind` per callback: `FX_STEP_EMIT/PUFF/PRINT` (§2), `FX_RING` (§3), `FX_PECK_EMIT/CHIP/FLASH/HOLE` (§4),
`FX_DEBRIS_EMIT/DEBRIS` (§5.1), `FX_BURN_EMIT/BURN/TRAIL_SMOKE/TRAIL_SPARK` (§5.2), `FX_SKELETON` (§6), drawn by
`fx_particle`. The sprite primitive is `hud_world_spr` / `hud_world_spr_mode` (`src/hud.c`), which takes the
original's flags as they are (§1); the alpha-blended path uses `GL_COMBINE` with `RGB_SCALE` 2 for MODULATE2X (`blend2x_begin`,
shared with `hud_world_fx` / `hud_world_fx_plane` since 2026-09-26, §1.1).

| original | port |
|---|---|
| `0x47cba0` | `game_footstep` (player.c passes foot 1 at 0.38 and 0 at 0.9) |
| `0x476140` | `game_smoke_ring`; `game_land_dust` = kind 3; the Buzz boss (`boss.c`) calls kind 0 (1.5, 6.0) |
| `0x479c80` | `game_peck_fx` |
| `0x477060` particles | `fx_explode` (kind 0 from `bomb_explode`, kind 1 from `game_explosion`, which the rocket now also uses) |
| `0x478c0e` muzzle ring | in `bombs_draw` with the launcher's muzzle smoke |
| `0x477e40` | `game_skeleton` from `player_kill` kinds 2/9 and the race kind 2 |
| `0x475440` / `0x475380` | `board_fx_draw` / `FX_BOARD_PUFF`: the race board's jets and smoke (RACE.md §2.2); needs sprite mode 0x13 (base 37, a 2:1 quad), which `hud_world_spr_mode` now knows |

The line primitive `0x471a10` is `hud_world_streak` / `hud_world_streak_flip` / `hud_world_beam` (`world_line_uv` in
`hud.c`); the uv table `k_uvset` is shared by sprites and lines, and the line's current set `g_line_uv` persists as in
the original (§1): only `hud_world_streak_flip` (the bolt) changes it.

Deviations: the splinter's rotation (§4); the player's facing at the skeleton flash is set to face the camera's look
direction at once (0x459ff0 resets the Mover ramps; the port sets `yaw`); the plane sprites get a GL polygon offset
instead of the original's 0.1-unit lift only (depth writes are off in both). Message 1509 and the smoke plume are ported
(§10).

Testing (`WOODY_FXLOG=1` logs every footstep, ring, peck, explosion and skeleton flash):
* footsteps + prints on sand: `W2A --pos 7220 1100 -13973 --yaw 45 --walk 2.5`; landing dust: the same with `--jump 1.0`
  (and the level-start landing);
* peck holes and splinters: `W1A --pos 3930 520 3153 --yaw 90 --walk 1.5 --peck 1.0 0.1` (climb wall inst 52);
* explosion kind 1: the rocket, `W1A --pos 8845 1140 330 --peck 1.0 0.3`, explodes at ≈ 8.3 s;
* explosion kind 0 / muzzle rings: `WOODY_GOD=1 W2A --pos 7100 1100 -13750 --yaw -54` (bomb throwers);
* skeleton flash: `WOODY_KILLAT="1.5 2" W1A` (Kill(2) at 1.5 s).

## 8. Corrections to earlier documents

* FOOTSTEPS.md §3: the third argument of `0x476140` is the ring kind, not a count, and 0.25 / 1.5 are the start age
  and lifetime; the particles are 35 white clouds on a ring of 100. `0x476140` is not the "shards of a rocket
  explosion": it belongs to explosion **kind 0** (bombs), not kind 1.
* FOOTSTEPS.md §4 / OBJECTS.md §1.6: the reconstructions (multiplied image-14 mark, gouged hole, tumbling chips,
  sawdust) are replaced by the ports above. The original's peck hole is a 21-unit sprite that lasts 5..9 s, not a 20 s
  gouge; kind 1 (the attack ray) makes no hole at all, only a flash.
* PROJECTILES.md §5.3 / ROCKET.md §5.3 uncertain 3, BOMB.md §4.3: `0x4767f0`, `0x4764f0`, `0x476cd0` and `0x476140`
  are read (§3, §5).
* FOOTSTEPS.md §1 swapped nothing, but the port did: foot 1 is the step at 0.38, foot 0 the one at 0.9.

## 9. Script effects: messages 1507 (hit star) and 1508 (torch flames)

Both are cases of the 1500 subsystem handler `0x46cca0` (this `0x5e823c`); the argument is the instance reference,
`& 0xffffff` into the level table `[0x50944c]+0x6c`.

### 9.1 Message 1507 `[inst]` = the hit star at the instance (`0x46ce85`)

`0x4750e0(&inst+0xc)`: the same hit star as a hit ordinary enemy's (PERSO_SPECIAL.md §3.4 has every constant: one 0.1 s
flash `0x475040`, image 9, half diagonal `250·u`, alpha 0.4, flags 3; eight 0.4 s sparks `0x474e00`, speed lines to
spinning stars of image 8, 20..40, on spokes 45° ± 17.6° apart across the view direction). The point is the instance's
`+0xc` position, read once. Senders (all cinematics, repeated with `DELAYPOP`): K3R object 277 → slot 420 (model 38,
a marker + volume dummy) four times at the race-end cinematic, S2R object 425 → slot 429 (model 44) once, WWS object 344
→ slots 372 (twice) / 373 (model 50) in the cinematic behind var 59. Port: `case 1507` → `game_hit_star(in->position)`.

### 9.2 Message 1508 `[inst]` = torch flames (`0x46ceae`, `0x47cdf0`, `0x47cea0`, `0x47cf10`, `0x47cd00`)

`0x46ceae`: `node = malloc(0x14)`, `node+0 = inst`, `node+0x10 = [0x5e8638]`, `[0x5e8638] = node` (push front), then
`0x47cdf0(node)`:
```
node->n = 0;                                                      /* +4 */
while (n < model+0x50 && 0x42f6b0(inst, /*typecode*/0, &tmp, n)) n++;   /* count the type-0 markers (0x47ce0f) */
node->pts = malloc(n * 24); node->t = malloc(n * 4);              /* +0xc: both points of every marker, +8: timers = 0 */
for (i = 0; i < n; i++) { 0x42f6b0(inst, 0, &node->pts[i], i); node->t[i] = 0; }
```
The marker points are taken **once**, in the pose of the moment (`0x42f6b0` runs `vtbl[2](1)` first); only point 0 is
used later. Per frame `0x47cea0` walks the list (called by the subsystem frame `0x46d004` at `0x46d0ba`, right before the
pool driver `0x470c70`); level end frees it (`0x47cec0` from `0x46d180`). Per node `0x47cf10`:
```
if (inst+0x58 != [0x509adc]+0) return;                            /* the instance's clock did not run this frame = not drawn */
for (i = 0; i < n; i++) {
    t[i] += dt; k = ftol(t[i] * 15); t[i] -= k * 0.0666667;       /* 15 flames a second per marker (0x4a9864, 0x4abd8c) */
    while (k--) {                                                 /* pool record, callback 0x47cd00 */
        dx = rnd*20 - 10; dz = rnd*20 - 10;                       /* 0x4a9994, 0x4a9750 */
        rec.pos = pts[i].P0 + (dx, 0, dz);
        f = 1 - sqrt(dx*dx + dz*dz) * 0.1;                        /* 0x4a974c; 1 in the middle, down to -0.41 in the corners */
        rec+0x14 R = rnd * (rnd * f * 20) + 40;                   /* 0x4ab294 */
        rec+4 life = rnd * f * 0.5 + 2.5;                         /* 0x4a9014, 0x4aa3e0 */
    }
}
```
The flame `0x47cd00` (u = age / life): `pos.y += dt * 40` (`0x4ab294`, it rises 40 a second and so about 100 in all);
sprite at `(pos.x + rnd*u, pos.y, pos.z + rnd*u)` (a jitter of at most one unit), rgba `(0.5, 0.5 − 0.5u, 0.5 − 0.5u,
1.0)`, image `0x1000c` = bank 0 **image 12**, mode 0x12, half diagonal `(1 − u) · R`, flags **3** (camera facing, own
colour, additive). So about 40 additive puffs of 40..60 per marker, white-orange at the bottom (image 12 at full
strength), turning red and shrinking to nothing as they rise: a torch flame.

Senders: W2D objects 772/773/776/777 (model 60, the torches that flank the two doors at z 16300 and 9120) and W3D
objects 111-116, 126, 127, 130, 131, 165, 832, 833 (model 19, the wall torches of the tunnels). Each model has one
type-0 marker (W3D model 19: node 7, P0 = (9, −1.2, 53) in model space = the mouth of the torch cup).

Port (`src/main_engine.c`): `torch_add` (the node, marker points via `inst_vector_at(in, 0, n)`), `torch_update` (the
emission, run before `fx_update`; "clock ran" = the renderer's `inst->drawn`) and `FX_FLAME` in `fx_particle`.
`WOODY_FXLOG=1` prints `torch: inst …, 1 marker at …`. Test: `W3D --cam 3400 -3080 -3840 -90 -5 --shot t.ppm 4` (torch
111 burning next to the tunnel wall), `W2D --cam -3925 2850 15700 0 -5` (the two torches beside the door). Before
the port these torches were dark cups.

## 10. Message 1509 and the smoke plume `0x475f30`

Senders (scan of the 28 disassembled scripts for `PUSH 1509 ... SEND 5`): only W1B, five sends `[405, 399, mode, x]` = (4, 1), (5, 0),
(5, 1), (5, 2), (4, 0) around cinematic 73; the handler `0x46cf6f..0x46cffa` was re-read against the port (`game_msg1509`): it matches.

**1509 `[a, inst, mode, x]`** (`0x46cf6f`, subsystem `0x46cca0`; `a` = arg 0 is not read, the instance is arg **1**):
mode 5 = `0x42f6b0(0, &v, x)` (typecode-0 marker x of inst) then `0x477060(1, &v, 0)` = explosion kind 1 (§5); mode 4 with
`x == 1` = `0x475f30(inst, 0)`, `(inst, 1)`, `(inst, 2)`; mode 4 with any other x = bytes `0x5e857c..0x5e857e` = 0 (the
plumes die out); other modes nothing. Only W1B sends it: object 397 around cinematic 73 with the cinematic saucer 399
(`[405, 399, 4, 1]`, then `[.., 5, 0]`, `[.., 5, 1]`, `[.., 5, 2]`, finally `[.., 4, 0]`).

**Plume `0x475f30(inst, n)`**: one pool record (life 100000 s, callback `0x475d90`, `+8` inst, `+0xc` n, `+0x10..+0x18`
prev = the marker's position now) and `smoke_on[n] = [0x5e857c + n] = 1`. The buzz boss uses the same function on hits
(BOSS14.md §9.2). **Emitter `0x475d90`**: `age += dt`; if `smoke_on[n] != 1` the record frees itself; else
`k = fistp(age·300)` (`0x4a986c`; fistp rounds to nearest), `age −= k·(1/300)` (`0x4aa3e4`), p = marker n now, D = prev − p,
and for i = 0..k−1 a puff at `p + (i/k)·D + (rnd·30 − 15, 0, rnd·30 − 15)` (`0x4a9740` = 30, `0x4a9864` = 15); `prev` becomes
the position of the LAST puff (jitter included), so the trail lags behind a moving marker. **Puff `0x475cd0`** (0.5 s):
`u = age/0.5`; position `(x, y + 50u, z)` (`0x4a9030`), colour (1, 1, 1), alpha `0.5 − 0.5u` (`0x4a9014`), size
`rnd·10 + 50u + 20` (`0x4a9750`, `0x4a9994`), rotation `fistp(rnd·512)` (`0x4a9874`), image 14 (`0x1000e`), mode 0x12,
flags **7** (camera facing, own colour, rotation; additive). Port: `smoke_attach` / `game_boss_smoke` / `game_msg1509` /
`boss_fx_draw` in main_engine.c (replacing the earlier reconstruction: grey 0.6, size `(rnd·10 + 20)(1 + u)`, rise 50t).
Test: `W1B --pos -7257 1400 -7200 --cam -6650 4900 -7600 180 0` with
`WOODY_MSGAT="1 6 399 1; 1.5 1509 405 399 4 1; 2 1509 405 399 5 0; 2.5 1509 405 399 5 1; 3 1509 405 399 5 2; 5 1509 405 399 4 0"`
(399 stands on its cinematic track, its markers are ~4800 up) and `WOODY_FXLOG=1`.
