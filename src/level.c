/* level.c - loaders for .gel / .tex / .ins and the skeletal pose evaluation (0x43a3a0). */
#include "level.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- reader */
typedef struct { const uint8_t *d; size_t pos, size; int err; } Rd;
static uint32_t ru32(Rd *r) { if (r->pos + 4 > r->size) { r->err = 1; r->pos = r->size; return 0; } uint32_t v; memcpy(&v, r->d + r->pos, 4); r->pos += 4; return v; }
static int32_t  ri32(Rd *r) { return (int32_t)ru32(r); }
static float    rf32(Rd *r) { uint32_t v = ru32(r); float f; memcpy(&f, &v, 4); return f; }
static Vec3     rvec3(Rd *r) { Vec3 v; v.x = rf32(r); v.y = rf32(r); v.z = rf32(r); return v; }
static uint32_t *ru32s(Rd *r, uint32_t n) { uint32_t *a = (uint32_t *)calloc(n ? n : 1, 4); for (uint32_t i = 0; i < n; i++) a[i] = ru32(r); return a; }
static const uint8_t *rraw(Rd *r, size_t n) { if (r->pos + n > r->size) { r->err = 1; return NULL; } const uint8_t *p = r->d + r->pos; r->pos += n; return p; }

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz + 16);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
    fclose(f); *size = (size_t)sz; return buf;
}

/* ---------------------------------------------------------------- .tex */
int tex_load(TexFile *t, const char *path)
{
    memset(t, 0, sizeof *t);
    t->data = read_file(path, &t->size); if (!t->data) return -1;
    Rd r = { t->data, 0, t->size, 0 };
    t->ngroups = ru32(&r); ru32(&r);           /* texture_count */
    t->groups = (TexGroup *)calloc(t->ngroups, sizeof(TexGroup));
    for (uint32_t g = 0; g < t->ngroups; g++) {
        TexGroup *tg = &t->groups[g];
        tg->width = ru32(&r); tg->height = ru32(&r); tg->flags = ru32(&r);
        tg->scroll_u = rf32(&r); tg->scroll_v = rf32(&r); tg->anim_duration = rf32(&r);
        tg->frame_count = ru32(&r); ru32(&r); ru32(&r);
        tg->frames = (uint16_t **)calloc(tg->frame_count, sizeof(uint16_t *));
        for (uint32_t f = 0; f < tg->frame_count; f++) tg->frames[f] = (uint16_t *)rraw(&r, (size_t)tg->width * tg->height * 2);
    }
    t->nmaterials = ru32(&r);
    t->materials = (Material *)calloc(t->nmaterials, sizeof(Material));
    for (uint32_t i = 0; i < t->nmaterials; i++) { t->materials[i].group = ru32(&r); for (int k = 0; k < 12; k++) t->materials[i].m[k] = rf32(&r); }
    if (r.err || r.pos != r.size) { fprintf(stderr, "%s: parse error (pos %zu of %zu)\n", path, r.pos, r.size); return -1; }
    return 0;
}
void tex_free(TexFile *t) { for (uint32_t g = 0; g < t->ngroups; g++) free(t->groups[g].frames); free(t->groups); free(t->materials); free(t->data); memset(t, 0, sizeof *t); }

