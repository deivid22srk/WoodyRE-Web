# MENU_OPTIONS.md — menu page 0x1b "Options" (Woody.exe, build 17-10-2001)

All statically from `out/disasm_full.txt` (imagebase 0x400000); not traced. Base class, blinking, logo and input actions:
TITLE.md §5.1/§5.2. Sound manager: SOUND.md §2/§4. Addresses without explanation are code in Woody.exe.

## 0. Summary

- Page 0x1b is a **regular list page** of the common class (TITLE §5.1): class `0x4601f0`, 0x20 B, vtable
  `0x4ab614`, item table `0x4b5eb0` (5 items), y-fraction 0.4, font size 30, all centered, white.
- Items: header **36 "Options"**, three sliders **38 "Sound FX volume"**, **39 "Music volume"**, **132 "Vibration"**
  (item flag 0x10) and **4 "Continue"** (result 5). A slider is drawn as **one line of text**
  `"<name> <value>%"`, e.g. `Sound FX volume 80%` — no bar, no arrows.
- Left/right: value −5/+5, clamped to 0..100, only on "just pressed" (no repeat while held), no sound.
  Each step is applied **immediately** to the sound engine (music too, even while the pause menu is up).
- **Continue** (confirm) keeps the new values; **back** (action 5) **restores the values from when the page was entered**. Both go
  to page 1 (title) or to the pause menu 0x18/0x19 (in a level).
- Storage: the volumes are the Woody.cfg globals `[0x4c2c50]` (sfx) and `[0x4c2c54]` (music), which are written back with
  the rest of the cfg on exit. In addition, **every save slot** in Woody.sav keeps a copy (music, sfx, vibration) that is
  restored on "Load game". The "4" in `u32 x[4]` of Woody.sav = **the 4 save slots**.
- **Vibration** writes `[joystick]+4` (float 0..1); the rumble method of the PC joystick class is an empty function
  (`0x467b20` = `ret 8`), so on PC **this option does nothing audible/perceptible**. The item is always present, even without a joystick.
- No sub-pages (no key-binding, controller or language page). Resolution, detail, VSync and key bindings live in Woody.cfg and are
  only ever changed by the setup program (`Detect.exe`/`Setup.dll`), never by this menu (DISPLAY.md §1–§2). The port adds a
  "Display" item and page (port extra, §9.4, DISPLAY.md §4).

## 1. Class, constructor, vtable

The menu object (`0x445e30`, TITLE §5.1) creates this page **first**: `new(0x20)` → `0x4601f0` → `menu+0x6c`
(= `menu + 4·0x1b`, `0x445e8a`). That happens once, at boot (`0x404d10` at the end of `0x4023a0`).
There is no separate base ctor; the ctor only sets the vtable and fills the item table:

```c
Options::Options() {                          /* 0x4601f0 */
    vt = 0x4ab614;
    item[1].value = cfg_sfx_vol;              /* [0x4b5ecc] = [0x4c2c50] */
    item[2].value = cfg_music_vol;            /* [0x4b5edc] = [0x4c2c54] */
    if (g_joy) item[3].value = ftol(g_joy->vib * 100.0f);   /* [0x4b5eec]; g_joy = [0x5e618c], 0x4a9010 = 100 */
}
```
Fields: `+4` selection, `+8` input delay, `+0xc`, `+0x10` result (base); **`+0x14` sfx backup, `+0x18` music backup,
`+0x1c` vibration backup (float)**. The item values themselves live in the **static table** in .data, not in the object.

