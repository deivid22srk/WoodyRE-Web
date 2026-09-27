# RESULTS.md — the results screen after a level (menu page 0x1e, Woody.exe build 17-10-2001)

Status: **static analysis**; addresses = Woody.exe (imagebase 0x400000), to be reread with
`python tools/disasm.py game/Woody.exe START END`. Scratch: `out/res_453c80.txt` (0x453c80..0x456100),
`out/res_panel.txt` (panel base 0x45b830..), `out/res_iris.txt` (iris 0x4776b0..). Supplement to GAMEFLOW.md §5.1
(states `perso+0x724`, score), HUD_TEXT.md §1-§6 (font, strings, `RectVirtual`, colours, sprites), TITLE.md §5.4 and
MENU_LOAD.md §2.1 (panel page + iris). Uncertain points are marked with **uncertain**. This document supersedes
the "own layout" of HUD_TEXT.md §6 and GAMEFLOW.md §5.2 (discrepancies).

## 0. Summary

- Page 0x1e is a **panel page** (base `0x45b830`) with its own vtable **`0x4aa934`** (object 0x6c B, `menu+0x78`,
  created in `0x446028..0x446059`). Enter `0x4544b0`, show `0x454560` → `0x4544f0`, hide `0x454580` →
  `0x454530`, draw content (`vt[17]`) **`0x454610`**, confirm (`vt[9]`) `0x4545a0`.
- Layout (640×480 virtual): a **black iris** around the **screen centre (320, 240)** with hole radius **175.8** (v = 0.37);
  Woody stands there only because the camera of animation 74/75 puts him in the middle. Top left **"RESULTS"**
  (red with white shadow, size 25). Top right **"HIGH SCORE"** (white, 18) with, below it, centred, the
  **old** best score for this level. On the left a column of five lines, each *icon + label (centred on x = 45)* and
  *"=" at x = 85 + number from x = 100 that counts up at 5000 points/s*: **clock** (time `m:ss`), **enemy face**
  (`defeated/total`), **big W** (`collected/total`), a white bar + **"TOTAL"**, and the **$ icon** with the count
  of new unique items (large, red). Between the first three lines a "+". At the bottom, centred, the **level name**
  ("Space Part A") at y = 415 and below it **"CLEARED!!"** (orange-red with white shadow, 20).
- There is **no "OK" button or prompt**: the only page item (string 41 "SEE HIGH SCORES", flag 2) is never drawn.
  First confirm while counting = everything finishes instantly; confirm once everything is done = result 5 (cheer).
- Page flags (`0x405af8[0x1e] = 1`): **not paused, no dim overlay**, HUD hidden. The logo from
  `0x446b00` has already faded out.

## 1. The page and its vtable

| vt | address | what |
|---|---|---|
| [1] | `0x45b990` | panel draw base (MENU_LOAD §2.1): `+0x28 += dt`, `+0x1c += dt`, `vt[16]`, iris, `vt[17]` |
| [2] | `0x446e30` | item table `0x4b5738`: **1 item** `{0x20029 (41 "SEE HIGH SCORES"), flag 2 (header), 0, 0}` — never drawn |
| [3] | `0x46c320` | count = 1 |
| [4] | `0x446e20` | page id 0x1e |
| [6] | `0x455db0` | `+0x5c ? 0 : 3` (no reader found) |
| [7] | `0x446e40` | y-start 0.9 (unused, the list is not drawn) |
| [9] | `0x4545a0` | confirm (§5) |
| [14] → [20] | `0x45bb30` → `0x445840` | back = `ret`: **Esc does nothing** |
| [15] | `0x4544b0` | enter (§4.1) |
| [16] | `0x462c60` | `ret` |
| [17] | `0x454610` | content (§2) |
| [18] | `0x45baa0` | deliver result after closing (not reached, §4.4) |

