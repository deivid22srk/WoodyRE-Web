/* boss.c - enemy class 14, Buzz Buzzard (docs/BOSS14.md): the end boss of W1B (mode 1, his flying machine) and of
 * W2D / W3D / WWS (mode 2, hopping). ctor 0x40eb50, vtable 0x4a98a8, Update 0x40eec0.
 *
 * The class does nothing until the level script writes 1 or 2 into its mailbox variable (message 60); every update it
 * answers -1, and 3 once it is beaten. Mode 1: it chases the player HIGH (at its placement height, invulnerable), shakes
 * 0.25 s over him and drops with gravity (a cone under it takes a heart), then hovers LOW (floor + 290) where a hit costs
 * it exactly 1 of its 5 hp; after a hit, or after it touched the player, it goes up again. The coupled instance
 * (message 59, W1B 404 = its machine) gets its position, rotation and animation record every frame.
 *
 * Simplified: the behaviours have no obstacle sensor (no free-direction search, no actor avoidance); the hit star
 * 0x40c2d0 is not drawn; mode 2 (W2D/W3D/WWS) is ported with the same
 * state machine but its dust is the landing dust of the player's footsteps and it is not verified in those levels. */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "enemy.h"
#include "player.h"
#include "audio.h"

#define PI_F 3.14159265f

/* AnimCtrl records 0x4b1958 (34 x 0x1c bytes, dumped from the exe; all priority 1000, restart 1) */
typedef struct { int sub[4]; float speed; } BRec;
static const BRec g_br[34] = {
    { { 5, 5, 5, 5 }, 3 }, { { 6, 7, 7, 7 }, 3 }, { { 7, 7, 7, 7 }, 3 }, { { 8, 5, -1, -1 }, 3 }, { { 9, 10, 10, 10 }, 3 },
    { { 11, 5, -1, -1 }, 3 }, { { 11, 5, -1, -1 }, 3 }, { { 13, 13, 13, 13 }, 2 }, { { 14, 5, -1, -1 }, 1 }, { { 18, -1, -1, -1 }, 1 },
    { { 15, 5, -1, -1 }, 3 }, { { 16, 5, -1, -1 }, 3 }, { { 17, 5, -1, -1 }, 3 }, { { 15, 5, -1, -1 }, 3 }, { { 17, 5, -1, -1 }, 3 },
    { { 12, 12, 12, 12 }, 3 }, { { 16, 17, 15, 16 }, 1.5f },
    { { 19, 19, 19, 19 }, 3 }, { { 20, 21, 21, 21 }, 3 }, { { 21, 21, 21, 21 }, 3 }, { { 22, 19, -1, -1 }, 3 }, { { 23, 24, 24, 24 }, 3 },
    { { 25, 19, -1, -1 }, 3 }, { { 25, 19, -1, -1 }, 3 }, { { 27, 27, 27, 27 }, 2 }, { { 28, 19, -1, -1 }, 1 }, { { 32, -1, -1, -1 }, 1 },
    { { 29, 19, -1, -1 }, 3 }, { { 30, 19, -1, -1 }, 3 }, { { 31, 19, -1, -1 }, 3 }, { { 29, 19, -1, -1 }, 3 }, { { 31, 19, -1, -1 }, 3 },
    { { 25, 26, 26, 26 }, 3 }, { { 30, 31, 29, 30 }, 1.5f },
};
/* AnimLen(n) = duration(sub[0]) / speed, always of the BOSS model (AnimCtrl+0x4c) */
static float anim_len(const Enemy *e, int n)
{
    const Model *m = e->inst->model; int s = g_br[n].sub[0];
    return (uint32_t)s < m->nanims ? m->anims[s].duration_s / g_br[n].speed : 0;
}
/* Request 0x436b70 + Tick 0x436a50 on one instance: a chain of up to four animations, the last one loops, -1 = hold */
static void rec_tick(Instance *in, int n, int *cur, int *sub)
{
    const Model *m = in->model; const BRec *r = &g_br[n];
    if (n != *cur) { *cur = n; *sub = 0; if ((uint32_t)r->sub[0] < m->nanims) { in->anim = r->sub[0]; in->anim_time = 0; } }
    if ((uint32_t)in->anim >= m->nanims) return;
    in->anim_speed = r->speed;
    int nx = *sub < 3 ? r->sub[*sub + 1] : 0; float L = m->anims[in->anim].duration_s;
    if (*sub < 3 && (nx < 0 || (uint32_t)nx >= m->nanims)) {       /* hold the last frame: the clock advances after this, so stop it just before the end */
        if (in->anim_time >= L - 0.15f) { in->anim_time = L * 0.999f; in->anim_speed = 0; }
    } else if (*sub < 3 && in->anim_time >= L) { in->anim_time -= L; in->anim = nx; (*sub)++; }
}

