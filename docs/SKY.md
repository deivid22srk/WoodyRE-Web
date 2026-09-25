# The sky (sky box) – texture flag `0x200`

Static analysis of `Woody.exe` (`out/disasm_full.txt`); nothing verified on the running game.
Numerical check on the data: `python tools/skycheck.py [LEVEL ...]` (§5).
Related documents: `FORMAT_TEX_COL_VIS_LIT.md` §1 (group flags), `FORMAT_GEL.md` §1
(polygons), `LIGHTING.md` §1.5 (buckets and render states of `0x4293f0`).

**Core idea.** "Bit `0x200`" is not a bit but **byte 1 of the group flags `== 2`**
(`cmp byte [tex+0x45], 2`, `0x42acea`). A `.gel` face whose material points to such a group is
**never drawn**; it is only a switch: "the sky can be seen from here". If, in a given frame,
at least one such face is in the visible set, the world renderer `0x42ac10` draws five fixed
quads at the end: a **cube with half-edge 50000 around the camera position** (four sides +
top, no bottom), each with its own texture.
So there is no per-face frame selection, no UV generation from the material, the normal, or the
vertex positions, and no time animation: the group's five "frames" are the five cube faces.
The material's UV matrix (`material_uv`) is not used for these faces (in House it would give
u −134..67, v −63..38: the fine stripes of the port).

## Recipe for the port (short)

1. On load: `sky_group` = the group with `((flags >> 8) & 0xff) == 2` (at most one per
   level). Leave all `.gel` polygons whose material points to that group **out of the
   world batches** (not drawn, not lit). Collision stays as it is.
2. Source of the five textures: if the **level bank** (`Data/<LVL>/<LVL>.rck`) has **≥ 5
   images** (type 1), then images **3, 0, 1, 2, 4** of that bank; otherwise the group's
   frames **0, 1, 2, 3, 4** (in this order for the quads A..E below).
3. Per frame, if the level has a sky group: five quads around the camera position `C`,
   `S = 50000`:

   | quad | plane | frame | bank img. | v0 | v1 | v2 | v3 |
   |---|---|---|---|---|---|---|---|
   | A | `z = C.z+S` | 0 | 3 | (−,−,+) | (−,+,+) | (+,+,+) | (+,−,+) |
   | B | `x = C.x+S` | 1 | 0 | (+,−,+) | (+,+,+) | (+,+,−) | (+,−,−) |
   | C | `z = C.z−S` | 2 | 1 | (+,−,−) | (+,+,−) | (−,+,−) | (−,−,−) |
   | D | `x = C.x−S` | 3 | 2 | (−,−,−) | (−,+,−) | (−,+,+) | (−,−,+) |
   | E | `y = C.y+S` | 4 | 4 | (−,+,+) | (−,+,−) | (+,+,−) | (+,+,+) |

   (signs = `C ± S` per axis x,y,z). UVs, the same for every quad, with `hu = 0.5/width`,
   `hv = 0.5/height` of the **group's** texture (§2.1, even when bank images are shown):
   `v0 = (hu, hv)`, `v1 = (hu, 1−hv)`, `v2 = (1−hu, 1−hv)`, `v3 = (1−hu, hv)`.
   Note: **v = 0 (the topmost image row) lies at `y = C.y − S`**; the side images are thus
   stored upside down in the file (dark blue at the top of the file = below the horizon in
   the world, clouds at the bottom of the file = above the horizon). §5 proves this at the
   seams with the top face.
4. States: opaque, no blending, no alpha test, white color (no vertex color, no
   `.lit` lighting, NO 0.6 factor), texenv MODULATE 1×, no culling, no fog,
   bilinear. The original draws with z-test LEQUAL and z-write on at a distance of 50000 (so
   behind all level geometry). For the port, equivalent and more robust is: **draw first
   with `glDepthMask(0)` + `glDisable(GL_DEPTH_TEST)`**; then the cube size does not
   matter. If you keep the original size, the far plane must be > `50000·√3 ≈ 86603`
   (the port has `zf = 200000`, which fits).
5. The sky does not rotate with the camera (translation only): the cube is axis-aligned in
   world space and is simply transformed by the view matrix like anything else.
6. The time animation of world groups (`rnd_frame`, `tg->gl_tex = gl_frames[time…]`) can be
   dropped: the only `.gel` faces with a multi-frame group are, in all 28 levels, the sky faces (§4.2).

