# CINEMATIC: real-time cinematics (messages 1130/1131/1132), camera mode 0x80, scripted Perso actions, text 1080

Static analysis of `Woody.exe` (imagebase 0x400000). Every fact has an address. See also MESSAGES.md (game handler
`0x444870`), INSTANCE.md (animation clock `0x43eee0`), CAMERA_SCRIPT.md §4.3, GAMEFLOW.md.
The engine itself calls this a **"Real time cinematic"** (strings `0x4b3b78`, `0x4b3b48`, `0x4b3c00`, `0x4b3bb8`).

## 0. Summary

* The cinematic object `Cin` sits **inline in the Game object at `game+0x64`** (no own allocation; reset `0x44e940`,
  called from the Game init `0x4458d4`). Update per frame: `0x44f0a0(Cin, dt)` from the Game update `0x445af4`.
* `1131 [inst, anim]` = pick the **main instance** (provides camera + end position) and its animation;
  `1132 [inst, anim]` = add an **actor** (max. 32); `1130 [vecInst, track, var]` = start: `vecInst` = instance
  with a typecode-5 node (2 points = position + direction), `track` = music track number, `var` = script variable that
  receives `ftol(t0·100)` at the actual start (t0 = start-time offset returned by the music player, in s).
* States (`Cin+0x120`): 0 = off, 1 = fade-out (0.5 s), 2 = playing, 3 = fade-out at the end (0.5 s),
  4 = fade-in after the end (0.5 s) → 0.
* Time base: all instances play their animation at **speed 3.0** (`0x42e290(3.0)`), so duration =
  `duration/4096/3` s = `duration · 8.138e-5` (`0x4aace8`); one-shot (slot1..3 = −1 → clamps on the last frame).

## 1. The object `Cin` = `game+0x64`

| offset | type | meaning | evidence |
|---|---|---|---|
| +0x000 | i32 | animation of the main instance (1131 arg 2) | `0x44e988` |
| +0x004 | Instance* | main instance (1131 arg 1); 0 = not initialized | `0x44e98a`, test `0x44e9e6` |
| +0x008 + 8·i | i32 | actor i: animation (1132 arg 2) | `0x44e9af` |
| +0x00c + 8·i | Instance* | actor i: instance (1132 arg 1); i < 32 | `0x44e9bd`, bound `0x44e9a6` |
| +0x108 | i32 | music track (1130 arg 2); −1 = none ("No Track selected…" `0x44ea60`) | `0x44e994` |
| +0x10c | u32 | script variable ref (1130 arg 3) | `0x44ea58` |
| +0x110 | i32 | number of actors | `0x44e9c8` |
| +0x114 | f32 | remaining play time (s) | `0x44ea25`, `0x44f182` |
| +0x118 | f32 | fade timer (s) | `0x44ea31`, `0x44f0c2` |
| +0x11c | Instance* | vector instance (1130 arg 1) | `0x44e9ff` |
| +0x120 | i32 | state 0..4 | `0x44ea15`, `0x44f0a3` |
| +0x124 | f32 | fade-out duration before start = 0.5 | `0x44e960` |
| +0x128 | f32 | fade-in duration at start = 0.5 | idem |
| +0x12c | f32 | fade-out duration at the end = 0.5 (also: end margin) | idem, `0x44f197` |
| +0x130 | f32 | fade-in duration after the end = 0.5 | idem |
| +0x134 | vec3 | end position for the player (= `game+0x198`) | `0x44f06e..0x44f081` |
| +0x140 | 3×vec3 | end rotation (rows; row 1 = look direction = `game+0x1b0`) | `0x44f014..0x44f064` |

Reset `0x44e940`: `+0x108 = −1`, `+0x120 = 0`, `+4 = 0`, `+0x110 = 0`, the four fade times = 0.5 (`0x3f000000`).
Because the reset runs after the end (`0x44f26c`), **1131/1132 must be sent again before every cinematic**.
Queries: `0x44f2d0` = "state == 1", `0x44f2e0` = "state 2 or 3" (= **cinematic is running**).

## 2. The messages (game handler `0x444870`)

