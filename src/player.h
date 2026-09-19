/* player.h - provisional player controller for the WoodyRE engine.
 *
 * This is NOT yet the decompiled Perso class (see docs/PERSO_*.md when available); it is a
 * placeholder that gives the level a controllable Woody so the engine->VM events (trigger
 * volumes) can be exercised: camera-relative movement, gravity, floor/wall collision against
 * the .gel polygons and a follow camera. Constants are guesses in world units (Woody is ~300 tall). */
#ifndef WOODY_PLAYER_H
#define WOODY_PLAYER_H
#include "level.h"
#include "render_gl.h"
#include "ekovm.h"

typedef struct {
    int forward, back, left, right, jump, action;   /* current key state */
    float cam_turn;                                  /* -1..1 manual camera orbit */
} PlayerInput;

typedef struct {
    Instance *inst;                 /* the Woody instance (model 0, instance 0) */
    const GelFile *gel;
    const InsFile *ins;
    Vec3 pos, vel;                  /* pos = feet (instance origin) */
    float yaw;                      /* facing, radians; forward = (sin yaw, 0, cos yaw) */
    int on_ground;
    float floor_y;                  /* last floor height found under the player */
    int floor_is_hull;              /* floor came from an instance collision hull (kind 4 node) */
    /* volume tracking: one flag per (instance, volume node) */
    uint32_t nvol; uint8_t *inside; Instance **vol_inst; uint32_t *vol_node; uint32_t *vol_id;
    /* follow camera state */
    float cam_yaw; Vec3 cam_pos; int cam_init;
    /* statistics */
    uint32_t events_sent;
} Player;

int  player_init(Player *p, InsFile *ins, const GelFile *gel);
void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw);
void player_camera(Player *p, FreeCamera *cam, float dt);
void player_free(Player *p);

/* world queries (brute force over the .gel polygons) */
float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found);

#endif
