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
/* the HUD animator (hud+0x30, docs/HUD_TEXT.md 4.6): the icon that flies in from the pickup and the 25 W's that
 * are paid out as a heart or an extra life. hud_draw ticks them; the reward is detected from the counters, like
 * the original's setters do (0x448380). kind 1..5 = pickup types 30, 36, 35, 34, 37 (0x448510);
 * screen = the pickup projected into 640x480, NULL when it is off screen; face = the portrait index for kind 1. */
void hud_anim_reset(void);
void hud_anim_pickup(int kind, const float *screen, int face);
/* House menu pages (docs/TITLE.md 5): page 0 = "Press a key", page 1 = New game / Load game / Options / Quit; logo = House.rck image 1 */
void hud_title_draw(int page, int sel, int want_logo, float dt);
void hud_title_reset(void);
/* a panel page of the common menu class (docs/TITLE.md 5.1): the items centred from y = yfrac * 480, one cell
 * (62 * S / 40) apart, size 30 shrinking until the widest one fits, the selected one blinking away at 2 Hz. */
void hud_menu_page(const uint32_t *ids, int n, float yfrac, int sel, float dt);
/* the results screen after a level (docs/GAMEFLOW.md 5.1, docs/HUD_TEXT.md 6): the two collectible categories with
 * their "+50 %" bonus, the level time and the score. `level` is the number printed behind string 127 "Level". */
typedef struct { int level, race, high, cats; int total_a, got_a, total_b, got_b; float time; int score, best; } HudResults;
void hud_results_draw(const HudResults *r, int show_ok, float dt);
int  hud_sky_images(uint32_t out[5]);                           /* level bank images in cube order 3,0,1,2,4 when the bank has >= 5 images, else 0 */
/* pickups are sprites, not meshes (docs/BONUS.md 3.1, 0x479530): n = 0 life, 1 charge, 2 W, 3 $, 4 flag. Call between the 3D frame and hud_begin. */
void hud_world_sprites_begin(const float *right, const float *up);
void hud_world_sprite(int n, const float *pos, float size);
void hud_world_wing(int n, const float *c, const float *u, const float *v, float half);   /* bank 0 images 53..56: one wing of a butterfly */
void hud_world_sprites_end(void);
/* additive effect sprite (bank 0 image 0, 4 or 6), rotated by `turns` around the view axis; colour = rgb * alpha */
void hud_world_fx(int image, const float *pos, float size, float turns, const float *rgb, float alpha);
/* the same sprite, but lying in the plane with normal `n` instead of facing the camera (sprite flag bit 0 off,
 * 0x4717d7): what the nine quads of an explosion flash are made of (docs/PROJECTILES.md 5.3) */
void hud_world_fx_plane(int image, const float *pos, const float *n, float size, const float *rgb, float alpha);
/* a sprite that lies in a plane instead of facing the camera (0x4717d7: without flag bit 0 the quad is built on the
 * normal S+0x230..0x238), turned so that +v runs along `dir`, optionally mirrored (flag 0x40, docs/PERSO_DEATH.md).
 * `size` is the half diagonal, as for every sprite. It darkens what is under it by `rgb * strength`, see hud.c. */
void hud_world_decal(int image, const float *pos, const float *normal, const float *dir, float size, int mirror, const float *rgb, float strength);
int  hud_step_image(void);                                      /* bank 0 image used for the footstep mark (WOODY_STEPIMG) */
/* the hole a peck leaves in the wood (docs/OBJECTS.md 1.6): a ragged cup lying in the pecked face (normal n, its
 * +v along `dir`) that darkens what is under it like the footstep mark, with a faint rim of split wood around it.
 * `size` is the outer radius. `seed` fixes the outline of this one hole, so it does not shimmer from frame to
 * frame and a column of them is not stamped out of the same shape. */
void hud_world_gouge(const float *pos, const float *n, const float *dir, float size, unsigned seed, const float *rgb, float strength, float rim);
/* one chip of wood the beak knocks loose: a solid, untextured sliver, not a sprite. `u` and `v` are its two half
 * axes and carry both its size and its tumble; there is no GL light in this pass, so it shades itself. */
void hud_world_chip(const float *c, const float *u, const float *v, const float *rgb, float alpha);
/* additive ribbon segment with a colour per end (bank 0 image 0) */
void hud_world_ribbon(const float *a, const float *b, const float *eye, float hw, const float *rgb_a, const float *rgb_b);
/* additive camera-facing line quad (line primitive 0x471a10, bank 0 image 1): half width hw, colour*alpha at both ends */
void hud_world_beam(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b);
/* additive ring of half width hw around c, lying in the plane with normal n: the landing marker under Woody */
void hud_world_ring(const float *c, const float *n, float radius, float hw, const float *rgb, float alpha);

#endif