Fields (besides the panel fields of MENU_LOAD §2.1): `+0x3c` clock of the counting lines (s), `+0x40` start time
of line 0 (0), `+0x44/+0x48/+0x4c/+0x50/+0x54` = end time of line 0..4 (−1 = not done yet), `+0x58` race flag,
`+0x5c` panel visible, `+0x60/+0x64` level-name string pointers, `+0x68` loop-sound object.

Every frame the menu (`0x4464f0`) first calls `vt[1]` with font `0x01030000` (**font of the level bank = the hub**),
size 30 and colour `0xffffffff`, and then the logo `0x446b00`; all text below sets its own size and colour.

## 2. Drawing: `0x454610` (only if `+0x5c`)

```c
void Results_Content(P *p) {                                   /* 0x454610 */
    if (!p->shown) goto sound;                                 /* +0x5c */
    Results_LevelName(p);                                      /* 0x4559b0: +0x60/+0x64/+0x58 from the level table, §3.6 */
    float off;
    if (p->t28 <= p->dur34) {                                  /* iris still animating (0.5 s after showing) */
        if (p->closing22) { off = p->t28 * -300 / p->dur34; Lines(off, 1); }   /* died: +0x22 stays 0, §4.4 */
        else              off = (p->dur34 - p->t28) * -300 / p->dur34;         /* -300 -> 0; NO counting lines */
    } else { off = 0; Lines(0, 0); }                           /* +0x58: 0 -> 0x454700, 1 -> 0x454860 */
    Title_RESULTS(off);                                        /* 0x455790 */
    HighScore(-off);                                           /* 0x455850 */
    LevelName_Cleared(-off);                                   /* 0x455bc0 */
sound:
    LoopSound_Update(&p->snd68, 0, 0x3d, [0x5e48c8], -1.0f);   /* 0x468e50 */
}
```
Call order = draw order within each blend list. **Uncertain/note**: `RectVirtual` with flag 4 (additive)
and flag 8 (alpha) go to different lists; in `0x428ee0`, per depth bucket, the alpha list is drawn first
(`0x4290bf`) and then the additive list (`0x429182`), so the additive icons (clock, enemy face, bar)
come **after** all text and the iris. Visually this makes no difference (nothing overlaps).

### 2.1 Fixed coordinates (static init `0x4542f0` and `0x4543c0`, via the CRT table `0x4b1028`/`0x4b102c`)

Icons, all horizontally centred on **x = 45** (`0x4ab27c`):

| # | icon | source | destination (x, y, w, h) | blend | address |
|---|---|---|---|---|---|
| 0 | **clock** | level bank (`0x01010001`) image 1, (51, 0, 36, 36) | (27, 75, 36, 36) | **4** additive | `0x4549fc`, data `0x4ab250` |
| 1 | **enemy face** (yellow bird) | level bank image 1, (0, 0, 50, 50) | (20, 170, 50, 50) | 4 additive | `0x454aa5`, `0x4ab264` |
| 2 | **big W** = HUD sprite 4 | bank 0 image 62, (0, 0, 94, 94) | (9,28, 275, 71,44, 71,44) → int (9, 275, 71, 71) | 8 alpha | `0x454b4f`, `0x4ab6a8` |
| 3 | **finish flag** = HUD sprite 5 (race) | bank 0 image 63, (0, 0, 94, 94) | (9,28, 225, 71,44, 71,44) | 8 alpha | `0x454c08`, `0x4ab6bc` |
| 4 | **bar** | blank white texture (`surface = 0`) | (16, 370, 140, 2) | 4 additive | `0x454cc2` |
| 5 | **$** = HUD sprite 3 | bank 0 image 61, (63, 63, 64, 64) | (20,68, 410, 48,64, 48,64) → (21, 410, 49, 49) | 8 alpha | `0x454d30`, `0x4ab694` |

