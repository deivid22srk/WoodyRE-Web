# Script cameras of Woody.exe (supplement to CAMERA.md; working document)

All addresses from `Woody.exe` (image base 0x400000), static analysis (`out/disasm_full.txt`,
`python tools/drange.py START END`). Read `docs/CAMERA.md` first (CamMgr `[0x4c737c]`, follow camera mode 1,
transitions §6.1, rail camera mode 8 §6.2). This document describes the **remaining modes** and the
script coupling. Script statistics: `python tools/ekodisasm.py extract/Data/<LEVEL>/code`.

## 0. Summary

- **Mode 2 (message 510) and mode 4 (message 520) are mathematically identical**: a **stationary** camera at the
  `.ins` camera position that does a look-at every frame toward `targetInstance.pos + (0, f, 0)`. No smoothing, no
  collision, no own fov. The only difference: mode 4 enables **letterbox** (16:9, image shifted upward) and
  `0x459090` freezes the player (§3). Movement "toward" the fixed camera comes entirely from the
  generic transition (CAMERA.md §6.1, messages 560/570/580).
- **Argument order corrected** vs. CAMERA.md §4: the message is `510/520 (cam, f, target)` – first the
  y-offset `f` (int → float, `msg+0xc`), then the target instance (`msg+0x10`). E.g. W1A: `SEND 510 [316, 140, 0]`
  = camera 316 looks at Woody (slot 0) + 140 up; `SEND 520 [285, 0, 196]` = cinematic camera 285 looks at
  the origin of instance 196.
- **Routing**: cameras are ordinary script objects with tag `0x01` in the same slot table as instances
  (`level[0x6c][id & 0xffffff]`); messages < 1000 go to `obj->vtable[22](msg)`, for cameras = `0x498bd0`.
  `0x02000000 | i` is **not** a camera class but a **script variable** (FORMAT_INS §5); `SEND 1160
  [0x2000001, 0x1000073]` = (var 1, object 0x73).
- **Mode 0x20** (engine, message 1088 + parameters 1110; 8 levels) is a side-view camera for plane-locked
  sections (§4.2); **0x80** = camera driven by a model animation (§4.3); **0x200** = first person (§4.4); 0x10/0x40/0x100
  are used by no level. Message 800 has no argument: the camera registers *itself* as a volume actor (§5).
- Our loader (`src/level.c` `ins_load`, `Camera`/`cam_slots`) already reads the camera objects fully (position, id, TRAJ).

## 1. Routing script messages to the camera

### 1.1 Message record and dispatcher

Message record (0x34 B, queue of 32 records on the game object, `0x401200`/`0x401250`): `+0` id, `+4` (not
used by the camera handler), `+8` **receiver** (1st script argument after the id), `+0xc` 2nd argument, `+0x10` 3rd
argument, …, `+0x30` next. In the script: `PUSH id; PUSH receiver; PUSH arg…; SEND n`.

Dispatcher `0x401370(msg)`:

| id | destination | address |
|---|---|---|
| 7 | `0x4012f0` (cancel pending messages 0xc/0xd for the same object) | `0x401381` |
| < 1000 | `obj = level[0x6c][msg[+8] & 0xffffff]` (`[0x50944c]+0x6c`); `obj->vtable[0x58/4 = 22](msg)` | `0x401392..0x4013ae` |
| 1000..1499 | game handler `0x444870` (`this = [0x5d7afc]`) – incl. 1088, 1110, 1160 | `0x4013b5` |
| 1500..1599 | `0x46cca0` (`[0x5e823c]`) | `0x4013cf` |
| 1600..1700 | `0x467fa0` | `0x4013e7` |

The receiver is only masked with `& 0xffffff`; the tag byte is not checked. Scripts send camera messages
either with a **bare slot number** (`PUSH 316`) or with `0x01000000 | slot` – both land in the
same slot. For a camera object, vtable slot 22 = `0x498bd0` (vtable `0x4ac040`); for a camera
with TRAJ `0x498fd0` (vtable `0x4aa224`): that only intercepts 540 (`0x21c`) and forwards the rest to
`0x498bd0`. An instance (not a camera) that receives 500..800 ends up in its own handler and does nothing with it.

### 1.2 The `.ins` camera object

`.ins`: after all models `u32 ncameras`, then per camera `vec3 position; u32 id (0x01000000|slot); TRAJ`
(FORMAT_INS §1/§4; `tools/insparse.py` `parse_ins()["cameras"]`). Engine object 0x2c B (ctor `0x498b90`: vtable
`0x4ac040`, `+0x28 = 0`): `+4` id, `+8` flags (`& 0x1f == 3` = object type camera), `+0xc` position, `+0x1c`
world cell, `+0x28` TRAJ pointer. The object goes into `level[0x6c][slot]` **and** `level[0x40][slot]`, in the same
table as the instances. So a camera also has its own script object (same slot number in the `code` file)
and can send itself messages (see message 800, §5).

