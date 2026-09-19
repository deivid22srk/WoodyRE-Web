# BONUS.md — bonus-/pickup-/trigger-instantieklassen (types 30, 34, 35, 36, 37, 38, 40)

Alle adressen zijn VA's in `game/Woody.exe`
(imagebase 0x400000). Basisklasse-instantie: ctor `0x42e1a0`, vtable `0x4aa31c` (28 slots,
`+0x00..+0x6c`), berichthandler `0x42d5e0`.

## 0. Correctie op docs/classmap_raw.txt

De fabriek `0x403502` (switch: bytetabel `0x403f3c`, jumptabel `0x403e94`, sleutel = type-1)
overschrijft voor twee types de vtable NA de gedeelde ctor `0x44f300`; classmap_raw.txt mist dat:

| type | case | alloc | ctor | werkelijke vtable |
|---|---|---|---|---|
| 30 | `0x4039b6` | 0x108 | `0x44f3e0` | `0x4aad64` |
| 34 | `0x4039ed` | 0x108 | `0x44f500` | `0x4aadd8` |
| 35 | `0x403a24` | 0x108 | `0x44f300` + `mov [esi],0x4a9444` (`0x403a4e`) | **`0x4a9444`** (niet `0x4aacf0`) |
| 36 | `0x403a59` | 0x10c | `0x44f670` | `0x4aae50` |
| 37 | `0x403a90` | 0x108 | `0x44f7d0` | `0x4aaec4` |
| 38 | `0x403ac7` | 0x108 | `0x44f300` + `mov [esi],0x4a93d0` (`0x403af1`) | **`0x4a93d0`** (handler `0x44f9b0`, niet `0x44f350`) |
| 40 | `0x403afc` | 0x134 | `0x44d250` | `0x4aac74` |

`0x4aacf0` is dus alleen de (abstracte) bonus-basisklasse "CBonus"; geen enkel type gebruikt hem direct.

## 1. Klassehiërarchie en vtables

```
Instance (ctor 0x42e1a0, vtable 0x4aa31c, 28 slots, 0x104 B)
 └─ "FadeInst" (geen eigen ctor; methoden 0x44e7c0 Init, 0x44e7f0 SetFade, 0x44e810 Update, 0x44e8f0 handler)
     │   +0xfc f32 fade-doel, +0x100 f32 fade-snelheid (eenheden/s), gebruikt inst+0x6c als alpha
     ├─ CBonus (ctor 0x44f300, vtable 0x4aacf0, 29 slots, 0x108 B; +0x104 u32 flags = 0)
     │    ├─ type 30  vtable 0x4aad64   kind 1  EXTRA LEVEN
     │    ├─ type 35  vtable 0x4a9444   kind 3  (teller Perso+0x254; zie §2.3)
     │    ├─ type 34  vtable 0x4aadd8   kind 4  GEWONE BONUS (25 = hartje), 30 slots (+0x74 Respawn)
     │    ├─ type 36  vtable 0x4aae50   kind 2  UNIEK/PERSISTENT item (savegame-vlag), 0x10c B
     │    ├─ type 37  vtable 0x4aaec4   kind 5  RACE-BONUS (lijst 0x5e48d0, max 256), 30 slots (+0x74 Respawn)
     │    └─ type 38  vtable 0x4a93d0   kind 6  ONKWETSBAARHEID (duur = berichtargument), handler 0x44f9b0
     ├─ type 40 BOM (ctor 0x44d250, vtable 0x4aac74, 0x134 B, handler 0x451820) – GEEN pickup, zie §7
     └─ types 120/121 "Exploding object (Chest)" (ctor 0x451650, vtable 0x4aafd0, handler 0x451820) – zie §7
```

`kind` = `(inst+0x104 >> 5) & 0x1f`; de lage bits 0x08 (alle bonussen) resp. 0x03/0x09 zijn
klasse-vlaggen. Slot `+0x10` (`0x403fe0`) geeft `&inst->flags104` terug (basis `0x4078b0` geeft NULL):
hiermee herkent andere code "dit is een bonus/kind-object".

Afwijkende vtable-slots (alle overige slots = basisklasse):

