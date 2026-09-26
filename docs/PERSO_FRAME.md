# Perso (player) and the per-frame pipeline – Woody.exe

Status: working document, updated incrementally. All addresses are VAs in `Woody.exe`
(imagebase 0x400000). Float constants were read from `game/Woody.exe` with `tools`/a PE parser.

Important up front: **`0x462c60` is an empty function (`ret`)** – a compiled-out debug log.
All `call 0x462c60` (including those with `printf`-style arguments like `'F/S : %f '`) do nothing.

## 0. Objects involved

| object | where | fields used in the frame function |
|---|---|---|
| **App** (`this` of `0x401ab0`, global `[0x4c2d00]`, ctor `0x401f00`) | | `+0` = mode (0 = game, 3 = ?), `+8` = input object (vtable[5](0x16) = key 0x16 pressed?), `+0x18` = message dispatcher (`0x4012a0/0x401250/0x401370`), `+0x1c`, `+0x20`, `+0x24`, `+0x28` = **World** (`[0x4c4c0c]`), `+0x2c` = **Game** (`[0x5d7afc]`), `+0x30` = HUD/menu object, `+0x34` = **Perso** (player, `[0x53a34c]`), `+0x44` = 0x388-byte object (ctor `0x44fa10`, flags `+0x384`), `+0x68`, `+0x70` (byte), `+0xe4`, `+0xf4` (flag byte; **bit 3 (0x08) = paused/menu**, bit 4 = ?) |
| **World** (`App+0x28`) | | `+0x38` = **dt (s) of this frame**, `+0x60` = instance count, `+0x64` = instance array, `+0xc0` = camera matrix/position for the renderer |
| **Game** (`App+0x2c`, `[0x5d7afc]`) | | `+0` = Perso*, `+4` = fader/transition object, `+0xc` = timer, `+0x10` = dt, `+0x14` = transition state 0..4, `+0x64` = mode object (`0x44f2e0`: `+0x120` in 2..3 ⇒ "cinematic/menu mode"), `+0x198`, `+0x1b0` = respawn position (vec3) |
| **Perso** (`App+0x34`) | struct 0x754, vtable `0x4aabc0` (43 methods) | see §3 |
| **Camera manager** `[0x4c737c]` | | `0x41fa30` = `&cam+0x140` (camera record; `+0x90..0x98` = position), `+0x138` = camera mode, `+0xc/+0x10/+0x14` = transition parameters, `+0x368` |

## 1. Frame pipeline `0x401ab0` (App::Frame)

Callers: `0x401615` (main loop) and `0x404e90`. `esi = App`. `bl = 8` = pause bit of `App+0xf4`.

| # | address | call | meaning |
|---|---|---|---|
| 1 | `0x401abb` | `[0x4c2dbc] = [0x4c2db8] = 0` | reset counters |
| 2 | `0x401ac7-0x401bb6` | `App+8->vtable[5](0x16)`; toggles `[0x4c2dc0]`; after that only `0x462c60` logs (bonus counts `[0x5e54e4..]`, `F/S = 1.0/dt`) | debug statistic (key 0x16); **does nothing in release** |
| 3 | `0x401bb9` | `[0x5e48cc] = (App+0xf4 >> 3) & 1` | global pause flag |
| 4 | `0x401bd9` | `0x40bf60(!pause)` | `[0x4b178c] = active`; copies the previous frame's registration lists: `0x4c5218[8]` (pairs obj,param, counter `0x4c5320`) → `0x4c52d8`/`0x4c531c`, `0x4c4d80[32]` (counter `0x4c5328`) → `0x4c5258`/`0x4c5324`, and resets the counters. Then loops over `0x4c4e00[0x4c5318]` (all registered actors): if `+0x10c` bit 0 is set ⇒ clear it and call `vtable[29]()` |
| 5 | `0x401be3` | `0x4018d0()` | frame counters `[0x509b2c]++`, `[0x509b30]++`, `[0x4c3bb0]+=2`, `[0x4c3bac]++`; on World: `0x42a810`, `0x42a7e0`, `0x4840d0`, `0x4843e0(0.3f)` (renderer/time preparation) |
| 6 | `0x401bee` | `0x4076d0([0x4c4c04])` | `obj+4 = obj+0` (keep the previous value; presumably a time/counter) |
| 7 | `0x401bf3` | `0x406ff0()` | `[0x4c4bec] = 0` (list 0x4c3bb4 of visible instances cleared) |
| 8 | `0x401c01` | `0x401940(App+0 != 3)` | if arg: `0x445ba0(Game, dt)` (Game sub-objects `+8` via `0x459090` and `+0x18` via `0x4571c0` tick – HUD/score). Always: copy the camera record (0x9c bytes) from `[0x4c737c]+0x140` to `0x4c2d08`, `0x42a680(level, &0x4c2d08, level+8, 0x41fa20(cam))` = **put the camera into the renderer**. If `App+0x44->+0x384 & 2`: `0x4883a0(App+0x20, cam+0x1dc)` |
| 9 | `0x401c06-0x401c63` | `0x40c350(Perso)` = `(vtable[4]()[0] >> 5) & 0x1f` = subtype; if 4 or 5 ⇒ `0x42a980(&campos, World+0xc0)` otherwise `0x42a980(&campos, 0)` | **visibility/kd-tree traversal**: the world groups to draw and the per-frame instance list `World+0x60/+0x64` (INSTANCE.md §4.1; `campos` = cam `+0x90..0x98`) |
| 10 | `0x401c68-0x401cbc` | loop over `World+0x64[i]`: instance with `flags(+8) & 0x20` and `(flags & 0x1f) == 1` and `inst+0xf8->+0x58 != 0` → `0x4c3bb4[n++] = inst` (max 0x3ff), `[0x4c4bec] = n` | list of "visible instances with skeleton/mesh" for the renderer |
| 11 | `0x401cc2` | if `0x44f2e0(Game+0x64)` (cinematic mode) ⇒ `0x468e20(Perso+0x4a4, 0, 0x3c, [0x5e48c8])` | stop the sound object in Perso (`0x468a30(...)`, clear flag &= ~2) |
| 12 | `0x401ce4-0x401d07` | if not paused and not cinematic: **`0x44b530(Perso, 1)` = Perso::Update** | player update (§2) |
| 13 | `0x401d0c` | if `App+0x68 == 0`: `0x44e690(Perso)` | see §1.1 |
| 14 | `0x401d1b` | if `App+0xe4 != 0`: `0x4846d0(App+0 == 0)`; if that is true ⇒ `0x404b60(App, 0.5f, 0x1a, 0, 0x20)` | transition/fade (0x404b60 = start fade, cf. message 1081) |
| 15 | `0x401d57` | `Perso->vtable[2](1)` | = `0x42e2b0` (base instance: per-frame animation/skeleton tick) |
| 16 | `0x401d69` | if not paused: `0x44b480(Perso)` | if `Perso+0x21c == 6 && Perso+0x690 == 0` ⇒ `0x463530(Perso)` |
| 17 | `0x401d78` | `0x42b400(dt)` | **Think** `vtbl[3]` of every instance of the list `World+0x64` built in step 9 (INSTANCE.md §4.1); kind 2 (`.lit` light) also `0x474a90` |
| 18 | `0x401d7d` | `0x44d820()` | for all objects in `0x5e4880[0x5e487c]`: `0x44d850(obj)` – if `obj+0x131` ⇒ `obj->vtable[2](1)` (tick), and more if not paused |
| 19 | `0x401d85` | `0x42abc0(World)` | render preparation |
| 20 | `0x401d91` | `0x42b380(World, Perso)` | render preparation with the player |
| 21 | `0x401d99` | `0x42b4e0(World)` | render (183 instr, calls `0x42c320`, `0x498830`) |
| 22 | `0x401da1` | `0x42ac10(World)` | **main render loop** (425 instr, `0x42b6c0`, `0x439540`) |
| 23 | `0x401da6` | `[0x509b2c]++` | frame counter |
| 24 | `0x401dbd` | not paused and `cam+0x138 != 8`: `0x44b4a0(Perso)` | Perso post-render update: if state 3 ⇒ `+0x100 = 100.0`, `0x44e7f0(1.0, 1)`; if `+0x268` ⇒ reset, `+0x100 = 100.0`, `0x44e7f0(0, 1)`; `+0x5cc = (0x42f6b0(Perso, 0, &Perso+0x59c, 0) == 1)` (ground test?); `0x44af90(Perso)` (the landing ring, PERSO_JUMP.md §5); `+0x248 = 0` |
| 25 | `0x401de2` | not paused: `0x42b450(dt)` | |
| 26 | `0x401def` | not paused: `0x42d2e0()` | 241 instr; calls `0x407790` (find world cell), `0x428ce0`, `0x4359b0` – **collision/cell assignment of moving instances** |
| 27 | `0x401dfa` | `0x46d040([0x5e823c])` | tick message-1500 subsystem (particles?) |
| 28 | `0x401e0b` | `0x46e0d0(pause)` | 136 instr, particle/effect system |
| 29 | `0x401e19-0x401e5a` | `a = !cinematic && (App+0x70 || cam+0x138 != 2)`; if `a || App+0x70`: (if `App+0x70`: `0x4484a0(App+0x30)`), `0x447210(App+0x30)` | draw HUD/menu object (`0x4484a0`: `+0xc = 2`, possibly `0x4618d0(+0x30)`) |
| 30 | `0x401e60` | `App+0x70 = 0` | |
| 31 | `0x401e6a` | not paused: **`0x4019c0(App)`** | **script VM tick** (§1.2) |
| 32 | `0x401e7e` | not paused: `0x4490f0(dt)` | (calls `0x4493c0`) |
| 33 | `0x401e9a` | not paused: **`0x4459c0(Game, dt)`** | Game transition state machine + respawn (§4) |
| 34 | `0x401ec2` | if `[0x5e48c8]` (sound manager) and (not paused or `App+0xe4 == 0`): `0x468ba0(dt)` | sound manager tick (list `+0x14/+0x18`, gate `[0x5e61a4]`) |
| 35 | `0x401eca` | if `App+0x44->+0x384 & 2`: `App+0x1c->vtable[7]([0x509adc]+0x64, [0x509adc]+0x60, 0)` | sound Update with the instance list (SOUND.md §2.2 step 3) |

