# PERSO_DEATH: teleport (bericht 26/30), deur-acties 17/18, doodsanimaties en -effecten, hit, knipperen, vijand-sterretjes

Statische analyse van `game/Woody.exe` (image base 0x400000). Alle adressen zijn VA's. Naamgeving volgt PERSO_MOVE.md
(`P` = Perso, `J` = springer `P+0x334`, `M` = Mover `P+0x388`, animcontroller `P+0x494`, tweede controller `P+0x498`).
`L(n)` = `0x436b90(n, 0)` = `model.anim[rec[n].sub[0]].duration · (1/4096) / rec[n].speed` (`0x4aa138` = 1/4096).
Effectpool, sprite- en lijnprimitief: zie PROJECTILES.md §5 (`[0x5e823c]+0xdb8`, 2000 × 80 B; `+0` leeftijd, `+4` levensduur,
`+0x4c` callback; sprite `S = [0x5e823c]+0xb00`, `0x470f10(S, vlaggen)`; beeldref `0x1000N` = bank 0 beeld N).

**Correcties op eerdere docs**
* `0x478980` is **geen camera-schok** maar een **strip-tekstballon** boven de instantie (§4.1). Geldt ook voor de harde landing
  in PERSO_JUMP §4 (`0x478980(p,1,2.0,180,50,0)`).
* `0x478660` (soort 7) is een **waterplons** (druppels + kringen), het model wordt niet verborgen (§4.3).
* `0x477e40` (soort 2/9) is het **skelet-effect** (model aan/uit, 6 skelet-sprites, flikkerlicht) (§4.2).
* `0x477610` (vijand dood) zijn geen wegvliegende deeltjes maar **5 sterretjes die boven het hoofd cirkelen** (§7).
* `0x44cf50`: PERSO_MOVE §4.4 heeft de twee takken omgedraaid (§6).
* Dode code: alle tests `P+0x21c == 6` in `0x464790` kunnen nooit waar zijn (toestand is dan 2).

Spritevlaggen `0x470f10` (aanvulling op PROJECTILES §5): bit 0 (1) = billboard naar de camera (`0x4714ea`), zonder bit 0 en
zonder 0x20 ligt de quad in het vlak met normaal `S+0x230..0x238` (`0x4717d7`); bit 1 (2) = eigen kleur/alpha
`S+0x214..0x220` gebruiken (`0x4710bd`, anders standaardkleur); bit 2 (4) = rotatie `S+0x224`; bit 3 (8) = niet-additief
(`0x4719b7`); bit 6 (0x40) = spiegelvlaggen `S+0x22c` toepassen (`0x4714b4` → `0x470d80`; waarde 2 = gespiegeld).

---

## 1. Bericht 26 (teleport) en 30 — Perso-handler `0x44cda0`

Berichtrecord: `+0` id, `+8` arg0, `+0xc` arg1, `+0x10` arg2. Alleen 26 (`0x44ce11`) en 30 (`0x44cde9`) worden hier
behandeld, de rest gaat naar de basis `0x40bf20`. Beide geven 0 terug.

### 1.1 Bericht 26 `[arg0 (ongebruikt), doelInst, mode]` (`0x44ce11..0x44cf24`)

```c
Instance *t = level->inst[arg1 & 0xffffff];            /* [0x50944c]+0x6c */
int mode   = arg2 & 0xffffff;
t->vtbl[2](1);                                          /* 0x44ce34: pose/matrices van het doel bijwerken */
vec3 pos = t->pos;                                      /* inst+0xc..0x14, NIET P0 van de vector */
if (mode == 1) { /* alleen positie */ }
else if (mode == 2) {                                   /* positie + kijkrichting */
    vec3 v[2];
    if (Inst_GetMarker(t, 5, v, 0) || Inst_GetMarker(t, 0, v, 0)) {   /* 0x42f6b0: typecode 5, anders typecode 0 */
        vec3 d = v[1] - v[0];                           /* NIET genormaliseerd, y inbegrepen */
        Mover_SetFacing(&P->M, &d);                     /* 0x459ff0 (0x44cefc) */
    }
} else { warn("Unknow teleportation mode !"); return 0; }   /* 0x44ce5e: verder gebeurt er niets */
Volumes_LeaveAll(P->id /*+4*/);                         /* 0x443ff0: voor elk volume waar P in zit leave-event (0x444550 + 0x443d20) */
Perso_Teleport(P, &pos);                                /* 0x44a650, zie onder */
CamFollow_Reset([0x5e5a74]);                            /* 0x458f90, zie onder */
```

Scripts gebruiken alleen `26 [0, andereDeur, 1]` (mode 1): de Perso komt op de **instantiepositie** van de andere deur; de
daaropvolgende `1040 [andereDeur, 18]` zet hem daarna op P0 van de deurvector (§2).

**`0x44a650(P, &pos)`**: als `P+0x21c == 5` (gescripte actie loopt) ⇒ **niets** (volumes-leave en camera-reset zijn dan wel
al/nog gedaan). Anders: `P.pos (+0x1f4) = pos`; `0x462990(P)` (grond-snap); `animctl->vtbl[4]()` (Reset, huidig = −1) en
idem voor `P+0x498` als die bestaat. Geen wijziging van health, checkpoint/spawn, onkwetsbaarheid of toestand.

