/* player.h - player controller for the WoodyRE engine, ported from the decompiled Perso class.
 *
 * Ported (docs/PERSO_FRAME.md, PERSO_MOVE.md, PERSO_JUMP.md, CAMERA.md, EVENTS.md): Mover walk ramp and turn blend,
 * Jumper state machine (time parabolas, short hop, coyote time, terminal speed), sub-stepped cylinder sweep with
 * ground clinging and the feet+43 ground probe, animations per state, follow camera (mode 1), and the engine->VM
 * events (trigger volumes, world_collision press nodes, msgmask 0x200).
 * Attacks (0x457a50): peck dash, rebounds, charge run and brake; logical animation chains (table 0x4b6180).
 * Ducking (action 5, 0x465b10, docs/PERSO_DUCK.md) is ported, and so is the follow camera's breadcrumb trail (0x423ab0).
 * Not ported yet: look-around, cfg key mapping. The ground type of the floor (Perso+0x308) is read, but only the
 * footstep effect uses it: the slippery turn ramp of type 1 is not ported. */
#ifndef WOODY_PLAYER_H
#define WOODY_PLAYER_H
#include "level.h"
#include "render_gl.h"
#include "ekovm.h"

typedef struct {
    int forward, back, left, right, jump, action, duck, special;   /* current key state; duck = action 5 (docs/PERSO_DUCK.md), special = action 11 (docs/PERSO_SPECIAL.md) */
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
    const TexFile *tex;             /* the level's textures: the ground type byte of the floor polygon's group */
    Vec3 pos, vel;                  /* pos = feet (instance origin) */
    float yaw;                      /* facing, radians; forward = (sin yaw, 0, cos yaw) */
    float speed;                    /* horizontal speed along the facing direction (Mover RampA, 0x45b110) */
    int ramp_phase; float ramp_t, ramp_target, ramp_v0;   /* 0 idle, 1 accelerating, 2 at target, 3 decelerating */
    int on_ground;
    int steep_edge;                 /* Perso+0x234, the ledge sensor 0x44b2e0: the floor drops away ahead (docs/OBSTACLE.md 2) */
    Jumper jumper;
    /* damage / death / respawn */
    float health; int lives;        /* Perso+0x24c (hearts, max 5), +0x250 */
    int dead_kind; float death_delay, invuln_respawn, invuln_hit;   /* +0x26c, +0x288, +0x270, +0x280 */
    Vec3 push_dir; float push_t, push_speed;                         /* knockback (Mover RampC) */
    int game_state; float game_t; int mask10_frames;                 /* Game sequence 0x4459c0 */
    float iris_from, iris_to, iris_dur, iris_t, iris; int iris_on;  /* its iris Game+4 (0x4776b0); iris_on = drawn this tick, iris = its value */
    Vec3 spawn_pos; float spawn_yaw;
    int bonus_got, bonus_total, bonus_count, special_charges, unique_items, race_bonus, race_total;   /* [0x5e54e8], [0x5e54e4], Perso+0x25c, +0x254, +0x260, +0x264, [0x5e54f4] */
    Vec3 ground_n, slide_dir; float slide_speed; int sliding;   /* ground normal (Mover+0xd0) and the slide ramp (RampB) */
    int ground_kind;                /* Perso+0x308 (0x4628e0): 0 normal, 1 slippery, 2 dust/sand/snow (docs/PERSO_MOVE.md 6.4) */
    float step_u;                   /* footsteps (docs/FOOTSTEPS.md): the fraction of the walk cycle at the previous frame, -1 = not walking */
    /* attack controller (Perso+0x5b4..): sub-state, timer, displacement, air window, charge; move lock = Perso+0x238 */
    /* peck climbing, Perso state 4 (0x4651d0, docs/OBJECTS.md 1.3): sub 1 grab, 2 climbing, 3 over the top, 4 let go */
    int use_root; Vec3 root_pos;   /* climb-over: pos is frozen, the root track carries the model; camera follows root_pos */
    int climb_sub, climb_act_prev; float grip, regrab, peck_t, over_t, over_len; Vec3 wall_n, over_from, over_to; const Instance *wall_inst;
    int atk; float atk_t; Vec3 atk_dir, atk_disp; int use_atk_disp; float air_win, charge, move_lock, vy_corr; int action_prev;
    struct EnemySet *enemies; void *target; Vec3 dash_start, aim; int has_target;   /* attack targets (Perso+0x5f0, +0x5e4, +0x5d0, +0x5dc) */
    int lanim, lanim_sub;           /* logical animation (table 0x4b6180) and position in its chain */
    int special_st, special_prev; float special_t;   /* special attack 0x458bf0: +0x750 (0 free, 1 charging up to the hit at 1.5 s, 2 after the hit), key state, +0x74c */
    int duck, duck_anim; float duck_t;   /* ducking 0x465b10: sub-state +0x694 (0 up, 1 going down, 2 down, 3 getting up), its logical anim, timer +0x698 */
    float floor_y;                  /* last floor height found under the player */
    int floor_is_hull;              /* floor came from an instance press node (kind 1) */
    const Instance *att_inst; uint32_t att_node; Vec3 att_local, att_world;   /* platform attachment (Perso+0x298) */
    uint32_t cur_col;               /* world_collision id currently pressed, 0xffffffff = none (Probe+0x20) */
    /* volume tracking: one flag per (instance, volume node) */
    uint32_t nvol; uint8_t *inside; Instance **vol_inst; uint32_t *vol_node; uint32_t *vol_id;
    /* follow camera state */
    float cam_yaw; Vec3 cam_pos; int cam_init;
    Vec3 cam_tprev; float cam_drop, cam_quick_t; int cam_behind_prev;   /* previous target, look-point drop while airborne, action 0xa */
    int cam_state, cam_n, cam_seg; float cam_u; Vec3 cam_pad[100];     /* C+0x2a4 (2 = target hidden, follow the breadcrumbs), C+0x7a0/+0x7a4/+0x7a8, trail C+0x2f0 */
    /* death / hit animations, scripted door actions (docs/PERSO_DEATH.md) */
    float dead_T, nograv_t, hit_anim_t; int dead_ground, hit_anim, dead_cam_req;
    int script_act, script_log; float script_t, script_total; int script_faded, fade_req, cam_end_req;   /* fade_req: 1 = fade out 0.5 s, 2 = fade in 0.5 s; cam_end_req: 0x44e5a0, back to the follow camera (both consumed by the app) */
    /* idle 0x464500: +0x230 seconds standing still, +0x530 which idle variation, +0x52c the zzz bubble is up (its live flag);
     * idle_hold = Perso state != 0 (results, title, frozen by a cinematic camera): the timer neither runs nor resets (set by the app) */
    float idle_t; int idle_var, sleep_bubble, idle_hold;
    int respawn_req;                                                    /* respawn 0x445930 happened: side view off + camera reset 0x458f90 (consumed by the app) */
    /* Perso state 8: riding a class-20 rocket (docs/ROCKET.md 6). The app fills ride_state / ride_seat / ride_q before the update */
    /* race levels (script types 18/19, docs/RACE.md): message 1120 hangs the board under the Perso (+0x4b4) and gives the track polyline (+0x4b0) */
    Instance *board; const Trajectory *race_path;
    /* Perso state 1 = riding (subtype 4/5): sub-state +0x4a8 (0 wait for the path, 1 ride, 2 crash), ride direction M+0x1c,
     * start-anim timer +0x4e4, boost +0x4c4.., stuck counter +0x4ac, lean +0x4bc/+0x4c0, crouch +0x694, up filter +0x210 */
    int race_char, race_sub, race_stuck, race_lean, race_crouch, has_ckpt;
    float race_start_t, race_lean_t, race_crouch_t, race_crash_t, boost_t, boost_speed;
    Vec3 race_dir, boost_target, race_upf;
    int race_cam_req;                                                   /* sub-state 0: hard cut to the follow camera (consumed by the app) */
    float cam_dist, cam_height, cam_zoom;                               /* follow camera C+0x7e0 (400), C+0x7d8 (180), zoom cam+0x678 (1.2) */
    /* Perso state 6, carrying a bomb (docs/BOMB_CARRY.md 1): +0x590 the bomb, +0x594 in his hands, +0x58c sub-state, +0x598 its
     * timer; carry_pressed = attack just pressed this frame (read by the sub-states), throw_hold = port: the throw animation
     * keeps playing after the release (the original does that with animation priorities) */
    struct Bomb *bomb; int carrying, bsub, carry_pressed; float bt, throw_hold;
    Instance *ride; int ride_state; Vec3 ride_seat, ride_p0; Quat ride_q, ride_q0, ride_cur; float ride_t; int ride_jprev, ride_aprev;
    /* statistics */
    float play_time;                /* Perso+0x710 accumulator (0x453ca0): seconds played in this level, one of the five result stats */
    uint32_t events_sent;
} Player;

