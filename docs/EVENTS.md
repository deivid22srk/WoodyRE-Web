# Engine → script-VM: gameplay-events (Woody.exe, build 17-10-2001)

Status: **werkdocument, incrementeel bijgewerkt.** Alle adressen verwijzen naar Woody.exe
(imagebase 0x400000) en zijn na te lezen met `python tools/drange.py START END` en
`python tools/funcinfo.py out/disasm_full.txt ADDR`.

Doel: precies vastleggen welke berichten de engine naar de EKO-VM stuurt als gevolg van
gameplay, met welke argumenten, op welk moment in het frame, zodat `src/main_engine.c` dit
1:1 kan nabouwen met de API uit `src/ekovm.h`.

## 0. Samenvatting

- Er bestaan maar **drie soorten** engine→VM-signalen: (1) volume Enter/In/Leave (+ Perso-varianten),
  (2) collision Press/In/UnPress (+ Perso-varianten), (3) `SetVar(var, waarde)` op een scriptvariabele.
  Daarnaast is er een per-object **berichtmasker** (`msgmask`, bits 0x10/0x20/0x200) dat de engine
  zet/wist en dat scripts met opcode `MSGTEST` lezen. Er zijn géén aparte berichten voor "animatie
  klaar", "camera klaar", "bonus opgepakt" of "timer": dat loopt via volumes/collisions, `SetVar`
  of het masker.
- Actors die volume-events veroorzaken: de **speler** (Perso-varianten, `0x4303e0`) en de
  **camera** (gewone varianten, `0x430210`). Vijanden/objecten doen dat niet.
- Actors die collision-events veroorzaken: speler (Perso-varianten via `0x436f00`), vijanden en
  projectielen (gewone varianten via `0x436dc0`). "Collision" betekent: de actor **staat op** een
  press-node (typecode 1) van een instantie; het is een bijproduct van de grondtest (`GetHeight`).
- Alle signalen worden binnen het frame vóór de VM-tick (`0x4019c0`) gegeven; de VM verwerkt ze in
  dezelfde tick (watchers gewekt via `0x443d20/0x443db0` → `0x442210`).

## 1. De twee kanalen engine → VM

1. **Volume-events via de callback-tabel** `0x5cc360[id]` (dispatcher `0x441c90`).
   Enige aanroeper van `0x441c90` is de volume-test `0x430210` (met id 0x65/0x66/0x67 = 101/102/103).
   De callbacks staan in `0x441ed0`:
   `0x5cc4f0 (100) = 0x441d50 SetVar` (nergens aangeroepen), `0x5cc4f4 (101) = 0x441da0 Enter`,
   `0x5cc4f8 (102) = 0x441e70 Leave`, `0x5cc4fc (103) = 0x441e10 In`.
   De dispatcher: `if (n < 0x500 && id < 2001) callback[id](&record[0x5cc330 + 48*n], &args)`,
   `n = [0x5ce2a4]` (wordt nergens gewijzigd → altijd 0; het record is dus een scratch-record van 48
   bytes, geen wachtrij). Het record wordt gevuld als `{id, nargs=3, inst_id, volid, actor_id}`
   (`0x441da0`: `[esi]=0x65, [esi+4]=3, [esi+8]=inst, [esi+0xc]=vol, [esi+0x10]=actor`) maar alleen
   `vol` en `actor` worden gebruikt.
2. **Directe aanroepen** van de VM-hulpfuncties:
   - Perso-volume: `0x441f00 PersoEnter`, `0x441f40 PersoIn`, `0x441f80 PersoLeave`
     (elk: `slot = 0x443d10(vol & 0xffffff)` → helper(slot, actor & 0xffffff) → `0x443d20(vol)` = watchers wekken);
   - collision: `0x442080 Press`, `0x4420c0 In`, `0x442100 UnPress`, `0x441fc0 PersoPress`,
     `0x442000 PersoIn`, `0x442040 PersoUnpress` (`slot = 0x443d90(col & 0xffffff)`, helper, `0x443db0(col)`);
   - variabelen: `0x443ca0 SetVar(var, waarde)` (var = id & 0xffffff; wekt watchers via `0x443ce0`);
   - berichtmasker: `0x443e50 msgmask_set(obj, bits)` / `0x443e90 msgmask_clear(obj, bits)` /
     `0x443ed0 msgmask_test(obj, bits)`;
   - `0x443ff0 actor_leave_all(actor)`: voor elk volume waarin `actor` zit (`0x4444b0`) → `0x444550 PersoLeave` + watchers wekken.

