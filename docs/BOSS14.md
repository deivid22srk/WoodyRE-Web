# Vijandklasse 14 – Buzz Buzzard (eindbaas W1B, W2D, W3D, WWS) – Woody.exe

Status: statische analyse van `game/Woody.exe` (`out/disasm_full.txt`, `tools/drange.py`), volledig gelezen van `0x40eb50` t/m `0x410e90`
(ctor tot en met `vtbl[31]`). Floats, jumptabellen en animatierecords zijn met een PE-lezer uit de exe gehaald, niet geraden. Vervolg op
ENEMY.md / ENEMY2.md: de basisklasse `Enemy`, het parameterblok **P**, de hoekregelaar **H**, de gedragingen (Dwalen / Achtervolgen /
Stilstaan), `Enemy::Update 0x41a3e0`, `Enemy_TakeDamage 0x41adc0`, `FindTarget` en `AnimLen(n,k) = 0x436b90` gelden hier ongewijzigd.
"onzeker" = niet statisch te bepalen.

## 0. Samenvatting

* **Klassefabriek** `0x403502`: type 14 → `new(0x24c)` + `0x40eb50(11)` (`push 0xb` op `0x403883`) → **subtype 11**; daarna (zoals bij
  elk type) `vtbl[1]` PostLoad, `vtbl[17]` Reset en `0x407790` (in de wereld hangen) (`0x403e6d..0x403e7a`).
* **Twee varianten** ("modus" `+0x228`), gekozen door het **script via een brievenbus-variabele** (bericht 60): modus **1** = W1B (Buzz in zijn
  vliegmachine, logische records 0..16), modus **2** = W2D / W3D / WWS (records 17..33, "hupsen" met stof). Modus **0** = uit: de klasse
  doet dan helemaal niets behalve de brievenbus lezen.
* **Gevecht** (modus 1): Buzz achtervolgt de speler **hoog** (op de y van zijn plaatsing, W1B 2430) met 900 u/s, schudt 0.25 s boven de speler en
  **valt** dan met zwaartekracht naar beneden (stomp; raakt de speler binnen een kegel = 1 hartje; camera-schok bij de landing). Daarna zweeft hij
  **laag** (grond onder de startpositie + 290, W1B ≈ 1635) en is **alleen dan kwetsbaar**: elke treffer = **1 hp** (ongeacht de schade),
  **5 hp**, 2.1 s rood knipperend "geraakt", daarna weer omhoog. Raakt hij de speler in de lage fase, dan gaat hij ook weer omhoog.
* **Gekoppelde instantie** (bericht 59, W1B: 404 = type 17, model 38): krijgt elke frame **exact de positie en rotatie** van de baas, speelt
  **dezelfde logische animatie** (eigen AnimCtrl, eigen model), knippert mee rood, en krijgt rookpluimen op zijn markers bij de treffers 2..4.
* **Einde**: bij hp ≤ 0 schrijft hij **3** in de brievenbus-variabele (W1B: var 51), stopt zijn geluid en zet modus 0 (bevriest). Het script
  verbergt 405/404 0.5 s later en start de outro. Verder schrijft hij alleen **−1** (bevestiging van een commando), nooit 1 of 2.
* **HUD**: elke frame `0x4484d0(1, (int)hp, (int)P+0x34)` = baas-levensbalk (HUD_TEXT.md §4.4) met 5 bolletjes.
* **Geen** projectielen, geen bommen, geen TRAJ/pad, geen `vtbl[58]`, geen sterf-deeltjes, geen fade, geen verwijdering (`+0x10c` blijft 0).

## 1. Vtable `0x4a98a8` (58 slots) t.o.v. de Enemy-basis `0x4a9fbc`

Slots 0..57 als in ENEMY.md §1.1. Na slot 57 staan drie floats (`0x4a9990` = 1/60, `0x4a9994` = 20.0, `0x4a9998` = 500.0) en dan de
AnimCtrl-vtable `0x4a999c` (`[0]` = `0x4108e0`, `[1]` `0x40d7f0`, `[2]` `0x436b70` Request, `[3]` `0x436a50` Tick, `[4]` `0x436a40` Reset).
Er is dus **geen slot 58 (Fire)**.

| slot | off | klasse 14 | basis | betekenis |
|---|---|---|---|---|
| 0 | +0x00 | `0x40ebc0` | `0x419d30` | scalar-dtor → `0x40ebe0`: beide AnimCtrl's (`+0x1c4`, `+0x1c8`) `vtbl[1](1)`, dan `0x419d50` |
| 1 | +0x04 | **`0x40ec50`** | `0x419e30` | PostLoad (§3.2) |
| 3 | +0x0c | = `0x41a320` | | Think: Update alleen als 3D-afstand tot de camera `< P+0xc0` (**3000**, niet overschreven) of `hp ≤ 0`; niet tijdens een cinematic |
| 17 | +0x44 | **`0x40ed90`** | `0x41a010` | Reset (§3.3) |
| 22 | +0x58 | **`0x410070`** | `0x41a740` | berichten 59 / 60, rest → `Enemy::HandleMsg` (§7) |
| 23 | +0x5c | `0x45b340` (`return 8`) | `0x41ad40` (1 of 0x10) | geen aanroeper met zekerheid gevonden: onzeker |
| 26 | +0x68 | **`0x40fde0`** | `0x430010` | render-kleur: rood/wit knipperen in toestand 9 en 12 (§8) |
| 31 | +0x7c | **`0x410cf0`** | `0x40c1e0` | **Touch** = kegeltest (§6.1) |
| 38 | +0x98 | `0x4078a0` (leeg) | `0x40c3b0` (leeg) | |
| 39 | +0x9c | **`0x40fe90`** | `0x41adc0` | **TakeDamage** (§6.2) |
| 40 | +0xa0 | = `0x41ae20` | | Blast: gaat via `vtbl[39]` ⇒ ook maar 1 hp |
| 43 | +0xac | **`0x410900`** | `0x41a4e0` | hoogteregeling / val (§5) |
| 45 | +0xb4 | **`0x410170`** | purecall | animatiekeuze (§4.2) |
| 47 | +0xbc | = `0x41a4d0` | | altijd als actor/aanvalsdoel geregistreerd |
| 51 | +0xcc | **`0x4105e0`** | purecall | `return 4.0f` (`0x4a94c0`); alleen gebruikt door de fade van `Enemy::Update`, die nooit start (`+0x15c` blijft 0) |
| 52 | +0xd0 | **`0x40eec0`** | `0x41a3e0` | Update / toestandsmachine (§3A) |
| 53 | +0xd4 | **`0x4105f0`** | `0x40d840` | duur per toestand (§4.3) |
| 54 | +0xd8 | **`0x40fe80`** | `0x4078b0` (0) | `return +0x234` = **gekoppelde instantie** |
| 55 | +0xdc | = `0x41b1b0` | | **meeslepen**: als `vtbl[54]()` ≠ 0 ⇒ positie en rotatie naar de gekoppelde instantie kopiëren (§9.1). Correctie op ENEMY.md §1.1 ("cel bijwerken") |
| 57 | +0xe4 | = `0x41b000` | | sterf-effect – wordt in deze klasse **nooit** aangeroepen |

Alle andere slots = basis (o.a. 32 straal = `P+4`, 33 hoogte = `P+0x28`, 34 positie-pointer, 37 waarschuwing `ret 4`, 41 = `0x4078a0` leeg,
48 FindTarget).

## 2. Velden (size 0x24c)

### 2.1 Eigen velden

