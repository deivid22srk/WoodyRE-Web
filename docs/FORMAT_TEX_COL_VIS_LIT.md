# Level formats .tex / .col / .vis / .lit

Derived from the disassembly of `Woody.exe` (`out/disasm_full.txt`). All values are
little-endian; `u32` = 32-bit unsigned, `i32` = signed, `f32` = IEEE single. Parser:
`tools/levelparse.py` (validates all 28 levels byte-exact, see bottom).

Context: the level loader `0x426bd0` (level object 0x78 bytes, global at `0x50944c` and
`0x4c4c0c`; the "world" object and the level object are the same object) loads in this
order `.gel` (via `0x407ae0`), `.tex` (inline), `.ins` (`0x427290`), `.col` (`0x4271e0`).
Afterwards `0x4043d0` also loads `.vis` (`0x408260`) and `.lit` (`0x40ac30`). Stream class:
`[vt+8]` = read(buf, nbytes), `0x43fdb0` = fread(buf, size, count), `0x43fd90` =
fread(buf, 1, n) with a return value (used for the optional `.lit` trailer),
`[vt+0x18]` = total file size (used by the `.col` loader for the allocation).

Relevant counts from `.gel` (loader `0x407ae0`, field names = offsets in the world object):

| Field | Content | Used by |
|---|---|---|
| `world+0x0c/+0x10` | number of faces / array of face polygons | `.lit` (face indices) |
| `world+0x04/+0x08` | number of vertices / array (stride 0x30) | `.lit` polygons |
| `world+0x18/+0x1c` | number of "sector" objects (leaves of the spatial tree, 0x4c bytes, vtable `0x4a94f4`) | `.col` (one record per sector) |
| `world+0x14` | `objects-1` split planes (16 B) of that tree | – |
| `world+0x20/+0x24` | number of visibility cells / array (same 0x4c class) | `.vis`, `.lit` trailer |
| `world+0x40` | object table: pointers to the `.ins` objects, index = object id | `.col`, `.lit` (`object_id`) |

The `.gel` loop implemented by `tools/levelparse.py::parse_gel_counts` matches byte-exact on
all levels.

---

## 1. `.tex` – textures + materials

Loader: inline in `0x426bd0`, range `0x426c90`–`0x427066`. Pixel data is read per frame by
`0x47f7f0(this=texture, stream, w, h, 3, alpha, 0)` → `0x47fa60(this=tex+0x70, stream, w, h,
mipmaps=3, alpha, srcformat=0, tex)`: `w*h` times 2 bytes, per pixel via `0x47f090(v, 0)`
(case 0 = **RGB565** → ARGB8888) and back to the screen format via `0x47f170`. There are
**no mipmaps** stored in the file (the engine generates 3 with a 2×2 box filter). If the
alpha flag is on, every pixel with `(argb & 0xF0F0F0) == 0xF000F0` (magenta) becomes fully
transparent; all others get alpha 0xFF.

