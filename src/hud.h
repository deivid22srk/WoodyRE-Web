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
/* menu pages of the common page class (docs/TITLE.md 5, MENU_NEWGAME.md 2): an item is {Common string, flags, value};
 * flags 1 selectable, 2 header (never selected), 4 right, 8 left, 0x80 centred on x 160, 0x20 size x 0.8,
 * 0x10 slider ("name value%"). The items run from y = yfrac * 480, one cell (62 * S / 40) apart, size 30 shrinking
 * until the widest name fits; the selected one blinks away at 2 Hz. `ready` = the page's input delay is over. */
typedef struct { uint32_t id, flags; int value; } MenuItem;
void hud_menu_items(const MenuItem *it, int n, float yfrac, int sel, int ready);
void hud_menu_tick(float dt);                                   /* the blink phase, once per frame */
void hud_menu_blink(float t);                                   /* set it: 0 after up/down, 0.25 after a slider step */
void hud_menu_page(const uint32_t *ids, int n, float yfrac, int sel, float dt);   /* tick + selectable items */
void hud_logo(int grow, float dt);                              /* House.rck image 1: fades in on pages 0/1 (grow), out elsewhere */
void hud_logo_off(void);
void hud_title_reset(void);
void hud_iris(float v);                                         /* black ring, hole radius v * 475 (0 = black, 0.85 = open) */
void hud_rect(uint32_t argb);                                   /* flat colour over the virtual screen */
/* the save-slot list of pages 2 (load) and 5 (save), docs/MENU_LOAD.md 2.2: sel 1..4, pct 0 = free slot,
 * open bit 0 Knothead / bit 1 Splinter unlocked, slide = the panels' offset while they move in or out,
 * cross = draw the red cross over free slots (page 2), title = Common string (25 / 24) */
typedef struct { int sel, pct[4], open[4], cross; float slide; uint32_t title; } HudSlots;
void hud_slot_list(const HudSlots *s, float dt);
/* the 2D layer of the world-select carousel, page 3 (docs/MENU_LOAD.md 4.6, vt[17] 0x45e8a0): name on top, the stats
 * panel left and "Game cleared" / "Location" right of the selected figure (only unlocked and not BlackBox: `stats`),
 * the PLAY / SEE HIGH SCORES list (`list`: unlocked, the page neither opening nor closing), the two yellow arrows and
 * "Total Score :". off = the text slide (<= 0), arrow_s = the arrows' slide, arrow_l / arrow_r their grey (128 = 1). */
typedef struct {
    uint32_t name;                                   /* Common string 30..34 */
    int stats, face, lives, unique, charges; float health;
    int pct; uint32_t world, part;                   /* location strings of the first unfinished level, 0 = none */
    int list, nitems, list_sel; const MenuItem *items; float yfrac;
    float off, arrow_s, arrow_l, arrow_r;
    int total;
} HudCarousel;
void hud_carousel(const HudCarousel *c);
/* the results screen after a level, menu page 0x1e (docs/RESULTS.md): level = the index of the level just left
 * (app+0x6c), st = {total enemies, total W, enemies beaten, W found} (app+0x74..), time in s, best = the saved best
 * score from BEFORE this run, cats = complete categories = new unique items (the $ line). */
typedef struct { int level, race; int st[4]; float time; int best, cats; } HudResults;
void hud_results_enter(void);                                   /* 0x4544b0: page entered (state 0), nothing drawn yet */
void hud_results_show(void);                                    /* 0x454560: iris closes, texts slide in, the lines count (state 1) */
void hud_results_hide(void);                                    /* 0x454580: everything gone at once, iris opens (state 2/3) */
int  hud_results_confirm(void);                                 /* 0x4545a0: 1 = all counted (OK); while counting it skips to the end */
int  hud_results_draw(const HudResults *r, float dt);           /* 1 while a number is counting (tick loop SoundFx 0x3d) */
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
/* the same line quad without a texture (0x471a10 without flag 0x200): the lightning arc along a laser */
void hud_world_line(const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b);
/* additive ring of half width hw around c, lying in the plane with normal n: the landing marker under Woody */
void hud_world_ring(const float *c, const float *n, float radius, float hw, const float *rgb, float alpha);

#endif
