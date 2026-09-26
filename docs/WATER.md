# WATER.md — class 60, the water volume

Static analysis of instance class 60 (`size 0x158`, ctor inline in `0x403440` at `0x403c79`, vtable `0x4a9194`) and the
port in `src/water.c`. Background: in W2A the water didn't move and Woody didn't drown in it. Four W2A instances
(183..186, model 25) get `SetTypeInstance 60` and immediately afterward message 1506; in total 16 levels use the class
(K2A/K2R/K3A/K3R/KWS, S2A/S2R/S3A/S3R/SWS, W2A/W2B/W2D/W3A/W3B/WWS).

| vtable | address | what |
|---|---|---|
| [1] | `0x473120` | Init: build the grid over the top (only if `+0x14c` is set, otherwise base `0x42e210`) |
| [2] | `0x4738c0` | Draw(bits): bit 1 = center/radius for visibility (`+0x88..+0x98`, average of the 8 box points × 0.125), bit 4 = draw the surface |
| [3] | `0x4747f0` | Update: wake effects and the drowning test |
| [22] | `0x474a40` | handler: 29, 33, 35 ignored (returns 0), rest to the base `0x42d5e0` (33 is what the scripts send, §1.1) |
| dtor | `0x474250` | frees the five grid buffers if `nc·nr < 0x190` |

The model (W2A model 25) is a box: node 0 mesh (12 triangles, top and bottom with texture group 56), node 1 child 2
(bbox), node 2 child 1 (press). **The box itself is never drawn** (Draw replaces the model renderer; `0x42e2b0` is only
called with bit 1) and **never collides**: Init sets `inst+8 |= 0x40` (`0x473876`).

## 1. Message 1506 SetWaterVolumeParameter (`0x46ce38` → `0x474690`)

`1506 [inst, a, b, c, d]` → `+0x13c = a·0.01` (cell size), `+0x140 = b` (**integer**, not ×0.01: number of cells along
the short side), `+0x144 = c·0.01` (wave amplitude), `+0x148 = d·0.01` (alpha), `+0x14c = 1`, then `vtable[1]` (Init).
MESSAGES.md had b as ×0.01; that's wrong (`0x46ce4e`: `mov ecx,[eax+0x10]` pushes it onto the stack unscaled).
W2A: `[1000000, 2, 3500, 40]` for 183..185 (cell 10000, 2 cells, 35, 0.40), `[130000, 4, 2000, 50]` for 186; the assert text
of the class cites that same second set as an example.

### 1.1 Message 33 `[inst, n, v]`: sent, but dropped

Every water volume of K3A (185, 186, 304, 372; model 34), S3A (178, 179, 311, 379; model 31) and W3A (216, 217, 364,
443; model 32) gets, right after `1200 [., 60]` and `1506 [., 130000, 2, 2000, 50]`, the four messages
`33 [., 4, 200]`, `33 [., 13, 0]`, `33 [., 14, 133]`, `33 [., 15, 0]`; W2B 30 and 47 (model 3) get `33 [., 4, 300]` (50
sends). They look like "set parameter n to v/100" of a richer water model, but nothing reads them: the class handler
`0x474a40` (vtable `0x4a9194` slot 22, installed by the SetTypeInstance case `0x403c79..0x403ca3` before the 1506/33
messages arrive) returns 0 for ids 29 (`0x1d`), 33 (`0x21`) and 35 (`0x23`) without calling the base, and the base
`0x42d5e0` would drop 33 as well (byte table `0x42df80[32]` = 21 → the default `0x42df1c`). No other class handler
receives message 33 from a level script, so the four parameters have no meaning left in the exe. The port handles it as
an explicit no-op (`case 33` in `on_msg`).

## 2. Init `0x473120`

1. `M = 0x42f7e0(0, 0, &M, 1)`: the track of the **first regular top node** (starting at `S+0x6c`, next sibling as long as the
   flags ≠ 0) at animation 0, phase 0, times the instance matrix (with scale). The boxes have their real position in that track: W2A 183
   is thus 340 lower than its `.ins` position (top y = −89, bottom of the lake −1000, the jetty 114).
   With only the instance matrix, the surface was above the jetty and Woody drowned at the start.
