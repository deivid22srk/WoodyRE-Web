# BOMB.md — de bom (klasse type 40), de bommenwerper (lanceerder soort 0) en het bomkanon (type 21)

Statische analyse van `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, `tools/drange.py`, `tools/funcinfo.py`). Elk feit heeft een adres;
floats zijn uit de exe gelezen (PE-secties zelf ontleed). "Onzeker" = niet regel voor regel gelezen of niet tegen het draaiende origineel gecontroleerd.
Sluit aan op PROJECTILES.md (projectielpool, sjabloon `T`), ROCKET.md §7 (klasse 21), BONUS.md §7 (eerste samenvatting, hier gecorrigeerd),
PERSO_DEATH.md (Kill-soorten), SOUND.md §5. Oppakken/dragen/gooien door Woody (Perso-toestand 6), de 1090-dispenser, chests 120/121 en klasse 17
staan in **BOMB_CARRY.md**; hier alleen waar de bom zelf dat raakt.

## 0. Samenvatting

- Een bom is een **gewone instantie** (klasse 40, 0x134 B) die tijdens zijn vlucht **meegedragen** wordt door een projectiel uit de 200-pool
  (`T+0x5c = bom`, PROJECTILES §2.4). Er zijn maximaal **16 bommen per level** (pool `0x5e4880`); W2A heeft er precies 16 (model 26). Een bom
  wordt nooit gemaakt of vernietigd: `0x44d5d0` pakt de eerste vrije uit de pool, zet hem op de startplek, en na de explosie gaat hij terug in de pool
  (uit de wereldcellen gehaald = onzichtbaar, op de plek van de explosie).
- **Lont** = `T.life` van het sjabloon: bommenwerper (sjabloon 0) **2.0 s**, bomkanon = `+0x114` van het kanon (W2A: **3.2 s**). Ontploffing
  **2 frames na** het verstrijken van de lont (toestanden 4 → 5 → explosie), of 2 frames nadat het projectiel verdween (tegen een **instantie-hull**
  gevlogen, `0x44d800`), of **direct** bij het raken van een vijand van subtype 8/12, bij het verlaten van de wereld, of door Boss2.
- **Explosie** `0x44d6e0`: scriptvar := 1, dan **straal 400** (`0x43c80000`) op **alle Npc's van categorie 1 en 2** (Npc-tabel `0x4c4e00`: de Perso
  en alle vijanden) via `vtbl[40]` en op alle chests (120/121) via `vtbl[28]`; SoundFx **6**; effect `0x477060(soort = bom+0x128, pos + (0,20,0), normaal)`:
  **soort 0** voor bommenwerper/vijanden, **soort 1** (de grote raket-explosie) voor het kanon; bom meteen onzichtbaar; 0.5 s later terug in de pool.
  **Type-17-objecten worden door de bom NIET direct geraakt** (correctie op BONUS.md/OBJECTS.md, §10).
- **Wat Woody doodt**: alleen de explosie. De Perso zit niet in het trefbereik van het bom-projectiel (`T.hits_all = 0`, alleen vijandsubtype 8/12),
  dus **een bom ontploft niet bij contact met Woody**, ook niet bij de bommenwerper. Binnen **400** (3D, bommidden tot Perso-positie) bij de explosie:
  `Hit(0 schade, knockback weg van de bom)` + **`Kill(6)`** (`0x44d040`), behalve tijdens de onkwetsbaarheid na een respawn (`+0x270 > 0`).
- De bom **stuitert eindeloos** tegen terrein en instantie-press-nodes (`max_bounce = −1`, reflectie zonder energieverlies behalve de luchtdemping 0.99
  per 1/60 s), valt met 3000 u/s² (max. 800 u/s omlaag), en "ligt" pas als hij **5 frames achter elkaar** binnen 1 eenheid boven de grond is; dan rolt hij
  uit met demping 0.95 per 1/60 s. Elke nieuwe grondcontact geeft SoundFx **12**.
- **Uiterlijk**: geen rotatie, geen schaal. De bom wordt **zwart** getekend (vertexkleur × 0) en **knippert rood-verlicht** (+128 rood) met een ritme dat
  versnelt naarmate de lont opraakt (vanaf 2 s resterend: om het andere frame). Een **lont** (lijn langs de eigen typecode-0-marker die opbrandt) met een
  **vonk** (bank 0 beeld 18) aan het brandende eind. Een bommenwerper geeft bij het afvuren een rookwolkje + scherven op zijn eigen marker.
- **Bommenwerper** (lanceerder type 42 na `1001 [inst, 0]`): elke `T` s (1003) één bom langs de typecode-0-marker, **sjabloon 0**: 1500 u/s,
  zwaartekracht 15·200, lont 2.0 s; SoundFx **14** op de lanceerder; schietanimatie 0 in `1002 [8]`·0.01 s. W2A: 20 van deze (model 5), zonder doel
  (`1003 [inst, −1, −1, t]`), eindeloos, elke 1.0 .. 8.5 s; 3 lopen vanaf levelstart, de rest alleen zolang Woody in een triggervolume staat (§8).

## 1. Klasse 40

### 1.1 Aanmaak, pool, parkeerplek

- `1200 [inst, 40]` → SetTypeInstance `0x403440`: `new(0x134)` (`0x403afc`), ctor **`0x44d250`**: basis-Instance-ctor `0x42e1a0`, `+0x104 = 0`,
  vtable `0x4aac74`; **als `[0x5e487c] < 16`**: `0x5e4880[n++] = this` (een 17e bom komt niet in de pool en wordt nooit gebruikt); `[0x5e48c0] = 4.0`
  (**geen lezer** in de hele exe), `[0x5e48c4] = 2.0` (waarschuwingstijd, §2.2).
- Daarna (`0x403e3a..0x403e7a`, generiek voor alle klassen): oude instantie-data overnemen (`0x42dfc0`), `vtbl[1]` = Init `0x44d2e0`,
  **`vtbl[17]` = Reset `0x44d320`** (haalt hem uit de cellen), en dan **`0x407790(NULL)`** = weer in de cel van zijn eigen positie.
  **De geparkeerde bom blijft dus zichtbaar en botsbaar op zijn `.ins`-plek**, maar is niet "in gebruik" (`+0x131 = 0`): geen update, niet op te pakken.
  W2A parkeert ze in sector 48 bij z ≈ 2340..2425 (§8.1), aan de rand van de wereld-bbox; of die plek voor de speler zichtbaar is: niet geverifieerd (onzeker).
- Init `0x44d2e0`: FadeInst-Init `0x44e7c0` (`+0x100 = 100`, `+0xfc = 0`, `+0x6c = 0`), typewoord `(w & 0xfffffc23) | 0x23` = **categorie 3, subtype 1**
  (dit testen de projectielcode `0x449c87` en `0x44a230`: `(w & 0x1f) == 3 && (w & 0x3e0) == 0x20`), `inst+8 |= 0x20` (niet her-cellen op animatie),
  `+0xf0 |= 1` (SetFlags-bit 1: extra schaduw-/spiegelpas, INSTANCE.md §6), `+0x124 = 0`.
- Dtor `0x44d2a0` → `0x44d2c0`: vtable terug, **`[0x5e487c] = 0`** (de pool is weg zodra één bom vernietigd wordt, d.w.z. bij het opruimen van het level), basis-dtor `0x42ff30`.

### 1.2 Vtable `0x4aac74` (28 slots; vergeleken met FadeInst `0x4a9124`)

| slot | adres | FadeInst | wat |
|---|---|---|---|
| [0] | `0x44d2a0` | `0x404010` | dtor (§1.1) |
| [1] | `0x44d2e0` | `0x44e7c0` | Init (§1.1) |
| [2] | `0x42e2b0` | = | basis update/teken; met arg 1 alleen de **animatieklok** (INSTANCE.md: klok max. 1× per frame, `+0x58` = framenummer) |
| [3] | `0x44e810` | = | FadeThink (fade + animatie-events) — **niet** de bomlogica; die zit in `0x44d850` (§3) |
| [17] | **`0x44d320`** | `0x42e250` | **Reset** (§1.5) |
| [22] | **`0x451820`** | `0x44e8f0` | berichten (§1.4) |
| [23] | **`0x44d990`** | `0x4600a0` (`return 4`) | `return +0x132 ? 1 : 8`; **geen aanroeper gevonden** (geen `call [reg+0x5c]` op een instantie): onzeker |
| [26] | **`0x44d9a0`** | `0x430010` | **render-kleur**: zwart / rood knipperen (§3.4) |
| overige | | = | basis (`0x403fe0` = `return this+0x104` typewoord, botsing `0x433140`, …) |

### 1.3 Velden (`+0x104..+0x133`; basis/FadeInst-velden: INSTANCE.md)

| off | type | betekenis | schrijvers / lezers |
|---|---|---|---|
| +0x08 | flags | bit 0x40 **niet-botsbaar**: elk frame gezet als `+0x132 \|\| +0x133`, anders gewist (`0x44d892..0x44d8aa`); tijdens de projectielstraal tijdelijk gezet (`0x449da2`) | |
| +0x0c | vec3 | positie = projectielpositie + (0, 1, 0) tijdens de vlucht (`0x449b52..0x449b7c`) | |
| +0x60 | vec3 | centrum (= projectielpositie, voor de cel) | `0x44d54b`, `0x449b8e` |
| +0x6c / +0xfc / +0x100 | f32 | FadeInst: transparantie / doel / snelheid | explosie `0x44e7f0(1.0, 1)`, eind `0x44e7f0(0, 1)` |
| +0x104 | u32 | typewoord (categorie 3, subtype 1) | Init |
| **+0x108** | int | **toestand** 0..6 (§3.2) | |
| +0x10c | f32 | **lont** (s) = `T.life` | `0x44d4dd` |
| +0x110 | f32 | **waarschuwingstijd** = `min([0x5e48c4] = 2.0, lont)` | `0x44d4e3..0x44d508` |
| +0x114 | f32 | tijd sinds de start (s); bij de explosie op 0 gezet en telt dan de 0.5 s nafase | `0x44d588`, `0x44d886`, `0x44d7dd` |
| +0x118 | f32 | knipper-fase-accumulator (render-kleur) | `0x44d598`, `0x44da87`, `0x44daad` |
| +0x11c | int | knipper-framewisselaar 0/1 (render-kleur, snelle fase) | `0x44d59e`, `0x44da57` |
| +0x120 | Launcher* | lanceerder die hem afvuurde (anders 0) | `0x44d56f` (0), `0x4526e7` (L); **enige lezer**: het lont-effect `0x478e90` (rookwolk bij de monding) |
| +0x124 | Proj* | het meedragende projectiel (`0x4490a0`) | `0x44d5b8`; 0 na de explosie (`0x44d7ca`) en in Reset |
| +0x128 | int | **explosiesoort** voor `0x477060` (4e argument van `0x44d5d0`) | `0x44d5a4`; lezers `0x44d754`, `0x44d785` |
| +0x12c | int | **scriptvariabele** (VM-id), −1 = geen; krijgt 1 bij explosie/opruimen | `0x44d569`; `0x44d6f3`, `0x44d373` |
| +0x130 | u8 | ongebruikt (geen verwijzing in `0x44d250..0x44db50`) | |
| **+0x131** | u8 | **in gebruik** (pool-vrij = 0) | `0x44d575` (1), Reset (0) |
| **+0x132** | u8 | **vastgehouden** door de Perso (BOMB_CARRY.md): projectiel staat stil, niet-botsbaar | `0x463508` (1), `0x44d3ca`/`0x44d57c`/Reset (0) |
| **+0x133** | u8 | **bereden** (bomkanon): niet-botsbaar, **niet op te pakken** (`0x4634ab`) | `0x45343a` (1), `0x44d582`/Reset (0) |

### 1.4 Berichten `0x451820` (vtbl[22])

Alleen **29** (`0x451824`): `this->vtbl[17]()` = Reset, retourneert 0. Alle andere → `0x44e8f0` (FadeInst 56/57, daarna de basis-handler).
**1090 is geen instantiebericht** maar een game-bericht in de VM-handler `0x444870` (`0x444fba`, BOMB_CARRY.md). In W2A krijgen de bommen alleen `1200 [inst, 40]`.

### 1.5 Reset en opruimen

```c
void Bomb_Reset(Bomb *b) {                       /* 0x44d320 = vtbl[17]; ook bericht 29, kanon-Reset 0x452ae0, einde toestand 6 */
    if (b->proj) { if (b->proj->active) Proj_Kill(b->proj); b->proj = NULL; }        /* 0x4490e0 */
    b->state = 0; b->in_use = b->held = b->ridden = 0;
    Inst_RemoveFromCells(b);                     /* 0x407850: onzichtbaar, +0x1c = +0x18 = -1; positie blijft staan */
}
void Bomb_Discard(Bomb *b) {                     /* 0x44d370 */
    if (b->var >= 0) SetVar(b->var, 1);          /* 0x443ca0 */
    b->vtbl[17]();                               /* Reset: GEEN explosie */
}
void Bombs_DiscardAll(void) {                    /* 0x44db10: alle bommen met +0x131 */
    for (i < [0x5e487c]) if (pool[i]->in_use) Bomb_Discard(pool[i]);
}
```
`0x44db10` wordt aangeroepen door **Perso-Reset `0x44ab20`** (`0x44ad79`, dus bij elke respawn: alle vliegende/liggende/gedragen bommen verdwijnen zonder
explosie, en hun scriptvars worden 1 zodat een dispenser opnieuw kan) en door Boss2 (`0x40e6dc`).

## 2. Een bom starten

### 2.1 `Bomb* Bomb_Start(T*, u8 ground, int var, int kind)` `0x44d5d0` (cdecl)

```c
for (i = 0; i < [0x5e487c]; i++) if (!pool[i]->in_use) break;                    /* 0x44d5e6 */
if (i == n || !pool[i]) {
    Log("Pas de bombes, ou plus assez de bombes dans ce niveau...");            /* 0x4b3ad4 via 0x462c60 = lege functie: niets zichtbaar */
    if (var >= 0) SetVar(var, 1);                                               /* 0x44d612: het script wacht niet eeuwig */
    return NULL;
}
pool[i]->Launch(T, ground, var, kind);                                          /* 0x44d4d0 */
return pool[i];
```
Een bom in toestand 6 (ontploft, nog 0.5 s) is nog in gebruik. Bommenwerpers en dispenser strijden om dezelfde 16.

### 2.2 `Launch(T*, ground, var, kind)` `0x44d4d0` (thiscall, `ret 0x10`)

```c
b->fuse = T->life;  b->warn = min(2.0f /*[0x5e48c4]*/, b->fuse);
b->pos = T->pos;
if (ground) { GetHeight(&b->pos, -1, 1); b->pos.y = [0x53a568] + 1.0f; }       /* 0x435650; 0x4a900c = 1.0 (alleen de 1090-dispenser) */
b->centre = b->pos;  Inst_Recell(b, NULL);                                      /* 0x4077f0: in de cel = zichtbaar op de startplek */
b->var = var;  b->launcher = NULL;  b->in_use = 1;  b->held = b->ridden = 0;
b->t = 0;  b->state = 1;  b->blink_acc = 0;  b->blink_n = 0;  b->kind = kind;
T->carried = b;                                                                 /* 0x44d5ab: T+0x5c, in de kopie van de aanroeper */
b->proj = Proj_Alloc(T);                                                        /* 0x4490a0 -> 0x449130: visual 4 = geen geluid, geen visual */
if (!b->proj) b->vtbl[17]();                                                    /* alle 200 projectielen bezet: bom meteen weer vrij (retour blijft b!) */
```
Rotatie (`+0x28..+0x48`) wordt nergens gezet: de bom houdt de oriëntatie uit de `.ins` en **draait niet**.
Het projectiel begint op `T.pos` met `dir0 · T.speed`; de bom staat dus exact op het beginpunt van de marker.

### 2.3 Aanroepers en hun sjabloon

| aanroeper | `T` | ground | var | kind | daarna |
|---|---|---|---|---|---|
| **lanceerder `Fire`** `0x4526db` | `L+0x108` = sjabloon 0 (door `1001 [inst,0]`) + 1002-parameters; `pos`/`dir0`/`target` door Fire (§6) | 0 | −1 | **0** | `b+0x120 = L`, SoundFx 14 op L |
| **bomkanon** type 21 `0x45342c` | sjabloon 0; `gravity = 0`, `speed = +0x11c`, `damp_air = 1.0`, `life = +0x114`, `pos`/`dir0` = marker 0 (§7) | 0 | −1 | **1** | `b+0x133 = 1`, `kanon+0x168 = b`, SoundFx 14 op het kanon |
| **1090** (VM `0x444fba`) | eigen blok (BOMB_CARRY.md) | **1** | uit het bericht (`edi`) | `esi`, dat in dezelfde functie als nul voor het blok dient (`0x444e85..`) → vermoedelijk **0** (onzeker) | — |
| vijand `0x411e44` (`0x412070`) / `0x413516` (`0x413713`) | sjabloon (niet gevolgd) | 0 | −1 | 0 | geeft `b != 0` terug (bomgooiende vijanden; niet in W2A-script gecontroleerd) |

**Sjabloon 0** (`0x448c70`, regel voor regel gelezen: `[0x5d7bc0..0x5d7c0c]`): straal **30**, zwaartekracht **15** (×200 = 3000 u/s²), snelheid **1500**,
demping grond **0.95** / lucht **0.99** per 1/60 s, levensduur **2.0**, schade **1000**, max. stuiters **−1**, doel 0, richthoogte 125, stuur 0.025 / 0 /
tijden 2.0 / 0, grenzen 0.7 / 0.7, eigenaar 0, meegedragen 0, visueel **4** (geen), `hits_all` **0**.

### 2.4 Opnieuw lanceren vanuit de hand: `0x44d3a0(T*)` en `0x4492d0`

Alleen als `+0x124`: `held = 0`; lokaal blok = ctor-waarden van `0x44a260` inline; `T == NULL` → sjabloon 0 met `dir0 = (0, −1, 0)` (**geen aanroeper geeft
NULL**: `0x4638a0` en `0x463dc0` geven allebei een blok mee), anders een kopie van `*T`; `T.pos = bom.pos`; `0x4492d0(proj, &T)`.
`0x4492d0` = herinit **zonder** geluid/visual: blok kopiëren, `press_frames = grounded = bounced = 0`, `pos = T.pos`, `dir = normalize(T.dir0)`,
`speed = T.speed`, `vel = dir·speed`. **Niet** gereset: leeftijd, stuiterteller, generatie, actief-vlag. Het doorgegeven blok moet `carried = bom`
bevatten, anders laat het projectiel de bom los (de Perso zorgt daarvoor; BOMB_CARRY.md). De bomtimers lopen door: de lont stopt niet in de hand.

## 3. Per frame

### 3.1 Plaats in het frame (PERSO_FRAME.md §1)

Stap 18 `0x401d7d`: **`0x44d820`** = voor alle 16 pool-bommen `0x44d850` (vóór de wereld-render, vóór de VM-tick). Stap 32 `0x401e7e`: projectielen
`0x4490f0` (de vlucht, §5), ná de VM. De FadeThink `vtbl[3]` loopt met de gewone instantie-denkstappen.

### 3.2 `Bomb_Update` `0x44d850` (jumptabel `0x44d970` op `state − 1`)

```c
if (!b->in_use) return;
b->vtbl[2](1);                                   /* animatieklok (model 26 heeft 1 animatie van 10.0 s; wat die doet: onzeker) */
if ([0x5e48cc] /*pauze*/) return;
b->t += dt;                                      /* [0x509adc]+0x38 */
if (b->held || b->ridden) b->flags8 |= 0x40; else b->flags8 &= ~0x40;
switch (b->state) {
case 1: CheckProj(b); b->state = 2; Fuse_Create(b); break;                  /* 0x44d8c4; 0x478e40 = lont-effect, §3.4 */
case 2: CheckProj(b); if (b->fuse - b->warn <= b->t) b->state = 3; break;   /* 0x44d8e0 */
case 3: CheckProj(b); if (b->t >= b->fuse) b->state = 4; break;             /* 0x44d90c */
case 4: b->state = 5; break;                                                /* 0x44d932 */
case 5: Bomb_Explode(b); break;                                             /* 0x44d93e -> 0x44d6e0, zet 6 */
case 6: if (b->t >= 0.5f /*0x4a9014*/) { Fade_Set(b, 0, 1); b->vtbl[17](); } break;   /* 0x44d947: zichtbaarheid terug op 0, Reset -> pool */
}
void CheckProj(Bomb *b) { if (b->proj && !b->proj->active) b->state = 4; }  /* 0x44d800: projectiel verdwenen -> ontsteken */
```
- Toestand 1 duurt precies één update; een 4 uit `CheckProj` wordt in toestand 1 direct door 2 overschreven, in toestand 2 eventueel door 3 (één frame vertraging).
- Toestand 2 en 3 gedragen zich gelijk; het enige verschil: Boss2 laat alleen bommen in **toestand 2** ontploffen (`0x40e9ff`, §4.5). Met lont 2.0 s
  (bommenwerper) is `fuse − warn = 0`, dus toestand 3 vanaf de tweede update.
- **Tijdlijn**: start (frame F) → F+1 toestand 2 → … → eerste update met `t ≥ lont` → 4 → +1 frame 5 → +1 frame explosie. Dus **lont + 2 frames**
  (bij 60 fps ≈ 2.03 s voor de bommenwerper, ≈ 3.23 s voor het kanon). Daarna 0.5 s onzichtbaar, dan vrij.

### 3.3 Botsbaar, op te pakken

Zolang in gebruik en niet vastgehouden/bereden is de bom een **botsbare instantie** (model 26: één hull-node, één press-node): Woody kan ertegen lopen/
erop staan (onzeker hoe dat in de praktijk uitpakt). Op te pakken (`0x463446`) als `in_use && !ridden` en binnen bereik (BOMB_CARRY.md) — de toestand wordt
**niet** getest, dus in theorie ook in de onzichtbare 0.5 s na de explosie (onzeker of dat speelbaar gebeurt).

### 3.4 Uiterlijk

**a. Render-kleur `vtbl[26]` `0x44d9a0`** (elke keer dat de bom getekend wordt; modi LIGHTING.md/ROCKET.md §5.2: `[0x5ac850]` 1 = vertexkleur
**vermenigvuldigen** `0x43bdfc`, 2 = **optellen** `0x43bdd9`, daarna begrensd op 255 `0x43be20`):

```c
if (pauze) { mode = 1; col = (0,0,0); return; }                                   /* 0x44dae3 */
float rem = b->fuse - b->t;  float A, B = 0.1f;                                    /* 0x3dcccccd */
if      (rem >= 4.0f) A = 0.5f;                                                    /* 0x4a94c0, 0x4a9014 */
else if (rem >= 3.0f) A = 0.2f;                                                    /* 0x4a988c, 0x4a9760 */
else if (rem >= 2.0f) A = 0.15f;                                                   /* 0x4a9870, 0x4aa1c8 */
else if (rem >= 0.0f) A = B = 0;                                                   /* 0x44da2e */
else                  A = 0.08f;                                                   /* 0x4aace4 */
int lit;
if (A + B < dt) { lit = (++b->blink_n == 2); if (lit) b->blink_n = 0; }            /* om het andere frame */
else { b->blink_acc += dt;
       if (b->blink_acc >= A) { b->blink_acc -= A; lit = 1; } else lit = (b->blink_acc <= B); }
