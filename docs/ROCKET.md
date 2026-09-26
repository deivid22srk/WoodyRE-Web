# ROCKET.md — rideable rocket (class type 20), bomb cannon (type 21) and Perso state 8

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone). Every claim has an address.
Floats are read from `.rdata`. Replaces OBJECTS.md §2.6 (that section was "in broad strokes"; two things in it were wrong, see §9).

**Key points up front (corrects the expectation from the assignment):**

* The rocket is **not steered**. It flies in a **straight line** from its start position to **point 0 of its own TRAJ** (`inst+0x78`,
  from the `.ins`), accelerates at 2000 u/s² up to `+0x11c` and explodes after exactly `+0x114` seconds of flight. No collision test at all during the flight.
* `+0x114` = **total flight time in s** (message 55 sub 1, ×0.01), `+0x11c` = **maximum speed in u/s** (message 55 sub 2).
* The player can only **jump off** (action 4 = jump or action 6 = attack, just pressed) and only during the flight (object state 5, 6, 7).
  Whoever stays seated **dies** in the explosion (`Kill(6)`, radius 600). The last second the rocket flashes red as a warning.
* The explosion only hits the **registered actors** (Perso, Boss2, enemy type 12), **not** type-17 objects and **not** chests (§4.3).
  In W1A the rocket therefore breaks nothing: it is purely **transport** over a chasm.
* Type 21 is a different thing: a **cannon that fires a bomb (class 40) straight ahead, with Woody riding on it** (W2A, W2B, W2D). Tied to the bomb system.

---

## 0. Recipe for the port (type 20, enough for W1A / WWS / KWS / SWS / K1A / S1A)

### 0.1 Data

```c
typedef struct {
    Instance *inst;  int type;              /* 20 (21: see §7, only after the bomb system) */
    int   state;  float t, speed;           /* +0x128, +0x130, +0x12c */
    float fly_time, vmax;                   /* +0x114 = 10.0, +0x11c = 1000.0 (message 55) */
    Vec3  start_pos;  Quat start_rot;       /* +0x134, +0x140 (original 3x3) */
    Quat  q0, q1;                           /* +0x170, +0x180 */
    int   exhaust;                          /* smoke emitter +0x16c: state +8 (0 off, 1 starting up, 2 on) */
} Rocket;
#define RK_TURN   2.0f    /* +0x10c */      #define RK_IGNITE 0.3f   /* +0x110 */
#define RK_WARN   1.0f    /* +0x118 */      #define RK_ACCEL  2000f  /* +0x120 */
#define RK_MOUNT  0.83f   /* +0x124 */      #define RK_BLAST  600f   /* 0x453593 */
#define RK_GONE   0.5f    /* 0x4a9014 */
```

### 0.2 Messages (`0x453730`)

* `1200 [inst, 20]` → create the `Rocket`, store start position/rotation, `exhaust = 0`, `inst->flags8 |= 0x20`.
* `55 [inst, 1, v]` → `fly_time = v * 0.01f`; `55 [inst, 2, v]` → `vmax = v`; other sub-numbers: nothing.
* `29 [inst]` → `rocket_reset()`.
* `40 [inst]` → if `state == 0` **and** player in Perso state 0 **and** on the ground: `player_mount()` (§0.4), `rocket_reset()`, `t = 0`, `state = 1`,
  `inst` non-collidable. Otherwise: nothing (the script only tries again 3 s later, §8).

```c
void rocket_reset(Rocket *r) {                 /* 0x452ae0 */
    inst.pos = start_pos; inst.rot = start_rot; recell; r->state = 0;
    inst.noncollide = 0;  fade_rate = 1.0f; fade_target = 0;      /* fade itself stays as is: after an explosion (fade 1) it comes back within 1 s */
    per exhaust marker: has_prev = 0;          /* exhaust state is NOT reset (see uncertain 6) */
}
```

### 0.3 Per-frame think step (`0x452e10`), only when not paused and `state != 0`; the fade update (`0x44e810`, INSTANCE.md §4) always runs

```c
exhaust.draw_this_frame = 1;
switch (state) {
case 1: t += dt; if (t >= 0.83f) state = 2; break;                         /* Woody is climbing on */
case 2: speed = 0; t = 0;                                                  /* determine turn target, 1 frame */
        f  = normalize(traj.point[0] - inst.pos);                          /* 3D */
        Ym = -f;  Zm = normalize(up - Ym * dot(Ym, up));  Xm = cross(Ym, Zm);   /* rows X,Y,Z = model axes in world space (§3.2) */
        q0 = quat(inst.rot);  q1 = quat(rows Xm, Ym, Zm);  state = 3; break;
case 3: loopB_active = 1;  t = min(t + dt, 2.0f);
        inst.rot = slerp(q0, q1, t / 2.0f);
        if (t >= 2.0f) { t = 0; state = 4; audio_fx(16, inst); exhaust = 1; } break;
case 4: t += dt; if (t >= 0.3f) { t = 0; state = 5; audio_fx(10, inst); } break;
case 5: fly(dt); t += dt; loopA_active = 1;
        if (t >= fly_time - 1.0f) { state = 6; t = 0; } break;
case 6: fly(dt); loopA_active = 1; t += dt;                                /* flashes red, §5.2 */
        if (t >= 1.0f) { audio_fx(6, inst); explosion_big(inst.pos); t = 0; state = 7; } break;
case 7: for (a in registered_actors) if (cat(a) == 1 || cat(a) == 2) a->Explode(&inst.pos, 600.0f);   /* player: §0.4 */
        fade = fade_target = 1.0f;  /* instantly invisible */  t = 0; state = 9; break;
case 9: t += dt; if (t >= 0.5f) rocket_reset(r); break;                    /* back at the start spot, fade-in 1 s */
}
loop(A: SoundFx 11 on inst, B: SoundFx 15 on inst);   /* starts on the first active frame, stops as soon as a frame is not active (SOUND.md §5) */

void fly(float dt) {                                                       /* 0x452cc0 */
    d = normalize(traj.point[0] - start_pos);                              /* fixed direction; not normalized at length 0 */
    speed = min(speed + dt * 2000.0f, vmax);
    inst.pos += d * (speed * dt);  inst.center = inst.pos;  recell;
}
```
No collision, no arrival test: the rocket flies through everything until the time is up (W1A: it overshoots the TRAJ point by 190..725 units, §8.3).

### 0.4 Player (Perso state 8, `0x465740` + `0x4657f0`)

