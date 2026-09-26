# Perso (player) – movement, input, physics, collision

Working document (updated incrementally). All addresses are in `Woody.exe` (image base 0x400000).
Notation: `P` = the Perso instance (struct 0x754 bytes, vtable `0x4aabc0`), `dt` = `P+0x2f8` (frame time in s).

## 1. Class hierarchy

| class | ctor | vtable | size | role |
|---|---|---|---|---|
| Instance (base) | `0x42e1a0` | `0x4aa31c` | 0xfc..0x104 | .ins instance: position `+0xc`, 3x3 rotation `+0x28`, scale `+0x4c`, world cell `+0x1c`, animation `+0xa0..0xb4` (see FORMAT_INS.md §2.6) |
| Npc | `0x40be10` | `0x4a9530` | 0x108+ | registers itself in the npc table `0x4c4e00[0x4c5318++]` (max 256, "Too many npc"); `+0x104` = typeword (bits 0-4 = category via `0x40c360`, bits 5-9 = subtype via `0x40c380`) |
| Perso | `0x44a2d0` | `0x4aabc0` | 0x754 | the player (script types 1, 2, 3, 18, 19 → classmap_raw.txt) |

Ctor `0x44a2d0(subtype)`: base ctors, sub-objects `+0x334` (ctor `0x462c50`, "jumper/gravity"), `+0x388` (ctor `0x47fa30`, "steering object": direction from camera + input), `+0x604` (ctor `0x4632c0`), `+0x110` (`0x4631b0`, plus `0x463280(column)`; jump table `0x44a394` on subtype−1: **subtype 1→column 0, 2→2, 3→1, 4→4, 5→3** – corrected, matches PERSO_FRAME §2.6). Script type → subtype (factory `0x403502`, table `0x403f3c`/`0x403e94`): type 1→1, 2→3, 3→2, 18→5, 19→4; so **type 1/2/3 = column 0/1/2** (the three walking characters; column 0 = type 1 = Woody) and type 18/19 = column 3/4 (max speed 1250: vehicle variants). Each variant writes the pointer to `[0x53a34c]` (`0x40361e`). Category (bits 0-4) = 1, subtype (bits 5-9) = ctor argument.

Vtable `0x4aabc0` (relevant slots):

| slot | offset | function | meaning |
|---|---|---|---|
| 1 | +0x04 | `0x44a3d0` | Init after loading (.ins position → `P+0x1f4`, start direction) |
| 17 | +0x44 | `0x44ab20` | Reset (respawn / level start) |
| 21 | +0x54 | `0x44c050` | return position + (0,1,0) |
| 22 | +0x58 | `0x44cda0` | script messages (1..999) |
| 26 | +0x68 | `0x44cf50` | render color: blinks white while timer `+0x704` is running (§4.4) |
| 34 | +0x88 | `0x44c030` | pointer to position `P+0x1f4` |
| 35 | +0x8c | `0x44c940` | ? |
| 36 | +0x90 | `0x44c720` | bool (used by 0x44c110 on npcs category 2 / subtype 12) |
| 38 | +0x98 | `0x44c110(kind)` | **Death/loss** (kind 1..9, jump table `0x44c498`) |
| 39 | +0x9c | `0x44ca00` | **Hit/damage** (called by enemies) |

## 2. Perso struct (fields)

(to be extended)

| offset | type | init | meaning | source |
|---|---|---|---|---|
| +0x0c | vec3 | .ins | render position of the instance; copied from `+0x1f4` every frame (`0x44bf10`) | |
| +0x28 | mat3 | .ins | rotation; rebuilt from the facing direction every frame (`0x44bd30`) | |
| +0x4c | vec3 | 1,1,1 | scale (`0x44ab20`) | |
| +0x54 | f32 | | z scale ← `+0x2e8` every frame (`0x44bd00`) | |
| +0x60 | vec3 | | "probe" point = position + (0, `+0x110`, 0), used for the world cell (`0x44bf10`) | |
| +0x104 | u32 | | typeword (Npc) | |
| +0x100 | f32 | 100.0 | (base instance) set by Reset `0x44ad6a`; 10000.0 in `0x44e690` | |
| +0x110 | f32 | 43.0 | = P+0 of the parameter block (table `0x4b5f14` row 0): height of the probe point above the feet; also max step-up in `0x436f00` | `0x463280` |
| +0x114 | f32 | 69.0 | P+4: collision radius | `0x462543` |
| +0x118 | f32 | 193/61 | P+8: current body height (`0x462490`: crouched `+0x694 ≠ 0` → P+0x10 = 61, else P+0xc = 193) | |
| +0x1f4 | vec3 | .ins pos | **position (feet)**; authoritative | `0x44a3d0`, `0x44bf10` |
| +0x204 | vec3 | 0 | **displacement this frame** (world space) = steering vector + jump/fall vector (+ dt·`+0x244` in y) | `0x44bb20` |
| +0x210 | vec3 | 0,1,0 | filtered "up" vector (0.9·old + 0.5·floor normal) for the rotation matrix in state 1 | `0x44bd30` |
| +0x21c | u32 | 0/1 | **state** (state), see §5 | `0x44c980` |
| +0x220 | u32 | | previous state | `0x44c980` |
| +0x22c | u8 | 1 | "on ground?" flag (getter `0x44bcf0`) | |
| +0x238 | f32 | 0 | **movement-lock timer** (`0x44cce0(t, force)`: max(old, t)); while > 0: no input, and the J vector is ignored (counted down with dt in `0x44b530`). Set by crouching (dt per frame), hard landing (len anim 10), special attack (len anim 0x13). Not invincibility (PERSO_FRAME calls it that — incorrect) | `0x44cce0` |
| +0x240 | f32 | 0 | timer (e.g. after death `0x44c110`: animation duration): while > 0 no gravity update | |
| +0x244 | f32 | 0 | extra vertical velocity this frame (added to y of `+0x204`, reset to 0 every frame) | |
| +0x24c | f32 | 1.0/3.0 | lives/"health"? (from save `+0x18`; Reset: 3.0 if ≤ 0) | `0x44a6a0`, `0x44ab20` |
| +0x250 | u32 | 0x63 | from save `+0xc` (score/coins?) | `0x44a6a0` |
| +0x25c | u32 | 0 | counter; at ≥ 25 → -25 and `+0x24c` += 1 (max 5) else `0x44c7a0(1)` | `0x44b530` |
| +0x26c | u32 | 0 | death kind (0 = alive) | `0x44c110` |
| +0x270 | f32 | 1.0 | **invincibility timer** (−dt in `0x44b1b0`): while > 0, `Hit` and `Kill` 2,3,4,5,6,8,9 are ignored; Reset sets 1.0 s; setter `0x44cd10` (max) | `0x44ca32`, `0x44c23d` |
| +0x280 | f32 | 0 | invincible after a hit (`0x44cd30`: max(old, P+0x5c = 0.6 s)) | `0x44ca49` |
| +0x2e0 / +0x2e4 | u32 / ptr | | wall contact from the sweep: 0 none, 3 world wall, 2 instance (+0x2e4 = instance) – NOT "ground type" | `0x462693` |
| +0x308 | u32 | 0 | **ground type** from the texture byte `tex+0x47` (1 = slick, 2 = dust), §6.4 | `0x462962` |
| +0x278 | s32 | -1 | countdown timer; at 0 → message 0x10 to the script (`0x443e90`) | |
| +0x288 | f32 | | duration/parameter of the death animation (3.5, 3.0, 2.5, 1.5, 0) | `0x44c110` |
| +0x2e8 | f32 | 1.0 | squash scale of the crush test (→ inst z scale +0x54 every frame, `0x44bd00`) | `0x462a40`, §6.6 |
| +0x2ec | ptr | | `[0x50944c]` level table | `0x44ae20` |
| +0x2f0 | ptr | | `[0x509adc]` time object: `[+0x38]` = dt | `0x44ae30` |
| +0x2f4 | ptr | | **controller** `[0x5e6188]` (see §3) | `0x44ae40` |
| +0x2f8 | f32 | | dt (copy, `0x44baf0`) | |
| +0x30c | vec3 | | level start position (copy of `+0x1f4` in Init) | `0x44a3d0` |
| +0x318 | vec3 | | respawn position (checkpoint / SavePos.bin) | `0x44a920` |
| +0x324 | vec3 | | respawn direction (normalized) | `0x44a3d0` |
| +0x334 | obj | | **jumper/gravity** object (ctor `0x462c50`) | |
| +0x388 | obj | | **steering object** (ctor `0x47fa30`; `+0x388+4` = P, `+8` = &P+0x110) | `0x459fd0` |
| +0x458 | vec3 | | floor normal (used for `+0x210`) | `0x44bd30` |
| +0x494 | ptr | | animation helper object (0x54 B, ctor `0x463df0`); `+0x49c`, `+0x4a0` = length of anim 3 and 0x42 | `0x44ad90` |
| +0x4b4 | ptr | | linked instance that receives position/rotation (`0x44bf10`) | |
| +0x4ec | u8 | | "stuck to a plane": plane `+0x4f0..0x4fc` (n, d) limits the displacement | `0x459eb0` |
| +0x5bc | vec3 / +0x5cd u8 | | displacement delivered by the jump/attack controller `0x457a50` (PERSO_JUMP.md); replaces the steering + J vector | `0x44bb20` |
| +0x604 | obj | | sub-object (ctor `0x4632c0`) | |
| +0x690 | u8 | | "state switched this frame → skip the rest of the update" | `0x44b530` |
| +0x69c | vec3 / +0x6ac u8 | | second override of the displacement | `0x44bb20` |
| +0x6e4.. | | | timer block (`0x465fe0`) | |
| +0x6f8 | f32 | | timer (counted down with dt) | `0x465fe0` |
| +0x710 | obj | | (0x453c80/0x453ca0: timer/HUD?) | |

## 3. Input

### 3.1 Cfg keys → action table (`0x44fbd0`, ctor of the app object `[0x5e5814]`)

Full detail (Woody.cfg layout, joystick object, dead zone, the port): **INPUT.md**.

The app object (paths, cd drive, `+0x104` = input mode, `+0x108` = action table) is created by `0x44fa10` (caller `0x401f00`).
`+0x104`: `cfg+0x114 == 1` → 0 (keyboard only); else `cfg+0x110 == 0` → 2, else 1 (joystick modes).
Action table `+0x108`: 12 actions x {key from config 1, key from config 2} (8 B per action). Every key code goes through `0x44fe80`: codes < 0x200 are translated by the DirectInput keyboard object `[0x5e6194]->vt[2](code)`; if that yields 0x90 or there is no keyboard → 0x90 (= invalid). Codes ≥ 0x200 are joystick buttons (button = code − 0x200, `0x44fed0`).

