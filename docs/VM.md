# EKO CODE – de script-VM van Woody Woodpecker (PC, Eko Software 2001)

Alles hieronder is afgeleid uit Woody.exe (build 17-10-2001) en geverifieerd met de
Python-tools in `tools/`: de disassembler ([ekodisasm.py](../tools/ekodisasm.py)) parseert alle 28
`code`-bestanden zonder fouten (alle sprongdoelen geldig, elk object eindigt netjes), en de
emulator ([ekovm.py](../tools/ekovm.py)) draait de level-initialisatie van alle 28 levels met een
sluitende stack en produceert alleen berichttypes die de engine ook echt afhandelt.

## 1. Bestandsformaat `Data\<LVL>\code`

Alles is little-endian uint32 ("woord"). Indexen hieronder zijn woordindexen vanaf het begin.

| woord | betekenis |
|---|---|
| 0-1 | magic `EKO CODE` |
| 2 | `nobj` aantal script-objecten |
| 3 | offset objecttabel (altijd 12) |
| 4, 5 | `nvars`, offset variabelentabel |
| 6, 7 | `nvol`, offset world_volume-tabel |
| 8, 9 | `nstr`, offset stringtabel |
| 10, 11 | `ncol`, offset world_collision-tabel |
| 12 .. 12+nobj-1 | objecttabel A: per object de codestart (relatief t.o.v. codebasis) |
| 12+nobj .. | **codebasis** B: de bytecode van alle objecten achter elkaar |
| vars_off | `nvars` entries van 2 woorden (waarde, runtime-pointer), daarna per variabele een lijst `[count][objId...]` = *watchers*: objecten die opnieuw draaien als de variabele verandert |
| vol_off | `nvol` entries van 4 woorden (runtime: count u16, flags u16, tijdstempel, ptr) + per volume een watcher-lijst `[count][objId...]` |
| str_off | `nstr` pointers (runtime ingevuld) gevolgd door C-strings |
| col_off | `ncol` entries van 3 woorden + watcher-lijsten |
| laatste-2 | `0xFADEFADE` |
| laatste | compilerversie, moet 6 zijn (`"Conflit: La version du compilateur est %d..."`) |

Loader in de exe: `0x4424b0` (leest bestand, versiecheck), `0x442570` (bouwt tabellen), `0x4427e0` (init).

## 2. Runtime-model

- **Integer-stack** (`0x5ce2b4`, sp op `0x5d051c`) en een aparte **boolean-stack** van bytes
  (`0x5ce44c`, sp op `0x5d0520`). Vergelijkingen halen ints van de int-stack en zetten een bool op de bool-stack;
  `JF` haalt van de bool-stack.
- **Globals** `0x5ce444 + 4*n`: global 0 = `this` (het actor-id in een `FOREACH`), global 1 = vrij.
- **Variabelen** (per level) met watcher-lijsten. `STOREVAR` wekt alle watchers.
- **Tijd** `0x5d0514` in honderdsten van seconden: de engine zet per frame `time = (int)(frametijd_s * 100.0)` (`0x4019fd`, constante `0x4a9010`). `DELAY 100` = 1 seconde.
- Elk object begint met `JMP <entry>`. Bij de init wordt die `JMP` (2 woorden) tijdelijk door `NOP NOP`
  vervangen en het object vanaf woord 0 uitgevoerd (init-blok). Daarna wordt de `JMP` teruggezet.
  Wordt het object later "gewekt", dan start het weer bij woord 0 en springt dus direct naar `<entry>`:
  bij 8006 van de 13227 objecten is dat het einde (object doet niets reactief), bij 5221 is het een
  reactief blok binnen het object.
- **Init** (`0x4427e0`) draait alle objecten **twee keer**; de berichten van de eerste ronde worden
  weggegooid (`0x441d40`), timers gereset, dan ronde twee "echt".
  De berichten van ronde twee gaan in de **wachtrij** (`0x5bd300`, max 1280) en worden pas **na** de init aan de
  game gegeven, net als elke tick. Dat is niet vrijblijvend: een handler die een scriptvariabele zet
  (`1082 LevelIsEnable` voor de hub-deuren, GAMEFLOW.md §4.6) wekt daarmee zijn watcherobject, en aan het eind van
  `0x4427e0` worden alle wekkerlijsten gewist. Geeft de port ze meteen tijdens de init door, dan verdwijnen die
  wekkers en draait zo'n object nooit (`level_load` doet de doorgifte daarom na `eko_init`).

