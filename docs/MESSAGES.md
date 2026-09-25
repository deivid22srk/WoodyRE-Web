# Messages from script to engine (SEND opcode)

Every `SEND n` pops n values from the stack: the first is the message id, the rest are arguments.
`arg0` is almost always an instance reference `0x01000000 | i`; the engine uses the low 24 bits as an
index into the instance table `[0x50944c]->0x6c`. Numbers representing a float are stored x100
(the engine multiplies by 0.01, constant `0x4aa0ac`). Time is in 1/100 s.

Usage = number of times sent during level init across all 28 levels (emulator).
Status: check = behavior read from the code, tilde = hypothesis based on arguments/strings, question mark = not yet examined.

## Routing
| id | handler | this |
|---|---|---|
| 1..999 | `instance->vtable[22]` (per class; base `0x42d5e0`) | the instance in arg0 |
| 1000..1180 | `0x444870` | game object `[0x5d7afc]` |
| 1200..1202 | `0x403440` | world `[0x4c4c0c]` |
| 1500..1511 | `0x46cca0` | subsystem `[0x5e823c]` |
| 1600..1657 | `0x467fa0` | sound manager `[0x4c2dd8]` (Cryo Sound Library) |

The game handler often ends in `0x4455be` = `eko_set_var(var, value)`: the answer to the script
thus comes back through a script variable (watchers get woken).

