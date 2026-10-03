/* datasetup_posix.c - datasetup.h outside Windows (Linux, Steam Deck; the Windows build has datasetup.c). Looks for the game
 * files in this order: WOODY_DATA (a Data dir), data/ next to the executable, the CD files straight next to it, extract/ in
 * the current directory (a development checkout), $XDG_DATA_HOME/WoodyRE/data (~/.local/share/WoodyRE/data). Nothing there:
 * looks for the CD (or a mounted ISO image) under /media, /run/media and /mnt, asks, and copies the 232 files of the
 * manifest (src/datafiles.h) into ~/.local/share/WoodyRE/data with their SHA-1 checked. The folder that holds data/ becomes
 * the current directory, so woodyre.cfg, woodyre.sav and mods/ live beside it. Names on the disc are matched ignoring case. */
#ifndef _WIN32
#include "datasetup.h"
#include "datafiles.h"
#include "plat.h"
#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define PMAX 4096

/* ---- SHA-1 (FIPS 180-1) ---------------------------------------------------------------------------------------------- */
typedef struct { uint32_t h[5]; uint64_t n; unsigned char b[64]; unsigned nb; } Sha1;
static uint32_t rol(uint32_t x, int k) { return x << k | x >> (32 - k); }
static void sha1_block(Sha1 *s, const unsigned char *p)
{
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; } else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha1_init(Sha1 *s) { static const uint32_t h0[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 }; memcpy(s->h, h0, sizeof h0); s->n = 0; s->nb = 0; }
static void sha1_add(Sha1 *s, const unsigned char *p, size_t n)
{
    s->n += n;
    while (n) { size_t k = 64 - s->nb < n ? 64 - s->nb : n; memcpy(s->b + s->nb, p, k); s->nb += (unsigned)k; p += k; n -= k; if (s->nb == 64) { sha1_block(s, s->b); s->nb = 0; } }
}
static void sha1_hex(Sha1 *s, char *hex)
{
    uint64_t bits = s->n * 8; unsigned char pad = 0x80, z = 0, len[8];
    sha1_add(s, &pad, 1); while (s->nb != 56) sha1_add(s, &z, 1);
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha1_add(s, len, 8);
    for (int i = 0; i < 20; i++) sprintf(hex + 2 * i, "%02x", (unsigned)(s->h[i / 4] >> (24 - 8 * (i % 4)) & 0xff));
}

/* ---- folders --------------------------------------------------------------------------------------------------------- */
static int readable(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return 0; fclose(f); return 1; }   /* fopen = plat_fopen: any case */
static int cd_layout(const char *root)
{
    static const char *probe[3] = { "Data/W1A/W1A.gel", "Common/Woody.rck", "Music.bf" };   /* Music.bf is copied last */
    char p[PMAX];
    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/%s", root, probe[i]); if (!readable(p)) return 0; }
    return 1;
}
static void exe_dir(char *d)
{
    ssize_t n = readlink("/proc/self/exe", d, PMAX - 1);
    if (n <= 0) { strcpy(d, "."); return; }
    d[n] = 0; char *s = strrchr(d, '/'); if (s) *s = 0;
}
static int home_dir(char *d)                                /* $XDG_DATA_HOME/WoodyRE or ~/.local/share/WoodyRE (not created here) */
{
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    if (x && *x) { snprintf(d, PMAX, "%s/WoodyRE", x); return 1; }
    if (h && *h) { snprintf(d, PMAX, "%s/.local/share/WoodyRE", h); return 1; }
    return 0;
}
static void make_dirs(char *path)                           /* every parent directory of path */
{
    for (char *p = path + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
}
static const char *enter(const char *home, const char *rel) { return chdir(home) ? NULL : rel; }
static int ask(const char *text, const char *yes, const char *no)   /* 1 = yes, 0 = no, -1 = cancel */
{
    const SDL_MessageBoxButtonData b[3] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes }, { 0, 0, no }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -1, "Cancel" } };
    const SDL_MessageBoxData m = { SDL_MESSAGEBOX_INFORMATION, NULL, "WoodyRE", text, no ? 3 : 2, no ? b : (const SDL_MessageBoxButtonData[]){ b[0], b[2] }, NULL };
    int r = -1; if (SDL_ShowMessageBox(&m, &r)) { fprintf(stderr, "%s\n", text); return -1; }
    return r;
}

/* reads one manifest file under src_root, writes it under dst_root when that is set (as name.part, renamed at the end) and
 * compares its SHA-1: 1 = equal, 0 = differs, -1 = missing (src) or cannot be written (dst) */
