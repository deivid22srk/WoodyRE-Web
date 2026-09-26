/* ambient.c - instance class 90, the "environment instance", modes 1 (motes) and 2 (rain). docs/AMBIENT.md.
 *
 * The class (ctor 0x403d2c: 0x150 bytes, base 0x42e1a0, vtable 0x4a90ac; think vt[3] 0x472560, vt[0x70/4] 0x472b30,
 * vt[0x74/4] 0x472b40, dtor 0x472e30) owns no geometry of its own: everything is measured on its FIRST VOLUME NODE
 * (model+0x4c[0], 1-based, node record model+0x68 + 0x90*(i-1), world matrix [0x509adc]+0xa0 row inst+0x5c+i-1).
 * The box is taken over the node's own points in NODE-LOCAL space (0x47285b / 0x472bde) and the author treats local z as
 * "up" (the 3ds Max axis): the rain falls along local -z from the plane z = maxZ - minZ, the motes start on z = minZ.
 *
 * Particles live in the one effect pool of the original (2000 x 0x50 at [0x5e823c]+0xdb8, docs/PARTICLES.md 0); the
 * port keeps its own pool of the same size for these two modes. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambient.h"
#include "player.h"
#include "hud.h"

typedef struct {
    Instance *in;
    int mode;                  /* +0xfc (1501) */
    int count, live;           /* +0x100 wanted (1502 / 1504), +0x108 alive: a mote that dies gives its place back */
    int off, fill;             /* +0x120 (1511; 1502 clears it via 0x472b30), +0x121 = "the stream starts empty": spawn at a random age */
    unsigned stamp;            /* +0x104 = the frame counter [[0x509adc]] of the last think; a mote dies on a frame its owner did not think */
    float rgb[3], life0;       /* +0x110..0x118 = rgb / 255, +0x11c = arg * 0.01: the base lifetime in s */
    int force; float interval, acc, zr; int rot, grid_ok;   /* +0x14c, +0x140, +0x13c, +0x148, +0x138, +0x124 != NULL */
    float grid[400][3];        /* +0x124: 5 x 5 cells x 16 start points on the top of the box (0x472b40) */
    int logged;
} Amb;

enum { K_MOTE = 1, K_DROP, K_RIPPLE };
typedef struct { int kind, owner; float age, life, speed, fallen, len; Vec3 pos, dir; } AmbFx;

#define AMB_MAX 32
#define FX_MAX 2000
static Amb g_amb[AMB_MAX]; static int g_namb;
static AmbFx g_fx[FX_MAX]; static int g_nfx;
static unsigned g_frame;
static uint32_t g_seed = 1;
static float rnd01(void) { g_seed = g_seed * 214013u + 2531011u; return (float)((g_seed >> 16) & 0x7fff) / 32767.0f; }   /* 0x43ff40 */

void ambient_reset(void) { g_namb = 0; g_nfx = 0; }

static Amb *amb_of(Instance *in, int create)
{
    for (int i = 0; i < g_namb; i++) if (g_amb[i].in == in) return &g_amb[i];
    if (!create || g_namb >= AMB_MAX) return NULL;
    Amb *a = &g_amb[g_namb++]; memset(a, 0, sizeof *a); a->in = in; a->interval = 0.2f; a->stamp = ~0u; return a;
}