/* ---------------------------------------------------------------- .gel */
int gel_load(GelFile *g, const char *path)
{
    memset(g, 0, sizeof *g);
    g->data = read_file(path, &g->size); if (!g->data) return -1;
    Rd r = { g->data, 0, g->size, 0 };
    g->npolys = ru32(&r); ru32(&r);            /* total indices */
    g->polys = (GelPoly *)calloc(g->npolys, sizeof(GelPoly));
    for (uint32_t i = 0; i < g->npolys; i++) {
        GelPoly *p = &g->polys[i];
        p->nverts = ru32(&r); p->material = ru32(&r);
        for (int k = 0; k < 4; k++) p->plane[k] = rf32(&r);
        p->indices = ru32s(&r, p->nverts);
    }
    /* portals */
    uint32_t nport = ru32(&r); ru32(&r);
    for (uint32_t i = 0; i < nport; i++) { uint32_t n = ru32(&r); r.pos += 16 + 4 * (size_t)n; }
    /* groups */
    uint32_t ngroups = ru32(&r); r.pos += 4 * (size_t)ngroups;
    for (uint32_t i = 0; i < ngroups; i++) { uint32_t k = ru32(&r); r.pos += 8 * (size_t)k; }
    g->nverts = ru32(&r);
    g->verts = (GelVert *)calloc(g->nverts, sizeof(GelVert));
    for (uint32_t i = 0; i < g->nverts; i++) { g->verts[i].x = rf32(&r); g->verts[i].y = rf32(&r); g->verts[i].z = rf32(&r); g->verts[i].colour = ru32(&r); }
    /* cells / kd-tree / sectors are not needed for rendering yet */
    if (r.err) { fprintf(stderr, "%s: parse error\n", path); return -1; }
    float *b = g->bbox; b[0] = b[2] = b[4] = 1e30f; b[1] = b[3] = b[5] = -1e30f;
    for (uint32_t i = 0; i < g->nverts; i++) {
        GelVert *v = &g->verts[i];
        if (v->x < b[0]) b[0] = v->x; if (v->x > b[1]) b[1] = v->x;
        if (v->y < b[2]) b[2] = v->y; if (v->y > b[3]) b[3] = v->y;
        if (v->z < b[4]) b[4] = v->z; if (v->z > b[5]) b[5] = v->z;
    }
    return 0;
}
void gel_free(GelFile *g) { for (uint32_t i = 0; i < g->npolys; i++) free(g->polys[i].indices); free(g->polys); free(g->verts); free(g->data); memset(g, 0, sizeof *g); }

/* ---------------------------------------------------------------- .ins */
static void read_trajectory(Rd *r, Trajectory *t)
{
    memset(t, 0, sizeof *t);
    t->npoints = ru32(r);
    if (!t->npoints) return;
    t->points = (Vec3 *)calloc(t->npoints, sizeof(Vec3));
    for (uint32_t i = 0; i < t->npoints; i++) { ru32(r); t->points[i] = rvec3(r); }
    t->closed = ru32(r);
}

static TrackRef *read_refs(Rd *r, uint32_t n) { TrackRef *t = (TrackRef *)calloc(n ? n : 1, sizeof(TrackRef)); for (uint32_t i = 0; i < n; i++) { t[i].off = ru32(r); t[i].cnt = ru32(r); } return t; }

static int read_node(Rd *r, InsNode *n, uint32_t nanims)
{
    memset(n, 0, sizeof *n);
    n->flags = ru32(r); n->kind = n->flags & 0xff; n->type_code = (n->flags >> 8) & 0xff; n->sub_index = n->flags >> 16;
    if (n->flags & 0x20) n->marker_value = rf32(r);
    else if (n->flags & 0x40) { n->light_intensity = rf32(r); n->light_colour = ru32(r); ru32(r); }
    else if (n->flags & 0x10) { n->helper_a = rf32(r); n->helper_b = rf32(r); n->helper_mode = ru32(r); }
    else n->npolys = ru32(r);
    n->npoints = ru32(r);
    n->pivot = rvec3(r);
    n->a = ru32(r); n->b = ru32(r); n->c = ru32(r);
    if (n->a + n->b + n->c) {
        size_t bytes = ((size_t)n->a + n->b + n->c / 4) * 4;
        n->pool = (uint8_t *)rraw(r, bytes);
        if (n->a) n->pos_refs = read_refs(r, nanims);
        if (n->b) n->rot_refs = read_refs(r, nanims);
        if (n->c) n->event_refs = read_refs(r, nanims);
    }
    n->first_child = ri32(r); n->next_sibling = ri32(r); n->parent = -1;
    if (n->first_child > 0) n->first_child -= 1;       /* file: 1-based, -1 none */
    if (n->next_sibling > 0) n->next_sibling -= 1;
    return r->err;
}

