/* enemy.c - enemy types 4/5/6 after docs/ENEMY.md (state machine 0x418cf0, behaviours 0x41b2c0..0x41cf00).
 * Simplifications: no obstacle sensor (16 directions), no actor avoidance, wander picks a random direction and the
 * idle variations all use one animation; movement is a straight step that is refused at ledges / steps over 10 units
 * (as the original's sweep does) instead of the full swept cylinder; the death particles are not spawned. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "enemy.h"
#include "player.h"

/* constants that no message changes */
#define E_ACC      1000.0f
#define E_STEP     10.0f      /* P+0x30 / P+0x2c: max step up / drop */
#define E_KNOCK    600.0f     /* P+0x44 */

/* parameter block P per type (0x41d510 and the subtype cases; docs/ENEMY.md 2.3 and 8.1) */
static EnemyParams params_for(int type)
{
    EnemyParams p = { 30, 140, 200, 600, 800, 1500, 600, 1.5708f, 6.2832f, 800, 1, 1.5f, 1, 1, 2.0f, 400, 300, 0, 3000, 2, 19 };
    if (type == 5) p.cool = 1.0f;
    if (type == 6) { p.hp = 2; p.cool = 0.5f; }
    if (type >= 7) { p.dy = 800; p.turn_fast = 12.5664f; p.melee = 150; p.shot_visual = 0; p.shot_fx = 17; p.steer = 0.2f; }
    if (type == 8) { p.height = 130; p.hp = 2; p.cool = 1.0f; p.reload = 1.5f; p.steer = 0; p.shot_visual = 1; p.shot_fx = 18; }
    if (type == 13) { p.walk = 100; p.run = 300; p.dash = 500; p.dy = 10000; p.hp = 3; p.bite = 2; p.cool = 0.5f; p.shot_dmg = 2; p.reload = 1.0f; p.leash = 1000; p.melee = 300; p.shot_visual = 3; p.shot_fx = 20; }   /* docs/ENEMY2.md 2 */
    if (type == 9) { p.leash = 1000; p.hp = 3; p.bite = 3; p.shot_dmg = 2; p.melee = 10; p.steer = 0; p.shot_visual = 3; p.shot_fx = 20; }
    return p;
}

/* logical animation -> main .ins animation and speed divisor (table 0x4b29b8) */
enum { EA_RUN, EA_WALK, EA_DASH, EA_BRAKE, EA_MISS, EA_WIN, EA_HIT, EA_DEAD, EA_IDLE, EA_TURN };
typedef struct { int anim; float speed; int hold; } EAnim;
static const EAnim g_ea[] = {
    { 2, 3, 0 }, { 4, 3, 0 }, { 13, 10, 0 }, { 15, 2, 1 }, { 14, 3, 1 }, { 18, 1.5f, 1 }, { 11, 4, 1 }, { 12, 3, 1 }, { 6, 3, 0 }, { 16, 3, 0 } };
/* ghost, type 13 (records 0x4b2190, docs/ENEMY2.md 3.6) */
static const EAnim g_ea13[] = {
    { 2, 3, 0 }, { 4, 3, 0 }, { 13, 3, 0 }, { 15, 3, 1 }, { 14, 2, 1 }, { 16, 3, 1 }, { 11, 4, 1 }, { 12, 3, 1 }, { 6, 2, 0 }, { 0, 3, 0 } };
#define EA(e, a) ((e)->type == 13 ? g_ea13[a] : g_ea[a])
static float ea_len(const Enemy *e, int a) { const Model *m = e->inst->model; int s = EA(e, a).anim; return (uint32_t)s < m->nanims ? m->anims[s].duration_s / EA(e, a).speed : 0.5f; }
static void ea_play(Enemy *e, int a)
{
    Instance *in = e->inst; const Model *m = in->model; int s = EA(e, a).anim; if ((uint32_t)s >= m->nanims) return;
    if (in->anim != s) { in->anim = s; in->anim_time = 0; }
    in->anim_speed = EA(e, a).speed;
    if (EA(e, a).hold && in->anim_time > m->anims[s].duration_s - 0.15f) { in->anim_time = m->anims[s].duration_s * 0.999f; in->anim_speed = 0; }   /* held on the last frame: the clock advances after this, so it must stop */
}

