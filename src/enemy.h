/* enemy.h - enemy classes (docs/ENEMY.md): types 4/5/6 (patrol / wander, notice, chase, dash attack, brake, hit, death with
 * fade) and the shooters 7/8/9 (section 8: wait, aim, fire, short dash with a bite, type 9 dodges the air dive). */
#ifndef WOODY_ENEMY_H
#define WOODY_ENEMY_H
#include "level.h"

struct Player;

/* parameter block P (0x41d510); message 11 writes into it. shot_visual = P+0x74 (0/1 missile, 2 bolt, 3 fireball),
 * shot_fx = the SoundFx that 0x449130 plays for that visual */
typedef struct { float radius, height, walk, run, dash, see, dy, turn, turn_fast, leash, hp, cool, bite, shot_dmg, reload, melee, dodge, steer, active_d; int shot_visual, shot_fx; } EnemyParams;

typedef struct Enemy {
    EnemyParams P; float reload; Vec3 warn, dodge_dir; int throw_hold;   /* shooters */
    int hand;                                                            /* ghost (type 13): fires from alternating hands */
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

void enemies_add(EnemySet *s, Instance *inst, int type);                      /* on SetTypeInstance 4/5/6 */
void enemies_update(EnemySet *s, struct Player *pl, Vec3 cam_pos, float dt);
/* vtbl[39] 0x419480: returns 1 when the enemy died. dir = (0,0,0) for a peck (no knockback). */
int  enemy_take_damage(Enemy *e, float dmg, Vec3 dir);
void enemies_msg11(EnemySet *s, Instance *inst, int n, int v);                /* Enemy::HandleMsg 0x41a740, id 11 */
void enemy_warn_dive(Enemy *e, Vec3 d);                                       /* vtbl[37] 0x417ee0: the player starts an air dive at this enemy */
void game_enemy_stars(Enemy *e);                                              /* vtbl[57] 0x41b000 -> 0x477610: five stars circle over the dying enemy (in main_engine.c) */
void enemy_player_killed(Enemy *e);                                           /* vtbl[41] 0x417fd0: its projectile killed the player */
/* implemented by the engine: projectile 0x4490a0 from an enemy (template 1 with the P overrides) */
void game_enemy_shot(Enemy *owner, Vec3 pos, Vec3 dir, float speed, float damage, float steer, int visual, int sound_fx);
float enemy_radius(const Enemy *e); float enemy_height(const Enemy *e);

#endif
