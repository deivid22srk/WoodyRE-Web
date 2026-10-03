/* texpack.c - texture packs (PORT EXTRA, docs/TEXTURES.md).
 * A texture is known by a 64-bit FNV-1a hash of what the game reads from its files: the 16-bit texels of a .tex frame
 * (world and models) or the RGBA of a bank image (HUD, sprites, sky, font pages, BlackBox). The same texture in two levels
 * has the same hash, so one PNG replaces it everywhere. A replacement may be any size: the game keeps every size and
 * texture coordinate of the original and only samples the bigger image. Files: mods\textures\ (any subfolders) in the
 * game's folder next to woodyre.cfg, named <anything>_<16 hex digits>.png - the dump names them <w>x<h>_<hash>.png. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "texpack.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"

typedef struct { uint64_t hash; wchar_t *path; } Entry;
static struct {
    int scanned, dump, ndumped, nreplaced; char scope[64];
    wchar_t root[MAX_PATH];          /* <exe dir>\mods */
    Entry *tab; uint32_t cap, n;     /* open addressing, hash 0 = empty */
} T = { .scope = "Common" };

uint64_t tp_hash(char kind, const void *data, uint32_t bytes, int w, int h)
{
    uint64_t x = 0xcbf29ce484222325ull; const uint8_t *p = (const uint8_t *)data;
    uint8_t head[9] = { (uint8_t)kind, (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24), (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24) };
    for (int i = 0; i < 9; i++) { x ^= head[i]; x *= 0x100000001b3ull; }
    for (uint32_t i = 0; i < bytes; i++) { x ^= p[i]; x *= 0x100000001b3ull; }
    return x ? x : 1;
}
void tp_scope(const char *name) { snprintf(T.scope, sizeof T.scope, "%s", name && *name ? name : "Common"); }
const char *tp_scope_get(void) { return T.scope; }
void tp_set_dump(int on) { T.dump = on; }

