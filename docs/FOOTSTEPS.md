# Footsteps: the step effect `0x47cba0` and the landing dust `0x476140`

Status: **both effect functions are decompiled and ported** — docs/PARTICLES.md §2 (`0x47cba0`: the print and the
puffs) and §3 (`0x476140`: the smoke ring). This file keeps the call side: when a step fires and what the ground kind
is. §4 says what the port does; the reconstruction that stood here before (a multiplied image-14 mark 22 units to the
side, three puffs on landing) is gone.

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
| 1 | slippery / ice | slow turn-in ramp in `0x45a850` (0.75 s / 1.0 s instead of 0.25 / 0.1, the walking direction keeps up to 95 % of itself per frame, PERSO_MOVE.md §6.4) — ported; no shipped level has it |
| 2 | dust / sand / snow | footstep kind 3 instead of 2 (`0x464231`), dust cloud on landing (`0x464486`) |

A floor that comes from an instance node (lift, platform, crate) has no ground kind: that applies to
world geometry.

## 3. Landing dust

Jumper state 6 (`landed`, one frame, PERSO_JUMP.md §1.1) in `0x4642f0`: if `P+0x308 == 2`, then
`0x476140(&pos + (0,30,0), &P+0x458, 3, 0.25, 1.5)` follows (`0x4644c6`), and **no** animation. `P+0x458` is the floor
normal (PERSO_MOVE.md §2, field table). `0x476140` is the **smoke ring** (PARTICLES.md §3): 3 is the ring kind (35 white
clouds, image 16, on a ring of radius 100 in the ground plane, alpha 0.1), 0.25 the age the clouds start at and 1.5 their
lifetime. Kind 0 of the same function is the dark ring of a bomb explosion (explosion kind 0) and of a launcher's
muzzle; it is not the rocket's (kind 1).

## 4. What the port does (`src/player.c`, `src/main_engine.c`, `src/hud.c`)

**Trigger.** After `anim_request`, `player_update` checks the fraction of the run cycle (only for logical animation 3
and on the ground) and, on passing 0.38 and 0.9, calls `game_footstep(pos, ground_normal, look_direction, foot, kind)`
with **foot 1 at 0.38 and foot 0 at 0.9** (`0x464244` / `0x464289`); `phase_passed` catches the wraparound, so there
are exactly two steps per cycle, even at low frame rates. The ground kind comes from `ground_type()`, which follows the
material index of the last-hit **world** polygon (`g_ground_mat`, set in `world_ground`) via
`TexFile.materials[i].group` to `TexGroup.flags >> 24`. Landing on kind 2 calls `game_land_dust(pos + (0,30,0),
ground_normal)` = `game_smoke_ring(..., 3, 0.25, 1.5)`.

**Effect** (PARTICLES.md §2): every step (kinds 2 and 3) starts a 0.2 s emitter of 50 faint additive puffs a second
(image 14) beside the foot, drifting back and up over 0.8 s; kind 3 (dust/sand/snow ground) also leaves a **print** for
15 s, 15 units to the side of the Perso position (foot 0 left): image 68 additive and image 69 (a crescent rim) alpha
blended, in the ground plane with +v along the walk direction, size 40 for Woody and 20 for the other characters.
Ground type 2 exists in W1A (groups 34, 37), W2A (1, 11, 12), W2B, W2D, the hubs WWS/KWS/SWS and K1A/K2A/S1A/S2A.

`WOODY_FXLOG=1` logs every step and every ring with position and kind.

## 5. Open points

1. **Ground kind 1 (slippery)**: `0x45a850` is ported (PERSO_MOVE.md §6.4), but no floor polygon of any shipped level has a
   texture group with ground type 1, so it only shows with the test hook `WOODY_ICE=1` (every floor slippery).
2. The print's rim image 69 is drawn with UV set 1 (v flipped) for both feet; whether the art expects a mirrored print
   for the other foot is a question for the data, not the code (the original never looks at the foot flag `+0x2c`).
