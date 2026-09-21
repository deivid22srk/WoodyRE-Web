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
    PIXELFORMATDESCRIPTOR pfd = {0}; pfd.nSize = sizeof pfd; pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(hdc, &pfd); SetPixelFormat(hdc, pf, &pfd);
    HGLRC rc2 = wglCreateContext(hdc); wglMakeCurrent(hdc, rc2);
    w->hwnd = hwnd; w->hdc = hdc; w->hglrc = rc2; w->width = width; w->height = height;
    printf("OpenGL: %s / %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    return 0;
}
void win_poll(Window *w) { MSG m; w->mouse_dx = w->mouse_dy = 0; while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); } }
void win_swap(Window *w) { SwapBuffers((HDC)w->hdc); }

/* screen brightness like the original's fader 0x4776d0: 1 = normal, 0 = black */
void rnd_fade(float brightness)
{
    if (brightness >= 1.0f) return;
    if (brightness < 0) brightness = 0;
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glColor4f(0, 0, 0, 1.0f - brightness);
    glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
    glColor4f(1, 1, 1, 1); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}
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

/* ---------------------------------------------------------------- static lighting (docs/LIGHTING.md)
 * pixel = 2 * tex * vcol * min(1, AMB + sum C/255 * max(0, 1 - |P-L|/R)) over the lights that see the point. Which
 * light sees what is precomputed in the .lit: list A = faces seen completely, list C = the lit fragments of the
 * partially lit faces B. A cast shadow is simply where no light polygon is. */
#define LIT_AMB (76.0f / 255.0f)                     /* 0x4C4C4C */
static const float *lit_vpos(const Renderer *r, int32_t i, float tmp[3])
{
    if (i >= 0) { const GelVert *v = &r->gel->verts[(uint32_t)i < r->gel->nverts ? (uint32_t)i : 0]; tmp[0] = v->x; tmp[1] = v->y; tmp[2] = v->z; }
    else { uint32_t k = (uint32_t)(-i - 1); Vec3 e = k < r->lit->nextra ? r->lit->extra[k] : (Vec3){ 0, 0, 0 }; tmp[0] = e.x; tmp[1] = e.y; tmp[2] = e.z; }
    return tmp;
}
static void batch_reserve(struct WorldBatch *b, uint32_t extra_tris, uint32_t *cap)
{
    if (b->ntris + extra_tris <= *cap) return;
    *cap = (b->ntris + extra_tris) * 2 + 64;
    b->pos = (float *)realloc(b->pos, (size_t)*cap * 9 * sizeof(float)); b->uv = (float *)realloc(b->uv, (size_t)*cap * 6 * sizeof(float)); b->col = (uint8_t *)realloc(b->col, (size_t)*cap * 9);
}
/* one light polygon (flush 0x4293f0 / 0x42c320): constant colour C*k, radial texture 15 - round(k*15.49) */
static void light_poly(Renderer *r, const LitLight *L, const float *plane, const int32_t *idx, uint32_t n, uint32_t cap[16])
{
    if (n < 3) return;
    float dist = plane[0] * L->pos.x + plane[1] * L->pos.y + plane[2] * L->pos.z + plane[3], R = L->range;
    if (fabsf(dist) >= R || R <= 0) return;
    float k = 1.0f - fabsf(dist) / R; int ti = 15 - (int)(k * 15.49f + 0.5f); if (ti < 0) ti = 0; if (ti > 15) ti = 15;
    float s = 0.5f / sqrtf(R * R - dist * dist);
    float F[3] = { L->pos.x - plane[0] * dist, L->pos.y - plane[1] * dist, L->pos.z - plane[2] * dist }, t0[3], t1[3], t2[3];
    const float *v2 = lit_vpos(r, idx[2], t2);
    float U[3] = { v2[0] - F[0], v2[1] - F[1], v2[2] - F[2] }, ul = sqrtf(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]);
    if (ul < 1e-4f) { U[0] = plane[1]; U[1] = plane[2]; U[2] = plane[0]; float d = U[0] * plane[0] + U[1] * plane[1] + U[2] * plane[2]; for (int q = 0; q < 3; q++) U[q] -= plane[q] * d; ul = sqrtf(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]); if (ul < 1e-4f) return; }
    for (int q = 0; q < 3; q++) U[q] /= ul;
    float W[3] = { plane[1] * U[2] - plane[2] * U[1], plane[2] * U[0] - plane[0] * U[2], plane[0] * U[1] - plane[1] * U[0] };
    struct WorldBatch *b = &r->lightb[ti]; batch_reserve(b, n - 2, &cap[ti]);
    uint8_t col[3]; for (int q = 0; q < 3; q++) { float c = L->colour[q] * k; col[q] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c); }
    for (uint32_t t = 1; t + 1 < n; t++) {
        const float *pv[3] = { lit_vpos(r, idx[0], t0), lit_vpos(r, idx[t], t1), lit_vpos(r, idx[t + 1], t2) };
        for (int c = 0; c < 3; c++) {
            size_t o = (size_t)b->ntris * 3 + c; float d[3] = { pv[c][0] - F[0], pv[c][1] - F[1], pv[c][2] - F[2] };
            b->pos[o * 3] = pv[c][0]; b->pos[o * 3 + 1] = pv[c][1]; b->pos[o * 3 + 2] = pv[c][2];
            b->uv[o * 2] = 0.5f + s * (d[0] * W[0] + d[1] * W[1] + d[2] * W[2]); b->uv[o * 2 + 1] = 0.5f + s * (d[0] * U[0] + d[1] * U[1] + d[2] * U[2]);
            b->col[o * 3] = col[0]; b->col[o * 3 + 1] = col[1]; b->col[o * 3 + 2] = col[2];
        }
        b->ntris++;
    }
}
static void light_textures(Renderer *r)                 /* generator 0x480090: 16 radial 32x32 textures */
{
    enum { N = 32 }; uint8_t px[N * N * 3];
    for (int i = 0; i < 16; i++) {
        float a = i / 16.0f;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            float fx = (x + 0.5f) / N * 2 - 1, fy = (y + 0.5f) / N * 2 - 1, d = sqrtf(fx * fx + fy * fy + a * a); if (d > 1) d = 1;
            float g = (1.0f - d) / (1.0f - a) * 255.0f; if (x == 0 || y == 0 || x == N - 1 || y == N - 1) g = 0;
            uint8_t v = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g); px[(y * N + x) * 3] = px[(y * N + x) * 3 + 1] = px[(y * N + x) * 3 + 2] = v;
        }
        GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, N, N, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        r->light_tex[i] = id;
    }
}

