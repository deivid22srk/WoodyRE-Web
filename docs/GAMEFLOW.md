# Gameflow: boot, menu, level transitions, save (Woody.exe, build 17-10-2001)

Status: **working document, updated incrementally.** Static analysis; all addresses refer to
Woody.exe (imagebase 0x400000) and can be looked up with `python tools/drange.py START END`.
Scripts: `python tools/ekodisasm.py extract/Data/<LVL>/code`.

Goal: document how the original goes from level to level, so that `src/main_engine.c` (which currently
loads exactly one level from the command line) can reproduce the full game flow.

## 0. Summary

- There is one **App object** (`[0x4c2d00]`, init `0x4023a0`) with a state machine `app+0x00`:
  0 = menu (`0x404e90`), 1 = game (`0x401ab0`), 2 = logos (`0x401500`), 3 = game in BlackBox mode.
  Per-frame dispatcher: `0x401590` (switch `0x4017f4`).
- Levels are loaded via **`LoadLevel(index)` = `0x4040c0`** → path from the level table `0x4b12a0`
  (29 pointers) → `0x404140(path, index)`.
- A level switch is *requested* with **`RequestLevel(fade_s, level, state, menupage)` = `0x404b60`**:
  starts a fade-out of `fade_s` seconds; once it reaches 0, `0x401590` performs the switch
  (`0x4049a0` unload → `0x4040c0(level)` → `0x401400(state)` → menu page).
- Script messages that do this: **1081 `GotoLevel(level)`** (fade 1.5 s, state 1), **1083 `EndLevel()`**
  (fade 0.5 s, back to the hub of the current character, statistics kept), **1180** (to Credits).
  **1084 `GetPrevLevel(var)`** returns the index of the previous level: the hub script uses that to
  pick its own spawn point.
- The main menu is an **engine menu** (page object `app+0x3c`, 34 pages) drawn over the rendered
  level **House** (index 0). House is thus the menu background + intro cinematic, not a playable
  level. "New game" → intro cinematic in House → level 1 (WWS).

## 1. The level table `0x4b12a0`

29 × `char*` (4 bytes per record), only reader `0x404124` in `LoadLevel` (`0x4040c0`/`0x404113`).
The path is prefixed with the CD base by `0x44ff00` (`[app+0x44]` = config/paths object `[0x5e5814]`).

| idx | path | role | character (`0x404830`) |
|---|---|---|---|
| 0 | `\Data\House\House.gel` | menu background + intro | 0 Woody |
| 1 | `\Data\WWS\WWS.gel` | hub Woody | 0 |
| 2..10 | W1A W1B W2A W2B W2D W3A W3B W3C W3D | Woody levels | 0 |
| 11 (0x0b) | `\Data\KWS\KWS.gel` | hub Knothead | 1 |
| 12..17 | K1A K1R K2A K2R K3A K3R | Knothead levels (R = race) | 1 |
| 18 (0x12) | `\Data\SWS\SWS.gel` | hub Splinter | 2 |
| 19..24 | S1A S1R S2A S2R S3A S3R | Splinter levels | 2 |
| 25 (0x19) | `\Data\BlackBox\BlackBox.gel` | BlackBox mode (state 3) | 0 |
| 26 (0x1a) | `\Data\Credits\Credits.gel` | credits (message 1180) | unchanged |
| 27 (0x1b) | `\Data\BlackBox\BlackBox.gel` | dev slot (level-picker dialog, cfg flag 4) | unchanged |
| 28 (0x1c) | `\Data\Lang\Lang.gel` | background language/save selection at boot | unchanged |

Character selection: `0x404758` — `if (index <= 0x19) cfg+0x380 = byte_404830[index]` (0 = Woody
`\Common\Woody`, 1 = `\Common\Knothead`, 2 = `\Common\Splinter`; `0x404797..0x4047df` loads the
corresponding resource folder). For index > 0x19 the previous character stays selected.

Lang, Blackbox and Credits have a one-object script that only does `SetTypeInstance(inst0, 1)`
(the player) and stops the music (`1655`); they are empty sets for engine menus.

## 2. App object (`[0x4c2d00]`)

| offset | meaning | source |
|---|---|---|
| +0x00 | state: 0 menu, 1 game, 2 logos, 3 game (BlackBox) | `0x401400`, switch `0x401621` |
| +0x04 | exit code / "stopping" (1 when `+0xcd` is set and the fade is done) | `0x4017e3` |
| +0x18 | 0x688-byte message-pump object (per level) | `0x404873` |
| +0x24 | level (gel) object 0x78 B, `0x426bd0` | `0x4044b8` |
| +0x28 | time/world object (`+0x38` = dt, `+0x30` = game time) | `0x401810` |
| +0x2c | game object 0x1c8 B (`0x4457b0`), recreated per level | `0x4041d7` |
| +0x30 | 0x54-byte object `0x4470d0` (HUD?) per level | `0x404925` |
| +0x34 | player (Perso) = `[0x53a34c]` after script init | `0x4048ad` |
| +0x3c | menu object 0x94 B (`0x445e30`); page via `0x446490(page)`, read `0x446430` | `0x404d10` |
| +0x44 | config `[0x5e5814]`: `+0x378` debug mode, `+0x380` character, `+0x384` dev flags | |
| +0x48 | **save struct** 0x14a4 B (`0x44ff70`) = `[0x5e5818]` | `0x402587` |
| +0x4c | save-slot manager 0x52c8 B (`0x450a50`), 4 slots | `0x4025b0` |
| +0x50 | frame counter since load (first 4 frames black, `0x4016f8`) | |
| +0x54 | menu music-switch counter | `0x404e30` |
| +0x58..+0x64 | menu intermediate state (slot, wait frames, next page) | |
| +0x68 | **current level index** | `0x40417e` |
| +0x6c | **previous level index** (init 0x1b) | `0x404176`, `0x4025e4` |
| +0x70 | byte, message 1172 sets it to 1; reset on load | `0x404230` |
| +0x74..+0x87 | 5 dwords, statistics of the level just finished (copy of `perso+0x710..`) | `0x404c21` |
| +0x7c | play-time counter `[0x4c532c]` | `0x404c34`, `0x40177c` |
| +0x88 | logo player (`+8` = logo index) | `0x40260c` |
| +0x8c, +0x90 | message 1160: script variable + instance of the intro cinematic | `0x444950` |
| +0x94 | byte: 1 = "New game chosen" during page 0x1f | `0x4051ca` |
| +0x98 | float idle timer for the title screen (35 s, `0x4a9000`) | `0x402653` |
| +0x9c | 4 × fader {float remaining, float total, byte direction} (12 B) | `0x401440/0x401480/0x445bf0` |
| +0xcc | byte `m_bHaveToClose`: fade-out into a level switch in progress | `0x404b6b` |
| +0xcd | byte: exit after the fade (`0x404cb0`) | |
| +0xd0 | float remaining fade time | |
| +0xd4 | byte: a level is queued | |
| +0xd8, +0xdc, +0xe0 | target level, target state, target menu page | `0x404bbe` |
| +0xe4 | BlackBox object 0xc0780 B (`0x484420`), only for level 0x19 or cfg flag 0x10 | `0x4042d5` |
| +0xec | language extension string (`.eng .fra .ita .esp .all`) | `0x405a5c` |
| +0xf4 | bit flags: 1 = frame in progress, 8 = world paused, 0x10 = level loaded | |