| action index | cfg index (+0xac / +0xdc) | default key (Detect) | controller action (§3.2) |
|---|---|---|---|
| 0 | 2 | ← | 0 (value −1.0) |
| 1 | 3 | → | 1 (+1.0) |
| 2 | 0 | ↑ | 2 (−1.0) |
| 3 | 1 | ↓ | 3 (+1.0) |
| 4 | 6 | LCtrl | 4 **and** 12 |
| 5 | 4 | Space | 5 |
| 6 | 5 | LShift | 6 |
| 7 | 8 | Enter | 7 |
| 8 | 7 | LShift | 8 |
| 9 | 9 | Esc | 9 |
| 10 | 10 | Num0 | 10 |
| 11 | 11 | RCtrl | 11 |

(The cfg index is in the order of the app table: `0x4c2c84,0x4c2c88,0x4c2c7c,0x4c2c80,0x4c2c94,0x4c2c8c,0x4c2c90,0x4c2c9c,0x4c2c98,0x4c2ca0,0x4c2ca4,0x4c2ca8` for config 1, same +0x30 for config 2.)

### 3.2 Controller object `[0x5e6188]` (14 actions x 12 B, + `+0xa8` = dt)

```c
struct PadAction { float value; float held; uint32 state; };   // state: 1 = just pressed, 2 = held, bit31 = just released
struct Pad { uint32 hdr; PadAction a[14]; float dt; };           // a[i] at +4+12*i
```
- `0x467340(dt)`: start of frame – store dt, all `state &= 0x80000000`.
- `0x4673b0(i, v)`: action i active: `state = (held <= 0) ? 1 : 2` (keeping bit 31), `held += dt`, `value = v`.
- `0x467370()`: end of frame – clear bit 31; actions not set this frame but with `held != 0`: `held = 0`, `state |= 0x80000000` (just released).
- Getters: `0x467400(i)` = pressed (state & 0x7fffffff), `0x467420(i)` = just pressed (state == 1), `0x467440(i)` = just released (bit 31), `0x467460(i)` = value (float; joystick axis).

### 3.3 Polling per frame (`0x402940`, method of the main object)

1. `[this+8]->vt[0xc]()` (keyboard update), debug keys via `vt[0x14](scancode)` if `app+0x384 & 8` (debug mode).
2. Joystick `[0x5e618c]` (modes 1/2 only): `vt[4]()` poll; X axis `vt[0xc]` > 0 → action 1 = X, < 0 → action 0 = X; Y axis `vt[0x10]` > 0 → action 3, < 0 → action 2. Buttons (via `0x44fed0`) for actions 6, 11, 5, 4(+12), 7, 10, 8, 9.
3. Keyboard `[0x5e6194]` (`vt[0x10](key)` = pressed): directions only in mode 0; actions 4(+12), 5, 6, 11, 7, 8, 10, 9 always. Values as in the table above.
4. `0x467420(9)` (Esc just pressed) → pause menu `0x404d80`.
5. `0x467370()` closes out.

The player thus **never** reads keys directly; everything goes through `P+0x2f4` (= `[0x5e6188]`) with action indices 0..13.

### 3.4 Meaning of the 14 actions for Perso (read sites; `h` = `0x467400` pressed, `p` = `0x467420` just pressed, `r` = `0x467440` just released, `v` = `0x467460` value)

| action | default | meaning | where read |
|---|---|---|---|
| 0 / 1 | ← / → | **left / right** (value −1 / +1 → "b") | Mover input `0x45a506..0x45a528` (h+v); altMode `0x45a7be/0x45a7cb`, `0x459c9d/0x459cae`; state 4 `0x4651f5/0x465205`, `0x46539f/0x4653d2` (v); state 1 `0x4565f1..0x456636` |
| 2 / 3 | ↑ / ↓ | **forward (away from the camera) / backward** (value −1 / +1 → "a"); angle = π − atan2(b, a) | `0x45a4cb..0x45a4fd`; altMode `0x459d57/0x459d64` |
| 4 | LCtrl | **jump** (hold = high jump, release = cut short, §4.2) | `0x44bb10` (h) → `0x462d70`; also in the attack controller `0x457adf`, `0x457c34`, `0x457deb` (h), `0x465a14`, `0x465e7d` (p) |
| 5 | Space | **crouch** (sub-state machine `+0x694`: 0→1 anim 0x31 (going down), 2 anim 0x32 (holding), 3 anim 0x33 (standing up); standing up only if segment y+P+0x10 (61) → y+P+0xc−P+0x10 (132) is free (corrected), `0x4359b0` → `[0x53a554] == 0`; in state 6 anims 0x4e/0x4f/0x50, in state 1 action **8** instead of 5 with anims 0x68/0x69/0x6a). During crouching every frame `0x44cce0(dt,0)` ⇒ `+0x238 > 0` ⇒ no horizontal movement. **Fully worked out (state machine, hitbox 61, camera, port recipe): PERSO_DUCK.md** | `0x465b69` (h) in `0x465b10`; altMode `0x459d77` |
| 6 | LShift | **attack (peck)**: on the ground hold/release (r `0x457416`, h `0x4574b5`) → sub-state 9 (anim 0x10/0x11, `0x45745a`); in the air just pressed (`0x4574f4`) → sub-state 1; combo in sub-state 5 (`0x4573d4`). Details: PERSO_JUMP.md | `0x457330`; further `0x46344c`, `0x463954`, `0x464f48`, `0x465216`, `0x465a25` (p; in the code of state 6/4/8 – not analyzed further) |
| 7 | Enter | **look-around mode** (state 3, camera mode 0x200) on/off on release. **Fully worked out (entry conditions, eye camera, limits, the bomb bug, port): [PERSO_LOOK.md](PERSO_LOOK.md)** | `0x44b9b7` (r) in `0x44b980` |
| 8 | LShift (cfg 7) | crouch variant in state 1 (see action 5) | `0x465b65` |
| 9 | Esc | pause (not read by Perso) | `0x402940`, `0x401500`, `0x403334` |
| 10 | Num0 | **put the camera behind the player** (0.5 s transition; sound 9 if not allowed) | camera controller `0x459163`, `0x45922d`, `0x45926a`, `0x4597a3` |
| 11 | RCtrl | **special attack with stock** `+0x254` (r, on the ground, state 0, `+0x750 == 0`): stock−1, anim 0x13, movement locked for the animation duration (`0x44cce0`), at 1.5 s **all** actors in `0x4c5258[]` (no range test) get `vt[39]`(damage P+0x94 = 3.0, kind 2) and possibly `vt[38](3)` (= `ret 4`); else sound 9. Full: [PERSO_SPECIAL.md](PERSO_SPECIAL.md) | `0x458c0c` in `0x458bf0` |
| 12 | = key of 4 | menu confirm (`0x4465c6`, p) – not read by Perso | |
| 13 | – | never set or read anywhere | |

`0x44cc30` = "one of the actions 0,3,2,1,6,4,10,5 pressed" (used to interrupt idle animations).

## 4. Movement logic

Overview per frame (state 0/6, `0x44bb20(P, 1)`; details laid out horizontally in PERSO_FRAME §2.3):

```c
void Perso_Move(Perso *p, bool input)                 /* 0x44bb20 */
{
    if (Timer6e4(p)) return;                           /* 0x465fe0 */
    if (p->M.pushTimer /*+0x474 = M+0xec, knockback*/ > 0 || p->t238 > 0 || p->sub5b4 != 0)  /* 0x44bb48..0x44bb78 (OR, not AND: §7) */
        input = false;
    Mover_Update(&p->M, input);                        /* 0x45b110: PERSO_FRAME §2.3 */
    vec3 h = p->M.velDir * p->M.dist;                  /* 0x44d1e0: M+0x1c * M+0xe4 (= speed*dt) */
    vec3 v = {0,0,0};
    if (p->t240 > 0) p->t240 -= dt;                    /* 0x44bc04: no falling during the death animation */
    else { Jumper_Update(&p->J, Pressed(4), input);    /* 0x462d70, §4.2; 0x44bb10 = 0x467400(4) */
           v = p->J.disp; }                            /* 0x463130: J+8..0x10 (only y is filled) */
    if (p->t238 > 0) v = 0;                            /* 0x44bc2f */
    if      (p->use5bc) p->disp = p->v5bc;             /* +0x5cd: attack/special jump (PERSO_JUMP.md) */
    else if (p->use69c) p->disp = p->v69c;             /* +0x6ac */
    else                p->disp = h + v;               /* 0x44bc94 */
    p->disp.y += dt * p->vy244;                        /* 0x44bcbe */
    ClampToPlane(p);                                   /* 0x459eb0 (+0x4ec only) */
}
```

### 4.1 Horizontal (summary; all verified in PERSO_FRAME §2.3)

Column 0 (Woody): max walking speed **600 units/s** (P+0x1c), accelerating in **0.25 s** with `v = (t/0.25)²·target`,
decelerating in **0.1 s** with `v = max·(1−(t/0.1)²)`; there is **no separate run button**: the speed scales with the
stick deflection (keyboard = ±1.0 → always 600) and with `(dot(newDirection, oldDirection)+1)/2` (sharp turns brake).
The facing direction turns each frame with `slerp(old, target, |stick|·0.25)` (`0x45a320`, **framerate-dependent**, no dt).
Desired direction = `normalize(pos − camPos)` in xz, rotated around y by `π − atan2(x, y)` (`0x45a4b0`).

### 4.2 Vertical: the jumper object `J = P+0x334` (`0x462d70`) – **no gravity constant, but parabolas over time**

Layout J: `+0` Perso, `+4` parameter block (P+0x110), `+8..0x10` displacement this frame (only `+0xc` = dy),
`+0x14` phase, `+0x18` "speed" S, `+0x1c` t (s, negative while rising), `+0x20` previous height h, `+0x24` last measured
fall speed (+ = downward), `+0x28` fallen height (= `Perso+0x35c`, see PERSO_FRAME), `+0x2c/+0x30/+0x34` = parameter
indices **0x19 / 0x1b / 0x1a** (`0x462c90`), `+0x38` = P[0x1a] = **P+0x68 = 650**, `+0x3c` = P[0x1b] = **P+0x6c = 600**, `+0x40` = P[0x19] = **H = P+0x64 = 380
(jump height)**, `+0x44` = **P+0x70 = 2000 (terminal fall speed)** (`0x462ce0`), `+0x48` coyote timer, `+0x4c` "jump rearmed",
`+0x4d` fell without jumping, `+0x4e` long fall (> P+0x7c), `+0x4f` jump cut short, `+0x50` coyote active.