static int file_pass(const char *src_root, const char *dst_root, int i, unsigned char *buf, size_t bufsz, unsigned long long *done)
{
    char sp[PMAX], dp[PMAX], tp[PMAX + 8];
    snprintf(sp, sizeof sp, "%s/%s", src_root, k_datafiles[i].path);
    FILE *s = fopen(sp, "rb"); if (!s) return -1;
    FILE *d = NULL;
    if (dst_root) {
        snprintf(dp, sizeof dp, "%s/%s", dst_root, k_datafiles[i].path); make_dirs(dp); snprintf(tp, sizeof tp, "%s.part", dp);
        if (!(d = fopen(tp, "wb"))) { fclose(s); return -1; }
    }
    Sha1 h; sha1_init(&h); int ok = 1; unsigned long long size = 0; size_t got;
    while ((got = fread(buf, 1, bufsz, s)) > 0) {
        sha1_add(&h, buf, got);
        if (d && fwrite(buf, 1, got, d) != got) { ok = -1; break; }
        size += got; if (done) *done += got;
    }
    fclose(s);
    char hex[41]; sha1_hex(&h, hex);
    if (ok > 0 && (size != k_datafiles[i].size || strcmp(hex, k_datafiles[i].sha1))) ok = 0;
    if (d) { if (fclose(d) || ok < 0 || rename(tp, dp)) { remove(tp); return -1; } }
    return ok;
}
static int copy_cd(const char *src, const char *home)       /* the number of files that differ from the 1.00 CD, -1 = failed */
{
    char dst[PMAX], m[PMAX + 400]; snprintf(dst, sizeof dst, "%s/data", home); mkdir(dst, 0755);
    struct statvfs vf;
    if (!statvfs(home, &vf) && (unsigned long long)vf.f_bavail * vf.f_frsize < (unsigned long long)DATAFILES_BYTES + (16u << 20)) {
        snprintf(m, sizeof m, "Not enough free disk space for the game files (%u MB) in\n%s", DATAFILES_BYTES >> 20, dst); plat_message(m, 1); return -1;
    }
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return -1;
    unsigned long long done = 0; int bad = 0, first_bad = -1, last = -1;
    printf("data: copying the game files from %s to %s\n", src, dst); fflush(stdout);
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        int r = file_pass(src, dst, i, buf, bufsz, &done);
        if (r < 0) {
            snprintf(m, sizeof m, "Could not copy %s\nfrom %s/ to\n%s/\n\nIs the CD complete, and is there room on the disk?", k_datafiles[i].path, src, dst);
            plat_message(m, 1); free(buf); return -1;
        }
        if (!r) { bad++; if (first_bad < 0) first_bad = i; printf("data: %s differs from the English 1.00 CD\n", k_datafiles[i].path); }
        int pct = (int)(done * 100 / DATAFILES_BYTES); if (pct / 10 != last) { last = pct / 10; printf("data: %d%%\n", pct); fflush(stdout); }
    }
    free(buf);
    if (bad) {
        snprintf(m, sizeof m, "%d of the copied files differ from the English 1.00 CD (the first: %s).\n\n"
                              "WoodyRE is made for that version; another release or a damaged copy may not work correctly.", bad, k_datafiles[first_bad].path);
        plat_message(m, 1);
    }
    return bad;
}
static int find_cd(char *root)                              /* a mounted CD or ISO image with the CD layout */
{
    const char *user = getenv("USER"); char bases[4][PMAX]; int nb = 0;
    if (user) { snprintf(bases[nb++], PMAX, "/media/%s", user); snprintf(bases[nb++], PMAX, "/run/media/%s", user); }
    snprintf(bases[nb++], PMAX, "/media"); snprintf(bases[nb++], PMAX, "/mnt");
    for (int b = 0; b < nb; b++) {
        DIR *d = opendir(bases[b]); struct dirent *de; if (!d) continue;
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.') continue;
            snprintf(root, PMAX, "%s/%s", bases[b], de->d_name);
            if (cd_layout(root)) { closedir(d); return 1; }
        }
        closedir(d);
    }
    return 0;
}

const char *data_find(void)
{
    const char *env = getenv("WOODY_DATA"); if (env && *env) return env;
    char exe[PMAX], home[PMAX], p[PMAX + 16];
    exe_dir(exe); int have_home = home_dir(home);
    snprintf(p, sizeof p, "%s/data", exe); if (cd_layout(p)) return enter(exe, "data/Data");
    if (cd_layout(exe)) return enter(exe, "Data");
    if (cd_layout("extract")) return "extract/Data";
    if (have_home) { snprintf(p, sizeof p, "%s/data", home); if (cd_layout(p)) return enter(home, "data/Data"); }
    if (!have_home) { plat_message("No folder for the game files ($HOME is not set).", 1); return NULL; }

    /* first start: copy the CD into ~/.local/share/WoodyRE/data */
    for (;;) {
        char src[PMAX], m[3 * PMAX];
        if (find_cd(src)) {
            snprintf(m, sizeof m, "Found the Woody Woodpecker CD at %s\n\nCopy its game files (%u MB) to\n%s/data ?\n\n"
                                  "This happens once; afterwards the CD is not needed. It takes a few minutes and the window\n"
                                  "only opens when it is done.", src, DATAFILES_BYTES >> 20, home);
            int r = ask(m, "Copy", NULL);
            if (r != 1) return NULL;
            make_dirs(strcat(strcpy(p, home), "/"));
            if (copy_cd(src, home) < 0) return NULL;
            return enter(home, "data/Data");
        }
        snprintf(m, sizeof m, "WoodyRE needs the files of the original game CD-ROM:\n"
                              "Woody Woodpecker: Escape from Buzz Buzzard Park (PC, English version).\n\n"
                              "Insert the CD or mount your ISO image of it, then press Search.\n"
                              "Or copy Data, Common, Logo, Game and Music.bf from the CD into\n%s/data\nyourself and start WoodyRE again.", home);
        if (ask(m, "Search", NULL) != 1) return NULL;
    }
}

int data_verify(const char *data_dir)
{
    char root[PMAX]; snprintf(root, sizeof root, "%s/..", data_dir);
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return -1;
    int bad = 0;
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        int k = file_pass(root, NULL, i, buf, bufsz, NULL);
        if (k <= 0) { bad++; printf("verify: %s %s\n", k_datafiles[i].path, k < 0 ? "MISSING" : "differs"); }
    }
    free(buf);
    printf("verify: %d of %d files equal the English 1.00 CD%s\n", DATAFILES_COUNT - bad, DATAFILES_COUNT, bad ? "" : " - all good");
    return bad;
}
#endif
