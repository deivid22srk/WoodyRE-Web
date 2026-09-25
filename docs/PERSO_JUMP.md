# Perso (player) – jump and attack

Working document, updated incrementally. All addresses are VAs in `Woody.exe` (imagebase 0x400000).
Notation: `p` = Perso (see docs/PERSO_FRAME.md §3), `P` = parameter block `p+0x110`, `M` = Mover `p+0x388`,
`J` = Jumper `p+0x334`, `dt` = `p+0x2f8`. Float constants are read from `game/Woody.exe`
(check: `0x4a9010` = 100.0, `0x4a9014` = 0.5). Woody = column 0 of table `0x4b5f14`.

## 0. Summary / corrections to PERSO_FRAME.md

* `0x457a50` is **only the attack controller** (substate `p+0x5b4`). **Jump and gravity**
  live in the embedded object `J = p+0x334` (`0x462c70..0x463170`), called from
  `Perso_Move 0x44bb20` → `0x462d70`.
* **Jump = action 4** (`0x44bb10` = `0x467400(4)`, default LCtrl), **attack = action 6**
  (default LShift). Action 5 (space) does not appear in this code.
* `0x44d1b0(p, a, b)` is not knockback but **joystick force feedback**
  (`[0x5e618c]->vtbl[2](a, b)`, skipped if state 2 = dead).
* `0x44cce0(p, t, force)` = **LockMove**: `p+0x238 = max(p+0x238, t)` (or `= t` if force). As long as
  `p+0x238 > 0`, `0x44bb20` sets the Mover displacement to 0 (`0x44bc2b`) and `arg = 0` (no input).
  `+0x238` is thus a "movement blocked" timer (duration of the attack animation), not invulnerability.
* `0x44bb20`: `arg = 0` (no loop input, **and also no jump**, because `0x462d70(a, arg)` sets `a = 0`
  if `arg == 0`) as soon as `p+0x474 > 0` **or** `p+0x238 > 0` **or** `p+0x5b4 != 0` (`0x44bb48..0x44bb78`).
* There is no separate gravity constant: the jump and the fall are **parabolas in time**
  `h(t) = H·(1 − (t/T)²)` with `H = P+0x64`, `T = ½·D/V`, `D = k·P+0x68`, `V = P+0x6c` (§1).

## 1. Jumper `J = p+0x334` (jump, fall, landing)

### 1.1 Layout (0x54 B; ctor `0x462c50` empty, init `0x462c70(J, p)`: `J+0 = p`, `J+4 = P`, reset)

| J+ | p+ | type | meaning |
|---|---|---|---|
| 0x00 | 0x334 | ptr | Perso |
| 0x04 | 0x338 | ptr | P (`p+0x110`) |
| 0x08 | 0x33c | vec3 | **displacement this frame**; only `J+0xc` (y) is written; `0x463130` copies it |
| 0x14 | 0x348 | int | **jumper state** 0..7 (getter `0x463160`), see §1.3 |
| 0x18 | 0x34c | f32 | `D` = horizontal reference distance: `0.75·P+0x68` (jump, `0x4aabb4`) or `1.25·P+0x68` (fall, `0x4ab798`) |
| 0x1c | 0x350 | f32 | `t` = time on the parabola (s); apex at `t = 0` |
| 0x20 | 0x354 | f32 | `hPrev` = parabola height of the previous frame |
| 0x24 | 0x358 | f32 | `vDown` = last descent speed (−Δh/dt), starting value for the terminal-velocity phase |
| 0x28 | 0x35c | f32 | **height fallen** (= `fallAccum` from PERSO_FRAME; accumulated in `0x44b914`) |
| 0x2c/0x30/0x34 | | int | P indices 0x19, 0x1b, 0x1a (fixed, `0x462c90`) |
| 0x38 | 0x36c | f32 | copy `P[0x1a]` = **P+0x68 = 650** (1250 for column 3/4) |
| 0x3c | 0x370 | f32 | copy `P[0x1b]` = **P+0x6c = 600** (1250) |
| 0x40 | 0x374 | f32 | copy `P[0x19]` = **P+0x64 = 380** (400) = **jump height H** |
| 0x44 | 0x378 | f32 | copy **P+0x70 = 2000** = terminal speed on a long fall |
| 0x48 | 0x37c | f32 | coyote timer |
| 0x4c | 0x380 | u8 | `armed`: jump button has been released while on the ground ⇒ allowed to jump |
| 0x4d | 0x381 | u8 | `fellOff`: fall started without a jump (walked off an edge / forced) – for animation only |
| 0x4e | 0x382 | u8 | `hardFall`: `J+0x28 > P+0x7c` during a fall (⇒ hard landing, fall damage in `0x44b220`) |
| 0x4f | 0x383 | u8 | `shortHop`: button released early |
| 0x50 | 0x384 | u8 | coyote window active |

`0x462c90` Reset: `J+8 = 0`, state 2, `armed = 1`, `vDown = 0`, `J+0x28 = 0`, flags 0, indices,
`0x462ce0` (copy parameters), `0x462d10`.
`0x462ce0`: `J+0x38 = P[J+0x34]`, `J+0x3c = P[J+0x30]`, `J+0x40 = P[J+0x2c]`, `J+0x44 = P+0x70`.

### 1.2 Constants (Woody, column 0)