if (lit) { mode = 2; col = (128, 0, 0); }                                          /* 0x44dab5: +128 rood op de belichte kleur */
else     { mode = 1; col = (0, 0, 0); }                                            /* vertexkleur × 0 = zwart */
[0x5ac860] = 0;
```
Dus: **zwart, met rode flitsen van 0.1 s** elke 0.5 s (≥ 4 s over), elke 0.2 s (3..4 s), elke 0.15 s (2..3 s), en in de **laatste 2 s om het andere
frame**. Bommenwerper-bommen (lont 2.0) flikkeren dus hun hele vlucht. Na de lont (`rem < 0`, toestanden 4/5) altijd rood. Of "× 0" in het spel echt
volledig zwart oplevert (textuur × vertexkleur) is niet visueel gecontroleerd (onzeker); de code laat geen andere lezing toe.

**b. Lont en vonk** (`0x478e40`, bij de overgang 1 → 2): één effectrecord (pool `[0x5e823c]+0xdb8`, levensduur 10000 s, update **`0x478b70`**, `+8 = bom`,
`+0x34 = proj->generation`, `+0x38 = (b->launcher != 0)`, `+0x1c = normalize(v1 − v0)` en `+0x10 = v0 + dir·10` van **marker 0 van de lanceerder**).
Per frame `0x478b70`: stop (record vrij) als de bom niet meer in gebruik is of zijn klok dit frame niet liep (`bom+0x58 != frame`, `0x478ba2`);
jumptabel `0x478e18` op de bomtoestand: 0, 4, 5, 6 → alleen controleren (projectiel weg of andere generatie → record vrij); 1 → niets; **2, 3 → tekenen**:
```c
if (!rec->puffed && rec->from_launcher) {                                        /* één keer: mondingsrook */
    Shrapnel(&rec->muzzle, &rec->dir, 0, 0.25f, 1.5f);                           /* 0x476140 (niet gelezen: onzeker) */
    rec->puffed = 1;  FxAdd(0x478aa0, 0.15 s, rec->muzzle);                      /* rookwolk: beeld 24, grootte 80, wit, alpha 0.7·cos(u·π/2), vlag 0xb (niet-additief) */
}
Vec3 v[2]; GetVector(bom, 0, v, 0);                                             /* eerste typecode-0-marker van model 26 (node 5 of 6: onzeker welke eerst komt) */
float f = b->t / b->fuse;
Line(v[0], v[1] - (v[1]-v[0])·f, flags 0xc00, rgba0 (0.5,0.5,0.5,1), rgba1 (0.2,0.2,0.2,1), halve breedte 1.0);   /* 0x471a10: de lont brandt op van v1 naar v0 */
Sprite(pos = v[1] - (v[1]-v[0])·f, beeld 18 (0x10012), wit, alpha 0.7, grootte 5 + rand·10, rot rand·511, flags 7);   /* 0x478d40..0x478de3: vonk, additief */
```
(`0x4a9750` = 10, `0x4a9884` = 5, `0x4abc90` = 511; `rand` = `0x43ff40`.) Bij kanon- en dispenserbommen geen mondingsrook.

**c. Verder**: geen spin, geen schaalpuls, geen alpha-knipperen; de modelanimatie loopt (klok via `vtbl[2](1)`, zowel in `0x44d850` als in de
projectiel-update `0x449417` — de klok loopt maar één keer per frame, `+0x58`); SetFlags-bit 1 (schaduwpas).

### 3.5 Geluid (SOUND.md §5; alles 3D)

| id | wanneer | bron | adres |
|---|---|---|---|
| 14 | bom afgevuurd (alleen bommenwerper en kanon, niet de dispenser) | de lanceerder / het kanon | `0x4526f6`, `0x45344a` |
| 12 | elk nieuw grondcontact (eerste frame op de grond na een frame in de lucht: elke stuit) | de bom | `0x449c24` |
| 6 | explosie (alleen als het projectiel nog bestaat, wat normaal zo is) | de bom | `0x44d730` |

Geen lontgesis: er is geen lus-geluid (`0x468e50`) in de bomcode. SoundFx 13 (ref 6, lus) heeft geen aanroeper — mogelijk het bedoelde gesis (onzeker).

## 4. Explosie

### 4.1 `Bomb_Explode` `0x44d6e0`

```c
if (b->state == 6) return;                                        /* idempotent */
b->state = 6;
if (b->var >= 0) SetVar(b->var, 1);                               /* 0x44d70a */
Bomb_Blast(b);                                                    /* 0x44d650, §4.2: vóór geluid en effect */
if (b->proj) {
    SoundFx(6, b);                                                /* 0x44d730 */
    Vec3 p = b->proj->pos + (0, 20.0f /*0x4a9994*/, 0);
    Explosion(b->kind, &p, b->proj->bounced ? &b->proj->plane_n /*P+0xf0*/ : NULL);   /* 0x477060 */
    if (b->proj->active) Proj_Kill(b->proj);                      /* 0x4490e0 */
    b->proj = NULL;
}
b->t = 0;  Fade_Set(b, 1.0f, 1);                                  /* 0x44e7f0: transparantie en doel direct 1.0 = niet getekend (> 0.98) */
```

### 4.2 `Bomb_Blast` `0x44d650`: straal 400, twee lijsten

```c
for (i = 0; i < [0x4c5318]; i++) {                                /* Npc-tabel 0x4c4e00: ALLE Npc's (Perso + alle vijanden), niet de actorlijst 1 */
    Npc *n = 0x4c4e00[i];
    if (cat(n) == 2 || cat(n) == 1) n->vtbl[40](&b->pos /*+0xc*/, 400.0f);   /* 0x44d68d */
}
for (i = 0; i < [0x5e58b0]; i++) chest[0x5e581c + i]->vtbl[28](&b->pos, 400.0f);   /* 0x44d6bd: klasse 120/121 0x451770 (BOMB_CARRY.md) */
```
- **Perso** `vtbl[40]` = `0x44d040`: `|pos − Perso+0xc|² < r²` ⇒ richting `normalize_xz(Perso+0x1f4 − pos)` (y = 0), **`Hit(0, 0, &richting, 0, 0)`**
  (0 schade: knockback/hit-anim, `0x44d118`), rumble `0x44d1b0(+0x1d4, +0x1d0)`, **`Kill(6)`** (`0x44d139`) → PERSO_DEATH.md §3.1: anim 0x2b,
  fade na 2.5 s; genegeerd als `+0x270 > 0` (1.0 s onkwetsbaar na respawn) of al dood. De straal is **3D** en voor de Perso dezelfde **400**.
- **Vijanden** `vtbl[40]` = `0x41ae20` (ENEMY.md): binnen r ⇒ `vtbl[39](0, hp, …)` = direct dood — hier voor **alle** vijanden in het level
  (de raket gebruikt de actorlijst 1 en raakt daardoor bijna niemand; de bom gebruikt de volledige Npc-tabel).
- **Chests** 120/121: `0x451770(pos, r)`: nog niet geraakt (msgmask 0x20) en binnen r ⇒ opengeblazen (BOMB_CARRY.md / BONUS.md §7).
- **Niet**: klasse 17 (is geen Npc: ctor `0x40c3d0` → basis `0x42e1a0`; vtable `0x4a95dc` heeft 28 slots, er bestaat geen slot 40), type-42/21-objecten,
  andere bommen (geen kettingreactie), gewone instanties.

### 4.3 Effect `0x477060(soort, pos, normaal)`

| soort | wie | records (pool `[0x5e823c]+0xdb8`, 80 B, max 2000) |
|---|---|---|
| **0** | bommenwerper, vijanden, (dispenser: onzeker) | `0x4765f0` 0.25 s · `0x476710` 0.3 s · `0x476cd0` 0.2 s · scherven `0x476140(pos, &n, 0, 0.25, 1.5)`; `n` = normaal of (0, 1, 0) |
| **1** | bomkanon | als de raket (ROCKET.md §5.3): `0x476b50`, `0x476cd0`, `0x4762e0` R 1400, `0x4762e0` R 400 |
| 2 | (missile, niet de bom) | `0x4762e0` R 400 |

Soort 0 gelezen (`0x4771dd..0x477349`, `0x4765f0`, `0x476710`):
- `0x4765f0` (0.25 s, `u = t/0.25`): sprite op pos, **geel** (1, 1, 0), alpha 1, beeld **4** (`0x10004`), modus 0x12, vlaggen 3 (additief);
  grootte `−500·c³` met `c = costab[(128 + trunc(511u)) & 511] ≈ −sin(2πu)` → `≈ 500·sin³(2πu)` (tweede helft negatief: hoe de primitief een
  negatieve grootte tekent is onzeker). Plus `0x498790(…, pos, kleur (255,255,255), grootte + 100)` (`0x4a9010` = 100): vermoedelijk een dynamisch licht (onzeker).
- `0x476710` (0.3 s): **vlakke ring op de grond**: quad met normaal `n` (`S+0x230`), grijs 0.8, alpha `cos(u·π/2)`, grootte **`1300·u`** (`0x4a9860`),
  beeld **24** (`0x10018`), vlag 0xa (vlak, niet-additief).
- `0x476cd0` (deeltjes-emitter) en `0x476140` (scherven): niet gelezen (onzeker).

### 4.4 Na de explosie

De bom staat onzichtbaar (`+0x6c = 1.0`) op de explosieplek; na 0.5 s (`t` telt opnieuw vanaf 0) → transparantie terug op 0 en **Reset**: uit de cellen,
`in_use = 0`. **Hij gaat niet terug naar zijn parkeerplek**; hij blijft onzichtbaar tot een volgende `Launch` hem verplaatst en in de cel zet.

### 4.5 Andere ontstekers

- Boss2 `0x40e930` (`0x40ea47`): elke pool-bom in **toestand 2** met `|bom − bossdeel|² < 40000` (`0x4a9890`, r = 200) → `Bomb_Explode`.
- Projectiel: buiten de wereld (§5.4) en treffer op een vijand (§5.3) → direct `Bomb_Explode`.
- Respawn: `0x44db10` ruimt op **zonder** explosie (§1.5).

## 5. De projectielkant van een bom (PROJECTILES §2.2–2.5, hier nagelopen voor `T.carried`)

### 5.1 Per frame `0x4493c0`

```c
P->age += dt;                                     /* levensduur-einde geldt NIET voor een meegedragen bom (0x4493f6) */
bom->vtbl[2](1);                                  /* klok */
if (cat(bom) == 3 && bom->held) return;           /* 0x449440: vastgehouden = projectiel bevroren (ook geen zwaartekracht) */
if (!P->grounded) { vel.y -= dt·T.gravity·200; if (vel.y < -800) vel.y = -800; damp = T.damp_air; }   /* sjabloon 0: 3000 u/s², 0.99 */
else              { vel.y = 0; damp = T.damp_ground; }                                               /* 0.95 */
vel *= pow(damp, dt·60);
(doelzoeken alleen als T.target: bij de W2A-bommenwerpers en het kanon NULL)
Move(P, vel·dt);                                  /* §5.2 (roept eerst HitActors, §5.3) */
if (!P->active) return;
bom->pos = P->pos + (0, 1, 0);  bom->centre = P->pos;  Inst_Recell(bom, &bom->pos);                  /* 0x449b52..0x449bac */
if (bom->cell < 0 || bom->sector < 0) { Bomb_Explode(bom); Proj_Kill(P); return; }                    /* 0x449bb7: uit de wereld */
if (Probe(&P->probe0, -1, &(P->pos + (0, r, 0)), r, bom->id)) {                                       /* 0x436dc0, r = T.radius = 30 */
    if (P->press_frames == 0) SoundFx(12, bom);
    P->press_frames++;
} else P->press_frames = 0;
P->grounded = (P->press_frames >= 5);             /* 0x449c37: ELK frame opnieuw bepaald — van een rand af rollen = weer vallen */
if (P->grounded) { vel.y = 0; P->pos.y = [0x53a568]; }
```
`Probe` `0x436dc0` (EVENTS.md §3.2, **correctie**): `GetHeight(punt, −1, 1)` onder het punt, "op de grond" als `punt.y − (grond + r) < 1.0` (`0x4a900c`),
d.w.z. **projectielpunt minder dan 1 eenheid boven de ondergrond** (terrein óf instantie-press-node; press-events alleen bij een press-node met collision-id).
Gevolg: na elke stuit is het punt één frame "op de grond" (SoundFx 12), het ligt pas echt stil als het 5 frames op rij < 1 boven de grond blijft,
d.w.z. als de stuitsnelheid klein is geworden (ruwweg < ~140 u/s bij 3000 u/s²; afgeleid, onzeker). Liggend: horizontaal uitrollen met 0.95 per 1/60 s
(snelheid × 0.046 per seconde, uitrolafstand ≈ `v / 3.08`).

### 5.2 `Move` `0x449cc0` voor een bom

`HitActors` eerst (§5.3); dan (alleen meegedragen) meebewegen met een platform via probe `P+0x24` (`0x437040`); dan de straal `0x4359b0(old, new, −1)`
met **`bom->flags8 |= 0x40`** zolang de straal loopt (eigen hull overslaan; oude waarde teruggezet, `0x449d97..0x449dcf`). Trefsoort `[0x53a554]` (jumptabel `0x449ea0`):

| soort | wat | bom |
|---|---|---|
| 0 | niets | vliegt door |
| 1 | **terrein** (gel) | **stuiter** (`max_bounce = −1`: altijd), vlak bewaard in `P+0xf0`, `bounced = 1` → normaal voor explosiesoort 0 |
| 2 | **press-node** van een instantie | eigen bom → negeren; anders stuiter |
| 3 | `0x497ed0` antwoordt **2** (`0x435b4a`): geen vlak, geen instantie vastgelegd — **niet** "hull van een instantie" (correctie, zie onder) | eigen bom → negeren; anders **projectiel weg** (`0x449e82`) → `CheckProj` → **ontploffing 2 frames later** |

Stuiter `0x449eb0` (PROJECTILES §2.3): eindpunt gespiegeld in het vlak, snelheid behouden (geen restitutie); het enige verlies is de demping.

**Correctie (bij het porten nagelopen, `0x4359b0` regel voor regel):** de straal zet trefsoort 1 als `0x497ed0` 3 antwoordt (wereld, vlak uit `0x4c4bc0`),
trefsoort **2** als het 4 antwoordt (een instantiepolygoon: knoop `[0x4c4be0]` → `[0x53a58c]`, instantie via `[0x4c4c0c]+0x40` → `[0x53a560]`) en trefsoort 3
als het 2 antwoordt — dan zonder vlak en zonder instantie. Een instantie geraakt = altijd soort 2 = **stuiteren**; er is geen aparte hull-soort. Een bom
ontploft dus niet tegen een kist of rots maar kaatst ertegen en blijft er liggen. Wat antwoord 2 van `0x497ed0` is, is niet uitgezocht. De port test
(zoals alle instantietesten van het origineel) de press-nodes (knoopsoort 1) en laat trefsoort 3 weg. Eerste poging met hulls (soort 4) als "projectiel weg":
elke bommenwerper-bom sneuvelde direct in de hull van zijn eigen lanceerder.

### 5.3 `HitActors` `0x44a0a0` voor een bom

Actorlijst 1 van het vorige frame (`0x4c52d8`, max. 8; de Perso, Boss2, vijandtype 12). Met `T.hits_all = 0` (`0x44a0bd`) tellen **alleen actors van
categorie 2 met subtype 8 of 12** — **de Perso (categorie 1) wordt nooit getest**. Trefstraal `2·T.radius` = **60** tegen de cilinder van de actor. Bij een treffer:
- eigenaar is een Perso/vijand (categorie 1/2): **geen schade** (want `carried`), `owner->vtbl[41](1)`;
- anders (bommenwerper: eigenaar = L, categorie 6; kanon/dispenser: eigenaar 0): **`actor->vtbl[39](0, T.damage = 1000, &dir, pos, 0)`**;
- daarna altijd **`Bomb_Explode`** (`0x44a240`) en projectiel weg.

W2A heeft 18 vijanden van type 8 (`1200 [inst, 8]`); als subtype = vijandtype (ENEMY.md §1: "subtype") ontploft een bom dus op zo'n vijand (onzeker: niet
gecontroleerd welke subtypewaarde type 8 in zijn typewoord zet).

### 5.4 Wanneer ontploft een bom — volledig

1. lont op (`t ≥ fuse`) → +2 frames; 2. projectiel verdwenen (hull-treffer §5.2, of — alleen als de bom niet meer meegedragen wordt — levensduur) → +2 frames
(via `CheckProj`, alleen in toestand 2/3); 3. treffer op vijand subtype 8/12 → direct; 4. buiten de wereld → direct; 5. Boss2 → direct.
**Niet** bij contact met Woody, niet bij het raken van terrein of press-nodes, niet door de aanval van de speler (geen aanroeper).

## 6. De bommenwerper (lanceerder type 42, soort 0)

`Fire` `0x452560` (PROJECTILES §4.3), het bom-pad regel voor regel:
```c
Vec3 v[2]; GetVector(L, 0, v, 0);                                   /* marker 0 in de HUIDIGE pose (vóór het herstarten van de schietanimatie) */
Vec3 d = (L->aim && L->target) ? L->target->pos + (0, L->T.aim_h, 0) - v[0] : v[1] - v[0];   d = normalize(d);
L->T.target = L->target;  L->T.pos = v[0];  L->T.dir0 = d;          /* L+0x140, L+0x108, L+0x114 */
Bomb *b = Bomb_Start(&L->T, 0, -1, 0);                              /* 0x4526db */
if (b) { b->launcher = L; SoundFx(14, L); }                         /* 0x4526e7, 0x4526f6; geen bom = geen geluid */
if (L->anim != -1 && L->anim < nanims) { anim = L->anim; ...; SetAnimSpeed(L, len(anim)/4096 / L->anim_dur); }   /* ook zonder bom */
```
- `L->T` = `1001 [inst, 0]` (`0x452330`: Reset `0x452260`, `+0x18c = 0`, `0x449070(0, L+0x108)` = sjabloon 0, **`L+0x160` eigenaar = L**) plus de
  1002-parameters (PROJECTILES §3). In W2A alleen `[7, 0]` (schietanimatie 0), `[8, 50|70]` (0.5/0.7 s) en bij lanceerder 13 `[2, 200]` (= 2.0 s,
  gelijk aan het sjabloon). Model 5 heeft één animatie van 8.0 s (`32768/4096`); die wordt dus met snelheid 16 (0.5 s) of 11.4 (0.7 s) afgespeeld.
- Doel: `1003 [inst, doel, aantal, t]` (`0x444d60..0x444d8f`: `+0xc` doel, −1 → NULL; `+0x10` aantal; `+0x14` t·0.01). W2A: **altijd `[inst, −1, −1, t]`**
  (de opdracht las "1, 1": de disassemblage heeft `PUSH 1; NEG`) ⇒ **geen doel** (dus geen doelzoeken ondanks stuurwaarden 0.025/2 s in sjabloon 0),
  **eindeloos**, periode t. Eerste bom 1–2 frames na 1003, daarna elke t (PROJECTILES §4.2).
- Richting = marker 0 van de lanceerder in zijn huidige pose. Model 5: marker-node 9 (typecode 0), lokaal (0, 119.6, 61.9) → (0, 234.3, 100.4), dus ≈ **71°
  omhoog** t.o.v. het lokale xz-vlak; node 4 (ouder) heeft een rotatiespoor, zodat de werkelijke hoek van de pose afhangt. **In de port gemeten**: in de pose
  bij het vuren is de richting ≈ (0, 0.32, −0.95), dus **≈ 19° omhoog** (W2A 13/14/15/339..341); de bommen vliegen door de gang van het schip. Met 71°, 1500 u/s,
  3000 u/s², luchtdemping 0.99 en −800-grens (eigen simulatie op 60 fps): top ≈ 270 boven de monding, landing op starthoogte na ≈ 0.9 s op ≈ **330**
  horizontaal, daarna stuiteren; explosie na 2.0 s, ruwweg 500–600 van de lanceerder (schatting, onzeker). Een **mortier**, geen kanon.
- De lanceerder doet daarna niets meer met de bom; `+0x120` wordt alleen door het lont-effect gelezen (mondingsrook, §3.4b).
- Pool leeg → geen bom, geen geluid, de animatie speelt wel.

## 7. Klasse 21 — bomkanon, de rest van ROCKET.md §10.8

Toestand 4 → 5 (`0x453279..0x45345a`, regel voor regel): `m = GetVector(kanon, 0)`, `dir = normalize(m[1] − m[0])`; blok = ctor-waarden inline, dan
`0x449070(0, &T)` (sjabloon 0), dan `T.pos = m[0]`, `T.dir0 = dir`, **`gravity = 0`**, **`speed = +0x11c`**, **`damp_air = 1.0`**, **`life = +0x114`**
(`0x4533c8..0x453425`). Ongewijzigd uit sjabloon 0: **`damp_ground = 0.95`**, straal 30, schade 1000, `max_bounce = −1`, doel 0, eigenaar 0, `hits_all = 0`.
`Bomb_Start(&T, 0, −1, 1)`; `kanon+0x168 = b`; **`b+0x133 = 1`** (bij NULL crasht het origineel hier); SoundFx 14 op het kanon.

- **Lont** = `+0x114` = de hele vluchttijd; waarschuwing = `min(2.0, +0x114)`. W2A 411: 3.2 s, 1300 u/s → rechte lijn van ≈ 4160 eenheden
  (tenzij hij stuitert). Rood knipperen volgens §3.4a: bij 3.2 s eerst elke 0.2 s, laatste 2 s om het andere frame.
- **Tegelijk met het kanon**: het kanon telt `t` in toestand 5 tot `+0x114 − 1.0` en in 6 tot 1.0 s (ROCKET §3) → toestand 7 op ≈ 3.2 s + 1–2 frames;
  de bom ontploft op 3.2 s + 2 frames. Welke van de twee in hetzelfde frame eerst komt hangt af van de volgorde kanon-denkstap / `0x44d820` (niet
  bepaald, onzeker); het maakt niet uit: zitpositie in toestand 5..7 = `bom+0xc`, en die blijft na de explosie op de explosieplek staan.
- **Wat de Perso ziet**: blijft hij zitten, dan staat hij op afstand ≈ 0 van het bommidden → `Hit(0)` + **`Kill(6)`** (anim 0x2b), `SetState(2)` haalt
  hem uit toestand 8 (ROCKET §6.3). Het kanon draait daarna zelf in 2 s terug (toestand 8 → 0) zonder de Perso. Met onkwetsbaarheid (`+0x270 > 0`,
  of de port met `WOODY_GOD`) blijft hij in toestand 8 hangen: het kanon in toestand 8/0 levert geen zitpositie meer en afspringen mag niet
  (ROCKET §0.4: port-afwijking "dismount als het object in toestand 0").
- **Afspringen** (toestand 5..7, sprong of aanval): Woody valt; de bom vliegt door. Explosie binnen 400 van waar Woody dan is → Kill(6). Bij 1300 u/s is de
  bom ≈ 0.3 s na het afspringen al 400 verder (plus de val); vroeg genoeg springen is dus veilig.
- **Stuiteren tijdens de rit**: terrein en press-nodes → de bom (en dus Woody, die de positie volgt maar de **rotatie van het kanon** houdt) kaatst af en
  vliegt verder met 1300 u/s. Een **instantie-hull** → projectiel weg → **explosie 2 frames later** met Woody erop → Kill(6). Een vijand subtype 8/12
  (straal 60) → directe explosie. Omdat `gravity = 0` en `damp_air = 1.0` vliegt de bom zonder obstakels exact recht; komt hij 5 frames binnen 1 eenheid
  boven een ondergrond, dan geldt **grond-demping 0.95** en remt hij (onwaarschijnlijk bij een schuin omhoog gericht kanon).
- `0x4634ab` (Perso-oppak-test `0x463446`): slaat bommen met `+0x133` over → de bereden bom kan niet gepakt worden. `0x44d894`: `+0x133` (of `+0x132`)
  → `flags8 |= 0x40`, de bom is niet-botsbaar voor de Perso-beweging zolang hij bereden wordt.
- **Kanon-Reset** `0x452ae0` (volgende bericht 40 / 29) roept `bom->vtbl[17]()` op `+0x168`. Die pointer blijft na de explosie staan; is de bom intussen
  door een bommenwerper hergebruikt, dan reset het kanon **diens** vliegende bom (echte bug in het origineel; zeldzaam, niet getest).

## 8. W2A-data (`out/w2a_code.txt`, `extract/Data/W2A/W2A.ins`)

### 8.1 Bommen (16, model 26, allemaal quat (1.414, 0, 0, −1.414), schaal 1)

Objecten/instanties 187..194 op y 2289: (2075, 2343), (2160, 2343), (2253, 2341), (2346, 2341), (2072, 2421), (2163, 2421), (2248, 2424), (2349, 2424);
318..325 op y 2953: (2429, 2340), (2427, 2425), (2510, 2340), (2510, 2425), (2586, 2340), (2588, 2423), (2661, 2343), (2664, 2421) (x, z).
Script: alleen `1200 [inst, 40]`. Model 26: 1 animatie (100 frames, 10.0 s), twee typecode-0-markers (nodes 5 en 6, beide lokaal (13.4, 0, 56.0) → (34.8, 0, 79.0)),
één hull-node, één press-node.

### 8.2 Bommenwerpers (20 × model 5; allemaal `1200 [inst,42]`, `1001 [inst,0]`, `1002 [7,0]`, `1003 [inst,−1,−1,t]`, `1004` bij var = 0)

| obj/inst | positie | anim (s) | periode t | start |
|---|---|---|---|---|
| 13 | (11961, 245, −24949) | 0.5 (+ `[2,200]`) | 2.5 | levelstart + 1.0 s, altijd |
| 14 | (11846, 250, −22702) | 0.5 | 6.5 | levelstart + 1.0 s, altijd |
| 15 | (13153, 268, −22698) | 0.5 | 8.5 | levelstart + 2.5 s, altijd |
| 25 | (−225, 746, −7030) | 0.5 | 3.0 | var 3: Woody betreedt/verlaat volume 86 (obj 332, VOL_FLAG4/3) |
| 26 | (2686, 1345, −6993) | 0.5 | 3.0 | var 4: volume 239 (obj 548, betreden/verlaten) |
| 68 | (2318, 1346, −2989) | 0.5 | **1.0** | var 5: Woody in volume 87 (obj 333, VOL_FLAG5) |
| 69 | (5043, 747, −3152) | 0.5 | 3.0 | var 6: in volume 240 (obj 549) |
| 113 | (4738, 747, −14604) | 0.7 | 3.0 | var 7: in volume 83 (obj 328) |
| 314 | (5143, 748, −14886) | 0.5 | 3.0 | var 33: volume 83 (obj 328), 1003 1.5 s later, 1004 1.6 s later |
| 114 | (7954, 1346, −14578) | 0.5 | 3.0 | var 8: in volume 241 (obj 550), 1003 na 1.5 s, 1004 na 1.6 s |
| 315 | (7455, 1346, −14943) | 0.7 | 3.0 | var 34: volume 241 (obj 550) |
| 122 | (7411, 1346, −10787) | 0.5 | 3.0 | var 9: volume 84 (obj 329), 1.5 / 1.6 s vertraagd |
| 316 | (7100, 1346, −11014) | 0.5 | 3.0 | var 35: volume 84 (obj 329) |
| 123 | (10148, 746, −10780) | 0.5 | 3.0 | var 10: volume 242 (obj 551) |
| 317 | (10516, 747, −11121) | 0.5 | 3.0 | var 36: volume 242 (obj 551), 1.5 / 1.6 s vertraagd |
| 297 | (6476, 145, −25749) | 0.5 | 4.0 | var 32: volume 82 (obj 327), 1003 na 2.0 s |
| 326 | (6476, 145, −25394) | 0.5 | 4.0 | var 37: volume 82 (obj 327) |
| 339 | (10599, −1050, −24760) | 0.5 | 2.0 | var 47: volume 91 (obj 338), 1003 na 1.0 s |
| 340 | (8909, −1046, −24349) | 0.5 | 2.0 | var 48: volume 91, 1003 na 2.0 s |
| 341 | (10795, −1050, −23950) | 0.5 | 2.0 | var 49: volume 91, 1003 na 3.0 s |

VOL_FLAG5/4/3 = volumebit 0x20/0x10/0x08 (VM.md §3 opcode 29/31/32); in `src/ekovm.c` zet Perso-Enter 0x36, Perso-In 0x24, Perso-Leave 9, dus 5 = "Woody
erin (dit frame)", 4 = "betreden", 3 = "verlaten" (afgeleid uit de port-VM, niet opnieuw in de exe nagelopen). De poortobjecten zetten de var op 1 zolang Woody
in het volume is en op 0 als hij eruit gaat → `1004` (bij 114/122/314/317 1.6 s vertraagd, zodat een gestarte bom nog valt). Paren in hetzelfde volume
(113+314, 114+315, 122+316, 123+317, 297+326, 339..341) vuren **om beurten** (1.5 s resp. 1.0 s verschoven).
Poolbelasting: elke bom is ≈ 2.55 s bezet (2.0 s lont + 2 frames + 0.5 s). Lanceerder 68 alleen al houdt er ≈ 2.6 bezet; 13/14/15 lopen het hele level
(≈ 1.0 + 0.4 + 0.3). Met de volumepoorten komt W2A normaal niet aan 16 (niet gesimuleerd).

### 8.3 Overig

- 12 lanceerders van **model 23** (246, 247, 330, 386..391, 544..546) krijgen **geen 1001** → sjabloon 1 (energiebol, PROJECTILES §5.1), alleen `1002 [2, 200|400]`:
  geen bommen.
- Bomkanon **411** (model 44, (9882, 139, −12797)): `55 [1, 320]` → 3.2 s, `55 [2, 1300]` → 1300 u/s, `45 [inst, 1]` (SetFlags). ROCKET §7/§8.
- Dispenser: object 536 stuurt `1090 [538, 0x20000be (var 190), 2000]` (BOMB_CARRY.md); chest 537 (type 121).

## 9. Recept voor de port

In `src/main_engine.c`, naast `Launcher`/`Shot` (r. ~1146–1230) en `Rocket` (r. ~1598–1700). Een bom is geen `Shot`: hij heeft zwaartekracht, stuiteren en een
eigen instantie. Het eenvoudigst is het projectiel in de bom in te bedden (het origineel houdt het in de 200-pool, maar niets anders dan de bom leest het).

```c
/* ---- class 40, the bomb (docs/BOMB.md) ------------------------------------------------------------------------ */
typedef struct { float radius, gravity, speed, damp_g, damp_a, life, damage; int hits_all; } BombT;
static const BombT BOMB_T0 = { 30, 15, 1500, 0.95f, 0.99f, 2.0f, 1000, 0 };          /* template 0, 0x448c70 */
typedef struct {
    Instance *inst; int state;                     /* +0x108: 0 free, 1 started, 2/3 fuse, 4/5 igniting, 6 exploded */
    float fuse, warn, t, blink_acc; int blink_n;   /* +0x10c +0x110 +0x114 +0x118 +0x11c */
    Launcher *launcher; int kind, var;             /* +0x120, +0x128 explosion kind, +0x12c script var (-1 none) */
    int in_use, held, ridden;                      /* +0x131 +0x132 +0x133 */
    BombT T; int p_active, press, grounded, bounced; Vec3 p, vel, n;   /* the carrying projectile */
    int puff_pending;                              /* fuse record 0x478e40: muzzle smoke once, only for a launcher's bomb */
} Bomb;
static Bomb g_bombs[16]; static int g_nbombs;