static int read_model(Rd *r, Model *m)
{
    memset(m, 0, sizeof *m);
    uint32_t B = ru32(r); m->nanims = ru32(r); ru32(r); ru32(r);
    m->nnodes = B ? B - 1 : 0;
    m->anims = (InsAnim *)calloc(m->nanims ? m->nanims : 1, sizeof(InsAnim));
    for (uint32_t i = 0; i < m->nanims; i++) { m->anims[i].nframes = ru32(r); m->anims[i].duration_4096 = ru32(r); m->anims[i].duration_s = m->anims[i].duration_4096 / 4096.0f; }
    m->first_top_node = ru32(r); m->bbox_node = ru32(r);
    uint32_t n; uint32_t *tmp;
    n = ru32(r); tmp = ru32s(r, n); free(tmp);                                 /* marker nodes */
    n = ru32(r); tmp = ru32s(r, n); free(tmp);                                 /* light nodes */
    m->nvolume_nodes = ru32(r); m->volume_nodes = ru32s(r, m->nvolume_nodes);
    uint32_t nhull = ru32(r); uint32_t *hull = ru32s(r, nhull);
    n = ru32(r); tmp = ru32s(r, n); free(tmp);                                 /* helper nodes */
    uint32_t npress = ru32(r); m->ncollision_ids = ru32(r); uint32_t *press = ru32s(r, npress);
    m->nmesh_nodes = ru32(r); m->mesh_nodes = ru32s(r, m->nmesh_nodes);

    m->nodes = (InsNode *)calloc(m->nnodes ? m->nnodes : 1, sizeof(InsNode));
    for (uint32_t i = 0; i < m->nnodes; i++) if (read_node(r, &m->nodes[i], m->nanims)) return -1;

    uint32_t nextra = nhull + ((nhull || m->nmesh_nodes || npress) ? 1 : 0);
    for (uint32_t i = 0; i < nextra; i++) { ru32(r); uint32_t cnt = ru32(r); r->pos += 4 * (size_t)cnt; }
    free(hull); free(press);

    m->npoints = ru32(r);
    m->points = (InsPoint *)calloc(m->npoints ? m->npoints : 1, sizeof(InsPoint));
    for (uint32_t i = 0; i < m->npoints; i++) m->points[i].pos = rvec3(r);
    for (uint32_t i = 0; i < m->npoints; i++) m->points[i].normal = rvec3(r);
    for (uint32_t i = 0; i < m->npoints; i++) m->points[i].colour = rvec3(r);

    ru32(r); ru32(r);                                                          /* poly / index totals */
    uint32_t base = 0;
    for (uint32_t j = 0; j < m->nnodes; j++) {
        InsNode *nd = &m->nodes[j];
        nd->point_base = base;
        if (!(nd->flags & 0x70)) {
            nd->polys = (InsPoly *)calloc(nd->npolys ? nd->npolys : 1, sizeof(InsPoly));
            for (uint32_t k = 0; k < nd->npolys; k++) {
                InsPoly *p = &nd->polys[k];
                p->material = ru32(r) & 0xffff; p->flags = ru32(r); p->nverts = ru32(r);
                p->indices = ru32s(r, p->nverts);
            }
        }
        base += nd->npoints;
        for (int32_t c = nd->first_child; c >= 0; c = m->nodes[c].next_sibling) {
            if ((uint32_t)c >= m->nnodes) return -1;
            m->nodes[c].parent = (int32_t)j;
        }
    }
    if (base != m->npoints) { fprintf(stderr, "point count mismatch\n"); return -1; }

    m->ntris = ru32(r);
    m->tris = (InsTri *)calloc(m->ntris ? m->ntris : 1, sizeof(InsTri));
    for (uint32_t i = 0; i < m->ntris; i++) { InsTri *t = &m->tris[i]; t->i2 = ru32(r); t->i1 = ru32(r); t->i0 = ru32(r); t->material = ru32(r) & 0xffff; }

    m->ninstances = ru32(r);
    m->instances = (Instance *)calloc(m->ninstances ? m->ninstances : 1, sizeof(Instance));
    uint32_t nids = m->nvolume_nodes + m->ncollision_ids;
    for (uint32_t i = 0; i < m->ninstances; i++) {
        Instance *in = &m->instances[i];
        in->model = m;
        in->unk0 = ru32(r);
        read_trajectory(r, &in->traj);
        in->position = rvec3(r);
        in->quat.x = rf32(r); in->quat.y = rf32(r); in->quat.z = rf32(r); in->quat.w = rf32(r);
        in->scale = rvec3(r);
        in->id = ru32(r); in->index = in->id & 0xffffff;
        in->nids = nids; in->ids = ru32s(r, nids);
        in->visible = 1; in->anim = 0; in->anim_speed = 1.0f;
        in->node_world = (Mat4 *)calloc(m->nnodes ? m->nnodes : 1, sizeof(Mat4));
        mat4_from_trs(&in->world, in->position, in->quat, in->scale);
    }
    return r->err;
}

