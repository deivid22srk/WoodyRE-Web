/* enemy.c - enemy types 4/5/6 after docs/ENEMY.md (state machine 0x418cf0, behaviours 0x41b2c0..0x41cf00).
 * Simplifications: no obstacle sensor (16 directions), no actor avoidance, wander picks a random direction and the
 * idle variations all use one animation; movement is a straight step that is refused at ledges / steps over 10 units
 * (as the original's sweep does) instead of the full swept cylinder; the death particles are not spawned. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "enemy.h"
#include "player.h"

/* parameter block P (0x41d510), type 4 column */
#define E_RADIUS   30.0f      /* P+0x04 */
#define E_HEIGHT   140.0f     /* P+0x28 */
#define E_WALK     200.0f     /* P+0x08 */
#define E_RUN      600.0f     /* P+0x0c */
#define E_DASH     800.0f     /* P+0x5c */
#define E_SEE      1500.0f    /* P+0x20, and |dy| < 600 */
#define E_TURN     1.5708f    /* P+0x10 */
#define E_TURN_RUN 6.2832f    /* P+0x14 */
#define E_ACC      1000.0f
#define E_STEP     10.0f      /* P+0x30 / P+0x2c: max step up / drop */
#define E_KNOCK    600.0f     /* P+0x44 */
#define E_ACTIVE_D 3000.0f

/* logical animation -> main .ins animation and speed divisor (table 0x4b29b8) */
enum { EA_RUN, EA_WALK, EA_DASH, EA_BRAKE, EA_MISS, EA_WIN, EA_HIT, EA_DEAD, EA_IDLE, EA_TURN };
static const struct { int anim; float speed; int hold; } g_ea[] = {
    { 2, 3, 0 }, { 4, 3, 0 }, { 13, 10, 0 }, { 15, 2, 1 }, { 14, 3, 1 }, { 18, 1.5f, 1 }, { 11, 4, 1 }, { 12, 3, 1 }, { 6, 3, 0 }, { 16, 3, 0 } };
static float ea_len(const Enemy *e, int a) { const Model *m = e->inst->model; int s = g_ea[a].anim; return (uint32_t)s < m->nanims ? m->anims[s].duration_s / g_ea[a].speed : 0.5f; }
static void ea_play(Enemy *e, int a)
{
    Instance *in = e->inst; const Model *m = in->model; int s = g_ea[a].anim; if ((uint32_t)s >= m->nanims) return;
    if (in->anim != s) { in->anim = s; in->anim_time = 0; }
    in->anim_speed = g_ea[a].speed;
    if (g_ea[a].hold && in->anim_time > m->anims[s].duration_s * 0.999f) in->anim_time = m->anims[s].duration_s * 0.999f;
}

float enemy_radius(const Enemy *e) { (void)e; return E_RADIUS; }
float enemy_height(const Enemy *e) { (void)e; return E_HEIGHT; }

void enemies_add(EnemySet *s, Instance *inst, int type)
{
    if (s->n >= MAX_ENEMIES) return;
    for (int i = 0; i < s->n; i++) if (s->e[i].inst == inst) return;
    Enemy *e = &s->e[s->n++]; Enemy z = { 0 }; *e = z;
    e->inst = inst; e->type = type; e->pos = e->home = inst->position; e->hp = type == 6 ? 2.0f : 1.0f;
    e->cool = -1; e->attackable = 1; e->speed = e->want_speed = E_WALK; e->path_dir = 1; e->path_to = 1;
    e->st = inst->traj.npoints > 1 ? 0 : 8;
    if (e->st == 0) { Vec3 a = inst->traj.points[0], b = inst->traj.points[1]; e->ang = atan2f(b.z - a.z, b.x - a.x); }
    inst->scripted = 0;
    printf("  enemy type %d inst %u at %.0f %.0f %.0f, %u path points\n", type, inst->index, e->pos.x, e->pos.y, e->pos.z, inst->traj.npoints);
}

int enemy_take_damage(Enemy *e, float dmg, Vec3 dir)
{
    if (e->removed || e->st == 12 || e->hit_t > 0 || e->knock_t > 0) return 0;
    e->st = 9; e->hit_t = e->knock_t = 0.25f; e->knock_dir = dir; e->hp -= dmg;
    return e->hp <= 0;
}

static float ang_diff(float a, float b) { float d = a - b; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f; return d; }
static void steer(Enemy *e, float want, float rate, float dt) { float d = ang_diff(want, e->ang), m = rate * dt; if (d > m) d = m; if (d < -m) d = -m; e->ang += d; }