static float ang_to(Vec3 a, Vec3 b) { return atan2f(b.z - a.z, b.x - a.x); }   /* direction = (cos, 0, sin), like the port's other enemies */
static float ang_diff(float a, float b) { float d = a - b; while (d > PI_F) d -= 2 * PI_F; while (d < -PI_F) d += 2 * PI_F; return d; }
static float dist_xz(Vec3 a, Vec3 b) { float x = a.x - b.x, z = a.z - b.z; return sqrtf(x * x + z * z); }
static float dist3(Vec3 a, Vec3 b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return sqrtf(x * x + y * y + z * z); }

/* ---- the base parts of Enemy (docs/ENEMY.md 3-5) with the P values of subtype 11 (BOSS14.md 2.3) ---------------- */
#define B_TURN_W    (PI_F / 4)   /* P+0x10 wander */
#define B_TURN_C    PI_F         /* P+0x14 chase, state 5 */
#define B_SEE       4600.0f      /* P+0x20 (3D) */
#define B_DY        3000.0f      /* P+0x24 */
#define B_HIGH_T    1.0f         /* P+0x38 */
#define B_BITE      1.0f         /* P+0x3c */
#define B_VSPEED    900.0f       /* P+0x78 */
#define B_CONE_H    250.0f       /* P+0x7c */
#define B_CONE_R    150.0f       /* P+0x80 */
#define B_LOW       290.0f       /* P+0x88 */
#define B_LAND_T    0.3f         /* P+0x9c */
#define B_SHAKE_T   0.25f        /* P+0xa4 */
#define B_SHAKE     20.0f        /* P+0xa8 */
#define B_ACC       1000.0f      /* H+0x28 */
#define B_PL_R      69.0f        /* the Perso's vtbl[32] / [33] */
#define B_PL_H      193.0f

static int m1(const Enemy *e) { return e->b.mode == 1; }

/* FindTarget vtbl[48](1, 0) = 0x41af80: the player within 4600 (3D) and |dy| < 3000 */
static int find_target(const Enemy *e, const Player *pl)
{
    if (pl->dead_kind) return 0;
    return dist3(e->pos, pl->pos) < B_SEE && fabsf(pl->pos.y - e->pos.y) < B_DY;
}

/* vtbl[53] 0x4105f0: how long an action / state lasts = AnimLen of the record vtbl[45] would pick for it */
static int wander_rec(const Enemy *e, int a)
{
    int k = m1(e) ? 0 : 17;
    if (a == 0 || a == 3) return k ? 27 : 10; if (a == 1 || a == 4) return k ? 28 : 11; if (a == 2) return k ? 29 : 12;
    if (a == 5 || a == 6 || a == 7 || a == 9 || a == 10) return k ? 19 : 2;
    return -1;
}
static float state_len(const Enemy *e)
{
    int k = m1(e) ? 0 : 17, n = -1;
    switch (e->b.st) {
    case 0: case 1: case 4: n = wander_rec(e, e->b.w_act); break;
    case 2: case 3: n = 2 + k; break;  case 5: n = 16 + k; break;  case 6: case 7: n = 4 + k; break;  case 8: n = 15 + k; break;
    case 9: n = 8 + k; break;          case 10: case 11: n = 7 + k; break;  case 12: n = 9 + k; break;  case 13: n = 6 + k; break;
    }
    return n < 0 ? 0 : anim_len(e, n);
}

