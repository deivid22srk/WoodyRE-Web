# Script-camera's van Woody.exe (aanvulling op CAMERA.md; werkdocument)

Alle adressen uit `Woody.exe` (imagebase 0x400000), statische analyse (`out/disasm_full.txt`,
`python tools/drange.py START END`). Lees eerst `docs/CAMERA.md` (CamMgr `[0x4c737c]`, volgcamera mode 1,
overgangen §6.1, rail-camera mode 8 §6.2). Dit document beschrijft de **overige modes** en de
script-koppeling. Scriptstatistieken: `python tools/ekodisasm.py extract/Data/<LEVEL>/code`.

## 0. Samenvatting

- **Mode 2 (bericht 510) en mode 4 (bericht 520) zijn wiskundig identiek**: een **stilstaande** camera op de
  `.ins`-camerapositie die elk frame een look-at doet naar `doelinstantie.pos + (0, f, 0)`. Geen smoothing, geen
  botsing, geen eigen fov. Het enige verschil: mode 4 zet **letterbox** (16:9, beeld naar boven geschoven) en
  `0x459090` bevriest de speler (§3). Beweging "naar" de vaste camera komt volledig uit de
  generieke overgang (CAMERA.md §6.1, berichten 560/570/580).
- **Argumentvolgorde gecorrigeerd** t.o.v. CAMERA.md §4: het bericht is `510/520 (cam, f, target)` – eerst de
  y-offset `f` (int → float, `msg+0xc`), dan de doelinstantie (`msg+0x10`). Bijv. W1A: `SEND 510 [316, 140, 0]`
  = camera 316 kijkt naar Woody (slot 0) + 140 omhoog; `SEND 520 [285, 0, 196]` = cinematic-camera 285 kijkt naar
  de oorsprong van instantie 196.
- **Routing**: camera's zijn gewone script-objecten met tag `0x01` in dezelfde slottabel als instanties
  (`level[0x6c][id & 0xffffff]`); berichten < 1000 gaan naar `obj->vtable[22](msg)`, voor camera's = `0x498bd0`.
  `0x02000000 | i` is **geen** cameraklasse maar een **scriptvariabele** (FORMAT_INS §5); `SEND 1160
  [0x2000001, 0x1000073]` = (var 1, object 0x73).
- **Mode 0x20** (engine, bericht 1088 + parameters 1110; 8 levels) is een zij-aanzicht-camera voor vlak-gebonden
  stukken (§4.2); **0x80** = camera uit een model-animatie (§4.3); **0x200** = first person (§4.4); 0x10/0x40/0x100
  worden door geen enkel level gebruikt. Bericht 800 heeft geen argument: de camera registreert *zichzelf* als volume-actor (§5).
- Onze loader (`src/level.c` `ins_load`, `Camera`/`cam_slots`) leest de camera-objecten al volledig (positie, id, TRAJ).

## 1. Routing van scriptberichten naar de camera

### 1.1 Berichtrecord en dispatcher

Berichtrecord (0x34 B, wachtrij met 32 records op het game-object, `0x401200`/`0x401250`): `+0` id, `+4` (niet
door de camerahandler gebruikt), `+8` **ontvanger** (1e scriptargument na het id), `+0xc` 2e argument, `+0x10` 3e
argument, …, `+0x30` next. In het script: `PUSH id; PUSH ontvanger; PUSH arg…; SEND n`.

Dispatcher `0x401370(msg)`:

| id | bestemming | adres |
|---|---|---|
| 7 | `0x4012f0` (annuleer wachtende berichten 0xc/0xd voor hetzelfde object) | `0x401381` |
| < 1000 | `obj = level[0x6c][msg[+8] & 0xffffff]` (`[0x50944c]+0x6c`); `obj->vtable[0x58/4 = 22](msg)` | `0x401392..0x4013ae` |
| 1000..1499 | game-handler `0x444870` (`this = [0x5d7afc]`) – o.a. 1088, 1110, 1160 | `0x4013b5` |
| 1500..1599 | `0x46cca0` (`[0x5e823c]`) | `0x4013cf` |
| 1600..1700 | `0x467fa0` | `0x4013e7` |

De ontvanger wordt alleen met `& 0xffffff` gemaskeerd; de tag-byte wordt niet gecontroleerd. Scripts sturen
camera-berichten dan ook met een **kaal slotnummer** (`PUSH 316`) of met `0x01000000 | slot` – beide komen in
hetzelfde slot uit. Voor een camera-object is vtable-slot 22 = `0x498bd0` (vtable `0x4ac040`), voor een camera
met TRAJ `0x498fd0` (vtable `0x4aa224`): die vangt alleen 540 (`0x21c`) af en stuurt de rest door naar `0x498bd0`.
Een instantie (geen camera) die 500..800 krijgt, komt in zijn eigen handler terecht en doet er niets mee.

### 1.2 Het `.ins`-camera-object