### 1.1 `0x44e690(Perso)` (step 13)
If `Perso+0x21c == 0` (state IDLE) and `Perso+0x57c != 0`: `0x44dda0(0x49, &Perso+0x564, 0)` and
`0x44c980(Perso, 5)` (**state := 5**). Then `Perso+0x100 = 10000.0f` and `0x44e7f0(Perso, 1.0 if
not cinematic otherwise 0.0, 0)` (`0x44e7f0(v, b)`: `+0xfc = v`, and if b: `+0x6c = v` – animation speed).

### 1.2 `0x4019c0(App)` – script VM tick
1. `0x4012a0(App+0x18)` (prepare the message queue), `[0x4c7384] = 0`, camera transition parameters
   `cam+0xc/+0x10/+0x14 = 0`.
2. **`0x442240()`** = VM: one tick for all threads (see docs/VM.md).
3. `0x444050((int)([0x509adc]+0x30 * 100.0))` – VM time in 1/100 s (constant `0x4a9010` = 100.0).
4. Messages: `n = 0x441d20()`; for i<n: `m = 0x441d30(i)`; id in 1200..1300 ⇒ `0x403440(App, m)`
   (world handler), otherwise `0x401370(App+0x18, m)` and if so `0x401250(App+0x18, m)` (dispatch to
   instance/game/sound, see docs/MESSAGES.md). `0x441d40()` = clear the queue.
5. Warnings (logs, no-op) if camera-mode parameters are set without a mode change.

## 2. Perso::Update `0x44b530(Perso, bool arg=1)`

### 2.0 Sub-objects embedded within Perso (no pointers)

| offset | size | ctor | role |
|---|---|---|---|
| `+0x110` | 0xe4 | `0x4631b0` (defaults) + `0x463280(column)` | **Parameter block P** – per character type, 31 floats from table `0x4b5f14` (31 rows × 5 columns, stride 0x14) followed by fixed defaults; see §2.6 |
| `+0x334` | 0x54 | `0x462c50`/`0x462c70(Perso)` | helper object "push/impulse" (`+0x28`, `+0x4e`; `0x463130` returns a displacement vector, `0x463160` a state (6 = ?)) |
| `+0x388` | 0x10c | `0x47fa30`, `0x459fd0(Perso)` | **Mover M** – horizontal movement controller (§2.3). `M+0` = input object, `M+4` = Perso, `M+8` = P |
| `+0x494` | ptr | `0x44ad90` → `new 0x54` (`0x463df0`) | animation controller (vtable: `[2](on)` on/off, `[3](dt)` tick, `[4]()` reset). `+0x498` second controller (may be NULL). `+0x49c`/`+0x4a0` = animation lengths of anim 3 and 0x42 (`0x436b90(anim, flag)`) |
| `+0x4a4` | | | sound source (`0x468e20/0x468e50`) |
| `+0x604` | | `0x4632c0` | (fields `+0x80/+0x84` = 0) |
| `+0x710` | | | counter/accumulator (`0x453ca0`: `+0x10 += dt`) |
| `+0x298`, `+0x2bc` | 0x24 | `0x436cf0`/`0x436d10` | two colliders (wall / ground) for `0x436d20`, `0x437040`, `0x436f00` |

### 2.1 Main function (pseudo-C, addresses between `/* */`)

