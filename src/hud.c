/* hud.c - 2D layer: font (rck type 3), strings (type 2), HUD sprites (bank 0 images 61..64), text box 1080.
 * Everything in 640x480 virtual coordinates, origin top left. Colours are 0xAARRGGBB with RGB 0x80 = 1.0 (docs/HUD_TEXT.md 5.2). */
#include "hud.h"
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

typedef struct { float w, adv; uint16_t x, y, wpx, hpx, page, junk; } Glyph;

static struct {
    int ok;
    GLuint img[4]; int img_w[4], img_h[4];                /* bank 0 images 61..64 */
    uint32_t nglyphs, npages, psize; float H, B, M; Glyph *gl; GLuint page[8];
    uint16_t **str; int nstr;                             /* bank 0 strings, 0-terminated u16 codes */
    float k;                                              /* current glyph scale = size / (H - B) */
    float blink;
    GLuint sky[5]; int nlevel_img;                        /* level bank images 0..4 in file row order (sky cube) */
    GLuint fx[11];                                        /* bank 0 images 0, 4, 6: ribbon, flash, bolt (docs/PROJECTILES.md); 5, 10, 11: glow and the two death stars (docs/PERSO_DEATH.md 7); 12, 14, 31, 32: explosion flash, smoke, flame, exhaust glow, shared by the rocket (docs/ROCKET.md 5) and the missiles (docs/PROJECTILES.md 5.3); slot 10 = the footstep mark (docs/FOOTSTEPS.md) */
    GLuint beam;                                          /* bank 0 image 1: the line texture */
    GLuint bonus[5]; float sr[3], su[3];                  /* bank 0 images 19, 21, 20, 46, 23 (jump table 0x479654) */
    GLuint env[4];                                        /* bank 0 images 53..56: the butterflies of the environment instances (0x47e050 picks one of the four) */
    GLuint logo; int logo_w, logo_h; float logo_v, menu_t;   /* level bank image 1 (the title logo in House.rck); fade value 0..5 */
    struct { int state, n; float t, size; uint32_t id[3]; float x[3], y[3]; float rect[4]; } box;
} H;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* walks an RKET bank (docs/RCK.md): calls cb(type, index, payload, size) for the wanted types; payloads of other types are skipped */
typedef void (*ItemCb)(int type, int index, const uint8_t *data, uint32_t size);
static int rck_walk(const char *path, unsigned want_mask, ItemCb cb)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    uint8_t h[0x38];
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "RKET", 4)) { fclose(f); return -1; }
    for (int type = 0; type < 4; type++) {
        uint32_t count = rd32(h + 8 + 0x18 + type * 4);
        for (uint32_t i = 0; i < count; i++) {
            uint8_t ih[8]; if (fread(ih, 1, 8, f) != 8) { fclose(f); return -1; }
            uint32_t size = rd32(ih);
            if (want_mask & (1u << type)) {
                uint8_t *d = malloc(size ? size : 1);
                if (fread(d, 1, size, f) != size) { free(d); fclose(f); return -1; }
                cb(type, (int)i, d, size); free(d);
            } else fseek(f, (long)size, SEEK_CUR);
        }
    }
    fclose(f); return 0;
}

static GLuint upload(const uint8_t *rgba, int w, int h)
{
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

/* which bank 0 image the footstep mark uses. 0x47cba0 is not decompiled, so its image is unknown: the port takes
 * the soft cloud (image 14) and WOODY_STEPIMG=<n> tries another one (docs/FOOTSTEPS.md 4). */
int hud_step_image(void) { static int v = -1; if (v < 0) { const char *e = getenv("WOODY_STEPIMG"); v = e ? atoi(e) : 14; if (v < 0) v = 14; } return v; }
static int fx_slot(int image) { return image == 0 ? 0 : image == 4 ? 1 : image == 6 ? 2 : image == 5 ? 3 : image == 10 ? 4 : image == 11 ? 5 : image == 12 ? 6 : image == 14 ? 7 : image == 31 ? 8 : image == 32 ? 9 : image == hud_step_image() ? 10 : -1; }
static void common_item(int type, int index, const uint8_t *d, uint32_t size)
{
    static const int bonus_img[5] = { 19, 21, 20, 46, 23 };
    if (type == 1 && (index == 0 || index == 4 || index == 6) && getenv("WOODY_FXLOG") && size >= 8) { int w = d[0] | d[1] << 8, h = d[2] | d[3] << 8; unsigned long sum = 0, sa = 0; for (int i = 0; i < w * h; i++) { sum += d[8 + i * 4] + d[9 + i * 4] + d[10 + i * 4]; sa += d[11 + i * 4]; } printf("fx image %d: %dx%d bpp %d mean rgb %.1f mean a %.1f", index, w, h, d[4], sum / (3.0 * w * h), sa / (1.0 * w * h)), puts(""); }
    if (type == 1 && fx_slot(index) >= 0) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.fx[fx_slot(index)] = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1 && index == 1) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.beam = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1 && index >= 53 && index <= 56) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.env[index - 53] = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1) for (int b = 0; b < 5; b++) if (index == bonus_img[b]) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.bonus[b] = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1 && index >= 61 && index <= 64 && size >= 8) {      /* i16 w, h; u16 bpp, alpha; BGRA, bottom row first (0x480780) */
        int w = (int16_t)(d[0] | d[1] << 8), h = (int16_t)(d[2] | d[3] << 8), bpp = d[4] | d[5] << 8;
        if (w <= 0 || h <= 0 || size < 8 + (uint32_t)w * h * 4) return;
        uint8_t *px = malloc((size_t)w * h * 4);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            const uint8_t *s = d + 8 + ((size_t)(h - 1 - y) * w + x) * 4; uint8_t *o = px + ((size_t)y * w + x) * 4;
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = bpp == 24 ? 255 : s[3];
        }
        H.img[index - 61] = upload(px, w, h); H.img_w[index - 61] = w; H.img_h[index - 61] = h; free(px);
    } else if (type == 2) {
        H.str = realloc(H.str, (size_t)(index + 1) * sizeof *H.str); H.nstr = index + 1;
        uint32_t n = size / 2; uint16_t *s = calloc(n + 1, 2); memcpy(s, d, (size_t)n * 2); H.str[index] = s;
    }
}

