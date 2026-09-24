# PERSO_DUCK.md — bukken / plat liggen (Perso `+0x694`, `0x465b10`), hitbox, camera, geluid en port-recept

Statische analyse van `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`). Alle adressen zijn VA's; floats en tabellen zijn met een
PE-lezer uit de exe gehaald, animatieduren uit `extract/Data/W1A/W1A.ins` model 0 (Woody; W2A model 21 en K1A model 45 geven dezelfde duren).
Naamgeving als in PERSO_MOVE.md / PERSO_FRAME.md: `p` = Perso, `P` = parameterblok `p+0x110`, `M` = Mover `p+0x388`, `J` = springer `p+0x334`,
`dt` = `p+0x2f8`, `A` = animcontroller `p+0x494`, `B` = tweede controller `p+0x498` (bord, alleen toestand 1).
`AnimLen(n)` = `0x436b90(n, 0)` = `.ins-duur(rec[n].sub[0]) / rec[n].speed` (PERSO_DEATH.md kop).

Zekerheid: **zeker** = instructie voor instructie gelezen; **afgeleid** = volgt uit de gelezen code en de volgorde van de frame-stappen, maar niet in
het origineel nagespeeld; **onzeker** = niet (volledig) gelezen of niet nagemeten.

## 0. Samenvatting (wat de port moet weten)

- "Plat liggen" = **bukken**: actie 5 (standaard Spatie; in toestand 1 = race actie 8). Eén functie `0x465b10`, elke frame als voorstap van
  Perso::Update, met een sub-automaat `+0x694` (0 staan, 1 gaat liggen, 2 ligt, 3 staat op) en een timer `+0x698`.
- Start alleen **op de grond** met de toets **ingedrukt gehouden** (niet "net ingedrukt"), niet tijdens een aanval (`+0x5b4 ≠ 0`) en niet dood.
  Liggen duurt minstens `AnimLen(0x31)` = **0.375 s**, opstaan `AnimLen(0x33)` = **0.19995 s**; de toets loslaten telt pas in sub 2.
- Zolang `+0x694 ≠ 0`: elke frame `LockMove(dt)` ⇒ `Perso_Move` krijgt geen invoer en de **hele horizontale verplaatsing** (lopen, glijden,
  knockback) wordt 0. Niet draaien, niet springen, niet pikken; wel vallen, meegedragen worden door een platform en weggeduwd door actoren.
- Lichaamshoogte `+0x118` = **61** i.p.v. 193 (Woody): de botscilinder voor wanden, **projectielen**, **lasers**, de kegel van Boss14, verpletteren
  en actor-duwen wordt 61 hoog. Woody kan dus **onder schoten (> voeten+66) en lasers (> voeten+61) door bukken**. Vijand-aanvallen (Touch,
  3D-afstand van de voeten) en ontploffingen (3D-straal) houden geen rekening met de hoogte.
- Opstaan alleen als het verticale segment **voeten+61 → voeten+132** (niet +193) vrij is van wereld- en instantiepolygonen; anders blijft hij liggen.
- Camera: de volgcamera verandert **niet**; alleen het zij-aanzicht (mode 0x20) zakt naar "hoogte bij ↓" zolang de **toets** 5 is ingedrukt.
- Geluid alleen via animatie-events (ref 32 bij liggen, 33 bij opstaan). Geen code-geluid.
- Port: `PlayerInput.duck` (voorstel toets **X**), `duck_update()` na `attack_trigger()`, `player_body_height()` voor alle hoogtetesten (§5).

## 1. De automaat `0x465b10`

### 1.1 Aanroep en volgorde (zeker)

Enige aanroeper: `0x44b797` in Perso::Update `0x44b530` (PERSO_FRAME §2.1). Volgorde binnen een frame (elke stap alleen als `+0x690 == 0`):

1. `0x44b620..0x44b644`: als `+0x238 > 0` ⇒ `+0x238 -= dt` (zonder ondergrens; kan licht negatief worden).
2. Voorstappen: `0x464ef0` (klimwand vastpikken) → `0x465e50` → `0x457a50` (aanvalscontroller) → `0x44ba70` (in toestand 0 `0x463430`
   bom oppakken, dan `0x457330` aanvaltrigger) → **`0x465b10` bukken** → `0x44b980` (actie 7, rondkijken) → `0x458bf0` (actie 11, speciale aanval).
3. `0x459c70` (zij-aanzicht, als `+0x4ec`), `0x45b0a0`, dispatch op toestand: 0/6 ⇒ `Perso_Move(p, 1)` `0x44bb20`; 2/3 ⇒ `Perso_Move(p, 0)`; …
4. `Perso_MoveCollide` `0x4624f0` (begint met `0x462490` = lichaamshoogte, §3.1), …, `Perso_AnimState` `0x463e60`.

Het bukken ziet dus de aanval-toestand van **dit** frame (trigger liep net), en zijn LockMove geldt al voor de `Perso_Move` van **hetzelfde** frame.

### 1.2 Pseudo-C (zeker; jumptabel `0x465de4` = `{0x465b8e, 0x465c11, 0x465c43, 0x465d8d}`)

