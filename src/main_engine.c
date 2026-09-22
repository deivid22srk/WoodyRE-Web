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
static float g_now; static double g_clock;           /* game time in seconds (VM time base): World+0x20 / +0x30 (0x401880), the sum of the CLAMPED frame times */
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
    /* mode 0x80 (docs/CAMERA_SCRIPT.md 4.3): the camera comes from a camera track in the animation an instance plays */
    Instance *anim_inst; int anim_letterbox; Vec3 anim_eye, anim_tgt;   /* CamMgr+0x5d4, +0x618 & 2, +0x1d0, +0x5d8 */
} g_cam = { 1 };
static const float k_sv_defaults[8] = { 1000, 300, 340, 500, 0, 400, 700, 200 };
static Camera *slot_camera(uint32_t ref) { uint32_t i = ref & 0xffffff; return i < g_ins.nslots + 16 ? g_ins.cam_slots[i] : NULL; }
static void cam_set_mode(int mode)                               /* SetMode 0x41f410 + 0x41eaa0 */
{
    if (getenv("WOODY_CAMLOG") && mode != g_cam.mode) printf("  CAM mode %d -> %d (%s)\n", g_cam.mode, mode, g_cam.cut ? "cut" : "travelling");
    if (!g_cam.cut) {
        g_cam.look_from = g_cam.active ? g_cam.look_cur : g_cam.look_off; g_cam.from_pos = g_cam.pos;
        g_cam.active = 1; if (g_cam.dur <= 0) g_cam.dur = 2.0f; g_cam.elapsed = 0; g_cam.t = 0;
    } else g_cam.active = 0;
    g_cam.mode = mode; if (mode != 0x80) g_cam.anim_inst = NULL;
    if (mode == 1 && g_player) g_player->cam_init = 0;
}
static float ramp_to(float v, float target, float step) { return v < target ? (v + step > target ? target : v + step) : (v - step < target ? target : v - step); }
/* 0x44de44: a Perso state change (scripted action, teleport, cinematic, death) ends the side view's plane lock */
static void plane_release(void) { if (!g_cam.plane_on) return; g_cam.plane_on = 0; if (g_cam.mode == 0x20) { g_cam.cut = 1; cam_set_mode(1); } puts("  side view: plane lock released"); }
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
    case 540: if (getenv("WOODY_CAMLOG")) for (uint32_t i = 0; i < c->traj.npoints; i++) printf("rail %u: %.0f %.0f %.0f closed %d", i, c->traj.points[i].x, c->traj.points[i].y, c->traj.points[i].z, c->traj.closed), puts("");
        if (c->traj.npoints >= 2) { g_cam.rail = &c->traj; g_cam.rail_d = (float)a1; g_cam.rail_first = 1; cam_set_mode(8); } break;
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
    if (g_cam.mode == 0x80 && g_cam.anim_inst) {                 /* 0x41f1ee: camera from the animation of CamMgr+0x5d4 (0x42fa80): cut, no smoothing.
                                                                  * That instance is the cinematic's main instance (0x44ed3f, letterboxed) or the Perso
                                                                  * himself during a scripted door action (0x44df7d, never letterboxed). */
        Instance *I = g_cam.anim_inst; const Model *mo = I->model; Vec3 eye, tgt;
        float L = (uint32_t)I->anim < mo->nanims && mo->anims[I->anim].duration_s > 0 ? mo->anims[I->anim].duration_s : 1.0f;
        if (ins_camera_eval(I, I->anim, I->anim_time / L, &eye, &tgt)) { g_cam.anim_eye = eye; g_cam.anim_tgt = tgt; }
        /* 0x41f21d sits inside the "has a camera track" test and 0x41f240 outside it, so an animation without one
         * leaves the previous eye and target standing: the camera holds that frame instead of snapping elsewhere */
        {   Vec3 e = g_cam.anim_eye, t = g_cam.anim_tgt, to = { t.x - e.x, t.y - e.y, t.z - e.z };
            cam->pos = e; cam->yaw = atan2f(to.x, to.z); cam->pitch = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z));
            cam->letterbox = g_cam.anim_letterbox; cam->fov_deg = g_cam.anim_letterbox ? 68.04f : 83.97f;
            g_cam.pos = e; g_cam.active = 0; return; }
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
            plane_release(); if (g_player && g_player->inst == m) player_place(g_player, P0, atan2f(P1.x - P0.x, P1.z - P0.z));
            m->scripted = 1; m->visible = 1; inst_play_once(m, g_cin.anim, 3.0f, now);
            for (int i = 0; i < g_cin.nactors; i++) inst_play_once(g_cin.actor[i].inst, g_cin.actor[i].anim, 3.0f, now);
            g_cam.anim_inst = m; g_cam.anim_letterbox = 1;                  /* 0x44ed3f / 0x44ed53: CamMgr+0x5d4 = the main instance, +0x618 |= 2 */
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
typedef struct { int32_t lives, unique, charges; float health; uint8_t done[29];
                 int32_t best[29], stats[29][4]; float stat_time[29]; } SaveChar;   /* rec+0x00 best, rec+0x04 done, rec+0x28..0x38 the five stats of that run */
static struct { uint32_t magic; SaveChar chr[3]; } g_save;
static int g_char, g_unlock_all;                                 /* cfg+0x380: 0 Woody, 1 Knothead, 2 Splinter */
#define SAVE_MAGIC 0x32565357u                                   /* "WSV2": the record grew a best score, so older files are dropped */
static void save_reset(void) { memset(&g_save, 0, sizeof g_save); g_save.magic = SAVE_MAGIC; for (int c = 0; c < 3; c++) { g_save.chr[c].lives = 9; g_save.chr[c].health = 3.0f; } }   /* 0x44ffa0 */
static int  save_write(void) { FILE *f = fopen("woodyre.sav", "wb"); if (!f) return 0; int ok = fwrite(&g_save, sizeof g_save, 1, f) == 1; fclose(f); return ok; }   /* 0x450b30; the menu shows page 8 or 9 */
static void save_read(void) { FILE *f = fopen("woodyre.sav", "rb"); save_reset(); if (f) { if (fread(&g_save, sizeof g_save, 1, f) != 1 || g_save.magic != SAVE_MAGIC) save_reset(); fclose(f); } }
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
/* vector marker of an instance (0x42f6b0): the `n`th 2-point node with typecode `tc`; P0 = start, dir = P1 - P0 (not
 * normalised). 0 when the instance has no such marker, which is how the callers that walk them count how many it has. */
static int inst_vector_at(const Instance *in, uint32_t tc, uint32_t n, Vec3 *p0, Vec3 *dir)
{
    const Model *mo = in->model; uint32_t seen = 0;
    for (uint32_t i = 0; i < mo->nnodes; i++) if (mo->nodes[i].kind != 0 && mo->nodes[i].type_code == tc && mo->nodes[i].npoints >= 2 && (mo->nodes[i].kind == 0x20 || tc == 5)) {
        if (seen++ != n) continue;
        Vec3 a = ins_point_world(in, mo->nodes[i].point_base), b = ins_point_world(in, mo->nodes[i].point_base + 1);
        *p0 = a; dir->x = b.x - a.x; dir->y = b.y - a.y; dir->z = b.z - a.z; return 1;
    }
    return 0;
}
static int inst_vector(const Instance *in, uint32_t tc, Vec3 *p0, Vec3 *dir) { return inst_vector_at(in, tc, 0, p0, dir); }
/* ---- results screen (docs/GAMEFLOW.md 5.1 and 10.1): the hub script ends a level with 1140 [door, var], the engine
 * puts the Perso on the door vector with scripted action 0x4a (he comes down at the door with his parasol, animation
 * 74 with its own camera track) and runs the state machine perso+0x724 while menu page 0x1e is up:
 *   0 arriving  -> the Perso is free again: action 0x4b (the pose that holds the panel), panel visible (state 1)
 *   1 panel     -> OK: n = categories with collected == total != 0 (0x453cf0), n unique items (0x44c840),
 *                  cheer action 0x4e (n != 0) or 0x4c (0x453fc0), state 2 / 3, panel hidden
 *   2 / 3 cheer -> cheer over: action 0x4d and state 4 (0x454020)
 *   4 score     -> score 0x453cb0 into the save when it beats the record (0x450230), then page 6 "Do you want to save?"
 *   5 leaving   -> 0x454050 started a 0.5 s fade-out and set +0x744 = 0.5; after it: prop hidden, fade-in 0.5 s,
 *                  camera back (0x41f9f0(2)), the Perso in front of the door and SetVar(perso+0x728, 1) (0x45422c),
 *                  which is what the hub script has been waiting for.
 * The exact end position of state 5 (0x454244..) and the layout of the panel are not decompiled; see docs/GAMEFLOW.md 10. */
static struct { int have, level; int stats[4]; float time; } g_stats;   /* app+0x74: copied from perso+0x710 by EndLevel (0x404c21) */
static struct {
    int on, state;                          /* perso+0x724 */
    uint32_t var;                           /* perso+0x728: the variable 1140 came with */
    float t;                                /* perso+0x744 */
    int page, sel;                          /* menu page 0x1e / 6 / 8 / 9 and its selected item */
    int race, cats, score, best, high;
    Vec3 door_p, door_d;
} g_res;

static int results_race(int level) { return level == 0xd || level == 0xf || level == 0x11 || level == 0x14 || level == 0x16 || level == 0x18; }   /* the R-levels */
static int results_score(const int *st, float time, int race)                      /* 0x453cb0 */
{
    int s = 0, t = (int)time;
    if (!race) { s += (t < 1800 ? 1800 - t : 0) * 10; s += st[2] * 100 + (st[0] && st[2] == st[0] ? st[2] * 50 : 0); }
    return s + st[3] * 100 + (st[1] && st[3] == st[1] ? st[3] * 50 : 0);                                              /* a complete category is worth 50 % more */
}
static int results_cats(const int *st) { return (st[0] && st[2] == st[0]) + (st[1] && st[3] == st[1]); }               /* 0x453cf0 */
/* the number behind string 127 "Level": the position of the level inside its own character's set */
static int results_level_no(int level) { return level >= 2 && level <= 10 ? level - 1 : level >= 12 && level <= 17 ? level - 11 : level >= 19 && level <= 24 ? level - 18 : level; }

static void script_action_camera(void)      /* the tail of 0x44dda0: an action whose animation carries a camera track becomes the camera (0x44df67) */
{
    Instance *pi = g_player ? g_player->inst : NULL; Vec3 e, t;
    if (pi && (uint32_t)pi->anim < pi->model->nanims && ins_camera_eval(pi, pi->anim, 0.0f, &e, &t)) {
        g_cam.anim_inst = pi; g_cam.anim_letterbox = 0; g_cam.anim_eye = e; g_cam.anim_tgt = t;
        g_cam.cut = 1; cam_set_mode(0x80);                                         /* 0x41f9f0(2) = cut, 0x41f410(7, 0) = mask 1 << 7; no letterbox, unlike a cinematic */
    }
}
static void results_action(int act) { Vec3 z = { 0, 0, 0 }; if (g_player) { player_script_action(g_player, act, 0, z, z); script_action_camera(); } }

static void results_capture(void)           /* 0x404c21: memcpy(app+0x74, perso+0x710, 20) before the level is unloaded */
{
    int race = g_player && (g_player->inst->type == 18 || g_player->inst->type == 19);
    int total = 0, dead = 0;
    for (int i = 0; i < g_enemies.n; i++) { total++; if (g_enemies.e[i].hp <= 0) dead++; }
    g_stats.have = 1; g_stats.level = g_level;
    g_stats.stats[0] = total;                                                        /* [0x4c5330]: every enemy counts itself in PostLoad */
    g_stats.stats[1] = g_player ? (race ? g_player->race_total : g_player->bonus_total) : 0;   /* [0x5e54f4] / [0x5e54e4] */
    g_stats.stats[2] = dead;                                                         /* [0x4c532c], which EndLevel copies over stat[2] */
    g_stats.stats[3] = g_player ? (race ? g_player->race_bonus : g_player->bonus_got) : 0;     /* [0x5e54e8] */
    g_stats.time = g_player ? g_player->play_time : 0.0f;                            /* the accumulator perso+0x710+0x10 */
    printf("  RESULTS stats of %s: %d/%d enemies, %d/%d bonuses, %.0f s", k_levels[g_level], g_stats.stats[2], g_stats.stats[0], g_stats.stats[3], g_stats.stats[1], g_stats.time), puts("");
}

static void results_begin(EkoVM *vm, Instance *door, uint32_t var)                    /* 0x453d90 */
{
    Vec3 p0, dir; int have = inst_vector(door, 5, &p0, &dir) || inst_vector(door, 0, &p0, &dir);
    if (!have) { float y = inst_yaw(door) + 3.14159265f; p0 = door->position; dir = (Vec3){ sinf(y), 0, cosf(y) }; }   /* no marker: out of the door */
    memset(&g_res, 0, sizeof g_res);
    g_res.on = 1; g_res.state = 0; g_res.var = var; g_res.page = 0x1e; g_res.door_p = p0; g_res.door_d = dir;
    if (!g_stats.have) { g_stats.level = g_prev_level; g_stats.time = 0; memset(g_stats.stats, 0, sizeof g_stats.stats); }   /* started straight in the hub */
    int lvl = g_stats.level, c = char_of_level(lvl) < 0 ? g_char : char_of_level(lvl);
    g_res.race = results_race(lvl);
    g_res.score = results_score(g_stats.stats, g_stats.time, g_res.race);
    g_res.best = (lvl >= 0 && lvl < 29) ? g_save.chr[c].best[lvl] : 0;
    g_res.high = g_res.score > g_res.best;
    eko_set_var(vm, var, 0);
    player_script_action(g_player, 0x4a, have, p0, dir);                              /* 0x453dbb: the arrival at the hub door */
    script_action_camera();
    if (g_prop) { g_prop->position = p0; mat4_from_trs(&g_prop->world, g_prop->position, g_prop->quat, g_prop->scale); g_prop->visible = 1; }   /* 0x4077f0 on perso+0x748 */
    printf("  RESULTS begin: level %s, score %d, best %d%s", k_levels[lvl >= 0 && lvl < 29 ? lvl : 0], g_res.score, g_res.best, g_res.high ? " (new record)" : ""), puts("");
}

static void results_store(void)                                                       /* 0x450230 / 0x450260 / 0x450380..0x450440 */
{
    int lvl = g_stats.level; if (lvl < 0 || lvl >= 29) return;
    SaveChar *sc = &g_save.chr[char_of_level(lvl) < 0 ? g_char : char_of_level(lvl)];
    if (g_res.score <= sc->best[lvl]) return;
    sc->best[lvl] = g_res.score;
    for (int i = 0; i < 4; i++) sc->stats[lvl][i] = g_stats.stats[i];
    sc->stat_time[lvl] = g_stats.time;
}
static void results_close(void) { fade_start(0.5f, 1); g_res.state = 5; g_res.t = 0.5f; g_res.page = 0; }   /* 0x454050 */

static void results_update(EkoVM *vm, float dt, int ok, int up, int dn)               /* the table 0x4542c4 of 0x454090 */
{
    if (!g_res.on || !g_player) return;
    switch (g_res.state) {
    case 0:                                                                            /* he is coming down; wait until the Perso is free again (+0x21c == 0) */
        if (!g_player->script_act) { results_action(0x4b); g_res.state = 1; }
        break;
    case 1:                                                                            /* 0x454560: the panel is up, OK closes it */
        if (ok) {
            g_res.cats = results_cats(g_stats.stats);
            g_player->unique_items += g_res.cats;                                      /* 0x44c840: n unique items straight into the save block */
            g_save.chr[g_char].unique = g_player->unique_items;
            results_action(g_res.cats ? 0x4e : 0x4c);                                  /* 0x453fc0: 0x453fd6 cheering, 0x453ffd shrugging */
            g_res.state = g_res.cats ? 2 : 3; audio_fx(63, NULL, NULL);
        }
        break;
    case 2: case 3:
        if (!g_player->script_act) { results_action(0x4d); results_store(); g_res.state = 4; g_res.page = 6; g_res.sel = 1; }   /* 0x454020; the cursor starts on the first selectable item, "Yes" */
        break;
    case 4:                                                                            /* "Do you want to save?" over the panel (pages 6 -> 8/9 -> 6) */
        if (g_res.page == 6) {
            if (up || dn) g_res.sel = g_res.sel == 1 ? 2 : 1;                          /* item 0 is a fixed heading and is skipped (0x446920) */
            if (ok) {
                if (g_res.sel == 1) { g_res.page = save_write() ? 8 : 9; g_res.sel = 1; }   /* one woodyre.sav, so no slot pages 5 / 0x17 / 0xc */
                else results_close();
            }
        } else if (ok) { g_res.page = 6; g_res.sel = 2; }                              /* 8 "Game Saved" / 9 "Save failed." -> back to the question (0x405358), now on "No" */
        break;
    default:                                                                           /* 5: the fade-out is running */
        if ((g_res.t -= dt) > 0) break;
        if (g_prop) g_prop->visible = 0;                                               /* 0x407850 */
        fade_start(0.5f, 0);
        g_player->script_act = 0; g_player->use_root = 0;                              /* "No" can come before 0x4d has played out; its root motion must not move him after this */
        player_place(g_player, g_res.door_p, (g_res.door_d.x * g_res.door_d.x + g_res.door_d.z * g_res.door_d.z) > 1e-6f ? atan2f(g_res.door_d.x, g_res.door_d.z) : g_player->yaw);
        g_cam.cut = 1; cam_set_mode(1); g_player->cam_init = 0;                        /* 0x41f9f0(2) + SetMode(0, 0) */
        eko_set_var(vm, g_res.var, 1);                                                 /* 0x45422c: the hub script opens the next door */
        save_write(); g_res.on = 0; g_res.page = 0; g_stats.have = 0;
        puts("  RESULTS done");
        break;
    }
}

