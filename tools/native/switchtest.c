/* switchtest.c - the peck switch, on the host and without game data.
 *
 * What it checks: message 1042 (0x445269, docs/OBJECTS.md 1.2) - the test every peck switch and every door in the
 * game runs through - and the brake it puts on the charge run (0x458e40), which is the peck the player sees.
 *
 * The code under test is the code that ships. src/player.c is included whole, so the static attack controller can be
 * driven frame by frame exactly as player_update drives it, and the 1042 handler plus the marker lookup are lifted
 * out of src/main_engine.c mechanically by the build command below - nothing of either is retyped here. Everything
 * else (the level, the enemies, the sound, the VM) is a stub: this is a unit test of one message, not of the engine.
 *
 * Build and run from the repo root (the first command lifts the two blocks out of src/main_engine.c):
 *
 *   mkdir -p out
 *   python3 -c "import io;s=io.open('src/main_engine.c',encoding='utf-8').read();a=s.index('static int inst_vector(');b=s.index(chr(10)+'}'+chr(10),a)+3;c=s.index('    case 1042:');d=s.index(chr(10)+'        break;'+chr(10),c)+16;io.open('out/msg1042.inc','w',encoding='utf-8').write(s[a:b]+'static void msg_1042(const EkoMsg *m, Instance *in, EkoVM *vm)'+chr(10)+'{'+chr(10)+'    switch (m->id) {'+chr(10)+s[c:d]+'    default: break;'+chr(10)+'    }'+chr(10)+'}'+chr(10))"
 *   cc -std=c99 -O1 -Isrc -Iout -o out/switchtest tools/native/switchtest.c src/level.c -lm && ./out/switchtest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- stubs for everything player.c talks to that is not the level loader --------------------------------------- */
#include "player.h"
#include "enemy.h"
#include "audio.h"
void audio_fx(int id, const void *owner, const float *pos) { (void)id; (void)owner; (void)pos; }
void audio_fx_stop(int id, const void *owner, int is3d) { (void)id; (void)owner; (void)is3d; }
int  enemy_take_damage(Enemy *e, float d, Vec3 v) { (void)e; (void)d; (void)v; return 0; }
void enemy_warn_dive(Enemy *e, Vec3 d) { (void)e; (void)d; }
float enemy_radius(const Enemy *e) { (void)e; return 0; }
float enemy_height(const Enemy *e) { (void)e; return 0; }
void game_footstep(Vec3 p, Vec3 n, Vec3 d, int f, int k) { (void)p; (void)n; (void)d; (void)f; (void)k; }
void game_land_dust(Vec3 p, Vec3 n) { (void)p; (void)n; }
void game_peck_fx(int k, Vec3 p, const Vec3 *n) { (void)k; (void)p; (void)n; }
void eko_vol_perso_enter(EkoVM *vm, uint32_t v, uint32_t a) { (void)vm; (void)v; (void)a; }
void eko_vol_perso_in(EkoVM *vm, uint32_t v, uint32_t a) { (void)vm; (void)v; (void)a; }
void eko_vol_perso_leave(EkoVM *vm, uint32_t v, uint32_t a) { (void)vm; (void)v; (void)a; }
void eko_col_perso_press(EkoVM *vm, uint32_t c, uint32_t a) { (void)vm; (void)c; (void)a; }
void eko_col_perso_in(EkoVM *vm, uint32_t c, uint32_t a) { (void)vm; (void)c; (void)a; }
void eko_col_perso_unpress(EkoVM *vm, uint32_t c, uint32_t a) { (void)vm; (void)c; (void)a; }
void eko_msgmask_set(EkoVM *vm, uint32_t o, uint32_t b) { (void)vm; (void)o; (void)b; }
void eko_msgmask_clear(EkoVM *vm, uint32_t o, uint32_t b) { (void)vm; (void)o; (void)b; }
void eko_actor_leave_all(EkoVM *vm, uint32_t a) { (void)vm; (void)a; }

