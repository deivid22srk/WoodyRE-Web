/* Generate Woody.cfg the same way Detect.exe does: load Setup.dll, run its hardware
 * detection, apply the default controls and let SaveConfig write the file into the cwd.
 * Build (32-bit, Setup.dll is x86):
 *   python -m ziglang cc -target x86-windows-gnu -O2 -o out/mkcfg.exe tools/native/mkcfg.c -lgdi32 -luser32
 * Run from the folder that contains Setup.dll (game/). Detect reads an existing Woody.cfg there first (0x10003920) and
 * carries its display / sound choices over; delete it for Setup's defaults (the keys are reset by DefaultControlSettings).
 * NO CoInitialize before Detect: Setup's sound enumeration 0x10002770 does its own and returns at once when that
 * gives S_FALSE (already initialised), leaving its sound page empty, so SaveConfig then writes the sound section
 * +0x68..+0x98 as all 0 = no sound at all in the game (docs/SETUP.md 4). Detect.exe calls Detect before OLE is up. */
#include <windows.h>
#include <stdio.h>
typedef int (__cdecl *DetectFn)(HWND);
typedef void (__cdecl *VoidFn)(void);
typedef int (__cdecl *SaveFn)(void);
int main(void)
{
    HMODULE h = LoadLibraryA("Setup.dll");
    if (!h) { printf("LoadLibrary Setup.dll failed: %lu\n", GetLastError()); return 1; }
    DetectFn Detect = (DetectFn)GetProcAddress(h, "Detect");
    VoidFn Defaults = (VoidFn)GetProcAddress(h, "DefaultControlSettings");
    SaveFn Save = (SaveFn)GetProcAddress(h, "SaveConfig");
    if (!Detect || !Defaults || !Save) { printf("exports missing\n"); return 1; }
    HWND hwnd = CreateWindowExA(0, "STATIC", "mkcfg", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    printf("hwnd=%p\n", (void*)hwnd);
    int r = Detect(hwnd);
    printf("Detect -> %d\n", r);
    /* dump the tables Detect filled: selected device and its data */
    unsigned char *b = (unsigned char*)h;
    printf("selected device = %d\n", *(int*)(b + 0x246b8));
    Defaults();
    int s = Save();
    printf("SaveConfig -> %d\n", s);
    return 0;
}
