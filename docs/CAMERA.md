# Camera-logica van Woody.exe (werkdocument, wordt incrementeel bijgewerkt)

Alle adressen zijn uit `Woody.exe` (imagebase 0x400000). Disassembly: `out/disasm_full.txt`,
`python tools/drange.py START END`. Vectorhelpers: zie §A.

## 0. Samenvatting

- **Volgcamera (mode 1, §3)** is een "lazy follow": hij draait niet actief achter de speler. Kijkdoel = speler + (0,140,0)
  (onvertraagd). Positie: xz-afstand tot `T = speler + (0,120,0)` wordt in de band 300..400 gehouden (inhalen 433·d/400
  eenh./s, wijken 66,7·400/d eenh./s, ×10 als de speler naar de camera toe kijkt); hoogte nadert `T.y + 180` met `6·dt`.
  Knop 0xa (en Perso-toestanden 1/4/8) trekt hem met `3·dt` (eerste 0,5 s `7·dt`) naar het punt 400 achter de speler.
  Botsing: bol r = 40 met uitduwen + zichtlijn-veto + kruimelpad als de speler achter geometrie verdwijnt (3.6).
- **Projectie (§5)**: software-T&L; `tan(hfov/2) = zoom·sx = 1.2` → hfov 100.4°, vfov 83.97° (4:3); camera-ruimte x rechts,
  y omlaag, z vooruit; rechtshandige wereld, geen spiegeling; look-at met up = (0,−1,0) ≡ `gluLookAt(..., up=+y)`.
- **Overgangen (§6)**: lineaire interpolatie (positie + kijk-offset) van de bevroren oude camera naar de bewegende nieuwe,
  duur 2.0 s default / bericht 570 (cs) / 560 (afstand÷snelheid); 580 kiest vloeiend of cut. Mode 8 (TRAJ) is een
  **rail-camera** op vaste afstand van de speler, geen tijdpad. `0x41fbd0` is camera-shake (±5·min(t,3) eenheden).

### 0.1 Recept: volgcamera in C (per frame; P = camerapositie, persistent; `pos`, `look` = spelerpositie/-kijkrichting)

```c
T = pos + (0,120,0);  L = pos + (0,140,0);
if (behind /*knop of toestand 1,4,8*/) { D = T - normalize(look)*400;  mv = (D - P) * dt * (quick ? 7 : 3); }
else {
    v = T - P; v.y = 0; d = len(v); vn = v/d;  mv = 0;
    k = dot(xzn(L - P), -xzn(look)) > 0.7f ? 10 : 1;          /* speler komt op de camera af */
    if (d < 300)      { s = (400/d)*dt*k*66.6667f; if (d + s > 300) s = 300 - d;  mv = -vn*s; catchUp = 0; }
    else if (d > 400) { s = (d/400)*dt*433.333f;   if (d - s < 400) s = d - 400;  mv =  vn*s; catchUp = 1; }
}
if (rising && P.y - T.y < 300) mv.y = T.y - Tprev.y;  else if (rising) mv.y = 0;
else                           mv.y = (T.y + 180 - P.y) * 6 * dt;
N = P + mv;  hit = sweep_sphere(P, N, r=40, step=35, &N2);     /* duw uit muren; corr = N2 - N */
if (hit) N = N2;
if (!in_world(N) || ray_blocked(N, T)) N = P;                    /* zichtlijn-veto */
if (ray_blocked(P, T) /*begin van het frame*/) follow_breadcrumbs(); /* pad {P, Tprev, T,...}, u += 0.04/frame */
P = N;
drop = (rising||falling) ? min(drop + 450*dt, 150) : drop*0.94f;   Lk = L - (0,drop,0);
view = lookAt(P, Lk, up=(0,1,0));  proj = perspective(fovy=83.97°, aspect=4/3);  Tprev = T;
```
Start/reset (bericht 500): `P = pos − look + (0,100,0)`, daarna 1 stap dt = 0.1 en 100 stappen dt = 0.04 in behind-mode.

- De camera is **geen** script-instantieklasse. Er is één globale **camera-manager** `CamMgr`
  op `[0x4c737c]` (ctor `0x41dca0`, grootte ≥ 0x698, aangemaakt vanuit `0x458ec0`, vernietigd
  door `0x41ddb0`). Hij bevat 9 sub-camera's ("modes") die als bitvlaggen worden geselecteerd
  (`CamMgr+0x134`), plus één camera-instantie (`CamMgr+0x664`) die de eindpositie krijgt (voor
  geluid/zichtbaarheid).
- De `.ins`-camera's (vtable `0x4ac040`, met TRAJ `0x4aa224`) zijn dunne script-objecten: hun
  berichthandler (`0x498bd0`, TRAJ-variant `0x498fd0`) vertaalt de scriptberichten 500…800
  naar aanroepen op de CamMgr (mode-wissel `0x41f410`, overgangsparameters, enz.). Zie §4.
- **Kandidaten die geen camera zijn** (§7): type-42-instantie (`0x452140`, berichten 1000–1004)
  is een *projectielwerper* (MESSAGES.md noemt dit ten onrechte "camera"); `0x452e10` is de tick
  van klasse 20/21; `0x447210`/`0x447660`/`0x447260`/`0x4480d0`/`0x447d70` zijn het
  HUD/GUI-subsysteem (`game+0x30`).

## 1. Datastructuren

### 1.1 `Repere` (frame/assenstelsel), 0x9c = 156 bytes, 39 dwords

Gebruikt als "camera-toestand" op `CamMgr+0x140` (accessor `0x41fa30` geeft `&CamMgr+0x140`).

| offset | type | betekenis | bron |
|---|---|---|---|
| +0x00 | 3×3 f32, rij-major, stride 12 | rotatiematrix R (rijvectoren); identiteit via `0x437710` (zet [0]=[0x10]=[0x20]=1, rest 0) | `0x437710` |
| +0x24 | vec3 | translatie T. In de camera-toestand = **−cameraPositie** (view-matrix `[R | T]`) | `0x41ef42..0x41ef7c` |
| +0x30..+0x8c | ? | nog niet uitgezocht (waarschijnlijk inverse/wereldmatrix en hulpvelden) | |
| +0x90 | vec3 | **camerapositie in wereld** (`CamMgr+0x1d0`); gebruikt door de framefunctie `0x401c11` voor de wereldcel/zichtbaarheid (`0x42a980`) en gekopieerd naar de camera-instantie (`inst+0xc`) in `0x41f396` | `0x401c11`, `0x41f396` |

`0x437740(this, B)`: `this = this * B` voor 3×4-matrices (rijvectoren): rotatie `R' = R·R_B`,
translatie `T' = T·R_B + T_B`. Bij het opbouwen van de view-matrix (`0x41ef3d..0x41ef82`):
`this = [I | −pos]`, dan `this *= [R_lookat | 0]` → view = translate(−pos) · R.

### 1.2 `CamMgr` (`[0x4c737c]`)