**Port status**: `src/level.c:207-216` reads `ncameras × {position, id, index, traj}` and builds `cam_slots[slot]`.
Nothing extra needs to be parsed; a script receiver `r` is a camera ⇔ `cam_slots[r & 0xffffff] != NULL`.
Slot 0 is the Woody instance in every level (`id 0x01000000`, model 0) → `target = 0` means "look at the player".

### 1.3 Usage in the level scripts (24 levels: K/S/W × 1A..3R + hubs KWS/SWS/WWS, W1B, W2A/B/D, W3A..D)

Count of `PUSH id; PUSH <cameraslot>` (receiver actually being an `.ins` camera):

| id | count | meaning |
|---|---|---|
| 580 | 174 | transition mode (1 smooth / 2 cut) – before almost every mode switch |
| 500 | 80 | back to the follow camera (behind the player) |
| 520 | 62 | **mode 4**: fixed cinematic camera with letterbox |
| 570 | 51 | transition duration (cs) |
| 660 / 670 / 680 / 650 | 29 / 27 / 19 / 14 | back to the remembered follow-camera distance/height / follow camera height / distance / remember them (once) and re-seat the follow camera; R levels and 9 others (W1A, W2D, W3A, W3B, K1A, K3A, S1A, S2A, S3A), CAMERA.md §4 |
| 540 | 23 | mode 8 rail camera |
| 710 | 9 | auto-zoom on (with the boss rail cameras) |
| 510 | 8 | **mode 2**: fixed camera without letterbox (player stays controllable) |
| 560 | 4 | transition at speed |
| 800 | 3 | camera registers itself as a volume actor (§5) |
| 501 | 2 | follow camera in front of the player |
| 700 | 1 | zoom back to 1.2 |
| 530, 550, 590, 600, 690 | 0 | **not used** by scripts (530 = mode 0x10, 550 = first person: engine only) |

Also (game messages, no camera receiver): **1110** in W1A 29×, K1A 29×, S1A 29×, W1B 24×, W2B 51×, W2D 84×,
W3C 124×, W3D 114× (parameters of mode 0x20, §4.2) and **1088** (start mode 0x20, §4.2).

Typical sequences:

```
; W1A obj: gameplay hint, player keeps walking (mode 2)      ; W1A cinematic (mode 4), hard cut
SEND 570 [316, 200]      ; transition 2.0 s                   DELAY 60:
SEND 580 [316, 1]        ; smooth                                SEND 580 [285, 2]        ; cut
SEND 510 [316, 140, 0]   ; cam 316 -> Woody + 140                SEND 520 [285, 0, 196]   ; cam 285 -> inst 196 + 0
...  (leaving the volume)                                     DELAY 160: ... animations/sound ...
SEND 570 [316, t]; SEND 580 [316, 1]; SEND 500 [316]          SEND 580 [285, 2]; SEND 500 [285]
```
Hubs (WWS/KWS/SWS): that `580 cam 2; 520 cam …` (13× in WWS) belongs to the **area-gate cutscenes** (object 258:
`26 [0, 260, 2]; 580 [259, 2]; 520 [259, 0, 11]`), not to the level doors. Note the order: message 26 comes
before 580/520, and the teleport itself does a hard cut to the follow camera (`0x458f90`, PERSO_DEATH §2.1). That cut
must therefore happen at its proper place in the message stream — if the port defers it to the next frame, it wipes out
the gate camera the script sets right after it, and the cutscene is gone before a single frame is drawn. A level door sends **no
camera message at all**: `1081 [level]; 1040 [door, 17]; 1602 …; 3 [marker, …]`. The camera there comes from the animation itself
(mode 0x80, §4.3). A pair of doors within the hub is similar: `1040 [302, 17]` … `DELAY 150` … `1040 [303, 18]`.
Rail: `580 cam 1|2; 540 cam d` … `500 cam`.

## 2. Mode 2 (message 510) and mode 4 (message 520)

### 2.1 Handler and parameters

`0x498c50` (510) / `0x498c9f` (520), `this` = camera object, `p = CamMgr+0x43c` resp. `CamMgr+0x470`:

```c
p->pos    (+4..+0xc) = cam->pos (+0xc..+0x14);          // 0x498c55..0x498c6d
p->f      (+0x20)    = (float)(int)msg[+0xc];            // 0x498c70  fild/fstp   – y-offset of the look point
p->target (+0x10)    = level[0x6c][msg[+0x10] & 0xffffff];   // 0x498c76..0x498c8b – object pointer, read every frame
SetMode(1 /*510*/ or 2 /*520*/, 0);                      // 0x41f410
```

`p+0x14` (vec3) = look-target base (filled by the update), `p+0x20` = f. `CamMgr_Update` (`0x41eff0`) then reads
`target (+0x278) = p+0x14`, `lookOffset (+0xc4) = (0, p+0x20, 0)` (CAMERA.md §2) – those are exactly the
quantities the transition (§6.1 there) interpolates.

### 2.2 Init

