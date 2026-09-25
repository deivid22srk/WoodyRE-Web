# Footsteps: the step effect `0x47cba0` and the landing dust `0x476140`

Status: **the call side is known from the disassembly, the two effect functions themselves have not been read.** In the
work environment where this was ported, `game/Woody.exe` was not present, so `0x47cba0` and `0x476140` could not
be decompiled. What the port draws, *when* and *with what*, is therefore split: the trigger and the
arguments come from the disassembly (PERSO_MOVE.md §4.3 and §6.4, PERSO_JUMP.md §4), the visual itself is a
reconstruction using the sprite primitives the port already has. §5 states, per point, what still needs to be measured.

## 1. When does the original fire off a footstep?

In the ground animation function `0x463f40` (Perso animation state, `0x463e60` → table `0x463f14`), branch
Mover phase 2 = "at speed", i.e. during the **run cycle** (logical animation 3, `.ins` animation 2). The
cycle duration is `len3 / max(0.5, clamp(M+0x44 / M+0x48, 0, 1))` (`0x436c20`), so the cycle runs at half
speed at ≤ 50% of max speed — the steps therefore automatically follow the running tempo.

| event | constant | call |
|---|---|---|
| cycle fraction passes **0.38** | `0x4ab278` | `0x47cba0(pos, normal, direction, foot, kind)` |
| cycle fraction passes **0.90** | `0x4a94b8` | same, other foot |

Arguments of `0x47cba0`: position, ground normal, run direction, left/right flag, and the **kind 2 or 3**;
kind 3 if the ground kind `P+0x308 == 2` (`0x464231`), otherwise 2.

The **sound** of a step does not come from here: that's the type-4 events on the root node of the
animation (`0x42f5e0` → `0x43a8f0` → `0x4695f0`, docs/SOUND.md §3, in the port `anim_sounds` in
`src/main_engine.c`). Those are independent of this effect and already worked.

## 2. Ground kind `P+0x308` (`0x4628e0`, reader `0x46295f` = `m_nGroundType`)

On every ground measurement: 0, except when the floor is a **world polygon** (`[0x53a554] == 1`) whose
material field `poly+8` bit 15 is not set. Then the kind is byte 3 of the flag word of the
texture group behind that material (`level+0x5c`, records of 0x24 B, `+0x20` = texture object → `tex+0x47`,
docs/FORMAT_TEX_COL_VIS_LIT.md §1):

| kind | meaning | use in the original |
|---|---|---|
| 0 | normal | – |
| 1 | slippery / ice | slow turn-in ramp in `0x45a850` (0.75 s / 1.0 s instead of 0.25 / 0.1) — **not ported** |
| 2 | dust / sand / snow | footstep kind 3 instead of 2 (`0x464231`), dust cloud on landing (`0x464486`) |

A floor that comes from an instance node (lift, platform, crate) has no ground kind: that applies to
world geometry.

## 3. Landing dust

Jumper state 6 (`landed`, one frame, PERSO_JUMP.md §1.1) in `0x4642f0`: if `P+0x308 == 2`, then
`0x476140(&pos + (0,30,0), &P+0x458, 3, 0.25, 1.5)` follows, and **no** animation. `P+0x458` is the floor normal
(PERSO_MOVE.md §2, field table). The same `0x476140` also creates the shards of a rocket explosion (PROJECTILES.md §5.3: explosion kind 0, "with normal"),
so it's a general particle burst; only the first argument pair (point, normal) and the count 3
can be interpreted with certainty here, `0.25` and `1.5` cannot.

## 4. What the port does (`src/player.c`, `src/main_engine.c`, `src/hud.c`)

**Trigger — taken from the disassembly.** After `anim_request`, `player_update` checks the fraction of the
run cycle (only for logical animation 3 and on the ground) and, on passing 0.38 and 0.9, calls
`game_footstep(pos, ground_normal, look_direction, foot, kind)`; `phase_passed` catches the wraparound, so there are
exactly two steps per cycle, even at low frame rates. The ground kind comes from `ground_type()`,
which follows the material index of the last-hit **world** polygon (`g_ground_mat`, set in `world_ground`)
via `TexFile.materials[i].group` to `TexGroup.flags >> 24`. Landing on kind 2 calls
`game_land_dust(pos + (0,30,0), ground_normal)`, exactly where the original calls `0x476140`.

**Visual — reconstruction.** `game_footstep` places an imprint in the ground plane (`hud_world_decal`, a sprite
without flag bit 0: the quad lies in the plane with the given normal, rotated to the run direction and
mirrored in u for the other foot — flag 0x40, see the sprite flags in PERSO_DEATH.md) next to the Perso position,
22 units to the side:

| | kind 2 (normal ground) | kind 3 (dust/sand/snow) |
|---|---|---|
| imprint | size 30, strength 0.22, 0.6 s | size 36, strength 0.6, 4 s |
| dust | none | one puff (image 14) diagonally backward, 0.45 s |

The imprint stays still for the first half of its life and then fades. There are 48 imprints and 64
dust particles; the oldest imprint is replaced once the ring is full. Landing spits out 3 puffs around it (the count
from the call), 0.5 s, outward and upward.

Two deliberate choices, because the original could not be read here:

1. **Which image.** Unknown. The port uses bank 0 image 14 (the soft puff from the rocket smoke) and
   draws the imprint **multiplicatively**: `dst · (1 − rgb·strength)`. That image is white on black with alpha 1,
   so a plain alpha blend would put a dark *square* on the ground; this way only the cloud shape remains
   as a dark smudge. `WOODY_STEPIMG=<n>` selects a different bank-0 image (it is then loaded as fx slot 10);
   once it's known which image the original uses, a different blend mode probably belongs with it too.
2. **The foot distance of 22 units to the side.** The original only passes a left/right flag; exactly where
   `0x47cba0` places the imprint (and whether it uses the foot node of the skeleton) is not known.

`WOODY_FXLOG=1` logs every step and every landing with position and kind.

## 5. Open points

1. **Decompile `0x47cba0`**: image number(s), size, color, lifetime, blend mode, whether the imprint also
   appears on normal ground (kind 2) and what the exact difference between kind 2 and 3 is, and where the imprint
   ends up relative to the Perso position. Without that, §4 remains a reconstruction.
2. **Decompile `0x476140`**: meaning of `0.25` and `1.5`, the particle itself (image, gravity,
   lifetime), and whether the count 3 is a count per call or per second.
3. **Ground kind 1 (slippery)**: `P+0x308` is now read, but `0x45a850` (slow turn-in ramp on ice) is
   not ported — see TODO.md.
4. **Where are the step effects placed in the levels?** With the exe available, `tools/funcinfo.py 0x47cba0` (callers
   and strings) and `tools/drange.py` are the starting point; `tools/levelparse.py` can enumerate per level which texture groups
   have a ground-type byte ≠ 0, which immediately says in which levels the imprints should be visible.