**`0x462990(P)` grond-snap**: `0x44bf10(P, 0)`; `pos.y += P+0x110 (43)`; `0x435650(&pos, P+0x200, 1)` (GetHeight) ⇒
`pos.y = [0x53a568]`; `onGround (+0x22c) = 1`; `+0x588 = 0`; `+0x580 = 1.0`; `0x462c90(J)` (springer reset);
`0x44bd00(P)` (matrix/oriëntatie); `P+0x200 = 0x428ce0(level, pos + (0,43,0))` (wereldcel).

**`0x458f90(F)` camera-reset** (`F = [0x5e5a74]`: `F+0` CamMgr, `F+4` Perso): `0x41df70(CamMgr)` (overgang/letterbox/
rail-toestand wissen: `+0x108..0x10a = 0`, `+0x690 = 0`, `+0x138 = −1`, `+0x134 = 0`, `+0x66c &= ~4`); volgblok
`CamMgr+0x338`: `+0x18 = P->vtbl[34]()` (positie-ptr), `+0x28 = −1`, `+0x1c..0x24 = M-kijkrichting` (`0x445780`),
`+0x74 = 1`, `+0x30 = 0`; `0x41f9f0(2)` (**harde cut**); `0x41f410(0, 0)` (volgcamera); `+0x70 &= ~1`;
`F+0x18 = 0`, `F+8 = P->state`, `F+0x20 = 0`. Dus: camera springt direct achter de nieuwe positie/kijkrichting.

### 1.2 Bericht 30 `[_, cs]` (`0x44cde9`) — geen teleport
`0x44cce0(P, arg1 · 0.01 (0x4aa0ac), 0)` = LockMove: `P+0x238 = max(P+0x238, t)` (geen invoer/verplaatsing, PERSO_JUMP §0)
en `animctl->vtbl[2](1)` (verzoek idle, logisch record 1, prio 6500). "Sta t seconden stil."

### 1.3 `0x42f6b0(inst, typecode, vec3 *out, n)` — marker-vector van een instantie
Loopt over de **marker-lijst** van het model (`S+0x50` aantal, `S+0x54` node-indices, 1-based; node-soort 0x20, FORMAT_INS),
neemt de n-de node met `(node.flags >> 8) == typecode` (`0x42f6ed`), roept `inst->vtbl[2](1)` en transformeert de
`node+0x14` punten (eerste punt `node+0x18`, puntarray `S+0x20`, 0x28 B/punt) met de wereldmatrix van die node
(`[0x509adc]+0xa0`, index `node + inst+0x5c − 1`, 0x30 B) naar `out[i]`. Geeft 0 als er geen is (out onaangeroerd).
De "deurvector" = **marker-node met type_code 5**: `P0 = out[0]` (markerbegin), `P1 = out[1]` (markereind).

---

## 2. Acties 17/18 in `0x44dda0` en toestand 5 `0x44db50`

Bericht 1040 `[inst, actie]` (`0x445230`): `0x42f6b0(inst, 5, v, 0)` (**zonder** fallback naar typecode 0 en zonder test op
het resultaat), dan `0x44dda0(P, actie, v, 0)`. (Variant `0x4451e4` = 1043 met doelinstantie als 3e argument.)

Start (`0x44ddd8..0x44dfd6`), aanvulling op CINEMATIC §6:
* geweigerd (return 0) als `state == 2`.
* `fadeOut (+0x560) = (actie == 17)`, `fadeIn (+0x561) = (actie == 18)`, behalve als subtype `(P+0x104 & 0x3e0)` 0x80/0xa0 is
  (race-personages 4/5) (`0x44dde7..0x44de19`).
* 17/18: eerst `0x443ff0(P->id)` (alle volumes verlaten, `0x44de36`). `+0x4ec = 0` (**de vlag van het zij-aanzicht**,
  CAMERA_SCRIPT §4.2: de speler is niet langer aan het vlak gebonden), `0x44dd70` (aanvalsdoel wissen).
* `logisch = 0x463e30(actie)`: 17 → record **24** `(17,−1,−1,−1)`, 18 → record **25** `(18,−1,−1,−1)`, beide prio 6000,
  speed 3.0, restart 1. `total = remain (+0x538/+0x53c) = L(logisch)` = 18432/4096/3 = **1.5 s** voor beide.
  `animctl->vtbl[4]()`, `vtbl[2](logisch)`; `+0x540 = actie`; toestand 5.
* `P.pos = P0`; `onGround = 1`; `d = (P1.x−P0.x, 0, P1.z−P0.z)` genormaliseerd (alleen als lengte > 0);
  `0x459ff0(M, &d)`; `+0x550 = 0`; `0x44bd00(P)`. **Geen grond-snap bij de start**: y = P0.y.
* 17/18 hebben geen cameratrack ⇒ camera blijft de volgcamera.

