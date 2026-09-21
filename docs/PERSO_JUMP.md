# Perso (speler) – sprong en aanval

Werkdocument, incrementeel bijgewerkt. Alle adressen zijn VA's in `Woody.exe` (imagebase 0x400000).
Notatie: `p` = Perso (zie docs/PERSO_FRAME.md §3), `P` = parameterblok `p+0x110`, `M` = Mover `p+0x388`,
`J` = Jumper `p+0x334`, `dt` = `p+0x2f8`. Float-constanten zijn uit `game/Woody.exe` gelezen
(controle: `0x4a9010` = 100.0, `0x4a9014` = 0.5). Woody = kolom 0 van tabel `0x4b5f14`.

## 0. Samenvatting / correcties op PERSO_FRAME.md

* `0x457a50` is **alleen de aanval-controller** (sub-toestand `p+0x5b4`). **Sprong en zwaartekracht**
  zitten in het ingebedde object `J = p+0x334` (`0x462c70..0x463170`), aangeroepen vanuit
  `Perso_Move 0x44bb20` → `0x462d70`.
* **Springen = actie 4** (`0x44bb10` = `0x467400(4)`, standaard LCtrl), **aanvallen = actie 6**
  (standaard LShift). Actie 5 (spatie) komt in deze code niet voor.
* `0x44d1b0(p, a, b)` is geen knockback maar **joystick force-feedback**
  (`[0x5e618c]->vtbl[2](a, b)`, overgeslagen als toestand 2 = dood).
* `0x44cce0(p, t, force)` = **LockMove**: `p+0x238 = max(p+0x238, t)` (of `= t` als force). Zolang
  `p+0x238 > 0` zet `0x44bb20` de Mover-verplaatsing op 0 (`0x44bc2b`) en is `arg = 0` (geen invoer).
  `+0x238` is dus een "beweging geblokkeerd"-timer (duur van de aanvalsanimatie), geen onkwetsbaarheid.
* `0x44bb20`: `arg = 0` (geen loop-invoer, **en ook geen sprong** want `0x462d70(a, arg)` maakt `a = 0`
  als `arg == 0`) zodra `p+0x474 > 0` **of** `p+0x238 > 0` **of** `p+0x5b4 != 0` (`0x44bb48..0x44bb78`).
* Er is geen aparte zwaartekrachtconstante: de sprong en de val zijn **parabolen in de tijd**
  `h(t) = H·(1 − (t/T)²)` met `H = P+0x64`, `T = ½·D/V`, `D = k·P+0x68`, `V = P+0x6c` (§1).

## 1. Jumper `J = p+0x334` (sprong, val, landing)

### 1.1 Layout (0x54 B; ctor `0x462c50` leeg, init `0x462c70(J, p)`: `J+0 = p`, `J+4 = P`, reset)

| J+ | p+ | type | betekenis |
|---|---|---|---|
| 0x00 | 0x334 | ptr | Perso |
| 0x04 | 0x338 | ptr | P (`p+0x110`) |
| 0x08 | 0x33c | vec3 | **verplaatsing dit frame**; alleen `J+0xc` (y) wordt geschreven; `0x463130` kopieert hem |
| 0x14 | 0x348 | int | **jumper-toestand** 0..7 (getter `0x463160`), zie §1.3 |
| 0x18 | 0x34c | f32 | `D` = horizontale referentieafstand: `0.75·P+0x68` (sprong, `0x4aabb4`) of `1.25·P+0x68` (val, `0x4ab798`) |
| 0x1c | 0x350 | f32 | `t` = tijd op de parabool (s); top bij `t = 0` |
| 0x20 | 0x354 | f32 | `hPrev` = paraboolhoogte van het vorige frame |
| 0x24 | 0x358 | f32 | `vDown` = laatste daalsnelheid (−Δh/dt), startwaarde voor de eindsnelheidsfase |
| 0x28 | 0x35c | f32 | **gevallen hoogte** (= `fallAccum` uit PERSO_FRAME; opgeteld in `0x44b914`) |
| 0x2c/0x30/0x34 | | int | P-indices 0x19, 0x1b, 0x1a (vast, `0x462c90`) |
| 0x38 | 0x36c | f32 | kopie `P[0x1a]` = **P+0x68 = 650** (1250 voor kolom 3/4) |
| 0x3c | 0x370 | f32 | kopie `P[0x1b]` = **P+0x6c = 600** (1250) |
| 0x40 | 0x374 | f32 | kopie `P[0x19]` = **P+0x64 = 380** (400) = **spronghoogte H** |
| 0x44 | 0x378 | f32 | kopie **P+0x70 = 2000** = eindsnelheid bij lange val |
| 0x48 | 0x37c | f32 | coyote-timer |
| 0x4c | 0x380 | u8 | `armed`: sprongknop is losgelaten geweest bij de grond ⇒ mag springen |
| 0x4d | 0x381 | u8 | `fellOff`: val begon zonder sprong (van rand gelopen / geforceerd) – alleen voor animatie |
| 0x4e | 0x382 | u8 | `hardFall`: `J+0x28 > P+0x7c` tijdens val (⇒ harde landing, valschade in `0x44b220`) |
| 0x4f | 0x383 | u8 | `shortHop`: knop vroeg losgelaten |
| 0x50 | 0x384 | u8 | coyote-venster actief |

`0x462c90` Reset: `J+8 = 0`, toestand 2, `armed = 1`, `vDown = 0`, `J+0x28 = 0`, vlaggen 0, indices,
`0x462ce0` (parameters kopiëren), `0x462d10`.
`0x462ce0`: `J+0x38 = P[J+0x34]`, `J+0x3c = P[J+0x30]`, `J+0x40 = P[J+0x2c]`, `J+0x44 = P+0x70`.

### 1.2 Constanten (Woody, kolom 0)

