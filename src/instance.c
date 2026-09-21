/* instance.c - generic instance behaviour of the base class (docs/INSTANCE.md): animation clock 0x43eee0,
 * PlayAnim messages 1..5/12/13, path follower 42/43/44/46, transparency fade 56/57, show/hide 6.
 * texture frame override 16/18/19. Not ported: the UV scroll override 15/17, SetFlags 45 (stored only), orientation along the path (46). */
#include <math.h>
#include "instance.h"

#define T_ACTIVE   0x20000u
#define T_REVERSE  0x40000u
#define T_LOOP     0x80000u
#define T_PINGPONG 0x100000u

static float anim_L(const Instance *I, int a) { const Model *m = I->model; return (a >= 0 && (uint32_t)a < m->nanims && m->anims[a].duration_s > 0) ? m->anims[a].duration_s : 1.0f; }
static void set_speed(Instance *I, float v) { I->a_speed = I->a_base_speed = v; }               /* 0x42e290 */

void inst_init(Instance *I)                                                                     /* ctor 0x42e250 / 0x42e210 / 0x44e7c0 */
{
    I->slot[0] = 0; I->slot[1] = I->slot[2] = I->slot[3] = -1;
    I->a_speed = I->a_base_speed = 0; I->a_start = 0; I->a_pos = 0; I->a_ended = 0;
    I->fade = 0; I->fade_target = 0; I->fade_rate = 100.0f; I->noncollide = 0; I->setflags = 0;
    I->traj_flags = 0; I->traj_start = 0; I->traj_dur = 1.0f; I->scripted = 1;
    I->tex_mode = 0; I->tex_t0 = 0; I->tex_fac = 1.0f;
}

/* returns 1 when the message must be offered again next frame (12/13 waiting for the running animation to end) */
int inst_msg(Instance *I, uint32_t id, const uint32_t *arg, uint32_t nargs, float now)
{
    int a1 = nargs > 1 ? (int)arg[1] : 0, a2 = nargs > 2 ? (int)arg[2] : 0, a3 = nargs > 3 ? (int)arg[3] : 0, a4 = nargs > 4 ? (int)arg[4] : 0;
    const Model *m = I->model;
    switch (id) {
    case 1:                                                       /* still frame at position t (scale 0.001 of the PREVIOUS slot0 length) */
        if ((uint32_t)a1 >= m->nanims) return 0;
        set_speed(I, 0); I->a_pos = anim_L(I, I->slot[0]) * a2 * 0.001f;
        I->slot[0] = a1; I->slot[1] = I->slot[2] = I->slot[3] = -1; I->a_ended = 0; return 0;
    case 2:                                                       /* one shot with start offset: anim, flag, dur, off */
        if ((uint32_t)a1 >= m->nanims || a3 <= 0) return 0;
        I->slot[0] = a1; set_speed(I, anim_L(I, a1) / (a3 * 0.01f)); I->a_start = now - a4 * 0.001f * a3 * 0.01f;
        if (a2 == 0) set_speed(I, -I->a_speed);
        I->slot[1] = I->slot[2] = I->slot[3] = -1; I->a_ended = 0; return 0;
    case 12: if (!I->a_ended && I->a_speed != 0) return 1;         /* fallthrough */
    case 3: {                                                     /* one shot "door": anim, flag, dur */
        if ((uint32_t)a1 >= m->nanims) return 0;
        if (a3 <= 10) { set_speed(I, 0); I->a_pos = a2 ? anim_L(I, I->slot[0]) : 0; return 0; }   /* jump to the end / start */
        float L = anim_L(I, a1), v = L / (a3 * 0.01f);
        if (I->slot[0] == a1 && I->a_speed != 0) {                /* already running: reverse from the current pose */
            if (I->a_speed > 0) {
                if (a2 != 0) return 0;
                float p = (now - I->a_start) / (L / I->a_speed); p = 1.0f - (p - floorf(p));
                I->a_start = now - (L / v) * p; set_speed(I, -v);
            } else {
                if (a2 != 1) return 0;
                float p = (now - I->a_start) / -(L / I->a_speed); p = p - floorf(p);
                set_speed(I, v); I->a_start = now - (L / v) * (1.0f - p);
            }
            return 0;
        }
        I->slot[0] = a1; set_speed(I, a2 == 0 ? -v : v); I->a_start = now;
        I->slot[1] = I->slot[2] = I->slot[3] = -1; I->a_ended = 0; return 0;
    }
    case 13: if (!I->a_ended && I->a_speed != 0) return 1;         /* fallthrough */
    case 4:                                                       /* loop: anim, flag, dur */
        if ((uint32_t)a1 >= m->nanims || a3 <= 0) return 0;
        I->slot[0] = I->slot[1] = I->slot[2] = I->slot[3] = a1;
        set_speed(I, anim_L(I, a1) / (a3 * 0.01f)); if (a2 == 0) set_speed(I, -I->a_speed);
        I->a_start = now; I->a_ended = 0; return 0;
    case 5: set_speed(I, 0); return 0;                            /* freeze on the current position */
    case 6: I->visible = a1 != 0; return 0;                       /* 0x407850 / 0x407790: hidden = not drawn, no collision, no volumes */
    case 42: case 43:                                             /* path follower: a (1 = forward), f = time in 1/100 s, c = ping-pong */
        if (I->traj.npoints < 2 || a2 <= 0) return 0;
        I->traj_flags = T_ACTIVE | (a1 != 1 ? T_REVERSE : 0) | (id == 43 ? T_LOOP | (a3 == 1 ? T_PINGPONG : 0) : (I->traj_flags & T_PINGPONG));
        I->traj_dur = a2 * 0.01f; I->traj_start = now;
        I->position = I->traj.points[a1 == 1 ? 0 : I->traj.npoints - 1]; return 0;
    case 44: I->traj_flags &= ~T_ACTIVE; return 0;
    case 16: case 18:                                             /* texture frame override B (docs/INSTANCE.md 2): 16 = one shot, 18 = loop; a2 = 1 forward, 0 backward,
                                                                   * 2 there and back; a3 x 0.01 = factor on the texture duration, a1 (0xffff) is stored but never read */
        I->tex_mode = (id == 16 ? 1 : 4) + (a2 == 1 ? 0 : a2 == 0 ? 1 : 2);
        I->tex_t0 = now; I->tex_fac = a3 * 0.01f; return 0;
    case 19: I->tex_mode = 0; return 0;                           /* 0x42db86: override off, back to the frame of the global texture animation */
    case 45: I->setflags |= (uint32_t)a1 & 0x23; return 0;
    case 56:                                                      /* transparency: direct on the base class, a target on classes behind 0x44e8f0 */
        if (I->type == 0 || I->type == 41 || I->type == 90) I->fade = I->fade_target = a1 * 0.01f; else I->fade_target = a1 * 0.01f;
        return 0;
    case 57: I->fade_rate = a1 * 0.01f; return 0;
    default: return 0;
    }
}