| name | value | source |
|---|---|---|
| `H` jump height | **380** | P+0x64 |
| `V` | 600 | P+0x6c (= walk speed) |
| `D_jump` | 0.75·650 = **487.5** | `0x462d10`, `0x4aabb4` = 0.75 |
| `T_jump = ½·D_jump/V` | **0.40625 s** (time to apex) | `0x462d2a`: `t0 = (D/V)·(−0.5)` (`0x4a94bc`) |
| initial upward speed `2H/T_jump` | **≈ 1870.8 units/s** | derived |
| gravity rise phase `2H/T_jump²` | **≈ 4605 units/s²** | derived |
| `D_fall` | 1.25·650 = **812.5** | `0x462d40`, `0x4ab798` = 1.25 |
| `T_fall = ½·D_fall/V` | **0.67708 s** | |
| gravity fall phase `2H/T_fall²` | **≈ 1657.8 units/s²** | derived |
| descent speed at `u = 1` (380 fallen) | 2H/T_fall ≈ **1122.5 units/s** | derived |
| terminal speed | **2000 units/s**, reached linearly between `u = 1` and `u = 1.5` (≈ 0.34 s) | P+0x70, `0x4630a8` |
| short-hop jump point | `t := −0.2 s` (`0x4aa430`) if button released and `t < −0.2` | `0x462e9d` |
| attack window opens | `t ≥ −0.15 s` (`0x4ab7a0`) ⇒ `p+0x5c8 = p+0x6f8 = max(·, 0.5)` | `0x463002` |
| "fall complete" threshold | `u ≥ 0.45` (`0x4ab79c`) | `0x4630f4` |
| coyote time | **0.15 s** (`0x4aa1c8`) | `0x462dcf` |
| re-arming the jump | button released **and** height above ground `p+0x228 ≤ P+0x84 (100)` | `0x462da0` |
| fall damage / hard landing | height fallen `> P+0x7c (1500)` | `0x462f52`, `0x44b220` |

Without releasing: rising for 0.406 s to a height of 380, then falling for 0.677 s down to the starting height (total ≈ 1.08 s).
Minimum jump (button released immediately): after frame 1 `t` jumps to −0.2 s; remaining rise =
`H·(0.2/T_jump)² = 380·0.2424 ≈ 92` units + whatever had already been gained.

### 1.3 States `J+0x14` (table `0x462fa4`)

| # | handler | meaning | transitions |
|---|---|---|---|
| 0 | `0x462e8e` | jump just started | → 1 (same frame, falls through into handler 1) |
| 1 | `0x462e95` | rising | button released and `t < −0.2` ⇒ short hop (`shortHop = 1`, `t = −0.2`, `hPrev = H(1−(0.2/T)²)`); tick: `t ≥ −0.15` and no shortHop ⇒ **7** + attack window; `t ≥ 0` with shortHop ⇒ 6 (on ground) or 4 |
| 2 | `0x462dff` | **on the ground / idle** | `jumpHeld && onGround && armed` ⇒ **0** (`0x462d10`, tick, `armed = 0`); otherwise if not onGround ⇒ **3** (`coyote = armed`, `J+0x48 = 0`, `0x462d40(1)`: `fellOff = 1`, tick, `armed = 0`) |
| 3 | `0x462f14` | falling, first part (`u < 0.45`) | tick returns 1 ⇒ **4**; `coyote && jumpHeld` ⇒ anim reset `p+0x494->vtbl[4]()`, `coyote = 0`, **jump (→ 0)** |
| 4 | `0x462f52` | falling | `J+0x28 > P+0x7c (1500)` ⇒ `hardFall = 1`, **5**; tick; `onGround && tick==1` ⇒ **6** |
| 5 | `0x462f6a` | long fall | tick; `onGround && tick==1` ⇒ **6** |
| 6 | `0x462f95` | landed (1 frame; `0x44b220` and the animation code read this) | ⇒ **2** |
| 7 | `0x462eee` | around the apex (after `t ≥ −0.15`) | tick (if `t ≥ 0` ⇒ 3 with `0x462d40(0)`); `onGround && tick==1` ⇒ 6 |

Note: states 1 and 3 do not test `onGround`; landing during 3 (`u < 0.45`, < 0.30 s falling) stays in 3
until `u ≥ 0.45`, then 4 → 6 → 2. In those frames the tick simply produces a negative dy, and the collision
in `0x4624f0` puts the player back on the ground.

### 1.4 Pseudo-C