### Tick (`0x442240`, aangeroepen uit de game-loop `0x4019c0`)
1. `0x442350`: voer alle verlopen **DELAY**-entries uit (gesorteerde lijst `0x5d24d8..`, entry = {tijd, doel}).
2. `0x4423a0`: voer alle verlopen **DURING**-entries uit (ongesorteerde lijst `0x5d0578..`).
3. `0x442320`: wissel de dubbele wake-lijst (`0x4b3578`/`0x4b357c`, max 1000).
4. Voor elk gewekt object (dedupe via frame-stempel per object `0x5d0550`): `run(codestart)`.
5. `0x4423f0`/`0x442450`: wis de per-frame flags van gewijzigde volumes/collisions.
6. Framecounter `0x4b3574`++, statistieken (`"Total des during executes %d"` enz.).

Daarna verwerkt de game-loop de **uitgaande berichtenwachtrij** (`0x5bd300`, 48-byte records, max 1280):
`record = {id, nargs, arg0..}`; arg0 is meestal de doelinstantie (`0x01000000 | index`).

## 3. Opcodes (handler-tabel `0x5d0418`, init in `0x442a30`, interpreter `0x4429f0`)

Handler-signatuur: `uint32* handler(uint32* pc)` geeft de volgende pc terug. Opcode ≥ 63 stopt.

| op | naam | operanden | semantiek |
|---|---|---|---|
| 0 | NOP | | |
| 1 | HANG | | geeft dezelfde pc terug (ongebruikt) |
| 2 | END | | stop deze run |
| 3 | PUSH | imm | |
| 4 | PUSHSTR | n | push pointer naar string n |
| 5 | PUSHVAR | n | push var[n] |
| 6 | STOREVAR | n | var[n] = pop; wek watchers |
| 7-11 | ADD SUB MUL DIV NEG | | int-stack |
| 12 | TOBOOL | | bpush(pop != 0) |
| 13-18 | EQ NE GT GE LT LE | | pop b, pop a → bpush(a ? b) (signed) |
| 19-21 | OR AND NOT | | bool-stack |
| 22 | JMP | t | pc = t (absoluut in B) |
| 23 | JF | t | if !bpop: pc = t |
| 24 | DELAY | d, t | plan run(t) op tijd now+d |
| 25 | SKIP1 | x | nop met operand |
| 26 | DURING | d, t | plan run(t) op tijd now+d (tweede lijst) |
| 27 | PUSHTIME | | push now |
| 28 | SEND | n | pop n waarden; bericht {id=eerste, args=rest} in de wachtrij |
| 29,31,32,48,49 | VOL_FLAGb | v | bpush(bit 5/4/3/6/2 van volume[v].flags) |
| 30 | VOL_STATE | v | bpush(flags==0 or flags&9) |
| 33 | VOL_COUNT | v | push volume[v].count |
| 34 | FOREACH | v, end | voor elke actor in volume[v] zonder flag 1: this=actor; run(body); daarna pc=end |
| 35/36 | PUSHGLOBAL/STOREGLOBAL | n | |
| 37/38 | VOL_HAS / VOL_HASNOT | v, a | actor a in volume v (37: alleen als flags&4 en &0x24) |
| 39-42 | COL_FLAGb | c | bit 5/4/6/3 van collision[c].flags |
| 43 | JMPPOP | | pc = pop |
| 44 | VOL_SEQ | v1, v2 | bpush(vol[v2].time - vol[v1].time == 1) |
| 45/46 | VOL_ACTOR_F2/F1 | v, a | actor a in volume v met entry-flag 2/1 |
| 47 | INVALID | | stop |
| 50 | VOL_ALL_F1 | v | alle actors in v hebben flag 1 |
| 51/52 | COL_B3_BIT0 / COL_B2_BIT0 | c | |
| 53 | COL_ALL_F4 | c | |
| 54-56 | COL_ACTOR_F2/F4/F1 | c, a | |
| 57 | CUT | x | keyword `cut`, niet geïmplementeerd: bpush(0) + waarschuwing |
| 58 | MSGTEST | o | bpush(pop & msgmask[o]) |
| 59 | MSGCLEAR | o | msgmask[o] = 0 |
| 60 | VOL_PAIR | v1, v2, a | a in v1 met flag 1 én in v2 met flag 4 |
| 61 | DELAYPOP | t | plan run(t) op now+pop |
| 62 | RANDOM | | push rand() % pop |

