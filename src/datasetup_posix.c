/* datasetup_posix.c - datasetup.h outside Windows (Linux, Steam Deck; the Windows build has datasetup.c). Looks for the game
 * files in this order: WOODY_DATA (a Data dir), data/ next to the executable, the CD files straight next to it, extract/ in
 * the current directory (a development checkout), $XDG_DATA_HOME/WoodyRE/data (~/.local/share/WoodyRE/data). Nothing there:
 * looks for the CD (or a mounted ISO image) under /media, /run/media and /mnt, asks, and copies the 232 files of the
 * manifest (src/datafiles.h) into ~/.local/share/WoodyRE/data with their SHA-1 checked. The folder that holds data/ becomes
 * the current directory, so woodyre.cfg, woodyre.sav and mods/ live beside it. Names on the disc are matched ignoring case.
 * Android has its own data_find and folder (below): the app's folder, filled from an ISO image or a folder the user picks. */
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
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s", root, probe[i]);
#ifdef __EMSCRIPTEN__
        if (!readable(p) && i == 2) snprintf(p, sizeof p, "/woody-big/%s", probe[i]);   /* big files live outside the IDBFS mount */
#endif
        if (!readable(p)) return 0;
    }
    return 1;
}
#ifndef __ANDROID__
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
#endif
static void make_dirs(char *path)                           /* every parent directory of path */
{
    for (char *p = path + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
}
static const char *enter(const char *home, const char *rel) { return chdir(home) ? NULL : rel; }
static int ask(const char *text, const char *yes, const char *no)   /* 1 = yes, 0 = no, -1 = cancel */
{
#ifdef __ANDROID__
    return plat_dialog(text, yes, no, "Cancel");
#endif
    const SDL_MessageBoxButtonData b[3] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes }, { 0, 0, no }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -1, "Cancel" } };
    const SDL_MessageBoxData m = { SDL_MESSAGEBOX_INFORMATION, NULL, "WoodyRE", text, no ? 3 : 2, no ? b : (const SDL_MessageBoxButtonData[]){ b[0], b[2] }, NULL };
    int r = -1; if (SDL_ShowMessageBox(&m, &r)) { fprintf(stderr, "%s\n", text); return -1; }
    return r;
}

static int g_fit[DATAFILES_RELEASES];   /* per supported release: how many files of the last copy/check were its copy (datafile_tally) */
/* reads one manifest file under src_root, writes it under dst_root when that is set (as name.part, renamed at the end) and
 * compares its SHA-1: 1 = equal, 2 = missing as in a supported release, 0 = differs, -1 = missing (src) or cannot be written (dst) */
