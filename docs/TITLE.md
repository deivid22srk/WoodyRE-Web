# TITLE.md — title screen: rotating camera, menu pose, page 0/1 (Woody.exe, build 17-10-2001)

Status: **static analysis**; addresses = Woody.exe (image base 0x400000), reviewable with
`python tools/drange.py START END`. Data: `extract/Data/House/House.ins` (`tools/insparse.py`), `House.rck`
(`tools/rckexport.py`), script `out/house_code.txt`. Supplement to GAMEFLOW.md §4.5/§5, CINEMATIC.md §4/§6,
CAMERA.md §2/§4 and HUD_TEXT.md §6. Uncertain points are marked **uncertain**.

## 0. Summary

- The title camera is **not** a separate camera mode and **not** an `.ins` camera or rail (House.ins has 0 cameras and 0
  trajectories). It is **camera mode 0x80 (index 7) = "camera from an instance's animation"**, and that instance is
  **the Perso itself**: the engine keeps Woody in House continuously in the scripted action **0x49 = .ins animation 73**, and that
  animation has a **camera track**: node 141 (flags 0x80, camera) describes a closed circle, node 142 (flags 0x180,
  target) stays still. `0x44dda0` switches to mode 0x80 every time the action starts (`0x44df7d..0x44dfad`), without
  letterbox, with a hard cut.
- Orbit (world space): center **(−45.8, 3117.0, 166.7)**, radius **2500**, fixed height y = 3117.0, **one revolution per
  10.0 s = 36°/s = 0.628 rad/s**, positive rotation about +y (counter-clockwise seen from above; +x → −z → −x → +z).
  Look point fixed at **(529.3, 3053.1, 528.7)** (i.e. 680 units to the side of the center: the camera-target distance breathes between
  1842 and 3166). No collision, no smoothing, no letterbox; vfov 83.97° (zoom 1.2, 4:3).
- Woody himself is **invisible** on the title screen: `0x44e690` sets his transparency target to 1.0 every tick with a
  rate of 10000/s (visible only during the intro cinematic 0). Animation 73 is also a static pose (all
  bones 1 key). He's only the carrier of the camera track.
- 2D: page 0 = logo (level bank House.rck image 1, 209×247 at (216,16), fade 1 s) + blinking text
  "Press a key" (string 21, size 30, y = 336); page 1 = same logo + 4 lines (22 New game, 23 Load game,
  36 Options, 2 Quit) starting at y = 264, line spacing 46.5, selected line blinks (0.25 s off / 0.25 s on), white.

## 1. Who sets the camera: the chain 1141 → `0x44e690` → `0x44dda0(0x49)` → mode 0x80

### 1.1 Script (House, `out/house_code.txt`)
The script sends no camera message at all. Object 124 (word 1933): `DELAY 10` → **`1141 [0x100007c]`** = instance
slot **124** (not 0x73; slot 0x73 = 115 is the vector instance of the intro: 1160 and 1130). Handler `0x4448d1`:
`0x42f6b0(inst124, 5, &vec6, 0)` fetches the **type-code-5 node** (vector, 2 points) of the instance in world coordinates
and `0x44e640(Perso, &vec6)` copies the 6 floats to `Perso+0x564..0x578` and sets `Perso+0x57c = 1`.