```c
void Perso_Update(Perso *p, bool arg /* always 1 */)
{
    p->vy_corr = 0.0f;                                   /* +0x244  0x44b552 */
    p->dt = p->world->dt;                                /* +0x2f8 = [+0x2f0]->+0x38  (0x44baf0) */
    p->camPos = cam->pos;                                /* +0x2fc..0x304 = [0x4c737c]+0x140+0x90 (0x44c000) */
    if (p->respawnCountdown == 0) {                      /* +0x278 */
        ClearIdFlag(p->id, 0x10);                        /* 0x443e90: table [0x5d054c][id&0xffffff] &= ~0x10, wake watchers 0x442210 */
        p->respawnCountdown = -1;
    } else if (p->respawnCountdown > 0) p->respawnCountdown--;
    vec3 oldPos = *p->vtbl[34](p);                       /* 0x44c030: &+0x544 if +0x550 else &+0x1f4 */
    if (p->invulnTimer > 0) p->invulnTimer -= dt;        /* +0x238 */
    if (!p->frozen) {                                    /* +0x690 == 0 */
        Perso_LifeTimers(p);                             /* 0x44b220 (§2.2) */
        Perso_DecTimers(p);                              /* 0x44b1b0: +0x270, +0x280, +0x704 -= dt (down to 0) */
    }
    p->disp = (0,0,0);                                   /* +0x204..0x20c: displacement this frame */
    if (!p->frozen) {
        if (p->deathKind == 0 && p->state != 5)          /* +0x26c (0 = alive, STORM.md §4), +0x21c */
            RegisterActor(p, 0xa);                       /* 0x40c080: 0x4c5218[n] = (p, 10), max 8; copied to actor list 1 at the next frame start (0x40bf60) */
        if (!p->frozen) {
            p->accu.t += dt;                             /* 0x453ca0 on +0x710 */
            if (p->bonusCount >= 25) {                   /* +0x25c */
                Sound(4);                                /* 0x468a00([0x5e48c8], 4, 0) */
                if (p->health < 5.0f) p->health += 1.0f; /* +0x24c (0x4a9884 = 5.0, 0x4a900c = 1.0) */
                else { Sound(0); Perso_AddLives(p, 1); } /* 0x44c7a0: +0x250 += 1, in the savegame [0x5e5818] */
                p->bonusCount -= 25;                     /* 25 bonuses = 1 heart / extra life */
            }
        }
    }
    RegisterActor2(p);                                   /* 0x40c0b0: 0x4c4d80[n] = p, max 32 – actor-actor collision list */
    if (!p->frozen) { /* each step can set +0x690, retested every time */
        0x464ef0(p);   /* timer +0x524 -= dt … */
        0x465e50(p);   /* 96 instr; uses direction M+0x10, sound */
        0x457a50(p);   /* JUMP/ATTACK controller, sub-state +0x5b4 (0..11), 1192 instr (§2.5) */
        0x44ba70(p);   /* timers +0x5f4/+0x5f8 (+0x5fc flag); state 0 → 0x463430; then 0x457330 */
        0x465b10(p);   /* 232 instr; input + anim */
        Perso_CheckKey7(p);  /* 0x44b980 (§2.2) */
        0x458bf0(p);   /* 119 instr; input 0x467440, sound */
    }
    if (p->altMode) 0x459c70(p);                         /* +0x4ec: fly/swim mode, 174 instr */
    M_LoadParams(&p->mover);                             /* 0x45b0a0: copies P → M (§2.3) */
    bool doPost = true;
    if (!p->frozen) switch (p->state) {                  /* +0x21c, table 0x44b950 */
        case 0: case 6: Perso_Move(p, 1);  break;        /* 0x44bb20 */
        case 2: case 3: Perso_Move(p, 0);  break;        /* 2 = DEAD */
        case 1: 0x456210(p); break;                      /* sub-state +0x4a8, sound on +0x4a4 */
        case 4: p->fallAccum = 0; p->+0x382 = 0; 0x4651d0(p); break;   /* +0x35c; keys 0,1,6 */
        case 5: 0x44db50(p); doPost = false; break;      /* scripted animation (§2.4) */
        case 7: 0x44e1c0(p); doPost = false; break;      /* position follows object +0x55c (0x42f6b0), direction from it */
        case 8: 0x4657f0(p); doPost = false; break;      /* 233 instr, quaternion 0x440370 – rail/lift? */
        case 9: 0x454090(p); doPost = false; break;      /* results sequence, sub-state +0x724 (0..5), calls 0x44db50 (PERSO_STATE9.md) */
    } else if (p->state == 7) { 0x44e1c0(p); doPost = false; }
    if (doPost) Perso_MoveCollide(p);                    /* 0x4624f0 (§2.4) – for 0/1/4/6/2/3 */
    if (p->state == 1) 0x4567f0(p);                      /* 271 instr */
    if (doPost && bl) { 0x462a40(p); }                   /* 129 instr: 0x428ce0/0x4359b0 cell switch (only after the switch cases 0/6/2/3/1/4) */
    if (p->onGround) SetIdFlag(p->id, 0x200);            /* +0x22c → 0x443e50 */
    else             ClearIdFlag(p->id, 0x200);          /* script can read "is on the ground" */
    if (p->ridden && p->state != 6) 0x463c90(p);         /* +0x590 (object being ridden) */
    Perso_CheckSteep(p);                                 /* 0x44b2e0 (§2.2) */
    Perso_Orient(p);                                     /* 0x44bd00 → 0x44bd30 + 0x44bf10 (§2.4) */
    Perso_AnimState(p);                                  /* 0x463e60: animations per state (table 0x463f14), ticks controllers +0x494/+0x498 */
    vec3 newPos = *p->vtbl[34](p);
    if (oldPos.y - newPos.y > 0) p->fallAccum += oldPos.y - newPos.y;   /* +0x35c: fallen height */
    Perso_UpdateHUD(p, 0);                               /* 0x44ae60 */
    p->frozen = 0;                                       /* +0x690 */
}
```

### 2.2 Small helpers

* **`0x44b220` lives/timers**: if `state != 2`: `k = 0x463160(&+0x334)`; if `k == 6`: if
  `+0x5b4 == 0` and `fallAccum(+0x35c) >= P+0x7c (1500)` ⇒ `0x44d1b0(p, P+0xcc (0.8), P+0xc8 (0.5))`
  (knockback), `health -= P+0x8c (1.0)`; if health ≤ 0 ⇒ `health = 0`, `p->vtbl[38](p, 8)`
  (**fall damage: falling 1500 units costs 1 heart**); otherwise `+0x334->+0x28 = 0`, `+0x4e = 0`.
  Then: if `health <= 0` ⇒ `health = 0; vtbl[38](3)` (event 3 = die).
* **`0x44b980` key 7** (`0x467440(7)` = release edge): in state 3 (and cam mode ≠ 9) or key:
  state 3 ⇒ `0x44c9f0` (back to the previous state `+0x220`), `+0x268 = 1`; state 0 with
  `+0x5b4 == 0` or state 6 with `+0x58c == 2` ⇒ if `onGround` and cam mode 0: `0x464620(p)`;
  **state := 3** (`0x44c980(3)`); otherwise sound 9. State 3 is the key-7 **look-around** (first person, camera mode 0x200;
  `0x44b4a0` fades the Perso out there: `+0x100 = 100.0`, `0x44e7f0(1.0, 1)` = instance fade 1.0). Worked out in [PERSO_LOOK.md](PERSO_LOOK.md).