## 2. Volume-events (world_volume, VM-id 0x03xxxxxx)

### 2.1 Waar in het frame

**Speler.** Framefunctie `0x401ab0` → `0x44b530(perso, 1)` (Perso-update, `0x401d07`) → na de
beweging `0x4624f0(perso)` (`0x44b859`, grondtest + collision, zie §3.3) → aan het eind
`0x462760(perso, 71.0)` (`0x462748`): testpunt = `perso->pos (+0x1f4)` met `y += 71.0`
(≈ het lichaamsmidden boven de voeten), dan **`0x4347b0(&punt, -1, perso)`** → per kandidaat-instantie
`0x4303e0(inst, punt, perso)` → `PersoEnter/PersoIn/PersoLeave(volid, perso->id /*+4*/)`.

**Camera.** `0x459090` (camera-controller, this+4 = perso; via vtable) → `0x41eff0(dt)` =
Camera::Update (`this = [0x4c737c]`, modus in `+0x134`). Aan het einde (`0x41f379..0x41f3cf`): als
`[cam+0x664]` (het camera-object uit het `.ins`, gezet door de camera-loader `0x498dad`; `+4` = id
`0x01000000|slot`, `+0xc` = positie) niet NULL is: positie `cam+0x1d0..0x1d8` → `obj+0xc`,
`0x4077f0(obj, 0)` (spatial update), **`0x434740(&obj->pos, -1, obj)`** → `0x430210(inst, pos, obj)`
→ `Enter/In/Leave(inst_id, volid, obj->id, 0)` via `0x441c90`. Dit zijn de enige aanroepers van
`0x434740`/`0x4347b0`: alleen speler en camera zijn volume-actors.

### 2.2 `0x434740` / `0x4347b0 (float* pos, int unused, Actor* actor)` – kandidaten zoeken

Identieke functies; alleen de callee verschilt (`0x430210` resp. `0x4303e0`).

```c
World* w = [0x4c4c0c];
int leaf = 0x408180(w, pos);        // BSP-afdaling: knopen van 16 B op w+0x14; 0x40ab10(knoop,pos) = vlakafstand;
                                    // > 0 -> kind [knoop+8], anders [knoop+0xc]; negatief kind = blad, return -1-kind
Sector* s = w->sectors /*+0x1c*/[leaf];
for (i = 0; i < s->ninst /*+0x40*/; i++) {
    Instance* inst = w->instances /*+0x40*/[ s->inst_ids /*+0x44*/[i] & 0xffff ];
    if ((inst->flags /*+8*/ & 0x1f) == 1)        // objecttype 1 = "instance" (camera's zijn type 3)
        test(inst, pos, actor);                  // 0x430210 of 0x4303e0
}
```

### 2.3 `0x430210` / `0x4303e0 (this = Instance*, float* pos, Actor* actor)` – punt-in-polyeder

Geen bounding-radius-test; per volume-node wordt het punt naar de lokale ruimte van de node
getransformeerd en tegen alle vlakken getest. De twee functies zijn instructie-voor-instructie
gelijk behalve de verzonden berichten.

