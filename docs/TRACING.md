# Running and tracing the original

The game runs on Windows 11 (tested 18-09-2026, 640×480, DirectDraw/D3D7 via the built-in compatibility; no dgVoodoo needed). Needed: a `Woody.cfg`, and the CD.

## 1. CD

At startup, the exe scans all drives of type CD-ROM for `Common\Woody.rck` (`0x406770`, called from `0x44fa95`) and uses that drive as the base for all data paths (`E:\.\Data\House\code` etc.). A mounted ISO counts as a CD-ROM:

```powershell
Mount-DiskImage -ImagePath "C:\...\WWP.iso"
```

Without a CD, the game shows the message from `WinResource.dll` (string 1/3) and stops.

## 2. Woody.cfg

`Woody.exe` reads `Woody.cfg` from the working directory (`0x401000`): `u32 magic 0x19072001` + 284 (0x11c) bytes of config that are copied 1:1 into `0x4c2bd0`. If the file is missing, the exe starts `Detect.exe` (an MFC dialog around `Setup.dll`). `Setup.dll` exports `Detect(hwnd)`, `DefaultControlSettings()` and `SaveConfig()`; [tools/native/mkcfg.c](../tools/native/mkcfg.c) calls those three and thereby writes a valid cfg without the dialog:

```bash
python -m ziglang cc -target x86-windows-gnu -O2 -o out/mkcfg.exe tools/native/mkcfg.c -lgdi32 -luser32
cd game && ../out/mkcfg.exe      # writes game/Woody.cfg
```

Until 2026-09-26 mkcfg called `CoInitialize` before `Detect`, which makes Setup's sound enumeration bail out, so the cfg
it wrote had the whole sound section 0 and the original ran **without any sound** (SETUP.md 4). Delete such a
`game/Woody.cfg` and run the fixed tool again (Detect would read the old zeros back). Every field and the Detect.exe
control that writes it: SETUP.md 1.

Layout of the 284 bytes (offsets relative to the start of the struct, i.e. file offset +4):

| offset | meaning |
|---|---|
| 0x00, 0x04 | flags "driver chosen", "device chosen" (1) |
| 0x08 | selected D3D device index |
| 0x0c | GUID D3D device (16 B); `84e63de0-46aa-11cf-816f-0000c020156e` = IID_IDirect3DHALDevice (`0x4027f5`) |
| 0x1c | GUID DirectDraw driver (16 B; all 0 → NULL = primary, `0x4027bd`) |
| 0x2c..0x3c | mode info (0x1ff, 1, 1, 3); 0x3c = effects quality (2 = max, `cmp [0x4c2c0c],2` in `0x43b43a`) |
| 0x40, 0x44, 0x48 | width, height, bpp (640, 480, 32 from Detect; `SetDisplayMode` uses them as they are — the 16 pushed at `0x4027eb` is the z-buffer depth, DISPLAY.md §1.1) |
| 0x50 | Detect's "Activate VSync" (`0x47ee16` inverts it on Windows NT; 0 → Flip with DDFLIP_WAIT = vsync, DISPLAY.md §2.2) |
| 0x68..0x98 | sound page: Sound Fx / Music / Cinematic / Invert Left/Right switches, volumes ×0.01, output device, speaker config → `0x5e81b4..` in `0x4691e0` (SETUP.md 1) |
| 0x9c..0xa8 | GUID of the joystick Setup found (the game does not read it) |
| 0xac | 12 × u32 keys "config 1" (DirectInput scancodes: ↑ ↓ ← → Space LShift LCtrl LShift Enter Esc Num0 RCtrl) |
| 0xdc | 12 × u32 keys "config 2" (R F D G + joystick buttons 0x200..0x207) |
| 0x10c..0x118 | joystick detected, joystick mode, keyboard only, ? (INPUT.md §2) |

## 3. Tracer

[tools/wtrace.py](../tools/wtrace.py) is a minimal Win32 debugger (ctypes, WOW64 context) that starts `Woody.exe` and sets int3 breakpoints at fixed addresses (the exe has no relocations):

| address | log |
|---|---|
| `0x401370` `bool __stdcall RouteMessage(EkoMsg*)` | `  SEND id [args]` — identical format to `ekorun`/`ekovm.py --trace` |
| `0x442240` VM tick | `TICK time=<1/100 s> frame=N` |
| `0x4424b0` script loader | `LOAD <path to code>` |
| `0x4427e0` VM init | `INIT` |
| `0x462c60` (disabled logger) | `# LOG <formatstring>` |
| IAT: CreateFileA, LoadLibraryA, CreateProcessA, ExitProcess | `# CreateFileA <path>` etc. |
| IAT: MessageBoxA | logs text and automatically presses OK |

```bash
python tools/wtrace.py game --seconds 120 --out out/trace/live_House.txt
python tools/wtrace.py game --seconds 130 --level W1A --out out/trace/live_W1A.txt   # boot directly into W1A
python tools/tracecmp.py out/trace/live_House.txt      # compare with the C VM
```

`--level` overwrites slot 0 (House) of the level table `0x4b12a0` with the path of the chosen level, so that the game loads that level directly after the logos. The first ~70 s are the three HNM logos.

Messages 1200..1300 (SetTypeInstance/flags) don't go through `0x401370` but directly via `0x403440`, and so don't appear in the live trace; `tracecmp.py` filters them out of the emulator stream. In the original, the init messages are only routed after the first tick (the pump runs after the VM tick of each frame).

### 3.1 Starting a scripted scene in the original: `tools/wsetvar.py`

`python tools/wsetvar.py game --level W2B --var 1 --val 1 --at 3 --from 15 --out out/trace/w2b_boss.txt` starts the level
(like `wiris.py`, logo skipped; allow ~70 s for INIT), calls the game's own `SetVar 0x443ca0(var, val)` 3 s after INIT
(via a fake frame from the VM tick, gadget `add esp, 8; ret` at `0x41a4bd`, so the watchers are woken just as in the game)
and from `--from` s onward logs, every tick, the Perso (pos `+0x1f4`, instance `+0xc`, state `+0x21c`, anim slot 0, onGround),
the camera-mode index, `0x44a650` calls with their position, and the GetHeight result after the snap at the end of a cinematic.
The ISO must be mounted (E:).

## 4. Result

House, 854 ticks (~21 s), with no input: the message stream of the C VM ([src/ekovm.c](../src/ekovm.c)) is identical to that of the original, including the `DELAY`-timed message 1141 at tick 3 (time=10). Two discrepancies were found and fixed along the way: the emulators forwarded the messages of the first init pass (the original discards them), and the `TICK` output of `ekorun` had to come before the tick to be diffable.
