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
Assenstelsel: de leveldata is **rechtshandig** met y omhoog (3ds Max-export; instanties dragen een
rotatie van -90° om x). Kijkend langs +z ligt +x dus links. Vertexkleuren zijn R,G,B-bytes met 128 = neutraal (×2).
`src/level.c` (C-loaders .gel/.tex/.ins + pose-evaluatie zoals `0x43a3a0`), `src/render_gl.c` (Win32 + OpenGL 1.1),
`src/main_engine.c` (hoofdlus: script-VM tikt elk frame in 1/100 s, berichten 1/4/6/1200 sturen animatie/zichtbaarheid/type).
Trackrotaties worden geconjugeerd toegepast (zie docs/FORMAT_INS.md, bevestigd met `tools/wquat.py` op het origineel);
textuurgroepen met vlag-bit 1 worden additief geblend.
Het **resultatenscherm** na een level draait ook: Woody komt bij de hub-deur naar binnen zweven (gescripte actie 0x4a
met wortelbeweging en eigen cameratrack), het paneel met de scores komt op, hij juicht en daarna volgt de vraag
"Do you want to save?" (docs/GAMEFLOW.md §5.1-5.2).
`src/player.c` is een VOORLOPIGE spelerbesturing (nog niet de gedecompileerde Perso-klasse): camera-relatief lopen, zwaartekracht,
vloer/muur-botsing op de `.gel`-polygonen en op de hull-nodes van instanties, volgcamera, en triggervolumes (convexe volume-nodes)
die `eko_vol_perso_enter/in/leave` naar de script-VM sturen; `--walk T` loopt T seconden vooruit voor tests. W1A komt nu overeen met een screenshot van het origineel (spiegeling, kleuren, oriëntatie van de zwevende schotels, gloei-effecten).

**Zichtbaarheid (`0x42a980`/`0x42ac10`, issue #9).** De kd-boom en de sectoren van `.gel` (secties 5-7) en de `.vis` worden nu
geladen en gebruikt, zoals het origineel dat doet. Per frame: de sector waar de camera in staat → de `.vis`-lijst van die sector →
frustumtest op de sectorboxen → alleen de polygonen van wat overblijft gaan naar de kaart (met het framestempel van `poly+4`, zodat
een vlak dat in meerdere sectoren staat één keer wordt getekend). Instanties buiten die sectoren krijgen geen belichting, schaduw of
tekenbeurt meer; poseren doen ze wél, want `player.c` botst tegen `node_world`. Dezelfde boom draagt nu ook alle geometrie-queries
(vloer onder een punt, push-out tegen muren, zichtlijnen) die eerst het hele level afliepen — per frame gebeurde dat tien tot dertig
keer, één keer per vijand erbij. `src/geltest.c` controleert die queries op synthetische data tegen brute force.
`F4` zet de culling stap voor stap uit als er iets verdwijnt dat er hoort te zijn, `WOODY_PROF=1` toont per frame hoeveel driehoeken
en sectoren er overblijven en `WOODY_NOKD=1` laat de queries weer het hele level aflopen.
```bash
python -m ziglang cc -std=c99 -O2 -o out/woody.exe src/level.c src/render_gl.c src/main_engine.c src/player.c src/instance.c src/enemy.c src/ekovm.c src/audio.c src/hud.c -lopengl32 -lgdi32 -luser32 -lwinmm
./out/woody.exe extract/Data                     # zonder level: het titelscherm (House, level 0); Enter start, daarna de hub
./out/woody.exe extract/Data W1A                 # pijltjes/WASD lopen (t.o.v. camera), spatie springen, F5 vrije camera (dan WASD + rechtermuisknop), [ ] animatie, Tab instantie, F1-F4 toggles (F4 = culling)
./out/woody.exe extract/Data W1A --shot out/s.ppm 3   # screenshot na 3 s en stoppen
./out/woody.exe extract/Data WWS --prev W1A --stats 12 12 25 20 245   # resultatenscherm: terug uit W1A met die statistieken
./out/woody.exe extract/Data W1A --cam 537 -1800 -2450 0 -10   # camera: x y z yaw pitch (graden)
python -m ziglang cc -std=c99 -O2 -o out/leveltest.exe src/level.c src/leveltest.c && ./out/leveltest.exe extract/Data   # parsertest 28 levels
python -m ziglang cc -std=c99 -O2 -o out/geltest.exe src/level.c src/geltest.c && ./out/geltest.exe    # kd-boom / .vis-queries tegen brute force, zonder gamedata
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