| off | type | init | betekenis | schrijvers / lezers |
|---|---|---|---|---|
| 0x1c0 | int | Reset 0 | **toestand** 0..12 (13 alleen in de anim-/duurtabel) | |
| 0x1c4 | AnimCtrl* | ctor 0; PostLoad | eigen AnimCtrl (`0x4108c0(this)`, 0x54 B) | |
| 0x1c8 | AnimCtrl* | ctor 0 | AnimCtrl van de **gekoppelde instantie** (`0x4108c0(link)`, bericht 59) | `0x410126` |
| 0x1d0 | float | – | tijd dat de speler stilstaat (+= dt in toestand 3, 0 als hij beweegt) | **geen lezer** |
| 0x1d4 | float | – | schudtimer toestand 6 (`P+0xa4` = 0.25) | proloog `-= dt` zolang ≥ 0 |
| 0x1d8 | float | Reset 0 | wachttimer na de landing, toestand 8 (`P+0x9c` = 0.3) | idem |
| 0x1e0 | float | – | juich-timer toestand 10/11 (4.0) | |
| 0x1e4 | float | – | timer toestand 1 (`P+0x38` = 1.0 of 0) | proloog `-= dt` zolang ≥ 0 |
| 0x1e8 | int | – | stapteller 0..3 van het schudden (toestand 6) | |
| 0x1ec | vec3 | – | spelerpositie vorige frame | toestand 2 zet 0 |
| 0x1f8 | vec3 | – | spelerpositie deze frame | |
| 0x204 | vec3 | – | bewaard thuispunt tijdens het terugwijken | |
| 0x210 | float | – | bewaarde `P+0x1c` (leash) tijdens het terugwijken | |
| 0x214 | float | PostLoad | **hoge zweefhoogte** = y van het thuispunt = y van de plaatsing (W1B **2430**) | |
| 0x218 | float | PostLoad | **lage zweefhoogte** = grond onder de plaatsing (`0x41a2a0`) + `P+0x88` (290). W1B: wereldvloer onder (−7031, −8863) = **1344.8** ⇒ **1634.8** (met `tools/gelparse.py` over de wereldpolygonen bepaald, zonder instantie-hulls) | |
| 0x21c | float | Reset 0 | accumulator voor de vaste 60-Hz-stappen van toestand 6 | |
| 0x224 | u8 | Reset 1 | **"hoog"**: 1 = hoge fase (onkwetsbaar), 0 = lage fase (kwetsbaar) | |
| 0x228 | int | PostLoad 0 | **modus** 0 = uit, 1 = W1B-variant, 2 = W2D/W3D/WWS-variant | `0x410be0` |
| 0x22c | int | Reset 0 | geen lezer | |
| 0x230 | int | PostLoad 0 | **brievenbus-variabele** (script-var-id, `& 0xffffff`), bericht 60 | |
| 0x234 | Inst* | ctor 0 | **gekoppelde instantie** (bericht 59) | |
| 0x23c | u8 | Reset 1 | modus 2: richting van het hupsen (1 = omlaag) | |
| 0x240 | float | Reset 0 | modus 2: hups-diepte 0..150 | |
| 0x244 | float | Reset 150.0 (`0x43160000`) | modus 2: maximale hups-diepte | |
| 0x248 | Bron (4 B) | Reset `0x468e10` | geluidsbron voor de lus (SOUND.md §5: `0x468e40` actief, `0x468e50` start, `0x468e20` stop) | |

### 2.2 Gebruikte basisvelden

`+0xc` positie (**de klasse verplaatst deze zelf**, §5), `+0x114` dt, `+0x118` P, `+0x11c` actief gedrag, `+0x120` H, `+0x128` startpositie,
`+0x134` thuispunt (`+0x138` = y), `+0x150` hp, `+0x158` hit-timer, `+0x15c` sterf-timer (altijd 0), `+0x160` Dwalen, `+0x164` Achtervolgen,
`+0x168` Stilstaan (**geen** Pad: `+0x16c` blijft 0, TRAJ wordt niet gebruikt), `+0x174` vlaggen (bit 1 op de grond, **bit 4 = zwaartekracht/val aan**),
`+0x178` grondprobe.

### 2.3 Parameterblok P voor subtype 11 (`0x41d510`, case `0x41daa6`)

| P+ | waarde | modus 1 (Reset) | modus 2 (Reset) | gebruik in deze klasse |
|---|---|---|---|---|
| 0x00 | 200 | **400** | **800** | valversnelling (basis-grondvolger tijdens de val) |
| 0x04 | **240** | | | straal (`vtbl[32]`; gebruikt door andere code, niet door de klasse) |
| 0x08 | **600** | **400** | **390** | loopsnelheid (Dwalen, terugwijken, H-snelheid in toestand 10) |
| 0x0c | **900** | **900** | **400** | rensnelheid (Achtervolgen); modus 2 ook hups-snelheid |
| 0x10 | **π/4** | | | draaisnelheid Dwalen / terugwijken |
| 0x14 | **π** | | | draaisnelheid Achtervolgen, toestand 5 |
| 0x18 | π/2 | | | niet gelezen |
| 0x1c | **1000** | | | leash rond het thuispunt (3D voor subtype ≥ 9); tijdens terugwijken 1.0 |
| 0x20 | **4600** | | | zichtafstand FindTarget (3D) |
| 0x24 | **3000** | | | max. \|dy\| FindTarget |
| 0x28 | **150** | | | hoogte (`vtbl[33]`; h/2 = 75 voor probe en cel) |
| 0x2c / 0x30 | **15000** | | | max. afstap / opstap ⇒ geen randtest |
| 0x34 | **5** | | | **levenspunten** (en HUD-maximum) |
| 0x38 | 1.0 | | | duur toestand 1 in de hoge fase |
| 0x3c | 1.0 | | | **schade aan de speler** (kegel-contact) |
| 0x78 | **900** | | | verticale snelheid van de hoogteregeling |
| 0x7c | **250** | | | **kegelhoogte** (onder `pos`); treffpunt op `pos.y − 125` |
| 0x80 | **150** | | | **kegelstraal** bovenaan / afstand treffpunt |
| 0x88 | **290** | | | lage zweefhoogte boven de grond |
| 0x9c | **0.3** | | | wachttijd na de landing (toestand 8) |
| 0xa4 | **0.25** | | | schudduur (toestand 6) |
| 0xa8 | **20** | | | schud-amplitude |
| 0x84, 0xa0 | 2.0, 2.0 | | | gezet, **geen lezer** |
| 0xc0 | 3000 | | | activeringsafstand tot de camera (Think) |

Overige velden = defaults (ENEMY.md §2.3; o.a. `P+0x44 = 600` terugslagfactor). Let op: Reset herschrijft `P+0/8/0xc` alleen als de modus 1 of 2 is;
de Reset uit de fabriek (modus 0) laat 200/600/900 staan.

## 3. Constructie, PostLoad, Reset, brievenbus

### 3.1 Ctor `0x40eb50(subtype)` en dtor

```c
Boss14 *Boss14_ctor(Boss14 *e, int subtype) {        /* 0x40eb50 */
    Enemy_ctor(e);                                    /* 0x419cd0 */
    e->vtbl = 0x4a98a8;  Npc_SetCategory(e, 2);  Npc_SetSubtype(e, subtype /*11*/);   /* 0x40c360, 0x40c380 */
    e->anim = NULL; e->linkAnim = NULL; e->link = NULL;   /* +0x1c4, +0x1c8, +0x234 */
    return e;
}
void Boss14_dtor(Boss14 *e) {                         /* 0x40ebe0 */
    if (e->anim) e->anim->vtbl[1](1);  if (e->linkAnim) e->linkAnim->vtbl[1](1);  Enemy_dtor(e);  /* 0x419d50 */
}
```

### 3.2 PostLoad `0x40ec50`

```c
void Boss14_PostLoad(Boss14 *e) {
    Enemy_PostLoad(e);                                /* 0x419e30: P, H, Sensor, Fall, start/thuis = pos */
    e->anim   = new AnimCtrl14(e);                    /* 0x4108c0: 0x4369f0(e) + vtable 0x4a999c */
    e->wander = new Dwalen(e, 1, &e->home);           /* +0x160, 0x41bf30: leash aan rond +0x134 */
    e->yHigh  = e->home.y;                            /* +0x214 */
    e->yLow   = GroundBelow(e) + P->+0x88;            /* +0x218 = 0x41a2a0() + 290 */
    e->chase  = new Achtervolgen(e, 0);               /* +0x164, 0x41bbf0 */
    e->still  = new Stilstaan(e);                     /* +0x168, 0x41b710 */
    e->mailVar = 0;  e->mode = 0;                     /* +0x230, +0x228 */
}
```
Geen Pad-gedrag en geen `[0x4c5330]++` (telt niet mee als "vijand in het level").

### 3.3 Reset `vtbl[17]` = `0x40ed90`

```c
void Boss14_Reset(Boss14 *e) {
    Enemy_Reset(e);            /* 0x41a010: pos = thuis = start, hp = P+0x34, hitT = deathT = 0, typewoord |= 0x400,
                                  niet in de wereld ⇒ 0x407790 (wordt dus weer zichtbaar!), vlag 4 aan, msgmask 0x10 wissen */
    e->behav = e->wander;  e->wander->vtbl[6]();  Wander_Start(e->wander);   /* 0x41c160: draaisnelheid P+0x10 */
    e->flags &= ~4;                                    /* geen val */
    e->state = 0;  e->t1d8 = 0;  e->deathT = 0;  e->high = 1;  e->u22c = 0;  e->acc = 0;
    e->bobDown = 1;  e->bobMax = 150.0f;  e->bob = 0;
    e->hp = P->+0x34;                                  /* 5 */
    smokeOn[0] = smokeOn[1] = smokeOn[2] = 0;          /* bytes 0x5e857c..e: rookpluimen uit (§9.2) */
    e->anim->vtbl[4]();  if (e->linkAnim) e->linkAnim->vtbl[4]();   /* AnimCtrl-reset (0x436a40) */
    switch (e->mode) {
    case 1: P->+0x0c = 900; P->+0x08 = 400; P->+0x00 = 400; break;     /* 0x40ee7d */
    case 2: P->+0x0c = 400; P->+0x08 = 390; P->+0x00 = 800; break;     /* 0x40ee49 */
    }
    SoundSrc_Init(&e->src);                            /* 0x468e10(this+0x248) */
}
```
Reset wordt aangeroepen door de fabriek (bericht 1200), door **elke modus-wissel** (§3.4) en door bericht `11 [inst, 4]` (basis; W2D/W3D
gebruiken dat om de baas terug te zetten).