/* case 1200, type 40: 0x44d250 + Init + Reset + 0x407790: stays visible and solid where the .ins put it, not in use */
if (in->type == 40 && g_nbombs < 16) { Bomb *b = &g_bombs[g_nbombs++]; memset(b, 0, sizeof *b); b->inst = in; b->var = -1; }
/* case 29 on a bomb instance: bomb_reset(b). Level change: g_nbombs = 0 (dtor 0x44d2c0). Player respawn (0x44ab20): bombs_discard_all(). */

static void bomb_reset(Bomb *b)   { b->p_active = 0; b->state = 0; b->in_use = b->held = b->ridden = 0; b->inst->visible = 0; }   /* 0x44d320 */
static void bombs_discard_all(void) { for (int i = 0; i < g_nbombs; i++) if (g_bombs[i].in_use) {                                  /* 0x44db10 */
    if (g_bombs[i].var >= 0) game_var_set(g_bombs[i].var, 1); bomb_reset(&g_bombs[i]); } }

static Bomb *bomb_start(const BombT *T, Vec3 pos, Vec3 dir, int ground, int var, int kind)   /* 0x44d5d0 + 0x44d4d0 */
{
    Bomb *b = NULL; for (int i = 0; i < g_nbombs && !b; i++) if (!g_bombs[i].in_use) b = &g_bombs[i];
    if (!b) { if (var >= 0) game_var_set(var, 1); return NULL; }
    b->T = *T; b->fuse = T->life; b->warn = T->life < 2.0f ? T->life : 2.0f;
    if (ground) { int f; float gy = gel_floor_below(&L.gel, pos, 0, 1e5f, &f); if (f) pos.y = gy + 1.0f; }
    b->p = pos; b->vel = (Vec3){ dir.x * T->speed, dir.y * T->speed, dir.z * T->speed };   /* dir normalised by the caller */
    b->inst->position = pos; b->inst->visible = 1; b->inst->fade = b->inst->fade_target = 0;
    b->var = var; b->kind = kind; b->launcher = NULL; b->in_use = 1; b->held = b->ridden = 0;
    b->t = 0; b->state = 1; b->blink_acc = 0; b->blink_n = 0;
    b->p_active = 1; b->press = b->grounded = b->bounced = 0; b->puff_pending = 0;
    return b;
}

