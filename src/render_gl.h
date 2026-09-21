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
} Window;

int  win_open(Window *w, const char *title, int width, int height);
void win_poll(Window *w);                     /* pumps messages, updates keys/mouse */
void win_swap(Window *w);
void win_close(Window *w);
double win_time(void);                        /* seconds, high resolution */

typedef struct {
    TexFile *tex; GelFile *gel; InsFile *ins;
    /* world geometry baked into vertex arrays per texture group */
    struct WorldBatch { uint32_t group; uint32_t ntris; float *pos; float *uv; uint8_t *col; } *batches; uint32_t nbatches;
    /* static lighting from the .lit (docs/LIGHTING.md): lit faces are drawn in passes (ambient fill, additive light
     * polygons, texture x2), everything else in one pass */
    const LitFile *lit; struct WorldBatch *litb;      /* lit faces per texture group (same count as batches) */
    struct WorldBatch lightb[16]; uint32_t light_tex[16];   /* light polygons per generated radial texture */
    int show_light; float *face_bound;                 /* per world face: centre xyz + radius (cast shadow receivers) */
    float last_time;
    int have_sky; uint32_t sky_tex[5]; float sky_hu, sky_hv;   /* sky cube (docs/SKY.md): +z, +x, -z, -x, top */
    int show_world, show_instances, wireframe;
} Renderer;

int  rnd_init(Renderer *r, TexFile *tex, GelFile *gel, InsFile *ins, const LitFile *lit);   /* lit may be NULL */
void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s);
void rnd_fade(float brightness);             /* darken the finished frame: 1 = normal, 0 = black */
void rnd_free(Renderer *r);
void rnd_set_sky(Renderer *r, const uint32_t tex[5]);   /* level bank images 3,0,1,2,4 replace the group's own frames when the bank has >= 5 images (0x5e8670) */
int  rnd_screenshot(const Window *w, const char *path);   /* binary PPM of the current back buffer */
Vec3 cam_forward(const FreeCamera *c);
Vec3 cam_right(const FreeCamera *c);
/* the projection inlined in 0x47b230: a world point into the 640x480 HUD space. 0 = outside the four side planes
 * (the original then skips the animation that wanted it), 1 = sx/sy filled in. */
int  rnd_project(const Window *w, const FreeCamera *cam, Vec3 p, float *sx, float *sy);

#endif
