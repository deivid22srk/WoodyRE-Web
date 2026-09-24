# BOMB_CARRY.md — Woody en de bom: oppakken, dragen, gooien (Perso-toestand 6), de bommenautomaat (bericht 1090), kisten (type 120/121) en klasse 17

Statische analyse van `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone). Elke bewering heeft een adres; floats zijn uit
`.rdata` van de exe gelezen. "Onzeker" = niet regel voor regel gelezen of niet in het origineel nagemeten.

Dit document gaat over wat de **speler** met bommen doet en wat bommen **breken**. De bomklasse zelf (type 40: lont, toestanden 1..6, vlucht,
stuiteren, explosie-effect, knipperen, lanceerders/bommengooiers, het bomkanon type 21) staat in **BOMB.md**; hier alleen wat nodig is om
de speler-kant te begrijpen. Verder: PROJECTILES.md (projectielpool, sjabloon 0, §2.4 meegedragen instantie), ROCKET.md §6-7 (Perso-toestand 8,
bomkanon), ENEMY2.md §4 (bommengooier type 12, terugwerpen), BOSS14.md §9 (klasse 17), PERSO_JUMP.md / PERSO_MOVE.md (Jumper, Mover, toetsen).

## 0. Samenvatting

* **Oppakken** (`0x463430`, elk frame in Perso-toestand 0): aanvalstoets (actie 6) *net ingedrukt*, op de grond, geen aanval bezig, en een bom
  met `+0x131` (in gebruik) en zonder `+0x133` (bereden) binnen **269** eenheden (3D, `P+4 + 200` = 69 + 200) van de voeten → toestand **6**.
  Geen kijkrichtingtest, geen test op de bomtoestand: ook een vliegende bom (van een lanceerder of de bommengooier) kan gevangen worden.
* **Dragen**: de bom staat elk frame op de positie van de **top-level knoop met vlaggen 0x80** (de "camera-knoop") van Woody's model in de
  huidige `.ins`-animatie (`0x42feb0`/`0x42fa40`, dezelfde track als camera-modus 0x80), en neemt Woody's rotatie over. Draaghouding: ± 117
  boven de voeten, 45 opzij, 7 voor (model 21, anim 47). Lopen en springen gaan **precies als in toestand 0** (zelfde `0x44bb20(1)` +
  `0x4624f0`); er is geen snelheidsverschil en geen springverbod. Wel: geen pikken/stormloop (de aanvaltrigger eist toestand 0).
* **Gooien**: aanval *net ingedrukt* in subtoestand 2 en niet gebukt. Op de grond: anim 0x43 (0.933 s, bewegen geblokkeerd), de bom gaat los
  **0.467 s** na de start; in de lucht: anim 0x44 (0.6 s), los na **0.3 s**. Worp = het lopende projectiel van de bom opnieuw gestart met
  richting `normalize(kijk.x, 1, kijk.z)` (45° omhoog) en snelheid **1000**, zwaartekracht van sjabloon 0 (3000 u/s²), eigenaar = Woody,
  doel = de vorige eigenaar (een teruggegooide bom van de bommengooier zoekt dus zijn werper). Vluchtafstand op vlakke grond ≈ 340.
* **Loslaten zonder te gooien** (`0x463c90`, snelheid 0, recht naar beneden): bij elke toestandswissel weg uit 6 terwijl hij de bom vast heeft
  (dood, rondkijken, klimwand, script), bij Reset, en tijdens een **lange val** (anim .ins 66).
* **Lont in de handen**: de lont loopt gewoon door (`bom+0x114 += dt`, `0x44d886`); ontploft hij, dan raakt de explosie (straal 400) Woody
  (`0x44d040`: `Kill(6)`), tenzij hij onkwetsbaar is. Treffers (`Hit`) laten de bom **niet** los.
* **Bericht 1090 `[inst, var, t]`** (`0x444e00`): pakt de eerste vrije bom uit de pool van 16, zet hem op de typecode-0-marker van `inst`
  (op de grond gezet), lont `t · 0.01` s, beginsnelheid **100** langs de marker; `var := 1` zodra hij ontploft (of meteen, als er geen vrije bom is).
* **Kisten** (type 121; type 120 komt in geen level voor): bomexplosie binnen **400** van de oorsprong van de kist → animatie 0 eenmalig,
  explosie-effect soort 1, in **2 s** uitfaden (vanaf 1.8 s niet meer botsbaar), **msgmask 0x20**. Ze verbergen een uniek item (type 36),
  een extra leven (30) of een type-35-bonus. 120 en 121 verschillen alleen in het subtype van het typewoord (1 resp. 2); geen lezer gevonden.
* **Klasse 17 is géén "breekbaar object voor bommen"**: het is de meegesleepte instantie van een baas (W1B 404 = het toestel van Buzz).
  Bommen raken hem niet. OBJECTS.md §3, BONUS.md §7 en PROJECTILES.md §1.2 hadden dit fout (§7).
* Wat een bomexplosie raakt (`0x44d650`): **alle actoren met categorie 1 of 2** (Woody en vijanden, `vtbl[40](pos, 400)`) en **alle kisten**
  (`vtbl[28](pos, 400)`). Niets anders; er zijn geen opblaasbare muren in de wereldgeometrie.

## 1. Perso-toestand 6 (bom dragen)

### 1.1 Velden

| veld | type | betekenis | schrijvers / lezers |
|---|---|---|---|
| Perso+0x590 | Bomb* | de gedragen bom (0 = geen) | `0x463502` (oppakken), `0x4638cb`, `0x463bd8`, `0x463dd6` (0), `0x44a50c` (ctor 0) |
| Perso+0x594 | u8 | "bom in de handen" (positie volgen) | `0x463512` (1), `0x4638a7`, `0x463dc7`, `0x44acaa` (0) |
| Perso+0x58c | int | subtoestand 0..8 (§1.3) | `0x463518` (0), jumptabel `0x463c28` |
| Perso+0x598 | f32 | timer van de subtoestand | `0x46390e`, `0x4639a5`, `0x463a84`, `0x463b96` |
| bom+0x131 | u8 | in gebruik (door 1090 / een werper gestart) | `0x44d575` (1), `0x44d34f` (0, Reset) |
| bom+0x132 | u8 | vastgehouden door Woody | `0x463508` (1), `0x44d3ca` (0 bij loslaten) |
| bom+0x133 | u8 | bereden (bomkanon type 21); zo'n bom kan niet opgepakt worden | ROCKET.md §7, test `0x4634ab` |
| bom+0x124 | Proj* | het lopende projectiel van de bom (PROJECTILES.md §1.2) | `0x44d5b8` |

### 1.2 Oppakken `0x463430(Perso)` (enige aanroeper `0x44bae1` in `0x44ba70`, alleen als toestand == 0)

```c
bool Perso_TryPickBomb(Perso *p) {                                   /* 0x463430 */
    if (p->state == 6) return true;                                  /* 0x463436 (dode tak: de aanroeper eist toestand 0) */
    if (!JustPressed(6) || !OnGround(p) || p->atk /*+0x5b4*/) return false;   /* 0x46344e, 0x44bcf0, 0x46346a */
    float r = p->P.radius /*+0x114 = 69*/ + 200.0f;  r *= r;         /* 0x463478..0x463492, 0x4aa164 = 200.0 */
    for (int i = 0; i < g_nbombs /*0x5e487c*/; i++) {
        Bomb *b = g_bombs[i];                                         /* 0x5e4880[16] */
        if (!b->in_use /*+0x131*/ || b->ridden /*+0x133*/) continue;  /* 0x4634a1, 0x4634ab */
        if (dist2(p->pos /*+0x1f4*/, b->pos /*+0xc*/) < r) {          /* 3D, 0x4634b5..0x4634ef */
            p->bomb = b; b->held = 1;                                 /* 0x463502, 0x463508 */
            p->carrying = 1; p->bsub = 0;                             /* 0x463512, 0x463518 */
            SetState(p, 6);  return true;                             /* 0x463522 */
        }
    }
    return false;
}
```
Het resultaat wordt genegeerd. Direct daarna roept `0x44ba70` de aanvaltrigger `0x457330` aan, die toestand 0 eist (`0x4573ad`): dezelfde
toetsdruk start dus geen pik. Let op: `0x464ef0` (vastpikken aan een klimwand, loopt eerder in het frame) heeft aan het begin geen
toestandstest (`0x464ef0..0x464f5f` gelezen): staat Woody met zijn snavel naar een pikbare wand (straal 169), dan wint de klimwand.

Geen test op de bomtoestand: ook een bom in toestand 6 (ontploft, 0.5 s uitfaden, `+0x131` is dan nog 1) kan worden "opgepakt"; zijn
projectiel is dan al weg (`+0x124 = 0`, `0x44d7ca`) en gooien doet niets (`0x44d3bd`). Randgeval, onzeker of dat in de praktijk voorkomt.

### 1.3 Per frame: `0x463530(Perso)`

Aangeroepen via `0x44b480` (alleen toestand 6 en `Perso+0x690 == 0`) vanuit de hoofdlus op **`0x401d69`**, ná de animatie/matrices van de
Perso-instantie (`0x401d5e`) en vóór de instantie-updates `0x42b400` en de bom-updates `0x44d820` (PERSO_FRAME.md §1, stap 16).

**Deel A: de bom in de handen** (alleen als `+0x594`):

```c
int ins = p->inst.slot0;  /* +0xb0: huidige .ins-animatie */       bool release = false;
switch (ins) {                                                       /* jumptabel 0x463bfc, index ins - 0x3d */
case 61: release = p->bt < 0.46667f; break;   /* 0x4635cb, 0x4ab7b0   grondworp (log. 0x43) */
case 62: release = p->bt < 0.3f;     break;   /* 0x4636d5, 0x4aab98   luchtworp (0x44) */
case 65: release = p->bt < 1.9f;     break;   /* 0x463595, 0x4ab7ac   log. 0x4d: dood soort 1 in toestand 6 - dode code (§1.8) */
case 66: release = p->bt < 0.16667f; break;   /* 0x4635b0, 0x4ab7a8   log. 0x4b: lange val */
case 69: case 70: release = p->bt < 0.46667f; break;   /* 0x46357a       log. 0x27/0x28: dood in toestand 6 - dode code */
case 71: release = p->bt < 0.23333f; break;   /* 0x4636c4, 0x4ab7a4   neerzetten (0x46) - onbereikbaar, §1.3 deel B */
}                                                                    /* 63, 64, 67, 68 en alle andere: vasthouden */
if (release) { Throw(p); continue_with_part_B; }                    /* 0x4635e2, hieronder */
else if (Inst_HasCameraTrack(&p->inst, ins)) {                       /* 0x42feb0: top-level knoop flags 0x80 heeft een pos-track voor ins */
    Vec3 hand;  Inst_CameraEval(&p->inst, &view, &hand, NULL);       /* 0x42fa40 -> 0x42fa80 (CINEMATIC.md §4): track-positie * instantiematrix */
    b->pos = hand;                                                   /* 0x46371c..0x463736 */
    if (b->proj) b->proj->pos = hand;                                /* 0x46374c..0x463776: het (bevroren) projectiel volgt */
    Bomb_Recell(b, hand + (0, 30, 0));                               /* 0x463c50: 0x4077f0, alleen als bom+0x1c != -1 (0x4a9740 = 30.0) */
    b->rot = p->rot;                                                 /* 0x463794: 9 floats +0x28 */
    b->clockFrame /*+0x58*/ = -1;  b->vtbl[2](1);                    /* klok/matrices dit frame opnieuw */
}                                                                    /* geen track: de bom blijft waar hij stond */
```

De knoop 0x80 heeft in Woody's model (W2A model 21, knopen 141 = 0x80 en 142 = 0x180) posities voor alle draag-animaties (46, 47, 49, 50,
53..60, 61, 62, 64..71). Enkele lokale waarden (modelruimte: +z = omhoog, −y = vooruit, zie `ins_root_at` in `src/level.c`):

| .ins | gebruik | lokale positie van de bom |
|---|---|---|
| 46 frame 0 → einde | oppakken | (0, −100, 0) = 100 vóór hem op de grond → (−53, 63, 19) … eindigt op de draagpositie |
| 47 | stilstaan met bom | ≈ (−45, −7, 117) (licht wiegend) |
| 49 | lopen met bom | (−44..−52, −12..−54, 95..115) |
| 60 | gebukt met bom | (−53, −69, 12) = voor zijn voeten op de grond |
| 61 (sleutel 280 = losmoment) | grondworp | (−45,−7,117) → achter hem (−15, 85, 82) … (−62, −8, 89) |
| 62 (sleutel 179) | luchtworp | … (−53, 51, 168) |

**Worp** (`0x4635e2..0x4638cb`):

```c
ProjT T = ProjT_Default();                                   /* 0x4635e2..0x46368d = de waarden van 0x44a260 */
Vec3 dir = (0, -1, 0);                                       /* 0x4635e4: standaard recht naar beneden */
if (b->proj) T = b->proj->T;                                 /* 0x4636b1: kopie van P+0x48 (het blok waarmee de bom gestart is) */
else         CopyTemplate(0, &T);                            /* 0x4637bb: sjabloon 0 (in de praktijk niet: zonder projectiel doet 0x44d3a0 niets) */
T.speed = 0;                                                 /* 0x4637c9 */
if (p->bsub == 4 || p->bsub == 6) {                          /* gooien (grond / lucht) */
    Vec3 f = Mover_GetDir(&p->M);                            /* 0x445780 */
    dir = normalize((f.x, 1.0f, f.z));                       /* 0x46380f: y = 1.0 vóór het normaliseren => 45 graden bij een eenheids-kijkrichting */
    T.speed = 1000.0f;                                       /* 0x46384e, 0x447a0000 */
}
T.pos = b->pos;  T.dir0 = dir;                               /* 0x463856..0x463894 */
T.target = T.owner;                                          /* 0x463894: het doel wordt de VORIGE eigenaar (0 voor een 1090-bom, de werper voor een gevangen bom) */
T.owner  = p;                                                /* 0x463899 */
Bomb_Launch(b, &T);                                          /* 0x44d3a0: b->held = 0, T.pos = b->pos, Proj_Reinit(b->proj, &T) (0x4492d0, geen geluid/visual) */
p->carrying = 0;  Bomb_Recell(b, b->pos + (0,30,0));         /* 0x4638a7, 0x463c50 */
SetState(p, 0);  p->bomb = NULL;                             /* 0x4638c6, 0x4638cb: meteen, ook al loopt de werpanimatie nog */
```

Alles wat niet overschreven wordt komt uit het lopende blok van de bom. Voor een 1090-bom is dat sjabloon 0 (PROJECTILES.md §1.1: straal 30,
**zwaartekracht 15 → 3000 u/s²**, demping 0.95 op de grond / 0.99 in de lucht per 1/60 s, onbeperkt stuiteren, `hits_all = 0` = raakt alleen
categorie 2 met subtype 8 of 12, geen visual) met levensduur = de lont (loopt niet af zolang er een meegedragen instantie is, PROJECTILES §2.2).
Omdat de eigenaar nu de Perso is slaat de treffertest `0x44a0a0` Woody over; raakt de bom een vijand van subtype 8 (type 12, de bommengooier)
of 12 (type 15), dan ontploft hij meteen (PROJECTILES §2.5, ENEMY2.md §4.3). Met `T.target` = de werper stuurt een teruggegooide bom 2 s lang in
xz bij naar zijn werper (sjabloon 0: stuurfactor 0.025, `T+0x48` = 2 s, geen verticaal sturen) — **onzeker** hoe sterk dat in de praktijk is.

Vluchtbaan (eigen simulatie met de constanten hierboven, 60 Hz, vlakke grond, zonder stuiteren; onzeker ±10 %): start ≈ 89 hoog, top ≈ 170,
landt na ≈ 0.57 s op ≈ **340** eenheden; daarna stuitert/rolt hij verder (BOMB.md). Bij de luchtworp (start ≈ 170 boven de voeten) ≈ 390.

**Deel B: subtoestanden** `+0x58c` (jumptabel `0x463c28`), elk frame ná deel A:

| sub | code | wat | naar |
|---|---|---|---|
| 0 | `0x4638eb` | `bt = AnimLen(0x45, 0)` (**1.2 s**), `LockMove(bt)` (`0x44cce0(t, 0)`: niet lopen) | 1 (valt door) |
| 1 | `0x46391c` | `bt -= dt`; `bt ≤ 0` | 2 |
| 2 | `0x46394e` | **dragen**. Aanval net ingedrukt (`0x467420(6)`) en niet gebukt (`+0x694 == 0`) | 3 op de grond, 5 in de lucht (`0x463978..0x463981`) |
| 3 | `0x4639b0` | ruimtetest: bol r 60 (`0x42700000`) op `pos + kijk·10` (xz), `y = pos.y + 193·schaal` (`0x4624c0` = `P+8 · inst+0x54`). Vrij → `bt = AnimLen(0x43)` (**0.933 s**), `LockMove(bt)` | 4; bezet → 2 |
| 4 | `0x463aa2` | `bt -= dt`; `bt ≤ 0` → `SetState(0)`, `bomb = 0` | (0) |
| 5 | `0x46398c` | `bt = AnimLen(0x44)` (**0.6 s**), geen LockMove | 6 |
| 6 | `0x463aa2` | als 4 | (0) |
| 7 | `0x463aca` | neerzetten: bol r 24 (`0x41c00000`) op `pos + kijk·140` (`0x4aa1b4`), `y + 60` (`0x4ab284`); vrij → `bt = AnimLen(0x46)` (1.0 s), LockMove | 8; bezet → 2 |
| 8 | `0x463bb0` | `bt -= dt`; `bt ≤ 0` → `SetState(0)`, `bomb = 0` | (0) |

* De bol-test `0x434820(r)` + `0x434830(&pt, −1)` is in deze build een **stomp**: `0x434830` zet alleen `[0x53a560] = [0x53a55c] = [0x53a554] = 0`
  en keert terug. Het resultaat is dus altijd "vrij": sub 3 gaat altijd naar 4. (ENEMY.md r. 356 beschrijft `0x434830` als echte boltest bij
  `0x41cd68`: dat lijkt dus ook niet te kloppen — niet nagelopen.)
* **Sub 7/8 (neerzetten) is onbereikbaar**: niemand schrijft `+0x58c = 7` (alle schrijvers staan in de tabel van §1.1; `0x463981` schrijft 3 of 5).
  Neerzetten zonder te gooien bestaat dus niet als speleractie.
* Na het losmoment is de toestand al 0. De rest van de werpanimatie speelt door omdat LockMove nog loopt en de werpanimatie (prio 1500,
  keten `[61, 0]` resp. `[62, 7]`) niet door stilstaan (prio 1100) onderbroken wordt. Sub 4/6 lopen in toestand 0 niet meer (`0x44b480`).
* Landt Woody tijdens een luchtworp vóór het losmoment, dan vraagt de animatiekeuze in sub 6 op de grond anim **0x40** (`0x464764[6]` → `0x464752`)
  in plaats van 0x44: .ins 62 stopt, er komt geen losmoment, en na `bt` zet sub 6 toestand 0 ⇒ `SetState` laat de bom vallen (§1.6). Onzeker of
  dat ooit merkbaar is (de luchtworp duurt maar 0.3 s tot het losmoment).

### 1.4 Animaties (`0x4646b0` = toestand 6 in `0x463e60`; tabel `0x4b6180`, alle snelheid 3.0)

Duur = `.ins`-duur / 3 (Woody-model W2A model 21; W1A gaf in ROCKET.md dezelfde duren voor andere animaties).

| log. | keten (.ins) | prio | duur sub 0 | wanneer (adres) |
|---|---|---|---|---|
| **0x45** | 46 → 47 | 1500 | 3.6/3 = **1.2 s** | sub 0/1 op de grond: oppakken (`0x4646df`) |
| **0x40** | 47 (lus) | 1100 | 4.0/3 = 1.333 s | stilstaan met bom (`0x464758`; Mover-fase 0 via `0x463f40(1)` of sub 5/6/7 op de grond) |
| **0x41** | 50 → 49 | 1501 | 0.5/3 = 0.167 s | aanlopen met bom (Mover-fase 1, `0x463f86`) |
| **0x42** | 49 (lus) | 1500 | 1.8/3 = **0.6 s** | lopen met bom (Mover-fase 2, `0x46401a`); cyclusduur `len(0x42) / max(0.5, clamp(v/vmax))` zoals anim 3 (`+0x4a0`, `0x44ae00`) |
| **0x43** | 61 → 0 | 1500 | 2.8/3 = **0.933 s** | grondworp (sub 4, `0x4646f9`) |
| **0x44** | 62 → 7 | 1500 | 1.8/3 = **0.6 s** | luchtworp (sub 6 in de lucht, `0x46473f`) |
| 0x46 | 71 → 0 | 1500 | 3.0/3 = 1.0 s | neerzetten (sub 8, `0x46472b`) — onbereikbaar |
| 0x47 | 53 → 54 | 1501 | 1.8/3 = 0.6 s | lucht: stijgen en top (Jumper 0/1/7; `0x464333`, `ebx = ebp = 0x47`) |
| 0x49 | 55 → 56 | 5000 | 0.8/3 = 0.267 s | lucht: van een rand gevallen (Jumper 3 met `J+0x4d`) |
| 0x4a | 57 → 47 | 1500 | 1.6/3 = 0.533 s | lucht: afgekapte sprong (Jumper 3 met `J+0x4f`) én landing (Jumper 4 op de grond) |
| **0x4b** | 56 → 66 → 33 | 1600 | 0.1/3, dan 0.8/3 | lucht: **lange val** (Jumper 5) ⇒ .ins 66 ⇒ **bom valt** (deel A) |
| 0x4c | 47 (lus) | 5200 | – | harde landing (Jumper 6 met harde val) |
| 0x4e / 0x4f / 0x50 | 58→60 / 60 / 59→47 | 1750 | – | bukken in / houden / opstaan met bom (`0x465bc0`, `0x465c54`, `0x465d2d`, alleen als toestand == 6) |
| 0x21 / 0x22 / 0x24 | 68→47 / 67→47 / 64→60 | 5110 | – | geraakt met bom: grond / lucht / gebukt (`0x464b70`, zet ook `+0x58c = 2`) |
| 0x27 / 0x28 / 0x4d | 69→30 / 70→30 / 65→33 | 6000 | – | dood met bom (`0x46481b`, `0x464840`, `0x46493e`): **dode code**, `Kill` zet eerst toestand 2 |
| 0x48 | 55 → 56 | 1000 | – | geen aanvraag gevonden |

De lucht-set is de gewone `0x4642f0` met andere nummers (`0x464333..0x46435c` tegen `0x46435e..0x464388`): 4→0x47, 5→0x47, 6→0x4a, 7→0x49,
8→0x4a, 9→0x4b, 0xa→0x4c. Geluid bij het oppakken en gooien komt uitsluitend uit de **event-tracks** van de animaties (knoop 1: .ins 46 geluid
29/30 op frame 6, .ins 61 en 62 geluid 31 op frame 0 — bank 0-refs, SOUND.md §1); `0x463430`/`0x463530` zelf roepen geen `0x468a00` aan.

### 1.5 Bewegen, springen, bukken, camera

* Toestand 6 draait `0x44bb20(1)` + `0x4624f0`, net als toestand 0 (PERSO_MOVE.md §5). In de Mover (`0x45b110`), de Jumper (`0x462d70`) en
  `0x44bb20` staat geen enkele lezing van `+0x21c` (grep over alle `+0x21c`-lezers): **zelfde loopsnelheid (600), zelfde sprong, zelfde
  valschade**. Alleen tijdens oppakken (1.2 s) en de grondworp (0.933 s) is bewegen geblokkeerd (LockMove).
* Geen pik/stormloop/luchtaanval: `0x457330` eist toestand 0 (`0x4573ad`), `0x44ba70` roept de trigger wel aan.
* Bukken mag (`0x465b10`, actie 5, anims 0x4e/0x4f/0x50); gebukt gooien niet (`+0x694 == 0`-test op `0x463963`).
* Rondkijken (actie 7, `0x44b980`) mag in toestand 6 met `+0x58c == 2` (`0x44b9e0..0x44b9ee`): `SetState(3)` ⇒ **de bom valt** (§1.6). Na het
  rondkijken zet `0x44c9f0` de vorige toestand (6) terug, maar zonder bom (`+0x590 = 0`, `+0x594 = 0`): Woody loopt dan in draaganimaties
  zonder bom tot hij op aanval drukt (sub 2 → 3 → 4 → toestand 0). Vermoedelijk een bug in het origineel; niet in het origineel nagespeeld.
* Camera: geen speciale behandeling. De behind-modus van de volgcamera geldt voor toestanden 1, 4 en 8 (`0x4591ec..0x4591fd`), niet 6.
  Andere camerapaden niet op toestand 6 nagelopen (onzeker, maar er is geen `cmp …, 6` op `+0x21c` buiten de vijf plekken van §1.6).

### 1.6 Loslaten zonder gooien: `0x463c90(Perso)`

```c
void Perso_DropBomb(Perso *p) {                               /* 0x463c90 */
    ProjT T = ProjT_Default();                                /* 0x463cc7..0x463d3f */
    if (p->bomb->proj) T = p->bomb->proj->T; else CopyTemplate(0, &T);
    T.speed = 0;  T.pos = p->bomb->pos;  T.dir0 = (0, -1, 0);  /* 0x463d7e, 0x43ff80(0,-1,0) */
    T.owner = 0;  T.target = 0;                               /* 0x463db8, 0x463dbc */
    Bomb_Launch(p->bomb, &T);                                 /* 0x44d3a0: held = 0 */
    p->carrying = 0;  Bomb_Recell(...);  p->bomb = NULL;      /* 0x463dc7, 0x463dcd, 0x463dd6 */
}
```
Aanroepers (alle vijf `cmp [..+0x21c], 6`-plekken zijn gecontroleerd):

| waar | voorwaarde | typisch |
|---|---|---|
| `0x44c9ad` in **SetState** `0x44c980` | `bomb && state == 6 && nieuw != 6 && bomb->held` | dood (`Kill` → 2), rondkijken (3), klimwand (4), gescripte actie (5), teleport enz. |
| `0x44acb4` in **Reset** `0x44ab20` | `bomb != 0` (ook `+0x594 = 0`, `0x44acaa`) | respawn, levelwissel |
| `0x44b8dc` in **Perso::Update** `0x44b530` | `bomb && state != 6` | vangnet na `0x44c9f0` (toestand terugzetten zonder SetState) |

Loslaten "via deel A" (snelheid 0, `dir (0,−1,0)`, want sub is dan 2) gebeurt ook bij .ins 66 (**lange val**); daarna `SetState(0)`.
Na het loslaten valt de bom met de zwaartekracht van zijn blok (3000 u/s²) en stuitert (BOMB.md).

### 1.7 Geraakt worden met een bom

`Hit` (`0x44ca00`, PERSO_MOVE.md §4.4) verandert toestand 6 niet (alleen 4 → 0). De hit-animatie `0x464b70` kiest in toestand 6 met `+0x594`
0x21/0x22/0x24 (`0x464b7c..0x464bd6`) en zet **`+0x58c = 2`**: een lopende worp vóór het losmoment wordt afgebroken (de .ins wordt 68/67/64,
geen losmoment), een lopende oppak-animatie ook — Woody houdt de bom. Knockback en `health −= schade` gewoon.

### 1.8 De lont loopt af in zijn handen / dood met bom

* De bom telt `+0x114 += dt` elke frame, ook als hij vastgehouden wordt (`0x44d875..0x44d88c`); vastgehouden of bereden ⇒ `inst+8 |= 0x40`
  (niet botsbaar, `0x44d8a5`). De projectiel-update doet niets zolang `held` (PROJECTILES.md §2.2: `carried->byte[0x132]` ⇒ return).
* Ontploffing `0x44d6e0`: `var := 1` (`0x44d70a`), `0x44d650` (§4.1) roept de Perso aan via `vtbl[40]` = `0x44d040(pos, 400)`: als
  `|pos − Perso.inst+0xc|² < 400²` (`0x44d05c..0x44d097`): `Hit(0, 0, weg_xz, 0, 0)` (`0x44d118`: knockback, 0 schade), rumble (`0x44d12e`) en
  **`Kill(6)`** (`0x44d139`). De bom zit ≈ 125 boven zijn voeten, dus altijd binnen 400. `Kill(6)` wordt genegeerd als `+0x270 > 0`
  (1 s na respawn); de treffer ook bij `+0x280 > 0`.
* `Kill` → `SetState(2)` → `0x463c90` (want `held` staat nog op 1): het projectiel van de bom wordt nog even herstart (0x44d3a0), daarna door
  `0x44d7c2` gedeactiveerd. Netto: bom weg, Woody dood soort 6 (anim 0x2b, fade na 2.5 s).
* Een dood met bom (elke soort) laat de bom vallen; de "dood met bom"-animaties 0x27/0x28/0x4d zijn dode code (`Kill` zet toestand 2 vóór
  `0x464790` naar `+0x21c == 6` kijkt; ook PERSO_DEATH.md §4).
* Wie de bom op tijd weggooit en > 400 van het ontploffingspunt staat merkt niets. Na een worp van ≈ 340 plus stuiteren is dat krap: wegrennen.

## 2. Bericht 1090: de bommenautomaat (`0x444e00`, game-berichten `0x444870`)

`1090 [inst, var, t]` (argumenten `[esi+8]`, `[esi+0xc]`, `[esi+0x10]`):

```c
case 1090: {
    Instance *src = world->inst[arg0 & 0xffffff];              /* 0x444e0f, [0x50944c]+0x6c */
    uint32_t var = arg1 & 0xffffff;                            /* 0x444e22 */
    float life = arg2 * 0.01f;                                 /* 0x444e00, 0x4aa0ac = 0.01 */
    ProjT T = ProjT_Default();  CopyTemplate(0, &T);           /* 0x444e28..0x444eec: sjabloon 0 */
    T.life = life;                                             /* 0x444efc: T+0x2c */
    Vec3 v[2];
    if (Inst_GetVector(src, /*typecode*/0, v, /*n*/0)) {       /* 0x42f6b0 */
        T.pos = v[0];  T.dir0 = normalize(v[1] - v[0]);  T.speed = 100.0f;   /* 0x444f15..0x444faf, 0x42c80000 */
    } else {
        T.pos = src->pos;  T.dir0 = (0, -1, 0);  T.speed = 0;    /* 0x444fc7..0x444fff */
    }
    Bomb_Spawn(&T, /*snap*/1, var, /*kind*/0);                 /* 0x444fba -> 0x44d5d0 */
}
```

`0x44d5d0(T*, snap, var, kind)`: eerste bom met `+0x131 == 0` uit `0x5e4880[0x5e487c]`; geen ⇒ waarschuwing
**`'Pas de bombes, ou plus assez de bombes dans ce niveau...'`** (`0x4b3ad4`, via `0x462c60` = lege log-functie) en **`SetVar(var, 1)`**
(`0x44d612`): het script denkt dan dat de bom al ontploft is en de automaat is meteen weer bruikbaar. Anders `0x44d4d0`:

| veld | waarde | adres |
|---|---|---|
| `+0x10c` lont | `T.life` = `t · 0.01` s (W2A: **20 s**) | `0x44d4dd` |
| `+0x110` waarschuwingsduur | `min([0x5e48c4] = 2.0, lont)` | `0x44d4e3..0x44d508`, ctor `0x44d28b` |
| positie | `T.pos`; met `snap`: `GetHeight(pos)` (`0x435650`) ⇒ `y = grond + 1.0` | `0x44d52c..0x44d545` |
| `+0x60` | = positie, her-cellen (`0x4077f0`) ⇒ **zichtbaar** | `0x44d548..0x44d55c` |
| `+0x12c` scriptvar | `var` (−1 = geen) | `0x44d569` |
| `+0x128` explosiesoort | `kind` = 0 voor 1090, 1 voor het kanon | `0x44d5a4` |
| `+0x131/+0x132/+0x133` | 1 / 0 / 0 | `0x44d575..0x44d582` |
| `+0x108` toestand | 1; `+0x114 = +0x118 = +0x11c = +0x120 = 0` | `0x44d56f..0x44d59e` |
| projectiel | `T.carried = bom`; `+0x124 = 0x4490a0(&T)`; pool vol ⇒ `vtbl[17]` Reset (bom weer weg) | `0x44d5ab..0x44d5c4` |

Er is **geen geluid of effect** in 1090 zelf; het script speelt het geluid en de animatie van de automaat (§5). De bom rolt/stuitert met
100 u/s uit de automaat (sjabloon-0-fysica, BOMB.md). Het script hoort het einde via de variabele: `0x44d6e0` zet `var := 1` bij de
**ontploffing** (`0x44d707`), niet bij het oppakken of gooien.

De 16 type-40-instanties van een level zijn alleen de **pool**: 1200 `[inst, 40]` → Init `0x44d2e0` + Reset `0x44d320` (uit de wereld,
`0x44d361`). Hun `.ins`-positie is een opslagplek (W2A: y = 2289 / 2953 bij x ≈ 2100..2660) en speelt geen rol.

## 3. Kisten, types 120 / 121 ("Exploding objects (as Chest)")

### 3.1 Klasse

* Fabriek: type 120 → `0x403dc4` (alloc 0x108, ctor `0x451650`, vtable **`0x4aafd0`**); type 121 → `0x403e02` (zelfde ctor, daarna
  `mov [esi], 0x4a9034`, **vtable `0x4a9034`**).
* Ctor `0x451650`: Instance-ctor, `+0x104 = 0`, in lijst `0x5e581c[0x5e58b0]`, max **32** (`'Too much Exploding objects (as Chest), max is %d'`,
  `0x4b3cf4`). Dtor `0x4516f0` zet de teller op 0.
* Verschil 120/121 (vtables slot voor slot vergeleken): alleen slot 0 (dtor `0x4516d0` / `0x404070` → beide `0x4516f0`) en **slot 1 Init**:
  `0x451710` zet typewoord `0x29` (categorie 9, subtype 1), `0x451840` zet `0x49` (categorie 9, subtype 2); beide eerst de FadeInst-Init
  `0x44e7c0` (fade 0, snelheid 100). Er is **geen lezer** van categorie 9 of van subtype 1/2 gevonden (alle `0x40c340`/`0x40c350`-aanroepers en alle
  `and …, 0x3e0`-maskers nagelopen): gedrag identiek. Type 120 wordt in geen enkel level gebruikt.
* Vtable (beide): `[3]` Update `0x40bf10` = `jmp 0x44e810` (fade-update, elke frame), `[17]` Reset `0x451730`, `[22]` handler `0x451820`,
  `[28]` `0x451770` = raak-test, `[29]` `0x4517d0` = openen.

### 3.2 Openen

```c
void Chest_Blast(Chest *c, Vec3 *pos, float r) {             /* vtbl[28] 0x451770, aangeroepen door 0x44d650 met r = 400 */
    if (MsgTest(c->id, 0x20)) return;                        /* 0x443ed0: al open */
    if (dist2(*pos, c->pos /*+0xc*/) < r*r) c->vtbl[29]();   /* 3D, vanaf de OORSPRONG van de instantie, niet de geometrie */
}
void Chest_Open(Chest *c) {                                  /* 0x4517d0 */
    Inst_PlayOnce(c, 1.0f, 0, -1, -1, -1);                   /* 0x436ca0: slot0 = anim 0, start = nu, snelheid 1.0·3 (0x4a988c), eenmalig */
    Effect_Explosion(1, &c->pos, 0);                         /* 0x477060 soort 1 (ROCKET.md §5.3): flitsen R 1400 en 400, deeltjes */
    c->fadeSpeed = 0.5f;  SetFade(c, 1.0f, 0);               /* 0x4517f8, 0x44e7f0: doel 1.0 (onzichtbaar), niet direct */
    MsgSet(c->id, 0x20);                                     /* 0x443e50: het script ziet MSGTEST 32 */
}
```
* Fade `0x44e810` (INSTANCE.md §5): `+0x6c` loopt met 0.5/s van 0 naar 1 ⇒ **na 1.8 s** voorbij 0.9 ⇒ `inst+8 |= 0x40` = niet meer botsbaar;
  na 2.0 s volledig doorzichtig. De instantie blijft in zijn cel (geen `0x407850`).
* **Geen eigen geluid** (geen `0x468a00` in `0x4517d0`); het ontploffingsgeluid is dat van de bom (SoundFx 6, `0x44d72e`).
* Model 48 (W2A 537) heeft één animatie van 4.8 s ⇒ het openbreken duurt 1.6 s. Vóór het openen staat de klok stil: Reset → `0x42e250`
  (snelheid 0, slot0 = 0, slots 1..3 = −1).

### 3.3 Berichten en reset

Handler `0x451820` (gedeeld met de bom): **29** ⇒ `vtbl[17]` (`0x451832`) = Reset `0x451730`: `0x42e250` (anim stil op frame 0),
her-cellen `0x4077f0(0)`, fade-snelheid 100 en doel 0 (dus in 0.01 s weer zichtbaar en botsbaar), **msgmask 0x20 wissen** (`0x45175c`).
Alle andere berichten → FadeInst-handler `0x44e8f0` (56 = fade-doel ×0.01, 57 = fade-snelheid ×0.01, rest → Instance `0x42d5e0`).
In de levels krijgen kisten alleen 1200 (en W2B test daarna MSGTEST 32); nooit 29.

## 4. Wat een bomexplosie raakt, en klasse 17

### 4.1 `0x44d650(bom)` — de enige schade-uitdeler van een bom

```c
for (i = 0; i < [0x4c5318]; i++) {                            /* actorlijst 0x4c4e00 (Npc's) */
    Actor *a = list[i];
    if (Category(a) == 2 || Category(a) == 1) a->vtbl[40](&bom->pos, 400.0f);   /* 0x40c340; 0x43c80000 */
}
for (i = 0; i < [0x5e58b0]; i++) chests[i]->vtbl[28](&bom->pos, 400.0f);         /* 0x44d6b3..0x44d6c9 */
```
* Categorie 1 = de Perso (`0x44d040`, §1.8). Categorie 2 = vijanden: basis `0x41ae20` (binnen r ⇒ dood, ENEMY.md), type 12 `0x4119b0`
  (1 hp per explosie, ENEMY2.md §4.4), Buzz (BOSS14.md §8).
* Géén test op zichtlijn of muren: 400 in 3D, door alles heen.
* BONUS.md §7 schreef "alle type-17-objecten met toestand 1/2": fout, het is categorie 1/2 (`0x40c340` = `typewoord & 0x1f`).

### 4.2 Klasse 17 (ctor `0x40c3d0`, vtable `0x4a95dc`, 0x118 B)

De vtable heeft **28 slots** (slot 28 is al de float 400.0 in `.rdata`); er is dus geen `vtbl[40]`, en klasse 17 staat niet in de actorlijst.
Bommen doen er niets mee. Wat het wel is staat in BOSS14.md §9.2: een door een baas meegesleepte instantie (bericht 59 koppelt hem;
de baas zet elke frame positie en rotatie, `0x40c5a0` zet de eigenaar `+0x110` voor de renderkleur `[26]` `0x40c5c0`).

| veld | betekenis | adres |
|---|---|---|
| `+0x108` u8 | "gereset" (BOSS14 §8: voorwaarde voor bericht 59) | Init `0x40c450` (0), Reset `0x40c491` (1) |
| `+0x10c` | rook-emitter (0x34 B, lijst `[0x5e8564]`, dezelfde uitlaat-lijst als de raket) over alle markers met **typecode 9**, toestand 2 = aan | Reset `0x40c4ab..0x40c548`, `0x40c563` |
| `+0x110` | eigenaar (baas) | `0x40c5a8`, Init/Reset 0 |
| `+0x114` | 1 = geen rook (ctor `0x40c3f8`); bericht **63 `[inst, v]`** ⇒ `+0x114 = v`, Reset (`0x40c5f2`) | |

Reset `0x40c460` maakt de rook alleen als `+0x114 == 0`. W2D (746) en W3D (776) sturen `63 [inst, 1]`, niemand stuurt 0 ⇒ de rook van
klasse 17 wordt in geen enkel level gebruikt. Gebruik: W1B 404, W2D 746, W3D 776, WWS 363 — telkens naast een baas.

De "0x40cb80-familie" die `0x4490a0` aanroept op `0x40d128` hoort **niet** bij klasse 17: `0x40cb80` staat op `0x4a9728` = slot 52 van de
vtable `0x4a9658` van **type 16** (baas, ctor `0x40c730`) en vuurt drie gewone projectielen in een waaier (`0x40d0a0..0x40d134`, lus van 3).

## 5. Gebruik in de levels (`tools/ekodisasm.py` over alle 28 `extract/Data/<LVL>/code`)

| level | pool type 40 | automaat: script-obj → 1090 `[inst, var, t]` | trigger | kist 121 (verbergt) |
|---|---|---|---|---|
| **W2A** | 16 (187..194, 318..325) | 536 → `[538, 190, 2000]` (20 s) | volume 233 + aanval los | 537 model 48 (13400, 256, −16847) → **539 type 36** |
| K2A | 16 | 540 → `[541, 171, 2000]` (20 s) | volume 237 + aanval los | 542 model 46 (11224, 999, −23352) → 543 type 36 |
| S2A | 16 | 336 → `[384, 15, 2000]` (20 s) | volume 109 + aanval los | 528, 529 model 47 → 527 type 36 en 530 type 30 |
| W2B | 15 | 509 → `[508, 172, 1000]` (10 s) | volume 200 + aanval los + 1042 `[510, 500, 60]` | 506, 507, 545 model 49 (≈ (7300..7900, 204, 8500..8640)); 523 model 49 (790, 876, −5370) → 525 type 35 |
| KWS / SWS / WWS | 1 (244 / 244 / 252) | 284/288/321 → `[282/286/319, …, 1000]` (10 s) | volume 38/39/68 + aanval los + 1042 `[283/287/320, 500, 90]` | – |
| S2R | 8 | – | – | – |
| W2D / W3D | 8 / 16 | – (bommen van het kanon / de bazen) | | – |

* Type 120: nergens. Type 17: W1B 404, W2D 746, W3D 776, WWS 363 (§4.2).
* MSGTEST 32 (kist open) staat alleen in W2B: 506 ⇒ var 55 := 1, 507 ⇒ var 56 := 1, 523 ⇒ var 180 := 1, `6 [525, 1]` (bonus 525 tonen),
  var 166 := 1. W2A, K2A en S2A testen de kist niet: het item erin wordt bereikbaar doordat de kist na 1.8 s niet meer botst.
* De hub-automaten (KWS/SWS/WWS) hebben één bom van 10 s en geen kist; waarvoor die bom dient (baas modus 2 in WWS?) is niet uitgezocht.

## 6. W2A: de bommenpuzzel

Script-objecten (`out/w2a_code.txt`, woord 16416..16510):

```
object 536 (init):  var189 = 0; var190 = 1                        ; 190 = "automaat klaar"
object 536 (body, gewekt door var 189/190 en volume 233):
    if VOL_FLAG5(233): SEND 1050 [var189, 2]                       ; var189 = 1 als de aanvalstoets NET LOSGELATEN is (0x467440)
    if var189 == 1 && var190 == 1:
        var189 = 0; var190 = 0
        SEND 1622 [538, 0x1000007, 100]                            ; 3D-geluid 7 uit de levelbank, eenmalig, op de automaat
        SEND 3 [538, 0, 1, 200]                                    ; automaat speelt anim 0 eenmalig over 2 s
        DELAY 100 ->  SEND 1090 [538, var190, 2000]                ; na 1 s: bom met lont 20 s, var190 := 1 als hij ontploft