```c
void Perso_Duck(Perso *p)                                            /* 0x465b10 */
{
    if (p->state /*+0x21c*/ == 2 || p->atk /*+0x5b4*/ != 0) return;  /* 0x465b35, 0x465b43: dood of aanval ⇒ niets, ook geen LockMove */
    int key;
    if (p->state == 1) { if (p->raceStartT /*+0x4e4*/ > 0) return;   /* 0x465b4e: tijdens de startanimatie van de race */
                         key = Held(8); }                            /* 0x465b65 */
    else                 key = Held(5);                              /* 0x465b69; Held = 0x467400 (ingedrukt, niet "net ingedrukt") */
    switch (p->duck /*+0x694*/) {
    case 0:                                                          /* 0x465b8e */
        if (!key || !p->onGround /*+0x22c via 0x44bcf0*/) break;
        p->duck = 1;
        if      (p->state == 6) { A->Request(0x4e); p->duckT = AnimLen(0x4e); }                  /* met bom */
        else if (p->state == 1) { A->Request(0x68); B->Request(0x68); p->duckT = AnimLen(0x68); } /* race */
        else                    { A->Request(0x31); p->duckT = AnimLen(0x31); }                  /* 0x465c06: alle andere toestanden */
        break;
    case 1:                                                          /* 0x465c11 */
        if ((p->duckT -= dt) <= 0) p->duck = 2;                      /* toets wordt hier NIET gelezen */
        break;
    case 2:                                                          /* 0x465c43 */
        if      (p->state == 6) A->Request(0x4f);
        else if (p->state == 1) { A->Request(0x69); B->Request(0x69); p->duckT = AnimLen(0x69); } /* timer ongebruikt */
        else                    A->Request(0x32);                    /* elk frame opnieuw aangevraagd */
        if (key) break;                                              /* 0x465c98 */
        vec3 a = p->pos /*+0x1f4*/, b = p->pos;
        a.y += P[0x10];                   /* 61 (Woody) */           /* 0x465cd8..0x465cf5 */
        b.y += P[0x0c] - P[0x10];         /* 193 − 61 = 132 */       /* 0x465cf9..0x465d05 */
        Ray(&a, &b, -1);                                             /* 0x4359b0, §4.3 */
        if (g_hitKind /*[0x53a554]*/ != 0) break;                    /* geblokkeerd: blijft liggen, volgend frame opnieuw testen */
        if      (p->state == 6) { A->Request(0x50); p->duckT = AnimLen(0x50); }
        else if (p->state == 1) { A->Request(0x6a); B->Request(0x6a); p->duckT = AnimLen(0x6a); }
        else                    { A->Request(0x33); p->duckT = AnimLen(0x33); }
        p->duck = 3;
        break;
    case 3:                                                          /* 0x465d8d */
        if ((p->duckT -= dt) <= 0) p->duck = 0;                      /* toets wordt hier NIET gelezen */
        break;
    }
    if (p->duck != 0) LockMove(p, dt, 0);                            /* 0x465db6: 0x44cce0(+0x2f8, 0) */
}
```

Bijzonderheden (zeker):
* De switch gebruikt de waarde bij binnenkomst: de overgang 0→1 en de eerste aftel-stap vallen nooit in hetzelfde frame; de LockMove-test
  onderaan leest de **nieuwe** waarde (dus al LockMove in het startframe, en niet meer in het frame waarin 3→0 gaat).
* Sub 1 en 3 eindigen **alleen op de timer** (= duur van het eerste `.ins`-deel van de keten), niet op het einde van de animatie of op de toets.
  Sub 2 eindigt op **toets los + segment vrij**. Bij geblokkeerd segment wordt elk frame opnieuw getest zolang de toets los is.
* Timers: aftellen met `fsub dt; fcomp 0.0 (0x4a9004); test ah,0x41` ⇒ overgang bij `T ≤ 0`.
* De toestand-6-keuze gebeurt per frame: wisselt de toestand tijdens het liggen (bom opgepakt, zie §2.6), dan wisselt de set mee.

### 1.3 Ingangsvoorwaarden per situatie

| situatie | kan hij gaan liggen? | waarom |
|---|---|---|
| stilstaan / lopen / rennen (toestand 0) | **ja**, direct; hij staat **in één frame stil** (geen uitloop) | `onGround`, en LockMove nult de horizontale verplaatsing (§2.2) |
| in de lucht (springen, vallen) | nee | `+0x22c == 0`. Blijft de toets ingedrukt, dan gaat hij liggen in het eerste frame met `onGround` (landing) |
| aanval / pik-dash / stormloop / terugslag (`+0x5b4 ≠ 0`) | nee (functie wordt overgeslagen) | `0x465b43` |
| aanval opladen (toets 6 vast, `+0x5b4 == 0`) | ja; de lading loopt daarna weg (−dt in `0x457a50`) en loslaten doet niets (`0x457388`) | |
| met bom (toestand 6) | ja, anims 0x4e/0x4f/0x50 (ook tijdens oppakken/grondworp: niets test `+0x58c`) | |
| rondkijken (toestand 3), klimwand (4), scripted (5), 7, raket (8), resultaten (9) | niet uitgesloten: de automaat loopt, met de 0x31-set; alleen de toets en `onGround` beslissen. In 3 wordt `Perso_Move(p,0)` gebruikt, in 4/5/7/8/9 geen `Perso_Move` | randgeval, **afgeleid** (niet nagespeeld) |
| dood (toestand 2) | nee; `+0x694` **bevriest** op zijn waarde | `0x465b35` |
| race (toestand 1) | ja met actie 8, niet tijdens `+0x4e4 > 0` | RACE.md §4.8 (al geport) |

