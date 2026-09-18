# Berichten van script naar engine (SEND-opcode)

Elk `SEND n` haalt n waarden van de stack: het eerste is het bericht-id, de rest zijn argumenten.
`arg0` is bijna altijd een instantieverwijzing `0x01000000 | i`; de engine gebruikt de onderste 24 bits als
index in de instantietabel `[0x50944c]->0x6c`. Getallen die een float voorstellen zijn ×100 opgeslagen
(de engine vermenigvuldigt met 0.01, constante `0x4aa0ac`). Tijd is in 1/100 s.

Gebruik = aantal keer verzonden tijdens de level-init van alle 28 levels (emulator).
Status: ✔ = gedrag uit de code gelezen, ~ = hypothese op basis van argumenten/strings, ? = nog niet bekeken.

## Routering
| id | handler | this |
|---|---|---|
| 1..999 | `instance->vtable[22]` (per klasse; basis `0x42d5e0`) | de instantie in arg0 |
| 1000..1180 | `0x444870` | game-object `[0x5d7afc]` |
| 1200..1202 | `0x403440` | wereld `[0x4c4c0c]` |
| 1500..1511 | `0x46cca0` | subsysteem `[0x5e823c]` |
| 1600..1657 | `0x467fa0` | geluidsmanager `[0x4c2dd8]` (Cryo Sound Library) |

De game-handler eindigt vaak in `0x4455be` = `eko_set_var(var, waarde)`: het antwoord aan het script
komt dus terug via een scriptvariabele (watchers worden gewekt).

## Instantieberichten (klasse-basis `0x42d5e0`, velden van de C++-instantie tussen haakjes)
| id | args | gebruik | betekenis |
|---|---|---|---|
| 1 | inst, anim, speed | 5 | ✔ PlayAnim: slot0 = anim, slots1-3 = -1, snelheid = animlen·k·speed, `0x42e290(0)` (reset blend) |
| 2 | inst, anim, flag, dur, x | 0 | ✔ PlayAnim met duur (deelt animlen door dur) |
| 3 | inst, anim, flag, n | 39 | ✔ SetAnim-variant (n ≤ 10 → status +0x88 reset); als flag=0 → snelheid 0 |
| 4 | inst, anim, flag, dur | 934 | ✔ PlayAnim op alle 4 slots, starttijd = nu; flag=0 → blend terug (`-[+0xa0]`) |
| 5 | inst | 9 | ✔ ResetAnim (`0x42e290(0)`) |
| 6 | inst, on | 285 | ✔ on≠0: als `[+0x1c] < 0` → `0x407790` (activeren/tonen); on=0 → `0x407850` (deactiveren/verbergen) |
| 7 | inst | 0 | ✔ annuleer wachtende 12/13 voor deze instantie (`0x4012f0`) |
| 10 | inst | 0 | ~ klasse 30-38 (volumes/triggers): eigen afhandeling `0x44f362` |
| 11 | inst, a, b | 1004 | ? vijandklassen 4-13 (`0x41a78b`, "trajet de suivi / aleatoire"): patrouille- of volgpad instellen |
| 12 / 13 | als 3 / 4 | 0 | ✔ uitgestelde 3 / 4: pas uitvoeren als `[+0x9c] != 0`, anders opnieuw proberen |
| 14 | inst, b0,b1,b2,f,d | 0 | ✔ per sub-onderdeel (16-byte records op `[+0x74]`, aantal `[+0xf8]->0x28`) kleur/waarde zetten; 0xffff = ongewijzigd |
| 15/17 | inst, a, mode, t1, t2 | 0 | ✔ overgang type A: byte +0xda, modusbits in +0xd8, tijden ×0.01 in +0xe8/+0xec, start = nu |
| 16/18 | inst, a, mode, t | 23/54 | ✔ overgang type B: byte +0xd9, modusbits, tijd ×0.01 in +0xe0 |
| 19 | inst | 0 | ✔ overgangsbits wissen (`+0xd8 &= 0xc0`) |
| 26 / 30 | perso | 0 | ~ alleen Perso-klasse (types 1-3, 18, 19): `0x44ce11` / `0x44cde9` |
| 29 | inst | 0 | ~ klassen 20/21 en 40/120/121: eigen afhandeling |
| 33 | inst, a, b | 50 | ? |
| 34 | inst, other | 340 | ✔ koppel instantie aan `other` (paar in tabel `[0x50944c]->0x50`, teller +0x4c) – "attach/link" |
| 40 / 55 | inst | 0 | ~ klasse 20/21 |
| 42 | inst, a, f | 5 | ✔ op padvolger `[+0x78]`: `0x437d10(a≠1, f·0.01)`, daarna positie uit pad kopiëren en `0x4077f0` (herpositioneren) |
| 43 | inst, a, f, c | 129 | ✔ als 42 met extra vlag `c==1` (`0x437d50`) |
| 44 | inst | 0 | ✔ padvolger `0x437d90()` (stop/reset) |
| 45 | inst, bits | 1296 | ✔ SetFlags: `+0xf0 \|= bits & (1\|2\|0x20)` |
| 46 | inst, a, b | 7 | ✔ padvolger `0x4381e0(a==1, b==1)` |
| 50 / 51 | inst, v | 279 / 44 | ~ klassen 50-52: `0x45108a` / `0x451059` |
| 52 | inst, v | 158 | ? |
| 53 | inst, v | 3 | ? |
| 54 | inst, mode, v | 98 | ✔ klasse 80: mode 1 → `0x451b74`; mode 2 → float +0x10c = v; daarna basis |
| 55 | inst, a, b | 34 | ? |
| 56 | inst, v | 606 | ✔ basis: float +0x6c = v·0.01; in `0x44e8f0` (meeste klassen) eerst `0x44e91b` |
| 57 | inst, v | 643 | ~ `0x44e907` (klasse-gemeenschappelijk) |
| 58..63 | inst, … | weinig | ? 59/60 in klassen 14-16 (`0x4100d2`, `0x40e7e3`: koppelt 8 instanties aan velden +0x1c8..+0x1e4 en zet vlag 0x40), 63 in klasse 17 |
| 650, 800 | inst | 11 / 2 | ? via vtable[22] van de klasse |

