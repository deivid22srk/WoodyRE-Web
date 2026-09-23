/* player.c - player controller ported from the decompiled Perso class (see player.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "player.h"
#include "audio.h"
#include "enemy.h"

/* ---- decompiled Perso parameters (docs/PERSO_FRAME.md 2.3 / 2.6, table 0x4b5f14, Woody column) ---- */
#define P_WALK_SPEED   600.0f     /* P+0x1c: RampA max speed, units/s */
#define P_ACC_TIME     0.25f      /* P+0x2c: RampA acceleration time, v = (t/T)^2 * target */
#define P_DEC_TIME     0.1f       /* P+0x30: RampA deceleration time, v = v0 * (1 - (t/T)^2) */
#define P_TURN_BLEND   0.25f      /* 0x4a9ca0: facing = slerp(old, wanted, |stick| * 0.25) per frame (0x45a320) */
#define P_REF_FPS      60.0f      /* the blend is per frame in the original; normalised to this rate here */
/* Jumper J = Perso+0x334 (docs/PERSO_JUMP.md 1): rise and fall are time parabolas, there is no gravity constant */
#define J_HEIGHT       380.0f     /* P+0x64: jump height H */
#define J_P68          650.0f     /* P+0x68: reference distance; x0.75 rising (0x4aabb4), x1.25 falling (0x4ab798) */
#define J_V            600.0f     /* P+0x6c */
#define J_TERMINAL     2000.0f    /* P+0x70: terminal fall speed */
#define J_REARM_H      100.0f     /* P+0x84: jump re-arms when the button is up and height above ground <= this */
#define J_COYOTE       0.15f      /* 0x4aa1c8 */
#define J_SHORT_HOP_T  -0.2f      /* 0x4aa430 */
#define J_APEX_T       -0.15f     /* 0x4ab7a0: state 1 -> 7 (attack window opens) */
#define J_HARD_FALL    1500.0f    /* P+0x7c */
#define P_SLIDE_SPEED  600.0f     /* P+0x40: RampB max, sliding down slopes with n.y < 0.71 (0x45aa60) */
#define P_SLIDE_TIME   0.25f      /* P+0x44 / P+0x48: RampB acceleration / deceleration time */
#define P_SLIDE_NY     0.71f      /* 0x4ab2d8 */
/* collision (docs/PERSO_MOVE.md 6) */
#define P_STEP         40.0f      /* 0x437180 step argument: clinging distance, wall test skips the lowest step+1 */
#define P_SUBSTEP      10.0f      /* 0x437180 substep length */
#define P_BODY_H       193.0f     /* P+0x0c: body height (61 when ducking) */
#define P_PROBE_Y      43.0f      /* P+0x00: ground probe / collision centre above the feet */
#define P_RADIUS       69.0f      /* P+0x04: horizontal collision radius (0x434820 in 0x4624f0) */
#define P_VOL_PROBE_Y  71.0f      /* volume test point above the feet: 0x462760(perso, 71.0), docs/EVENTS.md 2.1 */
/* follow camera (docs/CAMERA.md 3) */
#define CAM_DIST_MIN   300.0f     /* xz distance to T is kept inside 300..400 */
#define CAM_DIST_MAX   400.0f
#define CAM_HEIGHT     180.0f     /* camera y approaches T.y + 180 with 6*dt */
#define CAM_TARGET_Y   120.0f     /* position target T = player + (0,120,0) */
#define CAM_LOOK_Y     140.0f     /* look target = player + (0,140,0) */
#define CAM_RADIUS     40.0f
#define CAM_FOV_Y      83.97f     /* tan(hfov/2) = zoom 1.2 at 4:3 (0x41f690 / 0x4379a0) */

static float vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 vsub(Vec3 a, Vec3 b) { Vec3 r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
static Vec3 vcross(Vec3 a, Vec3 b) { Vec3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; return r; }

/* point-in-polygon test in the plane's dominant projection */
static int poly_contains(const GelFile *g, const GelPoly *p, Vec3 q)
{
    float ax = fabsf(p->plane[0]), ay = fabsf(p->plane[1]), az = fabsf(p->plane[2]);
    int u = 0, v = 2;                                   /* project away the dominant axis */
    if (ay >= ax && ay >= az) { u = 0; v = 2; } else if (ax >= az) { u = 1; v = 2; } else { u = 0; v = 1; }
    const float *qq = &q.x; int sign = 0;
    for (uint32_t i = 0; i < p->nverts; i++) {
        const GelVert *a = &g->verts[p->indices[i]], *b = &g->verts[p->indices[(i + 1) % p->nverts]];
        const float *pa = &a->x, *pb = &b->x;
        float cr = (pb[u] - pa[u]) * (qq[v] - pa[v]) - (pb[v] - pa[v]) * (qq[u] - pa[u]);
        int s = cr > 0 ? 1 : (cr < 0 ? -1 : 0);
        if (!s) continue;
        if (!sign) sign = s; else if (s != sign) return 0;
    }
    return 1;
}

/* The query volume of one of the tests below, clamped to the level so an unbounded drop does not walk the
 * whole tree. Only the polygons of the kd leaves it meets can answer the test (docs/FORMAT_GEL.md 5). */
static void query_box(const GelFile *g, Vec3 c, float rxz, float ylo, float yhi, float box[6])
{
    if (ylo < g->bbox[2] - 1.0f) ylo = g->bbox[2] - 1.0f;
    if (yhi > g->bbox[3] + 1.0f) yhi = g->bbox[3] + 1.0f;
    if (yhi < ylo) yhi = ylo;
    box[0] = c.x - rxz; box[1] = c.x + rxz; box[2] = ylo; box[3] = yhi; box[4] = c.z - rxz; box[5] = c.z + rxz;
}

float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found)
{
    float best = -1e30f, box[6]; *found = 0;
    query_box(g, p, 1.0f, p.y - max_drop, p.y + step_up, box);
    GelPolySet ps = gel_polys_in_box(g, box);
    for (uint32_t k = 0; k < ps.n; k++) {
        uint32_t i = ps.polys[k];
        const GelPoly *pl = &g->polys[i];
        if (pl->plane[1] < 0.5f || pl->nverts < 3) continue;          /* walkable: normal mostly up */
        float y = -(pl->plane[0] * p.x + pl->plane[2] * p.z + pl->plane[3]) / pl->plane[1];
        if (y > p.y + step_up || y < p.y - max_drop || y <= best) continue;
        Vec3 q = { p.x, y, p.z };
        if (poly_contains(g, pl, q)) { best = y; *found = 1; }
    }
    return best;
}

/* floor provided by the press nodes (kind 1) and collision hulls (kind 4) of visible instances:
 * highest upward-facing polygon under p within [p.y - max_drop, p.y + step_up]. The original's
 * GetHeight (0x435650) reports press nodes as hit type 2 and hulls as type 3 (docs/EVENTS.md 3.1);
 * hit_inst/hit_node return what was hit so the caller can raise world_collision events. */
static int point_in_tri_xz(Vec3 a, Vec3 b, Vec3 c, Vec3 q)
{
    float d1 = (b.x - a.x) * (q.z - a.z) - (b.z - a.z) * (q.x - a.x);
    float d2 = (c.x - b.x) * (q.z - b.z) - (c.z - b.z) * (q.x - b.x);
    float d3 = (a.x - c.x) * (q.z - c.z) - (a.z - c.z) * (q.x - c.x);
    return (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
}
/* Horizontal distance from p to an instance, measured from its animated root node (as the renderer's cull does), not
 * from the .ins origin: an animation can carry the whole model far away - the W1B shuttle platforms (model 42) travel
 * 4400 units from their origin and back, and an origin-based reject threw them out at the far end (issue #27). */
static float inst_dist2_xz(const Instance *in, Vec3 p)
{
    const float *w = in->node_world ? in->node_world[0].m : in->world.m;
    float dx = w[12] - p.x, dz = w[14] - p.z; return dx * dx + dz * dz;
}
static Vec3 g_ground_n = { 0, 1, 0 };   /* normal of the last world_ground() hit ([0x4b3108..10]) */
static int32_t g_ground_mat = -1;       /* material of that hit when it is a world polygon ([0x53a554] == 1, poly+8), else -1 */
static Vec3 g_ins_n;
static float ins_floor_below(const InsFile *ins, Vec3 p, float step_up, float max_drop, int *found, const Instance *skip,
                             const Instance **hit_inst, const InsNode **hit_node)
{
    float best = -1e30f; *found = 0; *hit_inst = NULL; *hit_node = NULL;
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) {
        const Model *m = &ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            const Instance *in = &m->instances[k]; if (!in->visible || in->noncollide || in == skip) continue;
            if (inst_dist2_xz(in, p) > 4000.0f * 4000.0f) continue;       /* cheap reject: far away horizontally */
            uint32_t ncn; const uint32_t *cn = ins_collision_nodes(m, &ncn);
            for (uint32_t ci = 0; ci < ncn; ci++) {
                uint32_t ni = cn[ci]; const InsNode *n = &m->nodes[ni];
                float nb[6];                                              /* the node's own box: no floor in it, nothing to test */
                if (ins_node_world_box(in, ni, nb) && (p.x < nb[0] || p.x > nb[1] || p.z < nb[4] || p.z > nb[5] || nb[3] < p.y - max_drop || nb[2] > p.y + step_up)) continue;
                for (uint32_t f = 0; f < n->npolys; f++) {
                    const InsPoly *pl = &n->polys[f]; if (pl->nverts < 3) continue;
                    Vec3 a = ins_point_world(in, pl->indices[0]);
                    for (uint32_t t = 1; t + 1 < pl->nverts; t++) {
                        Vec3 b = ins_point_world(in, pl->indices[t]), c = ins_point_world(in, pl->indices[t + 1]);
                        Vec3 nrm = vcross(vsub(b, a), vsub(c, a)); float nl = sqrtf(vdot(nrm, nrm)); if (nl < 1e-6f) continue;
                        if (fabsf(nrm.y) / nl < 0.5f) continue;                   /* not a walkable face */
                        if (!point_in_tri_xz(a, b, c, p)) continue;
                        float y = a.y - (nrm.x * (p.x - a.x) + nrm.z * (p.z - a.z)) / nrm.y;
                        if (y > p.y + step_up || y < p.y - max_drop || y <= best) continue;
                        best = y; *found = 1; *hit_inst = in; *hit_node = n;
                        { float sg = nrm.y < 0 ? -1.0f / nl : 1.0f / nl; g_ins_n = (Vec3){ nrm.x * sg, nrm.y * sg, nrm.z * sg }; }
                    }
                }
            }
        }
    }
    return best;
}

/* Push-out contribution of one convex polygon (world-space vertices v[0..n), unit normal nrm on the outward side)
 * for a body of radius r centred at q: closest point of the polygon to q (plane point if the projection is inside,
 * else the nearest edge point), penetration r - distance, pushed along the horizontal direction away from it.
 * Accumulated like 0x408600: positive maximum and negative minimum per axis (acc = px, nx, pz, nz). */
static int point_in_poly3(const Vec3 *v, uint32_t n, Vec3 nrm, Vec3 q)
{
    int sign = 0;
    for (uint32_t i = 0; i < n; i++) {
        Vec3 e = vsub(v[(i + 1) % n], v[i]), w = vsub(q, v[i]); float c = vdot(vcross(e, w), nrm);
        int sg = c > 1e-3f ? 1 : (c < -1e-3f ? -1 : 0);
        if (!sg) continue;
        if (!sign) sign = sg; else if (sg != sign) return 0;
    }
    return 1;
}
static void poly_push_accum(const Vec3 *v, uint32_t n, Vec3 nrm, Vec3 q, float r, float acc[4])
{
    float d = vdot(nrm, vsub(q, v[0])); if (d < 0 || d >= r) return;        /* front side only */
    Vec3 cp = { q.x - nrm.x * d, q.y - nrm.y * d, q.z - nrm.z * d };
    if (!point_in_poly3(v, n, nrm, cp)) {
        float best = 1e30f; Vec3 bc = cp;
        for (uint32_t i = 0; i < n; i++) {
            Vec3 a = v[i], e = vsub(v[(i + 1) % n], a); float el = vdot(e, e), t = el > 1e-9f ? vdot(vsub(q, a), e) / el : 0;
            if (t < 0) t = 0; if (t > 1) t = 1;
            Vec3 c = { a.x + e.x * t, a.y + e.y * t, a.z + e.z * t }, w = vsub(q, c); float dd = vdot(w, w);
            if (dd < best) { best = dd; bc = c; }
        }
        cp = bc;
    }
    Vec3 w = vsub(q, cp); float dist = sqrtf(vdot(w, w)); if (dist >= r) return;
    float hx = w.x, hz = w.z, hl = sqrtf(hx * hx + hz * hz);
    if (hl < 1e-4f) { hx = nrm.x; hz = nrm.z; hl = sqrtf(hx * hx + hz * hz); if (hl < 1e-4f) return; }
    float pen = r - dist, vx = hx / hl * pen, vz = hz / hl * pen;
    if (vx > acc[0]) acc[0] = vx; if (vx < acc[1]) acc[1] = vx; if (vz > acc[2]) acc[2] = vz; if (vz < acc[3]) acc[3] = vz;
}