| naam | waarde | bron |
|---|---|---|
| `H` spronghoogte | **380** | P+0x64 |
| `V` | 600 | P+0x6c (= loopsnelheid) |
| `D_jump` | 0.75·650 = **487.5** | `0x462d10`, `0x4aabb4` = 0.75 |
| `T_jump = ½·D_jump/V` | **0.40625 s** (tijd tot de top) | `0x462d2a`: `t0 = (D/V)·(−0.5)` (`0x4a94bc`) |
| beginsnelheid omhoog `2H/T_jump` | **≈ 1870.8 eenh/s** | afgeleid |
| zwaartekracht stijgfase `2H/T_jump²` | **≈ 4605 eenh/s²** | afgeleid |
| `D_fall` | 1.25·650 = **812.5** | `0x462d40`, `0x4ab798` = 1.25 |
| `T_fall = ½·D_fall/V` | **0.67708 s** | |
| zwaartekracht valfase `2H/T_fall²` | **≈ 1657.8 eenh/s²** | afgeleid |
| daalsnelheid bij `u = 1` (380 gevallen) | 2H/T_fall ≈ **1122.5 eenh/s** | afgeleid |
| eindsnelheid | **2000 eenh/s**, lineair bereikt tussen `u = 1` en `u = 1.5` (≈ 0.34 s) | P+0x70, `0x4630a8` |
| short-hop sprongpunt | `t := −0.2 s` (`0x4aa430`) als knop los en `t < −0.2` | `0x462e9d` |
| aanvalsvenster opent | `t ≥ −0.15 s` (`0x4ab7a0`) ⇒ `p+0x5c8 = p+0x6f8 = max(·, 0.5)` | `0x463002` |
| "val voltooid"-drempel | `u ≥ 0.45` (`0x4ab79c`) | `0x4630f4` |
| coyote-tijd | **0.15 s** (`0x4aa1c8`) | `0x462dcf` |
| her-armeren sprong | knop los **en** hoogte boven grond `p+0x228 ≤ P+0x84 (100)` | `0x462da0` |
| valschade / harde landing | gevallen hoogte `> P+0x7c (1500)` | `0x462f52`, `0x44b220` |

Zonder loslaten: stijgen 0.406 s tot 380 hoog, daarna 0.677 s vallen tot beginhoogte (totaal ≈ 1.08 s).
Minimale sprong (knop meteen los): na frame 1 springt `t` naar −0.2 s; resterende stijging =
`H·(0.2/T_jump)² = 380·0.2424 ≈ 92` eenheden + wat al gestegen was.

### 1.3 Toestanden `J+0x14` (tabel `0x462fa4`)

| # | handler | betekenis | overgangen |
|---|---|---|---|
| 0 | `0x462e8e` | sprong net gestart | → 1 (zelfde frame, valt door in handler 1) |
| 1 | `0x462e95` | stijgen | knop los en `t < −0.2` ⇒ short hop (`shortHop = 1`, `t = −0.2`, `hPrev = H(1−(0.2/T)²)`); tick: `t ≥ −0.15` en geen shortHop ⇒ **7** + aanvalsvenster; `t ≥ 0` met shortHop ⇒ 6 (op grond) of 4 |
| 2 | `0x462dff` | **op de grond / inactief** | `jumpHeld && onGround && armed` ⇒ **0** (`0x462d10`, tick, `armed = 0`); anders als niet onGround ⇒ **3** (`coyote = armed`, `J+0x48 = 0`, `0x462d40(1)`: `fellOff = 1`, tick, `armed = 0`) |
| 3 | `0x462f14` | vallen, eerste deel (`u < 0.45`) | tick geeft 1 ⇒ **4**; `coyote && jumpHeld` ⇒ anim-reset `p+0x494->vtbl[4]()`, `coyote = 0`, **sprong (→ 0)** |
| 4 | `0x462f52` | vallen | `J+0x28 > P+0x7c (1500)` ⇒ `hardFall = 1`, **5**; tick; `onGround && tick==1` ⇒ **6** |
| 5 | `0x462f6a` | lange val | tick; `onGround && tick==1` ⇒ **6** |
| 6 | `0x462f95` | geland (1 frame; `0x44b220` en de animatiecode lezen dit) | ⇒ **2** |
| 7 | `0x462eee` | rond de top (na `t ≥ −0.15`) | tick (bij `t ≥ 0` ⇒ 3 met `0x462d40(0)`); `onGround && tick==1` ⇒ 6 |

Let op: toestand 1 en 3 testen `onGround` niet; landen tijdens 3 (`u < 0.45`, < 0.30 s vallen) blijft in 3
tot `u ≥ 0.45`, dan 4 → 6 → 2. In die frames levert de tick gewoon een negatieve dy, de grondtest in
`0x4624f0` zet de speler terug op de grond.

### 1.4 Pseudo-C

