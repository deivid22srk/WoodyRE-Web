/* main_engine.c - WoodyRE native engine prototype: loads a level (.gel/.tex/.ins + code), runs the
 * EKO CODE script VM every frame and renders world + instances with OpenGL.
 *
 * usage: woody.exe <Data dir> <LVL>            e.g. woody.exe extract/Data W1A
 * keys: arrows/WASD = walk Woody (camera-relative), Space = jump, F5 = toggle free-fly camera
 *       (in fly mode WASD + right mouse = fly, Shift = fast), F1 world, F2 instances, F3 wireframe,
 *       [ ] = previous/next animation of the selected instance (default: Woody), Tab = next instance,
 *       P = pause VM, Esc = quit. Script messages are printed to the console.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "level.h"
#include "render_gl.h"
#include "ekovm.h"
#include "player.h"
#include "instance.h"
#include "enemy.h"
#include "audio.h"
#include "hud.h"

static InsFile g_ins;
static int g_log_msgs = 1;
static Player *g_player;
static EnemySet g_enemies;
static float g_now;                                  /* game time in seconds (VM time base) */
/* messages 12/13 wait for the running animation to end: offered again every frame (max 32 in the original, 0x4012f0 clears) */
static EkoMsg g_retry[32]; static int g_nretry;
static float inst_yaw(const Instance *in) { Vec3 f = mat4_apply(&in->world, (Vec3){ 0, -1, 0 }); return atan2f(f.x - in->position.x, f.z - in->position.z); }   /* as player_bind */
static Instance *slot_instance(uint32_t ref) { if ((ref >> 24) > 1) return NULL;   /* scripts also use bare slot numbers; the dispatcher 0x401370 only masks & 0xffffff */
    uint32_t i = ref & 0xffffff; return i < g_ins.nslots + 16 ? g_ins.slots[i] : NULL; }

/* level table 0x4b12a0 (docs/GAMEFLOW.md 1): 0 House (menu backdrop), 1 WWS hub, 2..10 Woody, 11 KWS hub, 12..17 Knothead,
 * 18 SWS hub, 19..24 Splinter, 25 BlackBox, 26 Credits, 27 dev slot, 28 Lang */
static const char *k_levels[29] = { "House", "WWS", "W1A", "W1B", "W2A", "W2B", "W2D", "W3A", "W3B", "W3C", "W3D", "KWS", "K1A", "K1R", "K2A", "K2R", "K3A", "K3R",
                                    "SWS", "S1A", "S1R", "S2A", "S2R", "S3A", "S3R", "BlackBox", "Credits", "BlackBox", "Lang" };
static int g_level = 27, g_prev_level = 27;           /* app+0x68 / +0x6c (init 0x1b) */
/* RequestLevel 0x404b60(fade_s, level, state, page): fade out, then the main loop swaps the level */
static int g_next_level = -1; static float g_fade_len = 0.5f, g_switch_fade = 1.0f;
static void request_level(int index, float fade_s) { if (g_next_level < 0 && index >= 0 && index < 29) { g_next_level = index; g_fade_len = fade_s > 0.01f ? fade_s : 0.01f; audio_music_stop(g_fade_len * 0.9f); audio_rtc(-1); } }   /* 0x404b95 */
static int level_index(const char *name) { for (int i = 0; i < 29; i++) if (!_stricmp(k_levels[i], name)) return i; return -1; }

/* real time cinematic state (docs/CINEMATIC.md, object game+0x64); the update is further down */
static struct { int state, anim, nactors; Instance *main_inst, *vec; struct { Instance *inst; int anim; } actor[32]; uint32_t var; float remain, timer; int rtc; } g_cin;
static int cin_running(void) { return g_cin.state == 2 || g_cin.state == 3; }               /* 0x44f2e0: Perso update skipped */

/* ---- camera manager: follow camera (mode 1, player.c) + the fixed script cameras (docs/CAMERA_SCRIPT.md, CAMERA.md 4 and 6.1)
 * mode 2 (message 510) / mode 4 (520, letterbox, player frozen): camera at the .ins camera position looking at
 * target origin + (0, f, 0). A mode change blends linearly from the frozen old camera unless the script asked for a cut. */
static struct {
    int mode; Vec3 fix_pos; Instance *fix_target; float fix_f;
    float dur, speed; int dur_from_speed, cut;                  /* 570 / 560 / 580 */
    int active; Vec3 from_pos, look_from, look_cur, look_off; float elapsed, t;
    Vec3 pos;                                                    /* camera position of the last frame */
    const Trajectory *rail; float rail_d, rail_y; Vec3 rail_pt; int rail_first;   /* mode 8 (540): camera on a TRAJ at distance d from the player */
    /* mode 0x20 (game messages 1088 + 1110, CAMERA_SCRIPT.md 4.2): side view of a section where the player is kept on a vertical plane */
    int plane_on, side; Vec3 plane_a, plane_d; float sv_par[8];   /* sv_par: lat 1000, ahead 300, h 340, h_up 500, h_down 0, rate_h 400, rate_a 700, rate_lat 200 */
    float sv_a, sv_h, sv_lat, sv_s;
    int death_cam;                                               /* engine use of mode 2 (0x41fb50 from 0x459030): the camera stops and watches the player fall */
} g_cam = { 1 };
static const float k_sv_defaults[8] = { 1000, 300, 340, 500, 0, 400, 700, 200 };
static Camera *slot_camera(uint32_t ref) { uint32_t i = ref & 0xffffff; return i < g_ins.nslots + 16 ? g_ins.cam_slots[i] : NULL; }
static void cam_set_mode(int mode)                               /* SetMode 0x41f410 + 0x41eaa0 */
{
    if (!g_cam.cut) {
        g_cam.look_from = g_cam.active ? g_cam.look_cur : g_cam.look_off; g_cam.from_pos = g_cam.pos;
        g_cam.active = 1; if (g_cam.dur <= 0) g_cam.dur = 2.0f; g_cam.elapsed = 0; g_cam.t = 0;
    } else g_cam.active = 0;
    g_cam.mode = mode; if (mode == 1 && g_player) g_player->cam_init = 0;
}
static float ramp_to(float v, float target, float step) { return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step); }
static void cam_side_start(Instance *in, int v)                   /* Perso::0x459960 */
{
    if (g_cam.plane_on) { if (g_cam.mode != 0x20) { g_cam.cut = 1; cam_set_mode(0x20); } return; }
    const Model *mo = in->model; int node = -1;                    /* marker node (kind 0x20) with typecode 0: two points A, B (0x42f6b0) */
    for (uint32_t i = 0; i < mo->nnodes && node < 0; i++) if (mo->nodes[i].kind == 0x20 && mo->nodes[i].type_code == 0 && mo->nodes[i].npoints >= 2) node = (int)i;
    if (node < 0) { printf("1088: instance %u has no marker\n", in->index); return; }
    ins_pose(in, in->anim, in->anim_time);
    Vec3 A = ins_point_world(in, mo->nodes[node].point_base), B = ins_point_world(in, mo->nodes[node].point_base + 1), d = { B.x - A.x, 0, B.z - A.z };
    float l = sqrtf(d.x * d.x + d.z * d.z); if (l < 0.01f) d = (Vec3){ 1, 0, 0 }; else { d.x /= l; d.z /= l; }
    g_cam.plane_on = 1; g_cam.plane_a = A; g_cam.plane_d = d; g_cam.side = v == 1 ? 0 : 1;
    memcpy(g_cam.sv_par, k_sv_defaults, sizeof k_sv_defaults);
    g_cam.sv_a = 0; g_cam.sv_h = g_cam.sv_par[2]; g_cam.sv_lat = g_cam.sv_par[0]; g_cam.sv_s = 1;
    g_cam.cut = 1; cam_set_mode(0x20);
}
static void cam_msg(const EkoMsg *m, const Camera *c)
{
    int a1 = m->nargs > 1 ? (int)m->args[1] : 0;
    switch (m->id) {
    case 500: case 501: cam_set_mode(1); g_cam.plane_on = 0; break;   /* the plane lock ends with a Perso state change (0x44de44); here: with the camera */                  /* 501 (camera in front of the player) starts behind as well */
    case 510: case 520: {
        Instance *t = m->nargs > 2 ? slot_instance(0x1000000 | (m->args[2] & 0xffffff)) : NULL; if (!t) break;
        g_cam.fix_pos = c->position; g_cam.fix_f = (float)a1; g_cam.fix_target = t; cam_set_mode(m->id == 510 ? 2 : 4); break; }
    case 540: if (c->traj.npoints >= 2) { g_cam.rail = &c->traj; g_cam.rail_d = (float)a1; g_cam.rail_first = 1; cam_set_mode(8); } break;
    case 560: g_cam.speed = (float)a1; g_cam.dur_from_speed = 1; break;
    case 570: g_cam.dur = a1 * 0.01f; g_cam.dur_from_speed = 0; break;
    case 580: g_cam.cut = a1 == 2; break;
    default: break;
    }
}
/* rail camera 0x421570 (CAMERA.md 6.2): the point of the polyline at distance d from the player that is nearest to the
 * previous camera; without one, the point of the polyline nearest to the player. The rail point moves at most 1000 u/s. */