| slot | address | function |
|---|---|---|
| [0] | `0x4462d0` | destructor (base) |
| [1] | `0x446640` | draw = base list renderer (**not** `0x45bd10`, so no logo fade-in) |
| [2] | `0x460460` | item table → `0x4b5eb0` |
| [3] | `0x460230` | count → **5** |
| [4] | `0x460450` | page id → **0x1b** |
| [5] | `0x446a80` | string of item i (base: `RckGet(item.stringref)`) |
| [6] | `0x460470` | returns **2** (base `0x460010` returns 3); no caller found (**uncertain**, see §10) |
| [7] | `0x446e10` | y-fraction **0.4** (`0x4aa394`) |
| [8] | `0x4462c0` | font size **30** (`0x4b39a8`) |
| [9] | `0x446aa0` | validate (base): `flags & 1 ? item.result : 0` |
| [10] | `0x446970` | down (base, wraps around, skips flag-2 items) |
| [11] | `0x446920` | up (base) |
| [12] | `0x460310` | **right**: base `0x4469d0` (+5) then applies it (§4) |
| [13] | `0x4603b0` | **left**: base `0x446a20` (−5) then applies it (§4) |
| [14] | `0x4602a0` | **back**: result 0x18 + revert (§4.3) |
| [15] | `0x460240` | **enter** (at `0x446490(0x1b)`, `0x4464ae`): base `0x4464c0` + backups (§4.1) |

## 2. Item table `0x4b5eb0` (16 B per item: stringref, flags, result, value)

| i | stringref | English | flags | result | value (address) | y (S = 30) |
|---|---|---|---|---|---|---|
| 0 | `0x20024` = 36 | Options | 2 (header, not selectable) | 0 | 0 | 192.0 |
| 1 | `0x20026` = 38 | Sound FX volume | 0x10 (slider) | 0 | sfx 0..100 (`0x4b5ecc`) | 238.5 |
| 2 | `0x20027` = 39 | Music volume | 0x10 | 0 | music 0..100 (`0x4b5edc`) | 285.0 |
| 3 | `0x20084` = 132 | Vibration | 0x10 | 0 | vibration 0..100 (`0x4b5eec`) | 331.5 |
| 4 | `0x20004` = 4 | Continue | 1 (selectable) | **5** | 0 | 378.0 |

`y = 0.4·480 = 192`, cell = `62·30/40 = 46.5` (TITLE §5.1). Strings 37 "Volume", 133 "On" and 134 "Off" are in the
Common table but the exe **never** references them (no dword `0x20025/0x20085/0x20086` in .data/.text as an
operand; console leftover: PS2 vibration on/off).

## 3. Drawing (`0x446640`, base; per frame from `0x4464f0`)

1. Size S = 30; shrinks (−1, min. 15) while an item **name** (only `vt[5]`, without the `" 80%"` suffix) is ≥ 640 wide
   (`0x446668..0x4466d8`). With these five strings, S stays 30.
2. Per item i (y starts at `0.4·480`): header items (flag 2) always; the others only once `+8 ≤ 0` (`0x44671a`). Enter sets
   `+8 = 0`, so everything is shown immediately.
3. x: without an alignment flag, centered, `x = 320 − w/2`. **Flag 0x10** (`0x4467e0..0x44689c`) builds a new u16 string and
   centers that instead:
   ```c
   u16 num[..], buf[..], sp[2];
   font_itoa(font, item.value, num);            /* 0x441820: digits via Common string 0, negative -> 0 */
   strcpy16(buf, RckGet(item.stringref));       /* 0x441730(src, dst) */
   sp[0] = RckGet(0x20028)[1]; sp[1] = 0;       /* string 40 = "a a": the middle character is the space glyph (code 18) */
   strcat16(buf, sp);                           /* 0x441770(src, dst) = dst += src */
   strcat16(buf, num);
   strcat16(buf, RckGet(0x20007));              /* string 7 = "%" */
   Measure(buf, &w, &h);  x = 0.5·640 − 0.5·w;  /* 0x44687a..0x44689c */
   ```
   Result: `Sound FX volume 80%`, `Music volume 100%`, `Vibration 0%`. **No bar, no arrows, no color difference**;
   the same font size and color (white, `font+0x60 = 0xffffffff`) as the rest.
4. Blinking (base): the **selected item is not drawn** while the blink phase `[0x5d7b1c] < 0.25`
   (`0x4468ab`); the phase runs 0..0.5 and falls back to 0 (2 Hz). On a slider, the whole line including the number blinks. After a ±5
   step, the base sets the phase to **0.25** (`0x446a08`, `0x446a57`): the line is immediately visible and only disappears
   0.25 s later.
