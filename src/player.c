/* player.c - provisional player controller (see player.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "player.h"

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
/* ---- remaining guesses ---- */
#define P_STEP         40.0f      /* 0x437180 step argument: clinging distance, wall test skips the lowest step+1 */
#define P_SUBSTEP      10.0f      /* 0x437180 substep length */
#define P_BODY_H       193.0f     /* P+0x0c: body height (61 when ducking) */
#define P_PROBE_Y      43.0f      /* P+0x00: ground probe / collision centre above the feet */
#define P_STEP_UP      60.0f      /* (old provisional floor search) */
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

float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found)
{
    float best = -1e30f; *found = 0;
    for (uint32_t i = 0; i < g->npolys; i++) {
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
static float ins_floor_below(const InsFile *ins, Vec3 p, float step_up, float max_drop, int *found, const Instance *skip,
                             const Instance **hit_inst, const InsNode **hit_node)
{
    float best = -1e30f; *found = 0; *hit_inst = NULL; *hit_node = NULL;
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) {
        const Model *m = &ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            const Instance *in = &m->instances[k]; if (!in->visible || in == skip) continue;
            /* cheap reject: instance origin far away horizontally */
            float dx = in->position.x - p.x, dz = in->position.z - p.z; if (dx * dx + dz * dz > 4000.0f * 4000.0f) continue;
            for (uint32_t ni = 0; ni < m->nnodes; ni++) {
                const InsNode *n = &m->nodes[ni]; if ((n->kind != 4 && n->kind != 1) || !n->polys) continue;
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
                    }
                }
            }
        }
    }
    return best;
}

/* One push-out vector for the body cylinder (radius r, vertical band [lo, hi]) against the world, after 0x407000 /
 * 0x408600: front side only, penetration r - dist along the polygon normal in xz, accumulated per axis as a positive
 * maximum plus a negative minimum. Simplifications: no band clipping / edge-circle intersection and no conical bottom
 * (the contact point is the centre projected onto the plane), walkable polygons (n.y > 0.71) are left to the floor code,
 * instance hulls are not tested yet. */
