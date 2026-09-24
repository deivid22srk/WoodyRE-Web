# Perso (speler) en de per-frame pipeline – Woody.exe

Status: werkdocument, wordt incrementeel bijgewerkt. Alle adressen zijn VA's in `Woody.exe`
(imagebase 0x400000). Float-constanten zijn met `tools`/PE-parser uit `game/Woody.exe` gelezen.

Belangrijk vooraf: **`0x462c60` is een lege functie (`ret`)** – een weggecompileerde debug-log.
Alle `call 0x462c60` (ook die met `printf`-achtige argumenten zoals `'F/S : %f '`) doen niets.

## 0. Betrokken objecten

| object | waar | velden die in de framefunctie gebruikt worden |
|---|---|---|
| **App** (`this` van `0x401ab0`, global `[0x4c2d00]`, ctor `0x401f00`) | | `+0` = modus (0 = spel, 3 = ?), `+8` = invoerobject (vtable[5](0x16) = toets 0x16 ingedrukt?), `+0x18` = berichten-dispatcher (`0x4012a0/0x401250/0x401370`), `+0x1c`, `+0x20`, `+0x24`, `+0x28` = **Wereld** (`[0x4c4c0c]`), `+0x2c` = **Game** (`[0x5d7afc]`), `+0x30` = HUD/menu-object, `+0x34` = **Perso** (speler, `[0x53a34c]`), `+0x44` = 0x388-byte object (ctor `0x44fa10`, flags `+0x384`), `+0x68`, `+0x70` (byte), `+0xe4`, `+0xf4` (flagbyte; **bit 3 (0x08) = gepauzeerd/menu**, bit 4 = ?) |
| **Wereld** (`App+0x28`) | | `+0x38` = **dt (s) van dit frame**, `+0x60` = aantal instanties, `+0x64` = instantie-array, `+0xc0` = camera-matrix/positie voor de renderer |
| **Game** (`App+0x2c`, `[0x5d7afc]`) | | `+0` = Perso*, `+4` = fader/overgangsobject, `+0xc` = timer, `+0x10` = dt, `+0x14` = overgangs-toestand 0..4, `+0x64` = modus-object (`0x44f2e0`: `+0x120` in 2..3 ⇒ "cinematic/menu-modus"), `+0x198`, `+0x1b0` = respawn-positie (vec3) |
| **Perso** (`App+0x34`) | struct 0x754, vtable `0x4aabc0` (43 methoden) | zie §3 |
| **Camera-manager** `[0x4c737c]` | | `0x41fa30` = `&cam+0x140` (camera-record; `+0x90..0x98` = positie), `+0x138` = cameramodus, `+0xc/+0x10/+0x14` = overgangsparameters, `+0x368` |

## 1. Frame-pipeline `0x401ab0` (App::Frame)

Aanroepers: `0x401615` (hoofdlus) en `0x404e90`. `esi = App`. `bl = 8` = pauze-bit van `App+0xf4`.

| # | adres | aanroep | betekenis |
|---|---|---|---|
| 1 | `0x401abb` | `[0x4c2dbc] = [0x4c2db8] = 0` | tellers reset |
| 2 | `0x401ac7-0x401bb6` | `App+8->vtable[5](0x16)`; toggelt `[0x4c2dc0]`; daarna alleen `0x462c60`-logs (bonus-tellingen `[0x5e54e4..]`, `F/S = 1.0/dt`) | debug-statistiek (toets 0x16); **doet in release niets** |
| 3 | `0x401bb9` | `[0x5e48cc] = (App+0xf4 >> 3) & 1` | globale pauzevlag |
| 4 | `0x401bd9` | `0x40bf60(!pauze)` | `[0x4b178c] = actief`; kopieert de registratielijsten van vorig frame: `0x4c5218[8]` (paren obj,param, teller `0x4c5320`) → `0x4c52d8`/`0x4c531c`, `0x4c4d80[32]` (teller `0x4c5328`) → `0x4c5258`/`0x4c5324`, en reset de tellers. Loopt daarna over `0x4c4e00[0x4c5318]` (alle geregistreerde actoren): als `+0x10c` bit 0 gezet ⇒ wissen en `vtable[29]()` aanroepen |
| 5 | `0x401be3` | `0x4018d0()` | frametellers `[0x509b2c]++`, `[0x509b30]++`, `[0x4c3bb0]+=2`, `[0x4c3bac]++`; op Wereld: `0x42a810`, `0x42a7e0`, `0x4840d0`, `0x4843e0(0.3f)` (renderer/tijd-voorbereiding) |
| 6 | `0x401bee` | `0x4076d0([0x4c4c04])` | `obj+4 = obj+0` (vorige waarde bewaren; waarschijnlijk tijd/teller) |
| 7 | `0x401bf3` | `0x406ff0()` | `[0x4c4bec] = 0` (lijst 0x4c3bb4 van zichtbare instanties leeg) |
| 8 | `0x401c01` | `0x401940(App+0 != 3)` | als arg: `0x445ba0(Game, dt)` (Game-subobjecten `+8` via `0x459090` en `+0x18` via `0x4571c0` tikken – HUD/score). Altijd: camera-record (0x9c bytes) van `[0x4c737c]+0x140` kopiëren naar `0x4c2d08`, `0x42a680(level, &0x4c2d08, level+8, 0x41fa20(cam))` = **camera in renderer zetten**. Als `App+0x44->+0x384 & 2`: `0x4883a0(App+0x20, cam+0x1dc)` |
| 9 | `0x401c06-0x401c63` | `0x40c350(Perso)` = `(vtable[4]()[0] >> 5) & 0x1f` = sub-type; als 4 of 5 ⇒ `0x42a980(&campos, Wereld+0xc0)` anders `0x42a980(&campos, 0)` | **zichtbaarheid/kd-boom-traversal** (render-voorbereiding; `campos` = cam `+0x90..0x98`) |
| 10 | `0x401c68-0x401cbc` | lus over `Wereld+0x64[i]`: instantie met `flags(+8) & 0x20` en `(flags & 0x1f) == 1` en `inst+0xf8->+0x58 != 0` → `0x4c3bb4[n++] = inst` (max 0x3ff), `[0x4c4bec] = n` | lijst "zichtbare instanties met skelet/mesh" voor de renderer |
| 11 | `0x401cc2` | als `0x44f2e0(Game+0x64)` (cinematic-modus) ⇒ `0x468e20(Perso+0x4a4, 0, 0x3c, [0x5e48c8])` | geluidsobject in Perso stoppen (`0x468a30(...)`, flag &= ~2) |
| 12 | `0x401ce4-0x401d07` | als niet gepauzeerd en niet cinematic: **`0x44b530(Perso, 1)` = Perso::Update** | spelerupdate (§2) |
| 13 | `0x401d0c` | als `App+0x68 == 0`: `0x44e690(Perso)` | zie §1.1 |
| 14 | `0x401d1b` | als `App+0xe4 != 0`: `0x4846d0(App+0 == 0)`; als dat true ⇒ `0x404b60(App, 0.5f, 0x1a, 0, 0x20)` | overgang/fade (0x404b60 = fade-start, vgl. bericht 1081) |
| 15 | `0x401d57` | `Perso->vtable[2](1)` | = `0x42e2b0` (basis-instantie: per-frame animatie/skelet-tick) |
| 16 | `0x401d69` | als niet gepauzeerd: `0x44b480(Perso)` | als `Perso+0x21c == 6 && Perso+0x690 == 0` ⇒ `0x463530(Perso)` |
| 17 | `0x401d78` | `0x42b400(dt)` | (per-frame tick van iets globaals, roept `0x474a90`) |
| 18 | `0x401d7d` | `0x44d820()` | voor alle objecten in `0x5e4880[0x5e487c]`: `0x44d850(obj)` – als `obj+0x131` ⇒ `obj->vtable[2](1)` (tick), en meer als niet gepauzeerd |
| 19 | `0x401d85` | `0x42abc0(Wereld)` | render-voorbereiding |
| 20 | `0x401d91` | `0x42b380(Wereld, Perso)` | render-voorbereiding met speler |
| 21 | `0x401d99` | `0x42b4e0(Wereld)` | render (183 instr, roept `0x42c320`, `0x498830`) |
| 22 | `0x401da1` | `0x42ac10(Wereld)` | **render-hoofdlus** (425 instr, `0x42b6c0`, `0x439540`) |
| 23 | `0x401da6` | `[0x509b2c]++` | frameteller |
| 24 | `0x401dbd` | niet gepauzeerd en `cam+0x138 != 8`: `0x44b4a0(Perso)` | Perso na-render update: als toestand 3 ⇒ `+0x100 = 100.0`, `0x44e7f0(1.0, 1)`; als `+0x268` ⇒ reset, `+0x100 = 100.0`, `0x44e7f0(0, 1)`; `+0x5cc = (0x42f6b0(Perso, 0, &Perso+0x59c, 0) == 1)` (grondtest?); `0x44af90(Perso)`; `+0x248 = 0` |
| 25 | `0x401de2` | niet gepauzeerd: `0x42b450(dt)` | |
| 26 | `0x401def` | niet gepauzeerd: `0x42d2e0()` | 241 instr; roept `0x407790` (wereldcel zoeken), `0x428ce0`, `0x4359b0` – **botsing/celtoewijzing van bewegende instanties** |
| 27 | `0x401dfa` | `0x46d040([0x5e823c])` | subsysteem 1500-berichten (particles?) tick |
| 28 | `0x401e0b` | `0x46e0d0(pauze)` | 136 instr, particle/effect-systeem |
| 29 | `0x401e19-0x401e5a` | `a = !cinematic && (App+0x70 || cam+0x138 != 2)`; als `a || App+0x70`: (als `App+0x70`: `0x4484a0(App+0x30)`), `0x447210(App+0x30)` | HUD/menu-object tekenen (`0x4484a0`: `+0xc = 2`, eventueel `0x4618d0(+0x30)`) |
| 30 | `0x401e60` | `App+0x70 = 0` | |
| 31 | `0x401e6a` | niet gepauzeerd: **`0x4019c0(App)`** | **script-VM tick** (§1.2) |
| 32 | `0x401e7e` | niet gepauzeerd: `0x4490f0(dt)` | (roept `0x4493c0`) |
| 33 | `0x401e9a` | niet gepauzeerd: **`0x4459c0(Game, dt)`** | Game-overgangs-state machine + respawn (§4) |
| 34 | `0x401ec2` | als `[0x5e48c8]` (geluidsmanager) en (niet gepauzeerd of `App+0xe4 == 0`): `0x468ba0(dt)` | geluidsmanager tick (lijst `+0x14/+0x18`, gate `[0x5e61a4]`) |
| 35 | `0x401eca` | als `App+0x44->+0x384 & 2`: `App+0x1c->vtable[7]([0x509adc]+0x64, [0x509adc]+0x60, 0)` | (video/afspeel-object?) |

