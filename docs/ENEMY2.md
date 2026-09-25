# Vijandklassen deel 2 (types 10, 11, 12, 13) – Woody.exe

Status: statische analyse van `game/Woody.exe` (`out/disasm_full.txt`, `tools/drange.py`; floats en tabellen met een PE-lezer uit de exe).
Vervolg op ENEMY.md: de basisklasse `Enemy`, het parameterblok **P**, de gedragingen (Dwalen / Achtervolgen / Stilstaan / Pad), de
hoekregelaar **H**, `Enemy::Update 0x41a3e0`, `Enemy_TakeDamage 0x41adc0` en de berichten 6/11 gelden hier ongewijzigd en worden niet
herhaald. "onzeker" = niet regel voor regel gelezen of geen lezer gevonden. Types 12 en 13 zijn volledig gelezen; types 10 en 11 komen
**in geen enkel level voor** (§1) en zijn op hoofdlijnen gevolgd.

## 0. Samenvatting

| type | ctor(subtype) | vtable | size | wat het is | gebruikt |
|---|---|---|---|---|---|
| 10 | `0x415190(10)` (`0x40379f`) | `0x4a9cb8` | 0x1ec | **vliegende schutter**: zweeft 180 boven de speler, houdt 800 afstand, doelzoekende bol, duikt binnen 800 | nergens |
| 11 | `0x4120e0(7)` (`0x403811`) | `0x4a9ab0` | 0x200 | **bommenwerper te voet**: type 7-variant (zelfde Reset `0x416ed0`), gooit bommen i.p.v. projectielen, stormloop binnen 300 | nergens |
| 12 | `0x410e90(8)` (`0x40384a`) | `0x4a99b0` | 0x204 | **stilstaande bommengooier / eindbaas W2B**: alleen gedrag Stilstaan, draait mee, gooit bommen, mept binnen 300; **onkwetsbaar voor de pik**, alleen bomexplosies doen schade (5 hp); dood ⇒ msgmask 0x10 ⇒ script stuurt 1083 (EndLevel) | W2B ×1 |
| 13 | `0x413780(9)` (`0x4037d8`) | `0x4a9bb0` | 0x1f8 | **spook**: half doorzichtig (fade-doel 0.5), vliegt (geen zwaartekracht, volgt de hoogte van de speler), schiet om en om uit twee handen een vuurbal, duikt binnen 300 | K3A, S3A, W3A, W3B, W3D ×19 |

Let op de volgorde in de fabriek `0x403502` (bytetabel `0x403f3c`, jumptabel `0x403e94`): case 9 → `0x40377e` = type 10, case 10 → `0x4037f0` = type 11,
case 11 → `0x403829` = type 12, case 12 → `0x4037b7` = **type 13** (de code van type 13 staat dus vóór die van 11/12). Alle vier: categorie 2
(`0x40c360(2)`), `+0x1c4 = 0`, PostLoad telt `[0x4c5330]++`.

Bazen (niet geanalyseerd): **type 14** = `0x40eb50(11)` (size 0x24c), **type 15** = `0x40d850(12)` (0x258), **type 16** = `0x40c730(13)` (0x2a8).

Vtable-verschillen t.o.v. de Enemy-basis (`0x4a9fbc`; slots als in ENEMY.md §1.1):

| slot | type 10 | type 11 | type 12 | type 13 | betekenis |
|---|---|---|---|---|---|
| 1 PostLoad | `0x415280` | `0x4121d0` | `0x410f80` | `0x413870` | |
| 17 Reset | `0x4153c0` | **`0x416ed0`** (= type 7) | `0x411020` | `0x4139b0` | |
| 31 Touch | = | = | **`0x411840`** | = | type 12: vergroot bereik (§4.4) |
| 39 TakeDamage | `0x416070` | `0x412d90` | `0x411ab0` | `0x414490` | |
| 40 Blast | = | = | **`0x4119b0`** | = | type 12: 1 schade i.p.v. dood |
| 41 (bool) | `0x4160d0` | `0x417fd0` (= type 7) | `0x411b10` | `0x414510` | "mijn projectiel heeft de speler gedood" (aangeroepen door `0x44a1eb`, PROJECTILES §2.5) |
| 43 grond | **`0x416a10`** | = | = | **`0x414f10`** | vliegers: eigen hoogteregeling |
| 45 animatie | `0x4160f0` | `0x412df0` | `0x411b30` | `0x414540` | |
| 47 RegisterActor2 | `0x416030` | `0x412d50` | `0x411970` | `0x414470` | |
| 51 sterfduur | `0x4164e0` = AnimLen(10)+1 | `0x4149e0` = AnimLen(13)+1 | `0x411d30` = AnimLen(8)+1 | `0x4149e0` = AnimLen(13)+1 | |
| 52 Update | `0x415490` | `0x412310` | `0x4110c0` | `0x413ab0` | |
| 53 duur | `0x416500` | `0x413290` | `0x411d50` | `0x414a00` | |
| 58 Fire | `0x416800` | `0x413580` | `0x411e80` | `0x414d10` | |
| AnimCtrl-vtable / record-getter / tabel | `0x4a9da4` / `0x4169f0` / `0x4b2478` | `0x4a9b9c` / `0x413760` / `0x4b1ed0` | `0x4a9a9c` / `0x4120c0` / `0x4b1d10` | `0x4a9ca4` / `0x414ef0` / `0x4b2190` | records van 0x1c B `{int sub[4]; int prio; float speed; u8 restart}` |

## 1. Gebruik per level (bericht 1200 = SetTypeInstance)

Geteld in `out/ekoasm/*.ekoasm` op het patroon `PUSH 1200; PUSH 0x1000000|inst; PUSH type; SEND 3` (alle 1200-aanroepen met een
constante; de 26 afwijkende vormen zijn `SEND 4/5` van andere berichten). Posities uit de `.ins` (`tools/insparse.py`).

| type | K1A | K2A | K3A | S1A | S2A | S3A | W1A | W1B | W2A | W2B | W2D | W3A | W3B | W3C | W3D | WWS | totaal |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 4 | 8 | | | 7 | | | 5 | 3 | | | | | | | | | 23 |
| 5 | 2 | 10 | | | 8 | | 1 | | 7 | 13 | 8 | | | | | | 49 |
| 6 | | | 12 | | | 10 | | | | | | 14 | 7 | 18 | 21 | | 82 |
| 7 | 7 | | | 8 | | | 1 | 13 | | | | | | | | | 29 |
| 8 | | 16 | | | 13 | | | | 18 | 10 | 17 | | | | | | 74 |
| 9 | | | 5 | | | 12 | | | | | | 2 | 6 | | 8 | | 33 |
| **10** | | | | | | | | | | | | | | | | | **0** |
| **11** | | | | | | | | | | | | | | | | | **0** |
| **12** | | | | | | | | | | 1 | | | | | | | **1** |
| **13** | | | 3 | | | 4 | | | | | | 2 | 6 | | 4 | | **19** |
| 14 (baas `0x40eb50`) | | | | | | | | 1 | | | 1 | | | | 1 | 1 | 4 |
| 15 (baas `0x40d850`) | | | | | | | | | | | 1 | | | | 1 | | 2 |
| 16 (baas `0x40c730`) | | | | | | | | | | | | | | | 1 | | 1 |

