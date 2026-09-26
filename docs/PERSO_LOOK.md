# PERSO_LOOK.md — look-around (action 7, Perso state 3, camera mode 0x200) and message 30 (LockMove)

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`). Every claim has an address; floats are read from
the exe's `.rdata` (pefile). **Certain** = read instruction by instruction; **derived** = follows from the code but was not replayed
in the original. Nothing here has been traced live.

Related: PERSO_MOVE.md §3.4 (the input actions), PERSO_FRAME.md §2 (order of the Perso update, frame steps), CAMERA.md §3.1 (the camera
controller `0x459090`) and §4 (`SetMode`), CAMERA_SCRIPT.md §4.4 (mode 0x200 in short), PERSO_DUCK.md (ducking still runs in state 3),
BOMB_CARRY.md §1.5/§1.6 (state 6 and the drop `0x463c90`), PERSO_DEATH.md §1.2 (message 30).

Notation: `p` = Perso, `M` = Mover `p+0x388`, `C` = CamMgr `[0x4c737c]`, `L` = the mode-0x200 parameter block `C+0x540`,
`idx` = `C+0x138` (mode index, `mode = 1 << idx`), `dt` = `p+0x2f8`.

## 0. Summary

* **Key**: action **7** (default **Enter**), on **release** (`0x467440(7)`), a toggle. Joystick: one of the buttons (`0x44fed0`).
* **Entry** (`0x44b980`): Perso state 0 without an attack running (`+0x5b4 == 0`), **or** state 6 in carry sub-state 2 (`+0x58c == 2`);
  on the ground (`+0x22c`); the camera in the **follow mode** (`idx == 0`). Otherwise SoundFx **9** ("can't"). Refused therefore in
  the air, during an attack/peck/charge run, while climbing (4), in a scripted action (5), on a rocket (8), in the race (1), dead (2),
  during a bomb pick-up or throw, and under every script camera (fixed 2/4, rail 8, side view 0x20, cinematic 0x80).
  Ducking, a hit animation, a hard-landing lock and even a running special attack do **not** refuse it.
* **State 3**: `SetState(3)` (drops a carried bomb) and the idle count reset `0x464620`. Per frame Perso_Move **without input**
  (`0x44bb20(0)`: the Mover runs out in 0.1 s, no jump, gravity/knockback/platforms still act), collision as usual, logical anim 0.
  Woody is **faded out** (`+0x6c = 1.0`, frame step 24 `0x44b4a0`) — no model, no shadow, no outline. The HUD stays.
* **Camera** (`0x459050`, then mode **0x200**, hard cut): eye at the feet + **0.9 × body height** (193 → **173.7**; ducked 61 → **54.9**),
  no forward offset. Yaw starts at Woody's facing, pitch 0. Direction keys / mouse turn the view: **56.25°/s** per key
  (5 counts × π/16 rad per count per second); **yaw unlimited**, **pitch ±72°** (±1.2566). Right/left turn right/left; the
  **forward key looks down and the back key up** (the keys are worth +5/−5 "mouse counts" and a mouse moved forward gives negative Y).
  Woody turns with the view (his facing = the view's horizontal direction, one frame late), so he faces where you looked when it ends.
* **End**: action 7 released again, **or** automatically as soon as the camera is no longer in mode 0x200 (a script camera, the
  teleport camera reset, the free debug camera). The Perso goes back to the **previous** state (`+0x220`, 0 or 6) without SetState, is
  made visible again (`+0x268`), and the camera cuts to the follow camera behind him (`SetMode(0, 0)`), unless the new state is 5.
* **The look-around bug with a bomb**: in state 6 the entry drops the bomb (SetState(3) → `0x463c90`), but the way back restores
  **state 6** — Woody walks on with the carrying animations and empty hands, cannot peck, pick up or special-attack, until the next
  attack press throws the non-existent bomb (0.933 s ground throw with a move lock) and ends state 6.
* **Message 30** `[perso, cs]` (`0x44cde9`): LockMove(cs × 0.01 s) (the longer of old and new) and logical animation 1
  (idle, prio 6500). No state test. **No level script sends it.**

## 1. The key: `0x44b980` (certain)

Called every Perso frame from `0x44b7a8`, in every state (only `+0x690`, the frozen Perso of camera mode 4, skips it), after the
attack controller `0x457a50`, the trigger `0x44ba70` and ducking `0x465b10`, before the special attack `0x458bf0`.

```c
void Perso_LookKey(Perso *p)                                       /* 0x44b980 */
{
    if (p->state /*+0x21c*/ != 4) p->climbSub /*+0x50c*/ = 0;       /* housekeeping, unrelated */
    bool forced = p->state == 3 && C->idx /*+0x138*/ != 9;           /* 0x44b99c..0x44b9af */
    if (!Released(7) /*0x467440*/ && !forced) return;
    bool enter = false, fail = true;
    switch (p->state) {
    case 0: enter = p->atk /*+0x5b4*/ == 0; break;                  /* 0x44ba05 */
    case 3: p->state = p->prevState /*+0x220*/;                      /* 0x44c9f0: NOT SetState */
            p->show /*+0x268*/ = 1; fail = false; break;             /* 0x44b9f7 */
    case 6: enter = p->carrySub /*+0x58c*/ == 2; break;             /* 0x44b9e5 */
    }
    if (p->onGround /*0x44bcf0 = +0x22c*/ && enter && C->idx == 0) {
        IdleReset(p);                                               /* 0x464620: +0x230 = 0, +0x52c = 0 (zzz bubble off) */
        SetState(p, 3);                                             /* 0x44c980 */
        return;
    }
    if (fail && App->state /*[[0x4c2d00]]*/ != 0) SoundFx(9, 0);    /* 0x44ba5f: "can't", 2D; not on the title (App state 0) */
}
```

`SetState` (`0x44c980`, PERSO_FRAME.md 2.2): drops a held bomb when leaving 6 (`0x44c9ad`), `+0x220 = old state`, clears `+0x50c`,
`+0x5f0` (target), `+0x5b4` (attack), `+0x5cd` (attack displacement), `+0x6ac`.

Notes:
* The forced exit is tested **before** the key: in state 3 with the camera out of mode 0x200 the Perso leaves state 3 in that frame,
  key or not. On the frame of the entry `idx` is still 0 — the camera controller only switches after the Perso update (§3.1) — so the
  test cannot fire on the entry frame.
* A release in state 3 never plays sound 9 and never re-enters (`bl = 0`).
* State 1 (race) has no special case: releasing Enter on the board gives sound 9.

## 2. State 3 on the Perso side (certain unless marked)

| what | where | effect |
|---|---|---|
| movement | `0x44b8a3` → `0x44bb20(0)` + `0x4624f0` (jump table `0x44b950`, states 2 and 3) | Perso_Move without input: the Mover gets no stick (ramp 2 → 3, runs out in 0.1 s), the Jumper gets no jump; gravity, the knockback ramp and platform carrying still act (the Mover's slope slide presumably too: `+0x238` is not set, so nothing zeroes the walk vector); MoveCollide as usual (`+0x118` body height, so ducking lowers the eye) |
| attack | `0x457330` wants state 0 (`0x4573ad`); `0x44ba70` picks up bombs only in state 0 (`0x44badd`) | no peck, no charge run, no pick-up, no climbing start from an attack |
| ducking | `0x465b10` has no state test | X still ducks; the body height 61 moves the eye to 54.9 |
| special attack | `0x458bf0` wants state 0 | refused, sound 9 |
| animation | `0x463e60` → `0x463e77` (table `0x463f14`) | request logical anim **0** every frame; the idle variations `0x464500` do not run (the count neither grows nor resets) |
| visibility | frame step 24 `0x44b4a0` (PERSO_FRAME.md §1), runs if `idx != 8` | state 3 ⇒ `+0x100 = 100.0`, `0x44e7f0(1.0, 1)` = fade target and fade **1.0** at once (instance `+0xfc`, `+0x6c`); `+0x268` set ⇒ clear it, `0x44e7f0(0, 1)` = visible at once |
| hits | `0x44ca00` does not change state 3 | damage and knockback as usual (invisible hit animation); the view stays |
| HUD | frame step 29 hides the HUD only for `idx == 2` (mode 4) | the HUD stays on |
| volumes / events | unchanged | he still presses collision nodes and triggers volumes (he can hardly move, though) |

Frame step 24 runs **after** the render (steps 19–22). So the fade follows the state one frame late: in the entry frame the eye camera
already sits inside a visible Woody, and in the exit frame the follow camera sees an invisible one (derived; a one-frame artefact).

The model scale: the eye uses `0x4624c0` = `+0x118 · inst+0x54` (body height × scale).

## 3. The camera: mode 0x200 (certain)

### 3.1 The controller `0x459090` (CAMERA.md §3.1)

Runs after the Perso update each frame. On a change of `p->state` (compared with `ctl+8`):

* **new state 3** → `0x459050`:
  ```c
  Look_Init(p, &C->L, 1);            /* 0x44c080, below */
  C->L.dx = C->L.dy = 0;             /* +0x28, +0x2c */
  C->CutMode(2);                     /* 0x41f9f0(2): hard cut */
  C->SetMode(9, 0);                  /* 0x41f410 -> 0x41e6c0: copies the state into sub-camera C+0x12c, params = C+0x540, 0x425b60
                                        (L+0x90 = 1), cut again */
  ```
* **old state 3, new state not 5** → `0x45910e`: `0x41f9f0(2)`, `C+0x368 = 0` (follow block `+0x30`, slerp start), `SetMode(0, 0)` =
  follow camera, reset **behind** the Perso (100 pre-simulation steps, CAMERA.md 3.8). Into state 5 (a scripted action) nothing happens:
  that action brings its own camera.

Then, because `SetMode` has already written `idx = 9`, the **index-9 input block `0x459346` runs in the entry frame too**:

```c
L->dx = mouse ? mouse->vt[2]() : 0;  L->dy = mouse ? mouse->vt[3]() : 0;   /* [0x5e6190]: DirectInput mouse (0x467bb5), relative X/Y */
if      (Held(1)) L->dx = ftol(Value(1) * 5.0f);       /* right,   value +1 -> +5  (0x4a9884) */
else if (Held(0)) L->dx = ftol(fabs(Value(0)) * -5.0f);/* left,    value -1 -> -5  (0x4ab2c8) */
if      (Held(3)) L->dy = ftol(Value(3) * -5.0f);      /* back,    value +1 -> -5 */
else if (Held(2)) L->dy = ftol(fabs(Value(2)) * 5.0f); /* forward, value -1 -> +5 */
vec3 d = { -L->R[1][0], 0, -L->R[1][2] };              /* L+0x3c, L+0x44: the view matrix of the LAST update */
normalize_xz(d); if (len(d) < 0.01f) d.x = 1.0f;       /* 0x4a94f8 */
M->dir34 = M->velDir /*+0x1c*/ = M->dir /*+0x10*/ = d; /* he turns with the view */
Look_Init(p, L, 0);                                    /* 0x44c080(.., 0): eye only */
```

The Mover write uses the matrix of the previous update, so Woody's facing trails the view by one frame. In the entry frame that
matrix is left over from the previous look-around (all zero the first time ⇒ `(1, 0, 0)`): for one frame the Perso's facing is
wrong. He is invisible then and the next frame repairs it, so nothing shows (derived).

### 3.2 `0x44c080(p, L, init)` — eye and start values

```c
L->eye /*+0*/ = *p->vt[34]();                          /* +0x1f4, or +0x544 on a moving platform */
L->eye.y += 0x4624c0(p) * 0.9f;                        /* body height x scale x 0.9 (0x4a94b8) */
if (init) {
    L->yaw /*+0x78*/ = L->pitch /*+0x7c*/ = 0;  L->fwdOff /*+0x24*/ = 0;
    L->pitchMax /*+0x80*/ = 1.2566371f;  L->pitchMin /*+0x84*/ = -1.2566371f;    /* 72 deg */
    L->yawMax /*+0x88*/ = -1.0f;  L->yawMin /*+0x8c*/ = 1.0f;                    /* see below: no yaw limit */
    L->base /*+0x54, 9 floats*/ = p->rot /*+0x28*/;                              /* the Perso's rotation now */
}
```

### 3.3 The update `0x425b80` (sub-camera `C+0x12c`, every frame in mode 0x200)

```c
if (L->armed /*+0x90*/) {                                  /* 1 from 0x425b60, always 1 afterwards */
    int d = clamp(L->dx, -64, 64);
    if (d) { float a = min(abs(d) * dt * (float)M_PI/16 /*0x4aa1e4*/, (float)M_PI/10 /*0x4aa1e0*/);  L->yaw   += d < 0 ? a : -a; }
    d = clamp(L->dy, -64, 64);
    if (d) { float a = min(abs(d) * dt * (float)M_PI/16, (float)M_PI/10);                          L->pitch += d < 0 ? a : -a; }
}
L->armed = 1;
if (L->pitch > L->pitchMax && L->pitchMax > 0) L->pitch = L->pitchMax;    /* 0x425cb7 */
if (L->pitch < L->pitchMin && L->pitchMin < 0) L->pitch = L->pitchMin;    /* 0x425cf9 */
if (L->yaw   > L->yawMax   && L->yawMax   > 0) L->yaw   = L->yawMax;      /* yawMax = -1: never */
if (L->yaw   < L->yawMin   && L->yawMin   < 0) L->yaw   = L->yawMin;      /* yawMin = +1: never */
L->R /*+0x30*/ = Mul(RotX(L->pitch) /*0x437940*/, L->base);               /* 0x440b40 (+ Gram-Schmidt) */
L->R = Mul(L->R, RotY(L->yaw) /*0x437970*/);
state.R = columns (-R.row0, -R.row2, -R.row1);                          /* camera right, down, forward (y-down camera, CAMERA.md §1) */
vec3 h = normalize_xz(-R.row1);
state.pos = L->eye + h * L->fwdOff;                                       /* fwdOff = 0: the eye itself */
L->target /*+0xc*/ = state.pos + h * 1000.0f;  L->lookOfs /*+0x18*/ = 0;   /* 0x4aa188: only for transitions (there are none: cuts) */
state.T = -pos . R;                                                       /* 0x425f24.. */
```

Rows of the Perso matrix: row 1 = −facing (the 3ds Max model looks along −Y), row 2 = up. `RotX(pitch) · base` turns the local
forward towards up (positive pitch = **up**); `· RotY(yaw)` on the right turns the result about the **world** y axis. In the port's
convention (facing angle θ, forward = (sin θ, 0, cos θ), +x on the left): view yaw = θ₀ + yaw, view pitch = pitch.

The `(-1, 1)` limit pair on the yaw fails both clamp guards (`max > 0`, `min < 0`), so the yaw is free: a full turn is possible.

| input | count per frame | turn | rate |
|---|---|---|---|
| right key (action 1) | +5 | yaw −, view turns right | 5·π/16 = 0.98 rad/s = **56.25°/s** |
| left key (0) | −5 | yaw +, left | 56.25°/s |
| forward key (2) | +5 on Y | pitch −, **looks down** | 56.25°/s, stops at −72° after 1.28 s |
| back key (3) | −5 on Y | pitch +, **looks up** | 56.25°/s, stops at +72° |
| mouse | its counts, clamped ±64 | right / down on positive counts | counts · dt · π/16 per frame, at most π/10 per frame |

The mouse term multiplies counts **per frame** by `dt`, so the mouse gets slower as the frame rate goes up; the π/10 cap only bites
below 40 fps at the ±64 limit. A key held overrides the mouse on that axis.

No transition (entry and exit are cuts), no shake (0x41fbd0 only runs for modes 1, 2, 4, 8, 0x10, 0x20), no letterbox, the normal
projection (zoom 1.2, vfov 83.97°). There is no collision for the eye: it is inside Woody's body, which is inside the level anyway.

## 4. Ways out of state 3 (certain unless marked)

| how | path | afterwards |
|---|---|---|
| action 7 released | `0x44b9f0` | previous state (0 or 6), visible next frame, camera cut behind him |
| the camera leaves mode 0x200 | `0x44b99c` forced exit, next Perso frame | the same. Since the exit then also runs `0x45910e`, a **script camera** that fired while he was looking (510/520/540/1088...) is overwritten by the follow camera one frame later (derived) |
| teleport (message 26) | `0x458f90` sets `SetMode(0, 0)`, then the forced exit | as above |
| death (`Kill`, SetState(2)) | `0x44c110` | no `+0x268`: he **stays faded out** through the whole death until Reset `0x44ab20` (`0x44ad6a`: `0x44e7f0(0, 1)`) at the respawn (derived) |
| scripted action (message 1040, SetState(5)) | `0x44dda0` | no `+0x268`, and `0x45910e` does not run for state 5: invisible through the action (derived; hard to reach, a door wants a peck) |

## 5. The look-around bug with a bomb (certain in the code, not replayed)

1. Carrying (state 6, sub 2), Enter released on the ground: `SetState(3)` → `0x463c90` lets go of the bomb (speed 0, straight down,
   owner cleared, its fuse runs again) and clears `+0x590`/`+0x594`. `+0x58c` stays 2.
2. Enter released again: `0x44c9f0` writes `state = +0x220 = 6` **without** SetState. The safety net `0x44b8c4` (`bomb && state != 6`)
   has nothing to drop.
3. State 6 with empty hands: `0x4646b0` plays the carry set (0x40 standing, 0x41/0x42 walking, the air set 0x47..0x4c), the
   hand-hold part A of `0x463530` is skipped (`+0x594 == 0`), part B keeps running. No peck, no pick-up, no special attack
   (all want state 0). Ducking uses the bomb set 0x4e/0x4f/0x50 (`state == 6` test). A hit plays **no** hit animation
   (`0x464b84`: state 6 without `+0x594` returns before any request). The look-around itself works again (sub 2).
4. The next attack press: sub 2 → 3 (the room test is a stub) → 4 with anim 0x43 (0.933 s) and LockMove, or in the air 5 → 6 with 0x44
   (0.6 s); the release point in part A needs the bomb, so nothing flies; after `bt` the sub-state calls `SetState(0)` — normal again.

## 6. Message 30 `[perso, cs]` = LockMove (certain)

`0x44cda0` (Perso message handler): id `0x1e` → `0x44cde9`:
```c
LockMove(p, (float)msg->arg1 * 0.01f /*0x4aa0ac*/, 0);   /* 0x44cce0: +0x238 = max(+0x238, t) */
p->anim->vt[2](1);                                        /* request logical anim 1: .ins 0 (idle), prio 6500, speed 3 */
```
No state test; it works dead or alive. While `+0x238 > 0` Perso_Move has no input and zeroes the horizontal displacement
(PERSO_DUCK.md 2.2). A scan of all 28 level scripts (every `SEND` whose first pushed value is 30) finds **no** use; neither is
message 550 (camera mode 0x200 from a script) used.

## 7. Port (`src/player.c`, `src/main_engine.c`)

* Keys: **Enter** (the original's default) or **V**, on release; direction keys (arrows / WASD) turn the view; the mouse turns it
  while the **right button** is held (the port has no DirectInput mouse; `win.mouse_dx/dy` only count with the right button).
* `look_update()` = `0x44b980` + the fade of `0x44b4a0` (one frame late, as in the original); `Player.look` is state 3,
  `Player.state6` is state 6 itself (so the bug's "state 6 without a bomb" exists), `Player.cam_mode` = `C+0x134` written by the app.
* `player_look_start()` = `0x459050`, `player_look_camera()` = `0x459346` + `0x425b80`, called by `cam_update()` for `g_cam.mode == 0x200`;
  the state-change block of `0x459090` sits in the main loop next to the other camera requests (`g_cam.look_prev`).
* `player_lock()` = message 30 (`case 30` in `on_msg`).
* Deviations: `look_update` runs at the top of the Perso update, before the attack controller (the original runs it after
  `0x457a50`/`0x44ba70`/`0x465b10`: an attack started in the very frame of the release would refuse the entry there); the one-frame
  stale facing of the entry frame (§3.1) is not reproduced; the mouse counts are window pixels.
* Test: `extract/Data W1A` + `WOODY_KEYS="2:RET 3:LEFT:1 4.5:UP:0.8 5.8:DOWN:1.6 8:RET"`, log `WOODY_LOOKLOG=1`.
  Bomb bug: `extract/Data W2A --pos 8225 280 -17322 --yaw 90` + `WOODY_PECKS="0.7 3.0 9.0" WOODY_POSAT="2.5 8150 200 -17350"
  WOODY_KEYS="5:RET 6.5:RET 7:UP:1" WOODY_BOMBLOG=1`.

## 8. Certainty and open points

1. Certain: §1, §2 table, §3.1–3.3, §6 (instruction by instruction). The sign of the pitch (positive = up) rests on the camera
   convention of CAMERA.md §1 (camera column 1 = −row 2 = screen down, so row 2 = world up); the port's screenshots agree.
2. Derived, not replayed: the one-frame fade lag, the stale facing in the entry frame, the invisible death/scripted action after
   leaving state 3 that way, the script camera being overwritten by the forced exit, the whole bomb bug.
3. The mouse object's vtable slots 2/3 are taken to be the relative X/Y of the DirectInput mouse (created at `0x467bb5` with
   `SetCooperativeLevel(6)`); not read in detail.
4. Which joystick button is action 7 by default (`0x44fed0` table) was not looked up.
