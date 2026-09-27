# Model rendering (`.ins` instances): type codes, eye texture/blinking, 0xFFFF and UVs

Statically derived from `Woody.exe`: instance renderer `0x43b3f0` (sole caller `0x42f0fe` in the per-instance
draw `0x42e2b0`), polygon drawer `0x43d790`, triangle drawer `0x43e0f0`, texture-frame picker `0x47f290`,
`.ins` loader `0x427290` (points `0x427b69`-`0x427f40`, polygons `0x427fa0`-`0x4283b6`, triangles
`0x428460`-`0x4285d5`), matrix helpers `0x440fc0` (inverse with scale) and `0x4408e0` (A·B, row vectors).
Numeric verification: `tools/modeluv.py`, `tools/helperuv.py`. Node numbers below are **1-based** (as in
the file / `insparse.py`); in the C port (0-based array) that is always one lower (Woody: eyes = port nodes 62/65,
"white squares" = 64/67, helpers = 63/66).

## Recipe (what needs to change in a port)

1. **Never draw mesh nodes with type code 2.** (`0x43b6c2`: `cmp byte [node+1], 2 ; jump to next node`.) Those are
   the two white squares for Woody's eyes. They are not eyelids and are not drawn anywhere else.
2. **Skinned triangles (`Model.tris`) have explicit UVs**, no projection: the "matrix" of their material
   is a table with three UV pairs. File vertex j (j = 0,1,2 in file order) gets
   `(u, v) = (f[3j], f[3j+1])`. In the port (`t->i2` = 1st dword, `t->i1` = 2nd, `t->i0` = 3rd):
   `i2 → (m[0], m[1])`, `i1 → (m[3], m[4])`, `i0 → (m[6], m[7])`. **This is bug B** (noise on the enemies' shirt;
   model 42 in W1A has 299 textured triangles; the planar formula gives UVs of −25..+34 there).
3. **Node polygons: planar projection on the point minus the node pivot**, i.e. on the same `lp` the port already
   uses for position: `u = m0·lp.x + m3·lp.y + m6·lp.z + m9`, `v = m1·lp.x + m4·lp.y + m7·lp.z + m10`
   (pivot = that of the node the point belongs to). For most models the pivot is 0 and it makes no difference;
   for Woody's eyes and beak it does.
4. **Mesh node with a helper child (kind 0x10, `parent` = the mesh):** in the normal path the UVs of *all*
   polygons of that mesh do not come from the material but from the helper: bring the (pivot-relative) point into
   the local space of the helper node, `q = p · W_mesh · W_helper⁻¹`, and take
   mode 0: `(a,b) = (q.y, q.z)`, mode 1: `(q.x, q.z)`, mode 2: `(q.x, q.y)`;
   `u = 0.5 − a / v_8`, `v = b / v_c − 0.5` (`v_c`, `v_8` = 1st and 2nd float of the helper in the file;
   Woody: 35 and 40, mode 1). Texture = **frame 0** of the group. The helper node is animated → that's how the pupil moves.
5. **Eyelids/blinking = animation events, not a timer.** Per frame, read the event track of **node 1** of the
   model for the current animation; the last event of type 5 with `t ≤ current frame` yields four frame indices
   `e[2], e[3], e[4], e[5]` for mesh nodes with type code **5, 6, 7, 8** (no event → 0). Index 0 = eyes open (only
   step 4). Index k ≠ 0: draw the polygons of that node **twice**: one layer with texture frame `k`
   (if `k < frame_count`, else frame 0) and the normal material UVs from step 3 (eyelid, colour key), and one layer
   with frame 0 and the helper UVs from step 4 (eyeball). Eyelid on top.
   Woody's idle (anim 0, 1200 frames in 6 s): t=600 → 1, t=640 → 2, t=660 → 1, t=700 → 0: **one blink per 6 s loop,
   at 3.0 s, 0.5 s long** (half 0.2 s, closed 0.1 s, half 0.2 s).
6. **No automatic texture-frame cycling on models.** Without message 16/18 (`inst+0xd8` bits 0-2 = 0), frame 0 is
   always used, regardless of `frame_count`/`anim_duration`. So for instances bind `gl_frames[0]`
   instead of the globally cycling `gl_tex` (the override modes are in INSTANCE.md §2).
7. **Material with bit 15 = flat colour ARGB1555**, actually drawn: `R = (m>>10)&31`, `G = (m>>5)&31`, `B = m&31`,
   each `× 8/255 ×` the lit vertex colour (0..255). `0xFFFF` = white (0.973) — Woody's gloves. The many
   0xFFFF polygons in kind-4/kind-2 nodes are not drawn because the renderer only walks the mesh list (kind 0).