```c
/* 0x462d70 – per frame vanuit Perso_Move (0x44bbcc), alleen als p+0x240 <= 0 */
void Jumper_Update(Jumper *J, bool jumpHeld /* 0x467400(4) */, bool inputAllowed /* arg van Perso_Move */)
{
    Perso *p = J->p;  const float *P = J->P;
    if (J->state == 2) { J->disp = (0,0,0); J->fallen = 0; J->hardFall = 0; Jumper_LoadParams(J); /*0x462ce0*/ }
    if (!inputAllowed) jumpHeld = false;
    if (p->heightAboveGround /*+0x228*/ <= P[0x84/4] /*100*/ && !jumpHeld && inputAllowed) J->armed = 1;
    if (J->coyote) { J->coyoteT += p->dt; if (J->coyoteT > 0.15f) J->coyote = 0; }
    switch (J->state) {
    case 2:
        if (jumpHeld && p->onGround && J->armed) {
    jump:   J->state = 0; Jumper_StartJump(J); Jumper_Tick(J, p->dt); J->armed = 0; return;
        }
        if (!p->onGround) {
            J->coyote = J->armed; if (J->armed) J->coyoteT = 0;
            J->state = 3; Jumper_StartFall(J, 1); Jumper_Tick(J, p->dt); J->armed = 0;
        }
        return;
    case 0: J->state = 1;  /* fallthrough */
    case 1:
        if (!jumpHeld && J->t < -0.2f) {                     /* variabele spronghoogte */
            J->shortHop = 1;
            float u = -0.2f / (0.5f * J->D / J->V);
            J->t = -0.2f;  J->hPrev = J->H - J->H*u*u;       /* geen positiesprong: alleen de klok verspringt */
        }
        Jumper_Tick(J, p->dt); return;
    case 7: { bool r = Jumper_Tick(J, p->dt); if (p->onGround && r) J->state = 6; return; }
    case 3:
        if (Jumper_Tick(J, p->dt)) J->state = 4;
        if (J->coyote && jumpHeld) { p->anim->vtbl[4](); J->coyote = 0; goto jump; }
        return;
    case 4:
        if (J->fallen > P[0x7c/4] /*1500*/) { J->hardFall = 1; J->state = 5; }
        /* fallthrough */
    case 5: { bool r = Jumper_Tick(J, p->dt); if (p->onGround && r) J->state = 6; return; }
    case 6: J->state = 2; return;
    }
}

void Jumper_StartJump(Jumper *J)  /* 0x462d10 */
{ J->D = J->P68 * 0.75f; J->t = (J->D / J->V) * -0.5f; J->hPrev = 0; J->vDown = 0; J->shortHop = J->fellOff = 0; }

void Jumper_StartFall(Jumper *J, bool fellOff)  /* 0x462d40 */
{ J->D = J->P68 * 1.25f; J->fellOff = fellOff; J->hPrev = J->H; J->t = 0; }

bool Jumper_Tick(Jumper *J, float dt)  /* 0x462fd0 */
{
    J->t += dt;
    float u = J->t / (0.5f * J->D / J->V);
    float h = J->H - J->H*u*u;
    if (J->t >= -0.15f && J->state == 1 && !J->shortHop) {
        Perso_OpenAttackWindow(J->p, 0.5f, 0);   /* 0x457560: p+0x5c8 = max(p+0x5c8, 0.5) */
        Perso_OpenAirMoveWindow(J->p, 0.5f, 0);  /* 0x465e00: p+0x6f8 = max(p+0x6f8, 0.5) */
        J->state = 7;
    }
    if (J->t >= 0 && (J->state == 7 || (J->state == 1 && J->shortHop))) {
        J->state = J->shortHop ? (J->p->onGround ? 6 : 4) : 3;
        Jumper_StartFall(J, 0);  h = J->H;                 /* u blijft de oude waarde dit frame */
    }
    if (u > 1.0f) {                                        /* dieper dan het beginpunt van de parabool */
        float k = min(u - 1.0f, 0.5f);
        J->disp.y = -(((J->vTerminal /*2000*/ - J->vDown) * 2*k + J->vDown) * dt);
    } else {
        J->disp.y = h - J->hPrev;
        J->vDown  = -J->disp.y / dt;
    }
    J->hPrev = h;
    return !(u < 0.45f && !J->shortHop);
}

void Jumper_ForceFall(Jumper *J, bool force)  /* 0x463170 */
{
    if ((J->state == 3 || J->state == 4 || J->state == 5) && !force) return;
    J->coyote = 0; Jumper_StartFall(J, 1); J->vDown = 0; J->state = 4;
}
```

`Perso_Move 0x44bb20`: `disp = Mover.d + J.disp` tenzij `p+0x5cd` (aanval levert `p+0x5bc`) of `p+0x6ac`;
daarna altijd `disp.y += dt·p+0x244`. Als `p+0x240 > 0`: jumper wordt niet aangeroepen, `p+0x240 -= dt`
(jumper-verplaatsing = 0 ⇒ geen zwaartekracht). **Luchtbesturing**: de Mover draait ongewijzigd door in
de lucht (zelfde RampA, 600 eenh/s, zelfde versnelling); er is geen aparte luchtfactor in de jumper.
Callers van `0x463170` (val forceren): `0x44c110` (TakeHit), `0x44ca9f`, `0x456c00`, `0x4656fd`,
`0x465a4f` en de aanval-controller (§2). Reset `0x462c90`: `0x44ab9b` (Perso-Reset), `0x44c389`,
`0x44c6a7`, `0x4629f5` (snap-to-ground `0x462990`), `0x4649d2`, `0x464b3c`, `0x4650ce`, `0x465a46`.

### 1.5 Tweede luchtactie (niet Woody): `0x465e50` + `0x465fe0`

`0x465e50` (elk frame vóór `0x457a50`): op de grond `p+0x6fd = 0`. Als venster `p+0x6f8 > 0`
(`0x465e30`; geopend door de jumper rond de top en door de aanval-terugslag) **en** actie 4 *net ingedrukt*
(`0x467420(4)`) en nog niet gebruikt (`p+0x6fd == 0`, `p+0x6e4 == 0`) en `p+0x228 > P+0x84 (100)`:
`p+0x6e4 = p+0x6fd = 1`, `p+0x6f4 = 0`, en per subtype (`(p+0x104 & 0x3e0)`):
* subtype 3 (`0x60`): **luchtdash** langs `M.dir`: duur `p+0x6e8 = 0.15 s`, snelheid `p+0x6f0 = 3000`, geluid 0x3a.
* subtype 2 (`0x40`): **dubbele sprong** richting (0,1,0): duur `0.25 s`, snelheid `1200`, geluid 0x3a.
* anders (Woody = subtype 1): `p+0x6e4 = 0` – **Woody heeft geen dubbele sprong / zweven**.

`0x465fe0` (begin van `Perso_Move`): `p+0x6f8 -= dt`; als `p+0x6e4`: `p+0x6ec += dt` (tot `p+0x6e8`, dan
einde), voor subtype 2 wordt de snelheid per 1/60 s (`0x4a9990`) met 0.99 (`0x4ab7d8`) vermenigvuldigd;
`p+0x204 = normalize(p+0x6d8) · stap · p+0x6f0`, en de functie geeft 1 ⇒ `Perso_Move` slaat Mover én
jumper over.

## 2. Aanval-controller `0x457a50` + trigger `0x457330`

### 2.1 Velden