Scale of the bank-0 sprites 0.76 (`0x4ab280`), x = 45 − 0.38·w (`0x4ab278`). Colour always 4× `0xfe808080`
(`0x454e2f`). Positions go to `RectVirtual` as int (`fistp`, rounding in the default FPU mode).
The level bank is the **hub**'s (WWS/KWS/SWS.rck image 1, 128×128, 24 bit, identical in all three; it is
a black sheet with the bird face top-left and the red clock next to it — black disappears via the additive draw).
Correction to TITLE.md §5.2: House.rck image 2 is **the same sheet**, not "blank white" (the alpha byte 0 misleads
the PNG export).

Row anchors `0x4b5748 + 8·r` (x, y), used for the labels (row = case + 1) and for "=" / number:

| row | x | y | formula (`0x4543c0`) | use |
|---|---|---|---|---|
| 1 | 45 | **106** | 75 + 36 − 5 | time label + time line |
| 2 | 45 | **215** | 170 + 50 − 5 | enemy label + line |
| 3 | 45 | **341.44** | 275 + 94·0.76 − 5 | W label + line |
| 4 | 45 | **291.44** | 225 + 94·0.76 − 5 | flag label + line (race) |
| 5 | 45 | **375** | 370 + 5 | "TOTAL" + line |
| 6 | **85** | **405** | constant | x = centre of all "=" signs; y = the $ line |
| 7 | 624 | 16 | constant | right edge of HIGH SCORE |
| 8 | 320 | 415 | constant | level name |
| (0) | 16 | 16 | data | RESULTS |

### 2.2 A counting line: icon + label `0x4549c0`, "=" + number `0x454f60`, slide-in `0x454f20`

`0x4549c0(xoff, case, str, size, colour, shadow)` (`ret 0x18`, switch table `0x454f08`): `SetSize(size)` (`0x4549e4`); icon
from §2.1 at `x + xoff`, **all five numbers of the rect rounded with `fistp`** (`0x454dd2..0x454e25`, round-to-nearest-even:
x = 9.28 → 9, w = 71.44 → 71, $ x = 20.68 → 21, w = 48.64 → 49; while sliding, `x + xoff` is rounded too) and drawn with
`0x480a10(x, y, w, h, sx, sy, sw, sh, 4 × 0xfe808080, texture, flag)`; if `str`: label at `x = 45 − w/2 + xoff`,
`y = row(case+1).y`, first (if `shadow`) white `0xfe808080` at `(+0.05·cel, +0.05·cel)` (`0x4aab4c`, cel = 62·S/40), then in
`colour`. It returns the clock `+0x3c` when `xoff == 0`, else −1 (`0x454edd`); every caller throws that away (`fstp st(0)`).

`0x454f20(start, xoff, force)`: `force ? xoff : (clock − start < 0.2 ? (0.2 − (clock − start))·(−1000) : 0)` —
each line **slides in over 0.2 s from −200 px to 0** (icon + label), linearly.

`0x454f60(start, value, row, size, colour, shadow)` (`ret 0x18`):
```c
if (start + 0.2f > clock) return -1;                            /* 0x454f72: only after sliding in */
SetSize(size);
Draw("=" /*string 8*/, 85 - w("=")/2, row.y, 0xfe808080);      /* 0x454fef */
float right = 85 + w(itoa(value)) + 15;                        /* 0x45502f: width of the FINAL number */
int n = fistp((clock - start - 0.2f) * 5000);                   /* 0x455041: 5000 points per second */
float done = -1;
if (n > value) { n = value; done = clock; }                    /* 0x455064 */
x = right - w(itoa(n));                                        /* right-aligned; final number starts at x = 100 */
if (shadow) Draw(itoa(n), x + 0.05*cel, row.y + 0.05*cel, 0xfe808080);
Draw(itoa(n), x, row.y, colour);
if (done == -1) LoopSound_Request(&p->snd68);                  /* 0x468e40: ticking sound while still counting */
return done;                                                    /* = start time of the next line */
```
The number thus counts up from 0 at 5000/s and is right-aligned, so the final number starts at x = 100.
Numbers: `itoa` `0x441820` (negative → 0). The end time becomes the start of the next line; a line takes
0.2 s + value/5000 s + 1 frame. Label, "=" and number of a line all use the line's own `SetSize`; the fit `0x45dc90` (TOTAL,
level name) starts from the **current** font size (`font+0x4c`), which is 15 there because the line before set it.

