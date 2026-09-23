# RESULTS.md — het resultatenscherm na een level (menupagina 0x1e, Woody.exe build 17-10-2001)

Status: **statische analyse**; adressen = Woody.exe (imagebase 0x400000), na te lezen met
`python tools/disasm.py game/Woody.exe START END`. Scratch: `out/res_453c80.txt` (0x453c80..0x456100),
`out/res_panel.txt` (paneelbasis 0x45b830..), `out/res_iris.txt` (iris 0x4776b0..). Aanvulling op GAMEFLOW.md §5.1
(toestanden `perso+0x724`, score), HUD_TEXT.md §1–§6 (font, strings, `RectVirtual`, kleuren, sprites), TITLE.md §5.4 en
MENU_LOAD.md §2.1 (paneelpagina + iris). Onzekere punten zijn gemarkeerd met **onzeker**. Dit document vervangt de
"eigen indeling" van HUD_TEXT.md §6 en GAMEFLOW.md §5.2 (afwijkingen).

## 0. Samenvatting

- Pagina 0x1e is een **paneelpagina** (basis `0x45b830`) met eigen vtable **`0x4aa934`** (object 0x6c B, `menu+0x78`,
  aangemaakt in `0x446028..0x446059`). Enter `0x4544b0`, tonen `0x454560` → `0x4544f0`, verbergen `0x454580` →
  `0x454530`, inhoud tekenen (`vt[17]`) **`0x454610`**, bevestigen (`vt[9]`) `0x4545a0`.
- Beeld (640×480 virtueel): een **zwarte iris** rond het **schermmidden (320, 240)** met gat-straal **175,8** (v = 0,37);
  Woody staat daar alleen in omdat de camera van animatie 74/75 hem in het midden zet. Linksboven **"RESULTS"**
  (rood met witte schaduw, grootte 25). Rechtsboven **"HIGH SCORE"** (wit, 18) met daaronder, gecentreerd, de
  **oude** beste score van dit level. Links een kolom van vijf regels, elk *icoon + label (gecentreerd op x = 45)* en
  *"=" op x = 85 + getal vanaf x = 100 dat optelt met 5000 punten/s*: **klok** (tijd `m:ss`), **vijandgezicht**
  (`verslagen/totaal`), **grote W** (`gepakt/totaal`), een witte streep + **"TOTAL"**, en het **$-icoon** met het aantal
  nieuwe unieke items (groot, rood). Tussen de eerste drie regels een "+". Onderaan gecentreerd de **levelnaam**
  ("Space Part A") op y = 415 en daaronder **"CLEARED!!"** (oranjerood met witte schaduw, 20).
- Er is **geen "OK"-knop of prompt**: de enige pagina-item (string 41 "SEE HIGH SCORES", vlag 2) wordt nooit getekend.
  Eerste bevestiging tijdens het optellen = alles meteen af; bevestiging als alles klaar is = resultaat 5 (juichen).
- Paginavlaggen (`0x405af8[0x1e] = 1`): **niet gepauzeerd, geen halfzwarte laag**, HUD verborgen. Het logo van
  `0x446b00` is al weggefadet.

## 1. De pagina en haar vtable

| vt | adres | wat |
|---|---|---|
| [1] | `0x45b990` | paneel-teken-basis (MENU_LOAD §2.1): `+0x28 += dt`, `+0x1c += dt`, `vt[16]`, iris, `vt[17]` |
| [2] | `0x446e30` | itemtabel `0x4b5738`: **1 item** `{0x20029 (41 "SEE HIGH SCORES"), vlag 2 (kop), 0, 0}` — nooit getekend |
| [3] | `0x46c320` | aantal = 1 |
| [4] | `0x446e20` | pagina-id 0x1e |
| [6] | `0x455db0` | `+0x5c ? 0 : 3` (lezer niet gevonden) |
| [7] | `0x446e40` | y-start 0,9 (ongebruikt, de lijst wordt niet getekend) |
| [9] | `0x4545a0` | bevestigen (§5) |
| [14] → [20] | `0x45bb30` → `0x445840` | terug = `ret`: **Esc doet niets** |
| [15] | `0x4544b0` | enter (§4.1) |
| [16] | `0x462c60` | `ret` |
| [17] | `0x454610` | inhoud (§2) |
| [18] | `0x45baa0` | resultaat leveren na sluiten (niet bereikt, §4.4) |