```c
int player_mount(Player *p, Rocket *r) {           /* 0x465740 */
    if (p->state != 0 || !p->on_ground) return 0;
    p->ride = r; SetState(8); atk = 0; hit_by = 0;  /* +0x5b4, +0x5f0 */
    p->ride_p0 = p->pos; p->ride_q0 = quat(p->rot); p->ride_t = p->ride_T = 0.7f;   /* +0x6b4, +0x6c0, +0x6d0, +0x6d4 */
    reset wall and ground colliders;  return 1;
}
/* every frame in state 8; NO Perso_Move, NO MoveCollide, no gravity, Orient leaves the matrix alone (0x44bd4b) */
seat = marker(r->inst, typecode 0, n 0).p[0];      /* world space, current pose; model 47: local (0, -54.1, 23.2) */
if (p->ride_t <= 0) { p->pos = seat; p->rot = r->inst.rot; }
else { p->ride_t -= dt;
       if (p->ride_t < 0.7f - 0.3f) { u = (0.4f - p->ride_t) / 0.4f;       /* first 0.3 s: stays standing */
            p->rot = slerp(p->ride_q0, quat(r->inst.rot), u);  p->pos = p->ride_p0 + (seat - p->ride_p0) * u; } }
switch (r->state) { case 1: Anim(0x3b); break;  case 2: case 3: Anim(0x3c); break;  case 4: Anim(0x3d); break;
                    case 5: Anim(0x3e); can_leave = 1; break;  case 6: case 7: can_leave = 1; ending = 1; break; }   /* 0, 8, 9: nothing */
if ((JustPressed(4) || JustPressed(6)) && can_leave) { SetState(0); Jumper_Reset(); Jumper_ForceFall(1); set_dir = 1; }
if (set_dir || ending) Mover_SetFacing(normalize_xz(-p->rot.rowY));        /* = nose direction of the rocket, horizontal */
volume_test(p, p->pos + (0, 20, 0));                                       /* 0x462760(p, 20.0): trigger volumes keep working */
```
* Explosion on the player (`0x44d040`): if `|explosion − inst.pos|² < 600²`: `Hit(0, 0, &away_xz, 0, 0)` (knockback, 0 damage), rumble, **`Kill(6)`**
  (PERSO_DEATH.md: anim 0x2b, fade after 2.5 s). Anyone who jumped off in time and is > 600 away notices nothing.
* After jumping off: a regular fall (Jumper state 4), **no carried-over velocity** (the Mover ramp is not touched), facing = flight direction.
* Camera: follow camera in **behind mode** as long as state 8 (CAMERA.md §3.1, `0x4591ec`); `savedDir = Perso row Y` = −flight direction (3D). No dedicated distances.
* Port deviation that is needed: with `WOODY_GOD` (or `+0x270 > 0`) `Kill(6)` is ignored and the original stays in state 8 forever on the
  respawned rocket. Do this in the port: `if (r->state == 0 || r->state == 9) dismount` (same code as jumping off).

### 0.5 Testing

`--pos 8845 1140 330` or similar in W1A (volume 61 = cube of 400 around (8845, ~1260, 238), §8.2), press attack and **release**.
Expected: 0.3 s standing still, 0.4 s to the saddle, at 0.83 s the rocket starts turning for 2.0 s (loop 15), 0.3 s ignition (sound 16, exhaust sputters on),
launch (sound 10, loop 11), 0.75 s accelerating to 1500 u/s, at `t = 2.8 s` after launch flashing red, at 3.8 s explosion at ≈ (9379, 2216, −4808).
Jumping off around 2.9 s after launch (≈ 3750 units away) gets you to the TRAJ point (9306, 2065, −4104).

---

## 1. Class and struct

Ctor `0x452850(type)` (size 0x194, `0x403440` branch of SetTypeInstance 20/21; OBJECTS.md §2.6): base Instance ctor `0x42e1a0`, `+0x104 = 0`, `+0x128 = 0`,
`+0x16c = 0`, vtable `0x4ab1b8` (28 slots), `+0x164 = type`. One class for both types; every difference is `cmp [this+0x164], 0x14 / 0x15`.

| vtbl | address | what |
|---|---|---|
| [1] | `0x452890` | Init (§2) |
| [3] | `0x452e10` | think step (§3) |
| [4] | `0x403fe0` | `return this+0x104` (typeword) |
| [17] (`+0x44`) | `0x452ae0` | Reset (§4.4); also message 29 and called internally |
| [22] (`+0x58`) | `0x453730` | messages (§2.1) |
| [26] (`+0x68`) | `0x4537d0` | render color: flashes red in state 6 (§5.2) |
| — | `0x452bd0(out)` | **seat position** for the Perso (no vtable slot; called directly from `0x4657f0`) |
| — | `0x452ca0(out)` | copies the 3×3 rotation `+0x28..0x48` (9 floats) |

| offset | type | default | meaning | address |
|---|---|---|---|---|
| +0x08 | flags | `\|= 0x20` | 0x20 = don't re-cell on animation; **0x40 = non-collidable** during the ride | `0x4529b5`, `0x452aac`, `0x452b92` |
| +0x6c / +0xfc / +0x100 | f32 | 0 / 0 / 100 | FadeInst: transparency, target, speed (INSTANCE.md §4) | `0x44e7c0` |
| +0x78 | TRAJ* | .ins | **point 0 = flight target/direction** (`[[this+0x78]+0x10]+4..0xc`) | `0x452ebf`, `0x452ce7` |
| +0x104 | u32 | | typeword: type 20 → `(w & 0xfffffc44) \| 0x44` (category 4, subtype 2); type 21 → `\| 0x24` (category 4, subtype 1) | `0x4528cb`, `0x45299f` |
| +0x108 | f32 | 0 | unused (first field of the parameter block) | `0x453790` |
| +0x10c | f32 | **2.0** | turn time (state 3 and 8) | `0x453796` |
| +0x110 | f32 | **0.3** | ignition time (state 4) | `0x45379d` |
| +0x114 | f32 | **10.0** | **total flight time** (state 5 + 6); message 55 sub 1 (`v · 0.01`, `0x4aa0ac`); type 21: bomb lifetime | `0x4537b9`, `0x453765` |
| +0x118 | f32 | **1.0** | duration of the warning phase (state 6) | `0x4537c0` |
| +0x11c | f32 | **1000.0** | **maximum speed** u/s; message 55 sub 2 (`v`, no scaling); type 21: bomb speed | `0x4537a4`, `0x453757` |
| +0x120 | f32 | **2000.0** | acceleration u/s² | `0x4537ab` |
| +0x124 | f32 | **0.83** (`0x3f547ae1`) | wait time after mounting (state 1) | `0x4537c0` |
| +0x128 | int | 0 | **state** 0..9 | |
| +0x12c | f32 | | current speed | `0x452d5f` |
| +0x130 | f32 | | state timer | |
| +0x134 | vec3 | Init | start position | `0x4529bd` |
| +0x140..0x160 | 9×f32 | Init | start rotation (copy of `+0x28..0x48`) | `0x4529c3..0x452a20` |
| +0x164 | int | ctor | 20 or 21 | |
| +0x168 | Bomb* | 0 | type 21: the fired bomb (class 40) | `0x453434` |
| +0x16c | Exhaust* | type 20 | smoke/exhaust object 0x34 B (§5.1); type 21: NULL | `0x452987` |
| +0x170 / +0x180 | quat | | slerp start / end | `0x453147`, `0x45315b` |
| +0x190 / +0x191 | u8 | | sound sources (SOUND.md §5 "source"): loop SoundFx **11** / **15** | `0x4536ce`, `0x4536e8` |

