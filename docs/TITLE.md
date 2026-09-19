# TITLE.md — titelscherm: draaiende camera, menu-pose, pagina 0/1 (Woody.exe, build 17-10-2001)

Status: **statische analyse**; adressen = Woody.exe (imagebase 0x400000), na te lezen met
`python tools/drange.py START END`. Data: `extract/Data/House/House.ins` (`tools/insparse.py`), `House.rck`
(`tools/rckexport.py`), script `out/house_code.txt`. Aanvulling op GAMEFLOW.md §4.5/§5, CINEMATIC.md §4/§6,
CAMERA.md §2/§4 en HUD_TEXT.md §6. Onzekere punten zijn gemarkeerd met **onzeker**.

## 0. Samenvatting

- De titelcamera is **geen** aparte cameramode en **geen** `.ins`-camera of rail (House.ins heeft 0 camera's en 0
  trajectories). Het is **camera-mode 0x80 (index 7) = "camera uit de animatie van een instantie"**, en die instantie is
  **de Perso zelf**: de engine houdt Woody in House continu in de gescripte actie **0x49 = .ins-animatie 73**, en die
  animatie bevat een **cameratrack**: node 141 (flags 0x80, camera) beschrijft een gesloten cirkel, node 142 (flags 0x180,
  doel) staat stil. `0x44dda0` schakelt bij elke start van de actie zelf naar mode 0x80 (`0x44df7d..0x44dfad`), zonder
  letterbox, met harde cut.
- Baan (wereld): middelpunt **(−45.8, 3117.0, 166.7)**, straal **2500**, vaste hoogte y = 3117.0, **één omwenteling per
  10,0 s = 36°/s = 0,628 rad/s**, positieve draaiing om +y (tegen de klok in van bovenaf gezien; +x → −z → −x → +z).
  Kijkpunt vast op **(529.3, 3053.1, 528.7)** (dus 680 eenheden naast het middelpunt: de afstand camera–doel ademt tussen
  1842 en 3166). Geen botsing, geen smoothing, geen letterbox; vfov 83.97° (zoom 1.2, 4:3).
- Woody zelf is op het titelscherm **onzichtbaar**: `0x44e690` zet elke tick zijn transparantiedoel op 1.0 met snelheid
  10000/s (alleen tijdens de intro-cinematic 0 = zichtbaar). Animatie 73 is bovendien een stilstaande pose (alle
  botten 1 sleutel). Hij is alleen de drager van de cameratrack.
- 2D: pagina 0 = logo (levelbank House.rck afbeelding 1, 209×247 op (216,16), fade 1 s) + knipperende tekst
  "Press a key" (string 21, grootte 30, y = 336); pagina 1 = zelfde logo + 4 regels (22 New game, 23 Load game,
  36 Options, 2 Quit) vanaf y = 264, regelafstand 46,5, geselecteerde regel knippert (0,25 s uit / 0,25 s aan), wit.

## 1. Wie zet de camera: de keten 1141 → `0x44e690` → `0x44dda0(0x49)` → mode 0x80

### 1.1 Script (House, `out/house_code.txt`)
Het script stuurt geen enkel camerabericht. Object 124 (woord 1933): `DELAY 10` → **`1141 [0x100007c]`** = instantie
slot **124** (niet 0x73; slot 0x73 = 115 is de vector-instantie van de intro: 1160 en 1130). Handler `0x4448d1`:
`0x42f6b0(inst124, 5, &vec6, 0)` haalt de **typecode-5-node** (vector, 2 punten) van de instantie op in wereldcoördinaten
en `0x44e640(Perso, &vec6)` kopieert de 6 floats naar `Perso+0x564..0x578` en zet `Perso+0x57c = 1`.

Instantie 124 (House.ins model 15, node 2 flags 0x520, lokale punten (0,0,0) en (0,0,−300), quat (1,1,1,−1), schaal 1):
**P0 = (1113.34, 2309.87, 1193.29)**, **P1 = (1113.34, 2309.87, 1493.29)** → richting P0→P1 = **+z** (yaw 0 in de
conventie `atan2(dx, dz)` van de port). (Berekend met `ins_point_world` van de port, dezelfde code die 1130 gebruikt.)

