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

void rnd_free(Renderer *r) { for (uint32_t i = 0; i < r->nbatches; i++) { free(r->batches[i].pos); free(r->batches[i].uv); free(r->batches[i].col); free(r->litb[i].pos); free(r->litb[i].uv); free(r->litb[i].col); } free(r->batches); free(r->litb); }

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
static void set_material(const Renderer *r, uint32_t material, const Material **mat_out, uint32_t frame)   /* frame: models never auto-cycle (0x47f290) */
{
    *mat_out = NULL; g_mat_scale = 1.0f;
    if (material & 0x8000) {
        float rgb[3]; argb1555_to_rgb(material, rgb); glDisable(GL_TEXTURE_2D); set_blend(0); glColor3f(rgb[0], rgb[1], rgb[2]);
    } else if (material < r->tex->nmaterials) {
        const Material *m = &r->tex->materials[material]; *mat_out = m; const TexGroup *g = &r->tex->groups[m->group];
        int bl = (g->flags & 2) != 0; set_blend(bl); if (bl) g_mat_scale = ((g->flags >> 16) & 0xff) / 255.0f;
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, g->gl_frames[frame < g->frame_count ? frame : 0]); glColor3f(g_mat_scale, g_mat_scale, g_mat_scale);
    } else { glDisable(GL_TEXTURE_2D); set_blend(0); glColor3f(1, 0, 1); }
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
static void instance_light(const Renderer *r, Instance *inst, float dt)
{
    const LitFile *lf = r->lit; Vec3 p = { inst->world.m[12], inst->world.m[13] + 20.0f, inst->world.m[14] };
    int chosen = -1, fallback = -1; float fb_d = 1e30f, cd = 0;
    for (uint32_t l = 0; l < lf->nlights; l++) {
        const LitLight *L = &lf->lights[l]; float dx = L->pos.x - p.x, dy = L->pos.y - p.y, dz = L->pos.z - p.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < L->range && lit_point_lit(L, r->gel, p)) { chosen = (int)l; cd = d; break; }
        if (d / L->range < fb_d) { fb_d = d / L->range; fallback = (int)l; }
    }
    float keep = powf(0.85f, dt * 60.0f); if (!inst->l_init) keep = 0;          /* Ldir *= 0.85 per frame [0x4aa3d8] */
    inst->ldir.x *= keep; inst->ldir.y *= keep; inst->ldir.z *= keep;
    inst->l_seen = chosen >= 0; inst->light = chosen >= 0 ? chosen : fallback; inst->l_init = 1;
    if (chosen >= 0) {
        const LitLight *L = &lf->lights[chosen]; float k = (1.0f - keep) * (1.0f - cd / L->range) / (cd > 1e-3f ? cd : 1.0f);
        inst->ldir.x += (L->pos.x - p.x) * k; inst->ldir.y += (L->pos.y - p.y) * k; inst->ldir.z += (L->pos.z - p.z) * k;
    }
    if (inst->light >= 0) for (int q = 0; q < 3; q++) inst->lcol[q] = lf->lights[inst->light].colour[q];
}
/* vertex colour: vcol * 0.3 + max(0, N.Ldir) * C, drawn MODULATE2X */
static void lit_vertex_colour(const Renderer *r, const Instance *inst, const Mat4 *M, const InsPoint *pt, const float base[3])
{
    if (!r->lit || !r->show_light) { glColor3f(base[0] * pt->colour.x / 128.0f * g_mat_scale, base[1] * pt->colour.y / 128.0f * g_mat_scale, base[2] * pt->colour.z / 128.0f * g_mat_scale); return; }
    const float *a = M->m; Vec3 n = pt->normal;
    Vec3 w = { a[0] * n.x + a[4] * n.y + a[8] * n.z, a[1] * n.x + a[5] * n.y + a[9] * n.z, a[2] * n.x + a[6] * n.y + a[10] * n.z };
    float l = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z), ndl = l > 1e-6f ? (w.x * inst->ldir.x + w.y * inst->ldir.y + w.z * inst->ldir.z) / l : 0; if (ndl < 0) ndl = 0;
    float vc[3] = { pt->colour.x, pt->colour.y, pt->colour.z }, c[3];
    for (int q = 0; q < 3; q++) { c[q] = (vc[q] * 0.6f + 2.0f * ndl * inst->lcol[q]) / 255.0f; if (c[q] > 1) c[q] = 1; c[q] *= base[q] * g_mat_scale; }
    glColor3f(c[0], c[1], c[2]);
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
    for (int list = 0; list < 2; list++) {
        const uint32_t *faces = list ? L->b : L->a; uint32_t nf = list ? L->nb : L->na;
        for (uint32_t fi = 0; fi < nf; fi++) {
            uint32_t f = faces[fi]; if (f >= r->gel->npolys) continue;
            const GelPoly *gp = &r->gel->polys[f]; const float *pl = gp->plane, *fb = &r->face_bound[4 * f]; if (gp->nverts < 3) continue;
            float dl = pl[0] * L->pos.x + pl[1] * L->pos.y + pl[2] * L->pos.z + pl[3], dc = pl[0] * c[0] + pl[1] * c[1] + pl[2] * c[2] + pl[3];
            if (dl <= 1.0f || dc >= dl || dc < -rad) continue;                 /* caster must be between the light and the plane */
            float den = dl - dc; if (den < 1.0f) continue;
            float s = dl / den; if (s > 40.0f) continue;                      /* projection scale; huge = grazing */
            float pc[3] = { L->pos.x + (c[0] - L->pos.x) * s, L->pos.y + (c[1] - L->pos.y) * s, L->pos.z + (c[2] - L->pos.z) * s };
            float dx = pc[0] - fb[0], dy = pc[1] - fb[1], dz = pc[2] - fb[2], reach = rad * s * 1.5f + fb[3];
            if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
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
            glEnable(GL_DEPTH_TEST); glColorMask(0, 0, 0, 0); glStencilFunc(GL_ALWAYS, 0, 1); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glBegin(GL_TRIANGLE_FAN); for (uint32_t k = 0; k < gp->nverts; k++) { const GelVert *v = &r->gel->verts[gp->indices[k]]; glVertex3f(v->x, v->y, v->z); } glEnd();
            glColorMask(1, 1, 1, 1);
        }
    }
}
static void draw_cast_shadows(const Renderer *r)
{
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_STENCIL_TEST); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f); glColor3f(LIT_AMB, LIT_AMB, LIT_AMB);
    for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) {
        Model *m = &r->ins->models[mi];
        for (uint32_t k = 0; k < m->ninstances; k++) {
            Instance *inst = &m->instances[k];
            int caster = (mi == 0 && k == 0) || (inst->setflags & 1) || (inst->type >= 4 && inst->type <= 13);   /* the player, SetFlags bit 1 (0x42b3cc); enemies are our addition */
            if (!caster || !inst->visible || inst->fade > 0.01f || !inst->l_seen || inst->light < 0 || !inst->node_world) continue;
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
static void draw_node_polys(const Renderer *r, Instance *inst, uint32_t ni, int pass, uint32_t frame, int helper)
{
    Model *m = inst->model; InsNode *n = &m->nodes[ni]; const Material *mat;
    for (uint32_t k = 0; k < n->npolys; k++) {
        InsPoly *p = &n->polys[k]; if (p->nverts < 3 || mat_blended(r, p->material) != pass) continue;
        set_material(r, p->material, &mat, frame);
        glBegin(GL_TRIANGLE_FAN);
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
                glTexCoord2f(u, v);
            }
            { float base[3] = { 1, 1, 1 }; if (!mat && (p->material & 0x8000)) argb1555_to_rgb(p->material, base); if (mat || (p->material & 0x8000)) lit_vertex_colour(r, inst, &inst->node_world[ni], pt, base); }
            glVertex3f(wp.x, wp.y, wp.z);
        }
        glEnd();
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
        if (lid) { glDepthFunc(GL_LEQUAL); draw_node_polys(r, inst, ni, pass, lid, -1); glDepthFunc(GL_LESS); }   /* eyelid layer on top of the eyeball */
    }
    /* skinned triangles */
    if (m->ntris) {
        uint32_t last = 0xffffffff; const int32_t *own = model_owner(m); float base[3] = { 1, 1, 1 };
        glBegin(GL_TRIANGLES);
        for (uint32_t t = 0; t < m->ntris; t++) {
            InsTri *tr = &m->tris[t]; if (mat_blended(r, tr->material) != pass) continue;
            if (tr->material != last) { glEnd(); set_material(r, tr->material, &mat, 0); last = tr->material; base[0] = base[1] = base[2] = 1; if (tr->material & 0x8000) argb1555_to_rgb(tr->material, base); glBegin(GL_TRIANGLES); }
            uint32_t idx[3] = { tr->i0, tr->i1, tr->i2 };
            for (int c = 0; c < 3; c++) {
                int o = own[idx[c]]; InsPoint *pt = &m->points[idx[c]]; Vec3 lp = pt->pos; if (o >= 0) { lp.x -= m->nodes[o].pivot.x; lp.y -= m->nodes[o].pivot.y; lp.z -= m->nodes[o].pivot.z; }
                const Mat4 *M = o >= 0 ? &inst->node_world[o] : &inst->world; Vec3 wp = mat4_apply(M, lp);
                if (mat || (tr->material & 0x8000)) lit_vertex_colour(r, inst, M, pt, base);
                if (mat) glTexCoord2f(mat->m[6 - 3 * c], mat->m[7 - 3 * c]);   /* explicit UVs: the material holds three UV pairs, file vertex j = (m[3j], m[3j+1]) and i0 is the third file vertex (0x43e39a) */
                glVertex3f(wp.x, wp.y, wp.z);
            }
        }
        glEnd();
    }
}

