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
`age += dt; u = age / life` and frees itself with `+4 = −1` once `u ≥ 1` (exceptions noted).

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

* **Colour path** (`0x4710b7`): with a MODULATE2X device the colour is copied as is; without one it is doubled
  and clamped to 1 (`0x471224`, `0x4713a7`). The end result is the same: in the alpha-blended path c = 0.5 is
  neutral and c = 1.0 doubles the texture (saturating).
* **Corners** (`0x470f39..0x4710b3`): corner k sits at `size·(cos t, sin t)` in the quad's own axes with
  `t = rot + base`, `rot − base + 256`, `rot + base + 256`, `rot − base + 512` (1/512 turn). `base` comes from the table
  `[0x5e823c]+0x800[mode]`, filled at `0x4024bb`: `table[8r + c] = ftol(atan(2^(r−c)) · 81.4873)` (`0x4a9028` = 512/2π)
  for r, c = 1..7. Mode 0x12 (r = c = 2) is the square (base 64 = 45°), mode **0x1a** (r = 3, c = 2) is a 1:2 upright
  quad (base 90, `atan 2`). So `size` is the half DIAGONAL (the known BONUS.md §2.4 fact) for every mode.
* **UV sets** `0x470d80` (jump table `0x470ef4`), for the corners in the order above (u, v):
  0 = (1,0)(0,0)(0,1)(1,1) · 1 = v flipped · 2 = u flipped · 3 = both · 4 = (1,1)(1,0)(0,0)(0,1) · 5 = unchanged · 6 = (0,0)(0,1)(1,1)(1,0).
  Without flag 0x40 a sprite whose last set was ≠ 0 is reset to 0.
* **Plane axes `0x471ee0(out, n)`** (4×4, columns): column 0 = u, column 1 = v, column 2 = n. Generally
  `u = normalize(n.z, 0, −n.x)` (level, across the normal), `v = n × u`; for n ≈ (0, ±1, 0) (`|n.x|, |n.z| < 0.001`,
  `1 − |n.y| < 0.001`) `v = normalize(0, −n.z, n.y)`, `u = v × n`. The corner offsets go along u (x) and v (y).
* **Frame `0x46d320(out, d)`** (rows X, Y, Z = d; used by §3 and §6): generally `X = normalize(d.z, 0, −d.x)`,
  `Y = d × X`; for `|d.x|, |d.z| < 0.001`: `Y = normalize(0, d.z, −d.y)`, `X = Y × d`.

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
* During the model phases no sprites are drawn, but the light `0x498790` is still registered at `S+0x208`, i.e. at
  whatever the last sprite of the frame was (LIGHTING.md §7: never drawn).
* Also called by the race kill `0x44c59d` (kind 2, RACE.md §4.9).

## 7. The port

All of the above live in the port's copy of the pool (`g_fx`, `fx_new`, `fx_update` in `src/main_engine.c`), one
`FxKind` per callback: `FX_STEP_EMIT/PUFF/PRINT` (§2), `FX_RING` (§3), `FX_PECK_EMIT/CHIP/FLASH/HOLE` (§4),
`FX_DEBRIS_EMIT/DEBRIS` (§5.1), `FX_BURN_EMIT/BURN/TRAIL_SMOKE/TRAIL_SPARK` (§5.2), `FX_SKELETON` (§6), drawn by
`fx_particle`. The sprite primitive is `hud_world_spr` / `hud_world_spr_mode` (`src/hud.c`), which takes the
original's flags as they are (§1); the alpha-blended path uses `GL_COMBINE` with `RGB_SCALE` 2 for MODULATE2X.

| original | port |
|---|---|
| `0x47cba0` | `game_footstep` (player.c passes foot 1 at 0.38 and 0 at 0.9) |
| `0x476140` | `game_smoke_ring`; `game_land_dust` = kind 3; the Buzz boss (`boss.c`) calls kind 0 (1.5, 6.0) |
| `0x479c80` | `game_peck_fx` |
| `0x477060` particles | `fx_explode` (kind 0 from `bomb_explode`, kind 1 from `game_explosion`, which the rocket now also uses) |
| `0x478c0e` muzzle ring | in `bombs_draw` with the launcher's muzzle smoke |
| `0x477e40` | `game_skeleton` from `player_kill` kinds 2/9 and the race kind 2 |

Deviations: the splinter's rotation (§4); the player's facing at the skeleton flash is set to face the camera's look
direction at once (0x459ff0 resets the Mover ramps; the port sets `yaw`); the plane sprites get a GL polygon offset
instead of the original's 0.1-unit lift only (depth writes are off in both). Message 1509 (explosion at an instance
vector) is still not handled by the port's message switch.

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
