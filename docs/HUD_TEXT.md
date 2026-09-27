# HUD_TEXT.md — the 2D layer: font, strings, text box (1080), HUD, sprites

All addresses are VAs in `game/Woody.exe` (image base 0x400000), from static analysis
(`out/disasm_full.txt`). Verified with [tools/fontrender.py](../tools/fontrender.py) (renders the font and
strings from the banks to PNG; output in `out/hud/`). Status per claim: unmarked = read from the code;
**uncertain** = derived / not fully followed.

Correction to RCK.md made up front: images (type 1) use **no magenta colour key**. The field `+6` of the
image header means "has alpha channel" and the fourth byte of every pixel is a real 8-bit alpha (§5.1).
The `reserved` field of a string item is garbage (old pointer / two glyph codes by coincidence); no kerning.

---------------------------------------------------------------------------------------------------

## 1. Font (rck type 3) and strings (rck type 2)

### 1.1 File layout of the font payload (loader `0x43f9a0`, via `0x43fc90`)

The loader reads: 8-byte item header (`0x43f9d7`), a **0x1c-byte header** into `font+4` (`0x43f9e6`), allocates
`n·20` bytes (`0x441a30`), reads **n glyph records of 20 bytes** (`0x43f9fd`), and then, `pages` times, a
surface of `size × size` pixels with `0x47f8d0` → `0x480550` (4 bytes per pixel, `0x4806a6..0x480713`).

| offset | type | W1A value | meaning | evidence |
|---|---|---|---|---|
| 0x00 | u32 | 75 | `n` = number of glyphs (`font+4`) | `0x4418d4`, `0x441a38` |
| 0x04 | u32 | 3 | `pages` = number of atlas pages (`font+8`) | `0x43fa00`, loop `0x43fa1b` |
| 0x08 | u32 | 256 | `size` = width = height of a page (`font+0xc`) | `0x43fa4e` (pushed twice) |
| 0x0c | f32 | 56.0 | `H` line height in atlas pixels (`font+0x10`) | `0x441960`, `0x441980`, `0x441a80` |
| 0x10 | f32 | 0.0 | unused (`font+0x14`) | no reader found |
| 0x14 | f32 | 16.0 | `B` (`font+0x18`): `H − B` = 40 = nominal font size that `SetSize` normalizes to | `0x441a80` |
| 0x18 | f32 | −3.0 | `M` margin around the glyph (outline/shadow), `font+0x1c`; cell height = `H + 2·|M|` = 62 | `0x441980` |
| 0x1c | 20·n | | glyph records | |
| 0x1c+20n | `pages·size²·4` | | pages, **RGBA, 8-bit alpha, top row first** | §1.3 |

Total W1A: 0x1c + 75·20 + 3·256·256·4 = 787 960 ✔. No per-glyph atlas: 3 pages of 256×256.

Glyph record (20 bytes):

| offset | type | meaning | evidence |
|---|---|---|---|
| +0x00 | f32 | `w` glyph width (= `+0x0c`) | `0x441970` |
| +0x04 | f32 | `adv` pen advance (always `w − 6` = `w + 2M`; the margins overlap) | `0x441950` |
| +0x08 | u16 | `x` in the page | `0x43f94e` |
| +0x0a | u16 | `y` in the page (from top) | `0x43f944` |
| +0x0c | u16 | `w` | `0x43f93b` |
| +0x0e | u16 | `h` (always 62) | `0x43f933` |
| +0x10 | u16 | page index | `0x43f926` |
| +0x12 | u16 | garbage (0x97; the only difference between the level and hub fonts) | not read |

**No kerning, no glyph pairs.** There are three font files in circulation: all A/B/C/D/R levels + Blackbox
(md5 `082e7cd3…`), House/hubs/Lang (`eb802741…`, identical to byte `+0x12` of every record onward) and Credits
(84 glyphs, 788 140 bytes: the same first 75 + 9 extra). A single glyph table thus suffices for the whole game.

### 1.2 Font object in memory (100 = 0x64 bytes, ctor `0x43f640` → base `0x441b80`)

| offset | type | init | meaning |
|---|---|---|---|
| +0x00 | vtable | `0x4aa3ec` | `[0]` = recompute scales `0x43f850`; `[1]` = SetMode `0x43f700`; `[2]` = digit string `0x43f880` |
| +0x04..+0x1c | | | the 0x1c-byte header from the file |
| +0x20 | ptr | | glyph records |
| +0x28 | f32 | 0 | extra line spacing (never set anywhere) |
| +0x2c/+0x30 | f32 | 1024/768 | virtual screen size `VW,VH` (`0x441a90`) |
| +0x34/+0x38 | f32 | 640/480 | output size in pixels (`0x441ad0`) |
| +0x3c/+0x40 | f32 | 0 | offset before scaling (`0x441af0`) |
| +0x44/+0x48 | f32 | 0 | offset after scaling (`0x441b10`) |
| +0x4c | f32 | 20 | font size `S` (`SetSize 0x441a60`); 30 after loading (`0x43fa70`) |
| +0x50/+0x54 | f32 | | `sx = out.w/VW`, `sy = out.h/VH` (`0x43f850`) |
| +0x58 | f32 | | `k = S / (H − B)` = S/40: glyph scale in virtual units |
| +0x5c | ptr | | array of `pages` surfaces (0x74 B each) |
| +0x60 | u32 | `0x80ffffff` | draw colour ARGB (§5.2); the caller sets it before every piece of text |

`SetMode(3)` (`0x43f7e6`, called after loading `0x43fa69`): `VW,VH = 640,480`, output = real
screen resolution `[0x5e8650]+4/+8`, offsets 0. **All text coordinates are thus 640×480 virtual, top-left =
(0,0), y down**, and scale with the resolution. (Modes 0/1/2 = pixels / y mirrored / x mirrored are
not used by the game; the HUD sets the same values as mode 3 again explicitly, `0x447126..0x447157`.)

Derived sizes (virtual units), with `k = S/40`:

| function | formula | S = 17 (HUD) | S = 23 |
|---|---|---|---|
| `0x441980` cell height (= height of every drawn glyph and line spacing in the text box) | `(2|M| + extra + H)·k` = 62k | 26.35 | 35.65 |
| `0x441960` line skip on code 4000 | `(extra + H)·k` = 56k | 23.8 | 32.2 |
| `0x441950` advance | `adv·k` | | |
| `0x441970` glyph width | `w·k` | | |

### 1.3 Pixel format, colour, alpha