Instance 124 (House.ins model 15, node 2 flags 0x520, local points (0,0,0) and (0,0,−300), quat (1,1,1,−1), scale 1):
**P0 = (1113.34, 2309.87, 1193.29)**, **P1 = (1113.34, 2309.87, 1493.29)** → direction P0→P1 = **+z** (yaw 0 in the
port convention `atan2(dx, dz)`). (Computed with the port's `ins_point_world`, the same code used by 1130.)

### 1.2 Engine, every tick while `app+0x68 == 0` (House): `0x401d0c` → `0x44e690(Perso)`
```c
void Perso_MenuPose(Perso *p) {                         // 0x44e690
    if (p->state == 0 && p->poseSet /*+0x57c*/) {       // +0x21c
        Perso_ScriptedAction(p, 0x49, &p->pose /*+0x564*/, NULL);   // 0x44dda0
        Perso_SetState(p, 5);                           // 0x44c980
    }
    p->fadeRate /*+0x100*/ = 10000.0f;                  // 0x461c4000
    Perso_SetFade(p, Cin_Running(game+0x64) /*0x44f2e0: cin state 2..3*/ ? 0.0f : 1.0f, /*direct*/0);   // 0x44e7f0
}
```
Position in the frame (`0x401ab0`): after the Perso update (`0x401d07`, only if not paused and no cinematic running), before
the instance tick `Perso->vt[2](1)` (`0x401d57`, animation clock) and the fade stepper (`0x42b400` → `vt[3]` = `0x40bf10` →
`0x44e810`, `0x401d78`). The camera update (`0x459090` → `0x41eff0`) sits right at the front of the frame (`0x401c01` →
`0x401940` → `0x445ba0`); that also runs in app state 0 (arg = `app+0 != 3`).

### 1.3 `0x44dda0(0x49, vec6, 0)` (CINEMATIC.md §6), the steps relevant to the title
1. `0x463e30(0x49)` → logical record **82** of table `0x4b6180`: `sub = {73, 0, 0, 0}`, `+0x10 = 6000`, **speed = 3.0**,
   restart = 1. (Record 81 = `{72,0,0,0}` = the intro.)
2. `total = remain = 0x436b90(82, 0)` = `duration(73)/4096 / speed` = 30.0 s / 3.0 = **10.0 s** (`Perso+0x538/+0x53c`).
   House.ins model 0: anim 73 = 6000 frames, duration 122880/4096 = 30.0 s.
3. Animation controller `Perso+0x494`: `vt[4]()` (`0x436a40`: current = −1 ⇒ the next start is always a **restart**),
   `vt[2](82)`; the tick `0x436a50` then sets `inst+0xa8 = now` (clock start), `inst+0xb0..0xbc = {73, 0, 0, 0}` and
   `0x42e290(inst, 3.0)` (clock speed).
4. Position: `Perso+0x1f4 = P0`, `onGround = 1`, facing = `normalize_xz(P1 − P0)` via `0x459ff0`, `0x44bd00` (orientation).
   `0x44bd30`: `f = −dir`; **row 0 = f × up** (`0x41af10(this, out, arg)` = `this × arg`; PERSO_FRAME §2.4 writes
   "up × f", which is the wrong order), row 1 = up × row 0 = f, row 2 = up. For dir = +z: row 0 = (1,0,0),
   row 1 = (0,0,−1), row 2 = (0,1,0).
5. Camera (`0x44df67`): if `0x42feb0(Perso, 0x49)` (top-level node with flags 0x80 has a position track for anim 73:
   yes, 100 keys) and `Perso+0x558`: `CamMgr+0x5d4 = Perso`, **`CamMgr+0x618 &= ~2` (no letterbox)**,
   `0x41f9f0(2)` (**hard cut**), **`SetMode(7, 0)`** = mode 0x80 (`0x41f410`; in the mode-0x80 branch: bit 2 of `+0x618`
   off ⇒ `0x41f940` = letterbox off / 4:3). `SetMode` does nothing if `CamMgr+0x690` is set (message 590; never in House).

### 1.4 Loop
State 5 (`0x44db50`) counts `remain -= dt`; when ≤ 0 → state 0 + idle + `0x44e5a0` (follow camera, 0.5 s smooth
transition, `SetMode(0,0)`). In the **same frame**, `0x401d16` then calls `0x44e690` again: state is 0 ⇒ action 0x49
starts over, controller reset ⇒ the clock is back at phase 0, and `SetMode(7)` with a cut cancels the transition that was
just started (`+0x109 = 0`). Since the camera update only runs the following frame, the follow camera is **never
visible**; the track is closed (key 0 = key 99), so the loop is seamless. Net effect: `phase = fmod(t, 10 s) / 10 s`.
The first 0.1 s after loading (DELAY 10 before 1141) shows the ordinary follow camera behind Woody's `.ins` position
(1114.5, 2390.6, 1093.6); that falls within the first 4 black frames plus the 1.0 s fade-in (GAMEFLOW §4.4) and is
practically invisible. **Uncertain**: whether the Perso is already in state 0 during that 0.1 s (falling to the ground
could delay the start of the pose slightly; `0x44e690` just waits until `+0x21c == 0`).

## 2. The movement, exactly

### 2.1 Track data (House.ins, model 0 = Woody, 142 nodes, 91 anims)
| node | flags | anim 73 | content |
|---|---|---|---|
| 141 | 0x80 (camera, top-level) | 100 position keys, t = 0 … 6000 (step ≈ 60.6 frames) | circle in the local xy plane: center (−1159.14, 1026.58), radius 2500.0 (±0.7), **z = 807.17 constant**; angle 0° → 360°, even (3.636° per key), increasing (local x → local y) |
| 142 | 0x180 (target, top-level) | 1 key | (−584.04, 664.62, 743.19) |
| 1 (root) and all bones | 0 | 1 key | static pose; root (0, −3, 52.09) |

Evaluation = CINEMATIC.md §4 (`0x41f1ee` → `0x42fa40` → `0x42fa80`): `phase = inst+0xac / (duration/4096)`,
`frame = 6000·phase`, linear interpolation between the keys (`0x43a660`; cut detection never triggers: keys are
60 frames apart), `C = c·M + T`, `G = t·M + T` with M = instance rotation (rows), T = instance position,
view = lookAt(C, G, up = +y). No roll, no fov track.

### 2.2 In world coordinates (Perso at P0, facing +z ⇒ world = P0 + (lx, lz, −ly))
| quantity | value |
|---|---|
| orbit center | **(−45.80, 3117.04, 166.71)** = P0 + (−1159.14, 807.17, −1026.58) |
| radius | **2500** (horizontal), constant height **y = 3117.04** (= P0.y + 807.17) |
| look point (fixed) | **(529.30, 3053.06, 528.67)** = P0 + (−584.04, 743.19, −664.62); lies 679.6 (xz) to the side of the center and 64 below the camera |
| start point (phase 0) | (2453.5, 3117.0, 166.7) = center + 2500·(+x) |
| orbit period | **10.0 s** (30 s animation at speed 3) ⇒ **36°/s = 0.6283 rad/s**, track speed ≈ 1571 units/s |
| direction | `eye = M + 2500·(cos a, 0, −sin a)`, `a = 2π·t/10`: phase 0.125 → (1719.1, 3117, −1600.7), 0.25 → (−47.3, 3117, −2332.3), 0.5 → (−2545.1, 3117, 168.1), 0.75 → (−44.6, 3117, 2665.7). Positive rotation about +y: counter-clockwise from above; the camera slides to its own right, the scenery drifts left across the frame |
| camera–target distance | 1842 (phase ≈ 0.875) … 3166 (phase ≈ 0.375); pitch ≈ −1.2° … −2.0° |
| projection | no letterbox: `sx/sy = 1/0.75`, zoom 1.2 ⇒ hfov 100.4°, **vfov 83.97°** (CAMERA.md §5) |
| collision / smoothing / transition | none (mode 0x80: direct look-at, cut) |

(The table values were sampled with the port's `ins_camera_eval` on the Woody instance placed at P0/yaw 0; they
match the hand calculation from §2.1.)

The rotation keeps running on **all** menu pages as long as the level is House (table `0x405af8`: House is never
paused, `0x404ef1`); only during the intro cinematic does anim 72 take over (§4).

## 3. Woody on the title screen

- Action 0x49 → logical record 82 → `.ins` animation **73**, once per 10 s, restarted forever by `0x44e690`
  (§1.4). The chain `{73, 0, 0, 0}` means the instance clock would flow into anim 0 (idle) after it ends, but the
  restart happens in the same frame. Anim 73 has one key on all mesh/bone nodes: a **fixed pose**.
- Location: P0 = (1113.34, 2309.87, 1193.29), facing direction +z (instance 124, §1.1). Root motion of 0x49 is zero.
- **Visibility: invisible.** `0x44e690` sets `+0xfc = 1.0` every tick (transparency target; 1 = gone, INSTANCE.md §5) with
  `+0x100 = 10000/s`; the stepper `0x44e810` (Perso vtable slot 3 `0x40bf10`, called from `0x42b400`) brings `+0x6c`
  to 1.0 in one frame; `0x42e374` does not draw an instance with `+0x6c > 0.98`. The animation clock still runs
  (`0x43eee0` sits in `0x42e2b0` before that test), so the camera keeps orbiting. During the cinematic (cin state 2..3)
  the target is 0 ⇒ visible immediately; the script also separately sends `57 [0,10000]` + `56 [0,0]` (word 1264).
  This resolves CINEMATIC.md §10.3: nothing resets `+0xfc` during the title (`0x44b4bd` only in state 3,
  `0x44d7e7`/`0x44d960` are the damage/respawn path).
- The HUD is hidden (`0x404ebb`: `0x448450(hud, 2)` for all pages except 0x18/0x19).

## 3.1 What flies around the treehouse: **butterflies**, not birds

Three "environment instances" (class **90**, ctor location `0x403d51`, vtable `0x4a90ac`) sit around the treehouse:
slots **60, 61, 62** (House.ins model 2 — a single volume node, `nmesh_nodes = 0`, so the instance itself is never
drawn). Their script (`out/house_code.txt`, init only) sends:

```
1200 [slot, 90]   1501 [slot, 0]   1504 [slot, 3 / 2 / 3]
```

`1501` (`0x46cd07`) = mode, `1504` (`0x46cdcc`) = count. The think function `0x472560`, mode 0 (`0x4727d3`),
calls `0x47e050` until the instance's live count `+0x108` reaches `+0x100` (3 + 2 + 3); while the volume stays in the
frame's instance list nothing dies, so that is the steady state. A butterfly dies the first frame its volume did not think
(`0x47dc77`, frame stamp `+0x104`) and the next think makes a new one (AMBIENT.md §3.4). `0x47e050` builds a 0x50-byte
record in the effects pool `[0x5e823c]+0xdb8` (max 2000, shared with every effect; AMBIENT.md §5):

| field | value |
|---|---|
| `+0x00` | `rand()·10` — wing-flap phase in seconds, incremented by dt every frame (`0x47d457`) |
| `+0x04` | `1e6` — lives practically forever |
| `+0x0c..0x14` | random point of the volume node's own box (node-local aabb of its points, local y/z extents swapped, mirrored through polygon 0) mapped by the node's world matrix (`0x472811..0x472a2a`, AMBIENT.md §3.1) — not the world aabb |
| `+0x18..0x20` | `(rnd·2−1, rnd·2−1, rnd·2−1)`, normalised if not zero |
| `+0x28` | `0x10035 − (int)(rand()·−3.99)` = bank 0, **image 53..56** of `Common/<character>.rck` = four butterflies (64×64, colour-key). Chosen once |
| `+0x2c` | the state: **not written** by `0x47e050` (whatever the slot's last record left there; 0 in fresh memory, AMBIENT.md §3.5) |
| `+0x4c` | `0x47d440`, the updater |

**Updater `0x47d440`** (clock = the global frame dt, not the instance clock):
every **0.3 s** (`0x4aab98`) a new direction — `dir.x += rand()·sign(dir.x)·3.5`, likewise `dir.z`,
`dir.y += rand()·k·3.5` with `k = −0.8` in state 1 and otherwise a random `+0.7` or `−0.5` (`0x4abd90` = 3.5) —
then normalize; `pos += dt·dir·100` (`0x4a9010`). `0x4300c0` tests the point against the owner's volume nodes:
outside ⇒ `dir = normalize(instance position − pos)`. State machine `+0x2c`: 0 → 1 with probability 0.001 per
frame (`0x4a94c4`, start descending), 1 → 2 when it is **outside** the volume at or below `inst+0x10c` (`0x472911`;
landed, `dir = (dx, 0, dz)` to the instance origin, no clamp), 2 → 0 with probability 0.008 per frame (`0x4abd94`, turning
to the instance origin). Landed it does not move and flaps at F = 166.667 instead of 1000 (`0x47d767`). Full pseudo-C:
AMBIENT.md §3.4.

**Drawing — not a sprite but two hinged wings** (`0x47d78b..0x47dc6b`, quad build `0x470f10`). The draw
state is still that of a sprite though: mode `0x28`, **alpha blend with colour-key, exactly the same state as the
bonus sprites**, not additive, diffuse = neutral (0.5, 0.5, 0.5, 1.0). The geometry:

- **V** (`+0x23c`) = the flight direction with a fixed `y = 0.5`, normalized — the hinge axis. **B0**, **B1** =
  an orthonormal pair perpendicular to it (`0x46d320`).
- Angle from the 512-entry cosine table (`[0x5e823c]`, `cos(i·2π/512)`, built in `0x40248f`): the
  table value at index `ftol(1000·phase)` gets **multiplied by itself** (`0x47d869`), ×128, and subtracted
  from 255. Closed form: **θ(t) = 134.30° + 45.00°·cos(2π·f·t)**, so 89.3°…179.3°, with
  **f = 2·1000/512 = 3.90625 Hz** (`F = [ebp+8]`, 1000 in flight, 166.667 in the slow branch `0x47d767`).
- Wing 1: **U = cos θ·B0 + sin θ·B1**; wing 2: **U′ = −cos θ·B0 + sin θ·B1** (`0x47da8c`, only the sign
  of the B0 component flips). So they mirror around B1: at one end of the stroke they lie almost flat
  (178.6° apart), at the other almost together (1.4°) — open and closed like a book.
- Each wing is a **square with half-side 21.2132** (= 30·cos 45°; corners at 45/135/225/315°,
  `0x470f94`, `table2[18] = 64`) in the plane of V and U, with **UV set 4** (`0x470e96`, = set 0 mirrored in u).
- Center = `pos + 20.2757·U + (0, 3·sin(2π·f·t), 0)` (`0x47d99b`, `0x47da2c`): the hinge edge therefore sits
  on the particle itself, with a vertical ±3-unit bob layered on top in quadrature.

`+0x254..0x25c` (R = U × V) is computed but later ignored, and `0x470f10` only reads V and U on this path.
A port that uses the table value as *width* gets a stationary flat blob: the animation is in the angle.

## 3.2 The frozen dots in the sky are a port artifact

House.ins places the ten figures of the world-select carousel (slots **105–114**, class **110**) at
x −10753..−10008, y 3816..4213, z 3985..5073 — 10 to 13 km from the treehouse, but there is no far plane
(`0x47b372` only clips the four side planes) and no fog, so a port that leaves them at their file position
draws them as a handful of dots in the sky. They are flat-colored (model 14 uses only ARGB1555 `0xFC00`
red and `0xFFE0` yellow) and don't move, because only menu page 3 (`0x45edc1` → `0x451890`) gives them a
carousel angle.

The original draws **nothing** there: vtable[3] of class 110 (`0x451a30` → `0x489650` → `0x489210`), which
`0x42b400` calls every frame for every instance **before** the draw loop, overwrites `inst+0x0c..0x14` with
`M·(offset + base) + T` from `[0x5e86ac]+0x154..0x18c`; the offset `+0x104..0x10c` is zero until page 3 is
active, so the local point is `(0, 0, 300)`. The file position is dead data. What that matrix holds during
the title is not established — **uncertain** whether the original draws them somewhere else then or not at all. The
port doesn't have the world-select page and simply makes them invisible.

## 4. New game, attract timer, return

### 4.1 Page 1 → "New game"
`0x4600e0` (validate of page 1, item 0): `+0x14 = 1` (deferred result), `+0x38 = 0` (no iris), logo alpha
`[0x5d7b28] = 0` (logo gone immediately), **return value 2** ⇒ App (`0x4051b0`): `SetVar(app+0x8c, 1)`. On the next frame
the page delivers the deferred result **1** (`0x4601b0` → `0x45baa0`) ⇒ `0x4051c5`: `app+0x94 = 1`, page 0x1f.
Script object 115: `var1 == 1` → `var1 = 2`, immediately `1131 [0, 72]`, 8× `1132`, `1130 [0x73, 8, var2]`.
Cinematic (CINEMATIC.md): wait 0.5 s with a 0.4 s fade-out (the orbiting camera keeps turning meanwhile), then
Perso goes to the vector of slot 115 (−11299, −7, 601), anim 72 (159 s / 3 = 53.0 s), camera mode 0x80 **with**
letterbox, Woody visible. The Perso update is skipped (`0x401cf9`), so state 5 / `remain` of action 0x49 freezes.
Page 0x1f (`0x40508c`): done when `var1 == 4` (script, T = 5300) or action **6** or **9** *released* (`0x467440`):
`app+0x98 = 35`, `0x44ffa0(save)`, `RequestLevel(var1 == 4 ? 0 : 0.5, 1, 1, 0)`. Page 0x1f draws nothing visible
(1 item, string 1 = empty) and turns off the logo directly on entry (`0x45b390`: `[0x5d7b28] = 0`).

### 4.2 Attract (35 s)
`app+0x98` = 35.0 at boot (`0x402653`) and after every end of page 0x1f (`0x4050c7`); **not** on House load.
It only counts down on **page 0** (`0x404fe4`), not on page 1 or deeper, and isn't reset by key presses.
- transition > 0 → ≤ 0: `SetVar(app+0x8c, 1)` (same script start as New game);
- while ≤ 0, page 0 input is ignored (0.5 s dead time);
- < −0.5 (`0x4a94bc`): `app+0x94 = 0`, page 0x1f. That coincides with the start of the cinematic (1130's 0.5 s wait).
Camera during attract = the cinematic camera (mode 0x80 on anim 72, letterbox), identical to New game.
End (var1 == 4 or action 6/9 released): if not yet 4: stop script object `app+0x90` (`0x444380`, `0x4441d0`),
`var1 = 4`, abort the cinematic (`0x44f290`), `Perso->vt[0x44]()` (reset `0x44ab20`, sets state 0 among others). Always: clear 4
faders, close the text box (`0x456ec0`), black screen, 0.5 s fade-in, `0x404e30` (page 0 + music change).
Since the Perso is then in state 0, `0x44e690` starts action 0x49 again: **the circle starts over at phase 0** (cut).
On the natural end, the cinematic itself does the Perso reset + `SetMode(0,0)` (`0x445b66`), with the same effect.

### 4.3 Returning to the title from a level
`RequestLevel(0.5, 0, 0, 0)` → load House → `0x404e30`: `app+0x54` toggles (after a load, track 0 "Menu" first, on the
next call without a load track 48 "Menu02", SOUND.md §4), page 0, state 0; script init → after 0.1 s 1141 → circle
from phase 0. The attract timer keeps running from its old value.

## 5. The 2D pages

### 5.1 Common page class (menu object `0x445e30`, update `0x446440` → `0x4464f0`)
Page object: `+4` selected item, `+8` input delay (s), `+0xc` byte "result given", `+0x10` result.
Vtable slots: [1] draw, [2] item table, [3] count, [4] page id, [5] string of item i, [7] y-start (fraction of 480),
[8] font size, [9] validate → result, [10] down, [11] up, [12]/[13] right/left (slider ±5, 0..100),
[14] back, [15] enter. Item = 16 bytes `{u32 stringref 0x0002xxxx, u32 flags, u32 result, u32 value}`; flags:
1 = selectable, 2 = fixed heading (not selectable, skipped on up/down `0x446920/0x446970`), 4 = right-
aligned (x = 640 − w − 6.4), 8 = left (x = 6.4), 0x80 = centered at x = 160, 0x40 = x unchanged, 0x10 = slider
(text + value + string 40/7), 0x20 = size × 0.8; no flag = centered (`x = 320 − w/2`).

Per frame (`0x4464f0(font, dt)`): font = `0x01030000` (font of the **level bank**), `[0x5d7b1c] += dt`, above 0.5
(`0x4b39a4`) back to 0 (blink phase); font size 30 (`0x4b39a8`), **color `font+0x60 = 0xffffffff`** (white, alpha 255);
`vt[1]()` draw; `0x446b00` logo (§5.2). Input only if `+8 ≤ 0` (otherwise `+8 -= dt`), via actions on `[0x5e6188]`,
all "just pressed" (`0x467420`): **2 = up** `vt[11]`, **3 = down** `vt[10]`, 1 = right `vt[12]`, 0 = left
`vt[13]`, **0xc = confirm** (`vt[9]`, only if `+0xc == 0`; action 0xc is set together with action 4 by the
jump key/button, `0x402efc`, `0x403168`: default LCtrl, PERSO_MOVE §3.4), **5 = back** `vt[14]` (default
Space); also `[0x5e6194]->vt[5](0)` → also `vt[14]` (**uncertain** which key: keyboard object, index 0).
Up/down wrap around; a successful move resets the blink phase to 0, a failed one to 0.25.
**No sound** for navigating or confirming in this base class.

Drawing the list (`0x446640`): size S = `vt[8]()`; for each item: while the measured width ≥ 640 and S ≥ 15:
S −= 1 (one S for the whole page). `y = vt[7]()·480`; per item: text at (x, y), then `y += cellHeight(62·S/40) + extra(0)`
= **46.5 at S = 30**. The **selected item is not drawn while blink phase < 0.25** (`[0x5d7b00]` =
0.5·0.5, `0x445e10`): 2 Hz blinking, no color difference, no cursor. Non-heading items only appear once `+8 ≤ 0`.

### 5.2 Logo (`0x446b00`, every page, only if `app+0x68 == 0`)
`RectVirtual(x=216, y=16, w=209, h=247, sx=0, sy=0, sw=209, sh=247, color = 0x808080 | (A << 24), surface = bank 1
image 1, flags 8)`: **House.rck image 1** (256×256 with alpha; the "Woody Woodpecker" logo with Woody in the
circle; exported and viewed: `tools/rckexport.py`). `A = ftol(v·0.2·254)` with `v = [0x5d7b28]` ∈ 0..5:
every frame `v -= 5·dt` (lower bound 0) after drawing; pages that want the logo (0 and 1) do in their draw function
`0x446ac0`: `v += 10·dt` (upper bound 5). Net effect: **fade-in 1.0 s on page 0/1, fade-out 1.0 s on every other page**;
page 0x1f/0x18/0x19/0x1e (`0x45b390`) and New game/Load game set `v = 0` directly. House.rck image 0 (cross +
dots) belongs to the world select, image 2 is the sheet with the clock and the enemy face of the
results screen (same as image 1 of the hubs, RESULTS.md §2.1; the PNG export looks white because the alpha byte is 0).

### 5.3 Page 0 — title (vtable `0x4aa9c8`, 0x14 B, handler `0x404fe4`)
Draw `0x45bd10` = list + logo fade. 1 item: **string 21 "Press a key"**, flags 1, result 5; S = 30, y fraction 0.7
⇒ **y = 336**, centered, blinks (it's the selected item). Handler: timer §4.2; while timer > 0:
action **9 released** (Esc, `0x467440`) or result 5 (confirm) → page 1. "Back" gives result 24, ignored.
Despite the text, page 0 thus only responds to confirm (action 0xc) and Esc. No sound.

### 5.4 Page 1 — main menu (vtable `0x4ab5c0`, 0x3c B, ctor `0x460040`, base "panel page" `0x45b830`, handler `0x40519f`)
Items (table `0x4b5e70`, S = 30, y fraction 0.55 ⇒ **y = 264, 310.5, 357, 403.5**, centered):

| i | string | result | consequence (`0x4600e0` + `0x405ba4`) |
|---|---|---|---|
| 0 | 22 "New game" | 2, then 1 | §4.1 |
| 1 | 23 "Load game" | 3 (after 0.5 s) | iris closes 0.85 → 0 in 0.5 s (`+0x38 = 1`), logo gone immediately; then `0x4051da`: savefile check `vt[3]` → page 0xb (read) / 7 ("No game saved") / 0xf, 0x10, 0x11 (errors) |
| 2 | 36 "Options" | 6 (next frame) | page 0x1b |
| 3 | 2 "Quit" | 7 (next frame) | page 0x1c ("Are you sure?", cursor starts on "No", `0x45bd40`) |

Enter (`0x460070`): base `0x45b8c0` (selection 0, **SoundFx 0x3f** = 63 → ref 111 in `Common/<character>.rck`, vol 50,
2D; `0x45b8ef`), then iris parameters 0 → 0.85 in 0.5 s, `+8 = 0` (immediately operable). While it's closing (`+0x24`),
up/down/back are blocked (`0x45bb90`, `0x45bba0`, `0x45bb30`); "back" does nothing on page 1 (slot 20 =
empty function).

**Iris** (`obj+0x2c`, 0x10 B: from, to, duration, t; `0x4776b0` sets it, `0x477920(dt)` animates linearly and draws via
`0x4776d0(v)`): a black ring of 50 segments around (320, 240), inner radius `v·0.99·480`, outer radius 475 (covers the
whole screen; angular gap = 400). v = 0.85 ⇒ gap 404 > 400 ⇒ nothing visible. It's only active if `+0x38 == 1`:
page 1's ctor sets 0, so **there is no iris from page 0 to 1**; after "Load game", `+0x38 = 1` stays set and the
iris opens (0 → 0.85, 0.5 s) when you come back to page 1 from the load/world-select pages. Color/vertex format of
`0x482cf0` was not traced into the renderer (**uncertain**, assumed to be opaque black). Draw order: items → iris → logo.

### 5.5 Remaining pages (list only; tables via the same class unless noted)
| page | content (strings) | y | notes |
|---|---|---|---|
| 2 | load slot select (class `0x45dd00`, 0x68 B) | | not worked out |
| 3 | world select, 3D carousel (class `0x45e560`, 0x148 B; GAMEFLOW §7) | | how the camera moves to the carousel at (−11300, −6, 600) not investigated |
| 4 | high scores (class `0x45bfb0`, 0x40 B; MENU_LOAD §4.8) | | black page, rows per finished level |
| 5 | save slot select (class `0x45e1f0`, 0x68 B) | | not worked out |
| 6 | 35 "Do you want to save?" / 5 Yes / 6 No | 0.4 | result 8/9 |
| 7 | 64, 65, 66, 4 "Continue" | 0.4 | no save found |
| 8 / 9 / 0xa | 67 "Game Saved" / 59 "Save failed." / 60 "Load failed." + 4 | 0.4 | |
| 0xb, 0xc, 0xe | empty (wait pages for reading/writing; 0xc stands 2 frames, 0xb/0xe 1, MENU_LOAD §5) | 0.4 | |
| 0xd, 0xf..0x16 | PS2 memory card texts 69..97 | 0.3/0.4 | almost unreachable on PC |
| 0x17 | 61 "…overwrite this save?" / Yes / No | 0.4 | |
| 0x18 / 0x19 | pause: 4 Continue, (19 Start again), 36 Options, 2 Quit | 0.05 | enter `0x45b390` (logo off) |
| 0x1a | 101..103 controller disconnected | 0.4 | |
| 0x1b | options (class `0x4601f0`): 36 heading, 38 / 39 / 132 sliders, 4 Continue | 0.4 | MENU_OPTIONS.md |
| 0x1c | 3 "Are you sure?" / 5 Yes / 6 No | 0.55 | cursor starts on No |
| 0x1d | game over (`0x45bbd0`: black panel + string 56 "GAME OVER", S = 35, centered, color 0xfeffffff; result 5 after 5 s) | | |
| 0x1e | results (panel page `0x45b830`, iris 0 → 0.37) | | GAMEFLOW §5.1 |
| 0x1f | intro running (empty) | 0.6 | §4.1 |
| 0x20 | credits (vtable `0x4aa474`, draw `0x45bd90` + roll `0x453930`): black panel, Credits.rck picture 256×256 at (32,112) from table `0x4b5df8` (set by `app+0x6c`), new one every 10 s with 1 s fades, string 131 "THE END" S = 35, and the 253-record roll `0x4b3d28` at 50 u/s; confirm after 5 s → title. See **CREDITS.md** | | |
| 0x21 | language/memory card (console leftover) | 0.6 | |

## 6. Port recipe

```c
/* --- camera: only in level 0 (House), outside the cinematic ------------------------------------------------ */
static float g_title_t;                        /* seconds since the start of action 0x49 */
void title_pose_tick(float dt) {               /* = 0x44e690, every tick in House */
    if (!g_pose || g_cin.state == 2 || g_cin.state == 3) { if (g_player) g_player->inst->fade = 0; return; }
    Vec3 P0, P1; vector_points(g_pose, &P0, &P1);                 /* type-code-5 node of slot 124, as with 1130 */
    if (!g_title_on) {                                            /* state 0 -> action 0x49 */
        player_place(g_player, P0, atan2f(P1.x - P0.x, P1.z - P0.z));   /* (1113.34, 2309.87, 1193.29), yaw 0 */
        inst_play_once(g_player->inst, 73, 3.0f, now);            /* pose; 30 s at speed 3 */
        g_title_t = 0; g_title_on = 1; g_cam.cut = 1; cam_set_mode(0x80); g_cam.anim_inst = g_player->inst;
        g_cam.letterbox = 0;
    }
    g_player->inst->fade = 1.0f;                                  /* Woody invisible (transparency 1) */
    g_title_t += dt; if (g_title_t >= 10.0f) g_title_on = 0;      /* restart at phase 0: seamless */
}
/* in cam_update, mode 0x80 with anim_inst == player: */
ins_camera_eval(inst, 73, fmodf(g_title_t, 10.0f) / 10.0f, &eye, &tgt);   /* existing function in src/level.c */
lookAt(eye, tgt, up=(0,1,0)); vfov = 83.97f; letterbox = 0;               /* NOT 68.04 / letterbox like with 1130 */
/* closed form for verification: a = 2*pi*t/10;
   eye = (-45.80 + 2500*cos(a), 3117.04, 166.71 - 2500*sin(a));  target = (529.30, 3053.06, 528.67) */
```
- After a cinematic (natural end or aborted) and after every load: `g_title_on = 0` ⇒ the circle restarts at phase 0.
- Skip the player update/input on the title (state 5 = no control); no HUD.
- The current port uses `g_pose->position` + `inst_yaw(g_pose)`; use the vector P0→P1 instead (same result here: yaw 0).

```c
/* --- 2D (640x480 virtual, hud.c font of the level bank, color white alpha 255) -------------------------------- */
logo_v = clamp(logo_v + (page <= 1 ? +5 : -5) * dt, 0, 5);       /* net effect of 0x446ac0 / 0x446b00 */
quad2d(216, 16, 209, 247,  0, 0, 209, 247,  rgba(0x80,0x80,0x80, 254 * logo_v / 5), house_rck_image[1]);
blink += dt; if (blink > 0.5f) blink = 0;  bool hide_sel = blink < 0.25f;
page 0: if (!hide_sel) text_centered(336, 30, STR(21));           /* "Press a key" */
        attract -= dt (start 35); <=0: set_var(intro,1); < -0.5: page = 0x1f (attract); input only if attract > 0
        confirm (action 0xc) or Esc released -> page 1
page 1: enter: audio_fx(63); sel = 0;   items STR(22), STR(23), STR(36), STR(2) at y = 264 + 46.5*i, centered,
        item sel hidden if hide_sel; up/down wrap (blink = 0 after a move)
        confirm: 0 -> logo_v = 0; set_var(intro, 1); next frame page = 0x1f, new_game = 1
                 1 -> logo_v = 0; iris 0.85 -> 0 in 0.5 s; then save check -> load pages
                 2 -> options (0x1b);  3 -> "Are you sure?" (0x1c, cursor on No; Yes = 0.5 s fade + quit)
page 0x1f: draw nothing; done when intro_var == 4 or action 6/9 released -> attract = 35;
        new_game ? (save_reset(), request_level(intro_var == 4 ? 0 : 0.5, 1)) : (abort intro, black, fade-in 0.5, page 0, music change)
```
Draw order: 3D → (HUD hidden) → text box 1080 → menu page (items, iris, logo) → faders.

## 7. Open questions / uncertain

1. Everything is static. Recommended check with `tools/wtrace.py`: a breakpoint on `0x44dfad` (SetMode(7) from `0x44dda0`)
   should hit every 10 s on the title; log `CamMgr+0x1d0` (camera position) to confirm the center/radius/direction.
2. The rotation direction depends on the Perso matrix (row 0 = f × up, `0x44bd30`) and on the port convention (right-handed,
   y up). The port function `ins_camera_eval` gives the same points as the hand calculation; the intro cinematic uses
   the same path and matches visually, but the title itself has not been checked against a recording of the original.
3. Whether the first start of action 0x49 falls exactly 0.1 s after the load (the Perso must be in state 0).
4. `[0x5e6194]->vt[5](0)` as an extra "back" key in menus; the default keys for actions 0xc/5 come from Woody.cfg.
5. Iris: color/alpha and whether `0x482cf0` expects virtual (640×480) or real pixels.
6. Pages 2, 3, 4, 5, 0x1b (classes `0x45dd00`, `0x45e560`, `0x45bfb0`, `0x45e1f0`, `0x4601f0`) have not been dissected; in
   particular how page 3 brings the camera to the carousel (no `SetMode` call found in that class; callers of
   `0x41f410`: `0x402a84..0x402c1d` (debug keys), `0x41e721`, `0x41fba2`, `0x445226/5f`, `0x445b66`, `0x44dfad`,
   `0x44e634`, `0x44ed6e`, `0x4562de`, `0x458ffb..0x459bba`, `0x498c20..0x499018`).
7. The field `+0x10 = 6000` in records 81/82 of `0x4b6180` (CINEMATIC calls it "prio") doesn't matter for the title.
8. Texture filtering/rounding of the 2D layer: see HUD_TEXT.md §9.5.