```c
if (inst->cell /*+0x1c*/ == -1) return;          // instantie buiten de wereld / inactief
if (inst->frame_stamp /*+0x58*/ != [[0x509adc]]) // wereldmatrices van dit frame nog niet berekend?
    inst->vtable[2](1);                          // matrices bijwerken
Model* m = inst->model /*+0xf8*/;
int nvol = m->nvol /*+0x48*/;                    // aantal volume-nodes (FORMAT_INS §2 #10)
for (i = 0; i < nvol; i++) {
    int nodeidx = m->vol_nodes /*+0x4c*/[i];     // 1-based
    Node* node = m->nodes /*+0x68*/ + (nodeidx - 1) * 0x90;
    // wereldmatrix (3x4, 48 B) van de node: [[0x509adc]+0xa0][inst->node_base /*+0x5c*/ + nodeidx - 1]
    Mat34* M = &[[0x509adc]+0xa0][inst->node_base + nodeidx - 1];
    // 0x440fc0(M, sx, sy, sz, out): inverse van M met schaal inst+0x4c/+0x50/+0x54
    // (out = 9 floats rijen r0,r1,r2 op esp+0x24.., translatie op esp+0x48..)
    Mat34 inv; 0x440fc0(M, inst->sx, inst->sy, inst->sz, &inv);
    float lx = inv.r0x*pos.x + inv.r0y*pos.y + inv.r0z*pos.z + inv.tx;   // 0x4302aa..0x430310
    float ly = inv.r1x*pos.x + inv.r1y*pos.y + inv.r1z*pos.z + inv.ty;
    float lz = inv.r2x*pos.x + inv.r2y*pos.y + inv.r2z*pos.z + inv.tz;
    int nplanes = node->npolys /*+4*/, passed = 0;
    Poly* p = node->polys /*+0x10*/;             // polygonen met vlak (n, d) op +8..+0x14 (FORMAT_INS §2.4)
    for (; passed < nplanes; passed++) {         // 0x430316..0x43034f
        float d = lx*p->nx /*+8*/ + ly*p->ny /*+0xc*/ + lz*p->nz /*+0x10*/ + p->d /*+0x14*/;
        if (d > 0.0f) break;                     // fcomp met [0x4a9004]=0.0: buiten dit vlak -> buiten
        p = (Poly*)(((char*)p + 2 * p->nverts /*u16 +2*/ + 0x1a) & ~3);   // volgende polygoon
    }
    int inside = (passed == nplanes);
    uint32_t volid = inst->vmids /*+0x70*/[i];   // 0x03xxxxxx
    int was_in = 0x443e20(actor->id /*+4*/, volid);   // = eko_vol_has_actor_f1 (actor in lijst zonder Leave-vlag)
    // 0x430210 (camera):
    if (!was_in) { if (inside) 0x441c90(0x65, inst->id, volid, actor->id, 0); }
    else if (inside)          0x441c90(0x67, inst->id, volid, actor->id, 0);
    else                      0x441c90(0x66, inst->id, volid, actor->id, 0);
    // 0x4303e0 (speler): zelfde logica met 0x441f00 / 0x441f40 / 0x441f80 (volid, actor->id)
}
```

Merk op: dit gebeurt **elk frame** en zonder cache: zolang de speler in het volume staat komt er
elk frame een `In`; het frame waarin hij eruit is komt er één `Leave` (daarna is `was_in` 0 omdat de
Leave-vlag gezet is, zie `eko_vol_has_actor_f1`), en bij binnenkomen één `Enter`.

### 2.4 Afbeelding op de C-VM

| gebeurtenis | exe | C-API (`src/ekovm.h`) |
|---|---|---|
| camera komt volume binnen | `0x441c90(0x65,…)` → `0x441da0` → `0x4443f0` | `eko_vol_enter(vm, volid & 0xffffff, actor_id & 0xffffff)` |
| camera blijft in volume (elk frame) | `0x67` → `0x441e10` → `0x444430` | `eko_vol_in(vm, volid, actor_id)` (waarschuwt `world_volume::MessageIn sans Enter prealable`) |
| camera verlaat volume | `0x66` → `0x441e70` → `0x444470` | `eko_vol_leave(vm, volid, actor_id)` |
| speler komt binnen | `0x441f00` → `0x4444e0` | `eko_vol_perso_enter(vm, volid, perso_id)` |
| speler blijft (elk frame) | `0x441f40` → `0x444530` | `eko_vol_perso_in(vm, volid, perso_id)` |
| speler verlaat | `0x441f80` → `0x444550` | `eko_vol_perso_leave(vm, volid, perso_id)` |
| speler sterft/teleporteert/herlaadt | `0x443ff0(perso_id)` | `eko_actor_leave_all(vm, perso_id)` |

