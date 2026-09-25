# Woody Woodpecker: Escape from Buzz Buzzard Park (PC, 2001) – analysis of WWP.iso

## Identification
- PC version (Win32), not PS2. Developer Eko Software, publisher Cryo. Build 17 Oct 2001, MSVC 6.0, no debug symbols, no RTTI.
- Installer: InstallShield 5.2 (data1.cab). Only the exe + DLLs are installed (5.6 MB); all data stays on the CD (CD check via WinResource.dll: "Please insert the Woody Woodpecker CD").
- Rendering: DirectDraw 7 + Direct3D 7 (IID_IDirectDraw7 / IID_IDirect3D7 present), 16-bit color, resolutions 512/640/800/1024.
- Audio: Cryo Sound Library 1.08 (DirectSound), Cryo BigFile 2.05, Cryo APC ADPCM.
- Video: HNM6 (Cryo). CM6_<res>x16.dll = HNM6 decoder plugins (exports HNMPI_Init/DecodeFrame/Cleanup), only for the 3 logo videos.
- Input: DirectInput 7. Config via Detect.exe (MFC) -> Woody.cfg + registry HKLM\Software\cryo\Woody Woodpecker.

## Woody.exe (884 KB)
- .text 688 KB, ~226k instructions, ~2170 functions (direct call targets), 40k FPU instructions (own math/physics/renderer code in the exe).
- Very many French debug/assert strings (camera, NPC patrols, water, bonuses, save games, VM statistics).
- Script VM ("emulateur"): handlers around 0x442cb0–0x444000, function-pointer table with 61 entries at 0x5d0418 (init 0x442a42–0x442c9f); opcode dispatch via call [ecx*4+0x5cc360]. Handler signature: uint32* handler(uint32* pc) -> new pc; evaluation stack at 0x5ce44c (byte stack, sp at 0x5d0520). Message dispatch to engine objects: switch with 46 cases (0x4448b2), switch with 58 cases for message IDs 1600..1700 (0x467fdd).
- VM language has keywords "during", "delay", "cut", messages, objects (event-driven, actor-like). Version check compiler vs. emulator.

## File formats (per level in Data\<LVL>\)
| Ext | Content | Status |
|---|---|---|
| code | "EKO CODE" bytecode, 32-bit words, end marker 0xFADEFADE, header with object count (e.g. 507 for W1A) and segment boundaries | structure partly known, opcodes still to be RE'd |
| .gel | Geometry: header {faces, indices(=3*faces), 3, n}; per face 36 B: plane(nx,ny,nz,d) + 3 indices + 2 dwords; then group table; then vertex count + vertices {x,y,z float, RGB color} | decoded, render made (W1A_topdown.png) |
| .tex | count; per texture header (w,h,flags,6 dwords; 36–40 B) + w*h*2 bytes RGB565 (no mipmaps) | decoded (minor header variants open) |
| .lit | Lights: position (3 floats), color RGB, range | almost trivial |
| .col | Collision: per-face material/flags (u16 pairs) | simple |
| .vis | Visibility (cell/PVS) tables of u32 pairs | simple |
| .ins | Instances (507 objects for W1A), plus paths/polylines/keyframes (time + quaternion/position) | partial |
| .rck | "RKET" resource bank: sequential chunks [size][type]: PCM sounds (22 kHz 16-bit mono), BGRA images (menus, sprites, magenta=transparent), meshes/animations (135 chunks in Woody.rck), font (786 KB bitmap) | sounds+images decoded, meshes/anims not yet |
| Music.bf | CryoBF 2.01: directory at the end, 49 separate uncompressed WAVs (44.1 kHz stereo 16-bit), 306 MB | fully decoded |
| .hnm | HNM6 video (3 logos) | decoders exist (ScummVM/ffmpeg family) |
| Game\mask.bin | 1024x768 8-bit mask | trivial |

## Assessment
1. Running on modern Windows: it's already a native Win32 game. Known issues (PCGamingWiki): no VSync, boss timers scale with framerate, 4:3 stretch; an ASI FOV fix exists. With dgVoodoo2 (DDraw/D3D7 -> D3D11) + mounting the ISO, mostly playable. Effort: hours.
2. Full decompilation (matching, à la SM64/Tomb Raider): ~2000 functions, no symbols, but many strings. Estimate 6–18 months for 1–2 people. Legally: own code is fine, but assets + IP (Universal) may not be redistributed.
3. Reimplementation (ScummVM approach): new engine that loads the original Data\ folder. Formats are uncompressed and simple; the biggest job is rebuilding the script VM (61 handlers + engine messages) 1:1, since all gameplay lives in the code files. Estimate 3–9 months to playable. Assets are 100% reusable without conversion.

## Tools (wwp_tools/)
unshield5.py (IS5 cab), tex_dump2.py, gel_render.py, rck_dump.py, Music.bf extractor (inline), exe_analysis.py, exe_tables.py, vm_tables.py, string_refs.py, vm_peek.py.