int rnd_init(Renderer *r, TexFile *tex, GelFile *gel, InsFile *ins, const LitFile *lit)
{
    memset(r, 0, sizeof *r); r->tex = tex; r->gel = gel; r->ins = ins; r->show_world = r->show_instances = r->show_light = 1;
    r->lit = (lit && lit->nlights) ? lit : NULL;
    for (uint32_t g = 0; g < tex->ngroups; g++) {
        TexGroup *tg = &tex->groups[g]; tg->gl_frames = (uint32_t *)calloc(tg->frame_count ? tg->frame_count : 1, 4);
        for (uint32_t f = 0; f < tg->frame_count; f++) tg->gl_frames[f] = upload_texture(tg, (int)f);
        tg->gl_tex = tg->gl_frames[0];
    }
    /* face classes: 0 = not lit by any light, 1 = lit (in a list A or B) */
    uint8_t *lit_face = (uint8_t *)calloc(gel->npolys + 1, 1);
    if (r->lit) for (uint32_t l = 0; l < r->lit->nlights; l++) {
        const LitLight *L = &r->lit->lights[l];
        for (uint32_t k = 0; k < L->na; k++) if (L->a[k] < gel->npolys) lit_face[L->a[k]] = 1;
        for (uint32_t k = 0; k < L->nb; k++) if (L->b[k] < gel->npolys) lit_face[L->b[k]] = 1;
    }
    r->nbatches = tex->ngroups + 1;
    r->batches = (struct WorldBatch *)calloc(r->nbatches, sizeof *r->batches); r->litb = (struct WorldBatch *)calloc(r->nbatches, sizeof *r->litb);
    uint32_t *cap = (uint32_t *)calloc(r->nbatches * 2, 4);
    for (uint32_t g = 0; g < r->nbatches; g++) r->batches[g].group = r->litb[g].group = g;
    for (uint32_t i = 0; i < gel->npolys; i++) {
        GelPoly *p = &gel->polys[i]; if (p->nverts < 3) continue;
        int nomat = (p->material & 0x8000) != 0;
        const Material *m = nomat ? NULL : &tex->materials[p->material & 0x7fff];
        uint32_t grp = nomat ? tex->ngroups : m->group; uint32_t gflags = nomat ? 0 : tex->groups[grp].flags;
        if (!nomat && ((gflags >> 8) & 0xff) == 2) {                               /* sky group (0x42acea): the face is never drawn, it only switches the sky cube on */
            TexGroup *sg = &tex->groups[grp];
            if (!r->have_sky) { r->have_sky = 1; r->sky_hu = 0.5f / (float)sg->width; r->sky_hv = 0.5f / (float)sg->height; for (int f = 0; f < 5; f++) r->sky_tex[f] = sg->gl_frames[(uint32_t)f < sg->frame_count ? f : 0]; }
            continue;
        }
        /* multi-pass only for plain opaque textures: colour-keyed faces would get their holes filled by the ambient pass,
         * they take the same light per vertex instead */
        int multipass = r->lit && lit_face[i] && !(gflags & 3);
        struct WorldBatch *b = multipass ? &r->litb[grp] : &r->batches[grp];
        batch_reserve(b, p->nverts - 2, &cap[grp * 2 + multipass]);
        for (uint32_t k = 1; k + 1 < p->nverts; k++) {
            uint32_t idx[3] = { p->indices[0], p->indices[k], p->indices[k + 1] };
            for (int c = 0; c < 3; c++) {
                GelVert *v = &gel->verts[idx[c]]; size_t o = (size_t)b->ntris * 3 + c;
                b->pos[o * 3] = v->x; b->pos[o * 3 + 1] = v->y; b->pos[o * 3 + 2] = v->z;
                float u = 0, vv = 0; if (m) material_uv(m, v->x, v->y, v->z, &u, &vv);
                b->uv[o * 2] = u; b->uv[o * 2 + 1] = vv;
                float scale[3] = { 2, 2, 2 };                                  /* bytes R,G,B; 128 = neutral (modulate 2x) */
                if (multipass) scale[0] = scale[1] = scale[2] = 1.0f;           /* texture pass: 2 * src * dst */
                else if (r->lit && !(gflags & 2)) {
                    if (!lit_face[i]) scale[0] = scale[1] = scale[2] = 0.6f;    /* unlit faces: tex x vcol x 0.6, modulate 1x (0x42bef9) */
                    else for (uint32_t l = 0; l < r->lit->nlights; l++) {       /* colour-keyed lit face: light per vertex */
                        const LitLight *L = &r->lit->lights[l]; int in = 0;
                        for (uint32_t q = 0; q < L->na && !in; q++) in = L->a[q] == i;
                        for (uint32_t q = 0; q < L->nb && !in; q++) in = L->b[q] == i;
                        float dx = v->x - L->pos.x, dy = v->y - L->pos.y, dz = v->z - L->pos.z, f = 1.0f - sqrtf(dx * dx + dy * dy + dz * dz) / L->range;
                        if (l == 0) scale[0] = scale[1] = scale[2] = LIT_AMB;
                        if (in && f > 0) for (int q = 0; q < 3; q++) scale[q] += L->colour[q] / 255.0f * f;
                        if (l + 1 == r->lit->nlights) for (int q = 0; q < 3; q++) scale[q] = (scale[q] > 1 ? 1 : scale[q]) * 2;
                    }
                }
                for (int q = 0; q < 3; q++) { float cv = ((v->colour >> (8 * q)) & 0xff) * scale[q]; b->col[o * 3 + q] = (uint8_t)(cv > 255 ? 255 : cv); }
            }
            b->ntris++;
        }
    }
    free(cap); free(lit_face);
    for (uint32_t g = 0; g < tex->ngroups; g++) if (tex->groups[g].flags & 2) {          /* additive groups: intensity = flags byte 2 */
        uint32_t a = (tex->groups[g].flags >> 16) & 0xff; struct WorldBatch *b = &r->batches[g];
        for (size_t i = 0; i < (size_t)b->ntris * 9; i++) b->col[i] = (uint8_t)(b->col[i] * a / 255);
    }
    if (r->lit) {
        r->face_bound = (float *)calloc(gel->npolys + 1, 4 * sizeof(float));
        for (uint32_t i = 0; i < gel->npolys; i++) {
            const GelPoly *p = &gel->polys[i]; float *fb = &r->face_bound[4 * i]; if (!p->nverts) continue;
            for (uint32_t k = 0; k < p->nverts; k++) { const GelVert *v = &gel->verts[p->indices[k]]; fb[0] += v->x; fb[1] += v->y; fb[2] += v->z; }
            fb[0] /= p->nverts; fb[1] /= p->nverts; fb[2] /= p->nverts;
            for (uint32_t k = 0; k < p->nverts; k++) { const GelVert *v = &gel->verts[p->indices[k]]; float dx = v->x - fb[0], dy = v->y - fb[1], dz = v->z - fb[2], d = sqrtf(dx * dx + dy * dy + dz * dz); if (d > fb[3]) fb[3] = d; }
        }
        uint32_t lcap[16] = { 0 }; light_textures(r);
        for (uint32_t l = 0; l < r->lit->nlights; l++) {
            const LitLight *L = &r->lit->lights[l];
            for (uint32_t k = 0; k < L->na; k++) if (L->a[k] < gel->npolys) { const GelPoly *p = &gel->polys[L->a[k]]; light_poly(r, L, p->plane, (const int32_t *)p->indices, p->nverts, lcap); }
            for (uint32_t k = 0; k < L->nc; k++) light_poly(r, L, L->c[k].plane, L->c[k].indices, L->c[k].n, lcap);
        }
        uint32_t nl = 0; for (int t = 0; t < 16; t++) nl += r->lightb[t].ntris;
        printf("lighting: %u lights, %u light triangles\n", r->lit->nlights, nl);
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

void rnd_free(Renderer *r)
{
    for (uint32_t i = 0; i < r->nbatches; i++) { free(r->batches[i].pos); free(r->batches[i].uv); free(r->batches[i].col); free(r->litb[i].pos); free(r->litb[i].uv); free(r->litb[i].col); }
    free(r->batches); free(r->litb); free(r->face_bound);
    for (int t = 0; t < 16; t++) { free(r->lightb[t].pos); free(r->lightb[t].uv); free(r->lightb[t].col); if (r->light_tex[t]) { GLuint id = r->light_tex[t]; glDeleteTextures(1, &id); } }
    for (uint32_t g = 0; r->tex && g < r->tex->ngroups; g++) {                 /* the level's textures live in the GL context, not in the TexFile */
        TexGroup *tg = &r->tex->groups[g]; if (!tg->gl_frames) continue;
        for (uint32_t f = 0; f < tg->frame_count; f++) { GLuint id = tg->gl_frames[f]; if (id) glDeleteTextures(1, &id); }
        free(tg->gl_frames); tg->gl_frames = NULL; tg->gl_tex = 0;
    }
    memset(r, 0, sizeof *r);
}

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
static uint32_t g_last_material = 0xffffffffu, g_last_frame;
static int g_mat_blended;                            /* [0x5ac8d8]: the material now bound has group flag bit 1 */

/* ---------------------------------------------------------------- instances */
/* ---- model vertex batching: the model code below is written like immediate mode (begin / colour / texcoord / vertex),
 * but the vertices are collected in one array per material state and sent with glDrawArrays. Immediate mode cost
 * 25 ms per frame in the hubs. Fans are turned into triangles; bt_flush() must run before any GL state change. */
static struct { float *v; uint32_t n, cap; float col[3], uv[2], first[8], prev[8]; int fan, count; } g_bt;
static void bt_flush(void)
{
    if (!g_bt.n) return;
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 32, g_bt.v); glTexCoordPointer(2, GL_FLOAT, 32, g_bt.v + 3); glColorPointer(3, GL_FLOAT, 32, g_bt.v + 5);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_bt.n);
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
    g_bt.n = 0;
}
static void bt_push(const float *v8)
{
    if (g_bt.n + 1 > g_bt.cap) { g_bt.cap = g_bt.cap * 2 + 4096; g_bt.v = (float *)realloc(g_bt.v, (size_t)g_bt.cap * 8 * sizeof(float)); }
    memcpy(g_bt.v + (size_t)g_bt.n * 8, v8, 8 * sizeof(float)); g_bt.n++;
}
static void bt_begin(int fan) { g_bt.fan = fan; g_bt.count = 0; }
static void bt_end(void) { }
static void bt_color(float r, float g, float b) { g_bt.col[0] = r; g_bt.col[1] = g; g_bt.col[2] = b; }
static void bt_texcoord(float u, float v) { g_bt.uv[0] = u; g_bt.uv[1] = v; }
static void bt_vertex(float x, float y, float z)
{
    float v[8] = { x, y, z, g_bt.uv[0], g_bt.uv[1], g_bt.col[0], g_bt.col[1], g_bt.col[2] };
    if (!g_bt.fan) { bt_push(v); return; }
    if (g_bt.count == 0) memcpy(g_bt.first, v, sizeof v);
    else if (g_bt.count >= 2) { bt_push(g_bt.first); bt_push(g_bt.prev); bt_push(v); }
    memcpy(g_bt.prev, v, sizeof v); g_bt.count++;
}

