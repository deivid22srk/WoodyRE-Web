# PERSO_SPECIAL.md — Woody's speciale aanval (actie 11, lading `Perso+0x254`, `0x458bf0`)

Statische analyse van `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone, `tools/disasm.py` waar de lineaire
disassembly verschoven is). Elke bewering heeft een adres; floats zijn uit `.rdata` van de exe gelezen (pefile). **Zeker** = instructie voor
instructie gelezen; **onzeker** = afgeleid of niet in het origineel nagemeten. Nog niets hiervan is live in het origineel getraced.

Samenhang: PERSO_MOVE.md §3.4 (invoeracties), BONUS.md §2.3 (type 35 geeft de lading), PERSO_JUMP.md §2-4 (aanvalscontroller,
doelwitlus, animatiecontroller), PERSO_FRAME.md (volgorde van de Perso-update), ENEMY.md / ENEMY2.md / BOSS14.md (`vtbl[39]`/`vtbl[38]`
per klasse), BONUS.md §2.4 (effectpool), HUD_TEXT.md §4.6 (HUD-animator), CAMERA.md §6.3 (camera-schok), SOUND.md (SoundFx-tabel).

Notatie: `p` = Perso, `P` = parameterblok `p+0x110`, `dt` = `p+0x2f8`, `tab[i]` = de cosinustabel van 512 floats op `[0x5e823c]`
(`tab[i] = cos(2π·i/512)`, `tab[i+128] = −sin(2π·i/512)`; `[0x4b798c]` = int 128), `rand01()` = `0x43ff40` = `rand()/32767` ([0,1] inclusief),
`ftol` = `0x499580` (afkappen).

## 0. Samenvatting

* **Invoer**: actie **11** (standaard **RCtrl**), op **loslaten** (`0x467440(0xb)` = bit 31 van de actietoestand, precies één frame).
* **Voorwaarden** (alle vier, `0x458c21..0x458c4c`): op de grond (`+0x22c`), Perso-toestand `+0x21c == 0`, geen speciale aanval bezig
  (`+0x750 == 0`), lading `+0x254 > 0`. De aanvalscontroller (`+0x5b4`), bukken (`+0x694`) en dood (`+0x26c`) worden **niet** getest.
* **Start**: lading −1, `+0x750 = 1`, timer `+0x74c = 0`, bewegingsblokkade en onkwetsbaarheid voor `AnimLen(0x13,0)` = **4.0667 s**
  (`.ins`-anim 86: 49971/4096 s = 12.2 s gedeeld door snelheid 3.0), logische animatie **0x13** aangevraagd, effect-emitter `0x47ab90` gestart.
* **Mislukt** (toets losgelaten maar een voorwaarde faalt): **SoundFx 9** ("mag niet", ref 107, vol 50, 2D), behalve voor subtype 4/5 (race-Perso's).
* **Treffer**: op een **vaste tijd**, niet op een animatie-event: het eerste frame waarin de timer **≥ 1.5 s** is (`[0x4aa184]`):
  camera-schok **2.0 s**, rumble (1.0, 1.0), `+0x750 = 2`, en **één** doorloop over de actorlijst `0x4c5258[0x4c5324]` met
  `t->vtbl[39](p, P+0x94 = 3.0, &(0,0,0), &p->pos, soort 2)`; bij `true` `t->vtbl[38](3)`, wat voor **elke** vijand-/baasklasse `ret 4` is.
  **Er is geen bereiktest**: de positie van het doelwit (`vtbl[34]`) wordt opgehaald en gekopieerd maar nooit gelezen (dode code).
  Wie op de lijst staat = elke levende vijand/baas waarvan de Think vorig frame draaide (zichtbare sectoren), max. 32.
* **Einde**: timer ≥ `AnimLen(0x13,0)` ⇒ `+0x750 = 0`. Reset (`0x44ab20`, respawn) wist `+0x74c`/`+0x750`.
* **Effect** (alles in de deeltjespool `[0x5e823c]+0xdb8`): 1.25 s lang **250 lichtstrepen per seconde** die vanaf een bol met straal 400 naar
  Woody's middel (voeten + 143) toe schieten (`0x47a790`, textuur bank 0 beeld 0), en op 1.75 / 2.0 / 2.25 s na de start drie **vuurringen** plat
  op de grond die in 0.5 s van straal 0 tot 4000 uitdijen (`0x47a4c0`, bank 0 beeld 33). Geen schermflits, geen sterren in de emitter zelf.
* **Per geraakte gewone vijand** (types 4..9, 13 en baas 16) het treffersterretje `0x4750e0` — **op Woody's voeten**, niet op de vijand, omdat
  het trefpunt dat de Perso doorgeeft zijn eigen positie is. 3.0 schade doodt elke gewone vijand (max. hp 3). Baas 14 (Buzz) neemt 1 punt (alleen
  in de lage fase), baas 15 en type 12 zijn immuun, baas 16 neemt 3 punten in toestand 3/4.
* **Geluid**: alleen de animatie-events van `.ins` 86: **ref 114** op t = 0 (aanzwellend, 2.06 s) en **ref 115** op t = 1.667 s (dreun, 1.84 s),
  beide vol 50, 2D. Geen SoundFx-aanroep bij succes.
* **HUD**: de daling van `+0x254` start de "min 1"-animatie van de ladingteller (`0x448300` → `0x462380` / tick `0x462020`), ≈ 2.4 s.
* **Script**: er wordt **geen** bericht naar de VM gestuurd, geen variabele gezet; schakelaars, kisten, bommen en andere wereldobjecten reageren niet.

## 1. Trigger en toestandsmachine

### 1.1 Waar het draait

`0x458bf0` wordt alleen aangeroepen vanuit de Perso-update `0x44b530` (`0x44b7b9`), elk frame, als laatste van de reeks
`0x462c60, 0x464ef0, 0x465e50, 0x457a50, 0x44ba70, 0x465b10, 0x44b980, 0x458bf0`, **vóór** de toestandsdispatch (bewegen, `0x44b7f9`).
Elke stap wordt overgeslagen zodra `+0x690` (toestand gewisseld dit frame) gezet is. Er is **geen** test op de Perso-toestand, op dood of op
cinematic: in elke toestand wordt de invoer bekeken en de lopende automaat verder getikt. De bewegingsblokkade `+0x238` is dat frame al met
`dt` verlaagd (`0x44b638`).

### 1.2 Decompilatie (zeker)

```c
void Perso_Special(Perso *p)                                   /* 0x458bf0, 119 instructies */
{
    if (Input_Released(p->input /*+0x2f4*/, 11)) {             /* 0x467440: (state[11] >> 31) & 1 */
        if (p->onGround /*+0x22c, 0x44bcf0*/ && p->state /*+0x21c*/ == 0 &&
            p->special /*+0x750*/ == 0 && p->charges /*+0x254*/ > 0) {
            p->charges--;                                      /* 0x458c5b */
            p->special = 1;  p->specialT /*+0x74c*/ = 0;
            LockMove(p, AnimLen(p->anim /*+0x494*/, 0x13, 0), 0);   /* 0x44cce0: +0x238 = max(+0x238, T) */
            SetInvulnerable(p, AnimLen(p->anim, 0x13, 0));     /* 0x44cd10: +0x270 = max(+0x270, T) */
            p->anim->vtbl[2](0x13);                            /* 0x436b70 Request(0x13), één keer */
            SpecialFx_Start();                                 /* 0x47ab90 */
        } else if (Subtype(p) != 4 && Subtype(p) != 5)         /* 0x40c350: (typewoord >> 5) & 0x1f */
            SoundFx(9, 0);                                     /* 0x468a00 op [0x5e48c8], 2D */
    }
    switch (p->special) {
    case 1:
        p->specialT += dt;                                     /* 0x458d37 */
        if (p->specialT < 1.5f /*[0x4aa184]*/) return;
        CameraShake([0x4c737c], 2.0f);                         /* 0x41fbb0: cam+0x67c = 2.0, cam+0x694 = 0 */
        Rumble(p, P->+0xb4 /*1.0*/, P->+0xb0 /*1.0*/);         /* 0x44d1b0: [0x5e618c]->vtbl[2], niet als +0x21c == 2 */
        p->special = 2;
        for (i = 0; i < [0x4c5324]; i++) {                     /* actorlijst van het vorige frame, 0x4c5258[] */
            Actor *t = [0x4c5258][i];
            if (t == p) continue;
            vec3 tp  = *t->vtbl[34]();                         /* positie – gekopieerd naar een local en NOOIT gebruikt */
            vec3 dir = { 0, 0, 0 };                            /* 0x43ff80(0,0,0) */
            if (t->vtbl[39](p, P->+0x94 /*3.0*/, &dir, &p->pos /*+0x1f4*/, 2))
                t->vtbl[38](3);
        }
        return;
    case 2:
        p->specialT += dt;
        if (AnimLen(p->anim, 0x13, 0) <= p->specialT) p->special = 0;   /* 0x458d1c */
        return;
    }
}
```

* Het frame waarin de aanval start telt al mee: de switch loopt in hetzelfde frame, dus `specialT = dt` na het startframe.
* `0x436b90(n, k)` = AnimLen = `anim[sub[k]].duur(1/4096 s) / speed` (PERSO_JUMP §4). Voor `(0x13, 0)`: sub[0] = `.ins` 86 ⇒ 49971/4096/3 = **4.06665 s**
  in alle drie de karaktermodellen (W1A model 0, K1A model 45, S1A model 43: 2440 frames, 49971/4096 s).
* `P+0x94`, `P+0xb0`, `P+0xb4` komen uit de standaardwaarden `0x4631b0` (`0x4631d6`: 3.0; `0x463227`/`0x46322d`: 1.0), niet uit de kolomtabel
  `0x4b5f14` (`0x463280` kopieert alleen P+0x00..0x78). Ze zijn dus voor alle vijf subtypes gelijk.
* Subtype 1..5 = script-type 1→1, 2→3, 3→2, 18→5, 19→4 (PERSO_MOVE §1): alleen de race-Perso's (18/19) blijven stil bij mislukken.

### 1.3 Velden

| veld | type | betekenis | schrijvers / lezers |
|---|---|---|---|
| `+0x254` | int | lading (aantal speciale aanvallen), HUD-teller, savegame `slot+0x14` | `0x44c800` (+1, bonus 35), `0x458c5b` (−1), `0x44ae60` → HUD |
| `+0x74c` | f32 | timer sinds de start (s) | `0x458c6b` = 0, `0x458d00`/`0x458d43` += dt, Reset `0x44ad5e` = 0 |
| `+0x750` | int | 0 = vrij, 1 = opladen (tot 1.5 s), 2 = na de treffer (tot AnimLen) | alleen `0x458bf0` en Reset `0x44ad64` (grep op alle `+ 0x750]`) |
| `+0x238` | f32 | bewegingsblokkade (LockMove, max) | PERSO_JUMP §0 |
| `+0x270` | f32 | onkwetsbaarheid (max) | PERSO_MOVE §2 |

### 1.4 Tijdlijn (vanaf het frame van het loslaten, t = opgetelde `dt`)

| t (s) | gebeurtenis | bron |
|---|---|---|
| 0 | lading −1; anim 0x13 (.ins 86) start (restart-vlag 1); blokkade + onkwetsbaar tot 4.067; geluid **114**; emitter start | `0x458c4e..0x458cac`, anim-event frame 0 |
| 0 .. 1.25 | 250 strepen/s, elk 0.5 s levend, schieten naar Woody toe | `0x47a8d0` (emitter-u < 0.5) |
| ≥ 1.5 (eerste frame) | camera-schok 2.0 s, rumble, **treffer**, `+0x750 = 2` | `0x458d49..0x458e28` |
| 1.667 | geluid **115** (anim-event frame 1000 van 2440) | `.ins` 86 root-node |
| 1.75 / 2.0 / 2.25 | vuurring 1 / 2 / 3 (elk 0.5 s) | `0x47aa3c..0x47ab68` |
| 2.5 | emitter op | `0x47a912` |
| ≈ 2.75 | laatste ring weg | |
| 3.5 | camera-schok voorbij | CAMERA.md §6.3 |
| 4.067 | `+0x750 = 0`, blokkade en onkwetsbaarheid op (idem) — anim 86 is uit | `0x458d1c` |

### 1.5 Wat blokkade en onkwetsbaarheid doen (uit bestaande docs, zeker)

* `+0x238 > 0`: `0x44bb20` geeft de Mover geen invoer ⇒ niet lopen, niet draaien, **niet springen** (`0x462d70(a, 0)`); de aanvaltrigger
  `0x457330` keert direct terug (geen pik, geen stormloop, geen luchtaanval). Bukken zet zelf `+0x238`, maar zijn trigger (`0x465b10`) kijkt niet
  naar `+0x750` — bukken tijdens de aanval is niet uitgesloten (onzeker hoe dat eruitziet; de anim-prioriteit 5500 wint van bukken).
* `+0x270 > 0`: `Hit` (`vtbl[39]` `0x44ca00`) en `Kill` soorten 2, 3, 4, 5, 6, 8, 9 worden genegeerd. **Kill 1 (put) en 7 (water) niet**: in een
  put of water gaat hij gewoon dood; de automaat loopt dan door (alleen Reset wist hem) en de treffer valt eventueel toch op 1.5 s.
* De voorwaarde `+0x21c == 0` sluit uit: dood (toestand 2), rondkijken (3), klimwand (4), scriptactie (5), bom dragen (6), raket (8),
  resultatenscherm (9), race (1). Loslaten in die toestanden = geluid 9 (behalve race).
* **Niet** uitgesloten (randgevallen, zo in de code): de aanval tijdens een lopende grondaanval (`+0x5b4 ≠ 0`, bv. stormloop 9/10 of de
  rebound 6/7), en loslaten tijdens een al lopende speciale aanval ⇒ geluid 9.

### 1.6 Animatie 0x13

Record `0x4b6180 + 0x13·0x1c` = `{sub {86, 0, 0, 0}, prio 5500, speed 3.0, restart 1}` (zeker, uit de exe; de port heeft het in `log_anim`).
Na `.ins` 86 volgt in de keten `.ins` 0 (idle). De Request gebeurt **één keer**; de controller `0x436a50` houdt 0x13 vast omdat de verzoeken
van de gewone animatiekeuze in de volgende frames een lagere prio hebben (idle 1100, lopen 1500/1501, val 5000, aanvallen 1600..1700). Alleen de
doodsanimaties (6000) en de scriptacties (6000) winnen. Onkwetsbaar ⇒ geen trefferanimatie. `.ins` 86 heeft in geen van de drie modellen een
track op een camera-knoop (vlag 0x80), dus geen camerabeweging uit de animatie.

Animatie-events van de root-node van `.ins` 86 (zelfde in W1A/K1A/S1A):

| frame | type | ref | kans | vol | pitch | dmin |
|---|---|---|---|---|---|---|
| 0 | 4 (geluid) | 114 (bank 0 = `Common/<karakter>.rck`, 22050 Hz mono, 2.056 s, aanzwellend) | 0..100 % | 50 | 100 % | 200 |
| 1000 | 4 (geluid) | 115 (idem, 1.842 s, harde lage dreun die uitsterft) | 0..100 % | 50 | 100 % | 200 |

De Perso speelt ze **2D** af (SOUND.md §3). Frame 1000 van 2440 = 5.0 s van 12.2 s ⇒ 1.667 s bij snelheid 3. De "beschrijving" van de klanken
komt uit een RMS-/nuldoorgangsmeting van de samples (onzeker, ik kan ze niet horen).

## 2. De treffer

### 2.1 Wie in `0x4c5258[]` staat (zeker)

De lijst is de kopie van vorig frame (`0x40bf60` → `0x4c5258`/`0x4c5324`) van de registratielijst `0x4c4d80` (max **32**, `0x40c0b0`:
volle lijst ⇒ stil niet toegevoegd). `0x40c0b0` wordt alleen aangeroepen door de Perso zelf (`0x44b731`, overgeslagen door `t != p`) en door
`vtbl[47]` van de vijandklassen, vanuit `Enemy_Think` (`vtbl[3]` `0x41a320`, **vóór** de 3000-activeringstest, maar ná "geen cinematic").
`vtbl[3]` draait alleen voor instanties in de per frame opgebouwde lijst `world+0x64` van de **zichtbare sectoren** (`0x42a980`, BONUS §3.1).

| klasse | `vtbl[47]` | geregistreerd in toestanden |
|---|---|---|
| 4/5/6 | `0x419460` | 0..11 (niet 12 = dood) |
| 7/8/9 | `0x417f20` | alles behalve 10 (dood) |
| 10 (geen level) | `0x416030` | onbekend |
| 11 (geen level) | `0x412d50` | niet 10 |
| 12 (W2B-baas) | `0x411970` | niet 1 en 13 |
| 13 (spook) | `0x414470` | 0..10 en 13 |
| 14/15/16 (bazen) | `0x41a4d0` | altijd |

**Niet** op de lijst: bonussen, kisten 120/121, bommen (40), klasse 17, lasers, lanceerders, raketten, schakelaars, basisinstanties
(OBJECTS.md §0 punt 1; BOMB_CARRY.md). Tijdens een cinematic registreert geen enkele vijand zich ⇒ lege lijst.

### 2.2 Geen bereik, geen kegel, geen hoogte (zeker)

De doorloop (`0x458d9f..0x458e22`) roept `vtbl[34]` aan en kopieert het resultaat naar `[esp+0x10..0x18]`; daarna wordt die local niet meer
gelezen. De enige argumenten van `vtbl[39]` zijn `(p, 3.0, &nulvector, &p+0x1f4, 2)`. De speciale aanval treft dus **iedere** geregistreerde
actor, waar hij ook staat — in de praktijk alles wat levend is in de sectoren die de camera kan zien (en maximaal 32).

### 2.3 `vtbl[39]` / `vtbl[38]` per klasse (zeker, per functie gelezen)

`vtbl[38]` is voor **alle** klassen 4..16 dezelfde `0x4078a0` = `ret 4` (vtable-dump van `0x4a9ec0`, `0x4a9dc0`, `0x4a9cb8`, `0x4a9ab0`,
`0x4a99b0`, `0x4a9bb0`, `0x4a98a8`, `0x4a9778`, `0x4a9658`, slot 38). De `vtbl[38](3)` na een dodelijke treffer doet dus niets.

| klasse | `vtbl[39]` | met (3.0, dir 0, pt = Woody's voeten, soort 2) | gevolg |
|---|---|---|---|
| 4/5/6 | `0x419480` | `hitT (+0x158) > 0` ⇒ false. Anders toestand 9, AnimCtrl reset, `hitT = vtbl[53](0)` (0.25 s), `Enemy_TakeDamage(…, soort 0)` | hp 1..2 ⇒ **dood** |
| 7/8/9 | `0x417f60` | idem met toestand **4** | hp 1..3 ⇒ **dood** |
| 10 | `0x416070` | toestand 8, `hitT`, `Enemy_TakeDamage(…, soort 1)` | (niet gebruikt) |
| 11 | `0x412d90` | toestand 4, `hitT`, soort 0 | (niet gebruikt) |
| 12 | `0x411ab0` | alleen soort 0 of 1 (`0x411ab5..0x411abe`) ⇒ bij soort 2 **niets**, false | **immuun** |
| 13 | `0x414490` | log `'TimeHit : %f'`, toestand 6, `hitT`, soort 0 | hp 3 ⇒ **dood** |
| 14 (Buzz) | `0x40fe90` | alleen lage fase (`+0x224 == 0`) en `hitT ≤ 0`: toestand 9, `hitT`, SoundFx 42/47, rook/explosie (BOSS14 §6.2), `Enemy_TakeDamage(…, **1.0**, soort **2** doorgegeven)` | **1 van 5** treffers, geen sterretje |
| 15 | `0x40e720` | `xor al,al; ret 0x14` | **immuun** |
| 16 | `0x40d480` | alleen toestand 3/4: `+0x26c −= 0.2`, `+0x298 = 0`, toestand 5, `Enemy_TakeDamage(…, 3.0, soort 0)`, stop SoundFx 0x42 (`0x468a30`), SoundFx 0x41; hp ≤ 0 ⇒ toestand 7; anders `AnimCtrl->vtbl[2](2)`; geeft **altijd true** | 3 schade (hp van baas 16 niet geanalyseerd) |

`Enemy_TakeDamage` `0x41adc0(att, dmg, dir, pt, soort)`:
```c
if (!Behav_Knock(e->behav /*+0x11c*/, dir)) return false;   /* 0x41b6b0: lopende terugslag ⇒ geweigerd, GEEN schade (toestand/hitT zijn al gezet) */
if (soort != 2) Effect_HitStar(pt);                          /* 0x40c2d0 → 0x4750e0(&kopie van *pt); pt == 0 ⇒ niets */
e->hp /*+0x150*/ -= dmg;  return e->hp <= 0;
```
`Behav_Knock` met `dir = (0,0,0)`: `knockDir = 0`, `knockT = vtbl[53](−1)` ⇒ geen verplaatsing. Omdat 4..9, 11 en 13 de soort op 0 (10: 1) zetten,
krijgt **elke geraakte gewone vijand een sterretje op Woody's voeten** (§3.4) — het trefpunt is `&p->pos`, niet de vijand. Voor Buzz (soort 2
doorgegeven) niet. De dood zelf (sterren boven het hoofd `vtbl[57]` `0x477610`, fade, verwijderen) regelt de vijand in zijn eigen automaat
(ENEMY.md §6.2); geen scriptbericht, geen SetVar (behalve Buzz' eigen mailbox in toestand 12, BOSS14 §6.3).

Het maximum-hp van de gewone types is 3 (`0x41d510`, ENEMY.md §2.3: types 9 en 13), dus 3.0 doodt ze allemaal, tenzij het script met bericht 11
hun hp hoger heeft gezet.

### 2.4 Wereldobjecten

Niets. Pikschakelaars (1050/1042) lezen actie **6**, niet 11; kisten reageren alleen op bomexplosies (`0x44d650`); bommen, lasers, klimwanden en
bonussen zitten niet in de actorlijst. Er is geen andere lezer van `+0x750` of `+0x74c` dan `0x458bf0` en Reset.

## 3. Effecten en geluid

Alle deeltjes gebruiken de pool `[0x5e823c]+0xdb8` (records van 0x50 B, max **2000** = `0x7d0`, teller `+0x27eb8` = `0xdb8 + 0x27100`, geen
vrije lijst: vol ⇒ het record wordt stil overgeslagen). Record: `+0` leeftijd, `+4` levensduur (−1.0 = weg), `+8..` data, `+0x4c` callback.
De driver `0x470c70` roept elke callback één keer per frame aan (na de wereld, vóór de HUD), elke callback telt zelf `dt = [[0x509adc]+0x38]`
op. `[0x5e823c]+0x27ec0` = **de actor met categorie 1** (de Perso), elk frame gezet door `0x46d120` (`0x46d151`).

### 3.1 De emitter `0x47ab90` → callback `0x47a8d0` (zeker)

`0x47ab90`: record `{leeftijd 0, levensduur 2.5 (0x40200000), +8 acc = 0, +0xc vlaggen = 0, callback 0x47a8d0}`. **Geen positie**: de kinderen
lezen Woody's positie zelf.

```c
void SpecialEmitter(Rec *r)                                    /* 0x47a8d0 */
{
    r->age += dt;  float acc = r->acc /*+8*/ + dt;  r->acc = acc;
    float u = r->age / r->life;                                /* life 2.5 */
    if (u >= 1.0f) { r->life = -1.0f; return; }                /* 0x47ab75 */
    if (u < 0.5f /*[0x4a9014]*/) {                             /* eerste 1.25 s */
        int n = ftol(acc * 250.0f /*[0x4ab144]*/);  r->acc = acc - n * 0.004f /*[0x4abd80]*/;   /* 250 per seconde */
        while (n-- > 0) {
            Rec *s = PoolNew(); if (!s) continue;              /* vol: overslaan */
            s->age = 0; s->life = 0.5f; s->cb = 0x47a790;
            int a = ftol(rand01() * 512.0f /*[0x4a9874]*/), b = ftol(rand01() * 512.0f);
            s->v.x =  400.0f /*[0x4a964c]*/ * tab[a & 511] * tab[b & 511];                 /* 400·cos a·cos b */
            s->v.y = -400.0f /*[0x4abd7c]*/ * tab[(a + 128) & 511];                        /* 400·sin a */
            s->v.z = -(400.0f * tab[a & 511]) * tab[(b + 128) & 511];                      /* 400·cos a·sin b */
        }
    }
    /* één ring per venster, hoogstens één per frame (vlaggen +0xc bit 0/1/2): */
    int w = u >= 0.9f ? 4 : u >= 0.8f ? 2 : u >= 0.7f ? 1 : 0;  /* [0x4a94b8] 0.9, [0x4a987c] 0.8, [0x4aa1d8] 0.7 */
    if (w && !(r->flags & w)) {
        Rec *g = PoolNew();
        if (g) { g->age = 0; g->life = 0.5f; g->cb = 0x47a4c0; g->c = Perso->+0xc; }      /* de voeten, NU vastgelegd */
        r->flags |= w;                                         /* ook als de pool vol was */
    }
}
```
Dus: strepen gedurende t ∈ [0, 1.25) s, ringen op t = 1.75, 2.0, 2.25 s (emitter-u 0.7/0.8/0.9). De strepen zijn **tijdgebaseerd** (niet per
frame); gemiddeld ~125 tegelijk in leven.

### 3.2 Streep `0x47a790` (zeker)

```c
void SpecialStreak(Rec *s)                                     /* 0x47a790, sprite-object S = [0x5e823c]+0xb00 */
{
    s->age += dt;  float u = s->age / s->life;                 /* life 0.5 */
    if (u >= 1.0f) { s->life = -1.0f; return; }
    vec3 C = Perso->+0x60 + (0, 100.0f /*[0x4a9010]*/, 0);     /* +0x60 = voeten + (0, P+0 = 43, 0) (0x44bf10) ⇒ C = voeten + (0,143,0), ELK FRAME opnieuw */
    float k = 1.0f - u * u;
    S.p0 /*+0x278*/ = C + s->v;                                /* vast punt op de bol, r = 400 */
    S.p1 /*+0x284*/ = C + k * s->v;                            /* kop: van de bol naar C, versnellend */
    S.rgba0 /*+0x290*/ = (1, 1, 1, 0);
    S.rgba1 /*+0x2a0*/ = (1, 1, 1, u <= 0.7f ? 1.0f : 1.0f - (u - 0.7f) * 3.33333f /*[0x4aa3a8]*/);
    S.halfWidth /*+0x2b0*/ = 2.0f;  S.tex /*+0x2b4*/ = 0x10000;   /* bank 0 beeld 0 (32×32 horizontale zachte band) */
    Line(&S, 0xe00);                                           /* 0x471a10: eigen breedte 0x400, eigen kleuren 0x800, textuur 0x200 */
}
```
De lijnprimitief `0x471a10` (OBJECTS.md §2.1) bouwt een camera-gerichte quad van p0 naar p1 met halve breedte 2.0 **wereldeenheden**
(de loodlijn wordt in 2D-projectie bepaald en in view-space toegepast), slaat hem over als de geprojecteerde lengte < 0.01 (`[0x4a94f8]`) en
dient hem in met `0x481560(…, 4, verts, tex, 0x24)` = **additief**. Beeld: 0.5 s lang schiet een witte streep vanuit een punt op 400 van
Woody's middel naar hem toe; de staart (p0) is zwart (alfa 0), de kop wit, de laatste 30 % dooft de kop.

### 3.3 Vuurring `0x47a4c0` (zeker)

```c
void SpecialRing(Rec *g)                                       /* 0x47a4c0 */
{
    g->age += dt;  float u = g->age / g->life;                 /* life 0.5 */
    if (u >= 1.0f) { g->life = -1.0f; return; }
    float r0 = 4000.0f /*[0x4aa318]*/ * u, r1 = 4000.0f * (u + 0.2f /*[0x4a9760]*/);
    for (int i = 0; i < 16; i++) {                             /* edi = 0..0x2000 stap 0x200, hoek = edi/16 ⇒ 32/512 = 22.5° per segment */
        int a0 = 32 * i, a1 = a0 + 32;
        Vtx v[4];                                              /* 0x40 B per vertex: xyz +0, rgba +0x24..+0x30, uv +0x34 */
        v[0] = { c.x + r0*tab[a0], c.y, c.z - r0*tab[a0+128],  rgba (0.5, 0.5, 0.5, 0),     uv (0,0) };   /* binnen */
        v[1] = { c.x + r1*tab[a0], c.y, c.z - r1*tab[a0+128],  rgba (0.5, 0.5, 0.4, 1 − u), uv (1,0) };   /* buiten */
        v[2] = { c.x + r1*tab[a1], c.y, c.z - r1*tab[a1+128],  rgba (0.5, 0.5, 0.4, 1 − u), uv (1,1) };
        v[3] = { c.x + r0*tab[a1], c.y, c.z - r0*tab[a1+128],  rgba (0.5, 0.5, 0.5, 0),     uv (0,1) };
        Submit([0x5e86ac], 4, v, [0x5e866c] + 0xef4 /*bank 0 beeld 33*/, 0x44);   /* 0x481560, TWEE KEER met dezelfde argumenten */
        Submit([0x5e86ac], 4, v, [0x5e866c] + 0xef4, 0x44);
    }
}
```
`c` = Perso`+0xc` (= de voeten) op het moment van spawnen; de ring volgt Woody niet. Vlak op de hoogte van de voeten (y = c.y), geen
grondvolging. Vlag 0x40 = wereldvertices (transformatie met `renderer+0x114`), 4 = additief, bit 1 niet gezet ⇒ geen culling (van onder
ook zichtbaar). Beeld 33 = 64×64 vuurverloop: u = 0 zwart, via rood/oranje naar wit-geel bij u = 1, met horizontale strepen; de u loopt van de
binnenrand (zwart) naar de buitenrand (wit-heet). Binnenstraal 0 → 4000, buitenstraal 800 → 4800 in 0.5 s (8000 eenh./s), band 800 breed.

### 3.4 Treffersterretje `0x4750e0(&pt)` (zeker; per geraakte gewone vijand, op `pt` = Woody's voeten)

Dit is het algemene treffereffect (ook van de pik en van bericht 1507); de port heeft het nog nergens.

* **Flits** (1 record, levensduur **0.1 s**, callback `0x475040`, pos = pt): billboard, beeld `0x10009` = **bank 0 beeld 9** (64×64 witte
  flits), halve diagonaal `u · 250` (`[0x4ab144]`), rgb (1,1,1), alfa **0.4**, spritemodus 0x12, tekenvlaggen **3** (billboard, eigen kleur,
  additief, geen rotatie).
* **8 vonken** (callback `0x474e00`, levensduur **0.4 s**): basis `U, V` = `0x46d320(normalize(camerapositie − pt))` (twee eenheidsvectoren loodrecht
  op de kijkrichting). Voor vonk j = 0..7 (`ebp = 64·j`):
  `a1 = ftol(rand01()·50 + 64j − 25)`, `a2 = ftol(rand01()·50 + 64j − 25)` (twee **onafhankelijke** trekkingen, `[0x4a9030]` = 50, `[0x4abc64]` = 25),
  `dir = normalize(U·tab[a1] + V·(−tab[a2+128]))` ⇒ 8 spaken om de 45° ± 17.6° in het schermvlak;
  `spin = rand01()·2 > 1.0` (`[0x4abcc0]` double 1.0); `grootte = rand01()·20 − 5 + 25` = **20..40** (`[0x4a9994]` 20, `[0x4a9884]` 5).
  Per frame (u = leeftijd/0.4):
  `i = ftol(u · 128)` (`[0x4a9020]`), `kop = pt + dir · (−150)·tab[(i+128) & 511]` = `pt + dir · 150 · sin(π/2 · i/128)` (`[0x4abcb8]` = −150);
  lijn `0x471a10(0xe00)` van pt (rgba (1,1,1,0)) naar kop (rgba (1,1,1,**0.1**)), halve breedte **10** (`0x41200000`), beeld `0x10007` =
  **bank 0 beeld 7** (snelheidsstreepjes); daarna sprite op de kop: beeld `0x10008` = **bank 0 beeld 8** (gele ster), halve diagonaal = grootte,
  rgb (1,1,1), alfa `1 − u`, rotatie `spin ? ftol(256u) : ftol(511 − 256u)` in 1/512 slag (`[0x4aa3ac]` 256, `[0x4abc90]` 511), modus 0x12,
  vlaggen **7** (billboard, eigen kleur, rotatie, additief).

### 3.5 Kleurconventie van het additieve pad (zeker; belangrijk voor de port)

`0x481560` met vlag 4 (additief, `0x481d8c`) zet de vertexkleur als `byte = ftol(a · c · 128)` per kanaal (`[0x4a9020]` = 128,
`0x481e5e..0x481f31`), zonder alfabyte; het doorzichtige/dekkende pad (vlag 8/2) gebruikt `c · 255` en `a · 255` (`[0x4aa308]`). Met
COLOROP MODULATE2X (LIGHTING §1.5) is de effectieve factor dus:

* **additief (ONE/ONE)**: `textuur × c × a` — de alfa werkt als voorvermenigvuldiging (en de ONE/ONE-menging negeert de alfa zelf);
* **doorzichtig/dekkend**: `textuur × 2c`, alfa `a` — hier is 0.5 = neutraal.

De sprite-route `0x470f10` kopieert de kleur ongewijzigd als het device MODULATE2X kan (`0x4710b7`) en verdubbelt hem anders (`0x471224`:
`fadd st0,st0`), met hetzelfde eindresultaat. Voor de port (`hud_world_fx`/`world_line` doen `rgb × alpha` bij ONE/ONE, zonder 2×):
**neem c en a van het origineel ongewijzigd over** voor additieve effecten. Gevolgen hier: streep kop 1.0, staart 0; ring buitenrand
`(0.5, 0.5, 0.4)·(1−u)`, binnenrand 0 — twee keer ingediend ⇒ netto `(1.0, 1.0, 0.8)·(1−u)`; flits 0.4; vonklijn 0 → 0.1; ster `1 − u`.
**Bijvangst**: BONUS.md §2.4 ("kleur (0.5,0.5,0.5) = vol wit") en `fx_update` in `main_engine.c` (wit = {1,1,1} voor het origineel 0.5)
rekenen met de 2×-regel van het doorzichtige pad; voor het additieve pad is dat volgens deze lezing een factor 2 te fel. Niet in deze ronde
gewijzigd; nagaan in het origineel (schermafbeelding van een pickup-effect) vóór het aanpassen.

### 3.6 Camera en rumble

* `0x41fbb0([0x4c737c], 2.0)` (`0x458d60`: `push 0x40000000`): resterende schoktijd `cam+0x67c = 2.0` (**zetten**, niet max), `cam+0x694 = 0`.
  De schok zelf (CAMERA.md §6.3, `0x41fbd0`): alleen de kijkrichting trilt, ±5·min(t, 3) per as — met t = 2.0 dus ±10 aflopend. De port heeft
  dit als `game_cam_shake(t)`.
* `0x44d1b0(p, 1.0, 1.0)` = joystick-force-feedback `[0x5e618c]->vtbl[2](1.0, 1.0)` (PERSO_JUMP §0); niet te porten.
* Geen schermflits, geen kleurfilter, geen slow-motion, geen fade (gecontroleerd: `0x458bf0` roept alleen `0x467440, 0x44bcf0, 0x436b90,
  0x44cce0, 0x44cd10, 0x47ab90, 0x40c350, 0x468a00, 0x41fbb0, 0x44d1b0, 0x43ff80` en de twee vtable-slots aan).

### 3.7 Geluid

| wanneer | wat | bron |
|---|---|---|
| start | ref 114, vol 50, 2D | anim-event `.ins` 86 frame 0 |
| 1.667 s | ref 115, vol 50, 2D | anim-event frame 1000 |
| mislukt | SoundFx 9 = ref 107 (0.40 s), vol 50, 2D | `0x458cd5` |
| Buzz geraakt | SoundFx 42 (W1B) / 47 | `0x40fef1` |
| baas 16 geraakt | stop SoundFx 0x42, speel 0x41 | `0x40d4e6`, `0x40d4f5` |
| gewone vijand geraakt | alleen hun eigen animatie-events (trefferanimatie) | |

### 3.8 HUD: "min 1" van de ladingteller (zeker)

`0x44ae60(p, 0)` (elk frame) → `0x448300(hud, +0x254, 0)`: als `hud+0x1c > nieuw` ⇒ `0x462380(animator)` en vlag `hud+0x41 = 1`; daarna
`hud+0x1c = nieuw`. De tick `0x462020(hud+0x1c)` draait vanuit `0x4480d0` (`0x448293`) zolang `+0x41` staat **en de pickup-animatie van de
lading (`+0x3a`) niet loopt**; zijn teruggave wordt `+0x41`. Hij gebruikt dezelfde objecten als de pickup (IconSlide `animator+0x20`, Plate
`+0x2c`, NumPopup `+0x10`) en toont alleen de **oude** waarde (`nieuw + 1`); de nieuwe waarde verschijnt nooit (buiten het pauzemenu tekent
`0x447660` de ladingrij niet, HUD_TEXT §4.6).

Start `0x462380`: IconSlide `(−63, 221) → (16, 221)`, sprite 6 (`0x47c1a0`; `[0x4ab6d8]` 63, `[0x4b3a30]` 16, `[0x4b3a34]` 221);
Plate `(50, 266)` van 0×0 naar 34×34 (`0x47bf90`; `[0x4b3a38]` 50, `[0x4b3a3c]` 266, `[0x4ab700]`/`[0x4ab704]` 34); NumPopup op anker A2
(`[0x5d7b60]`/`[0x5d7b64]` = (66, 282)), 2 fasen, 17 → 37, 0.2 s (`[0x4b3a88]` 17, +`[0x4a9994]` 20); `+0x64 = 0`, `+0x68 = 1.0`,
bytes `+0x77` (pop), `+0x7b` (slide), `+0x7e` (plate) = 1, fase `+0x50 = 1`.

| fase | zolang | tekent | dan |
|---|---|---|---|
| 1a | slide loopt (0.2 s) | het inglijdende icoon (sprite 6) | |
| 1b | plate loopt (0.2 s) | sprite 6 op slot 4 (16, 221) + groeiend rondje (sprite 8) | |
| 1c | pop loopt (0.4 s) | sprites 6/8 + getal `oud` 17→37→17 | |
| 1d | één frame | getal `oud` (17) + sprites 6/8; start pop (A2, **1 fase, 17 → 0**, 0.2 s), slide `(16,221) → (−63,221)`, plate 34×34 → 0×0 | fase 2 |
| 2 | `+0x64 += dt` tot > 1.0 s | getal `oud` (grootte 17) + sprites 6/8 | fase 3 |
| 3a | pop loopt (0.2 s) | sprites 6/8 + het krimpende getal `oud` | |
| 3b | plate loopt (0.2 s) | sprite 6 op slot 4 + krimpend rondje | |
| 3c | slide loopt (0.2 s) | het uitglijdende icoon; einde ⇒ `+0x41 = 0` | |

Totaal ≈ 2.4 s. Alles lineair, kleur `0xfe808080`, getal rood (HUD_TEXT §4.6, NumPopup).

## 4. Het mislukte pad en het script

* Loslaten van actie 11 terwijl een voorwaarde faalt ⇒ `0x40c350(p)` (subtype); ≠ 4 en ≠ 5 ⇒ `SoundFx(9, 0)` (`0x458cc1..0x458cd5`). Niets
  anders: geen animatie, geen HUD, lading ongewijzigd. Dit geldt ook bij loslaten tijdens een lopende speciale aanval, in de lucht, dood, tijdens
  bom dragen enz.
* Er gaat **geen** bericht naar de VM (geen `0x443e50`/`0x443e90`/`0x443ca0` in `0x458bf0` of de effectfuncties), geen msgmask-bit, geen
  scriptvariabele. De enige indirecte scriptgevolgen zijn die van de doelwitten zelf (Buzz' mailbox bij zijn dood, BOSS14 §6.3).
* De lading staat in de savegame (`slot+0x14`, BONUS §2.3) en wordt op dezelfde manier als bij het oppakken aan het levelende weggeschreven.

## 5. Portrecept

Doel: alles hierboven, met de bestaande bouwstenen. Bestanden: `src/player.h`, `src/player.c`, `src/enemy.h`, `src/enemy.c`,
`src/main_engine.c`, `src/hud.h`, `src/hud.c` (en optioneel `src/render_gl.c` voor RCtrl). CRLF-bestanden: `player.c`, `render_gl.c`.

### 5.1 Invoer

* `PlayerInput` (`player.h`): `int special;` (actie 11, vasthouden).
* `main_engine.c` (rond regel 2520, naast `pin.action`): `pin.special = (!fly && win.keys['E']) || win.keys[VK_RCONTROL] || (special_at >= 0 && now - t0 >= special_at && now - t0 < special_at + 0.1);`
  plus een testoptie `--special T` (zoals `--peck`). E is vrij buiten de vliegcamera (daar = omhoog).
* RCtrl zoals het origineel: `WM_KEYDOWN/UP` levert voor beide Ctrl-toetsen `VK_CONTROL`; in de WndProc van `render_gl.c` (regel 21/22)
  extra `if (wp == VK_CONTROL) w->keys[(lp & 0x1000000) ? VK_RCONTROL : VK_LCONTROL] = down;` (bit 24 = extended key = rechts). Wil je RCtrl
  exclusief voor de speciale aanval, zet de aanval dan op `VK_LCONTROL || VK_SHIFT` in plaats van `VK_CONTROL`.
* Lading geven voor tests: de bestaande `--pickup 35 T` (echt oppakpad, `special_charges++`).

### 5.2 Player-velden (`player.h`)

`int special_st, special_prev; float special_t;` — `+0x750`, vorige toetstoestand (voor de loslaatrand), `+0x74c`.
Wissen in de respawn (`player.c` ≈ regel 896, Reset `0x44ab20`: `p->special_st = 0; p->special_t = 0;`) en bij het binden/laden van een level.
**Niet** wissen bij `player_place` (teleport 26): het origineel doet dat alleen in Reset.

### 5.3 `special_update` (`player.c`, nieuw, naast `attack_trigger`)

```c
/* ---- special attack 0x458bf0 (docs/PERSO_SPECIAL.md): action 11 on RELEASE, charge Perso+0x254 ---------------- */
static void special_hit(Player *p);
static void special_update(Player *p, const PlayerInput *in, float dt)
{
    int released = !in->special && p->special_prev; p->special_prev = in->special;   /* 0x467440(11): bit 31, one frame */
    if (released) {
        int state0 = player_state_free(p) && !p->race_char;                      /* Perso+0x21c == 0 */
        if (p->on_ground && state0 && p->special_st == 0 && p->special_charges > 0) {
            float T = anim_len(p, 0x13, 0);                                       /* .ins 86: 12.2 s / 3 = 4.067 s */
            p->special_charges--; p->special_st = 1; p->special_t = 0;
            if (p->move_lock < T) lock_move(p, T);                                /* 0x44cce0(T, 0) = max */
            if (p->invuln_respawn < T) p->invuln_respawn = T;                     /* 0x44cd10: +0x270 = max */
            game_special_fx();                                                    /* 0x47ab90 */
        } else if (!p->race_char) audio_fx(9, NULL, NULL);                        /* subtype 4/5 stay silent */
    }
    if (p->special_st == 1) {
        p->special_t += dt;                                                       /* the start frame counts too */
        if (p->special_t >= 1.5f) { game_cam_shake(2.0f); p->special_st = 2; special_hit(p); }   /* [0x4aa184]; rumble not ported */
    } else if (p->special_st == 2) {
        p->special_t += dt;
        if (anim_len(p, 0x13, 0) <= p->special_t) p->special_st = 0;
    }
}
```
Aanroepen in `player_update` **direct na** `if (p->game_state == 0) return;` (vóór de early returns van scriptactie, raket en klimwand): het
origineel tikt de automaat in elke Perso-toestand en speelt geluid 9 als je in die toestanden loslaat. `p->on_ground` is dan die van het vorige
frame, net als `+0x22c` in het origineel. Geen test op `p->atk` (het origineel test `+0x5b4` niet).

Animatiekeuze (`player.c` ≈ regel 1476, de if-keten van `Perso_AnimState`): direct na `else if (p->hit_anim_t > 0) want = p->hit_anim;`
toevoegen `else if (p->special_st) want = 0x13;` — dus onder de doods-/raceanimaties (prio 6000) en boven bom, aanval, lopen en idle
(prio ≤ 5200). `anim_request` herstart .ins 86 vanzelf (restart 1, en 0x13 kan niet twee keer na elkaar starten). De idle-teller: in het
origineel loopt `0x464500` tijdens de aanval door (de idle-aanvraag verliest alleen op prioriteit); wie dat exact wil roept in deze tak
`idle_anim(p, dt)` aan zonder het resultaat te gebruiken en zet `idling = 1`. Anders reset de port de teller (klein verschil).

### 5.4 De treffer (`player.c` + `enemy.c`)

```c
static void special_hit(Player *p)                                               /* 0x458d89..0x458e28 */
{
    if (!p->enemies) return;
    for (int i = 0, n = 0; i < p->enemies->n && n < 32; i++) {                   /* 0x4c5258[0x4c5324], max 32 */
        Enemy *e = &p->enemies->e[i];
        if (e->removed || !e->attackable || !e->inst->visible) continue;          /* "registered last frame" (vtbl[47]) */
        n++;
        enemy_hit(e, 3.0f /* P+0x94 */, (Vec3){ 0, 0, 0 }, p->pos, 2);            /* vtbl[39](p, 3.0, &0, &p->pos, 2); vtbl[38](3) = ret 4 */
    }
}
```
* **Geen** afstands-, kegel- of hoogtetest. Het doelwittenfilter is een benadering van "Think draaide vorig frame": de port heeft geen
  sectorlijst per vijand; wie het strakker wil doet de PVS-test van `instance_visible` (`render_gl.c`, `r->sec_vis[]` van het vorige frame) op
  `e->inst`. Zonder dat raakt de port alle levende vijanden van het level (onzeker hoeveel dat in de praktijk scheelt; W1A's vijanden zitten in
  een paar gangen).
* `enemy.c`: nieuwe `int enemy_hit(Enemy *e, float dmg, Vec3 dir, Vec3 pt, int kind)` = `vtbl[39]` met trefpunt en soort; de bestaande
  `enemy_take_damage(e, dmg, dir)` blijft als `enemy_hit(e, dmg, dir, pt, 0)`-achtige wrapper voor de pik. Gedrag:
  * type 14: `boss_take_damage(e)` (altijd 1.0); geen sterretje als `kind == 2`, anders `game_hit_star(pt)` als de treffer aangenomen werd;
  * types 4..9 en 13: exact de huidige `enemy_take_damage`, en **als de treffer aangenomen werd** (niet dood, `hit_t <= 0`, `knock_t <= 0`)
    `game_hit_star(pt)` (soort wordt 0 ⇒ ster ook bij de speciale aanval, op Woody's voeten);
  * types 12 en 15 (nog niet geport): niets; 16: toestand 3/4, schade 3, SoundFx 0x41 (niet geport).
  * **Niet** `audio_fx(6)` spelen bij een dode vijand: SoundFx 6 hoort bij de bomexplosie (`0x44d730`, ENEMY.md §8 en BOMB.md); de pik-lus in `player.c`
    (≈ regel 704) doet dat nu wel — dat is een bestaand port-foutje, niet overnemen.
* De pik-lus kan dezelfde `enemy_hit` gebruiken met het echte trefpunt (dash: `p+0x59c`, stormloop: `lerp(a, b, 0.5)`, PERSO_JUMP §3) zodat hij
  ook het sterretje krijgt; de dash geeft soort 1, de stormloop 0.

### 5.5 Effecten (`main_engine.c`, bij `game_pickup_fx`/`fx_update`)

* `FxRec` uitbreiden: `Vec3 v; float size; int flags;` en soorten `3` = emitter `0x47a8d0`, `4` = streep `0x47a790`, `5` = ring `0x47a4c0`,
  `6` = flits `0x475040`, `7` = vonk `0x474e00`. Eén pool van 2000 zoals nu (vol ⇒ overslaan; de emitter zet zijn ringvlag ook als de ring
  niet paste).
* `void game_special_fx(void)` (`0x47ab90`): `fx_new(2.5f, (Vec3){0}, 3)` met `acc = 0`, `flags = 0`.
* `void game_hit_star(Vec3 pt)` (`0x4750e0`): één `fx_new(0.1f, pt, 6)` en 8× `fx_new(0.4f, pt, 7)` met `v = dir`, `size = 20..40`,
  `flags = spin`, volgens §3.4 (basis uit de camerapositie: `U`, `V` = twee eenheidsvectoren ⟂ `normalize(eye − pt)`).
* `fx_update(float dt)` → `fx_update(const float *eye, float dt)` (de aanroep op ≈ regel 2649 geeft `&cam.pos.x`); per soort:
  * 3: §3.1 letterlijk (`acc`, 250/s zolang u < 0.5, `v` per streep, ringvenster 0.7/0.8/0.9 met `flags` bit 0/1/2, ringcentrum = `g_player->pos`);
  * 4: `C = g_player->pos + (0, 43 + 100, 0)` **elk frame**, `k = 1 − u²`, `hud_world_line_img(0, C+v, C+k·v, eye, 2.0, wit, 0.0, u <= 0.7 ? 1 : 1 − (u − 0.7)·3.3333)`;
  * 5: 16 segmenten volgens §3.3 met `hud_world_quad(33, …)`, **twee keer** tekenen (of rgb ×2 met klemmen — twee keer is exact);
  * 6: `hud_world_fx(9, pt, 250·u, 0, wit, 0.4)`;
  * 7: `i = (int)(u·128)`, `kop = pt + v·150·sinf(1.5707963f·i/128)`, `hud_world_line_img(7, pt, kop, eye, 10, wit, 0, 0.1)`,
    `hud_world_fx(8, kop, size, (flags ? (int)(256u) : (int)(511 − 256u)) / 512.0f, wit, 1 − u)`.
  * Kleuren **zonder** 2×-correctie (§3.5): wit = {1,1,1} betekent hier echt c = 1.0 van het origineel. Let op de richting van de rotatie in
    `hud_world_fx` (turns = hoek/512); de zin van de draaiing is visueel niet te onderscheiden bij willekeurige `spin`.
* Volgorde: `fx_update` draait al tussen `hud_world_sprites_begin/end`, na de wereld — zoals `0x470c70`.

### 5.6 `hud.c` / `hud.h`

* Beelden: `fx_slot` uitbreiden met 7, 8, 9 en 33 (de eerstvolgende vrije slots; `GLuint fx[]` in `H` zo nodig vergroten); ze worden dan
  door `common_item` automatisch geladen. Beeld 0 zit al in slot 0.
* `void hud_world_line_img(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b)`
  = `world_line(…, H.fx[fx_slot(image)])` (de bestaande `world_line` doet al additief met `rgb × alpha`).
* `void hud_world_quad(int image, const float v[4][3], const float uv[4][2], const float rgb[4][3], const float alpha[4])`: additief
  (`GL_ONE, GL_ONE`), kleur per vertex `rgb × alpha`, geen culling, diepte-test aan en diepteschrijven uit zoals de andere effecten.
* "Min 1" van de lading (§3.8) in `hud_anim_tick`: `int mcharge; float mcharge_t;` en `prev_charges` in `A`. Detectie naast die van de
  levens: `if (s->charges < A.prev_charges && !A.mcharge) { slide_start(2, 6, -63, 221, 16, 221); plate_start(2, 50, 266, 0, 0, 34, 34);
  pop_start(2, 2, 17.0f, 37.0f, 0.2f); A.mcharge = 1; A.mcharge_t = 0; }`. Tikken alleen als `!A.stage[2] && !A.fly[3].on` (de pickup
  `+0x3a` gaat voor), met `oud = s->charges + 1`, precies de fasen van de tabel in §3.8 (slide → plate → pop 17→37→17 → één frame: pop
  `(2, 1, 17, 0, 0.2)` + `slide_start(2, 6, 16, 221, -63, 221)` + `plate_start(2, 50, 266, 34, 34, 0, 0)` → 1.0 s vasthouden met
  `number_sized(k_anchor[2]…, oud, 17)` → pop → plate → slide). `A.prev_charges = s->charges` elk frame; `hud_anim_reset` wist het.

### 5.7 Testen

* `extract/Data W1A --pickup 35 0.5 --special 2 --pos 652 -1985 -800` met `WOODY_ANIMLOG=1` (verwacht `lanim … -> 19`) en
  `WOODY_SHOTSEQ="out/sp 2.0 0.15 20"` (strepen tot 3.25 s, ringen op 3.75/4.0/4.25 s na het laden), daarna `--special 1` zonder lading ⇒ geluid 9
  (`WOODY_SNDLOG=1`). W1A-vijanden 282/293 moeten op 3.5 s tegelijk sterven, elk met een sterretje op Woody's voeten.
* Buzz: de bestaande W1B-baastest (`--pos -5753 1800 -8901 --yaw -90 --walk 2`) + `--pickup 35` en `--special` in zijn lage fase:
  `BOSS … hit` met 1 punt.
* Onkwetsbaarheid: `WOODY_KILLAT` / een vijandelijke stormloop binnen 4.07 s na de start mag geen hartje kosten; een put wel.

## 6. Zekerheid en open punten

**Zeker (instructie voor instructie)**: §1.2 geheel; de afwezigheid van een bereiktest; `vtbl[38]` = `ret 4` voor alle vijandklassen; alle
`vtbl[39]`-varianten in §2.3; de effectfuncties `0x47ab90`, `0x47a8d0`, `0x47a790`, `0x47a4c0`, `0x4750e0`, `0x475040`, `0x474e00` met hun
constanten; de kleurconversie van `0x481560`; de HUD-functies `0x448300`, `0x462380`, `0x462020`; het animatierecord en de anim-events.

**Onzeker / niet nagemeten**:
1. Of de lijst `world+0x64` (wie Think krijgt) naast de .vis ook op het frustum snijdt (BONUS §3.1 zegt "zichtbare sectoren"); dat bepaalt hoeveel
   vijanden de speciale aanval in het origineel echt raakt. Live te meten met `tools/wtrace.py` op `[0x4c5324]` op het trefmoment.
2. De betekenis van ONE/ONE + voorvermenigvuldiging (§3.5) is uit de code afgeleid, niet visueel vergeleken; ook de gevolgtrekking over
   BONUS §2.4 en `fx_update`.
3. Hoe het origineel een stormloop die al liep afhandelt als de speciale aanval tijdens die stormloop start (§1.5); de controller `0x457a50` en
   de blokkade lopen dan door elkaar.
4. De "klank" van ref 114/115 (alleen gemeten).
5. Baas 16 (W3D): hp en toestanden 3/4/5/7 niet geanalyseerd; baas 15 is zeker immuun voor elke `vtbl[39]`.

### 6.1 Constanten

| adres | waarde | gebruik |
|---|---|---|
| `0x4aa184` | 1.5 | tijd tot de treffer |
| `0x40000000` (imm) | 2.0 | camera-schok (s) |
| `0x4631d6` (P+0x94) | 3.0 | schade |
| `0x463227`/`0x46322d` (P+0xb0/+0xb4) | 1.0 / 1.0 | rumble |
| `0x4b6180 + 0x13·0x1c` | {86,0,0,0}, 5500, 3.0, 1 | animatierecord |
| `0x40200000` (imm, `0x47abc0`) | 2.5 | levensduur emitter |
| `0x4a9014` / `0x4a900c` | 0.5 / 1.0 | emitter: spawnvenster / einde |
| `0x4ab144` / `0x4abd80` | 250 / 0.004 | strepen per s / 1/250 |
| `0x4a9874` | 512 | willekeurige hoek |
| `0x4a964c` / `0x4abd7c` | 400 / −400 | straal van de strepenbol |
| `0x4aa1d8` / `0x4a987c` / `0x4a94b8` | 0.7 / 0.8 / 0.9 | ringmomenten (emitter-u) |
| `0x3f000000` (imm) | 0.5 | levensduur streep en ring |
| `0x4a9010` | 100 | streepmiddelpunt boven `Perso+0x60` (= voeten + 43) |
| `0x4aa3a8` | 3.33333 | uitdoven streepkop na u = 0.7 |
| `0x40000000` (imm, `0x47a8a4`) | 2.0 | halve breedte streep |
| `0x10000` (imm) | bank 0 beeld 0 | streeptextuur |
| `0xe00` / `0x24` | vlaggen | lijnprimitief / indienen additief |
| `0x4aa318` | 4000 | ringstraal per eenheid u |
| `0x4a9760` | 0.2 | ringbreedte in u |
| `[0x5e866c] + 0xef4` | bank 0 beeld 33 (0xef4/0x74) | ringtextuur |
| `0x3ecccccd` (imm) | 0.4 | blauw van de ring-buitenrand; alfa van de flits |
| `0x44` | vlaggen | ring: wereldvertices + additief |
| `0x3dcccccd` (imm) | 0.1 | levensduur flits; alfa vonklijn-kop |
| `0x3ecccccd` (imm, `0x475216`) | 0.4 | levensduur vonk |
| `0x4abcc0` (double) | 1.0 | spin-drempel (`rand·2 > 1`) |
| `0x4a9994` / `0x4a9884` / `0x4abc64` | 20 / 5 / 25 | vonkgrootte 20..40; hoekjitter ±25/512 |
| `0x4a9030` | 50 | hoekjitterbreedte |
| `0x4a9020` | 128 | vonkfase; kleurschaal additief pad |
| `0x4abcb8` | −150 | vonkafstand |
| `0x41200000` (imm) | 10 | halve breedte vonklijn |
| `0x10007` / `0x10008` / `0x10009` | bank 0 beeld 7 / 8 / 9 | snelheidsstreepjes / ster / flits |
| `0x4aa3ac` / `0x4abc90` | 256 / 511 | sterrotatie |
| `0x4ab144` | 250 | flitsgrootte per u |
| `0x4aa308` | 255 | kleurschaal doorzichtig/dekkend pad |
| `0x4ab6d8`, `0x4b3a30`, `0x4b3a34` | 63, 16, 221 | HUD: slide-coördinaten |
| `0x4b3a38`, `0x4b3a3c`, `0x4ab700`, `0x4ab704` | 50, 266, 34, 34 | HUD: plate |
| `0x4b3a88`, `0x4a9994`, `0x3e4ccccd` | 17, 20, 0.2 | HUD: getal-pop 17 → 37, 0.2 s |