| p+ | type | betekenis |
|---|---|---|
| 0x5b4 | int | **aanval-subtoestand** 0..11 (0 = geen) |
| 0x5b8 | f32 | timer van de subtoestand (s, telt af) |
| 0x5bc..0x5c4 | vec3 | verplaatsing dit frame (of richting) uit de aanval; gebruikt als `p+0x5cd` |
| 0x5c8 | f32 | **luchtaanval-venster** (s): luchtaanval alleen als > 0 (`0x457590`); `0x457560(v, force)` zet `max` |
| 0x5cc | u8 | mesh heeft aanvalsvector (`0x42f6b0(p, 0, &p+0x59c, 0) == 1`, gezet in `0x44b4a0`) |
| 0x59c / 0x5a8 | vec3 ×2 | begin/eind van de **snavelvector** (wereldruimte) uit de mesh; zonder vector beide = instantiepositie `p+0xc` |
| 0x5cd | u8 | "gebruik `p+0x5bc` als verplaatsing" (elk frame 0 aan het begin van `0x457a50`) |
| 0x5d0..0x5d8 | vec3 | doelpunt van de pik-dash |
| 0x5dc | u8 | pik-dash heeft een doel (auto-aim) |
| 0x5e0 | f32 | **stormloop-lading** 0..1.5 (HUD toont ·2/3); `−= dt` per frame als > 0 (`0x457a78`) |
| 0x5e4..0x5ec | vec3 | positie aan het begin van de dash (voor de sweep-test) |
| 0x5f0 | ptr | huidig doelwit (vijand); 0 in toestand 3/11 en na muurtreffer |
| 0x5f4/0x5f8/0x5fc | f32/f32/u8 | `0x44ba70`: zolang `+0x5f8 > 0`: elke 0.2 s `+0x5fc = 1` (puls, stofeffect); gezet bij 9→10: `+0x5f4 = 0.2`, `+0x5f8 = lading` |
| 0x5fd | u8 | eerste frame van toestand 9 (sla auto-aim over) |
| 0x600 | f32 | rumble-interval in toestand 10 (0.2 s) |
| 0x604 | obj | **doelzoeker** (`0x4632e0/0x463420`): 16 × {inst, afstand}, `+0x80` = aantal |
| 0x510..0x518 / 0x51c | vec3 / ptr | normaal (xz, genormaliseerd) en instantie van het geraakte "pikbare" oppervlak |
| 0x524 | f32 | blokkeertimer voor "vastpikken" (moet < 0 zijn; `0x464ef0` telt af) |

### 2.2 Trigger `0x457330` (elk frame via `0x44ba70`, ná `0x457a50`)

```c
void Perso_AttackTrigger(Perso *p)   /* 0x457330 */
{
    if (Jumper_IsFalling(p) /*0x44c910: J.state 3|4*/ || p->vtbl[35](p) /*0x44c940: J.state 0|1|7*/)
        p->charge /*+0x5e0*/ = 0;                                   /* in de lucht geen lading */
    if (p->airWin /*+0x5c8*/ > 0) p->airWin -= dt;
    if (p->+0x50c || p->+0x694 || p->moveLock /*+0x238*/ > 0 || p->state /*+0x21c*/ != 0) return;
    if (p->atk != 0) {                                              /* alleen ketting-aanval uit terugslag */
        if (p->atk == 5 && JustPressed(6) && p->airWin > 0) p->atk = 1;
        return;
    }
    if (p->onGround) {
        if (JustReleased(6)) {                                      /* 0x467440(6): STORMLOOP bij LOSLATEN */
            Anim(p, 0x10);
            p->atkT = AnimLen(0x10,0) + AnimLen(0x11,0);
            p->atk = 9;
            if (p->charge <= 0.1f) { LockMove(p, p->atkT, 0); p->charge = 0; }   /* tik = korte stormloop */
            p->first9 /*+0x5fd*/ = 1;  p->rumbleT /*+0x600*/ = 0.2f;
        } else if (Held(6)) {                                       /* opladen */
            p->charge += 4.0f*dt;                                   /* netto +3·dt (−dt in 0x457a50) */
            if (p->charge > 1.5f) p->charge = 1.5f;                 /* vol na ≈ 0.5 s */
        }
    } else if (JustPressed(6) && p->airWin > 0) {                   /* LUCHT: PIK-DASH */
        p->atk = 1;
        Sound(0x37 + rand(0,3) /*0x43ff20(0,3): 0x37/0x38/0x39*/);
    }
}
```

Vóór de trigger test `0x44ba70` (toestand 0) nog `0x463430`: actie 6 *net ingedrukt*, op de grond,
`atk == 0`, en een object uit `0x5e4880[]` met `+0x131 != 0`, `+0x133 == 0` binnen
`(P+4 + 200)` (3D-afstand², `0x4aa164`) ⇒ `p+0x590 = obj`, `obj+0x132 = 1`, `p+0x594 = 1`,
`p+0x58c = 0`, **SetState(6)** (object oppakken/berijden i.p.v. aanvallen).

### 2.3 Toestandstabel `p+0x5b4` (jump-table `0x458bb8`)