## 2. Init `0x452890` and messages

1. `0x44e7c0` (FadeInst Init: `+0x100 = 100`, `+0xfc = 0`, `+0x6c = 0`).
2. Type 20: typeword, and an exhaust object like the missile (PROJECTILES.md §5.4; here inline `0x4528d8..0x452979`): `new(0x34)`, `+0 = this`,
   `+4` = number of marker nodes with **typecode 9** (counted with `GetVector(this, 9, ·, n)` `0x42f6b0` until it returns 0), `+0x24 = new(12·n)`, `+0x2c = new(n)`,
   `+0xc = 0`, `+8 = 2`, phases 0, **`+0x28 = 6`** (size-table index; the missile has 3), in list `[0x5e8564]`. Right after that **`+8 = 0`** (off; `0x45298f`).
   Type 21: only the typeword.
3. `inst+8 |= 0x20`; start position `+0xc..0x14` → `+0x134`, rotation `+0x28..0x48` → `+0x140`; `+0x168 = 0`; parameter-block defaults `0x453790` (table §1).

### 2.1 Handler `0x453730`

| message | code | effect |
|---|---|---|
| **29** (0x1d) | `0x453783` | `this->vtbl[17]()` = Reset |
| **40** (0x28) | `0x453779` → `0x452a50` | mounting, see below |
| **55** (0x37) | `0x45374e` | `arg[1] == 1`: `+0x114 = (float)arg[2] · 0.01`; `== 2`: `+0x11c = (float)arg[2]`; otherwise nothing |
| other | `0x453745` | `0x44e8f0` (FadeInst: 56/57, then base) |

`0x452a50` (message 40): only if `+0x128 == 0`. Walks the Npc table `0x4c4e00[0x4c5318]` until the first with category 1 (`0x40c340`, = the Perso),
calls `Perso::0x465740(this)`; on `true`: `this->vtbl[17]()` (Reset), `+0x130 = 0`, **`+0x128 = 1`**, `inst+8 |= 0x40` (non-collidable), both
sound sources initialized (`0x468e10`). No Perso found or `false` ⇒ nothing.

The class sets **no msgmask bits**, sends **no VM events**, touches **neither the HUD nor the textbox**, and does **not call the CamMgr** (no `0x443e50`,
`0x4c737c`, `0x456ed0` in `0x452850..0x4538ff`). The text "To take a ride…" is entirely script-driven (§8).

## 3. Think step `0x452e10` (jump table `0x453708` on `+0x128 − 1`)

Start: `0x44e810` (fade + animation events, always). Stops if the pause byte `[0x5e48cc]` or `+0x128 == 0`. `dt = [0x509adc]+0x38`. If there is an
exhaust object: `exhaust+0xc = 1` ("draw me this frame") — so in **every** state ≠ 0. End (`0x4536ba`, every frame): `0x468e50(&+0x190, this, 11, fx, −1.0)`
and `0x468e50(&+0x191, this, 15, fx, −1.0)` (loops starting/stopping depending on whether `0x468e40` was called this frame).

| state | code | type 20 | type 21 | transition |
|---|---|---|---|---|
| 0 | — | idle (think step does nothing) | same | message 40 → 1 |
| **1** mounting | `0x452e83` | `t += dt` | same | `t ≥ +0x124` (0.83) → 2 (timer keeps running: not cleared) |
| **2** determine target | `0x452eb3` | §3.2; `speed = 0`, `t = 0` | same | direct → 3 |
| **3** turning | `0x45317d` | loop 15 active; `t = min(t + dt, +0x10c)`; `rot = matrix(slerp(q0, q1, t / 2.0))` (`0x440dd0`, `0x440370`; CAMERA.md §3.4 for the helpers) | same | `t ≥ 2.0` → `t = 0`, 4, **SoundFx 16** (`0x45321a`, 3D on the instance), `exhaust+8 = 1` (starting up) |
| **4** ignition | `0x453239` | `t += dt` | same | `t ≥ +0x110` (0.3) → `t = 0`, 5; type 20: **SoundFx 10** (`0x453468`); type 21: fire the bomb + **SoundFx 14** (§7) |
| **5** flight | `0x453472` | `Fly(dt)` (§3.3), loop 11 active, `t += dt` | only `t += dt` (Fly and the loop are type-20-only) | `t ≥ +0x114 − +0x118` → `t = 0`, 6 |
| **6** warning | `0x4534d4` | `Fly(dt)`, loop 11, `t += dt`, flashes red (§5.2) | `t += dt` | `t ≥ +0x118` (1.0) → type 20: **SoundFx 6** (`0x45352c`) + `0x477060(1, &pos, 0)` (big explosion, §5.3); both: `t = 0`, 7 |
| **7** | `0x453555` | explosion damage (§4.3), `0x44e7f0(1.0, 1)` = transparency AND target instantly 1.0 (invisible), `t = 0` → **9** | `q0 = quat(rot)`, `q1 = quat(+0x140 start rotation)`, `t = 0` → **8** | 1 frame |
| **8** turning back | `0x453613` | (does not occur) | slerp back to the start rotation over 2.0 s | `t ≥ 2.0` → `t = 0`, **0** (without Reset: `+0x168` and the `0x40` flag remain until the next 40) |
| **9** gone | `0x453696` | `t += dt` | (does not occur) | `t ≥ 0.5` (`0x4a9014`) → `vtbl[17]()` Reset → 0 |

Total timeline type 20 from message 40: 0.83 + (1 frame) + 2.0 + 0.3 = **3.13 s until launch**, then `+0x114` s flight, 0.5 s gone, then 1 s fade-in at the starting spot.

### 3.2 Turn target (state 2, `0x452ebf..0x453178`)