float enemy_radius(const Enemy *e) { return e->P.radius; }
float enemy_height(const Enemy *e) { return e->P.height; }

void enemies_add(EnemySet *s, Instance *inst, int type)
{
    if (s->n >= MAX_ENEMIES) return;
    for (int i = 0; i < s->n; i++) if (s->e[i].inst == inst) return;
    Enemy *e = &s->e[s->n++]; Enemy z = { 0 }; *e = z;
    e->inst = inst; e->type = type; e->pos = e->home = inst->position; e->P = params_for(type); e->hp = e->P.hp;
    e->cool = -1; e->attackable = 1; e->speed = e->want_speed = e->P.walk; e->path_dir = 1; e->path_to = 1;
    e->st = inst->traj.npoints > 1 ? 0 : (type >= 7 && type <= 9 ? 3 : 8); e->hand = rand() & 1;
    if (e->st == 0) { Vec3 a = inst->traj.points[0], b = inst->traj.points[1]; e->ang = atan2f(b.z - a.z, b.x - a.x); }
    inst->scripted = 0;
    if (type == 14) boss_init(e);
}

int enemy_take_damage(Enemy *e, float dmg, Vec3 dir)
{
    if (e->type == 14) return boss_take_damage(e);
    int shooter = e->type >= 7 && e->type <= 9;
    if (e->removed || e->st == (shooter ? 10 : 12) || e->hit_t > 0 || e->knock_t > 0) return 0;
    e->st = shooter ? 4 : 9; e->hit_t = e->knock_t = 0.25f; e->knock_dir = dir; e->hp -= dmg;
    return e->hp <= 0;
}

int enemy_hit(Enemy *e, float dmg, Vec3 dir, Vec3 pt, int kind)
{
    if (e->type == 14) return boss_take_damage(e);                  /* 0x40fe90 passes the kind on: no star for kind 2 (the special attack) */
    (void)kind;                                                      /* types 4..9, 13 call Enemy_TakeDamage with kind 0, so they always get the star */
    int shooter = e->type >= 7 && e->type <= 9;
    if (e->removed || e->st == (shooter ? 10 : 12) || e->hit_t > 0 || e->knock_t > 0) return 0;
    int died = enemy_take_damage(e, dmg, dir);
    game_hit_star(pt);
    return died;
}

/* bomb blast 0x44d650 -> vtbl[40] 0x41ae20 on every Npc of category 2: inside r (3D, to the instance origin) it dies at once,
 * through the hit state like a last peck (docs/BOMB.md 4.2); the boss takes its usual single point */
void enemies_blast(EnemySet *s, Vec3 c, float r)
{
    for (int i = 0; i < s->n; i++) {
        Enemy *e = &s->e[i]; if (e->removed || !e->inst->visible || e->hp <= 0) continue;
        float dx = e->pos.x - c.x, dy = e->pos.y - c.y, dz = e->pos.z - c.z; if (dx * dx + dy * dy + dz * dz >= r * r) continue;
        float l = sqrtf(dx * dx + dz * dz); Vec3 d = l > 1e-3f ? (Vec3){ dx / l, 0, dz / l } : (Vec3){ 0, 0, 1 };
        e->hit_t = e->knock_t = 0; enemy_take_damage(e, e->type == 14 ? 1.0f : e->hp, d);
    }
}

static float ang_diff(float a, float b) { float d = a - b; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f; return d; }
static void steer(Enemy *e, float want, float rate, float dt) { float d = ang_diff(want, e->ang), m = rate * dt; if (d > m) d = m; if (d < -m) d = -m; e->ang += d; }

/* common move 0x41b2c0: a step is only taken when the ground there is within [-10, +10] of the feet */
static int enemy_move(Enemy *e, struct Player *pl, Vec3 delta)
{
    Vec3 to = { e->pos.x + delta.x, e->pos.y, e->pos.z + delta.z }; int found;
    if (e->type == 13) {                                          /* 0x41b2c0 for subtype >= 9: y is kept, no ledge / step test (P+0x2c/0x30 = 10000), walls still stop it */
        float l = sqrtf(delta.x * delta.x + delta.z * delta.z), k = l > 1e-4f ? (l + e->P.radius) / l : 1; Vec3 c = { e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z };
        if (player_segment_blocked(pl, c, (Vec3){ c.x + delta.x * k, c.y, c.z + delta.z * k })) return 0;
        e->pos.x = to.x; e->pos.z = to.z; return 1;
    }
    float gy = player_ground_query(pl, e->inst, (Vec3){ to.x, to.y + e->P.height * 0.5f, to.z }, &found);
    if (!found || gy - e->pos.y > E_STEP || e->pos.y - gy > E_STEP) return 0;
    e->pos.x = to.x; e->pos.z = to.z; return 1;
}

