# PERSO_STATE7.md — Perso state 7: carried by an object's vector marker (`0x44e140` / `0x44e1c0` / `0x44e1a0`, messages 1044 / 1045)

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`). All addresses are VAs; tables and floats were read
from the exe with a PE reader, the message dispatch with `tools/switchmap.py` (`0x444870`: jump table `0x4455e4`, byte table `0x44569c`).
Naming as in PERSO_FRAME.md / PERSO_MOVE.md: `p` = Perso, `P` = parameter block `p+0x110`, `M` = Mover `p+0x388`, `J` = jumper `p+0x334`,
`A` = anim controller `p+0x494`, `dt` = `p+0x2f8`.

Confidence: **certain** = read instruction by instruction; **derived** = follows from the code and the frame order but was not replayed in
the original. Nothing here was traced live: no level reaches the state (§4), so there is nothing to trace.

Related: PERSO_FRAME.md §2.1 (the state dispatch), PERSO_MOVE.md §4.3 (animations per state), CAMERA.md §3.1 (camera controller
`0x459090`), FORMAT_INS.md (vector markers, `0x42f6b0`), GAMEFLOW.md §8 (game messages 1010..1050).

## 0. Summary

* **What it is**: the Perso stands with his feet on the first point of the first typecode-0 vector marker of an instance and faces along
  that marker. Every frame the position is copied from the (posed, animated, moving) marker; there is no movement of his own, no gravity,
  no collision, no volume test and no animation update. The state names no visual effect: he simply "rides" the object in his idle pose.
* **Entry**: only game message **1044 [inst]** (`0x4451a1` → `0x44e140`). **Exit**: game message **1045 [inst]** (`0x4451c2` → `0x44e1a0`,
  argument ignored), or any other state switch (death, a scripted action 1040, climbing grab, Reset).
* **Nothing uses it**: no SEND in the 28 level scripts has id 1044 or 1045 (every SEND pushes its id as an immediate; §4), and no engine
  code posts either message. State 7 is dead code in the shipped game. Ported anyway (§5), testable with `WOODY_MSGAT`.
* **Correction**: CAMERA_SCRIPT.md §2.5 called `0x459030` "used by Perso state 7 'disappearing'". That is the **death kind 7** (water,
  `Kill(7)` `0x44c308`, PERSO_DEATH.md §1) and message 1020, not Perso state 7; state 7 never touches the camera (§3.4).

## 1. All writers (certain)

### 1.1 Perso `+0x21c` = 7

`SetState 0x44c980(p, s)` is the only writer of `+0x21c` in the Perso class (the direct `mov [.. + 0x21c]` hits in `0x40e169`,
`0x46d96f`, `0x46f0a6`.. are other classes' fields). Its callers push constants 0, 3, 4, 5, 6, 8, 9, or `edi`/`ebx` = 0
(`0x44ac3c` Reset, `0x44cbc7`, `0x465694`/`0x4656e8`/`0x465a34` in the climb state `0x4651d0`, which zeroes `edi` at `0x4651ed`), and
**7 exactly once: `0x44e14c` in `0x44e140`**. `0x44c9f0` (restore `+0x220`) is only called from the look-around key `0x44b9f2` in
state 3, and state 3 is only entered from 0 or 6 (PERSO_LOOK.md), so it never restores a 7.

`0x44e140` has one caller: `0x4451b8` in the game-message handler `0x444870`, case **1044** (switchmap: `msg 1044 -> 0x4451a1`).

### 1.2 Perso `+0x55c` (the followed object)

| address | function | what |
|---|---|---|
| `0x44ac84` | Reset `0x44ab20` (vt[17]) | `+0x55c = 0` (and SetState(0) at `0x44ac3f`) |
| `0x44e150` | `0x44e140` (message 1044) | `+0x55c = inst` |
| `0x44e1a2` | `0x44e1a0` (message 1045) | `+0x55c = 0` |
| `0x44e1e1` | `0x44e1c0` (state 7 frame) | the only reader |

(`0x41ed06` / `0x41f2b0` read `+0x55c` of the **camera manager**, not the Perso: part of the mode-0x200 block `CamMgr+0x540`.)

## 2. Entry and exit

### 2.1 Message 1044 [inst] → `0x44e140(p, inst)` (certain)

```c
case 1044: p->Enter7(level->slots[args[0] & 0xffffff]); break;       /* 0x4451a1: [0x50944c]+0x6c table, no NULL test */