| slot | basis | CBonus | 30 | 35 | 34 | 36 | 37 | 38 | betekenis |
|---|---|---|---|---|---|---|---|---|---|
| +0x00 | `0x42e1f0` | `0x404010` | `0x404010` | `0x404010` | `0x44f530` | `0x44f6a0` | `0x44f860` | `0x404010` | scalar deleting dtor (34/36/37 zetten hun globale tellers op 0) |
| +0x04 | `0x42e210` | `0x44e7c0` | `0x44f400` | `0x44f480` | `0x44f570` | `0x44f6e0` | `0x44f8d0` | `0x44f970` | Init: bits 0..9 van `flags104` = 0x28 / 0x68 / 0x88 / 0x48 / 0xa8 / 0xc8 (kolomvolgorde 30/35/34/36/37/38); CBonus zelf: FadeInst-Init |
| +0x08 | `0x42e2b0` | `0x44f370` | = | = | = | = | = | = | Animate/Matrices(flags): lichte variant, zie §3 |
| +0x0c | nop `0x462c60` | nop | `0x44f460` | `0x44f4e0` | `0x44f630` | `0x44f770` | `0x44f9f0` | `0x44f9f0` | **Update per frame** (aangeroepen door `0x42b400` voor elke instantie in `world+0x64`): tekent de halo-sprite, §3 |
| +0x10 | `0x4078b0` | `0x403fe0` | = | = | = | = | = | = | `return &this->flags104` |
| +0x44 | `0x42e250` | nop | nop | nop | nop | nop | nop | nop | (basis: reset anim/links) |
| +0x58 | `0x42d5e0` | `0x44f350` | = | = | = | = | = | `0x44f9b0` | berichthandler, §4 |
| +0x70 | – | `0x44f320` | `0x44f420` | `0x44f4a0` | `0x44f5c0` | `0x44f700` | `0x44f920` | `0x44f990` | **Collect()** (oppakken), §2 |
| +0x74 | – | – | – | – | `0x44f590` | – | `0x44f8f0` | – | **Respawn()**: terug in de wereld zetten, §2.5 |

### 1.1 Structvelden

| offset | type | klasse | betekenis | bewijs |
|---|---|---|---|---|
| +0x0c | vec3 | Instance | positie (effect-/HUD-/halo-positie) | `0x44f440`, `0x44f468` |
| +0x18 / +0x1c | int | Instance | sector-index / wereldcel; −1 = uit de wereld (= opgepakt/verborgen) | `0x407850`, `0x407790` |
| +0x5c | int | Instance | node_base (index in matrixbuffer `[0x509adc]+0xa0`, 48 B per node) | `0x44f5db` |
| +0x6c | f32 | Instance | transparantie 0..1 (bericht 56); > 0.9 ⇒ `inst+8 \|= 0x40` | `0x44e810` |
| +0xf8 | Model* | Instance | `model+0x4c` = lijst volume-nodes (1-based) | `0x44f5d0` |
| +0xfc | f32 | FadeInst | fade-doel (bericht 56, ×0.01) | `0x44e7f0` |
| +0x100 | f32 | FadeInst | fade-snelheid per s (bericht 57, ×0.01; Init = 100.0) | `0x44e907`, `0x44e7cf` |
| +0x104 | u32 | CBonus | flags; bits 5..9 = kind (1..6), ctor zet 0 | `0x44f308`, `0x44f400` |
| +0x108 | int | type 36 | volgnummer binnen het level = waarde van `[0x5e54f0]` bij constructie (0,1,2,…) | `0x44f67e` |

Let op: de Init van de bonus-subklassen (`0x44f400` e.d.) roept de FadeInst-Init `0x44e7c0` NIET aan
en hun Update (`+0x0c`) roept `0x44e810` niet aan ⇒ de fade (berichten 56/57) doet op bonussen niets
behalve velden zetten.

### 1.2 Globals