## 1. Where it happens: `0x42ac10` (base faces of the world)

`0x42ac10(this = renderer)` is called per frame from `0x401da1` (`LIGHTING.md` §1.1).

| Address | What |
|---|---|
| `0x42ac1d..0x42ac75` | frame counter `[0x4c4c24]++`; all polygons of the visible groups (`renderer+0x54/+0x58`, `gel+0x38`) get that stamp in `poly+4` |
| `0x42ac94..0x42ad2f` | loop over the visible sectors (`renderer+0x48/+0x4c`, `gel+0x24`) and their polygon lists (`sector+0x08/+0x0c`); only polygons with this frame's stamp, stamp then set to `−1` (each polygon exactly once) |
| `0x42acd3..0x42acd9` | `material & 0x8000` → skip |
| `0x42acdb..0x42ace6` | `tex = [level+0x5c + material·0x24 + 0x20]` = pointer to **frame 0** of the material's group |
| `0x42acea` | `cmp byte [tex+0x45], 2` – byte 1 of the group flags. Equal → `[esp+0x10] = 1` (sky seen), `ebx = tex`, **`jmp 0x42ad19`: the face is not passed to `0x42b6c0`, so not drawn** (`0x42acf0..0x42acfa`) |
| `0x42acfc..0x42ad14` | all other faces: `lightsys+0x28[face] == 1` → skip, otherwise `0x42b6c0(face)` |
| `0x42ad35` | after the loop: `cmp [esp+0x10], 1`; not equal → done. Otherwise the sky box (§2) |

Consequences:
- The test is **equality with 2**, not a bit test. Byte 1 `== 3` (W3A group 77 `0x00ff0300`,
  S3A group 75 `0x00ff0368`, K3A group 76 `0x00ff0310`; 32×32, 17 frames) is therefore **not**
  sky, see §4.1.
- `ebx` is the group of the *last* sky face seen. In the data there is at most one sky group per
  level (`tools/skycheck.py`), so this does not matter.
- The sky box only appears if a sky face is in the visible set (sector/group visibility from
  `.vis`). A port without `.vis` culling simply always draws it if the level has a sky group;
  interior spaces are closed off, so it would not be visible there anyway. (Uncertain: whether there
  are spots where the original shows the clear color through a gap instead of the sky.)
- The sky faces are also not lit: they do not go through `0x42b6c0` and thus not into buckets
  1/10. In the data, no sky face appears in a `.lit` list A or B (checked for
  House, K1R, K2R, K3R, WWS, W2A, W2B, W2D, W3C, W3D), so no light polygon is produced either.

## 2. The cube: `0x42ad40..0x42b373`

Four vertices of 0x44 bytes on the stack (`esp+0x18`, `+0x5c`, `+0xa0`, `+0xe4`; after the
`push 0x40` at `0x42adf2`, all offsets are 4 higher in the listing). Layout of such a vertex,
read from `0x439540`: `+0x00` position (world), `+0x0c/+0x10/+0x14` view space,
`+0x18/+0x1c` screen, `+0x20` 1/w, `+0x24/+0x28/+0x2c` color r,g,b (0..1), `+0x30` alpha,
`+0x38/+0x3c` u,v, `+0x40` clip codes.