object 537 (init):  SEND 1200 [537, 121]                           ; de "kist"
objects 187..194, 318..325: SEND 1200 [x, 40]                      ; de bommenpool
```

* **Automaat**: instantie 538 (model 43) op (8232, 259, −17315). Triggervolume 233 hoort bij instantie 536 (model 22 = alleen een volumeknoop):
  een kubus van 400, x 8025..8425, y 274..674, z −17522..−17122. Er is geen richtingtest (geen 1042): in het volume de aanvalstoets indrukken
  en loslaten is genoeg. Het indrukken start in toestand 0 gewoon een pik (en pakt een bom op als er al een binnen 269 ligt).
* **Uitworp**: marker typecode 0 van model 43 (knoop 16, ouder = knoop 1 ≈ identiteit): in rustpose ≈ **(8217, 236, −17281)** met richting
  ≈ (−0.38, 0, 0.92) (rustpose, met de port-conventie `mat4_from_trs` gerekend, niet in het origineel gemeten: onzeker). De bom wordt op de
  grond gezet en rolt met 100 u/s die kant op.
* **Doel**: kist 537, model 48 met de oorsprong op (13400, 256, −16847). **Correctie (in de port gezien):** het is wél een schatkist, naast de palm op het
  zandeilandje; de 2200-bbox is die van meshknoop 1 over de hele open-anim (rondvliegende planken). Botsing: press-knoop 8 en hull-knoop 9, ≈ 300 × 200 × 300
  rond de oorsprong (de eerdere "hull-knoop 10 / press-knoop 9" hoort bij instantie 127, model 15, de rots ernaast); binnenin, 83 eenheden van die oorsprong, ligt **instantie 539 type 36** (uniek item, savegame-vlag,
  BONUS.md §2.3) met volume 234. Twee type-8-vijanden in de buurt (376 op (12837, 177, −16936), 364 op (12111, 181, −17024)); de eerste staat
  binnen 575 van de kist en is dus met dezelfde bom op te blazen als die dichtbij genoeg ontploft.
* **Afstand** automaat → kist ≈ 5190 (vooral +x). Woody loopt 600 u/s ⇒ minstens 8.7 s; de lont is 20 s. Gooien moet zo dat de bom binnen
  **400 van de oorsprong** (13400, 256, −16847) ontploft; gooibereik ≈ 340 + stuiteren. De speler moet daarna zelf buiten 400 van de bom staan.
* Mislukt het (bom te vroeg, te ver, in het water/de afgrond ⇒ `0x44d6e0` bij `cel < 0`): de bom ontploft, var 190 = 1 en de automaat kan
  opnieuw. Er is geen limiet.
* Het bomkanon 411 (type 21, ROCKET.md §7) op (9882, 139, −12797) staat hier los van: zijn bom komt uit dezelfde pool, maar ontploft ver van 537.

## 7. Correcties op andere documenten

1. **OBJECTS.md §3** ("17 = breekbaar object voor bommen, `vtbl[0xa0](&pos, r)`") en **TODO.md** (zelfde regel): klasse 17 heeft geen
   `vtbl[40]` en wordt door bommen niet geraakt; het is de meegesleepte baas-instantie (§4.2, BOSS14.md §9.2).
2. **BONUS.md §7**: "`0x44d650`: alle type-17-objecten met toestand 1/2 krijgen `vtable[+0xa0]`" → alle **actoren met categorie 1 of 2** uit
   `0x4c4e00` (§4.1). ROCKET.md §7 en ENEMY2.md §4.3 hadden het al goed.
3. **PROJECTILES.md §1.2**: "`0x40d128` (klasse 17-familie `0x40cb80`)" → type 16 (baas, vtable `0x4a9658` slot 52).
4. **EVENTS.md §4.1 / BONUS.md §7**: de kist test de afstand vanaf `inst+0xc` (oorsprong), en 120/121 verschillen alleen in het typewoord.
5. **ENEMY.md** (`0x41cd68`): `0x434830` is in deze build een stomp die de resultaatglobals op 0 zet; een "boltest" daar is altijd vrij (niet nagelopen
   wat dat voor de vijand betekent).
6. **GAMEFLOW.md §4.6**: `1050(var, 2)` = actie 6 net **losgelaten** (`0x467440`), niet net ingedrukt (GAMEFLOW §8 en OBJECTS §4 hebben het goed).

## 8. Recept voor de port

Voorwaarde: de bomklasse uit BOMB.md (pool van 16, `Bomb` met projectiel, lont, explosie). Hieronder alleen de interface die de speler nodig
heeft, en de speler-kant. Namen passen bij `src/player.c` / `src/main_engine.c`; alles tussen `/* */` is het adres in het origineel.
`v3_sub/v3_dot/v3_norm/v3_dist2` staan voor de gewone vectorhulpjes (in de port nu inline uitgeschreven); `ProjT`/`Proj` is het
projectielblok van PROJECTILES.md §1 zoals BOMB.md het in de port neerzet.

### 8.1 Gedeelde structuren (BOMB.md bepaalt de rest)

```c
/* src/bomb.h (of in main_engine.c naast Rocket) */
typedef struct Bomb {
    Instance *inst;
    int   state;                 /* +0x108: 0 uit, 1..5 lont, 6 ontploft (BOMB.md) */
    float fuse, warn, t;         /* +0x10c, +0x110, +0x114 */
    int   in_use, held, ridden;  /* +0x131, +0x132, +0x133 */
    uint32_t var;                /* +0x12c, 0xffffffff = geen */
    int   kind;                  /* +0x128: explosiesoort 0 (1090) of 1 (kanon) */
    Proj *proj;                  /* +0x124: projectiel met T.carried = deze bom */
} Bomb;
extern Bomb g_bombs[16]; extern int g_nbombs;                 /* 0x5e4880, 0x5e487c: gevuld door 1200 [inst, 40] */
Bomb *bomb_spawn(const ProjT *T, int snap, uint32_t var, int kind);   /* 0x44d5d0 + 0x44d4d0 */
void  bomb_launch(Bomb *b, const ProjT *T);                  /* 0x44d3a0: b->held = 0; T.pos = b->inst->position; proj_reinit(b->proj, T) */
void  bomb_explode(Bomb *b);                                  /* 0x44d6e0: var := 1, bomb_blast(), SoundFx 6, effect soort b->kind, fade */
```

### 8.2 Speler (`src/player.h` / `src/player.c`)

```c
/* Player: Perso-toestand 6 */
struct Bomb *bomb;  int carrying, bsub;  float bt;          /* +0x590, +0x594, +0x58c, +0x598 */
float throw_hold;                                           /* port: houdt 0x43/0x44 vast na het losmoment (het origineel doet dat met anim-prioriteiten) */