### 1.1 `0x44e690(Perso)` (stap 13)
Als `Perso+0x21c == 0` (toestand IDLE) en `Perso+0x57c != 0`: `0x44dda0(0x49, &Perso+0x564, 0)` en
`0x44c980(Perso, 5)` (**toestand := 5**). Daarna `Perso+0x100 = 10000.0f` en `0x44e7f0(Perso, 1.0 als
niet cinematic anders 0.0, 0)` (`0x44e7f0(v, b)`: `+0xfc = v`, en als b: `+0x6c = v` – animatiesnelheid).

### 1.2 `0x4019c0(App)` – script-VM tick
1. `0x4012a0(App+0x18)` (berichtwachtrij voorbereiden), `[0x4c7384] = 0`, camera-overgangsparameters
   `cam+0xc/+0x10/+0x14 = 0`.
2. **`0x442240()`** = VM: alle threads een tick (zie docs/VM.md).
3. `0x444050((int)([0x509adc]+0x30 * 100.0))` – VM-tijd in 1/100 s (constante `0x4a9010` = 100.0).
4. Berichten: `n = 0x441d20()`; voor i<n: `m = 0x441d30(i)`; id in 1200..1300 ⇒ `0x403440(App, m)`
   (wereld-handler), anders `0x401370(App+0x18, m)` en zo ja `0x401250(App+0x18, m)` (dispatch naar
   instantie/game/geluid, zie docs/MESSAGES.md). `0x441d40()` = wachtrij leeg.
5. Waarschuwingen (logs, no-op) als cameramodus-parameters gezet zijn zonder moduswissel.

## 2. Perso::Update `0x44b530(Perso, bool arg=1)`

### 2.0 Sub-objecten binnen Perso (ingebed, geen pointers)

| offset | grootte | ctor | rol |
|---|---|---|---|
| `+0x110` | 0xe4 | `0x4631b0` (defaults) + `0x463280(kolom)` | **Parameterblok P** – per karaktertype 31 floats uit tabel `0x4b5f14` (31 rijen × 5 kolommen, stride 0x14) gevolgd door vaste defaults; zie §2.6 |
| `+0x334` | 0x54 | `0x462c50`/`0x462c70(Perso)` | hulpobject "duw/impuls" (`+0x28`, `+0x4e`; `0x463130` levert een verplaatsingsvector, `0x463160` een toestand (6 = ?)) |
| `+0x388` | 0x10c | `0x47fa30`, `0x459fd0(Perso)` | **Mover M** – horizontale bewegingscontroller (§2.3). `M+0` = invoerobject, `M+4` = Perso, `M+8` = P |
| `+0x494` | ptr | `0x44ad90` → `new 0x54` (`0x463df0`) | animatiecontroller (vtable: `[2](on)` aan/uit, `[3](dt)` tick, `[4]()` reset). `+0x498` tweede controller (mag NULL). `+0x49c`/`+0x4a0` = animatielengtes anim 3 en 0x42 (`0x436b90(anim, flag)`) |
| `+0x4a4` | | | geluidsbron (`0x468e20/0x468e50`) |
| `+0x604` | | `0x4632c0` | (velden `+0x80/+0x84` = 0) |
| `+0x710` | | | teller/accu (`0x453ca0`: `+0x10 += dt`) |
| `+0x298`, `+0x2bc` | 0x24 | `0x436cf0`/`0x436d10` | twee colliders (wand / grond) voor `0x436d20`, `0x437040`, `0x436f00` |