### 3.4 Brievenbus `0x410be0` (elke Update, als eerste)

```c
bool Boss14_CheckCommand(Boss14 *e) {
    int v = *GetVar(e->mailVar);                       /* 0x443cd0 */
    if (v == 1)      { if (e->mode != 1) { e->mode = 1; e->vtbl[17](); } SetVar(e->mailVar, -1); return true; }
    else if (v == 2) { if (e->mode != 2) { e->mode = 2; e->vtbl[17](); } SetVar(e->mailVar, -1); return true; }
    SetVar(e->mailVar, -1);                            /* 0x443ca0; óók bij elke andere waarde (0, 3, -1) */
    return false;                                      /* resultaat wordt door Update niet gebruikt */
}
```
* Het script schrijft **1 of 2** (commando "start in modus 1/2"); de baas bevestigt met **−1**. Een commando voor de modus die al actief is doet
  niets (geen Reset). SetVar wekt de watchers vóór het schrijven (`0x443ce0`), dus elke Update wekt de script-objecten die op de var wachten.
* Zolang bericht 60 niet is ontvangen is `mailVar = 0`: de baas leest dan **var 0** en schrijft er −1 in (alleen als hij in die eerste frames al
  geüpdatet wordt, d.w.z. binnen 3000 van de camera staat).
* De brievenbus wordt alleen gelezen als Think Update aanroept (camera < 3000, geen cinematic).

## 3A. Update `vtbl[52]` = `0x40eec0` (jumptabel `0x40fd9c`, 13 toestanden)

```c
void Boss14_Update(Boss14 *e) {
    Boss14_CheckCommand(e);                                        /* 0x410be0 */
    if (e->mode == 0) return;                                      /* 0x40eee8 → 0x40fd8a: ook geen HUD */
    Enemy_Update(e);   /* 0x41a3e0: gedrag-tick, vtbl[43] hoogte (§5), vtbl[44] rotatie, vtbl[45] animatie (§4), vtbl[55] meeslepen (§9.1) */
    if (e->mode == 1 && e->link && e->link->smoke) e->link->smoke->on = 1;   /* 0x40ef08: type-17-rook (+0x10c)->+0xc; W1B: smoke == 0 */
    if (e->state == 3) e->stillT += dt;
    if (e->t1d4 >= 0) e->t1d4 -= dt;   if (e->t1d8 >= 0) e->t1d8 -= dt;   if (e->t1e4 >= 0) e->t1e4 -= dt;
    Actor *t;                              /* FindTarget = vtbl[48](1, 0): zicht 4600 (3D), |dy| < 3000 */
    switch (e->state) {
    case 0: /* NAAR ZWEEFHOOGTE / DWALEN START  0x40efb5 */
        e->home.y = e->high ? e->yHigh : e->yLow;
        e->flags &= ~4;
        e->behav = e->wander;  e->wander->vtbl[6]();  Wander_Start(e->wander);
        e->t1e4 = e->high ? P->+0x38 /*1.0*/ : 0;   e->state = 1;
        break;
    case 1: /* DWALEN  0x40f045 */
        if (e->t1e4 < 0) e->state = 2;
        break;
    case 2: /* OPMERKEN  0x40f06b */
        if (!(t = FindTarget(1,0))) { e->state = 0; break; }
        e->flags &= ~4;  H->speed = P->+0x08;                      /* direct overschreven door Chase_Start */
        e->behav = e->chase;  e->chase->vtbl[6]();  Chase_Start(e->chase, t);   /* 0x41bc80: snelheid P+0xc (900) direct, draaien P+0x14 (π) */
        e->stillT = 0;  e->plPrev = (0,0,0);  e->state = 3;
        break;
    case 3: /* ACHTERVOLGEN  0x40f0e9 */
        if (e->high && e->home.y - e->pos.y >= 20.0f) break;        /* 0x4a9994: eerst (bijna) op de hoge zweefhoogte */
        if (!(t = FindTarget(1,0))) { e->state = 0; break; }
        e->plPrev = e->plCur;  e->plCur = t->pos;
        if (!PlayerStill(&e->plPrev, &e->plCur)) e->stillT = 0;     /* 0x410c60: |Δ| < 1.0 */
        if (!e->high) {
            if (e->vtbl[31](1, 0)) {                                /* kegel-contact §6.1 */
                vec3 d = normalize_xz(t->pos - e->pos);
                vec3 pt = { e->pos.x + d.x*P->+0x80, e->pos.y - P->+0x7c*0.5f, e->pos.z + d.z*P->+0x80 };   /* pos + d·150 − (0,125,0) */
                if (t->vtbl[39](e, P->+0x3c /*1.0*/, &d, &pt, 0)) { e->state = 10; break; }
                SoundFx(e->mode == 1 ? 41 : 46, 0);                 /* 0x40f291 */
                e->high = 1;  e->state = 0;  break;                 /* terug omhoog */
            }
            if (dist_xz(e->pos, e->plCur) < 150.0f) {               /* 0x4a9754; speler onder/naast hem maar niet in de kegel ⇒ TERUGWIJKEN */
                vec3 a = e->home - t->pos;  float L = len3(a);      /* 3D-lengte, maar alleen x/z worden gebruikt */
                e->homeSave = e->home;
                e->home = (vec3){ t->pos.x + a.x/L*1300.0f, e->yLow, t->pos.z + a.z/L*1300.0f };   /* 0x4a9860 */
                e->behav = e->wander;  e->wander->vtbl[6]();  Wander_Start(e->wander);  Wander_SetLeash(e->wander, 1, &e->home);
                e->leashSave = P->+0x1c;  P->+0x1c = 1.0f;  e->state = 4;  break;
            }
        }
        if (dist_xz(e->pos, e->plCur) < 150.0f) {                   /* 0x40f407 */
            e->behav = e->still;  e->t1e8 = 0;  e->t1d4 = P->+0xa4 /*0.25*/;
            if (e->high) e->state = 6;                              /* STOMP-aanloop */
        }
        break;
    case 4: /* TERUGWIJKEN  0x40f47d */
        if (!(t = FindTarget(1,0))) goto restore;
        e->plPrev = e->plCur;  e->plCur = t->pos;
        if (!PlayerStill(&e->plPrev, &e->plCur)) goto restore;      /* speler beweegt ⇒ afbreken */
        Wander_SetLeash(e->wander, 1, &e->home);
        if (dist3(e->pos, e->home) < 150.0f) e->state = 5;
        break;
    case 5: /* WACHTEN OP HET WIJKPUNT  0x40f558 */
        if (!(t = FindTarget(1,0))) goto restore;
        e->plPrev = e->plCur;  e->plCur = t->pos;
        if (!PlayerStill(&e->plPrev, &e->plCur)) goto restore;
        e->behav = e->still;  H_SetTurnSpeed(H, P->+0x14 /*π*/);  H_TurnTo(H, e->pos, t->pos, 0);   /* 0x41b940, 0x41ba60 */
        if (AngArc(H->target, H->angle > 0 ? 1.0f : 0.0f) != 0) H_Tick(H, dt);   /* 0x4401c0 – letterlijk zo (vermoedelijk bedoeld:
                                                                                     boog(doel, hoek)); in de praktijk draait hij altijd */
        break;
    restore: /* 0x40f66f */
        P->+0x1c = e->leashSave;  e->home = e->homeSave;  Wander_SetLeash(e->wander, 1, &e->home);  e->state = 2;
        break;
    case 6: /* SCHUDDEN BOVEN DE SPELER  0x40f6bb */
        if (!(t = FindTarget(1,0))) { e->state = 2; break; }
        if (e->t1d4 <= 0) { e->state = 7; e->flags |= 4; break; }   /* zwaartekracht aan ⇒ vallen */
        e->acc += dt;  if (e->acc <= 1/60.0f) break;                /* 0x4a9990: vaste stap van 1/60 s */
        vec3 p = e->pos;  vec3 d = normalize_xz(H_MoveDir(H));      /* 0x41b860 */
        int n = e->t1e8++;
        switch (n) { case 0: s = -20; break;  case 1: case 2: s = +20; break;  case 3: e->t1e8 = 0; s = -20; break; }   /* tabel 0x40fdd0, P+0xa8 */
        p += d * s;                                                  /* patroon −20, +20, +20, −20 langs de bewegingsrichting */
        if (e->t1e8 & 1) { vec3 w = t->pos - p; w.y = 0; if (len_xz(w) != 0) p += w * 0.1f; }   /* 0x4a9008: 10 % naar de speler */
        e->pos = p;
        while (e->acc > 1/60.0f) e->acc -= 1/60.0f;                  /* maximaal één stap per frame, overschot vervalt */
        break;
    case 7: /* VALLEN (STOMP)  0x40f909 */
        if (!(t = FindTarget(1,0))) { e->state = 0; break; }
        if (e->flags & 1) {                                          /* geland (basis-grondvolger, §5) */
            CameraShake(cam, 1.5f);                                  /* 0x41fbb0 */
            e->high = 0;  e->t1d8 = P->+0x9c /*0.3*/;  e->state = 8;
            if (e->mode == 2) Dust(&(vec3){pos.x, pos.y - 50, pos.z}, &(vec3){0,1,0}, 0, 1.5f, 6.0f);   /* 0x476140 */
            SoundFx(e->mode == 1 ? 40 : 45, 0);                      /* 0x40f9e5 */
        }                                                            /* geen break: loopt door */
        H_TurnTo(H, e->pos, t->pos, 0);  H_Tick(H, dt);
        e->pos.x += (t->pos.x - e->pos.x) * 0.01f;  e->pos.z += (t->pos.z - e->pos.z) * 0.01f;   /* 0x4a94f8: 1 % per FRAME */
        if (e->vtbl[31](1, 0)) {
            vec3 d = normalize_xz(t->pos - e->pos), pt = e->pos + d*150 − (0,125,0);
            if (t->vtbl[39](e, 1.0f, &d, &pt, 0)) { e->state = 10; break; }
            SoundFx(e->mode == 1 ? 41 : 46, 0);  CameraShake(cam, 1.5f);   /* 0x40fb78, 0x40fb88 */
            e->high = 0;  e->t1d8 = 0.3f;  e->state = 8;
        }
        break;
    case 8: /* NA DE LANDING  0x40fbb5 */
        if (e->t1d8 < 0) e->state = 0;                               /* lage fase begint (high == 0) */
        break;
    case 9: /* GERAAKT  0x40fbd1 */
        if (e->hp <= 0) { e->state = 12; break; }
        e->hitT -= dt;  e->behav = e->still;
        if (e->hitT < 0) { e->high = 1; e->state = 0; }               /* 0x40fd4e: weer omhoog */
        break;
    case 10: /* SPELER VERSLAGEN start  0x40fcb1 */
        if (e->mode == 1) { SoundFx(43, 0); SoundSrc_Stop(&e->src, 0, 39); } else { SoundFx(48, 0); SoundSrc_Stop(&e->src, 0, 44); }
        e->t1e0 = 4.0f;  e->state = 11;  e->behav = e->still;  H_SetSpeed(H, P->+0x08, 1);   /* 0x41b9d0 */
        /* fallthrough */
    case 11: /* JUICHEN  0x40fd2f */
        if ((e->t1e0 -= dt) <= 0) { e->high = 1; e->state = 0; }
        break;
    case 12: /* VERSLAGEN  0x40fc27 */
        e->behav = e->still;  e->flags |= 4;
        if (e->hitT >= 0) e->hitT -= dt;
        SetVar(e->mailVar, 3);                                        /* 0x40fc6f: ⇒ script "baas verslagen" */
        SoundSrc_Stop(&e->src, 0, e->mode == 1 ? 39 : 44);            /* 0x468e20 */
        e->mode = 0;                                                  /* vanaf de volgende frame doet Update niets meer */
        break;
    }
    HUD_BossBar(1, (int)e->hp, (int)P->+0x34);                        /* 0x40fd82: 0x4484d0 op [0x5d7b44] */
}
bool PlayerStill(vec3 *a, vec3 *b) { return len3(*a - *b) < 1.0f; }  /* 0x410c60 */
```