## Instance messages (base class `0x42d5e0`, fields of the C++ instance in parentheses)
| id | args | usage | meaning |
|---|---|---|---|
| 1 | inst, anim, speed | 5 | check PlayAnim: slot0 = anim, slots1-3 = -1, speed = animlen·k·speed, `0x42e290(0)` (reset blend) |
| 2 | inst, anim, flag, dur, x | 0 | check PlayAnim with duration (divides animlen by dur) |
| 3 | inst, anim, flag, n | 39 | check SetAnim variant (n ≤ 10 → status +0x88 reset); if flag=0 → speed 0 |
| 4 | inst, anim, flag, dur | 934 | check PlayAnim on all 4 slots, start time = now; flag=0 → blend back (`-[+0xa0]`) |
| 5 | inst | 9 | check ResetAnim (`0x42e290(0)`) |
| 6 | inst, on | 285 | check on≠0: if `[+0x1c] < 0` → `0x407790` (activate/show); on=0 → `0x407850` (deactivate/hide) |
| 7 | inst | 0 | check cancel pending 12/13 for this instance (`0x4012f0`) |
| 10 | inst | 0 | tilde class 30-38 (volumes/triggers): own handling `0x44f362` |
| 11 | inst, a, b | 1004 | ? enemy classes 4-13 (`0x41a78b`, "follow / random path"): set patrol or follow path |
| 12 / 13 | as 3 / 4 | 0 | check deferred 3 / 4: only executed once `[+0x9c] != 0`, otherwise retried |
| 14 | inst, b0,b1,b2,f,d | 0 | check for each sub-part (16-byte records at `[+0x74]`, count `[+0xf8]->0x28`) set color/value; 0xffff = unchanged |
| 15/17 | inst, a, mode, f, t2 | 0 | check **UV scroll override** (`0x42d9c3` / `0x42daae`, INSTANCE.md §2): byte `+0xda = a` (never read), mode bits 3-5 of `+0xd8` (15: 1/2, 17: 4/5 for mode 1/0; other modes leave the bits), speed factor `f·0.01 → +0xe8`, 15 only: duration `t2·0.01 → +0xec`, start `+0xe4` = now. **Sent by no level script** (all 28 checked); ported |
| 16/18 | inst, a, mode, f | 23/54 | check **texture frame override** (`0x42da32` / `0x42db0f`): byte `+0xd9 = a` (never read), mode bits 0-2 (16: 1/2/3, 18: 4/5/6 for mode 1/0/2; other modes leave the bits), factor `f·0.01 → +0xe0`, start `+0xdc` = now; ported |
| 19 | inst | 0 | check both overrides off (`+0xd8 &= 0xc0`, `0x42db86`); ported |
| 26 / 30 | perso | 0 | check Perso class only: 26 `[_, inst, mode]` = teleport (1 = position, 2 = + direction of the marker), 30 `[_, cs]` = LockMove; see PERSO_DEATH.md §1 |
| 29 | inst | 0 | check class 20: Reset `0x452ae0` (ROCKET.md §4.4); tilde classes 21, 40/120/121 |
| 33 | inst, a, b | 50 | ? |
| 34 | inst, other | 340 | check link instance to `other` (pair in table `[0x50944c]->0x50`, counter +0x4c) – "attach/link" |
| 40 / 55 | inst | 0 | check class 20 (ROCKET.md §2.1): 40 = mount, 55 `(1, v)` flight time v·0.01 s, `(2, v)` max speed; class 21 not ported |
| 42 | inst, a, f | 5 | check on path follower `[+0x78]`: `0x437d10(a≠1, f·0.01)`, then copy position from the path and `0x4077f0` (reposition) |
| 43 | inst, a, f, c | 129 | check like 42 with extra flag `c==1` (`0x437d50`) |
| 44 | inst | 0 | check path follower `0x437d90()` (stop/reset) |
| 45 | inst, bits | 1296 | check SetFlags: `+0xf0 \|= bits & (1\|2\|0x20)` |
| 46 | inst, a, b | 7 | check path follower `0x4381e0(a==1, b==1)` |
| 50 / 51 | inst, v | 279 / 44 | tilde classes 50-52: `0x45108a` / `0x451059` |
| 52 | inst, v | 158 | ? |
| 53 | inst, v | 3 | ? |
| 54 | inst, mode, v | 98 | check class 80 (lightning rod, STORM.md §2): mode 1 → float +0x108 = v (radius of the shelter sphere, raw); mode 2 → float +0x10c = v (rod height, raw); then the fade handler `0x44e8f0` |
| 55 | inst, a, b | 34 | check rocket/cannon parameters class 20/21 (ROCKET.md §2.1) |
| 56 | inst, v | 606 | check base: float +0x6c = v·0.01; in `0x44e8f0` (most classes) first `0x44e91b` |
| 57 | inst, v | 643 | tilde `0x44e907` (class-common) |
| 58..63 | inst, … | few | class 14 (boss Buzz, `0x410070`): **59** `[boss, inst]` links **one** instance (`+0x234`, flag 0x20, own AnimCtrl), **60** `[boss, var]` = mailbox variable (BOSS14.md §7); class 15 (`0x40e781`): 8 instances at +0x1c8..+0x1e4, flag 0x40; 63 in class 17 |
| 650, 800 | inst | 11 / 2 | ? via vtable[22] of the class |

## World (`0x403440`)
| id | args | usage | meaning |
|---|---|---|---|
| 1200 | obj, type | 4907 | check **SetTypeInstance**: `new` C++ class for `type` (see classmap_raw.txt), links to the script object |
| 1201 / 1202 | obj | 0 | check set / clear flag 0x400 on the instance (via vtable[4]) |