| Address | What |
|---|---|
| `0x42ad43..0x42ade2` | `+0x24..+0x30` of all four vertices `= 1.0` → white color, alpha 1 |
| `0x42ad40`, `0x42ad63` | `hu = 0.5 / (float)[tex+0x38]` (`[0x4a9014] = 0.5`, width of the **group's** texture, even when bank images are used afterwards) |
| `0x42adfc`, `0x42ae0d` | `hv = 0.5 / (float)[tex+0x3c]` |
| `0x42adf4..0x42ae4d` | UVs, set once and reused for all five quads: v0 `(hu, hv)` (`0x42adf8`, `0x42ae13`), v1 `(hu, 1−hv)` (`0x42ae03`, `0x42ae1f`), v2 `(1−hu, 1−hv)` (`0x42ae34`, `0x42ae3f`), v3 `(1−hu, hv)` (`0x42ae46`, `0x42ae4d`); `[0x4a900c] = 1.0` |
| `0x42ae0a`, `0x42ae54` … | camera = `[renderer+8]`, position at `+0x90/+0x94/+0x98` (the same position that `0x439540` uses for its backface test, `0x43958d`); each corner = camera position ± `[0x4aa2f8]` = **50000.0** per axis |
| `0x42ae54..0x42af4e` | quad **A**, plane `z+S`: v0 (−,−,+), v1 (−,+,+), v2 (+,+,+), v3 (+,−,+) |
| `0x42af53..0x42b051` | quad **B**, plane `x+S`: (+,−,+), (+,+,+), (+,+,−), (+,−,−) |
| `0x42b056..0x42b15a` | quad **C**, plane `z−S`: (+,−,−), (+,+,−), (−,+,−), (−,−,−) |
| `0x42b15f..0x42b264` | quad **D**, plane `x−S`: (−,−,−), (−,+,−), (−,+,+), (−,−,+) |
| `0x42b269..0x42b36e` | quad **E**, plane `y+S`: (−,+,+), (−,+,−), (+,+,−), (+,+,+) |

There is no sixth quad: nothing is drawn below the horizon (`y−S`).

### 2.1 Texture choice per quad

Before each call: `cmp [0x5e8670], 5` (`esi = 5`, `0x42aded`; tests at `0x42af1f`, `0x42b023`,
`0x42b126`, `0x42b22f`, `0x42b339`).

| quad | `[0x5e8670] < 5`: group frame | `[0x5e8670] ≥ 5`: level bank image |
|---|---|---|
| A | `ebx + 0` = frame 0 (`0x42af32`) | `[0x5e8674] + 0x15c` = image **3** (`0x42af36..0x42af3f`) |
| B | `ebx + 0x74` = frame 1 (`0x42b032`) | `[0x5e8674] + 0` = image **0** (`0x42b03d`) |
| C | `ebx + 0xe8` = frame 2 (`0x42b135`) | `+0x74` = image **1** (`0x42b143..0x42b14d`) |
| D | `ebx + 0x15c` = frame 3 (`0x42b23e`) | `+0xe8` = image **2** (`0x42b24c..0x42b255`) |
| E | `ebx + 0x1d0` = frame 4 (`0x42b348`) | `+0x1d0` = image **4** (`0x42b356..0x42b35f`) |

`[0x5e8674]` / `[0x5e8670]` = array (0x74-byte texture structs) and count of the
**bank-1 images**, i.e. the type-1 items of the level `.rck` (`0x47f9a0` adds one
and increments the counter, `0x47f630` allocates and sets the counter to 0, `0x47f6c0` frees;
see `RCK.md` and `HUD_TEXT.md` §5.1). Frames of one group lie contiguously with stride 0x74
(`FORMAT_TEX…` §1), hence `ebx + i·0x74`.

This matches exactly in the data (`tools/skycheck.py`):

| Levels | sky group | frames | level `.rck` images | source |
|---|---|---|---|---|
| House g0, K1R/S1R g3, K2R/S2R/K3R/S3R g0, WWS/KWS/SWS g35 | 128×128 | 5 | 0, 2 or 3 | frames 0..4 |
| W2A/K2A/S2A g5, W2B g1, W3C g14, W3D g23 (32×32), W2D g0 (128×128) | | 1 | exactly 5 (128×128) | bank images 3,0,1,2,4 |

The one-frame groups are thus only a placeholder; their own (32×32) texture is never
shown. There is no level with a sky group with < 5 frames AND < 5 bank images (the engine
would then read past the group). Conversely, Blackbox (72), Credits (13) and Lang (6) have
bank images but no sky group: nothing happens there.

Note the **0.5-texel inset for bank images**: `hu/hv` come from the group texture
(`ebx`), not from the bank image. For W2D that is 128 = 128 (exact); for W2A/K2A/S2A/W2B/
W3C/W3D the group is 32×32 and the image is 128×128, so the inset there is `0.5/32` = 2 texels
of the image. A port that wants to be pixel-accurate therefore takes `hu = 0.5/group.width`.

### 2.2 `0x439540(this = renderer, n = 4, verts, tex, flags = 0x40)`

| Address | What |
|---|---|
| `0x439554` | `flags & 1` = backface test; not set → **no culling** |
| `0x43960e..0x439630` | `flags & 0x70 == 0x40`: vertices are in **world space**; `0x43964d..0x439689` transforms them with the view matrix `[renderer+8]+0x30` (rows at `+0x30/+0x40/+0x50`) |
| `0x43968c..0x4396eb` | clip codes against the four side planes of the frustum (`±x' > z'`, `±y' > z'`); **no near or far plane** – the cube cannot disappear via a far clip |
| `0x439896..0x439968` | everything outside → discarded; otherwise clipped (`0x438cc0`, `0x438f00`, `0x439130`, `0x439340`) |
| `0x439999` | `flags & 8` not set → path `0x439b2a`: `rhw = 1/z'`, **`z = 1 − 12·rhw`** (`[0x4aa2fc] = 12.0`, `0x439b38..0x439b4a`), diffuse `= (alpha·r·255)<<16 | (alpha·g·255)<<8 | alpha·b·255` = `0x00ffffff` (`0x439b73..0x439be9`, `[0x4aa308] = 255`), specular `= 0` (`0x439bd2`), u,v copied (`0x439b63..0x439b70`) |
| `0x439bf6`, `0x439c1c..0x439c3c` | `flags & 4` not set → **bucket 0**: `0x42b460(tex, 0)` |

With `z' ∈ [50000, 86603]`, `z ≈ 0.99976..0.99986`: behind all level geometry (levels are
at most ~±20000 in size), in front of the clear plane `z = 1`.

## 3. Render states

Bucket 0 is flushed first (`0x4293f0`, `LIGHTING.md` §1.5, row 1):

| State | Value | Address |
|---|---|---|
| TSS0 COLOROP | MODULATE (4) – 1×, not 2× | `0x4294b2` |
| ALPHATESTENABLE | 0 | `0x4294ca` |
| ALPHABLENDENABLE | 0 | `0x4294dd` |
| ZWRITEENABLE | 1 | `0x4294f0` |
| TSS0 ADDRESS | **WRAP** (1) | `0x429503` |
| SPECULARENABLE | 0 | `0x42951b` |
| ZENABLE / ZFUNC | 1 / LESSEQUAL (4) – device init, never changed per bucket | `0x47ec3f` (ZFUNC) |
| CULLMODE | NONE (1) – device init | `0x47ec86` |
| MAG / MIN / MIP filter | LINEAR (2) / LINEAR (2) / POINT (2) – device init, stage 0 | `0x47ed3c`, `0x47ed4c`, `0x47ed5c` |
| Fog | not a single `SetRenderState(0x1c FOGENABLE, …)` found anywhere in the exe → off | – |

So: opaque, white × texture, **no** `.lit` factor (the 0.6 for unlit faces is in
`0x42b6c0`, which is not reached here), no vertex color, no fog. Address mode is WRAP;
it is the half-texel-inset UVs that prevent bilinear filtering from pulling in the
opposite edge across the boundary (at `u = hu` the sample falls exactly on the center of texel 0). The
group textures have 3 engine-generated mipmaps (`0x47f7f0(…, 3, …)`, `0x426e9f`;
details and the port in §8); at 128×128 over ~90° field of view, mip 0 is used as soon as the
window is wider than ~256 px, so mip bleeding is not an issue for the cube in practice.
Uncertain: whether the bank images (`0x480780`) get mipmaps.

For OpenGL 1.1: `GL_REPEAT` + `GL_LINEAR` with the inset UVs gives the same picture for the
cube (`GL_CLAMP` from 1.1 blends in the edge color and is thus worse; `GL_CLAMP_TO_EDGE`
`0x812F` may be used if the driver supports it, but is not necessary). Since issue #38 the port uses
the same mipmapped textures for the group frames as the world does (§8), same as the original.

## 4. The remaining flag bits and the other questions

### 4.1 Byte 1 of the flags (`tex+0x45`)

Only reader in the whole exe: `0x42acea` (`== 2`). Values in the data: 0 (almost everything), 2 (sky),
3 (three groups: W3A g77, S3A g75, K3A g76; 32×32, 17 frames, duration 1.6 s). Byte 1 `== 3` has
**no meaning** for the engine. W3A g77 consists of 16 frames of green, rippling liquid plus a 17th solid
light-green frame (`python tools/skycheck.py --frames W3A 77` →
`out/frames_W3A_g77.png`); no `.gel` face uses it (material 18859), it belongs to
`.ins` models and is animated via the model route `0x47f290`. Uncertain: what the editor used "3" for
(presumably a type dropdown: 0 normal, 2 sky, 3 animation).

### 4.2 Time animation and scrolling of world faces

- `0x42b6c0` (base face) gives `[material+0x20]` = **frame 0** to the bucket in both paths
  (`0x42c2f4..0x42c304`; the lit path jumps there with `push 1` at `0x42bef2`). In
  `0x42b6c0..0x42c320`, `tex+0x48/+0x4c` (scroll), `+0x54` (duration) or `+0x58` (frames) is never
  read. **`.gel` faces do not animate or scroll.**
- The frame/scroll calculation `0x47f290(this = tex, animstate, uv_out, material, time)` has
  exactly one caller: `0x43c341`, the model/instance renderer. The animation state comes from
  the instance (`inst+0xd8`: mode `&7`, start time `+4`, speed `+8`; `0x47f2bd..0x47f2f1`).
- Data: the only `.gel` faces with a multi-frame group or scroll ≠ 0 are, in all 28 levels, the
  sky faces (House 104, K1R/S1R 577, K2R/S2R 1429, K3R/S3R 2082, WWS/KWS/SWS 523). The
  `anim_duration` of sky groups is garbage (1.0, 0.0 or denormals like `1.79e-43`) and is
  never read for the sky.

### 4.3 Low bits `0x08..0x80` of byte 0 (`tex+0x44`)

All readers/writers of `byte [tex+0x44]` in the exe:

| Address | What |
|---|---|
| `0x426e97`, `0x47f7fd..0x47f81e` | loader: pass on / write back bit 0 (colour key) |
| `0x42801a..0x42802b` | `.ins` loader: `(& 6) << 4` into the face flags (bits 1–2) |
| `0x428f6b` … `0x4297fd`, `0x429a6b`, `0x429c51`, `0x429e5b`, `0x42a1b7` | flush: bit 0 → ALPHATESTENABLE on/off |
| `0x42b4b2..0x42b4b8` | `0x42b460` (texture used for the first time this frame in a bucket): **`and 0xf7`** – clears bit 3 and zeroes the bucket lists `tex+0x08..+0x34` |

- **Bit 3 (`0x08`)**: cleared every frame on first use; there is **no code that sets or tests
  it** (the only `or [..+0x44]` in the exe, `0x4a172e`, is in the CRT). Dead
  runtime bit; its value in the file is meaningless.
- **Bits 4–7 (`0x10..0x80`)**: no reader found (also no dword reader of `tex+0x44` in the
  render code `0x426000..0x440000` / `0x47e000..0x486000`). In the data they look like
  leftover editor data: in K3A and K3R almost every group has a different value in the high
  nibble (0x10, 0x20, … 0xf0, unrelated to the kind of texture), in S1R all groups have
  `0xc0`. **No** scroll, environment mapping, no-fog, no-z, double-sided or draw-first: those
  do not exist in this renderer as a texture flag (culling is always off, fog does not exist,
  the draw order is fixed by the buckets). Uncertain remains only whether an
  as-yet-unfound indirect reader exists; the sky route demonstrably does not use them.

## 5. Numerical check (`tools/skycheck.py`)

The script applies the recipe: finds the sky group, counts the (undrawn) sky faces, picks
the source per §2.1, builds the five quads and compares, along every shared cube edge, the
edge texels of the two adjacent textures (RMS over 64 samples, scale 0..255; for
verification also with one of the two edges mirrored). It then writes
`out/sky_<LVL>_equirect.png` (360°×180°, center = look direction +z, +x left) and
`out/sky_<LVL>_cross.png`.

House (frames 0..4 of group 0):

```
A z=+S | B x=+S   RMS 0.4   mirrored 30.2        C z=-S | D x=-S   RMS 1.5   mirrored 42.9
A z=+S | D x=-S   RMS 0.8   mirrored 29.6        C z=-S | E y=+S   RMS 1.4   mirrored 15.1
A z=+S | E y=+S   RMS 0.4   mirrored  0.8        D x=-S | E y=+S   RMS 0.5   mirrored 15.5
B x=+S | C z=-S   RMS 0.6   mirrored 32.5
B x=+S | E y=+S   RMS 0.4   mirrored  0.6
```

All eight seams close to within ~1/255. The four seams with the top face E prove that the
**bottom** image row (v = 1) of the side images lies at the top (`y+S`), so the
images are stored "upside down" in the file, and the side order 0→1→2→3 = +z → +x → −z → −x.
The equirect preview shows a continuous sky: clouds above a light horizon band,
dark blue below, and a black gap where there is no bottom quad.

W2D (one-frame group, bank images 3,0,1,2,4): worst seam RMS 4.7, mirrored
50..217 → the order 3,0,1,2,4 is correct. Other levels (worst seam): K1R/S1R/K3R/S3R 0.0,
K2R/S2R/WWS/KWS/SWS 4.9, W2B 6.7, W3C/W3D 7.4, W2A/K2A/S2A 27.9 (the seams with E there are
0.6..4.5; the outlier is in the side seams A|B, C|D, where the hard horizon edge between two
images is off by one texel – a blemish in the assets, below the level's horizon).

W3A has no group with byte 1 `== 2` and thus no sky box; group 77 (byte 1 `== 3`) is the
animated liquid of §4.1.

## 6. Recipe for the port (`src/render_gl.c`)

1. **Loading** (where the world batches are currently built, loop over `gel->polys`):
   `sky = ((gflags >> 8) & 0xff) == 2` → `continue` (face in no batch at all, not in
   `litb` and not in the light polygons of list A/C either). Remember `r->sky_group = grp`.
2. **Textures**: if the level `.rck` has ≥ 5 images, upload images 3,0,1,2,4
   (BGRA, row 0 = top row, same orientation as the `.tex` frames: the seams with E close
   in W2D) as `sky_tex[0..4]`; otherwise `sky_tex[i] = groups[sky_group].gl_frames[i]` (those
   have, like every `.tex` texture, mipmaps: §8). `GL_LINEAR`, `GL_REPEAT`. The renderer needs
   access to the level bank for this (currently in `hud.c`/`main_engine.c`): pass the five images
   at `rnd_init` or via a `rnd_set_sky(images)`.
3. **Drawing**, in `rnd_frame` right after setting projection and view, before the world:
   ```
   glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
   glDisable(GL_CULL_FACE); glEnable(GL_TEXTURE_2D); glTexEnvi(..., GL_MODULATE); glColor3f(1,1,1);
   hu = 0.5f / group.width; hv = 0.5f / group.height;          /* from the GROUP, even for bank images */
   for quad q in A..E: bind sky_tex[q];
       uv = (hu,hv) (hu,1-hv) (1-hu,1-hv) (1-hu,hv);  pos = cam.pos + S * sign[q][k]   /* table above */
   glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
   ```
   `S = 50000` (or any other value between near and far/√3: without the depth test the image is
   identical). Do not break through `rnd_fade`/letterbox: the cube is ordinary 3D content and falls
   under the same fade and the same viewport as the world.
4. **Remove**: the time cycle `tg->gl_tex = tg->gl_frames[…time…]` for world batches (§4.2);
   world batches always bind frame 0. Models keep their own route (`set_material(…,
   frame)`).
5. Clear color: the original draws nothing below the horizon; what is visible there is the
   original's clear color (not investigated). In the levels there is geometry there.

## 7. Open questions / uncertain

- The original's framebuffer clear color (visible below `y−S` if a level is open there, and
  anywhere sky faces are not in the visible set).
- Whether the bank images get mipmaps (`0x480780` not read); irrelevant for the cube,
  which is sampled at mip 0.
- W1B (§8): the original's image has not been placed side by side with the port; that the far
  starfield walls there are almost black follows from the mipmap chain and MIPFILTER POINT, not from
  a screenshot.
- Left/right orientation on screen has not been separately verified: the recipe uses
  world coordinates directly and thus inherits the port's (already checked against a screenshot)
  convention "right-handed, y up, +x left of +z". The seams are independent
  of that choice. A screenshot of the original's title screen (sun/cloud positions) would settle
  this.
- Meaning of byte 1 `== 3` and of bits `0x10..0x80` for the *editor*; for the engine
  they have no effect (§4.1, §4.3).

## 8. W1B: starry sky without a sky group, and the mipmaps (issue #38)

Issue #38: "Sky in sideways part W1B looks glitched" – in the side view near the door at
(−11231, 187, −13722) (marker `0x19b`, script `1088 0x100019b 2`; test with
`WOODY_SIDE=0x19b … --pos -11231 187 -13722`, camera (−12231, 527, −13422) looking towards +x)
the sky was a flickering noise of loose pixels.

**W1B has no sky group** (`tools/skycheck.py W1B`: no group with byte 1 `== 2`), so there is
no cube. The "sky" is ordinary `.gel` geometry: **group 52** (128×128, flags `0x00ff0000`,
one frame, 171 single-texel stars on a black background) on 156 polygons that form boxes around the
play space, e.g. x −13455..−9484, y 253..1253, z −15772..−8019 around this spot. All
materials of that group have `|∂u/∂x| = 0.01`: **one repeat per 100 units, 1.28
texels per unit**. From the side camera, the wall at x = −9484 is about 2750 units away;
at 800 px screen height and ~84° vertical field of view, one pixel there is ≈ 6 units ≈ 8 texels.

The port had **no mipmaps**: `upload_texture` set `GL_LINEAR` and thus sampled from mip 0
one random texel out of eight, which hit or missed a different star with every camera movement –
the noise from the issue (and less noticeable on every distant floor).

The original (everything read statically):

| Address | What |
|---|---|
| `0x426e9f..0x426eaa` | `.tex` loader: per frame `0x47f7f0(stream, w, h, 3, colourkey, 0)` |
| `0x47f82c` | → `0x47fa60(stream, w, h, mips = 3, colourkey, 0, tex)` |
| `0x47fa93..0x47fa9d`, `0x47fac2` | `mips ≠ 0` → DDSD flags `0x21007` (+`DDSD_MIPMAPCOUNT`), `dwMipMapCount = 4` |
| `0x47fb4f..0x47fb5c` | caps `0x401008` = `TEXTURE | MIPMAP | COMPLEX` |
| `0x47fc80..0x47fed1` | loop of 3: `GetAttachedSurface` to the next level, half width/height (`0x47fd2b`, `0x47fd2d`), per destination texel the **2×2 box average** of the four source pixels, alpha included (`0x47fd83..0x47fe17`: `Σ (p & 0xfcfcfc) >> 2` for RGB, `Σ ((p >> 2) & 0x3fc00000)` for A), back to the surface format (`0x47f170`) |
| `0x47f1fd` | format 3 (colour key) = ARGB1555: alpha survives only as the top bit, so an averaged texel is opaque if ≥ 3 of the 4 sources are |
| `0x47ed3a..0x47ed62` | device init, stage 0: `SetTextureStageState` (IDirect3DDevice7 `+0x94`) MAGFILTER 2 = `D3DTFG_LINEAR`, MINFILTER 2 = `D3DTFN_LINEAR`, **MIPFILTER 2 = `D3DTFP_POINT`** (D3D7: NONE 1, POINT 2, LINEAR 3); the same three for stage 1 at `0x47edc4..0x47ede6`. No other writer of 0x10/0x11/0x12 in the exe, no LOD bias (0x13) or MAXMIPLEVEL (0x14) |

So: 4 levels (128, 64, 32, 16 for a 128×128 group), bilinear within the nearest
level = OpenGL `GL_LINEAR_MIPMAP_NEAREST`. This applies to **every** `.tex` texture (world,
models and the sky cube); the bank images (`0x480780`) and the generated 32×32 textures
(`0x47f840`) go through a different route.

For the W1B sky, this means: from ~4 texels per pixel onward, level 2 or 3 is chosen, in which
a one-texel star is smeared over 16 or 64 texels; the far starfield walls then become an
almost black, calm plane and only close by (the ceiling at y = 1253 directly above the player)
do loose stars remain visible. No flicker.

**Port** (`src/render_gl.c`, `upload_texture` + `box_halve`): mip 0 as before, then
repeatedly the half size with the same 2×2 box average (alpha of colour-key textures snapped back
to 0/255 with threshold 128, as with the 1555 bit), `GL_TEXTURE_MAX_LEVEL` (`0x813D`) = 3,
`GL_LINEAR_MIPMAP_NEAREST` / `GL_LINEAR`. The chain is computed down to 1×1 so the texture
is also complete under a strict GL 1.1; `MAX_LEVEL` restricts usage to the original's four
levels. Not replicated: the 16-bit quantization between the levels.
