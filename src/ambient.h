/* ambient.h - instance class 90, the "environment instance" (vtable 0x4a90ac, think 0x472560), modes 1 and 2
 * (docs/AMBIENT.md). Mode 1 = a steady stream of glowing motes rising from the bottom of the instance's volume node
 * (particle 0x47e160 / 0x47dca0), mode 2 = rain falling through it (0x47e230 / 0x47de10, ripple 0x47e310 / 0x47df80).
 * Mode 0 (the butterflies) stays in main_engine.c (env_update / env_draw). */
#ifndef WOODY_AMBIENT_H
#define WOODY_AMBIENT_H
#include <stdint.h>
#include "level.h"

void ambient_reset(void);                                            /* level load / free: every record and particle gone */
/* the subsystem handler 0x46cca0: 1501 mode (0x46cd07), 1502 colour/life/count (0x46cd2e), 1503 rain force (0x46cda0),
 * 1504 count (0x46cdcc), 1511 off flag (0x46cf44); args[0] is the instance ref */
void ambient_msg(Instance *in, int id, const uint32_t *args, int nargs);
void ambient_update(float dt);                                       /* the think of every class-90 instance whose sector is drawn, then the particles */
void ambient_draw(const float *eye);                                 /* between hud_world_sprites_begin / end */

#endif