Pages: 4 bytes per pixel, **top row first**, file order **R, G, B, A** (`0x4806a6..0x480713`:
`byte0<<16 | byte1<<8 | byte2 | byte3<<24` = D3D-ARGB) — unlike the images (B, G, R, A, §5.1). In the data RGB is white everywhere and the
shape is entirely carried by **A** (29–36 distinct alpha values per page = anti-aliasing + soft edge). Port: load as RGBA8, draw with
`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, multiply by the text colour.

For the font, the R/B order makes no difference (RGB = white). The engine converts to 16-bit (ARGB4444) unless the device reports 32-bit
textures (`device+0x1c`, `0x480584`); a port just uses 8888.

### 1.4 Strings and special codes

A string item is a row of **u16 codes, terminated with 0** (the 0 is part of the payload). The loader `0x43f440`
copies the payload verbatim into a string pool; `RckGet` (`0x43fb30` → `0x441580`) returns the pointer.

Classes (`0x441870`): `0` = end; `1..2000` = glyph; `2001..3000` and `3001..3999` = reserved classes
(looked up as a glyph but fall outside `n` → skipped); **`4000` (0xfa0) = new line**
(`0x4419b5`: `x = 0` — truly zero, not the starting x — and `y += 56k`); `≥ 4001` = ignored. In the shipped
banks 4000 does **not** occur: multi-line text is always split into multiple strings.

**Glyph = code − 1** (`0x4418dd`: record = `glyphs + code·20 − 20`). Note: RCK.md called this "indices 0..75";
these are codes 1..75, 0 is the terminator. There is no space exception: space is just glyph 18 (empty, adv 13).

Code → character (derived by rendering the font, `out/hud/font_chart.png`; the order follows first
occurrence in the English string table, so it differs per language build):

```
 1-10  0 1 2 3 4 5 6 7 8 9        31 +    41 K    51 w    61 M    71 ,
11 Q   16 r   21 s   26 N         32 L    42 B    52 m    62 f    72 $
12 u   17 e   22 ?   27 %         33 E    43 H    53 d    63 .    73 z
13 i   18 ' ' 23 C   28 =         34 R    44 I    54 l    64 h    74 J
14 t   19 y   24 n   29 /         35 D    45 G    55 c    65 (    75 x
15 A   20 o   25 Y   30 :         36 !    46 F    56 v    66 )    --- Credits font only: ---
                                  37 S    47 a    57 W    67 ®    76 &   77 -   78 é   79 ô   80 j
                                  38 U    48 g    58 p    68 q    81 ç   82 "   83 Z   84 °
                                  39 T    49 P    59 V    69 '
                                  40 O    50 k    60 X    70 b
```
Missing from the regular font: `j`, `Z`, `-`, `&`, `"`. As a C table: see `GLYPHS` in tools/fontrender.py.

**Numbers** are built with `0x441820(font, value, u16 *buf)` → `0x4417d0` (recursive, base 10): each digit
is `digits[value % 10]` with `digits` = **Common string 0** (`0x20000`, `"0123456789"`, via vtable[2] `0x43f880` →
`0x43f520`). Negative values become 0 (`0x441828`). Helpers: `0x441730` strcpy16, `0x441770` strcat16,
`0x441710` strlen16. Digits are thus **font glyphs, not sprites**.

### 1.5 Languages

There is **one language per installation**. Every bank contains exactly one string table (Common: 135 strings, identical in
Woody/Knothead/Splinter.rck; level: 0–1; Credits: 252) and the font is generated per language (glyph order =
first occurrence in the English text: "0123456789", "Quit", "Are you sure?", …). There is no per-language index offset
in the exe: `RckGet` indexes directly (`0x441580`). The level `Lang` (`\Data\Lang\Lang.gel`, string
`0x4b1314`) has 6 images (flags) and no strings; it presumably selects which set of banks the
installer/launcher uses — **uncertain, not followed**; in this (English) build there is nothing to choose.

The full Common table is in §8.

---------------------------------------------------------------------------------------------------

## 2. Drawing text

### 2.1 `Font::Draw` = `0x43f890(font, float *x, float *y, const u16 *str)` (thiscall, `ret 0xc`)

No scale, colour, alignment or wrap arguments: size via `SetSize 0x441a60(S)`, colour via `font+0x60`,
alignment is done by the caller using `Measure`. `*x`/`*y` are updated (pen).

```c
void Font_Draw(Font *f, float *x, float *y, const u16 *s) {
    for (; *s; ) {
        float x0 = *x;
        Glyph *g = Font_Advance(f, *s++, x, y);          /* 0x4419a0: *x += adv*k, or newline; NULL = draw nothing */
        if (!g) continue;
        float h = Font_CellH(f);                          /* 0x441980 = 62k */
        float w = g->w * f->k;                            /* 0x441970 */
        float px = x0, py = *y;                           /* top-left of the cell */
        Font_ToScreen(f, &px, &py, 1);                    /* 0x441900: (p + off1)*s + off2 → pixels */
        Font_ToScreen(f, &w,  &h,  0);                    /*           *s only */
        Quad2D_Pixels(renderer, ftol(px), ftol(py), ftol(w), ftol(h),          /* 0x481040, §5.3 */
                      g->x, page->h - g->y, g->wpx, -g->hpx, f->colour, f->page[g->page], 8 /*alpha blend*/);
    }
}
```
`y` is the **top** of the 62k-tall cell, not the baseline. The source y is passed mirrored
(`pageH − y`, height negative) because `0x481040` computes v = 1 − y/H; net effect: file row `y` ends up at the top.
`ftol` (`0x499580`) **always truncates**: it saves the control word, sets RC = 11 (`or ah, 0xc`), `fistp`s and restores it
(`0x499587..0x49959c`). The control word the game runs with is `0x007F` (24-bit precision, round to nearest-even), verified
live with `tools/wverify.py --probe fpu` (CAMERA.md §3.6); only inline `fistp`s round to nearest-even.

### 2.2 `Font::Measure` = `0x441b30(font, const u16 *str, float *w, float *h)`

Sets `*w = *h = 0` and calls `0x4419a0` per code: `*w` = sum of the advances (restarts from 0 after a 4000,
so the width of the **last** line), `*h` = number of newlines × 56k. The caller must get the height of a single
line from `0x441980` itself.

### 2.3 Centering

* Number in a HUD circle: `0x4605b0(cx, cy, value, font, &x, &y)`: `x = cx − w/2`, `y = cy − cell/2`
  (`0x4605fc..0x460612`). `0x448660(anchor, value, font, &x, &y)` gets `(cx, cy)` from the anchor table
  `0x5d7b50` and shrinks the font to `17 × 0.75` at `value ≥ 100` (`0x448676`, `0x4aabb4`).
* Text box: per line `x = 320 − w/2` (§3). There is **no automatic line wrapping** and no max width;
  the text box shrinks the font instead.

---------------------------------------------------------------------------------------------------

## 3. Message 1080: text box

Handler: game handler `0x444870` → `0x44511f` → `0x456ed0(box = game+0x18 object, record)`; every frame
`0x4571c0(box, dt)` from the end of the game update `0x4459c0` (`0x445bb5`). Confirms and refines
CINEMATIC.md §8.

### 3.1 Arguments

`SEND 7: 1080, hAlign, vAlign, var, id1, id2, id3` (record: `+8` hAlign, `+0xc` vAlign, `+0x10` var,
`+0x14..` ids; the loop stops at the first `−1` or after 3 lines, `0x456f07`).