Velden (naast de paneelvelden van MENU_LOAD §2.1): `+0x3c` klok van de telregels (s), `+0x40` starttijd regel 0
(0), `+0x44/+0x48/+0x4c/+0x50/+0x54` = eindtijd van regel 0..4 (−1 = nog niet klaar), `+0x58` race-vlag,
`+0x5c` paneel zichtbaar, `+0x60/+0x64` stringpointers levelnaam, `+0x68` lusgeluid-object.

Per frame roept het menu (`0x4464f0`) met font `0x01030000` (**font van de levelbank = de hub**), grootte 30 en kleur
`0xffffffff` eerst `vt[1]` aan en daarna het logo `0x446b00`; alle tekst hieronder zet zelf grootte en kleur.

## 2. Tekenen: `0x454610` (alleen als `+0x5c`)

```c
void Results_Content(P *p) {                                   /* 0x454610 */
    if (!p->shown) goto sound;                                 /* +0x5c */
    Results_LevelName(p);                                      /* 0x4559b0: +0x60/+0x64/+0x58 uit de leveltabel, §3.6 */
    float off;
    if (p->t28 <= p->dur34) {                                  /* iris loopt nog (0,5 s na tonen) */
        if (p->closing22) { off = p->t28 * -300 / p->dur34; Lines(off, 1); }   /* dood: +0x22 blijft 0, §4.4 */
        else              off = (p->dur34 - p->t28) * -300 / p->dur34;         /* -300 -> 0; GEEN telregels */
    } else { off = 0; Lines(0, 0); }                           /* +0x58: 0 -> 0x454700, 1 -> 0x454860 */
    Title_RESULTS(off);                                        /* 0x455790 */
    HighScore(-off);                                           /* 0x455850 */
    LevelName_Cleared(-off);                                   /* 0x455bc0 */
sound:
    LoopSound_Update(&p->snd68, 0, 0x3d, [0x5e48c8], -1.0f);   /* 0x468e50 */
}
```
Volgorde van de aanroepen = tekenvolgorde binnen elke blend-lijst. **Onzeker/let op**: `RectVirtual` met vlag 4 (additief)
en vlag 8 (alfa) gaan naar verschillende lijsten; in `0x428ee0` wordt per diepte-emmer eerst de alfa-lijst
(`0x4290bf`) en dan de additieve lijst (`0x429182`) getekend, dus de additieve iconen (klok, vijandgezicht, streep)
komen **na** alle tekst en de iris. Visueel maakt dat niets uit (niets overlapt).

### 2.1 Vaste coördinaten (statische init `0x4542f0` en `0x4543c0`, via de CRT-tabel `0x4b1028`/`0x4b102c`)

Iconen, alle horizontaal gecentreerd op **x = 45** (`0x4ab27c`):

| # | icoon | bron | doel (x, y, w, h) | blend | adres |
|---|---|---|---|---|---|
| 0 | **klok** | levelbank (`0x01010001`) afb. 1, (51, 0, 36, 36) | (27, 75, 36, 36) | **4** additief | `0x4549fc`, data `0x4ab250` |
| 1 | **vijandgezicht** (geel vogeltje) | levelbank afb. 1, (0, 0, 50, 50) | (20, 170, 50, 50) | 4 additief | `0x454aa5`, `0x4ab264` |
| 2 | **grote W** = HUD-sprite 4 | bank 0 afb. 62, (0, 0, 94, 94) | (9,28, 275, 71,44, 71,44) → int (9, 275, 71, 71) | 8 alfa | `0x454b4f`, `0x4ab6a8` |
| 3 | **finishvlag** = HUD-sprite 5 (race) | bank 0 afb. 63, (0, 0, 94, 94) | (9,28, 225, 71,44, 71,44) | 8 alfa | `0x454c08`, `0x4ab6bc` |
| 4 | **streep** | blanco witte textuur (`surface = 0`) | (16, 370, 140, 2) | 4 additief | `0x454cc2` |
| 5 | **$** = HUD-sprite 3 | bank 0 afb. 61, (63, 63, 64, 64) | (20,68, 410, 48,64, 48,64) → (21, 410, 49, 49) | 8 alfa | `0x454d30`, `0x4ab694` |