int  player_init(Player *p, InsFile *ins, const GelFile *gel, const TexFile *tex);
void player_bind(Player *p, Instance *inst);           /* SetTypeInstance 1/2/3/18/19: this instance is the player */
void player_update(Player *p, const PlayerInput *in, float dt, EkoVM *vm, float cam_yaw);
void player_game_tick(Player *p, EkoVM *vm, float dt); /* 0x4459c0: level-start iris, death -> iris closes -> respawn -> iris opens */
void player_camera(Player *p, FreeCamera *cam, float dt, int behind_key);   /* behind_key = action 0xa */
void player_camera_reset(Player *p);                   /* SetMode(0, 0) / message 500: put the camera behind the player now */
int  volume_contains(const Instance *inst, uint32_t node, Vec3 p);   /* 0x4300c0: is the point inside this volume node of the instance? */
void player_free(Player *p);
void player_boost(Player *p, Vec3 p0, Vec3 dir, float speed, float dur);   /* message 1121 StartBoostSurf 0x456000 */
void player_sync_board(Player *p);                    /* 0x44bf10 tail + 0x463e60: the race board takes the Perso's placement and animation */
/* GetHeight for other actors: ground under pt, ignoring the instance `skip` */
float player_ground_query(const Player *p, const Instance *skip, Vec3 pt, int *found);
float player_body_height(const Player *p);            /* 0x462490 -> P+0x08: 193 standing / 61 ducked (Woody), race 160 / 81 */
void player_set_carried(const Instance *owner, const Instance *follower);   /* follower moves with owner: a query that skips owner skips it too */
/* landing ring (docs/PERSO_JUMP.md 5): the floor point and its normal under an airborne Woody, 0 = draw nothing */
int  player_landing_ring(const Player *p, Vec3 *pos, Vec3 *normal);
int  player_collect(Player *p, int type, int arg);      /* bonus classes 30, 34..38: message 10; returns 1 when the instance must disappear */
void player_script_hold(Player *p, float t);       /* message 1040: scripted action, control taken away for t s */
void player_place(Player *p, Vec3 pos, float yaw);     /* Perso reset + SetPos + SetFacing (end of a cinematic, hub door) */
void player_ground_snap(Player *p);                    /* 0x462990: onto the floor under feet + 43, on the ground, Jumper reset */
void player_kill(Player *p, int kind);
Quat q_slerp(Quat a, Quat b, float u);
int  player_mount(Player *p, Instance *obj);           /* 0x465740: only in state 0 on the ground */
int  player_state_free(const Player *p);               /* Perso state +0x21c == 0: he has his own controls (0x44bcf0) */
void player_brake_charge(Player *p);                   /* 0x458e40: message 1042 stops the charge run the release just started */
void player_teleport(Player *p, Vec3 pos, int have_dir, Vec3 dir);   /* message 26 (0x44ce11); the caller leaves all volumes in the VM */
/* message 1040 / 1140 (0x44dda0): the action number IS the raw .ins animation. 17 = into a door, 18 = out of it;
 * 10..16, 19 and 72..78 (the results animations) run with the root motion of 0x44e290. */