```c
d  = -normalize(P0 - pos);                 /* P0 = TRAJ point 0; ×−1 = 0x4a9500; length 0 → not normalized */
v  = (0, -1, 0);                           /* 0x43ff80(0, -1.0, 0) */
A  = normalize(cross(d, v));               /* 0x41af10: out = this × arg */
B  = normalize(cross(d, A));               /* = "up" perpendicular to d:  normalize(up − d·(d·up)) */
A2 = normalize(cross(d, B));               /* = −A = cross(v, d) */
M  = rows { A2, d, B };                    /* esp+0xac.. ; row 0 = model-X, row 1 = model-Y, row 2 = model-Z (INSTANCE.md: rotation in rows) */
q0 (+0x170) = quat(inst.rot);   q1 (+0x180) = quat(M);        /* 0x4404b0 */
```
So: **model −Y = flight direction**, model +Z = up, model +X = Y × Z. This matches model 47: marker typecode 0 runs from (0, −54.1, 23.2) to (0, −154.1, 23.2)
(−Y = nose), exhaust marker typecode 9 from (0, 115.9, 23.2) to (0, 155.9, 23.2) (+Y = tail), bbox x ±42, y −241..118, z −19..68. It's the same axis
convention as the Perso itself (row Y = −look direction, PERSO_FRAME §2.4), which is why the Perso can take over the rocket's matrix 1-to-1.
Vertical target (d ∥ y): A has length 0 and stays 0 ⇒ degenerate matrix; does not occur in the data.

### 3.3 `Fly(dt)` `0x452cc0` (type 20 only)

`dir = normalize(P0 − +0x134)` (**start position**, so the same direction every frame; length ≤ 0 → not normalized); `speed = min(speed + dt · +0x120, +0x11c)`
(`0x452d5f..0x452d88`); `pos += dir · speed · dt`; `inst+0x60..0x68 = pos`; `0x4077f0(this, 0)` (re-cell). **No** ray/sphere test against world or instances,
no arrival test at P0. Distance traveled after T s: `vmax²/4000 + vmax·(T − vmax/2000)`.

## 4. Mounting, seat position, damage, reset

### 4.1 Seat position `0x452bd0(out)`

Type 20: always `GetVector(this, 0, v, 0)` → `out = v[0]` (start point of the first marker node typecode 0, world space, current pose; so it turns along in state 3).
Type 21 (jump table `0x452c84`, bytes `0x452c8c` = 0,0,0,0,1,1,1): state 1..4 → marker 0; state 5..7 → if `+0x168`: **bomb position** (`bomb+0xc`), otherwise `out`
left untouched; other states: `out` left untouched.

### 4.2 What the rocket "sees"

Nothing: no distance test to the player, no collision. Activation only happens via message 40 (script). The two press nodes of model 47 (nodes 2 and 3, typecode 0, no
collision ids in W1A) only participate in the regular instance collision, and that is off (`0x40`) from message 40 until the Reset.

### 4.3 Explosion damage (state 7, `0x453560..0x4535a9`)

For every actor in list 1 `0x4c52d8[0x4c531c]` (pairs of 8 B; registered via `0x40c080` — **only** by the Perso `0x44b6b0`, Boss2 `0x40dd58` and enemy type 12 `0x4110e6`)
with category 2 or 1: `actor->vtbl[40](&this->pos, 600.0)` (`0x44160000`).
* Perso `vtbl[40]` = `0x44d040(pos, r)`: `|pos − Perso.inst.pos(+0xc)|² < r²` ⇒ direction `normalize_xz(Perso.pos − pos)`, `vtbl[39](0, 0, &direction, 0, 0)` (Hit with 0 damage:
  knockback/anim, PERSO_MOVE §4.4), rumble `0x44d1b0(+0x1d4, +0x1d0)`, **`vtbl[38](6)` = Kill(6)** (ignored if `+0x270 > 0` or already dead).
* Enemies `vtbl[40]` = `0x41ae20` (ENEMY.md §7): dies instantly within r — but regular enemies are not in this list, so in practice only Boss2/type 12.
* Type-17 breakable objects and chests 120/121 are only hit by the **bomb** (`0x44d650`: own lists, radius 400; BONUS.md §7), **not** by the rocket.
  `0x477060` itself does no damage (PROJECTILES.md §5.3).

**The loop in detail** (only for type 20: `cmp [+0x164], 0x14` at `0x45355b`; type 21 jumps to the turn-back at `0x4535cf`):
```c
for (i = 0; i < [0x4c531c]; i++) {                         /* 0x453560; ebx = i, ebp walks 0x4c52d8 in steps of 8 */
    Actor *a = list1[i].actor;                             /* +0 = actor, +4 = the argument of RegisterActor (always 10) */
    if (Category(a) == 2 || Category(a) == 1)              /* 0x40c340 twice; every entry passes (the three registrants are 1 or 2) */
        a->vtbl[40](&this->pos /* +0xc */, 600.0f);        /* call [edx+0xa0], 0x44160000; no return value used, no rider exception */
}
FadeInst(1.0, 1);  t = 0;  state = 9;                      /* 0x4535ac */
```
* **List 1** is double-buffered: `RegisterActor 0x40c080` (ecx = actor, arg 10) appends to the building list `0x4c5218[0x4c5320]` (max **8**, extra
  entries are dropped); `0x40bf60` (once per frame) copies it to `0x4c52d8[0x4c531c]` and empties the building list. So every reader
  sees the actors registered in the **previous frame**. Readers: this loop, `HitActors 0x44a0a0` (projectiles), the laser hit test `0x450f80`,
  the storm `0x451d32`, and the nearest-actor query `0x40c0d0` (category/subtype filter, 3D distance below a limit, |dy| below a limit).
* **Registrants** (all three `call 0x40c080` in the image):
  * the Perso `0x44b6b0`: only if `+0x690 == 0`, `+0x26c == 0` and state `+0x21c != 5` (not dead);
  * enemy type 12, the bomb thrower, `0x4110e6`: first thing in its Update `0x4110c0` after `Enemy_Update 0x41a3e0`, every frame (also when dead);
  * Boss2 (class 15) `0x40dd58`: in its Update `0x40dd30` once message 61 has linked the crushers (`+0x1c8 != 0`).
  Both enemy Updates only run after Think `0x41a320` (in the world, within `active_d` of the camera, or dead). No other class registers,
  so ordinary enemies, Buzz (14), class 16 and all objects are never hit by the rocket.
* **What `vtbl[40]` does** per registrant: Perso `0x44d040` (above: Kill(6)); thrower `0x4119b0` (1 hp per blast, 3.6 s immune, ENEMY2.md §4);
  Boss2 `0x40e800` (sphere r against his cylinder 50 × 140, 1 of 6 hp, no immunity, BOSS15_16.md §6.1).