```c
/* 0x462d70 – per frame from Perso_Move (0x44bbcc), only when p+0x240 <= 0 */
void Jumper_Update(Jumper *J, bool jumpHeld /* 0x467400(4) */, bool inputAllowed /* arg from Perso_Move */)
{
    Perso *p = J->p;  const float *P = J->P;
    if (J->state == 2) { J->disp = (0,0,0); J->fallen = 0; J->hardFall = 0; Jumper_LoadParams(J); /*0x462ce0*/ }
    if (!inputAllowed) jumpHeld = false;
    if (p->heightAboveGround /*+0x228*/ <= P[0x84/4] /*100*/ && !jumpHeld && inputAllowed) J->armed = 1;
    if (J->coyote) { J->coyoteT += p->dt; if (J->coyoteT > 0.15f) J->coyote = 0; }
    switch (J->state) {
    case 2:
        if (jumpHeld && p->onGround && J->armed) {
    jump:   J->state = 0; Jumper_StartJump(J); Jumper_Tick(J, p->dt); J->armed = 0; return;
        }
        if (!p->onGround) {
            J->coyote = J->armed; if (J->armed) J->coyoteT = 0;
            J->state = 3; Jumper_StartFall(J, 1); Jumper_Tick(J, p->dt); J->armed = 0;
        }
        return;
    case 0: J->state = 1;  /* fallthrough */
    case 1:
        if (!jumpHeld && J->t < -0.2f) {                     /* variable jump height */
            J->shortHop = 1;
            float u = -0.2f / (0.5f * J->D / J->V);
            J->t = -0.2f;  J->hPrev = J->H - J->H*u*u;       /* no position jump: only the clock skips */
        }
        Jumper_Tick(J, p->dt); return;
    case 7: { bool r = Jumper_Tick(J, p->dt); if (p->onGround && r) J->state = 6; return; }
    case 3:
        if (Jumper_Tick(J, p->dt)) J->state = 4;
        if (J->coyote && jumpHeld) { p->anim->vtbl[4](); J->coyote = 0; goto jump; }
        return;
    case 4:
        if (J->fallen > P[0x7c/4] /*1500*/) { J->hardFall = 1; J->state = 5; }
        /* fallthrough */
    case 5: { bool r = Jumper_Tick(J, p->dt); if (p->onGround && r) J->state = 6; return; }
    case 6: J->state = 2; return;
    }
}

void Jumper_StartJump(Jumper *J)  /* 0x462d10 */
{ J->D = J->P68 * 0.75f; J->t = (J->D / J->V) * -0.5f; J->hPrev = 0; J->vDown = 0; J->shortHop = J->fellOff = 0; }

void Jumper_StartFall(Jumper *J, bool fellOff)  /* 0x462d40 */
{ J->D = J->P68 * 1.25f; J->fellOff = fellOff; J->hPrev = J->H; J->t = 0; }

bool Jumper_Tick(Jumper *J, float dt)  /* 0x462fd0 */
{
    J->t += dt;
    float u = J->t / (0.5f * J->D / J->V);
    float h = J->H - J->H*u*u;
    if (J->t >= -0.15f && J->state == 1 && !J->shortHop) {
        Perso_OpenAttackWindow(J->p, 0.5f, 0);   /* 0x457560: p+0x5c8 = max(p+0x5c8, 0.5) */
        Perso_OpenAirMoveWindow(J->p, 0.5f, 0);  /* 0x465e00: p+0x6f8 = max(p+0x6f8, 0.5) */
        J->state = 7;
    }
    if (J->t >= 0 && (J->state == 7 || (J->state == 1 && J->shortHop))) {
        J->state = J->shortHop ? (J->p->onGround ? 6 : 4) : 3;
        Jumper_StartFall(J, 0);  h = J->H;                 /* u keeps the old value this frame */
    }
    if (u > 1.0f) {                                        /* deeper than the parabola's starting point */
        float k = min(u - 1.0f, 0.5f);
        J->disp.y = -(((J->vTerminal /*2000*/ - J->vDown) * 2*k + J->vDown) * dt);
    } else {
        J->disp.y = h - J->hPrev;
        J->vDown  = -J->disp.y / dt;
    }
    J->hPrev = h;
    return !(u < 0.45f && !J->shortHop);
}

void Jumper_ForceFall(Jumper *J, bool force)  /* 0x463170 */
{
    if ((J->state == 3 || J->state == 4 || J->state == 5) && !force) return;
    J->coyote = 0; Jumper_StartFall(J, 1); J->vDown = 0; J->state = 4;
}
```

`Perso_Move 0x44bb20`: `disp = Mover.d + J.disp` unless `p+0x5cd` (attack supplies `p+0x5bc`) or `p+0x6ac`;
then always `disp.y += dt·p+0x244`. If `p+0x240 > 0`: the jumper isn't called, `p+0x240 -= dt`
(jumper displacement = 0 ⇒ no gravity). **Air control**: the Mover keeps running unchanged in
the air (same RampA, 600 units/s, same acceleration); there is no separate air factor in the jumper.
Callers of `0x463170` (force a fall): `0x44c110` (TakeHit), `0x44ca9f`, `0x456c00`, `0x4656fd`,
`0x465a4f` and the attack controller (§2). Reset `0x462c90`: `0x44ab9b` (Perso Reset), `0x44c389`,
`0x44c6a7`, `0x4629f5` (snap-to-ground `0x462990`), `0x4649d2`, `0x464b3c`, `0x4650ce`, `0x465a46`.

### 1.5 Second air action (not Woody): `0x465e50` + `0x465fe0`

`0x465e50` (every frame before `0x457a50`): on the ground `p+0x6fd = 0`. If window `p+0x6f8 > 0`
(`0x465e30`; opened by the jumper around the apex and by the attack knockback) **and** action 4 *just pressed*
(`0x467420(4)`) and not yet used (`p+0x6fd == 0`, `p+0x6e4 == 0`) and `p+0x228 > P+0x84 (100)`:
`p+0x6e4 = p+0x6fd = 1`, `p+0x6f4 = 0`, and per subtype (`(p+0x104 & 0x3e0)`):
* subtype 3 (`0x60`): **air dash** along `M.dir`: duration `p+0x6e8 = 0.15 s`, speed `p+0x6f0 = 3000`, sound 0x3a.
* subtype 2 (`0x40`): **double jump** direction (0,1,0): duration `0.25 s`, speed `1200`, sound 0x3a.
* otherwise (Woody = subtype 1): `p+0x6e4 = 0` – **Woody has no double jump / hover**.

`0x465fe0` (start of `Perso_Move`): `p+0x6f8 -= dt`; if `p+0x6e4`: `p+0x6ec += dt` (up to `p+0x6e8`, then
end), for subtype 2 the speed is multiplied by 0.99 per 1/60 s (`0x4a9990`, `0x4ab7d8`);
`p+0x204 = normalize(p+0x6d8) · step · p+0x6f0`, and the function returns 1 ⇒ `Perso_Move` skips both the Mover and
the jumper.

## 2. Attack controller `0x457a50` + trigger `0x457330`

### 2.1 Fields