/* ---- behaviours (ENEMY.md 5): 0 Dwalen (wander with a leash around home), 1 Achtervolgen (chase), 2 Stilstaan ---- */
static void wander_choose(Enemy *e)                                  /* 0x41c180(-1): walking and idling alternate */
{
    BossState *b = &e->b; int a = b->w_act > 5 ? rand() % 5 : 6 + (rand() & 1);
    b->w_act = a; int n = wander_rec(e, a); b->w_t = n < 0 ? 0 : anim_len(e, n);
    if (a == 7) e->want_ang = (float)(rand() % 6283) * 0.001f;     /* 0x41ba10: no sensor, a random direction */
}
static void wander_start(Enemy *e) { e->b.behav = 0; e->b.turn = B_TURN_W; e->b.w_act = -1; wander_choose(e); }   /* Init + 0x41c160 */
static void chase_start(Enemy *e, const Player *pl)                  /* 0x41bc80: run speed at once, turn pi, first turn on the spot */
{
    BossState *b = &e->b; b->behav = 1; b->turn = B_TURN_C; e->speed = e->want_speed = e->P.run;
    float a = fabsf(ang_diff(ang_to(e->pos, pl->pos), e->ang));
    if (a > 1e-4f) { b->c_turn_t = a / b->turn; b->c_run = 0; } else b->c_run = 1;
}
static void h_turn(Enemy *e, float dt)                               /* H tick 0x41ba90 */
{
    float d = ang_diff(e->want_ang, e->ang), s = e->b.turn * dt;
    if (fabsf(d) <= s) e->ang = e->want_ang; else e->ang += d > 0 ? s : -s;
}
static void h_speed(Enemy *e, float dt)
{
    if (e->speed < e->want_speed) { e->speed += B_ACC * dt; if (e->speed > e->want_speed) e->speed = e->want_speed; }
    else if (e->speed > e->want_speed) { e->speed -= B_ACC * dt; if (e->speed < e->want_speed) e->speed = e->want_speed; }
}
/* sweep 0x437580(res, from, to, up, 30): a SPHERE of radius P+4 (240) whose centre sits r + up + 1 above the feet
 * (up = min(P+0x30, h/2) = 75, so 316 over pos: it spans pos.y + 76 .. pos.y + 556) moves in substeps of at most 30;
 * after each one it is pushed out of the world polygons and the instance press nodes (0x407340, xz only, the full push)
 * and lifted so the feet never end below GetHeight. This is what keeps Buzz out of the four lanterns of the W1B arena
 * (model 6, press nodes up to y 1972) and away from the rock walls behind them: in the low phase (pos.y 1635) the sphere
 * meets the lantern heads, in the high phase (2430) it passes over them but meets the walls, so he never gets within the
 * 150 (xz) he needs to stomp a player who hides in the corner behind a lantern (docs/BOSS14.md 5.1). */
static Vec3 boss_sweep(Enemy *e, Player *pl, Vec3 from, Vec3 to)
{
    float r = e->P.radius, up = e->P.height * 0.5f, h = r + up + 1.0f;
    Vec3 d = { to.x - from.x, to.y - from.y, to.z - from.z };
    int n = (int)(floorf(sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) / 30.0f) + 1.0f + 0.5f); if (n < 1) return from;
    d.x /= n; d.y /= n; d.z /= n;
    Vec3 c = { from.x, from.y + h, from.z }, res = from;
    for (; n > 0; n--) {
        c.x += d.x; c.y += d.y; c.z += d.z;
        Vec3 push = player_sphere_push(pl, e->inst, c, r); c.x += push.x; c.z += push.z;
        int found; float gy = player_ground_query(pl, e->inst, c, &found);
        if (found && c.y - h < gy) c.y = gy + h;
        res = (Vec3){ c.x, c.y - h, c.z };
    }
    return res;
}
/* common move 0x41b2c0 for subtype >= 9: y is kept, no ledge or step test (P+0x2c/0x30 = 15000); the sweep slides the
 * sphere along whatever it touches. The "free" test ([0x4b310c] = ground normal y >= 0.8) always holds over the arena
 * floor and is not ported. */