/* the first volume node: 0-based index, or -1 ("une instance d'environnement n'a pas de volume", 0x4725c5) */
static int amb_node(const Instance *in)
{
    const Model *m = in->model;
    if (!m || !m->nvolume_nodes) return -1;
    uint32_t ni = m->volume_nodes[0] - 1; if (ni >= m->nnodes || !m->nodes[ni].npoints) return -1;
    return (int)ni;
}
/* 0x47285b..0x47290f: node-local aabb of the node's points (pivot-relative, as the loader leaves them) */
static void amb_box(const Instance *in, int ni, float lo[3], float hi[3])
{
    const Model *m = in->model; const InsNode *n = &m->nodes[ni];
    lo[0] = lo[1] = lo[2] = 1e30f; hi[0] = hi[1] = hi[2] = -1e30f;
    for (uint32_t k = 0; k < n->npoints; k++) {
        Vec3 p = m->points[n->point_base + k].pos; float v[3] = { p.x - n->pivot.x, p.y - n->pivot.y, p.z - n->pivot.z };
        for (int q = 0; q < 3; q++) { if (v[q] < lo[q]) lo[q] = v[q]; if (v[q] > hi[q]) hi[q] = v[q]; }
    }
}
/* the loader's polygon plane (0x4280c2, same as render_gl.c poly_plane): longest (R-Q)x(R-P) over consecutive triples */
static void amb_plane(const Model *m, const InsNode *n, const InsPoly *p, float out[4])
{
    float best = 0, nx = 1, ny = 0, nz = 0; const InsPoint *rb = NULL;
    for (uint32_t i = 0; i < p->nverts; i++) {
        const InsPoint *P = &m->points[p->indices[i]], *Q = &m->points[p->indices[(i + 1) % p->nverts]], *R = &m->points[p->indices[(i + 2) % p->nverts]];
        float ux = R->pos.x - Q->pos.x, uy = R->pos.y - Q->pos.y, uz = R->pos.z - Q->pos.z;
        float vx = R->pos.x - P->pos.x, vy = R->pos.y - P->pos.y, vz = R->pos.z - P->pos.z;
        float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx, l = sqrtf(cx * cx + cy * cy + cz * cz);
        if ((!rb || best < l) && l > 0.01f) { best = l; nx = cx / l; ny = cy / l; nz = cz / l; rb = R; }
    }
    out[0] = nx; out[1] = ny; out[2] = nz; out[3] = rb ? -(nx * (rb->pos.x - n->pivot.x) + ny * (rb->pos.y - n->pivot.y) + nz * (rb->pos.z - n->pivot.z)) : 0;
}
/* the original's 3x4 node matrix has rows = the images of the local axes (+0x00, +0x0c, +0x18) and the origin at +0x24;
 * the port's Mat4 is column-major, so row k is column k here */
static Vec3 m_point(const Mat4 *M, const float p[3]) { return mat4_apply(M, (Vec3){ p[0], p[1], p[2] }); }
static Vec3 m_dir(const Mat4 *M, const float d[3])
{
    const float *o = M->m;
    return (Vec3){ o[0] * d[0] + o[4] * d[1] + o[8] * d[2], o[1] * d[0] + o[5] * d[1] + o[9] * d[2], o[2] * d[0] + o[6] * d[1] + o[10] * d[2] };
}

static AmbFx *fx_new(int kind, int owner)
{
    if (g_nfx >= FX_MAX) return NULL;                                            /* a full pool drops the new record (0x47e172) */
    AmbFx *f = &g_fx[g_nfx++]; memset(f, 0, sizeof *f); f->kind = kind; f->owner = owner; return f;
}

/* 0x472b40 (vt[0x74/4], called by 1503): 25 cells over the x/y extent of the box, 16 random start points each, all on the
 * local plane z = maxZ - minZ; the rain direction (0.05, 0.05, -0.8) in local space; the spawn interval from the force */
static void amb_rain_init(Amb *a)
{
    int ni = amb_node(a->in); if (ni < 0) return;
    float lo[3], hi[3]; amb_box(a->in, ni, lo, hi);
    float zr = hi[2] - lo[2], dx = (hi[0] - lo[0]) * 0.2f, dy = (hi[1] - lo[1]) * 0.2f;   /* 0x4aab94 = 0.2 */
    int k = 0;
    for (int row = 0; row < 5; row++) for (int col = 0; col < 5; col++) for (int j = 0; j < 16; j++, k++) {
        a->grid[k][0] = (rnd01() + (float)col) * dx + lo[0];
        a->grid[k][1] = (rnd01() + (float)row) * dy + lo[1];
        a->grid[k][2] = zr;
    }
    for (int j = 0; j < 400; j++) rnd01();                                       /* +0x128: 400 floats rnd*45 + 90, never read again */
    a->zr = zr; a->rot = 0; a->acc = 0;
    switch (a->force) {                                                          /* jump table 0x472e18 */
    case 1: a->interval = 0.2f; break; case 2: a->interval = 0.15f; break; case 3: a->interval = 0.1f; break; case 4: a->interval = 0.05f; break;
    default: printf("ambient: La force du vent doit etre comprise entre 1 et 4 (%d)\n", a->force); break;   /* 0x4b7ad4; the interval stays */
    }
    a->grid_ok = 1;
}