static float g_tex_now;                                          /* game time of this frame, for the per instance texture override */
static uint32_t tex_frame(const Instance *I, const TexGroup *g)   /* frame mode B of 0x47f290 (docs/INSTANCE.md 2), n = frame_count, P = duration x factor */
{
    int n = (int)g->frame_count; float P = g->anim_duration * I->tex_fac;
    if (n < 2 || P <= 0) return 0;
    float t = g_tex_now - I->tex_t0, u = t / P; int k, f = 0;
    u = u - floorf(u);                                           /* the looping modes run on the fraction, the one shot modes test t against P first */
    switch (I->tex_mode) {
    case 1: f = t >= P ? n - 1 : (int)(t / P * n); break;
    case 2: f = t >= P ? 0 : (int)((1.0f - t / P) * n); break;
    case 3: if (t >= P) { f = 0; break; } k = (int)(t / P * (2 * n - 1)); f = k >= n ? 2 * n - 1 - k : k; break;
    case 4: f = (int)(u * n); break;
    case 5: f = (int)((1.0f - u) * n); break;
    case 6: k = (int)(u * (2 * n - 1)); f = k >= n ? 2 * n - 1 - k : k; break;
    }
    return (uint32_t)(f < 0 ? 0 : f > n - 1 ? n - 1 : f);
}

static void set_material(const Renderer *r, uint32_t material, const Material **mat_out, uint32_t frame, const Instance *inst)   /* frame: models never auto-cycle (0x47f290), only an override does */
{
    /* GL state = (texture or none, blend mode). Every polygon has its own material record (a planar projection), so the
     * batch is keyed on the state, not on the material index: g_last_material holds texture id + 1 (0 = untextured) | blend << 31 */
    *mat_out = NULL; g_mat_blended = 0;
    uint32_t tex = 0; int bl = 0; float col[3] = { 1, 0, 1 };
    if (material & 0x8000) argb1555_to_rgb(material, col);
    else if (material < r->tex->nmaterials) {
        const Material *m = &r->tex->materials[material]; *mat_out = m; const TexGroup *g = &r->tex->groups[m->group];
        bl = g_mat_blended = (g->flags & 2) != 0;        /* byte 2 of the flags looks like an intensity, but nothing in the engine reads tex+0x46 */
        if (!frame && inst && inst->tex_mode) frame = tex_frame(inst, g);
        tex = g->gl_frames[frame < g->frame_count ? frame : 0];
    }
    uint32_t key = (tex + 1) | (uint32_t)bl << 31;
    if (key != g_last_material) {
        bt_flush(); g_last_material = key; set_blend(bl);
        if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    }
    bt_color(col[0], col[1], col[2]);
}

/* ---- model lighting (0x42e3e4 light choice, 0x43b912 light vector, 0x43bce4 vertex colour; docs/LIGHTING.md 3).
 * Simplified to one light vector per instance (the original keeps one per model part). */
static const int32_t *model_owner(Model *m)
{
    if (!m->owner) {
        m->owner = (int32_t *)malloc((size_t)(m->npoints ? m->npoints : 1) * 4);
        for (uint32_t i = 0; i < m->npoints; i++) m->owner[i] = -1;
        for (uint32_t n = 0; n < m->nnodes; n++) for (uint32_t k = 0; k < m->nodes[n].npoints; k++) if (m->nodes[n].point_base + k < m->npoints && m->owner[m->nodes[n].point_base + k] < 0) m->owner[m->nodes[n].point_base + k] = (int32_t)n;
    }
    return m->owner;
}
/* 0x42e3e4..0x42e573: the candidate lights of an instance are the lights of its sector (the .lit trailer, lightsys+0x10,
 * indexed by inst+0x1c). n == 0 means no light and no shadow; n == 1 is taken without any test; otherwise the first
 * light that sees the point wins and, when none does, the one whose shadow plane the point is least far behind. Taking
 * the first light of the whole level instead put everyone inside the House under the sun that stands over the village,
 * 15000 units away, and its lit faces are all outdoors - so nothing there ever received a shadow. */
static int sector_light(const Renderer *r, Vec3 p, int *have_list)
{
    const LitFile *lf = r->lit; const GelFile *g = r->gel;
    int32_t sec = gel_sector(g, p);
    *have_list = sec >= 0 && (uint32_t)sec < lf->nsectors;
    if (!*have_list) return -1;
    const LitSector *S = &lf->sectors[sec];
    if (S->n == 1) return S->idx[0] < lf->nlights ? (int)S->idx[0] : -1;        /* 0x42e422: no visibility or range test */
    int fallback = -1; float fb_d = -1e30f;
    for (uint32_t k = 0; k < S->n; k++) {
        uint32_t li = S->idx[k]; if (li >= lf->nlights) continue;
        const LitLight *L = &lf->lights[li];
        int32_t f = lit_bsp_face(L, g, p);
        if (f < 0) {                                                            /* 0x42e4c2: in the open part of the light volume */
            float dx = L->pos.x - p.x, dy = L->pos.y - p.y, dz = L->pos.z - p.z;
            if (dx * dx + dy * dy + dz * dz < L->range * L->range) return (int)li;
            continue;
        }
        const float *fp = g->polys[f].plane; float d = fp[0] * p.x + fp[1] * p.y + fp[2] * p.z + fp[3];
        if (d > 0.0f) return (int)li;                                           /* 0x42e541: in front of the leaf face, no range test */
        if (d > fb_d) { fb_d = d; fallback = (int)li; }                         /* 0x42e4a4: least far behind it */
    }
    return fallback;
}
static void instance_light(const Renderer *r, Instance *inst, float dt)
{
    const LitFile *lf = r->lit; Vec3 p = { inst->world.m[12], inst->world.m[13] + 20.0f, inst->world.m[14] };
    int have_list = 0, chosen = sector_light(r, p, &have_list), fallback = -1; float cd = 0;
    if (!have_list)                                                             /* no trailer in this .lit: the whole light list, as before */
        for (uint32_t l = 0; l < lf->nlights; l++) {
            const LitLight *L = &lf->lights[l]; float dx = L->pos.x - p.x, dy = L->pos.y - p.y, dz = L->pos.z - p.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < L->range && lit_point_lit(L, r->gel, p)) { chosen = (int)l; break; }
            if (fallback < 0 || d / L->range < 1.0f) fallback = (int)l;
        }
    if (chosen < 0) chosen = fallback;
    if (chosen >= 0) { const LitLight *L = &lf->lights[chosen]; float dx = L->pos.x - p.x, dy = L->pos.y - p.y, dz = L->pos.z - p.z; cd = sqrtf(dx * dx + dy * dy + dz * dz); }
    /* the light vector only grows while the light really sees the part and it is in range (0x43b912); the choice above
     * stands either way, and so does the shadow */
    int seen = chosen >= 0 && cd < lf->lights[chosen].range && lit_point_lit(&lf->lights[chosen], r->gel, p);
    float keep = powf(0.85f, dt * 60.0f); if (!inst->l_init) keep = 0;          /* Ldir *= 0.85 per frame [0x4aa3d8] */
    inst->ldir.x *= keep; inst->ldir.y *= keep; inst->ldir.z *= keep;
    inst->l_seen = seen; inst->light = chosen; inst->l_init = 1;
    if (seen) {
        const LitLight *L = &lf->lights[chosen]; float k = (1.0f - keep) * (1.0f - cd / L->range) / (cd > 1e-3f ? cd : 1.0f);
        inst->ldir.x += (L->pos.x - p.x) * k; inst->ldir.y += (L->pos.y - p.y) * k; inst->ldir.z += (L->pos.z - p.z) * k;
    }
    if (inst->light >= 0) for (int q = 0; q < 3; q++) inst->lcol[q] = lf->lights[inst->light].colour[q];
}
/* vertex colour: vcol * 0.3 + max(0, N.Ldir) * C, drawn MODULATE2X.
 * Except on a blended face: 0x43d91d tests the flag 0x43d7cf raises for polygon flags 0x20/0x40 (group flag bit 1,
 * copied into the polygon at load by 0x428020) and jumps straight past the lit RGB at v+0x24..0x2c. It writes
 * 0x00iiiiii with i = (int)(alpha * 0.5) (0x43d9a4) and alpha = (1 - inst->fade) * 255 (0x43b504), so i = 128 for an
 * instance that is not fading, and under MODULATE2X that is plain 1.0 x texture. A neon sign is therefore never dimmed
 * by the world light or by the angle its own plate makes with it - which is what "glowing" means here. */