| id | args | code | behavior |
|---|---|---|---|
| 1131 | inst, anim | `0x444acf` → `0x44e980(Cin, instPtr, anim)` | `Cin+4 = inst`, `Cin+0 = anim`. inst = index into the instance table `[0x50944c]+0x6c` (`& 0xffffff`); House/W1A/W3D/WWS: **instance 0 = the Perso itself** (`1200 [0x1000000, 1]`; the Woody model has the camera nodes 0x80/0x180); W1B inst 8, W2B 1, W2D 3, W3B 310, K1A 291, K1R 124, …: in each case the instance that `1200 SetTypeInstance` gives 1/2/3/18 = **always the Perso**; races use anim 10 instead of 72. Animations: 72..75 (multiple cinematics per level = 72, 73, …) |
| 1132 | inst, anim | `0x444afb` → `0x44e9a0` | add actor (silently ignored at ≥ 32) |
| 1130 | vecInst, track, var | `0x444a90` → `0x44e990(track)`; `0x44e9e0(vecInstPtr, var)` | start, see below. `track` = index into the stream table `0x4b73a0` (8 = `/Rtc/Menu.wav`, 9 = `/Rtc/Woody/W1A.wav`, 10/11 = W1Ba/b, 12 = W2B, 13..15 = W2Da..c, 16 = W3B, 17..20 = W3Da..d, 21/22 = WWSa/b, 23.. = Knothead…; 0..7 = the regular level music) |

`0x44e9e0(vecInst, var)`:
```c
if (!cin->main) { warn("You can't start a Real time cinematic without initialisation..."); return; }
cin->vecInst = vecInst;
cin->remain  = model(cin->main)->anim[cin->anim].duration * 8.138e-5f;   // 0x4aace8 = 1/(4096*3)
cin->state   = 1;
cin->timer   = cin->fadeOut0;                  // 0.5
App_FadeOut(cin->timer - 0.1f);                // 0x401480: to black in 0.4 s (0x4a9008 = 0.1)
cin->var = var;
if (cin->track == -1) warn("No Track selected for Real time cinematic...");
if (music) { music->vt[0x94](cin->track);      // 0x46cc00: stop previous rtc stream, remember the track number
             music->vt[0x50](cin->timer*0.9f); }   // fade level music out in 0.45 s (0x4a94b8 = 0.9)
```

## 3. Per frame: `0x44f0a0(Cin, dt)` (jump table `0x44f278`: `0x44f0c2`, `0x44f14d`, `0x44f1da`, `0x44f24d`)

```c
bool Cin_Update(Cin *c, float dt) {             // return 1 = "cinematic just ended" (for 1 frame)
  switch (c->state) {
  case 1:                                        // turning black
    c->timer -= dt;  if (c->timer > 0) break;
    c->timer = c->fadeIn0;                       // 0.5
    App_FadeIn(c->timer);                        // 0x401440
    App_DrawBlack();                             // 0x4014c0: draw a black plane 640x480 immediately
    float t0 = 0;
    if (music) while (!music->vt[0x98](&t0)) ;   // 0x46cc20: open stream 0x4b73a0[track]; sets t0 = 0; always returns 1
    c->state = 2;  Cin_Start(c, t0);             // 0x44eab0
    break;
  case 2:                                        // playing
    debug("Time : %f / %f", total - c->remain, total);     // 0x44f178, total = duration*8.138e-5 (0x4aacec)
    c->remain -= dt;
    if (c->remain > c->fadeOut1 /*0.5*/) break;
    c->timer = c->remain;  App_FadeOut(c->remain);          // last half second: fading to black
    c->state = 3;  break;
  case 3:
    c->timer -= dt;  if (c->timer > 0) break;
    c->timer = c->fadeIn1;  App_FadeIn(c->timer);           // 0.5
    c->state = 4;
    Cin_ComputeEnd(c);                                      // 0x44edb0 (§5)
    if (music) music->vt[0x54](c->timer * 0.9f);            // fade level music back in (0.45 s)
    return 1;
  case 4:
    c->timer -= dt;  if (c->timer <= 0) Cin_Reset(c);       // 0x44e940 -> state 0, main = 0, nActors = 0, track = -1
    break;
  }
  return 0;
}
```