5. Then the logo (`0x446b00`, TITLE §5.2): because slot [1] does not call the fade-in `0x446ac0`, **the logo fades
   out over 1.0 s** on this page (v −= 5·dt) if you come from page 1; in a level (`app+0x68 ≠ 0`) it is not drawn.
6. No iris (that belongs to page 1, `+0x38`, TITLE §5.4).

## 4. Input

Via `0x4464f0` (TITLE §5.1): only while `+8 ≤ 0`; actions on `[0x5e6188]`, all **"just pressed"** (`0x467420`):
2 up `vt[11]`, 3 down `vt[10]`, 1 right `vt[12]`, 0 left `vt[13]`, 0xc confirm `vt[9]` (if `+0xc == 0`),
5 back `vt[14]`, and `[0x5e6194]->vt[5](0)` also `vt[14]`. **No auto-repeat**: a held arrow key gives one step.
No input delay (enter sets `+8 = 0`). No SoundFx sound at all on this page (not even on entry: enter
calls `0x4464c0`, not the panel enter `0x45b8c0` that plays SoundFx 0x3f).

### 4.1 Enter `0x460240`
```c
base_enter();                       /* 0x4464c0: sel = 0, +8 = 0, +0xc = 0; item 0 is a header -> vt[10]: sel = 1, phase 0 */
bak_sfx = cfg_sfx_vol;  bak_mus = cfg_music_vol;          /* +0x14, +0x18 */
item[2].value = cfg_music_vol;  item[1].value = cfg_sfx_vol;
if (g_joy) { bak_vib = g_joy->vib; item[3].value = ftol(g_joy->vib * 100); }   /* +0x1c */
```
The cursor thus always starts on **"Sound FX volume"**, invisible for the first 0.25 s (phase 0).

### 4.2 Up/down
Base: wraps around, skipping the header (item 0): down from Continue → Sound FX volume; up from Sound FX volume →
Continue. Up always sets the phase to 0, down sets it to 0 when it moves and to 0.25 when there was nothing to move to.

### 4.3 Left/right `0x4603b0` / `0x460310`
```c
void right() {                      /* 0x460310; left = 0x4603b0 with base_left() */
    base_right();                   /* 0x4469d0: only if item[sel].flags & 0x10: value += 5, > 100 -> 100, phase = 0.25 */
    Item *it = &item[sel]; if (!(it->flags & 0x10)) return;
    switch (sel) {
    case 1: cfg_sfx_vol = it->value;                                  /* [0x4c2c50] */
            if (snd_sys()) snd_sys()->SetSfxVolume(cfg_sfx_vol);      /* [0x5e81b0]->vt[0x54] = 0x469570 */
            break;
    case 2: cfg_music_vol = it->value;                                /* [0x4c2c54] */
            if (snd_sys()) snd_sys()->SetMusicVolume(cfg_music_vol);  /* vt[0x5c] = 0x4695a0 */
            if (snd_mgr) snd_mgr->MusicUpdate();                      /* [0x5e61a4]->vt[0x44] = 0x46cbf0 -> 0x46c760 */
            break;
    case 3: if (g_joy) g_joy->vib = it->value * 0.01f; break;         /* 0x4aa0ac */
    }
}
```
Step 5, clamp 0..100 (`0x4469fd`, `0x446a4d`); no wraparound. The value is not rounded to a multiple of 5: a cfg value
73 gives 78, 83, … 98, 100. Confirming on a slider gives result 0 (flag 1 is absent) = nothing.

### 4.4 Back `0x4602a0` = cancel
```c
result = 0x18;                                     /* 24 = back */
cfg_sfx_vol = bak_sfx;  cfg_music_vol = bak_mus;
if (snd_sys()) { snd_sys()->SetSfxVolume(cfg_sfx_vol); snd_sys()->SetMusicVolume(cfg_music_vol); snd_mgr->MusicUpdate(); }
if (g_joy) g_joy->vib = bak_vib;
```
The table values are not restored, but the next enter overwrites them. Only **Continue** thus keeps the change.
(Exception: without a joystick there is no backup and the static vibration value simply stays as it is.)

