# CINEMATIC: real-time cinematics (berichten 1130/1131/1132), camera-mode 0x80, gescripte Perso-acties, tekst 1080

Statische analyse van `Woody.exe` (imagebase 0x400000). Elk feit heeft een adres. Zie ook MESSAGES.md (game-handler
`0x444870`), INSTANCE.md (animatieklok `0x43eee0`), CAMERA_SCRIPT.md §4.3, GAMEFLOW.md.
De engine noemt dit zelf een **"Real time cinematic"** (strings `0x4b3b78`, `0x4b3b48`, `0x4b3c00`, `0x4b3bb8`).

## 0. Samenvatting

* Het cinematic-object `Cin` zit **inline in het Game-object op `game+0x64`** (geen eigen allocatie; reset `0x44e940`,
  aangeroepen uit de Game-init `0x4458d4`). Update per frame: `0x44f0a0(Cin, dt)` vanuit de Game-update `0x445af4`.
* `1131 [inst, anim]` = kies de **hoofdinstantie** (levert camera + eindpositie) en haar animatie;
  `1132 [inst, anim]` = voeg een **acteur** toe (max. 32); `1130 [vecInst, track, var]` = start: `vecInst` = instantie
  met een typecode-5-node (2 punten = plaats + richting), `track` = muzieknummer, `var` = scriptvariabele die bij de
  echte start `ftol(t0·100)` krijgt (t0 = starttijd-offset die de muziekspeler teruggeeft, in s).
* Toestanden (`Cin+0x120`): 0 = uit, 1 = fade-out (0.5 s), 2 = speelt, 3 = fade-out aan het eind (0.5 s),
  4 = fade-in na afloop (0.5 s) → 0.
* Tijdbasis: alle instanties spelen hun animatie met **snelheid 3.0** (`0x42e290(3.0)`), dus duur =
  `duration/4096/3` s = `duration · 8.138e-5` (`0x4aace8`); eenmalig (slot1..3 = −1 → klemt op het laatste frame).

## 1. Het object `Cin` = `game+0x64`

| offset | type | betekenis | bewijs |
|---|---|---|---|
| +0x000 | i32 | animatie van de hoofdinstantie (1131 arg 2) | `0x44e988` |
| +0x004 | Instance* | hoofdinstantie (1131 arg 1); 0 = niet geïnitialiseerd | `0x44e98a`, test `0x44e9e6` |
| +0x008 + 8·i | i32 | acteur i: animatie (1132 arg 2) | `0x44e9af` |
| +0x00c + 8·i | Instance* | acteur i: instantie (1132 arg 1); i < 32 | `0x44e9bd`, grens `0x44e9a6` |
| +0x108 | i32 | muziektrack (1130 arg 2); −1 = geen ("No Track selected…" `0x44ea60`) | `0x44e994` |
| +0x10c | u32 | scriptvariabele-ref (1130 arg 3) | `0x44ea58` |
| +0x110 | i32 | aantal acteurs | `0x44e9c8` |
| +0x114 | f32 | resterende speeltijd (s) | `0x44ea25`, `0x44f182` |
| +0x118 | f32 | fade-timer (s) | `0x44ea31`, `0x44f0c2` |
| +0x11c | Instance* | vector-instantie (1130 arg 1) | `0x44e9ff` |
| +0x120 | i32 | toestand 0..4 | `0x44ea15`, `0x44f0a3` |
| +0x124 | f32 | duur fade-out vóór start = 0.5 | `0x44e960` |
| +0x128 | f32 | duur fade-in bij start = 0.5 | idem |
| +0x12c | f32 | duur fade-out aan het eind = 0.5 (ook: eindmarge) | idem, `0x44f197` |
| +0x130 | f32 | duur fade-in na afloop = 0.5 | idem |
| +0x134 | vec3 | eindpositie voor de speler (= `game+0x198`) | `0x44f06e..0x44f081` |
| +0x140 | 3×vec3 | eindrotatie (rijen; rij 1 = kijkrichting = `game+0x1b0`) | `0x44f014..0x44f064` |

Reset `0x44e940`: `+0x108 = −1`, `+0x120 = 0`, `+4 = 0`, `+0x110 = 0`, de vier fadetijden = 0.5 (`0x3f000000`).
Omdat de reset na afloop draait (`0x44f26c`) moeten **1131/1132 vóór elke cinematic opnieuw gestuurd worden**.
Queries: `0x44f2d0` = "toestand == 1", `0x44f2e0` = "toestand 2 of 3" (= **cinematic loopt**).