| adres | betekenis | gezet door |
|---|---|---|
| `0x5e54e4` | totaal aantal type-34 bonussen in het level ("Woody Bonus" totaal) | ctor `0x44f50e` (++), dtor `0x44f558` (=0) |
| `0x5e54e8` | aantal opgepakte type-34 bonussen | Collect `0x44f5c0` (++), **elke ctor zet 0** (`0x44f514`) |
| `0x5e54ec`, `0x5e4cd0` | debug: vlag + pointer naar eerste type-34 die bij Respawn buiten de wereld valt | `0x44f5a9` |
| `0x5e54f0` | aantal type-36 items in het level (uitgedeeld als volgnummer) | ctor `0x44f689`, dtor `0x44f6c6` |
| `0x5e54f4` | totaal aantal type-37 race-bonussen; lijst `0x5e48d0[256]` (`'Too much bonus race max is : %d'`) | ctor `0x44f803..0x44f817` |
| `0x5e54f8` | aantal opgepakte race-bonussen | Collect `0x44f920` (++), `0x44f8a0` (=0), ctor (=0) |
| `0x5e54fc`, `0x5e50e4` | debug als 0x5e54ec, voor race | `0x44f909` |
| `[0x5d7afc]` → `[+0]` | Game → Perso* (de speler die de bonus krijgt) | alle Collects |
| `0x5d7b44` | HUD-object | `0x44f334` |
| `0x5e48c8` | geluidswachtrij (`0x468a00(this, id, 0)` = push (id,0) in `+0x14[]`, teller +4, max +0xc) | |
| `0x5e5814` / `0x5e5818` | savegame-manager (`+0x380` = actief slot) / savegame-buffer (0 = geen) | `0x44fa13` |

Debug-toets 0x16 (`0x401af8..0x401b8a`) print `'Woody Bonus : %d / %d'` (0x5e54e8 / 0x5e54e4),
`'Race Bonus : %d / %d'` (0x5e54f8 / 0x5e54f4) en `'-> Bonus Woody/Race with ID %x is outside of the world'`.
Daarmee liggen de namen vast: **type 34 = "Bonus Woody", type 37 = "Bonus Race"**.

## 2. Oppakken

### 2.1 Wie test? Het SCRIPT, via het volume van het bonusmodel (geen afstandstest in de klasse)

Er is geen per-frame afstandstest in de bonusklassen. (`0x451770`, in EVENTS.md §4.1 als
"bonus-/schakelobject" aangeduid, hoort bij de **Chest-klasse** types 120/121, zie §7.) Elk
bonusobject heeft in het levelscript dit patroon (W1A object 62, `out/ekoasm/W1A.ekoasm` r. 732..757):

```
init:  SEND 1200 (obj 0x100003e, type 34)          ; SetTypeInstance
run:   VOL_FLAG4 <volslot>  ; JF end               ; volume.flags & 0x10
       SEND 10 (inst 0x100003e, 0, 0)              ; Collect
```

- Volume-bit 0x10 wordt alleen gezet door **PersoEnter** (`0x4444e0`: `flags |= 0x36`; PersoIn
  `0x444530` zet alleen 0x24) ⇒ één keer bij binnenkomst.
- PersoEnter komt uit de volumetest van de speler (EVENTS.md §2): `0x462760(perso, 71.0)` →
  testpunt = **Perso.pos (+0x1f4) + (0, 71, 0)**, punt-in-convex-polyeder tegen de volume-node(s)
  van het bonusmodel (`0x4303e0`), alleen voor instanties in de sector van het punt en met
  `inst+0x1c != -1`. Er is dus **geen straal**; de pickup-afstand = de vorm van de volume-node in
  het .ins-model (geschaald met inst+0x4c..).
- Bericht 10 → `0x44f350` → `vtable[+0x70]` (Collect). Er is geen controle "al opgepakt"; dat is
  impliciet: na Collect is de instantie uit de wereld (`+0x1c = -1`) en geeft ze geen volume-events meer.

### 2.2 Gemeenschappelijk einde: `0x44f320` CBonus::Collect

```c
void CBonus_Collect(CBonus *b) {                         /* 0x44f320 */
    HUD_Notify([0x5d7b44], (b->flags104 >> 5) & 0x1f, &b->pos);   /* 0x448510, §5 */
    Instance_RemoveFromWorld(b);                         /* 0x407850: uit sectorlijst, +0x1c = +0x18 = -1 */
}
```

Geen msgmask-bit, geen scriptvariabele, geen schaal-animatie: het object verdwijnt direct; het
visuele "oppakken" bestaat uit het deeltjeseffect + het HUD-icoon.

### 2.3 Per type