static void common_item(int type, int index, const uint8_t *d, uint32_t size);
static void level_item(int type, int index, const uint8_t *d, uint32_t size)
{
    if (type == 1) { H.nlevel_img = index + 1; }
    if (type == 1 && index < 5 && size >= 8) {                       /* raw row order: file row 0 = v 0 = bottom of the cube face (docs/SKY.md) */
        int w = (int16_t)(d[0] | d[1] << 8), h = (int16_t)(d[2] | d[3] << 8);
        if (w > 0 && h > 0 && size >= 8 + (uint32_t)w * h * 4) {
            uint8_t *px = malloc((size_t)w * h * 4);
            for (int i = 0; i < w * h; i++) { px[i * 4] = d[8 + i * 4 + 2]; px[i * 4 + 1] = d[8 + i * 4 + 1]; px[i * 4 + 2] = d[8 + i * 4]; px[i * 4 + 3] = 255; }
            H.sky[index] = upload(px, w, h); free(px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }
    }
    if (type == 1 && index == 1) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.logo = H.img[0]; H.logo_w = H.img_w[0]; H.logo_h = H.img_h[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type != 3 || index != 0 || size < 0x1c) return;              /* font 0x01030000 (0x43f9a0) */
    H.nglyphs = rd32(d); H.npages = rd32(d + 4); H.psize = rd32(d + 8);
    memcpy(&H.H, d + 0xc, 4); memcpy(&H.B, d + 0x14, 4); memcpy(&H.M, d + 0x18, 4);
    if (H.npages > 8 || size < 0x1c + H.nglyphs * 20 + H.npages * H.psize * H.psize * 4) { H.nglyphs = 0; return; }
    H.gl = malloc(H.nglyphs * sizeof *H.gl); memcpy(H.gl, d + 0x1c, H.nglyphs * 20);
    const uint8_t *p = d + 0x1c + H.nglyphs * 20;
    for (uint32_t i = 0; i < H.npages; i++, p += H.psize * H.psize * 4) H.page[i] = upload(p, (int)H.psize, (int)H.psize);   /* RGBA, top row first */
}

int hud_load(const char *common_rck, const char *level_rck)
{
    hud_free();
    if (rck_walk(common_rck, 6, common_item)) return -1;
    if (rck_walk(level_rck, 8 | 2, level_item) || !H.nglyphs) return -1;
    H.ok = 1; H.k = 17.0f / (H.H - H.B);
    return 0;
}

void hud_free(void)
{
    for (int i = 0; i < 4; i++) if (H.img[i]) glDeleteTextures(1, &H.img[i]);
    for (int i = 0; i < 8; i++) if (H.page[i]) glDeleteTextures(1, &H.page[i]);
    if (H.logo) glDeleteTextures(1, &H.logo);
    for (int i = 0; i < 5; i++) if (H.sky[i]) glDeleteTextures(1, &H.sky[i]);
    for (int i = 0; i < 5; i++) if (H.bonus[i]) glDeleteTextures(1, &H.bonus[i]);
    for (int i = 0; i < 4; i++) if (H.env[i]) glDeleteTextures(1, &H.env[i]);
    if (H.beam) glDeleteTextures(1, &H.beam);
    for (int i = 0; i < 11; i++) if (H.fx[i]) glDeleteTextures(1, &H.fx[i]);
    for (int i = 0; i < H.nstr; i++) free(H.str[i]);
    free(H.str); free(H.gl); memset(&H, 0, sizeof H);
}

/* ---------------------------------------------------------------- drawing primitives */
static void set_col(uint32_t c)
{
    float r = ((c >> 16) & 255) / 128.0f, g = ((c >> 8) & 255) / 128.0f, b = (c & 255) / 128.0f;
    glColor4f(r > 1 ? 1 : r, g > 1 ? 1 : g, b > 1 ? 1 : b, (c >> 24) / 255.0f);
}

/* RectVirtual 0x480a10: corner colours top left, bottom left, bottom right, top right; tex 0 = blank */
static void quad(float x, float y, float w, float h, GLuint tex, float u0, float v0, float u1, float v1, uint32_t tl, uint32_t bl, uint32_t br, uint32_t tr)
{
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    set_col(tl); glTexCoord2f(u0, v0); glVertex2f(x, y);
    set_col(bl); glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    set_col(br); glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    set_col(tr); glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glEnd();
}

static const struct { float x, y, w, h; int img; } k_spr[16] = {     /* table 0x4ab658 */
    {0,0,64,64,0}, {63,0,64,64,0}, {0,63,64,64,0}, {63,63,64,64,0}, {0,0,94,94,1}, {0,0,94,94,2}, {0,0,63,90,3}, {0,90,23,23,3},
    {27,90,34,34,3}, {97,103,29,23,3}, {5,116,123,12,1}, {5,94,123,22,1}, {63,0,64,64,3}, {111,111,16,16,2}, {97,84,19,15,3}, {0,96,31,31,2},
};
static void sprite_part(int n, float x, float y, float wcrop, uint32_t cl, uint32_t cr)   /* 0x460480, scale 1 */
{
    int i = k_spr[n].img; if (!H.img[i]) return;
    float W = (float)H.img_w[i], Hh = (float)H.img_h[i], w = wcrop < k_spr[n].w ? wcrop : k_spr[n].w;
    quad(x, y, w, k_spr[n].h, H.img[i], k_spr[n].x / W, k_spr[n].y / Hh, (k_spr[n].x + w) / W, (k_spr[n].y + k_spr[n].h) / Hh, cl, cl, cr, cr);
}
static void sprite(int n, float x, float y) { sprite_part(n, x, y, 1e9f, 0xfe808080, 0xfe808080); }

static void font_size(float s) { H.k = s / (H.H - H.B); }                          /* SetSize 0x441a60 */
static float font_cell(void) { return (H.H + 2 * (H.M < 0 ? -H.M : H.M)) * H.k; }  /* 0x441980: 62k */
static float font_measure(const uint16_t *s)                                       /* 0x441b30 (width of the last line) */
{
    float w = 0;
    for (; *s; s++) { if (*s == 4000) w = 0; else if (*s <= H.nglyphs) w += H.gl[*s - 1].adv * H.k; }
    return w;
}
static void font_draw(float x, float y, const uint16_t *s, uint32_t col)           /* 0x43f890; y = top of the cell */
{
    float x0 = x;
    for (; *s; s++) {
        if (*s == 4000) { x = x0; y += H.H * H.k; continue; }
        if (*s > H.nglyphs) continue;
        const Glyph *g = &H.gl[*s - 1]; float P = (float)H.psize;
        if (g->page < H.npages) quad(x, y, g->w * H.k, g->hpx * H.k, H.page[g->page], g->x / P, g->y / P, (g->x + g->wpx) / P, (g->y + g->hpx) / P, col, col, col, col);
        x += g->adv * H.k;
    }
}
static const uint16_t *hud_string(uint32_t ref) { uint32_t i = ref & 0xffff; return (ref >> 24) == 0 && (int)i < H.nstr && H.str[i] ? H.str[i] : NULL; }

static int number_codes(uint16_t *out, int v)                                       /* 0x441820: digits through Common string 0 "0123456789" */
{
    char b[16]; int n = snprintf(b, sizeof b, "%d", v < 0 ? 0 : v); const uint16_t *dig = hud_string(0);
    for (int i = 0; i < n; i++) out[i] = dig ? dig[b[i] - '0'] : (uint16_t)(b[i] - '0' + 1);
    out[n] = 0; return n;
}
static void number_sized(float cx, float cy, int v, float size)                     /* 0x4605b0 / 0x448660; the pop animations pass their own size */
{
    uint16_t s[16]; number_codes(s, v);
    font_size(v >= 100 ? size * 0.75f : size);
    font_draw(cx - font_measure(s) * 0.5f, cy - font_cell() * 0.5f, s, 0xfeff0000);
    font_size(17.0f);
}
static void number_centred(float cx, float cy, int v) { number_sized(cx, cy, v, 17.0f); }

/* ---------------------------------------------------------------- HUD animations (animator hud+0x30, docs/HUD_TEXT.md 4.6)
 * The five pickup flights (start 0x47b230, tick 0x47b4c0) with their trail (0x47b710), the number pop
 * (0x47c390 / 0x47c3d0), the growing plate and the sliding icon (0x47bf90 / 0x47c1a0), and the swarm of W's that
 * pays out 25 bonuses (0x47c5b0 / 0x47c620 / 0x47c7c0). Everything is linear in time and nothing ever fades: a
 * "pop" is a font size, a flight fades in by growing from zero. The animator draws on top of the static HUD
 * (0x447660 runs before 0x4480d0), and the static HUD leaves out whatever an animation has taken over. */
#define FLY_DUR   0.2f                                                /* PickupFly+0x4c, fixed in the ctor 0x47b170 */
#define SWARM_DUR 0.4f                                                /* one trip of one W, 0x47c760 */

static const float k_anchor[4][2] = { {32,82}, {66,197}, {66,282}, {600,86} };                           /* 0x5d7b50 = slot[2i+1] + 16 */
static const float k_slot[8][2] = { {16,16}, {16,66}, {16,136}, {50,181}, {16,221}, {50,266}, {544,30}, {584,70} };   /* 0x4b3a10 */

typedef struct { float x, y, size, t; int fresh, spawned; } SwarmW;
static struct {
    struct { int on, sprite, mode; float sx, sy, tx, ty, t, cur; } fly[6];    /* index = pickup kind 1..5 (0x448510) */
    struct { int on, phase, nph; float t, dur, s0, ds; } pop[4];              /* index = number anchor 0..3 */
    struct { int on, sprite; float cx, cy, w0, h0, dw, dh, t; } plate[3];     /* 1 = the $ plate, 2 = the charge plate */
    struct { int on, sprite; float x0, y0, dx, dy, t; } slide[3];
    struct { int on, to_life, count, popval, poprun, phase2; float counter, tx; SwarmW p[3]; } sw;
    struct { int mode; float t, life, x, y, w0, h0, vx, vy, dw, dh, len, tau, swing; } gh[128]; int ngh;
    int stage[3]; float hold[3];                                              /* kinds 2 and 3 run a 3-stage sequence with a 1.5 s hold */
    int latch;                                                                /* hud+0x10: the reward waits until the W pickup flight has landed */
    int mlives;                                                               /* hud+0x40: a life was lost (0x4622e0) */
    int prev_ok, prev_lives, prev_bonus; float prev_health;
} A;

void hud_anim_reset(void) { memset(&A, 0, sizeof A); }

static float frnd(void) { return (float)rand() / (float)RAND_MAX; }
static float costab(int k) { return (float)cos(6.2831853 * (k & 511) / 512.0); }   /* the engine's 512-entry table at [0x5e823c] */

/* a sprite into an explicit destination rect (0x480a10; 0x460480 is the same thing with a uniform scale) */
static void sprite_rect(int n, float x, float y, float w, float h)
{
    if (n < 0 || n >= 16 || w <= 0 || h <= 0) return;
    int i = k_spr[n].img; if (!H.img[i]) return;
    float W = (float)H.img_w[i], Hh = (float)H.img_h[i];
    quad(x, y, w, h, H.img[i], k_spr[n].x / W, k_spr[n].y / Hh, (k_spr[n].x + k_spr[n].w) / W, (k_spr[n].y + k_spr[n].h) / Hh,
         0xfe808080, 0xfe808080, 0xfe808080, 0xfe808080);
}
/* a trail blob: bank 0 image 4, additive (0x47bba0 submits it with flag 4) */
static void fx_rect(float x, float y, float w, float h)
{
    if (!H.fx[1] || w <= 0 || h <= 0) return;
    glBlendFunc(GL_ONE, GL_ONE);
    quad(x, y, w, h, H.fx[1], 0, 0, 1, 1, 0xfe808080, 0xfe808080, 0xfe808080, 0xfe808080);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* ---- the number pop 0x47c390 / 0x47c3d0: an odd phase grows s0 -> s1, an even one shrinks back; red, no alpha */
static void pop_start(int a, int nph, float s0, float s1, float dur)
{
    A.pop[a].on = 1; A.pop[a].phase = 1; A.pop[a].nph = nph; A.pop[a].t = 0; A.pop[a].dur = dur; A.pop[a].s0 = s0; A.pop[a].ds = s1 - s0;
}
static int pop_tick(int a, int value, float dt)
{
    float f = A.pop[a].t / A.pop[a].dur, size;                        /* the ratio from BEFORE this frame, as in 0x47c3d0 */
    if (A.pop[a].phase <= A.pop[a].nph) {
        A.pop[a].t += dt;
        if (A.pop[a].t < A.pop[a].dur) size = (A.pop[a].phase & 1) ? A.pop[a].s0 + f * A.pop[a].ds : A.pop[a].s0 + A.pop[a].ds - f * A.pop[a].ds;
        else { A.pop[a].phase++; A.pop[a].t = 0; size = (A.pop[a].phase & 1) ? A.pop[a].s0 : A.pop[a].s0 + A.pop[a].ds; }
    } else size = A.pop[a].s0;
    number_sized(k_anchor[a][0], k_anchor[a][1], value, size);
    return A.pop[a].on = A.pop[a].phase <= A.pop[a].nph;
}

/* ---- the flying icon 0x47b230 / 0x47b4c0 ------------------------------------------------------------------- */
static int fly_start(int kind, const float *screen, int sprite, int slot, int mode)
{
    if (!screen) return 0;                                            /* behind the camera or off screen: 0x47b230 refuses and only the pop plays */
    A.fly[kind].on = 1; A.fly[kind].sprite = sprite; A.fly[kind].mode = mode; A.fly[kind].t = 0; A.fly[kind].cur = 0;
    A.fly[kind].sx = screen[0]; A.fly[kind].sy = screen[1];
    A.fly[kind].tx = k_slot[slot][0]; A.fly[kind].ty = k_slot[slot][1];
    return 1;
}
static void fly_trail(int kind, float dx, float dy, float w, float h)  /* 0x47b710: ghosts along the same line at a fixed step in animation time */
{
    int mode = A.fly[kind].mode;
    float step = mode ? 0.005f : 0.02f;
    while (A.fly[kind].cur < A.fly[kind].t) {
        float ut = A.fly[kind].cur, u = ut / FLY_DUR, r;
        float gx = A.fly[kind].sx + dx * u, gy = A.fly[kind].sy + dy * u, gw = w * u, gh = h * u;
        if (A.ngh < 128) {
            int i = A.ngh++;
            A.gh[i].mode = mode; A.gh[i].t = A.fly[kind].t - ut;        /* born already this old */
            r = costab((int)(frnd() * 180.0f)) + 1.0f;
            A.gh[i].w0 = r * gw * 0.25f; A.gh[i].h0 = r * gh * 0.25f;
            if (!mode) {                                               /* 0x47b800: a blob beside the icon that shrinks away in 0.3 s */
                r = costab((int)(frnd() * 180.0f)); A.gh[i].x = dx < 0 ? gx + r * 10.0f + gw * 0.25f : gx - gw * 0.25f - r * 10.0f;
                r = costab((int)(frnd() * 180.0f)); A.gh[i].y = dy < 0 ? gy + r * 10.0f + gh * 0.25f : gy - gh * 0.25f - r * 10.0f;
                A.gh[i].vx = dx / FLY_DUR * 0.2f; A.gh[i].vy = dy / FLY_DUR * 0.2f;
                A.gh[i].life = 0.3f; A.gh[i].dw = A.gh[i].w0 / 0.3f; A.gh[i].dh = A.gh[i].h0 / 0.3f;
            } else {                                                   /* 0x47ba10: two glows winding 3.5 turns around the flight line, crawling at a fifth of its speed */
                float ox = A.fly[kind].sx + (dx >= 0 ? -2.0f * gw : 2.0f * gw), oy = A.fly[kind].sy + (dy >= 0 ? -2.0f * gh : 2.0f * gh);
                float Dx = A.fly[kind].sx + dx - ox, Dy = A.fly[kind].sy + dy - oy, L = (float)sqrt(Dx * Dx + Dy * Dy);
                if (L < 1e-3f) { A.ngh--; A.fly[kind].cur = ut + step; continue; }
                A.gh[i].x = ox; A.gh[i].y = oy; A.gh[i].vx = Dx / L; A.gh[i].vy = Dy / L;
                A.gh[i].len = L; A.gh[i].tau = 5.0f * ut; A.gh[i].swing = w; A.gh[i].life = 0.5f;
            }
        }
        A.fly[kind].cur = ut + step;
    }
}
static int fly_tick(int kind, float dt)
{
    int sp = A.fly[kind].sprite; float w = k_spr[sp].w, h = k_spr[sp].h;
    float dx = A.fly[kind].tx + w * 0.5f - A.fly[kind].sx, dy = A.fly[kind].ty + h * 0.5f - A.fly[kind].sy;
    A.fly[kind].t += dt;
    if (A.fly[kind].t < FLY_DUR) {
        float u = A.fly[kind].t / FLY_DUR, cw = w * u, ch = h * u;
        fly_trail(kind, dx, dy, w, h);                                 /* 0x47b4d9: the trail is emitted before the icon moves */
        sprite_rect(sp, A.fly[kind].sx + dx * u - cw * 0.5f, A.fly[kind].sy + dy * u - ch * 0.5f, cw, ch);
        return 1;
    }
    sprite_rect(sp, A.fly[kind].tx, A.fly[kind].ty, w, h);             /* the last frame is exactly the static icon: a seamless hand-over */
    return A.fly[kind].on = 0;
}
static void ghosts_draw(float dt)                                      /* 0x47bba0 (mode 0) and 0x47bca0 (mode 1); both shrink to nothing, neither fades */
{
    for (int i = 0; i < A.ngh; i++) {
        float t = (A.gh[i].t += dt);
        if (t >= A.gh[i].life || (A.gh[i].mode && A.gh[i].tau + t > 1.0f)) { A.gh[i] = A.gh[--A.ngh]; i--; continue; }   /* mode 1 reaches the HUD: the original reflects the time, which kills it the next frame anyway */
        if (!A.gh[i].mode) {
            float X = A.gh[i].x + t * A.gh[i].vx, Y = A.gh[i].y + t * A.gh[i].vy;
            float W = A.gh[i].w0 - t * A.gh[i].dw, Hh = A.gh[i].h0 - t * A.gh[i].dh;
            fx_rect(X - W * 0.5f, Y - Hh * 0.5f, W, Hh);
        } else {
            float f = A.gh[i].tau + t;                                 /* = S / L, the fraction of the path this strand has reached (5*FLY_DUR = 1) */
            float S = A.gh[i].len * f, k = 1.0f - t / A.gh[i].life;
            float off = (float)sin(6.2831853 * ((int)(1800.0f * f) & 511) / 512.0) * A.gh[i].swing * 0.5f * f;   /* 1800/512 = 3.5 turns, opening to half the icon width */
            float cx = A.gh[i].x + A.gh[i].vx * S, cy = A.gh[i].y + A.gh[i].vy * S, nx = -A.gh[i].vy, ny = A.gh[i].vx;
            float W = A.gh[i].w0 * k, Hh = A.gh[i].h0 * k;
            fx_rect(cx + nx * off - W * 0.5f, cy + ny * off - Hh * 0.5f, W, Hh);
            fx_rect(cx - nx * off - W * 0.5f, cy - ny * off - Hh * 0.5f, W, Hh);
        }
    }
}

/* ---- the plate and the sliding icon, both linear over 0.2 s (0x47bff0 / 0x47c1e0) -------------------------- */
static void plate_start(int k, float cx, float cy, float w0, float h0, float w1, float h1)
{ A.plate[k].on = 1; A.plate[k].sprite = 8; A.plate[k].cx = cx; A.plate[k].cy = cy; A.plate[k].w0 = w0; A.plate[k].h0 = h0; A.plate[k].dw = w1 - w0; A.plate[k].dh = h1 - h0; A.plate[k].t = 0; }
static int plate_tick(int k, float dt)
{
    float u = A.plate[k].t / 0.2f, w, h;
    A.plate[k].t += dt;
    if (A.plate[k].t < 0.2f) { w = A.plate[k].w0 + u * A.plate[k].dw; h = A.plate[k].h0 + u * A.plate[k].dh; }
    else { w = A.plate[k].w0 + A.plate[k].dw; h = A.plate[k].h0 + A.plate[k].dh; }
    sprite_rect(A.plate[k].sprite, A.plate[k].cx - w * 0.5f, A.plate[k].cy - h * 0.5f, w, h);
    return A.plate[k].on = u < 1.0f;
}
static void slide_start(int k, int sprite, float x0, float y0, float x1, float y1)
{ A.slide[k].on = 1; A.slide[k].sprite = sprite; A.slide[k].x0 = x0; A.slide[k].y0 = y0; A.slide[k].dx = x1 - x0; A.slide[k].dy = y1 - y0; A.slide[k].t = 0; }
static int slide_tick(int k, float dt)
{
    float u = (A.slide[k].t += dt) / 0.2f; if (u > 1.0f) u = 1.0f;
    sprite_rect(A.slide[k].sprite, A.slide[k].x0 + u * A.slide[k].dx, A.slide[k].y0 + u * A.slide[k].dy, k_spr[A.slide[k].sprite].w, k_spr[A.slide[k].sprite].h);
    return A.slide[k].on = u < 1.0f;
}

/* ---- the swarm of W's: 25 bonuses being paid out as a heart or as an extra life (0x47c5b0) ----------------- */
static void swarm_start(int to_life, float relx)
{
    memset(&A.sw, 0, sizeof A.sw);
    A.sw.on = 1; A.sw.to_life = to_life; A.sw.counter = 25.0f; A.sw.count = 1; A.sw.tx = relx;
    for (int i = 0; i < 3; i++) { A.sw.p[i].x = k_slot[0][0]; A.sw.p[i].y = k_slot[0][1]; A.sw.p[i].size = k_spr[4].w; A.sw.p[i].fresh = 1; }
    if (to_life) pop_start(3, 2, 17.0f, 37.0f, 0.2f);                  /* 0x460cc0 arms the lives pop for phase 2 right away */
}
static int swarm_tick(float dt)                                        /* 0x47c620 + 0x47c7c0 */
{
    const float span = k_spr[4].w - 23.0f;                             /* the W shrinks 94 -> 23 on the way */
    if (A.sw.counter <= 0.0f) { A.sw.on = 0; A.sw.poprun = 0; A.pop[0].on = 0; return 0; }   /* the original tests == 0.0f exactly; <= 0 cannot get stuck */
    float third = A.sw.tx / 3.0f;
    for (int i = 0; i < A.sw.count; i++) {
        SwarmW *p = &A.sw.p[i];
        if (p->x >= third && p->x < 2.0f * third && !p->spawned && A.sw.count < 3) { A.sw.count++; p->spawned = 1; }   /* the next W leaves once this one is a third of the way */
        if (A.sw.counter < 3.0f && p->fresh) continue;                 /* wind-down: do not launch a fresh W for the last two */
        int n = (int)A.sw.counter;
        if (n % 5 == 0 && !A.sw.poprun) { A.sw.popval = n - 5; pop_start(0, 1, 17.0f, 0.0f, 1.0f); A.sw.poprun = 1; }   /* 20, 15, 10, 5, 0, each shrinking away */
        if (A.sw.poprun) A.sw.poprun = pop_tick(0, A.sw.popval, dt);   /* ticked once per W in flight, exactly as the original */
        p->fresh = 0; p->t += dt;
        if (p->t < SWARM_DUR) { float u = p->t / SWARM_DUR; p->x = k_slot[0][0] + A.sw.tx * u; p->y = k_slot[0][1] + span * 0.5f * u; p->size = k_spr[4].w - span * u; }
        else { p->x = k_slot[0][0] + A.sw.tx; p->y = k_slot[0][1] + span * 0.5f; p->size = 23.0f; A.sw.counter -= 2.5f; }   /* ten landings pay out the 25 */
        sprite_rect(4, p->x, p->y, p->size, p->size);
        if (p->t >= SWARM_DUR) { p->x = k_slot[0][0]; p->y = k_slot[0][1]; p->size = k_spr[4].w; p->t = 0; p->fresh = 1; p->spawned = 0; }
    }
    return 1;
}

/* ---- starting an animation ---------------------------------------------------------------------------------- */
/* 0x448510: kind 1..5 for types 30, 36, 35, 34, 37; screen = the pickup projected into the 640x480 HUD, NULL when it is off screen */
void hud_anim_pickup(int kind, const float *screen, int face)
{
    if (!H.ok) return;
    switch (kind) {
    case 1:                                                            /* 0x461300: the character's own face flies to the portrait */
        pop_start(3, 2, 17.0f, 37.0f, 0.2f);
        fly_start(1, screen, face >= 0 && face < 3 ? face : 0, 6, 1);
        break;
    case 2:                                                            /* 0x461420: the $ item; its whole counter slides in, waits and leaves again */
        slide_start(1, 3, k_slot[2][0], k_slot[2][1], -k_spr[3].w, k_slot[2][1]);
        pop_start(1, 2, 17.0f, 37.0f, 0.2f);
        plate_start(1, k_slot[3][0], k_slot[3][1], 0, 0, 34.0f, 34.0f);
        fly_start(2, screen, 3, 2, 0);
        A.stage[1] = 1; A.hold[1] = 0;
        break;
    case 3:                                                            /* 0x461560: the charge, the same sequence one row lower */
        slide_start(2, 6, k_slot[4][0], k_slot[4][1], -k_spr[6].w, k_slot[4][1]);
        pop_start(2, 2, 17.0f, 37.0f, 0.2f);
        plate_start(2, k_slot[5][0], k_slot[5][1], 0, 0, 34.0f, 34.0f);
        fly_start(3, screen, 6, 4, 1);
        A.stage[2] = 1; A.hold[2] = 0;
        break;
    case 4:                                                            /* 0x4616a0: the big W to the bonus icon */
        pop_start(0, 2, 17.0f, 37.0f, 0.2f);
        fly_start(4, screen, 4, 0, 0);
        A.latch = 0;                                                   /* hud+0x10 is cleared here and set again by the reward */
        break;
    case 5:                                                            /* 0x461760: the race flag, same slot */
        pop_start(0, 2, 17.0f, 37.0f, 0.2f);
        fly_start(5, screen, 5, 0, 0);
        break;
    default: break;                                                    /* kind 6 (type 38) does nothing */
    }
}
/* 0x448380: the bonus counter went down, so 25 W's have just been paid out */
static void hud_anim_reward(int to_life, float health_old)
{
    swarm_start(to_life, to_life ? (k_spr[0].w - 23.0f) * 0.5f + k_slot[6][0] - k_slot[0][0]    /* 548.5: a 23 wide square centred on the portrait */
                                 : 559.0f - 30.0f * (health_old + 1.0f) - k_slot[0][0]);        /* the slot of the heart that is coming in */
    A.latch = 1;
}

/* the animator itself: 0x4480d0, run after the static HUD so everything here draws on top */
static void hud_anim_tick(const HudState *s, float dt)
{
    int lives_hud = s->lives > 0 ? s->lives - 1 : 0;                   /* hud+0x20, the value the row shows */
    if (A.latch && !A.fly[4].on && !A.pop[0].on) A.latch = 0;          /* 0x44812b clears hud+0x10 with the flag; never deadlock if the flight never started */
    if (A.sw.on && !A.latch) {
        if (!A.sw.to_life) A.sw.on = swarm_tick(dt);                                         /* 0x460bb0: the heart variant is the swarm and nothing else */
        else if (!A.sw.phase2) {                                                             /* 0x460be0 phase 1 */
            if (!swarm_tick(dt)) A.sw.phase2 = 1;
            number_sized(k_anchor[3][0], k_anchor[3][1], lives_hud > 0 ? lives_hud - 1 : 0, 17.0f);
            A.sw.on = 1;
        } else {                                                                             /* phase 2: the lives number pops to its new value */
            number_sized(k_anchor[0][0], k_anchor[0][1], s->bonus > 0 ? s->bonus - 1 : 0, 17.0f);
            if (!pop_tick(3, lives_hud, dt)) A.sw.on = 0;
        }
    }
    if (A.fly[4].on || A.latch) {                                      /* 0x4611b0; 0x448115 feeds it a literal 25 while the latch is up, so the circle reads 24 and then pops 25 */
        int v = A.latch ? 25 : s->bonus;
        if (A.fly[4].on) { fly_tick(4, dt); number_sized(k_anchor[0][0], k_anchor[0][1], v > 0 ? v - 1 : 0, 17.0f); }
        else if (!A.pop[0].on || !pop_tick(0, v, dt)) A.latch = 0;     /* the whole animation, flight and pop, holds the reward back */
    }
    else if (A.fly[5].on) { fly_tick(5, dt); number_sized(k_anchor[0][0], k_anchor[0][1], s->bonus > 0 ? s->bonus - 1 : 0, 17.0f); }
    else if (A.pop[0].on && !A.sw.on) pop_tick(0, s->bonus == 0 && !s->race ? 25 : s->bonus, dt);   /* 0x4611b0: a counter that wrapped pops "25" */
    if (A.fly[1].on) { fly_tick(1, dt); number_sized(k_anchor[3][0], k_anchor[3][1], lives_hud > 0 ? lives_hud - 1 : 0, 17.0f); }
    else if (A.mlives) {                                                                     /* 0x461f00: a life lost, 17 -> 37 -> 17 and then away */
        if (!pop_tick(3, lives_hud + 1, dt)) { if (A.mlives == 1) { A.mlives = 2; pop_start(3, 1, 17.0f, 0.0f, 0.2f); } else A.mlives = 0; }
    } else if (A.pop[3].on && !A.sw.on) pop_tick(3, lives_hud, dt);
    for (int k = 1; k <= 2; k++) {                                     /* 0x460db0 / 0x460fb0: the $ and charge counters appear, hold 1.5 s and leave */
        int icon = k == 1 ? 3 : 6, slot_i = k == 1 ? 2 : 4, slot_p = k == 1 ? 3 : 5, value = k == 1 ? s->unique : s->charges;
        if (!A.stage[k]) continue;                                     /* 0x447b18: outside the pause page 0x447660 draws no $ / charge row at all, so this sequence is the only thing showing them */
        if (A.stage[k] == 1) {
            if (A.fly[k + 1].on) fly_tick(k + 1, dt);
            if (A.plate[k].on) { plate_tick(k, dt); continue; }
            sprite(icon, k_slot[slot_i][0], k_slot[slot_i][1]); sprite(8, k_slot[slot_p][0], k_slot[slot_p][1]);
            if (A.pop[k].on) { pop_tick(k, value, dt); continue; }
            number_centred(k_anchor[k][0], k_anchor[k][1], value);
            plate_start(k, k_slot[slot_p][0], k_slot[slot_p][1], 34.0f, 34.0f, 0, 0); A.stage[k] = 2;
        } else if (A.stage[k] == 2) {
            sprite(icon, k_slot[slot_i][0], k_slot[slot_i][1]); sprite(8, k_slot[slot_p][0], k_slot[slot_p][1]);
            number_centred(k_anchor[k][0], k_anchor[k][1], value);
            if ((A.hold[k] += dt) > 1.5f) A.stage[k] = 3;
        } else {
            plate_tick(k, dt);
            if (!slide_tick(k, dt)) A.stage[k] = 0;
        }
    }
    ghosts_draw(dt);
    /* the setters 0x448380 / 0x4482c0 watch the values themselves; the port does the same by comparing frames */
    if (A.prev_ok && !s->race) {
        if (s->bonus < A.prev_bonus && !A.sw.on) hud_anim_reward(!(A.prev_health < 5.0f), A.prev_health);
        if (s->lives < A.prev_lives && !A.mlives) { A.mlives = 1; pop_start(3, 2, 17.0f, 37.0f, 0.2f); }
    }
    A.prev_ok = 1; A.prev_lives = s->lives; A.prev_bonus = s->bonus; A.prev_health = s->health;
}

void hud_begin(int win_w, int win_h)
{
    glViewport(0, 0, win_w, win_h);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, 640, 480, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING); glDisable(GL_ALPHA_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_FOG);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}
void hud_end(void)
{
    glColor4f(1, 1, 1, 1); glDisable(GL_TEXTURE_2D); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

/* ---------------------------------------------------------------- HUD 0x447210 (docs/HUD_TEXT.md 4.4, draw order as there) */
void hud_draw(const HudState *s, float dt)
{
    if (!H.ok) return;
    const float Y = 51, BH = 23;
    quad(0, Y, 256, BH, 0, 0, 0, 0, 0, 0x800000ff, 0x800000ff, 0x000000ff, 0x000000ff);                   /* blue bar (0x4472d8) */
    if (!s->race) {
        quad(384, Y, 27, BH, 0, 0, 0, 0, 0, 0x00ff0000, 0x00ff0000, 0x13ff0000, 0x13ff0000);
        for (int i = 1; i <= 5; i++) {                                                                      /* empty health slots, alpha ramp (x - 384) * 128 / 175 */
            float x = 556.0f - 29.0f * i; uint32_t al = (uint32_t)((x - 384) * 128 / 175), ar = (uint32_t)((x + 29 - 384) * 128 / 175);
            sprite_part(9, x, Y, 1e9f, al << 24 | 0x808080, ar << 24 | 0x808080);
        }
    } else quad(384, Y, 172, BH, 0, 0, 0, 0, 0, 0x00ff0000, 0x00ff0000, 0x80ff0000, 0x80ff0000);
    quad(556, Y, 84, BH, 0, 0, 0, 0, 0, 0x80ff0000, 0x80ff0000, 0x80ff0000, 0x80ff0000);
    sprite(s->face >= 0 && s->face < 3 ? s->face : 0, 544, 30);                                             /* slot 6 */
    sprite(8, 584, 70);                                                                                     /* slot 7, anchor A3 */
    if (!A.fly[1].on && !A.mlives && !(A.sw.on && A.sw.to_life && !A.latch)) number_centred(600, 86, s->lives > 0 ? s->lives - 1 : 0);   /* 0x44771e */
    sprite(s->race ? 5 : 4, 16, 16);                                                                        /* slot 0 */
    sprite(8, 16, 66);                                                                                      /* slot 1, A0: the icon and the plate stay, only the number moves out of the way */
    if (!A.sw.on && !A.fly[4].on && !A.fly[5].on) number_centred(32, 82, s->bonus);                          /* 0x44792e, 0x44794f */
    if (s->show_total && !A.sw.on) {                                                                        /* the "taken / total" line goes too while the reward is paid out */
        uint16_t t[40]; int n = number_codes(t, s->got); const uint16_t *sl = hud_string(9);                /* "/" */
        for (; sl && *sl && n < 20; sl++) t[n++] = *sl;
        number_codes(t + n, s->total);
        font_size(17.0f); font_draw(110, Y + 12 - font_cell() * 0.5f, t, 0xfe808080);
    }
    if (!s->race) for (int i = 1; i <= (int)s->health && i <= 5; i++) {
        if (A.sw.on && !A.sw.to_life && i == (int)s->health) continue;                                       /* 0x447ad5: the heart the W's are bringing in is left out until they land */
        sprite(7, 559.0f - 29.0f * i, Y);
    }
    if (s->extended) {
        sprite(3, 16, 136); sprite(8, 50, 181); number_centred(66, 197, s->unique);                         /* slots 2/3, A1 */
        sprite(6, 16, 221); sprite(8, 50, 266); number_centred(66, 282, s->charges);                        /* slots 4/5, A2 */
    }
    if (s->power > 0) {                                                                                     /* power gauge 0x447d70 */
        if (s->power >= 1.0f) { H.blink += dt; if (H.blink >= 0.3f) H.blink -= 0.3f; sprite(10, 0, 426); if (H.blink >= 0.15f) sprite(11, 0, 421); }
        else { float w = s->power * 104.0f; sprite_part(10, 0, 426, w, 0xfe808080, 0xfe808080); sprite_part(11, 0, 421, w, 0xfe808080, 0xfe808080); }
    } else H.blink = 0;
    hud_anim_tick(s, dt);                                                                                   /* 0x4480d0 runs after 0x447660, so the animations draw on top */
}

/* ---------------------------------------------------------------- text box: message 1080 (0x456ed0 / 0x4571c0) */
void hud_text_reset(void) { H.box.state = 0; }

void hud_text_open(int halign, int valign, const uint32_t *ids, int n)
{
    if (!H.ok) return;
    memset(&H.box, 0, sizeof H.box);
    float size = 25;
    for (int i = 0; i < n && i < 3; i++) { if (ids[i] == 0xffffffffu) break; H.box.id[H.box.n++] = ids[i]; }
    for (int i = 0; i < H.box.n; i++) {
        const uint16_t *s = hud_string(H.box.id[i]); if (!s) continue;
        for (;;) { font_size(size); if (font_measure(s) < 640.0f) break; size -= 1; if (size < 17) break; }
    }
    font_size(size); H.box.size = size - 2;                          /* laid out at `size`, drawn 2 smaller (0x456fa7) */
    float cell = font_cell(), tot = H.box.n * cell, y = valign == 0 ? 240 - tot * 0.5f : valign == 1 ? 16 : 480 - tot - 16, top = y, minx = 640, maxx = 0;
    for (int i = 0; i < H.box.n; i++) {
        const uint16_t *s = hud_string(H.box.id[i]); float w = s ? font_measure(s) : 0;
        float x = halign == 0 ? 320 - w * 0.5f : halign == 1 ? 16 : 640 - w - 16;
        if (x < minx) minx = x; if (x + w > maxx) maxx = x + w;
        H.box.x[i] = x; H.box.y[i] = y; y += cell;
        if (H.box.id[i] == 0x20001) top = y;
    }
    H.box.rect[0] = (float)(int)(minx - 16); H.box.rect[1] = (float)(int)(top - 16); H.box.rect[2] = (float)(int)(maxx + 16); H.box.rect[3] = (float)(int)(y + 16);
    H.box.state = 1;
}

void hud_text_draw(int closed, float dt)
{
    if (!H.ok || !H.box.state) return;
    float a = 254;
    H.box.t += dt;
    if (H.box.state == 1) { a = 2 * H.box.t * 254; if (H.box.t >= 0.5f) { H.box.state = 2; a = 254; } }
    else if (H.box.state == 2) { if (closed) { H.box.state = 3; H.box.t = 0; } }
    if (H.box.state == 3) { a = (1 - 2 * H.box.t) * 254; if (H.box.t >= 0.5f) { H.box.state = 0; return; } }
    if (a < 0) a = 0;
    uint32_t col = (uint32_t)a << 24 | 0x808080, bg = (col >> 1) & 0x7f000000;
    quad(H.box.rect[0], H.box.rect[1], H.box.rect[2] - H.box.rect[0], H.box.rect[3] - H.box.rect[1], 0, 0, 0, 0, 0, bg, bg, bg, bg);
    font_size(H.box.size);
    for (int i = 0; i < H.box.n; i++) { const uint16_t *s = hud_string(H.box.id[i]); if (s) font_draw(H.box.x[i], H.box.y[i], s, col); }
}

/* ---------------------------------------------------------------- menu pages (docs/TITLE.md 5) */
void hud_title_reset(void) { H.logo_v = 0; H.menu_t = 0; }

/* the item list of the common page class (0x446640): one size S for the whole page, shrunk until the widest item
 * fits in 640; y = yfrac * 480 and a cell (62 * S / 40) per item; the selected item is left out while the blink
 * phase is under 0.25 s - the original has no cursor and no colour difference. Advancing the phase is the caller's
 * job (0x4464f0 does it once per frame), so hud_menu_page and hud_title_draw never both tick it. */
static void page_items(const uint32_t *ids, int n, float yfrac, int sel)
{
    float S = 30.0f;                                                  /* 0x4b39a8 */
    for (int i = 0; i < n; i++) {
        const uint16_t *s = hud_string(ids[i]); if (!s) continue;
        while (S > 15.0f) { font_size(S); if (font_measure(s) < 640.0f) break; S -= 1.0f; }
    }
    font_size(S);
    float y = yfrac * 480.0f, cell = font_cell();
    for (int i = 0; i < n; i++) {
        const uint16_t *s = hud_string(ids[i]);
        if (s && !(i == sel && H.menu_t < 0.25f)) font_draw(320 - font_measure(s) * 0.5f, y, s, 0xff808080);
        y += cell;
    }
    font_size(17.0f);
}

void hud_menu_page(const uint32_t *ids, int n, float yfrac, int sel, float dt)
{
    if (!H.ok) return;
    H.menu_t += dt; if (H.menu_t >= 0.5f) H.menu_t -= 0.5f;           /* [0x5d7b1c], wraps at 0.5 (0x4b39a4) */
    page_items(ids, n, yfrac, sel);
}

void hud_title_draw(int page, int sel, int want_logo, float dt)
{
    if (!H.ok) return;
    static const uint32_t items0[1] = { 21 };                        /* "Press a key" */
    static const uint32_t items1[4] = { 22, 23, 36, 2 };             /* New game, Load game, Options, Quit */
    H.menu_t += dt; if (H.menu_t >= 0.5f) H.menu_t -= 0.5f;
    if (page == 0) page_items(items0, 1, 0.7f, 0);
    else if (page == 1) page_items(items1, 4, 0.55f, sel);
    font_size(30.0f);
    if (H.logo && H.logo_v > 0) {                                     /* 0x446b00: alpha = 254 * v / 5, source 0,0,209,247 at (216,16) */
        uint32_t c = (uint32_t)(254.0f * H.logo_v / 5.0f) << 24 | 0x808080;
        quad(216, 16, 209, 247, H.logo, 0, 0, 209.0f / H.logo_w, 247.0f / H.logo_h, c, c, c, c);
    }
    if (want_logo) { H.logo_v += 5 * dt; if (H.logo_v > 5) H.logo_v = 5; } else H.logo_v = 0;
    font_size(17.0f);
}

/* ---------------------------------------------------------------- results screen (docs/GAMEFLOW.md 5.1, HUD_TEXT.md 6)
 * The strings are the ones the original reserves for it (12 CLEARED!!, 13 RESULTS, 15 OK, 17 HIGH SCORE, 46 points,
 * 127 Level, 128 Seconds, 129 Final Score, 130 "Total Score :" and the characters 7 "%", 8 "=", 10 ":", 11 "+"), the
 * two categories are the ones the score formula 0x453cb0 uses, and each one gets its "+ 50 %" when it is complete.
 * The layout itself (the 20-odd Measure/Draw pairs of 0x454963..0x455d97) is NOT decompiled: the placement below is
 * this port's, and so is the dark backdrop (drawn like the text box of message 1080). String 14 "TOTAL" has no place
 * here yet because nothing says where the original puts it. */
static uint16_t g_row[64]; static int g_rown;
static void row_reset(void) { g_rown = 0; g_row[0] = 0; }
static void row_str(uint32_t ref)
{
    const uint16_t *s = hud_string(ref);
    for (; s && *s && g_rown < 62; s++) g_row[g_rown++] = *s;
    g_row[g_rown] = 0;
}
static void row_space(void)                                          /* Common string 40 is "a a": its middle code is the space glyph */
{
    const uint16_t *s = hud_string(40);
    if (s && s[0] && s[1] && g_rown < 62) { g_row[g_rown++] = s[1]; g_row[g_rown] = 0; }
}
static void row_num(int v)
{
    uint16_t d[16]; int n = number_codes(d, v);
    for (int i = 0; i < n && g_rown < 62; i++) g_row[g_rown++] = d[i];
    g_row[g_rown] = 0;
}
static float row_draw(float x, float y, int right, uint32_t col)     /* right: x is the right edge */
{
    float w = font_measure(g_row);
    font_draw(right ? x - w : x, y, g_row, col);
    return w;
}

void hud_results_draw(const HudResults *r, int show_ok, float dt)
{
    if (!H.ok) return;
    H.menu_t += dt; if (H.menu_t >= 0.5f) H.menu_t -= 0.5f;
    const uint32_t col = 0xff808080;                                  /* the menu colour: 0x80 per channel is 1.0 (docs/HUD_TEXT.md 5.2) */
    const float L = 150, R = 490, rows = 42;
    quad(96, 14, 448, 464, 0, 0, 0, 0, 0, 0x60000000, 0x60000000, 0x60000000, 0x60000000);   /* backdrop, like the 1080 text box: black at half the text alpha */
    font_size(35.0f);
    { const uint16_t *s = hud_string(12); if (s) font_draw(320 - font_measure(s) * 0.5f, 28, s, col); }     /* CLEARED!! */
    font_size(30.0f);
    { const uint16_t *s = hud_string(13); if (s) font_draw(320 - font_measure(s) * 0.5f, 78, s, col); }     /* RESULTS */
    font_size(24.0f);
    float y = 132;
    row_reset(); row_str(127); row_space(); row_str(10); row_draw(L, y, 0, col);                            /* "Level :" */
    row_reset(); row_num(r->level); row_draw(R, y, 1, col);
    y += rows;
    for (int cat = r->race ? 1 : 0; cat < 2; cat++) {                                                       /* the score categories of 0x453cb0; a race level only counts the second one */
        int got = cat ? r->got_b : r->got_a, tot = cat ? r->total_b : r->total_a;
        sprite_rect(cat ? 4 : 12, L, y - 6, 40, 40);                                                        /* sprite 4 = the W of the HUD; 12 is the 64x64 icon of image 64 the HUD never draws */
        row_reset(); row_num(got); row_space(); row_str(8); row_space(); row_num(tot);                       /* "got = total" */
        if (tot && got == tot) { row_space(); row_space(); row_str(11); row_num(50); row_str(7); }           /* "+50%" */
        row_draw(R, y, 1, col);
        y += rows;
    }
    row_reset(); row_str(128); row_space(); row_str(10); row_draw(L, y, 0, col);                            /* "Seconds :" */
    row_reset(); row_num((int)r->time); row_draw(R, y, 1, col);
    y += rows + 10;
    row_reset(); row_str(129); row_space(); row_str(10); row_draw(L, y, 0, col);                            /* "Final Score :" */
    row_reset(); row_num(r->score); row_space(); row_str(46); row_draw(R, y, 1, col);                        /* "<score> points" */
    y += rows;
    if (r->high) { const uint16_t *s = hud_string(17); if (s && H.menu_t >= 0.25f) font_draw(320 - font_measure(s) * 0.5f, y, s, col); }   /* HIGH SCORE, blinking with the menu phase */
    y += rows;
    row_reset(); row_str(130); row_draw(L, y, 0, col);                                                       /* "Total Score :" (the best run of this level) */
    row_reset(); row_num(r->best > r->score ? r->best : r->score); row_draw(R, y, 1, col);
    if (show_ok) { const uint32_t ok = 15; page_items(&ok, 1, 0.90f, 0); }                                    /* the panel item of page 0x1e */
    font_size(17.0f);
}

/* ---------------------------------------------------------------- pickup sprites in the world (0x479530 -> DrawSprite 0x470f10) */
void hud_world_sprites_begin(const float *right, const float *up)
{
    memcpy(H.sr, right, sizeof H.sr); memcpy(H.su, up, sizeof H.su);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.02f);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glColor4f(1, 1, 1, 1);   /* rgb 0.5 = neutral */
}
void hud_world_sprite(int n, const float *pos, float size)
{
    if (!H.ok || n < 0 || n >= 5 || !H.bonus[n]) return;
    float h = size * 0.70710678f, c[3] = { pos[0], pos[1] + 50.0f, pos[2] };        /* +50: 0x4a9030; h = size/sqrt(2), see hud_world_fx */
    glBindTexture(GL_TEXTURE_2D, H.bonus[n]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(c[0] - H.sr[0] * h + H.su[0] * h, c[1] - H.sr[1] * h + H.su[1] * h, c[2] - H.sr[2] * h + H.su[2] * h);
    glTexCoord2f(0, 1); glVertex3f(c[0] - H.sr[0] * h - H.su[0] * h, c[1] - H.sr[1] * h - H.su[1] * h, c[2] - H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(1, 1); glVertex3f(c[0] + H.sr[0] * h - H.su[0] * h, c[1] + H.sr[1] * h - H.su[1] * h, c[2] + H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(1, 0); glVertex3f(c[0] + H.sr[0] * h + H.su[0] * h, c[1] + H.sr[1] * h + H.su[1] * h, c[2] + H.sr[2] * h + H.su[2] * h);
    glEnd();
}
/* one wing of a butterfly (0x47d440 / 0x470f10): a square of 2*half units in the plane of u and v, centred on c. It is
 * NOT camera-facing - the two wings share a hinge along v (the flight direction) and swing about it. Corner angles
 * 45/135/225/315 degrees (0x470f94, table2[18] = 64) with the UV set of case 4 (0x470e96). Between
 * hud_world_sprites_begin/end, like the pickups: the original submits it with the same mode 0x28. */
void hud_world_wing(int n, const float *c, const float *u, const float *v, float half)
{
    if (!H.ok || n < 0 || n >= 4 || !H.env[n]) return;
    glBindTexture(GL_TEXTURE_2D, H.env[n]);
    glBegin(GL_QUADS);
    glTexCoord2f(1, 0); glVertex3f(c[0] + (v[0] + u[0]) * half, c[1] + (v[1] + u[1]) * half, c[2] + (v[2] + u[2]) * half);
    glTexCoord2f(1, 1); glVertex3f(c[0] + (-v[0] + u[0]) * half, c[1] + (-v[1] + u[1]) * half, c[2] + (-v[2] + u[2]) * half);
    glTexCoord2f(0, 1); glVertex3f(c[0] - (v[0] + u[0]) * half, c[1] - (v[1] + u[1]) * half, c[2] - (v[2] + u[2]) * half);
    glTexCoord2f(0, 0); glVertex3f(c[0] + (v[0] - u[0]) * half, c[1] + (v[1] - u[1]) * half, c[2] + (v[2] - u[2]) * half);
    glEnd();
}
void hud_world_sprites_end(void) { glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDisable(GL_TEXTURE_2D); }

/* ring lying in the plane through c with normal n (the landing marker, docs/PERSO_JUMP.md 5). The band runs from
 * radius-hw to radius+hw and carries its brightness in the vertex colours: 0 at both rims, rgb*alpha at radius, so
 * it has no hard edge and needs no texture. Additive, like the other world effects. */
#define RING_SEGS 48
void hud_world_ring(const float *c, const float *n, float radius, float hw, const float *rgb, float alpha)
{
    if (!H.ok || radius <= 0 || hw <= 0 || alpha <= 0) return;
    float up[3] = { n[0], n[1], n[2] }, l = (float)sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) up[i] /= l;
    float ax[3] = { 1, 0, 0 }; if (fabs(up[0]) > 0.9f) { ax[0] = 0; ax[2] = 1; }           /* any axis that is not parallel to n */
    float u[3] = { ax[1] * up[2] - ax[2] * up[1], ax[2] * up[0] - ax[0] * up[2], ax[0] * up[1] - ax[1] * up[0] };
    l = (float)sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) u[i] /= l;
    float v[3] = { up[1] * u[2] - up[2] * u[1], up[2] * u[0] - up[0] * u[2], up[0] * u[1] - up[1] * u[0] };
    glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D); glBlendFunc(GL_ONE, GL_ONE);
    for (int band = 0; band < 2; band++) {                                                 /* inner rim -> core, core -> outer rim */
        float r0 = band ? radius : radius - hw, r1 = band ? radius + hw : radius;
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= RING_SEGS; i++) {
            float a = 6.2831853f * (float)i / (float)RING_SEGS, ca = (float)cos(a), sa = (float)sin(a);
            float d[3] = { u[0] * ca + v[0] * sa, u[1] * ca + v[1] * sa, u[2] * ca + v[2] * sa };
            for (int e = 0; e < 2; e++) {                                                  /* the core edge is bright, the rim edge is black */
                float r = e ? r1 : r0, w = (e == 0) == (band != 0) ? alpha : 0.0f;
                glColor3f(rgb[0] * w, rgb[1] * w, rgb[2] * w);
                glVertex3f(c[0] + d[0] * r, c[1] + d[1] * r, c[2] + d[2] * r);
            }
        }
        glEnd();
    }
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}