```c
void J_StartJump(J *j) { j->S = j->p38 * 0.75f;              /* 0x462d10; 0x4aabb4 = 0.75 → 487.5 */
                         j->t = -(j->S / j->p3c) * 0.5f;     /* 0x4a94bc = -0.5 → t = -0.40625 s */
                         j->h = 0; j->vfall = 0; j->cut = 0; j->noJump = 0; }
void J_StartFall(J *j, bool noJump) { j->S = j->p38 * 1.25f; /* 0x462d40; 0x4ab798 = 1.25 → 812.5 */
                         j->t = 0; j->h = j->H; j->noJump = noJump; }

bool J_Tick(J *j, float dt)                                  /* 0x462fd0 */
{
    j->t += dt;
    float T = (j->S / j->p3c) * 0.5f;                        /* rise 0.40625 s; fall 0.67708 s */
    float u = j->t / T;
    float h = j->H - u*u*j->H;                               /* parabola, peak (H) at t = 0 */
    if (j->t >= -0.15f && j->phase == 1 && !j->cut) {        /* 0x4ab7a0 = -0.15 */
        0x457560(perso, 0.5f, 0); 0x465e00(perso, 0.5f, 0);  /* anim transition to the peak */
        j->phase = 7; }
    if (j->t >= 0 && (j->phase == 7 || (j->phase == 1 && j->cut))) {
        j->phase = j->cut ? (OnGround(perso) ? 6 : 4) : 3;
        J_StartFall(j, 0); h = j->H; }                       /* 0x463087 */
    if (u > 1.0f) {                                          /* past the parabola: towards terminal speed */
        float k = min(u - 1.0f, 0.5f);
        j->disp.y = -((j->vmax /*2000*/ - j->vfall) * 2*k + j->vfall) * dt;   /* 0x4630c7 */
    } else { j->disp.y = h - j->h; j->vfall = -(h - j->h) / dt; }            /* 0x4630e1 */
    j->h = h;
    return !(u < 0.45f && !j->cut);                          /* 0x4ab79c = 0.45 */
}

void Jumper_Update(J *j, bool pressed, bool input)           /* 0x462d70, table 0x462fa4 */
{
    if (j->phase == 2) { j->disp = 0; j->fallen = 0; j->longFall = 0; J_LoadParams(j); }
    if (!input) pressed = false;
    if (perso->heightAboveGround /*+0x228*/ <= P[0x84] /*100*/ && !pressed && input) j->armed = 1;
    if (j->coyote) { j->tCoyote += dt; if (j->tCoyote > 0.15f) j->coyote = 0; }   /* 0x4aa1c8 */
    switch (j->phase) {
    case 2: /* on the ground  0x462dff */
        if (pressed && OnGround() && j->armed) { jump: j->phase = 0; J_StartJump(j); J_Tick(j, dt); j->armed = 0; return; }
        if (OnGround()) return;
        j->coyote = j->armed; j->tCoyote = 0;               /* walked off an edge */
        j->phase = 3; J_StartFall(j, 1); J_Tick(j, dt); j->armed = 0; return;
    case 0: j->phase = 1;                                    /* 0x462e8e, falls through */
    case 1: /* rising  0x462e95 */
        if (!pressed && j->t < -0.2f) {                      /* 0x4aa430: button released → short jump */
            j->cut = 1; j->t = -0.2f;
            float u = -0.2f / ((j->S / j->p3c) * 0.5f); j->h = j->H - u*u*j->H; }  /* no position jump */
        J_Tick(j, dt); return;
    case 7: /* peak  0x462eee */  b = J_Tick(j, dt); if (OnGround() && b) j->phase = 6; return;
    case 3: /* start falling  0x462f14 */
        if (J_Tick(j, dt)) j->phase = 4;
        if (j->coyote && pressed) { perso->anim494->vt[4](); j->coyote = 0; goto jump; }   /* coyote jump ≤ 0.15 s */
        return;
    case 4: if (j->fallen > P[0x7c] /*1500*/) { j->longFall = 1; j->phase = 5; }            /* 0x462f52 */
    case 5: b = J_Tick(j, dt); if (OnGround() && b) j->phase = 6; return;                  /* 0x462f6a */
    case 6: j->phase = 2; return;                            /* landed (0x44b220 reads phase 6 → fall damage) */
    }
}
```

Numbers for column 0/1/2 (Woody et al.; column 3/4: H = 400, p38 = 1250, p3c = 1250):

| quantity | formula | value |
|---|---|---|
| jump height (button held) | H = P+0x64 | **380** units |
| time to the peak | 0.5·0.75·650/600 | **0.40625 s** |
| equivalent initial speed / g while rising | 2H/T ; 2H/T² | 1870.8 units/s ; **4605 units/s²** |
| short jump (button released before t = −0.2 s) | t jumps to −0.2 s; remaining rise H·(0.2/T)² | +92.1 units above the release point |
| falling parabola | T = 0.5·1.25·650/600 = **0.67708 s**, h = H(1−(t/T)²) | g = **1657.8 units/s²** until 380 fallen (v = 1122.5) |
| after that | v = v_last + (2000 − v_last)·2·min(t/T − 1, 0.5) | linear to **2000 units/s** in 0.3385 s, then constant |
| coyote time | `0x4aa1c8` | 0.15 s |
| jump rearm | button released and height above ground ≤ P+0x84 | 100 |
| fall damage | J+0x28 (fallen height) ≥ P+0x7c on landing (`0x44b220`) | 1500 → −1 heart |

Because the vertical displacement is a height difference from a fixed curve (not an integrated velocity), the
jump is framerate-independent. `0x463170(J, force)`: force falling (phase 4, vfall 0) unless already in phase 3/4/5 and `!force`.

### 4.3 Animations per state (`0x463e60`, table `0x463f14` on state−1; anim start = `P+0x494->vt[2](id)`, tick = `vt[3](dt)`)

| state | function | animations |
|---|---|---|
| 0 (and 9) | `0x464630` – only if `+0x5b4 == 0` and `+0x694 == 0` (no attack, not crouching) | J phase 2 (ground) → `0x463f40(0)`; else `0x4642f0(0)` |
| 1 | `0x464c20` | air set 0x6b/0x6c/0x6d/0x6e/0x6f (`0x464302`) |
| 2 (death) | `+0x4d8` ? `0x464a00` : `0x464790` | |
| 3 (looking around) | inline `0x463e77` | anim 0 |
| 4, 5, 8 | – (state handles its own animation) | |
| 6 | `0x4646b0` | ground 0x41/0x42, air set 0x47/0x4a/0x49/0x4c/0x4b (`0x464333`) |
| 7 | no anim and no controller tick (`0x463f11`) | |

If camera mode `cam+0x138 == 2`: always anim 1 (`0x463ec8`).

**Ground (`0x463f40`)**, on Mover phase `M+0xc` (table `0x4642d4`): phase 1 (accelerating) → **anim 2** (start walking); phase 2 (at speed) →
**anim 3 = walk cycle** with duration `len3 / max(0.5, clamp(M+0x44 / M+0x48, 0, 1))` (`0x436c20`; `len3 = P+0x49c`): so the cycle runs at
half speed at ≤ 50% of max speed. Footstep effect `0x47cba0(pos, normal, direction, left/right, ground type 2|3)` when the
cycle fraction passes 0.38 (`0x4ab278`) resp. 0.9 (`0x4a94b8`). Phase 3..6 → animation stays; phase 0 → **idle** `0x464500`:
timer `+0x230 += dt`; < 10 s (`0x4a9750`) → **anim 0**; at 10 s randomly (`0x43ff20(0,7)` = `rand() % 7`, so **1 in 7**) **0x5a** (.ins 89, until `T ≥ 2·L(0x5a) + 10`, then reset), else **anim 0x59** (.ins 0 → 88 → 87 = sleeping); once `.ins` anim 87 (`P+0xb0 == 0x57`) is playing and `T > 10.5` (`0x4ab7c8`) and `P+0x52c == 0`: `P+0x52c = 1` and zzz balloon `0x478980(P, 4, 2.5, 130, 50, &P+0x52c)` (lives until the reset); reset
(`0x464620`) as soon as an action key is pressed (`0x44cc30`).

**The frame of `0x464630`** (state 0, read in full in round 29): nothing at all while `+0x5b4` (attack sub-state) or `+0x694`
(ducking) is non-zero - no animation request, no idle tick and no reset, so the idle count is **frozen** through a peck or a
duck. Otherwise `0x463f40(0)` on the ground resp. `0x4642f0(0)` in the air; `0x463f40` answers "idle" (`bl = 1`) only for
Mover phase 0 (phases 1/2 walk, 3..6 jump table `0x4642d4` → `0x463f99` clear it) and `0x4642f0` always answers 0. Idle ⇒
`0x464500` if `+0x21c == 0` (state 9 skips it without a reset); not idle ⇒ reset `0x464620`. Then, idle or not, a held action
0/3/2/1/6/4/10/5 (`0x44cc30`, `0x467400` = held) **or `+0x550`** (the root-position flag of `0x44e290`, i.e. a scripted action
or the climb-over carrying the model) resets it. A hit (`+0x5b4 = 0`, Mover phase 0 under the knockback) and the special attack
leave the count running: their animations simply outrank the idle requests. Ported (`player.c`, the anim block of
`player_update`): the idle tick moved out of the priority chain so it runs under a hit or the special attack too; the reset on
`use_root` and the freeze on `atk` / `duck` are in; state 6 still resets every frame (it has no idle variations).

**Air (`0x4642f0`)**, on J phase (table `0x4644dc`): 0/1 rising → **anim 4**; 7 peak → **anim 5**; 3 start falling → anim **7** if fallen off an edge
without jumping (`J+0x4d`), **6** if the jump was cut short (`J+0x4f`), else stays at 5; 4 falling → **anim 8** as soon as onGround (landing, unless Mover phase 2),
else stays at phase 3; 5 long fall → **anim 9**, on the ground **anim 10 (hard landing)**: movement locked for the animation duration
(`0x44cce0(len10, 0)`, `+0x524 = len10`, camera shake `0x478980(p, 1, 2.0, 180.0, 50.0, 0)`); 6 → dust effect `0x476140` if ground type `+0x308 == 2`.

### 4.4 Damage, death, reset (vtable methods)