void player_script_action(Player *p, int act, int have, Vec3 p0, Vec3 dir);
int  player_segment_blocked(const Player *p, Vec3 a, Vec3 b);   /* world polygons only */
Vec3 player_sphere_push(const Player *p, const Instance *skip, Vec3 c, float r);   /* 0x407340: world + instance press nodes */
float gel_ray_frac(const GelFile *g, Vec3 a, Vec3 b);   /* first world polygon hit on a->b as a fraction 0..1, or 2 when nothing is hit */                 /* Perso vt[38] */
int  player_ray_instances(const Player *p, const Instance *skip, Vec3 a, Vec3 b, float *frac, Vec3 *n_out, const Instance **inst_out);   /* ray 0x4359b0, instance part (hit kind 2): press-node polygons */
float gel_ray_hit(const GelFile *g, Vec3 a, Vec3 b, Vec3 *n_out);   /* the same, and the normal of that polygon, turned towards a */
int  player_ray_endless(const Player *p, Vec3 a, Vec3 dir, float *t);   /* 0x497a30: 1 nothing, 3 world polygon, 4 instance press node; *t in units of dir */
int  player_hit(Player *p, float damage, Vec3 dir);    /* Perso vt[39]; returns 1 when the caller should Kill(3) */

/* footstep effects, drawn by the app (main_engine.c) as the pickup effects are (docs/FOOTSTEPS.md)
 * 0x47cba0(pos, ground normal, direction, foot 0/1, kind 2 or 3) twice per walk cycle, and the landing
 * dust 0x476140(pos + (0,30,0), &ground normal, 3, 0.25, 1.5) on ground type 2. */