int hud_sky_images(uint32_t out[5])
{
    static const int order[5] = { 3, 0, 1, 2, 4 };
    if (H.nlevel_img < 5) return 0;
    for (int f = 0; f < 5; f++) out[f] = H.sky[order[f]];
    return 1;
}

void hud_world_beam(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b)
{
    if (!H.ok) return;
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, m[3] = { (a[0] + b[0]) * 0.5f - eye[0], (a[1] + b[1]) * 0.5f - eye[1], (a[2] + b[2]) * 0.5f - eye[2] };
    float s[3] = { d[1] * m[2] - d[2] * m[1], d[2] * m[0] - d[0] * m[2], d[0] * m[1] - d[1] * m[0] };      /* perpendicular to the segment and to the view ray */
    float l = (float)sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) s[i] *= hw / l;
    glBlendFunc(GL_ONE, GL_ONE); glDisable(GL_ALPHA_TEST);                          /* flag 4 = additive ONE/ONE */
    if (H.beam) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, H.beam); } else glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor3f(rgb[0] * alpha_a, rgb[1] * alpha_a, rgb[2] * alpha_a);
    glTexCoord2f(0, 0); glVertex3f(a[0] - s[0], a[1] - s[1], a[2] - s[2]);
    glTexCoord2f(0, 1); glVertex3f(a[0] + s[0], a[1] + s[1], a[2] + s[2]);
    glColor3f(rgb[0] * alpha_b, rgb[1] * alpha_b, rgb[2] * alpha_b);
    glTexCoord2f(1, 1); glVertex3f(b[0] + s[0], b[1] + s[1], b[2] + s[2]);
    glTexCoord2f(1, 0); glVertex3f(b[0] - s[0], b[1] - s[1], b[2] - s[2]);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}