static void bomb_explode(Bomb *b, Player *pl)                                       /* 0x44d6e0 */
{
    if (b->state == 6) return; b->state = 6;
    if (b->var >= 0) game_var_set(b->var, 1);
    Vec3 c = b->inst->position;                                                     /* 0x44d650: radius 400 */
    if (pl && !pl->dead_kind && pl->invuln_respawn <= 0) {                          /* Perso 0x44d040 */
        Vec3 d = { pl->inst->position.x - c.x, pl->inst->position.y - c.y, pl->inst->position.z - c.z };
        if (d.x*d.x + d.y*d.y + d.z*d.z < 400.0f*400.0f) { float l = sqrtf(d.x*d.x + d.z*d.z);
            player_hit(pl, 0, l > 1e-3f ? (Vec3){ d.x/l, 0, d.z/l } : (Vec3){ 0, 0, 0 }); player_kill(pl, 6); } }
    enemies_blast(&g_enemies, c, 400.0f);                                           /* vtbl[40] 0x41ae20 of every enemy: dies inside r */
    chests_blast(c, 400.0f);                                                        /* 120/121, BOMB_CARRY.md */
    audio_fx(6, b->inst, &b->inst->position.x);                                     /* the original only while +0x124 exists: always, in practice */
    Vec3 e = { b->p.x, b->p.y + 20.0f, b->p.z };                                    /* the projectile point + 20, not the bomb */
    if (b->kind == 1) game_explosion(e); else blast_add(e, 400.0f);                 /* kind 0 = 0x4765f0 / 0x476710 / ..., section 4.3; placeholder */
    b->p_active = 0; b->t = 0; b->inst->fade = b->inst->fade_target = 1.0f;        /* invisible at once */
}