void enemy_place(Enemy *e)
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
    { float dx = e->pos.x - cam.x, dy = e->pos.y - cam.y, dz = e->pos.z - cam.z; if (dx * dx + dy * dy + dz * dz >= e->P.active_d * e->P.active_d && e->st != 12) return; }   /* Think 0x41a320 */
    if (e->cool >= 0) e->cool -= dt;
    if (e->hit_t > 0) e->hit_t -= dt;
    if (e->reload >= 0) e->reload -= dt;
    if (e->type == 13 && e->st != 12) in->fade = 0.5f;            /* 0x413ab0: fade target 0.5 every frame = half transparent */
    /* FindTarget 0x41af80: no view cone, no line of sight */
    Vec3 tp = pl->pos; float dx = tp.x - e->pos.x, dy = tp.y - e->pos.y, dz = tp.z - e->pos.z;
    int see = !pl->dead_kind && dx * dx + dy * dy + dz * dz < e->P.see * e->P.see && fabsf(dy) < e->P.dy;
    float dxz = sqrtf(dx * dx + dz * dz), to_player = atan2f(dz, dx);
    Vec3 step = { 0, 0, 0 }; int anim = EA_IDLE;

    /* speed regulator H: accelerates with 1000 u/s^2 toward the wanted speed */
    if (e->speed < e->want_speed) { e->speed += E_ACC * dt; if (e->speed > e->want_speed) e->speed = e->want_speed; }
    else if (e->speed > e->want_speed) e->speed = e->want_speed;

    switch (e->st) {
    case 0: {                                                     /* patrol: slides exactly along the TRAJ segments, ping-pong on an open path */
        const Trajectory *T = &in->traj; Vec3 b = T->points[e->path_to];
        float bx = b.x - e->pos.x, bz = b.z - e->pos.z, l = sqrtf(bx * bx + bz * bz), mv = e->P.walk * dt;
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
          else if (hx * hx + hz * hz > e->P.leash * e->P.leash) e->want_ang = atan2f(hz, hx); }
        if (e->wander_walk) {
            steer(e, e->want_ang, e->P.turn, dt); e->want_speed = e->P.walk; anim = EA_WALK;
            step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
            if (fabsf(ang_diff(e->want_ang, e->ang)) > 0.5f) { step = (Vec3){ 0, 0, 0 }; anim = EA_TURN; }
        }
        if (e->cool < 0 && see) e->st = 2;
        break;
    case 2:                                                       /* noticed: turn on the spot first, then run */
        if (!see) { e->st = 8; break; }
        e->turn_t = fabsf(ang_diff(to_player, e->ang)) / e->P.turn_fast; e->speed = e->want_speed = e->P.run; e->st = 1; if (e->type == 13) e->reload = e->P.reload;
        /* fallthrough */
    case 1:
        if (!see) { e->want_speed = e->P.walk; e->st = 8; break; }
        steer(e, to_player, e->P.turn_fast, dt);
        if ((e->turn_t -= dt) <= 0) { step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt }; anim = EA_RUN; } else anim = EA_TURN;
        e->atk_t = ea_len(e, EA_DASH) + 0.5f * ea_len(e, EA_MISS);
        if (e->type == 13) {                                      /* ghost 0x413d0d: dives within 300 (xz) when it moves at the player (3D dot), else fires while chasing */
            float d3 = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dxz <= e->P.melee && d3 > 1e-3f && (cosf(e->ang) * dx + sinf(e->ang) * dz) / d3 > 0.95f) { e->speed = e->want_speed = e->P.dash; e->st = 4; break; }
            if (e->reload > 0) break;
            const Model *mo = in->model; uint32_t want = e->hand == 1 ? 0 : 1, seen = 0; Vec3 m0 = { e->pos.x, e->pos.y + e->P.height * 0.6f, e->pos.z };
            for (uint32_t i = 0; i < mo->nnodes; i++) if (mo->nodes[i].kind == 0x20 && mo->nodes[i].type_code == 1 && mo->nodes[i].npoints >= 1) { if (seen++ == want) { m0 = ins_point_world(in, mo->nodes[i].point_base); break; } }
            Vec3 sd = { tp.x - m0.x, tp.y - m0.y, tp.z - m0.z }; float sl = sqrtf(sd.x * sd.x + sd.y * sd.y + sd.z * sd.z);
            if (sl > 1e-3f && !player_segment_blocked(pl, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, m0))
                game_enemy_shot(e, m0, (Vec3){ sd.x / sl, sd.y / sl, sd.z / sl }, 1000.0f, e->P.shot_dmg, 0, e->P.shot_visual, e->P.shot_fx);   /* straight fireball at the feet (0x414d10) */
            e->reload += e->P.reload; e->hand ^= 1;
            break;
        }
        if (dxz <= e->P.dash * e->atk_t && dxz > 1e-3f && (cosf(e->ang) * dx + sinf(e->ang) * dz) / dxz > 0.95f) { e->speed = e->want_speed = e->P.dash; e->st = 4; }
        break;
    case 4:                                                       /* dash: the only state that hurts the player */
        anim = EA_DASH;
        if (!see || e->atk_t <= 0) { e->t = ea_len(e, EA_BRAKE); e->st = 6; break; }
        e->atk_t -= dt; steer(e, to_player, e->P.turn_fast, dt);
        step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
        if (sqrtf(dx * dx + dy * dy + dz * dz) < e->P.radius + 69.0f) {
            Vec3 d = dxz > 1e-3f ? (Vec3){ dx / dxz, 0, dz / dxz } : (Vec3){ 1, 0, 0 };
            if (player_hit(pl, e->P.bite, d)) { player_kill(pl, 3); e->t = ea_len(e, EA_WIN); e->want_speed = e->P.walk; e->st = 11; }
            else { e->t = ea_len(e, EA_MISS); e->st = 3; }
        }
        break;
    case 3: anim = EA_MISS; if ((e->t -= dt) <= 0) { e->cool = e->P.cool; e->want_speed = e->P.walk; e->st = 8; } break;
    case 6: anim = EA_BRAKE; if ((e->t -= dt) <= 0) { if (e->type != 13) e->cool = e->P.cool; e->want_speed = e->P.walk; e->st = 8; } break;
    case 11: anim = EA_WIN; if ((e->t -= dt) <= 0) e->st = 8; break;
    case 9:
        anim = EA_HIT;
        if (e->hp <= 0) { game_enemy_stars(e); e->attackable = 0; e->dead_t = e->type == 13 ? (ea_len(e, EA_DEAD) + 1.0f) * 0.5f : 0; e->st = 12; }   /* the ghost starts fading at once */
        else if (e->hit_t <= 0) { e->st = 8; if (e->type == 13) e->reload = 2.0f * e->P.reload; }
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
    if (e->type == 13) {                                          /* height control 0x414f10: feet at the player's feet height (home without a target); frozen when hit / dead */
        if (e->st != 9 && e->st != 12) {
            int found; float gy = player_ground_query(pl, in, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, &found);
            float want = see ? tp.y - e->pos.y : e->home.y - e->pos.y, stp = see ? e->P.run * dt : e->P.walk * dt;
            if (see && want > 0 && (pl->jumper.state == 0 || pl->jumper.state == 1 || pl->jumper.state == 7)) stp *= 0.2f;
            if (want < -0.01f) { e->pos.y += want < -stp ? -stp : want; if (found && e->pos.y < gy) e->pos.y = gy; }
            else if (want > 0.01f) { float up = want > stp ? stp : want; if (!player_segment_blocked(pl, (Vec3){ e->pos.x, e->pos.y + e->P.height, e->pos.z }, (Vec3){ e->pos.x, e->pos.y + e->P.height + up, e->pos.z })) e->pos.y += up; }
        }
    } else
    /* ground following 0x41a4e0: v += 200*dt - 0.2*v per frame, y -= v, never below the ground */
    { int found; float gy = player_ground_query(pl, in, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, &found);
      e->vfall += 200.0f * dt - 0.2f * e->vfall; e->pos.y -= e->vfall;
      if (found && e->pos.y <= gy) { e->pos.y = gy; e->vfall = 0; } }
    ea_play(e, anim);
    enemy_place(e);
}