## 2. De berichten (game-handler `0x444870`)

| id | args | code | werking |
|---|---|---|---|
| 1131 | inst, anim | `0x444acf` → `0x44e980(Cin, instPtr, anim)` | `Cin+4 = inst`, `Cin+0 = anim`. inst = index in de instantietabel `[0x50944c]+0x6c` (`& 0xffffff`); House/W1A/W3D/WWS: **instantie 0 = de Perso zelf** (`1200 [0x1000000, 1]`; het Woody-model heeft de camera-nodes 0x80/0x180); W1B inst 8, W2B 1, W2D 3, W3B 310, K1A 291, K1R 124, …: telkens de instantie die `1200 SetTypeInstance` 1/2/3/18 krijgt = **altijd de Perso**; races gebruiken anim 10 i.p.v. 72. Animaties: 72..75 (meerdere cinematics per level = 72, 73, …) |
| 1132 | inst, anim | `0x444afb` → `0x44e9a0` | acteur toevoegen (stil genegeerd bij ≥ 32) |
| 1130 | vecInst, track, var | `0x444a90` → `0x44e990(track)`; `0x44e9e0(vecInstPtr, var)` | start, zie onder. `track` = index in de streamtabel `0x4b73a0` (8 = `/Rtc/Menu.wav`, 9 = `/Rtc/Woody/W1A.wav`, 10/11 = W1Ba/b, 12 = W2B, 13..15 = W2Da..c, 16 = W3B, 17..20 = W3Da..d, 21/22 = WWSa/b, 23.. = Knothead…; 0..7 = de gewone levelmuziek) |

`0x44e9e0(vecInst, var)`:
```c
if (!cin->main) { warn("You can't start a Real time cinematic without initialisation..."); return; }
cin->vecInst = vecInst;
cin->remain  = model(cin->main)->anim[cin->anim].duration * 8.138e-5f;   // 0x4aace8 = 1/(4096*3)
cin->state   = 1;
cin->timer   = cin->fadeOut0;                  // 0.5
App_FadeOut(cin->timer - 0.1f);                // 0x401480: naar zwart in 0.4 s (0x4a9008 = 0.1)
cin->var = var;
if (cin->track == -1) warn("No Track selected for Real time cinematic...");
if (music) { music->vt[0x94](cin->track);      // 0x46cc00: vorige rtc-stream stoppen, tracknummer onthouden
             music->vt[0x50](cin->timer*0.9f); }   // levelmuziek uitfaden in 0.45 s (0x4a94b8 = 0.9)
```

## 3. Per frame: `0x44f0a0(Cin, dt)` (sprongtabel `0x44f278`: `0x44f0c2`, `0x44f14d`, `0x44f1da`, `0x44f24d`)

```c
bool Cin_Update(Cin *c, float dt) {             // return 1 = "cinematic is net afgelopen" (1 frame lang)
  switch (c->state) {
  case 1:                                        // zwart worden
    c->timer -= dt;  if (c->timer > 0) break;
    c->timer = c->fadeIn0;                       // 0.5
    App_FadeIn(c->timer);                        // 0x401440
    App_DrawBlack();                             // 0x4014c0: meteen een zwart vlak 640x480
    float t0 = 0;
    if (music) while (!music->vt[0x98](&t0)) ;   // 0x46cc20: stream 0x4b73a0[track] openen; zet t0 = 0; geeft altijd 1
    c->state = 2;  Cin_Start(c, t0);             // 0x44eab0
    break;
  case 2:                                        // speelt
    debug("Time : %f / %f", total - c->remain, total);     // 0x44f178, total = duration*8.138e-5 (0x4aacec)
    c->remain -= dt;
    if (c->remain > c->fadeOut1 /*0.5*/) break;
    c->timer = c->remain;  App_FadeOut(c->remain);          // laatste halve seconde: naar zwart
    c->state = 3;  break;
  case 3:
    c->timer -= dt;  if (c->timer > 0) break;
    c->timer = c->fadeIn1;  App_FadeIn(c->timer);           // 0.5
    c->state = 4;
    Cin_ComputeEnd(c);                                      // 0x44edb0 (§5)
    if (music) music->vt[0x54](c->timer * 0.9f);            // levelmuziek weer infaden (0.45 s)
    return 1;
  case 4:
    c->timer -= dt;  if (c->timer <= 0) Cin_Reset(c);       // 0x44e940 -> state 0, main = 0, nActors = 0, track = -1
    break;
  }
  return 0;
}
```

