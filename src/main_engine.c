/* main_engine.c - WoodyRE native engine prototype: loads a level (.gel/.tex/.ins + code), runs the
 * EKO CODE script VM every frame and renders world + instances with OpenGL.
 *
 * usage: woody.exe <Data dir> <LVL>            e.g. woody.exe extract/Data W1A
 * keys: WASD + right mouse = fly, Shift = fast, F1 world, F2 instances, F3 wireframe,
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

static InsFile g_ins;
static int g_log_msgs = 1;
static Instance *slot_instance(uint32_t ref) { if ((ref >> 24) != 1) return NULL; uint32_t i = ref & 0xffffff; return i < g_ins.nslots + 16 ? g_ins.slots[i] : NULL; }

/* script -> engine messages. Only the subset needed to see something happen is implemented;
 * everything else is logged. See docs/MESSAGES.md. */
static void on_msg(EkoVM *vm, const EkoMsg *m, void *user)
{
    (void)vm; (void)user;
    Instance *in = m->nargs ? slot_instance(m->args[0]) : NULL;
    switch (m->id) {
    case 1200: if (in && m->nargs > 1) in->type = (int)m->args[1]; break;          /* SetTypeInstance */
    case 1: case 4:                                                                 /* PlayAnim(inst, anim, ...) */
        if (in && m->nargs > 1 && m->args[1] < in->model->nanims) { in->anim = (int)m->args[1]; in->anim_time = 0; }
        break;
    case 2: case 3: case 12: case 13:
        if (in && m->nargs > 1 && m->args[1] < in->model->nanims) { in->anim = (int)m->args[1]; }
        break;
    case 6: if (in && m->nargs > 1) in->visible = m->args[1] != 0; break;         /* show/hide */
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

static void *read_all(const char *path, size_t *sz) { FILE *f = fopen(path, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); void *b = malloc((size_t)n); if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; } fclose(f); *sz = (size_t)n; return b; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "extract/Data", *lvl = argc > 2 ? argv[2] : "W1A";
    const char *shot_path = NULL; double shot_after = 0;                          /* --shot file.ppm seconds: screenshot then quit */
    for (int i = 3; i + 2 < argc + 0 && argv[i]; i++) if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_path = argv[i + 1]; shot_after = atof(argv[i + 2]); }
    char path[512]; TexFile tex; GelFile gel;
    snprintf(path, sizeof path, "%s/%s/%s.tex", dir, lvl, lvl); if (tex_load(&tex, path)) return 1;
    snprintf(path, sizeof path, "%s/%s/%s.gel", dir, lvl, lvl); if (gel_load(&gel, path)) return 1;
    snprintf(path, sizeof path, "%s/%s/%s.ins", dir, lvl, lvl); if (ins_load(&g_ins, path)) return 1;
    snprintf(path, sizeof path, "%s/%s/code", dir, lvl);
    size_t codesz; void *code = read_all(path, &codesz); if (!code) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    EkoVM vm; if (eko_load(&vm, code, codesz)) { fprintf(stderr, "bad code file\n"); return 1; }
    vm.on_msg = on_msg; vm.on_warn = on_warn; vm.rand_fn = msvc_rand;
    printf("%s: %u polys, %u verts, %u textures, %u models, %u slots, %u script objects\n", lvl, gel.npolys, gel.nverts, tex.ngroups, g_ins.nmodels, g_ins.nslots, vm.nobj);

    Window win; if (win_open(&win, "WoodyRE", 1280, 800)) return 1;
    Renderer rnd; rnd_init(&rnd, &tex, &gel, &g_ins);

    /* camera: start behind Woody (model 0, instance 0) if present */
    FreeCamera cam = { {0, 0, 0}, 0, 0, 70 };
    Instance *sel = (g_ins.nmodels && g_ins.models[0].ninstances) ? &g_ins.models[0].instances[0] : NULL;
    if (sel) { cam.pos = sel->position; cam.pos.y += 120; cam.pos.z -= 350; }
    else { cam.pos.x = (gel.bbox[0] + gel.bbox[1]) / 2; cam.pos.y = gel.bbox[3]; cam.pos.z = (gel.bbox[4] + gel.bbox[5]) / 2; cam.pitch = -1.2f; }

    printf("VM init...\n"); eko_init(&vm);
    printf("init done: %d messages\n", vm.nmsgs);
    double t0 = win_time(), last = t0; int paused = 0, tab_prev = 0, br_prev[2] = {0, 0}, f_prev[3] = {0, 0, 0}, p_prev = 0; uint32_t frames = 0; double fps_t = t0;
    while (!win.quit) {
        win_poll(&win);
        double now = win_time(); float dt = (float)(now - last); last = now;
        if (dt > 0.1f) dt = 0.1f;
        /* camera */
        float speed = (win.keys[VK_SHIFT] ? 3000.0f : 600.0f) * dt;
        Vec3 fw = cam_forward(&cam), rt = cam_right(&cam);
        if (win.keys['W']) { cam.pos.x += fw.x * speed; cam.pos.y += fw.y * speed; cam.pos.z += fw.z * speed; }
        if (win.keys['S']) { cam.pos.x -= fw.x * speed; cam.pos.y -= fw.y * speed; cam.pos.z -= fw.z * speed; }
        if (win.keys['D']) { cam.pos.x += rt.x * speed; cam.pos.z += rt.z * speed; }
        if (win.keys['A']) { cam.pos.x -= rt.x * speed; cam.pos.z -= rt.z * speed; }
        if (win.keys['E'] || win.keys[VK_SPACE]) cam.pos.y += speed;
        if (win.keys['Q']) cam.pos.y -= speed;
        cam.yaw += win.mouse_dx * 0.004f; cam.pitch -= win.mouse_dy * 0.004f;
        if (cam.pitch > 1.5f) cam.pitch = 1.5f; if (cam.pitch < -1.5f) cam.pitch = -1.5f;
        /* toggles */
        for (int k = 0; k < 3; k++) { int down = win.keys[VK_F1 + k]; if (down && !f_prev[k]) { if (k == 0) rnd.show_world ^= 1; else if (k == 1) rnd.show_instances ^= 1; else rnd.wireframe ^= 1; } f_prev[k] = down; }
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
        /* VM tick: time in 1/100 s like the original */
        if (!paused) {
            eko_tick(&vm, (int32_t)((now - t0) * 100.0));
            for (uint32_t mi = 0; mi < g_ins.nmodels; mi++) for (uint32_t k = 0; k < g_ins.models[mi].ninstances; k++) g_ins.models[mi].instances[k].anim_time += dt;
        }
        rnd_frame(&rnd, &win, &cam, (float)(now - t0));
        if (shot_path && now - t0 >= shot_after) { rnd_screenshot(&win, shot_path); printf("screenshot -> %s\n", shot_path); win.quit = 1; }
        win_swap(&win);
        frames++;
        if (now - fps_t > 2.0) { char title[256]; snprintf(title, sizeof title, "WoodyRE - %s - %.0f fps - VM t=%d frame %u msgs %u - cam %.0f %.0f %.0f", lvl, frames / (now - fps_t), vm.time, vm.frame, vm.stat_msgs_total, cam.pos.x, cam.pos.y, cam.pos.z); SetWindowTextA((HWND)win.hwnd, title); frames = 0; fps_t = now; }
    }
    rnd_free(&rnd); win_close(&win);
    eko_free(&vm); free(code); ins_free(&g_ins); gel_free(&gel); tex_free(&tex);
    return 0;
}
