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
    GLuint fx[20];                                        /* bank 0 images 0, 4, 6: ribbon, flash, bolt (docs/PROJECTILES.md); 5, 10, 11: glow and the two death stars (docs/PERSO_DEATH.md 7); 12, 14, 31, 32: explosion flash, smoke, flame, exhaust glow, shared by the rocket (docs/ROCKET.md 5) and the missiles (docs/PROJECTILES.md 5.3); slot 10 = the footstep mark (docs/FOOTSTEPS.md); slot 11 = image 58, the wake on the water (docs/WATER.md 4.1); 12 = image 18, the spark of a bomb's fuse, 13 = image 24, the smoke of the bomb blast (docs/BOMB.md 3.4, 4.3); 14 = image 57, the drop of the water splash (docs/SPLASH.md 4); 15..17 = images 7, 8, 9, the hit star (0x4750e0), 18 = image 33, the fire ring of the special attack (docs/PERSO_SPECIAL.md 3), 19 = image 30, a segment of the storm's lightning bolt (docs/STORM.md 5) */
    GLuint beam;                                          /* bank 0 image 1: the line texture */
    GLuint bonus[5]; float sr[3], su[3];                  /* bank 0 images 19, 21, 20, 46, 23 (jump table 0x479654) */
    GLuint env[4];                                        /* bank 0 images 53..56: the butterflies of the environment instances (0x47e050 picks one of the four) */
    GLuint bub[9];                                        /* bank 0 images 44..52: the speech bubble and its contents (0x478980); 46 is bonus[3] */
    GLuint logo; int logo_w, logo_h; float logo_v, menu_t;   /* level bank image 1 (the title logo in House.rck); fade value 0..5 */
    GLuint sheet; int sheet_w, sheet_h;                   /* level bank image 0 (House and the three hubs carry the same one): the save-slot panel, ring and cross */
    struct { int state, n; float t, size; uint32_t id[3]; float x[3], y[3]; float rect[4]; } box;
    float iris_kx, iris_ky;                               /* hud_iris: virtual units per round pixel on this window (1, 1 at 4:3) */
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
static int fx_slot(int image) { return image == 0 ? 0 : image == 4 ? 1 : image == 6 ? 2 : image == 5 ? 3 : image == 10 ? 4 : image == 11 ? 5 : image == 12 ? 6 : image == 14 ? 7 : image == 31 ? 8 : image == 32 ? 9 : image == hud_step_image() ? 10 : image == 0x3a ? 11 : image == 18 ? 12 : image == 24 ? 13 : image == 57 ? 14 : image == 7 ? 15 : image == 8 ? 16 : image == 9 ? 17 : image == 33 ? 18 : image == 30 ? 19 : -1; }
static void common_item(int type, int index, const uint8_t *d, uint32_t size)
{
    static const int bonus_img[5] = { 19, 21, 20, 46, 23 };
    if (type == 1 && (index == 0 || index == 4 || index == 6 || index == 0x3a) && getenv("WOODY_FXLOG") && size >= 8) { int w = d[0] | d[1] << 8, h = d[2] | d[3] << 8; unsigned long sum = 0, sa = 0; for (int i = 0; i < w * h; i++) { sum += d[8 + i * 4] + d[9 + i * 4] + d[10 + i * 4]; sa += d[11 + i * 4]; } printf("fx image %d: %dx%d bpp %d mean rgb %.1f mean a %.1f", index, w, h, d[4], sum / (3.0 * w * h), sa / (1.0 * w * h)), puts(""); }
    if (type == 1 && fx_slot(index) >= 0) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.fx[fx_slot(index)] = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1 && index == 1) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.beam = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
    if (type == 1 && index >= 44 && index <= 52 && index != 46) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.bub[index - 44] = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
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
    if (type == 1 && index == 0) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.sheet = H.img[0]; H.sheet_w = H.img_w[0]; H.sheet_h = H.img_h[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; }
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
    if (H.logo) glDeleteTextures(1, &H.logo); if (H.sheet) glDeleteTextures(1, &H.sheet);
    for (int i = 0; i < 5; i++) if (H.sky[i]) glDeleteTextures(1, &H.sky[i]);
    for (int i = 0; i < 5; i++) if (H.bonus[i]) glDeleteTextures(1, &H.bonus[i]);
    for (int i = 0; i < 4; i++) if (H.env[i]) glDeleteTextures(1, &H.env[i]);
    for (int i = 0; i < 9; i++) if (H.bub[i]) glDeleteTextures(1, &H.bub[i]);
    if (H.beam) glDeleteTextures(1, &H.beam);
    for (int i = 0; i < 20; i++) if (H.fx[i]) glDeleteTextures(1, &H.fx[i]);
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
    int mcharge; float mcharge_t;                                             /* hud+0x41: a charge was spent (0x462380 / 0x462020), its phase and hold timer */
    int prev_ok, prev_lives, prev_bonus, prev_charges; float prev_health;
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
    if (A.mcharge && !A.stage[2] && !A.fly[3].on) {                   /* 0x462020: only while the pickup sequence of the charge does not run; shows the OLD value */
        int old = s->charges + 1; const float *si = k_slot[4], *sp = k_slot[5];
        switch (A.mcharge) {
        case 1: if (!slide_tick(2, dt)) A.mcharge = 2; break;                                               /* the icon slides in */
        case 2: sprite(6, si[0], si[1]); if (!plate_tick(2, dt)) A.mcharge = 3; break;                      /* the round plate grows */
        case 3: sprite(6, si[0], si[1]); sprite(8, sp[0], sp[1]); if (!pop_tick(2, old, dt)) A.mcharge = 4; break;   /* 17 -> 37 -> 17 */
        case 4: sprite(6, si[0], si[1]); sprite(8, sp[0], sp[1]); number_centred(k_anchor[2][0], k_anchor[2][1], old);
                pop_start(2, 1, 17.0f, 0.0f, 0.2f); slide_start(2, 6, si[0], si[1], -k_spr[6].w, si[1]); plate_start(2, sp[0], sp[1], 34.0f, 34.0f, 0, 0);
                A.mcharge = 5; A.mcharge_t = 0; break;
        case 5: sprite(6, si[0], si[1]); sprite(8, sp[0], sp[1]); number_sized(k_anchor[2][0], k_anchor[2][1], old, 17.0f);
                if ((A.mcharge_t += dt) > 1.0f) A.mcharge = 6; break;
        case 6: sprite(6, si[0], si[1]); sprite(8, sp[0], sp[1]); if (!pop_tick(2, old, dt)) A.mcharge = 7; break;   /* the number shrinks away */
        case 7: sprite(6, si[0], si[1]); if (!plate_tick(2, dt)) A.mcharge = 8; break;
        default: if (!slide_tick(2, dt)) A.mcharge = 0; break;
        }
    }
    ghosts_draw(dt);
    /* the setters 0x448380 / 0x4482c0 watch the values themselves; the port does the same by comparing frames */
    if (A.prev_ok && !s->race) {
        if (s->bonus < A.prev_bonus && !A.sw.on) hud_anim_reward(!(A.prev_health < 5.0f), A.prev_health);
        if (s->lives < A.prev_lives && !A.mlives) { A.mlives = 1; pop_start(3, 2, 17.0f, 37.0f, 0.2f); }
        if (s->charges < A.prev_charges && !A.mcharge) {               /* 0x448300 -> 0x462380 */
            slide_start(2, 6, -k_spr[6].w, k_slot[4][1], k_slot[4][0], k_slot[4][1]); plate_start(2, k_slot[5][0], k_slot[5][1], 0, 0, 34.0f, 34.0f);
            pop_start(2, 2, 17.0f, 37.0f, 0.2f); A.mcharge = 1;
        }
    }
    A.prev_ok = 1; A.prev_lives = s->lives; A.prev_bonus = s->bonus; A.prev_health = s->health; A.prev_charges = s->charges;
}