static void lit_vertex_colour(const Renderer *r, const Instance *inst, const Mat4 *M, const InsPoint *pt, const float base[3])
{
    if (g_mat_blended) { float a = 1.0f - inst->fade; bt_color(base[0] * a, base[1] * a, base[2] * a); return; }
    if (!r->lit || !r->show_light) { bt_color(base[0] * pt->colour.x / 128.0f, base[1] * pt->colour.y / 128.0f, base[2] * pt->colour.z / 128.0f); return; }
    const float *a = M->m; Vec3 n = pt->normal;
    Vec3 w = { a[0] * n.x + a[4] * n.y + a[8] * n.z, a[1] * n.x + a[5] * n.y + a[9] * n.z, a[2] * n.x + a[6] * n.y + a[10] * n.z };
    float l = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z), ndl = l > 1e-6f ? (w.x * inst->ldir.x + w.y * inst->ldir.y + w.z * inst->ldir.z) / l : 0; if (ndl < 0) ndl = 0;
    float vc[3] = { pt->colour.x, pt->colour.y, pt->colour.z }, c[3];
    for (int q = 0; q < 3; q++) { c[q] = (vc[q] * 0.6f + 2.0f * ndl * inst->lcol[q]) / 255.0f; if (c[q] > 1) c[q] = 1; c[q] *= base[q]; if (q && inst->tint_red) c[q] = 0; }
    bt_color(c[0], c[1], c[2]);
}

/* ---- cast shadows (0x42e651-0x42ec3a, drawn by 0x4385f0): the caster's geometry projected from its light onto the
 * receiving faces, as opaque ambient-coloured polygons between the light pass and the texture pass, so the shadow
 * looks like an unlit face. The original clips against the faces on the CPU; here the stencil buffer does it. */