| offset | type | betekenis | bron |
|---|---|---|---|
| +0x00 | u32 | "race-info actief" (0/1); +4, +8 = bewaarde afstand/hoogte van de volgcamera (150.0, 50.0 default) | `0x41dd67`, `0x41fab0` |
| +0x0c | u32 | vlag: overgangsparameters gezet dit frame | `0x41f9b0`, `0x41f9d0` |
| +0x10 | u32 | vlag: overgangsmodus gezet dit frame | `0x41f9f0` |
| +0x14 | u32 | vlag: cameramode gewisseld dit frame | `0x41f49a` |
| +0x18 | ptr | instantie (speler?) waarvan de rotatiematrix (+0x34..+0x3c) de start-kijkrichting voor mode 1 geeft | `0x41e488` |
| +0x1c | Repere | bevroren toestand bij begin van een overgang ("cut"-startframe) | `0x41eafd` |
| +0xac | vec3 | startpositie van de overgang (= +0x1c+0x90) | `0x41ebcb` |
| +0xc4 | vec3 | look-at-**offset** van de actieve mode t.o.v. het doelpunt `+0x278` (mode 1: `L' − spelerpos`; andere: alleen y) | `0x41f073` e.a. |
| +0xd0 | vec3 | look-at-offset aan het begin van de overgang | `0x41eb11` |
| +0xdc | vec3 | huidige geblende look-at-offset | `0x41ed4d` |
| +0xf8 | f32 | duur van de overgang (s), default 2.0; `< 0` → 2.0 | `0x41dd57`, `0x41eb6e` |
| +0xfc | f32 | verstreken tijd in de overgang | `0x41efb2` |
| +0x100 | f32 | voortgang t = +0xfc/+0xf8 (≤ 1) | `0x41ef87` |
| +0x104 | f32 | snelheid voor overgang-type "snelheid" (afstand/snelheid = duur) | `0x41ec0d` |
| +0x108 | u8 | 1 = duur uit snelheid herberekenen (`0x41f9b0` zet 1, `0x41f9d0` zet 0) | |
| +0x109 | u8 | overgang bezig | `0x41efd1` |
| +0x10a | u8 | overgangsmodus: 0 = vloeiend (travelling), 1 = harde cut | `0x41f9f0` |
| +0x10b | u8 | overgang gestart terwijl een andere bezig was (look-at-start uit +0xdc i.p.v. +0xc4) | `0x41eaf7` |
| +0x110 | ptr | sub-camera **mode 8** (TRAJ-camera), 0x280 B, ctor `0x420340` | `0x41e210` |
| +0x114 | ptr | sub-camera **mode 0x10**, 0x2c0 B, ctor `0x420340`, update `0x4203c0` | `0x41e050` |
| +0x118 | ptr | sub-camera **mode 1** = volgcamera ("Center"), 0x9dc B, ctor `0x422170`, update `0x424760` | `0x41dfe0` |
| +0x11c | ptr | sub-camera **mode 2**, 0x328 B, update `0x4254c0` | `0x41e130` |
| +0x120 | ptr | sub-camera **mode 4**, 0x328 B, update `0x425810` | `0x41e1a0` |
| +0x124 | ptr | sub-camera **mode 0x40**, 0x280 B, update `0x420cf0` (quaternion→matrix) | `0x41e280` |
| +0x128 | ptr | sub-camera **mode 0x100**, 0x280 B, update `0x420010` | `0x41e2f0` |
| +0x12c | ptr | sub-camera **mode 0x200**, 0x284 B, update `0x425b80` | `0x41e3a0` |
| +0x130 | ptr | sub-camera **mode 0x20**, 0x390 B, update `0x424bf0`; parameters op `CamMgr+0x61c` (bericht 1110) | `0x41e0c0` |
| +0x134 | u32 | **actieve mode** (bit): 1,2,4,8,0x10,0x20,0x40,0x80,0x100,0x200 | `0x41f5ec` |
| +0x138 | u32 | **mode-index** = 1e argument van `0x41f410` (`mode = 1 << index`, 0..9; gecorrigeerd: niet het 2e argument – dat gaat alleen naar `0x41e450`/`0x4247f0`). Framefunctie test `== 8` (debug-camera 0x100) en `== 2` (mode 4); `0x459090` schakelt erop (3.1) | `0x41f5f2`, `0x401dc3` |
| +0x13c | f32 | dt van dit frame | `0x41f014` |
| +0x140 | Repere | **huidige camera-toestand** (view-matrix + positie op +0x1d0) | `0x41fa30` |
| +0x1dc | Repere | toestand van het vorige frame | `0x41f385` |
| +0x278 | vec3 | doelpunt ("target"): door `0x41f960` elk frame = spelerpos + (0,50,0), daarna door de mode-update overschreven (mode 1: spelerpos) | `0x41f05b`, `0x41f960` |
| +0x284 | vec3 | kijkrichting van de speler (`M->dir`), uit `0x41f960` | `0x41f960` |
| +0x290 | u32 | 1 = camera bevroren/gepauzeerd (`0x41fa40`), 0 = actief (`0x41fa50`, bij elke mode-wissel) | |
| +0x294 | u32 | vorige mode | `0x41f48b` |
| +0x298 | u32 | vorig mode-argument | `0x41f474` |
| +0x29c | Repere | toestand op het moment van de laatste mode-wissel | `0x41f480` |
| +0x338 | struct | parameters `p` van mode 1 (volgcamera, §3): `p+0xc` (+0x344) look-at-offset `L' − pos`; `p+0x18` (+0x350) ptr naar spelerpositie; `p+0x1c` (+0x354) kijkrichting speler (of `Perso+0x34` in behind-mode); `p+0x28` (+0x360) = −1; `p+0x2c` (+0x364) 1 = valt, 2 = stijgt; `p+0x30` (+0x368) 1 = slerp-start; `p+0x3c` (+0x374) Perso-toestand; `p+0x40` quaternion slerp-start; `p+0x50` startpositie; `p+0x5c` (+0x394) `Perso+0x458`; `p+0x68/+0x6c` slerp-duur/-tijd; `p+0x70` (+0x3a8) vlaggen 1 = slerp, 2 = reset, 4 = snel hercentreren; `p+0x74` (+0x3ac) behind-mode | `0x41e48f`, `0x4591a5` |
| +0x3b0 | struct | parameters mode 8 (rail-camera, §6.2): +0x20 (+0x3d0) spelerpositie (elk frame door `0x459090`; bericht 540 zet hier eenmalig de `.ins`-positie), +0x2c kijkrichting, +0x38 railpunt, +0x44 f32 **afstand tot de speler**, +0x4c ptr TRAJ, +0x50 vlaggen | `0x498fe5`, `0x459698` |
| +0x404 | struct | parameters mode 0x10 (+0x404 f32, +0x40c vec3 look-at) | `0x41f13f` |
| +0x43c | struct | parameters mode 2: +4 vec3 camerapositie, +0x10 ptr doelinstantie, +0x20 f32; look-at-resultaat +0x450, +0x45c | `0x498c50` |
| +0x470 | struct | parameters mode 4: idem (+0x484 look-at, +0x490) | `0x498c9f` |
| +0x4a4 | struct | parameters mode 0x40 | |
| +0x508 | struct | parameters mode 0x100 (+0x508 = 1.0, +0x518 = 1.0, +0x528 = 1.0, +0x538 = 15.0) | `0x41e34a` |
| +0x540 | struct | parameters mode 0x200 (+0x54c look-at, +0x558 vec3) | `0x41f280` |
| +0x5d4 | ptr | instantie voor mode 0x80 (camera op marker van een instantie, `0x42feb0`/`0x42fa40`) | `0x41f1ee` |
| +0x5d8 | vec3 | look-at voor mode 0x80 | |
| +0x61c | struct | parameters mode 0x20; +0x28..+0x44: 8 floats via bericht 1110 (defaults 1000, 300, 340, 500, 0, 700, 400, 200) | `0x444b9d` |
| +0x664 | ptr | camera-**instantie** (script-object, bericht 800 zet hem); positie wordt elk frame overschreven | `0x498d93`, `0x41f396` |
| +0x66c | u32 | vlaggen: bit2 (4) = auto-zoom op afstand tot doel aan (`0x41f650`, bericht 710), bit3 (8) = 16:9 letterbox toegestaan | `0x41f696`, `0x41f8d0` |
| +0x670 | f32 | projectie-schaal X (1.0) | `0x41f940` |
| +0x674 | f32 | projectie-schaal Y (0.75 = 4:3 normaal; 0.5625 = 16:9 in letterbox-modi 1/2) | `0x41f940`, `0x41f8d0`, `0x41f910` |
| +0x678 | f32 | **zoomfactor / focal** (default 1.2 = `0x41f680`; bericht 690 zet hem ×0.01 via `0x41f660`) | |
| +0x67c | f32 | resterende **shake**-tijd (`0x41fbb0` zet hem); > 0 → `0x41fbd0` camera-shake (§6.3) | `0x41f2fb` |
| +0x680 | u32 | letterbox-modus: 0 = geen, 1 = letterbox (`0x41f8d0`), 2 = letterbox-variant (`0x41f910`, mode 4) | |
| +0x684 | u32 | vorige letterbox-modus | |
| +0x688 | u32 | ? (viewport-gerelateerd) | `0x41f778` |
| +0x690 | u8 | vlag via bericht 590/600 (`0x41f630`/`0x41f640`) | |
| +0x694 | u32 | gewist door `0x41fbb0` | |

## 2. Per-frame flow

`0x459090` (game-update, 700 instr) roept `CamMgr::Update(dt)` = **`0x41eff0`** aan (`0x459913`).