* **`0x44b2e0` ledge sensor** (full analysis: OBSTACLE.md §2): `+0x234 = 0`; if onGround: `d = normalize(M->dir(+0x10))`
  `* P+0x80 (40.0)`; endless ray `0x497a30(a = pos + (0, P+0 = 43, 0), dir = pos + d − a, -1)`, i.e. from the collision
  centre down to the floor point 40 ahead; if the result type `[0x4c4bd0]` is 3 (world polygon) or 4 (press node) and
  `t = [0x4c4bd4] > 3.0` ⇒ `+0x234 = 1`: the floor ahead lies more than 86 below the feet. Walls never set it (the
  collision radius 69 keeps them beyond the 40 look-ahead). Read only by the charge run `0x457abe` / `0x457c20`.
* **`0x44c980(p, s)` SetState**: if `+0x590` (ridden object) and state 6 → 0x463c90; `+0x220 =
  old state`, `+0x21c = s`, clears `+0x50c`, `+0x5f0`, `+0x5b4`, `+0x5cd`, `+0x6ac`.
* **`0x44c030` (vtbl[34])** = position pointer: `+0x544` if `+0x550` (alternative position, e.g. on
  a moving object) else `+0x1f4`.
* **`0x44c720` (vtbl[36])** = `state == 2` (dead). **`0x44c730`** = lose a life: `+0x250--`
  (unless cheat `[0x5d7b88]`), into the savegame `[0x5e5818]`, `0x443ff0(id)`, set id flag 0x10,
  `+0x278 = 2`; returns `lives <= 0`.
* **`0x44c110` (vtbl[38], `TakeHit(type)`)**: ignored in cinematic mode (`0x44f2e0/0x44f2d0`),
  when `App+0xcc` is set, for subtypes 4/5, or if a category-2 actor of subtype 0xc with
  `vtbl[36]()` active is present (shield/protection). Types seen: 3 (death), 8 (fall damage),
  9 (thunderstorm §4.2).
* **`0x44dda0(anim, &pos, flag)`** = start a scripted animation (`state 5`); returns 0 if dead.

### 2.3 Mover `M = Perso+0x388` (horizontal movement, `0x45b110(M, arg)`)

Layout M: `+0` input (`[0x5e6188]`, records of 12 B per action: `+0` analog value, `+8` status,
bit 31 = edge; `0x467400(k)` = held, `0x467420(k)` = status==1 (just pressed), `0x467440(k)`
= bit31, `0x467460(k)` = analog value), `+4` Perso, `+8` P, `+0xc` movement phase (0..6, `0x45ad30`),
`+0x10..0x18` **look direction** (xz, y = 0), `+0x1c..0x24` **normalized velocity direction**,
`+0x28/+0x30` input direction relative to the camera, `+0x34` Ramp A (walk speed), `+0x68` Ramp B (slide),
`+0x9c` Ramp C (push/knockback), `+0xd0..0xd8` **ground normal**, `+0xdc` sliding, `+0xe0` speed
(units/s), `+0xe4` = speed·dt (distance covered), `+0xe8` dt, `+0xec` push timer, `+0xf0` camPos,
`+0xfc` pos, `+0x108` flags: 1 = onGround, 2 = arg, 4 = slide-stop block, 8 = moving,
0x10 = no input, 0x20 = ?, 0x40 = inverted controls, 0x80 = close to the follow target `+0x68c`.

**Ramp** (0x34 B, `0x467110` reset, `0x467130` start accelerating, `0x467180` start decelerating,
`0x4671d0(dt, useMax)` tick): `+0` vec3 direction, `+0xc` current speed, `+0x10` target speed,
`+0x14` max speed, `+0x18` (= −P+0x24, unused as far as observed), `+0x1c` accel time, `+0x20` decel time,
`+0x24/+0x28` phase timers, `+0x2c` phase (1 accelerating, 2 at target, 3 decelerating, 0 stopped), `+0x30`
double-speed flag. Accelerating: `v = (t/T_acc)² · target`; decelerating: `v = max·(1 − (t/T_dec)²)`.
If `target < 0` (tick, `0x4671d0`): `v = target` directly.

`0x45b0a0` loads per frame: RampA: max = **P+0x1c (600 = walk speed)**, `+0x18 = −P+0x24 (−300)`,
T_acc = **P+0x2c (0.25 s)**, T_dec = **P+0x30 (0.1 s)**; RampB: max = **P+0x40 (600 = slide speed)**,
T = P+0x44/P+0x48 (0.25/0.25); RampC: max = **P+0x4c (500 = push speed)**, T = P+0x50/P+0x54 (0.1/0.5).

Per frame (`0x45b110`): `M+0xe8 = dt`; flag1 = onGround; `M+0xf0 = camPos`, `M+0xfc = pos`; follow
target `+0x68c`: xz distance < 200 (`0x4aa164`) ⇒ flag 0x80 off; ≥ 20 (`0x4a9994`) and (≥ 200 or not
flag 0x80) ⇒ `M+0x10 = normalize(target − pos)` (look direction towards the target). Then:
* flag 2 (arg=1, states 0/6): **`0x45a4b0` input**: x = analog key 2/3 (left/right),
  y = key 0/1 (up/down). Flag 0x40 ⇒ `0x45a200(−x, −y)` (direction relative to the current
  look direction: `M+0x28 = y·dir.x + x·dir.z`, `M+0x30 = −x·dir.x + y·dir.z`; target speed
  `M+0x44 = min(|v|,1)·P+0x20 (300)`). Otherwise: both 0 ⇒ flag 0x10, target speed 0. Otherwise
  `angle = π (0x4ab2d0 = 3.14116) − atan2(x, y)`; `camDir = normalize(pos − camPos)` (xz), rotated about
  the y-axis by `angle` (`0x440d40`: quaternion → matrix `0x440370`); target speed
  `M+0x44 = (dot(new, old)+1)/2 · P+0x1c (600) · min(|stick|,1)` (turning slows you down),
  `M+0x10 = slerp(old, new, |stick|·0.25 (0x4a9ca0))` (`0x45a320`, threshold `0x4aa41c` = 0.9999),
  y = 0, normalize; flag 8.
  With `+0x4ec` (altMode): **`0x45a7b0`**: keys 0/1 (mirrored if cam+0x61c == 1) with flags
  `+0x4ed/+0x4ee` ⇒ target speed P+0x1c or 0.
