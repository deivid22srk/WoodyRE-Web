# OBJECTS.md — interactive non-enemy classes (peck switches, climb walls, lasers, launchers)

Source: static analysis of `game/Woody.exe` (image base 0x400000, all addresses are VAs), the W1A script
(`out/w1a_code.txt`, `tools/ekodisasm.py`) and `extract/Data/W1A/W1A.ins` / `.tex` (`tools/insparse.py`,
`tools/levelparse.py parse_tex`). Floats were read from the exe with pefile. What is not proven is marked **uncertain**.
Related documents: INSTANCE.md (base class), EVENTS.md (engine → VM), PERSO_JUMP.md §2/§3 (attack),
BONUS.md (types 30..40, 120/121), ENEMY.md (types 4..13).

## 0. Summary

1. **The attack only hits NPCs.** The target loop `0x457ceb` iterates over `0x4c5258[0x4c5324]`; that list is only filled
   by `0x40c0b0` and its only callers are the Perso (`0x44b731`) and the enemy classes (`jmp 0x40c0b0` at `0x41198a`,
   `0x412d6a`, `0x414484`, `0x41604a`, `0x417f3a`, `0x41946f`, `0x41a4d0`). Non-enemy classes have only 28/29 vtable slots
   (no `vtbl[39]` TakeDamage). There is **no** "Perso hits instance" callback, no Press variant, and in W1A no
   msgmask: W1A's `code` contains 0× `MSGTEST`, 0 collisions, only `VOL_FLAG3/4/5`.
2. **"Peck switches" are scripted work**: volume (PersoIn) + game message **1050** (key 6 *released*) + game message **1042**
   (on the ground, state 0, within `dist` of the switch's **vector marker** and look direction within `angle`° of that
   vector) → script variable = 1 and the engine brakes the just-started charge run (`0x458e40`). The script does the rest (PlayAnim 3,
   sound, fade, camera, lasers off, blocks up). Doors use exactly the same pattern with 1040 (action 17/18) + 26.
3. **"Climb blocks" are engine work**: a press node with **type code 4** (`flags & 0xff00 == 0x400`) is a peckable surface.
   The attack ray hitting such a node → substate 8 → Perso state 4 (`0x4651d0`): Woody hangs with his beak in the wood
   and climbs 250 units/s upward as long as the player keeps tapping the attack button; at the top, the vector marker (type code 0) of the
   instance is used to climb onto it. W1A: instance 495 (model 51) and 52 (model 11), no script.
4. **The lasers of W1A are type 51** (8×, model 33). The model contains only the housing; the beam is an **engine-drawn
   primitive**: class "lazer" (`'Too many lazers in this level max is : %d'`, `0x46e4a0`), draw function `0x46e530`, additive
   camera-facing quads (core width 6, glow width 30, texture bank 0 image 1) between the marker's start point and
   `begin + direction · length` (message 52), shortened by a raycast. On/off = message 50. Hit = `Perso->vtbl[38](2)`
   (electrocution, instant death). Our port draws nothing because there's nothing in the `.ins` model to draw.

## 1. Peck interaction

### 1.1 What the attack controller itself tests (`0x457a50`, PERSO_JUMP.md §2/§3)

| path | address | test | consequence for non-enemies |
|---|---|---|---|
| target loop | `0x457ceb..0x458b96` | only actors in `0x4c5258` (see §0 point 1), `t != perso`; swept circle `0x433920` (peck dash) or beak segment against cylinder `0x433de0` (charge run); hit → `t->vtbl[39](…)`, if true `t->vtbl[38](3)` | **none**: bonuses, lasers, launchers, type 70/90 and base instances are not in the list |
| target finder | `0x4632e0` | all world instances with `vtbl[4]() != NULL` and type-word bit 0x400, 3D distance < 500 | aiming only (auto-aim); the type word of 42/50-52/20 doesn't have bit 0x400 (`0x45224d`: `\|= 0x26`, `0x450dbc`: `\|= 5`, `0x4528d0`: `\|= 0x44`) |
| attack ray, air | `0x4575b0` | ray `pos+(0,5,0)` → `+ n·50`, then `+ n_xz·100` (`0x4359b0`); hit kind 2 and **node type code 4** of press node `[0x53a58c]` of instance `[0x53a560]` | `0x464e00(p, inst)` → **substate 8** → Perso state 4 (§1.3); otherwise wall bounce 6/7 |
| grabbing from the ground | `0x464ef0` → `0x464f42` | action 6 *just pressed*, `atk ∈ {0,10}`, `p+0x50c == 0`, `p+0x524 < 0`; ray along `M.dir` over `p+0x114 + 100` = 169 | same → `SetState(4)`, `p+0x50c = 1` |
| picking up | `0x463430` | action 6 just pressed, on the ground, object from `0x5e4880[]` (bombs type 40) with `+0x131 != 0`, `+0x133 == 0` within `P+4 + 200` | `SetState(6)`; not in W1A (no type 40) |

`0x4305c0` (vtable slot 11, sets msgmask 0x20 on an instance; EVENTS.md §4.1/§6.1) has **no caller** in the entire exe:
the 19 places with `call [reg+0x2c]` are DirectDraw/COM (`0x4266e3`, `0x42674a`, `0x495e04`), the menu (`0x446588`), sound
(`0x467797`…`0x46822d`) and runtime (`0x47eeb4`, `0x48ce0f`…); none of them operate on an instance. Dead code (uncertain whether an
indirect `mov reg,[vt+0x2c]; call reg` exists; not found in the world functions `0x497a30`/`0x497ed0`/`0x498440`).
In practice msgmask 0x20 is only set by chests (`0x451814`, by a bomb explosion, BONUS.md §7); 12 levels use
`MSGTEST` (incl. W2B 6×, on type 121), not W1A.

### 1.2 Peck switch = volume + 1050 + 1042

**Message 1050 `(var, mode)`** (`0x44548b`, table: byte table `0x44569c[id−1000]` → jump table `0x4455e4`; 1048 = `0x4454ff` action 0,
1049 = `0x445562` action 1, 1050 = action **6**): first `SetVar(var, 0)` (`0x44549d`), then on the input object `[0x5e6188]`:

| mode | function | meaning (record per action 12 B: `+4` f32 hold time, `+8` u32 counter \| bit 31) |
|---|---|---|
| 0 | `0x467400` | `counter & 0x7fffffff != 0` = **held down** |
| 1 | `0x467420` | `counter & 0x7fffffff == 1` = **just pressed** |
| 2 | `0x467440` | bit 31 = **just released**: `0x467370` clears bit 31 every frame and sets it when counter == 0 and hold time > 0 (hold time → 0) |