8. Textures must **repeat** (wrap): the UVs of Woody's eyes lie in [−1, 0].
9. **Backface culling is done by the engine itself, per polygon; the device is set to `D3DCULL_NONE`** (`0x47ec8b`),
   because polygon flag `0x2` marks a double-sided polygon that must still be drawn. Node polygons
   (`0x43bf65`): bring the camera into the node's local space and skip the polygon if
   `n·cam_local + d ≤ 0` with the plane the loader builds from the rest pose (`0x4280c2`-`0x428375`): over each
   triplet of consecutive corners P, Q, R, the triplet with the longest `n = (R−Q) × (R−P)` above 0.01, normalized,
   `d = −n·R`; if no triplet is that long → (1, 0, 0, 0). **Only the winding decides**; vertex normals are
   not consulted (they are nonsense on some models: W1A model 18, the glass elevator platform from issue #2, has every face
   twice, textured and as a reversed 0xFFFF copy, and picking by the normal sum kept the wrong one of each pair).
   Skinned triangles
   (`0x43c1a4`), each frame in world space: `n = (A−B) × (A−C)`, drawn if `n·(camera − A) > 0`.
   Without this, backfaces would also be drawn; they get `ndl = 0` and thus only the ambient term
   (0.6 × vertex colour), and precisely at the silhouette edge — where front and back have the same depth —
   the dark one can win: a dark outline around every figure.
10. **Alpha test**: `ALPHAREF = 0x7f`, `ALPHAFUNC = GREATEREQUAL` (`0x47ec50`, `0x47ec5c`), on/off per **texture**
   (the colour-key flag of the `.tex` group, `tex+0x44 & 1`), not per pass (`0x429a6b`). Filter: MAG/MIN LINEAR,
   MIP POINT (`0x47ed42`-`0x47ed62`) over 4 self-built levels (SKY.md §8; port: `GL_LINEAR_MIPMAP_NEAREST`),
   addressing WRAP except for the light spots (CLAMP, `0x429597`).
   When converting a colour-key texture, the original throws away the magenta: the texel becomes
   `ARGB 0x00000000`, i.e. **black with alpha 0** (`0x47fc1e`). If the magenta stays, the filter blends it
   with the opaque neighbours and every alpha edge gets a pink fringe.
11. **Black outline** (`0x43ea30`, fed by the two backface lists that `0x43b3f0` builds up): this is the
   ink line around the figures in the original. See §7.
12. **Draw the additive (blended-group) model faces after the fade list, in its depth buckets**, with the sort depth
   the original really uses: the global `[0x5ac8d4]` that only fading instances write (0 until the first fade). In
   practice: glow faces after all fading instances, and a fading instance's own glow after its own body. See §9.
13. **Keep every texture level as the 16-bit surface** (RGB565 as is; colour key → ARGB1555 with green truncated to
   5 bits) and build the mip levels with the original's truncating average; a colour-key mip texel is opaque only when
   all four sources are. Flat colours (bit 15) are `c5 · 8/255`. See §9 and §5.

## 1. Which nodes get drawn; type codes

`0x43b3f0` only walks the mesh list `S+0x30/0x34` (kind 0x00; `0x43b40a`-`0x43b420`, loop `0x43b67d`-
`0x43c158`). Hull (0x04), bbox (0x02), press, volume, marker and helper nodes do not occur there and are thus
never drawn, whatever their material.

Type code (`node+1`, flag bits 8-15) in the renderer:

| type code | kind | behaviour | address |
|---|---|---|---|
| 2 | 0x00 mesh | skip node (mode 1 only reserves its 16-byte light slot) | `0x43b6ae`, `0x43b6c2`; likewise in the non-draw path `0x42f170` |
| 5, 6, 7, 8 | 0x00 mesh | texture frame from event type 5: `idx = typecode − 5` in `[esp+0x90..0x9c]` | `0x43bf03`-`0x43bf3a` |
| other / 0 | 0x00 mesh | normal path | `0x43c065` |

Further comparisons against type code 2 don't exist (`cmp byte [reg+1], 2` occurs only at `0x42f170` and `0x43b6c2`).
Across all 28 levels, only type codes 2 (50×), 5 (48×) and 6 (48×) occur on mesh nodes. The other
observed codes sit on other node kinds and don't touch the renderer: press 1/4, marker 1/5/9, light 5/7,
dummy 1 (markers are looked up via `0x42f6b0(typecode, n, out)`; see FORMAT_INS.md §6).

Woody (model 0): node 63 (tc 6, pivot (−7.96, 24.98, −12), 15 polys material 0x458d) with children 65 (tc 2,
2 triangles 0xFFFF, 24.4 × 31.8 plane) and 64 (helper mode 1, 35/40); node 66 (tc 5, 0x458e) with 68 (tc 2) and 67
(helper). Group 64: 64×64, colour-key, 5 frames: **0 = eye (white + green iris), 1 = eyelid half, 2 = eyelid closed,
3 and 4 = angled "angry" eyelids** (magenta = transparent).

## 2. Frame selection: event type 5 (`0x43b58b`-`0x43b62c`)

```
N1   = first node of the node array (S+0x68)              ; 0x43b58b
ev   = N1+0x78 [anim = inst+0xb0] -> (off, cnt) in N1+0x88 ; 0x43b595-0x43b5ce
tcur = inst+0xac / (duration/4096) * nframes               ; 0x43b5ae-0x43b5bd  (0x4aa138 = 1/4096)
f[0..3] = 0
for each event (in order) while event.t <= tcur:           ; 0x43b5d7-0x43b5e3
    type 5: f[0..3] = event dwords 2,3,4,5 ; 6 dwords      ; 0x43b5f2-0x43b61d
    type 4: skip 9 dwords ; type 3: skip 15 dwords          ; 0x43b61f / 0x43b624
```
When drawing a mesh node with type code 5..8 and `k = f[typecode−5] ≠ 0` (`0x43bf2d`-`0x43bf3a`):
for each forward-facing (or double-sided, polyflag 0x2) textured polygon,
`tex = (k < tex+0x58) ? group + k·0x74 : group` (`0x43bfb0`-`0x43bfc8`) and `0x43d790(poly, tex, material, flag)`
(`0x43bfcb`); if the mesh has a helper, then once more `0x43d790(poly, material->tex (frame 0), helpermatrix)`
for the same polygons (`0x43c00c`-`0x43c056`). Flat-colour polygons are skipped in this path (`0x43bf97`).