/* player_state_free: ook !p->bomb (toestand 6 is niet 0: geen 1042-schakelaar, geen raket, geen pik) */

static void bomb_drop(Player *p)                            /* 0x463c90 */
{
    Bomb *b = p->bomb; if (!b) return;
    ProjT T = b->proj ? b->proj->T : proj_template(0);
    T.speed = 0; T.pos = b->inst->position; T.dir0 = (Vec3){ 0, -1, 0 }; T.owner = NULL; T.target = NULL;
    bomb_launch(b, &T); p->carrying = 0; p->bomb = NULL;
}
/* "SetState": overal waar de port toestand 6 verlaat (player_kill, klimwand, rondkijken, script_action, teleport, place/reset,
 * player_mount kan niet: die eist state_free) eerst:  if (p->bomb && p->bomb->held) bomb_drop(p);   (0x44c9ad, 0x44acb4, 0x44b8dc) */

static int bomb_try_pick(Player *p, int pressed)            /* 0x463430; in player_update vóór attack_trigger, alleen als state_free */
{
    if (!pressed || !p->on_ground || p->atk) return 0;
    float r2 = (69.0f + 200.0f) * (69.0f + 200.0f);
    for (int i = 0; i < g_nbombs; i++) { Bomb *b = &g_bombs[i];
        if (!b->in_use || b->ridden) continue;
        Vec3 d = v3_sub(p->pos, b->inst->position);
        if (v3_dot(d, d) < r2) { p->bomb = b; b->held = 1; p->carrying = 1; p->bsub = 0; p->atk = 0; p->charge = 0; return 1; }
    }
    return 0;
}
/* in attack_trigger: niets doen als p->bomb (0x457330 eist toestand 0) - maar wel action_prev bijwerken */