## 3. Boot

1. WinMain (message loop `0x405ea1`, in function `0x405e78`) creates the App object (`0x4060c0` → init
   `0x4023a0`, global `[0x4c3a90]` = `[0x4c2d00]`) and calls `App::Frame` = `0x401590` each frame;
   return value 1 ⇒ write `Woody.cfg` (`0x401130`) and WM_CLOSE.
2. End of `0x4023a0` (`0x402582..0x402666`): menu object (`0x404d10`), save struct (`0x44ff70(1)`,
   immediately emptied = "new game" values), slot manager (`0x450a50`), `app+0x6c = 0x1b`, logo player.
   - cfg flag `[cfg+0x384] & 4` (dev): `LoadLevel(0x1b)` → `0x4040c0` then shows a **level-picker
     dialog** (`0x4066c0`, DialogBoxParam resource 0x66) and loads the chosen path with index 0x1b.
     No logos.
   - normal: `app+0x98 = 35.0`, state = 2, logo 0 starts (`0x445d00(0)`).
3. State 2 = `0x401500`: logo table `0x4b3960` = `\Logo\Cryo.hnm`, `\Logo\Eko.hnm`, `\Logo\Universal.hnm` (player and format: docs/HNM.md).
   Key 9 (`0x467400(9)`) aborts the currently running logo. When the logo is finished
   (`0x445de0`): index 0 → logo 1, 1 → logo 2, 2 → done:
   - `[app+0x4c]->vt[1]() == 2` → `LoadLevel(0x1c)` (Lang) + menu page 0x16 + state 0. On the PC
     `vt[1]` = `0x4078b0` = `return 0`, so **this branch is dead** (console leftover: memory-card check).
   - otherwise: **`LoadLevel(0)` (House)** and `0x404e30` = menu page 0 (title screen) + state 0 + music.
4. `wtrace.py --level` overwrites `0x4b12a0[0]`; the game is then still in state 0 with the menu
   drawn over that level (index stays 0).

## 4. Level switch

### 4.1 Requesting: `0x404b60(float fade, int level, int state, int page)` (thiscall on App)
`+0xd4 = 1`, `+0xcc = 1`, `+0xd0 = fade`, starts the fade-out `0x401480(fade)` (fader slot with
direction 1), fades the music out (`[0x5e61a4]->vt[0x4c](fade*0.9)`, `vt[0x9c]()`), `+0xd8/+0xdc/+0xe0
= level/state/page`.

Callers (complete list, `grep "call    0x404b60"`):

| where | fade | level | state | page | meaning |
|---|---|---|---|---|---|
| message **1081** `0x445057` | 1.5 | arg0 (raw int, no mask) | 1 | 0 | **GotoLevel(level)** – doors in the hubs |
| message **1083** `0x445106` → `0x404be0(0.5)` | 0.5 | hub of the character (1 / 0xb / 0x12), or 0 | 1 (or 0) | 0 | **EndLevel** |
| message **1180** `0x4448b9` | 0 | 0x1a Credits | 0 | 0x20 | credits (arg ignored) |
| `0x401d35` (in the frame, BlackBox object `0x4846d0` reports done) | 0.5 | 0x1a | 0 | 0x20 | credits |
| menu page 3, result 14/15/16 `0x4056e3..` | 0.4 | 1 / 0xb / 0x12 | 1 | 0 | world choice after "load game" |
| menu page 3, result 17 `0x405725` | 0.4 | 0x19 | 3 | 0 | BlackBox |
| menu page 0x1f `0x4050f3` | 0 (intro watched through) or 0.5 (skipped) | 1 | 1 | 0 | **New game → WWS** |
| menu `0x405780` (pages 0x16, 0x1c-yes, 0x1d, 0x20, 0x21) | 0.5 | 0 | 0 | 0 | back to House/title |
| menu page 0x21 `0x405ac9` | 0.5 | 0x1c | 0 | 0x16 | (dead console branch) |

`0x404be0` (EndLevel) in detail:
```
cur = app+0x68
if cur in {1, 0xb, 0x12, 0x19}: RequestLevel(fade, 0, 0, 0); return      // leaving a hub = go to title
save_set_done(app+0x48, cfg+0x380 /*character*/, cur)                     // 0x450700: rec+4 = 1
memcpy(app+0x74, perso+0x710, 20)                                         // level statistics
app+0x7c = [0x4c532c]                                                     // (overwrites stat[2])
RequestLevel(fade, character==0 ? 1 : character==1 ? 0xb : 0x12, 1, 0)
```