## 5. What the options do

### 5.1 Sfx and music
`0x469570(v)`: `[0x5e81cc] = v`, **`[0x5e81ec] = v·0.01`** (sfx master). `0x4695a0(v)`: `[0x5e81d0] = v`, **`[0x5e81f0] =
v·0.01`** (music master). (`0x4695d0` = third volume `[0x5e81f4]`, not used by this menu.) The same globals are set from
the cfg at sound engine startup (`0x4691e2..0x4692b9`, SOUND §2.4).

- **Sfx**: per 2D voice `ftol(cur · [0x5e81ec])` (`0x46b791`), 3D in software mode `cur · gain · [0x5e81ec]` (`0x46b6a7`),
  to the lib `0x48f410` → `0x48f2a0`. Applied in the manager's per-frame update; in the pause menu all
  sfx voices are suspended (`0x404d8f`, `mgr+0x10 = 0`), so an sfx change is only heard there after "Continue".
- **Music**: `0x46cb30`: target = `[0x5e81f0]`, `vol = lerp(vol, target, clamp((now − t0)/T, 0, 1))` (`0x46cb70`) — outside
  a fade, `t = 1`, so immediately at the target — and `lib_SetVolume(ftol(vol·100))` (`0x48f4e0`). The menu calls this update
  itself (`0x4602f2`, `0x460383`), because the manager update is stalled in the pause menu; the music stream itself
  keeps playing during the pause.
- **Curve** (`0x48bf50(min 0, max 100, v)`): `v == 0 → −10000` mB (silent), `v == 100 → 0`, otherwise
  **`mB = −2000·log10(100/v)`** (`0x4abec0` = −2000), then `× lib+0x48 / 100` (lib master, init 100 `0x48b88b`) →
  `IDirectSoundBuffer::SetVolume` (vt 0x3c, `0x48f312`). −2000·log10(100/v) mB = 20·log10(v/100) dB: **linear amplitude
  v/100**. For sfx, the voice volume and the master multiply together before the conversion: amplitude = `voice/100 · sfx/100`.

### 5.2 Vibration
`[0x5e618c]` = joystick object (0x5c B, ctor `0x4678d0`, vtable `0x4ab90c`), only present if DirectInput found a
connected joystick via `EnumDevices(DIDEVTYPE_JOYSTICK, …, DIEDFL_ATTACHEDONLY)` (`0x467926..0x467943`);
`+4` = vibration strength, init **1.0** (`0x4674b0`). The only "rumble" call `0x44d1b0` (Perso, not when `perso+0x21c == 2`)
goes to `vt[2]` = `0x467b20` = **`ret 8`**. No reader of `+4` other than the menu and the save (§6.2). Without a joystick,
the item shows the static starting value **0%**, with a joystick **100%**.
Port: the value (woodyre.cfg `rumble=`, default 100) scales the rumble of the pads of `src/pad.c` (PORT EXTRA, INPUT.md 6);
moving the slider gives a short rumble at the new strength. The old key `vibration=` is no longer read.

## 6. Storage

### 6.1 Woody.cfg (setup + exit)
`Woody.cfg` = `u32 0x19072001` + 0x11c B that go 1:1 to `0x4c2bd0` (TRACING §2). Relevant fields (struct offset /
file offset): `+0x68/0x6c` sfx on `[0x4c2c38]`, `+0x6c/0x70` music on `[0x4c2c3c]`, **`+0x80/0x84` sfx volume
`[0x4c2c50]`**, **`+0x84/0x88` music volume `[0x4c2c54]`**, `+0x88/0x8c` third volume. The game writes back **the whole block**
on exit (`0x401130`, after `App::Frame` = 1, GAMEFLOW §3): this is how the menu volumes survive a restart. Resolution,
bpp, VSync, detail (`[0x4c2c0c]`), speaker configuration and key bindings (`+0xac`, `+0xdc`) are in the same block but
**this menu does not touch them**; the only writers of `[0x4c2c50]/[0x4c2c54]` are the options menu and the slot-load code (§6.2).