### 1.2 Engine, elke tick zolang `app+0x68 == 0` (House): `0x401d0c` → `0x44e690(Perso)`
```c
void Perso_MenuPose(Perso *p) {                         // 0x44e690
    if (p->state == 0 && p->poseSet /*+0x57c*/) {       // +0x21c
        Perso_ScriptedAction(p, 0x49, &p->pose /*+0x564*/, NULL);   // 0x44dda0
        Perso_SetState(p, 5);                           // 0x44c980
    }
    p->fadeRate /*+0x100*/ = 10000.0f;                  // 0x461c4000
    Perso_SetFade(p, Cin_Running(game+0x64) /*0x44f2e0: cin-state 2..3*/ ? 0.0f : 1.0f, /*direct*/0);   // 0x44e7f0
}
```
Plaats in de frame (`0x401ab0`): ná de Perso-update (`0x401d07`, alleen als niet gepauzeerd en geen cinematic), vóór de
instantietick `Perso->vt[2](1)` (`0x401d57`, animatieklok) en de fade-stepper (`0x42b400` → `vt[3]` = `0x40bf10` →
`0x44e810`, `0x401d78`). De camera-update (`0x459090` → `0x41eff0`) zit helemaal vooraan in de frame (`0x401c01` →
`0x401940` → `0x445ba0`); die draait ook in app-state 0 (arg = `app+0 != 3`).

### 1.3 `0x44dda0(0x49, vec6, 0)` (CINEMATIC.md §6), de voor de titel relevante stappen
1. `0x463e30(0x49)` → logisch record **82** van tabel `0x4b6180`: `sub = {73, 0, 0, 0}`, `+0x10 = 6000`, **speed = 3.0**,
   restart = 1. (Record 81 = `{72,0,0,0}` = de intro.)
2. `total = remain = 0x436b90(82, 0)` = `duration(73)/4096 / speed` = 30.0 s / 3.0 = **10.0 s** (`Perso+0x538/+0x53c`).
   House.ins model 0: anim 73 = 6000 frames, duration 122880/4096 = 30.0 s.
3. Animatiecontroller `Perso+0x494`: `vt[4]()` (`0x436a40`: huidig = −1 ⇒ de volgende start is altijd een **herstart**),
   `vt[2](82)`; de tick `0x436a50` zet dan `inst+0xa8 = nu` (klok-start), `inst+0xb0..0xbc = {73, 0, 0, 0}` en
   `0x42e290(inst, 3.0)` (kloksnelheid).
4. Positie: `Perso+0x1f4 = P0`, `onGround = 1`, facing = `normalize_xz(P1 − P0)` via `0x459ff0`, `0x44bd00` (oriëntatie).
   `0x44bd30`: `f = −dir`; **rij 0 = f × up** (`0x41af10(this, out, arg)` = `this × arg`; PERSO_FRAME §2.4 schrijft
   "up × f", dat is de verkeerde volgorde), rij 1 = up × rij 0 = f, rij 2 = up. Voor dir = +z: rij 0 = (1,0,0),
   rij 1 = (0,0,−1), rij 2 = (0,1,0).
5. Camera (`0x44df67`): als `0x42feb0(Perso, 0x49)` (top-level node met flags 0x80 heeft een positie-track voor anim 73:
   ja, 100 sleutels) en `Perso+0x558`: `CamMgr+0x5d4 = Perso`, **`CamMgr+0x618 &= ~2` (geen letterbox)**,
   `0x41f9f0(2)` (**harde cut**), **`SetMode(7, 0)`** = mode 0x80 (`0x41f410`; in de mode-0x80-tak: bit 2 van `+0x618`
   uit ⇒ `0x41f940` = letterbox uit / 4:3). `SetMode` doet niets als `CamMgr+0x690` gezet is (bericht 590; in House nooit).

### 1.4 Lus
Toestand 5 (`0x44db50`) telt `remain -= dt`; bij ≤ 0 → toestand 0 + idle + `0x44e5a0` (volgcamera, overgang 0.5 s
vloeiend, `SetMode(0,0)`). In **dezelfde frame** roept `0x401d16` daarna `0x44e690` aan: toestand is 0 ⇒ actie 0x49
start opnieuw, controller-reset ⇒ klok opnieuw op fase 0, en `SetMode(7)` met cut annuleert de zojuist gestarte overgang
(`+0x109 = 0`). Omdat de camera-update pas in de volgende frame komt is de volgcamera **nooit zichtbaar**; de track is
gesloten (sleutel 0 = sleutel 99), dus de lus is naadloos. Netto: `fase = fmod(t, 10 s) / 10 s`.
De eerste 0.1 s na het laden (DELAY 10 vóór 1141) staat de gewone volgcamera achter de `.ins`-positie van Woody
(1114.5, 2390.6, 1093.6); dat valt in de eerste 4 zwarte frames + de fade-in van 1.0 s (GAMEFLOW §4.4) en is
praktisch onzichtbaar. **Onzeker**: of de Perso in die 0.1 s al in toestand 0 staat (vallen naar de grond kan de
start van de pose iets uitstellen; `0x44e690` wacht gewoon tot `+0x21c == 0`).