int ins_load(InsFile *f, const char *path)
{
    memset(f, 0, sizeof *f);
    f->data = read_file(path, &f->size); if (!f->data) return -1;
    Rd r = { f->data, 0, f->size, 0 };
    f->nslots = ru32(&r);
    f->nmodels = ru32(&r);
    f->models = (Model *)calloc(f->nmodels ? f->nmodels : 1, sizeof(Model));
    for (uint32_t i = 0; i < f->nmodels; i++) if (read_model(&r, &f->models[i])) { fprintf(stderr, "%s: model %u parse error at %zu\n", path, i, r.pos); return -1; }
    f->ncameras = ru32(&r);
    f->cameras = (Camera *)calloc(f->ncameras ? f->ncameras : 1, sizeof(Camera));
    for (uint32_t i = 0; i < f->ncameras; i++) { Camera *c = &f->cameras[i]; c->position = rvec3(&r); c->id = ru32(&r); c->index = c->id & 0xffffff; read_trajectory(&r, &c->traj); }
    ru32(&r);
    if (r.err || r.pos != r.size) { fprintf(stderr, "%s: parse error (pos %zu of %zu)\n", path, r.pos, r.size); return -1; }
    f->slots = (Instance **)calloc(f->nslots + 16, sizeof(Instance *));
    f->cam_slots = (Camera **)calloc(f->nslots + 16, sizeof(Camera *));
    for (uint32_t i = 0; i < f->nmodels; i++)
        for (uint32_t k = 0; k < f->models[i].ninstances; k++) { Instance *in = &f->models[i].instances[k]; if (in->index < f->nslots + 16) f->slots[in->index] = in; }
    for (uint32_t i = 0; i < f->ncameras; i++) if (f->cameras[i].index < f->nslots + 16) f->cam_slots[f->cameras[i].index] = &f->cameras[i];
    return 0;
}

void ins_free(InsFile *f)
{
    for (uint32_t i = 0; i < f->nmodels; i++) {
        Model *m = &f->models[i];
        for (uint32_t j = 0; j < m->nnodes; j++) { InsNode *n = &m->nodes[j]; for (uint32_t k = 0; k < n->npolys && n->polys; k++) free(n->polys[k].indices); free(n->polys); free(n->pos_refs); free(n->rot_refs); free(n->event_refs); }
        for (uint32_t j = 0; j < m->ninstances; j++) { free(m->instances[j].ids); free(m->instances[j].traj.points); free(m->instances[j].node_world); }
        free(m->nodes); free(m->anims); free(m->points); free(m->tris); free(m->instances); free(m->volume_nodes); free(m->mesh_nodes);
    }
    for (uint32_t i = 0; i < f->ncameras; i++) free(f->cameras[i].traj.points);
    free(f->models); free(f->cameras); free(f->slots); free(f->cam_slots); free(f->data); memset(f, 0, sizeof *f);
}