void hud_world_fx(int image, const float *pos, float size, float turns, const float *rgb, float alpha)
{
    int n = fx_slot(image), blend = image == 10 || image == 11; if (!H.ok || n < 0 || !H.fx[n] || alpha <= 0 || size <= 0) return;   /* the stars are alpha blended, the rest additive */
    /* 0x470fee..0x4710b3: every corner is (size*cos t, size*sin t) with t = rot +- 45 deg, so `size` is the half
     * DIAGONAL, not the half width: the half width is size/sqrt(2) and the side is 1.4142*size */
    float h = size * 0.70710678f, c = (float)cos(turns * 6.2831853f) * h, s = (float)sin(turns * 6.2831853f) * h, r[3], u[3];
    for (int i = 0; i < 3; i++) { r[i] = H.sr[i] * c + H.su[i] * s; u[i] = H.su[i] * c - H.sr[i] * s; }
    glDisable(GL_ALPHA_TEST); glBindTexture(GL_TEXTURE_2D, H.fx[n]);
    if (blend) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(rgb[0], rgb[1], rgb[2], alpha); }
    else { glBlendFunc(GL_ONE, GL_ONE); glColor3f(rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha); }
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(pos[0] - r[0] + u[0], pos[1] - r[1] + u[1], pos[2] - r[2] + u[2]);
    glTexCoord2f(0, 1); glVertex3f(pos[0] - r[0] - u[0], pos[1] - r[1] - u[1], pos[2] - r[2] - u[2]);
    glTexCoord2f(1, 1); glVertex3f(pos[0] + r[0] - u[0], pos[1] + r[1] - u[1], pos[2] + r[2] - u[2]);
    glTexCoord2f(1, 0); glVertex3f(pos[0] + r[0] + u[0], pos[1] + r[1] + u[1], pos[2] + r[2] + u[2]);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST);
}
/* the same quad, but lying in the plane with normal `n` (0x4717d7 builds it on S+0x230..0x238 when sprite flag bit 0
 * is off). The flash of an explosion is nine of these, each on its own normal (docs/PROJECTILES.md 5.3), so which way
 * round the two in-plane axes point does not matter: any pair perpendicular to `n` gives the same square. */
