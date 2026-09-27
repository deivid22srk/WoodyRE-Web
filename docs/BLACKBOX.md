# BLACKBOX.md — the BlackBox mini game (App state 3, level 0x19 `\Data\BlackBox`)

Static analysis of `game/Woody.exe` (image base 0x400000, `out/disasm_full.txt`, `tools/funcinfo.py`), floats and tables read from
the exe. Checked against the running original on 2026-09-27 with `tools/wverify.py --probe blackbox` (§11): walking, the jump over
the crate, Buzz's bullets, the lives, the death and respawn, the ladder, the jump off it, the potion, the cage, the 5 s end and
round 2 behave and time as described. "Uncertain" = not checked that way. Port: `src/blackbox.c` (§12).
Related: GAMEFLOW.md §2/§4 (App state 3, the load, the credits request), MENU_LOAD.md §4 (the carousel figure), HUD_TEXT.md §2/§5
(Font_Draw, RectVirtual), SOUND.md §5 (the SoundFx ids), RESULTS.md (level 25 = "Mini Game").

## 0. Summary

- The BlackBox is a **2D arcade platformer in the style of Donkey Kong**, not a 3D level. One fixed 640×480 screen (a jungle
  picture with log floors, two ladders, a crate, floating stones) drawn with the 2D quad `RectVirtual 0x480a10` over the 3D level
  `Blackbox.gel`, which is loaded, frozen (App flag 8) and covered. The sprites are pixel art in 58 sheets of `Blackbox.rck`.
- **Goal**: Woody starts at the bottom left and has to reach the **potion** at the top right (545, 107). Touching it: Woody holds it
  up (state 11, 0.8 s), then the **cage** of Knothead and Splinter on the upper floor (93, 233) blows up (five explosions); the two
  walk off to the right, the round time is shown for 5 s ("Level n  t Seconds"), and the next round starts.
- **Three rounds**: speed factor 1.0 / 1.4 / 1.7 on everything that moves, 3 / 2 / 1 lives. Losing all lives only sends Woody back to
  the start with the round's lives again (no game over); the score is the time. After round 3 also "Final Score  (t1+t2+t3)
  Seconds", and 5 s later `0x4846d0` returns 1: `0x401d35` requests the credits (`RequestLevel(0.5, 0x1a, 0, 0x20)`).
- **Buzz Buzzard** attacks according to where Woody is (§6): on the ground floor he walks in from the right and shoots bullets along
  the floor (crouch to let them pass; the crate stops them), on the middle floor he throws rolling stones from the left (jump over them),
  on the upper floor / log platforms he hops along above Woody dropping dynamite, on the floating stones he swoops down across him.
- **`\Game\mask.bin`** is the collision map: 4 blocks of 256×256 RGB (3 bytes per pixel, only the first read) covering the play area
  65..576 × −15..496: byte 0 = solid, 0x7f = ladder, anything else = free (§3).
- Controls: actions 0/1 left/right, 2 up (climb), 3 down or 5 duck (crouch / climb down), 4 jump. No attack (action 6 only stops the
  "stand still" reset). Sounds: Common bank of Woody through the SoundFx table (§9). No music (the level script stops it: 1655).

## 1. App integration

| where | what |
|---|---|
| `0x4042b6..0x404313` (load `0x404140`) | `if (cfg+0x384 & 0x10 \|\| index == 0x19)`: `app+0xe4 = new(0xc0780) 0x484420`, `SetState(3)`; else `app+0xe4 = 0`, `SetState(1)`. Dev flag 0x10 is never set in the shipped exe (GAMEFLOW.md open point 6) |
| `0x401615` switch, state 3 (`0x40165c`) | HUD hidden (`0x448450(2)`), **`app+0xf4 \|= 8`** (the world is paused: no Perso, no VM tick, no instances) and then the normal game frame `0x401ab0` |
| `0x401d1b..0x401d42` in `0x401ab0` | `if (app+0xe4) { if (0x4846d0(app->state == 0)) RequestLevel(0.5, 0x1a, 0, 0x20); }` — the argument is 1 while a menu page is open (state 0, the pause menu): dt 0, everything frozen but drawn |
| `0x401ea9..0x401ec2` | the SoundFx queue `0x468ba0` also runs while paused when `app+0xe4` is set |
| `0x40580c` pause "Continue" | `SetState(app+0xe4 ? 3 : 1)`; the ride page 0x19 is never used here (`0x404d80`) |
| `0x4049a0` unload | the object is deleted with the level |
| level script | one object: `1655 [3]` (music off) and `1200 [inst0, 1]` (GAMEFLOW.md §1) |