/* ---------------------------------------------------------------- math */
void mat4_identity(Mat4 *m) { memset(m->m, 0, sizeof m->m); m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f; }
void mat4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b)
{
    Mat4 r;
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a->m[k * 4 + rr] * b->m[c * 4 + k];
        r.m[c * 4 + rr] = s;
    }
    *out = r;
}
void mat4_from_trs(Mat4 *m, Vec3 t, Quat q, Vec3 s)
{
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (n == 0) n = 1;   /* instance quaternions have norm 2 */
    float x = q.x / n, y = q.y / n, z = q.z / n, w = q.w / n;
    float *o = m->m;
    o[0] = (1 - 2 * (y * y + z * z)) * s.x; o[1] = (2 * (x * y + z * w)) * s.x; o[2] = (2 * (x * z - y * w)) * s.x; o[3] = 0;
    o[4] = (2 * (x * y - z * w)) * s.y; o[5] = (1 - 2 * (x * x + z * z)) * s.y; o[6] = (2 * (y * z + x * w)) * s.y; o[7] = 0;
    o[8] = (2 * (x * z + y * w)) * s.z; o[9] = (2 * (y * z - x * w)) * s.z; o[10] = (1 - 2 * (x * x + y * y)) * s.z; o[11] = 0;
    o[12] = t.x; o[13] = t.y; o[14] = t.z; o[15] = 1;
}
Vec3 mat4_apply(const Mat4 *m, Vec3 v)
{
    const float *o = m->m; Vec3 r;
    r.x = o[0] * v.x + o[4] * v.y + o[8] * v.z + o[12];
    r.y = o[1] * v.x + o[5] * v.y + o[9] * v.z + o[13];
    r.z = o[2] * v.x + o[6] * v.y + o[10] * v.z + o[14];
    return r;
}
void rgb565_to_rgba(const uint16_t *src, uint8_t *dst, uint32_t n, int colour_key)
{
    for (uint32_t i = 0; i < n; i++) {
        uint16_t v = src[i];
        uint8_t r = (uint8_t)(((v >> 11) & 31) << 3), g = (uint8_t)(((v >> 5) & 63) << 2), b = (uint8_t)((v & 31) << 3);
        r |= r >> 5; g |= g >> 6; b |= b >> 5;
        dst[4 * i] = r; dst[4 * i + 1] = g; dst[4 * i + 2] = b;
        dst[4 * i + 3] = (colour_key && (r & 0xF0) == 0xF0 && (b & 0xF0) == 0xF0 && (g & 0xF0) == 0) ? 0 : 255;
    }
}
void argb1555_to_rgb(uint32_t v, float rgb[3])
{
    rgb[0] = ((v >> 10) & 31) / 31.0f; rgb[1] = ((v >> 5) & 31) / 31.0f; rgb[2] = (v & 31) / 31.0f;
}