/* frame step 18, before the VM tick, not while paused */
static void bombs_update(float dt, Player *pl)                                      /* 0x44d820 / 0x44d850 */
{
    for (int i = 0; i < g_nbombs; i++) { Bomb *b = &g_bombs[i]; if (!b->in_use) continue;
        b->t += dt; b->inst->noncollide = b->held || b->ridden;
        int gone = !b->p_active;                                                    /* CheckProj 0x44d800 */
        switch (b->state) {
        case 1: b->state = 2; b->puff_pending = b->launcher != NULL; break;   /* 0x478e40: fuse record + muzzle smoke */
        case 2: if (gone) b->state = 4; if (b->fuse - b->warn <= b->t) b->state = 3; break;
        case 3: if (gone) b->state = 4; if (b->t >= b->fuse) b->state = 4; break;
        case 4: b->state = 5; break;
        case 5: bomb_explode(b, pl); break;
        case 6: if (b->t >= 0.5f) { b->inst->fade = b->inst->fade_target = 0; bomb_reset(b); } break;
        }
    }
}

/* frame step 32, after the VM tick (where launchers_update runs now) */
static void bomb_fly(Bomb *b, float dt, Player *pl, const GelFile *gel)             /* 0x4493c0 with T.carried */
{
    if (!b->p_active || b->held) return;
    float damp;
    if (!b->grounded) { b->vel.y -= dt * b->T.gravity * 200.0f; if (b->vel.y < -800.0f) b->vel.y = -800.0f; damp = b->T.damp_a; }
    else { b->vel.y = 0; damp = b->T.damp_g; }
    float k = powf(damp, dt * 60.0f); b->vel.x *= k; b->vel.y *= k; b->vel.z *= k;
    Vec3 a = b->p, e = { a.x + b->vel.x*dt, a.y + b->vel.y*dt, a.z + b->vel.z*dt };
    if (enemies_sweep_hit(&g_enemies, a, e, 2.0f * b->T.radius, /*only types 8 and 12*/ 1, b->T.damage)) { b->p = e; bomb_explode(b, pl); return; }
    /* never the player: hits_all == 0 */
    Vec3 n; float f = gel_ray_hit(gel, a, e, &n);
    if (f <= 1.0f) {                                                                /* terrain: bounce, unlimited (0x449eb0) */
        Vec3 d = { e.x-a.x, e.y-a.y, e.z-a.z }; float L = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z), s = L * f - 0.01f; if (s < 0) s = 0;
        Vec3 h = { a.x + d.x/L*s, a.y + d.y/L*s, a.z + d.z/L*s };
        float q = 2.0f * ((e.x-h.x)*n.x + (e.y-h.y)*n.y + (e.z-h.z)*n.z); Vec3 r = { e.x - q*n.x, e.y - q*n.y, e.z - q*n.z };
        Vec3 u = { r.x-h.x, r.y-h.y, r.z-h.z }; float ul = sqrtf(u.x*u.x + u.y*u.y + u.z*u.z), sp = sqrtf(b->vel.x*b->vel.x + b->vel.y*b->vel.y + b->vel.z*b->vel.z);
        if (ul > 1e-6f) b->vel = (Vec3){ u.x/ul*sp, u.y/ul*sp, u.z/ul*sp };
        b->p = h; b->bounced = 1; b->n = n;
    } else b->p = e;
    /* instance hulls (hit type 3): projectile gone -> the bomb goes off 2 frames later (b->p_active = 0); needs an instance ray in the port */
    b->inst->position = (Vec3){ b->p.x, b->p.y + 1.0f, b->p.z };
    if (gel_cell(gel, b->inst->position) < 0) { bomb_explode(b, pl); return; }
    int found; float gy = gel_floor_below(gel, (Vec3){ b->p.x, b->p.y + b->T.radius, b->p.z }, 0, 1e5f, &found);
    if (found && b->p.y - gy < 1.0f) { if (b->press == 0) audio_fx(12, b->inst, &b->inst->position.x); b->press++; } else b->press = 0;
    b->grounded = b->press >= 5; if (b->grounded) { b->vel.y = 0; b->p.y = gy; }
}
```
**Tekenen** (per bom in gebruik, `state` 2/3, na de 3D-scène zoals de lasers):
- instantie-tint (vtbl[26], §3.4a): nieuwe tintmodus naast `tint_red`: "× 0" (zwart) en "+ (128/255, 0, 0)" (rood erbij), met de knipperregels uit §3.4a.
- lont: `inst_vector(b->inst, 0, &v0, &dv)`, `v1 = v0 + dv`; `f = t/fuse`; lijn `v0 → v1 − (v1−v0)·f`, halve breedte 1, kleur 0.5 → 0.2 grijs;
  vonk `hud_world_fx(18, v1 − (v1−v0)·f, 5 + rand·10, rand·511/512, wit, 0.7)` (additief).
- mondingsrook één keer bij de overgang 1 → 2 als `launcher`: beeld 24, 80 groot, 0.15 s, `alpha 0.7·cos(u·π/2)` op `m0 + normalize(m1−m0)·10` van de lanceerder.

**Bommenwerper** in `launcher_fire` en de berichten:
```c
case 1001: l->kind = a1; l->life = a1 == 0 ? 2.0f : 15.0f; ...                   /* template 0 lives 2.0 s: that IS the fuse */
case 1002: n = 0 -> l->speed, 1 -> l->gravity, 4 -> l->damage (alleen kind 0 gebruikt ze; W2A stuurt alleen 2, 7, 8)
           n = 7 -> l->anim = v; n = 8 -> l->anim_dur = v * 0.01f;