### 3.1 Start `0x44eab0(Cin, t0)`
```c
vec3 P[2];
if (!Inst_GetTypecodePoints(c->vecInst, 5, P, 0)) {        // 0x42f6b0: 0-de node met typecode 5 (flags>>8 == 5),
    warn("You must give a vector to begin a real time cinematic...");   // punten -> wereld (matrixpalet [0x509adc]+0xa0)
    c->remain = 0; return; }
SetVar(c->var, ftol(t0 * 100.0f));                         // 0x443ca0; 0x4a9010 = 100. In deze build is t0 altijd 0 => var = 0
c->remain -= t0;
Instance *m = c->main;
m->start = now - t0;   m->slot[0] = c->anim;   Inst_SetSpeed(m, 3.0f);   // +0xa8, +0xb0, 0x42e290(3.0): +0xa0 = +0xa4 = 3
m->flags8 &= ~0x20;                                        // klok mag de cel weer bepalen
m->slot[1] = m->slot[2] = m->slot[3] = -1;                 // eenmalig: klemt op het laatste frame
m->pos = m->center = P[0];                                 // +0xc en +0x60
vec3 d = -(P[1] - P[0]);  d.y = 0;  normalize(d);          // LET OP het minteken (fchs 0x44ebd6/0x44ebe4)
m->rot = { cross(d,(0,1,0)), d, (0,1,0) };                 // rijen +0x28/+0x34/+0x40, zelfde conventie als INSTANCE.md r. 214
for (i = 0; i < c->nActors; i++) {                         // 0x44ecc0
    Instance *a = c->actor[i].inst;
    a->slot[0] = c->actor[i].anim;  a->start = now - t0;  Inst_SetSpeed(a, 3.0f);
    a->center = a->pos;  Inst_Recell(a, 0);                // 0x4077f0(0): uit de cel halen en ALTIJD opnieuw in de cel zetten ⇒ een door het
                                                           // script verborgen acteur (bericht 6 [a, 0]) wordt weer zichtbaar (W1B: Buzz 398 + schotel 399)
    a->slot[1] = a->slot[2] = a->slot[3] = -1;
}                                                          // acteurs worden NIET verplaatst: ze staan waar het level ze zet
if (!Inst_HasCameraTrack(m, c->anim)) {                    // 0x42feb0
    warn("No Camera in the main instance given to play the Real time cinematic"); return; }
cam->animInst = m;                                         // CamMgr+0x5d4
cam->flags618 |= 2;                                        // letterbox aan
CamMgr_SetTransition(cam, 2);                              // 0x41f9f0(2) = harde cut
CamMgr_SetMode(cam, 7, 0);                                 // 0x41f410: mode 0x80
```
`now` = `[0x509adc]+0x30`. Alle instanties lopen dus op **dezelfde wereldklok met snelheid 3**, fase =
`(now − start)·3·4096/duration` (INSTANCE.md §1.2); verdere synchronisatie is er niet. Animaties van verschillende
lengte eindigen elk op hun eigen laatste frame (klem, snelheid → 0). De looptijd van de cinematic is die van de
**hoofdanimatie**.

### 3.2 Wat de rest van de engine doet zolang `0x44f2e0` waar is (toestand 2/3)
| waar | effect |
|---|---|
| hoofdlus `0x401cf9..0x401d07` | **`Perso::Update` (`0x44b530`) wordt overgeslagen**: geen invoer, beweging, botsing of Perso-animatiekeuze; de spelerinstantie wordt alleen nog door de gewone instantieklok/render geanimeerd (slot0 = cinematic-animatie) |
| hoofdlus `0x401cc2` | geluidsbron van de Perso (`Perso+0x4a4`) stoppen (`0x468e20`) |
| hoofdlus `0x401e19` | HUD wordt niet getekend |
| `0x4033cc/0x4033db` en `0x445980` | pauzemenu geblokkeerd (ook in toestand 1) |
| Perso `0x44afc5`, `0x44c136/0x44c14b` | twee Perso-routines (o.a. schade) overgeslagen (ook in toestand 1) |
| `0x44e6c0` (`0x44e690`, hoofdlus `0x401d16`, **alleen House**) | Perso-transparantiedoel `0x44e7f0`: 0 (zichtbaar) tijdens de cinematic, anders 1.0 (onzichtbaar), snelheid `+0x100 = 10000` (Onzeker 3) |