| arg | meaning |
|---|---|
| hAlign | 0 = centred `x = 320 − w/2`; 1 = left `x = 16`; 2 = right `x = 640 − w − 16` (`0x45707b..0x4570bf`) |
| vAlign | 0 = middle `y0 = 240 − tot/2`; 1 = top `y0 = 16`; 2 = bottom `y0 = 480 − tot − 16` (`0x456fe5..0x45702b`), `tot = lines × cell` |
| var | script variable (`0x02000000 | n`, the engine masks `& 0xffffff`, `0x456ef5`): **close flag**, the engine only reads it (`0x457245` → `0x443cd0`) |
| id1..3 | string refs. In all 53 calls across the 28 levels: bank 0 (`0x0002xxxx` = `131072 + n`). `0x20001` (empty string) = **spacer line**: the background only starts below that line (`0x457105`) |

All 53 calls use `hAlign 0, vAlign 2`. Examples: House intro `[0,2,var4, 0x20001, 107, 108]`
("If you ever want to see…" / "I want $1,000,000!"); WWS doors `[0,2,var22, 0x20001, 47 "Space", 51 "Part A"]`;
W1A `[0,2,var54, 0x20001, 122, 123]` (tutorial); KWS/SWS `[0,2,var24, 113, 114, 115]` (three lines, no
spacer line).

### 3.2 Layout (`0x456ed0`)

```c
box->font = RckGet(0x01030000);                   /* font 0 of the level bank */
box->size = 25;
for (each line)                                   /* 0x456f3b */
    for (;;) {
        Font_SetSize(font, box->size);
        Measure(line, &w, &h);
        if (640 > w) break;                       /* fits (compared against the full 640, no margin) */
        box->size -= 1;
        if (box->size < 17) break;                /* 0x4ab2b8 */
    }
box->size -= 2;                                   /* 0x456fa7 — BUT the font is still set to the last measured size */
cel = Font_CellH(font);  tot = n * cel;           /* so layout at (size+2), drawing at size: the text ends up ~8 % smaller than the box */
y = y0(vAlign, tot);  top = y;
for (each line) { Measure; x = x(hAlign, w); minx = min(minx, x); maxx = max(maxx, x + w);
                  line.x = x; line.y = y;  y += cel;  if (line.id == 0x20001) top = y; }
box->rect = { left = minx − 16, top = top − 16, right = maxx + 16, bottom = y + 16 };   /* ints, 0x457158.. */
box->t = 0; box->state = 1;
```
A new 1080 while a box is still showing simply overwrites it (no queue).

### 3.3 Per frame (`0x4571c0`)

| state | behaviour |
|---|---|
| 1 | fade-in **0.5 s**: alpha = `ftol(2t · 254)`; at `t ≥ 0.5` → state 2 |
| 2 | colour `0xfeffffff`; **stays until `*var != 0`** → state 3, `t = 0`. No timer, no key |
| 3 | fade-out 0.5 s: alpha = `(1 − 2t) · 254`; at `t ≥ 0.5` → state 0 |
| 0 | nothing |

Drawing: first the background `RectVirtual(left, top, right−left, bottom−top)` (`0x480a10`, blank texture
`[0x5e8684]`, flag 8) with all four corners `(colour >> 1) & 0x7f000000` = **black at half the text alpha**
(max 0x7f ≈ 50 %); then the lines with `Font_Draw`, colour white × alpha.

**The game does not pause**, the player stays in control, there is no key to dismiss it. The script controls the
lifetime: `var = 0` → `SEND 1080` → later `var = 1` (House: fixed times T+300…; hubs: when leaving the
volume). The script doesn't "wait" on the variable; it sets it.

### 3.4 Related messages

A scan of all `SEND`s across the 28 levels (tools/ekodisasm.py) finds **no other message with a string ref**.
1081–1085 (level flow), 1088 (side camera), 1090 (particles) are in MESSAGES.md. 1086, 1087, 1089, 1091–1099
do not occur in any script. Message **1172** does belong to the HUD: see §4.5.

---------------------------------------------------------------------------------------------------

## 4. The HUD

Object: 0x54 bytes, ctor `0x4470d0`, `[0x5d7b44]`, per level (`app+0x30`); font `[0x5d7b48]` = `0x01030000`
(level bank), animator object 0x88 B at `hud+0x30` (ctor `0x460620`). Drawing: `0x447210` from the main loop
(`0x401e55`). All HUD images come from **bank 0 = `Common\<character>.rck`, images 61..64**
(128×128, with alpha; `out/rck/Woody/image_061…064`, overview `out/hud/hud_atlas_61_64.png`).

### 4.1 When

`0x401e19..0x401e55`: `draw = !cinematic_playing (0x44f2e0)`; and if `app+0x70 == 0` also
`camMgr+0x138 != 2` (fall-death camera). If `app+0x70 != 0` (message 1172 this frame) the HUD is always drawn
and `0x4484a0` is called first (§4.5). `app+0x70` is cleared every frame (`0x401e60`).
`hud+0` (state, `0x448450`): 0 = game, 1 = pause menu page 0x18/0x19 (extended HUD), 2 = hidden
(other menus and app state 3 = BlackBox; `0x40165c`, `0x404eba`). In state 2 `0x447210` draws nothing.

The base HUD stays **permanently** on screen (no sliding in/out); only the $ and charge counters
(§4.4 rows 9–12) are temporary.

### 4.2 Sprites: table `0x4ab658` (20 bytes: `f32 x, y, w, h; u32 ref`), drawer `0x460480`

`0x460480(sprite, float x, float y, float scale, flags)` → `RectVirtual(x, y, w·scale, h·scale, source x,y,w,h,
4× 0xfe808080, surface = [0x5e866c] + (ref & 0xffff)·0x74, flags)`. Source coordinates count **from top-left**
in the upright image (the way rckexport writes them out).

| # | source (x,y,w,h) | img | what |
|---|---|---|---|
| 0 | 0,0,64,64 | 61 | face character 0 |
| 1 | 63,0,64,64 | 61 | face character 1 |
| 2 | 0,63,64,64 | 61 | face character 2 |
| 3 | 63,63,64,64 | 61 | green **$** (unique item, type 36) |
| 4 | 0,0,94,94 | 62 | big **W** (bonus type 34) |
| 5 | 0,0,94,94 | 63 | finish flag (race bonus type 37) |
| 6 | 0,0,63,90 | 64 | silhouette + star (charge, type 35) |
| 7 | 0,90,23,23 | 64 | **W ball** = 1 health |
| 8 | 27,90,34,34 | 64 | white circle with black outline (number plate) |
| 9 | 97,103,29,23 | 64 | red box with black hole (empty health slot) |
| 10 | 5,116,123,12 | 62 | power meter: orange fill with "MAX" |
| 11 | 5,94,123,22 | 62 | power meter: dark bar/glow with "MAX" |
| 12 | 63,0,64,64 | 64 | face of Buzz Buzzard (boss) |
| 13 | 111,111,16,16 | 63 | blue **B** ball (boss health) |
| 14 | 97,84,19,15 | 64 | small red box (boss slot) |
| 15 | 0,96,31,31 | 63 | (appears empty in the data; **uncertain**) |