* **In the shipped levels this never meets an enemy**: type 20 exists only in W1A, WWS, KWS, SWS, K1A, S1A; the thrower only in W2B (533) and Boss2
  only in W2D (762) / W3D (790) (`1200 [inst, type]` over all 28 `code` files). W2B and W2D have only the **cannon** (type 21), whose ridden bomb
  explodes through the bomb path `0x44d650` (Npc table `0x4c4e00`, r 400 — Boss2 and the thrower are hit through that, BOMB.md §4); the W2D cannon 178
  (−11096, 3524, −2405) shoots toward (−9905, 4325, 1118), ≈ 12000 from Boss2's arena at (−3928, 2206, 9596). W3D has neither a rocket nor a cannon.
  So in the original the rocket blast only ever hits Woody; Boss2 and the thrower are reachable only in principle.
* **Port** (`src/main_engine.c` rocket state 7 → `enemies_actor_blast` in `src/enemy.c`): the player test as before, then `vtbl[40](pos, 600)` on
  every enemy whose last Update registered it (`Enemy.list1`, set in `enemies_update` from the same conditions: type 12 / 15, in the world,
  within `active_d` or dead, class 15 only after message 61) → `bomber_blast` / `boss15_blast`. Order within one blast: the player first, then
  the enemies (the original's list order is the frame's Update order; it only matters when the same blast kills Woody and takes Boss2's last
  point: then his `vtbl[36]` test keeps him at 1 hp). Verified with a forced rocket (temporary hack turning the W2D cannon into a type-20
  rocket exploding at (−3928, 2300, 9900)): `BOSS2 762 blast: hp 5`, the HUD bar drops to 5; at (−3928, 2300, 10300) (704 away) no hit;
  in W2B at (10953, −2900, 11400): `THROWER 533 blast: hp 4`. W1A unchanged (`ROCKET 323 blast kills the player`).

### 4.4 Reset `0x452ae0` (vtbl[17])

Position, rotation and center (`+0x60`) back to the start values, `+0x128 = 0`, `0x4077f0(this, 0)`; if `+0x168` (type 21): `bomb->vtbl[17]()` (deactivate bomb) and `+0x168 = 0`;
`inst+8 &= ~0x40`; **`+0x100 = 1.0`** (fade speed 1/s) and `0x44e7f0(0, 0)` (target 0, transparency stays): after an explosion (transparency 1.0) the rocket becomes visible
again in **1 s**; every `exhaust+0x2c[i] = 0` ("no previous marker position"). `+0x12c/+0x130` are not cleared (state 2 does that).
Note: Reset is also called at the start of every ride (`0x452a9c`), before `+0x128 = 1`.

## 5. Effects and sound

### 5.1 Exhaust (marker typecode 9)

Draw function `0x475440`, list `[0x5e8564]`, described in PROJECTILES.md §5.4 (glow image 32 ×2, flame image 31 as three crossed quads, smoke image 14 with 200 puffs/s,
bank 0). Differences for the rocket:
* size table `0x4abcc8` with **index 6** ⇒ flame **100** (+ `rand·10 − 5`), glow 1 **70**, glow 2 **60** (missile, index 3: 45 / 40 / 35).
* emitter state `+8`: 0 until the end of turning; then **1 = starting up**: timer `+0x1c` runs for 1.0 s, scale factor `s` for the sizes (`0x475560..0x475601`):
  `0 < τ < 0.15` → `τ·6.667`; `0.3 < τ < 0.45` → `(τ − 0.3)·6.667`; `0.85 < τ < 1.0` → `(τ − 0.85)·6.667`; otherwise 0 (three "sputters"; `0x4aa1c8`, `0x4aab98`, `0x4ab79c`,
  `0x4aa3d8`, `0x4abd04`); at `τ ≥ 1` → state **2 = on** (`0x475542`). Because the ignition only lasts 0.3 s, the second and third sputter already fall within the flight.
* only drawn on frames where the think step sets `+0xc = 1` (rocket state ≠ 0).
* trail direction `d` (`0x47563d..0x475694`): the first frame `P1 − P0` of the marker, after that the stored point minus this frame's `P0`;
  for size index **6** (and 9) the stored point is the marker's own `P1` (`0x475c2c..0x475c75`, written in the puff loop), for the others the
  last puff position. So the rocket's flame lies along `P1(previous frame) − P0`: in flight it leans back along the way flown.