Data (W1A, model 0, event track of node 1; 14 of 91 animations have type-5 events):
anim 0 (idle, 1200 fr / 6.0 s): (600: 1,1) (640: 2,2) (660: 1,1) (700: 0,0); anim 20/22/26: (0: 0,0) (3: 2,2)
(10 or 7: 0,0); anim 31: (0: 2,2) = eyes closed; one animation uses (4, 3) = angry look (type-5 eye = node 66 frame 4, type-6 eye = node 63 frame 3).
Other models use this too (455 type-5 events outside model 0 across all levels).
There is no random/timer blink logic in the renderer; `Perso` (`0x44a2d0`/`0x44cda0`) is not needed for this.

## 3. No auto-cycling on models (`0x47f290`)

Ordinary polygons are collected (`0x43c10b`) and then drawn with
`0x47f290(this = material->tex, &inst+0xd8, out, material, now)` (`0x43c314`-`0x43c35d`; sole caller).
`0x47f290`: `tex+0 == 0` (static group) → done; otherwise `[0x5e8688] = this` (= frame 0) and only if
`inst+0xd8 & 7 ≠ 0` and `frame_count ≠ 1` is a different frame chosen (`0x47f2bd`-`0x47f2d4`, jump table `0x47f604`).
The ctor reset `0x42e218` sets `+0xd8 &= 0xc0`. Multi-frame groups with `anim_duration > 0` therefore do **not**
cycle automatically on instances; only after message 16/18 (INSTANCE.md §2). The sentence "without an override the
normal global texture animation applies" in INSTANCE.md §2 does not hold for instances: it's just frame 0 then.
The helper path (`0x43c0e0`) and the type-code path do not call `0x47f290`.

## 4. UV generation