void game_footstep(Vec3 pos, Vec3 normal, Vec3 dir, int foot, int kind);
void game_land_dust(Vec3 pos, Vec3 normal);
void game_splash(Vec3 c, float speed, float radius);   /* 0x478660, docs/SPLASH.md */
void game_special_fx(void);                            /* 0x47ab90: the streaks and fire rings of the special attack (docs/PERSO_SPECIAL.md 3) */
int  game_enemy_thinks(const Instance *inst);           /* is the actor in a sector drawn last frame, i.e. did its Think run (list 0x4c5258)? */
/* the comic speech bubble 0x478980(inst, kind, duration, offY, offX, live) (docs/PERSO_DEATH.md 4.1): kind 0 "?!" (Kill 1),
 * 1 curse (hard landing), 2 "$", 3 "...", 4 "zzz"; with `live` it lasts while *live != 0 instead of `duration` */
void game_bubble(Instance *inst, int kind, float dur, float offy, float offx, const int *live);
/* and the beak impact 0x479c80(kind, point, normal), on the same primitives (docs/OBJECTS.md 1.6):
 * kind 1 = a hit of the attack probe (no normal), 0 = the wall he is climbing */
void game_peck_fx(int kind, Vec3 pos, const Vec3 *n);
/* bombs (main_engine.c, docs/BOMB.md): pick one up (0x463430: in use, not ridden, within r of pos in 3D; it is held from now on),
 * hold it in the hand (0x463530 part A), and start its projectile again from where it is (0x44d3a0: the throw and the drop) */
struct Bomb;
struct Bomb *game_bomb_pick(Vec3 pos, float r);
void game_bomb_hold(struct Bomb *b, Vec3 pos, Quat q);
void game_bomb_launch(struct Bomb *b, Vec3 dir, float speed);

/* world queries (brute force over the .gel polygons) */
float gel_floor_below(const GelFile *g, Vec3 p, float step_up, float max_drop, int *found);

#endif