### 1.4 Animaties (tabel `0x4b6180`, record `{sub[4], prio, speed, restart}`; zeker)

| log. | keten (.ins) | prio | speed | restart | duur deel 0 | lus | gebruik | event-geluid (§4.2) |
|---|---|---|---|---|---|---|---|---|
| **0x31** | 24 → 25 lus | 1750 | 4.0 | 1 | 1.5/4 = **0.375 s** | 25: 5.0/4 = 1.25 s | gaan liggen (sub 0→1) | .ins 24: ref 32 |
| **0x32** | 25 lus | 1750 | 3.0 | 0 | – | 5.0/3 = 1.667 s | liggen (sub 2, elk frame) | – |
| **0x33** | 23 → 0 | 1750 | 4.0 | 1 | 0.8/4 = **0.19995 s** | (0 = idle) | opstaan (sub 2→3) | .ins 23: ref 33 |
| 0x4e | 58 → 60 lus | 1750 | 4.0 | 1 | 1.5/4 = 0.375 s | 60: 5.0 s | liggen met bom | .ins 58: ref 32 |
| 0x4f | 60 lus | 1750 | 3.0 | 0 | – | 1.667 s | liggen met bom | – |
| 0x50 | 59 → 47 | 1750 | 4.0 | 1 | 0.8/4 = 0.19995 s | 47 = draag-idle | opstaan met bom | .ins 59: ref 33 |
| 0x68/0x69/0x6a | 5→6 / 6 / 7→0 (race-model) | 1750 | 5/3/5 | 1 | modelafhankelijk | | race (RACE.md) | |
| 0x23 | 26 → 25 lus | 5110 | 3.0 | 1 | 0.8/3 = 0.267 s | | geraakt terwijl gebukt (`0x464bfd`) | .ins 26: 55/56/57 |
| 0x24 | 64 → 60 lus | 5110 | 3.0 | 1 | 0.267 s | | geraakt gebukt met bom (`0x464ba3`) | .ins 64: 55/56/57 |
| 0x2c | 36, eenmalig | 6000 | 3.0 | 1 | 5.1/3 = 1.7 s | | dood 3/4/5 op de grond met `+0x694 == 2` (`0x4647f9`) | .ins 36: ref 34 |

(`.ins`-duren in 1/4096 s: 23 = 3276, 24 = 6144, 25 = 20480, 26 = 3276, 36 = 20889, 58 = 6144, 59 = 3276, 60 = 20480, 64 = 3276.)

Prioriteit/controller (Tick `0x436a50`, zeker): het verzoek met de hoogste prio wint; is de huidige prio hoger dan de nieuwe, dan wordt pas
gewisseld als `inst+0xc0 == 1` = de keten zit in zijn laatste (lus)deel (`0x43f246`: slot0 == slot1, INSTANCE.md). Gevolgen:
* 0x31 (24) kan door niets met prio < 1750 onderbroken worden; na 0.375 s zit hij in lus 25 en neemt 0x32 (zelfde prio, restart 0) naadloos over.
* Na 0x33 speelt .ins 0 als lus-deel ⇒ de gewone grondanims (idle 0 prio 1100, aanlopen 2 prio 1501, loop 3) nemen direct over zodra sub 3 eindigt.
* Tijdens het liggen doet `Perso_AnimState` in toestand 0/9 **niets** (`0x464630`: `+0x694 ≠ 0` ⇒ return, ook geen idle-teller, geen idle-reset).
  In toestand 6 vraagt `0x4646b0` wél zijn draaganims aan (0x40..0x45, prio ≤ 1501): die verliezen van 0x4e/0x4f/0x50 (1750). In toestand 1
  zet `0x464c74` alleen `+0x4bc = 0` (geen leunen).

### 1.5 Tijdlijn bij 60 fps (afgeleid uit §1.2)

| frame | gebeurt |
|---|---|
| 0 | toets vast en op de grond: sub 1, anim 0x31, `T = 0.375`, LockMove, hoogte 61 (MoveCollide van dit frame) |
| 1..23 | `T -= 1/60`; in frame 23 wordt `T ≤ 0` ⇒ sub 2 |
| 24.. | sub 2: 0x32 elk frame; toets los ⇒ segmenttest |
| F | toets los en vrij: 0x33, `T = 0.19995`, sub 3 |
| F+1..F+12 | aftellen; in F+12 ⇒ sub 0, **geen LockMove meer in F+12**: bewegen kan in datzelfde frame (en `+0x238 = dt_vorig − dt ≈ 0`) |

Minimaal (tik op de toets): 36 frames = 0.6 s geblokkeerd. Hoogte 61 geldt van frame 0 t/m F+11.

## 2. Bewegen, draaien, springen, aanvallen tijdens het liggen

### 2.1 `0x44cce0(t, force)` LockMove (zeker)
`if (t > p->t238 || force) p->t238 = t;` (ret 8). Bukken roept `(dt, 0)` aan: een langere lopende blokkade (harde landing, speciale aanval)
blijft staan. De port-functie `lock_move()` overschrijft altijd en zet snelheid 0 – voor het bukken dus `max` gebruiken (§5.3).