### 3A.1 Toestanden in één oogopslag

| # | code | naam | gedrag | uit |
|---|---|---|---|---|
| 0 | `0x40efb5` | naar zweefhoogte | Dwalen | → 1 |
| 1 | `0x40f045` | dwalen | Dwalen | `t1e4 < 0` → 2 (hoog: na 1.0 s, laag: volgende frame) |
| 2 | `0x40f06b` | opmerken | → Achtervolgen | doel → 3, anders → 0 |
| 3 | `0x40f0e9` | achtervolgen (900 u/s, draaien π) | Achtervolgen | hoog: xz < 150 → **6**; laag: kegel-contact → speler geraakt → 0 (hoog) of 10; xz < 150 zonder contact → **4**; geen doel → 0 |
| 4 | `0x40f47d` | terugwijken naar een punt 1300 van de speler | Dwalen (leash 1.0) | speler beweegt → 2; binnen 150 van het punt → 5 |
| 5 | `0x40f558` | stil hangen, naar de speler draaien | Stilstaan | speler beweegt / weg → 2 |
| 6 | `0x40f6bb` | schudden boven de speler (0.25 s) | Stilstaan | → 7 (val aan) |
| 7 | `0x40f909` | vallen, 1 %/frame naar de speler schuiven | Stilstaan | geland of contact → 8 (laag); speler dood → 10 |
| 8 | `0x40fbb5` | na de landing (0.3 s) | Stilstaan | → 0 (laag) |
| 9 | `0x40fbd1` | geraakt (2.1 s) | Stilstaan | hp ≤ 0 → 12; `hitT < 0` → 0 (hoog) |
| 10/11 | `0x40fcb1` / `0x40fd2f` | speler verslagen, 4.0 s | Stilstaan | → 0 (hoog) |
| 12 | `0x40fc27` | verslagen: var := 3, modus := 0 | Stilstaan | (bevroren) |

**Cyclus**: 0 → 1 (1 s) → 2 → 3 (hoog achtervolgen) → 6 (0.25 s schudden) → 7 (val) → 8 (0.3 s) → 0 → 1 → 2 → 3 (laag achtervolgen;
kwetsbaar) → { pik ⇒ 9 (2.1 s) ⇒ 0 hoog | contact ⇒ speler −1 ⇒ 0 hoog | speler staat stil dichtbij ⇒ 4/5 terugwijken tot hij beweegt ⇒ 2 }.
De juiste "val-in"-voorwaarde voor de stomp is alleen **xz-afstand < 150** (plus ≤ 20 onder de hoge zweefhoogte); er is geen zichtlijn- of hoektest.
Frame-afhankelijk (letterlijk): de 1 %-schuif in toestand 7, de val (per frame, §5) en de maximaal één schudstap per frame in toestand 6.

## 4. Animatie

### 4.1 AnimCtrl `0x4108c0` en records

`0x4108c0(inst)` = `0x4369f0(inst)` + vtable `0x4a999c`; record-getter `0x4108e0(n)` = `0x4b1958 + n·0x1c` (34 records, eindigt precies waar de
type-12-tabel `0x4b1d10` begint), formaat `{int sub[4]; int prio; float speed; u8 restart}`; alle prio 1000, restart 1. `AnimLen(n) = duur(sub[0]) / speed`
van het **baasmodel** (`AnimCtrl+0x4c` = de baas). Duur in s van W1B-model 37 (43 anims; opgemeten uit de .ins): 5 = 5.9, 6 = 1.0, 7 = 0.5, 8 = 1.8,
9 = 0.9, 10 = 0.1, 11 = 0.9, 12 = 1.3, 13 = 6.6, 14 = 2.1, 15 = 2.1, 16 = 4.3, 17 = 5.9, 18 = 4.6; 0/1 = 59.6/54.8 (cinematic-sporen), 2/3/4 = 10.0.

| n | sub[] | speed | modus | gebruikt in toestand | AnimLen W1B |
|---|---|---|---|---|---|
| 0 | 5,5,5,5 | 3 | 1 | – (ongebruikt) | 1.97 |
| 1 | 6,7,7,7 | 3 | 1 | – | 0.33 |
| **2** | 7,7,7,7 | 3 | 1 | **bewegen**: dwaal-acties 5,6,7,9,10; toestanden 2, 3 | 0.167 |
| 3 | 8,5,−1,−1 | 3 | 1 | – | 0.6 |
| **4** | 9,10,10,10 | 3 | 1 | **6, 7** schudden + vallen | 0.3 (daarna 10 in lus) |
| 5 | 11,5,−1,−1 | 3 | 1 | – | 0.3 |
| 6 | 11,5,−1,−1 | 3 | 1 | toestand 13 (bestaat niet in Update) | 0.3 |
| **7** | 13,13,13,13 | 2 | 1 | **10, 11** speler verslagen | 3.3 |
| **8** | 14,5,−1,−1 | 1 | 1 | **9** geraakt; ook de hit-timer | **2.1** |
| **9** | 18,−1,−1,−1 | 1 | 1 | **12** verslagen (eenmalig, laatste beeld blijft) | 4.6 |
| **10/11/12** | 15,5 / 16,5 / 17,5 | 3 | 1 | **idles**: dwaal-actie 0/3 ⇒ 10, 1/4 ⇒ 11, 2 ⇒ 12 (toestanden 0, 1, 4) | 0.7 / 1.43 / 1.97 |
| 13, 14 | 15,5 / 17,5 | 3 | 1 | – | |
| **15** | 12,12,12,12 | 3 | 1 | **8** na de landing | 0.43 |
| **16** | 16,17,15,16 | 1.5 | 1 | **5** hangen op het wijkpunt | 2.87 |
| 17 | 19×4 | 3 | 2 | – | |
| **18/19** | 20,21,21,21 / 21×4 | 3 | 2 | 19 = bewegen (als 2) | |
| 20 | 22,19 | 3 | 2 | – | |
| **21** | 23,24,24,24 | 3 | 2 | 6, 7 | |
| 22 / **23** | 25,19 / 25,19 | 3 | 2 | 23 = toestand 13 | |
| **24** | 27×4 | 2 | 2 | 10, 11 | |
| **25** | 28,19 | 1 | 2 | 9 geraakt | |
| **26** | 32 | 1 | 2 | 12 verslagen | |
| **27/28/29** | 29,19 / 30,19 / 31,19 | 3 | 2 | idles (als 10/11/12) | |
| 30, 31 | 29,19 / 31,19 | 3 | 2 | – | |
| **32** | 25,26,26,26 | 3 | 2 | 8 | |
| **33** | 30,31,29,30 | 1.5 | 2 | 5 | |

