# WoodyRE

Reverse engineering of *Woody Woodpecker: Escape from Buzz Buzzard Park* (PC, Eko Software / Cryo, 2001),
with the goal of a reimplementation of the engine that loads the original data (`Data\`, `Common\`, `Music.bf`).

There are no game files in this repo. Put the contents of the ISO in `extract/` and the installed
exe/DLLs in `game/` (both are in `.gitignore`).

## Status
- **Script VM ("EKO CODE") fully dissected**: file format, 63 opcodes, tick/scheduler, message routing → [docs/VM.md](docs/VM.md)
- **Disassembler** `tools/ekodisasm.py` (0 errors on all 28 levels) and **emulator** `tools/ekovm.py`
- **C implementation** `src/ekovm.c` (+ `src/ekorun.c` test harness): message traces identical to the Python emulator
- **Message catalogue** script ↔ engine → [docs/MESSAGES.md](docs/MESSAGES.md)
- **Original runs on Windows 11 and is live-traced** (`tools/wtrace.py`, custom Win32 debugger): message stream of the C VM identical to the original (House 854 ticks, W1A init+2 ticks) → [docs/TRACING.md](docs/TRACING.md)
- **RKET resource banks fully dissected** (audio, 2D images, glyph strings, font; no 3D data) → [docs/RCK.md](docs/RCK.md), `tools/rckparse.py`
- **All level formats dissected and validated on 28/28 levels (byte-exact to EOF)**: `.gel` world geometry + kd-tree → [docs/FORMAT_GEL.md](docs/FORMAT_GEL.md) (`tools/gelparse.py`); `.ins` models, skeletal animation, instances, cameras, trigger volumes → [docs/FORMAT_INS.md](docs/FORMAT_INS.md) (`tools/insparse.py`); `.tex/.col/.vis/.lit` → [docs/FORMAT_TEX_COL_VIS_LIT.md](docs/FORMAT_TEX_COL_VIS_LIT.md) (`tools/levelparse.py`)
- **glTF export + viewer**: `tools/export_gltf.py` writes one `.glb` per level (world with textures and per-polygon UV projection, all instances, Woody as a skinned mesh with animation); `viewer/index.html` (three.js) displays it. Can also be opened in Blender.
- Older overview analysis → [docs/ANALYSE.md](docs/ANALYSE.md)

## Building / running
```bash
pip install pefile capstone ziglang pillow
python -m ziglang cc -std=c99 -O2 -o out/ekorun.exe src/ekovm.c src/ekorun.c
./out/ekorun.exe extract/Data/W1A/code 60          # init + 60 ticks, print SEND trace
python tools/ekodisasm.py extract/Data out/ekoasm   # disassemble all levels
python tools/ekovm.py extract/Data                  # message statistics for all levels
```

## Native engine (prototype)
Coordinate system: the level data is **right-handed** with y up (3ds Max export; instances carry a
rotation of -90° about x). Looking along +z, +x is therefore to the left. Vertex colors are R,G,B bytes with 128 = neutral (×2).
`src/level.c` (C loaders for .gel/.tex/.ins + pose evaluation like `0x43a3a0`), `src/render_gl.c` (Win32 + OpenGL 1.1),
`src/main_engine.c` (main loop: the script VM ticks every frame at 1/100 s, messages 1/4/6/1200 drive animation/visibility/type).
Track rotations are applied conjugated (see docs/FORMAT_INS.md, confirmed with `tools/wquat.py` against the original);
texture groups with flag bit 1 are blended additively.
The **results screen** after a level also runs: Woody floats in through the hub door (scripted action 0x4a
with root motion and its own camera track), the panel with the scores comes up, he cheers and then comes the question
"Do you want to save?" (docs/GAMEFLOW.md §5.1-5.2).
`src/player.c` is a PROVISIONAL player controller (not yet the decompiled Perso class): camera-relative walking, gravity,
floor/wall collision against the `.gel` polygons and against the hull nodes of instances, follow camera, and trigger volumes (convex volume nodes)
that send `eko_vol_perso_enter/in/leave` to the script VM; `--walk T` runs T seconds forward for tests. W1A now matches a screenshot of the original (mirroring, colors, orientation of the floating saucers, glow effects).

**Visibility (`0x42a980`/`0x42ac10`, issue #9).** The kd-tree and the sectors of `.gel` (sections 5-7) and the `.vis` are now
loaded and used, as the original does. Per frame: the sector the camera is in → the `.vis` list of that sector →
frustum test on the sector boxes → only the polygons of what remains go to the card (with the frame stamp of `poly+4`, so
a face that is in multiple sectors gets drawn once). Instances outside those sectors no longer get lighting, shadow, or
a draw call; they do get posed though, because `player.c` collides against `node_world`. The same tree now also carries all geometry queries
(floor under a point, push-out against walls, sightlines) that used to walk the whole level first — per frame that happened ten to thirty
times, once per enemy added. `src/geltest.c` checks those queries on synthetic data against brute force.
`F4` disables the culling step by step if something disappears that should be there, `WOODY_PROF=1` shows per frame how many triangles
and sectors remain, and `WOODY_NOKD=1` makes the queries walk the whole level again.
```bash
python -m ziglang cc -std=c99 -O2 -o out/woody.exe src/level.c src/render_gl.c src/main_engine.c src/player.c src/instance.c src/enemy.c src/boss.c src/water.c src/storm.c src/ekovm.c src/audio.c src/hud.c src/hnm.c src/ambient.c -lopengl32 -lgdi32 -luser32 -lwinmm
./out/woody.exe extract/Data                     # without a level: the three logo films (Esc skips, --nologo), then the title screen (House, level 0); Enter starts, then the hub
./out/woody.exe extract/Data W1A                 # arrow keys/WASD walk (relative to camera), space jumps, Enter (or V) looks around (release toggles; arrows turn the view; the mouse only with WOODY_LOOKMOUSE=1, the original never polls it; docs/PERSO_LOOK.md), F5 free camera (then WASD + right mouse button), [ ] animation, Tab instance, F1-F4 toggles (F4 = culling)
./out/woody.exe extract/Data W1A --shot out/s.ppm 3   # screenshot after 3 s and stop
./out/woody.exe extract/Data W1A --res 1920x1080 --aspect 4:3   # display (port extras, docs/DISPLAY.md): --res WxH, --windowed / --fullscreen, --aspect 4:3|wide; WOODY_VSYNC=0/1, WOODY_FPSCAP=N; F11 = fullscreen; Options > Display saves them in woodyre.cfg
./out/woody.exe extract/Data WWS --prev W1A --stats 12 12 25 20 245   # results screen: back from W1A with these stats
./out/woody.exe extract/Data W1A --cam 537 -1800 -2450 0 -10   # camera: x y z yaw pitch (degrees)
python -m ziglang cc -std=c99 -O2 -o out/leveltest.exe src/level.c src/leveltest.c && ./out/leveltest.exe extract/Data   # parser test, 28 levels
python -m ziglang cc -std=c99 -O2 -o out/geltest.exe src/level.c src/geltest.c && ./out/geltest.exe    # kd-tree / .vis queries against brute force, no game data needed
python -m ziglang cc -std=c99 -O2 -o out/hnmtest.exe src/hnm.c src/hnmtest.c && ./out/hnmtest.exe extract/Logo/Eko.hnm out/eko 50 100   # HNM6 film decoder: frames as PPM + sound as WAV (docs/HNM.md)
cc -std=c99 -O1 -Isrc -Iout -o out/switchtest tools/native/switchtest.c src/level.c -lm   # peck switch (message 1042 + rem 0x458e40), no game data needed; see the header of switchtest.c
```

## Viewer
```bash
python tools/export_gltf.py --all            # out/gltf/<LVL>.glb (or a single level: export_gltf.py W1A [--anim N])
python -m http.server 8765                   # then http://localhost:8765/viewer/index.html?level=W1A
```

## Running the original
Mount the ISO (`Mount-DiskImage`), generate `Woody.cfg` with `out/mkcfg.exe` (build: see docs/TRACING.md) and then
`python tools/wtrace.py game --seconds 120 --out out/trace/live.txt`; compare with `python tools/tracecmp.py out/trace/live.txt`.

## Analysis tools for Woody.exe
`tools/disasm.py` (annotated disassembly), `funcinfo.py` (function/callers/strings), `switchmap.py`
(switch → cases with strings/calls), `classmap.py` (SetTypeInstance → classes → message handlers),
`msgblocks.py` (all message handlers in one file), `drange.py` (disassembly per address range), `unshield5.py` (InstallShield 5 cab extractor).