| p+ | type | meaning |
|---|---|---|
| 0x5b4 | int | **attack substate** 0..11 (0 = none) |
| 0x5b8 | f32 | timer of the substate (s, counts down) |
| 0x5bc..0x5c4 | vec3 | displacement this frame (or direction) from the attack; used as `p+0x5cd` |
| 0x5c8 | f32 | **air-attack window** (s): air attack only when > 0 (`0x457590`); `0x457560(v, force)` sets `max` |
| 0x5cc | u8 | mesh has an attack vector (`0x42f6b0(p, 0, &p+0x59c, 0) == 1`, set in `0x44b4a0`) |
| 0x59c / 0x5a8 | vec3 ×2 | start/end of the **beak vector** (world space) from the mesh; without a vector both = instance position `p+0xc` |
| 0x5cd | u8 | "use `p+0x5bc` as displacement" (cleared to 0 every frame at the start of `0x457a50`) |
| 0x5d0..0x5d8 | vec3 | target point of the peck dash |
| 0x5dc | u8 | peck dash has a target (auto-aim) |
| 0x5e0 | f32 | **charge run** charge 0..1.5 (HUD shows ·2/3); `−= dt` per frame when > 0 (`0x457a78`) |
| 0x5e4..0x5ec | vec3 | position at the start of the dash (for the sweep test) |
| 0x5f0 | ptr | current target (enemy); 0 in state 3/11 and after a wall hit |
| 0x5f4/0x5f8/0x5fc | f32/f32/u8 | `0x44ba70`: as long as `+0x5f8 > 0`: every 0.2 s `+0x5fc = 1` (pulse, dust effect); set on 9→10: `+0x5f4 = 0.2`, `+0x5f8 = charge` |
| 0x5fd | u8 | first frame of state 9 (skip auto-aim) |
| 0x600 | f32 | rumble interval in state 10 (0.2 s) |
| 0x604 | obj | **target finder** (`0x4632e0/0x463420`): 16 × {inst, distance}, `+0x80` = count |
| 0x510..0x518 / 0x51c | vec3 / ptr | normal (xz, normalized) and instance of the hit "peckable" surface |
| 0x524 | f32 | block timer for "stuck peck" (must be < 0; counted down by `0x464ef0`) |

### 2.2 Trigger `0x457330` (every frame via `0x44ba70`, after `0x457a50`)

```c
void Perso_AttackTrigger(Perso *p)   /* 0x457330 */
{
    if (Jumper_IsFalling(p) /*0x44c910: J.state 3|4*/ || p->vtbl[35](p) /*0x44c940: J.state 0|1|7*/)
        p->charge /*+0x5e0*/ = 0;                                   /* no charge while airborne */
    if (p->airWin /*+0x5c8*/ > 0) p->airWin -= dt;
    if (p->+0x50c || p->+0x694 || p->moveLock /*+0x238*/ > 0 || p->state /*+0x21c*/ != 0) return;
    if (p->atk != 0) {                                              /* only a chained attack from knockback */
        if (p->atk == 5 && JustPressed(6) && p->airWin > 0) p->atk = 1;
        return;
    }
    if (p->onGround) {
        if (JustReleased(6)) {                                      /* 0x467440(6): CHARGE RUN on RELEASE */
            Anim(p, 0x10);
            p->atkT = AnimLen(0x10,0) + AnimLen(0x11,0);
            p->atk = 9;
            if (p->charge <= 0.1f) { LockMove(p, p->atkT, 0); p->charge = 0; }   /* a tap = short charge run */
            p->first9 /*+0x5fd*/ = 1;  p->rumbleT /*+0x600*/ = 0.2f;
        } else if (Held(6)) {                                       /* charging up */
            p->charge += 4.0f*dt;                                   /* net +3·dt (−dt in 0x457a50) */
            if (p->charge > 1.5f) p->charge = 1.5f;                 /* full after ≈ 0.5 s */
        }
    } else if (JustPressed(6) && p->airWin > 0) {                   /* AIR: PECK DASH */
        p->atk = 1;
        Sound(0x37 + rand(0,3) /*0x43ff20(0,3): 0x37/0x38/0x39*/);
    }
}
```

Before the trigger, `0x44ba70` (state 0) also tests `0x463430`: action 6 *just pressed*, on the ground,
`atk == 0`, and an object from `0x5e4880[]` with `+0x131 != 0`, `+0x133 == 0` within
`(P+4 + 200)` (3D distance², `0x4aa164`) ⇒ `p+0x590 = obj`, `obj+0x132 = 1`, `p+0x594 = 1`,
`p+0x58c = 0`, **SetState(6)** (pick up/ride object instead of attacking).

### 2.3 State table `p+0x5b4` (jump table `0x458bb8`)

