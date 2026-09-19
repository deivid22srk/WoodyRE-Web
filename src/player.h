/* player.h - player controller for the WoodyRE engine, ported from the decompiled Perso class.
 *
 * Ported (docs/PERSO_FRAME.md, PERSO_MOVE.md, PERSO_JUMP.md, CAMERA.md, EVENTS.md): Mover walk ramp and turn blend,
 * Jumper state machine (time parabolas, short hop, coyote time, terminal speed), sub-stepped cylinder sweep with
 * ground clinging and the feet+43 ground probe, animations per state, follow camera (mode 1), and the engine->VM
 * events (trigger volumes, world_collision press nodes, msgmask 0x200).
 * Attacks (0x457a50): peck dash, rebounds, charge run and brake; logical animation chains (table 0x4b6180).
 * Not ported yet: attack targets/hits/recoil (no actors yet), peckable surfaces, ducking, look-around, sliding on
 * steep slopes, ground kinds, platform carry, damage/death, camera breadcrumb path, cfg key mapping.
 * Geometry queries are brute force over the .gel polygons instead of the original kd-tree cells. */
#ifndef WOODY_PLAYER_H
#define WOODY_PLAYER_H
#include "level.h"
#include "render_gl.h"
#include "ekovm.h"

typedef struct {
    int forward, back, left, right, jump, action;   /* current key state */
    float cam_turn;                                  /* -1..1 manual camera orbit */
} PlayerInput;

/* Jumper = Perso+0x334 (docs/PERSO_JUMP.md 1.1) */
typedef struct {
    int state;                      /* J+0x14: 0 start, 1 rising, 2 grounded, 3 early fall, 4 fall, 5 long fall, 6 landed, 7 apex */
    float D, t, h_prev, v_down;     /* J+0x18, +0x1c, +0x20, +0x24 */
    float fallen, coyote_t, dy;     /* J+0x28, +0x48, J+0xc (vertical displacement this frame) */
    int armed, fell_off, hard_fall, short_hop, coyote;
    int open_window;                /* set when the air attack window opens (0x457560 from the tick) */
} Jumper;

typedef struct {
    Instance *inst;                 /* the Woody instance (model 0, instance 0) */
    const GelFile *gel;
    const InsFile *ins;
    Vec3 pos, vel;                  /* pos = feet (instance origin) */
    float yaw;                      /* facing, radians; forward = (sin yaw, 0, cos yaw) */
    float speed;                    /* horizontal speed along the facing direction (Mover RampA, 0x45b110) */
    int ramp_phase; float ramp_t, ramp_target, ramp_v0;   /* 0 idle, 1 accelerating, 2 at target, 3 decelerating */
    int on_ground;
    Jumper jumper;
    /* attack controller (Perso+0x5b4..): sub-state, timer, displacement, air window, charge; move lock = Perso+0x238 */
    int atk; float atk_t; Vec3 atk_dir, atk_disp; int use_atk_disp; float air_win, charge, move_lock, vy_corr; int action_prev;
    int lanim, lanim_sub;           /* logical animation (table 0x4b6180) and position in its chain */
    float floor_y;                  /* last floor height found under the player */
    int floor_is_hull;              /* floor came from an instance node (press kind 1 or hull kind 4) */
    uint32_t cur_col;               /* world_collision id currently pressed, 0xffffffff = none (Probe+0x20) */
    /* volume tracking: one flag per (instance, volume node) */
    uint32_t nvol; uint8_t *inside; Instance **vol_inst; uint32_t *vol_node; uint32_t *vol_id;
    /* follow camera state */
    float cam_yaw; Vec3 cam_pos; int cam_init;
    Vec3 cam_tprev; float cam_drop, cam_quick_t; int cam_behind_prev;   /* previous target, look-point drop while airborne, action 0xa */
    /* statistics */
    uint32_t events_sent;
} Player;

int  player_init(Player *p, InsFile *ins, const GelFile *gel);
void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw);
void player_camera(Player *p, FreeCamera *cam, float dt, int behind_key);   /* behind_key = action 0xa */
void player_free(Player *p);

/* world queries (brute force over the .gel polygons) */
float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found);

#endif
