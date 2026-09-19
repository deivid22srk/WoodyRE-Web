/* render_gl.h - minimal Win32 + OpenGL 1.1 renderer for the reimplemented engine. */
#ifndef WOODY_RENDER_GL_H
#define WOODY_RENDER_GL_H
#include "level.h"

typedef struct {
    Vec3 pos; float yaw, pitch;               /* radians; yaw 0 looks along +z (D3D convention) */
    float fov_deg;
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
    int show_world, show_instances, wireframe;
} Renderer;

int  rnd_init(Renderer *r, TexFile *tex, GelFile *gel, InsFile *ins);
void rnd_frame(Renderer *r, const Window *w, const FreeCamera *cam, float time_s);
void rnd_free(Renderer *r);
int  rnd_screenshot(const Window *w, const char *path);   /* binary PPM of the current back buffer */
Vec3 cam_forward(const FreeCamera *c);
Vec3 cam_right(const FreeCamera *c);

#endif