De scripts (VM) lopen gewoon door: ondertiteling/fades/geluiden van de cinematic worden door het levelscript met
`DELAY`s getimed (§7). **Er is geen skip-toets** in `0x44f0a0`; alleen de House-intro kan worden afgebroken, door het
menu (pagina 0x1f, toets 6/9, GAMEFLOW §5): script-object stoppen, `SetVar(app+0x8c, 4)`, `0x44f290(Cin)` =
reset + `music->vt[0x54](timer·0.9)` + `vt[0x9c]()` (rtc-stream stoppen), Perso `vtbl[0x44]()` (`0x405129..0x40515b`).

## 4. Camera-mode 0x80: `0x41f1ee` → `0x42fa40` → `0x42fa80`

`CamMgr_Update` mode 0x80 (`0x41f1ee`): `I = CamMgr+0x5d4`; als `0x42feb0(I, I->slot0)`:
`0x42fa40(I, &CamMgr+0x140 /*view-matrix*/, &pos, &CamMgr+0x5d8 /*doel*/)`, `CamMgr+0x1d0 = pos`; daarna altijd
`CamMgr+0xc4 (look-at) = CamMgr+0x5d8`. **Geen fov-track**: de fov is die van de letterbox (CAMERA_SCRIPT §1.4,
sy = 0.5625 → vfov 68.0°). Geen overgang (cut), geen botsing, geen smoothing.

`0x42feb0(I, anim)`: loop de **top-level broerlijst** af (`S+0x6c` = eerste node, 1-based; volgende = `N+0x80`, −1 =
einde), onthoud de laatste node met `flags == 0x80`; resultaat = `N->posTracks(+0x70)[anim].count(+4) != 0`.

`0x42fa40`: `phase = I->pos_ac(+0xac) / (duration[I->slot0] / 4096)` (`0x4aa138` = 1/4096) → dus de fase die de
instantieklok dit frame heeft gezet (de klok van de hoofdinstantie moet vóór de camera-update gedraaid hebben, anders
loopt de camera 1 frame achter).

`0x42fa80(I, outView, outPos, phase, anim, outTarget)`:
```c
M = diag(I->scale) * I->rot;  T = I->pos;                 // rijvectoren: w = l.x*M[0] + l.y*M[1] + l.z*M[2] + T
frame = model->anim[anim].nframes * phase;                // float, zelfde eenheid als de klok (0x43a2b0)
camN = node met flags == 0x80;  tgtN = node met flags == 0x180;  // soort 0x80, typecode 0 resp. 1; top-level
                                                          // (lus stopt pas als beide gevonden zijn -> model MOET beide hebben)
vec3 c = PosTrack(camN, anim, frame);                     // 0x43a660: alleen de POSITIE-track van de node zelf,
vec3 t = PosTrack(tgtN, anim, frame);                     //   geen ouder, geen rotatie
C = c*M + T;   G = t*M + T;                               // naar wereld met de INSTANTIE-matrix (niet de botten)
if (outTarget) *outTarget = G;
f = normalize(G - C);
r = normalize(cross(f, (0,1,0)));                         // 0x41af10(this=f, out, arg) = this x arg
u = cross(f, r);                                          // view-y wijst OMLAAG (f=(0,0,1) -> r=(-1,0,0), u=(0,-1,0))
outView = kolommen {r, u, f}, translatie = (-r.C, -u.C, -f.C);     // v_view = v_world * outView
*outPos = C;
```
Geen asconversie: track-posities worden gebruikt zoals ze in de .ins staan (y-up, net als de meshes). Roll bestaat niet
(up = wereld-y). Voor de port: `eye = C`, `center = G`, `up = (0,1,0)`.

### 4.1 Positie-track met cut-detectie `0x43a660(node, out, anim, frame)`
Zoekt het eerste sleutelframe met `key.t >= frame` (16-byte frames `{t, x, y, z}`, FORMAT_INS §2.2 veld 7). Als de
twee omliggende sleutels precies **1 frame uit elkaar** liggen (`ftol(t1 − t0) == 1`): evalueer de gewone lineaire
track (`0x43a590`) op `floor(frame)` en op `floor(frame)+1`; is de afstand daartussen **> 200** (kwadraat > 40000,
`0x4a9890`) → geef de positie op `floor(frame)` terug (**camerasprong = harde cut, niet interpoleren**). Anders (en in
alle andere gevallen) gewoon `0x43a590(frame)` = lineaire interpolatie. Dit geldt voor camera én doel afzonderlijk.

## 5. Einde: `0x44edb0` en de Game-update `0x445af9..0x445b66`