static float *g_sh; static uint32_t g_sh_n, g_sh_cap;                       /* caster triangles, world space */
static void sh_push(Vec3 a, Vec3 b, Vec3 c)
{
    if (g_sh_n + 1 > g_sh_cap) { g_sh_cap = g_sh_cap * 2 + 1024; g_sh = (float *)realloc(g_sh, (size_t)g_sh_cap * 9 * sizeof(float)); }
    float *o = &g_sh[(size_t)g_sh_n * 9]; o[0] = a.x; o[1] = a.y; o[2] = a.z; o[3] = b.x; o[4] = b.y; o[5] = b.z; o[6] = c.x; o[7] = c.y; o[8] = c.z; g_sh_n++;
}
static int g_shlog;                                                         /* WOODY_SHLOG=1: one line per second per instance that reaches the caster test */
static void cast_shadow(const Renderer *r, Instance *inst)
{
    Model *m = inst->model; const LitLight *L = &r->lit->lights[inst->light]; const int32_t *own = model_owner(m);
    g_sh_n = 0;
    for (uint32_t ni = 0; ni < m->nnodes; ni++) {
        InsNode *n = &m->nodes[ni]; if (n->kind != 0 || !n->polys || n->type_code == 2) continue;
        for (uint32_t k = 0; k < n->npolys; k++) {
            InsPoly *p = &n->polys[k]; if (p->nverts < 3 || mat_blended(r, p->material)) continue;
            Vec3 w[3];
            for (uint32_t c = 0; c < p->nverts; c++) {
                InsPoint *pt = &m->points[p->indices[c]]; Vec3 lp = { pt->pos.x - n->pivot.x, pt->pos.y - n->pivot.y, pt->pos.z - n->pivot.z };
                Vec3 q = mat4_apply(&inst->node_world[ni], lp);
                if (c == 0) w[0] = q; else { w[1] = w[2]; w[2] = q; if (c >= 2) sh_push(w[0], w[1], w[2]); }
            }
        }
    }
    for (uint32_t t = 0; t < m->ntris; t++) {
        InsTri *tr = &m->tris[t]; if (mat_blended(r, tr->material)) continue;
        uint32_t idx[3] = { tr->i0, tr->i1, tr->i2 }; Vec3 w[3];
        for (int c = 0; c < 3; c++) { int o = own[idx[c]]; Vec3 lp = m->points[idx[c]].pos; if (o >= 0) { lp.x -= m->nodes[o].pivot.x; lp.y -= m->nodes[o].pivot.y; lp.z -= m->nodes[o].pivot.z; } w[c] = mat4_apply(o >= 0 ? &inst->node_world[o] : &inst->world, lp); }
        sh_push(w[0], w[1], w[2]);
    }
    if (!g_sh_n) return;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (uint32_t i = 0; i < g_sh_n * 3; i++) for (int q = 0; q < 3; q++) { float v = g_sh[i * 3 + q]; if (v < lo[q]) lo[q] = v; if (v > hi[q]) hi[q] = v; }
    float c[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
    float rad = 0.5f * sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
    static float *proj; static uint32_t proj_cap; if (proj_cap < g_sh_n) { proj_cap = g_sh_n + 1024; proj = (float *)realloc(proj, (size_t)proj_cap * 9 * sizeof(float)); }
    int n_plane = 0, n_scale = 0, n_reach = 0, n_drawn = 0;
    for (int list = 0; list < 2; list++) {
        const uint32_t *faces = list ? L->b : L->a; uint32_t nf = list ? L->nb : L->na;
        for (uint32_t fi = 0; fi < nf; fi++) {
            uint32_t f = faces[fi]; if (f >= r->gel->npolys) continue;
            const GelPoly *gp = &r->gel->polys[f]; const float *pl = gp->plane, *fb = &r->face_bound[4 * f]; if (gp->nverts < 3) continue;
            float dl = pl[0] * L->pos.x + pl[1] * L->pos.y + pl[2] * L->pos.z + pl[3], dc = pl[0] * c[0] + pl[1] * c[1] + pl[2] * c[2] + pl[3];
            if (dl <= 1.0f || dc >= dl || dc < -rad) continue;                 /* caster must be between the light and the plane */
            n_plane++;
            float den = dl - dc; if (den < 1.0f) continue;
            float s = dl / den; if (s > 40.0f) continue;                      /* projection scale; huge = grazing */
            n_scale++;
            float pc[3] = { L->pos.x + (c[0] - L->pos.x) * s, L->pos.y + (c[1] - L->pos.y) * s, L->pos.z + (c[2] - L->pos.z) * s };
            float dx = pc[0] - fb[0], dy = pc[1] - fb[1], dz = pc[2] - fb[2], reach = rad * s * 1.5f + fb[3];
            if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
            n_reach++;
            uint32_t np = 0;
            for (uint32_t t = 0; t < g_sh_n; t++) {
                const float *tv = &g_sh[(size_t)t * 9]; float *o = &proj[(size_t)np * 9]; int ok = 1;
                for (int v = 0; v < 3 && ok; v++) {
                    float ex = tv[v * 3] - L->pos.x, ey = tv[v * 3 + 1] - L->pos.y, ez = tv[v * 3 + 2] - L->pos.z, nd = pl[0] * ex + pl[1] * ey + pl[2] * ez;
                    if (nd > -1e-3f) { ok = 0; break; }
                    float k = -dl / nd; if (k < 1.0f || k > 100.0f) { ok = 0; break; }   /* k < 1: the vertex is behind the plane */
                    o[v * 3] = L->pos.x + ex * k; o[v * 3 + 1] = L->pos.y + ey * k; o[v * 3 + 2] = L->pos.z + ez * k;
                }
                if (ok) np++;
            }
            if (!np) continue;
            /* stencil = 1 on the visible part of the receiving face */
            glColorMask(0, 0, 0, 0); glStencilFunc(GL_ALWAYS, 1, 1); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glBegin(GL_TRIANGLE_FAN); for (uint32_t k = 0; k < gp->nverts; k++) { const GelVert *v = &r->gel->verts[gp->indices[k]]; glVertex3f(v->x, v->y, v->z); } glEnd();
            glColorMask(1, 1, 1, 1); glStencilFunc(GL_EQUAL, 1, 1); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); glDisable(GL_DEPTH_TEST);
            glVertexPointer(3, GL_FLOAT, 0, proj); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)np * 3);
            n_drawn++;
            glEnable(GL_DEPTH_TEST); glColorMask(0, 0, 0, 0); glStencilFunc(GL_ALWAYS, 0, 1); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glBegin(GL_TRIANGLE_FAN); for (uint32_t k = 0; k < gp->nverts; k++) { const GelVert *v = &r->gel->verts[gp->indices[k]]; glVertex3f(v->x, v->y, v->z); } glEnd();
            glColorMask(1, 1, 1, 1);
        }
    }
    if (g_shlog) printf("    tris %u light %d at %.0f %.0f %.0f range %.0f faces A %u B %u -> plane %d scale %d reach %d drawn %d", g_sh_n, inst->light, L->pos.x, L->pos.y, L->pos.z, L->range, L->na, L->nb, n_plane, n_scale, n_reach, n_drawn), puts("");
}
static void draw_cast_shadows(const Renderer *r)
{
    { static int last = -1; int s = (int)g_tex_now; g_shlog = getenv("WOODY_SHLOG") && s != last; if (g_shlog) last = s; }
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_STENCIL_TEST); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glColor3f(LIT_AMB, LIT_AMB, LIT_AMB);
    for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) {
        Model *m = &r->ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            Instance *inst = &m->instances[k];
            int caster = inst->type == 1 || inst->type == 2 || inst->type == 3 || inst->type == 18 || inst->type == 19 || (inst->setflags & 1) || (inst->type >= 4 && inst->type <= 13);   /* the player, SetFlags bit 1 (0x42b3cc); enemies are our addition */
            if (g_shlog && (caster || inst->type)) printf("  SH t %.1f model %u inst %u type %d setflags %x caster %d vis %d fade %.2f l_seen %d light %d nodes %d at %.0f %.0f %.0f", g_tex_now, mi, k, inst->type, inst->setflags, caster, inst->visible, inst->fade, inst->l_seen, inst->light, inst->node_world != NULL, inst->world.m[12], inst->world.m[13], inst->world.m[14]), puts("");
            /* 0x42e2c3/0x42e377/0x42e417: no sector, fade >= 0.98 or an empty sector light list drop the shadow. Whether
             * the light SEES the caster is never tested - a character standing in shadow still casts one, from the
             * fallback light (0x42e524). */
            if (!caster || !inst->visible || inst->fade > 0.98f || inst->light < 0 || !inst->node_world) continue;
            cast_shadow(r, inst);
        }
    }
    glDisable(GL_STENCIL_TEST); glDisable(GL_POLYGON_OFFSET_FILL); glEnableClientState(GL_COLOR_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_BLEND);
}

/* inverse of an affine matrix applied to a point (rotation, scale, translation) */
static int affine_inv_apply(const Mat4 *M, Vec3 w, Vec3 *out)
{
    const float *a = M->m; float x = w.x - a[12], y = w.y - a[13], z = w.z - a[14];
    float c00 = a[5] * a[10] - a[9] * a[6], c01 = a[8] * a[6] - a[4] * a[10], c02 = a[4] * a[9] - a[8] * a[5];
    float det = a[0] * c00 + a[1] * c01 + a[2] * c02; if (fabsf(det) < 1e-12f) return 0;
    float id = 1.0f / det;
    out->x = (c00 * x + c01 * y + c02 * z) * id;
    out->y = ((a[9] * a[2] - a[1] * a[10]) * x + (a[0] * a[10] - a[8] * a[2]) * y + (a[8] * a[1] - a[0] * a[9]) * z) * id;
    out->z = ((a[1] * a[6] - a[5] * a[2]) * x + (a[4] * a[2] - a[0] * a[6]) * y + (a[0] * a[5] - a[4] * a[1]) * z) * id;
    return 1;
}
/* texture frames for mesh nodes with typecode 5..8: the last type-5 event of the root node's event track with
 * t <= the current frame (0x43b58b-0x43b62c, docs/MODEL_RENDER.md 5). This is how the eyes blink. */
static void event_frames(const Instance *inst, uint32_t out[4])
{
    const Model *m = inst->model; out[0] = out[1] = out[2] = out[3] = 0;
    if (!m->nnodes || !m->nanims || inst->anim < 0 || (uint32_t)inst->anim >= m->nanims) return;
    const InsNode *n = &m->nodes[0]; if (!n->event_refs || !n->pool) return;
    const InsAnim *a = &m->anims[inst->anim]; float dur = a->duration_s > 0 ? a->duration_s : 1.0f;
    float ph = fmodf(inst->anim_time / dur, 1.0f); if (ph < 0) ph += 1.0f; float tf = ph * (float)a->nframes;
    const uint32_t *e = (const uint32_t *)(n->pool + ((size_t)n->a + n->b + n->event_refs[inst->anim].off) * 4);
    for (uint32_t i = 0; i < n->event_refs[inst->anim].cnt; i++) {
        uint32_t type = e[0], size = type == 3 ? 15 : type == 4 ? 9 : type == 5 ? 6 : 0; if (!size) return;
        float t; memcpy(&t, &e[1], 4);
        if (type == 5 && t <= tf) { out[0] = e[2]; out[1] = e[3]; out[2] = e[4]; out[3] = e[5]; }
        e += size;
    }
}

/* one mesh node layer. helper >= 0: UVs come from the helper child node (0x43b74d-0x43b908), the moving pupil */
/* ---- back-face culling (0x43bf65 for node polygons, 0x43c1a4 for the skinned triangles). The original culls per
 * polygon on the CPU - the device is left on D3DCULL_NONE - because polygon flag 0x2 marks a double-sided polygon that
 * must survive. Drawing the back faces too is not just wasted fill: a back face has its normals pointing away, so
 * lit_vertex_colour() gives it ndl = 0 and only the 0.6 * vcol ambient term, and wherever front and back tie in depth
 * (exactly along a silhouette) the dark one can win - a dark rim around every character. */