### 4.2 Executing: in `App::Frame` `0x401590`
After rendering the frame (`0x4016d9..`): draw the 4 faders (`0x445bf0`, full-screen quad
640×480 with alpha = remaining/total·254, direction 1 = towards black), first 4 frames after a load
are black (`app+0x50 < 4` → `0x4014c0`). Then:
```
if (app+0xcc) { app+0xd0 -= dt; if (app+0xd0 <= 0) present black (0x47ef80); }
if (app+0xcc && app+0xd0 <= 0) {
    app+0x7c = [0x4c532c];
    Unload();                       // 0x4049a0, sets +0xcc = 0
    if (app+0xd4) {
        LoadLevel(app+0xd8);        // 0x4040c0
        app+0xd4 = 0;
        SetState(app+0xdc);         // 0x401400 (overwrites the state 1 that LoadLevel sets)
        if (state == 0) { if (app+0xe0 == 0) TitlePage(); /*0x404e30: page 0 + music*/
                          else menu_set_page(app+0xe0); }
    }
    if (app+0xcd) app+0x04 = 1;     // exit
}
```

### 4.3 Unload `0x4049a0`
Only if `+0xf4 & 0x10`. Warns if `+0xcc` was not set (`"The only way to close a level is to
set m_bHaveToClose to true !!!"`). Order: `0x44e770`, stop sound (`[app+0x20]` vt 0x8c/0x9c/0x4c/4),
free resources `0x4414b0(1)`, `0x4414b0(0)`, stop debug recording, `delete app+0x30`, `delete
app+0x2c` (game object `0x4457d0`), `delete app+0x18`, free the VM `0x442160`, level object
`app+0x24->vt[0](1)`, light system `[0x4c4cac]` (`0x40abd0`), `[0x54ba28]`, BlackBox object, `0x404b50`,
clear flag 0x10. **Nothing of the player survives in the Perso object**; everything that persists is
in the save struct (§6) and in `app+0x68/0x6c/0x74..`.

### 4.4 Load `0x404140(path, index)`
```
app+0x6c = app+0x68;  app+0x68 = index;                 // previous / current level
reset_globals()  (0x404360: counters, 0x42a5d0, enemy/bonus/projectile/effect lists cleared)
menu_reset(app+0x3c) (0x446410);  app+0x54 = 0
path without extension → new 0x1c8 game object (0x4457b0) → app+0x2c
LoadFiles(path)  (0x4043d0): reset the time object, load "<dir>\code" (0x4424b0, 0x442570),
      level object 0x426bd0(path) → app+0x24, world 0x408260(path), light system 0x40ac30(path,0x10,0x400)
      ("Can't load lightsystem : please rebuild lights !!!"), 0x42a680, 0x4369a0.., 0x42d2c0
app+0xf4 &= ~8; app+0x50 = 0; app+0x70 = 0
LoadResources(path, index) (0x4045c0): register resource handlers, pick character from
      byte_404830[index] (only index <= 0x19), load "\Common\<Woody|Knothead|Splinter>" + language
      extension (0x441230), then the level resources; font 0x1030000 is mandatory
      ("No font in your level...")
set eko time (0x444050(time*100))
ScriptInit (0x404850): 0x4427e0 (double init pass), callbacks 0x441ed0, new 0x688 message pump,
      one call to 0x4019c0 (tick + route messages → this is where every SetTypeInstance runs),
      app+0x34 = [0x53a34c] (the Perso)
0x4048d0: activate all lights; 0x404900: link the game object to the Perso (0x445850), new 0x54 HUD
      object (0x4470d0), camera init 0x42a680
0x40c000; app+0xf4 |= 0x10; 0x404990
BlackBox object only if cfg&0x10 or index == 0x19 → state 3, otherwise state 1  (0x401400)
reset the 4 faders, fade-in 1.0 s (0x401440(1.0))
```
The Perso reset (`0x44a6a0`, called from the game init `0x4458e2`) pulls the player state from
the save struct (§6.2): lives, items, charges, health.

### 4.5 Spawn point
- **Default**: the position of the Perso instance in the level's `.ins` (`perso+0x30c` →
  `+0x1f4`, `0x44a6e6`). There is **no** engine table "coming from level X → spawn Y".
- **Checkpoints** within a level: message **1030 `SaveAuto(this)`** (`0x445129` → `0x44aa10`):
  `perso+0x318` = position of the instance, `+0x324` = direction (xz of the marker with typecode 0 of the
  instance, normalized; otherwise the current look direction). When dying with lives > 0, `0x44a810`
  respawns there (via `0x445930`). Not persistent. The respawn also clears the side view (`Perso+0x4ec = 0` in Reset
  `0x44ab20`) and hard-cuts the follow camera back (`0x458f90`): if you die in a side-view section
  you end up back in 3D at the last checkpoint, usually right before the door of that section (PERSO_DEATH §3.4).
  `1030` with instance 0 takes Woody's current position and facing (`0x44a920(NULL, 0)`). `SavePos.bin` (the `save = 1`
  paths of `0x44a920`/`0x44a810`) belongs only to the dev-flag debug keys, which the shipped exe cannot enable (PERSO_DEATH §3.4).
  (Message 1020 = `perso->vt[0x98](1)` + camera `0x459030`: belongs to the same kind of volumes; exact
  meaning not worked out.)
- **Back in the hub**: the hub script handles it itself. Object 297 in WWS (KWS/SWS analogous):
  init: `DELAY 1` → **1084 `GetPrevLevel(var51)`** (`0x4450ee`: `var = app+0x6c`). Then per value
  `var51 == N` (2..10): `1085(N+1, var54)` (is the next level already done?), **`1140(door_inst_N,
  var53)`**, door sound + door animation. `1140` (`0x4449e3` → `0x453d90`): the Perso is placed at the
  position/direction of the door instance (`perso+0x72c..0x740`, scripted action 0x4a, state 9),
  `perso+0x724 = 0`, `perso+0x728 = var`, the instance from message **1142** (`perso+0x748`) is placed
  at the same spot, and the menu switches to page 0x1e (results screen, `0x404df0`, state 0).
  If the player did not come from a level (`var51` = 0 or 0x1b) the `.ins` position remains in effect.