### 3.1 Start `0x44eab0(Cin, t0)`
```c
vec3 P[2];
if (!Inst_GetTypecodePoints(c->vecInst, 5, P, 0)) {        // 0x42f6b0: 0th node with typecode 5 (flags>>8 == 5),
    warn("You must give a vector to begin a real time cinematic...");   // points -> world (matrix palette [0x509adc]+0xa0)
    c->remain = 0; return; }
SetVar(c->var, ftol(t0 * 100.0f));                         // 0x443ca0; 0x4a9010 = 100. In this build t0 is always 0 => var = 0
c->remain -= t0;
Instance *m = c->main;
m->start = now - t0;   m->slot[0] = c->anim;   Inst_SetSpeed(m, 3.0f);   // +0xa8, +0xb0, 0x42e290(3.0): +0xa0 = +0xa4 = 3
m->flags8 &= ~0x20;                                        // let the clock determine the cell again
m->slot[1] = m->slot[2] = m->slot[3] = -1;                 // one-shot: clamps on the last frame
m->pos = m->center = P[0];                                 // +0xc and +0x60
vec3 d = -(P[1] - P[0]);  d.y = 0;  normalize(d);          // NOTE the minus sign (fchs 0x44ebd6/0x44ebe4)
m->rot = { cross(d,(0,1,0)), d, (0,1,0) };                 // rows +0x28/+0x34/+0x40, same convention as INSTANCE.md l. 214
for (i = 0; i < c->nActors; i++) {                         // 0x44ecc0
    Instance *a = c->actor[i].inst;
    a->slot[0] = c->actor[i].anim;  a->start = now - t0;  Inst_SetSpeed(a, 3.0f);
    a->center = a->pos;  Inst_Recell(a, 0);                // 0x4077f0(0): removed from the cell and ALWAYS re-inserted ⇒ an
                                                           // actor hidden by the script (message 6 [a, 0]) becomes visible again (W1B: Buzz 398 + dish 399)
    a->slot[1] = a->slot[2] = a->slot[3] = -1;
}                                                          // actors are NOT moved: they stay where the level places them
if (!Inst_HasCameraTrack(m, c->anim)) {                    // 0x42feb0
    warn("No Camera in the main instance given to play the Real time cinematic"); return; }
cam->animInst = m;                                         // CamMgr+0x5d4
cam->flags618 |= 2;                                        // letterbox on
CamMgr_SetTransition(cam, 2);                              // 0x41f9f0(2) = hard cut
CamMgr_SetMode(cam, 7, 0);                                 // 0x41f410: mode 0x80
```
`now` = `[0x509adc]+0x30`. So all instances run on the **same world clock at speed 3**, phase =
`(now − start)·3·4096/duration` (INSTANCE.md §1.2); there is no further synchronization. Animations of different
lengths each end on their own last frame (clamp, speed → 0). The cinematic's running time is that of the
**main animation**.

### 3.2 What the rest of the engine does while `0x44f2e0` is true (state 2/3)
| where | effect |
|---|---|
| main loop `0x401cf9..0x401d07` | **`Perso::Update` (`0x44b530`) is skipped**: no input, movement, collision or Perso animation selection; the player instance is only still animated by the ordinary instance clock/render (slot0 = cinematic animation) |
| main loop `0x401cc2` | stop the Perso's sound source (`Perso+0x4a4`) (`0x468e20`) |
| main loop `0x401e19` | HUD is not drawn |
| `0x4033cc/0x4033db` and `0x445980` | pause menu blocked (also in state 1) |
| Perso `0x44afc5`, `0x44c136/0x44c14b` | two Perso routines (e.g. damage) skipped (also in state 1) |
| `0x44e6c0` (`0x44e690`, main loop `0x401d16`, **House only**) | Perso transparency target `0x44e7f0`: 0 (visible) during the cinematic, otherwise 1.0 (invisible), speed `+0x100 = 10000` (Uncertain 3) |

The scripts (VM) keep running: subtitles/fades/sounds of the cinematic are timed by the level script with
`DELAY`s (§7). **There is no skip key** in `0x44f0a0`; only the House intro can be aborted, via the
menu (page 0x1f, key 6/9, GAMEFLOW §5): stop the script object, `SetVar(app+0x8c, 4)`, `0x44f290(Cin)` =
reset + `music->vt[0x54](timer·0.9)` + `vt[0x9c]()` (stop rtc stream), Perso `vtbl[0x44]()` (`0x405129..0x40515b`).