`hud+8` (which face) comes from the Perso child `(perso+0x104 >> 5) & 0x1f` (jump table `0x44af78`):
child 1 → 0, child 2 and 4 → 1, child 3 and 5 → 2 (`0x44af58`, `0x44af70`, `0x44af64`). The pictures are Woody
(top-left), Knothead (top-right), Splinter (bottom-left); that child 2/4 = Knothead and 3/5 = Splinter is
inferred from the order of the pictures and strings 30–32, not from the Perso code (**uncertain**).

### 4.3 Fixed positions (640×480)

Slots `0x4b3a10` (pairs x,y; `0x448620(sprite, slot)` draws at scale 1, flag 8):

| slot | x,y | slot | x,y |
|---|---|---|---|
| 0 | 16,16 | 4 | 16,221 |
| 1 | 16,66 | 5 | 50,266 |
| 2 | 16,136 | 6 | 544,30 |
| 3 | 50,181 | 7 | 584,70 |

Number anchors `0x5d7b50` (init `0x447000`): centre of the circle (sprite 8, 34×34) in slot 1/3/5/7 =
`slot + 17 − 1`: **A0 = (32,82), A1 = (66,197), A2 = (66,282), A3 = (600,86)**.
Other (init `0x446f10`): health row `x = 559 − 29·i`, `y = 51`; bars `y = 51`, height 23; power meter
fill (0,426), bar (0,421); HUD font size **17** (`0x4b3a88`).

### 4.4 Elements (drawing order; `0x447260` bars → `0x447660` sprites+text → `0x447d70` meter → `0x4480d0` animations)

| # | element | what / where | value | when |
|---|---|---|---|---|
| 1 | blue bar, left | rect (0,51) 256×23, blank texture, left `0x800000ff` → right alpha 0 (`0x4472d8`) | | always |
| 2 | red bar, right, in-game | rect (384,51) 27×23 red alpha 0 → 19; then 5 slots sprite 9 at `x = 556 − 29i` (i = 1..5), 29×23, left/right alpha = `(x − 384)·128/175`; then rect (556,51) 84×23 `0x80ff0000` (`0x44737e..0x44764c`) | | `hud+4 == 0` |
| 2' | red bar, right, race | rect (384,51) 172×23 red alpha 0 → 0x80, + the same end rect; **no slots** (`0x4472f1`) | | `hud+4 == 1` |
| 3 | face | sprite `hud+8` in slot 6 (544,30) | | always |
| 4 | lives | sprite 8 in slot 7 (584,70) + number on A3, **red `0xfeff0000`**, S = 17 | `hud+0x20` = `perso+0x250 − 1` (min 0) | number hidden during life animations (`hud+0x39`, `+0x40`, `+0x35`) |
| 5 | bonus icon | game: sprite 4 (W) in slot 0 (16,16); race: sprite 5 (flag) | | `hud+0xc == 0` and no $ animation (`+0x3b/+0x3c`) |
| 6 | bonus circle + number | sprite 8 in slot 1 (16,66), number on A0 red | game: `hud+0x14` = `perso+0x25c` (0..24, 25 = heart); race: `hud+0x24` = `perso+0x264` | number hidden during its own pickup animation (`+0x36` / `+0x37`) and during `+0x34/+0x35` |
| 7 | "collected / total" | text white `0xfe808080`, S = 17, `x = 94 + 16 = 110`, `y = 51 + 12 − cel/2` (vertically centred on the bar); string = num + Common string 9 "/" + num | game: `[0x5e54e8] / [0x5e54e4]`; race: `hud+0x24 / [0x5e54f4]` | game: not on level indices 1, 11, 18 (`0x4479bb`) |
| 8 | health | sprite 7 at `(559 − 29·i, 51)` for `i = 1..ftol(hud+0x28)`; if `hud+0x34` (a new heart flying in) the last one is skipped | `hud+0x28` = `perso+0x24c` (float, max 5) | only `hud+4 == 0` |
| 9 | $ icon | sprite 3 in slot 2 (16,136) | | `hud+0 == 1` (pause) or `hud+0xc > 0` (1172), not during slide animations |
| 10 | $ number | sprite 8 in slot 3 (50,181) + number on A1 red | `hud+0x18` = `perso+0x260` (type 36) | same |
| 11 | charge icon | sprite 6 in slot 4 (16,221) | | only `hud+0 == 1` (pause) |
| 12 | charge number | sprite 8 in slot 5 (50,266) + number on A2 red | `hud+0x1c` = `perso+0x254` (type 35) | same |
| 13 | power meter | sprite 10 at (0,426) and sprite 11 at (0,421), **both clipped to width `f·104`** (source and destination); at `f ≥ 1`: sprite 10 full 123 wide and sprite 11 blinks (0.15 s off / 0.15 s on, timer `hud+0x44`) | `f = hud+0x2c = perso+0x5e0 × 2/3` (charge time, max 1.5 s, `0x4574e7`) | always drawn, invisible at f = 0 |

Numbers ≥ 100 are drawn at S = 12.75 (§2.3). The data come every frame from `0x44ae60` (Perso → HUD);
`hud+4` = 1 if `perso+0x21c == 1` or `perso+0x4d8` (race/surf levels).

**Race timer/position**: there is none in `0x447660`; the race HUD is only the flag + "n / total". A timer
("Seconds", string 128) only exists on the results screen (§6). **Boss health**: not in `0x447660` but in its
own object (`animator+0x44`, drawer `0x47b0b0(cur, max)`
via `0x462450`), active while `hud+0x48`; the boss classes set it every frame with `0x4484d0(on, cur, max)`
(`0x40cbf1`, `0x40dd80`, `0x40fd82`; start `0x462470` → `0x47aca0`). Bottom-right, same layout as the
health bar: base `X = 559 − 3 = 556` (`0x47acaa`), row y = 424 (`0x4b3a84`, `0x47ac03`);
`max` slots sprite 14 (19×15) at `x = X − 19·i − 2` with increasing alpha (colour `a<<24 | 0x808080`, `0x47adb7..`),
end rect `0x80ff0000` from `X − 2` to 640 (`0x47ae6e`), `cur` B balls sprite 13 (16×16) at `x = X − 19·i`
(`0x47b043`), and the Buzz face sprite 12 at (559, 400) (`0x47aee0`). The bar first slides in from the right
(timer `+0x20` up to duration `+0x24`, `0x47b0ee`); the precise y of the balls relative to the slots is
**uncertain**. Ctor `0x47abe0`: duration `+0x24` = **2.0 s**, y `+4` = `[0x4b3a84]`, alpha ramp `+0x14` = 30 / `+0x1c` = 98 ⇒ slot i has left alpha
`(x_i − X + W)·98/W + 30` (W = 19·max), so from 30 to 128 across the row. During the slide-in (`0x47ad10(max, x)`) both the slots and the end rect
(`x − 2`, width `640 − X`, height 15) slide together from `X + W + 640 − X + 2` to X; then `0x47bff0` grows the face (sprite 12) over **0.2 s**
(`0x47bf70`: `+0x20 = 0.2`) and only once that is done do the face (`0x47aee0`) and balls (`0x47afb0`) follow. Ported as `hud_boss_bar`.

