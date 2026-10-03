/* pack.c - makes the single-file build for your own use (make_standalone.bat): the launcher stub (bundle.c) + WoodyRE.exe + the
 * game files of the manifest (src/datafiles.h) from your copy of the CD, in the payload layout bundle.c documents.
 * The result holds the game's data: it is for your own machine only, never share it.
 * Usage: pack <stub.exe> <WoodyRE.exe> <folder with Data, Common, Logo, Game, Music.bf> <out.exe>
 * Build: zig cc -std=c99 -O2 -o pack.exe tools/native/pack.c */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/datafiles.h"

static int append(FILE *o, const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "pack: cannot open %s\n", path); return 0; }
    static unsigned char buf[4 << 20]; size_t n; *size = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) { if (fwrite(buf, 1, n, o) != n) { fclose(f); return 0; } *size += n; }
    fclose(f); return 1;
}

static void put(FILE *o, const void *p, size_t n) { fwrite(p, 1, n, o); }

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: pack <stub.exe> <WoodyRE.exe> <game folder> <out.exe>\n"); return 2; }
    FILE *o = fopen(argv[4], "wb"); if (!o) { fprintf(stderr, "pack: cannot write %s\n", argv[4]); return 1; }
    uint64_t sz, offs[DATAFILES_COUNT + 1], sizes[DATAFILES_COUNT + 1];
    if (!append(o, argv[1], &sz)) return 1;
    offs[0] = (uint64_t)_ftelli64(o);
    if (!append(o, argv[2], &sizes[0])) return 1;                      /* entry 0: woody.exe, as the launcher runs it */
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        char p[1024]; snprintf(p, sizeof p, "%s/%s", argv[3], k_datafiles[i].path);
        offs[i + 1] = (uint64_t)_ftelli64(o);
        if (!append(o, p, &sizes[i + 1])) { fclose(o); remove(argv[4]); return 1; }
        if (i % 40 == 0) printf("pack: %d / %d files\n", i, DATAFILES_COUNT);
    }
    uint64_t ioff = (uint64_t)_ftelli64(o);
    for (int i = 0; i <= DATAFILES_COUNT; i++) {
        const char *name = i ? k_datafiles[i - 1].path : "woody.exe"; uint32_t nl = (uint32_t)strlen(name);
        put(o, &nl, 4); put(o, name, nl); put(o, &offs[i], 8); put(o, &sizes[i], 8);
    }
    uint32_t count = DATAFILES_COUNT + 1; uint64_t id = (uint64_t)time(NULL) * 1000003u ^ (uint64_t)ioff;
    put(o, &ioff, 8); put(o, &count, 4); put(o, &id, 8); put(o, "WOODYPK1", 8);
    if (fclose(o)) { fprintf(stderr, "pack: write error\n"); return 1; }
    printf("pack: %s - %u files, %.0f MB\n", argv[4], count, (double)ioff / (1 << 20));
    return 0;
}