void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s)
{
    glViewport(0, 0, w->width, w->height);
    for (uint32_t g = 0; g < r->tex->ngroups; g++) {                 /* texture animation: frame_count frames over anim_duration seconds */
        TexGroup *tg = &r->tex->groups[g];
        if (tg->frame_count > 1 && tg->anim_duration > 0) tg->gl_tex = tg->gl_frames[(uint32_t)(time_s / tg->anim_duration * tg->frame_count) % tg->frame_count];
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glClearColor(0.08f, 0.09f, 0.11f, 1); glClearStencil(0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    {   /* pose every visible instance up front: the cast shadows are drawn inside the world passes */
        float dt = time_s - r->last_time; if (dt < 0 || dt > 0.25f) dt = 0.016f; r->last_time = time_s;
        for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) { Model *m = &r->ins->models[mi]; for (uint32_t k = 0; k < m->ninstances; k++) {
            Instance *inst = &m->instances[k]; if (!inst->visible || inst->fade > 0.98f) continue;
            ins_pose(inst, inst->anim, inst->anim_time); if (r->lit) instance_light(r, inst, dt); } }
    }
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
            if (r->show_light && r->show_instances) draw_cast_shadows(r);
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
        for (uint32_t mi = 0; mi < r->ins->nmodels; mi++) {
            Model *m = &r->ins->models[mi];
            for (uint32_t k = 0; k < m->ninstances; k++) {
                Instance *inst = &m->instances[k]; if (!inst->visible || inst->fade > 0.98f) continue;   /* 0x42e374 */
                draw_instance(r, inst, pass);
            }
        }
    }
    }
    set_blend(0);
    (void)time_s;
}