The 2D quads land in the 2D list, drawn after the (frozen) 3D picture, in call order; the menu and the fades come later (HUD_TEXT.md §5.4).

## 2. The object `0x484420` (0xc0780 B) and its frame `0x4846d0`

| offset | what | ctor |
|---|---|---|
| +0x00 | 8 image refs `0x01010000..7` (background) | `0x488790` |
| +0x20 | `mask.bin`, 0xc0000 B; block pointers +0xc0020/24/28/2c (+0x20, +0x30020, +0x60020, +0x90020), +0xc0030 = last block used | `0x4887f6` |
| +0xc0034 | Woody, 0x88 B (type 0) | `0x485a50(0)` |
| +0xc00bc | Buzz, 0x88 B (type 1) | `0x486d60(1)` |
| +0xc0144 | potion (type 12), box {1,16,22,1} | `0x488180(12)` |
| +0xc01a8 | cage (type 5) | `0x488180(5)` |
| +0xc020c / +0xc0278 | Splinter (type 3) / Knothead (type 2), 0x6c B: +0x64 speed, +0x68 dt | `0x487c90` |
| +0xc02e4 | pool of 10 sprites × 0x68 (+0x64 = in use), +0x410 speed, +0x414 dt | `0x485330` |
| +0xc06fc | lives icon (type 4) | `0x488180(4)` |
| +0xc0760 | byte: cage blown | |
| +0xc0764 | round 1..3 (`0x484e20/30`) | 1 |
| +0xc0768 | dt of this frame (0 while a menu is open, `0x484f80`) | |
| +0xc076c | round timer; +0xc0770..78 the three round times; +0xc077c the end timer | 0 |

`0x484530` (start of every round): Woody (85, 430), Buzz (600, 435), potion (545, 107), cage (93, 233), Splinter (84, 261) state 0,
Knothead (104, 261) state 0, icon (125, 62) frame = Woody's type 0 → 12 (type 2 → 6, 3 → 0: the head of the character playing, always
Woody), then `0x484f20` = the speed factor of the round (`0x484ef0`: 1.0 / 1.4 / 1.7, `0x4a900c/0x4abda4/0x4ab13c`) into all movers.
Woody's state and Buzz's attack are **not** reset between rounds.

Frame `0x4846d0(menu)`, in this order (drawing and logic interleaved):
```
dt = menu ? 0 : clock dt                                     // 0x484f80 -> all +dt fields
background 0x488880; Splinter 0x487ce0; Knothead 0x487ce0; icon draw
Woody 0x485ac0(cage_open)                                    // draw, animation, state velocity, input (§5)
if Woody.state not in {7,8,5,11,12}:                         // gravity
    oldy = y; d = fall_step(Woody, round speed, dt)          // 0x4884b0
    hit = <the argument byte>; if (d != 0) move(Woody, (0, d), &hit, Woody+0x68)   // 0x488df0 -> 0x488b80
    Woody_land(hit, oldy)                                    // 0x486860
move(Woody, Woody.vel, &hit, 0); if (hit) Woody_bump()       // 0x488b80, 0x486aa0
if Woody.state == 7: reset the fall integrator; Woody+0x68 = 0 (0x486850)
Buzz.target = Woody.pos (0x486f10); Buzz_ai 0x487500; Buzz frame 0x486dd0
if Woody.state not 9/10 && swoop_hits(crouched) && invul == 0: hurt      // 0x487ba0
Buzz_fire(pool) 0x487800; pool 0x485370(Woody.pos)
if Woody.state not 9/10 && pool_hits(crouched) && invul == 0: hurt       // 0x485720
if Woody.state != 11 { draw potion; timer += dt } else if potion.frame == 0: potion.frame = 6
if potion.frame != 0 && Woody.state != 11:
    if !cage_open { draw cage; blow_cage 0x484c30; cage_open = 1; HUD; return 0 }
    if pool empty:                                           // the explosions (and every stone / shot) are gone
        if time[round] == 0: time[round] = timer;  timer = 0
        if both kids idle and left of 320: both state 1       // they walk off to the right
        end text 0x485130; end_t += dt
        if end_t > 5 { end_t = 0; if round == 3 return 1; next round 0x484e40 }
else { draw cage; if Woody.state != 11: potion touch 0x484b20 }
HUD 0x484ff0; return 0
```
"hurt" = `0x4866d0(9)`, lives −1, invulnerability `+0x74 = 1.5` s (`0x485d80`). The argument byte doubles as the gravity hit flag when
the step is 0 (only with dt 0): a pause in mid-air makes a short fall "land".