static Vec3 rail_target(const Trajectory *tr, Vec3 c, float d, Vec3 prev)
{
    Vec3 best = tr->points[0], nearest = tr->points[0]; float best_d = 1e30f, near_d = 1e30f;
    uint32_t nseg = tr->closed ? tr->npoints : tr->npoints - 1;
    for (uint32_t i = 0; i < nseg; i++) {
        Vec3 a = tr->points[i], b = tr->points[(i + 1) % tr->npoints], ab = { b.x - a.x, b.y - a.y, b.z - a.z }, ca = { a.x - c.x, a.y - c.y, a.z - c.z };
        float A = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z; if (A < 1e-6f) continue;
        float B = 2 * (ab.x * ca.x + ab.y * ca.y + ab.z * ca.z), C = ca.x * ca.x + ca.y * ca.y + ca.z * ca.z - d * d, disc = B * B - 4 * A * C;
        for (int k = 0; k < 2 && disc >= 0; k++) {
            float s = (-B + (k ? 1.0f : -1.0f) * sqrtf(disc)) / (2 * A); if (s < 0 || s > 1) continue;
            Vec3 q = { a.x + ab.x * s, a.y + ab.y * s, a.z + ab.z * s }; float e = (q.x - prev.x) * (q.x - prev.x) + (q.y - prev.y) * (q.y - prev.y) + (q.z - prev.z) * (q.z - prev.z);
            if (e < best_d) { best_d = e; best = q; }
        }
        float s = -(ab.x * ca.x + ab.y * ca.y + ab.z * ca.z) / A; if (s < 0) s = 0; if (s > 1) s = 1;
        Vec3 q = { a.x + ab.x * s, a.y + ab.y * s, a.z + ab.z * s }; float e = (q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y) + (q.z - c.z) * (q.z - c.z);
        if (e < near_d) { near_d = e; nearest = q; }
    }
    return best_d < 1e30f ? best : nearest;
}
static void cam_update(Player *p, FreeCamera *cam, float dt, int behind_key)
{
    Vec3 P, T;
    if (g_cam.mode == 0x80 && g_cin.main_inst) {                 /* camera from the animation of the cinematic's main instance (0x42fa80): cut, no smoothing */
        Instance *I = g_cin.main_inst; const Model *mo = I->model; Vec3 eye, tgt;
        float L = (uint32_t)I->slot[0] < mo->nanims && mo->anims[I->slot[0]].duration_s > 0 ? mo->anims[I->slot[0]].duration_s : 1.0f;
        if (ins_camera_eval(I, I->slot[0], I->a_pos / L, &eye, &tgt)) {
            Vec3 to = { tgt.x - eye.x, tgt.y - eye.y, tgt.z - eye.z };
            cam->pos = eye; cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z)); cam->letterbox = 1; cam->fov_deg = 68.04f;
            g_cam.pos = eye; g_cam.active = 0; return;
        }
    }
    if (g_cam.death_cam && !p->dead_kind) { g_cam.death_cam = 0; g_cam.cut = 1; cam_set_mode(1); }   /* respawn: hard cut back to the follow camera (0x41f9f0(2), SetMode(0,0)) */
    if (g_cam.mode == 0x20 && g_cam.plane_on) {                  /* 0x424bf0 */
        const float *q = g_cam.sv_par; Vec3 d = g_cam.plane_d, sidev = { -d.z, 0, d.x };   /* (0,-1,0) x dir */
        float htarget = behind_key == 2 ? q[3] : behind_key == 3 ? q[4] : q[2];
        g_cam.sv_a = ramp_to(g_cam.sv_a, q[1], q[6] * dt); g_cam.sv_h = ramp_to(g_cam.sv_h, htarget, q[5] * dt); g_cam.sv_lat = ramp_to(g_cam.sv_lat, q[0], q[7] * dt);
        float face = sinf(p->yaw) * d.x + cosf(p->yaw) * d.z;    /* the look-ahead follows the walking direction; the reversal blends at the look-ahead rate */
        g_cam.sv_s = ramp_to(g_cam.sv_s, face >= 0 ? 1.0f : -1.0f, (g_cam.sv_a > 1 ? q[6] / g_cam.sv_a : 10.0f) * dt);
        Vec3 C = { p->pos.x + d.x * g_cam.sv_a * g_cam.sv_s, p->pos.y + g_cam.sv_h, p->pos.z + d.z * g_cam.sv_a * g_cam.sv_s };
        float lat = g_cam.side == 0 ? -g_cam.sv_lat : g_cam.sv_lat;
        P = (Vec3){ C.x + sidev.x * lat, C.y, C.z + sidev.z * lat }; T = p->pos; g_cam.look_off = (Vec3){ C.x - T.x, C.y - T.y, C.z - T.z };
    } else if (g_cam.mode == 8 && g_cam.rail) {
        int js = p->jumper.state, air = js == 0 || js == 1 || js == 7 || js == 3 || js == 4;
        if (g_cam.rail_first) g_cam.rail_y = p->pos.y; else if (!air) { float k = powf(0.95f, 30.0f * dt); g_cam.rail_y = (1 - k) * p->pos.y + k * g_cam.rail_y; }
        Vec3 c = { p->pos.x, g_cam.rail_y, p->pos.z }, want = rail_target(g_cam.rail, c, g_cam.rail_d, g_cam.rail_first ? g_cam.pos : g_cam.rail_pt);
        Vec3 d = { want.x - g_cam.rail_pt.x, want.y - g_cam.rail_pt.y, want.z - g_cam.rail_pt.z }; float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), step = 1000.0f * dt;
        if (g_cam.rail_first || len <= step) g_cam.rail_pt = want; else { g_cam.rail_pt.x += d.x / len * step; g_cam.rail_pt.y += d.y / len * step; g_cam.rail_pt.z += d.z / len * step; }
        g_cam.rail_first = 0;
        P = g_cam.rail_pt; T = c; g_cam.look_off = (Vec3){ 0, 140.0f, 0 };   /* look height: params+0x1c, writer not found; the follow camera's value */
    } else
    if (g_cam.mode == 1 || !g_cam.fix_target) {
        player_camera(p, cam, dt, behind_key); P = cam->pos; T = p->pos; g_cam.look_off = (Vec3){ 0, 140.0f - p->cam_drop, 0 };
    } else {
        P = g_cam.fix_pos; T = g_cam.fix_target == p->inst ? p->pos : g_cam.fix_target->position; g_cam.look_off = (Vec3){ 0, g_cam.fix_f, 0 };
    }
    Vec3 look = { T.x + g_cam.look_off.x, T.y + g_cam.look_off.y, T.z + g_cam.look_off.z };
    if (g_cam.active) {                                          /* Transition_Travelling 0x41eb90 */
        Vec3 d = { P.x - g_cam.from_pos.x, P.y - g_cam.from_pos.y, P.z - g_cam.from_pos.z };
        if (g_cam.dur_from_speed) { g_cam.dur = g_cam.speed > 0 ? sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) / g_cam.speed : 2.0f; g_cam.dur_from_speed = 0; }
        float t = g_cam.t;
        P = (Vec3){ g_cam.from_pos.x + d.x * t, g_cam.from_pos.y + d.y * t, g_cam.from_pos.z + d.z * t };
        g_cam.look_cur = (Vec3){ g_cam.look_from.x + (g_cam.look_off.x - g_cam.look_from.x) * t, g_cam.look_from.y + (g_cam.look_off.y - g_cam.look_from.y) * t, g_cam.look_from.z + (g_cam.look_off.z - g_cam.look_from.z) * t };
        look = (Vec3){ T.x + g_cam.look_cur.x, T.y + g_cam.look_cur.y, T.z + g_cam.look_cur.z };
        g_cam.t = g_cam.dur > 0 ? g_cam.elapsed / g_cam.dur : 1; if (g_cam.t > 1) g_cam.t = 1;
        g_cam.elapsed += dt; if (g_cam.elapsed > g_cam.dur) g_cam.active = 0;
    }
    Vec3 to = { look.x - P.x, look.y - P.y, look.z - P.z };
    cam->pos = P; cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z));
    cam->letterbox = g_cam.mode == 4; cam->fov_deg = g_cam.mode == 4 ? 68.04f : 83.97f;   /* tan(vfov/2) = 1.2 * 0.5625 resp. 1.2 * 0.75 */
    g_cam.pos = P; p->cam_yaw = cam->yaw;                         /* movement stays relative to the camera on screen */
}