2. Corners: from each polygon of the first mesh node (`S+0x34`, 1-based) with **normal-z ≥ 0.1 in node space**
   (`[poly+0x10]`; the boxes are modeled z-up and stood upright by the instance) the first three indices, unique, up to 4.
   The first polygon without bit 15 in the material supplies the texture (`+0x110`).
3. `0x4742f0(c, 4)` half-sorts: pass 1 `i = 0..2`: swap `c[i]`↔`c[0]` if `P[i].x < P[0].x && P[i].y > P[0].y`; pass 2
   `i = 1..2`: swap with `c[1]` if `x > && y >`; pass 3 swaps `c[2]` with itself. `c[3]` is never examined.
4. `A = M·P[c0]`, `E1 = M·P[c1] − A`, `E2 = M·P[c3] − A`; `len1 = |E1|` (`+0x11c`), `len2 = |E2|` (`+0x120`);
   `+0x13c = min(+0x13c, len1, len2)`.
5. `n1 = round(+0x140 · len1 / +0x13c)`, `n2` likewise with len2 (fistp, at least 1); `nc = n1+1` (`+0x114`), `nr = n2+1` (`+0x118`).
   `nc·nr > 400` → `"Un volume d'eau a trop de face"` and done (never in the data).
6. Vertex `k = j·nc + i`, `s = i/(nc−1)`, `t = j/(nr−1)`: `v = A + s·E1 + t·E2 + (0, 10, 0)` (`+0xfc`),
   `uv = (s·len1, t·len2) / +0x13c` (`+0x100`: the texture repeats every `+0x13c` units), phase `= ftol(rand·512)` (`+0x10c`).
7. Triangles per cell: `(k, k+1, k+nc)` and `(k+nc, k+1, k+nc+1)` (`+0x108`).
8. Center `+0x130 = v[0] + 0.5·E1 + 0.5·E2`; `inst+8 |= 0x40`; wake timer `+0x154 = rand·5`.

## 3. Draw bit 4 (`0x473a58..0x474229`)

Tables in the subsystem `[0x5e823c]` (filled at `0x40248f`/`0x402520`): `+0` = `cos(i·2π/512)`, 512 entries;
`+0x900` = `0.5·cos(i·2π/512)^8`, 128 entries.

* **Light** (`0x474128`): `d` = normalized xz direction camera → center, `L = len1 + len2`;
  sun point `+0x124 = center + (L·d.x, 0.25·L, L·d.z)` — seen behind the water from the camera.
* **Color per vertex** (`0x474490`): `a = norm(camera − v)`, `b = norm(v − sun)`, mirrored `r = (b.x, −b.y, b.z)`;
  `a·r < 0` → value 1, otherwise `|a × r|` (sine of the angle). RGB = `table900[round(value·127)] + 0.4`, alpha = `+0x148`.
  A glint line where the reflected sun hits the eye: 0.9 dead-on, 0.4 outside it.
* **Two layers**, each all triangles; first layer B, then A. Only **interior points** move (not row 0, not the last row,
  not `k % nc == 0`, not `(k+1) % nc == 0`), with `c = cos[ph & 511]`, `s = −cos[(ph+128) & 511] = sin`, amplitude `+0x144`:
  A = `(x + c·amp, y, z + s·amp)`, uv; B = `(x − c·amp, y − 5, z − s·amp)`, `uv + (0.23, 0.85)`. The two layers thus rotate
  against each other; with the coarse grids of W2A (3×5, 3×7, 3×3, 12×8) the effect is mostly a swirling texture.
* Triangles via `0x472040` (clip) to **texture list 8** (`0x42b460(tex, 8)`): in the flush `0x4293f0` blend on,
  SRCALPHA/INVSRCALPHA, **z-write off**, after the models (list 11) and before the additive list 3 and the fade list; MODULATE2X
  (LIGHTING.md §1.5). Color bytes = `float·255` (`0x4aa308`). No culling.
