# Platforms (PORT EXTRA)

The engine is plain C99 with OpenGL 1.1; what it needs from the operating system sits in a few files. Windows builds with
`build.bat` (Zig, Win32 + WGL, waveOut, raw HID + XInput), Linux and the Steam Deck with `build.sh` (the system's C
compiler and SDL2). Both builds draw exactly the same frames: W1A, W2A, House and BlackBox screenshots of the two differ
in under 1 % of the pixels (rasteriser rounding between drivers), and texture-pack hashes (docs/TEXTURES.md) are equal.

| Part | Windows | Linux / SDL2 |
|---|---|---|
| window, GL context, keys, mouse, timer (`render_gl.h` `win_*`) | `render_gl.c` | `plat_sdl.c` |
| OS bits (`plat.h`: VK codes, `Sleep`, `fopen`, message boxes, GL procs) | `<windows.h>` | `plat_sdl.c` |
| sound output (`audio.c`, same mixer) | waveOut thread, 4 x 1024 frames | SDL audio callback, 1024-frame blocks |
| pads (`pad.h`) | `pad.c` (Sony HID, XInput) + WinMM joystick | `pad_sdl.c` (SDL game controllers) |
| finding / copying the game files (`datasetup.h`) | `datasetup.c` | `datasetup_posix.c` |
| texture-pack folders (`texpack.c`) | wide paths | UTF-8 paths |

## 1. What the SDL build does differently
- **Keys** are SDL scancodes stored as Windows VK codes (`vk_of`): letters by the layout (as VK letters are on Windows),
  the rest by position; Ctrl / Shift / Alt set their side and the plain code. A key pressed and released between two
  frames stays down for one frame, so a quick tap is never lost.
- **Files**: `plat_fopen` opens for reading as asked, and when that path is not there looks it up again part by part
  ignoring case: the CD has `Data/Blackbox`, the level table says `BlackBox`; a CD mounted without Joliet may be all
  upper case. Writing is never redirected.
- **Game files**: `WOODY_DATA`, `data/` next to the executable, the CD files next to it, `./extract`, then
  `$XDG_DATA_HOME/WoodyRE/data` (`~/.local/share/WoodyRE/data`). The first start looks for the CD under
  `/media/$USER`, `/run/media/$USER`, `/media` and `/mnt`, asks (SDL message box) and copies the 232 files of the
  manifest with their SHA-1 checked (own SHA-1, no library), printing the progress to the log; the window opens when
  the copy is done. The folder holding `data/` becomes the current directory (woodyre.cfg, woodyre.sav, mods/).
- **Display**: fullscreen is SDL's desktop fullscreen; a window is centred and shrunk to the usable area like on
  Windows; the drawable size counts (HiDPI). Vsync = `SDL_GL_SetSwapInterval`.
- **No WinMM joystick**: every pad goes through SDL.

## 2. Testing without Linux hardware
WSL2 with WSLg (Ubuntu) runs the SDL build with a window and sound: `sudo apt install build-essential libsdl2-dev`,
`sh build.sh /root/woodyre`, then run it on the repository's `extract/Data` under `/mnt/c/...`. Mesa's llvmpipe draws the
frames (slow but exact). Real key presses: `xdotool keydown/keyup --window` (the first event after `windowactivate` can
be lost; wait half a second); SDL's message boxes need `SDL_VIDEODRIVER=x11` to be visible to xdotool and take a mouse
click (`xdotool mousemove X Y click 1`). Pads: `WOODY_VPAD` (docs/INPUT.md 6). Copy files to Windows through
`/mnt/c/...`; WSL's `/tmp` does not survive the distribution stopping between calls.