static int file_pass(const char *src_root, const char *dst_root, int i, unsigned char *buf, size_t bufsz, unsigned long long *done)
{
    char sp[PMAX], dp[PMAX], tp[PMAX + 8];
    snprintf(sp, sizeof sp, "%s/%s", src_root, k_datafiles[i].path);
    FILE *s = fopen(sp, "rb");
#ifdef __EMSCRIPTEN__
    if (!s) { const char *b = strrchr(k_datafiles[i].path, '/'); snprintf(sp, sizeof sp, "/woody-big/%s", b ? b + 1 : k_datafiles[i].path); s = fopen(sp, "rb"); }
#endif
    if (!s) return datafile_absent(i, g_fit) ? 2 : -1;   /* the Russian CD has no Data/Lang (never read) */
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
    if (ok > 0 && !datafile_tally(i, size, hex, g_fit)) ok = 0;
    if (d) { if (fclose(d) || ok < 0 || rename(tp, dp)) { remove(tp); return -1; } }
    return ok;
}
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
static int copy_cd(const char *src, const char *home)       /* the number of files that differ from the 1.00 CD, -1 = failed */
{
    char dst[PMAX], m[PMAX + 400]; snprintf(dst, sizeof dst, "%s/data", home); mkdir(dst, 0755);
    struct statvfs vf;
    if (!statvfs(home, &vf) && (unsigned long long)vf.f_bavail * vf.f_frsize < (unsigned long long)DATAFILES_BYTES + (16u << 20)) {
        snprintf(m, sizeof m, "Not enough free disk space for the game files (%u MB) in\n%s", DATAFILES_BYTES >> 20, dst); plat_message(m, 1); return -1;
    }
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return -1;
    unsigned long long done = 0; int bad = 0, first_bad = -1, last = -1; memset(g_fit, 0, sizeof g_fit);
    printf("data: copying the game files from %s to %s\n", src, dst); fflush(stdout);
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        int r = file_pass(src, dst, i, buf, bufsz, &done);
        if (r < 0) {
            snprintf(m, sizeof m, "Could not copy %s\nfrom %s/ to\n%s/\n\nIs the CD complete, and is there room on the disk?", k_datafiles[i].path, src, dst);
            plat_message(m, 1); free(buf); return -1;
        }
        if (!r) { bad++; if (first_bad < 0) first_bad = i; printf("data: %s matches none of the supported CDs\n", k_datafiles[i].path); }
        int pct = (int)(done * 100 / DATAFILES_BYTES); if (pct / 10 != last) { last = pct / 10; printf("data: %d%%\n", pct); fflush(stdout); }
    }
    free(buf);
    printf("data: copied from the %s CD\n", k_releases[datafile_best(g_fit)]);
    if (bad) {
        snprintf(m, sizeof m, "%d of the copied files match none of the supported CDs (the first: %s).\n\n"
                              "WoodyRE supports the English 1.00, Brazilian, Polish, Spanish and Russian CDs; another release or a damaged copy may not work correctly.", bad, k_datafiles[first_bad].path);
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

/* the game files in dir/rel next to the executable: dir becomes the current directory (woodyre.cfg, .sav, .log, mods/ beside
 * them) unless it cannot be written (/opt, /usr, a read-only mount): then home does, and the files are read by their full path */
static const char *enter_exe(const char *dir, const char *rel, const char *home)
{
    static char out[PMAX + 16]; char h[PMAX + 2];
    if (!home || access(dir, W_OK) == 0) return enter(dir, rel);
    snprintf(out, sizeof out, "%s/%s", dir, rel); snprintf(h, sizeof h, "%s/", home); make_dirs(h);
    return chdir(home) ? enter(dir, rel) : out;
}

const char *data_find(void)
{
    const char *env = getenv("WOODY_DATA"); if (env && *env) return env;
    char exe[PMAX], home[PMAX], p[PMAX + 16];
    exe_dir(exe); int have_home = home_dir(home);
    snprintf(p, sizeof p, "%s/data", exe); if (cd_layout(p)) return enter_exe(exe, "data/Data", have_home ? home : NULL);
    if (cd_layout(exe)) return enter_exe(exe, "Data", have_home ? home : NULL);
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
                              "Woody Woodpecker: Escape from Buzz Buzzard Park (PC; the English, Brazilian, Polish, Spanish or Russian CD).\n\n"
                              "Insert the CD or mount your ISO image of it, then press Search.\n"
                              "Or copy Data, Common, Logo, Game and Music.bf from the CD into\n%s/data\nyourself and start WoodyRE again.", home);
        if (ask(m, "Search", NULL) != 1) return NULL;
    }
}
#else
/* ---- Android and the web build: the game files come from what the user picks once - an ISO image of the CD (read here,
 * by the ISO 9660 parser below) or a folder with a copy of it. Android goes through the system's file picker
 * (WoodyActivity.java, which hands out a file descriptor for the ISO); the web build's shell (web/shell.html) hands over
 * the picked File object of the ISO, or writes a picked folder copy into the virtual file system (see data_find below).
 * Nothing is uploaded anywhere: everything stays on the device / in the browser's own storage. */
#include <fcntl.h>
#ifdef __ANDROID__
#include <jni.h>

static int home_dir(char *d)
{
    const char *p = SDL_AndroidGetExternalStoragePath(); if (!p) p = SDL_AndroidGetInternalStoragePath();
    if (!p) return 0;
    snprintf(d, PMAX, "%s", p); return 1;
}
/* WoodyActivity.pickGameData(kind, dest): kind 1 = an ISO image, returns its file descriptor; kind 2 = a folder, copied
 * into dest by the Java side, returns 0. -1 = cancelled, -2 = failed (the Java side said why). */
static int java_pick(int kind, const char *dest)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv(); jobject act = (jobject)SDL_AndroidGetActivity(); int r = -2;
    if (!env || !act) return -2;
    jclass c = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetStaticMethodID(env, c, "pickGameData", "(ILjava/lang/String;)I");
    if (m) { jstring s = (*env)->NewStringUTF(env, dest ? dest : ""); r = (*env)->CallStaticIntMethod(env, c, m, kind, s); (*env)->DeleteLocalRef(env, s); }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); r = -2; }
    (*env)->DeleteLocalRef(env, c); (*env)->DeleteLocalRef(env, act);
    return r;
}
static void java_progress(const char *text)                 /* WoodyActivity.progress: a dialog with this text, NULL closes it */
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv(); jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return;
    jclass c = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetStaticMethodID(env, c, "progress", "(Ljava/lang/String;)V");
    if (m) { jstring s = text ? (*env)->NewStringUTF(env, text) : NULL; (*env)->CallStaticVoidMethod(env, c, m, s); if (s) (*env)->DeleteLocalRef(env, s); }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c); (*env)->DeleteLocalRef(env, act);
}
#endif   /* __ANDROID__ (java glue) */