## 4. Camera mode 0x80: `0x41f1ee` → `0x42fa40` → `0x42fa80`

`CamMgr_Update` mode 0x80 (`0x41f1ee`): `I = CamMgr+0x5d4`; if `0x42feb0(I, I->slot0)`:
`0x42fa40(I, &CamMgr+0x140 /*view matrix*/, &pos, &CamMgr+0x5d8 /*target*/)`, `CamMgr+0x1d0 = pos`; then always
`CamMgr+0xc4 (look-at) = CamMgr+0x5d8`. **No fov track**: the fov is that of the letterbox (CAMERA_SCRIPT §1.4,
sy = 0.5625 → vfov 68.0°). No transition (cut), no collision, no smoothing.

`0x42feb0(I, anim)`: walk the **top-level sibling list** (`S+0x6c` = first node, 1-based; next = `N+0x80`, −1 =
end), remember the last node with `flags == 0x80`; result = `N->posTracks(+0x70)[anim].count(+4) != 0`.

`0x42fa40`: `phase = I->pos_ac(+0xac) / (duration[I->slot0] / 4096)` (`0x4aa138` = 1/4096) → i.e. the phase that the
instance clock set this frame (the main instance's clock must have run before the camera update, otherwise
the camera lags 1 frame behind).

`0x42fa80(I, outView, outPos, phase, anim, outTarget)`:
```c
M = diag(I->scale) * I->rot;  T = I->pos;                 // row vectors: w = l.x*M[0] + l.y*M[1] + l.z*M[2] + T
frame = model->anim[anim].nframes * phase;                // float, same unit as the clock (0x43a2b0)
camN = node with flags == 0x80;  tgtN = node with flags == 0x180;  // kind 0x80, typecode 0 resp. 1; top-level
                                                          // (loop stops only once both are found -> model MUST have both)
vec3 c = PosTrack(camN, anim, frame);                     // 0x43a660: only the node's own POSITION track,
vec3 t = PosTrack(tgtN, anim, frame);                     //   no parent, no rotation
C = c*M + T;   G = t*M + T;                               // to world with the INSTANCE matrix (not the bones)
if (outTarget) *outTarget = G;
f = normalize(G - C);
r = normalize(cross(f, (0,1,0)));                         // 0x41af10(this=f, out, arg) = this x arg
u = cross(f, r);                                          // view-y points DOWN (f=(0,0,1) -> r=(-1,0,0), u=(0,-1,0))
outView = columns {r, u, f}, translation = (-r.C, -u.C, -f.C);     // v_view = v_world * outView
*outPos = C;
```
No axis conversion: track positions are used as they are stored in the .ins (y-up, same as the meshes). No roll
(up = world-y). For the port: `eye = C`, `center = G`, `up = (0,1,0)`.

### 4.1 Position track with cut detection `0x43a660(node, out, anim, frame)`
Finds the first keyframe with `key.t >= frame` (16-byte frames `{t, x, y, z}`, FORMAT_INS §2.2 field 7). If the
two surrounding keys are exactly **1 frame apart** (`ftol(t1 − t0) == 1`): evaluate the ordinary linear
track (`0x43a590`) at `floor(frame)` and at `floor(frame)+1`; if the distance between them is **> 200** (square > 40000,
`0x4a9890`) → return the position at `floor(frame)` (**camera jump = hard cut, do not interpolate**). Otherwise (and in
all other cases) just `0x43a590(frame)` = linear interpolation. This applies to both the camera and the target
separately.

## 5. End: `0x44edb0` and the Game update `0x445af9..0x445b66`