### 2.2 `Perso_Move` `0x44bb20` met `+0x238 > 0` (zeker; **correctie op PERSO_MOVE §4**)
```c
if (p->M.pushT /*+0x474*/ > 0 || p->t238 > 0 || p->atk != 0) input = false;   /* 0x44bb48..0x44bb78 */
Mover_Update(&p->M, input);                     /* 0x45b110 */
vec3 h = M.velDir * M.dist;                     /* 0x44d1e0 → [esp+0x10] */
vec3 v = 0;                                     /* 0x43ff80 → [esp+4] */
if (p->t240 > 0) p->t240 -= dt; else { Jumper_Update(&J, Held(4), input); v = J.disp; }
if (p->t238 > 0) h = 0;                         /* 0x44bc16..0x44bc2f: 0x43ffa0 op [esp+0x10] = h (NIET v) */
p->disp = (use5bc ? v5bc : use69c ? v69c : h + v);  p->disp.y += dt * p->vy244;
```
PERSO_MOVE.md §4 schrijft `if (p->t238 > 0) v = 0;` – dat is fout: de stackslot na `pop edi; pop ebp` is de **horizontale** vector
(PERSO_FRAME §2.4 "d = 0" klopt). Dus tijdens het liggen:
* **geen** loop-, glij- (RampB, steile helling) of knockback-verplaatsing (RampC): alles zit in `h`;
* **wel** verticaal: de springer levert `v` gewoon (vallen als de grond wegvalt);
* **wel** alles wat ná `Perso_Move` bij `disp` komt: actor-duwen `0x4627d0` en de platformbeweging `0x436d20` in `Perso_MoveCollide`.

### 2.3 Mover zonder invoer (zeker)
`0x45b110` met arg 0: vlag 2 uit ⇒ `0x45a1f0` (vlaggen 8/0x10/0x20 wissen) en fase `M+0xc = 0` (`0x45b2bf`). `0x45a4b0` (invoer, draaien)
loopt niet ⇒ **kijkrichting `M+0x10` verandert niet: hij draait niet** (uitzonderingen: het volgdoel `+0x68c`, `0x45b1b4..0x45b26a`, en
`Hit`, die de richting direct zet). RampA wordt niet vertraagd (geen `0x467180`), maar telt zonder vlag 2 niet mee in `0x45ae80`; RampB/RampC
tikken door maar worden via `h = 0` weggegooid. Een restant knockback (`M+0xec` 0.2 s + 0.5 s uitlopen) kan dus ná het opstaan nog doorwerken
(afgeleid). Snelheid na het opstaan: fase 0 → 1 via `0x467130` (start versnellen) zodra een richting ingedrukt is; of RampA daarbij van zijn
oude waarde of van 0 vertrekt is **onzeker** (niet gelezen).

### 2.4 Springen (zeker)
`Jumper_Update(J, Held(4), input = 0)`: `pressed := false` en geen herbewapening (`input` vereist). Geen sprong, geen coyote-sprong. Omdat de
springer **Held(4)** leest (niet "net ingedrukt"): wie de springtoets vasthoudt tijdens het opstaan, springt in het eerste vrije frame (F+12,
§1.5) — afgeleid.

### 2.5 Wat gebeurt er als de grond wegvalt / bij een helling / op een platform (afgeleid)
* Hij kan niet van een rand lopen (geen horizontale verplaatsing). Valt de grond weg (bewegend platform, uitgeduwd door een actor): Jumper fase 2
  → 3 (`J_StartFall`), hij **valt gebukt** (anim blijft 0x32; `0x4642f0` wordt niet aangeroepen). Loslaten in de lucht mag: sub 2 test alleen
  toets + segment, niet de grond ⇒ opstaan in de lucht, daarna weer de lucht-anims.
* Steile helling (`n.y < 0.71`): hij glijdt **niet** zolang hij ligt (RampB zit in `h`).
* Landing met de toets vast: hij gaat liggen in het eerste frame met `onGround`; in dat frame of het volgende kan Jumper fase 6 vallen, maar
  `0x464630` wordt overgeslagen ⇒ **geen** landingsanim 8/0xa, **geen** LockMove(len 0xa), **geen** schok/ballon `0x478980`. Valschade
  (`0x44b220`, fase 6 en `fallen ≥ 1500`) blijft (die leest geen `+0x694`).

### 2.6 Aanvallen en andere acties (zeker, per leesplaats)
| actie | tijdens liggen | adres |
|---|---|---|
| pikken / stormloop / luchtaanval / ketting uit terugslag | **geblokkeerd** (`+0x694 ≠ 0` ⇒ return, vóór alles) | `0x457388` in `0x457330` |
| bom gooien (toestand 6, sub 2) | **geblokkeerd** | `0x463963` |
| bom oppakken | **niet** geblokkeerd: `0x463430` test geen `+0x694`/`+0x238` ⇒ toestand 6, hij blijft liggen (set 0x4f), de oppak-anim 0x45 (1500) verliest van 0x4f (1750) | `0x46344e` |
| vastpikken aan klimwand | niet geblokkeerd (`0x464f42..0x464f72` testen alleen `+0x5b4 ∈ {0,10}` en `+0x50c`) | `0x464ef0` |
| speciale aanval (actie 11) | niet geblokkeerd (test op de grond, toestand 0, `+0x750`, voorraad): anim 0x13 (prio 5500) en LockMove(len 0x13) | `0x458c0c..0x458c80` |
| rondkijken (actie 7) | niet geblokkeerd ⇒ toestand 3; het bukken loopt door; ooghoogte `0x44c080` = voeten + 0.9·H = **54.9** gebukt | `0x44b980`, `0x44c0a4` |