| # | adres | naam | gedrag | einde / overgang | anim |
|---|---|---|---|---|---|
| 0 | – | geen | `0x457a50` doet niets (alleen `+0x5cd = 0`, lading −dt) | trigger §2.2 | – |
| 1 | `0x457eac` | pik-dash start (1 frame) | doel zoeken (500), richting bepalen, Mover-richting zetten, `fallen = 0`, `hardFall = 0`, vensters dicht (`0x457560(0,1)`, `0x465e00(0,1)`), spoor-effect `0x47a450` | → **2** | 0xb |
| 2 | `0x458281` | **pik-dash** | `disp = normalize(p+0x5bc)·1500·dt` (`0x4ab2c0`), geen zwaartekracht; muurtest 50 vooruit, anders 100 horizontaal (`0x4575b0`); doelwittest (sweep) | muur ⇒ **6/7/8**; doelwit geraakt ⇒ **3**; geen andere uitgang (zie §5) | (0xb loopt) |
| 3 | `0x458587` | treffer (1 frame) | `+0x5f0 = 0`, `disp = 0` | → **4**, `T = AnimLen(0xc,0)` | 0xc |
| 4 | `0x4585cf` | treffer-stilstand | `+0x5cd = 1`, `disp` blijft (0,0,0) ⇒ hangt stil in de lucht | `T ≤ 0` ⇒ terugslagrichting, `T = 0.75`, → **5** | 0xc |
| 5 | `0x45878c` | **terugslag** | `disp = normalize(r)·(1000·T)·dt` (`0x4aa188`), `r = (−dir.x, 0.8, ≈0)` genormaliseerd; bij `T < 0.5`: venster `p+0x5c8`/`p+0x6f8` = 0.5 als het dicht is ⇒ **kettingaanval** mogelijk (actie 6 ⇒ 1) | `T ≤ 0` ⇒ `Jumper_ForceFall(1)`, → **0** | – |
| 6 | `0x45841d` | muurstuit, laag (≤ 100 boven grond) | jumper elk frame gereset (geen zwaartekracht); bij `T < 0.4` (`0x4aa394`): grond zoeken `0x435650(pos+(0,1,0), −1, 1)` en `p+0x244 −= (pos.y − [0x53a568])·2.5` (veer naar de grond) | `T ≤ 0` ⇒ `0x462990` (op de grond zetten, `onGround = 1`, jumper reset), → **0** | 0xd |
| 7 | `0x4584e1` | muurstuit, hoog (> 100) | jumper gereset (hangt stil) | `T ≤ 0` ⇒ → **0**, `Jumper_ForceFall(0)` | 0xe |
| 8 | `0x458524` | snavel in "pikbaar" oppervlak | jumper gereset | `T ≤ 0` ⇒ **SetState(4)**, anim 0x15, `p+0x50c = 2`, `p+0x520 = 0.8`, → **0** | 0xf → 0x15 |
| 9 | `0x457abe` | **stormloop aanloop** | `disp.xz = M.dir·700·dt` (`0x4ab2c4`), `disp.y = 0`; auto-aim `0x4579a0` (niet in 1e frame); snaveltreffer-test | steile rand (`p+0x234`): knop 4 ingedrukt ⇒ LockMove(0,1), anim-reset, → 0; anders rem → **11**. `T ≤ 0` ⇒ → **10**, `+0x5f4 = 0.2`, `+0x5f8 = lading` | 0x10 (→ sub-anim 0x11) |
| 10 | `0x457c02` | **stormloop** | auto-aim; zolang `lading > 0` en geen steile rand en actie 4 niet ingedrukt: `disp.xz = M.dir·700·dt`; elke 0.2 s rumble `0x44d1b0(P+0xac, P+0xa8)` = (0.5, 0.15); snaveltreffer-test | anders: actie 4 ⇒ LockMove(0,1), anim-reset, → **0** (sprong volgt); anders rem → **11** | 0x11 |
| 11 | `0x457e78` | rem | `+0x5f0 = 0`; geen `+0x5cd` (Mover staat stil door LockMove) | `T ≤ 0` ⇒ → **0** | 0x12 |

"rem" (`0x457b0a`, `0x457e16`, extern `0x458e40`): `lading = 0`, `T = AnimLen(0x12,0) + AnimLen(0x12,1)`,
`LockMove(T, 0)`, anim 0x12, → 11. `0x458e40` (caller `0x44542f`, Game-code) breekt een stormloop (9/10) van
buitenaf af.

De stormloop duurt dus: `AnimLen(0x10,0)+AnimLen(0x11,0)` (aanloop) + resterende lading (max 1.5 s, −dt/s).
Een tik (lading ≤ 0.1) geeft alleen de aanloop en remt dan direct. Snelheid **700 eenh/s** (lopen = 600).

### 2.4 Pseudo-C van de toestanden

```c
void Perso_AttackUpdate(Perso *p)   /* 0x457a50 */
{
    p->useAtkDisp /*+0x5cd*/ = 0;
    if (p->charge > 0) p->charge -= dt;
    switch (p->atk) {
    case 1: {                                                        /* 0x457eac */
        TargetFinder_Scan(&p->finder, &p->pos, 500.0f);              /* 0x4632e0 */
        Inst *t = TargetFinder_Nearest(&p->finder);                  /* 0x463420 */
        p->hasTarget /*+0x5dc*/ = 0;
        if (t) {
            p->aim /*+0x5d0*/ = t->pos /*inst+0xc*/;  bool enemy = false;
            if (t->vtbl[4]() && (*t->vtbl[4]() & 0x1f) == 2) {       /* categorie 2 = vijand */
                enemy = true;  p->target /*+0x5f0*/ = t;
                p->aim.y += t->vtbl[33]() /*hoogte*/ * 0.8f;         /* 0x4a987c */
            }
            if (p->pos.y - p->aim.y > 50.0f) {                       /* 0x4a9030: alleen doelen ONDER de speler */
                p->atkDisp = p->aim - p->pos;
                if (enemy) t->vtbl[37](&p->atkDisp);                 /* +0x94: doelwit waarschuwen */
                p->hasTarget = 1;
            }
        }
        if (!p->hasTarget) { p->atkDisp = normalize(M.dir.x, 0, M.dir.z); p->atkDisp.y = -2.0f; }  /* schuin omlaag */
        p->dashStart /*+0x5e4*/ = p->pos;
        p->atkDisp = normalize(p->atkDisp);
        M.rampA.dir = (atkDisp.x, 0, atkDisp.z); normalize (0x424720); if (len < 0.01) dir.x = 1;
        M.velDir /*+0x1c*/ = M.dir /*+0x10*/ = M.rampA.dir;
        p->J.fallen = 0;  p->J.hardFall = 0;                         /* +0x35c, +0x382: dash wist valschade */
        OpenAttackWindow(p, 0, 1);  OpenAirMoveWindow(p, 0, 1);      /* vensters dicht */
        Anim(p, 0xb);
        vec3 d = normalize(M.dir.x, 0, M.dir.z);  float L = sqrt(d.x*d.x + d.z*d.z + 4.0f /*0x4a94c0*/);
        d = (d.x/L, -2.0f/L /*0x4a9504*/, d.z/L);                    /* 0x45820d..0x458258 */
        SpawnDashTrail(p, &d);                                       /* 0x47a450: effect leeft zolang atk == 2 (0x479d88) */
        p->atk = 2;  return;
    }
    case 2: {                                                        /* 0x458281 */
        vec3 n = normalize(p->atkDisp);
        p->atkDisp = n * (dt * 1500.0f);  p->useAtkDisp = 1;
        if (!Perso_AttackProbe(p, n*50.0f)) {                        /* 0x4575b0 */
            vec3 h = normalize(n.x, 0, n.z) * 100.0f;
            Perso_AttackProbe(p, h);
        }
        break;                                                       /* → doelwitlus */
    }
    case 3: p->atk = 4; p->target = 0; Anim(p,0xc); p->atkT = AnimLen(0xc,0); p->atkDisp = 0; p->useAtkDisp = 1; return;
    case 4:
        p->useAtkDisp = 1;
        if ((p->atkT -= dt) > 0) return;
        base = p->hasTarget ? p->aim : p->pos;
        p->atkDisp.x = base.x - M.dir.x - base.x;                    /* = −dir.x */
        p->atkDisp.y = 0;
        p->atkDisp.z = base.y - M.dir.y - base.y;                    /* 0x458686: gebruikt .y i.p.v. .z (bug in origineel) ⇒ ≈ 0 */
        normalize; p->atkDisp.y = 0.8f; normalize;
        p->atkT = 0.75f;  p->atk = 5;  return;
    case 5:
        p->atkT -= dt;
        p->atkDisp = normalize(p->atkDisp) * (dt * p->atkT * 1000.0f);  p->useAtkDisp = 1;
        if (p->atkT < 0.5f && !(p->airWin > 0)) { OpenAttackWindow(p, 0.5f, 0); OpenAirMoveWindow(p, 0.5f, 0); }
        if (p->atkT <= 0) { Jumper_ForceFall(&p->J, 1); p->atk = 0; }
        return;
    case 6:
        Jumper_Reset(&p->J);
        if ((p->atkT -= dt) < 0.4f) { GroundProbe(pos + (0,1,0), -1, 1); p->vyCorr /*+0x244*/ -= (p->pos.y - g_groundY /*0x53a568*/) * 2.5f; }
        if (p->atkT <= 0) { Perso_SnapToGround(p) /*0x462990*/; p->atk = 0; }
        return;
    case 7: Jumper_Reset(&p->J); if ((p->atkT -= dt) <= 0) { p->atk = 0; Jumper_ForceFall(&p->J, 0); } return;
    case 8: Jumper_Reset(&p->J);
        if ((p->atkT -= dt) <= 0) { Perso_SetState(p, 4); Anim(p, 0x15); p->+0x50c = 2; p->+0x520 = 0.8f; p->atk = 0; }
        return;
    case 9:
        if (p->steepEdge /*+0x234*/) { p->+0x5fc = 0; p->+0x5f8 = 0;
            if (Held(4)) { LockMove(p, 0, 1); p->anim->vtbl[4](); p->atk = 0; } else Brake(p);   /* → 11 */
            return; }
        if (p->first9) p->first9 = 0; else Perso_AutoAim(p);         /* 0x4579a0 */
        if ((p->atkT -= dt) <= 0) { p->atk = 10; p->+0x5f4 = 0.2f; p->+0x5f8 = p->charge; }
        run: p->useAtkDisp = 1; p->atkDisp = (M.dir.x*dt*700, 0, M.dir.z*dt*700);
        break;                                                       /* → doelwitlus */
    case 10:
        Perso_AutoAim(p);
        if (p->charge > 0 && !p->steepEdge && !Held(4)) {
            if ((p->rumbleT -= dt) <= 0) { p->rumbleT += 0.2f; Rumble(p, P[0xac/4] /*0.5*/, P[0xa8/4] /*0.15*/); }
            Anim(p, 0x11);  goto run;
        }
        p->+0x5fc = 0; p->+0x5f8 = 0;
        if (Held(4)) { LockMove(p, 0, 1); p->anim->vtbl[4](); p->atk = 0; } else Brake(p);
        break;                                                       /* → doelwitlus (ook in dit frame) */
    case 11: p->target = 0; if ((p->atkT -= dt) <= 0) p->atk = 0; return;
    default: return;
    }
    Perso_AttackHitLoop(p);                                          /* 0x457ceb, §3 */
}
```