* otherwise `0x45a1f0` (clear flags 8/0x10/0x20), phase = 0.
* **`0x45a850` direction**: if `+0x308 == 1` (ground type slippery, PERSO_MOVE.md §6.4) and not flag 0x40 and target speed ≥ 0:
  `k = clamp(M+0xe0 / P+0x1c, 0, 0.95) · P+0x3c (1.0)`; RampA.dir = k·normalize(RampA.dir) + (1 − k)·normalize(M+0x10)
  (keeps k of the old direction, not normalised again; RampA T = P+0x34 (0.75)/P+0x38 (1.0)); otherwise RampA T =
  P+0x2c/P+0x30 and RampA.dir = `M+0x28` (flag 0x40) or `M+0x10`.
* **`0x45aa60` sliding**: `n = normalize(M+0xd0)`; if `n.y < 0.71` (`0x4ab2d8`, f64) and onGround
  and not already sliding: RampB.max = P+0x40, start accelerating, RampB.dir = (n × up) × n (downhill),
  `M+0xdc = 1`. If sliding and onGround (flatter): decelerate, `M+0xdc = 0`, reset RampA to M+0x10;
  not onGround ⇒ flag 4. Tick RampB.
* **`0x45acb0` push**: `M+0xec -= dt`; if `M+0xc8 == 2` ⇒ RampC decelerate; tick RampC; timer ≤ 0
  ⇒ RampC.dir = 0.
* **`0x45ad30` phase automaton** `M+0xc` (table `0x45ae2c`): 0 stopped → 1 (start accelerating) on flag 8,
  → 4 on flag 0x20; 1 → 2 while flag 8 stays; 2 → 3 (start decelerating) when flag 8 clears; 3 → 2 or 0;
  4 → 5/6; 5/6 → 1.
* **`0x45ae50`**: if `Perso+0x2e0` (ground type ≠ 0) ⇒ `0x4672d0(RampA, 0.25)`; tick RampA(dt, 1).
* **`0x45ae80` total**: `v = RampA.dir·RampA.v (only with flag 2) + RampB.dir·RampB.v +
  RampC.dir·RampC.v`; with flag 4: only A projected (`0x440070` = dot) ≥ 0; `M+0xe0 = |v|`,
  `M+0xe4 = |v|·dt`, `M+0x1c = v/|v|`.

### 2.4 Movement, collision and orientation

**`0x44bb20(p, arg)` Perso_Move**: if `0x465fe0(p)` (timer `+0x6f8`, mode `+0x6e4`) ⇒ done.
If `+0x474 > 0` and `+0x238 > 0` and `+0x5b4 == 0` ⇒ arg = 0 (no input during invulnerability flash?).
`0x45b110(M, arg)`; `d = M+0x1c · M+0xe4` (`0x44d1e0`); push from `+0x334` (`0x462d70(0x467400(4))`,
`0x463130`) if `+0x240 > 0` otherwise `+0x240 -= dt`; if `+0x238 > 0` ⇒ d = 0.
Then `disp(+0x204)` = `+0x5bc` if `+0x5cd` (**the jump controller supplies the displacement**), or
`+0x69c` if `+0x6ac`, or `d + push`. `disp.y += dt · +0x244`. `0x459eb0`: in altMode (`+0x4ec`)
disp is clamped to the plane `+0x4f0..0x4fc` (n·p + d ≤ |disp|).

**`0x4624f0(p)` Perso_MoveCollide**: `0x462490`; **`0x4627d0`**: for every other actor in
`0x4c5258[0x4c5324]` (copy of the previous frame): circle-circle push-out (`0x433d40`) with radii
`vtbl[32]()` and `vtbl[32]() − 5.0`, result added to `disp.x/z`. Radius `0x434820(P+4 (69))`
(halved in state 1 with `+0x694`). `+0x28c = pos` (old position). `newPos = pos + disp`;
wall collision `0x436d20(&+0x298, &newPos, &out, id)` and `0x437040(&+0x2bc, …)`; in state ≠ 2 the
correction is applied. `+0x200 = 0x437180(…, 80.0, 10.0, …)`; `+0x2e0 = [0x53a554]` (ground type,
2 if `[0x53a560]`), `+0x2e4 = [0x53a560]`. Ground test `0x436f00(&+0x298, -1, &pos, y+P+0, id)`:
`+0x224 = [0x53a568]` (ground height), `+0x228 = y − ground height`; **1 ⇒ `onGround(+0x22c) = 1` and
`pos.y = ground height`**, otherwise `onGround = 0`. `0x4628e0(p)`; `0x462760(p, 71.0)` → `0x4347b0`
(test at height y+71: ceiling/head). There is **no gravity** in this function: falling and jumping
are done by the jump controller (`+0x5cd/+0x5bc`) and `+0x244` (see §2.5).

**`0x44bd00` → `0x44bd30` Perso_Orient**: `+0x54 = +0x2e8` (scale z = scale factor). If
state ≠ 8: `f = normalize(M->dir)`, `f.x = −f.x; f.z = −f.z` (**the instance faces along −dir**).
State 1: up = `+0x458..0x460 · 0.1` filtered: `+0x210 = +0x210·0.9 + up` (`0x4a94b8` = 0.9,
`0x4a9008` = 0.1: low-pass on the "up" vector); otherwise up = (0,1,0). `right = up × f`
(`0x41af10` = cross product), `f' = right × up`; rotation-matrix rows: `+0x28 = right`,
`+0x34 = f'`, `+0x40 = up`. Then **`0x44bf10(+0x550)`**: instance position `+0xc = +0x1f4`
(or `+0x544`), `+0x60..0x68 = pos + (0, P+0 (43), 0)` (midpoint), `0x4077f0(&mid)` (find world cell).
If `+0x4b4` (linked second instance, e.g. shadow/vehicle): copy position, midpoint, rotation and
cell.

### 2.5 Jump/attack controller `0x457a50` (sub-state `+0x5b4`, 1192 instr – not fully read)

`+0x5cd = 0` on entry; `+0x5e0 -= dt`. Table `0x458bb8` (index `+0x5b4 − 1`):
1→`0x457eac`, 2→`0x458281`, 3→`0x458587`, 4→`0x4585cf`, 5→`0x45878c`, 6→`0x45841d`, 7→`0x4584e1`,
8→`0x458524`, 9→`0x457abe`, 10→`0x457c02`, 11→`0x457e78`. Sub-state 9 (`0x457abe`): if `+0x234`
(steep edge) and key 4 (attack) ⇒ `0x44cce0(p, 0, 1)`, reset anim, `+0x5b4 = 0`; otherwise
`+0x5b8 = animlen(0x12,0) + animlen(0x12,1)`, start anim 0x12, `+0x5b4 = 11`. Every path sets
`+0x5cd = 1` and computes `+0x5bc..0x5c4` = displacement from `dt · M->dir · …`. In `0x45848c`:
`+0x244 -= (pos.y − ground height[0x53a568]) · 2.5` (`0x4ab2bc`) – **spring correction towards the
ground**, added via `disp.y += dt·+0x244` in `0x44bb20`. String: `'Please get the latest version of
the Woody mesh with the Vector used for Attacks'` (`0x457a50` uses a mesh vector for attacks).
Other sub-states set `+0x5b4` to 1..11 at `0x4573f2..0x458ead` (see the writers list §3).