void ambient_msg(Instance *in, int id, const uint32_t *args, int nargs)
{
    if (!in) return;
    Amb *a = amb_of(in, 1); if (!a) return;
#define AI(i) ((i) < nargs ? (int32_t)args[i] : 0)
    switch (id) {
    case 1501: a->mode = AI(1); break;                                           /* 0x46cd07 */
    case 1502:                                                                   /* 0x46cd2e: 0x4abc5c = 1/255, 0x4aa0ac = 0.01 */
        a->rgb[0] = AI(1) / 255.0f; a->rgb[1] = AI(2) / 255.0f; a->rgb[2] = AI(3) / 255.0f; a->life0 = AI(4) * 0.01f;
        a->live = 0; a->count = AI(5);
        a->off = 0; a->fill = 0;                                                 /* vt[0x70/4] = 0x472b30 */
        break;
    case 1503: a->force = AI(1); amb_rain_init(a); break;                        /* 0x46cda0 -> vt[0x74/4] = 0x472b40 */
    case 1504: a->count = AI(1); a->live = 0; break;                             /* 0x46cdcc */
    case 1511: a->off = AI(1) != 0; break;                                       /* 0x46cf44 */
    }
#undef AI
}

/* 0x4727d3 with mode 1: keep `count` motes alive. x anywhere across the box, z on its bottom, but y is drawn over the
 * z EXTENT (minY + rnd * (maxZ - minZ), 0x47296a): a slip in the original, kept. A point in front of the node's first
 * polygon is mirrored through it (0x472985; the loop never advances past polygon 0). Every mote moves along the
 * world image of the local normal of the polygon that faces most along local +z (0x472a47), i.e. up out of the box. */
static void amb_think_motes(Amb *a, int ai)
{
    if (a->off && !getenv("WOODY_AMBON")) return;                                /* 0x4727d3; WOODY_AMBON=1 (test hook) ignores 1511 and hiding */
    if (a->live == 0) a->fill = 1;
    if (a->live >= a->count) { a->fill = 0; return; }
    int ni = amb_node(a->in); if (ni < 0 || !a->in->node_world) return;
    const Model *m = a->in->model; const InsNode *n = &m->nodes[ni]; const Mat4 *M = &a->in->node_world[ni];
    float lo[3], hi[3]; amb_box(a->in, ni, lo, hi);
    float zr = hi[2] - lo[2], pl0[4] = { 0, 0, 0, -1 }, up[3] = { 0, 0, 1 };
    if (n->npolys) {
        amb_plane(m, n, &n->polys[0], pl0);
        float best = -2;
        for (uint32_t k = 0; k < n->npolys; k++) { float pl[4]; amb_plane(m, n, &n->polys[k], pl); if (k == 0 || pl[2] > best) { best = pl[2]; up[0] = pl[0]; up[1] = pl[1]; up[2] = pl[2]; } }
    }
    Vec3 N = m_dir(M, up);                                                       /* not normalised: a scaled node scales the speed */
    if (getenv("WOODY_AMBLOG") && !a->logged) {
        a->logged = 1;
        printf("ambient: inst %u mode 1 count %d life %.2f rgb %.2f %.2f %.2f box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f up %.2f %.2f %.2f world %.0f %.0f %.0f\n",
               a->in->index, a->count, a->life0, a->rgb[0], a->rgb[1], a->rgb[2], lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], N.x, N.y, N.z, M->m[12], M->m[13], M->m[14]);
    }
    while (a->live < a->count) {
        float p[3];
        p[0] = rnd01() * (hi[0] - lo[0]) + lo[0];
        p[2] = lo[2];                                                            /* mode 1: [esp+0x3c] = minZ */
        p[1] = rnd01() * zr + lo[1];
        if (n->npolys) {
            float d = p[0] * pl0[0] + p[1] * pl0[1] + p[2] * pl0[2] + pl0[3];
            if (d > 0) for (int q = 0; q < 3; q++) p[q] -= 2 * d * pl0[q];
        }
        Vec3 w = m_point(M, p);
        AmbFx *f = fx_new(K_MOTE, ai);                                           /* 0x47e160 */
        if (f) {
            float s = a->fill ? rnd01() : 0.0f;
            f->life = rnd01() * 2.0f + a->life0; f->age = f->life * s;          /* the first fill starts spread over the lifetime */
            f->dir = N;
            f->speed = rnd01() * 20.0f + 40.0f;                                  /* 0x4a9994 = 20, 0x4ab294 = 40 */
            f->pos = (Vec3){ w.x + N.x * f->age * f->speed, w.y + N.y * f->age * f->speed, w.z + N.z * f->age * f->speed };
        }
        a->live++;                                                               /* counted even when the pool was full */
    }
    a->fill = 0;                                                                 /* 0x472b13 */
}