/* ---------------------------------------------------------------- .lit (loader 0x40ac30) */
int lit_load(LitFile *l, const char *path)
{
    memset(l, 0, sizeof *l);
    l->data = read_file(path, &l->size); if (!l->data) return -1;
    Rd r = { l->data, 0, l->size, 0 };
    if (ru32(&r) != 0x20010822) { free(l->data); memset(l, 0, sizeof *l); return -1; }
    l->nlights = ru32(&r); l->lights = (LitLight *)calloc(l->nlights ? l->nlights : 1, sizeof(LitLight));
    for (uint32_t i = 0; i < l->nlights && !r.err; i++) {
        LitLight *L = &l->lights[i];
        ru32(&r); ru32(&r); L->pos = rvec3(&r); for (int k = 0; k < 3; k++) L->colour[k] = rf32(&r); L->range = rf32(&r);
        L->na = ru32(&r); L->a = ru32s(&r, L->na);
        L->nb = ru32(&r); L->b = ru32s(&r, L->nb);
        L->nc = ru32(&r); ru32(&r); L->c = (LitPoly *)calloc(L->nc ? L->nc : 1, sizeof(LitPoly));
        for (uint32_t k = 0; k < L->nc && !r.err; k++) {
            LitPoly *p = &L->c[k]; p->n = ru32(&r); for (int q = 0; q < 4; q++) p->plane[q] = rf32(&r); ru32(&r);
            if (p->n > 4096) { r.err = 1; p->n = 0; break; }
            p->indices = (int32_t *)ru32s(&r, p->n);
        }
        uint32_t nd = ru32(&r); r.pos += 16 * (size_t)nd;
        L->nbsp = ru32(&r); L->bsp = ru32s(&r, 3 * L->nbsp);
        L->nplanes = ru32(&r); L->planes = (float *)ru32s(&r, 4 * L->nplanes);
    }
    l->nextra = ru32(&r); l->extra = (Vec3 *)calloc(l->nextra ? l->nextra : 1, sizeof(Vec3));
    for (uint32_t i = 0; i < l->nextra && !r.err; i++) { l->extra[i] = rvec3(&r); ru32(&r); }
    if (r.err) { fprintf(stderr, "%s: truncated\n", path); l->nlights = 0; return -1; }
    return 0;
}
int lit_point_lit(const LitLight *l, const GelFile *g, Vec3 p)
{
    if (!l->nbsp) return 1;
    uint32_t i = 0;
    for (int guard = 0; guard < 4096; guard++) {
        if (i >= l->nbsp) return 1;
        const uint32_t *nd = &l->bsp[3 * i]; if (nd[0] >= l->nplanes) return 1;
        const float *pl = &l->planes[4 * nd[0]];
        uint32_t v = (pl[0] * p.x + pl[1] * p.y + pl[2] * p.z + pl[3]) > 0.0f ? nd[1] : nd[2];
        if ((v & 0xF) == 0) { i = v >> 4; continue; }
        if ((v & 0xF) != 1) return 1;
        uint32_t f = v >> 4; if (f >= g->npolys) return 1;
        const float *fp = g->polys[f].plane;
        return fp[0] * p.x + fp[1] * p.y + fp[2] * p.z + fp[3] > 0.0f;
    }
    return 1;
}

/* ---------------------------------------------------------------- pose evaluation (0x43a3a0) */
static int track_pos(const InsNode *n, int anim, float t, Vec3 *out)
{
    if (!n->pos_refs || !n->pos_refs[anim].cnt) return 0;
    const float *f = (const float *)(n->pool + (size_t)n->pos_refs[anim].off * 4); uint32_t cnt = n->pos_refs[anim].cnt;
    if (t <= f[0] || cnt == 1) { out->x = f[1]; out->y = f[2]; out->z = f[3]; return 1; }
    const float *last = f + 4 * (cnt - 1);
    if (t >= last[0]) { out->x = last[1]; out->y = last[2]; out->z = last[3]; return 1; }
    uint32_t i = 1; while (i < cnt && f[4 * i] < t) i++;
    const float *a = f + 4 * (i - 1), *b = f + 4 * i; float u = (b[0] > a[0]) ? (t - a[0]) / (b[0] - a[0]) : 0;
    out->x = a[1] + (b[1] - a[1]) * u; out->y = a[2] + (b[2] - a[2]) * u; out->z = a[3] + (b[3] - a[3]) * u; return 1;
}
static int track_rot(const InsNode *n, int anim, float t, Quat *out)
{
    if (!n->rot_refs || !n->rot_refs[anim].cnt || n->kind == 0x80) return 0;
    const float *f = (const float *)(n->pool + ((size_t)n->a + n->rot_refs[anim].off) * 4); uint32_t cnt = n->rot_refs[anim].cnt;
    const float *a, *b; float u;
    if (t <= f[0] || cnt == 1) { a = b = f; u = 0; }
    else if (t >= f[5 * (cnt - 1)]) { a = b = f + 5 * (cnt - 1); u = 0; }
    else { uint32_t i = 1; while (i < cnt && f[5 * i] < t) i++; a = f + 5 * (i - 1); b = f + 5 * i; u = (b[0] > a[0]) ? (t - a[0]) / (b[0] - a[0]) : 0; }
    float dot = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]; float sgn = dot < 0 ? -1.0f : 1.0f;
    out->x = a[1] + (sgn * b[1] - a[1]) * u; out->y = a[2] + (sgn * b[2] - a[2]) * u; out->z = a[3] + (sgn * b[3] - a[3]) * u; out->w = a[4] + (sgn * b[4] - a[4]) * u;
    return 1;
}