/* ---- shooters, types 7/8/9 (docs/ENEMY.md 8): state machine 0x416fb0, animation table 0x4b26a8 -------------------- */
enum { S_PATH = 0, S_BITE = 1, S_TOWANDER = 2, S_WANDER = 3, S_HIT = 4, S_DASH0 = 5, S_DASH = 6, S_BRAKE = 7, S_WIN = 9, S_DEAD = 10, S_WAIT = 11, S_AIM = 12, S_FIRE = 13, S_DODGE0 = 15, S_DODGE = 16 };
enum { SA_WALK, SA_DASH, SA_BRAKE, SA_BITE, SA_WIN, SA_HIT, SA_DEAD, SA_IDLE, SA_TURN_L, SA_TURN_R, SA_AIM, SA_THROW, SA_DODGE };
static const struct { int anim; float speed; int hold; } g_sa[] = {
    { 4, 3, 0 }, { 13, 3, 0 }, { 15, 3, 1 }, { 14, 2, 1 }, { 18, 3, 1 }, { 11, 4, 1 }, { 12, 3, 1 }, { 6, 2, 0 }, { 16, 3, 0 }, { 17, 3, 0 }, { 21, 3, 1 }, { 22, 3, 1 }, { 19, 3, 1 } };
static float sa_len(const Enemy *e, int a) { const Model *m = e->inst->model; int s = g_sa[a].anim; return (uint32_t)s < m->nanims ? m->anims[s].duration_s / g_sa[a].speed : 0.5f; }
static void sa_play(Enemy *e, int a, float speed)
{
    Instance *in = e->inst; const Model *m = in->model; int s = g_sa[a].anim; if ((uint32_t)s >= m->nanims) return;
    if (in->anim != s) { in->anim = s; in->anim_time = 0; }
    in->anim_speed = speed > 0 ? speed : g_sa[a].speed;
    if (g_sa[a].hold && in->anim_time > m->anims[s].duration_s - 0.15f) { in->anim_time = m->anims[s].duration_s * 0.999f; in->anim_speed = 0; }
}
static int shooter_vector(const Instance *in, uint32_t tc, Vec3 *p0)      /* 0x42f6b0: start of the first marker with this typecode */
{
    const Model *mo = in->model;
    for (uint32_t i = 0; i < mo->nnodes; i++) if (mo->nodes[i].kind == 0x20 && mo->nodes[i].type_code == tc && mo->nodes[i].npoints >= 1) { *p0 = ins_point_world(in, mo->nodes[i].point_base); return 1; }
    return 0;
}
static void shooter_update(Enemy *e, struct Player *pl, Vec3 cam, float dt)
{
    Instance *in = e->inst;
    if (e->removed || !in->visible) return;
    { float dx = e->pos.x - cam.x, dy = e->pos.y - cam.y, dz = e->pos.z - cam.z; if (dx * dx + dy * dy + dz * dz >= e->P.active_d * e->P.active_d && e->st != S_DEAD) return; }
    if (e->cool >= 0) e->cool -= dt;
    if (e->hit_t > 0) e->hit_t -= dt;
    if (e->reload >= 0) e->reload -= dt;
    Vec3 tp = pl->pos; float dx = tp.x - e->pos.x, dy = tp.y - e->pos.y, dz = tp.z - e->pos.z;
    int see = !pl->dead_kind && dx * dx + dy * dy + dz * dz < e->P.see * e->P.see && fabsf(dy) < e->P.dy;
    float dxz = sqrtf(dx * dx + dz * dz), to_player = atan2f(dz, dx);
    Vec3 step = { 0, 0, 0 }; int anim = SA_IDLE; float anim_speed = 0;

    if (e->speed < e->want_speed) { e->speed += E_ACC * dt; if (e->speed > e->want_speed) e->speed = e->want_speed; }
    else if (e->speed > e->want_speed) e->speed = e->want_speed;

    switch (e->st) {
    case S_PATH: {
        const Trajectory *T = &in->traj; Vec3 b = T->points[e->path_to];
        float bx = b.x - e->pos.x, bz = b.z - e->pos.z, l = sqrtf(bx * bx + bz * bz), mv = e->P.walk * dt;
        if (l <= mv) {
            e->pos.x = b.x; e->pos.z = b.z;
            int nx = (int)e->path_to + e->path_dir;
            if (nx < 0 || nx >= (int)T->npoints) { if (T->closed) nx = nx < 0 ? (int)T->npoints - 1 : 0; else { e->path_dir = -e->path_dir; nx = (int)e->path_to + e->path_dir; } }
            e->path_to = (uint32_t)nx;
        } else { e->ang = atan2f(bz, bx); e->pos.x += bx / l * mv; e->pos.z += bz / l * mv; }
        anim = SA_WALK;
        if (e->cool < 0 && see) { e->home = e->pos; e->st = S_WAIT; }
        break; }
    case S_TOWANDER: e->speed = e->want_speed = e->P.walk; e->wander_t = 0; e->wander_walk = 1; e->st = S_WANDER;
        /* fallthrough */
    case S_WANDER:
        e->wander_t -= dt;
        if (e->wander_t <= 0) {
            e->wander_walk = !e->wander_walk;
            if (e->wander_walk) { e->want_ang = (float)(rand() % 6283) * 0.001f; e->wander_t = 1.5f + (float)(rand() & 0x3ff) / 1024.0f; }
            else e->wander_t = sa_len(e, SA_IDLE);
        }
        { float hx = e->home.x - e->pos.x, hz = e->home.z - e->pos.z;
          if (in->traj.npoints > 1) { e->want_ang = atan2f(hz, hx); e->wander_walk = 1; if (hx * hx + hz * hz < 10.0f * 10.0f) { e->st = S_PATH; break; } }
          else if (hx * hx + hz * hz > e->P.leash * e->P.leash) e->want_ang = atan2f(hz, hx); }
        if (e->wander_walk) {
            steer(e, e->want_ang, e->P.turn, dt); anim = SA_WALK;
            step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
            if (fabsf(ang_diff(e->want_ang, e->ang)) > 0.5f) { step = (Vec3){ 0, 0, 0 }; anim = ang_diff(e->want_ang, e->ang) > 0 ? SA_TURN_L : SA_TURN_R; }
        }
        if (e->cool < 0 && see) e->st = S_WAIT;
        break;
    case S_WAIT: {                                                /* reloading: stands still and does NOT turn (H.Tick is not called in 0x4171bd) */
        if (!see) { e->st = S_TOWANDER; break; }
        float d = ang_diff(to_player, e->ang), T = 4.0f * fabsf(d) / e->P.turn_fast;
        anim = d > 0 ? SA_TURN_L : SA_TURN_R;
        if (T > 0.02f) { const Model *m = in->model; int s = g_sa[anim].anim; anim_speed = (uint32_t)s < m->nanims ? m->anims[s].duration_s / T : 0; } else anim = SA_IDLE;
        if (e->reload <= 0) { e->t = sa_len(e, SA_AIM); e->st = S_AIM; }
        break; }
    case S_AIM:                                                   /* windup: the only place it turns, at 4 * P+0x14 */
        if (!see) { e->st = S_TOWANDER; break; }
        if (dxz <= e->P.melee) { e->st = S_DASH0; break; }
        if (e->t > 0) e->t -= dt; else { if (e->type == 9) e->t = 0.1f; e->st = S_FIRE; }
        steer(e, to_player, 4.0f * e->P.turn_fast, dt); anim = SA_AIM;
        break;
    case S_FIRE: {
        if (!see) { e->st = S_TOWANDER; break; }
        anim = SA_THROW; Vec3 m0;
        if (!shooter_vector(in, 1, &m0)) { m0 = (Vec3){ e->pos.x + cosf(e->ang) * e->P.radius, e->pos.y + e->P.height * 0.7f, e->pos.z + sinf(e->ang) * e->P.radius }; }
        if (e->type == 9 && (e->t -= dt) > 0) break;
        /* 0x497ed0: from the own centre to the own muzzle; blocked = the shot is skipped but the reload still counts */
        if (!player_segment_blocked(pl, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, m0))
            game_enemy_shot(e, m0, (Vec3){ cosf(e->ang), 0, sinf(e->ang) }, 1000.0f, e->P.shot_dmg, e->P.steer, e->P.shot_visual, e->P.shot_fx);
        e->reload += e->P.reload; e->throw_hold = 1; e->st = S_WAIT;
        break; }
    case S_DASH0:
        if (!see) { e->st = S_TOWANDER; break; }
        if (dxz > e->P.melee) break;
        e->turn_t = fabsf(ang_diff(to_player, e->ang)) / e->P.turn_fast; e->speed = e->want_speed = e->P.run; e->atk_t = dxz / e->P.run; e->st = S_DASH;
        break;
    case S_DASH:
        anim = SA_DASH;
        if (!see || e->atk_t < 0) { e->t = sa_len(e, SA_BRAKE); e->want_speed = e->P.walk; e->st = S_BRAKE; break; }
        e->atk_t -= dt; steer(e, to_player, e->P.turn_fast, dt);
        if ((e->turn_t -= dt) <= 0) step = (Vec3){ cosf(e->ang) * e->speed * dt, 0, sinf(e->ang) * e->speed * dt };
        if (sqrtf(dx * dx + dy * dy + dz * dz) < e->P.radius + 69.0f) {
            Vec3 d = dxz > 1e-3f ? (Vec3){ dx / dxz, 0, dz / dxz } : (Vec3){ 1, 0, 0 };
            if (player_hit(pl, e->P.bite, d)) { player_kill(pl, 3); e->t = sa_len(e, SA_WIN); e->st = S_WIN; }
            else { e->t = sa_len(e, SA_BITE); e->st = S_BITE; }
        }
        break;
    case S_BITE: anim = SA_BITE; if (e->t > 0) e->t -= dt; else { e->cool = e->P.cool; e->st = S_TOWANDER; } break;
    case S_BRAKE: anim = SA_BRAKE; if (e->t < 0) { e->cool = e->P.cool; e->st = S_TOWANDER; } else e->t -= dt; break;
    case S_WIN: anim = SA_WIN; if ((e->t -= dt) <= 0) e->st = S_TOWANDER; break;
    case S_HIT:
        anim = SA_HIT;
        if (e->hp <= 0) { game_enemy_stars(e); e->attackable = 0; e->dead_t = 0; e->st = S_DEAD; }
        else if (e->hit_t <= 0) e->st = S_TOWANDER;
        break;
    case S_DEAD: {
        anim = SA_DEAD; e->dead_t += dt; float L = sa_len(e, SA_DEAD) + 1.0f;
        if (e->dead_t > L * 0.5f) { in->fade = (e->dead_t - L * 0.5f) / (L * 0.5f); if (in->fade > 1) in->fade = 1; }
        if (e->dead_t >= L) { e->removed = 1; in->visible = 0; return; }
        break; }
    case S_DODGE0: {                                              /* 0x417a63: 300 away along the dive, else sideways, else back */
        float l = sqrtf(e->warn.x * e->warn.x + e->warn.z * e->warn.z); Vec3 d = l > 1e-3f ? (Vec3){ e->warn.x / l * e->P.dodge, 0, e->warn.z / l * e->P.dodge } : (Vec3){ e->P.dodge, 0, 0 };
        Vec3 cand[4] = { { d.x, 0, d.z }, { -d.z, 0, d.x }, { d.z, 0, -d.x }, { -d.x, 0, -d.z } }, pick = cand[0];
        for (int k = 0; k < 4; k++) {
            Vec3 c = { e->pos.x + cand[k].x, e->pos.y, e->pos.z + cand[k].z }; int found;
            float gy = player_ground_query(pl, in, (Vec3){ c.x, c.y + e->P.height * 0.5f, c.z }, &found);
            if (found && fabsf(gy - e->pos.y) <= E_STEP && !player_segment_blocked(pl, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, (Vec3){ c.x, c.y + e->P.height * 0.5f, c.z })) { pick = cand[k]; break; }
        }
        e->dodge_dir = (Vec3){ pick.x / e->P.dodge, 0, pick.z / e->P.dodge }; e->ang = atan2f(-pick.z, -pick.x);   /* faces against the move */
        e->t = e->P.dodge / (3.0f * e->P.run); e->st = S_DODGE;
        break; }
    case S_DODGE:
        anim = SA_DODGE;
        if (e->t > 0) { e->t -= dt; float v = 3.0f * e->P.run * dt; step = (Vec3){ e->dodge_dir.x * v, 0, e->dodge_dir.z * v }; }
        else { e->reload = e->P.reload; e->cool = e->P.cool; e->speed = e->want_speed = e->P.walk; e->st = S_WANDER; }
        break;
    }
    if (e->knock_t > 0) { e->knock_t -= dt; if (e->knock_t < 0) e->knock_t = 0; float v = dt * E_KNOCK * e->knock_t; step = (Vec3){ e->knock_dir.x * v, 0, e->knock_dir.z * v }; }
    if (step.x != 0 || step.z != 0) { if (!enemy_move(e, pl, step) && e->st == S_WANDER) e->want_ang = e->ang + 3.14159265f; }
    { int found; float gy = player_ground_query(pl, in, (Vec3){ e->pos.x, e->pos.y + e->P.height * 0.5f, e->pos.z }, &found);
      e->vfall += 200.0f * dt - 0.2f * e->vfall; e->pos.y -= e->vfall;
      if (found && e->pos.y <= gy) { e->pos.y = gy; e->vfall = 0; } }
    /* the throw (priority 1000) plays out over the turn animations (priority 900) of the wait state */
    if (e->throw_hold) { const Model *m = in->model; int s = g_sa[SA_THROW].anim; if (e->st == S_WAIT && (uint32_t)s < m->nanims && in->anim == s && in->anim_time < m->anims[s].duration_s * 0.98f) { anim = SA_THROW; anim_speed = 0; } else if (e->st != S_FIRE) e->throw_hold = 0; }
    sa_play(e, anim, anim_speed);
    enemy_place(e);
}