- **House**: message 1141 (`0x4448d1` → `0x44e640`) sets `perso+0x564..0x578` (pos+direction) and flag
  `+0x57c`; as long as `app+0x68 == 0` the frame calls `0x44e690` every tick (`0x401d0c`), which holds
  Woody in that pose with scripted action 0x49 (menu background).

### 4.6 The door in the hub (script, WWS around word 2819..2965)
```
player in volume 5 (VOL_FLAG5)      → 1080 help text; 1050(var20, 2): var20 = 1 if action key 6 was just pressed
var20 == 1                          → 1042(door 223, 500, 60, var21): player within 500 units of the door
                                       and looking within 60° of it → 0x458e40(perso), var21 = 1
var21 == 1                          → 1081(2)  [GotoLevel W1A, fade 1.5 s]
                                       1040(door 223, 17) [Perso scripted action 17 = walk through door]
                                       sound 1602, door anim (message 3)
```
Whether a door may open: **1082 `LevelIsEnable(level, var)`** (§6.3). End of a level (W1A, word
13794..13839): `1040(exit door, 17)`, door anim, sound, `DELAY 100` → **`1083(0)`**.

#### The check mark / cross next to the door

That is **one instance** with a texture group of two frames (WWS group 145: frame 0 = red cross,
frame 1 = green arrow; the light bar above the door is group 146, also red → green). Instances never
let their texture frames advance **on their own** (`0x47f290` only looks at the override on `inst+0xd8`,
INSTANCE.md §2), so without a message frame 0 stays put: the cross. Turning it green is done with
**message 16** `16 [marker, 0xffff, 1, 50]` = play the frames forward once, over 0.5 × the texture
duration, and stay on frame n−1.

The **init block** of each door object asks `1082 LevelIsEnable(level, var)` and thereby becomes a
watcher of that variable; once it becomes 1, the reactive block sends message 16 to the marker and sets
the variable to 2 (so it happens only once). From `out/wws_code.txt`:

| script object | 1081 GotoLevel | level | 1082 → var | door | marker |
|---|---|---|---|---|---|
| 217 + 193 | 2 | W1A | — (always open) | 223 | 193 |
| 218 | 3 | W1B | 26 | 224 | 194 |
| 322 | 4 | W2A | 84 | 323 | 180 |
| 324 | 5 | W2B | 89 | 325 | 181 |
| 326 | 6 | W2D | 94 | 327 | 182 |
| 328 | 7 | W3A | 99 | 329 | 157 |
| 330 | 8 | W3B | 104 | 333 | 158 |
| 331 | 9 | W3C | 109 | 334 | 159 |
| 332 | 10 | W3D | 114 | 335 | 160 |

Object **193** (W1A) has no condition: it sends message 16 in its **init block**, so that door
stands green from the first visit. The marker therefore means **open**, not *played out*. Object **297**
plays the reveal again if you come back from the level that unlocked the next door
(`1084 GetPrevLevel`, `1085 LevelIsDone(10)`): first message 16 with mode 0 (back to the cross), later
mode 1. The three area gates work with a **pair** of instances (green arrow 77/79/56, red cross
78/80/57, groups 123/124) from objects 287/354. KWS and SWS have the same layout.

> Gate: the init messages must be delivered to the app **after** `eko_init` (VM.md §2, `level_load`),
> otherwise 1082 still writes its variable during init and the end of init clears the wake list —
> then no door object ever sends message 16 and everything stays on the red cross.

## 5. The menu (state 0, `0x404e90`)

- Menu object `app+0x3c` (`0x445e30`): table of page objects at `menu + 4*page`, current page
  `menu+0x8c`; `0x446490(page)` switches, `0x446440(dt)` updates + returns a **result code**, `0x446430`
  reads the page. 2D over the 3D view.
- The world keeps rendering via `0x401ab0`. Table `0x405af8[page]` decides how: House (index 0) always
  keeps running (not paused); in an actual level flag 8 (pause) is set and, for most pages, a
  half-black quad (`0x80000000`) is laid over the picture. Pages 0x16 and 0x21 draw full black.
- Result codes: 1 New game, 2 start intro, 3 Load game, 5 OK/continue, 6 options, 7 quit, 8 yes, 9 no,
  10..13 slot 0..3, 14..17 Woody/Knothead/Splinter/BlackBox, 18 restart from checkpoint (pause),
  19..23 language (.eng .fra .ita .esp .all), 24 back.

| page | handler | role |
|---|---|---|
| 0 | `0x404fe4` | title screen. `app+0x98` counts down from 35 s; at 0 → `SetVar(app+0x8c, 1)` (start the House intro), at < -0.5 → page 0x1f with `+0x94 = 0` (attract). Key 9 → page 1 |
| 1 | `0x40519f` | main menu: 1 → page 0x1f with `+0x94 = 1`; 2 → `SetVar(app+0x8c, 1)`; 3 → check save file (`vt[3]` `0x450aa0`: 0 ok → page 0xb = read, 7 → page 7 "no save"); 6 → 0x1b; 7 → 0x1c |
| 0x1f | `0x40508c` | intro cinematic playing. Done when script var `app+0x8c` == 4 or key 6/9. New game: `0x44ffa0(save)` (clear save) + `RequestLevel(0 or 0.5, 1, 1, 0)`. Attract: stop script object `app+0x90` (`0x444380`, `0x4441d0`), var = 4, reset Perso, black, fade-in 0.5, page 0 |
| 0xb → 2 | `0x405276`, `0x4052ac` | read `Woody.sav` (`vt[5]`), slot choice: slot → `0x456df0(slot, app+0x48)` copies 0x14a4 B into the active save struct + volumes → page 3 |
| 3 | `0x4056c8` | world choice (3D objects = class 110, §7): 14/15/16/17 → `RequestLevel(0.4, 1/0xb/0x12/0x19, …)`; 24 → page 1 |
| 0x18 / 0x19 | `0x4057f5` | pause menu (opened by `0x404d80`; 0x19 if `perso+0x21c == 1`): 5 → continue (state 1 or 3), 6 → options 0x1b, 7 → 0x1c, 18 → `0x445930(game)` + `0x4560f0(perso)` + state 1 |
| 0x1c | `0x4057a5` | "quit?": yes → in a level: `RequestLevel(0.5, 0, 0, 0)` (to title); in House (or dev flag 4): `0x404cb0(0.5)` = fade + exit. no → page 1 or pause menu |
| 0x1d | `0x405796` | **game over** (opened by `0x404e10` from respawn `0x44a8f5` when lives == 0): OK → `0x44ffa0(save)` (wipe the whole active save) + to title |
| 0x1e | `0x4058cd` | **results** after a level (§5.1) |
| 6, 5, 0x17, 0xc, 8, 9 | `0x405358`, `0x4054ac`, `0x405586`, `0x405662` | "save?" → slot choice (occupied slot, `0x4501f0 != 0` → 0x17 confirm) → `0x456dc0` copy to slot → `vt[4]` writes `Woody.sav` → 8 succeeded / 9 failed → back to 6. "no"/done: `0x454050(perso)` (fade-out 0.5 s, `perso+0x724 = 5`; the Perso update `0x454192` then does the fade-in, `SetVar(var of 1140, 1)`) + state 1 |
| 0x20 | `0x40577b` | credits: 5 (confirm after 5 s) → to title; the page itself: CREDITS.md |
| 0x16, 0x21 | `0x404f90`, `0x405a49` | language/memory-card pages (console leftover, unreachable on PC) |