Note: the `game/Woody.cfg` produced by `tools/native/mkcfg.c` has sfx/music **off** and both volumes **0** (all
bytes 0x5c..0xab are 0); with that cfg, the page shows `0%`. What defaults Detect.exe writes with a real sound card
has not been investigated (**uncertain**).

### 6.2 Woody.sav, per save slot
Slot manager `app+0x4c` (file starts at `mgr+4`, GAMEFLOW §6.1). Accessors: `0x456e40(mus, sfx, slot)` writes
`[mgr+0x5298+4·slot] = mus`, `[mgr+0x52a8+4·slot] = sfx`; `0x456e60(slot)` reads music, `0x456e70(slot)` sfx;
`0x456e80/0x456e90` read/write the float `[mgr+0x52b8+4·slot]`. In **file offsets**:

| offset | contents |
|---|---|
| `0x5294` | `u32 musicvol[4]` (per slot) |
| `0x52a4` | `u32 sfxvol[4]` (per slot) |
| `0x52b4` | `float vibration[4]` (per slot, = joystick+4) |

(**GAMEFLOW §6.1 has sfx and music swapped**: `0x5294` is music, `0x52a4` is sfx.)

- **Saving** (after a level, page 6 → 5): empty slot `0x405536`: volumes + (if there is a joystick) vibration into the slot,
  then `0x456dc0` + write; occupied slot after "overwrite?" `0x405609`: only the volumes (vibration is **not**
  updated there). The menu itself never writes to Woody.sav.
- **Loading** (page 2, `0x4052db`): `[0x4c2c54] = musicvol[slot]`, `[0x4c2c50] = sfxvol[slot]`, `joy+4 = vibration[slot]`,
  then SetSfxVolume/SetMusicVolume (`0x40531b..0x405347`; no explicit MusicUpdate, the manager does that next
  frame). So "Load game" restores the sound settings of that slot, and those also go into Woody.cfg on exit.
- New game (`0x44ffa0`) and the slot ctor (`0x456d20`) do **not** initialize the arrays (**uncertain** what is in
  never-written slots; the whole file is read/written in one go).

## 7. Leaving, return, overlay

- Handler `0x40575c` (jump table `0x405b1c[0x1b]`): result **5** (Continue) or **0x18** (back), nothing else:
  - `app+0x68 == 0` (House/title): `0x446490(1)` → page 1. The panel enter `0x45b8c0`/`0x460070` plays **SoundFx 0x3f**,
    puts the cursor on item 0 **"New game"** (not back on "Options"), and the logo fades back in (1.0 s).
  - in a level: `0x404d80` = reopen the pause menu: `app+0x20 ->vt[0x88]()` (Suspend, again), page **0x19** if
    `perso+0x21c == 1` and no BlackBox (`0x41fa80(200.0)` on `[0x4c737c]`), otherwise **0x18**; state 0. Pause enter
    `0x45b390`: cursor on "Continue", logo `v = 0`.
- Reached from page 1 result 6 (`0x4051b0` ff.) and from the pause menu result 6 (`0x40587a`).
- **Background** (`0x404e90`, byte table `0x405af8[0x1b] = 5` → `0x404ee8`): world rendered via `0x401ab0`;
  in a level, the world is paused (`app+0xf4 |= 8`) and a **half-black quad** `RectVirtual(0,0,640,480, 0x80000000 ×4)`
  (`0x404f1a..0x404f53`); in House, no overlay and no pause (the title camera keeps turning). HUD: `0x448450(2)` =
  hidden (only 0x18/0x19 get state 1, the extended HUD), so the HUD disappears on the options page when coming from the pause menu.
- Esc (action 9) does nothing on this page.

## 8. Sub-pages