### 4.5 Message 1172 and the temporary counters

`1172` sets `app+0x70 = 1` for one frame → `0x4484a0`: if `hud+0xc == 0` and no slide-out animation is running,
start the slide-in animation (`0x4618d0`, flag `+0x3b`); `hud+0xc = 2` (details and port: §4.6). `0x447210` counts `hud+0xc` down every frame; as soon as
it hits 0 the slide-out animation starts (`0x461a70`, flag `+0x3c`). The script must therefore send 1172 **every frame**
as long as the $ counter should be visible (hub: near the Jackpot door). While `hud+0xc > 0` the
bonus icon (rows 5–7) disappears and the $ counter takes its place.

### 4.6 Animations (animator `hud+0x30`, 0x88 B, ctor `0x460620`, driven from `0x4480d0`)

Every flag `hud+0x34..0x41` belongs to one running animation; the associated function returns 1 as long as it
runs and `0x4480d0` writes that return value back into the flag. `0x447660` (the static HUD) runs **before**
`0x4480d0`, so the animations draw over it; the static HUD omits whatever an animation has taken over.

**Everything is linear over time and nothing ever fades.** All four corner colours are `0xfe808080` every frame. A
"pop" is a font-size change, a flying-in icon "appears" by growing from zero, and a trail particle
disappears by shrinking to zero (additive blended).

| flag | start | per frame | what |
|---|---|---|---|
| +0x34 | `0x448380` if the bonus counter drops and health < 5: `0x460bc0` | `0x460bb0` | the W swarm to the new heart |
| +0x35 | same at health = 5 (→ extra life): `0x460cc0` | `0x460be0` | the W swarm to the portrait, then the lives pop |
| +0x36 | pickup type 34 (`0x448510` child 4 → `0x4616a0`) | `0x4611b0` | 3D position → bonus icon (sprite 4), trail mode 0 |
| +0x37 | type 37 → `0x461760` | `0x461270` | same with the finish flag (sprite 5), same slot |
| +0x38 | type 36 → `0x461420` | `0x460db0` | same with the $ (sprite 3) to slot 2, plus the entire $ counter |
| +0x39 | type 30 → `0x461300` | `0x460d20` | same with the **player's own face** (sprite `hud+8`) to slot 6, trail mode 1 |
| +0x3a | type 35 → `0x461560` | `0x460fb0` | same with the silhouette (sprite 6) to slot 4, plus the charge counter |
| +0x3b/+0x3c | §4.5 | `0x461820` / `0x461a00` | $ counter in/out |
| +0x3d/+0x3e | `0x448450(1)` / back | `0x461b60` / `0x461da0` | extended HUD (pause) in/out |
| +0x3f/+0x40/+0x41 | value dropped ($ `0x462330`, lives `0x4622e0`, charge `0x462380`) | `0x461f70` / `0x461f00` / `0x462020` | "minus 1" animation |

#### The flying-in icon (`PickupFly`, 0x50 B; ctor `0x47b170`, start `0x47b230`, tick `0x47b4c0`)

Five instances at `animator+0x30..+0x40`, one per pickup kind. `0x47b230` is **not just a projection helper**: it is the
Start of this class, with the projection written into it. Duration `T = 0.2 s` (fixed in the ctor).

```
(sx,sy) = project(world position)         ; virtual 640x480; outside the four side planes -> Start returns 0
dx = tx + spriteW/2 - sx                  ; tx,ty = the fixed slot from 0x4b3a10
dy = ty + spriteH/2 - sy
u  = t / 0.2
x(t) = sx + u*dx ;  y(t) = sy + u*dy      ; LINEAR, no easing
w(t) = u*spriteW ;  h(t) = u*spriteH      ; the icon grows from nothing to full size
draw rect (x - w/2, y - h/2, w, h), colour 0xfe808080, blend flag 8
last frame: exactly (tx, ty, spriteW, spriteH) = what 0x448620 would draw -> seamless transition
```

The projection (`0x47b230` itself) works against `[0x5e86ac]+0x114` (3 rows of 4 floats, stride 0x10, translation at
+0x144) and then `x = (vp.cx + clipX/w · 0.5 · (vpW−2)) · 640/screenW`, likewise y with 480/screenH. **No clamping**:
if the point falls outside the four side planes, the flight does not start and only the number pop remains.

If the flight is running, the associated tick draws the number from **before** the pickup at its anchor (size 17), so
the counter only jumps once the icon has landed.

#### The trail (`0x47b710`, dispatcher `0x47b7b0`)

The trail samples the same path at a fixed step in *animation time* (`0.02 s` for mode 0, `0.005 s` for
mode 1) and puts the samples into the same particle pool as the pickup effects (`[0x5e823c]+0xdb8`, §BONUS.md 2.4).
Both modes draw with **bank 0 image 4** (64×64 soft glow), colour `0xfe808080`, **additive (flag 4)**,
and never fade: they shrink.

- **Mode 0** (`0x47b800`, callback `0x47bba0`) — types 34, 36, 37. Lifetime 0.3 s. The sample is scattered
  sideways (`± (rndtab·10 + w/4)` in x and y, sign follows the travel direction), gets size `k·w/4` with
  `k = 1 + tab[rnd]` and drifts at `0.2 ×` the icon's speed, and shrinks linearly to zero.
- **Mode 1** (`0x47ba10`, callback `0x47bca0`) — types 30 and 35. Lifetime 0.5 s, **two** quads per particle per
  frame. The sample crawls at a fifth of the icon's speed along the flight line
  (`S/L = tau + pt`, `tau = 5·u`) and deviates perpendicular to it by
  `W = sin(2π·⌊1800·S/L⌋/512) · spriteW/2 · S/L`: **a double helix of 1800/512 ≈ 3.5 turns** that linearly
  opens up to half an icon width right at the HUD. `0x47bf00` is not a drawer but a math helper (7 arguments,
  three floats via output pointers).

#### The number pop (`NumPopup`, 0x24 B; start `0x47c390`, tick `0x47c3d0`)

`Start(x, y, nPhases, s0, s1, duration)`; an odd phase interpolates `s0 → s1`, an even phase `s1 → s0`, linear, one frame
delayed. Colour always red `0xfeff0000`, size ×0.75 at a value ≥ 100. Every pickup starts
`(anchor, 2, 17.0, 37.0, 0.2)` = 0.4 s total. Anchors: child 4/5 → A0, child 2 → A1, child 3 → A2, child 1 → A3.

#### The W swarm (`WSwarm`, 0x20 B; ctor `0x47c4e0`, start `0x47c5b0`, tick `0x47c620`, particle `0x47c7c0`)

`0x460bc0` (heart) and `0x460cc0` (extra life) drive **the same** object `animator+0x04`; `0x460cc0` additionally
sets `animator+0x74` and starts the lives pop `(A3, 2, 17.0, 37.0, 0.2)` for phase 2.

