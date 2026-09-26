# MENU_NEWGAME.md — title menu: page 0, 1, 0x1c, 0x1f, attract timer and New game → WWS (Woody.exe, build 17-10-2001)

Status: **static analysis, read instruction by instruction**; addresses = Woody.exe (imagebase 0x400000), to be re-read
with `python tools/drange.py START END`. Supplements and corrects TITLE.md §4/§5, GAMEFLOW.md §2..§6 and
CINEMATIC.md §3.2. Script: `out/house_code.txt` (object 115), `out/wws_code.txt` (object 297). Uncertain points are
marked with **uncertain**. The options and load pages (0x1b, 0xb/2/3/7/0xf..0x11) are covered elsewhere; here only the hooks
where page 1 calls them.

## 0. Summary

- **Input in menus is mixed: confirm = Enter *released* or the jump key *pressed*; back = Esc *released* or
  the duck key (action 5) pressed.** Enter sets action 0xc via `0x4033fb` only on the frame the key is released
  (`[app+8]->vt[5](0x1b)` = "just released", internal code 0x1b = DIK_RETURN); the jump key sets action 4 + 0xc on
  press. Esc: keyboard code 0 (= DIK_ESCAPE) "just released" → `vt[14]` (back) in `0x4465f7`, and action 9.
- **Page 0** ("Press a key", blinking): confirm or Esc (action 9 released) → page 1. Back does nothing. The
  **attract timer** (35 s, only on page 0, never reset by keys) starts the House intro as a demo when it reaches 0.