```c
void CamMgr_Update(CamMgr *m, float dt)              // 0x41eff0
{
    m->dt = dt;                                       // +0x13c
    switch (m->mode) {                                // +0x134, tabel 0x41f3e8/0x41f400
    case 1:    Mode1_Update(m);  m->target = *m->p338.targetPtr(+0x350); m->lookAt = m->p338+0x344..; break;
    case 2:    Mode2_Update(m);  m->target = m->p43c+0x14 (+0x450); m->lookAt = (0, +0x45c, 0); break;
    case 4:    Mode4_Update(m);  m->target = +0x484;  m->lookAt = (0, +0x490, 0); break;
    case 8:    Mode8_Update(m);  m->target = +0x3d0;  m->lookAt = (0, +0x3cc, 0); break;
    case 0x10: Mode10_Update(m); m->target = +0x40c;  m->lookAt = (0, +0x404, 0); break;
    case 0x20: Mode20_Update(m); m->target = +0x624;  m->lookAt = sub130+0x280; break;
    case 0x40: Mode40_Update(m); break;
    case 0x80: /* camera op marker van instantie +0x5d4 */ pos = marker(0x42fa40); m->lookAt = +0x5d8; break;
    case 0x100: Mode100_Update(m); break;
    case 0x200: Mode200_Update(m); m->target = +0x54c; m->lookAt = +0x558; break;
    }
    // ModeN_Update (0x41e740..0x41ea40): kopieer huidige toestand naar sub+0xa0, sub->params = &m->pNNN,
    // roep de eigenlijke update(dt) aan, kopieer sub+4 (0x9c bytes, de nieuwe Repere) terug naar m+0x140.

    if (!m->cutMode /*+0x10a*/ && m->transitionActive /*+0x109*/) Transition_Travelling(m, m->target);  // 0x41eb90
    if (m->shakeTime /*+0x67c*/ > 0) { Shake(m); /*0x41fbd0, §6.3*/ m->shakeTime -= dt; }
    if (m->mode == 4) Letterbox2(m);                   // 0x41f910
    if ((m->prevMode == 4 && m->mode != 4) || (m->prevMode == 0x80 && m->mode != 0x80)) Letterbox0(m); // 0x41f940
    SetupProjection(m);                                 // 0x41f690, zie §5
    m->prevState = m->state;                            // +0x1dc = +0x140
    if (m->camInstance) { camInstance->pos = m->state.pos; 0x4077f0(inst,0) /*hercel*/; 0x434740(&pos,-1,inst); }
}
```

Elke mode-update heeft de vorm `sub->prevState (sub+0xa0) = m->state; sub->params (sub+0x27c) = &m->pXXX; update(dt); m->state = sub->state (sub+4)`.

## 3. Volgcamera (mode 1, "Center")

Sub-camera `C = [CamMgr+0x118]` (0x9dc B, ctor `0x422170`). Parameterblok `p = CamMgr+0x338`
(`C+0x9cc = p`, gezet in `0x41e7c7`). Debugstrings in de code: `'cam_state = CENTER'`,
`'Target Out of world'`, `'Center ColSphere Blind Move'`, `'SubCenter ColWall Move'`.

### 3.1 Invoer: wie vult het parameterblok? (`0x459090`, Game+8 = "camera-besturing")

`0x459090(ctl, dt)` (`ctl+0` = CamMgr, `ctl+4` = Perso, `ctl+8` = vorige Perso-toestand, `ctl+0xc` = bewaarde
richting, `ctl+0x18` = hercentreer-timer, `ctl+0x1c` = dt) draait elk frame vóór `CamMgr_Update`:

1. `0x41f960(CamMgr, Perso->vtable[34]() /*&pos*/, &M->dir)`: `CamMgr+0x278 = pos + (0,50,0)`
   (`0x4a9030`), `CamMgr+0x284 = M->dir` (Mover-kijkrichting `Perso+0x388+0x10`, via `0x445780`).
2. Bij verandering van Perso-toestand (`Perso+0x21c`): nieuwe toestand 3 → `0x459050`; vorige toestand 3 en
   nieuwe ≠ 3, 5 → harde cut naar volgcamera (`0x41f9f0(2)`, `CamMgr+0x368 = 0`, `SetMode(0,0)`).
3. `switch (CamMgr+0x138)` (**= mode-index = 1e argument van SetMode, níet het 2e**; tabel `0x459934`):

| idx | mode | adres | per-frame invoer |
|---|---|---|---|
| 0 | 1 volg | `0x4591a5` | zie hieronder |
| 1 | 2 | `0x45915d` | knop 0xa net ingedrukt (`0x467420`) → geluid 9 (`0x468a00`) ("kan niet") |
| 2 | 4 | `0x459185` | `0x44a650(Perso, &pos)`, `Perso+0x690 = 1` |
| 3 | 8 TRAJ | `0x459698` | `p3b0+0x20 (CamMgr+0x3d0) = Perso-pos`, `+0x3dc = normalize(M->dir)`, vlag 4/8 in `+0x400` (vtable[35]() / `0x44c910`), knop 0xa → geluid 9 |
| 4 | 0x10 | `0x4597c5` | `CamMgr+0x40c = Perso-pos`, `+0x418 = normalize(M->dir)`, vlaggen `+0x438` |
| 5, 6 | 0x20, 0x40 | – | niets |
| 7 | 0x80 | `0x4598c4` | `CamMgr+0x618 |= 2`, `+0x5f0 = 0x44e030(Perso)`, `+0x5e4 = Perso-pos` |
| 8 | 0x100 | `0x4594ea` | debug-vrije camera: toetsen (DIK 0x49/0x4d: `+0x538` ∓ 1 binnen 0..100; 0x78, 0x7d, 0x29/0x35, 0x1c/0x6a, 0x7a, 0x7b, 0x7f, 0x80 → bits 0..7 van `CamMgr+0x53c`), `Perso+0x690 = 1` |
| 9 | 0x200 | `0x459346` | first-person/kijk-mode: muis (`[0x5e6190]` vtable[2]/[3] → `p540+0x28/+0x2c`) of acties 1/0 (×5 / ×−5) en 3/2 (×−5 / ×5); schrijft de resulterende kijkrichting `(−p540+0x3c, 0, −p540+0x44)` genormaliseerd terug in `M+0x34`, `M+0x1c`, `M+0x10` en roept `0x44c080(Perso, p540, 0)` |

**Mode-index 0 (volgcamera)**, `0x4591a5..0x45933b`:

```c
p->targetPtr (+0x18, CamMgr+0x350) = Perso->vtable[34]();        // &Perso-positie
p->+0x28 (CamMgr+0x360)            = -1;
dir = M->dir;                                                     // Perso+0x388+0x10 (0x445780)
p->dir (+0x1c, CamMgr+0x354)       = dir;                         // kijkrichting speler (+vooruit)
st = Perso->state (+0x21c);
if (st == 1 || st == 4 || st == 8) { p->behind (+0x74, CamMgr+0x3ac) = 1; ctl->savedDir = Perso+0x34..0x3c; }
else                                 p->behind = 0;
if (pressed(0xa) && !p->behind) { ctl->timer = 0.5f; ctl->savedDir = Perso+0x34..0x3c; }   // 0x467420
else if (held(0xa))               p->behind = 1;                                           // 0x467400
if (ctl->timer > 0) { ctl->timer -= dt; p->behind = 1; p->flags (+0x70) |= 4; } else p->flags &= ~4;
if (p->behind) p->dir = ctl->savedDir;       // = rij 1 van de Perso-rotatie = −kijkrichting (PERSO_FRAME §2.4)
p->+0x2c (CamMgr+0x364) = 0x44c910(Perso) ? 1 : Perso->vtable[35]() ? 2 : 0;
p->+0x5c (CamMgr+0x394) = Perso+0x458..0x460;   p->+0x3c (CamMgr+0x374) = st;
```

Actie **0xa** is dus de "camera achter de speler"-knop: één tik = 0,5 s hercentreren (sneller, vlag 4),
vasthouden = blijven hercentreren. Er is **geen** vrije camera-rotatie links/rechts in mode 1.
Let op het teken: normaal is `p->dir = +kijkrichting`; in "behind"-mode is het `Perso+0x34` = **−**kijkrichting
(de instantie kijkt langs −dir). `0x424760` negeert `p->dir` nog eens (zie 3.2), zodat `F` in behind-mode
de echte kijkrichting is en de camera op `T − F·afstand` (achter de speler) uitkomt.

### 3.2 Velden van `C`

