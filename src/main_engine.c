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

static InsFile g_ins;
static int g_log_msgs = 1;
static Player *g_player;
static EnemySet g_enemies;
static float g_now;                                  /* game time in seconds (VM time base) */
/* messages 12/13 wait for the running animation to end: offered again every frame (max 32 in the original, 0x4012f0 clears) */
static EkoMsg g_retry[32]; static int g_nretry;
static Instance *slot_instance(uint32_t ref) { if ((ref >> 24) != 1) return NULL; uint32_t i = ref & 0xffffff; return i < g_ins.nslots + 16 ? g_ins.slots[i] : NULL; }

/* script -> engine messages. Only the subset needed to see something happen is implemented;
 * everything else is logged. See docs/MESSAGES.md. */
static void on_msg(EkoVM *vm, const EkoMsg *m, void *user)
{
    (void)vm; (void)user;
    Instance *in = m->nargs ? slot_instance(m->args[0]) : NULL;
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
    case 1020: if (g_player) player_kill(g_player, 1); break;                       /* 0x44516a: Perso->vt[38](1), sent by the pit / water volumes */
    default: break;
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
    if (L->have_player) player_free(&L->player);
    g_player = NULL; g_enemies.n = 0; g_nretry = 0;
    rnd_free(&L->rnd); eko_free(&L->vm); free(L->code); ins_free(&g_ins); if (L->have_lit) lit_free(&L->lit); gel_free(&L->gel); tex_free(&L->tex);
    memset(L, 0, sizeof *L);
}
static int level_load(Level *L, const char *dir, const char *lvl)
{
    char path[512]; memset(L, 0, sizeof *L); snprintf(L->name, sizeof L->name, "%s", lvl);
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
    printf("VM init...\n"); eko_init(&L->vm);
    printf("init done: %d messages\n", L->vm.nmsgs);
    L->t0 = win_time();
    return 0;
}
/* level change: requested by name, executed at the end of the frame after a fade to black */
static char g_next_level[32]; static float g_switch_fade = 1.0f;
static void request_level(const char *name) { if (!g_next_level[0]) snprintf(g_next_level, sizeof g_next_level, "%s", name); }
static const char *k_levels[] = { "House", "WWS", "W1A", "W1B", "W2A", "W2B", "W2D", "W3A", "W3B", "W3C", "W3D", "KWS", "K1A", "K1R", "K2A", "K2R", "K3A", "K3R",
                                  "SWS", "S1A", "S1R", "S2A", "S2R", "S3A", "S3R", "Blackbox", "Credits", "Lang" };

