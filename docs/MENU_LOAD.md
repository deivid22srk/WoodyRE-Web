# MENU_LOAD.md — "Load game": save file, slot selection, world-selection carousel (Woody.exe, build 17-10-2001)

Status: **static analysis**; addresses = Woody.exe (image base 0x400000), to be re-read with
`python tools/drange.py START END`. Data: `extract/Data/House/House.ins` (`tools/insparse.py`), `House.rck` and
`Common/Woody.rck` (`tools/rckexport.py`), script `out/house_code.txt`. Addendum to TITLE.md §5, GAMEFLOW.md §5–§7,
HUD_TEXT.md §4.2/§8 and CAMERA.md §5.2. Uncertain points are marked **uncertain**.

## 0. Summary

- **Chain**: page 1 "Load game" → iris closes (0.85 → 0 in 0.5 s, no sound) → result 3 → does `Woody.sav` exist?
  (`0x450aa0`) → no: page **7** "No game saved on hard drive / Choose new game"; yes: page **0xb** (exactly one
  frame) → read `Woody.sav` (`0x450be0`) → failed: page **0xa** "Load failed."; succeeded: page **2** (slot selection,
  2x2 grid, empty slots not selectable) → slot i → slot copied into the active save struct + volumes → page **3**
  (world-selection carousel) → PLAY on an unlocked character → `RequestLevel(0.4, 1 / 0xb / 0x12 / 0x19, 1 or 3, 0)`.
  "Back" on 2 and 3 goes to page 1 (the iris opens again there, 0 → 0.85); the error pages only have "Continue".
- **Page 2**: four panels in the corners of a black screen (iris hole r ≈ 176 px in the middle shows the spinning
  treehouse). Per slot: a green neon panel (House.rck image 0, additive) with three circles for the faces
  (Woody always, Knothead if W2D reached, Splinter if W3D reached), a gold ring with the **percentage** in
  red, the label "Save N" or "FREE"; an empty slot gets a red **cross** (House.rck image 0, 63x63 stretched
  to 108x108). Title "Select game". Panels slide in/out (±300 px, 0.5 s, linear).
- **Page 3**: the camera does **not** change. The eight class-110 instances (4 characters + 4 pedestals, House
  slots 105–114) are placed **in camera space every frame** (`0x489210`): a circle with radius 150 at 560 in front
  and 100 below the eye, tilted 10°, 90° per character, the character at position `pos` stands at the front (+7°).
  Left/right rotates the carousel linearly 90° in 0.5 s; the selection switches halfway. Locked characters are
  drawn with light color x0.1 (near-black silhouette); a not-yet-unlocked character is the "?" figure (model 13).
  Around the carousel: name on top, stats panel on the left (face, lives, health dots, $, charges), right
  "Game cleared NN %" and "Location" (next level), below "PLAY" / "SEE HIGH SCORES" and "Total Score :"; yellow
  arrows left and right.
- **Save advice**: the original format is a flat POD of 0x52c4 bytes with no working checksum; the port can carry it
  over 1-to-1 (4 slots) and optionally import an existing `Woody.sav` (§7).

## 1. The chain from "Load game" to the hub

### 1.1 Timeline