/* 0x472596, mode 2: every `interval` s one drop per cell, from start point `rot` of that cell; rot then steps 1..16 */
static void amb_think_rain(Amb *a, int ai, float dt)
{
    int ni = amb_node(a->in); if (ni < 0 || !a->in->node_world || !a->grid_ok || a->interval <= 0) return;
    const Mat4 *M = &a->in->node_world[ni];
    a->acc += dt;
    if (getenv("WOODY_AMBLOG") && !a->logged) {
        float lo[3], hi[3]; amb_box(a->in, ni, lo, hi); a->logged = 1;
        static const float dl[3] = { 0.05f, 0.05f, -0.8f }; Vec3 d = m_dir(M, dl);
        printf("ambient: inst %u mode 2 force %d interval %.2f box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f fall %.2f %.2f %.2f world %.0f %.0f %.0f\n",
               a->in->index, a->force, a->interval, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], d.x, d.y, d.z, M->m[12], M->m[13], M->m[14]);
    }
    while (a->acc > a->interval) {
        a->acc -= a->interval;
        static const float dl[3] = { 0.05f, 0.05f, -0.8f };                     /* +0x12c..0x134 */
        Vec3 d = m_dir(M, dl); float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (l > 0) { d.x /= l; d.y /= l; d.z /= l; }
        const float *o = M->m; float len = sqrtf(o[8] * o[8] + o[9] * o[9] + o[10] * o[10]) * a->zr;   /* |row 2| * height */
        for (int c = 0; c < 25; c++) {
            Vec3 w = m_point(M, a->grid[(a->rot + 16 * c) % 400]);
            AmbFx *f = fx_new(K_DROP, ai);                                       /* 0x47e230 */
            if (f) { f->life = 1.0f; f->pos = w; f->dir = d; f->fallen = 0; f->len = len; f->speed = 4000.0f; }   /* 0x457a0000 */
        }
        a->rot = (a->rot + (int)(rnd01() * 15.0f + 1.0f)) & 15;                 /* 0x4a9864 = 15 */
    }
}