`0x44edb0(Cin)` (at the 3 → 4 transition, screen is black):
```c
m->flags8 |= 0x20;
A = RootNode(m, anim, phase 1.0) * W(m);          // 0x42f7e0(1.0, anim, &A, 1): first top-level node with flags == 0
                                                  //   (kind 0, the skeleton root): rot track 0x43a9c0 (+0x74),
                                                  //   pos track 0x43a590 (+0x70), frame = nframes*phase; * instance matrix
B = RootNode(m, anim 0, phase 0);                 // 0x42f7e0(0, 0, &B, 0): rest pose, without the instance matrix
C = inverse(B) * A;                               // 0x440fc0(B,1,1,1) ; 0x4405e0
pos = C.t   (via A^-1 and back through A again, 0x44ee5d..0x44efb2; net result C.t, see Uncertain 2)
dir = normalize_xz(C.row2);                       // local z-axis of the root, y = 0 (0x4239f0)
cin->endPos = pos;                                // +0x134
cin->endRot = { cross(dir,(0,1,0)), dir, (0,1,0) };   // +0x140 / +0x14c / +0x158
```
Game update (`0x445af4`): if `Cin_Update` returns 1:
```c
vec3 dir = game->+0x1b0;                          // = cin->endRot row 1
Perso->vtbl[0x44]();                              // Perso reset (same as on respawn, PERSO_FRAME §respawn)
Perso_SetFacing(Perso+0x388, &dir);               // 0x459ff0
Perso_SetPos(Perso, &game->+0x198);               // 0x44a650 (= cin->endPos), re-establish ground
CamMgr_SetTransition(cam, 2);  cam->+0x368 = 0;  CamMgr_SetMode(cam, 0, 0);   // 0x445b66: cut to the follow camera behind the player
```
The letterbox goes off in `CamMgr_Update` as soon as the previous mode was 0x80 and the new one is not (CAMERA_SCRIPT §1.4,
`0x41f34f`). **Nothing is sent to or written back to the script at the end**; the script knows the length itself
(fixed `DELAY`s relative to the start, §7). The actors stay on their last frame until the script hides them.

## 6. Perso: scripted actions `0x44dda0(Perso, action, vec6*, targetInst)`

**The action number IS the raw .ins animation number** of the player model: `0x463e30(action)` looks up the first record
in the logical table `0x4b6180` (0x80 records × 0x1c: `{int sub[4]; int prio; float speed; u8 restart}`) whose
`sub[0] == action` (only then `sub[1]`, …) and returns that logical index; the camera test also uses the action number
directly as the animation index (`0x44df67`: `0x42feb0(Perso, action)`).

```c
bool Perso_ScriptedAction(Perso *p, int act, const float *v6, Instance *target) {
    if (p->state == 2) return 0;                                  // +0x21c
    p->target554 = target;  p->camAllowed558 = 1;
    if ((p->flags104 & 0x3e0) != 0xa0 && != 0x80) { p->fadeOutFlag560 = (act == 17); p->fadeInFlag561 = (act == 18); }
    // valid actions: 10..16, 19, 72..78 (table 0x44dfe8); 17/18 same + first 0x443ff0(p->+4); otherwise
    // warn("Animation n°%d is not know as a cinematic animat…") and still continue
    p->altMode4ec = 0;  0x44dd70(p);                              // clear jump/attack state
    p->logical534 = LogicalFromRaw(act);                          // 0x463e30
    p->total538 = p->remain53c = L(sub[0]) / rec.speed;           // 0x436b90: duration/4096/3.0  (= duration in s)
    animctl->vt[4]();  animctl->vt[2](p->logical534);             // reset + start logical animation
    p->action540 = act;   Perso_SetState(p, 5);                   // 0x44c980
    if (v6) {                                                     // vector = typecode-5 node of the instance from the message
        p->pos = v6[0..2];  p->onGround22c = 1;
        dir = normalize_xz(v6[3..5] - v6[0..2]);  Perso_SetFacing(p+0x388, &dir);   // here WITHOUT the minus sign
        p->useRootPos550 = 0;  Perso_Orient(p);                   // 0x44bd00
    }
    if (Inst_HasCameraTrack(p, act) && p->camAllowed558) {        // only anims with a camera track (72..78)
        cam->animInst = p;  cam->flags618 &= ~2;                  // NO letterbox (difference from 1130)
        CamMgr_SetTransition(cam, 2);  CamMgr_SetMode(cam, 7, 0); }
    animctl->vt[3](p->dt2f8);
    return 1;
}
```
State 5 per frame (`0x44db50`):
* `target554` ≠ 0: `0x42fa40(p, …, &camPos, 0)` → `target->pos = camPos`, `target->rot = p->rot`, force the clock
  (`+0x58 = −1`, `vt[2]()`): an instance that **follows the animation camera** (never used by scripts: 1043 occurs 0×).