static Vec3 gel_push(const GelFile *g, Vec3 c, float r, float lo, float hi)
{
    float px = 0, nx = 0, pz = 0, nz = 0;
    for (uint32_t i = 0; i < g->npolys; i++) {
        const GelPoly *pl = &g->polys[i];
        if (pl->nverts < 3 || pl->plane[1] > 0.71f) continue;
        float ymin = 1e30f, ymax = -1e30f;
        for (uint32_t k = 0; k < pl->nverts; k++) { float y = g->verts[pl->indices[k]].y; if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
        if (ymax < lo || ymin > hi) continue;
        Vec3 q = c; float a = lo > ymin ? lo : ymin, b = hi < ymax ? hi : ymax; if (q.y < a) q.y = a; if (q.y > b) q.y = b;
        float d = pl->plane[0] * q.x + pl->plane[1] * q.y + pl->plane[2] * q.z + pl->plane[3];
        if (d < 0 || d >= r) continue;
        Vec3 on = { q.x - pl->plane[0] * d, q.y - pl->plane[1] * d, q.z - pl->plane[2] * d };
        if (!poly_contains(g, pl, on)) continue;
        float pen = r - d, vx = pl->plane[0] * pen, vz = pl->plane[2] * pen;
        if (vx > px) px = vx; if (vx < nx) nx = vx; if (vz > pz) pz = vz; if (vz < nz) nz = vz;
    }
    Vec3 out = { px + nx, 0, pz + nz }; return out;
}

/* GetHeight 0x435650 -> 0x498520: nearest surface below the point: world polygons with n.y > 1e-5 (any slope) that
 * contain the point in xz, and the press / hull nodes of instances (hit_node != NULL then). */
static float world_ground(const Player *p, Vec3 pt, int *found, const Instance **hit_inst, const InsNode **hit_node)
{
    const GelFile *g = p->gel; float best = 1e30f; int f1 = 0, f2;
    for (uint32_t i = 0; i < g->npolys; i++) {
        const GelPoly *pl = &g->polys[i];
        if (pl->nverts < 3 || pl->plane[1] <= 1e-5f) continue;
        float dist = pl->plane[0] * pt.x + pl->plane[1] * pt.y + pl->plane[2] * pt.z + pl->plane[3];
        if (dist <= 0 || dist / pl->plane[1] >= best) continue;
        Vec3 q = { pt.x, pt.y - dist / pl->plane[1], pt.z };
        if (poly_contains(g, pl, q)) { best = dist / pl->plane[1]; f1 = 1; }
    }
    float y1 = pt.y - best;
    float y2 = ins_floor_below(p->ins, pt, 0.0f, 1e9f, &f2, p->inst, hit_inst, hit_node);
    if (f2 && (!f1 || y2 > y1)) { *found = 1; return y2; }
    *hit_inst = NULL; *hit_node = NULL; *found = f1; return f1 ? y1 : pt.y;
}

/* ---- Jumper (0x462d70 update, 0x462fd0 tick) ------------------------------------ */
static void jumper_start_jump(Jumper *j) { j->D = J_P68 * 0.75f; j->t = (j->D / J_V) * -0.5f; j->h_prev = 0; j->v_down = 0; j->short_hop = j->fell_off = 0; }   /* 0x462d10 */
static void jumper_start_fall(Jumper *j, int fell_off) { j->D = J_P68 * 1.25f; j->fell_off = fell_off; j->h_prev = J_HEIGHT; j->t = 0; }                       /* 0x462d40 */
static int jumper_tick(Jumper *j, float dt, int on_ground)
{
    j->t += dt;
    float u = j->t / (0.5f * j->D / J_V), h = J_HEIGHT - J_HEIGHT * u * u;
    if (j->t >= J_APEX_T && j->state == 1 && !j->short_hop) j->state = 7;
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
static int volume_contains(const Instance *inst, uint32_t node, Vec3 p)
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

int player_init(Player *p, InsFile *ins, const GelFile *gel)
{
    memset(p, 0, sizeof *p);
    if (!ins->nmodels || !ins->models[0].ninstances) return -1;
    p->inst = &ins->models[0].instances[0]; p->gel = gel; p->ins = ins;
    p->pos = p->inst->position; p->cur_col = 0xffffffffu; p->floor_y = p->pos.y;
    p->jumper.state = 2; p->jumper.armed = 1;                      /* 0x462c90 reset */
    /* facing from the instance quaternion: rotation about y composed with the -90 deg x model rotation */
    Vec3 fwd = mat4_apply(&p->inst->world, (Vec3){ 0, -1, 0 }); fwd = vsub(fwd, p->inst->position);
    p->yaw = atan2f(fwd.x, fwd.z);
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
    printf("player: start (%.0f %.0f %.0f) yaw %.1f deg, %u trigger volumes\n", p->pos.x, p->pos.y, p->pos.z, p->yaw * 57.2958f, p->nvol);
    return 0;
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

void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw)
{
    if (dt <= 0) return;
    /* input direction relative to the camera */
    float ix = (float)(in->right - in->left), iz = (float)(in->forward - in->back);
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
    } else if (p->ramp_phase == 1 || p->ramp_phase == 2) { p->ramp_phase = 3; p->ramp_t = 0; p->ramp_v0 = p->speed; }                  /* 2 -> 3 */
    /* Ramp tick (0x4671d0) */
    if (p->ramp_phase == 1) { p->ramp_t += dt; float k = p->ramp_t / P_ACC_TIME; if (k >= 1.0f) { k = 1.0f; p->ramp_phase = 2; } p->speed = k * k * p->ramp_target; }
    else if (p->ramp_phase == 2) p->speed = p->ramp_target;
    else if (p->ramp_phase == 3) { p->ramp_t += dt; float k = p->ramp_t / P_DEC_TIME; if (k >= 1.0f) { k = 1.0f; p->ramp_phase = 0; } p->speed = p->ramp_v0 * (1.0f - k * k); }
    else p->speed = 0;
    p->vel.x = sinf(p->yaw) * p->speed; p->vel.z = cosf(p->yaw) * p->speed;
    /* vertical motion comes from the Jumper; air control is the unchanged Mover (docs/PERSO_JUMP.md 1.4) */
    jumper_update(&p->jumper, in->jump, p->on_ground, p->pos.y - p->floor_y, dt);
    p->vel.y = p->jumper.dy / dt;
    if (p->jumper.dy < 0) p->jumper.fallen -= p->jumper.dy;                          /* 0x44b914: fallen height accumulates */

    /* Perso_MoveCollide 0x4624f0 -> SweepCylinder 0x437180 (docs/PERSO_MOVE.md 6.1/6.2): move in substeps of at
     * most 10 units; per substep one xz push-out vector (x0.9), landing clamp when falling, ground clinging when walking. */
    const Instance *hit_inst = NULL; const InsNode *hit_node = NULL; int found;
    Vec3 np;
    {
        const float half = P_BODY_H * 0.5f;
        Vec3 cur = { p->pos.x, p->pos.y + half, p->pos.z };
        Vec3 d = { p->vel.x * dt, p->jumper.dy, p->vel.z * dt };
        int mode = d.y > 0.1f ? 3 : (d.y < -0.1f ? 2 : 0);
        int n = (int)(floorf(sqrtf(vdot(d, d)) / P_SUBSTEP) + 1.5f);
        d.x /= n; d.y /= n; d.z /= n;
        float margin = 5.0f;
        float gy = world_ground(p, cur, &found, &hit_inst, &hit_node);
        if (found && cur.y - half - 1.0f < gy) { margin = P_STEP + 1.0f; if (mode == 0) mode = 1; }
        for (; n > 0; n--) {
            cur.x += d.x; cur.y += d.y; cur.z += d.z;
            float feet = cur.y - half;
            Vec3 push = gel_push(p->gel, cur, P_RADIUS, feet + margin, feet + P_BODY_H);
            cur.x += push.x * 0.9f; cur.z += push.z * 0.9f;
            gy = world_ground(p, cur, &found, &hit_inst, &hit_node);
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
    }
    if (np.y < p->gel->bbox[2] - 2000.0f) { np = p->pos; p->jumper.state = 2; }   /* fell out of the world: hold */
    p->pos = np;
    player_apply_transform(p);
    /* animations per state, Perso_AnimState 0x463e60 (docs/PERSO_MOVE.md 4.3): ground by Mover phase (0x463f40),
     * air by Jumper state (0x4642f0). Idle variations 0x59/0x5a after 10 s are not done. */
    {
        Instance *wi = p->inst; int cur = wi->anim, want = cur; int js = p->jumper.state; float spd = 1.0f;
        int landing = (cur == 8 || cur == 10) && wi->anim_time < wi->model->anims[cur].duration_s;
        if (js == 2) {
            if (p->ramp_phase == 1) want = 2;                                        /* start walking */
            else if (p->ramp_phase == 2) { want = 3; spd = p->speed / P_WALK_SPEED; if (spd < 0.5f) spd = 0.5f; if (spd > 1.0f) spd = 1.0f; }   /* walk cycle, 0x436c20 */
            else if (p->ramp_phase == 0 && !landing) want = 0;                       /* idle */
        }
        else if (js == 0 || js == 1) want = 4;                                       /* rising */
        else if (js == 7) want = 5;                                                  /* apex */
        else if (js == 3) { if (p->jumper.fell_off) want = 7; else if (p->jumper.short_hop) want = 6; }
        else if (js == 5) want = 9;                                                  /* long fall */
        else if (js == 6) want = p->jumper.hard_fall ? 10 : (p->ramp_phase == 2 ? cur : 8);   /* landing / hard landing */
        if ((uint32_t)want < wi->model->nanims) { if (want != cur) { wi->anim = want; wi->anim_time = 0; } wi->anim_speed = spd; }
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

    /* trigger volumes: enter / in / leave -> script VM (player = "perso" variants) */
    if (vm) {
        Vec3 probe = { p->pos.x, p->pos.y + P_VOL_PROBE_Y, p->pos.z };
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

/* ---- follow camera, mode 1 (docs/CAMERA.md 0.1 / 3: 0x424760 -> 0x422790 -> 0x4231e0) ------------------- */
/* segment a->b blocked by a world polygon? (line-of-sight veto 0x423a40; instances are not tested yet) */
static int gel_ray_blocked(const GelFile *g, Vec3 a, Vec3 b)
{
    for (uint32_t i = 0; i < g->npolys; i++) {
        const GelPoly *pl = &g->polys[i]; if (pl->nverts < 3) continue;
        float da = pl->plane[0] * a.x + pl->plane[1] * a.y + pl->plane[2] * a.z + pl->plane[3];
        float db = pl->plane[0] * b.x + pl->plane[1] * b.y + pl->plane[2] * b.z + pl->plane[3];
        if ((da > 0) == (db > 0)) continue;
        float t = da / (da - db); Vec3 q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
        if (poly_contains(g, pl, q)) return 1;
    }
    return 0;
}

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
        if (gel_ray_blocked(p->gel, N, T) && !gel_ray_blocked(p->gel, P, T)) N = P;
    }
    p->cam_pos = N; p->cam_tprev = T;
    if (rising || falling) { p->cam_drop += 450.0f * dt; if (p->cam_drop > 150.0f) p->cam_drop = 150.0f; }
    else p->cam_drop *= powf(0.94f, dt * P_REF_FPS);
}

void player_camera(Player *p, FreeCamera *cam, float dt, int behind_key)
{
    if (!p->cam_init) {
        /* message 500 "camera behind the player": P = pos - look + (0,100,0), one step of 0.1 s and 100 of 0.04 s in behind mode */
        p->cam_init = 1; p->cam_tprev = (Vec3){ p->pos.x, p->pos.y + CAM_TARGET_Y, p->pos.z };
        p->cam_pos = (Vec3){ p->pos.x - sinf(p->yaw), p->pos.y + 100.0f, p->pos.z - cosf(p->yaw) };
        camera_step(p, 0.1f, 1, 0, 0); for (int i = 0; i < 100; i++) camera_step(p, 0.04f, 1, 0, 0);
    }
    /* action 0xa: a tap pulls the camera behind the player for 0.5 s at 7*dt, holding it at 3*dt */
    if (behind_key && !p->cam_behind_prev) p->cam_quick_t = 0.5f;
    p->cam_behind_prev = behind_key; if (p->cam_quick_t > 0) p->cam_quick_t -= dt;
    camera_step(p, dt, behind_key || p->cam_quick_t > 0, p->cam_quick_t > 0, 1);
    cam->pos = p->cam_pos; cam->fov_deg = CAM_FOV_Y;
    Vec3 to = { p->pos.x - cam->pos.x, p->pos.y + CAM_LOOK_Y - p->cam_drop - cam->pos.y, p->pos.z - cam->pos.z };
    float h = sqrtf(to.x * to.x + to.z * to.z);
    cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, h); p->cam_yaw = cam->yaw;
}

void player_free(Player *p) { free(p->inside); free(p->vol_inst); free(p->vol_node); free(p->vol_id); }