`actor_id` = `obj+4` = `0x01000000 | slot` (instantie- of cameraslot in `level[0x6c]`); de VM
maskeert met `& 0xffffff`. De C-implementatie van de vlaggen staat in `src/ekovm.c` (regels 144-191).

## 3. Collision-events (world_collision, VM-id 0x07xxxxxx)

### 3.1 Mechanisme: "staan op een press-node"

Een collision wordt niet met een aparte geometrietest gedetecteerd maar via de **grondtest**
`0x435650` ("GetHeight", ook door de bewegingscode gebruikt): de engine zoekt de ondergrond onder
een punt en zet het resultaat in globals:

| global | betekenis |
|---|---|
| `0x53a554` | type van de treffer: 0 = niets (`'GetHeight return : NotFound'`), 1 = terrein (`0x4356f7`), **2 = press-node van een instantie** (`0x431654` / `0x431d01` in `0x431200` / `0x431840`), 3 = hull (`0x430a3e`) |
| `0x53a560` | getroffen instantie |
| `0x53a58c` | index in `model->press_nodes` (S+0x5c) van de getroffen node |
| `0x53a568` | hoogte (y) van het trefpunt |
| `0x4b3108..0x4b3114` | vlak (normaal, d) van het trefpunt |

Welke instanties meedoen: de framefunctie bouwt elk frame (`0x401c6a..0x401cbc`) de lijst
`0x4c3bb4[0x4c4bec]` (max 0x3ff) van instanties met `flags&0x20` (actief), type 1 en
`model->npress /*+0x58*/ != 0`.

### 3.2 `0x436dc0(this = Probe*, float tol, float* pos, int unused, uint32 actor)` – generiek

`Probe` is een klein struct in de eigenaar (+0 vlag, +8/+0x14 contactinfo, **+0x20 = huidige
collision-id of -1**). Retourneert 1 als het punt op/bij de grond is, anders 0.

```c
0x435650(pos, tol, 1);                                   // GetHeight onder pos
if (pos->y - (hit_y + tol) < 0) {                        // voet ligt (bijna) op de ondergrond
    if (hit_type == 2) {                                 // ondergrond = press-node van een instantie
        0x436d80(this, hit_inst, hit_idx, pos);          // contactinfo bewaren (+0x431700)
        Model* m = hit_inst->model;
        if (m->ncol /*+0x60*/ != 0) {
            Node* node = m->nodes + (m->press_nodes /*+0x5c*/[hit_idx] - 1) * 0x90;
            uint32_t f = node->flags /*+0*/;
            if ((f & 0xff00) == 0x100) {                 // typecode 1 = world_collision
                int k = (int)f >> 16;                    // sub-index
                uint32_t colid = hit_inst->vmids /*+0x70*/[m->nvol /*+0x48*/ + k];   // 0x07xxxxxx
                if (this->cur == colid) { 0x4420c0 In(colid, actor);   return 1; }
                else { 0x442080 Press(colid, actor); this->cur = colid; return 1; }   // 0x436e87
            }
        }
    } else this->flag = 0;                               // 0x436d10
    /* 0x436ea9: */ if (this->cur != -1) { 0x442100 UnPress(this->cur, actor); this->cur = -1; }
    return 1;
}
this->flag = 0;                                          // 0x436ed0: in de lucht
if (this->cur != -1) { 0x442100 UnPress(this->cur, actor); this->cur = -1; }
return 0;
```

NB: bij een overgang van collision A naar collision B in één frame wordt alleen `Press(B)`
gestuurd, geen `UnPress(A)` (`this->cur` wordt overschreven).

Aanroepers (met `actor = eigen instantie-id`):
- vijandklassen: `0x410900`, `0x414f10`, `0x416a66`, `0x41a010`, `0x41a4e0` (uit `0x410b78`/`0x416a10`), `0x41b030`;
- **projectielen**: `0x4490f0(dt)` (uit `0x401ab0` op `0x401e7e`, ná de VM-tick) loopt over de 50
  vaste projectiel-objecten `0x5d7d48 + 0x104*i` (actief als byte +0xe4) → `0x4493c0(obj, dt)`;
  na de val (`0x449bc9..0x449c04`) `0x436dc0(&obj->probe, tol=obj+0x60, pos, -1, actor=obj->inst->id)`.
  Ligt het projectiel 5 frames stil (`+0xe8 >= 5`) dan stopt het (`+0xec`).