* actions 17/18: `fadeOutFlag` and `remain < 0.6` (`0x4a9650`) → `App_FadeOut(0.5)` (one-shot); `fadeInFlag` →
  `App_FadeIn(0.5)` immediately in the first frame. `remain -= dt`; at ≤ 0 → state 0; and for 18 additionally
  `facing = −facing` (`0x4a9500` = −1: Woody comes out of the door backwards and turns around), `0x462990` (ground), idle
  (logical anim 1) and `0x44e5a0`.
* other actions: `remain -= dt`; at ≤ 0 → state 0 + idle; every frame `0x44e290(last)`:
  `E = inverse(Root(anim 1, phase 0)) * Root(act, phase 1) * W` (end point), `Q` = the same at `phase = (total − remain)/total`;
  `p->rootPos544 = (1 − phase)·p->pos + phase·Q'` and `useRootPos550 = 1` (display/camera position follows the root);
  on the last frame: idle, `facing = −E.row2`, `p->pos = E.t`, `useRootPos = 0`, `action540 = −1`, `0x462990`
  → **the Perso gets moved to wherever the animation brought the root**; then `0x44e5a0`.
* `0x44e5a0` (end): restore follow-camera parameters (`CamMgr+0x338`: player-position ptr, look direction, `+0x28 = −1`,
  `+0x30 = 0`), `0x41f9d0(0.5)` (transition duration 0.5 s), `SetTransition(1)` (smooth), `SetMode(0, 0)` (`0x44e634`).

| action (= .ins anim) | logical record (`0x4b6180`) | chain | movement / notes | used by |
|---|---|---|---|---|
| 10 | 12 | 10 → 11 | root movement | scripts: only as the 1131 animation in the race levels (K/S 1R..3R) |
| 11 | 121 | 11 → 0 | | – |
| 12 / 13 / 14 / 15 | 20 / 21 / 22 / 23 | 12→13 loop / 13 loop / 14→6→7 / 15 | | – (ordinary gameplay records) |
| 16 | 13 | 16 → 0 | | – |
| **17** | 24 | 17, one-shot | **walking into a door**: Perso at P0 of the door vector, looking towards P1; fade-out 0.5 s if 0.6 s remain; duration = L(17)/3 | 1040 `[door, 17]` (192×) |
| **18** | 25 | 18, one-shot | **coming out of a door**: fade-in 0.5 s at the start; facing reversed at the end | 1040 `[door, 18]` (159×) |
| 19 | 14 | 19 → 6 → 7 | | – |
| 72 / 73 | 81 / 82 | N → 0 | cinematic anims with a camera track | 1131 (W-levels 72..75); **0x49 = 73**: House menu pose, engine `0x44e6b0` (trigger `Perso+0x57c` via message 1141 `0x4448d1` → `0x44e640`, GAMEFLOW §4.5), `0x4594f0`, `0x4788a7`, `0x47896b` |
| **74 (0x4a)** | 26 | 74, one-shot | placement at the hub door `0x453dbb` (vector in `Perso+0x72c`) | engine |
| 75 (0x4b) | 27 | one-shot | state 9 / SaveAuto `0x4540f4`, `0x45412c` | engine |
| 76 (0x4c) / 78 (0x4e) | 29 / 28 | one-shot | cheering `0x453ffd` / `0x453fd6` | engine |
| 77 (0x4d) | 30 | one-shot | `0x45402e`, `0x454186`, `0x4541b0` | engine |

All records have `speed = 3.0` → duration = `duration/12288` s, same as for 1130. Because 74..78 are found as `sub[0]`
first in the one-shot records 26..30, the records 83..87 (`N → 0`) are unreachable for `0x44dda0`.

## 7. Script protocol (House obj 115, W1A obj 277; identical in all levels)

