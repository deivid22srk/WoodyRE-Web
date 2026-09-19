/* render_gl.c - Win32 window + OpenGL 1.1 fixed-function renderer.
 * The game data is left-handed (D3D). We keep world coordinates untouched and mirror z in the
 * projection, so a camera with yaw 0 looks along +z like the original. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_gl.h"

static Window *g_win;

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Window *w = g_win;
    switch (msg) {
    case WM_CLOSE: case WM_DESTROY: if (w) w->quit = 1; return 0;
    case WM_SIZE: if (w) { w->width = LOWORD(lp); w->height = HIWORD(lp); } return 0;
    case WM_KEYDOWN: if (w && wp < 256) w->keys[wp] = 1; if (wp == VK_ESCAPE && w) w->quit = 1; return 0;
    case WM_KEYUP: if (w && wp < 256) w->keys[wp] = 0; return 0;
    case WM_RBUTTONDOWN: if (w) { w->mouse_right = 1; SetCapture(h); } return 0;
    case WM_RBUTTONUP: if (w) { w->mouse_right = 0; ReleaseCapture(); } return 0;
    case WM_MOUSEMOVE: {
        static int lx = -1, ly = -1; int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (w && w->mouse_right && lx >= 0) { w->mouse_dx += x - lx; w->mouse_dy += y - ly; }
        lx = x; ly = y; return 0; }
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int win_open(Window *w, const char *title, int width, int height)
{
    memset(w, 0, sizeof *w); g_win = w;
    WNDCLASSA wc = {0}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "WoodyRE"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    RECT rc = {0, 0, width, height}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("WoodyRE", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return -1;
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0}; pfd.nSize = sizeof pfd; pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(hdc, &pfd); SetPixelFormat(hdc, pf, &pfd);
    HGLRC rc2 = wglCreateContext(hdc); wglMakeCurrent(hdc, rc2);
    w->hwnd = hwnd; w->hdc = hdc; w->hglrc = rc2; w->width = width; w->height = height;
    printf("OpenGL: %s / %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    return 0;
}
void win_poll(Window *w) { MSG m; w->mouse_dx = w->mouse_dy = 0; while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); } }
void win_swap(Window *w) { SwapBuffers((HDC)w->hdc); }
void win_close(Window *w) { wglMakeCurrent(NULL, NULL); wglDeleteContext((HGLRC)w->hglrc); ReleaseDC((HWND)w->hwnd, (HDC)w->hdc); DestroyWindow((HWND)w->hwnd); }
double win_time(void) { static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }

static uint8_t col2x(uint32_t v) { v = (v & 0xff) * 2; return v > 255 ? 255 : (uint8_t)v; }
Vec3 cam_forward(const FreeCamera *c) { Vec3 v = { sinf(c->yaw) * cosf(c->pitch), sinf(c->pitch), cosf(c->yaw) * cosf(c->pitch) }; return v; }
Vec3 cam_right(const FreeCamera *c) { Vec3 v = { -cosf(c->yaw), 0, sinf(c->yaw) }; return v; }   /* right-handed world: looking along +z, +x is on the left */

/* ---------------------------------------------------------------- textures */
static GLuint upload_texture(const TexGroup *g, int frame)
{
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    uint32_t n = g->width * g->height; uint8_t *rgba = (uint8_t *)malloc((size_t)n * 4);
    rgb565_to_rgba(g->frames[frame], rgba, n, g->flags & 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)g->width, (GLsizei)g->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    free(rgba); return id;
}

/* ---------------------------------------------------------------- world batches */
static void material_uv(const Material *m, float x, float y, float z, float *u, float *v)
{
    *u = m->m[0] * x + m->m[3] * y + m->m[6] * z + m->m[9];
    *v = m->m[1] * x + m->m[4] * y + m->m[7] * z + m->m[10];
}