static void behav_move(Enemy *e, Player *pl, float step, float dt)
{
    BossState *b = &e->b;
    /* the sweep runs EVERY frame, also with a zero step (Stilstaan, the shake, the stomp fall): 0x437580 then does one
     * substep in place, so the sphere keeps pushing him off a lantern head while he drops next to it */
    if (b->knock[b->behav] > 0) { b->knock[b->behav] -= dt; if (b->knock[b->behav] < 0) b->knock[b->behav] = 0; step = 0; }   /* a peck knocks with dir 0: no step */
    if (step < 0) step = 0;
    Vec3 d = { cosf(e->ang), 0, sinf(e->ang) };
    Vec3 res = boss_sweep(e, pl, e->pos, (Vec3){ e->pos.x + d.x * step, e->pos.y, e->pos.z + d.z * step });
    if (b->behav == 1 && step > 0) {                                /* Achtervolgen hook [2] 0x41bdf0: stuck (< 0.01) => wriggle -16..15 */
        float mx = res.x - e->pos.x, mz = res.z - e->pos.z;
        if (sqrtf(mx * mx + mz * mz) < 0.01f) { res.x += (float)(rand() % 32 - 16); res.z += (float)(rand() % 32 - 16); }
    }
    e->pos.x = res.x; e->pos.z = res.z;                              /* subtype >= 9: res.y = from.y */
}
static void behav_tick(Enemy *e, Player *pl, float dt)
{
    BossState *b = &e->b;
    switch (b->behav) {
    case 0: {                                                        /* Dwalen 0x41c640 */
        if ((b->w_t -= dt) <= 0) { if (b->w_act == 10) { b->w_act = 9; b->w_t = anim_len(e, wander_rec(e, 9)); } else wander_choose(e); }
        if (dist3(e->pos, e->home) > e->P.leash) {                   /* 0x41c500: back home, turn first (action 10), then walk (9) */
            e->want_ang = ang_to(e->pos, e->home);
            if (b->w_act != 9 && b->w_act != 10) { b->w_act = 10; b->w_t = fabsf(ang_diff(e->want_ang, e->ang)) / b->turn; }
        }
        int mv = b->w_act == 6 || b->w_act == 7 || b->w_act == 9 || b->w_act == 10;
        e->want_speed = mv ? e->P.walk : 0; h_speed(e, dt);
        if (b->w_act > 4) h_turn(e, dt);
        behav_move(e, pl, mv ? dt * e->speed : 0, dt);
        break; }
    case 1:                                                          /* Achtervolgen 0x41bee0 */
        e->want_ang = ang_to(e->pos, pl->pos); h_turn(e, dt); h_speed(e, dt);
        if (b->c_turn_t <= 0 && !b->c_run) b->c_run = 1; else b->c_turn_t -= dt;
        behav_move(e, pl, b->c_run ? dt * e->speed : 0, dt);
        break;
    default: behav_move(e, pl, 0, dt); break;                         /* Stilstaan: only the knockback timer */
    }
}

/* vtbl[43] 0x410900: hover height, or the fall (flag 4) with the feet k below pos */
static void height_tick(Enemy *e, Player *pl, float dt)
{
    BossState *b = &e->b;
    if (b->grav) {
        float k = m1(e) ? 200.0f : 130.0f, feet = e->pos.y - k; int found;
        float gy = player_ground_query(pl, e->inst, (Vec3){ e->pos.x, feet + e->P.height * 0.5f, e->pos.z }, &found);
        int on = found && gy >= feet - 0.5f;
        if (on) b->fall_v = 0; else { b->fall_v += dt * e->P.fall_g - b->fall_v * 0.2f; if (b->fall_v > 0) feet -= b->fall_v; }   /* 0x41a4e0: v in units per FRAME */
        if (found && feet < gy) { feet = gy; on = 1; }
        b->on_ground = on; e->pos.y = feet + k; return;
    }
    b->on_ground = 0;
    if (!m1(e) && !b->high && b->st != 9) {                          /* mode 2 hops in the low phase */
        float s = dt * e->P.run;
        if (b->bob_down) { b->bob += s; if (b->bob >= b->bob_max) { game_land_dust((Vec3){ e->pos.x, e->pos.y - 50, e->pos.z }, (Vec3){ 0, 1, 0 }); audio_fx(49, NULL, NULL); b->bob = b->bob_max; b->bob_down = 0; } }
        else { b->bob -= s; if (b->bob <= 0) { b->bob = 0; b->bob_down = 1; } }
    } else b->bob = 0;
    if (b->high) e->home.y = b->y_high;
    float d = e->home.y - e->pos.y - b->bob;
    if (fabsf(d) > 0.01f) { float s = dt * B_VSPEED; if (fabsf(d) > s) d = d < 0 ? -s : s; e->pos.y += d; }
}

