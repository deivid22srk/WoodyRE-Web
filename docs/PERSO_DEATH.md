# PERSO_DEATH: teleport (message 26/30), door actions 17/18, death animations and effects, hit, blinking, enemy sparkles

Static analysis of `game/Woody.exe` (image base 0x400000). All addresses are VAs. Naming follows PERSO_MOVE.md
(`P` = Perso, `J` = jumper `P+0x334`, `M` = Mover `P+0x388`, anim controller `P+0x494`, second controller `P+0x498`).
`L(n)` = `0x436b90(n, 0)` = `model.anim[rec[n].sub[0]].duration · (1/4096) / rec[n].speed` (`0x4aa138` = 1/4096).
Effect pool, sprite and line primitive: see PROJECTILES.md §5 (`[0x5e823c]+0xdb8`, 2000 × 80 B; `+0` age, `+4` lifetime,
`+0x4c` callback; sprite `S = [0x5e823c]+0xb00`, `0x470f10(S, flags)`; image ref `0x1000N` = bank 0 image N).

**Corrections to earlier docs**
* `0x478980` is **not a camera shake** but a **comic-strip speech bubble** above the instance (§4.1). Also applies to the hard landing
  in PERSO_JUMP §4 (`0x478980(p,1,2.0,180,50,0)`).
* `0x478660` (kind 7) is a **water splash** (droplets + rings), the model is not hidden (§4.3).
* `0x477e40` (kind 2/9) is the **skeleton effect** (model on/off, 6 skeleton sprites, flicker light) (§4.2).
* `0x477610` (enemy death) is not flying-away particles but **5 sparkles circling above the head** (§7).
* `0x44cf50`: PERSO_MOVE §4.4 has the two branches swapped (§6).
* Dead code: all tests `P+0x21c == 6` in `0x464790` can never be true (state is 2 by then).

Sprite flags `0x470f10` (addition to PROJECTILES §5): bit 0 (1) = billboard facing the camera (`0x4714ea`), without bit 0 and
without 0x20 the quad lies in the plane with normal `S+0x230..0x238` (`0x4717d7`); bit 1 (2) = use own color/alpha
`S+0x214..0x220` (`0x4710bd`, otherwise default color); bit 2 (4) = rotation `S+0x224`; bit 3 (8) = non-additive
(`0x4719b7`); bit 6 (0x40) = apply mirror flags `S+0x22c` (`0x4714b4` → `0x470d80`; value 2 = mirrored).

---

## 1. Message 26 (teleport) and 30 — Perso handler `0x44cda0`

Message record: `+0` id, `+8` arg0, `+0xc` arg1, `+0x10` arg2. Only 26 (`0x44ce11`) and 30 (`0x44cde9`) are
handled here, the rest goes to the base `0x40bf20`. Both return 0.

### 1.1 Message 26 `[arg0 (unused), targetInst, mode]` (`0x44ce11..0x44cf24`)

```c
Instance *t = level->inst[arg1 & 0xffffff];            /* [0x50944c]+0x6c */
int mode   = arg2 & 0xffffff;
t->vtbl[2](1);                                          /* 0x44ce34: refresh the target's pose/matrices */
vec3 pos = t->pos;                                      /* inst+0xc..0x14, NOT P0 of the vector */
if (mode == 1) { /* position only */ }
else if (mode == 2) {                                   /* position + facing */
    vec3 v[2];
    if (Inst_GetMarker(t, 5, v, 0) || Inst_GetMarker(t, 0, v, 0)) {   /* 0x42f6b0: typecode 5, otherwise typecode 0 */
        vec3 d = v[1] - v[0];                           /* NOT normalized, y included */
        Mover_SetFacing(&P->M, &d);                     /* 0x459ff0 (0x44cefc) */
    }
} else { warn("Unknow teleportation mode !"); return 0; }   /* 0x44ce5e: nothing further happens */
Volumes_LeaveAll(P->id /*+4*/);                         /* 0x443ff0: leave event for every volume P is in (0x444550 + 0x443d20) */
Perso_Teleport(P, &pos);                                /* 0x44a650, see below */
CamFollow_Reset([0x5e5a74]);                            /* 0x458f90, see below */
```

Scripts only use `26 [0, otherDoor, 1]` (mode 1): the Perso arrives at the **instance position** of the other door; the
following `1040 [otherDoor, 18]` then places him at P0 of the door vector (§2).

**`0x44a650(P, &pos)`**: if `P+0x21c == 5` (a scripted action is running) ⇒ **nothing** (volumes-leave and camera-reset have
already/still been done). Otherwise: `P.pos (+0x1f4) = pos`; `0x462990(P)` (ground snap); `animctl->vtbl[4]()` (Reset, current = −1) and
likewise for `P+0x498` if it exists. No change to health, checkpoint/spawn, invulnerability or state.

