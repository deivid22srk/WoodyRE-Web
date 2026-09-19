/* player.c - provisional player controller (see player.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "player.h"

/* ---- tunables (guesses, world units; to be replaced by the decompiled Perso constants) ---- */
#define P_WALK_SPEED   700.0f
#define P_TURN_RATE    12.0f      /* rad/s toward the input direction */
#define P_GRAVITY      2400.0f
#define P_JUMP_SPEED   900.0f
#define P_STEP_UP      60.0f      /* max step height */
#define P_RADIUS       40.0f      /* body radius for wall collision */
#define P_HEIGHT       260.0f
#define P_VOL_PROBE_Y  80.0f      /* height above the feet used as the volume test point */
#define CAM_DIST       650.0f
#define CAM_HEIGHT     260.0f
#define CAM_TARGET_Y   150.0f
#define CAM_SMOOTH     6.0f

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

/* push the body (sphere at feet+height/2, radius r) out of wall polygons */
static Vec3 gel_slide(const GelFile *g, Vec3 pos, float r)
{
    for (int iter = 0; iter < 3; iter++) {
        int moved = 0;
        Vec3 c = { pos.x, pos.y + P_HEIGHT * 0.5f, pos.z };
        for (uint32_t i = 0; i < g->npolys; i++) {
            const GelPoly *pl = &g->polys[i];
            if (fabsf(pl->plane[1]) > 0.5f || pl->nverts < 3) continue;   /* walls only */
            float d = pl->plane[0] * c.x + pl->plane[1] * c.y + pl->plane[2] * c.z + pl->plane[3];
            if (d >= r || d <= -r) continue;
            /* vertical extent of the polygon must overlap the body */
            float ymin = 1e30f, ymax = -1e30f;
            for (uint32_t k = 0; k < pl->nverts; k++) { float y = g->verts[pl->indices[k]].y; if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
            if (ymax < pos.y + P_STEP_UP || ymin > pos.y + P_HEIGHT) continue;
            Vec3 q = { c.x - pl->plane[0] * d, c.y - pl->plane[1] * d, c.z - pl->plane[2] * d };
            if (!poly_contains(g, pl, q)) continue;
            float push = (d >= 0 ? r - d : -r - d);
            pos.x += pl->plane[0] * push; pos.z += pl->plane[2] * push; c.x = pos.x; c.z = pos.z; moved = 1;
        }
        if (!moved) break;
    }
    return pos;
}

/* floor provided by the collision hulls (kind 4 nodes) of visible instances: highest upward-facing
 * hull polygon under p within [p.y - max_drop, p.y + step_up]. */
static int point_in_tri_xz(Vec3 a, Vec3 b, Vec3 c, Vec3 q)
{
    float d1 = (b.x - a.x) * (q.z - a.z) - (b.z - a.z) * (q.x - a.x);
    float d2 = (c.x - b.x) * (q.z - b.z) - (c.z - b.z) * (q.x - b.x);
    float d3 = (a.x - c.x) * (q.z - c.z) - (a.z - c.z) * (q.x - c.x);
    return (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
}
static float ins_floor_below(const InsFile *ins, Vec3 p, float step_up, float max_drop, int *found, const Instance *skip)
{
    float best = -1e30f; *found = 0;
    for (uint32_t mi = 0; mi < ins->nmodels; mi++) {
        const Model *m = &ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            const Instance *in = &m->instances[k]; if (!in->visible || in == skip) continue;
            /* cheap reject: instance origin far away horizontally */
            float dx = in->position.x - p.x, dz = in->position.z - p.z; if (dx * dx + dz * dz > 4000.0f * 4000.0f) continue;
            for (uint32_t ni = 0; ni < m->nnodes; ni++) {
                const InsNode *n = &m->nodes[ni]; if (n->kind != 4 || !n->polys) continue;
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
                        best = y; *found = 1;
                    }
                }
            }
        }
    }
    return best;
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
    p->pos = p->inst->position;
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
        float maxturn = P_TURN_RATE * dt; if (d > maxturn) d = maxturn; if (d < -maxturn) d = -maxturn;
        p->yaw += d;
        p->vel.x = sinf(p->yaw) * P_WALK_SPEED; p->vel.z = cosf(p->yaw) * P_WALK_SPEED;
    } else { p->vel.x = 0; p->vel.z = 0; }
    if (in->jump && p->on_ground) { p->vel.y = P_JUMP_SPEED; p->on_ground = 0; }
    p->vel.y -= P_GRAVITY * dt;
    if (p->vel.y < -6000) p->vel.y = -6000;

    Vec3 np = { p->pos.x + p->vel.x * dt, p->pos.y + p->vel.y * dt, p->pos.z + p->vel.z * dt };
    np = gel_slide(p->gel, np, P_RADIUS);
    int found, found2; float drop = p->on_ground ? P_STEP_UP + 20.0f : 1e9f;
    float fy = gel_floor_below(p->gel, (Vec3){ np.x, p->pos.y, np.z }, P_STEP_UP, drop, &found);
    float fy2 = ins_floor_below(p->ins, (Vec3){ np.x, p->pos.y, np.z }, P_STEP_UP, drop, &found2, p->inst);
    if (found2 && (!found || fy2 > fy)) { fy = fy2; found = 1; p->floor_is_hull = 1; } else if (found) p->floor_is_hull = 0;
    if (found) p->floor_y = fy;
    if (found && np.y <= fy + 0.01f && p->vel.y <= 0) { np.y = fy; p->vel.y = 0; p->on_ground = 1; }
    else if (found && p->on_ground && np.y <= fy + P_STEP_UP) { np.y = fy; p->vel.y = 0; }
    else p->on_ground = 0;
    if (np.y < p->gel->bbox[2] - 2000.0f) { np = p->inst->position; np = p->pos; np.y += 10; p->vel.y = 0; }   /* fell out of the world: hold */
    p->pos = np;
    player_apply_transform(p);

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

void player_camera(Player *p, FreeCamera *cam, float dt)
{
    Vec3 target = { p->pos.x, p->pos.y + CAM_TARGET_Y, p->pos.z };
    if (!p->cam_init) { p->cam_yaw = p->yaw; p->cam_init = 1; p->cam_pos.x = target.x - sinf(p->cam_yaw) * CAM_DIST; p->cam_pos.z = target.z - cosf(p->cam_yaw) * CAM_DIST; p->cam_pos.y = target.y + CAM_HEIGHT; }
    /* the camera drifts behind the player's facing direction */
    float d = p->yaw - p->cam_yaw; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f;
    p->cam_yaw += d * fminf(1.0f, 2.0f * dt);
    Vec3 want = { target.x - sinf(p->cam_yaw) * CAM_DIST, target.y + CAM_HEIGHT, target.z - cosf(p->cam_yaw) * CAM_DIST };
    float k = fminf(1.0f, CAM_SMOOTH * dt);
    p->cam_pos.x += (want.x - p->cam_pos.x) * k; p->cam_pos.y += (want.y - p->cam_pos.y) * k; p->cam_pos.z += (want.z - p->cam_pos.z) * k;
    cam->pos = p->cam_pos;
    Vec3 to = vsub(target, cam->pos); float h = sqrtf(to.x * to.x + to.z * to.z);
    cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, h);
}

void player_free(Player *p) { free(p->inside); free(p->vol_inst); free(p->vol_node); free(p->vol_id); }