/* One push-out vector for the body cylinder (radius r, vertical band [lo, hi]) against the world, after 0x407000 /
 * 0x408600: front side only, penetration r - dist along the polygon normal in xz, accumulated per axis as a positive
 * maximum plus a negative minimum. Simplifications: no band clipping / edge-circle intersection and no conical bottom
 * (the contact point is the centre projected onto the plane), walkable polygons (n.y > 0.71) are left to the floor code,
 * instance hulls are not tested yet. */
static Vec3 gel_push(const GelFile *g, Vec3 c, float r, float lo, float hi)
{
    float acc[4] = { 0, 0, 0, 0 }, box[6]; Vec3 v[32];
    query_box(g, c, r, lo, hi, box);
    GelPolySet ps = gel_polys_in_box(g, box);
    for (uint32_t k = 0; k < ps.n; k++) {
        uint32_t i = ps.polys[k];
        const GelPoly *pl = &g->polys[i];
        if (pl->nverts < 3 || pl->nverts > 32 || pl->plane[1] > 0.71f) continue;
        float d0 = pl->plane[0] * c.x + pl->plane[1] * c.y + pl->plane[2] * c.z + pl->plane[3];
        if (d0 < -r - (hi - lo) || d0 > r + (hi - lo)) continue;               /* cheap reject */
        float ymin = 1e30f, ymax = -1e30f;
        for (uint32_t t = 0; t < pl->nverts; t++) { const GelVert *gv = &g->verts[pl->indices[t]]; v[t] = (Vec3){ gv->x, gv->y, gv->z }; if (gv->y < ymin) ymin = gv->y; if (gv->y > ymax) ymax = gv->y; }
        if (ymax < lo || ymin > hi) continue;
        Vec3 q = c; float a = lo > ymin ? lo : ymin, b = hi < ymax ? hi : ymax; if (q.y < a) q.y = a; if (q.y > b) q.y = b;
        poly_push_accum(v, pl->nverts, (Vec3){ pl->plane[0], pl->plane[1], pl->plane[2] }, q, r, acc);
    }
    Vec3 out = { acc[0] + acc[1], 0, acc[2] + acc[3] }; return out;
}

/* Same push-out against the collision hulls (kind 4 nodes) of visible instances, after inst->vt[8] = 0x433140.
 * Hull polygons are taken in world space; their outward side is the one facing away from the node's centroid. */
static Vec3 ins_push(const InsFile *ins, const Instance *skip, Vec3 c, float r, float lo, float hi)
{
    float acc[4] = { 0, 0, 0, 0 }; Vec3 v[16];
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) {
        const Model *m = &ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            const Instance *in = &m->instances[k]; if (!in->visible || in->noncollide || in == skip || !in->node_world) continue;
            if (inst_dist2_xz(in, c) > 3000.0f * 3000.0f) continue;
            uint32_t ncn; const uint32_t *cn = ins_collision_nodes(m, &ncn);
            for (uint32_t ci = 0; ci < ncn; ci++) {
                uint32_t ni = cn[ci]; const InsNode *nd = &m->nodes[ni]; if (nd->kind != 4 || !nd->npoints) continue;
                float nb[6];                                              /* the node's own box against the body cylinder */
                if (ins_node_world_box(in, ni, nb) && (nb[0] > c.x + r || nb[1] < c.x - r || nb[4] > c.z + r || nb[5] < c.z - r || nb[3] < lo || nb[2] > hi)) continue;
                Vec3 cen = { 0, 0, 0 };
                for (uint32_t t = 0; t < nd->npoints; t++) { Vec3 w = ins_point_world(in, nd->point_base + t); cen.x += w.x; cen.y += w.y; cen.z += w.z; }
                cen.x /= nd->npoints; cen.y /= nd->npoints; cen.z /= nd->npoints;
                for (uint32_t f = 0; f < nd->npolys; f++) {
                    const InsPoly *pl = &nd->polys[f]; if (pl->nverts < 3 || pl->nverts > 16) continue;
                    float ymin = 1e30f, ymax = -1e30f;
                    for (uint32_t t = 0; t < pl->nverts; t++) { v[t] = ins_point_world(in, pl->indices[t]); if (v[t].y < ymin) ymin = v[t].y; if (v[t].y > ymax) ymax = v[t].y; }
                    if (ymax < lo || ymin > hi) continue;
                    Vec3 nrm = vcross(vsub(v[1], v[0]), vsub(v[2], v[0])); float nl = sqrtf(vdot(nrm, nrm)); if (nl < 1e-6f) continue;
                    nrm.x /= nl; nrm.y /= nl; nrm.z /= nl;
                    if (vdot(nrm, vsub(cen, v[0])) > 0) { nrm.x = -nrm.x; nrm.y = -nrm.y; nrm.z = -nrm.z; }
                    if (nrm.y > 0.71f) continue;                               /* walkable: floor code */
                    Vec3 q = c; float a = lo > ymin ? lo : ymin, b = hi < ymax ? hi : ymax; if (q.y < a) q.y = a; if (q.y > b) q.y = b;
                    float before[4] = { acc[0], acc[1], acc[2], acc[3] }; poly_push_accum(v, pl->nverts, nrm, q, r, acc);
                    if (getenv("WOODY_PUSHLOG") && (before[0] != acc[0] || before[1] != acc[1] || before[2] != acc[2] || before[3] != acc[3])) printf("push: inst %u model %d node %u type %d fade %.2f at %.0f %.0f %.0f", in->index, (int)mi, ni, in->type, in->fade, c.x, c.y, c.z), puts("");
                }
            }
        }
    }
    Vec3 out = { acc[0] + acc[1], 0, acc[2] + acc[3] }; return out;
}

/* GetHeight 0x435650 -> 0x498520: nearest surface below the point: world polygons with n.y > 1e-5 (any slope) that
 * contain the point in xz, and the press / hull nodes of instances (hit_node != NULL then). */
static const Instance *g_ground_skip;      /* set by player_ground_query(): instance to ignore instead of the player */
static float world_ground(const Player *p, Vec3 pt, int *found, const Instance **hit_inst, const InsNode **hit_node)
{
    const GelFile *g = p->gel; float best = 1e30f, box[6]; int f1 = 0, f2; Vec3 gn = { 0, 1, 0 }; int32_t gm = -1;
    query_box(g, pt, 1.0f, g->bbox[2] - 1.0f, pt.y, box);            /* the column under the point */
    GelPolySet ps = gel_polys_in_box(g, box);
    for (uint32_t k = 0; k < ps.n; k++) {
        uint32_t i = ps.polys[k];
        const GelPoly *pl = &g->polys[i];
        if (pl->nverts < 3 || pl->plane[1] <= 1e-5f) continue;
        float dist = pl->plane[0] * pt.x + pl->plane[1] * pt.y + pl->plane[2] * pt.z + pl->plane[3];
        if (dist <= 0 || dist / pl->plane[1] >= best) continue;
        Vec3 q = { pt.x, pt.y - dist / pl->plane[1], pt.z };
        if (poly_contains(g, pl, q)) { best = dist / pl->plane[1]; f1 = 1; gn = (Vec3){ pl->plane[0], pl->plane[1], pl->plane[2] }; gm = (int32_t)pl->material; }
    }
    float y1 = pt.y - best;
    float y2 = ins_floor_below(p->ins, pt, 0.0f, 1e9f, &f2, g_ground_skip ? g_ground_skip : p->inst, hit_inst, hit_node);
    if (f2 && (!f1 || y2 > y1)) { *found = 1; g_ground_n = g_ins_n; g_ground_mat = -1; return y2; }
    *hit_inst = NULL; *hit_node = NULL; *found = f1; g_ground_n = f1 ? gn : (Vec3){ 0, 1, 0 }; g_ground_mat = f1 ? gm : -1; return f1 ? y1 : pt.y;
}

float player_ground_query(const Player *p, const Instance *skip, Vec3 pt, int *found)
{
    const Instance *hi; const InsNode *hn; Vec3 keep = g_ground_n;
    int32_t keep_mat = g_ground_mat;
    g_ground_skip = skip; float y = world_ground(p, pt, found, &hi, &hn); g_ground_skip = NULL; g_ground_n = keep; g_ground_mat = keep_mat;
    return y;
}

/* Landing ring (docs/PERSO_JUMP.md 5): while Woody is off the ground the spot he hangs over is marked on the
 * floor. Only the states in which he falls under his own weight get one: not while a script drives him, not on
 * the rocket, not on a wall and not while he is dying. The drop is unlimited, so a pit shows its bottom. */
int player_landing_ring(const Player *p, Vec3 *pos, Vec3 *normal)
{
    if (!p->inst || p->on_ground || p->dead_kind || p->script_act || p->ride || p->climb_sub || p->use_root) return 0;
    const Instance *hi; const InsNode *hn; int found = 0; Vec3 keep = g_ground_n; int32_t keep_mat = g_ground_mat;
    float y = world_ground(p, (Vec3){ p->pos.x, p->pos.y + P_PROBE_Y, p->pos.z }, &found, &hi, &hn);
    Vec3 n = g_ground_n; g_ground_n = keep; g_ground_mat = keep_mat;
    if (!found) return 0;
    *pos = (Vec3){ p->pos.x, y, p->pos.z };
    if (normal) *normal = n;
    return 1;
}

/* ground type Perso+0x308 (0x4628e0 -> 0x46295f): byte 3 of the flag word of the texture group behind the material of
 * the floor polygon ("m_nGroundType", docs/FORMAT_TEX_COL_VIS_LIT.md 1). 1 = slippery, 2 = dust/sand/snow. Only world
 * polygons have one: a floor made by an instance node, a polygon without a material (bit 15) or a missing .tex is 0. */
static int ground_type(const Player *p, int32_t mat)
{
    if (mat < 0 || (mat & 0x8000) || !p->tex) return 0;
    uint32_t mi = (uint32_t)mat; if (mi >= p->tex->nmaterials) return 0;
    uint32_t g = p->tex->materials[mi].group; if (g >= p->tex->ngroups) return 0;
    return (int)(p->tex->groups[g].flags >> 24);
}

/* ---- platform attachment (Perso+0x298, 0x436d80 store / 0x436d20 delta; docs/PERSO_MOVE.md 6.1) -------------
 * While standing on an instance node the contact point is remembered in node space; next frame the node's
 * movement (world(local) - previous world position) is added to the player's displacement. */
static int mat4_inv_apply(const Mat4 *m, Vec3 w, Vec3 *out)        /* out = M^-1 * w for an affine column-major M */
{
    const float *a = m->m;
    float x = w.x - a[12], y = w.y - a[13], z = w.z - a[14];
    float c00 = a[5] * a[10] - a[9] * a[6], c01 = a[9] * a[2] - a[1] * a[10], c02 = a[1] * a[6] - a[5] * a[2];
    float det = a[0] * c00 + a[4] * c01 + a[8] * c02; if (fabsf(det) < 1e-12f) return 0;
    float c10 = a[8] * a[6] - a[4] * a[10], c11 = a[0] * a[10] - a[8] * a[2], c12 = a[4] * a[2] - a[0] * a[6];
    float c20 = a[4] * a[9] - a[8] * a[5], c21 = a[8] * a[1] - a[0] * a[9], c22 = a[0] * a[5] - a[4] * a[1];
    out->x = (c00 * x + c10 * y + c20 * z) / det; out->y = (c01 * x + c11 * y + c21 * z) / det; out->z = (c02 * x + c12 * y + c22 * z) / det;
    return 1;
}
static Vec3 attach_delta(Player *p)
{
    Vec3 d = { 0, 0, 0 };
    if (!p->att_inst || !p->att_inst->node_world || !p->att_inst->visible) return d;
    Vec3 nw = mat4_apply(&p->att_inst->node_world[p->att_node], p->att_local);
    d = vsub(nw, p->att_world);
    if (vdot(d, d) > 200.0f * 200.0f) { d = (Vec3){ 0, 0, 0 }; p->att_inst = NULL; }   /* teleporting platform: let go */
    return d;
}
static void attach_store(Player *p, const Instance *inst, const InsNode *node, Vec3 contact)
{
    p->att_inst = NULL;
    if (!inst || !node || !inst->node_world) return;
    uint32_t ni = (uint32_t)(node - inst->model->nodes);
    if (!mat4_inv_apply(&inst->node_world[ni], contact, &p->att_local)) return;
    p->att_inst = inst; p->att_node = ni; p->att_world = contact;
}