**`0x462990(P)` ground snap**: `0x44bf10(P, 0)`; `pos.y += P+0x110 (43)`; `0x435650(&pos, P+0x200, 1)` (GetHeight) ⇒
`pos.y = [0x53a568]` (also on NotFound: then the search height itself is stored there, i.e. feet + 43 — measured live at the end
of the W2B boss intro, GetHeight type 0, y −3194 → −3151; the next frame steps him up 10 onto the floor); `onGround (+0x22c) = 1`; `+0x588 = 0`; `+0x580 = 1.0`; `0x462c90(J)` (jumper reset);
`0x44bd00(P)` (matrix/orientation); `P+0x200 = 0x428ce0(level, pos + (0,43,0))` (world cell).

**`0x458f90(F)` camera reset** (`F = [0x5e5a74]`: `F+0` CamMgr, `F+4` Perso): `0x41df70(CamMgr)` (clear transition/letterbox/
rail state: `+0x108..0x10a = 0`, `+0x690 = 0`, `+0x138 = −1`, `+0x134 = 0`, `+0x66c &= ~4`); follow block
`CamMgr+0x338`: `+0x18 = P->vtbl[34]()` (position ptr), `+0x28 = −1`, `+0x1c..0x24 = M facing` (`0x445780`),
`+0x74 = 1`, `+0x30 = 0`; `0x41f9f0(2)` (**hard cut**); `0x41f410(0, 0)` (follow camera); `+0x70 &= ~1`;
`F+0x18 = 0`, `F+8 = P->state`, `F+0x20 = 0`. So: the camera jumps directly behind the new position/facing.

### 1.2 Message 30 `[_, cs]` (`0x44cde9`) — not a teleport
`0x44cce0(P, arg1 · 0.01 (0x4aa0ac), 0)` = LockMove: `P+0x238 = max(P+0x238, t)` (no input/movement, PERSO_JUMP §0)
and `animctl->vtbl[2](1)` (request idle, logical record 1, prio 6500). "Stand still for t seconds." No state test; no level script
sends it (scan of all 28 scripts). Ported as `player_lock()` (PERSO_LOOK.md §6).

### 1.3 `0x42f6b0(inst, typecode, vec3 *out, n)` — marker vector of an instance
Walks the model's **marker list** (`S+0x50` count, `S+0x54` node indices, 1-based; node kind 0x20, FORMAT_INS),
takes the n-th node with `(node.flags >> 8) == typecode` (`0x42f6ed`), calls `inst->vtbl[2](1)` and transforms the
`node+0x14` points (first point `node+0x18`, point array `S+0x20`, 0x28 B/point) with that node's world matrix
(`[0x509adc]+0xa0`, index `node + inst+0x5c − 1`, 0x30 B) into `out[i]`. Returns 0 if there is none (out left untouched).
The "door vector" = **marker node with type_code 5**: `P0 = out[0]` (marker start), `P1 = out[1]` (marker end).

---

## 2. Actions 17/18 in `0x44dda0` and state 5 `0x44db50`

Message 1040 `[inst, action]` (`0x445230`): `0x42f6b0(inst, 5, v, 0)` (**without** falling back to typecode 0 and without
testing the result), then `0x44dda0(P, action, v, 0)`. (Variant `0x4451e4` = 1043 with a target instance as the 3rd argument.)

Start (`0x44ddd8..0x44dfd6`), addition to CINEMATIC §6:
* refused (returns 0) if `state == 2`.
* `fadeOut (+0x560) = (action == 17)`, `fadeIn (+0x561) = (action == 18)`, except when subtype `(P+0x104 & 0x3e0)` is 0x80/0xa0
  (race characters 4/5) (`0x44dde7..0x44de19`).
* 17/18: first `0x443ff0(P->id)` (leave all volumes, `0x44de36`). `+0x4ec = 0` (**the side-view flag**,
  CAMERA_SCRIPT §4.2: the player is no longer bound to the plane), `0x44dd70` (clear attack target).
* `logical = 0x463e30(action)`: 17 → record **24** `(17,−1,−1,−1)`, 18 → record **25** `(18,−1,−1,−1)`, both prio 6000,
  speed 3.0, restart 1. `total = remain (+0x538/+0x53c) = L(logical)` = 18432/4096/3 = **1.5 s** for both.
  `animctl->vtbl[4]()`, `vtbl[2](logical)`; `+0x540 = action`; state 5.
* `P.pos = P0`; `onGround = 1`; `d = (P1.x−P0.x, 0, P1.z−P0.z)` normalized (only if length > 0);
  `0x459ff0(M, &d)`; `+0x550 = 0`; `0x44bd00(P)`. **No ground snap at the start**: y = P0.y.
* 17/18 have no camera track ⇒ camera stays the follow camera.