| type | Collect | effect `0x4793d0(n, pos)` | Perso-actie | geluid `0x468a00([0x5e48c8], id, 0)` | HUD kind |
|---|---|---|---|---|---|
| 30 extra leven | `0x44f420` | 0 op inst.pos | `0x44c7a0(perso, +1)`: `+0x250 += 1` (min 1); savegame `slot+0xc = levens` | **0** | 1 |
| 35 speciale lading | `0x44f4a0` | 1 op inst.pos | `0x44c800`: `+0x254 += 1`; savegame `slot+0x14` | **2** (na effect + basis-Collect) | 3 |
| 34 Bonus Woody | `0x44f5c0` | 2 op **wereldpositie van volume-node 0** (`M[model->vol_nodes[0] + node_base - 1].t`) | `[0x5e54e8]++`; `0x44c8c0`: `+0x258++, +0x25c++, +0x71c++` | **3** | 4 |
| 36 uniek item | `0x44f700` | 3 op inst.pos | `0x44c840(perso, +1)`: `+0x260 += 1` (min 0); savegame `slot+0x10`; savevlag `0x450760(slot, level, b->index)` = 1 | **1** | 2 |
| 37 Bonus Race | `0x44f920` | 4 op inst.pos | `[0x5e54f8]++`; `0x44c8f0`: `+0x264++, +0x71c++` | **5** | 5 |
| 38 onkwetsbaar | handler `0x44f9c8` + `0x44f990` | 4 op inst.pos | `0x44c890(perso, arg·0.01)`: `+0x700 = +0x704 = T`, `+0x270 = max(+0x270, T)`, `+0x280 = max(+0x280, T)` | geen | 6 (HUD doet niets: `0x448510` kent alleen 1..5) |

- `Perso+0x25c` ≥ 25 ⇒ in de Perso-update (PERSO_FRAME.md §2.1, `0x44b6d1`): geluid 4, health +1
  (max 5.0) of anders geluid 0 + extra leven; `+0x25c -= 25`. `+0x258` is het niet-afnemende totaal.
- `Perso+0x254` (type 35) wordt verbruikt in `0x458bf0` (invoeractie 0xb, `0x458c44`): als > 0 →
  `-1`, toestand `+0x750 = 1`, animatie 0x13, onkwetsbaar voor de duur van anim 0x13 (`0x44cd10`);
  als 0 → geluid 9. Het is dus munitie voor een speciale actie.
- `Perso+0x704` > 0 laat de speler wit knipperen (PERSO_MOVE.md §4.4); `+0x270` = onkwetsbaar.
- Savegame-layout: `slotbase = [0x5e5818] + slot·0x6dc` (slot = `[0x5e5814]+0x380`); `+0xc` levens,
  `+0x10` type-36-teller, `+0x14` type-35-teller, `+0x21 + level·0x3c + index` = byte "type-36 item
  #index van dit level is gepakt" (`0x450730` lezen / `0x450760` zetten; level = `[0x4c2d00]+0x68`;
  max 60 per level).

### 2.4 Effecten `0x4793d0(n, pos)` (emitter in pool `[0x5e823c]+0xdb8`, 80 B per stuk, max 2000)

| n | callback | levensduur | positie | beschrijving |
|---|---|---|---|---|
| 0 | `0x478f70`, param `+0x14 = 1` | 2.0 s | pos + (0, 50, 0) | sterren-ring variant A (type 30) |
| 1 | `0x478f70`, param 0 | 2.0 s | pos + (0, 50, 0) | sterren-ring variant B (type 35) |
| 2, 3, 4 | `0x4792d0` | 1.0 s | pos | glitter: 50 deeltjes/s (`acc += dt; n = (int)(acc·50); acc -= n·0.02`), elk 0.2 s (`0x4791f0`), op `pos + (rnd·60−30, (rnd+1)·25, rnd·60−30)`, rnd = `0x43ff40` ∈ [0,1) |

### 2.5 Terugkomen (respawn)

- **Type 34**: `Respawn` `0x44f590` = `0x407790(this, NULL)` (terug in de sector van de eigen positie;
  debugregistratie als dat mislukt). Er is **geen enkele aanroeper** van dit slot voor type 34
  gevonden (alle `call [reg+0x74]`: `0x40bfe1`, `0x4517c5`, `0x4687bb`, `0x46cdc0`, `0x44f8bc` horen
  bij andere klassen of bij type 37) ⇒ Woody-bonussen komen binnen een level niet terug, ook niet
  bij dood. Alleen het script kan ze met bericht 6 (on=1 → `0x407790`) terugzetten. De teller
  `0x5e54e8` wordt nooit verlaagd.