## Wereld (`0x403440`)
| id | args | gebruik | betekenis |
|---|---|---|---|
| 1200 | obj, type | 4907 | ✔ **SetTypeInstance**: `new` C++-klasse voor `type` (zie classmap_raw.txt), koppelt aan scriptobject |
| 1201 / 1202 | obj | 0 | ✔ vlag 0x400 zetten / wissen op de instantie (via vtable[4]) |

## Game (`0x444870`)
| id | args | gebruik | betekenis |
|---|---|---|---|
| 1000 | cam, target | 0 | ✔ camera (sub-object type 6): `0x4522b0(1, 1.0, target)` |
| 1001 | cam, mode | 94 | ✔ camera `0x452330(mode)` |
| 1002 | cam, a, b | 513 | ✔ camera `0x452360(a, b)` |
| 1003 | cam, target, mode, t | 177 | ✔ camera `0x4522b0(mode, t·0.01, target)` |
| 1004 | cam | 7 | ✔ camera `0x452320()` (reset) |
| 1010..1050 | … | 0 | ? (1042 groot: 152 instr.) |
| 1080 | rec | 0 | ✔ `0x456ed0(record)` op `game+0x18` |
| 1081 | inst | 0 | ✔ `0x404b60(1.5, inst, 1, 0)` (overgang/fade) |
| 1082 | level, var | 24 | ✔ **LevelIsEnable**: var = `0x450470(saved, level)`; waarschuwt zonder save-struct |
| 1083 | – | 0 | ✔ `0x404be0(0.5)` |
| 1084 | inst, var | 3 | ✔ var = instantietabel-waarde |
| 1085 | level, var | 4 | ✔ var = `0x4509e0(saved, level)` |
| 1088 | inst, v | 0 | ✔ `0x459960(inst, v)` |
| 1090 | inst, other, f | 0 | ✔ effect (particles) van inst naar other, `0x44d5d0` |
| 1100 / 1101 | f / – | 0 | ✔ `0x451ba0(f·0.01)` / `0x451bd0()` |
| 1110 | n, v | 0 | ✔ cameraparameter n (1..9) in struct `[0x4c737c]+0x61c` |
| 1120 | inst, other | 6 | ✔ `0x455dc0(inst, other->0x28)` als other type 3 |
| 1121 | inst, a, f | 0 | ✔ **StartBoostSurf**(vector van inst, a, f·0.01) |
| 1130 | a, b, c | 0 | ✔ `0x44e990`/`0x44e9e0` op `game+0x64` |
| 1131 / 1132 | inst, v | 0 | ✔ `0x44e980` / `0x44e9a0` |
| 1140 | inst, var | 0 | ✔ var = 0; `0x404df0`; `0x453d90(vector, var)` (SaveAuto-achtig) |
| 1141 | inst | 1 | ✔ `0x44e640(vector van inst)` |
| 1142 | inst | 2 | ✔ `game+0x748 = inst` |
| 1150 / 1151 | f | 0 | ✔ `0x401440` / `0x401480` (f·0.01) |
| 1152 | – | 0 | ✔ `0x4014c0`: volledig scherm vullen (fade naar zwart) |
| 1160 | a, b | 1 | ✔ `game+0x8c = a`, `+0x90 = b` |
| 1170 / 1171 | – | 0 | ✔ `0x44c7a0(-1)` / `0x44c840(-1)` |
| 1172 | – | 0 | ✔ `byte game+0x70 = 1` |
| 1173 | var | 0 | ✔ als `world+0x260 > 0`: var = 1 anders var = 0 |
| 1180 | – | 0 | ✔ `0x404b60(0, 0x1a, 0, 0x20)` |