/* vtbl[31] 0x410cf0: the player inside the cone that hangs point down under pos (250 high, 150 wide at the top) */
static int cone_touch(const Enemy *e, const Player *pl)
{
    Vec3 A = pl->pos, S = e->pos; float bot = S.y - B_CONE_H;
    if (A.y + B_PL_H < bot || S.y < A.y) return 0;
    float o = A.y - bot + B_PL_H, r = B_PL_R + (o < B_CONE_H ? o * B_CONE_R / B_CONE_H : B_CONE_R);
    return (A.x - S.x) * (A.x - S.x) + (A.z - S.z) * (A.z - S.z) <= r * r;
}
/* the player takes the bite; 1 = he died of it (the caller of the Perso's vtbl[39] kills him, like the other enemies) */
static int bite(Enemy *e, Player *pl)
{
    float dx = pl->pos.x - e->pos.x, dz = pl->pos.z - e->pos.z, l = sqrtf(dx * dx + dz * dz); Vec3 d = l > 1e-3f ? (Vec3){ dx / l, 0, dz / l } : (Vec3){ 0, 0, 0 };
    if (player_hit(pl, B_BITE, d)) { player_kill(pl, 3); return 1; }
    return 0;
}

/* vtbl[45] 0x410170: the record for the state of the PREVIOUS frame (it runs inside Enemy::Update, before the switch) */
static void anim_choose(Enemy *e, float dt)
{
    BossState *b = &e->b; int k = m1(e) ? 0 : 17, n = -1; (void)dt;
    switch (b->st) {
    case 0: case 1: case 4: n = (b->w_act == 8 || b->w_act < 0) ? -1 : wander_rec(e, b->w_act); break;
    case 2: case 3: n = 2 + k; break;  case 5: n = 16 + k; break;  case 6: case 7: n = 4 + k; break;  case 8: n = 15 + k; break;
    case 9: n = 8 + k; break;          case 10: case 11: n = 7 + k; break;  case 12: n = 9 + k; break;  case 13: n = 6 + k; break;
    }
    if (n < 0) n = b->rec >= 0 ? b->rec : 2 + k;                     /* no request: the controller keeps the running one */
    rec_tick(e->inst, n, &b->rec, &b->sub);
    if (b->link) rec_tick(b->link, n, &b->lrec, &b->lsub);          /* the same record number, on the link's own model */
    if (b->st != 10 && b->st != 11 && b->st != 12) {                 /* 0x468e40 / 0x468e50: the 2D loop, started on the first active frame */
        b->active = 1; if (!b->loop_on) { audio_fx(m1(e) ? 39 : 44, NULL, NULL); b->loop_on = 1; }
    }
}
static void loop_stop(Enemy *e) { if (e->b.loop_on) { audio_fx_stop(39, NULL, 0); audio_fx_stop(44, NULL, 0); e->b.loop_on = 0; } }   /* 0x468e20 */

/* vtbl[55] = 0x41b1b0 because vtbl[54] returns the link: it takes the boss's position and rotation */
static void sync_link(Enemy *e)
{
    Instance *l = e->b.link; if (!l) return;
    l->position = e->inst->position; l->quat = e->inst->quat; l->scale = e->inst->scale; l->world = e->inst->world;
}

void boss_reset(Enemy *e)                                            /* vtbl[17] 0x40ed90 (Enemy::Reset 0x41a010 first) */
{
    BossState *b = &e->b;
    e->pos = e->home = e->inst->position; e->hp = getenv("WOODY_BOSSHP") ? (float)atof(getenv("WOODY_BOSSHP")) : e->P.hp; e->hit_t = 0;   /* WOODY_BOSSHP: testing */ e->inst->visible = 1;   /* 0x407790: back in the world */
    wander_start(e); b->grav = 0; b->st = 0; b->t1d8 = 0; b->high = 1; b->acc = 0;
    b->bob_down = 1; b->bob_max = 150.0f; b->bob = 0; b->fall_v = 0; b->knock[0] = b->knock[1] = b->knock[2] = 0;
    b->rec = b->lrec = -1; b->sub = b->lsub = 0;
    game_boss_smoke(b->link, -1, 0);                                 /* bytes 0x5e857c..e */
    if (b->mode == 1) { e->P.run = 900; e->P.walk = 400; e->P.fall_g = 400; }
    else if (b->mode == 2) { e->P.run = 400; e->P.walk = 390; e->P.fall_g = 800; }
    loop_stop(e);                                                    /* 0x468e10: a new source */
}

