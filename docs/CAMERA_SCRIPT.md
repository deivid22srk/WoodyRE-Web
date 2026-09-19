# Script-camera's van Woody.exe (aanvulling op CAMERA.md; werkdocument)

Alle adressen uit `Woody.exe` (imagebase 0x400000), statische analyse (`out/disasm_full.txt`,
`python tools/drange.py START END`). Lees eerst `docs/CAMERA.md` (CamMgr `[0x4c737c]`, volgcamera mode 1,
overgangen §6.1, rail-camera mode 8 §6.2). Dit document beschrijft de **overige modes** en de
script-koppeling. Scriptstatistieken: `python tools/ekodisasm.py extract/Data/<LEVEL>/code`.

## 0. Samenvatting

- **Mode 2 (bericht 510) en mode 4 (bericht 520) zijn wiskundig identiek**: een **stilstaande** camera op de
  `.ins`-camerapositie die elk frame een look-at doet naar `doelinstantie.pos + (0, f, 0)`. Geen smoothing, geen
  botsing, geen eigen fov. Het enige verschil: mode 4 zet **letterbox** (16:9, beeld naar boven geschoven) en
  `0x459090` bevriest/verplaatst de speler (§6). Beweging "naar" de vaste camera komt volledig uit de
  generieke overgang (CAMERA.md §6.1, berichten 560/570/580).
- **Argumentvolgorde gecorrigeerd** t.o.v. CAMERA.md §4: het bericht is `510/520 (cam, f, target)` – eerst de
  y-offset `f` (int → float, `msg+0xc`), dan de doelinstantie (`msg+0x10`). Bijv. W1A: `SEND 510 [316, 140, 0]`
  = camera 316 kijkt naar Woody (slot 0) + 140 omhoog; `SEND 520 [285, 0, 196]` = cinematic-camera 285 kijkt naar
  de oorsprong van instantie 196.
- **Routing**: camera's zijn gewone script-objecten met tag `0x01` in dezelfde slottabel als instanties
  (`level[0x6c][id & 0xffffff]`); berichten < 1000 gaan naar `obj->vtable[22](msg)`, voor camera's = `0x498bd0`.
  `0x02000000 | i` is **geen** cameraklasse maar een **scriptvariabele** (FORMAT_INS §5); `SEND 1160
  [0x2000001, 0x1000073]` = (var 1, object 0x73).
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
Hubs (WWS/KWS/SWS): per leveldeur `580 cam 2; 520 cam 0 deur` … `580 cam 2; 500 cam` (13× in WWS).
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
| fov/zoom | niets mode-specifieks: globale zoom `CamMgr+0x678` (1.2; 690/700/710). Met 710 (auto-zoom) zoomt een vaste camera in op verre doelen: `zoom = lerp(1.1, 0.2, clamp((|target+(0,50,0)... |` zie noot |
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
`p43c.f = CamMgr+0x348` (= y van de volgcamera-kijkoffset, ≈ 140 − drop); `SetMode(1, 0)`. Aanroepers `0x459030`,
`0x46496a`, `0x464aab` (context: zie Onzeker).

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

## Onzeker

1. `0x41fb50` (engine-mode-2): aanroepers `0x459030`, `0x46496a`, `0x464aab` niet uitgezocht.
2. Of het zwart van de letterboxbalken uit een expliciete clear komt of uit het niet-tekenen buiten het viewport is niet
   nagegaan (renderer `0x4843b0`).