/* script screen faders (app+0x9c, 0x401440 / 0x401480 / 0x4014c0): 1150 fades in from black over f s, 1151 fades out,
 * 1152 blacks out the current frame (scripts repeat it with DURING) */
static struct { float rest, total; int out, hold, script; } g_sfade; static int g_black_frame;
static void fade_start(float t, int out) { g_sfade.total = g_sfade.rest = t; g_sfade.out = out; g_sfade.hold = 0; g_sfade.script = 0; }

/* ---- real time cinematics (docs/CINEMATIC.md): object game+0x64, update 0x44f0a0 */
static void cin_update(EkoVM *vm, float dt, float now)
{
    Instance *m = g_cin.main_inst;
    switch (g_cin.state) {
    case 1:
        if ((g_cin.timer -= dt) > 0) break;
        fade_start(0.5f, 0); g_black_frame = 1; g_cin.state = 2; eko_set_var(vm, g_cin.var, 0); audio_rtc(g_cin.rtc);   /* vt[0x98]: the /Rtc/ stream of this scene */
        {   /* 0x44eab0: the main instance goes to the vector P0, facing P0 -> P1; everything plays once at speed 3 */
            const Model *mo = g_cin.vec->model; int node = -1;
            for (uint32_t i = 0; i < mo->nnodes && node < 0; i++) if (mo->nodes[i].type_code == 5 && mo->nodes[i].npoints >= 2) node = (int)i;
            if (node < 0) { printf("cinematic: no vector on instance %u\n", g_cin.vec->index); g_cin.remain = 0; break; }
            Vec3 P0 = ins_point_world(g_cin.vec, mo->nodes[node].point_base), P1 = ins_point_world(g_cin.vec, mo->nodes[node].point_base + 1);
            if (g_player && g_player->inst == m) player_place(g_player, P0, atan2f(P1.x - P0.x, P1.z - P0.z));
            m->scripted = 1; m->visible = 1; inst_play_once(m, g_cin.anim, 3.0f, now);
            for (int i = 0; i < g_cin.nactors; i++) inst_play_once(g_cin.actor[i].inst, g_cin.actor[i].anim, 3.0f, now);
            g_cam.cut = 1; cam_set_mode(0x80);
        }
        break;
    case 2:
        g_cin.remain -= dt; if (g_cin.remain > 0.5f) break;
        g_cin.timer = g_cin.remain; fade_start(g_cin.remain > 0.01f ? g_cin.remain : 0.01f, 1); g_cin.state = 3; break;
    case 3:
        if ((g_cin.timer -= dt) > 0) break;
        fade_start(0.5f, 0); g_cin.timer = 0.5f; g_cin.state = 4; audio_rtc(-1); audio_music_pause(0, 0.45f);
        {   /* 0x44edb0 + 0x445af9: the player continues where the animation left the root, follow camera behind him */
            Vec3 pos, fwd;
            if (g_player && g_player->inst == m) {
                m->scripted = 0;
                if (ins_root_end(m, g_cin.anim, &pos, &fwd)) player_place(g_player, pos, atan2f(fwd.x, fwd.z));
            }
            g_cam.cut = 1; cam_set_mode(1);
        }
        break;
    case 4: if ((g_cin.timer -= dt) <= 0) memset(&g_cin, 0, sizeof g_cin); break;
    default: break;
    }
}

/* ---- progress (docs/GAMEFLOW.md 6): the active save struct is the only player state that survives a level change.
 * Kept in our own file (woodyre.sav), not in the original's Woody.sav. */
typedef struct { int32_t lives, unique, charges; float health; uint8_t done[29]; } SaveChar;
static struct { uint32_t magic; SaveChar chr[3]; } g_save;
static int g_char, g_unlock_all;                                 /* cfg+0x380: 0 Woody, 1 Knothead, 2 Splinter */
static void save_reset(void) { memset(&g_save, 0, sizeof g_save); g_save.magic = 0x31565357; for (int c = 0; c < 3; c++) { g_save.chr[c].lives = 9; g_save.chr[c].health = 3.0f; } }   /* 0x44ffa0 */
static void save_write(void) { FILE *f = fopen("woodyre.sav", "wb"); if (f) { fwrite(&g_save, sizeof g_save, 1, f); fclose(f); } }
static void save_read(void) { FILE *f = fopen("woodyre.sav", "rb"); save_reset(); if (f) { if (fread(&g_save, sizeof g_save, 1, f) != 1 || g_save.magic != 0x31565357) save_reset(); fclose(f); } }
static int char_of_level(int i) { return i <= 10 ? 0 : i <= 17 ? 1 : i <= 24 ? 2 : i == 25 ? 0 : -1; }   /* byte table 0x404830; -1 = unchanged */
/* LevelIsEnable 0x450470 (table 0x450694): the done flag of the predecessor. The original reads it in the block of the
 * current character; here in the block of the predecessor's own character (otherwise K1A could never open from KWS). */
static int level_is_enable(int level)
{
    if (g_unlock_all || level <= 2 || level > 25) return 1;
    int pred = (level == 11 || level == 12) ? 6 : (level == 18 || level == 19) ? 10 : level - 1;
    return g_save.chr[char_of_level(pred)].done[pred];
}
static Instance *g_prop;                                         /* message 1142: the instance 1140 moves along with the player */
static int g_act_now[3], g_act_prev[3];                          /* input actions 0 (left), 1 (right), 6 (attack) for 1048 / 1049 / 1050 */
static Instance *g_pose;                                         /* message 1141: House menu pose (0x44e640); held every tick while level 0 runs (0x44e690) */
static uint32_t g_intro_var; static int g_have_intro;            /* message 1160: House intro state variable (0 rest, 1 start, 2/3 running, 4 done) */

/* script -> engine messages. Only the subset needed to see something happen is implemented;
 * everything else is logged. See docs/MESSAGES.md. */

/* ---- sound (docs/SOUND.md): script messages 1600..1657 (0x467fa0) -> the mixer in audio.c.
 * vol 0..100; pitch args are x0.01 (f > 0 = frequency factor), "dur" args are x-0.01 (wanted duration); dmin args are x0.01 m. */