/* the progress dialog of a copy / check: Android shows it in the app, the web build in the page */
static void data_progress(const char *text)
{
#ifdef __ANDROID__
    java_progress(text);
#else
    EM_ASM({ if (window.WoodyShell) WoodyShell.progress(UTF8ToString($0)); }, text);
#endif
}

static void progress_pct(const char *what, unsigned long long done, int *last)
{
    int pct = (int)(done * 100 / DATAFILES_BYTES); char m[128];
    if (pct == *last) return;
    *last = pct; snprintf(m, sizeof m, "%s %d %%", what, pct); data_progress(m);
}

/* ---- ISO 9660 (ECMA-119) with the Joliet names when there are some: just enough to find the manifest's files ---- */
typedef struct { int fd; uint32_t root_lba, root_len; int joliet; } Iso;
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
#ifdef __EMSCRIPTEN__
/* reads [off, off+n) of the ISO file the shell picked into the WASM heap; -1 = no file / read failed */
EM_ASYNC_JS(int, web_pread, (void *b, double off, double n), {
    const f = window.WoodyShell && WoodyShell.dataFile;
    if (!f) return -1;
    try { const buf = await f.slice(off, off + n).arrayBuffer(); HEAPU8.set(new Uint8Array(buf), b); return 0; }
    catch (e) { return -1; }
});
/* writes the virtual file system (/woody: the game files, woodyre.cfg, woodyre.sav) into the browser's IndexedDB */
EM_ASYNC_JS(int, web_syncfs, (void), {
    if (!window.WoodyShell || !WoodyShell.persistEnabled()) return 0;
    const show = window.__woodyFinalSave;                     /* only the copy's final save reports progress */
    if (show) WoodyShell.phase(-1, 'save');
    try { await new Promise((ok, bad) => FS.syncfs(false, e => e ? bad(e) : ok())); WoodyShell.saved();
          if (show) window.__woodyFinalSave = 0; return 0; }
    catch (e) { console.warn('WoodyRE: syncfs failed:', e); if (show) window.__woodyFinalSave = 0;
        WoodyShell.syncError(e && e.name == 'QuotaExceededError'
        ? 'O navegador n\u00e3o tem cota de armazenamento suficiente para guardar tudo (~650 MB). O jogo roda mesmo assim, mas na pr\u00f3xima visita ele pede a ISO de novo.'
        : 'N\u00e3o foi poss\u00edvel salvar os arquivos do jogo no navegador (a sess\u00e3o atual funciona mesmo assim).'); return -1; }
});
/* the same, silent on success: called while the extraction runs, every so often, so the IndexedDB never
 * has to take the whole copy in one transaction (a 650 MB write can outgrow a small machine's memory) */
EM_ASYNC_JS(int, web_syncfs_quiet, (void), {
    if (!window.WoodyShell || !WoodyShell.persistEnabled()) return 0;
    try { await new Promise((ok, bad) => FS.syncfs(false, e => e ? bad(e) : ok())); return 0; }
    catch (e) { console.warn('WoodyRE: syncfs failed:', e); return -1; }
});
/* Music.bf is ~300 MB and the engine only streams it: IDBFS would copy the whole file into IndexedDB on
 * every sync (and again on restore), which can outgrow a small machine's memory. So big files live in
 * /woody-big, OUTSIDE the IDBFS mount, and get their own chunked store (16 MB pieces, written once, as
 * Blobs so IndexedDB streams them to disk instead of cloning each one whole). With saving off (the quick
 * mode) nothing is written: the session runs from RAM and the next visit asks for the CD files again. */
