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

typedef struct {
    uint8_t *data; size_t size;
    uint32_t npolys; GelPoly *polys;
    uint32_t nverts; GelVert *verts;
    float bbox[6];
} GelFile;

/* ---- .lit: precomputed light visibility (docs/LIGHTING.md) ---------------------- */
typedef struct { uint32_t n; float plane[4]; int32_t *indices; } LitPoly;   /* index < 0: extra vertex -i-1 */
typedef struct {
    Vec3 pos; float colour[3]; float range;   /* colour 0..255 */
    uint32_t na, *a;                           /* faces the light sees completely */
    uint32_t nb, *b;                           /* partially lit faces (parents of the polygons) */
    uint32_t nc; LitPoly *c;                   /* the lit fragments of the b faces */
    uint32_t nbsp; uint32_t *bsp;              /* {plane, front, back} */
    uint32_t nplanes; float *planes;           /* 4 floats each */
} LitLight;
typedef struct {
    uint8_t *data; size_t size;
    uint32_t nlights; LitLight *lights;
    uint32_t nextra; Vec3 *extra;
} LitFile;

/* ---- .ins ------------------------------------------------------------------ */
typedef struct { uint32_t off, cnt; } TrackRef;

typedef struct {
    uint32_t material;                         /* bit 15 set: ARGB1555 flat colour; else index in TexFile.materials */
    uint32_t flags, nverts;
    uint32_t *indices;                         /* absolute indices into Model.points */
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
    Mat4 world;                                 /* instance placement */
    /* base class state driven by the script (instance.c, docs/INSTANCE.md) */
    int scripted;                               /* 0 = animation owned by other code (the player) */
    int slot[4]; float a_speed, a_base_speed, a_start, a_pos; int a_ended;   /* +0xb0.., +0xa0, +0xa4, +0xa8, +0xac, +0x9c */
    float fade, fade_target, fade_rate; int noncollide; uint32_t setflags;   /* +0x6c, +0xfc, +0x100, +8 & 0x40, +0xf0 */
    uint32_t traj_flags; float traj_start, traj_dur;                         /* path follower (+0x78) */
    Mat4 *node_world;                           /* per node, updated by ins_pose() */
    Vec3 ldir; float lcol[3]; int l_init, light, l_seen;   /* model lighting: smoothed light vector, light colour, chosen light, seen by it (0x43b912, 0x42e3e4) */
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
    int32_t *owner;                             /* per point: owning node (built lazily by the renderer) */
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
int  lit_point_lit(const LitLight *l, const GelFile *g, Vec3 p);   /* BSP point query 0x40b540 + leaf plane test */
void tex_free(TexFile *t); void gel_free(GelFile *g); void ins_free(InsFile *f);

/* Animation: evaluate the node hierarchy of `inst` for animation `anim` at `t` seconds
 * (wraps around); writes inst->node_world[] (includes the instance placement). */
void ins_pose(Instance *inst, int anim, float t);
/* Transform point i of the model (owner node applied, pivot subtracted) to world space. */
Vec3 ins_point_world(const Instance *inst, uint32_t point_index);
/* Which node owns point index i (0-based node index). */
int  ins_point_owner(const Model *m, uint32_t point_index);

/* math helpers */
void mat4_identity(Mat4 *m);
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b);      /* out = a * b (apply b first) */
void mat4_from_trs(Mat4 *m, Vec3 t, Quat q, Vec3 s);
Vec3 mat4_apply(const Mat4 *m, Vec3 v);
void rgb565_to_rgba(const uint16_t *src, uint8_t *dst, uint32_t n, int colour_key);
void argb1555_to_rgb(uint32_t v, float rgb[3]);

#endif