**`0x44ca00` vt[39] `Hit(attacker, damage, &direction, &pos, kind)` → bool "dead"** (ret 0x14). Ignored if the same
attacker is already registered (`+0x5f0 == attacker ≠ 0`), if `+0x270 > 0` (invincible after respawn: Reset sets 1.0 s),
if `+0x280 > 0` (invincible after a hit), or cheat `[0x5d7b8c]`. Otherwise: effect `0x40c2d0(damage, pos)` if pos ≠ 0; force
feedback `0x44d1b0(P+0xbc, P+0xb8)` (**`0x44d1b0` is joystick rumble `[0x5e618c]->vt[2]`, not knockback** – see §7); `0x463170(J, 0)` (start falling);
**knockback** `0x45a140(M, direction)`: if push timer `M+0xec ≤ 0`: RampC target = RampC max (**500**), accelerate (0.1 s), RampC.dir = direction
(or (0,1,0) if |direction| < 0.01), `M+0xec = P+0x58 =` **0.2 s**, then decelerate over 0.5 s (`0x45acb0`); facing direction `M+0x10`, `M+0x1c` and
RampA.dir = normalize(−direction.xz) (the player looks at the attacker; (1,0,0) if the length < 0.01); hit animation `0x464b70`; `+0x280 = max(+0x280, P+0x5c =` **0.6 s**`)`
(`0x44cd30`); `+0x238 = 0` (`0x44cce0(0, 1)`); state 4 → 0; `+0x5f0 = +0x5b4 = 0`, `+0x550 = 0`; `health(+0x24c) −= damage`; ≤ 0 ⇒ 0 and return 1
(the caller then does `vt[38](3)`).

**`0x44c110` vt[38] `Kill(kind)`** (table `0x44c498`; subtypes 4/5 → own variant `0x44c4c0`; the `0x44c4ba` mentioned in the
task lies in the middle of that jump table and is not a function). Ignored in cinematics (`0x44f2e0/0x44f2d0`), on
`App+0xcc`, if an actor of category 2 / subtype 12 has `vt[36]()` active, and if `+0x26c ≠ 0` (already dead; exceptions
`0x44c1e6`: kind 7 may override any death other than 7, kind 1 any other than 1). `+0x288` (duration until the fade,
used by `0x4459c0`) defaults to **3.5 s**.

| kind | code | effect |
|---|---|---|
| 1 | `0x44c297` | camera shake `0x478980(p,0,2.5,180,50,0)`, `+0x240 = len(anim 0x2f)` (no falling during the animation), `0x463170(J,0)` |
| 2, 9 (lightning) | `0x44c3ab` | rumble, `+0x288 = 1.5`; cheat `[0x5d7b8a]` or `+0x270 > 0` ⇒ ignored; effect `0x477e40(pos + (0, P+0xc=193, 0))`, `+0x240 = len(anim 0x30)` |
| 3 (health restore), 8 (fall damage) | `0x44c230` | cheat `[0x5d7b8b]` or `+0x270 > 0` ⇒ ignored; `+0x288 = 3.0`; `0x463170(J,0)` |
| 4, 5 | `0x44c26f` | `+0x270 > 0` ⇒ ignored; `0x463170(J,0)` |
| 6 (explosion `0x44d040`) | `0x44c2d6` | same, `+0x288 = 2.5` |
| 7 | `0x44c308` | `+0x288 = 0` (immediate fade); effect `0x478660(instpos + (0,110,0), speed `0x44d170`, 50.0)`, camera `0x459030`, attack/J/colliders reset (`0x462c90`, `0x436d10`x2) – "vanish" (water/pit, by script/volume) |

Always afterward (`0x44c443`): `health = 0`, `+0x274 = 0`, `+0x26c = kind`, `+0x4d8 = 0`, `+0x550 = 0`, **state := 2**, `+0x268 = 1`.
In state 2, `0x44bb20(p, 0)` keeps running: no input, but J still lets the player fall (except during `+0x240`), and
`0x4624f0` no longer applies the wall correction (§6.1).

**`0x44ab20` vt[17] Reset**: scale 1, `+0x2e8 = 1`, up `+0x210 = (0,1,0)`, `0x459ff0(M, &+0x324)` (facing direction = start direction), `0x462c90(J)`
(phase 2, `armed = 1`), anim controllers `vt[4]()`, `onGround = 1`, `+0x228 = 0`, **`+0x270 = 1.0`** (1 s invincible), `+0x280 = +0x238 = +0x240 = 0`,
`health = 3.0` only if health ≤ 0, state := 0 (subtype 4/5: `0x456150` + state 1), colliders `0x436d10`x2, all sub-states 0,
`+0x100 = 100.0` (note: **`+0x100`**, not `+0x110`; `+0x110` = P+0 = 43), `0x44e7f0`, `0x44db10`.

**`0x44cf50` vt[26]** = render color: while timer `+0x704` is running, the player blinks white (`[0x5ac850] = 2`, color 255,255,255; period 0.5 + 0.1 s if
`+0x704 < 5.0`) – no effect on movement.

## 5. State machine (`P+0x21c`)

Dispatch in the per-frame update `0x44b530` via jump table `0x44b950`:

| state | handler | note |
|---|---|---|
| 0 | `0x44bb20(1)` + `0x4624f0` | normal (walking/standing) |
| 1 | `0x456210` + `0x4624f0`, then `0x4567f0` | (hanging/climbing? – `0x44bd30` uses the floor normal in state 1) |
| 2, 3 | `0x44bb20(0)` + `0x4624f0` | without input (death `0x44c110` sets state 2) |
| 4 | `0x4651d0` (+0x35c = 0, +0x382 = 0) | reads the controller |
| 5 | `0x44db50` | respawn/level transition? (`0x44a650` refuses teleport in 5) |
| 6 | `0x44bb20(1)` + `0x4624f0` | variant of 0 with `+0x590` object (`0x44c980`) |
| 7 | `0x44e1c0` | |
| 8 | `0x4657f0` | `0x44bd30` skips the rotation update |
| 9 | `0x454090` | |

Before the dispatch, every frame (unless `+0x690` is set): `0x464ef0`, `0x465e50`, `0x457a50` (1192 instr, attacks), `0x44ba70`, `0x465b10`, `0x44b980`, `0x458bf0`; then `0x459c70` if `+0x4ec`, `0x45b0a0` (steering object), after the dispatch `0x44bcf0` check → message 0x200 to the script (`0x443e50`/`0x443e90`), `0x44b2e0`, `0x44bd00` (matrix + position), `0x463e60`.

## 6. Collision with the world

Addition/correction to PERSO_FRAME §2.4. Everything below is in `0x4624f0` (Perso_MoveCollide) and what it calls.
Global results of the collision routines: `[0x4c4bd0]` raw result (1 = nothing, 3 = world polygon, 4 = instance), `[0x4c4bd4]` distance,
`[0x4c4bc0..cc]` plane, `[0x4c4bd8]` polygon index, `[0x4c4bdc]`/`[0x4c4be0]` instance index/press node; translated by the wrappers to
`[0x53a554]` (0 = nothing, 1 = world, 2 = instance, 3 = "wall hit" from `0x437180`), `[0x53a558]` t/distance, `[0x53a560]` instance*,
`[0x53a568]` **ground height**, `[0x53a58c]` node, `[0x4b3108..14]` plane (normal + d) of the ground, `[0x4b3118]` **collision radius**.

### 6.1 Order in `0x4624f0`

```c
void Perso_MoveCollide(Perso *p)
{
    p->H = p->duck694 ? P[0x10] /*61*/ : P[0x0c] /*193*/;          /* 0x462490 → P+0x08 (+0x118) = body height */
    PushFromActors(p);                                             /* 0x4627d0: PERSO_FRAME §2.4 (circles, r_other − 5.0) */
    g_radius = (p->state == 1 && p->duck694) ? P[4]*0.5f : P[4];   /* 0x434820; P+4 = 69 */
    p->oldPos = p->pos;                                            /* +0x28c */
    vec3 np = p->pos + p->disp;
    vec3 carry = AttachDelta(&p->att298);                          /* 0x436d20: movement of the platform you were standing on:
                                                                      world(local point in instance/node) − previous world position */
    np += carry;
    vec3 c2 = WallAttach(&p->att2bc, &np);                         /* 0x437040: DEAD CODE – the test 0x435b60 is a stub
                                                                      ([0x53a554]=0), so c2 = 0 and att2bc is cleared */
    if (p->state != 2) { np.x += c2.x; np.z += c2.z; }
    SweepCylinder(&p->pos, &p->oldPos, np, p->H * p->scaleZ,       /* 0x437180(…, 40.0, 10.0, P[0]); §6.2 */
                  /*step*/ 40.0f, /*substep*/ 10.0f);
    p->wall2e0 = g_hitType;  p->wallInst2e4 = g_hitInst;           /* 0 / 3 (world wall) / 2 if the wall is an instance */
    /* floor: */
    vec3 probe = p->pos + (0, P[0] /*43*/, 0);
    bool hit = FloorAttach(&p->att298, -1, &probe, P[0], p->id);   /* 0x436f00 → 0x435650 (GetHeight, §6.3) */
    p->groundY = g_groundY;  p->heightAbove = p->pos.y - g_groundY;   /* +0x224, +0x228 */
    if (hit) { p->onGround = 1; p->pos.y = g_groundY; } else p->onGround = 0;
        /* hit ⇔ probe.y − (groundY + 43) < 1.0  ⇔  pos.y − groundY < 1.0  (also if the floor is up to 43 HIGHER: step-up) */
    GroundInfo(p);                                                 /* 0x4628e0, §6.4 */
    VolumeTest(p, 71.0f);                                          /* 0x462760 → 0x4347b0: trigger volumes at pos + (0,71,0) (EVENTS.md) */
}
```

`0x436f00` additionally does: if the player is standing on an **instance** (`[0x53a554] == 2`), `0x436d80(att, inst, node, &probe)`: remember
the local point (`0x431700` world→node) so `0x436d20` can pass on the platform movement next frame; if the press node
has flag `(flags & 0xff00) == 0x100`, collision id = `inst+0x70[(flags >> 16) + model+0x48]` → script events **PersoPress `0x441fc0`** (new),
**PersoIn `0x442000`** (same as previous frame, `att+0x20`), **PersoUnpress `0x442040`** (released). Not on an instance ⇒ `att` cleared (`0x436d10`).

### 6.2 `0x437180` – moving in substeps with cylinder push-out