House script, object 115: var1 = 0 idle, 1 = start request from the engine, 2/3 = intro playing
(text via 1080, fades via 1151/1152), 4 = done. Message 1160 `(var1, inst 0x73)` reports the var and
script object to the App.

### 5.1 Results screen (page 0x1e), driven by `perso+0x724`
0/2/3: hide panel (`0x454580`). 1: show panel (`0x454560`); on OK: `n = 0x453cf0(app+0x74)` =
number of categories with `collected == total != 0` (0..2); `0x453fc0(perso, n != 0)` (cheer action
0x4e or 0x4c), `0x44c840(perso, n)` = **n unique items added** (`perso+0x260`, save block+4).
4: score = `0x453cb0(stats, isRace)` with `isRace = prev ∈ {0xd,0xf,0x11,0x14,0x16,0x18}` (the R
levels): race → only `stat[3]*100 (+50 % if stat[3]==stat[1])`; otherwise `max(0, 1800 - time_s)*10 +
stat[2]*100(+50 % if == stat[0]) + stat[3]*100(+50 % if == stat[1])`. If score >
`save.rec[prev].best` (`0x450230`): store the best and the 5 stats in the save (`0x450260`,
`0x450380..0x450440`). Then page 6 (save?).
`stats` = `app+0x74` = copy of `perso+0x710`: {total A, total B, collected A, collected B, float time}
(`0x453c80` init with `[0x4c5330]` and the Bonus-Woody total `[0x5e54e4]`; see BONUS.md).

**Which two categories?** `0x453c80` receives the two totals: `[0x4c5330]` (every enemy counts itself in
its PostLoad, ENEMY.md) and `[0x5e54e4]` (the Bonus-Woody total, `[0x5e54f4]` in a race level). Because
the score compares `stat[2]` with `stat[0]` (and `stat[3]` with `stat[1]`), `stat[2]` **must** be the
number of **defeated enemies** — the +50 % is for "cleared everything". `[0x4c532c]`, which EndLevel
copies over `stat[2]`, is updated every frame in App::Frame (`0x40177c`), presumably as "total − still
alive"; not decompiled (was §10.2).
`stat[4]` is the clock `perso+0x710+0x10` (`0x453ca0`, every frame `+= dt`).

### 5.2 The port of the results screen (`src/main_engine.c`, `src/hud.c`, `src/player.c`)

The whole chain now runs: **1083 EndLevel** saves the five statistics of the level being left
(`results_capture`, = `memcpy(app+0x74, perso+0x710, 20)`; they must survive the level switch, since the
screen runs in the hub), **1140** starts the sequence (`results_begin` = `0x453d90`) and `results_update`
(= `0x454090`) steps through the states:

| `perso+0x724` | port | what happens |
|---|---|---|
| 0 | `case 0` | Perso at the door vector with **scripted action 0x4a** (.ins anim 74, with its own camera track): he floats in and lies down under the parasol (screenshot of issue #7). The parasol, the lounge chair and the glass are **not** part of Woody but the prop of message 1142 (WWS: instance 366, model 67), which plays its own animation 0 at the same time (§5.3). As soon as the action ends: action **0x4b** and state 1 |
| 1 | `case 1` | page 0x1e shown (`0x454560`: iris closes, text slides in, the lines count up, RESULTS.md); 0x4b is restarted each time it finishes (`0x45410a`), so Woody keeps lying there. OK: `n` = complete categories, `n` unique items added, cheer with **0x4e** (n ≠ 0) or **0x4c**, state 2/3, text removed and iris opens (`0x454580`). The first OK while still counting only snaps all lines to their final value |
| 2 / 3 | `case 2/3` | done cheering → action **0x4d** (`0x454020`), store the score, state 4 and menu page **6** ("Do you want to save?") |
| 4 | `case 4` | 0x4d loops (`0x454164`); menu pages 6 → 5 (slot) → 0x17 (overwrite?) → 8 "Game Saved" / 9 "Save failed." run through the menu (`menu_update`, MENU_LOAD.md §5); once there is no more page (**No** on 6, or Continue on 8) `0x454050` = fade-out 0.5 s, state 5 |
| 5 | `default` | 0x4d loops (`0x454192`); after 0.5 s: prop removed from the world (`0x407850`), fade-in 0.5 s, camera hard-cut back to the follow camera (`0x41f9f0(2)`, `CamMgr+0x368 = 0`, `SetMode(0, 0)`), **`SetVar(perso+0x728, 1)`** — this is what the hub script is waiting for — ground snap (`0x462990`), look direction **P0 − P1** (`0x454244`: away from the door, into the hub), idle, `+0x550 = 0`, state 0 |

The scripted actions themselves run through `player_script_action`: the action number is the raw .ins
animation number, the logical records 26..30 (`0x1a..0x1e` in `log_anim`) are the one-shots for 74..78,
and everything that isn't a door (so also 10..16 and 19) now follows the **root motion** of `0x44e290`
via `ins_root_at(inst, action, phase, 1)`: the model is drawn at `(1−f)·pos + f·q(f)` and on the last
frame the Perso stands where the root motion brought him, looking along `−E.row2`, snapped to the
ground. That is the "flying in" motion of the parasol animation.

**Every action of the sequence gets the door vector passed in again** (`0x44dda0(act, perso+0x72c, 0)` in
`0x453d90`, `0x453fc0`, `0x454020` and the loops of `0x454090`): each time he is placed back on P0,
looking at P1. The five animations were made in one shared coordinate system (the root of 74 ends
exactly where 75 begins, 76/78 end where 77 begins); the offset to the end of the root motion that
`0x44db50` applies after each action is thereby undone again by the next action. The tail of
`0x44dda0` sets the camera, in the same frame, to the camera track of the new action (cut), taking over
from the follow camera that `0x44e5a0` requested at the end of the previous action. `0x453d90` and the
other starters immediately set **Perso state 9** (`0x44c980(9)`): the state-5 handler `0x44db50` then only
runs through the calls in `0x454090`. (Until issue #28, the port did not pass the vector in: 75 then
played offset and rotated, with the camera far behind him and no parasol in view.)

### 5.3 The prop of message 1142: parasol, lounge chair and glass

In WWS, script object 366 sends `1142 [0x100016e]` to itself (= itself, `0x4449c2`: `perso+0x748 = inst`).
Instance 366 is model 67: five mesh nodes and one animation of 1740 frames / 8.7 units, **exactly as
long as Woody's anim 74**. What `0x453d90` does with it (`0x453e0a..0x453f9b`):

1. position (`inst+0xc`) and `inst+0x60` = P0 of the door vector;
2. `F` = horizontal (P0 − P1), normalized if the length is greater than `[0x4a9004]` (otherwise stays
   (0,0,0)); `U = (0, 1, 0)` (`0x43ff80`), `R = F × U`, `T = U × R` (= F) (`0x41af10` is a cross product
   `this × arg2 → arg1`); the instance's 3×3 (`inst+0x28..+0x48`, rows = images of the model axes)
   becomes `R, T, U`: model-z points up (3ds Max) and model **−y points away from the door**, the same
   way Woody looks;
3. `0x4077f0(0)`: place it in the world cell at its own position (= draw; state 0 does this every
   frame again);
4. `0x436ca0(prop, 1.0, 0, −1, −1, −1)`: play animation 0 once from now, on the regular clock (×3.0). It
   stays on the last frame until state 5 removes it from the world with `0x407850`.

The port: `results_prop` in `src/main_engine.c` (quaternion from the three axes, `inst_play_once(prop,
0, 3.0, now)`).

**The screen itself** (page 0x1e) is documented in **RESULTS.md** and is ported as follows
(`hud_results_enter/show/hide/confirm/draw` in `src/hud.c`): black iris around the screen centre,
"RESULTS" top-left, "HIGH SCORE" with the old best score top-right, level name + "CLEARED!!" at the
bottom, on the left the counting-up lines for time / enemies / W / TOTAL / $, tick sound SoundFx 0x3d
while still counting and SoundFx 0x3f when the page opens.

**Deviations and assumptions**:
- Slot choice: pages 5 / 0x17 are ported (MENU_LOAD.md §5); the wait page 0xc (2 frames) is dropped.
  Page 8 + Continue leaves the menu (not back to 6, as previously stated here); 9 goes back to 6. The
  cursor on page 6 starts on the first selectable item ("Yes"); only for page 0x1c is it known that it
  starts on "No".
- End position of state 5: P0 of the door vector (the last 0x4d put him there), ground snap, looking
  towards P0 − P1.
- The game is **not** paused during the screen and there is no half-black overlay; the normal HUD stays
  hidden (table `0x405af8[0x1e] = 1`, RESULTS.md §6).

**Testing** (with the original data): `./out/woody.exe extract/Data WWS --prev W1A --stats 12 12 25 20 245`
= "we're coming from W1A, 12 of 12 enemies, 20 of 25 bonuses, 245 s" (order: total A, collected A, total B,
collected B, seconds); the hub script then sends 1140 itself. Enter/space = confirm, up/down arrows =
Yes/No. The screen is up around 7 s after start, `--shot out/x.ppm 9` gives it with all lines counted;
race: `KWS --prev K1R`.

## 6. Save

### 6.1 File `Woody.sav` (working directory), 0x52c4 bytes
`u32 version 0x11004` · 4 × slot (0x14a4 B, from 4) · `u32 musicvol[4]` (0x5294) · `u32 sfxvol[4]`
(0x52a4) · `float x[4]` (0x52b4). Read `0x450be0` (version ≠ → "Save file Woody.sav is obsolete..."),
write `0x450b30`, existence check `0x450aa0`. Slot manager `app+0x4c` (vtable `0x4aaf3c`, slots at
`mgr+8`). **Only written through the menu** (page 6 → 5 → 0xc), so after every level played through.

### 6.2 Slot / active save struct (`app+0x48` = `[0x5e5818]`, 0x14a4 B, reset `0x44ffa0`)
| offset | contents |
|---|---|
| +0x00 | checksum = sum of all bytes (signed) (`0x450030`) |
| +0x04 | 0x11004 · +0x08 = 0 |
| +0x0c + c·0x6dc | block for character c (0 Woody, 1 Knothead, 2 Splinter) |
| block+0x00 | lives (init 9) → `perso+0x250` |
| block+0x04 | unique items (type 36) → `perso+0x260` |
| block+0x08 | special charges (type 35) → `perso+0x254` |
| block+0x0c | float health (init 3.0; ≤ 0 → 1.0 on load, `0x44a759`) → `perso+0x24c` |
| block+0x10 + L·0x3c | record for level L (29 of them) |
| rec+0x00 | best score (`0x450230/0x450260`) |
| rec+0x04 | byte **done** (`0x4509e0` read, `0x450700` set) |
| rec+0x05 .. +0x24 | 32 bytes "unique item n of this level already picked up" (`0x450730/0x450760`) |
| rec+0x28..+0x38 | 5 stats of the best run (4 int + float time) |

(The port now has these three fields too: `SaveChar.best[29]`, `stats[29][4]` and `stat_time[29]` in
`woodyre.sav`, written by `results_store`. The file format has thus changed; the magic is `WSV2`,
older files are ignored and the game starts fresh.)
| +0x14a0 | u32 (added to the sum of all scores in `0x450a10`) |

The Perso writes lives/items/charges **directly** into this struct while you play (`0x44c7a0`,
`0x44c800`, `0x44c840`, see BONUS.md); `perso+0x258/+0x25c` (Bonus-Woody counters) start each level at 0
(`0x44a7d2`). This is the only player state that survives a level switch.

### 6.3 Messages
- **1082 `LevelIsEnable(level, var)`** (`0x445074` → `0x450470(save, character, level)`), character =
  `cfg+0x380` (of the *current* level). Table `0x450694`: level ≤ 2 or > 0x19 → always 1; otherwise the
  done flag of the predecessor **in the block of the current character**:
  W1B←W1A, W2A←W1B, W2B←W2A, W2D←W2B, W3A←W2D, W3B←W3A, W3C←W3B, W3D←W3C, KWS and K1A←W2D,
  K1R←K1A, K2A←K1R, K2R←K2A, K3A←K2R, K3R←K3A, SWS and S1A←W3D, S1R←S1A, … S3R←S3A, BlackBox←S3R.
- **1085 `LevelIsDone(level, var)`** (`0x4450b1` → `0x4509e0`): done flag of (current character, level).
- **1084 `GetPrevLevel(var)`**: `var = app+0x6c`. (MESSAGES.md calls this "instance-table value": wrong;
  also 1081 "transition/fade" and 1083 are too vague there, and 1160 writes into the App, not into the game object.)
- `0x4509b0(c)`: character 1 is unlocked if Woody-W2D (0,6) is done, character 2 if (0,10) is done.
  `0x450050(c)` = completion percentage (weights W: 6,7,8,9,12,13,14,15,16; K/S: 14,15,16,17,18,20),
  `0x4501f0` = average of the three (0 = empty slot).

## 7. Class 110 (ctor `0x451860`, vtable `0x4ab048`, handler `0x451940`)
Not a level portal but a **3D menu object of the world-choice screen** (menu page 3, class
`0x45e560`, global `[0x5e5adc]`). Base ctor `0x4890c0`. Message 58 `(inst, n)`: `inst+0x184 = n` and
`0x45e6f0(page3, inst)` registers the instance: n=0/1/2 → `page+0xec/0xf0/0xf4` (rank `inst+0x18c` =
1/2/3: the three characters), n=3 → `page+0xfc` (rank 4, plus `0x436ca0`), n=4 → first `page+0xf8` (rank 3)
then `page+0x100` (rank 4), n=5 → four spots `page+0x104..0x110` (rank 1..4). That matches House:
n = 0,1,2,3,4,4,5,5,5,5. `0x451890` places an object on a circle (angle from rank − position): the carousel.
All other messages → base `0x42d5e0`. There is no "player touches" behavior. Only needed for the port
if the menu must match 1:1.

The hub types 60/40/20/17/14 play no role in the level switch (that runs entirely through 1050/1042/1081
in the script).

## 8. Other game messages 1010..1050 (`0x444870`)
| id | args | meaning |
|---|---|---|
| 1010 | 9 ints | debug print (`0x462c60`, disabled logger) |
| 1020 | – | `perso->vt[0x98](1)`; if camera mode ≠ 5: `0x459030(game+8, cam+0x90)` |
| 1030 | inst | **SaveAuto(this)**: checkpoint (§4.5); inst 0 → warning + `0x44a920(0,0)` |
| 1040 | inst, action | Perso scripted action `0x44dda0(action, vector of inst, 0)` (17 = walk through door) |
| 1043 | inst, action, inst2 | same, with a target instance |
| 1041 | inst, x | `0x44e040(x, position of inst)` |
| 1042 | inst, dist, angle, var | var = 0; if the Perso is on the ground (`0x44bcf0` = `+0x22c`, `0x44527f`) and in state 0 (`+0x21c == 0`), within `dist` (XZ, not ×0.01) of inst and looking within `angle`° of the direction to inst → `0x458e40`, var = 1 |
| 1044 / 1045 | inst | `0x44e140` / `0x44e1a0` |
| 1048 / 1049 / 1050 | var, mode | var = 0; key 0 / 1 / 6 on `[0x5e6188]`; mode 0 = `0x467400`, 1 = `0x467420`, 2 = `0x467440` → var = 1 |
| 1081 | level | **GotoLevel** (fade 1.5 s) |
| 1083 | – | **EndLevel** (fade 0.5 s) |
| 1084 | var | **GetPrevLevel** |
| 1140 | inst, var | hub: player at door + results screen (§4.5) |
| 1141 | inst | House: menu pose |
| 1142 | inst | `perso+0x748 = inst` (object that `1140` moves along) |
| 1160 | var, inst | `app+0x8c = var`, `app+0x90 = script object` of the intro |
| 1172 | – | `app+0x70 = 1` |
| 1180 | x | to Credits |

## 9. Recipe for the port

```c
enum { ST_MENU, ST_GAME, ST_LOGO, ST_BLACKBOX };
static const char *kLevels[29] = { "House","WWS","W1A","W1B","W2A","W2B","W2D","W3A","W3B","W3C","W3D",
  "KWS","K1A","K1R","K2A","K2R","K3A","K3R","SWS","S1A","S1R","S2A","S2R","S3A","S3R",
  "BlackBox","Credits","BlackBox","Lang" };
static int char_of_level(int i){ return i<=10?0 : i<=17?1 : i<=24?2 : i==25?0 : -1 /*unchanged*/; }

struct App { int state, cur, prev /*init 0x1b*/; bool closing, have_next, quit; float fade_left;
             int next_level, next_state, next_page; int stats[5]; Save save; };

void request_level(App*a,float fade,int lvl,int st,int page){      // 0x404b60
    a->have_next=a->closing=true; a->fade_left=fade; fade_out(fade); music_fade(fade*0.9f);
    a->next_level=lvl; a->next_state=st; a->next_page=page; }

void end_level(App*a,float fade){                                   // 0x404be0 (message 1083)
    if(a->cur==1||a->cur==0xb||a->cur==0x12||a->cur==0x19){ request_level(a,fade,0,ST_MENU,0); return; }
    a->save.chr[cur_char].rec[a->cur].done=1;
    memcpy(a->stats,&perso->acc,20);
    request_level(a,fade, cur_char==0?1: cur_char==1?0xb:0x12, ST_GAME,0); }

void load_level(App*a,int idx){                                     // 0x4040c0 + 0x404140
    a->prev=a->cur; a->cur=idx;
    if(char_of_level(idx)>=0) cur_char=char_of_level(idx);
    load_everything(kLevels[idx]);  eko_init();  eko_tick_and_route_once();     // SetTypeInstance
    perso_init_from_save(&a->save.chr[cur_char]);   // lives, items, charges, health(<=0 → 1)
    a->state = idx==0x19 ? ST_BLACKBOX : ST_GAME;  frames_since_load=0;  fade_in(1.0f); }

void app_frame(App*a,float dt){                                     // 0x401590
    switch(a->state){ case ST_LOGO: logos(); break;        // 3 HNM's; done → load_level(0) + title
      case ST_MENU: menu_frame(a); break;                  // renders the level underneath
      default: game_frame(a); }
    draw_faders(dt); if(frames_since_load++<4) black();
    if(a->closing && (a->fade_left-=dt)<=0){
        unload_level(a); a->closing=false;
        if(a->have_next){ load_level(a,a->next_level); a->have_next=false; a->state=a->next_state;
            if(a->state==ST_MENU) menu_set_page(a->next_page /*0 = title + music*/); }
        if(a->quit) exit_app(); } }

// game messages
case 1081: request_level(app,1.5f,arg0,ST_GAME,0); break;
case 1083: end_level(app,0.5f); break;
case 1084: eko_set_var(arg0&0xffffff, app->prev); break;
case 1082: eko_set_var(arg1&0xffffff, level_is_enable(&app->save,cur_char,arg0)); break;   // §6.3
case 1085: eko_set_var(arg1&0xffffff, app->save.chr[cur_char].rec[arg0].done); break;
case 1180: request_level(app,0,0x1a,ST_MENU,0x20); break;
case 1030: perso->respawn_pos=inst(arg0)->pos; perso->respawn_dir=vector_of(inst(arg0)); break;
case 1140: eko_set_var(arg1&0xffffff,0); perso_place_at(vector_of(inst(arg0))); results_begin(arg1); break;
```
Minimal faithful flow without the menu: boot → (logos) → `load_level(0)`; "New game" = reset the save
(9 lives, health 3.0) and `request_level(0.5, 1, ST_GAME, 0)`; doors and exits then work automatically via
1081/1083/1084/1140. For 1140, a first version can get by with: place the player on the door vector,
update the score, skip the `perso+0x724` state machine and immediately do `eko_set_var(var, 1)` (that is
what the hub script gets back at the end of the results screen, `0x45422c`).
Game over (lives 0 on respawn): reset the save and `request_level(0.5, 0, ST_MENU, 0)`.
A command-line level can keep working as a "dev slot": index 0x1b, character unchanged.

## 10. Uncertain

1. The results state machine `perso+0x724` (update `0x454090`, table `0x4542c4`) has only been read in
   broad strokes (see §5.1/§5.2 for the chain the port derives from it). Not worked out: the exact end
   position of state 5 (`0x454244..`), the layout of the panel (`0x454963..0x455d97`) and what table
   `0x405af8` sets for pause/overlay flags on page 0x1e.
2. Given the score comparison, `stat[2]` is the number of defeated enemies (§5.1); where `[0x4c532c]`
   exactly comes from (`0x40177c`, every frame) is not decompiled. Which of the two totals is
   "Bonus Woody" is known for certain though: `stat[1]`.
3. 1082 reads the block of the *current* character. In KWS (character 1) the script asks
   `LevelIsEnable(13..17)`; K1A (12) is opened without 1082. `LevelIsEnable(11/12)` would look at
   W2D in block 1 and thus always give 0; character selection uses `0x4509b0` with an explicit
   block 0 for that instead. Not verified dynamically. The hubs also ask 1082/1085 for levels of the
   other characters (4, 7, 14, 16, 21, 23 / 2, 12, 19: presumably progress boards).
4. Messages 1020, 1041, 1044, 1045 and the action codes of `0x44dda0` (0x11 walk through door, 0x49
   menu pose, 0x4a..0x4e results) are only inferred from usage.
5. The BlackBox object (`0x484420`, 0xc0780 B, state 3, level 0x19, unlocked after S3R) and the credits
   trigger in `0x401d1b..0x401d42` have not been analyzed (TODO.md "Not analysed yet"; round 33 takes them on).
6. Dev flags `cfg+0x384` (2 = sound debug, 4 = level-picker dialog, 8 = debug keys incl. `SavePos.bin`, 0x10 = BlackBox object
   always on): `0x44fa54` clears 1..0x10 and only `0x44fe5f` sets 2 again (from the cfg), so 4/8/0x10 are never set in the shipped
   exe (PERSO_DEATH.md §3.4); `0x493e53` is a store to a different object (the 3D library's window/thread object), not this field.
7. Menu pages 4, 7..0x15, 0x1a, 0x1b (options, error messages; `0x4033f6` opens 0x1a from the
   frame input) have only been identified in general terms; the meaning of result codes 2 and 18 is
   inferred from the handlers, not from the page classes.
8. Everything is static; recommended to verify with `tools/wtrace.py` (breakpoints on `0x404b60` and
   `0x4040c0`) during New game → WWS → W1A → exit.
