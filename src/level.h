/* level.h - in-memory representation and loaders for the level files of
 * Woody Woodpecker: Escape from Buzz Buzzard Park (PC, 2001).
 * Formats: docs/FORMAT_GEL.md, docs/FORMAT_INS.md, docs/FORMAT_TEX_COL_VIS_LIT.md.
 * Coordinates are kept exactly as in the files (D3D left-handed, y up). */
#ifndef WOODY_LEVEL_H
#define WOODY_LEVEL_H
#include <stdint.h>
#include <stddef.h>

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z, w; } Quat;
typedef struct { float m[16]; } Mat4;          /* column-major, OpenGL style */

/* ---- .tex --------------------------------------------------------------- */
typedef struct {
    uint32_t width, height, flags;             /* flags bit0 = magenta colour key */
    float scroll_u, scroll_v, anim_duration;
    uint32_t frame_count;
    uint16_t **frames;                         /* frame_count pointers into file data (RGB565, row-major) */
    uint32_t gl_tex;                           /* renderer handle of the current frame */
    uint32_t *gl_frames;                       /* renderer handles per frame (frame_count) */
} TexGroup;

typedef struct {
    uint32_t group;
    float m[12];                               /* u = m0 x + m3 y + m6 z + m9 ; v = m1 x + m4 y + m7 z + m10 */
} Material;

typedef struct {
    uint8_t *data; size_t size;
    uint32_t ngroups; TexGroup *groups;
    uint32_t nmaterials; Material *materials;
} TexFile;

/* ---- .gel (only what the renderer needs: polygons + vertices) -------------- */
typedef struct {
    uint32_t nverts;
    uint32_t material;                         /* bit 15 = no material */
    float plane[4];
    uint32_t *indices;
} GelPoly;

typedef struct { float x, y, z; uint32_t colour; } GelVert;

typedef struct { int32_t axis, sector; float d; int32_t le, gt; } KdNode;   /* 0x40ab10: p[axis] + d <= 0 -> le, otherwise gt; child < 0 = leaf ~child; sector >= 0 marks a sector root */

/* Sections 5 and 7 share one record (0x4c bytes, vtable 0x4a94f4): the leaves of the kd-tree and, one level up,
 * the sectors. Both carry the polygons that cross them, which is how the original answers every geometry question
 * without looking at the whole level: the floor under a point (0x40a0c0), the collision push-out (0x407000) and
 * which part of the world to draw (0x42a980) all start from the cell or sector the point falls in. */
typedef struct { uint32_t npolys; const uint32_t *polys; float bbox[6];              /* bbox order xmin xmax ymin ymax zmin zmax (0x406e50) */
                 int32_t link[6]; uint32_t nnodes; const uint8_t *nodes; } GelCell;   /* neighbour per face -x +x -y +y -z +z (+0x10..+0x24): INT_MIN none,
                                                                                      * < 0 cell ~link, >= 0 root of a local subtree in nodes (16 B each, +0x48) */
typedef struct { uint32_t first, end; } GelGroup;                                    /* section 3: the polygons [first, end) of one zone */
typedef struct { uint32_t n; const uint32_t *polys; } GelPolySet;                    /* result of a query below; valid until the next one */
struct GelQuery;

typedef struct {
    uint8_t *data; size_t size;
    uint32_t npolys; GelPoly *polys;
    uint32_t nverts; GelVert *verts;
    uint32_t ncells, nsectors;                 /* kd leaf cells and sectors; the sector a point is in decides its lights (docs/LIGHTING.md 3) */
    uint32_t nkd; KdNode *kd;                  /* the main kd-tree (0x408180) */
    float bbox[6];
    uint32_t ngroups; GelGroup *groups;        /* section 3, addressed by the .vis pairs */
    GelCell *cells;                            /* ncells kd leaves; a .col record belongs to each of them */
    GelCell *sectors;                          /* nsectors sectors; .vis and the .lit trailer are indexed by these */
    uint32_t nloose; uint32_t *loose;          /* polygons that no cell or sector lists: never cull these away */
    struct GelQuery *q;                        /* scratch of the queries below (level.c) */
} GelFile;