Schaal van de bank-0-sprites 0,76 (`0x4ab280`), x = 45 − 0,38·w (`0x4ab278`). Kleur altijd 4× `0xfe808080`
(`0x454e2f`). Posities gaan als int naar `RectVirtual` (`fistp`, afronden in de standaard FPU-modus).
De levelbank is die van de **hub** (WWS/KWS/SWS.rck afbeelding 1, 128×128, 24 bit, in alle drie identiek; het is
een zwart vel met het vogelgezicht linksboven en de rode klok ernaast — zwart valt weg door het additief tekenen).
Correctie op TITLE.md §5.2: House.rck afbeelding 2 is **hetzelfde vel**, niet "blanco wit" (alfa-byte 0 misleidt de
PNG-export).

Rij-ankers `0x4b5748 + 8·r` (x, y), gebruikt voor de labels (rij = geval + 1) en voor "=" / getal:

| rij | x | y | formule (`0x4543c0`) | gebruik |
|---|---|---|---|---|
| 1 | 45 | **106** | 75 + 36 − 5 | tijdlabel + tijdregel |
| 2 | 45 | **215** | 170 + 50 − 5 | vijandlabel + regel |
| 3 | 45 | **341,44** | 275 + 94·0,76 − 5 | W-label + regel |
| 4 | 45 | **291,44** | 225 + 94·0,76 − 5 | vlaglabel + regel (race) |
| 5 | 45 | **375** | 370 + 5 | "TOTAL" + regel |
| 6 | **85** | **405** | constant | x = midden van alle "="-tekens; y = de $-regel |
| 7 | 624 | 16 | constant | rechterrand HIGH SCORE |
| 8 | 320 | 415 | constant | levelnaam |
| (0) | 16 | 16 | data | RESULTS |

### 2.2 Een telregel: icoon + label `0x4549c0`, "=" + getal `0x454f60`, inschuiven `0x454f20`

`0x4549c0(xoff, geval, str, grootte, kleur, schaduw)`: `SetSize(grootte)`; icoon uit §2.1 op `x + xoff`; als
`str`: label op `x = 45 − w/2 + xoff`, `y = rij(geval+1).y`, eerst (als `schaduw`) wit `0xfe808080` op
`(+0,05·cel, +0,05·cel)` (`0x4aab4c`, cel = 62·S/40), dan in `kleur`.

`0x454f20(start, xoff, forceer)`: `forceer ? xoff : (klok − start < 0,2 ? (0,2 − (klok − start))·(−1000) : 0)` —
elke regel **schuift in 0,2 s van −200 px naar 0** (icoon + label), lineair.

`0x454f60(start, waarde, rij, grootte, kleur, schaduw)` (`ret 0x18`):
```c
if (start + 0.2f > klok) return -1;                            /* 0x454f72: pas na het inschuiven */
SetSize(grootte);
Draw("=" /*string 8*/, 85 - w("=")/2, rij.y, 0xfe808080);      /* 0x454fef */
float rechts = 85 + w(itoa(waarde)) + 15;                      /* 0x45502f: breedte van het EINDgetal */
int n = fistp((klok - start - 0.2f) * 5000);                   /* 0x455041: 5000 punten per seconde */
float klaar = -1;
if (n > waarde) { n = waarde; klaar = klok; }                  /* 0x455064 */
x = rechts - w(itoa(n));                                       /* rechts uitgelijnd; eindgetal begint op x = 100 */
if (schaduw) Draw(itoa(n), x + 0.05*cel, rij.y + 0.05*cel, 0xfe808080);
Draw(itoa(n), x, rij.y, kleur);
if (klaar == -1) LoopSound_Request(&p->snd68);                 /* 0x468e40: tikgeluid zolang er geteld wordt */
return klaar;                                                  /* = starttijd van de volgende regel */
```
Het getal loopt dus van 0 op met 5000/s en staat rechts uitgelijnd, zodat het eindgetal op x = 100 begint.
Getallen: `itoa` `0x441820` (negatief → 0). De eindtijd wordt de start van de volgende regel; een regel duurt
0,2 s + waarde/5000 s + 1 frame.