Per frame (`0x44dbdf..0x44dcf3`, tak `17 ≤ actie ≤ 18`):
```c
if (remain < 0.6f /*0x4a9650*/ && P->fadeOut) { P->fadeOut = 0; App_FadeOut(app, 0.5f); }   /* 0x401480 */
if (P->fadeIn)                               { P->fadeIn  = 0; App_FadeIn (app, 0.5f); }   /* 0x401440, eerste frame */
remain -= dt;
if (remain <= 0) {
    Perso_SetState(P, 0);
    if (actie == 18) {
        f = Mover_GetFacing(M);  f = -f;  Mover_SetFacing(M, &f);        /* 0x445780, ×−1 (0x4a9500), 0x459ff0 */
        Perso_GroundSnap(P);                                              /* 0x462990 */
        animctl->vtbl[2](1);                                              /* idle */
        if (!P->planeMode /*+0x4ec*/) Perso_EndScripted(P);              /* 0x44e5a0, zie onder */
    }                                                                     /* na 17: alleen toestand 0, verder niets */
}
```
* **De Perso-positie verandert niet tijdens 17/18** (`0x44e290` wordt in deze tak niet aangeroepen, `+0x550` blijft 0).
  De zichtbare verplaatsing zit volledig in de positietrack van node 0 van het Woody-model (modelruimte, y = diepte):
  anim 17: `(0,−3,52) → (−3.7,−264,66)` (loopt ±261 de deur in); anim 18: `(−0.25,−454,60) → (0,−3,52)` met de wortel
  180° gedraaid (start-quat ≈ (0.13,−0.43,−0.89,−0.01), eind = idle-quat 180° om de verticale as). Daarom staat de Perso
  bij 18 op P0 **met het gezicht naar de deur (P0→P1)**, het model begint ±450 "in" de deur, loopt achterstevoren-gedraaid
  naar P0, en pas op het laatste frame wordt `facing = −facing` gezet zodat idle naadloos aansluit.
* Tijdlijn 17: t = 0 start, **t = 0.9 s fade-out (0.5 s)**, zwart op 1.4 s, einde 1.5 s ⇒ toestand 0. De one-shot 17 blijft op
  het laatste frame staan (prio 6000, geen lus ⇒ idle kan hem niet vervangen) tot bericht 26 via `0x44a650` de controller
  reset. Tijdlijn 18: t = 0 **fade-in (0.5 s)**, einde 1.5 s.
* Volgorde in de scripts: `1040 [deur,17]` → `DELAY 150` → `26 [0, andereDeur, 1]` (toestand is dan 0, dus niet geweigerd) →
  `1040 [andereDeur,18]`. De DELAY is even lang als de animatie (1,5 s), dus of de teleport nog in toestand 5 valt hangt
  van de framegrens af; valt hij erin, dan verplaatst pas actie 18 de speler (naar P0) en cut de camera bij de oude positie.

### 2.1 De camera door de hele deursequentie

De enige twee camera-acties zitten in bericht 26 en aan het **eind** van actie 18 — actie 17/18 zelf laten de camera met rust:

| moment | aanroep | effect |
|---|---|---|
| bericht 26 (teleport) | `0x458f90` (§1.1), **buiten** de toestand-5-test | harde cut: `0x41f9f0(2)` + `SetMode(0, 0)` → `0x41e450` → `0x4247f0` zet de volgcamera **op dat moment** neer: `P = pos − look + (0,100,0)`, dan 1 stap van 0,1 s en 100 van 0,04 s in behind-mode |
| eind actie 18, na `facing = −facing` | `0x44e5a0`, alleen als `+0x4ec == 0` | `0x41f9d0(0.5)` + `0x41f9f0(1)` + `SetMode(0, 0)`: volgcamera achter de **nieuwe** kijkrichting, met een travelling van 0,5 s |

`0x44e5a0` zelf: `p->targetPtr (+0x18) = &P.pos`, `p+0x28 = −1`, `p->dir (+0x1c) = M-kijkrichting`, `p+0x30 = 0`
(de `pos.y += 43` / `−= 43` eromheen is een no-op), daarna de drie CamMgr-aanroepen hierboven.

Zonder die laatste reset staat de camera na het uitstappen **vóór** de speler: de camera stond achter de kijkrichting
"de deur in", en die draait in het laatste frame 180°. De `+0x4ec`-test is er voor deuren die in een zij-aanzicht-stuk
uitkomen: het script zet dat stuk (bericht 1088) in hetzelfde frame weer aan, en dan blijft mode 0x20 staan.

**Volgorde binnen één scripttick, en waar de port afwijkt.** Het script stuurt 26 en 1040/18 achter elkaar
(`out/w1a_code.txt` 2297..2313), zonder DELAY ertussen. Bericht 26 draagt in de deurscripts modus 1: alleen een
positie, **geen richting**; en `0x44a650` verplaatst niets zolang toestand 5 nog loopt (de `DELAY 150` van het script
is even lang als animatie 17, dus dat gebeurt geregeld). Pas actie 18 legt vast waar hij staat en welke kant hij op
kijkt (`pos = P0` van de deurvector, `0x44dec4`). Zet je de volgcamera zoals `0x4247f0` **tijdens** bericht 26 neer, dan
staat hij dus achter de kijkrichting van de *vorige* deur — bij de W1A-deur 336 → 334 scheelt dat 135° en kijkt de
speler anderhalve seconde lang tegen een muur aan. De port doet daarom de **modewissel** wel op zijn plaats in de
berichtenstroom (cut + `SetMode(0, 0)` in de handler van bericht 26) maar laat de volgcamera zichzelf pas neerzetten in
de camera-update **na** de scripttick: `SetMode(0, 0)` wist alleen `cam_init`, en `player_camera` zet hem dan neer op de
positie en kijkrichting die actie 18 hem geeft. Het scherm is op dat moment zwart (fade-out klaar aan het eind van 17,
fade-in 0,5 s in 18), dus er is niets van te zien; het verschil met het origineel is één frame aan het begin van actie 18.