## 3. Sprites, drawing, the mask

**Sprite class** (0x64 B, ctor `0x488180(type)`, pool slots `0x488150`): +0 first animation record of the type (table `0x4b7ea8`,
types 0..12 → 0, 13, 23, 36, 49, 50..57), +8 type, +0xc state (= animation: record = first + state, `0x4882a0` → `0x488220`),
+0x10 position (x, y, z; z = ladder centre, §3.2), +0x1c displacement of this frame, +0x24 animation length, +0x28 animation time,
+0x2c scale 1, +0x30 frame (dword offset, 6 per frame), +0x34 frame count, +0x38 frame list, +0x3c box {left, top, right, bottom}
around the position, +0x4c image ref, +0x50 base y, +0x54 facing (±1), +0x58 ping-pong direction, +0x5c/+0x60 fall integrator.

**Animation table** `0x4b7f10`: 58 records × 101 dwords = frames {u, v, w, h, hot x, hot y} up to −1, dword 100 = image ref
`0x0101xxxx` (level bank images 8..65, one sheet per record). Frame = `ftol(n · t / length) · 6 mod 6n`; states that loop back and
forth flip +0x58 at the end and draw frame `6n − frame − 6` while it is −1. Types: 0 Woody (13 anims), 1 Buzz (10), 2 Knothead (13),
3 Splinter (13), 4 the three heads, 5 cage, 6 dynamite, 7 flames (unused), 8 explosion, 9 hit star, 10 stone, 11 bullet, 12 potion.

**Draw** `0x4884e0` → clip `0x488610`: box top = Y − hy, left = X − hx (facing −1: right = X + hx, mirrored), X/Y = `ftol` of the
position. Nothing is drawn outside 64..576 × 40..460; a partly outside sprite is cut (clip amounts in texture space, left/right swapped
when mirrored). `RectVirtual(X − dir·(hx − cl), Y − hy + ct, dir·(w − cr − cl), h − cb − ct, u + cl, v + ct, w − cr − cl, h − cb − ct,
0xfe808080 ×4, image, 8)`: a negative width mirrors.

**Background** `0x488880`: images 0..3 (256², source 255²) at (0,−16), (256,−16), (0,240), (256,240); images 4..7 (128², source 127²)
at x 512, y −16/112/240/368. Images 66..70 (language flags) and 71 (logo) of the bank are not used here.

### 3.1 `mask.bin` (`0x488e50`)
`x ≤ 320`: block 0 (y ≤ 240, local (x−65, y+15)) or 2 (local (x−65, y−241)); else block 1 / 3 with x−321. Byte `[(ly·256 + lx)·3]`:
0 → 1 solid, 0x7f → 2 ladder, else 0. Layout (screen y): ground 436, crate 127..156 × 411..435, middle floor 354 (a rock 220..516
to 401, a step 497..516 from 329), upper floor 263..272 (whole width, the left ladder passes through), log platforms 204..213
(65..176 incl. the cage roof, 214..264, 307..364, 406..455, 486..505), cage wall 112..123 × 214..262, ledges 66..97 × 174 and
551..576 × 233, floating stones at 132..142 (136..168, 225..256, 319..350, 417..448), the potion ledge 526..576 × 109..142.
Ladders: left 136..158 × 214..353, right 517..539 × 273..435 (through the middle floor).