EM_ASYNC_JS(int, web_bigfile_store, (const char *name), {
    try {
        const n = UTF8ToString(name), path = '/woody-big/' + n;
        if (!FS.analyzePath(path).exists) return 0;
        if (window.WoodyShell && !WoodyShell.persistEnabled()) {
            console.warn('WoodyRE: not keeping ' + n + ' (saving is off; the next visit asks for the CD files again)');
            return 0;
        }
        const size = FS.stat(path).size, CH = 16 << 20;
        const tick = (ms, what) => new Promise((_, bad) => setTimeout(() => bad(new Error('timeout: ' + what)), ms));
        console.warn('WoodyRE: store ' + n + ': opening db');
        const t0 = performance.now();
        const req = indexedDB.open('woodyre-big', 1);
        req.onupgradeneeded = () => req.result.createObjectStore('chunks');
        const db = await Promise.race([new Promise((ok, e) => { req.onsuccess = () => ok(req.result); req.onerror = () => e(req.error); }), tick(30000, 'db open')]);
        console.warn('WoodyRE: store ' + n + ': db open, ' + Math.ceil(size / CH) + ' chunks');
        if (window.WoodyShell) WoodyShell.phase(0, 'store');
        const f = FS.open(path, 'r');
        const buf = new Uint8Array(CH);
        try {
            for (let off = 0, i = 0; off < size; off += CH, i++) {
                const got = Math.min(CH, size - off);
                FS.read(f, buf, 0, got, off);
                const tx = db.transaction('chunks', 'readwrite');          // one transaction per chunk: IndexedDB
                tx.objectStore('chunks').put(new Blob([buf.subarray(0, got)]), n + ':' + i); // drains to disk instead of queueing
                await Promise.race([new Promise((ok, e) => { tx.oncomplete = ok; tx.onerror = () => e(tx.error); tx.onabort = () => e(tx.error); }), tick(30000, 'chunk ' + i)]); // ~300 MB at once
                if (window.WoodyShell) WoodyShell.phase(Math.round((i + 1) * 100 / Math.ceil(size / CH)), 'store');
            }
            const tx = db.transaction('chunks', 'readwrite');
            tx.objectStore('chunks').put({ size, chunk: CH }, n + ':meta');
            await Promise.race([new Promise((ok, e) => { tx.oncomplete = ok; tx.onerror = () => e(tx.error); }), tick(30000, 'meta')]);
        } finally { FS.close(f); db.close(); }
        console.warn('WoodyRE: the store of ' + n + ' took ' + ((performance.now() - t0) / 1000).toFixed(1) + ' s');
        console.warn('WoodyRE: ' + n + ' (' + Math.round(size / 1048576) + ' MB) stored in chunks for the next visits');
        if (window.WoodyShell) WoodyShell.progress('');
        return 0;
    } catch (e) { console.warn('WoodyRE: bigfile store failed:', e); if (window.WoodyShell) WoodyShell.progress(''); return -1; }
});
EM_ASYNC_JS(int, web_bigfile_restore, (const char *name), {
    try {
        const n = UTF8ToString(name), path = '/woody-big/' + n;
        if (FS.analyzePath(path).exists) { if (window.WoodyShell) WoodyShell.progress(''); return 1; }
        const req = indexedDB.open('woodyre-big', 1);
        req.onupgradeneeded = () => req.result.createObjectStore('chunks');
        const db = await new Promise((ok, e) => { req.onsuccess = () => ok(req.result); req.onerror = () => e(req.error); });
        const meta = await new Promise((ok, e) => { const t = db.transaction('chunks'), r = t.objectStore('chunks').get(n + ':meta'); r.onsuccess = () => ok(r.result); r.onerror = () => e(r.error); });
        if (!meta) { db.close(); if (window.WoodyShell) WoodyShell.progress(''); return 0; }
        if (window.WoodyShell) WoodyShell.phase(0, 'restore-big');
        FS.mkdirTree('/woody-big');
        const f = FS.open(path, 'w+');
        let blobs = [];
        try {
            const nc = Math.ceil(meta.size / meta.chunk);
            await new Promise((ok, e) => {                    // every get queued at once: no await between them,
                const tx = db.transaction('chunks'), st = tx.objectStore('chunks');   // the transaction never idles
                for (let i = 0; i < nc; i++) {
                    const r = st.get(n + ':' + i);
                    r.onsuccess = () => { blobs[i] = r.result; if (window.WoodyShell) WoodyShell.phase(Math.round((i + 1) * 100 / nc), 'restore-big'); };
                }
                tx.oncomplete = ok; tx.onerror = () => e(tx.error); tx.onabort = () => e(tx.error);
            });
        } catch (e) { db.close(); console.warn('WoodyRE: bigfile restore failed:', e); if (window.WoodyShell) WoodyShell.progress(''); return -1; }
        // outside the transaction: Blob -> bytes one chunk at a time (a Blob read with the transaction still
        // open can deadlock the browser, so this waits for oncomplete above)
        try {
            for (let i = 0; i * meta.chunk < meta.size; i++) {
                let c = blobs[i];
                if (!c) { if (window.WoodyShell) WoodyShell.progress(''); return -1; }   // incomplete store: caller falls back to the picker
                if (typeof Blob !== 'undefined' && c instanceof Blob) c = new Uint8Array(await c.arrayBuffer());   // stores written as Blobs
                FS.write(f, c, 0, c.length, i * meta.chunk);
            }
        } finally { blobs = null; FS.close(f); db.close(); }
        console.log('WoodyRE: ' + n + ' restored from the browser storage (no pick needed)');
        if (window.WoodyShell) WoodyShell.progress('');
        return 1;
    } catch (e) { console.warn('WoodyRE: bigfile restore failed:', e); if (window.WoodyShell) WoodyShell.progress(''); return -1; }
});
/* a folder copy (the shell writes every file under /woody/data) leaves Music.bf inside the IDBFS mount: move it
 * to /woody-big before anything syncs. FS.rename cannot cross the mount, so this streams a copy and unlinks. */