| offset | betekenis | default (ctor `0x422170`) |
|---|---|---|
| +0x004 | Repere-resultaat (naar `CamMgr+0x140`) | |
| +0x0a0 | Repere vorige toestand (uit `CamMgr+0x140`) | |
| +0x280 | **afstand** voor behind-mode/reset (bericht 680) | 400.0 |
| +0x28c | extra verlaging van het kijkpunt bij vlag 1/2 (0..150) | 0 |
| +0x294 | byte: +0x28c groeit | 0 |
| +0x296 / +0x297 | byte-vlaggen uit `p+0x2c` == 1 / == 2 (per frame gewist) | |
| +0x298 | wereldcel van het doel (`0x40aba0`) | |
| +0x2a4 | **toestand**: 0/1 = CENTER, 2 = doel onzichtbaar (pad volgen), 4 = terug naar bewaard punt | 0 |
| +0x2a8 | 1 = behind-mode dit frame (`p+0x74 != 0`) | 0 |
| +0x2ac | 1 = look-at-matrix herberekenen (`0x422760`) | 1 |
| +0x2e4 | vec3 botsingscorrectie van dit frame (uit `0x422e30`) | |
| +0x2f0.. | 100 × vec3 kruimelpad (toestand 2); +0x7a0/+0x7a4/+0x7a8 tellers | |
| +0x7b0 | dt | 1.0 |
| +0x7b4 | wereldcel van de camera | −1 |
| +0x7b8 | 1 = camera was aan het inhalen (hysterese) | 0 |
| +0x7d8 | **hoogte** boven het doelpunt (bericht 670) | 180.0 |
| +0x7dc | hoogte van het doelpunt T boven de spelerpositie | 120.0 |
| +0x7e0 / +0x7e4 | **max. afstand** (begin inhalen / tot waar inhalen) (bericht 680) | 400.0 / 400.0 |
| +0x7e8 | Repere die wordt opgebouwd (view-matrix) | |
| +0x878 | camerapositie (= Repere+0x90) | |
| +0x884 | 3×3 look-at-rotatie (`0x4223b0`) | |
| +0x920 | **F** = normalize(−`p->dir`) | |
| +0x92c / +0x938 | F en P van het vorige frame | |
| +0x944 | **P** = camerapositie | |
| +0x950 | P aan het begin van het frame (fallback als nieuwe P buiten de wereld valt) | |
| +0x95c | (0, 120, 0) | |
| +0x968 | **T** = spelerpos + (0, `+0x7dc`, 0) | |
| +0x974 | T van het vorige frame; +0x980 = T − T_vorig | |
| +0x98c | **L** = spelerpos + (0, 140, 0) (`0x4aa1b4`) = kijkpunt | |
| +0x998 | **move** = gewenste verplaatsing van dit frame; +0x9a4 kopie | |
| +0x9b0 | bewaard punt voor toestand 4 | |
| +0x9bc | up-vector voor look-at | (0, −1, 0) |
| +0x9c8 | byte: in reset-lus (`0x424200`) | |
| +0x9cc | `p` | |

### 3.3 Per frame: `0x424760(dt)` → `0x422790(&pos, -1, &M)`

```c
void Center_Update(C, dt) {                      // 0x424760
    C->dt = dt;                                  // +0x7b0
    if (p->+0x2c == 1) C->f296 = 1; else if (p->+0x2c == 2) C->f297 = 1;
    C->behind (+0x2a8) = p->behind != 0;
    // lokale matrix waarvan alleen kolom 2 gevuld wordt: (m02,m12,m22) = −p->dir
    Center_Step(C, p->targetPtr, p->+0x28, &M);  // 0x422790
}
void Center_Step(C, vec3 *pos, int cell, M) {    // 0x422790
    if (p->flags & 1) { p->slerpT (+0x6c) += dt; if (slerpT >= p->slerpDur (+0x68)) { slerpT = slerpDur; p->flags &= ~1; } }
    C->corr (+0x2e4) = 0;  C->move (+0x998) = 0;
    F = normalize(M.m02, M.m12, M.m22);          // +0x920
    T = *pos + (0, C->+0x7dc /*120*/, 0);        // +0x968
    L = *pos + (0, 140.0, 0);                    // +0x98c  (0x4aa1b4)
    C->camCell = FindCell(P);                    // 0x40aba0([0x4c93b0]+0x14, x,y,z)
    c = FindCell(T); if (c == -1) return /*ongewijzigd, 'Target Out of world'*/;  C->cell = c;
    Pstart (+0x950) = P;
    if (state == 1 && !behind) state = 0;
    if (state != 2,3,4) {
        Ray(P, T, -1);                           // 0x4359b0 → globals 0x53a554 (hit-soort), 0x53a558 (t), 0x53a560 (object)
        if ([0x53a554] != 0 && !HitIsType7()) {  // 0x422140: hit-soort 2 én object-categorie 7 telt niet als blokkade
            state = 2;  +0x7a0 = 2; pad[0] = P; pad[1] = T_vorig; pad[2] = T; +0x7a4 = +0x7a8 = 0;
            P += T_vorig; P -= T;                // 0x43ffd0 / 0x440020: P verschuift met −(T − T_vorig)?? (zie 3.6)
        }
    }
    dT (+0x980) = T − T_vorig;
    switch (state) { case 0: case 1: Center_Normal(C); break;        // 0x4231e0
                     case 2: Center_Occluded(C, M); break;           // 0x423ab0
                     case 4: /* 3.7 */ break; }
    P += move; P += corr;                        // 0x422cb3, 0x422cc2
    if (FindCell(P) == -1) P = Pstart;
    LookAt(C);                                   // 0x422760 → 0x4223b0 (als +0x2ac == 1)
    Repere = [I | −P] · [R | 0];                 // 0x437710, 0x437740;  Repere+0x90 = P
    f296 = f297 = 0;  Fprev = F; Pprev = P; Tprev = T;  C->result (+4) = Repere;
}
```

### 3.4 Normale toestand `0x4231e0` (CENTER)

```c
dy = T.y − P.y;
if (p->flags & 2) { Center_Reset(C) /*0x424940*/; p->flags &= ~2; return; }
if (behind) {                                    // 0x423264: naar het punt recht achter de speler
    D = T − F̂·C->dist(+0x280);                   // F̂ = F genormaliseerd (incl. y)
    d = D − P;  len = |d|;  d.y-loze normalisatie: d̂ = d / sqrt(d.x²+d.y²+d.z²)  // 0x423345.. (zie noot)
    move = d̂ · len · dt · ((p->flags & 4) ? 7.0 : 3.0);      // 0x4aa1b8 / 0x4a988c
} else Center_Distance(C);                       // 0x4242d0, zie 3.5
Center_BehindArc(C);                             // 0x423ed0 (alleen actief in behind-mode, zie 3.5b)
if (f297 && (P.y − T.y) < 300.0)  move.y = T.y − Tprev.y;          // 0x4a986c: camera volgt y 1:1
else if (!f297)                   move.y = (dy + C->height(+0x7d8)) · dt · 6.0;   // 0x4a9888
else                              move.y = 0;
if (p->flags & 1) { /* 0x423485: slerp-overgang, zie 3.8 */ }
switch (Center_Collide(C)) { ... }               // 0x422e30, zie 3.6
```

Noot behind-mode: `0x423323..0x4233cb` normaliseert `d` en vermenigvuldigt weer met `|d|`, netto
`move = (D − P)·dt·k` met k = 3.0, of 7.0 als `p->flags & 4` (de 0,5 s na een tik op knop 0xa); `move.y` wordt
daarna toch overschreven.

De verticale as is dus een **exponentiële nadering**: `P.y += (T.y + hoogte − P.y) · 6·dt`, met
`T.y + hoogte = speler.y + 120 + 180 = speler.y + 300`. Bij springen volgt de camera de speler-y met
deze tijdconstante van 1/6 s (geen aparte sprong-logica, behalve vlag `f297`/`f296`, zie 3.9).

### 3.5 Horizontale afstandsregeling `0x4242d0` (niet-behind)

De camera draait **niet** actief achter de speler; hij wordt als aan een elastiek meegetrokken
("lazy follow"). Alleen de xz-afstand tot T wordt geregeld, met een dode zone van 100 eenheden:

```c
a = L − P; a = xzNormalize(a);  f = xzNormalize(F);          // 0x424720 normaliseert alleen x,z
k = (dot(a, f) > 0.7f /*0x4aa1d8*/) ? 10.0f : 1.0f;          // F = −kijkrichting: speler loopt náár de camera toe → 10× sneller wijken
v = T − P; v.y = 0; d = |v|;
dMax = C->+0x7e0 (400); dStop = C->+0x7e4 (400); dMin = dMax − 100.0f /*0x4a9010*/;
if (d < dMin) {                                              // te dichtbij: achteruit
    w = normalize(v) · (dMax / d) · dt · k · 66.6667f;       // 0x4aa1c4
    if (d + |w| > dMin) w = normalize(w) · (dMin − d);       // niet voorbij de rand van de dode zone
    C->catchUp (+0x7b8) = 0;  move.xz = −w.xz;
} else if (d > dMax || (d > dStop && C->catchUp == 1)) {     // te ver: inhalen
    w = normalize(v) · (d / dMax) · dt · 433.333f;           // 0x4aa1bc
    if (d − |w| < dStop) w = normalize(w) · (d − dStop);
    C->catchUp = 1;  move.xz = w.xz;
} else move.xz = 0;                                          // dode zone 300..400
move.y = (T.y − P.y + C->height) · 0.02f;                    // 0x4aa1c0; wordt in 0x4231e0 overschreven (3.4)
```