## Game (`0x444870`)
| id | args | usage | meaning |
|---|---|---|---|
| 1000 | inst, target\|−1 | 0 | check **launcher** (type 42, typeword category 6; PROJECTILES.md §4): `0x4522b0(1, 1.0, target)` = one shot on the next think step |
| 1001 | inst, kind | 94 | check launcher `0x452330(kind)`: reset (`vtbl[17]`) + projectile template `0x5d7ba8 + kind·0x68` into `this+0x108`; kind 0 = bomb thrower, 1..3 = projectile (ctor: 1) |
| 1002 | inst, n, v | 513 | check launcher `0x452360(n, v)`: projectile parameter n = 0..19 (0 speed, 1 gravity, 2 lifetime ×0.01 s, 3 max bounces, 4 damage, 5 aim height, 7/8 shoot animation + duration, 9 radius, 10/11 drag, 12..17 homing, 18 visual, 19 aim at target); table in PROJECTILES.md §3 |
| 1003 | inst, target\|−1, count\|−1, t | 177 | check launcher `0x4522b0(count, t·0.01, target)`: start a series, interval t·0.01 s (min. 0.2), count −1 = endless, first shot immediate |
| 1004 | inst | 7 | check launcher `0x452320()`: stop series (`+0x198 = 0`) |
| 1010..1050 | … | 0 | check see GAMEFLOW.md §8 (1030 SaveAuto, 1040/1043 scripted action, 1042 proximity + facing, 1048..1050 key tests) |
| 1080 | rec | 0 | check `0x456ed0(record)` on `game+0x18` |
| 1081 | level | 0 | check **GotoLevel**: `0x404b60(1.5, level, 1, 0)` = RequestLevel with 1.5 s fade (hub doors; see GAMEFLOW.md §4) |
| 1082 | level, var | 24 | check **LevelIsEnable**: var = `0x450470(saved, level)`; warns without a save struct |
| 1083 | – | 0 | check **EndLevel** `0x404be0(0.5)`: mark level as done, back to the character's hub (GAMEFLOW.md §4) |
| 1084 | var | 3 | check **GetPrevLevel**: var = `app+0x6c` (previous level index; the hub script uses it to pick the spawn door) |
| 1085 | level, var | 4 | check **LevelIsDone**: var = done flag `0x4509e0(saved, level)` |
| 1088 | inst, v | 0 | check `0x459960(inst, v)` |
| 1090 | inst, other, f | 0 | check effect (particles) from inst to other, `0x44d5d0` |
| 1100 / 1101 | f / – | 0 | check thunderstorm on (`0x451ba0(f·0.01)`, f = strike interval ×100: 1500 in W3A/W3D/K3A/S3A, 1200 in W3B) / off (`0x451bd0()`); sent by trigger volumes, so "usage 0" at init; STORM.md §1 |
| 1110 | n, v | 0 | check parameter n (1..9) of the side-view camera mode 0x20 (CAMERA_SCRIPT.md §4.2); 1088 starts that mode |
| 1120 | inst, other | 6 | check **SetRaceInfo**: `0x455dc0(inst, other->0x28)` if other is type 3 (camera with polyline): the board `inst` rides along with Perso (`+0x4b4`), polyline = the track (RACE.md §2) |
| 1121 | inst, a, f | 0 | check **StartBoostSurf**(vector of inst, a, f·0.01): `0x456000`, RACE.md §3.3 |
| 1130 | a, b, c | 0 | check `0x44e990`/`0x44e9e0` on `game+0x64` |
| 1131 / 1132 | inst, v | 0 | check `0x44e980` / `0x44e9a0` |
| 1140 | inst, var | 0 | check var = 0; `0x404df0`; `0x453d90(vector, var)` (SaveAuto-like) |
| 1141 | inst | 1 | check `0x44e640(vector of inst)` |
| 1142 | inst | 2 | check `game+0x748 = inst` |
| 1150 / 1151 | f | 0 | check `0x401440` / `0x401480` (f·0.01) |
| 1152 | – | 0 | check `0x4014c0`: fill the whole screen (fade to black) |
| 1160 | var, obj | 1 | check `app+0x8c = var`, `app+0x90 = script object` of the House intro (GAMEFLOW.md §5) |
| 1170 / 1171 | – | 0 | check `0x44c7a0(-1)` / `0x44c840(-1)`: **life −1** (min 1) / **coin −1** (unique items, min 0); only the Jackpot in WWS (object 349) sends them |
| 1172 | – | 0 | check `byte game+0x70 = 1` |
| 1173 | var | 0 | check if `perso+0x260 > 0` (coins, unique items): var = 1 else var = 0; WWS object 425 uses it to turn the Jackpot on |
| 1180 | – | 0 | check `0x404b60(0, 0x1a, 0, 0x20)` |