void hud_begin(int win_w, int win_h)
{
    glViewport(0, 0, win_w, win_h);
    { float sx = win_w / 640.0f, sy = win_h / 480.0f, s = sx > sy ? sx : sy; H.iris_kx = sx > 0 ? s / sx : 1; H.iris_ky = sy > 0 ? s / sy : 1; }
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

/* ---------------------------------------------------------------- boss bar (docs/HUD_TEXT.md 4.4): object ctor 0x47abe0, start 0x47aca0,
 * drawer 0x47b0b0(cur, max). X = 559 - 3, row y 424; the max slots (sprite 14) with alpha 30..128 across the row and the red end
 * rect slide in from the right edge in 2.0 s (+0x24), then the Buzz face (sprite 12) grows in 0.2 s (0x47bf70 / 0x47bff0) and
 * only after that the cur balls (sprite 13) are drawn. t = seconds since the bar was switched on. */
void hud_boss_bar(int cur, int max, float t)
{
    if (!H.ok || max <= 0) return;
    const float X = 556, Y = 424, W = 19.0f * max, D = W + (640.0f - X) + 2.0f;   /* +0x10 */
    float x = t < 2.0f ? X + D - t * D / 2.0f : X;
    for (int i = 1; i <= max; i++) {                                                                           /* 0x47ad10 */
        float xi = x - 19.0f * i - 2.0f; float al = (xi - X + W) * 98.0f / W + 30.0f, ar = (xi + 19.0f - X + W) * 98.0f / W + 30.0f;
        if (al < 0) al = 0; if (al > 255) al = 255; if (ar < 0) ar = 0; if (ar > 255) ar = 255;
        sprite_part(14, xi, Y, 1e9f, (uint32_t)al << 24 | 0x808080, (uint32_t)ar << 24 | 0x808080);
    }
    quad(x - 2.0f, Y, 640.0f - X, 15, 0, 0, 0, 0, 0, 0x80ff0000, 0x80ff0000, 0x80ff0000, 0x80ff0000);
    if (t < 2.0f) return;
    const float FX = 559, FY = 400; float g = (t - 2.0f) / 0.2f;
    if (g < 1.0f) { sprite_rect(12, FX + 32.0f * (1 - g), FY + 32.0f * (1 - g), 64.0f * g, 64.0f * g); return; }   /* 0x47bff0 still running: no balls yet */
    sprite(12, FX, FY);                                                                                        /* 0x47aee0 */
    for (int i = 1; i <= cur && i <= max; i++) sprite(13, X - 19.0f * i, Y);                                   /* 0x47afb0 */
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

/* ---------------------------------------------------------------- menu pages (docs/TITLE.md 5, MENU_NEWGAME.md 2, MENU_OPTIONS.md 3)
 * The common page class 0x445e30: 0x4464f0 ticks the blink phase once per frame, draws the page (vt[1], mostly the
 * list 0x446640), then the logo (0x446b00). The page logic itself lives in main_engine.c; these are its pieces. */
void hud_title_reset(void) { H.logo_v = 0; H.menu_t = 0; }
void hud_menu_tick(float dt) { H.menu_t += dt; if (H.menu_t > 0.5f) H.menu_t = 0; }   /* [0x5d7b1c], back to 0 above 0.5 (0x4b39a4) */
void hud_menu_blink(float t) { H.menu_t = t; }                                        /* up: 0, down: 0 or 0.25 (nothing to move to), slider step: 0.25 */
void hud_logo_off(void) { H.logo_v = 0; }                                             /* New game, Load game, pages 0x18/0x19/0x1e/0x1f */

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
/* the item list 0x446640: one size S for the whole page, shrunk (-1, down to 15) until every item NAME is under 640
 * wide; from y = yfrac * 480 one cell per item. Headers (flag 2) always show, the other items only once the input
 * delay page+8 has run out (`ready`) - the loop stops at the first one. The selected item is left out while the
 * blink phase is under 0.25 s: the original has no cursor and no colour difference. A slider (flag 0x10) reads
 * "name value%" (0x4467e0: the space from string 40, the "%" is string 7) and is centred as a whole. */
void hud_menu_items(const MenuItem *it, int n, float yfrac, int sel, int ready)
{
    if (!H.ok) return;
    float S = 30.0f;                                                  /* 0x4b39a8 */
    for (int i = 0; i < n; i++) {
        const uint16_t *s = hud_string(it[i].id); if (!s) continue;
        while (S > 15.0f) { font_size(S); if (font_measure(s) < 640.0f) break; S -= 1.0f; }
    }
    float y = yfrac * 480.0f;
    for (int i = 0; i < n; i++) {
        if (!(it[i].flags & 2) && !ready) break;
        font_size(it[i].flags & 0x20 ? S * 0.8f : S);
        row_reset(); row_str(it[i].id);
        if (it[i].flags & 0x10) { row_space(); row_num(it[i].value); row_str(7); }
        float w = font_measure(g_row);
        float x = (it[i].flags & 4) ? 640.0f - w - 6.4f : (it[i].flags & 8) ? 6.4f : (it[i].flags & 0x80) ? 160.0f - w * 0.5f : 320.0f - w * 0.5f;
        if (!(i == sel && H.menu_t < 0.25f)) font_draw(x, y, g_row, 0xff808080);
        y += font_cell();                                             /* CellH 0x441980 + Extra 0x441a50 (0) */
    }
    font_size(17.0f);
}

void hud_menu_page(const uint32_t *ids, int n, float yfrac, int sel, float dt)
{
    MenuItem it[8]; if (n > 8) n = 8;
    for (int i = 0; i < n; i++) { it[i].id = ids[i]; it[i].flags = 1; it[i].value = 0; }
    hud_menu_tick(dt);
    hud_menu_items(it, n, yfrac, sel, 1);
}

/* the logo 0x446b00, drawn after the page, only in House: level bank image 1, source 0,0,209,247 at (216,16) with
 * alpha trunc(50.8 v); pages 0 and 1 first add 10 dt (0x446ac0, up to 5), and every frame takes 5 dt off after the
 * draw. Net: 1 s in on pages 0 and 1, 1 s out on every other page. */
void hud_logo(int grow, float dt)
{
    if (!H.ok) return;
    if (grow) { H.logo_v += 10 * dt; if (H.logo_v > 5) H.logo_v = 5; }
    if (H.logo && H.logo_v > 0) {
        uint32_t c = (uint32_t)(50.8f * H.logo_v) << 24 | 0x808080;
        quad(216, 16, 209, 247, H.logo, 0, 0, 209.0f / H.logo_w, 247.0f / H.logo_h, c, c, c, c);
    }
    H.logo_v -= 5 * dt; if (H.logo_v < 0) H.logo_v = 0;
}

/* the iris 0x4776d0 (docs/MENU_NEWGAME.md 2.7): an opaque black ring of 50 segments around (320, 240), inner radius
 * 0.99 * 480 * v, outer 0.99 * 480 - the corners are 400 away, so v = 0.85 shows nothing and v = 0 is all black.
 * 0x482cf0 clips to the virtual screen; the viewport does that here. The original ran at 4:3; on another window shape
 * the stretched virtual screen would make an ellipse, so the ring stays round and takes the larger of the two scales
 * (v = 1 still opens it completely). */
void hud_iris(float v)
{
    if (!H.ok) return;
    const float r = 0.99f * 480.0f, ri = r * v, kx = H.iris_kx ? H.iris_kx : 1, ky = H.iris_ky ? H.iris_ky : 1;
    glDisable(GL_TEXTURE_2D); glColor4f(0, 0, 0, 1); glBegin(GL_QUADS);
    for (int k = 0; k < 50; k++) {
        float a0 = k * (6.2831853f / 50), a1 = (k + 1) * (6.2831853f / 50), c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        glVertex2f(320 + ri * kx * c0, 240 + ri * ky * s0); glVertex2f(320 + ri * kx * c1, 240 + ri * ky * s1);
        glVertex2f(320 + r * kx * c1, 240 + r * ky * s1);   glVertex2f(320 + r * kx * c0, 240 + r * ky * s0);
    }
    glEnd();
}

/* a flat colour over the whole virtual screen: 0x80000000 is the half-black backdrop of a menu page in a level (0x404f1a) */
void hud_rect(uint32_t argb) { if (H.ok) quad(0, 0, 640, 480, 0, 0, 0, 0, 0, argb, argb, argb, argb); }

static void fit_size(const uint16_t *s, float S, float maxw, float minS) { font_size(S); while (S > minS && font_measure(s) > maxw) font_size(S -= 1.0f); }   /* 0x45dc90 */
static void sheet_quad(float x, float y, float w, float h, float sx, float sy, float sw, float sh, uint32_t c, int additive)
{
    if (!H.sheet) return;
    if (additive) glBlendFunc(GL_ONE, GL_ONE);                        /* flag 4: the neon panel and the ring have alpha 0 in the data */
    quad(x, y, w, h, H.sheet, sx / H.sheet_w, sy / H.sheet_h, (sx + sw) / H.sheet_w, (sy + sh) / H.sheet_h, c, c, c, c);
    if (additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* the save-slot list of pages 2 and 5 (docs/MENU_LOAD.md 2.2, 3): four panels in the corners, sliding in from the
 * sides, and the page title (25 "Select game" / 24 "Select save") coming up from below. */
void hud_slot_list(const HudSlots *s, float dt)
{
    static const float SX[4] = { 16, 488, 16, 488 }, SY[4] = { 48, 48, 324, 324 };   /* table 0x4ab420 */
    static float blink;                                                                /* page+0x40 */
    if (!H.ok) return;
    blink += dt; if (blink > 0.6f) blink = 0;
    for (int i = 0; i < 4; i++) {                                                      /* 0x45d530(i + 1, xoff) */
        int sel = s->sel == i + 1; uint32_t c = sel ? 0xfe808080 : 0xfe202020, tc = c;
        float x = SX[i] + ((i & 1) ? -s->slide : s->slide), y = SY[i];
        if (!sel) sheet_quad(x, y, 137, 108, 0, 0, 137, 108, c, 1);
        else if (blink > 0.3f) sheet_quad(x, y, 137, 108, 0, 0, 137, 108, 0xfe808080, 1);
        else tc = 0xfe202020;
        sheet_quad(x + 45, y + 50, 49, 49, 0, 108, 49, 49, c, 1);                     /* the gold ring */
        if (s->pct[i] && H.img[0]) {                                                   /* the faces: Common image 61, alpha (flag 8) */
            float W = (float)H.img_w[0], Hh = (float)H.img_h[0];
            quad(x + 33, y - 7, 64, 64, H.img[0], 0, 0, 64 / W, 64 / Hh, c, c, c, c);                                   /* Woody */
            if (s->open[i] & 1) quad(x - 4, y + 8, 64, 64, H.img[0], 0, 63 / Hh, 64 / W, 127 / Hh, c, c, c, c);          /* Knothead */
            if (s->open[i] & 2) quad(x + 69, y + 7, 64, 64, H.img[0], 63 / W, 0, 127 / W, 64 / Hh, c, c, c, c);         /* Splinter */
        }
        row_reset(); row_str(s->pct[i] ? 26 + i : 18);                                /* "Save N" / "FREE" (0x45d3c0) */
        fit_size(g_row, 28, 137, 10);
        float w = font_measure(g_row), ly = i < 2 ? SY[i] + 108 : SY[i] - font_cell();
        font_draw(x + 68.5f - w * 0.5f, ly, g_row, tc);
        row_reset(); row_num(s->pct[i]); row_str(7);                                    /* "NN%", red, also "0%" on a free slot */
        font_size(s->pct[i] >= 100 ? 12.0f : 16.0f);
        font_draw(x + 69.5f - font_measure(g_row) * 0.5f, y + 75 - font_cell() * 0.5f, g_row, sel ? 0xfeff1400 : 0xfe3f0500);
        if (s->cross && !s->pct[i]) sheet_quad(x + 20, y, 108, 108, 0, 160, 63, 63, 0xfe808080, 0);   /* page 2: the red cross over a free slot (0x45e050) */
    }
    row_reset(); row_str(s->title);
    fit_size(g_row, 30, 330, 10);
    float w = font_measure(g_row), h = font_cell(), x = 320 - w * 0.5f, y = 415 - s->slide;
    font_draw(x, y, g_row, 0xfe808080);                                                /* first plain, then orange-red slightly up and left over it */
    font_draw(x - 0.05f * h, y - 0.05f * h, g_row, 0xfe801400);
    font_size(17.0f);
}

/* ---------------------------------------------------------------- page 3, the world-select carousel (docs/MENU_LOAD.md 4.6) */
static void row_centred(float cx, float y, uint32_t col) { font_draw(cx - font_measure(g_row) * 0.5f, y, g_row, col); }
void hud_carousel(const HudCarousel *c)
{
    if (!H.ok) return;
    float off = c->off;
    if (c->stats) {
        /* 0x45f6f0: the portrait, the lives plate, the health dots (0x45ea00), $ and charge icons with their plates */
        sprite(c->face, 16 + off, 113); sprite(8, 56 + off, 153);
        for (int i = 0; i < (int)c->health; i++) sprite(7, 31 + off + 24.0f * i, 78);
        sprite(3, 22 + off, 213); sprite(8, 56 + off, 258); sprite(6, 22 + off, 308); sprite(8, 56 + off, 353);
        /* 0x45eb00 -> 0x45f2a0: red, size 17 centred on the plates; a value >= 100 sets 30 x 0.75 and that size stays for the next ones */
        const int v[3] = { c->lives - 1, c->unique, c->charges }; float S = 17.0f;
        for (int i = 0; i < 3; i++) {
            uint16_t s[16]; number_codes(s, v[i]); if (v[i] >= 100) S = 22.5f; font_size(S);
            font_draw(72 + off - font_measure(s) * 0.5f, 169 + 105.0f * i - font_cell() * 0.5f, s, 0xfeff0000);
        }
        /* 0x45f790(-off): centred on x 565 + off', every line shrunk to <= 115 wide */
        float cx = 565 - off;
        row_reset(); row_str(43); fit_size(g_row, 20, 115, 10); row_centred(cx, 110, 0xfeffffff);          /* "Game cleared" */
        float y = 110 + font_cell();
        row_reset(); row_num(c->pct); row_str(7); fit_size(g_row, 50, 115, 10); row_centred(cx, y, 0xfeff1400);   /* "NN%" */
        if (c->pct < 100) {
            row_reset(); row_str(44); fit_size(g_row, 20, 115, 10); row_centred(cx, 240, 0xfeffffff);      /* "Location" */
            row_reset(); if (c->world) row_str(c->world); fit_size(g_row, 25, 115, 10); row_centred(cx, 270, 0xfeff1400);
            y = 270 + font_cell();
            row_reset(); if (c->part) row_str(c->part); row_centred(cx, y, 0xfeff1400);                                 /* measured at the world's size, no fit of its own */
        }
    }
    if (c->list && c->items) hud_menu_items(c->items, c->nitems, c->yfrac, c->list_sel, 1);   /* 0x446640 */
    {   /* 0x45fe30: size 30, the grey copy 5 % of a cell right and down, the orange-red one on top */
        row_reset(); row_str(c->name); font_size(30.0f);
        float w = font_measure(g_row), h = font_cell(), x = 320 - w * 0.5f, y = 16 + off;
        font_draw(x + 0.05f * h, y + 0.05f * h, g_row, 0xfe808080);
        font_draw(x, y, g_row, 0xfe801400);
    }
    {   /* 0x45fac0: HUD sprite 15 (Common image 63, 0,96,31,31) doubled to 62x62, additive (flag 4); the left one mirrored */
        int i = k_spr[15].img;
        if (H.img[i]) {
            float W = (float)H.img_w[i], Hh = (float)H.img_h[i], u0 = (k_spr[15].x + 0.5f) / W, v0 = (k_spr[15].y + 0.5f) / Hh, u1 = (k_spr[15].x + k_spr[15].w - 0.5f) / W, v1 = (k_spr[15].y + k_spr[15].h - 0.5f) / Hh;   /* half a texel in: the row above is opaque white, which the bilinear filter smeared into a line over the arrow at 2x */
            uint32_t gr = (uint32_t)(int)(c->arrow_r + 0.5f) & 255, gl = (uint32_t)(int)(c->arrow_l + 0.5f) & 255;
            uint32_t cr = 0xfe000000u | gr << 16 | gr << 8 | gr, cl = 0xfe000000u | gl << 16 | gl << 8 | gl;
            glBlendFunc(GL_ONE, GL_ONE);
            quad(470 + c->arrow_s, 209, 62, 62, H.img[i], u0, v0, u1, v1, cr, cr, cr, cr);
            quad(108 - c->arrow_s, 209, 62, 62, H.img[i], u1, v0, u0, v1, cl, cl, cl, cl);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
    {   /* 0x45e8a0: 130 "Total Score :" size 15 at (16, 410), the sum 0x450a10 one cell below, both white */
        row_reset(); row_str(130); font_size(15.0f); font_draw(16, 410, g_row, 0xfeffffff);
        row_reset(); row_num(c->total); font_draw(16, 410 + font_cell(), g_row, 0xfeffffff);
    }
    font_size(17.0f);
}

/* ---------------------------------------------------------------- results screen, menu page 0x1e (docs/RESULTS.md)
 * The page object of vtable 0x4aa934: no panel, no backdrop and no OK item. Once shown (0x454560) a black iris closes
 * round the centre of the screen (1.0 -> 0.37 in 0.5 s), "RESULTS" slides in from the left, "HIGH SCORE" from the right
 * and the level name with "CLEARED!!" from below; then a column of lines on the left counts up one after the other
 * (0x454700 / race 0x454860): time, "+", enemies, "+", W bonuses, TOTAL, $. Each line slides in from -200 in 0.2 s,
 * then "=" on x 85 and a number counting up at 5000 points/s, right aligned so that the final value starts at x 100;
 * the time at which a line is done is the start of the next one. */
static struct {
    int shown, iris_on;                         /* +0x5c, +0x38 */
    float t, delay;                             /* +0x28 (time since enter / show / hide), +8 (input delay) */
    float iv0, iv1, it;                         /* iris 0x4776b0: from, to, time */
    float clock, end[6];                        /* +0x3c, +0x40 (start of line 0) and +0x44..+0x54 (-1 = not done) */
    int counting;                               /* a counter asked for the tick loop this frame (0x468e40) */
} RS;
void hud_results_enter(void)                    /* 0x4544b0 (+ 0x45b8c0: SoundFx 0x3f and the 0.5 s input delay are the caller's / here) */
{
    memset(&RS, 0, sizeof RS); RS.delay = 0.5f; RS.iv0 = RS.iv1 = 1.0f;
    for (int i = 1; i < 6; i++) RS.end[i] = -1.0f;
}
void hud_results_show(void) { if (RS.shown) return; RS.shown = 1; RS.t = 0; RS.delay = 0.5f; RS.iv0 = 1.0f; RS.iv1 = 0.37f; RS.it = 0; RS.iris_on = 1; }   /* 0x454560 -> 0x4544f0 */
void hud_results_hide(void) { if (!RS.shown) return; RS.shown = 0; RS.t = 0; RS.delay = 0.5f; RS.iv0 = 0.37f; RS.iv1 = 1.0f; RS.it = 0; RS.iris_on = 1; }   /* 0x454580 -> 0x454530 */
int hud_results_confirm(void)                   /* 0x4545a0; 1 = everything has been counted (result 5) */
{
    if (RS.delay > 0) return 0;
    if (RS.end[5] > -1) return RS.shown;
    if (RS.t > 0.5f) { float k = RS.clock; RS.clock += 60.0f; for (int i = 0; i < 6; i++) RS.end[i] = k; }   /* still counting: all lines done at once */
    return 0;
}

static const float k_res_row[9][2] = { { 16, 16 }, { 45, 106 }, { 45, 215 }, { 45, 341.44f }, { 45, 291.44f }, { 45, 375 }, { 85, 405 }, { 624, 16 }, { 320, 415 } };   /* 0x4b5748 (0x4543c0) */
static void res_text(float x, float y, uint32_t col, int shadow)                  /* g_row; the shadow is the same text in white 0.05 cell down right, first */
{
    if (shadow) { float d = 0.05f * font_cell(); font_draw(x + d, y + d, g_row, 0xfe808080); }
    font_draw(x, y, g_row, col);
}
static void res_icon(int n, float xoff)                                           /* the six icons of 0x4542f0, all centred on x 45 */
{
    switch (n) {
    case 0: case 1:                                                               /* clock / enemy face: hub bank image 1, additive (flag 4) */
        if (!H.logo) break;
        { float sx = n ? 0 : 51, sw = n ? 50 : 36, x = n ? 20 : 27, y = n ? 170 : 75;
          glBlendFunc(GL_ONE, GL_ONE);
          quad(x + xoff, y, sw, sw, H.logo, sx / H.logo_w, 0, (sx + sw) / H.logo_w, sw / H.logo_h, 0xfe808080, 0xfe808080, 0xfe808080, 0xfe808080);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
        break;
    case 2: sprite_rect(4, 9 + xoff, 275, 71, 71); break;                         /* the big W, 94 x 0.76 */
    case 3: sprite_rect(5, 9 + xoff, 225, 71, 71); break;                         /* the flag (race) */
    case 4: glBlendFunc(GL_ONE, GL_ONE); quad(16 + xoff, 370, 140, 2, 0, 0, 0, 0, 0, 0xfe808080, 0xfe808080, 0xfe808080, 0xfe808080); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;   /* the bar above TOTAL */
    case 5: sprite_rect(3, 21 + xoff, 410, 49, 49); break;                        /* $, 64 x 0.76 */
    }
}
static float res_slide(float start) { float d = RS.clock - start; return d < 0.2f ? (0.2f - d) * -1000.0f : 0; }   /* 0x454f20 */
static void res_line(float xoff, int n, float size, uint32_t col, int shadow)    /* 0x4549c0: icon n, the label in g_row (empty: none) on row n + 1 */
{
    font_size(size); res_icon(n, xoff);
    if (g_rown) res_text(45 - font_measure(g_row) * 0.5f + xoff, k_res_row[n + 1][1], col, shadow);
}
static float res_count(float start, int value, int row, float size, uint32_t col, int shadow)   /* 0x454f60: "=" and the counter; the time it is done or -1 */
{
    if (start + 0.2f > RS.clock) return -1.0f;
    font_size(size);
    const float x = k_res_row[6][0], y = k_res_row[row][1];
    row_reset(); row_str(8); font_draw(x - font_measure(g_row) * 0.5f, y, g_row, 0xfe808080);
    row_reset(); row_num(value); float right = x + font_measure(g_row) + 15.0f;
    int n = (int)lrintf((RS.clock - start - 0.2f) * 5000.0f); float done = -1.0f;
    if (n > value) { n = value; done = RS.clock; }
    row_reset(); row_num(n); res_text(right - font_measure(g_row), y, col, shadow);
    if (done == -1.0f) RS.counting = 1;
    return done;
}
static void res_plus(int k) { font_size(15.0f); row_reset(); row_str(11); font_draw(45 - font_measure(g_row) * 0.5f, k_res_row[k][1] + 30.0f, g_row, 0xfe808080); }   /* 0x454920 */
static void res_ratio(int a, int b) { row_reset(); row_num(a); row_str(9); row_num(b); }                                              /* "a/b" */
static int res_bonus(int got, int total) { int v = got * 100; return got == total ? v + v / 2 : v; }                                 /* 0x453d50 / 0x453d20 */
static float res_total(const HudResults *r, float start)                          /* 0x455580 */
{
    float S = 15.0f; const uint16_t *s = hud_string(14);
    if (s) { fit_size(s, S, 65.0f, 10.0f); S = H.k * (H.H - H.B); }
    row_reset(); row_str(14); res_line(res_slide(start), 4, S, 0xfeff0000, 1);
    int v = r->race ? res_bonus(r->st[3], r->st[1]) : (r->time < 1800 ? 1800 - (int)r->time : 0) * 10 + res_bonus(r->st[2], r->st[0]) + res_bonus(r->st[3], r->st[1]);
    return res_count(start, v, 5, S, 0xfe808080, 0);
}
static float res_dollar(const HudResults *r, float start)                          /* 0x455650: the number of new unique items, big and red */
{
    row_reset(); res_line(res_slide(start), 5, 0, 0xfe808080, 0);
    if (start + 0.2f <= RS.clock) { font_size(40.0f); row_reset(); row_str(8); font_draw(k_res_row[6][0] - font_measure(g_row) * 0.5f, k_res_row[6][1], g_row, 0xfe808080); }
    return res_count(start + 0.4f, r->cats, 6, 40.0f, 0xfeff0000, 1);
}
static void res_lines(const HudResults *r, float dt)                              /* 0x454700 (normal) / 0x454860 (race) */
{
    RS.clock += dt;
    float *e = RS.end;
    if (!r->race) {
        int t = (int)r->time;                                                     /* _ftol */
        row_reset(); row_num(t / 60); row_str(10); if (t % 60 < 10) row_num(0); row_num(t % 60);
        res_line(res_slide(e[0]), 0, 15.0f, 0xfe808080, 0);
        { float d = res_count(e[0], (t < 1800 ? 1800 - t : 0) * 10, 1, 15.0f, 0xfe808080, 0); if (e[1] == -1.0f) e[1] = d; }
        if (e[1] <= -1.0f) return;
        res_plus(1);
        res_ratio(r->st[2], r->st[0]); res_line(res_slide(e[1]), 1, 15.0f, 0xfe808080, 0);
        { float d = res_count(e[1], res_bonus(r->st[2], r->st[0]), 2, 15.0f, 0xfe808080, 0); if (e[2] == -1.0f) e[2] = d; }
        if (e[2] <= -1.0f) return;
        res_plus(2);
        res_ratio(r->st[3], r->st[1]); res_line(res_slide(e[2]), 2, 15.0f, 0xfe808080, 0);
        { float d = res_count(e[2], res_bonus(r->st[3], r->st[1]), 3, 15.0f, 0xfe808080, 0); if (e[3] == -1.0f) e[3] = d; }
    } else {                                                                      /* the flag line starts at +0x48 = -1: no slide, the counter is as good as done */
        res_ratio(r->st[3], r->st[1]); res_line(res_slide(e[2]), 3, 15.0f, 0xfe808080, 0);
        { float d = res_count(e[2], res_bonus(r->st[3], r->st[1]), 4, 15.0f, 0xfe808080, 0); if (e[3] == -1.0f) e[3] = d; }
    }
    if (e[3] <= -1.0f) return;
    { float d = res_total(r, e[3]); if (e[4] == -1.0f) e[4] = d; }
    if (e[4] <= -1.0f) return;
    { float d = res_dollar(r, e[4]); if (e[5] == -1.0f) e[5] = d; }
}
static void res_name(int level, uint32_t *a, uint32_t *b)                        /* 0x4559b0, table 0x455b60: 47 Space / 48 Pirate / 49 House / 50 Mini Game, 51..54 Part A..D, 55 Race */
{
    static const unsigned char k[24][2] = {
        { 47, 51 }, { 47, 52 }, { 48, 51 }, { 48, 52 }, { 48, 53 }, { 49, 51 }, { 49, 52 }, { 49, 53 }, { 49, 54 },   /* W1A .. W3D (2..10) */
        { 1, 1 }, { 47, 51 }, { 47, 55 }, { 48, 51 }, { 48, 55 }, { 49, 51 }, { 49, 55 },                             /* KWS, K1A .. K3R (11..17) */
        { 1, 1 }, { 47, 51 }, { 47, 55 }, { 48, 51 }, { 48, 55 }, { 49, 51 }, { 49, 55 }, { 50, 1 } };               /* SWS, S1A .. S3R, BlackBox (18..25) */
    *a = *b = 1;
    if (level >= 2 && level <= 25) { *a = k[level - 2][0]; *b = k[level - 2][1]; }
}
int hud_results_draw(const HudResults *r, float dt)
{
    RS.counting = 0;
    if (!H.ok) return 0;
    RS.t += dt; if (RS.delay > 0) RS.delay -= dt;
    if (RS.iris_on) { RS.it += dt; float f = RS.it / 0.5f; if (f > 1) f = 1; hud_iris(RS.iv0 - (RS.iv0 - RS.iv1) * f); }   /* 0x477920, round (320, 240) */
    if (!RS.shown) return 0;
    float off;
    if (RS.t <= 0.5f) off = (0.5f - RS.t) * -300.0f / 0.5f;                       /* -300 -> 0; the lines wait */
    else { off = 0; res_lines(r, dt); }
    font_size(25.0f); row_reset(); row_str(13); res_text(16 + off, 16, 0xfe800000, 1);                                  /* RESULTS 0x455790 */
    font_size(18.0f); row_reset(); row_str(17);                                                                          /* HIGH SCORE 0x455850 */
    { float wl = font_measure(g_row); font_draw(624 - wl - off, 16, g_row, 0xfe808080);
      row_reset(); row_num(r->best); font_draw(624 - wl * 0.5f - off - font_measure(g_row) * 0.5f, 16 + font_cell(), g_row, 0xfe808080); }   /* the best score saved BEFORE this run */
    uint32_t na, nb; res_name(r->level, &na, &nb);                                                                       /* 0x455bc0 */
    row_reset(); row_str(na); row_space(); row_str(nb);
    { float S = 15.0f; font_size(S); while (S > 10.0f && font_measure(g_row) > 400.0f) font_size(S -= 1.0f);
      font_draw(320 - font_measure(g_row) * 0.5f, 415 - off, g_row, 0xfe808080);
      float y = 415 + font_cell() - off;
      font_size(20.0f); row_reset(); row_str(12); res_text(320 - font_measure(g_row) * 0.5f, y, 0xfe801400, 1); }     /* CLEARED!! */
    font_size(17.0f);
    return RS.counting;
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
/* the comic speech bubble 0x478980 (docs/PERSO_DEATH.md 4.1): bank 0 image 44 = the balloon, 45..52 = what is in it.
 * Sprite flags 0x49: camera facing (bit 0), alpha blended instead of additive (bit 3), mirror flags applied (bit 6);
 * mirror value 2 (0x470d80 case 2) swaps u, so the tail points the other way. Standard colour (bit 1 off) = white.
 * `size` is the half diagonal, as for every sprite (0x470fee). Between hud_world_sprites_begin/end. */
void hud_world_bubble(int image, const float *pos, float size, int mirror)
{
    GLuint t = image == 46 ? H.bonus[3] : image >= 44 && image <= 52 ? H.bub[image - 44] : 0;
    if (!H.ok || !t || size <= 0) return;
    float h = size * 0.70710678f, u0 = mirror ? 1.0f : 0.0f, u1 = 1.0f - u0;
    glBindTexture(GL_TEXTURE_2D, t); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, 0); glVertex3f(pos[0] - H.sr[0] * h + H.su[0] * h, pos[1] - H.sr[1] * h + H.su[1] * h, pos[2] - H.sr[2] * h + H.su[2] * h);
    glTexCoord2f(u0, 1); glVertex3f(pos[0] - H.sr[0] * h - H.su[0] * h, pos[1] - H.sr[1] * h - H.su[1] * h, pos[2] - H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(u1, 1); glVertex3f(pos[0] + H.sr[0] * h - H.su[0] * h, pos[1] + H.sr[1] * h - H.su[1] * h, pos[2] + H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(u1, 0); glVertex3f(pos[0] + H.sr[0] * h + H.su[0] * h, pos[1] + H.sr[1] * h + H.su[1] * h, pos[2] + H.sr[2] * h + H.su[2] * h);
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

static void world_line_uv(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b, GLuint tex, int flip);
static void world_line(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b, GLuint tex)
{
    world_line_uv(a, b, eye, hw, rgb, alpha_a, alpha_b, tex, 0);
}
/* flip = the uv mode of 0x470d80: bit 0 mirrors v (across the line), bit 1 mirrors u (along it) */
static void world_line_uv(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b, GLuint tex, int flip)
{
    if (!H.ok) return;
    float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, m[3] = { (a[0] + b[0]) * 0.5f - eye[0], (a[1] + b[1]) * 0.5f - eye[1], (a[2] + b[2]) * 0.5f - eye[2] };
    float s[3] = { d[1] * m[2] - d[2] * m[1], d[2] * m[0] - d[0] * m[2], d[0] * m[1] - d[1] * m[0] };      /* perpendicular to the segment and to the view ray */
    float l = (float)sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); if (l < 1e-6f) return;
    for (int i = 0; i < 3; i++) s[i] *= hw / l;
    glBlendFunc(GL_ONE, GL_ONE); glDisable(GL_ALPHA_TEST);                          /* flag 4 = additive ONE/ONE */
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glColor3f(rgb[0] * alpha_a, rgb[1] * alpha_a, rgb[2] * alpha_a);
    float u0 = flip & 2 ? 1.0f : 0.0f, u1 = 1.0f - u0, v0 = flip & 1 ? 1.0f : 0.0f, v1 = 1.0f - v0;
    glTexCoord2f(u0, v0); glVertex3f(a[0] - s[0], a[1] - s[1], a[2] - s[2]);
    glTexCoord2f(u0, v1); glVertex3f(a[0] + s[0], a[1] + s[1], a[2] + s[2]);
    glColor3f(rgb[0] * alpha_b, rgb[1] * alpha_b, rgb[2] * alpha_b);
    glTexCoord2f(u1, v1); glVertex3f(b[0] + s[0], b[1] + s[1], b[2] + s[2]);
    glTexCoord2f(u1, v0); glVertex3f(b[0] - s[0], b[1] - s[1], b[2] - s[2]);
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); glEnable(GL_TEXTURE_2D);
}
void hud_world_beam(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b) { world_line(a, b, eye, hw, rgb, alpha_a, alpha_b, H.beam); }
void hud_world_line(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b) { world_line(a, b, eye, hw, rgb, alpha_a, alpha_b, 0); }
void hud_world_quad(int image, const float v[4][3], const float uv[4][2], const float rgb[4][3])
{
    int k = fx_slot(image); if (!H.ok || k < 0 || !H.fx[k]) return;
    glDisable(GL_ALPHA_TEST); glBindTexture(GL_TEXTURE_2D, H.fx[k]); glBlendFunc(GL_ONE, GL_ONE);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) { glColor3f(rgb[i][0], rgb[i][1], rgb[i][2]); glTexCoord2f(uv[i][0], uv[i][1]); glVertex3f(v[i][0], v[i][1], v[i][2]); }
    glEnd();
    glColor4f(1, 1, 1, 1); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST);
}
void hud_world_streak(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b) { int k = fx_slot(image); if (k >= 0 && H.fx[k]) world_line(a, b, eye, hw, rgb, alpha_a, alpha_b, H.fx[k]); }
void hud_world_streak_flip(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b, int flip) { int k = fx_slot(image); if (k >= 0 && H.fx[k]) world_line_uv(a, b, eye, hw, rgb, alpha_a, alpha_b, H.fx[k], flip); }

void hud_world_fx(int image, const float *pos, float size, float turns, const float *rgb, float alpha)
{
    int n = fx_slot(image), blend = image == 10 || image == 11 || image == 24; if (!H.ok || n < 0 || !H.fx[n] || alpha <= 0 || size <= 0) return;   /* the stars and the bomb smoke (sprite flag 8) are alpha blended, the rest additive */
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
    if (image == 24) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(rgb[0], rgb[1], rgb[2], alpha); }   /* the smoke ring of a bomb (flag 0xa: not additive) */
    else { glBlendFunc(GL_ONE, GL_ONE); glColor3f(rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha); }
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