/* ---- .vis: the potentially visible set of a sector (docs/FORMAT_TEX_COL_VIS_LIT.md 3) ------------- */
typedef struct { uint32_t id, npairs; const uint32_t *pairs; } VisList;   /* npairs x (sector, flag); flag = the group of section 3 */
typedef struct { uint32_t nlists, first; } VisSector;                     /* lists [first, first+nlists) of VisFile.pool */
typedef struct {
    uint8_t *data; size_t size;
    uint32_t nsectors; VisSector *sectors;
    VisList *pool;
} VisFile;

/* ---- .lit: precomputed light visibility (docs/LIGHTING.md) ---------------------- */
typedef struct { uint32_t n, face; float plane[4]; int32_t *indices; } LitPoly;   /* index < 0: extra vertex -i-1; face = P+0x08, the parent's MATERIAL word, not a face index (render_gl.c finds the parent B face) */
typedef struct {
    Vec3 pos; float colour[3]; float range;   /* colour 0..255 */
    uint32_t na, *a;                           /* faces the light sees completely */
    uint32_t nb, *b;                           /* partially lit faces (parents of the polygons) */
    uint32_t nc; LitPoly *c;                   /* the lit fragments of the b faces */
    uint32_t nbsp; uint32_t *bsp;              /* {plane, front, back} */
    uint32_t nplanes; float *planes;           /* 4 floats each */
} LitLight;
typedef struct { uint32_t n; const uint32_t *idx; } LitSector;   /* the lights of one gel sector (trailer -> lightsys+0x10) */
typedef struct {
    uint8_t *data; size_t size;
    uint32_t nlights; LitLight *lights;
    uint32_t nextra; Vec3 *extra;
    uint32_t nsectors; LitSector *sectors;     /* trailer: one light list per gel sector; 0 when the .lit has no trailer */
} LitFile;

/* ---- .ins ------------------------------------------------------------------ */
typedef struct { uint32_t off, cnt; } TrackRef;

typedef struct {
    uint32_t material;                         /* bit 15 set: ARGB1555 flat colour; else index in TexFile.materials */
    uint32_t flags, nverts;                    /* flags bit 1 (0x2) = double sided, never back-face culled (0x43bf65) */
    uint32_t *indices;                         /* absolute indices into Model.points */
    float plane[4]; int plane_ok;              /* polygon plane in pivot-relative node space, built on first use (0x4280c2) */
} InsPoly;

typedef struct {
    uint32_t flags, kind, type_code, sub_index;
    uint32_t npolys, npoints, point_base;
    Vec3 pivot;
    uint32_t a, b, c;                          /* track pool sizes: dwords, dwords, bytes */
    uint8_t *pool;                             /* a+b+c/4 dwords */
    TrackRef *pos_refs, *rot_refs, *event_refs;/* per animation (or NULL) */
    int32_t first_child, next_sibling, parent; /* 0-based node indices, -1 = none/root */
    InsPoly *polys;
    float box[6]; int box_state;               /* pivot-relative aabb of the node's own points, built on first use (0 = not yet, -1 = the points are not all this node's) */
    /* light / helper / marker payload */
    float light_intensity; uint32_t light_colour; float helper_a, helper_b; uint32_t helper_mode; float marker_value;
} InsNode;

typedef struct { Vec3 pos, normal, colour; } InsPoint;
typedef struct { uint32_t i0, i1, i2, material; } InsTri;
typedef struct { uint32_t nframes, duration_4096; float duration_s; } InsAnim;
typedef struct { uint32_t npoints; Vec3 *points; uint32_t closed; } Trajectory;