## 2. De beweging exact

### 2.1 Track-data (House.ins, model 0 = Woody, 142 nodes, 91 anims)
| node | flags | anim 73 | inhoud |
|---|---|---|---|
| 141 | 0x80 (camera, top-level) | 100 positiesleutels, t = 0 … 6000 (stap ≈ 60.6 frames) | cirkel in het lokale xy-vlak: middelpunt (−1159.14, 1026.58), straal 2500.0 (±0.7), **z = 807.17 constant**; hoek 0° → 360°, gelijkmatig (3.636° per sleutel), toenemend (lokaal x → lokaal y) |
| 142 | 0x180 (doel, top-level) | 1 sleutel | (−584.04, 664.62, 743.19) |
| 1 (wortel) en alle botten | 0 | 1 sleutel | stilstaande pose; wortel (0, −3, 52.09) |

Evaluatie = CINEMATIC.md §4 (`0x41f1ee` → `0x42fa40` → `0x42fa80`): `fase = inst+0xac / (duration/4096)`,
`frame = 6000·fase`, lineaire interpolatie tussen de sleutels (`0x43a660`; de cut-detectie slaat nooit aan: sleutels
liggen 60 frames uit elkaar), `C = c·M + T`, `G = t·M + T` met M = instantierotatie (rijen), T = instantiepositie,
view = lookAt(C, G, up = +y). Geen rol, geen fov-track.

### 2.2 In wereldcoördinaten (Perso op P0, facing +z ⇒ wereld = P0 + (lx, lz, −ly))
| grootheid | waarde |
|---|---|
| baanmiddelpunt | **(−45.80, 3117.04, 166.71)** = P0 + (−1159.14, 807.17, −1026.58) |
| straal | **2500** (horizontaal), hoogte constant **y = 3117.04** (= P0.y + 807.17) |
| kijkpunt (vast) | **(529.30, 3053.06, 528.67)** = P0 + (−584.04, 743.19, −664.62); ligt 679.6 (xz) naast het middelpunt en 64 lager dan de camera |
| startpunt (fase 0) | (2453.5, 3117.0, 166.7) = middelpunt + 2500·(+x) |
| omlooptijd | **10.0 s** (30 s animatie op snelheid 3) ⇒ **36°/s = 0.6283 rad/s**, baansnelheid ≈ 1571 eenh./s |
| richting | `eye = M + 2500·(cos a, 0, −sin a)`, `a = 2π·t/10`: fase 0.125 → (1719.1, 3117, −1600.7), 0.25 → (−47.3, 3117, −2332.3), 0.5 → (−2545.1, 3117, 168.1), 0.75 → (−44.6, 3117, 2665.7). Positieve draaiing om +y: van bovenaf tegen de klok in; de camera schuift naar zijn eigen rechterkant, het decor trekt naar links door beeld |
| afstand camera–kijkpunt | 1842 (fase ≈ 0.875) … 3166 (fase ≈ 0.375); pitch ≈ −1.2° … −2.0° |
| projectie | geen letterbox: `sx/sy = 1/0.75`, zoom 1.2 ⇒ hfov 100.4°, **vfov 83.97°** (CAMERA.md §5) |
| botsing / smoothing / overgang | geen (mode 0x80: directe look-at, cut) |

(De tabelwaarden zijn gesampled met `ins_camera_eval` van de port op de Woody-instantie geplaatst op P0/yaw 0; ze vallen
samen met de handberekening uit 2.1.)

De omwenteling loopt door op **alle** menupagina's zolang het level House is (tabel `0x405af8`: House wordt nooit
gepauzeerd, `0x404ef1`); alleen tijdens de intro-cinematic neemt anim 72 het over (§4).

## 3. Woody op het titelscherm

- Actie 0x49 → logisch record 82 → `.ins`-animatie **73**, eenmalig per 10 s, door `0x44e690` eindeloos herstart
  (§1.4). De keten `{73, 0, 0, 0}` betekent dat de instantieklok na afloop naar anim 0 (idle) zou doorlopen, maar de
  herstart komt in dezelfde frame. Anim 73 heeft op alle mesh/bot-nodes één sleutel: een **vaste pose**.