/* ---- Jumper (0x462d70 update, 0x462fd0 tick) ------------------------------------ */
static void jumper_start_jump(Jumper *j) { j->D = J_P68 * 0.75f; j->t = (j->D / J_V) * -0.5f; j->h_prev = 0; j->v_down = 0; j->short_hop = j->fell_off = 0; }   /* 0x462d10 */
static void jumper_start_fall(Jumper *j, int fell_off) { j->D = J_P68 * 1.25f; j->fell_off = fell_off; j->h_prev = J_HEIGHT; j->t = 0; }                       /* 0x462d40 */
static int jumper_tick(Jumper *j, float dt, int on_ground)
{
    j->t += dt;
    float u = j->t / (0.5f * j->D / J_V), h = J_HEIGHT - J_HEIGHT * u * u;
    if (j->t >= J_APEX_T && j->state == 1 && !j->short_hop) { j->state = 7; j->open_window = 1; }   /* 0x457560(0.5): air attack window */
    if (j->t >= 0 && (j->state == 7 || (j->state == 1 && j->short_hop))) {
        j->state = j->short_hop ? (on_ground ? 6 : 4) : 3;
        jumper_start_fall(j, 0); h = J_HEIGHT;
    }
    if (u > 1.0f) { float k = u - 1.0f < 0.5f ? u - 1.0f : 0.5f; j->dy = -(((J_TERMINAL - j->v_down) * 2 * k + j->v_down) * dt); }
    else { j->dy = h - j->h_prev; j->v_down = -j->dy / dt; }
    j->h_prev = h;
    return !(u < 0.45f && !j->short_hop);
}
static void jumper_update(Jumper *j, int jump_held, int on_ground, float height_above_ground, float dt)
{
    if (j->state == 2) { j->dy = 0; j->fallen = 0; j->hard_fall = 0; }
    if (height_above_ground <= J_REARM_H && !jump_held) j->armed = 1;
    if (j->coyote) { j->coyote_t += dt; if (j->coyote_t > J_COYOTE) j->coyote = 0; }
    switch (j->state) {
    case 2:
        if (jump_held && on_ground && j->armed) { j->state = 0; jumper_start_jump(j); jumper_tick(j, dt, on_ground); j->armed = 0; return; }
        if (!on_ground) { j->coyote = j->armed; if (j->armed) j->coyote_t = 0; j->state = 3; jumper_start_fall(j, 1); jumper_tick(j, dt, on_ground); j->armed = 0; }
        return;
    case 0: j->state = 1; /* fallthrough */
    case 1:
        if (!jump_held && j->t < J_SHORT_HOP_T) {                 /* variable jump height: only the clock jumps */
            float u = J_SHORT_HOP_T / (0.5f * j->D / J_V);
            j->short_hop = 1; j->t = J_SHORT_HOP_T; j->h_prev = J_HEIGHT - J_HEIGHT * u * u;
        }
        jumper_tick(j, dt, on_ground); return;
    case 7: if (jumper_tick(j, dt, on_ground) && on_ground) j->state = 6; return;
    case 3:
        if (jumper_tick(j, dt, on_ground)) j->state = 4;
        if (j->coyote && jump_held) { j->coyote = 0; j->state = 0; jumper_start_jump(j); jumper_tick(j, dt, on_ground); j->armed = 0; }
        return;
    case 4: if (j->fallen > J_HARD_FALL) { j->hard_fall = 1; j->state = 5; } /* fallthrough */
    case 5: if (jumper_tick(j, dt, on_ground) && on_ground) j->state = 6; return;
    case 6: j->state = 2; return;
    }
}

/* ---- volumes ------------------------------------------------------------------ */
int volume_contains(const Instance *inst, uint32_t node, Vec3 p)
{
    const Model *m = inst->model; const InsNode *n = &m->nodes[node];
    if (!n->polys || n->npolys < 4) return 0;
    /* centroid of the transformed points decides the outward side of each face */
    Vec3 cen = { 0, 0, 0 }; uint32_t cnt = 0;
    for (uint32_t k = 0; k < n->npoints; k++) { Vec3 w = ins_point_world(inst, n->point_base + k); cen.x += w.x; cen.y += w.y; cen.z += w.z; cnt++; }
    if (!cnt) return 0;
    cen.x /= cnt; cen.y /= cnt; cen.z /= cnt;
    int valid = 0;
    for (uint32_t f = 0; f < n->npolys; f++) {
        const InsPoly *pl = &n->polys[f]; if (pl->nverts < 3) continue;
        Vec3 a = ins_point_world(inst, pl->indices[0]), b = ins_point_world(inst, pl->indices[1]), c = ins_point_world(inst, pl->indices[2]);
        Vec3 nrm = vcross(vsub(b, a), vsub(c, a));
        float side_c = vdot(nrm, vsub(cen, a)), side_p = vdot(nrm, vsub(p, a));
        if (fabsf(side_c) < 1e-6f) continue;                          /* degenerate face (or unposed instance) */
        valid++;
        if ((side_c > 0) != (side_p > 0) && side_p != 0) return 0;   /* p on the other side of a face than the centroid */
    }
    return valid >= 4;
}

/* The player is the instance the level script gives a Perso class (SetTypeInstance 1, 2, 3, 18 or 19): Woody in the W
 * levels, type 2 in the K levels, 3 in the S levels, 18 in the races. Until the script has run it is model 0 instance 0. */
void player_bind(Player *p, Instance *inst)
{
    p->inst = inst; p->pos = inst->position; p->floor_y = p->pos.y; p->cam_init = 0;
    /* facing from the instance quaternion: rotation about y composed with the -90 deg x model rotation */
    ins_pose(inst, 0, 0);
    Vec3 fwd = mat4_apply(&inst->world, (Vec3){ 0, -1, 0 }); fwd = vsub(fwd, inst->position);
    p->yaw = atan2f(fwd.x, fwd.z);
    p->spawn_pos = p->pos; p->spawn_yaw = p->yaw;
}

int player_init(Player *p, InsFile *ins, const GelFile *gel, const TexFile *tex)
{
    memset(p, 0, sizeof *p);
    if (!ins->nmodels || !ins->models[0].ninstances) return -1;
    p->gel = gel; p->ins = ins; p->tex = tex; p->cur_col = 0xffffffffu; p->step_u = -1.0f;
    player_bind(p, &ins->models[0].instances[0]);
    p->jumper.state = 2; p->jumper.armed = 1;                      /* 0x462c90 reset */
    p->health = 3.0f; p->lives = 3; p->game_state = 2; p->fade = 1.0f; p->lanim = -1;
    /* volume table */
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) p->nvol += ins->models[mi].nvolume_nodes * ins->models[mi].ninstances;
    p->inside = (uint8_t *)calloc(p->nvol ? p->nvol : 1, 1);
    p->vol_inst = (Instance **)calloc(p->nvol ? p->nvol : 1, sizeof *p->vol_inst);
    p->vol_node = (uint32_t *)calloc(p->nvol ? p->nvol : 1, 4); p->vol_id = (uint32_t *)calloc(p->nvol ? p->nvol : 1, 4);
    uint32_t v = 0;
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) {
        Model *m = &ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) for (uint32_t j = 0; j < m->nvolume_nodes; j++) {
            Instance *in = &m->instances[k];
            p->vol_inst[v] = in; p->vol_node[v] = m->volume_nodes[j] - 1;   /* node lists in the file are 1-based */ p->vol_id[v] = j < in->nids ? in->ids[j] : 0; v++;
        }
    }
    /* pose every instance once so volume tests are valid before the first rendered frame */
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) for (uint32_t k = 0; k < ins->models[mi].ninstances; k++) ins_pose(&ins->models[mi].instances[k], 0, 0);
    p->spawn_pos = p->pos; p->spawn_yaw = p->yaw;
    printf("player: start (%.0f %.0f %.0f) yaw %.1f deg, %u trigger volumes\n", p->pos.x, p->pos.y, p->pos.z, p->yaw * 57.2958f, p->nvol);
    if (getenv("WOODY_VOLDUMP")) for (uint32_t v = 0; v < p->nvol; v++)   /* id of every trigger volume (VOL_FLAG5 in the level script) and the instance carrying it */
        printf("  VOL 0x%x: inst %u at %.0f %.0f %.0f", p->vol_id[v], p->vol_inst[v]->index, p->vol_inst[v]->position.x, p->vol_inst[v]->position.y, p->vol_inst[v]->position.z), puts("");
    return 0;
}

/* segment a->b blocked by a world polygon? (line-of-sight veto 0x423a40; instances are not tested yet) */
static int gel_ray_blocked(const GelFile *g, Vec3 a, Vec3 b)
{
    GelPolySet ps = gel_polys_on_seg(g, a, b);
    for (uint32_t k = 0; k < ps.n; k++) {
        const GelPoly *pl = &g->polys[ps.polys[k]]; if (pl->nverts < 3) continue;
        float da = pl->plane[0] * a.x + pl->plane[1] * a.y + pl->plane[2] * a.z + pl->plane[3];
        float db = pl->plane[0] * b.x + pl->plane[1] * b.y + pl->plane[2] * b.z + pl->plane[3];
        if ((da > 0) == (db > 0)) continue;
        float t = da / (da - db); Vec3 q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        if (poly_contains(g, pl, q)) return 1;
    }
    return 0;
}

float gel_ray_hit(const GelFile *g, Vec3 a, Vec3 b, Vec3 *n_out)
{
    float best = 2.0f;
    GelPolySet ps = gel_polys_on_seg(g, a, b);
    for (uint32_t k = 0; k < ps.n; k++) {
        const GelPoly *pl = &g->polys[ps.polys[k]]; if (pl->nverts < 3) continue;
        float da = pl->plane[0] * a.x + pl->plane[1] * a.y + pl->plane[2] * a.z + pl->plane[3];
        float db = pl->plane[0] * b.x + pl->plane[1] * b.y + pl->plane[2] * b.z + pl->plane[3];
        if ((da > 0) == (db > 0)) continue;
        float t = da / (da - db); if (t >= best) continue;
        Vec3 q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        if (!poly_contains(g, pl, q)) continue;
        best = t;
        if (n_out) { *n_out = (Vec3){ pl->plane[0], pl->plane[1], pl->plane[2] };                  /* towards the side the ray came from */
                     if (da < 0) { n_out->x = -n_out->x; n_out->y = -n_out->y; n_out->z = -n_out->z; } }
    }
    return best;
}
float gel_ray_frac(const GelFile *g, Vec3 a, Vec3 b) { return gel_ray_hit(g, a, b, NULL); }

int player_segment_blocked(const Player *p, Vec3 a, Vec3 b) { return gel_ray_frac(p->gel, a, b) <= 1.0f; }

/* ---- logical animations: table 0x4b6180 (0x1c bytes per record: sub[4], prio, speed, restart) --------------
 * A logical animation is a chain of up to four .ins animations played in order, the last one looping (-1 = hold);
 * the speed divides the .ins duration (docs/PERSO_JUMP.md 4). Priorities are not used here. */
typedef struct { int sub[4]; float speed; int restart; } LogAnim;
static const LogAnim *log_anim(int n)
{
    static const LogAnim t[0x18] = {
        {{0,0,0,0},3,0}, {{0,0,0,0},3,0}, {{1,2,2,2},3,1}, {{2,2,2,2},3,0}, {{3,4,4,4},8,1}, {{5,6,7,7},2,1}, {{6,7,7,7},3,0},
        {{6,7,7,7},3,1}, {{8,0,0,0},3,1}, {{7,32,33,33},3,0}, {{34,0,0,0},3,1}, {{9,9,9,9},3,1}, {{10,11,-1,-1},3,1},
        {{16,0,0,0},3,1}, {{19,6,7,7},3,1}, {{21,13,13,13},3,1}, {{38,51,51,51},3,1}, {{51,51,51,51},3,0}, {{51,52,0,0},3,0},
        {{86,0,0,0},3,1}, {{12,13,13,13},5,1}, {{13,13,13,13},3,0}, {{14,6,7,7},3,0}, {{15,-1,-1,-1},3,0} };   /* 0x14..0x17: grab, climb, let go, over the top */
    /* doors 0x18/0x19 (records 24/25), hit 0x1f/0x20, deaths 0x25..0x30 (docs/PERSO_DEATH.md 3.3) */
    static const struct { int n; LogAnim a; } x[] = {
        { 0x18, {{17,-1,-1,-1},3,1} }, { 0x19, {{18,-1,-1,-1},3,1} }, { 0x1f, {{20,0,0,0},3,1} }, { 0x20, {{22,0,0,0},3,1} },
        { 0x25, {{29,30,30,30},3,1} }, { 0x26, {{28,30,30,30},3,1} }, { 0x29, {{31,-1,-1,-1},3,1} }, { 0x2a, {{27,-1,-1,-1},3,1} },
        { 0x3b, {{84,-1,-1,-1},3,1} }, { 0x3c, {{80,-1,-1,-1},3,1} }, { 0x3d, {{81,82,82,82},3,1} }, { 0x3e, {{82,82,82,82},3,1} },   /* rocket: mount, sit, ignition, flight */
        { 0x2b, {{37,-1,-1,-1},3,1} }, { 0x2e, {{35,-1,-1,-1},3,1} }, { 0x2f, {{48,33,33,33},3,1} }, { 0x30, {{85,-1,-1,-1},3,1} },
        /* results screen (docs/CINEMATIC.md 6): one-shot records for the .ins animations 74..78, all speed 3 */
        { 0x1a, {{74,-1,-1,-1},3,1} }, { 0x1b, {{75,-1,-1,-1},3,1} }, { 0x1c, {{78,-1,-1,-1},3,1} }, { 0x1d, {{76,-1,-1,-1},3,1} }, { 0x1e, {{77,-1,-1,-1},3,1} } };
    for (unsigned i = 0; i < sizeof x / sizeof x[0]; i++) if (x[i].n == n) return &x[i].a;
    return (n >= 0 && n < 0x18) ? &t[n] : &t[0];
}
/* 0x463e30: a scripted action names a raw .ins animation; the logical record is the first one that starts with it */
static int log_from_raw(int act) { for (int n = 0; n < 0x80; n++) if (log_anim(n)->sub[0] == act) return n; return -1; }
static float anim_len(const Player *p, int n, int k)                       /* 0x436b90 AnimLen(n, k) */
{
    const LogAnim *a = log_anim(n); const Model *m = p->inst->model; int s = a->sub[k];
    return (s >= 0 && (uint32_t)s < m->nanims) ? m->anims[s].duration_s / a->speed : 0.0f;
}
static void anim_request(Player *p, int n, float rate)                     /* 0x436b70 Request + 0x436a50 Tick */
{
    Instance *wi = p->inst; const LogAnim *a = log_anim(n); const Model *m = wi->model;
    if (n != p->lanim) {
        if (getenv("WOODY_ANIMLOG")) printf("  lanim %d -> %d (jumper %d t %.3f)\n", p->lanim, n, p->jumper.state, p->jumper.t);
        p->lanim = n; p->lanim_sub = 0;
        if ((uint32_t)a->sub[0] < m->nanims && (a->restart || wi->anim != a->sub[0])) { wi->anim = a->sub[0]; wi->anim_time = 0; }
    }
    wi->anim_speed = a->speed * rate;
    /* advance through the chain */
    int nx = p->lanim_sub < 3 ? a->sub[p->lanim_sub + 1] : 0;
    if ((uint32_t)wi->anim < m->nanims && p->lanim_sub < 3 && (nx < 0 || (uint32_t)nx >= m->nanims)) {     /* hold the last frame: the clock advances after this, so stop it just before the end */
        if (wi->anim_time >= m->anims[wi->anim].duration_s - 0.15f) { wi->anim_time = m->anims[wi->anim].duration_s * 0.999f; wi->anim_speed = 0; }
    } else if ((uint32_t)wi->anim < m->nanims && wi->anim_time >= m->anims[wi->anim].duration_s && p->lanim_sub < 3) {
        { wi->anim_time -= m->anims[wi->anim].duration_s; wi->anim = nx; p->lanim_sub++; }
    }
}

