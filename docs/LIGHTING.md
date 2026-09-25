# Lighting and shadow (`.lit`) – how the original draws it

Static analysis of `game/Woody.exe` (nothing verified against the running game).
File format: see `FORMAT_TEX_COL_VIS_LIT.md` §4; this document describes what the engine does with it
per frame and corrects that format document on three points (§2).
Data check: `python tools/litparse.py W1A` (per-light statistics + 5 checks, §6).

**Core idea.** The engine uses a *multipass lightmap in the framebuffer*:

```
pixel = 2 · texture · vertex-colour · ( AMB + Σ_lights  C/255 · max(0, 1 − |P − L| / R) )      AMB = 76/255 ≈ 0.30
```

where the sum only runs over lights that **see** the point. Visibility is precomputed:
list A = faces the light fully sees, list C = the lit *pieces* of
faces that are partly in shadow. Shadow is thus not a dark polygon; shadow is
the place where the additive light polygon is **missing**. The sharp polygonal edges of
the platform shadows in W1A are the edges of the C polygons. Faces hit by no light
at all get, in a single pass, `texture · vertex-colour · 0.6` (= 2·AMB, so seamless).
There is **no N·L** on the world, only linear distance falloff.

## Recipe (in order)

1. **Loading.** Per light: position `L`, colour `C` (floats 0..255), range `R`, lists A, B, C,
   cell ranges, BSP. Also load the table the format document calls "probes" as an
   **extra vertex table**: index `i < 0` in a C polygon = record `−i−1` (position = the first
   3 floats; the u32 after it is unused). Also load the trailer (lights per cell) for step 5.
2. **Face flags per frame.** `lit[face] = 0`; for each light, for each cell range whose
   cell is visible: all faces of A and B → `lit[face] = 2`. (A port without
   cell visibility may do this once at load time for all lights.)
3. **World, unlit faces** (`lit == 0`): one pass, `texture × vertex-colour × 0.6`,
   texenv MODULATE **1×** (not 2×), opaque.
4. **World, lit faces** (`lit == 2`), four steps in this order (all faces per step,
   since the framebuffer is the accumulator):
   1. *Ambient fill*: face without texture, flat colour `0x4C4C4C` (76,76,76), opaque,
      z-write on.
   2. *Light pass*: `glDepthMask(0)`, `glEnable(GL_BLEND)`, `glBlendFunc(GL_ONE, GL_ONE)`,
      wrap **CLAMP**, MODULATE 1×, depth test LEQUAL. Per light, per visible cell range:
      each face of **A** (whole face) and each polygon of **C**. Per polygon with plane
      `(n,d)`:
      - `dist = n·L + d`; if `|dist| ≥ R` → skip; backface (camera behind the plane) → skip;
      - `k = 1 − |dist|/R`; colour of *all* vertices = `((int)(C.r·k), (int)(C.g·k), (int)(C.b·k))`;
      - texture = radial glow no. `i = 15 − round(k · 15.49)` (16 textures 32×32, §1.4);
      - UV: `F = L − n·dist`, `r = sqrt(R² − dist²)`, `s = 0.5/r`,
        `U = normalize(V2 − F)` (V2 = **third** vertex of the polygon), `W = n × U`,
        `u = 0.5 + s·(P − F)·W`, `v = 0.5 + s·(P − F)·U`.
      - Net (texture × colour) that is exactly `C/255 · max(0, 1 − |P − L|/R)`; the choice of U
        doesn't matter since the texture is rotationally symmetric. A port may just as well
        generate a single 2D glow texture per `i` or compute the formula per pixel/vertex.
   3. *Cast shadows from instances* (optional, §4): opaque polygons (blend off,
      z-write off) in colour `0x4C4C4C` that overwrite the summed lights again.
   4. *Texture pass*: the same faces again with their own texture and vertex colour (1.0×),
      `glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR)` (= 2 · src · dst), z-write off, depth test
      LEQUAL/EQUAL.
   Everything transparent/additive (water, effects, models with alpha) comes after this.