static void bomb_substate(Player *p, int pressed, float dt)  /* 0x463530 deel B */
{
    switch (p->bsub) {
    case 0: p->bt = anim_len(p, 0x45, 0); lock_move(p, p->bt); p->bsub = 1;   /* 1.2 s */  /* fallthrough */
    case 1: if ((p->bt -= dt) <= 0) p->bsub = 2; break;
    case 2: if (pressed && !crouching /* +0x694, bukken is nog niet geport: 0 */) p->bsub = p->on_ground ? 3 : 5; break;
    case 3: p->bt = anim_len(p, 0x43, 0); lock_move(p, p->bt); p->bsub = 4; break;   /* ruimtetest 0x434830 is een stomp: altijd vrij */
    case 5: p->bt = anim_len(p, 0x44, 0); p->bsub = 6; break;
    case 4: case 6: if ((p->bt -= dt) <= 0) { if (p->bomb) bomb_drop(p); p->bsub = 0; } break;   /* SetState(0): bom nog vast => valt */
    }
}

/* animatiekeuze (0x4646b0) in het blok "animations, Perso_AnimState": vóór js == 2 enz., als p->bomb || p->throw_hold > 0 */
if (p->bomb) {
    if (js == 2 || p->on_ground) {
        if (p->bsub <= 1) want = 0x45;
        else if (p->bsub == 4) want = 0x43;
        else if (p->bsub <= 3) want = p->ramp_phase == 1 ? 0x41 : p->ramp_phase == 2 ? 0x42 /* rate zoals anim 3 */ : 0x40;
        else want = 0x40;                                       /* sub 5/6 op de grond */
    } else want = p->bsub == 6 ? 0x44 : js <= 1 || js == 7 ? 0x47 : js == 5 ? 0x4b
                : (js == 3 || js == 4) ? (p->jumper.fell_off ? 0x49 : 0x4a) : js == 6 ? (p->jumper.hard_fall ? 0x4c : 0x4a) : p->lanim;
    if (p->hit_anim_t > 0) want = p->hit_anim;                  /* 0x21 / 0x22 / 0x24 via player_hit, zie hieronder */
}
else if (p->throw_hold > 0) { p->throw_hold -= dt; want = p->lanim; }   /* na het losmoment: 0x43 / 0x44 uitspelen (toestand is al 0) */
/* bukken met bom: 0x4e / 0x4f / 0x50 i.p.v. 0x31 / 0x32 / 0x33 (zodra bukken geport is) */
/* player_hit: if (p->bomb) { p->hit_anim = crouch ? 0x24 : on_ground ? 0x21 : 0x22; p->bsub = 2; }  (0x464b70) */
```

### 8.3 Na de pose: bom in de hand en het losmoment (`src/main_engine.c`, direct na `player_update`, zoals de raket-sync op r. ~1664)

```c
void player_carry_frame(Player *p, float dt)                 /* 0x463530 deel A, frame-stap 16: na de pose van Woody */
{
    Bomb *b = p->bomb; if (!b || !p->carrying) goto sub;
    int ins = p->inst->anim; int rel = 0;
    switch (ins) { case 61: rel = p->bt < 0.46667f; break; case 62: rel = p->bt < 0.3f; break;
                   case 66: rel = p->bt < 0.16667f; break;   /* lange val: bsub is 2 => laten vallen */
                   case 71: rel = p->bt < 0.23333f; break; } /* 65/69/70 zijn dode code */
    if (rel) {
        ProjT T = b->proj ? b->proj->T : proj_template(0); Vec3 dir = { 0, -1, 0 }; T.speed = 0;
        if (p->bsub == 4 || p->bsub == 6) { Vec3 f = { sinf(p->yaw), 1.0f, cosf(p->yaw) }; dir = v3_norm(f); T.speed = 1000.0f; }
        T.pos = b->inst->position; T.dir0 = dir; T.target = T.owner; T.owner = p->inst;
        bomb_launch(b, &T); p->carrying = 0; p->bomb = NULL;           /* toestand 0; LockMove en de werpanim lopen door */
        p->throw_hold = p->bt;                                          /* port: blijf 0x43/0x44 vragen tot de keten klaar is */
    } else {
        const Model *m = p->inst->model; Vec3 hand, tgt;
        float ph = (uint32_t)ins < m->nanims && m->anims[ins].duration_s > 0 ? p->inst->anim_time / m->anims[ins].duration_s : 0;
        if (ins_camera_eval(p->inst, ins, ph, &hand, &tgt)) {           /* 0x42feb0 + 0x42fa40: knoop 0x80 (en 0x180 moet bestaan) */
            b->inst->position = hand; if (b->proj) b->proj->pos = hand;
            b->inst->quat = p->inst->quat; mat4_from_trs(&b->inst->world, hand, b->inst->quat, b->inst->scale);
            b->inst->noncollide = 1;                                    /* 0x44d8a5: vastgehouden = niet botsbaar */
        }
    }
sub:
    if (p->bomb || p->bsub == 4 || p->bsub == 6) bomb_substate(p, g_act_now[2] && !g_act_prev[2], dt);
}
```
De projectiel-update moet een vastgehouden bom overslaan (PROJECTILES §2.2: `held` ⇒ return), en de lont moet doorlopen (BOMB.md).

### 8.4 Berichten (`on_msg`, r. ~1860-2040)

```c
case 1200: ... if (in->type == 40) bomb_register(in);                  /* Init 0x44d2e0 + Reset 0x44d320: onzichtbaar tot 1090 */
               if (in->type == 120 || in->type == 121) chest_register(in);   /* 0x5e581c[32]; anim stil op frame 0 */