static uint32_t g_text_var; static int g_hud_ext;                 /* 1080: close flag variable; 1172: extended HUD this frame (app+0x70) */
static void snd_msg(const EkoMsg *m, Instance *in)
{
#define AI(i) ((i) < (int)m->nargs ? (float)(int32_t)m->args[i] : 0.0f)
    uint32_t s2 = m->nargs ? m->args[0] : 0, s3 = m->nargs > 1 ? m->args[1] : 0;   /* sample ref of the 2D / 3D forms */
    const float *pos = in ? &in->position.x : NULL;
    switch (m->id) {
    case 1600: case 1603: audio_play(s2, NULL, 0, AI(1), 1.0f, NULL, 0, 0); break;
    case 1601: case 1604: audio_play(s2, NULL, 0, AI(1), AI(2) * 0.01f, NULL, 0, 0); break;
    case 1602: case 1605: audio_play(s2, NULL, 0, AI(1), AI(2) * -0.01f, NULL, 0, 0); break;
    case 1606: case 1611: audio_play(s2, NULL, 1, AI(1), 1.0f, NULL, 0, 0); break;
    case 1607: case 1612: audio_play(s2, NULL, 1, AI(1), AI(2) * 0.01f, NULL, 0, 0); break;
    case 1609: case 1614: audio_play(s2, NULL, 1, AI(1), AI(2) * -0.01f, NULL, 0, 0); break;
    case 1608: case 1610: case 1613: case 1615: audio_play(s2, NULL, 1, AI(1), AI(2) * 0.01f, NULL, 0, AI(3) * 0.01f); break;
    case 1616: case 1617: audio_play(s3, in, 0, AI(2), 1.0f, NULL, 0, 0); break;
    case 1652: audio_stop2d(s2, AI(1) * 0.01f, (int)AI(2)); break;
    case 1655: audio_music((int)AI(0)); break;
    case 1646: case 1656: audio_music_stop(AI(0) * 0.01f); break;
    case 1657: audio_next_fade_in(AI(0) * 0.01f); break;
    default: break;
    }
    if (!in) return;
    switch (m->id) {                                                                /* 3D: key (instance, sample); default dmin 2 m */
    case 1620: audio_play(s3, in, 1, AI(2), 1.0f, pos, 2.0f, 0); break;
    case 1621: audio_play(s3, in, 1, AI(2), AI(3) * 0.01f, pos, 2.0f, 0); break;
    case 1622: case 1624: audio_play(s3, in, 0, AI(2), 1.0f, pos, 2.0f, 0); break;
    case 1623: case 1625: audio_play(s3, in, 0, AI(2), AI(3) * 0.01f, pos, 2.0f, 0); break;
    case 1626: case 1627: audio_play(s3, in, 0, AI(2), AI(3) * -0.01f, pos, 2.0f, 0); break;
    case 1628: audio_stop3d(s3, in, AI(2) * 0.01f); break;
    case 1629: audio_play(s3, in, 1, AI(2), AI(3) * -0.01f, pos, 2.0f, AI(4) * 0.01f); break;
    case 1630: audio_play(s3, in, 1, AI(2), 1.0f, pos, AI(3) * 0.01f, 0); break;
    case 1631: audio_play(s3, in, 1, AI(2), AI(3) * 0.01f, pos, AI(4) * 0.01f, 0); break;
    case 1632: audio_play(s3, in, 1, AI(2), AI(3) * -0.01f, pos, AI(5) * 0.01f, AI(4) * 0.01f); break;
    case 1633: case 1636: audio_play(s3, in, 0, AI(2), 1.0f, pos, AI(3) * 0.01f, 0); break;
    case 1634: case 1637: audio_play(s3, in, 0, AI(2), AI(3) * 0.01f, pos, AI(4) * 0.01f, 0); break;
    case 1635: case 1638: audio_play(s3, in, 0, AI(2), AI(3) * -0.01f, pos, AI(4) * 0.01f, 0); break;
    default: break;
    }
#undef AI
}

/* animation events of type 4 on the root node = sounds (0x42f5e0 -> 0x43a8f0 -> 0x4695f0, docs/SOUND.md 3):
 * {4, t, ref, probLo, probHi, vol, pitch%, dmin cm, 0}; one random draw per call picks among the variants; the Perso plays 2D */
