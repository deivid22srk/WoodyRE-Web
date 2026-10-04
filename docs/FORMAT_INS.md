# File format `.ins` (level instances) – Woody Woodpecker: Escape from Buzz Buzzard Park

Derived from the disassembly of `Woody.exe` (MSVC 6): loader `0x427290` (`level::LoadIns(stream, closeFlag)`,
called from the level loader `0x426bd0` at `0x4270eb`), sub-reader `0x428bc0` (trajectory),
instance ctor `0x42e1a0`, `0x440370` (quaternion → 3×3 matrix), `0x407790` (world cell lookup),
`0x437ca0` (path length), pose evaluation `0x43a2b0`/`0x43a3a0` with track readers `0x43a590` (position),
`0x43a9c0` (rotation), `0x43a7b0`/`0x43a820` (events), animation clock `0x43eee0`/`0x43f074`,
volume/collision tests `0x430210`/`0x4303e0`, renderer `0x43b3f0`.
Parser: `tools/insparse.py` (all 28 levels are consumed byte-exact).

Conventions: little-endian, `u32` = 32-bit unsigned, `f32` = IEEE single, `vec3` = 3×f32.
All counters are `u32`. **Node indices in the file are 1-based**: node 0 is the (not stored) root
of the model, node `k` (1 ≤ k ≤ B−1) is element `k−1` of the node array (`-1` = none).

## 1. Global layout

```
u32  nslots        -> level+0x68  (number of script objects; == "nobj" in the code header)
u32  nmodels
nmodels × MODEL    (§2)
u32  ncameras
ncameras × CAMERA  (§7)
u32  trailer       (always 0; read, unused)
```

The loader allocates `level+0x6c` and `level+0x40` as `(nslots+16)` pointers (`level+0x74 = 16`
reserve, `level+0x70 = nslots`, `level+0x3c = nslots+16`) and fills all slots with an empty
placeholder (0x28 bytes, vtable `0x4aa28c`, `+4` = index, `+0x1c` = −1). Instances and cameras
then overwrite slot `id & 0xffffff`.

The `.ins` file contains **no** reference to `.gel` groups: the geometry of all
moving/placeable objects (including the Woody character model with 142 nodes and 91 animations,
which appears as model 0 in every level) is **inline** in the `.ins`.

## 2. MODEL (engine struct `S`, 0x70 bytes, malloc'd at 0x427454)

A model is a hierarchy of nodes with its own point list, polygons and keyframe animations,
plus a list of instances that place the model into the world.

| # | Size | Type | Meaning | Engine |
|---|---|---|---|---|
| 1 | 4 | u32 `B` | number of nodes **including root**; `B−1` node records follow | `S+0x64 = B−1`, `S+0x68` = node array (0x90 bytes/node, with a u32 count ahead of it) |
| 2 | 4 | u32 `A` | number of animations | `S+4` |
| 3 | 4 | u32 | total number of polygons (== §2.5 `npolys_total`) | `S+0xc` |
| 4 | 4 | u32 | number of points (duplicate of §2.4) | `S+0x1c` (overwritten later) |
| 5 | 8·A | A × {u32 `nframes`, u32 `duration`} | animation table: `nframes` = length in frames (= time value of the last keyframe), `duration` = play duration in **1/4096 s** (`0x43eee0`: `fild [S+8+i*8+4]; fmul 1/4096`). E.g. (1200, 0x6000) = 1200 frames in 6 s = 200 fps; (100, 0xA000) = 10 s | `S+8` |
| 6 | 4 | u32 | first top-level node (1-based); the remaining top-level nodes follow via `next_sibling` | `S+0x6c` |
| 7 | 4 | u32 | bounding-box node (1-based, flags 0x02). Models without a bbox node (volumes only) contain `B` here (= out of range) | `S+0x24` |
| 8 | 4+4n | u32 n, n×u32 | list of **marker nodes** (flags 0x20) | `S+0x50/0x54` |
| 9 | 4+4n | u32 n, n×u32 | list of light nodes (flags 0x40); `n` determines how many lights get copied per instance | `S+0x28/0x2c` |
| 10 | 4+4n | u32 n, n×u32 | list of **volume nodes** (flags 0x08) = trigger volumes for the VM | `S+0x48/0x4c` |
| 11 | 4+4n | u32 n, n×u32 | list of **hull nodes** (flags 0x04) | `S+0x38/0x3c` |
| 12 | 4+4n | u32 n, n×u32 | list of **helper nodes** (flags 0x10) | `S+0x40/0x44` |
| 13 | 4 | u32 `n58` | number of **press nodes** (flags 0x01) | `S+0x58` |
| 14 | 4 | u32 `ncol` | number of collision ids per instance (0 or 1) | `S+0x60` |
| 15 | 4·n58 | n58×u32 | list of press nodes | `S+0x5c` |
| 16 | 4+4n | u32 n, n×u32 | list of **mesh nodes** (flags 0x00) | `S+0x30/0x34` |
| 17 | var | (B−1) × NODE | see §2.1 | |
| 18 | var | EXTRA LISTS | see §2.2 | |
| 19 | var | POINTS | see §2.3 | |
| 20 | var | POLYGONS | see §2.4 | |
| 21 | var | TRIANGLES | see §2.5 | |
| 22 | var | INSTANCES | see §2.6 | |