/* ---- attack controller 0x457a50 + trigger 0x457330 (docs/PERSO_JUMP.md 2) -----------------------------------
 * Ported: peck dash with auto-aim (1,2), hit pause and recoil (3,4,5), wall/ground rebound (6,7), charge run with
 * auto-steer (9,10), brake (11), hit loop against the enemies. Not ported: peckable surfaces (8), the steep-edge test,
 * rumble. The dash additionally ends on landing, which the original leaves to its ray probe. */
static void lock_move(Player *p, float t) { p->move_lock = t; p->ramp_phase = 0; p->speed = 0; }   /* 0x44cce0 */
/* did the fraction of a looping animation pass `t` between the previous frame and this one? */
static int phase_passed(float prev, float cur, float t) { return cur >= prev ? (t > prev && t <= cur) : (t > prev || t <= cur); }
static void jumper_reset(Jumper *j) { memset(j, 0, sizeof *j); j->state = 2; j->armed = 1; }       /* 0x462c90 */
static void jumper_force_fall(Jumper *j, int force)                                                 /* 0x463170 */
{
    if ((j->state == 3 || j->state == 4 || j->state == 5) && !force) return;
    j->coyote = 0; jumper_start_fall(j, 1); j->v_down = 0; j->state = 4;
}
static int attack_probe(Player *p, Vec3 v)                                                          /* 0x4575b0 */
{
    Vec3 a = { p->pos.x, p->pos.y + 5.0f, p->pos.z }, b = { a.x + v.x, a.y + v.y, a.z + v.z }, face_n;
    float f = gel_ray_hit(p->gel, a, b, &face_n); if (f > 1.0f) return 0;
    /* 0x4575b0 fires the impact on ANY hit. It passes no normal (kind 1); the port takes the normal of the face the
     * same ray just hit, because the hole and the chips have to come out of that face (docs/OBJECTS.md 1.6). */
    game_peck_fx(1, (Vec3){ a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f }, &face_n);
    int found; const Instance *hi; const InsNode *hn;
    float gy = world_ground(p, (Vec3){ p->pos.x, p->pos.y + 1.0f, p->pos.z }, &found, &hi, &hn);
    int n = (found && p->pos.y - gy > 100.0f) ? 0xe : 0xd;
    p->atk = n == 0xe ? 7 : 6; p->atk_t = anim_len(p, n, 0); lock_move(p, p->atk_t);
    return 1;
}
/* target finder 0x4632e0 / 0x463420: nearest attackable instance (type bit 0x400) within r (3D) */
static Enemy *nearest_enemy(Player *p, float r)
{
    Enemy *best = NULL; float bd = r * r;
    if (!p->enemies) return NULL;
    for (int i = 0; i < p->enemies->n; i++) {
        Enemy *e = &p->enemies->e[i]; if (e->removed || !e->attackable || !e->inst->visible) continue;
        Vec3 d = vsub(e->pos, p->pos); float dd = vdot(d, d); if (dd < bd) { bd = dd; best = e; }
    }
    return best;
}
static void auto_aim(Player *p)                                          /* 0x4579a0: the charge run steers to the nearest enemy */
{
    Enemy *t = nearest_enemy(p, 500.0f); if (!t) return;
    float dx = t->pos.x - p->pos.x, dz = t->pos.z - p->pos.z; if (dx * dx + dz * dz > 1.0f) p->yaw = atan2f(dx, dz);
    p->target = t;
}
/* hit loop 0x457ceb: dash = swept circle (radius 100 + target radius) along dash start -> position in xz plus a height
 * overlap; charge run = 50 long beak segment against the target's vertical cylinder. The target handles the hit itself. */
static void attack_hit_loop(Player *p)
{
    if (!p->enemies) return;
    for (int i = 0; i < p->enemies->n; i++) {
        Enemy *e = &p->enemies->e[i]; if (e->removed || !e->attackable || !e->inst->visible) continue;
        float r = enemy_radius(e), h = enemy_height(e); int hit = 0; Vec3 dir = { 0, 0, 0 };
        if (p->atk == 2) {
            float ax = p->dash_start.x, az = p->dash_start.z, bx = p->pos.x - ax, bz = p->pos.z - az, l2 = bx * bx + bz * bz;
            float t = l2 > 1e-6f ? ((e->pos.x - ax) * bx + (e->pos.z - az) * bz) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
            float cx = ax + bx * t - e->pos.x, cz = az + bz * t - e->pos.z, R = 100.0f + r;
            hit = cx * cx + cz * cz <= R * R && p->pos.y < e->pos.y + h + 100.0f && p->pos.y + P_BODY_H > e->pos.y;
        } else {
            Vec3 f = { sinf(p->yaw), 0, cosf(p->yaw) };
            for (int k = 0; k <= 2 && !hit; k++) {                        /* beak segment: from the body surface 50 forward, at head height */
                float s = P_RADIUS * 0.5f + 25.0f * k, qx = p->pos.x + f.x * s - e->pos.x, qz = p->pos.z + f.z * s - e->pos.z;
                hit = qx * qx + qz * qz <= (r + 15.0f) * (r + 15.0f) && p->pos.y + P_BODY_H * 0.6f > e->pos.y && p->pos.y < e->pos.y + h;
            }
            if (hit) { float dx = e->pos.x - p->pos.x, dz = e->pos.z - p->pos.z, l = sqrtf(dx * dx + dz * dz); if (l > 1e-3f) dir = (Vec3){ dx / l, 0, dz / l }; }
        }
        if (!hit) continue;
        int was = p->atk; if (p->atk == 2) p->atk = 3;
        int died = enemy_take_damage(e, 1.0f /* P+0x90 */, dir);
        if (died) audio_fx(6, e->inst, &e->inst->position.x);            /* 0x44d730: destroyed, 3D on the victim */
        printf("  ATTACK hit enemy %u (%s)%s\n", e->inst->index, was == 2 ? "peck" : "charge", died ? " - dead" : "");
        if (was == 2) return;
    }
}

static void attack_update(Player *p, const PlayerInput *in, float dt)
{
    p->use_atk_disp = 0;
    if (p->charge > 0) p->charge -= dt;
    Vec3 dir = { sinf(p->yaw), 0, cosf(p->yaw) };
    switch (p->atk) {
    case 1: {                                                             /* dash start: aim at the nearest attackable target within 500 that is > 50 below, else diagonally down */
        Enemy *t = nearest_enemy(p, 500.0f); p->has_target = 0; p->target = NULL;
        p->atk_dir = (Vec3){ dir.x, -2.0f, dir.z };
        if (t) {
            p->aim = t->pos; p->aim.y += enemy_height(t) * 0.8f; p->target = t;
            if (p->pos.y - p->aim.y > 50.0f) { p->atk_dir = vsub(p->aim, p->pos); p->has_target = 1; enemy_warn_dive(t, p->atk_dir);   /* 0x457f90 -> vtbl[37] */ float yl = sqrtf(p->atk_dir.x * p->atk_dir.x + p->atk_dir.z * p->atk_dir.z); if (yl > 0.01f) p->yaw = atan2f(p->atk_dir.x, p->atk_dir.z); }
        }
        p->dash_start = p->pos; audio_fx(55 + rand() % 3, NULL, NULL);          /* 0x45752b: air attack cry, 0x37 + rand(0,3) */
        { float l = sqrtf(vdot(p->atk_dir, p->atk_dir)); p->atk_dir.x /= l; p->atk_dir.y /= l; p->atk_dir.z /= l; }
        p->jumper.fallen = 0; p->jumper.hard_fall = 0; p->air_win = 0; p->atk = 2; return; }
    case 2:
        p->atk_disp = (Vec3){ p->atk_dir.x * dt * 1500.0f, p->atk_dir.y * dt * 1500.0f, p->atk_dir.z * dt * 1500.0f }; p->use_atk_disp = 1;
        if (!attack_probe(p, (Vec3){ p->atk_dir.x * 50.0f, p->atk_dir.y * 50.0f, p->atk_dir.z * 50.0f }))
            attack_probe(p, (Vec3){ dir.x * 100.0f, 0, dir.z * 100.0f });
        if (p->atk == 2 && p->on_ground) { p->atk = 6; p->atk_t = anim_len(p, 0xd, 0); lock_move(p, p->atk_t); }
        if (p->atk == 2) attack_hit_loop(p);
        return;
    case 3: p->atk = 4; p->target = NULL; p->atk_t = anim_len(p, 0xc, 0); p->atk_disp = (Vec3){ 0, 0, 0 }; p->use_atk_disp = 1; return;   /* hit: hang still */
    case 4:
        p->use_atk_disp = 1; p->atk_disp = (Vec3){ 0, 0, 0 };
        if ((p->atk_t -= dt) > 0) return;
        /* recoil direction (-dir.x, 0.8, ~0): the original uses dir.y for z at 0x458686, so z is ~0 */
        p->atk_dir = (Vec3){ -dir.x, 0, 0 }; { float l = fabsf(p->atk_dir.x); if (l > 1e-4f) p->atk_dir.x /= l; p->atk_dir.y = 0.8f; l = sqrtf(vdot(p->atk_dir, p->atk_dir)); p->atk_dir.x /= l; p->atk_dir.y /= l; }
        p->atk_t = 0.75f; p->atk = 5; return;
    case 5:
        p->atk_t -= dt; { float v = dt * p->atk_t * 1000.0f; p->atk_disp = (Vec3){ p->atk_dir.x * v, p->atk_dir.y * v, p->atk_dir.z * v }; } p->use_atk_disp = 1;
        if (p->atk_t < 0.5f && !(p->air_win > 0)) p->air_win = 0.5f;          /* chained attack possible */
        if (p->atk_t <= 0) { jumper_force_fall(&p->jumper, 1); p->atk = 0; }
        return;
    case 6:
        jumper_reset(&p->jumper);
        if ((p->atk_t -= dt) < 0.4f) p->vy_corr -= (p->pos.y - p->floor_y) * 2.5f;   /* spring toward the ground */
        if (p->atk_t <= 0) { p->pos.y = p->floor_y; p->on_ground = 1; p->atk = 0; }  /* 0x462990 snap to ground */
        return;
    case 7: jumper_reset(&p->jumper); if ((p->atk_t -= dt) <= 0) { p->atk = 0; jumper_force_fall(&p->jumper, 0); } return;
    case 9:
        if ((p->atk_t -= dt) <= 0) p->atk = 10;
        auto_aim(p); dir = (Vec3){ sinf(p->yaw), 0, cosf(p->yaw) };
        p->use_atk_disp = 1; p->atk_disp = (Vec3){ dir.x * dt * 700.0f, 0, dir.z * dt * 700.0f }; attack_hit_loop(p); return;
    case 10:
        auto_aim(p); dir = (Vec3){ sinf(p->yaw), 0, cosf(p->yaw) };
        if (p->charge > 0 && !in->jump) { p->use_atk_disp = 1; p->atk_disp = (Vec3){ dir.x * dt * 700.0f, 0, dir.z * dt * 700.0f }; attack_hit_loop(p); return; }
        if (in->jump) { lock_move(p, 0); p->atk = 0; return; }             /* jump cancels the run */
        player_brake_charge(p); return;                                    /* 0x457e16: the charge ran out */
    case 11: if ((p->atk_t -= dt) <= 0) p->atk = 0; return;
    default: return;
    }
}
static void attack_trigger(Player *p, const PlayerInput *in, float dt)
{
    int held = in->action, pressed = held && !p->action_prev, released = !held && p->action_prev;
    p->action_prev = held;
    if (p->jumper.state != 2 && p->jumper.state != 6) p->charge = 0;       /* no charge in the air */
    if (p->air_win > 0) p->air_win -= dt;
    if (p->atk == 5 && pressed && p->air_win > 0) { p->atk = 1; return; }  /* chained attack out of the recoil */
    if (p->move_lock > 0 || p->atk != 0) return;
    if (p->on_ground) {
        if (released) {                                                    /* charge run starts on RELEASE */
            p->atk_t = anim_len(p, 0x10, 0) + anim_len(p, 0x11, 0); p->atk = 9;
            if (p->charge <= 0.1f) { lock_move(p, p->atk_t); p->charge = 0; }
        } else if (held) { p->charge += 4.0f * dt; if (p->charge > 1.5f) p->charge = 1.5f; }
    } else if (pressed && p->air_win > 0) p->atk = 1;                      /* air: peck dash */
}