int rnd_init(Renderer *r, TexFile *tex, GelFile *gel, InsFile *ins)
{
    memset(r, 0, sizeof *r); r->tex = tex; r->gel = gel; r->ins = ins; r->show_world = r->show_instances = 1;
    for (uint32_t g = 0; g < tex->ngroups; g++) tex->groups[g].gl_tex = upload_texture(&tex->groups[g], 0);
    /* count triangles per texture group */
    uint32_t *count = (uint32_t *)calloc(tex->ngroups + 1, 4);
    for (uint32_t i = 0; i < gel->npolys; i++) {
        GelPoly *p = &gel->polys[i]; if (p->nverts < 3) continue;
        uint32_t grp = (p->material & 0x8000) ? tex->ngroups : tex->materials[p->material & 0x7fff].group;
        count[grp] += p->nverts - 2;
    }
    r->batches = (struct WorldBatch *)calloc(tex->ngroups + 1, sizeof *r->batches);
    for (uint32_t g = 0; g <= tex->ngroups; g++) {
        struct WorldBatch *b = &r->batches[g]; b->group = g;
        b->pos = (float *)malloc((size_t)count[g] * 9 * sizeof(float)); b->uv = (float *)malloc((size_t)count[g] * 6 * sizeof(float)); b->col = (uint8_t *)malloc((size_t)count[g] * 9);
    }
    for (uint32_t i = 0; i < gel->npolys; i++) {
        GelPoly *p = &gel->polys[i]; if (p->nverts < 3) continue;
        int nomat = (p->material & 0x8000) != 0;
        const Material *m = nomat ? NULL : &tex->materials[p->material & 0x7fff];
        struct WorldBatch *b = &r->batches[nomat ? tex->ngroups : m->group];
        for (uint32_t k = 1; k + 1 < p->nverts; k++) {
            uint32_t idx[3] = { p->indices[0], p->indices[k], p->indices[k + 1] };
            for (int c = 0; c < 3; c++) {
                GelVert *v = &gel->verts[idx[c]]; size_t o = (size_t)b->ntris * 3 + c;
                b->pos[o * 3] = v->x; b->pos[o * 3 + 1] = v->y; b->pos[o * 3 + 2] = v->z;
                float u = 0, vv = 0; if (m) material_uv(m, v->x, v->y, v->z, &u, &vv);
                b->uv[o * 2] = u; b->uv[o * 2 + 1] = vv;
                b->col[o * 3] = col2x(v->colour); b->col[o * 3 + 1] = col2x(v->colour >> 8); b->col[o * 3 + 2] = col2x(v->colour >> 16);   /* bytes R,G,B; 128 = neutral (modulate 2x) */
            }
            b->ntris++;
        }
    }
    r->nbatches = tex->ngroups + 1; free(count);
    for (uint32_t g = 0; g < tex->ngroups; g++) if (tex->groups[g].flags & 2) {          /* additive groups: intensity = flags byte 2 */
        uint32_t a = (tex->groups[g].flags >> 16) & 0xff; struct WorldBatch *b = &r->batches[g];
        for (size_t i = 0; i < (size_t)b->ntris * 9; i++) b->col[i] = (uint8_t)(b->col[i] * a / 255);
    }
    return 0;
}

int rnd_screenshot(const Window *w, const char *path)
{
    int W = w->width, H = w->height; uint8_t *px = (uint8_t *)malloc((size_t)W * H * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb"); if (!f) { free(px); return -1; }
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = H - 1; y >= 0; y--) fwrite(px + (size_t)y * W * 3, 1, (size_t)W * 3, f);
    fclose(f); free(px); return 0;
}

void rnd_free(Renderer *r) { for (uint32_t i = 0; i < r->nbatches; i++) { free(r->batches[i].pos); free(r->batches[i].uv); free(r->batches[i].col); } free(r->batches); }

/* ---------------------------------------------------------------- blend modes
 * .tex group flags (tex+0x44 in the exe): bit 0 colour key, bit 1 = blended (rendered additively here: the glare
 * plates are white-on-black), byte 2 = intensity/alpha, byte 3 = ground type (0x46295f). */
static int group_blended(const Renderer *r, uint32_t group) { return group < r->tex->ngroups && (r->tex->groups[group].flags & 2); }
static int mat_blended(const Renderer *r, uint32_t material) { return !(material & 0x8000) && material < r->tex->nmaterials && group_blended(r, r->tex->materials[material].group); }
static void set_blend(int blended)
{
    if (blended) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE); }
    else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
}
static float g_mat_scale = 1.0f;                     /* colour scale of the current material (blend intensity) */

/* ---------------------------------------------------------------- instances */
static void set_material(const Renderer *r, uint32_t material, const Material **mat_out)
{
    *mat_out = NULL; g_mat_scale = 1.0f;
    if (material & 0x8000) {
        float rgb[3]; argb1555_to_rgb(material, rgb); glDisable(GL_TEXTURE_2D); set_blend(0); glColor3f(rgb[0], rgb[1], rgb[2]);
    } else if (material < r->tex->nmaterials) {
        const Material *m = &r->tex->materials[material]; *mat_out = m; const TexGroup *g = &r->tex->groups[m->group];
        int bl = (g->flags & 2) != 0; set_blend(bl); if (bl) g_mat_scale = ((g->flags >> 16) & 0xff) / 255.0f;
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g->gl_tex); glColor3f(g_mat_scale, g_mat_scale, g_mat_scale);
    } else { glDisable(GL_TEXTURE_2D); set_blend(0); glColor3f(1, 0, 1); }
}