In the data every low flag byte maps 1:1 to one list (verified across all levels):
0x00→mesh, 0x01→press, 0x02→bbox (no list, `S+0x24`), 0x04→hull, 0x08→volume, 0x10→helper,
0x20→marker, 0x40→light, 0x80→dummy (no list).

### 2.1 NODE (engine struct `N`, 0x90 bytes)

| # | Size | Type | Meaning | Engine |
|---|---|---|---|---|
| 1 | 4 | u32 `flags` | bits 0–7: node kind (see above); bits 8–15: **type code** (observed 1,2,4,5,6,7,9; `0x42f6b0` looks up nodes by type code, `0x436dc0` requires code 1 for press nodes); bits 16–31: sub-index into `inst+0x70` (type code 1 only) | `N+0` |
| 2 | var | – | depends on `flags`: | |
| | 4 | u32 | **ordinary** (flags & 0x70 == 0, or flags & 0x20 set without 0x40/0x10): `npolys` | `N+4`; `N+0x10 = 0` |
| | | | – for marker nodes (0x20) this field is an **f32** in the data (≈45.0) and no polygons are read | |
| | 12 | f32, u32, u32 | **light** (0x40): intensity (engine × 3.0), colour `0x00RRGGBB`, extra (0 or 4). Not stored in `N` but in a temporary list → per instance `inst+0x74` (16 B: R,G,B,pad, f32 int·3, u32 extra), `inst+0x88 = 2` | |
| | 12 | f32, f32, u32 | **helper** (0x10): `v_c`, `v_8`, `mode` (0/1/2). The engine stores `1/v_c` in `N+0xc`, `1/v_8` in `N+8`, `mode` in `N+4`; renderer `0x43b7a3` switches on `mode` and applies the scales to the vertices of the **parent** mesh (`N+0x84` must be the mesh) | |
| 3 | 4 | u32 `npoints` | number of points of this node (contiguous block in the model's point list) | `N+0x14`; `N+0x18` = start index (derived), `N+0x1c` = pointer |
| 4 | 12 | vec3 `pivot` | pivot point; the loader subtracts it from every point of the node, computes `N+0x2c` = max |p| (bounding radius) and `N+0x30..0x38` = centroid | `N+0x20..0x28` |
| 5 | 12 | u32 `a`, u32 `b`, u32 `c` | size of the track pool: `a` dwords of position frames, `b` dwords of rotation frames, `c` **bytes** of event records | |
| 6 | (a+b+c/4)·4 | bytes | **track pool** (only if a+b+c ≠ 0): [position frames][rotation frames][events] | `N+0x88` |
| 7 | 8·A | A × {u32 off, u32 cnt} | only if `a≠0`: per animation, offset (dwords, relative to the start of the pool) and number of position frames. Frame = {f32 t, f32 x,y,z} (16 B), linearly interpolated (`0x43a590`) | `N+0x70` |
| 8 | 8·A | A × {u32 off, u32 cnt} | only if `b≠0`: per animation, offset (relative to the start of the rotation section; the engine adds `a`) and number of rotation frames. Frame = {f32 t, f32 qx,qy,qz,qw} (20 B), interpolated via `0x440a80` (a true slerp with `acos`, plain lerp when the dot is ≥ 0.9999, no shortest-path sign flip: a pair with a negative dot turns the long way; PERSO_MOVE.md §6.6), → matrix via `0x440370`. Exception: dummy nodes (flags 0x80) have 3 dwords per frame (content 0) and are never evaluated by `0x43a3f2` | `N+0x74` |
| 9 | 8·A | A × {u32 off, u32 cnt} | only if `c≠0`: per animation, offset (relative to the start of the event section; the engine adds `a+b`) and number of events. Record = {u32 type, f32 t, …}, size depends on type: 3 → 15 dwords (contains a 3×4 matrix at +0xc, replaces the node matrix), 4 → 9 dwords, 5 → 6 dwords (`0x43a7b0`). Only 4 and 5 occur in the data | `N+0x78` |
| 10 | 4 | i32 `first_child` | first child (1-based) or −1 | `N+0x7c` |
| 11 | 4 | i32 `next_sibling` | next sibling (1-based) or −1 | `N+0x80` |

Derived by the loader: `N+0x84` = parent (loop over the child list of every node), `N+0x8c` = 0, `N+0x6c` = 0.
The animation hierarchy is walked recursively by `0x43a3a0` (child via `+0x7c`, siblings via `+0x80`);
the world matrices are stored in `[0x509adc]+0xa0` per node (0x30 bytes, base `inst+0x5c`).

**Rotation convention.** `0x440370(q, out)` first negates x, y and z (`fchs`) and then writes the
standard quaternion matrix row-major (`out[r*3+c]`), which the engine uses with row vectors
(`0x4405e0`: `A' = A·B`, local × parent). Net effect: the engine applies the rotation of the
**conjugated** quaternion. The instance loader (`0x428758`) already negates x, y, z before the
call, so instances apply the stored quaternion directly; track frames are *not*
pre-negated and thus act as `conj(q)`. Anyone building the matrix as a column-vector matrix
(OpenGL/glTF) must conjugate track frames and leave instance quaternions as-is.
Visible in W1A: model 3 (floating saucers) has `rotx(+90°)` as its first track frame and
instance rotation `rotx(-90°)`; only with the conjugation does the rim end up on top and the
point on the bottom, as in the original. The coordinate system is right-handed (y up, 3ds Max export).

### 2.2 EXTRA LISTS (`node+0x8c`)

Count = `len(hull nodes)` + 1 if the model has mesh, hull or press nodes (loader `0x427acb`).
Per list: `u32 node` (1-based), `u32 cnt`, `cnt × u32`. In the data these are the hull nodes plus
the bbox node; the contents are point indices within the node (for an 8-point box e.g. 12
triangles = 36 indices, but lengths like 52 also occur, so not a pure triangle list). Only used
by `0x4738c0`/`0x494f5d` (no direct callers found; presumably debug/editor).

### 2.3 POINTS (engine 0x28 bytes/point, `S+0x20`)

```
u32  npoints              -> S+0x1c
npoints × vec3 position   -> P+0   (loader makes them relative to the node pivot)
npoints × vec3 normal     -> P+0x10 (normalized)
npoints × vec3 color      -> P+0x1c (vertex colour 0..255; doubled if [0x5e8650]+0x20 == 0)
```
`P+0xc` = UV split flag (runtime), `P+0x28..` UVs are computed by the loader per material
(planar projection, see §5).

### 2.4 POLYGONS

```
u32  npolys_total        (== sum of npolys of all mesh nodes)
u32  nindices_total      (== sum of all nverts)
per node without flags&0x70, in node order, per polygon:
   u32 material   -> low16 in poly+0
   u32 flags      -> low8 | nodeflags in poly+4 (bits 5-6 are replaced by texture flags&6<<4)
   u32 nverts     -> low16 in poly+2
   nverts × u32   -> u16 indices in poly+0x18.. (index into the model's point list, absolute)
```
Engine polygon: `0x18 + 2·nverts` bytes, rounded up to 4; `poly+8..0x14` = plane (n, d), computed
by the loader from the two largest edge vectors. `material` bit 15 set → flat colour **RGB565**
(no texture, 0xFFFF = white); bit 15 clear → index into the **material table at the end of the
`.tex`** (`level+0x5c`, 36 B/entry: 8 UV-projection floats + texture pointer; read at
`0x426f54`, count matches exactly the highest index found in the `.ins`). Polygon flag bit 0 (0x1)
skips the UV computation; observed flag values 0, 2, 8.

### 2.5 TRIANGLES (`S+0x14`/`S+0x18`, 32 B per engine polygon)

```
u32 ntris
ntris × { u32 i2, u32 i1, u32 i0, u32 material }   (order in the file: last index first)
```
Same material encoding; UVs for these triangles come directly from the material entry
(`entry+8-4k` / `entry+0x18-4k`). Only the Woody model (712 of them) and a few others use this.

### 2.6 INSTANCES (engine object 0xfc bytes, vtable `0x4aa31c`, ctor `0x42e1a0`)

```
u32 ninstances                        -> S+0
per instance:
   u32   unk0          read into a local, unused (0; once per level 8209, sometimes 22)
   TRAJ  trajectory    see §6                                        -> inst+0x78
   vec3  position                                                    -> inst+0xc
   4×f32 quaternion (x,y,z,w)  -> 3×3 rotation matrix (0x440370)     -> inst+0x28..0x4c
   3×f32 scale (x,y,z), usually 1.0                                  -> inst+0x4c,0x50,0x54
   u32   id            0x01000000 | slot                             -> inst+4; level[0x6c][slot] = level[0x40][slot] = inst
   (nvol + ncol) × u32 VM ids                                        -> inst+0x70
```
`nvol` = number of volume nodes (§2 #10), `ncol` = `S+0x60`. The first `nvol` ids are
`0x03000000 | world_volume-index` (one per volume node, in list order), the last `ncol`
`0x07000000 | world_collision-index` (selected via bits 16–31 of the press-node flags,
`0x436dc0`). The loader also sets `inst+0xf8 = S`, `inst+0xf4` = `len(mesh nodes)` × 28 B
runtime state, `inst+8` = flags (`&0xc1|1`), `inst+0x1c` = world cell via `0x407790`
(logs "An object outside the world" if −1), `inst+0x74/0x88` = lights (§2.1).

## 3. Trajectory (sub-reader `0x428bc0`, engine object 0x14 bytes)

```
u32 npoints          (0 → no object, NULL pointer)
npoints × { u32 unk (always 0), f32 x, f32 y, f32 z }   -> T+0x10 (16 B/point)
u32 closed           -> bit 16 of T+0 (closed path)
```
`T+0` low16 = npoints, `T+0xc` = total path length (sum of segments, `0x437ca0`; only +4..+0xc are used).

## 4. CAMERA (engine object 0x2c bytes, ctor `0x498b90`, vtable `0x4ac040`; with path `0x4aa224`)

```
vec3 position        -> cam+0xc..0x14
u32  id              0x01000000 | slot  -> cam+4; level[0x6c][slot] = level[0x40][slot] = cam
TRAJ trajectory      -> cam+0x28 (when present the camera gets vtable 0x4aa224)
```
`cam+8 = (cam+8 & ~0x1c) | 3`, `cam+0x1c` world cell via `0x407790` ("A camera outside the world").
Cameras thus share object slots with instances (same tag 0x01).

## 5. Meaning of the VM references

| Tag | Meaning | Source |
|---|---|---|
| `0x01000000 \| i` | script object *i* = instance **or camera** in `level[0x6c][i]`; `nslots` == "nobj" in the `code` header (e.g. 507 in W1A). Not all slots are filled in the `.ins` (placeholders remain) | loader, `0x4012fb` etc. mask `& 0xffffff` |
| `0x02000000 \| i` | **script variable** *i*: in every level the highest index occurring is < `nvars` from the code header (e.g. W1A: max 154, nvars 161); does not appear in the `.ins` | code files |
| `0x03000000 \| i` | **world_volume** *i*: stored per instance in `inst+0x70[k]` for volume node *k*; the indices are 0..nvol−1 contiguous and `nvol` matches the code header (W1A 218/218; W2B, W2D, W3B, K3A, S3A, W3A have 1–4 more volumes in the code than in the `.ins`). `0x430210` tests every frame whether an actor is within all planes of the volume node (convex polyhedron in instance space, with scale) and sends message 0x65/0x66/0x67 (Enter/Leave/In) with (instance id, volume id, actor id); `0x443e20` indexes table `[0x5d0544]` (16 B/entry) with `id & 0xffffff` | |
| `0x07000000 \| i` | **world_collision** *i*: `inst+0x70[nvol + sub_index]`; `0x4303e0`/`0x436dc0` send Press/UnPress/In (`0x441f00`, `0x4420c0`, …) via table `0x443d90`. Count matches `ncol` in the code header | |

The **geometry of the volumes** is thus indeed defined by the `.ins`: every
volume node is a convex polyhedron (usually an 8-point/6-polygon box, pivot 0) in the
model hierarchy, placed via the instance matrix. The `code` file only contains the watcher lists.

## 6. Other engine details

- Animation clock (`0x43eee0`): phase = (now − `inst+0xa8`) / (duration/4096 / `inst+0xa0`); `inst+0xac` = phase·duration; frame = phase·nframes. `inst+0xb0` = current animation index, `inst+0xb4` = next.
- Track references (`off`, `cnt`) with `cnt = 0` mean: no keyframes for this animation (node stays at the last computed/identity matrix).
- Bounding-box node (`S+0x24`) is used for the centroid/bounds (`0x42e580`, `0x42eedd`); mesh nodes from `S+0x30` also act as collision spheres (radius `N+0x2c`) against the world (`0x42f110`).
- Marker nodes (`S+0x50`): 2 points; `0x42f6b0(typecode, n, out)` returns the transformed points of the *n*-th marker with that type code (attachment points for script/effects).
- Material table in `.tex`: after `u32 nouter, u32 ntotal` and the textures (36 B header + frames·w·h·2) comes `u32 nmat` and `nmat × {u32 texture, 12×f32}`; the engine keeps f0,f3,f6,f9 (u row) and f1,f4,f7,f10 (v row).

## 7. Uncertainties / open questions

1. Type codes in flag bits 8–15 (1,2,4,5,6,7,9): only code 1 (press) and the lookup function `0x42f6b0` are verified; the meaning of the other codes (attachment points for hand/feet? camera targets?) has not been determined.
2. Helper nodes (0x10): `mode` 0/1/2 and the two scales are applied to the parent mesh in the renderer (presumably texture-coordinate/effect projection, values like 35×40, 47×47); exact function not worked out.
3. Event records type 4 (9 dwords: `{4, t, u32, u32, f32, f32, u32=100, f32=200, u32}`) and type 5 (6 dwords: `{5, t, u32, u32, 0, 0}`) on the root node of the Woody model: probably sound/footstep/effect triggers; the meaning of the parameters is unknown. Type 3 (matrix override) does not occur in the data.
4. Light node `extra` (0 or 4) and the per-instance field `unk0` (8209 at exactly one instance per level, 22 in the hub levels) are not used by the loader; meaning unknown.
5. The extra lists (`node+0x8c`) have no structure enforced by the loader; their consumers `0x4738c0`/`0x494f5d` have no callers in the disassembly.
6. Polygon flag bits (0x2, 0x8) and the UV-generation exception (bit 0) are derived only from the loader; render behaviour not verified.
7. The first dword of every trajectory point is 0 in all data and is skipped by the path-length computation; possibly time/speed.
8. Some levels have more world_volumes/collisions in the code header than in the `.ins` (e.g. W2B 215 vs 211); the missing ones may be defined elsewhere (Woody.rck/`.gel`?) or not at all.
