/* render_gl.h - minimal Win32 + OpenGL 1.1 renderer for the reimplemented engine. */
#ifndef WOODY_RENDER_GL_H
#define WOODY_RENDER_GL_H
#include "level.h"

typedef struct {
    Vec3 pos; float yaw, pitch;               /* radians; yaw 0 looks along +z (D3D convention) */
    float fov_deg;
    int letterbox;                            /* camera mode 4: 16:9 strip, shifted up (0x41f910) */
} FreeCamera;

typedef struct {
    int width, height;
    int keys[256];                            /* current key state (VK codes) */
    int mouse_dx, mouse_dy, mouse_right;
    int quit;
    void *hwnd, *hdc, *hglrc;
    int vx, vy;                               /* port extra (docs/DISPLAY.md 3): rnd_frame draws into the box vx, vy, width, height of the real window */
} Window;

int  win_open(Window *w, const char *title, int width, int height);
/* port extras (docs/DISPLAY.md 3): full = borderless on the window's monitor, else a client of width x height (shrunk to the
 * work area, aspect kept); interval = the swap interval (1 = vsync, 0 = off; returns -1 without WGL_EXT_swap_control) */
void win_mode(Window *w, int width, int height, int full);
int  win_vsync(int interval);
void win_poll(Window *w);                     /* pumps messages, updates keys/mouse */
void win_swap(Window *w);
void win_close(Window *w);
double win_time(void);                        /* seconds, high resolution */

/* One texture group's share of the world, baked once. `idx` is what the current frame actually draws out of it:
 * the triangles of the faces the visibility pass kept (see rnd_frame). */
struct WorldBatch { uint32_t group; uint32_t ntris; float *pos; float *uv; uint8_t *col;
                    uint32_t *idx, nidx, idx_cap; uint32_t *face; };   /* face: per triangle, only for the light batches */
/* Where a world face ended up in the batches, so the visibility pass can put its triangles back in. */
struct FaceBatch { uint32_t tri0, ntris, group; uint8_t lit; };

typedef struct {
    TexFile *tex; GelFile *gel; InsFile *ins;
    /* world geometry baked into vertex arrays per texture group */
    struct WorldBatch *batches; uint32_t nbatches;
    /* static lighting from the .lit (docs/LIGHTING.md): lit faces are drawn in passes (ambient fill, additive light
     * polygons, texture x2), everything else in one pass */
    const LitFile *lit; struct WorldBatch *litb;      /* lit faces per texture group (same count as batches) */
    struct WorldBatch lightb[16]; uint32_t light_tex[16];   /* light polygons per generated radial texture */
    int show_light; float *face_bound;                 /* per world face: centre xyz + radius (cast shadow receivers) */
    float last_time;
    int have_sky; uint32_t sky_tex[5]; float sky_hu, sky_hv;   /* sky cube (docs/SKY.md): +z, +x, -z, -x, top */
    int show_world, show_instances, wireframe;
    /* visibility (0x42a980 / 0x42ac10): which sectors of the .gel the camera can see, and the faces that go with them */
    const VisFile *vis;                                /* .vis potentially visible sets, NULL when the level has none */
    int cull;                                          /* 0 = draw the whole level, 1 = frustum only, 2 = frustum + .vis */
    struct FaceBatch *face_batch;                      /* per world face */
    uint32_t *face_stamp, stamp_gen;                   /* the frame stamp of poly+4: one face is collected once */
    uint8_t *sec_vis, *sec_prev; int pvs_on, sec_dirty;/* per sector: visible now / last frame */
    uint32_t drawn_tris, total_tris; uint32_t nsec_vis;
    /* drawn after the models and before the fade list: texture list 8 of the flush 0x4293f0 (docs/LIGHTING.md 1.5),
     * where the water surfaces of class 60 go (water.c) */
    void (*post_models)(const TexFile *tex, Vec3 eye);
    uint8_t *model_blend;                              /* per .ins model: 1 = a drawn mesh node has a polygon of a blended group (list +0x1cc), built on the first frame */
    Instance **links; uint32_t nlinks, links_cap;     /* message 34 pairs (volume instance, hidden instance), level+0x50 / +0x4c (docs/INSTANCE.md 10.1) */
} Renderer;

int  rnd_init(Renderer *r, TexFile *tex, GelFile *gel, InsFile *ins, const LitFile *lit, const VisFile *vis);   /* lit / vis may be NULL */
void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s);
void rnd_fade(float brightness);             /* darken the finished frame: 1 = normal, 0 = black */
void rnd_free(Renderer *r);
/* message 34 [inst, other] (0x42dc21): while the camera is inside one of inst's volume nodes, `other` is not drawn (0x42aa0b) */
void rnd_link(Renderer *r, Instance *inst, Instance *other);
void rnd_set_sky(Renderer *r, const uint32_t tex[5]);   /* level bank images 3,0,1,2,4 replace the group's own frames when the bank has >= 5 images (0x5e8670) */
int  rnd_screenshot(const Window *w, const char *path);   /* binary PPM of the current back buffer */
/* an HNM film frame (RGB565, docs/HNM.md) over the whole window, 4:3 kept with black bars; px NULL frees the texture */
void rnd_film_frame(const Window *w, const uint16_t *px, int width, int height);
void rnd_uv_report(const Renderer *r, const Instance *inst);   /* WOODY_UVLOG: texture group + generated UV range per mesh node and material */
Vec3 cam_forward(const FreeCamera *c);
Vec3 cam_right(const FreeCamera *c);
/* the projection inlined in 0x47b230: a world point into the 640x480 HUD space. 0 = outside the four side planes
 * (the original then skips the animation that wanted it), 1 = sx/sy filled in. */
/* 0x498790(lightsys, kind, &pos, &rgb 0..255, radius): register a dynamic point light for the next drawn frame.
 * 1 = stored, 0 = table full (16). Drawn only with WOODY_DYNLIGHT=1: the original never draws them (docs/LIGHTING.md 7). */
int  rnd_light_add(int kind, Vec3 pos, const float rgb[3], float radius);
int  rnd_project(const Window *w, const FreeCamera *cam, Vec3 p, float *sx, float *sy);

#endif