void boss_init(Enemy *e)
{
    BossState *b = &e->b; Instance *in = e->inst;
    e->P.radius = 240; e->P.height = 150; e->P.walk = 600; e->P.run = 900; e->P.fall_g = 200; e->P.leash = 1000; e->P.see = B_SEE; e->P.dy = B_DY;
    e->P.hp = 5; e->P.active_d = 3000; e->attackable = 1;
    /* PostLoad 0x419ec1: the start angle from the placement (vtbl[44] builds the same quaternion as enemy_place) */
    { Quat q = in->quat; float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (l > 1e-6f) { q.x /= l; q.y /= l; } float yaw = 2.0f * atan2f(-q.y, q.x); e->ang = e->want_ang = PI_F * 0.5f - yaw; }
    b->y_high = in->position.y; b->mode = 0; b->mail_var = 0; b->link = NULL;
    boss_reset(e);
    in->visible = 1;                                                 /* the factory hangs it in the world; the W1B script hides it 1 s later */
}

int boss_take_damage(Enemy *e)                                       /* vtbl[39] 0x40fe90 */
{
    BossState *b = &e->b;
    if (b->mode == 0 || b->high || e->hit_t > 0) return 0;          /* high phase or still "hit": invulnerable */
    b->st = 9; e->hit_t = anim_len(e, m1(e) ? 8 : 25);               /* vtbl[53]: AnimLen(8) = 2.1 s in W1B */
    audio_fx(m1(e) ? 42 : 47, NULL, NULL);
    if (m1(e)) switch ((int)e->hp) {                                 /* hp BEFORE the hit, table 0x410054 */
        case 1: game_boss_smoke(b->link, -1, 0); break;             /* the last hit: the smoke stops */
        case 2: game_boss_smoke(b->link, 0, 1); break;
        case 3: game_boss_smoke(b->link, 1, 1); break;
        case 4: game_boss_smoke(b->link, 2, 1); break;
        default: break;
    } else game_explosion((Vec3){ e->pos.x, e->pos.y + 400, e->pos.z });
    /* Enemy_TakeDamage 0x41adc0 with 1.0 whatever the attacker says: a knockback still running on the active behaviour refuses the hit */
    if (b->knock[b->behav] > 0) return 0;
    b->knock[b->behav] = state_len(e);
    e->hp -= 1.0f;
    printf("  BOSS %u hit, hp %.0f", e->inst->index, e->hp), puts("");
    return e->hp <= 0;
}