### 2.3 De regels (normaal level: `0x454700`; race: `0x454860`)

`0x454700(xoff, forceer)`: `klok (+0x3c) += dt`; dan in volgorde, elk alleen als de vorige klaar is (≠ −1):

| regel | functie | icoon (geval) | label (S 15, wit, geen schaduw) | "=" + getal (S, kleur) | waarde | eindtijd |
|---|---|---|---|---|---|---|
| 0 tijd | `0x455160(+0x40)` | klok (0) | `m:ss`: `t = _ftol(app+0x84)` (afkappen), `m = t/60`, `s = t%60` met voorloopnul als `s < 10` (`0x4551bc`), scheiding string 10 ":" | 15, wit | `max(0, 1800 − t)·10` (`0x453d70`) | `+0x44` |
| — | `0x454920(1)` | | **"+"** (string 11), S 15, wit, gecentreerd op x 45, **y 136** (= 106 + 30, `0x4a9740`) | | | zodra `+0x44 ≠ −1` |
| 1 vijanden | `0x4552b0(+0x44)` | vijandgezicht (1) | `stat[2]` + string 9 "/" + `stat[0]` (`app+0x7c`, `app+0x74`) | 15, wit | `stat[2]·100`, ×1,5 (int, `(v + v/2)`) als `stat[2] == stat[0]` (`0x453d50`) | `+0x48` |
| — | `0x454920(2)` | | "+" op x 45, **y 245** | | | zodra `+0x48 ≠ −1` |
| 2 W-bonussen | `0x4553a0(+0x48)` | grote W (2) | `stat[3]/stat[1]` (`app+0x80`, `app+0x78`) | 15, wit | `stat[3]·100`, ×1,5 als compleet (`0x453d20`) | `+0x4c` |
| 3 totaal | `0x455580(+0x4c)` | streep (4) | string 14 **"TOTAL"**, S 15 verkleind tot breedte ≤ 65 (`0x45dc90`), **rood `0xfeff0000` met witte schaduw** | zelfde S, wit | `0x453cb0(stats, +0x58 == 1)` = som van de drie (race: alleen regel 2) | `+0x50` |
| 4 $ | `0x455650(+0x50)` | $ (5) | — (`SetSize(0)`, geen tekst) | "=" S **40** wit op (85 − w/2, 405) vanaf start + 0,2; getal via `0x454f60(start + 0,4, …, rij 6, 40, **rood `0xfeff0000`**, schaduw)` → verschijnt vanaf start + 0,6 | `0x453cf0` = aantal complete categorieën (0..2) = aantal nieuwe unieke $-items (GAMEFLOW §5.1) | `+0x54` |

De waarden komen uit `app+0x74..0x84` (= `perso+0x710`, GAMEFLOW §5.1: `{totaal vijanden, totaal W, verslagen,
gepakt, float tijd}`). Het screenshot "5:01 = 12363" is een regel die **nog optelt** (eindwaarde (1800 − 301)·10 = 14990).

**Race** (`+0x58 = 1`, `0x454860`): geen tijd- en vijandregel, geen "+"-tekens. Regels: **vlag** `0x455490(+0x48)`
(geval 3: vlagsprite op y 225, label `stat[3]/stat[1]` en getal op **y 291,44** (rij 4), waarde `0x453d20`), dan
TOTAL (`0x455580`, race-score) en $ (`0x455650`). Omdat `+0x48` bij enter −1 is (`0x4544d2`), start de vlagregel met
`start = −1`: geen inschuiven en de teller begint bij ≈ (klok + 0,8)·5000 — in de praktijk staat het getal er meteen.

### 2.4 "RESULTS" `0x455790(off)`

`SetSize(25)` (`0x41c80000`), string 13. Schaduw wit `0xfe808080` op `(16 + 0,05·38,75 + off, 16 + 1,94)`, daarna
**rood `0xfe800000`** op **(16 + off, 16)** (`0x455835`). Links uitgelijnd.

### 2.5 "HIGH SCORE" `0x455850(a)` (a = −off)