void hud_world_fx_plane(int image, const float *pos, const float *n, float size, const float *rgb, float alpha)
{
    int k = fx_slot(image); if (!H.ok || k < 0 || !H.fx[k] || alpha <= 0 || size <= 0) return;
    float N[3] = { n[0], n[1], n[2] }, l = (float)sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
    if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) N[i] /= l;
    float ax[3] = { 1, 0, 0 }; if (fabs(N[0]) > 0.9f) { ax[0] = 0; ax[2] = 1; }
    float u[3] = { ax[1] * N[2] - ax[2] * N[1], ax[2] * N[0] - ax[0] * N[2], ax[0] * N[1] - ax[1] * N[0] };
    l = (float)sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (l < 1e-6f) return;
    float h = size * 0.70710678f;
    for (int i = 0; i < 3; i++) u[i] *= h / l;
    float v[3] = { (N[1] * u[2] - N[2] * u[1]), (N[2] * u[0] - N[0] * u[2]), (N[0] * u[1] - N[1] * u[0]) };
    glDisable(GL_ALPHA_TEST); glBindTexture(GL_TEXTURE_2D, H.fx[k]);
    glBlendFunc(GL_ONE, GL_ONE); glColor3f(rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(pos[0] - u[0] + v[0], pos[1] - u[1] + v[1], pos[2] - u[2] + v[2]);
    glTexCoord2f(0, 1); glVertex3f(pos[0] - u[0] - v[0], pos[1] - u[1] - v[1], pos[2] - u[2] - v[2]);
    glTexCoord2f(1, 1); glVertex3f(pos[0] + u[0] - v[0], pos[1] + u[1] - v[1], pos[2] + u[2] - v[2]);
    glTexCoord2f(1, 0); glVertex3f(pos[0] + u[0] + v[0], pos[1] + u[1] + v[1], pos[2] + u[2] + v[2]);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST);
}
/* the plane a mark lies in: N = the surface normal, +v = `dir` flattened into that plane, u = v x N, mirrored in u
 * for the other foot (sprite flag 0x40). 0 = the direction is along the normal and there is no plane to speak of. */
