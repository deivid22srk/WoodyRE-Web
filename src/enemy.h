/* enemy.h - enemy classes (docs/ENEMY.md): types 4/5/6 (patrol / wander, notice, chase, dash attack, brake, hit, death with
 * fade) and the shooters 7/8/9 (section 8: wait, aim, fire, short dash with a bite, type 9 dodges the air dive). */
#ifndef WOODY_ENEMY_H
#define WOODY_ENEMY_H
#include "level.h"

struct Player;

/* parameter block P (0x41d510); message 11 writes into it. shot_visual = P+0x74 (0/1 missile, 2 bolt, 3 fireball),
 * shot_fx = the SoundFx that 0x449130 plays for that visual */
typedef struct { float radius, height, walk, run, dash, see, dy, turn, turn_fast, leash, hp, cool, bite, shot_dmg, reload, melee, dodge, steer, active_d; int shot_visual, shot_fx;
                 float fall_g; } EnemyParams;                                  /* P+0: gravity of the ground follower (200; the boss sets 400/800) */

/* class 14, the Buzz boss (docs/BOSS14.md): fields beyond the Enemy base */
typedef struct BossState {
    int mode;                       /* +0x228: 0 off (only reads its mailbox), 1 W1B flying machine, 2 W2D/W3D/WWS hopping */
    uint32_t mail_var;              /* +0x230: script variable of message 60, the command mailbox */
    Instance *link;                 /* +0x234: the coupled instance of message 59 (W1B 404, type 17), carried along */
    int st, high, behav;            /* +0x1c0 state 0..12, +0x224 high phase (invulnerable), active behaviour 0 wander 1 chase 2 still */
    float t1d4, t1d8, t1e0, t1e4, acc;   /* shake / landing / cheer / high-wait timers, 60 Hz accumulator of the shake */
    int shake_i;                    /* +0x1e8 */
    float y_high, y_low; int y_low_ok;   /* +0x214 / +0x218 hover heights */
    Vec3 pl_prev, pl_cur, home_save; float leash_save;
    int bob_down; float bob, bob_max;    /* mode 2 hops */
    int grav, on_ground; float fall_v;   /* flag 4 (falling) and flag 1 (on the ground), the base ground follower's per-frame speed */
    float turn;                     /* H turn rate (rad/s) */
    int w_act; float w_t;           /* Dwalen: current action (-1 none) and its remaining time */
    int c_run; float c_turn_t;      /* Achtervolgen: running, turn-in-place timer */
    float knock[3];                 /* each behaviour's own knockback timer (Behav+0x1c): a peck gives no direction, so it only freezes the step */
    int rec, sub, lrec, lsub;       /* AnimCtrl +0x1c4 and the link's +0x1c8: record and position in its chain */
    int blink, loop_on, active;     /* render-colour counter [0x4c5348], sound loop 39/44 playing, updated this frame */
} BossState;

typedef struct Enemy {
    EnemyParams P; float reload; Vec3 warn, dodge_dir; int throw_hold;   /* shooters */
    BossState b;                                                         /* type 14 */
    int hand;                                                            /* ghost (type 13): fires from alternating hands */
    int nlong, big_touch, idle_a, done; float idle_t, melee_t, windup;          /* bomb thrower (type 12): +0x200, +0x1fc, +0x1f8, +0x1f4, +0x1d4, +0x1f0 */
    Instance *inst; int type;
    int st;                         /* state machine 0x418cf0: 0 patrol, 1 chase, 2 notice, 3 miss, 4 dash, 6 brake, 8 wander, 9 hit, 11 win, 12 dead */
    Vec3 pos, home; float ang;      /* ang: movement angle, direction = (cos, 0, sin) */
    float speed, want_speed, turn_t, cool, hit_t, knock_t, atk_t, t, dead_t, hp, vfall;
    Vec3 knock_dir;
    int attackable, removed, chasing;
    int wander_walk; float wander_t, want_ang;   /* wander: alternating idle / walk actions */
    uint32_t path_to; int path_dir;              /* patrol along the instance TRAJ */
} Enemy;

#define MAX_ENEMIES 256
typedef struct EnemySet { Enemy e[MAX_ENEMIES]; int n; } EnemySet;

