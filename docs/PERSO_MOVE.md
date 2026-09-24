# Perso (speler) – beweging, invoer, fysica, botsing

Werkdocument (incrementeel bijgewerkt). Alle adressen zijn in `Woody.exe` (imagebase 0x400000).
Notatie: `P` = de Perso-instantie (struct 0x754 bytes, vtable `0x4aabc0`), `dt` = `P+0x2f8` (frametijd in s).

## 1. Klassenhiërarchie

| klasse | ctor | vtable | grootte | rol |
|---|---|---|---|---|
| Instance (basis) | `0x42e1a0` | `0x4aa31c` | 0xfc..0x104 | .ins-instantie: positie `+0xc`, 3×3 rotatie `+0x28`, schaal `+0x4c`, wereldcel `+0x1c`, animatie `+0xa0..0xb4` (zie FORMAT_INS.md §2.6) |
| Npc | `0x40be10` | `0x4a9530` | 0x108+ | registreert zich in de npc-tabel `0x4c4e00[0x4c5318++]` (max 256, "Too many npc"); `+0x104` = typewoord (bits 0-4 = categorie via `0x40c360`, bits 5-9 = subtype via `0x40c380`) |
| Perso | `0x44a2d0` | `0x4aabc0` | 0x754 | de speler (script-types 1, 2, 3, 18, 19 → classmap_raw.txt) |

Ctor `0x44a2d0(subtype)`: basis-ctors, sub-objecten `+0x334` (ctor `0x462c50`, "springer/zwaartekracht"), `+0x388` (ctor `0x47fa30`, "stuurobject": richting uit camera + invoer), `+0x604` (ctor `0x4632c0`), `+0x110` (`0x4631b0`, plus `0x463280(kolom)`; jump-table `0x44a394` op subtype−1: **subtype 1→kolom 0, 2→2, 3→1, 4→4, 5→3** – gecorrigeerd, komt overeen met PERSO_FRAME §2.6). Script-type → subtype (fabriek `0x403502`, tabel `0x403f3c`/`0x403e94`): type 1→1, 2→3, 3→2, 18→5, 19→4; dus **type 1/2/3 = kolom 0/1/2** (de drie loop-personages; kolom 0 = type 1 = Woody) en type 18/19 = kolom 3/4 (max. snelheid 1250: voertuig-varianten). Elke variant schrijft de pointer naar `[0x53a34c]` (`0x40361e`). Categorie (bits 0-4) = 1, subtype (bits 5-9) = ctor-argument.

Vtable `0x4aabc0` (relevante slots):

