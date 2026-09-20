/* hud.h - the 2D layer: bitmap font, strings, text box (message 1080) and the in-game HUD (docs/HUD_TEXT.md). */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>

typedef struct {
    int face;                   /* 0 Woody, 1 Knothead, 2 Splinter (hud+8) */
    int race;                   /* hud+4: race levels have no health row */
    int lives, bonus, got, total, show_total, unique, charges;
    int extended;               /* pause menu / message 1172: also the $ and charge counters */
    float health, power;        /* power = charge time * 2/3, 1 = full */
} HudState;

int  hud_load(const char *common_rck, const char *level_rck);   /* images 61..64 + strings of bank 0, font of the level bank; needs a GL context */
void hud_free(void);
void hud_begin(int win_w, int win_h);                           /* 640x480 virtual, origin top left; after the 3D frame */
void hud_end(void);
void hud_draw(const HudState *s, float dt);
void hud_text_open(int halign, int valign, const uint32_t *ids, int n);   /* message 1080; ids = string refs, 0x20001 = spacer */
void hud_text_draw(int closed, float dt);                       /* closed = the script variable went non-zero */
void hud_text_reset(void);
/* House menu pages (docs/TITLE.md 5): page 0 = "Press a key", page 1 = New game / Load game / Options / Quit; logo = House.rck image 1 */
void hud_title_draw(int page, int sel, int want_logo, float dt);
void hud_title_reset(void);
int  hud_sky_images(uint32_t out[5]);                           /* level bank images in cube order 3,0,1,2,4 when the bank has >= 5 images, else 0 */
/* pickups are sprites, not meshes (docs/BONUS.md 3.1, 0x479530): n = 0 life, 1 charge, 2 W, 3 $, 4 flag. Call between the 3D frame and hud_begin. */
void hud_world_sprites_begin(const float *right, const float *up);
void hud_world_sprite(int n, const float *pos, float size);
void hud_world_sprites_end(void);
/* additive effect sprite (bank 0 image 0, 4 or 6), rotated by `turns` around the view axis; colour = rgb * alpha */
void hud_world_fx(int image, const float *pos, float size, float turns, const float *rgb, float alpha);
/* additive ribbon segment with a colour per end (bank 0 image 0) */
void hud_world_ribbon(const float *a, const float *b, const float *eye, float hw, const float *rgb_a, const float *rgb_b);
/* additive camera-facing line quad (line primitive 0x471a10, bank 0 image 1): half width hw, colour*alpha at both ends */
void hud_world_beam(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b);

#endif