### File layout

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | 4 | u32 | `group_count` – number of texture groups (`[esp+0x1c]`) |
| 0x04 | 4 | u32 | `texture_count` – total number of frames; the engine allocates `(texture_count+19)` texture structs of 0x74 bytes (19 extra: 2×32×32, 1 special, 16 for RCK images) |
| 0x08 | … | | `group_count` × **group**: |
| +0x00 | 4 | u32 | width (→ `tex+0x38`) – in the data 16/32/64/128/256 |
| +0x04 | 4 | u32 | height (→ `tex+0x3c`), always = width |
| +0x08 | 4 | u32 | flags (→ `tex+0x44`): bit 0 = colour-key alpha (passed on to `0x47f7f0` and set in bit 0 of `tex+0x44`); bits 1–2 (`&6`) get copied `<<4` into the face flags by the `.ins` loader (`0x42801a`); byte 1 (`tex+0x45`) `== 2` is tested in `0x42acea` (separate render mode); byte 2 = intensity/alpha (0xFF = opaque; 0x7f/0x99/0xb2/0xcc/0xe5 only occur together with bit 1); byte 3 (`tex+0x47`) = player's **ground type** (`m_nGroundType`, `0x46295f`). Bit 1 = blended rendering: the glow textures (white on black, e.g. W1A g77 `0x00cc0002`) and the fan haze (g66 `0x007f0002`) match the original when rendered additively (`ONE/ONE`, no z-write) with intensity byte 2. `0x428ee0` knows three passes (ZERO/ONE, SRCALPHA/INVSRCALPHA, ONE/ONE); **which list belongs to which bit is now worked out**: both bit 1 and bit 2 become polygon flag 0x20/0x40 (`0x42801a`) and send the face to **mode 3** (list `renderer+0x1cc`, `ONE/ONE` at `0x429182`/`0x429198`, z-write off at `0x42908d`, MODULATE 2×). Bit 2 does not occur in the data. **Intensity byte 2 has no reader at all** – the loader (`0x426e2f`) keeps the flags word unchanged and only bit 0, bits 1-2, byte 1 and byte 3 are ever read back; a blended model face is thus drawn as `1.0 × texture`, see LIGHTING.md §"Recipe" item 5 |
| +0x0c | 4 | f32 | `scroll_u`? (→ `tex+0x48`); almost always 0.0, 3 groups have 0.05 |
| +0x10 | 4 | f32 | `scroll_v`? (→ `tex+0x4c`); same |
| +0x14 | 4 | f32 | animation duration (→ `tex+0x54`); used in `0x47f290` as `duration * speed` to compute the frame from time (modes 1–6: once, reversed, ping-pong, looping, …). Usually 1.0; non-animated groups sometimes have leftover integers (32/64/128) |
| +0x18 | 4 | u32 | `frame_count` (→ `tex+0x58`); 1, 2, 4, 5, 8 or 17 |
| +0x1c | 4 | u32 | dword 8 (→ `tex+0x5c`); 0, 1 or ~9647 – no reader found |
| +0x20 | 4 | u32 | dword 9 (→ `tex+0x50`); 1.0f, 0 or pointer-like values (0x0141xxxx) – no reader found, presumably editor leftover data |
| +0x24 | `frame_count × w×h×2` | u16[] | frames, **RGB565**, row by row, no padding |
| … | 4 | u32 | `material_count` (after all groups) |
| … | `material_count × 0x34` | | **material** (→ `level+0x5c`, records of 0x24 bytes in memory): |
| +0x00 | 4 | u32 | group index; the engine stores the pointer to the first frame of that group in `mat+0x20` |
| +0x04 | 48 | f32[12] | `f[0..11]`: 4 rows of 3 floats (3×3 matrix + translation row, world→UV). The loader (`0x426fc2`–`0x427058`) keeps only column 0 (`f0,f3,f6,f9` → `mat+0x00..0x0c`) and column 1 (`f1,f4,f7,f10` → `mat+0x10..0x1c`); column 2 (`f2,f5,f8,f11`) is discarded |

**UV computation** (`.ins` loader, `0x4280c2`–`0x428110`): every face in `.ins` has a u16
material index (`face+0x00`, `0x42800a`) and a u16 vertex count (`face+0x02`) followed by
u16 vertex indices (starting at `face+0x18`) into the vertex array of the corresponding mesh
(stride 0x28, position at +0/+4/+8). For every vertex `(x,y,z)` in **object space**:

```
u = f0*x + f3*y + f6*z + f9        (mat+0x00..0x0c)
v = f1*x + f4*y + f7*z + f10       (mat+0x10..0x1c)
```

The result is directly the texture coordinate in repeats (1.0 = the texture once); it is
not divided by the texture size. Face flag bit 0, or `face+1 & 0x80` (`0x427ff8`), skips
the material linkage. `tools/levelparse.py::material_uv(material, x, y, z)`
implements this.

Static-group test in the loader (`0x426e5a`): `tex+0x00 = 0` if `frame_count == 1` and
both floats `+0x0c/+0x10 == 0.0`, otherwise `1` ("needs updating"). Hence the interpretation
of `+0x0c/+0x10` as UV scroll speed is plausible; the render code confirms it: `0x47f290` multiplies `tex+0x48/+0x4c` by the
factor of messages 15/17 and adds the fraction to the material's constant terms (INSTANCE.md §2). No other reader was found (a scan of the float reads of `+0x48/+0x4c` in the renderer range found only camera and instance fields; uncertain), and no
level sends 15/17, so in the shipped game nothing ever scrolls.

Faces in `.ins` reference this material table (`level+0x5c + idx*0x24`) with a u16;
the renderer (`0x43b3f0`) picks the frame `min(frame, frame_count-1)` from the group per face.

### Texture struct (0x74 bytes, ctor `0x47f250`)