## Subsystem `[0x5e823c]` (`0x46cca0`) – movement/effects on instances
| id | args | usage | meaning |
|---|---|---|---|
| 1500 | inst, a, f, b, c | 0 | check `0x478980(inst, a, f·0.01, b, c, 0)` |
| 1501 | inst, v | 59 | check `inst+0xfc = v` |
| 1502 | inst, x, y, z, w, flag | 27 | check target/vector: `+0x110..0x118 = x,y,z · k`, `+0x11c = w·0.01`, `+0x100 = flag`, `+0x108 = 0`, `vtable[0x1c]()` |
| 1503 | inst, v | 18 | check `inst+0x14c = v`, `vtable[0x1d]()` |
| 1504 | inst, v | 14 | check `inst+0x100 = v`, `+0x108 = 0` |
| 1505 | inst, f | 0 | check `0x478660(&pos, 1000.0, f·0.01)` |
| 1506 | inst, a, b, c, d | 63 | check SetWaterVolumeParameter (class 60): `0x474690(inst, a·0.01, b, c·0.01, d·0.01)` = cell, tiles (unscaled), amplitude, alpha; WATER.md §1 |
| 1507 | inst | 0 | check `0x4750e0(&pos)` |
| 1508 | inst | 17 | check register 20-byte node in list `0x5e8638`, `0x47cdf0` |
| 1509 | a, inst, mode, x | 0 | check mode 4/5: vector of inst → `0x477060(1, vec, 0)` |
| 1510 | obj | 0 | check add world instance to array `0x5e8428` |
| 1511 | inst, b | 7 | check `byte inst+0x120 = (b != 0)` |

## Sound (`0x467fa0`, vtable of the Cryo sound manager)
> **Note:** the table below is outdated. [SOUND.md](SOUND.md) §1 is authoritative: 1655 = PlayMusic(track), 1628 = stop of the
> 3D voices of (inst, id) with fade, 1652 = stop 2D, 1620/1630 = 3D loops, 1622/1623/1627 = 3D one-shot, 1600/1602 = 2D one-shot, 1606 = 2D loop.
| id | args | usage | meaning |
|---|---|---|---|
| 1600..1619 | … | 0 | tilde 2D variants (vtable +0x24/+0x28: PlaySound2D(id, 0, 1.0, …)) |
| 1606 | id, f | 8 | check `vt[0x28](id, 0, 1.0, f, 1e13)` |
| 1620 | inst, id, vol | 445 | check **PlaySound3D**: `vt[0x40](id, inst, 0, 1.0, 1e10, vol, 2.0)` |
| 1621 | inst, id, vol, f | 37 | check like 1620 with `f·0.01` |
| 1622 / 1623 | id, … | 22 | check 2D with parameters (`0x468468`) |
| 1628 | inst, id, f | 6 | check `vt[0x58](id, inst, f·0.01)` (adjust volume/pitch) |
| 1629 | inst, id, a, b, c | 0 | check 3D with distances (-b·0.01, c·0.01 or 1e10) |
| 1630 | inst, id, a, b | 233 | check `vt[0x40](id, inst, 0, 1.0, 1e13, a, b·0.01)` |
| 1631 | inst, id, a, b, c | 22 | check like 1630 with `c·0.01` as an extra |
| 1632..1636 | … | 4-19 | check variants (vt[0x38]/[0x40]) with ×0.01 and ×`0x4ab990` scaling |
| 1646/1656, 1649/1650 (`0x41fa40/50`), 1652..1654, 1657 | … | 0-26 | tilde stop/pause/resume |
| 1655 | id | 26 | check `vt[0x48](id)` = StopSound |

## Engine → script
Callback table `0x5cc360`: 100 SetVar(var, v) · 101 VolumeEnter(vol, actor) · 102 VolumeLeave · 103 VolumeIn,
plus collision Press/In/UnPress and Perso variants (see `src/ekovm.h`). Answers to game messages go through
`eko_set_var`; per-object message flags through `eko_msgmask_set` (MSGTEST/MSGCLEAR opcode).