static uint32_t msvc_rand(void *user);
static void anim_sounds(Instance *ii)
{
    const Model *mo = ii->model;
    if (!ii->visible || !mo->nnodes || ii->anim < 0 || (uint32_t)ii->anim >= mo->nanims) return;
    const InsNode *n = &mo->nodes[0]; if (!n->event_refs || !n->pool || !n->event_refs[ii->anim].cnt) { ii->snd_anim = ii->anim; return; }
    const InsAnim *a = &mo->anims[ii->anim]; float dur = a->duration_s > 0 ? a->duration_s : 1.0f;
    float ph = fmodf(ii->anim_time / dur, 1.0f); if (ph < 0) ph += 1.0f; float tf = ph * (float)a->nframes;
    float t0 = ii->snd_anim == ii->anim ? ii->snd_tf : (tf > 2.0f ? tf - 2.0f : 0.0f);
    ii->snd_anim = ii->anim; ii->snd_tf = tf;
    if (tf == t0) return;
    float r = -1;
    const uint32_t *e = (const uint32_t *)(n->pool + ((size_t)n->a + n->b + n->event_refs[ii->anim].off) * 4);
    for (uint32_t i = 0; i < n->event_refs[ii->anim].cnt; i++) {
        uint32_t type = e[0], size = type == 3 ? 15 : type == 4 ? 9 : type == 5 ? 6 : 0; if (!size) return;
        if (type == 4) {
            float t, lo, hi, vol, dmin; int32_t pitch;
            memcpy(&t, &e[1], 4); memcpy(&lo, &e[3], 4); memcpy(&hi, &e[4], 4); memcpy(&vol, &e[5], 4); memcpy(&pitch, &e[6], 4); memcpy(&dmin, &e[7], 4);
            int hit = tf > t0 ? (t >= t0 && t < tf) : (t >= t0 || t < tf);
            if (hit) {
                if (r < 0) { r = (float)msvc_rand(NULL) / 32767.0f * 100.0f - 1.0f; if (r < 0) r = 0; }
                if (r >= lo && r < hi) {
                    int perso = g_player && g_player->inst == ii;
                    audio_play(e[2], ii, 0, vol, (float)pitch * 0.01f, perso ? NULL : &ii->position.x, dmin * 0.01f, 0);
                }
            }
        }
        e += size;
    }
}
static void on_msg(EkoVM *vm, const EkoMsg *m, void *user)
{
    (void)user;
    Instance *in = m->nargs ? slot_instance(m->args[0]) : NULL;
    if (m->id >= 500 && m->id <= 800 && m->nargs && slot_camera(m->args[0])) cam_msg(m, slot_camera(m->args[0]));
    switch (m->id) {
    case 1200: if (in && m->nargs > 1) { in->type = (int)m->args[1]; if (g_player && (in->type == 1 || in->type == 2 || in->type == 3 || in->type == 18 || in->type == 19) && g_player->inst != in) { g_player->inst->scripted = 1; player_bind(g_player, in); in->scripted = 0; printf("player: instance %u (type %d) at %.0f %.0f %.0f\n", in->index, in->type, in->position.x, in->position.y, in->position.z); } if (in->type >= 4 && in->type <= 6) enemies_add(&g_enemies, in, in->type); if (in->type == 34 && g_player) { g_player->bonus_total++; } } break;   /* SetTypeInstance; [0x5e54e4] = Woody bonus total */
    case 1: case 2: case 3: case 4: case 5: case 6: case 12: case 13:               /* base class: animation, show/hide, path, fade (instance.c) */
    case 42: case 43: case 44: case 45: case 56: case 57:
        if (in && in->scripted && inst_msg(in, m->id, m->args, m->nargs, g_now) && g_nretry < 32) g_retry[g_nretry++] = *m;
        break;
    case 7: if (in) { int k = 0; for (int i = 0; i < g_nretry; i++) if (slot_instance(g_retry[i].args[0]) != in) g_retry[k++] = g_retry[i]; g_nretry = k; } break;
    case 10:                                                                        /* Collect (docs/BONUS.md): the level script saw the player enter the bonus volume */
        if (in && g_player && in->visible && player_collect(g_player, in->type, m->nargs > 1 ? (int)m->args[1] : 0)) in->visible = 0;   /* 0x407850: cell = -1 */
        break;
    case 1020:                                                                      /* 0x44516a: Perso->vt[38](1), sent by the pit / water volumes; + 0x459030 unless in the side view */
        if (g_player) {
            player_kill(g_player, 1);
            if (g_cam.mode != 0x20 && g_player->dead_kind) { g_cam.fix_pos = g_cam.pos; g_cam.fix_target = g_player->inst; g_cam.fix_f = g_cam.look_off.y; g_cam.cut = 1; cam_set_mode(2); g_cam.death_cam = 1; }
        }
        break;
    /* game flow (docs/GAMEFLOW.md) */
    case 1081: if (m->nargs) request_level((int)m->args[0], 1.5f); break;                                   /* GotoLevel: 0x404b60(1.5, level, 1, 0) */
    case 1083:                                                                                              /* EndLevel 0x404be0 */
        if (g_level == 1 || g_level == 11 || g_level == 18 || g_level == 25) { request_level(0, 0.5f); break; }    /* from a hub: to the title */
        if (g_level >= 0 && g_level < 29) g_save.chr[g_char].done[g_level] = 1;
        save_write(); request_level(g_char == 0 ? 1 : g_char == 1 ? 11 : 18, 0.5f); break;
    case 1082: if (m->nargs > 1) eko_set_var(vm, m->args[1], level_is_enable((int)m->args[0])); break;
    case 1085: if (m->nargs > 1) eko_set_var(vm, m->args[1], m->args[0] < 29 ? g_save.chr[g_char].done[m->args[0]] : 0); break;   /* LevelIsDone 0x4509e0 */
    case 1030: if (in && g_player) { g_player->spawn_pos = in->position; g_player->spawn_yaw = g_player->yaw; }   /* direction: the instance's vector node when it has one (not parsed), else the current facing */ break;   /* SaveAuto: checkpoint */
    case 1142: g_prop = in; break;
    case 1040: case 1043: if (g_player) player_script_hold(g_player, 2.0f); break;                         /* scripted Perso action (17 = into the door); the animation itself is not ported */
    case 1140:                                                                                              /* hub: player at the door he came out of (0x453d90); the results screen is skipped */
        if (in && g_player && m->nargs > 1) {
            g_player->pos = in->position; g_player->yaw = inst_yaw(in) + 3.14159265f;   /* out of the door */ g_player->floor_y = g_player->pos.y; g_player->vel = (Vec3){ 0, 0, 0 }; g_player->cam_init = 0;
            if (g_prop) g_prop->position = in->position;
            save_write(); eko_set_var(vm, m->args[1], 1);                                                   /* 0x45422c: what the script gets when the results screen closes */
        }
        break;
    case 1042:                                                                                              /* near `inst` (xz) and facing it within `angle` degrees */
        if (m->nargs > 3) {
            int ok = 0;
            if (in && g_player && g_player->dead_kind == 0) {
                float dx = in->position.x - g_player->pos.x, dz = in->position.z - g_player->pos.z, d = sqrtf(dx * dx + dz * dz);
                if (d < (float)(int)m->args[1]) ok = d < 1e-3f || (sinf(g_player->yaw) * dx + cosf(g_player->yaw) * dz) / d >= cosf((float)(int)m->args[2] * 3.14159265f / 180.0f);
            }
            eko_set_var(vm, m->args[3], ok);
        }
        break;
    case 1048: case 1049: case 1050:                                                                        /* key tests: mode 0 held, 1 released, 2 just pressed (0x467400 / 0x467420 / 0x467440) */
        if (m->nargs > 1) { int k = m->id - 1048, mode = (int)m->args[1], now = g_act_now[k], prev = g_act_prev[k];
                            eko_set_var(vm, m->args[0], mode == 0 ? now : mode == 1 ? (!now && prev) : (now && !prev)); }
        break;
    case 1141: g_pose = in; break;
    case 1160: if (m->nargs) { g_intro_var = m->args[0]; g_have_intro = 1; } break;
    case 1084: if (m->nargs) eko_set_var(vm, m->args[0], g_prev_level); break;                              /* GetPrevLevel: the hub script picks the spawn point with it */
    case 1180: request_level(26, 0.5f); break;
    case 1150: case 1151: if (m->nargs) { fade_start((int)m->args[0] * 0.01f, m->id == 1151); g_sfade.script = 1; } break;   /* a script fade-out does not stay black when it ends: the House intro cuts to its second scene behind 1152 */
    case 1131: if (in && m->nargs > 1) { g_cin.main_inst = in; g_cin.anim = (int)m->args[1]; } break;
    case 1132: if (in && m->nargs > 1 && g_cin.nactors < 32) { g_cin.actor[g_cin.nactors].inst = in; g_cin.actor[g_cin.nactors++].anim = (int)m->args[1]; } break;
    case 1130:                                                                     /* (vector instance, rtc sound track, var) */
        if (in && g_cin.main_inst && g_cin.state == 0 && m->nargs > 2) {
            const Model *mo = g_cin.main_inst->model; g_cin.vec = in; g_cin.var = m->args[2];
            g_cin.remain = ((uint32_t)g_cin.anim < mo->nanims ? mo->anims[g_cin.anim].duration_s : 0) / 3.0f;   /* duration / 12288 */
            g_cin.state = 1; g_cin.timer = 0.5f; fade_start(0.4f, 1); g_cin.rtc = (int)m->args[1]; audio_music_pause(1, 0.45f);
        }
        break;
    case 1152: g_black_frame = 1; break;
    case 1080: if (m->nargs > 3) { hud_text_open((int)m->args[0], (int)m->args[1], &m->args[3], (int)m->nargs - 3); g_text_var = m->args[2]; printf("  TEXT box at vm t=%d: strings %u %u %u\n", vm->time, m->args[3] & 0xffff, m->nargs > 4 ? m->args[4] & 0xffff : 0, m->nargs > 5 ? m->args[5] & 0xffff : 0); } break;   /* text box 0x456ed0: stays until the script sets var != 0 */
    case 1172: g_hud_ext = 1; break;
    case 1088: if (in && g_player) cam_side_start(in, m->nargs > 1 ? (int)m->args[1] : 0); break;
    case 1110: if (m->nargs > 1) { static const int fld[9] = { -1, 3, 2, 4, 1, 0, 6, 5, 7 }; int n = (int)m->args[0];   /* n -> sv_par index */
                   if (n == 9) memcpy(g_cam.sv_par, k_sv_defaults, sizeof k_sv_defaults); else if (n >= 1 && n <= 8) g_cam.sv_par[fld[n]] = (float)(int)m->args[1]; } break;                                                              /* credits */                       /* 0x44516a: Perso->vt[38](1), sent by the pit / water volumes */
    default: if (m->id >= 1600 && m->id <= 1657) snd_msg(m, in); break;
    }
    if (g_log_msgs) {
        printf("  SEND %u [", m->id);
        for (uint32_t i = 0; i < m->nargs; i++) printf("%s%s%x", i ? ", " : "", m->args[i] >= 0x1000000 ? "0x" : "", m->args[i] >= 0x1000000 ? m->args[i] : m->args[i]);
        printf("]\n");
    }
}
static void on_warn(EkoVM *vm, const char *s, void *user) { (void)vm; (void)user; printf("VM warning: %s\n", s); }
static uint32_t g_seed = 1;
static uint32_t msvc_rand(void *user) { (void)user; g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }

/* one loaded level: everything that is torn down and rebuilt on a level change (the window and GL context stay) */
typedef struct {
    char name[32]; TexFile tex; GelFile gel; LitFile lit; int have_lit; void *code; EkoVM vm; Renderer rnd;
    Player player; int have_player; double t0;
} Level;
static void *read_all(const char *path, size_t *sz);
static void level_free(Level *L)
{
    hud_text_reset(); audio_stop_all(); audio_bank_free(1); audio_rtc(-1);                            /* vt[0x8c] StopAll on leaving a level (0x4049e0); the voices read instance memory */
    if (L->have_player) player_free(&L->player);
    g_player = NULL; g_prop = NULL; g_pose = NULL; g_have_intro = 0; g_enemies.n = 0; g_nretry = 0; memset(&g_cam, 0, sizeof g_cam); g_cam.mode = 1; memset(&g_sfade, 0, sizeof g_sfade); g_black_frame = 0; memset(&g_cin, 0, sizeof g_cin);
    rnd_free(&L->rnd); eko_free(&L->vm); free(L->code); ins_free(&g_ins); if (L->have_lit) lit_free(&L->lit); gel_free(&L->gel); tex_free(&L->tex);
    memset(L, 0, sizeof *L);
}
static int level_load(Level *L, const char *dir, const char *lvl)
{
    char path[512]; memset(L, 0, sizeof *L); snprintf(L->name, sizeof L->name, "%s", lvl);
    if (char_of_level(g_level) >= 0) g_char = char_of_level(g_level);
    snprintf(path, sizeof path, "%s/%s/%s.tex", dir, lvl, lvl); if (tex_load(&L->tex, path)) return -1;
    snprintf(path, sizeof path, "%s/%s/%s.gel", dir, lvl, lvl); if (gel_load(&L->gel, path)) { tex_free(&L->tex); return -1; }
    snprintf(path, sizeof path, "%s/%s/%s.ins", dir, lvl, lvl); if (ins_load(&g_ins, path)) { gel_free(&L->gel); tex_free(&L->tex); return -1; }
    snprintf(path, sizeof path, "%s/%s/code", dir, lvl);
    size_t codesz; L->code = read_all(path, &codesz);
    if (!L->code || eko_load(&L->vm, L->code, codesz)) { fprintf(stderr, "cannot load %s\n", path); free(L->code); ins_free(&g_ins); gel_free(&L->gel); tex_free(&L->tex); return -1; }
    L->vm.on_msg = on_msg; L->vm.on_warn = on_warn; L->vm.rand_fn = msvc_rand;
    printf("%s: %u polys, %u verts, %u textures, %u models, %u slots, %u script objects\n", lvl, L->gel.npolys, L->gel.nverts, L->tex.ngroups, g_ins.nmodels, g_ins.nslots, L->vm.nobj);
    snprintf(path, sizeof path, "%s/%s/%s.lit", dir, lvl, lvl); L->have_lit = lit_load(&L->lit, path) == 0;
    rnd_init(&L->rnd, &L->tex, &L->gel, &g_ins, L->have_lit ? &L->lit : NULL);
    L->have_player = player_init(&L->player, &g_ins, &L->gel) == 0;
    g_player = L->have_player ? &L->player : NULL;
    for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) inst_init(&g_ins.models[mi].instances[k]);
    if (L->have_player) { L->player.inst->scripted = 0; L->player.enemies = &g_enemies; }
{ static const char *chr[3] = { "Woody", "Knothead", "Splinter" }; static int bank0 = -1, title_n;
      if (bank0 != g_char) { snprintf(path, sizeof path, "%s/../Common/%s.rck", dir, chr[g_char]); printf("sound bank 0: %d sounds\n", audio_bank_load(0, path)); bank0 = g_char; }
      snprintf(path, sizeof path, "%s/%s/%s.rck", dir, lvl, lvl); printf("sound bank 1: %d sounds\n", audio_bank_load(1, path));
      { char common[512]; snprintf(common, sizeof common, "%s/../Common/%s.rck", dir, chr[g_char]); if (hud_load(common, path)) printf("hud: no font / images\n"); }
      if (g_level == 0) audio_music((title_n++ & 1) ? 0 : 48); }                   /* 0x404e30: the title alternates Menu02 / Menu; levels send 1655 during init */
    printf("VM init...\n"); eko_init(&L->vm);
    printf("init done: %d messages\n", L->vm.nmsgs);
    if (L->have_player) { SaveChar *sc = &g_save.chr[g_char]; L->player.lives = sc->lives; L->player.health = sc->health > 0 ? sc->health : 1.0f; }   /* 0x44a6a0 / 0x44a759 */
    L->t0 = win_time();
    return 0;
}