* the puffs run in state 1 too, also between the three sputters: `+0x20` (the puff accumulator) grows in every state but 0 (`0x4754e3`).
* **Port** (`rockets_draw` → `exhaust_glow_flames`, shared with the race board's spray): the camera-facing glow (70), the glow in the plane
  across `d` (60, flags 6), and the flame as **three 2:1 quads** (image 31, mode 0x13, alpha 0.8, flags 0x62) crossed on `d` at 0/85/170
  (1/512 turn) + `f3·512`, size `s·100 + rand·10 − 5` (the ramp scales the table size only, `0x4758b8`), centred `d · size · (cos 37 − 1/64)`
  from the nozzle (`0x475912..0x4759c5`). Until this round it was one billboard of `(100 ± 5)·s` at `0.4·size` and both glows camera facing.

### 5.2 Flashing red (vtbl[26] `0x4537d0`)

Only type 20, state 6, not paused: `k = (int)(t · 20.0) & 1` (`0x4a9994`, `0x499580`). `[0x5ac850] = 1` (= **multiply** the vertex color, `0x43bdfc`),
`[0x5ac860] = 0`, color `[0x5ac854..c]` = `k == 0` → **(1, 0, 0)**, otherwise (1, 1, 1). So 10 Hz red/normal during the last second. Otherwise `[0x5ac850] = 0`.
(Correction to LIGHTING.md §3: mode 1 = multiply, mode 2 = add, `0x43bdd3..0x43be1d`.)

### 5.3 Explosion `0x477060(1, &pos, 0)` (kind 1, `0x477089`)

Four effect records (pool `[0x5e823c]+0xdb8`, 0x50 B, max 2000): `0x476b50` (0.2 s: 60 particles/s ≈ 12 units, each record `0x4767f0`, lifetime 2.0 s, random direction
`normalize(2r−1, 2r−0.5, 2r−1)`, start point `pos + direction·300`), `0x476cd0` (0.2 s: 400/s ≈ 80 units, record `0x4764f0`), `0x4762e0` with **R = 1400** (0.3 s) and — falling
through into kind 2 — `0x4762e0` with **R = 400** (0.3 s). `0x4762e0` = nine flat quads image 12, `size = R·(0.3 + 0.7·sin(u·π/2))`, `alpha = 0.3·cos(u·π/2)` (PROJECTILES.md §5.3).
The particle callbacks `0x4767f0` / `0x4764f0` are decompiled in PARTICLES.md §5: ~12 **burning pieces** that fly 2 s at 1200 u/s on arcs that bend 0.2 down
every 0.1 s, each leaving white smoke (image 15) and fire (image 13) every 0.025 s behind an additive head (image 12), plus ~80 **white dust clouds**
(images 16/17, alpha 0.2, 0.7 s) flying up to ~420 units into the upper half. The port draws them (`fx_explode`, `game_explosion`) together with the two
flashes, as the nine flat quads of `0x4762e0` itself (`hud_world_fx_plane`), no longer as a single billboard at ×3 brightness. The same effect is used by chests (`0x4517d0`)
and bombs, so it belongs in a shared effect function: in `src/main_engine.c` that is `blast_add` / `fx_smoke_draw`, which the rocket and the missiles now share.

### 5.4 Sounds (SoundFx table SOUND.md §5; all 3D on the rocket instance)

| id | ref (Common bank) | vol | when | address |
|---|---|---|---|---|
| 15 (loop) | 7 | 50 | during turning (state 3); source `+0x191` | `0x45317d`, `0x4536e8` |
| 16 | 8 | 50 | end of turning = ignition | `0x45321a` |
| 10 | 10 | 50 | launch (type 20) | `0x453468` |
| 14 | 9 | 52 | bomb fired (type 21) | `0x45344a` |
| 11 (loop) | 5 | 50 | during flight (state 5 and 6, type 20); source `+0x190` | `0x45349d`, `0x4534f5`, `0x4536ce` |
| 6 | 4 | 50 | explosion (type 20) | `0x45352c` |

### 5.5 Animations of the rocket model

None: the class never starts a `.ins` animation (no `0x436ca0`/PlayAnim call). Model 47 has one 10 s animation with constant tracks (all nodes 1 key);
turning and flying happen entirely via `inst.pos`/`inst.rot`.

## 6. Perso side

### 6.1 `0x465740(obj)` → bool

`+0x21c != 0` ⇒ false; `!onGround` (`0x44bcf0`) ⇒ false. Otherwise: `+0x6b0 = obj`, `SetState(8)` (`0x44c980`), `+0x5f0 = 0` (last attacker), `+0x5b4 = 0` (attack substate),
`+0x6b4..0x6bc = pos (+0x1f4)`, `+0x6c0..0x6cc = quat(rot +0x28)` (`0x4404b0`), **`+0x6d0 = +0x6d4 = 0.7`** (`0x3f333333`), colliders `+0x298` and `+0x2bc` reset (`0x436d10`).

New Perso fields: `+0x6b0` rideable object, `+0x6b4` vec3 start position, `+0x6c0` quat start rotation, `+0x6d0` remaining transition time, `+0x6d4` transition duration.

### 6.2 State 8 per frame `0x4657f0` (from `0x44b822`; `doPost = false` ⇒ no `0x44bb20` Perso_Move, no `0x4624f0` MoveCollide, no cell change `0x462a40`)

1. **Attaching** (`0x465811..0x465966`): `+0x6d0 ≤ 0` ⇒ `pos = obj.Seat()` (`0x452bd0`), `rot = obj.rot` (`0x452ca0`). Otherwise `+0x6d0 −= dt` (`+0x2f8`); as long as
   `+0x6d0 ≥ +0x6d4 − 0.3` (`0x4aab98`): nothing (Woody keeps standing, anim 0x3b begins); after that `u = ((+0x6d4 − 0.3) − +0x6d0) / (+0x6d4 − 0.3)`,
   `rot = matrix(slerp(+0x6c0, quat(obj.rot), u))`, `pos = +0x6b4 + (Seat − +0x6b4)·u`. The **Perso follows the rocket** (position AND full 3×3), never the other way around.
   `Perso_Orient` (`0x44bd30`) skips state 8 (`0x44bd4b`), `0x44bf10` copies the position to the instance.
2. **Animation** matching object state (jump table `0x465af0` on `obj+0x128 − 1`), request to controller `+0x494`:

   | object state | type 20 | type 21 | may jump off |
   |---|---|---|---|
   | 1 | **0x3b** | **0x36** | no |
   | 2, 3 | **0x3c** | **0x37** | no |
   | 4 | **0x3d** | **0x38** | no |
   | 5 | **0x3e** | **0x39** | **yes** |
   | 6, 7 | (no new request) | | **yes**, and every frame set facing |
   | 0, 8, 9 | nothing | | no |

3. **Jumping off** (`0x465a0e..0x465a55`): `JustPressed(4)` (jump) or `JustPressed(6)` (attack) (`0x467420`) AND "allowed": `SetState(0)`, `Jumper_Reset` (`0x462c90`),
   `Jumper_ForceFall(J, 1)` (`0x463170`: Jumper state 4 = falling). There is **no** left/right/up/down steering: no single `Held`/`Analog` call in the function.
4. After jumping off, or every frame in object state 6/7: `Mover_SetFacing(M, normalize_xz(−rot.row1))` (`0x465a5f..0x465ac5`, `0x459ff0`) = the horizontal flight direction.
5. Always: **`0x462760(p, 20.0)`** (`0x465ad2`): volume test on `pos + (0, 20, 0)` (normally 71.0, EVENTS.md §2) ⇒ trigger volumes (Enter/In/Leave, script 1020-chasms!) keep working.

Logical animations (table `0x4b6180`, record `{sub[4], prio, speed, restart}`; all prio **1800**, speed **3.0**, restart 1; duration = `.ins` duration / 3):

| log. | .ins chain | duration (Woody model, W1A) | usage |
|---|---|---|---|
| 0x3b | 84 | 2.6 s / 3 = **0.867 s** | type 20 mounting (rocket waits 0.83 s) |
| 0x3c | 80 | 6.0 / 3 = **2.0 s** | type 20 sitting during the turn |
| 0x3d | 81 → 82 (loop) | 0.567 s, then 0.2 s loop | type 20 ignition (already replaced by 0x3e after 0.3 s) |
| 0x3e | 82 (loop) | 0.2 s | type 20 flight |
| 0x3f | 83 → 7 | 0.2 s | type 20 dismounting — **never requested anywhere** (no `push 0x3f` with an animation call in the exe) |
| 0x36 | 45 | 2.5 / 3 = 0.833 s | type 21 mounting |
| 0x37 | 41 | 4.0 / 3 = 1.333 s | type 21 turning |
| 0x38 | 42 → 43 (loop) | 0.333 s, then 0.133 s loop | type 21 ignition |
| 0x39 | 43 (loop) | 0.133 s | type 21 flight |
| 0x3a | 44 → 7 | 0.233 s | type 21 dismounting — never requested |

The root tracks of 80..84 themselves contain the sitting posture (root ≈ (0, 5.75, 41.6), rotation ≈ 63° about X), so position = marker, rotation = rocket matrix is all the port
needs to do. `Perso_AnimState` (`0x463e60`, table `0x463f14` index 7) itself requests nothing in state 8, it only ticks the controllers.

### 6.3 What else runs during state 8

* The steps before the state switch (PERSO_FRAME §2.1) keep running, but do nothing: the attack trigger `0x457330` requires state 0 (`0x4573ad`), the attack controller
  `0x457a50` stops at `+0x5b4 == 0`, `0x44ba70` only calls `0x463430` in state 0.
* Timers, the 25-bonus rule, `RegisterActor` (needed: it keeps the Perso in the explosion list), HUD update: normal.
* **Being hit** during the ride: `Hit` `0x44ca00` does not change state 8 (only 4 → 0, `0x44cbba`); knockback is set but not executed (no Perso_Move);
  health simply decreases; health 0 ⇒ `Kill(3)` ⇒ state 2, the rocket keeps flying empty, explodes and resets.
* **Death** during the ride (Kill(1) from a chasm volume, or whatever): state 2; `+0x6b0` stays but is no longer read. The rocket is told nothing.
* Invulnerability after the ride: none. Anyone within 600 of the explosion dies (unless `+0x270 > 0`).

## 7. Type 21: bomb cannon (W2A 411, W2B 214/295/297/299/307/512, W2D 178; models 44 / 36 / 40)

Same automaton, with these differences (all branches on `+0x164`):
* no exhaust, no loop sound 11, no `Fly`, no explosion, no flashing red, no state 9.
* State 4 → 5 (`0x453279..0x45345a`): `m = GetVector(this, 0, ·, 0)`; `dir = normalize(m[1] − m[0])`; projectile block `T` (PROJECTILES.md §1.1) = standard ctor inline,
  then **template 0** (`0x449070(0, &T)`: the "thrown bomb", radius 30, damage 1000, unlimited bouncing, invisible), then overwritten: `T.pos = m[0]`, `T.dir0 = dir`,
  **`T+0x1c` gravity = 0**, **`T+0x20` speed = `+0x11c`**, **`T+0x28` air drag = 1.0**, **`T+0x2c` lifetime = `+0x114`**. `bomb = 0x44d5d0(&T, 0, −1, 1)`
  (first free class-40 bomb from `0x5e4880[]`; `0x44d4d0`: fuse `+0x10c = T.life`, `+0x110 = min(2.0, life)`, **not** placed on the ground, script var −1, `+0x128 = 1`),
  `+0x168 = bomb`, **`bomb+0x133 = 1`** ("ridden": the bomb stays non-collidable `0x44d894` and cannot be picked up `0x4634ab`), SoundFx 14.
  No free bomb ⇒ `0x44d5d0` returns NULL and writing to `NULL+0x133` crashes the original (so levels always have bombs available).
* Seat position in state 5..7 = bomb position ⇒ **Woody rides on the bomb**; rotation stays that of the cannon. Jumping off same as type 20.
* State 7 → 8: cannon turns back to `+0x140` in 2.0 s, then state 0 (no Reset: the `0x40` flag and `+0x168` stay until the next message-40 Reset).
* The bomb explodes on its own (class 40, `0x44d6e0`, radius 400: type-17 objects, chests **and** all Npcs of category 1/2 from `0x4c4e00` via `vtbl[40](pos, 400)` ⇒ also the Perso ⇒
  `Kill(6)` if Woody is still riding on it). This is the mechanism used to blow up walls/chests in W2x.

Ported (BOMB.md §9): `Rocket.type == 21` in `src/main_engine.c`; the bomb comes from `bomb_start`, the seat position in state 5..7 is the bomb, state 8 turns back.
Test W2A: `extract/Data W2A --pos 9887 160 -12700 --peck 0.7 0.1` (volume 95 = instance 356), firing ≈ 4.0 s, explosion ≈ 7.2 s; `--jump T` to jump off.

## 8. Script side

### 8.1 Usage in all levels (`tools/ekodisasm.py` over all 28 `code` files; `1200 [inst, 20|21]`)

| level | inst | type | model | position | TRAJ point 0 | 55 sub 1 (s) | 55 sub 2 (u/s) |
|---|---|---|---|---|---|---|---|
| **W1A** | 323 | 20 | 47 | (8845, 1137, 185) | (9306, 2065, −4104) | 3.80 | 1500 |
| **W1A** | 326 | 20 | 47 | (8138, 2014, −3080) | (8195, 2448, 656) | **4.20** | **1000** |
| WWS / KWS / SWS | 214 / 210 / 210 | 20 | 48 / 47 / 47 | (−4659, 654, 2129) | (−4654, 1202, −1448) | 2.50 | 1500 |
| K1A | 366 | 20 | 44 | (5401, 1855, −7678) | (−1722, 2006, −7679) (+1 point) | 3.50 | 2500 |
| S1A | 419 / 425 / 483 | 20 | 42 | (8153, 2007, −3085) / (9432, 1887, −932) / (−1209, 1948, −7396) | (9788, 2058, −1027) / (7833, 2077, −461) / (5795, 2997, −7485) | 3.2 / 2.6 / 3.5 | 1000 / 800 / 2500 |
| W2A | 411 | 21 | 44 | (9882, 139, −12797) | (11263, 651, −17032) | 3.2 | 1300 |
| W2B | 214, 295, 297, 299, 307, 512 | 21 | 36 | — | — | 1.5..4.0 | 1200..1500 |
| W2D | 178 | 21 | 40 | (−11096, 3524, −2405) | (−9905, 4325, 1118) | 2.75 | 1500 |

Only point 0 of the TRAJ is read; a second point (often ≈ the start position) is unused. Every instance gets only **1200, 55, 55 and 40** in every level
(no 29, no 56/57, no MSGTEST).

### 8.2 W1A script objects (`out/w1a_code.txt`; 0x1000143 = inst 323, 0x1000146 = inst 326)

Object 323 (init): `SEND 1200 [323, 20]`, `SEND 55 [323, 1, 380]`, `SEND 55 [323, 2, 1500]`. Object 326: `1200 [326, 20]`, `55 [326, 1, 420]`, `55 [326, 2, 1000]`.
No per-frame code.

Object **324** (= instance 324, model 29, volume carrier at (8845, 1260, 238); **volume 61** = cube of 400: x 8645..9045, z 38..438, y band of 400 around the rocket):
```
init:  var62 = 0; var63 = 1; var64 = 1; var65 = 1
frame: if VOL_FLAG5(61)                      SEND 1050 [var62, 2]          ; Perso in the volume: var62 = "attack just RELEASED" (GAMEFLOW.md 1048..1050)
       if var62 == 1 && var63 == 1 {         var63 = 0;  DELAY 300 { var63 = 1 }          ; 3 s repeat-block
                                             SEND 40 [323];  var62 = 0 }
       if VOL_FLAG5(61) && var64 == 1 && var65 == 1 {  var64 = 0; var65 = 0;
                                             SEND 1080 [0, 2, var64, str 124, str 125, str 126] }   ; "To take a ride on the rocket / stand next to it and / press the ACTION button"
       if var64 == 0 && VOL_FLAG3(61) {      var64 = 1;  DELAY 60 { var65 = 1 } }         ; on leaving: close the textbox (var != 0, HUD_TEXT.md §3), allowed again after 0.6 s
```
Object **327** (instance 327, model 29 at (8150, 2095, −3178); **volume 62**: x 7950..8350, z −3378..−2978): identical without the textbox: `1050 [var66, 2]`, `var67`
repeat-block 3 s, `SEND 40 [326]`.

Otherwise **nothing** in W1A refers to 323/326 (no 29, no camera, no MSGTEST, no door/wall): the only hits on `0x1000143`/`0x1000146` are the six lines above
and `PUSH 323`/`PUSH 326` in objects 324/327.

### 8.3 Flights in W1A (from §3.3; distance traveled = `vmax²/4000 + vmax·(T − vmax/2000)`)

| rocket | direction (normalized) | distance to P0 | flight path | explosion point | role |
|---|---|---|---|---|---|
| 323 | (0.104, 0.210, −0.972) | 4412 | 562 + 4575 = **5137** in 3.8 s | ≈ (9379, 2216, −4808) | up (+930) and 4300 toward −z |
| 326 | (0.015, 0.115, 0.993) | 3762 | 250 + 3700 = **3950** in 4.2 s | ≈ (8197, 2468, 842) | back toward +z, +430 up |

The rocket therefore passes P0 0.48 s (323) resp. 0.19 s (326) before the explosion; the flashing starts 1.0 s before that. The player must jump off around P0 and is then
still > 600 from the explosion point for 323 (725), but **not** automatically for 326 (190 + whatever he still travels himself): there he must jump earlier. The rocket "opens" nothing.

## 9. Corrections to earlier documents

* OBJECTS.md §2.6: "spawns its own projectile/bomb" only applies to **type 21**; "hits actors" = only the registered list `0x4c52d8` (in W1A: the player himself).
  The parameters of 326 are `55 [.,1,420]` and `55 [.,2,1000]`, not 380/1500.
* PERSO_FRAME §3: state 8 = "rides object `+0x6b0` (class 20/21)", not "rail?".
* LIGHTING.md §3: `[0x5ac850] == 1` multiplies the vertex color by `[0x5ac854..c]`, `== 2` adds it (§5.2).
* BONUS.md §7: the bomb explosion `0x44d650` also walks the Npc table `0x4c4e00` (category 1/2, `vtbl[40](pos, 400)`), not just type 17 + chests.

## 10. Uncertain / not checked

1. ~~**Axis direction of the start orientation**~~ - **verified live** (`tools/wverify.py --probe rocket`, W1A, Woody teleported to
   (8845, 1160, 385), attack tapped at 7 s): the rows `+0x28` of 323 start as (1, 0, 0), (0, 0.3827, −0.9239), (0, 0.9239, 0.3827), so the
   nose (−Y) points toward (0, −0.38, +0.92), away from the target, exactly the port's `.ins` reading; state 2 stores q0 = (0.5556, 0, 0,
   0.8315) and q1 = (−0.0416, 0.6275, 0.7768, −0.0336) (x, y, z, w; the rows are the transposed matrix of the quaternion), the 2 s of
   state 3 follow `slerp(q0, q1, t/2)` to within 0.001 over all 1517 traced frames, and the end rows (−0.9943, 0, −0.1069),
   (−0.1045, −0.2102, 0.9721), (−0.0225, 0.9777, 0.2090) are the port's (`WOODY_ROCKETLOG=1` prints the port's rows; max difference
   against the same slerp 0.0006). The drawn node matrices (palette `[0x509adc]+0xa0`) follow the rows with one frame of lag, so
   the rocket visibly turns. State timings: 0.83 s mount, 1 frame state 2, 2.0 s turn, 0.3 s ignition, then the flight.
