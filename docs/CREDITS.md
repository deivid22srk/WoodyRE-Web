# CREDITS.md — the credits screen (menu page 0x20 on level 0x1a, Woody.exe build 17-10-2001)

Status: **static analysis** (the original was not run for this); addresses = Woody.exe (imagebase 0x400000), reread with
`python tools/funcinfo.py out/disasm_full.txt ADDR`. Supplements GAMEFLOW.md §4 (RequestLevel, level switch), MENU_NEWGAME.md §2
(menu frame, common page class), HUD_TEXT.md §1 and §5 (font, strings, `RectVirtual`, colours) and TITLE.md §5.5 (row 0x20, which
this document replaces). Uncertain points are marked **uncertain**.

## 0. Summary

- The credits are **menu page 0x20 in App state 0 on level 0x1a** (`\Data\Credits\Credits.gel`). Both ways in call
  `RequestLevel 0x404b60(fade, 0x1a, state 0, page 0x20)`: script message **1180** (`0x4448b9`, fade **0**, argument ignored)
  and the frame when the BlackBox object reports done (`0x401d1b..0x401d42`, fade **0.5**). No other caller loads 0x1a.
- The page (vtable **`0x4aa474`**, object 0x20 B at `menu+0x80`, created at `0x44623e`) draws, every frame (`0x45bd90`):
  a **black panel** over the whole 640×480 (`0xfe000000`), a **256×256 picture at (32, 112)** from the Credits bank that fades
  in 1 s, stays 8 s, fades out 1 s and is replaced every **10 s** (three pictures per character, chosen by the **previous level**
  `app+0x6c`), **"THE END"** (Common string 131, size 35) centred on x 160 at y 360, and the **roll** (`0x453930`): a table of
  **253 records** in the exe (`0x4b3d28`) of Credits.rck strings and two images that climbs **50 units/s** from y = 480 and
  starts again from the bottom when it is through (**~181.5 s** per pass).
- **Nothing ends it by itself.** Confirm (action 0xc) returns 5 only after **5 s** on the page (`0x446e90`); the handler
  `0x40577b` then does `RequestLevel(0.5, 0, 0, 0)` = back to House and the title (page 0 + menu music). Back / Esc give result
  24, which the handler ignores; the pause key does nothing in state 0. No sound effect anywhere in the page.
- Meanwhile the Credits level only exists underneath: its script starts music **track 2** (`/Game/1A.wav`, loop) and makes
  instance 0 the Perso; the world is **paused** (table `0x405af8`), the HUD hidden, and the panel covers it (alpha 254/255).

## 1. How it starts

### 1.1 Message 1180 (`0x4448b9`, in the game-message switch `0x444870`)
```c
case 1180: RequestLevel(app, 0.0f, 0x1a, 0, 0x20); break;     /* 0x4448bf..0x4448c7: push 0x20, 0, 0x1a, 0 (float 0.0) */
```
Fade 0: `0x404b60` sets `+0xcc = +0xd4 = 1`, `+0xd0 = 0`, starts the fader with 0 s, stops the music at once
(`[0x5e61a4]->vt[0x4c](0 · 0.9)`, `vt[0x9c]()`, `0x404b85..0x404bac`) and stores level/state/page (`+0xd8/+0xdc/+0xe0`). There is no
guard against a second request. The next App frame finds `+0xd0 − dt ≤ 0`, presents black (`0x47ef80`) and switches (GAMEFLOW §4.2):
unload, `LoadLevel(0x1a)`, `SetState(0)`, `menu_set_page(0x20)` (not `0x404e30`, so no title music). The character stays the same
(index > 0x19, `0x404758`), `app+0x6c` = the level the message came from.

Senders in the shipped scripts (`tools/ekodisasm.py extract/Data/<LVL>/code`), all in the hubs:

| level | object | when | args |
|---|---|---|---|
| WWS | 344 (words 9933..10504) | the ending: state `var59` 4 → 5 starts cinematic 22 (`1130 [0x1000158, 22, var118]`, stream 22 = `/Rtc/Woody/WWSb`); in state 5 → 6 at T = 760 (7.6 s into it, `DELAYPOP 760 − var118`) it hides 342 and sends 1180. The chain starts when object 297 sees `GetPrevLevel var51 == 10` (back from **W3D**) and sets `var59 = 1` (cinematic 21 etc.); object 362 (word 11964) sets `var59 = 4` once `var138 == 3 && var121 == 2` | `[0]` |
| KWS | 307 (words 6427..7580) | `1084 GetPrevLevel(var78)` after `DELAY 1`, then `var78 == 17` (back from **K3R**) → 1180 at once | `[1]` |
| SWS | 314 (words 6693..7846) | same with `var81 == 24` (back from **S3R**) | `[1]` |

So `app+0x6c` is 1 (WWS), 0xb (KWS) or 0x12 (SWS) when the credits start from a script, which selects the picture set (§3.2).

### 1.2 The BlackBox end (`0x401d1b..0x401d42`, in `Game_Frame 0x401ab0`)
```c
if (app->blackbox /*+0xe4*/ && BlackBox_Frame(app->blackbox, app->state == 0) /*0x4846d0*/)
    RequestLevel(app, 0.5f, 0x1a, 0, 0x20);                    /* 0x401d35..0x401d42 */
```
Checked every frame while the BlackBox object exists (level 0x19, or dev flag 0x10); `app+0x6c` is then 0x19 → Woody's pictures.
The BlackBox object itself is outside this document.

## 2. What the Credits level does meanwhile

- `Credits.gel` 156 polygons, one sector; `Credits.ins` one model, one slot (Woody, instance 0 at (497, 1007, 255)); `Credits.rck`
  = no sounds, **13 images, 252 strings, a font of 84 glyphs** (the 75 of the other banks plus `& - é ô j ç " Z °`, HUD_TEXT §1.1).
- Script (1 object): init `1200 [0x1000000, 1]` (SetTypeInstance: instance 0 is the Perso) and **`1655 [2]`** = PlayMusic track 2
  `/Game/1A.wav`, looping (SOUND.md §4). Nothing after init.
- Load as any level: fade-in 1.0 s (`0x401440(1.0)` at the end of `0x404140`), the first 4 frames black (`app+0x50 < 4`, `0x4016f8`).
  The four faders are drawn after the state handler (`0x4016e4`), i.e. over the credits page too: the page fades in from black.
- App state 0, page 0x20: table `0x405af8` → **paused** (App flag 8) because the level is not 0, **no dim overlay**
  (MENU_NEWGAME §2.1); `hud_show(2)` = HUD hidden (`0x404e9d`). `Game_Frame` still renders the frozen world with the follow camera
  behind Woody; the page's panel (alpha 0xfe) leaves 1/255 of it. The House logo `0x446b00` is not drawn (`app+0x68 ≠ 0`).
- No text box, no cinematic, no camera script.

## 3. The page (`vtable 0x4aa474`)

| vt | address | what |
|---|---|---|
| 0 | `0x4462d0` | dtor (shared) |
| 1 | **`0x45bd90`** | draw (§3.1) |
| 2 | `0x446e80` | items = `0x4b5de8`: one item `{0x20001 (empty string 1), flags 2 (header), result 0}` |
| 3 | `0x46c320` | count = 1 |
| 4 | `0x446e70` | page id = 0x20 |
| 7 / 8 | `0x4462b0` / `0x4462c0` | y fraction 0.1 / size 30 (unused: the draw never calls the list drawer `0x446640`) |
| 9 | **`0x446e90`** | validate: `return page+0x18 > 5.0f ? 5 : 0` |
| 10..13 | `0x446970`, `0x446920`, `0x4469d0`, `0x446a20` | base down / up / right / left (nothing to move) |
| 14 | `0x446a70` | back: `+0x10 = 0x18` (result 24) |
| 15 | **`0x45bd60`** | enter: base enter `0x4464c0`, `0x4538f0(0)`, `+0x14 = +0x18 = +0x1c = 0` |

Fields: `+0x14` picture 0..2, `+0x18` time on the page, `+0x1c` time of the current picture. The per-frame driver is the common
`0x4464f0(font, dt)` (MENU_NEWGAME §2.2): `[0x4b39a0] = dt`, font = the level font `[0x5d7b24]` at size 30, white, then `vt[1]`,
then (outside a pause delay) the input: confirm = action 0xc just pressed → `vt[9]`; action 5 or the keyboard's Esc → `vt[14]`.