De overige levels (Blackbox, Credits, House, K1R..S3R, KWS, SWS, Lang) hebben geen types 4..16 (WWS alleen type 14). Prioriteit voor de port:
type 8 (74) en 7/9 (ENEMY.md §8) > **type 13 (19)** > **type 12 (1, maar het is de afsluiting van W2B)** > 10/11 (dode code).

Testplekken (level, instantie-index, positie):

| type | level | inst | positie | opmerking |
|---|---|---|---|---|
| 4 | W1A | 282 | (652, −1985, −433) | |
| 5 | W1A | 494 | (9395, 1006, −3515) | |
| 6 | W3A | 267 | (−2533, −389, 2626) | |
| 7 | W1A | 312 | (3701, 4, 2671) | |
| 8 | W2A | 357 | (9575, 157, −16400) | |
| 9 | W3A | 549 | (−10241, −254, 11011) | |
| 12 | W2B | 533 | (10953, −3082, 11031) | model 52, 17 animaties; start gedeactiveerd (zie onder) |
| 13 | W3B | 509 | (933, −560, 974) | model 39, 20 animaties; zicht 1500, standaard herlaadtijd ⇒ schiet |
| 13 | W3A | 446 | (−2924, −308, 4312) | zicht 600, loopsnelheid 150 |
| 13 | W3A | 485 | (9911, 1563, 2860) | met `11/33 = 200` ⇒ schiet praktisch nooit (zie onder) |
| 14 | W1B | 405 | (−7031, 2430, −8863) | baas, model met 43 animaties |
| 15 | W2D | 762 | (−3928, 2206, 9596) | |
| 16 | W3D | 801 | (5591, −2415, −19918) | |

Alle type 12/13-instanties hebben **geen TRAJ** (toestand "patrouille" komt dus niet voor).

Scriptberichten aan de type 13-instanties (allemaal bericht 11, ENEMY.md §7; de port moet ze per instantie in P verwerken):

| level: inst | berichten 11 `(a, b)` |
|---|---|
| K3A 375 / S3A 381 / W3A 446 | (5,1) leash aan, (0,400) leash 400, (1,600) zicht 600, (7,150) loopsnelheid 150 |
| K3A 387 / S3A 399 / W3A 485 | **(33,200)**, (8,350) rensnelheid 350, (5,1), (0,400), (1,500), (7,200) |
| K3A 388 / S3A 400 | (33,200), (8,350), (5,1), (0,400), (1,400), (7,200) |
| S3A 435 | (5,1), (0,400), (1,400), (7,100) |
| W3B 504 | (5,1), (0,50), (1,300), (7,150) |
| W3B 509 / 510 | (5,1), (0,800), (1,1500) / (5,1), (0,800), (1,500), (7,250) |
| W3B 759, 760, 761 | bericht 6 (aan/uit) en (4,0) = Reset |
| W3D 869..872 | (0,100), (1,400); 869 ook (5,1), (7,150) |

`11/33` schrijft de ruwe int naar `P+0x4c` (`0x41ab74`, jumptabel `0x41ac40[33]`) = **herlaadtijd in seconden**: 200 s. Die spoken schieten dus
in de praktijk niet (de timer start op `P+0x4c` bij het opmerken). Vermoedelijk dacht de ontwerper in 1/100 s; de code schaalt niet.

Type 12 in W2B (object 533): `1200 12`, na 1 s `6 [inst, 0]` (deactiveren); object 529 stuurt later `6 [533, 1]` en zet `var 2 = 1`;
het script van 533 test elke tick `MSGTEST 16` op zichzelf ⇒ `1083` (EndLevel). Hetzelfde level gebruikt bericht 1090 (bom starten) elders.

## 2. Parameterblok P voor subtypes 7..10 (`0x41d510`, jumptabel `0x41dc38`; cases `0x41d95c`, `0x41da36`, `0x41d9c0`, `0x41d8f3`)

Alleen de velden die de case overschrijft; de rest = kolom "default" van ENEMY.md §2.3 (o.a. `P+0x10 = π/2`, `P+0x14 = 2π`, `P+0x44 = 600`, `P+0xc0 = 3000`).

| off | default | sub 7 (type 11) | sub 8 (type 12) | sub 9 (type 13) | sub 10 (type 10) | betekenis (bewezen in deze klassen) |
|---|---|---|---|---|---|---|
| 0x04 | 50 | 50 | **60** | **30** | **80** | straal |
| 0x08 | 200 | 200 | | **100** | **400** | loopsnelheid (dwalen); type 13: ook verticale snelheid zonder doel |
| 0x0c | 600 | 600 | | **300** | **700** | rensnelheid (Achtervolgen); type 13: verticale snelheid met doel |
| 0x1c | 800 | | | **1000** | 1000 | leash-afstand (3D voor subtype ≥ 9) |
| 0x20 | 800 | 2500 | **2400** | **1500** | 2500 | zichtafstand |
| 0x24 | 600 | 800 | **1000** | **10000** | 10000 | max. \|dy\| zien |
| 0x28 | 140 | 180 | **280** | **140** | 175 | hoogte |
| 0x2c / 0x30 | 10 / 10 | | | **10000 / 10000** | 10000 / 10000 | max. afstap / opstap (vliegers: geen randtest) |
| 0x34 | 1 | 2 | **5** | **3** | 2 | levenspunten |
| 0x38 | 3.0 | 0.1 | | **0.5** | | afkoeltijd na een beet |
| 0x3c | 1 | 2 | **3** | **2** | 1 | schade van de beet / mep |
| 0x40 | 1 | 10 | 4 | **2** | 1 | schade van het projectiel (type 11/12: ongebruikt, de bom heeft zijn eigen schade) |
| 0x48 | 500 | 300 | 300 | 1 | 1 | geen lezer gevonden in deze klassen |
| 0x4c | 3.0 | 0.7 | **3.5** | **1.0** | 1.2 | **herlaadtijd** (s) |
| 0x50 | 800 | | | | 800 | type 10: afstand die hij houdt |
| 0x54 | 400 | 300 | | **300** | | xz-afstand waarbinnen de stormloop/duik mag starten |
| 0x5c | 1000 | 1000 | | **500** | | stormloop-/duiksnelheid |
| 0x60 | 1000 | 1500 | **800** | **1000** | 1500 | projectielsnelheid (type 12: werpsnelheid van de bom) |
| 0x74 | – | | | **3** (int) | 2 (int) | visueel soort van het projectiel (3 = vuurbal, 2 = energiebol; PROJECTILES §5) |
| 0x8c | 300 | | **300** | | | type 12: mep-afstand (3D) |
| 0x90 | 0.8 | | **1.3** | | | type 12: korte lont (s) |
| 0x94 | 7 | | **7.5** | | | type 12: lange lont (s) |
| 0x98 | – | | **4** (int) | | | type 12: aantal lange lonten tussen twee korte |

## 3. Type 13 – het spook (volledig)

### 3.1 Velden (size 0x1f8) en Reset