```
init:   var = -1
start:  (DELAY 50..100: swapping props with message 6 = show/hide, possibly 56/57 fade)
        1131 [mainInst, anim]            ; W levels: player instance, anim 72..75; K/S levels: separate instance; races anim 10
        1132 [actor, anim] ...           ; 2..10 entries, anim usually 0 or the same as the main anim
        1130 [vecInst, track, var]
wait:   if (var != -1)                    ; the engine writes var = ftol(t0*100) = 0 as soon as the screen is black and playback starts
           timeline: for every event at T (1/100 s after start):  DELAYPOP (T - var) -> action
```
House: T = 900 → `1151 [300]` (fade-out 3 s), +290 → swap actors + `1152 [0]`; T = 4200/4600/4950… → `1080` text,
T = 4500/4900… → `var4 = 1` (close text). W1A: T = 1600 → hide actors 276/278, show 220. `var` is thus the
**elapsed time at the start** (music sync, always 0 in this build); −1 is only the script's own
"not yet started" value. Counts: see `1130` in House 1×, W1A 1×, W1B 2×, W2D 3×, W3D 2×, WWS 2×, each K/S level 1×.

## 8. Message 1080: text box `0x456ed0(Game+0x18, record)`; update/draw `0x4571c0(dt)`

`1080 [hAlign, vAlign, var, id1, id2, id3]` (lines up to the first −1, max. 3):
* `hAlign` (arg 1, `0x45707b`): 0 = centered, 1 = left (x = 16), 2 = right (`width − w − 16`).
* `vAlign` (arg 2, `0x456fe5`): 0 = middle, 1 = top (y = 16), 2 = bottom (`height − total text height − 16`).
* `var` (arg 3 → `+0x30`): **close flag set by the script**; the engine never writes to it.
* `idN` = resource refs (`RckGet` `0x43fb30`/`0x441580`, RCK.md: `0x0002xxxx` = string xxxx from bank 0 =
  `extract/Common/<character>.rck`, glyph indices); font = `0x01030000` (font 0 of the level bank). Font size starts at
  25 and shrinks in steps of 1 until every line fits (lower bound 17, `0x4ab2b8`), then −2. `0x20001` = empty separator
  line (the background rectangle only starts after that line, `0x457105`).
* States (`+4`): 1 = fade-in 0.5 s (alpha = 2t·`0x4aa2f0`), 2 = stays until `*var != 0` (`0x457245`), 3 = fade-out
  0.5 s, 0 = gone. Background = semi-transparent black plane (`0x480a10`, alpha = text alpha/2) with a 16-unit margin.

## 9. Recipe for the port

```c
typedef struct { int anim; Instance *inst; } CinActor;
typedef struct { int anim; Instance *main; CinActor actor[32]; int track; unsigned var; int nActors;
                 float remain, timer; Instance *vecInst; int state; vec3 endPos, endDir; } Cin;

void msg_1131(Instance *i, int anim) { cin.main = i; cin.anim = anim; }
void msg_1132(Instance *i, int anim) { if (cin.nActors < 32) cin.actor[cin.nActors++] = (CinActor){anim, i}; }
void msg_1130(Instance *vec, int track, unsigned var) {
    if (!cin.main) return;
    cin.vecInst = vec; cin.track = track; cin.var = var;
    cin.remain = anim_duration(cin.main, cin.anim) / 12288.0f;
    cin.state = 1; cin.timer = 0.5f; fade_out(0.4f); music_fade_out(0.45f);
}
bool cin_running(void) { return cin.state == 2 || cin.state == 3; }   // -> skip player_update, no HUD, no pause

void cin_update(float dt) {
    switch (cin.state) {
    case 1: if ((cin.timer -= dt) > 0) break;
        fade_in(0.5f); music_play_stream(rtc_track[cin.track]);
        vm_setvar(cin.var, 0);
        vec3 P0, P1; inst_typecode5_points(cin.vecInst, &P0, &P1);
        Instance *m = cin.main;
        m->pos = P0; vec3 d = norm_xz(sub(P0, P1));                   // d = -(P1-P0)
        m->rot = rows(cross(d, UP), d, UP);
        inst_play_once(m, cin.anim, /*speed*/3.0f, now);              // slot1..3 = -1, clamps on the end
        for (each actor a) { inst_play_once(a.inst, a.anim, 3.0f, now); inst_recell(a.inst); }   // recell = make visible
        cam.mode = 0x80; cam.animInst = m; cam.letterbox = 1; cam.cut = 1;
        cin.state = 2; break;
    case 2: if ((cin.remain -= dt) > 0.5f) break;
        cin.timer = cin.remain; fade_out(cin.remain); cin.state = 3; break;
    case 3: if ((cin.timer -= dt) > 0) break;
        fade_in(0.5f); cin.timer = 0.5f; cin.state = 4; music_fade_in(0.45f);
        Mat C = mul(inverse(root_node(m, 0, 0.0f)), mul(root_node(m, cin.anim, 1.0f), inst_matrix(m)));
        player_reset(); player_set_facing(norm_xz(C.row2)); player_set_pos(C.t);
        cam.mode = 1 /*follow camera behind the player, cut*/; cam.letterbox = 0; break;
    case 4: if ((cin.timer -= dt) <= 0) memset-like reset (main = 0, nActors = 0, track = -1, state = 0); break;
    }
}
void cam_update_mode80(void) {                                        // after the instance clock of animInst
    Instance *I = cam.animInst; int a = I->slot[0];
    float frame = nframes(I, a) * (I->pos_ac / (duration(I, a) / 4096.0f));
    vec3 eye = xform(I, pos_track_cut(node_flags(I, 0x80),  a, frame));   // §4.1; xform = p*diag(scale)*rot + pos
    vec3 tgt = xform(I, pos_track_cut(node_flags(I, 0x180), a, frame));
    look_at(eye, tgt, UP);  vfov = 68.0f /* letterbox */;
}
```

