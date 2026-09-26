# File format `.gel` (level geometry)

Derived from the loader at `0x407ae0` in Woody.exe (called from the
level loader `0x426bd0`, which afterward loads `.tex`, `.ins` and `.col`;
then `0x408260` loads the `.vis` and `0x40ac30` the `.lit`). All values
little-endian, floats IEEE single. There is no magic and no version number.
Parser: `tools/gelparse.py` (consumes all 28 files in `extract/Data` exactly
to the end and checks internal consistency).

The file consists of seven sections placed strictly one after another:

| # | Section | Engine field (gel object, `ecx` of `0x407ae0`) |
|---|---|---|
| 1 | Polygons (triangles) | `+0x0c` count, `+0x10` `ptr[]` to records |
| 2 | Portal polygons | `+0x2c` count, `+0x30` `ptr[]` (NULL if count 0) |
| 3 | Groups/zones + portal lists | `+0x34` count, `+0x38` array (0x18 B each) |
| 4 | Vertices | `+0x04` count, `+0x08` array (0x30 B each) |
| 5 | kd leaf cells | `+0x18` count, `+0x1c` `ptr[]` to objects (0x4c B) |
| 6 | kd tree | `+0x14` array (16 B/node); count is not stored |
| 7 | Sectors | `+0x20` count, `+0x24` `ptr[]` to objects (0x4c B) |

The gel object is the base class (vtable `0x4a94f0`) of the level object
(0x78 B, vtable `0x4aa220`) and sits in the global `0x4c4c0c`. The fields
`+0x28` (per-sector `.vis` lists), `+0x3c/+0x40` (count/array of dynamic
world objects) and `+0x44` are filled by other loaders and are not part
of `.gel`.

## 1. Polygons