static Vec3 g_cam_pos;
static const float *poly_plane(Model *m, const InsNode *n, InsPoly *p)
{
    if (!p->plane_ok) {
        const InsPoint *a = &m->points[p->indices[0]], *b = &m->points[p->indices[1]], *c = &m->points[p->indices[2]];
        float ux = b->pos.x - a->pos.x, uy = b->pos.y - a->pos.y, uz = b->pos.z - a->pos.z;
        float vx = c->pos.x - a->pos.x, vy = c->pos.y - a->pos.y, vz = c->pos.z - a->pos.z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        float sx = 0, sy = 0, sz = 0;                                          /* the file winding points inward; the stored
                                                                                * vertex normals say which way is out */
        for (uint32_t i = 0; i < p->nverts; i++) { const InsPoint *q = &m->points[p->indices[i]]; sx += q->normal.x; sy += q->normal.y; sz += q->normal.z; }
        if (nx * sx + ny * sy + nz * sz < 0) { nx = -nx; ny = -ny; nz = -nz; }
        float l = sqrtf(nx * nx + ny * ny + nz * nz); if (l > 1e-12f) { nx /= l; ny /= l; nz /= l; }
        float ax = a->pos.x - n->pivot.x, ay = a->pos.y - n->pivot.y, az = a->pos.z - n->pivot.z;
        p->plane[0] = nx; p->plane[1] = ny; p->plane[2] = nz; p->plane[3] = -(nx * ax + ny * ay + nz * az);
        p->plane_ok = 1;
    }
    return p->plane;
}
static void draw_node_polys(const Renderer *r, Instance *inst, uint32_t ni, int pass, uint32_t frame, int helper)
{
    Model *m = inst->model; InsNode *n = &m->nodes[ni]; const Material *mat;
    Vec3 cl; int have_cl = affine_inv_apply(&inst->node_world[ni], g_cam_pos, &cl);    /* the camera in this node's space */
    for (uint32_t k = 0; k < n->npolys; k++) {
        InsPoly *p = &n->polys[k]; if (p->nverts < 3 || mat_blended(r, p->material) != pass) continue;
        if (have_cl && !(p->flags & 2)) { const float *pl = poly_plane(m, n, p); if (pl[0] * cl.x + pl[1] * cl.y + pl[2] * cl.z + pl[3] <= 0) continue; }
        set_material(r, p->material, &mat, frame, inst);
        bt_begin(1);
        for (uint32_t c = 0; c < p->nverts; c++) {
            InsPoint *pt = &m->points[p->indices[c]];
            Vec3 lp = { pt->pos.x - n->pivot.x, pt->pos.y - n->pivot.y, pt->pos.z - n->pivot.z };
            Vec3 wp = mat4_apply(&inst->node_world[ni], lp);
            if (mat) {
                float u, v; Vec3 q; const InsNode *h = helper >= 0 ? &m->nodes[helper] : NULL;
                if (h && h->helper_a != 0 && h->helper_b != 0 && affine_inv_apply(&inst->node_world[helper], wp, &q)) {
                    float ha = h->helper_mode == 0 ? q.y : q.x, hb = h->helper_mode == 2 ? q.y : q.z;
                    u = 0.5f - ha / h->helper_b; v = hb / h->helper_a - 0.5f;
                } else material_uv(mat, lp.x, lp.y, lp.z, &u, &v);              /* planar projection of the pivot-relative point (0x43da37) */
                bt_texcoord(u, v);
            }
            { float base[3] = { 1, 1, 1 }; if (!mat && (p->material & 0x8000)) argb1555_to_rgb(p->material, base); if (mat || (p->material & 0x8000)) lit_vertex_colour(r, inst, &inst->node_world[ni], pt, base); }
            bt_vertex(wp.x, wp.y, wp.z);
        }
        bt_end();
    }
}

static void draw_instance(const Renderer *r, Instance *inst, int pass)   /* pass 0 = opaque, 1 = blended */
{
    Model *m = inst->model;
    const Material *mat;
    uint32_t evf[4]; event_frames(inst, evf);
    /* rigid node polygons: only mesh nodes, never those with typecode 2 (0x43b6c2) */
    for (uint32_t ni = 0; ni < m->nnodes; ni++) {
        InsNode *n = &m->nodes[ni]; if (n->kind != 0 || !n->polys || n->type_code == 2) continue;
        int helper = -1;
        for (uint32_t j = 0; j < m->nnodes; j++) if (m->nodes[j].kind == 0x10 && m->nodes[j].parent == (int32_t)ni) { helper = (int)j; break; }
        uint32_t lid = (n->type_code >= 5 && n->type_code <= 8) ? evf[n->type_code - 5] : 0;
        draw_node_polys(r, inst, ni, pass, 0, helper);
        if (lid) { bt_flush(); glDepthFunc(GL_LEQUAL); draw_node_polys(r, inst, ni, pass, lid, -1); bt_flush(); glDepthFunc(GL_LESS); }   /* eyelid layer on top of the eyeball */
    }
    /* skinned triangles */
    if (m->ntris) {
        uint32_t last = 0xffffffff; const int32_t *own = model_owner(m); float base[3] = { 1, 1, 1 };
        bt_begin(0);
        for (uint32_t t = 0; t < m->ntris; t++) {
            InsTri *tr = &m->tris[t]; if (mat_blended(r, tr->material) != pass) continue;
            if (tr->material != last) { bt_end(); set_material(r, tr->material, &mat, 0, inst); last = tr->material; base[0] = base[1] = base[2] = 1; if (tr->material & 0x8000) argb1555_to_rgb(tr->material, base); bt_begin(0); }
            uint32_t idx[3] = { tr->i0, tr->i1, tr->i2 };
            Vec3 wp[3]; const Mat4 *MM[3];
            for (int c = 0; c < 3; c++) {
                int o = own[idx[c]]; Vec3 lp = m->points[idx[c]].pos; if (o >= 0) { lp.x -= m->nodes[o].pivot.x; lp.y -= m->nodes[o].pivot.y; lp.z -= m->nodes[o].pivot.z; }
                MM[c] = o >= 0 ? &inst->node_world[o] : &inst->world; wp[c] = mat4_apply(MM[c], lp);
            }
            {   /* 0x43c1a4: n = (A - B) x (A - C) in world space, front-facing iff n . (camera - A) > 0 */
                float ux = wp[0].x - wp[1].x, uy = wp[0].y - wp[1].y, uz = wp[0].z - wp[1].z;
                float vx = wp[0].x - wp[2].x, vy = wp[0].y - wp[2].y, vz = wp[0].z - wp[2].z;
                float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                if (nx * (g_cam_pos.x - wp[0].x) + ny * (g_cam_pos.y - wp[0].y) + nz * (g_cam_pos.z - wp[0].z) <= 0) continue;
            }
            for (int c = 0; c < 3; c++) {
                InsPoint *pt = &m->points[idx[c]];
                if (mat || (tr->material & 0x8000)) lit_vertex_colour(r, inst, MM[c], pt, base);
                if (mat) bt_texcoord(mat->m[6 - 3 * c], mat->m[7 - 3 * c]);   /* explicit UVs: the material holds three UV pairs, file vertex j = (m[3j], m[3j+1]) and i0 is the third file vertex (0x43e39a) */
                bt_vertex(wp[c].x, wp[c].y, wp[c].z);
            }
        }
        bt_end();
    }
}

/* ---- outline (0x43ea30, fed by the two back-face lists 0x43b3f0 collects): the back faces once more, every vertex
 * pushed out along its own normal, flat black, at the same depth as the model. Only for instances that the level
 * script gave SetFlags bit 0x20 (message 45) - characters and a handful of props - and only within 1500 units.
 * w = d/300 up to 2.5, then 5 - d/300 (0x43b4ce..0x43b4f3), so the rim keeps a constant width on screen. Drawn after
 * the model with the ordinary depth test: outside the silhouette the hull is all there is, and where it pokes through
 * a concave fold it beats the model - that is where the creases along a snout or a finger come from. */
