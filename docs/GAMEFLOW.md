# Gameflow: boot, menu, levelwissels, save (Woody.exe, build 17-10-2001)

Status: **werkdocument, incrementeel bijgewerkt.** Statische analyse; alle adressen verwijzen naar
Woody.exe (imagebase 0x400000) en zijn na te lezen met `python tools/drange.py START END`.
Scripts: `python tools/ekodisasm.py extract/Data/<LVL>/code`.

Doel: vastleggen hoe het origineel van level naar level gaat, zodat `src/main_engine.c` (dat nu precies
één level van de commandoregel laadt) de volledige spelstroom kan nabouwen.

## 0. Samenvatting

- Er is één **App-object** (`[0x4c2d00]`, init `0x4023a0`) met een toestandsmachine `app+0x00`:
  0 = menu (`0x404e90`), 1 = spel (`0x401ab0`), 2 = logo's (`0x401500`), 3 = spel in BlackBox-modus.
  Dispatcher per frame: `0x401590` (switch `0x4017f4`).
- Levels worden geladen via **`LoadLevel(index)` = `0x4040c0`** → pad uit de leveltabel `0x4b12a0`
  (29 pointers) → `0x404140(pad, index)`.
- Een levelwissel wordt *aangevraagd* met **`RequestLevel(fade_s, level, state, menupage)` = `0x404b60`**:
  start een fade-out van `fade_s` seconden; als die op 0 staat doet `0x401590` de wissel
  (`0x4049a0` unload → `0x4040c0(level)` → `0x401400(state)` → menupagina).
- Scriptberichten die dat doen: **1081 `GotoLevel(level)`** (fade 1.5 s, state 1), **1083 `EndLevel()`**
  (fade 0.5 s, terug naar de hub van het huidige personage, statistieken bewaard), **1180** (naar Credits).
  **1084 `GetPrevLevel(var)`** geeft de index van het vorige level terug: daarmee kiest het hub-script
  zelf het spawnpunt.
