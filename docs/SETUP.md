# SETUP.md — Detect.exe / Setup.dll and every field of Woody.cfg

The setup tool of the game is two files in `game/`: **`Detect.exe`** (an MFC property sheet "Configuration" with the pages
Display, Sound and Controls; imagebase 0x400000) and **`Setup.dll`** (the hardware detection and the cfg writer; imagebase
0x10000000, 64 KB, exports `Detect`, `SaveConfig`, `DefaultControlSettings`, `InputDetect`, `WaitInput`,
`GetStringFromCode`, `SafeExit`). `dsetup.dll` is Microsoft's DirectX setup DLL; Detect.exe imports only its ordinals 5
`DirectXSetupA` and 11 `DirectXSetupGetVersion` (`0x417f98`, `0x417f9e`) for the "needs DirectX 7" check and install
(strings 0x68..0x6c); it does not touch the cfg. Woody.exe runs `Detect.exe` when `Woody.cfg` is missing (`0x40104b`, docs/INPUT.md 2).

Method: static analysis (capstone, `tools/disasm.py` over both files; the resources with pefile) plus a **live probe**:
[tools/native/cfgprobe.c](../tools/native/cfgprobe.c) loads Setup.dll, calls `Detect` in an empty folder and prints the
tables the dialog pages show (run on the user's machine: Windows 11, one AMD RX 6800 with two monitors, 12 sound devices).
The name must not contain "setup": Windows' installer detection then demands elevation and refuses to start it.
Addresses `0x10xxxxxx` are Setup.dll, `0x40xxxx`–`0x42xxxx` with a Detect.exe context are Detect.exe, the rest is Woody.exe.
Related: INPUT.md 2 (keys), DISPLAY.md (mode, vsync), SOUND.md 2.4 (sound globals), HNM.md (films), TRACING.md 2.

## 0. Summary

- **Reverse stereo** = Woody.cfg struct `+0x74` (file `+0x78`) = the Sound page checkbox **"Invert Left/Right"** (control
  0x475, handler `0x403a30`). Default 0 (`0x10003322`). Ported (§3.1).
- **VSync**: the Display page checkbox 0x3f2 reads **'Activate " VSync"'** at run time (string 0x6f set over the template
  text "Disable VSYNC" in `0x402c68..0x402c84`), so cfg `+0x50` = 1 means "vsync wanted". Woody.exe honours it as labelled
  on Win9x and inverted on Windows NT (§2.2). Its per-device default (`0x1000363f..0x10003651`) is **1 for a 3D device on a
  DirectDraw driver enumerated with a GUID** (a secondary driver: a 3D add-on board, or on multi-monitor Windows every
  per-monitor entry) and 0 for the primary display driver; Setup's default device is on the primary driver here, so the
  default cfg has 0 = vsync on NT.
- **Sound page**: checkboxes "Sound Fx" `+0x68`, "Music" `+0x6c`, "Cinematic" `+0x70`, "Invert Left/Right" `+0x74`;
  sliders Sound Fx `+0x80` and Music `+0x84` (0..100); combos "Output Device" `+0x8c`, "Speakers configuration" `+0x98`.
  There is **no slider for the third volume `+0x88`** (default 100) and **Woody.exe never reads it** (`[0x5e81f4]`).
  **"Cinematic" only gates the sound of the HNM logo films**, which play at full level whatever the volume sliders say.
- **tools/native/mkcfg.c wrote a cfg without sound** (all of `+0x68..+0x98` = 0) because it called `CoInitialize` before
  `Detect`; fixed (§4). With such a cfg the original starts no sound library at all (`0x469360`: `[0x5e81e8]` = 0).
- Fields no control writes and the game does not read: `+0x2c..+0x38`, `+0x4c`, `+0x54`, `+0x58..+0x64`, `+0x78`, `+0x7c`,
  `+0x88`, `+0x90`, `+0x94`, `+0x9c`, `+0x10c`, `+0x118` (table §1). `+0x118` is the **Language** registry value.
- Messages **1649 / 1650** set / clear `CamMgr+0x290`, which nothing reads (§5).

## 1. Woody.cfg field by field

Setup.dll builds the struct at **`0x10021d88`** (= cfg offset 0; file offset = cfg offset + 4 after the magic
`0x19072001`). `Detect` (`0x100033e0`) sets the defaults (`0x100032a0`), enumerates the DirectDraw drivers and their
Direct3D devices (`0x10002bb0`), reads an existing `Woody.cfg` over the defaults (`0x10003920`, whole struct), copies the
cfg's choices back into the per-device tables of that device when its D3D GUID still matches (`0x10003775..0x100038a6`),
and enumerates the sound devices (`0x10002770`). It returns the base `0x10021ea8` of the data the pages read and write
(Detect.exe keeps it in `[0x4334f8]`; offsets below are from that base where Detect.exe code uses them). `SaveConfig`
(`0x10003990`) copies the selected device's tables into the struct (`0x100039d1..0x10003ae2`), the sound page
(`0x100028d0`), the controls (`0x100025d0`) and the language (`0x10003af9`), and writes magic + 0x11c bytes. Detect.exe
calls it on OK (`0x402325` → `0x402130`).

"dev" = the selected 3D device `[0x100246b8]`. Default = Setup's value on a fresh detection (probe). Woody.exe copies the
file 1:1 to `0x4c2bd0` (`0x4010b1`) and writes the same block back on a normal quit (`0x401130` from `0x405ef5`).

| cfg | file | Setup.dll source | Detect.exe control (page, id, handler) | default | Woody.exe |
|---|---|---|---|---|---|
| 0x00 | 0x04 | 1 at the end of Detect (`0x100038b1`) | – | 1 | not read |
| 0x04 | 0x08 | 1 (`0x100038ac`) | – | 1 | not read |
| 0x08 | 0x0c | `[0x100246b8]` (`0x100039d6`) | Display, combo 0x3ef "Display Driver" (`0x402e80` → base+0x2810): the device list, names at base+0x140c | count − 1 (`0x10003763`) | not read |
| 0x0c | 0x10 | device +0xe80 = D3D device GUID (`0x10003a1d..0x10003a44`) | via 0x3ef | IID_IDirect3DHALDevice | `0x4027f5` |
| 0x1c | 0x20 | device +0xe90 = its DirectDraw driver GUID (`0x100039f0..0x10003a17`) | via 0x3ef | 0 = primary | `0x4027bd` |
| 0x2c | 0x30 | device +0 = capability flags of `0x10002970` (`0x10003ae7`) | – | 0x1ff | not read |
| 0x30 | 0x34 | `[0x100337bc + 20·dev]`, list `0x100337ac`: 0, 1 if flag 0x80 (bilinear filtering caps), 2 if 0x100 (trilinear) — strings 0x9a "Disactivate", 0x9b "Bilinear", 0x9c "Trilinear" exist, no dialog shows them | – | 1 | not read |
| 0x34 | 0x38 | `[0x10033948 + 16·dev]`, list `0x1003393c`: 0, 1 if flag 2; 0 on a secondary-driver device, else the last | – | 1 (0 on the RX 6800 entries) | not read |
| 0x38 | 0x3c | `[0x10033a90 + 24·dev]`, list `0x10033a7c`: 0, 1..3 if flag 2; 0 on a secondary-driver device, else the last | – | 3 | not read |
| 0x3c | 0x40 | `[0x10033c6c + 20·dev]`, list `0x10033c5c`: 0, 1, 2 if flag 0x10; default the last | Display, combo 0x468 "Effects quality" (`0x402f70`): 0x46a Minimum, 0x46b Normal, 0x46c Maximum | 2 | `0x42b380`, `0x43b43a` (LIGHTING.md 5) |
| 0x40..0x48 | 0x44 | `[0x100247ac + (256·dev + i)·12]` w, h, bpp (`0x10003a9d..0x10003abb`), i = `[0x1002475c + 4·dev]` | Display, combo 0x3f0 "Display Mode" (`0x402ec0`), items "%d x %d, %d bits" (`0x402d4c`) | first 640x480x32, else the first 640x480 (`0x100031ec..0x1000321d`) | `0x4027da`, `0x40614f` |
| 0x4c | 0x50 | `[0x10033e8c + 4·dev]` = 1 if flag 2, else −1 (`0x10003682`) | – | 1 | not read |
| **0x50** | **0x54** | `[0x10033dec + 4·dev]` (`0x10003648` / `0x10003651`) | Display, checkbox 0x3f2 **'Activate " VSync"'** (`0x402fa0` toggles; `0x402ef0` disables it for −1) | 1 on a secondary-driver device, else 0 (§2.3) | `0x47ee16` (inverted on NT), `0x47eea0` (§2.2) |
| 0x54 | 0x58 | `[0x10033e3c + 4·dev]` = 1 if flag 4, else −1 (`0x10003666`) | – | 1 | not read |
| 0x58..0x64 | 0x5c | never written by Setup (a loaded cfg passes through) | – | 0 | not read |
| **0x68** | 0x6c | `[0x10033edc]` (base+0x12034) | Sound, checkbox 0x46f "Sound Fx" (`0x403a00`) | 1 | `[0x5e81b4]`: sfx voices (`0x46a863`) |
| **0x6c** | 0x70 | `[0x10033ee0]` (+0x12038) | Sound, checkbox 0x470 "Music" (`0x403a10`) | 1 | `[0x5e81b8]`: music (`0x46c760..0x46cbf0`) |
| **0x70** | 0x74 | `[0x10033ee4]` (+0x1203c) | Sound, checkbox 0x71 "Cinematic" (`0x403a20`) | 1 | `[0x5e81bc]`: HNM film sound only (§3.3) |
| **0x74** | 0x78 | `[0x10033ee8]` (+0x12040) | Sound, checkbox 0x475 **"Invert Left/Right"** (`0x403a30`) | 0 | `[0x5e81c0]`: reverse stereo `0x46b7e0` (§3.1) |
| 0x78 | 0x7c | `[0x10033eec]`: never set (`0x10002770` does not copy it back) → always 0 | – | 0 | `[0x5e81c4]`, not read |
| 0x7c | 0x80 | `[0x10033ef0]`: pass-through | – | 0 | `[0x5e81c8]`, not read |
| **0x80** | 0x84 | `[0x10033ef4]` (+0x1204c) | Sound, slider 0x472 beside "Sound Fx" (range 0..100 `0x4036da`; `0x403a40` on NM_RELEASEDCAPTURE) | 100 (`0x100032c0`) | `[0x5e81cc]`, master `[0x5e81ec]` = v/100 |
| **0x84** | 0x88 | `[0x10033ef8]` (+0x12050) | Sound, slider 0x473 beside "Music" (`0x403a70`) | **30** (`0x10003318`) | `[0x5e81d0]`, master `[0x5e81f0]` |
| 0x88 | 0x8c | `[0x10033efc]` | **none** (only two sliders on the page) | 100 (`0x100032c5`) | `[0x5e81d4]` / `[0x5e81f4]`, **not read** (§3.2) |
| 0x8c | 0x90 | `[0x10033f08]` (+0x12060) | Sound, combo 0x40a "Output Device" (`0x403980`), names `0x10033f0c + 256·i` | count − 1 (`0x100027ad`) | `[0x5e81d8]`, sound system +0x108 → `0x48ead0(i, 0x11)` device GUID (`0x469391`) |
| 0x90 | 0x94 | `[0x1003530c + 4·i]` = DSCAPS.dwMaxHw3DStaticBuffers of device i (`0x1000289d`) | – | 0 | `[0x5e81dc]`, not read |
| 0x94 | 0x98 | `[0x1003535c + 4·i]` = DSCAPS.dwMaxHwMixingStaticBuffers (`0x1000289a`) | – | 1 | `[0x5e81e0]`, not read |
| 0x98 | 0x9c | `[0x10033f00]` (+0x12058) | Sound, combo 0x47d "Speakers configuration" (`0x4039b0`): items 0x477..0x47f = 0 Default system configuration, 1 Headphones, 2 Stereo speakers, 3 Wide angle, 4 Narrow angle, 5 Minimum angle, 6 Maximum angle stereo speakers, 7 Quadrophonic, 8 Surround | 0 | `[0x5e81e4]`, `0x4693b0`: 1..8 → SetSpeakerConfig 1, 4, 0x140004, 0xa0004, 0x50004, 0xb40004, 3, 5 (`0x469468` table); 0 = leave the system's |
| 0x9c | 0xa0 | `0x1000d6e0..ec` joystick GUID (`InputDetect` callback `0x10001070`) | – | 0 | not read |
| 0xac | 0xb0 | keys config 1 `[0x100353ac]` (`0x100025d2`) | Controls page | table `0x1000c030` | `0x44fc5a..` (INPUT.md 2) |
| 0xdc | 0xe0 | keys config 2 `[0x100353dc]` | Controls page | table `0x1000c060` | `0x44fd42..` |
| 0x10c | 0x110 | `[0x1003540c]` = a joystick was found (`[0x1000d6d8]`, `InputDetect` `0x10001841`) | when 0: the joystick group (0x45e, 0x45f, 0x460, 0x46a) is disabled and "Key" forced (`0x401d78..0x401ddc`) | 0 | not read |
| 0x110 | 0x114 | `[0x10035418]` | Controls, radio 0x45f "Digital" = 1 (`0x402040`) / 0x460 "Analogique" = 0 (`0x402050`); strings 0x96..0x98 "Digital", "Analog", "Joystick Mode" | 0 | `0x44fc20`: mode 1 (digital) / 2 (analog), identical in the game (INPUT.md 3) |
| 0x114 | 0x118 | `[0x1003541c]` | Controls, radio 0x46b "Key" = 1 (`0x4020a0`, also disables Digital/Analog) / 0x46a "Joy" = 0 (`0x402060`) | 1 (`0x10003360`) | `0x44fc05`: keyboard only |
| 0x118 | 0x11c | `[0x10035420]` ← `HKCU\Software\Eko Software\The Gift`, value **"Language"** (`0x10003364..0x100033b8`; 0 without the key) | – | 0 | not read |

Device flags (`0x10002970(ddcaps, d3ddesc)`): 0 = rejected, unless the driver's surface caps have FLIP | PRIMARYSURFACE |
TEXTURE | ZBUFFER (`0x21210`), the device DRAWPRIMTLVERTEX, 16/32-bit render and z depths, min texture ≤ 16, max ≥ 256,
CULLNONE, flat + gouraud RGB shading and ONE/ONE blending. Then 1, |2 (→ 3) with SPECULARFLATRGB shading and DESTCOLOR /
SRCCOLOR blending (`0x10002a4e..0x10002a78`), |8 with ZERO / INVSRCCOLOR, |0x14 with alpha-blended shading and SRCALPHA /
INVSRCALPHA (`0x10002a94..0x10002ab6`), |0x20 with two texture stages and MODULATE + ADD, |0x40 with MIPMAP surfaces,
|0x80 / |0x100 with LINEAR / LINEARMIPLINEAR filtering. Flag 2 feeds `+0x34`, `+0x38`, `+0x4c`; 4 `+0x54`; 0x10 the
"Maximum" quality of `+0x3c`; 0x80 / 0x100 `+0x30`. Only the quality list reaches anything a user sees.

Device order: the D3D devices are qsorted with `0x10002b70`, which only separates hardware-rasterised devices from the
rest; all accepted devices are hardware (`0x10002fc7`: D3DDEVCAPS_HWRASTERIZATION), so the order is whatever MSVC's qsort
leaves. Probe: 0 "Direct3D T&L HAL" (primary), 1..4 "AMD Radeon RX 6800" (the two per-monitor GUIDs, HAL and T&L each),
5 "Direct3D HAL" (primary) — the default device 5 therefore gives the cfg the HAL GUID and a zero DirectDraw GUID, as the
shipped/generated cfgs have. A device on a secondary driver is named after the driver, one on the primary after the D3D
device (`0x100030ba..0x10003110`). The log `Log3d.txt` ("Scanning : ...", `0x10002bb0`) is written next to Setup.dll.

## 2. VSync

### 2.1 Label
The Display page's DDX (`0x402b50`) binds control 0x3f2 to the CWnd at `this+0xc8`; `OnInitDialog` (`0x402bc0`) loads
string 0x6f **'Activate " VSync"'** into it (`0x402c68..0x402c84`), replacing the template text "Disable VSYNC". Checked =
`[base + 0x11f44 + 4·dev]` = `[0x10033dec + 4·dev]` = 1 (`0x402f47`), toggled by `0x402fa0` (`v = (v == 0)`), and the box is
unchecked and disabled when the value is −1 (`0x402efe`), which Setup itself never stores.

### 2.2 What Woody.exe does with it (DISPLAY.md 2.2)
Device init: `if ([0x4c3aa0]) [0x4c2c20] = 1 − [0x4c2c20]` (`0x47ee0e..0x47ee22`; `[0x4c3aa0]` = 1 when GetVersionExA
reports platform 2 = NT, `0x405ddf`; 0 for platform 1 = Win9x, `0x405dbe`). Present (`0x47eea0`): non-zero → `Flip(DDFLIP_WAIT)`
= vsync, zero → `Blt` with DDBLTFX_NOTEARING. So:

| platform | "Activate VSync" (cfg +0x50) | present |
|---|---|---|
| Win9x | 1 | Flip, vsync — as labelled |
| Win9x | 0 | Blt + NOTEARING — as labelled |
| NT / 2000 / XP and later | 1 | Blt + NOTEARING — **inverted** |
| NT / 2000 / XP and later | 0 (Setup default on the primary driver) | Flip, vsync — **inverted** |

Settled, from the code: the label is right on Win9x and wrong on NT. Whether the inversion was meant (a workaround for
NT drivers) cannot be proven from the binary, but it is not a consistent design: the inversion is applied to the global
that the game **writes back to Woody.cfg on a normal quit** (`0x401130` writes the whole block `0x4c2bd0`, incl. `[0x4c2c20]`,
which nothing else touches), so on NT every session that ends through the quit path stores the opposite flag, and the next
session runs the other present mode (Flip, Blt, Flip, ...). The generated cfg in `game/` holds 0.

### 2.3 Per-device default
`Detect`, per device (`0x1000347c` loop): `[esp+0x14]` = device record `+0x220` ≠ 0 (`0x100034a1..0x100034b0`), which the
D3D enumeration callback `0x10002f80` sets (to the address of its own GUID copy, `0x100030c6..0x100030ef`) exactly when the
DirectDraw enumeration callback `0x10002c30` got a non-NULL driver GUID (`0x10002d44..0x10002d76`). Then `0x1000363f`:
vsync = 1 for such a device, else 0. A driver with a GUID is a secondary one: in 2001 a 3D add-on board (3dfx Voodoo /
Voodoo2 style, fullscreen-only, own DirectDraw driver); with `DirectDrawEnumerateExA(..., 7)` (`0x10002bc7`: attached
secondary, detached and non-display devices) on a multi-monitor system also every per-monitor entry. The same flag also
sets `+0x34` / `+0x38` to 0 and, on a first run without a cfg, clears the three sound switches (§3.4). Probe: devices 1..4
(the RX 6800 per monitor) 1, devices 0 and 5 (primary driver) 0.

### 2.4 Port
`main_engine.c setup_import`: when `woodyre.cfg` has no `vsync=` yet, the port takes Woody.cfg `+0x50` with the NT rule,
`vsync = (flag != 1)` (the port only runs on the NT line). The default cfg (0) keeps the port's default, vsync on. Later
runs use `woodyre.cfg` (the Display page, DISPLAY.md 4); the port never writes Woody.cfg, so its toggling does not happen.

## 3. Sound options

### 3.1 Reverse stereo (+0x74)
`0x4691e8`: `[0x5e81c0] = [0x4c2c44]`. `0x46b7e0` (called on every 3D position commit: `0x46b9e1`, `0x46bf38`, `0x46c066`)
returns when it is 0; else it mirrors the source through the listener's median plane: `n = normalize(col1) × normalize(col2)`
of the listener Repere (= its right axis), `src += −2·(n·src − n·lis)·n` (`0x4a9504` = −2). The listener lies in that plane,
so the distance stays and the right component changes sign: left and right swap for every 3D voice; 2D voices are untouched.
**Port**: `audio.c voice_geom` negates the pan when `audio_reverse_stereo(1)` (exactly equivalent); the value comes from
`woodyre.cfg reverse_stereo=0/1`, imported once from a live Woody.cfg `+0x74`; `WOODY_REVSTEREO=0/1` overrides it for one
run without saving. Verified with `WOODY_AUDIODUMP` on W1A (music 0, 12 s, many 3D loops left of the camera): L/R rms 1.53
without, 0.73 with.

### 3.2 The third volume (+0x88) and SoundFx `this+0x1c`
- `+0x88` → `[0x5e81d4]` and `[0x5e81f4]` = v/100 (`0x469264..0x4692b9`); the only other writer is the sound system's
  `vt[0x64]` = `0x4695d0` (getter `vt[0x60]` = `0x4695c0`). No caller uses `vt[0x60]`/`vt[0x64]` (all 14 calls through the
  getter `0x468f50` are `vt[0x54]`/`vt[0x5c]`: `0x405334`, `0x405347`, `0x4602d4..0x460449`), and nothing reads `[0x5e81f4]`.
  Detect.exe has no slider for it. By its place it is the "Cinematic" volume, but it is dead in this build. Not ported.
- SoundFx `[0x5e48c8]` `+0x1c` ("pitch override"): set to 1.0 by the base ctor `0x468910` (`0x468951`) and never written
  again (the SoundFx class has one virtual `0x4670f0`; its non-virtual methods `0x468980..0x468e60` only read it at
  `0x468c7f`; every external use of `[0x5e48c8]` passes the object as an argument). The `≠ 1.0` test at `0x468c82` never
  fires. Nothing to port.

### 3.3 Films and the volume options
`0x426a57..0x426a97` (the HNM player's Open, docs/HNM.md 1.2): player `+0x2c` = `[[0x5e81f8] + 0x4c]` (the sound library's
object) when `[0x5e81bc]` ("Cinematic", cfg `+0x70`) is set, else 0; `+0x60` = 1 only with that flag and a non-zero `+0x2c`.
The film core (`0x492000..0x4a0000`) never calls SetVolume or SetPan on its buffer (no `vt[0x3c]`/`vt[0x40]` call there; the
sound library sets volumes per voice, `0x48f312`, on its own buffers only), so **the films play at full level; the Sound Fx and Music
sliders do not apply, only the "Cinematic" switch** (and, since the film uses the library's object, no sound at all when
all three switches are off). The rtc cinematic streams (SOUND.md 6) do not look at `[0x5e81bc]`.
**Port**: `logos_play` opens the film sound only when `film_sound=1` (woodyre.cfg; imported once from a live Woody.cfg
`+0x70`, default 1), at full gain as before. Verified: the first 2.7 s of the Cryo film dump rms 563 with, 0 without.

### 3.4 First-run sound switches
`Detect` passes a flag to `0x10002770` (`0x100038f7`): 1 unless a valid Woody.cfg was read (`0x10003784`) or the default
device is on the primary driver (`0x100038e5`). With it set, `0x100027c4..0x100027d0` clear `+0x68`, `+0x6c`, `+0x70`: a fresh
install whose default 3D device sits on a secondary driver starts with all sound off. `0x10002770` also returns early,
without filling the sound page at all, when `CoInitialize` does not return S_OK (`0x10002783`).

### 3.5 The Sound Fx / Music volumes (+0x80 / +0x84)
The two sliders of Detect's Sound page (0..100, Setup defaults **100 / 30**, §1) are the game's master volumes: `0x4691e2..0x4692b9`
copies `[0x4c2c50]` / `[0x4c2c54]` to `[0x5e81cc]` / `[0x5e81d0]` and sets `[0x5e81ec]` / `[0x5e81f0]` = v / 100 (SOUND.md §2.4); the
in-game Options page changes the same globals, and they are written back to Woody.cfg at exit (MENU_OPTIONS.md).
**Port**: `setup_import` takes them the same way as reverse stereo and film sound: only from a **live** sound section (one of the
switches `+0x68` / `+0x6c` / `+0x70` on; the all-zero section of the old mkcfg cfg is ignored), clamped to 0..100, and only while
`woodyre.cfg` has no `sfx=` / `music=` line (each key on its own). `opt_write` puts both keys in at the first exit (and on the Options
page's Continue), so from then on woodyre.cfg leads and Woody.cfg is never read for them again; the port still never writes Woody.cfg.
Without a live Woody.cfg the port's own defaults stay (100 / 70). The two switches themselves (Sound Fx / Music off = that part
silent in the original) are not mapped. Verified with `WOODY_CFG=` test files (run from a scratch directory): a dead section keeps
100 / 70; a live 100 / 30 gives `sfx=100 music=30` in the new woodyre.cfg; a second run with a live 55 / 45 keeps 100 / 30; a
woodyre.cfg with only `music=80` takes sfx 55 from Woody.cfg and keeps music 80. The boot line `setup: ...` says what was imported.

## 4. mkcfg.c and the silent game

`tools/native/mkcfg.c` called `CoInitialize(NULL)` before `Detect`. Setup's `0x10002770` then gets S_FALSE from its own
`CoInitialize`, returns at once, and the sound page tables `0x10033edc..0x10033f08` stay 0; `SaveConfig` copies them
(`0x100028d0`), so the generated cfg had sfx/music/cinematic off and all volumes 0 (the `game/Woody.cfg` of this repo still
has that). Woody.exe then never initialises its sound library (`0x469360`: `[0x5e81e8]` = OR of the three switches = 0).
Detect.exe itself is not affected: it calls `Detect` from `InitInstance` (`0x4022c1`) before OLE is initialised (the only
`OleInitialize`, `0x423002`, belongs to the property sheet's control container). Probe: with `co` the sound page is all 0,
without it `1, 1, 1, 0, …, 100, 30, 100, device 11`. The fix removes the call; regenerate `game/Woody.cfg` with the fixed
tool to hear the original (delete the old one first, or Detect reads its zeros back via `0x10003920`/`0x10002770`).
The port ignores a Woody.cfg sound section with all three switches 0 (`setup_import`).

## 5. Messages 1649 / 1650

Jump table `0x4686b0`: 1649 → `0x46864f` = `0x41fa40` on `[0x4c737c]` (CamMgr), 1650 → `0x468660` = `0x41fa50`; no arguments.
`0x41fa40` sets `CamMgr+0x290 = 1`, `0x41fa50` sets it to 0; the camera mode switch also clears it (`0x41f50c..0x41f5dc`,
CAMERA.md 1). 1653 (`0x468603`) and 1654 (`0x46862c`) fall through into the same `0x41fa40` call after their LFO calls.
**No code reads `CamMgr+0x290`** (every other `+0x290` access in the exe is on another object: the sub-camera's height
`0x425220`, particles, ...), and no script sends 1649/1650 (SOUND.md 1). Port: `snd_msg` stores the flag (`g_cam_hold`)
and logs it with `WOODY_SNDLOG`; there is nothing else to do.

## 6. Open
- `+0x30`, `+0x34`, `+0x38`, `+0x4c`, `+0x54` are capability-derived choices for options the shipped dialogs do not show;
  their intended meaning beyond the filter strings of `+0x30` is unknown and irrelevant to the game (never read).
- The rtc streams' level (SOUND.md 8.2) is untouched by this document.