`Perso_AutoAim 0x4579a0`: doelzoeker (500); dichtstbijzijnde met categorie 2 ⇒
`Mover_SetDir(M, (t.x − pos.x, 0, t.z − pos.z))` (`0x459ff0`: reset de drie ramps en zet
`RampA.dir` genormaliseerd) en `p+0x5f0 = t` – **de stormloop stuurt automatisch naar de dichtstbijzijnde vijand**.

`Perso_AttackProbe 0x4575b0(p, v)`: straal van `a = p+0xc + (0,5,0)` (`0x4a9884`) naar `a + v`
(`0x4359b0(&a, &b, −1)`); `[0x53a554]` = trefsoort (0 niets, 1 wereld, 2 instantie), `[0x53a558]` = fractie.
Geen treffer ⇒ 0 (log "On Ground during air attack" als onGround – no-op). Treffer: vonk-effect
`0x479c80(1, &trefpunt, 0)`; als trefsoort 2 en de polygoon-vlag `(poly[0] & 0xff00) == 0x400` van instantie
`[0x53a560]` (poly-index `[0x53a58c]`, 0x90 B per poly): `0x464e00(p, inst)` – vereist `p+0x524 < 0`,
`p+0x26c == 0`, en wandnormaal `|n.y| ≤ 0.05` (`0x4ab7d0`, f64; normaal uit `0x4b3108`); dan
`p+0x510 = normalize(n.x, 0, n.z)`, `p+0x51c = inst` ⇒ **toestand 8**: `T = AnimLen(0xf,0)`, anim 0xf,
LockMove(T), `RampA.dir = M.velDir = M.dir = −n` (kijk naar de wand). Anders grondtest
(`0x435650(pos+(0,1,0), −1, 1)`): `pos.y − [0x53a568] > 100` ⇒ **toestand 7** (anim 0xe) anders
**toestand 6** (anim 0xd), `T = AnimLen(·,0)`, LockMove(T, 0). Altijd: `p+0x5f0 = 0`,
rumble `0x44d1b0(P+0x9c, P+0x98)` = (0.5, 0.5); geeft 1.

### 2.5 Verwante functie `0x464ef0` – vastpikken vanaf de grond/stormloop

Elk frame vóór `0x457a50`: `p+0x524 -= dt` zolang ≥ 0 (daarna niets anders dat frame). Als `p+0x524 < 0`
en actie 6 *net ingedrukt* en `atk ∈ {0, 10}` en `p+0x50c == 0`: straal (`0x4359b0`, cel `p+0x200`) vanaf
de speler langs `normalize(M.dir)` over `P+4 + 100` (= 169); bij een instantie-polygoon met vlag
`(poly & 0xff00) == 0x400` en `0x464e00` geslaagd (wandnormaal, zie §2.4): **SetState(4)**, `p+0x50c = 1`,
`p+0x520 = 0.8`, `Mover_SetDir(M, NULL)`, jumper-reset, aanvalsvenster dicht (`0x457560(0,1)`), en
`RampA.dir = M.velDir = M.dir = −normaal` (genormaliseerd, xz). Toestand 4 (`0x4651d0`) is het
"snavel in hout"-gedrag; vanuit de lucht komt men er via subtoestand 8 (`p+0x50c = 2`).