EM_JS(int, web_bigfile_relocate, (void), {
    try {
        if (!FS.analyzePath('/woody/data/Music.bf').exists) return 0;
        if (window.WoodyShell) WoodyShell.phase(-1, 'move');
        FS.mkdirTree('/woody-big');
        const s = FS.open('/woody/data/Music.bf', 'r'), d = FS.open('/woody-big/Music.bf', 'w+');
        const size = FS.stat('/woody/data/Music.bf').size, CH = 8 << 20, buf = new Uint8Array(CH);
        for (let off = 0; off < size; off += CH) {
            const got = Math.min(CH, size - off);
            FS.read(s, buf, 0, got, off);
            FS.write(d, buf, 0, got, off);
        }
        FS.close(s); FS.close(d);
        FS.unlink('/woody/data/Music.bf');
        if (window.WoodyShell) WoodyShell.progress('');
        return 0;
    } catch (e) { console.warn('WoodyRE: bigfile relocate failed:', e); if (window.WoodyShell) WoodyShell.progress(''); return -1; }
});
/* the big file is written slice by slice straight from the WASM heap: never a second 300 MB JS copy */
EM_JS(int, web_bigfile_begin, (const char *name), {
    try { FS.mkdirTree('/woody-big'); window.__bigf = FS.open('/woody-big/' + UTF8ToString(name), 'w+'); return 0; }
    catch (e) { console.warn('WoodyRE: bigfile begin failed:', e); return -1; }
});
EM_JS(int, web_bigfile_write, (double off, void *b, double n), {
    try { FS.write(window.__bigf, HEAPU8.subarray(b, b + n), 0, n, off); return 0; }
    catch (e) { console.warn('WoodyRE: bigfile write failed:', e); return -1; }
});
EM_JS(int, web_bigfile_end, (void), {
    try { if (window.__bigf) { FS.close(window.__bigf); window.__bigf = null; } return 0; }
    catch (e) { return -1; }
});
#endif
/* the ISO 9660 reader: Android reads a file descriptor; the web build reads the picked File object in the page
 * (web_pread above, a chunk at a time, so the whole image never sits in memory at once) */