| Offset | Meaning |
|---|---|
| +0x00 | 0 = static, 1 = animated/scrolling |
| +0x04 | -1 (ctor) |
| +0x08..+0x34 | runtime (zeroed) |
| +0x38 / +0x3c | width / height |
| +0x44 | flags (header dword 3, bit 0 overwritten by the loader with the alpha arg) |
| +0x48 / +0x4c | header float 4 / 5 |
| +0x50 | header dword 9 |
| +0x54 | animation duration (header dword 6) |
| +0x58 | frame_count |
| +0x5c | header dword 8 |
| +0x60 | this (ctor) |
| +0x6c | -1 (ctor) |
| +0x70 | `IDirectDrawSurface7*` (filled by `0x47fa60`) |

Frames of one group lie contiguously in the array (stride 0x74); the group table on the
stack (`esp+0x3a0`) holds the pointer to frame 0 of each group.

### Uncertain
- Meaning of dword 8 (`+0x5c`) and dword 9 (`+0x50`): no reader found in the code.
- `+0x0c/+0x10` as UV scroll: confirmed by `0x47f290` (INSTANCE.md §2), only used by the unused messages 15/17.
- Flag bytes 1–3 only partly understood.

---

## 2. `.col` – object lists per sector

Loader `0x4271e0(this=level, stream)`. Allocates a single pool the size of the file
(`[vt+0x18]/4` dwords + margin) and, for each object `i`, fills `level+0x1c[i]` (count
`level+0x18`, the sector objects from `.gel`) with `obj+0x40` = count and `obj+0x44` = pointer
into the pool.

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | … | | `world+0x18` × **sector record** (order = `.gel` object order): |
| +0x00 | 4 | u32 | `n` (→ `sector+0x40`) |
| +0x04 | `4n` | u32[] | references (→ `sector+0x44`): `(mask << 16) \| object_index` |

Usage (`0x4071ae`, `0x407502`, `0x42aa0b`, `0x434771`, `0x497a9c`, `0x497f3c`, `0x4984ab`): `object_index =
v & 0xFFFF` indexes the object table `world+0x40` (the `.ins` objects; `[obj+8] & 0x1f` =
object type), the full u32 is passed as the `id` argument to the instance's collision methods (vt[5] `+0x14`
ray, vt[6] `+0x18` endless ray, vt[7] `+0x1c` floor, vt[8] `+0x20` cylinder, vt[9] `+0x24` sphere);
`0x407282` itself constructs such values with `id | 0xFFFF0000` for the dynamic list.

**The high 16 bits are a PHASE mask.** Each test ANDs them with the instance's `+0xd0`
(`0x4324df`, `0x432b11`, `0x431e40`, `0x4331c3`, `0x434054`: `if (!(inst+0xd0 & id & 0xffff0000)) return`),
and `+0xd0` is written by the animation clock `0x43eee0` after every pose: `(1 << (int)(min(phase, 1.0)·15.0 + 0.5)) << 16`
(`0x43f264..0x43f2a3`, constants `0x4a9864` = 15.0, `0x4a9014` = 0.5, `_ftol` `0x499580`), phase = position / length of the
current animation. So a ref says "this instance reaches into this cell at these of the 16 sample phases of its
animation": a static prop has 0xFFFF, a moving platform is listed along its whole path with one or a few bits per
stretch (W1B shuttle 605, model 42: 78 cells, e.g. cell 3718 mask 0x0c38, cell 4791 mask 0x0180; the W1A stamper 186:
137 cells, 0xFFFF in the column above the floor, 0x00F8 in the floor cell). Of 11615 refs in W1B 450 are partial
masks; masks are never 0. The registration is **static**: nothing writes `cell+0x40/+0x44` after the loader, so an
instance moved by code away from where the level tool registered it (a boss pad, a thrown bomb, a flying rocket) is
found only through the dynamic list 0x4c3bb4 (the listed instances with flag 0x20), or not at all. The level tool
evidently registered the geometry per phase generously (every sampled node box that touches a cell is registered, and
more; the TRAJ points of the path-followers of W1B/WWS/W2D/W3D lie inside their registered cells, except lasers
50-52 and the bomb cannons 21 whose TRAJ is not their own path).

Order: within a cell the refs are NOT sorted by object index (W1B: 5746 of 11615 consecutive pairs descend), no ref
occurs twice in a cell, and every index is an `.ins` instance (no camera) on all levels.

Validation: record count == `.gel` objects on all levels; `object_index` < number of
`.ins` objects (1st u32 of `.ins`) on all levels.