- **Type 37**: `0x44f8a0` = `[0x5e54f8] = 0; for (i < [0x5e54f4]) list[i]->Respawn()` – enige
  aanroeper `0x45617c` in `0x456150` (Perso, vanuit `0x44ab20` = herstart): race opnieuw beginnen;
  daarbij `Perso+0x264 = Perso+0x71c = Perso+0x4e0`.
- **Type 36**: permanent. Update `0x44f770` test elk frame de savevlag (`0x450730`); indien gezet →
  `0x407850` (object verdwijnt direct na het laden van een level waarin het al gepakt is).
- Types 30/35/38: geen respawn-code.

## 3. Per-frame gedrag

### 3.1 Update (slot +0x0c) = halo-sprite `0x479530(n, pos)`

```c
void Bonus_Update(CBonus *b) {            /* 0x44f460 (30, n=0), 0x44f4e0 (35, n=1), 0x44f630 (34, n=2, pos = volume-node 0),
                                             0x44f770 (36, n=3, eerst savevlag-test), 0x44f9f0 (37 en 38, n=4) */
    static const int sprite[5] = { 0x10013, 0x10015, 0x10014, 0x1002e, 0x10017 };  /* jumptabel 0x479654 */
    Sprite *s = [0x5e823c] + 0xb00;
    s->tex   /*+0x228*/ = sprite[n];
    s->pos   /*+0x208*/ = pos + (0, 50.0f, 0);              /* 0x4a9030 */
    s->rgb   /*+0x214..*/ = (0.5f, 0.5f, 0.5f);  s->a /*+0x220*/ = 1.0f;
    s->mode  /*+0x260*/ = 0x12;
    float w = sintab[(int)(T * 256.0f) & 0x1ff];            /* T = [0x5e85d0], += dt, modulo 2.0 s (0x46d040); tabel 512 floats op [0x5e823c] */
    s->size  /*+0x264*/ = w * w * 60.0f + 50.0f;            /* 0x4ab284, 0x4a9030: pulseert 50..110, periode 1 s */
    DrawSprite(s, 0x1b);                                    /* 0x470f10 */
}
```

Update wordt door `0x42b400` (frame `0x401d78`, this = App+0x28 = `[0x509adc]`, `+0x38` = dt)
aangeroepen voor elke instantie in de lijst `+0x64` (aantal `+0x60`). Die lijst wordt **elk frame
opnieuw opgebouwd** (`0x42a980` zet `+0x60 = 0`; `0x42a931..0x42a948` voegt de entiteiten uit de
sector-lijsten `sector+0x44 → inst+0x24` van de zichtbare sectoren toe). Een opgepakte bonus
(`0x407850`: uit de sectorlijst) krijgt dus geen Update meer ⇒ geen halo; een bonus in een niet
zichtbare sector ook niet (en type 36 test zijn savevlag pas als zijn sector zichtbaar wordt).

### 3.2 Draaien / zweven / magneet / schaal

In de bonusklassen zit **geen** code voor rotatie, bob, magneet of schaal-animatie. Slot +0x08
(`0x44f370`) is een uitgeklede versie van de basis-Animate `0x42e2b0`: (a) als transparantie
`+0x6c < 0.01` → cache-test `0x42f3d0`; (b) vlag 1 → `0x43eee0` (standaard keyframe-animatie +
node-matrices, rot 3×3 × schaal +0x4c/+0x50/+0x54); (c) vlag 4 → `0x42f460`/`0x42f490`
(render-cache-klasse naar grootte). Eventueel draaien/zweven komt dus uit de **keyframe-animatie
van het model** (animatietoestand `+0xa0..+0xb0`, gestart door het .ins-record of bericht 4), niet
uit code. Dat type 34 de halo en het effect op de *volume-node* zet in plaats van op `inst.pos`
wijst erop dat bij dat model de node beweegt (geanimeerd zweven).

## 4. Berichten

