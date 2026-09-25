# Het origineel draaien en traceren

Het spel draait op Windows 11 (getest 18-09-2026, 640×480, DirectDraw/D3D7 via de ingebouwde compatibiliteit; geen dgVoodoo nodig). Nodig zijn een `Woody.cfg`, en de CD.

## 1. CD

De exe zoekt bij het starten alle drives van type CD-ROM af naar `Common\Woody.rck` (`0x406770`, aangeroepen uit `0x44fa95`) en gebruikt die drive als basis voor alle datapaden (`E:\.\Data\House\code` enz.). Een gemounte ISO telt als CD-ROM:

```powershell
Mount-DiskImage -ImagePath "C:\...\WWP.iso"
```

Zonder CD toont het spel de melding uit `WinResource.dll` (string 1/3) en stopt.

## 2. Woody.cfg

`Woody.exe` leest `Woody.cfg` uit de werkmap (`0x401000`): `u32 magic 0x19072001` + 284 (0x11c) bytes config die 1:1 in `0x4c2bd0` worden gekopieerd. Ontbreekt het bestand, dan start de exe `Detect.exe` (een MFC-dialoog rond `Setup.dll`). `Setup.dll` exporteert `Detect(hwnd)`, `DefaultControlSettings()` en `SaveConfig()`; [tools/native/mkcfg.c](../tools/native/mkcfg.c) roept die drie aan en schrijft zo zonder dialoog een geldige cfg:

```bash
python -m ziglang cc -target x86-windows-gnu -O2 -o out/mkcfg.exe tools/native/mkcfg.c -lgdi32 -luser32 -lole32
cd game && ../out/mkcfg.exe      # schrijft game/Woody.cfg
```

Layout van de 284 bytes (offsets t.o.v. het begin van de struct, dus bestandsoffset +4):

| offset | betekenis |
|---|---|
| 0x00, 0x04 | flags "driver gekozen", "device gekozen" (1) |
| 0x08 | geselecteerde D3D-device-index |
| 0x0c | GUID DirectDraw-driver (16 B) |
| 0x1c | GUID D3D-device (16 B; alles 0 → NULL); `84e63de0-46aa-11cf-816f-0000c020156e` = IID_IDirect3DHALDevice |
| 0x2c..0x3c | modus-info (0x1ff, 1, 1, 3); 0x3c = effectenkwaliteit (2 = max, `cmp [0x4c2c0c],2` in `0x43b43a`) |
| 0x40, 0x44, 0x48 | breedte, hoogte, bpp (640, 480, 32 uit Detect; de exe forceert 16 bpp bij `0x4027eb`) |
| 0x50 | VSync-vlag (`0x47ee16` inverteert hem bij het toggelen) |
| 0x58..0x98 | geluidsinstellingen (device, frequentie, volumes ×0.01, speaker-config) → `0x5e81bc..` in `0x4691e0` |
| 0x9c..0xa8 | joystick/controls-opties |
| 0xac | 12 × u32 toetsen "config 1" (DirectInput-scancodes: ↑ ↓ ← → Spatie LShift LCtrl LShift Enter Esc Num0 RCtrl) |
| 0xdc | 12 × u32 toetsen "config 2" (R F D G + joystickknoppen 0x200..0x207) |
| 0x10c..0x118 | joystickmodus, invert L/R, VSync-optie |

## 3. Tracer

[tools/wtrace.py](../tools/wtrace.py) is een minimale Win32-debugger (ctypes, WOW64-context) die `Woody.exe` start en int3-breakpoints zet op vaste adressen (de exe heeft geen relocaties):

| adres | log |
|---|---|
| `0x401370` `bool __stdcall RouteMessage(EkoMsg*)` | `  SEND id [args]` — identiek formaat aan `ekorun`/`ekovm.py --trace` |
| `0x442240` VM-tick | `TICK time=<1/100 s> frame=N` |
| `0x4424b0` scriptloader | `LOAD <pad naar code>` |
| `0x4427e0` VM-init | `INIT` |
| `0x462c60` (uitgeschakelde logger) | `# LOG <formatstring>` |
| IAT: CreateFileA, LoadLibraryA, CreateProcessA, ExitProcess | `# CreateFileA <pad>` enz. |
| IAT: MessageBoxA | logt tekst en drukt automatisch OK |

```bash
python tools/wtrace.py game --seconds 120 --out out/trace/live_House.txt
python tools/wtrace.py game --seconds 130 --level W1A --out out/trace/live_W1A.txt   # boot direct in W1A
python tools/tracecmp.py out/trace/live_House.txt      # vergelijk met de C-VM
```

`--level` overschrijft slot 0 (House) van de leveltabel `0x4b12a0` met het pad van het gekozen level, zodat het spel na de logo's direct dat level laadt. De eerste ~70 s zijn de drie HNM-logo's.

Berichten 1200..1300 (SetTypeInstance/flags) lopen niet via `0x401370` maar rechtstreeks via `0x403440` en verschijnen dus niet in de live trace; `tracecmp.py` filtert ze uit de emulatorstream. De init-berichten worden in het origineel pas na de eerste tick gerouteerd (de pomp draait na de VM-tick van elk frame).

### 3.1 Een gescripte scène in het origineel starten: `tools/wsetvar.py`

`python tools/wsetvar.py game --level W2B --var 1 --val 1 --at 3 --from 15 --out out/trace/w2b_boss.txt` start het level
(zoals `wiris.py`, logo overgeslagen; reken op ~70 s voor INIT), roept 3 s na INIT de eigen `SetVar 0x443ca0(var, val)` aan
(via een nepframe vanaf de VM-tick, gadget `add esp, 8; ret` op `0x41a4bd`, dus de watchers worden gewekt zoals in het spel)
en logt vanaf `--from` s daarna elke tick de Perso (pos `+0x1f4`, instantie `+0xc`, toestand `+0x21c`, anim-slot 0, onGround),
de cameramodus-index, `0x44a650`-aanroepen met hun positie en het GetHeight-resultaat na de snap aan het einde van een cinematic.
Het ISO moet gemount zijn (E:).

## 4. Resultaat

House, 854 ticks (~21 s), zonder invoer: de berichtenstroom van de C-VM ([src/ekovm.c](../src/ekovm.c)) is identiek aan die van het origineel, inclusief het `DELAY`-getimede bericht 1141 op tick 3 (time=10). Twee afwijkingen zijn daarbij gevonden en gefixt: de emulators gaven de berichten van de eerste init-pass door (het origineel gooit die weg), en de `TICK`-uitvoer van `ekorun` moest vóór de tick komen om diffbaar te zijn.