### 2.1 How the queries use it
Every instance collision test runs only for the instances registered in the cells the query visited (FORMAT_GEL.md 5.1)
and then for the dynamic list; see PERSO_MOVE.md 6.7 for the order, the skips and the hit selection. Port: the file is
loaded into the level's GelFile by `gel_col_load` (`src/level.c`, through `rnd_load_col`), the renderer reads the camera
leaf's list with `gel_col_cell` (message-34 pass `0x42aa0b`), the collision queries with `gel_col_instances`.

### Uncertain
- Which animation the level tool sampled for the masks (presumably animation 0, the only one most props have); an
  instance that plays another animation is still tested with the bit of its CURRENT phase, as in the original.

---

## 3. `.vis` – visibility per cell

Loader `0x408260(this=world, path)`. `world+0x28` = array of `world+0x20` pointers; per cell
`(entry_count + total_pairs)*8 + 4` bytes are allocated and filled as
`{u32 entry_count, entries…}`.

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | … | | `world+0x20` × **cell**: |
| +0x00 | 4 | u32 | `entry_count` (A) – 1 or 2 in the data |
| +0x04 | 4 | u32 | `total_pairs` (B) = sum of all `pair_count` (allocation only) |
| +0x08 | … | | A × **entry**: |
| ++0x00 | 4 | u32 | `id` – 0 or 1 in the data |
| ++0x04 | 4 | u32 | `pair_count` |
| ++0x08 | `8·pair_count` | (u32,u32)[] | `(cell-index, flag)`; cell index < `world+0x20`, flag 0/1 |

Interpretation: per cell, one or two lists of cells visible from that cell (PVS) with a
flag per cell. Validation: cell count == `.gel` cells on all levels.

### Use in the port
`src/level.c::vis_load` reads the file with `world+0x20` from the `.gel` as the count and checks
that every first word of a pair is a valid sector index and that `total_pairs` matches; otherwise
the file is ignored and the renderer falls back to frustum culling only. Every frame,
`world_visibility` (`src/render_gl.c`) looks up the camera's sector with `gel_sector`
(`0x4081c0`), takes the **union** of all its lists (1 or 2 — since the meaning of `id` is not
confirmed, a union can at most show too much), adds the sector itself, and then discards every
sector whose bbox falls outside the view frustum. The polygon lists (`.gel` section 7) of the
remaining sectors are stamped the way `0x42ac10` does it, so that a face belonging to multiple
sectors is drawn once. `vis_load` prints the min/average/max number of visible sectors when
loading and how many sectors list themselves, so it is immediately visible whether the reading is
correct; **F4** steps the culling down one stage at a time: frustum + `.vis` → frustum only → whole level.

The port uses the sector polygon list, not the group from the second word of a pair: 22 of the 28
levels have a single group covering all polygons, so nothing would be lost by that. The
flag/group is therefore not read on that path. The six race levels have a separate path since round 30
(RACE.md §2.1): one entry chosen as `0x408210` does, sectors from the first words, groups = the floor group under
the camera + the next entry of the race region list, faces filtered by their `.gel` group.

### Reader `0x408210` (round 29/30)
`0x408210(pos)` reads `world+0x28`: sector `s = 0x4081c0(pos)`, `g = 0x40a0c0(pos, −1)` = the `.gel` group (section 3) of
the floor polygon under `pos`, then the entry of `s` whose **`id == g`**, else the first one; it returns `&pair_count`.
So `id` is a floor group (a sector that spans two floor zones has one list per zone), and the second word of a pair
("flag") is a `.gel` group index: `0x42a980` marks those groups (their polygons get the frame stamp in `0x42ac10`) and
only stamped polygons of the listed sectors are drawn (RACE.md §2.1). Correction to the table above: the race levels
have entry ids and pair groups 0..4 (port log `WOODY_RACEVISLOG=1`, e.g. K1R sector 114 entry id 4).

---

## 4. `.lit` – lighting system (precomputed lighting and shadow BSPs)