Randgevallen in de "niet geblokkeerd"-rijen zijn afgeleid, niet nagespeeld.

### 2.7 Geraakt worden terwijl hij ligt (zeker voor de code, volgorde afgeleid)
`Hit 0x44ca00` (PERSO_MOVE §4.4): knockback-richting, **kijkrichting naar de aanvaller** (M+0x10/M+0x1c/RampA.dir), `0x463170(J,0)`,
`+0x280 = 0.6 s`, `+0x238 = 0` (`0x44cce0(0, 1)`), `+0x5b4 = 0`, hit-anim `0x464b70`: grond + `+0x694 ≠ 0` (1, 2 of 3) ⇒ **0x23** (toestand 6 met
bom: **0x24**, en `+0x58c = 2`). `+0x694` wordt niet veranderd. Schoten (stap 32), vijanden en bommen lopen ná de Perso-update, dus het volgende
frame zet het bukken `+0x238` weer op dt vóór `Perso_Move` ⇒ **geen knockback-verplaatsing** zolang hij ligt; hij blijft liggen. Na 0x23
(0.267 s) staat de keten in lus 25, waar 0x32 naadloos op aansluit.

### 2.8 Dood terwijl hij ligt (zeker)
`Kill` zet toestand 2 en laat `+0x694` staan; `0x465b10` loopt niet meer (bevroren), `+0x238` wordt niet ververst (na één frame ≤ 0 ⇒ de
doodsbeweging van toestand 2 geldt normaal) en de lichaamshoogte blijft 61. Animatie (`0x4647c7`, PERSO_DEATH §3.2): soort 3/4/5 op de grond met
`+0x694 == 2` ⇒ **0x2c** (.ins 36, 1.7 s, eenmalig) en **geen** 0x29 erna (`0x46486e`); met 1 of 3 ⇒ de gewone 0x26 → 0x29; in de lucht 0x25.
Andere soorten (1, 2/9, 6, 7, 8) negeren `+0x694`. Reset `0x44ab20` zet `+0x694 = 0` (`0x44ad28`); `+0x698` wordt niet gewist (onschadelijk).

## 3. Botsing en hitbox

### 3.1 Lichaamshoogte `0x462490` (zeker)
Eerste aanroep in `Perso_MoveCollide 0x4624f0` (toestanden 0/1/2/3/4/6): `p+0x118 (P+0x08) = p->duck ? P+0x10 : P+0x0c`. Tabel `0x4b5f14`:

| P+ | kolom 0 (Woody, type 1) | kolom 1/2 (type 3/2) | kolom 3/4 (race) | betekenis |
|---|---|---|---|---|
| 0x0c | **193** | 143 | 160 | staande hoogte |
| 0x10 | **61** | 61 | 81 | gebukte hoogte |
| 0x14 / 0x18 | 193 / 61 | 143 / 61 | 143 / 81 | geen lezer gevonden in `0x44a000..0x466fff` (onzeker waarvoor) |
| 0x00 | 43 | 43 | 43 | grondpeil boven de voeten – **ongewijzigd** bij bukken |
| 0x04 | 69 | 69 | 69 | straal – ongewijzigd (alleen toestand 1 halveert, `0x462517`) |

Overal wordt de hoogte via **`0x4624c0` = `p+0x118 · inst+0x54`** (z-schaal van het model, normaal 1; de pletter-schaal `+0x2e8`) gelezen,
of als Perso-vtable slot 33 (`0x4624e0` → `0x4624c0`). `P+0x0c` rechtstreeks: alleen het effect van Kill 2/9 (`0x44c40e`, `0x44c58c`: bliksem op
voeten + 193, ook gebukt).

### 3.2 Wat er met de hoogte verandert (zeker, alle aanroepers van `0x4624c0` en van vtable-slots 24/33 nagelopen)