### 3.1 Draw `0x45bd90`
```c
page->t += dt;                                                              /* 0x45bd9e: +0x18 */
RectVirtual(0, 0, 640, 480, 0, 0, 0, 0, 0xfe000000 x4, blank [0x5e8684], 8);  /* 0x45bdda: black panel */
page->t_img += dt;                                                          /* 0x45bde8: +0x1c */
if (page->t_img > 10.0f) { page->t_img = 0; if (++page->img == 3) page->img = 0; }    /* 0x4a9750 = 10 */
float v = page->t_img;
if (v > 9.0f) v = 1.0f - (v - 9.0f);                                        /* 0x4aa1a4 = 9: fade out over the last second */
if (v > 1.0f) v = 1.0f; else if (v < 0.0f) v = 0.0f;                        /* fade in over the first second */
uint32_t c = 0x808080 - (ftol(v * -254.0f) << 24);                        /* 0x45be5c..0x45be7a = (trunc(254 v) << 24) | 0x808080 */
int k = page->img;
switch (app->prev /*+0x6c*/ - 0xb) {                                       /* 0x45be7c, byte table 0x45bf94, jumps 0x45bf8c */
case 0..6:  k += 3; break;                                                 /* KWS, K1A..K3R: Knothead */
case 7..13: k += 6; break;                                                 /* SWS, S1A..S3R: Splinter */
}                                                                          /* everything else (WWS, W levels, BlackBox): Woody */
RectVirtual(32, 112, 256, 256, 0, 0, 256, 256, c x4, level image table_4b5df8[k], 8);   /* 0x45bee3 */
str = RckGet(0x20083);                                                      /* Common string 131 "THE END" */
Font_SetSize(font, 35.0f); Font_Measure(font, str, &w, &h);                 /* h = 0: it grows only on code 4000 (0x4419b5) */
font->color = 0xfeffffff;
Font_Draw(font, 640 * 0.25f - w * 0.5f, 480 * 0.75f - h * 0.5f, str);       /* (160 - w/2, 360), 0x441ab0 / 0x441ac0 = 640 / 480 */
Roll_Draw();                                                                /* 0x453930 */
```
Table `0x4b5df8` = `{4, 5, 6, 7, 8, 9, 10, 11, 12}` (the next dwords `0, 0x20001, 1, 5, 0` belong to another page's items).
The alpha of the picture: `ftol(−254 v)` is `−trunc(254 v)`, shifted into the top byte and subtracted from `0x808080`, so the
colour is `trunc(254 v) << 24 | 0x808080` (texture × 1.0, SRCALPHA). `RectVirtual` skips a quad whose four alphas are < 2.

The pictures (Credits.rck, 256×256, round screenshots with black corners): 4..6 Woody, 7..9 Knothead, 10..12 Splinter.

### 3.2 The roll `0x453930` (only caller `0x45bf82`)
List objects: two statics with a one-method-pair vtable {records, count}: **`0x5e59f4`** (ctor `0x453890`, vtable `0x4ab228`:
`0x453c60` → records **`0x4b3d28`**, `0x4538c0` → **253**) and `0x5e59f0` (ctor `0x4538b0`, vtable `0x4ab230`: `0x453c70` →
`0x4b54e0`, `0x4538d0` → 25). `0x4538f0(k)` picks one into `[0x5e59f8]` (0 → `0x5e59f4`, 1 → `0x5e59f0`, else NULL) and clears the
offset `[0x5e59fc]`; its only call is `0x4538f0(0)` from the enter `0x45bd6a`, so the second roll is **dead** (25 records: string 0
three times, image 1 256×256, strings 2..0x13, image 0 128×128, two blanks; it matches no shipped bank).

Record (24 B): `+0 ref` (`0x0102xxxx` level string or `0x0101xxxx` level image), `+4 float scale`, `+8 colour`, `+0xc w`, `+0x10 h`,
`+0x14 flags` (1 text, 2 image; placement 0x10 / 4 / 8 / 0x20 / 0x40, tested in that order).
```c
if (!list) return;                                         /* [0x5e59f8] */
font = RckGet(0x1030000);                                  /* the level font */
rec = list->vt[0]();
offset += world->dt * 50.0f;                               /* [[0x509adc]+0x38] * 0x4a9030; the frame dt, written even while paused (0x401880) */
float y = 480.0f - offset;                                 /* 0x4ab248 */
font->color = 0xfeffffff;  int any = 0;
for (i = 0; i < list->vt[1](); i++, rec++) {
    int draw = !(y < -480.0f); if (draw) any = 1;         /* 0x4ab244 */
    if (y > 496.0f) break;                                 /* 0x4ab240 */
    if (rec->flags & 1) {                                  /* text */
        Font_SetSize(font, rec->scale * 26.0f);            /* 0x4ab23c */
        font->color = rec->colour;  s = RckGet(rec->ref);  Font_Measure(font, s, &w, &h);
        x = flags & 0x10 ? 320 - w/2 : flags & 4 ? 16 : flags & 8 ? 640 - w - 16 : flags & 0x20 ? 160 - w/2 + 16 : /*0x40*/ 480 - w/2 - 16;
        if (draw) Font_Draw(font, x, y, s);                /* y = top of the cell */
        y += CellH(font) + Extra(font);                    /* 0x441980 = 62·S/40, 0x441a50 = k·font+0x28 = 0 */
    }
    if (rec->flags & 2) {                                  /* image */
        int w = ftol(rec->w * rec->scale), h = ftol(rec->h * rec->scale);
        x = flags & 0x10 ? 320 - w/2 : flags & 4 ? 16 : flags & 8 ? 624 - w : flags & 0x20 ? ftol(160 - w*0.5f) : ftol(480 - w*0.5f);   /* no -16 */
        if (draw) RectVirtual(x, ftol(y), w, h, 0, 0, rec->w - 1, rec->h - 1, 0xff808080 x4, level image rec->ref & 0xffff, 8);
        y += 2 * Extra(font) + h;
    }
}
if (!any) offset = 0;                                      /* 0x453c43: all of it above -480: start again from the bottom */
```
Placement of the shipped roll: records 0..15 flag **0x41** (centred on x = 464, the images on x = 480: the 16-unit offset of
the text branch `0x453ab1` is not in the image branch), records 16..252 flag **0x09** (right-aligned to x = 624). So the roll runs
down the right half while the picture and "THE END" sit in the left half.

Content (Credits.rck, English build): records 0..15 = strings 0..3 ("Jean Martial LEFRANC", "&", "Philippe ULRICH", "present"),
image 3 (Woody logo 256×256), strings 5..8 ("Woody Woodpecker", "Escape from Buzz Park" 0.6, "was developed and Designed by" 0.6,
empty), image 2 (Eko logo 128×128), strings 9..14 (empty, "powered by the" / "CACTUS TECHNOLOGY" at 0.8, three blanks); records 16..252 =
strings 15..251 in order (team headings at 1.2 / 1.0, job titles 0.8, names 0.6, from "Eko team" to "Universal Studios Consumer
Products Group."). String 4 (empty) and images 0 and 1 are not used. Scales: 125× 0.6, 76× 1.0, 46× 0.8, 4× 1.2 (text); every
colour `0xfeffffff`. The full table is `k_cred` in `src/hud.c`.

Timing: total height 8145.8 units (text 62·26·s/40 per line, images h), last record 32.2. The last line leaves the top at
offset ≈ 8626 (**172.5 s**); the roll restarts when the last record's top passes −480, offset > 960 + 8145.8 − 32.2 = 9073.5
(**181.5 s**), so ~9 s of the pass show only the picture and "THE END".

## 4. Input and the end

- `+0x18` counts from the enter; confirm (action 0xc: the jump key/button, which the port also maps to Enter released) gives
  **5 only after 5.0 s** (`0x446e90`, `0x4a9884`), earlier 0 (nothing). Back (action 5, Esc) gives 24: the handler `0x40577b`
  (`cmp esi, 5`) ignores it. There is no automatic end: pictures and roll loop until confirmed.
- Result 5: `0x405780` = `RequestLevel(0.5, 0, 0, 0)` (shared with pages 0x16, 0x1c-yes, 0x1d, 0x21): fader out 0.5 s, music fade
  out over 0.45 s. While `+0xcc` is set the menu keeps drawing page 0x20 (the page stays current) but no handler runs
  (`0x404f71`). Then House loads, state 0, page 0 via `0x404e30` (`app+0x54` toggles 0 → 1 after a load: track 0 "Menu").
- Sound: none (no `SoundFx` in `0x45bd60`, `0x45bd90`, `0x453930`, `0x446e90`, `0x40577b`; the base enter `0x4464c0` has none either).

## 5. Recipe for the port

1. **Data**: the level bank's strings (type 2) and images 0..15 must be loaded (`src/hud.c` `level_item`: `H.lstr`, `H.limg`);
   `hud_string` resolves `0x01xxxxxx` refs to the level strings. The roll table `0x4b3d28` is not in any data file: it is
   copied into `hud.c` as `k_cred[253] = {string/image index, scale·10, flags, w, h}` (colour always `0xfeffffff`).
2. **Entry**: every load of level 26 enters page 0x20 (`level_load`: House → page 0, 26 → `menu_enter(0x20)`, else no page), since
   both original callers of `RequestLevel(…, 0x1a, …)` pass state 0 + page 0x20. So 1180 (`request_level(26, 0)`), the BlackBox
   (`request_level(26, 0.5f)`) and a command-line `Credits` all give the credits. 1180 is a cut (fade 0 → the port's 0.01 s).
3. **Enter** (`0x45bd60`): picture 0, picture clock 0, page clock 0, roll offset 0 (`hud_credits_enter`, `M.cred_t = 0`).
4. **World**: `menu_pauses_world()` includes page 0x20 (paused, no overlay); the HUD is hidden on every page but 0x18/0x19 already.
5. **Draw** (`hud_credits(g_prev_level, dt)` from `menu_draw`): `hud_rect(0xfe000000)`; picture `4 + {0,3,6}[set] + img` at
   (32, 112) 256×256 with alpha `trunc(254 v)`; "THE END" size 35 at (160 − w/2, 360); the roll as §3.2 (font sizes 26·s,
   `font_cell()` per text line, `(int)y` for images, restart when nothing is above −480).
6. **Input** (`menu_update`, page 0x20): `cred_t += dt`; `ok && cred_t > 5` → `request_level(0, 0.5f)` without `menu_off()`, so the
   page keeps drawing during the fade like the original. Back/Esc: nothing.
7. **Return**: `level_load` of House puts page 0 and the menu music on, as for every return to the title.

Deviations of the port: the black panel covers the whole view on a wide window (port extra, as every `hud_rect`); image UVs use
`sx/W .. (sx+sw)/W` like the other port blits instead of the half-texel mapping of `RectVirtual` (HUD_TEXT §5.3); the page clock is
counted in `menu_update` (which is skipped during the fade-out; only the validate reads it); the 4 black frames after a load are not
reproduced.

Test: `out/woody_dbg.exe <Data> Credits --prev KWS --shot out/cr.ppm 16` (Knothead picture 8, roll at offset 800);
`WOODY_MSGAT="1 1180 1" WOODY_KEYS="2:RET 3:ESC 7:RET" WOODY_MENULOG=1 out/woody_dbg.exe <Data> KWS --shot out/t.ppm 8` (credits
from the hub, Enter at 2 s and Esc at 3 s ignored, Enter at 7 s → title).

## 6. Open / uncertain

1. Static only. A live check could put a breakpoint on `0x45bee3` (picture index / alpha) and `0x453930` (offset `[0x5e59fc]`).
2. The texel mapping of the last source column/row (`sw = w − 1` in the roll, `sw = 256` for the picture) is not verified.
3. The fader with duration 0 (1180): `0x445bf0` draws `remaining/total·254`; with total 0 that is 0/0 for the one frame before the
   switch (**uncertain** what it shows; the switch happens on that frame anyway).
4. The dead second roll (`0x5e59f0`, 25 records) may have been for a demo or another language's shorter Credits bank.