| off | type | betekenis |
|---|---|---|
| 0x1c0 | int | toestand 0..13 |
| 0x1c4 | AnimCtrl* | `0x414ec0` (vtable `0x4a9ca4`) |
| 0x1c8 | float | afkoeltimer `cool` (≥ 0 ⇒ `-= dt`; speler wordt alleen opgemerkt als `< 0`) |
| 0x1cc | float | timer "speler verslagen" = AnimLen(11) |
| 0x1d0 | float | **herlaadtimer** (≥ 0 ⇒ `-= dt`) |
| 0x1d4 | float | resterende duiktijd |
| 0x1d8 | float | remtimer = AnimLen(9) |
| 0x1dc | float | timer na de beet = AnimLen(10) |
| 0x1e0 | float | `AnimLen(4, 1)` (padanimatie-menging `0x4148a5`) |
| 0x1e4, 0x1e8, 0x1ec | | 0 bij Reset, geen lezer gevonden |
| 0x1f0, 0x1f1 | u8 | 1 / 0 bij Reset, geen lezer gevonden |
| 0x1f4 | int | **hand** 0/1 voor het volgende schot; Reset: `rand() & 1` |

PostLoad `0x413870` = als type 4: AnimCtrl, Pad (alleen met TRAJ), `Dwalen(this, 1, &thuis)`, `Achtervolgen(this, 0)`, `Stilstaan(this)`.
Reset `0x4139b0`: `Enemy::Reset`; met TRAJ ⇒ toestand 0 (Pad, `P+0x1c = 10`), anders toestand **5** (Dwalen, `0x41c160`); timers 0;
**`deathT (+0x15c) = vtbl[51]() · 0.5`** – de sterfteller begint dus op de helft, zodat het uitfaden van `Enemy::Update` (ENEMY.md §4.2) direct bij de
dood begint en het spook al na **½·(AnimLen(13) + 1)** s verdwijnt.

### 3.2 Update `0x413ab0` (jumptabel `0x414430`)

```c
void Ghost_Update(Ghost *e) {
    Enemy_Update(e);                                  /* 0x41a3e0: gedrag-tick, fade bij dood, vtbl[43] = hoogte (§3.3), rotatie, animatie */
    FadeInst_SetTarget(e, 0.5f, 0);                   /* 0x44e7f0: +0xfc = 0.5 -> half doorzichtig (fade-snelheid 100/s = direct) */
    if (e->cool   >= 0) e->cool   -= dt;              /* 0x413ae2 */
    if (e->reload >= 0) e->reload -= dt;              /* 0x413b07 */
    if (e->hitT   >  0) e->hitT   -= dt;
    Actor *t;
    switch (e->state) {
    case 0:  /* PATROUILLE 0x413c41 (alleen met TRAJ) */
        if (e->cool < 0 && FindTarget(1,0)) { e->state = 1; e->home = e->pos; Dwalen_SetLeash(e->wander, 1, &e->home); }   /* 0x41c140 */
        break;
    case 1:  /* OPGEMERKT 0x413cb8 */
        if ((t = FindTarget(1,0))) { behav = Achtervolgen; Init(); Chase_Start(t);   /* 0x41bc80: snelheid P+0xc (300) direct, draaisnelheid P+0x14 (2π) */
                                     e->state = 2; e->reload = P->reload /*+0x4c = 1.0*/; }
        else e->state = 4;
        /* fallthrough naar 2 */
    case 2:  /* ACHTERVOLGEN + SCHIETEN 0x413d0d */
        if (!(t = FindTarget(1,0))) { e->state = 4; break; }
        vec3 d3 = normalize(t->pos - e->pos);                                    /* 3D, 0x440040 */
        if (dist_xz(t, e) <= P->dashDist /*+0x54 = 300*/ && dot(H_moveDir(), d3) > 0.95f /*0x4a9c9c*/) {
            e->atkT = AnimLen(8) + 0.5f * AnimLen(10);                           /* +0x1d4 */
            H_SetSpeed(P->dash /*+0x5c = 500*/, direct);  e->state = 7;  break;
        }
        if (e->reload > 0) break;
        int n = (e->hand == 1) ? 0 : 1;                                           /* hand 1 -> marker 0, hand 0 -> marker 1 */
        Seg m;  if (!GetVector(e, /*typecode*/1, &m, n)) break;                   /* 0x42f6b0: twee markers met typecode 1 (flags 0x120) = de handen */
        m.end = t->pos;                                                          /* vtbl[34] van de speler = voetpositie */
        Ray(e->pos + (0, h/2, 0), m.start, -1);                                  /* 0x497ed0: steekt de hand door een muur? */
        if ([0x4c4bd0] != 3 && [0x4c4bd0] != 4)
            e->vtbl[58](t, &m, (e->hand == 1) ? 130.0f : 30.0f);                  /* Fire 0x414d10, §3.5 */
        e->reload += P->reload;  e->hand ^= 1;                                   /* ook als het schot geblokkeerd was */
        break;
    case 3:  /* NA DE BEET 0x41422f */
        if (e->t_bite > 0) e->t_bite -= dt; else { e->cool = P->cool /*0.5*/; e->state = 4; }
        break;
    case 4:  /* NAAR DWALEN 0x413b6b */  behav = Dwalen; Init(); Wander_Start(); /*0x41c160*/  e->state = 5;  break;
    case 5:  /* DWALEN 0x413ba4 */
        if (e->cool < 0 && FindTarget(1,0)) e->state = 1;
        if (e->traj && dist_xz(e->pos, e->home) < 10.0f) { behav = Pad; Path_Resume(); /*0x41cfb0*/ e->state = 0; }
        break;
    case 6:  /* GERAAKT 0x4143be */
        if (e->hp <= 0) e->state = 11;                                           /* géén vtbl[57] (geen sterretjes) */
        else if (e->hitT <= 0) { e->reload = 2 * P->reload; e->state = 4; }
        break;
    case 7:  /* DUIK 0x414085 (gedrag blijft Achtervolgen, snelheid 500) */
        if (!(t = FindTarget(1,0)) || e->atkT <= 0) { e->state = 13; break; }
        e->atkT -= dt;
        if (!Touch(1,0)) break;                                                  /* 3D-afstand < 30 + straal speler */
        dir = normalize_xz(t->pos - e->pos);  pt = e->pos + dir * P->radius + (0, h/2, 0);
        if (t->vtbl[39](e, P->damage /*+0x3c = 2*/, &dir, &pt, 0)) e->state = 9;
        else { e->t_bite = AnimLen(10); behav = Stilstaan; H_SetSpeed(P->walk, direct); e->state = 3; }
        break;
    case 13: /* REMMEN start 0x41427b */
        behav = Stilstaan; H_SetSpeed(P->dash, direct); e->t_brake = AnimLen(9); e->state = 8;   /* fallthrough */
    case 8:  /* REMMEN 0x4142bc */
        if (e->t_brake < 0) e->state = 4; else e->t_brake -= dt;                 /* géén afkoeling (anders dan type 4) */
        break;
    case 9:  /* SPELER VERSLAGEN start 0x4142f8 */
        e->t_win = AnimLen(11); e->state = 10; behav = Stilstaan; H_SetSpeed(P->walk, direct);   /* fallthrough */
    case 10: if ((e->t_win -= dt) <= 0) e->state = 4;  break;                    /* 0x414339 */
    case 11: /* DOOD 0x414361 */
        e->deathT += dt; behav = Stilstaan;
        if (e->vtbl[51]() <= e->deathT) e->flags10c |= 1;                        /* verwijderen; teller [0x4c532c]++ via vtbl[29] */
        *e->vtbl[4]() &= ~0x400;                                                 /* niet meer aanvalbaar */
        break;
    case 12: break;                                                              /* 0x41441c: leeg, wordt nergens gezet */
    }
}
```
Toestand 1 en 2 lopen in hetzelfde frame door (`0x413d01 → 0x413d0d`). Er is geen "zie ik de speler"-test in toestand 2 behalve FindTarget zelf
(zicht 1500, |dy| < 10000 ⇒ hoogte speelt geen rol). Het spook **schiet terwijl het achtervolgt** (elke `P+0x4c` s, om en om links/rechts) en gaat binnen
300 (xz) en ±18° over op de duik. Duikbereik = 500 · (AnimLen(8) + ½AnimLen(10)); met het W3B-model (anim 13 = 1.1 s / 3, anim 14 = 0.8 s / 2)
is dat 0.57 s ≈ 283 eenheden.