5. **Models (Woody, enemies, instances)**, §3: pick one light per instance from the
   light list of its cell; per model part a smoothed light vector
   `Ldir = 0.85·Ldir + 0.15·normalize(L − p)·(1 − |L − p|/R)` if the part is seen
   by that light (BSP query), otherwise just `Ldir *= 0.85`; vertex colour
   `= vcol·0.3 + max(0, N·Ldir)·C` (scale 0..255), drawn with MODULATE **2×**. In
   shadow a figure thus sinks to `0.6·vcol` over ~10 frames.
   **Exception: a face of a blended texture group (flag bit 1) gets no lighting.** `0x428020`
   copies group flags 1-2 at load time to polygon flags 0x20/0x40, `0x43d7b9` tests `0x60` and
   sets `[0x5ac8d8] = 1`, and `0x43d91d` then skips the lit RGB at `v+0x24..0x2c`: it writes
   `0x00iiiiii` with `i = (int)(alpha · 0.5)` (`0x43d9a4`) and `alpha = (1 − inst+0x6c) · 255`
   (`0x43b504`). For an instance that isn't fading out that's `i = 128`, under MODULATE 2× thus exactly
   `1.0 × texture`. Neon signs, the red cross / green arrow next to a door and the light bars above them
   are therefore **always equally bright**, wherever they are. The intensity byte `tex+0x46` is never read.
   Only model faces do this: `.gel` world polygons never get the flags (`0x42801a` is in the
   `.ins` loader) and skinned triangles neither (`0x43e107` sets `[0x5ac8d8] = 0`). In the data of all 28
   levels, no world polygon or skinned triangle is in a blended group.
6. There is **no blob shadow** (§4); the shadow under Woody is real model geometry projected
   from the light, and only when the detail option is on (§5).

## 1. World

### 1.1 Frame order (`0x401ab0`)

| Address | Call | What |
|---|---|---|
| `0x401922` | `0x4843e0(0.3)` | per frame: `renderer+0x1ac = 0.6` (factor for unlit faces), `renderer+0x1b0 = 0x4C4C4C` (`(int)(0.3·256)` = 76 in R, G and B) = **AMB**. Renderer = `[0x5e86ac]` = `[0x509adc]` (same object, `0x42a451`/`0x48407e`) |
| `0x401d78` | `0x42b400` | update objects; for visible light objects (kind 2 = a `.lit` light record, `0x40ae60`) `0x474a90`: lens flare with sightline test `0x497ed0` – no effect on world lighting. **Unreachable in the shipped game**: `0x474a90` only walks the table `0x5e8428` (64 slots), which is only ever filled by the handler of message **1510** (`0x46cf02`) – that message does not occur in any of the 28 level scripts (all other ids 1500..1511 do). The table therefore stays empty and the function always returns immediately at `0x474abe`. Do not port |
| `0x401d85` | `0x42abc0` | `lightsys+0x28[face] = 0` for all faces of the visible sectors |
| `0x401d91` | `0x42b380` | draw instances; shadow-receiving faces also get flag 2 here (`0x42eb73`, `0x42ecbc`) |
| `0x401d99` | `0x42b4e0` | **light pass** (unconditional, not gated by an option) |
| `0x401da1` | `0x42ac10` | base faces via `0x42b6c0(face)`; skips flag `== 1` (`0x42ad0b`), but no writer of 1 was found (only 0 and 2) |
| `0x401756` | `0x4293f0` | flush of all buckets with the render states (§1.5) |

### 1.2 `0x42b4e0` – light pass

Loop over all lights (`lightsys+0x04`, 0x40 B per light; `S = light+0x3c`). Per cell range
(0x1c B, `S+0x04`) only if `range.cel` is in the list of visible cells
(`renderer+0x58`, count `+0x54`; `0x42b548`).

| List | Code | Action |
|---|---|---|
| A (`S+0x0c`) | `0x42b56b` | once per light per frame (stamp `face+4 == [0x4c4c24]`): flag `= 2`; `0x498830(tmp, face, &light+0x0c, light+0x2c)`; `0x42c320(face, tmp, &light+0x30)` |
| B (`S+0x14`) | `0x42b5e3` | flag `= 2` only – nothing drawn |
| C (`S+0x1c`) | `0x42b60f` | `0x498830(tmp, poly, pos, range)`; `0x42c320(poly, tmp, colour)` |