void ambient_update(float dt)
{
    g_frame++;
    for (int i = 0; i < g_namb; i++) {                                           /* think 0x472560 (vt[3]): only for the instances of the drawn sectors */
        Amb *a = &g_amb[i];
        if (a->in->type != 90 || (!a->in->visible && !getenv("WOODY_AMBON")) || !game_enemy_thinks(a->in)) continue;   /* hidden = out of the world (0x407850) */
        a->stamp = g_frame;                                                      /* +0x104 */
        if (a->mode == 1) amb_think_motes(a, i);
        else if (a->mode == 2) amb_think_rain(a, i, dt);
    }
    for (int i = 0; i < g_nfx; i++) {                                            /* the pool driver 0x470c70: new records run this frame too */
        AmbFx *f = &g_fx[i]; Amb *a = &g_amb[f->owner]; int dead = 0;
        if (f->kind == K_MOTE) {                                                 /* 0x47dca0 */
            if (a->stamp != g_frame) dead = 1;                                   /* the owner did not think: gone at once */
            else if ((f->age += dt) >= f->life) dead = 1;
            else { f->pos.x += dt * f->dir.x * f->speed; f->pos.y += dt * f->dir.y * f->speed; f->pos.z += dt * f->dir.z * f->speed; }
            if (dead) a->live--;
        } else if (f->kind == K_DROP) {                                          /* 0x47de10 */
            f->age += dt;
            float s = dt * f->speed * f->dir.y, nf = f->fallen - s;              /* +0x24 counts the height fallen */
            if (nf < f->len) {
                f->fallen = nf;
                f->pos.x += dt * f->speed * f->dir.x; f->pos.y += s; f->pos.z += dt * f->speed * f->dir.z;
                if (f->age > 5.0f) dead = 1;                                     /* port guard: a drop that never falls (dir.y >= 0) would live for ever */
            } else {                                                             /* 0x47df30: the rest of the step down to the bottom, a ripple there */
                float t = (f->fallen - f->len) / s * dt;
                f->pos.x += t * f->dir.x * f->speed; f->pos.y += t * f->dir.y * f->speed; f->pos.z += t * f->dir.z * f->speed;
                Vec3 p = f->pos; int ow = f->owner; dead = 1;
                AmbFx *r = fx_new(K_RIPPLE, ow);                                 /* 0x47e310 */
                if (r) { r->life = 0.2f; r->pos = p; f = &g_fx[i]; }
            }
        } else if (f->kind == K_RIPPLE) {                                        /* 0x47df80 */
            if ((f->age += dt) / f->life >= 1.0f) dead = 1;
        }
        if (dead) { g_fx[i] = g_fx[--g_nfx]; i--; }
    }
}

void ambient_draw(const float *eye)
{
    static const float grey[3] = { 0.4f, 0.4f, 0.4f }, white[3] = { 1, 1, 1 }, up[3] = { 0, 1, 0 };
    for (int i = 0; i < g_nfx; i++) {
        const AmbFx *f = &g_fx[i];
        if (f->kind == K_MOTE) {
            /* flags 7 (camera facing, own colour, rotation, additive), mode 0x12, size 25, image 29 (0x1001d), rotation
             * (int)(age * 250) in 1/512 turn; alpha 1 until u = 0.7, then down to 0 at u = 1 (0x4aa1d8, 0x4abd30 = 3.33) */
            float u = f->age / f->life, al = u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) * 3.33f;
            hud_world_spr(29, &f->pos.x, 25.0f, (int)lrintf(f->age * 250.0f), g_amb[f->owner].rgb, al, 7, NULL, 0);
        } else if (f->kind == K_DROP) {
            /* line 0x471a10(0xe00): from 250 behind (alpha 0) to the drop (alpha 0.5), grey 0.4, half width 4, image 57 */
            if (!eye) continue;
            float a[3] = { f->pos.x - f->dir.x * 250.0f, f->pos.y - f->dir.y * 250.0f, f->pos.z - f->dir.z * 250.0f };
            hud_world_streak(57, a, &f->pos.x, eye, 4.0f, grey, 0.0f, 0.5f);
        } else if (f->kind == K_RIPPLE) {
            /* flags 2 (plane with normal (0,1,0), own colour, additive), image 3 = byte-identical to 58 (hud slot 0x3a),
             * size u*30 + 5, alpha 0.5 - 0.5u */
            float u = f->age / f->life;
            hud_world_spr(0x3a, &f->pos.x, u * 30.0f + 5.0f, 0, white, 0.5f - 0.5f * u, 2, up, 0);
        }
    }
}