`SetSize(18)`, string 17, wit `0xfe808080`, **rechts uitgelijnd op x 624**: `(624 − w + a, 16)`. Daaronder het getal
`0x450230(save [0x5e5818], personage [[0x5e5814]+0x380], level app+0x6c)` = **de opgeslagen beste score van dit
level vóór deze run** (het nieuwe record wordt pas in toestand 4 geschreven, `0x40598d..0x405a36`, als de pagina al
weg is) — vandaar "0" bij een eerste keer. Getal: zelfde grootte en kleur, **horizontaal gecentreerd onder het label**
(`x = 624 − wLabel/2 + a − wGetal/2`), `y = 16 + cel(18) + extra(0)` = **43,9** (`0x455960..0x455985`).
Geen knipperen, geen "nieuw record"-aanduiding.

### 2.6 Levelnaam en "CLEARED!!" `0x455bc0(a)`

Naam = `str(+0x60) + " " + str(+0x64)`; de spatie is het middelste teken van string 40 "a a" (`0x455be6`). Grootte
15, verkleind tot breedte ≤ **400** (`0x45dc90`, stap 1, min 10). Wit op **(320 − w/2, 415 + a)**.
Dan `SetSize(20)`, string 12 **"CLEARED!!"**: gecentreerd op x 320, **y = 415 + cel(naamgrootte) + a** (= 438,25 bij
S 15); schaduw wit op +0,05·31 = +1,55, dan **`0xfe801400`** (R 1,0, G 0,16: oranjerood) (`0x455d84`). Altijd
getekend, ook in race-levels. Bij 480 hoog eindigt de cel op 469 (niet afgesneden in 640×480).

`0x4559b0` kiest per frame de strings uit `app+0x6c` (het level waar je vandaan komt; tabel `0x455b60`, index − 2):

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
| overig (0, 1, 11, 18, ≥ 26) | 1 "" | 1 "" | |

De race-vlag hier (`0x455af9`) is dezelfde set als `isRace` in de handler (`0x405932`: 0xd, 0xf, 0x11, 0x14, 0x16, 0x18).

### 2.7 De "W" rechtsboven uit het screenshot

**Niet gevonden.** Geen enkele tekenaanroep van pagina 0x1e (§2), van de paneelbasis `0x45b990`, van het menu
(`0x4464f0`) of van de HUD (verborgen, `0x447213`) tekent iets rechtsboven behalve "HIGH SCORE" en het getal. Het
logo `0x446b00` staat op (216, 16) en is op deze pagina al uitgefadet (`[0x5d7b28]` −5/s). De enige W is sprite 4
van de W-regel links (x 9..80, y 275..346). **Onzeker**: een additief 3D-object dat in dezelfde diepte-emmer als de
2D-laag valt zou na de (alfa-)iris getekend worden (§2, `0x428ee0`); niet nagegaan.

## 3. De iris

| | waarde | adres |
|---|---|---|
| vorm | zwarte ring van **50 segmenten** (stap 0,02 van een 512-sinustabel `[0x5e823c]`), binnenstraal `v·0,99·480`, buitenstraal 0,99·480 = **475,2** | `0x4776d0` |
| midden | **(320, 240)**, vast (`0x4ab5bc`, `0x4abd40`) — níet op Woody; de camera zet Woody in het midden | `0x477812`, `0x47782a` |
| punten | `(320 + r·sin θ, 240 − r·cos θ)` (tabel ×0,99 en ×−0,99, `0x4ab7d8`, `0x4abd44`) | `0x47770e`, `0x47773c` |
| kleur | vertexkleur (0, 0, 0, 1,0) → `0xff000000`, **dekkend zwart**, vlag 8 (alfa-lijst); coördinaten 640×480 virtueel, geklipt op 640/480 | `0x4777bc..`, `0x482fb7..0x483041`, `0x482d25` |
| tijd | lineair: `v = van − (van − naar)·min(t/duur, 1)` | `0x477920` |
| tonen (toestand 1) | **1,0 → 0,37 in 0,5 s** (gat 475 → **175,8 px**) | `0x4544f0` (`0x3ebd70a4` = 0,37) |
| verbergen (toestand 2/3) | 0,37 → 1,0 in 0,5 s | `0x454530` |
| enter (toestand 0) | `+0x38 = 0` ⇒ **geen iris** tijdens de aankomst | `0x4544b9` |