Modus 1 gebruikt dus alleen de .ins-animaties **5..18**, modus 2 alleen **19..32**. De cinematic-sporen 0/1 (wortel op y ≈ 5826 bij W1B) en 2..4, 33..42
worden door de klasse **nooit** aangevraagd; ze zijn voor scripts/cinematics. De "welke beweging is het"-namen hierboven komen uit het gebruik, niet uit
het beeld (onzeker).

### 4.2 Keuze `vtbl[45]` = `0x410170` (tabellen `0x410574` per toestand, `0x4105ac` per dwaal-actie + 1)

```c
void Boss14_Anim(Boss14 *e) {
    int n = -1;  bool m1 = (e->mode == 1);
    switch (e->state) {
    case 0: case 1: case 4: {                                   /* 0x410190: actie = Dwalen+0x50 (0x41c630) */
        int a = e->wander->action;
        if (a == 0 || a == 3) n = m1 ? 10 : 27;  else if (a == 1 || a == 4) n = m1 ? 11 : 28;  else if (a == 2) n = m1 ? 12 : 29;
        else if (a == 5 || a == 6 || a == 7 || a == 9 || a == 10) n = m1 ? 2 : 19;   /* a == -1 of 8: niets */
        break; }
    case 2: case 3: if (e->chase->running /*+0x30*/ >= 0 && e->chase->running <= 1) n = m1 ? 2 : 19;  break;   /* 0x4102a5: altijd */
    case 5:  n = m1 ? 16 : 33;  break;         case 6: case 7:   n = m1 ? 4 : 21;  break;
    case 8:  n = m1 ? 15 : 32;  break;         case 9:           n = m1 ? 8 : 25;  break;
    case 10: case 11: n = m1 ? 7 : 24;  break; case 12:          n = m1 ? 9 : 26;  break;
    case 13: n = m1 ? 6 : 23;  break;
    }
    if (n >= 0) { e->anim->Request(n);  if (e->linkAnim) e->linkAnim->Request(n); }   /* zelfde recordnummer voor beide */
    e->anim->Tick(dt);  if (e->linkAnim) e->linkAnim->Tick(dt);
    if (e->state != 10 && e->state != 11 && e->state != 12) {                /* geluidslus */
        SoundSrc_Active(&e->src);                                            /* 0x468e40 */
        SoundSrc_Play(&e->src, 0 /*2D*/, m1 ? 39 : 44, [0x5e48c8], -1.0f);   /* 0x468e50 */
    }
}
```
Omdat `vtbl[45]` in `Enemy::Update` vóór de switch loopt, hoort de animatie bij de toestand van het **vorige** frame. Na de dood (modus 0) wordt er niet
meer ge-tickt; de instantieklok speelt anim 18 (record 9) verder uit en houdt het laatste beeld vast. Of record 9 nog vóór het bevriezen echt start
hangt af van de prio-/wachtrijregels van `0x436a50` (ENEMY.md §8.6; gelijke prio) – vermoedelijk wel, niet nagelopen.

### 4.3 Duur `vtbl[53]` = `0x4105f0` (tabellen `0x410858`, `0x410890`; argument genegeerd)

Zelfde indeling als §4.2 maar met `AnimLen(n, 0)`: 0/1/4 per dwaal-actie (idles 10/11/12 resp. 27/28/29, beweegacties 2/19, actie −1 of 8 ⇒ 0.0);
2/3 ⇒ AnimLen(2/19); 5 ⇒ 16/33; 6/7 ⇒ 4/21; 8 ⇒ 15/32; **9 ⇒ 8/25**; 10/11 ⇒ 7/24; 12 ⇒ 9/26; 13 ⇒ 6/23. Gebruikt door Dwalen (actieduur:
een beweeg-actie duurt dus maar AnimLen(2) = 0.167 s) en door **TakeDamage** (`hitT` en terugslagtijd = AnimLen(8) = **2.1 s** in W1B).

## 5. Beweging en hoogte (antwoord op "waar staat hij?")

* **Horizontaal**: de gewone gedragingen (ENEMY.md §5): Dwalen (400 u/s, draaien π/4 rad/s, leash 1000 rond het thuispunt), Achtervolgen (900 u/s,
  π rad/s), Stilstaan. Subtype ≥ 9 ⇒ de gemeenschappelijke verplaatsing `0x41b2c0` houdt y vast; `P+0x2c/0x30 = 15000` ⇒ geen rand-/opstaptest, alleen
  de sweep tegen muren (§5.1). Plus de directe schrijvingen in toestand 6 (schudden) en 7 (1 %-schuif). **Geen TRAJ/pad.**
* **Verticaal** `vtbl[43]` = `0x410900`:
```c
void Boss14_Height(Boss14 *e) {
    float h2 = P->+0x28 * 0.5f;                                     /* 75 */
    vec3 p = e->pos + (0,h2,0);
    bool hit = Probe_Test(&e->probe, World_FindCell(&p), &p, h2, e->id);   /* 0x428ce0, 0x436dc0: press-events */
    e->flags = hit ? e->flags | 1 : e->flags & ~1;   msgmask(e->id, 0x200) = hit;   /* 0x443e50 / 0x443e90 */
    if (e->flags & 4) {                                             /* 0x410b78: VALLEN (toestand 7, 8, 10, 12) */
        float k = (e->mode == 2) ? 130.0f : 200.0f;                 /* voeten liggen k onder pos */
        e->pos.y -= k;  Enemy_Ground(e);  e->pos.y += k;            /* 0x41a4e0: zwaartekracht P+0 (400), v += dt·g − 0.2·v per frame */
        return;
    }
    if (e->mode == 2 && !e->high && e->state != 9) {                /* HUPSEN (alleen modus 2, lage fase) */
        float s = dt * P->+0x0c;                                    /* 400 */
        if (e->bobDown) { e->bob += s;  if (e->bob >= e->bobMax) {
                              Dust(&(vec3){pos.x, pos.y - 50, pos.z}, &(vec3){0,1,0}, 0, 1.5f, 6.0f);   /* 0x476140 */
                              SoundFx(49, 0);  e->bob = e->bobMax;  e->bobDown = 0; } }                  /* 0x410a77 */
        else            { e->bob -= s;  if (e->bob <= 0) { e->bob = 0; e->bobDown = 1; } }
    } else e->bob = 0;
    if (e->high) e->home.y = e->yHigh;
    float d = e->home.y - e->pos.y - e->bob;
    if (fabsf(d) > 0.01f) {                                         /* 0x4a94f8 */
        float s = dt * P->+0x78;                                    /* 900 u/s */
        if (fabsf(d) > s) d = (d < 0) ? -s : s;
        e->pos.y += d;  Instance_SetCell(e, &(e->pos + (0,h2,0)));  /* 0x4077f0 */
    }
}
```
* **Zweefhoogtes W1B** (modus 1): hoog = **2430** (plaatsing), laag = **1634.8** (vloer 1344.8 + 290), landing: `pos.y` = vloer + **200** (≈ 1544.8 als de
  arenavloer daar ook 1344.8 is). De lage hoogte is een **vaste** y (berekend onder de startpositie), niet grondvolgend.
* `pos` is het logische punt van de baas; zijn **kegel** loopt van `pos.y − 250` tot `pos.y` (§6.1), zijn basis-botscilinder (`vtbl[24]` = `0x41ad80`)
  van `pos.y` tot `pos.y + 150` met straal 240. Het model wordt op `pos` getekend met de rotatie uit H (`vtbl[44]`); de wortelknoop komt uit de
  animatie. Blijft een port op **anim 0** staan (cinematic-spoor, wortel y ≈ 5826), dan zweeft het model ver boven de arena: in het origineel speelt
  de klasse vanaf de eerste Update in modus 1 alleen records 2..16 (anims 5..18). Vóór het commando (modus 0) is 405 door het script verborgen.