`0x44edb0(Cin)` (bij de overgang 3 → 4, beeld is zwart):
```c
m->flags8 |= 0x20;
A = RootNode(m, anim, phase 1.0) * W(m);          // 0x42f7e0(1.0, anim, &A, 1): eerste top-level node met flags == 0
                                                  //   (soort 0, de wortel van het skelet): rot-track 0x43a9c0 (+0x74),
                                                  //   pos-track 0x43a590 (+0x70), frame = nframes*phase; * instantie-matrix
B = RootNode(m, anim 0, phase 0);                 // 0x42f7e0(0, 0, &B, 0): rustpose, zonder instantie-matrix
C = inverse(B) * A;                               // 0x440fc0(B,1,1,1) ; 0x4405e0
pos = C.t   (via A^-1 en weer A heen en terug gerekend, 0x44ee5d..0x44efb2; netto C.t, zie Onzeker 2)
dir = normalize_xz(C.row2);                       // lokale z-as van de wortel, y = 0 (0x4239f0)
cin->endPos = pos;                                // +0x134
cin->endRot = { cross(dir,(0,1,0)), dir, (0,1,0) };   // +0x140 / +0x14c / +0x158
```
Game-update (`0x445af4`): als `Cin_Update` 1 teruggeeft:
```c
vec3 dir = game->+0x1b0;                          // = cin->endRot rij 1
Perso->vtbl[0x44]();                              // Perso-reset (dezelfde als bij respawn, PERSO_FRAME §respawn)
Perso_SetFacing(Perso+0x388, &dir);               // 0x459ff0
Perso_SetPos(Perso, &game->+0x198);               // 0x44a650 (= cin->endPos), grond opnieuw vastleggen
CamMgr_SetTransition(cam, 2);  cam->+0x368 = 0;  CamMgr_SetMode(cam, 0, 0);   // 0x445b66: cut naar de volgcamera achter de speler
```
De letterbox gaat uit in `CamMgr_Update` zodra de vorige mode 0x80 was en de nieuwe niet (CAMERA_SCRIPT §1.4,
`0x41f34f`). **Er wordt niets naar het script gestuurd of geschreven aan het eind**; het script weet de lengte zelf
(vaste `DELAY`s t.o.v. de start, §7). De acteurs blijven op hun laatste frame staan tot het script ze verbergt.

## 6. Perso: gescripte acties `0x44dda0(Perso, actie, vec6*, doelInst)`

**Het actienummer IS het ruwe .ins-animatienummer** van het spelermodel: `0x463e30(actie)` zoekt in de logische
tabel `0x4b6180` (0x80 records × 0x1c: `{int sub[4]; int prio; float speed; u8 restart}`) het eerste record met
`sub[0] == actie` (daarna pas `sub[1]`, …) en geeft die logische index; ook de cameratest gebruikt het actienummer
direct als animatie-index (`0x44df67`: `0x42feb0(Perso, actie)`).

```c
bool Perso_ScriptedAction(Perso *p, int act, const float *v6, Instance *target) {
    if (p->state == 2) return 0;                                  // +0x21c
    p->target554 = target;  p->camAllowed558 = 1;
    if ((p->flags104 & 0x3e0) != 0xa0 && != 0x80) { p->fadeOutFlag560 = (act == 17); p->fadeInFlag561 = (act == 18); }
    // geldige acties: 10..16, 19, 72..78 (tabel 0x44dfe8); 17/18 idem + eerst 0x443ff0(p->+4); anders
    // warn("Animation n°%d is not know as a cinematic animat…") en toch doorgaan
    p->altMode4ec = 0;  0x44dd70(p);                              // sprong-/aanvalstoestand wissen
    p->logical534 = LogicalFromRaw(act);                          // 0x463e30
    p->total538 = p->remain53c = L(sub[0]) / rec.speed;           // 0x436b90: duration/4096/3.0  (= duur in s)
    animctl->vt[4]();  animctl->vt[2](p->logical534);             // reset + start logische animatie
    p->action540 = act;   Perso_SetState(p, 5);                   // 0x44c980
    if (v6) {                                                     // vector = typecode-5-node van de inst uit het bericht
        p->pos = v6[0..2];  p->onGround22c = 1;
        dir = normalize_xz(v6[3..5] - v6[0..2]);  Perso_SetFacing(p+0x388, &dir);   // hier ZONDER minteken
        p->useRootPos550 = 0;  Perso_Orient(p);                   // 0x44bd00
    }
    if (Inst_HasCameraTrack(p, act) && p->camAllowed558) {        // alleen anims met cameratrack (72..78)
        cam->animInst = p;  cam->flags618 &= ~2;                  // GEEN letterbox (verschil met 1130)
        CamMgr_SetTransition(cam, 2);  CamMgr_SetMode(cam, 7, 0); }
    animctl->vt[3](p->dt2f8);
    return 1;
}
```
Toestand 5 per frame (`0x44db50`):
* `target554` ≠ 0: `0x42fa40(p, …, &camPos, 0)` → `target->pos = camPos`, `target->rot = p->rot`, klok forceren
  (`+0x58 = −1`, `vt[2]()`): een instantie die de **animatie-camera volgt** (door scripts nooit gebruikt: 1043 komt 0× voor).