## 4. Berichtroutering (engine-kant, `0x4019c0` → `0x401370`)

| id-bereik | handler | betekenis |
|---|---|---|
| 1200-1300 | `0x403440` | 1200 = **SetTypeInstance(obj, type)**: `new` van de C++-klasse voor `type` (tabel in `0x403502`, 121 types → 42 klassen). 1201/1202 = flag 0x400 zetten/wissen |
| < 1000 | `instance->vtable[22](record)` | per-klasse berichthandler; gedeelde basis `0x42d5e0` (ids 1..56, 22 cases), klassespecifieke ids inline (bv. Perso 26/30, vijanden 6/11) |
| 7 | `0x4012f0` | annuleer wachtende berichten 12/13 voor dat object |
| 1000-1499 | `0x444870` (this `0x5d7afc`) | game/level-berichten, 46 cases voor 1000..1180 (o.a. LevelIsEnable, SaveAuto, SetRaceInfo, StartBoostSurf) |
| 1500-1599 | `0x46cca0` (this `0x5e823c`) | 12 cases (positie/vector-achtig, floats × schaal) |
| 1600-1700 | `0x467fa0` (this `0x4c2dd8`) | 58 cases, geluid (vtable-calls met volume 1.0) |

Handlers die `true` teruggeven worden opnieuw geprobeerd (`0x401250`, lijst van 32 uitgestelde records).

Referentie-encoding in argumenten: `0x01000000 | i` = instantie i (in `[0x50944c]->0x6c[i]`), `0x02000000 | i` = tweede soort verwijzing (nog te bepalen), `0x0002xxxx` paren komen voor als (type, index).

## 5. Engine → VM

Callback-tabel `0x5cc360[id]` (dispatcher `0x441c90`), ids 100-103 gezet in `0x441ed0`:
100 = SetVar(var, waarde), 101 = volume **Enter**(vol, actor), 102 = **Leave**, 103 = **In**.
Collision-varianten (Press/UnPress/In/PersoUnpress) via `0x441fc0..0x442100`.
Elke gebeurtenis zet flags op het volume en op de actor-entry en wekt de watcher-objecten van dat volume (`0x443d20`).
Bron van de volume-events: `0x430210` (bounding-volume test per actor, `push 0x65/0x66/0x67`).

## 6. Implementaties

- `tools/ekovm.py`: Python-emulator (init + tick), gebruikt voor statistieken en als referentie.
- `src/ekovm.c` + `src/ekovm.h`: C-implementatie met dezelfde semantiek (inclusief de eigenaardigheden:
  dubbele init-pass, `FOREACH` beëindigt de omliggende run, wake-lijst achterstevoren, wachtrij-cap van 1280
  berichten met waarschuwing). `src/ekorun.c` is een testharnas; traces zijn identiek aan de Python-emulator.
- Beide gebruiken de MSVC-`rand()` LCG (`seed*214013+2531011`) zodat `RANDOM` deterministisch vergelijkbaar is.

## 7. Wat nog ontbreekt voor een 1:1 reimplementatie

- Semantiek per berichttype: zie [MESSAGES.md](MESSAGES.md) (routering compleet, gedrag deels).
- Plek van de VM-tick in de frame-loop (`0x4019c0` wordt aangeroepen uit `0x401ab0`/`0x404822`).
- Betekenis van de `0x02000000`-verwijzingen en de `.ins`-koppeling object ↔ instantie.