static void pose_rec(Instance *inst, int32_t node, const Mat4 *parent, int anim, float tf)
{
    Model *m = inst->model;
    for (int32_t i = node; i >= 0; i = m->nodes[i].next_sibling) {
        InsNode *n = &m->nodes[i];
        Mat4 local; Vec3 p = {0, 0, 0}; Quat q = {0, 0, 0, 1}; Vec3 one = {1, 1, 1};
        int hp = track_pos(n, anim, tf, &p), hr = track_rot(n, anim, tf, &q);
        if (hp || hr) { q.x = -q.x; q.y = -q.y; q.z = -q.z;      /* 0x440370 conjugates; the .ins loader pre-negates instance quaternions (0x428758) but not track keys */
                        mat4_from_trs(&local, p, q, one); mat4_mul(&inst->node_world[i], parent, &local); }
        else inst->node_world[i] = *parent;                   /* 0x43a50a: no tracks -> copy parent */
        if (n->kind != 0x80 && n->first_child >= 0) pose_rec(inst, n->first_child, &inst->node_world[i], anim, tf);
    }
}

void ins_pose(Instance *inst, int anim, float t)
{
    Model *m = inst->model;
    if (!m->nnodes) return;
    float tf = 0;
    if (m->nanims && anim >= 0 && (uint32_t)anim < m->nanims) {
        InsAnim *a = &m->anims[anim];
        float dur = a->duration_s > 0 ? a->duration_s : 1.0f;
        float phase = fmodf(t / dur, 1.0f); if (phase < 0) phase += 1.0f;
        tf = phase * (float)a->nframes;
    } else anim = 0;
    int32_t top = m->first_top_node ? (int32_t)m->first_top_node - 1 : 0;
    if ((uint32_t)top >= m->nnodes) top = 0;
    /* nodes not reached from the top chain keep the instance matrix */
    for (uint32_t i = 0; i < m->nnodes; i++) inst->node_world[i] = inst->world;
    pose_rec(inst, top, &inst->world, anim, tf);
}

int ins_point_owner(const Model *m, uint32_t pi)
{
    for (uint32_t i = 0; i < m->nnodes; i++) if (pi >= m->nodes[i].point_base && pi < m->nodes[i].point_base + m->nodes[i].npoints) return (int)i;
    return -1;
}
Vec3 ins_point_world(const Instance *inst, uint32_t pi)
{
    const Model *m = inst->model; int o = ins_point_owner(m, pi);
    Vec3 p = m->points[pi].pos;
    if (o < 0) return mat4_apply(&inst->world, p);
    Vec3 piv = m->nodes[o].pivot; p.x -= piv.x; p.y -= piv.y; p.z -= piv.z;
    return mat4_apply(&inst->node_world[o], p);
}