### 5.1 De sweep `0x437580`: een BOL van straal 240 (de lantaarns van W1B)

`0x41b2c0` roept `0x437580(&res, &from, &to, up, 30.0)` aan met `up = min(P+0x30, h/2) = 75` en `[0x4b3118] = P+4 = 240` (`0x41b489`):

```c
void Sweep(vec3 *res, vec3 *from, vec3 *to, float up, float sub /*30*/) {      /* 0x437580 */
    float h = r + up + 1.0f;                                    /* 240 + 75 + 1 = 316: bolmiddelpunt boven pos */
    vec3 c = *from + (0,h,0), d = *to - *from;
    int n = (int)(floor(|d| / sub) + 1.0f + 0.5f);  d /= n;     /* substappen <= 30; bij |d| = 0 precies één */
    for (; n > 0; n--) {
        c += d;
        SpherePush(&c, r, -1);                                  /* 0x407340: wereld (0x409ad0) + instanties vt[9] = 0x433ff0 */
        if ([0x4c4bd0]) { c.x += push.x; c.z += push.z; }       /* de volle uitduw, alleen x/z */
        GetHeight(&c);  if (c.y - h < groundY) c.y = groundY + h;
        *res = c - (0,h,0);
    }
}
```
* `0x407340` = bol tegen alle wereldpolygonen van de geraakte cellen (voorkant, `0.001 < d < r`, randtests) en de statische + dynamische
  instanties via `vt[9]` = **`0x433ff0`**, dat (net als de cilindertest `vt[8]` `0x433140` en de vloertest `vt[7]` `0x432480`) de lijst
  **`S+0x58/0x5c` = de press-nodes (vlag 0x01)** doorloopt, **niet** de hull-nodes (`S+0x38`). Per as positief maximum + negatief minimum.
* De bol loopt dus van `pos.y + 76` tot `pos.y + 556`. **Laag** (1634.8) raakt hij de koppen van de vier lantaarns in de hoeken van de arena
  (W1B inst 235..238, model 6, press-nodes tot y 1972); **hoog** (2430) gaat hij erover, maar raakt hij de rotswanden achter de lantaarns.
  Een speler die in een hoek achter een lantaarn staat is zo nooit binnen de 150 (xz) die toestand 3 nodig heeft om te schudden/stompen:
  Buzz blijft er ≈ 200 vandaan in toestand 3 hangen (port gemeten: Woody (−7983, −7567), Buzz (−7855, 2430, −7717)). Dat is het "verstoppen
  bij de lantaarns" uit het origineel.
* De sweep loopt **elke frame**, ook met stap 0 (Stilstaan, schudden, de val in toestand 7): de bol duwt hem dan ter plekke uit wat hij raakt.
* Na de sweep: subtype ≥ 9 ⇒ `res.y = from.y`; vrij als `[0x4b310c]` (grondnormaal-y van GetHeight) ≥ 0.8 en de afstap < `P+0x2c`, anders
  alleen platformdelta + `OnBlocked`. Achtervolgen-haak `[2]` `0x41bdf0`: verplaatsing < 0.01 ⇒ ±16 willekeurig in x en z (loswrikken).
* Het uitduwen per polygoon (dichtstbijzijnde punt, `r − afstand` langs die richting) is de lezing van de port van `0x409ad0`/`0x433ff0` op
  aanroepniveau, niet instructie voor instructie nagelopen.

## 6. Schade

### 6.1 Touch `vtbl[31](cat, subtype)` = `0x410cf0` – kegel met de punt omlaag

```c
Actor *Boss14_Touch(Boss14 *e, int cat, int sub) {
    for (i = 0; i < [0x4c5324]; i++) { Actor *a = [0x4c5258][i];
        if (a == e || (cat && Cat(a) != cat) || (sub && Subtype(a) != sub)) continue;   /* 0x40c340 / 0x40c350 */
        vec3 A = *a->vtbl[34](), S = *e->vtbl[34]();
        float bot = S.y - P->+0x7c;                                 /* pos.y − 250 */
        if (A.y + a->vtbl[33]() < bot) continue;                     /* actor helemaal onder de kegel */
        if (S.y < A.y) continue;                                     /* voeten boven pos */
        float o = A.y - bot + a->vtbl[33]();                        /* hoe ver de actor de kegel in steekt */
        float r = a->vtbl[32]() + (o < P->+0x7c ? o * P->+0x80 / P->+0x7c : P->+0x80);   /* straal_a + 150·min(o,250)/250 */
        if ((A.x-S.x)*(A.x-S.x) + (A.z-S.z)*(A.z-S.z) <= r*r) return a;
    }
    return NULL;
}
```
Alleen de klasse zelf roept dit aan (toestand 3 laag en toestand 7) met `(1, 0)` = de speler. Treffer ⇒ `Perso->vtbl[39](baas, 1.0, dir_xz, pos + dir·150 − (0,125,0), 0)`
(PERSO_MOVE.md §4.4: 1 hartje, terugslag, onkwetsbaar). Buiten die twee toestanden doet aanraken **geen** schade.

### 6.2 TakeDamage `vtbl[39]` = `0x40fe90`

```c
bool Boss14_TakeDamage(Boss14 *e, Actor *att, float dmg, vec3 *dir, vec3 *pt, int kind) {
    if (e->high || e->hitT > 0) return false;                       /* hoge fase of nog "geraakt": onkwetsbaar */
    e->state = 9;
    e->hitT = e->vtbl[53](0);                                       /* AnimLen(8) = 2.1 s (W1B) */
    SoundFx(e->mode == 1 ? 42 : 47, 0);                             /* 0x40fef1 */
    if (e->mode == 1) {
        switch ((int)e->hp) {                                       /* hp VÓÓR de treffer (fistp naar [0x4c5334]); tabel 0x410054 */
        case 1: smokeOn[0] = smokeOn[1] = smokeOn[2] = 0;  break;   /* laatste treffer: rook uit */
        case 2: Explode(link, 0); break;   case 3: Explode(link, 1); break;   case 4: Explode(link, 2); break;
        default: break;                                             /* hp 5 (eerste treffer): niets */
        }
    } else {
        vec3 v[2] = { e->pos + (0,400,0), e->pos + (0,500,0) };     /* 0x4a964c, 0x4a9998 */
        Effect_Explosion(1, v, 0);                                  /* 0x477060 */
    }
    return Enemy_TakeDamage(e, att, 1.0f, dir, pt, kind);           /* 0x41adc0: schade ALTIJD 1.0 */
}
void Explode(Inst *link, int n) {                                   /* 0x40ff48 / 0x40ff7e / 0x40ffb1 */
    vec3 v[2];  GetVector(link, /*typecode*/0, v, n);               /* 0x42f6b0 */
    Effect_Explosion(1, v, 0);  Smoke_Attach(link, n);              /* 0x477060, 0x475f30 (§9.2) */
}
```
* **Wie kan hem raken**: elke `vtbl[39]`-aanroep (pik, luchtaanval, `vtbl[40]` Blast), maar alleen in de **lage fase** (`+0x224 == 0`: van de
  landing tot hij weer opstijgt) en buiten de 2.1 s van "geraakt". Schade is altijd **1**: 5 treffers. De gekoppelde instantie 404 (type 17) heeft
  geen `vtbl[39]`; hem raken doet niets.
* `Enemy_TakeDamage` (ENEMY.md §6.2): `Behav_Knock` op het **actieve** gedrag (terugslag `600·t_rest` langs `dir` gedurende AnimLen(8) = 2.1 s;
  bij `dir = 0` (pik) geen verplaatsing); als dat gedrag nog een lopende terugslag heeft ⇒ `false` **zonder** hp-verlies (toestand 9 en hitT zijn dan
  wel al gezet – randgeval, letterlijk zo). Sterretje `0x40c2d0` op `pt` (behalve `kind == 2`). Retour `hp ≤ 0`.
* Of de speler hem vanaf de vloer kan pikken of moet springen hangt af van de aanvalsgeometrie van de Perso (PERSO_JUMP.md) t.o.v. `pos.y` ≈
  vloer + 290 en is statisch niet vastgesteld.
* Na de treffer: toestand 9 (rood/wit knipperen §8, anim 14), na 2.1 s `high = 1` ⇒ omhoog. Bij de 5e treffer: toestand 9 → 12 (volgende frame).

### 6.3 Dood / einde (toestand 12)

`SetVar(mailVar, 3)`, geluidslus stoppen, `modus = 0`. **Geen** `vtbl[57]`, geen `+0x10c`-verwijdering, geen fade (`+0x15c` loopt niet op),
typewoord-bit 0x400 blijft staan. Omdat Update in modus 0 direct stopt, valt hij ook niet (vlag 4 heeft geen effect meer): hij bevriest in de lucht
en de HUD-balk blijft met 0 bolletjes staan (niets zet `hud+0x48` weer op 0; alleen de HUD-init `0x4471c6`).

## 7. Berichten `vtbl[22]` = `0x410070`