`.ins`: na alle modellen `u32 ncameras`, dan per camera `vec3 position; u32 id (0x01000000|slot); TRAJ`
(FORMAT_INS §1/§4; `tools/insparse.py` `parse_ins()["cameras"]`). Engine-object 0x2c B (ctor `0x498b90`: vtable
`0x4ac040`, `+0x28 = 0`): `+4` id, `+8` vlaggen (`& 0x1f == 3` = objecttype camera), `+0xc` positie, `+0x1c`
wereldcel, `+0x28` TRAJ-pointer. Het object komt in `level[0x6c][slot]` **en** `level[0x40][slot]`, in dezelfde
tabel als de instanties. Een camera heeft dus ook een eigen script-object (zelfde slotnummer in het `code`-bestand)
en kan zichzelf berichten sturen (zie bericht 800, §5).

**Port-status**: `src/level.c:207-216` leest `ncameras × {position, id, index, traj}` en bouwt `cam_slots[slot]`.
Er hoeft niets extra's geparsed te worden; een script-ontvanger `r` is een camera ⇔ `cam_slots[r & 0xffffff] != NULL`.
Slot 0 is in elk level de Woody-instantie (`id 0x01000000`, model 0) → `target = 0` betekent "kijk naar de speler".

### 1.3 Gebruik in de levelscripts (24 levels: K/S/W × 1A..3R + hubs KWS/SWS/WWS, W1B, W2A/B/D, W3A..D)

Telling van `PUSH id; PUSH <cameraslot>` (ontvanger is echt een `.ins`-camera):

| id | aantal | betekenis |
|---|---|---|
| 580 | 174 | overgangsmodus (1 vloeiend / 2 cut) – vóór vrijwel elke mode-wissel |
| 500 | 80 | terug naar volgcamera (achter de speler) |
| 520 | 62 | **mode 4**: vaste cinematic-camera met letterbox |
| 570 | 51 | overgangsduur (cs) |
| 660 / 670 / 680 / 650 | 29 / 27 / 19 / 14 | race-info uit / volgcamera-hoogte / -afstand / race-info aan (R-levels, races) |
| 540 | 23 | mode 8 rail-camera |
| 710 | 9 | auto-zoom aan |
| 510 | 8 | **mode 2**: vaste camera zonder letterbox (speler blijft bestuurbaar) |
| 560 | 4 | overgang op snelheid |
| 800 | 3 | camera registreert zichzelf als volume-actor (§5) |
| 501 | 2 | volgcamera vóór de speler |
| 700 | 1 | zoom terug naar 1.2 |
| 530, 550, 590, 600, 690 | 0 | **niet gebruikt** door scripts (530 = mode 0x10, 550 = first person: alleen engine) |

Daarnaast (game-berichten, geen camera-ontvanger): **1110** in W1A 29×, K1A 29×, S1A 29×, W1B 24×, W2B 51×, W2D 84×,
W3C 124×, W3D 114× (parameters van mode 0x20, §4.2) en **1088** (start mode 0x20, §4.2).

Typische sequenties:

```
; W1A obj: gameplay-hint, speler blijft lopen (mode 2)      ; W1A cinematic (mode 4), harde cut
SEND 570 [316, 200]      ; overgang 2.0 s                   DELAY 60:
SEND 580 [316, 1]        ; vloeiend                           SEND 580 [285, 2]        ; cut
SEND 510 [316, 140, 0]   ; cam 316 -> Woody + 140             SEND 520 [285, 0, 196]   ; cam 285 -> inst 196 + 0
...  (volume verlaten)                                       DELAY 160: ... animaties/geluid ...
SEND 570 [316, t]; SEND 580 [316, 1]; SEND 500 [316]          SEND 580 [285, 2]; SEND 500 [285]
```
Hubs (WWS/KWS/SWS): dat `580 cam 2; 520 cam …` (13× in WWS) hoort bij de **gebiedspoort-filmpjes** (object 258:
`26 [0, 260, 2]; 580 [259, 2]; 520 [259, 0, 11]`), niet bij de leveldeuren. Een leveldeur stuurt **geen enkel
camerabericht**: `1081 [level]; 1040 [deur, 17]; 1602 …; 3 [marker, …]`. De camera komt daar uit de animatie zelf
(mode 0x80, §4.3). Een deurenpaar binnen de hub idem: `1040 [302, 17]` … `DELAY 150` … `1040 [303, 18]`.
Rail: `580 cam 1|2; 540 cam d` … `500 cam`.

## 2. Mode 2 (bericht 510) en mode 4 (bericht 520)

### 2.1 Handler en parameters

`0x498c50` (510) / `0x498c9f` (520), `this` = camera-object, `p = CamMgr+0x43c` resp. `CamMgr+0x470`:

```c
p->pos    (+4..+0xc) = cam->pos (+0xc..+0x14);          // 0x498c55..0x498c6d
p->f      (+0x20)    = (float)(int)msg[+0xc];            // 0x498c70  fild/fstp   – y-offset van het kijkpunt
p->target (+0x10)    = level[0x6c][msg[+0x10] & 0xffffff];   // 0x498c76..0x498c8b – objectpointer, elk frame gelezen
SetMode(1 /*510*/ of 2 /*520*/, 0);                      // 0x41f410
```