static void draw_instance(const Renderer *r, Instance *inst, int pass)   /* pass 0 = opaque, 1 = blended */
{
    Model *m = inst->model;
    const Material *mat;
    /* rigid node polygons */
    for (uint32_t ni = 0; ni < m->nnodes; ni++) {
        InsNode *n = &m->nodes[ni]; if (n->kind != 0 || !n->polys) continue;
        for (uint32_t k = 0; k < n->npolys; k++) {
            InsPoly *p = &n->polys[k]; if (p->nverts < 3 || mat_blended(r, p->material) != pass) continue;
            set_material(r, p->material, &mat);
            glBegin(GL_TRIANGLE_FAN);
            for (uint32_t c = 0; c < p->nverts; c++) {
                InsPoint *pt = &m->points[p->indices[c]];
                Vec3 lp = { pt->pos.x - n->pivot.x, pt->pos.y - n->pivot.y, pt->pos.z - n->pivot.z };
                Vec3 wp = mat4_apply(&inst->node_world[ni], lp);
                if (mat) { float u, v; material_uv(mat, pt->pos.x, pt->pos.y, pt->pos.z, &u, &v); glTexCoord2f(u, v); }
                if (mat) glColor3f(pt->colour.x / 128.0f * g_mat_scale, pt->colour.y / 128.0f * g_mat_scale, pt->colour.z / 128.0f * g_mat_scale);
                glVertex3f(wp.x, wp.y, wp.z);
            }
            glEnd();
        }
    }
    /* skinned triangles */
    if (m->ntris) {
        uint32_t last = 0xffffffff;
        glBegin(GL_TRIANGLES);
        for (uint32_t t = 0; t < m->ntris; t++) {
            InsTri *tr = &m->tris[t]; if (mat_blended(r, tr->material) != pass) continue;
            if (tr->material != last) { glEnd(); set_material(r, tr->material, &mat); last = tr->material; glBegin(GL_TRIANGLES); }
            uint32_t idx[3] = { tr->i0, tr->i1, tr->i2 };
            for (int c = 0; c < 3; c++) {
                Vec3 wp = ins_point_world(inst, idx[c]);
                if (mat) { InsPoint *pt = &m->points[idx[c]]; float u, v; material_uv(mat, pt->pos.x, pt->pos.y, pt->pos.z, &u, &v); glTexCoord2f(u, v); }
                glVertex3f(wp.x, wp.y, wp.z);
            }
        }
        glEnd();
    }
}

void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s)
{
    glViewport(0, 0, w->width, w->height);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glClearColor(0.08f, 0.09f, 0.11f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    glPolygonMode(GL_FRONT_AND_BACK, r->wireframe ? GL_LINE : GL_FILL);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* projection: the world data is right-handed (3ds Max export, y up after the -90 deg x instance rotation), so a plain GL frustum */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    float aspect = w->height ? (float)w->width / (float)w->height : 1.333f, zn = 5.0f, zf = 200000.0f;
    float f = 1.0f / tanf(cam->fov_deg * 3.14159265f / 360.0f);
    float proj[16] = { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1, 0, 0, 2 * zf * zn / (zn - zf), 0 };
    glMultMatrixf(proj);
    /* view: look-at from the free camera */
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    Vec3 fw = cam_forward(cam), rt = cam_right(cam);
    Vec3 up = { rt.y * fw.z - rt.z * fw.y, rt.z * fw.x - rt.x * fw.z, rt.x * fw.y - rt.y * fw.x };   /* right x forward */
    float view[16] = { rt.x, up.x, -fw.x, 0, rt.y, up.y, -fw.y, 0, rt.z, up.z, -fw.z, 0,          /* GL camera looks along -z */
                       -(rt.x * cam->pos.x + rt.y * cam->pos.y + rt.z * cam->pos.z), -(up.x * cam->pos.x + up.y * cam->pos.y + up.z * cam->pos.z), (fw.x * cam->pos.x + fw.y * cam->pos.y + fw.z * cam->pos.z), 1 };
    glLoadMatrixf(view);

    for (int pass = 0; pass < 2; pass++) {                      /* pass 0 opaque, pass 1 additive (no depth writes) */
    if (r->show_world) {
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        set_blend(pass);
        for (uint32_t i = 0; i < r->nbatches; i++) {
            struct WorldBatch *b = &r->batches[i]; if (!b->ntris || group_blended(r, b->group) != pass) continue;
            if (b->group < r->tex->ngroups) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, r->tex->groups[b->group].gl_tex); } else glDisable(GL_TEXTURE_2D);
            glVertexPointer(3, GL_FLOAT, 0, b->pos); glTexCoordPointer(2, GL_FLOAT, 0, b->uv); glColorPointer(3, GL_UNSIGNED_BYTE, 0, b->col);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b->ntris * 3);
        }
        glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    if (r->show_instances) {
        for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) {
            Model *m = &r->ins->models[mi];
            for (uint32_t k = 0; k < m->ninstances; k++) {
                Instance *inst = &m->instances[k]; if (!inst->visible) continue;
                if (pass == 0) ins_pose(inst, inst->anim, inst->anim_time);
                draw_instance(r, inst, pass);
            }
        }
    }
    }
    set_blend(0);
    (void)time_s;
}
