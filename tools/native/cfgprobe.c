/* setupprobe: run Setup.dll's Detect and dump the tables Detect.exe's dialog pages show (docs/SETUP.md).
 * Build (32-bit, Setup.dll is x86):
 *   python -m ziglang cc -target x86-windows-gnu -O2 -o out/setupprobe.exe tools/native/setupprobe.c -lgdi32 -luser32 -lole32
 * Run from a folder with a copy of Setup.dll and WITHOUT a Woody.cfg (Detect reads one when it is there; "save" makes
 * SaveConfig write a new one into that folder). "co" calls CoInitialize first, like the old mkcfg.c: the sound page
 * then stays empty (0x10002770 returns on S_FALSE). Addresses are Setup.dll's (imagebase 0x10000000). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef int (__cdecl *DetectFn)(HWND);
typedef void (__cdecl *VoidFn)(void);
typedef int (__cdecl *SaveFn)(void);
static unsigned char *B;
#define A(x) (B + ((x) - 0x10000000u))
#define I(x) (*(int *)A(x))
int main(int argc, char **argv)
{
    int co = 0, save = 0, i;
    for (i = 1; i < argc; i++) { if (!strcmp(argv[i], "co")) co = 1; if (!strcmp(argv[i], "save")) save = 1; }
    if (co) printf("CoInitialize -> %ld\n", (long)CoInitialize(NULL));
    HMODULE h = LoadLibraryA("Setup.dll");
    if (!h) { printf("LoadLibrary Setup.dll failed %lu\n", GetLastError()); return 1; }
    B = (unsigned char *)h;
    DetectFn Detect = (DetectFn)GetProcAddress(h, "Detect");
    VoidFn Defaults = (VoidFn)GetProcAddress(h, "DefaultControlSettings");
    SaveFn Save = (SaveFn)GetProcAddress(h, "SaveConfig");
    if (!Detect || !Defaults || !Save) { printf("exports missing\n"); return 1; }
    HWND hwnd = CreateWindowExA(0, "STATIC", "setupprobe", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    int r = Detect(hwnd);                                                   /* returns the base Detect.exe indexes: 0x10021ea8 */
    printf("Detect -> %#x\n", r);
    int n = I(0x10035438);
    printf("3D devices %d, selected %d (Display Driver combo)\n", n, I(0x100246b8));
    for (i = 0; i < n; i++) {                                               /* records 0x1000f628 + 0xeb8 i (0x10002f80) */
        unsigned char *rec = A(0x1000f628) + i * 0xeb8;
        unsigned *g = (unsigned *)(rec + 0xe90);
        printf(" dev %d flags=%#x '%s' ddraw-guid-ptr=%#x ddguid=%08x-%08x-%08x-%08x vsync=%d [+0x54]=%d [+0x4c]=%d modes=%d defmode=%d [+0x30]=%d [+0x34]=%d [+0x38]=%d quality=%d\n",
               i, *(unsigned *)rec, (char *)(rec + 4), *(unsigned *)(rec + 0x220), g[0], g[1], g[2], g[3],
               I(0x10033dec + 4 * i), I(0x10033e3c + 4 * i), I(0x10033e8c + 4 * i), I(0x100246bc + 4 * i), I(0x1002475c + 4 * i),
               I(0x100337bc + 20 * i), I(0x10033948 + 16 * i), I(0x10033a90 + 24 * i), I(0x10033c6c + 20 * i));
    }
    printf("sound page: fx=%d music=%d cinematic=%d invert=%d [+0x78]=%d [+0x7c]=%d fxvol=%d musvol=%d [+0x88]=%d speakers=%d devices=%d sel=%d\n",
           I(0x10033edc), I(0x10033ee0), I(0x10033ee4), I(0x10033ee8), I(0x10033eec), I(0x10033ef0), I(0x10033ef4),
           I(0x10033ef8), I(0x10033efc), I(0x10033f00), I(0x10033f04), I(0x10033f08));
    for (i = 0; i < I(0x10033f04) && i < 20; i++)
        printf("  sound %d '%s' max3dstatic=%d maxmixstatic=%d\n", i, (char *)A(0x10033f0c + i * 0x100), I(0x1003530c + 4 * i), I(0x1003535c + 4 * i));
    printf("cfg struct 0x10021d88 (= Woody.cfg - 4):");
    for (i = 0; i < 0x11c; i += 4) printf("%s%03x:%x", (i % 32) ? " " : "\n  ", i, I(0x10021d88 + i));
    printf("\n");
    if (save) { Defaults(); printf("SaveConfig -> %d\n", Save()); }
    return 0;
}