`0x41e520` (mode 2) / `0x41e560` (mode 4): `sub->prev (sub+0xa0) = CamMgr state (+0x140, 0x27 dwords)`;
`sub->params (sub+0x27c) = p`; "init" = `0x462c60` = **empty function** (`ret`). Mode 4 first calls `0x41f8d0`
(letterbox 1, §2.4). No state, no pre-simulation.

### 2.3 Per-frame update `0x4254c0` (mode 2) = `0x425810` (mode 4), byte-for-byte identical on addresses after

```c
void Fixed_Update(Sub *s, float dt) {                // 0x4254c0 / 0x425810
    s->dt (+0x274) = dt;                             // unused otherwise
    s->P (+0x280) = s->params->pos;                  // 0x4254ea..0x42550a
    if (FindCell(s->P) == -1)                        // 0x40aba0([0x4c93b0]+0x14, x,y,z) - never true, CAMERA.md 3.3
        s->P = s->prev.pos (sub+0x130 = sub+0xa0+0x90);   // camera outside the world -> keep the previous camera position
    Fixed_LookAt(s);                                 // 0x4255e0 / 0x425930
    s->state.pos (+0x94) = s->P;
    s->state = [I | -P] * [R | 0];                   // 0x437710, T = -P (+0x28..+0x30), 0x437740(state, &s->R (+0x28c))
    s->prev (+0xa0) = s->state;  (+0x1d8 = +0x13c: second shadow copy)
}
void Fixed_LookAt(Sub *s) {                          // 0x4255e0 / 0x425930
    up = (0, -1, 0);                                 // 0x4255fc: 0x43ff80(0, -1.0, 0)
    T  = s->params->target->pos (+0xc);              // INSTANCE ORIGIN (inst+0xc..+0x14), no marker/bbox
    s->params->look (+0x14) = T;                     // -> CamMgr+0x278 (target point for transition/shake/auto-zoom)
    d  = T - s->P;  d.y += s->params->f (+0x20);     // 0x44005c, 0x42566b
    fwd   = normalize(d);                            // (not normalized if |d| == 0)
    right = normalize(up x fwd);                     // 0x4400f0(up, fwd)
    up2   = normalize(fwd x right);
    R (+0x28c): R[i][0] = right[i], R[i][1] = up2[i], R[i][2] = fwd[i];   // same convention as CAMERA.md 3.7
}
```

Properties:

| question | answer |
|---|---|
| camera position | exactly `cam->pos` from the `.ins`, constant; snapshotted at the moment of the message (cameras don't move anyway) |
| look point | `target->pos + (0, f, 0)`; `target->pos` = `inst+0xc` = instance origin, **read again every frame** (follows moving targets with no lag) |
| `f` | integer from the script, 1:1 world units upward (W1A: 140 at Woody = same height as the follow-camera look point; 0 for props) |
| smoothing/lag | none in the mode itself; only the transition (position + look offset linear in `t`, CAMERA.md §6.1) |
| fov/zoom | nothing mode-specific: global zoom `CamMgr+0x678` (1.2; messages 690/700/710). With 710 (auto-zoom), a fixed camera zooms in on distant targets, see note |
| collision | none; the only check is "position outside the world → keep the previous camera position" |
| camera TRAJ | ignored in modes 2/4 (only 540 uses it) |

Note on auto-zoom: `0x41f690` uses `CamMgr+0x278`. In modes 2/4 that is the **target point `target->pos`** (without f),
because `CamMgr_Update` overwrites `+0x278` with `p+0x14` after `0x41f960`. So `d = |target->pos − P|`,
`zoom = 1.1 + (0.2 − 1.1)·clamp((d − 300)/2700, 0, 1)`.

### 2.4 Letterbox (`0x41f8d0`, `0x41f910`, `0x41f940`)

There are **no drawn bars and no animation**: letterbox = a different projection scale + a smaller viewport,
switched directly (in a single frame). The "bars" are the undrawn part of the screen (cleared to black).

| function | effect | callers |
|---|---|---|
| `0x41f8d0` Letterbox1 | only if `CamMgr+0x66c & 8` (ctor `0x41dd42` sets `|= 0x18` → always true): `+0x684 = +0x680; +0x680 = 1; sy (+0x674) = 0.5625; sx = 1.0` | mode-4 init `0x41e565`; SetMode(7) (mode 0x80) if `+0x618 & 2` (`0x41f5ab`) |
| `0x41f910` Letterbox2 | `+0x684 = old; +0x680 = 2; sy = 0.5625; sx = 1.0` | `CamMgr_Update` **every frame while mode == 4** (`0x41f338`) |
| `0x41f940` Letterbox0 | `+0x680 = 0; sy = 0.75; sx = 1.0` | ctor, reset `0x41dfb2`, SetMode(5) (mode 0x20, `0x41f57b`), SetMode(7) without the flag, and `CamMgr_Update` when the previous mode was 4/0x80 and the current one isn't (`0x41f34f`, `0x41f36d`) |

Consequence for mode 4 (letterbox 2, `0x41f690`, CAMERA.md §5.1): `aspect = 1/0.5625 = 16:9`, `Hview = 4H/(3·aspect) =
0.75·H`, viewport center `cy = (H + Hview)/4`, height `Hview + 0.5`. At 640×480: image strip y = 30..390
(30 px black above, **90 px black below** – room for subtitles/dialogue); `tan(vfov/2) = 1.2·0.5625 = 0.675`
→ vfov 68.0°, hfov stays 100.4°. Letterbox 1 (mode 0x80) centers the strip: `cy = (H+1)/2` → y = 60..420.
During a smooth transition into mode 4, the letterbox is already active from the first frame; when returning to
mode 1 it disappears on the first frame of the new mode (even if the transition is still running).

### 2.5 Engine use of mode 2: `0x41fb50(m, &pos, inst)`

`p43c.pos = *pos; p43c.target = inst; SetSpeed(100.0)` (`0x41f9b0`: duration = distance/100 s); `SetTransition(1)` (smooth);
`p43c.f = CamMgr+0x348` (= y of the follow camera's look offset, ≈ 140 − drop); `SetMode(1, 0)`. Only caller:
`0x459030(ctl, &pos)` = `0x41fb50(ctl->CamMgr, pos, ctl->Perso)`, used by Perso state 7 "disappearing"
(water/pit, PERSO_MOVE `0x44c308`): the camera stays fixed at a point and watches the falling player.
(The `0x46496a`/`0x464aab` mentioned in CAMERA.md §8.9 are not callers but coincidental address hits.)

## 3. Input and control during a script camera (`0x459090`, table `0x459934`)

| mode | what `0x459090` does every frame | consequence |
|---|---|---|
| 2 (idx 1, `0x45915d`) | only: button 0xa just pressed → sound 9 ("not allowed") | **player stays fully controllable** |
| 4 (idx 2, `0x459185`) | `0x44a650(Perso, Perso->vtable[34]() /*&own pos*/)`; `Perso+0x690 = 1` | **player frozen** |
| 8 (idx 3) | see CAMERA.md 3.1; controllable | |
| 0x20 (idx 5) | nothing here; input runs via `0x459c70` (§4.2) | 2.5D control |
| 0x80 (idx 7) | `+0x618 |= 2`, pass position/instance through | animation-driven |
| 0x100 (idx 8), 0x200 (idx 9) | debug keys resp. mouse/arrow keys → look angle; `Perso+0x690 = 1` only in 0x100 | |

* `0x44a650(Perso, &p)` (not in state 5): `Perso+0x1f4 = p` (= own position → net effect "stay in place"), `0x462990`
  (re-fix position/ground), and `vtable[4]()` (reset) on the anim/movement objects `+0x494` and `+0x498`.
* `Perso+0x690` = **frozen** (PERSO_FRAME §: `if (!p->frozen) {…}` skips input, attacks, jumping and movement;
  `0x44b64a`, `0x44b68b`, `0x44b6b5`; back to 0 at the end of the Perso update, `0x44b938`). `0x44b480` (state 6) is
  also skipped. In mode 4, the player thus stands still and there is **no input**; animations the script starts still play.
  Also: with `CamMgr+0x138 == 2`, `0x463ec8` always chooses animation 1 (PERSO_MOVE l. 281) → Woody is in idle.
* **Camera-relative control** doesn't care about the mode: the Mover takes `camDir = normalize(pos − camPos)` (xz) with
  `camPos = Perso+0x2fc = CamMgr+0x140+0x90` (`0x44c000`, PERSO_FRAME l. 98/220) = the **final camera position
  of the previous frame, including transition**. In mode 2 (and 8), "forward" is thus "away from the fixed/rail camera"; during
  a smooth transition, the control frame turns along with the traveling camera.

## 4. Remaining modes

### 4.1 Overview: who switches what on? (all 24 callers of `SetMode` `0x41f410`)

| mode (idx) | init / update | who | used by scripts? |
|---|---|---|---|
| 1 (0) | `0x41e450` / `0x424760` | 500/501; engine: `0x445b66`, `0x44e634`, `0x454227` (SaveAuto), `0x4562de`, `0x458ffb`, `0x45912b` (out of first person), debug `0x402ad3`, `0x402b33` | yes (82×) |
| 2 (1) | `0x41e520` / `0x4254c0` | 510; engine `0x41fb50` (§2.5) | yes (8×) |
| 4 (2) | `0x41e560` / `0x425810` | 520 | yes (62×) |
| 8 (3) | `0x41e5b0` / `0x421570` | 540 (TRAJ camera) | yes (23×) |
| 0x10 (4) | `0x41e630` → `0x420350` / `0x4203c0` | message 530 only | **no** (0×) |
| 0x20 (5) | `0x41e4e0` → `0x424b30` / `0x424bf0` | engine: message **1088** → `0x459960` (`0x4599a8`, `0x459bba`) | yes, via 1088 (46×) + 1110 |
| 0x40 (6) | `0x41e5f0` → `0x420cb0` / `0x420cf0` | no caller found with index 6 (possibly debug path `0x402a84` with a variable index) | no |
| 0x80 (7) | – / inline `0x41f1ee` | engine: Perso state switch `0x44dfad`, cinematic object `0x44ed6e`; always with a cut | indirect |
| 0x100 (8) | `0x41e670` → `0x41ffe0` / `0x420010` | debug key `0x402c1d`; forces a cut | no |
| 0x200 (9) | `0x41e6c0` → `0x425b60` / `0x425b80` | message 550 (0× used); engine `0x459050` (Perso state 3), forces a cut | indirect |

### 4.2 Mode 0x20 = side view ("2.5D") for plane-locked sections (messages 1088 + 1110)

**Start**: message 1088 `(inst, v)` → `0x445001` → `Perso::0x459960(inst, v)`:

```c
if (perso->planeMode (+0x4ec)) { if (cam->modeIdx != 5) { SetTransition(2 /*cut*/); SetMode(5,0); } return; }
perso->+0x4e8 = inst; perso->planeMode = 1;
marker(inst, type 0, n 0) -> 2 points A,B (0x42f6b0);  d = xzNormalize(B - A); (|d| < 0.01 -> (1,0,0))
perso->M.dir (+0x3bc, +0x3a4, +0x398) = d;  0x44a650(perso, &pos); 0x462990(perso);
if (v == 1) { p->side (CamMgr+0x61c) = 0; perso->+0x4ed = 1; perso->+0x4ee = 0; }
else        { p->side = 1;                perso->+0x4ed = 0; perso->+0x4ee = 1; }
p->+0x28..+0x44 = defaults (1000, 300, 340, 500, 0, 400(+0x3c), 700(+0x40), 200);      // 0x459b19..0x459b5f
p->pos (+8, CamMgr+0x624) = perso pos;  p->dir (+0x14, CamMgr+0x630) = d;
SetTransition(2);  SetMode(5, 0);                                                     // + Letterbox0 (0x41f57b)
plane: n = ..., perso->+0x4f0..+0x4fc = (n, -n·A)  // 0x459bbf..0x459c57: the player is kept on the vertical plane through A,B (0x459eb0)
```
End: Perso state switch `0x44de44` sets `+0x4ec = 0`; the script switches the camera back with 580/500.
Also the respawn after death (`0x445930` → `0x44a810` → Reset `0x44ab20`, `0x44ad22`) sets `+0x4ec = 0`, and `0x458f90` cuts
back to the follow camera: the side view only returns once the script sends 1088 again (PERSO_DEATH §3.4).

**Per frame**, `Perso::0x459c70` (only if `+0x4ec`) fills the block `p = CamMgr+0x61c`:
`p->pos (+8) = player pos`; `p->dir (+0x14)` = walking direction along the plane; `p->h (+4)` = **0** if action 2 (↑) is held,
**2** if action 3 (↓) or 5 (duck), otherwise **1**; `p->flip (+0x20)` = 1 on the frame the player flips left↔right
(actions 0/1, `+0x4ed/+0x4ee`; only if mode index 5, not in state 2, `+0x5b4 == 0`, `+0x50c == 0`).

**Update `0x424bf0(dt)`** (sub `S = [CamMgr+0x130]`, `S+0x28c` = A "look-ahead", `S+0x290` = H height, `S+0x294` = Lat side distance):

```c
if (!in_world(S->P (+0x298))) S->P = S->prev.pos;
dir  = normalize(p->dir);  side = normalize((0,-1,0) x dir);
p->Htarget (+0x24) = (p->h == 0) ? p->+0x34 /*500*/ : (p->h == 1) ? p->+0x30 /*340*/ : p->+0x38 /*0*/;
ramp(&S->A,   p->+0x2c /*300*/,  rate p->+0x40 /*700 u/s*/);      // 0x425300
ramp(&S->H,   p->Htarget,        rate p->+0x3c /*400 u/s*/);      // 0x425220
ramp(&S->Lat, p->+0x28 /*1000*/, rate p->+0x44 /*200 u/s*/);      // 0x4253e0
   // ramp: on a new target: T = |target - start| / rate; then linear start + (target-start)*(t/T), t += dt, until t/T > 1
side *= (p->side == 0) ? -S->Lat : +S->Lat;
ahead = (S->A < 0.001) ? dir : (dir.x*S->A, 0, dir.z*S->A) * s;   // 0x4250b0; s = ±blend (S+0x340, S+0x344):
   // on p->flip: sign flips and the blend runs linearly from -current to 1 over (|ahead| / p->+0x40)*(1 - start) s
C = p->pos + ahead + (0, S->H, 0);                               // look point: A units ahead of the player, H above
S->P = C + side;                                                  // camera: same height, Lat units to the side of the plane
S->lookOffset (+0x280) = C - p->pos;   -> CamMgr+0xc4;  CamMgr+0x278 = p->pos
R = lookAt(S->P -> C, up (0,-1,0));  state = [I|-P]*[R|0];
```
So a horizontally-looking side camera at 1000 units from the plane, looking 300 ahead in the walking direction, with
↑/↓ putting the camera 500/0 instead of 340 above the player. No collision.

**Which side (settled).** `0x4400f0(a, b)` computes `a × b` with `a` = the first argument (the last push): `x = a.y·b.z − a.z·b.y`
etc. At `0x424cee` the pushes are `dir` then `up`, so `side = up × dir = (0,−1,0) × d = (−d.z, 0, d.x)`, scaled by `−Lat` when
`p->side (CamMgr+0x61c) == 0` and `+Lat` when it is 1 (any other value leaves it a unit vector). `0x459960` takes `(inst, v)` in
message order (`0x445001`: `msg+8` = inst, `msg+0xc` = v; the second argument is compared with 1 at `0x459ae9`) and writes
`+0x61c = 0` for `v == 1`, else 1; `d` is `xzNormalize(B − A)` of marker (type 0, n 0) (`0x4599e4`: `[esp+0x3c] − [esp+0x30]`).
Result: the camera stands at `C + Lat·(d.z, 0, −d.x)` for `v == 1` and `C + Lat·(−d.z, 0, d.x)` for `v == 2`; on screen, `A → B`
points **left** for `v == 1` and **right** for `v == 2` (screen right = `(0,−1,0) × fwd`). The port had exactly this. Geometric
check over all 41 distinct `1088 (inst, v)` pairs of the 8 levels that use them (`WOODY_SIDECHECK`, five points along `A..B`,
camera 340 up and 1000 aside, ray to the body at +100): with this sign 5 of 205 lines of sight are blocked by the world, with the
opposite sign 114 (W2B, W2D and W3C 5/5 almost everywhere). Rendered: W1A door 164 → the side section of 144 (`v` 2) and W1B door
392 → 408 (`v` 1, `WOODY_SETVAR="1.0 42 1" --pos -8500 400 -16806 --yaw -90`) both show the walkway with its bonuses in the
open, the rock behind it.

**Message 1110 `(n, v)`** (`0x444b9d`, jump table `0x445754`, `p = CamMgr+0x61c`, v as float):

| n | field | meaning | default |
|---|---|---|---|
| 1 | `p+0x34` | height on ↑ | 500 |
| 2 | `p+0x30` | normal height | 340 |
| 3 | `p+0x38` | height on ↓/duck | 0 |
| 4 | `p+0x2c` | look-ahead A | 300 |
| 5 | `p+0x28` | side distance | 1000 |
| 6 | `p+0x40` | speed of A (and of the flip blend) u/s | 700 |
| 7 | `p+0x3c` | speed of H u/s | 400 |
| 8 | `p+0x44` | speed of the side distance u/s | 200 |
| 9 | – | all eight back to default | |

(CAMERA.md §1.2 gives the defaults in offset order `+0x28..0x44` = 1000, 300, 340, 500, 0, 400, 700, 200; note that
`0x459b55/0x459b4b` set `+0x3c = 400` and `+0x40 = 700`.) Usage: W1A/K1A/S1A 29×, W1B 24×, W2B 51×, W2D 84×, W3C 124×,
W3D 114×; typically `1110 2 150; 1110 1 500; 1110 3 0; 1110 7 200` around a volume, and `1088 inst 1|2` on entry.

### 4.3 Mode 0x80 = camera from an instance's animation (doors and cinematics)

`CamMgr_Update` `0x41f1ee`: `inst = CamMgr+0x5d4`; if `0x42feb0(inst, inst->anim (+0xb0))` (the model has a node of
kind **0x80** with a track for this animation): `0x42fa40(inst, &state, &pos, &CamMgr+0x5d8)` → `0x42fa80`: looks for the
nodes of kind **0x80 (camera)** and **0x180 (camera target)** in the model (`inst+0xf8`), evaluates their position tracks
(`0x43a660`) at the current animation time (in **keyframes**: `nframes · t`), transforms with the instance and builds the
look-at; `state.pos = pos`, `CamMgr+0xc4 = +0x5d8`. Note: in this mode, `+0xc4` holds an **absolute world position**
(the transformed target point), not an offset as in modes 1/2/4, and `+0x278` is not written here.
The search loop at `0x42fb39` has **no end test**: it only stops once both nodes are found, so a model with an
eye node but no target node runs off the end of the node table. If `0x42feb0` fails, the whole branch is skipped and the
previous eye and target stay put – the camera **freezes** on that image instead of snapping back.

Two triggers, both with a cut (`0x41f9f0(2)`) and `SetMode(7, 0)` (the first parameter is a **bit shift**: mask
`1 << 7`):

* **the scripted Perso action itself**, `0x44df67..0x44dfb2` – that is the *tail of `0x44dda0`*, not a separate
  state switch (correction to an earlier reading of this document). It **clears** `CamMgr+0x618` bit 1
  (`0x44df92 and edi, 0xfffffffd`): so the door camera **never** has letterbox. The test on `Perso+0x558` is always
  true (set 24 instructions earlier, only cleared by the Perso reset `0x44ac7e`) and doesn't need to be ported.
* **the cinematic object**, `0x44ed3f` + `0x44ed53 or esi, 2` – that **sets** the bit, so it does have letterbox
  (`0x41f5ab` reads `CamMgr+0x618 & 2`). The per-frame handler `0x4598c4` restores the bit as long as mode 0x80 runs,
  so both triggers need to write it.

The Woody model has exactly two such nodes (`insparse`: `80:2`): index 140 with flags `0x080` (eye) and 141 with
`0x180` (target), both top-level. They carry a track on animations **17, 18, 41..47, 49, 50, 53..78, 80..84** –
so on the two door actions and on the cinematics. In animation 17, the eye is at local `(−371, −78, 178)`: 371 units
**beside** Woody's own axis at head height, looking at a point on that axis. That's the side view where you see him walk
into the door. Both animations are 900 frames / 4.5 s and run at speed 3, so 1.5 s – exactly the `DELAY 150`
the hub scripts put between `1040 [door, 17]` and `1040 [other door, 18]`.

**Leaving.** Action 18 ends in `0x44db60` at `0x44e5a0`: `SetTransitionDuration(0.5)`, `SetTransition(1)` = *blend*,
`SetMode(0, 0)` = follow camera – unless the side view has turned back on in the meantime (`Perso+0x4ec`). Action 17 **never**
restores the camera: it stays on the last track frame until the level changes or the teleport `0x458f90` hard-cuts.

### 4.4 Mode 0x200 = first person (Perso state 3)

`0x459050`: `0x44c080(Perso, p540, 1)` (eye position/direction in `p = CamMgr+0x540`), `p+0x28 = p+0x2c = 0`, cut,
`SetMode(9)`. Per frame `0x459090` (idx 9) puts the mouse deltas or ±5 (arrow keys) into `p+0x28/+0x2c`; update `0x425b80`:
delta clamped to ±64, `yaw (p+0x78) ∓= min(|dx|·dt·0.19635 (0x4aa1e4 = π/16), 0.31416 (0x4aa1e0 = π/10))`, likewise
`pitch (p+0x7c)` with `p+0x2c`; pitch clamped to ±72° by `p+0x80/+0x84`, the yaw pair `p+0x88/+0x8c` = (−1, 1) fails the clamp guards
(free yaw); rotation = Rx(pitch) · base `p+0x54` · Ry(yaw) (`0x437940`, `0x437970`, `0x440b40`). Corrected and worked out in
[PERSO_LOOK.md](PERSO_LOOK.md) §3. The look direction is written back to the Mover (CAMERA.md 3.1). Leaving:
`0x45910e` → cut to the follow camera. Message 550 only does `SetMode(9,0)` and appears in no script.

### 4.5 Modes 0x10, 0x40, 0x100 (not used by the levels)

* **0x10** (message 530, 0× in scripts): `0x420350` takes over the current state, `p404+0x20 = camera position`,
  `p404+0x34 |= 1`, `p404+0 = p404+4 = 150.0`; update `0x4203c0` runs over a TRAJ (`p404+0x30`, points stride 0x10)
  with `0x438210` (nearest segment) and a filtered speed (`×1.5` `0x4aa184`, steps of 1/60 s `0x4a9990`,
  damping 0.2) – an older rail variant. No one fills in `p404+0x30` in the code found → without a TRAJ the update does nothing
  (`0x4203f4`/`0x4203fc` → return). Do not port.
* **0x40**: quaternion camera (`0x420cf0`), no caller. **0x100**: free debug camera (key in `0x402940`).

## 5. Message 800: camera as a volume actor (`CamMgr+0x664`)

`0x498d93`: `CamMgr+0x664 = level[0x6c][msg[+8] & 0xffffff]` – note: `msg+8` is the **receiver itself**, there is no
extra argument (correction to CAMERA.md §4 "800 cam, inst"). Scripts: `DELAY n: SEND 800 [0x0100018b]` from the
camera's own script (W2B obj 395, K2R obj 483, S2R; 3× total; all three `.ins` cameras without TRAJ).
Per frame (end of `0x41eff0`, `0x41f379..0x41f3cf`): `obj->pos (+0xc) = CamMgr.state.pos`; `0x4077f0(obj, 0)` (re-determine
world cell); `0x434740(&pos, -1, obj)` → volume tests with this object as the actor ⇒ script events "camera enters/leaves
volume" (messages 0x65/0x66/0x67 with `actor_id = obj+4`, EVENTS.md l. 59-65, 132-140). The `.ins` position of
that camera object gets overwritten in the process; so don't also use such a camera for 510/520. In the other levels,
`+0x664` is NULL and nothing happens.

## 6. Messages 1160 and 1110 (game handler `0x444870`)

* **1160 `(a, b)`**: `game+0x8c = a`, `game+0x90 = b`. **Not a camera message.** `a` = script variable (`0x02000000|i`),
  `b` = script object. The engine writes `var a = 1` when the level-transition timer `game+0x98` crosses 0
  (`0x405029`, `0x4051b9`) and `var a = 4` + cancels the timers of object `b` (`0x444380`, `0x4441d0`) on level
  leave/restart (`0x405143`) – see EVENTS.md §4.2. House: `SEND 1160 [0x2000001, 0x1000073]` = "signal in var 1 /
  object 0x73 when the transition is done".
* **1110 `(n, v)`**: parameters of mode 0x20, see §4.2.

## Recipe (C-like pseudocode)

```c
/* state */ int mode; Vec3 fixPos; int fixTarget; float fixF;       /* + transition from CAMERA.md 6.1 */

void cam_msg(int id, int recv, int a1, int a2) {                    /* recv & 0xffffff must be a cam_slot */
    Camera *c = cam_slots[recv & 0xffffff]; if (!c) return;
    switch (id) {
    case 510: case 520:
        fixPos = c->position; fixF = (float)a1; fixTarget = a2 & 0xffffff;
        set_mode(id == 510 ? 2 : 4);          /* set_mode: start a transition if trans_mode == smooth (freeze old pos + lookOffset) */
        break;
    case 500: case 501: set_mode_follow(id == 501); break;
    case 570: trans_dur = a1 * 0.01f; dur_from_speed = 0; break;
    case 560: trans_speed = (float)a1; dur_from_speed = 1; break;
    case 580: trans_cut = (a1 == 2); break;
    }
}
void cam_fixed_update(float dt) {                                   /* mode 2 and 4 */
    Vec3 P = fixPos;  if (!in_world(P)) P = prevCamPos;
    Vec3 T = instance_origin(fixTarget);                            /* inst+0xc, every frame */
    target = T;  lookOffset = (Vec3){0, fixF, 0};                   /* for transition / shake / auto-zoom */
    view = lookAt(P, T + lookOffset, up=(0,1,0));                   /* == engine up (0,-1,0) with y-down camera space */
    if (transition_active) blend(): pos = from + (P - from)*t; look = T + lerp(lookFrom, lookOffset, t);
    if (mode == 4) { sy = 0.5625f; viewport = {0, H*0.0625, W, H*0.75}; /* y 30..390 at 480 */ freeze_player(); }
    else           { sy = 0.75f;   viewport = full; }
}
```

## Recipe mode 0x20 (side view), short

```c
/* on 1088(inst, v): d = xz direction of marker type 0 of inst; side = (v == 1) ? -1 : +1; A = 300; H = 340; Lat = 1000; cut */
Vec3 sideV = normalize(cross((Vec3){0,-1,0}, d));           /* = (-d.z, 0, d.x); sign settled in 4.2 */
Htarget = up_held ? h_up : (down_or_crouch ? h_down : h_norm);      /* 500 / 0 / 340, via 1110 n = 1 / 3 / 2 */
A = ramp(A, a_target, 700*dt);  H = ramp(H, Htarget, 400*dt);  Lat = ramp(Lat, lat_target, 200*dt);   /* linear */
C = playerPos + (d.x*A*s, H, d.z*A*s);                      /* s = +1/-1 walking direction, blended linearly on flip */
P = C + sideV * (side * Lat);   view = lookAt(P, C, up=(0,1,0));
```

## Uncertain

1. Letterbox: whether the black of the bars comes from an explicit clear or from simply not drawing outside the viewport was not
   checked (renderer `0x4843b0`); it is certain that there is no bar animation in the CamMgr.
2. Mode 0x20: (a) ~~the sign of `side`~~ settled statically and geometrically (§4.2); (b) the exact flip blend (`S+0x340..+0x350`, `0x4250e8..0x42520e`) is shown simplified;
   (c) the plane calculation in `0x459bbf..0x459c57` (`0x41af10`, `0x4239f0`) is not spelled out; (d) K1A sends a series of
   1110 messages right **before** 1088, while `0x459960` resets the defaults on a new start – whether those 1110 values
   are then lost, or whether `+0x4ec` is already set at that point, was not investigated.
3. Mode 0x200: settled in PERSO_LOOK.md §3.3 — `p+0x78` is the yaw (`0x437970` = RotY, multiplied on the right: world axis),
   `p+0x7c` the pitch (`0x437940` = RotX, on the left: the Perso's own side axis).
4. Mode 0x80: the track evaluation `0x42fa80` was only read at a high level (node kinds 0x80/0x180, `0x43a660`);
   the meaning of `Perso+0x558` and `CamMgr+0x618` bit 1 (letterbox on cinematics) is derived from context.
5. Mode 0x10: writer of `p404+0x30` (TRAJ) not found; treated as dead since no script sends 530.
6. Message record `+4`: not determined (sender or argument count); irrelevant for the camera.
7. 510/520 with a target slot that is empty (placeholder `0x4aa28c`): the update then reads `placeholder+0xc`
   (undefined/0); not encountered in the data.