/* Perso state (+0x21c), set by SetState 0x44c980 and read by 0x44bcf0: 0 means the player has his own controls.
 * The port keeps that state in the fields that stand for it - 2 = dead, 4 = hanging in a peckable wall, 5 = a
 * scripted action, 8 = riding a class-20 rocket - so the messages that only answer "when the Perso is free"
 * (1042, and 0x465740 when he steps onto a rocket) ask here. */
int player_state_free(const Player *p) { return !p->dead_kind && !p->climb_sub && !p->use_root && !p->script_act && !p->ride; }

/* 0x458e40 Perso_BrakeCharge, the one brake that is called from outside the attack controller: the game code calls it
 * at 0x44542f, in the handler of message 1042, when the player is standing at a peck switch. Releasing the attack
 * button started the charge run (atk 9) in the Perso update of this very frame; the VM tick that follows brakes it
 * again, so what the player sees at the switch is the peck animation 0x12 in place and not a run into it.
 * Only the two charge states are braked - the dash and the rebounds are left alone - and the brake is the same one
 * the controller uses internally (0x457b0a / 0x457e16, docs/PERSO_JUMP.md 2.3). */
void player_brake_charge(Player *p)
{
    if (p->atk != 9 && p->atk != 10) return;
    p->charge = 0;
    p->atk_t = anim_len(p, 0x12, 0) + anim_len(p, 0x12, 1);
    lock_move(p, p->atk_t);                                                /* LockMove(T, 0) */
    p->atk = 11;                                                           /* atk_anim[11] = 0x12 plays the peck */
}

/* ---- damage, death, respawn (docs/PERSO_MOVE.md 4.4, PERSO_FRAME.md 4.1) ------------------------------------ */
void player_kill(Player *p, int kind)                                   /* vt[38] Kill(kind) 0x44c110 */
{
    if (p->dead_kind) { if (!((kind == 7 && p->dead_kind != 7) || (kind == 1 && p->dead_kind != 1))) return; }
    if ((kind == 2 || kind == 9 || kind == 3 || kind == 8 || kind == 4 || kind == 5 || kind == 6) && p->invuln_respawn > 0) return;
    p->death_delay = 3.5f;                                              /* +0x288: time until the fade */
    if (kind == 2 || kind == 9) p->death_delay = 1.5f; else if (kind == 3 || kind == 8) p->death_delay = 3.0f;
    else if (kind == 6) p->death_delay = 2.5f; else if (kind == 7) p->death_delay = 0.0f;
    if (kind == 7) { jumper_reset(&p->jumper); p->att_inst = NULL; } else if (kind != 2 && kind != 9) jumper_force_fall(&p->jumper, 0);
    p->nograv_t = kind == 1 ? anim_len(p, 0x2f, 0) : (kind == 2 || kind == 9) ? anim_len(p, 0x30, 0) : 0;   /* +0x240: no fall while he hangs / is zapped */
    p->dead_T = 0; p->dead_cam_req = 0; p->hit_anim_t = 0; p->script_act = 0; p->ride = NULL;
    p->atk = 0; p->charge = 0; p->health = 0; p->dead_kind = kind;      /* state := 2 */
    printf("  PLAYER killed (kind %d), lives %d\n", kind, p->lives);
}
int player_hit(Player *p, float damage, Vec3 dir)                       /* vt[39] Hit 0x44ca00: returns 1 when health ran out */
{
    if (p->dead_kind || p->invuln_respawn > 0 || p->invuln_hit > 0 || getenv("WOODY_GOD")) return 0;   /* WOODY_GOD: testing */
    jumper_force_fall(&p->jumper, 0);
    /* knockback 0x45a140: RampC to 500 u/s (0.1 s up), held 0.2 s, 0.5 s out; the player turns to face the attacker */
    float l = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (p->push_t <= 0) { p->push_dir = l > 0.01f ? (Vec3){ dir.x / l, 0, dir.z / l } : (Vec3){ 0, 0, 0 }; p->push_t = 0.2f; p->push_speed = 0; }
    if (l > 0.01f) p->yaw = atan2f(-dir.x, -dir.z);
    if (p->invuln_hit < 0.6f) p->invuln_hit = 0.6f;
    p->move_lock = 0; p->atk = 0;
    p->hit_anim = p->on_ground ? 0x1f : 0x20; p->hit_anim_t = anim_len(p, p->hit_anim, 0); p->lanim = -1;   /* 0x464b70: priority 5110, plays out over walking / jumping */
    if (getenv("WOODY_ONEHIT")) damage = 99;                             /* testing: every hit kills */
    p->health -= damage; if (p->health < 0) p->health = 0;
    printf("  PLAYER hit, health %.0f\n", p->health);
    if (p->health <= 0) { p->health = 0; return 1; }
    return 0;
}
static void player_reset(Player *p)                                     /* vt[17] Reset 0x44ab20 + respawn in 0x4459c0 */
{
    p->pos = p->spawn_pos; p->yaw = p->spawn_yaw; p->floor_y = p->pos.y;
    jumper_reset(&p->jumper); p->on_ground = 1; p->invuln_respawn = 1.0f; p->invuln_hit = 0; p->move_lock = 0;
    if (p->health <= 0) p->health = 3.0f;
    p->ride = NULL; p->dead_kind = 0; p->dead_T = 0; p->nograv_t = 0; p->hit_anim_t = 0; p->script_act = 0; p->atk = 0; p->charge = 0; p->speed = 0; p->ramp_phase = 0; p->slide_speed = 0; p->push_t = 0; p->push_speed = 0;
    p->att_inst = NULL; p->lanim = -1; p->step_u = -1.0f; p->cam_init = 0;
}
/* Game sequence 0x4459c0: 2 play -> (dead) 3 wait death_delay - 1 s -> 4 fade out 1 s -> lose a life -> 0 wait 0.25 s,
 * respawn -> 1 fade in 1 s -> 2. p->fade is the screen brightness (1 = normal). */
static void game_sequence(Player *p, EkoVM *vm, float dt)
{
    switch (p->game_state) {
    case 2: if (p->dead_kind) { p->game_state = 3; p->game_t = 0; } break;
    case 3: p->game_t += dt; if (p->game_t >= p->death_delay - 1.0f) { p->game_state = 4; p->game_t = 0; } break;
    case 4: p->game_t += dt; p->fade = 1.0f - p->game_t; if (p->fade <= 0) {
                p->fade = 0; if (p->lives > 0) p->lives--;               /* 0x44c730: life lost, leave all volumes, msgmask 0x10 pulse */
                if (vm) { eko_actor_leave_all(vm, p->inst->id); eko_msgmask_set(vm, p->inst->id, 0x10); p->mask10_frames = 2; }
                for (uint32_t v = 0; v < p->nvol; v++) p->inside[v] = 0;
                p->game_state = 0; p->game_t = 0.25f; } break;
    case 0: p->game_t -= dt; if (p->game_t <= 0) { player_reset(p); p->game_state = 1; p->game_t = 0; } break;
    case 1: p->game_t += dt; p->fade = p->game_t; if (p->fade >= 1.0f) { p->fade = 1.0f; p->game_state = 2; } break;
    }
    if (p->mask10_frames > 0 && --p->mask10_frames == 0 && vm) eko_msgmask_clear(vm, p->inst->id, 0x10);
}

/* ---- peck climbing: Perso state 4 (0x4651d0). A press node (kind 1) with typecode 4 is a peckable wall. ---- */
static float g_climb_frac;   /* fraction of the last climb_ray hit */
static int climb_ray(const Player *p, Vec3 from, Vec3 to, Vec3 *n_out, const Instance **inst_out, int *peckable)
{
    float best = 2.0f; int hit = 0;
    for (uint32_t mi = 0; mi < p->ins->nmodels; mi++) {
        const Model *m = &p->ins->models[mi];
        for (uint32_t ni = 0; ni < m->nnodes; ni++) {
            const InsNode *nd = &m->nodes[ni]; if (nd->kind != 1 || !nd->polys) continue;
            for (uint32_t k = 0; k < m->ninstances; k++) {
                const Instance *in = &m->instances[k]; if (!in->visible || in->noncollide || in == p->inst) continue;
                if (inst_dist2_xz(in, from) > 3000.0f * 3000.0f) continue;
                for (uint32_t pi = 0; pi < nd->npolys; pi++) {
                    const InsPoly *pl = &nd->polys[pi]; if (pl->nverts < 3 || pl->nverts > 8) continue;
                    Vec3 v[8]; for (uint32_t c = 0; c < pl->nverts; c++) v[c] = ins_point_world(in, pl->indices[c]);
                    Vec3 nrm = vcross(vsub(v[1], v[0]), vsub(v[2], v[0])); float l = sqrtf(vdot(nrm, nrm)); if (l < 1e-6f) continue;
                    nrm.x /= l; nrm.y /= l; nrm.z /= l;
                    float da = vdot(vsub(from, v[0]), nrm), db = vdot(vsub(to, v[0]), nrm); if ((da > 0) == (db > 0)) continue;
                    float t = da / (da - db); if (t >= best) continue;
                    Vec3 q = { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t };
                    if (!point_in_poly3(v, pl->nverts, nrm, q)) continue;
                    if (da < 0) { nrm.x = -nrm.x; nrm.y = -nrm.y; nrm.z = -nrm.z; }   /* normal towards the player */
                    best = t; g_climb_frac = t; hit = 1; *n_out = nrm; *inst_out = in; *peckable = nd->type_code == 4;
                }
            }
        }
    }
    return hit;
}
static Vec3 climb_probe_to(const Player *p, Vec3 from) { Vec3 to = { from.x + sinf(p->yaw) * (P_RADIUS + 100.0f), from.y, from.z + cosf(p->yaw) * (P_RADIUS + 100.0f) }; return to; }