### 2.3 The lines (normal level: `0x454700`; race: `0x454860`)

`0x454700(xoff, force)`: `clock (+0x3c) += dt` (`[0x509adc]+0x38`); then in order, each only if the previous is done (> −1).
A line runs every frame once it is reached; its return value is stored only while its slot is still −1 (`cmp [+0x44], −1`
at `0x454716`, likewise `0x454777`, `0x4547c6`, `0x454802`), so the end time is the frame it finished. Two details:
the **"+" is drawn in the else branch** (`0x454738`, `0x45478f`): only from the frame *after* the line above it finished, and
before that line is drawn again; and the **$ line's result is stored every frame** (`0x454848`, race `0x454909`): `+0x54`
is the clock of the current frame once the $ counter is done (−1 in a frame where the counter is exactly at its value).

| line | function | icon (case) | label (S 15, white, no shadow) | "=" + number (S, colour) | value | end time |
|---|---|---|---|---|---|---|
| 0 time | `0x455160(+0x40)` | clock (0) | `m:ss`: `t = _ftol(app+0x84)` (truncated), `m = t/60`, `s = t%60` with leading zero if `s < 10` (`0x4551bc`), separator string 10 ":" | 15, white | `max(0, 1800 − t)·10` (`0x453d70`) | `+0x44` |
| — | `0x454920(1)` | | **"+"** (string 11), S 15, white, centred on x 45, **y 136** (= 106 + 30, `0x4a9740`) | | | once `+0x44 ≠ −1` |
| 1 enemies | `0x4552b0(+0x44)` | enemy face (1) | `stat[2]` + string 9 "/" + `stat[0]` (`app+0x7c`, `app+0x74`) | 15, white | `stat[2]·100`, ×1.5 (int, `(v + v/2)`) if `stat[2] == stat[0]` (`0x453d50`) | `+0x48` |
| — | `0x454920(2)` | | "+" at x 45, **y 245** | | | once `+0x48 ≠ −1` |
| 2 W bonuses | `0x4553a0(+0x48)` | big W (2) | `stat[3]/stat[1]` (`app+0x80`, `app+0x78`) | 15, white | `stat[3]·100`, ×1.5 if complete (`0x453d20`) | `+0x4c` |
| 3 total | `0x455580(+0x4c)` | bar (4) | string 14 **"TOTAL"**, S 15 shrunk to width ≤ 65 (`0x45dc90`), **red `0xfeff0000` with white shadow** | same S, white | `0x453cb0(stats, +0x58 == 1)` = sum of the three (race: line 2 only) | `+0x50` |
| 4 $ | `0x455650(+0x50)` | $ (5) | — (`0x4549c0(…, 5, str 0, S 15, white, 0)`, no text) | "=" S **40** white at (85 − w/2, 405) from start + 0.2; number via `0x454f60(start + 0.4, …, row 6, 40, **red `0xfeff0000`**, shadow)` → appears from start + 0.6 | `0x453cf0` = count of complete categories (0..2) = count of new unique $ items (GAMEFLOW §5.1) | `+0x54` |

The values come from `app+0x74..0x84` (= `perso+0x710`, GAMEFLOW §5.1: `{total enemies, total W, defeated,
collected, float time}`). The screenshot "5:01 = 12363" is a line that's **still counting** (final value (1800 − 301)·10 = 14990).