**De modewissel zelf uitstellen mag niet.** Alles wat de rest van diezelfde tick met de camera doet moet winnen, en dat
is precies wat er ná bericht 26 komt: `1040 [deur, 18]` zet mode 0x80 op de cameratrack van de animatie
(CAMERA_SCRIPT §4.3) en een gebiedspoort in een hub stuurt `26 [0, marker, 2]; 580 [cam, 2]; 520 [cam, 0, doel]` — de
vaste camera die de poort laat opengaan (CAMERA_SCRIPT §1.3, WWS-object 258). Een cut die een frame later alsnog
`SetMode(0, 0)` doet, haalt allebei onderuit: het poortfilmpje sloeg over (de speler kreeg meteen weer de volgcamera en
de besturing terug) en de deurcamera stond er maar één frame.
(Niet dynamisch geverifieerd: of het origineel op die 135° hetzelfde beeld geeft — behind-mode staat in toestand 5 uit
(`CAMERA.md` §3, `p+0x74`), dus de camera hercentreert daar ook niet vanzelf.)

---

## 3. Dood: `Kill(soort)` `0x44c110` (tabel `0x44c498`) en animatiekeuze `0x464790` (tabel `0x4649dc`)

Toestand 2 → `0x463e8f`: `P+0x4d8 ? 0x464a00 : 0x464790`. `+0x4d8` is alleen 1 in de race-variant `0x44c4c0` (§3.3).
Kill zet altijd (`0x44c443`): `health = 0`, **`+0x274 = 0` (doodsklok T)**, `+0x26c = soort`, `+0x4d8 = 0`, `+0x550 = 0`,
toestand 2, `+0x268 = 1`. `0x464790` doet elk frame eerst `T += dt` (`0x464796`).
Fade van het scherm: `0x4459c0` toestand 3 begint de fade-out (1.0 s) op `T ≥ P+0x288 − 1.0` (PERSO_FRAME §4.1).
Het model wordt **nergens verborgen of vervaagd**, behalve het aan/uit-flitsen bij soort 2/9 (§4.2).

Animcontroller-regel (`0x436a50`): een verzoek vervangt de huidige als `prio_nieuw ≥ prio_huidig` of als de huidige in
een lus zit (`inst+0xc0 == 1`, slot0 == slot1). Keten-index −1 = leeg ⇒ **one-shot blijft op het laatste frame staan**
(INSTANCE.md §2); te grote index ⇒ 0 (`0x436b34`, signed vergelijking, −1 blijft −1).

### 3.1 Per soort

| soort | Kill-tak | `+0x288` | Kill-bijzonderheden | animatie (`0x464790`) |
|---|---|---|---|---|
| 1 (script 1020, afgrond) | `0x44c297` | 3.5 | tekstballon `0x478980(P, 0, 2.5, 180, 50, 0)`; `+0x240 = L(0x2f)` = **2.267 s geen zwaartekracht**; `0x463170(J,0)` | `0x464915`: `T ≤ L(0x2f)` ⇒ verzoek **0x2f** elk frame; daarna verzoek **9** (lange val); in het ene frame waar `T−dt ≤ L < T`: camera `0x41fb50(CamMgr, &(inst.pos + (0,100,0)), P)` (§4.4) |
| 2, 9 (bliksem/laser) | `0x44c3ab` | 1.5 | rumble `0x44d1b0`; `+0x288` wordt al vóór de tests gezet; cheat `[0x5d7b8a]` of `+0x270 > 0` ⇒ genegeerd; `0x477e40(&(pos + (0, P+0x11c, 0)))` (argument ongebruikt, §4.2); `+0x240 = L(0x30)` = 1.7 s; **geen** `0x463170` | `0x464903`: verzoek **0x30** elk frame |
| 3 (health op), 8 (val) | `0x44c230` | 3.0 | cheat `[0x5d7b8b]` of `+0x270 > 0` ⇒ genegeerd; `0x463170(J,0)` | 3: `0x4647c7` (§3.2); 8: `0x4648df` verzoek **0x2e** |
| 4, 5 | `0x44c26f` | 3.5 | `+0x270 > 0` ⇒ genegeerd; `0x463170(J,0)` | `0x4647c7` (§3.2) |
| 6 (explosie) | `0x44c2d6` | 2.5 | idem | `0x4648f1`: verzoek **0x2b** |
| 7 (water) | `0x44c308` | **0** | plons `0x478660(&(inst.pos + (0,110,0)), snelheid, 50.0)` met snelheid = `0x44d170(P)` = `|P+0x204..0x20c| / dt`; camera `0x459030(F, &CamMgr.state.pos)` = `0x41fb50(CamMgr, huidige camerapositie, P)` (camera blijft staan en kijkt de speler na); `+0x5f0 = +0x5b4 = 0`, `+0x5cd = 0`, `animctl->vtbl[4]()`, `0x462c90(J)`, colliders `0x436d10` ×2 | `0x4649bf`: verzoek **0x2a** en **elk frame `0x462c90(J)`** ⇒ valt niet verder |

Soort 7 mag elke andere lopende dood overschrijven, soort 1 elke andere behalve 1 (`0x44c1e6..0x44c20d`).
In toestand 2 loopt `0x44bb20(P, 0)` door: geen invoer, knockback loopt uit, J laat hem vallen zodra `+0x240 ≤ 0`.

### 3.2 Soort 3/4/5 (`0x4647c7`): grond- of luchtvariant, daarna liggen
```c
if (T <= dt) {                                   /* eerste frame (0x4647c7) */
    P->deadOnGround /*+0x284*/ = P->onGround;    /* 0x44bcf0 */
    if (onGround) Request(P->duck694 == 2 ? 0x2c : 0x26);      /* (dode code: state 6 → 0x28) */
    else          Request(0x25);                                /* (dode code: state 6 → 0x27) */
} else if (P->deadOnGround) {
    if (P->duck694 != 2 && T >= L(0x26) /*1.067*/) Request(0x29);
} else {
    if (T >= L(0x25) /*0.733*/) Request(0x29);   /* ongeacht of hij al geland is */
}
```
Tussen het eerste frame en `L` wordt niets aangevraagd (de keten loopt door naar de lus-anim 30). 0x29 is one-shot ⇒ blijft liggen.