| gebruiker | test | effect van bukken |
|---|---|---|
| wandsweep `0x437180` (`0x462663`) | cilinderband `[voeten+marge, voeten+H]`, marge 41 op de grond / 5 in de lucht; middelpunt voeten+H/2 | band 41..61 op de grond: plafonds en overhangen boven 61 raken hem niet. Grondtest met peil +43 (`0x436f00`) en volumetest op +71 (`0x462760`) ongewijzigd |
| **projectielen** `0x44a0a0` (`0x44a102`: `vtbl[24]` = `0x44cd60`) | gesweepte bol r (5) tegen cilinder `{pos + (0,H/2,0), r = 69, halve hoogte H/2}` ⇒ y-bereik `[voeten − 5, voeten + H + 5]` | **raakt alleen onder voeten + 66** (staand 198) |
| **lasers** 50/51/52 `0x450f80` (`vtbl[24]`, `0x433de0`) | segment tegen cilinder straal 69·0.85 = 58.65, y-bereik `[voeten + 0.1, voeten + H − 0.1]` (`0x433f84..0x433fb2`, `0x4a9008` = 0.1) | **raakt alleen onder voeten + 60.9** (staand 192.9) |
| Boss14-kegel `0x410cf0` (`vtbl[33]` op `0x410d95/0x410dce`) | kegel 250 hoog onder de baas; `o = A.y − bot + H` | kleiner `o` ⇒ kleinere straal; wie diep genoeg ligt valt eronder door (BOSS14 §6.1) |
| actor-duwen `0x4627d0` (`vtbl[33]` van beide) | `0x433d40` met beide hoogtes | duwen alleen bij verticale overlap met de 61-cilinder (vorm van `0x433d40` onzeker) |
| verpletteren `0x462a40` (`0x462aa8`) | straal voeten+1 → voeten+H−1; schaal = vrij/H, < 0.3 ⇒ `Kill(4)` | gebukt pas dood bij vrije hoogte < 18.3 (staand < 57.9) |
| eerste persoon `0x44c080` (toestand 3) | oog = voeten + 0.9·H | 54.9 i.p.v. 173.7 |
| bom-worp ruimtetest `0x463a26` | bol op voeten + H | n.v.t.: gooien gebukt is geblokkeerd |
| **niet** hoogte-afhankelijk | vijand-Touch `0x40c1e0` (3D-afstand voeten–positie < r+r), dus stormloop/beet/duik van alle vijandtypes; bom-/raketexplosie `vtbl[40]` `0x44d040` (3D-afstand tot `inst.pos` < 400); Boss2 | bukken helpt **niet** |

Conclusie: **ja, Woody kan onder schoten en lasers door bukken**, zolang die hoger dan 66 resp. 61 boven zijn voeten gaan. Schutter-vijanden
schieten horizontaal vanaf hun mondingshoogte (PROJECTILES §6, richthoogte 0), lanceerders langs hun marker; of een bepaald schot hoog genoeg
vliegt hangt van het level af (niet per level nagemeten).

### 3.3 Alle lezers en schrijvers van Perso `+0x694` (volledige grep op `0x694]`; zeker)

| adres | functie | wat |
|---|---|---|
| `0x44ad28` | Reset `0x44ab20` | `= 0` |
| `0x45656d` | race `0x456210` | draaisnelheid 1.2 i.p.v. 1.8 rad/s (`0x4aa39c` / `0x4ab290`) |
| `0x457388` | aanvaltrigger `0x457330` | `≠ 0` ⇒ return |
| `0x462490` | lichaamshoogte | 61 / 193 |
| `0x462520` | `0x4624f0` | toestand 1: wandstraal ×0.5 (`0x4a9014`) |
| `0x463963` | bom sub 2 `0x46394e` | geen worp |
| `0x46463d` | anims toestand 0/9 `0x464630` | `≠ 0` ⇒ niets |
| `0x4647e7`, `0x46486e` | doodsanim `0x464790` | `== 2` ⇒ 0x2c, geen 0x29 |
| `0x464b93`, `0x464bed` | hit-anim `0x464b70` | `≠ 0` ⇒ 0x24 / 0x23 |
| `0x464c74` | race-anims `0x464c20` | `≠ 0` ⇒ `+0x4bc = 0` |
| `0x465b78..0x465dbc` | `0x465b10` | de automaat (schrijft 1, 2, 3, 0) |
| (`0x41dfc5`, `0x41fbb4`) | camera-manager | **ander object** (CamMgr+0x694, CAMERA.md) |

`+0x698` wordt alleen in `0x465b10` gelezen en geschreven.

## 4. Camera, geluid, de opstaan-test

### 4.1 Camera (zeker voor de leesplaatsen)
* **Volgcamera** (mode 0/1, `0x459090`/CAMERA.md): geen lezing van `+0x694`, `+0x118` of vtable-slot 33; het kijkdoel blijft speler + 140. Geen verandering.
* **Zij-aanzicht** (mode 0x20, `0x459c70` → `CamMgr+0x61c`): `0x459d51..0x459dbe`: ↑ (actie 2) ⇒ index 0 (`h_up`, 500); anders ↓ (actie 3) **of
  actie 5 ingedrukt** ⇒ index 2 (`h_down`, standaard 0); anders 1 (340). Het leest de **toets**, niet `+0x694` (ook in de lucht of geblokkeerd).
  Hoogte loopt lineair met 400 e/s (CAMERA_SCRIPT §4.2).
* **Eerste persoon** (toestand 3): oog op 0.9·H (§3.2).

### 4.2 Geluid (zeker)
`0x465b10` roept geen geluid aan. Alles komt uit type-4-events op de wortelknoop (SOUND.md §3), W1A model 0:
.ins 24 en 58: `t = 0`, ref **32** (bank 0 = `Common/<karakter>.rck`), kans [0,100), volume 50, toonhoogte 100 %, dmin 200;
.ins 23 en 59: idem ref **33**; .ins 26 en 64 (geraakt gebukt): refs 55 [0,30) / 56 [30,60) / 57 [60,100); .ins 36 (dood gebukt): ref 34.
De Perso speelt ze 2D. In de port speelt `src/main_engine.c` (r. 2060, "animation events of type 4") ze al automatisch af.