```c
void SweepCylinder(vec3 *out, vec3 *old, vec3 target, float H, float step /*40*/, float sub /*10*/)
{
    float half = H * 0.5f;                                   /* 96.5 standing, 30.5 crouched */
    vec3 cur = *old + (0, half, 0);                          /* body center */
    float dy = target.y - old->y;
    int mode = dy > 0.1f ? 3 /*rising*/ : dy < -0.1f ? 2 /*falling*/ : 0;      /* 0x4a9008, 0x4aa3d0 */
    vec3 d = (target + (0,half,0)) - cur;
    int n = (int)(floor(|d| / sub) + 1 + 0.5);  d /= n;      /* substeps of max. 10 units (0x499ede/0x499580) */
    float margin = 5.0f;                                     /* bottom of the wall test above the feet, in the air */
    GetHeight(&cur);                                         /* 0x435650 */
    if (cur.y - half - 1.0f < g_groundY) { margin = step + 1.0f;  /* 41: on the ground, the lowest 41 units are NOT
                                                                     tested against walls ⇒ **step-up height ≈ 40** */
                                           if (mode == 0) mode = 1; /* walking over the ground */ }
    g_hitType = 0; g_hitInst = 0;
    for (; n > 0; n--) {
        vec3 prev = cur;  cur += d;
        float feet = cur.y - half;
        g_bandLo = feet + margin;  g_bandHi = feet + H;      /* [0x53a54c], [0x53a350] */
        CylinderVsWorld(&cur, g_radius, /*up*/ g_bandHi - cur.y, /*down*/ cur.y - g_bandLo, -1);   /* 0x407000, §6.5 */
        if (g_raw /*[0x4c4bd0]*/ != 0) { cur.x += g_push.x * 0.9f;  cur.z += g_push.z * 0.9f;       /* 0x4a94b8 = 0.9 */
                                         g_hitType = 3; g_hitInst = [0x53a560]; }
        GetHeight(&cur);  float gy = g_groundY;
        if (mode == 2) { if (cur.y - half < gy) cur.y = gy + half; }                 /* landing: don't go through the floor */
        else if (mode == 1) { float f = cur.y - half;
                              if (f - gy < step /*40*/ && f > gy) cur.y = gy + half; }   /* **stick to the ground** while descending (≤ 40 per substep) */
        /* the head ray 0x43744f..0x43753d. [esp+0x60] = H - half and [esp+0x10] = (H - half) + d.y (0x4372ed: [esp+0x64] is
           the argument H - the same one the band top feet + H uses at 0x437358 - not the step, which is [esp+0x68]); with
           |d| <= 10 that is positive for every substep, standing or crouched, so the ray runs every substep. */
        if ((H - half) + d.y > 0) {
            vec3 head = (cur.x, cur.y + (H - half), cur.z);         /* the top of the head after the clamps above */
            vec3 from = prev;                                        /* the centre before this substep ([esp+0x38..0x40]) */
            int k = Ray(from, head);                                 /* 0x4359b0(&prev, &head, -1), §6.6; k = [0x53a554] */
            if (k == 3) { from = (cur.x, gy + 0.1f, cur.z);          /* prev inside a press node: 0x4374d1, 0.1 = 0x4a9008 */
                          k = Ray(from, head); }
            if (k != 0) { cur.y = from.y + (head.y - from.y) * t - (H - half);   /* the head at the hit point, 0x4374a6 / 0x43750c */
                          if (cur.y - half < gy) cur.y = gy + half; }        /* ... but never below the floor, 0x4374c4 / 0x437535 */
        }
        *out = (cur.x, cur.y - half, cur.z);
    }
    [0x53a554] = g_hitType; [0x53a560] = g_hitInst;          /* return 0 → P+0x200 = 0 */
}
```

Consequences for a reimplementation:
* **No iterative "slide"**: per substep (≤ 10 units) a single push-out vector in xz, x0.9; the sliding along walls arises
  because only the component along the polygon normal is pushed back. At 600 units/s and 60 Hz that's 1 to 2 substeps per frame.
* **Player dimensions (column 0)**: radius **69**, height **193** (crouched **61**), wall test from feet+41 (ground) or feet+5 (air) to feet+H,
  with the cone bottom of §6.5 below the body centre.
* **GetHeight probes from the body centre** (`0x4372a2`, `0x4373d5`: `&cur`, not feet + 43), so while falling (mode 2) any floor
  between the feet and the centre (up to 96 above the feet) lifts him onto it; walking (mode 1) only clings downwards.
* Against a wall, `[0x53a554] = 3` (`0x4373a7`) whenever `0x407000` reported a hit, even with a zero vector; that is `P+0x2e0`, read by
  `0x45ae50` next frame: `0x4672d0(RampA, 0.25)`: only in the braking phase (`+0x2c == 3`) the phase timer moves a quarter of the
  way to T_dec (`t += (T − t)·0.25`), i.e. he stops faster against a wall.
  The wall test never blocks the vertical displacement; **ceilings** are the head ray's job: every substep the segment from the
  previous body centre to the new top of the head (`feet + H`) is cast with `0x4359b0`, and a world polygon or press-node polygon
  facing the centre on it (or the centre starting inside a press node) puts the head at the hit point - never lower than
  `floor + half`. So a jump into a ceiling stops there (the head stays under it); on the ground it changes nothing, because of the
  floor clamp. `0x437180` does not tell the jump controller J: the rise goes on against the ceiling until the arc turns.
  The squeeze from above on the ground is the crush test §6.6.
* Stepping up to 40–43 units happens "for free": the wall test ignores the bottom 41, and `0x436f00` sets `pos.y = ground`
  as soon as `pos.y − ground < 1` (the probe starts 43 above the feet, so higher floors up to +43 are found).
* Descending: sticks up to 40 per substep; if the floor is further than 1.0 after the sweep ⇒ `onGround = 0` ⇒ J goes to
  phase 3 (falling, §4.2).
* Against a wall (`P+0x2e0 ≠ 0`): `0x4672d0(RampA, 0.25)` – only during the deceleration phase does the phase timer jump 25% per frame
  towards T_dec (stops faster).
* **Port** (`src/player.c` `move_collide`, `body_push`, `cyl_poly`): the sweep, `0x408600` for world polygons and instance press nodes,
  the body-centre GetHeight, the wall-contact braking and the actor push (normalised to 60 fps, §6.6) follow the code above; test hook
  `WOODY_PUSHLOG=1` logs every contact of the sweep. The head ray is ported as above (`ray_4359b0`, §6.6; `WOODY_PUSHLOG=1` also
  logs its hits as `sweep: head ray`). Port difference: when the port finds no floor at all in a substep it skips the floor clamps
  and the head ray (the original then uses `gy = cur.y`, §6.6 "Fell out of the world"). Test: W1A `--pos 258 -1990 -1677
  --jump 0.5` (under the start saucer, inst 17): he stays on the floor instead of jumping up through the saucer onto it.

### 6.3 Finding the floor: `0x435650` GetHeight → `0x498440` → `0x498520`

```c
void GetHeight(vec3 *p)                                   /* 0x435650(p, cell=-1, 1) */
{
    g_raw = 1;  float best = +inf;
    int cell = FindCell(p);                               /* 0x408180 (kd tree, FORMAT_GEL.md) */
    for (;;) {                                            /* 0x498580 */
        celllist[ncell++] = cell;   Cell *c = world->cells[cell];
        float maxd = p->y - c->ymin;                      /* cel+0x30 */
        for (poly in c->polys)  if (poly->stamp != g_stamp) {         /* cel+8 / +0xc → wereld+0x10[] */
            if (poly->n.y <= 1e-5f) continue;             /* 0x4aa398: upward-facing normals only (every slope!) */
            float dist = dot(poly->n, *p) + poly->d;      /* poly+0xc..0x18 */
            if (dist <= 0) continue;                      /* point must be above the plane */
            if (poly->n.y * maxd + 0.001f < dist) continue;            /* 0x4a94c4: further than the current best/cell floor */
            if (!PointInPolyXZ(poly, p)) continue;        /* all edges: cross product in xz ≥ 0 (0x498666) */
            maxd = dist / poly->n.y;  g_dist = maxd;  g_poly = index;  g_plane = poly->plane;  g_raw = 3;
        }
        if (g_poly != -1) break;
        int link = c->down;                               /* cel+0x18 */
        if (link >= 0)               cell = KdDescend(&c->nodes[link], p);   /* 0x40ab60(cel+0x48 + link*16, p) */
        else if (link != 0x80000000) cell = -1 - link;
        else { g_raw = 1; break; }                        /* no more cell beneath us */
    }
    /* g_dist ([0x4c4bd4]) starts at 0 (0x49852e), not at +inf, and is only written on a hit (0x4986e8).
       afterwards (0x498475): all instances in the visited cells (cell+0x40/+0x44, id & 0xffff → world+0x40[]) and all dynamic
       instances 0x4c3bb4[0x4c4bec]: inst->vt[7](p, id) = 0x432480 (floor ray over the PRESS nodes S+0x58/0x5c; sets g_raw = 4 if
       nearer than g_dist - so never when no world floor was found, see below) */
    if (g_raw == 1) { g_groundY = p->y; g_type = 0; g_plane = (0,1,0,0); }   /* log 'GetHeight return : NotFound !!!!!!' */
    if (g_raw == 3) { g_type = 1; g_groundY = p->y - g_dist; }
    if (g_raw == 4) { g_type = 2; g_groundY = p->y - g_dist; g_hitInst = world->inst[g_instIdx]; g_node = [0x4c4be0];
                      g_local = WorldToNode(inst, node, p - (0,g_dist,0)); /* 0x431700 → [0x53a57c..84] */ }
}
```

**The instance floor test `inst->vt[7]` = `0x432480(p, id)`** (`ret 8`, `this` = instance; the same entry in all 37 vtables
of the instance classes, next to vt[5] `0x432ab0` and vt[8] `0x433140`). A ray from `p` straight down against the polygons of
the press nodes, done in node space:

```c
void Inst_FloorRay(Instance *I, vec3 *p, uint32 id)                       /* 0x432480 */
{
    if (I->cell == -1 || I->stamp == g_stamp || (I->flags8 & 0x40) || !I->model->npress) return;   /* +0x1c, +0x20, +8, S+0x58 */
    if (I->poseFrame != *g_frame) I->vt[2](1);                             /* skeleton up to date, 0x4324dc */
    if (!(I->mask & id & 0xffff0000)) return;                              /* +0xd0 */
    I->stamp = g_stamp;
    vec3 q = *p - (0, 1.0f, 0);                                            /* 0x4a900c: the ray is one unit long */
    for (i = 0; i < model->npress; i++) {                                  /* S+0x5c[i] = node index */
        Mat34 *M = &g_nodeMats[I->matBase + node - 1];                     /* [0x509adc]+0xa0, 0x30 bytes: 3x3 + translation */
        Node *N = &model->nodes[node - 1];                                 /* S+0x68, 0x90 bytes */
        if (sx == sy && sx == sz) {                                        /* +0x4c/+0x50/+0x54: the cull only for a uniform scale */
            float R = sx * N->radius;                                      /* N+0x2c */
            if (R*R < (p.x - M.t.x)² + (p.z - M.t.z)²) continue;           /* 0x4325e8 */
            if (M.t.y - p.y > 0 && M.t.y - p.y > R) continue;              /* node origin more than R above p, 0x43260a */
        }
        Mat34 Mi = Inverse(M, sx, sy, sz);                                 /* 0x440fc0: M^T / s² when uniform, else the cofactor inverse */
        vec3 lp = Mi * p, dir = Mi * q - lp;                               /* 0x43262e..0x43274f */
        for (j = 0; j < N->npolys; j++) {                                  /* N+0x10, records of align4(0x1a + 2 nv) */
            Poly *P = ...;  float f = dot(P->n, lp) + P->d;                /* the loader's plane in node space, poly+8..+0x14 */
            if (f < 0) continue;                                           /* 0x43279f: lp behind the polygon */
            for (each edge a = v[k-1] - lp, b = v[k] - lp, starting with the last vertex)   /* model+0x20, 0x28-byte points */
                if (!(dot(dir, cross(a, b)) > 0)) goto next;               /* 0x432886: strictly inside, winding-sensitive */
            float den = dot(dir, P->n);  if (|den| <= 1e-5f) continue;     /* 0x4aa398 */
            float t = -f / den;                                            /* p - (0, t, 0) is the hit: t is in world units */
            if (!(t < g_dist)) continue;                                   /* 0x432941, [0x4c4bd4] */
            vec3 n = normalize(M3x3 * P->n);                               /* 0x432952..0x4329c5 (M includes the scale) */
            g_n = n;  g_d = -dot(n, M * v_last);  g_dist = t;  g_raw = 4;  /* [0x4c4bc0..cc], [0x4c4bd4], [0x4c4bd0] */
            g_node = i;  g_poly = j;  g_instIdx = id & 0xffff;             /* [0x4c4be0], [0x4c4bd8], [0x4c4bdc] */
        next:;
        }
    }
}
```

* **One-sided.** `dir` points down, so `dot(dir, (v_k-1 − p) × (v_k − p)) > 0` for every edge means the polygon's winding normal
  points down, i.e. its loader normal (`(R − Q) × (R − P)`, the opposite of the winding normal) points up; with `f ≥ 0` the
  point is above it. On the outward-wound press nodes (§6.5) only the top faces count: a point **inside** a node finds neither
  its top (behind it) nor its bottom (wrong side), so the floor there is whatever lies below the node. Any slope with
  `|n.y| > 1e-5` counts, as for world polygons.
* **Never without a world floor.** `g_dist` enters at the world floor's distance, or at 0 when `0x498520` found none (`0x49852e`),
  and `t ≥ 0`: an instance is the floor only if it is strictly nearer than a world floor that was found. A platform over a void
  with no world polygon anywhere below it would not carry the player.
* The ray is transformed with the true inverse node matrix, so the geometry is exact under a non-uniform scale; only the
  reported normal is `M·n` normalised (the same formula as in `0x433140`, §6.5), which is not the true normal of a
  non-uniformly scaled polygon. It becomes the ground normal `[0x4b3108]` (sliding, the race tilt).
* The uniform-scale cull only uses the node radius `N+0x2c` around the node origin; a non-uniformly scaled instance is tested
  without a cull.
* **Port** (`src/player.c` `ins_floor_below`, `loader_plane`, `press_normal`): the same test in world space (the loader plane
  of the world-space vertices has the same side as the node-space one, since no instance of the 28 levels has a negative scale -
  checked for all 5586 press-node instances), `t < g_dist` against the world floor's distance or 0, the node box as the cull,
  and the reported normal `normalize(M·n)`. Before, the port took any face with `|n.y| ≥ 0.5` of either winding, so the underside
  of a hovering press node (the W1A start saucer, inst 17) could become the floor of a point inside or just above it.

The `0x40a0c0`/`0x407790`/`0x4077f0` mentioned in the task are **not used by player collision**: `0x4077f0(&center)` is only
called in `0x44bf10` to hang the player's instance in the correct world cell (for rendering/visibility and the cell lists),
`0x407790` by other classes and the loader. The player's floor comes exclusively from `0x498520` (ray downward from feet+43).

### 6.4 Ground info `0x4628e0`

* Ground normal → Mover: `0x45a110(M, n)` with n = `[0x4b3108..10]` if `n.y ≥ 0`, else (0,1,0) → `M+0xd0` (sliding if `n.y < 0.71`, PERSO_FRAME §2.3:
  slopes steeper than ≈ 45° make the player slide down at 600 units/s).
* **Ground type `P+0x308`** = 0, except on a world polygon (`[0x53a554] == 1`) whose `poly+8` (texture index) does not have bit 15 set:
  `P+0x308 = byte(level->tex[poly+8]->+0x47)` (`level+0x5c`, records of 0x24 B, `+0x20` = texture object) – this is the "ground type" byte from the .tex file.
  Use: **1 = slick/ice** (`0x45a850`, called every frame from `0x45b2c8`: if `P+0x308 == 1`, not inverted controls (`M+0x108 & 0x40`)
  and RampA's target ≥ 0: RampA times `P+0x34`/`P+0x38` = 0.75 s / 1.0 s instead of `P+0x2c`/`P+0x30` = 0.25 / 0.1, and
  `k = clamp(M+0xe0 / P+0x1c, 0, 0.95)·P+0x3c` (|v| of the previous frame / 600; `0x4a9c9c` = 0.95, P+0x3c = 1.0),
  `RampA.dir = k·normalize(RampA.dir) + (1 − k)·normalize(M+0x10)` (`0x45a9ad`; the result is **not** normalised again, so a
  reversal on ice first slows him down); otherwise `RampA.dir = M+0x10`, the facing (`0x45aa48`). So on ice he faces where the
  stick points at once but keeps sliding the old way, 95 % of it per frame at full speed. No shipped level has ground type 1:
  no texture group of any `.tex` has byte 3 = 1 on a floor polygon (type 2 is on W1A/W2A/W2B/W2D/WWS and their K/S copies)),
  **2 = dust/sand/snow** (footstep effect kind 3 instead of 2 `0x464231`, dust cloud on landing `0x464486`).
  There is **no lethal ground type** in the Perso code: death by water/pit comes from scripts (volume → message → `Kill(1)` `0x44516a`) or from `Kill(7)` (`0x4747f0`).

### 6.5 `0x407000` cylinder vs. world + instances (push-out vector)

1. `0x40aa30(c, r, up, down, cel)` collects the cells the cylinder touches in `0x4c4be8[0x4c4be4]`.
2. Per cell, per world polygon (once per call, via stamp `poly+4`): **`0x408600(poly; c, r, up, down, &pos, &neg)`**, fully read
   (`0x408600..0x409a8e`, `ret 0x18`; poly = `this`: `+0` nverts, `+0xc..0x18` plane n, d, `+0x1c` vertex indices). All vertices
   are taken relative to `c`, so the band is `−down ≤ y ≤ up` around the body centre:
   ```c
   int CylPoly(Poly *P, vec3 c, float r, float up, float down, vec3 *pos, vec3 *neg)      /* 0x408600 */
   {
       float dist = dot(P->n, c) + P->d;  if (dist < 0) return 0;                       /* 0x40863e: c behind the polygon */
       float lo = dist - down*P->n.y, hi = dist + up*P->n.y;
       if (lo <= -r && hi <= -r) return 0;  if (lo >= r && hi >= r) return 0;          /* 0x40867a / 0x4086a5 */
       /* Sutherland-Hodgman against the band, jump table 0x409a94 on code(prev) + 4*code(cur) - 1,
          code(y) = (y < up) + (y < 0) + (y < -down): 0 above, 1 [0, up), 2 [-down, 0), 3 below.
          Per edge prev -> cur (starting with vertex n-1 -> 0): emit prev if its code is 1 or 2, then the crossings of
          y = up / 0 / -down in the order the edge meets them, t = (prev.y - Y) / (prev.y - cur.y). Crossings of y = 0
          also go to a second list M (the chord where the polygon cuts the centre plane). Globals 0x4c4c28..0x4c4c80. */
       vec3 L[..], M[..]; int nL, nM;   /* L at esp+0xbc, M at esp+0x5c */
       if (nL == 0) return 0;                                                           /* 0x409653 */
       bool hit = 0;
       for (each edge P0 -> Q of L, closing edge L[nL-1] -> L[0] first; then the open polyline M[0] -> M[1] -> ...) {   /* 0x409698, switch 0x4099dd */
           float r0 = r, dr = 0;
           if (Q.y < -0.01f || P0.y < -0.01f) {                                          /* 0x4096c7: the cone */
               r0 = r*(down + P0.y)/down;  dr = r*(down + Q.y)/down - r0; }
           vec2 d = (Q - P0).xz;  float a = |d|^2 - dr^2;  if (a <= 0.01f) continue;     /* 0x4a94f8 */
           float b = 2*(dot(P0.xz, d) - dr*r0), cq = |P0.xz|^2 - r0^2, D = b*b - 4*a*cq;   /* 0x4a94c0 = 4 */
           if (D < 0) continue;
           float t1 = (-b - sqrt(D))/(2a), t2 = (sqrt(D) - b)/(2a);  if (t1 > 1 || t2 < 0) continue;
           if (!hit) { hit = 1; *pos = *neg = 0; }                                       /* 0x40981f */
           float tm = clamp((t1 + t2)*0.5f, 0, 1), y = P0.y + (Q.y - P0.y)*tm, pen;
           if (y < 0) pen = (y + down)*r/down - |(P0 + (Q - P0)*tm).xz|;                 /* 0x4098a5: cone radius at y */
           else       pen = r - sqrt(a*tm*tm + b*tm + cq + r*r);                         /* 0x409907 */
           for (axis in x, z) { v = pen*P->n.axis;  if (v < 0) neg.axis = min(neg.axis, v); else pos.axis = max(pos.axis, v); }
       }
       if (hit) return 1;
       /* 0x409a1e: no edge contact - is the centre inside the xz outline of L? cr = cur.z*prev.x - prev.z*cur.x per edge */
       if (all cr <= 0 || all cr >= 0) { *pos = *neg = 0; return 1; }                    /* hit with push 0 */
       return 0;
   }
   ```
   So the body is a cylinder of radius r above the centre and a **cone** below it (radius 0 at the bottom of the band): a low
   wall that only reaches into the lower half lets him come closer (at a wall top `h` above the band bottom only
   `r·h/down`); a floor or a step lower than the band is clipped away entirely, which is why there is no walkable-normal test.
   The push is `pen·(n.x, n.z)` - along the polygon normal, not along the contact direction. The loop over M adds the chord at
   the centre height (full radius) to the edges. With a negative `down` (crouched on the ground: half 30.5 < margin 41) the
   codes and the cone formula are used unchanged (the crossing for codes 1/2 is still interpolated at y = 0).