* acties 17/18: `fadeOutFlag` en `remain < 0.6` (`0x4a9650`) → `App_FadeOut(0.5)` (eenmalig); `fadeInFlag` →
  `App_FadeIn(0.5)` direct in het eerste frame. `remain -= dt`; bij ≤ 0 → toestand 0; bij 18 bovendien
  `facing = −facing` (`0x4a9500` = −1: Woody komt achterstevoren uit de deur en draait om), `0x462990` (grond), idle
  (logische anim 1) en `0x44e5a0`.
* overige acties: `remain -= dt`; bij ≤ 0 → toestand 0 + idle; elk frame `0x44e290(laatste)`:
  `E = inverse(Root(anim 1, fase 0)) * Root(act, fase 1) * W` (eindpunt), `Q` = idem op `fase = (total − remain)/total`;
  `p->rootPos544 = (1 − fase)·p->pos + fase·Q'` en `useRootPos550 = 1` (weergave-/camerapositie volgt de wortel);
  op het laatste frame: idle, `facing = −E.row2`, `p->pos = E.t`, `useRootPos = 0`, `action540 = −1`, `0x462990`
  → **de Perso wordt verplaatst naar waar de animatie de wortel heeft gebracht**; daarna `0x44e5a0`.
* `0x44e5a0` (einde): volgcamera-parameters herstellen (`CamMgr+0x338`: spelerpositie-ptr, kijkrichting, `+0x28 = −1`,
  `+0x30 = 0`), `0x41f9d0(0.5)` (overgangsduur 0.5 s), `SetTransition(1)` (vloeiend), `SetMode(0, 0)` (`0x44e634`).

| actie (= .ins-anim) | logisch record (`0x4b6180`) | keten | beweging / bijzonder | gebruikt door |
|---|---|---|---|---|
| 10 | 12 | 10 → 11 | wortelbeweging | scripts: alleen als 1131-animatie in de race-levels (K/S 1R..3R) |
| 11 | 121 | 11 → 0 | | – |
| 12 / 13 / 14 / 15 | 20 / 21 / 22 / 23 | 12→13 lus / 13 lus / 14→6→7 / 15 | | – (gewone gameplay-records) |
| 16 | 13 | 16 → 0 | | – |
| **17** | 24 | 17, eenmalig | **deur inlopen**: Perso op P0 van de deur-vector, kijkt naar P1; fade-out 0.5 s als nog 0.6 s rest; duur = L(17)/3 | 1040 `[deur, 17]` (192×) |
| **18** | 25 | 18, eenmalig | **deur uitkomen**: fade-in 0.5 s bij start; aan het eind facing omgekeerd | 1040 `[deur, 18]` (159×) |
| 19 | 14 | 19 → 6 → 7 | | – |
| 72 / 73 | 81 / 82 | N → 0 | cinematic-anims met cameratrack | 1131 (W-levels 72..75); **0x49 = 73**: menu-pose House, engine `0x44e6b0` (trigger `Perso+0x57c` via bericht 1141 `0x4448d1` → `0x44e640`, GAMEFLOW §4.5), `0x4594f0`, `0x4788a7`, `0x47896b` |
| **74 (0x4a)** | 26 | 74, eenmalig | plaatsing bij de hub-deur `0x453dbb` (vector in `Perso+0x72c`) | engine |
| 75 (0x4b) | 27 | eenmalig | toestand 9 / SaveAuto `0x4540f4`, `0x45412c` | engine |
| 76 (0x4c) / 78 (0x4e) | 29 / 28 | eenmalig | juichen `0x453ffd` / `0x453fd6` | engine |
| 77 (0x4d) | 30 | eenmalig | `0x45402e`, `0x454186`, `0x4541b0` | engine |

Alle records hebben `speed = 3.0` → duur = `duration/12288` s, net als bij 1130. Omdat 74..78 als `sub[0]` eerst in de
eenmalige records 26..30 gevonden worden, zijn de records 83..87 (`N → 0`) voor `0x44dda0` onbereikbaar.