static int decal_basis(const float *normal, const float *dir, int mirror, float *N, float *u, float *v)
{
    float l = (float)sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (l < 1e-6f) { N[0] = 0; N[1] = 1; N[2] = 0; } else { N[0] = normal[0] / l; N[1] = normal[1] / l; N[2] = normal[2] / l; }
    float d = dir[0] * N[0] + dir[1] * N[1] + dir[2] * N[2];
    v[0] = dir[0] - N[0] * d; v[1] = dir[1] - N[1] * d; v[2] = dir[2] - N[2] * d;
    l = (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 1e-6f) return 0;
    v[0] /= l; v[1] /= l; v[2] /= l;
    u[0] = v[1] * N[2] - v[2] * N[1]; u[1] = v[2] * N[0] - v[0] * N[2]; u[2] = v[0] * N[1] - v[1] * N[0];
    if (mirror) { u[0] = -u[0]; u[1] = -u[1]; u[2] = -u[2]; }
    return 1;
}
/* ---- ground mark of a footstep (docs/FOOTSTEPS.md): a sprite that lies in a plane instead of facing the camera
 * (0x4717d7 builds the quad on the normal S+0x230..0x238 when flag bit 0 is off), turned so that +v runs along the
 * walking direction (flag bit 2 = rotation) and mirrored in u for the other foot (flag bit 0x40, value 2 = mirrored).
 * `size` is the half diagonal, as everywhere (0x470fee). What 0x47cba0 draws is not decompiled, so the port prints
 * the mark as `dst * (1 - rgb*strength)`: the only ground-ish image it has is a white cloud on black whose alpha is a
 * constant 1, and an alpha blend of that is a dark square. Multiplying keeps the black of the texture out of it. */