### 4.1 Node polygons: planar on pivot-relative points
The loader subtracts, per node, before reading that node's polygons, the pivot from all points of the node
(`0x427daf`-`0x427df9`: `P -= N+0x20..0x28`). The UV calculation in the loader (`0x4280e4`-`0x428110`) only serves
to set `P+0xc` (UV split flag, threshold 0.015 = `0x4aa288`); no UVs are stored.
The actual UVs arise per frame in `0x43d790` (`0x43da31`-`0x43da6c`), unless `poly+1 & 0x80` (flat colour):
```
P = [renderer+0x14] + idx·0x28        ; renderer+0x14 = S+0x20 (0x42e3de; 0x509adc and 0x5e86ac are the same object, 0x484068/0x48407e)
u = M[0]·P.x + M[1]·P.y + M[2]·P.z + M[3]        ; M = 3rd argument (material: f0,f3,f6,f9)
v = M[4]·P.x + M[5]·P.y + M[6]·P.z + M[7]        ; (f1,f4,f7,f10)
```
`P` is the rest-pose point relative to the pivot, not the animated point → the texture is fixed to the node.
No per-polygon flag changes the formula: polyflag 0x1 (`0x427ff8`) only drops the material link in the loader
(doesn't occur in the data), 0x2 = double-sided (no backface test, `0x43bf65`/`0x43c094`), bits 0x60 =
texture flag `&6` → mixed draw mode 3 (`0x43d7b9`). There is no separate material table for models (`level+0x5c`,
`0x43bf9c`/`0x43c314`).

Verification (`python tools/modeluv.py W1A -m 0 -n 61 63 66`): with pivot subtraction, both eyes fall within one tile and
are symmetric (L: u −0.67..−0.24, v −0.80..−0.04; R: u −0.79..−0.33, v −0.82..−0.06); without subtraction the
right eye runs past the tile edge (v −1.18..−0.41). Beak face node 61: v −0.81..−0.29 instead of −0.43..+0.08.
Enemy (model 42): all pivots 0, polygon UVs 0.01..0.99 with both formulas (each polygon has its own material).

### 4.2 Skinned triangles: explicit UVs from the material entry
Loader `0x428460`: file dwords d0,d1,d2,mat → `poly+0x1c = d0`, `+0x1a = d1`, `+0x18 = d2`. Drawer `0x43e0f0`:
`ptr = material+8` (`0x43e13c`); for engine vertex k = 0,1,2 (`0x43e39a`-`0x43e3ab`): `u = [ptr]`, `v = [ptr+0x10]`,
`ptr −= 4`. So engine vertex k: `u = mat[2−k]`, `v = mat[4+2−k]`; with `mat+0..0xc = f0,f3,f6,f9` and
`mat+0x10..0x1c = f1,f4,f7,f10` (FORMAT_TEX §1) that is: **file vertex j → (f[3j], f[3j+1])**; row 3 and column 2 of
the "matrix" are 0 for these materials. No point position is used.

Verification (`python tools/modeluv.py W1A -s 282`): model 42 (slots 282, 293, 313, 314, 396, 494), 299 triangles,
group 105: explicit u 0.28..0.98, v 0.02..0.98, max span per triangle 0.19; planar −25..+34, span 37.
Shared points: 347 times the same UV with this assignment, versus ≤ 100 for any other permutation of the three rows
(the rest are genuine UV seams). Woody has 712 triangles of which 4 are textured (group 63), the rest flat colour.

### 4.3 Helper projection (kind 0x10) — `0x43b6ce`-`0x43b908`
For each mesh node the renderer looks in the helper list `S+0x40/0x44` for a helper with `N+0x84 == mesh` (`0x43b716`-
`0x43b746`). If found: `X = 0x440fc0(W_helper, scale)` (inverse), `H = 0x4408e0: W_mesh · X` (`0x43b74d`-`0x43b798`),
and an 8-float "pseudo-material" at `[esp+0x70]` (s8 = `N+8` = 1/v_8, sc = `N+0xc` = 1/v_c, H row-major 4×3):

| mode (`N+4`) | u row | v row |
|---|---|---|
| 0 (`0x43b878`) | −s8·(H1,H4,H7), 0.5 − s8·H10 | sc·(H2,H5,H8), sc·H11 − 0.5 |
| 1 (`0x43b83f`) | −s8·(H0,H3,H6), 0.5 − s8·H9 | sc·(H2,H5,H8), sc·H11 − 0.5 |
| 2 (`0x43b7ba`) | −s8·(H0,H3,H6), 0.5 − s8·H9 | sc·(H1,H4,H7), sc·H10 − 0.5 |

In the normal path every visible polygon of such a mesh is drawn directly with this pseudo-material and
`material->tex` = frame 0 (`0x43c0d8`-`0x43c106`). Verification (`python tools/helperuv.py W1A 0 63 0`): u 0.35..0.83,
v −0.82..0.03 across all keyframes of the helper (the helper shifts between t=540 and 560 → the pupil looks sideways).
Across all levels: 96 helpers mode 1, 1× mode 2.

## 5. Material 0xFFFF / bit 15 (`0x43db83`-`0x43df09`, triangles `0x43e4c5` ff.)
`test byte [poly+1], 0x80` → texture = default texture `[0x5e8684]`, no UV calculation, and per vertex
`colour = (int)(c5 · 0.0313725 (0x4aa3e8 = 8/255) · vertex-light)` with c5 = bits 10-14 (R), 5-9 (G), 0-4 (B)
(`0x43dc65`-`0x43dd3f`; the triangle drawer the same at `0x43e5c7`). That is ARGB1555 with bit 15 as flag, not RGB565
(FORMAT_INS.md §2.4 is wrong on this point). The factor is 8/255, not 1/31: the low bits stay 0, so `0xFFFF` is
248/255 = 0.973, not 1.0 (the port's `argb1555_to_rgb` used /31 until the round of §9). 0xFFFF is thus visible
(near) white; there is no "don't draw" value.

## 7. The black outline (`0x43ea30`)

This is the ink line around Woody and the other figures. It is **not a line primitive and not a crease list**, but the
backface of the model once more, inflated: a classic back-face hull.

**Port (the two only callers, `0x43c5ac` and `0x43c5d1`, at the end of `0x43b3f0`)** over the two lists
the renderer built up during normal drawing: backward-facing triangles (`0x43c289`) and
backward-facing node polygons (`0x43c0c7`).

**Conditions** (all three in the prologue of `0x43b3f0`):

1. `inst+0xf0 & 0x20` — SetFlags bit 0x20, message 45 (`0x43b423`). The level script sets this per instance:
   2 to 51 per level (W1A 9, K2A 29, W3D 51; Blackbox and Credits none). Instance 0 (the player) has it in
   House, W1A, W3C, W3D and WWS; in W2B/W3A it is `…0001`, W2D `…0003`, W1B `…0008`.
2. `[0x4c2c0c] == 2` (`0x43b43a`) — the detail option from `Woody.cfg` (file offset 0x40), 2 in the supplied cfg.
3. The width must be positive (below).

**Width** (`0x43b447..0x43b4fe`), with `d` = distance from the camera to `inst+0x60` (the animated skeleton root,
INSTANCE.md §1.1; port `ins_anim_centre()`). **Not** the instance position: Buzz in W1A (slot 276) stands 1800 units
from the spot his cutscene (door 321) makes him walk to, and measured from `inst+0xc` he fell outside 1500 and
lost his outline (issue #35):

| d | w (world units) |
|---|---|
| 0 … 750 | `d / 300` (0 → 2.5) |
| 750 … 1500 | `5 − d/300` (2.5 → 0) |
| > 1500 | no outline |

Constants: `[0x4aa3e4] = 1/300`, `[0x4aa3e0] = 2.5`, `[0x4a9884] = 5.0`. Because `w ∝ d`, the line is up to 750
units **equally thick in screen pixels** (with the port's projection roughly `height/540` px).

**Geometry** (`0x43c49a..0x43c56a`): for each vertex of a backward-facing primitive,
`p' = M_node · ((p − pivot) + w · n)` with `n` the **normalized** vertex normal (the loader normalizes on load,
`0x427c01`; the port doesn't and has to do it itself). Only **stamped** vertices move: `0x43b3f0` first writes `0xffff0000` into `v+0x40` of the
vertex records of every back face — all three corners of a skin triangle (`0x43c3bc`, index words `+0x18/+0x1a/+0x1c`), but also only the
index words `+0x18/+0x1a/+0x1c`, i.e. **corners 0, 1 and 2**, of a node polygon (`0x43c42d`) — then the node loop `0x43c49a..0x43c56a`
rewrites the position of every stamped vertex (and sets `v+0x40 = 0xffffffff`), and `0x43ea30` draws each back face from the records.
A fourth or later corner that no other back face stamps keeps the plain position of the model pass, so the rim of such a quad tapers to
the surface at that corner. The port reproduces this quirk (`draw_outline`: pass 0 stamps, pass 1 emits).

**Colour and depth** (`0x43ecd3..0x43ed17`, `0x43edf0`): flat **black**, alpha = `2 × (1 − inst+0x6c)` clamped to
255 — with z-write on and no blending on the opaque list, that is just black; only the fade list
(`renderer+0x1c4`, if `(1−fade)·255 < 252`) actually blends. The depth is `1 − 12·rhw`, **exactly the same as the
model** (no bias, unlike the shadow which subtracts `3/65536`). Because batches are linked at the front,
the outline ends up before the model in the flush: outside the silhouette edge the hull stays visible, and where it
pokes through a hollow fold it wins the depth test — that's where the lines around a snout or a finger come from.

**What does and doesn't participate**: all backward-facing skinned triangles; node polygons only if they are not
double-sided (flag 0x2) and have no blend flags (`flags & 0x60`, `0x43c0c2`). Type-code-2 nodes and the
eyelid layer (type code 5..8, `0x43bf65`) do not participate.

**The bit only comes from the script.** Every level sends message 45 with 0x21 directly after message 1200
(SetTypeInstance) to every actor that has an outline, bosses included (W1B Buzz slot 405, W3D 775/790/801). Across all 28
levels, only these actors do not get a 0x20, and that is authentic: Woody in Blackbox/Credits/Lang, the final boss of
W2B (type 12) and the three ghosts of W3B (type 13). The port used to set the bit itself on every actor class because Buzz
stood without a line; that, however, was the distance bug above (his animated root stands 3400 units from
`inst+0xc` in W1B), and that hack is gone. `WOODY_SHLOG=1` writes one line per second for every drawn actor that does not get
an outline, with the distance and the reason (no bit 0x20, or beyond 1500).

## 8. Fading instances (`inst+0x6c`, messages 56/57): the list `+0x1c4`

Example: the disappearing platforms at the end of W1B (type 70, model 1, slots 229/230/232, fade speed
57 = 0.6/s). Before this round the port only drew the glow faces with `1 − fade`; everything else stayed opaque
until `fade > 0.98` and then vanished in a single frame.

- `0x43b504`: `alpha = (1 − inst+0x6c) · 255` (`[0x4a900c] = 1`, `[0x4aa308] = 255`); **`alpha < 252`**
  (`[0x4aa3dc]`) sets `[esp+0x2c] = 1` and computes the sort depth `[0x5ac8d4]` = camera-z of the `.ins` position
  `inst+0xc` (row `+0x11c/+0x12c/+0x13c/+0x14c`), clamped to ≥ 0 (`0x43b528..0x43b56a`).
- `0x43bdc4`: every vertex gets `v+0x30 = alpha`; `0x43d926` writes that as diffuse byte 3. The device is set to
  ALPHAOP MODULATE, ALPHAARG1 TEXTURE, ALPHAARG2 DIFFUSE (`0x47ed72..0x47ed91`), so final alpha = texture × vertex.
- `[esp+0x2c]` is the batch mode (4th arg of `0x43d790`, also used for the outline `0x43ea30` via `0x43c59d`):
  0 = list `+0x1c0` (opaque), 1 = list `+0x1c4` (fading); blend faces always get mode 3 (`0x43d7c8`).
- `0x428d00` (after the transparent world buckets 11/8/3 in `0x4293f0`): deepest = max(1, all batch depths of
  `+0x1c4/+0x1c8/+0x1cc`); bucket = `round(depth · 254 / deepest)` (`[0x4aa2f0] = 254`), batches of the same
  instance (`batch+8`) stay together; drawn from bucket 255 to 0 (far to near). Per bucket:
  1. ZWRITE on, SRCBLEND ZERO, DESTBLEND ONE (`0x428f10..0x428f45`): depth only;
  2. SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA (`0x428fdd..0x428fff`), ZWRITE stays on; ZFUNC is globally
     LESSEQUAL (`0x47ec44`), so only the frontmost surface of the object blends (no interior faces visible);
  3. ALPHATESTENABLE per texture = colour-key bit `tex+0x44 & 1` (`0x428f6b`): a colour-keyed texture
     thus already vanishes below alpha 127, the rest fades to 0.98.
- The mode-3 batches (`+0x1cc`) go through the same buckets, but with the depth the last fading instance left behind
  in `[0x5ac8d4]` (§9).
- The shadow of a fading caster (`0x42e69a`/`0x42eb7a`, path `0x4388e0`): see §8.1.

### 8.1 The cast shadow of a fading instance (`0x4388e0`)

`0x42e2b0` picks the shadow drawer per receiving polygon by `inst+0x6c > 0.01` (`[0x4a94f8]`, `0x42eb7a`):

- `≤ 0.01` → `0x4385f0(poly)`: flat AMB (`[0x5e86ac]+0x1b0`), specular 0, blank texture `[0x5e8684]`, `u = v = 0.5`, bucket **5**.
- `> 0.01` → first `C' = light colour (light+0x30..0x38) · inst+0x6c` (`0x42e6b7..0x42e6e2`, once per instance), then per polygon
  `0x498830(tmp, receiving poly, &light+0x0c, light+0x2c)` (`0x42ebaf`, the same sphere projection the world light pass uses, LIGHTING.md §1.3)
  and `0x4388e0(poly, tmp, &C')` (`0x42ebc8`).

`0x4388e0` (`ret 0xc`) clips the projected polygon against the frustum exactly like `0x4385f0` (`0x438cc0/0x438f00/0x439130/0x439340`),
computes `u/v` per vertex from the two rows of `tmp` applied to the projected world point (`0x438a32..0x438a6f`), and emits:

| field | value | address |
|---|---|---|
| diffuse | `(int)(C'·k)` per channel packed as `0x00RRGGBB` (alpha byte 0), `k = tmp+0x20 = 1 − abs(n·L + d)/R` | `0x438b65..0x438bc5` |
| specular | AMB `[0x5e86ac]+0x1b0` | `0x438c75` |
| texture | light texture `[0x5e8678] + 0x74·(15 − round(k·15.49))` | `0x438ba9..0x438bf3` |
| bucket | **2** (`push 2`, `0x438c86` → `0x42b460`) | |

Bucket 2 is flushed after the light polygons (bucket 4) and before the opaque shadows (bucket 5), blend off, z-write off,
SPECULARENABLE on (LIGHTING.md §1.5), so the shaded pixel becomes `AMB + lighttex · C · k · fade`: the chosen light's own contribution
comes back in proportion to the fade (fade 0.01 = a full shadow, 0.98 = almost none; above 0.98 the instance is not drawn at all,
`0x42e374`). Like the opaque variant it overwrites whatever the other lights had added there.

**Port** (`cast_shadow` in `src/render_gl.c`): the fading casters are drawn first (bucket 2 before 5), through the same stencil as the
opaque ones, in two passes so overlapping caster triangles still write each pixel once (the original's blend-off overwrite): flat AMB
(stencil 1 → 2), then `light_tex[15 − round(k·15.49)] × C·fade·k` added ONE/ONE (stencil 2 → 3), `u/v` from the sphere projection of the
receiving face with `U` towards its third vertex (the light textures are radially symmetric, so the choice of `U` does not show).
Test: W1B, slots 273..277 (type 70, SetFlags 1, the casting platforms at (−6818, 1775, −1359) ff.):
`--cam -7000 3200 -1100 0 -60`, `WOODY_MSGAT="0.3 57 273 800; 0.3 56 273 50; …"` (same for 274, 275, 277).

## 9. The additive list `+0x1cc` in `0x428d00`, and the 16-bit texture surfaces

**Sorting.** Every model polygon of a blended group (polygon flags `0x60`) goes to mode 3 (`0x43d7c8`), i.e. list
`+0x1cc`, whatever the instance's own mode is. A batch is the current run of polygons with the same (texture, mode,
`[[0x53a0f8]+4]` = the instance being drawn) (`0x43dbac..0x43dbbb`); a change of key links the old batch at the front
of its list and bump-allocates the next one (`0x43dbc4..0x43dc56`), so batch addresses grow in creation order.
At the end of each append (`0x43e0d3`, triangles `0x43eec7`, outline `0x43ea1d`) the batch copies its sort depth
from the global `[0x5ac8d4]` into `batch+0x10`. That global has exactly one writer, `0x43b56a` (a byte search for
`d4 c8 5a 00` over the image finds only these four uses), reached only for a fading instance (alpha < 252). It lives
in `.bss` (`.data` has 0x12000 raw bytes from `0x4b1000`), so it starts at 0 and is never reset, not even on a
level change.

Consequences:
- The sort is **per instance** (per batch of one instance), never per face.
- A fading instance's own glow faces get its own depth and thus land in the same bucket as its fade batches, and are
  drawn **after** them (per bucket: fade depth-only, fade blend, ZWRITE off, `+0x1c8` SRCALPHA, `+0x1cc` ONE/ONE,
  `0x428efc..0x42922c`).
- A glow face of an instance that is not fading gets the depth of the last fading instance drawn before it (in this
  frame or an earlier one): until the first fade of the session that is 0, so every glow face sits in **bucket 0**, the
  last one drawn: after the whole fade list. Bucket 0 merges `+0x1c8` and `+0x1cc` by batch address (`0x429240..0x4293e0`:
  `cmp ebx, edi; jae` → the lower address first).
- `deepest` is scanned over all three lists, so a stale glow depth can stretch the buckets of the fade list.
- Among themselves the ONE/ONE batches commute (no z-write, the framebuffer clamp `min(1, a+b)` is associative for
  non-negative terms); only their place against the fade list (z-write on) and `+0x1c8` is visible.
- ALPHATESTENABLE follows the colour key bit of the texture here too (`0x4291c4`); no blended group of the 28 levels
  has bit 0, so it is off. (With it on, the diffuse alpha 0 that `0x43d91d` writes would discard every pixel.)

**Port** (`rnd_frame`, the block after `post_models`): pass 1 no longer draws model faces; one loop over the drawn
instances in the original's draw order (the Perso, then the instance list, §9.1) keeps a static `sort_depth` = `[0x5ac8d4]` (updated by every fading instance), puts
fading instances on the fade list and every instance of a model with a blended mesh polygon (`Renderer.model_blend`)
on the additive list, both with the current `sort_depth`; then buckets `round(d · 254 / max(1, all d))` from the
highest down: fade depth-only, fade blend, additive faces (ONE/ONE, alpha test off). Measured (W1A, WWS, `WOODY_PROF=1`):
no change in the instance time (~1 ms) beyond noise. Visible difference: only where a glow face meets a fading
instance. W1A with the fans (model 2, haze group 66) set to 50 % (message 56 to slots 11/13/14/15): before, the haze was
added first and the half-transparent blades were blended over it; now the haze is added after the blades and washes
them out, as in the original. A frame without a fading instance is bit-identical to before (K1A, S1A, WWS start
frames with a fixed time step); W1A's start frame has one (slot 486, model 49, an all-glow pickup fading in) and only
its pixels change (the glow behind its fading body is now depth-rejected).

### 9.1 The instance order that feeds the stale depth

"Drawn before it" is the order of the world draw `0x42b380`: the Perso first (`vtbl[2](4 or 6)`, `0x42b396..0x42b3a9`), then
every entry of the frame's instance list `world+0x64` in list order, the Perso skipped (`vtbl[2](5 or 7)`, `0x42b3b2..0x42b3ed`).
The list is built by `0x42a980` → `0x42a840` (INSTANCE.md §4.1), and its order is fixed by the **sector chains**:
* `0x42ab60`: the pairs of the camera's `.vis` entry in file order; each pair's sector once (sector stamp `+4`), and for it
  `0x42a840` walks the chain `sector+0x44 → inst+0x24` from its head, reading `next` before it handles the instance
  (`0x42a85b`), appending whatever passes (floor group, message-34 link, cached-sphere frustum / race distance).
* The chain is a stack: `0x407790` links an instance **in front** (`0x4077da..0x4077dd`), `0x407850` unlinks it
  (`+0x1c = +0x18 = −1`, `0x407889..0x407897`), and the re-cell `0x4077f0` is unlink + link in front. The loader links the
  `.ins` objects in file order (`0x4288cf`; cameras too, `0x428a4a`), so a chain starts as the reverse file order. Every
  SetTypeInstance **1200** of the level script's init then unlinks the old object (`0x40351c`) and links the new class object
  in front (`0x403e7a`), in message order.
* **A clock run re-cells - unless the pose cache hits.** `0x42e2b0` with arg bit 0 runs the clock `0x43eee0` (`0x42e310`), which
  works at most once per frame (`inst+0x58 == [[0x509adc]]`, `0x43eeee`) and ends, unless the instance has flag 0x20, with
  `inst+0x60` = the animated root and `0x4077f0(inst+0x60)` (`0x43f2ed..0x43f351`). But when the instance is opaque (fade
  `+0x6c < 0.01`, `0x42e2cc`), `0x42e2b0` first asks the **pose cache** `+0x7c` (`0x42f3d0`: clock speed `+0xa0 == 0` and the
  record's position, animation position `+0xac` and slot0 `+0xb0` equal to the instance's) and hands a hit to the clock as
  `ext`; without a TRAJ the clock then copies the cached matrices (`0x43efff..0x43f048`) and **returns before the re-cell**
  (`0x43f06e`). The draw stores that cache (`0x42ecf8 → 0x42f490`, fade ≤ 0.98) whenever the speed is 0 (and `+0x84 ≠ 0`,
  `0x42f460`) and clears it otherwise (`0x42f483`). The list build calls `vtbl[2](0x81)` for every instance it appends that has
  no flag 0x20 (`0x42a94b..0x42a95b`), so each listed instance whose clock really runs (animating, moving, fading, or not yet
  drawn with its current pose) goes to the front of its chain **while the walk goes on behind it**; a stationary, opaque one
  keeps its place. So only the animated instances of a chain alternate between two orders every frame; after the first
  frame of a level (which reverses every chain once, nothing being cached yet) a static scene's list is **stable**.
  **Verified live** (`tools/wverify.py --probe list`, W1A start, 16 consecutive frames at 15 s and the first 8 frames):
  frame 1 lists 77 instances in the chain order of the load (`300 299 298 296 295 308 294 …`), frame 2 the once-reversed
  order (`294 308 296 298 299 300 295 …`), and from then on the 70 entries stay put except the looping pairs 11/12, 14/15
  and 77/79, which swap every frame. (The earlier reading here - "the whole list alternates every frame" - missed the
  cache branch; the port reproduced that wrong reading until 2026-09-26.)
  Actors and the links of messages 61/62 (flag 0x20) keep their place unless their own mover re-cells them.
* Before the walk, the message-34 loop (`0x42aa0b`) runs `vtbl[2](1)` = the clock on every type-1 object of the camera's
  kd leaf, in `.col` order (`0x4271e0` list `cell+0x40/+0x44`, `0x42aa0b..0x42aa2e`), so those move to the front first.

So which fading instance last wrote `[0x5ac8d4]` before a given glow instance depends on the sectors' `.vis` order, the
chain history and the frame parity; a glow batch of a non-fading instance between two fading ones can flip buckets every
frame.

**Port** (`rnd_instance_list` in `src/render_gl.c`): `Renderer.chain` holds one chain per sector (`Instance.cell_next`,
`chain_sec1`), built in `.ins` order on the first list of a level at the `.ins` positions, then the 1200 relinks in message
order (`rnd_note_link`, `Instance.link_seq`, `chains_relink`); `chains_sync` unlinks hidden instances, links shown ones in
front (at the `.ins` position), re-cells a moved actor (its own `0x4077f0`) in front, and leaves every other instance where
its last clock put it (INSTANCE.md §4.2); the camera leaf's `.col` objects are clocked first (`rnd_load_col`, `.col` loaded
by `level_load`); the walk follows the `.vis` pairs and `chain_clock` (once per frame, `Instance.clock_frame`) re-links every
listed non-0x20 instance in front unless its pose cache hits (`Instance.pc_ok`, `pc_pos`, `pc_ac`, `pc_slot`: stored by the
list's draw when the speed is 0 and fade ≤ 0.98, used when fade < 0.01 and there is no TRAJ). The collision queries of the
frame clock the instances they test too (`gel_col_clock`, `0x4324d6`), which re-links them for the next frame's list.
The sort loop of the fade / additive buckets then
takes the Perso, the list in order, and last the few instances the port draws outside the list (model order). `WOODY_VISLOG=4`
prints the whole list every frame; at the W1A start it equals the original's (above) except: the `.ins` cameras (295, 285,
1, 284) are in the original's list but are not instances in the port, and a few entries whose start history the port does not
model sit elsewhere in their chain. Before the query clocks were ported (at 15 s): lasers `196 198 292 197` in the original,
`196 197 198 292` in the port; `13 30 45 17 29` / `13 17 29 30 45`; `34 36 481 35 33` / `33 34 35 36 481`; 37 before 77/79
in the port, after them in the original. With them: `292 196 198 197`, `13 29 30 45 17`, `33 34 35 36 481`, and 37 after
77/79 as in the original (not re-checked live).

**Texture surfaces** (`0x47fa60`). The file's RGB565 goes through `0x47f090(v, 0)` to ARGB8888 with **the low bits 0**
(`r5 << 3`, `g6 << 2`, `b5 << 3`), the colour key test (`0x47fc0e`), and back through `0x47f170` to the surface format:
`[0x5e8690]` (`0x40283b..0x40287b`: 0 = RGB565 if the device offers it, else 1 = X1R5G5B5, 2 = A4R4G4B4) or, for a
colour-key texture, 3 = ARGB1555 (`0x47fae9`). The low-bits-0 value is only an intermediate: an RGB565 texture reaches
the device **bit for bit** as in the file, a colour-key texture loses the lowest green bit and keeps one alpha bit.
The widening that the screen finally sees is the sampler's; the port uses bit replication (D3D7-era hardware and all
current hardware). The mip levels (`0x47fd83..0x47fe17`) are where the low bits matter: the four source texels are
decoded the same way (alpha bit of format 3 → `0x80`, `0x47f127`), averaged per channel **with truncation**
(`Σ (p & 0xfcfcfc) >> 2`, alpha `(Σ (p >> 2) & 0x3fc00000) & 0xff000000`) and packed with truncation again. So every
level loses up to 3/4 of a 5-bit step (distant surfaces a little darker, mean −0.2 of 255 on a W1A frame, at most 22
on single pixels), and a colour-key mip texel is opaque **only when all four sources are** (4 × `0x80` / 4 = `0x80`
keeps the alpha bit, 3 × `0x80` / 4 = `0x60` does not; SKY.md §8 had "3 of 4"). Port: `tex16_texel`,
`tex16_halve`, `tex16_widen` in `render_gl.c` keep each level as the 16-bit surface and widen only for GL. Visible:
colour-key foliage at a distance thins out slightly (W2A palms).
## Uncertain
- Draw order of the two eye layers: `0x43d790` doesn't draw directly but fills batches per (texture, mode)
  (`renderer+0x1b8`, lists `+0x1c0`); a closed batch is linked at the front, so the later-closed
  eyeball batch is presumably drawn first and the eyelid after. The flush itself was not traced; logically the
  eyelid should be on top (same depth → in a port `GL_LEQUAL` or polygon offset).
- Purpose of the type-code-2 faces (never drawn, no other reader found): presumably an editor/export leftover.
- `0x440fc0` is only fully correct for uniform scale (transposed rotation × 1/s²); for instances with
  scale ≠ 1 the helper UV may deviate from "local helper space".
- Type code 7 and 8 (f[2], f[3]) do not occur in the data on mesh nodes; event dwords 4 and 5 are 0 everywhere.
- Wrap/clamp state of the texture was not read from the D3D calls; the data (UVs in [−1,0]) requires repeat.
- How world polygons (`.gel`) pick their frame has not been investigated here (`0x47f290` is only called
  for instances).
- Where `[0x4c2c0c]` is written was not found: the block comes as a whole from `Woody.cfg`.