| t | page / code | what happens |
|---|---|---|
| 0 | page 1, validate `0x460132` | item 1: `+0x22 = +0x24 = 1` (closing, input on slot), `+0x18 = 1`, **`+0x38 = 1`** (iris on), `+0x14 = 3`, `+0x28 = 0`, iris `0x4776b0(0.85 → 0, 0.5 s)`, logo alpha `[0x5d7b28] = 0` (logo gone immediately). **No sound.** Up/down/back blocked (`+0x24`) |
| 0.5 s | `0x45b990` → `vt[18]` `0x4601b0` → `0x45baa0` | result 3 (deferred, `+0x28 ≥ +0x34`) |
| 0.5 s | handler `0x4051da` | `r = [app+0x4c]->vt[3]()` = `0x450aa0` (open `Woody.sav` with mode 5): **0** = exists, **7** = doesn't. Switch on `r − 3` (table `0x405bc0`): 3/6 → page 0xf (`app+0x58 = 0`), 4 → 0x11, 7 → **page 7**, 8 → 0x10 (`app+0x58 = 0`), everything else (so 0) → **page 0xb**, `app+0x5c = 0`. On PC, `0x450aa0` only returns 0 or 7 |
| +1 frame | page 0xb, handler `0x405276` | `app+0x5c` (wait frames) is 0 ⇒ immediately `vt[5]()` = read `0x450be0`: **false** (can't be opened, or version ≠ 0x11004: "Save file Woody.sav is obsolete..." to the disabled logger) → **page 0xa**; true → **page 2**. So page 0xb (1 item: empty string, header) shows for exactly one frame |
| … | page 2 (§3) | pick a slot; confirming sets result 10 + i after 0.5 s |
| | handler `0x4052ac` | table `0x405bf0`/`0x405bd8`: 10..13 → slot 0..3; **24 → `0x4057b9`: `app+0x68 == 0` ⇒ page 1** (else pause menu `0x404d80`); 14..23 nothing. Slot s: `0x456df0(s, app+0x48)` (0x14a4 B slot → active save struct), `[0x4c2c54] = 0x456e60(s)` (music volume of that slot), `[0x4c2c50] = 0x456e70(s)` (effects volume), `[0x5e618c]+4 = 0x456e80(s)` (vibration, if the object exists), `0x468f50()->vt[0x54](sfx)`, `->vt[0x5c](music)`, **page 3** |
| … | page 3 (§4) | carousel; PLAY on an unlocked character: result 14..17 after 0.5 s |
| | handler `0x4056c8` | table `0x405cd0`/`0x405cb4` on `r − 4`: **4 → page 4** (high scores), **14 → `RequestLevel(0.4, 1, 1, 0)`** (WWS), **15 → (0.4, 0xb, 1, 0)** (KWS), **16 → (0.4, 0x12, 1, 0)** (SWS), **17 → (0.4, 0x19, 3, 0)** (BlackBox, state 3), **24 → page 1** |
| +0.4 s | `0x401590` | level switch (GAMEFLOW §4.2): `LoadLevel` sets `cfg+0x380 = byte_404830[level]` = 0 / 1 / 2 / 0 ⇒ Perso reads **block `cfg+0x380` of the just-loaded save struct** (lives, $, charges, health; GAMEFLOW §6.2). Spawn = the `.ins` position of the Perso in the hub: `app+0x6c` = 0 (House), so `1084 GetPrevLevel` returns 0 and the hub object 297 does nothing (GAMEFLOW §4.5). Fade-in 1.0 s |

On page 3, PLAY's validate additionally sets **`page1+0x38 = 0`** (`0x45ef8a`): whoever returns to the title screen
after playing gets page 1 again without the iris.

### 1.2 What "back" does
| page | back (action 5 or `[0x5e6194]->vt[5](0)`) |
|---|---|
| 1 while closing | blocked (`+0x24`) |
| 0xb | – (one frame) |
| 7, 0xa, 0xf, 0x10, 0x11 | list class `vt[14]` = `0x446a70` → result 24, but the handlers (`0x405075` resp. `0x40524c`) only react to **5** = "Continue" ⇒ back does nothing |
| 2 | `0x45bb30` → `0x45bb40`: **SoundFx 0x3f**, iris 0.37 → 0 in 0.5 s, panels slide out, after 0.5 s result 24 → page 1 |
| 3 | same (`0x45bb40`), on result delivery (`0x45e870`) the 8 characters are hidden → page 1 |
| 1 (return) | enter `0x460070`: SoundFx 0x3f, selection 0, iris 0 → 0.85 in 0.5 s (since `+0x38` is still 1), `+8 = 0` |

### 1.3 Error pages (list class, y fraction 0.4, S = 30, no iris)
All five: `vt[7]` = `0x446e10` (0.4 ⇒ y = 192), first selectable item = "Continue" (result 5). Background: table
`0x405af8` gives these pages case 3 = **half-black plane `0x80000000` over the 3D scene** (also in House).

| page | vtable | items (`0x0002xxxx`, flag 2 = header) | Continue → |
|---|---|---|---|
| 7 | `0x4aa988`, table `0x4b5828` | 64 "No game saved on hard drive", 65 "Choose new game", 66 "", 1 "", **4 Continue** | page 1 (`0x405075`) |
| 0xa | `0x4aa834`, `0x4b5918` | 60 "Load failed.", 1 "", 4 Continue | page 1 (`0x405075`) |
| 0xb | `0x4aa7b4`, `0x4b5948` | 1 "" (wait page, 1 frame) | – |
| 0xf | `0x4aa6f4`, `0x4b59b8` | 77/78/79 (PS2: "memory card … is not inserted …"), 1, 4 | `app+0x58 == 0` ⇒ page 1, `== 1` ⇒ page 6 (`0x40524c`) |
| 0x10 | `0x4aa6b4`, `0x4b5a08` | 80..83 (PS2: "corrupted data …"), 1, 4 | same |
| 0x11 | `0x4aa674`, `0x4b5a68` | 69/70 (PS2: "unformatted"), 1, 4 | page 1 |

0xf/0x10/0x11 are unreachable on PC (`0x450aa0` never returns 3/4/6/8). `app+0x58` remembers whether the error came
from the loading (0) or saving chain (1); `app+0x5c` = wait frames before reading/writing (loading 0, writing 2).

## 2. The two base classes

### 2.1 Panel page `0x45b830` (vtable `0x4ab2e4`; also page 1 and 0x1e)
| field | meaning |
|---|---|
| `+0x14` | deferred result |
| `+0x18` | closing kind: 0 = back (yields 24), 1 = confirm (yields `+0x14`) (`0x45baa0`) |
| `+0x1c` | time since opening/closing/rotation started (s) |
| `+0x20` / `+0x21` | "slide out" / "slide in" (texts on page 3) |
| `+0x22` | closing in progress; `+0x23` = opening in progress (until `+0x1c ≥ 0.5`); `+0x24` = input on slot |
| `+0x28` | iris time; `+0x2c` iris object; `+0x30` iris target (**0.37**); `+0x34` duration (0.5); `+0x38` iris on |

- **Enter `0x45b8c0`**: list-base reset (`0x4464c0`), `+8 = 0.5` (input delay), **SoundFx 0x3f** (ref 111 in
  `Common/<character>.rck`, vol 50, 2D — TITLE §5.4), iris **0 → 0.37 in 0.5 s**, `+0x23 = +0x21 = 1`.
- **Confirm-close `0x45bae0`** / **back-close `0x45bb40`**: SoundFx 0x3f, iris 0.37 → 0 in 0.5 s, `+0x22 =
  +0x24 = +0x20 = 1`, `+0x1c = +0x28 = 0`, `+0x18` = 1 resp. 0.
- **Draw `0x45b990`** (`vt[1]`): first frame of opening (`+0x28 == 0`): draw iris closed; `+0x28 += dt`,
  `+0x1c += dt`; opening done at `+0x1c ≥ 0.5`. `vt[16]` (pre-draw); animate iris (`0x477920`, or a fully
  black plane if `+0x30 ≤ 0`); if `+0x22` and `+0x28 ≥ +0x34`: `vt[18]` (deliver result); content `vt[17]` only
  while not (`+0x24` and `+0x28 ≥ +0x34`).
- Iris hole at 0.37: inner radius `0.37·0.99·480 = 175.8` px around (320, 240); black outside it (TITLE §5.4).

### 2.2 Slot list `0x45d2a0..0x45dcf0` (shared by page 2 and 5)
- Enter `0x45d2b0` = panel-enter + for s = 0..3 (`0x45d2e0`): `slot = [0x5e5a48]->slot(s)` (`0x456da0`),
  bytes `+0x4c+s` / `+0x50+s` / `+0x54+s` = `0x4509b0(0/1/2)` (character unlocked: Woody always; Knothead = done(W2D);
  Splinter = done(W3D)), `+0x58+4s = 0x4501f0(slot)` = **percentage** (0 = empty).
- Percentage `0x4501f0` = `(P0 + P1 + P2) / 3` (int, truncated) with `0x450050(c)` = sum of weights of the completed
  levels: Woody W1A 6, W1B 7, W2A 8, W2B 9, W2D 12, W3A 13, W3B 14, W3C 15, W3D 16; Knothead K1A 14, K1R 15, K2A 16,
  K2R 17, K3A 18, K3R 20; Splinter S1A..S3R the same (each 100 total).
- Sliding `0x45d330` (in `vt[17]`): while `+0x28 ≤ 0.5`: `off = closing ? +0x1c·(−600) : (0.5 − +0x1c)·(−600)`,
  else 0; slots 1 and 3 get x-offset `off`, slots 2 and 4 `−off` (slide left resp. right off-screen and back),
  the title `y = 415 − off` (from the bottom). Linear.
- **Slot drawing `0x45d530(i, xoff)`** (i = 1..4; positions table `0x4ab420`: **(16,48), (488,48), (16,324), (488,324)**):
  - `sel = (+0x3c == i)`; color `c = sel ? 0xfe808080 : 0xfe202020` (0x80 = 1.0; so unselected is at 25%).
  - **Panel** `RectVirtual(x, y, 137, 108, source 0,0,137,108, c, House.rck img. 0, flag 4 = additive)`; green
    neon border: three small circles (faces) above a large one (percentage). The selected slot blinks:
    `+0x40 += dt`; ≤ 0.3 s: no panel, text color `0xfe202020`; 0.3..0.6 s: panel with `0xfe808080`, text
    `0xfe808080`; above 0.6 back to 0.
  - **Ring** `(x+45, y+50.5→50, 49x49)`, source `(0, 108.5, 49, 49)` of House.rck image 0 (gold ring), color `c`, additive.
  - If the slot is **not empty**: faces from `Common/<character>.rck` image **61**, 64x64, color `c`, flag 8
    (alpha): Woody source (0,0) at **(x+33, y−7)**; Knothead (if unlocked) source **(0,63)** at **(x−4, y+8)**; Splinter
    (if unlocked) source **(63,0)** at **(x+69, y+7)**.
  - Label (`0x45d3c0`): empty ⇒ string **18 "FREE"**, else **26+i−1 "Save i"**; size 28, shrunk until width
    ≤ 137 (`0x45dc90`: −1 per step, min 10); top slots: `x = slot.x + 68.5 − w/2`, `y = 48 + 108 = 156` (below the
    panel); bottom slots: `y = 324 − cell height` (above the panel). Color = the text color above.
  - **Percentage**: `itoa(p) + string 7 "%"`, size 16 (at p ≥ 100: 12), centered at **(x+69.5, y+75)**,
    color `sel ? 0xfeff1400 : 0xfe3f0500` (red). Also drawn for an empty slot ("0%").

## 3. Page 2 — slot selection for loading (class `0x45dd00`, 0x68 B, vtable `0x4ab468`)

| vt | function | |
|---|---|---|
| [2] | `0x45dd20` → table `0x4b5e40` | 1 item: 25 "Select game" (not drawn via the list) |
| [9] validate | `0x45bad0` → [19] `0x45dd70` | only if `pct[sel] > 0`: confirm-close (`0x45d2a0` = `0x45bae0`, **SoundFx 0x3f**), `+0x14 = 10 + sel − 1`, and `0x45e620(page 3)` (carousel back to Woody, §4.2). Empty slot: `+0x14 = 0`, nothing happens (no sound) |
| [10] down | `0x45dfb0` | 1 → 3, else 4; 2 → 4, else 3 (only to non-empty slots; not while closing) |
| [11] up | `0x45e000` | 3 → 1, else 2; 4 → 2, else 1 |
| [12] right | `0x45df20` | 1 → 2, else 4; 3 → 4, else 2 |
| [13] left | `0x45df60` | 2 → 1, else 3; 4 → 3, else 1 |
| [14] back | `0x45bb30` → `0x45bb40` | §1.2 |
| [15] enter | `0x45dd30` | slot-list enter (§2.2), `+0x40 = 0`; **the selection stays as it was from the previous visit** (ctor: 1) and slides on to the first non-empty slot ≥ the current one, max 4 |
| [17] content | `0x45de10` | slots (`0x45d330` → [21] `0x45e050`) + title |
| [21] slot | `0x45e050` | `0x45d530` + for an **empty** slot the red **cross**: `RectVirtual(x+20, y, 108, 108, source 0,160,63,63, 0xfe808080, House.rck img. 0, flag 8)` |

- Navigation is a 2x2 grid, **without sound**, empty slots are skipped and cannot be confirmed.
- Title: string 25 "Select game", size 30 (shrunk to ≤ 330), `x = 320 − w/2`, `y = 415 − off`; first in
  `0xfe808080` at (x, y), over it in **`0xfe801400`** (orange-red) at `(x − 0.05·h, y − 0.05·h)` (shadow
  bottom-right), `h = 0x441980` = cell height.
- No logo (the fade-out of `0x446b00` runs, but page 1 already set the alpha to 0), no half-black plane
  (table `0x405af8`: case 0), the treehouse keeps spinning in the iris hole.
- Edge case: if the remembered selection is past the last filled slot, the loop reads `+0x68` (out of bounds
  of the object) and ends up at 4, possibly an empty slot (**uncertain**, harmless: confirming then does nothing).

## 4. Page 3 — world-selection carousel (class `0x45e560`, 0x148 B, vtable `0x4ab560`, global `[0x5e5adc]`)

### 4.1 Fields
| field | meaning |
|---|---|
| `+4` | list selection (0 = PLAY, 1 = SEE HIGH SCORES) |
| `+0x3c + 0x2c·k` … | **record k** = carousel slot k+1 (k = 0..3), see 4.3 |
| `+0xec..+0x100` | registered characters: n=0 `+0xec`, n=1 `+0xf0`, n=2 `+0xf4`, n=4 first `+0xf8` then `+0x100`, n=3 `+0xfc` |
| `+0x104..+0x110` | the four pedestals (n=5), rank 1..4 in registration order |
| `+0x114` | selected slot 1..4 (int) |
| `+0x118` / `+0x11c` / `+0x120` | carousel position (float) / start position / target |
| `+0x124, +0x128, +0x12c, +0x130` | radius **150**, distance **560**, height **100**, angular spacing **90°** (ctor / enter) |
| `+0x134` / `+0x138` | rotation duration **0.5 s** / rotation time |
| `+0x13c` / `+0x13d` | rotating left / rotating right |
| `+0x140` / `+0x144` | string refs "world" / "part" of the location (`0x45ec50`) |

Registration (message **58** `(inst, n)` → `0x451960` → `0x45e6f0`, House script objects 105–114, all three also
`1200 [slot, 110]`; 105–107 additionally get `45 [slot, 32]` = SetFlags 0x20 = ink outline):

| slot | model (House.ins) | n | role |
|---|---|---|---|
| 105 | 0 (Woody, 91 anims) | 0 | Woody figure, rank 1 |
| 106 | 10 (Knothead) | 1 | Knothead figure, rank 2 |
| 107 | 11 (Splinter) | 2 | Splinter figure, rank 3 |
| 108 | 12 (BlackBox, 2 anims) | 3 | BlackBox figure, rank 4; `0x436ca0(1.0, {1,−1,−1,−1})` |
| 109, 110 | 13 ("?" figure, 1 anim) | 4 | stand-in rank 3 resp. 4 |
| 111–114 | 14 (pedestal, flat red/yellow) | 5 | pedestals rank 1..4 |

Every level load clears the registrations via `0x446410` → `0x45e5f0`; the House script sets them again.

### 4.2 Enter `0x45e660` and reset `0x45e620`
`0x45e620` (ctor and page 2's validate): `+0x118 = +0x120 = 1.0`, `+0x114 = 1` ⇒ **always starts on Woody**.
Enter: panel-enter (**SoundFx 0x3f**, iris 0 → 0.37, input after 0.5 s), `+0x134 = 0.5`, `+0x138 = 0`, not rotating,
`+0x130 = 90`; pedestals to the records; fill the records (`0x45f310`, 4.3); Woody's location (`0x45ec50`); show all 8
record instances (`0x4891d0(1)` = reset offsets, local basis, place, hang in a cell `0x4077f0`); start the
animation of the selected character (`0x45f5d0`).

### 4.3 Records (`0x45f310`, from the active save struct `[0x5e5818]`)
| k | figure (`+0x60`) | pedestal (`+0x64`) | unlocked (`+0x4c`) | sprite (`+0x58`) | `+0x5c` | dark (`inst+0x188 = 1`) |
|---|---|---|---|---|---|---|
| 0 | 105 Woody | 111 | always | 0 | 0 | never |
| 1 | 106 Knothead | 112 | done(W2D) | 2 | 0 | both if not unlocked |
| 2 | Knothead unlocked ? **107 Splinter** : **109 "?"** | 113 | done(W3D) | 1 | 0 | not unlocked: figure 109 normal + pedestal dark; figure 107 + pedestal dark |
| 3 | Splinter unlocked ? **108 BlackBox** : **110 "?"** | 114 | done(S3R) (`0x4509e0(2, 0x18)`) | 1 | **1** | same |

For each unlocked record additionally: `+0x3c` lives, `+0x40` health (float), `+0x44` unique items, `+0x48` charges (block k of the
save; record 3 = BlackBox gets no stats), `+0x50` percentage `0x450050(k)`, `+0x54` location = `0x450790(k)` =
**the first not-yet-reached level** for that character (W1A..W3D / K1A..K3R / S1A..S3R; all reached: 0x19 or 0x18).
A name is thus only revealed once the preceding character is unlocked (see 4.6), a locked character appears as a
dark silhouette. **"Dark"** = `vt[26]` `0x451a40`: `[0x5ac850] = 1`, `[0x5ac854..5c] = 0.1` ⇒ the renderer multiplies
the lit vertex color by **0.1** (`0x43bdfc`; mode 2 = additive, see LIGHTING.md and PERSO_DEATH.md for `[0x5ac850]`).

### 4.4 Placement in camera space (`0x451890` → `0x489780`, `0x489210`)
Every frame (`vt[16]` = `0x45e800`: `+0x138 += dt`, then `0x45edc0`) for each of the 8 instances
`0x451890(pos = +0x118, 90, 150, 560, 100)`:
```c
f     = rank - pos;                                  // rank = inst+0x18c (1..4)
theta = (f * 90.0f + 7.0f) * PI/180;                 // +0x174 in degrees; the front one stands 7° clockwise
off   = ( 150*sin(theta),  26.047f*cos(theta),  -150*cos(theta) );   // 26.047 = 150*sin(10°): ring tilted 10°
inst+0xfc = 560 (distance), inst+0x100 = 100 (height), inst+0x104..0x10c = off
local_rot(inst+0x14c) = R(-10°, theta, 0) * Basis   // 0x489780; Basis +0x128 = rows (1,0,0),(0,0,-1),(0,1,0)
```
`vt[3]` of class 110 (`0x451a30` → `0x489650` → `0x489210`, called by `0x42b400` every frame for each instance):
```c
P = ( off.x, off.y + 100, off.z + 560 );             // "design" camera space: x right, y DOWN, z forward
world = campos + Rcam * ( P.x / 1.0, P.y / 1.3333, P.z / 1.2 );   // [0x5e86ac]+0x154..0x17c = inverse of (R · diag(1/sx, 1/sy, zoom))
inst.rows = normalize(Rcam * local_rot columns / scale)          // orientation = camera ∘ local
inst.pos  = world; relink in cell (0x4077f0)
```
The inverse matrix contains the projection scale (CAMERA.md §5.2: `sx = 1`, `sy = 0.75`, `zoom = 1.2` without
letterboxing), so on screen simply **NDC = (P.x / P.z, P.y / P.z)**, `screen = (320 + 320·ndc.x,
240 + 240·ndc.y)`:

| slot | θ | P | screen (px) | distance (world) |
|---|---|---|---|---|
| front (selected) | 7° | (18.3, 125.9, 411.1) | (334, 313) | 343 |
| right | 97° | (148.9, 96.8, 578.3) | (402, 280) | 482 |
| back | 187° | (−18.3, 74.1, 708.9) | (312, 265) | 591 |
| left | 277° | (−148.9, 103.2, 541.7) | (232, 286) | 451 |

Everything lies within the iris hole (r ≈ 176 around (320,240)). The models are z-up (like Perso: row 2 = up);
with the basis, model-z maps to camera-up and model-y away from the camera, so the front character faces the camera;
each character rotates along with θ (facing outward), plus 10° tilt.

**Verified live** (`tools/wverify.py --probe carousel`, title → Load game → slot → page 3, carousel at rest on positions 1, 2
and 3; `--sav` points the game's `Woody.sav` at a copy of the port's save): `0x451890` calls `0x489780(a = −10°, b = θ,
c = 0)` (pushed `0`, `θ` in radians, `0xbe32b8c3` = −0.17453), with θ = (rank − pos)·90 + 7 (−83°, −173°, … for ranks
before `pos`: no wrap into 0..360). `0x489780` builds, in the row-vector convention of `0x489670` (`out = X·Y`),
`L = Rx(a) · Ry(b) · Rz(c) · Basis` with `Rx` rows (1,0,0), (0,cos a,sin a), (0,−sin a,cos a), `Ry` rows (cos b,0,−sin b),
(0,1,0), (sin b,0,cos b), `Rz` rows (cos c,sin c,0), (−sin c,cos c,0), (0,0,1), and stores it at `+0x14c` (θ = 7°:
rows (0.9925, −0.1219, 0), (−0.0212, −0.1724, −0.9848), (0.1200, 0.9775, −0.1736), identical in the trace). `0x489210` then
takes the **columns** of `L` (`0x489213..0x48926c` read `+0x14c/+0x158/+0x164` for row 0 …) as the model axes, puts each
through the scaled inverse camera matrix `[0x5e86ac]+0x154` (rows = world vectors of design x, y, z with lengths 1, 0.75,
0.833, verified) and **normalises** it. In design space (x right, y down, z ahead) model x = (cos θ, −sin θ·sin 10°,
sin θ·cos 10°), model y = (−sin θ, −cos θ·sin 10°, cos θ·cos 10°), model z = (0, −cos 10°, −sin 10°): exactly the port's
axes. Positions `+0xc` = campos + M·(off.x, off.y + 100, off.z + 560) match the table to 0.01.

**Port** (`car_place` in `src/main_engine.c`): position exactly as above (camera space
→ world with `R·x − U·(0.75·y) + F·(z / 1.2)`, `R`/`U`/`F` = right/up/forward of the title camera). Orientation:
model-x → `(cos θ, 0, sin θ)`, model-y → `(−sin θ, 0, cos θ)`, model-z → `(0, −1, 0)` (up), then 10° around the camera-x
axis so the front of the ring dips (the same tilt as the offsets `26.047·cos θ`), then - since 2026-09-26, after the live
check - through the same scale (y · 0.75, z / 1.2) and normalised per axis like `0x489210`, so the rows are no longer
exactly orthogonal. Against the traced instance rows the port's axes now agree to 2·10⁻⁴; the earlier pure rotation was
1.1-1.7° off. Result: characters upright, each looking outward from the ring's center, the front one
straight into the lens, the pedestals as ellipses seen from above; Woody stands with his feet on his pedestal. The "?" figure and the
BlackBox crate float noticeably above their pedestal in frame 0 of their anim 0 (that's how the data has it; not
comparable without footage of the original). The placement runs every frame after the title-camera path and before rendering.

**The camera goes nowhere.** Page 3 never calls `SetMode`; the title camera (mode 0x80, circle around the
treehouse, TITLE §2) just keeps turning, the carousel hangs fixed in front of the lens and the scenery turns behind it. The
file positions of 105–114 (x ≈ −10500, y ≈ 4000, z ≈ 4000–5000) and the intro vector (−11299, −7, 601) of slot 115
play no role.

### 4.5 Input and rotation
| action | function | effect |
|---|---|---|
| 1 right | `vt[12]` `0x45efb0` | only if not rotating and not closing: `a = +0x118` (4 → 0), target `a + 1`, `+0x13d = 1`, `0x45f690` (previous character: anim queue `{1,0,0,0}` = back to anim 0 after anim 1; BlackBox: `0x436ca0`), `+0x1c = 0`, `+0x20 = 1` (texts slide out) |
| 0 left | `vt[13]` `0x45f050` | mirror image: `a` (1 → 5), target `a − 1` |
| 2/3 up/down | `0x45bb90`/`0x45bba0` | list PLAY ↔ SEE HIGH SCORES (wraps, not while closing) |
| 0xc confirm | `vt[9]` `0x45ee50` | see 4.7 |
| 5 back | `0x45bb30` | §1.2 |

**Animation** (`0x45f1d0` right, `0x45f0f0` left, every frame): `pos = start ± t/0.5` — **linear, 90° in 0.5 s =
180°/s, no easing**. At `t ≥ 0.25`: `+0x114` = the target (5 → 1, 0 → 4), location strings (`0x45ec50`), `+0x21 = 1`,
`+0x20 = 0` (texts slide back in). At `t ≥ 0.5`: `pos = target` (normalized to 1..4), `+0x1c = 0`,
`+0x21 = 0`, and `0x45f5d0`: the new character gets **clock = now, speed 3.0, queue {1, 2, 2, 2}** = .ins anim 1
once, then anim 2 in a loop (model 0: anim 1 = 160 frames = 0.27 s at speed 3, anim 2 = 360 frames = 0.6 s);
BlackBox: `0x436ca0(1.0, {1,−1,−1,−1})`. While rotating, no new rotation can start; there is **no menu sound**
on rotation (any sounds are in the animations themselves).

Checked (port: `car_anim_sel` / `car_anim_prev`):
- `0x436ca0(inst, f, s0..s3)` = clock `inst+0xa8` = now, queue `+0xb0..+0xbc` = s0..s3, speed `0x42e290(f · [0x4a988c])`
  with `[0x4a988c]` = **3.0**; the BlackBox thus also plays anim 1 at speed 3 and then holds on the last frame.
- `0x45f5d0` (new character): first speed 3.0 and clock = now, then per role (`inst+0x184`): 0..2 → queue {1,2,2,2},
  3 → `0x436ca0(1.0, {1,−1,−1,−1})`, 4 ("?") → nothing more (anim 0 once at speed 3).
- `0x45f690` (departing character): role 3 → `0x436ca0(1.0, {1,−1,−1,−1})` (restart); else only **slot 0 = 1 and
  slots 1..3 = 0 on the running clock** (no restart, speed stays 3): the phase of anim 1 follows from the clock, then
  anim 0 loops.
- The other characters keep their ctor state (anim 0, speed 0 = still frame 0) until they get their turn in front.

### 4.6 2D layer (`vt[17]` = `0x45e8a0`)
`off` = slide: `+0x21 ? (0.5 − +0x1c)·(−600) : +0x20 ? +0x1c·(−600) : 0` (in/out on opening and around every rotation).

| element | function | content / placement |
|---|---|---|
| name | `0x45fe30(off)` | slot 1 "WOODY" (30), 2 "KNOTHEAD" (31) if Woody unlocked (always), 3 "SPLINTER" (32) if Knothead unlocked, 4 "BONUS" (33) if Splinter unlocked, else **34 "???"**; size 30, `x = 320 − w/2`, `y = 16 + off`; shadow `0xfe808080` at +0.05·h, text `0xfe801400` |
| stats left (only unlocked and `+0x5c == 0`, so not BlackBox) | `0x45f6f0(off)` | HUD sprites (table `0x4ab658`, flag 8): face `+0x58` at (16+off, 113); digit sprite 8 at (56+off, 153) with **lives − 1**; health: sprite 7 x `ftol(health)` at (31+off+24i, 78); sprite 3 ($) at (22+off, 213), digit at (56+off, 258) with **unique items**; sprite 6 (charge) at (22+off, 308), digit at (56+off, 353) with **charges**. Numbers (`0x45eb00`/`0x4605b0`): red `0xfeff0000`, size 17 (≥ 100: 22.5), centered at (72+off, 169 / 274 / 369) |
| right (same) | `0x45f790(−off)` | center x = 565 − off: 43 "Game cleared" (20, white `0xfeffffff`) at y 110; below it "NN%" (50, `0xfeff1400`); if NN < 100: 44 "Location" (20, white) at y 240, then world (25, `0xfeff1400`) at y 270 and part below it. Everything shrunk to width ≤ 115 |
| list | `0x446640` (only unlocked, not opening/closing) | items `0x4b5e50` 42 "PLAY" (result via validate) and `0x4b5e60` 41 "SEE HIGH SCORES" (flag 0x21: x0.8); at the BlackBox slot, item 1 becomes string 1 / header (`0x45e800`) and `+4 = 0`. S = 30·0.8 = 24, y fraction `0.85 + slide·0.2/0.5` (`0x45ffa0`), centered, the selected item blinks (TITLE §5.1) |
| arrows | `0x45fac0` | Common img 63 source (0,96,31,31) = **HUD sprite 15, a yellow arrow outline** (alpha 0 in the data, so only visible additively: resolves HUD_TEXT §9 point 6), doubled to 62x62, **additive**; right at (470 + s, 209), left mirrored at (108 − s, 209), `s = (closing ? +0x28 : 0.5 − +0x28)·600`. Grayscale 128; the arrow in the rotation direction dims to 32 and back during rotation (`128 − t·96/0.25`, then `(t − 0.25)·96/0.25 + 32`) |
| total score | `0x45e8a0` | 130 "Total Score :" (15, `0xfeffffff`) at (16, 410), below it the number `0x450a10` (sum of all `best` + `save+0x14a0`) |

Details (checked for the port, `hud_carousel` in `src/hud.c`):
- Numbers on the left (`0x45eb00` → `0x45f2a0`): size 17; a value ≥ 100 sets `30 × 0.75 = 22.5` and that size **stays**
  for the following numbers of the same frame.
- Right (`0x45f790`): "NN%" sits at `110 + cell height` of the (shrunk) size-20 line; the part (`+0x144`) is
  measured and drawn at the size of the world line (no separate shrink), at `270 + cell height`.
- Name (`0x45fe30`): the gray copy `0xfe808080` at `(x + 0.05·h, y + 0.05·h)`, over it `0xfe801400` at `(x, y)`.
- Arrows: source `[0x4ab784..0x4ab794]` = (0, 96, 31, 31), image 63; gray `128 − t·96/0.25` while sliding out,
  `(t − 0.25)·96/0.25 + 32` while sliding in, only for the arrow of the rotation direction. **Port choice**: the
  source trimmed by half a texel, because the row above it (y 95) is white and the bilinear filter at 2x scale
  made a line above the arrow.
- Total score: string 130 size 15 at (16, 410), the number at (16, 410 + cell height), both `0xfeffffff`.
- The list on page 3 is not part of the port's ordinary list navigation: up/down toggles PLAY ↔ SEE HIGH SCORES,
  at the BlackBox slot item 1 is a header and the selection stays on PLAY.

Location strings `0x45ec50(level)` (table `0x45ed60`): W1A/K1A/S1A = 47 "Space" + 51 "Part A"; W1B = Space +
52 "Part B"; K1R/S1R = Space + 55 "Race"; W2A/K2A/S2A = 48 "Pirate" + Part A; W2B = Pirate + Part B; **W2D = Pirate +
53 "Part C"**; K2R/S2R = Pirate + Race; W3A/K3A/S3A = 49 "House" + Part A; W3B/W3C/W3D = House + Part B/C/D;
K3R/S3R = House + Race; hubs: nothing (the function then leaves the previous strings in place; they're only
drawn when NN < 100). Full list: W1A 47+51, W1B 47+52, W2A 48+51, W2B 48+52, W2D 48+53, W3A 49+51, W3B 49+52, W3C 49+53, W3D 49+54
("Part D"), K1A/S1A 47+51, K1R/S1R 47+55, K2A/S2A 48+51, K2R/S2R 48+55, K3A/S3A 49+51, K3R/S3R 49+55.

### 4.7 Confirming (`0x45ee50`)
- **PLAY** (item 0): only if the record is unlocked and not already closing: iris 0.37 → 0 in 0.5 s (`+0x28 = 0`), **no
  sound**, `+0x22 = +0x24 = 1`, `+0x18 = 1`, `+0x14 = 14 + k` (Woody 14, Knothead 15, Splinter 16, BlackBox 17),
  `+0x1c = 0`, `+0x20 = 1`, `page1+0x38 = 0`. After 0.5 s `vt[18]` = `0x45e870`: deliver the result **and hide the 8
  instances** (`0x4891d0(0)` → `0x407850`).
- **SEE HIGH SCORES** (item 1): unlocked, not closing, not slot 4: same but `+0x14 = 4` and `[0x5e5a8c]+0x3c = k`
  (page 4 = high scores of character k, §4.8; back → page 3, `0x405749`). Unlike PLAY it leaves `page1+0x38` alone.
  **Port**: ported (`carousel_update`, result 4 → `menu_enter(4)`).
- Locked character: nothing (no sound).

### 4.8 Page 4 — high scores (class `0x45bfb0`, 0x40 B, vtable `0x4ab368`, global `[0x5e5a8c]`)

A panel page (base `0x45b830`) with only a few slots of its own:

| vt | address | what |
|---|---|---|
| [2] | `0x45cd60` | item table `0x4b5e20`: one item `{string 1 "", flag 1, result 5}`; never drawn (vt[17] does not call `0x446640`) |
| [3] / [4] | `0x46c320` / `0x45cd50` | count 1 / page id 4 |
| [7] | `0x45cd70` | y fraction 0.8 (`0x4a987c`, unused) |
| [9] → [19] | `0x45bad0` → `0x462c60` | confirm = `ret`: **Enter does nothing** |
| [14] → [20] | `0x45bb30` → `0x45bb40` | back: SoundFx 0x3f, iris `+0x30` → 0, result 24 after 0.5 s |
| [15] | `0x45bfd0` | enter: panel enter `0x45b8c0` (SoundFx 0x3f, input after 0.5 s), then **`+0x30 = 0`** |
| [17] | `0x45bff0` | content |

Because the iris target `+0x30` is 0, the panel base `0x45b990` never animates the ring but lays a **full black
rect** (`0x45ba29`: `RectVirtual(0, 0, 640, 480, 4× 0xfe000000, flag 8)`); the 3D title scene is gone for the whole page.
Handler `0x405749`: result 24 → page 3 (carousel enter again: iris 0 → 0.37, the figure's animation restarts), any
other result → nothing. `+0x3c` = the character k, written by page 3 (`0x45eec1..0x45ef01`).

Content `0x45bff0`: with `t = +0x28`, `T = +0x34 = 0.5`: while `t ≤ T`, `a = (closing ? t : T − t)·600/T` and
`b = (closing ? T − t : t)/T`, else `a = 0`, `b = 1`. Then:

1. `0x45c090(b)`: `RectVirtual(320 − 157.5b, 240 − 186b, 315b, 372b)` of **House image 1 (the title logo)**, source
   (0, 0, 209, 247), 4× **`0x80202020`** (a quarter bright, half transparent), flag 8: a dim logo growing out of
   the centre as backdrop.
2. `0x45c3d0(−a)`: three 24×24 column heads at y 80: House image 2 (the clock / enemy-face sheet) (52, 0, 36, 36) at
   x `580 − 12 − a` and (0, 0, 51, 51) at `520 − 12 − a`, both additive (flag 4); Common image 64 (0, 89, 24, 24) (the
   W ball of the health row) at `440 − 12 − a`, flag 8. Then per character (`k` 0/1/2) `0x45ca40` / `0x45cd80` /
   `0x45cf90`: the levels W1A..W3D / K1A..K3R / S1A..S3R in play order, each only if done (`0x4509e0(k, L)`), **stopping at
   the first one not done**, row r = 0, 1, 2 … → `0x45c510(−a, world, part, best, r, k, L)` with the pairs of the
   location table (§4.6).
3. `0x45c150(a)`: string 45 "HIGH SCORES", size 28, `0xfeff1400`, at (320 − w/2, 30 − a); `0x45c230`: the HUD face
   sprite (k 0 → 0, 1 → **2**, 2 → **1**, the same faces as the carousel records) at (320 − w/2 − 64 − 10, 16 − a) and
   mirrored (negative source width) at (320 + w/2 + 10, 16 − a).

A row `0x45c510` (y = 105 + 37r, font of the level bank, size 18, white `0xfeffffff`):
- `0x45d1a0(off, y, 22)`: blue bar (off, y, 256, 22), left `0x800000ff` → right `0x000000ff` (the HUD bar).
- text y = y + (22 − cell)/2; label "world" + space (word 1 of string 40) + "part" at x = 20 + off; the pen moves on;
  score "best" + space + string 46 "points" at `max(pen + 3·w(space), 220 + off)`.
- not for the races (L = 13, 15, 17, 20, 22, 24): time `m:ss` (`_ftol`, string 10, a 0 below 10 s) centred on
  `off + 580`, enemies "rec+0x2c / rec+0x28" (`0x4502c0` / `0x450290`) centred on `off + 520`;
- always W's "rec+0x34 / rec+0x30" (`0x450320` / `0x4502f0`) centred on `off + 440`. (The centring multiplies by
  `font+0x2c / 640` = 1.)

So texts and rows slide in from the left over 0.5 s, the title and faces come down from above, and the logo
grows; closing runs the same backwards. **Port**: `scores_draw` (`src/main_engine.c`) → `hud_scores` (`src/hud.c`);
House image 2 is loaded as `H.sheet2`.

## 5. Page 5 (slot selection for saving) and 0x17 — briefly

Class `0x45e1f0` (0x68 B, vtable `0x4ab4c0`), same slot list as page 2 with these differences:
- Enter `0x45e230`: `+0x38 = 0` during the base enter, then iris **1.0 → 0.37** in 0.5 s (you're coming out of the
  game) and `+0x38 = 1`. Title string **24 "Select save"** (`0x45e2e0`).
- **All slots selectable**, grid without skipping (`0x45e430` down 1,2 → +2; `0x45e450` up 3,4 → −2;
  `0x45e3f0` right 1,3 → +1; `0x45e410` left 2,4 → −1). Slot drawing = only `0x45d530` (**no cross**; empty =
  "FREE" + "0%").
- Validate `0x45e270`: confirm-close (SoundFx 0x3f) + `+0x14 = 10 + sel − 1`, but the iris then goes back **open**
  (0.37 → 1.0 in 0.5 s).
- Handler `0x4054ac`: 24 (back) → page 6; slot s → `app+0x60 = s`; slot occupied (`0x4501f0 ≠ 0`) → **page 0x17**
  (61 "Are you sure you want to overwrite this save?" / 5 Yes / 6 No, handler `0x405586`: Yes → write, **No and
  back → page 6**); empty → volumes `0x456e40(music, sfx, s)` + vibration `0x456e90`, `0x456dc0(save → slot s)`,
  page 0xc with `app+0x5c = 2` → after 2 frames `vt[4]` writes → 8 "Game Saved" / 9 "Save failed."
  (the handler `0x405662` counts `app+0x5c` down once per frame, `0x4052a4`, and acts on the frame after it hit 0; the
  read pages 0xb (`0x405276`) and 0xe (`0x405483`) start at 0, so they stand for one frame; all three are empty pages
  over the dim layer. Port: `menu_wait`).
- Page 6 "Yes" → `vt[3]`: file doesn't exist ⇒ `0x456e20` (clear 4 slots) → page 5; exists ⇒ page 0xe
  (1 frame) → read: succeeded → page 5, failed → page 6. Music object `[0x5e61a4]->vt[0x50](0.5)` when opening
  page 5 and `vt[0x54](0.5)` after a successful write (meaning **uncertain**, presumably ducking/restoring).
- **Correction to GAMEFLOW §5**: page 8 "Game Saved" + Continue does **not** go back to 6 but leaves the menu
  (`0x4056c0` → `0x405364`: `0x454050(perso)` + state 1); only page 9 "Save failed." goes back to 6.

## 6. `Woody.sav` byte-exact

Working memory, **0x52c4 bytes**, little-endian, one `fread`/`fwrite` of `mgr+4` (`0x450be0`, `0x450b30`):

| offset | type | content |
|---|---|---|
| 0x0000 | u32 | version **0x00011004** (always set on write; on read, ≠ ⇒ fails) |
| 0x0004 + s·0x14a4 | 4 x slot | s = 0..3, each a copy of the active save struct |
| 0x5294 | u32[4] | **music volume** per slot (`[0x4c2c54]`, options item 39 "Music volume") |
| 0x52a4 | u32[4] | **effects volume** per slot (`[0x4c2c50]`, item 38 "Sound FX volume") |
| 0x52b4 | f32[4] | vibration per slot (`[0x5e618c]+4`, item 132) |

(GAMEFLOW §6.1 calls 0x5294 "sfxvol" and 0x52a4 "musicvol": that's backwards, see the options item table `0x4b5ec0`.)

Slot / save struct (0x14a4 B):

| offset | type | content |
|---|---|---|
| +0x0000 | i32 | "checksum" (see below) |
| +0x0004 | u32 | 0x11004 |
| +0x0008 | u32 | 0 |
| +0x000c + c·0x6dc | block | c = 0 Woody, 1 Knothead, 2 Splinter |
| block+0x00 | i32 | lives (new: 9) |
| block+0x04 | i32 | unique items |
| block+0x08 | i32 | special charges |
| block+0x0c | f32 | health (new: 3.0; ≤ 0 becomes 1.0 on load) |
| block+0x10 + L·0x3c | record | L = level index 0..28 |
| rec+0x00 | i32 | best score |
| rec+0x04 | u8 | done |
| rec+0x05 | u8[32] | "unique item n of this level collected" (`0x450730`/`0x450760`; n = the type-36 sequence number, BONUS.md §2.5; the writer does not check n < 32). Port: `uniq_flag` / `uniq_update` |
| rec+0x25 | u8[3] | padding |
| rec+0x28 | i32 | stat[0] (`app+0x74`, total enemies) |
| rec+0x2c | i32 | **stat[2]** (`app+0x7c`, enemies defeated) |
| rec+0x30 | i32 | **stat[1]** (`app+0x78`, total bonuses) |
| rec+0x34 | i32 | stat[3] (`app+0x80`, bonuses collected) |
| rec+0x38 | f32 | time |
| +0x14a0 | i32 | extra score (added in `0x450a10`; no writer found) |

Note the order of +0x2c/+0x30 (`0x4503b0` writes `app+0x7c`, `0x4503e0` writes `app+0x78`; readers `0x4502c0`/`0x4502f0`).

**Checksum**: `0x450030` = sum of all 0x14a4 bytes as *signed char* (starting from 0). The only caller is the
reset `0x44ffa0`; there is **no verification** on read and no recalculation on write. A fresh struct always gives
**432** (0x1b0: version 4+16+1, per block lives 9 + health bytes 0x40+0x40); since the active struct only ever
arises via reset (boot, New game, game over) or a slot copy, in practice it's always 432 or whatever the source
slot had. A port may thus ignore the field; anyone wanting a byte-exact write should set 432.

**Empty slot** = a reset slot (lives 9, health 3, everything else 0) ⇒ percentage 0 ⇒ "FREE". The volumes of
never-written slots are uninitialized memory (the slot manager is created with `new`; **uncertain** whether that's zero).

## 7. Advice for the port: four slots

The port currently has one `woodyre.sav` with `{u32 'WSV2'; SaveChar chr[3]}` (`src/main_engine.c` around `g_save`),
where `SaveChar` is a subset of the original block (no `rec+0x05` unique-item bits, `stats` in the order of
`app+0x74`).

**Recommendation: adopt the original format 1-to-1** as both the in-memory model and the file format, but under the
port's own name `woodyre.sav`:
1. One `#pragma pack(1)` struct `WoodySav { u32 version; SaveSlot slot[4]; u32 music[4], sfx[4]; f32 vib[4]; }`
   with `static_assert(sizeof == 0x52c4)` and `SaveSlot { i32 sum; u32 ver, zero; SaveBlock chr[3]; i32 extra; }`
   (0x14a4), `SaveBlock { i32 lives, unique, charges; f32 health; SaveRec rec[29]; }` (0x6dc),
   `SaveRec { i32 best; u8 done, uniq[32], pad[3]; i32 s0, s2, s1, s3; f32 time; }` (0x3c).
   The active struct (`g_save`) becomes a `SaveSlot`; the current fields map directly (`done[L]` → `rec[L].done`, …).
2. Version 0x11004 is the magic. The original does nothing with the checksum, so reading a real `Woody.sav` works
   without extra work: offer **import** — if `woodyre.sav` is missing and there's a `Woody.sav` next to
   `game/Woody.exe` (or in the working directory), read that instead. Never overwrite the original `Woody.sav`
   (the user might still need it for the original); write only to `woodyre.sav`.
3. Convert an existing `WSV2` file to slot 0 once (volumes = current options), so nobody loses their progress;
   never write WSV2 again after that.
4. The port extras that don't exist in the original (e.g. `g_unlock_all`) belong in `Woody.cfg`/the command line,
   not in the save. That way a port save also stays readable by the original (just rename the file).

A custom format has no advantage: everything the port keeps is already in the original, and the unique-item bits
(`rec+0x05`) will be needed by the port eventually anyway (BONUS.md).

## 8. Port recipe

Basis: `src/main_engine.c` (title menu: `title_page`, `title_sel`, `cont`, `request_level`) and `src/hud.c`
(`page_items`, `hud_menu_page`, `hud_title_draw`, `quad`, `k_spr`). Iris and panel base are shared with
Options/New game: a single implementation.

```c
/* ---- hud.c: House.rck image 0 as the menu texture (level_item now loads index 0..4 as sky, without alpha) */
if (type == 1 && index == 0) { /* like the logo via common_item: RGBA, top row first, alpha kept */ H.slotsheet = ...; }

/* ---- shared panel page (0x45b830) */
typedef struct { float t, ti; int opening, closing, lock, slide_out, slide_in, kind, result; float iris_v, iris_from, iris_to, iris_t; int iris_on; } Panel;
static void iris_set(Panel *p, float from, float to) { p->iris_from = from; p->iris_to = to; p->iris_t = 0; }
static void panel_enter(Panel *p) { memset(p, 0, sizeof *p); p->opening = p->slide_in = 1; p->iris_on = 1; iris_set(p, 0, 0.37f); audio_fx(63, NULL, NULL); /* input only after 0.5 s */ }
static void panel_close(Panel *p, int kind, int result, int sound) { if (sound) audio_fx(63, NULL, NULL); iris_set(p, 0.37f, 0); p->closing = p->lock = p->slide_out = 1; p->t = p->ti = 0; p->kind = kind; p->result = result; }
/* per frame: ti += dt; t += dt; if (opening && t >= .5f) opening = slide_in = 0;
   iris_v = lerp(from, to, min(iris_t/0.5,1)); draw ring: black, inner radius iris_v*0.99*480, outer radius 475, center (320,240), 50 segments;
   if (closing && ti >= .5f) { closing = 0; return kind ? result : 24; }   draw content while !(lock && ti >= .5f) */

/* ---- slot list (0x45d530) in 640x480 virtual space; sheet = H.slotsheet (256x256) */
static const float SX[4] = { 16, 488, 16, 488 }, SY[4] = { 48, 48, 324, 324 };
float slide = p->ti <= 0.5f ? (p->closing ? p->t * -600 : (0.5f - p->t) * -600) : 0;   /* 0x45d330 */
for (int i = 0; i < 4; i++) {
    int sel = cur == i + 1; uint32_t c = sel ? 0xfe808080 : 0xfe202020, tc = c;
    float x = SX[i] + ((i & 1) ? -slide : slide), y = SY[i];
    if (!sel) quad_add(x, y, 137, 108, sheet, 0, 0, 137, 108, c);                  /* additive (flag 4) */
    else { blink += dt; if (blink > 0.3f) { if (blink > 0.6f) blink = 0; quad_add(x, y, 137, 108, sheet, 0, 0, 137, 108, 0xfe808080); tc = 0xfe808080; } else tc = 0xfe202020; }
    quad_add(x + 45, y + 50, 49, 49, sheet, 0, 108, 49, 49, c);                    /* gold ring */
    if (pct[i]) { face(0, x + 33, y - 7, c); if (open1[i]) face(2, x - 4, y + 8, c); if (open2[i]) face(1, x + 69, y + 7, c); }   /* k_spr 0/2/1, alpha */
    label = pct[i] ? STR(26 + i) : STR(18);  size 28 fit 137;  lx = SX-column + xoff + 68.5 - w/2;  ly = i < 2 ? 156 : 324 - font_cell();  color tc
    snprintf("%d", pct[i]) + STR(7); size pct >= 100 ? 12 : 16; center at (x + 69.5, y + 75); color sel ? 0xfeff1400 : 0xfe3f0500
    if (page == 2 && !pct[i]) quad_alpha(x + 20, y, 108, 108, sheet, 0, 160, 63, 63, 0xfe808080);                   /* cross */
}
title = page == 2 ? STR(25) : STR(24); size 30 fit 330; x = 320 - w/2; y = 415 - slide;
font_draw(x, y, s, 0xfe808080); font_draw(x - .05f*font_cell(), y - .05f*font_cell(), s, 0xfe801400);

/* ---- percentage (0x4501f0), unlocked (0x4509b0) */
static const int8_t W[29] = { [2]=6,[3]=7,[4]=8,[5]=9,[6]=12,[7]=13,[8]=14,[9]=15,[10]=16, [12]=14,[13]=15,[14]=16,[15]=17,[16]=18,[17]=20, [19]=14,[20]=15,[21]=16,[22]=17,[23]=18,[24]=20 };
int pct_char(const SaveSlot *s, int c) { int p = 0; for (int L = 0; L < 29; L++) if (char_of_level(L) == c && s->chr[c].rec[L].done) p += W[L]; return p; }
int pct_slot(const SaveSlot *s) { return (pct_char(s,0) + pct_char(s,1) + pct_char(s,2)) / 3; }
int char_open(const SaveSlot *s, int c) { return c == 0 || s->chr[0].rec[c == 1 ? 6 : 10].done; }

/* ---- main_engine.c: the chain (replaces "if (cont ...) request_level(1, 0.5f)") */
case 1 "Load game": iris 0.85 -> 0 (0.5 s), logo_v = 0; after 0.5 s:
    if (!file_exists("woodyre.sav") && !import_available()) page = 7;               /* 64/65/66/""/Continue, y 0.4, half-black plane */
    else if (!sav_read_all(&g_file)) page = 0xa;                                    /* 60 "Load failed." */
    else page = 2;                                                                  /* page 0xb: 1 frame, can be skipped */
page 7 / 0xa: only confirm on Continue -> page = 1 (enter: fx 63, iris 0 -> 0.85)
page 2: panel; confirm on non-empty slot -> panel_close(1, 10+s, 1); carousel_pos = 1;
        result 10+s: g_save = g_file.slot[s]; options.music = g_file.music[s]; options.sfx = g_file.sfx[s]; page = 3
        result 24: page = 1
page 3: result 14..17: request_level((int[]){1, 11, 18, 25}[r - 14], 0.4f);  /* BlackBox: state 3 = the BlackBox object, not yet ported */
        result 4: high-score page (not yet ported: ignore or a simple list from rec[].best)
        result 24: page = 1

/* ---- carousel: instances */
static Instance *CAR_FIG[4], *CAR_PED[4];          /* from the 58 registration, or fixed: slots 105..114 */
void carousel_fill(void) {
    int o1 = char_open(&g_save, 1), o2 = char_open(&g_save, 2), o3 = g_save.chr[2].rec[24].done;
    CAR_FIG[0] = slot(105); CAR_FIG[1] = slot(106); CAR_FIG[2] = o1 ? slot(107) : slot(109); CAR_FIG[3] = o2 ? slot(108) : slot(110);
    for (k) CAR_PED[k] = slot(111 + k);
    dark: fig1/ped1 = !o1;  k=2: fig = !o2 && o1 (placeholder never dark), ped = !o2;  k=3: fig = !o3 && o2, ped = !o3
}
/* in the 1200 handler, "type 110 -> visible = 0" remains; page 3 only turns these 8 on and off again after result delivery */
void carousel_place(const FreeCamera *cam, float pos) {                    /* after cam_update, before rendering, every frame */
    Vec3 F = { sinf(cam->yaw)*cosf(cam->pitch), sinf(cam->pitch), cosf(cam->yaw)*cosf(cam->pitch) };
    Vec3 R = normalize(cross(F, (Vec3){0,1,0})), D = cross(F, R);           /* right, DOWN (original's camera space) */
    for (int k = 0; k < 4; k++) {
        float th = ((k + 1 - pos) * 90.0f + 7.0f) * (float)M_PI / 180.0f;
        float px = 150*sinf(th), py = 100 + 26.047f*cosf(th), pz = 560 - 150*cosf(th);
        Vec3 w = cam->pos + R*px + D*(py * 0.75f) + F*(pz / 1.2f);          /* sy 0.75, zoom 1.2 (title: no letterbox) */
        /* orientation: model-z = -D (up), model +y = away from the ring towards the back: -(sin th * R - cos th * F),
           so the character faces (model -y) outward; plus 10° forward tilt (uncertain, may be dropped first) */
        place(CAR_FIG[k], w, ...); place(CAR_PED[k], w, ...);
    }
}
/* rotating: pos linear ±1 over 0.5 s; at t >= 0.25 sel = target (1..4 wrapping); at 0.5 pos = target;
   new character: inst->slot = {1,2,2,2}, a_speed = 3.0, a_start = now (BlackBox: {1,-1,-1,-1}); old character: slot = {1,0,0,0} */
/* dark: alongside tint_red in render_gl.c (line ~559) a tint_scale (0.1) on the lit vertex color = vt[26] 0x451a40 */
```

2D of page 3: follow the table in §4.6 literally (sprites from `k_spr`, arrow = sprite 15 additive x2, numbers red).
Order: 3D (with characters) → iris → 2D texts/arrows → faders.

**State of the port (page 3 ported)**: `src/main_engine.c` `g_car` + `car_fill` / `car_place` / `car_rotate` /
`carousel_enter` / `carousel_update` / `carousel_draw` / `carousel_frame` (every frame after `menu_update` and the
title camera, before `rnd_frame`); records via the fixed slots 105..114 (`slot_instance`), not via message 58; `level_free`
forgets the pointers (`car_forget`). Dark = `Instance.tint_scale = 0.1` (render hook next to `tint_red` in
`src/render_gl.c`). 2D = `hud_carousel(HudCarousel *)` in `src/hud.c`. After PLAY, `level_load` loads the character from the
level (`char_of_level`: KWS → Knothead, block 1 of the save; verified: KWS with Knothead, 7 lives (HUD shows 6) + 5 hearts from a
test save). Found and fixed: the main loop copied the House Perso's lives/health/items/charges into
`g_save.chr[g_char]` every frame, so "Load game" → WWS always started with 9 lives / 3 health; the original only writes via
setters on a change, so at level 0 the port now skips that copy.

## 9. Uncertain / open

1. **Visibility of the class-110 characters outside page 3.** `vt[3]` (`0x489650`) repositions them every frame
   in front of the camera and re-hangs them in a cell (`0x4077f0`); the "hiding" by `0x4891d0(0)` (`0x407850`,
   detach cell) would thus be undone again a frame later, and the characters not in a record (107/109
   or 108/110) sit at camera space (0, 0, 300). That the original shows no characters on the title screen and after "back"
   (TITLE §3.2) therefore doesn't follow from this code — there must be some other gate somewhere (the cell lookup
   `0x4081c0` returning −1 outside the kd sectors? the visible-sector list?). Port: only show the 8 record
   instances, only on page 3 (done; hidden on result delivery and on every other page).
   Check with `tools/wtrace.py`: breakpoint `0x4077f0` with `ecx` = instance 105, log `inst+0x1c` on page 1.
2. ~~Order/sign of the three angles in `0x489780` and the effect of the non-uniform scale on the orientation rows~~:
   verified live (§4.4): `L = Rx(−10°)·Ry(θ)·Rz(0)·Basis`, its columns are the model axes, scaled and normalised by
   `0x489210`; the port now does the same.
3. Which faces belong to which character: the code uses (0,63) for Knothead and (63,0) for Splinter of
   image 61 (both the slot panel and the stats: `+0x58` = 2 resp. 1). HUD_TEXT §4.2 calls sprite 1 "character 1";
   that label may be swapped.
4. What .ins animations 1 and 2 of the characters actually are (a hop / a cheer loop?) and whether speed 3 matches
   what you see (0.27 s + a 0.6 s loop seems fast).
5. `vt[6]` of page 3 (`0x45fff0`: 2 if the selected record is unlocked, else 1): no reader found.
6. Music object `[0x5e61a4]->vt[0x50]/vt[0x54](0.5)` in the save chain (ducking?); `[0x5e618c]` (vibration) on PC.
7. `save+0x14a0` ("extra score"): no writer found.
8. ~~Page 4 (high scores, class `0x45bfb0`) has not been analyzed and is not ported (§4.7)~~ — done, §4.8. "SEE HIGH SCORES" did
   nothing in the port.
9. ~~The exact rounding of `fistp` at 108.5 (ring source) and 50.5 (ring y)~~: the game runs with control word `0x007F`
   (round to nearest-even, 24-bit precision; verified live, `tools/wverify.py --probe fpu`) ⇒ 108 resp. 50.
10. Everything here is static; recommended check with `tools/wtrace.py`: breakpoints on `0x4051da`, `0x4052db` (slot),
    `0x45ee50` (PLAY) and `0x404b60` during Load game → slot 1 → WOODY → PLAY.