void hud_world_decal(int image, const float *pos, const float *normal, const float *dir, float size, int mirror, const float *rgb, float strength)
{
    int n = fx_slot(image); if (!H.ok || n < 0 || !H.fx[n] || strength <= 0 || size <= 0) return;
    float N[3], u[3], v[3]; if (!decal_basis(normal, dir, mirror, N, u, v)) return;
    float h = size * 0.70710678f, c[3] = { pos[0] + N[0] * 3.0f, pos[1] + N[1] * 3.0f, pos[2] + N[2] * 3.0f };   /* lifted off the floor: coplanar it z-fights */
    glDisable(GL_ALPHA_TEST); glBindTexture(GL_TEXTURE_2D, H.fx[n]);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR); glColor3f(rgb[0] * strength, rgb[1] * strength, rgb[2] * strength);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(c[0] - u[0] * h + v[0] * h, c[1] - u[1] * h + v[1] * h, c[2] - u[2] * h + v[2] * h);
    glTexCoord2f(0, 1); glVertex3f(c[0] - u[0] * h - v[0] * h, c[1] - u[1] * h - v[1] * h, c[2] - u[2] * h - v[2] * h);
    glTexCoord2f(1, 1); glVertex3f(c[0] + u[0] * h - v[0] * h, c[1] + u[1] * h - v[1] * h, c[2] + u[2] * h - v[2] * h);
    glTexCoord2f(1, 0); glVertex3f(c[0] + u[0] * h + v[0] * h, c[1] + u[1] * h + v[1] * h, c[2] + u[2] * h + v[2] * h);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST);
}
/* ---- what a peck leaves behind (docs/OBJECTS.md 1.6) ----------------------------------------------------------
 * A beak does not scorch wood, it takes a bite out of it, so neither of these two is an effect sprite: the hole is
 * a ragged cup drawn straight into the pecked face and the chips are solid slivers. Both are built here instead of
 * from a bank 0 image because no image in the bank is a hole or a splinter, and multiplying a soft white cloud over
 * the wall (what the footstep mark does with image 14) only ever gives a smudge, never a hole. */