struct Model;
typedef struct Instance {
    struct Model *model;
    uint32_t unk0;
    Trajectory traj;
    Vec3 position; Quat quat; Vec3 scale;
    uint32_t id, index;
    uint32_t nids; uint32_t *ids;              /* 0x03.. world_volume ids then 0x07.. collision ids */
    /* runtime */
    int visible; int anim; float anim_time; float anim_speed; int type;   /* type from SetTypeInstance */
    int snd_anim; float snd_tf;                 /* sound events: animation and frame time at the previous tick */
    Mat4 world;                                 /* instance placement */
    /* base class state driven by the script (instance.c, docs/INSTANCE.md) */
    int scripted;                               /* 0 = animation owned by other code (the player) */
    int slot[4]; float a_speed, a_base_speed, a_start, a_pos; int a_ended;   /* +0xb0.., +0xa0, +0xa4, +0xa8, +0xac, +0x9c */
    float fade, fade_target, fade_rate; int noncollide; uint32_t setflags;   /* +0x6c, +0xfc, +0x100, +8 & 0x40, +0xf0 */
    uint32_t traj_flags; float traj_start, traj_dur;                         /* path follower (+0x78) */
    Mat4 *node_world;                           /* per node, updated by ins_pose() */
    int tint_red;                               /* render colour hook vtbl[26]: vertex colours times (1,0,0) (the rocket's warning blink 0x4537d0) */
    float tint_scale;                           /* the same hook as a factor on the lit colour, 0 = off: 0.1 = a locked figure of the world-select carousel (0x451a40) */
    int tint_mode; float tint_rgb[3];            /* the same hook in general: [0x5ac850] 1 = lit vertex colour times rgb, 2 = plus rgb (1.0 = 255), 0 = off: the bomb, black with red flashes (0x44d9a0) */
    Vec3 ldir; float lcol[3]; int l_init, light, l_seen;   /* model lighting: smoothed light vector, light colour, chosen light, seen by it (0x43b912, 0x42e3e4) */
    int tex_mode; float tex_t0, tex_fac;                   /* +0xd8 bits 0-2, +0xdc, +0xe0: texture frame override, messages 16 / 18 / 19 (docs/INSTANCE.md 2) */
    int uv_mode; float uv_t0, uv_fac, uv_t2;               /* +0xd8 bits 3-5, +0xe4, +0xe8, +0xec: UV scroll override, messages 15 / 17 / 19 (docs/INSTANCE.md 2) */
    int drawn;                                             /* set by the renderer each frame: this instance survived the visibility pass */
} Instance;

typedef struct Model {
    uint32_t nnodes, nanims;
    InsAnim *anims;
    uint32_t first_top_node, bbox_node;
    InsNode *nodes;
    uint32_t npoints; InsPoint *points;
    uint32_t ntris; InsTri *tris;
    uint32_t ninstances; Instance *instances;
    uint32_t nvolume_nodes, *volume_nodes; uint32_t nmesh_nodes, *mesh_nodes;
    uint32_t ncollision_ids;
    int32_t *owner; float cull_r;                             /* per point: owning node (built lazily, ins_point_owner) */
    uint32_t ncoll, *coll; int coll_ok;                       /* the press nodes (kind 1), built lazily */
} Model;

typedef struct { Vec3 position; uint32_t id, index; Trajectory traj; } Camera;

typedef struct {
    uint8_t *data; size_t size;
    uint32_t nslots;
    uint32_t nmodels; Model *models;
    uint32_t ncameras; Camera *cameras;
    Instance **slots;                          /* nslots pointers (instance or NULL) */
    Camera **cam_slots;
} InsFile;

/* ---- API ----------------------------------------------------------------------- */
int  tex_load(TexFile *t, const char *path);   /* 0 on success */
int  gel_load(GelFile *g, const char *path);
int  ins_load(InsFile *f, const char *path);
int  lit_load(LitFile *l, const char *path);
int  vis_load(VisFile *v, const char *path, uint32_t nsectors);   /* nsectors comes from the .gel; -1 when the file is missing or does not match */
int  lit_point_lit(const LitLight *l, const GelFile *g, Vec3 p);   /* BSP point query 0x40b540 + leaf plane test */
int32_t lit_bsp_face(const LitLight *l, const GelFile *g, Vec3 p); /* 0x40b540 itself: the leaf face, -1 = none */
int32_t gel_sector(const GelFile *g, Vec3 p);                      /* 0x4081c0: the kd sector a point is in, -1 = none */
int32_t gel_cell(const GelFile *g, Vec3 p);                        /* 0x408180: the kd leaf cell a point is in, -1 = none */
/* 0x40a0c0(p, -1): the polygon of the floor under p (last match in the first cell down the -y links that has one), -1 = none;
 * gel_floor_group = 0x40a26a of that polygon: the section-3 group it lies in, -1 = none (docs/RACE.md 2.1) */