static void traj_update(Instance *I, float now)                                                 /* 0x437da0 */
{
    if (!(I->traj_flags & T_ACTIVE)) return;
    const Trajectory *T = &I->traj; uint32_t n = T->npoints;
    float t = now - I->traj_start, frac, ip = 0;
    if (!(I->traj_flags & T_LOOP) && t > I->traj_dur) { frac = 1.0f; I->traj_flags &= ~T_ACTIVE; }
    else frac = modff(t / I->traj_dur, &ip);
    int fwd = !(I->traj_flags & T_REVERSE); if ((I->traj_flags & T_PINGPONG) && ((int)ip & 1)) fwd = !fwd;
    float total = 0; for (uint32_t i = 0; i + 1 < n; i++) { Vec3 a = T->points[i], b = T->points[i + 1]; total += sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z)); }
    float d = frac * total, acc = 0, rem = d;
    for (uint32_t s = 0; s + 1 < n; s++) {
        uint32_t i = fwd ? s : n - 1 - s, j = fwd ? i + 1 : i - 1;
        Vec3 a = T->points[i], b = T->points[j]; float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
        acc += len;
        if (d <= acc) { float k = len > 1e-6f ? rem / len : 0; I->position = (Vec3){ a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k }; break; }
        rem -= len;
    }
    mat4_from_trs(&I->world, I->position, I->quat, I->scale);
}

void inst_play_once(Instance *I, int anim, float speed, float now)
{
    I->slot[0] = anim; I->slot[1] = I->slot[2] = I->slot[3] = -1; set_speed(I, speed); I->a_start = now; I->a_pos = 0; I->a_ended = 0;
}

void inst_tick(Instance *I, float now, float dt)
{
    if (!I->scripted) return;
    /* fade 0x44e810 */
    float old = I->fade;
    if (I->fade < I->fade_target) { I->fade += dt * I->fade_rate; if (I->fade > I->fade_target) I->fade = I->fade_target; }
    else if (I->fade > I->fade_target) { I->fade -= dt * I->fade_rate; if (I->fade < I->fade_target) I->fade = I->fade_target; }
    if (I->fade > 0.9f && old <= 0.9f) I->noncollide = 1;
    if (I->fade < 0.9f && old >= 0.9f) I->noncollide = 0;
    if (!I->visible) return;                                       /* hidden: no clock (time still runs, it is absolute) */
    traj_update(I, now);
    /* animation clock 0x43eee0 */
    float phase, L = anim_L(I, I->slot[0]);
    if (I->a_speed != 0) {
        I->a_ended = 0;
        if (I->a_speed > 0) phase = (now - I->a_start) / (L / I->a_speed);
        else phase = 1.0f - (now - I->a_start) / -(L / I->a_speed);
        if (!(phase >= 0 && phase < 1)) {
            I->a_ended = 1;
            if (I->slot[1] != -1) {                                /* next slot: seamless, time exact */
                float ratio = L / anim_L(I, I->slot[1]), nwrap = 0;
                if (phase < 0) { nwrap = -floorf(phase); phase += nwrap; }
                if (phase >= 1) { nwrap = floorf(phase); phase -= nwrap; }
                phase *= ratio; if (phase >= 1) phase = 0.9999f;
                if (nwrap != 0) I->a_start += nwrap * (L / fabsf(I->a_speed));
                I->a_speed = I->a_base_speed;
                I->slot[0] = I->slot[1]; I->slot[1] = I->slot[2]; I->slot[2] = I->slot[3];
                L = anim_L(I, I->slot[0]);
            }
            else if (I->a_speed < 0) { if (phase < 0) { set_speed(I, 0); phase = 0; } }
            else if (phase > 1 || phase == 1) { set_speed(I, 0); phase = 1; }
        }
        I->a_pos = L * phase;
    } else phase = I->a_pos / L;
    if (phase > 0.9999f) phase = 0.9999f; if (phase < 0) phase = 0;   /* ins_pose() wraps at 1 */
    I->anim = I->slot[0]; I->anim_time = phase * L;
}