| # | address | name | behavior | end / transition | anim |
|---|---|---|---|---|---|
| 0 | – | none | `0x457a50` does nothing (only `+0x5cd = 0`, charge −dt) | trigger §2.2 | – |
| 1 | `0x457eac` | peck-dash start (1 frame) | find target (500), determine direction, set Mover direction, `fallen = 0`, `hardFall = 0`, windows closed (`0x457560(0,1)`, `0x465e00(0,1)`), trail effect `0x47a450` | → **2** | 0xb |
| 2 | `0x458281` | **peck dash** | `disp = normalize(p+0x5bc)·1500·dt` (`0x4ab2c0`), no gravity; wall test 50 ahead, otherwise 100 horizontal (`0x4575b0`); target test (sweep) | wall ⇒ **6/7/8**; target hit ⇒ **3**; no other exit (see §5) | (0xb keeps running) |
| 3 | `0x458587` | hit (1 frame) | `+0x5f0 = 0`, `disp = 0` | → **4**, `T = AnimLen(0xc,0)` | 0xc |
| 4 | `0x4585cf` | hit standstill | `+0x5cd = 1`, `disp` stays (0,0,0) ⇒ hangs still in the air | `T ≤ 0` ⇒ knockback direction, `T = 0.75`, → **5** | 0xc |
| 5 | `0x45878c` | **knockback** | `disp = normalize(r)·(1000·T)·dt` (`0x4aa188`), `r = (−dir.x, 0.8, ≈0)` normalized; when `T < 0.5`: window `p+0x5c8`/`p+0x6f8` = 0.5 if closed ⇒ **chained attack** possible (action 6 ⇒ 1) | `T ≤ 0` ⇒ `Jumper_ForceFall(1)`, → **0** | – |
| 6 | `0x45841d` | low wall bounce (≤ 100 above ground) | jumper reset every frame (no gravity); if `T < 0.4` (`0x4aa394`): find ground `0x435650(pos+(0,1,0), −1, 1)` and `p+0x244 −= (pos.y − [0x53a568])·2.5` (spring toward the ground) | `T ≤ 0` ⇒ `0x462990` (snap to ground, `onGround = 1`, jumper reset), → **0** | 0xd |
| 7 | `0x4584e1` | high wall bounce (> 100) | jumper reset (hangs still) | `T ≤ 0` ⇒ → **0**, `Jumper_ForceFall(0)` | 0xe |
| 8 | `0x458524` | beak stuck in "peckable" surface | jumper reset | `T ≤ 0` ⇒ **SetState(4)**, anim 0x15, `p+0x50c = 2`, `p+0x520 = 0.8`, → **0** | 0xf → 0x15 |
| 9 | `0x457abe` | **charge-run windup** | `disp.xz = M.dir·700·dt` (`0x4ab2c4`), `disp.y = 0`; auto-aim `0x4579a0` (not on the 1st frame); beak-hit test | steep edge (`p+0x234`): button 4 pressed ⇒ LockMove(0,1), anim reset, → 0; otherwise brake → **11**. `T ≤ 0` ⇒ → **10**, `+0x5f4 = 0.2`, `+0x5f8 = charge` | 0x10 (→ sub-anim 0x11) |
| 10 | `0x457c02` | **charge run** | auto-aim; as long as `charge > 0` and no steep edge and action 4 not pressed: `disp.xz = M.dir·700·dt`; every 0.2 s rumble `0x44d1b0(P+0xac, P+0xa8)` = (0.5, 0.15); beak-hit test | otherwise: action 4 ⇒ LockMove(0,1), anim reset, → **0** (jump follows); otherwise brake → **11** | 0x11 |
| 11 | `0x457e78` | brake | `+0x5f0 = 0`; no `+0x5cd` (Mover stands still due to LockMove) | `T ≤ 0` ⇒ → **0** | 0x12 |

"brake" (`0x457b0a`, `0x457e16`, external `0x458e40`): `charge = 0`, `T = AnimLen(0x12,0) + AnimLen(0x12,1)`,
`LockMove(T, 0)`, anim 0x12, → 11. `0x458e40` (caller `0x44542f`, game code) breaks off a charge run (9/10) from
outside.

The charge run therefore lasts: `AnimLen(0x10,0)+AnimLen(0x11,0)` (windup) + remaining charge (max 1.5 s, −dt/s).
A tap (charge ≤ 0.1) only gives the windup and then brakes immediately. Speed **700 units/s** (walking = 600).

### 2.4 Pseudo-C of the states