/* ---- lasers: classes 50 / 51 / 52 (docs/OBJECTS.md 2.1). One beam per typecode-0 vector marker; off until message 50.
 * 51: marker start along the marker direction for `len` (message 52, default 400), cut at the first world polygon;
 * 50: endless until the first hit; 52: to the marker start of the target instance (message 53). Touching a beam kills (Kill(2)). */
typedef struct { Instance *inst, *target; int type, on; float len, phase; } Laser;
static Laser g_lasers[64]; static int g_nlasers;
static Laser *laser_of(const Instance *in) { for (int i = 0; i < g_nlasers; i++) if (g_lasers[i].inst == in) return &g_lasers[i]; return NULL; }
static int laser_segment(const Laser *z, uint32_t marker, const GelFile *gel, Vec3 *a, Vec3 *b, int *kind)
{
    const Model *mo = z->inst->model; uint32_t seen = 0;
    for (uint32_t i = 0; i < mo->nnodes; i++) {
        const InsNode *n = &mo->nodes[i]; if (n->kind != 0x20 || n->type_code != 0 || n->npoints < 2) continue;
        if (seen++ != marker) continue;
        Vec3 p0 = ins_point_world(z->inst, n->point_base), p1 = ins_point_world(z->inst, n->point_base + 1), d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (l < 1e-4f) return 0;
        float len = z->type == 50 ? 100000.0f : z->len; *a = p0; *kind = 0;
        if (z->type == 52) { Vec3 t0, td; if (z->target && inst_vector(z->target, 0, &t0, &td)) { *b = t0; *kind = 2; return 1; } if (marker) return 0; }
        b->x = p0.x + d.x / l * len; b->y = p0.y + d.y / l * len; b->z = p0.z + d.z / l * len;
        float t = gel_ray_frac(gel, *a, *b);
        if (t <= 1.0f) { b->x = a->x + (b->x - a->x) * t; b->y = a->y + (b->y - a->y) * t; b->z = a->z + (b->z - a->z) * t; *kind = 1; }
        return 1;
    }
    return 0;
}
/* hit test 0x450f80: segment against the player's cylinder (radius 69 * 0.85, centre at half height) */
static int laser_hits_player(Vec3 a, Vec3 b, const Player *p)
{
    const float R = 69.0f * 0.85f, H = 193.0f;
    Vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z }; float best = 1e30f;
    for (int i = 0; i <= 16; i++) {                                                 /* closest approach in xz, sampled (beams are short next to the player) */
        float t = i / 16.0f, x = a.x + d.x * t - p->pos.x, y = a.y + d.y * t - p->pos.y, zz = a.z + d.z * t - p->pos.z;
        if (y < 0 || y > H) continue; float q = x * x + zz * zz; if (q < best) best = q;
    }
    float l2 = d.x * d.x + d.z * d.z;
    if (l2 > 1e-6f) { float t = ((p->pos.x - a.x) * d.x + (p->pos.z - a.z) * d.z) / l2; t = t < 0 ? 0 : t > 1 ? 1 : t; float y = a.y + d.y * t - p->pos.y, x = a.x + d.x * t - p->pos.x, zz = a.z + d.z * t - p->pos.z; if (y >= 0 && y <= H && x * x + zz * zz < best) best = x * x + zz * zz; }
    return best <= R * R;
}
static uint32_t msvc_rand(void *user);
static Quat quat_from_axes(Vec3 X, Vec3 Y, Vec3 Z);

/* ---- exhaust smoke and explosion flashes -------------------------------------------------------------------------
 * Shared by the missiles (visual 0/1, docs/PROJECTILES.md 5.3-5.4) and the rideable rocket (class 20, docs/ROCKET.md 5.1):
 * both hang on the one exhaust list of 0x475440 and both end in flash records 0x4762e0, one per explosion radius. */
typedef struct { Vec3 pos; float t, rot; } Puff;              /* 0x475380: one smoke cloud, 0.2 s, image 14 */
typedef struct { Vec3 pos; float t, R; } Blast;               /* 0x4762e0: the nine flat quads of one flash, 0.3 s, image 12 */
static Puff g_puffs[96]; static int g_puff_next; static Blast g_blasts[16];
static void puff_add(Vec3 p) { Puff *q = &g_puffs[g_puff_next++ % 96]; q->pos = p; q->t = 1e-4f; q->rot = (float)msvc_rand(NULL) / 32767.0f; }
static void blast_add(Vec3 p, float R) { for (int i = 0; i < 16; i++) if (g_blasts[i].t <= 0) { g_blasts[i] = (Blast){ p, 1e-4f, R }; return; } }
/* 200 clouds a second, spread over the way the nozzle covered since the previous frame */
static void exhaust_smoke(Vec3 from, Vec3 to, float *acc, float dt)
{
    if (dt <= 0) return;
    *acc += dt * 200.0f; int n = (int)*acc; *acc -= n; if (n > 24) n = 24;
    for (int k = 0; k < n; k++) { float u = (k + 0.5f) / n;
        puff_add((Vec3){ from.x + (to.x - from.x) * u, from.y + (to.y - from.y) * u, from.z + (to.z - from.z) * u }); }
}
static void fx_smoke_draw(float dt)
{
    static const float white[3] = { 1, 1, 1 };
    static const float k_blast_n[9][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.7f, 0, 0.7f }, { -0.7f, 0, 0.7f },
                                           { 0.7f, 0.7f, 0 }, { -0.7f, 0.7f, 0 }, { 0, 0.7f, 0.7f }, { 0, -0.7f, 0.7f } };   /* 0x4762e0 */
    for (int i = 0; i < 96; i++) if (g_puffs[i].t > 0) { Puff *p = &g_puffs[i]; float u = p->t / 0.2f;
        hud_world_fx(14, &p->pos.x, 15.0f * (1 + u), p->rot, white, 0.3f * (1 - u)); if ((p->t += dt) >= 0.2f) p->t = 0; }
    for (int i = 0; i < 16; i++) if (g_blasts[i].t > 0) { Blast *b = &g_blasts[i]; float u = b->t / 0.3f;
        float size = b->R * (0.3f + 0.7f * sinf(u * 1.5707963f)), a = 0.3f * cosf(u * 1.5707963f);
        for (int q = 0; q < 9; q++) hud_world_fx_plane(12, &b->pos.x, k_blast_n[q], size, white, a);
        if ((b->t += dt) >= 0.3f) b->t = 0; }
}

/* ---- missile models, type 41 (docs/PROJECTILES.md 5.4) -------------------------------------------------------------
 * The type-41 instances of a level are a pool of missile models. They wait hidden (0x472530); a projectile with visual
 * 0/1 takes one (0x4722f0), it then follows that projectile with its +Z axis along the flight direction (0x46d320) and
 * its typecode-9 markers trailing flame, glow and smoke (0x475440), and at the end of the flight it goes back to the
 * pool (0x472370). An empty pool means no model and nothing else: the trail, the head and the explosion do not care.
 * Port deviation: the original swaps the taken entry to the front of the pool; here a flag keeps the records in place,
 * because the projectile holds a pointer to one. */
#define MISSILE_MARKS 4                                       /* typecode-9 nozzles per model that the port follows */
typedef struct { Instance *inst; int taken, nmark; float f1, f2, f3, acc; Vec3 prev[MISSILE_MARKS]; int has_prev[MISSILE_MARKS]; } Missile;   /* the exhaust object at inst+0x100 (0x4723f0) */
static Missile g_missiles[50]; static int g_nmissiles;
static Missile *missile_of(const Instance *in) { for (int i = 0; i < g_nmissiles; i++) if (g_missiles[i].inst == in) return &g_missiles[i]; return NULL; }
static void missile_add(Instance *in)                                              /* SetTypeInstance 41, 0x403b5d */
{
    if (missile_of(in) || g_nmissiles >= 50) return;                               /* 0x5e8344[]: 50 at most, the original stops with an error */
    Missile *mi = &g_missiles[g_nmissiles++]; memset(mi, 0, sizeof *mi); mi->inst = in;
    Vec3 m, d; while (mi->nmark < MISSILE_MARKS && inst_vector_at(in, 9, (uint32_t)mi->nmark, &m, &d)) mi->nmark++;   /* 0x4723f0 counts them the same way */
    in->visible = 0;                                                               /* 0x472530: hidden until a projectile takes it */
}
static void missile_place(Missile *mi, Vec3 pos, Vec3 dir)                         /* 0x4724e0 with the frame of 0x46d320: rows = model axes in world space */
{
    Instance *in = mi->inst; Vec3 X = { 1, 0, 0 }, Y = { 0, 1, 0 }, Z = { 0, 0, 1 };   /* a direction of length 0 leaves the model unturned */
    if (fabsf(dir.x) < 0.001f && fabsf(dir.z) < 0.001f) {                          /* straight up or down (0x46d375) */
        Vec3 v = { 0, dir.z, -dir.y }; float l = sqrtf(v.y * v.y + v.z * v.z);
        if (l > 1e-6f) { v.y /= l; v.z /= l; Z = dir; Y = v;
                         X = (Vec3){ v.y * dir.z - v.z * dir.y, v.z * dir.x - v.x * dir.z, v.x * dir.y - v.y * dir.x }; }
    } else {                                                                       /* 0x46d417 */
        Vec3 a = { dir.z, 0, -dir.x }; float l = sqrtf(a.x * a.x + a.z * a.z);
        if (l > 1e-6f) { a.x /= l; a.z /= l; Z = dir; X = a;
                         Y = (Vec3){ dir.y * a.z - dir.z * a.y, dir.z * a.x - dir.x * a.z, dir.x * a.y - dir.y * a.x }; }
    }
    in->position = pos; in->quat = quat_from_axes(X, Y, Z);
    mat4_from_trs(&in->world, in->position, in->quat, in->scale);
}
static Missile *missile_take(Vec3 pos, Vec3 dir)                                   /* 0x4722f0 */
{
    for (int i = 0; i < g_nmissiles; i++) {
        Missile *mi = &g_missiles[i]; if (mi->taken) continue;
        mi->taken = 1; mi->f1 = mi->f2 = mi->f3 = mi->acc = 0;
        for (int k = 0; k < MISSILE_MARKS; k++) mi->has_prev[k] = 0;               /* +0x2c[i] = 0: the trail starts here */
        mi->inst->visible = 1; mi->inst->noncollide = 1;                           /* 0x4077f0 puts it back in the cells; a missile model has no hull nodes, so nothing can walk into it */
        missile_place(mi, pos, dir);
        if (getenv("WOODY_FXLOG")) printf("missile %u (model %d, %d nozzle%s) takes off at %.0f %.0f %.0f", mi->inst->index, (int)(mi->inst->model - g_ins.models), mi->nmark, mi->nmark == 1 ? "" : "s", pos.x, pos.y, pos.z), puts("");
        return mi;
    }
    if (getenv("WOODY_FXLOG")) printf("missile pool (%d) empty: this shot flies without a model", g_nmissiles), puts("");
    return NULL;                                                                   /* the rest of the visual runs anyway */
}
static void missile_release(Missile *mi) { if (mi && mi->taken) { mi->taken = 0; mi->inst->visible = 0; } }   /* 0x472370 -> 0x407850 */
/* exhaust 0x475440 per nozzle, size table 0x4abcc8 index 3: flame 45, glows 40 / 35 (the rocket is index 6). The flame
 * is a billboard here, not three crossed quads. State 2 (on) from the moment the projectile takes the model, so there
 * is no start-up ramp; the smoke comes out over the way the nozzle covered, or the trail would be a string of dots. */
static void missile_exhaust(Missile *mi, float dt)
{
    static const float white[3] = { 1, 1, 1 };
    mi->f1 += dt * 0.05f; mi->f2 += dt * 0.15f; mi->f3 += dt * 3.0f;
    for (int k = 0; k < mi->nmark; k++) {
        Vec3 m, d; if (!inst_vector_at(mi->inst, 9, (uint32_t)k, &m, &d)) break;
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (l > 1e-4f) { d.x /= l; d.y /= l; d.z /= l; }
        float fl = 45.0f + (float)msvc_rand(NULL) / 32767.0f * 10.0f - 5.0f, fp[3] = { m.x + d.x * fl * 0.4f, m.y + d.y * fl * 0.4f, m.z + d.z * fl * 0.4f };
        hud_world_fx(32, &m.x, 40.0f, 1.0f - mi->f1, white, 1.0f - 0.2f * sinf(3.14159265f * mi->f1));
        hud_world_fx(32, &m.x, 35.0f, 1.0f - mi->f2, white, 1.0f - 0.2f * sinf(3.14159265f * mi->f2));
        hud_world_fx(31, fp, fl, mi->f3, white, 0.8f);
        if (mi->has_prev[k]) exhaust_smoke(mi->prev[k], m, &mi->acc, dt);
        mi->prev[k] = m; mi->has_prev[k] = 1;
    }
}

/* ---- launcher type 42 + projectiles (docs/PROJECTILES.md). Only template kind 1: straight line at 1000 u/s, radius 5,
 * 1 heart, removed on any hit or after `life` seconds. Visual 2 (the energy bolt every level script uses) and visual
 * 0/1 (the missile of the shooting enemies, section 5.3) are ported; homing, bounces, gravity, the bomb thrower
 * (kind 0) and the fireball of visual 3 (which is drawn as the bolt) are not. */
typedef struct { Instance *inst, *target; int active, count, aim, kind, visual; float T, t0, last, life; } Launcher;
typedef struct { int active, visual; const Instance *owner; Vec3 pos, dir, dir0, origin; float age, life, dying, speed, damage, steer; Enemy *enemy; Missile *missile; } Shot;
typedef struct { Vec3 pos; float t; int kind; } Flash;                             /* kind 0 = the flash of visual 2 (0x46f180), 1 = the muzzle flash of the missile (0x46fa40) */
static Launcher g_launchers[32]; static int g_nlaunchers;
static Shot g_shots[200]; static Flash g_flashes[64];
static Launcher *launcher_of(const Instance *in) { for (int i = 0; i < g_nlaunchers; i++) if (g_launchers[i].inst == in) return &g_launchers[i]; return NULL; }
static void flash_add(Vec3 p, int kind) { for (int i = 0; i < 64; i++) if (g_flashes[i].t <= 0) { g_flashes[i].pos = p; g_flashes[i].t = 1e-4f; g_flashes[i].kind = kind; return; } }
static int shot_is_missile(const Shot *s) { return s->visual == 0 || s->visual == 1; }
/* how long the trail keeps shrinking after the projectile is gone: seconds per segment / (3 * that per second),
 * 0.04 / 0.3 for the bolt (0x46f8a0) and 0.025 / 0.15 for the missile (0x46fb30) */
static float shot_fade_len(const Shot *s) { return shot_is_missile(s) ? 0.16667f : 0.13333f; }
/* 0x449130: the sound and the visual of a new projectile. Visual 0/1 take a missile model from the pool and open with
 * their own muzzle flash, 2 and 3 open with the flash of the bolt, and from 4 on a projectile is silent and invisible
 * (the thrown bomb of template 0). SoundFx 17/18/19/20 by visual (SOUND.md 5), 3D on the owner. */
static void shot_begin(Shot *s, const Instance *owner, int sound_fx)
{
    if (s->visual >= 4) return;                                                    /* jump table 0x4492b8 has four entries */
    if (owner) audio_fx(sound_fx, owner, &owner->position.x);
    if (shot_is_missile(s)) { s->missile = missile_take(s->pos, s->dir); flash_add(s->pos, 1); } else flash_add(s->pos, 0);
}
static int shot_sound(int visual) { return visual == 0 ? 17 : visual == 1 ? 18 : visual == 2 ? 19 : 20; }
static void launcher_fire(Launcher *l)                                             /* 0x452560 -> 0x4490a0 / 0x449130 */
{
    Vec3 p0, d; if (l->kind == 0 || !inst_vector(l->inst, 0, &p0, &d)) return;
    if (l->aim && l->target) { d.x = l->target->position.x - p0.x; d.y = l->target->position.y + 125.0f - p0.y; d.z = l->target->position.z - p0.z; }
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (len < 1e-4f) return;
    for (int i = 0; i < 200; i++) if (!g_shots[i].active) {
        Shot *s = &g_shots[i]; memset(s, 0, sizeof *s); s->active = 1; s->owner = l->inst; s->pos = s->origin = p0; s->dir = s->dir0 = (Vec3){ d.x / len, d.y / len, d.z / len }; s->life = l->life; s->speed = 1000.0f; s->damage = 1.0f; s->visual = l->visual;
        shot_begin(s, l->inst, shot_sound(s->visual)); return;
    }
}
/* projectile of a shooting enemy (0x418820): template 1 with the enemy's speed / damage / xz steering and its own
 * visual (P+0x74): 0/1 = the missile, 3 = the fireball, which is not ported and is drawn as the visual-2 bolt */