Per frame (`0x44dbdf..0x44dcf3`, branch `17 ≤ action ≤ 18`):
```c
if (remain < 0.6f /*0x4a9650*/ && P->fadeOut) { P->fadeOut = 0; App_FadeOut(app, 0.5f); }   /* 0x401480 */
if (P->fadeIn)                               { P->fadeIn  = 0; App_FadeIn (app, 0.5f); }   /* 0x401440, first frame */
remain -= dt;
if (remain <= 0) {
    Perso_SetState(P, 0);
    if (action == 18) {
        f = Mover_GetFacing(M);  f = -f;  Mover_SetFacing(M, &f);        /* 0x445780, ×−1 (0x4a9500), 0x459ff0 */
        Perso_GroundSnap(P);                                              /* 0x462990 */
        animctl->vtbl[2](1);                                              /* idle */
        if (!P->planeMode /*+0x4ec*/) Perso_EndScripted(P);              /* 0x44e5a0, see below */
    }                                                                     /* after 17: state 0 only, nothing else */
}
```
* **The Perso's position does not change during 17/18** (`0x44e290` is not called in this branch, `+0x550` stays 0).
  The visible movement lies entirely in the position track of node 0 of the Woody model (model space, y = depth):
  anim 17: `(0,−3,52) → (−3.7,−264,66)` (walks ±261 into the door); anim 18: `(−0.25,−454,60) → (0,−3,52)` with the root
  rotated 180° (start quat ≈ (0.13,−0.43,−0.89,−0.01), end = idle quat rotated 180° about the vertical axis). That's why at 18
  the Perso stands at P0 **facing the door (P0→P1)**, the model starts ±450 "in" the door, walks backward-facing toward P0,
  and only on the last frame is `facing = −facing` set so idle connects seamlessly.