static int32_t g_var = -1;                                   /* the script variable 1042 answers in */
void eko_set_var(EkoVM *vm, uint32_t var, int32_t value) { (void)vm; (void)var; g_var = value; }

#include "../../src/player.c"                                /* the real Perso: attack controller, brake, state test */

static Player *g_player;                                     /* main_engine.c calls it that, so the lifted block does too */
#include "msg1042.inc"                                       /* inst_vector + case 1042, lifted from src/main_engine.c */

/* ---- a switch to stand in front of ---------------------------------------------------------------------------- */
static int fails;
static void check(int ok, const char *what, const char *got)
{
    if (!ok) { printf("  FAIL %-58s %s\n", what, got); fails++; } else printf("  ok   %-58s %s\n", what, got);
}
#define CHECK(cond, what, fmt, ...) do { char b_[128]; snprintf(b_, sizeof b_, fmt, __VA_ARGS__); check((cond), (what), b_); } while (0)

static InsAnim g_anims[96];
static InsPoint g_points[2];
static InsNode g_nodes[1];
static Model g_model;
static Mat4 g_nw[1];
static Instance g_inst;

/* one switch at `pos` whose marker vector points along `dir`, like model 41 in W1A (docs/OBJECTS.md 1.2) */
static void switch_at(Vec3 pos, Vec3 dir)
{
    memset(&g_nodes, 0, sizeof g_nodes); memset(&g_model, 0, sizeof g_model); memset(&g_inst, 0, sizeof g_inst);
    g_nodes[0].kind = 0x20; g_nodes[0].type_code = 0; g_nodes[0].npoints = 2; g_nodes[0].point_base = 0;
    g_points[0].pos = pos; g_points[1].pos = (Vec3){ pos.x + dir.x, pos.y + dir.y, pos.z + dir.z };
    g_model.nnodes = 1; g_model.nodes = g_nodes; g_model.npoints = 2; g_model.points = g_points;
    g_model.nanims = 96; g_model.anims = g_anims;
    for (int i = 0; i < 16; i++) g_nw[0].m[i] = (i % 5) == 0 ? 1.0f : 0.0f;         /* identity: the points are world space */
    g_inst.model = &g_model; g_inst.node_world = g_nw; g_inst.position = pos; g_inst.index = 279; g_inst.id = 0x01000117;
}
static Player *a_player(void)
{
    static Player p; static Instance wi; static Mat4 wnw[1];
    memset(&p, 0, sizeof p); memset(&wi, 0, sizeof wi);
    for (int i = 0; i < 96; i++) g_anims[i].duration_s = 0.6f;                      /* every animation 0.6 s: AnimLen = 0.2 at speed 3 */
    g_anims[51].duration_s = 0.6f; g_anims[52].duration_s = 0.9f;                   /* the peck 0x12 = anims 51 + 52 -> 0.2 + 0.3 = 0.5 s */
    wi.model = &g_model; wi.node_world = wnw;
    p.inst = &wi; p.on_ground = 1; p.lanim = -1; p.health = 5; p.lives = 3;
    p.jumper.state = 2;                                                             /* standing on the ground */
    return &p;
}
/* one player frame, in the order player_update runs it (the move itself is only the attack displacement here) */
static void frame(Player *p, const PlayerInput *in, float dt)
{
    if (p->move_lock > 0) p->move_lock -= dt;
    attack_update(p, in, dt);
    attack_trigger(p, in, dt);
    if (p->use_atk_disp) { p->pos.x += p->atk_disp.x; p->pos.y += p->atk_disp.y; p->pos.z += p->atk_disp.z; }
}
/* the VM tick of that same frame: the switch script asks 1042 [inst, dist, angle, var] */
static int ask_1042(Player *p, int dist, int angle)
{
    EkoMsg m; memset(&m, 0, sizeof m);
    m.id = 1042; m.nargs = 4; m.args[0] = g_inst.id; m.args[1] = (uint32_t)dist; m.args[2] = (uint32_t)angle; m.args[3] = 21;
    g_player = p; g_var = -1; msg_1042(&m, &g_inst, NULL);
    return g_var;
}