int32_t gel_floor_poly(const GelFile *g, Vec3 p);
int32_t gel_floor_group(const GelFile *g, Vec3 p);
/* 0x408210: the .vis list of p - the entry of p's sector whose id is the floor group under p, else the sector's first entry. NULL = none */
const VisList *vis_entry(const VisFile *v, const GelFile *g, Vec3 p);
/* SetRaceInfo 0x455dc0 tail: the distinct floor groups under the points of the race polyline, in track order, at most 5, -1 terminated */
void gel_race_regions(const GelFile *g, const Vec3 *pts, uint32_t n, int32_t out[6]);
/* Every polygon that crosses a kd leaf meeting the box / the segment, each one once, plus the loose polygons.
 * The set lives in the level's own scratch buffer and is replaced by the next query on the same level. */
GelPolySet gel_polys_in_box(const GelFile *g, const float box[6]);   /* box: xmin xmax ymin ymax zmin zmax */
GelPolySet gel_polys_on_seg(const GelFile *g, Vec3 a, Vec3 b);
/* Sectors whose kd subtree meets the box (0x4081c0 widened to a box). Returns how many were written to out. */
uint32_t gel_sectors_in_box(const GelFile *g, const float box[6], int32_t *out, uint32_t max);
void tex_free(TexFile *t); void gel_free(GelFile *g); void ins_free(InsFile *f); void lit_free(LitFile *l); void vis_free(VisFile *v);

/* Animation: evaluate the node hierarchy of `inst` for animation `anim` at `t` seconds
 * (wraps around); writes inst->node_world[] (includes the instance placement). */
void ins_pose(Instance *inst, int anim, float t);
/* cinematics (docs/CINEMATIC.md): camera eye/target from the top-level nodes with flags 0x80 / 0x180 of an animation
 * (0x42fa80, with the cut detection of 0x43a660); 0 when the animation has no camera track */
int  ins_camera_eval(const Instance *inst, int anim, float phase, Vec3 *eye, Vec3 *target);
/* where the root motion of `anim` leaves the model (0x44edb0): C = W * root(anim, end) * root(anim 0, start)^-1 */
int  ins_root_end(const Instance *inst, int anim, Vec3 *pos, Vec3 *forward);
/* root motion 0x44e290: where the root of `anim` at `phase` 0..1 puts a model standing in `base` anim frame 0 (E = B^-1 . A . W) */
int  ins_root_at(const Instance *inst, int anim, float phase, int base, Vec3 *pos, Vec3 *forward);
/* Transform point i of the model (owner node applied, pivot subtracted) to world space. */
Vec3 ins_point_world(const Instance *inst, uint32_t point_index);
/* inst+0x60 (0x43f2f1): where the animation has put the model - the world position of its skeleton root. */
Vec3 ins_anim_centre(const Instance *inst);
/* Which node owns point index i (0-based node index). */
int  ins_point_owner(const Model *m, uint32_t point_index);
/* The nodes a collision query has to look at: the press nodes (kind 1). */
const uint32_t *ins_collision_nodes(const Model *m, uint32_t *count);
/* World aabb of one node's geometry under the instance's current pose, to reject it without touching its polygons.
 * 0 when the node has no usable box (no points of its own), and the caller has to take it as it comes. */
int  ins_node_world_box(const Instance *inst, uint32_t node, float out[6]);

/* math helpers */
void mat4_identity(Mat4 *m);
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b);      /* out = a * b (apply b first) */
void mat4_from_trs(Mat4 *m, Vec3 t, Quat q, Vec3 s);
Vec3 mat4_apply(const Mat4 *m, Vec3 v);
void rgb565_to_rgba(const uint16_t *src, uint8_t *dst, uint32_t n, int colour_key);
void argb1555_to_rgb(uint32_t v, float rgb[3]);

#endif