## 7. Scriptprotocol (House obj 115, W1A obj 277; identiek in alle levels)

```
init:   var = -1
start:  (DELAY 50..100: rekwisieten wisselen met bericht 6 = tonen/verbergen, evt. 56/57 fade)
        1131 [hoofdInst, anim]            ; W-levels: spelerinstantie, anim 72..75; K/S-levels: aparte instantie; races anim 10
        1132 [acteur, anim] ...           ; 2..10 stuks, anim meestal 0 of dezelfde als de hoofdanim
        1130 [vecInst, track, var]
wacht:  if (var != -1)                    ; de engine schrijft var = ftol(t0*100) = 0 zodra het beeld zwart is en het afspelen start
           tijdlijn: voor elke gebeurtenis op T (1/100 s na start):  DELAYPOP (T - var) -> actie
```
House: T = 900 → `1151 [300]` (fade-out 3 s), +290 → acteurs wisselen + `1152 [0]`; T = 4200/4600/4950… → `1080`-tekst,
T = 4500/4900… → `var4 = 1` (tekst sluiten). W1A: T = 1600 → acteurs 276/278 verbergen, 220 tonen. `var` is dus de
**al verstreken tijd bij de start** (muziek-synchronisatie, in deze build altijd 0); −1 is alleen de eigen
"nog niet gestart"-waarde van het script. Aantallen: zie `1130` in House 1×, W1A 1×, W1B 2×, W2D 3×, W3D 2×, WWS 2×, elk K/S-level 1×.

## 8. Bericht 1080: tekstvak `0x456ed0(Game+0x18, record)`; update/teken `0x4571c0(dt)`

`1080 [hAlign, vAlign, var, id1, id2, id3]` (regels tot de eerste −1, max. 3):
* `hAlign` (arg 1, `0x45707b`): 0 = gecentreerd, 1 = links (x = 16), 2 = rechts (`breedte − w − 16`).
* `vAlign` (arg 2, `0x456fe5`): 0 = midden, 1 = boven (y = 16), 2 = onder (`hoogte − totale teksthoogte − 16`).
* `var` (arg 3 → `+0x30`): **sluitvlag die het script zet**; de engine schrijft er niets in.
* `idN` = resource-refs (`RckGet` `0x43fb30`/`0x441580`, RCK.md: `0x0002xxxx` = string xxxx uit bank 0 =
  `extract/Common/<karakter>.rck`, glyph-indices); font = `0x01030000` (font 0 van de levelbank). Fontgrootte start op
  25 en krimpt per stap 1 tot elke regel past (ondergrens 17, `0x4ab2b8`), daarna −2. `0x20001` = lege scheidingsregel
  (de achtergrondrechthoek begint pas ná die regel, `0x457105`).
* Toestanden (`+4`): 1 = fade-in 0.5 s (alpha = 2t·`0x4aa2f0`), 2 = staat tot `*var != 0` (`0x457245`), 3 = fade-out
  0.5 s, 0 = weg. Achtergrond = halfdoorzichtig zwart vlak (`0x480a10`, alpha = tekstalpha/2) met marge 16.

## 9. Recept voor de port