case 1090: if (in && m->nargs > 2) {                                   /* 0x444e00 */
        ProjT T = proj_template(0); T.life = (float)(int32_t)m->args[2] * 0.01f; Vec3 p0, d;
        if (inst_vector(in, 0, &p0, &d)) { T.pos = p0; T.dir0 = v3_norm(d); T.speed = 100.0f; }
        else { T.pos = in->position; T.dir0 = (Vec3){ 0, -1, 0 }; T.speed = 0; }
        if (!bomb_spawn(&T, 1, m->args[1], 0)) eko_set_var(vm, m->args[1], 1);   /* 'Pas de bombes...' */
    } break;
case 29: ... if (in && chest_of(in)) chest_reset(chest_of(in));        /* 0x451730 */
```

### 8.5 Explosie en kisten

```c
void bomb_blast(Vec3 c)                                                /* 0x44d650, r = 400 */
{
    if (g_player && !g_player->dead_kind && v3_dist2(g_player->inst->position, c) < 400.0f * 400.0f) {   /* 0x44d040 */
        Vec3 d = v3_sub(g_player->inst->position, c); d.y = 0; d = v3_norm_or(d, (Vec3){ 0, 0, 1 });
        player_hit(g_player, 0, d); player_kill(g_player, 6);          /* kill laat de bom vallen (8.2) */
    }
    enemies_blast(&g_enemies, c, 400.0f);                             /* vtbl[40] per klasse (ENEMY.md, ENEMY2.md §4.4, BOSS14.md) */
    for (int i = 0; i < g_nchests; i++) {                              /* vtbl[28] 0x451770 */
        Chest *k = &g_chests[i]; Instance *in = k->inst;
        if (eko_msgmask_test(g_vm, in->id, 0x20) || v3_dist2(c, in->position) >= 400.0f * 400.0f) continue;
        inst_play_once(in, 0, 3.0f, g_now);                           /* 0x436ca0(1.0) */
        game_explosion(in->position);                                  /* 0x477060 soort 1 */
        in->fade_rate = 0.5f; in->fade_target = 1.0f;                /* > 0.9 na 1.8 s => noncollide (inst_tick doet dat al) */
        eko_msgmask_set(g_vm, in->id, 0x20);
    }
}
void chest_reset(Chest *k) { Instance *in = k->inst; in->anim = 0; in->anim_time = 0; in->anim_speed = 0;
                             in->visible = 1; in->fade_rate = 100.0f; in->fade_target = 0; eko_msgmask_clear(g_vm, in->id, 0x20); }
