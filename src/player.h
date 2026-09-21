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

struct EnemySet;

/* Jumper = Perso+0x334 (docs/PERSO_JUMP.md 1.1) */
typedef struct {
    int state;                      /* J+0x14: 0 start, 1 rising, 2 grounded, 3 early fall, 4 fall, 5 long fall, 6 landed, 7 apex */
    float D, t, h_prev, v_down;     /* J+0x18, +0x1c, +0x20, +0x24 */
    float fallen, coyote_t, dy;     /* J+0x28, +0x48, J+0xc (vertical displacement this frame) */
    int armed, fell_off, hard_fall, short_hop, coyote;
    int open_window;                /* set when the air attack window opens (0x457560 from the tick) */
} Jumper;

typedef struct Player {
    Instance *inst;                 /* the Woody instance (model 0, instance 0) */
    const GelFile *gel;
    const InsFile *ins;
    Vec3 pos, vel;                  /* pos = feet (instance origin) */
    float yaw;                      /* facing, radians; forward = (sin yaw, 0, cos yaw) */
    float speed;                    /* horizontal speed along the facing direction (Mover RampA, 0x45b110) */
    int ramp_phase; float ramp_t, ramp_target, ramp_v0;   /* 0 idle, 1 accelerating, 2 at target, 3 decelerating */
    int on_ground;
    Jumper jumper;
    /* damage / death / respawn */
    float health; int lives;        /* Perso+0x24c (hearts, max 5), +0x250 */
    int dead_kind; float death_delay, invuln_respawn, invuln_hit;   /* +0x26c, +0x288, +0x270, +0x280 */
    Vec3 push_dir; float push_t, push_speed;                         /* knockback (Mover RampC) */
    int game_state; float game_t, fade; int mask10_frames;           /* Game sequence 0x4459c0; fade = screen brightness */
    Vec3 spawn_pos; float spawn_yaw;
    int bonus_got, bonus_total, bonus_count, special_charges, unique_items, race_bonus;   /* [0x5e54e8], [0x5e54e4], Perso+0x25c, +0x254, +0x260, +0x264 */
    Vec3 ground_n, slide_dir; float slide_speed; int sliding;   /* ground normal (Mover+0xd0) and the slide ramp (RampB) */
    /* attack controller (Perso+0x5b4..): sub-state, timer, displacement, air window, charge; move lock = Perso+0x238 */
    /* peck climbing, Perso state 4 (0x4651d0, docs/OBJECTS.md 1.3): sub 1 grab, 2 climbing, 3 over the top, 4 let go */
    int use_root; Vec3 root_pos;   /* climb-over: pos is frozen, the root track carries the model; camera follows root_pos */
    int climb_sub, climb_act_prev; float grip, regrab, peck_t, over_t, over_len; Vec3 wall_n, over_from, over_to; const Instance *wall_inst;
    int atk; float atk_t; Vec3 atk_dir, atk_disp; int use_atk_disp; float air_win, charge, move_lock, vy_corr; int action_prev;
    struct EnemySet *enemies; void *target; Vec3 dash_start, aim; int has_target;   /* attack targets (Perso+0x5f0, +0x5e4, +0x5d0, +0x5dc) */
    int lanim, lanim_sub;           /* logical animation (table 0x4b6180) and position in its chain */
    float floor_y;                  /* last floor height found under the player */
    int floor_is_hull;              /* floor came from an instance node (press kind 1 or hull kind 4) */
    const Instance *att_inst; uint32_t att_node; Vec3 att_local, att_world;   /* platform attachment (Perso+0x298) */
    uint32_t cur_col;               /* world_collision id currently pressed, 0xffffffff = none (Probe+0x20) */
    /* volume tracking: one flag per (instance, volume node) */
    uint32_t nvol; uint8_t *inside; Instance **vol_inst; uint32_t *vol_node; uint32_t *vol_id;
    /* follow camera state */
    float cam_yaw; Vec3 cam_pos; int cam_init;
    Vec3 cam_tprev; float cam_drop, cam_quick_t; int cam_behind_prev;   /* previous target, look-point drop while airborne, action 0xa */
    /* death / hit animations, scripted door actions (docs/PERSO_DEATH.md) */
    float dead_T, nograv_t, hit_anim_t; int dead_ground, hit_anim, dead_cam_req;
    int script_act; float script_t; int script_faded, fade_req, cam_end_req, cam_cut_req;   /* fade_req: 1 = fade out 0.5 s, 2 = fade in 0.5 s; cam_cut_req: 0x458f90, hard cut behind him; cam_end_req: 0x44e5a0, back to the follow camera (all consumed by the app) */
    /* Perso state 8: riding a class-20 rocket (docs/ROCKET.md 6). The app fills ride_state / ride_seat / ride_q before the update */
    Instance *ride; int ride_state; Vec3 ride_seat, ride_p0; Quat ride_q, ride_q0, ride_cur; float ride_t; int ride_jprev, ride_aprev;
    /* statistics */
    uint32_t events_sent;
} Player;

int  player_init(Player *p, InsFile *ins, const GelFile *gel);
void player_bind(Player *p, Instance *inst);           /* SetTypeInstance 1/2/3/18/19: this instance is the player */
void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw);
void player_camera(Player *p, FreeCamera *cam, float dt, int behind_key);   /* behind_key = action 0xa */
void player_camera_reset(Player *p);                   /* SetMode(0, 0) / message 500: put the camera behind the player now */
int  volume_contains(const Instance *inst, uint32_t node, Vec3 p);   /* 0x4300c0: is the point inside this volume node of the instance? */
void player_free(Player *p);
/* GetHeight for other actors: ground under pt, ignoring the instance `skip` */
float player_ground_query(const Player *p, const Instance *skip, Vec3 pt, int *found);
int  player_collect(Player *p, int type, int arg);      /* bonus classes 30, 34..38: message 10; returns 1 when the instance must disappear */
void player_script_hold(Player *p, float t);       /* message 1040: scripted action, control taken away for t s */
void player_place(Player *p, Vec3 pos, float yaw);     /* Perso reset + SetPos + SetFacing (end of a cinematic, hub door) */
void player_kill(Player *p, int kind);
Quat q_slerp(Quat a, Quat b, float u);
int  player_mount(Player *p, Instance *obj);           /* 0x465740: only in state 0 on the ground */
void player_teleport(Player *p, Vec3 pos, int have_dir, Vec3 dir);   /* message 26 (0x44ce11); the caller leaves all volumes in the VM */
void player_script_action(Player *p, int act, int have, Vec3 p0, Vec3 dir);   /* message 1040 (0x44dda0): 17 = into a door, 18 = out of it */
int  player_segment_blocked(const Player *p, Vec3 a, Vec3 b);   /* world polygons only */
float gel_ray_frac(const GelFile *g, Vec3 a, Vec3 b);   /* first world polygon hit on a->b as a fraction 0..1, or 2 when nothing is hit */                 /* Perso vt[38] */
int  player_hit(Player *p, float damage, Vec3 dir);    /* Perso vt[39]; returns 1 when the caller should Kill(3) */

/* world queries (brute force over the .gel polygons) */
float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found);

#endif