3. Per cell the static instances (`cel+0x40/+0x44`) and then all dynamic ones (`0x4c3bb4[]`, id `| 0xffff0000`):
   `inst->vt[8](c, r, up, down, &pos, &neg, id)` = **`0x433140`**: skipped if the instance has no cell (`+0x1c == −1`), was already tested,
   **flag `+8 & 0x40` set** (non-collidable), the model has no press node (`model+0x58 == 0`), or `inst+0xd0 & id & 0xffff0000 == 0` (collision mask);
   skeleton updated if needed (`vt[2](1)`); per **press node** (list `model+0x5c`, node flag 0x01; `0x433245`), uniform scale branch:
   a bounding test with the node radius (`node+0x2c`·scale) and the band, then the node's vertices are transformed into **world
   space** with the node matrix (`0x433329`), the polygon's plane normal (the loader's plane, `0x4280c2`: n = (R − Q) × (R − P) of the
   longest consecutive vertex triple) is rotated, `d = −n·v0`, and `0x435b90` - the same code as `0x408600` with the vertices
   passed as an array (cdecl) - runs against the world-space centre; result type 4 with `[0x4c4bdc]` = instance, `[0x4c4be0]` = node.
   On every press node of the shipped levels the loader's winding normal points away from the node (checked for all 17 play
   levels), so the front-face rule `dist ≥ 0` means "outside the node".
   **Uniform vs. non-uniform scale** (`0x43320a`: `sx == sy` and `sx == sz`, else `0x4335d7`). Both branches transform the node's
   vertices with the node matrix `M` (3x3 with the instance scale in it, plus translation; `0x433329` / `0x433639`, the same
   sums in another order), gather each polygon's world vertices, build the plane `n' = M3x3·n`, `d = −n'·v0` (`v0` = the
   polygon's first world vertex) and call `0x435b90` with identical arguments and the identical merge into `pos`/`neg`
   (`0x4334c6..0x433583` / `0x433804..0x4338c1`). They differ only in:
   * the cull: the uniform branch first skips a node whose origin is farther than `(radius·s + r)` from the centre in xz, or
     whose origin lies more than `radius·s + up` above / `radius·s + down` below it (`0x433285..0x43330f`, radius = `N+0x2c`);
     the non-uniform branch tests every press node;
   * the length of `n'`: times `1/s` (`[esp+0x4c]`, `0x43322c`) in the uniform branch, divided by `|n'|` (when > 0,
     `0x433762..0x43378f`) in the non-uniform one.
   So under a non-uniform scale the plane normal is `normalize(M·n)`, not the transformed polygon's true normal
   `normalize(M^−T·n)`: the front-face test, the band test and the push direction use a skewed normal while the clipped
   outline uses the true vertices. **It matters**: 276 of the 5586 press-node instances of the 28 levels have a non-uniform
   scale (none a negative one); on axis-aligned faces the two normals agree, but in K2A and S2A 136 polygons are off by more
   than 5° (worst 57.5°, K2A inst 468 / S2A inst 453), in W2D 154 (worst 48.4°, inst 65, scale 2.75 × 1.04 × 1), in W3C 316
   (worst 31.9°, inst 501), in W3D 6 (26.1°), in W2B 10 (9.1°); all other levels ≤ 3.1°.
   **Port** (`body_push`, `press_normal`): the plane is `normalize(M·n_local)` with `n_local` the loader normal of the
   node-space points and `d = −n'·v0`, for both scales (for a uniform scale it equals the true normal); the cull is the node's
   world box.
4. Result: `push.x = max⁺.x + min⁻.x`, `push.z = max⁺.z + min⁻.z`, `push.y = 0` → `[0x4c4bb4..bc]`.

### 6.6 Other

* **Actors** (`0x4627d0`): for every other actor `a` of the previous frame's list `0x4c5258[0x4c5324]` (RegisterActor2: the living enemies)
  `0x433d40(a->vt[34]() /*pos*/, a->vt[32]() − 5.0 /*0x4a9884*/, a->vt[33]() /*height*/, &P+0x1f4, P->vt[32]() /*radius*/, P->vt[33]() /*height*/, &out)`,
  and on a hit `disp.x += out.x`, `disp.z += out.z` (before the sweep). `0x433d40(A, rA, hA, B, rB, hB, out)`:
  ```c
  float d2 = (A.x-B.x)² + (A.z-B.z)², R = rA + rB;
  if (!(d2 < R*R) || A.y + hA <= B.y || B.y + hB <= A.y) return 0;     /* circles in xz, then both height ranges [y, y+h] */
  float k = 1.0f - sqrt(d2) / R;  out = ((B.x-A.x)·k, 0, (B.z-A.z)·k);  return 3;
  ```
  Not a penetration depth: the push is `dist·(1 − dist/R)` per **frame** (R/4 at half the distance), not scaled by dt.
  The original's dt is the raw `QueryPerformanceCounter` delta (`0x42a3f0`, called from `0x401810`; clamped to 0.1 s at `0x40185b`,
  or `1/[0x4b3a8c]` when `[0x5d7b89]` is set; stored in `World+0x38`, initialised to 1/30 at `0x42a474`), with no frame cap, so this
  push is frame-rate dependent there. Port: the recurrence `s ← s·(2 − s/R)` is run `60·dt` times per frame (identical at 60 fps,
  no overshoot at low rates).
* **The segment ray `0x4359b0(a, b, cell)`** (the sweep's head ray §6.2, the crush test below, the stand-up test of ducking
  PERSO_DUCK.md, shots and bombs): it zeroes `[0x53a554]`, `[0x53a560]`, `[0x53a55c]` (not the fraction `[0x53a558]`) and calls
  `0x497ed0` = the world part `0x497fb0` plus vt[5] `0x432ab0` of every instance in the visited cells and of the dynamic list.
  * World (`0x497fb0`): the cells along a→b through their exit faces; a polygon counts when `f(a) ≥ 0`, `n·(b − a) ≤ 0` and
    `f(a) < −n·(b − a)` (b strictly behind it; `0x4981ac..0x4981f8`) and the hit point is inside it (`0x408430`); the nearest
    such polygon of the first cell that has one. `g_raw = 3`, `g_dist` = the fraction.
  * Instances (`0x432ab0`, `ret 0xc`; same skips, skeleton update, mask and uniform-scale radius cull as vt[7]): a and b in
    node space (`0x440fc0`); per press-node polygon: `f(a) < 0` → counted as "behind" and skipped (`0x432d8c`); `f(b) > 0` →
    skipped (`0x432dc3`); every edge `(v_k − a) × (v_k+1 − a) · (b − a) > 0` (`0x432ee2`; the ray must enter the front face);
    `t = −f(a) / n·(b − a)`. It is recorded when `t < g_dist` **or when `[0x53a554]` is still 0** (`0x432f7a..0x432f8e`) -
    and `0x4359b0` has just zeroed it, so any instance polygon on the segment replaces the world hit and, among instances,
    the last one tested wins, not the nearest. After a node: if every one of its polygons had `f(a) < 0` (`0x4330c0`: the
    count equals `N+4`), `a` is **inside** the press node: `[0x53a554] = 3`, `[0x53a558] = 0`, `[0x53a560]` = the instance,
    `g_raw = 2`, `g_dist = 0`; from then on nothing overrides it (`t < 0` is impossible and `[0x53a554] ≠ 0`).
  * Result (`0x4359db..0x435b5f`): `g_raw 3` → kind 1 (world polygon; `[0x53a558]` = t, the normal to `[0x4b3108]`),
    `g_raw 4` → kind 2 (instance polygon; t, the instance `[0x53a560]`, the node `[0x53a58c]`, the local hit point `0x431700`
    → `[0x53a57c..84]`), `g_raw 2` → kind 3 (a inside a press node, t = 0), otherwise 0.
  * **Port** `ray_4359b0` (player.c): `gel_ray_front` for the world, then the press nodes one-sided as above with the world-space
    loader planes; an instance polygon replaces the world hit, and among instances the nearest one is taken (the original's cell
    order is not reproduced); a start inside a press node gives kind 3. Used by the head ray, the crush test and, now, the
    stand-up test of ducking (which used two-sided rays before).