void enemies_add(EnemySet *s, Instance *inst, int type);                      /* on SetTypeInstance 4..9, 12, 13, 14 */
void enemies_update(EnemySet *s, struct Player *pl, Vec3 cam_pos, float dt);
/* vtbl[39] 0x419480: returns 1 when the enemy died. dir = (0,0,0) for a peck (no knockback). */
int  enemy_take_damage(Enemy *e, float dmg, Vec3 dir);
int  enemy_hit(Enemy *e, float dmg, Vec3 dir, Vec3 pt, int kind);              /* vtbl[39] with the hit point: Enemy_TakeDamage 0x41adc0 puts the hit star 0x4750e0 on pt (not for Buzz on kind 2) */
void game_hit_star(Vec3 pt);                                                  /* 0x4750e0: flash + 8 sparks with stars (in main_engine.c) */
void enemies_blast(EnemySet *s, Vec3 c, float r);                          /* bomb blast 0x44d650: every enemy within r dies (vtbl[40] 0x41ae20) */
void enemies_msg11(EnemySet *s, Instance *inst, int n, int v);                /* Enemy::HandleMsg 0x41a740, id 11 */
void enemy_warn_dive(Enemy *e, Vec3 d);                                       /* vtbl[37] 0x417ee0: the player starts an air dive at this enemy */
void game_enemy_stars(Enemy *e);                                              /* vtbl[57] 0x41b000 -> 0x477610: five stars circle over the dying enemy (in main_engine.c) */
void enemy_player_killed(Enemy *e);                                           /* vtbl[41] 0x417fd0: its projectile killed the player */
/* implemented by the engine: projectile 0x4490a0 from an enemy (template 1 with the P overrides) */
void game_enemy_shot(Enemy *owner, Vec3 pos, Vec3 dir, float speed, float damage, float steer, int visual, int sound_fx);
float enemy_radius(const Enemy *e); float enemy_height(const Enemy *e);
void enemy_place(Enemy *e);                                                   /* vtbl[44] 0x41a680: pos and the H angle into the instance placement */

/* type 12, the bomb thrower (W2B end boss, docs/ENEMY2.md 4) */
Enemy *enemies_bomb_contact(EnemySet *s, const Enemy *owner, Vec3 a, Vec3 b, float r);   /* HitActors 0x44a0a0 for a bomb: the first thrower whose cylinder the swept sphere touches */
int  game_enemy_bomb(Enemy *e, Vec3 pos, Vec3 dir, float speed, float fuse);  /* Fire 0x411e80: 0 = no free bomb in the pool (in main_engine.c) */
void game_msgmask(Instance *in, uint32_t bits, int on);                      /* MsgMask_Set 0x443e50 / _Clear 0x443e90 on the instance's script object (in main_engine.c) */

/* class 14, the Buzz boss (boss.c, docs/BOSS14.md) */
void boss_init(Enemy *e);                                                     /* ctor 0x40eb50 + PostLoad 0x40ec50 + factory Reset */
void boss_update(Enemy *e, struct Player *pl, Vec3 cam, float dt);            /* Think 0x41a320 -> Update 0x40eec0 */
int  boss_take_damage(Enemy *e);                                              /* vtbl[39] 0x40fe90: always 1 hp, only in the low phase */
void boss_reset(Enemy *e);                                                    /* vtbl[17] 0x40ed90 */
void boss_frame_end(Enemy *e);                                                /* sound source 0x468e50: the loop stops on the first frame without an update */
void enemies_boss_msg(EnemySet *s, Instance *inst, int id, uint32_t arg, Instance *linked);   /* vtbl[22] 0x410070: 59 couple, 60 mailbox */
/* implemented by the engine */
int  game_var_get(uint32_t var);
void game_var_set(uint32_t var, int v);                                       /* SetVar 0x443ca0: wakes the watchers */
void game_cam_shake(float t);                                                 /* 0x41fbb0 */
void game_boss_bar(int on, int cur, int max);                                 /* 0x4484d0 */
void game_explosion(Vec3 p);                                                  /* 0x477060 kind 1 (two flash records) */
void game_boss_smoke(Instance *link, int n, int on);                          /* on: explosion 0x477060 + smoke plume 0x475f30 on marker typecode 0 nr n; n = -1, on = 0: all plumes off */

#endif