### 3.3 `0x436f00` – Perso-variant (speler)

Byte-voor-byte gelijk aan `0x436dc0` maar met `0x435b70` (wrapper om `0x435650`) en de
Perso-hulpfuncties `0x441fc0 PersoPress` / `0x442000 PersoIn` / `0x442040 PersoUnpress`.
Enige aanroeper: `0x4624f0` (Perso-grondtest, this = Perso, uit `0x44b530`). Aanroep op
`0x4626e1..0x4626f4`: `0x436f00(this = &perso->probe /*+0x298*/, tol = [esp+0x10] (loopdrempel),
pos = kopie van perso->pos (+0x1f4) met y += perso->0x1f8-offset, -1, actor = perso->id /*+4*/)`.
Het resultaat (op de grond ja/nee) gaat naar `perso+0x22c` (zie §4 msgmask 0x200).

### 3.4 Afbeelding op de C-VM

| gebeurtenis | exe | C-API |
|---|---|---|
| vijand/projectiel komt op collision te staan | `0x442080` → `0x4445e0` | `eko_col_press(vm, colid & 0xffffff, actor & 0xffffff)` |
| … staat er nog (elk frame) | `0x4420c0` → `0x444610` | `eko_col_in(…)` |
| … verlaat de collision (of komt in de lucht) | `0x442100` → `0x444650` | `eko_col_unpress(…)` |
| speler komt erop te staan | `0x441fc0` → `0x444690` | `eko_col_perso_press(…)` |
| speler staat er nog (elk frame) | `0x442000` → `0x4446d0` | `eko_col_perso_in(…)` |
| speler verlaat | `0x442040` → `0x4446f0` | `eko_col_perso_unpress(…)` (waarschuwt `world_collision::MessagePersoUnpress sans Press prealable`) |

## 4. Overige engine → VM-signalen

### 4.1 Berichtmasker per script-object (`msgmask`, opcode 58 `MSGTEST`, 59 `MSGCLEAR`)

`0x443e50(obj_id, bits)` zet bits en wekt de watchers van het object; `0x443e90` wist bits (en
wekt ook). `obj_id` = `inst+4`. Gevonden bits:

| bit | object | gezet | gewist | betekenis |
|---|---|---|---|---|
| 0x200 | speler | `0x44b89c` (in `0x44b530`, elk frame als `perso+0x22c` ≠ 0) | `0x44b8bf` (anders) | **speler staat op de grond** (resultaat van `0x436f00`) |
| 0x200 | vijand | `0x410993`, `0x41514d`, `0x416c69`, `0x41a642` (`this+0x174` bit 0 = resultaat `0x436dc0`) | `0x4109ab`, `0x415169`, `0x416c85`, `0x41a65e` | vijand staat op de grond |
| 0x10 | speler | `0x44c77c` in `0x44c730` (leven verliezen; zet `perso+0x278 = 2`) | `0x44b5de` (2 frames later, `+0x278` telt af) | **speler is gestorven** (puls van 2 frames) |
| 0x10 | vijand | `0x411729` (`this+0x10c \|= 1`, na afloop van timer `+0x15c`) | `0x4110ab` (reset), `0x41a167` (landing, `+0x174 = (…&~0x10)\|4`) | vijand-toestand (waarschijnlijk "geraakt/klaar"); exacte semantiek open |
| 0x20 | instantie | `0x4309ac`, `0x430a64` in `0x4305c0` (bol/segment-test van een bewegend object tegen de press-nodes van een instantie, als vlag 0x10 in de aanroep staat) | `0x430acf` (geen treffer) | **instantie aangeraakt/geduwd** door de speler (vermoedelijk; `0x4305c0` wordt via vtable aangeroepen) |
| 0x20 | bonus-/schakelobject (klasse met `+0x100` = schaal) | `0x451814` in `0x4517d0` (effect `0x477060`, schaal 0.5) | `0x45175c` in `0x451730` (schaal 100, reset) | object "genomen/geactiveerd" (`0x451770` test eerst `msgmask & 0x20`; als niet gezet en afstand tot punt < r → `vtable[0x74]`) |