None. No item has a result other than 5, and the handler only knows 5/0x18. Key bindings, controller mode,
resolution and detail are only ever set on PC by Detect.exe (`Setup.dll`, `DefaultControlSettings`/`SaveConfig`); the
language is per-install (HUD_TEXT §1.5); the language/memory-card pages 0x16/0x21 are console leftovers.

## 9. Recipe for the port

### 9.1 State
```c
/* src/main_engine.c */
typedef struct { uint32_t magic; int32_t sfx, music, vib; /* port-only, after the original: */ int32_t aspect, res, fullscreen, vsync; } Options;
static Options g_opt = { OPT_MAGIC, 100, 70, 100, 0, 0, 0, 1 };   /* defaults: sfx 1.0 / music 0.7 = current audio.c values */
static int32_t g_opt_bak[3];                                        /* +0x14 / +0x18 / +0x1c */
```
Store this in a **dedicated `woodyre.cfg`** (analogous to Woody.cfg: read at boot before `audio_init`, written on
exit and on Continue), **not** in `woodyre.sav`: that has a fixed binary struct with magic `WSV2` that gets thrown
away on every format change (`save_read`), and new port options would then invalidate progress. A text format
`key=value` (unknown keys ignored, missing keys = default) lets aspect/resolution be added later without a version bump.
The original's per-slot copy (§6.2) is optional: the port has a single save; anyone wanting to replicate it stores
`sfx/music/vib` in `g_save` and applies them in the "Load game" path.

At boot: `audio_master(g_opt.sfx * 0.01f, g_opt.music * 0.01f)` — **linear is exactly** what the original does (§5.1: mB =
20·log10(v/100)·100). `audio.c` already multiplies voice·`A.m_sfx` and stream·`A.m_mus`; `audio_master` is however not yet
called anywhere. Vibration: store and show the value; only pass it on if rumble is ever added (original: no-op).

### 9.2 Page (hud.c)
`page_items` currently knows no flags. Add a variant with item flags, using the existing `row_*` helpers (which already
use the string-40 space trick):
```c
typedef struct { uint32_t id; uint32_t flags; int value; } MenuItem;          /* flags: 1 selectable, 2 header, 0x10 slider */
static void page_items_ex(const MenuItem *it, int n, float yfrac, int sel)   /* 0x446640 */
{
    float S = 30.0f;
    for (int i = 0; i < n; i++) { const uint16_t *s = hud_string(it[i].id); if (!s) continue;
        while (S > 15.0f) { font_size(S); if (font_measure(s) < 640.0f) break; S -= 1.0f; } }   /* only the name */
    font_size(S);
    float y = yfrac * 480.0f, cell = font_cell();
    for (int i = 0; i < n; i++, y += cell) {
        row_reset(); row_str(it[i].id);
        if (it[i].flags & 0x10) { row_space(); row_num(it[i].value); row_str(7); }          /* "name 80%" */
        if (i == sel && H.menu_t < 0.25f) continue;
        font_draw(320 - font_measure(g_row) * 0.5f, y, g_row, 0xff808080);
    }
    font_size(17.0f);
}
void hud_menu_page_ex(const MenuItem *it, int n, float yfrac, int sel, float dt);  /* ticks H.menu_t like hud_menu_page */
void hud_menu_blink(float phase);                                                   /* 0 or 0.25 after a step (§3.4, §4.2) */
```
(`row_reset`/`row_str`/`row_space`/`row_num` currently sit after `page_items` in hud.c, near the results screen; move them
up.) Logo: `hud_title_draw` currently sets
`logo_v = 0` as soon as the logo is not wanted; for page 0x1b it must **fade out**: `logo_v -= 5·dt` (min 0) and keep
drawing while `logo_v > 0` (0x446b00). Draw items before the logo.