- **Page 1** (New game / Load game / Options / Quit): on entry **SoundFx 63**, cursor on New game, **no
  input delay**, **no iris** (the ctor sets `+0x38 = 0`). **Back and Esc do nothing** (slot 20 = empty function; the
  handler doesn't check action 9). Up/down wrap around, no sound.
- **Quit** goes to **page 0x1c "Are you sure?"** (header + Yes + No, cursor on **No**). Yes on the title = fade-out
  0.5 s + music off in 0.45 s + quit (`0x404cb0`); Yes in a level = `RequestLevel(0.5, 0, 0, 0)` (to the title);
  No or back = page 1 (title) / pause menu (level).
- **New game**: validate returns **2** (`SetVar(app+0x8c, 1)` = House script object 115 starts the intro), one frame later
  **1** (page 0x1f, `app+0x94 = 1`). The intro lasts 0.5 s black + 53.0 s. **Skipping = action 6 (attack) or 9 (Esc)
  *released*** — not Enter, not the jump key. End: attract timer = 35, **`0x44ffa0` only clears the active
  save struct in memory** (all three characters: 9 lives, health 3.0, all level records 0), **no Woody.sav slot is
  read, chosen or written and there is no "overwrite?" prompt**; `RequestLevel(0 or 0.5, 1, 1, 0)`
  → WWS as Woody (character follows from the level table), spawn at the `.ins` position of WWS instance 0
  (−86.5, 87.5, −793.0), because `GetPrevLevel` = 0 (House) leaves the hub script nothing to move.
- **Iris** = black ring of 50 quads in **virtual 640×480**, center (320, 240), inner radius `0.99·480·v`,
  outer radius 475.2, opaque black (vertex color (0,0,0,1)), clipped to 0..640 × 0..480. Only drawn when
  panel-page field `+0x38 == 1`: on page 1 only after you chose "Load game" and come back.

## 1. Input

### 1.1 Action object `[0x5e6188]` = `app+0x14` (14 × {float value, float hold time, u32 state})
PERSO_MOVE §3.2 holds: `0x467340(dt)` start of frame (state &= bit 31), `0x4673b0(i, v)` sets (1 = just pressed, 2 =
held), `0x467370` end of frame (bit 31 = just released, exactly one frame). Getters `0x467400` held, `0x467420` just
pressed (`== 1`), `0x467440` just released (bit 31). **`SetState 0x401400` clears all 14 actions** (`0x467320`), so
a state change carries no edges over.

### 1.2 Keyboard object `[0x5e6194]` = `app+8` (ctor `0x467d70`, vtable `0x4ab974`, DirectInput)
- Poll `0x467ef0`: 256 bytes DIK status → **internal code = table `0x4b6f88`[DIK]**, for low codes `DIK − 1`
  (table[1] = 0, table[0x1c] = 0x1b; only DIK 1 and DIK 0x1c yield 0 resp. 0x1b; Numpad Enter = 0x69).
- `0x4675a0` (vt[3]): `prev = current`, poll, edge[i] = `prev[i] && !current[i]`.
- **vt[4] `0x4675f0` = pressed, vt[5] `0x467610` = just released** (not "pressed" as TITLE.md assumed).

### 1.3 What the menus read
| what | source | when | default key (PERSO_MOVE §3.1) |
|---|---|---|---|
| confirm = action **0xc** just pressed | `0x402ef7`/`0x403163` (together with action 4) | jump key **pressed** | LCtrl, joystick button of action 4 |
| | `0x4033fb`: `[app+8]->vt[5](0x1b)` → `0x4673b0(0xc, 1.0)` | **Enter released** (DIK_RETURN) | fixed, not configurable |
| back = `vt[14]` | action **5** just pressed (`0x4465e2`) | duck key pressed | Space |
| | `[0x5e6194]->vt[5](0)` (`0x4465f7`) | **Esc released** (DIK_ESCAPE) | fixed |
| up / down | actions 2 / 3 just pressed | | ↑ / ↓ (joystick axis) |
| right / left | actions 1 / 0 (sliders only, flag 0x10) | | → / ← |
| page 0 → 1, skip intro | action **9** just **released** (`0x405062`, `0x4050b5`) | | Esc |
| skip intro | action **6** just **released** (`0x4050a7`) | | LShift (attack) |
| pause menu in-game | action 9 just pressed (`0x403334`), only state 1/3 | | Esc |

Enter (DIK_RETURN) is also action 7 in the default cfg; no menu reads action 7.

## 2. Menu frame and the shared page class

### 2.1 `0x404e90` (App state 0)
```c
hud_show(app->hud, page in {0x18,0x19} && !app->blackbox ? 1 : 2);     // 0x448450: 2 = HUD hidden
switch (tab_405ae0[tab_405af8[page]]) { ... }                          // pause/overlay, see table
Game_Frame();                                                          // 0x401ab0: world, VM tick, cinematic
if (overlay) RectVirtual(0,0,640,480, 0x80000000 x4, blank, 8);        // half-black
res = Menu_Update(app->menu, dt);                                      // 0x446440 -> 0x4464f0 (§2.2)
if (app->closing /*+0xcc*/) return;                                    // during a level switch/quit fade: NO handler
handler_405b1c[page](res);                                             // 0x404fe4 (0), 0x40519f (1), 0x4057a5 (0x1c), 0x40508c (0x1f)
```
Table `0x405af8` → `0x405ae0`: pages 0..4, 0x1d, 0x1f, 0x20 → paused if level ≠ 0, no overlay; 5, 0x1e → not
paused, no overlay; 6, 8, 9, 0xc..0xe, 0x12..0x15, 0x17 → overlay, not paused; 7, 0xa, 0xb, 0xf..0x11 → overlay
+ pause if level ≠ 0; 0x16, 0x21 → pause + full black; **0x18..0x1c → overlay and pause only if level ≠ 0**. In
House there is therefore never pause or overlay: the orbit camera keeps running on all title pages.

### 2.2 `0x4464f0(font, dt)` — per frame, current page
```c
page->result = 0;                                   // +0x10 cleared every frame
g_dt = dt;  blink += dt; if (blink > 0.5f) blink = 0;           // [0x5d7b1c], 0x4b39a4 = 0.5
Font_SetSize(font, 30);  font->color = 0xffffffff;              // 0x4b39a8; font+0x60
page->vt[1]();                                      // draw (+ for panel pages: deferred result, §2.5)
Logo_Draw();                                        // 0x446b00, §2.6
if (page->delay /*+8*/ > 0) page->delay -= dt;
else {
    if (P(2)) vt[11]();  if (P(3)) vt[10]();  if (P(1)) vt[12]();  if (P(0)) vt[13]();   // P = 0x467420
    if (P(0xc) && !page->given /*+0xc*/) page->result = vt[9]();
    if (P(5)) vt[14]();
    if (kbd && kbd->vt[5](0 /*Esc*/)) vt[14]();     // only in this branch
}
page->given = 0;  return page->result;
```
Page-enter (`0x4464a0` → `vt[15]`), base `0x4464c0`: `+0xc = 0`, `sel = 0`, `+8 = 0`; if item 0 is a header
(flag 2) then `vt[10]()` (down, sets the blink phase to 0).

### 2.3 List drawing `0x446640` (base vt[1])
```c
S = vt[8]();                                        // 30
for (i = 0; i < n; i++) while (!(640 > Measure(str(i), S)) && (S -= 1) >= 15) ;   // one S per page
y = vt[7]() * 480;
for (i = 0; i < n; i++) {
    if (!(flags[i] & 2) && page->delay > 0) return; // loop STOPS at the first non-header item as long as the delay runs
    Font_SetSize(flags & 0x20 ? S*0.8f : S);  w = Measure(str(i));
    x = flags&4 ? 640-w-6.4f : flags&8 ? 6.4f : flags&0x80 ? 160-w/2 : flags&0x40 ? x : 320-w/2;
    if (flags & 0x10) { ...slider text... }        // not on these pages
    if (!(i == sel && blink < 0.25f)) Font_Draw(x, y, str(i));   // [0x5d7b00] = 0.5*0.5
    y += CellH() + Extra();                         // 0x441980 + 0x441a50
}
```

### 2.4 Navigation (base slots)
- **up `0x446920`**: `sel--`, below 0 → `count−1`, skip headers; **always sets the blink phase to 0**, even if
  nothing actually moved (TITLE.md said "0.25 on failure" — that only applies to down).
- **down `0x446970`**: `sel++`, above → 0, skip headers; phase = 0 if it moved, otherwise 0.25.
- right/left `0x4469d0/0x446a20`: only items with flag 0x10 (±5, 0..100), phase 0.25.
- back `0x446a70`: `result = 24`.  validate `0x446aa0`: `flags & 1 ? item.result : 0`.
- After a successful step the new item is therefore first **0.25 s invisible**, then 0.25 s visible.

### 2.5 Panel page (base `0x45b830`, vtable `0x4ab2e4`) — page 1 inherits from this
| field | meaning |
|---|---|
| `+0x14` | deferred result |
| `+0x18` | kind: 0 → returns 24 (back), 1 → returns `+0x14` |
| `+0x1c` | time since enter; `+0x23`/`+0x21` go off after 0.5 s (`0x4ab2e0`) |
| `+0x22` | "closing": waits until `+0x28 ≥ +0x34`, then `vt[18]` (delivers the result, `0x45baa0`) |
| `+0x23` | "opening" (first 0.5 s) |
| `+0x24` | **locked**: validate/up/down/back do nothing (`0x4600e0`, `0x45bb90/0x45bba0/0x45bb30`) |
| `+0x28` | time since validate/enter |
| `+0x2c` | iris object (0x10 B) · `+0x30` iris "open" value · `+0x34` duration (0.5) · `+0x38` byte iris active |

Ctor `0x45b830`: `+0x38 = 1`; **page 1 ctor `0x460040`: `+0x38 = 0`**.
Base enter `0x45b8c0`: base enter, `+0x22 = +0x24 = 0`, `+0x30 = 0.37`, `+0x34 = 0.5`, **`+8 = 0.5`** (input delay),
`+0x28 = 0`, **`SoundFx(0x3f, 0)`** (`0x45b8ef`, 2D), iris(0 → 0.37, 0.5 s), `+0x1c = 0`, `+0x23 = +0x21 = 1`, `+0x20 = 0`.
Draw `0x45b990`:
```c
if (opens && t28 == 0 && iris_on) Iris_Draw(0);         // first frame: fully black
t28 += dt;  t1c += dt;  if (opens && t1c >= 0.5f) opens = opt21 = 0;
vt[16]();                                                // page 1: 0x4600b0 = list + logo fade-in (§2.6)
if (iris_on) { if (open30 > 0) Iris_Anim(dt); else RectVirtual(0,0,640,480, 0xfe000000 x4); }
if (closing && t28 >= dur) vt[18]();                     // deliver result -> +0x10, +0xc = 1 (blocks confirm)
if (!locked || t28 < dur) vt[17]();                      // page 1: empty
```

### 2.6 Logo (`0x446b00`, after `vt[1]`, only when `app+0x68 == 0`)
`v = [0x5d7b28]`, `A = −ftol(v·0.2·−254)` (i.e. `trunc(50.8·v)`), `RectVirtual(216,16,209,247, 0,0,209,247,
0x808080|A<<24, bank-1 image 1, 8)`, then `v −= 5·dt` (lower bound 0). Page 0 (`0x45bd10`) and page 1
(`0x4600b0`, **not** while `+0x22` or while the iris is active AND `+0x23`) do `v += 10·dt` before drawing
(upper bound 5). Net effect: 1.0 s fade-in on page 0/1, **1.0 s fade-out on every other page (including 0x1c)**;
instantly 0 on entering New game and Load game (`0x460127`, `0x460150`) and on entering 0x18/0x19/0x1e/0x1f (`0x45b390`).

### 2.7 Iris (`0x4776b0` sets, `0x477920` animates, `0x4776d0` draws, `0x482cf0` 2D polygon)
```c
void Iris_Set(Iris *s, float from, float to, float dur) { s->from = from; s->to = to; s->dur = dur; s->t = 0; }  // 0x4776b0
bool Iris_Anim(Iris *s, float dt) {                                 // 0x477920
    s->t += dt; float f = s->t / s->dur; if (f > 1) f = 1;
    Iris_Draw(s->from - (s->from - s->to) * f);  return f >= 1; }
void Iris_Draw(float v) {                                           // 0x4776d0
    for (float i = 0; i < 1.0f; i += 0.02f) {                       // 50 segments (0x4aa1b0)
        a0 = i*512, a1 = (i+0.02f)*512;                             // cos table [0x5e823c], sin = cos(a+128)*-1
        c = 0.99f*cos, s = 0.99f*sin;                               // 0x4ab7d8 / 0x4abd44
        quad { (320+480v·c0, 240+480v·s0), (320+480v·c1, 240+480v·s1),
               (320+480·c1,  240+480·s1),  (320+480·c0,  240+480·s0) }, color (0,0,0,1) on vertex+0x24..0x30
        0x482cf0(4, verts, tex 0 -> [0x5e8684], flag 8);
    }
}
```
`0x482cf0` clips every polygon to x 0..640 (`0x4aaba8`) and y 0..480 (`0x4ab248`) and then scales with `W/640`, `H/480`:
**virtual 640×480 coordinates**. Color ×255 (`0x4aa308`) → opaque black. The screen corners lie at 400 from the
center, the outer edge at 475.2: at `v = 0.85` the opening is 403.9 → invisible; at 0 → fully black.
Draw order within the menu layer: items → iris → logo.

## 3. Page 0 — title (vtable `0x4aa9c8`, 0x14 B, no ctor, handler `0x404fe4`)
- Items `0x4b5da8`: `{0x20015 "Press a key", flag 1, result 5}`; y = 0.7·480 = **336**, centered, S = 30, white
  (`0xffffffff`). It's the selected item ⇒ **blinks**: 0.25 s off / 0.25 s visible.
- Enter = base `0x4464c0` (no sound, no delay); draw `0x45bd10` = list + logo fade-in.
- Handler:
```c
bool was = attract > 0;  attract -= dt;                             // app+0x98, 0x4a9004 = 0
if (attract <= 0) {
    if (was) SetVar(app->introVar /*+0x8c*/, 1);                    // same start as New game
    if (attract < -0.5f) { app->newGame = 0; Menu_SetPage(0x1f); }  // 0x4a94bc
    return;                                                         // input (including result 5) ignored
}
if (Released(9)) Menu_SetPage(1);                                   // Esc
if (res == 5)    Menu_SetPage(1);                                   // confirm; result 24 (back) ignored
```
Keys that work: Enter (release), jump key (LCtrl, press), joystick button of action 4, Esc (release). Other
keys do nothing, despite the text.

## 4. Page 1 — main menu (vtable `0x4ab5c0`, 0x3c B, ctor `0x460040`, handler `0x40519f`)
Items `0x4b5e70`, y = 0.55·480 = **264**, then per row `CellH + Extra` (46.5 at S = 30), centered, white:

| i | string | item result | validate `0x4600e0` (table `0x460194`) | handler |
|---|---|---|---|---|
| 0 | 22 New game | 1 | `+0x14 = 1`, `+0x38 = 0`, `+0x28 = +0x34 + 1 = 1.5`, **logo 0**, **return 2** | 2 → `SetVar(app+0x8c, 1)` (`0x4051b0`); next frame 1 → `app+0x94 = 1`, page **0x1f** (`0x4051c5`) |
| 1 | 23 Load game | 3 | `+0x38 = 1`, `+0x14 = 3`, `+0x28 = 0`, iris(0.85 → 0, 0.5 s), **logo 0**, return 0 | after 0.5 s 3 → `0x4051da` (load pages) |
| 2 | 36 Options | 6 | `+0x28 = 1.5`, `+0x14 = 6`, `+0x38 = 0` | next frame 6 → page **0x1b** |
| 3 | 2 Quit | 7 | `+0x28 = 1.5`, `+0x14 = 7`, `+0x38 = 0` | next frame 7 → page **0x1c** |

All four first set `+0x22 = +0x24 = 1`, `+0x18 = 1` (`0x4600f9..0x4600ff`); if `+0x24` is already set, validate does nothing.
`vt[18] = 0x4601b0`: deliver result (`0x45baa0`) and on result 1 `+0x38 = 0`.
- Enter `0x460070`: panel enter (sel 0, **SoundFx 63**, …), then **`+8 = 0`** (immediately usable, list visible right away),
  `+0x30 = 0.85`, iris(0 → 0.85, 0.5 s). This sound plays **every** time page 1 opens: from page 0, after "No" on
  0x1c, after returning from Options/Load.
- Up/down wrap (4 items, no headers); blocked after a validate (`+0x24`).
- **Back (Space, Esc release) does nothing** (`0x45bb30` → slot 20 = `0x462c60` = `ret`); the handler doesn't check
  action 9. There is no way back from page 1 to page 0.
- Iris on page 1: ctor `+0x38 = 0` and enter doesn't touch `+0x38` ⇒ not drawn, unless an earlier "Load game" left it at
  1; then on return it opens from 0 to 0.85 over 0.5 s and the logo fade-in is off for the first 0.5 s.

## 5. Page 0x1c — "Are you sure?" (vtable `0x4aab08`, 0x14 B, handler `0x4057a5`)
- Items `0x4b5db8`: `{3 "Are you sure?", flag 2 (header)}`, `{5 "Yes", 1, 8}`, `{6 "No", 1, 9}`; y = 0.55 → **264, 310.5,
  357**; S = 30, centered. Draw = base list (**no logo fade-in ⇒ the logo fades out over 1 s**), no sound,
  no iris, `+8 = 0`.
- Enter `0x45bd40`: base enter (item 0 is a header ⇒ down ⇒ sel 1, blink phase 0), then **`sel = 2` ("No")**. "No" is
  therefore first 0.25 s invisible.
- Up/down: between Yes and No (header skipped), wraps around.
- Handler:
```c
if (res == 8) {                                          // Yes
    if (app->level == 0 || (cfg->dev & 4)) Quit(0.5f);   // 0x404cb0
    else RequestLevel(0.5f, 0, 0, 0);                    // to House / title (page 0 + menu music)
} else if (res == 9 || res == 24) {                      // No or back (Space / Esc release)
    if (app->level == 0) Menu_SetPage(1);                // SoundFx 63 again
    else Pause_Open();                                    // 0x404d80: page 0x18/0x19, sound paused
}
```
`Quit 0x404cb0(0.5)`: `+0xcc = +0xcd = 1`, `+0xd4 = 0`, `+0xd0 = 0.5`, fade to black 0.5 s (`0x401480`), music
`vt[0x4c](0.45)` + rtc stream stop `vt[0x9c]`. During the fade no page handlers run (`0x404f71`). After the fade:
unload, `app+4 = 1` → WinMain writes Woody.cfg and quits. In a level (table `0x405af8`) a half-black plane lies
under the page and the world stands still; in House it does not.

## 6. Attract timer and page 0x1f

### 6.1 Timer `app+0x98`
Set to **35.0** (`0x4a9000`) on boot (`0x402653`) and at the end of page 0x1f (`0x4050c2`), **nowhere else**:
not when loading House, not on a key, not when switching to page 1. It only runs on page 0 and only when no
level switch/quit fade is running. Whoever returns to the title after a level gets the remaining value.
Behavior on page 0: on the transition to ≤ 0 `SetVar(var1, 1)` (unconditional: the script block reacts to every
change to 1, even after an earlier intro with var1 = 4), at < −0.5 page 0x1f with `app+0x94 = 0`.

### 6.2 Page 0x1f (vtable `0x4aa4b4`, 0x14 B, handler `0x40508c`)
Item `0x4b5cd8`: `{0x20001 "" , flag 1, result 9}` at y = 0.6 (`0x4a9650`) — invisible; enter `0x45b390` =
base enter + logo 0; draw = base list (no logo fade-in). Confirm returns 9, which the handler ignores.
```c
int v = *GetVar(app->introVar);                           // 0x443cd0
if (v != 4 && !Released(6) && !Released(9)) return;       // action 6 = attack (LShift), 9 = Esc; RELEASE
app->attract = 35.0f;
if (app->newGame /*+0x94*/) {                             // --- New game ---
    Save_Reset(app->save /*+0x48*/);                      // 0x44ffa0, §7.3
    RequestLevel(v == 4 ? 0.0f : 0.5f, 1 /*WWS*/, 1 /*play*/, 0);
    return;                                               // script, cinematic, textbox keep running until the unload
}
if (v != 4) {                                             // --- attract aborted ---
    Eko_CancelTimers(app->introObj /*+0x90*/);            // 0x444380 + 0x4441d0: DELAY and DURING list of object 115
    SetVar(app->introVar, 4);
    Cin_Abort(game + 0x64);                               // 0x44f290: reset + resume music (timer*0.9) + rtc stream stop
    Perso->vt[0x44]();                                    // 0x44ab20: reset, state 0 -> 0x44e690 starts action 0x49 again
}
for (i = 0; i < 4; i++) Fader_Clear(&app->fader[i]);      // 0x445bc0
TextBox_Close(game + 0x18);                               // 0x456ec0
DrawBlack();  FadeIn(0.5f);                               // 0x4014c0, 0x401440
TitlePage();                                               // 0x404e30: switch menu music + page 0 + state 0
```
`app+0x90` = second argument of 1160 & 0xffffff = **0x73 = script object 115** (`0x44496d`).

### 6.3 Menu music `0x404e30`
`app+0x54` is set to 0 on every LoadLevel (`0x4041b0`). `0x404e30`: `n = app+0x54 + 1; if (n == 2) n = 0;` →
`n == 1` ⇒ **PlayMusic(0) "Menu"**, `n == 0` ⇒ **PlayMusic(48) "Menu02"**. After every load of House (boot, return from
a level) it's therefore first **track 0**; only a second call without a load (end of an attract) gives 48, the third again 0.
Callers: `0x401575` (boot), `0x4017c9` (after a level switch with state 0 and page 0), `0x405196` (end of attract).

## 7. New game, from confirm to WWS

### 7.1 Timeline
| when | what | address |
|---|---|---|
| frame N | confirm on "New game" (Enter release / jump key press). Validate → **2**; logo `v = 0` (off); handler `SetVar(var1, 1)` | `0x46010f`, `0x4051b0` |
| frame N+1 | `Game_Frame` runs before the menu: VM tick, object 115: `var1 = 2`, `var2 = −1`, `1131 [0, 72]`, 8× `1132` (122, 123, 120, 119, 121, 116 anim 0; 117, 118 anim 72), **`1130 [0x73, 8, var2]`**; `DELAY 100` → after 1 s `57 [0,10000]` + `56 [0,0]` (Woody instance instantly opaque) | `out/house_code.txt` 1243..1361 |
| | 1130: cin state 1, fade to black 0.4 s, menu music **paused** over 0.45 s (`vt[0x50]`), rtc track 8 `/Rtc/Menu.wav` staged | CINEMATIC §2 |
| | menu: page 1 returns **1** → `app+0x94 = 1`, page 0x1f (enter: logo 0). Page 1 is visible for exactly one frame longer | `0x4051c5` |
| +0.5 s | cin state 2: fade-in 0.5 s, **`/Rtc/Menu.wav`** starts, `var2 = 0`, Woody (inst 0) at the vector of slot 115, anim 72 at speed 3, camera mode 0x80 **with** letterbox; Perso update skipped; the orbit camera stops | CINEMATIC §3 |
| +9.0 s / +11.9 s | `1151 [300]` fade-out 3 s; actors swap, `1152` 1.15 s black frames | T = 900 / +290 |
| +42.0 / 46.0 / 49.5 s | textboxes 1080 bottom-centered: 107+108, 109+110, 111+112 (close at 45.0 / 49.0 / 52.5 s) | T = 4200..5250 |
| +52.5 s | cin state 3: fade to black 0.5 s | CINEMATIC §3 |
| +53.0 s | script: `var1 = 4`, `DURING 10` `1152` (0.1 s black), var4 = 1 (close text); cin → state 4 (Perso reset, resume menu music 0.45 s) | T = 5300 |
| next frame(s) | page 0x1f sees `var1 == 4`: attract = 35, `Save_Reset`, **`RequestLevel(0, 1, 1, 0)`** (no extra fade: the screen is already black) | `0x4050ff` |
| skip | at any moment after frame N+1: **action 6 or 9 release** → attract = 35, `Save_Reset`, **`RequestLevel(0.5, 1, 1, 0)`**: 0.5 s fade-out over the ongoing cinematic (text, rtc music keep running), music `StopMusic(0.45)` + rtc stop | `0x405786` |

Enter and the jump key do **not** skip (Enter → action 0xc → validate of 0x1f = 9, ignored).

### 7.2 `RequestLevel(fade, 1, 1, 0)` and arrival
`0x404b60`: `+0xd4 = +0xcc = 1`, `+0xd0 = fade`, fade-out (`0x401480`), `vt[0x4c](fade·0.9)` (music stop) + `vt[0x9c]()`
(rtc stop), target = (1, state 1, page 0). After the fade (`0x401590`): unload House, `LoadLevel(1)`: `app+0x6c = 0`,
`app+0x68 = 1`, **character = `byte_404830[1]` = 0 = Woody** (`cfg+0x380`), `app+0x54 = 0`, script init, Perso init
from the save struct (block 0: 9 lives, health 3.0, 0 items, 0 charges), fade-in **1.0 s** (`0x401440(1.0)`) and
the first 4 frames black; then `SetState(1)` (all actions cleared, current menu page = NULL).
**Spawn**: WWS instance 0 (`1200 [0x1000000, 1]`, model 0) at its `.ins` position **(−86.49, 87.48, −792.99)** with the
`.ins` direction (`0x44a3d0`). Object 297: `1655 [1]` (/Game/WS.wav), `DELAY 1` → `1084 GetPrevLevel(var51)` = **0**
(House) ⇒ no `1140` (no door placement, no results screen); the WWS cinematic (object 344, `var59 = 1`) only starts
if `var51 == 10` (returning from W3D). So: Woody just stands at the hub start point, follow camera.

### 7.3 `0x44ffa0` Save_Reset (active save struct `app+0x48`, 0x14a4 B)
```c
s->ver = 0x11004;  s->u8 = 0;                                   // +4, +8
for (c = 0; c < 3; c++) {                                       // Woody, Knothead, Splinter (+0xc + c*0x6dc)
    blk->lives = 9;  blk->unique = 0;  blk->charges = 0;  blk->health = 3.0f;
    for (L = 0; L < 29; L++) {                                  // rec = blk + 0x10 + L*0x3c
        rec->best = 0;  rec->done = 0;  memset(rec->got, 0, 32);   // +0, +4, +5..+0x24
        memset(rec->stats, 0, 20);                              // +0x28..+0x38 (bytes +0x25..+0x27 stay as is)
    }
}
s->u14a0 = 0;  s->checksum = signed_byte_sum(s);                // 0x450030
```
That's all. **Untouched**: the slot manager `app+0x4c` (4 slots from Woody.sav), the file Woody.sav, the
save slot index `app+0x60` (only chosen on page 5), `cfg+0x380` (the character follows from the level). There is therefore
**no slot choice and no overwrite prompt on New game**; that only comes at the first save after a level
(page 6 → 5 → occupied slot → 0x17 "…overwrite this save?"). An old Woody.sav stays untouched until the player picks
"Yes" there.

## 8. Corrections to existing docs

1. TITLE.md §5.1 / §7.4: `[0x5e6194]->vt[5](0)` is **Esc *released***  (vt[5] = release edge `0x467610`, internal
   code 0 = DIK_ESCAPE). Enter works as confirm via `0x4033fb` (**Enter released** → action 0xc), not via the cfg.
2. TITLE.md §5.1: "a failed [move] at 0.25" only applies to **down**; up always sets the phase to 0.
   Also: the list **stops** at the first non-header item as long as `+8 > 0` (later headers are then not drawn either).
3. TITLE.md §5.4: the iris color is **confirmed**: opaque black (vertex color (0,0,0,1) × 255, `0x482fb7..`),
   coordinates virtual 640×480, clipped to the virtual screen (`0x482d25..0x482d8c`). Additionally the panel draws the
   iris on the first frame (`+0x28 == 0`, iris active) at 0 = fully black.
4. TITLE.md §5.4 / §6: page 1 enter sets `+8 = 0`, but the **panel base sets 0.5 s** (`0x45b8e0`); that applies for
   other panel pages (including 0x1e), not for 0, 1, 0x1c, 0x1f (all 0).
5. TITLE.md §6 recipe: "Yes = fade 0.5 s + quit" only on the title; in a level = to the title. "No" and back →
   page 1 with SoundFx 63.
6. TITLE.md §4.2 / GAMEFLOW §5: key 6/9 = **release**; on New game (`+0x94 = 1`) **nothing** is stopped (no
   `0x444380`, no `0x44f290`, no Perso reset): only save reset + RequestLevel. CINEMATIC.md §3.2 ("only the
   House intro can be aborted ... stop the script object ...") only applies to the attract path.
7. SOUND.md §4 ("track 48 at 0, track 0 at 1") holds, but the actual order in practice is: after every House load first
   **track 0**, only after an attract 48 (§6.3).
8. GAMEFLOW §4.2/§5: `SetState 0x401400` also clears all input actions (`0x467320`) and at state 1 sets the
   current menu page to NULL and turns the sound back on (`+0x10 = 1` of `[app+0x20]`).
9. GAMEFLOW §5 table page 0: "Key 9 → page 1" = Esc **released**; page 0x1c: result 24 (back) = "No".

## 9. The port compared (`src/main_engine.c` ~l. 1397..1425, `src/hud.c` l. 592..636, `src/render_gl.c` l. 21)

| # | original | port now | recipe |
|---|---|---|---|
| 1 | Esc = action 9 / menu back (release); pause menu in-game | `WM_KEYDOWN VK_ESCAPE → w->quit = 1` (render_gl.c) | done: Esc is read as an edge and opens pause menu 0x18/0x19 in a level (ported, TODO.md "2D, menus, game flow"); read Esc as an edge (below) |
| 2 | confirm = Enter **release** or jump key **press** | Enter or Space **press** | `m_ok = REL(VK_RETURN) \|\| PRS(VK_SPACE)` (Space = the port's jump key) |
| 3 | page 0: also Esc release → page 1 | `(win.keys[VK_ESCAPE] && 0)` | `if (m_ok \|\| REL(VK_ESCAPE)) page1_enter();` |
| 4 | attract timer 35 s, page 0 only | missing | §9.2 |
| 5 | page 1: back/Esc does nothing | same (no code) | do nothing — but Esc must no longer close the port (#1) |
| 6 | Quit → page 0x1c, cursor No; Yes = fade 0.5 s + music 0.45 s + quit | `win.quit = 1` directly | §9.3 |
| 7 | New game: `SetVar(var1, 1)` **always** | only if `*iv == 0`, otherwise intro skipped | `if (iv) eko_set_var(&L.vm, g_intro_var, 1); else new_game_pending = 2;` |
| 8 | skip intro: action 6 or 9 **released** | Enter/Space pressed | `if (REL(VK_ESCAPE) \|\| REL(VK_CONTROL) \|\| REL(VK_SHIFT))` (the port's attack keys = action 6) |
| 9 | natural end: `RequestLevel(0, …)` | `request_level(1, 0.5f)` | `request_level(1, *iv == 4 ? 0.0f : 0.5f)` (request_level already clamps to 0.01) |
| 10 | Save_Reset in memory, **no** write action | `save_reset(); save_write();` — overwrites woodyre.sav immediately | remove `save_write()`; Load game must then re-read the file itself (`save_read()`), see the load analysis |
| 11 | menu music: track 0 after every House load, 48 after an attract | `title_n++ & 1 ? 0 : 48`, static across loads ⇒ the first title plays 48 | §9.4 |
| 12 | logo: +10·dt/−5·dt, 1 s fade-out on other pages, instant 0 on New game/Load/0x1f | `else H.logo_v = 0` (always instant) | §9.5 |
| 13 | blink phase: up → 0, down → 0 / 0.25; >0.5 → 0 | no reset on navigate; `-= 0.5` | `hud_menu_blink(0.0f)` after every step (down without moving 0.25); wrap `if (t > 0.5f) t = 0` |
| 14 | SoundFx 63 on **every** entry to page 1 | only 0 → 1 | via `page1_enter()` (also after 0x1c No, Options, Load) |
| 15 | page 1 delivers New game/Options/Quit one frame later | immediate | optional: one-frame delay (`p1_defer`); invisible difference except one extra frame with the menu |
| 16 | after end/abort of an attract: orbit back to phase 0; Woody only visible during cin 2..3 | `title_t` keeps running; `visible = state >= 2`; orbit only at `state < 2` | `title_t = 0` at the end of the attract; `visible = (state == 2 \|\| state == 3)`; orbit at `state != 2 && state != 3` |
| 17 | menu text color `0xffffffff` (MODULATE2X ⇒ texture ×2, saturated) | `0xff808080` (×1; `set_col` clamps to 1) | **uncertain**: only visible if the font glyphs aren't white; if so: font quads with `GL_COMBINE`/`RGB_SCALE 2` |
| 18 | iris | missing | only needed on Load return; `hud_iris(v)` §9.6 |

`--enter T` (synthetic confirm) and the port key "L = continue" are test hooks; leave them in place.

### 9.1 Edges
```c
/* main_engine.c, next to the other *_prev arrays; end of frame: memcpy(key_prev, win.keys, sizeof key_prev) */
static unsigned char key_prev[256];
#define PRS(k) (win.keys[k] && !key_prev[k])                 /* 0x467420 */
#define REL(k) (!win.keys[k] && key_prev[k])                 /* 0x467440 / keyboard vt[5] */
int m_ok   = REL(VK_RETURN) || PRS(VK_SPACE) || syn;          /* action 0xc */
int m_back = REL(VK_ESCAPE);                                  /* vt[14]; + PRS(duck key) once action 5 exists */
int m_up = PRS(VK_UP) || PRS('W'), m_dn = PRS(VK_DOWN) || PRS('S');
int a9_rel = REL(VK_ESCAPE), a6_rel = REL(VK_CONTROL) || REL(VK_SHIFT);
```

### 9.2 Title state machine (replaces l. 1411..1422)
```c
static float g_attract = 35.0f;          /* app+0x98: do NOT reset in level_load */
static int g_newgame, g_intro_obj;       /* app+0x94; 1160 arg 2 & 0xffffff (case 1160: g_intro_obj = m->args[1] & 0xffffff) */
static int g_quitting; static float g_quit_t;

static void page1_enter(void) { title_page = 1; title_sel = 0; audio_fx(63, NULL, NULL); }   /* 0x460070 */

if (g_level == 0 && !fly && g_next_level < 0 && !g_quitting) {        /* 0x404f71: no handler during a fade */
    int32_t *iv = ...;                                                 /* as now */
    switch (title_page) {
    case 0: { int was = g_attract > 0; g_attract -= dt;
        if (g_attract <= 0) { if (was && iv) eko_set_var(&L.vm, g_intro_var, 1);
                              if (g_attract < -0.5f) { g_newgame = 0; title_page = 0x1f; hud_logo_off(); } }
        else if (m_ok || a9_rel) page1_enter();
        break; }
    case 1:
        if (m_up) { title_sel = (title_sel + 3) % 4; hud_menu_blink(0); }
        if (m_dn) { title_sel = (title_sel + 1) % 4; hud_menu_blink(0); }
        if (m_ok) switch (title_sel) {
            case 0: hud_logo_off(); g_newgame = 1; title_page = 0x1f;
                    if (iv) eko_set_var(&L.vm, g_intro_var, 1); break;     /* result 2, then 1 */
            case 1: /* Load game: load analysis */ break;
            case 2: /* Options 0x1b: options analysis */ break;
            case 3: title_page = 0x1c; title_sel = 2; hud_menu_blink(0); break;
        }
        break;                                                         /* back: nothing */
    case 0x1c:
        if (m_up || m_dn) { title_sel = title_sel == 1 ? 2 : 1; hud_menu_blink(0); }
        if (m_ok && title_sel == 1) { g_quitting = 1; g_quit_t = 0.5f; fade_start(0.5f, 1); audio_music_stop(0.45f); audio_rtc(-1); }
        else if ((m_ok && title_sel == 2) || m_back) page1_enter();
        break;
    case 0x1f: {
        int v = iv ? *iv : 4;
        if (v != 4 && !a6_rel && !a9_rel) break;
        g_attract = 35.0f;
        if (g_newgame) { save_reset(); request_level(1, v == 4 ? 0.0f : 0.5f); g_newgame = 0; break; }
        if (v != 4) { eko_cancel_timers(&L.vm, g_intro_obj); eko_set_var(&L.vm, g_intro_var, 4);
                      memset(&g_cin, 0, sizeof g_cin); audio_rtc(-1); audio_music_pause(0, 0.45f); }
        memset(&g_sfade, 0, sizeof g_sfade); hud_text_reset(); g_black_frame = 1; fade_start(0.5f, 0);
        title_t = 0; title_page0();                                    /* §9.4 */
        break; }
    }
}
if (g_quitting && (g_quit_t -= dt) <= 0) win.quit = 1;                /* 0x404cb0 -> app+4 */
```
In a level (pause menu, if that gets ported) 0x1c Yes goes to `request_level(0, 0.5f)` and No/back to the
pause menu. `new_game_pending` becomes obsolete; the `syn` test hook can keep setting `m_ok`.

### 9.3 hud.c: drawing page 0x1c
In `hud_title_draw`: `else if (page == 0x1c) { static const uint32_t items[3] = { 3, 5, 6 }; page_items(items, 3, 0.55f, sel); }`
(header 3 is never selected, so never hidden). Page 0x1f draws nothing.

### 9.4 Menu music
```c
static int g_title_music;                                   /* app+0x54; 0 in level_load */
static void title_page0(void) {                             /* 0x404e30 */
    if (++g_title_music == 2) g_title_music = 0;
    audio_music(g_title_music == 1 ? 0 : 48);
    title_page = 0; title_sel = 0;
}
```
In `level_load`: `g_title_music = 0;` and for `g_level == 0` `title_page0()` instead of `audio_music((title_n++ & 1) ? 0 : 48)`.

### 9.5 Logo
```c
void hud_logo_off(void) { H.logo_v = 0; }
/* in hud_title_draw, replaces the last lines: */
if (want_logo /* page 0 or 1 */) { H.logo_v += 10 * dt; if (H.logo_v > 5) H.logo_v = 5; }   /* 0x446ac0, before drawing */
... draw with alpha = (int)(50.8f * H.logo_v) ...
H.logo_v -= 5 * dt; if (H.logo_v < 0) H.logo_v = 0;                                        /* 0x446b00, after drawing */
```
`want_logo` = page 0 or 1 (and on page 1 not while the iris is opening). On New game, Load game and page 0x1f
`hud_logo_off()`. Blink phase: `void hud_menu_blink(float t) { H.menu_t = t; }` and in `hud_title_draw` /
`hud_menu_page` `H.menu_t += dt; if (H.menu_t > 0.5f) H.menu_t = 0;`.

### 9.6 Iris (for the load-return)
```c
void hud_iris(float v)                                       /* 0x4776d0, virtual 640x480 */
{
    glDisable(GL_TEXTURE_2D); glColor4f(0, 0, 0, 1); glBegin(GL_QUADS);
    for (int k = 0; k < 50; k++) {
        float a0 = k * (2 * 3.14159265f / 50), a1 = (k + 1) * (2 * 3.14159265f / 50), r = 0.99f * 480.0f;
        glVertex2f(320 + r * v * cosf(a0), 240 + r * v * sinf(a0)); glVertex2f(320 + r * v * cosf(a1), 240 + r * v * sinf(a1));
        glVertex2f(320 + r * cosf(a1), 240 + r * sinf(a1));         glVertex2f(320 + r * cosf(a0), 240 + r * sinf(a0));
    }
    glEnd();                                                  /* clipping to 0..640 x 0..480 is done by the viewport/scissor */
}
```
Animation: `v = from − (from − to)·min(t/dur, 1)`; page-1 enter (0 → 0.85, 0.5 s) only if the iris flag is on.

## 10. Uncertain

1. The default keys (Esc = action 9, LShift = 6, Space = 5, LCtrl = 4) come from PERSO_MOVE §3.1 (cfg default);
   a different Woody.cfg changes them. Only Enter (action 0xc on release) and Esc (menu back on release) are fixed.
2. Frame order: `SetVar(var1, 1)` in frame N is only seen in `Game_Frame`'s VM tick in frame N+1 (the menu update
   comes after `0x401ab0`); not dynamically verified.
3. Natural end: which of `var1 = 4` (script, 5300 ticks) and the end of the cinematic (53.0 s) happens first
   depends on the rounding of the VM clock; the screen is already black in both cases anyway.
4. `Cin_Abort 0x44f290` uses `+0x118` after the reset (not cleared) for the resume fade of the music; right after that
   `0x404e30` replaces the track anyway.
5. Menu text color `0xffffffff`: that the font quads become 2× brighter under MODULATE2X follows from HUD_TEXT §5.2, not
   from a dedicated trace through `0x43f890`; whether that's visible depends on the glyph colors.
6. Blend state of `0x482cf0` with flag 8 (assumed: the same alpha blend as RectVirtual flag 8); at alpha 255 it doesn't
   matter.
7. Slot 6 of the page vtables (base `0x460010` = 3, page 0x1c `0x460470` = 2) is not identified; unused on these pages
   in the paths read so far.
8. The direction Woody faces on starting in WWS (quat of instance 0) has not been computed; the port simply takes the
   `.ins` value.