**Race** (`+0x58 = 1`, `0x454860`): no time and enemy lines, no "+" signs. Lines: **flag** `0x455490(+0x48)`
(case 3: flag sprite at y 225, label `stat[3]/stat[1]` and number at **y 291.44** (row 4), value `0x453d20`), then
TOTAL (`0x455580`, race score) and $ (`0x455650`). Because `+0x48` is −1 at enter (`0x4544d2`), the flag line starts with
`start = −1`: no sliding in and the counter starts at roughly (clock + 0.8)·5000 — in practice the number is there instantly.

### 2.4 "RESULTS" `0x455790(off)`

`SetSize(25)` (`0x41c80000`), string 13. Shadow white `0xfe808080` at `(16 + 0.05·38.75 + off, 16 + 1.94)`, then
**red `0xfe800000`** at **(16 + off, 16)** (`0x455835`). Left-aligned.

### 2.5 "HIGH SCORE" `0x455850(a)` (a = −off)

`SetSize(18)`, string 17, white `0xfe808080`, **right-aligned at x 624**: `(624 − w + a, 16)`. Below it the number
`0x450230(save [0x5e5818], character [[0x5e5814]+0x380], level app+0x6c)` = **the saved best score for this
level before this run** (the new record is only written in state 4, `0x40598d..0x405a36`, once the page is already
gone) — hence "0" the first time. Number: same size and colour, **horizontally centred under the label**
(`x = 624 − wLabel/2 + a − wNumber/2`), `y = 16 + cel(18) + extra(0)` = **43.9** (`0x455960..0x455985`).
No blinking, no "new record" indicator.

### 2.6 Level name and "CLEARED!!" `0x455bc0(a)`

Name = `str(+0x60) + " " + str(+0x64)`; the space is the middle character of string 40 "a a" (`0x455be6`). Size
15, shrunk to width ≤ **400** (`0x45dc90`, step 1, min 10). White at **(320 − w/2, 415 + a)**.
Then `SetSize(20)`, string 12 **"CLEARED!!"**: centred on x 320, **y = 415 + cel(namesize) + a** (= 438.25 at
S 15); shadow white at +0.05·31 = +1.55, then **`0xfe801400`** (R 1.0, G 0.16: orange-red) (`0x455d84`). Always
drawn, even in race levels. At 480 height the cel ends at 469 (not clipped in 640×480).

`0x4559b0` picks the strings per frame from `app+0x6c` (the level you came from; table `0x455b60`, index − 2):

| level (index) | `+0x60` | `+0x64` | race |
|---|---|---|---|
| W1A (2), K1A (12), S1A (19) | 47 Space | 51 Part A | |
| W1B (3) | 47 Space | 52 Part B | |
| W2A (4), K2A (14), S2A (21) | 48 Pirate | 51 Part A | |
| W2B (5) | 48 Pirate | 52 Part B | |
| **W2D (6)** | 48 Pirate | **53 Part C** | |
| W3A (7), K3A (16), S3A (23) | 49 House | 51 Part A | |
| W3B (8) | 49 House | 52 Part B | |
| W3C (9) | 49 House | 53 Part C | |
| W3D (10) | 49 House | 54 Part D | |
| K1R (13), S1R (20) | 47 Space | 55 Race | **1** |
| K2R (15), S2R (22) | 48 Pirate | 55 Race | 1 |
| K3R (17), S3R (24) | 49 House | 55 Race | 1 |
| BlackBox (25) | 50 Mini Game | 1 "" | |
| other (0, 1, 11, 18, ≥ 26) | 1 "" | 1 "" | |

The race flag here (`0x455af9`) is the same set as `isRace` in the handler (`0x405932`: 0xd, 0xf, 0x11, 0x14, 0x16, 0x18).

### 2.7 The "W" top-right from the screenshot

