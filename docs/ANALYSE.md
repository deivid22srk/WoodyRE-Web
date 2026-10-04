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
| .gel | Geometry: header {faces, indices(=3*faces), 3, n}; per face 36 B: plane(nx,ny,nz,d) + 3 indices + 2 dwords; then group table; then vertex count + vertices {x,y,z float, RGB color} | decoded, render made |
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

## Coverage triage, round 34 (the functions nobody had named)

`tools/coverage.py` now also treats switch tables as part of their function: a case label reached only through a
`jmp [reg*4 + T]` table, and the table itself when the disassembler took it for code (e.g. `0x41304c`, `0x4162f4` in the
type 11 / type 10 enemy code), are merged into the function with the `jmp`, like any other jump target. That alone took
383 such "functions" off the list (92.5 % → 95.0 %). The listing also hid every function that an earlier line named as a caller or callee (`doc()` indexed the defaultdict and so marked it named; the totals were right, the list was not), e.g. `0x460a30` and `0x47e7b0`; fixed. Every function still unnamed after that was classified
(A = dead in the shipped game, B = dev/debug only, C = trivial or infrastructure the port replaces, D = reachable behaviour
the port may lack). The D items and the larger A/B items are written up in their own docs:

| class | what | where |
|---|---|---|
| D | item tables of menu pages 6/8/9/0x17 (an empty line under the question) and the enter `0x45b370` of page 0x17 (cursor on "No") | MENU_LOAD.md §5.1 (ported) |
| B | input recording / replay `0x406aa0` with `0x406930`, `0x4069c0`, `0x406960`, `0x406a70`, `0x406a90` (`cfg+0x378`, never set) | INPUT.md §4.2 |
| C | the 61 VM opcode handlers `0x442cb0..0x443c50`, the node free list `0x444780`/`0x4447b0` | VM.md §3 |
| A | page 0x21 (language selection) `0x45b3a0`/`0x45b3c0`/`0x45b620`.., PS2 memory-card page enters `0x45b310`/`0x45b350` | MENU_LOAD.md §5.1 |

**A — dead in the shipped game**
- `0x42a2d0`: runs a list of timed callbacks (`0x509468`, 32-byte records `{fn, arg, ?, end, start, paused, remove-mode}`,
  count `[0x509aa8]`, removal `0x42a380`) every game frame (`0x40164a`, App state 1). No code anywhere adds a record, so the
  list is always empty.
- `0x4367f0`: at every level load (`0x40456e`) fills `[0x54ba28]` with `[0x4b3200]` records of 0x30 B of random floats
  (`0x43ff40`); nothing reads it, the unload frees it (`0x404aad`).
- Types 10 / 11 (no level uses them, ENEMY2.md §1): AnimCtrl ctors `0x413740` (vtable `0x4a9b9c`) and `0x4169d0`, dtor
  `0x412170` / `0x412150` (type 11, vtable `0x4a9ab0`) and `0x415220` / `0x415200` (type 10, `0x4a9cb8`).
- Page 0x21 cases `0x45b6a1`, `0x45b801`, `0x45b807` (MENU_LOAD.md §5.1).

**B — dev / debug only** (all behind the dev flag `cfg+0x384 & 8` of the debug keys in `0x402940`, never set, or the
recorder above)
- `0x442140` (debug key code 0x2b): cycles `[0x5d0518]` 0 → 1 → 2 (the VM statistics display, `0x443f00`).
- `0x468ec0` (key code 0x2c): `[0x5e81a8]++`; with bit 0 set the sound manager logs through `0x469110` / `0x469140`
  (vsprintf into `0x5e61a8` / `0x5e71a8`, then the debug print `0x462c60`).
- `0x4486d0`: sets the four dev-dialog flags `0x5d7b88`/`8a`/`8b`/`8c` at once (debug key path `0x402b69`).