Keten: `0x44f9b0` (alleen type 38) → `0x44f350` (CBonus) → `0x44e8f0` (FadeInst) → `0x42d5e0` (basis).
Berichtrecord: `[0]` id, `[8]` inst-id, `[0xc]` eerste argument.

| id | args | handler | betekenis |
|---|---|---|---|
| 10 | inst, a, b | `0x44f362` (types 30, 34..37) | **Collect**: `this->vtable[+0x70]()`; a en b worden genegeerd (script stuurt 0, 0) |
| 10 | inst, a, b | `0x44f9c8` (type 38) | `Perso_SetInvincible(a · 0.01 s)` (`0x44c890`, const `0x4aa0ac` = 0.01) en daarna Collect |
| 56 | inst, v | `0x44e91b` | fade-doel `+0xfc = v·0.01` via `0x44e7f0(v, 0)` (2e arg 0 ⇒ transparantie `+0x6c` niet direct gezet). Op bonussen zonder effect (§1.1); werkt op type 40 / chests |
| 57 | inst, v | `0x44e907` | fade-snelheid `+0x100 = v·0.01` per seconde |
| 29 | inst | `0x451832` (types 40, 120, 121) | `this->vtable[+0x44]()`: type 40 → `0x44d320` (bom deactiveren: projectiel `+0x124` vrijgeven, state `+0x108 = 0`, bytes `+0x131..0x133 = 0`, uit de wereld); chest → `0x451730` (reset: anim-reset `0x42e250`, terug in wereld `0x4077f0`, fade-snelheid 100, fade-doel 0, msgmask 0x20 wissen) |
| overige | | `0x42d5e0` | basisberichten (6 = tonen/verbergen werkt ook op bonussen: on=1 is de enige manier om een type-34 terug te zetten) |

Aanvulling op MESSAGES.md: 10 is dus niet "volumes/triggers" maar **"bonus oppakken"**; het
gebruik-aantal 0 in MESSAGES.md klopt niet voor W1A (elk bonusobject stuurt het, met `SEND 4`).

## 5. HUD-koppeling

`0x448510(hud = [0x5d7b44], kind, pos)` (jumptabel `0x448604`, kind 1..5):

| kind | type | HUD-functie (op `hud+0x30`) | HUD-vlag = 1 |
|---|---|---|---|
| 1 | 30 | `0x461300(pos, hud+8)` | `hud+0x39` |
| 2 | 36 | `0x461420(pos)` | `hud+0x38` |
| 3 | 35 | `0x461560(pos)` | `hud+0x3a` |
| 4 | 34 | `0x4616a0(pos)`; wist eerst byte `hud+0x10` | `hud+0x36` |
| 5 | 37 | `0x461760(pos)` | `hud+0x37` |

De `0x4613xx`-functies starten een HUD-animatie met de 3D-positie als bron (`0x47b230(pos, …)`,
duur-constante 0.2 = `0x3e4ccccd`); niet verder uitgewerkt. De HUD-waarden zelf komen elk frame uit
`0x44ae60` (Perso → HUD): `0x448380(+0x25c)`, `0x4482c0(levens−1)`, `0x448440(health)`,
`hud+0x24 = +0x264`, `0x448300(+0x254)`, `0x448340(+0x260)`.

HUD-tekst (`0x447660`): modus `hud+4 == 0` (normaal) tekent `"[0x5e54e8] / [0x5e54e4]"` (gepakt /
totaal type 34; `0x4479d6..0x447a0d`), behalve in levels (`App+0x68`) 1, 11 en 18; modus 1 (race)
tekent `"hud+0x24 / [0x5e54f4]"` (`0x4477c8..0x4477fc`). `0x44a6a0` geeft het totaal
(`0x5e54f4` als Perso-kind 4/5, anders `0x5e54e4`) aan de Perso-accumulator `+0x710` (`0x453c80`).

## 6. Pseudo-C (samenvatting)