/* 0x464e00: grab when the attack meets a peckable, (nearly) vertical wall */
static int climb_try(Player *p)
{
    if (p->regrab > 0 || p->climb_sub) return 0;
    Vec3 from = { p->pos.x, p->pos.y + 40.0f, p->pos.z }, to = climb_probe_to(p, from), n; const Instance *wi; int peck = 0;
    if (!climb_ray(p, from, to, &n, &wi, &peck) || !peck || fabsf(n.y) > 0.05f) return 0;
    float l = sqrtf(n.x * n.x + n.z * n.z); if (l < 1e-4f) return 0;
    p->wall_n = (Vec3){ n.x / l, 0, n.z / l }; p->wall_inst = wi; p->yaw = atan2f(-p->wall_n.x, -p->wall_n.z);
    p->climb_sub = p->on_ground ? 1 : 2; p->grip = 0.8f; p->peck_t = 0.3f; jumper_reset(&p->jumper);   /* 0x462c90 at 0x4650ce */
    p->atk = 0; p->charge = 0; p->speed = 0; p->ramp_phase = 0; p->vel = (Vec3){ 0, 0, 0 }; p->use_atk_disp = 0;
    /* the hit that grabs is a probe hit (0x4575b0), so it pecks too; the wall normal is the one climb_ray just measured */
    { float f = g_climb_frac * 0.95f; game_peck_fx(1, (Vec3){ from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f }, &n); }
    printf("  CLIMB grab on instance %u\n", wi->index);
    return 1;
}
static void climb_update(Player *p, const PlayerInput *in, float dt)
{
    int tap = in->action && !p->climb_act_prev; p->climb_act_prev = in->action;
    if (getenv("WOODY_TAP")) { static float tt; tt += dt; if (tt > 0.3f) { tt = 0; tap = 1; } }   /* testing: tap the attack key automatically */
    if (p->grip > 0) p->grip -= dt;
    if (tap && p->grip < 0.5f) p->grip = 0.5f;                          /* tapping keeps him on the wall */
    if (p->grip <= 0 && p->climb_sub != 3) p->climb_sub = 4;
    switch (p->climb_sub) {
    case 1: anim_request(p, 0x14, 1.0f); p->climb_sub = 2; p->peck_t = 0.3f; break;
    case 2: {
        anim_request(p, 0x15, 1.0f);
        Vec3 side = { p->wall_n.z, 0, -p->wall_n.x };                   /* cross((0,1,0), n) */
        float s = 300.0f * dt, a = in->left ? -s : in->right ? s : 0;
        p->pos.y += 250.0f * dt;                                        /* always upwards, no input needed (P+0x74) */
        p->pos.x += side.x * a; p->pos.z += side.z * a;
        Vec3 from = { p->pos.x, p->pos.y + 40.0f, p->pos.z }, to = climb_probe_to(p, from), n; const Instance *wi; int peck = 0;
        if (climb_ray(p, from, to, &n, &wi, &peck)) {
            if (!peck) { p->climb_sub = 4; break; }
            /* 0x4aa164: pressed against the wall at 200 u/s; the cylinder stops at its radius (the root motion of the climb-over counts on it) */
            { float gap = g_climb_frac * (P_RADIUS + 100.0f) - P_RADIUS, st = 200.0f * dt; if (st > gap) st = gap; if (st > 0) { p->pos.x -= p->wall_n.x * st; p->pos.z -= p->wall_n.z * st; } }
            if ((p->peck_t -= dt) <= 0) {                               /* a peck every 0.3 s: 0x479c80(0, hit point, wall normal) */
                float f = g_climb_frac * 0.95f;                         /* 0x4a9c9c: just short of the wall, so the mark sits on it */
                p->peck_t += 0.3f;
                game_peck_fx(0, (Vec3){ from.x + (to.x - from.x) * f, from.y + (to.y - from.y) * f, from.z + (to.z - from.z) * f }, &n);
            }
        } else {
            /* nothing in front any more: the top, when within 50 of the wall instance's typecode-0 marker */
            const Model *wm = p->wall_inst->model; int top = 0; float ty = 0;
            for (uint32_t i = 0; i < wm->nnodes && !top; i++) if (wm->nodes[i].kind == 0x20 && wm->nodes[i].type_code == 0 && wm->nodes[i].npoints >= 1) { ty = ins_point_world(p->wall_inst, wm->nodes[i].point_base).y; top = 1; }
            if (top && fabsf(p->pos.y - ty) < 50.0f) {
                p->climb_sub = 3; p->over_len = p->over_t = anim_len(p, 0x17, 0) > 0.05f ? anim_len(p, 0x17, 0) : 0.5f; anim_request(p, 0x17, 1.0f);
                p->use_root = 1; p->root_pos = p->pos;
            } else p->climb_sub = 4;
        }
        break; }
    case 3: {
        anim_request(p, 0x17, 1.0f);
        /* Perso_RootMotion 0x44e290 (docs/OBJECTS.md 1.5): pos stays frozen while the root track of .ins anim 15 carries the model over
         * the edge; the position getter returns (1-f) pos + f q(f); on the last frame pos = the root's end relative to idle (anim 1) */
        p->over_t -= dt; float f = 1.0f - (p->over_t > 0 ? p->over_t / p->over_len : 0); Vec3 q, fw;
        if (ins_root_at(p->inst, 15, f, 1, &q, &fw)) p->root_pos = (Vec3){ p->pos.x + (q.x - p->pos.x) * f, p->pos.y + (q.y - p->pos.y) * f, p->pos.z + (q.z - p->pos.z) * f };
        if (p->over_t <= 0) {
            if (ins_root_at(p->inst, 15, 1.0f, 1, &q, &fw)) { p->pos = q; if (fw.x * fw.x + fw.z * fw.z > 1e-6f) p->yaw = atan2f(fw.x, fw.z); }
            { int fnd; const Instance *gi; const InsNode *gn; Vec3 pr = { p->pos.x, p->pos.y + P_PROBE_Y + 60.0f, p->pos.z }; float gy = world_ground(p, pr, &fnd, &gi, &gn); if (fnd && fabsf(gy - p->pos.y) < 150.0f) p->pos.y = gy; }   /* SnapToGround 0x462990 */
            p->use_root = 0; p->climb_sub = 0; jumper_reset(&p->jumper); p->on_ground = 1; p->lanim = -1; anim_request(p, 0, 1.0f); printf("  CLIMB over the top at %.0f %.0f %.0f\n", p->pos.x, p->pos.y, p->pos.z);
        }
        break; }
    default:                                                            /* 4: let go; no new grab for twice the fall animation */
        p->use_root = 0; p->regrab = 2.0f * anim_len(p, 0x16, 0); anim_request(p, 0x16, 1.0f);
        p->climb_sub = 0; jumper_force_fall(&p->jumper, 1); printf("  CLIMB let go\n");
        break;
    }
}

/* ---- bonuses (docs/BONUS.md): the script sends message 10 to the bonus instance; vtable[+0x70] per class ------ */
int player_collect(Player *p, int type, int arg)
{
    switch (type) {
    case 30: p->lives++; audio_fx(0, NULL, NULL); break;                  /* extra life, SoundFx 0 (0x44f43b) */
    case 34: p->bonus_got++; p->bonus_count++; audio_fx(3, NULL, NULL); break;                     /* "Bonus Woody", sound 3; 25 = a heart or a life */
    case 35: p->special_charges++; audio_fx(2, NULL, NULL); break;                                 /* charge for the special action (Perso+0x254), sound 2 */
    case 36: p->unique_items++; audio_fx(1, NULL, NULL); break;                                    /* unique item (save game flag), sound 1 */
    case 37: p->race_bonus++; audio_fx(5, NULL, NULL); break;                                      /* "Bonus Race", sound 5 */
    case 38: { float t = arg * 0.01f; if (p->invuln_respawn < t) p->invuln_respawn = t; if (p->invuln_hit < t) p->invuln_hit = t; } break;   /* invulnerability */
    default: return 0;
    }
    /* reward in Perso_Update 0x44b530: 25 bonuses = one heart, or an extra life when already at 5 hearts */
    if (p->bonus_count >= 25) { p->bonus_count -= 25; audio_fx(4, NULL, NULL); if (p->health < 5.0f) p->health += 1.0f; else { p->lives++; audio_fx(0, NULL, NULL); } }   /* 0x44b6e4, 0x44b71a */
    printf("  BONUS type %d collected: woody bonus %d / %d, health %.0f, lives %d\n", type, p->bonus_got, p->bonus_total, p->health, p->lives);
    return 1;
}

static void player_apply_transform(Player *p)
{
    Instance *in = p->inst;
    /* q = yaw(y) * rotx(-90): (0.7071 c, -0.7071 s, -0.7071 s, -0.7071 c) with c=cos(yaw/2), s=sin(yaw/2) */
    float c = cosf(p->yaw * 0.5f), s = sinf(p->yaw * 0.5f);
    in->position = p->pos;
    in->quat.x = 0.70710678f * c; in->quat.y = -0.70710678f * s; in->quat.z = -0.70710678f * s; in->quat.w = -0.70710678f * c;
    mat4_from_trs(&in->world, in->position, in->quat, in->scale);
}

void player_place(Player *p, Vec3 pos, float yaw)
{
    p->pos = pos; p->yaw = yaw; p->vel = (Vec3){ 0, 0, 0 }; p->speed = 0; p->ramp_phase = 0; p->floor_y = pos.y; p->atk = 0; p->move_lock = 0; p->lanim = -1; p->step_u = -1.0f;
    jumper_reset(&p->jumper); p->on_ground = 1; p->cam_init = 0; player_apply_transform(p);
}

/* trigger volumes: enter / in / leave -> script VM (player = "perso" variants); runs in every Perso state */
static void player_volumes_y(Player *p, EkoVM *vm, float probe_y)
{
    if (vm) {
        Vec3 probe = { p->pos.x, p->pos.y + probe_y, p->pos.z };
        for (uint32_t v = 0; v < p->nvol; v++) {
            Instance *in2 = p->vol_inst[v]; if (!in2->visible) { continue; }
            int now = volume_contains(in2, p->vol_node[v], probe);
            if (now && !p->inside[v]) { eko_vol_perso_enter(vm, p->vol_id[v], p->inst->id); p->events_sent++; printf("  VOL enter 0x%x (inst %u)\n", p->vol_id[v], in2->index); }
            else if (now) { eko_vol_perso_in(vm, p->vol_id[v], p->inst->id); }
            else if (p->inside[v]) { eko_vol_perso_leave(vm, p->vol_id[v], p->inst->id); p->events_sent++; printf("  VOL leave 0x%x (inst %u)\n", p->vol_id[v], in2->index); }
            p->inside[v] = (uint8_t)now;
        }
    }
}

static Quat q_unit(Quat q);
static void player_volumes(Player *p, EkoVM *vm) { player_volumes_y(p, vm, P_VOL_PROBE_Y); }