static int iso_pread(int fd, void *b, size_t n, unsigned long long off)
{
#ifdef __EMSCRIPTEN__
    (void)fd;
    return (int)web_pread(b, (double)off, (double)n);
#else
    for (size_t got = 0; got < n; ) { ssize_t k = pread(fd, (char *)b + got, n - got, (off_t)(off + got)); if (k <= 0) return -1; got += (size_t)k; }
    return 0;
#endif
}
static int iso_open(Iso *is, int fd)
{
    unsigned char b[2048]; int have = 0; memset(is, 0, sizeof *is); is->fd = fd;
    for (uint32_t s = 16; s < 64; s++) {
        if (iso_pread(fd, b, sizeof b, (unsigned long long)s * 2048) || memcmp(b + 1, "CD001", 5)) break;
        if (b[0] == 255) break;
        int jol = b[0] == 2 && b[88] == '%' && b[89] == '/' && (b[90] == '@' || b[90] == 'C' || b[90] == 'E');
        if ((b[0] == 1 && !have) || jol) { is->root_lba = le32(b + 156 + 2); is->root_len = le32(b + 156 + 10); is->joliet = jol; have = 1; }
        if (jol) break;
    }
    return have ? 0 : -1;
}
static int iso_name_is(const unsigned char *n, int len, int joliet, const char *want, size_t wl)
{
    char a[256]; size_t o = 0;
    if (joliet) { for (int i = 0; i + 1 < len && o < sizeof a - 1; i += 2) a[o++] = n[i] ? '?' : (char)n[i + 1]; }
    else for (int i = 0; i < len && o < sizeof a - 1; i++) a[o++] = (char)n[i];
    a[o] = 0;
    char *semi = strchr(a, ';'); if (semi) *semi = 0;             /* the version ";1" */
    o = strlen(a); if (o && a[o - 1] == '.') a[--o] = 0;          /* "MUSIC." of a name without an extension */
    return o == wl && !strncasecmp(a, want, wl);
}
static int iso_find(const Iso *is, const char *path, uint32_t *lba, uint32_t *len)   /* 0 = found (a file) */
{
    uint32_t cl = is->root_lba, cn = is->root_len; const char *p = path;
    while (*p) {
        const char *e = strchr(p, '/'); size_t wl = e ? (size_t)(e - p) : strlen(p); int found = 0;
        unsigned char *d = malloc(cn ? cn : 1); if (!d || iso_pread(is->fd, d, cn, (unsigned long long)cl * 2048)) { free(d); return -1; }
        for (uint32_t o = 0; o < cn; ) {
            unsigned rl = d[o];
            if (!rl) { o = (o / 2048 + 1) * 2048; continue; }        /* records do not cross sectors */
            if (o + 33 > cn || o + rl > cn) break;
            int nl = d[o + 32];
            if (!(nl == 1 && d[o + 33] <= 1) && iso_name_is(d + o + 33, nl, is->joliet, p, wl)) { cl = le32(d + o + 2); cn = le32(d + o + 10); found = 1; if (!e && (d[o + 25] & 2)) found = 0; break; }
            o += rl;
        }
        free(d);
        if (!found) return -1;
        p = e ? e + 1 : p + wl;
    }
    *lba = cl; *len = cn; return 0;
}
static int iso_copy(int fd, const char *home)               /* the number of files that differ from the 1.00 CD, -1 = failed */
{
    Iso is; char m[PMAX + 400], dst[PMAX];
#ifndef WEB_BIGFILE_MIN                                      /* only Music.bf (306 MB) crosses the default bar; tests can lower it */
#define WEB_BIGFILE_MIN (128u << 20)
#endif
    if (iso_open(&is, fd)) { plat_message("That file is not an ISO image of a CD.", 1); return -1; }
    uint32_t l0, n0;
    if (iso_find(&is, "Data/W1A/W1A.gel", &l0, &n0) || iso_find(&is, "Music.bf", &l0, &n0)) { plat_message("That ISO image does not hold the Woody Woodpecker game files (Data, Common, Music.bf).", 1); return -1; }
    snprintf(dst, sizeof dst, "%s/data", home); mkdir(dst, 0755);
#ifndef __EMSCRIPTEN__                                      /* a browser has no useful free-space number; the copy just fails if it has to */
    struct statvfs vf;
    if (!statvfs(home, &vf) && (unsigned long long)vf.f_bavail * vf.f_frsize < (unsigned long long)DATAFILES_BYTES + (16u << 20)) {
        snprintf(m, sizeof m, "Not enough free space for the game files (%u MB) in\n%s", DATAFILES_BYTES >> 20, dst); plat_message(m, 1); return -1;
    }
#endif
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return -1;
    unsigned long long done = 0, synced = 0; int bad = 0, first_bad = -1, last = -1; memset(g_fit, 0, sizeof g_fit);
    printf("data: copying the game files from the ISO image to %s\n", dst);
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        char dp[PMAX], tp[PMAX + 8]; uint32_t lba, len; int ok = 1;
        int found = iso_find(&is, k_datafiles[i].path, &lba, &len) == 0;
        if (!found && datafile_absent(i, g_fit)) continue;        /* the manifest lists it, a release lacks it */
        if (!found) { snprintf(m, sizeof m, "The ISO image has no %s.", k_datafiles[i].path); data_progress(NULL); plat_message(m, 1); free(buf); return -1; }
#ifdef __EMSCRIPTEN__
        if (len >= WEB_BIGFILE_MIN) {                               /* a big file (Music.bf): streamed 4 MB at a time from the
                                                                     * ISO straight into /woody-big, outside the IDBFS mount, the
                                                                     * SHA-1 computed on the way — never a 300 MB buffer in RAM
                                                                     * and never a 300 MB IndexedDB write */
            const char *b = strrchr(k_datafiles[i].path, '/');
            snprintf(dp, sizeof dp, "/woody-big/%s", b ? b + 1 : k_datafiles[i].path);
            EM_ASM({ FS.mkdirTree('/woody-big'); });
            Sha1 hb; sha1_init(&hb); ok = 1;
            if (web_bigfile_begin(b ? b + 1 : k_datafiles[i].path)) { free(buf); return -1; }
            for (uint32_t o = 0; ok > 0 && o < len; ) {
                size_t n = len - o < bufsz ? len - o : bufsz;
                if (iso_pread(fd, buf, n, (unsigned long long)lba * 2048 + o)) { ok = -1; break; }
                sha1_add(&hb, buf, n);
                if (web_bigfile_write(o, buf, n)) { ok = -1; break; }   // straight from the heap, 4 MB at a time
                o += (uint32_t)n; done += n; progress_pct("Copying the game files...", done, &last);
            }
            web_bigfile_end();
            if (ok < 0) { data_progress(NULL); plat_message("Could not read the big file from the ISO image.", 1); free(buf); return -1; }
            char hex[41]; sha1_hex(&hb, hex);
            if (!datafile_tally(i, len, hex, g_fit)) { bad++; if (first_bad < 0) first_bad = i; printf("data: %s matches none of the supported CDs\n", k_datafiles[i].path); }
            continue;
        }
#endif
        snprintf(dp, sizeof dp, "%s/%s", dst, k_datafiles[i].path); make_dirs(dp); snprintf(tp, sizeof tp, "%s.part", dp);
        FILE *d = fopen(tp, "wb"); if (!d) ok = -1;
        Sha1 h; sha1_init(&h);
        for (uint32_t o = 0; ok > 0 && o < len; ) {
            size_t n = len - o < bufsz ? len - o : bufsz;
            if (iso_pread(fd, buf, n, (unsigned long long)lba * 2048 + o) || fwrite(buf, 1, n, d) != n) { ok = -1; break; }
            sha1_add(&h, buf, n); o += (uint32_t)n; done += n; progress_pct("Copying the game files...", done, &last);
        }
        if (d && fclose(d)) ok = -1;
        if (ok < 0 || rename(tp, dp)) {
            remove(tp); data_progress(NULL);
            snprintf(m, sizeof m, "Could not copy %s from the ISO image to\n%s/\n\nIs there room on the device?", k_datafiles[i].path, dst);
            plat_message(m, 1); free(buf); return -1;
        }
        char hex[41]; sha1_hex(&h, hex);
        if (!datafile_tally(i, len, hex, g_fit)) { bad++; if (first_bad < 0) first_bad = i; printf("data: %s matches none of the supported CDs\n", k_datafiles[i].path); }
#ifdef __EMSCRIPTEN__                                       /* persist a little at a time: one IndexedDB write of the
                                                             * whole 650 MB copy can outgrow a small machine's memory */
        if (done - synced >= (64u << 20)) { web_syncfs_quiet(); synced = done; }
#endif
    }
    free(buf); data_progress(NULL);
    printf("data: the %s CD\n", k_releases[datafile_best(g_fit)]);
    if (bad) {
        snprintf(m, sizeof m, "%d of the copied files match none of the supported CDs (the first: %s).\n\n"
                              "WoodyRE supports the English 1.00, Brazilian, Polish, Spanish and Russian CDs; another release or a damaged copy may not work correctly.", bad, k_datafiles[first_bad].path);
        plat_message(m, 1);
    }
    return bad;
}
static void check_copy(const char *home)                     /* after the Java side copied a folder: compare it with the manifest */
{
    char root[PMAX], m[PMAX + 300]; snprintf(root, sizeof root, "%s/data", home);
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return;
    unsigned long long done = 0; int bad = 0, first_bad = -1, last = -1; memset(g_fit, 0, sizeof g_fit);
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        if (file_pass(root, NULL, i, buf, bufsz, &done) <= 0) { bad++; if (first_bad < 0) first_bad = i; printf("data: %s missing or differs\n", k_datafiles[i].path); }
        progress_pct("Checking the game files...", done, &last);
    }
    free(buf); data_progress(NULL);
    printf("data: the %s CD\n", k_releases[datafile_best(g_fit)]);
    if (bad) {
        snprintf(m, sizeof m, "%d game files are missing or match none of the supported CDs (the first: %s).\n\n"
                              "WoodyRE supports the English 1.00, Brazilian, Polish, Spanish and Russian CDs; another release or a damaged copy may not work correctly.", bad, k_datafiles[first_bad].path);
        plat_message(m, 1);
    }
}