### 9.3 Logic (main_engine.c, title block at `title_page`)
```c
static const MenuItem k_opt_base[5] = { {36,2,0}, {38,0x10,0}, {39,0x10,0}, {132,0x10,0}, {4,1,0} };
enter (page 1, item 2 "Options"):  title_page = 0x1b; opt_sel = 1; hud_menu_blink(0);
                                     g_opt_bak = {sfx, music, vib};              /* no audio_fx */
per frame (only "just pressed", no repeat):
  up/down: wraps over 1..4 (skipping item 0); blink = 0 (down with no movement: 0.25)
  left/right on 1..3: v = clamp(v -/+ 5, 0, 100); blink = 0.25; applied immediately:
       audio_master(g_opt.sfx*0.01f, g_opt.music*0.01f)
  confirm on 4 (Continue): opt_save(); back
  back (Esc/Backspace; original action 5):  g_opt = bak; audio_master(...); back
back: g_level == 0 ? (title_page = 1, title_sel = 0, audio_fx(63)) : pause menu (0x18/0x19, sel 0)
drawing: g_level == 0 ? no overlay : half-black plane 0x80000000 + hide HUD; hud_menu_page_ex(items, 5, 0.4f, opt_sel, dt)
```
The pause menus 0x18/0x19 are ported (INPUT.md §5); their "Options" (result 6) leads here.
Note the keys: in the port, Space is now also "confirm"; in the original, action 5 (back) is Space by default and
confirm is the jump key. Pick one fixed set for the whole port (proposal: Enter/Space = confirm, Esc/Backspace = back).

### 9.4 Place for the new options (issue #12: aspect ratio, 4K)
The font has all the glyphs needed for e.g. `Display`, `Aspect ratio 16:9`, `Resolution 3840x2160`, `Fullscreen`
(`x` = code 75, `:` = 30; only `j Z - & "` are missing), and strings 133 "On" / 134 "Off" / 16 "BACK" already exist.
Port-only text must go through the ASCII→code table (HUD_TEXT §1.4) to become u16.

Proposal that leaves the original layout intact: **the five items stay at y 192..378**; add **one item after Continue**
(i = 5, y = 424.5, cell bottom 471 < 480), e.g. `Display` (flag 1, port-own result) that leads to a **port-own page**
(id ≥ 0x40, same `page_items_ex`, y-fraction 0.4) with: aspect ratio (4:3 / 16:9 / follow window), resolution
(list of monitor modes), fullscreen, VSync, and "Continue"/"BACK". A choice item gets a new flag (e.g. 0x100:
`name + space + choice text`, left/right switches it) alongside 0x10. A compat switch can hide the extra item for a
pixel-exact original page. Resolution/aspect belong in `woodyre.cfg` (like Woody.cfg has them for the original), not
in the save. Reverting on "back" also applies to this page (backup on enter), but a mode switch is only applied on
Continue.

**Implemented** (port extra) as proposed: item 5 "Display" → port page **0x40** with aspect ratio, window size, fullscreen,
VSync, frame rate limit and Continue; choice flag **0x100**; stored in `woodyre.cfg`. What the original does for display
mode and frame pacing, and the port's behaviour, overrides and test hooks: **DISPLAY.md**. (The compat switch to hide
item 5 was not added.)

## 10. Uncertain

1. Slot [6] (`0x460470` → 2, base `0x460010` → 3): no caller of `vt+0x18` found on a menu page anywhere in
   `0x401000..0x406200` and `0x445e00..0x463000`; meaning unknown (perhaps dead).
2. Default sfx/music volume in Woody.cfg as Detect.exe writes it with a real sound card (the mkcfg cfg has 0/0 and
   sound off). The port defaults 100/70 are our own choice.
3. Whether an already-playing 2D voice (without a fade) picks up its new sfx volume every frame or only at the next
   `0x46b780` call; the exact caller conditions of `0x46b6xx/0x46b78x` in `0x46bbb0` have not been traced.
4. The "back" key via `[0x5e6194]->vt[5](0)` (TITLE §10 point 4) and the default key for action 5.
5. Contents of the volume arrays in Woody.sav for never-written slots (uninitialized, §6.2).
6. Whether the vibration value does anything through some other route (e.g. Setup.dll/force feedback outside the exe); in the exe the
   rumble method is empty.
7. Everything is static; a trace (breakpoints on `0x460310`, `0x4602a0`, `0x469570`, `0x4695a0`) would confirm the steps and
   the revert.