`p+0x14` (vec3) = kijkdoel-basis (door de update gevuld), `p+0x20` = f. `CamMgr_Update` (`0x41eff0`) leest daarna
`target (+0x278) = p+0x14`, `lookOffset (+0xc4) = (0, p+0x20, 0)` (CAMERA.md §2) – dat zijn precies de
grootheden die de overgang (§6.1 aldaar) interpoleert.

### 2.2 Init

`0x41e520` (mode 2) / `0x41e560` (mode 4): `sub->prev (sub+0xa0) = CamMgr-toestand (+0x140, 0x27 dwords)`;
`sub->params (sub+0x27c) = p`; "init" = `0x462c60` = **lege functie** (`ret`). Mode 4 roept eerst `0x41f8d0`
(letterbox 1, §2.4). Geen toestand, geen voorsimulatie.

### 2.3 Per-frame update `0x4254c0` (mode 2) = `0x425810` (mode 4), byte-voor-byte gelijk op adressen na

```c
void Fixed_Update(Sub *s, float dt) {                // 0x4254c0 / 0x425810
    s->dt (+0x274) = dt;                             // verder ongebruikt
    s->P (+0x280) = s->params->pos;                  // 0x4254ea..0x42550a
    if (FindCell(s->P) == -1)                        // 0x40aba0([0x4c93b0]+0x14, x,y,z)
        s->P = s->prev.pos (sub+0x130 = sub+0xa0+0x90);   // camera buiten de wereld -> blijf op de vorige camerapositie
    Fixed_LookAt(s);                                 // 0x4255e0 / 0x425930
    s->state.pos (+0x94) = s->P;
    s->state = [I | -P] * [R | 0];                   // 0x437710, T = -P (+0x28..+0x30), 0x437740(state, &s->R (+0x28c))
    s->prev (+0xa0) = s->state;  (+0x1d8 = +0x13c: tweede schaduwkopie)
}
void Fixed_LookAt(Sub *s) {                          // 0x4255e0 / 0x425930
    up = (0, -1, 0);                                 // 0x4255fc: 0x43ff80(0, -1.0, 0)
    T  = s->params->target->pos (+0xc);              // INSTANTIE-OORSPRONG (inst+0xc..+0x14), geen marker/bbox
    s->params->look (+0x14) = T;                     // -> CamMgr+0x278 (doelpunt voor overgang/shake/auto-zoom)
    d  = T - s->P;  d.y += s->params->f (+0x20);     // 0x44005c, 0x42566b
    fwd   = normalize(d);                            // (niet genormaliseerd als |d| == 0)
    right = normalize(up x fwd);                     // 0x4400f0(up, fwd)
    up2   = normalize(fwd x right);
    R (+0x28c): R[i][0] = right[i], R[i][1] = up2[i], R[i][2] = fwd[i];   // zelfde conventie als CAMERA.md 3.7
}
```

Eigenschappen:

| vraag | antwoord |
|---|---|
| camerapositie | exact `cam->pos` uit het `.ins`, constant; gesnapshot op het moment van het bericht (camera's bewegen toch niet) |
| kijkpunt | `target->pos + (0, f, 0)`; `target->pos` = `inst+0xc` = instantie-oorsprong, **elk frame opnieuw gelezen** (volgt bewegende doelen zonder vertraging) |
| `f` | integer uit het script, 1:1 wereld-eenheden omhoog (W1A: 140 bij Woody = zelfde hoogte als het volgcamera-kijkpunt; 0 bij props) |
| smoothing/lag | geen in de mode zelf; alleen de overgang (positie + kijk-offset lineair in `t`, CAMERA.md §6.1) |
| fov/zoom | niets mode-specifieks: globale zoom `CamMgr+0x678` (1.2; berichten 690/700/710). Met 710 (auto-zoom) zoomt een vaste camera in op verre doelen, zie noot |
| botsing | geen; enige check is "positie buiten de wereld → vorige camerapositie houden" |
| TRAJ van de camera | genegeerd in modes 2/4 (alleen 540 gebruikt hem) |

Noot auto-zoom: `0x41f690` gebruikt `CamMgr+0x278`. In modes 2/4 is dat het **doelpunt `target->pos`** (zonder f),
want `CamMgr_Update` overschrijft `+0x278` met `p+0x14` ná `0x41f960`. Dus `d = |target->pos − P|`,
`zoom = 1.1 + (0.2 − 1.1)·clamp((d − 300)/2700, 0, 1)`.

### 2.4 Letterbox (`0x41f8d0`, `0x41f910`, `0x41f940`)

Er zijn **geen getekende balken en geen animatie**: letterbox = een andere projectie-schaal + een kleiner viewport,
direct (in één frame) omgeschakeld. De "balken" zijn het niet-beschreven deel van het scherm (gewist naar zwart).

| functie | effect | aanroepers |
|---|---|---|
| `0x41f8d0` Letterbox1 | alleen als `CamMgr+0x66c & 8` (ctor `0x41dd42` zet `|= 0x18` → altijd waar): `+0x684 = +0x680; +0x680 = 1; sy (+0x674) = 0.5625; sx = 1.0` | mode-4-init `0x41e565`; SetMode(7) (mode 0x80) als `+0x618 & 2` (`0x41f5ab`) |
| `0x41f910` Letterbox2 | `+0x684 = oude; +0x680 = 2; sy = 0.5625; sx = 1.0` | `CamMgr_Update` **elk frame zolang mode == 4** (`0x41f338`) |
| `0x41f940` Letterbox0 | `+0x680 = 0; sy = 0.75; sx = 1.0` | ctor, reset `0x41dfb2`, SetMode(5) (mode 0x20, `0x41f57b`), SetMode(7) zonder vlag, en `CamMgr_Update` wanneer de vorige mode 4/0x80 was en de huidige niet (`0x41f34f`, `0x41f36d`) |

Gevolg voor mode 4 (letterbox 2, `0x41f690`, CAMERA.md §5.1): `aspect = 1/0.5625 = 16:9`, `Hview = 4H/(3·aspect) =
0.75·H`, viewport-centrum `cy = (H + Hview)/4`, hoogte `Hview + 0.5`. Bij 640×480: beeldstrook y = 30..390
(30 px zwart boven, **90 px zwart onder** – ruimte voor ondertitels/dialoog); `tan(vfov/2) = 1.2·0.5625 = 0.675`
→ vfov 68.0°, hfov blijft 100.4°. Letterbox 1 (mode 0x80) centreert de strook: `cy = (H+1)/2` → y = 60..420.
Tijdens een vloeiende overgang naar mode 4 is de letterbox al vanaf het eerste frame actief; bij terugkeer naar
mode 1 verdwijnt hij op het eerste frame van de nieuwe mode (ook als de overgang nog loopt).

### 2.5 Engine-gebruik van mode 2: `0x41fb50(m, &pos, inst)`

`p43c.pos = *pos; p43c.target = inst; SetSpeed(100.0)` (`0x41f9b0`: duur = afstand/100 s); `SetTransition(1)` (vloeiend);
`p43c.f = CamMgr+0x348` (= y van de volgcamera-kijkoffset, ≈ 140 − drop); `SetMode(1, 0)`. Enige aanroeper:
`0x459030(ctl, &pos)` = `0x41fb50(ctl->CamMgr, pos, ctl->Perso)`, gebruikt door Perso-toestand 7 "verdwijnen"
(water/afgrond, PERSO_MOVE `0x44c308`): de camera blijft op een vast punt staan en kijkt de vallende speler na.
(De in CAMERA.md §8.9 genoemde `0x46496a`/`0x464aab` zijn geen aanroepers maar toevallige adrestreffers.)

## 3. Invoer en besturing tijdens een script-camera (`0x459090`, tabel `0x459934`)

| mode | wat `0x459090` elk frame doet | gevolg |
|---|---|---|
| 2 (idx 1, `0x45915d`) | alleen: knop 0xa net ingedrukt → geluid 9 ("mag niet") | **speler blijft volledig bestuurbaar** |
| 4 (idx 2, `0x459185`) | `0x44a650(Perso, Perso->vtable[34]() /*&eigen pos*/)`; `Perso+0x690 = 1` | **speler bevroren** |
| 8 (idx 3) | zie CAMERA.md 3.1; bestuurbaar | |
| 0x20 (idx 5) | niets hier; invoer loopt via `0x459c70` (§4.2) | 2.5D-besturing |
| 0x80 (idx 7) | `+0x618 |= 2`, positie/instantie doorgeven | animatie-gestuurd |
| 0x100 (idx 8), 0x200 (idx 9) | debugtoetsen resp. muis/pijltjes → kijkhoek; `Perso+0x690 = 1` alleen in 0x100 | |

* `0x44a650(Perso, &p)` (niet in toestand 5): `Perso+0x1f4 = p` (= eigen positie → netto "blijf staan"), `0x462990`
  (positie/grond opnieuw vastleggen), en `vtable[4]()` (reset) op de animatie-/bewegingsobjecten `+0x494` en `+0x498`.
* `Perso+0x690` = **frozen** (PERSO_FRAME §: `if (!p->frozen) {…}` slaat invoer, aanvallen, sprong en beweging over;
  `0x44b64a`, `0x44b68b`, `0x44b6b5`; aan het eind van de Perso-update weer 0, `0x44b938`). Ook `0x44b480` (toestand 6)
  wordt overgeslagen. In mode 4 staat de speler dus stil en is er **geen invoer**; animaties die het script start lopen wel.
  Bovendien: bij `CamMgr+0x138 == 2` kiest `0x463ec8` altijd animatie 1 (PERSO_MOVE r. 281) → Woody staat in idle.
* **Camera-relatieve besturing** gebruikt geen mode-kennis: de Mover neemt `camDir = normalize(pos − camPos)` (xz) met
  `camPos = Perso+0x2fc = CamMgr+0x140+0x90` (`0x44c000`, PERSO_FRAME r. 98/220) = de **uiteindelijke camerapositie
  van het vorige frame, inclusief overgang**. In mode 2 (en 8) is "vooruit" dus "weg van de vaste/rail-camera"; tijdens
  een vloeiende overgang draait het stuurkader mee met de reizende camera.

## 4. Overige modes

### 4.1 Overzicht: wie schakelt wat in? (alle 24 aanroepers van `SetMode` `0x41f410`)

| mode (idx) | init / update | wie | door scripts gebruikt? |
|---|---|---|---|
| 1 (0) | `0x41e450` / `0x424760` | 500/501; engine: `0x445b66`, `0x44e634`, `0x454227` (SaveAuto), `0x4562de`, `0x458ffb`, `0x45912b` (uit first person), debug `0x402ad3`, `0x402b33` | ja (82×) |
| 2 (1) | `0x41e520` / `0x4254c0` | 510; engine `0x41fb50` (§2.5) | ja (8×) |
| 4 (2) | `0x41e560` / `0x425810` | 520 | ja (62×) |
| 8 (3) | `0x41e5b0` / `0x421570` | 540 (TRAJ-camera) | ja (23×) |
| 0x10 (4) | `0x41e630` → `0x420350` / `0x4203c0` | alleen bericht 530 | **nee** (0×) |
| 0x20 (5) | `0x41e4e0` → `0x424b30` / `0x424bf0` | engine: bericht **1088** → `0x459960` (`0x4599a8`, `0x459bba`) | ja, via 1088 (46×) + 1110 |
| 0x40 (6) | `0x41e5f0` → `0x420cb0` / `0x420cf0` | geen aanroeper met index 6 gevonden (evt. debugpad `0x402a84` met variabele index) | nee |
| 0x80 (7) | – / inline `0x41f1ee` | engine: Perso-toestandswissel `0x44dfad`, cinematic-object `0x44ed6e`; altijd met cut | indirect |
| 0x100 (8) | `0x41e670` → `0x41ffe0` / `0x420010` | debugtoets `0x402c1d`; forceert cut | nee |
| 0x200 (9) | `0x41e6c0` → `0x425b60` / `0x425b80` | bericht 550 (0× gebruikt); engine `0x459050` (Perso-toestand 3), forceert cut | indirect |

### 4.2 Mode 0x20 = zij-aanzicht ("2.5D") voor vlak-gebonden stukken (berichten 1088 + 1110)

**Start**: bericht 1088 `(inst, v)` → `0x445001` → `Perso::0x459960(inst, v)`:

```c
if (perso->planeMode (+0x4ec)) { if (cam->modeIdx != 5) { SetTransition(2 /*cut*/); SetMode(5,0); } return; }
perso->+0x4e8 = inst; perso->planeMode = 1;
marker(inst, type 0, n 0) -> 2 punten A,B (0x42f6b0);  d = xzNormalize(B - A); (|d| < 0.01 -> (1,0,0))
perso->M.dir (+0x3bc, +0x3a4, +0x398) = d;  0x44a650(perso, &pos); 0x462990(perso);
if (v == 1) { p->side (CamMgr+0x61c) = 0; perso->+0x4ed = 1; perso->+0x4ee = 0; }
else        { p->side = 1;                perso->+0x4ed = 0; perso->+0x4ee = 1; }
p->+0x28..+0x44 = defaults (1000, 300, 340, 500, 0, 400(+0x3c), 700(+0x40), 200);      // 0x459b19..0x459b5f
p->pos (+8, CamMgr+0x624) = perso pos;  p->dir (+0x14, CamMgr+0x630) = d;
SetTransition(2);  SetMode(5, 0);                                                     // + Letterbox0 (0x41f57b)
vlak: n = ..., perso->+0x4f0..+0x4fc = (n, -n·A)  // 0x459bbf..0x459c57: speler wordt op het verticale vlak door A,B gehouden (0x459eb0)
```
Einde: Perso-toestandswissel `0x44de44` zet `+0x4ec = 0`; het script schakelt de camera terug met 580/500.

**Per frame** vult `Perso::0x459c70` (alleen als `+0x4ec`) het blok `p = CamMgr+0x61c`:
`p->pos (+8) = spelerpos`; `p->dir (+0x14)` = looprichting langs het vlak; `p->h (+4)` = **0** als actie 2 (↑) ingedrukt,
**2** als actie 3 (↓) of 5 (bukken), anders **1**; `p->flip (+0x20)` = 1 op het frame dat de speler links↔rechts omkeert
(acties 0/1, `+0x4ed/+0x4ee`; alleen als mode-index 5, niet in toestand 2, `+0x5b4 == 0`, `+0x50c == 0`).

**Update `0x424bf0(dt)`** (sub `S = [CamMgr+0x130]`, `S+0x28c` = A "vooruitblik", `S+0x290` = H hoogte, `S+0x294` = Lat zijafstand):

```c
if (!in_world(S->P (+0x298))) S->P = S->prev.pos;
dir  = normalize(p->dir);  side = normalize((0,-1,0) x dir);
p->Htarget (+0x24) = (p->h == 0) ? p->+0x34 /*500*/ : (p->h == 1) ? p->+0x30 /*340*/ : p->+0x38 /*0*/;
ramp(&S->A,   p->+0x2c /*300*/,  rate p->+0x40 /*700 e/s*/);      // 0x425300
ramp(&S->H,   p->Htarget,        rate p->+0x3c /*400 e/s*/);      // 0x425220
ramp(&S->Lat, p->+0x28 /*1000*/, rate p->+0x44 /*200 e/s*/);      // 0x4253e0
   // ramp: bij nieuw doel: T = |doel - start| / rate; daarna lineair start + (doel-start)*(t/T), t += dt, tot t/T > 1
side *= (p->side == 0) ? -S->Lat : +S->Lat;
ahead = (S->A < 0.001) ? dir : (dir.x*S->A, 0, dir.z*S->A) * s;   // 0x4250b0; s = ±blend (S+0x340, S+0x344):
   // bij p->flip: teken wisselt en blend loopt lineair van -huidig naar 1 in (|ahead| / p->+0x40)*(1 - start) s
C = p->pos + ahead + (0, S->H, 0);                               // kijkpunt: A eenheden vóór de speler, H erboven
S->P = C + side;                                                  // camera: zelfde hoogte, Lat eenheden opzij van het vlak
S->lookOffset (+0x280) = C - p->pos;   -> CamMgr+0xc4;  CamMgr+0x278 = p->pos
R = lookAt(S->P -> C, up (0,-1,0));  state = [I|-P]*[R|0];
```
Dus een horizontaal kijkende zijcamera op 1000 eenheden van het vlak, die 300 vooruit kijkt in de looprichting, met
↑/↓ om de camera 500/0 i.p.v. 340 boven de speler te zetten. Geen botsing.

**Bericht 1110 `(n, v)`** (`0x444b9d`, jumptable `0x445754`, `p = CamMgr+0x61c`, v als float):

| n | veld | betekenis | default |
|---|---|---|---|
| 1 | `p+0x34` | hoogte bij ↑ | 500 |
| 2 | `p+0x30` | normale hoogte | 340 |
| 3 | `p+0x38` | hoogte bij ↓/bukken | 0 |
| 4 | `p+0x2c` | vooruitblik A | 300 |
| 5 | `p+0x28` | zijafstand | 1000 |
| 6 | `p+0x40` | snelheid van A (en van de omkeer-blend) e/s | 700 |
| 7 | `p+0x3c` | snelheid van H e/s | 400 |
| 8 | `p+0x44` | snelheid van de zijafstand e/s | 200 |
| 9 | – | alle acht terug naar default | |

(CAMERA.md §1.2 noemt de defaults in offsetvolgorde `+0x28..+0x44` = 1000, 300, 340, 500, 0, 400, 700, 200; let op dat
`0x459b55/0x459b4b` `+0x3c = 400` en `+0x40 = 700` zetten.) Gebruik: W1A/K1A/S1A 29×, W1B 24×, W2B 51×, W2D 84×, W3C 124×,
W3D 114×; typisch `1110 2 150; 1110 1 500; 1110 3 0; 1110 7 200` rond een volume, en `1088 inst 1|2` bij binnenkomst.

### 4.3 Mode 0x80 = camera uit de animatie van een instantie (deuren én cinematics)

`CamMgr_Update` `0x41f1ee`: `inst = CamMgr+0x5d4`; als `0x42feb0(inst, inst->anim (+0xb0))` (model heeft een node van
soort **0x80** met een track voor deze animatie): `0x42fa40(inst, &state, &pos, &CamMgr+0x5d8)` → `0x42fa80`: zoekt de
nodes van soort **0x80 (camera)** en **0x180 (camera-doel)** in het model (`inst+0xf8`), evalueert hun positie-tracks
(`0x43a660`) op de huidige animatietijd (in **keyframes**: `nframes · t`), transformeert met de instantie en bouwt de
look-at; `state.pos = pos`, `CamMgr+0xc4 = +0x5d8`. Let op: in deze mode staat in `+0xc4` een **absolute wereldpositie**
(het getransformeerde doelpunt), geen offset zoals in de modes 1/2/4, en `+0x278` wordt hier niet geschreven.
De zoeklus op `0x42fb39` heeft **geen eindtest**: hij stopt pas als beide nodes gevonden zijn, dus een model met een
oog-node zonder doel-node loopt de nodetabel uit. Faalt `0x42feb0`, dan wordt de hele tak overgeslagen en blijven het
vorige oog en doel staan – de camera **bevriest** op dat beeld in plaats van terug te springen.

Twee aanzetters, allebei met cut (`0x41f9f0(2)`) en `SetMode(7, 0)` (de eerste parameter is een **schuif**: masker
`1 << 7`):

* **de gescripte Perso-actie zelf**, `0x44df67..0x44dfb2` – dat is de *staart van `0x44dda0`*, niet een aparte
  toestandswissel (correctie op een eerdere lezing van dit document). Hij **wist** `CamMgr+0x618` bit 1
  (`0x44df92 and edi, 0xfffffffd`): de deurcamera heeft dus **nooit** letterbox. De test op `Perso+0x558` is altijd
  waar (24 instructies eerder gezet, alleen gewist door de Perso-reset `0x44ac7e`) en hoeft niet geport te worden.
* **het cinematic-object**, `0x44ed3f` + `0x44ed53 or esi, 2` – die **zet** de bit, dus mét letterbox
  (`0x41f5ab` leest `CamMgr+0x618 & 2`). De per-frame handler `0x4598c4` herstelt de bit zolang mode 0x80 loopt,
  daarom moeten beide aanzetters hem schrijven.

Het Woody-model heeft precies twee zulke nodes (`insparse`: `80:2`): index 140 met vlaggen `0x080` (oog) en 141 met
`0x180` (doel), allebei top-level. Ze dragen een track op de animaties **17, 18, 41..47, 49, 50, 53..78, 80..84** –
dus op de twee deuracties én op de cinematics. In animatie 17 staat het oog op lokaal `(−371, −78, 178)`: 371 eenheden
**naast** Woody's eigen as op hoofdhoogte, kijkend naar een punt op die as. Dat is het zijaanzicht waarin je hem de
deur in ziet lopen. Beide animaties zijn 900 frames / 4.5 s en lopen op snelheid 3, dus 1.5 s – precies de `DELAY 150`
die de hubscripts tussen `1040 [deur, 17]` en `1040 [andere deur, 18]` zetten.

**Verlaten.** Actie 18 eindigt in `0x44db60` op `0x44e5a0`: `SetTransitionDuration(0.5)`, `SetTransition(1)` = *blend*,
`SetMode(0, 0)` = volgcamera – tenzij het zijaanzicht intussen weer aanstaat (`Perso+0x4ec`). Actie 17 herstelt de
camera **nooit**: die blijft op de laatste trackframe staan tot het level wisselt of de teleport `0x458f90` hard cut.

### 4.4 Mode 0x200 = first person (Perso-toestand 3)

`0x459050`: `0x44c080(Perso, p540, 1)` (oogpositie/-richting in `p = CamMgr+0x540`), `p+0x28 = p+0x2c = 0`, cut,
`SetMode(9)`. Per frame zet `0x459090` (idx 9) de muis-delta's of ±5 (pijltjes) in `p+0x28/+0x2c`; update `0x425b80`:
delta geklemd op ±64, `pitch (p+0x78) ∓= min(|dx|·dt·0.19635 (0x4aa1e4 = π/16), 0.31416 (0x4aa1e0 = π/10))`, idem
`yaw (p+0x7c)` met `p+0x2c`; begrenzing door `p+0x80..+0x8c` (als ≠ 0); rotatie = basis `p+0x54` · Rx(pitch) · Ry(yaw)
(`0x437940`, `0x437970`, `0x440b40`). De kijkrichting wordt teruggeschreven naar de Mover (CAMERA.md 3.1). Verlaten:
`0x45910e` → cut naar volgcamera. Bericht 550 doet alleen `SetMode(9,0)` en komt in geen enkel script voor.

### 4.5 Modes 0x10, 0x40, 0x100 (niet door de levels gebruikt)

* **0x10** (bericht 530, 0× in scripts): `0x420350` neemt de huidige toestand over, `p404+0x20 = camerapositie`,
  `p404+0x34 |= 1`, `p404+0 = p404+4 = 150.0`; update `0x4203c0` loopt over een TRAJ (`p404+0x30`, punten stride 0x10)
  met `0x438210` (dichtstbijzijnde segment) en een gefilterde snelheid (`×1.5` `0x4aa184`, stappen van 1/60 s `0x4a9990`,
  demping 0.2) – een oudere rail-variant. Niemand vult `p404+0x30` in de gevonden code → zonder TRAJ doet de update niets
  (`0x4203f4`/`0x4203fc` → return). Niet porten.
* **0x40**: quaternion-camera (`0x420cf0`), geen aanroeper. **0x100**: vrije debugcamera (toets in `0x402940`).

## 5. Bericht 800: camera als volume-actor (`CamMgr+0x664`)

`0x498d93`: `CamMgr+0x664 = level[0x6c][msg[+8] & 0xffffff]` – let op: `msg+8` is de **ontvanger zelf**, er is geen
extra argument (correctie op CAMERA.md §4 "800 cam, inst"). Scripts: `DELAY n: SEND 800 [0x0100018b]` vanuit het
script van de camera zelf (W2B obj 395, K2R obj 483, S2R; 3× totaal; alle drie `.ins`-camera's zonder TRAJ).
Per frame (einde `0x41eff0`, `0x41f379..0x41f3cf`): `obj->pos (+0xc) = CamMgr.state.pos`; `0x4077f0(obj, 0)` (wereldcel
opnieuw bepalen); `0x434740(&pos, -1, obj)` → volume-tests met dit object als actor ⇒ script-events "camera komt volume
binnen/verlaat" (berichten 0x65/0x66/0x67 met `actor_id = obj+4`, EVENTS.md r. 59-65, 132-140). De `.ins`-positie van
dat camera-object wordt daarbij overschreven; gebruik zo'n camera dus niet ook voor 510/520. In de andere levels is
`+0x664` NULL en gebeurt er niets.

## 6. Berichten 1160 en 1110 (game-handler `0x444870`)

* **1160 `(a, b)`**: `game+0x8c = a`, `game+0x90 = b`. **Geen camera-bericht.** `a` = scriptvariabele (`0x02000000|i`),
  `b` = script-object. De engine schrijft `var a = 1` wanneer de level-overgangstimer `game+0x98` door 0 gaat
  (`0x405029`, `0x4051b9`) en `var a = 4` + annuleert de timers van object `b` (`0x444380`, `0x4441d0`) bij level
  verlaten/herstarten (`0x405143`) – zie EVENTS.md §4.2. House: `SEND 1160 [0x2000001, 0x1000073]` = "meld in var 1 /
  object 0x73 wanneer de overgang klaar is".
* **1110 `(n, v)`**: parameters van mode 0x20, zie §4.2.

## Recept (C-achtige pseudocode)

```c
/* state */ int mode; Vec3 fixPos; int fixTarget; float fixF;       /* + overgang uit CAMERA.md 6.1 */

void cam_msg(int id, int recv, int a1, int a2) {                    /* recv & 0xffffff moet een cam_slot zijn */
    Camera *c = cam_slots[recv & 0xffffff]; if (!c) return;
    switch (id) {
    case 510: case 520:
        fixPos = c->position; fixF = (float)a1; fixTarget = a2 & 0xffffff;
        set_mode(id == 510 ? 2 : 4);          /* set_mode: start overgang als trans_mode == smooth (bevries oude pos + lookOffset) */
        break;
    case 500: case 501: set_mode_follow(id == 501); break;
    case 570: trans_dur = a1 * 0.01f; dur_from_speed = 0; break;
    case 560: trans_speed = (float)a1; dur_from_speed = 1; break;
    case 580: trans_cut = (a1 == 2); break;
    }
}
void cam_fixed_update(float dt) {                                   /* mode 2 en 4 */
    Vec3 P = fixPos;  if (!in_world(P)) P = prevCamPos;
    Vec3 T = instance_origin(fixTarget);                            /* inst+0xc, elk frame */
    target = T;  lookOffset = (Vec3){0, fixF, 0};                   /* voor overgang / shake / auto-zoom */
    view = lookAt(P, T + lookOffset, up=(0,1,0));                   /* == engine up (0,-1,0) met y-omlaag camera-ruimte */
    if (transition_active) blend(): pos = from + (P - from)*t; look = T + lerp(lookFrom, lookOffset, t);
    if (mode == 4) { sy = 0.5625f; viewport = {0, H*0.0625, W, H*0.75}; /* y 30..390 bij 480 */ freeze_player(); }
    else           { sy = 0.75f;   viewport = full; }
}
```

## Recept mode 0x20 (zij-aanzicht), kort

```c
/* bij 1088(inst, v): d = xz-richting van marker type 0 van inst; side = (v == 1) ? -1 : +1; A = 300; H = 340; Lat = 1000; cut */
Vec3 sideV = normalize(cross((Vec3){0,-1,0}, d));           /* engine-conventie; port: controleer het teken één keer in-game */
Htarget = up_held ? h_up : (down_or_crouch ? h_down : h_norm);      /* 500 / 0 / 340, via 1110 n = 1 / 3 / 2 */
A = ramp(A, a_target, 700*dt);  H = ramp(H, Htarget, 400*dt);  Lat = ramp(Lat, lat_target, 200*dt);   /* lineair */
C = playerPos + (d.x*A*s, H, d.z*A*s);                      /* s = +1/-1 looprichting, bij omkeren lineair geblend */
P = C + sideV * (side * Lat);   view = lookAt(P, C, up=(0,1,0));
```

## Onzeker

1. Letterbox: of het zwart van de balken uit een expliciete clear komt of uit het niet-tekenen buiten het viewport is niet
   nagegaan (renderer `0x4843b0`); zeker is dat er geen balk-animatie in de CamMgr zit.
2. Mode 0x20: (a) het teken van `side` (welke kant van het vlak bij `v == 1`) is alleen uit de formule afgeleid, niet in-game
   geverifieerd; (b) de exacte omkeer-blend (`S+0x340..+0x350`, `0x4250e8..0x42520e`) is vereenvoudigd weergegeven;
   (c) de vlakberekening in `0x459bbf..0x459c57` (`0x41af10`, `0x4239f0`) is niet uitgeschreven; (d) K1A stuurt een reeks
   1110-berichten vlak **vóór** 1088, terwijl `0x459960` bij een nieuwe start de defaults terugzet – of die 1110-waarden
   dan verloren gaan of dat `+0x4ec` op dat moment al gezet is, is niet nagegaan.
3. Mode 0x200: welke van `p+0x78`/`p+0x7c` pitch resp. yaw is (`0x437940` vs `0x437970`) is niet geverifieerd.
4. Mode 0x80: de trackevaluatie `0x42fa80` is alleen op hoofdlijnen gelezen (nodesoorten 0x80/0x180, `0x43a660`);
   de betekenis van `Perso+0x558` en `CamMgr+0x618` bit 1 (letterbox bij cinematics) is afgeleid uit de context.
5. Mode 0x10: schrijver van `p404+0x30` (TRAJ) niet gevonden; als dood beschouwd omdat geen script 530 stuurt.
6. Berichtrecord `+4`: niet bepaald (afzender of argumentaantal); voor de camera irrelevant.
7. 510/520 met een doel-slot dat leeg is (placeholder `0x4aa28c`): de update leest dan `placeholder+0xc` (ongedefinieerd/0);
   in de data niet aangetroffen.