### 1.3 `0x498830` / `0x498871` / `0x498890` – project the light sphere onto the plane

`dist = n·L + d` (`0x49883c`); `dist ≥ R` or `dist ≤ −R` → `tmp+0x20 = 0`, done.
Otherwise `s = 0.5 / sqrt(R² − dist²)` (`0x498890..0x4988b4`, `[0x4a9014] = 0.5`),
`F = L − n·dist`, reference vertex = index at `poly+0x24` (third index; negative → extra
vertex, `0x49891a`), `U = normalize(V − F)`, `W = n × U` (`0x498a0e..0x498a83`).
Output: `tmp+0x00..0x08 = W·s`, `tmp+0x0c = 0.5 − F·(W·s)` (row for `u`);
`tmp+0x10..0x18 = U·s`, `tmp+0x1c = 0.5 − F·(U·s)` (row for `v`);
`tmp+0x20 = k = 1 − |dist|/R` (`0x498b54..0x498b7c`).

### 1.4 `0x42c320` – emit the light polygon

- Backface test against the camera (`0x42c33c`), transform + clip against the four
  frustum planes (`0x42c910/0x42cb90/0x42ce00/0x42d070`).
- Vertex `i < 0` → `lightsys+0x0c + 0x30·(−i−1)` (`0x42c3a2`, `0x42c7cf`); the 0x30-byte
  records have the same layout as a world vertex (position, transformed `+0x0c`,
  screen `+0x18`, clip flags `+0x24`, frame stamp `+0x2c`).
- Colour `(int)(C.r·k)<<16 | (int)(C.g·k)<<8 | (int)(C.b·k)` for all vertices
  (`0x42c5da..0x42c63d`); `u,v` from the two rows of `tmp` (`0x42c78b..0x42c89c`).
- Texture `[0x5e8678] + 0x74·(15 − round(k·15.49))` (`0x42c621..0x42c66a`,
  `[0x4aa304] = 15.49`), bucket **4** (`0x42c8ee`).

Light textures: 16 × 32×32, created in `0x426f2b` (`0x47f870(i)` → `0x480090`):
`a = i/16` (`[0x4abd9c] = 0.0625`), `px = ((x + 0.5 − 16)/16)·sqrt(1 − a²)`, `py` likewise,
`d = min(1, sqrt(px² + py² + a²))`, `grey = (1 − d) · (i ≠ 0 ? 1/(1 − a) : 1) · 255`.
Texture `i` is thus the cross-section of the linear light sphere at height `a·R`, normalized
to its maximum `1 − a`; multiplied by the vertex colour `C·k` (`k ≈ 1 − a`) that gives
`C · (1 − distance/R)`.

### 1.5 `0x42b6c0` – base face, and the buckets in `0x4293f0`

`0x42b6c0` looks at the flag (`0x42ba31..0x42ba3f`):

- flag ≠ 2 (`0x42bef9`): one polygon, vertex colour × `renderer+0x1ac` (0.6; `0x42bfb5`…),
  own texture, bucket **10** (`0x42c2ec`).
- flag = 2: (a) polygon with the blank texture `[0x5e8684]`, colour `renderer+0x1b0`
  (AMB), `u = v = 0.5`, bucket **10** (`0x42ba45..0x42bb99`); (b) polygon with own
  texture and unattenuated vertex colour, bucket **1** (`0x42bb9e..0x42bef4`).

Bucket `n` = list `tex+8+4n` (`0x42b460`). Order and states in `0x4293f0`
(D3D7 render states: 0x0e ZWRITEENABLE, 0x0f ALPHATESTENABLE, 0x13 SRCBLEND, 0x14 DESTBLEND,
0x1b ALPHABLENDENABLE, 0x1d SPECULARENABLE; TSS 1 = COLOROP, 0x0c = ADDRESS):