| id | args | code | effect |
|---|---|---|---|
| **59** | inst, a | `0x4100d2` | `link (+0x234) = World->inst[a & 0xffffff]` (`[0x50944c]+0x6c`). Als `link && link+0x108 (u8) ≠ 0` (type 17: "gereset"): `linkAnim (+0x1c8) = new AnimCtrl14(link)` + `vtbl[4]()` reset; `link->flags8 |= 0x20` (INSTANCE.md: geen her-cellen op de geanimeerde positie); `0x40c5a0(link, this)` ⇒ `link+0x110 = baas` (eigenaar voor de render-kleur). Eén instantie, geen lijst |
| **60** | inst, var | `0x4100b0` | `mailVar (+0x230) = var & 0xffffff` |
| overige | | `0x41a740` | `Enemy::HandleMsg` (6 aan/uit, 11 parameters, 11/4 = Reset) → Instance/FadeInst |

Retour altijd 0. **Correctie op MESSAGES.md** ("59/60 in klassen 14-16 koppelen 8 instanties aan +0x1c8..+0x1e4 en zetten vlag 0x40"): dat is de
handler van klasse **15** (`0x40e781..0x40e7d8`: 8 instanties in `+0x1c8..+0x1e4`, OR in `inst+8`, dan Reset; bericht 60 ⇒ `+0x24c` op `0x40e7e3`).
Klasse 14 koppelt **één** instantie aan `+0x234`, zet vlag **0x20** en maakt er een AnimCtrl voor.

## 8. Render-kleur `vtbl[26]` = `0x40fde0` (ook voor de gekoppelde instantie)

```c
void Boss14_RenderColour(Boss14 *e) {
    if (e->state == 9 || e->state == 12) {
        if (e->hitT < 0) e->hitT = 0;
        g_colMode /*[0x5ac850]*/ = 1;  g_col.r /*[0x5ac854]*/ = 1.0f;
        int n = ++[0x4c5348];
        g_col.g = g_col.b /*[0x5ac858], [0x5ac85c]*/ = (n <= 8) ? 0.0f : 1.0f;
        if (n == 16) [0x4c5348] = 0;
        [0x5ac860] = 0;
    } else { g_colMode = 0; [0x5ac860] = 0; }
}
```
Kleurmodus 1 = vermenigvuldigen (MENU_LOAD.md, LIGHTING.md) ⇒ afwisselend **rood** (1,0,0) en normaal (1,1,1), elk 8 render-aanroepen. De teller
is globaal en wordt per getekend model opgehoogd (baas + 404 ⇒ ongeveer 4 frames rood, 4 frames normaal). Type 17 roept via `vtbl[26]` = `0x40c5c0`
de eigenaar aan, dus 404 knippert mee.

## 9. De gekoppelde instantie (W1B 404, type 17) en effecten

### 9.1 Meeslepen `vtbl[55]` = `0x41b1b0` (basis, actief omdat `vtbl[54]` de link teruggeeft)

```c
void Enemy_SyncLinked(Enemy *e) {
    Inst *l = e->vtbl[54]();  if (!l) return;
    l->colCenter = e->pos + (0, P->+0x28*0.5f, 0);   /* +0x60..0x68 */
    l->pos = e->pos;  Instance_SetCell(l, &(e->pos + (0,75,0)));   /* 0x4077f0 */
    memcpy(&l->rot /*+0x28*/, &e->rot, 9*4);                         /* rotatiematrix */
}
```
Elke frame (via `Enemy::Update`, dus alleen in modus ≠ 0) staat 404 op **exact dezelfde positie en rotatie** als 405 en speelt via `+0x1c8` hetzelfde
logische record met **zijn eigen** .ins-animaties (model 38 heeft 19 anims ⇒ sub 5..18 van modus 1 bestaan). Omdat 404 zijn eigen AnimCtrl heeft maar
`AnimLen` altijd van 405 komt, lopen de twee modellen parallel. 405 = Buzz, 404 = zijn toestel/voertuig (vermoedelijk; de kegel-botsing past bij een
toestel onder Buzz – onzeker).

### 9.2 Klasse 17 (kort; alleen wat de baas nodig heeft)

Ctor `0x40c3d0` (Instance-basis `0x42e1a0`; `+0x104 = 0`, `+0x108 = 0`, `+0x10c = 0` rook-emitter, `+0x110 = 0` eigenaar, `+0x114 = 1`), vtable `0x4a95dc`
(28 slots, Instance-niveau: **geen** `vtbl[39]`): `[1]` PostLoad `0x40c440` (`0x44e7c0`, `+0x110 = 0`, `+0x108 = 0`), `[3]` Think `0x44e810` (FadeInst),
`[17]` Reset `0x40c460` (`0x42e250`; **`+0x110 = 0`**, `+0x108 = 1`; alleen als `+0x114 == 0`: eenmalig een rook-emitter over zijn markers met typecode 9
(lijst `0x5e8564`)), `[22]` `0x40c5e0`: bericht **63** `[inst, v]` ⇒ `+0x114 = v`, Reset; rest → `0x44e8f0`. `[26]` `0x40c5c0`: eigenaar ? `eigenaar->vtbl[26]()` :
`0x430010`. W1B stuurt geen 63 ⇒ `+0x114 = 1` ⇒ geen emitter ⇒ de regel `link->smoke->on = 1` in de Update-proloog doet in W1B niets. Volgorde in W1B
klopt: 1200 (PostLoad + Reset ⇒ `+0x108 = 1`) vóór bericht 59 (0.05 s later); een latere Reset van 404 zou de eigenaar wissen.

**Rookpluim** `0x475f30(inst, n)` (bij de treffers met hp 4/3/2 vóór de treffer ⇒ marker n = 2/1/0): partikel-emitter (pool `[0x5e823c]+0xdb8`, max 2000,
levensduur 100000 s, callback `0x475d90`) aan marker typecode 0 nr. n van 404, zet `smokeOn[n] = [0x5e857c + n] = 1`. Zolang die byte 1 is: **300 deeltjes/s**
(`0x4a986c`) langs het pad van de marker, ±15 jitter in x/z, deeltje `0x475cd0`: 0.5 s, sprite `0x1000e`, stijgt 50·t, grootte ≈ `rand·10 + 20`. De laatste
treffer (hp 1) en Reset zetten de drie bytes op 0 ⇒ de emitters sterven. Explosie `0x477060(1, v, 0)` op dezelfde marker (zelfde effect als script-bericht 1509
modus 4/5).

## 10. Geluid, camera, HUD, events

| wat | id (ref) modus 1 / modus 2 | adres | wanneer |
|---|---|---|---|
| lus (2D, bron `+0x248`) | **39** (76) / **44** (81) | `0x410553` / `0x410569` | elke Update in toestanden ≠ 10/11/12; gestopt in 10 (`0x40fcf3`) en 12 (`0x40fc9d`) |
| landing stomp | **40** (77) / 45 (82) | `0x40f9e5` | toestand 7, geland |
| speler geraakt | **41** (78) / 46 (83) | `0x40f291`, `0x40fb78` | kegel-contact (toestand 3 laag, 7) |
| baas geraakt | **42** (80) / 47 (85) | `0x40fef1` | TakeDamage geaccepteerd |
| speler verslagen | **43** (79) / 48 (84) | `0x40fcc6` / `0x40fcde` | toestand 10 |
| hups-bodem | – / **49** (12, vol 100) | `0x410a77` | modus 2, lage fase |

Alle SoundFx met `inst = 0` (2D). Camera-schok `0x41fbb0(cam, 1.5 s)` bij de landing (`0x40f946`) en bij contact tijdens de val (`0x40fb88`).
HUD: `0x4484d0(1, hp, 5)` elke Update (modus ≠ 0) ⇒ de baasbalk schuift in bij de eerste aanroep (HUD_TEXT.md §4.4). Events: msgmask **0x200** (op de
grond, `0x410993`/`0x4109ab`), 0x10 gewist in `Enemy::Reset`; **script-var**: `mailVar := −1` (elke Update), `:= 3` (verslagen).

## 11. Het W1B-script rond de baas (ter controle van de port)

* **Object 405** (baas): init `var51 = −1; var49 = 0; var51 = 3`; `1200 [405, 14]`, `45 [405, 33]`; na 0.05 s `59 [405, 404]`, `60 [405, var51]`;
  na 1 s `6 [405, 0]` (verbergen). Wachters:
  * `var0 == 1 && var49 == 1` ⇒ `var0 = 0; var51 = 3; var49 = 0;` na 0.1 s `var49 = 1`. **var 0** wordt geschreven door **object 8 = de Perso**
    (`1200 [8, 1]`): init `var0 = 0`, en `MSGTEST 16` (msgmask 0x10 = "Woody verloor een leven", puls uit `0x44c730`) ⇒ `var0 = 1`. Het is dus een
    grendel "Woody is ooit gestorven"; de tak vuurt alleen op het moment dat het gevecht start (var49 is maar één tick 1) en stelt de start 0.1 s uit.
    `var51 = 3` is voor de baas geen commando (hij antwoordt −1). Niet de baas schrijft var 0 (behalve het −1-randgeval van §3.4).
  * `var49 == 1` ⇒ `var49 = 2`, `6 [405, 1]`, `6 [404, 1]`, **`var51 = 1`** (⇒ modus 1 + Reset op de volgende Update), 1152/1150, na 0.5 s railcamera 403
    (580 / 540 / 710).
  * `var51 == 3 && var49 == 2` ⇒ `var49 = 0; var46 = 4`; na 0.5 s `6 [405, 0]`, `6 [404, 0]` ⇒ object 397 speelt cinematic 73 (1131) met 398/399,
    explosies `1509 [405, 399, 4/5, …]` en tenslotte **1083** (EndLevel).