static void launcher_fire(Launcher *l) {
    Vec3 p0, d; if (!inst_vector(l->inst, 0, &p0, &d)) return;  /* before restarting the anim */
    (aim als nu) ; normalise d;
    if (l->kind == 0) { BombT t = BOMB_T0; t.life = l->life; /* + speed/gravity/damage uit 1002 */
        Bomb *b = bomb_start(&t, p0, d, 0, -1, 0); if (b) { b->launcher = l; audio_fx(14, l->inst, &l->inst->position.x); } }
    else { ... bestaande Shot-code ... }
    if (l->anim >= 0 && l->anim < nanims(l->inst)) { l->inst->anim = l->anim; l->inst->anim_time = 0;
        l->inst->anim_speed = anim_len(l->inst, l->anim) / l->anim_dur; }                /* 0x4526fb: the whole anim in anim_dur seconds */
}
```
**Bomkanon** (type 21) in de `Rocket`-automaat: zelfde toestanden, maar in 4 → 5 geen Fly/uitlaat/geluid 10/11 maar
```c
Vec3 m0, md; inst_vector(in, 0, &m0, &md); normalise md;
BombT t = BOMB_T0; t.gravity = 0; t.speed = r->vmax; t.damp_a = 1.0f; t.life = r->fly_time;
r->bomb = bomb_start(&t, m0, md, 0, -1, 1); if (r->bomb) r->bomb->ridden = 1; audio_fx(14, in, &in->position.x);
```
toestand 5: alleen `t += dt` (naar 6 bij `t ≥ fly_time − 1`); 6: `t += dt`, geen rood, geen explosie (naar 7 bij 1.0 s); 7: `q0 = quat(rot)`, `q1 = start_q`,
naar 8; 8: slerp in 2 s terug, dan 0 (zonder Reset). Rocket-Reset: `if (r->bomb) { bomb_reset(r->bomb); r->bomb = NULL; }`.
Zitpositie voor de speler in toestand 5..7 = `r->bomb->inst->position` (anders marker 0), rotatie = die van het kanon. De Kill(6) komt uit `bomb_explode`.

**Controle**: W2A, lanceerder 13 op (11961, 245, −24949) vuurt vanaf 1.0 s elke 2.5 s een zwarte, rood flikkerende bom met lont hoog de lucht in; die stuitert
(tik bij elke landing, SoundFx 12) en ontploft 2.03 s na het schot (SoundFx 6, gele flits + grondring). Woody binnen 400 → Kill(6); tegen de bom aan lopen doet
niets. In de volumes bij (2318, 1346, −2989) vuurt 68 elke seconde.

## 10. Correcties op eerdere documenten

- **BONUS.md §7 / OBJECTS.md §3**: "alle type-17-objecten met toestand 1/2 krijgen `vtable[+0xa0]`" is fout: de eerste lus van `0x44d650` loopt de **Npc-tabel**
  `0x4c4e00` af en de "1/2" is de **categorie** (Perso/vijand). Klasse 17 is geen Npc en heeft geen slot 40; de bom raakt klasse 17 niet direct (hoe type 17 dan
  wel breekt: BOMB_CARRY.md). "Update = `0x44e810` (fade)" → de bomlogica is `0x44d850` uit `0x44d820` (frame-stap 18). `+0x10c/+0x110` = lont / waarschuwingstijd.
- **ROCKET.md §7**: "`0x44d4d0`: … `+0x128 = 1`" — `+0x128` is de **explosiesoort** (kanon: 1 = grote explosie). "De bom ontploft … straal 400: type-17-objecten" → zie hierboven.
- **EVENTS.md §4.2**: de rijen `0x44d70a` en `0x44d380` gaan over **bommen** (`+0x12c` = scriptvar van de bom, pool `0x5e4880`), niet over vijanden; `0x44d380`
  doodt niets, het ruimt bommen op zonder explosie. **EVENTS.md §3.2**: de grondtest is `punt.y − (grond + tol) < 1.0`, niet `< 0`.
- **PROJECTILES.md §2.4**: `grounded` wordt elk frame opnieuw bepaald (niet eenmalig), de probe test "< 1 eenheid boven een ondergrond" (terrein telt ook, niet
  alleen press-nodes) en SoundFx 12 klinkt bij élk nieuw contact. §2.5: "schade niet toegepast want carried" geldt alleen als de eigenaar een Perso/vijand is;
  bij een lanceerder of eigenaar 0 wordt `T.damage` (1000) wél toegepast.
- **SOUND.md §5**: id 6 "object/vijand vernietigd (`0x44d730`)" = de bomexplosie.

## 11. Onzeker / niet nagelopen

1. Visueel resultaat van de render-kleur (× 0 = helemaal zwart?) en van spritegroottes < 0 in `0x4765f0`.
2. `0x476cd0` (deeltjes) en `0x476140` (scherven) van explosiesoort 0; `0x498790` (licht?).
3. `vtbl[23]` `0x44d990` (1/8): geen aanroeper gevonden.
4. Welke van de twee typecode-0-markers van model 26 `GetVector(…, 0, …, 0)` kiest (ze liggen op dezelfde plek, dus zonder gevolg).
5. De werkelijke schiethoek van de model-5-lanceerders (node 4 is geanimeerd) en daarmee de worpafstand (§6 is een schatting).
6. Volgorde kanon-denkstap t.o.v. `0x44d820` in hetzelfde frame (§7); vijandsubtype van type 8 (§5.3); de betekenis van de VOL_FLAG-bits komt uit de port-VM.
7. Of de geparkeerde W2A-bommen ooit in beeld zijn (sector 48); of oppakken in de onzichtbare 0.5 s na een explosie kan (§3.3).
8. Niets is met `tools/wtrace.py` tegen het draaiende origineel gecontroleerd; goede meetpunten: SoundFx 14 → 6 (2.0 s + 2 frames), SoundFx 12 per stuit.