static void *read_all(const char *path, size_t *sz) { FILE *f = fopen(path, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); void *b = malloc((size_t)n); if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; } fclose(f); *sz = (size_t)n; return b; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "extract/Data", *lvl = argc > 2 ? argv[2] : "W1A";
    const char *shot_path = NULL; double shot_after = 0;                          /* --shot file.ppm seconds: screenshot then quit */
    int have_cam = 0; float cam_args[5] = {0, 0, 0, 0, 0};                          /* --cam x y z yaw pitch (degrees) */
    double jump_at = -1; float max_y = -1e30f, start_y = 0;                       /* --jump T: hold jump from T s for 1 s (testing), reports the apex */
    double jump_len = 1.0, jump2_at = -1, jump2_len = getenv("WOODY_J2LEN") ? atof(getenv("WOODY_J2LEN")) : 0.15;                                         /* --jump2 LEN T2: first press lasts LEN s, second press (0.15 s) at T2 */
    double peck_at = -1, peck_len = 0.1;                                          /* --peck T LEN: hold the attack key from T s for LEN s (testing) */
    int have_pos = 0; float pos_args[3] = {0, 0, 0};                               /* --pos x y z: start the player there (testing) */
    const char *next_name = NULL; double next_at = 0;                              /* --next LVL T: change to level LVL after T s (testing) */
    double walk_for = 0; int fly = 0;                                             /* --walk T: hold forward for T s (testing); --fly: start in free camera */
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_path = argv[i + 1]; shot_after = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--cam") && i + 5 < argc) { for (int k = 0; k < 5; k++) cam_args[k] = (float)atof(argv[i + 1 + k]); have_cam = 1; i += 5; fly = 1; }
        else if (!strcmp(argv[i], "--walk") && i + 1 < argc) { walk_for = atof(argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "--jump") && i + 1 < argc) { jump_at = atof(argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "--jump2") && i + 2 < argc) { jump_len = atof(argv[i + 1]); jump2_at = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--peck") && i + 2 < argc) { peck_at = atof(argv[i + 1]); peck_len = atof(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--pos") && i + 3 < argc) { for (int k = 0; k < 3; k++) pos_args[k] = (float)atof(argv[i + 1 + k]); have_pos = 1; i += 3; }
        else if (!strcmp(argv[i], "--fly")) fly = 1;
        else if (!strcmp(argv[i], "--next") && i + 2 < argc) { next_name = argv[i + 1]; next_at = atof(argv[i + 2]); i += 2; }
    }
    Window win; if (win_open(&win, "WoodyRE", 1280, 800)) return 1;
    static Level L; if (level_load(&L, dir, lvl)) return 1;

    /* camera: start behind Woody (model 0, instance 0) if present */
    FreeCamera cam = { {0, 0, 0}, 0, 0, 70 };
    Instance *sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
    if (sel) { cam.pos = sel->position; cam.pos.y += 120; cam.pos.z -= 350; }
    if (have_cam) { cam.pos.x = cam_args[0]; cam.pos.y = cam_args[1]; cam.pos.z = cam_args[2]; cam.yaw = cam_args[3] * 3.14159265f / 180; cam.pitch = cam_args[4] * 3.14159265f / 180; }
    else { cam.pos.x = (L.gel.bbox[0] + L.gel.bbox[1]) / 2; cam.pos.y = L.gel.bbox[3]; cam.pos.z = (L.gel.bbox[4] + L.gel.bbox[5]) / 2; cam.pitch = -1.2f; }

    if (!L.have_player) fly = 1;
    if (L.have_player && have_pos) { L.player.pos.x = pos_args[0]; L.player.pos.y = pos_args[1]; L.player.pos.z = pos_args[2]; L.player.floor_y = L.player.pos.y - 1000.0f; }
    double t0 = L.t0, last = t0; int pg_prev[2] = {0, 0}; int paused = 0, tab_prev = 0, br_prev[2] = {0, 0}, f_prev[3] = {0, 0, 0}, p_prev = 0, f5_prev = 0; uint32_t frames = 0; double fps_t = t0;
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
            player_update(&L.player, &pin, dt, &L.vm, fly ? cam.yaw : L.player.cam_yaw);
            enemies_update(&g_enemies, &L.player, cam.pos, dt);
            if (!fly) player_camera(&L.player, &cam, dt, win.keys['C']);
            if (jump_at >= 0) { if (now - t0 < jump_at) start_y = L.player.pos.y; else if (L.player.pos.y > max_y) { max_y = L.player.pos.y; printf("jump apex so far %.1f above start at t=%.2f (jumper state %d)\n", max_y - start_y, now - t0 - jump_at, L.player.jumper.state); } }
        }
        /* VM tick: time in 1/100 s like the original */
        if (!paused) {
            eko_tick(&L.vm, (int32_t)((now - t0) * 100.0));
            { int n = g_nretry; g_nretry = 0; for (int i = 0; i < n; i++) { Instance *ri = slot_instance(g_retry[i].args[0]); if (ri && inst_msg(ri, g_retry[i].id, g_retry[i].args, g_retry[i].nargs, g_now) && g_nretry < 32) g_retry[g_nretry++] = g_retry[i]; } }
            for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) {
                Instance *ii = &g_ins.models[mi].instances[k];
                if (ii->scripted) inst_tick(ii, g_now, dt); else ii->anim_time += dt * ii->anim_speed;
            }
        }
        rnd_frame(&L.rnd, &win, &cam, (float)(now - t0));
        /* level change: PgUp / PgDn cycle through the levels (debug); a request fades out, swaps the level, fades in */
        for (int k = 0; k < 2; k++) { int down = win.keys[k ? VK_NEXT : VK_PRIOR]; if (down && !pg_prev[k]) { int n = (int)(sizeof k_levels / sizeof *k_levels), cur = 0; for (int i = 0; i < n; i++) if (!_stricmp(k_levels[i], L.name)) cur = i; request_level(k_levels[(cur + (k ? 1 : n - 1)) % n]); } pg_prev[k] = down; }
        if (next_name && now - t0 >= next_at) { request_level(next_name); next_name = NULL; }
        if (g_next_level[0]) { g_switch_fade -= dt / 0.5f; if (g_switch_fade < 0) g_switch_fade = 0; } else if (g_switch_fade < 1) { g_switch_fade += dt / 0.5f; if (g_switch_fade > 1) g_switch_fade = 1; }
        { float f = (L.have_player && !fly) ? L.player.fade : 1.0f; if (g_switch_fade < f) f = g_switch_fade; if (f < 1.0f) rnd_fade(f); }
        if (shot_path && now - t0 >= shot_after) { rnd_screenshot(&win, shot_path); printf("screenshot -> %s\n", shot_path); win.quit = 1; }
        win_swap(&win);
        frames++;
        if (g_next_level[0] && g_switch_fade <= 0) {
            char name[32]; snprintf(name, sizeof name, "%s", g_next_level); g_next_level[0] = 0;
            int lives = L.have_player ? L.player.lives : -1; float health = L.have_player ? L.player.health : 0;
            level_free(&L);
            if (level_load(&L, dir, name)) { fprintf(stderr, "level %s failed to load\n", name); return 1; }
            if (L.have_player && lives >= 0) { L.player.lives = lives; L.player.health = health; }
            if (!L.have_player) fly = 1; else if (!have_cam) fly = 0;
            t0 = L.t0; last = win_time(); sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
            lvl = L.name; continue;
        }
        if (now - fps_t > 2.0) { char title[256]; snprintf(title, sizeof title, "WoodyRE - %s - %.0f fps - VM t=%d frame %u msgs %u - %s - woody %.0f %.0f %.0f %s - vol events %u - hearts %.0f lives %d bonus %d/%d", lvl, frames / (now - fps_t), L.vm.time, L.vm.frame, L.vm.stat_msgs_total, fly ? "fly" : "play", L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.on_ground ? "ground" : "air", L.player.events_sent, L.player.health, L.player.lives, L.player.bonus_got, L.player.bonus_total); SetWindowTextA((HWND)win.hwnd, title); if (L.have_player) printf("player t=%.1f pos %.0f %.0f %.0f vel %.0f %.0f %.0f %s floor %.0f cam %.0f %.0f %.0f\n", now - t0, L.player.pos.x, L.player.pos.y, L.player.pos.z, L.player.vel.x, L.player.vel.y, L.player.vel.z, L.player.on_ground ? (L.player.floor_is_hull ? "hull" : "ground") : "air", L.player.floor_y, cam.pos.x, cam.pos.y, cam.pos.z); frames = 0; fps_t = now; }
    }
    level_free(&L); win_close(&win);
    return 0;
}