```

### 8.6 Controle

* W2A: start bij de automaat (bv. `--pos 8225 280 -17322`), aanval indrukken en loslaten ⇒ geluid, anim van 538, na 1 s een bom bij
  ≈ (8217, 236, −17281) die langzaam wegrolt. Aanval indrukken binnen 269 ⇒ 1.2 s oppak-animatie, dan de bom boven zijn schouder; lopen en
  springen als gewoonlijk. Na 20 s zonder gooien: ontploffing in zijn handen, dood soort 6, en de automaat werkt weer.
* Aanval nog eens ⇒ werpanimatie, na 0.47 s vliegt de bom 45° omhoog weg, ≈ 340 ver. Naar (13400, 256, −16847) lopen (≈ 9 s) en daar gooien
  ⇒ 537 breekt open (1.6 s), vervaagt in 2 s en item 539 is te pakken.

## 9. Onzeker / niet nagelopen

1. De marker-uitworp van de automaat (8217, 236, −17281) is in rustpose en met de port-conventie berekend, niet in het origineel gemeten.
2. De vluchtafstand (≈ 340) is een eigen simulatie; stuiteren/uitrollen daarna (BOMB.md) niet meegerekend.
3. Of het bijsturen van een teruggegooide bom naar de werper (`T.target = oude eigenaar`) merkbaar is; de xz-begrenzing `T+0x50` vergelijkt met een
   niet-genormaliseerde snelheid (PROJECTILES §2.2).
4. Hoe de animatiecontroller (prioriteiten) precies beslist dat de werpanimatie na `SetState(0)` doorspeelt: niet gelezen (`0x436b70`); de port
   houdt hem vast met `throw_hold`.
5. `vtbl[23]` van de bom (`0x44d990`: 1 als vastgehouden, anders 8) — betekenis onbekend.
6. De rondkijk-bug (§1.5) en het pikken aan een klimwand met een bom (§1.2) zijn uit de code afgeleid, niet in het origineel nagespeeld.
7. Waarvoor de hub-automaten (KWS/SWS/WWS, 1 bom, 10 s) dienen.