### 3.3 Gebruikte logische records (`0x4b6180`, 0x1c B/record; `{sub[4], prio, speed, restart}`; duur = dur/4096/speed)

| log. | .ins-keten | prio | speed | duur sub[0] | gebruik |
|---|---|---|---|---|---|
| 9 | 7 → 32 → 33 lus | 1600 | 3 | 0.033 s | soort 1 na het hangen (kan 0x2f alleen vervangen omdat die dan in lus 33 zit) |
| 0x1f | 20 → 0 | 5110 | 3 | 0.467 | hit op de grond |
| 0x20 | 22 → 0 | 5110 | 3 | 0.400 | hit in de lucht |
| 0x23 | 26 → 25 lus | 5110 | 3 | 0.267 | hit tijdens bukken |
| 0x21 / 0x22 / 0x24 | 68 → 47 / 67 → 47 / 64 → 60 | 5110 | 3 | 0.467 / 0.400 / 0.267 | hit in toestand 6 (dragen): grond / lucht / gebukt |
| 0x25 | 29 → 30 lus | 6000 | 3 | 0.733 | dood 3/4/5 in de lucht |
| 0x26 | 28 → 30 lus | 6000 | 3 | 1.067 | dood 3/4/5 op de grond |
| 0x29 | 31, one-shot | 6000 | 3 | 0.400 | daarna: gaan liggen, blijft staan |
| 0x2a | 27, one-shot | 6000 | 3 | 0.833 | soort 7 (wortel zakt ±190 in de anim zelf) |
| 0x2b | 37, one-shot | 6000 | 3 | 1.867 | soort 6 |
| 0x2c | 36, one-shot | 6000 | 3 | 1.700 | dood 3/4/5 gebukt op de grond |
| 0x2e | 35, one-shot | 6000 | 3 | 2.367 | soort 8 |
| 0x2f | 48 → 33 lus | 6000 | 3 | 2.267 | soort 1: in de lucht hangen/spartelen |
| 0x30 | 85, one-shot | 6000 | 3 | 1.700 | soort 2/9 |
| (0x27, 0x28, 0x4d) | 69→30, 70→30, 65→33 | 6000 | 3 | | alleen via dode `state == 6`-takken |

Alle records restart = 1 behalve 9 (0).

Race-variant (`0x44c4c0` → `+0x4d8 = 1`, `0x464a00`, tabel `0x464b48`, verzoeken op **beide** controllers): soort 1 → 0x75 (A)
en 0x76 (B) + dezelfde camera-actie bij `T` over `L(0x75)`; 2 → 0x72; 3 → 0x74; 4/5 → niets; 6 → 0x73; 7 → 0x77 + `0x462c90(J)`;
8 → 0x71. Kill-kant: `+0x240 = L(0x72)` resp. `L(0x75)`, soort 7 `+0x288 = 4.0`. Niet verder uitgewerkt.

### 3.4 Respawn `0x445930` — checkpoint, kijkrichting, zij-aanzicht en camera

Aangeroepen uit toestand 0 van `0x4459c0` (PERSO_FRAME §4.1), 0,25 s na het leven-eraf (`0x44c730`):
```c
Fader(Game+4, 0, 0, 0.1);  Game->state = 0;  Game->timer = 0.1;  [0x4b3354] = 0.2f;
Perso_Respawn(P, 0);                    /* 0x44a810 */
Actors_ResetAll();                      /* 0x40c040: vtbl[28] van alle actoren */
CamFollow_Reset(Game+8);                /* 0x458f90 (§1.1): 0x41df70, 0x41f9f0(2) harde cut, SetMode(0, 0) = volgcamera */
```
`0x44a810(P, save)`: als `P+0x250` (levens) 0 is ⇒ game over (`0x404e10`, of `0x44a6a0` = levelstart als
`[0x5e5814]+0x384 & 4`). Anders (met `save` = 1 eerst `SavePos.bin` lezen, hier 0): **`P.pos = P+0x318`**, `P->vtbl[17]()`
= Reset `0x44ab20`, grond-snap `0x462990`. Reset zet o.a. **`0x459ff0(M, P+0x324)`** (kijkrichting van het checkpoint),
health 3 als die op was, `+0x270 = 1.0` (onkwetsbaar), en **`+0x4ec = 0` (`0x44ad22`)**: het vlak-slot van het
zij-aanzicht (CAMERA_SCRIPT §4.2) houdt op bij de dood. Samen met de volgcamera van `0x458f90` betekent dat: na een dood in
een zij-aanzicht-stuk staat Woody **gewoon in 3D op het laatste checkpoint, met de camera achter zich**. Er is geen apart
"zij-aanzicht-checkpoint"; het script zet het zij-aanzicht pas weer aan (1088) als hij opnieuw door de deur van dat stuk gaat.

Het checkpoint zelf (`1030 SaveAuto`, `0x445129` → `0x44aa10`): `+0x318 = inst.pos`; `+0x324` = `(P1.x − P0.x, 0, P1.z − P0.z)`
van de marker met typecode 0 (`0x42f6b0(inst, 0, v, 0)`), genormaliseerd; zonder marker de huidige kijkrichting (`0x445780`);
`+0x330 = 1`, `+0x4e0 = +0x264`. Alle checkpoint-instanties van W1B (0x196, 0x200..0x20b, 0x238) hebben zo'n marker.