* **Crushing** `0x462a40` (`this` = Perso; one caller, Perso_Update `0x44b87c`, followed by the no-op `0x462c60`). It runs after
  MoveCollide `0x4624f0` (and after `0x4567f0` in state 1) whenever that ran: the dispatch `0x44b7f9` (table `0x44b950`) keeps the
  flag `bl` = 1 for the states 0, 1, 2, 3, 4 and 6 and clears it for 5 (`0x44db50`), 7 (`0x44e1c0`), 8 (`0x4657f0`) and 9
  (`0x454090`), which skip MoveCollide.
  ```c
  void Perso_Crush(Perso *P)                                               /* 0x462a40 */
  {
      if (P->deathKind) return;                                            /* +0x26c: dead - the scale stays as it is */
      float h = BodyHeight(P) / P->inst.sz;                                /* 0x4624c0 / +0x54 = P+0x118: the unsquashed height */
      vec3 a = P->pos + (0, 1, 0), b = P->pos + (0, h - 1, 0);             /* 0x4a900c = 1.0 */
      if (World_FindCell(&a) < 0) return;                                  /* 0x428ce0 (kd descent 0x40ab60); also leaves the scale */
      Ray(&a, &b, cell);                                                   /* 0x4359b0 */
      if ([0x53a558] < 1.0f && P->onGround /* +0x22c, 0x44bcf0 */ && [0x53a554] != 0
          && ([0x53a554] == 2 ? [0x53a560]->animSpeed != 0                 /* inst+0xa0: the instance's animation is running */
                              : P->platform != 0)) {                        /* +0x298: world polygon or inside a node, standing on an instance */
          float free = max((b.y - a.y) * t, 2.0f);                         /* 0x4a9870 */
          P->squash = min(free / (h - 2.0f), 1.0f);                        /* +0x2e8, 0x462bb7 */
          if (P->squash < 0.3f) {                                          /* 0x4aab98 */
              P->vt[38](4);                                                /* Kill(4), 0x462bed */
              if (P->squash < 0.01f) P->squash = 0.01f;                    /* 0x4a94f8 / 0x3c23d70a */
          }
          return;
      }
      if (!P->deathKind) P->squash = 1.0f;                                 /* 0x462c20 */
  }
  ```
  `P+0x2e8` is copied to the instance's z scale `+0x54` by Orient `0x44bd00` every frame, and Reset `0x44ab20` sets the scale
  and `+0x2e8` to 1 (`0x44ab32..0x44ab3b`). The Perso model's z axis is its up axis, so the squash **flattens the model** to
  the free height; and since `0x4624c0 = P+0x118 · inst+0x54`, every body height of the next frame shrinks with it (the
  sweep's band and head ray, shots, the camera eye). Consequences: pressed from above by an **animating** instance
  (a lift or stamper coming down) or carried by a platform into a ceiling, he is squeezed to the gap and dies (kind 4, the
  death animations 0x25/0x26/0x2c, PERSO_DEATH.md) once it is below 30 % of `h − 2` (57 units standing, 18 ducked); as
  soon as the ray is free again he is back to full height at once. A **static** instance over him does nothing unless he
  stands on a platform (the hovering W1A start saucer: no squash, he just cannot stand under it, §6.5). The ray is cast
  from the feet, so kind 3 means the feet are inside a press node.
  **Port** `crush_test` (player.c), called after `move_collide` in the main path (after the race crash test) and in the
  climbing path; `Player.crush` = `P+0x2e8`, multiplied into `player_body_height` and into the z scale of the player's
  instance matrix (`player_apply_transform`); `WOODY_CRUSHLOG=1` logs it. Verified: W1A `--pos 258 -1990 -1677` with
  `WOODY_MSGAT="0.5 4 17 0 1 1000"` (message 4 starts a loop on the start saucer, so its `+0xa0` ≠ 0): kind 2, free 106 of 191
  → scale 0.557, the model drawn squashed; without the message nothing happens.
  **A natural crush spot exists and kills in the original** (verified live, `tools/wverify.py --probe crush --level W1A --pos
  2249 1120 -7577`): W1A objects 186/187 (model 32, at (2249, −7577) and (1453, −7569) on the floor y 1100) are not lifts but
  **stampers** - the script replays anim 0 once every 2.2 s (`3 [186, 0, 1, 200]` + sounds 1633/1635), and the 1900-high column
  comes down from 2601 to ~1100. Standing under one, the original logged a single crush frame, squash 0.291 (free 55.6 of 191),
  and `Kill(4)` in the same frame (`0x462bed`), 2.05 s after the teleport; Perso state 2 follows and the respawn at the level start.
  The port (`WOODY_POSAT="2 2249 1120 -7577" WOODY_CRUSHLOG=1`, 60 fps) kills him there too, but flattens him over three frames
  first (0.952, 0.623, 0.295). The original's jump straight to 0.291 (at ~800 fps, where the column moves a few units per
  frame) presumably comes from the cell registration of the ray `0x4359b0`: it tests
  only the instances registered in the kd cells the segment visits, and the stamper is registered by its animated root
  `inst+0x60`, which only enters the cell of Woody's head ray when the column is almost down (the port tests every instance; see
  the cell-order note below). The static scan `tools/native/crushscan.c` (press nodes of every animated instance posed at 64
  phases against the world floor under / ceiling over them) lists the other candidates: in W1A only 53/54 (model 12, raised once
  in a scripted cutscene) besides the stampers; many more in K2A, K3A, S1A, S3A, W2A, W2B, W3A-W3D (mostly lifts and doors
  whose lower face meets the floor they rest on - whether Woody can stand under them was not checked one by one).
* **Ledge edge** (`0x44b2e0`) and **fall damage** (`0x44b220`): see PERSO_FRAME §2.2.
* **"Fell out of the world"**: does not exist as a separate test. If GetHeight finds no floor (`g_raw == 1`), then `ground height = probe point.y`
  (= feet+43): `0x436f00` then reports `onGround` and sets `pos.y += 43` (!), and in the sweep it counts as "on the ground". In practice, levels
  always have a floor or a script volume that sends `Kill`; the behavior above a real gap has not been verified in the game (§7).
* `0x462990` (teleport/respawn helper): puts the player on the floor beneath `pos + 43`, `onGround = 1`, J reset, cell recomputed (`0x428ce0`).
  The **level start** also ends with it: game ctor `0x445850` → `0x445930` → `0x44a810(0)` → `0x44a6a0` (Reset, `pos = +0x30c`,
  health/lives from the save, `0x44a7ee`: snap to ground), then `0x44a902` and another direct `0x44a6a0` (`0x4458e2`). The `.ins` position
  of the player floats a few units above the floor; without this snap every level would start with a fall (issue #11). Respawn
  (`0x445b41` → `0x44a650`), end of the results screen (`0x45423f`) and end of a cinematic (`0x445af9` → `0x44a650`) also snap.

## 7. Open questions and contradictions

### 7.1 Contradictions with PERSO_FRAME.md (the code was re-read here; PERSO_MOVE is authoritative)

1. **Gravity**: PERSO_FRAME §2.4/§5 says "no gravity found, falling happens via the jump controller `+0x5bc`". Incorrect: jumping AND falling
   both live in the object `P+0x334` (`0x462d70`/`0x462fd0`, §4.2), which PERSO_FRAME calls the "push/impulse object". `0x457a50` only supplies the displacement
   during attacks/special jumps (`+0x5cd`).
2. `0x44bb20`: PERSO_FRAME writes "push from +0x334 if `+0x240 > 0`, else `+0x240 −= dt`" – exactly backwards (`0x44bbca`: `> 0` ⇒ count down, else update J).
   And input is disabled if `+0x474 > 0` **or** `+0x238 > 0` **or** `+0x5b4 ≠ 0` (not "and").
3. `0x44d1b0` is **joystick force feedback** (`[0x5e618c]->vt[2](a, b)`), not knockback; the actual knockback is `0x45a140` (RampC, 500 units/s, 0.2 s).
4. `P+0x238` is a movement lock, not invincibility; invincibility = `+0x270` (respawn 1.0 s) and `+0x280` (after a hit 0.6 s).
5. `P+0x2e0` is wall contact (0/3/2) from `0x437180`, not ground type; the ground type is `P+0x308` (`0x4628e0`). `0x437180` always returns 0 (`P+0x200 = 0`).
6. The "colliders" `+0x298`/`+0x2bc` are **platform links** (instance, node, local point, previous world position), not wall/floor colliders;
   `0x437040` (+0x2bc) is effectively dead code because `0x435b60` is a stub.
7. Input axes: PERSO_FRAME §2.3 says "x = keys 2/3 (left/right), y = keys 0/1" – it's the other way around: actions 0/1 = left/right, 2/3 = forward/backward (§3.4; `angle = π − atan2(value01, value23)`).
8. Column assignment of table `0x4b5f14`: the old text in §1 of this document was wrong; PERSO_FRAME §2.6 is correct (subtype 1→0, 3→1, 2→2, 5→3, 4→4).
9. Task text: `0x44c4ba` is not a function (in the middle of jump table `0x44c498`; the next function is `0x44c4c0` = Kill variant for subtype 4/5);
   `0x40a0c0`/`0x407790`/`0x4077f0` belong to cell assignment, not player collision (§6.3).

### 7.2 Still open

* Which script type (1, 2 or 3) Woody exactly is, is assumed (type 1 → column 0); columns 1/2 differ only in body height (143 instead of 193). To be
  verified with a .ins dump of W1A (type of the Perso instance).
* Turn direction of `0x440d40` (sign of the rotation around y for "right"): not written out; fix it in the reimplementation with the rule
  "action 1 (→) must walk right on screen" and verify with `tools/wtrace.py`.
* The facing-direction slerp (`0.25·|stick|` per frame), the wall push-out (x0.9 per substep), the actor push (§6.6), the ice direction
  blend and the braking boost after a wall contact (§6.4) are **framerate-dependent**; the original's dt is real time (QPC, §6.6) with
  no frame cap, so there is no reference framerate in the code. The port normalises these to 60 fps.
* `0x408600` is decoded (§6.5); the 14 clip cases were checked by the number of points each emits into L and M (all match
  plain Sutherland-Hodgman with the y = 0 crossings added), not instruction by instruction. `0x435b90` was only compared at its
  start (same rejection tests, same layout).
* Behavior without a floor (GetHeight "NotFound", §6.6) is derived from the code but not seen in the game.
* Read and ported since: the floor test vt[7] `0x432480` (§6.3), the non-uniform branch `0x4335d7` of `0x433140` (§6.5), the
  head ray of the sweep (§6.2, which is not a crouch-only sub-ray: it runs every substep), the segment ray `0x4359b0` with its
  instance part vt[5] `0x432ab0` and the crush test `0x462a40` (§6.6). Left over: the instance tests run in the original only for
  the instances registered in the cells the query visits (plus the dynamic list), in that order; the port tests every
  instance (culled by the node boxes) and so cannot reproduce the "last instance wins" order of `0x432ab0`, nor an instance
  that the original misses because its cell was not visited. The Kill(4) branch of the crush test is live-verified under the
  W1A stampers 186/187 (§6.6).
* **Press nodes, not hulls.** All four instance tests (floor vt[7] `0x432480`, cylinder vt[8] `0x433140`, sphere vt[9] `0x433ff0`,
  ray `0x4359b0`) walk only the press node list `model+0x58/0x5c` (node flag 0x01). The hull list `model+0x38/0x3c` (flag 0x04) is only
  read by the draw function `0x42e2b0` (`0x42e7e8`): hull nodes are the visible meshes of characters and props (Woody: 43 hull nodes,
  no press node) and never collide. Consequences: Woody passes through props that have only hull nodes (the traffic cones of K2R/S2R,
  type 70, model 13) and collides with ones that have press nodes but little or no visible mesh there (the hovering saucer at the W1A
  start, the statue plinth in W3D, model 37). Enemies do not block him with their meshes but through the actor push §6.6.
* States 1, 4, 6, 8, 9 (own movement code `0x456210`, `0x4651d0`, `0x463530`, `0x4657f0`, `0x454090`) and altMode `0x459c70`/`0x45a7b0` fall outside this
  document; likewise the attack controller `0x457a50` (PERSO_JUMP.md) and the spring correction `+0x244` (`0x45848c`).
* `P+0x474` = `M+0xec` (0x388+0xec), the Mover's **knockback timer** (0.2 s after a hit, `0x45a140`): so no input during knockback. `P+0x750` (blocks the special attack): writer not searched for.
* Meaning of P+0x28 (1.8), P+0x60 (200), P+0x74 (250), P+0x78 (300), P+0x98..0xb4, P+0xd8/0xdc: not used in the functions read here
  (probably attacks/state 1/4/6).