### 2.6 Parameter block P = `Perso+0x110` (table `0x4b5f14`; column per subtype: 1→0, 3→1, 2→2, 5→3, 4→4; `0x44a394`)

| P+ | Perso+ | columns 0,1,2 / 3,4 | meaning (where used) |
|---|---|---|---|
| 0x00 | 0x110 | 43 | height of the collision centre above the feet (`0x44bf10`, `0x44b2e0`); anim scale `0x4624f0` |
| 0x04 | 0x114 | 69 | horizontal collision radius (`0x434820` in `0x4624f0`; ½ in state 1 with `+0x694`) |
| 0x08 | 0x118 | 193 / 143 | ? (speeds) |
| 0x0c | 0x11c | 193,143,143 / 160 | ? |
| 0x10 | 0x120 | 61 / 81 | ? |
| 0x14 | 0x124 | 193 / 143 | ? |
| 0x18 | 0x128 | 61 / 81 | ? |
| 0x1c | 0x12c | 600 / 1250 | **maximum walk speed** (units/s) – RampA.max, `0x45a72b` |
| 0x20 | 0x130 | 300 | speed with inverted controls (`0x45a290`) |
| 0x24 | 0x134 | 300 | → RampA+0x18 as −300 |
| 0x28 | 0x138 | 1.8 | ? |
| 0x2c | 0x13c | 0.25 | RampA accel time (s) |
| 0x30 | 0x140 | 0.1 | RampA decel time (s) |
| 0x34 | 0x144 | 0.75 | RampA T when `+0x308==1` |
| 0x38 | 0x148 | 1.0 | idem |
| 0x3c | 0x14c | 1.0 | turn-in factor `0x45a8cc` |
| 0x40 | 0x150 | 600 | slide speed on a steep slope (RampB.max) |
| 0x44/0x48 | 0x154/0x158 | 0.25 | RampB times |
| 0x4c | 0x15c | 500 | push/knockback speed (RampC.max) |
| 0x50/0x54 | 0x160/0x164 | 0.1 / 0.5 | RampC times |
| 0x58 | 0x168 | 0.2 | ? |
| 0x5c | 0x16c | 0.6 | ? |
| 0x60 | 0x170 | 200 | ? |
| 0x64 | 0x174 | 380 / 400 | ? (jump?) |
| 0x68 | 0x178 | 650 / 1250 | ? (jump?) |
| 0x6c | 0x17c | 600 / 1250 | ? |
| 0x70 | 0x180 | 2000 | ? |
| 0x74 | 0x184 | 250 | ? |
| 0x78 | 0x188 | 300 | ? |
| 0x7c | 0x18c | 1500 (default `0x4631b0`) | fall height above which fall damage applies (`0x44b254`) |
| 0x80 | 0x190 | 40 | look-ahead distance of the ledge sensor (`0x44b378`, OBSTACLE.md §2) |
| 0x84 | 0x194 | 100 | ? |
| 0x88..0x90 | 0x198..0x1a0 | 1.0 | `0x19c` = damage per fall (`0x44b27c`) |
| 0x94 | 0x1a4 | 3.0 | ? |
| 0x98..0xac | | 0.5, 0.5, 0.3, 0.5, 0.15, 0.5 | ? |
| 0xb0/0xb4 | | 1.0 | ? |
| 0xb8..0xc8 | | 0.5,0.5,0.5,1.0,0.5 | `0x1d8` (P+0xc8 = 0.5) knockback arg |
| 0xcc | 0x1dc | 0.8 | knockback arg (`0x44b261`) |
| 0xd0 | 0x1e0 | 1.5 | ? |
| 0xd4..0xdc | | 0.5 | ? |
| 0xe0 | 0x1f0 | column index | (−1 = not loaded yet) |

## 3. Perso struct fields (0x754 bytes, vtable `0x4aabc0`, 43 methods)

Base instance (0..0xfc, see docs/FORMAT_INS.md): `+4` id, `+8` flags (`|= 0x20` in `0x44a3d0`),
`+0xc` position, `+0x28` rotation 3×3, `+0x4c..0x54` scale, `+0x60..0x68` midpoint, `+0x6c`
animation speed, `+0x1c` cell, `+0xf8` model.