**W1B, issue #39.** Het zij-aanzicht-stuk achter deur 392 (→ 409, vlak 411, `1088 [411, 2]`) heeft geen eigen checkpoint;
het laatste is 0x206 op (−8009, 430, −16808), 575 eenheden vóór deur 392, marker naar de deur (−x). De port hield het
vlak-slot (`g_cam.plane_on`) aan na de respawn, dus werd die positie elk frame op het vlak x = −11231 geprojecteerd: naar
(−11231, 140, −16808), in de leegte naast het stuk, waar hij viel, in het water belandde (soort 7) en opnieuw doodging —
zonder einde. De port doet nu wat `0x44ad22` + `0x458f90` doen (`respawn_req` → `plane_release()` + harde cut naar de
volgcamera, vóór de vlak-projectie in hetzelfde frame) en neemt de kijkrichting van het checkpoint uit de marker over.

---

## 4. Effecten

### 4.1 Tekstballon `0x478980(inst, soort, duur, offY, offX, u8 *levend)` (callback `0x4786f0`)
Record: `+4 = duur`, `+8 = inst`, `+0x10 = offY (180)`, `+0xc = offX (50)`, `+0x28 = levend-ptr`, `+0x14.. = beelden`,
`+0x24 = aantal`, `+0x2c = kant`. Beelden per soort (`0x478a8c`): 0 → {0x2d}; 1 → {0x33, 0x34}; 2 → {0x2e}; 3 → {0x32};
4 → {0x2f, 0x30, 0x31, 0} (aantal 4). Soort 0 = dood 1, soort 1 = harde landing.
`kant = (view.row0 · inst.T + view.row0.w ≥ 0)` (`0x4789e3..0x478a1d`; view = `[[0x509adc]+8]+0x30`, inst.T = `inst+0x60..0x68`), één keer bij het aanmaken.
```c
u = age / duur;
if (levend ? *levend == 0 : u >= 1) einde;
s = levend ? (age < 0.5 ? 2*age : 1)
           : (u < 0.04 ? 25*u : u > 0.96 ? 25*(1-u) : 1);                 /* 0x4aa1cc, 0x4abd58, 0x4abd5c */
d = normalize_xz(cam.pos - inst.T);  R = normalize(d.z, 0, -d.x);  U = (0,1,0);   /* 0x46d320 */
size = 90*s + 30;  h = size/2;
X = kant ? -(offX + h) : (offX + h);   Y = offY + h;
pos = inst.T + R*X + U*Y;
sprite(img 0x2c, pos, size, mode 0x12, flags 0x49, mirror = kant ? 2 : 0);        /* ballon, niet-additief, standaardkleur */
img = beelden[(int)(u * 8) % aantal];
if (img) sprite(img, pos, size = 55*s + 10, flags 0x49, mirror 0);               /* inhoud */
```
Nagelopen tegen de disassembly (2026-09-24): constanten 0.04/0.96/25/90/30/0.5/55/10/8 kloppen, `0x46d320` geeft voor `d = (dx,0,dz)` `R = (dz, 0, −dx)` (= schermrechts) en `U = (0,1,0)`; spiegelwaarde 2 (`0x470d80` geval 2) wisselt u. `size` is `S+0x264`, de halve diagonaal. Camera-ruimte x ≥ 0 = rechterhelft van het scherm ⇒ ballon links van de instantie, gespiegeld, staart naar hem toe. Beelden (bank 0): 0x2c ballon, 0x2d "?!", 0x2e "$", 0x2f..0x31 z/zz/zzz, 0x32 "...", 0x33/0x34 zwarte/rode vloek. Aanroepers: Kill(1) `0x44c2a9` (soort 0), race-Kill(1) `0x44c5d8`, harde landing `0x464470` (soort 1, 2.0 s), slapen `0x464601` (soort 4, 2.5 s, 130/50, levend = `P+0x52c`), bericht 1500 `0x46ccf6` `[inst, soort, duur·100, offY, offX]` (alleen K2R en S2R). Port: `game_bubble` / `bubbles_draw` in main_engine.c, `hud_world_bubble` in hud.c; test `WOODY_KILLAT=2` (dood), 12 s stilstaan (zzz), `WOODY_POSAT="1 537 200 -2148"` in W1A (harde landing), log `WOODY_BUBLOG=1`.

### 4.2 Skelet-effect `0x477e40(&pos)` (callback `0x477980`) — soort 2/9
Aanmaken: levensduur **1.5 s**; `+8 = &speler.pos (inst+0xc)`; `+0x10 =` oude `speler+0x6c`; `+0x14 =` beeldtabel
`0x4b7e30` (karaktertype 1 = Woody) of `0x4b7e60`; het pos-argument wordt **niet gebruikt**. Zet ook de kijkrichting van
de speler: `0x459ff0(M, −(view+0x20..0x28))` = −view.row2 (`0x477efb..0x477f7b`) ⇒ speler draait naar de camera.

Per frame, `u = age/1.5`, fasen van 1/8:

| u | toestand | pose-rij | fliprij | rotatie hoofd |
|---|---|---|---|---|
| 0–0.125, 0.25–0.375 | **skelet A** | 2 | 12 | 40/512 omw. |
| 0.5–0.625, 0.75–0.875 | **skelet B** | 0 | 0 | 472/512 omw. (= −40) |
| overige achtsten | **model** | | | |
| > 1 | einde: `0x44e7f0(speler, oude +0x6c, 0)`, levensduur −1 | | | |

