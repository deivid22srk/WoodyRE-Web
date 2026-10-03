# Texture packs (PORT EXTRA)

The original has no way to replace its textures; the port lets any texture the game uploads be swapped for a PNG of
any size, e.g. for HD packs. Code: `src/texpack.c`, hooked into the three places that upload textures:
`upload_texture` (render_gl.c: every `.tex` frame, world and models), `upload` (hud.c: every bank image - HUD, pickups,
particles, speech bubbles, sky faces, menu sheets - and the font pages) and `upload` (blackbox.c: the BlackBox images).

## 1. Names
A texture is known by a 64-bit FNV-1a hash of what the game reads from its files:
- `.tex` frame: tag `'T'`, width, height (32-bit little endian each), then the frame's 16-bit texels as stored;
- bank image: tag `'I'`, width, height, then the RGBA the port uploads (after the BGRA bottom-up conversion, or the raw
  row order of the sky faces, docs/SKY.md).

Identical texels give the same hash in every level, so one file replaces a texture everywhere. Every frame of an animated
texture is its own texture. File names are `<anything>_<16 hex digits>.png` (or just `<16 hex digits>.png`); only the
hex part counts, so a pack may rename `128x128_1a2b....png` to `grass_1a2b....png`.

## 2. Folders
Both live in the game's folder, next to `woodyre.cfg` (next to `WoodyRE.exe`, or `%LOCALAPPDATA%\WoodyRE` for the
standalone exe; the developer build uses the current directory):
- `mods\textures\` - the replacements, any subfolders (8 deep), read once at the first texture upload. When two files
  carry the same hash, the first one found wins. The log prints `texture pack: N replacement textures`.
- `mods\dump\<level>\` - written by `--dumptex`: every texture as `<w>x<h>_<hash>.png`, exactly as the game shows it
  (colour key texels transparent black). Bank 0 (`Common\<character>.rck`) goes to `mods\dump\Common\`. A texture
  that is already there is not written again. Play through a level with `--dumptex` to collect everything it uses;
  the `.tex` and the bank images are complete at level load, only the BlackBox images come later.

## 3. How a replacement is drawn
- The game keeps every size, texture coordinate and HUD layout of the original; only the sampled image changes. Keep
  the aspect ratio of the original.
- All mip levels are made with a 2x2 box filter and sampled trilinearly (GL_LINEAR_MIPMAP_LINEAR); the original's own
  textures keep their 4-level 16-bit chain (render_gl.c, 0x47fa60).
- Alpha: a `.tex` group with the colour key bit (flags bit 0) gets the PNG's alpha cut at 128 and black under
  transparent texels (as the original's key, 0x47fc1e - no coloured fringes); a mip texel is opaque when at least two of
  its four sources are. Other `.tex` groups are opaque (alpha ignored; additive groups use only the colour). Bank images
  use the PNG's alpha as is.
- What cannot be replaced: polygons drawn in a flat material colour (material bit 15, ARGB1555 - Woody's own body is
  such a model), the 16 radial light textures (generated, 0x480090), the HNM films, and the rendered shadows.