void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw)
{
    if (dt <= 0) return;
    p->vy_corr = 0;
    p->play_time += dt;                                                   /* 0x453ca0: the accumulator Perso+0x710 runs in every state, it is the level time on the results screen */
    if (p->move_lock > 0) p->move_lock -= dt;
    if (p->invuln_respawn > 0) p->invuln_respawn -= dt;
    if (p->invuln_hit > 0) p->invuln_hit -= dt;
    game_sequence(p, vm, dt);
    if (p->game_state == 0) return;                                       /* waiting for the respawn */
    /* fall damage 0x44b220: landing after more than 1500 fallen costs one heart */
    if (!p->dead_kind && p->jumper.state == 6 && p->atk == 0 && p->jumper.fallen >= J_HARD_FALL) {
        p->health -= 1.0f; printf("  PLAYER fall damage, health %.0f\n", p->health);
        if (p->health <= 0) { p->health = 0; player_kill(p, 8); }
    }
    if (!p->dead_kind && p->health <= 0) player_kill(p, 3);
    if (p->regrab > 0) p->regrab -= dt;
    if (p->hit_anim_t > 0) p->hit_anim_t -= dt;
    if (p->dead_kind) p->dead_T += dt;
    if (!p->dead_kind && p->script_act) {                                  /* state 5, 0x44db50: the Perso stands still, the movement is in the root track of the animation */
        int door = p->script_act == 17 || p->script_act == 18;
        anim_request(p, p->script_log, 1.0f);
        if (p->script_act == 17 && !p->script_faded && p->script_t < 0.6f) { p->script_faded = 1; p->fade_req = 1; }
        p->script_t -= dt;
        if (!door) {                                                       /* 0x44e290 (docs/OBJECTS.md 1.5): pos stays put, the root track of the action carries the model */
            float rest = p->script_t > 0 ? p->script_t : 0, f = p->script_total > 0 ? (p->script_total - rest) / p->script_total : 1.0f;
            Vec3 q, fw;
            if (ins_root_at(p->inst, p->script_act, f, 1, &q, &fw)) {
                p->use_root = 1;
                p->root_pos = (Vec3){ p->pos.x + (q.x - p->pos.x) * f, p->pos.y + (q.y - p->pos.y) * f, p->pos.z + (q.z - p->pos.z) * f };
            }
        }
        if (p->script_t <= 0) {
            if (p->script_act == 18) {                                     /* he came out backwards: facing flips, ground snap, idle, follow camera */
                p->yaw += 3.14159265f; int found; float gy = player_ground_query(p, p->inst, (Vec3){ p->pos.x, p->pos.y + P_PROBE_Y, p->pos.z }, &found);
                if (found) { p->pos.y = gy; p->floor_y = gy; } p->lanim = -1; anim_request(p, 0, 1.0f);
                p->cam_end_req = 1;                                        /* 0x44e5a0: follow camera behind the NEW facing, 0.5 s travelling */
            } else if (!door) {                                            /* last frame of 0x44e290: he ends where the root track left him, facing -E.row2 */
                Vec3 q, fw;
                if (ins_root_at(p->inst, p->script_act, 1.0f, 1, &q, &fw)) { p->pos = q; if (fw.x * fw.x + fw.z * fw.z > 1e-6f) p->yaw = atan2f(fw.x, fw.z); }
                int found; float gy = player_ground_query(p, p->inst, (Vec3){ p->pos.x, p->pos.y + P_PROBE_Y, p->pos.z }, &found);
                if (found) { p->pos.y = gy; p->floor_y = gy; }             /* SnapToGround 0x462990 */
                p->use_root = 0; p->lanim = -1; anim_request(p, 0, 1.0f); p->cam_end_req = 1;
            } else lock_move(p, 0.3f);                                     /* 17: the pose is held until the teleport (message 26) resets the controller */
            p->script_act = 0;
        }
        player_apply_transform(p); return;
    }
    if (!p->dead_kind && p->ride) {                                        /* state 8, 0x4657f0: the Perso follows the rocket (seat marker + its full rotation); no move, no collision, no gravity */
        Quat rq = p->ride_q; int st = p->ride_state, can = 0, ending = 0;
        if (p->ride_t > 0) {                                               /* first 0.3 s he stands, then 0.4 s to the saddle */
            p->ride_t -= dt;
            if (p->ride_t < 0.4f) { float u = (0.4f - p->ride_t) / 0.4f; if (u > 1) u = 1; p->ride_cur = q_slerp(p->ride_q0, rq, u);
                p->pos = (Vec3){ p->ride_p0.x + (p->ride_seat.x - p->ride_p0.x) * u, p->ride_p0.y + (p->ride_seat.y - p->ride_p0.y) * u, p->ride_p0.z + (p->ride_seat.z - p->ride_p0.z) * u }; }
        } else { p->pos = p->ride_seat; p->ride_cur = rq; }
        switch (st) { case 1: anim_request(p, 0x3b, 1.0f); break; case 2: case 3: anim_request(p, 0x3c, 1.0f); break; case 4: anim_request(p, 0x3d, 1.0f); break;
                      case 5: anim_request(p, 0x3e, 1.0f); can = 1; break; case 6: case 7: can = 1; ending = 1; break; default: break; }
        int leave = ((in->jump && !p->ride_jprev) || (in->action && !p->ride_aprev)) && can;
        if (st == 0 || st == 9) leave = 1;                                 /* port deviation (ROCKET.md 0.4): the original hangs here when the rider survives the blast */
        p->ride_jprev = in->jump; p->ride_aprev = in->action;
        Quat u = q_unit(p->ride_cur); Vec3 nose = { -2 * (u.x * u.y - u.z * u.w), -(1 - 2 * (u.x * u.x + u.z * u.z)), -2 * (u.y * u.z + u.x * u.w) };   /* model -Y */
        if ((p->ride_t <= 0 || leave || ending) && nose.x * nose.x + nose.z * nose.z > 1e-4f) p->yaw = atan2f(nose.x, nose.z);
        if (leave) {                                                       /* plain fall, no carried speed, facing = flight direction */
            p->ride = NULL; jumper_reset(&p->jumper); jumper_force_fall(&p->jumper, 1); p->on_ground = 0; p->lanim = -1; p->speed = 0; p->ramp_phase = 0; p->action_prev = in->action;
            p->floor_y = p->pos.y; player_apply_transform(p); puts("  PLAYER leaves the rocket");
        } else { p->on_ground = 0; p->inst->position = p->pos; p->inst->quat = p->ride_cur; mat4_from_trs(&p->inst->world, p->pos, p->ride_cur, p->inst->scale); }
        if (vm) eko_msgmask_clear(vm, p->inst->id, 0x200);
        player_volumes_y(p, vm, 20.0f); return;                            /* 0x462760(p, 20.0) */
    }
    if (!p->dead_kind && p->climb_sub) { climb_update(p, in, dt); if (p->climb_sub) p->on_ground = 0; player_apply_transform(p); if (vm) eko_msgmask_clear(vm, p->inst->id, 0x200); player_volumes(p, vm); return; }
    if (p->dead_kind) { p->climb_sub = 0; p->use_root = 0; }
    if (!p->dead_kind) { attack_update(p, in, dt); attack_trigger(p, in, dt); if (p->atk && climb_try(p)) { p->climb_act_prev = in->action; player_apply_transform(p); player_volumes(p, vm); return; } }
    /* Perso_Move 0x44bb20: no input (no walking, no jump) while locked or attacking */
    int allow = !(p->move_lock > 0 || p->atk != 0 || p->dead_kind);
    /* input direction relative to the camera */
    float ix = allow ? (float)(in->right - in->left) : 0, iz = allow ? (float)(in->forward - in->back) : 0;
    float len = sqrtf(ix * ix + iz * iz);
    if (len > 0) {
        ix /= len; iz /= len;
        float cs = cosf(cam_yaw), sn = sinf(cam_yaw);
        /* camera forward = (sin yaw, 0, cos yaw); camera right (right-handed, +x left when looking +z) = (-cos yaw, 0, sin yaw) */
        float wx = iz * sn + ix * -cs, wz = iz * cs + ix * sn;
        float want = atan2f(wx, wz);
        float d = want - p->yaw; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
        /* 0x45a4b0: turning slows down, target = (dot(new, old) + 1) / 2 * max; facing blends toward the wanted direction */
        p->ramp_target = (cosf(d) + 1.0f) * 0.5f * P_WALK_SPEED;
        p->yaw += d * (1.0f - powf(1.0f - P_TURN_BLEND, dt * P_REF_FPS));
        if (p->ramp_phase == 0 || p->ramp_phase == 3) { p->ramp_phase = 1; p->ramp_t = sqrtf(p->speed / P_WALK_SPEED) * P_ACC_TIME; }   /* 0x45ad30: 0/3 -> 1 */
    } else if (!allow) { p->ramp_phase = 0; p->speed = 0; }
    else if (p->ramp_phase == 1 || p->ramp_phase == 2) { p->ramp_phase = 3; p->ramp_t = 0; p->ramp_v0 = p->speed; }                  /* 2 -> 3 */
    /* Ramp tick (0x4671d0) */
    if (p->ramp_phase == 1) { p->ramp_t += dt; float k = p->ramp_t / P_ACC_TIME; if (k >= 1.0f) { k = 1.0f; p->ramp_phase = 2; } p->speed = k * k * p->ramp_target; }
    else if (p->ramp_phase == 2) p->speed = p->ramp_target;
    else if (p->ramp_phase == 3) { p->ramp_t += dt; float k = p->ramp_t / P_DEC_TIME; if (k >= 1.0f) { k = 1.0f; p->ramp_phase = 0; } p->speed = p->ramp_v0 * (1.0f - k * k); }
    else p->speed = 0;
    /* sliding, Mover RampB 0x45aa60: on ground steeper than n.y < 0.71 the player accelerates downhill to 600 u/s;
     * on flatter ground the slide decelerates. (The original keeps separate ramp phases; this is one linear ramp.) */
    {
        Vec3 n = p->ground_n; int steep = p->on_ground && n.y < P_SLIDE_NY;
        if (steep) {
            Vec3 up = { 0, 1, 0 }, dh = vcross(vcross(n, up), n); float l = sqrtf(vdot(dh, dh));
            if (l > 1e-5f) { if (dh.y > 0) l = -l; p->slide_dir = (Vec3){ dh.x / l, dh.y / l, dh.z / l }; }
            p->sliding = 1; p->slide_speed += P_SLIDE_SPEED / P_SLIDE_TIME * dt; if (p->slide_speed > P_SLIDE_SPEED) p->slide_speed = P_SLIDE_SPEED;
        } else if (p->slide_speed > 0) {
            if (p->on_ground) p->sliding = 0;
            p->slide_speed -= P_SLIDE_SPEED / P_SLIDE_TIME * dt; if (p->slide_speed < 0) p->slide_speed = 0;
        }
    }
    /* vertical motion comes from the Jumper; air control is the unchanged Mover (docs/PERSO_JUMP.md 1.4) */
    if (p->dead_kind == 7) jumper_reset(&p->jumper);                       /* 0x4649bf: the water death never falls further */
    if (p->nograv_t > 0) { p->nograv_t -= dt; p->jumper.dy = 0; if (p->dead_kind == 1 && p->nograv_t <= 0) p->dead_cam_req = 1; }
    else if (p->atk != 6 && p->atk != 7) jumper_update(&p->jumper, allow && in->jump, p->on_ground, p->pos.y - p->floor_y, dt);
    if (p->jumper.open_window) { p->jumper.open_window = 0; if (p->air_win < 0.5f) p->air_win = 0.5f; }
    /* displacement this frame: the attack's own, or Mover + Jumper; then disp.y += dt * (+0x244) */
    Vec3 disp = p->use_atk_disp ? p->atk_disp : (Vec3){ sinf(p->yaw) * p->speed * dt, p->jumper.dy, cosf(p->yaw) * p->speed * dt };
    if (p->push_t > 0 || p->push_speed > 0) {                              /* RampC 0x45acb0: 500 u/s, 0.1 s up, 0.5 s out */
        if (p->push_t > 0) { p->push_t -= dt; p->push_speed += 500.0f / 0.1f * dt; if (p->push_speed > 500.0f) p->push_speed = 500.0f; }
        else { p->push_speed -= 500.0f / 0.5f * dt; if (p->push_speed < 0) p->push_speed = 0; }
        if (!p->use_atk_disp) { disp.x += p->push_dir.x * p->push_speed * dt; disp.z += p->push_dir.z * p->push_speed * dt; }
    }
    if (!p->use_atk_disp && p->slide_speed > 0) { disp.x += p->slide_dir.x * p->slide_speed * dt; disp.z += p->slide_dir.z * p->slide_speed * dt; }
    disp.y += dt * p->vy_corr;
    p->vel = (Vec3){ disp.x / dt, disp.y / dt, disp.z / dt };
    if (disp.y < 0) p->jumper.fallen -= disp.y;                                      /* 0x44b914: fallen height accumulates */

    /* Perso_MoveCollide 0x4624f0 -> SweepCylinder 0x437180 (docs/PERSO_MOVE.md 6.1/6.2): move in substeps of at
     * most 10 units; per substep one xz push-out vector (x0.9), landing clamp when falling, ground clinging when walking. */
    const Instance *hit_inst = NULL; const InsNode *hit_node = NULL; int found;
    Vec3 np;
    {
        const float half = P_BODY_H * 0.5f;
        Vec3 cur = { p->pos.x, p->pos.y + half, p->pos.z };
        Vec3 carry = attach_delta(p);                                 /* 0x436d20: the platform moved under the player */
        Vec3 d = { disp.x + carry.x, disp.y + carry.y, disp.z + carry.z };
        int mode = d.y > 0.1f ? 3 : (d.y < -0.1f ? 2 : 0);
        int n = (int)(floorf(sqrtf(vdot(d, d)) / P_SUBSTEP) + 1.5f);
        d.x /= n; d.y /= n; d.z /= n;
        float margin = 5.0f;
        /* the original probes from the body centre here; feet + 43 (as in the final ground test) keeps objects of up to
         * half the body height from counting as floor while the hull push-out is still approximate */
        #define SWEEP_PROBE(c) ((Vec3){ (c).x, (c).y - half + P_PROBE_Y, (c).z })
        float gy = world_ground(p, SWEEP_PROBE(cur), &found, &hit_inst, &hit_node);
        if (found && cur.y - half - 1.0f < gy) { margin = P_STEP + 1.0f; if (mode == 0) mode = 1; }
        for (; n > 0; n--) {
            cur.x += d.x; cur.y += d.y; cur.z += d.z;
            float feet = cur.y - half;
            Vec3 push = gel_push(p->gel, cur, P_RADIUS, feet + margin, feet + P_BODY_H);
            { Vec3 ip = ins_push(p->ins, p->inst, cur, P_RADIUS, feet + margin, feet + P_BODY_H); push.x += ip.x; push.z += ip.z; }
            cur.x += push.x * 0.9f; cur.z += push.z * 0.9f;
            gy = world_ground(p, SWEEP_PROBE(cur), &found, &hit_inst, &hit_node);
            if (!found) continue;
            if (mode == 2) { if (cur.y - half < gy) cur.y = gy + half; }
            else if (mode == 1) { float f = cur.y - half; if (f - gy < P_STEP && f > gy) cur.y = gy + half; }
        }
        np.x = cur.x; np.y = cur.y - half; np.z = cur.z;
    }
    /* ground probe from feet + 43 (0x436f00 -> GetHeight 0x435650): on the ground iff feet - ground < 1.0, which also
     * steps up onto floors up to 43 higher. Without any floor the original treats the probe point as ground; here the
     * player simply keeps falling and is held at the bottom of the world. */
    {
        Vec3 probe = { np.x, np.y + P_PROBE_Y, np.z };
        float gy = world_ground(p, probe, &found, &hit_inst, &hit_node);
        p->floor_is_hull = found && hit_node != NULL;
        if (found) p->floor_y = gy;
        if (found && np.y - gy < 1.0f) { p->on_ground = 1; np.y = gy; } else p->on_ground = 0;
        p->ground_n = p->on_ground ? g_ground_n : (Vec3){ 0, 1, 0 };            /* 0x45a110 -> Mover+0xd0 */
        p->ground_kind = found ? ground_type(p, g_ground_mat) : 0;              /* 0x4628e0 -> Perso+0x308 */
        attach_store(p, p->on_ground ? hit_inst : NULL, hit_node, np);  /* 0x436d80 / 0x436d10 */
    }
    if (np.y < p->gel->bbox[2] - 2000.0f) { np = p->pos; player_kill(p, 7); }     /* below the world: "disappear" death (the original leaves this to script volumes) */
    p->pos = np;
    player_apply_transform(p);
    /* animations, Perso_AnimState 0x463e60 (docs/PERSO_MOVE.md 4.3, PERSO_JUMP.md 4): attack sub-state first, then
     * ground by Mover phase (0x463f40) or air by Jumper state (0x4642f0). Idle variations 0x59/0x5a are not done. */
    {
        static const int atk_anim[12] = { -1, 0xb, 0xb, 0xc, 0xc, -1, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12 };
        int js = p->jumper.state, want = p->lanim; float rate = 1.0f;
        int landing = (p->lanim == 8 || p->lanim == 0xa) && p->lanim_sub == 0;
        if (p->dead_kind) {                                                /* 0x464790 */
            int k = p->dead_kind; want = p->lanim;
            if (k == 1) want = p->dead_T <= anim_len(p, 0x2f, 0) ? 0x2f : 9;
            else if (k == 2 || k == 9) want = 0x30;
            else if (k == 6) want = 0x2b; else if (k == 7) want = 0x2a; else if (k == 8) want = 0x2e;
            else if (p->dead_T <= dt) { p->dead_ground = p->on_ground; want = p->on_ground ? 0x26 : 0x25; }
            else if (p->dead_T >= anim_len(p, p->dead_ground ? 0x26 : 0x25, 0)) want = 0x29;
        }
        else if (p->hit_anim_t > 0) want = p->hit_anim;
        else if (p->atk) { if (atk_anim[p->atk] >= 0) want = atk_anim[p->atk]; }
        else if (js == 2) {
            if (p->ramp_phase == 1) want = 2;
            else if (p->ramp_phase == 2) { want = 3; rate = p->speed / P_WALK_SPEED; if (rate < 0.5f) rate = 0.5f; if (rate > 1.0f) rate = 1.0f; }   /* 0x436c20 */
            else if (p->ramp_phase == 0 && !landing) want = 0;
        }
        else if (js == 0 || js == 1) want = 4;
        else if (js == 7) want = 5;
        else if (js == 3 || js == 4) { if (p->jumper.fell_off) want = 7; else if (p->jumper.short_hop) want = 6; }
        else if (js == 5) want = 9;
        else if (js == 6) {
            if (p->jumper.hard_fall) { want = 0xa; lock_move(p, anim_len(p, 0xa, 0)); }     /* hard landing blocks movement */
            else if (p->ramp_phase != 2) want = 8;
            if (p->ground_kind == 2) game_land_dust((Vec3){ p->pos.x, p->pos.y + 30.0f, p->pos.z }, p->ground_n);   /* 0x464486: 0x476140(&pos + (0,30,0), &normal, 3, 0.25, 1.5) */
        }
        anim_request(p, want, rate);
        /* footsteps (docs/FOOTSTEPS.md): in the walk cycle (logical animation 3) 0x463f40 puts a foot down when the
         * fraction of the cycle passes 0.38 (0x4ab278) and 0.9 (0x4a94b8) and calls the effect 0x47cba0 for it. */
        {
            const Model *pm = p->inst->model; float u = -1.0f;
            if (p->lanim == 3 && p->on_ground && (uint32_t)p->inst->anim < pm->nanims) {
                float dur = pm->anims[p->inst->anim].duration_s;
                if (dur > 0) { u = fmodf(p->inst->anim_time / dur, 1.0f); if (u < 0) u += 1.0f; }
            }
            if (u >= 0 && p->step_u >= 0) {
                Vec3 f = { sinf(p->yaw), 0, cosf(p->yaw) };                     /* the walk direction is the facing (Mover+0x10) */
                int kind = p->ground_kind == 2 ? 3 : 2;                         /* 0x464231: dust ground gets kind 3 */
                if (phase_passed(p->step_u, u, 0.38f)) game_footstep(p->pos, p->ground_n, f, 0, kind);
                if (phase_passed(p->step_u, u, 0.90f)) game_footstep(p->pos, p->ground_n, f, 1, kind);
            }
            p->step_u = u;
        }
    }

    /* world_collision events: standing on a press node with type code 1 (0x436f00, docs/EVENTS.md 3.2/3.3).
     * Moving from collision A straight onto B sends only Press(B), as in the original. */
    if (vm) {
        uint32_t col = 0xffffffffu;
        if (p->on_ground && hit_node && hit_node->kind == 1 && hit_node->type_code == 1) {
            const Model *hm = hit_inst->model; uint32_t k = hm->nvolume_nodes + hit_node->sub_index;
            if (hm->ncollision_ids && k < hit_inst->nids) col = hit_inst->ids[k];
        }
        if (col != 0xffffffffu) {
            if (p->cur_col == col) eko_col_perso_in(vm, col, p->inst->id);
            else { eko_col_perso_press(vm, col, p->inst->id); p->cur_col = col; p->events_sent++; printf("  COL press 0x%x (inst %u)\n", col, hit_inst->index); }
        } else if (p->cur_col != 0xffffffffu) {
            eko_col_perso_unpress(vm, p->cur_col, p->inst->id); p->events_sent++; printf("  COL unpress 0x%x\n", p->cur_col); p->cur_col = 0xffffffffu;
        }
        /* msgmask 0x200 = "player stands on the ground" (0x44b89c / 0x44b8bf) */
        if (p->on_ground) eko_msgmask_set(vm, p->inst->id, 0x200); else eko_msgmask_clear(vm, p->inst->id, 0x200);
    }

    player_volumes(p, vm);
}