### 3.2 The move `0x488b80(pos, vel, box, &hit, flag)`
New position n = pos + vel. Out of 65..576 × 40..440 (box included) → hit. Else the **top row** and the **bottom row** of the box at
n (left..right−1): a solid pixel sets hit (top or bottom blocked); the first ladder pixel of the top row gives the ladder centre
(`0x488f00` at (ftol nx, top): the middle of that ladder, or of the nearest one along the row, `0x488fa0`). Only if both rows are clear,
the **left and right columns** (top..bottom−1): solid = side blocked. Not hit → pos = n. Hit → only y moves when (side blocked, vy ≠ 0,
flag 0) or (bottom blocked, vy < 0) or (top blocked, vy > 0), and hit is cleared. **pos.z = ladder centre or 0** in every case.
`0x488df0(pos, box, d, &hit, flag)`: d = 0 → nothing (hit untouched), else the move with (0, d).

Fall integrator `0x4884b0(speed, dt)`: `t = speed·dt; v += 98t; d += v·t; return Δd`, reset `0x4884a0`: v = 25, d = 0.

## 4. Woody (`0x485a50`, 0x88 B)

Extra fields: +0x64 climb direction (1 up, −1 down), +0x68 "long fall began this frame", +0x6c lives, +0x70 lives of the round
(`0x485d40`: round 1 → 3, 2 → 2, 3 → 1), +0x74 invulnerability, +0x78 blink toggle, +0x7c speed, +0x80 dt, +0x84 footstep source.
Box `0x485db0`: left 8, top 26, right 11, bottom = h − hy of the frame (Knothead 9/20/8, Splinter 10/19/8 — the class could play them).

| state | anim (image) | length `0x4866d0` | sound | meaning |
|---|---|---|---|---|
| 0 | idle (8) | 1.2, back and forth | | |
| 1 | walk (9) | 0.6 loop | footstep chain 25..28 | 80 px/s |
| 2 | turn (10) | 0.2 | | |
| 3 | crouched (11) | 1.2, back and forth | | bullets pass |
| 4 | ducking (12) | 0.15 → 3 | 32 | last frame counts as crouched (`0x486cf0`) |
| 5 | jump (13) | 0.55 → 6 | 21..24 random | frame by time (`0x486bb0`: >0.45 3, >0.35 2, >0.05 1) |
| 6 | fall (14) | 15.0 | landing 29..31 | frame `0x486c10`: 0 short, 1 longer than a jump, 2..4 hard landing (t 14.7..15) |
| 7 | climb (15) | 1.0 loop | | 50 px/s |
| 8 | on the ladder (16) | 1.2, back and forth | | |
| 9 | hit (17) | 0.4 | 34..36 random | → 10 if no lives, else 6 |
| 10 | dying (18) | 1.5 | 33 | → 0 at (85, 430), facing right, lives of the round |
| 11 | holds the potion (19) | 0.8 → 0 | | the timer stops |
| 12 | (20, unused) | 1.0 | | |

**Frame `0x485ac0(frozen)`**: vel = 0; draw (while invulnerable only every other frame); t += speed·dt; frame; box; end of animation
`0x486370` (4 → 3; 2, 6, 11, 12 → 0; 5 → 6 with base = y; 1, 7 loop; 0, 3, 8 flip; 9, 10 as above); state velocity `0x486560`
(5: y = base, vy = −hop(t) once t > 0.05; 1: vx = dir·80·k; 7: vy = climb·(−50)·k; k = speed·dt); **input `0x485e70`** unless state
9/10/11, frozen (cage blown) or dt 0; state 6 with t ≥ 14.7: vel = 0; invulnerability −= k.

**Jump**: a height over the start, `hop(t) = 7 + 140a(1 − a)`, a = t − 0.05 (`0x486b80`) — 7 px at once, 42 px at 0.55 s; then
falling (state 6, fall integrator). A jump that hits anything (`0x486aa0`) falls from the previous frame's height. Air control in
5/6: vx = dir·75·k (`0x486ca0`).