* Afterward, per vertex `phase += ftol((rand·30 + 250)·dt)` (fistp, rounding): ~265/512 revolution per second, only in
  frames where the water is drawn.

## 4. Update `0x4747f0`

1. `+0x154 −= dt`; below 0: wake at a random point `v0 + rand·len1·norm(v1−v0) + rand·len2·norm(v_nc−v0)` (the
   first triangle), height `v0.y`, direction `(cos a, 0, sin a)` with `a = ftol(rand·512)`: `0x472ec0(pos, dir)`;
   then `+0x154 += rand·2`.
2. Drowning: `p = Perso.pos + (0, 120, 0)` (`0x4abcb4`); `0x4746e0` transforms p into node space (`0x440fc0`, the
   current node matrix) and tests all faces of the mesh node: everywhere `n·p + d ≤ 0` → inside. Then `+0x14d = 1`,
   **`0x44d160` = `Perso->vtbl[0x98](7)` = Kill(7)** and `+0x150 = 3.0`. Every frame again; the Perso ignores it if it's already
   drowning. Feet 120 below the surface is thus death: in W2A that's y < −209.

### 4.1 Wake (`0x472ec0` → `0x472f50` → `0x473050`, particle list `[0x5e823c]+0xdb8`, max 2000)

Emitter: lives 1.0 s, moves 300 units/s (`0x4a986c`) along `dir` and leaves a mark every 0.1 s.
Mark: lives 0.5 s, `p = age/0.5`; sprite `0x470f10` with **flags 6** (`0x473088`: own color + rotation, no billboard →
flat on normal (0, 1, 0), additive) and mode `S+0x260 = 0x12` (`0x4730e5`, the square), image `0x1003a` = bank 0 image 58,
color 0.8, alpha `(1−p)·0.7`, half-diagonal `10 + 70·p`, rotation `+0x224` = the record's random `+0x14` (`0x473108`) —
applied, but invisible because image 58 is round (the port leaves it out).

## 5. Port (`src/water.c`)

* `water_add` on `SetTypeInstance 60`, `water_param` on 1506 (Init, with `M` = `node_world` after `ins_pose(inst, 0, 0)`),
  `water_update` every non-paused frame (phases, wake, Kill(7) via `player_kill`), `water_draw` via the new hook
  `Renderer.post_models` (after the model passes, before the fade list), `water_fx_draw` for the effect sprites (`hud_world_fx_plane`,
  fx slot 11 = image 58). `draw_instance` skips type 60.
* The wake and the drowning test run only while the water instance is in this frame's instance list (`game_enemy_thinks`):
  the Update vtbl[3] `0x4747f0` is called from `0x42b400` for the listed instances only (INSTANCE.md §4.1).
* Deviations: the phase runs on as a float (no ftol per frame, so independent of framerate) and also off-screen (the
  original advances it in the Draw `0x4741d6`, i.e. only in drawn frames);
  the inside test uses the Init matrix instead of the current one (no water box is animated); MODULATE2X via
  `GL_COMBINE` + `RGB_SCALE 2` (without that extension: color ×2, clamped).
* Test: `extract/Data W2A` (start on the jetty, water all around); drowning `--pos -700 200 500` (Kill 7 around 0.6 s,
  respawn); animation `WOODY_SHOTSEQ="out/wq 1.0 0.25 6" --cam -600 300 -600 200 -35`; `WOODY_WATERLOG=1` = grid per volume
  and, every second, the number of wakes/marks.

## 6. Open

* The water splash `0x478660` (from Kill(7) and from message 1505 `[inst, f]` = `0x478660(&inst.pos, 1000, f·0.01)`; 45× in 7 levels,
  W2A 17×) is worked out in **SPLASH.md** and ported (`game_splash`, fx kinds 3..6 in `fx_update`, `case 1505`, both Kill(7)
  branches of `player_kill`).
* `+0x14d` and `+0x150` (3.0) after drowning are write-only: the only accesses in `0x472e00..0x474b00` are the writes at
  `0x474a0c` and `0x474a23`.
* Not compared against the running original.
