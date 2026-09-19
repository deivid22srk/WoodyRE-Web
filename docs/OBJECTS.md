# OBJECTS.md — interactieve niet-vijandklassen (pikschakelaars, klimwanden, lasers, lanceerders)

Bron: statische analyse van `game/Woody.exe` (imagebase 0x400000, alle adressen zijn VA's), het W1A-script
(`out/w1a_code.txt`, `tools/ekodisasm.py`) en `extract/Data/W1A/W1A.ins` / `.tex` (`tools/insparse.py`,
`tools/levelparse.py parse_tex`). Floats zijn met pefile uit de exe gelezen. Wat niet bewezen is staat als **onzeker**.
Aansluitende documenten: INSTANCE.md (basisklasse), EVENTS.md (engine → VM), PERSO_JUMP.md §2/§3 (aanval),
BONUS.md (types 30..40, 120/121), ENEMY.md (types 4..13).

## 0. Samenvatting

1. **De aanval raakt alleen Npc's.** De doelwitlus `0x457ceb` loopt over `0x4c5258[0x4c5324]`; die lijst wordt alleen gevuld
   door `0x40c0b0` en de enige aanroepers zijn de Perso (`0x44b731`) en de vijandklassen (`jmp 0x40c0b0` op `0x41198a`,
   `0x412d6a`, `0x414484`, `0x41604a`, `0x417f3a`, `0x41946f`, `0x41a4d0`). Niet-vijandklassen hebben maar 28/29 vtable-slots
   (geen `vtbl[39]` TakeDamage). Er bestaat **geen** "Perso raakt instantie"-callback, geen Press-variant en in W1A geen
   msgmask: `code` van W1A bevat 0× `MSGTEST`, 0 collisions, alleen `VOL_FLAG3/4/5`.
2. **"Pikschakelaars" zijn scriptwerk**: volume (PersoIn) + game-bericht **1050** (toets 6 *losgelaten*) + game-bericht **1042**
   (op de grond, toestand 0, binnen `dist` van de **vectormarker** van de schakelaar en kijkrichting binnen `hoek`° van die
   vector) → scriptvariabele = 1 en de engine remt de net gestarte stormloop af (`0x458e40`). Het script doet de rest (PlayAnim 3,
   geluid, fade, camera, lasers uit, blokken omhoog). Deuren gebruiken exact hetzelfde patroon met 1040 (actie 17/18) + 26.
3. **"Klimblokken" zijn engine-werk**: een press-node met **typecode 4** (`flags & 0xff00 == 0x400`) is een pikbaar oppervlak.
   Straal van de aanval raakt zo'n node → subtoestand 8 → Perso-toestand 4 (`0x4651d0`): Woody hangt met zijn snavel in het hout
   en klimt 250 eenh/s omhoog zolang de speler op de aanvalsknop blijft tikken; boven wordt de vectormarker (typecode 0) van de
   instantie gebruikt om erop te klimmen. W1A: instantie 495 (model 51) en 52 (model 11), geen script.
4. **De lasers van W1A zijn type 51** (8×, model 33). Het model bevat alleen de behuizing; de straal is een **engine-getekende
   primitief**: klasse "lazer" (`'Too many lazers in this level max is : %d'`, `0x46e4a0`), tekenfunctie `0x46e530`, additieve
   camera-gerichte quads (kern breedte 6, gloed breedte 30, textuur bank 0 beeld 1) tussen het beginpunt van de marker en
   `begin + richting · lengte` (bericht 52), ingekort door een raycast. Aan/uit = bericht 50. Raken = `Perso->vtbl[38](2)`
   (elektrocutie, direct dood). Onze port tekent niets omdat er niets in het `.ins`-model zit om te tekenen.

## 1. Pik-interactie

### 1.1 Wat de aanval-controller zelf test (`0x457a50`, PERSO_JUMP.md §2/§3)

| pad | adres | test | gevolg voor niet-vijanden |
|---|---|---|---|
| doelwitlus | `0x457ceb..0x458b96` | alleen actors in `0x4c5258` (zie §0 punt 1), `t != perso`; geveegde cirkel `0x433920` (pik-dash) of snavelsegment tegen cilinder `0x433de0` (stormloop); treffer → `t->vtbl[39](…)`, bij true `t->vtbl[38](3)` | **geen**: bonussen, lasers, lanceerders, type 70/90 en basisinstanties staan niet in de lijst |
| doelzoeker | `0x4632e0` | alle wereldinstanties met `vtbl[4]() != NULL` en typewoord-bit 0x400, 3D-afstand < 500 | alleen richten (auto-aim); het typewoord van 42/50-52/20 heeft bit 0x400 niet (`0x45224d`: `\|= 0x26`, `0x450dbc`: `\|= 5`, `0x4528d0`: `\|= 0x44`) |
| aanvalsstraal lucht | `0x4575b0` | straal `pos+(0,5,0)` → `+ n·50`, daarna `+ n_xz·100` (`0x4359b0`); trefsoort 2 en **node-typecode 4** van press-node `[0x53a58c]` van instantie `[0x53a560]` | `0x464e00(p, inst)` → **subtoestand 8** → Perso-toestand 4 (§1.3); anders muurstuit 6/7 |
| vastpikken vanaf de grond | `0x464ef0` → `0x464f42` | actie 6 *net ingedrukt*, `atk ∈ {0,10}`, `p+0x50c == 0`, `p+0x524 < 0`; straal langs `M.dir` over `p+0x114 + 100` = 169 | idem → `SetState(4)`, `p+0x50c = 1` |
| oppakken | `0x463430` | actie 6 net ingedrukt, op de grond, object uit `0x5e4880[]` (bommen type 40) met `+0x131 != 0`, `+0x133 == 0` binnen `P+4 + 200` | `SetState(6)`; niet in W1A (geen type 40) |

`0x4305c0` (vtable-slot 11, zet msgmask 0x20 op een instantie; EVENTS.md §4.1/§6.1) heeft in de hele exe **geen aanroeper**:
de 19 plaatsen met `call [reg+0x2c]` zijn DirectDraw/COM (`0x4266e3`, `0x42674a`, `0x495e04`), het menu (`0x446588`), geluid
(`0x467797`…`0x46822d`) en runtime (`0x47eeb4`, `0x48ce0f`…); geen ervan werkt op een instantie. Dode code (onzeker of er een
indirecte `mov reg,[vt+0x2c]; call reg` bestaat; niet gevonden in de wereldfuncties `0x497a30`/`0x497ed0`/`0x498440`).
Msgmask 0x20 wordt in de praktijk alleen door chests gezet (`0x451814`, door een bomexplosie, BONUS.md §7); 12 levels gebruiken
`MSGTEST` (o.a. W2B 6×, op type 121), W1A niet.

### 1.2 Pikschakelaar = volume + 1050 + 1042

**Bericht 1050 `(var, mode)`** (`0x44548b`, tabel: bytetabel `0x44569c[id−1000]` → jumptabel `0x4455e4`; 1048 = `0x4454ff` actie 0,
1049 = `0x445562` actie 1, 1050 = actie **6**): eerst `SetVar(var, 0)` (`0x44549d`), daarna op het invoerobject `[0x5e6188]`:

| mode | functie | betekenis (record per actie 12 B: `+4` f32 houdtijd, `+8` u32 teller \| bit 31) |
|---|---|---|
| 0 | `0x467400` | `teller & 0x7fffffff != 0` = **ingedrukt gehouden** |
| 1 | `0x467420` | `teller & 0x7fffffff == 1` = **net ingedrukt** |
| 2 | `0x467440` | bit 31 = **net losgelaten**: `0x467370` wist bit 31 elk frame en zet hem als teller == 0 en houdtijd > 0 (houdtijd → 0) |

Bij true `SetVar(var, 1)` (`0x4455bb`). **W1A gebruikt overal mode 2.** Let op: GAMEFLOW.md §8 noemt de functies goed, maar
`src/main_engine.c:371` heeft mode 1 en 2 **verwisseld** (1 = losgelaten, 2 = net ingedrukt).

**Bericht 1042 `(inst, dist, hoek, var)`** (`0x445269`), pseudo-C:
```c
SetVar(var, 0);                                            /* 0x445275 */
if (!perso->onGround /*0x44bcf0: +0x22c*/) return;
if (perso->state /*+0x21c*/ != 0) return;
float cosmax = cos(hoek * (1/360.0f) * 6.2831855f);        /* 0x4aa42c, 0x4aa0f0: hoek in graden */
vec3 v[2];
if (!GetVector(inst, 0, v, 0)) GetVector(inst, 5, v, 0);   /* 0x42f6b0: marker-node typecode 0, anders typecode 5 */
vec3 pp = *perso->vtbl[34]();                              /* positie */
float d = sqrt((pp.x-v[0].x)^2 + (pp.z-v[0].z)^2);         /* XZ-afstand tot het BEGINPUNT van de vector */
if (d > (float)dist) return;                               /* 0x445341: rauw, niet x0.01 */
vec3 m = normalize_xz(v[1] - v[0]);                        /* y = 0 */
vec3 f = normalize_xz(Mover_GetDir(&perso->M /*+0x388*/)); /* 0x445780 */
if (dot(f, m) <= cosmax) return;                           /* 0x440070 */
Perso_BrakeCharge(perso);                                  /* 0x458e40: alleen als atk == 9 of 10: lading 0, T = AnimLen(0x12,0)+AnimLen(0x12,1), LockMove, anim 0x12, atk = 11 */
SetVar(var, 1);
```
`GetVector 0x42f6b0(inst, typecode, out[2], n)`: zoekt in `model+0x54[model+0x50]` (marker-nodes, kind 0x20) de n-de node met
`flags >> 8 == typecode` (dus sub-index 0), werkt de matrices bij (`vtbl[2](1)`) en transformeert de **twee punten** van de node met
de wereldmatrix van die node (`[0x509adc]+0xa0[inst+0x5c + node − 1]`). Returnwaarde 1 = gevonden.

De test is dus **niet** "kijkt naar de instantie" maar "kijkt dezelfde kant op als de markervector". In de schakelaarmodellen
ligt het beginpunt op de instantie-oorsprong (model 28/31: punten (0,0,0) en (0,0,−300) onder een node-rotatie; model 41:
(0,0,0) en (0,100,0)), dus de afstand klopt met `inst.pos`, de richting niet. `src/main_engine.c:361` gebruikt de richting
speler → instantie: staat de speler in het volume (dus bijna óp de oorsprong) dan is die richting willekeurig en faalt de test
meestal. Verder ontbreken in de port de voorwaarden *op de grond* en *toestand 0* en het afremmen van de stormloop.

**Volgorde in het origineel**: knop loslaten op de grond start in `0x457330` de stormloop (atk = 9, PERSO_JUMP §2.2) in de
Perso-update; in de VM-tick van hetzelfde frame zet 1050 de variabele, de watcher stuurt 1042, en 1042 remt de stormloop af
(anim 0x12 = de "pik" die de speler ziet). Eén VM-tick vertraging tussen 1050 en 1042 is mogelijk (STOREVAR wekt de watcher);
bit 31 is dan al gewist maar 1042 kijkt niet meer naar de toets.

#### Concreet: schakelaar 279 (eerste na de start; zet de drie lasers 196/197/198 uit)

Instantie **279** (slot 0x117, model 41: volume-node ±200 lokaal + marker typecode 0, schaal (0.25, 0.37, 0.89)), positie **(1844, −820, −2283)**,
volume-id `0x300001c` = VM-volume **28** (wereld-doos x 1794..1894, y −997..−643, z −2357..−2209; het testpunt is Perso.pos + (0,71,0)), markervector a = (1844, −820, −2283) → richting **(0, 0, −1)**. De hendel is instantie
**202** (model 35) op (1844, −899, −2371); het gloei-object **273** (type 70, model 38) hangt op (1845, −572, −2357).
Script-object 279 (woorden 5937..6199):
```
init:  var52 = var53 = var44 = 0
run:   if var44 != 0: END
       if VOL_FLAG5 28:  SEND 1050 [var52, 2]                 ; speler in volume (PersoIn zet bit 0x20): aanval losgelaten?
       if var52 == 1:    var52 = 0; SEND 1042 [279, 500, 60, var53]
       if var53 == 1:    var44 = 1; var53 = 0
                         SEND 3    [202, 0, 1, 50]            ; hendel: anim 0 vooruit in 0.5 s
                         SEND 1622 [202, sample 0, 100]
                         SEND 57 [273, 50]; SEND 56 [273, 100]   ; gloed 273 uitfaden met 0.5/s
         +0.6 s          SEND 580 [285, 2]; SEND 520 [285, 0, 196]   ; cut naar camera 285, kijkt naar laser 196
         +1.6 s          SEND 50 [198, 0]; SEND 1628 [198, 0x1000019, 100]; SEND 1623 [198, 0x1000018, 100, 150]
         +2.1 s          idem voor 197
         +2.6 s          idem voor 196
         +3.6 s          SEND 580 [285, 2]; SEND 500 [285]    ; terug naar de volgcamera
```
Test: ga op de grond in het volume staan (x 1794..1894, z −2357..−2209), kijk naar −Z (naar de hendel), tik de aanval.
De lasers hangen op (647, −1439, 166), (355, −1667, 167), (648, −1893, 165) (zie §2.1).

#### Alle 1042-gebruikers in W1A (15×; alle met `dist 500, hoek 60` behalve object 409)

| script-obj = inst | model | positie | volume | markerrichting (xz) | wat het script daarna doet |
|---|---|---|---|---|---|
| **279** | 41 | (1844, −820, −2283) | 28 | (0, −1) | hendel 202, gloed 273 uit, lasers 198/197/196 uit (`50 [.,0]`) |
| **309** | 41 | (2590, 894, 3727); volume x 2528..2652, y 804..983, z 3678..3775 | 53 | (−1, 0) | hendel 203, gloed 274 uit, camera 310 → 311, **`3 [54,0,1,150]` en `3 [53,0,1,150]`**: de twee zuilen (model 12) op (2500, −1, 3149) en (3394, 0, 3149) komen in 1.5 s omhoog, geluid 1635 |
| **399** | 41 | (−7904, 2023, −7198) | 128 | (1, 0) | hendel 204, gloed 275 uit, camera 400 → 401, `3 [201,0,1,200]` (model 34) |
| 164 / 166 | 31 | (6178, 1797, −2282) / (5830, 1701, −7533) | 18 / 17 | (0, −1) / (1, 0) | **deur**: `1040 [inst,17]`, deur-anim `3 [162,0,1,100]`, `26 [0, andere deur, 1]` (teleport), `1040 [andere,18]`, deur dicht |
| 319 / 332 | 31 | (2499, 800, 2301) / (8799, 1003, 846) | 57 / 66 | (0, −1) / (0, 1) | deurpaar (deuren 81 / 111) |
| 321 / 397 | 31 | (−1636, 994, −7492) / (−8047, 1001, −7234) | 59 / 127 | (−1, 0) / (0, 1) | deurpaar (200 / 220) |
| 334 / 336 | 31 | (9084, 1302, −4123) / (11001, 1001, −5956) | 68 / 70 | (−.71, −.71) / (0, 1) | deurpaar (116 / 269) |
| 356 / 357 | 31 | (8318, 1902, −4127) / (10998, 2105, −5922) | 90 / 93 | (.71, −.71) / (0, 1) | deurpaar (117 / 268) |
| 489 | 31 | (−8694, 1006, −9778) | 213 | (0, −1) | einddeur: `1040 [489,17]`, deur 219, **`1083`** (EndLevel) |
| (409) → inst 144 | 28 | (5760, 1703, −7527) | 137 | (−1, 0) | `dist 50000`: alleen de kijkrichtingtest, kiest parameters `1110` van de zijcamera |

Model 31 heeft zijn marker op **typecode 5** (node-flags 0x520): dat is de terugval `GetVector(inst, 5, …)` in 1042.
Ook het rijdbare object type 20 (§2.6) wordt zo "geactiveerd": `VOL_FLAG5 61` + `1050 [var62, 2]` → `SEND 40 [323]` (script-object 324).

### 1.3 Pikbare klimwand: subtoestand 8 → Perso-toestand 4 (`0x4651d0`)

Voorwaarden om vast te pikken, `0x464e00(p, inst)` (PERSO_JUMP §2.4): `p+0x524 < 0`, `p+0x26c == 0`, wandnormaal `|n.y| ≤ 0.05`
(`0x4ab7d0`); dan `p+0x510..0x518 = normalize(n.x, 0, n.z)`, `p+0x51c = inst`. De straal (`0x4359b0`) test de **press-nodes**
(kind 0x01) van instanties; "pikbaar" = typecode 4 van die node (`0x4576ce` in de aanvalsstraal, `0x46553d` in toestand 4: `(node.flags & 0xff00) == 0x400`).
Binnenkomst: vanaf de grond `0x464f42` → `SetState(4)`, `+0x50c = 1`, `+0x520 = 0.8`; uit de lucht via aanval-subtoestand 8
(`0x458524`, na `AnimLen(0xf,0)`): `SetState(4)`, anim 0x15, `+0x50c = 2`, `+0x520 = 0.8`. In beide gevallen kijkt de Perso
naar de wand (`RampA.dir = M.velDir = M.dir = −n`).

Toestand 4, elk frame (`0x4651d0`, aangeroepen uit `0x44b847`; jumptabel `0x465728` op `+0x50c − 1`):
```c
bool left = Held(0), right = Held(1), tap = JustPressed(6);          /* 0x467400, 0x467400, 0x467420 */
if (p->grip /*+0x520*/ > 0) p->grip -= dt;                            /* dt = p+0x2f8 */
if (tap && p->grip < 0.5f) p->grip = 0.5f;                            /* 0x465259: tikken houdt hem vast */
if (p->grip <= 0 && p->sub /*+0x50c*/ != 3) p->sub = 4;               /* loslaten */
switch (p->sub) {
case 1: Anim(0x14); p->sub = 2; p->peckT /*+0x528*/ = 0.3f; return;    /* 0x4652a0 */
case 2:                                                                /* 0x4652d3: klimmen */
    Anim(0x15);
    p->disp.y /*+0x208*/ = p->[+0x184] /*P+0x74 = 250*/ * dt;          /* ALTIJD omhoog, geen invoer nodig */
    side = normalize_xz(cross((0,1,0), p->wallN /*+0x510*/));          /* 0x43ff80, 0x41af10 */
    if (!p->[+0x4ec] /*zijcamera-modus*/) {
        float s = p->[+0x188] /*P+0x78 = 300*/ * dt;
        if (left)       { a = Analog(0) * s; p->disp.x = -side.x*a; p->disp.z = -side.z*a; }   /* 0x467460 */
        else if (right) { a = Analog(1) * s; p->disp.x =  side.x*a; p->disp.z =  side.z*a; }
    }
    p->disp.x -= p->wallN.x * dt * 200.0f;  p->disp.z -= p->wallN.z * dt * 200.0f;   /* 0x4aa164: tegen de wand gedrukt */
    from = p->pos + (0, 40, 0);                                        /* 0x4ab294 */
    to   = from + normalize_xz(Mover_GetDir(&p->M)) * (p->[+0x114] + 100.0f);        /* 69 + 100 */
    Ray(&from, &to, -1);                                               /* 0x4359b0 */
    if (hitKind == 2) {
        if (typecode(hit press node) != 4) { p->sub = 4; return; }     /* 0x465549 */
        if ((p->peckT -= dt) <= 0) { p->peckT += 0.3f; Spark(0, from + (to-from)*frac*0.95f, &n); }   /* 0x479c80, 0x4a9c9c */
        return;
    }
    /* straal raakt niets meer: bovenkant */
    if (hitKind == 0 && GetVector(p->wallInst /*+0x51c*/, 0, v, 0) && fabs(p->inst.pos.y /*+0x10*/ - v[0].y) < 50.0f) {
        p->[+0x538] = p->[+0x53c] = AnimLen(0x17, 0);  p->[+0x540] = 0xf;  Anim(0x17);  p->sub = 3;   /* 0x465622 */
    } else p->sub = 4;                                                 /* hitKind 1 (wereld) gaat ook hierheen: 0x465510 */
    return;
case 3:                                                                /* 0x465670: eroverheen klimmen */
    done = ((p->[+0x53c] -= dt) <= 0);
    if (done) { SetState(0); p->sub = 0; }
    Perso_RootMotion(p, done);                                         /* 0x44e290: volgt de wortelbeweging van .ins-anim +0x540 = 15 */
    return;
case 4: p->[+0x524] = 2 * AnimLen(0x16, 0); Anim(0x16);                /* 0x4656c5: val eraf, daarna 2x die duur niet opnieuw vastpikken */
case 5: SetState(0); p->sub = 0; Jumper_ForceFall(&p->J, 0); return;   /* 0x4656e8, 0x463170 */
}
```
Onzeker: de precieze tak voor `hitKind == 1` (de code springt bij `!= 2` naar `0x4655ec`, de top-test, dus ook bij een wereldtreffer;
hierboven vereenvoudigd) en de inhoud van `0x44e290` (alleen het begin gelezen: `0x42f7e0` = node-matrix van anim `+0x540`).

**W1A-klimwanden** (geen script; object 52 en 495 zijn leeg):

| inst | model | positie | pikbare node (wereld-bbox) | topmarker (typecode 0) | opmerking |
|---|---|---|---|---|---|
| **495** | 51 (node 4 = `0x401`) | (514, 0, 1905) | x 247..747, y −1000..1000, z 1905..2105 | (513, 1000, 2106) → +z | eerste klimwand; gloed 486 (type 70, model 49) op (510, −791, 1891). Pik het vlak z = 1905 (speler kijkt naar +Z) |
| **52** | 11 (node 2 = `0x401`) | (4130, 501, 3153) | x 3953..4303, y 1..801, z 2976..3328 | (4130, 802, 3328) → +z | naast de zuilen 53/54 van schakelaar 309; gloed 487 op (3919, 161, 3147) |

Beide modellen: textuurgroep 78 (16×16, vlaggen `0x00b20002` = additieve gloed) op de "pik hier"-vlakken (nodes 4..7 resp. 5..6),
een hull-node en gewone press-nodes (typecode 0) om op te staan.

## 2. Klassen

Gemeenschappelijk: alle klassen hieronder behalve 41 en 90 vallen door naar de FadeInst-handler `0x44e8f0` (56/57) en roepen in hun
denk-stap (`vtbl[3]`, uit `0x42b400` voor álle instanties, ook verborgen) eerst `0x44e810` aan. `vtbl[4]` = `0x403fe0` geeft het
typewoord `&inst+0x104` (bits 0-4 categorie, 5-9 subtype).

### 2.1 Types 50 / 51 / 52 — **laser** ("lazer")

| | 50 | **51 (W1A, 8×, model 33)** | 52 |
|---|---|---|---|
| alloc / ctor (`0x403bda`/`0x403c0f`/`0x403c44`) | 0x114 / `0x450cd0` | 0x118 / `0x450cd0` | 0x118 / `0x450cd0` |
| vtable (na de ctor overschreven) | `0x4a92ec` | **`0x4a9278`** | `0x4a9204` |
| Init `vtbl[1]` | `0x4510a0` (typewoord 0x25) | **`0x451290`** (typewoord 0x45, `+0x114 = 400.0`) | `0x4514f0` (0x65, `+0x114 = NULL`) |
| denk-stap `vtbl[3]` | `0x450f20` | `0x450f20` | `0x450f20` |
| berichten `vtbl[22]` | `0x451040` | **`0x4514d0`** (+52) → `0x451040` | `0x451610` (+53) → `0x451040` |
| segment bouwen `vtbl[28]` (+0x70) | `0x4510c0`: oneindige straal `0x435810` langs de marker tot de eerste treffer, `rec+0x2c = 1` | **`0x4512c0`** | `0x451520`: marker-begin → marker-begin van doelinstantie `+0x114` (bericht 53; zonder doel: eigen 2e marker), `rec+0x2c = 2` |

Basis-Init `0x450da0`: `0x44e7c0` (fade-init), `+0x88 = 2`, dan `0x450dd0`: telt de marker-nodes met typecode 0 (`GetVector(this,0,·,n)`
tot 0) → `+0x10c` = aantal, `+0x108` = array van 0x30-byte records; per record een **lazer-effectobject** (0x40 B, `0x46e4a0(inst, index, 0x3c0)`)
op `rec+0x1c`. Record: `+0x00` begin a, `+0x0c` eind b, `+0x18` f32 (bericht 51), `+0x1c` effect, `+0x20` trefnormaal, `+0x2c` 0 = vrij eind /
1 = eindigt op geometrie / 2 = naar doel.

Denk-stap `0x450f20`: `0x44e810`; niets als pauzebyte `[0x5e48cc]` of `+0x110 == 0`; anders per record `vtbl[28](i)`, de treffertest
`0x450f80(i)` en `effect->byte[8] = 1` ("teken mij dit frame").

Type 51 segment `0x4512c0(i)`: `v = GetVector(this, 0, ·, i)`; `dir = normalize(v[1] − v[0])`; `a = v[0]`; `b = a + dir · (+0x114)`;
`Ray(&a, &b, −1)` (`0x4359b0`); bij een treffer (`[0x53a554] != 0`): `b = a + (b − a)·[0x53a558]`, `rec+0x2c = 1`, normaal uit `0x4b3108..`;
anders `rec+0x2c = 0`. Het segment volgt dus elk frame de (eventueel door de padvolger verplaatste) emitter en stopt op muren én instanties.

Treffertest `0x450f80(i)`: voor elke actor in `0x4c52d8[0x4c531c]` (lijst 1 van het vorige frame, `0x40c080`: Perso alleen als
`+0x26c == 0` en toestand ≠ 5, `0x44b699`) met **categorie 1** (`0x40c340`, = de speler; vijanden zijn 2): `actor->vtbl[24](&cyl)`
(Perso `0x44cd60`: middelpunt = pos + (0, h/2, 0), straal = `perso+0x114` (= P+4 = 69), halve hoogte h/2), dan
`t = 0x433de0(&rec.a, &rec.b, &cyl.c, cyl.r · 0.85 /*0x4aa3d8*/, cyl.h)`; `0 ≤ t ≤ 1` → **`actor->vtbl[38](2)`** = `Perso::Kill(2)` (`0x44c110`,
PERSO_MOVE.md: bliksemdood, `+0x288 = 1.5 s`, genegeerd bij onkwetsbaarheid `+0x270 > 0` of cheat `[0x5d7b8a]`). Geen schadebedrag, geen
terugslag: aanraken = dood. (Correctie op INSTANCE.md §7: de straal in de test is die van de Perso · 0.85; `rec+0x18` van bericht 51 wordt
hier niet gelezen. Geen lezer van `rec+0x18` gevonden: onzeker.)

Berichten:

| id | args | adres | werking |
|---|---|---|---|
| **50** | on | `0x45108a` | `+0x110 = (on == 1)`: straal aan/uit (ctor: uit). Uit = geen treffertest én geen tekening |
| 51 | v | `0x451059` | alle `rec+0x18 = (float)v` |
| **52** | v | `0x4514e2` | alleen 51: lengte `+0x114 = (float)v` (rauw) |
| 53 | inst | `0x451622` | alleen 52: doelinstantie |
| 56 / 57 | v | `0x44e8f0` | fade van de behuizing (de straal kijkt daar niet naar) |
| 6, 42..46, 1..5 | | basis | verbergen, padvolger, animatie |

**Tekenen** — lijst `[0x5e82b4]` (gelinkt via `fx+0x3c`), lus `0x46d078`: `if (fx->byte[8]) { Lazer_Draw(fx) /*0x46e530*/; fx->byte[8] = 0; }`.
Alles gaat via de lijn-primitief `0x471a10(this = [0x5e823c]+0xb00, flags)`: parameters `+0x278` p0, `+0x284` p1, `+0x290..0x29c` RGBA bij p0,
`+0x2a0..0x2ac` RGBA bij p1, `+0x2b0` halve breedte, `+0x2b4` textuurref. De functie transformeert beide punten naar view-space, neemt de 2D-loodlijn
van het geprojecteerde segment en bouwt een **camera-gerichte quad** (vlag 0x400 = eigen breedte, anders `[0x4b7aa0]` = 2.0; 0x800 = eigen kleuren,
anders (0.5,0.5,0.5,1); 0x200 = getextureerd met oppervlak `[0x5e866c] + (ref & 0xffff)·0x74` = **bank 0, beeld 1**), ingediend met
`0x481560(…, 4, verts, tex, 0x24)`: vlag 4 = **additief ONE/ONE** (HUD_TEXT.md §5), 0x20 = onzeker.

`Lazer_Draw`, per frame (dt = `[0x509adc]+0x38`):
```c
fx->phase /*+0xc, start = (float)inst->id*/ += dt * 127.75f;          /* 0x4abc8c */
g = 0.5f - 0.5f * costab[((int)fx->phase % 254 + 0x80) & 0x1ff];      /* tabel [0x5e823c][i] = cos(2*pi*i/512) (0x40248f); g pulseert 0.5 -> 1 -> 0.5 in ~2 s */
if (rec.kind == 0 || rec.kind == 1) {                                 /* 0x46e71e: uiteinden faden over 70 eenheden (0x4abc88) */
    d = normalize(b - a) * 70;
    kern  (kleur (1, .7, .7), breedte 6):  a..a+d alpha 0->1,  a+d..b-d alpha 1,  b-d..b alpha 1->0
    gloed (kleur (1, .4, .4), breedte 30): zelfde drie stukken met alpha 0->g, g, g->0
} else {                                                              /* 0x46e645: type 52, geen fade */
    kern a..b alpha 1 (breedte 6); gloed a..b alpha g (breedte 30)
}
if (fx->flags /*+0x34 = 0x3c0*/ & 0x80) {                             /* puls, 0x46ea1d */
    fx->acc /*+0x10*/ += rand01() * 0.8f * dt;  if (acc > 1) { acc -= 1; s /*+0x14*/ = 0; }
    if (0 <= s && s < 1) { s += 4*dt; if (s < 1) twee quads rond a+(b-a)*s, elk 0.1 van de lengte, kleur (1,.6,.6), alpha g in het midden -> 0, breedte 25; else s = -1; }
}
if (fx->flags & 0x100) {                                              /* bliksemboog, 0x46ebd4 */
    elke 2 s (+0x18, start rand*2) een salvo van 0.9 s (+0x1c); op 0 / 0.3 / 0.6 s worden N = lengte*0.02 punten (>= 1600: 32)
    met willekeurige dwarsafwijking +-25 (0x47d160, pool 0x5e82a8, max 100 lazers) opnieuw gegenereerd; polyline zonder textuur (flags 0xc00), breedte 3
}
if ((fx->flags & 0x200) && rec.kind == 1) {                           /* inslag op b, 0x46efb9 */
    sprite bank 0 beeld 5 (ref 0x10005) op b, kleur (1, .4, .4), grootte 60 + r, alpha 0.2 + r*0.01, r = rand*50 elke 0.1 s opnieuw; 4x 0x470f10; plus vonken 50/s
}
```
Onzeker: exacte kleuren/alpha van de boog en de oriëntatie van de vier inslag-quads (`0x470f10` niet gelezen); vlagbit 0x40 van `fx+0x34`.

**Model 33** (de W1A-laser): node 1 mesh 100 polygonen, textuurgroep 97 (128×128, `0x00ff0000` = ondoorzichtig, geen animatie/scroll);
nodes 2 en 4 hull, node 3 press (typecode 0), node 5 bbox, node 6 **marker typecode 0** (de straalvector, 303 lang). Geen mesh-typecode 2/5..8,
geen geblende groep: **er zit geen straal in het model**, het script verbergt of fadet ook niets. Daarom toont de port alleen de behuizing.

**W1A-gebruik**:
- 196/197/198 (script-objecten 196..198): `1200 51`, `52 [.,350]`, `50 [.,1]`, +0.01 s `1620 [inst, 0x1000019, var27]` (3D-lusgeluid).
  Stralen (a → richting, 350 lang, ingekort door de raycast): 196 (609, −1462, 166) → −x; 197 (393, −1690, 166) → +x; 198 (610, −1917, 166) → −x.
  Uit via schakelaar 279 (§1.2).
- 249..252: `52 [.,2250]` + `43 [.,1,550,1]` (padvolger heen-en-weer, 5.5 s, 8 punten), straal recht omlaag vanaf ≈ (10820, 1159, −7253);
  253: `52 [.,2250]`, schaal 1.5, omlaag vanaf (10998, 1168, −7259). Deze vijf krijgen hun `50` later van andere script-objecten (21× bericht 50 in W1A).

### 2.2 Type 42 — **lanceerder** (categorie 6) en type 41 — **missile**

Type 42: ctor `0x452140` (0x19c B), vtable `0x4ab148`, Init `0x452230` (typewoord 0x26, `+0x88 = 2`), denk-stap `0x452780`, reset `vtbl[17]` `0x452260`,
instantieberichten = `0x44e8f0` (geen eigen). Besturing via **game-berichten 1000..1004** (`0x444870`; elk controleert `vtbl[4]` en categorie == 6;
MESSAGES.md noemt ze ten onrechte "camera"):

| id | args | adres | werking |
|---|---|---|---|
| 1000 | inst, target | `0x444d99` | `0x4522b0(1, 1.0, target)`: één schot |
| 1001 | inst, kind | `0x444c50` | `0x452330(kind)`: reset + `0x449070(kind, &this+0x108)` kopieert projectielsjabloon `0x5d7ba8 + kind·0x68` (0x68 B); ctor doet kind 1 |
| 1002 | inst, n, v | `0x444c99` | `0x452360(n, v)`, jumptabel `0x4524fc` (20 parameters, zie onder) |
| 1003 | inst, target\|−1, count, t | `0x444d27` | `0x4522b0(count, t·0.01, target)`: `+0x170 = count`, `+0x174 = max(t, 0.2)`, `+0x178 = target`, `+0x190 = now + dt`, `+0x194 = now − t`, `+0x198 = 1` |
| 1004 | inst | `0x444ce4` | `0x452320`: `+0x198 = 0` (stop) |

Denk-stap: als `+0x198`: bij elke overgang van `floor((now − t0)/T)` (`0x499ede`) en minstens `T − 0.2` s na het vorige schot: `count--`, `Fire()` (`0x452560`),
`count == 0` → stop (count −1 = eindeloos). `Fire`: `v = GetVector(this, 0, ·, 0)`; richting = `normalize(v[1] − v[0])`, of bij richtvlag `+0x199` (param 19)
en een doel: `normalize(target.pos + (0, +0x144, 0) − v[0])`; `+0x108 = v[0]`, `+0x114 = richting`, `+0x140 = target`; projectielsoort `+0x18c != 0` →
`0x4490a0(&this+0x108)` = vrij slot in de 50 projectielen `0x5d7d48 + i·0x104` (`0x449130` init, update `0x4490f0`/`0x4493c0`, EVENTS.md §3.2);
soort 0 → bom `0x44d5d0` + geluid 0xe. Daarna, als `+0x17c` (param 7) een geldige animatie is: eenmalig afspelen in `+0x180` s (param 8).
1002-parameters (n → veld, schaal; defaults uit de ctor): 0 `+0x128` rauw (1000), 1 `+0x124` rauw, 2 `+0x134` en `+0x184` ×0.01 (5.0), 3 `+0x13c` int,
4 `+0x138` rauw (20), 5 `+0x144` rauw (150, richthoogte), 6 `+0x188` ×0.01, 7 `+0x17c` int (schiet-anim), 8 `+0x180` ×0.01, 9 `+0x120` rauw (5),
10/11 `+0x12c`/`+0x130` ×0.01 (1.0), 12 `+0x148` ×0.001, 13 `+0x14c` ×0.1, 14/15 `+0x150`/`+0x154` ×0.01, 16/17 `+0x15c`/`+0x158` ×0.01 (0.7),
18 `+0x168` ∈ {2,3,4,0} (tabel `0x45254c`), 19 `+0x199` bool. Betekenis van de velden in het projectiel: onzeker (niet gevolgd).

Type 41: basis-ctor + vtable `0x4a9360` (`0x403b5d`), geregistreerd in de pool `0x5e8344[0x5e840c]` (max 0x32, `'vous avez depasse le nombre maximum de
missile (%d)'`). Init `0x4723f0` maakt een rook-effect (0x34 B, lijst `0x5e8564`, tekenfunctie `0x475440`) op de marker-nodes **typecode 9**;
denk-stap `0x4723d0` → `0x4724e0`: `pos = projectiel+0xb0`, oriëntatie uit `projectiel+0xbc` (`0x46d320`), `0x4077f0` (her-cellen), rook aan.
`0x472530` (uit `0x46d120`, levelstart) **verbergt alle missiles** (`0x407850`); `0x4722f0` (uit het projectiel-effect `0x4700e0`) pakt de volgende uit de pool.
Model 52: vijf kleine mesh-nodes (groep 113, 32×32, ondoorzichtig), bbox, marker typecode 9. Model 6 (lanceerder): behuizing groep 71, zes dubbelzijdige
(polyflag 0x2) gloedvlakjes groep 72 (`0x00b20002`), press-node, vier hulls, marker typecode 0 = loop.

W1A: 26/27/28 op (1067, 1114, 2648) → −x, (−283, 1120, 2915) → +x, (1065, 1115, 3254) → −x en 194/195 op (5647, 3191, −7505) / (5308, 3193, −7518):
elk `1002 [inst, 2, 140]` (27: 150) en `1003 [inst, −1, −1, 300]` = eindeloos elke 3 s langs de loop. De 8 missiles staan geparkeerd rond
(−2159, 1500, −780) en op (−2118, −2139, −779); in het origineel zijn ze daar onzichtbaar.

### 2.3 Type 70 — **fade-instantie** (9× in W1A: modellen 38, 49, 48)

Basis-ctor + vtable `0x4a9124` (`0x403cea`), Init `0x44e7c0`, denk-stap `0x44e810`, handler `0x44e8f0`; INSTANCE.md §5 beschrijft hem volledig
(56 = fade-doel, 57 = snelheid, > 0.9 = niet-botsbaar). Geen interactie met de speler. Modellen: 38 = 68 polygonen groep 102 (16×16, `0x00cc0002`),
49 = groep 111 (128×128, `0x00bf0002`), 48 = groep 110 (64×64, `0x007f0002`): allemaal bit 1 = additief geblend met intensiteit byte 2 —
lichtkegels/gloed boven schakelaars (270..275, deels op een padvolger) en bij de klimwanden (486, 487). Al geport; niets te doen.

### 2.4 Type 90 — **omgevingsdeeltjes-volume** (3× in W1A: model 29 = alleen een volume-node)

Basis-ctor + vtable `0x4a90ac` (`0x403d56`), basishandler, denk-stap `0x472560` (`'une instance d'environnement n'a pas de volume'`). Game-berichten
1501 `(inst, mode)` → `+0xfc` en 1502 `(inst, r, g, b, grootte·0.01, aantal)` → kleur `+0x110..0x118`, `+0x11c`, `+0x100`, herinit `0x472b30` (INSTANCE.md §7).
W1A: `1501 [.,1]`, `1502 [.,255,255,255,700,400]` op (8718, 1445, −1412), (4384, 1437, −7792), (−83, 1266, −7792), geschaald 1.7..7.2. Puur visueel
(deeltjes binnen het volume, `0x47e230`/`0x47e160`/`0x47e050`); soort deeltje per mode: onzeker.

### 2.5 Type 7 — schietende vijand (1× in W1A, model 45 op (3701, 4, 2671))

Npc/Enemy-klasse, ctor `0x416ca0`, vtable `0x4a9dc0`, handler `0x414530`; zie ENEMY.md §8 (subtype 4: straal 50, zicht 2500, hp 2, schade 2,
`vtbl[58]` `0x418820` = projectiel afvuren vanaf de marker). Pikbaar via de gewone doelwitlus (registreert zich met `jmp 0x40c0b0`). Model 45 heeft
markers typecode 1 en 0 (monding/aanvalsvector).

### 2.6 Type 20 (en 21) — **berijdbaar/afvuurbaar object** (2× in W1A: model 47)

Ctor `0x452850(type)` (0x194 B), vtable `0x4ab1b8`, Init `0x452890` (type 20: typewoord 0x44 + rook-effect op markers typecode 9; type 21: 0x24; bewaart
startpositie/-rotatie in `+0x134..0x160`), denk-stap `0x452e10` (toestanden `+0x128` 0..9, jumptabel `0x453708`), reset `vtbl[17]` `0x452ae0`, handler
`0x453730`: **40** → `0x452a50` (als toestand 0: zoek de Npc met categorie 1 en roep `Perso::0x465740(this)`: alleen op de grond en in toestand 0 →
`SetState(8)`, `perso+0x6b0 = object`, aanval gewist; object wordt niet-botsbaar, toestand 1), **29** → reset, **55** `(1, v)` → `+0x114 = v·0.01` s,
`(2, v)` → `+0x11c = v`. Perso-toestand 8 = `0x4657f0` (anims 0x36..0x3e, sturen met de acties). De denk-stap spawnt zelf een projectiel/bom
(`0x449070`, `0x44d5d0`), vliegt `+0x114` s, explodeert (`0x477060`) en raakt actors met `vtbl[0xa0]`. W1A: 323 (8845, 1137, 185) en 326 (8138, 2014, −3080),
`55 [.,1,380]`, `55 [.,2,1500]`, geactiveerd met aanval-loslaten in volume 61 (script-object 324) resp. volume 62 (script-object 327). Wat het precies voorstelt (raket/katapult): **onzeker**, niet
gedecompileerd. Model 47: behuizing groep 108, twee press-nodes, markers typecode 0 en 9.

## 3. Overige niet-geporte klassen (kort)

| type | wat | sleuteladressen |
|---|---|---|
| 40 | bom: pool `0x5e4880[16]`, oppakken (`0x463430`, Perso-toestand 6), game-bericht 1090 start er een, explosie straal 400 raakt type 17 en chests | ctor `0x44d250`, vtable `0x4aac74`, handler `0x451820`, `0x44d5d0`, `0x44d6e0`, `0x44d650` (BONUS.md §7) |
| 120 / 121 | "exploding object (chest)": bomexplosie binnen r → fade uit + **msgmask 0x20** (het enige `MSGTEST 32` dat scripts gebruiken); 29 = reset | ctor `0x451650`, vtable `0x4aafd0`, `vtbl[28]` `0x451770`, `vtbl[29]` `0x4517d0`, reset `0x451730` |
| 17 | breekbaar object voor bommen (`vtbl[0xa0](&pos, r)`), rook op markers typecode 9 | ctor `0x40c3d0`, vtable `0x4a95dc`, Init `0x40c440`, handler `0x40c5e0` |
| 50 / 52 | laser-varianten (oneindig tot muur / naar doelinstantie), §2.1 | `0x4510c0`, `0x451520`, bericht 53 `0x451622` |
| 42 + projectielen | §2.2; 50 projectielen `0x5d7d48` (0x104 B), sjablonen `0x5d7ba8` (0x68 B), Press/UnPress door landende projectielen | `0x4490a0`, `0x449130`, `0x4490f0`, `0x4493c0`; visuals `0x4700e0` (missile), `0x46f8a0`, `0x470af0` |
| 60 | eigen handler (1503/1506), soort 2 in `inst+8` | vtable `0x4a9194`, handler `0x474a40` (`0x403ca3`) |
| berichten 15..19 | textuurframe-/UV-override per instantie (INSTANCE.md §2); W1A: 16 12×, 18 10×; **niet in `src/instance.c`** | `0x42d9c3`, `0x42da32`, `0x42daae`, `0x42db0f`, `0x42db86`, lezer `0x47f290` |
| 1201 / 1202 | typewoord-bit 0x400 (aanvalbaar doel) zetten/wissen | `0x403440` |

## 4. Recept voor de port

**A. Pikschakelaars en deuren (`src/main_engine.c`, `src/player.c`)** — hiermee werken alle 15 triggers van W1A.
1. `case 1048..1050`: mode 0 = ingedrukt, **mode 1 = net ingedrukt, mode 2 = net losgelaten** (nu verwisseld). Zet de variabele eerst op 0, dan op 1 bij true.
2. `case 1042`: `var = 0`; eis `player on ground` en Perso-toestand 0 (niet dood, geen scripted hold, niet in toestand 4/8); haal de markervector
   op: nieuwe helper `ins_vector(inst, typecode, n, Vec3 out[2])` in `src/level.c` = n-de node met `kind == 0x20 && type_code == tc && sub_index == 0`,
   `ins_pose()` actueel, `out[k] = mat4_apply(&inst->node_world[node], model->points[node.point_base + k].pos)`; typecode 0, anders 5.
   Test `dist_xz(player.pos, out[0]) <= dist` en `dot(normalize_xz(out[1]-out[0]), (sin yaw, cos yaw)) > cos(hoek°)`. Bij succes: als `atk == 9 || atk == 10`
   → rem (zoals `0x458e40`: `charge = 0`, `atk_t = AnimLen(0x12,0) + AnimLen(0x12,1)`, move-lock, anim 0x12, `atk = 11`), `var = 1`.
3. Controle: start W1A, loop naar (1844, −820, −2283), kijk naar −Z, tik de aanval → hendel 202 beweegt, camera-cut, lasers 198/197/196 gaan na 1.6/2.1/2.6 s uit.
   Schakelaar 309 op (2590, 894, 3727), kijk naar −X → zuilen 54 en 53 komen omhoog.

**B. Klimwand (`src/player.c`)**
1. Laat de aanvalsstraal (`0x4575b0`-port) en een nieuwe grondtest (`0x464f42`: aanval net ingedrukt, `atk ∈ {0,10}`, straal 169 langs de kijkrichting
   vanaf de speler) instantie-press-nodes raken en het typecode van de geraakte node teruggeven. Typecode 4 + `|n.y| ≤ 0.05` + `regrip_timer < 0` →
   bewaar `wall_n = normalize_xz(n)`, `wall_inst`, kijk naar `−n`; uit de lucht eerst subtoestand 8 (`AnimLen(0xf)`), dan toestand "cling".
2. Implementeer toestand 4 volgens het pseudo-C in §1.3: `grip = 0.8`, tikken zet hem op minstens 0.5, `disp.y = 250·dt`, zijwaarts 300·dt met links/rechts,
   `−wall_n·200·dt`, geen zwaartekracht, vooruit-straal vanaf +40 hoogte over 169; niet-pikbaar → vallen (`regrip_timer = 2·AnimLen(0x16)`);
   geen treffer en `|pos.y − ins_vector(wall_inst,0)[0].y| < 50` → klim-over (anim 0x17; zet de speler aan het eind op de markervector-start; de
   wortelbeweging van `0x44e290` is onzeker), anders vallen.
3. Test: wand 495, vlak z = 1905 tussen x 247..747 (kijk naar +Z), top y = 1000; wand 52 naast de zuilen van schakelaar 309.

**C. Lasers (`src/main_engine.c`, `src/hud.c`/`src/render_gl.c`)**
1. `1200` met type 50/51/52: markeer de instantie als laser, `on = 0`, `length = 400`, `has_fader = 1`. Instantieberichten: 50 → `on = (v == 1)`,
   52 → `length = v`, 53 → doelinstantie, 51 → opslaan.
2. Per frame per laser met `on` (ook als de instantie verborgen is; de denk-stap loopt altijd): per marker typecode 0: `a, dir` via `ins_vector`,
   `b = a + dir·length`, raycast tegen wereld + instanties → inkorten, `kind = hit`. Treffertest: lijnstuk tegen de spelercilinder (middelpunt pos + h/2,
   straal 69·0.85, halve hoogte h/2; hergebruik de `0x433de0`-port van de stormloop) → `player_kill(p, 2)` tenzij dood of onkwetsbaar.
3. Tekenen na de 3D-scène, vóór de 2D-laag, naast de pickup-sprites: nieuwe `hud_world_beam(a, b, half_width, rgba0, rgba1, image)` = camera-gerichte quad
   (loodlijn van het geprojecteerde segment, of `cross(b − a, cam_pos − mid)` genormaliseerd × half_width), additief `GL_ONE, GL_ONE`, diepte-test aan,
   diepte-schrijven uit, textuur bank 0 beeld 1 over de volle quad. Minimaal: kern (1,.7,.7,1) breedte 6 + gloed (1,.4,.4,g) breedte 30 met
   `g = 0.5 − 0.5·cos(2π·(((int)(t·127.75) % 254) + 128)/512)`, uiteinden in 70 eenheden naar alpha 0 (bij additief: kleur × alpha). Daarna naar smaak puls,
   boog en inslag-sprite (beeld 5) uit §2.1.
4. Test: vanaf de start (537, −1800, −2450) richting +z; drie horizontale rode stralen van 350 lang op z ≈ 166, y = −1462 / −1690 / −1917.

**D. Klein**: verberg type-41-instanties bij levelstart (`0x472530`); voeg 15..19 toe aan `src/instance.c`; corrigeer in MESSAGES.md 1000..1004
(lanceerder, niet camera) en in INSTANCE.md §7 de straal van de lasertest.

## 5. Open vragen

1. `0x44e290` (klim-over, wortelbeweging van anim `+0x540 = 15`) en de `hitKind == 1`-tak in toestand 4 zijn niet volledig gelezen.
2. Laser: kleuren/alpha van de bliksemboog, de vier `0x470f10`-quads van de inslag, vlagbit 0x40 van `fx+0x34`, lezer van `rec+0x18` (bericht 51), vlag 0x20 van `0x481560`.
3. Lanceerder: betekenis van de 1002-parameters in het projectiel (`0x4493c0`), de drie projectiel-visuals en de schade aan de speler.
4. Type 20/21: toestandsmachine `0x452e10` en Perso-toestand 8 (`0x4657f0`) zijn alleen op hoofdlijnen gevolgd.
5. Type 90: welk deeltje bij welke mode (`+0xfc` 0/1/2).
6. Of er één VM-tick vertraging zit tussen 1050 en 1042 (watcher-wekken binnen dezelfde tick) — voor de port niet relevant zolang 1042 niet naar de toets kijkt.
7. `0x4305c0` lijkt dode code; met een breakpoint in het origineel te bevestigen.