### 3.3 Hoogteregeling `vtbl[43]` = `0x414f10` (vervangt zwaartekracht/grond volgen)

```c
void Ghost_Height(Ghost *e) {
    if (e->state == 11 || e->state == 6) return;               /* dood / geraakt: hoogte bevriest, msgmask blijft staan */
    h2 = P->height * 0.5f;
    hit = Probe_Test(&e->probe, -1, &(pos + (0,h2,0)), h2, e->id);   /* 0x436dc0: press-events + grondhoogte [0x53a568] */
    flags.onGround = hit != 0;   groundY = [0x53a568];
    Actor *t = FindTarget(1,0);
    if (t) { dy = t->pos.y - pos.y;  step = dt * P->run /*+0xc = 300*/;
             if (dy > 0 && t->vtbl[35]()) step *= 0.2f; }      /* 0x44c940: speler stijgt in een sprong (jumper 0/1/7) -> 5x trager omhoog */
    else   { dy = e->home.y /*+0x138*/ - pos.y;  step = dt * P->walk /*+8 = 100*/; }
    if (fabs(dy) > 0.01f) {                                    /* 0x4a94f8 */
        if (dy < 0) { dy = max(dy, -step);  pos.y += dy;
                      if (pos.y < groundY) { pos.y = groundY; flags.onGround = 1; } }
        else        { dy = min(dy, step);
                      Ray((x, y + h, z), (x, y + h + dy, z), -1);                /* 0x4359b0: plafond boven het hoofd? */
                      if ([0x53a554] == 0) pos.y += dy; }
    }
    msgmask(e->id, 0x200) = flags.onGround ? set : clear;      /* 0x443e50 / 0x443e90 */
}
```
Het spook zweeft dus met zijn **voeten op de voethoogte van de speler** (of, zonder doel, op de y van het thuispunt) en raakt de grond niet bewust.
Horizontaal: de gewone gedragingen; voor subtype ≥ 9 houdt de gemeenschappelijke verplaatsing `0x41b2c0` de y vast (`res.y = from.y`), `P+0x2c/0x30 = 10000`
schakelt de rand-/opstaptest uit, de sweep tegen muren blijft. De loopsnelheid uit bericht 11/7 is ook de verticale terugkeersnelheid.

### 3.4 Schade

* **Spook raakt speler**: alleen in toestand 7 (duik) via `Touch(1,0)`: `Perso->vtbl[39](this, 2.0, dir_xz, punt, 0)`; en via de vuurbal (2.0).
  Doodt het projectiel de speler, dan roept het `vtbl[41](true)` = `0x414510` aan: toestand ≠ 11 ⇒ toestand 9.
* **Speler raakt spook** `vtbl[39]` = `0x414490` (logt `'TimeHit : %f'`): als `hitT ≤ 0`: toestand 6, AnimCtrl reset, `hitT = vtbl[53](0)` = **0.25 s**
  (`0x4a9ca0`), `return Enemy_TakeDamage(att, dmg, dir, pt, 0)` (terugslag `600·t` gedurende 0.25 s als `dir ≠ 0`, `hp −= dmg`). Anders `false`.
  3 hp ⇒ drie treffers; in elke toestand kwetsbaar behalve tijdens de 0.25 s van "geraakt". Na "geraakt": herlaadtimer = 2·`P+0x4c`.
* `vtbl[47]` `0x414470`: registreert als actor in toestanden 0..10 en 13 (niet 11/12). `vtbl[40]` Blast = basis (bom binnen 400 ⇒ dood).

### 3.5 Fire `vtbl[58](doel, Seg *m, float richthoogte)` = `0x414d10`

`T` = default-ctor inline + **sjabloon 1** (`0x449070(1, &T)`), daarna: `T.pos = m.start`, `T.dir0 = normalize(m.end − m.start)` (3D, recht op de voeten van de speler),
`T.damage (+0x30) = P+0x40 = 2`, `T.target (+0x38) = doel`, `T.aim_h (+0x3c) = 130 / 30`, **`T.steer (+0x40) = 0`, `T.vsteer (+0x44) = 0`**, `T.lim_y (+0x54) = 0`,
`T.speed (+0x20) = P+0x60 = 1000`, `T.owner (+0x58) = this`, `T.visual (+0x60) = P+0x74 = 3` ⇒ `0x4490a0(&T)`. Met stuurfactoren 0 vliegt de **vuurbal kaarsrecht**
(doel en richthoogte zijn dan zonder effect), levensduur 15 s (sjabloon 1), straal 5, SoundFx 20 (3D op het spook) bij het aanmaken. Retour = slot gevonden.

### 3.6 Animaties (`vtbl[45]` = `0x414540`, tabel `0x41468c`; records `0x4b2190`; model: 20 animaties)

| toestand | logische anim | sub[] (.ins-animatie) | speed | |
|---|---|---|---|---|
| 0 | `0x4147d0` naar Pad-substaat: 0 ⇒ 0x15/0x16/0x17 gemengd (`0x436bd0`), 1/7 ⇒ 0x18/0x19, 2..6 ⇒ 0xe..0x12 | | | alleen met TRAJ |
| 1, 2 | Achtervolgen loopt (`+0x30 == 1`) ⇒ **3**; anders draaien: `H+0xc > 0` ⇒ 1, anders 2 | 3: 1,2,2,2; 1/2: 0 | 3 | draaien = idle-animatie 0 |
| 3 | **10** | 14 | 2 (prio 1500) | beet |
| 4, 5 | `0x4146d0` naar dwaal-actie: 0..4 ⇒ **14..18**; 5 ⇒ 0x18/0x19; 6 ⇒ **4**; 7 ⇒ **5**; 9 ⇒ 6; 10 ⇒ 7 | 14..18: 6,7,8,9,10 (speed 2); 4/5: 3,4,5,0; 6: 4,4,5,0; 7: 4 | 3 | zweven/idle |
| 6 | **12** | 11 | 4 | geraakt |
| 7 | **8** | 13 | 3 | duik |
| 8, 13 | **9** | 15 | 3 | remmen |
| 9, 10 | **11** | 16 | 3 | speler verslagen |
| 11 | **13** | 12 | 3 (prio 2000) | dood; `vtbl[51]` = AnimLen(13) + 1.0 |