static float *g_ol; static uint32_t g_ol_n, g_ol_cap;
static void ol_push(Vec3 a, Vec3 b, Vec3 c)
{
    if (g_ol_n + 1 > g_ol_cap) { g_ol_cap = g_ol_cap * 2 + 1024; g_ol = (float *)realloc(g_ol, (size_t)g_ol_cap * 9 * sizeof(float)); }
    float *o = &g_ol[(size_t)g_ol_n * 9]; o[0] = a.x; o[1] = a.y; o[2] = a.z; o[3] = b.x; o[4] = b.y; o[5] = b.z; o[6] = c.x; o[7] = c.y; o[8] = c.z; g_ol_n++;
}
static Vec3 ol_vertex(const Instance *inst, const Mat4 *M, const InsPoint *pt, Vec3 pivot, float w)
{
    Vec3 n = pt->normal; float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);   /* the original normalises at load (0x427c01), the port does not */
    if (l > 1e-6f) { n.x /= l; n.y /= l; n.z /= l; } else { n.x = n.y = n.z = 0; }
    Vec3 lp = { pt->pos.x - pivot.x + w * n.x, pt->pos.y - pivot.y + w * n.y, pt->pos.z - pivot.z + w * n.z };
    (void)inst; return mat4_apply(M, lp);
}
static void draw_outline(const Renderer *r, Instance *inst)
{
    if (!(inst->setflags & 0x20) || !inst->node_world) return;                  /* 0x43b423; the second gate is the cfg detail level, 2 in the shipped Woody.cfg */
    const float *wm = inst->world.m;
    float dx = wm[12] - g_cam_pos.x, dy = wm[13] - g_cam_pos.y, dz = wm[14] - g_cam_pos.z;
    float w = sqrtf(dx * dx + dy * dy + dz * dz) / 300.0f;
    if (w > 2.5f) { w = 5.0f - w; if (w <= 0) return; }
    { const char *e = getenv("WOODY_OLW"); if (e) w *= (float)atof(e); }     /* test helper: scale the rim */
    Model *m = inst->model; const int32_t *own = model_owner(m); const Vec3 zero = { 0, 0, 0 };
    g_ol_n = 0;
    for (uint32_t ni = 0; ni < m->nnodes; ni++) {
        InsNode *n = &m->nodes[ni]; if (n->kind != 0 || !n->polys || n->type_code == 2) continue;
        if (n->type_code >= 5 && n->type_code <= 8) continue;                   /* 0x43bf65 throws the eyelid layer's back faces away */
        Vec3 cl; if (!affine_inv_apply(&inst->node_world[ni], g_cam_pos, &cl)) continue;
        for (uint32_t k = 0; k < n->npolys; k++) {
            InsPoly *p = &n->polys[k];
            if (p->nverts < 3 || (p->flags & 2) || (p->flags & 0x60)) continue; /* double sided and blended polygons never outline (0x43c0c2) */
            const float *pl = poly_plane(m, n, p);
            if (pl[0] * cl.x + pl[1] * cl.y + pl[2] * cl.z + pl[3] > 0) continue;   /* front facing: the model pass drew it */
            Vec3 v[3];
            for (uint32_t c = 0; c < p->nverts; c++) {
                Vec3 q = ol_vertex(inst, &inst->node_world[ni], &m->points[p->indices[c]], n->pivot, w);
                if (c == 0) v[0] = q; else { v[1] = v[2]; v[2] = q; if (c >= 2) ol_push(v[0], v[2], v[1]); }
            }
        }
    }
    for (uint32_t t = 0; t < m->ntris; t++) {
        InsTri *tr = &m->tris[t];
        uint32_t idx[3] = { tr->i0, tr->i1, tr->i2 }; Vec3 wp[3]; const Mat4 *MM[3]; Vec3 pv[3];
        for (int c = 0; c < 3; c++) {
            int o = own[idx[c]]; pv[c] = o >= 0 ? m->nodes[o].pivot : zero;
            Vec3 lp = { m->points[idx[c]].pos.x - pv[c].x, m->points[idx[c]].pos.y - pv[c].y, m->points[idx[c]].pos.z - pv[c].z };
            MM[c] = o >= 0 ? &inst->node_world[o] : &inst->world; wp[c] = mat4_apply(MM[c], lp);
        }
        float ux = wp[0].x - wp[1].x, uy = wp[0].y - wp[1].y, uz = wp[0].z - wp[1].z;
        float vx = wp[0].x - wp[2].x, vy = wp[0].y - wp[2].y, vz = wp[0].z - wp[2].z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (nx * (g_cam_pos.x - wp[0].x) + ny * (g_cam_pos.y - wp[0].y) + nz * (g_cam_pos.z - wp[0].z) > 0) continue;   /* front facing */
        Vec3 e[3];
        for (int c = 0; c < 3; c++) e[c] = ol_vertex(inst, MM[c], &m->points[idx[c]], pv[c], w);
        ol_push(e[0], e[2], e[1]);
    }
    if (g_shlog) printf("  OL inst %u setflags %x w %.2f tris %u fade %.2f", inst->index, inst->setflags, w, g_ol_n, inst->fade), puts("");
    if (!g_ol_n) return;
    bt_flush();
    float a = 2.0f * (1.0f - inst->fade); if (a > 1) a = 1;                     /* 0x43ece9: twice the opacity, clamped */
    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (a < 0.999f) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); } else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
    glColor4f(0, 0, 0, a);
    glEnableClientState(GL_VERTEX_ARRAY);                                      /* bt_flush() leaves it disabled */
    glVertexPointer(3, GL_FLOAT, 0, g_ol); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_ol_n * 3);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColor4f(1, 1, 1, 1); glDepthMask(GL_TRUE); glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST);
    glEnableClientState(GL_COLOR_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_BLEND);
    g_last_material = 0xffffffffu;
}

int rnd_project(const Window *w, const FreeCamera *cam, Vec3 p, float *sx, float *sy)
{
    Vec3 fw = cam_forward(cam), rt = cam_right(cam);
    Vec3 up = { rt.y * fw.z - rt.z * fw.y, rt.z * fw.x - rt.x * fw.z, rt.x * fw.y - rt.y * fw.x };
    Vec3 d = { p.x - cam->pos.x, p.y - cam->pos.y, p.z - cam->pos.z };
    float ex = d.x * rt.x + d.y * rt.y + d.z * rt.z, ey = d.x * up.x + d.y * up.y + d.z * up.z, ez = d.x * fw.x + d.y * fw.y + d.z * fw.z;
    if (ez <= 0.001f || !w->width || !w->height) return 0;
    float aspect = (float)w->width / (float)w->height;
    int vpy = 0, vph = w->height;
    if (cam->letterbox) { vpy = (int)(w->height * (cam->letterbox == 1 ? 0.125f : 0.1875f)); vph = (int)(w->height * 0.75f); aspect /= 0.75f; }
    float f = 1.0f / tanf(cam->fov_deg * 3.14159265f / 360.0f);
    float ndx = f / aspect * ex / ez, ndy = f * ey / ez;
    if (ndx < -1 || ndx > 1 || ndy < -1 || ndy > 1) return 0;           /* the four side planes; there is no near/far test */
    *sx = (ndx * 0.5f + 0.5f) * 640.0f;
    *sy = (w->height - (vpy + (ndy * 0.5f + 0.5f) * vph)) * 480.0f / w->height;
    return 1;
}