Snelheden: wijken ≈ 66,7·(400/d) eenheden/s (×10 als de speler naar de camera kijkt/loopt), inhalen ≈
433·(d/400) eenheden/s (speler loopt 600/s → de camera raakt bij rennen iets achter en d groeit tot
≈ 554 waar 433·d/400 = 600). Alles is dt-geschaald (lineair, geen exponentiële demping) behalve y.

### 3.5b Behind-mode-correctie `0x423ed0` (alleen als `C->behind`)

* Als de speler stilstaat (`|T − Tprev|xz < 0.5`), niet in de reset-lus, en `| |T−P|xz − dist | < 10.0`
  (`0x4aa1d0`, double): als `((T−P) × F).y` en `((T−(P+move)) × F).y` van teken wisselen (de stap zou over de
  lijn "recht achter de speler" heen schieten) → `move = 0`.
* Als de stap de cirkel met straal `dist ± 5.0` (`0x4a9884`) rond T kruist (van binnen `dist−5` naar buiten
  `dist+5` of omgekeerd): los `|P + s·move − T|xz = dist` op (A = |move|xz², B = 2·(P−T)·move, C = |P−T|xz² − dist²,
  `s = (−B ± √(B²−4AC))/2A`, de wortel met de kleinste |s|) en `move *= s` – de camera blijft op de cirkel.

### 3.6 Botsing en zichtlijn

Drie mechanismen, alle in `0x4231e0`/`0x422790`:

1. **Bol-tegen-wereld** `0x422e30`: `0x434820(40.0)` zet de botsingsstraal (`[0x4b3118]`), dan
   `0x439c50(&out, &P, -1, &(P+move), 35.0, 0)`: het traject P → P+move wordt in `n = floor(len/35)+1` stappen
   afgelopen; na elke stap `0x407340(out, 40.0, -1)` (statische bol-test); bij contact (`[0x4c4bd0] != 0`)
   wordt de uitduwvector `[0x4c4bb4..0x4c4bbc]` bij `out` opgeteld. Resultaat ≠ 0 → `C->corr (+0x2e4) = out − (P+move)`
   en returnwaarde 2; anders 0. De camera is dus een **bol met straal 40** die langs muren glijdt.
2. **Zichtlijn-veto** `0x423a40(newP, T)`: straal `0x4359b0(newP, T, -1)`; toegestaan als er niets geraakt wordt
   (`[0x53a554] == 0`) of als het geraakte object categorie 7 heeft (`0x422140`: `[0x53a554] == 2` en
   `([0x53a560]->vtable[4]()[0] & 0x1f) == 7`). Anders (`'Center ... Blind Move'`): `move = 0` en
   `0x422f10` ("SubCenter": berekent een alternatieve stap `(T−P)·dt·0.1` maar gebruikt die uiteindelijk niet;
   netto-effect: `corr = 0` tenzij de camera op zijn huidige plek een muur raakt). Ook `move = corr = 0` als
   `P+move(+corr)` buiten de wereld valt (`0x40aba0` = −1). **De camera weigert dus elke stap waarna hij T niet meer ziet.**
3. **Doel verdwijnt achter geometrie** (straal P → T geblokkeerd aan het begin van het frame, `0x4229b8`):
   toestand 2 "FIND" (`0x423ab0`). Kruimelpad `pad[]` (`C+0x2f0`, max 100 punten): start `pad = {P, Tprev, T}`, n = 2.
   Elk frame: als `pad[n]` → T geblokkeerd is: (als ook Tprev → T geblokkeerd is: opgeven, `state = 0`,
   `P = T + (1, 1, 100)`), `pad[n] = Tprev; n++`. `pad[n] = T`. De camera loopt het pad af:
   `u (+0x7a8) += 0.04` (`0x4aa1cc`, **per frame, niet dt-geschaald**); bij `u ≥ 1`: `seg (+0x7a4)++, u = 0`;
   `P = pad[seg] + (pad[seg+1] − pad[seg])·u`. Zodra P → T weer vrij is: `state = 0`. (Bijproduct: `+0x2b4 =
   normalize(+0x2b4 + normalize(T−P)·0.15)`, `0x4aa1c8`.) De camera volgt dus letterlijk het spoor van de speler
   om de hoek, 25 frames per segment.

Er is **geen** "camera naar voren schuiven langs de straal speler→camera" in de normale update. Dat gebeurt
alleen bij de **reset** `0x424940` (`p->flags & 2`): `D = T − F̂·dist + (0, height, 0)`; straal `0x4359b0(T, D, cel)`;
als `0 < t < 1.1` (`[0x53a558]`, `0x4aa168`): `t = min(t, 0.999)·0.999` (`0x4aa1dc`), `D = T + (D−T)·t`;
`move = D − P`, `corr = 0`. (Bit 1 van `p->flags` wordt in de gevonden code nergens gezet → dood pad, zie §8.)

Toestand 4 (`0x422ab7`): beweeg P naar het bewaarde punt `+0x9b0` met `(Q − P)·0.02·dt·50` (`0x4aa1b0`,
`0x4a9030`) zolang `|Q−P| > 3.0` (`0x4a988c`), anders `P = Q`; herhaal zolang P buiten de wereld ligt. Ook
toestand 4 wordt nergens gezet (dood pad).

### 3.7 Kijkdoel en look-at-matrix `0x4223b0`

```c
L' = L;                                            // spelerpos + (0,140,0)
if (f296 || f297) { C->drop (+0x28c) = min(drop + dt·450.0 /*0x4aa1a8*/, 150.0 /*0x4a9754*/); }
else                C->drop *= 0.94f;              // 0x4aa1ac, per frame (niet dt-geschaald)
L'.y −= C->drop;
delta = L'.y − prevLy [0x4c83a0];
if (!f296 && !f297 && [0x4c93a8] /*niet eerste frame*/ && (delta >= 9.0 || delta <= −9.0)) smooth [0x4c93ac] = 1;   // 0x4aa1a4/0x4aa1a0
if (smooth) { L'.y = prevLy + delta·dt·10.0 /*0x4a9750*/; if (|delta| < 0.1) smooth = 0; }
prevL [0x4c839c..] = L';
fwd   = normalize(L' − P);
right = normalize(up × fwd);      up = C+0x9bc = (0, −1, 0)
up2   = normalize(fwd × right);
R (+0x884) = kolommen (right, up2, fwd):  R[i][0] = right[i], R[i][1] = up2[i], R[i][2] = fwd[i]
if (p->flags & 1) R = matrix(slerp(p->q0 (+0x40), quat(R), p->slerpT / p->slerpDur));   // 0x4404b0, 0x440dd0, 0x440370
p->lookOffset (+0xc, CamMgr+0x344) = L' − *p->targetPtr;
```

Het kijkdoel is dus **spelerpositie + (0, 140, 0)**, zonder vertraging (alleen sprongen in y ≥ 9 per frame
worden met 10·dt gefilterd). **Springen**: `f296` = `0x44c910(Perso)` = jumper-toestand (`Perso+0x334+0x14`) 3 of 4 =
**vallen**; `f297` = `Perso->vtable[35]()` (`0x44c940`) = jumper-toestand 0, 1 of 7 = **stijgen/top van de sprong**
(PERSO_JUMP §1.3). In beide gevallen zakt het kijkpunt met 450/s tot max. 150 onder `speler + 140` (de camera blijft
dus ongeveer naar de afzetplek/grond kijken in plaats van de sprong mee te knikken) en veert daarna terug met ×0.94 per
frame. Tijdens het stijgen (`f297`) gaat de camerapositie bovendien 1:1 mee omhoog met de speler (`move.y = ΔT.y`, zolang
`P.y − T.y < 300`), tijdens vallen en lopen geldt de 6·dt-nadering (3.4).

Met `up = (0,−1,0)` is voor `fwd = +z`: `right = (−1,0,0)`, `up2 = (0,−1,0)`. In een rechtshandige y-omhoog-wereld
is −x precies de rechterkant van een kijker die langs +z kijkt, en `up2` wijst omlaag: camera-ruimte is
**x = schermrechts, y = schermomlaag, z = vooruit** (zuivere rotatie, det = +1; geen spiegeling). Dat sluit direct aan
op schermcoördinaten (§5). View: `v_cam = (v − P) · R` (rijvector).

### 3.8 Start/reset van de volgcamera `0x41e450(arg)` → `0x4247f0(arg)`

Bij `SetMode(0, arg)`: `p->flags &= ~1`; `C->prev = CamMgr-toestand`; `state = 0` (`0x422350`); als `CamMgr+0x18`
(instantie) gezet: `p->dir = (−i[0x34], i[0x38], −i[0x3c])` (= +kijkrichting van die instantie). Dan `0x4247f0(arg)`:

* `p+0x30 (CamMgr+0x368) == 1` → **slerp-start** (`0x424809`): `0x423e10` neemt de huidige CamMgr-toestand over
  (P = huidige camerapositie), `p->flags |= 1`, `slerpDur = 0.5`, `slerpT = 0`, `q0 = quat(huidige rotatie)`
  (`0x4404b0`), `p+0x50 = huidige positie`, P-start = `pos + (dir.x, 100, dir.z)`. Tijdens de slerp (3.4,
  `0x423485..0x42367d`) geldt met `s = slerpT/slerpDur`, `S = p+0x50`, `D = T − F̂·dist + (0,height,0)`:
  `move = T + normalize(S + (D−S)·s − T) · ((1−s)·|S−T| + s·dist) − P` (boog rond T van start naar behind-punt).
  Berichten 500/501 en `0x459090` zetten `+0x368 = 0`; wie er 1 in zet is niet gevonden (§8).
* anders (`0x4248b3`): `P = pos − (dir.x, −100, dir.z)`; `p->flags &= ~1`; `p+0x2c = 0`; `behind = 1`;
  `Center_Update(0.1)`; **`0x424200(arg)`**: 100 iteraties van `Center_Step` met `dt = 0.04` en
  `F = (arg ? +F : −F)` zodat de camera "uitdempt" naar zijn rustpositie; daarna `behind = 0`, `p+0x30 = 0`.
  `arg = 0` (bericht 500): camera **achter** de speler; `arg = 1` (bericht 501): camera **vóór** de speler.

### 3.9 Samenvatting gedrag

| vraag | antwoord |
|---|---|
| afstand | xz-afstand tot T wordt tussen 300 en 400 gehouden (`+0x7e0` = 400, dode zone 100); bericht 680 zet hem |
| hoogte | `P.y → speler.y + 120 + 180` met `6·dt` per frame; bericht 670 zet de 180 |
| kijkdoel | speler + (0,140,0), onvertraagd |
| smoothing | xz: lineaire snelheid ∝ afstand (433·d/400 /s inhalen; 66,7·400/d /s wijken, ×10 als speler naar camera kijkt); y: exponentieel 6/s |
| springen | stijgen (jumper 0/1/7): camera-y 1:1 mee, kijkpunt zakt 450/s tot −150; vallen (3/4): y met 6/s, kijkpunt zakt ook; daarna kijkpunt-offset ×0.94 per frame terug |
| botsing | bol r = 40 met uitduwen (stapjes van 35), zichtlijn-veto, kruimelpad bij verlies zichtlijn |
| input | alleen actie 0xa: camera achter de speler (3·dt, eerste 0,5 s 7·dt); toestanden 1, 4, 8 van Perso forceren behind-mode |
| first-person | aparte mode 0x200 (index 9, bericht 550), niet in de volgcamera |

## 4. Script-camera's (.ins) en berichten

`.ins`-camera-object (0x2c B, `0x498b90`): `+0xc` positie, `+0x28` TRAJ (→ vtable `0x4aa224`).
Berichthandler `0x498bd0` (vtable slot 22), `this` = het camera-object, `msg` = {id, args…}.
Byte-tabellen `0x498e84` (500–580) en `0x498f00` (600–800).

| id | args | actie | adres |
|---|---|---|---|
| 500 | cam | `CamMgr+0x368 = 0; SetMode(0, 0)` (mode 1<<0 = **1**, volgcamera; arg 0 = start **achter** de speler, 3.8) | `0x498c2b` |
| 501 | cam | `CamMgr+0x368 = 0; SetMode(0, 1)` (volgcamera, arg 1 = start **vóór** de speler) | `0x498c06` |
| 510 | cam, f, target (volgorde gecorrigeerd, zie CAMERA_SCRIPT.md §2.1) | **mode 2** (vaste camera op `.ins`-positie kijkend naar instantie): `p43c.pos = cam->pos; p43c.target = inst[target]; p43c.f(+0x20) = (float)f; SetMode(1, 0)` | `0x498c50` |
| 520 | cam, f, target | **mode 4**: idem in `p470`; `SetMode(2, 0)` | `0x498c9f` |
| 530 | cam | `SetMode(4, 0)` → mode **0x10** | `0x498d03` |
| 540 | cam, d | alleen TRAJ-camera (`0x498fe5`): `p3b0.traj(+0x4c) = cam->traj; p3b0.dist(+0x44) = (float)d` (afstand rail-camera ↔ speler, §6.2); `p3b0.pos(+0x20) = cam->pos; SetMode(3, 0)` → mode **8** | `0x498fe5` |
| 550 | cam | `SetMode(9, 0)` → mode **0x200** | `0x498cee` |
| 560 | cam, v | overgang: snelheid `+0x104 = (float)v`, `+0x108 = 1` (duur = afstand/snelheid) | `0x498d18` → `0x41f9b0` |
| 570 | cam, t | overgang: duur `+0xf8 = t·0.01` s, `+0x108 = 0` | `0x498d30` → `0x41f9d0` |
| 580 | cam, mode | overgangsmodus: 1 = vloeiend (`+0x10a=0`), 2 = harde cut (`+0x10a=1`) | `0x498d4e` → `0x41f9f0` |
| 590 | cam | `+0x690 = 1` | `0x498d63` → `0x41f630` |
| 600 | cam | `+0x690 = 0` | `0x498db8` → `0x41f640` |
| 650 | cam | race-info aan: bewaar volgcamera-afstand/hoogte (+0x280, +0x7d8 van sub118) in `CamMgr+4/+8`, reset volgcamera (`0x422350`, `0x4247f0(0)`) | `0x498df9` → `0x41fab0` |
| 660 | cam | race-info uit: herstel afstand/hoogte | `0x498e0a` → `0x41fb00` |
| 670 | cam, h | volgcamera-**hoogte**: `sub118+0x7d8 = (float)h` | `0x498dc9` → `0x41fa60` |
| 680 | cam, d | volgcamera-**afstand**: `sub118+0x7e0 = +0x7e4 = +0x280 = (float)d` | `0x498de1` → `0x41fa80` |
| 690 | cam, z | zoomfactor `+0x678 = z·0.01` (alleen als > 0) | `0x498e1b` → `0x41f660` |
| 700 | cam | zoomfactor terug naar 1.2 | `0x498e39` → `0x41f680` |
| 710 | cam | auto-zoom aan (`+0x66c |= 4`) | `0x498e4a` → `0x41f650` |
| 800 | cam, inst | `CamMgr+0x664 = inst[inst]` (camera-instantie die de positie volgt) | `0x498d93` |

`SetMode(bitIndex, arg)` = **`0x41f410`** (`mode = 1 << bitIndex`): waarschuwt als geen
overgangsmodus (+0x10) of -parameters (+0xc) gezet zijn; bewaart `prevMode/prevArg/prevState`
(+0x294/+0x298/+0x29c); zet `+0x14 = 1`; als overgangsmodus vloeiend (+0x10a == 0): start
overgang (`+0x109 = 1`, `0x41eaa0` bevriest de huidige toestand in +0x1c en het look-at-doel in
+0xd0; duur < 0 → 2.0; +0xfc = 0, +0x100 = 0); bij harde cut: +0x109 = +0x10b = 0. Daarna de
mode-specifieke init (`0x41e450` mode 1, `0x41e520` mode 2, `0x41e560` mode 4, `0x41e5b0` mode 8,
`0x41e630` mode 0x10, `0x41e4e0` mode 0x20 (+ letterbox uit), `0x41e5f0` mode 0x40,
`0x41e670` mode 0x100 (+ cut), `0x41e6c0` mode 0x200 (+ cut), mode 0x80: letterbox aan als
`+0x618 & 2`). Ten slotte `+0x134 = mode`, `+0x138 = bitIndex` (het 2e argument gaat alleen naar de mode-1-init `0x41e450`).

De framefunctie `0x4019c0` (berichtdispatch) wist +0xc/+0x10/+0x14 vóór het verwerken van de
scriptberichten en waarschuwt erna als overgangsparameters/-modus zijn gezet zonder mode-wissel.

## 5. Projectie

Het spel doet **eigen (software) transformatie en projectie**; D3D krijgt schermcoördinaten + rhw. Er is dus geen
D3D view-/projectiematrix; de "projectie" bestaat uit drie schaalfactoren + een viewport.

### 5.1 `0x41f690` CamMgr::SetupProjection (elk frame aan het eind van `0x41eff0`)