- Plaats: P0 = (1113.34, 2309.87, 1193.29), kijkrichting +z (instantie 124, §1.1). Rootmotion van 0x49 is nul.
- **Zichtbaarheid: onzichtbaar.** `0x44e690` zet elke tick `+0xfc = 1.0` (transparantiedoel; 1 = weg, INSTANCE.md §5) met
  `+0x100 = 10000/s`; de stepper `0x44e810` (Perso-vtable slot 3 `0x40bf10`, aangeroepen uit `0x42b400`) brengt `+0x6c`
  in één frame op 1.0; `0x42e374` tekent een instantie met `+0x6c > 0.98` niet. De animatieklok loopt wél door
  (`0x43eee0` staat in `0x42e2b0` vóór die test), dus de camera blijft draaien. Tijdens de cinematic (cin-state 2..3)
  is het doel 0 ⇒ direct zichtbaar; het script stuurt daarnaast zelf nog `57 [0,10000]` + `56 [0,0]` (woord 1264).
  Dit lost CINEMATIC.md §10.3 op: niemand zet `+0xfc` terug tijdens de titel (`0x44b4bd` alleen in toestand 3,
  `0x44d7e7`/`0x44d960` zijn het schade-/respawnpad).
- De HUD is verborgen (`0x404ebb`: `0x448450(hud, 2)` voor alle pagina's behalve 0x18/0x19).

## 4. New game, attract-timer, terugkeer

### 4.1 Pagina 1 → "New game"
`0x4600e0` (validate van pagina 1, item 0): `+0x14 = 1` (uitgesteld resultaat), `+0x38 = 0` (geen iris), logo-alfa
`[0x5d7b28] = 0` (logo direct weg), **returnwaarde 2** ⇒ App (`0x4051b0`): `SetVar(app+0x8c, 1)`. In de volgende frame
levert de pagina het uitgestelde resultaat **1** (`0x4601b0` → `0x45baa0`) ⇒ `0x4051c5`: `app+0x94 = 1`, pagina 0x1f.
Script object 115: `var1 == 1` → `var1 = 2`, meteen `1131 [0, 72]`, 8× `1132`, `1130 [0x73, 8, var2]`.
Cinematic (CINEMATIC.md): 0.5 s wachten met fade-out 0.4 s (de cirkelcamera draait in die tijd nog door), daarna
Perso naar de vector van slot 115 (−11299, −7, 601), anim 72 (159 s / 3 = 53.0 s), camera mode 0x80 **met** letterbox,
Woody zichtbaar. De Perso-update wordt overgeslagen (`0x401cf9`), dus toestand 5 / `remain` van actie 0x49 bevriezen.
Pagina 0x1f (`0x40508c`): klaar als `var1 == 4` (script, T = 5300) of actie **6** of **9** *losgelaten* (`0x467440`):
`app+0x98 = 35`, `0x44ffa0(save)`, `RequestLevel(var1 == 4 ? 0 : 0.5, 1, 1, 0)`. Pagina 0x1f tekent niets zichtbaars
(1 item, string 1 = leeg) en zet bij binnenkomst het logo direct uit (`0x45b390`: `[0x5d7b28] = 0`).

### 4.2 Attract (35 s)
`app+0x98` = 35.0 bij boot (`0x402653`) en na elk einde van pagina 0x1f (`0x4050c7`); **niet** bij het laden van House.
Hij telt alleen af op **pagina 0** (`0x404fe4`), niet op pagina 1 of dieper, en wordt door toetsen niet teruggezet.
- overgang > 0 → ≤ 0: `SetVar(app+0x8c, 1)` (zelfde scriptstart als New game);
- zolang ≤ 0 wordt de invoer van pagina 0 genegeerd (0.5 s dode tijd);
- < −0.5 (`0x4a94bc`): `app+0x94 = 0`, pagina 0x1f. Dat valt samen met het begin van de cinematic (0.5 s wachttijd van 1130).
Camera tijdens attract = de cinematic-camera (mode 0x80 op anim 72, letterbox), identiek aan New game.
Einde (var1 == 4 of toets 6/9 los): als nog niet 4: script-object `app+0x90` stoppen (`0x444380`, `0x4441d0`),
`var1 = 4`, cinematic afbreken (`0x44f290`), `Perso->vt[0x44]()` (reset `0x44ab20`, zet o.a. toestand 0). Altijd: 4 faders
wissen, tekstvak sluiten (`0x456ec0`), zwart beeld, fade-in 0.5 s, `0x404e30` (pagina 0 + muziekwissel).
Omdat de Perso daarna in toestand 0 staat start `0x44e690` actie 0x49 opnieuw: **de cirkel begint weer op fase 0** (cut).
Bij het natuurlijke einde doet de cinematic zelf de Perso-reset + `SetMode(0,0)` (`0x445b66`), met hetzelfde gevolg.

### 4.3 Terug naar de titel uit een level
`RequestLevel(0.5, 0, 0, 0)` → House laden → `0x404e30`: `app+0x54` wisselt (na een load eerst **track 0 "Menu"**, bij de
volgende aanroep zonder load track 48 "Menu02", SOUND.md §4), pagina 0, state 0; script-init → na 0.1 s 1141 → cirkel
vanaf fase 0. De attract-timer loopt verder vanaf de oude waarde.

## 5. De 2D-pagina's

### 5.1 Gemeenschappelijke paginaklasse (menu-object `0x445e30`, update `0x446440` → `0x4464f0`)
Pagina-object: `+4` geselecteerd item, `+8` invoervertraging (s), `+0xc` byte "resultaat gegeven", `+0x10` resultaat.
Vtable-slots: [1] teken, [2] itemtabel, [3] aantal, [4] pagina-id, [5] string van item i, [7] y-start (fractie van 480),
[8] lettergrootte, [9] validate → resultaat, [10] omlaag, [11] omhoog, [12]/[13] rechts/links (slider ±5, 0..100),
[14] terug, [15] enter. Item = 16 bytes `{u32 stringref 0x0002xxxx, u32 flags, u32 resultaat, u32 waarde}`; flags:
1 = kiesbaar, 2 = vaste kop (niet kiesbaar, wordt bij omhoog/omlaag overgeslagen `0x446920/0x446970`), 4 = rechts
uitgelijnd (x = 640 − w − 6.4), 8 = links (x = 6.4), 0x80 = gecentreerd op x = 160, 0x40 = x ongewijzigd, 0x10 = slider
(tekst + waarde + string 40/7), 0x20 = grootte × 0.8; geen vlag = gecentreerd (`x = 320 − w/2`).

Per frame (`0x4464f0(font, dt)`): font = `0x01030000` (font van de **levelbank**), `[0x5d7b1c] += dt`, boven 0.5
(`0x4b39a4`) terug naar 0 (knipperfase); fontgrootte 30 (`0x4b39a8`), **kleur `font+0x60 = 0xffffffff`** (wit, alfa 255);
`vt[1]()` tekenen; `0x446b00` logo (§5.2). Invoer alleen als `+8 ≤ 0` (anders `+8 -= dt`), via acties op `[0x5e6188]`,
alle "net ingedrukt" (`0x467420`): **2 = omhoog** `vt[11]`, **3 = omlaag** `vt[10]`, 1 = rechts `vt[12]`, 0 = links
`vt[13]`, **0xc = bevestigen** (`vt[9]`, alleen als `+0xc == 0`; actie 0xc wordt samen met actie 4 gezet door de
sprong-toets/-knop, `0x402efc`, `0x403168`: standaard LCtrl, PERSO_MOVE §3.4), **5 = terug** `vt[14]` (standaard
spatie); daarnaast `[0x5e6194]->vt[5](0)` → ook `vt[14]` (**onzeker** welke toets: toetsenbordobject, index 0).
Omhoog/omlaag lopen rond; een geslaagde verplaatsing zet de knipperfase op 0, een mislukte op 0.25.
**Geen geluid** bij navigeren of bevestigen in deze basisklasse.

Tekenen van de lijst (`0x446640`): grootte S = `vt[8]()`; voor elk item: zolang de gemeten breedte ≥ 640 en S ≥ 15:
S −= 1 (één S voor de hele pagina). `y = vt[7]()·480`; per item: tekst op (x, y), dan `y += celhoogte(62·S/40) + extra(0)`
= **46.5 bij S = 30**. Het **geselecteerde item wordt niet getekend zolang knipperfase < 0.25** (`[0x5d7b00]` =
0.5·0.5, `0x445e10`): 2 Hz knipperen, geen kleurverschil, geen cursor. Niet-kop-items verschijnen pas als `+8 ≤ 0`.

### 5.2 Logo (`0x446b00`, elke pagina, alleen als `app+0x68 == 0`)
`RectVirtual(x=216, y=16, w=209, h=247, sx=0, sy=0, sw=209, sh=247, kleur = 0x808080 | (A << 24), surface = bank 1
afbeelding 1, flags 8)`: **House.rck afbeelding 1** (256×256 met alfa; het "Woody Woodpecker"-logo met Woody in de
cirkel; geëxporteerd en bekeken: `tools/rckexport.py`). `A = ftol(v·0.2·254)` met `v = [0x5d7b28]` ∈ 0..5:
elke frame `v -= 5·dt` (ondergrens 0) ná het tekenen; pagina's die het logo willen (0 en 1) doen in hun teken-functie
`0x446ac0`: `v += 10·dt` (bovengrens 5). Netto: **fade-in 1.0 s op pagina 0/1, fade-out 1.0 s op elke andere pagina**;
pagina 0x1f/0x18/0x19/0x1e (`0x45b390`) en New game/Load game zetten `v = 0` direct. House.rck afbeelding 0 (kruis +
bolletjes) hoort bij de wereldkeuze, afbeelding 2 is blanco wit.

### 5.3 Pagina 0 — titel (vtable `0x4aa9c8`, 0x14 B, handler `0x404fe4`)
Teken `0x45bd10` = lijst + logo-fade. 1 item: **string 21 "Press a key"**, flags 1, resultaat 5; S = 30, y-fractie 0.7
⇒ **y = 336**, gecentreerd, knippert (het is het geselecteerde item). Handler: timer §4.2; zolang timer > 0:
actie **9 losgelaten** (Esc, `0x467440`) of resultaat 5 (bevestigen) → pagina 1. "Terug" geeft resultaat 24, genegeerd.
Ondanks de tekst reageert pagina 0 dus alleen op bevestigen (actie 0xc) en Esc. Geen geluid.

### 5.4 Pagina 1 — hoofdmenu (vtable `0x4ab5c0`, 0x3c B, ctor `0x460040`, basis "paneelpagina" `0x45b830`, handler `0x40519f`)
Items (tabel `0x4b5e70`, S = 30, y-fractie 0.55 ⇒ **y = 264, 310.5, 357, 403.5**, gecentreerd):

| i | string | resultaat | gevolg (`0x4600e0` + `0x405ba4`) |
|---|---|---|---|
| 0 | 22 "New game" | 2, dan 1 | §4.1 |
| 1 | 23 "Load game" | 3 (na 0.5 s) | iris dicht 0.85 → 0 in 0.5 s (`+0x38 = 1`), logo direct weg; dan `0x4051da`: savebestand-check `vt[3]` → pagina 0xb (lezen) / 7 ("No game saved") / 0xf, 0x10, 0x11 (fouten) |
| 2 | 36 "Options" | 6 (volgende frame) | pagina 0x1b |
| 3 | 2 "Quit" | 7 (volgende frame) | pagina 0x1c ("Are you sure?", cursor start op "No", `0x45bd40`) |

Enter (`0x460070`): basis `0x45b8c0` (selectie 0, **SoundFx 0x3f** = 63 → ref 111 in `Common/<karakter>.rck`, vol 50,
2D; `0x45b8ef`), daarna iris-parameters 0 → 0.85 in 0.5 s, `+8 = 0` (direct bedienbaar). Tijdens het sluiten (`+0x24`)
zijn omhoog/omlaag/terug geblokkeerd (`0x45bb90`, `0x45bba0`, `0x45bb30`); "terug" doet op pagina 1 niets (slot 20 =
lege functie).

**Iris** (`obj+0x2c`, 0x10 B: van, naar, duur, t; `0x4776b0` zet, `0x477920(dt)` animeert lineair en tekent via
`0x4776d0(v)`): een zwarte ring van 50 segmenten rond (320, 240), binnenstraal `v·0.99·480`, buitenstraal 475 (dekt het
hele scherm; hoekafstand = 400). v = 0.85 ⇒ gat 404 > 400 ⇒ niets te zien. Hij is alleen actief als `+0x38 == 1`:
de ctor van pagina 1 zet 0, dus **van pagina 0 naar 1 is er geen iris**; na "Load game" blijft `+0x38 = 1` en opent de
iris (0 → 0.85, 0.5 s) wanneer je uit de laad-/wereldkeuzepagina's terugkomt op pagina 1. Kleur/vertexformaat van
`0x482cf0` niet tot in de renderer gevolgd (**onzeker**, aangenomen dekkend zwart). Tekenvolgorde: items → iris → logo.

### 5.5 Overige pagina's (alleen lijst; tabellen via dezelfde klasse tenzij vermeld)
| pagina | inhoud (strings) | y | bijzonder |
|---|---|---|---|
| 2 | slotkeuze laden (klasse `0x45dd00`, 0x68 B) | | niet uitgewerkt |
| 3 | wereldkeuze, 3D-carrousel (klasse `0x45e560`, 0x148 B; GAMEFLOW §7) | | hoe de camera naar de carrousel bij (−11300, −6, 600) gaat is niet uitgezocht |
| 4 | klasse `0x45bfb0` (0x40 B) | | niet uitgewerkt |
| 5 | slotkeuze opslaan (klasse `0x45e1f0`, 0x68 B) | | niet uitgewerkt |
| 6 | 35 "Do you want to save?" / 5 Yes / 6 No | 0.4 | resultaat 8/9 |
| 7 | 64, 65, 66, 4 "Continue" | 0.4 | geen save gevonden |
| 8 / 9 / 0xa | 67 "Game Saved" / 59 "Save failed." / 60 "Load failed." + 4 | 0.4 | |
| 0xb, 0xc, 0xe | leeg (wachtpagina's lezen/schrijven) | 0.4 | |
| 0xd, 0xf..0x16 | PS2-memorycardteksten 69..97 | 0.3/0.4 | op pc (bijna) onbereikbaar |
| 0x17 | 61 "…overwrite this save?" / Yes / No | 0.4 | |
| 0x18 / 0x19 | pauze: 4 Continue, (19 Start again), 36 Options, 2 Quit | 0.05 | enter `0x45b390` (logo uit) |
| 0x1a | 101..103 controller disconnected | 0.4 | |
| 0x1b | opties (klasse `0x4601f0`, 0x20 B; vermoedelijk de sliders 38/39 met itemvlag 0x10, **onzeker**) | | niet uitgewerkt |
| 0x1c | 3 "Are you sure?" / 5 Yes / 6 No | 0.55 | cursor start op No |
| 0x1d | game over (`0x45bbd0`: zwart vlak + string 56 "GAME OVER", S = 35, midden, kleur 0xfeffffff; na 5 s resultaat 5) | | |
| 0x1e | resultaten (paneelpagina `0x45b830`, iris 0 → 0.37) | | GAMEFLOW §5.1 |
| 0x1f | intro loopt (leeg) | 0.6 | §4.1 |
| 0x20 | credits (`0x45bd90`: zwart vlak, levelbank-afbeelding uit tabel `0x4b5df8` (3 per set, set gekozen op `app+0x6c`) 256×256 op (32,112), wissel per 10 s met alfa-fade, string 131 "THE END" S = 35; details **onzeker**) | | |
| 0x21 | taal/memory card (console-rest) | 0.6 | |

## 6. Recept voor de port

```c
/* --- camera: alleen in level 0 (House), buiten de cinematic ------------------------------------------------ */
static float g_title_t;                        /* seconden sinds de start van actie 0x49 */
void title_pose_tick(float dt) {               /* = 0x44e690, elke tick in House */
    if (!g_pose || g_cin.state == 2 || g_cin.state == 3) { if (g_player) g_player->inst->fade = 0; return; }
    Vec3 P0, P1; vector_points(g_pose, &P0, &P1);                 /* typecode-5-node van slot 124, zoals bij 1130 */
    if (!g_title_on) {                                            /* toestand 0 -> actie 0x49 */
        player_place(g_player, P0, atan2f(P1.x - P0.x, P1.z - P0.z));   /* (1113.34, 2309.87, 1193.29), yaw 0 */
        inst_play_once(g_player->inst, 73, 3.0f, now);            /* pose; 30 s op snelheid 3 */
        g_title_t = 0; g_title_on = 1; g_cam.cut = 1; cam_set_mode(0x80); g_cam.anim_inst = g_player->inst;
        g_cam.letterbox = 0;
    }
    g_player->inst->fade = 1.0f;                                  /* Woody onzichtbaar (transparantie 1) */
    g_title_t += dt; if (g_title_t >= 10.0f) g_title_on = 0;      /* herstart op fase 0: naadloos */
}
/* in cam_update, mode 0x80 met anim_inst == speler: */
ins_camera_eval(inst, 73, fmodf(g_title_t, 10.0f) / 10.0f, &eye, &tgt);   /* bestaande functie in src/level.c */
lookAt(eye, tgt, up=(0,1,0)); vfov = 83.97f; letterbox = 0;               /* NIET 68.04 / letterbox zoals bij 1130 */
/* gesloten vorm ter controle: a = 2*pi*t/10;
   eye = (-45.80 + 2500*cos(a), 3117.04, 166.71 - 2500*sin(a));  target = (529.30, 3053.06, 528.67) */
```
- Na een cinematic (natuurlijk einde of afbreken) en na elke load: `g_title_on = 0` ⇒ cirkel opnieuw vanaf fase 0.
- De spelerupdate/invoer overslaan op de titel (toestand 5 = geen besturing); geen HUD.
- De huidige port gebruikt `g_pose->position` + `inst_yaw(g_pose)`; gebruik de vector P0→P1 (zelfde uitkomst hier: yaw 0).

```c
/* --- 2D (640x480 virtueel, hud.c-font van de levelbank, kleur wit alfa 255) -------------------------------- */
logo_v = clamp(logo_v + (page <= 1 ? +5 : -5) * dt, 0, 5);       /* netto-effect van 0x446ac0 / 0x446b00 */
quad2d(216, 16, 209, 247,  0, 0, 209, 247,  rgba(0x80,0x80,0x80, 254 * logo_v / 5), house_rck_image[1]);
blink += dt; if (blink > 0.5f) blink = 0;  bool hide_sel = blink < 0.25f;
page 0: if (!hide_sel) text_centered(336, 30, STR(21));           /* "Press a key" */
        attract -= dt (start 35); <=0: set_var(intro,1); < -0.5: page = 0x1f (attract); input alleen als attract > 0
        confirm (actie 0xc) of Esc los -> page 1
page 1: enter: audio_fx(63); sel = 0;   items STR(22), STR(23), STR(36), STR(2) op y = 264 + 46.5*i, gecentreerd,
        item sel verborgen als hide_sel; omhoog/omlaag rondlopend (blink = 0 na een stap)
        confirm: 0 -> logo_v = 0; set_var(intro, 1); volgende frame page = 0x1f, new_game = 1
                 1 -> logo_v = 0; iris 0.85 -> 0 in 0.5 s; dan save-check -> laadpagina's
                 2 -> opties (0x1b);  3 -> "Are you sure?" (0x1c, cursor op No; Yes = fade 0.5 s + afsluiten)
page 0x1f: niets tekenen; klaar als intro_var == 4 of actie 6/9 losgelaten -> attract = 35;
        new_game ? (save_reset(), request_level(intro_var == 4 ? 0 : 0.5, 1)) : (intro afbreken, zwart, fade-in 0.5, page 0, muziekwissel)
```
Tekenvolgorde: 3D → (HUD verborgen) → tekstvak 1080 → menupagina (items, iris, logo) → faders.

## 7. Open vragen / onzeker

1. Alles is statisch. Aanbevolen controle met `tools/wtrace.py`: breakpoint op `0x44dfad` (SetMode(7) uit `0x44dda0`)
   moet op de titel elke 10 s vallen; `CamMgr+0x1d0` (camerapositie) loggen om middelpunt/straal/richting te bevestigen.
2. De draairichting hangt aan de Perso-matrix (rij 0 = f × up, `0x44bd30`) en aan de port-conventie (rechtshandig,
   y omhoog). De port-functie `ins_camera_eval` geeft dezelfde punten als de handberekening; de intro-cinematic gebruikt
   hetzelfde pad en klopt visueel, maar de titel zelf is niet tegen een opname van het origineel gelegd.
3. Of de eerste start van actie 0x49 precies 0.1 s na de load valt (Perso moet in toestand 0 staan).
4. `[0x5e6194]->vt[5](0)` als extra "terug"-toets in menu's; de standaardtoetsen van acties 0xc/5 komen uit Woody.cfg.
5. Iris: kleur/alfa en of `0x482cf0` virtuele (640×480) of echte pixels verwacht.
6. Pagina 2, 3, 4, 5, 0x1b (klassen `0x45dd00`, `0x45e560`, `0x45bfb0`, `0x45e1f0`, `0x4601f0`) zijn niet ontleed; met
   name hoe pagina 3 de camera naar de carrousel brengt (geen `SetMode`-aanroep in die klasse gevonden; aanroepers van
   `0x41f410`: `0x402a84..0x402c1d` (debugtoetsen), `0x41e721`, `0x41fba2`, `0x445226/5f`, `0x445b66`, `0x44dfad`,
   `0x44e634`, `0x44ed6e`, `0x4562de`, `0x458ffb..0x459bba`, `0x498c20..0x499018`).
7. Het veld `+0x10 = 6000` in de records 81/82 van `0x4b6180` (CINEMATIC noemt het "prio") is voor de titel niet van belang.
8. Textuurfilter/afronding van de 2D-laag: zie HUD_TEXT.md §9.5.