void game_enemy_shot(Enemy *owner, Vec3 pos, Vec3 dir, float speed, float damage, float steer, int visual, int sound_fx)
{
    for (int i = 0; i < 200; i++) if (!g_shots[i].active) {
        Shot *s = &g_shots[i]; memset(s, 0, sizeof *s); s->active = 1; s->owner = owner->inst; s->enemy = owner; s->pos = s->origin = pos; s->dir = s->dir0 = dir;
        s->life = 15.0f; s->speed = speed; s->damage = damage; s->steer = steer; s->visual = visual;
        shot_begin(s, owner->inst, sound_fx); return;
    }
}
static void launchers_update(float now, float dt, Player *pl, const GelFile *gel, int player_ok)
{
    for (int i = 0; i < g_nlaunchers; i++) {                                       /* think step 0x452780 */
        Launcher *l = &g_launchers[i]; if (!l->active) continue;
        if (floorf((now - l->t0) / l->T) > floorf((now - dt - l->t0) / l->T) && now - l->last >= l->T - 0.2f) {
            l->last = now; launcher_fire(l); if (l->count > 0 && --l->count == 0) l->active = 0;
        }
    }
    for (int i = 0; i < 200; i++) {                                                /* 0x4490f0 / 0x4493c0 */
        Shot *s = &g_shots[i]; if (!s->active) continue;
        if (s->dying > 0) { if ((s->dying -= dt) <= 0) s->active = 0; continue; }
        if (s->steer > 0 && player_ok) {                                           /* xz homing 0x4493c0: k = (1 - steer)^(dt * 60), never turns back past the launch direction */
            float k = powf(1.0f - s->steer, dt * 60.0f), tx = pl->pos.x - s->pos.x, tz = pl->pos.z - s->pos.z, tl = sqrtf(tx * tx + tz * tz);
            if (tl > 1e-3f) { float nx = tx / tl * (1 - k) + s->dir.x * k, nz = tz / tl * (1 - k) + s->dir.z * k, nl = sqrtf(nx * nx + nz * nz); if (nl > 1e-4f && (nx * s->dir0.x + nz * s->dir0.z) >= 0) { s->dir.x = nx / nl; s->dir.z = nz / nl; } }
        }
        s->age += dt; Vec3 a = s->pos, b = { a.x + s->dir.x * s->speed * dt, a.y + s->dir.y * s->speed * dt, a.z + s->dir.z * s->speed * dt }; int end = s->age >= s->life;
        if (!end && player_ok) {                                                   /* swept sphere r 5 against the cylinder r 69: 74, feet - 5 .. feet + 198 */
            Vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z }; float l2 = d.x * d.x + d.z * d.z, t = l2 > 1e-6f ? ((pl->pos.x - a.x) * d.x + (pl->pos.z - a.z) * d.z) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
            float x = a.x + d.x * t - pl->pos.x, y = a.y + d.y * t - pl->pos.y, z = a.z + d.z * t - pl->pos.z;
            if (x * x + z * z <= 74.0f * 74.0f && y >= -5.0f && y <= 198.0f) { if (player_hit(pl, s->damage, s->dir)) { player_kill(pl, 3); enemy_player_killed(s->enemy); } b = (Vec3){ a.x + d.x * t, a.y + d.y * t, a.z + d.z * t }; end = 1; }
        }
        if (!end) { float f = gel_ray_frac(gel, a, b); if (f <= 1.0f) { b = (Vec3){ a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f }; end = 1; } }
        s->pos = b;
        if (!end) { if (s->missile) missile_place(s->missile, s->pos, s->dir); continue; }                 /* 0x4723d0 -> 0x4724e0: the model rides along */
        /* 0x46fbca: a missile explodes (0x477060 kind 2, radius 400, no damage of its own) and hands its model back;
         * the bolt only leaves the flash of 0x46f36d. Either way the trail goes on shrinking for a moment. */
        if (shot_is_missile(s)) { blast_add(b, 400.0f); missile_release(s->missile); s->missile = NULL; } else flash_add(b, 0);
        s->dying = shot_fade_len(s);
        if (getenv("WOODY_FXLOG")) printf("shot %d (visual %d) ends at %.0f %.0f %.0f age %.2f from %.0f %.0f %.0f", i, s->visual, b.x, b.y, b.z, s->age, s->origin.x, s->origin.y, s->origin.z), puts("");
    }
    for (int i = 0; i < 64; i++) if (g_flashes[i].t > 0) { g_flashes[i].t += dt; if (g_flashes[i].t >= 0.4f) g_flashes[i].t = 0; }
}
/* death stars 0x477610 (docs/PERSO_DEATH.md 7): five sprites circle over the head of a dying enemy for 2.5 s, one turn, bobbing six times */
static struct { Enemy *e; float age; int img; } g_stars[16];
void game_enemy_stars(Enemy *e)
{
    for (int i = 0; i < 16; i++) if (!g_stars[i].e) { g_stars[i].e = e; g_stars[i].age = 0; g_stars[i].img = (rand() & 1) ? 11 : 10; return; }
}
static void stars_draw(float dt)
{
    static const float white[3] = { 1, 1, 1 }, glow[3] = { 1, 1, 0.5f };
    for (int s = 0; s < 16; s++) {
        Enemy *e = g_stars[s].e; if (!e) continue;
        float u = (g_stars[s].age += dt) / 2.5f;
        if (u >= 1 || e->removed || !e->inst->visible) { g_stars[s].e = NULL; continue; }
        Vec3 p0, d; float len;
        if (inst_vector(e->inst, 0, &p0, &d)) len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); else { p0 = e->inst->position; len = enemy_height(e); }
        float a = u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) * 3.33f;
        for (int i = 1; i <= 5; i++) {
            int ang = (i * 512 / 5 + (int)(512 * u)) % 512; float f = ang * 6 / 256.0f; int k = (int)f; float w = (k & 1) ? f - k : 1 - (f - k), r = ang * 6.2831853f / 512.0f;
            float pos[3] = { p0.x + 80 * cosf(r), p0.y + len + (4 * w) * (4 * w), p0.z + 80 * sinf(r) };
            if (g_stars[s].img == 10) { float gp[3] = { pos[0], pos[1] + 15, pos[2] }; hud_world_fx(5, gp, 40, 0, glow, 0.8f * a); }
            hud_world_fx(g_stars[s].img, pos, 40, (float)(int)((w - 0.5f) * 56) / 512.0f, white, a);
        }
    }
}
/* ---- pickup effects (docs/BONUS.md 2.4, 0x4793d0) --------------------------------------------------------------
 * One emitter record per pickup. Neither emitter draws: both spawn the same particle (0x4791f0), a camera facing
 * additive quad of bank 0 image 4 whose size swells 0 -> 30 -> 0 over its life. One pool as in the original
 * ([0x5e823c]+0xdb8, 2000 records, no free list: a dead record is swapped with the last one). */
static const float k_fx_shape[12][3] = {                                     /* 0x4b7990: the 8 corners of a cube, then a tetrahedron */
    { 1, 1, 1}, { 1, 1,-1}, { 1,-1, 1}, { 1,-1,-1}, {-1, 1, 1}, {-1, 1,-1}, {-1,-1, 1}, {-1,-1,-1},
    { 0, 0.5f, 0}, {-0.494f,-0.5f, 0.855f}, { 1,-0.5f, 0}, {-0.494f,-0.5f,-0.855f},
};
typedef struct { float age, life, acc; Vec3 pos; int kind, shape; } FxRec;   /* kind 0 = shape burst (0x478f70), 1 = sparkle box (0x4792d0), 2 = the particle (0x4791f0) */
static FxRec g_fx[2000]; static int g_nfx;
static FxRec *fx_new(float life, Vec3 pos, int kind)
{
    if (g_nfx >= 2000) return NULL;                                          /* 0x4793e7: a full pool silently drops the effect */
    FxRec *r = &g_fx[g_nfx++]; r->age = 0; r->life = life; r->acc = 0; r->pos = pos; r->kind = kind; r->shape = 0; return r;
}
static float fx_rnd(void) { return (float)rand() / (float)RAND_MAX; }        /* 0x43ff40: rand() / 32767, so [0,1] inclusive */
static void fx_rotmat(int a0, int a1, int a2, float M[9])                    /* 0x46d220; the angles are in 1/512 turn */
{
    const float k = 6.2831853f / 512.0f;
    float S0 = sinf(a0 * k), C0 = cosf(a0 * k), S1 = sinf(a1 * k), C1 = cosf(a1 * k), S2 = sinf(a2 * k), C2 = cosf(a2 * k);
    M[0] = C1 * C2;  M[1] = S0 * S1 * C2 + C0 * S2;  M[2] = S0 * S2 - C0 * S1 * C2;
    M[3] = -C1 * S2; M[4] = C0 * C2 - S0 * S1 * S2;  M[5] = S0 * C2 + C0 * S1 * S2;
    M[6] = S1;       M[7] = -S0 * C1;                M[8] = C0 * C1;
}
static struct { int kind; Vec3 pos; } g_pick[8]; static int g_npick;        /* pickups waiting to be projected: 0x448510 needs the view matrix, which the frame loop owns */
void game_pickup_fx(int n, Vec3 pos)                                         /* 0x4793d0: n = 0 life, 1 charge, 2 W, 3 unique, 4 race/invincible */
{
    FxRec *e;
    if (n == 0 || n == 1) { pos.y += 50.0f; if ((e = fx_new(2.0f, pos, 0)) != NULL) e->shape = n == 0 ? 1 : 0; }   /* tetrahedron resp. cube */
    else if (n >= 2 && n <= 4) fx_new(1.0f, pos, 1);
}
static void fx_update(float dt)
{
    static const float white[3] = { 1, 1, 1 };                               /* rgb 0.5 with the engine's x2 = full white; alpha is a constant 1 */
    for (int i = 0; i < g_nfx; i++) {                                        /* 0x470c70 re-reads the bound, so a particle born this frame also draws this frame */
        FxRec *e = &g_fx[i];
        float u = (e->age += dt) / e->life;
        if (u < 1.0f) {
            if (e->kind == 2) {                                              /* 0x4791f0: the only thing that draws. The fade in and out is the size, not the alpha */
                float size = 30.0f * sinf(3.14159265f * (int)(255.0f * u) / 256.0f);
                hud_world_fx(4, &e->pos.x, size, (float)(int)(45.0f * u) / 512.0f, white, 1.0f);
            } else if (e->kind == 0) {                                       /* 0x478f70: a rotating cage of spark sources that shrinks onto the point */
                float M[9]; fx_rotmat((int)(u * 255.5f), (int)(u * 408.8f), (int)(u * 511.0f), M);
                int first = e->shape ? 8 : 0, cnt = e->shape ? 4 : 8, reps;
                e->acc += dt * 60.0f; reps = (int)e->acc; e->acc -= reps;     /* the original emits one set per FRAME; normalised to its 60 Hz so the density does not follow our frame rate */
                for (int r = 0; r < reps; r++) for (int k = 0; k < cnt; k++) {
                    const float *S = k_fx_shape[first + k], s = 1.0f - u;
                    float X = s * S[0] * 40.0f, Y = s * S[1] * 40.0f, Z = s * S[2] * 40.0f;
                    Vec3 p = { e->pos.x + M[0] * X + M[3] * Y + M[6] * Z, e->pos.y + M[1] * X + M[4] * Y + M[7] * Z, e->pos.z + M[2] * X + M[5] * Y + M[8] * Z };
                    if (!fx_new(0.4f, p, 2)) break;
                }
            } else {                                                         /* 0x4792d0: 50 sparks a second in a box over the bonus, each 0.2 s */
                float acc = e->acc + dt; int n = (int)(acc * 50.0f); e->acc = acc - n * 0.02f;
                while (n-- > 0) {
                    Vec3 p = { e->pos.x + fx_rnd() * 60.0f - 30.0f, e->pos.y + (fx_rnd() + 1.0f) * 25.0f, e->pos.z + fx_rnd() * 60.0f - 30.0f };
                    if (!fx_new(0.2f, p, 2)) break;
                }
            }
            continue;
        }
        g_fx[i] = g_fx[--g_nfx]; i--;                                        /* 0x470cf4: swap with the last and look at this slot again */
    }
}

/* ---- footsteps (docs/FOOTSTEPS.md) -----------------------------------------------------------------------------
 * The walk cycle calls 0x47cba0(pos, ground normal, direction, foot, kind) twice per turn and a landing on ground
 * type 2 calls 0x476140(pos + (0,30,0), &normal, 3, 0.25, 1.5). Both effect functions are known by their call site
 * only - they are not decompiled - so what is drawn here is a reconstruction with the primitives the port has: a
 * mark that lies in the ground plane and is mirrored between the two feet (faint and short on ordinary ground,
 * kind 2; a lasting print on dust, sand or snow, kind 3), plus a puff of the smoke image for kind 3 and three
 * puffs on landing. The step SOUND is not from here: it comes from the type 4 events of the animation (docs/SOUND.md 3). */
typedef struct { Vec3 pos, n, dir; float t, life, size, strength; int mirror; } Mark;
typedef struct { Vec3 pos, vel; float t, life, size0, size1, rot, rgb[3]; } Dust;
static Mark g_marks[48]; static Dust g_dust[64];
static Mark *mark_new(void)
{
    Mark *pick = &g_marks[0]; float oldest = -1;
    for (int i = 0; i < 48; i++) { if (g_marks[i].life <= 0) return &g_marks[i]; if (g_marks[i].t > oldest) { oldest = g_marks[i].t; pick = &g_marks[i]; } }
    return pick;                                                             /* full: the oldest print makes room */
}
static const float k_dust_rgb[3] = { 1.0f, 0.95f, 0.85f };                   /* ground dust */
static const float k_sawdust_rgb[3] = { 0.85f, 0.68f, 0.44f };               /* what a beak raises out of wood */
static void dust_new(Vec3 pos, Vec3 vel, float life, float size0, float size1, const float *rgb)
{
    for (int i = 0; i < 64; i++) if (g_dust[i].life <= 0) {
        g_dust[i].pos = pos; g_dust[i].vel = vel; g_dust[i].t = 0; g_dust[i].life = life;
        g_dust[i].size0 = size0; g_dust[i].size1 = size1; g_dust[i].rot = fx_rnd();
        g_dust[i].rgb[0] = rgb[0]; g_dust[i].rgb[1] = rgb[1]; g_dust[i].rgb[2] = rgb[2]; return;
    }
}
static Vec3 vunit(Vec3 v) { float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); if (l < 1e-6f) return (Vec3){ 0, 1, 0 }; v.x /= l; v.y /= l; v.z /= l; return v; }
void game_footstep(Vec3 pos, Vec3 normal, Vec3 dir, int foot, int kind)      /* 0x47cba0, from 0x463f40 */
{
    Vec3 n = vunit(normal), d = vunit(dir);
    Vec3 side = vunit((Vec3){ d.y * n.z - d.z * n.y, d.z * n.x - d.x * n.z, d.x * n.y - d.y * n.x });
    float off = foot ? 22.0f : -22.0f;                                       /* the two feet beside the Perso position; the original passes only the flag */
    Vec3 fp = { pos.x + side.x * off, pos.y + side.y * off, pos.z + side.z * off };
    Mark *m = mark_new();
    m->pos = fp; m->n = n; m->dir = d; m->t = 0; m->mirror = foot;
    m->life = kind == 3 ? 4.0f : 0.6f; m->size = kind == 3 ? 36.0f : 30.0f; m->strength = kind == 3 ? 0.6f : 0.22f;
    if (kind == 3) {                                                         /* dust ground: the foot kicks a little of it up behind itself */
        Vec3 v = { -d.x * 30.0f + n.x * 40.0f, -d.y * 30.0f + n.y * 40.0f, -d.z * 30.0f + n.z * 40.0f };
        dust_new((Vec3){ fp.x + n.x * 10.0f, fp.y + n.y * 10.0f, fp.z + n.z * 10.0f }, v, 0.45f, 14.0f, 34.0f, k_dust_rgb);
    }
    if (getenv("WOODY_FXLOG")) printf("footstep %s kind %d at %.0f %.0f %.0f", foot ? "right" : "left", kind, fp.x, fp.y, fp.z), puts("");
}
void game_land_dust(Vec3 pos, Vec3 normal)                                   /* 0x476140(&pos + (0,30,0), &normal, 3, 0.25, 1.5), argument 3 = the number of particles */
{
    Vec3 n = vunit(normal);
    Vec3 a = vunit(fabsf(n.y) < 0.9f ? (Vec3){ n.z, 0, -n.x } : (Vec3){ 1, 0, 0 });     /* two axes in the ground plane */
    Vec3 b = { n.y * a.z - n.z * a.y, n.z * a.x - n.x * a.z, n.x * a.y - n.y * a.x };
    for (int i = 0; i < 3; i++) {
        float t = (i + fx_rnd() * 0.5f) * 2.0944f;                           /* three directions spread around the landing point */
        Vec3 o = { a.x * cosf(t) + b.x * sinf(t), a.y * cosf(t) + b.y * sinf(t), a.z * cosf(t) + b.z * sinf(t) };
        Vec3 v = { o.x * 150.0f + n.x * 60.0f, o.y * 150.0f + n.y * 60.0f, o.z * 150.0f + n.z * 60.0f };
        dust_new(pos, v, 0.5f, 18.0f, 52.0f, k_dust_rgb);
    }
    if (getenv("WOODY_FXLOG")) printf("landing dust at %.0f %.0f %.0f", pos.x, pos.y, pos.z), puts("");
}
/* ---- the beak impact 0x479c80(kind, point, normal) ------------------------------------------------------------
 * The original marks every place the beak lands: the attack probe calls it on any hit (kind 1, docs/PERSO_JUMP.md
 * 2.4) and the climb loop every 0.3 s on the wall he hangs in (kind 0, with the wall normal, docs/OBJECTS.md
 * 1.3/1.6). Like 0x47cba0 above, the function is known by its call sites only - not decompiled - but what has to
 * come out of it is not in doubt: a beak going into wood takes a bite out of it. So a peck cuts a HOLE in the face
 * it hit and throws the wood it knocked loose out of the wall as CHIPS that tumble and fall:
 *   - the hole is `hud_world_gouge` in the plane of that face, in its own pool, so walking does not push the holes
 *     out of the footstep pool and a wall keeps the whole trail he climbed;
 *   - the chips are `hud_world_chip`, solid slivers, not sprites, on the engine's own fall (vel.y -= dt*g*200 with
 *     the -800 floor and the air damping of the projectile templates, docs/PROJECTILES.md 1.1);
 *   - plus one small puff of sawdust out of the hole.
 * What was here before was the laser hit 0x46efb9 - an additive glow plus sparks at 50 a second. That is what an
 * energy bolt does to a wall, not what a woodpecker does to a plank. */
