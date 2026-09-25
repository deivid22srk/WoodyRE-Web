# Camera logic of Woody.exe (working document, updated incrementally)

All addresses are from `Woody.exe` (image base 0x400000). Disassembly: `out/disasm_full.txt`,
`python tools/drange.py START END`. Vector helpers: see §A.

## 0. Summary

- **Follow camera (mode 1, §3)** is a "lazy follow": it does not actively turn to stay behind the player. Look target = player + (0,140,0)
  (unlagged). Position: xz distance to `T = player + (0,120,0)` is kept in the band 300..400 (catching up 433·d/400
  units/s, backing off 66.7·400/d units/s, ×10 if the player looks toward the camera); height approaches `T.y + 180` at `6·dt`.
  Button 0xa (and Perso states 1/4/8) pulls it at `3·dt` (first 0.5 s `7·dt`) toward the point 400 behind the player.
  Collision: sphere r = 40 with push-out + line-of-sight veto + breadcrumb trail if the player disappears behind geometry (3.6).
- **Projection (§5)**: software T&L; `tan(hfov/2) = zoom·sx = 1.2` → hfov 100.4°, vfov 83.97° (4:3); camera space x right,
  y down, z forward; right-handed world, no mirroring; look-at with up = (0,−1,0) ≡ `gluLookAt(..., up=+y)`.
- **Transitions (§6)**: linear interpolation (position + look offset) from the frozen old camera to the moving
  new one, duration 2.0 s default / message 570 (s) / 560 (distance÷speed); 580 selects smooth or cut. Mode 8 (TRAJ) is a
  **rail camera** at a fixed distance from the player, not a time-based path. `0x41fbd0` is camera shake (±5·min(t,3) units).

### 0.1 Recipe: follow camera in C (per frame; P = camera position, persistent; `pos`, `look` = player position/look direction)

```c
T = pos + (0,120,0);  L = pos + (0,140,0);
if (behind /*button or state 1,4,8*/) { D = T - normalize(look)*400;  mv = (D - P) * dt * (quick ? 7 : 3); }
else {
    v = T - P; v.y = 0; d = len(v); vn = v/d;  mv = 0;
    k = dot(xzn(L - P), -xzn(look)) > 0.7f ? 10 : 1;          /* player comes toward the camera */
    if (d < 300)      { s = (400/d)*dt*k*66.6667f; if (d + s > 300) s = 300 - d;  mv = -vn*s; catchUp = 0; }
    else if (d > 400) { s = (d/400)*dt*433.333f;   if (d - s < 400) s = d - 400;  mv =  vn*s; catchUp = 1; }
}
if (rising && P.y - T.y < 300) mv.y = T.y - Tprev.y;  else if (rising) mv.y = 0;
else                           mv.y = (T.y + 180 - P.y) * 6 * dt;
N = P + mv;  hit = sweep_sphere(P, N, r=40, step=35, &N2);     /* push out of walls; corr = N2 - N */
if (hit) N = N2;
if (!in_world(N) || ray_blocked(N, T)) N = P;                    /* line-of-sight veto */
if (ray_blocked(P, T) /*start of the frame*/) follow_breadcrumbs(); /* path {P, Tprev, T,...}, u += 0.04/frame */
P = N;
drop = (rising||falling) ? min(drop + 450*dt, 150) : drop*0.94f;   Lk = L - (0,drop,0);
view = lookAt(P, Lk, up=(0,1,0));  proj = perspective(fovy=83.97°, aspect=4/3);  Tprev = T;
```
Start/reset (message 500): `P = pos − look + (0,100,0)`, then 1 step dt = 0.1 and 100 steps dt = 0.04 in behind mode.

- The camera is **not** a script instance class. There is one global **camera manager** `CamMgr`
  at `[0x4c737c]` (ctor `0x41dca0`, size ≥ 0x698, created from `0x458ec0`, destroyed
  by `0x41ddb0`). It holds 9 sub-cameras ("modes") selected as bit flags
  (`CamMgr+0x134`), plus one camera instance (`CamMgr+0x664`) that receives the final position (for
  sound/visibility).
- The `.ins` cameras (vtable `0x4ac040`, with TRAJ `0x4aa224`) are thin script objects: their
  message handler (`0x498bd0`, TRAJ variant `0x498fd0`) translates the script messages 500…800
  into calls on the CamMgr (mode switch `0x41f410`, transition parameters, etc.). See §4.
- **Candidates that are not a camera** (§7): the type-42 instance (`0x452140`, messages 1000–1004)
  is a *projectile thrower* (MESSAGES.md incorrectly calls this "camera"); `0x452e10` is the tick
  of class 20/21; `0x447210`/`0x447660`/`0x447260`/`0x4480d0`/`0x447d70` are the
  HUD/GUI subsystem (`game+0x30`).

## 1. Data structures

### 1.1 `Repere` (frame/coordinate system), 0x9c = 156 bytes, 39 dwords

Used as "camera state" at `CamMgr+0x140` (accessor `0x41fa30` returns `&CamMgr+0x140`).

| offset | type | meaning | source |
|---|---|---|---|
| +0x00 | 3×3 f32, row-major, stride 12 | rotation matrix R (row vectors); identity via `0x437710` (sets [0]=[0x10]=[0x20]=1, rest 0) | `0x437710` |
| +0x24 | vec3 | translation T. In the camera state = **−cameraPosition** (view matrix `[R | T]`) | `0x41ef42..0x41ef7c` |
| +0x30..+0x8c | ? | not yet figured out (probably inverse/world matrix and helper fields) | |
| +0x90 | vec3 | **camera position in the world** (`CamMgr+0x1d0`); used by the frame function `0x401c11` for the world cell/visibility (`0x42a980`) and copied to the camera instance (`inst+0xc`) in `0x41f396` | `0x401c11`, `0x41f396` |

`0x437740(this, B)`: `this = this * B` for 3×4 matrices (row vectors): rotation `R' = R·R_B`,
translation `T' = T·R_B + T_B`. When building the view matrix (`0x41ef3d..0x41ef82`):
`this = [I | −pos]`, then `this *= [R_lookat | 0]` → view = translate(−pos) · R.

### 1.2 `CamMgr` (`[0x4c737c]`)

