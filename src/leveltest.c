/* leveltest.c - parse all levels with the C loaders (regression test against the Python parsers).
 * usage: leveltest <Data dir> */
#include "level.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *LEVELS[] = { "House", "WWS", "W1A", "W1B", "W2A", "W2B", "W2D", "W3A", "W3B", "W3C", "W3D", "KWS", "K1A", "K1R", "K2A", "K2R",
    "K3A", "K3R", "SWS", "S1A", "S1R", "S2A", "S2R", "S3A", "S3R", "Blackbox", "Credits", "Lang", NULL };

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "extract/Data";
    int fail = 0;
    for (int i = 0; LEVELS[i]; i++) {
        char p[512]; TexFile t; GelFile g; InsFile f;
        snprintf(p, sizeof p, "%s/%s/%s.tex", dir, LEVELS[i], LEVELS[i]); int a = tex_load(&t, p);
        snprintf(p, sizeof p, "%s/%s/%s.gel", dir, LEVELS[i], LEVELS[i]); int b = gel_load(&g, p);
        snprintf(p, sizeof p, "%s/%s/%s.ins", dir, LEVELS[i], LEVELS[i]); int c = ins_load(&f, p);
        uint32_t ninst = 0, nnodes = 0; for (uint32_t k = 0; k < f.nmodels; k++) { ninst += f.models[k].ninstances; nnodes += f.models[k].nnodes; }
        printf("%-9s tex %s (%u groups, %u materials)  gel %s (%u polys, %u verts)  ins %s (%u models, %u nodes, %u instances, %u cams)\n",
               LEVELS[i], a ? "FAIL" : "ok", t.ngroups, t.nmaterials, b ? "FAIL" : "ok", g.npolys, g.nverts, c ? "FAIL" : "ok", f.nmodels, nnodes, ninst, f.ncameras);
        if (a || b || c) fail++;
        /* pose test: evaluate Woody's first animation at a few times */
        if (!c && f.nmodels && f.models[0].ninstances) {
            Instance *w = &f.models[0].instances[0];
            for (int s = 0; s < 3; s++) ins_pose(w, 0, s * 0.37f);
            if (f.models[0].ntris) { Vec3 v = ins_point_world(w, f.models[0].tris[0].i0); printf("          woody point0 at %.1f %.1f %.1f\n", v.x, v.y, v.z); }
        }
        if (!a) tex_free(&t); if (!b) gel_free(&g); if (!c) ins_free(&f);
    }
    printf("%d failures\n", fail);
    return fail ? 1 : 0;
}
