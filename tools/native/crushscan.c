/* crushscan: where could the crush test 0x462a40 kill Woody (Kill(4), docs/PERSO_MOVE.md 6.6)? For every instance with press
 * nodes (kind 1: the only nodes the ray 0x4359b0 sees) and every animation of its model, the press nodes are posed at 64 phases
 * and compared with the world floor under / the world ceiling over the centre of each node's box:
 *   A  "press": the node comes down to less than 57 units (0.3 * (193 - 2)) above the floor under it, and at another phase stands
 *      at least 193 above it (room for Woody to stand there) - an ANIMATING instance over him on the ground (hit kind 2, +0xa0 != 0);
 *   B  "lift": the node's top rises to less than 57 under the ceiling over it and at another phase leaves 193 of room - Woody
 *      carried on it into world geometry (kind 1/3 with Perso+0x298 = the platform).
 * Candidates only: whether the script ever runs that animation, and whether Woody can get there, is checked by hand.
 * build: python -m ziglang cc -std=c99 -O2 -Isrc -o out/crushscan.exe tools/native/crushscan.c src/level.c
 * run:   out/crushscan.exe <extract/Data> [LVL ...]       (no LVL: all 28 levels) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "level.h"

static const char *k_levels[] = { "House", "WWS", "W1A", "W1B", "W2A", "W2B", "W2D", "W3A", "W3B", "W3C", "W3D", "KWS", "K1A", "K1R",
    "K2A", "K2R", "K3A", "K3R", "SWS", "S1A", "S1R", "S2A", "S2R", "S3A", "S3R", "Blackbox", "Credits", NULL };

/* the world polygon straight above / below (x, z): highest floor with y <= ymax (want_floor) or lowest ceiling with y >= ymin */
static int world_y(const GelFile *g, float x, float z, float y0, float y1, int want_floor, float lim, float *out)
{
    Vec3 a = { x, y1, z }, b = { x, y0, z }; GelPolySet ps = gel_polys_on_seg(g, a, b); int found = 0;
    for (uint32_t k = 0; k < ps.n; k++) {
        const GelPoly *p = &g->polys[ps.polys[k]]; if (p->nverts < 3) continue;
        float ny = p->plane[1]; if (want_floor ? ny < 0.6f : ny > -0.6f) continue;
        float y = -(p->plane[0] * x + p->plane[2] * z + p->plane[3]) / ny;
        if (want_floor ? y > lim : y < lim) continue;
        int in = 0;                                                   /* point in the polygon, projected on xz */
        for (uint32_t i = 0, j = p->nverts - 1; i < p->nverts; j = i++) {
            const GelVert *vi = &g->verts[p->indices[i]], *vj = &g->verts[p->indices[j]];
            if ((vi->z > z) != (vj->z > z) && x < (vj->x - vi->x) * (z - vi->z) / (vj->z - vi->z) + vi->x) in = !in;
        }
        if (!in) continue;
        if (!found || (want_floor ? y > *out : y < *out)) { *out = y; found = 1; }
    }
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "crushscan <Data dir> [LVL ...]\n"); return 1; }
    const char **lv = argc > 2 ? (const char **)argv + 2 : k_levels;
    for (; *lv; lv++) {
        char p[600]; GelFile g; InsFile f;
        snprintf(p, sizeof p, "%s/%s/%s.gel", argv[1], *lv, *lv); if (gel_load(&g, p)) continue;
        snprintf(p, sizeof p, "%s/%s/%s.ins", argv[1], *lv, *lv); if (ins_load(&f, p)) { gel_free(&g); continue; }
        for (uint32_t mi = 0; mi < f.nmodels; mi++) {
            Model *m = &f.models[mi]; uint32_t nc; const uint32_t *cn = ins_collision_nodes(m, &nc);
            if (!nc || !m->nanims) continue;
            for (uint32_t k = 0; k < m->ninstances; k++) {
                Instance *in = &m->instances[k];
                for (uint32_t an = 0; an < m->nanims; an++) {
                    float L = m->anims[an].duration_s; if (L <= 0) continue;
                    for (uint32_t c = 0; c < nc; c++) {
                        float bot[64], top[64], cx = 0, cz = 0; int ok = 1;
                        for (int s = 0; s < 64 && ok; s++) {
                            float b[6]; ins_pose(in, (int)an, L * s / 64.0f);
                            if (!ins_node_world_box(in, cn[c], b)) { ok = 0; break; }
                            bot[s] = b[2]; top[s] = b[3]; if (!s) { cx = (b[0] + b[1]) * 0.5f; cz = (b[4] + b[5]) * 0.5f; }
                        }
                        if (!ok) continue;
                        float bmin = bot[0], bmax = bot[0], tmin = top[0], tmax = top[0];
                        for (int s = 1; s < 64; s++) { if (bot[s] < bmin) bmin = bot[s]; if (bot[s] > bmax) bmax = bot[s]; if (top[s] < tmin) tmin = top[s]; if (top[s] > tmax) tmax = top[s]; }
                        if (bmax - bmin < 1.0f && tmax - tmin < 1.0f) continue;          /* the node does not move up or down */
                        float fl, ce;
                        if (world_y(&g, cx, cz, bmin - 4000, bmax, 1, bmax, &fl)) {
                            float gmin = bmin - fl, gmax = bmax - fl;
                            if (gmin < 57.3f && gmax >= 193.0f)
                                printf("%s A press: inst %u model %u anim %u node %u at %.0f %.0f: bottom %.0f..%.0f over floor %.0f (gap %.0f..%.0f)\n", *lv, in->index, mi, an, cn[c], cx, cz, bmin, bmax, fl, gmin, gmax);
                        }
                        if (world_y(&g, cx, cz, tmin, tmax + 4000, 0, tmin, &ce)) {
                            float gmin = ce - tmax, gmax = ce - tmin;
                            if (gmin < 57.3f && gmax >= 193.0f)
                                printf("%s B lift: inst %u model %u anim %u node %u at %.0f %.0f: top %.0f..%.0f under ceiling %.0f (gap %.0f..%.0f)\n", *lv, in->index, mi, an, cn[c], cx, cz, tmin, tmax, ce, gmin, gmax);
                        }
                    }
                }
            }
        }
        ins_free(&f); gel_free(&g);
    }
    return 0;
}