**Landing `0x486860(hit, oldy)`**: not hit: states other than 5..8 get base = oldy, and (not 9/10) state 6. Hit in state 6 with a base:
fell ≤ 42 → state 0; else a hard landing: y += 10 (the long-fall frame reaches 11 px below the hotspot), t = 14.7, next frame
(the squash, 0.3 s); if the long frame only began this frame, instead t = 0, frame 0, base += 10. Landing sounds 29..31.

**Input `0x485e70`** (actions held, `0x467400`): left/right (right wins): facing the other way → turn (state 2 unless 4..8); else
0 → walk; 3/4 without down → walk; 5/6 → air control; 8 → jump off the ladder (base = y, state 5); 7 without up/down → 8.
Nothing held: states other than 0/5/6 (and not with attack held) stop: 7 → 8, 8 → vel 0, else state 0.
Then jump (not in 5/6, from any other state, ladder included); else up: needs a ladder centre (z ≠ 0), lifts 2 px from 0/1, snaps
x to the ladder centre, state 7 climbing up; else down/duck: with a ladder the same climbing down, without one 0/1 → duck (4).
The top of a ladder is under a floor (the head row blocks): leave it sideways (release up, press left/right = a jump off).

## 5. Knothead and Splinter (`0x487c90` / `0x487ce0`)
Idle in the cage (state 0, back and forth 1.2 s). Once the cage is gone and the pool is empty, both walk right (state 1, 80 px/s);
at the end of 1/11/12 `0x4880b0` picks at random: walk again, jump (5: up 80 px/s for 0.55 s, then 6: down until below the start),
12 or 11 (tumbles, also moving 80 px/s). Splinter stops (state 0) past x 490, Knothead past 515. No collision with anything.
Speeds `0x488010`: 0 1.2, 1 0.6, 5 0.55, 6 15, 11 0.8, 12 1.0.

## 6. Buzz (`0x486d60`, `0x487500`, `0x4870b0`)

+0x64 attack (−1 none, 0 leaving, 1..4), +0x68 Woody's position, +0x70 armed, +0x74 swoop start x, +0x78 swoop direction,
+0x79 shallow swoop, +0x7a swoop due, +0x7c speed, +0x80 dt, box {16, 28, 12, h−hy}. Not drawn in attack −1 or state 6.
Lengths `0x487460`: 0/2 1.0, 1/3/9 0.8, 4 0.6 (+armed), 5 1.2, 6 1.4, 7 1.6, 8 1.8.

| Woody at | attack | Buzz |
|---|---|---|
| 130 < x < 495, y > 355 | 1 | from (598, 435) walks left (2, 80 px/s) to 555, then shoots (3 ↔ 9): on frame 1 a **bullet** from (x−6, y−20), 120 px/s left, box {4,3,2,3}, sound 18 |
| y < 390, x > 175, y > 265 | 2 | from (42, 353) walks right to 85, then throws (4 ↔ 8): on frame 0 a **stone** at (x+11, y−1), box {10,22,12,0}; after 0.2 s it jumps to x 107 and rolls right at 60 px/s |
| 150 < y < 265, 120 < x < 520, not mid-swoop | 3 | (at (598, 94) unless he was leaving) hops (5, 1.2 s loop) after Woody with the fall integrator as a speed, turning 50 px past him; on each frame 0 a **dynamite** at his feet that falls (integrator) through everything and explodes (box {14,10,12,12}, sound 6) once below Woody's y − 10 or 265 |
| y < 150, 140 < x < 460, only after he left | 4 | hidden (6, 1.4 s) above Woody, then a **swoop** (7, 1.6 s): from x ± 80 of Woody to the other side at 100 px/s, y = 20 + t(200 − 125t) (shallow, to 100) or 20 + t(275 − 171.875t) (deep, to 130), alternating side and depth (`0x487ae0`, `0x487b20`) |
| elsewhere | 0 | walks / hops off to the nearer side, gone past 45 / 595 |

Swoop hit `0x487ba0`: Buzz box (x−10, y−2, x+15, y+15) against Woody's; crouching only helps against the shallow one.