Loader `0x40ac30(this=lightsys (0x2c bytes), path, 0x10, 0x400)`, object in global
`0x4c4cac`. On a wrong magic, `0x4c4cac` stays 0 and `0x404536` reports
"Can't load lightsystem : please rebuild lights !!!".

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | 4 | u32 | magic `0x20010822` (date 22-08-2001) |
| 0x04 | 4 | u32 | `light_count` (→ `lightsys+0x00`; lights are 0x40-byte objects, ctor `0x40b450`, array `lightsys+0x04`) |
| 0x08 | … | | `light_count` × **light**: |
| +0x00 | 4 | u32 | `object_id` (→ `light+0x04`); `id & 0xFFFFFF` = index into `world+0x40`, high byte = type (always 1). If `world+0x40` exists: `world+0x40[idx] = light` |
| +0x04 | 4 | u32 | → `light+0x28` (2, once 3) |
| +0x08 | 12 | f32[3] | position (→ `light+0x0c..0x14`) |
| +0x14 | 12 | f32[3] | colour R,G,B as 0..255 (→ `light+0x30..0x38`) |
| +0x20 | 4 | f32 | range/radius (→ `light+0x2c`; e.g. 6000, 50000) |
| | | | followed by sub-struct **S** (`light+0x3c`, 0x28 bytes, malloc'd): |
| +0x24 | 4 | u32 | `nA` (→ `S+0x08`) |
| | `4·nA` | u32[] | list A (→ `S+0x0c`): ascending sorted **face indices** (`world+0x10`), the faces lit by this light (`0x497c2c` walks them and tests the face plane) |
| | 4 | u32 | `nB` (→ `S+0x10`) |
| | `4·nB` | u32[] | list B (→ `S+0x14`): ascending sorted face indices (not a subset of A) |
| | 4 | u32 | `nC` (→ `S+0x18`) |
| | 4 | u32 | `total_indices` = sum of all `n` below (for the pool allocation `(7·nC + total)*4`) |
| | … | | nC × **polygon** (same format as `.gel` faces): `u32 n`, `f32[4]` plane (nx,ny,nz,d) → `P+0x0c`, `u32 face` → `P+0x08`, `i32[n]` vertex indices → `P+0x1c` (positive = `.gel` vertex, negative = extra vertex from clipping; `P+0x04` is set to -1) |
| | 4 | u32 | `nD` (→ `S+0x00`) |
| | `16·nD` | u32[4] | **range per cell** (→ `S+0x04`, records of 0x1c): `{cell, nA_i, nB_i, nC_i}`; the engine computes cumulative offsets in `+0x08/+0x10/+0x18` – lists A/B/C are thus grouped per cell |
| | 4 | u32 | `nE` (count not stored) |
| | `12·nE` | u32[3] | **BSP nodes** (→ `S+0x20`): `{plane_index, front, back}`; child = `(v & 0xF)`: 0 → node `v>>4`, 1 → leaf with face `v>>4`, otherwise empty. `0x40b540(S, point)` walks the tree (plane·p + d ≥ 0 → front) and returns the face |
| | 4 | u32 | `nF` (count not stored) |
| | `16·nF` | f32[4] | **BSP planes** (→ `S+0x24`), normalized (nx,ny,nz,d) |
| … | 4 | u32 | `probe_count` (→ `lightsys+0x08`) |
| … | `16·probe_count` | | **probe** (→ `lightsys+0x0c`, records of 0x30): `f32[3]` position, `u32` colour 0x00RRGGBB (→ `+0x28`); `+0x2c` = -1 (runtime). Used in `0x42c320`/`0x498890` (distance to a point) |
| … | 4 | u32 | *(optional, via `0x43fd90`)* `total` – number of dwords in the trailer |
| … | `4·total` | u32[] | trailer: `world+0x20` × `{u32 n, u32 light_index[n]}` – lights per **sector** (→ `lightsys+0x10`). If the trailer is missing, `lightsys+0x10 = 0`. Measured: the number of lists equals exactly the number of `.gel` sectors on every level (House 33, the rest 128), not the number of cells (2530–7691) – see LIGHTING.md §3 |

The remaining lightsys fields (`+0x14..+0x28`) are allocated after loading (depending on the
arguments 0x10/0x400 and `world+0x0c`).

### Uncertain
- Distinction between list A and B (both face indices); B is possibly the shadow-casting
  faces.
- Meaning of `light+0x28` (2/3) and of the type byte in `object_id`.
- What the probes (0x30 records) exactly are (light samples / ambient points).
- What the `cel` field in the range records exactly is (values < `world+0x20`, consistent with a cell).

---

## Validation

`python tools/levelparse.py` parses all four files of all 28 levels, checks that every
parser fully consumes the file, and cross-checks against `.gel`: `.col` records ==
objects, `.vis` cells == cells, `.lit` trailer lists == cells, all face indices <
face count. Result: 28/28 OK.

`python tools/levelparse.py --dump-tex W1A out/tex_W1A` writes the first frame of every
group as a PNG (RGBA; magenta key applied if the flag bit 0 is set).