* skelet: `0x44e7f0(speler, 1.0, 0)` (doel-transparantie `+0xfc = 1`), model: `0x44e7f0(speler, 0, 0)`; altijd
  `speler+0x100 = 100.0` (fade-snelheid 100/s ⇒ binnen één frame) ⇒ **model onzichtbaar tijdens skelet-fasen**.
* basis: `d = normalize(cam.pos − speler.pos)` (3D), `R, U, d` uit `0x46d320`; `jit = rnd·20 − 10` (één per frame).
* 6 sprites `i = 0..5` op `speler.pos + R·o.x + U·o.y + d·o.z`, offsets `o` = `vec3 0x4b7d10[poseRij·6 + i]` (4 rijen × 6; code-index in floats: 0 resp. 0x24):
  rij 0 = (−5,180) (−60,100) (60,150) (0,96) (−40,41) (40,46); rij 2 = (10,180) (−60,150) (60,100) (0,96) (−40,41) (40,46) (z = 0).
  Grootte: i = 0 → 120 (met hoofdrotatie), i = 3 → 75 (mode 0x1a), overige 55; mode 0x12; rotatie 0 voor i ≠ 0.
  Beeld `T[i]` = Woody {0x25, 0x22, 0x22, 0x23, 0x24, 0x24} (anderen: hoofd 0x2a), spiegel = `0x4b7cb0[fliprij + i]`
  (rij 0: 0,1,2,0,0,2; rij 12: 2,0,3,0,0,2), **flags 0x4d** (niet-additief). Direct daarna dezelfde sprite met
  `size += jit`, beeld `T[6+i]` = {0x29, 0x26, 0x26, 0x27, 0x28, 0x28} (anderen: hoofd 0x2b), **flags 0x45** (additieve gloed).
* elk frame (ook in modelfasen) dynamisch licht `0x498790([0x4c4cac], 0, &S.pos (laatst getekende sprite), wit (255,255,255), straal 200 + rnd·100)`.

### 4.3 Waterplons `0x478660(&C, v, r)` (emitter-callback `0x478360`) — soort 7 (ook `0x46ce27`)
Emitter: levensduur **1.0 s**, `C` = centrum, `h = v · 0.001` (`0x4aa0f4`), `R = r + rnd·50` (dood: 50..100),
`accA = 0`, `accB = 0.2`. Per frame (`u = age`): `u ≥ 1` ⇒ einde.
```c
if (u < 0.3) { accA += dt; n = (int)(accA * 500); accA -= n * 0.002;  n × druppel(); }      /* 0x4aab98, 0x4a9998, 0x4abd54 */
accB += dt; n = (int)(accB * 5); accB -= n * 0.2;  n × kring();                             /* eerste kring direct */

druppel (0x4783e8, callback 0x477fa0): life = (rnd+1)*0.4;  a = (int)(rnd*511);  c = cos(a), s = sin(a)  (2π/512);
    p0 = C + ((rnd*20 + R)*c, 0, (rnd*20 + R)*s);   dir = (c, 0, s);   D = 150 + rnd*100;    /* +0x2c */
    per frame, w = age/life:  pt(w) = (p0.x + dir.x*D*w,  p0.y + sin(pi*w*255/256) * h*100,  p0.z + dir.z*D*w)
    lijn 0x471a10 van pt(w) naar (x,z op w+0.08; y op w+0.1), rgba1 = (.5,.5,.5,.65), alpha0 = 0, breedte 4,
        beeld 0x39, flags 0xe00 (additief);
    bij w >= 1: rimpel (callback 0x478290, 0.6 s) op pt(1): sprite beeld 3, horizontaal (flags 2, normaal (0,1,0)),
        rgb (.65,.65,.8), alpha = (1-w)*0.3, size = 25*w + 5.
kring (0x4785cc, callback 0x4781b0): life 0.7 s, op C, beeld 0x3a, horizontaal + rotatie (flags 6), rot = (int)(rnd*512),
    rgb (.65,.65,.8), alpha = (1-w)*0.3, size = 2*R + 600*w.
```
Druppelhoogte = `snelheid · 0.1` eenheden (val met 1500 u/s ⇒ 150).

### 4.4 Camera `0x41fb50(CamMgr, &pos, inst)` (ook `0x459030`)
`CamMgr+0x440..0x448 = pos`, `+0x44c = inst`, `0x41f9b0(100.0)` (overgangssnelheid 100 u/s, duur = afstand/snelheid),
`0x41f9f0(1)` (vloeiend), `+0x45c = +0x348`, `0x41f410(1, 0)` = **mode 2: vaste camera op `pos` die naar `inst` kijkt**
(CAMERA.md §mode 2). Soort 1: vanaf het punt waar Woody hing (+100) hem nakijken; soort 7: camera bevriest op zijn
huidige plek. De respawn zet de volgcamera terug met een harde cut (PERSO_FRAME §4.1).

---

## 5. Hit-animatie `0x464b70` (aangeroepen uit `Hit` `0x44ca00`, niet dood)
```c
if (state != 6)      Request(onGround ? (duck694 ? 0x23 : 0x1f) : 0x20);
else if (P->+0x594) { Request(onGround ? (duck694 ? 0x24 : 0x21) : 0x22);  P->+0x58c = 2; }
```
Eén verzoek (prio 5110, restart); de keten loopt zelf terug naar idle (anim 0) resp. buk-lus 25. **Geen LockMove**: `Hit`
zet juist `+0x238 = 0` (`0x44cce0(0,1)`). Besturing is alleen geblokkeerd tijdens de knockback-timer `M+0xec = 0.2 s`
(`0x44bb48`: `+0x474 > 0` ⇒ geen invoer; 500 u/s, uitlopen 0.5 s). Onkwetsbaar `+0x280 = 0.6 s`. Animatieduur 0.47 s (grond) /
0.40 s (lucht) heeft geen invloed op de besturing; prio 5110 > lopen/springen, dus hij speelt uit tenzij een prio ≥ 5110 komt.