```c
enum { SND_LIFE = 0, SND_ITEM36 = 1, SND_CHARGE = 2, SND_BONUS = 3, SND_25BONUS = 4, SND_RACE = 5 };
#define PICKUP_TEST_Y      71.0f   /* testpunt = perso.pos + (0,71,0), 0x462760 */
#define BONUS_PER_HEART    25      /* 0x44b6d1 */
#define HEALTH_MAX         5.0f
#define HALO_Y             50.0f
#define HALO_SIZE_MIN      50.0f
#define HALO_SIZE_AMP      60.0f   /* size = 50 + 60*sin^2(2*pi*t/2s) */
#define HALO_RGB           0.5f
#define GLITTER_LIFE       1.0f
#define GLITTER_RATE       50.0f   /* per seconde, deeltje 0.2 s */
#define RING_LIFE          2.0f

/* script: if (volume.flags & 0x10 /*PersoEnter*/) send(10, inst, 0, 0); */
bool Bonus_OnMessage(CBonus *b, const Msg *m) {          /* 0x44f350 / 0x44f9b0 */
    if (m->id != 10) return FadeInst_OnMessage(b, m);    /* 56, 57, basis */
    if (b->type == 38) Perso_SetInvincible(perso, m->arg0 * 0.01f);
    switch (b->type) {
    case 30: Perso_AddLives(perso, 1); Sound(0); Effect(0, b->pos); break;
    case 35: perso->charges++; save.charges = perso->charges; Effect(1, b->pos); break; /* Sound(2) na afloop */
    case 34: g_bonusTaken++; Effect(2, VolNodePos(b)); perso->bonusTotal++; perso->bonusCount++; perso->stat71c++; Sound(3); break;
    case 36: Effect(3, b->pos); perso->items++; save.items = perso->items; Sound(1); save.taken[level][b->index] = 1; break;
    case 37: g_raceTaken++; Effect(4, b->pos); /* Collect */ perso->raceCount++; perso->stat71c++; Sound(5); break;
    case 38: Effect(4, b->pos); break;
    }
    HUD_Notify(hud, kind(b), &b->pos);                   /* 0x448510 */
    World_Remove(b);                                     /* 0x407850: cell = -1 ⇒ onzichtbaar, geen volume-events */
    if (b->type == 35) Sound(2);
    return false;
}
void Bonus_Update(CBonus *b) {                           /* slot +0x0c, elk frame */
    if (b->type == 36 && save.taken[level][b->index]) { World_Remove(b); return; }
    DrawHalo(haloSprite[b->type], (b->type == 34 ? VolNodePos(b) : b->pos) + (0, HALO_Y, 0),
             HALO_SIZE_MIN + HALO_SIZE_AMP * sq(sin512[(int)(T * 256) & 511]));
}
/* Perso-update: */ if (perso->bonusCount >= 25) { Sound(4); if (health < 5) health += 1; else { Sound(0); lives++; } perso->bonusCount -= 25; }
```

## 7. Type 40 en de Chest-klasse (geen pickups)

- **Type 40 = bom** (ctor `0x44d250`): pool `0x5e4880[16]`, teller `0x5e487c`; globals
  `[0x5e48c0] = 4.0`, `[0x5e48c4] = 2.0`. Init `0x44d2e0`: FadeInst-Init, kind-bits 0x23, `inst+8 |= 0x20`,
  `+0xf0 |= 1`, `+0x124 = 0`. Velden: `+0x108` toestand 0..6, `+0x10c/+0x110` tijden, `+0x124`
  projectiel-object (`0x4490a0`), `+0x12c` script-var (krijgt 1 bij ontploffen, `0x44d70a`),
  `+0x131` "in gebruik". Game-bericht 1090 → `0x44d5d0` zoekt een vrije bom (`'Pas de bombes, ou plus
  assez de bombes dans ce niveau...'`) en start hem met `0x44d4d0`. Ontploffing `0x44d6e0` → state 6,
  SetVar, **`0x44d650`: straal 400.0** (`0x43c80000`): alle type-17-objecten met toestand 1/2 krijgen
  `vtable[+0xa0](&pos, 400)`, alle chests `vtable[+0x70](&pos, 400)`; geluid, effect `0x477060`,
  fade uit. Update = `0x44e810` (fade). Bericht 29 = deactiveren (§4).
