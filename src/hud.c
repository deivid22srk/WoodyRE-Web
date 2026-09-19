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
    GLuint beam;                                          /* bank 0 image 1: the line texture */
    GLuint bonus[5]; float sr[3], su[3];                  /* bank 0 images 19, 21, 20, 46, 23 (jump table 0x479654) */
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

static void common_item(int type, int index, const uint8_t *d, uint32_t size)
{
    static const int bonus_img[5] = { 19, 21, 20, 46, 23 };
    if (type == 1 && index == 1) { GLuint keep = H.img[0]; int kw = H.img_w[0], kh = H.img_h[0]; H.img[0] = 0; common_item(1, 61, d, size); H.beam = H.img[0]; H.img[0] = keep; H.img_w[0] = kw; H.img_h[0] = kh; return; }
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
    if (H.beam) glDeleteTextures(1, &H.beam);
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
static void number_centred(float cx, float cy, int v)                              /* 0x4605b0 / 0x448660 */
{
    uint16_t s[16]; number_codes(s, v);
    font_size(v >= 100 ? 17.0f * 0.75f : 17.0f);
    font_draw(cx - font_measure(s) * 0.5f, cy - font_cell() * 0.5f, s, 0xfeff0000);
    font_size(17.0f);
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
    sprite(8, 584, 70); number_centred(600, 86, s->lives > 0 ? s->lives - 1 : 0);                           /* slot 7, anchor A3 */
    sprite(s->race ? 5 : 4, 16, 16);                                                                        /* slot 0 */
    sprite(8, 16, 66); number_centred(32, 82, s->bonus);                                                    /* slot 1, A0 */
    if (s->show_total) {
        uint16_t t[40]; int n = number_codes(t, s->got); const uint16_t *sl = hud_string(9);                /* "/" */
        for (; sl && *sl && n < 20; sl++) t[n++] = *sl;
        number_codes(t + n, s->total);
        font_size(17.0f); font_draw(110, Y + 12 - font_cell() * 0.5f, t, 0xfe808080);
    }
    if (!s->race) for (int i = 1; i <= (int)s->health && i <= 5; i++) sprite(7, 559.0f - 29.0f * i, Y);
    if (s->extended) {
        sprite(3, 16, 136); sprite(8, 50, 181); number_centred(66, 197, s->unique);                         /* slots 2/3, A1 */
        sprite(6, 16, 221); sprite(8, 50, 266); number_centred(66, 282, s->charges);                        /* slots 4/5, A2 */
    }
    if (s->power > 0) {                                                                                     /* power gauge 0x447d70 */
        if (s->power >= 1.0f) { H.blink += dt; if (H.blink >= 0.3f) H.blink -= 0.3f; sprite(10, 0, 426); if (H.blink >= 0.15f) sprite(11, 0, 421); }
        else { float w = s->power * 104.0f; sprite_part(10, 0, 426, w, 0xfe808080, 0xfe808080); sprite_part(11, 0, 421, w, 0xfe808080, 0xfe808080); }
    } else H.blink = 0;
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

/* ---------------------------------------------------------------- House menu pages (docs/TITLE.md 5) */
void hud_title_reset(void) { H.logo_v = 0; H.menu_t = 0; }

void hud_title_draw(int page, int sel, int want_logo, float dt)
{
    if (!H.ok) return;
    static const uint32_t items1[4] = { 22, 23, 36, 2 };             /* New game, Load game, Options, Quit */
    H.menu_t += dt; if (H.menu_t >= 0.5f) H.menu_t -= 0.5f;
    int hide_sel = H.menu_t < 0.25f;                                  /* the selected item blinks at 2 Hz; no colour highlight, no cursor */
    font_size(30.0f);
    if (page == 0) {
        const uint16_t *s = hud_string(21);                           /* "Press a key", y fraction 0.7 */
        if (s && !hide_sel) font_draw(320 - font_measure(s) * 0.5f, 336, s, 0xff808080);
    } else if (page == 1) {
        for (int i = 0; i < 4; i++) {
            const uint16_t *s = hud_string(items1[i]);
            if (s && !(i == sel && hide_sel)) font_draw(320 - font_measure(s) * 0.5f, 264 + 46.5f * i, s, 0xff808080);
        }
    }
    if (H.logo && H.logo_v > 0) {                                     /* 0x446b00: alpha = 254 * v / 5, source 0,0,209,247 at (216,16) */
        uint32_t c = (uint32_t)(254.0f * H.logo_v / 5.0f) << 24 | 0x808080;
        quad(216, 16, 209, 247, H.logo, 0, 0, 209.0f / H.logo_w, 247.0f / H.logo_h, c, c, c, c);
    }
    if (want_logo) { H.logo_v += 5 * dt; if (H.logo_v > 5) H.logo_v = 5; } else H.logo_v = 0;
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
    float h = size * 0.5f, c[3] = { pos[0], pos[1] + 50.0f, pos[2] };               /* +50: 0x4a9030 */
    glBindTexture(GL_TEXTURE_2D, H.bonus[n]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(c[0] - H.sr[0] * h + H.su[0] * h, c[1] - H.sr[1] * h + H.su[1] * h, c[2] - H.sr[2] * h + H.su[2] * h);
    glTexCoord2f(0, 1); glVertex3f(c[0] - H.sr[0] * h - H.su[0] * h, c[1] - H.sr[1] * h - H.su[1] * h, c[2] - H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(1, 1); glVertex3f(c[0] + H.sr[0] * h - H.su[0] * h, c[1] + H.sr[1] * h - H.su[1] * h, c[2] + H.sr[2] * h - H.su[2] * h);
    glTexCoord2f(1, 0); glVertex3f(c[0] + H.sr[0] * h + H.su[0] * h, c[1] + H.sr[1] * h + H.su[1] * h, c[2] + H.sr[2] * h + H.su[2] * h);
    glEnd();
}
void hud_world_sprites_end(void) { glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDisable(GL_TEXTURE_2D); }

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