| # | Bucket | Address | States | Content |
|---|---|---|---|---|
| 1 | 0, 10 | `0x4294ad..0x429564` | COLOROP MODULATE(4), blend off, zwrite on, ADDRESS WRAP | opaque world: unlit faces and the AMB fill |
| 2 | **4** | `0x429572..0x429600` | zwrite off, ADDRESS **CLAMP(3)**, blend on, SRC = **ONE(2)**, DEST = **ONE(2)** | light polygons (A and C) |
| 3 | 2 | `0x42960e..0x42965e` | blend off, zwrite off, SPECULAR on | semi-transparent instance shadow: `AMB (specular) + light texture × C·k·…` (§4) |
| 4 | 5 | `0x42966c..0x429696` | blend off, zwrite off, SPECULAR off | instance shadow: flat AMB (§4) |
| 5 | **1** | `0x4296a4..0x429732` | ADDRESS WRAP, blend on, SRC = **DESTCOLOR(9)**, DEST = **SRCCOLOR(3)** | texture pass of the lit faces = 2·src·dst |
| 6 | – | `0x429740` | if `device+0x20 ≠ 0`: COLOROP = **MODULATE2X(5)** for everything after this | |
| 7 | list `+0x1c0`, 11, 8 (SRCALPHA/INVSRCALPHA), 3 (ONE/ONE), `0x428d00`, 9 | `0x4297ae..0x429a07` | | models, transparent, effects |

## 2. Lists A, B, C and the extra vertices (corrections to the format document)

| What | Meaning | Evidence |
|---|---|---|
| **A** | faces the light fully sees; the whole face gets the light polygon | `0x42b56b`; data: light is always in front of the plane and within range, centroid according to the BSP always lit (W1A 5385/5385) |
| **B** | **parent faces of the C polygons** (partly lit); only needed to give them flag 2 so they get drawn in multipass. Not "shadow casters" | `0x42b5e3`; data: every C polygon is coplanar with a B face, A ∩ B = ∅ |
| **C** | the lit remainder polygons of B faces, pre-clipped by the light build tool | `0x42b60f` |
| C field `+0x08` ("face") | **not a face index**: it sits where the material word of a `.gel` face would be and is, in ~93% of cases, exactly the material word of the parent; the light pass never reads it | `0x42c320` never reads `poly+8`; `litparse.py` check 3 |
| **extra vertices** (negative indices) | **stored in the file**: it's the table the format doc calls "probes" (`lightsys+0x08/+0x0c`, 16 B in the file → 0x30 B in memory). Index `i` → record `−i−1`. So they are not light samples; the u32 "colour" is not used by any reader found | `0x42c3a2`, `0x42c7cf`, `0x49891a`; data: all negative indices < record count and every point lies on the plane of its polygon |
| fully shadowed faces | occur in no list → flag 0 → `vcol × 0.6` | |
| BSP (`S+0x20/+0x24`) | shadow BSP for **point** queries by models (§3), not used for the world | `0x40b540`: only called from `0x42e463`, `0x42f20c`, `0x43ba38` |

## 3. Dynamic objects (Woody, enemies, instances)

All in the instance draw function `0x42e2b0`/`0x42e374` (arg bits: 2 = cast shadow,
4 = draw model) and the model renderer `0x43b3f0`.

**Light choice** (`0x42e3e4..0x42e573`): light list of the instance's **sector**
(`lightsys+0x10[inst+0x1c]` = `{n, index…}`). `n == 0` → no light (bit 2 drops out).
`n == 1` → that light. `n > 1`: per light `f = 0x40b540(S, inst+0x60)` (the animated skeleton root, INSTANCE.md §1.1;
the port takes `ins_anim_centre()` + 20 up as its sole reference point, also for the light direction, which the original determines per part):

- `f == −1` and `|L − p|² < R²` → this light, done (`0x42e4c2`);
- `f ≠ −1` and `plane(f)·p > 0` (point in front of the leaf face = lit) → this light, done (`0x42e541`);
- otherwise (in shadow): remember the light with the largest (least negative) plane distance; that
  is chosen if no light sees the point.