| property | value |
|---|---|
| sprite | 4 (big W, 94×94), drawn as a square |
| source | (16, 16) = slot 0, the bonus icon |
| target, heart | `(559 − 30·(health_old+1), 51.5)` — note: 30, while the health row sits at 29 |
| target, extra life | `(564.5, 51.5)`, a 23-wide square centred on the **portrait** (not on the life circle) |
| duration per trip | 0.4 s, linear: `x = 16 + relX·u`, `y = 16 + 35.5·u`, `size = 94 − 71·u` |
| particles | at most 3, staggered: the next one leaves once the front one is `relX/3` along |
| counter | starts at 25.0, **−2.5 per landing** ⇒ 10 landings; a landed particle restarts immediately |
| wind-down | once the counter < 3.0, freshly reset particles stay put |
| end | counter == 0.0 (exact float comparison) ⇒ ≈ 1.6 s total |

While the swarm runs, the bonus number counts down: at every `⌊counter⌋ % 5 == 0` a `(A0, 1 phase, 17.0 → 0.0, 1.0 s)` pop starts with
value `n − 5`, so **20, 15, 10, 5, 0** shrinking away. That pop is ticked **once per live particle**,
so in practice it runs ~3× as fast — that's how the code has it.

Order when picking up 25: the ordinary pickup flight (+0x36) goes first, because `hud+0x10` (set by
`0x448380`, cleared by `0x448510` child 4 and by the end of `0x4611b0`) blocks the tick of +0x34/+0x35.
While that latch is set, the circle shows `24` and only then does it pop to `25`.

#### What `0x447660` omits

- **Life number** (`0x44771e`): hidden if `f39 || f40 || (f35 && !f10)`; while `f35` runs, the built-up string
  is `hud+0x20 − 1` regardless.
- **Bonus** (`0x44792e`, `0x44794f`): the icon (sprite 4 in slot 0) and the circle (sprite 8 in slot 1) **stay**;
  only the number and the "collected / total" line drop out at `f34 || f35`, and the number alone at `f36`.
- **Health row** (`0x447ad5`): `if (f34 && i == ftol(health)) continue;` — the incoming heart is
  skipped until the Ws have landed. `f35` does not touch the row.
- **$ and charge**: outside the pause page `0x447660` doesn't even reach those rows (`0x447b18`:
  `cmp dword ptr [esi], 1 / jne`), so in normal gameplay `0x460db0` / `0x460fb0` draw them themselves — which is why
  those ticks call `0x448620`.

#### $ and charge counter on pickup (child 2 and 3)

A four-stage sequence, ≈ 2.5 s: (1) 0.2 s flight together with the circle growing from 0×0 to 34×34, (2) number pop
0.4 s, (3) 1.5 s hold, (4) circle shrinks to 0 while the icon slides to `x = −spriteW` (0.2 s).
The `IconSlide` object is started right at pickup but only ticked in stage 4.

#### The $ counter of message 1172 (+0x3b / +0x3c), the pause HUD (+0x3d / +0x3e), the "minus 1" of $ (+0x3f)

All of them work on the same animator objects as the pickup sequences: `+0x18` the slide of the W icon (sprite 4),
`+0x24` the plate under it (slot 1), `+0x1c` / `+0x28` / `+0x0c` the slide, plate and number pop of the $ (slots 2 / 3,
anchor A1), `+0x20` / `+0x2c` / `+0x10` the same for the charge (slots 4 / 5, A2). A plate `0x47bf90(x, y, w0, h0, w1,
h1)` takes the slot (the top left of the full 34×34 plate) and keeps its **centre** `(x + 17, y + 17)`; a slide
`0x47c1a0(x0, y0, x1, y1, sprite)` draws the sprite at full size along the line. Both run 0.2 s, linear. The step
flags `animator+0x76..0x7e` are the return values of those ticks.

| anim | start | tick | sequence |
|---|---|---|---|
| $ in (+0x3b) | `0x4618d0` | `0x461820(hud+0x18)` | the bonus plate shrinks under the W (W drawn at slot 0), then the W slides to x = −94; at the same time the $ slides in from x = −64, its plate grows, then the number pops (A1, 2 phases 17 → 37 → 17, 0.2 s); returns the pop's result, so it ends with the pop |
| $ out (+0x3c) | `0x461a70` | `0x461a00` | the W slides back from −94, then its plate grows; the $ plate shrinks (icon drawn), then the $ slides to −64; ends with the $ slide |
| pause in (+0x3d) | `0x461c40` | `0x461b60($, charges)` | $ and charge slide in together (from −64 / −63), both plates grow, both numbers pop; ends with the $ pop |
| pause out (+0x3e) | `0x461e10` | `0x461da0` | both plates shrink (no numbers), then both icons slide out; ends with the $ slide |
| $ minus (+0x3f) | `0x462330` | `0x461f70($)` | icon and plate appear at once (no slide), the OLD value ($ + 1) pops 17 → 37 → 17, then a 1-phase pop 17 → 0 in 0.2 s; `animator+0x54` = phase |

Driving (`0x4480d0`): `+0x3b` ticks only while `+0x3c` is clear; when it ends and `hud+0xc < 2` (1172 stopped),
`0x461a70` starts at once (`+0x3c = 1`). `+0x3c` ticks only while `+0x3b` is clear; when it ends and `hud+0xc == 2`
(1172 came back), `0x4618d0` starts again. `0x4484a0` (1172) starts `+0x3b` only when `hud+0xc == 0` and `+0x3c` is
clear, then `hud+0xc = 2`; `0x447232` counts `hud+0xc` down after every HUD frame and starts `+0x3c` when it reaches 0
and `+0x3b` is clear. So one frame without 1172 is enough to send the counter out. `+0x3d/+0x3e` come from the state
setter `0x448450`: 0 → 1 starts `0x461c40` (+0x3d = 1, +0x3e = 0), 1 → anything starts `0x461e10` (+0x3e = 1,
+0x3d = 0); on a race level (`hud+4 == 1`) the state changes without any animation. `+0x3f` is started by the $
setter `0x448340` whenever the value drops (not on the silent first set), ticked only while `+0x38` (the $ pickup)
is clear, and `+0x38` in turn only ticks while `+0x3f` is clear.

What `0x447660` draws meanwhile: with `hud+0xc == 0`, the bonus icon row (W, plate, number, "n / total") only when
neither `+0x3b` nor `+0x3c` runs (race: always the flag row, and nothing below it); on the pause page, $ and charge
only when neither `+0x3d` nor `+0x3e` runs. With `hud+0xc > 0` (1172): no bonus row at all, the health row, the $ row
unless `+0x3b`, `+0x3c` or `+0x3f` runs, the charge row only on the pause page (same test).

#### Port status