```
u32 count            -> gel+0x0c
u32 total_indices    (= sum of nverts; only used for the malloc size)
count x record:
```

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | 4 | u32 | `nverts` (always 3 in all 28 levels) |
| 0x04 | 4 | u32 | `material`: bits 0..14 = index into the material table of `<LVL>.tex` (`level+0x5c`, 0x24 B each: 8 uv-projection floats + texture pointer at +0x20); bit 15 (`test ah,0x80`) = **no material: collision only, never drawn** (e.g. 0x8000 in Blackbox, 0xDAD6 in W3B/K3A). The renderer skips such a face before anything else (`0x42acd3`, see SKY.md §1), while collision and the ground type (`0x462948`) read it normally: this is how the invisible barriers are built — the "glass panel" of W1A is one of them (issue #2) |
| 0x08 | 16 | 4 x f32 | plane `(nx, ny, nz, d)`; test in `0x40a0c0`: `nx*x + ny*y + nz*z + d` |
| 0x18 | 4·n | u32[n] | vertex indices (into section 4) |

Engine record (0x1c + 4·n bytes, `0x407bee`): `+0` nverts, `+4` frame stamp
(runtime, init -1 by `0x408420`; set by the renderer `0x42ac10` and collision
`0x407000` to avoid processing twice), `+8` material, `+0xc` plane,
`+0x1c` indices. `gel_render.py` read the records offset by 16 bytes; its
"two extra dwords" are `nverts` and `material` of the next record.

Negative vertex indices do not occur in the files; the renderer
(`0x42c38c`) does interpret them as dynamic vertices from the `.lit`
object.

## 2. Portal polygons

```
u32 count            -> gel+0x2c   (may be 0; u32 total_indices still follows)
u32 total_indices
count x record:
```

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0x00 | 4 | u32 | `nverts` (always 3) |
| 0x04 | 16 | 4 x f32 | plane |
| 0x14 | 4·n | u32[n] | vertex indices |

Same engine record as section 1, but `material` is set to -1
(`0x407cea`). These are axis-aligned quads (two triangles, planes
±x/±z) that serve as portals between groups; they are addressed by
section 3. 22 of the 28 levels have none.

## 3. Groups (zones) and portal lists

```
u32 count                         -> gel+0x34
count x u32 end                   exclusive end index into section 1
count x { u32 k ; k x { u32 portal ; u32 group } }
```

Group *i* contains the polygons `[end[i-1], end[i])` (with `end[-1] = 0`);
the last `end` equals the polygon count. Each group is followed by a list
of `k` pairs `(portal index in section 2, index of the neighboring group)`;
in every level `portal < count_2` and `group < count_3` hold.

Engine record (0x18 B, `0x407d52`/`0x407d95`): `+0` frame stamp (runtime,
init -1 by `0x40ab00`; "group visible this frame", `0x42aa9b`),
`+4` first polygon, `+8` last polygon (`end-1`), `+0xc` k,
`+0x10` `u32* portal`, `+0x14` `u32* group`.

Use: the `.vis` lists (per sector, per floor group, `0x408210`) yield pairs
`(sector, group)`; the renderer `0x42a980` marks those groups and `0x42ac10`
stamps every polygon in the range of each visible group. `0x40a26a`
looks up the group of a polygon index. No mapping from `.ins` instances to
these groups has been found in the `.ins` loader (`0x427290`); it only uses
`0x407790` (sector of a point).

## 4. Vertices

```
u32 count            -> gel+0x04
count x 16 B:  f32 x, f32 y, f32 z, u32 colour
```

`colour` has its high byte 0 in every level. Byte order is **R,G,B,0**
(so `0x00BBGGRR` as a little-endian u32, NOT D3DCOLOR), and **128 = neutral**:
the renderer modulates the texture with 2x the vertex color (D3D
`MODULATE2X`; `0x808080` is the most common value, `0x00fefe` = yellow,
`0x0000fe` = red). Verified against a screenshot of the original (W1A:
yellow edges, red start platform).
Engine record
0x30 B: `+0` xyz, `+0x28` colour, `+0x2c` frame stamp (runtime, init -1;
transform cache), the rest is not written on load.
y is the vertical axis (see `0x40a0c0`, which finds the floor via `ny > 0`).

## 5. kd leaf cells (and 7. sectors: identical record)

```
u32 count            -> gel+0x18 (cells) / gel+0x20 (sectors)
count x record:
```

| Offset | Size | Type | Meaning | Engine offset (object 0x4c B, vtable `0x4a94f4`) |
|---|---|---|---|---|
| 0x00 | 4 | u32 | `npoly` | `+0x08` |
| 0x04 | 4·npoly | u32[] | polygon indices (section 1) intersecting this cell | `+0x0c` (malloc) |
| .. | 24 | 6 x f32 | bbox `xmin, xmax, ymin, ymax, zmin, zmax` (order proven by `0x406e50`) | `+0x28..+0x3c` |
| .. | 24 | 6 x i32 | neighbor link per bbox face, order `-x, +x, -y, +y, -z, +z` | `+0x10..+0x24` |
| .. | 4 | u32 | `nnodes` | (not stored) |
| .. | 16·nnodes | node[] | local kd subtrees for the neighbor links (see 6 for the node layout) | `+0x48` (malloc, NULL if 0) |

Neighbor link encoding (`0x40a0c0`, which follows link 2 = `-y` to find the
floor; the full floor search is in RACE.md §2.1, ported as `gel_floor_poly`, which keeps the links and
local subtrees in `GelCell.link/nodes` since round 30):

* `0x80000000` (INT_MIN): no neighbor (world edge).
* `< 0`: exactly one neighbor cell, index `~link`.
* `>= 0`: root of a subtree in the local node array; the engine calls
  `0x40ab60(&nodes[link], point)`. **Child indices in such a subtree are
  relative to the root** (`ecx*16 + edi`), so child *k* = `nodes[link+k]`.
  Each subtree is a contiguous block; every node of a cell is used by
  exactly one of the six links. Leaves `~c` are cell indices in the same
  array (cells resp. sectors). Verified: every neighbor found this way
  borders exactly the relevant bbox face (0 failures across all levels).

The high 16-bit word of the node type in these local trees is 0 or a
garbage value (one constant per level, e.g. 1134 in W1A) and is never
read.

Runtime fields of the object: `+0x04` frame stamp (init -1 by the virtual
init `0x406e40`; the collision walks of 5.1 stamp visited cells with `[0x4c3bac]`),
`+0x40/+0x44` count/array of the objects registered in the cell (for leaf cells:
filled once from the `.col` file by `0x4271e0` and never written again - a
**static** registration with a per-ref phase mask, FORMAT_TEX_COL_VIS_LIT.md 2;
index into `gel+0x40` in the low 16 bits); for sectors, `+0x44`
is the head of the linked list of entities in the sector (`0x407790`
inserts, `0x407850` removes; entity `+0x1c` = sector, `+0x18` =
floor **group** (return value of `0x40a0c0`, `0x4077bc`; read by `0x42a840`, RACE.md §2.1), `+0x24` = next).
The sector chains are the only per-instance cell bookkeeping that changes at run time; the collision queries do not
read them (they use the leaf cells' `.col` lists), only the per-frame instance list `0x42a840` does.

### 5.1 The collision walks over the leaf cells

Every geometry query collects the leaf cells it has to look at into the list `0x4c4be8` (count `[0x4c4be4]`, allocated by
`0x406fc0`) and then tests the world polygons of those cells and the instances registered in them (`.col`, in the order of
this list; PERSO_MOVE.md 6.7). Four walks, all starting in the cell of the query point (`0x408180`; the callers pass -1):

| Walk | Used by | Which cells, in which order |
|---|---|---|
| `0x40aa30(c, r, up, down, cell)` | cylinder push-out `0x407000` | flood fill: the start cell (stamped and appended; `0x40aa92` calls the cell test on it but ignores the answer), then depth first over its six links in the order `-x +x -y +y -z +z` (`0x40aaa9`). A link `>= 0` is a local subtree whose nodes are **all** visited, the `<=` child first (recursion on `node+8`, then loop on `node+0xc`, `0x40a930`); a leaf `~c` is stamped **before** its test (`0x40a99d`), and when the cell test `0x40a7a0` passes it is appended and its own six links are walked. `0x40a7a0`: out if `c.y + up < ymin` or `c.y - down > ymax`; then region codes in xz (`x > xmax` 8, `x < xmin` 4, `z > zmax` 2, `z < zmin` 1, jump table `0x40a900`): inside -> in, one axis out -> in when `c + r > min` / `c - r < max`, a corner -> in when the xz distance to the corner `< r`. |
| `0x40a700(c, r, cell)` | sphere push-out `0x407340` | the same flood (`0x40a640`; the start cell is not tested at all) with the sphere test `0x40a290` (three axes, table `0x40a5a0` via the byte map `0x40a610`: squared distance from c to the box `< r²`). |
| `0x498520(p, cell)` (in GetHeight `0x498440`) | floor `0x435650` | p's cell, then while the cell holds no floor polygon the cell under it through link 2 (`-y`, `+0x18`): a subtree is descended with p itself (`0x40ab60`), `~link` directly, `INT_MIN` ends (`0x498737..0x498776`). A cell only accepts floors within its own height (`maxd = p.y - ymin`, `0x4985b9`), so the walk ends in the cell that holds the floor point. |
| `0x497fb0(a, b, cell)` / `0x497b10(a, dir, cell)` | segment ray `0x497ed0` / endless ray `0x497a30` | along the ray: in the first cell the candidate exit faces are fixed - the ray moves towards the face (`0x406ee0(dir, f) < 0`: `f` even `dir[f/2]`, odd `-dir[f/2]`) and a lies inside it (`0x406e50(a, f) > 0`: even `a - min`, odd `max - a`); per cell the exit face is the one with the smallest `dist / -dir` (the first on a tie), the polygons count only up to that fraction, and when none was hit the walk continues through the exit face's link (a subtree descended at the exit point `a + dir·t`, `0x40ab60`, or `~link`; `INT_MIN` = out of the world, raw 0). The segment also ends in the cell where `t_exit > 1` (b lies inside, `0x498127`). |

The flood's recursion follows every link whose leaf passes the cell test, so for the convex query volumes the collected set
is exactly the set of cells whose box passes the test (port check: 4000 random queries of each kind in W1A, W3D, K1R, WWS
and House, 0 differences from brute force over the cell boxes). Port: `gel_walk_cyl`, `gel_walk_sphere`, `gel_walk_down`,
`gel_walk_seg` in `src/level.c`; the port finds the world hit with its own polygon queries and hands the walk the point
where it has to stop (the floor point, the hit fraction).

For sectors it additionally holds that: the bbox is exactly the union of
the bboxes of the leaf cells under the sector root in section 6, and the
polygon list equals (in a few cases is a superset of) the union of the
cell polygon lists.

## 6. kd tree

```
u32 count            (in every level = number of cells - 1)
count x 16 B:
```

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | i32 | low 16 bits (sign-extended, `0x40ab10`): axis 0=x, 1=y, 2=z; high 16 bits (`sar 16`): -1 for a regular node, otherwise **sector index** (this node is the root of that sector's subtree) |
| 0x04 | f32 | `d`; test `point[axis] + d <= 0` (so `d = -splitvalue`) |
| 0x08 | i32 | child if the test is true; `>= 0` node index (absolute, within this array), `< 0` leaf = cell `~child` |
| 0x0c | i32 | child if the test is false; same encoding |

Root is node 0. `0x408180` descends to a leaf and returns the cell index
(section 5); `0x4081c0` descends to the first node with a non-negative
high word and returns that word (sector index, section 7). Every sector
has exactly one root node. Verified: the center of every cell bbox
descends into that cell itself (100%).

## Overview of the file order

```
[1] u32 npoly, u32 nidx, npoly x { u32 n, u32 material, f32 plane[4], u32 idx[n] }
[2] u32 nport, u32 nidx, nport x { u32 n, f32 plane[4], u32 idx[n] }
[3] u32 ngroup, u32 end[ngroup], ngroup x { u32 k, k x { u32 portal, u32 group } }
[4] u32 nvert, nvert x { f32 x, y, z, u32 colour }
[5] u32 ncell, ncell x { u32 np, u32 poly[np], f32 bbox[6], i32 link[6], u32 nn, node[nn] }
[6] u32 nnode, nnode x { i32 type, f32 d, i32 child_le, i32 child_gt }
[7] u32 nsect, nsect x { same record as [5] }
```

## Validation

`python tools/gelparse.py` parses all 28 `.gel` files; each one is consumed
exactly to the last byte, and the following invariants hold everywhere:
indices within range, `end[-1] == npoly`, portal pairs within range,
sector roots = exactly 0..nsect-1, neighbor trees without cycles and
without unused nodes, every neighbor borders the correct face.

## Still uncertain

* No other bits >15 of `material` occur. (Bit 15 itself is resolved:
  invisible, collision only — §1.)
* Why some sectors contain more polygons than their leaf cells combined
  (2 of 128 in W1A, 1 of 193 in W3B).
* The high word of the node type in the local neighbor trees (garbage or
  meaningful for the tool; the engine does not read it).
* Whether `.ins` instances reference groups (section 3) anywhere; not
  found in the `.ins` loader, though `.vis` does reference them.