void boss_update(Enemy *e, Player *pl, Vec3 cam, float dt)
{
    BossState *b = &e->b; Instance *in = e->inst;
    if (!b->y_low_ok) {                                              /* PostLoad: the ground under the placement + 290 (0x41a2a0) */
        int found; float gy = player_ground_query(pl, in, (Vec3){ in->position.x, in->position.y + e->P.height * 0.5f, in->position.z }, &found);
        b->y_low = (found ? gy : in->position.y) + B_LOW; b->y_low_ok = 1;
    }
    if (b->mode == 0) { if (b->rec >= 0) { rec_tick(in, b->rec, &b->rec, &b->sub); if (b->link) rec_tick(b->link, b->lrec, &b->lrec, &b->lsub); } }   /* beaten: the last record plays out */
    /* Think 0x41a320: only within 3000 of the camera (or dead) */
    if (dist3(e->pos, cam) >= e->P.active_d && e->hp > 0) return;
    /* mailbox 0x410be0, first thing in every update */
    { int v = game_var_get(b->mail_var);
      if ((v == 1 || v == 2) && b->mode != v) { b->mode = v; boss_reset(e); printf("  BOSS %u: mode %d", in->index, v), puts(""); }
      game_var_set(b->mail_var, -1); }
    if (b->mode == 0) return;
    /* Enemy::Update 0x41a3e0: behaviour, height, rotation, animation, the link */
    behav_tick(e, pl, dt);
    height_tick(e, pl, dt);
    enemy_place(e);
    anim_choose(e, dt);
    if (b->t1d4 >= 0) b->t1d4 -= dt; if (b->t1d8 >= 0) b->t1d8 -= dt; if (b->t1e4 >= 0) b->t1e4 -= dt;
    int t = find_target(e, pl), k = m1(e) ? 0 : 1;
    switch (b->st) {
    case 0:                                                          /* to the hover height, wander 0x40efb5 */
        e->home.y = b->high ? b->y_high : b->y_low; b->grav = 0;
        wander_start(e); b->t1e4 = b->high ? B_HIGH_T : 0; b->st = 1; break;
    case 1: if (b->t1e4 < 0) b->st = 2; break;
    case 2:                                                          /* notice 0x40f06b */
        if (!t) { b->st = 0; break; }
        b->grav = 0; chase_start(e, pl); b->pl_prev = (Vec3){ 0, 0, 0 }; b->st = 3; break;
    case 3: {                                                        /* chase 0x40f0e9 */
        if (b->high && e->home.y - e->pos.y >= 20.0f) break;         /* first (nearly) up at the high hover height */
        if (!t) { b->st = 0; break; }
        b->pl_prev = b->pl_cur; b->pl_cur = pl->pos;
        if (!b->high) {
            if (cone_touch(e, pl)) {
                if (bite(e, pl)) { b->st = 10; break; }
                audio_fx(k ? 46 : 41, NULL, NULL); b->high = 1; b->st = 0; break;
            }
            if (dist_xz(e->pos, b->pl_cur) < 150.0f) {               /* under / next to him without touching: back off 1300 from the player */
                Vec3 a = { e->home.x - pl->pos.x, e->home.y - pl->pos.y, e->home.z - pl->pos.z }; float L = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); if (L < 1e-3f) L = 1e-3f;
                b->home_save = e->home;
                e->home = (Vec3){ pl->pos.x + a.x / L * 1300.0f, b->y_low, pl->pos.z + a.z / L * 1300.0f };
                wander_start(e); b->leash_save = e->P.leash; e->P.leash = 1.0f; b->st = 4; break;
            }
        }
        if (dist_xz(e->pos, b->pl_cur) < 150.0f) { b->behav = 2; b->shake_i = 0; b->t1d4 = B_SHAKE_T; if (b->high) b->st = 6; }
        break; }
    case 4: case 5: {                                                /* back off 0x40f47d / hang there facing the player 0x40f558 */
        int still = 0;
        if (t) { b->pl_prev = b->pl_cur; b->pl_cur = pl->pos; still = dist3(b->pl_prev, b->pl_cur) < 1.0f; }   /* 0x410c60 */
        if (!t || !still) { e->P.leash = b->leash_save; e->home = b->home_save; b->st = 2; break; }   /* restore 0x40f66f */
        if (b->st == 4) { if (dist3(e->pos, e->home) < 150.0f) b->st = 5; }
        else { b->behav = 2; b->turn = B_TURN_C; e->want_ang = ang_to(e->pos, pl->pos); h_turn(e, dt); }
        break; }
    case 6: {                                                        /* shake over the player 0x40f6bb */
        if (!t) { b->st = 2; break; }
        if (b->t1d4 <= 0) { b->st = 7; b->grav = 1; b->fall_v = 0; break; }   /* gravity on: the drop */
        b->acc += dt; if (b->acc <= 1.0f / 60) break;                /* a fixed 60 Hz step, at most one per frame */
        Vec3 d = { cosf(e->ang), 0, sinf(e->ang) }; int n = b->shake_i++; float s = n == 0 ? -B_SHAKE : n == 3 ? -B_SHAKE : B_SHAKE;
        if (n == 3) b->shake_i = 0;
        Vec3 p = { e->pos.x + d.x * s, e->pos.y, e->pos.z + d.z * s };
        if (b->shake_i & 1) { p.x += (pl->pos.x - p.x) * 0.1f; p.z += (pl->pos.z - p.z) * 0.1f; }   /* 10 % toward the player */
        e->pos = p; while (b->acc > 1.0f / 60) b->acc -= 1.0f / 60;
        break; }
    case 7:                                                          /* the drop 0x40f909 */
        if (!t) { b->st = 0; break; }
        if (b->on_ground) {
            game_cam_shake(1.5f); b->high = 0; b->t1d8 = B_LAND_T; b->st = 8;
            if (!m1(e)) game_land_dust((Vec3){ e->pos.x, e->pos.y - 50, e->pos.z }, (Vec3){ 0, 1, 0 });
            audio_fx(k ? 45 : 40, NULL, NULL);
        }
        e->want_ang = ang_to(e->pos, pl->pos); b->turn = B_TURN_C; h_turn(e, dt);
        e->pos.x += (pl->pos.x - e->pos.x) * 0.01f; e->pos.z += (pl->pos.z - e->pos.z) * 0.01f;   /* 1 % per FRAME */
        if (cone_touch(e, pl)) {
            if (bite(e, pl)) { b->st = 10; break; }
            audio_fx(k ? 46 : 41, NULL, NULL); game_cam_shake(1.5f); b->high = 0; b->t1d8 = B_LAND_T; b->st = 8;
        }
        break;
    case 8: if (b->t1d8 < 0) b->st = 0; break;                       /* after the landing: the low phase begins */
    case 9:                                                          /* hit 0x40fbd1 */
        if (e->hp <= 0) { b->st = 12; break; }
        e->hit_t -= dt; b->behav = 2;
        if (e->hit_t < 0) { b->high = 1; b->st = 0; }
        break;
    case 10:                                                         /* the player is beaten 0x40fcb1 */
        audio_fx(k ? 48 : 43, NULL, NULL); loop_stop(e);
        b->t1e0 = 4.0f; b->st = 11; b->behav = 2; e->want_speed = e->P.walk;
        /* fallthrough */
    case 11: if ((b->t1e0 -= dt) <= 0) { b->high = 1; b->st = 0; } break;
    case 12:                                                         /* beaten 0x40fc27 */
        b->behav = 2; b->grav = 1; if (e->hit_t >= 0) e->hit_t -= dt;
        game_var_set(b->mail_var, 3);                                /* the script: "boss beaten" */
        loop_stop(e); b->mode = 0;
        printf("  BOSS %u beaten: var %u := 3", in->index, b->mail_var & 0xffffff), puts("");
        break;
    }
    sync_link(e);
    game_boss_bar(1, (int)e->hp, (int)e->P.hp);                      /* 0x40fd82 */
    if (getenv("WOODY_BOSSLOG")) printf("  boss st %d high %d mode %d pos %.0f %.0f %.0f hp %.0f rec %d anim %d behav %d act %d", b->st, b->high, b->mode, e->pos.x, e->pos.y, e->pos.z, e->hp, b->rec, in->anim, b->behav, b->w_act), puts("");
}