void Perso_Enter7(Perso *p, Instance *inst)                            /* 0x44e140 */
{
    SnapToGround(p);                                                   /* 0x462990: Orient 0x44bf10(0); pos.y += P+0 (43); GetHeight 0x435650;
                                                                          pos.y = [0x53a568]; onGround +0x22c = 1; +0x588 = 0; +0x580 = 1.0;
                                                                          J reset 0x462c90; Orient 0x44bd00; world cell 0x428ce0 -> +0x200 */
    p->follow = inst;                                                  /* +0x55c (0x44e150) */
    SetState(p, 7);                                                    /* 0x44c980: state 6 with a bomb in hand -> drop 0x463c90; +0x220 = old;
                                                                          clears +0x50c, +0x5f0, +0x5b4 (attack), +0x5cd, +0x6ac */
    A->Reset();                                                        /* vt[4] 0x436a40: current = -1, the next start is a restart */
    A->Request(1);                                                     /* vt[2] 0x436b70: logical anim 1 = {0,0,0,0}, prio 6500, speed 3 */
    A->Tick(p->dt);                                                    /* vt[3] 0x436a50: switches to it now (.ins anim 0, the idle pose) */
    A->Reset();                                                        /* current = -1 again */
}
```

No state test at all: it works from any state, including dead (2) and the race (1). With an invalid slot `inst` is NULL and the first
frame crashes in `0x42f6b0` (`[ecx+0xf8]`). Logical anim 1 is record 1 of table `0x4b6180` (the one camera mode 2 also forces,
`0x463ec8`); its priority 6500 outranks everything but that no longer matters, because nothing ticks the controller afterwards (§3.2).

### 2.2 Message 1045 [inst] → `0x44e1a0(p)` (certain)

```c
void Perso_Leave7(Perso *p) { p->follow = NULL; SetState(p, 0); }   /* 0x44e1a0: the argument (the instance) is pushed but unused */
```

Unconditional as well: sent in any other state it would end that state (a scripted action, the rocket, climbing, even death) with a bare
SetState(0). He stays where the marker last put him; `onGround` is still the 1 of the entry snap, the jumper still reset, so the first
state-0 frame runs MoveCollide from there and he falls if the marker held him above the floor (derived).

### 2.3 Other ways out (certain)

Any `SetState(!= 7)` ends it; `+0x55c` then keeps its stale pointer until Reset or the next 1044/1045:
* **death** `Kill` `0x44c110` → state 2 (the Perso still takes hits in state 7, §3.3);
* **scripted action** 1040/1043 `0x44dda0` → state 5 (then state 0 at the end);
* **climbing grab** `0x464ef0` → state 4: it has no state test (OBJECTS.md §1.1, BOMB_CARRY.md §1.2), so attack just pressed while facing a peckable wall
  within 169 grabs the wall out of state 7;
* **Reset** (respawn, `0x44ab20`) → state 0 and `+0x55c = 0`.
Not: the look-around key (needs state 0/6, §3.3), the special attack (needs state 0), the rocket mount `0x465740` (needs state 0),
the attack trigger `0x457330` (needs state 0, `0x4573ad`), the bomb pick-up `0x463430` (state 0 only, `0x44bad5`).

## 3. The frame in state 7

### 3.1 `0x44e1c0` (certain)

Called from `0x44b8b4`, the dispatch of Perso::Update `0x44b530` (table `0x44b950`: slot 7 = `0x44b8ac`), and also when the Perso is
frozen (`+0x690`, `0x44b7ee` → `0x44b8a7`: `state == 7` is the only state that still runs its handler while frozen).

```c
void Perso_State7(Perso *p)                                            /* 0x44e1c0 */
{
    vec3 m[2];                                                         /* uninitialised stack buffer */
    GetVector(p->follow, /*typecode*/0, m, /*n*/0);                    /* 0x42f6b0: vtbl[2](1) poses the instance at its current animation time,
                                                                          then the first marker node (S+0x50 list) with flags>>8 == 0: its two
                                                                          points in world space. The return value is NOT tested. */
    p->pos = m[0];                                                     /* +0x1f4..0x1fc (0x44e1fb) */
    vec3 d = { m[1].x - m[0].x, 0, m[1].z - m[0].z };
    float l = sqrtf(d.x * d.x + d.z * d.z);
    if (l > 0) d *= 1 / l;                                             /* 0x44e245: 0x4a9004 = 0.0, 0x4a900c = 1.0 */
    Mover_SetDir(M, &d);                                               /* 0x459ff0: reset RampB (+0x68), RampC (+0x9c), RampA (+0x34) (0x467110);
                                                                          RampA.dir = (d.x, 0, d.z) normalised, |d| < 0.01 (0x4a94f8) -> x = 1;
                                                                          M+0x10 (facing) and M+0x1c follow it */
}
```

So: position = marker point A, facing = the marker's direction A→B flattened (the instance itself faces −dir, PERSO_FRAME.md §2.4), and
every frame all three Mover ramps are zeroed: no walk speed, no slope slide, no knockback survive. A marker without typecode 0 leaves
`m` uninitialised (garbage position). Because the pose is evaluated inside the call, a marker on an animated node (an enemy's hand, a
moving platform with a trajectory) carries him along the animation.

### 3.2 What the rest of Perso::Update does in state 7 (certain unless marked)

| step | state 7 | where |
|---|---|---|
| pre-steps `0x464ef0`, `0x465e50`, `0x457a50`, `0x44ba70`, `0x465b10`, `0x44b980`, `0x458bf0` | run (unless frozen) | `0x44b75d..0x44b7b9` |
| climbing grab `0x464ef0` | **active** (no state test): can switch to state 4 | OBJECTS.md §1.1, BOMB_CARRY.md §1.2 |
| `0x465e50` | gated by the timer `+0x6f8 > 0` (`0x465e30`) | `0x465e68` |
| attack controller `0x457a50` | idle: SetState cleared `+0x5b4`, and the trigger `0x457330` wants state 0 (`0x4573ad`) | |
| bomb pick-up `0x463430` | not (state 0 only) | `0x44bad5` |
| ducking `0x465b10` | runs (state ≠ 2, `+0x5b4 == 0`, onGround = 1 from the snap): sub-state, LockMove(dt), body height 61; no visible anim (below) | PERSO_DUCK.md §1.6 |
| look-around `0x44b980` | refused (state not 0/6) → SoundFx 9 | `0x44b9ca` |
| special attack `0x458bf0` | refused (state ≠ 0, `0x458c30`) → SoundFx 9 | |
| side view `0x459c70` | runs if `+0x4ec`, fills the camera block; the plane projection `0x459eb0` does **not** (only called from Perso_Move `0x44bcd8`) | |
| `Perso_MoveCollide 0x4624f0` | **skipped** (`bl = 0` / `[esp+0xb] = 0` at `0x44b8ac`): no ground probe, walls, platforms, ceiling; `+0x22c` keeps the 1 of the snap | `0x44b8b9` → `0x44b85e` |
| volume test `0x462760` | skipped (inside `0x4624f0`): no VolumeEnter/Leave/In while carried | |
| crush test `0x462a40` | skipped (`doPost` false) | `0x44b86e` |
| id flag 0x200 (script "on the ground") | set every frame (`+0x22c` = 1) | `0x44b888` |
| ledge sensor `0x44b2e0` | runs (onGround) — only read by the charge run | |
| Orient `0x44bd00` | runs: instance matrix from M+0x10, instance position `+0xc = +0x1f4`, world cell | `0x44b8ea` |
| **animation `0x463e60`** | **nothing**: table `0x463f14` slot 6 (state 7) = `0x463f11` = the function's epilogue, so no request and **no Tick** of A (and B) | `0x463e70` |
| fallen height `+0x35c` (= J+0x28) | `oldPos.y − newPos.y` is added when the marker goes down (`0x44b914`); the fall damage in `0x44b220` only looks at it when J reports a landing (`0x463160 == 6`), and J is idle in state 7 (reset by the entry snap). After the exit J starts in its ground state, which clears it (derived, as the port's `jumper_update`) | `0x44b241` |
| landing ring `0x44af90` | runs (state 7 is not excluded); onGround 1 → alpha stays 0, nothing drawn | PERSO_JUMP.md §5 |

Animation consequence (derived): A was switched to logical anim 1 (.ins 0, the idle loop at speed 3) by the entry Tick. The instance
clock `0x43eee0` keeps running on its own, so the idle loop plays on; but requests made during the state (a duck, a hit animation, the
special) are only queued, never switched to, because the only Tick is in `0x463e60` and state 7 returns before it.

### 3.3 Input, damage (certain)

No input reaches movement: Perso_Move (`0x44bb20`, the only reader of the direction keys and the jump) is not called in state 7. The
keys that still do something are the pre-steps of §3.2: duck (lock + body height), look-around / special (refused with sound 9), attack
while facing a peckable wall (grab → state 4). Hits: `Hit 0x44ca00` has no state-7 test — health, invulnerability, `0x463170(J, 0)`
(jumper to falling), the knockback ramp C and the facing towards the attacker — but the next `0x44e1c0` resets the ramps and the facing,
and the jumper only matters after the exit. Health ≤ 0 → `Kill(3)` (`0x44b220`) → state 2 ends state 7.

### 3.4 Camera, visibility (certain)

Nothing in `0x44e140`, `0x44e1c0` or `0x44e1a0` calls the camera manager. The camera controller `0x459090` only reacts to a state change
into 3 (look start `0x459050`) or out of 3 (`0x459109`: cut + SetMode(0)); into or out of 7 it does nothing, and `0x4591ec` sets
`CamMgr+0x3ac` only for the states 1, 4, 8. So the follow camera simply follows the Perso's position (derived: it chases the marker like a
walking Woody). Entering 7 from state 3 is the "out of 3" case: cut back to the follow camera, but without `+0x268`, so Woody stays faded
out (PERSO_LOOK.md §4). No fade, no letterbox, no visibility change of its own.

## 4. Use in the levels (certain)

`tools/ekodisasm.py` on the 28 `code` files, then for every `SEND n` a backward stack walk over the pushes and arithmetic to find the
instruction that produced the id: all 1044..1045 counts are **0**, and **every** SEND in the game has an immediate id (no computed id
that could become 1044). For comparison 1040 occurs 354 times, 1020 73 times. The exe has no internal `push 0x414` / `0x415` (game
messages come only from the VM, `0x4013b5`). State 7 is therefore unreachable in the shipped game; it is either a leftover or meant for
a level that was cut (the design fits "Woody carried off by something": a crane, an enemy, a vehicle).

## 5. Port (`src/player.c`, `src/player.h`, `src/main_engine.c`)

* `Player.follow` (+0x55c; non-NULL = state 7) and `follow_nomark` (log once when the instance has no typecode-0 marker).
* `player_follow(p, inst)` = message 1044 / `0x44e140`: `player_ground_snap`, `ground_22c = 1`, then SetState(7) as the port spells it
  (drop a carried bomb, `look = 0` without `look_show`, end script_act / climb / ride, clear the attack), then anim 1 restarted
  (`lanim = -1; anim_request(1); anim_time = 0; lanim = -1`). Refused (logged) for a NULL instance or a dead Perso — the original
  crashes resp. revives him into state 7.
* `follow_update(p)` = `0x44e1c0`, called from `player_update` right after the state-8 block, returning early like the states 5 and 8:
  `game_inst_vector(follow, 0, &A, &d)` (main_engine.c: `ins_pose` + `inst_vector`, i.e. 0x42f6b0 with its vtbl[2](1) pose), `pos = A`,
  `yaw = atan2(d.x, d.z)` (`|d| < 0.01` → +x), `move_dir` = facing, speed / ramp / slide / push zeroed, `vel = 0`,
  `player_apply_transform`; then `perso_mask200(.., keep)`. No move_collide, no volumes, no animation request, no jumper.
  The early-return guard `p->follow && (dead / script_act / ride / climb_sub) → follow = NULL` stands for "another SetState ended 7".
* Pre-steps kept as in the original: `duck_update` (anim_owned) also runs in state 7; `player_state_free` answers 0 (special attack →
  sound 9, message 1042 "busy", rocket mount refused); the look-around entry refuses (sound 9).
* `player_follow_end(p)` = message 1045 / `0x44e1a0`: leaves state 7 only (the original's unconditional SetState(0) would also end any
  other state; nothing sends it). `player_reset` clears `follow` (`0x44ac84`).
* main_engine.c: `case 1044` / `case 1045` in `on_msg`; the side-view plane projection is skipped while `follow` is set (0x459eb0 lives
  in Perso_Move only).
* Test: `WOODY_MSGAT="2 1044 282; 6 1045 0"` in W1A (instance 282 = a model-42 enemy, which has a typecode-0 marker) with
  `WOODY_FOLLOWLOG=1` (one line per frame: position, facing). Verified: from 2 s Woody rides the walking enemy (position and facing
  follow its marker, ~100 above the floor, idle pose, follow camera behind him), at 6 s he is let go, falls to the floor
  (jumper 3 → 6) and is back under control. An instance without such a marker (`1044 60`) logs once and leaves him in place.

Not ported (never reachable): the climbing grab out of state 7 (`0x464ef0` runs only inside the port's attack path), the frozen-Perso
case (`+0x690`: the port does not call `player_update` during a cinematic at all), the one-frame order between the marker's animation
tick and the Perso update (the port reads the pose of the previous VM tick), and hits during state 7 queue their animation in the port
as in any other state (the original never switches to it before the exit either).

## 6. Open questions

* Why the state exists: no level, no engine path, no string mentions it. Possibly for a cut level or an early version of the rocket
  ride (state 8 does the same with a seat marker and full rotation).