**Not found.** No draw call of page 0x1e (§2), of the panel base `0x45b990`, of the menu
(`0x4464f0`) or of the HUD (hidden, `0x447213`) draws anything top-right besides "HIGH SCORE" and the number. The
logo `0x446b00` is at (216, 16) and has already faded out on this page (`[0x5d7b28]` −5/s). The only W is sprite 4
of the W line on the left (x 9..80, y 275..346). **Uncertain**: an additive 3D object that falls in the same depth
bucket as the 2D layer would be drawn after the (alpha) iris (§2, `0x428ee0`); not verified.

Second pass (static): every other 2D drawer that uses a W was checked and excluded. The HUD animator `0x4480d0`
(W swarm `0x47c7c0`, the W flight `0x4611b0`, the 1172 slide `0x461820`/`0x461a00` that draws sprite 4 at slot 0) runs
only inside `0x447210`, which draws nothing in HUD state 2 (`0x447213`, set by `0x404eba` for page 0x1e). The
high-score page 4 (MENU_LOAD §4.8) has the W ball as a column head, but it is not reachable from page 0x1e (its only
entry is page 3's result 4). The pickup trail particles (`0x47bba0`/`0x47bca0`) are bank-0 image 4 glows, not W's. So
the W is still unexplained; the most likely candidates remain a 3D sprite of the hub (a type-34 bonus halo, bank 0
image 20, `0x479530`) that is drawn in the 2D bucket, or a frame of the page 0x1e → 6 transition. The screenshot of
issue #7 itself was not inspected (downloading the attachment needs the user's permission).

## 3. The iris

| | value | address |
|---|---|---|
| shape | black ring of **50 segments** (step 0.02 of a 512-entry sine table `[0x5e823c]`), inner radius `v·0.99·480`, outer radius 0.99·480 = **475.2** | `0x4776d0` |
| centre | **(320, 240)**, fixed (`0x4ab5bc`, `0x4abd40`) — **not** on Woody; the camera puts Woody in the middle | `0x477812`, `0x47782a` |
| points | `(320 + r·sin θ, 240 − r·cos θ)` (table ×0.99 and ×−0.99, `0x4ab7d8`, `0x4abd44`) | `0x47770e`, `0x47773c` |
| colour | vertex colour (0, 0, 0, 1.0) → `0xff000000`, **opaque black**, flag 8 (alpha list); coordinates 640×480 virtual, clipped to 640/480 | `0x4777bc..`, `0x482fb7..0x483041`, `0x482d25` |
| time | linear: `v = from − (from − to)·min(t/duration, 1)` | `0x477920` |
| showing (state 1) | **1.0 → 0.37 in 0.5 s** (hole 475 → **175.8 px**) | `0x4544f0` (`0x3ebd70a4` = 0.37) |
| hiding (state 2/3) | 0.37 → 1.0 in 0.5 s | `0x454530` |
| enter (state 0) | `+0x38 = 0` ⇒ **no iris** during the arrival | `0x4544b9` |

At v = 1.0 the hole is bigger than the screen diagonal (400) ⇒ invisible. `0x45b990` only animates the iris if `+0x38`
and `+0x30 > 0`; after hiding it stays at 1.0.

## 4. Progression per state of `perso+0x724` (handler `0x4058cd`, jump table `0x405d0c`)

### 4.1 State 0 — arrival (action 0x4a)
The menu switches to page 0x1e (GAMEFLOW §4.5). Enter `0x4544b0`: `+0x5c = 0`, `+0x38 = 0`, base enter `0x45b8c0`
(**SoundFx 0x3f**, `+8 = 0.5` input delay, `+0x28 = 0`), `+0x3c = +0x40 = 0`, `+0x44..+0x54 = −1`, loop sound
cleared (`0x468e10`). The handler calls `0x454580` every frame (does nothing, panel is already gone). **Nothing 2D on screen**: no iris,
no text, no HUD, no overlay; the world keeps running.
**Quirk**: `+0x28` runs from the enter; a confirm after 0.5 s but still in state 0 sets, via §5, all
lines to "done", so the screen will appear immediately with final values.

### 4.2 State 1 — panel (action 0x4b)
Handler `0x4058f8`: every frame `0x454560` (once, via `+0x5c`): `+0x28 = 0`, `+8 = 0.5`, iris 1.0 → 0.37 (0.5 s),
`+0x38 = 1`.
- **0 .. 0.5 s**: iris closes; "RESULTS" slides in from the left (x −284 → 16), "HIGH SCORE" + number from the right (+300 → 0),
  level name + "CLEARED!!" from below (y +300 → 0); linear, `off = (0.5 − t)·(−300)/0.5`. No counting lines yet, the
  counting clock stands still. No input (`+8`).
- **from 0.5 s**: counting clock runs; line 0 slides in (0.2 s), counts up; "+"; line 1; "+"; line 2; TOTAL; $ (§2.3).
  Tick sound SoundFx **0x3d** plays while counting is in progress (`0x468e50`: starts `0x468a00`, stops `0x468a30`).
- **Confirm** (§5): first time while counting → everything finishes; once everything is done (`+0x54 > −1`) → result 5 →
  `0x453cf0` → `0x453fc0` (cheer action 0x4e if n ≠ 0, otherwise 0x4c; state 2/3) + `0x44c840(n)` (n unique items).

### 4.3 State 2/3 — cheering
Handler `0x4058ef`: `0x454580` → `+0x5c = 0`: **all text and icons disappear instantly**, iris opens 0.37 → 1.0 over
0.5 s, `+8 = 0.5`. Tick sound stops. After this, only the 3D view (not paused).

### 4.4 State 4 — save? / 5 — gone
`0x405932`: score (§2.3 TOTAL formula, `isRace`), write record if higher (`0x450260`, `0x450380..0x450440`),
then **page 6** (`0x405a40`). Page 0x1e is no longer active by then; page 6 has page flag 2 (see §6) and draws
its own list. State 5 has no page (GAMEFLOW §5.2).
The panel base's close path (`+0x22`, `vt[18]`, sliding out with `off = t·(−300)/0.5` and `force = 1` in the
lines) is **dead** for this page: nothing calls `0x45bae0`/`0x45bb40` (confirm is `0x4545a0`, back is `ret`).