static void *read_all(const char *path, size_t *sz) { FILE *f = fopen(path, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); void *b = malloc((size_t)n); if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; } fclose(f); *sz = (size_t)n; return b; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "extract/Data", *lvl = argc > 2 && argv[2][0] != '-' ? argv[2] : "House";   /* no level: boot to the title (House, level 0) */
    const char *shot_path = NULL; double shot_after = 0;                          /* --shot file.ppm seconds: screenshot then quit */
    int have_cam = 0; float cam_args[5] = {0, 0, 0, 0, 0};                          /* --cam x y z yaw pitch (degrees) */
    double jump_at = -1; float max_y = -1e30f, start_y = 0;                       /* --jump T: hold jump from T s for 1 s (testing), reports the apex */
    double jump_len = 1.0, jump2_at = -1, jump2_len = getenv("WOODY_J2LEN") ? atof(getenv("WOODY_J2LEN")) : 0.15;                                         /* --jump2 LEN T2: first press lasts LEN s, second press (0.15 s) at T2 */
    double peck_at = -1, peck_len = 0.1;                                          /* --peck T LEN: hold the attack key from T s for LEN s (testing) */
    int have_pos = 0; float pos_args[3] = {0, 0, 0};                               /* --pos x y z: start the player there (testing) */
    int have_yaw = 0; float yaw_arg = 0;
    double enter_at = -1;                                                         /* --enter T: press Enter on the title after T s (testing) */
    int new_game = 0; const char *next_name = NULL; double next_at = 0;                              /* --next LVL T: change to level LVL after T s (testing) */
    double walk_for = 0; int fly = 0;                                             /* --walk T: hold forward for T s (testing); --fly: start in free camera */
    for (int i = (argc > 2 && argv[2][0] != '-') ? 3 : 2; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_path = argv[i + 1]; shot_after = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--cam") && i + 5 < argc) { for (int k = 0; k < 5; k++) cam_args[k] = (float)atof(argv[i + 1 + k]); have_cam = 1; i += 5; fly = 1; }
        else if (!strcmp(argv[i], "--walk") && i + 1 < argc) { walk_for = atof(argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "--jump") && i + 1 < argc) { jump_at = atof(argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "--jump2") && i + 2 < argc) { jump_len = atof(argv[i + 1]); jump2_at = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--peck") && i + 2 < argc) { peck_at = atof(argv[i + 1]); peck_len = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--pos") && i + 3 < argc) { for (int k = 0; k < 3; k++) pos_args[k] = (float)atof(argv[i + 1 + k]); have_pos = 1; i += 3; }
        else if (!strcmp(argv[i], "--fly")) fly = 1;
        else if (!strcmp(argv[i], "--enter") && i + 1 < argc) { enter_at = atof(argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) { have_yaw = 1; yaw_arg = (float)atof(argv[i + 1]) * 3.14159265f / 180; i += 1; }   /* with --pos: facing in degrees */
        else if (!strcmp(argv[i], "--unlock")) g_unlock_all = 1;                       /* every level door open */
        else if (!strcmp(argv[i], "--newgame")) new_game = 1;                          /* ignore woodyre.sav */
        else if (!strcmp(argv[i], "--prev") && i + 1 < argc) { g_prev_level = level_index(argv[i + 1]); i += 1; }   /* --prev LVL: pretend we came from LVL (hub spawn point) */
        else if (!strcmp(argv[i], "--next") && i + 2 < argc) { next_name = argv[i + 1]; next_at = atof(argv[i + 2]); i += 2; }
    }
    Window win; if (win_open(&win, "WoodyRE", 1280, 800)) return 1;
    if (new_game) save_reset(); else save_read();
    if (!getenv("WOODY_NOSOUND") && !audio_init()) { char bf[512]; snprintf(bf, sizeof bf, "%s/../Music.bf", dir); printf("Music.bf: %d files\n", audio_bf_open(bf)); }
    static Level L; g_level = level_index(lvl); if (level_load(&L, dir, lvl)) return 1;

    /* camera: start behind Woody (model 0, instance 0) if present */
    FreeCamera cam = { {0, 0, 0}, 0, 0, 70 };
    Instance *sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
    if (sel) { cam.pos = sel->position; cam.pos.y += 120; cam.pos.z -= 350; }
    if (have_cam) { cam.pos.x = cam_args[0]; cam.pos.y = cam_args[1]; cam.pos.z = cam_args[2]; cam.yaw = cam_args[3] * 3.14159265f / 180; cam.pitch = cam_args[4] * 3.14159265f / 180; }
    else { cam.pos.x = (L.gel.bbox[0] + L.gel.bbox[1]) / 2; cam.pos.y = L.gel.bbox[3]; cam.pos.z = (L.gel.bbox[4] + L.gel.bbox[5]) / 2; cam.pitch = -1.2f; }

    if (!L.have_player) fly = 1;
    if (L.have_player && have_pos) { L.player.pos.x = pos_args[0]; L.player.pos.y = pos_args[1]; L.player.pos.z = pos_args[2]; L.player.floor_y = L.player.pos.y - 1000.0f; }
    if (L.have_player && have_yaw) L.player.yaw = yaw_arg;
    double t0 = L.t0, last = t0; int pg_prev[2] = {0, 0}, end_prev = 0, enter_prev = 0, l_prev = 0, new_game_pending = 0; int paused = 0, tab_prev = 0, br_prev[2] = {0, 0}, f_prev[3] = {0, 0, 0}, p_prev = 0, f5_prev = 0; uint32_t frames = 0; double fps_t = t0;
    while (!win.quit) {
        win_poll(&win);
        double now = win_time(); float dt = (float)(now - last); last = now; g_now = (float)(now - t0);
        if (dt > 0.1f) dt = 0.1f;
        if (win.keys[VK_F5] && !f5_prev && L.have_player) { fly ^= 1; if (!fly) L.player.cam_init = 0; }
        f5_prev = win.keys[VK_F5];
        /* camera */
        float speed = (win.keys[VK_SHIFT] ? 3000.0f : 600.0f) * dt;
        Vec3 fw = cam_forward(&cam), rt = cam_right(&cam);
        if (fly && win.keys['W']) { cam.pos.x += fw.x * speed; cam.pos.y += fw.y * speed; cam.pos.z += fw.z * speed; }
        if (fly && win.keys['S']) { cam.pos.x -= fw.x * speed; cam.pos.y -= fw.y * speed; cam.pos.z -= fw.z * speed; }
        if (fly && win.keys['D']) { cam.pos.x += rt.x * speed; cam.pos.z += rt.z * speed; }
        if (fly && win.keys['A']) { cam.pos.x -= rt.x * speed; cam.pos.z -= rt.z * speed; }
        if (fly && (win.keys['E'] || win.keys[VK_SPACE])) cam.pos.y += speed;
        if (fly && win.keys['Q']) cam.pos.y -= speed;
        if (fly) { cam.yaw -= win.mouse_dx * 0.004f; cam.pitch -= win.mouse_dy * 0.004f; }
        if (cam.pitch > 1.5f) cam.pitch = 1.5f; if (cam.pitch < -1.5f) cam.pitch = -1.5f;
        /* toggles */
        for (int k = 0; k < 3; k++) { int down = win.keys[VK_F1 + k]; if (down && !f_prev[k]) { if (k == 0) L.rnd.show_world ^= 1; else if (k == 1) L.rnd.show_instances ^= 1; else L.rnd.wireframe ^= 1; } f_prev[k] = down; }
        if (win.keys['P'] && !p_prev) paused ^= 1; p_prev = win.keys['P'];
        if (win.keys[VK_TAB] && !tab_prev && sel) {                                   /* next instance with animations */
            Instance *nxt = NULL; int found = 0;
            for (uint32_t mi = 0; mi < g_ins.nmodels && !nxt; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) {
                Instance *c = &g_ins.models[mi].instances[k];
                if (found && c->model->nanims > 1) { nxt = c; break; }
                if (c == sel) found = 1;
            }
            if (nxt) sel = nxt; else sel = &g_ins.models[0].instances[0];
            printf("selected instance %u (model with %u nodes, %u anims, type %d)\n", sel->index, sel->model->nnodes, sel->model->nanims, sel->type);
        }
        tab_prev = win.keys[VK_TAB];
        int br[2] = { win.keys[VK_OEM_4], win.keys[VK_OEM_6] };
        if (sel && sel->model->nanims) {
            if (br[0] && !br_prev[0]) { sel->anim = (sel->anim + (int)sel->model->nanims - 1) % (int)sel->model->nanims; sel->anim_time = 0; printf("anim %d (%u frames, %.2f s)\n", sel->anim, sel->model->anims[sel->anim].nframes, sel->model->anims[sel->anim].duration_s); }
            if (br[1] && !br_prev[1]) { sel->anim = (sel->anim + 1) % (int)sel->model->nanims; sel->anim_time = 0; printf("anim %d (%u frames, %.2f s)\n", sel->anim, sel->model->anims[sel->anim].nframes, sel->model->anims[sel->anim].duration_s); }
        }
        br_prev[0] = br[0]; br_prev[1] = br[1];
        /* player (provisional controller) + follow camera */
        if (L.have_player && !paused) {
            PlayerInput pin = { 0 };
            pin.forward = win.keys[VK_UP] || (!fly && win.keys['W']) || (now - t0 < walk_for);
            pin.back = win.keys[VK_DOWN] || (!fly && win.keys['S']);
            pin.left = win.keys[VK_LEFT] || (!fly && win.keys['A']); pin.right = win.keys[VK_RIGHT] || (!fly && win.keys['D']);
            pin.jump = (!fly && win.keys[VK_SPACE]) || (jump_at >= 0 && now - t0 >= jump_at && now - t0 < jump_at + jump_len) || (jump2_at >= 0 && now - t0 >= jump2_at && now - t0 < jump2_at + jump2_len); pin.action = win.keys[VK_CONTROL] || (!fly && win.keys[VK_SHIFT]) || (peck_at >= 0 && now - t0 >= peck_at && now - t0 < peck_at + peck_len);
            if (g_level == 0 && !fly) {                                             /* title: House is the backdrop of the menu (docs/GAMEFLOW.md 5); the 2D menu pages are not ported */
                memset(&pin, 0, sizeof pin);
                if (g_pose && !g_cin.state) { L.player.pos = g_pose->position; L.player.yaw = inst_yaw(g_pose); L.player.vel = (Vec3){ 0, 0, 0 }; }
                int32_t *iv = g_have_intro && (g_intro_var & 0xffffff) < L.vm.nvars ? &L.vm.varval[g_intro_var & 0xffffff] : NULL;
                int enter_key = win.keys[VK_RETURN] || (enter_at >= 0 && ((now - t0 >= enter_at && now - t0 < enter_at + 0.1) || (getenv("WOODY_ENTER2") && now - t0 >= enter_at + 2 && now - t0 < enter_at + 2.1))); int enter = enter_key && !enter_prev, cont = win.keys['L'] && !l_prev; enter_prev = enter_key; l_prev = win.keys['L'];
                if (enter && !new_game_pending) { new_game_pending = 1; if (iv && *iv == 0) eko_set_var(&L.vm, g_intro_var, 1); else enter = 0, new_game_pending = 2; }   /* page 1 -> 0x1f: the House script plays the intro */
                else if (enter && new_game_pending == 1) new_game_pending = 2;               /* a key skips the intro */
                if (new_game_pending == 1 && iv && *iv == 4) new_game_pending = 2;
                if (new_game_pending == 2) { save_reset(); save_write(); request_level(1, 0.5f); new_game_pending = 0; }   /* 0x44ffa0 + RequestLevel(0.5, WWS) */
                if (cont && !new_game_pending) request_level(1, 0.4f);                       /* load game -> world select -> hub; here straight to Woody's hub */
            }
            for (int k = 0; k < 3; k++) g_act_prev[k] = g_act_now[k];
            g_act_now[0] = pin.left; g_act_now[1] = pin.right; g_act_now[2] = pin.action;
            g_save.chr[g_char].lives = L.player.lives; g_save.chr[g_char].health = L.player.health;          /* the Perso writes straight into the save struct */
            if (g_cam.mode == 4 && !fly) memset(&pin, 0, sizeof pin);              /* cinematic camera: the player is frozen (0x459090) */
            cin_update(&L.vm, dt, g_now);
            if (!cin_running()) player_update(&L.player, &pin, dt, &L.vm, fly ? cam.yaw : L.player.cam_yaw);
            if (!cin_running()) enemies_update(&g_enemies, &L.player, cam.pos, dt);
            if (g_cam.plane_on) {                                                   /* 0x459eb0: the player stays on the vertical plane through the marker */
                Vec3 n = { -g_cam.plane_d.z, 0, g_cam.plane_d.x }; float off = (L.player.pos.x - g_cam.plane_a.x) * n.x + (L.player.pos.z - g_cam.plane_a.z) * n.z;
                L.player.pos.x -= n.x * off; L.player.pos.z -= n.z * off;
            }
            if (!fly) cam_update(&L.player, &cam, dt, g_cam.mode == 0x20 ? (pin.forward ? 2 : pin.back ? 3 : 0) : win.keys['C']); else cam.letterbox = 0;
            if (jump_at >= 0) { if (now - t0 < jump_at) start_y = L.player.pos.y; else if (L.player.pos.y > max_y) { max_y = L.player.pos.y; printf("jump apex so far %.1f above start at t=%.2f (jumper state %d)\n", max_y - start_y, now - t0 - jump_at, L.player.jumper.state); } }
        }
        /* VM tick: time in 1/100 s like the original */
        if (!paused) {
            eko_tick(&L.vm, (int32_t)((now - t0) * 100.0));
            { int n = g_nretry; g_nretry = 0; for (int i = 0; i < n; i++) { Instance *ri = slot_instance(g_retry[i].args[0]); if (ri && inst_msg(ri, g_retry[i].id, g_retry[i].args, g_retry[i].nargs, g_now) && g_nretry < 32) g_retry[g_nretry++] = g_retry[i]; } }
            for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) {
                Instance *ii = &g_ins.models[mi].instances[k];
                if (ii->scripted) inst_tick(ii, g_now, dt); else ii->anim_time += dt * ii->anim_speed;
                anim_sounds(ii);
            }
        }
        { Vec3 cr = cam_right(&cam); audio_listener(&cam.pos.x, &cr.x); audio_pause(paused); }   /* the listener is the camera (mgr+0x28) */
        rnd_frame(&L.rnd, &win, &cam, (float)(now - t0));
        {   /* 2D layer (docs/HUD_TEXT.md 5.4): HUD, then the text box, then the fades. No HUD in menus, BlackBox, cinematics and the fall death camera (0x401e19) */
            if (g_black_frame || (g_sfade.hold && !(g_sfade.rest > 0))) { rnd_fade(0); g_black_frame = 0; }                /* 1152 blanks the 3D picture only: the House intro shows its text on black */
            hud_begin(win.width, win.height);
            if (L.have_player && !fly && g_level >= 1 && g_level <= 24 && !cin_running() && (!g_cam.death_cam || g_hud_ext) && !getenv("WOODY_NOHUD")) {
                const Player *pl = &L.player; int race = pl->inst->type == 18 || pl->inst->type == 19;
                HudState hs = { g_char, race, pl->lives, race ? pl->race_bonus : pl->bonus_count, race ? pl->race_bonus : pl->bonus_got, pl->bonus_total,
                                g_level != 1 && g_level != 11 && g_level != 18, pl->unique_items, pl->special_charges, paused || g_hud_ext, pl->health, pl->charge * (2.0f / 3.0f) };
                hud_draw(&hs, dt);
            }
            { uint32_t v = g_text_var & 0xffffff; hud_text_draw(v < L.vm.nvars && L.vm.varval[v] != 0, paused ? 0 : dt); }
            hud_end(); g_hud_ext = 0;
        }
        /* level change: PgUp / PgDn cycle through the levels (debug); a request fades out, swaps the level, fades in */
        for (int k = 0; k < 2; k++) { int down = win.keys[k ? VK_NEXT : VK_PRIOR]; if (down && !pg_prev[k]) { int cur = g_level >= 0 && g_level < 27 ? g_level : 0; request_level((cur + (k ? 1 : 26)) % 27, 0.5f); } pg_prev[k] = down; }
        if (getenv("WOODY_SIDE") && now - t0 >= 1.0 && !g_cam.plane_on && L.have_player) { Instance *si = slot_instance(0x1000000 | (uint32_t)strtol(getenv("WOODY_SIDE"), NULL, 0)); if (si) cam_side_start(si, 2); }   /* testing: force the side view on a marker instance */
        if ((next_name && now - t0 >= next_at && !strcmp(next_name, "END")) || (win.keys[VK_END] && !end_prev)) {   /* End key / --next END T: finish the level as its exit door does (message 1083) */
            EkoMsg em; memset(&em, 0, sizeof em); em.id = 1083; on_msg(&L.vm, &em, NULL); if (next_name && !strcmp(next_name, "END")) next_name = NULL;
        }
        end_prev = win.keys[VK_END];
        if (next_name && now - t0 >= next_at) { request_level(level_index(next_name), 0.5f); next_name = NULL; }
        if (g_next_level >= 0) { g_switch_fade -= dt / g_fade_len; if (g_switch_fade < 0) g_switch_fade = 0; } else if (g_switch_fade < 1) { g_switch_fade += dt / 0.5f; if (g_switch_fade > 1) g_switch_fade = 1; }
        { float f = (L.have_player && !fly) ? L.player.fade : 1.0f; if (g_switch_fade < f) f = g_switch_fade;
          if (g_sfade.rest > 0 && g_sfade.total > 0) { float k = g_sfade.rest / g_sfade.total, b = g_sfade.out ? k : 1.0f - k; if (b < f) f = b; g_sfade.rest -= dt; if (g_sfade.rest <= 0 && g_sfade.out && !g_sfade.script) g_sfade.hold = 1; }
          /* a finished fade-out keeps the 3D picture black until the next fade-in; that is drawn under the 2D layer (above) */
          if (f < 1.0f) rnd_fade(f); }
        if (shot_path && now - t0 >= shot_after) { rnd_screenshot(&win, shot_path); printf("screenshot -> %s\n", shot_path); win.quit = 1; }
        win_swap(&win);
        frames++;
        if (g_next_level >= 0 && g_switch_fade <= 0) {
            const char *name = k_levels[g_next_level]; g_prev_level = g_level; g_level = g_next_level; g_next_level = -1;
            level_free(&L);
            if (level_load(&L, dir, name)) { fprintf(stderr, "level %s failed to load\n", name); return 1; }
            if (!L.have_player) fly = 1; else if (!have_cam) fly = 0;
            t0 = L.t0; last = win_time(); sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
            lvl = L.name; continue;
        }
        if (now - fps_t > 2.0) { char title[256]; snprintf(title, sizeof title, "WoodyRE%s - %s - %.0f fps - VM t=%d frame %u msgs %u - %s - woody %.0f %.0f %.0f %s - vol events %u - hearts %.0f lives %d bonus %d/%d", g_level == 0 ? " - TITLE: Enter = new game, L = continue" : "", lvl, frames / (now - fps_t), L.vm.time, L.vm.frame, L.vm.stat_msgs_total, fly ? "fly" : "play", L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.on_ground ? "ground" : "air", L.player.events_sent, L.player.health, L.player.lives, L.player.bonus_got, L.player.bonus_total); SetWindowTextA((HWND)win.hwnd, title); if (L.have_player) printf("player t=%.1f pos %.0f %.0f %.0f vel %.0f %.0f %.0f %s floor %.0f cam %.0f %.0f %.0f\n", now - t0, L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.vel.x, L.player.vel.y, L.player.vel.z, L.player.on_ground ? (L.player.floor_is_hull ? "hull" : "ground") : "air", L.player.floor_y, cam.pos.x, cam.pos.y, cam.pos.z); frames = 0; fps_t = now; }
    }
    level_free(&L); audio_shutdown(); win_close(&win);
    return 0;
}