Bij v = 1,0 is het gat groter dan de schermhoek (400) ⇒ onzichtbaar. `0x45b990` animeert de iris alleen als `+0x38`
en `+0x30 > 0`; na het verbergen blijft hij op 1,0 staan.

## 4. Verloop per toestand van `perso+0x724` (handler `0x4058cd`, jumptabel `0x405d0c`)

### 4.1 Toestand 0 — aankomst (actie 0x4a)
Het menu gaat naar pagina 0x1e (GAMEFLOW §4.5). Enter `0x4544b0`: `+0x5c = 0`, `+0x38 = 0`, basis-enter `0x45b8c0`
(**SoundFx 0x3f**, `+8 = 0,5` invoervertraging, `+0x28 = 0`), `+0x3c = +0x40 = 0`, `+0x44..+0x54 = −1`, lusgeluid
leeg (`0x468e10`). De handler roept elke frame `0x454580` (niets, paneel is al weg). **Niets 2D in beeld**: geen iris,
geen tekst, geen HUD, geen overlay; de wereld loopt door.
**Eigenaardigheid**: `+0x28` loopt vanaf de enter; een bevestiging ná 0,5 s maar nog in toestand 0 zet via §5 alle
regels op "klaar", zodat het scherm straks meteen met eindwaarden verschijnt.

### 4.2 Toestand 1 — paneel (actie 0x4b)
Handler `0x4058f8`: elke frame `0x454560` (eenmalig door `+0x5c`): `+0x28 = 0`, `+8 = 0,5`, iris 1,0 → 0,37 (0,5 s),
`+0x38 = 1`.
- **0 .. 0,5 s**: iris sluit; "RESULTS" schuift van links in (x −284 → 16), "HIGH SCORE" + getal van rechts (+300 → 0),
  levelnaam + "CLEARED!!" van onder (y +300 → 0); lineair, `off = (0,5 − t)·(−300)/0,5`. Nog geen telregels, de
  telklok staat stil. Geen invoer (`+8`).
- **vanaf 0,5 s**: telklok loopt; regel 0 schuift in (0,2 s), telt op; "+"; regel 1; "+"; regel 2; TOTAL; $ (§2.3).
  Tikgeluid SoundFx **0x3d** loopt zolang er geteld wordt (`0x468e50`: starten `0x468a00`, stoppen `0x468a30`).
- **Bevestigen** (§5): eerste keer tijdens het tellen → alles af; als alles klaar (`+0x54 > −1`) → resultaat 5 →
  `0x453cf0` → `0x453fc0` (juich-actie 0x4e als n ≠ 0, anders 0x4c; toestand 2/3) + `0x44c840(n)` (n unieke items).

### 4.3 Toestand 2/3 — juichen
Handler `0x4058ef`: `0x454580` → `+0x5c = 0`: **alle tekst en iconen verdwijnen meteen**, iris gaat open 0,37 → 1,0 in
0,5 s, `+8 = 0,5`. Tikgeluid stopt. Daarna alleen het 3D-beeld (niet gepauzeerd).

### 4.4 Toestand 4 — opslaan? / 5 — weg
`0x405932`: score (§2.3 TOTAL-formule, `isRace`), record wegschrijven als hoger (`0x450260`, `0x450380..0x450440`),
dan **pagina 6** (`0x405a40`). Pagina 0x1e is dan niet meer actief; pagina 6 heeft paginavlag 2 (zie §6) en tekent
zijn eigen lijst. Toestand 5 heeft geen pagina (GAMEFLOW §5.2).
Het sluitpad van de paneelbasis (`+0x22`, `vt[18]`, uitschuiven met `off = t·(−300)/0,5` en `forceer = 1` in de
regels) is voor deze pagina **dood**: niets roept `0x45bae0`/`0x45bb40` aan (bevestigen is `0x4545a0`, terug is `ret`).

## 5. Bevestigen `0x4545a0` (actie 0xc, alleen als `+8 ≤ 0` en `+0xc == 0`, `0x4465c6`)