### 2.1 Hoofdfunctie (pseudo-C, adressen tussen `/* */`)

```c
void Perso_Update(Perso *p, bool arg /* altijd 1 */)
{
    p->vy_corr = 0.0f;                                   /* +0x244  0x44b552 */
    p->dt = p->world->dt;                                /* +0x2f8 = [+0x2f0]->+0x38  (0x44baf0) */
    p->camPos = cam->pos;                                /* +0x2fc..0x304 = [0x4c737c]+0x140+0x90 (0x44c000) */
    if (p->respawnCountdown == 0) {                      /* +0x278 */
        ClearIdFlag(p->id, 0x10);                        /* 0x443e90: tabel [0x5d054c][id&0xffffff] &= ~0x10, wek watchers 0x442210 */
        p->respawnCountdown = -1;
    } else if (p->respawnCountdown > 0) p->respawnCountdown--;
    vec3 oldPos = *p->vtbl[34](p);                       /* 0x44c030: &+0x544 als +0x550 anders &+0x1f4 */
    if (p->invulnTimer > 0) p->invulnTimer -= dt;        /* +0x238 */
    if (!p->frozen) {                                    /* +0x690 == 0 */
        Perso_LifeTimers(p);                             /* 0x44b220 (§2.2) */
        Perso_DecTimers(p);                              /* 0x44b1b0: +0x270, +0x280, +0x704 -= dt (tot 0) */
    }
    p->disp = (0,0,0);                                   /* +0x204..0x20c: verplaatsing dit frame */
    if (!p->frozen) {
        if (p->attachedTo == 0 && p->state != 5)         /* +0x26c, +0x21c */
            RegisterActor(p, 0xa);                       /* 0x40c080: 0x4c5218[n] = (p, 10), max 8 – lijst voor het "onweer"-systeem (§4.2) */
        if (!p->frozen) {
            p->accu.t += dt;                             /* 0x453ca0 op +0x710 */
            if (p->bonusCount >= 25) {                   /* +0x25c */
                Sound(4);                                /* 0x468a00([0x5e48c8], 4, 0) */
                if (p->health < 5.0f) p->health += 1.0f; /* +0x24c (0x4a9884 = 5.0, 0x4a900c = 1.0) */
                else { Sound(0); Perso_AddLives(p, 1); } /* 0x44c7a0: +0x250 += 1, in savegame [0x5e5818] */
                p->bonusCount -= 25;                     /* 25 bonussen = 1 hartje / extra leven */
            }
        }
    }
    RegisterActor2(p);                                   /* 0x40c0b0: 0x4c4d80[n] = p, max 32 – actor-actor botsingslijst */
    if (!p->frozen) { /* elke stap kan +0x690 zetten, telkens hergetest */
        0x464ef0(p);   /* timer +0x524 -= dt … */
        0x465e50(p);   /* 96 instr; gebruikt richting M+0x10, geluid */
        0x457a50(p);   /* SPRONG/AANVAL-controller, sub-toestand +0x5b4 (0..11), 1192 instr (§2.5) */
        0x44ba70(p);   /* timers +0x5f4/+0x5f8 (+0x5fc vlag); toestand 0 → 0x463430; daarna 0x457330 */
        0x465b10(p);   /* 232 instr; invoer + anim */
        Perso_CheckKey7(p);  /* 0x44b980 (§2.2) */
        0x458bf0(p);   /* 119 instr; invoer 0x467440, geluid */
    }
    if (p->altMode) 0x459c70(p);                         /* +0x4ec: vlieg/zwem-modus, 174 instr */
    M_LoadParams(&p->mover);                             /* 0x45b0a0: kopieert P → M (§2.3) */
    bool doPost = true;
    if (!p->frozen) switch (p->state) {                  /* +0x21c, tabel 0x44b950 */
        case 0: case 6: Perso_Move(p, 1);  break;        /* 0x44bb20 */
        case 2: case 3: Perso_Move(p, 0);  break;        /* 2 = DOOD */
        case 1: 0x456210(p); break;                      /* sub-toestand +0x4a8, geluid op +0x4a4 */
        case 4: p->fallAccum = 0; p->+0x382 = 0; 0x4651d0(p); break;   /* +0x35c; toetsen 0,1,6 */
        case 5: 0x44db50(p); doPost = false; break;      /* scripted animatie (§2.4) */
        case 7: 0x44e1c0(p); doPost = false; break;      /* positie volgt object +0x55c (0x42f6b0), richting eruit */
        case 8: 0x4657f0(p); doPost = false; break;      /* 233 instr, quaternion 0x440370 – rail/lift? */
        case 9: 0x454090(p); doPost = false; break;      /* sub-toestand +0x724 (0..5), anim 0x4b, roept 0x44db50 */
    } else if (p->state == 7) { 0x44e1c0(p); doPost = false; }
    if (doPost) Perso_MoveCollide(p);                    /* 0x4624f0 (§2.4) – voor 0/1/4/6/2/3 */
    if (p->state == 1) 0x4567f0(p);                      /* 271 instr */
    if (doPost && bl) { 0x462a40(p); }                   /* 129 instr: 0x428ce0/0x4359b0 celwissel (alleen na de switch-cases 0/6/2/3/1/4) */
    if (p->onGround) SetIdFlag(p->id, 0x200);            /* +0x22c → 0x443e50 */
    else             ClearIdFlag(p->id, 0x200);          /* script kan "staat op de grond" lezen */
    if (p->ridden && p->state != 6) 0x463c90(p);         /* +0x590 (object waarop gestaan wordt) */
    Perso_CheckSteep(p);                                 /* 0x44b2e0 (§2.2) */
    Perso_Orient(p);                                     /* 0x44bd00 → 0x44bd30 + 0x44bf10 (§2.4) */
    Perso_AnimState(p);                                  /* 0x463e60: animaties per toestand (tabel 0x463f14), tick controllers +0x494/+0x498 */
    vec3 newPos = *p->vtbl[34](p);
    if (oldPos.y - newPos.y > 0) p->fallAccum += oldPos.y - newPos.y;   /* +0x35c: gevallen hoogte */
    Perso_UpdateHUD(p, 0);                               /* 0x44ae60 */
    p->frozen = 0;                                       /* +0x690 */
}
```

### 2.2 Kleine helpers

* **`0x44b220` levens/timers**: als `state != 2`: `k = 0x463160(&+0x334)`; als `k == 6`: als
  `+0x5b4 == 0` en `fallAccum(+0x35c) >= P+0x7c (1500)` ⇒ `0x44d1b0(p, P+0xcc (0.8), P+0xc8 (0.5))`
  (duw/knockback), `health -= P+0x8c (1.0)`; als health ≤ 0 ⇒ `health = 0`, `p->vtbl[38](p, 8)`
  (**valschade: 1500 eenheden vallen kost 1 hartje**); anders `+0x334->+0x28 = 0`, `+0x4e = 0`.
  Daarna: als `health <= 0` ⇒ `health = 0; vtbl[38](3)` (event 3 = sterven).