#define PECK_MARKS      64                                                   /* a peck every 0.3 s: one climbed wall is about 20 holes */
#define PECK_MARK_LIFE  20.0f                                                /* the hole stays put: he climbs in seconds and gets to look back at his trail */
#define PECK_MARK_FADE  4.0f                                                 /* and only closes over in its last seconds */
#define PECK_MARK_SIZE  26.0f                                                /* outer radius, against his own collision radius of 69 (P+0x04): a beak, not a foot */
#define PECK_CHIPS      10
#define CHIP_G          5.0f                                                 /* gravity in the engine's own units (0x44945f): vel.y -= dt * g * 200, floor -800. The thrown bomb uses 15, a chip of wood is light */
#define CHIP_DAMP       0.97f                                                /* damping per 1/60 s (T+0x28), heavier than a projectile's 0.99: wood flutters down instead of dropping like a stone */
typedef struct { Vec3 pos, n, dir; float t, life; unsigned seed; } Peck;
typedef struct { Vec3 pos, vel, ax, ay, rax; float t, life, spin, len, wid, rgb[3]; } Chip;
static Peck g_pecks[PECK_MARKS]; static int g_peck_next;
static Chip g_chips[128];
static Vec3 vrot(Vec3 v, Vec3 k, float a)                                    /* Rodrigues: a chip tumbles around its own axis */
{
    float c = cosf(a), s = sinf(a), d = (1.0f - c) * (k.x * v.x + k.y * v.y + k.z * v.z);
    return (Vec3){ v.x * c + (k.y * v.z - k.z * v.y) * s + k.x * d,
                   v.y * c + (k.z * v.x - k.x * v.z) * s + k.y * d,
                   v.z * c + (k.x * v.y - k.y * v.x) * s + k.z * d };
}
static void chip_new(Vec3 pos, Vec3 vel, float len, float wid, const float *rgb)
{
    for (int i = 0; i < 128; i++) if (g_chips[i].life <= 0) {
        Chip *c = &g_chips[i];
        c->pos = pos; c->vel = vel; c->t = 0; c->life = 0.9f + fx_rnd() * 0.6f;
        c->ay = vunit(vel);                                                  /* it leaves the wall long side first, then tumbles */
        Vec3 a = fabsf(c->ay.y) < 0.9f ? (Vec3){ 0, 1, 0 } : (Vec3){ 1, 0, 0 };
        c->ax = vunit((Vec3){ c->ay.y * a.z - c->ay.z * a.y, c->ay.z * a.x - c->ay.x * a.z, c->ay.x * a.y - c->ay.y * a.x });
        c->rax = vunit((Vec3){ fx_rnd() - 0.5f, fx_rnd() - 0.5f, fx_rnd() - 0.5f });
        c->spin = 6.0f + fx_rnd() * 12.0f; c->len = len; c->wid = wid;
        c->rgb[0] = rgb[0]; c->rgb[1] = rgb[1]; c->rgb[2] = rgb[2];
        return;
    }
}
void game_peck_fx(int kind, Vec3 pos, const Vec3 *n)                         /* 0x479c80 */
{
    static const float wood[3] = { 0.72f, 0.52f, 0.30f };                    /* fresh wood; every chip takes its own shade of it */
    Vec3 nn = n ? vunit(*n) : (Vec3){ 0, 1, 0 };
    if (n) {                                                                 /* there is a face to bite into, so the hole goes in it */
        Peck *m = &g_pecks[g_peck_next]; g_peck_next = (g_peck_next + 1) % PECK_MARKS;   /* the oldest hole gives way */
        m->pos = pos; m->n = nn; m->t = 0; m->life = PECK_MARK_LIFE; m->seed = (unsigned)rand();
        m->dir = fabsf(nn.y) < 0.9f ? (Vec3){ 0, 1, 0 } : (Vec3){ 1, 0, 0 };   /* he climbs upwards, so the hole stands up in the wall */
    }
    Vec3 t0 = vunit(fabsf(nn.y) < 0.9f ? (Vec3){ nn.z, 0, -nn.x } : (Vec3){ 1, 0, 0 });   /* two axes in the pecked face */
    Vec3 t1 = { nn.y * t0.z - nn.z * t0.y, nn.z * t0.x - nn.x * t0.z, nn.x * t0.y - nn.y * t0.x };
    for (int i = 0; i < PECK_CHIPS; i++) {                                   /* the wood he knocked out: away from the face, spread around the hole */
        float a = fx_rnd() * 6.2831853f, sp = 40.0f + fx_rnd() * 150.0f, out = 130.0f + fx_rnd() * 190.0f, ca = cosf(a), sa = sinf(a);
        Vec3 v = { nn.x * out + (t0.x * ca + t1.x * sa) * sp, nn.y * out + (t0.y * ca + t1.y * sa) * sp, nn.z * out + (t0.z * ca + t1.z * sa) * sp };
        v.y += 70.0f + fx_rnd() * 150.0f;                                    /* they hop up first, so the fall is what you see */
        float k = 0.8f + fx_rnd() * 0.45f, rgb[3] = { wood[0] * k, wood[1] * k, wood[2] * k };
        Vec3 p = { pos.x + nn.x * 6.0f + (t0.x * ca + t1.x * sa) * 7.0f, pos.y + nn.y * 6.0f + (t0.y * ca + t1.y * sa) * 7.0f, pos.z + nn.z * 6.0f + (t0.z * ca + t1.z * sa) * 7.0f };
        chip_new(p, v, 16.0f + fx_rnd() * 12.0f, 5.0f + fx_rnd() * 4.0f, rgb);   /* half axes: a chip is 32 to 56 long against Woody's 193, and has to read at the game's 84 degree fov */
    }
    dust_new((Vec3){ pos.x + nn.x * 8.0f, pos.y + nn.y * 8.0f, pos.z + nn.z * 8.0f },
             (Vec3){ nn.x * 40.0f, nn.y * 40.0f + 25.0f, nn.z * 40.0f }, 0.3f, 7.0f, 26.0f, k_sawdust_rgb);
    if (getenv("WOODY_FXLOG")) printf("peck kind %d at %.0f %.0f %.0f%s", kind, pos.x, pos.y, pos.z, n ? " (hole)" : ""), puts("");
}
/* the holes stay where they were pecked and the chips fall out of them; both are ticked from the same pass as the
 * footsteps, so a paused game (dt = 0) keeps drawing them without moving them. */
static void peck_draw(float dt)
{
    static const float hole[3] = { 0.75f, 0.78f, 0.82f };                    /* what it takes OUT of the wall: dst * (1 - rgb*strength), so a shade less blue stays warm */
    for (int i = 0; i < PECK_MARKS; i++) {
        Peck *m = &g_pecks[i]; if (m->life <= 0) continue;
        float left = m->life - m->t; if (left <= 0) { m->life = 0; continue; }
        float k = left < PECK_MARK_FADE ? left / PECK_MARK_FADE : 1.0f;
        hud_world_gouge(&m->pos.x, &m->n.x, &m->dir.x, PECK_MARK_SIZE, m->seed, hole, 0.72f * k, 0.13f * k);
        m->t += dt;
    }
    for (int i = 0; i < 128; i++) {
        Chip *c = &g_chips[i]; if (c->life <= 0) continue;
        float u = c->t / c->life; if (u >= 1.0f) { c->life = 0; continue; }
        float U[3] = { c->ax.x * c->wid, c->ax.y * c->wid, c->ax.z * c->wid };
        float V[3] = { c->ay.x * c->len, c->ay.y * c->len, c->ay.z * c->len };
        hud_world_chip(&c->pos.x, U, V, c->rgb, u < 0.7f ? 1.0f : (1.0f - u) / 0.3f);
        c->vel.y -= dt * CHIP_G * 200.0f; if (c->vel.y < -800.0f) c->vel.y = -800.0f;
        float d = powf(CHIP_DAMP, dt * 60.0f); c->vel.x *= d; c->vel.y *= d; c->vel.z *= d;
        c->pos.x += c->vel.x * dt; c->pos.y += c->vel.y * dt; c->pos.z += c->vel.z * dt;
        float a = c->spin * dt; c->ax = vrot(c->ax, c->rax, a); c->ay = vrot(c->ay, c->rax, a);
        c->t += dt;
    }
}
static void steps_draw(float dt)
{
    static const float mark_rgb[3] = { 0.55f, 0.45f, 0.35f };
    for (int i = 0; i < 48; i++) {
        Mark *m = &g_marks[i]; if (m->life <= 0) continue;
        float u = m->t / m->life; if (u >= 1.0f) { m->life = 0; continue; }
        float k = u < 0.5f ? 1.0f : (1.0f - u) * 2.0f;                       /* the print stays, then fades away over the second half of its life */
        hud_world_decal(hud_step_image(), &m->pos.x, &m->n.x, &m->dir.x, m->size, m->mirror, mark_rgb, m->strength * k);
        m->t += dt;
    }
    for (int i = 0; i < 64; i++) {
        Dust *d = &g_dust[i]; if (d->life <= 0) continue;
        float u = d->t / d->life; if (u >= 1.0f) { d->life = 0; continue; }
        hud_world_fx(14, &d->pos.x, d->size0 + (d->size1 - d->size0) * u, d->rot, d->rgb, 0.3f * (1.0f - u));
        d->pos.x += d->vel.x * dt; d->pos.y += d->vel.y * dt; d->pos.z += d->vel.z * dt;
        float slow = 1.0f - 2.5f * dt; if (slow < 0) slow = 0;               /* the puff runs out of speed as it fades */
        d->vel.x *= slow; d->vel.y *= slow; d->vel.z *= slow;
        d->t += dt;
    }
}

/* the visuals of a projectile: the bolt of visual 2 (0x46f8a0) and the missile of visual 0/1 (0x4700e0). Both are a
 * head sprite with a ribbon behind it, and the missile has the model and its exhaust on top of that (0x46fb30).
 * The original samples the ribbon from the path the projectile really flew; the port lays it out straight behind the
 * current direction, so a homing shot (only the type-7 enemy steers) drags its ribbon around with it. */
static void launchers_draw(const float *eye, float dt)
{
    static const float white[3] = { 1, 1, 1 };
    for (int i = 0; i < 200; i++) {
        Shot *s = &g_shots[i]; if (!s->active || s->visual >= 4) continue;
        int mis = shot_is_missile(s), nseg = mis ? 20 : 10;                         /* 500 in 20 resp. 400 in 10 (0x4a9998 / 0x4a964c) */
        float span = mis ? 500.0f : 400.0f, hw = mis ? 7.0f : 30.0f;
        float t = fmodf(s->age, 2.0f), k = s->dying > 0 ? s->dying / shot_fade_len(s) : 1.0f;
        float trav = sqrtf((s->pos.x - s->origin.x) * (s->pos.x - s->origin.x) + (s->pos.y - s->origin.y) * (s->pos.y - s->origin.y) + (s->pos.z - s->origin.z) * (s->pos.z - s->origin.z));
        float seg = (trav < span ? trav : span) * k / nseg;                         /* the ribbon grows from the muzzle and shrinks once the projectile is gone */
        for (int j = 0; j < nseg && seg > 0.01f; j++) {
            float u0 = (float)j / nseg, u1 = (float)(j + 1) / nseg, c0 = cosf(u0 * 1.5707963f), c1 = cosf(u1 * 1.5707963f);
            float ca[3], cb[3];
            if (mis) { for (int q = 0; q < 3; q++) { ca[q] = (1 - u0) * c0; cb[q] = (1 - u1) * c1; } }   /* rgb = 1 - u, alpha = cos(u*pi/2) */
            else { ca[0] = 0.25f * (1 - u0) * c0; ca[1] = 0.4f * (1 - u0) * c0; ca[2] = 0.45f * c0; cb[0] = 0.25f * (1 - u1) * c1; cb[1] = 0.4f * (1 - u1) * c1; cb[2] = 0.45f * c1; }
            Vec3 a = { s->pos.x - s->dir.x * seg * j, s->pos.y - s->dir.y * seg * j, s->pos.z - s->dir.z * seg * j }, b = { a.x - s->dir.x * seg, a.y - s->dir.y * seg, a.z - s->dir.z * seg };
            hud_world_ribbon(&a.x, &b.x, eye, hw, ca, cb);
        }
        if (s->dying > 0) continue;
        if (mis) {                                                                  /* head 0x46fbfa: on the nose, 55 in front of the projectile point */
            static const float flame[3] = { 1.0f, 0.58f, 0 };
            float hp[3] = { s->pos.x + s->dir.x * 55.0f, s->pos.y + s->dir.y * 55.0f, s->pos.z + s->dir.z * 55.0f };
            hud_world_fx(4, hp, 40.0f, t * 255.5f / 512.0f, flame, 1.0f - (t - floorf(t)));
            if (s->missile) missile_exhaust(s->missile, dt);
        } else {
            hud_world_fx(6, &s->pos.x, 50.0f, t * 255.5f / 512.0f, white, 1.0f);
            hud_world_fx(6, &s->pos.x, 80.0f, (1.0f - t * 0.5f) * 511.0f / 512.0f, white, 0.5f);
        }
    }
    for (int i = 0; i < 64; i++) if (g_flashes[i].t > 0) {                          /* muzzle / end flash, 0.4 s */
        float t = g_flashes[i].t, u = t / 0.4f, size = 200.0f * cosf(u * 1.5707963f);
        if (g_flashes[i].kind) { hud_world_fx(32, &g_flashes[i].pos.x, size, (1.0f - t * 0.5f), white, 0.5f - 0.5f * u); continue; }   /* 0x46fa40: one image-32 flash at the muzzle */
        hud_world_fx(6, &g_flashes[i].pos.x, size, t * 256.0f / 512.0f, white, 1.0f - u);
        hud_world_fx(4, &g_flashes[i].pos.x, size, (1.0f - t * 0.5f) * 512.0f / 512.0f, white, 0.5f - 0.5f * u);
    }
}
/* ---- rideable rocket, class 20 (docs/ROCKET.md): not steered. Message 40 seats the player, the rocket turns 2 s towards point 0 of
 * its own trajectory, ignites 0.3 s, flies a straight line (2000 u/s^2 up to vmax, no collision at all) and explodes after fly_time
 * seconds (blast 600: Kill(6) for a rider who stayed on); the last second it blinks red. 0.5 s later it is back at its start and fades
 * in over 1 s. Class 21 (the bomb cannon of W2x) waits for the bomb system. */