## 5. Confirm `0x4545a0` (action 0xc, only if `+8 ≤ 0` and `+0xc == 0`, `0x4465c6`)

```c
p->result10 = 0;
if (p->lock24) return 0;
if (p->t54 > -1) { if (p->shown5c) return p->result10 = 5; }   /* everything done: OK (returns before clearing +0x14, 0x4545cb) */
else if (p->t28 > p->dur34) {                                  /* still counting: skip */
    float k = p->klok3c; p->klok3c += 60;                      /* 0x4ab284 */
    p->t40 = p->t44 = p->t48 = p->t4c = p->t50 = p->t54 = k;   /* all lines "done at time k" */
}                                                              /* ⇒ no more sliding in, numbers at final value */
p->deferred14 = 0;
return p->result10;
```
Back (Esc, action 5) and up/down do nothing visible.

## 6. Page flags (`0x404e90`, byte table `0x405af8`, jump table `0x405ae0`)

| case | code | pause (`app+0xf4` bit 3) | dim `0x80000000` | pages |
|---|---|---|---|---|
| 0 | `0x404ef1` | = (`app+0x68` ≠ 0, i.e. in a level) | no | 0–4, 0x1d, 0x1f, 0x20 |
| **1** | **`0x404fb7`** | **off** | **no** | 5, **0x1e** |
| 2 | `0x404fb5` | off | yes | 6, 8, 9, 0xc–0xe, 0x12–0x15, 0x17 |
| 3 | `0x404ee6` | = in level | yes | 7, 0xa, 0xb, 0xf–0x11 |
| 4 | `0x404fc3` | on, + `0x4014c0` | no | 0x16, 0x21 |
| 5 | `0x404ee8` | = in level | = in level | 0x18–0x1c |