## 10. Uncertain

1. `music->vt[0x98]` (`0x46cc20`) only sets `t0` to 0 and always returns 1; the loop and `var = t0·100` suggest a
   (console-?) variant where opening the stream takes time. In this PC build, `var` is always 0. Not verified live.
2. (resolved, measured live) `0x44edb0` yields net `C.t`: W2B boss intro, original end point (9911.3, −3194.0, 11017.7) = port.
   A uses the instance matrix **at the moment of the end**: if the script sends a teleport before the end (message 26;
   W2B does this 0.37 s before the end, to marker 535), then `0x44bf10` has already moved the instance position and the
   end point is relative to the teleport spot. W2B's anim 72 ends in the rest pose (T-pose) with the root 53 lower than
   in anim 0, i.e. below the floor; ground-snap then finds nothing and sets `y = feet + 43` (PERSO_DEATH.md §1.1), the
   step-up of the next frame puts him on the floor. The T-pose itself is never visible: from the teleport the script
   sends `1152` (`DURING 50`, VM.md) every tick for 0.5 s. Same for the corresponding step in `0x44e290` (not measured).
3. `0x44e690` (main loop `0x401d16`, **House only**, `App+0x68 == 0`): keeps Woody in the menu pose (action 0x49, message
   1141) and sets the Perso's transparency target (`+0xfc` -> `+0x6c`, INSTANCE.md par. 5; 0 = opaque) to **0 during the
   intro cinematic and 1.0 (invisible) otherwise**, speed 10000/s = instant. Whether the menu pose itself is visible
   (who sets `+0xfc` back to 0 then: `0x44d7e7`, `0x44d960`, `0x44b4bd`) has not been investigated.
4. (resolved) In every level checked, the main instance is the **Perso instance itself** (W1B `1200 [8, 1]`, W2D `[3, 1]`,
   K1A `[291, 2]`, K1R `[124, 18]` = each time the 1131 argument). Not every level has been checked.
5. The order of clock vs. camera within a single frame (could give 1 frame of delay) has not been investigated.
6. Drawing direction: 1130 orients the main instance with `d = −(P1 − P0)` in row 1, `0x44dda0` sets the Perso facing to
   `+(P1 − P0)`; assumed that the Perso instance matrix's row 1 = −facing (consistent), not verified.
7. 1080: exact pixel sizes/line spacing (`0x441980`, `0x441a50`) and the meaning of `0x20001` as an "empty line" are inferred.
8. The table in §6: which pose 72..78 exactly is (cheering, menu pose, …) is inferred from the callers, not from the animation data.
