/* Test harness: load a level's `code` file, run the init and some ticks, print the message trace.
 * usage: ekorun <code file> [ticks]
 * Output format matches tools/ekovm.py --trace ("  SEND id [args]") so the two can be diffed. */
#include "ekovm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_msg(EkoVM *vm, const EkoMsg *m, void *user)
{
    (void)vm; (void)user;
    printf("  SEND %u [", m->id);
    for (uint32_t i = 0; i < m->nargs; i++) {
        uint32_t a = m->args[i];
        if (a >= 0x1000000) printf("%s'0x%x'", i ? ", " : "", a); else printf("%s'%d'", i ? ", " : "", (int32_t)a);
    }
    printf("]\n");
}
static void on_warn(EkoVM *vm, const char *s, void *user) { (void)vm; (void)user; fprintf(stderr, "warning: %s\n", s); }

/* MSVC CRT rand(), as linked into Woody.exe: deterministic and shared with tools/ekovm.py */
static uint32_t g_seed = 1;
static uint32_t msvc_rand(void *user) { (void)user; g_seed = g_seed * 214013u + 2531011u; return (g_seed >> 16) & 0x7fff; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: ekorun <code file> [ticks]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)sz); if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { perror("read"); return 1; } fclose(f);
    EkoVM vm;
    if (eko_load(&vm, buf, (size_t)sz) != 0) { fprintf(stderr, "not an EKO CODE file\n"); return 1; }
    vm.on_msg = on_msg; vm.on_warn = on_warn; vm.rand_fn = msvc_rand;
    eko_init(&vm);
    int ticks = argc > 2 ? atoi(argv[2]) : 60;
    int total = 0;
    for (int t = 1; t <= ticks; t++) { total += eko_tick(&vm, t); }
    fprintf(stderr, "%s: objects=%u vars=%u volumes=%u strings=%u collisions=%u msgs(pass2)=%d ticks=%d objects_run=%d delays_run=%u error=%d sp=%d bsp=%d\n",
            argv[1], vm.nobj, vm.nvars, vm.nvol, vm.nstr, vm.ncol, vm.nmsgs, ticks, total, vm.stat_delays_run, vm.error, vm.sp, vm.bsp);
    eko_free(&vm); free(buf);
    return 0;
}