HUD: `0x448450(2)` = hidden for everything except 0x18/0x19 (`0x404e9d..0x404ebe`). Page 0x1e: the world keeps
running (Woody's animations), **no dim layer**, HUD gone. Only the next page 6 ("Do you want to save?") lays
the dim layer over the screen.

## 7. Recipe for the port (`src/hud.c`, `src/main_engine.c`)

```c
/* state: shown, t (since show/hide), iris_from/to/t, clock, end[5] (-1), race, snd */
show(): shown = 1; t = 0; input_delay = 0.5; iris(1.0 -> 0.37, 0.5); iris_on = 1;
hide(): shown = 0; t = 0; input_delay = 0.5; iris(0.37 -> 1.0, 0.5);
enter(): shown = iris_on = 0; t = 0; clock = 0; end[*] = -1; fx(0x3f); input_delay = 0.5;
draw(dt): t += dt; if (iris_on) hud_iris(v(t));               /* centre (320,240), opaque black */
  if (!shown) { stop fx 0x3d; return; }
  off = t < 0.5 ? (0.5 - t) * -300 / 0.5 : 0;
  if (t > 0.5) { clock += dt; lines(); }                       /* §2.3, 5000 pt/s, 0.2 s slide-in per line */
  RESULTS (25, red 0xfe800000 + white shadow) at (16+off, 16);
  HIGH SCORE (18, white) right at 624-off, number (old best) centred below at y 43.9;
  name (15, fit 400) at (320-w/2, 415-off); CLEARED!! (20, 0xfe801400 + shadow) at y 415+cel+(-off);
confirm(): end[4] > -1 ? OK : (t > 0.5 ? skip_all : nothing);
```
Shadow = the same text in `0xfe808080` at (+0.05·cel, +0.05·cel), before the coloured text. Colours per
HUD_TEXT §5.2 (`rgb = min(1, 2·c/255)`). No "OK" item, no dim panel, no background panel.

## 8. Uncertain / open

1. The grey "W" top-right from the screenshot (§2.7).
2. ~~Rounding of `fistp` in the counter and the icon positions~~: round to nearest-even (control word `0x007F`, verified
   live with `tools/wverify.py --probe fpu`); `_ftol` for the time truncates.
3. Draw order alpha vs. additive list within the 2D bucket (§2) is inferred from the structure of `0x428ee0`, not
   traced in full detail.
4. `vt[6]` `0x455db0` (0 if the panel is visible, else 3: `neg al; sbb; and 0xfd; add 3`): no reader found.

Round 33 (static): every function of the page (`0x4542f0`, `0x4543c0`, `0x4544b0..0x454610`, `0x454700`, `0x454860`,
`0x454920`, `0x4549c0`, `0x454f20`, `0x454f60`, `0x455160`, `0x4552b0`, `0x4553a0`, `0x455490`, `0x455580`, `0x455650`,
`0x455790`, `0x455850`, `0x4559b0` with table `0x455b60`, `0x455bc0`, `0x455db0`) and the panel base (`0x45b8c0`,
`0x45b990`) re-read instruction by instruction, every constant read from the exe (`0x4ab250..0x4ab28c`, `0x4ab694..0x4ab6cc`,
`0x4a9014` 0.5, `0x4a9740` 30, `0x4a9760` 0.2, `0x4a9864` 15, `0x4a9868` −300, `0x4a9884` 5, `0x4aa394` 0.4, `0x4a9750` 10).
Corrections: the icon rects are rounded (§2.2), the "+" appears one frame after its line, `+0x54` is rewritten every frame
(§2.3), the $ line sets size 15 (§2.3), result 5 returns early (§5). All of them are in the port now (`src/hud.c`
`res_icon`, `res_lines`, `res_dollar`).
