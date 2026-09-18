# Woody Woodpecker: Escape from Buzz Buzzard Park (PC, 2001) – analyse WWP.iso

## Identificatie
- PC-versie (Win32), niet PS2. Ontwikkelaar Eko Software, uitgever Cryo. Build 17 okt 2001, MSVC 6.0, geen debug-symbolen, geen RTTI.
- Installer: InstallShield 5.2 (data1.cab). Alleen exe + DLL's worden geïnstalleerd (5,6 MB); alle data blijft op de CD (CD-check via WinResource.dll: "Please insert the Woody Woodpecker CD").
- Rendering: DirectDraw 7 + Direct3D 7 (IID_IDirectDraw7 / IID_IDirect3D7 aanwezig), 16-bit kleur, resoluties 512/640/800/1024.
- Audio: Cryo Sound Library 1.08 (DirectSound), Cryo BigFile 2.05, Cryo APC ADPCM.
- Video: HNM6 (Cryo). CM6_<res>x16.dll = HNM6 decoder-plugins (exports HNMPI_Init/DecodeFrame/Cleanup), alleen voor de 3 logo-filmpjes.
- Input: DirectInput 7. Config via Detect.exe (MFC) -> Woody.cfg + registry HKLM\Software\cryo\Woody Woodpecker.

## Woody.exe (884 KB)
- .text 688 KB, ~226k instructies, ~2170 functies (direct call targets), 40k FPU-instructies (eigen math/physics/renderer-code in exe).
- Zeer veel Franse debug/assert-strings (camera, NPC-patrouilles, water, bonussen, save-games, VM-statistieken).
- Script-VM ("emulateur"): handlers rond 0x442cb0–0x444000, function-pointer-tabel met 61 entries op 0x5d0418 (init 0x442a42–0x442c9f); opcode-dispatch via call [ecx*4+0x5cc360]. Handler-signatuur: uint32* handler(uint32* pc) -> nieuwe pc; evaluatiestack op 0x5ce44c (byte-stack, sp op 0x5d0520). Message-dispatch naar engine-objecten: switch 46 cases (0x4448b2), switch 58 cases voor message-ID's 1600..1700 (0x467fdd).
- VM-taal heeft keywords "during", "delay", "cut", messages, objecten (event-gedreven, actor-achtig). Versiecheck compiler vs emulator.

## Bestandsformaten (per level in Data\<LVL>\)
| Ext | Inhoud | Status |
|---|---|---|
| code | "EKO CODE" bytecode, 32-bit woorden, eindmarker 0xFADEFADE, header met object-aantal (bijv. 507 voor W1A) en segmentgrenzen | structuur deels bekend, opcodes nog te RE'en |
| .gel | Geometrie: header {faces, indices(=3*faces), 3, n}; per face 36 B: plane(nx,ny,nz,d) + 3 indices + 2 dwords; daarna groepentabel; daarna vertexcount + vertices {x,y,z float, RGB kleur} | gedecodeerd, render gemaakt (W1A_topdown.png) |
| .tex | count; per texture header (w,h,flags,6 dwords; 36–40 B) + w*h*2 bytes RGB565 (geen mipmaps) | gedecodeerd (kleine header-varianten open) |
| .lit | Lichten: positie (3 floats), kleur RGB, bereik | bijna triviaal |
| .col | Collision: per-face materiaal/flags (u16-paren) | eenvoudig |
| .vis | Visibility (cell/PVS) tabellen van u32-paren | eenvoudig |
| .ins | Instances (507 objecten W1A), plus trajecten/polylines/keyframes (tijd + quaternion/positie) | gedeeltelijk |
| .rck | "RKET" resource bank: sequentiële chunks [size][type]: PCM-sounds (22 kHz 16-bit mono), BGRA-images (menu's, sprites, magenta=transparant), meshes/animaties (135 chunks in Woody.rck), font (786 KB bitmap) | sounds+images gedecodeerd, meshes/anims nog niet |
| Music.bf | CryoBF 2.01: directory achteraan, 49 losse ongecomprimeerde WAV's (44,1 kHz stereo 16-bit), 306 MB | volledig gedecodeerd |
| .hnm | HNM6 video (3 logo's) | decoders bestaan (ScummVM/ffmpeg-familie) |
| Game\mask.bin | 1024x768 8-bit masker | triviaal |

## Inschatting
1. Draaien op modern Windows: het is al een native Win32-game. Bekende problemen (PCGamingWiki): geen VSync, bosstimers schalen met framerate, 4:3 stretch; ASI FOV-fix bestaat. Met dgVoodoo2 (DDraw/D3D7 -> D3D11) + ISO mounten meestal speelbaar. Werk: uren.
2. Volledige decompilatie (matching, à la SM64/Tomb Raider): ~2000 functies, geen symbolen, wel veel strings. Schatting 6–18 maanden voor 1–2 personen. Juridisch: eigen code mag, maar assets + IP (Universal) niet herdistribueren.
3. Reimplementatie (ScummVM-aanpak): nieuwe engine die de originele Data\-map inlaadt. Formaten zijn ongecomprimeerd en simpel; grootste klus is de script-VM (61 handlers + engine-messages) 1:1 nabouwen, want alle gameplay zit in de code-bestanden. Schatting 3–9 maanden voor speelbaar. Assets zijn 100% herbruikbaar zonder conversie.

## Tools (wwp_tools/)
unshield5.py (IS5 cab), tex_dump2.py, gel_render.py, rck_dump.py, Music.bf-extractor (inline), exe_analysis.py, exe_tables.py, vm_tables.py, string_refs.py, vm_peek.py.
