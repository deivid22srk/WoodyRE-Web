/* enemy.h - enemy classes, script types 4/5/6 (docs/ENEMY.md): patrol / wander, notice, chase, dash attack,
 * brake, hit, death with fade. Types 7..13 are not ported. */
#ifndef WOODY_ENEMY_H
#define WOODY_ENEMY_H
#include "level.h"

struct Player;

typedef struct {
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
float enemy_radius(const Enemy *e); float enemy_height(const Enemy *e);

#endif