## 6. Knipperen
* `+0x270` (respawn 1.0 s) en `+0x280` (na hit 0.6 s) worden **alleen** gelezen door `Hit`/`Kill` en afgeteld in `0x44b1b0`
  (alle lezers in de exe nagelopen): **er is geen zichtbaarheids- of kleurknipper bij gewone onkwetsbaarheid.**
* Wit knipperen bestaat alleen voor de onkwetsbaarheids-bonus: `0x44c890(P, t)` zet `+0x704 = +0x700 = t`, `+0x270 = max`,
  `+0x280 = max`. Aanroepers: bonusklasse-handler `0x44f9c8` (bericht 10, `t = arg1 · 0.01`) en debugtoets 0x30 in `0x402940` (10 s).
* `0x44cf50` (vt[26], render-kleur): `[0x5ac860] = 0`; `+0x704 ≤ 0` ⇒ `[0x5ac850] = 0` (normaal). Wit = `[0x5ac850] = 2`,
  kleur `[0x5ac854..c] = (255,255,255)`:
  * `+0x704 ≥ 5.0` (`0x4a9884`): `acc (+0x708) += dt`; `acc ≥ 0.5` ⇒ `acc −= 0.5` en wit; anders wit zolang `acc ≤ 0.1`
    ⇒ **0.1 s wit per 0.5 s**.
  * `+0x704 < 5.0` (bijna op): teller `+0x70c` ⇒ **om het andere frame wit**.

---

## 7. Vijand dood: sterretjes `0x477610(soort, enemy)` (callback `0x477350`)
`vtbl[57]` `0x41b000`: `rand() & 1 ? soort 0 : soort 1`. Vijf records `i = 1..5`, levensduur **2.5 s**, `+8 = i`,
`+0xc` = beeld (soort 0 → **0x0b**, 1 → **0x0a**, 2 → 0x08), `+0x10 = enemy`.
```c
if (enemy->poseFrame /*+0x58*/ != frameCounter /*[0x509adc][0]*/) { life = -1; return; }   /* vijand weg/niet meer gepost ⇒ einde */
u = age / 2.5;  if (u >= 1) return;
Inst_GetMarker(enemy, 0, v, 0);                   /* eerste marker met typecode 0: P0 = v[0], len = |v[1]-v[0]| */
ang = (i*512/5 + (int)(512*u)) mod 512;           /* 72° uit elkaar, 1 omwenteling per 2.5 s (0x4abd3c = −512) */
f = ang*6/256.0;  k = (int)f;  w = (k & 1) ? f-k : 1-(f-k);                                /* driehoeksgolf, 6 per omwenteling */
pos = (P0.x + 80*cos(ang),  P0.y + len + (4*w)*(4*w),  P0.z + 80*sin(ang));                /* 0x4ab134, 0x4a94c0 */
rot = (int)((w - 0.5) * 56);  size = 40;  mode 0x12;
a = u < 0.7 ? 1 : 1 - (u - 0.7)*3.33;                                                      /* 0x4aa1d8, 0x4abd30 */
if (img == 0x1000a)  sprite(img 5, pos + (0,15,0), rgb (1,1,.5), alpha 0.8*a, flags 3);    /* additieve gloed, billboard */
sprite(img, pos, rgb (.5,.5,.5), alpha a, rot, mirror 2, flags 0x4f);                      /* niet-additief, gespiegeld, geroteerd */
```
Geen snelheid/zwaartekracht: de sterren zijn elk frame een functie van de tijd en van de marker, en volgen dus de vijand.

Bevestigd uit ENEMY.md en de code: de sterf-animatie is logisch 13 = `(12,−1,−1,−1)` ⇒ one-shot, **blijft op het laatste frame**
(`0x436b34` laat −1 staan; INSTANCE.md §2: fase geklemd, snelheid 0); verwijderd na `AnimLen(13) + 1.0` s, fade `+0x6c` lineair
0 → 1 over de tweede helft (`0x41a47d`). Toestand 12 (types 4/5/6) resp. 10 (types 7..9) gebruikt gedrag **Stilstaan**, waarvan de
Tick de gewone `0x41b2c0` is ⇒ **de terugslag (600·t_rest, 0.25 s) en platform-meebewegen lopen door terwijl hij dood is**.
De sterren eindigen zodra de vijand uit de wereld is (dus na min(2.5, AnimLen+1.0) s).

---

## 8. Onzeker
1. Tekenrichting van `R` en van `kant` bij de tekstballon en van `−view.row2` bij het skelet-effect: formules zijn letterlijk;
   links/rechts hangt af van de view-matrixconventie (niet live geverifieerd).
2. `0x44bf10(P, 0)` in de grond-snap en `CamMgr+0x45c = +0x348` zijn niet uitgezocht.
3. Beeldinhoud van bank-0 beelden 0x22..0x34, 0x39, 0x3a is afgeleid uit het gebruik, niet bekeken.
4. `[0x5ac850] = 2`: hoe de renderer "wit" mengt (vervangen of optellen) is niet nagelopen.