* Tijdens het gevecht wordt de baas bij een dood van Woody **niet** gereset (de Perso-reset `0x445b23` raakt alleen de Perso); var49 blijft 2.
* W2D (745 + 746, `63 [746, 1]`, var 288), W3D (775 + 776, var 318) en WWS (362 + 363, var 138) schrijven **2** ⇒ modus 2.

## 12. Recept voor de port (W1B speelbaar en correct in beeld)

Volgorde van implementeren; getallen voor modus 1 / W1B.

1. **Bericht 59 en 60** op type 14: `link = inst[a]`, `mail_var = var & 0xffffff`; link: `no_recell = 1`, eigen anim-toestand, eigenaar = baas.
   Type 17 (404) krijgt verder géén gedrag: zichtbaarheid via bericht 6 zoals elke instantie.
2. **Brievenbus** aan het begin van elke baas-update: `v = var[mail_var]`; `v == 1/2` ⇒ als modus verandert: `mode = v; boss_reset()`; altijd `var[mail_var] = -1`
   (watchers wekken). `mode == 0` ⇒ verder niets doen (ook geen animatiekeuze, geen HUD, geen meeslepen).
3. **Activering** als bij `Enemy::Think`: alleen updaten als de baas binnen **3000** van de camera is (of hp ≤ 0) en er geen cinematic loopt.
4. **Reset** (`boss_reset`): pos = thuis = plaatsing (−7031, 2430, −8863), hp = 5, `high = 1`, state 0, val uit, zichtbaar maken, rook uit,
   `walk = 400, run = 900, g = 400`; `y_high = 2430`, `y_low = vloer_onder_start + 290` (**1634.8**, eenmalig bij het laden bepalen).
5. **Animatie**: nooit anim 0 laten staan. Tabel logisch record → (.ins-anim, speed, keten): 2 → (7, 3, lus), 4 → (9, 3, dan 10 in lus), 7 → (13, 2, lus),
   8 → (14, 1, dan 5), 9 → (18, 1, vasthouden), 10/11/12 → (15/16/17, 3, dan 5), 15 → (12, 3, lus), 16 → (16,17,15,16, 1.5). Keuze per toestand §4.2
   (van het vorige frame). **Zelfde record op 404** met diens eigen animaties. `AnimLen(8) = len(14)/1 = 2.1 s`, `AnimLen(2) = 0.167 s`.
6. **Beweging**: horizontaal de bestaande wander/chase uit `src/enemy.c` (maar: y vasthouden, geen rand-/opstaptest, draaien π/4 resp. π, snelheden 400/900,
   leash 1000 in 3D); verticaal §5: naar `home.y` met max 900 u/s, of vallen met `g = 400` (per-frame formule van ENEMY.md §3.3) met de voeten 200 onder `pos`.
7. **Meeslepen**: na de baas-update 404.pos = 405.pos en 404.rot = 405.rot (ook de cel).
8. **Toestandsmachine** §3A letterlijk (13 toestanden, inclusief het 60-Hz-schudden −20/+20/+20/−20 + 10 % naar de speler, de 1 %/frame-schuif tijdens de val,
   het terugwijken 1300 van de speler zolang die stilstaat (< 1 eenheid/frame) en de 20-eenheden-poort vóór de stomp).
9. **Kegel-contact** §6.1 (hoogte 250 onder `pos`, straal `r_speler + 150·min(o,250)/250`) ⇒ `player_hit(1 hartje, dir_xz, pos + dir·150 − (0,125,0))`;
   speler dood ⇒ toestand 10 (4 s juichen, SoundFx 43, lus stoppen).
10. **Kwetsbaarheid**: `enemy_take_damage` voor type 14: alleen als `!high && hit_t <= 0`; altijd 1 hp; `state = 9; hit_t = 2.1`; SoundFx 42; bij hp-vóór 4/3/2:
    explosie + rookpluim op marker typecode 0 nr. 2/1/0 van 404, bij hp-vóór 1 alle rook uit. Rood/wit knipperen in toestand 9 en 12.
11. **Einde**: toestand 12 ⇒ `var[mail_var] = 3` (**vóór** de VM-tick van dezelfde frame, zodat object 405 het ziet), lus stoppen, `mode = 0` (bevriezen).
12. **HUD**: elke actieve frame `hud_boss_bar(1, (int)hp, 5)` (balk met Buzz-gezicht, HUD_TEXT.md §4.4).
13. **Geluid/camera**: lus 39 (2D) in alle actieve toestanden behalve 10..12; 40 landing + camera-schok 1.5 s; 41 speler geraakt (+ schok alleen tijdens de val); 42 geraakt; 43 speler verslagen.

## 13. Stand van de port (`src/boss.c`)

* Type 14 is een gewone `Enemy` in `g_enemies` (zodat pik, luchtaanval en auto-aim hem vinden) met de eigen velden in `Enemy.b`; `enemy.c` stuurt
  Update, TakeDamage en `11/4` door naar `boss.c`. Motor-haken in `main_engine.c`: `game_var_get/set` (brievenbus), `game_cam_shake` (`0x41fbd0`,
  nu in `cam_update`), `game_boss_bar` (`hud_boss_bar`), `game_explosion` (twee flitsrecords), `game_boss_smoke` (explosie + rookpluim op marker
  typecode 0 nr. n van de schotel).
* Beweging: `boss_sweep` = de bol-sweep §5.1 met `player_sphere_push` (wereld + press-nodes van instanties, zonder de baas zelf, zijn schotel
  en de speler), elke frame, ook bij stap 0. Daarvoor was het een dunne straal alleen tegen wereldpolygonen en werd er bij stap 0 niets getest:
  Buzz vloog dwars door de lantaarns en bereikte de speler in de hoeken. Niet geport: de "vrij"-test `[0x4b310c] >= 0.8` (boven de arenavloer altijd waar).
  Test hoek: `WOODY_POSAT="24 -7990 1360 -7560"` bij de test hieronder (Woody wordt niet meer geraakt; de oude build doodt hem).
* De gekoppelde instantie wordt door `player_set_carried` uitgesloten van de grondtest van de baas; `player_ground_query` slaat nu ook de speler zelf
  over (de baas landde op Woodys eigen botsnode).
* Gevonden bij het porten: de cinematic-start `0x44ecc0` roept `0x4077f0` aan op elke acteur, en die zet een verborgen instantie altijd weer in zijn
  cel. Daardoor verschijnen Buzz (398) en zijn schotel (399) in de intro van het gevecht, hoewel het script ze bij de init verbergt (CINEMATIC.md).
* Test: `extract/Data W1B --pos -5753 1800 -8901 --yaw -90 --walk 2` (loopt het volume 94 in; intro tot ≈ 22 s, dan het gevecht);
  `--jump 25.2 --peck 25.5 0.1` raakt hem bij de eerste landing. Haken: `WOODY_BOSSLOG=1` (toestand per frame), `WOODY_BOSSHP=N` (start-hp),
  `WOODY_FPS=N` (framecap, voor de per-frame-formules), `WOODY_GOD=1`. Een run tot het einde voltooit W1B in `woodyre.sav`: maak eerst een kopie.

## 14. Open vragen

* Visuele betekenis van anims 5..18 (model 37) en 5..18 van model 38; welke precies "vliegen", "lachen", "geraakt" zijn is uit het gebruik afgeleid.
* Slot 23 (`0x45b340` = 8) en `P+0x84`/`P+0xa0` (2.0): geen lezer gevonden. `+0x1d0` (stilsta-tijd) en `+0x22c` hebben geen lezer.
* De vergelijking in toestand 5 (`boog(doel, hoek > 0 ? 1 : 0)`) is letterlijk overgenomen; vermoedelijk een bug in het origineel.
* Of de camera tijdens het gevecht altijd binnen 3000 van de baas blijft (anders bevriest hij en stopt zijn geluid) en of de speler hem vanaf de vloer
  kan raken, is statisch niet vastgesteld; tracen in het origineel (`tools/wtrace.py`) op `0x40fe90` en `0x40eec0` kan dat beslissen.
* De grondhoogte 1344.8 is alleen over wereldpolygonen bepaald; `0x435650` kan ook instantie-hulls meenemen.