Daarna altijd `AnimCtrl->Tick(dt)`. Records 19/20 (sub 20/21) bestaan in de tabel maar het model heeft maar 20 animaties en de klasse gebruikt ze niet.
Duur-functie `vtbl[53]` `0x414a00` (tabel `0x414c98`): toestand 6 ⇒ 0.25; 3 ⇒ AnimLen(10); 7 ⇒ AnimLen(8); 8/13 ⇒ AnimLen(9); 9/10 ⇒ AnimLen(11); 11 ⇒ AnimLen(13);
4/5 ⇒ per dwaal-actie (0..4 ⇒ AnimLen(14..18), 5 ⇒ draai-animatie, 6 ⇒ ΣAnimLen(4,0..2), 7 ⇒ ΣAnimLen(5,0..2), 9 ⇒ ΣAnimLen(6,0..2) − `inst+0xac/inst+0xa0`).
W3B-model 39, duur in s van animatie 0..19: 5.7, 0.7, 1.5, 1.1, 6.5, 1.2, 17.9, 16.4, 11.4, 11.4, 5.6, 1.8, 5.7, 1.1, 0.8, 2.9, 8.9, 36, 36, 36 (AnimLen = duur / speed).

### 3.7 Geluid, berichten, events
Geen `SoundFx`-aanroep in de klasse (alleen SoundFx 20 van het projectiel). Berichten: alleen `Enemy::HandleMsg` (6 en 11). Events: msgmask **0x200**
(§3.3); Reset wist 0x10 (basis). Geen SetVar, geen bonus; `vtbl[29]` telt `[0x4c532c]++`.

## 4. Type 12 – stilstaande bommengooier (volledig)

Geport in `src/enemy.c` (`bomber_update`, `bomber_peck`, `bomber_blast`, `enemies_bomb_contact`); de bom-eigenaar en het sturen van een
teruggegooide bom zitten in `bombs_fly` (`src/main_engine.c`). Getest met de W2B-start (`WOODY_SETVAR="1 1 1"`): gooien, oppakken,
teruggooien, 1 hp per explosie, dood ⇒ msgmask 0x10 ⇒ `1083`.

### 4.1 Velden (size 0x204) en Reset

| off | type | betekenis |
|---|---|---|
| 0x1c0 | int | toestand 0..14 |
| 0x1c4 | AnimCtrl* | `0x4120a0` (vtable `0x4a9a9c`) |
| 0x1cc | float | timer "speler verslagen" = AnimLen(6) |
| 0x1d0 | float | **herlaad-/werptimer** (> 0 ⇒ `-= dt`) |
| 0x1d4 | float | totale mepduur = AnimLen(3) + AnimLen(5) |
| 0x1d8 | float | timer toestand 8 = AnimLen(4) |
| 0x1e4 | float | timer toestand 12 = AnimLen(15) |
| 0x1ec | float | timer na een rake mep = AnimLen(5) |
| 0x1f0 | float | aanloop van de mep = AnimLen(3) |
| 0x1f4, 0x1f8 | float, int | idle: resterende tijd en gekozen idle-animatie 9..13 (`0x411c80`) |
| 0x1fc | u8 | "volgende Touch met vergroot bereik" (§4.4) |
| 0x200 | int | aantal lange lonten tot de volgende korte; Reset: `P+0x98` = 4 |

PostLoad `0x410f80`: alleen AnimCtrl en **Stilstaan** (`+0x168`) – geen Dwalen/Achtervolgen/Pad: hij verplaatst zich nooit (alleen terugslag/platform).
Reset `0x411020`: `Enemy::Reset`, gedrag = Stilstaan, alle timers 0, `+0x1f8 = 9`, toestand **0**, **msgmask 0x10 wissen** (`0x4110ab`).

### 4.2 Update `0x4110c0` (jumptabel `0x4117fc`)

```c
void Bomber_Update(Bomber *e) {
    Enemy_Update(e);
    RegisterActor(e, 10);                             /* 0x40c080: actorlijst 1 -> projectielen (teruggegooide bommen) kunnen hem raken, PROJECTILES §2.5 */
    Actor *t = FindTarget(1, 0);                      /* zicht 2400, |dy| < 1000 */
    if (e->reload > 0) e->reload -= dt;   if (e->hitT > 0) e->hitT -= dt;
    if (t && dist3(t->pos, e->pos) < P->meleeDist /*+0x8c = 300*/)       /* bytetabel 0x4117f0 */
        if (e->state != 4 && e->state != 5 && e->state != 13 && e->state != 14) e->state = 4;
    switch (e->state) {
    case 0:  if (t) e->state = 2;  break;                                /* IDLE 0x4111db */
    case 1:  break;                                                      /* leeg */
    case 2:  /* RICHTEN 0x4111ff */
        if (!t) { e->state = 0; break; }
        H_SetTurnSpeed(P+0x14 /*2π*/);  H_Tick(dt);  H_TurnTo(e->pos, t->pos, /*snap*/0);   /* 0x41b940, 0x41ba90, 0x41ba60 */
        if (e->reload > 0 || AngDiff(H.target, H.cur) > 0) break;        /* pas werpen als hij uitgedraaid is */
        Seg m;  if (!GetVector(e, /*typecode*/1, &m, 0)) break;          /* één marker = werphand */
        m.end = t->pos;
        if (!e->vtbl[58](t, &m)) break;                                  /* Fire 0x411e80, §4.3: false = geen vrije bom */
        e->reload = AnimLen(14);  e->state = 3;  SoundFx(50, 0);
        break;
    case 3:  /* WERPANIMATIE 0x411341 */
        if (e->reload > 0) break;
        e->reload += P->reload /*3.5*/ - 0.5f * floor((P->hpMax - e->hp) * 0.333333f);   /* 0x4a9880; vanaf 3 schade 0.5 s sneller */
        e->state = 2;  break;
    case 4:  /* MEP start 0x4113b2 */
        if (!t) { e->state = 0; break; }
        e->windup = AnimLen(3);  e->meleeT = AnimLen(3) + AnimLen(5);  e->state = 5;  SoundFx(51, 0);  break;
    case 5:  /* MEP 0x411412 */
        if (!t || e->meleeT < 0) { e->state = 7; break; }
        e->meleeT -= dt;  e->windup -= dt;
        if (e->windup > 0) break;
        e->bigTouch = 1;                                                 /* +0x1fc */
        if (!Touch(1, 0)) { e->state = 0; break; }                       /* §4.4: bereik = 60 + straal speler + 300 */
        dir = normalize_xz(t->pos - e->pos);  pt = e->pos + dir * 60 + (0, 140, 0);
        if (t->vtbl[39](e, P->damage /*3.0*/, &dir, &pt, 0)) e->state = 9;
        else { e->t_after = AnimLen(5); e->state = 6; }
        break;
    case 6:  if (e->t_after > 0) e->t_after -= dt; else e->state = 11;  break;      /* 0x4115b2 */
    case 7:  e->t7 = AnimLen(4); e->state = 8;  break;                               /* 0x411605 */
    case 8:  if ((e->t7 -= dt) <= 0) e->state = 0;  break;
    case 9:  e->t_win = AnimLen(6); e->state = 10;  break;                           /* speler verslagen */
    case 10: if ((e->t_win -= dt) <= 0) e->state = 0;  break;
    case 11: e->t_stun = AnimLen(15); e->state = 12;  break;                         /* 0x41169f */
    case 12: if ((e->t_stun -= dt) <= 0) e->state = 0;  break;
    case 13: /* DOOD 0x4116ec */
        e->deathT += dt;
        if (e->vtbl[51]() /*AnimLen(8) + 1.0*/ <= e->deathT) { e->flags10c |= 1;  MsgMask_Set(e->id, 0x10); }   /* 0x411729 -> script: EndLevel */
        *e->vtbl[4]() &= ~0x400;  break;
    case 14: /* GERAAKT DOOR EXPLOSIE 0x411759 */
        if (e->hp <= 0) { e->deathT = 0; e->vtbl[57](); /*sterretjes*/ e->state = 13; SoundFx(53, 0); }
        else if (e->hitT <= 0) e->state = 0;
        break;
    }
}
```
Omdat toestanden 6..12 binnen 300 meteen weer 4 worden, mept hij door zolang de speler dichtbij blijft (aanloop AnimLen(3) ≈ 0.23 s met het W2B-model).