* Timeline 17: t = 0 start, **t = 0.9 s fade-out (0.5 s)**, black at 1.4 s, end at 1.5 s ⇒ state 0. The one-shot 17 stays on
  the last frame (prio 6000, no loop ⇒ idle can't replace it) until message 26 resets the controller via `0x44a650`. Timeline 18:
  t = 0 **fade-in (0.5 s)**, end 1.5 s.
* Order in the scripts: `1040 [door,17]` → `DELAY 150` → `26 [0, otherDoor, 1]` (state is 0 by then, so not refused) →
  `1040 [otherDoor,18]`. The DELAY is exactly as long as the animation (1.5 s), so whether the teleport still falls within
  state 5 depends on the frame boundary; if it does, action 18 is what moves the player (to P0) and the camera cuts at the old position.

### 2.1 The camera through the whole door sequence

There are only two camera actions in the whole sequence, in message 26 and at the **end** of action 18 — actions 17/18
themselves leave the camera alone:

| moment | call | effect |
|---|---|---|
| message 26 (teleport) | `0x458f90` (§1.1), **outside** the state-5 test | hard cut: `0x41f9f0(2)` + `SetMode(0, 0)` → `0x41e450` → `0x4247f0` places the follow camera **at that moment**: `P = pos − look + (0,100,0)`, then 1 step of 0.1 s and 100 of 0.04 s in behind mode |
| end of action 18, after `facing = −facing` | `0x44e5a0`, only if `+0x4ec == 0` | `0x41f9d0(0.5)` + `0x41f9f0(1)` + `SetMode(0, 0)`: follow camera behind the **new** facing direction, with a 0.5 s travelling shot |

`0x44e5a0` itself: `p->targetPtr (+0x18) = &P.pos`, `p+0x28 = −1`, `p->dir (+0x1c) = M facing`, `p+0x30 = 0`
(the `pos.y += 43` / `−= 43` around it is a no-op), then the three CamMgr calls above.

Without that last reset, the camera would stand **in front of** the player after exiting: the camera stood behind the facing
direction "into the door," which rotates 180° in the last frame. The `+0x4ec` test exists for doors that lead into a
side-view section: the script turns that section (message 1088) back on in the same frame, and then mode 0x20 stays in effect.

**Order within one script tick, and where the port differs.** The script sends 26 and 1040/18 back to back
(`out/w1a_code.txt` 2297..2313), with no DELAY in between. Message 26 carries mode 1 in the door scripts: only a
position, **no facing**; and `0x44a650` moves nothing as long as state 5 is still running (the script's `DELAY 150`
is exactly as long as animation 17, so that happens fairly often). Only action 18 fixes where he stands and which
way he's facing (`pos = P0` of the door vector, `0x44dec4`). If the follow camera is placed the way `0x4247f0`
does **during** message 26, it ends up behind the facing direction of the *previous* door — for the W1A door
336 → 334 that's off by 135° and the player stares at a wall for a second and a half. The port therefore does the
**mode switch** in its proper place in the message stream (cut + `SetMode(0, 0)` in the handler of message 26) but
lets the follow camera place itself only in the camera update **after** the script tick: `SetMode(0, 0)` only clears
`cam_init`, and `player_camera` then places it at the position and facing that action 18 gives it. The screen is
black at that point (fade-out done at the end of 17, fade-in 0.5 s in 18), so there's nothing to see; the difference
from the original is one frame at the start of action 18.

**The mode switch itself must not be deferred.** Everything else that touches the camera within the same tick must
win, and that's exactly what comes right after message 26: `1040 [door, 18]` sets mode 0x80 on the animation's
camera track (CAMERA_SCRIPT §4.3), and an area gate in a hub sends `26 [0, marker, 2]; 580 [cam, 2]; 520 [cam, 0, target]` —
the fixed camera that opens the gate (CAMERA_SCRIPT §1.3, WWS object 258). A cut that does `SetMode(0, 0)` one
frame later anyway breaks both: the gate cutscene gets skipped (the player instantly gets the follow camera and
control back) and the door camera only stands for one frame.
(Not dynamically verified: whether the original gives the same image on that 135° — behind mode is off in state 5
(`CAMERA.md` §3, `p+0x74`), so the camera doesn't recenter there on its own either.)

---

## 3. Death: `Kill(kind)` `0x44c110` (table `0x44c498`) and animation choice `0x464790` (table `0x4649dc`)

State 2 → `0x463e8f`: `P+0x4d8 ? 0x464a00 : 0x464790`. `+0x4d8` is only 1 in the race variant `0x44c4c0` (§3.3).
Kill always sets (`0x44c443`): `health = 0`, **`+0x274 = 0` (death clock T)**, `+0x26c = kind`, `+0x4d8 = 0`, `+0x550 = 0`,
state 2, `+0x268 = 1`. `0x464790` first does `T += dt` (`0x464796`) every frame.
Iris: `0x4459c0` state 3 lets the iris close (1.0 s, black ring around the screen center, no brightness fade) at
`T ≥ P+0x288 − 1.0`; after the respawn it opens again over 1.0 s (PERSO_FRAME §4.1).
The model is **never** hidden or faded, except for the on/off flashing on kind 2/9 (§4.2).

Anim controller rule (`0x436a50`): a request replaces the current one if `prio_new ≥ prio_current` or if the current one is
looping (`inst+0xc0 == 1`, slot0 == slot1). Chain index −1 = empty ⇒ **one-shot stays on the last frame**
(INSTANCE.md §2); too-large index ⇒ 0 (`0x436b34`, signed comparison, −1 stays −1).

### 3.1 Per kind

| kind | Kill branch | `+0x288` | Kill specifics | animation (`0x464790`) |
|---|---|---|---|---|
| 1 (script 1020, chasm) | `0x44c297` | 3.5 | speech bubble `0x478980(P, 0, 2.5, 180, 50, 0)`; `+0x240 = L(0x2f)` = **2.267 s no gravity**; `0x463170(J,0)` | `0x464915`: `T ≤ L(0x2f)` ⇒ request **0x2f** every frame; then request **9** (long fall); in the single frame where `T−dt ≤ L < T`: camera `0x41fb50(CamMgr, &(inst.pos + (0,100,0)), P)` (§4.4) |
| 2, 9 (lightning/laser) | `0x44c3ab` | 1.5 | rumble `0x44d1b0`; `+0x288` is set already before the tests; cheat `[0x5d7b8a]` or `+0x270 > 0` ⇒ ignored; `0x477e40(&(pos + (0, P+0x11c, 0)))` (argument unused, §4.2); `+0x240 = L(0x30)` = 1.7 s; **no** `0x463170` | `0x464903`: request **0x30** every frame |
| 3 (health depleted), 8 (fall) | `0x44c230` | 3.0 | cheat `[0x5d7b8b]` or `+0x270 > 0` ⇒ ignored; `0x463170(J,0)` | 3: `0x4647c7` (§3.2); 8: `0x4648df` request **0x2e** |
| 4, 5 | `0x44c26f` | 3.5 | `+0x270 > 0` ⇒ ignored; `0x463170(J,0)` | `0x4647c7` (§3.2) |
| 6 (explosion) | `0x44c2d6` | 2.5 | same | `0x4648f1`: request **0x2b** |
| 7 (water) | `0x44c308` | **0** | splash `0x478660(&(inst.pos + (0,110,0)), speed, 50.0)` with speed = `0x44d170(P)` = `|P+0x204..0x20c| / dt`; camera `0x459030(F, &CamMgr.state.pos)` = `0x41fb50(CamMgr, current camera position, P)` (camera stays put and looks the player down); `+0x5f0 = +0x5b4 = 0`, `+0x5cd = 0`, `animctl->vtbl[4]()`, `0x462c90(J)`, colliders `0x436d10` ×2 | `0x4649bf`: request **0x2a** and **every frame `0x462c90(J)`** ⇒ never falls further |

Kind 7 may override any other running death, kind 1 any other except 1 (`0x44c1e6..0x44c20d`).
In state 2, `0x44bb20(P, 0)` keeps running: no input, knockback plays out, J lets him fall as soon as `+0x240 ≤ 0`.

### 3.2 Kind 3/4/5 (`0x4647c7`): ground or air variant, then lying down
```c
if (T <= dt) {                                   /* first frame (0x4647c7) */
    P->deadOnGround /*+0x284*/ = P->onGround;    /* 0x44bcf0 */
    if (onGround) Request(P->duck694 == 2 ? 0x2c : 0x26);      /* (dead code: state 6 → 0x28) */
    else          Request(0x25);                                /* (dead code: state 6 → 0x27) */
} else if (P->deadOnGround) {
    if (P->duck694 != 2 && T >= L(0x26) /*1.067*/) Request(0x29);
} else {
    if (T >= L(0x25) /*0.733*/) Request(0x29);   /* regardless of whether he already landed */
}
```
Between the first frame and `L`, nothing is requested (the chain runs on into loop anim 30). 0x29 is one-shot ⇒ stays lying down.

### 3.3 Logical records used (`0x4b6180`, 0x1c B/record; `{sub[4], prio, speed, restart}`; duration = dur/4096/speed)

| log. | .ins chain | prio | speed | duration sub[0] | usage |
|---|---|---|---|---|---|
| 9 | 7 → 32 → 33 loop | 1600 | 3 | 0.033 s | kind 1 after hanging (can only replace 0x2f because that's in loop 33 by then) |
| 0x1f | 20 → 0 | 5110 | 3 | 0.467 | hit on the ground |
| 0x20 | 22 → 0 | 5110 | 3 | 0.400 | hit in the air |
| 0x23 | 26 → 25 loop | 5110 | 3 | 0.267 | hit while ducking |
| 0x21 / 0x22 / 0x24 | 68 → 47 / 67 → 47 / 64 → 60 | 5110 | 3 | 0.467 / 0.400 / 0.267 | hit in state 6 (carrying): ground / air / ducking |
| 0x25 | 29 → 30 loop | 6000 | 3 | 0.733 | death 3/4/5 in the air |
| 0x26 | 28 → 30 loop | 6000 | 3 | 1.067 | death 3/4/5 on the ground |
| 0x29 | 31, one-shot | 6000 | 3 | 0.400 | after that: lie down, stays there |
| 0x2a | 27, one-shot | 6000 | 3 | 0.833 | kind 7 (root sinks ±190 in the anim itself) |
| 0x2b | 37, one-shot | 6000 | 3 | 1.867 | kind 6 |
| 0x2c | 36, one-shot | 6000 | 3 | 1.700 | death 3/4/5 ducking, on the ground |
| 0x2e | 35, one-shot | 6000 | 3 | 2.367 | kind 8 |
| 0x2f | 48 → 33 loop | 6000 | 3 | 2.267 | kind 1: hanging/struggling in the air |
| 0x30 | 85, one-shot | 6000 | 3 | 1.700 | kind 2/9 |
| (0x27, 0x28, 0x4d) | 69→30, 70→30, 65→33 | 6000 | 3 | | only via dead `state == 6` branches |

All records restart = 1 except 9 (0).

Race variant (`0x44c4c0` → `+0x4d8 = 1`, `0x464a00`, table `0x464b48`, requests on **both** controllers): kind 1 → 0x75 (A)
and 0x76 (B) + the same camera action when `T` crosses `L(0x75)`; 2 → 0x72; 3 → 0x74; 4/5 → nothing; 6 → 0x73; 7 → 0x77 + `0x462c90(J)`;
8 → 0x71. Kill side: `+0x240 = L(0x72)` resp. `L(0x75)`, kind 7 `+0x288 = 4.0`. Not worked out further.

### 3.4 Respawn `0x445930` — checkpoint, facing, side-view and camera

Called from state 0 of `0x4459c0` (PERSO_FRAME §4.1), 0.25 s after losing a life (`0x44c730`):
```c
Fader(Game+4, 0, 0, 0.1);  Game->state = 0;  Game->timer = 0.1;  [0x4b3354] = 0.2f;
Perso_Respawn(P, 0);                    /* 0x44a810 */
Actors_ResetAll();                      /* 0x40c040: vtbl[28] of all actors */
CamFollow_Reset(Game+8);                /* 0x458f90 (§1.1): 0x41df70, 0x41f9f0(2) hard cut, SetMode(0, 0) = follow camera */
```
`0x44a810(P, save)`: if `P+0x250` (lives) is 0 ⇒ game over (`0x404e10`, or `0x44a6a0` = level restart if
`[0x5e5814]+0x384 & 4`). Otherwise (with `save` = 1 first reading `SavePos.bin`, here 0): **`P.pos = P+0x318`**, `P->vtbl[17]()`
= Reset `0x44ab20`, ground snap `0x462990`. Reset sets, among other things, **`0x459ff0(M, P+0x324)`** (checkpoint facing),
health 3 if it was depleted, `+0x270 = 1.0` (invulnerable), and **`+0x4ec = 0` (`0x44ad22`)**: the plane-lock slot of the
side-view (CAMERA_SCRIPT §4.2) ends on death. Together with the follow camera from `0x458f90` this means: after dying in
a side-view section, Woody simply stands in 3D at the last checkpoint, with the camera behind him. There's no separate
"side-view checkpoint"; the script turns the side-view back on (1088) only when he goes through that section's door again.

The checkpoint itself (`1030 SaveAuto`, `0x445129` → `0x44aa10`): `+0x318 = inst.pos`; `+0x324` = `(P1.x − P0.x, 0, P1.z − P0.z)`
of the marker with typecode 0 (`0x42f6b0(inst, 0, v, 0)`), normalized; without a marker the current facing (`0x445780`);
`+0x330 = 1`, `+0x4e0 = +0x264`. All checkpoint instances of W1B (0x196, 0x200..0x20b, 0x238) have such a marker.

**W1B, issue #39.** The side-view section behind door 392 (→ 409, plane 411, `1088 [411, 2]`) has no checkpoint of its own;
the last one is 0x206 at (−8009, 430, −16808), 575 units before door 392, marker toward the door (−x). The port kept the
plane-lock slot (`g_cam.plane_on`) engaged after the respawn, so that position was projected onto plane x = −11231 every
frame: to (−11231, 140, −16808), out in the void next to the section, where he fell, landed in water (kind 7) and died
again — with no way out. The port now does what `0x44ad22` + `0x458f90` do (`respawn_req` → `plane_release()` + hard cut to
the follow camera, before the plane projection in the same frame) and takes the checkpoint's facing from the marker.

---

## 4. Effects

### 4.1 Speech bubble `0x478980(inst, kind, duration, offY, offX, u8 *alive)` (callback `0x4786f0`)
Record: `+4 = duration`, `+8 = inst`, `+0x10 = offY (180)`, `+0xc = offX (50)`, `+0x28 = alive ptr`, `+0x14.. = images`,
`+0x24 = count`, `+0x2c = side`. Images per kind (`0x478a8c`): 0 → {0x2d}; 1 → {0x33, 0x34}; 2 → {0x2e}; 3 → {0x32};
4 → {0x2f, 0x30, 0x31, 0} (count 4). Kind 0 = death 1, kind 1 = hard landing.
`side = (view.row0 · inst.T + view.row0.w ≥ 0)` (`0x4789e3..0x478a1d`; view = `[[0x509adc]+8]+0x30`, inst.T = `inst+0x60..0x68`), computed once on creation.
```c
u = age / duration;
if (alive ? *alive == 0 : u >= 1) end;
s = alive ? (age < 0.5 ? 2*age : 1)
          : (u < 0.04 ? 25*u : u > 0.96 ? 25*(1-u) : 1);                 /* 0x4aa1cc, 0x4abd58, 0x4abd5c */
d = normalize_xz(cam.pos - inst.T);  R = normalize(d.z, 0, -d.x);  U = (0,1,0);   /* 0x46d320 */
size = 90*s + 30;  h = size/2;
X = side ? -(offX + h) : (offX + h);   Y = offY + h;
pos = inst.T + R*X + U*Y;
sprite(img 0x2c, pos, size, mode 0x12, flags 0x49, mirror = side ? 2 : 0);        /* bubble, non-additive, default color */
img = images[(int)(u * 8) % count];
if (img) sprite(img, pos, size = 55*s + 10, flags 0x49, mirror 0);               /* content */
```
Verified against the disassembly (2026-09-24): constants 0.04/0.96/25/90/30/0.5/55/10/8 check out, `0x46d320` for `d = (dx,0,dz)` gives `R = (dz, 0, −dx)` (= screen right) and `U = (0,1,0)`; mirror value 2 (`0x470d80` case 2) flips u. `size` is `S+0x264`, the half-diagonal. Camera-space x ≥ 0 = right half of the screen ⇒ bubble to the left of the instance, mirrored, tail pointing at him. Images (bank 0): 0x2c bubble, 0x2d "?!", 0x2e "$", 0x2f..0x31 z/zz/zzz, 0x32 "...", 0x33/0x34 black/red curse. Callers: Kill(1) `0x44c2a9` (kind 0), race Kill(1) `0x44c5d8`, hard landing `0x464470` (kind 1, 2.0 s), sleeping `0x464601` (kind 4, 2.5 s, 130/50, alive = `P+0x52c`), message 1500 `0x46ccf6` `[inst, kind, duration·100, offY, offX]` (only K2R and S2R). Port: `game_bubble` / `bubbles_draw` in main_engine.c, `hud_world_bubble` in hud.c; test `WOODY_KILLAT=2` (death), 12 s standing still (zzz), `WOODY_POSAT="1 537 200 -2148"` in W1A (hard landing), log `WOODY_BUBLOG=1`.

### 4.2 Skeleton effect `0x477e40(&pos)` (callback `0x477980`) — kind 2/9
Creation: lifetime **1.5 s**; `+8 = &player.pos (inst+0xc)`; `+0x10 =` old `player+0x6c`; `+0x14 =` image table
`0x4b7e30` (character type 1 = Woody) or `0x4b7e60`; the pos argument is **not used**. Also sets the player's
facing: `0x459ff0(M, −(view+0x20..0x28))` = −view.row2 (`0x477efb..0x477f7b`) ⇒ the player turns toward the camera.

Per frame, `u = age/1.5`, eighth-phases:

| u | state | pose row | flip row | head rotation |
|---|---|---|---|---|
| 0–0.125, 0.25–0.375 | **skeleton A** | 2 | 12 | 40/512 turns |
| 0.5–0.625, 0.75–0.875 | **skeleton B** | 0 | 0 | 472/512 turns (= −40) |
| other eighths | **model** | | | |
| > 1 | end: `0x44e7f0(player, old +0x6c, 0)`, lifetime −1 | | | |

* skeleton: `0x44e7f0(player, 1.0, 0)` (target transparency `+0xfc = 1`), model: `0x44e7f0(player, 0, 0)`; always
  `player+0x100 = 100.0` (fade speed 100/s ⇒ within a single frame) ⇒ **model invisible during skeleton phases**.
* base: `d = normalize(cam.pos − player.pos)` (3D), `R, U, d` from `0x46d320`; `jit = rnd·20 − 10` (one per frame).
* 6 sprites `i = 0..5` at `player.pos + R·o.x + U·o.y + d·o.z`, offsets `o` = `vec3 0x4b7d10[poseRow·6 + i]` (4 rows × 6; code index in floats: 0 resp. 0x24):
  row 0 = (−5,180) (−60,100) (60,150) (0,96) (−40,41) (40,46); row 2 = (10,180) (−60,150) (60,100) (0,96) (−40,41) (40,46) (z = 0).
  Size: i = 0 → 120 (with head rotation), i = 3 → 75 (mode 0x1a), others 55; mode 0x12; rotation 0 for i ≠ 0.
  Image `T[i]` = Woody {0x25, 0x22, 0x22, 0x23, 0x24, 0x24} (others: head 0x2a), mirror = `0x4b7cb0[flipRow + i]`
  (row 0: 0,1,2,0,0,2; row 12: 2,0,3,0,0,2), **flags 0x4d** (non-additive). Right after that the same sprite with
  `size += jit`, image `T[6+i]` = {0x29, 0x26, 0x26, 0x27, 0x28, 0x28} (others: head 0x2b), **flags 0x45** (additive glow).
* every frame (also in model phases) dynamic light `0x498790([0x4c4cac], 0, &S.pos (last sprite drawn), white (255,255,255), radius 200 + rnd·100)`.

### 4.3 Water splash `0x478660(&C, v, r)` (emitter callback `0x478360`) — kind 7 (also `0x46ce27`)
Emitter: lifetime **1.0 s**, `C` = center, `h = v · 0.001` (`0x4aa0f4`), `R = r + rnd·50` (death: 50..100),
`accA = 0`, `accB = 0.2`. Per frame (`u = age`): `u ≥ 1` ⇒ end.
```c
if (u < 0.3) { accA += dt; n = (int)(accA * 500); accA -= n * 0.002;  n × droplet(); }      /* 0x4aab98, 0x4a9998, 0x4abd54 */
accB += dt; n = (int)(accB * 5); accB -= n * 0.2;  n × ring();                             /* first ring immediately */

droplet (0x4783e8, callback 0x477fa0): life = (rnd+1)*0.4;  a = (int)(rnd*511);  c = cos(a), s = sin(a)  (2π/512);
    p0 = C + ((rnd*20 + R)*c, 0, (rnd*20 + R)*s);   dir = (c, 0, s);   D = 150 + rnd*100;    /* +0x2c */
    per frame, w = age/life:  pt(w) = (p0.x + dir.x*D*w,  p0.y + sin(pi*w*255/256) * h*100,  p0.z + dir.z*D*w)
    line 0x471a10 from pt(w) to (x,z at w+0.08; y at w+0.1), rgba1 = (.5,.5,.5,.65), alpha0 = 0, width 4,
        image 0x39, flags 0xe00 (additive);
    at w >= 1: ripple (callback 0x478290, 0.6 s) at pt(1): sprite image 3, horizontal (flags 2, normal (0,1,0)),
        rgb (.65,.65,.8), alpha = (1-w)*0.3, size = 25*w + 5.
ring (0x4785cc, callback 0x4781b0): life 0.7 s, at C, image 0x3a, horizontal + rotation (flags 6), rot = (int)(rnd*512),
    rgb (.65,.65,.8), alpha = (1-w)*0.3, size = 2*R + 600*w.
```
Droplet height = `speed · 0.1` units (falling at 1500 u/s ⇒ 150).

### 4.4 Camera `0x41fb50(CamMgr, &pos, inst)` (also `0x459030`)
`CamMgr+0x440..0x448 = pos`, `+0x44c = inst`, `0x41f9b0(100.0)` (transition speed 100 u/s, duration = distance/speed),
`0x41f9f0(1)` (smooth), `+0x45c = +0x348`, `0x41f410(1, 0)` = **mode 2: fixed camera at `pos` looking at `inst`**
(CAMERA.md §mode 2). Kind 1: from the point where Woody was hanging (+100) watch him fall; kind 7: camera freezes at
its current spot. The respawn resets the follow camera with a hard cut (PERSO_FRAME §4.1).

---

## 5. Hit animation `0x464b70` (called from `Hit` `0x44ca00`, not death)
```c
if (state != 6)      Request(onGround ? (duck694 ? 0x23 : 0x1f) : 0x20);
else if (P->+0x594) { Request(onGround ? (duck694 ? 0x24 : 0x21) : 0x22);  P->+0x58c = 2; }
```
One request (prio 5110, restart); the chain returns on its own to idle (anim 0) resp. duck loop 25. **No LockMove**: `Hit`
actually sets `+0x238 = 0` (`0x44cce0(0,1)`). Control is only blocked during the knockback timer `M+0xec = 0.2 s`
(`0x44bb48`: `+0x474 > 0` ⇒ no input; 500 u/s, tapering off over 0.5 s). Invulnerable `+0x280 = 0.6 s`. Animation duration 0.47 s (ground) /
0.40 s (air) has no effect on control; prio 5110 > walking/jumping, so it plays out unless a prio ≥ 5110 comes along.

## 6. Blinking
* `+0x270` (respawn 1.0 s) and `+0x280` (post-hit 0.6 s) are **only** read by `Hit`/`Kill` and counted down in `0x44b1b0`
  (all readers in the exe traced): **there is no visibility or color blink for ordinary invulnerability.**
* White blinking only exists for the invulnerability bonus: `0x44c890(P, t)` sets `+0x704 = +0x700 = t`, `+0x270 = max`,
  `+0x280 = max`. Callers: bonus-class handler `0x44f9c8` (message 10, `t = arg1 · 0.01`) and debug key 0x30 in `0x402940` (10 s).
* `0x44cf50` (vt[26], render color): `[0x5ac860] = 0`; `+0x704 ≤ 0` ⇒ `[0x5ac850] = 0` (normal). White = `[0x5ac850] = 2`,
  color `[0x5ac854..c] = (255,255,255)`:
  * `+0x704 ≥ 5.0` (`0x4a9884`): `acc (+0x708) += dt`; `acc ≥ 0.5` ⇒ `acc −= 0.5` and white; otherwise white while `acc ≤ 0.1`
    ⇒ **0.1 s white per 0.5 s**.
  * `+0x704 < 5.0` (nearly expired): counter `+0x70c` ⇒ **white every other frame**.

---

## 7. Enemy death: sparkles `0x477610(kind, enemy)` (callback `0x477350`)
`vtbl[57]` `0x41b000`: `rand() & 1 ? kind 0 : kind 1`. Five records `i = 1..5`, lifetime **2.5 s**, `+8 = i`,
`+0xc` = image (kind 0 → **0x0b**, 1 → **0x0a**, 2 → 0x08), `+0x10 = enemy`.
```c
if (enemy->poseFrame /*+0x58*/ != frameCounter /*[0x509adc][0]*/) { life = -1; return; }   /* enemy gone/no longer posted ⇒ end */
u = age / 2.5;  if (u >= 1) return;
Inst_GetMarker(enemy, 0, v, 0);                   /* first marker with typecode 0: P0 = v[0], len = |v[1]-v[0]| */
ang = (i*512/5 + (int)(512*u)) mod 512;           /* 72° apart, 1 revolution per 2.5 s (0x4abd3c = −512) */
f = ang*6/256.0;  k = (int)f;  w = (k & 1) ? f-k : 1-(f-k);                                /* triangle wave, 6 per revolution */
pos = (P0.x + 80*cos(ang),  P0.y + len + (4*w)*(4*w),  P0.z + 80*sin(ang));                /* 0x4ab134, 0x4a94c0 */
rot = (int)((w - 0.5) * 56);  size = 40;  mode 0x12;
a = u < 0.7 ? 1 : 1 - (u - 0.7)*3.33;                                                      /* 0x4aa1d8, 0x4abd30 */
if (img == 0x1000a)  sprite(img 5, pos + (0,15,0), rgb (1,1,.5), alpha 0.8*a, flags 3);    /* additive glow, billboard */
sprite(img, pos, rgb (.5,.5,.5), alpha a, rot, mirror 2, flags 0x4f);                      /* non-additive, mirrored, rotated */
```
No velocity/gravity: the stars are a function of time and the marker every frame, so they follow the enemy.

Confirmed from ENEMY.md and the code: the death animation is logical 13 = `(12,−1,−1,−1)` ⇒ one-shot, **stays on the last frame**
(`0x436b34` leaves −1 as is; INSTANCE.md §2: phase clamped, speed 0); removed after `AnimLen(13) + 1.0` s, fade `+0x6c` linear
0 → 1 over the second half (`0x41a47d`). State 12 (types 4/5/6) resp. 10 (types 7..9) uses behavior **Stand still**, whose
tick is the regular `0x41b2c0` ⇒ **the knockback (600·t_rest, 0.25 s) and platform-riding keep running while it's dead**.
The stars end as soon as the enemy is removed from the world (i.e. after min(2.5, AnimLen+1.0) s).

---

## 8. Uncertain
1. The direction of `R` and of `side` in the speech bubble and of `−view.row2` in the skeleton effect: formulas are taken literally;
   left/right depends on the view-matrix convention (not verified live).
2. `0x44bf10(P, 0)` in the ground snap and `CamMgr+0x45c = +0x348` have not been investigated.
3. Image content of bank-0 images 0x22..0x34, 0x39, 0x3a is inferred from usage, not viewed.
4. `[0x5ac850] = 2`: how the renderer blends "white" (replace or add) has not been traced.