* **`0x44b980` toets 7** (`0x467440(7)` = losgelaten-flank): in toestand 3 (en cam-modus ≠ 9) of toets:
  toestand 3 ⇒ `0x44c9f0` (terug naar vorige toestand `+0x220`), `+0x268 = 1`; toestand 0 met
  `+0x5b4 == 0` of toestand 6 met `+0x58c == 2` ⇒ als `onGround` en cam-modus 0: `0x464620(p)`;
  **toestand := 3** (`0x44c980(3)`); anders geluid 9. Toestand 3 is dus een toets-7-modus
  (vermoedelijk "rondkijken"/eerste persoon; `0x44b4a0` zet er `+0x100 = 100.0` en animsnelheid 1).
* **`0x44b2e0` steile helling**: `+0x234 = 0`; als onGround: `d = normalize(M->dir(+0x10))`
  `* P+0x80 (40.0)`; test `0x497a30(pos + P+0 (43) omhoog, pos + d, -1)` (raycast/hoogtetest) en als
  `[0x4c4bd0]` (resultaattype) 3 of 4 en `[0x4c4bd4] >= 3.0` ⇒ `+0x234 = 1` ("staat voor een
  steile rand", gebruikt door de sprongcontroller `0x457abe`).
* **`0x44c980(p, s)` SetState**: als `+0x590` (bereden object) en toestand 6 → 0x463c90; `+0x220 =
  oude toestand`, `+0x21c = s`, wist `+0x50c`, `+0x5f0`, `+0x5b4`, `+0x5cd`, `+0x6ac`.
* **`0x44c030` (vtbl[34])** = positie-pointer: `+0x544` als `+0x550` (alternatieve positie, bv. op
  een bewegend object) anders `+0x1f4`.
* **`0x44c720` (vtbl[36])** = `state == 2` (dood). **`0x44c730`** = leven verliezen: `+0x250--`
  (tenzij cheat `[0x5d7b88]`), naar savegame `[0x5e5818]`, `0x443ff0(id)`, id-flag 0x10 zetten,
  `+0x278 = 2`; geeft `levens <= 0`.
* **`0x44c110` (vtbl[38], `TakeHit(type)`)**: genegeerd in cinematic-modus (`0x44f2e0/0x44f2d0`),
  bij `App+0xcc`, voor subtypes 4/5, of als een klasse-2-actor subtype 0xc met `vtbl[36]()` actief
  is (schild/bescherming). Types gezien: 3 (dood), 8 (valschade), 9 (onweer §4.2).
* **`0x44dda0(anim, &pos, flag)`** = scripted animatie starten (`state 5`); geeft 0 als dood.

### 2.3 Mover `M = Perso+0x388` (horizontale beweging, `0x45b110(M, arg)`)

Layout M: `+0` invoer (`[0x5e6188]`, records van 12 B per actie: `+0` analoge waarde, `+8` status,
bit 31 = flank; `0x467400(k)` = ingedrukt, `0x467420(k)` = status==1 (net ingedrukt), `0x467440(k)`
= bit31, `0x467460(k)` = analoge waarde), `+4` Perso, `+8` P, `+0xc` bewegingsfase (0..6, `0x45ad30`),
`+0x10..0x18` **kijkrichting** (xz, y = 0), `+0x1c..0x24` **genormaliseerde snelheidsrichting**,
`+0x28/+0x30` invoerrichting t.o.v. camera, `+0x34` Ramp A (loopsnelheid), `+0x68` Ramp B (glijden),
`+0x9c` Ramp C (duw/knockback), `+0xd0..0xd8` **grondnormaal**, `+0xdc` glijdt, `+0xe0` snelheid
(eenheden/s), `+0xe4` = snelheid·dt (afgelegde weg), `+0xe8` dt, `+0xec` duwtimer, `+0xf0` camPos,
`+0xfc` pos, `+0x108` vlaggen: 1 = onGround, 2 = arg, 4 = glijstop-blokkade, 8 = beweegt,
0x10 = geen invoer, 0x20 = ?, 0x40 = omgekeerde besturing, 0x80 = dicht bij volgdoel `+0x68c`.

**Ramp** (0x34 B, `0x467110` reset, `0x467130` start versnellen, `0x467180` start vertragen,
`0x4671d0(dt, useMax)` tick): `+0` vec3 richting, `+0xc` huidige snelheid, `+0x10` doelsnelheid,
`+0x14` maxsnelheid, `+0x18` (= −P+0x24, ongebruikt gezien), `+0x1c` versneltijd, `+0x20` vertraagtijd,
`+0x24/+0x28` fasetimers, `+0x2c` fase (1 versnellen, 2 op doel, 3 vertragen, 0 stil), `+0x30`
dubbele snelheid-vlag. Versnellen: `v = (t/T_acc)² · doel`; vertragen: `v = max·(1 − (t/T_dec)²)`.
Als `doel < 0` (tick, `0x4671d0`): `v = doel` direct.

`0x45b0a0` laadt per frame: RampA: max = **P+0x1c (600 = loopsnelheid)**, `+0x18 = −P+0x24 (−300)`,
T_acc = **P+0x2c (0.25 s)**, T_dec = **P+0x30 (0.1 s)**; RampB: max = **P+0x40 (600 = glijsnelheid)**,
T = P+0x44/P+0x48 (0.25/0.25); RampC: max = **P+0x4c (500 = duwsnelheid)**, T = P+0x50/P+0x54 (0.1/0.5).

Per frame (`0x45b110`): `M+0xe8 = dt`; vlag1 = onGround; `M+0xf0 = camPos`, `M+0xfc = pos`; volgdoel
`+0x68c`: afstand xz < 200 (`0x4aa164`) ⇒ vlag 0x80 uit; ≥ 20 (`0x4a9994`) en (≥ 200 of niet vlag 0x80)
⇒ `M+0x10 = normalize(doel − pos)` (kijkrichting naar doel). Dan:
* vlag 2 (arg=1, toestanden 0/6): **`0x45a4b0` invoer**: x = analoog toets 2/3 (links/rechts),
  y = toets 0/1 (op/neer). Vlag 0x40 ⇒ `0x45a200(−x, −y)` (richting relatief aan huidige
  kijkrichting: `M+0x28 = y·dir.x + x·dir.z`, `M+0x30 = −x·dir.x + y·dir.z`; doelsnelheid
  `M+0x44 = min(|v|,1)·P+0x20 (300)`). Anders: beide 0 ⇒ vlag 0x10, doelsnelheid 0. Anders
  `hoek = π (0x4ab2d0 = 3.14116) − atan2(x, y)`; `camDir = normalize(pos − camPos)` (xz), gedraaid om
  de y-as met `hoek` (`0x440d40`: quaternion → matrix `0x440370`); doelsnelheid
  `M+0x44 = (dot(nieuw, oud)+1)/2 · P+0x1c (600) · min(|stick|,1)` (draaien remt af),
  `M+0x10 = slerp(oud, nieuw, |stick|·0.25 (0x4a9ca0))` (`0x45a320`, drempel `0x4aa41c` = 0.9999),
  y = 0, normaliseren; vlag 8.
  Met `+0x4ec` (altMode): **`0x45a7b0`**: toetsen 0/1 (gespiegeld als cam+0x61c == 1) met vlaggen
  `+0x4ed/+0x4ee` ⇒ doelsnelheid P+0x1c of 0.
* anders `0x45a1f0` (vlaggen 8/0x10/0x20 wissen), fase = 0.
* **`0x45a850` richting**: als `+0x308 == 1` (wandmodus?) en niet vlag 0x40 en doelsnelheid ≥ 0:
  `k = clamp(M+0xe0 / P+0x1c, 0, 0.95) · P+0x3c (1.0)`; RampA.dir = lerp(normalize(RampA.dir),
  normalize(M+0x10), k) (traag bijdraaien; RampA T = P+0x34 (0.75)/P+0x38 (1.0)); anders RampA.dir =
  `M+0x28` (vlag 0x40) of `M+0x10`.
* **`0x45aa60` glijden**: `n = normalize(M+0xd0)`; als `n.y < 0.71` (`0x4ab2d8`, f64) en onGround
  en niet al glijdend: RampB.max = P+0x40, start versnellen, RampB.dir = (n × up) × n (bergafwaarts),
  `M+0xdc = 1`. Als glijdend en onGround (vlakker): vertragen, `M+0xdc = 0`, RampA reset op M+0x10;
  niet onGround ⇒ vlag 4. Tick RampB.
* **`0x45acb0` duw**: `M+0xec -= dt`; als `M+0xc8 == 2` ⇒ RampC vertragen; tick RampC; timer ≤ 0
  ⇒ RampC.dir = 0.
* **`0x45ad30` fase-automaat** `M+0xc` (tabel `0x45ae2c`): 0 stil → 1 (start versnellen) bij vlag 8,
  → 4 bij vlag 0x20; 1 → 2 als vlag 8 blijft; 2 → 3 (start vertragen) als vlag 8 weg; 3 → 2 of 0;
  4 → 5/6; 5/6 → 1.
* **`0x45ae50`**: als `Perso+0x2e0` (grondtype ≠ 0) ⇒ `0x4672d0(RampA, 0.25)`; tick RampA(dt, 1).
* **`0x45ae80` totaal**: `v = RampA.dir·RampA.v (alleen bij vlag 2) + RampB.dir·RampB.v +
  RampC.dir·RampC.v`; bij vlag 4: alleen A geprojecteerd (`0x440070` = dot) ≥ 0; `M+0xe0 = |v|`,
  `M+0xe4 = |v|·dt`, `M+0x1c = v/|v|`.

### 2.4 Verplaatsing, botsing en oriëntatie

**`0x44bb20(p, arg)` Perso_Move**: als `0x465fe0(p)` (timer `+0x6f8`, modus `+0x6e4`) ⇒ klaar.
Als `+0x474 > 0` en `+0x238 > 0` en `+0x5b4 == 0` ⇒ arg = 0 (geen invoer tijdens onkwetsbaar-flits?).
`0x45b110(M, arg)`; `d = M+0x1c · M+0xe4` (`0x44d1e0`); duw uit `+0x334` (`0x462d70(0x467400(4))`,
`0x463130`) als `+0x240 > 0` anders `+0x240 -= dt`; als `+0x238 > 0` ⇒ d = 0.
Dan `disp(+0x204)` = `+0x5bc` als `+0x5cd` (**sprongcontroller levert de verplaatsing**), of
`+0x69c` als `+0x6ac`, of `d + duw`. `disp.y += dt · +0x244`. `0x459eb0`: in altMode (`+0x4ec`)
wordt disp begrensd tot het vlak `+0x4f0..0x4fc` (n·p + d ≤ |disp|).

**`0x4624f0(p)` Perso_MoveCollide**: `0x462490`; **`0x4627d0`**: voor elke andere actor in
`0x4c5258[0x4c5324]` (kopie van vorig frame): cirkel-cirkel uitduwen (`0x433d40`) met stralen
`vtbl[32]()` en `vtbl[32]() − 5.0`, resultaat opgeteld bij `disp.x/z`. Straal `0x434820(P+4 (69))`
(gehalveerd in toestand 1 met `+0x694`). `+0x28c = pos` (oude positie). `newPos = pos + disp`;
wandbotsing `0x436d20(&+0x298, &newPos, &out, id)` en `0x437040(&+0x2bc, …)`; in toestand ≠ 2 wordt
de correctie toegepast. `+0x200 = 0x437180(…, 80.0, 10.0, …)`; `+0x2e0 = [0x53a554]` (grondtype,
2 als `[0x53a560]`), `+0x2e4 = [0x53a560]`. Grondtest `0x436f00(&+0x298, -1, &pos, y+P+0, id)`:
`+0x224 = [0x53a568]` (grondhoogte), `+0x228 = y − grondhoogte`; **1 ⇒ `onGround(+0x22c) = 1` en
`pos.y = grondhoogte`**, anders `onGround = 0`. `0x4628e0(p)`; `0x462760(p, 71.0)` → `0x4347b0`
(test op hoogte y+71: plafond/hoofd). Er is **geen zwaartekracht in deze functie**: vallen en springen
worden door de sprongcontroller (`+0x5cd/+0x5bc`) en `+0x244` gedaan (zie §2.5).

**`0x44bd00` → `0x44bd30` Perso_Orient**: `+0x54 = +0x2e8` (schaal z = schaalfactor). Als
toestand ≠ 8: `f = normalize(M->dir)`, `f.x = −f.x; f.z = −f.z` (**instantie kijkt langs −dir**).
Toestand 1: up = `+0x458..0x460 · 0.1` gefilterd: `+0x210 = +0x210·0.9 + up` (`0x4a94b8` = 0.9,
`0x4a9008` = 0.1: laagdoorlaat op de "up"-vector); anders up = (0,1,0). `right = up × f`
(`0x41af10` = kruisproduct), `f' = right × up`; rotatiematrix rijen: `+0x28 = right`,
`+0x34 = f'`, `+0x40 = up`. Daarna **`0x44bf10(+0x550)`**: instantiepositie `+0xc = +0x1f4`
(of `+0x544`), `+0x60..0x68 = pos + (0, P+0 (43), 0)` (middelpunt), `0x4077f0(&mid)` (wereldcel
zoeken). Als `+0x4b4` (gekoppelde tweede instantie, bv. schaduw/voertuig): kopieer positie,
middelpunt, rotatie en cel.

### 2.5 Sprong/aanval-controller `0x457a50` (sub-toestand `+0x5b4`, 1192 instr – niet volledig gelezen)

`+0x5cd = 0` bij binnenkomst; `+0x5e0 -= dt`. Tabel `0x458bb8` (index `+0x5b4 − 1`):
1→`0x457eac`, 2→`0x458281`, 3→`0x458587`, 4→`0x4585cf`, 5→`0x45878c`, 6→`0x45841d`, 7→`0x4584e1`,
8→`0x458524`, 9→`0x457abe`, 10→`0x457c02`, 11→`0x457e78`. Sub-toestand 9 (`0x457abe`): als `+0x234`
(steile rand) en toets 4 (aanval) ⇒ `0x44cce0(p, 0, 1)`, anim reset, `+0x5b4 = 0`; anders
`+0x5b8 = animlen(0x12,0) + animlen(0x12,1)`, anim 0x12 starten, `+0x5b4 = 11`. Elk pad zet
`+0x5cd = 1` en berekent `+0x5bc..0x5c4` = verplaatsing uit `dt · M->dir · …`. In `0x45848c`:
`+0x244 -= (pos.y − grondhoogte[0x53a568]) · 2.5` (`0x4ab2bc`) – **veercorrectie naar de grond**,
opgeteld via `disp.y += dt·+0x244` in `0x44bb20`. String: `'Please get the latest version of the
Woody mesh with the Vector used for Attacks'` (`0x457a50` gebruikt een mesh-vector voor aanvallen).
Andere sub-toestanden zetten `+0x5b4` naar 1..11 op `0x4573f2..0x458ead` (zie writers-lijst §3).

### 2.6 Parameterblok P = `Perso+0x110` (tabel `0x4b5f14`; kolom per subtype: 1→0, 3→1, 2→2, 5→3, 4→4; `0x44a394`)

| P+ | Perso+ | kolommen 0,1,2 / 3,4 | betekenis (waar gebruikt) |
|---|---|---|---|
| 0x00 | 0x110 | 43 | hoogte van het botsmiddelpunt boven de voeten (`0x44bf10`, `0x44b2e0`); anim-schaal `0x4624f0` |
| 0x04 | 0x114 | 69 | horizontale botsstraal (`0x434820` in `0x4624f0`; ½ in toestand 1 met `+0x694`) |
| 0x08 | 0x118 | 193 / 143 | ? (snelheden) |
| 0x0c | 0x11c | 193,143,143 / 160 | ? |
| 0x10 | 0x120 | 61 / 81 | ? |
| 0x14 | 0x124 | 193 / 143 | ? |
| 0x18 | 0x128 | 61 / 81 | ? |
| 0x1c | 0x12c | 600 / 1250 | **maximale loopsnelheid** (eenheden/s) – RampA.max, `0x45a72b` |
| 0x20 | 0x130 | 300 | snelheid bij omgekeerde besturing (`0x45a290`) |
| 0x24 | 0x134 | 300 | → RampA+0x18 als −300 |
| 0x28 | 0x138 | 1.8 | ? |
| 0x2c | 0x13c | 0.25 | RampA versneltijd (s) |
| 0x30 | 0x140 | 0.1 | RampA vertraagtijd (s) |
| 0x34 | 0x144 | 0.75 | RampA T bij `+0x308==1` |
| 0x38 | 0x148 | 1.0 | idem |
| 0x3c | 0x14c | 1.0 | bijdraaifactor `0x45a8cc` |
| 0x40 | 0x150 | 600 | glijsnelheid op steile helling (RampB.max) |
| 0x44/0x48 | 0x154/0x158 | 0.25 | RampB tijden |
| 0x4c | 0x15c | 500 | duw/knockback-snelheid (RampC.max) |
| 0x50/0x54 | 0x160/0x164 | 0.1 / 0.5 | RampC tijden |
| 0x58 | 0x168 | 0.2 | ? |
| 0x5c | 0x16c | 0.6 | ? |
| 0x60 | 0x170 | 200 | ? |
| 0x64 | 0x174 | 380 / 400 | ? (sprong?) |
| 0x68 | 0x178 | 650 / 1250 | ? (sprong?) |
| 0x6c | 0x17c | 600 / 1250 | ? |
| 0x70 | 0x180 | 2000 | ? |
| 0x74 | 0x184 | 250 | ? |
| 0x78 | 0x188 | 300 | ? |
| 0x7c | 0x18c | 1500 (default `0x4631b0`) | valhoogte waarboven valschade (`0x44b254`) |
| 0x80 | 0x190 | 40 | vooruitkijkafstand steile-randtest (`0x44b378`) |
| 0x84 | 0x194 | 100 | ? |
| 0x88..0x90 | 0x198..0x1a0 | 1.0 | `0x19c` = schade per val (`0x44b27c`) |
| 0x94 | 0x1a4 | 3.0 | ? |
| 0x98..0xac | | 0.5, 0.5, 0.3, 0.5, 0.15, 0.5 | ? |
| 0xb0/0xb4 | | 1.0 | ? |
| 0xb8..0xc8 | | 0.5,0.5,0.5,1.0,0.5 | `0x1d8` (P+0xc8 = 0.5) knockback-arg |
| 0xcc | 0x1dc | 0.8 | knockback-arg (`0x44b261`) |
| 0xd0 | 0x1e0 | 1.5 | ? |
| 0xd4..0xdc | | 0.5 | ? |
| 0xe0 | 0x1f0 | kolomindex | (−1 = nog niet geladen) |

## 3. Perso-structvelden (0x754 bytes, vtable `0x4aabc0`, 43 methoden)

Basisinstantie (0..0xfc, zie docs/FORMAT_INS.md): `+4` id, `+8` vlaggen (`|= 0x20` in `0x44a3d0`),
`+0xc` positie, `+0x28` rotatie 3×3, `+0x4c..0x54` schaal, `+0x60..0x68` middelpunt, `+0x6c`
animatiesnelheid, `+0x1c` cel, `+0xf8` model.

| offset | type | betekenis | gezet / gelezen |
|---|---|---|---|
| 0x100 | f32 | anim-/tijdfactor (100.0 normaal, 10000.0 bij `0x44e690`) | `0x44b4a0`, `0x44e690`, `0x44ad6a` |
| 0xfc | f32 | snelheidsfactor (`0x44e7f0`) | |
| 0x104 | u32 | typevlaggen; `(>>5)&0x1f` = subtype 1..5 (`0x40c350`) | ctor `0x40c380` |
| 0x110..0x1f0 | P | parameterblok (§2.6) | ctor |
| 0x1f4..0x1fc | vec3 | **positie** (voeten) | `0x44a3d0` (uit +0xc), `0x4624f0` (y = grond), `0x44e1c0`, `0x44dec6`, `0x44e55e` |
| 0x200 | u32 | resultaat `0x437180` (grond-/materiaalcode) | `0x4624f0` |
| 0x204..0x20c | vec3 | verplaatsing dit frame | `0x44bb20`, `0x4627d0`, `0x456210`, `0x4653b4` |
| 0x210..0x218 | vec3 | gefilterde up-vector (toestand 1) | `0x44bd30`, reset `0x44ab20` (0,1,0) |
| 0x21c | int | **toestand** 0 normaal, 1 ?, 2 dood, 3 toets-7-modus, 4 ?, 5 scripted anim, 6 op object/bereden, 7 volgt object +0x55c, 8 rail?, 9 sub-automaat +0x724 | `0x44c980`, `0x44c9f0` |
| 0x220 | int | vorige toestand | `0x44c980` |
| 0x224 | f32 | grondhoogte | `0x4624f0` |
| 0x228 | f32 | hoogte boven grond | `0x4624f0` |
| 0x22c | u8 | **onGround** | `0x4624f0`; `0x44bcf0` getter |
| 0x234 | u8 | staat voor steile rand | `0x44b2e0` |
| 0x238 | f32 | onkwetsbaarheidstimer (−dt) | `0x44b638`, `0x44ccfd` |
| 0x23c | int | frameteller HUD | `0x44ae60` |
| 0x240 | f32 | duwtimer (−dt) | `0x44bb20` |
| 0x244 | f32 | verticale correctiesnelheid (0 per frame, `0x45848c`) | `0x44bb20` |
| 0x248 | u8 | (0 na render) | `0x44b4a0` |
| 0x24c | f32 | **gezondheid** (hartjes, 3.0 bij reset, max 5) | `0x44b220`, `0x44b6e9`, `0x44ab20` |
| 0x250 | int | **levens** (99 bij levelstart `0x44a538`) | `0x44c730`, `0x44c7a0` |
| 0x254, 0x258, 0x260, 0x264 | int | tellers voor HUD (`0x44ae60`) | `0x44a3d0` = 0 |
| 0x25c | int | **bonusteller** (25 → hartje) | `0x44b6d1`, `0x44c8d4` |
| 0x268 | u8 | terugkeer uit toestand 3 | `0x44b980`, `0x44b4a0` |
| 0x26c | u32 | gekoppeld object (geen onweer-registratie als ≠ 0) | `0x44c453` |
| 0x270, 0x280, 0x704 | f32 | timers −dt | `0x44b1b0`, `0x44cd25`, `0x44cd45`, `0x44c89c` |
| 0x274, 0x27c | | reset 0 | `0x44ab20` |
| 0x278 | int | respawn-aftelling (−1 idle; 2 na dood; 0 ⇒ id-flag 0x10 wissen) | `0x44c730`, `0x44b5ce` |
| 0x288 | f32 | duur doodsanimatie (gebruikt door `0x4459c0` state 3) | |
| 0x28c..0x294 | vec3 | positie vóór verplaatsing | `0x4624f0` |
| 0x298, 0x2bc | 0x24 | colliders (wand, grond) | `0x436cf0/0x436d10` |
| 0x2e0 | u32 | grondtype (`[0x53a554]`, 2 = `[0x53a560]`) | `0x4624f0`, `0x45ae50` |
| 0x2e4 | u32 | `[0x53a560]` | |
| 0x2e8 | f32 | schaalfactor (→ +0x54) | `0x44bd00`, `0x462bb7` |
| 0x2ec | ptr | level `[0x50944c]` | `0x44ae20` |
| 0x2f0 | ptr | wereld/tijd `[0x509adc]` (`+0x38` = dt) | `0x44ae30` |
| 0x2f4 | ptr | invoer `[0x5e6188]` | `0x44ae40` |
| 0x2f8 | f32 | **dt** | `0x44baf0`; getter `0x44bb00` |
| 0x2fc..0x304 | vec3 | camerapositie | `0x44c000`; getter `0x44c020` |
| 0x308 | int | 1 = bijdraaimodus (`0x45a850`) | `0x462920/0x462962` |
| 0x30c, 0x318 | vec3 | startpositie (2×) | `0x44a3d0` |
| 0x324..0x32c | vec3 | startkijkrichting (−rot[1][0], rot[1][1], −rot[1][2]) | `0x44a3d0`; `0x44ab20` → M |
| 0x334 | obj | duw/impuls-object | |
| 0x35c | f32 | gevallen hoogte (accumulator) | `0x44b914`, reset `0x44b836`, `0x458162` |
| 0x388 | M | mover (§2.3) | |
| 0x458..0x460 | vec3 | up-vector bron (toestand 1) | `0x44bd30` |
| 0x474 | f32 | timer (blokkeert invoer met +0x238) | |
| 0x494/0x498 | ptr | animatiecontrollers | `0x44ad90` |
| 0x49c/0x4a0 | f32 | animatielengtes anim 3 / 0x42 | `0x44ad90` |
| 0x4a4 | obj | geluidsbron | `0x401cdf`, `0x456246` |
| 0x4a8 | int | sub-toestand van toestand 1 | `0x456210` |
| 0x4b4 | ptr | gekoppelde tweede instantie (positie/rotatie gekopieerd) | `0x44bf10`, `0x455de4` |
| 0x4d8 | u8 | HUD-vlag | `0x44c459/0x44c6d9` |
| 0x4ec | u8 | altMode (vlieg/zwem) | `0x4599d9`, `0x44de44` |
| 0x4ed/0x4ee | u8 | altMode-richtingsvlaggen | `0x45a7b0` |
| 0x4f0..0x4fc | plane | begrenzingsvlak in altMode | `0x459eb0` |
| 0x50c | int | (reset in SetState) | |
| 0x520, 0x524 | f32 | timers | `0x4651d0`, `0x464ef0` |
| 0x53c / 0x540 | f32/int | scripted-anim timer / anim-id (0x11/0x12 = speciaal) | `0x44db50` |
| 0x544..0x54c | vec3 | alternatieve positie (op bewegend object) | `0x44bf10` |
| 0x550 | u8 | gebruik +0x544 | `0x44e451`, `0x4542a4` |
| 0x554 | ptr | object dat positie/rotatie van de Perso overneemt in toestand 5 | `0x44db50` |
| 0x55c | ptr | object dat gevolgd wordt in toestand 7 | `0x44e1c0` |
| 0x560/0x561 | u8 | vlaggen → `0x401480/0x401440(App, 0.5)` (fade) in toestand 5 | `0x44db50` |
| 0x564 | vec3 | positie voor anim 0x49 | `0x44e690` |
| 0x57c | u8 | trigger anim 0x49 (toestand 5) | `0x44e679` |
| 0x58c | int | 2 = ? (toestand 6) | `0x44b980` |
| 0x590 | ptr | bereden object (`+0x132` vlag) | `0x463502` |
| 0x5b4 | int | **sprong/aanval-subtoestand 0..11** | `0x457a50` e.a. |
| 0x5b8 | f32 | timer sprongcontroller | |
| 0x5bc..0x5c4 | vec3 | verplaatsing uit sprongcontroller | `0x457a50` |
| 0x5cd | u8 | gebruik +0x5bc | `0x457a50` |
| 0x5cc | u8 | `0x42f6b0(...) == 1` (grondtest na render) | `0x44b4a0` |
| 0x5e0 | f32 | timer (→ HUD+0x2c ·2/3) | `0x457a50` |
| 0x5f4/0x5f8/0x5fc | f32/f32/u8 | timers (0.2 = `0x4a9760`) | `0x44ba70` |
| 0x68c | ptr | volgdoel (kijkrichting) | `0x45b110` |
| 0x690 | u8 | **frozen**: slaat de rest van de update over; 0 aan het eind | `0x459199`, `0x45968d` |
| 0x694 | int | halveert botsstraal in toestand 1 | |
| 0x69c..0x6a4 / 0x6ac | vec3 / u8 | tweede externe verplaatsing | `0x44bb20` |
| 0x6e4, 0x6e8, 0x6ec, 0x6f8 | | modus/timer `0x465fe0` (blokkeert Perso_Move) | |
| 0x710 | obj | accu (`+0x10 += dt`) | `0x453ca0` |
| 0x724 | int | sub-toestand van toestand 9 (0..5) | `0x454090` |
| 0x72c | vec3 | positie voor anim 0x4b | `0x454090` |
| 0x748 | ptr | object (`0x4077f0`) | `0x454090` |

Vtable `0x4aabc0`: `[1]=0x44a3d0` PostLoad, `[2]=0x42e2b0` anim/skelet-tick (basis), `[4]=0x403fe0`
typevlaggen-ptr, `[17]=0x44ab20` Reset, `[22]=0x44cda0` berichthandler, `[32]=0x4624d0` straal,
`[34]=0x44c030` positie-ptr, `[36]=0x44c720` isDead, `[38]=0x44c110` TakeHit(type),
`[10]/[18]/[20]/[42]=0x462c60` leeg.

## 4. `0x4459c0(Game, dt)` en `0x451cc0(dt)`

### 4.1 `0x4459c0` – dood/respawn-sequentie met iris (Game+0x14, tabel `0x445b80`)
`Game+0x10 = dt`; eerst `0x451cc0(dt)` (§4.2). **Iris** `Game+4` (`0x4776b0(van, naar, duur)`:
`+0=van, +4=naar, +8=duur, +0xc=t`; `0x477920(dt)`: `t += dt`, waarde = van − (van−naar)·min(t/duur,1)
→ `0x4776d0(waarde)`, geeft klaar). `0x4776d0` is **geen helderheid** maar dezelfde iris als in de menu's
(MENU_NEWGAME.md §2.7): een dekkend zwarte ring van 50 segmenten om het schermmidden (320, 240)
(`0x4ab5bc`/`0x4abd40`, de enige lezers van die constanten), binnenstraal `waarde·0.99·480`, buitenstraal 475. Het beeld
blijft dus op volle helderheid binnen een krimpende/groeiende cirkel, en omdat de volgcamera Woody in het midden houdt,
zie je hem in het gat. De iris wordt alleen getekend in toestand 0, 1 en 4 (de aanroepen van `0x477920`); de functie loopt in
stap 33 van het frame, dus **ná de HUD** (stap 29) en ook tijdens een cinematic (alleen de pauze houdt hem stil).
**Levelstart**: de Game-ctor `0x445850` doet `0x445930`, één iris-tick van 0.1 s en dan iris **0 → 1 in 1 s**, toestand 1:
elk level (ook House achter het titelmenu) gaat open met de iris, gelijktijdig met de vlakke fade-in van 1 s uit de laadroutine
(`0x404332`, `0x401440(1.0)`). De scriptfades 1150/1151/1152 zijn iets anders: zwarte 640×480-rechthoeken van de App-faders
(`0x445bf0`, `0x4014c0`); het begin van het W1B-baasgevecht gebruikt alleen die (`1152` 0.3 s, dan `1150 [100]`).
Andere schrijvers van `Game+0x14`/`+0xc` zijn er niet (buiten `0x445850`/`0x445930`/`0x4459c0`); `0x445930` komt verder
alleen nog uit het pauzemenu (herstart, `0x40584d`: toestand 0 met timer 0.1 ⇒ nogmaals respawn, iris open).
* state 0: `0x451bd0()` (onweer uit), fader-tick, `Game+0xc -= dt`; ≤ 0 ⇒ `0x445930(Game)`
  (fader (0,0,0.1), timer 0.1, `Perso->0x44a810(0)`, alle actoren `vtbl[28]()` via `0x40c040`,
  `0x458f90(Game+8)`), iris (0 → 1.0 in 1.0 s), **state 1** (iris gaat open).
* state 1: iris klaar ⇒ **state 2** (spel).
* state 2: `Perso->vtbl[36]()` (dood) ⇒ **state 3**, timer 0.
* state 3: `0x451bd0()`; timer += dt; ≥ `Perso+0x288 − 1.0` ⇒ iris (1.0 → 0 in 1.0 s), **state 4**.
* state 4: iris dicht ⇒ `Perso->0x44c730()` (leven eraf), **state 0**, timer 0.25 (scherm zwart: de iris staat op 0).
Daarna altijd: `0x44f0a0(Game+0x64, dt)` (modus-automaat 1..4 met timers `+0x118/+0x128` en
`0x401440/0x4014c0(App, …)`); geeft true ⇒ **respawn**: `Perso.pos = Game+0x1b0`, `Perso->vtbl[17]()`
(Reset `0x44ab20`), `0x459ff0(&Perso.mover, &Game+0x198)` (kijkrichting), `0x44a650(Perso, &Game+0x198)`,
camera `0x41f9f0(2)`, `cam+0x368 = 0`, `0x41f410(0, 0)`.

### 4.2 `0x451cc0(dt)` – "onweer"/periodiek gevaar met schuilzones (klasse 80)
Globals: `0x5e59ec` actief, `0x5e59e4` timer, `0x5e59e8` interval (gezet door `0x451ba0(interval)`
vanuit de game-berichthandler `0x444b79`, argument ×0.01; id in 1010..1050), `0x5e58cc[0x5e59e0]`
= lijst klasse-80-instanties (ctor `0x451a90`, vtable `0x4ab0c4`, bericht 54: `+0x108` = straal² (default
1000), `+0x10c` = hoogte (default 400), `+0x114` = "speler binnen"). `0x451bd0` = stop (`0x46e3a0`).
Per frame als actief: `timer -= dt`; kruist 1.7 (`0x4ab13c`) ⇒ geluid 8 (donder-aankondiging).
Voor elke geregistreerde actor (`0x4c52d8[0x4c531c]`, klasse 1 = Perso): `0x451be0(pos)` zoekt een
klasse-80-zone met `dx²+dz² < +0x108` (rond `pos+(0, +0x10c·0.5, 0)`) ⇒ `zone+0x114 = 1`.
Als `timer ≤ 0`: `timer += interval`, `0x46e030(timer, 0)` (bliksemflits/effect); per Perso: geen zone
⇒ `Perso->vtbl[38](9)` (**TakeHit 9 = blikseminslag**) + geluid 7 op `pos+(180,0,0)`; twee effecten
`0x46def0` op pos en pos+(0,2000,0); in een zone: geluid 7, drie effecten `0x46df80` op willekeurige
punten (`0x43ff40`) rond de zone met straal `+0x10c − 80` en nog twee (0.4 = `0x3ecccccd`).

## 5. Open vragen
* Betekenis van P+0x08..0x18 (193/143/160, 61/81), P+0x58..0x78 (0.2, 0.6, 200, 380/400, 650/1250,
  600/1250, 2000, 250, 300) – waarschijnlijk sprong-/valparameters in `0x457a50` (niet gelezen).
* Zwaartekracht: nergens een constante gevonden in `0x44b530`/`0x4624f0`; vallen gebeurt via de
  sprongcontroller (`+0x5bc`) – de val-/sprongformule in `0x457eac..0x458b9c` moet nog gelezen worden.
* Exacte betekenis van toestanden 1, 4, 6, 8, 9 en van toets 7 (toestand 3) en toets 6.
* Wat doen `0x464ef0`, `0x465e50`, `0x465b10`, `0x458bf0`, `0x463430/0x457330` (elke frame)?
* `0x44f0a0` (Game+0x64 modus-automaat): wanneer wordt de respawn getriggerd (modus 1..4)?
* Welk game-bericht (1010..1050) start `0x451ba0`? (`switchmap.py` op `0x444870` nodig.)
* `0x42d2e0` (stap 26) en `0x42b4e0/0x42ac10` (render) zijn niet gedecompileerd.
* `0x44c720` (vtbl[36]) is uit de bytes afgeleid (`8B 91 1C 02 00 00 33 C0 83 FA 02 0F 94 C0 C3`),
  de disassembler had die functie verkeerd uitgelijnd.

(wordt hieronder ingevuld)