- Het hoofdmenu is een **engine-menu** (pagina-object `app+0x3c`, 34 pagina's) dat over het gerenderde
  level **House** (index 0) heen getekend wordt. House is dus de menu-achtergrond + intro-cinematic,
  geen speelbaar level. "New game" → intro-cinematic in House → level 1 (WWS).

## 1. De leveltabel `0x4b12a0`

29 × `char*` (4 bytes per record), enige lezer `0x404124` in `LoadLevel` (`0x4040c0`/`0x404113`).
Het pad wordt door `0x44ff00` (`[app+0x44]` = config/paden-object `[0x5e5814]`) voor de CD-basis gezet.

| idx | pad | rol | personage (`0x404830`) |
|---|---|---|---|
| 0 | `\Data\House\House.gel` | menu-achtergrond + intro | 0 Woody |
| 1 | `\Data\WWS\WWS.gel` | hub Woody | 0 |
| 2..10 | W1A W1B W2A W2B W2D W3A W3B W3C W3D | levels Woody | 0 |
| 11 (0x0b) | `\Data\KWS\KWS.gel` | hub Knothead | 1 |
| 12..17 | K1A K1R K2A K2R K3A K3R | levels Knothead (R = race) | 1 |
| 18 (0x12) | `\Data\SWS\SWS.gel` | hub Splinter | 2 |
| 19..24 | S1A S1R S2A S2R S3A S3R | levels Splinter | 2 |
| 25 (0x19) | `\Data\BlackBox\BlackBox.gel` | BlackBox-modus (state 3) | 0 |
| 26 (0x1a) | `\Data\Credits\Credits.gel` | credits (bericht 1180) | ongewijzigd |
| 27 (0x1b) | `\Data\BlackBox\BlackBox.gel` | dev-slot (levelkiezer-dialoog, cfg-vlag 4) | ongewijzigd |
| 28 (0x1c) | `\Data\Lang\Lang.gel` | achtergrond taal-/savekeuze bij boot | ongewijzigd |

Personagekeuze: `0x404758` — `if (index <= 0x19) cfg+0x380 = byte_404830[index]` (0 = Woody
`\Common\Woody`, 1 = `\Common\Knothead`, 2 = `\Common\Splinter`; `0x404797..0x4047df` laadt de
bijbehorende resource-map). Voor index > 0x19 blijft het vorige personage staan.

Lang, Blackbox en Credits hebben een script van één object dat alleen `SetTypeInstance(inst0, 1)`
(de speler) doet en muziek stopt (`1655`); het zijn lege decors voor engine-menu's.

## 2. App-object (`[0x4c2d00]`)

| offset | betekenis | bron |
|---|---|---|
| +0x00 | state: 0 menu, 1 spel, 2 logo's, 3 spel (BlackBox) | `0x401400`, switch `0x401621` |
| +0x04 | exitcode / "stoppen" (1 als `+0xcd` gezet en fade klaar) | `0x4017e3` |
| +0x18 | 0x688-byte berichtenpomp-object (per level) | `0x404873` |
| +0x24 | level (gel) object 0x78 B, `0x426bd0` | `0x4044b8` |
| +0x28 | tijd/wereld-object (`+0x38` = dt, `+0x30` = speltijd) | `0x401810` |
| +0x2c | game-object 0x1c8 B (`0x4457b0`), per level opnieuw | `0x4041d7` |
| +0x30 | 0x54-byte object `0x4470d0` (HUD?) per level | `0x404925` |
| +0x34 | speler (Perso) = `[0x53a34c]` na script-init | `0x4048ad` |
| +0x3c | menu-object 0x94 B (`0x445e30`); pagina via `0x446490(page)`, lezen `0x446430` | `0x404d10` |
| +0x44 | config `[0x5e5814]`: `+0x378` debugmodus, `+0x380` personage, `+0x384` dev-vlaggen | |
| +0x48 | **save-struct** 0x14a4 B (`0x44ff70`) = `[0x5e5818]` | `0x402587` |
| +0x4c | save-slot-manager 0x52c8 B (`0x450a50`), 4 slots | `0x4025b0` |
| +0x50 | frameteller sinds load (eerste 4 frames zwart, `0x4016f8`) | |
| +0x54 | muziek-wisselteller menu | `0x404e30` |
| +0x58..+0x64 | menu-tussenstanden (slot, wachtframes, vervolgpagina) | |
| +0x68 | **huidige levelindex** | `0x40417e` |
| +0x6c | **vorige levelindex** (init 0x1b) | `0x404176`, `0x4025e4` |
| +0x70 | byte, bericht 1172 zet 1; reset bij load | `0x404230` |
| +0x74..+0x87 | 5 dwords statistiek van het afgelopen level (kopie van `perso+0x710..`) | `0x404c21` |
| +0x7c | speltijd-teller `[0x4c532c]` | `0x404c34`, `0x40177c` |
| +0x88 | logo-speler (`+8` = logo-index) | `0x40260c` |
| +0x8c, +0x90 | bericht 1160: scriptvariabele + instantie van de intro-cinematic | `0x444950` |
| +0x94 | byte: 1 = "New game gekozen" tijdens pagina 0x1f | `0x4051ca` |
| +0x98 | float idle-timer titelscherm (35 s, `0x4a9000`) | `0x402653` |
| +0x9c | 4 × fader {float rest, float totaal, byte richting} (12 B) | `0x401440/0x401480/0x445bf0` |
| +0xcc | byte `m_bHaveToClose`: fade-out naar levelwissel loopt | `0x404b6b` |
| +0xcd | byte: afsluiten na fade (`0x404cb0`) | |
| +0xd0 | float resterende fade-tijd | |
| +0xd4 | byte: er staat een level klaar | |
| +0xd8, +0xdc, +0xe0 | doel-level, doel-state, doel-menupagina | `0x404bbe` |
| +0xe4 | BlackBox-object 0xc0780 B (`0x484420`), alleen level 0x19 of cfg-vlag 0x10 | `0x4042d5` |
| +0xec | taal-extensie-string (`.eng .fra .ita .esp .all`) | `0x405a5c` |
| +0xf4 | bitvlaggen: 1 = frame bezig, 8 = wereld gepauzeerd, 0x10 = level geladen | |


## 3. Boot

1. WinMain (berichtenlus `0x405ea1`, in functie `0x405e78`) maakt het App-object (`0x4060c0` → init
   `0x4023a0`, globaal `[0x4c3a90]` = `[0x4c2d00]`) en roept per frame `App::Frame` = `0x401590` aan;
   returnwaarde 1 ⇒ `Woody.cfg` wegschrijven (`0x401130`) en WM_CLOSE.
2. Einde van `0x4023a0` (`0x402582..0x402666`): menu-object (`0x404d10`), save-struct (`0x44ff70(1)`,
   meteen leeg = "new game"-waarden), slot-manager (`0x450a50`), `app+0x6c = 0x1b`, logo-speler.
   - cfg-vlag `[cfg+0x384] & 4` (dev): `LoadLevel(0x1b)` → `0x4040c0` toont dan een **levelkiezer-dialoog**
     (`0x4066c0`, DialogBoxParam resource 0x66) en laadt het gekozen pad met index 0x1b. Geen logo's.
   - normaal: `app+0x98 = 35.0`, state = 2, logo 0 starten (`0x445d00(0)`).
3. State 2 = `0x401500`: logotabel `0x4b3960` = `\Logo\Cryo.hnm`, `\Logo\Eko.hnm`, `\Logo\Universal.hnm`.
   Toets 9 (`0x467400(9)`) breekt het lopende logo af. Als het logo klaar is
   (`0x445de0`): index 0 → logo 1, 1 → logo 2, 2 → klaar:
   - `[app+0x4c]->vt[1]() == 2` → `LoadLevel(0x1c)` (Lang) + menupagina 0x16 + state 0. Op de pc is
     `vt[1]` = `0x4078b0` = `return 0`, dus **deze tak is dood** (console-restant: memory-card-check).
   - anders: **`LoadLevel(0)` (House)** en `0x404e30` = menupagina 0 (titelscherm) + state 0 + muziek.
4. `wtrace.py --level` overschrijft `0x4b12a0[0]`; het spel staat dan nog steeds in state 0 met het menu
   over dat level heen (index blijft 0).

## 4. Levelwissel

### 4.1 Aanvragen: `0x404b60(float fade, int level, int state, int page)` (thiscall op App)
`+0xd4 = 1`, `+0xcc = 1`, `+0xd0 = fade`, fade-out starten `0x401480(fade)` (fader-slot met richting 1),
muziek uitfaden (`[0x5e61a4]->vt[0x4c](fade*0.9)`, `vt[0x9c]()`), `+0xd8/+0xdc/+0xe0 = level/state/page`.

Aanroepers (volledige lijst, `grep "call    0x404b60"`):

| waar | fade | level | state | page | betekenis |
|---|---|---|---|---|---|
| bericht **1081** `0x445057` | 1.5 | arg0 (ruwe int, geen masker) | 1 | 0 | **GotoLevel(level)** – deuren in de hubs |
| bericht **1083** `0x445106` → `0x404be0(0.5)` | 0.5 | hub van personage (1 / 0xb / 0x12), of 0 | 1 (of 0) | 0 | **EndLevel** |
| bericht **1180** `0x4448b9` | 0 | 0x1a Credits | 0 | 0x20 | credits (arg genegeerd) |
| `0x401d35` (in de frame, BlackBox-object `0x4846d0` meldt klaar) | 0.5 | 0x1a | 0 | 0x20 | credits |
| menu pagina 3, resultaat 14/15/16 `0x4056e3..` | 0.4 | 1 / 0xb / 0x12 | 1 | 0 | wereldkeuze na "load game" |
| menu pagina 3, resultaat 17 `0x405725` | 0.4 | 0x19 | 3 | 0 | BlackBox |
| menu pagina 0x1f `0x4050f3` | 0 (intro uitgekeken) of 0.5 (overgeslagen) | 1 | 1 | 0 | **New game → WWS** |
| menu `0x405780` (pagina's 0x16, 0x1c-ja, 0x1d, 0x20, 0x21) | 0.5 | 0 | 0 | 0 | terug naar House/titel |
| menu pagina 0x21 `0x405ac9` | 0.5 | 0x1c | 0 | 0x16 | (dode console-tak) |

`0x404be0` (EndLevel) in detail:
```
cur = app+0x68
if cur in {1, 0xb, 0x12, 0x19}: RequestLevel(fade, 0, 0, 0); return      // hub verlaten = naar titel
save_set_done(app+0x48, cfg+0x380 /*personage*/, cur)                     // 0x450700: rec+4 = 1
memcpy(app+0x74, perso+0x710, 20)                                         // levelstatistiek
app+0x7c = [0x4c532c]                                                     // (overschrijft stat[2])
RequestLevel(fade, personage==0 ? 1 : personage==1 ? 0xb : 0x12, 1, 0)
```

### 4.2 Uitvoeren: in `App::Frame` `0x401590`
Na het renderen van het frame (`0x4016d9..`): de 4 faders tekenen (`0x445bf0`, volledig-scherm quad
640×480 met alpha = rest/totaal·254, richting 1 = naar zwart), eerste 4 frames na een load zwart
(`app+0x50 < 4` → `0x4014c0`). Dan:
```
if (app+0xcc) { app+0xd0 -= dt; if (app+0xd0 <= 0) zwart presenteren (0x47ef80); }
if (app+0xcc && app+0xd0 <= 0) {
    app+0x7c = [0x4c532c];
    Unload();                       // 0x4049a0, zet +0xcc = 0
    if (app+0xd4) {
        LoadLevel(app+0xd8);        // 0x4040c0
        app+0xd4 = 0;
        SetState(app+0xdc);         // 0x401400 (overschrijft de state 1 die LoadLevel zet)
        if (state == 0) { if (app+0xe0 == 0) TitlePage(); /*0x404e30: pagina 0 + muziek*/
                          else menu_set_page(app+0xe0); }
    }
    if (app+0xcd) app+0x04 = 1;     // afsluiten
}
```

### 4.3 Unload `0x4049a0`
Alleen als `+0xf4 & 0x10`. Waarschuwt als `+0xcc` niet gezet was (`"The only way to close a level is to
set m_bHaveToClose to true !!!"`). Volgorde: `0x44e770`, geluid stoppen (`[app+0x20]` vt 0x8c/0x9c/0x4c/4),
resources vrijgeven `0x4414b0(1)`, `0x4414b0(0)`, debug-opname stoppen, `delete app+0x30`, `delete
app+0x2c` (game-object `0x4457d0`), `delete app+0x18`, VM vrijgeven `0x442160`, level-object
`app+0x24->vt[0](1)`, lichtsysteem `[0x4c4cac]` (`0x40abd0`), `[0x54ba28]`, BlackBox-object, `0x404b50`,
vlag 0x10 wissen. **Niets van de speler overleeft in het Perso-object**; alles wat blijft staat in de
save-struct (§6) en in `app+0x68/0x6c/0x74..`.

### 4.4 Load `0x404140(path, index)`
```
app+0x6c = app+0x68;  app+0x68 = index;                 // vorige / huidige level
reset_globals()  (0x404360: tellers, 0x42a5d0, vijand-/bonus-/projectiel-/effectlijsten leeg)
menu_reset(app+0x3c) (0x446410);  app+0x54 = 0
pad zonder extensie → game-object new 0x1c8 (0x4457b0) → app+0x2c
LoadFiles(path)  (0x4043d0): tijd-object resetten, "<dir>\code" laden (0x4424b0, 0x442570),
      level-object 0x426bd0(path) → app+0x24, wereld 0x408260(path), lichtsysteem 0x40ac30(path,0x10,0x400)
      ("Can't load lightsystem : please rebuild lights !!!"), 0x42a680, 0x4369a0.., 0x42d2c0
app+0xf4 &= ~8; app+0x50 = 0; app+0x70 = 0
LoadResources(path, index) (0x4045c0): resource-handlers registreren, personage kiezen uit
      byte_404830[index] (alleen index <= 0x19), "\Common\<Woody|Knothead|Splinter>" + taal-extensie
      laden (0x441230), daarna de level-resources; font 0x1030000 verplicht
      ("Pas de font dans votre level...")
eko-tijd zetten (0x444050(time*100))
ScriptInit (0x404850): 0x4427e0 (dubbele init-pass), callbacks 0x441ed0, berichtenpomp new 0x688,
      één keer 0x4019c0 (tick + berichten routeren → hier worden alle SetTypeInstance uitgevoerd),
      app+0x34 = [0x53a34c] (de Perso)
0x4048d0: alle lichten activeren; 0x404900: game-object koppelen aan Perso (0x445850), HUD-object
      new 0x54 (0x4470d0), camera-init 0x42a680
0x40c000; app+0xf4 |= 0x10; 0x404990
BlackBox-object alleen als cfg&0x10 of index == 0x19 → state 3, anders state 1  (0x401400)
4 faders resetten, fade-in 1.0 s (0x401440(1.0))
```
De Perso-reset (`0x44a6a0`, aangeroepen uit game-init `0x4458e2`) haalt de spelerstand uit
de save-struct (§6.2): levens, items, ladingen, health.

### 4.5 Spawnpunt
- **Standaard**: de positie van de Perso-instantie in het `.ins` van het level (`perso+0x30c` →
  `+0x1f4`, `0x44a6e6`). Er is géén engine-tabel "kom van level X → spawn Y".
- **Checkpoints** binnen een level: bericht **1030 `SaveAuto(this)`** (`0x445129` → `0x44aa10`):
  `perso+0x318` = positie van de instantie, `+0x324` = richting (vector-node van de instantie, anders de
  huidige kijkrichting). Bij doodgaan met levens > 0 respawnt `0x44a810` daar. Niet persistent.
  (Bericht 1020 = `perso->vt[0x98](1)` + camera `0x459030`: hoort bij dezelfde soort volumes; exacte
  betekenis niet uitgezocht.)
- **Terug in de hub**: het hub-script regelt het zelf. Object 297 in WWS (KWS/SWS analoog):
  init: `DELAY 1` → **1084 `GetPrevLevel(var51)`** (`0x4450ee`: `var = app+0x6c`). Daarna per waarde
  `var51 == N` (2..10): `1085(N+1, var54)` (is het volgende level al gedaan?), **`1140(deur_inst_N,
  var53)`**, deurgeluid + deuranimatie. `1140` (`0x4449e3` → `0x453d90`): Perso wordt op de
  positie/richting van de deur-instantie gezet (`perso+0x72c..0x740`, scripted actie 0x4a, state 9),
  `perso+0x724 = 0`, `perso+0x728 = var`, de instantie uit bericht **1142** (`perso+0x748`) wordt op
  dezelfde plek gezet, en het menu gaat naar pagina 0x1e (resultatenscherm, `0x404df0`, state 0).
  Komt de speler niet uit een level (`var51` = 0 of 0x1b) dan blijft de `.ins`-positie gelden.
- **House**: bericht 1141 (`0x4448d1` → `0x44e640`) zet `perso+0x564..0x578` (pos+richting) en vlag
  `+0x57c`; zolang `app+0x68 == 0` roept de frame elke tick `0x44e690` aan (`0x401d0c`), dat Woody met
  scripted actie 0x49 in die pose houdt (menu-achtergrond).

### 4.6 De deur in de hub (script, WWS rond woord 2819..2965)
```
speler in volume 5 (VOL_FLAG5)      → 1080 helptekst; 1050(var20, 2): var20 = 1 als actietoets 6 net ingedrukt
var20 == 1                          → 1042(deur 223, 500, 60, var21): speler binnen 500 eenheden van de deur
                                       én kijkrichting binnen 60° → 0x458e40(perso), var21 = 1
var21 == 1                          → 1081(2)  [GotoLevel W1A, fade 1.5 s]
                                       1040(deur 223, 17) [Perso scripted actie 17 = deur inlopen]
                                       geluid 1602, deur-anim (bericht 3)
```
Of een deur open mag: **1082 `LevelIsEnable(level, var)`** (§6.3). Einde van een level (W1A, woord
13794..13839): `1040(exitdeur, 17)`, deur-anim, geluid, `DELAY 100` → **`1083(0)`**.

#### Het vinkje / kruisje naast de deur

Dat is **één instantie** met een textuurgroep van twee frames (WWS-groep 145: frame 0 = rood kruis,
frame 1 = groene pijl; de lichtbalk boven de deur is groep 146, ook rood → groen). Instanties laten hun
textuurframes **nooit vanzelf** lopen (`0x47f290` kijkt alleen naar de override op `inst+0xd8`, INSTANCE.md §2),
dus zonder bericht blijft frame 0 staan: het kruis. Groen zetten gebeurt met **bericht 16**
`16 [marker, 0xffff, 1, 50]` = speel de frames eenmalig vooruit in 0,5 × de textuurduur en blijf op frame n−1 staan.

Het **init-blok** van elk deurobject vraagt `1082 LevelIsEnable(level, var)` en is daarmee watcher van die
variabele; wordt hij 1, dan stuurt het reactieve blok bericht 16 naar de marker en zet de variabele op 2 (zodat
het één keer gebeurt). Uit `out/wws_code.txt`:

| scriptobject | 1081 GotoLevel | level | 1082 → var | deur | marker |
|---|---|---|---|---|---|
| 217 + 193 | 2 | W1A | — (altijd open) | 223 | 193 |
| 218 | 3 | W1B | 26 | 224 | 194 |
| 322 | 4 | W2A | 84 | 323 | 180 |
| 324 | 5 | W2B | 89 | 325 | 181 |
| 326 | 6 | W2D | 94 | 327 | 182 |
| 328 | 7 | W3A | 99 | 329 | 157 |
| 330 | 8 | W3B | 104 | 333 | 158 |
| 331 | 9 | W3C | 109 | 334 | 159 |
| 332 | 10 | W3D | 114 | 335 | 160 |

Object **193** (W1A) heeft geen voorwaarde: het stuurt bericht 16 in zijn **init-blok**, dus die deur staat
vanaf het eerste bezoek op groen. De marker betekent dus **open**, niet *uitgespeeld*. Object **297** speelt de
onthulling opnieuw af als je terugkomt uit het level dat de volgende deur vrijspeelde (`1084 GetPrevLevel`,
`1085 LevelIsDone(10)`): eerst bericht 16 met modus 0 (terug naar het kruis), later modus 1. De drie
gebiedspoorten werken met een **paar** instanties (groene pijl 77/79/56, rood kruis 78/80/57, groepen 123/124)
vanuit de objecten 287/354. KWS en SWS hebben dezelfde opbouw.

> Poort: de init-berichten moeten **na** `eko_init` aan de app gegeven worden (VM.md §2, `level_load`),
> anders schrijft 1082 zijn variabele nog tijdens de init en wist het einde van de init de wekkerlijst —
> dan stuurt geen enkel deurobject ooit bericht 16 en blijft alles op het rode kruis staan.

## 5. Het menu (state 0, `0x404e90`)

- Menu-object `app+0x3c` (`0x445e30`): tabel van pagina-objecten op `menu + 4*page`, huidige pagina
  `menu+0x8c`; `0x446490(page)` wisselt, `0x446440(dt)` update + geeft een **resultaatcode**, `0x446430`
  leest de pagina. 2D over het 3D-beeld.
- De wereld blijft gerenderd via `0x401ab0`. Tabel `0x405af8[page]` bepaalt hoe: House (index 0) loopt
  altijd door (niet gepauzeerd); in een echt level wordt vlag 8 (pauze) gezet en, voor de meeste
  pagina's, een halfzwarte quad (`0x80000000`) over het beeld gelegd; pagina's 0x16 en 0x21 tekenen
  volledig zwart.
- Resultaatcodes: 1 New game, 2 intro starten, 3 Load game, 5 OK/verder, 6 opties, 7 quit, 8 ja, 9 nee,
  10..13 slot 0..3, 14..17 Woody/Knothead/Splinter/BlackBox, 18 herstart vanaf checkpoint (pauze),
  19..23 taal (.eng .fra .ita .esp .all), 24 terug.

| pagina | handler | rol |
|---|---|---|
| 0 | `0x404fe4` | titelscherm. `app+0x98` telt af vanaf 35 s; bij 0 → `SetVar(app+0x8c, 1)` (House-intro starten), bij < -0.5 → pagina 0x1f met `+0x94 = 0` (attract). Toets 9 → pagina 1 |
| 1 | `0x40519f` | hoofdmenu: 1 → pagina 0x1f met `+0x94 = 1`; 2 → `SetVar(app+0x8c, 1)`; 3 → savebestand checken (`vt[3]` `0x450aa0`: 0 ok → pagina 0xb = lezen, 7 → pagina 7 "geen save"); 6 → 0x1b; 7 → 0x1c |
| 0x1f | `0x40508c` | intro-cinematic loopt. Klaar als scriptvar `app+0x8c` == 4 of toets 6/9. New game: `0x44ffa0(save)` (save leegmaken) + `RequestLevel(0 of 0.5, 1, 1, 0)`. Attract: script-object `app+0x90` stoppen (`0x444380`, `0x4441d0`), var = 4, Perso resetten, zwart, fade-in 0.5, pagina 0 |
| 0xb → 2 | `0x405276`, `0x4052ac` | `Woody.sav` lezen (`vt[5]`), slotkeuze: slot → `0x456df0(slot, app+0x48)` kopieert 0x14a4 B naar de actieve save-struct + volumes → pagina 3 |
| 3 | `0x4056c8` | wereldkeuze (3D-objecten = klasse 110, §7): 14/15/16/17 → `RequestLevel(0.4, 1/0xb/0x12/0x19, …)`; 24 → pagina 1 |
| 0x18 / 0x19 | `0x4057f5` | pauzemenu (geopend door `0x404d80`; 0x19 als `perso+0x21c == 1`): 5 → verder (state 1 of 3), 6 → opties 0x1b, 7 → 0x1c, 18 → `0x445930(game)` + `0x4560f0(perso)` + state 1 |
| 0x1c | `0x4057a5` | "stoppen?": ja → in een level: `RequestLevel(0.5, 0, 0, 0)` (naar titel); in House (of dev-vlag 4): `0x404cb0(0.5)` = fade + afsluiten. nee → pagina 1 of pauzemenu |
| 0x1d | `0x405796` | **game over** (geopend door `0x404e10` uit de respawn `0x44a8f5` als levens == 0): OK → `0x44ffa0(save)` (hele actieve save gewist) + naar titel |
| 0x1e | `0x4058cd` | **resultaten** na een level (§5.1) |
| 6, 5, 0x17, 0xc, 8, 9 | `0x405358`, `0x4054ac`, `0x405586`, `0x405662` | "opslaan?" → slotkeuze (bezet slot, `0x4501f0 != 0` → 0x17 bevestigen) → `0x456dc0` kopie naar slot → `vt[4]` schrijft `Woody.sav` → 8 gelukt / 9 mislukt → terug naar 6. "nee"/klaar → `0x454050(perso)` (fade-out 0.5 s, `perso+0x724 = 5`; de Perso-update `0x454192` doet daarna fade-in, `SetVar(var van 1140, 1)`) + state 1 |
| 0x20 | `0x40577b` | credits: 5 → naar titel |
| 0x16, 0x21 | `0x404f90`, `0x405a49` | taal-/memory-card-pagina's (console-restant, op pc onbereikbaar) |

House-script, object 115: var1 = 0 rust, 1 = startverzoek van de engine, 2/3 = intro loopt (tekst via
1080, fades via 1151/1152), 4 = klaar. Bericht 1160 `(var1, inst 0x73)` meldt var en script-object aan
de App.

### 5.1 Resultatenscherm (pagina 0x1e), gestuurd door `perso+0x724`
0/2/3: paneel verbergen (`0x454580`). 1: paneel tonen (`0x454560`); op OK: `n = 0x453cf0(app+0x74)` =
aantal categorieën met `verzameld == totaal != 0` (0..2); `0x453fc0(perso, n != 0)` (juich-actie 0x4e of
0x4c), `0x44c840(perso, n)` = **n unieke items erbij** (`perso+0x260`, save blok+4).
4: score = `0x453cb0(stats, isRace)` met `isRace = prev ∈ {0xd,0xf,0x11,0x14,0x16,0x18}` (de R-levels):
race → alleen `stat[3]*100 (+50 % als stat[3]==stat[1])`; anders `max(0, 1800 - tijd_s)*10 +
stat[2]*100(+50 % als == stat[0]) + stat[3]*100(+50 % als == stat[1])`. Als score >
`save.rec[prev].best` (`0x450230`): best en de 5 stats in de save zetten (`0x450260`,
`0x450380..0x450440`). Daarna pagina 6 (opslaan?).
`stats` = `app+0x74` = kopie van `perso+0x710`: {totaal A, totaal B, verzameld A, verzameld B, float tijd}
(`0x453c80` init met `[0x4c5330]` en het Bonus-Woody-totaal `[0x5e54e4]`; zie BONUS.md).

**Welke twee categorieën?** `0x453c80` krijgt de twee totalen mee: `[0x4c5330]` (elke vijand telt zichzelf in zijn
PostLoad, ENEMY.md) en `[0x5e54e4]` (het Bonus-Woody-totaal, `[0x5e54f4]` in een race-level). Omdat de score
`stat[2]` met `stat[0]` vergelijkt (en `stat[3]` met `stat[1]`) móet `stat[2]` het aantal **verslagen vijanden** zijn —
de +50 % is er voor "alles opgeruimd". `[0x4c532c]`, dat EndLevel over `stat[2]` heen kopieert, wordt elke frame in
App::Frame bijgewerkt (`0x40177c`), vermoedelijk als "totaal − nog levend"; niet gedecompileerd (was §10.2).
`stat[4]` is de klok `perso+0x710+0x10` (`0x453ca0`, elke frame `+= dt`).

### 5.2 De port van het resultatenscherm (`src/main_engine.c`, `src/hud.c`, `src/player.c`)

De hele keten draait nu: **1083 EndLevel** bewaart de vijf statistieken van het level dat je verlaat
(`results_capture`, = `memcpy(app+0x74, perso+0x710, 20)`; ze moeten de levelwissel overleven, want het scherm draait
in de hub), **1140** start de sequentie (`results_begin` = `0x453d90`) en `results_update` (= `0x454090`) loopt de
toestanden af:

| `perso+0x724` | port | wat er gebeurt |
|---|---|---|
| 0 | `case 0` | Perso op de deurvector met **gescripte actie 0x4a** (.ins-anim 74, mét eigen cameratrack): de binnenzwevende animatie uit het screenshot van issue #7 — of de parasol in die animatie zit of de prop van bericht 1142 is, is niet nagekeken (geen data in de repo). Die prop wordt op dezelfde plek neergezet en getoond (`0x4077f0`). Zodra de actie klaar is: actie **0x4b** en toestand 1 |
| 1 | `case 1` | paneel zichtbaar (`0x454560`). OK: `n` = categorieën compleet, `n` unieke items erbij, juichen met **0x4e** (n ≠ 0) of **0x4c**, toestand 2/3, paneel weg |
| 2 / 3 | `case 2/3` | klaar met juichen → actie **0x4d**, score opslaan, toestand 4 en menupagina **6** ("Do you want to save?") |
| 4 | `case 4` | pagina 6 → **Ja**: `woodyre.sav` schrijven → pagina 8 "Game Saved" (of 9 "Save failed.") → terug naar 6; **Nee**: `0x454050` = fade-out 0,5 s, toestand 5 |
| 5 | `default` | na 0,5 s: prop verbergen (`0x407850`), fade-in 0,5 s, camera terug (mode 1), Perso vóór de deur en **`SetVar(perso+0x728, 1)`** — daar wacht het hub-script op |

De gescripte acties zelf lopen via `player_script_action`: het actienummer is het ruwe .ins-animatienummer, de
logische records 26..30 (`0x1a..0x1e` in `log_anim`) zijn de one-shots voor 74..78, en alles wat geen deur is (dus ook
10..16 en 19) volgt nu de **wortelbeweging** van `0x44e290` via `ins_root_at(inst, actie, fase, 1)`: het model wordt op
`(1−f)·pos + f·q(f)` getekend en op het laatste frame staat de Perso waar de wortel hem gebracht heeft, kijkend langs
`−E.rij2`, op de grond gezet. Dat is de "invliegende" beweging van de parasol-animatie.

**Afwijkingen en aannames** (alles wat hier staat is niet gedecompileerd):
- De **layout** van het paneel (`0x454963..0x455d97`) is niet ontleed; `hud_results_draw` gebruikt de strings die het
  origineel ervoor reserveert (12, 13, 15, 17, 46, 127, 128, 129, 130 en de tekens 7/8/10/11) in een eigen indeling,
  met een half-zwarte achtergrond zoals het tekstvak van 1080. String 14 "TOTAL" heeft nog geen plek.
- De twee categorieregels tonen `gepakt = totaal` plus "+ 50 %" als de categorie compleet is, met sprite 4 (de W van
  de HUD) en sprite **12** — een 64×64-icoon in bank-0-beeld 64 dat de HUD zelf nooit tekent (aanname: de vijanden).
- "Level" krijgt het volgnummer binnen de set van het personage (W1A = 1 … W3D = 9), want de levelnamen staan niet in
  de stringtabel.
- Slotkeuze (pagina's 5 / 0x17 / 0xc) bestaat niet: de port heeft één `woodyre.sav` (§6, bewuste afwijking), dus "Ja"
  schrijft meteen en gaat naar 8 of 9. De cursor van pagina 6 begint op het eerste kiesbare item ("Yes"); alleen van
  pagina 0x1c is bekend dat hij op "No" begint.
- De eindpositie van toestand 5 (`0x454244..`) is onbekend: de port zet hem terug op de deurvector, achter de fade.
- Het spel wordt tijdens het scherm **niet** gepauzeerd (de animaties moeten lopen) en er ligt geen halfzwart vlak
  overheen; welke vlaggen tabel `0x405af8` voor pagina 0x1e zet is niet gelezen. De gewone HUD blijft weg.
- De prop van 1142 wordt één keer neergezet (zoals `0x453d90` doet) en loopt niet met de animatie mee.

**Testen** (met de originele data): `./out/woody.exe extract/Data WWS --prev W1A --stats 12 12 25 20 245`
= "we komen uit W1A, 12 van de 12 vijanden, 20 van de 25 bonussen, 245 s"; het hub-script stuurt dan zelf 1140.
Enter/spatie = bevestigen, pijltjes omhoog/omlaag = Ja/Nee.

## 6. Save

### 6.1 Bestand `Woody.sav` (werkmap), 0x52c4 bytes
`u32 versie 0x11004` · 4 × slot (0x14a4 B, vanaf 4) · `u32 sfxvol[4]` (0x5294) · `u32 musicvol[4]`
(0x52a4) · `float x[4]` (0x52b4). Lezen `0x450be0` (versie ≠ → "Save file Woody.sav is obsolete..."),
schrijven `0x450b30`, bestaan-check `0x450aa0`. Slot-manager `app+0x4c` (vtable `0x4aaf3c`, slots op
`mgr+8`). Er wordt **alleen geschreven via het menu** (pagina 6 → 5 → 0xc), dus na elk uitgespeeld level.

### 6.2 Slot / actieve save-struct (`app+0x48` = `[0x5e5818]`, 0x14a4 B, reset `0x44ffa0`)
| offset | inhoud |
|---|---|
| +0x00 | checksum = som van alle bytes (signed) (`0x450030`) |
| +0x04 | 0x11004 · +0x08 = 0 |
| +0x0c + c·0x6dc | blok personage c (0 Woody, 1 Knothead, 2 Splinter) |
| blok+0x00 | levens (init 9) → `perso+0x250` |
| blok+0x04 | unieke items (type 36) → `perso+0x260` |
| blok+0x08 | speciale ladingen (type 35) → `perso+0x254` |
| blok+0x0c | float health (init 3.0; ≤ 0 → 1.0 bij laden, `0x44a759`) → `perso+0x24c` |
| blok+0x10 + L·0x3c | record level L (29 stuks) |
| rec+0x00 | beste score (`0x450230/0x450260`) |
| rec+0x04 | byte **done** (`0x4509e0` lezen, `0x450700` zetten) |
| rec+0x05 .. +0x24 | 32 bytes "uniek item n van dit level al gepakt" (`0x450730/0x450760`) |
| rec+0x28..+0x38 | 5 stats van de beste run (4 int + float tijd) |

(De port heeft deze drie velden nu ook: `SaveChar.best[29]`, `stats[29][4]` en `stat_time[29]` in `woodyre.sav`,
geschreven door `results_store`. Het bestandsformaat is daarmee veranderd; de magic is `WSV2`, oudere bestanden
worden genegeerd en het spel begint opnieuw.)
| +0x14a0 | u32 (in `0x450a10` opgeteld bij de som van alle scores) |

De Perso schrijft levens/items/ladingen **direct** in deze struct terwijl je speelt (`0x44c7a0`,
`0x44c800`, `0x44c840`, zie BONUS.md); `perso+0x258/+0x25c` (Bonus-Woody-tellers) beginnen elk level op 0
(`0x44a7d2`). Dit is de enige spelerstoestand die een levelwissel overleeft.

### 6.3 Berichten
- **1082 `LevelIsEnable(level, var)`** (`0x445074` → `0x450470(save, personage, level)`), personage =
  `cfg+0x380` (van het *huidige* level). Tabel `0x450694`: level ≤ 2 of > 0x19 → altijd 1; anders de
  done-vlag van de voorganger **in het blok van het huidige personage**:
  W1B←W1A, W2A←W1B, W2B←W2A, W2D←W2B, W3A←W2D, W3B←W3A, W3C←W3B, W3D←W3C, KWS en K1A←W2D,
  K1R←K1A, K2A←K1R, K2R←K2A, K3A←K2R, K3R←K3A, SWS en S1A←W3D, S1R←S1A, … S3R←S3A, BlackBox←S3R.
- **1085 `LevelIsDone(level, var)`** (`0x4450b1` → `0x4509e0`): done-vlag van (huidig personage, level).
- **1084 `GetPrevLevel(var)`**: `var = app+0x6c`. (MESSAGES.md noemt dit "instantietabel-waarde": fout;
  ook 1081 "overgang/fade" en 1083 zijn daar te vaag, en 1160 schrijft in de App, niet in het game-object.)
- `0x4509b0(c)`: personage 1 vrij als Woody-W2D (0,6) done, personage 2 als (0,10) done.
  `0x450050(c)` = voltooiingspercentage (gewichten W: 6,7,8,9,12,13,14,15,16; K/S: 14,15,16,17,18,20),
  `0x4501f0` = gemiddelde van de drie (0 = leeg slot).

## 7. Klasse 110 (ctor `0x451860`, vtable `0x4ab048`, handler `0x451940`)
Geen levelportaal maar een **3D-menu-object van het wereldkeuzescherm** (menupagina 3, klasse
`0x45e560`, globaal `[0x5e5adc]`). Basis-ctor `0x4890c0`. Bericht 58 `(inst, n)`: `inst+0x184 = n` en
`0x45e6f0(page3, inst)` registreert de instantie: n=0/1/2 → `page+0xec/0xf0/0xf4` (rang `inst+0x18c` =
1/2/3: de drie personages), n=3 → `page+0xfc` (rang 4, plus `0x436ca0`), n=4 → eerst `page+0xf8` (rang 3)
dan `page+0x100` (rang 4), n=5 → vier plekken `page+0x104..0x110` (rang 1..4). Dat klopt met House:
n = 0,1,2,3,4,4,5,5,5,5. `0x451890` zet een object op een cirkel (hoek uit rang − positie): de carrousel.
Alle andere berichten → basis `0x42d5e0`. Er is geen "speler raakt aan"-gedrag. Voor de port alleen
nodig als het menu 1:1 moet.

De hub-types 60/40/20/17/14 spelen geen rol in de levelwissel (die loopt volledig via 1050/1042/1081 in
het script).

## 8. Overige game-berichten 1010..1050 (`0x444870`)
| id | args | betekenis |
|---|---|---|
| 1010 | 9 ints | debug-print (`0x462c60`, uitgeschakelde logger) |
| 1020 | – | `perso->vt[0x98](1)`; als cameramodus ≠ 5: `0x459030(game+8, cam+0x90)` |
| 1030 | inst | **SaveAuto(this)**: checkpoint (§4.5); inst 0 → waarschuwing + `0x44a920(0,0)` |
| 1040 | inst, actie | Perso scripted actie `0x44dda0(actie, vector van inst, 0)` (17 = deur in) |
| 1043 | inst, actie, inst2 | idem met doelinstantie |
| 1041 | inst, x | `0x44e040(x, positie van inst)` |
| 1042 | inst, dist, hoek, var | var = 0; als Perso vrij (`0x44bcf0`, `+0x21c == 0`), binnen `dist` (XZ, niet ×0.01) van inst en kijkrichting binnen `hoek`° van de richting naar inst → `0x458e40`, var = 1 |
| 1044 / 1045 | inst | `0x44e140` / `0x44e1a0` |
| 1048 / 1049 / 1050 | var, mode | var = 0; toets 0 / 1 / 6 op `[0x5e6188]`; mode 0 = `0x467400`, 1 = `0x467420`, 2 = `0x467440` → var = 1 |
| 1081 | level | **GotoLevel** (fade 1.5 s) |
| 1083 | – | **EndLevel** (fade 0.5 s) |
| 1084 | var | **GetPrevLevel** |
| 1140 | inst, var | hub: speler bij deur + resultatenscherm (§4.5) |
| 1141 | inst | House: menu-pose |
| 1142 | inst | `perso+0x748 = inst` (object dat `1140` meeverplaatst) |
| 1160 | var, inst | `app+0x8c = var`, `app+0x90 = script-object` van de intro |
| 1172 | – | `app+0x70 = 1` |
| 1180 | x | naar Credits |

## 9. Recept voor de port

```c
enum { ST_MENU, ST_GAME, ST_LOGO, ST_BLACKBOX };
static const char *kLevels[29] = { "House","WWS","W1A","W1B","W2A","W2B","W2D","W3A","W3B","W3C","W3D",
  "KWS","K1A","K1R","K2A","K2R","K3A","K3R","SWS","S1A","S1R","S2A","S2R","S3A","S3R",
  "BlackBox","Credits","BlackBox","Lang" };
static int char_of_level(int i){ return i<=10?0 : i<=17?1 : i<=24?2 : i==25?0 : -1 /*ongewijzigd*/; }

struct App { int state, cur, prev /*init 0x1b*/; bool closing, have_next, quit; float fade_left;
             int next_level, next_state, next_page; int stats[5]; Save save; };

void request_level(App*a,float fade,int lvl,int st,int page){      // 0x404b60
    a->have_next=a->closing=true; a->fade_left=fade; fade_out(fade); music_fade(fade*0.9f);
    a->next_level=lvl; a->next_state=st; a->next_page=page; }

void end_level(App*a,float fade){                                   // 0x404be0 (bericht 1083)
    if(a->cur==1||a->cur==0xb||a->cur==0x12||a->cur==0x19){ request_level(a,fade,0,ST_MENU,0); return; }
    a->save.chr[cur_char].rec[a->cur].done=1;
    memcpy(a->stats,&perso->acc,20);
    request_level(a,fade, cur_char==0?1: cur_char==1?0xb:0x12, ST_GAME,0); }

void load_level(App*a,int idx){                                     // 0x4040c0 + 0x404140
    a->prev=a->cur; a->cur=idx;
    if(char_of_level(idx)>=0) cur_char=char_of_level(idx);
    load_everything(kLevels[idx]);  eko_init();  eko_tick_and_route_once();     // SetTypeInstance
    perso_init_from_save(&a->save.chr[cur_char]);   // levens, items, ladingen, health(<=0 → 1)
    a->state = idx==0x19 ? ST_BLACKBOX : ST_GAME;  frames_since_load=0;  fade_in(1.0f); }

void app_frame(App*a,float dt){                                     // 0x401590
    switch(a->state){ case ST_LOGO: logos(); break;        // 3 HNM's; klaar → load_level(0) + titel
      case ST_MENU: menu_frame(a); break;                  // rendert het level eronder
      default: game_frame(a); }
    draw_faders(dt); if(frames_since_load++<4) black();
    if(a->closing && (a->fade_left-=dt)<=0){
        unload_level(a); a->closing=false;
        if(a->have_next){ load_level(a,a->next_level); a->have_next=false; a->state=a->next_state;
            if(a->state==ST_MENU) menu_set_page(a->next_page /*0 = titel + muziek*/); }
        if(a->quit) exit_app(); } }

// game-berichten
case 1081: request_level(app,1.5f,arg0,ST_GAME,0); break;
case 1083: end_level(app,0.5f); break;
case 1084: eko_set_var(arg0&0xffffff, app->prev); break;
case 1082: eko_set_var(arg1&0xffffff, level_is_enable(&app->save,cur_char,arg0)); break;   // §6.3
case 1085: eko_set_var(arg1&0xffffff, app->save.chr[cur_char].rec[arg0].done); break;
case 1180: request_level(app,0,0x1a,ST_MENU,0x20); break;
case 1030: perso->respawn_pos=inst(arg0)->pos; perso->respawn_dir=vector_of(inst(arg0)); break;
case 1140: eko_set_var(arg1&0xffffff,0); perso_place_at(vector_of(inst(arg0))); results_begin(arg1); break;
```
Minimale getrouwe stroom zonder menu: boot → (logo's) → `load_level(0)`; "New game" = save resetten
(levens 9, health 3.0) en `request_level(0.5, 1, ST_GAME, 0)`; deuren en exits werken dan vanzelf via
1081/1083/1084/1140. Voor 1140 volstaat in een eerste versie: speler op de deurvector zetten, score
bijwerken, de `perso+0x724`-machine overslaan en meteen `eko_set_var(var, 1)` doen (dat is wat het
hub-script aan het eind van het resultatenscherm terugkrijgt, `0x45422c`).
Game over (levens 0 bij respawn): save resetten en `request_level(0.5, 0, ST_MENU, 0)`.
Een commandoregel-level kan blijven werken als "dev-slot": index 0x1b, personage ongewijzigd.

## 10. Onzeker

1. De resultaten-statemachine `perso+0x724` (update `0x454090`, tabel `0x4542c4`) is alleen in grote
   lijnen gelezen (zie §5.1/§5.2 voor de keten die de port daaruit draait). Niet uitgewerkt: de exacte
   eindpositie van toestand 5 (`0x454244..`), de layout van het paneel (`0x454963..0x455d97`) en wat
   tabel `0x405af8` voor pagina 0x1e aan pauze-/overlayvlaggen zet.
2. `stat[2]` is gezien de score-vergelijking het aantal verslagen vijanden (§5.1); waar `[0x4c532c]`
   precies vandaan komt (`0x40177c`, elke frame) is niet gedecompileerd. Welke van de twee totalen
   "Bonus Woody" is, staat wél vast: `stat[1]`.
3. 1082 leest het blok van het *huidige* personage. In KWS (personage 1) vraagt het script
   `LevelIsEnable(13..17)`; K1A (12) wordt zonder 1082 geopend. `LevelIsEnable(11/12)` zou in blok 1 naar
   W2D kijken en dus altijd 0 geven; de personagekeuze gebruikt daarvoor `0x4509b0` met expliciet
   blok 0. Niet dynamisch geverifieerd. De hubs vragen ook 1082/1085 voor levels van de andere
   personages (4, 7, 14, 16, 21, 23 / 2, 12, 19: vermoedelijk voortgangsborden).
4. Berichten 1020, 1041, 1044, 1045 en de actiecodes van `0x44dda0` (0x11 deur in, 0x49 menu-pose,
   0x4a..0x4e resultaten) zijn alleen op gebruik geduid.
5. Het BlackBox-object (`0x484420`, 0xc0780 B, state 3, level 0x19, vrij na S3R) en de credits-trigger
   in `0x401d1b..0x401d42` zijn niet ontleed.
6. Dev-vlaggen `cfg+0x384` (2 = geluidsdebug, 4 = levelkiezer-dialoog, 0x10 = BlackBox-object altijd):
   waar ze gezet worden is niet gezocht; `0x493e53` zet ze op 0.
7. Menupagina's 4, 7..0x15, 0x1a, 0x1b (opties, foutmeldingen; `0x4033f6` opent 0x1a vanuit de
   frame-invoer) zijn alleen globaal geduid; de betekenis van resultaatcodes 2 en 18 is afgeleid uit de
   handlers, niet uit de pagina-klassen.
8. Alles is statisch; aanbevolen controle met `tools/wtrace.py` (breakpoints op `0x404b60` en
   `0x4040c0`) tijdens New game → WWS → W1A → exit.