```c
if (m->flags66c & 4) {                                  // auto-zoom (bericht 710)
    d = |m->target(+0x278) − m->state.pos(+0x1d0)|;     // target = spelerpos + (0,50,0), zie 0x41f960
    t = d < 300 ? 0 : d > 3000 ? 1 : (d − 300) · (1/2700);      // 0x4a986c, 0x4aa170, 0x4aa16c
    m->zoom (+0x678) = (1 − t)·1.1 + t·0.2;             // 0x4aa168, 0x4a9760
}
aspect = m->sx / m->sy;                                  // +0x670 / +0x674 = 1/0.75 = 4:3 (of 1/0.5625 = 16:9)
W = [0x5e8650]+4; H = [0x5e8650]+8;                      // schermresolutie
Hview = (4.0/W) / (3.0/H) · (W / aspect) = 4H / (3·aspect);     // 4:3 → H; 16:9 → 0.75·H
0x437a30(&m->state, 1/m->sx, 1/m->sy, m->zoom);          // → [0x5ac894] = 1/sx, [0x5ac88c] = 1/sy, [0x5ac890] = zoom
if ((float)W / H > aspect)      Viewport(W/2, H/2, W + 0.5, H + 0.5);                      // 0x437c40(0x4c5350, cx, cy, w, h)
else if (m->letterbox == 2)     Viewport(W/2, (H + Hview)/4, W + 0.5, Hview + 0.5);        // beeld naar boven geschoven
else                            Viewport(W/2, (H + 1)/2, W, Hview + 0.5);
```

`0x437c40(vp, cx, cy, w, h)`: `vp+8 = cx, +0xc = cy, +0x10 = w, +0x14 = h, +0 = w/2 − 1, +4 = h/2 − 1`.
`0x437a30` slaat alleen de drie globals op (de `this` wordt niet gebruikt).

### 5.2 Van Repere naar clip-ruimte: `0x4379a0`, `0x437a50`, `0x4840f0`

De framefunctie (`0x401940` → `0x42a680(level, &cameraRecord, …)`) roept op de gekopieerde Repere:

* **`0x4379a0`**: clip-matrix op `Repere+0x30` (3 rijen × 4: x, y, z):
  `rij_x = [0x5ac894]·(R[0][0], R[1][0], R[2][0], T.x)`, `rij_y = [0x5ac88c]·(kolom 1, T.y)`,
  `rij_z = [0x5ac890]·(kolom 2, T.z)`.
* **`0x437a50`**: inverse schaal op `Repere+0x60` (kolommen van R × `1/[0x5ac894]`, `1/[0x5ac88c]`, `1/[0x5ac890]`)
  en `Repere+0x84 = Repere+0x90` (positie) – voor terugprojectie/billboards.
* **`0x4840f0(renderer [0x5e86ac], R, pos, scale)`**: dezelfde matrix als 4×4 met stride 0x10 op `renderer+0x114`
  (translatie `−pos·R` op `+0x144..0x14c`, daarna per kolom × scale), inverse op `+0x154`, positie op `+0x184`.
  `0x4843b0` zet het renderer-viewport: centrum `(vp+8, vp+0xc)` → `+0x194/+0x198`, grootte
  `(vp+0x10 − 2.0, vp+0x14 − 2.0)` → `+0x1a0/+0x1a4` (`0x4a9870` = 2.0).

Dus met `v_cam = (v − P)·R` (camera-ruimte: **x = rechts, y = omlaag, z = vooruit**; zie 3.7):

```
x_clip = x_cam / sx  = x_cam            (sx = 1.0)
y_clip = y_cam / sy  = y_cam · 1.3333   (sy = 0.75;  16:9-letterbox: 0.5625 → · 1.7778)
w      = z_cam · zoom = z_cam · 1.2     (zoom default 1.2; bericht 690: z·0.01; auto-zoom 1.1 … 0.2)
zichtbaar ⇔ |x_clip| ≤ w en |y_clip| ≤ w          (clipcodes 1/2/4/8 in 0x47b372..0x47b3c6; bol-test 0x437b00 met marge r·1.4142)
scherm.x = cx + (x_clip / w) · 0.5 · vpW ;  scherm.y = cy + (y_clip / w) · 0.5 · vpH ;  rhw = 1/w     (0x47b3ce..0x47b429)
```

**FOV**: `tan(hfov/2) = zoom·sx = 1.2` → **horizontaal 100.4°**; `tan(vfov/2) = zoom·sy = 0.9` → **verticaal 83.97°**
(4:3). De zoomfactor is dus de *tangens van de halve horizontale hoek* (groter = wijder). In letterbox (sy = 0.5625):
verticaal 2·atan(0.675) = 68.0° op een viewport van 0.75·H. Auto-zoom ver weg (zoom 0.2): hfov 22.6°.

Voor een OpenGL-herimplementatie: `gluLookAt(P, L', up = (0,1,0))` levert exact dezelfde rechts/boven-assen
(`right = (0,−1,0) × fwd = fwd × (0,1,0)`; de wereld is rechtshandig, er wordt **niet** gespiegeld), en
`gluPerspective(fovy = 83.97°, aspect = 4/3)`. Near/far: in de gelezen code zit alleen de test op de vier
zijvlakken; near/far-afhandeling is niet gevonden (§8).

## 6. Overgangen, rail-camera en camera-shake

### 6.1 Overgang tussen twee modes ("travelling") `0x41eaa0` + `0x41eb90`

Een overgang is **geen** beweging langs een TRAJ maar een lineaire interpolatie van de bevroren oude camera naar
de (bewegende) camera van de nieuwe mode. Alleen actief als overgangsmodus = vloeiend (bericht 580 arg 1).

* Start (`0x41eaa0`, vanuit `SetMode`): `m->from (+0x1c) = [I | pos]·prevState` (rotatie van de oude camera, `+0xac` =
  oude positie); `lookFrom (+0xd0) = lookOffset (+0xc4)` van de oude mode (of het lopende geblende `+0xdc` als er al een
  overgang bezig was, `+0x10b`); `dur (+0xf8) ≤ 0 → 2.0`; `elapsed (+0xfc) = 0`, `t (+0x100) = 0`.
* Elk frame ná de mode-update (`0x41eb90(m, vec3 target)`, target = `m+0x278`-waarde van de mode, by value):

```c
delta = m->state.pos − m->fromPos;                       // nieuwe mode-positie van dit frame − startpositie
if (m->durFromSpeed (+0x108)) { m->dur = (m->speed (+0x104) > 0) ? |delta| / m->speed : 2.0; m->durFromSpeed = 0; }
pos = m->fromPos + delta · m->t;                         // lineair, geen easing
if (m->elapsed == 0 && m->mode == 1) {                   // naar de volgcamera: look-offsets zijn daar relatief t.o.v. de speler
    if (m->prevMode == 0x80)  m->lookFrom = m->+0x5d8 − *p338.targetPtr;
    if (m->prevMode == 0x200) m->lookFrom = m->+0x558 − *p338.targetPtr;
}
m->lookCur (+0xdc) = m->lookFrom + (m->lookOffset (+0xc4) − m->lookFrom) · m->t;
look = target + m->lookCur;
fwd = normalize(look − pos); right = normalize((0,−1,0) × fwd); up2 = normalize(fwd × right);
m->state = [I | −pos]·[R(right, up2, fwd) | 0]; m->state.pos = pos;
m->t = min(m->elapsed / m->dur, 1);  m->elapsed += dt;  if (m->elapsed > m->dur) m->active (+0x109) = 0;
```

`+0xc4` ("look-at van de actieve mode" in §1.2) is dus een **offset t.o.v. het doelpunt `+0x278`** (mode 1: `L' − spelerpos`;
modes 2/4/8/0x10: alleen een y-offset), niet een absoluut punt. Duur: default 2.0 s; bericht 570: `t·0.01` s; bericht 560:
`afstand / snelheid` (berekend op het eerste frame). Interpolatie van positie en kijk-offset is **lineair in de tijd**; de
rotatie wordt niet geïnterpoleerd maar elk frame opnieuw als look-at opgebouwd. Harde cut (580 arg 2): geen overgang,
de nieuwe mode bepaalt direct de toestand.

**Terug naar de volgcamera**: script stuurt 560/570 + 580 en dan 500 (achter de speler) of 501 (vóór de speler);
`SetMode(0, arg)` reset de volgcamera (3.8: 100 voorsimulatiestappen zodat hij al op zijn rustplek staat) en de overgang
interpoleert ernaartoe. De engine zelf schakelt terug met een harde cut wanneer Perso toestand 3 (first-person) verlaat
(`0x45910e`) en bij respawn (PERSO_FRAME: `0x41f9f0(2)`, `+0x368 = 0`, `SetMode(0,0)`).

### 6.2 Mode 8 = rail-camera op een TRAJ (init `0x420e10`, update `0x421570`)