void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s)
{
    glViewport(0, 0, w->width, w->height);
    g_tex_now = time_s; g_cam_pos = cam->pos;
    for (uint32_t g = 0; g < r->tex->ngroups; g++) {                 /* texture animation: frame_count frames over anim_duration seconds */
        TexGroup *tg = &r->tex->groups[g];
        if (tg->frame_count > 1 && tg->anim_duration > 0 && ((tg->flags >> 8) & 0xff) != 2) tg->gl_tex = tg->gl_frames[(uint32_t)(time_s / tg->anim_duration * tg->frame_count) % tg->frame_count];
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glClearColor(0.08f, 0.09f, 0.11f, 1); glClearStencil(0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    static double T[5]; static int TN; double q0 = win_time();
    {   /* pose every visible instance up front: the cast shadows are drawn inside the world passes */
        float dt = time_s - r->last_time; if (dt < 0 || dt > 0.25f) dt = 0.016f; r->last_time = time_s;
        for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) { Model *m = &r->ins->models[mi]; for (uint32_t k = 0; k < m->ninstances; k++) {
            Instance *inst = &m->instances[k]; if (!inst->visible || inst->fade > 0.98f) continue;
            ins_pose(inst, inst->anim, inst->anim_time); if (r->lit) instance_light(r, inst, dt); } }
    }
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GEQUAL, 127.0f / 255.0f);   /* 0x47ec50/0x47ec5c: ALPHAREF 0x7f, GREATEREQUAL. The device stays on CULL_NONE; the culling is per polygon on the CPU */
    glPolygonMode(GL_FRONT_AND_BACK, r->wireframe ? GL_LINE : GL_FILL);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    double q1 = win_time(); T[0] += q1 - q0;

    /* projection: the world data is right-handed (3ds Max export, y up after the -90 deg x instance rotation), so a plain GL frustum */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    float aspect = w->height ? (float)w->width / (float)w->height : 1.333f, zn = 5.0f, zf = 200000.0f;
    if (cam->letterbox) {                     /* image strip y = 30..390 of 480: black above (30) and below (90), docs/CAMERA_SCRIPT.md 2.4 */
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, (int)(w->height * (cam->letterbox == 1 ? 0.125f : 0.1875f)), w->width, (int)(w->height * 0.75f)); aspect /= 0.75f;   /* 1 = centred (cinematics, 0x41f8d0), 2 = shifted up (mode 4) */
    }
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
    if (r->have_sky && r->show_world) {                                             /* 0x42ad40..0x42b373: five quads of a cube around the camera, white, unlit, drawn behind everything */
        static const signed char q[5][4][3] = {
            { {-1,-1, 1}, {-1, 1, 1}, { 1, 1, 1}, { 1,-1, 1} }, { { 1,-1, 1}, { 1, 1, 1}, { 1, 1,-1}, { 1,-1,-1} },
            { { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1}, {-1,-1,-1} }, { {-1,-1,-1}, {-1, 1,-1}, {-1, 1, 1}, {-1,-1, 1} },
            { {-1, 1, 1}, {-1, 1,-1}, { 1, 1,-1}, { 1, 1, 1} } };
        const float S = 50000.0f, hu = r->sky_hu, hv = r->sky_hv, uv[4][2] = { { hu, hv }, { hu, 1 - hv }, { 1 - hu, 1 - hv }, { 1 - hu, hv } };
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D); glColor3f(1, 1, 1);
        for (int f = 0; f < 5; f++) {
            glBindTexture(GL_TEXTURE_2D, r->sky_tex[f]);
            glBegin(GL_QUADS);
            for (int c = 0; c < 4; c++) { glTexCoord2f(uv[c][0], uv[c][1]); glVertex3f(cam->pos.x + q[f][c][0] * S, cam->pos.y + q[f][c][1] * S, cam->pos.z + q[f][c][2] * S); }
            glEnd();
        }
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    }

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
        if (pass == 0 && r->lit) {
            /* 1. ambient fill: flat 0x4C4C4C, opaque, writes z */
            glDisable(GL_TEXTURE_2D); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisable(GL_ALPHA_TEST);
            if (r->show_light) glColor3f(LIT_AMB, LIT_AMB, LIT_AMB); else glColor3f(1, 1, 1);
            for (uint32_t i = 0; i < r->nbatches; i++) { struct WorldBatch *b = &r->litb[i]; if (!b->ntris) continue; glVertexPointer(3, GL_FLOAT, 0, b->pos); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b->ntris * 3); }
            glEnableClientState(GL_COLOR_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glDepthMask(GL_FALSE); glDepthFunc(GL_LEQUAL); glEnable(GL_BLEND);
            /* 2. light polygons, additive (flush 0x4293f0) */
            if (r->show_light) {
                glBlendFunc(GL_ONE, GL_ONE); glEnable(GL_TEXTURE_2D); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);
                for (int t = 0; t < 16; t++) {
                    struct WorldBatch *b = &r->lightb[t]; if (!b->ntris) continue;
                    glBindTexture(GL_TEXTURE_2D, r->light_tex[t]);
                    glVertexPointer(3, GL_FLOAT, 0, b->pos); glTexCoordPointer(2, GL_FLOAT, 0, b->uv); glColorPointer(3, GL_UNSIGNED_BYTE, 0, b->col);
                    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b->ntris * 3);
                }
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            { double a = win_time(); if (r->show_light && r->show_instances) draw_cast_shadows(r); T[2] += win_time() - a; }
            /* 3. texture pass: 2 * src * dst (0x4296a4) */
            glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
            for (uint32_t i = 0; i < r->nbatches; i++) {
                struct WorldBatch *b = &r->litb[i]; if (!b->ntris) continue;
                if (b->group < r->tex->ngroups) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, r->tex->groups[b->group].gl_tex); } else glDisable(GL_TEXTURE_2D);
                glVertexPointer(3, GL_FLOAT, 0, b->pos); glTexCoordPointer(2, GL_FLOAT, 0, b->uv); glColorPointer(3, GL_UNSIGNED_BYTE, 0, b->col);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b->ntris * 3);
            }
            glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS); glEnable(GL_ALPHA_TEST);
        }
        glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    if (r->show_instances) {
        double a = win_time(); g_last_material = 0xffffffffu;
        for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) {
            Model *m = &r->ins->models[mi];
            for (uint32_t k = 0; k < m->ninstances; k++) {
                Instance *inst = &m->instances[k]; if (!inst->visible || inst->fade > 0.98f) continue;   /* 0x42e374 */
                {   /* frustum cull on a bounding sphere (the original only draws the instances of the visible sectors) */
                    if (m->cull_r <= 0) { float mx = 1; for (uint32_t pi = 0; pi < m->npoints; pi++) { const Vec3 *q = &m->points[pi].pos; float d2 = q->x * q->x + q->y * q->y + q->z * q->z; if (d2 > mx) mx = d2; } m->cull_r = sqrtf(mx) * 1.5f + 50.0f; }
                    const float *wm = inst->node_world ? inst->node_world[0].m : inst->world.m;
                    float sx = sqrtf(wm[0] * wm[0] + wm[1] * wm[1] + wm[2] * wm[2]); if (sx < 1) sx = 1;
                    float R = m->cull_r * sx, dx = wm[12] - cam->pos.x, dy = wm[13] - cam->pos.y, dz = wm[14] - cam->pos.z;
                    float vz = dx * fw.x + dy * fw.y + dz * fw.z, vx = dx * rt.x + dy * rt.y + dz * rt.z, vy = dx * up.x + dy * up.y + dz * up.z;
                    float ty = 1.0f / f, tx = ty * aspect, zz = vz + R;
                    if (zz < 0 || fabsf(vx) > zz * tx + R * 1.5f || fabsf(vy) > zz * ty + R * 1.5f) continue;
                }
                { static double mt[512]; static int mn; double b0 = win_time(); draw_instance(r, inst, pass); if (pass == 0) draw_outline(r, inst); if (mi < 512) mt[mi] += win_time() - b0; if (getenv("WOODY_PROF2") && pass == 1 && mi == r->ins->nmodels - 1 && k == m->ninstances - 1 && ++mn == 120) { for (uint32_t z = 0; z < r->ins->nmodels && z < 512; z++) if (mt[z] / 120 * 1000 > 0.3) { printf("   model %u: %.2f ms (%u nodes, %u tris, %u inst)", z, mt[z] / 120 * 1000, r->ins->models[z].nnodes, r->ins->models[z].ntris, r->ins->models[z].ninstances); puts(""); } } }
            }
        }
        bt_flush(); g_last_material = 0xffffffffu;
        T[3] += win_time() - a;
    }
    }
    set_blend(0);
    T[4] += win_time() - q0;
    if (getenv("WOODY_PROF") && ++TN == 60) { printf("  RND ms: pose %.2f shadows %.2f instances %.2f total %.2f", T[0] / 60 * 1000, T[2] / 60 * 1000, T[3] / 60 * 1000, T[4] / 60 * 1000); puts(""); T[0] = T[2] = T[3] = T[4] = 0; TN = 0; }
    (void)time_s;
}

void rnd_set_sky(Renderer *r, const uint32_t tex[5]) { if (r->have_sky) for (int f = 0; f < 5; f++) if (tex[f]) r->sky_tex[f] = tex[f]; }
