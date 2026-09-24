# SPLASH.md — de waterplons `0x478660`

Statische analyse van `game/Woody.exe` (image base 0x400000; alle adressen zijn VA's). Floats zijn rechtstreeks uit de
PE-secties gelezen. Aanleiding: de plons van `Kill(7)` (verdrinken in een watervolume, klasse 60, WATER.md §4) en van
scriptbericht 1505 was nog niet geport (WATER.md §6, PERSO_DEATH.md §4.3, TODO.md).

Notatie: `fx` = de effectpool `[0x5e823c]+0xdb8` (2000 records van 0x50 B, teller `[0x5e823c]+0x27eb8` = `fx+0x27100`,
driver `0x470c70`, BONUS.md §2.4), `S` = de sprite-/lijnparameters `[0x5e823c]+0xb00`, `dt` = `[[0x509adc]+0x38]`,
`rnd()` = `0x43ff40` = `rand()/32767.0` (**[0, 1] inclusief**), `cos512[i]` = `[0x5e823c][i]` = `cos(2π·i/512)`
(512 floats). `[0x4b798c]` is een dword in .data met waarde **128** (nergens geschreven): `−cos512[(a+128)&511] = sin(2π·a/512)`.
`ftol` = `0x499580` (**afkappen**, zet de FPU tijdelijk op truncatie); `fistp` = afronden met de actieve FPU-modus
(**naar dichtstbij**, standaard).

**Zekerheid.** Alles in §2–§6 is instructie voor instructie gelezen (✔). Onzeker (≈) staat er expliciet bij.

---

## 0. Samenvatting

`Splash(C, v, r)` zet **één emitter** (1.0 s) in de effectpool. Die emitter tekent zelf niets; hij spawnt

* in de eerste **0.3 s** **500 druppels per seconde** (≈ 140–150 in totaal) op een ring met straal `R = r + rnd·50` rond `C`:
  elke druppel is een **additieve getextureerde lijn** (bank 0 beeld **57**) die in 0.4–0.8 s 150–250 eenheden naar
  buiten vliegt langs een boog met hoogte `v·0.1` en bij het neerkomen een **klein rimpelkringetje** (bank 0 beeld **3**,
  0.6 s, plat op het water) achterlaat;
* elke **0.2 s** (de eerste meteen) een **grote kring** (bank 0 beeld **58**, 0.7 s, plat op het water, additief) om `C`
  die groeit van halve diagonaal `2R` tot `2R + 600`; in totaal **5 kringen**.

Geen geluid, geen camera-effect, geen licht, geen zwaartekracht (de boog is een sinus, geen integratie), geen ringdecal op
het wateroppervlak zelf (de kringen en rimpels zíjn de "decals": platte sprites met normaal (0, 1, 0)).

---

## 1. Signatuur en aanroepers

```c
void Splash(const vec3 *C, float v /* "snelheid" */, float r /* straal */);   /* 0x478660, cdecl (aanroeper ruimt 0xc op) */
```

Er zijn precies **drie** `call 0x478660` in de exe en **geen** enkele verwijzing naar `0x478660` als data (dword-zoektocht
over het hele bestand: 0 treffers). Ook de callbacks `0x478360`/`0x477fa0`/`0x478290`/`0x4781b0` komen elk maar één keer
als data voor (in hun eigen spawner). Er is dus **geen** plons voor vijanden, bommen, projectielen of "Perso stapt in het
water" in de code: alleen verdrinken en het script.

| # | plaats | context | `C` | `v` | `r` |
|---|---|---|---|---|---|
| 1 | `0x44c33e` in `Kill` `0x44c110` (vtbl[38], tak `0x44c308`) | Perso `Kill(7)`, normaal | `P.inst.pos + (0, 110, 0)` (`P+0xc..0x14`, `0x4aa390` = 110.0) | `0x44d170(P)` = `|P+0x204..0x20c| / P+0x2f8` = lengte van de verplaatsing van dit frame / dt (u/s) | **50.0** (`push 0x42480000`) |
| 2 | `0x44c65e` in race-Kill `0x44c4c0` (tak `0x44c623`) | idem voor Perso-subtype 4/5 (race, `0x44c16d`/`0x44c17d`) | idem | idem | **50.0** |
| 3 | `0x46ce27` in `0x46cdfd` | scriptbericht **1505** `[inst, f]` | `&level->inst[arg0 & 0xffffff]->pos` (`inst+0xc`, **geen** offset) | **1000.0** (`push 0x447a0000`) | `(float)(int)f · 0.01` (`fild`, `0x4aa0ac` = 0.01) |

**Kill(7)** komt maar uit één plek: het watervolume (`0x4747f0` → `0x44d160` = `P->vtbl[0x98](7)`, WATER.md §4.2; alle
`vtbl[0x98]`-aanroepen met een constante zijn nagelopen). Kill zelf laat soort 7 alleen door als de Perso nog niet aan
het verdrinken is (`0x44c1e6..0x44c20d`, race `0x44c4e3..0x44c4f8`), dus de plons komt **één keer**, ook al test het
water elk frame. Bijbehorend in Kill (niet in de plons): `+0x288 = 0` (race: 4.0), camera bevriest op zijn plek en kijkt
de speler na (`0x459030`, PERSO_DEATH §4.4), `0x462c90(J)` (valt niet verder). Typisch: val met 1000–1500 u/s ⇒
`h = 1.0..1.5` ⇒ druppelboog 100–150 hoog; `R ∈ [50, 100]`. `C.y` = voeten + 110; het water doodt als voeten + 120 onder de
bovenkant zitten en het rooster ligt 10 boven die bovenkant (WATER.md §2 punt 6), dus `C` ligt **≥ 20 eenheden onder het
getekende oppervlak** (plus de val van dat frame). Omdat het water geen z schrijft en vóór de additieve lijst 3 getekend
wordt, zie je de kringen door het water heen.

**Bericht 1505** (`0x46cdfd`: `fild [msg+0xc]; fmul 0.01; …; push 1000.0; push &inst->pos`): 45 keer in 7 levels (MESSAGES.md
zegt "gebruik 0" en WATER.md "W2A 10×"; beide fout). Alle gevallen, uit `tools/ekodisasm.py extract/Data`:

| level | trigger | 1505-args | `R` | geluid dat het script erbij speelt (SOUND.md) |
|---|---|---|---|---|
| W2A (17×, obj 130–144, 148, 159) | `COL_FLAG5 0..16` (Woody op botsingsslot n) | `[0x1000082.., 20000]` | 200..250 | 1622 `[inst, 0x1000002, 25]` (3D, 6× incl. één met vol 200), rest geen |
| W2B (9×, obj 18–28) | `COL_FLAG5 16..26` | `[.., 20000]` | 200..250 | 1627 `[inst, 0x1000001, 130]` |
| W2D (10×, obj 291–329) | `VOL_FLAG4 65..83` | `[292.., 20000]` (7×), `[306/308/330, 10000]` (3×) | 200..250 / 100..150 | 1622 `[250.., 0x1000000, var28]` |
| K2A (5×, obj 552–556) | `COL_FLAG5 28..32` | `[0x1000228.., 20000]` | 200..250 | 1622 `[inst, 0x1000000, 100]` |
| S2A (2×, obj 534–535) | `COL_FLAG5 17..18` | `[0x1000216.., 20000]` | 200..250 | 1622 `[inst, 0x1000000, 100]` |
| K2R (1×, obj 495) | `VOL_FLAG4 138` | `[318, 50]` | **0.5..50.5** | 1600 `[0x1000000, 50]` (2D) |
| S2R (1×, obj 397) | `VOL_FLAG4 49` | `[422, 50]` | **0.5..50.5** | 1600 `[0x1000000, 50]` (2D) |

De `COL_FLAG5`-gevallen zijn (vermoedelijk) wegzakkende drijvende platforms: vlak bij de 1505 start het script animatie
`3 [inst, 1, 1, 50]` (W2A, ervóór) of `3 [inst, 1, 1, 600]` (K2A/S2A, erna) en zet hem na een `DELAY` terug. De `VOL_FLAG4`-gevallen in de race-levels K2R/S2R zijn verdrinkplekken (kleine plons + 2D-geluid). Het **geluid komt
dus altijd uit het script, nooit uit `0x478660`.** Het tweede argument zonder `0x1000000`-vlag (W2D/K2R/S2R) is gewoon een
kaal instantieslot (`& 0xffffff`).

---

## 2. De spawner `0x478660` ✔

```c
void Splash(const vec3 *C, float v, float r)                  /* 0x478660 */
{
    FxRec *e = FxAlloc();          /* inline: n = [fx+0x27100]; if (n >= 2000) return; [fx+0x27100] = n+1; e = fx + n*0x50 */
    if (!e) return;                /* vol ⇒ stil niets */
    e->age   /*+0x00*/ = 0;
    e->life  /*+0x04*/ = 1.0f;                     /* 0x3f800000 imm */
    e->cb    /*+0x4c*/ = Splash_Emit;              /* 0x478360 */
    e->C     /*+0x08..0x10*/ = *C;
    e->h     /*+0x1c*/ = v * 0.001f;               /* 0x4aa0f4 = 0.001 */
    e->accD  /*+0x14*/ = 0;                        /* druppel-accumulator */
    e->accK  /*+0x18*/ = 0.2f;                     /* kring-accumulator, 0x3e4ccccd imm ⇒ eerste kring in het eerste frame */
    e->R     /*+0x20*/ = rnd() * 50.0f + r;        /* 0x4a9030 = 50.0; de enige rnd()-aanroep */
}
```

Recordlayout van de emitter: `+0` leeftijd, `+4` levensduur, `+8..+0x10` C, `+0x14` accD, `+0x18` accK, `+0x1c` h,
`+0x20` R, `+0x4c` callback. De driver `0x470c70` roept `cb(rec)` (cdecl) aan zolang `life > 0` en ruimt een record met
`life ≤ 0` pas bij de **volgende** doorloop op (wisselen met de laatste); hij leest de grens elke iteratie opnieuw, dus
records die tijdens de doorloop gespawnd worden krijgen in **hetzelfde frame** al hun eerste update (leeftijd = dt bij
de eerste tekening).

## 3. Emitter-update `0x478360` ✔

```c
void Splash_Emit(FxRec *e)                                    /* 0x478360 */
{
    e->age += dt;  e->accD += dt;  e->accK += dt;
    float u = e->age / e->life;                               /* life = 1.0 */
    if (!(u < 1.0f)) { e->life = -1.0f; return; }             /* 0x47864f: in het laatste frame géén spawns */
    if (u < 0.3f) {                                           /* 0x4aab98 = 0.3 */
        int n = ftol(e->accD * 500.0f);                       /* 0x4a9998 = 500 */
        e->accD -= n * 0.002f;                                /* 0x4abd54 = 0.002 */
        for (; n > 0; n--) SpawnDrop(e);                      /* 0x4783e8; pool vol ⇒ deze overslaan, lus loopt door */
    }
    int n = ftol(e->accK * 5.0f);                             /* 0x4a9884 = 5.0 */
    e->accK -= n * 0.2f;                                      /* 0x4a9760 = 0.2 */
    for (; n > 0; n--) SpawnRing(e);                          /* 0x4785cc */
}
```

* Druppels: 500/s zolang `age < 0.3` ⇒ bij 60 fps 8 per frame (eerste frame `ftol(8.33) = 8`), **≈ 141–150 totaal**.
* Kringen: bij `age ≈ dt, 0.2, 0.4, 0.6, 0.8` ⇒ **5 kringen** (een 6e alleen als float-afronding `age` net onder 1.0
  houdt terwijl `accK` 0.2 haalt — in de praktijk niet).

### 3.1 SpawnDrop (inline `0x4783e8..0x47858f`) ✔ — zeven `rnd()`-aanroepen in deze volgorde

```c
FxRec *d = FxAlloc(); if (!d) continue;
d->age  = 0;
d->life = (rnd() + 1.0f) * 0.4f;                    /* #1; 0x4a900c = 1.0, 0x4aa394 = 0.4 ⇒ 0.4..0.8 s */
d->cb   = Drop_Update;                              /* 0x477fa0 */
int a   = ftol(rnd() * 511.0f);                     /* #2; 0x4abc90 = 511 ⇒ 0..511 (511 alleen bij rand()==32767) */
float c = cos512[a & 511], s = -cos512[(a + 128) & 511];         /* s = sin(2πa/512) */
d->p0.x /*+0x08*/ = (rnd() * 20.0f + e->R) * c + e->C.x;        /* #3; 0x4a9994 = 20 */
d->p0.y /*+0x0c*/ = e->C.y;
d->p0.z /*+0x10*/ = (rnd() * 20.0f + e->R) * s + e->C.z;        /* #4: eigen jitter per as (niet zuiver radiaal) */
d->dir  /*+0x14..0x1c*/ = (c·R, 0, s·R);  if (|dir| > 0) dir /= |dir|;   /* ⇒ (c, 0, s); bij R == 0 blijft hij (0,0,0) */
d->f24  /*+0x24*/ = (rnd() + 1.0f) * 10.0f;         /* #5; 0x4a9750 = 10; GEEN lezer */
d->i20  /*+0x20*/ = fistp(rnd() * 511.0f);          /* #6; int; GEEN lezer */
d->h    /*+0x28*/ = e->h;
d->D    /*+0x2c*/ = rnd() * 100.0f + 200.0f - 50.0f;  /* #7; 0x4a9010 = 100, 0x4aa164 = 200, 0x4a9030 = 50 ⇒ 150..250 */
```

### 3.2 SpawnRing (inline `0x4785cc..0x47863f`) ✔

```c
FxRec *k = FxAlloc(); if (!k) continue;
k->age = 0;  k->life = 0.7f;                         /* 0x3f333333 imm */
k->cb  = Ring_Update;                                /* 0x4781b0 */
k->pos /*+0x08..0x10*/ = e->C;
k->R   /*+0x14*/ = e->R;
k->rot /*+0x18*/ = fistp(rnd() * 512.0f);            /* 0x4a9874 = 512 ⇒ int 0..512, 1/512 omwenteling */
```

## 4. De druppel `0x477fa0` ✔ (lijn, geen sprite)

```c
void Drop_Update(FxRec *d)                                    /* 0x477fa0 */
{
    d->age += dt;
    float w = d->age / d->life;
    if (!(w < 1.0f)) goto land;                               /* 0x478100 */
    int i0 = fistp(w * 255.0f);                               /* 0x4aa308 = 255 */
    S->p0 /*+0x278*/ = { d->p0.x + d->dir.x * d->D * w,
                         d->p0.y - cos512[(i0 + 128) & 511] * d->h * 100.0f,     /* = p0.y + sin(2π·i0/512)·100h; 0x4a9010 */
                         d->p0.z + d->dir.z * d->D * w };
    float w8 = w + 0.08f;                                     /* 0x4aace4 = 0.08 */
    int i1 = fistp((w + 0.1f) * 255.0f);                      /* 0x4a9008 = 0.1 (let op: y gebruikt +0.1, x/z +0.08) */
    S->p1 /*+0x284*/ = { d->p0.x + d->dir.x * d->D * w8,
                         d->p0.y - cos512[(i1 + 128) & 511] * d->h * 100.0f,
                         d->p0.z + d->dir.z * d->D * w8 };
    S->rgba0.a /*+0x29c*/ = 0.0f;                             /* rgb0 (+0x290..0x298) wordt NIET gezet: restant van de vorige lijn */
    S->rgba1   /*+0x2a0..0x2ac*/ = (0.5f, 0.5f, 0.5f, 0.65f);   /* 0x3f000000 ×3, 0x3f266666 */
    S->hw      /*+0x2b0*/ = 4.0f;                             /* 0x40800000: halve breedte ⇒ band van 8 breed */
    S->tex     /*+0x2b4*/ = 0x10039;                          /* bank 0 beeld 57 */
    Line(S, 0xe00);                                           /* 0x471a10: 0x800 eigen kleuren, 0x400 eigen breedte, 0x200 getextureerd */
    return;
land:                                                         /* 0x478100: rimpel op het eindpunt, druppel weg */
    FxRec *r = FxAlloc();
    if (r) { r->age = 0; r->life = 0.6f;                      /* 0x3f19999a */
             r->cb = Ripple_Update;                           /* 0x478290 */
             r->pos = { d->p0.x + d->dir.x * d->D * w,        /* hetzelfde punt als S->p0 met déze w (≥ 1, dus iets voorbij) */
                        d->p0.y - cos512[(fistp(w * 255.0f) + 128) & 511] * d->h * 100.0f,
                        d->p0.z + d->dir.z * d->D * w }; }
    d->life = -1.0f;                                          /* ook als de pool vol was */
}
```

* **Baan**: horizontaal lineair van `p0` naar `p0 + D·dir` (150–250 eenheden in 0.4–0.8 s ⇒ 190–625 u/s), verticaal
  `100·h·sin(π·round(255w)/256)` — een halve sinus, top `100h` op `w ≈ 0.5`, in stapjes van 1/255 (fistp). Geen zwaartekracht.
* **Streep**: van `P(w)` (staart, alfa 0) naar het punt 0.08 verder langs de baan (kop, alfa 0.65); de y van de kop
  hoort bij `w + 0.1`, dus de streep staat iets steiler dan de baan. Lengte ≈ `0.08·D` = 12–20 horizontaal. Voor
  `w > 0.9` ligt `w + 0.1` voorbij 1 ⇒ `sin < 0` ⇒ de kop duikt net onder `C.y`.
* **Lijnprimitief `0x471a10`** (OBJECTS.md §2.1): beide punten naar view-space, loodlijn op het geprojecteerde segment,
  camera-gerichte quad `p0 ± n·hw`, `p1 ± n·hw`; vertices v0/v1 bij p0 met rgba0, v2/v3 bij p1 met rgba1. UV (vast,
  `0x470d80(0, 1)` op `0x470d6c`): v0 `(0,0)`, v1 `(0,1)`, v2 `(1,1)`, v3 `(1,0)` ⇒ **u loopt van p0 (0) naar p1 (1)**.
  Ingediend met `0x481560(4 vertices, …, surface, 0x24)`: vlag 4 = **additief ONE/ONE**, z-test aan, z-write uit (lijst 3).
* Dat rgb0 niet gezet wordt is onschadelijk: in het additieve pad wordt rgb met alfa vermenigvuldigd (§7) en alfa0 = 0.

## 5. De rimpel `0x478290` ✔ (bij elke neergekomen druppel)

```c
void Ripple_Update(FxRec *r)                                  /* 0x478290, life 0.6 s */
{
    r->age += dt;  float u = r->age / r->life;
    if (!(u < 1.0f)) { r->life = -1.0f; return; }
    S->pos    /*+0x208*/ = r->pos;
    S->normal /*+0x230*/ = (0, 1.0f, 0);
    S->rgb    /*+0x214*/ = (0.65f, 0.65f, 0.8f);             /* 0x3f266666 ×2, 0x3f4ccccd */
    S->a      /*+0x220*/ = (1.0f - u) * 0.3f;                /* 0x4a900c, 0x4aab98 */
    S->mode   /*+0x260*/ = 0x12;
    S->tex    /*+0x228*/ = 0x10003;                           /* bank 0 beeld 3 */
    S->size   /*+0x264*/ = u * 25.0f + 5.0f;                  /* 0x4abc64 = 25, 0x4a9884 = 5; halve DIAGONAAL */
    DrawSprite(S, 2);                                         /* 0x470f10: eigen kleur; geen bit 0 ⇒ plat in het vlak ⊥ normaal; geen bit 2 ⇒ rotatie genegeerd; geen bit 3 ⇒ additief */
}
```

## 6. De kring `0x4781b0` ✔ (5× per plons, om het middelpunt)

```c
void Ring_Update(FxRec *k)                                    /* 0x4781b0, life 0.7 s */
{
    k->age += dt;  float u = k->age / k->life;
    if (!(u < 1.0f)) { k->life = -1.0f; return; }
    S->pos    = k->pos;                                       /* = C */
    S->normal = (0, 1.0f, 0);
    S->rgb    = (0.65f, 0.65f, 0.8f);
    S->a      = (1.0f - u) * 0.3f;
    S->mode   = 0x12;
    S->size   = 2.0f * k->R + u * 600.0f;                     /* fadd st0,st0 ; 0x4ab3bc = 600 */
    S->tex    = 0x1003a;                                      /* bank 0 beeld 58 (= dezelfde ring als het kielzog, WATER.md §4.1) */
    S->rot    /*+0x224*/ = k->rot;
    DrawSprite(S, 6);                                         /* eigen kleur + rotatie, plat, additief */
}
```

**Maat.** `size` is de halve **diagonaal** (hoeken op `size·(cos, sin)(rot ± 45°)`, BONUS.md §2.4), zijde = `1.4142·size`.
Beeld 58 (en 3) is een dunne witte ring op zwart, 64×64, bpp 24 (geen alfa); de helderheidspiek ligt op straal **21.5 px van
32** ⇒ zichtbare ringstraal ≈ `0.672 · size/√2` = **0.475·size**. Kring: straal ≈ `0.95·R` bij de start (precies waar de
druppels vertrekken) tot `0.95·R + 285` aan het eind; Kill(7): 47–95 → 330–380; bericht 1505 met 20000: 190–238 → 475–523.
Rimpel: straal 2.4 → 14.3. De rotatie is visueel zonder effect (rotatiesymmetrisch beeld).

**Beelden** (Common/Woody.rck, Knothead.rck en Splinter.rck zijn byte-gelijk voor deze drie): **3** = **58** (byte-identiek,
64×64, gemiddelde 46.5/255), **57** = zachte witte druppel/streep, 32×32, bpp 24, gemiddelde 152.5/255.

## 7. Kleur en blending (✔, geldt voor álle additieve sprites en lijnen)

Het submitpad `0x481560` kiest op zijn 4e argument (sprite `0x4719b2..0x4719c9`: `0x20 | (bit 3 ? 8 : 4)`, lijn: `0x24`):

* **vlag 8** (alfa-blend, `0x481a05..`): kleurbyte = `c·255` (`0x4aa308`), alfabyte = `a·255` ⇒ met MODULATE2X is rgb 0.5 = 1.0.
* **vlag 4** (additief, `0x481d8c` → `0x481e5e..0x481f37`): kleurbyte = **`a · c · 128`** (`0x4a9020` = 128.0), geen
  alfabyte; blend ONE/ONE (`0x429182`/`0x429198`: SRCBLEND = DESTBLEND = 2). Met MODULATE2X (LIGHTING.md §1.5 stap 6) is
  de bijdrage dus **`textuur · rgb · alfa · 1.004`**: alfa werkt als helderheid en rgb 0.5 is hier **half**, niet vol wit.

Voor de plons: kring/rimpel piek `(0.65, 0.65, 0.8)·0.3 = (0.195, 0.195, 0.24)` × textuur, lineair naar 0 — een vage
blauwwitte kring; druppelkop `0.5·0.65 = 0.325` grijs, staart 0.

> Bijvangst (niet in deze taak aangepast): BONUS.md §2.4 ("kleur (0.5,0.5,0.5) = vol wit") geldt dus alleen voor
> alfa-geblende sprites. Het pickup-deeltje `0x4791f0` is additief (vlag 7) ⇒ effectief 0.5; `game_pickup_fx` in de port
> tekent het met `white = {1,1,1}` en is daarmee 2× te fel. Het kielzog in `src/water.c` (rauwe 0.8) is wel correct.

## 8. Constanten

| adres | waarde | gebruik |
|---|---|---|
| `0x4aa0f4` | 0.001 | `h = v·0.001` |
| `0x4a9030` | 50.0 | `R = r + rnd·50`; −50 in D |
| imm `0x3e4ccccd` | 0.2 | accK start |
| `0x4a900c` | 1.0 | levensduur emitter, `u < 1`-tests, `1 − u`, `rnd + 1` |
| `0x4aab98` | 0.3 | druppelvenster; alfafactor kring/rimpel |
| `0x4a9998` | 500.0 | druppels per s |
| `0x4abd54` | 0.002 | 1/500 |
| `0x4a9884` | 5.0 | kringen per s; +5 rimpelgrootte |
| `0x4a9760` | 0.2 | 1/5 |
| `0x4aa394` | 0.4 | druppelleven `(rnd+1)·0.4` |
| `0x4abc90` | 511.0 | druppelhoek, `+0x20` |
| `0x4a9994` | 20.0 | radiale jitter |
| `0x4a9750` | 10.0 | `+0x24` (ongebruikt) |
| `0x4a9010` | 100.0 | `D`-bereik; boog `h·100` |
| `0x4aa164` | 200.0 | `D = rnd·100 + 200 − 50` |
| `0x4a9004` | 0.0 | normalisatietest |
| `0x4a9874` | 512.0 | kringrotatie |
| `0x4aa308` | 255.0 | `round(255·w)` |
| `0x4aace4` | 0.08 | x/z-voorsprong van de kop |
| `0x4a9008` | 0.1 | y-voorsprong van de kop |
| imm | 0.5 ×3, 0.65, 4.0, `0x10039` | lijn rgb1, alfa1, halve breedte, beeld 57 |
| imm `0x3f19999a` | 0.6 | levensduur rimpel |
| imm `0x3f333333` | 0.7 | levensduur kring |
| imm `0x3f266666`, `0x3f4ccccd` | 0.65, 0.8 | kring/rimpel rgb |
| `0x4abc64` | 25.0 | rimpelgroei |
| `0x4ab3bc` | 600.0 | kringgroei |
| `[0x4b798c]` | int 128 | kwartslag in `cos512` |
| `0x4aa390` | 110.0 | Kill(7): `C = pos + (0,110,0)` |
| `0x4aa0ac` | 0.01 | bericht 1505: `r = f·0.01` |
| `0x4a9020` | 128.0 | additief submitpad: kleurbyte `a·c·128` |

## 9. Port-recept

Alles gaat in de bestaande pickup-pool van `src/main_engine.c` (`FxRec g_fx[2000]`, `fx_new`, `fx_update`) — dezelfde
pool als het origineel, en `fx_update` verwerkt al records die tijdens de doorloop ontstaan in hetzelfde frame.

**`src/hud.c` / `src/hud.h`**
1. `fx_slot`: beeld 57 een eigen slot geven (`image == 57 ? 14 : …`), `GLuint fx[14]` → `fx[15]`, en de vrijgeeflus in
   `hud_free` (`i < 14`) → `i < 15`. Beeld 3 hoeft niet geladen te worden: het is byte-gelijk aan 58 (slot 11) — geef bij
   de rimpel `0x3a` mee met een commentaar.
2. Een getextureerde lijn per beeld: `void hud_world_streak(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b)`
   = `world_line(a, b, eye, hw, rgb, alpha_a, alpha_b, H.fx[fx_slot(image)])` (+ declaratie in hud.h). `world_line` heeft
   de juiste UV (u 0 bij a, 1 bij b), ONE/ONE en `glColor3f(rgb·alpha)` — precies §7.
3. `hud_world_fx_plane` volstaat voor kring en rimpel (normaal (0,1,0); de rotatie mag weg, het beeld is rond).

**`src/main_engine.c`**
4. `FxRec` uitbreiden: `Vec3 dir; float D, h, R, acc2; int rot;` en soorten `3` = emitter (`0x478360`), `4` = druppel
   (`0x477fa0`), `5` = rimpel (`0x478290`), `6` = kring (`0x4781b0`). `fx_new` zet de nieuwe velden op 0.
5. Spawner, gedeclareerd in `src/player.h` naast `game_land_dust` (player.c roept hem aan):
   ```c
   void game_splash(Vec3 c, float speed, float radius)                           /* 0x478660 (docs/SPLASH.md) */
   {
       FxRec *e = fx_new(1.0f, c, 3); if (!e) return;
       e->h = speed * 0.001f; e->acc = 0; e->acc2 = 0.2f; e->R = fx_rnd() * 50.0f + radius;
   }
   ```
6. In `fx_update`. Die rekent al `u = (age += dt) / life`, laat bij `u < 1` het record staan (`continue`) en wisselt het
   anders met het laatste (`i--`) — gelijk aan `!(u < 1) ⇒ life = −1` van het origineel. `fx_new` schrijft in een vaste
   array, dus `e` blijft geldig; een record dat tijdens de lus ontstaat wordt nog in dit frame bijgewerkt, zoals in `0x470c70`.
   Vóór de bestaande `if (u < 1.0f)`:
   ```c
   if (e->kind == 4 && u >= 1.0f) fx_new(0.6f, drop_pt(e, u, u), 5);   /* 0x478100: a drop that lands leaves a ripple (at w >= 1) */
   ```
   en binnen `if (u < 1.0f)` drie takken erbij:
   ```c
   } else if (e->kind == 3) {                                         /* 0x478360: the emitter, draws nothing */
       e->acc += dt; e->acc2 += dt;
       if (u < 0.3f) {
           int n = (int)(e->acc * 500.0f); e->acc -= n * 0.002f;      /* ftol truncates */
           while (n-- > 0) {
               FxRec *d = fx_new((fx_rnd() + 1.0f) * 0.4f, e->pos, 4); if (!d) continue;   /* rand #1; p0.y = C.y */
               int a = (int)(fx_rnd() * 511.0f);                                            /* rand #2 */
               float c = cos512(a), s = sin512(a);
               d->pos.x = (fx_rnd() * 20.0f + e->R) * c + e->pos.x;                         /* rand #3 */
               d->pos.z = (fx_rnd() * 20.0f + e->R) * s + e->pos.z;                         /* rand #4 */
               d->dir = e->R > 0 ? (Vec3){ c, 0, s } : (Vec3){ 0, 0, 0 };
               fx_rnd(); fx_rnd();                                    /* rand #5, #6: +0x24/+0x20, never read */
               d->h = e->h; d->D = fx_rnd() * 100.0f + 150.0f;         /* rand #7 */
           }
       }
       int n = (int)(e->acc2 * 5.0f); e->acc2 -= n * 0.2f;
       while (n-- > 0) { FxRec *k = fx_new(0.7f, e->pos, 6); if (k) { k->R = e->R; k->rot = (int)lrintf(fx_rnd() * 512.0f); } }
   } else if (e->kind == 4) {                                         /* 0x477fa0: an additive streak along a half-sine arc */
       Vec3 a = drop_pt(e, u, u), b = drop_pt(e, u + 0.08f, u + 0.1f);
       hud_world_streak(57, &a.x, &b.x, eye, 4.0f, grey05, 0.0f, 0.65f);
   } else if (e->kind == 5) {                                         /* 0x478290: ripple where a drop landed */
       hud_world_fx_plane(0x3a /* image 3 == image 58 */, &e->pos.x, up, u * 25.0f + 5.0f, blue, (1.0f - u) * 0.3f);
   } else if (e->kind == 6) {                                         /* 0x4781b0: the growing ring round the centre */
       hud_world_fx_plane(0x3a, &e->pos.x, up, 2.0f * e->R + 600.0f * u, blue, (1.0f - u) * 0.3f);
   }
   ```
   met `grey05 = {0.5, 0.5, 0.5}`, `blue = {0.65, 0.65, 0.8}` (rauw, §7: géén ×2), `up = {0, 1, 0}`,
   `cos512(i) = cos(2π·(i & 511)/512)`, `sin512(i) = −cos512(i + 128)` en
   `drop_pt(e, wx, wy) = { pos.x + dir.x·D·wx, pos.y + sin512(lrintf(255·wy))·h·100, pos.z + dir.z·D·wx }`
   (`lrintf` = fistp, afronden; `e->pos` van een druppel is zijn `p0`). Op de kring de rotatie `rot` weglaten mag
   (rond beeld); wie hem wil: `hud_world_fx_plane` een `turns`-argument geven (`rot/512`).
7. `fx_update(float dt)` → `fx_update(float dt, const float *eye)`; aanroep op de tekenregel (nu `fx_update(paused ? 0 : dt)`,
   ±r. 2649) wordt `fx_update(paused ? 0 : dt, &cam.pos.x)`. Die staat binnen `hud_world_sprites_begin/end`, zoals nodig.
8. Bericht 1505 in `on_msg` (naast 1506):
   `case 1505: if (in && m->nargs > 1) game_splash(in->position, 1000.0f, (float)(int32_t)m->args[1] * 0.01f); break;   /* 0x46cdfd */`

**`src/player.c`** (`player_kill`, beide takken, pas ná de "al aan het verdrinken"-test):
9. `if (kind == 7) game_splash((Vec3){ p->pos.x, p->pos.y + 110.0f, p->pos.z }, sqrtf(p->vel.x * p->vel.x + p->vel.y * p->vel.y + p->vel.z * p->vel.z), 50.0f);`
   — `p->vel` = verplaatsing/dt van het laatste frame (`src/player.c` ±r. 1405) = `0x44d170`. Race-tak idem (`0x44c65e`).

**Niet nodig**: `src/water.c` verandert niet (Kill(7) loopt al via `player_kill`); geen geluid toevoegen (komt uit de
scripts: 1622/1627/1600 worden al door de geluidsmanager afgehandeld).

**Test**: `extract/Data W2A --pos -700 200 500` (verdrinken, WATER.md §5): één plons ≈ 20 onder het oppervlak;
W2A de drijvende platforms (COL_FLAG5 0..16) voor bericht 1505 met R = 200..250.

## 10. Zeker / onzeker

* ✔ Alle rekenregels, constanten, recordvelden, volgorde van de `rnd()`-aanroepen, beelden, spritevlaggen, het additieve
  submitpad (`a·c·128`) en de UV van de lijn.
* ✔ Er zijn geen andere aanroepers (drie `call`s, geen data-verwijzingen).
* ≈ `P.inst.pos` (`P+0xc`) vs. `P+0x1f4`: de Kill-tak leest `+0xc..0x14`; aangenomen dat dat de voetpositie van dit frame
  is (in de port `p->pos`).
* ≈ Of de Perso-verplaatsing `+0x204` op het moment van Kill(7) al die van het huidige frame is (volgorde Perso-update vs.
  water-Update) — verschil is hooguit één frame snelheid.
* ≈ Niet vergeleken met het draaiende origineel (geen trace/screenshot).
* Correcties op eerdere docs: MESSAGES.md 1505 "gebruik 0" → 45× (7 levels); WATER.md §6 "W2A 10×" → 17×. PERSO_DEATH.md
  §4.3 klopt, met als aanvulling: aparte jitter per as, `dir` is de genormaliseerde (c, 0, s), `+0x20`/`+0x24` ongebruikt,
  rgb0 van de lijn ongezet maar zonder effect, 5 kringen, en §7 hierboven (alfa = helderheid in het additieve pad).