`0x465b10` (232 instr, na de trigger) doet niets als `atk != 0` of dood (`0x465b3b`).

## 3. Treffertest (doelwitlus `0x457ceb..0x458b96`)

Draait in subtoestand **2, 9 en 10** (9/10 ook in het frame waarin gestopt wordt). Als de mesh geen
aanvalsvector heeft (`p+0x5cc == 0`): `p+0x59c = p+0x5a8 = p+0xc` (en no-op-log "Please get the latest
version of the Woody mesh with the Vector used for Attacks").

Voor elke actor `t` in `0x4c5258[0x4c5324]` (actor-lijst van het vorige frame, `RegisterActor2`), `t != p`;
`tp = *t->vtbl[34]()` (positie):

* **subtoestand 2 (pik-dash)**: `0x433920(&p+0x5e4 /*dashstart*/, &p->pos, 100.0, &tp, t->vtbl[32]() /*straal*/,
  t->vtbl[33]() /*hoogte*/)` – **geveegde cirkel in het xz-vlak**: lijnstuk dashstart→pos tegen cirkel met
  straal `100 + r_t` (kwadratische vergelijking, discriminant ≥ 0, `0 ≤ s ≤ 1`), daarna een y-overlaptest met
  de hoogte (en `0x433bc0` voor de randgevallen); ≠ 0 = raak, `[0x53a558]` = fractie.
  Treffer: `dir = (0,0,0)`, `trefpunt = p+0x59c`.
* **subtoestand 9/10 (stormloop)**: snavelsegment `a = p+0x59c`, `b = a + normalize(p+0x5a8 − a)·50`
  (b wordt teruggeschreven in `p+0x5a8`); cilinder om `c = tp + (0, hoogte·0.5, 0)`:
  `0x433de0(&a, &b, &c, straal, hoogte)` – lijnstuk tegen **verticale cilinder** (xz-kwadratisch,
  y binnen `c.y ± (hoogte − 0.1)`); geeft 0.5 bij raak, −1.0 bij mis. Treffer: `dir = normalize(tp.xz − pos.xz)`,
  `trefpunt = lerp(a, b, 0.5)`; in toestand 10 ook `Mover_SetDir(M, dir)`.

Bij een treffer:
1. subtoestand 2 ⇒ **`p+0x5b4 = 3`**, `isPeck = 1` (anders 0).
2. rumble `0x44d1b0(p, P+0xa4, P+0xa0)` = (0.5, 0.3).
3. `hit = t->vtbl[39](p, P+0x90 /*schade 1.0*/, &dir, &trefpunt, isPeck)` (slot +0x9c; Perso's eigen
   implementatie is `0x44ca00`) – **er wordt geen scriptbericht gestuurd vanuit de controller**; het
   doelwit handelt de treffer zelf af.
4. als `hit` ⇒ `t->vtbl[38](3)` (slot +0x98 = "sterf/verlies, soort 3").

De doelzoeker `0x4632e0(F, &pos, r)` loopt over alle wereldinstanties (`[0x509adc]+0x60/+0x64`) met
`vtbl[4]()` ≠ NULL en typewoord-bit **0x400** gezet, 3D-afstand van `inst+0xc` tot `pos` < r; max 16,
bubble-sort oplopend op afstand; `0x463420` = eerste (dichtste) of NULL.

## 4. Animaties

Animatiecontroller `p+0x494` (klasse vtable `0x4ab7b4`, basis `0x4aa3bc`, ctor `0x463df0`):
`vtbl[0](n)` = `0x463e10` → tabelrecord `0x4b6180 + n·0x1c`: `{int sub[4]; int prio; float speed; u8 restart}`;
`vtbl[2](n)` = `0x436b70` **Request(n)** (max 16 per frame); `vtbl[3](dt)` = `0x436a50` Tick: kiest het verzoek
met de hoogste `prio`; wisselt als er geen huidige is, of `prio_nieuw ≥ prio_huidig`, of de instantie-animatie
klaar is (`inst+0xc0 == 1`); kopieert `sub[0..3]` naar `inst+0xb0..0xbc` (−/te grote index ⇒ 0), bij `restart`
`inst+0xa8 = nu`; `vtbl[4]()` = `0x436a40` Reset (`huidig = −1`). `0x436b90(n, k)` = **AnimLen** =
`model.anim[sub[k]].frames · (1/4096) / speed` (`0x4aa138`). "Animatie klaar" wordt in deze controller **nooit
opgevraagd**: alle overgangen lopen op de timer `p+0x5b8` die bij de start op `AnimLen` gezet is.

Verzoeken komen uit `0x457a50/0x457330` (aanval) en uit `Perso_AnimState 0x463e60` → `0x464630` (toestand 0/6…):
als `atk != 0` of `p+0x694`: niets; jumper-toestand 2 ⇒ `0x463f40` (loop/idle-animaties), anders `0x4642f0(0)`:

| situatie | logische anim | .ins-sub-anims | prio | speed |
|---|---|---|---|---|
| jumper 0/1 (stijgen) | **4** | 3, 4 | 1501 | 8.0 |
| jumper 7 (top) | **5** | 5, 6, 7 | 1050 | 2.0 |
| jumper 3 met `fellOff` | **7** | 6, 7 | 5000 | 3.0 |
| jumper 3 met `shortHop` | **6** | 6, 7 | 1000 | 3.0 |
| jumper 4, onGround en Mover-fase ≠ 2 | **8** (landen) | 8 | 1500 | 3.0 |
| jumper 4 anders | als jumper 3 | | | |
| jumper 5 in de lucht (lange val) | **9** | 7, 32, 33 | 1600 | 3.0 |
| jumper 5 op de grond (harde landing) | **0xa** + `LockMove(AnimLen(0xa,0))`, `p+0x524 = AnimLen`, schok `0x478980(p,1,2.0,180.0,50.0,0)` | 34 | 5200 | 3.0 |
| jumper 6 met `p+0x308 == 2` | geen anim; stofeffect `0x476140(&pos+(0,30,0), &p+0x458, 3, 0.25, 1.5)` | | | |
| pik-dash (atk 1/2) | **0xb** | 9 | 1600 | 3.0 |
| treffer (atk 3/4) | **0xc** | 10, 11 | 1600 | 3.0 |
| muurstuit laag (atk 6) | **0xd** | 16 | 1600 | 3.0 |
| muurstuit hoog (atk 7) | **0xe** | 19, 6, 7 | 1600 | 3.0 |
| vastpikken (atk 8) | **0xf**, daarna 0x15 (toestand 4) | 21, 13 / 13 | 1600 / 5000 | 3.0 |
| stormloop aanloop (atk 9) | **0x10** | 38, 51 | 1700 | 3.0 |
| stormloop (atk 10) | **0x11** | 51 | 1700 | 3.0 |
| rem (atk 11) | **0x12** | 51, 52 | 1700 | 3.0 |

In toestand 1 (`p+0x21c == 1`) gebruikt `0x4642f0` de set 0x6b..0x6f, met argument 1 de set 0x47..0x4c.

## 5. Landingsring onder de speler (port-reconstructie, issue #1)

Zodra Woody los van de grond is ligt er in het origineel een **heldere ring op de vloer onder hem**: de plek
waar hij neerkomt. Het is niet de schaduw — die is geprojecteerde geometrie (LIGHTING.md §4), ligt er ook als
hij gewoon loopt en heeft de vorm van het model; de ring hoort bij de sprong en verdwijnt bij de landing.

**Niet gedecompileerd.** De tekenfunctie is in `Woody.exe` nog niet aangewezen; de ring hieronder is van een
schermafdruk afgelezen. Beste kandidaat om te lezen: **`0x44af90(Perso)`**, de enige nog ongelezen Perso-functie
die elk frame ná het renderen draait (`0x401dbd` → `0x44b4a0`, PERSO_FRAME.md §1 stap 24, naast
`0x44ae60` Perso_UpdateHUD) — precies de plaats voor een grond-decal. Het gereedschap is er ook: de
sprite-primitief `0x470f10` tekent zonder vlagbit 0 géén billboard maar een quad in het vlak met de normaal uit
`S+0x230` (PERSO_DEATH.md §4.1, PROJECTILES.md §5.3), dus een platte quad op de grondnormaal.

Wat de port doet — `player_landing_ring` (`src/player.c`), `hud_world_ring` (`src/hud.c`), aanroep in
`src/main_engine.c` tussen de wereld en de HUD:

| | |
|---|---|
| wanneer | zolang `on_ground == 0` én de speler onder eigen gewicht valt: niet dood, geen gescripte actie (toestand 5), niet op de raket (toestand 8), niet aan een wand (toestand 4) en niet tijdens de klim-over-wortelbeweging. Niet in cinematics, in de vrije camera of buiten een speelbaar level |
| waar | `GetHeight` (`world_ground`, `0x435650`) vanaf de voeten + 43 (`P+0x00`) recht omlaag, dus zowel wereldpolygonen als de press-/hull-nodes van instanties (ook op een bewegend platform). Geen maximumval: boven een put licht de bodem op; geen vloer gevonden ⇒ geen ring. De ring staat recht onder hem, er wordt niet vooruit gerekend met zijn horizontale snelheid |
| hoe | een ring in het vlak van de gevonden vloernormaal, 3 eenheden erboven (anders z-fight hij met de vloer), straal **69** = de botsingsstraal van de Perso (`P+0x04`), bandbreedte ±12 % van de straal, wit, additief, helderheid 0.7, 48 segmenten. Vaste maat: de ring krimpt of vervaagt niet met de hoogte. De helderheid zit in de vertexkleuren (0 op beide randen, vol op de straal), dus de band heeft geen harde rand en er is geen textuur voor nodig |
| stelschroeven | `WOODY_RING=<straal>` zet de straal, `WOODY_RING=0` schakelt de ring uit |

Onzeker zolang `0x44af90` niet gelezen is: straal, dikte, kleur en helderheid, of de ring pulseert of meedraait,
of het origineel hem op de grondnormaal legt of horizontaal, en of andere actoren er ook een krijgen.

## 6. Open vragen

* Subtoestand 2 (pik-dash) heeft **geen eigen time-out**: hij eindigt alleen door een muur-/grondtreffer van
  `0x4575b0` (50 langs de dashrichting, daarna 100 horizontaal) of een doelwittreffer. Omdat de richting
  altijd omlaag wijst (y = −2 vóór normalisatie ⇒ ≈ 63° omlaag zonder doel) raakt hij normaal de grond ⇒
  toestand 6. Wat gebeurt als de botsing in `0x4624f0` de speler op de grond zet zonder dat de straal iets
  raakt (log "On Ground during air attack") is niet verder gevolgd; externe resets van `+0x5b4`:
  `0x44c980` (SetState), `0x44accb` (Reset), `0x44c372/0x44c690/0x44cbdf` (TakeHit/hit), `0x465787`, `0x41e432`, `0x44dd78`.
* De terugslagrichting in toestand 4 gebruikt `M.dir.y` voor de z-component (`0x45868a`); dat lijkt een bug
  in het origineel (terugslag alleen langs wereld-x). In een herimplementatie is `(−dir.x, 0.8, −dir.z)`
  genormaliseerd waarschijnlijk de bedoeling – met Frida verifiëren.
* Eenheid van `AnimLen`: `frames/4096/speed` – of `anim+4` frames of 1/4096-seconden-ticks zijn is niet
  nagegaan (zie FORMAT_INS.md); de getalwaarden van `AnimLen(0xb..0x12)` moeten uit Woody's model gelezen worden.
* Toestand 4 (`p+0x21c`, handler `0x4651d0`, "snavel vast in pikbaar oppervlak", `p+0x50c = 2`, `p+0x520 = 0.8`)
  is niet gedecompileerd.
* `vtbl[37]` (+0x94) en `vtbl[39]` (+0x9c) van de vijandklassen (wat doet een treffer per vijandtype, welke
  scriptevents volgen) zijn niet gevolgd.
* `0x478980(p, 1, 2.0, 180.0, 50.0, 0)` (harde landing) en `0x479c80` (vonk) zijn niet gelezen.