C-API: `eko_msgmask_set(vm, obj_id & 0xffffff, bits)` / `eko_msgmask_clear(…)`.

### 4.2 `SetVar` door de engine (alle 21 aanroepers van `0x443ca0`)

| bron | var | waarde | wanneer |
|---|---|---|---|
| `0x442d30` | – | – | dit is de **STOREVAR-opcodehandler** van de VM zelf (tabel `0x5d0430`), geen engine-event |
| `0x441d87` | – | – | callback 100 (`0x441d50`), nergens aangeroepen |
| `0x444a12`, `0x445275`, `0x44549d`, `0x445511`, `0x445574`, `0x4455be` | uit bericht | 0 / 1 | **antwoorden** op script→engine-berichten in `0x444870` (1082 LevelIsEnable, 1084, 1085, 1140 SaveAuto, 1173; zie MESSAGES.md): synchroon in dezelfde tick |
| `0x405029` (`0x404e90`), `0x4051b9` | `game+0x8c` (bericht 1160 a) | 1 | hoofdlus: als de timer `game+0x98` door 0 gaat (overgang/menu-state), resp. in state-switch `0x405ba4` |
| `0x405143` (`0x40510c`) | `game+0x8c` | 4 | als var ≠ 4: timers van object `game+0x90` (bericht 1160 b) annuleren (`0x444380`, `0x4441d0`) en var = 4 (level verlaten/herstarten) |
| `0x44eb22` (`0x44eab0`, cinematic-object `level+0x64`) | `cin+0x10c` (bericht 1130) | `(int)(t·100)` | elk frame tijdens een **real-time cinematic**: verstreken tijd in 1/100 s |
| `0x44d70a` (`0x44d6e0`) | `enemy+0x12c` | 1 | **vijand/object vernietigd** (`+0x108 = 6`); aangeroepen door Boss2 (`0x40e930`), door een landend projectiel op een type-3-object met `(flags&0x3e0)==0x20` (`0x449c72`), en via vtable (`0x44a186`, `0x44d93e`) |
| `0x44d380` (`0x44d370`, via `0x44db10`) | `enemy+0x12c` | 1 | "alle vijanden met byte `+0x131` doden" (lijst `0x5e4880`), door Boss2 en `0x44ab20` |
| `0x44d612` (`0x44d5d0`) | uit bericht 1090 | 1 | script-geïnitieerd (bommen), geen event |
| `0x40d3ff` (`0x40cb80`) | `enemy+0x294` | 3 | vijandklasse: fase-/toestandswissel |
| `0x40e6c8` (`0x40dd30`, **Boss2**) | `boss+0x24c` | 3 | Boss2 volgende aanvalsfase / dood (`'Boss2 -> Vie:%f AttackPhase:%d'`) |
| `0x40fc6f` (`0x40eec0`) | `enemy+0x230` | 3 | vijandklasse: toestand bereikt |
| `0x410c03`, `0x410c49` (`0x410be0`) | `enemy+0x230` | -1 | **acknowledge**: script schrijft 1 of 2 in de var (commando), engine leest (`0x443cd0`), zet state `+0x228` en schrijft -1 terug |
| `0x454235` (`0x454164` in `0x454090`) | `perso+0x728` (bericht 1140 SaveAuto) | 1 | einde van de **SaveAuto-sequentie** van de speler (animatie 0x4b/0x4d, timer `+0x744`, fade, camera-reset) |

De var-id's `+0x294`, `+0x230`, `+0x24c` worden gezet door klassespecifieke berichten
`(inst, var)` (`0x40d7aa`, `0x4100b0`, `0x40e7e3`: `var = record[0xc] & 0xffffff`); `+0x12c` en
`+0x10c` door 1130/1160-achtige game-berichten. C-API: `eko_set_var(vm, var & 0xffffff, waarde)`.

### 4.3 `actor_leave_all` (speler uit alle volumes halen)