```c
typedef struct { int anim; Instance *inst; } CinActor;
typedef struct { int anim; Instance *main; CinActor actor[32]; int track; unsigned var; int nActors;
                 float remain, timer; Instance *vecInst; int state; vec3 endPos, endDir; } Cin;

void msg_1131(Instance *i, int anim) { cin.main = i; cin.anim = anim; }
void msg_1132(Instance *i, int anim) { if (cin.nActors < 32) cin.actor[cin.nActors++] = (CinActor){anim, i}; }
void msg_1130(Instance *vec, int track, unsigned var) {
    if (!cin.main) return;
    cin.vecInst = vec; cin.track = track; cin.var = var;
    cin.remain = anim_duration(cin.main, cin.anim) / 12288.0f;
    cin.state = 1; cin.timer = 0.5f; fade_out(0.4f); music_fade_out(0.45f);
}
bool cin_running(void) { return cin.state == 2 || cin.state == 3; }   // -> sla player_update over, geen HUD, geen pauze

void cin_update(float dt) {
    switch (cin.state) {
    case 1: if ((cin.timer -= dt) > 0) break;
        fade_in(0.5f); music_play_stream(rtc_track[cin.track]);
        vm_setvar(cin.var, 0);
        vec3 P0, P1; inst_typecode5_points(cin.vecInst, &P0, &P1);
        Instance *m = cin.main;
        m->pos = P0; vec3 d = norm_xz(sub(P0, P1));                   // d = -(P1-P0)
        m->rot = rows(cross(d, UP), d, UP);
        inst_play_once(m, cin.anim, /*speed*/3.0f, now);              // slot1..3 = -1, klemt op het eind
        for (each actor a) { inst_play_once(a.inst, a.anim, 3.0f, now); inst_recell(a.inst); }   // recell = zichtbaar maken
        cam.mode = 0x80; cam.animInst = m; cam.letterbox = 1; cam.cut = 1;
        cin.state = 2; break;
    case 2: if ((cin.remain -= dt) > 0.5f) break;
        cin.timer = cin.remain; fade_out(cin.remain); cin.state = 3; break;
    case 3: if ((cin.timer -= dt) > 0) break;
        fade_in(0.5f); cin.timer = 0.5f; cin.state = 4; music_fade_in(0.45f);
        Mat C = mul(inverse(root_node(m, 0, 0.0f)), mul(root_node(m, cin.anim, 1.0f), inst_matrix(m)));
        player_reset(); player_set_facing(norm_xz(C.row2)); player_set_pos(C.t);
        cam.mode = 1 /*volgcamera achter de speler, cut*/; cam.letterbox = 0; break;
    case 4: if ((cin.timer -= dt) <= 0) memset-achtige reset (main = 0, nActors = 0, track = -1, state = 0); break;
    }
}
void cam_update_mode80(void) {                                        // na de instantieklok van animInst
    Instance *I = cam.animInst; int a = I->slot[0];
    float frame = nframes(I, a) * (I->pos_ac / (duration(I, a) / 4096.0f));
    vec3 eye = xform(I, pos_track_cut(node_flags(I, 0x80),  a, frame));   // §4.1; xform = p*diag(scale)*rot + pos
    vec3 tgt = xform(I, pos_track_cut(node_flags(I, 0x180), a, frame));
    look_at(eye, tgt, UP);  vfov = 68.0f /* letterbox */;
}
```

## 10. Onzeker

1. `music->vt[0x98]` (`0x46cc20`) zet `t0` alleen op 0 en geeft altijd 1; de lus en `var = t0·100` wijzen op een
   (console-?)variant waarin het openen van de stream tijd kost. In deze PC-build is `var` altijd 0. Niet live geverifieerd.
2. `0x44edb0`: de tussenstap `p = C.t · inverse(A, schaal)` gevolgd door `p · A + A.t` (`0x44ee42..0x44efb2`) is niet
   cijfer-voor-cijfer uitgeschreven; aangenomen dat dit netto `C.t` oplevert (evt. met schaalcorrectie van de instantie).
   Idem voor de overeenkomstige stap in `0x44e290`. Bij Woody is B (wortel in rustpose) vermoedelijk alleen een y-offset.
3. `0x44e690` (hoofdlus `0x401d16`, alleen in **House**, `App+0x68 == 0`): houdt Woody in menu-pose (actie 0x49, bericht
   1141) en zet het transparantie-doel van de Perso (`+0xfc` -> `+0x6c`, INSTANCE.md par. 5; 0 = dekkend) op **0 tijdens de
   intro-cinematic en 1.0 (onzichtbaar) daarbuiten**, snelheid 10000/s = direct. Of de menu-pose zelf zichtbaar is
   (wie `+0xfc` dan weer op 0 zet: `0x44d7e7`, `0x44d960`, `0x44b4bd`) is niet uitgezocht.
4. (opgelost) De hoofdinstantie is in alle gecontroleerde levels de **Perso-instantie zelf** (W1B `1200 [8, 1]`, W2D `[3, 1]`,
   K1A `[291, 2]`, K1R `[124, 18]` = telkens het 1131-argument). Niet elk level is nagelopen.
5. Volgorde klok ↔ camera binnen één frame (kan 1 frame vertraging geven) is niet nagegaan.
6. Tekenrichting: 1130 oriënteert de hoofdinstantie met `d = −(P1 − P0)` in rij 1, `0x44dda0` zet de Perso-facing op
   `+(P1 − P0)`; aangenomen dat de Perso-instantiematrix rij 1 = −facing gebruikt (consistent), niet geverifieerd.
7. 1080: exacte pixelmaten/regelafstand (`0x441980`, `0x441a50`) en de betekenis van `0x20001` als "lege regel" zijn afgeleid.
8. De tabel in §6: welke pose 72..78 precies is (juichen, menu-pose, …) volgt uit de aanroepers, niet uit de animatiedata.