#ifdef __ANDROID__
const char *data_find(void)
{
    const char *env = getenv("WOODY_DATA"); if (env && *env) return env;
    char home[PMAX], p[PMAX + 16], m[3 * PMAX];
    if (!home_dir(home)) { plat_message("No storage for the game files.", 1); return NULL; }
    snprintf(p, sizeof p, "%s/data", home); if (cd_layout(p)) return enter(home, "data/Data");
    for (;;) {
        snprintf(m, sizeof m, "WoodyRE needs the files of the original game CD-ROM:\n"
                              "Woody Woodpecker: Escape from Buzz Buzzard Park (PC; the English, Brazilian, Polish, Spanish or Russian CD).\n\n"
                              "Choose an ISO image of the CD, or a folder with a copy of it (Data, Common, Logo, Game and Music.bf). "
                              "The game files (%u MB) are copied once.\n\n"
                              "Or copy those files with a USB cable into\n%s/data\nand start WoodyRE again.", DATAFILES_BYTES >> 20, home);
        int r = ask(m, "ISO image", "Folder");
        if (r < 0) return NULL;
        mkdir(p, 0755);
        if (r == 1) {
            int fd = java_pick(1, NULL);
            if (fd == -2) plat_message("Could not open that file.", 1);
            if (fd < 0) continue;
            int bad = iso_copy(fd, home); close(fd);
            if (bad >= 0 && cd_layout(p)) return enter(home, "data/Data");
        } else {
            int k = java_pick(2, p);
            if (k == -2) plat_message("Could not copy that folder. Is there room on the device?", 1);
            if (k < 0) continue;
            if (!cd_layout(p)) { plat_message("That folder does not hold the game files: it needs Data, Common, Logo, Game and Music.bf of the CD.", 1); continue; }
            check_copy(home);
            return enter(home, "data/Data");
        }
    }
}
#endif   /* __ANDROID__ (data_find) */
#endif   /* !desktop: the shared ISO parser + copy/check above serve Android and the web build */