```c
p->result10 = 0;
if (p->lock24) return 0;
if (p->t54 > -1) { if (p->shown5c) p->result10 = 5; }          /* alles klaar: OK */
else if (p->t28 > p->dur34) {                                  /* nog aan het tellen: overslaan */
    float k = p->klok3c; p->klok3c += 60;                      /* 0x4ab284 */
    p->t40 = p->t44 = p->t48 = p->t4c = p->t50 = p->t54 = k;   /* alle regels "klaar op tijd k" */
}                                                              /* ⇒ geen inschuiven meer, getallen op eindwaarde */
p->deferred14 = 0;
return p->result10;
```
Terug (Esc, actie 5) en omhoog/omlaag doen niets zichtbaars.

## 6. Paginavlaggen (`0x404e90`, byte-tabel `0x405af8`, sprongtabel `0x405ae0`)

| geval | code | pauze (`app+0xf4` bit 3) | halfzwart `0x80000000` | pagina's |
|---|---|---|---|---|
| 0 | `0x404ef1` | = (`app+0x68` ≠ 0, dus in een level) | nee | 0–4, 0x1d, 0x1f, 0x20 |
| **1** | **`0x404fb7`** | **uit** | **nee** | 5, **0x1e** |
| 2 | `0x404fb5` | uit | ja | 6, 8, 9, 0xc–0xe, 0x12–0x15, 0x17 |
| 3 | `0x404ee6` | = in level | ja | 7, 0xa, 0xb, 0xf–0x11 |
| 4 | `0x404fc3` | aan, + `0x4014c0` | nee | 0x16, 0x21 |
| 5 | `0x404ee8` | = in level | = in level | 0x18–0x1c |

HUD: `0x448450(2)` = verborgen voor alles behalve 0x18/0x19 (`0x404e9d..0x404ebe`). Pagina 0x1e: wereld loopt door
(Woody's animaties), **geen donkere laag**, HUD weg. Pas de volgende pagina 6 ("Do you want to save?") legt de
halfzwarte laag over het beeld.

## 7. Recept voor de port (`src/hud.c`, `src/main_engine.c`)

```c
/* toestand: shown, t (sinds tonen/verbergen), iris_from/to/t, klok, end[5] (-1), race, snd */
show(): shown = 1; t = 0; input_delay = 0.5; iris(1.0 -> 0.37, 0.5); iris_on = 1;
hide(): shown = 0; t = 0; input_delay = 0.5; iris(0.37 -> 1.0, 0.5);
enter(): shown = iris_on = 0; t = 0; klok = 0; end[*] = -1; fx(0x3f); input_delay = 0.5;
draw(dt): t += dt; if (iris_on) hud_iris(v(t));               /* midden (320,240), dekkend zwart */
  if (!shown) { stop fx 0x3d; return; }
  off = t < 0.5 ? (0.5 - t) * -300 / 0.5 : 0;
  if (t > 0.5) { klok += dt; lines(); }                        /* §2.3, 5000 pt/s, 0.2 s inschuiven per regel */
  RESULTS (25, rood 0xfe800000 + witte schaduw) op (16+off, 16);
  HIGH SCORE (18, wit) rechts op 624-off, getal (oude best) gecentreerd eronder op y 43.9;
  naam (15, fit 400) op (320-w/2, 415-off); CLEARED!! (20, 0xfe801400 + schaduw) op y 415+cel+(-off);
confirm(): end[4] > -1 ? OK : (t > 0.5 ? skip_all : niets);
```
Schaduw = dezelfde tekst in `0xfe808080` op (+0,05·cel, +0,05·cel), vóór de gekleurde tekst. Kleuren volgens
HUD_TEXT §5.2 (`rgb = min(1, 2·c/255)`). Geen "OK"-item, geen halfzwart vlak, geen achtergrondpaneel.

## 8. Onzeker / open

1. De grijze "W" rechtsboven uit het screenshot (§2.7).
2. Afronding van `fistp` in de teller en de icoonposities (standaard FPU-modus = afronden naar dichtstbij aangenomen);
   `_ftol` voor de tijd kapt af.
3. Tekenvolgorde alfa- vs. additieve lijst binnen de 2D-emmer (§2) is uit de structuur van `0x428ee0` afgeleid, niet
   tot in detail gevolgd.
4. `vt[6]` `0x455db0` (0 als het paneel zichtbaar is, anders 3): geen lezer gevonden.