Ported in `src/hud.c` (`hud_anim_pickup`, `hud_anim_tick`, ticked from `hud_draw`) and `src/render_gl.c`
(`rnd_project`). The port itself watches the counters to recognize the reward, just like the setter `0x448380`.
The $ counter of 1172 (`dollar_in_*` / `dollar_out_*`, `hud_draw` does `0x4484a0` and the countdown), the pause HUD
(`hud_state` = `0x448450`, called by the main loop every frame with 0 / 1 / 2) and the "minus 1" of $
(`dollar_minus_*`) are ported, as are the "minus 1" of lives and charge. Test hook: `WOODY_DOLLAR="T0 T1"` sends
1172 every frame between T0 and T1. A port fix on the way: the plates used to be centred on the slot's top left
corner (17 units up and left of the static plate) instead of on the plate's centre (`0x47bf90`).
---------------------------------------------------------------------------------------------------

## 5. Images and the 2D blit

### 5.1 Image payload (loader `0x47f910` bank 0 / `0x47f9a0` bank 1 → `0x480780`)

`i16 w, i16 h, u16 bpp (24 or 32), u16 hasAlpha (0/1), u8[w·h·4]`. Pixel = bytes **B, G, R, A**
(`0x48092f..0x48099b`), **bottom row first** (the blit computes `v = 1 − y/H`, §5.3). `hasAlpha == 1` → texture with
alpha channel (ARGB4444 if the device can do that, `0x48082a`), A is a real 8-bit alpha (up to 256 levels in the data,
no magenta). `hasAlpha == 0` (bpp 24): the A byte is 0 in the file and **must be treated as opaque**.
The colour-key branch (`tex+0x44 & 1`, `0x4809a5`) is always off for rck images (`0x47f926`).
Surfaces: array of 0x74-byte objects, bank 0 `[0x5e866c]` (count `[0x5e8668]`), bank 1 `[0x5e8674]`;
`+0x38/+0x3c` = w/h.

### 5.2 Colour convention

Vertex colour = D3D-ARGB. **RGB: 0x80 = 1.0** (PS2 legacy): if the device has no MODULATE2X
(`device+0x20 == 0`) the blit doubles RGB in software with saturation (`0x480a72..0x480b2e`, `0x48106b`);
otherwise COLOROP is set to MODULATE2X (`0x429740`, LIGHTING.md). **Alpha: plain 0..255**, the game uses 0xfe as
"full" and 0x80 as half. `0xfe808080` = opaque white; `0xfeff0000` = red; the font default `0x80ffffff` = white, half.
Port: `rgb = min(1, 2·c/255)`, `a = c/255`.

### 5.3 The two blits (renderer `[0x5e86ac]`)

* **`0x480a10` RectVirtual**`(x, y, w, h, sx, sy, sw, sh, c0, c1, c2, c3, surface, flags)` — ints, `ret 0x38`.
  Destination in **640×480 virtual** (× `W/640` `0x4ab3d4`, × `H/480` `0x4abda0`): scales with the resolution.
  Corner colours: **c0 = top-left, c1 = bottom-left, c2 = bottom-right, c3 = top-right** (vertex fill
  `0x480efc..0x48100e`). `surface == 0` → blank white texture `[0x5e8684]`. Skipped if all four alphas are < 2.
* **`0x481040` QuadPixels**`(x, y, w, h, sx, sy, sw, sh, colour, surface, flags)` — destination in **real pixels**, single
  colour, `ret 0x2c`; only used by `Font_Draw`.

Both: `u = (sx + 0.5)/texW`, `v = 1 − (sy + 0.5)/texH` (half-texel offset, `0x4a9014`), software clipping against
the screen with UV correction, `z = 1/65536`, `rhw = 1`, 6 vertices in a batch per (texture, mode).
`flags & 8` → mode 2 = **SRCALPHA / INVSRCALPHA**, z-write off; `flags & 4` → mode 3 = **additive ONE/ONE**;
otherwise mode 0 = opaque (list `+0x1c0`). Evidence: lists `renderer+0x1c8` / `+0x1cc` are sorted in `0x428d00` into 256
depth buckets and drawn in `0x428ee0` with states 5/6 resp. 2/2 (`0x4290bf`, `0x429182`). 2D quads
have depth 0 → last bucket → **after all 3D transparency, in call order**. Everything in the HUD/text uses
flag 8. Texture filter: not read (**uncertain**; the soft alpha edges call for bilinear).

### 5.4 Draw order of a frame

1. 3D world (the letterbox is a **viewport scale**, not black quads: CAMERA_SCRIPT.md §2.4; the 2D layer
   stays fully 640×480 and the text box with vAlign 2 falls in the bottom black band).
2. HUD `0x447210` (`0x401e55`).
3. Text box `0x4571c0` (end of `0x4459c0`, `0x401e9a`) → drawn over the HUD.
4. Pause: frozen game frame + HUD, then a fullscreen `0x80000000` (50 % black, `0x404f53`), then the menu `0x446440`.
5. Right on top: the 4 fade objects `app+0x9c..` (`0x445bf0`, `0x4016e4`): black 640×480 rect, alpha
   `254·t/duration` (or reversed).

---------------------------------------------------------------------------------------------------

## 6. Results screen, pause menu, mask.bin (brief)

* **Menu object** `app+0x3c` (0x94 B, ctor `0x445e30`, vtable around `0x4ab600`; pages via `0x446490`, GAMEFLOW.md
  §6). It draws with the same `Font_Draw`/`RectVirtual`; text calls in `0x4468cf`, `0x45bca8..0x462258`.
  Pause = page 0x18/0x19: strings 4 "Continue", 36 "Options", 2 "Quit", 3 "Are you sure?", 5/6 "Yes"/"No";
  HUD in state 1 (all counters visible, §4.4 rows 9–12) under a 50 % black layer. Layout **not worked out**.
* **Results** (after EndLevel): strings 12 "CLEARED!!", 13 "RESULTS", 14 "TOTAL", 15 "OK", 17 "HIGH SCORE",
  46 "points", 127 "Level", 128 "Seconds", 129 "Final Score", 130 "Total Score :", and the characters 7 "%", 8 "=",
  10 ":", 11 "+". Worked out in **RESULTS.md** (page 0x1e, vtable `0x4aa934`, drawing `0x454610`) and ported as:
  no panel and no "OK", but an iris wipe with lines adding up on the left; of the strings above, the
  original only uses 12, 13, 14, 17 and 8/9/10/11 (plus level names 47..55). 15 "OK", 46, 127..130 and 7 "%" don't
  occur on this screen.
* **Save prompt**: page 6 (35 "Do you want to save?" / 5 "Yes" / 6 "No") with 8 "Game Saved" and 9 "Save failed."
  run as ordinary menu pages (page flag 2: half-black layer), all via `hud_menu_page` (y fraction 0.4).
* **`extract/Game/mask.bin`** (786 432 B): loaded by ctor `0x488790` (`0x4887f6`, path `\Game\mask.bin`)
  as **4 blocks of 0x30000 = 196 608 bytes** (`obj+0x20`, `+0x30020`, `+0x60020`, `+0x90020`; pointers at
  `obj+0xc0020..`). The same object holds 8 image refs from the **level bank** `0x01010000..7` and draws 256×256
  tiles (`0x4888db` ff.); `0x488610` tests positions against the rectangle 64..576 × 40..460. It thus belongs to
  the minigame **Blackbox** (72 images in Blackbox.rck), not to the HUD. Each block is **256×256 RGB** (3 bytes per pixel, only the
  first read): the collision map of the play area 65..576 × −15..496 (BLACKBOX.md §3). Not needed for the HUD/text port.