/* ---- follow camera, mode 1 (docs/CAMERA.md 0.1 / 3: 0x424760 -> 0x422790 -> 0x4231e0) ------------------- */
static void camera_step(Player *p, float dt, int behind, int quick, int collide)
{
    Vec3 look = { sinf(p->yaw), 0, cosf(p->yaw) };
    Vec3 T = { p->pos.x, p->pos.y + CAM_TARGET_Y, p->pos.z }, L = { p->pos.x, p->pos.y + CAM_LOOK_Y, p->pos.z };
    Vec3 P = p->cam_pos, mv = { 0, 0, 0 };
    int js = p->jumper.state, rising = js == 0 || js == 1 || js == 7, falling = js == 3 || js == 4;
    if (behind) {
        float k = dt * (quick ? 7.0f : 3.0f);
        mv.x = (T.x - look.x * CAM_DIST_MAX - P.x) * k; mv.z = (T.z - look.z * CAM_DIST_MAX - P.z) * k;
    } else {
        float vx = T.x - P.x, vz = T.z - P.z, d = sqrtf(vx * vx + vz * vz);
        if (d > 1e-3f) {
            vx /= d; vz /= d;
            float lx = L.x - P.x, lz = L.z - P.z, ll = sqrtf(lx * lx + lz * lz);
            float k = (ll > 1e-3f && (lx * -look.x + lz * -look.z) / ll > 0.7f) ? 10.0f : 1.0f;   /* player walks toward the camera */
            if (d < CAM_DIST_MIN) { float st = (CAM_DIST_MAX / d) * dt * k * 66.6667f; if (d + st > CAM_DIST_MIN) st = CAM_DIST_MIN - d; mv.x = -vx * st; mv.z = -vz * st; }
            else if (d > CAM_DIST_MAX) { float st = (d / CAM_DIST_MAX) * dt * 433.333f; if (d - st < CAM_DIST_MAX) st = d - CAM_DIST_MAX; mv.x = vx * st; mv.z = vz * st; }
        }
    }
    if (rising) mv.y = (P.y - T.y < 300.0f) ? T.y - p->cam_tprev.y : 0;
    else mv.y = (T.y + CAM_HEIGHT - P.y) * 6.0f * dt;
    Vec3 N = { P.x + mv.x, P.y + mv.y, P.z + mv.z };
    if (collide) {
        /* sphere r = 40 pushed out of walls (0x422e30 -> 0x439c50, here one push per frame), then the veto: a step
         * after which the camera no longer sees T is refused. The breadcrumb path (0x423ab0) is not ported. */
        Vec3 push = gel_push(p->gel, N, CAM_RADIUS, N.y - CAM_RADIUS, N.y + CAM_RADIUS);
        N.x += push.x; N.z += push.z;
        { Vec3 ip = ins_push(p->ins, p->inst, N, CAM_RADIUS, N.y - CAM_RADIUS, N.y + CAM_RADIUS); N.x += ip.x; N.z += ip.z; }
        if (gel_ray_blocked(p->gel, N, T) && !gel_ray_blocked(p->gel, P, T)) N = P;
    }
    p->cam_pos = N; p->cam_tprev = T;
    if (rising || falling) { p->cam_drop += 450.0f * dt; if (p->cam_drop > 150.0f) p->cam_drop = 150.0f; }
    else p->cam_drop *= powf(0.94f, dt * P_REF_FPS);
}

/* SetMode(0, 0) = message 500 "camera behind the player" (0x41e450 -> 0x4247f0): P = pos - look + (0,100,0), then
 * one step of 0.1 s and 100 of 0.04 s in behind mode, so the camera settles on its resting point straight away. */
void player_camera_reset(Player *p)
{
    p->cam_init = 1; p->cam_tprev = (Vec3){ p->pos.x, p->pos.y + CAM_TARGET_Y, p->pos.z };
    p->cam_pos = (Vec3){ p->pos.x - sinf(p->yaw), p->pos.y + 100.0f, p->pos.z - cosf(p->yaw) };
    camera_step(p, 0.1f, 1, 0, 1); for (int i = 0; i < 100; i++) camera_step(p, 0.04f, 1, 0, 1);   /* Center_Step collides during the pre-simulation too */
}

void player_camera(Player *p, FreeCamera *cam, float dt, int behind_key)
{
    if (!p->cam_init) player_camera_reset(p);
    /* action 0xa: a tap pulls the camera behind the player for 0.5 s at 7*dt, holding it at 3*dt */
    if (behind_key && !p->cam_behind_prev) p->cam_quick_t = 0.5f;
    p->cam_behind_prev = behind_key; if (p->cam_quick_t > 0) p->cam_quick_t -= dt;
    /* 0x4591ec: Perso states 1, 4 (climbing) and 8 force behind mode at the slow rate; the target is the root position during the climb-over */
    Vec3 keep = p->pos; if (p->use_root) p->pos = p->root_pos;
    camera_step(p, dt, behind_key || p->cam_quick_t > 0 || p->climb_sub || p->ride, p->cam_quick_t > 0, !p->ride);   /* port choice: no camera collision during the ride, the rocket flies through walls and the veto would strand the camera */
    cam->pos = p->cam_pos; cam->fov_deg = CAM_FOV_Y;
    Vec3 to = { p->pos.x - cam->pos.x, p->pos.y + CAM_LOOK_Y - p->cam_drop - cam->pos.y, p->pos.z - cam->pos.z };
    float h = sqrtf(to.x * to.x + to.z * to.z);
    cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, h); p->cam_yaw = cam->yaw;
    p->pos = keep;
}

/* scripted Perso action (message 1040, 0x44dda0): only the effect on control is ported - the running attack is dropped
 * and the player stands still for t seconds (17 = walk into a door: the level change follows) */
static Quat q_unit(Quat q) { float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (n < 1e-9f) return (Quat){ 0, 0, 0, 1 }; return (Quat){ q.x / n, q.y / n, q.z / n, q.w / n }; }
Quat q_slerp(Quat a, Quat b, float u)
{
    a = q_unit(a); b = q_unit(b); float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { d = -d; b = (Quat){ -b.x, -b.y, -b.z, -b.w }; }
    float ka = 1 - u, kb = u; if (d < 0.9995f) { float th = acosf(d), s = sinf(th); ka = sinf((1 - u) * th) / s; kb = sinf(u * th) / s; }
    return q_unit((Quat){ a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb });
}
int player_mount(Player *p, Instance *obj)                              /* 0x465740 */
{
    if (!player_state_free(p) || !p->on_ground) return 0;               /* 0x465740: only in state 0 and on the ground */
    p->ride = obj; p->ride_state = 1; p->atk = 0; p->charge = 0; p->use_atk_disp = 0; p->has_target = 0; p->speed = 0; p->ramp_phase = 0; p->push_t = 0; p->att_inst = NULL;
    p->ride_p0 = p->pos; p->ride_q0 = p->ride_cur = p->inst->quat; p->ride_t = 0.7f; p->ride_jprev = p->ride_aprev = 1; p->lanim = -1;
    printf("  PLAYER mounts instance %u", obj->index), puts("");
    return 1;
}
void player_script_action(Player *p, int act, int have, Vec3 p0, Vec3 dir)
{
    if (p->dead_kind) return;                                       /* 0x44dda0 refuses state 2 (dead) */
    int lg = log_from_raw(act);                                     /* the action number is the raw .ins animation (docs/CINEMATIC.md 6) */
    if (lg < 0) { printf("  scripted action %d: no logical record, standing still", act), puts(""); player_script_hold(p, 2.0f); return; }
    p->atk = 0; p->charge = 0; p->use_atk_disp = 0; p->climb_sub = 0; p->use_root = 0; p->speed = 0; p->ramp_phase = 0; p->push_t = 0; p->push_speed = 0; p->slide_speed = 0;
    if (have) { p->pos = p0; if (dir.x * dir.x + dir.z * dir.z > 1e-6f) p->yaw = atan2f(dir.x, dir.z); }   /* on P0 of the door vector (typecode 5), facing P1; no ground snap */
    jumper_reset(&p->jumper); p->on_ground = 1; p->floor_y = p->pos.y;
    p->script_act = act; p->script_log = lg; p->lanim = -1; anim_request(p, lg, 1.0f); p->script_total = p->script_t = anim_len(p, lg, 0); p->script_faded = 0;
    if (act == 18) p->fade_req = 2;   /* fade in 0.5 s on the first frame (0x44dc2b). The camera is NOT cut here: the tail of 0x44dda0
                                       * puts it on the animation's own camera track (message 1040 in main_engine.c) and a cut back to the
                                       * follow camera would undo that one frame later. Coming out of the door ends with 0x44e5a0, a 0.5 s
                                       * blend back to it - that is cam_end_req, raised when the action runs out. */
    player_apply_transform(p);
}
void player_teleport(Player *p, Vec3 pos, int have_dir, Vec3 dir)       /* 0x44ce11 -> 0x44a650: SetPos + ground snap 0x462990, anim controllers reset, camera cut 0x458f90 */
{
    for (uint32_t v = 0; v < p->nvol; v++) p->inside[v] = 0;
    if (!p->script_act) {                                               /* 0x44a650 does nothing in state 5 */
        int found; float gy = player_ground_query(p, p->inst, (Vec3){ pos.x, pos.y + P_PROBE_Y, pos.z }, &found);
        if (found) pos.y = gy;
        p->pos = pos; if (have_dir && dir.x * dir.x + dir.z * dir.z > 1e-6f) p->yaw = atan2f(dir.x, dir.z);
        p->vel = (Vec3){ 0, 0, 0 }; p->speed = 0; p->ramp_phase = 0; p->floor_y = pos.y; p->att_inst = NULL; p->lanim = -1; p->step_u = -1.0f;
        jumper_reset(&p->jumper); p->on_ground = 1; player_apply_transform(p);
    }
    /* 0x458f90 (the camera cut that sits outside that test) is in the message-26 handler: it has to run in script order, because
     * what the rest of the tick does with the camera has to win over it. The follow camera still seats itself one frame later,
     * on the position and facing a door action gives him (mode 1 of message 26 carries no direction), because SetMode(0, 0)
     * only clears cam_init and player_camera() re-seats on the next camera update - which runs after the tick. */
}
void player_script_hold(Player *p, float t) { p->atk = 0; p->charge = 0; p->use_atk_disp = 0; lock_move(p, t); }

void player_free(Player *p) { free(p->inside); free(p->vol_inst); free(p->vol_node); free(p->vol_id); }
