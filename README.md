# WoodyRE

Reverse engineering van *Woody Woodpecker: Escape from Buzz Buzzard Park* (PC, Eko Software / Cryo, 2001),
met als doel een reimplementatie van de engine die de originele data (`Data\`, `Common\`, `Music.bf`) inlaadt.

Er staan geen game-bestanden in deze repo. Zet de inhoud van de ISO in `extract/` en de geïnstalleerde
exe/DLL's in `game/` (beide staan in `.gitignore`).

## Stand van zaken
- **Script-VM ("EKO CODE") volledig ontleed**: bestandsformaat, 63 opcodes, tick/scheduler, berichtroutering → [docs/VM.md](docs/VM.md)
- **Disassembler** `tools/ekodisasm.py` (0 fouten op alle 28 levels) en **emulator** `tools/ekovm.py`
- **C-implementatie** `src/ekovm.c` (+ `src/ekorun.c` testharnas): berichttraces identiek aan de Python-emulator
- **Berichtencatalogus** script ↔ engine → [docs/MESSAGES.md](docs/MESSAGES.md)
- **Origineel draait op Windows 11 en wordt live getraceerd** (`tools/wtrace.py`, eigen Win32-debugger): berichtenstroom van de C-VM identiek aan het origineel (House 854 ticks, W1A init+2 ticks) → [docs/TRACING.md](docs/TRACING.md)
- **RKET resource-banks volledig ontleed** (geluid, 2D-afbeeldingen, glyph-strings, font; geen 3D-data) → [docs/RCK.md](docs/RCK.md), `tools/rckparse.py`
- **Alle levelformaten ontleed en gevalideerd op 28/28 levels (byte-exact tot EOF)**: `.gel` wereldgeometrie + kd-boom → [docs/FORMAT_GEL.md](docs/FORMAT_GEL.md) (`tools/gelparse.py`); `.ins` modellen, skeletanimatie, instanties, camera's, triggervolumes → [docs/FORMAT_INS.md](docs/FORMAT_INS.md) (`tools/insparse.py`); `.tex/.col/.vis/.lit` → [docs/FORMAT_TEX_COL_VIS_LIT.md](docs/FORMAT_TEX_COL_VIS_LIT.md) (`tools/levelparse.py`)
- **glTF-export + viewer**: `tools/export_gltf.py` schrijft per level één `.glb` (wereld met texturen en per-polygoon UV-projectie, alle instanties, Woody als geskinde mesh met animatie); `viewer/index.html` (three.js) toont het. Ook te openen in Blender.
- Oudere overzichtsanalyse → [docs/ANALYSE.md](docs/ANALYSE.md)

## Bouwen / draaien
```bash
pip install pefile capstone ziglang pillow
python -m ziglang cc -std=c99 -O2 -o out/ekorun.exe src/ekovm.c src/ekorun.c
./out/ekorun.exe extract/Data/W1A/code 60          # init + 60 ticks, print SEND-trace
python tools/ekodisasm.py extract/Data out/ekoasm   # disassembleer alle levels
python tools/ekovm.py extract/Data                  # berichtstatistieken van alle levels
```

## Native engine (prototype)
`src/level.c` (C-loaders .gel/.tex/.ins + pose-evaluatie zoals `0x43a3a0`), `src/render_gl.c` (Win32 + OpenGL 1.1),
`src/main_engine.c` (hoofdlus: script-VM tikt elk frame in 1/100 s, berichten 1/4/6/1200 sturen animatie/zichtbaarheid/type).
```bash
python -m ziglang cc -std=c99 -O2 -o out/woody.exe src/level.c src/render_gl.c src/main_engine.c src/ekovm.c -lopengl32 -lgdi32 -luser32
./out/woody.exe extract/Data W1A                 # WASD + rechtermuisknop, Shift snel, [ ] animatie, Tab instantie, F1-F3 toggles
./out/woody.exe extract/Data W1A --shot out/s.ppm 3   # screenshot na 3 s en stoppen
python -m ziglang cc -std=c99 -O2 -o out/leveltest.exe src/level.c src/leveltest.c && ./out/leveltest.exe extract/Data   # parsertest 28 levels
```

## Viewer
```bash
python tools/export_gltf.py --all            # out/gltf/<LVL>.glb (of één level: export_gltf.py W1A [--anim N])
python -m http.server 8765                   # daarna http://localhost:8765/viewer/index.html?level=W1A
```

## Origineel draaien
ISO mounten (`Mount-DiskImage`), `Woody.cfg` genereren met `out/mkcfg.exe` (bouwen: zie docs/TRACING.md) en dan
`python tools/wtrace.py game --seconds 120 --out out/trace/live.txt`; vergelijken met `python tools/tracecmp.py out/trace/live.txt`.

## Analyse-tools voor Woody.exe
`tools/disasm.py` (geannoteerde disassembly), `funcinfo.py` (functie/callers/strings), `switchmap.py`
(switch → cases met strings/calls), `classmap.py` (SetTypeInstance → klassen → berichthandlers),
`msgblocks.py` (alle berichthandlers in één bestand), `drange.py` (disassembly per adresbereik), `unshield5.py` (InstallShield 5 cab-extractor).