- **Types 120/121 = "Exploding objects (as Chest)"** (ctor `0x451650`, vtable `0x4aafd0`, lijst
  `0x5e581c[32]`): `0x451770(pos, r)` = als msgmask 0x20 nog niet gezet en `|pos − inst.pos|² < r²`
  → `vtable[+0x74]` = `0x4517d0`: `0x436ca0(this, 1.0, 0, -1, -1, -1)`, effect `0x477060(1, &pos, 0)`,
  fade-snelheid 0.5/s naar transparantie 1.0, **msgmask 0x20 zetten** (script reageert daarop).
  `0x451730` = reset. Dit corrigeert EVENTS.md §4.1: de rij "bonus-/schakelobject" is de chest;
  `+0x100 = 0.5` is een fade-snelheid, geen schaal, en de "afstand tot punt" is de bomexplosie.

## 8. Recept voor herimplementatie in C (src/main_engine.c / src/player.c)

1. Bij bericht 1200 (SetTypeInstance) met type 30/34/35/36/37/38: markeer de instantie als bonus,
   bewaar `type`; type 34: `bonus_total++` en `bonus_taken = 0`; type 37: `race_total++`, in lijst;
   type 36: `index = item36_count++`.
2. De bestaande volumetest van de speler (punt = pos + (0,71,0), alleen instanties met cel ≠ −1)
   levert al `eko_vol_perso_enter`; het script doet de rest en stuurt bericht 10. Niets extra's
   testen in C.
3. Implementeer instantiebericht 10 voor deze types volgens §6 (`Bonus_OnMessage`): tellers op de
   speler (`lives`, `charges`, `bonusTotal/bonusCount`, `items`, `raceCount`), globale tellers,
   geluid-id 0/2/3/1/5, daarna instantie inactief maken (niet meer tekenen, niet meer in de
   volumetest). Type 38: `invincible = max(invincible, arg·0.01)`, knippertimer = arg·0.01.
4. In de spelerupdate: `if (bonusCount >= 25) { health<5 ? health++ : lives++; bonusCount -= 25; }`.
5. Per frame voor elke actieve bonus: additieve billboard op pos + (0,50,0), kleur 0.5, grootte
   `50 + 60·sin²(π·t)` (t in s; periode 1 s), sprite-id per type 0x10013/0x10015/0x10014/0x1002e/0x10017.
   Type 34: gebruik de wereldpositie van de eerste volume-node van het model i.p.v. inst.pos.
6. Bij oppakken: glitter-emitter (1 s, 50 deeltjes/s, 0.2 s per deeltje, doos ±30 × 25..50 × ±30)
   voor 34/36/37/38; sterren-ring (2 s, op +50 y) voor 30/35.
7. Draaien/zweven: speel de keyframe-animatie van het model af zoals voor elke instantie; geen
   extra code.
8. Geen respawn bij dood voor 30/34/35/36/38. Race-herstart: alle type 37 terug in de wereld,
   `race_taken = 0`, `raceCount` = opgeslagen waarde.
9. Type 36: bij levelstart (eerste Update) verbergen als `save.taken[level][index]`.
10. HUD: toon `bonus_taken / bonus_total` (niet in levels 1, 11, 18); in race-modus
    `raceCount / race_total`.

## 9. Open vragen

1. De vorm/grootte van de volume-node van de bonusmodellen (.ins) is niet uitgemeten; die bepaalt
   de werkelijke pickup-afstand.
2. Het exacte zichtbaarheidscriterium waarmee `0x42a858..` de per-frame lijst vult (welke sectoren)
   is niet uitgewerkt; en `0x479530` schrijft elke keer in hetzelfde sprite-record
   `[0x5e823c]+0xb00` en tekent direct (`0x470f10(…, 0x1b)`), de betekenis van vlag 0x1b is open.
3. De HUD-animaties `0x461300/0x461420/0x461560/0x4616a0/0x461760` (icoon dat van de 3D-positie
   naar de teller vliegt?) zijn niet uitgewerkt.
4. Betekenis van type 36 in speltermen (welk voorwerp; sprite 0x1002e) en van `Perso+0x71c`.
5. Welke actie `0x458bf0` precies is (verbruikt `Perso+0x254`, animatie 0x13).
6. Sterren-ring `0x478f70` (tabel `0x4b7990`, consts `0x4abc90/0x4abd60/0x4abc94/0x4ab294`) niet uitgewerkt.
7. Type 40: toestanden 1..5 (`0x44d850..0x44d96e`) alleen globaal bekeken.
8. Of de bonusmodellen in het .ins een actieve animatie hebben (draaien/zweven) is niet gecontroleerd.