## 7. The pool (`0x485330`, 10 slots)
Alloc `0x485450` (first free slot, **no bound check**), free `0x485480`, empty `0x485490`. Per frame `0x485370`: move each against
the mask (`0x4854b0`; a bullet shows its second frame on the first hit and goes on the next; a stone becomes a star (9, 0.2 s, +10/−10,
sound 37); dynamite moves anyway), draw, animate (not bullets), ends `0x485990` (stones/dynamite loop, explosions/stars go), next
velocities `0x485870`. Hits on Woody `0x485720`: the **first** overlapping object decides — a bullet while crouched, unexploded dynamite
or an explosion past frame 2 → no hit. Cage blast `0x484c30`: explosions at (94,233) 0.8 s, (110,240) 1.4, (80,210) 1.6, (110,225) 1.0,
(80,255) 1.8, sound 6 each.

## 8. Texts
Font `0x01030000` (level bank), virtual 640×480, colour black. HUD `0x484ff0` (size 15, 0xff000000): Common string 127 "Level" at
(64, 40), 3 px later the round number, the icon at (pen + 12, 62), the lives at pen + 34. End `0x485130` (size 30, 0xfe000000):
"Level" at (200, 275), +10 the round, +30 `ftol(time)`, +10 string 128 "Seconds"; in round 3 below it (155, 305) string 129 "Final Score",
+30 `ftol(t1+t2+t3)`, +10 "Seconds".

## 9. Sounds (SoundFx `0x468a00(id, 0)`, 2D; SOUND.md §5 — the "menu" callers listed there at `0x484ca1..0x4867cb` are these)
6 explosion, 18 shot, 21..24 jump, 25→28 footsteps (source Woody+0x84, `0x468e50`; `0x468b40(25, 4)` shuffles the chain every walking
frame), 29..31 landing, 32 duck, 33 death, 34..36 hit, 37 stone breaks. The swoop marks Buzz's source +0x84 but never starts a sound.

## 10. Open points
1. Texture filter of RectVirtual (HUD_TEXT.md §5.3): unknown; the port filters linearly.
2. `0x485450` writes past the pool when all 10 slots are busy (into the icon); the port drops the spawn.
3. The chain shuffle of `0x468b40` is emulated as a random footstep every 0.3 s.
4. A GDI screen grab of the running original only shows the last logo film (DirectDraw flips), so there is no picture of the
   original to compare with; the positions and states were compared numerically.

## 11. Live check (`tools/wverify.py --probe blackbox`)
```
python tools/wverify.py game --level BlackBox --probe blackbox --at 3 --from 2.5 --until 22 --every 0.25 --keys "5:RIGHT:1.2 7:JUMP:0.1 7:RIGHT:1.5" --seconds 130 --out out/trace/bb_live1.txt
python tools/wverify.py game --level BlackBox --probe blackbox --at 3 --from 3.2 --until 24 --every 0.25 --bbpos "3.5 528 435 11 535 100" --keys "4.5:UP:3.2 7.8:LEFT:0.6" --seconds 125 --out out/trace/bb_live2.txt
```
The probe patches `0x4042c9` (je → jmp, so loading slot 0 creates the object), sets App state 3 at `--at` and logs the object per call of
`0x4846d0`. Results: Woody stops at the crate at x 115.9, jumps to y 394, Buzz enters (attack 1) when Woody passes x 130, bullets take a
life per hit, the third (lives 0) → state 9 → 10 → respawn at (85, 430) with 3 lives and Buzz leaves; the right ladder is climbed at 50 px/s to
y 299 (head under the upper floor), left jumps off onto the rock (483, 353); the potion (state 11 at (545, 107)), the cage (pool 8 while
stones are still rolling), end timer 5 s, round 2 with 2 lives. The port gives the same values (x 115.5, y 299.3, (484, 353), ...).

## 12. Recipe for the port (done: `src/blackbox.c`)
1. Load images 0..65 of `<LVL>.rck` (bottom row first, 24 bit opaque) and `mask.bin`; embed the table `0x4b7f10` (58 records) and `0x4b7ea8`.
2. Level 25: create the object after the load, pause the world (App flag 8) but not the sound, and call the frame inside the 2D layer
   with dt (0 while a menu page is open); on 1 request level 26 with a 0.5 s fade.
3. Port every function as above in the same order, float for float (`ftol` = truncation); draw with the clip of §3.