### 4.3 Fire `vtbl[58](doel, Seg *m)` = `0x411e80` – gooit een **bom** (type 40, BONUS.md §7)

`T` = default-ctor inline + **sjabloon 0** (`0x449070(0, &T)`: zwaartekracht 15·200, stuitert onbeperkt, demping 0.95/0.99, schade 1000, onzichtbaar,
`hits_all = 0` ⇒ raakt alleen categorie 2 subtype 8/12), daarna `T.pos = m.start`, `T.dir0 = normalize(m.end − m.start)` (3D), `T.speed = P+0x60 = 800`,
`T.target = 0`, `T.owner = this`, **lont `T.life (+0x2c)`**: als `+0x200 ≠ 0` ⇒ `P+0x94 = 7.5 s` en `+0x200--`; anders `P+0x90 = 1.3 s` en `+0x200 = P+0x98 = 4`
(dus 4 lange, 1 korte, 4 lange, …). Dan `0x44d5d0(&T, 0, −1, 0)`: eerste vrije bom uit de pool `0x5e4880` (`+0x131 == 0`) → `0x44d4d0` (lont `+0x10c = T.life`,
projectiel met `T.carried = bom`); geen vrije bom ⇒ NULL ⇒ Fire geeft `false` en de toestand blijft 2.
De lange lont geeft de speler tijd om de bom op te pakken (Perso-toestand 6, OBJECTS.md §3) en terug te gooien. Eigen bommen slaan de eigenaar over in de
treffertest (`a == T.owner`); een teruggegooide bom (eigenaar = speler) ontploft bij contact met type 12 (subtype 8). Elke explosie (`0x44d650`, straal **400**) roept
`vtbl[40]` aan op **alle** npc's met categorie 1 of 2.

### 4.4 Schade en Touch

* `vtbl[39]` = `0x411ab0` (pik/stormloop van de speler): als `soort` 0 of 1 en `hitT ≤ 0`: `hitT = AnimLen(15)`, SoundFx 54, toestand 11. **Retourneert altijd `false`,
  hp blijft gelijk**: de speler kan hem niet pikken, alleen even uit zijn werpritme halen (en staat de speler binnen 300, dan wordt 11 direct 4 = tegenmep).
* `vtbl[40](&pos, r)` = `0x4119b0` (explosie): als toestand ≠ 13, `hitT ≤ 0` en `|pos − eigen pos|² < r²`: SoundFx 52, toestand 14, `hitT = AnimLen(15)`,
  `Enemy_TakeDamage(0, 1.0, &(0,0,0), pos, 0)` ⇒ **1 hp per explosie, 5 explosies**. Tijdens `hitT` (≈ 3.6 s) onkwetsbaar.
* `vtbl[31]` Touch = `0x411840`: als `+0x1fc == 0` ⇒ basis `0x40c1e0`; anders (vlag wordt gewist) dezelfde lus over `0x4c5258` maar met
  bereik `straal_this + straal_ander + P+0x8c` (60 + 69 + 300).
* `vtbl[41](true)` = `0x411b10`: toestand ≠ 13 ⇒ 9. `vtbl[47]` `0x411970` (bytetabel `0x411998`): geen actor in toestand 1 en 13.

### 4.5 Animaties (`vtbl[45]` = `0x411b30`, tabel `0x411c40`; records `0x4b1d10`; model: 17 animaties)

| toestand | logische anim | sub[0] | speed | |
|---|---|---|---|---|
| 0 | idle `0x411c80`: als `+0x1f4 ≤ 0` kies `9 + rand() % 5` (gelijk aan de vorige ⇒ `9 + (n − 8) % 5`), speel, `+0x1f4 = AnimLen(n)` | 9..13: 11,12,13,14,15 | 2 | |
| 1 | – | | | |
| 2 | **0** | 0 | 3 | richten (staat) |
| 3 | **14** | 6 | 3 | werpen |
| 4, 5 | **3** | 3 | 3 | mep-aanloop |
| 6 | **5** | 4 | 2 (prio 1500) | na de mep |
| 7, 8 | **4** | 5 | 3 | |
| 9, 10 | **6** | 7 | 3 | speler verslagen |
| 11, 12 | **15** | 8 | 3 | gepikt / na rake mep |
| 13 | **8** | 10 | 1 (prio 2000) | dood |
| 14 | **7** | 9 | 3 | door explosie geraakt |

Records 1/2 (sub 1/2, draaien) worden alleen door de duur-functie `vtbl[53]` `0x411d50` gebruikt (toestand 2), niet afgespeeld; 16 = sub 0, 17 = sub 16 ongebruikt.
W2B-model 52, duur in s van animatie 0..16: 5.8, 1.6, 1.6, 0.7, 1.1, 1.1, 0.8, 8.1, 10.8, 1.2, 2.5, 11.8, 9.7, 9.8, 3.7, 4.9, 49.1.

### 4.6 Geluid, berichten, events
SoundFx (allemaal 2D, `inst = 0`; SOUND.md §5 refs 86..90): **50** werpen (`0x41132a`), **51** mep start (`0x4113fb`), **52** geraakt door explosie (`0x411a56`),
**53** dood (`0x41178f`), **54** gepikt (`0x411af2`). Berichten: alleen 6/11. Events: **msgmask 0x10 zetten bij het verwijderen** (`0x411729`), gewist in Reset;
0x200 via het basis-grond-volgen (hij valt gewoon met zwaartekracht).

## 5. Type 11 – bommenwerper te voet (niet gebruikt; op hoofdlijnen)

Zelfde opbouw en toestandsnummers als type 7 (ENEMY.md §8): PostLoad `0x4121d0` maakt alle vier gedragingen, Reset en `vtbl[41]` zijn letterlijk die van type 7
(`0x416ed0`, `0x417fd0`). Update `0x412310`, jumptabel `0x412d14`; timers `+0x1d0` herladen, `+0x1c8` afkoeling, `+0x1e8` (alleen afgeteld), `+0x158`.