2. The channel of the flash color: `[0x5ac854]` is the first component of the vertex color `+0x24`; assumed to be red (vertex colors are R,G,B).
3. ~~Explosion kind 1: the particle records `0x4767f0` and `0x4764f0` are not decompiled~~ — resolved, PARTICLES.md §5.
4. Camera: state 8 only sets the behind flag (`0x4591ec`); the stored direction is row 1 of the rocket matrix and has a **y component** here. How `0x424760` handles
   that (ignores y or tilts the camera along) has not been checked. There are no separate distances/heights for the ride.
5. Speed after jumping off: Mover ramp A is never cleared or set anywhere; assumed ≈ 0 because the player was standing still/attacking when mounting. A charge run (attack
   released = also the trigger for the charge run, PERSO_JUMP §2.2) could have started in the same frame just before message 40 arrives; `0x465740` clears `+0x5b4`, but whether the
   ramp still holds 700 u/s at the moment of jumping off has not been tested.
6. Exhaust state `+8` is not reset to 0 by Reset: on a **second** ride the exhaust is already burning (state 2) right from mounting. That's how it is in the code
   (`0x452baa..0x452bcb` only clears `+0x2c[]`); whether that is visible in the game has not been verified. The port may set `exhaust = 0` in the Reset.
7. The smoke/flame details of `0x475440` are taken over from PROJECTILES.md §5.4 ("read in broad strokes"); only the index-6 sizes and the startup ramp have been checked here.
8. Type 21: not read further than §7 (bomb update `0x44d870`, what happens to the Perso if the cannon is already in state 8/0 while he is still in state 8 —
   presumably the bomb has already exploded by then and he is dead or has jumped off).
9. Staying seated without dying (`+0x270 > 0`, cheat): the original then stays stuck in state 8 on the respawned rocket (object state 0/9 ⇒ no exit). See the
   port deviation in §0.4.
10. Everything is static analysis; nothing has been checked against the running original with `tools/wtrace.py` (the 0.83 / 2.0 / 0.3 s timings can be measured well against sounds 16 and 10).