> **Sector, not cell.** The trailer of the `.lit` (FORMAT_TEX_COL_VIS_LIT.md §4, "lights per
> cell") has, in all 28 levels, exactly as many lists as the `.gel` has **sectors**, not
> as many as there are cells: House 33 lists / 33 sectors / 2530 cells, W1A/WWS/W3D/K1A 128
> lists / 128 sectors / 6245-7691 cells. `world+0x20` is thus the sector count and `inst+0x1c`
> the sector index; the sector of a point comes from `0x4081c0` (descend the main kd-tree until a
> node carries a sector index), not from the leaf-cell query `0x408180`. Indexing with the cell would
> read far past the table. This was measured, not read from the disassembly.

**Point query `0x40b540(S, p)`**: walk from node 0; `plane·p + d > 0` → `front`, otherwise
`back`; child `& 0xF`: 0 = node `>>4`, 1 = leaf with face `>>4`, otherwise −1. Interpretation
by all three callers: **lit ⇔ result −1 or p lies in front of the plane of the
leaf face** (`0x43ba3d..0x43ba80`, `0x42f211..0x42f254`). (Checked with `litparse.py` against the
centroids of A faces and C polygons: 100% / 99.9% lit.)

**Light vector per model part** (`0x43b912..0x43bc4c`; cached variant for stationary
instances `0x42f110`, without damping):

```
p      = world position of the part (matrix translation +0x24..+0x2c)
Ldir  *= 0.85                                   ; [0x4aa3d8], every frame
if a light was chosen and the point is lit (query above) and |L − p| < R:
    Ldir += 0.15 · normalize(L_local − p_local) · (1 − |L − p|/R)     ; [0x4aa1c8]
    Lcolour = C (floats 0..255)                 ; part+0x0c..+0x14
```

Storage: 0x18 B per part at `inst+0xf4 + partoffset` (`Ldir` 3 floats, colour 3 floats).

**Vertex colour** (`0x43bce4..0x43bdbd`): `ndl = N·Ldir` (vertex normal `+0x10`, vertex colour
`+0x1c..+0x24`):
`out = vcol · renderer+0x1ac · 0.5 (= vcol·0.3) + (ndl > 0 ? ndl·Lcolour : 0)`, per channel,
then clamped. Models are drawn after step 6 of §1.5 (MODULATE2X), so effectively
`0.6·vcol + 2·ndl·C`. There is no separate per-instance "colour"; it's contained in the per-part
light vector. `[0x5ac850]` (1/2) then still adds `[0x5ac854]` to the colour (`0x43bdce`;
flash/highlight, not investigated).

## 4. Shadow under figures: no blob, but projected geometry

- No blob texture is in use: the two candidates `[0x5e867c]` (64×64
  alpha gradient, `0x480310`) and `[0x5e8680]` are only ever written, never read.
- Instead: arg bit 2 of `0x42e2b0` (`0x42e651..0x42ec3a`). With the chosen light, the outline
  of each model part is determined (`0x43aaa0`), clipped against the world via the light BSP
  (`0x40bb40`, `0x40bda0` → list of receiving polygons `{n, kind, face, n×16 B}`), and
  every vertex is projected from the light onto the face plane:
  `P' = L + (P − L) · (−(n·L + d)) / (n·(P − L))` (`0x42eac1..0x42eb53`). The receiving
  face gets flag 2 (`0x42eb73`).
- Drawing: `inst+0x6c` (transparency) ≤ 0.01 → `0x4385f0`: flat colour AMB, blank
  texture, bucket 5 (opaque, overwrites the summed lights → after the texture pass it looks
  like an unlit face, i.e. *all* lights gone, not just the chosen one).
  Transparency > 0.01 → `0x4388e0`: bucket 2, light texture, diffuse = `C·k·transparency`,
  specular = AMB (shadow gets lighter as the instance fades). Transparency
  > 0.98 → instance not drawn at all (`0x42e374`).
- No height fade or ground finder: the shadow falls wherever the projection hits a face; the
  range is implicitly that of the light.
- There is **nowhere a test whether the chosen light sees the caster**. At `n == 1` that light is
  taken without any test (`0x42e422`); at `n > 1` there is always the fallback choice
  (`0x42e524`); only an **empty** sector list removes bit 2 (`0x42e56a`). A figure standing in
  shadow thus still casts a shadow, from that fallback light. The whole draw function is also
  skipped entirely if `inst+0x1c == -1` (no sector, `0x42e2c3`).

### What the port does differently (`cast_shadow`/`draw_cast_shadows` in `src/render_gl.c`)

| Original | Port |
|---|---|
| caster = outline (`0x43aaa0`) of the **hull nodes** (`S+0x3c`, node flag 0x04; Woody 43 of 142), pre-filtered with the outline of the bbox node | all polygons of every mesh node + all skin triangles, projected per triangle |
| receivers from the **light BSP** (`0x40bb40`/`0x40bda0`), already clipped to convex polygons | lists A and B of the light, clipped with the stencil buffer. A ∪ B is not the same set: a fully shadowed face is in neither |
| only tests: leaf kind ≠ 2, ≥ 1 vertex in front of the receiving plane, camera in front of that plane | plus made-up bounds (`s > 40`, a sphere-radius `reach` test, `k` outside 1..100). They exist because the port walks A/B instead of the BSP, and can drop valid shadows |
| `0.01 < transparency ≤ 0.98` → translucent shadow (bucket 2, `C·k·transparency`) | always the opaque AMB variant up to 0.98 |
| a caster without animation reuses its polygons (`0x42f3d0`/`0x42f460`) | recomputed every frame |
| the caster is **not** tested for visibility: `0x42b380` calls `0x42e2b0` with bit 2 on for every instance in `world+0x64` | same (issue #29); the port only skips receiving faces that aren't drawn this frame, which is exact |

## 5. Detail option `[0x4c2c0c]`

| Value | Effect | Address |
|---|---|---|
| 0 | player drawn with arg 4 (no cast shadow); instances arg 5 | `0x42b380..0x42b39d` |
| ≠ 0 | player arg 6 (shadow + model); instances with SetFlags bit 1 (`inst+0xf0 & 1`) and kind 1: arg 7 instead of 5 | `0x42b3a2`, `0x42b3cc..0x42b3df` |
| 2 | plus the effect pass of SetFlags bit 0x20 (see `INSTANCE.md` §6) | `0x43b423` |

The world light pass (§1) and the model lighting (§3) do **not** depend on the option. This
also answers open point 2 of `INSTANCE.md`: the "extra pass against table `[0x4c4cac]+4`" is
the cast shadow, and that table is the light table.

## 6. Check against W1A (`python tools/litparse.py W1A`)

7 lights, 1987 extra vertices, 20382 faces. Per light, among others: light 0 `R = 2500`,
colour (168,236,255), A = 1626, B = 232, C = 304; light 4 `R = 2450`, A = 570, B = 299,
C = 442. Checks: (1) 0 negative indices outside the extra table; (2) 0 extra vertices
outside their polygon's plane; (3) 0 C polygons without a coplanar B face, A ∩ B = ∅
(C field `+8` = material word of that B face in 1538 of 1654 cases); (4) 0 A faces with the light behind
the plane or out of range; (5) BSP query: C centroids 1653/1654 lit, A 5385/5385.

## 7. Dynamic point lights `0x498790` — registered, never drawn

**Result: the original has no visible dynamic lights.** The registration function exists and seven places call it, but
the table it fills is never freed and never read by any draw path. After the first 16 registrations of a level every
further call fails. Verified statically (every load of `[0x4c4cac]` and every access to `lightsys+0x14/+0x18` enumerated)
and by a live trace of the original (§7.4).

### 7.1 The light system object (`0x2c` bytes, `[0x4c4cac]`, ctor `0x40ac30`)

Created in the level loader at `0x4044fa` (`new(0x2c)`, `0x40ac30(name, 0x10, 0x400)`), destroyed at `0x404a99` (`0x40abd0`), so
**once per level load**.

| Offset | Content | Written | Read |
|---|---|---|---|
| +0x00 / +0x04 | static light count / records (0x40 B, §1.2) | `.lit` | light pass, model light choice |
| +0x08 / +0x0c | extra vertices (§2) | `.lit` | `0x42c3a2`, `0x498923` |
| +0x10 | light list per sector (§3) | `.lit` | `0x42e409` |
| **+0x14** | **dynamic light capacity = 16** (ctor arg 2, `push 0x10` at `0x404517`) | `0x40b375` | `0x498791`, `0x42f068` |
| **+0x18** | **dynamic light records**, `16 × 0x2c` B, all `+0x00 = 0` (`0x40b3a3` loop) | `0x40b37f` | `0x49879b`, `0x42f075` |
| +0x1c / +0x20 / +0x24 | capacity 1024 (ctor arg 3), count 0, `int[1024]` | ctor only | **nobody** (a planned list, never used) |
| +0x28 | per world face flag 0/2 (§1.1) | `0x42abf6`, `0x42b5a1`, … | `0x42ba3c` |

Dynamic record (`0x2c` B): `+0x00` in use (0/1), `+0x04` kind, `+0x08..+0x10` position, `+0x14..+0x1c` colour (floats,
0..255), `+0x20` radius, `+0x24..+0x2b` never written or read.

### 7.2 `0x498790(this = lightsys, int kind, const vec3 *pos, const vec3 *rgb, float radius)`, `ret 0x10`

```c
int DynLight_Add(LightSys *ls, int kind, vec3 *pos, vec3 *rgb, float radius) {
    int i;
    for (i = 0; i < ls->dynMax /*+0x14 = 16*/; i++)          /* 0x4987a0: first record with +0 == 0 */
        if (ls->dyn[i].used == 0) { ls->dyn[i].used = 1; break; }   /* 0x4987b5 */
    if (i == ls->dynMax) return 0;                            /* 0x4987bc: table full */
    ls->dyn[i].kind = kind;  ls->dyn[i].radius = radius;  ls->dyn[i].pos = *pos;  ls->dyn[i].rgb = *rgb;
    return 1;
}
```

**Nothing ever sets `used` back to 0**: the only other writer of a record is the ctor. There is no per-frame reset, no
removal function, no expiry. No caller looks at the return value.

### 7.3 The only reader: `0x42f05c` in the instance draw function `0x42e374` — a dead end

Every time a model is drawn with arg bit 4, after its bounding sphere (centre `c`, radius `r`; cached in
`inst+0x88..+0x98` for instances that do not move, `0x42eec9..0x42f038`) is known:

```c
for (i = 0; i < ls->dynMax; i++)                              /* 0x42f075 */
    if (ls->dyn[i].used == 1 && |c - dyn[i].pos|^2 < dyn[i].radius^2 + r^2)   /* 0x42f07e..0x42f0bd; note R^2 + r^2, not (R + r)^2 */
        ctx.dynList[ctx.dynCount++] = i;                      /* 0x42f0bf: [esp+0x1b738] count, [esp+0x1b73c] list */
Model_Draw(&ctx, flags, ...);                                 /* 0x43b3f0 */
```

The list lives in the draw context `ctx = esp+0x330` handed to `0x43b3f0`, at `ctx+0x1b408` (count) / `ctx+0x1b40c`
(indices). **No instruction in the exe reads `ctx+0x1b408..+0x1b47b`**: the model renderer reads only `ctx+0x1b404` (the
vertex buffer), `ctx+0x1b47c` (a static light was chosen) and `ctx+0x1b480` (its index). The per-vertex colour of
`0x43bce4..0x43be20` (§3) has no loop over further lights. The world light pass `0x42b4e0` loops over the static lights
only (`lightsys+0`, `+4`); `0x42c320` is only called from it and `0x498830` only from it and from the cast shadow
(`0x42ebaf`, with the chosen static light). So a registered light changes nothing:
not the world, not the models, not the cast shadows.

### 7.4 Live check (original, W3D class 16)

`python tools/wdynlight.py game out/trace/dyn_w3d.txt` (ISO mounted; breakpoints `0x498790`, `0x4987bc`, `0x42f0bf`; SetVar 316 = 433, 315 = 9, Woody teleported
to the arena): the first 16 calls, all from class 16's appear state (`0x40cce7`) at (6791, −2385, −19918), white,
radius 400, took slots 0..15 within 0.04 s; **every later call (1542 of 1558 in ~8 s: appear, taunt, throw, vanish and the
wave records `0x40c6dc` with radius 15..400) returned 0 "full"**, and the table stayed `1111111111111111` until the end of
the run. `0x42f0bf` appended ~28000 entries in that time — into the list nobody reads.

### 7.5 Callers (all pass kind 0 and white (255, 255, 255))

| Call | Function | Position | Radius | Port |
|---|---|---|---|---|
| `0x40c6dc` | class 16 wave record `0x40c610` (BOSS15_16.md §5) | the column | `(1 − t/0.9) · 400` | `boss.c` `wave_tick` |
| `0x40cce7` | class 16 state 2, appear | his column | 400 | `boss.c` |
| `0x40cd9d` | class 16 state 3, taunt | his column | 400 | `boss.c` |
| `0x40cea4` | class 16 state 4, throw | his column | 400 | `boss.c` |
| `0x40d1da` | class 16 state 5, vanish | his column | `(1 − t/1.5) · 400` | `boss.c` |
| `0x4766ee` | explosion kind 0 record `0x4765f0` (bomb, BOMB.md §4.3), while `u = t/0.25 < 1` | the explosion | sprite size + 100 = `500·sin³(2πu) + 100` (signed: −400..600) | `main_engine.c` `bombs_draw` (one frame late: registered in the effect draw after `rnd_frame`) |
| `0x477dc6` | Woody's skeleton effect `0x477980` (PERSO_DEATH.md §4.2), every frame | the last sprite drawn | `rnd · 100 + 200` | not ported (the effect itself is not) |

### 7.6 Port

`rnd_light_add(kind, pos, rgb, radius)` in `src/render_gl.c` (declared in `render_gl.h`) keeps a 16-slot table like the
original, but **emptied after every drawn frame**, so the callers above (which all register every frame) always get a
slot; `WOODY_DYNLOG=1` logs each registration and, when drawn, the faces each light touches. By default nothing is drawn —
that is the original's picture. `WOODY_DYNLIGHT=1` is a **port extra** that draws them the way the engine's own static
lighting would have, had the list of §7.3 been wired up:

* world: every face drawn this frame with the light in front of it (`0 < n·L + d < R`), not back-facing the camera,
  not in an additive group, gets one more additive pass (ONE/ONE, depth LEQUAL, no z-write): face texture × vertex colour
  (unit 0) × radial light texture `15 − round(k·15.49)` placed by the sphere projection of `0x498830` (unit 1), vertex
  colour `min(1, 2 · vcol · C/255 · k)`, `k = 1 − (n·L + d)/R`. That adds `2 · tex · vcol · C/255 · max(0, 1 − |P − L|/R)`,
  the term of the recipe's formula, also on single-pass (unlit) faces, but without the `min(1, …)` of the framebuffer sum
  and without a shadow test (a dynamic light shines through walls within its radius).
* models: per drawn instance, each light within `R` of its reference point (animated centre + 20, as §3) adds
  `2 · max(0, N·Ldyn) · C` to the vertex colour before the clamp, `Ldyn = normalize(L − p) · (1 − |L − p|/R)` (the static
  light vector of `0x43b912` without the 0.85 smoothing).

Cost measured in the class-16 fight: +0.1 ms per frame.

## Uncertain

- ~~Dynamic lights~~: resolved in §7 — registered, never drawn.
- `light+0x28` (2, once 3) and the type byte of `object_id`: no reader found in the
  light pass.
- C field `+0x08`: 7% deviates by a few units from the material word of the parent
  (presumably a renumbered material table after the light build); functionally irrelevant. The port looks for the real parent itself (coplanar B face containing the centroid, `lit_c_parent`): using the field as a face index linked 0 of the 7209 C polygons in W1A/W1B/House to their parent, causing the lit half of floors to flicker on and off with the visible sectors (issue #29).
- Flag value 1 in `lightsys+0x28` (tested in `0x42ad0b`, never set).
- `[0x5ac860]` = 1 with vector `[0x5ac864..0x5ac86c]` (`0x42ed16`): alternative
  light direction for models (menu/cutscene?), and `[0x5ac850]/[0x5ac854]` (colour summation)
  are not investigated.
- The exact clamping/rounding of the model vertex colour after `0x43bdbd` and the details of
  the outline/clip functions `0x43aaa0`, `0x40b8f0`, `0x40bbc0` (only needed for cast
  instance shadows) are not worked out.
- Whether `device+0x20` (MODULATE2X support, `0x429745`) is set on every card; if not,
  models are half as bright. Assumption in this document: set.
- Framebuffer saturation: the summation clamps to 1.0 before the ×2 texture pass; a port that
  computes the formula in a single pass must take `min(1, AMB + Σ)` to get the same result.