| # | code | kern |
|---|---|---|
| 0 | `0x4124d8` | patrouille; `cool < 0` en doel ⇒ thuis = pos, leash aan, → 11 |
| 2 → 3 | `0x4123e6`, `0x412434` | naar dwalen (draaisnelheid `P+0x10`), dwalen; doel ⇒ 11; TRAJ en < 10 van thuis ⇒ 0 |
| 11 | `0x412550` | richten start: Stilstaan, draaisnelheid `P+0x14`, doelhoek naar speler; `+0x1e0 = hoek / P+0x14`, `+0x1e4 = AnimLen(19) − reload`; animaties 1/2 geschaald; → 12 |
| 12 | `0x412639` | nog niet uitgedraaid ⇒ blijven draaien; anders xz-afstand `≤ P+0x54` (300) ⇒ **5**; anders als `reload ≤ 0`: `+0x1e4` aftellen, ≤ 0 ⇒ **13**; geen doel ⇒ 2 |
| 13 | `0x4127a5` | werpen: marker **typecode 0** n 0, `m.end = speler`, `vtbl[58]` (`0x413580`); gelukt ⇒ `reload += P+0x4c` (0.7), → 11 |
| 5 | `0x41283d` | stormloop start (alleen als xz-afstand ≤ 300; anders blijft hij hier staan – onzeker of dat bedoeld is): richting in `+0x1ec`, Achtervolgen, `+0x1d4 = afstand / P+0xc`, animatie 8 geschaald, → 6 |
| 6 | `0x41297f` | stormloop: geen doel / tijd om ⇒ 14; Touch ⇒ `Perso->vtbl[39](this, P+0x3c = 2, …)`: dood ⇒ 8, anders `+0x1f8 = AnimLen(10)`, → 1 |
| 1 | `0x412b08` | na de beet; daarna `cool = P+0x38` (0.1), → 2 |
| 14 → 7 | `0x412b54`, `0x412b96` | remmen AnimLen(9), snelheid `P+8`; daarna `cool`, → 2 |
| 8 → 9 | `0x412be2`, `0x412c24` | speler verslagen AnimLen(11), → 2 |
| 4 | `0x412ca9` | geraakt: `hp ≤ 0` ⇒ `vtbl[57]`, → 10; anders na `hitT` → 2 |
| 10 | `0x412c4c` | dood (als type 4) |

Fire `0x413580`: richting = `normalize_xz(m.end − m.start)` (y = 0), **sjabloon 0** ongewijzigd (snelheid 1500, levensduur/lont 2 s), `T.target = speler`
(stuurfactor 0.025), `T.owner = this` ⇒ `0x44d5d0` (bom). `vtbl[39]` `0x412d90` = als type 7 (toestand 4, `hitT = vtbl[53](0)`). `vtbl[47]`: niet in toestand 10.
Animaties (tabel `0x412f44`, records `0x4b1ed0`): 0 ⇒ pad; 2/3 ⇒ dwalen; 11 ⇒ draaien 1/2 (sub 16/17); 12, 13 ⇒ **19** (sub 19, werpen); 5/6 ⇒ 8 (sub 13);
7/14 ⇒ 9 (sub 15); 1 ⇒ 10 (sub 14); 8/9 ⇒ 11 (sub 18); 4 ⇒ 12 (sub 11); 10 ⇒ 13 (sub 12). Het model zou dus ≥ 20 animaties moeten hebben; geen level heeft er een.

## 6. Type 10 – vliegende schutter (niet gebruikt; op hoofdlijnen)

Update `0x415490`, jumptabel `0x415fe8`; Reset `0x4153c0` (TRAJ ⇒ 0, anders 7); timers `+0x1c8` cool, `+0x1d0` herladen, `+0x1d4` duik, `+0x1dc`, `+0x1cc`.

| # | code | kern |
|---|---|---|
| 0 | `0x41561a` | patrouille; doel ⇒ thuis = pos, leash aan, → 1 |
| 1 | `0x415692` | Stilstaan, draaisnelheid `P+0x14`, doelhoek naar speler; 3D-afstand `> P+0x50` (800) ⇒ **2**, anders **3**; geen doel ⇒ 6 |
| 2 | `0x415798` | **op afstand schieten**: doordraaien; uitgedraaid en `reload ≤ 0` ⇒ marker typecode 1 n 0, `vtbl[58]`, `reload += P+0x4c` (1.2); afstand `< 800` ⇒ 3; afstand `> 810` ⇒ schuift met **100 eenh/s** (`0x4a9010`) recht naar de speler (twee stralen `0x4359b0`: lijf en verticaal, bij botsing niet bewegen) |
| 3 → 4 | `0x415afb`, `0x415b3e` | Achtervolgen starten; in 4: xz-afstand `≤ P+0x5c · T` (T uit AnimLen(7)) en `dot > 0.95` ⇒ snelheid `P+0x5c`, → **9**; anders schieten zodra `reload ≤ 0`; geen doel ⇒ 6 |
| 9 | `0x415d4c` | duik: tijd om / geen doel ⇒ 6; Touch ⇒ schade `P+0x3c` (1): dood ⇒ 12, anders `+0x1dc = AnimLen(7)`, Stilstaan, → 6 |
| 12 → 13 | `0x415eb6`, `0x415ef8` | speler verslagen AnimLen(8), → 6 |
| 6 → 7 | `0x41553d`, `0x415576` | naar dwalen, dwalen (doel ⇒ 1; TRAJ ⇒ 0) |
| 8 | `0x415f7d` | geraakt: `hp ≤ 0` ⇒ `vtbl[57]`, → 14; anders na `hitT` → 6 |
| 14 | `0x415f20` | dood; `vtbl[43]` valt dan terug op de basis `0x41a4e0` ⇒ **hij valt naar beneden** |
| 5, 10, 11 | – | leeg |

Hoogte `0x416a10`: doelhoogte = `speler.y + 180` (`0x4a9dbc`), of `thuis.y` zonder doel **en in toestand 4** (letterlijk zo; onzeker of bedoeld); stap = `dt · P+0xc` (700) in
toestand 4, anders `dt · P+8 · 0.125` (50/s); dalen tot de grond, stijgen met plafondstraal (bij botsing `dy · [0x53a558]`); msgmask 0x200.
Fire `0x416800`: default-`T` + schade `P+0x40` (1), snelheid `P+0x60` (1500), doel = speler, richthoogte 100, **stuurfactor 0.02 / verticaal 0.01, 50 s**, begrenzing 0.8/0.8,
levensduur 50 s, visueel `P+0x74` = 2 (energiebol) ⇒ `0x4490a0`. `vtbl[39]` `0x416070`: toestand 8, `hitT = vtbl[53](0)`, `Enemy_TakeDamage(…, soort 1)`.
`vtbl[41]`: ≠ 14 ⇒ 12. Animaties (bytetabel `0x416230`, records `0x4b2478`): 1/2 ⇒ 1; 3/4 ⇒ 2 (sub 1,2,2,2) als Achtervolgen loopt, anders 1; 6/7 ⇒ dwalen; 8 ⇒ 9 (sub 11);
9 ⇒ 7; 12/13 ⇒ 8 (sub 14); 14 ⇒ 10 (sub 12).

## 7. Recept voor de port (aansluitend op `src/enemy.c`)

Algemeen (alle nieuwe types): `enemies_add` ook voor het type aanroepen (`src/main_engine.c` regel 441: nu alleen 4..6); `Enemy` krijgt per instantie een P-kopie
(`radius, height, walk, run, dash, see, see_dy, hp, damage, cool, reload, leash`) die **bericht 11** kan overschrijven (§1: a = 0 leash, 1 zicht, 7 loop-, 8 rensnelheid,
33 herlaadtijd in hele seconden, 4 = reset, 5 = leash aan) en bericht 6 (aan/uit). `E_RADIUS/E_HEIGHT/...` worden velden; `enemy_radius/enemy_height` lezen ze.