---------------------------------------------------------------------------------------------------

## 7. Recipe for the port (src/render_gl.c, src/main_engine.c; virtual 640×480)

1. **Banks**: load `Common/<character>.rck` images 61–64 and the strings; load the font from the level bank.
   Images: BGRA → RGBA, flip rows (bottom first), alpha = 255 when `hasAlpha == 0`. Font: 3 pages
   256×256 BGRA, don't flip. Textures: `GL_CLAMP_TO_EDGE`, `GL_LINEAR`, no mipmaps.
2. **2D pass** after the 3D scene: ortho 0..640 × 0..480 (y down), depth test off, `glBlendFunc(SRC_ALPHA,
   ONE_MINUS_SRC_ALPHA)`. One function `quad2d(x,y,w,h, sx,sy,sw,sh, c[4], tex)` with corner order top-left,
   bottom-left, bottom-right, top-right; colour `rgb = min(255, 2·rgb)`, alpha unchanged; `u = (sx+0.5)/W`, `v = (sy+0.5)/H`
   (in upright texture space). `tex = 0` → 1×1 white.
3. **Font**: `k = S/40`; cell = 62k; per code: 0 stop, 4000 → `x = 0; y += 56k`, 1..n → quad
   `(x, y, w·k, 62k)` from `(g.x, g.y, g.w, 62)` of page `g.page`, then `x += adv·k`. Since the port already
   draws in 640×480 virtual, the sx/sy step drops out. `measure()` = sum of `adv·k`.
4. **Numbers**: `itoa` → codes `digit + 1` (digits are code 1..10), or via string 0.
5. **Text box** (§3): struct `{state, t, size, var, n, line[3]{id,x,y}, rect}`; `msg_1080` does the layout
   (note: measure at `size`, draw at `size − 2`); every frame after the HUD: 0.5 s fade in, wait for
   `vars[var] != 0`, 0.5 s fade out; background black at alpha/2, margin 16; spacer line `0x20001`.
6a. **Menu pages**: `hud_menu_page(ids, n, yfrac, sel, dt)` draws the item list of the shared
   page class (TITLE §5.1): one size for the whole page (30, shrinking until everything fits), `y = yfrac·480`, per
   item a cell `62·S/40`, centred, and the selected item disappears while the blink phase < 0.25 s. Title
   (page 0/1), save prompt (6/8/9) and the results-screen OK all use it; the phase may only run
   once per frame, so anyone drawing two layers passes `dt = 0` to the lower one.
6. **HUD** (§4.3/4.4), in this order: blue bar, red bar + 5 slots (or race variant), face + lives,
   W/flag + circle + number + "n / total", health balls, (pause or 1172:) $ and charge, power meter. Numbers
   red `(255,0,0)`, S = 17 (≥ 100: 12.75), centred on the anchor. Skip the HUD during cinematics and the
   fall-death camera. Animations (§4.6) later.
7. **1172**: `hud.show_unique = 2` per receipt; counted down every frame.
8. **Order**: 3D → HUD → text box → (pause overlay + menu) → fades.

## 8. Common string table (English; index = low 16 bits of `0x0002xxxx`)

0 "0123456789" · 1 "" (spacer line) · 2 Quit · 3 Are you sure? · 4 Continue · 5 Yes · 6 No · 7 % · 8 = · 9 / ·
10 : · 11 + · 12 CLEARED!! · 13 RESULTS · 14 TOTAL · 15 OK · 16 BACK · 17 HIGH SCORE · 18 FREE · 19 Start again ·
20 Press start · 21 Press a key · 22 New game · 23 Load game · 24 Select save · 25 Select game · 26–29 Save 1..4 ·
30 WOODY · 31 KNOTHEAD · 32 SPLINTER · 33 BONUS · 34 ??? · 35 Do you want to save? · 36 Options · 37 Volume ·
38 Sound FX volume · 39 Music volume · 40 "a a" · 41 SEE HIGH SCORES · 42 PLAY · 43 Game cleared · 44 Location ·
45 HIGH SCORES · 46 points · 47 Space · 48 Pirate · 49 House · 50 Mini Game · 51–54 Part A..D · 55 Race ·
56 GAME OVER · 57 NOT · 58 ACCESSIBLE · 59 Save failed. · 60 Load failed. · 61 Are you sure you want to overwrite
this save? · 62 Cancel · 63 Select · 64 No game saved on hard drive · 65 Choose new game · 66 "" · 67 Game Saved ·
68 Game Loaded · 69–100 PS2 memory-card texts (unused on PC) · 101–103 Controller disconnected… ·
104–106 "If you want to leave the game / keep moving in this / direction." · 107 If you ever want to see Knothead
and Splinter again, · 108 I want $1,000,000! · 109 I'm holding them prisoner · 110 in my theme park. · 111 They'll
be no cash for you Buzz! · 112 I'm off to save Knothead and Splinter... · 113–115 To go through the door / stand
in front of it / and press the ACTION button. · 116–118 To play the Jackpot game, find some / dollars and press
the / ACTION button in front of the big slot machine. · 119–121 Behind this door is the Jackpot game. / Press the
ACTION button / to enter. · 122–123 To climb onto the wooden blocks / press the ACTION button repeatedly. ·
124–126 To take a ride on the rocket / stand next to it and / press the ACTION button. · 127 Level · 128 Seconds ·
129 Final Score · 130 Total Score : · 131 THE END · 132 Vibration · 133 On · 134 Off.
Level banks: one string "TOTO" (test leftover). Full dump: `python tools/fontrender.py string <bank> all out.png <textbank>`.

## 9. Open questions

1. HUD animations (§4.6): paths, duration and easing of the `0x4606xx..0x4624xx` functions are not worked out.
2. Boss health (`0x47aca0..0x47b165`): main lines read; slide-in duration and exact y offsets not.
3. Whether Perso child 2/4 is really Knothead and 3/5 Splinter (§4.2).
4. Results screen (`0x4549xx..0x455dxx`) and menu layout (`0x45bxxx..0x4622xx`): only localized. The port
   has its own layout for it (§6); once the original can be traced again: compare y positions, font sizes and the
   icons of the two categories.
5. Texture filter of the 2D layer. (~~Rounding mode of `ftol` `0x499580`~~: it always truncates, §2.1; the live control
   word is `0x007F`.)
6. Sprite 15 (0,96,31,31 in image 63) appears empty; no user found. Sprite 12 (63,0,64,64 in
   image 64) is also not drawn by the HUD; the port uses it as the enemy icon on the
   results screen (**uncertain**).
7. Role of the level `Lang` in language selection; mask.bin layout: done, BLACKBOX.md §3.
8. Power meter: that sprite 11 is also clipped is right there in the code, but the intended look hasn't been
   checked against the original.