/* common move 0x41b2c0: a step is only taken when the ground there is within [-10, +10] of the feet */
static int enemy_move(Enemy *e, struct Player *pl, Vec3 delta)
{
    Vec3 to = { e->pos.x + delta.x, e->pos.y, e->pos.z + delta.z }; int found;
    float gy = player_ground_query(pl, e->inst, (Vec3){ to.x, to.y + E_HEIGHT * 0.5f, to.z }, &found);
    if (!found || gy - e->pos.y > E_STEP || e->pos.y - gy > E_STEP) return 0;
    e->pos.x = to.x; e->pos.z = to.z; return 1;
}

static void enemy_apply(Enemy *e)
{
    Instance *in = e->inst;
    /* model faces along (cos, 0, sin) of ang; same construction as the player: yaw about y composed with rotx(-90) */
    float yaw = atan2f(cosf(e->ang), sinf(e->ang)), c = cosf(yaw * 0.5f), s = sinf(yaw * 0.5f);
    in->position = e->pos;
    in->quat.x = 0.70710678f * c; in->quat.y = -0.70710678f * s; in->quat.z = -0.70710678f * s; in->quat.w = -0.70710678f * c;
    mat4_from_trs(&in->world, in->position, in->quat, in->scale);
}

static void enemy_update(Enemy *e, struct Player *pl, Vec3 cam, float dt)
{
    Instance *in = e->inst;
    if (e->removed || !in->visible) return;
    { float dx = e->pos.x - cam.x, dy = e->pos.y - cam.y, dz = e->pos.z - cam.z; if (dx * dx + dy * dy + dz * dz >= E_ACTIVE_D * E_ACTIVE_D && e->st != 12) return; }   /* Think 0x41a320 */
    if (e->cool >= 0) e->cool -= dt;
    if (e->hit_t > 0) e->hit_t -= dt;
    /* FindTarget 0x41af80: no view cone, no line of sight */
    Vec3 tp = pl->pos; float dx = tp.x - e->pos.x, dy = tp.y - e->pos.y, dz = tp.z - e->pos.z;
    int see = !pl->dead_kind && dx * dx + dy * dy + dz * dz < E_SEE * E_SEE && fabsf(dy) < 600.0f;
    float dxz = sqrtf(dx * dx + dz * dz), to_player = atan2f(dz, dx);
    Vec3 step = { 0, 0, 0 }; int anim = EA_IDLE;

    /* speed regulator H: accelerates with 1000 u/s^2 toward the wanted speed */
    if (e->speed < e->want_speed) { e->speed += E_ACC * dt; if (e->speed > e->want_speed) e->speed = e->want_speed; }
    else if (e->speed > e->want_speed) e->speed = e->want_speed;

    switch (e->st) {
    case 0: {                                                     /* patrol: slides exactly along the TRAJ segments, ping-pong on an open path */
        const Trajectory *T = &in->traj; Vec3 b = T->points[e->path_to];
        float bx = b.x - e->pos.x, bz = b.z - e->pos.z, l = sqrtf(bx * bx + bz * bz), mv = E_WALK * dt;
        if (l <= mv) {
            e->pos.x = b.x; e->pos.z = b.z;
            int nx = (int)e->path_to + e->path_dir;
            if (nx < 0 || nx >= (int)T->npoints) { if (T->closed) nx = nx < 0 ? (int)T->npoints - 1 : 0; else { e->path_dir = -e->path_dir; nx = (int)e->path_to + e->path_dir; } }
            e->path_to = (uint32_t)nx;
        } else { e->ang = atan2f(bz, bx); e->pos.x += bx / l * mv; e->pos.z += bz / l * mv; }
        anim = EA_WALK;
        if (e->cool < 0 && see) { e->home = e->pos; e->st = 2; }
        break; }
    case 8:                                                       /* wander: alternate an idle action and walking straight */
        e->wander_t -= dt;
        if (e->wander_t <= 0) {
            e->wander_walk = !e->wander_walk;
            if (e->wander_walk) { e->want_ang = (float)(rand() % 6283) * 0.001f; e->wander_t = 1.5f + (float)(rand() & 0x3ff) / 1024.0f; }
            else e->wander_t = ea_len(e, EA_IDLE);
        }
        { float hx = e->home.x - e->pos.x, hz = e->home.z - e->pos.z;                     /* leash: walk back home */
          if (in->traj.npoints > 1) { e->want_ang = atan2f(hz, hx); e->wander_walk = 1; if (hx * hx + hz * hz < 10.0f * 10.0f) { e->st = 0; break; } }
          else if (hx * hx + hz * hz > 500.0f * 500.0f) e->want_ang = atan2f(hz, hx); }
        if (e->wander_walk) {
            steer(e, e->want_ang, E_TURN, dt); e->want_speed = E_WALK; anim = EA_WALK;
            step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
            if (fabsf(ang_diff(e->want_ang, e->ang)) > 0.5f) { step = (Vec3){ 0, 0, 0 }; anim = EA_TURN; }
        }
        if (e->cool < 0 && see) e->st = 2;
        break;
    case 2:                                                       /* noticed: turn on the spot first, then run */
        if (!see) { e->st = 8; break; }
        e->turn_t = fabsf(ang_diff(to_player, e->ang)) / E_TURN_RUN; e->speed = e->want_speed = E_RUN; e->st = 1;
        /* fallthrough */
    case 1:
        if (!see) { e->want_speed = E_WALK; e->st = 8; break; }
        steer(e, to_player, E_TURN_RUN, dt);
        if ((e->turn_t -= dt) <= 0) { step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt }; anim = EA_RUN; } else anim = EA_TURN;
        e->atk_t = ea_len(e, EA_DASH) + 0.5f * ea_len(e, EA_MISS);
        if (dxz <= E_DASH * e->atk_t && dxz > 1e-3f && (cosf(e->ang) * dx + sinf(e->ang) * dz) / dxz > 0.95f) { e->speed = e->want_speed = E_DASH; e->st = 4; }
        break;
    case 4:                                                       /* dash: the only state that hurts the player */
        anim = EA_DASH;
        if (!see || e->atk_t <= 0) { e->t = ea_len(e, EA_BRAKE); e->st = 6; break; }
        e->atk_t -= dt; steer(e, to_player, E_TURN_RUN, dt);
        step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
        if (sqrtf(dx * dx + dy * dy + dz * dz) < E_RADIUS + 69.0f) {
            Vec3 d = dxz > 1e-3f ? (Vec3){ dx / dxz, 0, dz / dxz } : (Vec3){ 1, 0, 0 };
            if (player_hit(pl, 1.0f, d)) { player_kill(pl, 3); e->t = ea_len(e, EA_WIN); e->want_speed = E_WALK; e->st = 11; }
            else { e->t = ea_len(e, EA_MISS); e->st = 3; }
        }
        break;
    case 3: anim = EA_MISS; if ((e->t -= dt) <= 0) { e->cool = e->type == 4 ? 1.5f : (e->type == 5 ? 1.0f : 0.5f); e->want_speed = E_WALK; e->st = 8; } break;
    case 6: anim = EA_BRAKE; if ((e->t -= dt) <= 0) { e->cool = e->type == 4 ? 1.5f : (e->type == 5 ? 1.0f : 0.5f); e->want_speed = E_WALK; e->st = 8; } break;
    case 11: anim = EA_WIN; if ((e->t -= dt) <= 0) e->st = 8; break;
    case 9:
        anim = EA_HIT;
        if (e->hp <= 0) { e->attackable = 0; e->dead_t = 0; e->st = 12; }
        else if (e->hit_t <= 0) e->st = 8;
        break;
    case 12: {                                                    /* dead: animation 13, fades out during the second half, then removed */
        anim = EA_DEAD; e->dead_t += dt; float L = ea_len(e, EA_DEAD) + 1.0f;
        if (e->dead_t > L * 0.5f) { in->fade = (e->dead_t - L * 0.5f) / (L * 0.5f); if (in->fade > 1) in->fade = 1; }
        if (e->dead_t >= L) { e->removed = 1; in->visible = 0; return; }
        break; }
    }
    /* knockback replaces the normal step: v = 600 * t_rest for 0.25 s */
    if (e->knock_t > 0) { e->knock_t -= dt; if (e->knock_t < 0) e->knock_t = 0; float v = dt * E_KNOCK * e->knock_t; step = (Vec3){ e->knock_dir.x * v, 0, e->knock_dir.z * v }; }
    if (step.x != 0 || step.z != 0) { if (!enemy_move(e, pl, step) && e->st == 8) { e->want_ang = e->ang + 3.14159265f; } }
    /* ground following 0x41a4e0: v += 200*dt - 0.2*v per frame, y -= v, never below the ground */
    { int found; float gy = player_ground_query(pl, in, (Vec3){ e->pos.x, e->pos.y + E_HEIGHT * 0.5f, e->pos.z }, &found);
      e->vfall += 200.0f * dt - 0.2f * e->vfall; e->pos.y -= e->vfall;
      if (found && e->pos.y <= gy) { e->pos.y = gy; e->vfall = 0; } }
    ea_play(e, anim);
    enemy_apply(e);
}

void enemies_update(EnemySet *s, struct Player *pl, Vec3 cam_pos, float dt)
{
    for (int i = 0; i < s->n; i++) enemy_update(&s->e[i], pl, cam_pos, dt);
}