#if defined __EMSCRIPTEN__
/* ---- web: the shell (web/shell.html) asks the user for the CD files and, for a folder copy, writes them into the
 * virtual file system itself. This side waits for the pick, extracts an ISO with the parser above (reading the picked
 * File object chunk by chunk, the image never sits in memory whole), checks the manifest and keeps the result in the
 * browser's IndexedDB (IDBFS mounted at /woody by the shell before main): a later visit starts without the picker. */
const char *data_find(void)
{
    const char *env = getenv("WOODY_DATA"); if (env && *env) return env;
    web_bigfile_restore("Music.bf");                                             /* a previous visit's big file, from its own chunk store */
    if (cd_layout("/woody/data")) return chdir("/woody") ? NULL : "data/Data";   /* a previous visit's copy */
    mkdir("/woody", 0755); mkdir("/woody/data", 0755);
    for (;;) {
        EM_ASM({ if (window.WoodyShell) WoodyShell.pickData(); });               /* show the picker (an ISO file or a folder) */
        while (!EM_ASM_INT({ return window.WoodyShell ? WoodyShell.dataReady() : 0; })) emscripten_sleep(120);
        if (EM_ASM_INT({ return WoodyShell.dataMode(); }) == 1) {                /* mode 1 = an ISO image, 2 = the shell already wrote a folder copy */
            if (iso_copy(0, "/woody") < 0) { EM_ASM({ if (window.WoodyShell) WoodyShell.pickAgain(); }); continue; }
        }
        if (!cd_layout("/woody/data")) {
            plat_message("That selection does not hold the game files: it needs Data, Common, Logo, Game and Music.bf of the CD.", 1);
            EM_ASM({ if (window.WoodyShell) WoodyShell.pickAgain(); }); continue;
        }
        printf("data: check of the copy done\n");
        data_progress("");
        web_bigfile_relocate();                                                  /* folder copies leave Music.bf in the mount: move it out */
        printf("data: the big file is out of the IDBFS mount\n");
        web_bigfile_store("Music.bf");                                           /* keep the big file in its own chunked store */
        printf("data: the big file is in its chunk store\n");
        EM_ASM({ window.__woodyFinalSave = 1; });                                 /* the next syncfs reports progress */
        web_syncfs();                                                            /* keep everything for the next visits (best effort) */
        printf("data: syncfs done\n");
        printf("data: the game files stay in this browser's own storage; nothing is sent anywhere\n");
        return chdir("/woody") ? NULL : "data/Data";
    }
}

void data_sync(void) { web_syncfs(); }
#endif

int data_verify(const char *data_dir)
{
    char root[PMAX]; snprintf(root, sizeof root, "%s/..", data_dir);
    size_t bufsz = 4u << 20; unsigned char *buf = malloc(bufsz); if (!buf) return -1;
    int bad = 0; memset(g_fit, 0, sizeof g_fit);
    for (int i = 0; i < DATAFILES_COUNT; i++) {
        int k = file_pass(root, NULL, i, buf, bufsz, NULL);
        if (k <= 0) { bad++; printf("verify: %s %s\n", k_datafiles[i].path, k < 0 ? "MISSING" : "matches none of the supported CDs"); }
    }
    free(buf);
    int best = datafile_best(g_fit);
    printf("verify: %d of %d files belong to a supported CD; the copy is the %s CD (%d of its files)%s\n", DATAFILES_COUNT - bad, DATAFILES_COUNT,
           k_releases[best], g_fit[best], bad ? "" : " - all good");
    return bad;
}
#endif