`0x443ff0(perso_id)` wordt aangeroepen bij: leven verliezen `0x44c730` (uit de doodsequentie
`0x4459c0`, states 0→4), teleportatie `0x44cde9` (bericht 26/30 op de Perso-klasse, `'Unknow
teleportation mode !'`), `0x44ddd8` (Perso-toestandswissel via jumptable `0x44dfdc`) en het
herladen van `SavePos.bin` (`0x44a7f5`). C-API: `eko_actor_leave_all(vm, perso_id)`.

### 4.4 Wat er NIET is

- Geen engine-bericht voor "animatie klaar" of "camera aan eind van traject": scripts gebruiken
  daarvoor `DELAY`/`DURING` en de var-antwoorden.
- Geen engine-bericht "bonus opgepakt": de bonus-tellers (`0x5e54e4..`, `'Woody Bonus : %d / %d'`)
  worden alleen bij debug-toets 0x16 geprint (`0x401ad4..0x401bb9`, incl. `'Bonus … is outside of
  the world'` voor bonussen met cel −1); het oprapen zelf is script-werk via volumes (speler-Enter op
  het bonus-volume → script stuurt berichten naar de engine).
- Timer-events komen uitsluitend uit de VM zelf (`0x442350`/`0x4423a0`).

## 5. Framevolgorde (0x401ab0, relevant voor de events)

1. `0x401c6a`: lijst van actieve press-instanties opbouwen (`0x4c3bb4`).
2. `0x401d07`: **Perso-update** `0x44b530` → beweging → `0x4624f0` (grondtest → `0x436f00` →
   PersoPress/PersoIn/PersoUnpress; `0x437180` bewegingscollisie) → `0x462760` (volumetest →
   PersoEnter/PersoIn/PersoLeave) → msgmask 0x200 zetten/wissen.
3. `0x401d5a..0x401da1`: matrices, level-/instantie-updates (`0x42b400`, `0x42abc0`, `0x42b380`,
   `0x42b4e0`, `0x42ac10`; hierin lopen de vijand-updates → `0x436dc0` → Press/In/UnPress en de
   camera-controller → `0x41eff0` → camera-volume-events).
4. `0x401e6a`: **VM-tick** `0x4019c0` (`0x444050(time)`, `0x442240`, daarna uitgaande berichten
   routeren).
5. `0x401e7e`: projectielen `0x4490f0(dt)` (→ Press/In/UnPress door projectielen; deze komen dus
   pas in de *volgende* tick bij de scripts aan).
6. `0x401e9a`: doodsequentie `0x4459c0(level, dt)` (→ `0x44c730`: `actor_leave_all`, msgmask 0x10).

Voor `src/main_engine.c`: doe per frame eerst de speler-grondtest en -volumetest, dan de overige
actors, dan `eko_tick`, dan projectielen.

## 6. Open vragen

1. `0x4305c0` (bol/segment vs press-nodes, msgmask 0x20 op de instantie) wordt via een vtable
   aangeroepen; de exacte aanroeper (vermoedelijk de bewegingscollisie `0x437180`/`0x437040` van de
   speler) en de betekenis van vlag 0x10 in de aanroep zijn niet geverifieerd (exe niet in de repo,
   dus vtables niet opzoekbaar).
2. De berichten-id's die `enemy+0x294 / +0x230 / +0x24c` registreren (klassespecifieke cases
   `0x40d7aa`, `0x4100b0`, `0x40e7e3`) zijn niet bepaald (jumptables staan in data).
3. Semantiek van msgmask 0x10 bij vijanden (`0x411729`).
4. `0x441e10` (callback 103, In) maskeert de actor-id niet met `0xffffff` voordat `0x444430`
   wordt aangeroepen; `0x444430` maskeert zelf wel (zie `eko_vol_in`). Geen functioneel verschil.
5. De volume-test gebruikt `inst->cell == -1` als uitsluitingscriterium; of gedeactiveerde
   instanties (bericht 6 met on=0 → `0x407850`) daardoor ook geen events geven is aannemelijk maar
   niet gecontroleerd.
6. De 50 projectiel-objecten (`0x5d7d48`, 0x104 B) — welke klasse ze aanmaakt (bommen/worpen) is
   niet uitgezocht; alleen het Press-gedrag is gevolgd.