| slot | offset | functie | betekenis |
|---|---|---|---|
| 1 | +0x04 | `0x44a3d0` | Init na laden (.ins-positie → `P+0x1f4`, startrichting) |
| 17 | +0x44 | `0x44ab20` | Reset (respawn / levelstart) |
| 21 | +0x54 | `0x44c050` | positie + (0,1,0) teruggeven |
| 22 | +0x58 | `0x44cda0` | scriptberichten (1..999) |
| 26 | +0x68 | `0x44cf50` | render-kleur: wit knipperen zolang timer `+0x704` loopt (§4.4) |
| 34 | +0x88 | `0x44c030` | pointer naar positie `P+0x1f4` |
| 35 | +0x8c | `0x44c940` | ? |
| 36 | +0x90 | `0x44c720` | bool (gebruikt door 0x44c110 op npc's categorie 2 / subtype 12) |
| 38 | +0x98 | `0x44c110(soort)` | **Dood/verlies** (soort 1..9, jump-table `0x44c498`) |
| 39 | +0x9c | `0x44ca00` | **Geraakt/schade** (aangeroepen door vijanden) |

## 2. Perso-struct (velden)

(wordt aangevuld)

| offset | type | init | betekenis | bron |
|---|---|---|---|---|
| +0x0c | vec3 | .ins | render-positie van de instantie; elk frame gekopieerd uit `+0x1f4` (`0x44bf10`) | |
| +0x28 | mat3 | .ins | rotatie; elk frame herbouwd uit kijkrichting (`0x44bd30`) | |
| +0x4c | vec3 | 1,1,1 | schaal (`0x44ab20`) | |
| +0x54 | f32 | | schaal z ← `+0x2e8` elk frame (`0x44bd00`) | |
| +0x60 | vec3 | | "probe"-punt = positie + (0, `+0x110`, 0), gebruikt voor de wereldcel (`0x44bf10`) | |
| +0x104 | u32 | | typewoord (Npc) | |
| +0x100 | f32 | 100.0 | (basisinstantie) gezet door Reset `0x44ad6a`; 10000.0 in `0x44e690` | |
| +0x110 | f32 | 43.0 | = P+0 van het parameterblok (tabel `0x4b5f14` rij 0): hoogte van het peil-/probe-punt boven de voeten; tevens max. opstap in `0x436f00` | `0x463280` |
| +0x114 | f32 | 69.0 | P+4: botsstraal | `0x462543` |
| +0x118 | f32 | 193/61 | P+8: actuele lichaamshoogte (`0x462490`: gebukt `+0x694 ≠ 0` → P+0x10 = 61, anders P+0xc = 193) | |
| +0x1f4 | vec3 | .ins pos | **positie (voeten)**; gezaghebbend | `0x44a3d0`, `0x44bf10` |
| +0x204 | vec3 | 0 | **verplaatsing dit frame** (wereldruimte) = stuurvector + sprong/valvector (+ dt·`+0x244` in y) | `0x44bb20` |
| +0x210 | vec3 | 0,1,0 | gefilterde "op"-vector (0.9·oud + 0.5·vloernormaal) voor de rotatiematrix in toestand 1 | `0x44bd30` |
| +0x21c | u32 | 0/1 | **toestand** (state), zie §5 | `0x44c980` |
| +0x220 | u32 | | vorige toestand | `0x44c980` |
| +0x22c | u8 | 1 | vlag "op de grond?" (getter `0x44bcf0`) | |
| +0x238 | f32 | 0 | **bewegingsblokkade-timer** (`0x44cce0(t, force)`: max(oud, t)); zolang > 0: geen invoer, en de J-vector wordt genegeerd (afgeteld met dt in `0x44b530`). Gezet door bukken (dt per frame), harde landing (len anim 10), speciale aanval (len anim 0x13). Geen onkwetsbaarheid (PERSO_FRAME noemt hem zo – onjuist) | `0x44cce0` |
| +0x240 | f32 | 0 | timer (bv. na dood `0x44c110`: animatieduur): zolang > 0 geen zwaartekracht-update | |
| +0x244 | f32 | 0 | extra verticale snelheid dit frame (wordt aan y van `+0x204` toegevoegd, elk frame op 0) | |
| +0x24c | f32 | 1.0/3.0 | levens/"health"? (uit save `+0x18`; Reset: 3.0 als ≤ 0) | `0x44a6a0`, `0x44ab20` |
| +0x250 | u32 | 0x63 | uit save `+0xc` (score/munten?) | `0x44a6a0` |
| +0x25c | u32 | 0 | teller; bij ≥ 25 → -25 en `+0x24c` += 1 (max 5) anders `0x44c7a0(1)` | `0x44b530` |
| +0x26c | u32 | 0 | doodsoort (0 = levend) | `0x44c110` |
| +0x270 | f32 | 1.0 | **onkwetsbaarheidstimer** (−dt in `0x44b1b0`): zolang > 0 worden `Hit` en `Kill` 2,3,4,5,6,8,9 genegeerd; Reset zet 1.0 s; setter `0x44cd10` (max) | `0x44ca32`, `0x44c23d` |
| +0x280 | f32 | 0 | onkwetsbaar na treffer (`0x44cd30`: max(oud, P+0x5c = 0.6 s)) | `0x44ca49` |
| +0x2e0 / +0x2e4 | u32 / ptr | | wandcontact uit de sweep: 0 geen, 3 wereldwand, 2 instantie (+0x2e4 = instantie) – niet "grondtype" | `0x462693` |
| +0x308 | u32 | 0 | **grondsoort** uit het textuurbyte `tex+0x47` (1 = glad, 2 = stof), §6.4 | `0x462962` |
| +0x278 | s32 | -1 | aftel-timer; bij 0 → bericht 0x10 naar script (`0x443e90`) | |
| +0x288 | f32 | | duur/parameter van doodsanimatie (3.5, 3.0, 2.5, 1.5, 0) | `0x44c110` |
| +0x2e8 | f32 | 1.0 | schaal-z | |
| +0x2ec | ptr | | `[0x50944c]` leveltabel | `0x44ae20` |
| +0x2f0 | ptr | | `[0x509adc]` tijdobject: `[+0x38]` = dt | `0x44ae30` |
| +0x2f4 | ptr | | **controller** `[0x5e6188]` (zie §3) | `0x44ae40` |
| +0x2f8 | f32 | | dt (kopie, `0x44baf0`) | |
| +0x30c | vec3 | | startpositie level (kopie van `+0x1f4` in Init) | `0x44a3d0` |
| +0x318 | vec3 | | respawn-positie (checkpoint / SavePos.bin) | `0x44a920` |
| +0x324 | vec3 | | respawn-richting (genormaliseerd) | `0x44a3d0` |
| +0x334 | obj | | **springer/zwaartekracht**-object (ctor `0x462c50`) | |
| +0x388 | obj | | **stuurobject** (ctor `0x47fa30`; `+0x388+4` = P, `+8` = &P+0x110) | `0x459fd0` |
| +0x458 | vec3 | | vloernormaal (gebruikt voor `+0x210`) | `0x44bd30` |
| +0x494 | ptr | | animatiehulp-object (0x54 B, ctor `0x463df0`) ; `+0x49c`, `+0x4a0` = lengte anim 3 en 0x42 | `0x44ad90` |
| +0x4b4 | ptr | | gekoppelde instantie die positie/rotatie meekrijgt (`0x44bf10`) | |
| +0x4ec | u8 | | "op een vlak geplakt": vlak `+0x4f0..0x4fc` (n, d) beperkt de verplaatsing | `0x459eb0` |
| +0x5bc | vec3 / +0x5cd u8 | | verplaatsing geleverd door de sprong/aanval-controller `0x457a50` (PERSO_JUMP.md); vervangt stuur- + J-vector | `0x44bb20` |
| +0x604 | obj | | sub-object (ctor `0x4632c0`) | |
| +0x690 | u8 | | "toestand gewisseld dit frame → rest van update overslaan" | `0x44b530` |
| +0x69c | vec3 / +0x6ac u8 | | tweede override van de verplaatsing | `0x44bb20` |
| +0x6e4.. | | | timer-blok (`0x465fe0`) | |
| +0x6f8 | f32 | | timer (afgeteld met dt) | `0x465fe0` |
| +0x710 | obj | | (0x453c80/0x453ca0: timer/HUD?) | |

## 3. Invoer

### 3.1 Cfg-toetsen → actietabel (`0x44fbd0`, ctor van het app-object `[0x5e5814]`)

Het app-object (paden, cd-drive, `+0x104` = invoermodus, `+0x108` = actietabel) wordt door `0x44fa10` aangemaakt (caller `0x401f00`).
`+0x104`: `cfg+0x114 == 1` → 0 (alleen toetsenbord); anders `cfg+0x110 == 0` → 2, anders 1 (joystickmodi).
Actietabel `+0x108`: 12 acties × {toets uit config 1, toets uit config 2} (8 B per actie). Elke toetscode gaat door `0x44fe80`: codes < 0x200 worden door het DirectInput-toetsenbordobject `[0x5e6194]->vt[2](code)` vertaald; als dat 0x90 oplevert of er geen toetsenbord is → 0x90 (= ongeldig). Codes ≥ 0x200 zijn joystickknoppen (knop = code − 0x200, `0x44fed0`).

| actie-index | cfg-index (+0xac / +0xdc) | standaardtoets (Detect) | controller-actie (§3.2) |
|---|---|---|---|
| 0 | 2 | ← | 0 (waarde −1.0) |
| 1 | 3 | → | 1 (+1.0) |
| 2 | 0 | ↑ | 2 (−1.0) |
| 3 | 1 | ↓ | 3 (+1.0) |
| 4 | 6 | LCtrl | 4 **en** 12 |
| 5 | 4 | Spatie | 5 |
| 6 | 5 | LShift | 6 |
| 7 | 8 | Enter | 7 |
| 8 | 7 | LShift | 8 |
| 9 | 9 | Esc | 9 |
| 10 | 10 | Num0 | 10 |
| 11 | 11 | RCtrl | 11 |

(De cfg-index staat in de volgorde van de app-tabel: `0x4c2c84,0x4c2c88,0x4c2c7c,0x4c2c80,0x4c2c94,0x4c2c8c,0x4c2c90,0x4c2c9c,0x4c2c98,0x4c2ca0,0x4c2ca4,0x4c2ca8` voor config 1, idem +0x30 voor config 2.)

### 3.2 Controller-object `[0x5e6188]` (14 acties × 12 B, + `+0xa8` = dt)

```c
struct PadAction { float value; float held; uint32 state; };   // state: 1 = net ingedrukt, 2 = vastgehouden, bit31 = net losgelaten
struct Pad { uint32 hdr; PadAction a[14]; float dt; };           // a[i] op +4+12*i
```
- `0x467340(dt)`: begin frame – dt opslaan, alle `state &= 0x80000000`.
- `0x4673b0(i, v)`: actie i actief: `state = (held <= 0) ? 1 : 2` (met behoud van bit 31), `held += dt`, `value = v`.
- `0x467370()`: einde frame – bit 31 wissen; acties die dit frame niet gezet zijn maar `held != 0` hadden: `held = 0`, `state |= 0x80000000` (net losgelaten).
- Getters: `0x467400(i)` = ingedrukt (state & 0x7fffffff), `0x467420(i)` = net ingedrukt (state == 1), `0x467440(i)` = net losgelaten (bit 31), `0x467460(i)` = value (float; joystick-as).

### 3.3 Polling per frame (`0x402940`, methode van het hoofdobject)

1. `[this+8]->vt[0xc]()` (toetsenbord-update), debugtoetsen via `vt[0x14](scancode)` als `app+0x384 & 8` (debugmodus).
2. Joystick `[0x5e618c]` (alleen modus 1/2): `vt[4]()` poll; X-as `vt[0xc]` > 0 → actie 1 = X, < 0 → actie 0 = X; Y-as `vt[0x10]` > 0 → actie 3, < 0 → actie 2. Knoppen (via `0x44fed0`) voor acties 6, 11, 5, 4(+12), 7, 10, 8, 9.
3. Toetsenbord `[0x5e6194]` (`vt[0x10](key)` = ingedrukt): richtingen alleen in modus 0; acties 4(+12), 5, 6, 11, 7, 8, 10, 9 altijd. Waarden zoals in de tabel hierboven.
4. `0x467420(9)` (Esc net ingedrukt) → pauzemenu `0x404d80`.
5. `0x467370()` afsluiten.

De speler leest dus **nooit** toetsen direct; alles gaat via `P+0x2f4` (= `[0x5e6188]`) met de actie-indices 0..13.

### 3.4 Betekenis van de 14 acties voor Perso (leesplaatsen; `h` = `0x467400` ingedrukt, `p` = `0x467420` net ingedrukt, `r` = `0x467440` net losgelaten, `v` = `0x467460` waarde)

| actie | standaard | betekenis | waar gelezen |
|---|---|---|---|
| 0 / 1 | ← / → | **links / rechts** (waarde −1 / +1 → "b") | Mover-invoer `0x45a506..0x45a528` (h+v); altMode `0x45a7be/0x45a7cb`, `0x459c9d/0x459cae`; toestand 4 `0x4651f5/0x465205`, `0x46539f/0x4653d2` (v); toestand 1 `0x4565f1..0x456636` |
| 2 / 3 | ↑ / ↓ | **vooruit (van de camera af) / achteruit** (waarde −1 / +1 → "a"); hoek = π − atan2(b, a) | `0x45a4cb..0x45a4fd`; altMode `0x459d57/0x459d64` |
| 4 | LCtrl | **springen** (vasthouden = hoge sprong, loslaten = afkappen, §4.2) | `0x44bb10` (h) → `0x462d70`; ook in de aanvalscontroller `0x457adf`, `0x457c34`, `0x457deb` (h), `0x465a14`, `0x465e7d` (p) |
| 5 | Spatie | **bukken** (sub-automaat `+0x694`: 0→1 anim 0x31 (in), 2 anim 0x32 (houden), 3 anim 0x33 (opstaan); opstaan alleen als segment y+P+0x10 (61) … y+P+0xc (193) vrij is, `0x4359b0` → `[0x53a554] == 0`; in toestand 6 anims 0x4e/0x4f/0x50, in toestand 1 actie **8** i.p.v. 5 met anims 0x68/0x69/0x6a). Tijdens bukken elke frame `0x44cce0(dt,0)` ⇒ `+0x238 > 0` ⇒ geen beweging | `0x465b69` (h) in `0x465b10`; altMode `0x459d77` |
| 6 | LShift | **aanval (pikken)**: op de grond vasthouden/loslaten (r `0x457416`, h `0x4574b5`) → sub-toestand 9 (anim 0x10/0x11, `0x45745a`); in de lucht net ingedrukt (`0x4574f4`) → sub-toestand 1; combo in sub-toestand 5 (`0x4573d4`). Details: PERSO_JUMP.md | `0x457330`; verder `0x46344c`, `0x463954`, `0x464f48`, `0x465216`, `0x465a25` (p; in de code van toestand 6/4/8 – niet verder ontleed) |
| 7 | Enter | **rondkijk-modus** (toestand 3) aan/uit op loslaten | `0x44b9b7` (r) in `0x44b980` |
| 8 | LShift (cfg 7) | bukken-variant in toestand 1 (zie actie 5) | `0x465b65` |
| 9 | Esc | pauze (niet door Perso gelezen) | `0x402940`, `0x401500`, `0x403334` |
| 10 | Num0 | **camera achter de speler zetten** (0.5 s overgang; geluid 9 als het niet mag) | camera-controller `0x459163`, `0x45922d`, `0x45926a`, `0x4597a3` |
| 11 | RCtrl | **speciale aanval met voorraad** `+0x254` (r, op de grond, toestand 0, `+0x750 == 0`): voorraad−1, anim 0x13, beweging geblokkeerd voor de animatieduur (`0x44cce0`), alle actoren in `0x4c5258[]` binnen bereik krijgen `vt[39]`(schade P+0x94 = 3.0) en evt. `vt[38](3)`; anders geluid 9 | `0x458c0c` in `0x458bf0` |
| 12 | = toets van 4 | menu-bevestiging (`0x4465c6`, p) – niet door Perso gelezen | |
| 13 | – | nergens gezet of gelezen | |

`0x44cc30` = "een van de acties 0,3,2,1,6,4,10,5 ingedrukt" (gebruikt om idle-animaties af te breken).

## 4. Bewegingslogica

Overzicht per frame (toestand 0/6, `0x44bb20(P, 1)`; details horizontaal in PERSO_FRAME §2.3):

```c
void Perso_Move(Perso *p, bool input)                 /* 0x44bb20 */
{
    if (Timer6e4(p)) return;                           /* 0x465fe0 */
    if (p->M.pushTimer /*+0x474 = M+0xec, knockback*/ > 0 || p->t238 > 0 || p->sub5b4 != 0)  /* 0x44bb48..0x44bb78 (OF, niet EN: §7) */
        input = false;
    Mover_Update(&p->M, input);                        /* 0x45b110: PERSO_FRAME §2.3 */
    vec3 h = p->M.velDir * p->M.dist;                  /* 0x44d1e0: M+0x1c * M+0xe4 (= snelheid*dt) */
    vec3 v = {0,0,0};
    if (p->t240 > 0) p->t240 -= dt;                    /* 0x44bc04: tijdens doodsanimatie geen val */
    else { Jumper_Update(&p->J, Pressed(4), input);    /* 0x462d70, §4.2; 0x44bb10 = 0x467400(4) */
           v = p->J.disp; }                            /* 0x463130: J+8..0x10 (alleen y wordt gevuld) */
    if (p->t238 > 0) v = 0;                            /* 0x44bc2f */
    if      (p->use5bc) p->disp = p->v5bc;             /* +0x5cd: aanval/speciale sprong (PERSO_JUMP.md) */
    else if (p->use69c) p->disp = p->v69c;             /* +0x6ac */
    else                p->disp = h + v;               /* 0x44bc94 */
    p->disp.y += dt * p->vy244;                        /* 0x44bcbe */
    ClampToPlane(p);                                   /* 0x459eb0 (alleen +0x4ec) */
}
```

### 4.1 Horizontaal (samenvatting; alles in PERSO_FRAME §2.3 geverifieerd)

Kolom 0 (Woody): max. loopsnelheid **600 eenh./s** (P+0x1c), versnellen in **0.25 s** met `v = (t/0.25)²·doel`,
uitlopen in **0.1 s** met `v = max·(1−(t/0.1)²)`; er is **geen aparte ren-toets**: de snelheid schaalt met de
stick-uitslag (toetsenbord = ±1.0 → altijd 600) en met `(dot(nieuweRichting, oudeRichting)+1)/2` (scherp draaien remt).
De kijkrichting draait per frame met `slerp(oud, doel, |stick|·0.25)` (`0x45a320`, **framerate-afhankelijk**, geen dt).
Gewenste richting = `normalize(pos − camPos)` in xz, om y gedraaid met `π − atan2(x, y)` (`0x45a4b0`).

### 4.2 Verticaal: het springer-object `J = P+0x334` (`0x462d70`) – **geen zwaartekrachtconstante, maar parabolen in de tijd**

Layout J: `+0` Perso, `+4` parameterblok (P+0x110), `+8..0x10` verplaatsing dit frame (alleen `+0xc` = dy),
`+0x14` fase, `+0x18` "snelheid" S, `+0x1c` t (s, negatief tijdens stijgen), `+0x20` vorige hoogte h, `+0x24` laatst gemeten
valsnelheid (+ = omlaag), `+0x28` gevallen hoogte (= `Perso+0x35c`, zie PERSO_FRAME), `+0x2c/+0x30/+0x34` = parameter-indices
**0x19 / 0x1b / 0x1a** (`0x462c90`), `+0x38` = P[0x1a] = **P+0x68 = 650**, `+0x3c` = P[0x1b] = **P+0x6c = 600**, `+0x40` = P[0x19] = **H = P+0x64 = 380
(spronghoogte)**, `+0x44` = **P+0x70 = 2000 (eindvalsnelheid)** (`0x462ce0`), `+0x48` coyote-timer, `+0x4c` "sprong herbewapend",
`+0x4d` val zonder sprong, `+0x4e` lange val (> P+0x7c), `+0x4f` sprong afgekapt, `+0x50` coyote actief.

```c
void J_StartJump(J *j) { j->S = j->p38 * 0.75f;              /* 0x462d10; 0x4aabb4 = 0.75 → 487.5 */
                         j->t = -(j->S / j->p3c) * 0.5f;     /* 0x4a94bc = -0.5 → t = -0.40625 s */
                         j->h = 0; j->vfall = 0; j->cut = 0; j->noJump = 0; }
void J_StartFall(J *j, bool noJump) { j->S = j->p38 * 1.25f; /* 0x462d40; 0x4ab798 = 1.25 → 812.5 */
                         j->t = 0; j->h = j->H; j->noJump = noJump; }

bool J_Tick(J *j, float dt)                                  /* 0x462fd0 */
{
    j->t += dt;
    float T = (j->S / j->p3c) * 0.5f;                        /* stijgen 0.40625 s; vallen 0.67708 s */
    float u = j->t / T;
    float h = j->H - u*u*j->H;                               /* parabool, top (H) bij t = 0 */
    if (j->t >= -0.15f && j->phase == 1 && !j->cut) {        /* 0x4ab7a0 = -0.15 */
        0x457560(perso, 0.5f, 0); 0x465e00(perso, 0.5f, 0);  /* anim-overgang naar de top */
        j->phase = 7; }
    if (j->t >= 0 && (j->phase == 7 || (j->phase == 1 && j->cut))) {
        j->phase = j->cut ? (OnGround(perso) ? 6 : 4) : 3;
        J_StartFall(j, 0); h = j->H; }                       /* 0x463087 */
    if (u > 1.0f) {                                          /* voorbij de parabool: naar eindsnelheid */
        float k = min(u - 1.0f, 0.5f);
        j->disp.y = -((j->vmax /*2000*/ - j->vfall) * 2*k + j->vfall) * dt;   /* 0x4630c7 */
    } else { j->disp.y = h - j->h; j->vfall = -(h - j->h) / dt; }            /* 0x4630e1 */
    j->h = h;
    return !(u < 0.45f && !j->cut);                          /* 0x4ab79c = 0.45 */
}

void Jumper_Update(J *j, bool pressed, bool input)           /* 0x462d70, tabel 0x462fa4 */
{
    if (j->phase == 2) { j->disp = 0; j->fallen = 0; j->longFall = 0; J_LoadParams(j); }
    if (!input) pressed = false;
    if (perso->heightAboveGround /*+0x228*/ <= P[0x84] /*100*/ && !pressed && input) j->armed = 1;
    if (j->coyote) { j->tCoyote += dt; if (j->tCoyote > 0.15f) j->coyote = 0; }   /* 0x4aa1c8 */
    switch (j->phase) {
    case 2: /* op de grond  0x462dff */
        if (pressed && OnGround() && j->armed) { jump: j->phase = 0; J_StartJump(j); J_Tick(j, dt); j->armed = 0; return; }
        if (OnGround()) return;
        j->coyote = j->armed; j->tCoyote = 0;               /* van een rand gelopen */
        j->phase = 3; J_StartFall(j, 1); J_Tick(j, dt); j->armed = 0; return;
    case 0: j->phase = 1;                                    /* 0x462e8e, valt door */
    case 1: /* stijgen  0x462e95 */
        if (!pressed && j->t < -0.2f) {                      /* 0x4aa430: toets losgelaten → korte sprong */
            j->cut = 1; j->t = -0.2f;
            float u = -0.2f / ((j->S / j->p3c) * 0.5f); j->h = j->H - u*u*j->H; }  /* geen positiesprong */
        J_Tick(j, dt); return;
    case 7: /* top  0x462eee */  b = J_Tick(j, dt); if (OnGround() && b) j->phase = 6; return;
    case 3: /* begin val  0x462f14 */
        if (J_Tick(j, dt)) j->phase = 4;
        if (j->coyote && pressed) { perso->anim494->vt[4](); j->coyote = 0; goto jump; }   /* coyote-sprong ≤ 0.15 s */
        return;
    case 4: if (j->fallen > P[0x7c] /*1500*/) { j->longFall = 1; j->phase = 5; }            /* 0x462f52 */
    case 5: b = J_Tick(j, dt); if (OnGround() && b) j->phase = 6; return;                  /* 0x462f6a */
    case 6: j->phase = 2; return;                            /* geland (0x44b220 leest fase 6 → valschade) */
    }
}
```

Getallen voor kolom 0/1/2 (Woody e.a.; kolom 3/4: H = 400, p38 = 1250, p3c = 1250):

| grootheid | formule | waarde |
|---|---|---|
| spronghoogte (toets vastgehouden) | H = P+0x64 | **380** eenheden |
| tijd tot de top | 0.5·0.75·650/600 | **0.40625 s** |
| equivalente beginsnelheid / g bij stijgen | 2H/T ; 2H/T² | 1870.8 eenh./s ; **4605 eenh./s²** |
| korte sprong (toets los vóór t = −0.2 s) | t springt naar −0.2 s; resterende stijging H·(0.2/T)² | +92.1 eenheden boven het loslaatpunt |
| val-parabool | T = 0.5·1.25·650/600 = **0.67708 s**, h = H(1−(t/T)²) | g = **1657.8 eenh./s²** tot 380 gevallen (v = 1122.5) |
| daarna | v = v_laatst + (2000 − v_laatst)·2·min(t/T − 1, 0.5) | lineair naar **2000 eenh./s** in 0.3385 s, dan constant |
| coyote-tijd | `0x4aa1c8` | 0.15 s |
| herbewapenen van de sprong | toets los én hoogte boven grond ≤ P+0x84 | 100 |
| valschade | J+0x28 (gevallen hoogte) ≥ P+0x7c bij landen (`0x44b220`) | 1500 → −1 hartje |

Omdat de verticale verplaatsing een hoogteverschil van een vaste curve is (niet een geïntegreerde snelheid), is de sprong
framerate-onafhankelijk. `0x463170(J, force)`: forceer vallen (fase 4, vfall 0) tenzij al in fase 3/4/5 en `!force`.

### 4.3 Animaties per toestand (`0x463e60`, tabel `0x463f14` op toestand−1; anim starten = `P+0x494->vt[2](id)`, tick = `vt[3](dt)`)

| toestand | functie | animaties |
|---|---|---|
| 0 (en 9) | `0x464630` – alleen als `+0x5b4 == 0` en `+0x694 == 0` (geen aanval, niet gebukt) | J-fase 2 (grond) → `0x463f40(0)`; anders `0x4642f0(0)` |
| 1 | `0x464c20` | lucht-set 0x6b/0x6c/0x6d/0x6e/0x6f (`0x464302`) |
| 2 (dood) | `+0x4d8` ? `0x464a00` : `0x464790` | |
| 3 (rondkijken) | inline `0x463e77` | anim 0 |
| 4, 5, 8 | – (toestand regelt zelf de animatie) | |
| 6 | `0x4646b0` | grond 0x41/0x42, lucht-set 0x47/0x4a/0x49/0x4c/0x4b (`0x464333`) |
| 7 | geen anim en geen controller-tick (`0x463f11`) | |

Als cameramodus `cam+0x138 == 2`: altijd anim 1 (`0x463ec8`).

**Grond (`0x463f40`)**, op Mover-fase `M+0xc` (tabel `0x4642d4`): fase 1 (versnellen) → **anim 2** (start lopen); fase 2 (op snelheid) →
**anim 3 = loopcyclus** met duur `len3 / max(0.5, clamp(M+0x44 / M+0x48, 0, 1))` (`0x436c20`; `len3 = P+0x49c`): de cyclus loopt dus
op halve snelheid bij ≤ 50 % van de max. snelheid. Voetstap-effect `0x47cba0(pos, normaal, richting, links/rechts, grondsoort 2|3)` wanneer de
cyclusfractie 0.38 (`0x4ab278`) resp. 0.9 (`0x4a94b8`) passeert. Fase 3..6 → animatie blijft; fase 0 → **idle** `0x464500`:
timer `+0x230 += dt`; < 10 s (`0x4a9750`) → **anim 0**; bij 10 s willekeurig (`0x43ff20(0,7)` = `rand() % 7`, dus **1 op 7**) **0x5a** (.ins 89, tot `T ≥ 2·L(0x5a) + 10`, dan reset), anders **anim 0x59** (.ins 0 → 88 → 87 = slapen); zodra `.ins`-anim 87 (`P+0xb0 == 0x57`) speelt en `T > 10.5` (`0x4ab7c8`) en `P+0x52c == 0`: `P+0x52c = 1` en zzz-ballon `0x478980(P, 4, 2.5, 130, 50, &P+0x52c)` (leeft tot de reset); reset
(`0x464620`) zodra een actietoets ingedrukt is (`0x44cc30`).

**Lucht (`0x4642f0`)**, op J-fase (tabel `0x4644dc`): 0/1 stijgen → **anim 4**; 7 top → **anim 5**; 3 begin val → anim **7** als zonder sprong
van een rand gevallen (`J+0x4d`), **6** als de sprong afgekapt is (`J+0x4f`), anders blijft 5; 4 val → **anim 8** zodra onGround (landing, tenzij Mover-fase 2),
anders als fase 3; 5 lange val → **anim 9**, bij de grond **anim 10 (harde landing)**: beweging geblokkeerd voor de animatieduur
(`0x44cce0(len10, 0)`, `+0x524 = len10`, camera-schok `0x478980(p, 1, 2.0, 180.0, 50.0, 0)`); 6 → stof-effect `0x476140` als grondsoort `+0x308 == 2`.

### 4.4 Schade, dood, reset (vtable-methoden)

**`0x44ca00` vt[39] `Hit(aanvaller, schade, &richting, &pos, soort)` → bool "dood"** (ret 0x14). Genegeerd als dezelfde aanvaller al
geregistreerd staat (`+0x5f0 == aanvaller ≠ 0`), als `+0x270 > 0` (onkwetsbaar na respawn: Reset zet 1.0 s), als `+0x280 > 0`
(onkwetsbaar na treffer) of cheat `[0x5d7b8c]`. Anders: effect `0x40c2d0(schade, pos)` als pos ≠ 0; force-feedback
`0x44d1b0(P+0xbc, P+0xb8)` (**`0x44d1b0` is joystick-rumble `[0x5e618c]->vt[2]`, geen knockback** – zie §7); `0x463170(J, 0)` (gaan vallen);
**knockback** `0x45a140(M, richting)`: als duwtimer `M+0xec ≤ 0`: RampC.doel = RampC.max (**500**), versnellen (0.1 s), RampC.dir = richting
(of (0,1,0) als |richting| < 0.01), `M+0xec = P+0x58 =` **0.2 s**, daarna uitlopen in 0.5 s (`0x45acb0`); kijkrichting `M+0x10`, `M+0x1c` en
RampA.dir = normalize(−richting.xz) (speler kijkt naar de aanvaller; (1,0,0) als de lengte < 0.01); hit-animatie `0x464b70`; `+0x280 = max(+0x280, P+0x5c =` **0.6 s**`)`
(`0x44cd30`); `+0x238 = 0` (`0x44cce0(0, 1)`); toestand 4 → 0; `+0x5f0 = +0x5b4 = 0`, `+0x550 = 0`; `health(+0x24c) −= schade`; ≤ 0 ⇒ 0 en return 1
(de aanroeper doet dan `vt[38](3)`).

**`0x44c110` vt[38] `Kill(soort)`** (tabel `0x44c498`; subtypes 4/5 → eigen variant `0x44c4c0`; het in de opdracht genoemde `0x44c4ba` ligt
midden in die jump-table en is geen functie). Genegeerd in cinematic (`0x44f2e0/0x44f2d0`), bij `App+0xcc`, als een actor categorie 2 /
subtype 12 met `vt[36]()` actief is, en als `+0x26c ≠ 0` (al dood; uitzonderingen `0x44c1e6`: soort 7 mag elke andere dood dan 7 overschrijven, soort 1 elke andere dan 1). `+0x288` (duur tot de fade, gebruikt door `0x4459c0`) standaard **3.5 s**.

| soort | code | effect |
|---|---|---|
| 1 | `0x44c297` | camera-schok `0x478980(p,0,2.5,180,50,0)`, `+0x240 = len(anim 0x2f)` (geen val tijdens de animatie), `0x463170(J,0)` |
| 2, 9 (bliksem) | `0x44c3ab` | rumble, `+0x288 = 1.5`; cheat `[0x5d7b8a]` of `+0x270 > 0` ⇒ genegeerd; effect `0x477e40(pos + (0, P+0xc=193, 0))`, `+0x240 = len(anim 0x30)` |
| 3 (health op), 8 (valschade) | `0x44c230` | cheat `[0x5d7b8b]` of `+0x270 > 0` ⇒ genegeerd; `+0x288 = 3.0`; `0x463170(J,0)` |
| 4, 5 | `0x44c26f` | `+0x270 > 0` ⇒ genegeerd; `0x463170(J,0)` |
| 6 (explosie `0x44d040`) | `0x44c2d6` | idem, `+0x288 = 2.5` |
| 7 | `0x44c308` | `+0x288 = 0` (directe fade); effect `0x478660(instpos + (0,110,0), snelheid `0x44d170`, 50.0)`, camera `0x459030`, aanval/J/colliders gereset (`0x462c90`, `0x436d10`×2) – "verdwijnen" (water/afgrond, door script/volume) |

Daarna altijd (`0x44c443`): `health = 0`, `+0x274 = 0`, `+0x26c = soort`, `+0x4d8 = 0`, `+0x550 = 0`, **toestand := 2**, `+0x268 = 1`.
In toestand 2 draait `0x44bb20(p, 0)` door: geen invoer, maar J laat de speler nog vallen (behalve tijdens `+0x240`), en `0x4624f0` past de
wandcorrectie niet meer toe (§6.1).

**`0x44ab20` vt[17] Reset**: schaal 1, `+0x2e8 = 1`, up `+0x210 = (0,1,0)`, `0x459ff0(M, &+0x324)` (kijkrichting = startrichting), `0x462c90(J)`
(fase 2, `armed = 1`), animcontrollers `vt[4]()`, `onGround = 1`, `+0x228 = 0`, **`+0x270 = 1.0`** (1 s onkwetsbaar), `+0x280 = +0x238 = +0x240 = 0`,
`health = 3.0` alleen als health ≤ 0, toestand := 0 (subtype 4/5: `0x456150` + toestand 1), colliders `0x436d10`×2, alle sub-toestanden 0,
`+0x100 = 100.0` (let op: **`+0x100`**, niet `+0x110`; `+0x110` = P+0 = 43), `0x44e7f0`, `0x44db10`.

**`0x44cf50` vt[26]** = render-kleur: zolang timer `+0x704` loopt knippert de speler wit (`[0x5ac850] = 2`, kleur 255,255,255; periode 0.5 + 0.1 s als
`+0x704 < 5.0`) – geen invloed op beweging.

## 5. Toestandsmachine (`P+0x21c`)

Dispatch in de per-frame update `0x44b530` via jump-table `0x44b950`:

| toestand | handler | opmerking |
|---|---|---|
| 0 | `0x44bb20(1)` + `0x4624f0` | normaal (lopen/staan) |
| 1 | `0x456210` + `0x4624f0`, daarna `0x4567f0` | (hangen/klimmen? – `0x44bd30` gebruikt in toestand 1 de vloernormaal) |
| 2, 3 | `0x44bb20(0)` + `0x4624f0` | zonder invoer (dood 0x44c110 zet toestand 2) |
| 4 | `0x4651d0` (+0x35c = 0, +0x382 = 0) | leest controller |
| 5 | `0x44db50` | respawn/levelovergang? (`0x44a650` weigert teleport in 5) |
| 6 | `0x44bb20(1)` + `0x4624f0` | variant van 0 met `+0x590`-object (`0x44c980`) |
| 7 | `0x44e1c0` | |
| 8 | `0x4657f0` | `0x44bd30` slaat de rotatie-update over |
| 9 | `0x454090` | |

Vóór de dispatch draaien elk frame (tenzij `+0x690` gezet): `0x464ef0`, `0x465e50`, `0x457a50` (1192 instr, aanvallen), `0x44ba70`, `0x465b10`, `0x44b980`, `0x458bf0`; daarna `0x459c70` als `+0x4ec`, `0x45b0a0` (stuurobject), na de dispatch `0x44bcf0`-check → bericht 0x200 naar het script (`0x443e50`/`0x443e90`), `0x44b2e0`, `0x44bd00` (matrix + positie), `0x463e60`.

## 6. Botsing met de wereld

Aanvulling/correctie op PERSO_FRAME §2.4. Alles hieronder zit in `0x4624f0` (Perso_MoveCollide) en wat het aanroept.
Globale resultaten van de botsroutines: `[0x4c4bd0]` ruw resultaat (1 = niets, 3 = wereldpolygoon, 4 = instantie), `[0x4c4bd4]` afstand,
`[0x4c4bc0..cc]` vlak, `[0x4c4bd8]` polygoonindex, `[0x4c4bdc]`/`[0x4c4be0]` instantie-index/hull-node; vertaald door de wrappers naar
`[0x53a554]` (0 = niets, 1 = wereld, 2 = instantie, 3 = "wand geraakt" uit `0x437180`), `[0x53a558]` t/afstand, `[0x53a560]` instantie*,
`[0x53a568]` **grondhoogte**, `[0x53a58c]` node, `[0x4b3108..14]` vlak (normaal + d) van de grond, `[0x4b3118]` **botsstraal**.

### 6.1 Volgorde in `0x4624f0`

```c
void Perso_MoveCollide(Perso *p)
{
    p->H = p->duck694 ? P[0x10] /*61*/ : P[0x0c] /*193*/;          /* 0x462490 → P+0x08 (+0x118) = lichaamshoogte */
    PushFromActors(p);                                             /* 0x4627d0: PERSO_FRAME §2.4 (cirkels, r_ander − 5.0) */
    g_radius = (p->state == 1 && p->duck694) ? P[4]*0.5f : P[4];   /* 0x434820; P+4 = 69 */
    p->oldPos = p->pos;                                            /* +0x28c */
    vec3 np = p->pos + p->disp;
    vec3 carry = AttachDelta(&p->att298);                          /* 0x436d20: beweging van het platform waarop je stond:
                                                                      wereld(lokaal punt in instantie/node) − vorige wereldpositie */
    np += carry;
    vec3 c2 = WallAttach(&p->att2bc, &np);                         /* 0x437040: DODE CODE – de test 0x435b60 is een stub
                                                                      ([0x53a554]=0), dus c2 = 0 en att2bc wordt gewist */
    if (p->state != 2) { np.x += c2.x; np.z += c2.z; }
    SweepCylinder(&p->pos, &p->oldPos, np, p->H * p->scaleZ,       /* 0x437180(…, 40.0, 10.0, P[0]); §6.2 */
                  /*stap*/ 40.0f, /*substap*/ 10.0f);
    p->wall2e0 = g_hitType;  p->wallInst2e4 = g_hitInst;           /* 0 / 3 (wereldwand) / 2 als de wand een instantie is */
    /* grond: */
    vec3 probe = p->pos + (0, P[0] /*43*/, 0);
    bool hit = FloorAttach(&p->att298, -1, &probe, P[0], p->id);   /* 0x436f00 → 0x435650 (GetHeight, §6.3) */
    p->groundY = g_groundY;  p->heightAbove = p->pos.y - g_groundY;   /* +0x224, +0x228 */
    if (hit) { p->onGround = 1; p->pos.y = g_groundY; } else p->onGround = 0;
        /* hit ⇔ probe.y − (groundY + 43) < 1.0  ⇔  pos.y − groundY < 1.0  (ook als de grond tot 43 HOGER ligt: opstap) */
    GroundInfo(p);                                                 /* 0x4628e0, §6.4 */
    VolumeTest(p, 71.0f);                                          /* 0x462760 → 0x4347b0: trigger-volumes op pos + (0,71,0) (EVENTS.md) */
}
```

`0x436f00` doet daarnaast: staat de speler op een **instantie** (`[0x53a554] == 2`) dan `0x436d80(att, inst, node, &probe)`: lokaal punt
onthouden (`0x431700` wereld→node) zodat `0x436d20` het volgende frame de platformbeweging kan doorgeven; heeft de hull-polygoon
vlag `(flags & 0xff00) == 0x100` dan collision-id = `inst+0x70[(flags >> 16) + model+0x48]` → script-events **PersoPress `0x441fc0`** (nieuw),
**PersoIn `0x442000`** (zelfde als vorig frame, `att+0x20`), **PersoUnpress `0x442040`** (losgelaten). Niet op een instantie ⇒ `att` gewist (`0x436d10`).

### 6.2 `0x437180` – verplaatsen in substappen met cilinder-uitduwen

```c
void SweepCylinder(vec3 *out, vec3 *old, vec3 target, float H, float step /*40*/, float sub /*10*/)
{
    float half = H * 0.5f;                                   /* 96.5 staand, 30.5 gebukt */
    vec3 cur = *old + (0, half, 0);                          /* lichaamsmidden */
    float dy = target.y - old->y;
    int mode = dy > 0.1f ? 3 /*stijgt*/ : dy < -0.1f ? 2 /*daalt*/ : 0;      /* 0x4a9008, 0x4aa3d0 */
    vec3 d = (target + (0,half,0)) - cur;
    int n = (int)(floor(|d| / sub) + 1 + 0.5);  d /= n;      /* substappen van max. 10 eenheden (0x499ede/0x499580) */
    float margin = 5.0f;                                     /* onderkant van de wandtest boven de voeten, in de lucht */
    GetHeight(&cur);                                         /* 0x435650 */
    if (cur.y - half - 1.0f < g_groundY) { margin = step + 1.0f;  /* 41: op de grond worden de onderste 41 eenheden NIET
                                                                     tegen wanden getest ⇒ **opstaphoogte ≈ 40** */
                                           if (mode == 0) mode = 1; /* loopt over de grond */ }
    g_hitType = 0; g_hitInst = 0;
    for (; n > 0; n--) {
        vec3 prev = cur;  cur += d;
        float feet = cur.y - half;
        g_bandLo = feet + margin;  g_bandHi = feet + H;      /* [0x53a54c], [0x53a350] */
        CylinderVsWorld(&cur, g_radius, /*up*/ g_bandHi - cur.y, /*down*/ cur.y - g_bandLo, -1);   /* 0x407000, §6.5 */
        if (g_raw /*[0x4c4bd0]*/ != 0) { cur.x += g_push.x * 0.9f;  cur.z += g_push.z * 0.9f;       /* 0x4a94b8 = 0.9 */
                                         g_hitType = 3; g_hitInst = [0x53a560]; }
        GetHeight(&cur);  float gy = g_groundY;
        if (mode == 2) { if (cur.y - half < gy) cur.y = gy + half; }                 /* landen: niet door de vloer */
        else if (mode == 1) { float f = cur.y - half;
                              if (f - gy < step /*40*/ && f > gy) cur.y = gy + half; }   /* **aan de grond kleven** bij afdalen (≤ 40 per substap) */
        if (step - half + d.y > 0) { /* alleen mogelijk als gebukt (half < 40): straal 0x4359b0 van prev naar (cur.x, voeten+40, cur.z);
                                        bij een treffer wordt cur.y op het trefpunt gezet maar nooit onder gy + half (0x437464..0x43753d) */ }
        *out = (cur.x, cur.y - half, cur.z);
    }
    [0x53a554] = g_hitType; [0x53a560] = g_hitInst;          /* return 0 → P+0x200 = 0 */
}
```

Gevolgen voor een herimplementatie:
* **Geen iteratieve "slide"**: per substap (≤ 10 eenh.) één uitduwvector in xz, ×0.9; het glijden langs muren ontstaat doordat alleen de
  component langs de polygoonnormaal wordt teruggeduwd. Bij 600 eenh./s en 60 Hz zijn dat 1 à 2 substappen per frame.
* **Spelerafmetingen (kolom 0)**: straal **69**, hoogte **193** (gebukt **61**), wandtest van voeten+41 (grond) of voeten+5 (lucht) tot voeten+H.
  De verticale verplaatsing wordt nooit door wanden/plafonds geblokkeerd (alleen de vloer via GetHeight); er is geen plafondbotsing
  behalve de verplettertest §6.6.
* Opstappen tot 40–43 eenheden gebeurt "gratis": de wandtest negeert de onderste 41 en `0x436f00` zet `pos.y = grond` zodra
  `pos.y − grond < 1` (de peiling start 43 boven de voeten, dus hogere vloeren tot +43 worden gevonden).
* Afdalen: kleven tot 40 per substap; is de vloer verder weg dan 1.0 na de sweep ⇒ `onGround = 0` ⇒ J gaat naar fase 3 (val, §4.2).
* Tegen een wand (`P+0x2e0 ≠ 0`): `0x4672d0(RampA, 0.25)` – alleen in de uitloopfase springt de fasetimer per frame 25 % naar T_dec (sneller stilstaan).

### 6.3 Vloer zoeken: `0x435650` GetHeight → `0x498440` → `0x498520`

```c
void GetHeight(vec3 *p)                                   /* 0x435650(p, cel=-1, 1) */
{
    g_raw = 1;  float best = +inf;
    int cell = FindCell(p);                               /* 0x408180 (kd-boom, FORMAT_GEL.md) */
    for (;;) {                                            /* 0x498580 */
        celllist[ncell++] = cell;   Cell *c = world->cells[cell];
        float maxd = p->y - c->ymin;                      /* cel+0x30 */
        for (poly in c->polys)  if (poly->stamp != g_stamp) {         /* cel+8 / +0xc → wereld+0x10[] */
            if (poly->n.y <= 1e-5f) continue;             /* 0x4aa398: alleen vlakken met normaal omhoog (elke helling!) */
            float dist = dot(poly->n, *p) + poly->d;      /* poly+0xc..0x18 */
            if (dist <= 0) continue;                      /* punt moet boven het vlak liggen */
            if (poly->n.y * maxd + 0.001f < dist) continue;            /* 0x4a94c4: verder dan de huidige beste/celbodem */
            if (!PointInPolyXZ(poly, p)) continue;        /* alle randen: kruisproduct in xz ≥ 0 (0x498666) */
            maxd = dist / poly->n.y;  g_dist = maxd;  g_poly = index;  g_plane = poly->plane;  g_raw = 3;
        }
        if (g_poly != -1) break;
        int link = c->down;                               /* cel+0x18 */
        if (link >= 0)               cell = KdDescend(&c->nodes[link], p);   /* 0x40ab60(cel+0x48 + link*16, p) */
        else if (link != 0x80000000) cell = -1 - link;
        else { g_raw = 1; break; }                        /* geen cel meer onder ons */
    }
    /* daarna (0x498475): alle instanties in de bezochte cellen (cel+0x40/+0x44, id & 0xffff → wereld+0x40[]) en alle dynamische
       instanties 0x4c3bb4[0x4c4bec]: inst->vt[7](p, id) = 0x432480 (hull-vloertest; zet g_raw = 4 als dichterbij) */
    if (g_raw == 1) { g_groundY = p->y; g_type = 0; g_plane = (0,1,0,0); }   /* log 'GetHeight return : NotFound !!!!!!' */
    if (g_raw == 3) { g_type = 1; g_groundY = p->y - g_dist; }
    if (g_raw == 4) { g_type = 2; g_groundY = p->y - g_dist; g_hitInst = world->inst[g_instIdx]; g_node = [0x4c4be0];
                      g_local = WorldToNode(inst, node, p - (0,g_dist,0)); /* 0x431700 → [0x53a57c..84] */ }
}
```

De in de opdracht genoemde `0x40a0c0`/`0x407790`/`0x4077f0` worden door de **spelerbotsing niet gebruikt**: `0x4077f0(&midden)` wordt alleen
in `0x44bf10` aangeroepen om de instantie van de speler in de juiste wereldcel te hangen (voor rendering/zichtbaarheid en de cel-lijsten),
`0x407790` door andere klassen en de lader. De vloer van de speler komt uitsluitend uit `0x498520` (straal omlaag vanaf voeten+43).

### 6.4 Grondinformatie `0x4628e0`

* Grondnormaal → Mover: `0x45a110(M, n)` met n = `[0x4b3108..10]` als `n.y ≥ 0`, anders (0,1,0) → `M+0xd0` (glijden als `n.y < 0.71`, PERSO_FRAME §2.3:
  hellingen steiler dan ≈ 45° laten de speler met 600 eenh./s naar beneden glijden).
* **Grondsoort `P+0x308`** = 0, behalve op een wereldpolygoon (`[0x53a554] == 1`) waarvan `poly+8` (textuurindex) bit 15 niet gezet is:
  `P+0x308 = byte(level->tex[poly+8]->+0x47)` (`level+0x5c`, records van 0x24 B, `+0x20` = textuurobject) – dit is het "grondtype"-byte uit het .tex-bestand.
  Gebruik: **1 = glad/ijs** (`0x45a850`: de loop­richting draait traag bij: RampA-tijden 0.75 s / 1.0 s i.p.v. 0.25 / 0.1 en
  `dir = lerp(dir, kijkrichting, clamp(v/600, 0, 0.95)·1.0)`), **2 = stof/zand/sneeuw** (voetstap-effect soort 3 i.p.v. 2 `0x464231`, stofwolk bij landen `0x464486`).
  Er is **geen dodelijk grondtype** in de Perso-code: dood door water/afgrond komt van scripts (volume → bericht → `Kill(1)` `0x44516a`) of van `Kill(7)` (`0x4747f0`).

### 6.5 `0x407000` cilinder tegen wereld + instanties (uitduwvector)

1. `0x40aa30(c, r, up, down, cel)` verzamelt de cellen die de cilinder raakt in `0x4c4be8[0x4c4be4]`.
2. Per cel, per wereldpolygoon (één keer per aanroep via stempel `poly+4`): **`0x408600(poly; c, r, up, down, &pos, &neg)`**:
   `dist = n·c + d`; verwerpen als `dist < 0` (achterkant) of als het interval `[dist − down·n.y, dist + up·n.y]` geheel buiten `[−r, r]` ligt;
   de polygoon wordt geclipt op de hoogteband `−down ≤ y ≤ up` (14-weg jump-table `0x409a94`); voor elke geclipte rand wordt in xz de
   doorsnede met de cirkel opgelost (kwadratische vergelijking, `0x4a94c0` = 4.0), het midden van het snij-interval (geklemd op [0,1]) is het
   dichtste punt; **onder het midden (y < 0) loopt de straal lineair af: r(y) = r·(down + y)/down** (de "cilinder" heeft een kegelvormige
   onderkant, `0x4096c7`/`0x4098b9`); indringdiepte `pen = r(y) − afstand_xz`; bijdrage `pen·(n.x, n.z)`, per as gesplitst in een
   positieve (max) en negatieve (min) accumulator. Ligt het middelpunt binnen de xz-projectie zonder randdoorsnede ⇒ treffer met vector 0.
3. Per cel de statische instanties (`cel+0x40/+0x44`) en daarna alle dynamische (`0x4c3bb4[]`, id `| 0xffff0000`):
   `inst->vt[8](c, r, up, down, &pos, &neg, id)` = **`0x433140`**: overgeslagen als de instantie geen cel heeft (`+0x1c == −1`), al getest is,
   **vlag `+8 & 0x40` gezet** (niet-botsbaar), het model geen hull heeft (`model+0x58 == 0`) of `inst+0xd0 & id & 0xffff0000 == 0` (botsmasker);
   skelet zo nodig bijgewerkt (`vt[2](1)`); per hull-node wordt het middelpunt naar node-ruimte getransformeerd (uniforme schaal: r/schaal) en
   dezelfde polygoontest `0x435b90` gebruikt; resultaat type 4 met `[0x4c4bdc]` = instantie, `[0x4c4be0]` = node.
4. Resultaat: `push.x = max⁺.x + min⁻.x`, `push.z = max⁺.z + min⁻.z`, `push.y = 0` → `[0x4c4bb4..bc]`.

### 6.6 Overig

* **Actoren** (`0x4627d0`): cirkel-cirkel in xz tegen de lijst van vorig frame `0x4c5258[0x4c5324]` (`0x433d40`), vóór de sweep bij `disp` opgeteld.
* **Verpletteren** (`0x462a40`, na de dispatch): straal `0x4359b0` van voeten+1 omhoog tot voeten+H−1; treffer (t < 1) terwijl de speler op de grond
  staat en óf de rakende instantie animeert (`inst+0xa0 ≠ 0`) óf de speler op een platform staat (`att298 ≠ 0`) ⇒ `P+0x2e8` (z-schaal van het model)
  `= clamp(max(vrije hoogte, 2.0) / H, …, 1)`; **< 0.3 (`0x4aab98`) ⇒ `Kill(4)`**.
* **Steile rand** (`0x44b2e0`) en **valschade** (`0x44b220`): zie PERSO_FRAME §2.2.
* **"Uit de wereld gevallen"**: bestaat niet als aparte test. Vindt GetHeight geen vloer (`g_raw == 1`) dan geldt `grondhoogte = peilpunt.y`
  (= voeten+43): `0x436f00` meldt dan `onGround` en zet `pos.y += 43` (!), en in de sweep geldt "op de grond". In de praktijk hebben levels
  overal een vloer of een script-volume dat `Kill` stuurt; het gedrag boven een echt gat is niet in het spel geverifieerd (§7).
* `0x462990` (teleport/respawn-hulp): zet de speler op de vloer onder `pos + 43`, `onGround = 1`, J gereset, cel opnieuw bepaald (`0x428ce0`).
  Ook de **levelstart** eindigt ermee: Game-ctor `0x445850` → `0x445930` → `0x44a810(0)` → `0x44a6a0` (Reset, `pos = +0x30c`,
  health/levens uit de save, `0x44a7ee`: grond-snap), daarna `0x44a902` en nog een directe `0x44a6a0` (`0x4458e2`). De `.ins`-positie
  van de speler zweeft een paar eenheden boven de vloer; zonder deze snap begint elk level met een val (issue #11). Respawn
  (`0x445b41` → `0x44a650`), einde resultatenscherm (`0x45423f`) en einde cinematic (`0x445af9` → `0x44a650`) snappen ook.

## 7. Open vragen en tegenstrijdigheden

### 7.1 Tegenstrijdigheden met PERSO_FRAME.md (hier is de code opnieuw gelezen; PERSO_MOVE is leidend)

1. **Zwaartekracht**: PERSO_FRAME §2.4/§5 zegt "geen zwaartekracht gevonden, vallen gebeurt via de sprongcontroller `+0x5bc`". Onjuist: springen én vallen
   zitten in het object `P+0x334` (`0x462d70`/`0x462fd0`, §4.2), dat PERSO_FRAME "duw/impuls-object" noemt. `0x457a50` levert alleen de verplaatsing
   tijdens aanvallen/speciale sprongen (`+0x5cd`).
2. `0x44bb20`: PERSO_FRAME schrijft "duw uit +0x334 als `+0x240 > 0`, anders `+0x240 −= dt`" – precies omgekeerd (`0x44bbca`: `> 0` ⇒ aftellen, anders J updaten).
   En de invoer wordt uitgeschakeld als `+0x474 > 0` **of** `+0x238 > 0` **of** `+0x5b4 ≠ 0` (niet "en").
3. `0x44d1b0` is **joystick-force-feedback** (`[0x5e618c]->vt[2](a, b)`), geen knockback; de echte knockback is `0x45a140` (RampC, 500 eenh./s, 0.2 s).
4. `P+0x238` is een bewegingsblokkade, geen onkwetsbaarheid; onkwetsbaarheid = `+0x270` (respawn 1.0 s) en `+0x280` (na treffer 0.6 s).
5. `P+0x2e0` is wandcontact (0/3/2) uit `0x437180`, geen grondtype; het grondtype is `P+0x308` (`0x4628e0`). `0x437180` geeft altijd 0 terug (`P+0x200 = 0`).
6. De "colliders" `+0x298`/`+0x2bc` zijn **platform-koppelingen** (instantie, node, lokaal punt, vorige wereldpositie), geen wand/grond-colliders;
   `0x437040` (+0x2bc) is effectief dode code omdat `0x435b60` een stub is.
7. Invoerassen: PERSO_FRAME §2.3 noemt "x = toets 2/3 (links/rechts), y = toets 0/1" – het is andersom: acties 0/1 = links/rechts, 2/3 = vooruit/achteruit (§3.4; `hoek = π − atan2(waarde01, waarde23)`).
8. Kolomtoewijzing van tabel `0x4b5f14`: de oude tekst in §1 van dit document was fout; PERSO_FRAME §2.6 klopt (subtype 1→0, 3→1, 2→2, 5→3, 4→4).
9. Opdrachttekst: `0x44c4ba` is geen functie (midden in jump-table `0x44c498`; de volgende functie is `0x44c4c0` = Kill-variant voor subtype 4/5);
   `0x40a0c0`/`0x407790`/`0x4077f0` horen bij celtoewijzing, niet bij de spelerbotsing (§6.3).

### 7.2 Nog open

* Welk script-type (1, 2 of 3) Woody precies is, is aangenomen (type 1 → kolom 0); kolom 1/2 verschillen alleen in lichaamshoogte (143 i.p.v. 193). Te
  controleren met een .ins-dump van W1A (type van de Perso-instantie).
* Draairichting van `0x440d40` (teken van de rotatie om y bij "rechts"): niet uitgeschreven; in de herimplementatie vastleggen met de regel
  "actie 1 (→) moet op het scherm naar rechts lopen" en controleren met `tools/wtrace.py`.
* De kijkrichting-slerp (`0.25·|stick|` per frame) en het wand-uitduwen (×0.9 per substap) zijn **framerate-afhankelijk**; het origineel draait op
  vsync (60 Hz?) – de referentie-framerate is niet vastgesteld.
* `0x408600`/`0x435b90` (polygoon–cilinder, ±1400 instructies) zijn alleen op hoofdlijnen gelezen (afwijzingstests, clipping, kegel-onderkant,
  uitduwvector); de 14 clip-gevallen zijn niet één voor één nagelopen. Voor de herimplementatie volstaat een eigen cilinder/kegel-polygoontest met dezelfde
  uitvoer (pen·n.xz, per as max⁺ + min⁻).
* Gedrag zonder vloer (GetHeight "NotFound", §6.6) is uit de code afgeleid maar niet in het spel gezien.
* `0x432480` (hull-vloertest, vt[7]) en de niet-uniforme-schaal-tak van `0x433140` (`0x4335d7`) zijn niet gelezen.
* Toestanden 1, 4, 6, 8, 9 (eigen bewegingscode `0x456210`, `0x4651d0`, `0x463530`, `0x4657f0`, `0x454090`) en altMode `0x459c70`/`0x45a7b0` vallen buiten dit
  document; idem de aanvalscontroller `0x457a50` (PERSO_JUMP.md) en de veercorrectie `+0x244` (`0x45848c`).
* `P+0x474` = `M+0xec` (0x388+0xec), de **knockback-timer** van de Mover (0.2 s na een treffer, `0x45a140`): tijdens knockback dus geen invoer. `P+0x750` (blokkeert de speciale aanval): schrijver niet gezocht.
* Betekenis van P+0x28 (1.8), P+0x60 (200), P+0x74 (250), P+0x78 (300), P+0x98..0xb4, P+0xd8/0xdc: niet gebruikt in de hier gelezen functies
  (waarschijnlijk aanvallen/toestand 1/4/6).