static void put(uint64_t hash, const wchar_t *path)
{
    if ((T.n + 1) * 2 > T.cap) {                           /* grow at half full */
        Entry *old = T.tab; uint32_t oc = T.cap; T.cap = oc ? oc * 2 : 256; T.tab = (Entry *)calloc(T.cap, sizeof *T.tab); T.n = 0;
        for (uint32_t i = 0; i < oc; i++) if (old[i].hash) { uint32_t k = (uint32_t)old[i].hash & (T.cap - 1); while (T.tab[k].hash) k = (k + 1) & (T.cap - 1); T.tab[k] = old[i]; T.n++; }
        free(old);
    }
    uint32_t k = (uint32_t)hash & (T.cap - 1);
    while (T.tab[k].hash) { if (T.tab[k].hash == hash) return; k = (k + 1) & (T.cap - 1); }   /* two files for one texture: the first one found wins */
    T.tab[k].hash = hash; T.tab[k].path = _wcsdup(path); T.n++;
}
static const wchar_t *find(uint64_t hash)
{
    if (!T.cap) return NULL;
    for (uint32_t k = (uint32_t)hash & (T.cap - 1); T.tab[k].hash; k = (k + 1) & (T.cap - 1)) if (T.tab[k].hash == hash) return T.tab[k].path;
    return NULL;
}
static int name_hash(const wchar_t *name, uint64_t *out)   /* <anything>_<16 hex>.png or <16 hex>.png */
{
    size_t n = wcslen(name); if (n < 20 || _wcsicmp(name + n - 4, L".png")) return 0;
    const wchar_t *h = name + n - 20; if (h > name && h[-1] != L'_') return 0;
    uint64_t v = 0;
    for (int i = 0; i < 16; i++) {
        wchar_t c = h[i]; int d = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
        if (d < 0) return 0;
        v = v << 4 | (uint64_t)d;
    }
    *out = v ? v : 1; return 1;
}
static void scan_dir(const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH]; WIN32_FIND_DATAW fd;
    if (_snwprintf(pat, MAX_PATH, L"%ls\\*", dir) >= MAX_PATH) return;
    HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        wchar_t p[MAX_PATH]; if (_snwprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) >= MAX_PATH) continue;
        uint64_t hv;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 8) scan_dir(p, depth + 1); }
        else if (name_hash(fd.cFileName, &hv)) put(hv, p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void scan(void)
{
    T.scanned = 1;
    DWORD n = GetCurrentDirectoryW(MAX_PATH, T.root);     /* the game's home with woodyre.cfg and data\ (datasetup.c makes it current) */
    if (!n || n >= MAX_PATH - 8) wcscpy(T.root, L".");
    wcsncat(T.root, L"\\mods", MAX_PATH - wcslen(T.root) - 1);
    wchar_t dir[MAX_PATH]; _snwprintf(dir, MAX_PATH, L"%ls\\textures", T.root); dir[MAX_PATH - 1] = 0;
    scan_dir(dir, 0);
    if (T.n) printf("texture pack: %u replacement textures in %ls\n", T.n, dir);
}

static uint8_t *read_file(const wchar_t *path, int *size)
{
    FILE *f = _wfopen(path, L"rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f); *size = (int)n; return d;
}
int tp_replace(uint64_t hash, int alpha_mode)
{
    if (!T.scanned) scan();
    const wchar_t *path = find(hash); if (!path) return 0;
    int size, w, h, ch; uint8_t *file = read_file(path, &size); if (!file) return 0;
    uint8_t *px = stbi_load_from_memory(file, size, &w, &h, &ch, 4); free(file);
    GLint max = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (!px || w > max || h > max) { printf("texture pack: cannot use %ls (%s)\n", path, px ? "too big for this card" : stbi_failure_reason()); if (px) stbi_image_free(px); return 0; }
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        uint8_t *p = px + i * 4;
        if (alpha_mode == TP_OPAQUE) p[3] = 255;
        else if (alpha_mode == TP_KEY) { if (p[3] < 128) p[0] = p[1] = p[2] = p[3] = 0; else p[3] = 255; }   /* like the original's colour key: transparent texels black (no fringe) */
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int level = 0;
    while (w > 1 || h > 1) {                               /* the full chain, 2x2 box filter; a colour key level keeps its alpha on/off */
        int dw = w > 1 ? w / 2 : 1, dh = h > 1 ? h / 2 : 1;
        for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
            int x0 = 2 * x < w ? 2 * x : w - 1, x1 = 2 * x + 1 < w ? 2 * x + 1 : x0, y0 = 2 * y < h ? 2 * y : h - 1, y1 = 2 * y + 1 < h ? 2 * y + 1 : y0;
            const uint8_t *s[4] = { px + ((size_t)y0 * w + x0) * 4, px + ((size_t)y0 * w + x1) * 4, px + ((size_t)y1 * w + x0) * 4, px + ((size_t)y1 * w + x1) * 4 };
            uint8_t *d = px + ((size_t)y * dw + x) * 4;           /* in place: d never passes the sources still to be read */
            unsigned c[4] = { 0, 0, 0, 0 }, on = 0;
            for (int k = 0; k < 4; k++) { for (int q = 0; q < 4; q++) c[q] += s[k][q]; on += s[k][3] >= 128; }
            if (alpha_mode == TP_KEY && on) { unsigned r = 0, g = 0, b = 0; for (int k = 0; k < 4; k++) if (s[k][3] >= 128) { r += s[k][0]; g += s[k][1]; b += s[k][2]; } c[0] = r * 4 / on; c[1] = g * 4 / on; c[2] = b * 4 / on; }
            for (int q = 0; q < 4; q++) d[q] = (uint8_t)((c[q] + 2) / 4);
            if (alpha_mode == TP_KEY) { if (on >= 2) d[3] = 255; else d[0] = d[1] = d[2] = d[3] = 0; }
        }
        w = dw; h = dh; level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, level);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(px); T.nreplaced++;
    return 1;
}

static void write_cb(void *ctx, void *data, int size) { fwrite(data, 1, (size_t)size, (FILE *)ctx); }
void tp_dump(uint64_t hash, const uint8_t *rgba, int w, int h)
{
    if (!T.dump) return;
    if (!T.scanned) scan();
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%ls\\dump\\%hs", T.root, T.scope); dir[MAX_PATH - 1] = 0;
    if (_snwprintf(path, MAX_PATH, L"%ls\\%dx%d_%016llx.png", dir, w, h, (unsigned long long)hash) >= MAX_PATH) return;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    CreateDirectoryW(T.root, NULL); wchar_t d2[MAX_PATH]; _snwprintf(d2, MAX_PATH, L"%ls\\dump", T.root); d2[MAX_PATH - 1] = 0; CreateDirectoryW(d2, NULL); CreateDirectoryW(dir, NULL);
    FILE *f = _wfopen(path, L"wb"); if (!f) return;
    stbi_write_png_to_func(write_cb, f, w, h, 4, rgba, w * 4);
    fclose(f);
    if (++T.ndumped % 100 == 1) printf("texture dump: %d files so far (%ls)\n", T.ndumped, dir);
}