If true, `SetVar(var, 1)` (`0x4455bb`). **W1A uses mode 2 everywhere.** (The port had 1 and 2 swapped for a while; since
issue #21 the table above matches `src/main_engine.c` and `WOODY_SWLOG=1` logs every key test that yields 1.)

**Message 1042 `(inst, dist, angle, var)`** (`0x445269`), pseudo-C:
```c
SetVar(var, 0);                                            /* 0x445275 */
if (!perso->onGround /*0x44bcf0: +0x22c*/) return;
if (perso->state /*+0x21c*/ != 0) return;
float cosmax = cos(angle * (1/360.0f) * 6.2831855f);        /* 0x4aa42c, 0x4aa0f0: angle in degrees */
vec3 v[2];
if (!GetVector(inst, 0, v, 0)) GetVector(inst, 5, v, 0);   /* 0x42f6b0: marker node type code 0, otherwise type code 5 */
vec3 pp = *perso->vtbl[34]();                              /* position */
float d = sqrt((pp.x-v[0].x)^2 + (pp.z-v[0].z)^2);         /* XZ distance to the START point of the vector */
if (d > (float)dist) return;                               /* 0x445341: raw, not x0.01 */
vec3 m = normalize_xz(v[1] - v[0]);                        /* y = 0 */
vec3 f = normalize_xz(Mover_GetDir(&perso->M /*+0x388*/)); /* 0x445780 */
if (dot(f, m) <= cosmax) return;                           /* 0x440070 */
Perso_BrakeCharge(perso);                                  /* 0x458e40: only if atk == 9 or 10: charge 0, T = AnimLen(0x12,0)+AnimLen(0x12,1), LockMove, anim 0x12, atk = 11 */
SetVar(var, 1);
```
`GetVector 0x42f6b0(inst, typecode, out[2], n)`: looks in `model+0x54[model+0x50]` (marker nodes, kind 0x20) for the n-th node with
`flags >> 8 == typecode` (so sub-index 0), updates the matrices (`vtbl[2](1)`) and transforms the **two points** of the node with
the world matrix of that node (`[0x509adc]+0xa0[inst+0x5c + node − 1]`). Return value 1 = found.

So the test is **not** "faces the instance" but "faces the same way as the marker vector". In the switch models
the start point lies at the instance origin (model 28/31: points (0,0,0) and (0,0,−300) under a node rotation; model 41:
(0,0,0) and (0,100,0)), so the distance matches `inst.pos`, the direction does not.

**Status of the port** (issue #21): `case 1042` in `src/main_engine.c` now does this fully — marker vector (type code 0, otherwise 5),
XZ distance to the *start point*, the look direction against the *marker direction*, only for a Perso in state 0 (`player_state_free`)
on the ground, and if true `player_brake_charge` = `0x458e40`. The latter is the visible part: **without that brake Woody keeps the
700 units/s of the charge run and rams the switch instead of pecking it** (anim 0x12 IS the peck the player sees).
`WOODY_SWLOG=1` logs, per 1042, the distance, the angle, the Perso state, the attack substate and the outcome.
The charge run also brakes at a **ledge**: the sensor `0x44b2e0` (`p+0x234`) casts the endless ray `0x497a30` down to the
floor 40 ahead and fires when the floor there lies more than 86 lower (result type 3 = world polygon or 4 = press node,
`t > 3`); ported, see OBSTACLE.md §2. It does not react to walls: a charge run against a wall is stopped by the collision
only, so how far he gets into an instance is up to the instance collision (PERSO_MOVE.md §6.5).

**Sequence in the original**: releasing the button on the ground starts the charge run (atk = 9, PERSO_JUMP §2.2) in the
Perso update in `0x457330`; in the VM tick of the same frame 1050 sets the variable, the watcher sends 1042, and 1042 brakes the charge run
(anim 0x12 = the "peck" the player sees). One VM-tick delay between 1050 and 1042 is possible (STOREVAR wakes the watcher);
bit 31 is then already cleared but 1042 no longer looks at the key.

#### Concrete example: switch 279 (first one after the start; turns off the three lasers 196/197/198)

Instance **279** (slot 0x117, model 41: volume node ±200 local + marker type code 0, scale (0.25, 0.37, 0.89)), position **(1844, −820, −2283)**,
volume id `0x300001c` = VM volume **28** (world box x 1794..1894, y −997..−643, z −2357..−2209; the test point is Perso.pos + (0,71,0)), marker vector a = (1844, −820, −2283) → direction **(0, 0, −1)**. The lever is instance
**202** (model 35) at (1844, −899, −2371); the glow object **273** (type 70, model 38) hangs at (1845, −572, −2357).
Script object 279 (words 5937..6199):
```
init:  var52 = var53 = var44 = 0
run:   if var44 != 0: END
       if VOL_FLAG5 28:  SEND 1050 [var52, 2]                 ; player in volume (PersoIn sets bit 0x20): attack released?
       if var52 == 1:    var52 = 0; SEND 1042 [279, 500, 60, var53]
       if var53 == 1:    var44 = 1; var53 = 0
                         SEND 3    [202, 0, 1, 50]            ; lever: anim 0 forward in 0.5 s
                         SEND 1622 [202, sample 0, 100]
                         SEND 57 [273, 50]; SEND 56 [273, 100]   ; fade out glow 273 at 0.5/s
         +0.6 s          SEND 580 [285, 2]; SEND 520 [285, 0, 196]   ; cut to camera 285, looks at laser 196
         +1.6 s          SEND 50 [198, 0]; SEND 1628 [198, 0x1000019, 100]; SEND 1623 [198, 0x1000018, 100, 150]
         +2.1 s          same for 197
         +2.6 s          same for 196
         +3.6 s          SEND 580 [285, 2]; SEND 500 [285]    ; back to the follow camera
```
Test: stand on the ground in the volume (x 1794..1894, z −2357..−2209), look toward −Z (toward the lever), tap the attack.
The lasers hang at (647, −1439, 166), (355, −1667, 167), (648, −1893, 165) (see §2.1).

#### All 1042 users in W1A (15×; all with `dist 500, angle 60` except object 409)

| script-obj = inst | model | position | volume | marker direction (xz) | what the script does afterward |
|---|---|---|---|---|---|
| **279** | 41 | (1844, −820, −2283) | 28 | (0, −1) | lever 202, glow 273 off, lasers 198/197/196 off (`50 [.,0]`) |
| **309** | 41 | (2590, 894, 3727); volume x 2528..2652, y 804..983, z 3678..3775 | 53 | (−1, 0) | lever 203, glow 274 off, camera 310 → 311, **`3 [54,0,1,150]` and `3 [53,0,1,150]`**: the two pillars (model 12) at (2500, −1, 3149) and (3394, 0, 3149) rise over 1.5 s, sound 1635 |
| **399** | 41 | (−7904, 2023, −7198) | 128 | (1, 0) | lever 204, glow 275 off, camera 400 → 401, `3 [201,0,1,200]` (model 34) |
| 164 / 166 | 31 | (6178, 1797, −2282) / (5830, 1701, −7533) | 18 / 17 | (0, −1) / (1, 0) | **door**: `1040 [inst,17]`, door anim `3 [162,0,1,100]`, `26 [0, other door, 1]` (teleport), `1040 [other,18]`, door closed |
| 319 / 332 | 31 | (2499, 800, 2301) / (8799, 1003, 846) | 57 / 66 | (0, −1) / (0, 1) | door pair (doors 81 / 111) |
| 321 / 397 | 31 | (−1636, 994, −7492) / (−8047, 1001, −7234) | 59 / 127 | (−1, 0) / (0, 1) | door pair (200 / 220) |
| 334 / 336 | 31 | (9084, 1302, −4123) / (11001, 1001, −5956) | 68 / 70 | (−.71, −.71) / (0, 1) | door pair (116 / 269) |
| 356 / 357 | 31 | (8318, 1902, −4127) / (10998, 2105, −5922) | 90 / 93 | (.71, −.71) / (0, 1) | door pair (117 / 268) |
| 489 | 31 | (−8694, 1006, −9778) | 213 | (0, −1) | end door: `1040 [489,17]`, door 219, **`1083`** (EndLevel) |
| (409) → inst 144 | 28 | (5760, 1703, −7527) | 137 | (−1, 0) | `dist 50000`: only the look-direction test, selects parameters `1110` of the side camera |

Model 31 has its marker on **type code 5** (node flags 0x520): that's the fallback `GetVector(inst, 5, …)` in 1042.
The rideable object type 20 (§2.6) is also "activated" this way: `VOL_FLAG5 61` + `1050 [var62, 2]` → `SEND 40 [323]` (script object 324).

### 1.3 Peckable climb wall: substate 8 → Perso state 4 (`0x4651d0`)

Conditions to grab, `0x464e00(p, inst)` (PERSO_JUMP §2.4): `p+0x524 < 0`, `p+0x26c == 0`, wall normal `|n.y| ≤ 0.05`
(`0x4ab7d0`); then `p+0x510..0x518 = normalize(n.x, 0, n.z)`, `p+0x51c = inst`. The ray (`0x4359b0`) tests the **press nodes**
(kind 0x01) of instances; "peckable" = type code 4 of that node (`0x4576ce` in the attack ray, `0x46553d` in state 4: `(node.flags & 0xff00) == 0x400`).
Entry: from the ground `0x464f42` → `SetState(4)`, `+0x50c = 1`, `+0x520 = 0.8`; from the air via attack substate 8
(`0x458524`, after `AnimLen(0xf,0)`): `SetState(4)`, anim 0x15, `+0x50c = 2`, `+0x520 = 0.8`. In both cases the Perso looks
at the wall (`RampA.dir = M.velDir = M.dir = −n`).

State 4, every frame (`0x4651d0`, called from `0x44b847`; jump table `0x465728` on `+0x50c − 1`):
```c
bool left = Held(0), right = Held(1), tap = JustPressed(6);          /* 0x467400, 0x467400, 0x467420 */
if (p->grip /*+0x520*/ > 0) p->grip -= dt;                            /* dt = p+0x2f8 */
if (tap && p->grip < 0.5f) p->grip = 0.5f;                            /* 0x465259: tapping holds him on */
if (p->grip <= 0 && p->sub /*+0x50c*/ != 3) p->sub = 4;               /* letting go */
switch (p->sub) {
case 1: Anim(0x14); p->sub = 2; p->peckT /*+0x528*/ = 0.3f; return;    /* 0x4652a0 */
case 2:                                                                /* 0x4652d3: climbing */
    Anim(0x15);
    p->disp.y /*+0x208*/ = p->[+0x184] /*P+0x74 = 250*/ * dt;          /* ALWAYS upward, no input needed */
    side = normalize_xz(cross((0,1,0), p->wallN /*+0x510*/));          /* 0x43ff80, 0x41af10 */
    if (!p->[+0x4ec] /*side-camera mode*/) {
        float s = p->[+0x188] /*P+0x78 = 300*/ * dt;
        if (left)       { a = Analog(0) * s; p->disp.x = -side.x*a; p->disp.z = -side.z*a; }   /* 0x467460 */
        else if (right) { a = Analog(1) * s; p->disp.x =  side.x*a; p->disp.z =  side.z*a; }
    }
    p->disp.x -= p->wallN.x * dt * 200.0f;  p->disp.z -= p->wallN.z * dt * 200.0f;   /* 0x4aa164: pressed against the wall */
    from = p->pos + (0, 40, 0);                                        /* 0x4ab294 */
    to   = from + normalize_xz(Mover_GetDir(&p->M)) * (p->[+0x114] + 100.0f);        /* 69 + 100 */
    Ray(&from, &to, -1);                                               /* 0x4359b0 */
    if (hitKind == 2) {
        if (typecode(hit press node) != 4) { p->sub = 4; return; }     /* 0x465549 */
        if ((p->peckT -= dt) <= 0) { p->peckT += 0.3f; Spark(0, from + (to-from)*frac*0.95f, &n); }   /* 0x479c80, 0x4a9c9c: §1.6 */
        return;
    }
    /* ray no longer hits anything: top */
    if (hitKind == 0 && GetVector(p->wallInst /*+0x51c*/, 0, v, 0) && fabs(p->inst.pos.y /*+0x10*/ - v[0].y) < 50.0f) {
        p->[+0x538] = p->[+0x53c] = AnimLen(0x17, 0);  p->[+0x540] = 0xf;  Anim(0x17);  p->sub = 3;   /* 0x465622 */
    } else p->sub = 4;                                                 /* hitKind 1 (world) also goes here: 0x465510 */
    return;
case 3:                                                                /* 0x465670: climbing over */
    done = ((p->[+0x53c] -= dt) <= 0);
    if (done) { SetState(0); p->sub = 0; }
    Perso_RootMotion(p, done);                                         /* 0x44e290: follows the root motion of .ins anim +0x540 = 15 */
    return;
case 4: p->[+0x524] = 2 * AnimLen(0x16, 0); Anim(0x16);                /* 0x4656c5: falls off, then no regrab for 2x that duration */
case 5: SetState(0); p->sub = 0; Jumper_ForceFall(&p->J, 0); return;   /* 0x4656e8, 0x463170 */
}
```
`hitKind == 1` (world) takes the same path as 0 (`0x465510`: `jne 0x4655ec` on `!= 2`), so a world wall in front counts as "the top"
and lets go unless the marker is within 50. `0x44e290` is §1.5. The displacement `+0x204..0x20c` is cleared at the start of every
Perso frame (`0x44b662`), so sub 1, 3, 4 move nothing and sub 2 without left/right only presses against the wall. **State 4 runs
the full collision**: the dispatch calls `0x4651d0` and then `Perso_MoveCollide 0x4624f0` (`0x44b834 → 0x44b857`, PERSO_MOVE.md §6.1),
plus the crush test `0x462a40`; only `Perso_Move`/the Jumper are skipped. So the wall he climbs stops the 200 u/s press at the body
radius (69 from the face), a side wall or a neighbouring press node stops the sideways climb, and near the top the cone bottom of the
body (PERSO_MOVE.md §6.5) lets him lean over the edge: at wall 495 the climb-over starts about 43 closer to the wall (z 1879 instead of
1836) and ends at z ≈ 1990 on top. A non-peckable press node in front sets sub 4 but that frame still moves; the let-go runs next frame.

Port (`src/player.c` `climb_update` + `move_collide`): as above. Test: `WOODY_TAP=1 W1A --pos 500 -990 1780 --yaw 0 --peck 1.0 0.15`
(climb and over the top at ≈ 9.5 s), sideways `WOODY_KEYS=1.6:LEFT:3.0` (slides along a neighbouring press node near x 690, lets go past
the node's edge). At more than 250 fps the first climb frame rises less than 1 unit and the ground probe of `0x4624f0` puts him back on
the floor, as it would in the original; he gets away on the first longer frame.

**W1A climb walls** (no script; objects 52 and 495 are empty):

| inst | model | position | peckable node (world bbox) | top marker (type code 0) | note |
|---|---|---|---|---|---|
| **495** | 51 (node 4 = `0x401`) | (514, 0, 1905) | x 247..747, y −1000..1000, z 1905..2105 | (513, 1000, 2106) → +z | first climb wall; glow 486 (type 70, model 49) at (510, −791, 1891). Peck the face z = 1905 (player looks toward +Z) |
| **52** | 11 (node 2 = `0x401`) | (4130, 501, 3153) | x 3953..4303, y 1..801, z 2976..3328 | (4130, 802, 3328) → +z | next to the pillars 53/54 of switch 309; glow 487 at (3919, 161, 3147) |

Both models: texture group 78 (16×16, flags `0x00b20002` = additive glow) on the "peck here" faces (nodes 4..7 resp. 5..6;
drawn unlit since LIGHTING.md recipe 5 — the mark left by the pecking itself is §1.6),
a hull node and regular press nodes (type code 0) to stand on.

### 1.4 Camera while climbing

**Short answer: there is no climb camera.** State 4 does exactly one thing to the camera: the follow camera (mode 1) stays in
**behind mode** for the duration of the state (as if button 0xa is being held, without the fast 7·dt phase). The rest is the regular follow camera of CAMERA.md §3.

1. **The climb code doesn't touch the camera.** In `0x464e00`, `0x464ef0`/`0x464f42` (…`0x4651b6`), `0x4651d0`…`0x465727`, the
   attack substate 8 (`0x458524`…`0x458582`) and `SetState` `0x44c980` there is not a single reference to the CamMgr `[0x4c737c]` and not a
   single call to a camera function (`0x41dxxx`…`0x425xxx`; the two `0x41af10`/`0x42f6b0` calls are cross product and marker fetch).
   `SetState(n)` (`0x44c980`) only writes `+0x21c = n`, `+0x220 = previous`, and clears `+0x50c`, `+0x5f0`, `+0x5b4`, `+0x5cd`, `+0x6ac`.
   All CamMgr references in the Perso code live elsewhere (`0x44b567..`, `0x44c343..`, `0x44df7d..`, `0x44e5a0..`, `0x454200..`,
   `0x4562bd..`, `0x458d5a` (shake), `0x45910e..`, `0x45997c`, `0x459c71..`, `0x45a7d0`, `0x463eb9`, `0x464998`, `0x464b02`).
2. **The only link is `0x459090` (camera control, CAMERA.md §3.1)**, mode index 0, `0x4591ec..0x459224`:
   ```c
   st = Perso->state;                                   /* +0x21c */
   if (st == 1 || st == 4 || st == 8) {                 /* dec eax / sub eax,3 / sub eax,4 */
       p->behind /*CamMgr+0x3ac*/ = 1;
       ctl->savedDir /*ctl+0xc*/ = Perso+0x34..0x3c;    /* row 1 of the instance rotation = −look direction = +wall normal */
   } else p->behind = 0;
   ...
   if (p->behind) p->dir /*CamMgr+0x354*/ = ctl->savedDir;
   p->+0x2c /*CamMgr+0x364*/ = Jumper falls ? 1 : Jumper rises ? 2 : 0;
   p->+0x3c /*CamMgr+0x374*/ = st;                      /* written, but read by NOBODY */
   ```
   At the state change itself (`0x4590fb..0x459142`) something only happens for state 3 (first-person) and when leaving it; nothing for 4.
   The flag `p->flags & 4` (7·dt) is only set by the 0.5 s timer of button 0xa, not by state 4 → factor **3·dt**.
3. **The camera update reads no Perso fields.** `0x422170..0x424b30` (follow camera) contains no reader of `p+0x3c` (`CamMgr+0x374`, the
   Perso state) or `p+0x5c` (`CamMgr+0x394` = `Perso+0x458`), and no Perso/Game global at all (all absolute addresses in that range are
   constants, `[0x4c93b0]` world, `[0x53a554/8]` ray result, `[0x4c83a0..0x4c93ac]` look-point filter). `+0x50c`, `+0x510` (wall normal),
   `+0x51c` and `+0x4ec` are never read outside the Perso code. So the wall normal only reaches the camera indirectly: when grabbing on,
   `0x4650de..0x4651b3` sets `M.dir = −n`, `0x44bd00` builds the instance rotation from that, and `Perso+0x34` (= +n) becomes `p->dir`; `0x424760`
   negates it again, giving `F = −n` (toward the wall).
4. **What the follow camera then does** (`0x4231e0`, behind branch `0x423264`; CAMERA.md §3.4/§3.5b/§3.6), with `n` = wall normal (xz, length 1):
   ```c
   T = pos + (0,120,0);  L = pos + (0,140,0);
   D = T + n * 400;                                     /* C+0x280: 400 units away from the wall, straight behind Woody */
   move.xz = (D − P).xz * 3 * dt;                       /* 0x4a988c = 3.0 */
   move.y  = (T.y + 180 − P.y) * 6 * dt;                /* jumper is reset on grab (0x462c90 → state 2), so f296/f297 = 0:
                                                           no 1:1 co-rise and no sagging look point (drop springs back at ×0.94/frame toward 0) */
   Center_BehindArc (0x423ed0);  sphere r = 40, steps of 35 (0x422e30);  sightline veto (0x423a40);  breadcrumb trail on blocked P→T
   look point = L  (pos + 140)
   ```
   Woody climbs at 250 u/s; at equilibrium the camera-y lags `250/6 ≈ 42` units behind `pos.y + 300`, so it hangs ≈ 118 above the
   look point and looks **down/horizontal at Woody's back**, never upward. In a shaft shallower than 400 + 40, the sphere test pushes the
   camera against the back wall; it then stays at that xz spot and only follows in y.
   The **jumper reset is essential**: `0x4650ce` (from the ground) and `0x45852a` (every frame of substate 8) call `0x462c90`
   (`J+0x14 = 2`), and state 4 doesn't run `Perso_Move`/`Jumper_Update` (`0x44b834`: only `0x4651d0` + `0x4624f0` + `0x462a40`), so
   `CamMgr+0x364` stays 0 during the entire climb.
5. **`Perso+0x4ec`** is the flag "2.5D/side-camera mode" (PERSO_FRAME `altMode`, PERSO_MOVE "stuck to a plane", CAMERA_SCRIPT §4.2):
   set by message 1088 → `0x459960` (`0x4599d9`, together with `+0x4e8 = v` and the plane `+0x4f0..0x4fc`), cleared by the Perso reset
   (`0x44ad22`) and by `0x44dda0` (`0x44de44`, scripted action). Readers: `0x44b7be` (→ `0x459c70`, fills the mode-0x20 block
   `CamMgr+0x61c` every frame), `0x459982` (1088 again: only for cutting to mode index 5), `0x459ec5` (`0x459eb0` ClampToPlane), `0x45b29e`,
   `0x44dce9` (end of action 18) and `0x465387` (state 4: **no sideways climbing in the side camera**, since left/right is the movement axis there).
   So it's not a climb-specific field and it doesn't change the follow camera.
6. **Collision/sightline**: unchanged from CAMERA.md §3.6 (sphere r = 40 with push-out vector in steps of 35; veto of every step after which `N → T`
   is blocked or N falls outside the world; if `P → T` is blocked at the start of the frame, state 2 = breadcrumb trail
   `{P, Tprev, T, …}` with `u += 0.04`/frame; category 7 instances don't block). The ray `0x4359b0` tests world **and** instances
   (hit kind 1 / 2). During climbing, the breadcrumb trail is Woody's track up the wall: a camera that gets stuck thus automatically
   ends up back in the shaft via `Tprev` (= 120 above an earlier Woody position).
   Status of the port (`src/player.c` `camera_step`/`player_camera`, `src/main_engine.c` `cam_update`): sphere push-out once per frame and
   only in xz (`gel_push` + `ins_push`); veto and breadcrumb trail (`cam_ray_blocked`, `camera_breadcrumbs`) test the world and the
   instance press nodes, like `0x4359b0`; **no** "outside the world → P = Pstart", no `Center_BehindArc`; state 4 does **not** force behind mode
   (`player_camera` only gets the flag), and `climb_try` doesn't reset the jumper (after grabbing on from the air, `rising`/`falling` stays
   true in `camera_step` throughout the climb → look point 150 lower resp. y 1:1/frozen).
7. **Climb-over (sub 3)**: `0x44e290` sets `+0x550 = 1`, causing `vtbl[34]` (`0x44c030`) to return `&+0x544` (root position, §1.5) instead
   of `&+0x1f4`. `0x459090` fetches `p->targetPtr` freshly every frame, so **T and L follow the root motion** over the edge; the camera
   stays in behind mode until the last frame (`SetState(0)` at `0x465697`), then regular lazy-follow. No cut, no transition, no
   `0x44e5a0` (that only applies to state 5).

Uncertain: whether the shaft at wall 495 is world geometry or instance hull. Category 7 is class 80 (type word `0x27`, `0x451b28`; CAMERA.md §3.6).

### 1.5 Climb-over root motion `0x44e290`

`void Perso_RootMotion(Perso *p, bool last)` – shared by state 5 (scripted actions, CINEMATIC §6) and state 4 sub 3. Fully read
(`0x44e290..0x44e59d`). Matrices are 3×4 with row vectors (`v' = v·R + t`; rot at +0..+0x20, t at +0x24), `X·Y` = X first then Y (`0x4405e0(X, Y)`: `X = X·Y`).

Helper `0x42f7e0(inst, float phase, int anim, Mat34 *out, int withWorld)`:
```c
W = withWorld ? { row0 = inst[+0x28..]*inst.sx(+0x4c), row1 = inst[+0x34..]*sy(+0x50), row2 = inst[+0x40..]*sz(+0x54), t = inst.pos(+0xc) } : I;
model = inst->+0xf8;  frame = (float)model->anims(+8)[anim].nframes /*int at +0, 8 B/entry*/ * phase;
node = &model->nodes(+0x68)[model->first_top(+0x6c) − 1];                /* S+0x6c = first top-level node (1-based, FORMAT_INS §2 #6), not a node count */
while (node->flags(+0) != 0) node = &model->nodes[node->+0x80 − 1];       /* follow +0x80 (1-based) until the node with flags == 0 = skeleton root */
if (node->rotTrack(+0x74)) q   = RotKey(node, anim, frame);               /* 0x43a9c0 */
if (node->posTrack(+0x70)) pos = PosKey(node, anim, frame);               /* 0x43a590, linear */
if (neither) *out = W;
else *out = { R = rotTrack ? Mat(q) /*0x440370*/ : I,  t = posTrack ? pos : 0 } · W;
```

```c
void Perso_RootMotion(Perso *p, bool last)                               /* 0x44e290 */
{
    int act = p->action540;  if (act == −1) return;                      /* 0x44e2b4 */
    Mat34 A, B, E, Ainv, Q;
    RootNode(p, 1.0f, act, &A, 1);                                       /* 0x44e2cf: root at the LAST frame, in the world */
    RootNode(p, 0.0f, 1,   &B, 0);                                       /* 0x44e2e4: root of raw .ins anim 1 (idle), frame 0, model space */
    E = Inverse(B, 1,1,1) · A;                                           /* 0x440fc0, 0x4405e0: E.t = where the model origin (feet) ends up */
    Ainv = Inverse(A, p->sx, p->sy, p->sz);                              /* 0x44e339 (scale +0x4c/+0x50/+0x54) */
    vec3 l = E.t · Ainv;                                                 /* 0x44e33e..0x44e3d5: end point in the local frame of the end root */
    if (p->remain53c < 0) p->remain53c = 0;
    float f = (p->total538 − p->remain53c) / p->total538;                /* phase 0..1 */
    RootNode(p, f, act, &Q, 1);                                          /* 0x44e421 */
    vec3 q = l · Q;                                                      /* = 0 · B⁻¹ · Root(act, f) · W : foot point under the root at phase f */
    p->useRootPos550 = 1;
    p->rootPos544 = (1 − f) * p->pos(+0x1f4) + f * q;                    /* 0x44e4aa..0x44e4f4: extra linear blend with the standing position */
    if (!last) return;
    p->animctl(+0x494)->vt[2](1);                                        /* idle */
    vec3 d = { −E.r[2].x, −E.r[2].y, −E.r[2].z };                        /* 0x44e50d..0x44e532: row 2 (local z) of E, negated */
    Mover_SetDir(&p->M, &d);                                             /* 0x459ff0 */
    p->pos = E.t;                                                        /* 0x44e552..0x44e56c */
    p->useRootPos550 = 0;  p->action540 = −1;
    Perso_SnapToGround(p);                                               /* 0x462990: floor under pos + 43, onGround = 1, jumper reset */
}
```
Because `A = Root(act,1)·W`, the `A⁻¹·Q` terms cancel: `q(f) = (0,0,0)·B⁻¹·Root_local(act, f)·W`, and `E.t = q(1)`.

Called from state 4 sub 3 (`0x465670`): starts at `0x465622` with `total538 = remain53c = AnimLen(logical 0x17, 0)` (`0x436b90`),
`action540 = 0xf` (raw anim 15), `Anim(0x17)`; every frame `remain −= dt`; when `remain ≤ 0`: first `SetState(0)`, `sub = 0`, then
`Perso_RootMotion(p, 1)`; otherwise `Perso_RootMotion(p, 0)`.

Effects during sub 3:
* `p->pos` (`+0x1f4`) doesn't change (sub 3 sets no `disp`, and disp is cleared every frame at `0x44b662`; `0x4624f0` still runs with the zero
  displacement, so only a push-out could move him) and
  `inst.pos` (`+0xc`) also stays put: `0x44bf10(useRootPos)` skips the copy `+0x1f4 → +0xc` when `+0x550 = 1` and only sets the
  sphere center `+0x60 = rootPos + (0, +0x110, 0)` and the world cell (`0x4077f0`). **So the model is drawn at the climb position and the
  animation itself (root track of anim 15) carries Woody over the edge**; the W in the formulas is therefore constant.
* `vtbl[34]` returns `&rootPos544` → camera (T, L), sound and everything that queries "the position" follows `rootPos`.
* Last frame: `pos = E.t`, look direction `= −E.row2` (Perso instance looks along −z, PERSO_FRAME §2.4), placed on the ground. Because
  `rootPos(1) = q(1) = E.t` there is no jump in the camera position; the model doesn't jump because idle frame 0 (B) is exactly subtracted out.
* Difference from `0x44edb0` (end of cinematic, CINEMATIC §5): there B = anim **0**, phase 0; here anim **1**.

For the port: `ins_root_end()` (`src/level.h`) already does `E`; needed is a variant with phase (`q(f)`) and B = anim 1. Recipe:
`over_to = E.t`, `yaw_end = atan2(−E.r2.x, −E.r2.z)`… note the sign: `Mover_SetDir` gets `−E.row2` as the **look direction**; every frame
`cam_target = lerp(pos, q(f), f)`, model stays at `pos` with the root track of anim 15 active; at the end `pos = E.t` + snap-to-ground.
Uncertain: whether the port renderer already applies the root translation of animations (if not, draw the model at `q(f)` without blending).

### 1.6 Peck impact `0x479c80(kind, point, normal)` — peck hole and wood chips

The engine calls this effect at every spot where the beak comes down:

| caller | arguments | source |
|---|---|---|
| attack ray `0x4575b0`, on **every** hit (world or instance), before the peckable test | `(1, &hitpoint, 0)` — no normal | PERSO_JUMP.md §2.4 |
| climb loop `0x4651d0` sub 2, every 0.3 s while the ray hits a type-code-4 node | `(0, from + (to−from)·frac·0.95, &wallnormal)` | `0x4a9c9c`, §1.3 |

The function itself is decompiled in **PARTICLES.md §4** and ported (`game_peck_fx` in `src/main_engine.c`):

* **kind 1** (the attack ray, every hit): one yellow flash, bank 0 image 18, size 80, alpha 0.8, additive, **0.05 s**.
  No hole, no splinters, no use of a normal.
* **kind 0** (the climb, every 0.3 s): a 0.2 s emitter that throws **33 splinters a second** (image 25, size 10..15, on
  an arc 80 outward and 100 high, level and outward whatever the wall, 0.3..0.6 s), keeps the flash lit (50 a second)
  and leaves **one hole** per peck (8 a second for 0.2 s): image 26, half diagonal 15, lying in the wall (plane with the
  wall normal), tinted (0.8, 0.8, 0) in the blended path, at a random offset of +-10 along the wall, solid for 5..5.3 s
  and then fading in 4 s.

While climbing this leaves a column of small holes, one every ~75 units (250 units/s upward, a peck every 0.3 s). The
peck he uses to **grab on** (`climb_try`, via the attack ray) is kind 1: a flash only. `WOODY_FXLOG=1` logs every peck.
The glowing "peck here" faces of the climb wall itself are something different: those are model faces with texture-group
flag bit 1 (§1.3), which since LIGHTING.md recipe 5 (issue #5) are drawn unlit and additive.

(Before the decompilation the port drew a reconstruction here: a ragged multiplied "gouge" that stayed 20 s, tumbling
untextured chips on the projectile gravity and a sawdust puff; before that, the laser hit `0x46efb9` (§2.1). Both are gone.)

## 2. Classes

Common: all classes below except 41 and 90 fall through to the FadeInst handler `0x44e8f0` (56/57) and, in their
think step (`vtbl[3]`, from `0x42b400` for the instances of the frame's list `world+0x64` only, INSTANCE.md §4.1) call `0x44e810` first. `vtbl[4]` = `0x403fe0` returns the
type word `&inst+0x104` (bits 0-4 category, 5-9 subtype).

### 2.1 Types 50 / 51 / 52 — **laser** ("lazer")

| | 50 | **51 (W1A, 8×, model 33)** | 52 |
|---|---|---|---|
| alloc / ctor (`0x403bda`/`0x403c0f`/`0x403c44`) | 0x114 / `0x450cd0` | 0x118 / `0x450cd0` | 0x118 / `0x450cd0` |
| vtable (overwritten after the ctor) | `0x4a92ec` | **`0x4a9278`** | `0x4a9204` |
| Init `vtbl[1]` | `0x4510a0` (type word 0x25) | **`0x451290`** (type word 0x45, `+0x114 = 400.0`) | `0x4514f0` (0x65, `+0x114 = NULL`) |
| think step `vtbl[3]` | `0x450f20` | `0x450f20` | `0x450f20` |
| messages `vtbl[22]` | `0x451040` | **`0x4514d0`** (+52) → `0x451040` | `0x451610` (+53) → `0x451040` |
| build segment `vtbl[28]` (+0x70) | `0x4510c0`: infinite ray `0x435810` along the marker to the first hit, `rec+0x2c = 1` | **`0x4512c0`** | `0x451520`: marker-start → marker-start of target instance `+0x114` (message 53; without a target: own 2nd marker), `rec+0x2c = 2` |

Base Init `0x450da0`: `0x44e7c0` (fade init), `+0x88 = 2`, then `0x450dd0`: counts the marker nodes with type code 0 (`GetVector(this,0,·,n)`
until 0) → `+0x10c` = count, `+0x108` = array of 0x30-byte records; per record a **laser effect object** (0x40 B, `0x46e4a0(inst, index, 0x3c0)`)
at `rec+0x1c`. Record: `+0x00` start a, `+0x0c` end b, `+0x18` f32 (message 51), `+0x1c` effect, `+0x20` hit normal, `+0x2c` 0 = free end /
1 = ends on geometry / 2 = to target.

Think step `0x450f20`: `0x44e810`; nothing if pause byte `[0x5e48cc]` or `+0x110 == 0`; otherwise per record `vtbl[28](i)`, the hit test
`0x450f80(i)` and `effect->byte[8] = 1` ("draw me this frame").

Type 51 segment `0x4512c0(i)`: `v = GetVector(this, 0, ·, i)`; `dir = normalize(v[1] − v[0])`; `a = v[0]`; `b = a + dir · (+0x114)`;
`Ray(&a, &b, −1)` (`0x4359b0`); on a hit (`[0x53a554] != 0`): `b = a + (b − a)·[0x53a558]`, `rec+0x2c = 1`, normal from `0x4b3108..`;
otherwise `rec+0x2c = 0`. So the segment follows the (possibly moved-by-path-follower) emitter every frame and stops at walls AND instances.

**What the beam ray tests (checked line by line, ported).** Type 51 calls `0x4359b0(&a, &b, −1)` (`0x4513aa`), type 50 the endless variant
`0x435810(&a, &(a + dir), −1)` (`0x45118c`, `dir` normalized, so `[0x53a558]` is the distance; `rec+0x2c = 1` is written whether or not
anything was hit, `0x45122d`), type 52 casts no ray at all. Both rays go to the world query (`0x497ed0` segment, `0x497a30` endless) and
translate its raw answer the same way: 3 = world polygon (kind 1, plane from `0x4c4bc0`), 4 = **press node of an instance** (kind 2, node
`[0x4c4be0]`, instance via `[0x4c4c0c]+0x40`), 2 = kind 3 without plane or instance. The third argument −1 is **the start cell** (`0x497fb0`
at `0x49802b`: −1 → look it up with `0x408180`), not an instance to leave out: nothing is skipped, not even the laser's own model. The
instance part walks, per visited cell, the instances linked into it (`cell+0x40/+0x44`, i.e. not hidden by message 6) and then the dynamic
list `0x4c3bb4[0x4c4bec]` (EVENTS.md §3.1); every instance answers through its `vtbl[5]` with its press nodes (kind 1). **Woody and the
enemies are not hit**: their models have no press node (W1A model 0 and 42), so the beam passes through them and only the separate hit test
`0x450f80` below reacts, and only to the player. Consequences seen in the port (`WOODY_FXLOG=1` prints `laser … stops on instance …`):
- The fences of class 50 end on the far post **of their own model**: W2D 41/42/43/444/445 (model 9, four beams each, 627..666 long),
  W3C 100..149 (model 5, ≈ 395), W2B 370/371 (model 39, 1279), K3A 120..131 (model 22) and 169/291 (model 31), W3D 1/2/3 and 853/854.
  Before, with the world alone, those beams went through the far post into the rock behind it (W2D: clearly visible).
- Moving platforms cut beams while they pass: W1B laser 150 by the shuttle (model 42), W3D 220/251/252/449 by the lifts of model 8.
- The own housing is not hit because the marker starts just outside it (W1A model 33: marker start z −62.9, tip of the press node z −60.4).
Port: `laser_segment` (main_engine.c) = `gel_ray_frac` + `inst_ray_press` (instance.c; the instance half of `0x4359b0`, the same test as
`player_ray_instances` in player.c but without its 4000-unit horizontal reject, so an endless beam also finds a post far away).

**Hit kind 3 and the stale distance (round 30, ported).** Raw answer 2 comes from exactly one place: `0x4330c0` in the instance
segment test `0x432ab0` (vtbl[5] of every instance, laser vtable `0x4a9278` slot 5) finds the START of the segment inside a press node
(behind all its planes) and writes `[0x53a554] = 3`, `[0x53a558] = 0`, `[0x53a560]` = the instance, `[0x4c4bd0] = 2`, t = 0 itself
(PROJECTILES.md §2.3); `0x4359b0` then leaves kind 3 (`0x435b4f`). For a type-51 beam `0x4513b7` only asks "anything?", so
`b = a + (b − a)·0 = a`, `rec+0x2c = 1` and the normal `rec+0x20` is copied from a stale `0x4b3108`: no beam, only the impact at
the marker start, and the hit test still runs on the zero-length segment. The endless test `0x431de0` (slot 6, used by `0x497a30`
for type 50) has no such answer. The endless ray `0x435810` writes `[0x53a558]` only for raw answers 3 and 4 (`0x4358a5`,
`0x435910`); for anything else (the ray leaves the world) `0x4511a2` builds `b = a + dir·[0x53a558]` with whatever value the last
writer left there (19 writers: every ray, GetHeight, the collision sweeps, `0x433920`/`0x433bc0` and the charge loop), and
`0x45122d` still marks the beam as ending on geometry. Port: `laser_segment` tests `inst_point_in_press` first for type 51, and keeps
the global in `g_hit_frac` (enemy.c), written by the laser rays, the two actor hit tests and the charge loop; the port's other rays
do not write it, so the stale length is the last of those (simplification). `WOODY_FXLOG=1` prints both cases.

Hit test `0x450f80(i)`: for each actor in `0x4c52d8[0x4c531c]` (list 1 of the previous frame, `0x40c080`: Perso only if
`+0x26c == 0` and state ≠ 5, `0x44b699`) with **category 1** (`0x40c340`, = the player; enemies are 2): `actor->vtbl[24](&cyl)`
(Perso `0x44cd60`: center = pos + (0, h/2, 0), radius = `perso+0x114` (= P+4 = 69), half-height h/2), then
`t = 0x433de0(&rec.a, &rec.b, &cyl.c, cyl.r · 0.85 /*0x4aa3d8*/, cyl.h)` (PERSO_JUMP.md §3.1; `cyl.h` is the half-height, so the y range is
feet + 0.1 .. feet + H − 0.1; ported exactly as `seg_cyl` in `laser_hits_player`, round 30); `0 ≤ t ≤ 1` → **`actor->vtbl[38](2)`** = `Perso::Kill(2)` (`0x44c110`,
PERSO_MOVE.md: lightning death, `+0x288 = 1.5 s`, ignored under invulnerability `+0x270 > 0` or cheat `[0x5d7b8a]`). No damage amount, no
knockback: touch = death. (Correction to INSTANCE.md §7: the radius in the test is that of the Perso · 0.85; `rec+0x18` from message 51
is not read here. No reader of `rec+0x18` found: uncertain.)

Messages:

| id | args | address | effect |
|---|---|---|---|
| **50** | on | `0x45108a` | `+0x110 = (on == 1)`: beam on/off (ctor: off). Off = no hit test and no drawing |
| 51 | v | `0x451059` | all `rec+0x18 = (float)v` |
| **52** | v | `0x4514e2` | 51 only: length `+0x114 = (float)v` (raw) |
| 53 | inst | `0x451622` | 52 only: target instance |
| 56 / 57 | v | `0x44e8f0` | fade of the housing (the beam doesn't look at that) |
| 6, 42..46, 1..5 | | base | hiding, path follower, animation |

**Drawing** — list `[0x5e82b4]` (linked via `fx+0x3c`), loop `0x46d078`: `if (fx->byte[8]) { Lazer_Draw(fx) /*0x46e530*/; fx->byte[8] = 0; }`.
Everything goes through the line primitive `0x471a10(this = [0x5e823c]+0xb00, flags)`: parameters `+0x278` p0, `+0x284` p1, `+0x290..0x29c` RGBA at p0,
`+0x2a0..0x2ac` RGBA at p1, `+0x2b0` half-width, `+0x2b4` texture ref. The function transforms both points to view space, takes the 2D perpendicular
of the projected segment and builds a **camera-facing quad** (flag 0x400 = own width, otherwise `[0x4b7aa0]` = 2.0; 0x800 = own colors,
otherwise (0.5,0.5,0.5,1); 0x200 = textured with surface `[0x5e866c] + (ref & 0xffff)·0x74` = **bank 0, image 1**), submitted with
`0x481560(…, 4, verts, tex, 0x24)`: flag 4 = **additive ONE/ONE** (HUD_TEXT.md §5), 0x20 = uncertain.

`Lazer_Draw`, per frame (dt = `[0x509adc]+0x38`):
```c
fx->phase /*+0xc, start = (float)inst->id*/ += dt * 127.75f;          /* 0x4abc8c */
g = 0.5f - 0.5f * costab[((int)fx->phase % 254 + 0x80) & 0x1ff];      /* table [0x5e823c][i] = cos(2*pi*i/512) (0x40248f); g pulses 0.5 -> 1 -> 0.5 over ~2 s */
if (rec.kind == 0 || rec.kind == 1) {                                 /* 0x46e71e: ends fade over 70 units (0x4abc88) */
    d = normalize(b - a) * 70;
    core  (color (1, .7, .7), width 6):  a..a+d alpha 0->1,  a+d..b-d alpha 1,  b-d..b alpha 1->0
    glow (color (1, .4, .4), width 30): same three sections with alpha 0->g, g, g->0
} else {                                                              /* 0x46e645: type 52, no fade */
    core a..b alpha 1 (width 6); glow a..b alpha g (width 30)
}
if (fx->flags /*+0x34 = 0x3c0*/ & 0x80) {                             /* pulse, 0x46ea1d */
    fx->acc /*+0x10*/ += rand01() * 0.8f * dt;  if (acc > 1) { acc -= 1; s /*+0x14*/ = 0; }
    if (0 <= s && s < 1) { s += 4*dt; if (s < 1) two quads around a+(b-a)*s, each 0.1 of the length, color (1,.6,.6), alpha g in the middle -> 0, width 25; else s = -1; }
}
if (fx->flags & 0x100) {                                              /* lightning arc, 0x46ebd4 */
    every 2 s (+0x18, start rand*2) a burst of 0.9 s (+0x1c); at 0 / 0.3 / 0.6 s, N = length*0.02 points (>= 1600: 32)
    with a random lateral offset +-25 (0x47d160, pool 0x5e82a8, max 100 lasers) are regenerated; polyline without texture (flags 0xc00), width 3
    details (0x46ec1f..0x46efa8): N = (int)(length*0.02), step = length/N; point i lies at a + w*step*(i+1) + u*ox + v*oy (ox, oy = rand*50-25,
    in the ring of 32 points per laser); base 0x46d320: w = direction, u = norm(w.z, 0, -w.x), v = w x u (for a near-vertical beam
    |w.x|, |w.z| < 0.001: v = norm(0, w.z, -w.y), u = v x w). Color white (1,1,1) at both ends, alpha = one rand01 per frame
    for the whole arc (flickering); only for rec.kind == 0 is alpha 0 at a (i = 0) and at b (i = N-1)
}
if ((fx->flags & 0x200) && rec.kind == 1) {                           /* impact at b, 0x46efb9 */
    sprite bank 0 image 5 (ref 0x10005) at b, color (1, .4, .4), size 60 + r, alpha 0.2 + r*0.01, r = rand*50 regenerated every 0.1 s; 4x 0x470f10(2)
    with S+0x230 = (.7,.7,0), (-.7,.7,0), (0,.7,.7), (0,.7,-.7): flag 2 = own color, without flag 1 the quad lies in the plane with that normal
    (0x4717d7), so four crossed planes, additive. The "sparks 50/s" loop (+0x28) only calls rand()%0x55 and rand01 and discards both:
    the spawning was compiled away, there are NO sparks
}
```
Starting values (`0x46e4a0`): +0x0c phase = inst id, +0x10 = 0, pulse s +0x14 = -1, arc clock +0x18 = rand*2, burst +0x1c = -1, sub-phase +0x20 = 0,
regenerate +0x24 = 1, +0x28 = +0x2c = 0, r +0x30 = rand*50. The pulse: +0x10 += rand01*0.8*dt per frame; > 1 → -1 and s = 0; s += 4*dt;
core quads from a+(b-a)s to a+(b-a)(s±0.1), color (1,.6,.6), alpha g → 0, width 25, textured (bank 0 image 1). Ported in
`laser_fx_draw` (main_engine.c). Uncertain: flag bit 0x40 of `fx+0x34` (no reader in Lazer_Draw).

**Model 33** (the W1A laser): node 1 mesh 100 polygons, texture group 97 (128×128, `0x00ff0000` = opaque, no animation/scroll);
nodes 2 and 4 hull, node 3 press (type code 0), node 5 bbox, node 6 **marker type code 0** (the beam vector, 303 long). No mesh type code 2/5..8,
no blended group: **there is no beam in the model**, the script doesn't hide or fade anything either. That's why the port only shows the housing.

**W1A usage**:
- 196/197/198 (script objects 196..198): `1200 51`, `52 [.,350]`, `50 [.,1]`, +0.01 s `1620 [inst, 0x1000019, var27]` (3D loop sound).
  Beams (a → direction, 350 long, shortened by the raycast): 196 (609, −1462, 166) → −x; 197 (393, −1690, 166) → +x; 198 (610, −1917, 166) → −x.
  Off via switch 279 (§1.2).
- 249..252: `52 [.,2250]` + `43 [.,1,550,1]` (path follower back-and-forth, 5.5 s, 8 points), beam straight down from ≈ (10820, 1159, −7253);
  253: `52 [.,2250]`, scale 1.5, down from (10998, 1168, −7259). These five get their `50` later from other script objects (21× message 50 in W1A).

### 2.2 Type 42 — **launcher** (category 6) and type 41 — **missile**

Type 42: ctor `0x452140` (0x19c B), vtable `0x4ab148`, Init `0x452230` (type word 0x26, `+0x88 = 2`), think step `0x452780`, reset `vtbl[17]` `0x452260`,
instance messages = `0x44e8f0` (none of its own). Control via **game messages 1000..1004** (`0x444870`; each checks `vtbl[4]` and category == 6;
MESSAGES.md wrongly calls them "camera"):

| id | args | address | effect |
|---|---|---|---|
| 1000 | inst, target | `0x444d99` | `0x4522b0(1, 1.0, target)`: one shot |
| 1001 | inst, kind | `0x444c50` | `0x452330(kind)`: reset + `0x449070(kind, &this+0x108)` copies projectile template `0x5d7ba8 + kind·0x68` (0x68 B); ctor does kind 1 |
| 1002 | inst, n, v | `0x444c99` | `0x452360(n, v)`, jump table `0x4524fc` (20 parameters, see below) |
| 1003 | inst, target\|−1, count, t | `0x444d27` | `0x4522b0(count, t·0.01, target)`: `+0x170 = count`, `+0x174 = max(t, 0.2)`, `+0x178 = target`, `+0x190 = now + dt`, `+0x194 = now − t`, `+0x198 = 1` |
| 1004 | inst | `0x444ce4` | `0x452320`: `+0x198 = 0` (stop) |

Think step: if `+0x198`: on every transition of `floor((now − t0)/T)` (`0x499ede`) and at least `T − 0.2` s after the previous shot: `count--`, `Fire()` (`0x452560`),
`count == 0` → stop (count −1 = endless). `Fire`: `v = GetVector(this, 0, ·, 0)`; direction = `normalize(v[1] − v[0])`, or with aim flag `+0x199` (param 19)
and a target: `normalize(target.pos + (0, +0x144, 0) − v[0])`; `+0x108 = v[0]`, `+0x114 = direction`, `+0x140 = target`; projectile kind `+0x18c != 0` →
`0x4490a0(&this+0x108)` = free slot among the 200 projectiles `0x5d7d48 + i·0x104` (PROJECTILES.md) (`0x449130` init, update `0x4490f0`/`0x4493c0`, EVENTS.md §3.2);
kind 0 → bomb `0x44d5d0` + sound 0xe. Afterward, if `+0x17c` (param 7) is a valid animation: play once over `+0x180` s (param 8).
1002 parameters (n → field, scale; defaults from the ctor): 0 `+0x128` raw (1000), 1 `+0x124` raw, 2 `+0x134` and `+0x184` ×0.01 (5.0), 3 `+0x13c` int,
4 `+0x138` raw (20), 5 `+0x144` raw (150, aim height), 6 `+0x188` ×0.01, 7 `+0x17c` int (fire anim), 8 `+0x180` ×0.01, 9 `+0x120` raw (5),
10/11 `+0x12c`/`+0x130` ×0.01 (1.0), 12 `+0x148` ×0.001, 13 `+0x14c` ×0.1, 14/15 `+0x150`/`+0x154` ×0.01, 16/17 `+0x15c`/`+0x158` ×0.01 (0.7),
18 `+0x168` ∈ {2,3,4,0} (table `0x45254c`), 19 `+0x199` bool. Meaning of the fields in the projectile: uncertain (not traced).

Type 41: base ctor + vtable `0x4a9360` (`0x403b5d`), registered in the pool `0x5e8344[0x5e840c]` (max 0x32, `'vous avez depasse le nombre maximum de
missile (%d)'`). Init `0x4723f0` creates a smoke effect (0x34 B, list `0x5e8564`, draw function `0x475440`) on the marker nodes **type code 9**;
think step `0x4723d0` → `0x4724e0`: `pos = projectile+0xb0`, orientation from `projectile+0xbc` (`0x46d320`), `0x4077f0` (re-cell), smoke on.
`0x472530` (from `0x46d120`, level start) **hides all missiles** (`0x407850`); `0x4722f0` (from the projectile effect `0x4700e0`) picks the next one from the pool.
Model 52: five small mesh nodes (group 113, 32×32, opaque), bbox, marker type code 9. Model 6 (launcher): housing group 71, six double-sided
(polyflag 0x2) glow faces group 72 (`0x00b20002`), press node, four hulls, marker type code 0 = loop.

W1A: 26/27/28 at (1067, 1114, 2648) → −x, (−283, 1120, 2915) → +x, (1065, 1115, 3254) → −x and 194/195 at (5647, 3191, −7505) / (5308, 3193, −7518):
each `1002 [inst, 2, 140]` (27: 150) and `1003 [inst, −1, −1, 300]` = endless every 3 s along the loop. The 8 missiles are parked around
(−2159, 1500, −780) and at (−2118, −2139, −779); in the original they're invisible there.

### 2.3 Type 70 — **fade instance** (9× in W1A: models 38, 49, 48)

Base ctor + vtable `0x4a9124` (`0x403cea`), Init `0x44e7c0`, think step `0x44e810`, handler `0x44e8f0`; INSTANCE.md §5 describes it fully
(56 = fade target, 57 = speed, > 0.9 = non-collidable). No player interaction. Models: 38 = 68 polygons group 102 (16×16, `0x00cc0002`),
49 = group 111 (128×128, `0x00bf0002`), 48 = group 110 (64×64, `0x007f0002`): all bit 1 = additively blended with intensity byte 2 —
light cones/glow above switches (270..275, some on a path follower) and at the climb walls (486, 487). Already ported; nothing to do.

### 2.4 Type 90 — **environment particle volume** (3× in W1A: model 29 = just a volume node)

Base ctor + vtable `0x4a90ac` (`0x403d56`), base handler, think step `0x472560` (`'une instance d'environnement n'a pas de volume'`). Game messages
1501 `(inst, mode)` → `+0xfc` and 1502 `(inst, r, g, b, size·0.01, count)` → color `+0x110..0x118`, `+0x11c`, `+0x100`, reinit `0x472b30` (INSTANCE.md §7).
W1A: `1501 [.,1]`, `1502 [.,255,255,255,700,400]` at (8718, 1445, −1412), (4384, 1437, −7792), (−83, 1266, −7792), scaled 1.7..7.2. Purely visual
(particles inside the volume, `0x47e230`/`0x47e160`/`0x47e050`); particle kind per mode: uncertain.

### 2.5 Type 7 — shooting enemy (1× in W1A, model 45 at (3701, 4, 2671))

Npc/Enemy class, ctor `0x416ca0`, vtable `0x4a9dc0`, handler `0x414530`; see ENEMY.md §8 (subtype 4: radius 30, sight 1500 (W1A script: 800), hp 1, damage 1, reload 2.0 s, homing missile;
`vtbl[58]` `0x418820` = fire projectile from the marker). Peckable via the regular target loop (registers itself with `jmp 0x40c0b0`). Model 45 has
markers type code 1 and 0 (muzzle/attack vector).

### 2.6 Type 20 (and 21) — **rideable/fireable object** (2× in W1A: model 47)

> **Superseded by ROCKET.md** (full analysis; type 20 is ported). The sketch below contains two errors, see ROCKET.md §9.

Ctor `0x452850(type)` (0x194 B), vtable `0x4ab1b8`, Init `0x452890` (type 20: type word 0x44 + smoke effect on markers type code 9; type 21: 0x24; stores
starting position/rotation in `+0x134..0x160`), think step `0x452e10` (states `+0x128` 0..9, jump table `0x453708`), reset `vtbl[17]` `0x452ae0`, handler
`0x453730`: **40** → `0x452a50` (if state 0: find the Npc with category 1 and call `Perso::0x465740(this)`: only on the ground and in state 0 →
`SetState(8)`, `perso+0x6b0 = object`, attack cleared; object becomes non-collidable, state 1), **29** → reset, **55** `(1, v)` → `+0x114 = v·0.01` s,
`(2, v)` → `+0x11c = v`. Perso state 8 = `0x4657f0` (anims 0x36..0x3e, steered with the actions). The think step itself spawns a projectile/bomb
(`0x449070`, `0x44d5d0`), flies for `+0x114` s, explodes (`0x477060`) and hits actors with `vtbl[0xa0]`. W1A: 323 (8845, 1137, 185) and 326 (8138, 2014, −3080),
`55 [.,1,380]`, `55 [.,2,1500]`, activated by releasing the attack in volume 61 (script object 324) resp. volume 62 (script object 327). What it exactly represents (rocket/catapult): **uncertain**, not
decompiled. Model 47: housing group 108, two press nodes, markers type code 0 and 9.

## 3. Other unported classes (brief)

| type | what | key addresses |
|---|---|---|
| 40 | bomb: pool `0x5e4880[16]`, pickup (`0x463430`, Perso state 6), game message 1090 starts one, explosion radius 400 hits type 17 and chests | ctor `0x44d250`, vtable `0x4aac74`, handler `0x451820`, `0x44d5d0`, `0x44d6e0`, `0x44d650` (BONUS.md §7) |
| 120 / 121 | "exploding object (chest)": bomb explosion within r → fade out + **msgmask 0x20** (the only `MSGTEST 32` any scripts use); 29 = reset | ctor `0x451650`, vtable `0x4aafd0`, `vtbl[28]` `0x451770`, `vtbl[29]` `0x4517d0`, reset `0x451730` |
| 17 | breakable object for bombs (`vtbl[0xa0](&pos, r)`), smoke on markers type code 9 | ctor `0x40c3d0`, vtable `0x4a95dc`, Init `0x40c440`, handler `0x40c5e0` |
| 50 / 52 | laser variants (infinite to wall / to target instance), §2.1 | `0x4510c0`, `0x451520`, message 53 `0x451622` |
| 42 + projectiles | §2.2 and **PROJECTILES.md**; 200 projectiles `0x5d7d48` (0x104 B), templates `0x5d7ba8` (0x68 B), Press/UnPress by landing projectiles | `0x4490a0`, `0x449130`, `0x4490f0`, `0x4493c0`; visuals `0x4700e0` (missile), `0x46f8a0`, `0x470af0` |
| 60 | water volume (message 1506): **WATER.md**, ported in `src/water.c` | vtable `0x4a9194`, handler `0x474a40` (`0x403ca3`) |
| 80 | lightning rod of the thunderstorm (messages 54, game 1100/1101): **STORM.md**, ported in `src/storm.c` | ctor `0x451a90`, vtable `0x4ab0c4`, Init `0x451b10`, handler `0x451b50`, storm `0x451cc0` |
| messages 15..19 | texture frame/UV override per instance (INSTANCE.md §2); W1A: 16 12×, 18 10×; **not in `src/instance.c`** | `0x42d9c3`, `0x42da32`, `0x42daae`, `0x42db0f`, `0x42db86`, reader `0x47f290` |
| 1201 / 1202 | set/clear type-word bit 0x400 (attackable target), §3.1; **no level sends them**; ported (`enemies_msg1201`) | `0x403440` |

### 3.1 Messages 1201 / 1202 and the type word

Handler `0x403440` (game messages 0x4b0..0x514 via `0x401a3e`): `in = world->table40[arg0 & 0xffffff]` (as 1200), `tw =
in->vtbl[4]()` (`0x40348b` / `0x4034d3`), then `if (in && (arg0 & 1))` — **bit 0 of the instance ref** (`0x403496` / `0x4034de`:
`test byte [msg+8], 1`), so only odd slots are touched — `1202: *tw &= ~0x400` (`0x4034a3`), `1201: *tw |= 0x400` (`0x4034eb`).
`in` is tested after the virtual call and `tw` never: classes whose vtbl[4] is `0x4078b0` (returns NULL: the base vtable
`0x4aa31c` of types 41/60/70/90, and 100/110) would crash.

The type word is `inst+0x104` (vtbl[4] `0x403fe0`): bits 0..4 category (`0x40c360`), 5..9 subtype (`0x40c380`), 0x400 =
attackable. Values: Perso category 1 + subtype = its class (`0x44a336`); enemies 4..16 category 2 (`push 2` before each
`0x40c360`, e.g. `0x418a12`); bomb 0x23, bonuses 30/34/35/36/37/38 = 0x28/0x88/0x68/0x48/0xa8/0xc8, lasers 0x25/0x45/0x65, 42 0x26,
20 0x44, 21 0x24, 80 0x27, 120/121 0x29/0x49. Bit 0x400 is set only by the enemy PostLoad `0x419e30` (`0x419fdf`) and Reset `0x41a010`
(`0x41a17f`) and cleared every frame of the dead state of types 4..13 (`0x4193a2`, `0x417a03`, `0x415f66`, `0x412c92`, `0x4143a7`,
`0x411743`); bosses 14/15/16 keep it. Its only reader is the target finder `0x4632e0` (`0x463323`).

No `PUSH 1201` / `PUSH 1202` exists in the 28 level scripts (all SEND ids are constants; the only 12xx are 1200, 1250 and 1275),
and the exe never builds them, so in the shipped game "attackable" = a live enemy (or any boss).

**Target finder** `0x4632e0(pos, r)` (object Perso+0x604): over this frame's list `world+0x64` (at most 16 candidates, `0x463303`),
every instance with bit 0x400 strictly within `r` (3D, instance origin `+0xc` vs `pos`), bubble-sorted by distance; `0x463420`
returns the first. Callers, both with the feet `Perso+0x1f4` and r = 500: the charge-run aim `0x4579a0` (substate 9 except its
first frame, `+0x5fd` set at `0x457499` and cleared at `0x457b6c`; substate 10 every frame; only category 2 targets: turn the Mover
to it, `+0x5f0 = t`) and the peck dash start (substate 1, `0x457eac..0x457f96`: aim at its origin + 0.8·height for an enemy).
Port: `nearest_enemy` / `auto_aim` / `attack_update` in `src/player.c` over the enemy set, filtered on `game_enemy_thinks`; no
16-candidate cap.

## 4. Recipe for the port

**A. Peck switches and doors (`src/main_engine.c`, `src/player.c`)** — **ported** (issue #21); all 15 triggers of W1A now work.
Verification without game data: `tools/native/switchtest.c` (the header of that file has the build command) mechanically lifts
`case 1042` and `inst_vector` out of `src/main_engine.c` and drives the actual attack controller of `src/player.c` frame by frame.
1. `case 1048..1050`: mode 0 = pressed, **mode 1 = just pressed, mode 2 = just released**. Set the variable to 0 first, then to 1 if true.
2. `case 1042`: `var = 0`; require `player on ground` and Perso state 0 (not dead, no scripted hold, not in state 4/8); fetch the marker
   vector: new helper `ins_vector(inst, typecode, n, Vec3 out[2])` in `src/level.c` = n-th node with `kind == 0x20 && type_code == tc && sub_index == 0`,
   `ins_pose()` current, `out[k] = mat4_apply(&inst->node_world[node], model->points[node.point_base + k].pos)`; type code 0, otherwise 5.
   Test `dist_xz(player.pos, out[0]) <= dist` and `dot(normalize_xz(out[1]-out[0]), (sin yaw, cos yaw)) > cos(angle°)`. On success: if `atk == 9 || atk == 10`
   → brake (like `0x458e40`: `charge = 0`, `atk_t = AnimLen(0x12,0) + AnimLen(0x12,1)`, move-lock, anim 0x12, `atk = 11`), `var = 1`.
3. Verification: start W1A, walk to (1844, −820, −2283), look toward −Z, tap the attack → lever 202 moves, camera cut, lasers 198/197/196 go off after 1.6/2.1/2.6 s.
   Switch 309 at (2590, 894, 3727), look toward −X → pillars 54 and 53 rise.

**B. Climb wall (`src/player.c`)**
1. Have the attack ray (`0x4575b0` port) and a new ground test (`0x464f42`: attack just pressed, `atk ∈ {0,10}`, ray 169 along the look direction
   from the player) hit instance press nodes and return the type code of the hit node. Type code 4 + `|n.y| ≤ 0.05` + `regrip_timer < 0` →
   store `wall_n = normalize_xz(n)`, `wall_inst`, look toward `−n`; from the air first substate 8 (`AnimLen(0xf)`), then state "cling".
2. Implement state 4 following the pseudo-C in §1.3: `grip = 0.8`, tapping sets it to at least 0.5, `disp.y = 250·dt`, sideways 300·dt with left/right,
   `−wall_n·200·dt`, no gravity, forward ray from +40 height over 169; not peckable → fall (`regrip_timer = 2·AnimLen(0x16)`);
   no hit and `|pos.y − ins_vector(wall_inst,0)[0].y| < 50` → climb-over (anim 0x17; place the player at the end on the marker-vector start; the
   root motion of `0x44e290` is uncertain), otherwise fall.
3. Test: wall 495, face z = 1905 between x 247..747 (look toward +Z), top y = 1000; wall 52 next to the pillars of switch 309.

**C. Lasers (`src/main_engine.c`, `src/hud.c`/`src/render_gl.c`)**
1. `1200` with type 50/51/52: mark the instance as a laser, `on = 0`, `length = 400`, `has_fader = 1`. Instance messages: 50 → `on = (v == 1)`,
   52 → `length = v`, 53 → target instance, 51 → store.
2. Per frame per laser with `on` (even if the instance is hidden; the think step always runs): per marker type code 0: `a, dir` via `ins_vector`,
   `b = a + dir·length`, raycast against world + instance press nodes (`gel_ray_frac` + `inst_ray_press`, ported) → shorten, `kind = hit`. Hit test: segment against the player cylinder (center pos + h/2,
   radius 69·0.85, half-height h/2; reuse the `0x433de0` port of the charge run) → `player_kill(p, 2)` unless dead or invulnerable.
3. Draw after the 3D scene, before the 2D layer, next to the pickup sprites: new `hud_world_beam(a, b, half_width, rgba0, rgba1, image)` = camera-facing quad
   (perpendicular of the projected segment, or `cross(b − a, cam_pos − mid)` normalized × half_width), additive `GL_ONE, GL_ONE`, depth test on,
   depth write off, texture bank 0 image 1 over the full quad. Minimal: core (1,.7,.7,1) width 6 + glow (1,.4,.4,g) width 30 with
   `g = 0.5 − 0.5·cos(2π·(((int)(t·127.75) % 254) + 128)/512)`, ends fading to alpha 0 over 70 units (with additive: color × alpha). Then, to taste, pulse,
   arc and impact sprite (image 5) from §2.1.
4. Test: from the start (537, −1800, −2450) facing +z; three horizontal red beams 350 long at z ≈ 166, y = −1462 / −1690 / −1917.

**D. Small**: hide type-41 instances at level start (`0x472530`); add 15..19 to `src/instance.c` (done: INSTANCE.md §2); correct 1000..1004 in MESSAGES.md
(launcher, not camera) and the radius of the laser test in INSTANCE.md §7.

## 5. Open questions

1. The `hitKind == 1` branch in state 4 hasn't been fully read. (`0x44e290` is now fully read, see §1.5; the camera during climbing §1.4.)
2. Laser: colors/alpha of the lightning arc, the four `0x470f10` quads of the impact, flag bit 0x40 of `fx+0x34`, reader of `rec+0x18` (message 51), flag 0x20 of `0x481560`.
3. Launcher: meaning of the 1002 parameters in the projectile (`0x4493c0`), the three projectile visuals and the damage to the player.
4. Type 20/21: state machine `0x452e10` and Perso state 8 (`0x4657f0`) have only been traced at a high level.
5. Type 90: which particle for which mode (`+0xfc` 0/1/2).
6. Whether there's a one-VM-tick delay between 1050 and 1042 (watcher wake within the same tick) — not relevant to the port as long as 1042 doesn't look at the key.
7. `0x4305c0` looks like dead code; to be confirmed with a breakpoint in the original.