#define GOUGE_SEGS 11
static float gouge_rnd(unsigned seed, int i)      /* stable per hole and per corner: a hole that re-rolls every frame boils */
{
    unsigned h = seed * 1664525u + (unsigned)i * 1013904223u + 0x9e3779b9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (float)(h & 0xffffu) / 65535.0f;
}
void hud_world_gouge(const float *pos, const float *n, const float *dir, float size, unsigned seed, const float *rgb, float strength, float rim)
{
    if (!H.ok || size <= 0 || strength <= 0) return;
    float N[3], u[3], v[3]; if (!decal_basis(n, dir, 0, N, u, v)) return;
    float c[3] = { pos[0] + N[0] * 3.0f, pos[1] + N[1] * 3.0f, pos[2] + N[2] * 3.0f };   /* off the face, like the footstep mark: coplanar it z-fights */
    float d[GOUGE_SEGS][3], r[GOUGE_SEGS];
    for (int i = 0; i < GOUGE_SEGS; i++) {
        float a = 6.2831853f * (float)i / (float)GOUGE_SEGS, ca = (float)cos(a), sa = (float)sin(a);
        for (int k = 0; k < 3; k++) d[i][k] = u[k] * ca + v[k] * sa;
        r[i] = size * (0.6f + 0.4f * gouge_rnd(seed, i));                                /* ragged: a peck is not a circle */
    }
    glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);                                        /* dst * (1 - rgb*strength), as the footstep mark darkens */
    glBegin(GL_TRIANGLE_FAN);                                                            /* the cup: dark to the inner edge */
    glColor3f(rgb[0] * strength, rgb[1] * strength, rgb[2] * strength); glVertex3f(c[0], c[1], c[2]);
    for (int i = 0; i <= GOUGE_SEGS; i++) { int j = i % GOUGE_SEGS; float k = r[j] * 0.55f;
        glVertex3f(c[0] + d[j][0] * k, c[1] + d[j][1] * k, c[2] + d[j][2] * k); }
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);                                                          /* and out to the rim, where it stops darkening */
    for (int i = 0; i <= GOUGE_SEGS; i++) { int j = i % GOUGE_SEGS;
        glColor3f(rgb[0] * strength, rgb[1] * strength, rgb[2] * strength);
        glVertex3f(c[0] + d[j][0] * r[j] * 0.55f, c[1] + d[j][1] * r[j] * 0.55f, c[2] + d[j][2] * r[j] * 0.55f);
        glColor3f(0, 0, 0);
        glVertex3f(c[0] + d[j][0] * r[j], c[1] + d[j][1] * r[j], c[2] + d[j][2] * r[j]);
    }
    glEnd();
    if (rim > 0) {                                                                       /* the lip: the wood that split away is paler than the face */
        static const float pale[3] = { 1.0f, 0.88f, 0.66f };
        glBlendFunc(GL_ONE, GL_ONE);
        for (int band = 0; band < 2; band++) {                                           /* 0.7r -> r -> 1.25r, brightest on the rim itself */
            glBegin(GL_TRIANGLE_STRIP);
            for (int i = 0; i <= GOUGE_SEGS; i++) { int j = i % GOUGE_SEGS;
                for (int e = 0; e < 2; e++) {
                    float k = band == 0 ? (e ? 1.0f : 0.7f) : (e ? 1.25f : 1.0f), w = (e == 0) == (band != 0) ? rim : 0.0f;
                    glColor3f(pale[0] * w, pale[1] * w, pale[2] * w);
                    glVertex3f(c[0] + d[j][0] * r[j] * k, c[1] + d[j][1] * r[j] * k, c[2] + d[j][2] * r[j] * k);
                }
            }
            glEnd();
        }
    }
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}
/* one chip: a dart, long along v and narrow along u, pointed at the leading end and broken off square at the other.
 * Lighting is off in this pass (hud_world_sprites_begin), so the chip shades itself on a fixed key direction -
 * without that a tumbling chip is a flat silhouette that blinks as it turns edge on. */
void hud_world_chip(const float *c, const float *u, const float *v, const float *rgb, float alpha)
{
    if (!H.ok || alpha <= 0) return;
    static const float key[3] = { 0.35f, 0.87f, 0.34f };
    float nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2], nz = u[0] * v[1] - u[1] * v[0];
    float l = (float)sqrt(nx * nx + ny * ny + nz * nz);
    float dp = l > 1e-6f ? (nx * key[0] + ny * key[1] + nz * key[2]) / l : 0.0f;
    float sh = 0.45f + 0.55f * (float)fabs(dp);
    glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(rgb[0] * sh, rgb[1] * sh, rgb[2] * sh, alpha);
    glBegin(GL_QUADS);
    glVertex3f(c[0] + v[0], c[1] + v[1], c[2] + v[2]);
    glVertex3f(c[0] + u[0] - v[0] * 0.35f, c[1] + u[1] - v[1] * 0.35f, c[2] + u[2] - v[2] * 0.35f);
    glVertex3f(c[0] - v[0] * 0.9f, c[1] - v[1] * 0.9f, c[2] - v[2] * 0.9f);
    glVertex3f(c[0] - u[0] - v[0] * 0.35f, c[1] - u[1] - v[1] * 0.35f, c[2] - u[2] - v[2] * 0.35f);
    glEnd();
    glColor4f(1, 1, 1, 1); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}
void hud_world_ribbon(const float *a, const float *b, const float *eye, float hw, const float *rgb_a, const float *rgb_b)
{
    if (!H.ok) return;
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, m[3] = { (a[0] + b[0]) * 0.5f - eye[0], (a[1] + b[1]) * 0.5f - eye[1], (a[2] + b[2]) * 0.5f - eye[2] };
    float s[3] = { d[1] * m[2] - d[2] * m[1], d[2] * m[0] - d[0] * m[2], d[0] * m[1] - d[1] * m[0] };
    float l = (float)sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) s[i] *= hw / l;
    glBlendFunc(GL_ONE, GL_ONE); glDisable(GL_ALPHA_TEST);
    if (H.fx[0]) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, H.fx[0]); } else glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor3f(rgb_a[0], rgb_a[1], rgb_a[2]);
    glTexCoord2f(0, 0); glVertex3f(a[0] - s[0], a[1] - s[1], a[2] - s[2]);
    glTexCoord2f(0, 1); glVertex3f(a[0] + s[0], a[1] + s[1], a[2] + s[2]);
    glColor3f(rgb_b[0], rgb_b[1], rgb_b[2]);
    glTexCoord2f(1, 1); glVertex3f(b[0] + s[0], b[1] + s[1], b[2] + s[2]);
    glTexCoord2f(1, 0); glVertex3f(b[0] - s[0], b[1] - s[1], b[2] - s[2]);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}