| offset | type | meaning | set / read |
|---|---|---|---|
| 0x100 | f32 | anim/time factor (100.0 normal, 10000.0 at `0x44e690`) | `0x44b4a0`, `0x44e690`, `0x44ad6a` |
| 0xfc | f32 | speed factor (`0x44e7f0`) | |
| 0x104 | u32 | type flags; `(>>5)&0x1f` = subtype 1..5 (`0x40c350`) | ctor `0x40c380` |
| 0x110..0x1f0 | P | parameter block (§2.6) | ctor |
| 0x1f4..0x1fc | vec3 | **position** (feet) | `0x44a3d0` (from +0xc), `0x4624f0` (y = ground), `0x44e1c0`, `0x44dec6`, `0x44e55e` |
| 0x200 | u32 | result of `0x437180` (ground/material code) | `0x4624f0` |
| 0x204..0x20c | vec3 | displacement this frame | `0x44bb20`, `0x4627d0`, `0x456210`, `0x4653b4` |
| 0x210..0x218 | vec3 | filtered up vector (state 1) | `0x44bd30`, reset `0x44ab20` (0,1,0) |
| 0x21c | int | **state** 0 normal, 1 ?, 2 dead, 3 key-7 mode, 4 ?, 5 scripted anim, 6 on object/ridden, 7 follows object +0x55c, 8 rail?, 9 results sequence (sub-automaton +0x724, PERSO_STATE9.md) | `0x44c980`, `0x44c9f0` |
| 0x220 | int | previous state | `0x44c980` |
| 0x224 | f32 | ground height | `0x4624f0` |
| 0x228 | f32 | height above ground | `0x4624f0` |
| 0x22c | u8 | **onGround** | `0x4624f0`; getter `0x44bcf0` |
| 0x234 | u8 | ledge ahead (charge run brakes), OBSTACLE.md §2 | `0x44b2e0` |
| 0x238 | f32 | invulnerability timer (−dt) | `0x44b638`, `0x44ccfd` |
| 0x23c | int | HUD frame counter | `0x44ae60` |
| 0x240 | f32 | push timer (−dt) | `0x44bb20` |
| 0x244 | f32 | vertical correction speed (0 per frame, `0x45848c`) | `0x44bb20` |
| 0x248 | u8 | (0 after render) | `0x44b4a0` |
| 0x24c | f32 | **health** (hearts, 3.0 on reset, max 5) | `0x44b220`, `0x44b6e9`, `0x44ab20` |
| 0x250 | int | **lives** (99 at level start `0x44a538`) | `0x44c730`, `0x44c7a0` |
| 0x254, 0x258, 0x260, 0x264 | int | HUD counters (`0x44ae60`) | `0x44a3d0` = 0 |
| 0x25c | int | **bonus counter** (25 → heart) | `0x44b6d1`, `0x44c8d4` |
| 0x268 | u8 | return from state 3 | `0x44b980`, `0x44b4a0` |
| 0x26c | u32 | **death kind** (0 = alive; Kill writes the kind, Reset `0x44ab54` clears it; no actor-list registration if ≠ 0), not an attached object | `0x44c453` |
| 0x270, 0x280, 0x704 | f32 | timers −dt | `0x44b1b0`, `0x44cd25`, `0x44cd45`, `0x44c89c` |
| 0x274, 0x27c | | reset to 0 | `0x44ab20` |
| 0x278 | int | respawn countdown (−1 idle; 2 after death; 0 ⇒ clear id flag 0x10) | `0x44c730`, `0x44b5ce` |
| 0x288 | f32 | duration of the death animation (used by `0x4459c0` state 3) | |
| 0x28c..0x294 | vec3 | position before displacement | `0x4624f0` |
| 0x298, 0x2bc | 0x24 | colliders (wall, ground) | `0x436cf0/0x436d10` |
| 0x2e0 | u32 | ground type (`[0x53a554]`, 2 = `[0x53a560]`) | `0x4624f0`, `0x45ae50` |
| 0x2e4 | u32 | `[0x53a560]` | |
| 0x2e8 | f32 | scale factor (→ +0x54) | `0x44bd00`, `0x462bb7` |
| 0x2ec | ptr | level `[0x50944c]` | `0x44ae20` |
| 0x2f0 | ptr | world/time `[0x509adc]` (`+0x38` = dt) | `0x44ae30` |
| 0x2f4 | ptr | input `[0x5e6188]` | `0x44ae40` |
| 0x2f8 | f32 | **dt** | `0x44baf0`; getter `0x44bb00` |
| 0x2fc..0x304 | vec3 | camera position | `0x44c000`; getter `0x44c020` |
| 0x308 | int | ground type: 1 = slippery (`0x45a850`), 2 = dust (PERSO_MOVE.md §6.4) | `0x462920/0x462962` |
| 0x30c, 0x318 | vec3 | start position (2×) | `0x44a3d0` |
| 0x324..0x32c | vec3 | start look direction (−rot[1][0], rot[1][1], −rot[1][2]) | `0x44a3d0`; `0x44ab20` → M |
| 0x334 | obj | push/impulse object | |
| 0x35c | f32 | fallen height (accumulator) | `0x44b914`, reset `0x44b836`, `0x458162` |
| 0x388 | M | mover (§2.3) | |
| 0x458..0x460 | vec3 | up-vector source (state 1) | `0x44bd30` |
| 0x474 | f32 | timer (blocks input via +0x238) | |
| 0x494/0x498 | ptr | animation controllers | `0x44ad90` |
| 0x49c/0x4a0 | f32 | animation lengths of anim 3 / 0x42 | `0x44ad90` |
| 0x4a4 | obj | sound source | `0x401cdf`, `0x456246` |
| 0x4a8 | int | sub-state of state 1 | `0x456210` |
| 0x4b4 | ptr | linked second instance (position/rotation copied) | `0x44bf10`, `0x455de4` |
| 0x4d8 | u8 | HUD flag | `0x44c459/0x44c6d9` |
| 0x4ec | u8 | altMode (fly/swim) | `0x4599d9`, `0x44de44` |
| 0x4ed/0x4ee | u8 | altMode direction flags | `0x45a7b0` |
| 0x4f0..0x4fc | plane | clamp plane in altMode | `0x459eb0` |
| 0x50c | int | (reset in SetState) | |
| 0x520, 0x524 | f32 | timers | `0x4651d0`, `0x464ef0` |
| 0x53c / 0x540 | f32/int | scripted-anim timer / anim id (0x11/0x12 = special) | `0x44db50` |
| 0x544..0x54c | vec3 | alternative position (on a moving object) | `0x44bf10` |
| 0x550 | u8 | use +0x544 | `0x44e451`, `0x4542a4` |
| 0x554 | ptr | object that takes over the Perso's position/rotation in state 5 | `0x44db50` |
| 0x55c | ptr | object being followed in state 7 | `0x44e1c0` |
| 0x560/0x561 | u8 | flags → `0x401480/0x401440(App, 0.5)` (fade) in state 5 | `0x44db50` |
| 0x564 | vec3 | position for anim 0x49 | `0x44e690` |
| 0x57c | u8 | trigger anim 0x49 (state 5) | `0x44e679` |
| 0x58c | int | 2 = ? (state 6) | `0x44b980` |
| 0x590 | ptr | ridden object (`+0x132` flag) | `0x463502` |
| 0x5b4 | int | **jump/attack sub-state 0..11** | `0x457a50` et al. |
| 0x5b8 | f32 | jump-controller timer | |
| 0x5bc..0x5c4 | vec3 | displacement from the jump controller | `0x457a50` |
| 0x5cd | u8 | use +0x5bc | `0x457a50` |
| 0x5cc | u8 | `0x42f6b0(...) == 1` (ground test after render) | `0x44b4a0` |
| 0x5e0 | f32 | timer (→ HUD+0x2c ·2/3) | `0x457a50` |
| 0x5f4/0x5f8/0x5fc | f32/f32/u8 | timers (0.2 = `0x4a9760`) | `0x44ba70` |
| 0x68c | ptr | follow target (look direction) | `0x45b110` |
| 0x690 | u8 | **frozen**: skips the rest of the update; 0 at the end | `0x459199`, `0x45968d` |
| 0x694 | int | halves the collision radius in state 1 | |
| 0x69c..0x6a4 / 0x6ac | vec3 / u8 | second external displacement | `0x44bb20` |
| 0x6e4, 0x6e8, 0x6ec, 0x6f8 | | mode/timer `0x465fe0` (blocks Perso_Move) | |
| 0x710 | obj | accumulator (`+0x10 += dt`) | `0x453ca0` |
| 0x724 | int | sub-state of state 9 (0..5) | `0x454090` |
| 0x72c | vec3 | position for anim 0x4b | `0x454090` |
| 0x748 | ptr | object (`0x4077f0`) | `0x454090` |

Vtable `0x4aabc0`: `[1]=0x44a3d0` PostLoad, `[2]=0x42e2b0` anim/skeleton tick (base),
`[4]=0x403fe0` type-flags ptr, `[17]=0x44ab20` Reset, `[22]=0x44cda0` message handler,
`[32]=0x4624d0` radius, `[34]=0x44c030` position ptr, `[36]=0x44c720` isDead,
`[38]=0x44c110` TakeHit(type), `[10]/[18]/[20]/[42]=0x462c60` empty.

## 4. `0x4459c0(Game, dt)` and `0x451cc0(dt)`