## Subsysteem `[0x5e823c]` (`0x46cca0`) – beweging/effecten op instanties
| id | args | gebruik | betekenis |
|---|---|---|---|
| 1500 | inst, a, f, b, c | 0 | ✔ `0x478980(inst, a, f·0.01, b, c, 0)` |
| 1501 | inst, v | 59 | ✔ `inst+0xfc = v` |
| 1502 | inst, x, y, z, w, flag | 27 | ✔ doel/vector: `+0x110..0x118 = x,y,z · k`, `+0x11c = w·0.01`, `+0x100 = flag`, `+0x108 = 0`, `vtable[0x1c]()` |
| 1503 | inst, v | 18 | ✔ `inst+0x14c = v`, `vtable[0x1d]()` |
| 1504 | inst, v | 14 | ✔ `inst+0x100 = v`, `+0x108 = 0` |
| 1505 | inst, f | 0 | ✔ `0x478660(&pos, 1000.0, f·0.01)` |
| 1506 | inst, a, b, c, d | 63 | ✔ `0x474690(inst, a·0.01, b·0.01, c·0.01, d)` |
| 1507 | inst | 0 | ✔ `0x4750e0(&pos)` |
| 1508 | inst | 17 | ✔ registreer 20-byte node in lijst `0x5e8638`, `0x47cdf0` |
| 1509 | a, inst, mode, x | 0 | ✔ mode 4/5: vector van inst → `0x477060(1, vec, 0)` |
| 1510 | obj | 0 | ✔ voeg wereld-instantie toe aan array `0x5e8428` |
| 1511 | inst, b | 7 | ✔ `byte inst+0x120 = (b != 0)` |

## Geluid (`0x467fa0`, vtable van de Cryo-soundmanager)
| id | args | gebruik | betekenis |
|---|---|---|---|
| 1600..1619 | … | 0 | ~ 2D-varianten (vtable +0x24/+0x28: PlaySound2D(id, 0, 1.0, …)) |
| 1606 | id, f | 8 | ✔ `vt[0x28](id, 0, 1.0, f, 1e13)` |
| 1620 | inst, id, vol | 445 | ✔ **PlaySound3D**: `vt[0x40](id, inst, 0, 1.0, 1e10, vol, 2.0)` |
| 1621 | inst, id, vol, f | 37 | ✔ als 1620 met `f·0.01` |
| 1622 / 1623 | id, … | 22 | ✔ 2D met parameters (`0x468468`) |
| 1628 | inst, id, f | 6 | ✔ `vt[0x58](id, inst, f·0.01)` (volume/pitch aanpassen) |
| 1629 | inst, id, a, b, c | 0 | ✔ 3D met afstanden (-b·0.01, c·0.01 of 1e10) |
| 1630 | inst, id, a, b | 233 | ✔ `vt[0x40](id, inst, 0, 1.0, 1e13, a, b·0.01)` |
| 1631 | inst, id, a, b, c | 22 | ✔ als 1630 met `c·0.01` als extra |
| 1632..1636 | … | 4-19 | ✔ varianten (vt[0x38]/[0x40]) met ×0.01 en ×`0x4ab990` schaling |
| 1646/1656, 1649/1650 (`0x41fa40/50`), 1652..1654, 1657 | … | 0-26 | ~ stop/pauze/hervat |
| 1655 | id | 26 | ✔ `vt[0x48](id)` = StopSound |

## Engine → script
Callback-tabel `0x5cc360`: 100 SetVar(var, v) · 101 VolumeEnter(vol, actor) · 102 VolumeLeave · 103 VolumeIn,
plus collision Press/In/UnPress en Perso-varianten (zie `src/ekovm.h`). Antwoorden op game-berichten gaan via
`eko_set_var`; per-object berichtvlaggen via `eko_msgmask_set` (opcode MSGTEST/MSGCLEAR).