```c
void Perso_AttackUpdate(Perso *p)   /* 0x457a50 */
{
    p->useAtkDisp /*+0x5cd*/ = 0;
    if (p->charge > 0) p->charge -= dt;
    switch (p->atk) {
    case 1: {                                                        /* 0x457eac */
        TargetFinder_Scan(&p->finder, &p->pos, 500.0f);              /* 0x4632e0 */
        Inst *t = TargetFinder_Nearest(&p->finder);                  /* 0x463420 */
        p->hasTarget /*+0x5dc*/ = 0;
        if (t) {
            p->aim /*+0x5d0*/ = t->pos /*inst+0xc*/;  bool enemy = false;
            if (t->vtbl[4]() && (*t->vtbl[4]() & 0x1f) == 2) {       /* category 2 = enemy */
                enemy = true;  p->target /*+0x5f0*/ = t;
                p->aim.y += t->vtbl[33]() /*height*/ * 0.8f;         /* 0x4a987c */
            }
            if (p->pos.y - p->aim.y > 50.0f) {                       /* 0x4a9030: only targets BELOW the player */
                p->atkDisp = p->aim - p->pos;
                if (enemy) t->vtbl[37](&p->atkDisp);                 /* +0x94: warn the target */
                p->hasTarget = 1;
            }
        }
        if (!p->hasTarget) { p->atkDisp = normalize(M.dir.x, 0, M.dir.z); p->atkDisp.y = -2.0f; }  /* diagonally down */
        p->dashStart /*+0x5e4*/ = p->pos;
        p->atkDisp = normalize(p->atkDisp);
        M.rampA.dir = (atkDisp.x, 0, atkDisp.z); normalize (0x424720); if (len < 0.01) dir.x = 1;
        M.velDir /*+0x1c*/ = M.dir /*+0x10*/ = M.rampA.dir;
        p->J.fallen = 0;  p->J.hardFall = 0;                         /* +0x35c, +0x382: dash clears fall damage */
        OpenAttackWindow(p, 0, 1);  OpenAirMoveWindow(p, 0, 1);      /* close windows */
        Anim(p, 0xb);
        vec3 d = normalize(M.dir.x, 0, M.dir.z);  float L = sqrt(d.x*d.x + d.z*d.z + 4.0f /*0x4a94c0*/);
        d = (d.x/L, -2.0f/L /*0x4a9504*/, d.z/L);                    /* 0x45820d..0x458258 */
        SpawnDashTrail(p, &d);                                       /* 0x47a450: effect lives as long as atk == 2 (0x479d88) */
        p->atk = 2;  return;
    }
    case 2: {                                                        /* 0x458281 */
        vec3 n = normalize(p->atkDisp);
        p->atkDisp = n * (dt * 1500.0f);  p->useAtkDisp = 1;
        if (!Perso_AttackProbe(p, n*50.0f)) {                        /* 0x4575b0 */
            vec3 h = normalize(n.x, 0, n.z) * 100.0f;
            Perso_AttackProbe(p, h);
        }
        break;                                                       /* → target-hit loop */
    }
    case 3: p->atk = 4; p->target = 0; Anim(p,0xc); p->atkT = AnimLen(0xc,0); p->atkDisp = 0; p->useAtkDisp = 1; return;
    case 4:
        p->useAtkDisp = 1;
        if ((p->atkT -= dt) > 0) return;
        base = p->hasTarget ? p->aim : p->pos;
        p->atkDisp.x = base.x - M.dir.x - base.x;                    /* = −dir.x */
        p->atkDisp.y = 0;
        p->atkDisp.z = base.y - M.dir.y - base.y;                    /* 0x458686: uses .y instead of .z (bug in the original) ⇒ ≈ 0 */
        normalize; p->atkDisp.y = 0.8f; normalize;
        p->atkT = 0.75f;  p->atk = 5;  return;
    case 5:
        p->atkT -= dt;
        p->atkDisp = normalize(p->atkDisp) * (dt * p->atkT * 1000.0f);  p->useAtkDisp = 1;
        if (p->atkT < 0.5f && !(p->airWin > 0)) { OpenAttackWindow(p, 0.5f, 0); OpenAirMoveWindow(p, 0.5f, 0); }
        if (p->atkT <= 0) { Jumper_ForceFall(&p->J, 1); p->atk = 0; }
        return;
    case 6:
        Jumper_Reset(&p->J);
        if ((p->atkT -= dt) < 0.4f) { GroundProbe(pos + (0,1,0), -1, 1); p->vyCorr /*+0x244*/ -= (p->pos.y - g_groundY /*0x53a568*/) * 2.5f; }
        if (p->atkT <= 0) { Perso_SnapToGround(p) /*0x462990*/; p->atk = 0; }
        return;
    case 7: Jumper_Reset(&p->J); if ((p->atkT -= dt) <= 0) { p->atk = 0; Jumper_ForceFall(&p->J, 0); } return;
    case 8: Jumper_Reset(&p->J);
        if ((p->atkT -= dt) <= 0) { Perso_SetState(p, 4); Anim(p, 0x15); p->+0x50c = 2; p->+0x520 = 0.8f; p->atk = 0; }
        return;
    case 9:
        if (p->steepEdge /*+0x234*/) { p->+0x5fc = 0; p->+0x5f8 = 0;
            if (Held(4)) { LockMove(p, 0, 1); p->anim->vtbl[4](); p->atk = 0; } else Brake(p);   /* → 11 */
            return; }
        if (p->first9) p->first9 = 0; else Perso_AutoAim(p);         /* 0x4579a0 */
        if ((p->atkT -= dt) <= 0) { p->atk = 10; p->+0x5f4 = 0.2f; p->+0x5f8 = p->charge; }
        run: p->useAtkDisp = 1; p->atkDisp = (M.dir.x*dt*700, 0, M.dir.z*dt*700);
        break;                                                       /* → target-hit loop */
    case 10:
        Perso_AutoAim(p);
        if (p->charge > 0 && !p->steepEdge && !Held(4)) {
            if ((p->rumbleT -= dt) <= 0) { p->rumbleT += 0.2f; Rumble(p, P[0xac/4] /*0.5*/, P[0xa8/4] /*0.15*/); }
            Anim(p, 0x11);  goto run;
        }
        p->+0x5fc = 0; p->+0x5f8 = 0;
        if (Held(4)) { LockMove(p, 0, 1); p->anim->vtbl[4](); p->atk = 0; } else Brake(p);
        break;                                                       /* → target-hit loop (this frame too) */
    case 11: p->target = 0; if ((p->atkT -= dt) <= 0) p->atk = 0; return;
    default: return;
    }
    Perso_AttackHitLoop(p);                                          /* 0x457ceb, §3 */
}
```

`Perso_AutoAim 0x4579a0`: target finder (500); nearest with category 2 ⇒
`Mover_SetDir(M, (t.x − pos.x, 0, t.z − pos.z))` (`0x459ff0`: resets the three ramps and sets
`RampA.dir` normalized) and `p+0x5f0 = t` – **the charge run automatically steers toward the nearest enemy**.

`Perso_AttackProbe 0x4575b0(p, v)`: ray from `a = p+0xc + (0,5,0)` (`0x4a9884`) to `a + v`
(`0x4359b0(&a, &b, −1)`); `[0x53a554]` = hit kind (0 nothing, 1 world, 2 instance), `[0x53a558]` = fraction.
No hit ⇒ 0 (log "On Ground during air attack" if onGround — no-op). Hit: spark effect
`0x479c80(1, &hitpoint, 0)` (OBJECTS.md §1.6); if hit kind 2 and the polygon flag `(poly[0] & 0xff00) == 0x400` of instance
`[0x53a560]` (poly index `[0x53a58c]`, 0x90 B per poly): `0x464e00(p, inst)` – requires `p+0x524 < 0`,
`p+0x26c == 0`, and wall normal `|n.y| ≤ 0.05` (`0x4ab7d0`, f64; normal from `0x4b3108`); then
`p+0x510 = normalize(n.x, 0, n.z)`, `p+0x51c = inst` ⇒ **state 8**: `T = AnimLen(0xf,0)`, anim 0xf,
LockMove(T), `RampA.dir = M.velDir = M.dir = −n` (look at the wall). Otherwise ground test
(`0x435650(pos+(0,1,0), −1, 1)`): `pos.y − [0x53a568] > 100` ⇒ **state 7** (anim 0xe) otherwise
**state 6** (anim 0xd), `T = AnimLen(·,0)`, LockMove(T, 0). Always: `p+0x5f0 = 0`,
rumble `0x44d1b0(P+0x9c, P+0x98)` = (0.5, 0.5); returns 1.

### 2.5 Related function `0x464ef0` – getting stuck from the ground/charge run