typedef struct { Instance *inst; int state, exhaust, has_prev; float t, speed, fly_time, vmax, ex_t, f1, f2, f3, puff_acc; Vec3 start_pos, prev_mk; Quat start_q, q0, q1; } Rocket;
static Rocket g_rockets[8]; static int g_nrockets;
static Rocket *rocket_of(const Instance *in) { for (int i = 0; i < g_nrockets; i++) if (g_rockets[i].inst == in) return &g_rockets[i]; return NULL; }
static void rocket_place(Rocket *r) { Instance *in = r->inst; mat4_from_trs(&in->world, in->position, in->quat, in->scale); ins_pose(in, in->anim, in->anim_time); }
static void rocket_reset(Rocket *r)                                                /* vtbl[17] 0x452ae0 */
{
    Instance *in = r->inst; if (r->state == 5 || r->state == 6) audio_fx_stop(11, in, 1); if (r->state == 3) audio_fx_stop(15, in, 1);
    in->position = r->start_pos; in->quat = r->start_q; r->state = 0; in->noncollide = 0; in->fade_rate = 1.0f; in->fade_target = 0; in->tint_red = 0;
    r->has_prev = 0; r->exhaust = 0;                                               /* the original leaves the exhaust state alone (ROCKET.md 10.6) */
    rocket_place(r);
}
static Quat quat_from_axes(Vec3 X, Vec3 Y, Vec3 Z)                                 /* images of the model axes = columns of the rotation */
{
    float m00 = X.x, m10 = X.y, m20 = X.z, m01 = Y.x, m11 = Y.y, m21 = Y.z, m02 = Z.x, m12 = Z.y, m22 = Z.z, t = m00 + m11 + m22; Quat q;
    if (t > 0) { float s = sqrtf(t + 1) * 2; q = (Quat){ (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, s * 0.25f }; }
    else if (m00 > m11 && m00 > m22) { float s = sqrtf(1 + m00 - m11 - m22) * 2; q = (Quat){ s * 0.25f, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s }; }
    else if (m11 > m22) { float s = sqrtf(1 + m11 - m00 - m22) * 2; q = (Quat){ (m01 + m10) / s, s * 0.25f, (m12 + m21) / s, (m02 - m20) / s }; }
    else { float s = sqrtf(1 + m22 - m00 - m11) * 2; q = (Quat){ (m02 + m20) / s, (m12 + m21) / s, s * 0.25f, (m10 - m01) / s }; }
    return q;
}
static Vec3 rocket_aim(const Rocket *r, Vec3 from)
{
    Vec3 d = { 0, 0, 1 }; if (r->inst->traj.npoints) { Vec3 p = r->inst->traj.points[0]; d = (Vec3){ p.x - from.x, p.y - from.y, p.z - from.z }; }
    float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (l > 1e-6f) { d.x /= l; d.y /= l; d.z /= l; } return d;
}
static void rocket_fly(Rocket *r, float dt)                                        /* 0x452cc0 */
{
    Vec3 d = rocket_aim(r, r->start_pos); r->speed += dt * 2000.0f; if (r->speed > r->vmax) r->speed = r->vmax;
    Instance *in = r->inst; in->position.x += d.x * r->speed * dt; in->position.y += d.y * r->speed * dt; in->position.z += d.z * r->speed * dt;
}
static void rockets_update(float dt, Player *pl, int have_player)                  /* think step 0x452e10 */
{
    for (int i = 0; i < g_nrockets; i++) {
        Rocket *r = &g_rockets[i]; Instance *in = r->inst;
        if (in->fade != in->fade_target) { float st = in->fade_rate * dt; in->fade = in->fade < in->fade_target ? (in->fade + st > in->fade_target ? in->fade_target : in->fade + st) : (in->fade - st < in->fade_target ? in->fade_target : in->fade - st); }
        if (!r->state) continue;
        in->tint_red = 0;
        switch (r->state) {
        case 1: r->t += dt; if (r->t >= 0.83f) r->state = 2; break;                /* Woody climbs on */
        case 2: { r->speed = 0; r->t = 0;                                          /* turn target 0x452ebf: model -Y = flight direction, model +Z = up */
            Vec3 f = rocket_aim(r, in->position), Y = { -f.x, -f.y, -f.z }; float k = Y.y;
            Vec3 Z = { -Y.x * k, 1 - Y.y * k, -Y.z * k }; float l = sqrtf(Z.x * Z.x + Z.y * Z.y + Z.z * Z.z); if (l < 1e-5f) { Z = (Vec3){ 0, 0, 1 }; l = 1; } Z.x /= l; Z.y /= l; Z.z /= l;
            Vec3 X = { Y.y * Z.z - Y.z * Z.y, Y.z * Z.x - Y.x * Z.z, Y.x * Z.y - Y.y * Z.x };
            r->q0 = in->quat; r->q1 = quat_from_axes(X, Y, Z); r->state = 3; audio_fx(15, in, &in->position.x); break; }
        case 3: r->t += dt; if (r->t > 2.0f) r->t = 2.0f; in->quat = q_slerp(r->q0, r->q1, r->t / 2.0f);
            if (r->t >= 2.0f) { r->t = 0; r->state = 4; audio_fx_stop(15, in, 1); audio_fx(16, in, &in->position.x); r->exhaust = 1; r->ex_t = 0; } break;
        case 4: r->t += dt; if (r->t >= 0.3f) { r->t = 0; r->state = 5; audio_fx(10, in, &in->position.x); audio_fx(11, in, &in->position.x); } break;
        case 5: rocket_fly(r, dt); r->t += dt; if (r->t >= r->fly_time - 1.0f) { r->state = 6; r->t = 0; } break;
        case 6: rocket_fly(r, dt); r->t += dt; in->tint_red = !((int)(r->t * 20.0f) & 1);   /* 0x4537d0: 10 Hz red / normal */
            if (r->t >= 1.0f) { audio_fx_stop(11, in, 1); audio_fx(6, in, &in->position.x); r->t = 0; r->state = 7;
                blast_add(in->position, 1400.0f); blast_add(in->position, 400.0f); } break;                    /* explosion kind 1: two flash records */
        case 7:                                                                    /* blast 600 on the registered actors: here the player (0x44d040) */
            if (have_player && !pl->dead_kind) { Vec3 d = { pl->inst->position.x - in->position.x, pl->inst->position.y - in->position.y, pl->inst->position.z - in->position.z };
                if (d.x * d.x + d.y * d.y + d.z * d.z < 600.0f * 600.0f && !getenv("WOODY_GOD")) { float l = sqrtf(d.x * d.x + d.z * d.z); Vec3 away = l > 1e-3f ? (Vec3){ d.x / l, 0, d.z / l } : (Vec3){ 0, 0, 1 };
                    player_hit(pl, 0, away); player_kill(pl, 6); printf("  ROCKET %u blast kills the player", in->index), puts(""); } }
            in->fade = in->fade_target = 1.0f; r->t = 0; r->state = 9; break;
        case 9: r->t += dt; if (r->t >= 0.5f) rocket_reset(r); break;
        }
        if (r->exhaust == 1 && (r->ex_t += dt) >= 1.0f) r->exhaust = 2;
        if (r->state) rocket_place(r);
        if (getenv("WOODY_FXLOG") && r->state >= 5 && r->state <= 7) printf("rocket %u state %d t %.2f pos %.0f %.0f %.0f speed %.0f", in->index, r->state, r->t, in->position.x, in->position.y, in->position.z, r->speed), puts("");
    }
    if (have_player && pl->ride) { Rocket *r = rocket_of(pl->ride); Vec3 d;
        if (r) { pl->ride_state = r->state; pl->ride_q = r->inst->quat; if (!inst_vector(r->inst, 0, &pl->ride_seat, &d)) pl->ride_seat = r->inst->position; } }
}
/* exhaust 0x475440 on the typecode-9 marker (table 0x4abcc8 index 6: flame 100, glows 70 / 60; start-up sputters) and its
 * smoke; the two flashes of explosion kind 1 (R 1400 and 400) are records like any other, see fx_smoke_draw.
 * The flame is a billboard here, not three crossed quads */
static void rockets_draw(float dt)
{
    static const float white[3] = { 1, 1, 1 };
    for (int i = 0; i < g_nrockets; i++) {
        Rocket *r = &g_rockets[i]; Vec3 m, d; if (!r->state || !r->exhaust || r->state >= 7 || !inst_vector(r->inst, 9, &m, &d)) continue;
        float s = 1.0f, ta = r->ex_t;
        if (r->exhaust == 1) s = ta < 0.15f ? ta * 6.667f : (ta > 0.3f && ta < 0.45f) ? (ta - 0.3f) * 6.667f : (ta > 0.85f && ta < 1.0f) ? (ta - 0.85f) * 6.667f : 0;
        r->f1 += dt * 0.05f; r->f2 += dt * 0.15f; r->f3 += dt * 3.0f;
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (l > 1e-4f) { d.x /= l; d.y /= l; d.z /= l; }
        if (s > 0) {
            float fl = (100.0f + (float)msvc_rand(NULL) / 32767.0f * 10.0f - 5.0f) * s, fp[3] = { m.x + d.x * fl * 0.4f, m.y + d.y * fl * 0.4f, m.z + d.z * fl * 0.4f };
            hud_world_fx(32, &m.x, 70.0f * s, 1.0f - r->f1, white, 1.0f - 0.2f * sinf(3.14159265f * r->f1));
            hud_world_fx(32, &m.x, 60.0f * s, 1.0f - r->f2, white, 1.0f - 0.2f * sinf(3.14159265f * r->f2));
            hud_world_fx(31, fp, fl, r->f3, white, 0.8f);
            if (r->has_prev) exhaust_smoke(r->prev_mk, m, &r->puff_acc, dt);       /* 200 puffs a second spread over the distance covered */
        }
        r->prev_mk = m; r->has_prev = 1;
    }
}
/* ---- environment instances, class 90 (0x472560), and their butterflies (0x47e050 / 0x47d440) -----------------------
 * The instance itself is never drawn - its model is a bare volume node. Message 1501 sets the mode (0x46cd07), 1504 the
 * number of butterflies (0x46cdcc), and the think function spawns them once at random points in the instance's volume
 * and then latches off. Each butterfly is a camera-facing sprite from bank 0 image 53..56 of Common/<character>.rck
 * that wanders inside that volume for ever. House slots 60/61/62 put 3 + 2 + 3 of them around the treehouse: they are
 * what flies over the title screen. Only mode 0 is ported; House is the only level that uses these at all. */
typedef struct { Instance *inst; int mode, count, spawned; } EnvInst;
typedef struct { Instance *owner; Vec3 pos, dir; float phase, wander, floor_y; int img, state; } Fly;
static EnvInst g_env[8]; static int g_nenv;
static Fly g_flies[64]; static int g_nflies;

static float frand01(void) { return (float)msvc_rand(NULL) / 32767.0f; }
static EnvInst *env_of(const Instance *in) { for (int i = 0; i < g_nenv; i++) if (g_env[i].inst == in) return &g_env[i]; return NULL; }

/* world AABB of the instance's first volume node, and whether a point is inside the volume itself (0x4300c0) */
static int env_volume(const Instance *in, float lo[3], float hi[3], uint32_t *node_out)
{
    const Model *m = in->model; if (!m->nvolume_nodes || !in->node_world) return 0;
    uint32_t ni = m->volume_nodes[0] - 1; if (ni >= m->nnodes) return 0;          /* the node lists in the file are 1-based */
    const InsNode *n = &m->nodes[ni]; if (!n->npoints) return 0;
    lo[0] = lo[1] = lo[2] = 1e30f; hi[0] = hi[1] = hi[2] = -1e30f;
    for (uint32_t k = 0; k < n->npoints; k++) {
        Vec3 w = ins_point_world(in, n->point_base + k); float v[3] = { w.x, w.y, w.z };
        for (int q = 0; q < 3; q++) { if (v[q] < lo[q]) lo[q] = v[q]; if (v[q] > hi[q]) hi[q] = v[q]; }
    }
    *node_out = ni; return 1;
}

static void env_update(float dt)
{
    for (int e = 0; e < g_nenv; e++) {                                            /* 0x4727d3: spawn `count` butterflies once */
        EnvInst *E = &g_env[e]; float lo[3], hi[3]; uint32_t node;
        if (E->mode != 0 || E->spawned >= E->count || !env_volume(E->inst, lo, hi, &node)) continue;
        while (E->spawned < E->count && g_nflies < 64) {
            Fly *f = &g_flies[g_nflies++]; memset(f, 0, sizeof *f);
            f->owner = E->inst; f->floor_y = lo[1];                               /* inst+0x10c, 0x472911 */
            f->pos = (Vec3){ lo[0] + (hi[0] - lo[0]) * frand01(), lo[1] + (hi[1] - lo[1]) * frand01(), lo[2] + (hi[2] - lo[2]) * frand01() };
            float dx = frand01() * 2 - 1, dy = frand01() * 2 - 1, dz = frand01() * 2 - 1, l = sqrtf(dx * dx + dy * dy + dz * dz);
            if (l < 1e-3f) { dx = 1; dy = 0; dz = 0; l = 1; }
            f->dir = (Vec3){ dx / l, dy / l, dz / l };
            f->phase = frand01() * 10.0f;                                         /* rec+0x00: the wing-flap phase */
            f->img = (int)(frand01() * 3.99f); if (f->img > 3) f->img = 3;        /* rec+0x28 = 0x10035..0x10038 */
            E->spawned++;
        }
    }
    for (int i = 0; i < g_nflies; i++) {
        Fly *f = &g_flies[i]; Instance *in = f->owner;
        f->phase += dt;
        if ((f->wander -= dt) <= 0) {                                             /* 0x4aab98: a new direction every 0.3 s */
            f->wander = 0.3f;
            float k = f->state == 1 ? -0.8f : (frand01() < 0.5f ? 0.7f : -0.5f);  /* state 1 = coming down to land */
            f->dir.x += frand01() * (f->dir.x < 0 ? -3.5f : 3.5f);                /* 0x4abd90 = 3.5, along the sign it already has */
            f->dir.z += frand01() * (f->dir.z < 0 ? -3.5f : 3.5f);
            f->dir.y += frand01() * k * 3.5f;
            float l = sqrtf(f->dir.x * f->dir.x + f->dir.y * f->dir.y + f->dir.z * f->dir.z);
            if (l > 1e-6f) { f->dir.x /= l; f->dir.y /= l; f->dir.z /= l; }
        }
        f->pos.x += f->dir.x * 100.0f * dt; f->pos.y += f->dir.y * 100.0f * dt; f->pos.z += f->dir.z * 100.0f * dt;   /* 0x4a9010 = 100 u/s */
        float lo[3], hi[3]; uint32_t node;
        if (env_volume(in, lo, hi, &node) && !volume_contains(in, node, f->pos)) {          /* outside: head back to the instance */
            Vec3 d = { in->position.x - f->pos.x, in->position.y - f->pos.y, in->position.z - f->pos.z };
            float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            if (l > 1e-3f) f->dir = (Vec3){ d.x / l, d.y / l, d.z / l };
            f->floor_y = lo[1];
        }
        float p = dt * 60.0f;                                                     /* the original rolls these per frame */
        if (f->state == 0) { if (frand01() < 0.001f * p) f->state = 1; }          /* 0x4a94c4 */
        else if (f->state == 1) { if (f->pos.y <= f->floor_y) { f->pos.y = f->floor_y; f->dir.y = 0; f->state = 2; } }
        else if (frand01() < 0.008f * p) f->state = 0;                            /* 0x4abd94: take off again */
    }
}

/* 0x47d440 draw: a butterfly is not a sprite but two squares hinged along the flight direction. V = the direction
 * flattened with a fixed +0.5 y, B0/B1 an orthonormal pair across it (0x46d320); the hinge angle
 * theta = 134.30 + 45.00 * cos(2 pi f t) sweeps 89.3..179.3 degrees at f = 3.90625 Hz (0x47d866: the table value
 * squared, times 128, subtracted from 255 and used as an angle). Wing 1 takes U = cos(t) B0 + sin(t) B1, wing 2
 * U' = -cos(t) B0 + sin(t) B1, so the two mirror about B1: flat open at one end of the sweep, folded together at
 * the other. Each square is 21.2132 (= 30 cos45) half-extents along V and U, its centre pushed 20.2757 along U so
 * the hinge edge sits on the particle, plus a 3-unit vertical bob in quadrature (0x47da2c). */
static void env_draw(void)
{
    for (int i = 0; i < g_nflies; i++) {
        Fly *f = &g_flies[i];
        Vec3 V = { f->dir.x, 0.5f, f->dir.z };
        float l = sqrtf(V.x * V.x + V.y * V.y + V.z * V.z); if (l < 1e-6f) continue;
        V.x /= l; V.y /= l; V.z /= l;
        Vec3 B0 = { V.z, 0, -V.x };                                            /* horizontal, across the flight direction */
        l = sqrtf(B0.x * B0.x + B0.z * B0.z); if (l < 1e-6f) { B0 = (Vec3){ 1, 0, 0 }; l = 1; }
        B0.x /= l; B0.z /= l;
        Vec3 B1 = { V.y * B0.z - V.z * B0.y, V.z * B0.x - V.x * B0.z, V.x * B0.y - V.y * B0.x };
        float w = 2 * 3.14159265f * 3.90625f * f->phase;
        float th = (134.30f + 45.0f * cosf(w)) * 3.14159265f / 180.0f, c = cosf(th), sn = sinf(th), bob = 3.0f * sinf(w);
        for (int k = 0; k < 2; k++) {
            float cc = k ? -c : c;
            Vec3 U = { cc * B0.x + sn * B1.x, cc * B0.y + sn * B1.y, cc * B0.z + sn * B1.z };
            float ctr[3] = { f->pos.x + 20.2757f * U.x, f->pos.y + 20.2757f * U.y + bob, f->pos.z + 20.2757f * U.z };
            hud_world_wing(f->img, ctr, &U.x, &V.x, 21.2132f);
        }
    }
}

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
    /* Every actor class carries the black outline. The renderer gates the ink line on SetFlags bit 0x20 (message 45,
     * docs/MODEL_RENDER.md 7); the level script sets that bit per instance and the Perso and the ordinary enemies are
     * in its list, but Buzz (type 14) stands in W1B without the line every other character has, so the bosses are not.
     * Classes are the second source of those bits in the original - type 40 sets bit 1 on itself (0x44d304) - and the
     * three boss classes 0x40eb50 / 0x40d850 / 0x40c730 are not ported, so the port sets the bit itself when the class
     * is assigned. Wherever the script already flags the instance this changes nothing; WOODY_SHLOG=1 lists the actors
     * that still come out without a rim, with the reason. */
    case 1200: if (in && m->nargs > 1) { in->type = (int)m->args[1]; if ((in->type >= 1 && in->type <= 16) || in->type == 18 || in->type == 19) in->setflags |= 0x20; if (g_player && (in->type == 1 || in->type == 2 || in->type == 3 || in->type == 18 || in->type == 19) && g_player->inst != in) { g_player->inst->scripted = 1; player_bind(g_player, in); in->scripted = 0; printf("player: instance %u (type %d) at %.0f %.0f %.0f\n", in->index, in->type, in->position.x, in->position.y, in->position.z); } if ((in->type >= 4 && in->type <= 9) || in->type == 13) enemies_add(&g_enemies, in, in->type); if (in->type == 34 && g_player) { g_player->bonus_total++; } if (in->type == 37 && g_player) { g_player->race_total++; } if (in->type == 20 && !rocket_of(in) && g_nrockets < 8) { Rocket *rk = &g_rockets[g_nrockets++]; memset(rk, 0, sizeof *rk); rk->inst = in; rk->start_pos = in->position; rk->start_q = in->quat; rk->fly_time = 10.0f; rk->vmax = 1000.0f; in->scripted = 0; }   /* 0x452890 */ if (in->type == 21) printf("type 21 (bomb cannon) instance %u: not ported", in->index), puts(""); if (in->type == 41) missile_add(in);   /* 0x403b5d: into the missile pool, hidden (0x472530) */ if (in->type == 90 && !env_of(in) && g_nenv < 8) { EnvInst *E = &g_env[g_nenv++]; E->inst = in; E->mode = 0; E->count = 0; E->spawned = 0; } if (in->type == 110) in->visible = 0;   /* 0x489210 (vtable[3]) puts these where the world-select carousel wants them every frame, so the original never draws them at their .ins position; that page is not ported, so keep them out of sight */ if (in->type == 42 && !launcher_of(in) && g_nlaunchers < 32) { Launcher *l = &g_launchers[g_nlaunchers++]; memset(l, 0, sizeof *l); l->inst = in; l->kind = 1; l->life = 15.0f; l->T = 1.0f; l->visual = 2; }   /* 0x452330(1): template 1 */ if (in->type >= 50 && in->type <= 52 && !laser_of(in) && g_nlasers < 64) { Laser *z = &g_lasers[g_nlasers++]; memset(z, 0, sizeof *z); z->inst = in; z->type = in->type; z->len = 400.0f; z->phase = (float)in->id; } if (getenv("WOODY_TYPELOG")) printf("  TYPE %d inst %u model %d visible %d fade %.2f pos %.0f %.0f %.0f", in->type, in->index, (int)(in->model - g_ins.models), in->visible, in->fade, in->position.x, in->position.y, in->position.z), puts(""); if (getenv("WOODY_VECLOG") && (in->type >= 1 && in->type <= 3)) for (uint32_t q = 0; q < g_ins.nslots; q++) { Vec3 vp, vd; Instance *w = g_ins.slots[q]; if (w && inst_vector(w, 5, &vp, &vd)) printf("  slot %u inst %u: vector5 at %.0f %.0f %.0f dir %.0f %.0f %.0f", q, w->index, vp.x, vp.y, vp.z, vd.x, vd.y, vd.z), puts(""); }   /* door / switch markers */ } break;   /* SetTypeInstance; [0x5e54e4] = Woody bonus total */
    case 1501: case 1504: {                                                         /* environment instance (class 90): 0x46cd07 mode, 0x46cdcc count */
        EnvInst *E = in ? env_of(in) : NULL;
        if (E && m->nargs > 1) { if (m->id == 1501) E->mode = (int)m->args[1]; else { E->count = (int)m->args[1]; E->spawned = 0; } }
        break; }
    case 16: case 18: case 19:                                                      /* texture frame override (docs/INSTANCE.md 2): the level-select doors turn their red
                                                                                     * cross into a green tick with it; 0x42db50 has no scripted test */
        if (in) inst_msg(in, m->id, m->args, m->nargs, g_now);
        break;
    case 1: case 2: case 3: case 4: case 5: case 6: case 12: case 13:               /* base class: animation, show/hide, path, fade (instance.c) */
    case 45:                                                                        /* SetFlags (0x42ddb4) is a plain store on every instance, the player included: bit
                                                                                     * 0x20 is what gives a model its black outline (docs/MODEL_RENDER.md 11) */
        if (in) inst_msg(in, m->id, m->args, m->nargs, g_now);
        break;
    case 42: case 43: case 44: case 56: case 57:
        if (in && in->scripted && inst_msg(in, m->id, m->args, m->nargs, g_now) && g_nretry < 32) g_retry[g_nretry++] = *m;
        break;
    case 40: if (in && rocket_of(in) && g_player) { Rocket *rk = rocket_of(in);      /* 0x452a50: only at rest, and only when the Perso accepts (state 0, on the ground) */
            if (rk->state == 0 && player_mount(g_player, in)) { rocket_reset(rk); rk->t = 0; rk->state = 1; in->noncollide = 1; Vec3 d; if (!inst_vector(in, 0, &g_player->ride_seat, &d)) g_player->ride_seat = in->position; g_player->ride_q = in->quat; } } break;
    case 29: if (in && rocket_of(in)) rocket_reset(rocket_of(in)); break;
    case 55: if (in && rocket_of(in) && m->nargs > 2) { Rocket *rk = rocket_of(in); if (m->args[1] == 1) rk->fly_time = (float)(int32_t)m->args[2] * 0.01f; else if (m->args[1] == 2) rk->vmax = (float)(int32_t)m->args[2]; } break;
    case 7: if (in) { int k = 0; for (int i = 0; i < g_nretry; i++) if (slot_instance(g_retry[i].args[0]) != in) g_retry[k++] = g_retry[i]; g_nretry = k; } break;
    case 10:                                                                        /* Collect (docs/BONUS.md): the level script saw the player enter the bonus volume */
        if (in && g_player && in->visible && player_collect(g_player, in->type, m->nargs > 1 ? (int)m->args[1] : 0)) {
            int t = in->type;
            Vec3 fp = in->position;
            if (t == 34 && in->node_world) { fp.x = in->node_world[0].m[12]; fp.y = in->node_world[0].m[13]; fp.z = in->node_world[0].m[14]; }   /* 0x44f630: the W sits on its animated volume node, not on inst.pos */
            int fx = t == 30 ? 0 : t == 35 ? 1 : t == 34 ? 2 : t == 36 ? 3 : (t == 37 || t == 38) ? 4 : -1;
            int kind = t == 30 ? 1 : t == 36 ? 2 : t == 35 ? 3 : t == 34 ? 4 : t == 37 ? 5 : 0;       /* 0x448510; type 38 has no HUD animation */
            in->visible = 0;                                                        /* 0x407850: cell = -1 */
            if (fx >= 0) game_pickup_fx(fx, fp);
            if (kind && g_npick < 8) { g_pick[g_npick].kind = kind; g_pick[g_npick].pos = fp; g_npick++; }   /* projected and started in the frame loop, where the camera is */
        }
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
        results_capture();                                                                                  /* memcpy(app+0x74, perso+0x710, 20): the results screen runs in the hub, after the switch */
        save_write(); request_level(g_char == 0 ? 1 : g_char == 1 ? 11 : 18, 0.5f); break;
    case 1082: if (m->nargs > 1) eko_set_var(vm, m->args[1], level_is_enable((int)m->args[0])); break;
    case 1085: if (m->nargs > 1) eko_set_var(vm, m->args[1], m->args[0] < 29 ? g_save.chr[g_char].done[m->args[0]] : 0); break;   /* LevelIsDone 0x4509e0 */
    case 1030: if (in && g_player) { g_player->spawn_pos = in->position; g_player->spawn_yaw = g_player->yaw; }   /* direction: the instance's vector node when it has one (not parsed), else the current facing */ break;   /* SaveAuto: checkpoint */
    case 1142: g_prop = in; break;
    case 1040:                                                                                              /* scripted Perso action 0x44dda0: 17 = walk into the door, 18 = come out of it (docs/PERSO_DEATH.md 2) */
        if (g_player && m->nargs > 1) {
            plane_release(); Vec3 p0 = { 0, 0, 0 }, dir = { 0, 0, 0 }; int have = in && inst_vector(in, 5, &p0, &dir);
            player_script_action(g_player, (int)m->args[1], have, p0, dir);
            /* 0x44df67 is the tail of 0x44dda0 itself, not a state-change hook (correcting docs/CAMERA_SCRIPT.md 4.3):
             * if the animation this action just started carries a camera track, that track becomes the camera. Woody's
             * animations 17 and 18 both have one, with the eye 371 units off his own axis - that is the sideways shot
             * of him walking into the door. 0x44df92 clears CamMgr+0x618 bit 1: no letterbox, unlike a cinematic. The
             * Perso+0x558 test at 0x44df73 is always true (set 24 instructions earlier) and is not ported. */
            script_action_camera();
        }
        break;
    case 1043: if (g_player) player_script_hold(g_player, 2.0f); break;
    case 26:                                                                                                /* Perso teleport 0x44ce11 [_, inst, mode]: 1 = position, 2 = position + direction of the vector marker */
        if (g_player && m->nargs > 2) {
            Instance *to = slot_instance(m->args[1]); int mode = (int)m->args[2];
            if (to && (mode == 1 || mode == 2)) {
                Vec3 p0, dir = { 0, 0, 0 }; int have = mode == 2 && (inst_vector(to, 5, &p0, &dir) || inst_vector(to, 0, &p0, &dir));
                plane_release(); eko_actor_leave_all(vm, g_player->inst->id); player_teleport(g_player, to->position, have, dir);
                /* 0x458f90 sits outside the state-5 test and cuts HERE, in script order: 0x41f9f0(2) + SetMode(0, 0). It has to happen
                 * inside the tick and not a frame later, because the rest of the tick usually puts the camera somewhere else and that has
                 * to win: an area gate in a hub sends 580 + 520 straight behind it (the fixed camera that watches the gate open,
                 * docs/CAMERA_SCRIPT.md 1.3, object 258 in WWS) and a door sends 1040 / action 18 (the camera track of the animation).
                 * Only the mode switch happens now: the follow camera seats itself (cam_init = 0) in the next camera update, which runs
                 * after this tick, so it still uses the position and facing the door action gives him. */
                g_cam.cut = 1; cam_set_mode(1);
                printf("  TELEPORT to inst %u (%.0f %.0f %.0f)%s", to->index, to->position.x, to->position.y, to->position.z, g_player->script_act ? " (refused: scripted action running, only the camera cuts)" : ""), puts("");
            }
        }
        break;
    case 1140:                                                                                              /* hub: the player arrives at the door he came out of and the results screen runs (0x453d90) */
        if (in && g_player && m->nargs > 1) results_begin(vm, in, m->args[1]);
        break;
    case 1042:                                                                                              /* peck switch: is the player at `inst` and pointing the same way as its marker? */
        if (m->nargs > 3) {
            /* 0x445269 (docs/OBJECTS.md 1.2), the engine half of every peck switch and every door in the game.
             * It answers only for a Perso who has his own controls (state 0) and stands on the ground, it measures
             * the xz distance to the START of the instance's own vector marker (typecode 0, else 5) - not to the
             * instance - and it does not test "looks at the switch" but "faces the same way as that marker".
             * On a yes it brakes the charge run that releasing the attack button started in the Perso update of this
             * same frame (0x44542f -> 0x458e40): that brake, animation 0x12, IS the peck the player sees at a switch.
             * Without it he keeps the 700 u/s of the charge run and storms into the thing he meant to peck. */
            int ok = 0, atk = g_player ? g_player->atk : 0; float d = 0, c = 0; int have = 0;
            if (in && g_player && player_state_free(g_player) && g_player->on_ground) {
                Vec3 p0, dir; have = inst_vector(in, 0, &p0, &dir) || inst_vector(in, 5, &p0, &dir);
                if (!have) { p0 = in->position; dir.x = p0.x - g_player->pos.x; dir.y = 0; dir.z = p0.z - g_player->pos.z; }   /* the original reads an uninitialised vector here; aim at the instance instead */
                float dx = p0.x - g_player->pos.x, dz = p0.z - g_player->pos.z, dl = sqrtf(dir.x * dir.x + dir.z * dir.z);
                d = sqrtf(dx * dx + dz * dz);
                c = dl < 1e-3f ? 1.0f : (sinf(g_player->yaw) * dir.x + cosf(g_player->yaw) * dir.z) / dl;   /* Mover direction . marker direction, both flattened */
                ok = d <= (float)(int)m->args[1] && c > cosf((float)(int)m->args[2] * 3.14159265f / 180.0f);   /* 0x445341: `dist` is raw, not x0.01 */
                if (ok) player_brake_charge(g_player);
            }
            if (getenv("WOODY_SWLOG") && in)
                printf("  1042 inst %u marker %d dist %.0f/%d angle %.0f/%d deg state %s atk %d -> %d", in->index, have, d, (int)m->args[1],
                       acosf(c < -1 ? -1 : c > 1 ? 1 : c) * 180.0f / 3.14159265f, (int)m->args[2],
                       !g_player ? "-" : !player_state_free(g_player) ? "busy" : !g_player->on_ground ? "air" : "free", atk, ok), puts("");
            eko_set_var(vm, m->args[3], ok);
        }
        break;
    case 1048: case 1049: case 1050:                                                                        /* key tests on actions 0, 1, 6 */
        if (m->nargs > 1) { int k = m->id - 1048, mode = (int)m->args[1], now = g_act_now[k], prev = g_act_prev[k];
                            int v = mode == 0 ? now : mode == 1 ? (now && !prev) : (!now && prev);            /* 0x467400 held, 0x467420 just pressed, 0x467440 just released */
                            if (getenv("WOODY_SWLOG") && v) printf("  %u action %d mode %d -> 1", m->id, k == 2 ? 6 : k, mode), puts("");
                            eko_set_var(vm, m->args[0], v); }
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
    case 1000: case 1001: case 1002: case 1003: case 1004: if (in) {                                         /* launcher, 0x444870 (docs/PROJECTILES.md) */
        Launcher *l = launcher_of(in); if (!l) break; int a1 = m->nargs > 1 ? (int)m->args[1] : 0, a2 = m->nargs > 2 ? (int)m->args[2] : 0, a3 = m->nargs > 3 ? (int)m->args[3] : 0;
        if (m->id == 1001) { l->kind = a1; l->life = 15.0f; l->aim = 0; l->active = 0; l->visual = a1 == 0 ? 4 : 2; }   /* template 0 is the thrown bomb: no visual of its own */
        else if (m->id == 1002) { if (a1 == 2) l->life = a2 * 0.01f; else if (a1 == 19) l->aim = a2 != 0;
            else if (a1 == 18 && a2 >= 0 && a2 <= 3) { static const int vis[4] = { 2, 3, 4, 0 }; l->visual = vis[a2]; } }   /* table 0x45254c */
        else if (m->id == 1004) l->active = 0;
        else {
            int tgt = m->id == 1000 ? a1 : a1, cnt = m->id == 1000 ? 1 : a2; float T = m->id == 1000 ? 1.0f : a3 * 0.01f; if (T < 0.2f) T = 0.2f;
            l->target = tgt == -1 ? NULL : slot_instance((uint32_t)tgt); l->count = cnt; l->T = T; l->t0 = (float)g_now + 1e-3f; l->last = (float)g_now - T; l->active = 1;
        }
    } break;
    case 11: if (in && m->nargs > 1) enemies_msg11(&g_enemies, in, (int)m->args[1], m->nargs > 2 ? (int)m->args[2] : 0); break;   /* Enemy::HandleMsg 0x41a740 */
    case 50: case 52: case 53: if (in) { Laser *z = laser_of(in); if (z && m->nargs > 1) { if (m->id == 50) z->on = m->args[1] == 1; else if (m->id == 52) z->len = (float)(int)m->args[1]; else z->target = slot_instance(m->args[1]); } } break;
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
    char name[32]; TexFile tex; GelFile gel; LitFile lit; int have_lit; VisFile vis; int have_vis; void *code; EkoVM vm; Renderer rnd;
    Player player; int have_player; double t0;
} Level;
static void *read_all(const char *path, size_t *sz);
static void level_free(Level *L)
{
    g_nlasers = 0; g_nlaunchers = 0; g_nmissiles = 0; memset(g_shots, 0, sizeof g_shots); memset(g_flashes, 0, sizeof g_flashes); hud_text_reset(); audio_stop_all(); audio_bank_free(1); audio_rtc(-1);                            /* vt[0x8c] StopAll on leaving a level (0x4049e0); the voices read instance memory */
    if (L->have_player) player_free(&L->player);
    memset(g_stars, 0, sizeof g_stars); g_nrockets = 0; g_nenv = 0; g_nflies = 0; g_nfx = 0; g_npick = 0; hud_anim_reset(); memset(g_puffs, 0, sizeof g_puffs); memset(g_blasts, 0, sizeof g_blasts); memset(g_marks, 0, sizeof g_marks); memset(g_dust, 0, sizeof g_dust); memset(g_pecks, 0, sizeof g_pecks); g_peck_next = 0; memset(g_chips, 0, sizeof g_chips); g_player = NULL; g_prop = NULL; g_pose = NULL; g_have_intro = 0; memset(&g_res, 0, sizeof g_res); g_enemies.n = 0; g_nretry = 0; memset(&g_cam, 0, sizeof g_cam); g_cam.mode = 1; memset(&g_sfade, 0, sizeof g_sfade); g_black_frame = 0; memset(&g_cin, 0, sizeof g_cin);
    rnd_free(&L->rnd); eko_free(&L->vm); free(L->code); ins_free(&g_ins); if (L->have_lit) lit_free(&L->lit); if (L->have_vis) vis_free(&L->vis); gel_free(&L->gel); tex_free(&L->tex);
    memset(L, 0, sizeof *L);
}
static int level_load(Level *L, const char *dir, const char *lvl)
{
    char path[512]; memset(L, 0, sizeof *L); snprintf(L->name, sizeof L->name, "%s", lvl);
    g_now = 0; g_clock = 0;                            /* the init scripts start animations / launchers against the new level's clock, not the previous level's */
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
    snprintf(path, sizeof path, "%s/%s/%s.vis", dir, lvl, lvl); L->have_vis = vis_load(&L->vis, path, L->gel.nsectors) == 0;   /* 0x408260: what each sector can see */
    if (getenv("WOODY_CELLLOG")) printf("  gel: %u cells, %u sectors, %u kd nodes | lit: %u lights, %u sector light lists\n", L->gel.ncells, L->gel.nsectors, L->gel.nkd, L->lit.nlights, L->lit.nsectors);
    rnd_init(&L->rnd, &L->tex, &L->gel, &g_ins, L->have_lit ? &L->lit : NULL, L->have_vis ? &L->vis : NULL);
    L->have_player = player_init(&L->player, &g_ins, &L->gel, &L->tex) == 0;
    g_player = L->have_player ? &L->player : NULL;
    for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) inst_init(&g_ins.models[mi].instances[k]);
    if (L->have_player) { L->player.inst->scripted = 0; L->player.enemies = &g_enemies; }
{ static const char *chr[3] = { "Woody", "Knothead", "Splinter" }; static int bank0 = -1, title_n;
      if (bank0 != g_char) { snprintf(path, sizeof path, "%s/../Common/%s.rck", dir, chr[g_char]); printf("sound bank 0: %d sounds\n", audio_bank_load(0, path)); bank0 = g_char; }
      snprintf(path, sizeof path, "%s/%s/%s.rck", dir, lvl, lvl); printf("sound bank 1: %d sounds\n", audio_bank_load(1, path));
      { char common[512]; snprintf(common, sizeof common, "%s/../Common/%s.rck", dir, chr[g_char]); if (hud_load(common, path)) printf("hud: no font / images\n"); }
      { uint32_t sky[5]; if (hud_sky_images(sky)) rnd_set_sky(&L->rnd, sky); }
      if (g_level == 0) audio_music((title_n++ & 1) ? 0 : 48); }                   /* 0x404e30: the title alternates Menu02 / Menu; levels send 1655 during init */
    printf("VM init...\n"); eko_init(&L->vm);
    printf("init done: %d messages\n", L->vm.nmsgs);
    for (int i = 0; i < L->vm.nmsgs; i++) on_msg(&L->vm, &L->vm.msgs[i], NULL);   /* docs/VM.md 2: the exe queues the messages and the game loop only takes the queue after the tick,
                                                                                  * so a variable one of them writes (1082 LevelIsEnable for the level-select doors) wakes its
                                                                                  * object in the first tick instead of in an init whose wake lists are cleared at the end */
    eko_msg_reset(&L->vm);
    if (L->have_player) { SaveChar *sc = &g_save.chr[g_char]; L->player.lives = sc->lives; L->player.health = sc->health > 0 ? sc->health : 1.0f;
                          L->player.unique_items = sc->unique; L->player.special_charges = sc->charges; }   /* 0x44a6a0 / 0x44a759 */
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
    int pick_type = 0, pre_bonus = -1; float pre_health = -1; double pick_at = 0;   /* --pickup TYPE T, --bonus N, --health N (testing) */
    int door_inst = -1, door_act = 17; double door_at = -1;                        /* --door INST ACT T (testing) */
    int new_game = 0; const char *next_name = NULL; double next_at = 0;                              /* --next LVL T: change to level LVL after T s (testing) */
    double walk_for = 0, walk_at = getenv("WOODY_WALKAT") ? atof(getenv("WOODY_WALKAT")) : 0; int fly = 0;                                             /* --walk T: hold forward for T s (testing); --fly: start in free camera */
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
        else if (!strcmp(argv[i], "--pickup") && i + 2 < argc) { pick_type = atoi(argv[i + 1]); pick_at = atof(argv[i + 2]); i += 2; }   /* --pickup TYPE T: collect a bonus of that type in front of the camera (testing) */
        else if (!strcmp(argv[i], "--door") && i + 3 < argc) { door_inst = atoi(argv[i + 1]); door_act = atoi(argv[i + 2]); door_at = atof(argv[i + 3]); i += 3; }   /* --door INST ACT T: send the script's own message 1040 (17 = walk into the door, 18 = come out) at T (testing) */
        else if (!strcmp(argv[i], "--bonus") && i + 1 < argc) { pre_bonus = atoi(argv[i + 1]); i += 1; }                                 /* --bonus N: start with N W's in the counter (testing) */
        else if (!strcmp(argv[i], "--health") && i + 1 < argc) { pre_health = (float)atof(argv[i + 1]); i += 1; }                        /* --health N: hearts before the pickup (testing) */
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) { have_yaw = 1; yaw_arg = (float)atof(argv[i + 1]) * 3.14159265f / 180; i += 1; }   /* with --pos: facing in degrees */
        else if (!strcmp(argv[i], "--unlock")) g_unlock_all = 1;                       /* every level door open */
        else if (!strcmp(argv[i], "--newgame")) new_game = 1;                          /* ignore woodyre.sav */
        else if (!strcmp(argv[i], "--prev") && i + 1 < argc) { g_prev_level = level_index(argv[i + 1]); i += 1; }   /* --prev LVL: pretend we came from LVL (hub spawn point) */
        else if (!strcmp(argv[i], "--stats") && i + 5 < argc) {                        /* --stats TOTAL_A GOT_A TOTAL_B GOT_B SECONDS: the five level statistics the results screen shows (testing) */
            g_stats.have = 1; g_stats.stats[0] = atoi(argv[i + 1]); g_stats.stats[2] = atoi(argv[i + 2]);
            g_stats.stats[1] = atoi(argv[i + 3]); g_stats.stats[3] = atoi(argv[i + 4]); g_stats.time = (float)atof(argv[i + 5]); i += 5; }
        else if (!strcmp(argv[i], "--next") && i + 2 < argc) { next_name = argv[i + 1]; next_at = atof(argv[i + 2]); i += 2; }
    }
    Window win; if (win_open(&win, "WoodyRE", 1280, 800)) return 1;
    if (g_stats.have) g_stats.level = g_prev_level;                                    /* --stats belongs to the level --prev says we came from */
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
    double t0 = L.t0, last = t0; int pg_prev[2] = {0, 0}, end_prev = 0, enter_prev = 0, l_prev = 0, new_game_pending = 0, title_page = 0, title_sel = 0, title_prev[2] = {0, 0}; float title_t = 0; int paused = 0, tab_prev = 0, br_prev[2] = {0, 0}, f_prev[4] = {0, 0, 0, 0}, p_prev = 0, f5_prev = 0, menu_prev[3] = {0, 0, 0}; uint32_t frames = 0; double fps_t = t0;
    while (!win.quit) {
        win_poll(&win);
        double now = win_time(); float dt = (float)(now - last); last = now;
        if (dt > 0.1f) dt = 0.1f;
        g_clock += dt; g_now = (float)g_clock;         /* 0x401880: everything (Perso timers, animations, the script VM) runs on this one clock, so a hitch cannot make script delays
                                                        * run ahead of the action timers - a door would then teleport while action 17 is still running and 0x44a650 refuses the move */
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
        {   /* F4 steps the visibility back: frustum + .vis -> frustum only -> the whole level every frame */
            static const char *cn[3] = { "off (whole level)", "frustum only", "frustum + .vis" };
            int down = win.keys[VK_F4], top = L.gel.nsectors ? (L.rnd.vis ? 2 : 1) : 0;
            if (down && !f_prev[3]) { L.rnd.cull = L.rnd.cull ? L.rnd.cull - 1 : top; L.rnd.sec_dirty = 1; printf("culling: %s\n", cn[L.rnd.cull]); }
            f_prev[3] = down;
        }
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
        static double pf[5]; static int pfn; static const int prof = 1; double pt0 = win_time();
        /* player (provisional controller) + follow camera */
        if (L.have_player && !paused) {
            PlayerInput pin = { 0 };
            pin.forward = win.keys[VK_UP] || (!fly && win.keys['W']) || (now - t0 >= walk_at && now - t0 < walk_at + walk_for);
            if (getenv("WOODY_INSTLOG") && (int)(now - t0) != (int)(now - t0 - dt)) { Instance *qi = slot_instance((uint32_t)atoi(getenv("WOODY_INSTLOG"))); if (qi) printf("instlog %u: visible %d fade %.2f type %d scripted %d anim %d pos %.0f %.0f %.0f model %d", qi->index, qi->visible, qi->fade, qi->type, qi->scripted, qi->anim, qi->position.x, qi->position.y, qi->position.z, (int)(qi->model - g_ins.models)), printf(" nw0 %.0f %.0f %.0f cull_r %.0f anim_time %.2f speed %.2f alpha? setflags %x", qi->node_world[0].m[12], qi->node_world[0].m[13], qi->node_world[0].m[14], qi->model->cull_r, qi->anim_time, qi->anim_speed, qi->setflags), puts(""); }
            /* WOODY_UVLOG=<slot> or =stand (the instance the player is standing on, Perso+0x298): one UV report per
             * instance, to tell a wrong texture from a wrong projection on a surface that looks untextured */
            if (getenv("WOODY_UVLOG")) {
                const char *e = getenv("WOODY_UVLOG"); static const Instance *uv_done;
                const Instance *qi = e[0] >= '0' && e[0] <= '9' ? slot_instance((uint32_t)atoi(e)) : L.player.att_inst;
                if (qi && qi != uv_done) { uv_done = qi; rnd_uv_report(&L.rnd, qi); }
            }
            if (getenv("WOODY_POSLOG") && (int)((now - t0) * 4) != (int)((now - t0 - dt) * 4)) {
                /* dev = angle between the camera->player direction and his facing: 0 = camera exactly behind him, +-180 = in front of him */
                float ax = L.player.pos.x - cam.pos.x, az = L.player.pos.z - cam.pos.z, fx = sinf(L.player.yaw), fz = cosf(L.player.yaw);
                printf("pos t %.2f: %.0f %.0f %.0f yaw %.0f ground %d | cam %.0f %.0f %.0f yaw %.0f dev %.0f mode %d", now - t0, L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.yaw * 57.3f, L.player.on_ground,
                       cam.pos.x, cam.pos.y, cam.pos.z, cam.yaw * 57.3f, atan2f(fx * az - fz * ax, fx * ax + fz * az) * 57.3f, g_cam.mode), puts("");
            }
            pin.back = win.keys[VK_DOWN] || (!fly && win.keys['S']);
            pin.left = win.keys[VK_LEFT] || (!fly && win.keys['A']); pin.right = win.keys[VK_RIGHT] || (!fly && win.keys['D']);
            pin.jump = (!fly && win.keys[VK_SPACE]) || (jump_at >= 0 && now - t0 >= jump_at && now - t0 < jump_at + jump_len) || (jump2_at >= 0 && now - t0 >= jump2_at && now - t0 < jump2_at + jump2_len); pin.action = win.keys[VK_CONTROL] || (!fly && win.keys[VK_SHIFT]) || (peck_at >= 0 && now - t0 >= peck_at && now - t0 < peck_at + peck_len);
            /* menu keys: confirm (action 0xc), up (2) and down (3), all "just pressed" like 0x467420 */
            int mk[3] = { win.keys[VK_RETURN] || win.keys[VK_SPACE], win.keys[VK_UP] || win.keys['W'], win.keys[VK_DOWN] || win.keys['S'] };
            int menu_ok = mk[0] && !menu_prev[0], menu_up = mk[1] && !menu_prev[1], menu_dn = mk[2] && !menu_prev[2];
            for (int k = 0; k < 3; k++) menu_prev[k] = mk[k];
            if (g_res.on) memset(&pin, 0, sizeof pin);                              /* Perso state 9: the results screen has the controls */
            if (g_level == 0 && !fly) {                                             /* title: House is the backdrop of the menu (docs/GAMEFLOW.md 5); the 2D menu pages are not ported */
                memset(&pin, 0, sizeof pin);
                if (g_pose && !g_cin.state) { L.player.pos = g_pose->position; L.player.yaw = inst_yaw(g_pose); L.player.vel = (Vec3){ 0, 0, 0 }; }
                int32_t *iv = g_have_intro && (g_intro_var & 0xffffff) < L.vm.nvars ? &L.vm.varval[g_intro_var & 0xffffff] : NULL;
                int synth = enter_at >= 0 && ((now - t0 >= enter_at && now - t0 < enter_at + 0.1) || (getenv("WOODY_ENTER2") && now - t0 >= enter_at + 2 && now - t0 < enter_at + 2.1));
                int ok_key = win.keys[VK_RETURN] || win.keys[VK_SPACE], up_key = win.keys[VK_UP] || win.keys['W'], dn_key = win.keys[VK_DOWN] || win.keys['S'];
                int ok = ok_key && !enter_prev, up = up_key && !title_prev[0], dn = dn_key && !title_prev[1], syn = synth && !l_prev;
                enter_prev = ok_key; title_prev[0] = up_key; title_prev[1] = dn_key; l_prev = synth;
                int start_new = 0, cont = 0;
                if (new_game_pending == 1) { if (ok || syn) new_game_pending = 2; }          /* a key skips the intro */
                else if (syn) start_new = 1;                                                 /* --enter T: straight to New game (testing) */
                else if (title_page == 0) { if (ok || (win.keys[VK_ESCAPE] && 0)) { title_page = 1; title_sel = 0; audio_fx(63, NULL, NULL); } }   /* page 0 -> 1, SoundFx 63 on entering a panel page */
                else if (title_page == 1) {
                    if (up) title_sel = (title_sel + 3) % 4; if (dn) title_sel = (title_sel + 1) % 4;
                    if (ok) { if (title_sel == 0) start_new = 1; else if (title_sel == 1) cont = 1; else if (title_sel == 3) win.quit = 1; }   /* Options is not ported; Quit skips the "are you sure" page 0x1c */
                }
                if (start_new) { new_game_pending = 1; title_page = -1; if (iv && *iv == 0) eko_set_var(&L.vm, g_intro_var, 1); else new_game_pending = 2; }   /* result 2: the House script plays the intro (page 0x1f) */
                if (new_game_pending == 1 && iv && *iv == 4) new_game_pending = 2;
                if (new_game_pending == 2) { save_reset(); save_write(); request_level(1, 0.5f); new_game_pending = 0; }   /* 0x44ffa0 + RequestLevel(0.5, WWS) */
                if (cont && !new_game_pending) { title_page = -1; request_level(1, 0.5f); }  /* load game -> world select -> hub; here straight to Woody's hub */
                /* 0x44e690: outside the cinematic Woody is invisible and plays action 0x49 = animation 73 at speed 3 (10 s loop), whose camera track is the orbit */
                L.player.inst->visible = g_cin.state >= 2;
                title_t += dt;
            }
            for (int k = 0; k < 3; k++) g_act_prev[k] = g_act_now[k];
            g_act_now[0] = pin.left; g_act_now[1] = pin.right; g_act_now[2] = pin.action;
            g_save.chr[g_char].lives = L.player.lives; g_save.chr[g_char].health = L.player.health;          /* the Perso writes straight into the save struct */
            g_save.chr[g_char].unique = L.player.unique_items; g_save.chr[g_char].charges = L.player.special_charges;   /* 0x44c800 / 0x44c840 */
            if (g_cam.mode == 4 && !fly) memset(&pin, 0, sizeof pin);              /* cinematic camera: the player is frozen (0x459090) */
            cin_update(&L.vm, dt, g_now);
            rockets_update(dt, &L.player, L.have_player && !fly);
            if (!cin_running()) player_update(&L.player, &pin, dt, &L.vm, fly ? cam.yaw : L.player.cam_yaw);
            if (L.player.fade_req) { fade_start(0.5f, L.player.fade_req == 1); L.player.fade_req = 0; }           /* door actions 17 / 18 */
            if (L.player.cam_end_req) { L.player.cam_end_req = 0;                                                    /* end of door action 18: 0x44e5a0 = 0x41f9d0(0.5), 0x41f9f0(1), SetMode(0, 0) */
                /* but only when the side view is off: 0x44dcf1 skips it while Perso+0x4ec is set, and the script turns
                 * that on again (message 1088) in the same frame as the end of the action for a door into a side section */
                if (!g_cam.plane_on) { g_cam.dur = 0.5f; g_cam.dur_from_speed = 0; g_cam.cut = 0; cam_set_mode(1); } }
            if (g_res.on) results_update(&L.vm, dt, menu_ok, menu_up, menu_dn);      /* 0x454090: after the action tick, so a finished action starts the next one in the same frame */
            if (g_cam.mode != 0x20 && (L.player.dead_cam_req || (L.player.dead_kind == 7 && !g_cam.death_cam))) {     /* 0x41fb50: kind 1 is watched from where he hung (+100), kind 7 from where the camera is */
                g_cam.fix_pos = L.player.dead_kind == 7 ? g_cam.pos : (Vec3){ L.player.pos.x, L.player.pos.y + 100.0f, L.player.pos.z };
                g_cam.fix_target = L.player.inst; g_cam.fix_f = g_cam.look_off.y; cam_set_mode(2); g_cam.death_cam = 1;
            }
            L.player.dead_cam_req = 0;
            if (!cin_running()) enemies_update(&g_enemies, &L.player, cam.pos, dt);
            if (g_cam.plane_on) {                                                   /* 0x459eb0: the player stays on the vertical plane through the marker */
                Vec3 n = { -g_cam.plane_d.z, 0, g_cam.plane_d.x }; float off = (L.player.pos.x - g_cam.plane_a.x) * n.x + (L.player.pos.z - g_cam.plane_a.z) * n.z;
                L.player.pos.x -= n.x * off; L.player.pos.z -= n.z * off;
            }
            if (!fly) cam_update(&L.player, &cam, dt, g_cam.mode == 0x20 ? (pin.forward ? 2 : pin.back ? 3 : 0) : win.keys['C']); else cam.letterbox = 0;
            if (g_level == 0 && !fly && g_cin.state < 2) {                           /* title orbit: camera mode 0x80 on the Perso's animation 73 (docs/TITLE.md 2): no letterbox, vfov 83.97, no smoothing */
                Vec3 eye, tgt; float ph = fmodf(title_t / 10.0f, 1.0f);
                if (ins_camera_eval(L.player.inst, 73, ph, &eye, &tgt)) {
                    Vec3 to = { tgt.x - eye.x, tgt.y - eye.y, tgt.z - eye.z };
                    cam.pos = eye; cam.yaw = atan2f(to.x, to.z); cam.pitch = atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z)); cam.letterbox = 0; cam.fov_deg = 83.97f;
                }
            }
            if (jump_at >= 0) { if (now - t0 < jump_at) start_y = L.player.pos.y; else if (L.player.pos.y > max_y) { max_y = L.player.pos.y; printf("jump apex so far %.1f above start at t=%.2f (jumper state %d)\n", max_y - start_y, now - t0 - jump_at, L.player.jumper.state); } }
        }
        double pt1 = win_time();
        /* VM tick: time in 1/100 s like the original */
        if (!paused) {
            eko_tick(&L.vm, (int32_t)((g_clock - dt) * 100.0));   /* 0x401a0c writes the VM clock AFTER the tick, so a tick always runs on the value of the previous frame */
            { int n = g_nretry; g_nretry = 0; for (int i = 0; i < n; i++) { Instance *ri = slot_instance(g_retry[i].args[0]); if (ri && inst_msg(ri, g_retry[i].id, g_retry[i].args, g_retry[i].nargs, g_now) && g_nretry < 32) g_retry[g_nretry++] = g_retry[i]; } }
            for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) {
                Instance *ii = &g_ins.models[mi].instances[k];
                if (ii->scripted) inst_tick(ii, g_now, dt); else ii->anim_time += dt * ii->anim_speed;
                anim_sounds(ii);
            }
        }
        if (!paused) env_update(dt);
        if (getenv("WOODY_FLYLOG") && (int)g_now != (int)(g_now - dt)) {
            printf("  FLY t %.0f env %d flies %d:", g_now, g_nenv, g_nflies);
            for (int i = 0; i < g_nflies; i++) printf("  %d[%.0f %.0f %.0f s%d]", i, g_flies[i].pos.x, g_flies[i].pos.y, g_flies[i].pos.z, g_flies[i].state);
            puts("");
        }
        if (!paused) launchers_update((float)g_now, dt, &L.player, &L.gel, L.have_player && !fly && !L.player.dead_kind && !cin_running());
        double pt2 = win_time();
        { Vec3 cr = cam_right(&cam); audio_listener(&cam.pos.x, &cr.x); audio_pause(paused); }   /* the listener is the camera (mgr+0x28) */
        rnd_frame(&L.rnd, &win, &cam, g_now);                  /* the same game clock as the instances: a texture override (message 16) starts on it */
        {   /* 2D layer (docs/HUD_TEXT.md 5.4): HUD, then the text box, then the fades. No HUD in menus, BlackBox, cinematics and the fall death camera (0x401e19) */
            {   /* pickups: no mesh, a pulsing sprite (50..110, period 1 s) 50 above the instance; type 34 sits on its animated volume node */
                Vec3 cr = cam_right(&cam), cf = cam_forward(&cam), cu = { cf.y * cr.z - cf.z * cr.y, cf.z * cr.x - cf.x * cr.z, cf.x * cr.y - cf.y * cr.x };
                if (cu.y < 0) { cu.x = -cu.x; cu.y = -cu.y; cu.z = -cu.z; }
                float w = sinf(3.14159265f * (float)fmod(now - t0, 2.0)), size = w * w * 60.0f + 50.0f;
                hud_world_sprites_begin(&cr.x, &cu.x);
                for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) {
                    Instance *ii = &g_ins.models[mi].instances[k]; if (!ii->visible || ii->fade > 0.98f) continue;
                    int n = ii->type == 30 ? 0 : ii->type == 35 ? 1 : ii->type == 34 ? 2 : ii->type == 36 ? 3 : ii->type == 37 || ii->type == 38 ? 4 : -1; if (n < 0) continue;
                    float p[3] = { ii->position.x, ii->position.y, ii->position.z };
                    if (ii->type == 34 && ii->node_world) { p[0] = ii->node_world[0].m[12]; p[1] = ii->node_world[0].m[13]; p[2] = ii->node_world[0].m[14]; }
                    hud_world_sprite(n, p, size);
                }
                if (L.have_player && !fly && !cin_running() && g_level >= 1 && g_level <= 24) {
                    /* landing ring (docs/PERSO_JUMP.md 5): while Woody hangs in the air the floor under him carries a
                     * bright ring. WOODY_RING overrides its radius, WOODY_RING=0 switches it off. */
                    static const float white[3] = { 1, 1, 1 };
                    const char *rv = getenv("WOODY_RING"); float rr = rv ? (float)atof(rv) : 69.0f;   /* default = the Perso collision radius P+0x04 */
                    Vec3 rp, rn;
                    if (rr > 0 && player_landing_ring(&L.player, &rp, &rn)) {
                        float rc[3] = { rp.x + rn.x * 3.0f, rp.y + rn.y * 3.0f, rp.z + rn.z * 3.0f };   /* 3 units clear of the floor, or it z-fights with it */
                        hud_world_ring(rc, &rn.x, rr, rr * 0.12f, white, 0.7f);
                    }
                }
                env_draw();
                for (int li = 0; li < g_nlasers; li++) {                            /* Lazer_Draw 0x46e530: core (1,.7,.7) width 6 + glow (1,.4,.4) width 30 pulsing 0.5..1, ends fade over 70 */
                    Laser *z = &g_lasers[li]; if (!z->on || !z->inst->visible) continue;
                    if (!paused) z->phase += dt * 127.75f;
                    float g = 0.5f - 0.5f * cosf(2 * 3.14159265f * (float)(((int)z->phase % 254 + 0x80) & 0x1ff) / 512.0f);
                    for (uint32_t mk = 0; mk < 8; mk++) {
                        Vec3 a, b; int kind; if (!laser_segment(z, mk, &L.gel, &a, &b, &kind)) break;
                        if (L.have_player && !paused && !fly && !L.player.dead_kind && !cin_running() && laser_hits_player(a, b, &L.player)) player_kill(&L.player, 2);
                        Vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z }; float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z); if (l < 1) continue;
                        static const float core[3] = { 1, 0.7f, 0.7f }, glow[3] = { 1, 0.4f, 0.4f };
                        float f = kind == 2 ? 0 : (l > 140 ? 70.0f : l * 0.5f) / l;
                        Vec3 a1 = { a.x + d.x * f, a.y + d.y * f, a.z + d.z * f }, b1 = { b.x - d.x * f, b.y - d.y * f, b.z - d.z * f };
                        for (int layer = 0; layer < 2; layer++) {
                            const float *c = layer ? glow : core; float hw = layer ? 30.0f : 6.0f, al = layer ? g : 1.0f;
                            if (f > 0) { hud_world_beam(&a.x, &a1.x, &cam.pos.x, hw, c, 0, al); hud_world_beam(&b1.x, &b.x, &cam.pos.x, hw, c, al, 0); }
                            hud_world_beam(&a1.x, &b1.x, &cam.pos.x, hw, c, al, al);
                        }
                    }
                }
                launchers_draw(&cam.pos.x, paused ? 0 : dt); stars_draw(paused ? 0 : dt); rockets_draw(paused ? 0 : dt); fx_smoke_draw(paused ? 0 : dt); steps_draw(paused ? 0 : dt); peck_draw(paused ? 0 : dt); fx_update(paused ? 0 : dt);
                hud_world_sprites_end();
            }
            if (g_black_frame || (g_sfade.hold && !(g_sfade.rest > 0))) { rnd_fade(0); g_black_frame = 0; }                /* 1152 blanks the 3D picture only: the House intro shows its text on black */
            if (L.have_player && now - t0 >= pick_at - 0.5) {            /* set the counters a few frames early, so the HUD sees them change like it would in play */
                if (pre_bonus >= 0) { L.player.bonus_count = pre_bonus; pre_bonus = -1; }
                if (pre_health >= 0) { L.player.health = pre_health; pre_health = -1; }
            }
            if (pick_type && now - t0 >= pick_at && L.have_player) {     /* --pickup: the same path a script pickup takes, 400 units in front of the camera */
                Vec3 f = cam_forward(&cam), fp = { cam.pos.x + f.x * 400, cam.pos.y + f.y * 400, cam.pos.z + f.z * 400 };
                int t = pick_type, fx = t == 30 ? 0 : t == 35 ? 1 : t == 34 ? 2 : t == 36 ? 3 : (t == 37 || t == 38) ? 4 : -1;
                int kind = t == 30 ? 1 : t == 36 ? 2 : t == 35 ? 3 : t == 34 ? 4 : t == 37 ? 5 : 0;
                player_collect(&L.player, t, 300);
                if (fx >= 0) game_pickup_fx(fx, fp);
                if (kind && g_npick < 8) { g_pick[g_npick].kind = kind; g_pick[g_npick].pos = fp; g_npick++; }
                pick_type = 0;
            }
            if (door_at >= 0 && now - t0 >= door_at && L.have_player) {  /* --door: the message a door script sends, on the real handler */
                EkoMsg dm; memset(&dm, 0, sizeof dm);
                dm.id = 1040; dm.nargs = 2; dm.args[0] = 0x1000000u | (uint32_t)door_inst; dm.args[1] = (uint32_t)door_act;
                on_msg(&L.vm, &dm, NULL); door_at = -1;
                printf("  --door: 1040 [inst %d, action %d]", door_inst, door_act), puts("");
            }
            for (int i = 0; i < g_npick; i++) {                          /* 0x448510: the flight starts from where the bonus was on screen */
                float sc[2]; int on = rnd_project(&win, &cam, g_pick[i].pos, &sc[0], &sc[1]);
                hud_anim_pickup(g_pick[i].kind, on ? sc : NULL, g_char);
            }
            g_npick = 0;
            hud_begin(win.width, win.height);
            if (L.have_player && !fly && g_level >= 1 && g_level <= 24 && !cin_running() && !g_res.on && (!g_cam.death_cam || g_hud_ext) && !getenv("WOODY_NOHUD")) {
                const Player *pl = &L.player; int race = pl->inst->type == 18 || pl->inst->type == 19;
                HudState hs = { g_char, race, pl->lives, race ? pl->race_bonus : pl->bonus_count, race ? pl->race_bonus : pl->bonus_got, pl->bonus_total,
                                g_level != 1 && g_level != 11 && g_level != 18, pl->unique_items, pl->special_charges, paused || g_hud_ext, pl->health, pl->charge * (2.0f / 3.0f) };
                hud_draw(&hs, dt);
            }
            if (g_level == 0 && !fly) hud_title_draw(g_next_level >= 0 ? -1 : title_page, title_sel, title_page >= 0 && g_next_level < 0, dt);
            if (g_res.on) {                                                          /* menu page 0x1e: the panel is up in state 1 (0x454560) and hidden while he cheers (0x454580) */
                int page_up = g_res.page == 6 || g_res.page == 8 || g_res.page == 9;
                if (g_res.state == 1 || g_res.state == 4) {
                    HudResults hr = { results_level_no(g_stats.level), g_res.race, g_res.high, g_res.cats,
                                      g_stats.stats[0], g_stats.stats[2], g_stats.stats[1], g_stats.stats[3], g_stats.time, g_res.score, g_res.best };
                    hud_results_draw(&hr, g_res.state == 1, page_up ? 0.0f : dt);    /* one blink phase per frame: the page below ticks it when it is up */
                }
                if (g_res.page == 6) { static const uint32_t q[3] = { 35, 5, 6 }; hud_menu_page(q, 3, 0.4f, g_res.sel, dt); }            /* "Do you want to save?" / Yes / No */
                else if (g_res.page == 8) { static const uint32_t q[2] = { 67, 4 }; hud_menu_page(q, 2, 0.4f, g_res.sel, dt); }          /* "Game Saved" / Continue */
                else if (g_res.page == 9) { static const uint32_t q[2] = { 59, 4 }; hud_menu_page(q, 2, 0.4f, g_res.sel, dt); }          /* "Save failed." / Continue */
            }
            { uint32_t v = g_text_var & 0xffffff; hud_text_draw(v < L.vm.nvars && L.vm.varval[v] != 0, paused ? 0 : dt); }
            hud_end(); g_hud_ext = 0;
        }
        /* level change: PgUp / PgDn cycle through the levels (debug); a request fades out, swaps the level, fades in */
        for (int k = 0; k < 2; k++) { int down = win.keys[k ? VK_NEXT : VK_PRIOR]; if (down && !pg_prev[k]) { int cur = g_level >= 0 && g_level < 27 ? g_level : 0; request_level((cur + (k ? 1 : 26)) % 27, 0.5f); } pg_prev[k] = down; }
        { static int side_done; if (getenv("WOODY_SIDE") && now - t0 >= 1.0 && !side_done && L.have_player) { side_done = 1; Instance *si = slot_instance(0x1000000 | (uint32_t)strtol(getenv("WOODY_SIDE"), NULL, 0)); if (si) cam_side_start(si, 2); } }   /* testing: force the side view on a marker instance */
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
        double pt3 = win_time();
        win_swap(&win);
        frames++;
        if (prof && getenv("WOODY_PROF")) { double pt4 = win_time(); pf[0] += pt1 - pt0; pf[1] += pt2 - pt1; pf[2] += pt3 - pt2; pf[3] += pt4 - pt3; if (++pfn == 60) { printf("PROF ms/frame: player+enemies+camera %.2f  vm+instances %.2f  render+2D %.2f  swap %.2f", pf[0] / 60 * 1000, pf[1] / 60 * 1000, pf[2] / 60 * 1000, pf[3] / 60 * 1000); puts(""); pf[0] = pf[1] = pf[2] = pf[3] = 0; pfn = 0; } }
        if (g_next_level >= 0 && g_switch_fade <= 0) {
            const char *name = k_levels[g_next_level]; g_prev_level = g_level; g_level = g_next_level; g_next_level = -1;
            level_free(&L);
            if (level_load(&L, dir, name)) { fprintf(stderr, "level %s failed to load\n", name); return 1; }
            if (!L.have_player) fly = 1; else if (!have_cam) fly = 0;
            title_page = 0; title_sel = 0; title_t = 0; new_game_pending = 0; hud_title_reset();
            t0 = L.t0; last = win_time(); sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
            lvl = L.name; continue;
        }
        if (now - fps_t > 2.0) { char title[256]; snprintf(title, sizeof title, "WoodyRE%s - %s - %.0f fps - VM t=%d frame %u msgs %u - %s - woody %.0f %.0f %.0f %s - vol events %u - hearts %.0f lives %d bonus %d/%d", g_level == 0 ? " - TITLE: Enter = new game, L = continue" : "", lvl, frames / (now - fps_t), L.vm.time, L.vm.frame, L.vm.stat_msgs_total, fly ? "fly" : "play", L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.on_ground ? "ground" : "air", L.player.events_sent, L.player.health, L.player.lives, L.player.bonus_got, L.player.bonus_total); SetWindowTextA((HWND)win.hwnd, title); if (L.have_player) printf("player t=%.1f pos %.0f %.0f %.0f vel %.0f %.0f %.0f %s floor %.0f cam %.0f %.0f %.0f\n", now - t0, L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.vel.x, L.player.vel.y, L.player.vel.z, L.player.on_ground ? (L.player.floor_is_hull ? "hull" : "ground") : "air", L.player.floor_y, cam.pos.x, cam.pos.y, cam.pos.z); frames = 0; fps_t = now; }
    }
    level_free(&L); audio_shutdown(); win_close(&win);
    return 0;
}