### 4.1 `0x4459c0` – death/respawn sequence with an iris (Game+0x14, table `0x445b80`)
`Game+0x10 = dt`; first `0x451cc0(dt)` (§4.2). **Iris** `Game+4` (`0x4776b0(from, to, dur)`:
`+0=from, +4=to, +8=dur, +0xc=t`; `0x477920(dt)`: `t += dt`, value = from − (from−to)·min(t/dur,1)
→ `0x4776d0(value)`, returns done). `0x4776d0` is **not brightness** but the same iris used in the
menus (MENU_NEWGAME.md §2.7): an opaque black ring of 50 segments around the screen centre (320, 240)
(`0x4ab5bc`/`0x4abd40`, the only readers of those constants), inner radius `value·0.99·480`, outer radius
475. The picture thus stays at full brightness inside a shrinking/growing circle, and because the follow
camera keeps Woody centred, you see him in the hole. The iris is only drawn in states 0, 1 and 4 (the
calls to `0x477920`); the function runs in step 33 of the frame, so **after** the HUD (step 29) and also
during a cinematic (only the pause holds it still).
**Level start**: the Game ctor `0x445850` does `0x445930`, one iris tick of 0.1 s and then the iris
**0 → 1 over 1 s**, state 1: every level (House too, behind the title menu) opens with the iris,
simultaneously with the flat fade-in of 1 s from the load routine (`0x404332`, `0x401440(1.0)`). The
script fades 1150/1151/1152 are something different: black 640×480 rectangles from the App faders
(`0x445bf0`, `0x4014c0`); the start of the W1B boss fight only uses those (`1152` 0.3 s, then `1150 [100]`).
There are no other writers of `Game+0x14`/`+0xc` (besides `0x445850`/`0x445930`/`0x4459c0`); `0x445930`
further only comes from the pause menu (restart, `0x40584d`: state 0 with timer 0.1 ⇒ respawn again,
iris opens).
**Live verified** (`tools/wiris.py game --level W1B --pos -7191 1400 -8939 --at 8`: the original starts
in W1B, Woody is placed in trigger volume 94 after 8 s): level-start iris 0 → 1 (`0x445908`), state 1 → 2
after 1.00 s; start of the boss fight (vt 28.33..28.62) only `1152` ×44, `1150 [100]` and the rail
camera, **no iris**; Woody (with no input) dies at vt 38.54 ⇒ state 3, iris 1 → 0 at 40.54 (death
duration 3.0 − 1.0), life lost at 41.54, respawn at 41.79, iris 0 → 1, state 2 at 42.80.
* state 0: `0x451bd0()` (thunderstorm off), fader tick, `Game+0xc -= dt`; ≤ 0 ⇒ `0x445930(Game)`
  (fader (0,0,0.1), timer 0.1, `Perso->0x44a810(0)`, all actors `vtbl[28]()` via `0x40c040` = an empty `ret` in every class, PERSO_DEATH §3.4,
  `0x458f90(Game+8)`), iris (0 → 1.0 over 1.0 s), **state 1** (iris opens).
* state 1: iris done ⇒ **state 2** (gameplay).
* state 2: `Perso->vtbl[36]()` (dead) ⇒ **state 3**, timer 0.
* state 3: `0x451bd0()`; timer += dt; ≥ `Perso+0x288 − 1.0` ⇒ iris (1.0 → 0 over 1.0 s), **state 4**.
* state 4: iris closed ⇒ `Perso->0x44c730()` (lose a life), **state 0**, timer 0.25 (screen black: the
  iris is at 0).
After that, always: `0x44f0a0(Game+0x64, dt)` (mode automaton 1..4 with timers `+0x118/+0x128` and
`0x401440/0x4014c0(App, …)`); returns true ⇒ **respawn**: `Perso.pos = Game+0x1b0`, `Perso->vtbl[17]()`
(Reset `0x44ab20`), `0x459ff0(&Perso.mover, &Game+0x198)` (look direction), `0x44a650(Perso, &Game+0x198)`,
camera `0x41f9f0(2)`, `cam+0x368 = 0`, `0x41f410(0, 0)`.

### 4.2 `0x451cc0(dt)` – "thunderstorm"/periodic hazard with safe zones (class 80)
**Superseded by STORM.md** (full decompilation and port, `src/storm.c`). Two corrections to the summary below: `+0x108` is
the radius itself (default 1000, the test squares it) and the zone is a **3D sphere** around `pos + (0, +0x10c·0.5, 0)`,
not a circle in xz.
Globals: `0x5e59ec` active, `0x5e59e4` timer, `0x5e59e8` interval (set by `0x451ba0(interval)`
from the game message handler `0x444b79`, argument ×0.01; id in 1010..1050), `0x5e58cc[0x5e59e0]`
= list of class-80 instances (ctor `0x451a90`, vtable `0x4ab0c4`, message 54: `+0x108` = radius²
(default 1000), `+0x10c` = height (default 400), `+0x114` = "player inside"). `0x451bd0` = stop
(`0x46e3a0`). Per frame if active: `timer -= dt`; crosses 1.7 (`0x4ab13c`) ⇒ sound 8
(thunder-warning). For each registered actor (`0x4c52d8[0x4c531c]`, class 1 = Perso): `0x451be0(pos)`
looks for a class-80 zone with `dx²+dz² < +0x108` (around `pos+(0, +0x10c·0.5, 0)`) ⇒ `zone+0x114 = 1`.
If `timer ≤ 0`: `timer += interval`, `0x46e030(timer, 0)` (lightning flash/effect); per Perso: no zone
⇒ `Perso->vtbl[38](9)` (**TakeHit 9 = lightning strike**) + sound 7 at `pos+(180,0,0)`; two effects
`0x46def0` at pos and pos+(0,2000,0); inside a zone: sound 7, three effects `0x46df80` at random
points (`0x43ff40`) around the zone with radius `+0x10c − 80` and two more (0.4 = `0x3ecccccd`).

## 5. Open questions
* Meaning of P+0x08..0x18 (193/143/160, 61/81), P+0x58..0x78 (0.2, 0.6, 200, 380/400, 650/1250,
  600/1250, 2000, 250, 300) – probably jump/fall parameters in `0x457a50` (not read).
* Gravity: no constant found anywhere in `0x44b530`/`0x4624f0`; falling happens through the
  jump controller (`+0x5bc`) – the fall/jump formula in `0x457eac..0x458b9c` still needs to be read.
* Exact meaning of states 1, 4, 6, 8, 9 and of key 7 (state 3) and key 6.
* What do `0x464ef0`, `0x465e50`, `0x465b10`, `0x458bf0`, `0x463430/0x457330` (every frame) do?
* `0x44f0a0` (Game+0x64 mode automaton): when is the respawn triggered (modes 1..4)?
* Which game message (1010..1050) starts `0x451ba0`? (`switchmap.py` on `0x444870` needed.)
* `0x42d2e0` (step 26) and `0x42b4e0/0x42ac10` (render) have not been decompiled.
* `0x44c720` (vtbl[36]) is derived from the bytes (`8B 91 1C 02 00 00 33 C0 83 FA 02 0F 94 C0 C3`),
  the disassembler had that function misaligned.

(to be filled in below)