### 4.3 De opstaan-test `0x4359b0(&a, &b, −1)` (zeker)
* `a = pos + (0, P+0x10, 0)`, `b = pos + (0, P+0x0c − P+0x10, 0)`; `pos` = `+0x1f4` (voeten, vóór de verplaatsing van dit frame).
  Woody: **voeten+61 → voeten+132** (lengte 71); kolom 1/2: +61 → +82; race: +81 → +79 (2 eenheden, RACE §4.8). PERSO_MOVE §3.4 noemde
  "y+61 … y+193": fout (FPU-stack `0x465cd8..0x465d09` regel voor regel: `fld P10; fld y; fadd st1` ⇒ a.y; `fld P0c; fsub st1; fadd y` ⇒ b.y).
* `0x4359b0` wist `[0x53a554]`, `[0x53a560]`, `[0x53a55c]` en roept `0x497ed0(a, b, −1)`; ruw antwoord `[0x4c4bd0]`: 3 (wereldpolygoon) ⇒
  `[0x53a554] = 1`; 4 (instantiepolygoon, knoop `[0x4c4be0]`: de press-knopen, BOMB.md §5.2-correctie) ⇒ 2; 2 ⇒ 3; anders 0. `[0x53a558]` = t.
  **Elke** waarde ≠ 0 blokkeert het opstaan. Id −1 = geen instantie overslaan.
* Onzeker: of `0x497ed0` polygonen van beide kanten raakt (de straal gaat omhoog en treft dus de onderkant van een plafond); de port-functies
  `gel_ray_frac` en `player_ray_instances` zijn tweezijdig, wat voor deze test het verwachte gedrag geeft.
* Een plafond tussen 132 en 193 blokkeert niet: hij staat dan op "in" het plafond (de wandsweep duwt alleen in xz). Afgeleid.

## 5. Port-recept (`src/`; niets hiervan is al gebouwd)

### 5.1 `src/player.h`
* `PlayerInput`: `int forward, back, left, right, jump, action, duck;` (duck = actie 5, huidige toetsstand).
* `Player`: `int duck, duck_anim; float duck_t;` (= `+0x694`, de aangevraagde log. anim, `+0x698`).
* `float player_body_height(const Player *p);   /* 0x462490 → P+0x08: 193/61 (Woody), race 160/81 */`
* Kopcommentaar "Not ported yet: … ducking" bijwerken.

### 5.2 `src/player.c`
1. Constanten: `#define P_DUCK_H 61.0f  /* P+0x10 */`.
2. Hoogte:
   ```c
   float player_body_height(const Player *p)                   /* 0x462490 */
   {
       if (p->race_char) return p->race_crouch ? 81.0f : 160.0f;
       return p->duck ? P_DUCK_H : P_BODY_H;
   }
   ```
   en in `player_update` (r. 1413): `const float body_h = player_body_height(p), radius = …;` (straal ongewijzigd).
3. De automaat (naast `race_crouch`):
   ```c
   /* bukken 0x465b10 (docs/PERSO_DUCK.md): duck = +0x694, duck_t = +0x698 */
   static void duck_update(Player *p, const PlayerInput *in, float dt)
   {
       if (p->dead_kind || p->atk) return;                         /* toestand 2 / +0x5b4: niets, ook geen LockMove */
       int b = p->bomb != NULL;                                    /* toestand 6: 0x4e/0x4f/0x50 */
       switch (p->duck) {
       case 0: if (in->duck && p->on_ground) { p->duck = 1; p->duck_anim = b ? 0x4e : 0x31; p->duck_t = anim_len(p, p->duck_anim, 0); } break;
       case 1: if ((p->duck_t -= dt) <= 0) p->duck = 2; break;
       case 2: p->duck_anim = b ? 0x4f : 0x32;
               if (!in->duck) {
                   Vec3 a = { p->pos.x, p->pos.y + P_DUCK_H, p->pos.z }, e = { p->pos.x, p->pos.y + (P_BODY_H - P_DUCK_H), p->pos.z }, n; float f;
                   int blocked = gel_ray_frac(p->gel, a, e) <= 1.0f || (player_ray_instances(p, NULL, a, e, &f, &n, NULL) && f <= 1.0f);   /* n_out mag niet NULL zijn */
                   if (!blocked) { p->duck_anim = b ? 0x50 : 0x33; p->duck_t = anim_len(p, p->duck_anim, 0); p->duck = 3; }
               }
               break;
       case 3: if ((p->duck_t -= dt) <= 0) p->duck = 0; break;
       }
       if (p->duck && p->move_lock < dt) p->move_lock = dt;        /* 0x44cce0(dt, 0) = max; snelheid wordt al 0 via !allow */
   }
   ```
   `anim_len(0x31)` = 0.375 en `anim_len(0x33)` = 0.19995 komen vanzelf uit het model (§1.4).
4. Aanroep in `player_update` (r. 1350), in de tak `if (!p->dead_kind && !racing) { … }` **ná** `attack_trigger()` en de `climb_try`-return:
   `duck_update(p, in, dt);`. Racen houdt `race_crouch` (actie 8). De vroege returns (scripted actie, raket, klimwand) slaan het bukken over:
   bewuste port-afwijking (het origineel laat de automaat daar doorlopen, §1.3); zet bij hun start `p->duck = 0` als dat netter oogt.
   `move_lock` wordt aan het begin van `player_update` al met dt afgeteld: de volgorde klopt met §1.1.