**C — trivial or replaced infrastructure** (the port has its own equivalent or needs none)
- App and boot: `0x401230` (free slot in the 32 × 0x34 message queue of `0x401250`), `0x402680` (clear App flags),
  `0x4026c0` (the heaps: `0x484040(0x8000, 0x1000)`, `0x406fc0(0x800)`, `0x407690(0x400)`), `0x4028a0` (create the sound
  manager `0x468ed0` → `[0x4c2dd8]`), `0x4028d0` (extension ".rck", `0x441450`, `0x402920`/`0x402930`), `0x4066f0` (the
  hidden window's WndProc: message 0x8000 → HNM `0x4263d0`, else DefWindowProc), CD check helpers of `0x406770`:
  `0x406800` (GetDriveType == CDROM), `0x406820` (find a file on a drive), `0x4068f0`; `0x406910`, `0x406fe0`, `0x4076c0`,
  `0x44fa30`, `0x44ff60`, `0x44ff90`, `0x467310`, `0x4676d0`, `0x468f60` (App teardown frees).
- Level load / unload resets: `0x430060` (`0x53a104[8] = 0`), `0x4346e0` (collision globals), `0x474a70`
  (`0x5e8428[0x40] = 0`), `0x4538e0` (`[0x5e59f8] = 0`), `0x4368b0` (size classes of the small-block pool `0x58ba30`),
  `0x437bf0` (viewport centre from w/h).
- Destructors (the deleting `vt[0]` wrappers and their bodies): `0x403ff0`, `0x404030`, `0x404050`, `0x404090`, `0x4040b0`,
  `0x40b3f0`, `0x40bea0`, `0x40bef0`, `0x40c410`, `0x410f00`/`0x410f20` (type 12), `0x4137f0`/`0x413810` (type 13),
  `0x416d10`/`0x416d30` (types 7..9), `0x418a60`/`0x418a80` (types 4..6), `0x414ee0`, `0x41bc50`, `0x41c080`, `0x41c6d0`
  (behaviour vtables), `0x41df00` (CamMgr), `0x426090`, `0x426b70`, `0x426b90`, `0x426ba0`, `0x4271c0`, `0x4078f0` (the
  `.gel` world, vtable `0x4a94f0`), `0x428ad0`, `0x428af0`, `0x42a620` (world arrays), `0x436a10`, `0x437c90`, `0x43b060`,
  `0x43b140`, `0x4462f0`, `0x4463b0`, `0x4463d0`, `0x4463e0` (the 34 menu pages), `0x4471e0` (HUD), `0x44f880`, `0x450a70`,
  `0x450d00`/`0x450d20` (vtable `0x4aaf5c`), `0x456d50`/`0x456d70` (save object), `0x458f40` (CamMgr owner), `0x45b8a0`,
  `0x45b920` (panel page), `0x467660`, `0x467980`/`0x4679a0` (joystick), `0x467b50`, `0x467d50`, `0x467e40`/`0x467e60`
  (keyboard), `0x468800`/`0x468820`, `0x468f90`/`0x468fb0`, `0x4692e0`, `0x4697d0`/`0x4697f0` (sound manager),
  `0x469ac0`/`0x469ae0`, `0x469300`, `0x47ac80`, `0x47b1f0`, `0x47c5e0`, `0x460a30` (the HUD's members), `0x407750`, `0x407ac0`, `0x40b460` (vtable `0x4a9508`), `0x428b10` (vtable `0x4aa220`), `0x43b2b0`, `0x441a10`, `0x43f590`, `0x43fbd0`, `0x43fc20`, `0x43fca0` (the 16 sprite banks `0x5ac920`).
- Static initialisers / atexit pairs (CRT table `0x4b1030..`): `0x43faa0`, `0x43fab0`, `0x43fae0`, `0x448b40`, `0x448b70`,
  `0x448b80`, `0x448bb0`, `0x45d210`, `0x45e470`, `0x4699b0`, `0x46d500`, `0x46e480`, `0x46f160`, `0x46fa20`; `0x448be0`
  is the default ctor of the 200 projectile slots `0x5d7d48` (every field is rewritten by the spawn, PROJECTILES.md).
- Getters, setters, vtable stubs: `0x406a90` (recorded dt), `0x407740`, `0x407770` (+0x18..+0x24 = −1), `0x4078c0`,
  `0x4078d0`, `0x4078e0`, `0x40c3c0`, `0x411d10` (type 12: AnimLen of `+0x1f8`, for `0x411d50`), `0x41acf0`, `0x41ad00`,
  `0x41ad10`, `0x41afd0`, `0x41afe0`, `0x41b6f0`, `0x41b770`, `0x41b920`, `0x41b930`, `0x41c780`, `0x422770`, `0x422780`,
  `0x4260a0`, `0x42a1e0`, `0x42a200`, `0x42ff10` (Instance position `+0x60`), `0x430020`, `0x430030`, `0x430040`,
  `0x430050`, `0x430080`, `0x430090` (`+0xc`), `0x438ca0` (dot product), `0x445bd0`, `0x445cb0`, `0x44cd50`, `0x44cf40`,
  `0x44e7a0`, `0x4040a0`, `0x406920` (recorder clear), `0x40c430`, `0x426360` / `0x426380` (HNM busy flag `0x5093d0`), `0x426bc0`, `0x4300b0`, `0x436a30`, `0x441be0`, `0x445cf0`, `0x450a90`, `0x460060`, `0x468f70`, `0x47efc0`, `0x450350` / `0x450410` (save record float `+0x54` / int `+0x50`), `0x450ee0` (copy a 0x30-B record),
  `0x456eb0`, `0x45a100`, `0x460030`, `0x4632d0`, `0x467a20`, `0x467b10`, `0x468860`, `0x468870`, `0x468880`, `0x468890`, `0x4688b0`, `0x4688c0`, `0x4688d0`, `0x4688e0`, `0x4688f0`, `0x468900`, `0x469030`, `0x469040`, `0x469050`, `0x469060`, `0x469070`, `0x4690c0`, `0x4690d0`, `0x469100` (sound object vtable stubs: empty `ret n`, flag pairs, getters),
  `0x469170`, `0x469560`, `0x469590`, `0x474660`, `0x474670`, `0x47c190`, `0x47c7a0`.
- Small maths / containers: `0x4076e0` (copy a bbox + 16-B records, hull draw `0x42e2b0`), `0x4317b0` (point × node
  matrix), `0x43b1a0` / `0x43b2f0` / `0x43b370` / `0x43b340` / `0x43b3a0` (an LRU cache: init, touch, evict, the first entry not used this frame; used by `0x42f3d0` / `0x42f490`), `0x43ff00` / `0x43ff60` (rand, rand in
  [−1, 1]), `0x47d230`, `0x47d280`, `0x47d2c0`, `0x47d300`, `0x47d340` (ring-buffer / array element getters of the
  ribbon and trail particles), `0x444120`, `0x444140`, `0x4441c0`, `0x444370` (VM list heads).
- RCK banks, fonts, text: `0x43f430`, `0x43f4b0`, `0x43f530`, `0x43f580`, `0x43f600`, `0x43f660`, `0x43fb00`, `0x43fb10`,
  `0x43fb20`, `0x43fb40`, `0x43fbe0`, `0x43fc50`, `0x43fdd0`, `0x43fe20`, `0x43fe30` (file size), `0x441550` (close all
  BigFile handles), `0x4417c0`, `0x441bf0`, `0x441c10`, `0x441c20` (the "RKET" header), `0x441c70`, `0x47c2f0` (HUD text
  widget ctor, size 0x1030000 on the 640 × 480 grid).
- Input / sound internals: `0x4672f0`, `0x467570` (keyboard ctor), `0x4690e0`, `0x46aaa0`, `0x46aaf0`, `0x46afa0`,
  `0x46b000` (the 24 physical / 512 logical voice lists, SOUND.md), `0x426270` (HNM stop).
- Camera glue: `0x41e800`, `0x41e860`, `0x41e8c0`, `0x41e920`, `0x41e980`, `0x41e9e0` (CamMgr::Update per mode: copy the
  0x9c-byte state to the mode object, call `0x4254c0` / `0x425810` / `0x421570` / `0x420cf0` / `0x4203c0` / `0x420010`, copy
  back; CAMERA.md), `0x424b10` (camera ctor).
- Textures / pixel formats: `0x47e710`, `0x47e790` (bit count), `0x47e7b0` (the Direct3D texture-format enumeration callback: classifies each format by the bit counts of its A/R/G/B masks and records which of 8888, 565, 1555, 4444 the device offers), `0x47efa0`, `0x47efe0` (pixel format masks per mode),
  `0x47f1a8`, `0x47f1cc` (ARGB → 1555 / 4444), `0x47f710`, `0x47f7a0`, `0x47f8a0`.

## Tools (wwp_tools/)
unshield5.py (IS5 cab), tex_dump2.py, gel_render.py, rck_dump.py, Music.bf extractor (inline), exe_analysis.py, exe_tables.py, vm_tables.py, string_refs.py, vm_peek.py.