void enemy_warn_dive(Enemy *e, Vec3 d) { if (e->type == 9 && !e->removed && e->st != S_DEAD && e->st != S_HIT) { e->warn = d; e->st = S_DODGE0; } }
void enemy_player_killed(Enemy *e)
{
    if (!e || e->removed) return;
    if (e->type == 13) { if (e->st != 12) { e->t = ea_len(e, EA_WIN); e->want_speed = e->P.walk; e->st = 11; } }
    else if (e->type >= 7 && e->type <= 9 && e->st != S_DEAD) { e->t = sa_len(e, SA_WIN); e->st = S_WIN; }
}

void enemies_msg11(EnemySet *s, Instance *inst, int n, int v)
{
    Enemy *e = NULL; for (int i = 0; i < s->n; i++) if (s->e[i].inst == inst) e = &s->e[i];
    if (!e) return;
    if (e->type == 14) { if (n == 4) boss_reset(e); return; }        /* Enemy::HandleMsg 11/4 = vtbl[17]; the rest writes P fields the boss sets itself */
    switch (n) {
    case 0: e->P.leash = (float)v; break;
    case 1: e->P.see = (float)v; break;
    case 7: e->P.walk = (float)v; if (e->want_speed < e->P.run) e->speed = e->want_speed = (float)v; break;
    case 8: e->P.run = (float)v; break;
    case 9: case 10: e->P.turn = (float)v * 3.14159265f / 180.0f; break;
    case 12: e->P.bite = (float)v; break;
    case 13: e->P.hp = e->hp = (float)v; break;
    case 14: e->P.dy = (float)v; break;
    case 15: e->P.cool = (float)v; break;
    case 31: e->P.shot_dmg = (float)v; break;
    case 33: e->P.reload = (float)v; break;
    case 35: e->P.melee = (float)v; break;
    case 36: e->P.dodge = (float)v; break;
    case 37: e->P.active_d = (float)v; break;
    default: break;                                               /* 2/3 step limits, 4 reset, 5/6 leash flags, 19..27 wander weights: not ported */
    }
}

void enemies_update(EnemySet *s, struct Player *pl, Vec3 cam_pos, float dt)
{
    for (int i = 0; i < s->n; i++) {
        Enemy *e = &s->e[i];
        if (e->type == 14) { boss_update(e, pl, cam_pos, dt); boss_frame_end(e); }
        else if (e->type >= 7 && e->type <= 9) shooter_update(e, pl, cam_pos, dt); else enemy_update(e, pl, cam_pos, dt);
    }
}