int main(void)
{
    const Vec3 SW = { 1844, -820, -2283 }, TOZ = { 0, 0, -300 };   /* switch 279 of W1A and its marker, docs/OBJECTS.md 1.2 */
    printf("1. the brake 0x458e40 only brakes a charge run\n");
    for (int a = 0; a <= 11; a++) {
        Player *p = a_player(); switch_at(SW, TOZ);
        p->atk = a; p->atk_t = 9.0f; p->charge = 1.2f;
        player_brake_charge(p);
        int braked = p->atk != a || p->atk_t != 9.0f || p->charge != 1.2f;      /* did the call touch anything at all? */
        CHECK(braked == (a == 9 || a == 10), "atk state braked iff it is the charge run", "atk %2d -> %2d%s", a, p->atk, braked ? ", braked" : ", untouched");
        if (braked) {
            CHECK(fabsf(p->atk_t - 0.5f) < 1e-4f, "brake timer = AnimLen(0x12,0) + AnimLen(0x12,1)", "%.3f s", p->atk_t);
            CHECK(p->charge == 0 && p->move_lock == p->atk_t && p->speed == 0, "charge cleared and the Mover locked", "charge %.2f lock %.3f", p->charge, p->move_lock);
            CHECK(log_anim(0x12)->sub[0] == 51, "and atk 11 plays the peck animation 0x12", "raw anim %d", log_anim(0x12)->sub[0]);
        }
    }

    printf("\n2. the 1042 test itself (dist 500, angle 60, as all 15 switches of W1A use it)\n");
    {
        struct { const char *what; float dx, dz; float yaw_deg; int want; } t[] = {
            { "in the cone, 100 in front of the marker start",   0,  100,   180, 1 },
            { "same spot, 900 away",                             0,  900,   180, 0 },
            { "in range, facing 90 degrees off the marker",      0,  100,    90, 0 },
            { "in range, facing away from the marker",           0,  100,     0, 0 },
            { "just inside the cone (59 deg off)",               0,  100,   121, 1 },
            { "just outside the cone (61 deg off)",              0,  100,   119, 0 },
            { "exactly 500 away (the test is <=)",               0,  500,   180, 1 },
            { "501 away",                                        0,  501,   180, 0 },
            { "beside it, xz distance only (y is ignored)",    300,  300,   180, 1 },
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            Player *p = a_player(); switch_at(SW, TOZ);
            p->pos = (Vec3){ SW.x + t[i].dx, SW.y, SW.z + t[i].dz }; p->yaw = t[i].yaw_deg * 3.14159265f / 180.0f;
            int v = ask_1042(p, 500, 60);
            CHECK(v == t[i].want, t[i].what, "var %d (wanted %d)", v, t[i].want);
        }
    }

    printf("\n3. only a Perso in state 0 on the ground answers (0x44bcf0 + the state test)\n");
    {
        const char *name[] = { "free", "airborne", "dead", "in a scripted action", "hanging in a wall", "climbing over", "riding a rocket" };
        for (int k = 0; k < 7; k++) {
            Player *p = a_player(); switch_at(SW, TOZ);
            p->pos = (Vec3){ SW.x, SW.y, SW.z + 100 }; p->yaw = 3.14159265f;
            if (k == 1) p->on_ground = 0; else if (k == 2) p->dead_kind = 3; else if (k == 3) p->script_act = 17;
            else if (k == 4) p->climb_sub = 2; else if (k == 5) p->use_root = 1; else if (k == 6) p->ride = &g_inst;
            int v = ask_1042(p, 500, 60);
            CHECK(v == (k == 0), name[k], "var %d", v);
        }
    }

    printf("\n4. a switch that says yes brakes the run, one that says no leaves it alone\n");
    for (int in_cone = 0; in_cone <= 1; in_cone++) {
        Player *p = a_player(); switch_at(SW, TOZ);
        p->pos = (Vec3){ SW.x, SW.y, SW.z + 100 }; p->yaw = in_cone ? 3.14159265f : 0;
        p->atk = 9; p->atk_t = 0.4f; p->charge = 1.2f;
        int v = ask_1042(p, 500, 60);
        CHECK(v == in_cone && (p->atk == 11) == in_cone, in_cone ? "in the cone: var 1 and atk 9 -> 11" : "outside: var 0 and atk stays 9", "var %d atk %d", v, p->atk);
    }
    {   /* the attack states that are not a charge run are never touched by a switch */
        Player *p = a_player(); switch_at(SW, TOZ);
        p->pos = (Vec3){ SW.x, SW.y, SW.z + 100 }; p->yaw = 3.14159265f; p->atk = 2;
        int v = ask_1042(p, 500, 60);
        CHECK(v == 1 && p->atk == 2, "a peck dash flying past is answered but not braked", "var %d atk %d", v, p->atk);
    }

    printf("\n5. the frame the player pecks the switch: release -> charge run -> 1042 brakes it\n");
    {
        const float dt = 1.0f / 100.0f;
        float run[2] = { 0, 0 }; int atk_after[2] = { 0, 0 };
        for (int brake = 0; brake <= 1; brake++) {
            Player *p = a_player(); switch_at(SW, TOZ);
            p->pos = (Vec3){ SW.x, SW.y, SW.z + 100 }; p->yaw = 3.14159265f;      /* on the marker, looking the way it points */
            Vec3 start = p->pos;
            PlayerInput in; memset(&in, 0, sizeof in);
            in.action = 1; for (int f = 0; f < 50; f++) frame(p, &in, dt);         /* half a second of charging */
            float charged = p->charge;
            in.action = 0; frame(p, &in, dt);                                       /* the release: 0x457330 starts the run */
            CHECK(p->atk == 9 && charged > 0.1f, "releasing the button starts the charge run", "atk %d charge %.2f", p->atk, charged);
            if (brake) ask_1042(p, 500, 60);                                        /* the VM tick of that same frame */
            for (int f = 0; f < 300; f++) frame(p, &in, dt);                        /* 3 s: long enough for the whole charge (1.5 s) plus its own brake */
            run[brake] = fabsf(p->pos.z - start.z); atk_after[brake] = p->atk;
        }
        CHECK(run[0] > 1000.0f, "without the brake he storms on at 700 u/s and runs into the switch", "%.0f units", run[0]);
        CHECK(run[1] < 10.0f, "with it he pecks where he stands", "%.0f units", run[1]);
        CHECK(atk_after[0] == 0 && atk_after[1] == 0, "and both are back in atk 0 afterwards", "%d / %d", atk_after[0], atk_after[1]);
    }

    printf("\n6. the marker, not the instance: standing behind a switch never triggers it\n");
    {   /* model 41 has its marker start on the instance origin and pointing -z; the lever is on the -z side, so the
           player has to stand on the +z side and look at it. From the other side the cone test fails. */
        Player *p = a_player(); switch_at(SW, TOZ);
        p->pos = (Vec3){ SW.x, SW.y, SW.z - 100 }; p->yaw = 0;                      /* behind it, looking back at it */
        int v = ask_1042(p, 500, 60);
        CHECK(v == 0, "behind the switch, looking straight at it", "var %d", v);
        p->pos = (Vec3){ SW.x, SW.y, SW.z - 100 }; p->yaw = 3.14159265f;            /* behind it, facing the marker way */
        v = ask_1042(p, 500, 60);
        CHECK(v == 1, "behind the switch but facing the way the marker points", "var %d", v);
    }

    printf("\n%s (%d failed)\n", fails ? "FAILED" : "all checks passed", fails);
    return fails != 0;
}