| offset | type | meaning | source |
|---|---|---|---|
| +0x00 | u32 | "race info active" (0/1); +4, +8 = saved distance/height of the follow camera (150.0, 50.0 default) | `0x41dd67`, `0x41fab0` |
| +0x0c | u32 | flag: transition parameters set this frame | `0x41f9b0`, `0x41f9d0` |
| +0x10 | u32 | flag: transition mode set this frame | `0x41f9f0` |
| +0x14 | u32 | flag: camera mode switched this frame | `0x41f49a` |
| +0x18 | ptr | instance (player?) whose rotation matrix (+0x34..+0x3c) gives the starting look direction for mode 1 | `0x41e488` |
| +0x1c | Repere | frozen state at the start of a transition ("cut" start frame) | `0x41eafd` |
| +0xac | vec3 | start position of the transition (= +0x1c+0x90) | `0x41ebcb` |
| +0xc4 | vec3 | look-at **offset** of the active mode relative to the target point `+0x278` (mode 1: `L' − playerPos`; other modes: y only) | `0x41f073` et al. |
| +0xd0 | vec3 | look-at offset at the start of the transition | `0x41eb11` |
| +0xdc | vec3 | current blended look-at offset | `0x41ed4d` |
| +0xf8 | f32 | duration of the transition (s), default 2.0; `< 0` → 2.0 | `0x41dd57`, `0x41eb6e` |
| +0xfc | f32 | elapsed time in the transition | `0x41efb2` |
| +0x100 | f32 | progress t = +0xfc/+0xf8 (≤ 1) | `0x41ef87` |
| +0x104 | f32 | speed for transition type "speed" (distance/speed = duration) | `0x41ec0d` |
| +0x108 | u8 | 1 = recompute duration from speed (`0x41f9b0` sets 1, `0x41f9d0` sets 0) | |
| +0x109 | u8 | transition in progress | `0x41efd1` |
| +0x10a | u8 | transition mode: 0 = smooth (travelling), 1 = hard cut | `0x41f9f0` |
| +0x10b | u8 | transition started while another was already in progress (look-at start from +0xdc instead of +0xc4) | `0x41eaf7` |
| +0x110 | ptr | sub-camera **mode 8** (TRAJ camera), 0x280 B, ctor `0x420340` | `0x41e210` |
| +0x114 | ptr | sub-camera **mode 0x10**, 0x2c0 B, ctor `0x420340`, update `0x4203c0` | `0x41e050` |
| +0x118 | ptr | sub-camera **mode 1** = follow camera ("Center"), 0x9dc B, ctor `0x422170`, update `0x424760` | `0x41dfe0` |
| +0x11c | ptr | sub-camera **mode 2**, 0x328 B, update `0x4254c0` | `0x41e130` |
| +0x120 | ptr | sub-camera **mode 4**, 0x328 B, update `0x425810` | `0x41e1a0` |
| +0x124 | ptr | sub-camera **mode 0x40**, 0x280 B, update `0x420cf0` (quaternion→matrix) | `0x41e280` |
| +0x128 | ptr | sub-camera **mode 0x100**, 0x280 B, update `0x420010` | `0x41e2f0` |
| +0x12c | ptr | sub-camera **mode 0x200**, 0x284 B, update `0x425b80` | `0x41e3a0` |
| +0x130 | ptr | sub-camera **mode 0x20**, 0x390 B, update `0x424bf0`; parameters at `CamMgr+0x61c` (message 1110) | `0x41e0c0` |
| +0x134 | u32 | **active mode** (bit): 1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200 | `0x41f5ec` |
| +0x138 | u32 | **mode index** = 1st argument of `0x41f410` (`mode = 1 << index`, 0..9; corrected: not the 2nd argument – that only goes to `0x41e450`/`0x4247f0`). The frame function tests `== 8` (debug camera 0x100) and `== 2` (mode 4); `0x459090` switches on it (3.1) | `0x41f5f2`, `0x401dc3` |
| +0x13c | f32 | dt of this frame | `0x41f014` |
| +0x140 | Repere | **current camera state** (view matrix + position at +0x1d0) | `0x41fa30` |
| +0x1dc | Repere | state of the previous frame | `0x41f385` |
| +0x278 | vec3 | target point ("target"): set by `0x41f960` every frame = playerPos + (0,50,0), then overwritten by the mode update (mode 1: playerPos) | `0x41f05b`, `0x41f960` |
| +0x284 | vec3 | player look direction (`M->dir`), from `0x41f960` | `0x41f960` |
| +0x290 | u32 | 1 = camera frozen/paused (`0x41fa40`), 0 = active (`0x41fa50`, on every mode switch) | |
| +0x294 | u32 | previous mode | `0x41f48b` |
| +0x298 | u32 | previous mode argument | `0x41f474` |
| +0x29c | Repere | state at the moment of the last mode switch | `0x41f480` |
| +0x338 | struct | parameters `p` of mode 1 (follow camera, §3): `p+0xc` (+0x344) look-at offset `L' − pos`; `p+0x18` (+0x350) ptr to player position; `p+0x1c` (+0x354) player look direction (or `Perso+0x34` in behind mode); `p+0x28` (+0x360) = −1; `p+0x2c` (+0x364) 1 = falling, 2 = rising; `p+0x30` (+0x368) 1 = slerp start; `p+0x3c` (+0x374) Perso state; `p+0x40` quaternion slerp start; `p+0x50` start position; `p+0x5c` (+0x394) `Perso+0x458`; `p+0x68/+0x6c` slerp duration/time; `p+0x70` (+0x3a8) flags 1 = slerp, 2 = reset, 4 = fast recentre; `p+0x74` (+0x3ac) behind mode | `0x41e48f`, `0x4591a5` |
| +0x3b0 | struct | parameters mode 8 (rail camera, §6.2): +0x20 (+0x3d0) player position (every frame via `0x459090`; message 540 sets the `.ins` position here once), +0x2c look direction, +0x38 rail point, +0x44 f32 **distance to the player**, +0x4c ptr TRAJ, +0x50 flags | `0x498fe5`, `0x459698` |
| +0x404 | struct | parameters mode 0x10 (+0x404 f32, +0x40c vec3 look-at) | `0x41f13f` |
| +0x43c | struct | parameters mode 2: +4 vec3 camera position, +0x10 ptr target instance, +0x20 f32; look-at result +0x450, +0x45c | `0x498c50` |
| +0x470 | struct | parameters mode 4: same layout (+0x484 look-at, +0x490) | `0x498c9f` |
| +0x4a4 | struct | parameters mode 0x40 | |
| +0x508 | struct | parameters mode 0x100 (+0x508 = 1.0, +0x518 = 1.0, +0x528 = 1.0, +0x538 = 15.0) | `0x41e34a` |
| +0x540 | struct | parameters mode 0x200 (+0x54c look-at, +0x558 vec3) | `0x41f280` |
| +0x5d4 | ptr | instance for mode 0x80 (camera at an instance's marker, `0x42feb0`/`0x42fa40`) | `0x41f1ee` |
| +0x5d8 | vec3 | look-at for mode 0x80 | |
| +0x61c | struct | parameters mode 0x20; +0x28..+0x44: 8 floats via message 1110 (defaults 1000, 300, 340, 500, 0, 700, 400, 200) | `0x444b9d` |
| +0x664 | ptr | camera **instance** (script object, message 800 sets it); position is overwritten every frame | `0x498d93`, `0x41f396` |
| +0x66c | u32 | flags: bit2 (4) = auto-zoom by distance to target on (`0x41f650`, message 710), bit3 (8) = 16:9 letterbox allowed | `0x41f696`, `0x41f8d0` |
| +0x670 | f32 | projection scale X (1.0) | `0x41f940` |
| +0x674 | f32 | projection scale Y (0.75 = 4:3 normal; 0.5625 = 16:9 in letterbox modes 1/2) | `0x41f940`, `0x41f8d0`, `0x41f910` |
| +0x678 | f32 | **zoom factor / focal** (default 1.2 = `0x41f680`; message 690 sets it ×0.01 via `0x41f660`) | |
| +0x67c | f32 | remaining **shake** time (`0x41fbb0` sets it); > 0 → `0x41fbd0` camera shake (§6.3) | `0x41f2fb` |
| +0x680 | u32 | letterbox mode: 0 = none, 1 = letterbox (`0x41f8d0`), 2 = letterbox variant (`0x41f910`, mode 4) | |
| +0x684 | u32 | previous letterbox mode | |
| +0x688 | u32 | ? (viewport-related) | `0x41f778` |
| +0x690 | u8 | flag via message 590/600 (`0x41f630`/`0x41f640`) | |
| +0x694 | u32 | cleared by `0x41fbb0` | |

## 2. Per-frame flow

`0x459090` (game update, 700 instr) calls `CamMgr::Update(dt)` = **`0x41eff0`** (`0x459913`).

```c
void CamMgr_Update(CamMgr *m, float dt)              // 0x41eff0
{
    m->dt = dt;                                       // +0x13c
    switch (m->mode) {                                // +0x134, table 0x41f3e8/0x41f400
    case 1:    Mode1_Update(m);  m->target = *m->p338.targetPtr(+0x350); m->lookAt = m->p338+0x344..; break;
    case 2:    Mode2_Update(m);  m->target = m->p43c+0x14 (+0x450); m->lookAt = (0, +0x45c, 0); break;
    case 4:    Mode4_Update(m);  m->target = +0x484;  m->lookAt = (0, +0x490, 0); break;
    case 8:    Mode8_Update(m);  m->target = +0x3d0;  m->lookAt = (0, +0x3cc, 0); break;
    case 0x10: Mode10_Update(m); m->target = +0x40c;  m->lookAt = (0, +0x404, 0); break;
    case 0x20: Mode20_Update(m); m->target = +0x624;  m->lookAt = sub130+0x280; break;
    case 0x40: Mode40_Update(m); break;
    case 0x80: /* camera at marker of instance +0x5d4 */ pos = marker(0x42fa40); m->lookAt = +0x5d8; break;
    case 0x100: Mode100_Update(m); break;
    case 0x200: Mode200_Update(m); m->target = +0x54c; m->lookAt = +0x558; break;
    }
    // ModeN_Update (0x41e740..0x41ea40): copy current state to sub+0xa0, sub->params = &m->pNNN,
    // call the actual update(dt), copy sub+4 (0x9c bytes, the new Repere) back to m+0x140.

    if (!m->cutMode /*+0x10a*/ && m->transitionActive /*+0x109*/) Transition_Travelling(m, m->target);  // 0x41eb90
    if (m->shakeTime /*+0x67c*/ > 0) { Shake(m); /*0x41fbd0, §6.3*/ m->shakeTime -= dt; }
    if (m->mode == 4) Letterbox2(m);                   // 0x41f910
    if ((m->prevMode == 4 && m->mode != 4) || (m->prevMode == 0x80 && m->mode != 0x80)) Letterbox0(m); // 0x41f940
    SetupProjection(m);                                 // 0x41f690, see §5
    m->prevState = m->state;                            // +0x1dc = +0x140
    if (m->camInstance) { camInstance->pos = m->state.pos; 0x4077f0(inst,0) /*re-cell*/; 0x434740(&pos,-1,inst); }
}
```

Every mode update has the form `sub->prevState (sub+0xa0) = m->state; sub->params (sub+0x27c) = &m->pXXX; update(dt); m->state = sub->state (sub+4)`.

## 3. Follow camera (mode 1, "Center")

Sub-camera `C = [CamMgr+0x118]` (0x9dc B, ctor `0x422170`). Parameter block `p = CamMgr+0x338`
(`C+0x9cc = p`, set in `0x41e7c7`). Debug strings in the code: `'cam_state = CENTER'`,
`'Target Out of world'`, `'Center ColSphere Blind Move'`, `'SubCenter ColWall Move'`.

### 3.1 Input: who fills the parameter block? (`0x459090`, Game+8 = "camera control")

`0x459090(ctl, dt)` (`ctl+0` = CamMgr, `ctl+4` = Perso, `ctl+8` = previous Perso state, `ctl+0xc` = saved
direction, `ctl+0x18` = recentre timer, `ctl+0x1c` = dt) runs every frame before `CamMgr_Update`:

1. `0x41f960(CamMgr, Perso->vtable[34]() /*&pos*/, &M->dir)`: `CamMgr+0x278 = pos + (0,50,0)`
   (`0x4a9030`), `CamMgr+0x284 = M->dir` (Mover look direction `Perso+0x388+0x10`, via `0x445780`).
2. On a change of Perso state (`Perso+0x21c`): new state 3 → `0x459050`; previous state 3 and
   new ≠ 3, 5 → hard cut to follow camera (`0x41f9f0(2)`, `CamMgr+0x368 = 0`, `SetMode(0,0)`).
3. `switch (CamMgr+0x138)` (**= mode index = 1st argument of SetMode, NOT the 2nd argument**; table `0x459934`):

| idx | mode | address | per-frame input |
|---|---|---|---|
| 0 | 1 follow | `0x4591a5` | see below |
| 1 | 2 | `0x45915d` | button 0xa just pressed (`0x467420`) → sound 9 (`0x468a00`) ("can't") |
| 2 | 4 | `0x459185` | `0x44a650(Perso, &pos)`, `Perso+0x690 = 1` |
| 3 | 8 TRAJ | `0x459698` | `p3b0+0x20 (CamMgr+0x3d0) = Perso pos`, `+0x3dc = normalize(M->dir)`, flag 4/8 in `+0x400` (vtable[35]() / `0x44c910`), button 0xa → sound 9 |
| 4 | 0x10 | `0x4597c5` | `CamMgr+0x40c = Perso pos`, `+0x418 = normalize(M->dir)`, flags `+0x438` |
| 5, 6 | 0x20, 0x40 | – | nothing |
| 7 | 0x80 | `0x4598c4` | `CamMgr+0x618 |= 2`, `+0x5f0 = 0x44e030(Perso)`, `+0x5e4 = Perso pos` |
| 8 | 0x100 | `0x4594ea` | debug free camera: keys (DIK 0x49/0x4d: `+0x538` ∓ 1 within 0..100; 0x78, 0x7d, 0x29/0x35, 0x1c/0x6a, 0x7a, 0x7b, 0x7f, 0x80 → bits 0..7 of `CamMgr+0x53c`), `Perso+0x690 = 1` |
| 9 | 0x200 | `0x459346` | first-person/look mode: mouse (`[0x5e6190]` vtable[2]/[3] → `p540+0x28/+0x2c`) or actions 1/0 (×5 / ×−5) and 3/2 (×−5 / ×5); writes the resulting look direction `(−p540+0x3c, 0, −p540+0x44)` normalised back into `M+0x34`, `M+0x1c`, `M+0x10` and calls `0x44c080(Perso, p540, 0)` |

**Mode index 0 (follow camera)**, `0x4591a5..0x45933b`:

```c
p->targetPtr (+0x18, CamMgr+0x350) = Perso->vtable[34]();        // &Perso position
p->+0x28 (CamMgr+0x360)            = -1;
dir = M->dir;                                                     // Perso+0x388+0x10 (0x445780)
p->dir (+0x1c, CamMgr+0x354)       = dir;                         // player look direction (+forward)
st = Perso->state (+0x21c);
if (st == 1 || st == 4 || st == 8) { p->behind (+0x74, CamMgr+0x3ac) = 1; ctl->savedDir = Perso+0x34..0x3c; }
else                                 p->behind = 0;
if (pressed(0xa) && !p->behind) { ctl->timer = 0.5f; ctl->savedDir = Perso+0x34..0x3c; }   // 0x467420
else if (held(0xa))               p->behind = 1;                                           // 0x467400
if (ctl->timer > 0) { ctl->timer -= dt; p->behind = 1; p->flags (+0x70) |= 4; } else p->flags &= ~4;
if (p->behind) p->dir = ctl->savedDir;       // = row 1 of the Perso rotation = −look direction (PERSO_FRAME §2.4)
p->+0x2c (CamMgr+0x364) = 0x44c910(Perso) ? 1 : Perso->vtable[35]() ? 2 : 0;
p->+0x5c (CamMgr+0x394) = Perso+0x458..0x460;   p->+0x3c (CamMgr+0x374) = st;
```

Action **0xa** is thus the "camera behind the player" button: one tap = 0.5 s recentring (faster, flag 4),
holding = keeps recentring. There is **no** free left/right camera rotation in mode 1.
Note the sign: normally `p->dir = +look direction`; in "behind" mode it is `Perso+0x34` = **−**look direction
(the instance looks along −dir). `0x424760` negates `p->dir` again (see 3.2), so that `F` in behind mode
is the actual look direction and the camera ends up at `T − F·distance` (behind the player).

### 3.2 Fields of `C`

| offset | meaning | default (ctor `0x422170`) |
|---|---|---|
| +0x004 | Repere result (to `CamMgr+0x140`) | |
| +0x0a0 | Repere previous state (from `CamMgr+0x140`) | |
| +0x280 | **distance** for behind mode/reset (message 680) | 400.0 |
| +0x28c | extra lowering of the look point at flag 1/2 (0..150) | 0 |
| +0x294 | byte: +0x28c growing | 0 |
| +0x296 / +0x297 | byte flags from `p+0x2c` == 1 / == 2 (cleared each frame) | |
| +0x298 | world cell of the target (`0x40aba0`) | |
| +0x2a4 | **state**: 0/1 = CENTER, 2 = target invisible (follow path), 4 = back to saved point | 0 |
| +0x2a8 | 1 = behind mode this frame (`p+0x74 != 0`) | 0 |
| +0x2ac | 1 = recompute look-at matrix (`0x422760`) | 1 |
| +0x2e4 | vec3 collision correction of this frame (from `0x422e30`) | |
| +0x2f0.. | 100 × vec3 breadcrumb trail (state 2); +0x7a0/+0x7a4/+0x7a8 counters | |
| +0x7b0 | dt | 1.0 |
| +0x7b4 | world cell of the camera | −1 |
| +0x7b8 | 1 = camera was catching up (hysteresis) | 0 |
| +0x7d8 | **height** above the target point (message 670) | 180.0 |
| +0x7dc | height of the target point T above the player position | 120.0 |
| +0x7e0 / +0x7e4 | **max. distance** (start catching up / how far it catches up to) (message 680) | 400.0 / 400.0 |
| +0x7e8 | Repere being built (view matrix) | |
| +0x878 | camera position (= Repere+0x90) | |
| +0x884 | 3×3 look-at rotation (`0x4223b0`) | |
| +0x920 | **F** = normalize(−`p->dir`) | |
| +0x92c / +0x938 | F and P of the previous frame | |
| +0x944 | **P** = camera position | |
| +0x950 | P at the start of the frame (fallback if the new P falls outside the world) | |
| +0x95c | (0, 120, 0) | |
| +0x968 | **T** = playerPos + (0, `+0x7dc`, 0) | |
| +0x974 | T of the previous frame; +0x980 = T − T_prev | |
| +0x98c | **L** = playerPos + (0, 140, 0) (`0x4aa1b4`) = look point | |
| +0x998 | **move** = desired displacement this frame; +0x9a4 copy | |
| +0x9b0 | saved point for state 4 | |
| +0x9bc | up vector for look-at | (0, −1, 0) |
| +0x9c8 | byte: in reset loop (`0x424200`) | |
| +0x9cc | `p` | |

### 3.3 Per frame: `0x424760(dt)` → `0x422790(&pos, -1, &M)`

```c
void Center_Update(C, dt) {                      // 0x424760
    C->dt = dt;                                  // +0x7b0
    if (p->+0x2c == 1) C->f296 = 1; else if (p->+0x2c == 2) C->f297 = 1;
    C->behind (+0x2a8) = p->behind != 0;
    // local matrix of which only column 2 is filled: (m02,m12,m22) = −p->dir
    Center_Step(C, p->targetPtr, p->+0x28, &M);  // 0x422790
}
void Center_Step(C, vec3 *pos, int cell, M) {    // 0x422790
    if (p->flags & 1) { p->slerpT (+0x6c) += dt; if (slerpT >= p->slerpDur (+0x68)) { slerpT = slerpDur; p->flags &= ~1; } }
    C->corr (+0x2e4) = 0;  C->move (+0x998) = 0;
    F = normalize(M.m02, M.m12, M.m22);          // +0x920
    T = *pos + (0, C->+0x7dc /*120*/, 0);        // +0x968
    L = *pos + (0, 140.0, 0);                    // +0x98c  (0x4aa1b4)
    C->camCell = FindCell(P);                    // 0x40aba0([0x4c93b0]+0x14, x,y,z)
    c = FindCell(T); if (c == -1) return /*unchanged, 'Target Out of world'*/;  C->cell = c;
    Pstart (+0x950) = P;
    if (state == 1 && !behind) state = 0;
    if (state != 2,3,4) {
        Ray(P, T, -1);                           // 0x4359b0 → globals 0x53a554 (hit kind), 0x53a558 (t), 0x53a560 (object)
        if ([0x53a554] != 0 && !HitIsType7()) {  // 0x422140: hit kind 2 AND object category 7 does not count as a blockage
            state = 2;  +0x7a0 = 2; pad[0] = P; pad[1] = T_prev; pad[2] = T; +0x7a4 = +0x7a8 = 0;
            P += T_prev; P -= T;                // 0x43ffd0 / 0x440020: P shifts by −(T − T_prev), but 0x423ab0 overwrites P in the same frame (3.6)
        }
    }
    dT (+0x980) = T − T_prev;
    switch (state) { case 0: case 1: Center_Normal(C); break;        // 0x4231e0
                     case 2: Center_Occluded(C, M); break;           // 0x423ab0
                     case 4: /* 3.7 */ break; }
    P += move; P += corr;                        // 0x422cb3, 0x422cc2
    if (FindCell(P) == -1) P = Pstart;
    LookAt(C);                                   // 0x422760 → 0x4223b0 (if +0x2ac == 1)
    Repere = [I | −P] · [R | 0];                 // 0x437710, 0x437740;  Repere+0x90 = P
    f296 = f297 = 0;  Fprev = F; Pprev = P; Tprev = T;  C->result (+4) = Repere;
}
```

### 3.4 Normal state `0x4231e0` (CENTER)

```c
dy = T.y − P.y;
if (p->flags & 2) { Center_Reset(C) /*0x424940*/; p->flags &= ~2; return; }
if (behind) {                                    // 0x423264: to the point directly behind the player
    D = T − F̂·C->dist(+0x280);                   // F̂ = F normalised (incl. y)
    d = D − P;  len = |d|;  y-less normalisation of d: d̂ = d / sqrt(d.x²+d.y²+d.z²)  // 0x423345.. (see note)
    move = d̂ · len · dt · ((p->flags & 4) ? 7.0 : 3.0);      // 0x4aa1b8 / 0x4a988c
} else Center_Distance(C);                       // 0x4242d0, see 3.5
Center_BehindArc(C);                             // 0x423ed0 (only active in behind mode, see 3.5b)
if (f297 && (P.y − T.y) < 300.0)  move.y = T.y − Tprev.y;          // 0x4a986c: camera follows y 1:1
else if (!f297)                   move.y = (dy + C->height(+0x7d8)) · dt · 6.0;   // 0x4a9888
else                              move.y = 0;
if (p->flags & 1) { /* 0x423485: slerp transition, see 3.8 */ }
switch (Center_Collide(C)) { ... }               // 0x422e30, see 3.6
```

Note on behind mode: `0x423323..0x4233cb` normalises `d` and then multiplies again by `|d|`, net
`move = (D − P)·dt·k` with k = 3.0, or 7.0 if `p->flags & 4` (the 0.5 s after tapping button 0xa); `move.y` is
overwritten afterwards regardless.

The vertical axis is thus an **exponential approach**: `P.y += (T.y + height − P.y) · 6·dt`, with
`T.y + height = player.y + 120 + 180 = player.y + 300`. While jumping, the camera follows the player's y with
this time constant of 1/6 s (no separate jump logic, except flag `f297`/`f296`, see 3.9).

### 3.5 Horizontal distance control `0x4242d0` (non-behind)

The camera does **not** actively turn to stay behind the player; it is pulled along as if on an elastic band
("lazy follow"). Only the xz distance to T is controlled, with a dead zone of 100 units:

```c
a = L − P; a = xzNormalize(a);  f = xzNormalize(F);          // 0x424720 normalises only x,z
k = (dot(a, f) > 0.7f /*0x4aa1d8*/) ? 10.0f : 1.0f;          // F = −look direction: player walks TOWARD the camera → 10× faster backing off
v = T − P; v.y = 0; d = |v|;
dMax = C->+0x7e0 (400); dStop = C->+0x7e4 (400); dMin = dMax − 100.0f /*0x4a9010*/;
if (d < dMin) {                                              // too close: back off
    w = normalize(v) · (dMax / d) · dt · k · 66.6667f;       // 0x4aa1c4
    if (d + |w| > dMin) w = normalize(w) · (dMin − d);       // not past the edge of the dead zone
    C->catchUp (+0x7b8) = 0;  move.xz = −w.xz;
} else if (d > dMax || (d > dStop && C->catchUp == 1)) {     // too far: catch up
    w = normalize(v) · (d / dMax) · dt · 433.333f;           // 0x4aa1bc
    if (d − |w| < dStop) w = normalize(w) · (d − dStop);
    C->catchUp = 1;  move.xz = w.xz;
} else move.xz = 0;                                          // dead zone 300..400
move.y = (T.y − P.y + C->height) · 0.02f;                    // 0x4aa1c0; overwritten in 0x4231e0 (3.4)
```

Speeds: backing off ≈ 66.7·(400/d) units/s (×10 if the player looks toward/walks toward the camera), catching up ≈
433·(d/400) units/s (player walks at 600/s → the camera falls slightly behind while running and d grows to
≈ 554 where 433·d/400 = 600). Everything is dt-scaled (linear, no exponential damping) except y.

### 3.5b Behind-mode correction `0x423ed0` (only if `C->behind`)

* If the player stands still (`|T − Tprev|xz < 0.5`), not in the reset loop, and `| |T−P|xz − dist | < 10.0`
  (`0x4aa1d0`, double): if `((T−P) × F).y` and `((T−(P+move)) × F).y` swap sign (the step would overshoot the
  line "directly behind the player") → `move = 0`.
* If the step crosses the circle of radius `dist ± 5.0` (`0x4a9884`) around T (from inside `dist−5` to outside
  `dist+5` or the reverse): solve `|P + s·move − T|xz = dist` (A = |move|xz², B = 2·(P−T)·move, C = |P−T|xz² − dist²,
  `s = (−B ± √(B²−4AC))/2A`, the root with the smallest |s|) and `move *= s` – the camera stays on the circle.

### 3.6 Collision and line of sight

Three mechanisms, all inside `0x4231e0`/`0x422790`:

1. **Sphere-vs-world** `0x422e30`: `0x434820(40.0)` sets the collision radius (`[0x4b3118]`), then
   `0x439c50(&out, &P, -1, &(P+move), 35.0, 0)`: the path P → P+move is traversed in `n = floor(len/35)+1` steps;
   after every step `0x407340(out, 40.0, -1)` (static sphere test); on contact (`[0x4c4bd0] != 0`)
   the push-out vector `[0x4c4bb4..0x4c4bbc]` is added to `out`. Non-zero result → `C->corr (+0x2e4) = out − (P+move)`
   and return value 2; otherwise 0. The camera is thus a **sphere with radius 40** that slides along walls.
2. **Line-of-sight veto** `0x423a40(newP, T)`: ray `0x4359b0(newP, T, -1)`; allowed if nothing is hit
   (`[0x53a554] == 0`) or if the object hit has category 7 (`0x422140`: `[0x53a554] == 2` and
   `([0x53a560]->vtable[4]()[0] & 0x1f) == 7`). Otherwise (`'Center ... Blind Move'`): `move = 0` and
   `0x422f10` ("SubCenter": computes an alternative step `(T−P)·dt·0.1` but doesn't actually use it;
   net effect: `corr = 0` unless the camera is currently touching a wall at its current spot). Also `move = corr = 0` if
   `P+move(+corr)` falls outside the world (`0x40aba0` = −1). **So the camera refuses any step after which it can no longer see T.**
3. **Target disappears behind geometry** (ray P → T blocked at the start of the frame, `0x4229b8`):
   state 2 "FIND" (`0x423ab0`). Breadcrumb trail `pad[]` (`C+0x2f0`, max 100 points): start `pad = {P, Tprev, T}`, n = 2.
   Every frame: if `pad[n]` → T is blocked: (if Tprev → T is also blocked: give up, `state = 0`,
   `P = T + (1, 1, 100)`), `pad[n] = Tprev; n++`. `pad[n] = T`. The camera walks the path:
   `u (+0x7a8) += 0.04` (`0x4aa1cc`, **per frame, not dt-scaled**); at `u ≥ 1`: `seg (+0x7a4)++, u = 0`;
   `P = pad[seg] + (pad[seg+1] − pad[seg])·u`. As soon as P → T is free again: `state = 0`. (Side effect: `+0x2b4 =
   normalize(+0x2b4 + normalize(T−P)·0.15)`, `0x4aa1c8`.) The camera thus literally follows the player's trail
   around the corner, 25 frames per segment.

   Checked instruction by instruction (`0x423ab0..0x423dfe`), which refines the summary above:
   ```c
   void Center_Occluded(C) {                       // 0x423ab0, 'FIND'
       if (Blocked(pad[n-1], T)) {                 // the LAST CRUMB (+0x2e4 + 12n = pad[n-1]), not pad[n], loses sight of T
           if (Blocked(Tprev, T)) { state = 0; P = (1, 1, 100); P += T; }   // 0x423b62
           pad[n] = Tprev; n++;                    // no bound check: pad[] ends at +0x7a0 = n itself
       }
       pad[n] = T;
       if (u >= 1.0) { u = 0; seg++; }             // 0x4a900c
       u += 0.04;                                  // incremented BEFORE use: the first frame is already at u = 0.04
       P = pad[seg] + (pad[seg+1] − pad[seg])·u;   // overwrites the give-up P above: that only ends state 2, and the
                                                   // next frame starts a fresh trail {P, Tprev, T} if T is still hidden
       dir2b4 = normalize(dir2b4 + normalize(T − P)·0.15);
       if (!Blocked(P, T)) state = 0;              // Blocked = ray 0x4359b0 hit and not 0x422140 (category 7)
   }
   ```
   `move` and `corr` stay 0 in state 2 (they are zeroed at the top of `0x422790`); only the `FindCell(P) == −1 → P = Pstart`
   test after the switch still applies. The shift `P += Tprev − T` on entry is dead (P is overwritten before it is read).
   The crumbs are sparse: a new one is dropped only where the trail turns a corner. The segment P → Tprev can be
   ~400 units long and takes 25 frames like any other, so while the player keeps running the camera slides toward and
   around the corner and returns to state 0 as soon as it sees T. Category 7 = type word `0x27` = class 80 only
   (`0x451b28`, the storm with shelter zones, PERSO_FRAME §4.2).
   Port: `camera_breadcrumbs()` in `src/player.c` (0.04 per frame normalised to 60 fps; `WOODY_CAMLOG=1` logs the trail).

There is **no** "slide the camera forward along the ray player→camera" in the normal update. That only happens
during the **reset** `0x424940` (`p->flags & 2`): `D = T − F̂·dist + (0, height, 0)`; ray `0x4359b0(T, D, cell)`;
if `0 < t < 1.1` (`[0x53a558]`, `0x4aa168`): `t = min(t, 0.999)·0.999` (`0x4aa1dc`), `D = T + (D−T)·t`;
`move = D − P`, `corr = 0`. (Bit 1 of `p->flags` is never set in the code found → dead path, see §8.)

State 4 (`0x422ab7`): move P toward the saved point `+0x9b0` with `(Q − P)·0.02·dt·50` (`0x4aa1b0`,
`0x4a9030`) while `|Q−P| > 3.0` (`0x4a988c`), otherwise `P = Q`; repeat while P is outside the world. State 4
is also never set anywhere (dead path).

### 3.7 Look target and look-at matrix `0x4223b0`

```c
L' = L;                                            // playerPos + (0,140,0)
if (f296 || f297) { C->drop (+0x28c) = min(drop + dt·450.0 /*0x4aa1a8*/, 150.0 /*0x4a9754*/); }
else                C->drop *= 0.94f;              // 0x4aa1ac, per frame (not dt-scaled)
L'.y −= C->drop;
delta = L'.y − prevLy [0x4c83a0];
if (!f296 && !f297 && [0x4c93a8] /*not the first frame*/ && (delta >= 9.0 || delta <= −9.0)) smooth [0x4c93ac] = 1;   // 0x4aa1a4/0x4aa1a0
if (smooth) { L'.y = prevLy + delta·dt·10.0 /*0x4a9750*/; if (|delta| < 0.1) smooth = 0; }
prevL [0x4c839c..] = L';
fwd   = normalize(L' − P);
right = normalize(up × fwd);      up = C+0x9bc = (0, −1, 0)
up2   = normalize(fwd × right);
R (+0x884) = columns (right, up2, fwd):  R[i][0] = right[i], R[i][1] = up2[i], R[i][2] = fwd[i]
if (p->flags & 1) R = matrix(slerp(p->q0 (+0x40), quat(R), p->slerpT / p->slerpDur));   // 0x4404b0, 0x440dd0, 0x440370
p->lookOffset (+0xc, CamMgr+0x344) = L' − *p->targetPtr;
```

The look target is thus **player position + (0, 140, 0)**, without lag (only jumps in y ≥ 9 per frame
are filtered with 10·dt). **Jumping**: `f296` = `0x44c910(Perso)` = jumper state (`Perso+0x334+0x14`) 3 or 4 =
**falling**; `f297` = `Perso->vtable[35]()` (`0x44c940`) = jumper state 0, 1 or 7 = **rising/apex of the jump**
(PERSO_JUMP §1.3). In both cases the look point drops at 450/s to a max. of 150 below `player + 140` (so the camera
keeps looking roughly at the takeoff spot/ground instead of nodding along with the jump) and then springs back at ×0.94 per
frame. While rising (`f297`) the camera position additionally moves up 1:1 with the player (`move.y = ΔT.y`, as long as
`P.y − T.y < 300`); while falling and walking, the 6·dt approach applies (3.4).

With `up = (0,−1,0)`, for `fwd = +z`: `right = (−1,0,0)`, `up2 = (0,−1,0)`. In a right-handed y-up
world, −x is exactly the right side for a viewer looking along +z, and `up2` points down: camera space is
**x = screen right, y = screen down, z = forward** (a pure rotation, det = +1; no mirroring). That maps directly
onto screen coordinates (§5). View: `v_cam = (v − P) · R` (row vector).

### 3.8 Start/reset of the follow camera `0x41e450(arg)` → `0x4247f0(arg)`

At `SetMode(0, arg)`: `p->flags &= ~1`; `C->prev = CamMgr state`; `state = 0` (`0x422350`); if `CamMgr+0x18`
(instance) is set: `p->dir = (−i[0x34], i[0x38], −i[0x3c])` (= +look direction of that instance). Then `0x4247f0(arg)`:

* `p+0x30 (CamMgr+0x368) == 1` → **slerp start** (`0x424809`): `0x423e10` takes over the current CamMgr state
  (P = current camera position), `p->flags |= 1`, `slerpDur = 0.5`, `slerpT = 0`, `q0 = quat(current rotation)`
  (`0x4404b0`), `p+0x50 = current position`, P-start = `pos + (dir.x, 100, dir.z)`. During the slerp (3.4,
  `0x423485..0x42367d`), with `s = slerpT/slerpDur`, `S = p+0x50`, `D = T − F̂·dist + (0,height,0)`:
  `move = T + normalize(S + (D−S)·s − T) · ((1−s)·|S−T| + s·dist) − P` (an arc around T from start to behind point).
  Messages 500/501 and `0x459090` set `+0x368 = 0`; who sets it to 1 has not been found (§8).
* otherwise (`0x4248b3`): `P = pos − (dir.x, −100, dir.z)`; `p->flags &= ~1`; `p+0x2c = 0`; `behind = 1`;
  `Center_Update(0.1)`; **`0x424200(arg)`**: 100 iterations of `Center_Step` with `dt = 0.04` and
  `F = (arg ? +F : −F)` so the camera "settles" into its resting position; then `behind = 0`, `p+0x30 = 0`.
  `arg = 0` (message 500): camera **behind** the player; `arg = 1` (message 501): camera **in front of** the player.

### 3.9 Behaviour summary

| question | answer |
|---|---|
| distance | xz distance to T is kept between 300 and 400 (`+0x7e0` = 400, dead zone 100); message 680 sets it |
| height | `P.y → player.y + 120 + 180` at `6·dt` per frame; message 670 sets the 180 |
| look target | player + (0,140,0), unlagged |
| smoothing | xz: linear speed ∝ distance (433·d/400 /s catching up; 66.7·400/d /s backing off, ×10 if the player looks at the camera); y: exponential 6/s |
| jumping | rising (jumper 0/1/7): camera y follows 1:1, look point drops 450/s to −150; falling (3/4): y at 6/s, look point also drops; then look-point offset springs back at ×0.94 per frame |
| collision | sphere r = 40 with push-out (35-unit steps), line-of-sight veto, breadcrumb trail if line of sight is lost |
| input | only action 0xa: camera behind the player (3·dt, first 0.5 s 7·dt); Perso states 1, 4, 8 force behind mode |
| first person | separate mode 0x200 (index 9, message 550), not part of the follow camera |

## 4. Script cameras (.ins) and messages

`.ins` camera object (0x2c B, `0x498b90`): `+0xc` position, `+0x28` TRAJ (→ vtable `0x4aa224`).
Message handler `0x498bd0` (vtable slot 22), `this` = the camera object, `msg` = {id, args…}.
Byte tables `0x498e84` (500–580) and `0x498f00` (600–800).

| id | args | action | address |
|---|---|---|---|
| 500 | cam | `CamMgr+0x368 = 0; SetMode(0, 0)` (mode 1<<0 = **1**, follow camera; arg 0 = start **behind** the player, 3.8) | `0x498c2b` |
| 501 | cam | `CamMgr+0x368 = 0; SetMode(0, 1)` (follow camera, arg 1 = start **in front of** the player) | `0x498c06` |
| 510 | cam, f, target (order corrected, see CAMERA_SCRIPT.md §2.1) | **mode 2** (fixed camera at `.ins` position looking at an instance): `p43c.pos = cam->pos; p43c.target = inst[target]; p43c.f(+0x20) = (float)f; SetMode(1, 0)` | `0x498c50` |
| 520 | cam, f, target | **mode 4**: same in `p470`; `SetMode(2, 0)` | `0x498c9f` |
| 530 | cam | `SetMode(4, 0)` → mode **0x10** | `0x498d03` |
| 540 | cam, d | TRAJ camera only (`0x498fe5`): `p3b0.traj(+0x4c) = cam->traj; p3b0.dist(+0x44) = (float)d` (distance rail camera ↔ player, §6.2); `p3b0.pos(+0x20) = cam->pos; SetMode(3, 0)` → mode **8** | `0x498fe5` |
| 550 | cam | `SetMode(9, 0)` → mode **0x200** | `0x498cee` |
| 560 | cam, v | transition: speed `+0x104 = (float)v`, `+0x108 = 1` (duration = distance/speed) | `0x498d18` → `0x41f9b0` |
| 570 | cam, t | transition: duration `+0xf8 = t·0.01` s, `+0x108 = 0` | `0x498d30` → `0x41f9d0` |
| 580 | cam, mode | transition mode: 1 = smooth (`+0x10a=0`), 2 = hard cut (`+0x10a=1`) | `0x498d4e` → `0x41f9f0` |
| 590 | cam | `+0x690 = 1` | `0x498d63` → `0x41f630` |
| 600 | cam | `+0x690 = 0` | `0x498db8` → `0x41f640` |
| 650 | cam | race info on: saves follow-camera distance/height (+0x280, +0x7d8 of sub118) in `CamMgr+4/+8`, resets follow camera (`0x422350`, `0x4247f0(0)`) | `0x498df9` → `0x41fab0` |
| 660 | cam | race info off: restores distance/height | `0x498e0a` → `0x41fb00` |
| 670 | cam, h | follow-camera **height**: `sub118+0x7d8 = (float)h` | `0x498dc9` → `0x41fa60` |
| 680 | cam, d | follow-camera **distance**: `sub118+0x7e0 = +0x7e4 = +0x280 = (float)d` | `0x498de1` → `0x41fa80` |
| 690 | cam, z | zoom factor `+0x678 = z·0.01` (only if > 0) | `0x498e1b` → `0x41f660` |
| 700 | cam | zoom factor back to 1.2 | `0x498e39` → `0x41f680` |
| 710 | cam | auto-zoom on (`+0x66c |= 4`) | `0x498e4a` → `0x41f650` |
| 800 | cam, inst | `CamMgr+0x664 = inst[inst]` (camera instance that follows the position) | `0x498d93` |

`SetMode(bitIndex, arg)` = **`0x41f410`** (`mode = 1 << bitIndex`): warns if no
transition mode (+0x10) or parameters (+0xc) have been set; saves `prevMode/prevArg/prevState`
(+0x294/+0x298/+0x29c); sets `+0x14 = 1`; if the transition mode is smooth (+0x10a == 0): starts a
transition (`+0x109 = 1`, `0x41eaa0` freezes the current state into +0x1c and the look-at target into
+0xd0; duration < 0 → 2.0; +0xfc = 0, +0x100 = 0); on a hard cut: +0x109 = +0x10b = 0. Then the
mode-specific init (`0x41e450` mode 1, `0x41e520` mode 2, `0x41e560` mode 4, `0x41e5b0` mode 8,
`0x41e630` mode 0x10, `0x41e4e0` mode 0x20 (+ letterbox off), `0x41e5f0` mode 0x40,
`0x41e670` mode 0x100 (+ cut), `0x41e6c0` mode 0x200 (+ cut), mode 0x80: letterbox on if
`+0x618 & 2`). Finally `+0x134 = mode`, `+0x138 = bitIndex` (the 2nd argument only goes to the mode-1 init `0x41e450`).

The frame function `0x4019c0` (message dispatch) clears +0xc/+0x10/+0x14 before processing the
script messages and warns afterwards if transition parameters/mode were set without a mode switch.

## 5. Projection

The game does its **own (software) transform and projection**; D3D gets screen coordinates + rhw. So there is no
D3D view/projection matrix; the "projection" consists of three scale factors + a viewport.

### 5.1 `0x41f690` CamMgr::SetupProjection (every frame at the end of `0x41eff0`)

```c
if (m->flags66c & 4) {                                  // auto-zoom (message 710)
    d = |m->target(+0x278) − m->state.pos(+0x1d0)|;     // target = playerPos + (0,50,0), see 0x41f960
    t = d < 300 ? 0 : d > 3000 ? 1 : (d − 300) · (1/2700);      // 0x4a986c, 0x4aa170, 0x4aa16c
    m->zoom (+0x678) = (1 − t)·1.1 + t·0.2;             // 0x4aa168, 0x4a9760
}
aspect = m->sx / m->sy;                                  // +0x670 / +0x674 = 1/0.75 = 4:3 (or 1/0.5625 = 16:9)
W = [0x5e8650]+4; H = [0x5e8650]+8;                      // screen resolution
Hview = (4.0/W) / (3.0/H) · (W / aspect) = 4H / (3·aspect);     // 4:3 → H; 16:9 → 0.75·H
0x437a30(&m->state, 1/m->sx, 1/m->sy, m->zoom);          // → [0x5ac894] = 1/sx, [0x5ac88c] = 1/sy, [0x5ac890] = zoom
if ((float)W / H > aspect)      Viewport(W/2, H/2, W + 0.5, H + 0.5);                      // 0x437c40(0x4c5350, cx, cy, w, h)
else if (m->letterbox == 2)     Viewport(W/2, (H + Hview)/4, W + 0.5, Hview + 0.5);        // image shifted upward
else                            Viewport(W/2, (H + 1)/2, W, Hview + 0.5);
```

`0x437c40(vp, cx, cy, w, h)`: `vp+8 = cx, +0xc = cy, +0x10 = w, +0x14 = h, +0 = w/2 − 1, +4 = h/2 − 1`.
`0x437a30` only stores the three globals (the `this` is not used).

### 5.2 From Repere to clip space: `0x4379a0`, `0x437a50`, `0x4840f0`

The frame function (`0x401940` → `0x42a680(level, &cameraRecord, …)`) calls, on the copied Repere:

* **`0x4379a0`**: clip matrix at `Repere+0x30` (3 rows × 4: x, y, z):
  `row_x = [0x5ac894]·(R[0][0], R[1][0], R[2][0], T.x)`, `row_y = [0x5ac88c]·(column 1, T.y)`,
  `row_z = [0x5ac890]·(column 2, T.z)`.
* **`0x437a50`**: inverse scale at `Repere+0x60` (columns of R × `1/[0x5ac894]`, `1/[0x5ac88c]`, `1/[0x5ac890]`)
  and `Repere+0x84 = Repere+0x90` (position) – for back-projection/billboards.
* **`0x4840f0(renderer [0x5e86ac], R, pos, scale)`**: the same matrix as 4×4 with stride 0x10 at `renderer+0x114`
  (translation `−pos·R` at `+0x144..0x14c`, then per column × scale), inverse at `+0x154`, position at `+0x184`.
  `0x4843b0` sets the renderer viewport: centre `(vp+8, vp+0xc)` → `+0x194/+0x198`, size
  `(vp+0x10 − 2.0, vp+0x14 − 2.0)` → `+0x1a0/+0x1a4` (`0x4a9870` = 2.0).

So with `v_cam = (v − P)·R` (camera space: **x = right, y = down, z = forward**; see 3.7):

```
x_clip = x_cam / sx  = x_cam            (sx = 1.0)
y_clip = y_cam / sy  = y_cam · 1.3333   (sy = 0.75;  16:9 letterbox: 0.5625 → · 1.7778)
w      = z_cam · zoom = z_cam · 1.2     (zoom default 1.2; message 690: z·0.01; auto-zoom 1.1 … 0.2)
visible ⇔ |x_clip| ≤ w and |y_clip| ≤ w          (clip codes 1/2/4/8 in 0x47b372..0x47b3c6; sphere test 0x437b00 with margin r·1.4142)
screen.x = cx + (x_clip / w) · 0.5 · vpW ;  screen.y = cy + (y_clip / w) · 0.5 · vpH ;  rhw = 1/w     (0x47b3ce..0x47b429)
```

**FOV**: `tan(hfov/2) = zoom·sx = 1.2` → **horizontal 100.4°**; `tan(vfov/2) = zoom·sy = 0.9` → **vertical 83.97°**
(4:3). The zoom factor is thus the *tangent of the half horizontal angle* (bigger = wider). In letterbox (sy = 0.5625):
vertical 2·atan(0.675) = 68.0° on a viewport of 0.75·H. Auto-zoom far away (zoom 0.2): hfov 22.6°.

For an OpenGL reimplementation: `gluLookAt(P, L', up = (0,1,0))` yields exactly the same right/up axes
(`right = (0,−1,0) × fwd = fwd × (0,1,0)`; the world is right-handed, there is **no** mirroring), and
`gluPerspective(fovy = 83.97°, aspect = 4/3)`. Near/far: the code read only contains the test against the four
side planes; near/far handling has not been found (§8).

## 6. Transitions, rail camera and camera shake

### 6.1 Transition between two modes ("travelling") `0x41eaa0` + `0x41eb90`

A transition is **not** movement along a TRAJ but a linear interpolation from the frozen old camera to
the (moving) camera of the new mode. Only active if the transition mode = smooth (message 580 arg 1).

* Start (`0x41eaa0`, from `SetMode`): `m->from (+0x1c) = [I | pos]·prevState` (rotation of the old camera, `+0xac` =
  old position); `lookFrom (+0xd0) = lookOffset (+0xc4)` of the old mode (or the currently blended `+0xdc` if a
  transition was already in progress, `+0x10b`); `dur (+0xf8) ≤ 0 → 2.0`; `elapsed (+0xfc) = 0`, `t (+0x100) = 0`.
* Every frame after the mode update (`0x41eb90(m, vec3 target)`, target = the mode's `m+0x278` value of this frame, by value):

```c
delta = m->state.pos − m->fromPos;                       // new mode position of this frame − start position
if (m->durFromSpeed (+0x108)) { m->dur = (m->speed (+0x104) > 0) ? |delta| / m->speed : 2.0; m->durFromSpeed = 0; }
pos = m->fromPos + delta · m->t;                         // linear, no easing
if (m->elapsed == 0 && m->mode == 1) {                   // to the follow camera: look offsets there are relative to the player
    if (m->prevMode == 0x80)  m->lookFrom = m->+0x5d8 − *p338.targetPtr;
    if (m->prevMode == 0x200) m->lookFrom = m->+0x558 − *p338.targetPtr;
}
m->lookCur (+0xdc) = m->lookFrom + (m->lookOffset (+0xc4) − m->lookFrom) · m->t;
look = target + m->lookCur;
fwd = normalize(look − pos); right = normalize((0,−1,0) × fwd); up2 = normalize(fwd × right);
m->state = [I | −pos]·[R(right, up2, fwd) | 0]; m->state.pos = pos;
m->t = min(m->elapsed / m->dur, 1);  m->elapsed += dt;  if (m->elapsed > m->dur) m->active (+0x109) = 0;
```

`+0xc4` ("look-at of the active mode" in §1.2) is thus an **offset relative to the target point `+0x278`** (mode 1: `L' − playerPos`;
modes 2/4/8/0x10: a y offset only), not an absolute point. Duration: default 2.0 s; message 570: `t·0.01` s; message 560:
`distance / speed` (computed on the first frame). Interpolation of position and look offset is **linear in time**; the
rotation is not interpolated but is rebuilt as a look-at every frame. Hard cut (580 arg 2): no transition,
the new mode determines the state immediately.

**Back to the follow camera**: the script sends 560/570 + 580 and then 500 (behind the player) or 501 (in front of the player);
`SetMode(0, arg)` resets the follow camera (3.8: 100 pre-simulation steps so it is already at its resting spot) and the transition
interpolates toward it. The engine itself switches back with a hard cut when the Perso leaves state 3 (first person)
(`0x45910e`) and on respawn (PERSO_FRAME: `0x41f9f0(2)`, `+0x368 = 0`, `SetMode(0,0)`).

### 6.2 Mode 8 = rail camera on a TRAJ (init `0x420e10`, update `0x421570`)

Message 540 `(cam, d)` sets `p3b0.traj (+0x4c)`, `p3b0.+0x44 = (float)d`, `SetMode(3, 0)`. Unlike what §4 suggested, `d` is
**not a speed but a distance**, and `p3b0+0x20` is overwritten every frame by `0x459090` (index 3) with the
**player position** (`+0x2c` = normalised look direction, `+0x50` bit 2/3 = jumper rising/falling):

* TRAJ points: `traj+0` low16 = count, `traj+0x10` = array with stride 0x10, xyz at `+4/+8/+0xc`; `traj+0xc` = total length (`0x437ca0`).
* Player y is filtered: `y = (1−k)·y + k·y_prev`, `k = 0.95^(30·dt)` (`0x4aa190` double, `0x4aa18c`, pow = `0x4995c0`);
  while rising/falling (`flags & 0xc`), y stays frozen at the previous value (`0x4215ad..0x421602`).
* Per segment `0x421c00(A, B, player, d²…)`: intersections of the segment with the **sphere of radius `d` around the player**
  (quadratic equation, discriminant < 0 → none); one candidate is chosen from the results (closest to the previous
  camera position `C+0x94`; details not fully read). No candidate → `0x420e40`: **closest point on the
  polyline** to the player (projection onto each segment, clamped to the endpoints, smallest distance²).
* The rail point `p3b0+0x38` moves toward the chosen point at max. `1000·dt` per frame (`0x4aa188`); on the first
  frame (`+0x50` bit 0 = 0) it jumps there directly. If `0x428ce0` (world cell) succeeds: `C->pos = p+0x38`.
* Orientation: look-at from the camera position toward `p+0x20` (player, filtered y) using `0x41af10` cross products
  (`0x421985..0x421afb`).

The TRAJ is thus a **rail along which the camera follows the player at a fixed distance**, not a path played back over time.
(A time-driven TRAJ player for cameras has not been found in the CamMgr.)

### 6.3 `0x41fbd0` is camera **shake**, not a blend

`0x41fbb0(m, t)` sets `m+0x67c = t` (remaining shake time; callers `0x40dd30`, `0x40eec0`, `0x458d37` with 2.0 s) and
`+0x694 = 0`. As long as `+0x67c > 0`, `0x41eff0` calls `0x41fbd0` after the mode update and subtracts dt. Only for modes
1, 2, 4, 8, 0x10, 0x20 (byte table `0x41ffb4`):

```c
a = min(m->shake, 3.0f);                                        // 0x4a988c
j = ((rand(1000) − 500)·0.01·a, ditto, ditto);                    // 0x43ff10(1000), 0x4aa0ac → ±5·a units per axis
dir = (m->target (+0x278) + j) − m->state.pos;  dir.y += m->lookOffset.y (+0xc8);
fwd = normalize(dir); right = normalize((0,−1,0) × fwd); up2 = normalize(fwd × right);
m->state = [I | −pos]·[R | 0];                                  // position unchanged, only the look direction trembles
```

## 7. Non-camera candidates

- **Type 42** (`0x452140`, 0x19c B, vtable `0x4ab148`, "kind" `[+0x104]&0x1f == 6`): projectile thrower.
  `0x452330(preset)` copies 0x68 B from table `0x5d7ba8[preset]` to +0x108; `0x452360(n, v)`
  sets parameter n (switch `0x4524fc`, 20 cases); `0x4522b0(count, interval, target)` starts
  repeated firing (tick `0x452780`: every `interval` s `0x452560` = normalise direction toward the target,
  projectile from pool `0x5d7e2c` (`0x4490a0`) or effect `0x44d5d0` + sound 0xe, play animation +0x17c);
  `0x452320` stops it. Messages 1000–1004 in `0x444870` belong to this.
- **`0x452e10`**: vtable slot 3 (tick) of class 20/21 (`0x452850`, 0x194 B, vtable `0x4ab1b8`); calls
  `0x440370` for its own instance orientation, not a camera.
- **`0x447210`** (+ `0x447660`, `0x447260`, `0x4480d0`, `0x447d70`): `game+0x30` object, HUD/GUI (calls
  `0x4484a0`/`0x461a70`; the frame function skips it if `[game]==2`).
- `0x4524fc` is not a function but the jump table of `0x452360`.
- `0x4510c0`/`0x4512c0`: class 50–52 (`0x450cd0`), use marker `0x42f6b0` and collision sphere `0x435810`/`0x4359b0` – not a camera.

## A. Helper functions (vec3 = 3×f32)

| address | meaning |
|---|---|
| `0x43ff80` | `v = (x,y,z)` |
| `0x43ffa0` | `v = 0` |
| `0x43ffb0` | `|v|²` |
| `0x43ffd0` | `v += a` |
| `0x43fff0` | `v = a + b` |
| `0x440020` | `v -= a` |
| `0x440040` | `v = a − b` |
| `0x440070` | `cos∠(v, a) = v·a / (|v||a|)` |
| `0x4400f0` | `v = a × b` |
| `0x440140` | `a · b` |
| `0x437710` | 3×4 matrix = identity |
| `0x437740` | `M = M · B` (3×4, row vectors) |
| `0x440370` | quaternion → 3×3 |

Constants: `0x4a9004`=0, `0x4a900c`=1, `0x4a9010`=100, `0x4a9014`=0.5, `0x4a9030`=50, `0x4a9760`=0.2,
`0x4a986c`=300, `0x4aa168`=1.1, `0x4aa16c`=1/2700, `0x4aa170`=3000, `0x4a94c0`=4, `0x4a988c`=3,
`0x4aa138`=1/4096, `0x4aa0ac`=0.01.

## 8. Open questions

1. **Near/far planes**: the projection code read (`0x47b372..0x47b429`) only contains the four side planes and
   `rhw = 1/w`; where near clipping and the z-buffer value (far) are set has not been found (renderer `[0x5e86ac]`,
   `0x42ac10`/`0x43b3f0`).
2. `p+0x30` (`CamMgr+0x368`) = 1 starts the slerp variant of the follow-camera init (3.8). All writers found set it to 0
   (`0x498c10`, `0x498c35`, `0x459123`, and with `ebx = 0`: `0x402aad`, `0x402b1a` (debug keys in `0x402940`), `0x445b5a`,
   `0x4562d2`). Nobody sets it to 1 → the slerp start is presumably dead code too.
3. `p->flags` bit 1 (reset `0x424940` with ray-forward-slide) and follow-camera state 4 (`+0x9b0`) are never
   set anywhere – presumably dead code from an earlier version. `0x422f10` ("SubCenter") computes an alternative step that
   is never applied.
4. `0x4359b0`/`0x497ed0` (ray) and `0x407340` (sphere test, push-out vector `[0x4c4bb4]`) have only been treated as a black box:
   `[0x53a554]` 0 = free, 1 = world geometry, 2 = instance (`[0x53a560]`); category-7 instances do not block
   the camera (which class is 7?).
5. Mode 8 (rail): the choice between multiple sphere intersections (`0x4216f0..0x4217dd`) has not been fully worked out.
6. Modes 2, 4, 0x10, 0x20, 0x40, 0x100, 0x200 (updates `0x4254c0`, `0x425810`, `0x4203c0`, `0x424bf0`, `0x420cf0`,
   `0x420010`, `0x425b80`) have not been read in detail; only their parameters (§1.2) and input (3.1).
7. `CamMgr+0x18` (instance for the starting look direction, `0x41e488`): writer not searched for.
8. Action 0xa: which key/button this is by default is in the input table (`[0x5e6188]`), not checked.
9. `0x41fb50(m, &pos, f)` (callers `0x459030`, `0x46496a`, `0x464aab`): helper routine "look from point `pos` toward the
   player" (mode 2, speed 100, smooth) – used by the engine itself, context not investigated.