Bericht 540 `(cam, d)` zet `p3b0.traj (+0x4c)`, `p3b0.+0x44 = (float)d`, `SetMode(3, 0)`. Anders dan §4 suggereerde is `d`
**geen snelheid maar een afstand**, en `p3b0+0x20` wordt elk frame door `0x459090` (index 3) overschreven met de
**spelerpositie** (`+0x2c` = genormaliseerde kijkrichting, `+0x50` bit 2/3 = jumper stijgt/valt):

* TRAJ-punten: `traj+0` low16 = aantal, `traj+0x10` = array met stride 0x10, xyz op `+4/+8/+0xc`; `traj+0xc` = totale lengte (`0x437ca0`).
* Speler-y wordt gefilterd: `y = (1−k)·y + k·y_vorig`, `k = 0.95^(30·dt)` (`0x4aa190` double, `0x4aa18c`, pow = `0x4995c0`);
  tijdens stijgen/vallen (`flags & 0xc`) blijft y bevroren op de vorige waarde (`0x4215ad..0x421602`).
* Per segment `0x421c00(A, B, speler, d²…)`: snijpunten van het segment met de **bol met straal `d` rond de speler**
  (kwadratische vergelijking, discriminant < 0 → geen); uit de kandidaten wordt er één gekozen (dichtst bij de vorige
  camerapositie `C+0x94`; details niet volledig gelezen). Geen kandidaat → `0x420e40`: **dichtstbijzijnde punt op de
  polylijn** tot de speler (projectie op elk segment, geklemd op de eindpunten, kleinste afstand²).
* Het railpunt `p3b0+0x38` beweegt naar het gekozen punt met max. `1000·dt` per frame (`0x4aa188`); op het eerste
  frame (`+0x50` bit 0 = 0) springt het er direct heen. Als `0x428ce0` (wereldcel) slaagt: `C->pos = p+0x38`.
* Oriëntatie: look-at van de camerapositie naar `p+0x20` (speler, gefilterde y) met `0x41af10`-kruisproducten
  (`0x421985..0x421afb`).

De TRAJ is dus een **rail waarlangs de camera de speler volgt op vaste afstand**, niet een in de tijd afgespeeld pad.
(Een tijd-gestuurde TRAJ-afspeler voor camera's is in de CamMgr niet gevonden.)

### 6.3 `0x41fbd0` is camera-**shake**, geen blend

`0x41fbb0(m, t)` zet `m+0x67c = t` (resterende shake-tijd; aanroepers `0x40dd30`, `0x40eec0`, `0x458d37` met 2.0 s) en
`+0x694 = 0`. Zolang `+0x67c > 0` roept `0x41eff0` na de mode-update `0x41fbd0` aan en trekt dt af. Alleen voor modes
1, 2, 4, 8, 0x10, 0x20 (bytetabel `0x41ffb4`):

```c
a = min(m->shake, 3.0f);                                        // 0x4a988c
j = ((rand(1000) − 500)·0.01·a, idem, idem);                    // 0x43ff10(1000), 0x4aa0ac → ±5·a eenheden per as
dir = (m->target (+0x278) + j) − m->state.pos;  dir.y += m->lookOffset.y (+0xc8);
fwd = normalize(dir); right = normalize((0,−1,0) × fwd); up2 = normalize(fwd × right);
m->state = [I | −pos]·[R | 0];                                  // positie ongewijzigd, alleen de kijkrichting trilt
```

## 7. Niet-camera kandidaten

- **Type 42** (`0x452140`, 0x19c B, vtable `0x4ab148`, "kind" `[+0x104]&0x1f == 6`): projectielwerper.
  `0x452330(preset)` kopieert 0x68 B uit tabel `0x5d7ba8[preset]` naar +0x108; `0x452360(n, v)`
  zet parameter n (switch `0x4524fc`, 20 gevallen); `0x4522b0(count, interval, target)` start
  herhaald vuren (tick `0x452780`: elke `interval` s `0x452560` = richting naar target normaliseren,
  projectiel uit pool `0x5d7e2c` (`0x4490a0`) of effect `0x44d5d0` + geluid 0xe, animatie +0x17c
  afspelen); `0x452320` stopt. Berichten 1000–1004 in `0x444870` horen hierbij.
- **`0x452e10`**: vtable-slot 3 (tick) van klasse 20/21 (`0x452850`, 0x194 B, vtable `0x4ab1b8`); roept
  `0x440370` aan voor de eigen instantie-oriëntatie, geen camera.
- **`0x447210`** (+ `0x447660`, `0x447260`, `0x4480d0`, `0x447d70`): `game+0x30`-object, HUD/GUI (roept
  `0x4484a0`/`0x461a70` aan; de framefunctie slaat het over als `[game]==2`).
- `0x4524fc` is geen functie maar de jumptable van `0x452360`.
- `0x4510c0`/`0x4512c0`: klasse 50–52 (`0x450cd0`), gebruiken marker `0x42f6b0` en botsingsbol `0x435810`/`0x4359b0` – niet camera.

## A. Hulpfuncties (vec3 = 3×f32)

| adres | betekenis |
|---|---|
| `0x43ff80` | `v = (x,y,z)` |
| `0x43ffa0` | `v = 0` |
| `0x43ffb0` | `|v|²` |
| `0x43ffd0` | `v += a` |
| `0x43fff0` | `v = a + b` |
| `0x440020` | `v -= a` |
| `0x440040` | `v = a − b` |
| `0x440070` | `cos∠(v, a) = v·a / (|v||a|)` |
| `0x4400f0` | `v = a × b` |
| `0x440140` | `a · b` |
| `0x437710` | 3×4-matrix = identiteit |
| `0x437740` | `M = M · B` (3×4, rijvectoren) |
| `0x440370` | quaternion → 3×3 |

Constanten: `0x4a9004`=0, `0x4a900c`=1, `0x4a9010`=100, `0x4a9014`=0.5, `0x4a9030`=50, `0x4a9760`=0.2,
`0x4a986c`=300, `0x4aa168`=1.1, `0x4aa16c`=1/2700, `0x4aa170`=3000, `0x4a94c0`=4, `0x4a988c`=3,
`0x4aa138`=1/4096, `0x4aa0ac`=0.01.

## 8. Open vragen

1. **Near/far-vlakken**: in de gelezen projectiecode (`0x47b372..0x47b429`) zitten alleen de vier zijvlakken en
   `rhw = 1/w`; waar near-clipping en de z-bufferwaarde (far) gezet worden is niet gevonden (renderer `[0x5e86ac]`,
   `0x42ac10`/`0x43b3f0`).
2. `p+0x30` (`CamMgr+0x368`) = 1 start de slerp-variant van de volgcamera-init (3.8). Alle gevonden schrijvers zetten 0
   (`0x498c10`, `0x498c35`, `0x459123`, en met `ebx = 0`: `0x402aad`, `0x402b1a` (debugtoetsen in `0x402940`), `0x445b5a`,
   `0x4562d2`). Niemand zet 1 → de slerp-start is vermoedelijk ook dode code.
3. `p->flags` bit 1 (reset `0x424940` met straal-naar-voren-schuiven) en volgcamera-toestand 4 (`+0x9b0`) worden nergens
   gezet – vermoedelijk dode code uit een eerdere versie. `0x422f10` ("SubCenter") berekent een alternatieve stap die
   niet wordt toegepast.
4. `0x4359b0`/`0x497ed0` (straal) en `0x407340` (bol-test, uitduwvector `[0x4c4bb4]`) zijn alleen als black box
   gebruikt: `[0x53a554]` 0 = vrij, 1 = wereldgeometrie, 2 = instantie (`[0x53a560]`); categorie-7-instanties blokkeren
   de camera niet (welke klasse is 7?).
5. Mode 8 (rail): de keuze tussen meerdere bol-snijpunten (`0x4216f0..0x4217dd`) is niet volledig uitgeschreven.
6. Modes 2, 4, 0x10, 0x20, 0x40, 0x100, 0x200 (updates `0x4254c0`, `0x425810`, `0x4203c0`, `0x424bf0`, `0x420cf0`,
   `0x420010`, `0x425b80`) zijn niet in detail gelezen; alleen hun parameters (§1.2) en invoer (3.1).
7. `CamMgr+0x18` (instantie voor de start-kijkrichting, `0x41e488`): schrijver niet gezocht.
8. Actie 0xa: welke toets/knop dit standaard is, staat in de invoertabel (`[0x5e6188]`), niet nagegaan.
9. `0x41fb50(m, &pos, f)` (aanroepers `0x459030`, `0x46496a`, `0x464aab`): hulproutine "kijk vanaf punt `pos` naar de
   speler" (mode 2, snelheid 100, vloeiend) – door de engine zelf gebruikt, context niet uitgezocht.