Every frame before `0x457a50`: `p+0x524 -= dt` as long as ≥ 0 (nothing else happens that frame after that). If `p+0x524 < 0`
and action 6 *just pressed* and `atk ∈ {0, 10}` and `p+0x50c == 0`: ray (`0x4359b0`, cell `p+0x200`) from
the player along `normalize(M.dir)` over `P+4 + 100` (= 169); on an instance polygon with flag
`(poly & 0xff00) == 0x400` and `0x464e00` succeeding (wall normal, see §2.4): **SetState(4)**, `p+0x50c = 1`,
`p+0x520 = 0.8`, `Mover_SetDir(M, NULL)`, jumper reset, attack window closed (`0x457560(0,1)`), and
`RampA.dir = M.velDir = M.dir = −normal` (normalized, xz). State 4 (`0x4651d0`) is the
"beak stuck in wood" behavior; from the air it is reached via substate 8 (`p+0x50c = 2`).

`0x465b10` (232 instructions, after the trigger) does nothing if `atk != 0` or dead (`0x465b3b`).

## 3. Hit test (target-hit loop `0x457ceb..0x458b96`)

Runs in substates **2, 9 and 10** (9/10 also in the frame where it stops). If the mesh has no
attack vector (`p+0x5cc == 0`): `p+0x59c = p+0x5a8 = p+0xc` (and a no-op log "Please get the latest
version of the Woody mesh with the Vector used for Attacks").

For each actor `t` in `0x4c5258[0x4c5324]` (actor list from the previous frame, `RegisterActor2`), `t != p`;
`tp = *t->vtbl[34]()` (position):

* **substate 2 (peck dash)**: `0x433920(&p+0x5e4 /*dashstart*/, &p->pos, 100.0, &tp, t->vtbl[32]() /*radius*/,
  t->vtbl[33]() /*height*/)` – **swept circle in the xz plane**: segment dashstart→pos against a circle with
  radius `100 + r_t` (quadratic equation, discriminant ≥ 0, `0 ≤ s ≤ 1`), then a y-overlap test against
  the height (and `0x433bc0` for edge cases); ≠ 0 = hit, `[0x53a558]` = fraction.
  Hit: `dir = (0,0,0)`, `hitpoint = p+0x59c`.
* **substate 9/10 (charge run)**: beak segment `a = p+0x59c`, `b = a + normalize(p+0x5a8 − a)·50`
  (b is written back into `p+0x5a8`); cylinder around `c = tp + (0, height·0.5, 0)`:
  `0x433de0(&a, &b, &c, radius, height)` – segment against a **vertical cylinder** (xz quadratic,
  y within `c.y ± (height − 0.1)`); returns 0.5 on hit, −1.0 on miss. Hit: `dir = normalize(tp.xz − pos.xz)`,
  `hitpoint = lerp(a, b, 0.5)`; in state 10 also `Mover_SetDir(M, dir)`.

On a hit:
1. substate 2 ⇒ **`p+0x5b4 = 3`**, `isPeck = 1` (otherwise 0).
2. rumble `0x44d1b0(p, P+0xa4, P+0xa0)` = (0.5, 0.3).
3. `hit = t->vtbl[39](p, P+0x90 /*damage 1.0*/, &dir, &hitpoint, isPeck)` (slot +0x9c; the Perso's own
   implementation is `0x44ca00`) – **no script message is sent from the controller**; the
   target handles the hit itself.
4. if `hit` ⇒ `t->vtbl[38](3)` (slot +0x98 = "die/lose, kind 3").

The target finder `0x4632e0(F, &pos, r)` walks all world instances (`[0x509adc]+0x60/+0x64`) with
`vtbl[4]()` ≠ NULL and typeword bit **0x400** set, 3D distance from `inst+0xc` to `pos` < r; max 16,
bubble-sorted ascending by distance; `0x463420` = first (closest) or NULL.

## 4. Animations

Animation controller `p+0x494` (class vtable `0x4ab7b4`, base `0x4aa3bc`, ctor `0x463df0`):
`vtbl[0](n)` = `0x463e10` → table record `0x4b6180 + n·0x1c`: `{int sub[4]; int prio; float speed; u8 restart}`;
`vtbl[2](n)` = `0x436b70` **Request(n)** (max 16 per frame); `vtbl[3](dt)` = `0x436a50` Tick: picks the request
with the highest `prio`; switches if there is no current one, or `prio_new ≥ prio_current`, or the instance animation
is done (`inst+0xc0 == 1`); copies `sub[0..3]` to `inst+0xb0..0xbc` (negative/too-large index ⇒ 0); on `restart`
`inst+0xa8 = now`; `vtbl[4]()` = `0x436a40` Reset (`current = −1`). `0x436b90(n, k)` = **AnimLen** =
`model.anim[sub[k]].frames · (1/4096) / speed` (`0x4aa138`). "Animation done" is **never** queried in this
controller: all transitions run on the timer `p+0x5b8`, which is set to `AnimLen` at the start.

Requests come from `0x457a50/0x457330` (attack) and from `Perso_AnimState 0x463e60` → `0x464630` (state 0/6…):
if `atk != 0` or `p+0x694`: nothing; jumper state 2 ⇒ `0x463f40` (loop/idle animations), otherwise `0x4642f0(0)`:

| situation | logical anim | .ins sub-anims | prio | speed |
|---|---|---|---|---|
| jumper 0/1 (rising) | **4** | 3, 4 | 1501 | 8.0 |
| jumper 7 (apex) | **5** | 5, 6, 7 | 1050 | 2.0 |
| jumper 3 with `fellOff` | **7** | 6, 7 | 5000 | 3.0 |
| jumper 3 with `shortHop` | **6** | 6, 7 | 1000 | 3.0 |
| jumper 4, onGround and Mover phase ≠ 2 | **8** (landing) | 8 | 1500 | 3.0 |
| jumper 4 otherwise | like jumper 3 | | | |
| jumper 5 in the air (long fall) | **9** | 7, 32, 33 | 1600 | 3.0 |
| jumper 5 on the ground (hard landing) | **0xa** + `LockMove(AnimLen(0xa,0))`, `p+0x524 = AnimLen`, shock `0x478980(p,1,2.0,180.0,50.0,0)` | 34 | 5200 | 3.0 |
| jumper 6 with `p+0x308 == 2` | no anim; dust effect `0x476140(&pos+(0,30,0), &p+0x458, 3, 0.25, 1.5)` | | | |
| peck dash (atk 1/2) | **0xb** | 9 | 1600 | 3.0 |
| hit (atk 3/4) | **0xc** | 10, 11 | 1600 | 3.0 |
| low wall bounce (atk 6) | **0xd** | 16 | 1600 | 3.0 |
| high wall bounce (atk 7) | **0xe** | 19, 6, 7 | 1600 | 3.0 |
| stuck peck (atk 8) | **0xf**, then 0x15 (state 4) | 21, 13 / 13 | 1600 / 5000 | 3.0 |
| charge-run windup (atk 9) | **0x10** | 38, 51 | 1700 | 3.0 |
| charge run (atk 10) | **0x11** | 51 | 1700 | 3.0 |
| brake (atk 11) | **0x12** | 51, 52 | 1700 | 3.0 |

In state 1 (`p+0x21c == 1`) `0x4642f0` uses set 0x6b..0x6f, with argument 1 set 0x47..0x4c.

## 5. Landing ring under the player (port reconstruction, issue #1)

As soon as Woody is off the ground, in the original there's a **bright ring on the floor beneath him** in the
spot where he will land. It's not the shadow — that's projected geometry (LIGHTING.md §4), it's also there when
he just walks and has the shape of the model; the ring belongs to the jump and disappears on landing.

**Not decompiled.** The draw function has not yet been located in `Woody.exe`; the ring below was read off a
screenshot. Best candidate to read: **`0x44af90(Perso)`**, the only still-unread Perso function that runs every
frame after rendering (`0x401dbd` → `0x44b4a0`, PERSO_FRAME.md §1 step 24, next to
`0x44ae60` Perso_UpdateHUD) — exactly the place for a ground decal. The tool is also available: the
sprite primitive `0x470f10` draws, without flag bit 0, not a billboard but a quad in the plane with the normal from
`S+0x230` (PERSO_DEATH.md §4.1, PROJECTILES.md §5.3), i.e. a flat quad on the ground normal. The port has had that
primitive since the footsteps too (`hud_world_decal`, FOOTSTEPS.md §4): once it's known which image and which
flags the original uses, the ring below can be replaced by it.

What the port does — `player_landing_ring` (`src/player.c`), `hud_world_ring` (`src/hud.c`), called in
`src/main_engine.c` between the world and the HUD:

| | |
|---|---|
| when | as long as `on_ground == 0` AND the player is falling under their own weight: not dead, no scripted action (state 5), not on the rocket (state 8), not on a wall (state 4) and not during the climb-over-root movement. Not during cinematics, in the free camera, or outside a playable level |
| where | `GetHeight` (`world_ground`, `0x435650`) from the feet + 43 (`P+0x00`) straight down, so both world polygons and the press nodes of instances (also on a moving platform). No maximum fall distance: above a pit the bottom lights up; no floor found ⇒ no ring. The ring sits directly under him, there's no forward projection using his horizontal speed |
| how | a ring in the plane of the found floor normal, 3 units above it (otherwise it z-fights with the floor), radius **69** = the Perso's collision radius (`P+0x04`), band width ±12% of the radius, white, additive, brightness 0.7, 48 segments. Fixed size: the ring doesn't shrink or fade with height. The brightness is in the vertex colors (0 on both edges, full at the radius), so the band has no hard edge and needs no texture |
| tuning knobs | `WOODY_RING=<radius>` sets the radius, `WOODY_RING=0` disables the ring |

Uncertain until `0x44af90` is read: radius, thickness, color and brightness, whether the ring pulses or rotates
along, whether the original places it on the floor normal or horizontally, and whether other actors get one too.

## 6. Open questions

* Substate 2 (peck dash) has **no dedicated time-out**: it only ends via a wall/ground hit from
  `0x4575b0` (50 along the dash direction, then 100 horizontal) or a target hit. Because the direction
  always points down (y = −2 before normalization ⇒ ≈ 63° downward with no target) it normally hits the ground ⇒
  state 6. What happens if the collision in `0x4624f0` puts the player on the ground without the ray hitting
  anything (log "On Ground during air attack") has not been followed further; external resets of `+0x5b4`:
  `0x44c980` (SetState), `0x44accb` (Reset), `0x44c372/0x44c690/0x44cbdf` (TakeHit/hit), `0x465787`, `0x41e432`, `0x44dd78`.
* The knockback direction in state 4 uses `M.dir.y` for the z component (`0x45868a`); that looks like a bug
  in the original (knockback along world-x only). In a reimplementation, `(−dir.x, 0.8, −dir.z)`
  normalized is probably the intent – verify with Frida.
* Unit of `AnimLen`: `frames/4096/speed` – whether `anim+4` is frames or 1/4096-second ticks has not been
  checked (see FORMAT_INS.md); the numeric values of `AnimLen(0xb..0x12)` need to be read from Woody's model.
* State 4 (`p+0x21c`, handler `0x4651d0`, "beak stuck in peckable surface", `p+0x50c = 2`, `p+0x520 = 0.8`)
  has not been decompiled.
* `vtbl[37]` (+0x94) and `vtbl[39]` (+0x9c) of the enemy classes (what does a hit do per enemy type, which
  script events follow) have not been traced.
* `0x478980(p, 1, 2.0, 180.0, 50.0, 0)` (hard landing) and `0x479c80` (peck impact) have not been read. The port does draw an
  impact at the call sites of `0x479c80` (a peck hole in the hit surface plus wood chips falling out of it), but that
  shape is a reconstruction — see OBJECTS.md §1.6.