/* every frame, updated or not: the render colour hook vtbl[26] 0x40fde0 (also the link's, through its owner) and the
 * sound source that stops the loop on the first frame without an update */
void boss_frame_end(Enemy *e)
{
    BossState *b = &e->b;
    if (!b->active) loop_stop(e);
    b->active = 0;
    int red = 0;
    if (b->st == 9 || b->st == 12) {                                 /* the counter goes up once per drawn model: 8 red, 8 normal */
        int nd = 1 + (b->link != NULL); b->blink = (b->blink + nd) % 16; red = b->blink >= 1 && b->blink <= 8;
    }
    e->inst->tint_red = red; if (b->link) b->link->tint_red = red;
}

void enemies_boss_msg(EnemySet *s, Instance *inst, int id, uint32_t arg, Instance *linked)
{
    Enemy *e = NULL; for (int i = 0; i < s->n; i++) if (s->e[i].inst == inst && s->e[i].type == 14) e = &s->e[i];
    if (!e) return;
    if (id == 60) { e->b.mail_var = arg & 0xffffff; return; }        /* 0x4100b0 */
    if (id == 59 && linked) {                                        /* 0x4100d2: one instance, its own AnimCtrl, owner = the boss */
        e->b.link = linked; linked->scripted = 0; e->b.lrec = -1; e->b.lsub = 0;
        player_set_carried(inst, linked);                            /* it moves with the boss: never the boss's floor */
    }
}