### 7.1 Type 13 (spook) – hergebruikt bijna alles van type 4

Constanten: straal 30, hoogte 140, dwalen 100 u/s, achtervolgen 300 u/s (draaien 2π rad/s, eerst op de plaats uitdraaien zoals type 4 toestand 2), duik 500 u/s,
zicht 1500 (|dy| onbeperkt), hp 3, beet 2 hartjes, vuurbal 2 hartjes / 1000 u/s / recht / 15 s / straal 5, herladen 1.0 s, na treffer 2.0 s, afkoeling na beet 0.5 s,
duikvoorwaarde xz ≤ 300 en dot > 0.95, duiktijd = len(anim 13)/3 + ½·len(anim 14)/2, leash 1000 (3D), hit 0.25 s, versnelling 1000, terugslag 600.

1. `enemies_add`: `hp = 3`, `st = 5` (dwalen; met TRAJ 0), `hand = rand() & 1`, `dead_t = 0`, `inst->fade_target = 0.5` (elk frame opnieuw zetten).
2. Toestanden → bestaande code: 5 = type 4's 8 (dwalen), 1/2 = 2/1 (opmerken/achtervolgen, snelheid 300), 7 = 4 (stormloop, snelheid 500), 13/8 = 6 (remmen, **geen** `cool`),
   3 = 3 (na de beet, daarna `cool = 0.5`), 9/10 = 11, 6 = 9 (geraakt; bij herstel `reload = 2·P.reload`), 11 = 12 (dood).
3. Nieuw in achtervolgen (toestand 2): eerst de duiktest; anders als `reload <= 0`: mondingspunt = `hand ? marker0 : marker1` (marker-nodes met typecode 1 van het model,
   wereldpositie van het eerste punt), richting = `normalize(speler.pos − monding)`, projectiel met visueel soort 3 (vuurbal; `src/main_engine.c` heeft alleen soort 2 –
   tot die er is de bol tekenen), schade 2, eigenaar = spook, SoundFx 20 (3D); `reload += P.reload; hand ^= 1`. Doodt het projectiel de speler ⇒ `st = 9`.
4. Verticaal: `enemy_move` mag voor dit type **geen grond-/randtest** doen (alleen muren; y blijft) en het zwaartekrachtblok onderaan `enemy_update` vervalt; in plaats daarvan §3.3:
   doel-y = speler-voeten (of `home.y`), stap `300·dt` (× 0.2 als de speler in een sprong stijgt) resp. `walk·dt`, niet onder de grond, niet door het plafond (straal vanaf y + 140);
   niet in toestand geraakt/dood.
5. Dood: `dead_t` start op `L/2` met `L = len(anim 12)/3 + 1.0`; fade = `(dead_t − L/2)/(L/2)`; weg bij `dead_t ≥ L`; geen deeltjes.
6. Animaties (`g_ea`-stijl, .ins-index / speed): rennen 2/3 (keten 1,2,2,2), zweven-lopen 4/3 (keten 3,4,5,0), idle 6..10 / 2, draaien 0/3, duik 13/3, remmen 15/3, beet 14/2,
   gewonnen 16/3, geraakt 11/4, dood 12/3.

### 7.2 Type 12 (bommengooier, W2B inst 533)

Constanten: straal 60, hoogte 280, zicht 2400 (|dy| < 1000), hp 5, mep 3 hartjes, mepzone 300 (3D, pos–pos), mepbereik 60 + 69 + 300, draaien 2π rad/s, herladen 3.5 s
(3.0 s zodra hij ≥ 3 schade heeft), werpsnelheid 800, lonten 7.5 s ×4 dan 1.3 s ×1, explosiestraal 400, 1 hp per explosie, onkwetsbaar AnimLen(15) = len(anim 8)/3 na elke treffer/pik.

1. Staat stil (geen wander/chase): alleen terugslag + zwaartekracht van de basis. Start uit (bericht 6,0 na 1 s) tot het script 6,1 stuurt ⇒ bericht 6 moet werken.
2. Toestandsmachine §4.2 letterlijk (15 toestanden, waarvan 1 leeg). `enemy_take_damage` voor dit type: geen hp-verlies, `hit_t = len(8)/3`, `st = 11`, SoundFx 54, return 0.
3. Vereist het **bomsysteem** (nog niet geport: BONUS.md §7, PROJECTILES §2.4, Perso-toestand 6): projectiel sjabloon 0 (g = 3000, stuitert, demping 0.95 grond / 0.99 lucht per 1/60 s,
   snelheid 800) met meegedragen bom-instantie (type 40, W2B heeft ze), lont uit `T.life`, oppakken/teruggooien, explosie r 400 ⇒ `blast()` op alle vijanden en de speler.
   `enemy_blast(e, pos, r)`: types 4..11, 13 ⇒ dood (`take_damage(hp)`); type 12 ⇒ §4.4 (1 hp, `st = 14`, SoundFx 52). Zonder bommen is het level niet uit te spelen:
   msgmask 0x10 op de instantie zetten wanneer hij verwijderd wordt (script ⇒ 1083).
4. Animaties (.ins-index / speed): idle 11..15 / 2 willekeurig, richten 0/3, werpen 6/3, mep 3/3, na mep 4/2, 5/3, gewonnen 7/3, gepikt 8/3, explosietreffer 9/3, dood 10/1 (+1.0 s, fade in de tweede helft).
5. SoundFx 50..54 (2D) op de momenten van §4.6.

### 7.3 Types 10 en 11
Niet porten: geen enkel level maakt ze aan (§1). Mocht het toch nodig zijn: type 11 = type 7-toestandsmachine (ENEMY.md §8) met `Fire` = bom (sjabloon 0, doel = speler) en
P-kolom sub 7; type 10 = §6 met de hoogteregeling van het spook (+180, 50/s).

## 8. Open vragen
* Type 13: `+0x1e4/+0x1e8/+0x1ec/+0x1f0/+0x1f1` hebben geen lezer; toestand 12 wordt nergens gezet. De richthoogte 130/30 van Fire is zonder effect (stuurfactor 0) – restant van een doelzoekende versie.
* Type 13 bij het sterven: `Enemy::Update` schrijft `+0x6c` rechtstreeks (0 → 1) terwijl het fade-doel 0.5 blijft; het eerste dode frame is hij dus even ondoorzichtig (onzeker of zichtbaar).
* Type 12: toestand 1 is leeg en wordt nergens gezet; `P+0x40 = 4` en `P+0x48 = 300` hebben geen lezer. De betekenis van animatie 8 ("gepikt"/lachen) is geraden.
* `[0x4c4bd0]` = 3 of 4 na `0x497ed0` (trefsoort van de straal lijf → hand) is niet benoemd; vermoedelijk wereld / instantie.
* De bom-kant (`0x44d4d0`, toestanden 1..6, `+0x132` "vastgehouden", Perso `0x44db50`) is alleen aan de aanroepkant gelezen.
* Types 10/11: duur-functies `0x416500`/`0x413290`, de pad-/dwaalanimatiekeuze en type 11's `+0x1e8`/`+0x1ec` zijn niet gevolgd.
