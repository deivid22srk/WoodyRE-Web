#!/bin/sh
# build_web.sh - builds WoodyRE for the browser with Emscripten: ./build_web.sh -> web/dist/
# The engine is the same as on Linux / Android: SDL2 (Emscripten's own port, -sUSE_SDL=2) and OpenGL ES 2/3
# (src/gles, WebGL 2). Needs emsdk 3.1.74 (source emsdk_env.sh). No game files take part in the build: the
# user's CD data is picked in the browser at runtime (see src/datasetup_posix.c, the __EMSCRIPTEN__ part).
set -e
cd "$(dirname "$0")/.."
OUT=web/dist
mkdir -p "$OUT"

SRC="src/level.c src/render_gl.c src/main_engine.c src/player.c src/instance.c src/enemy.c src/boss.c src/water.c
     src/storm.c src/ekovm.c src/audio.c src/hud.c src/hnm.c src/ambient.c src/blackbox.c src/texpack.c
     src/plat_sdl.c src/pad_sdl.c src/datasetup_posix.c src/touch.c src/gles/gles2.c"

# -sASYNCIFY: Sleep() yields to the browser (main loop, frame cap, logo films) and data_find() waits for the
#   user's pick; -sALLOW_MEMORY_GROWTH: the game data lives in the file system, levels are loaded on demand;
#   -lidbfs.js: the browser's IndexedDB keeps the game files for the next visits (no COOP/COEP needed: no threads).
emcc $SRC \
    -std=gnu99 -O2 -fsigned-char -Wno-format-truncation -D_FILE_OFFSET_BITS=64 \
    -Isrc/gles -Isrc \
    -sUSE_SDL=2 -sMAX_WEBGL_VERSION=2 \
    -sASYNCIFY \
    -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sINITIAL_MEMORY=134217728 \
    -sSTACK_SIZE=2MB \
    -sFORCE_FILESYSTEM=1 -lidbfs.js \
    -sENVIRONMENT=web \
    -sMODULARIZE=1 -sEXPORT_NAME=WoodyRE \
    -sEXPORTED_RUNTIME_METHODS=ccall,callMain,FS,HEAPU8 \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
    -sEXIT_RUNTIME=0 \
    -o "$OUT/woodyre.js"

cp web/shell.html "$OUT/index.html"
touch "$OUT/.nojekyll"
echo "Done: $OUT (woodyre.js + woodyre.wasm + index.html)"