5. Horizontale verplaatsing nul bij een blokkade (§2.2): in het `disp`-blok de knockback- en glijbijdragen alleen toepassen als `p->move_lock <= 0`
   (origineel: `h = 0` bij élke `+0x238 > 0`, dus ook bij harde landing/bom oppakken/gooien). Conservatief alternatief: alleen `!p->duck`.
   De push-timers laten doortikken (restant na het opstaan, §2.3). Verticaal (`jumper.dy`) niet aanraken.
6. Animatiekeuze (r. 1476): na `else if (p->hit_anim_t > 0) want = p->hit_anim;` invoegen
   `else if (p->duck) want = p->duck_anim;` — vóór de bom-tak (0x4e..0x50 winnen van de draaganims) en vóór de grond/lucht-takken (dus ook geen
   harde-landingsanim/`lock_move`/ballon tijdens het liggen, §2.5). De keten 24→25, 23→0, 58→60, 59→47 loopt `anim_request` zelf af.
   Idle (r. 1511): `if (!p->idle_hold && !p->duck && (…)) idle_reset(p);` (`0x464630` raakt de teller niet aan tijdens het liggen).
7. `player_hit` (r. 885/886): `p->hit_anim = p->on_ground ? (p->duck ? 0x23 : 0x1f) : 0x20;` en met bom `p->on_ground ? (p->duck ? 0x24 : 0x21) : 0x22`.
   `p->duck` niet wijzigen (hij blijft liggen; de yaw-draai naar de aanvaller blijft).
8. Doodsanim (r. 1473/1474, soort 3/4/5):
   `want = p->on_ground ? (p->duck == 2 ? 0x2c : 0x26) : 0x25;` bij `dead_T <= dt`, en de 0x29-stap alleen `if (!(p->dead_ground && p->duck == 2))`.
   `player_kill` laat `duck` staan; `player_reset` zet `p->duck = 0; p->duck_t = 0;`.
9. `attack_trigger`: na de `air_win`-aftelling (r. 816) `if (p->duck) return;` (vóór de `atk == 5`-ketting; het oppakken erboven blijft toegestaan, §2.6).
   `carry_frame` case 2: `if (p->carry_pressed && !p->duck)` (`0x463963`).

### 5.3 `src/main_engine.c`
1. Invoer (r. 2518): `pin.duck = (!fly && win.keys['X']) || (duck_at >= 0 && now - t0 >= duck_at && now - t0 < duck_at + duck_len);`
   met een testoptie `--duck T LEN` naast `--peck` (r. 2365/2379). **X** is vrij (in gebruik: W A S D C P, en E/Q/Spatie alleen in vliegmodus);
   Spatie (= origineel bukken) blijft springen en Ctrl/Shift aanval, zoals nu. De bestaande `memset(&pin, 0, …)` (resultaten, titel, cinematic)
   wissen `duck` automatisch.
2. Zij-aanzicht (r. 2569): `g_cam.mode == 0x20 ? (pin.forward ? 2 : (pin.back || pin.duck) ? 3 : 0) : win.keys['C']` (↑ heeft voorrang, `0x459db3`).
3. `laser_hits_player` (r. 968): `H = player_body_height(p)` i.p.v. 193.
4. Schoten (r. 1420): `y <= player_body_height(pl) + 5.0f` i.p.v. 198.
5. Bom-/raketexplosie, vijand-beten (`src/enemy.c`): **niet** aanpassen (3D-afstand, geen hoogte).

### 5.4 `src/boss.c`
`cone_touch` (r. 220): `B_PL_H` vervangen door `player_body_height(pl)` (`0x410cf0` leest `vtbl[33]`).

### 5.5 Testen
* W1A-start, `--duck 2 1.5`: 0.375 s gaan liggen, blijft liggen, 0.2 s opstaan; tijdens het liggen geen beweging/draaien met pijltjes, Spatie doet niets.
* Met WOODY_ANIMLOG de keten `lanim 0x31 → 0x32 → 0x33 → 0` controleren; gebukt voor een W1A-lanceerder of -laser gaan liggen en nagaan dat
  een schot/straal hoger dan 66 resp. 61 boven de voeten hem mist (welke dat zijn: §6.6).
* Onder een laag plafond loslaten: blijft liggen tot hij eronder uit is geduwd (kan alleen door een platform/actor, want lopen kan niet).

## 6. Open vragen

1. Of RampA na het opstaan vanaf 0 of vanaf zijn oude snelheid versnelt (`0x467130` niet gelezen); de port begint bij 0.
2. `0x497ed0`: eenzijdig of tweezijdig tegen polygonen (§4.3); antwoord 2 (trefsoort 3) is niet uitgezocht.
3. `0x433d40` (actor-duwen): exacte hoogtevoorwaarde.
4. De randgevallen van §1.3/§2.6 (bukken in toestand 3/4/5/7/8/9, oppakken of vastpikken terwijl hij ligt) zijn alleen uit de code afgeleid.
5. `P+0x14/P+0x18` (193/61): geen lezer gevonden.
6. In welke levels schoten of lasers daadwerkelijk tussen 66 en 198 boven de grond vliegen (dus ontwijkbaar zijn door te bukken): niet nagemeten.
