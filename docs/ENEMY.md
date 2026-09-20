# Vijandklassen (types 4..13) – Woody.exe

Status: werkdocument, incrementeel bijgewerkt. Alle adressen zijn VA's in `game/Woody.exe`
(imagebase 0x400000). Floats zijn met een PE-parser uit de exe gelezen. `0x462c60` = lege log-functie,
`0x4995b2` = `operator new`, `0x4995a7` = `operator delete`, `0x499ed5` = `_purecall`.

## 1. Klassenhiërarchie

```
Instance   ctor 0x42e1a0  vtable 0x4aa31c  (28 slots)   handler 0x42d5e0
  └ Npc    ctor 0x40be10  vtable 0x4a9530  (45 slots)   handler 0x40bf20 → 0x44e8f0
      │      (tweede afgeleide: Perso, ctor 0x44a2d0, vtable 0x4aabc0)
      └ Enemy (abstract) ctor 0x419cd0  vtable 0x4a9fbc (58 slots; [45] en [51] = _purecall) handler 0x41a740
          ├ type 4,5,6   ctor 0x4189f0(subtype 1,2,3)  vtable 0x4a9ec0  size 0x1fc
          ├ type 7,8,9   ctor 0x416ca0                 vtable 0x4a9dc0  size 0x20c
          ├ type 10      ctor 0x415190                 vtable 0x4a9cb8  size 0x1ec
          ├ type 11      ctor 0x4120e0                 vtable 0x4a9ab0  size 0x200
          ├ type 12      ctor 0x410e90                 vtable 0x4a99b0  size 0x204
          ├ type 13      ctor 0x413780                 vtable 0x4a9bb0  size 0x1f8
          └ (nog drie: ctors 0x40c730, 0x40d850, 0x40eb50 – bazen, buiten dit document)
```

* `Npc::Npc` (`0x40be10`): `+0x104 = 0` (typewoord), `+0x108 = index` in de globale npc-tabel
  `0x4c4e00[0x4c5318]` (max 0x100, "Too many npc. Max is %d"). Dtor `0x40bec0` haalt hem eruit (swap met laatste).
* Typewoord `+0x104` (via `vtbl[4]` = `0x403fe0` = `return this+0x104`): bits 0..4 = categorie
  (`0x40c360(cat)`, lezer `0x40c340`), bits 5..9 = **subtype** (`0x40c380(n)`, lezer `0x40c350`),
  bit 0x400 = aanvalbaar doel.
* `Enemy::Enemy` (`0x419cd0`): nul in `+0x110, +0x118, +0x120, +0x124, +0x160, +0x164, +0x168, +0x16c`;
  `+0x170 = 1.0f`; `+0x174 &= ~2`.
* Type 4/5/6-ctor `0x4189f0(subtype)`: `Enemy::Enemy`, vtable, `0x40c360(2)` (categorie 2 = vijand),
  `0x40c380(subtype)`, `+0x1c4 = 0`. Aanroepen in de klassefabriek `0x403502`: type 4 → subtype **1**
  (`0x403649`), type 5 → **2** (`0x403682`), type 6 → **3** (`0x4036bb`).

### 1.1 Vtable-slots (Enemy-basis `0x4a9fbc` / type 4-6 `0x4a9ec0` / type 7-9 `0x4a9dc0`)

Slots 0..27 = Instance, 28..44 = Npc, 45..57 = Enemy. Alleen de relevante:

| slot | off | basis | type 4-6 | type 7-9 | betekenis |
|---|---|---|---|---|---|
| 1 | +0x04 | `0x419e30` | `0x418ae0` | `0x416d90` | **PostLoad**: hulpobjecten aanmaken (§2.2) |
| 3 | +0x0c | `0x41a320` | = | = | **per-frame Think** (§3.1) |
| 4 | +0x10 | `0x403fe0` | = | = | `&typewoord` (`this+0x104`) |
| 17 | +0x44 | `0x41a010` | `0x418c20` | `0x416ed0` | **Reset** (terug naar startpositie, hp vol, begin-toestand) |
| 22 | +0x58 | `0x41a740` | `0x414530`→`0x41a740` | idem | berichthandler (§6) |
| 29 | +0x74 | `0x41aff0` | = | = | "verwijderd": `[0x4c532c]++` (teller gedode vijanden) en `jmp 0x407850`; aangeroepen door `0x40bf60` als `+0x10c` bit 0 staat |
| 31 | +0x7c | `0x40c1e0` | = | = (type 12: `0x411840`) | `Touch(cat, subtype)`: eerste actor uit `0x4c5258[0x4c5324]` (≠ this) met die categorie/subtype (0 = elk) en 3D-afstand `< straal_this + straal_ander` |
| 32 | +0x80 | `0x41ad20` | = | = | straal = `P+0x04` |
| 33 | +0x84 | `0x41ad30` | = | = | hoogte = `P+0x28` |
| 34 | +0x88 | `0x41ace0` | = | = | positie-pointer |
| 37 | +0x94 | `0x40c3b0` (`ret 4`, niets) | = | `0x417ee0` | waarschuwing "speler valt aan" |
| 38 | +0x98 | `0x40c3b0` | `0x4078a0` (niets) | `0x4078a0` | `Kill(soort)` – bij vijanden **leeg** |
| 39 | +0x9c | `0x41adc0` | `0x419480` | `0x417f60` | **TakeDamage(aanvaller, schade, &dir, &punt, soort) → bool dood** (§5) |
| 40 | +0xa0 | `0x41ae20` | = | = | `Blast(&pos, r)`: als 3D-afstand² < r² ⇒ `vtbl[39](0, hp, &normalize_xz(this−pos), 0, 0)` (doodt altijd) |
| 43 | +0xac | `0x41a4e0` | = | = | grond volgen (§3.3) |
| 44 | +0xb0 | `0x41a680` | = | = | rotatiematrix uit kijkhoek (§3.4) |
| 45 | +0xb4 | purecall | `0x4194f0` | `0x418000` | animatiekeuze per toestand (§4.3) |
| 46 | +0xb8 | `0x45bd30` | = | = | `return 3` (modus voor obstakelsensor `+0x124`) |
| 47 | +0xbc | `0x41a4d0` → `0x40c0b0` | `0x419460` | `0x417f20` | RegisterActor2 (type 4: alleen als toestand 0..11, dus niet als dood) |
| 48 | +0xc0 | `0x41af80` | = | = | `FindTarget(cat, subtype)` (§3.2) |
| 49 | +0xc4 | `0x462c60` (leeg) | = | = | |
| 51 | +0xcc | purecall | `0x4149e0` | `0x4149e0` | duur van de sterf-animatie (float) |
| 52 | +0xd0 | `0x41a3e0` | `0x418cf0` | `0x416fb0` | **Update / toestandsmachine** (§4) |
| 53 | +0xd4 | `0x40d840` | `0x419980` | `0x4184f0` | duur hit-animatie (arg: int) |
| 55 | +0xdc | `0x41b1b0` | = | = | na-beweging (cel bijwerken) |
| 56 | +0xe0 | `0x41b030` | = | = | verplaats met botsing (door gedragingen aangeroepen) |
| 57 | +0xe4 | `0x41b000` | = | = | **sterf-effect** (5 deeltjes): `0x477610(rand()&1 ? 0 : 1, this)` – géén bonus (zie §4.2) |
| 58 | +0xe8 | – | – | `0x418820` | (alleen type 7-9) projectiel afvuren `(doel, &mondingspunt)` |

Direct ná elke klasse-vtable staat de vtable van de bijbehorende animatiecontroller (5 slots,
`[0]`=record-getter, `[1]`=`0x40d7f0`, `[2]`=`0x436b70` Request, `[3]`=`0x436a50` Tick, `[4]`=`0x436a40` Reset;
type 4-6: `0x4a9fa8` met `[0] = 0x419cb0`).

## 2. Structvelden

### 2.1 Enemy (basis; `this`)

| off | type | default | betekenis | bron |
|---|---|---|---|---|
| 0x04 | u32 | | VM-id van de instantie (voor msgmask `0x443e50/0x443e90`) | `0x41a63e` |
| 0x0c | vec3 | | positie (voeten) | |
| 0x28 | mat3 | | rotatie (rijen); door `0x41a680` elke frame gezet | |
| 0x60 | vec3 | | botsmiddelpunt = pos + (0, hoogte·0.5, 0) | `0x41a60x` |
| 0x6c | float | 0 | fade/transparantie 0..1 (FadeInst; loopt op tijdens de tweede helft van het sterven, §4.2) | `0x41a47d` |
| 0x78 | ptr | | TRAJ-traject uit de .ins (0 = geen) | `0x418b2a` |
| 0x104 | u32 | | typewoord (cat 2, subtype, 0x400) | |
| 0x108 | int | | index in npc-tabel `0x4c4e00` | `0x40be49` |
| 0x10c | u8 | | bit 0 = "verwijder mij" (→ `vtbl[29]` in `0x40bf60`) | `0x419391` |
| 0x110 | Fall* (0xc) | | valobject `{t, g = P+0x00, v}` (`0x4402f0`) | `0x419ebb` |
| 0x114 | float | 0 | dt van dit frame (kopie van `Wereld+0x38`) | `0x41a34c` |
| 0x118 | Params* (0xc4) | | parameterblok **P** (§2.3) | `0x419e89` |
| 0x11c | Behav* | | **actief gedrag** (één van +0x160..0x16c) | |
| 0x120 | Heading* (0x2c) | | hoek-/snelheidsregelaar **H** (§3.4) | `0x419f38` |
| 0x124 | Sensor* (0xb8) | | obstakelsensor, 16 richtingen (`0x41cfc0`, tick `0x41d4a0`) | `0x419f6c` |
| 0x128 | vec3 | pos bij PostLoad | **startpositie** (Reset zet pos hierop) | `0x419f74` |
| 0x134 | vec3 | = startpos | **thuispunt** (middelpunt voor dwalen; type 4 zet het op de plek waar het pad verlaten werd) | `0x419f98` |
| 0x140 | vec3 | 0 | (`0x43ffa0`) | |
| 0x14c | float | 0 | | `0x41a0c3` |
| 0x150 | **float** | `P+0x34` | **levenspunten** | `0x41a0bd` |
| 0x154 | float | 0 | | |
| 0x158 | float | 0 | hit-timer (> 0 = onkwetsbaar, toestand "geraakt") | `0x4194c5` |
| 0x15c | float | 0 | sterf-timer (loopt op in toestand dood) | `0x419378` |
| 0x160 | Behav* | | gedrag **Dwalen** (`0x41bf30`, 0x6c bytes) | |
| 0x164 | Behav* | | gedrag **Achtervolgen** (`0x41bbf0`, 0x3c) | |
| 0x168 | Behav* | | gedrag **Stilstaan** (`0x41b710`, 0x3c) | |
| 0x16c | Behav* | | gedrag **Pad volgen** (`0x41c6a0`, 0x60; alleen als `+0x78 ≠ 0`) | |
| 0x170 | float | 1.0 | | `0x419d16` |
| 0x174 | u8 | | vlaggen: **1** = op de grond, **2** = "bewaak thuispunt" (FindTarget meet vanaf `+0x134` i.p.v. eigen pos), **4** = grond volgen aan (Reset), **8** = gedrag-tick zonder sensor (§4.2), **0x10** = geen zwaartekracht, **0x20** = gezet in PostLoad | |
| 0x178 | Probe (0x24) | | grond-probe voor `0x436dc0` (ctor `0x436cf0`, reset `0x436d10`) | |
| 0x19c | Probe (0x24) | | tweede probe (gebruikt door `0x41b030`) | |

### 2.2 Type 4/5/6 (size 0x1fc)

| off | type | betekenis |
|---|---|---|
| 0x1c0 | int | **toestand** 0..12 (§4.1) |
| 0x1c4 | AnimCtrl* (0x54, ctor `0x419c90`) | animatiecontroller (zelfde basisklasse als die van Perso, `0x436b70/0x436a50/0x436a40`) |
| 0x1c8 | float | afkoeltimer ná een aanval; zolang ≥ 0 wordt de speler **niet** opnieuw opgemerkt (start `P+0x38`) |
| 0x1cc | float | timer "aanval mis" (toestand 3) = AnimLen(10) |
| 0x1d0 | float | timer toestand 11 = AnimLen(11) |
| 0x1d4 | float | resterende aanvalsduur (toestand 1 berekent, toestand 4 telt af) |
| 0x1d8 | float | timer toestand 6 = AnimLen(9) |
| 0x1dc, 0x1e0 | | 0 bij Reset |
| 0x1e4 | float | `AnimLen(4, 1)` (Reset `0x418cd9`) |

PostLoad `0x418ae0`: `Enemy::PostLoad`; `+0x1c4 = new AnimCtrl(this)`; als `+0x78`: `+0x16c = new Pad(this, traj)`;
`+0x160 = new Dwalen(this, 1, &this+0x134)`; `+0x164 = new Achtervolgen(this, 0)`; `+0x168 = new Stilstaan(this)`;
`[0x4c5330]++` (totaal aantal vijanden in het level).

Reset `0x418c20`: `Enemy::Reset`; met TRAJ ⇒ toestand **0**, gedrag = Pad (`vtbl[6]()` init + `0x41c760`), `P+0x1c = 10.0`;
zonder TRAJ ⇒ toestand **8**, gedrag = Dwalen (`vtbl[6]()` + `0x41c160`). Alle timers 0.

### 2.3 Parameterblok P (`+0x118`, ctor `0x41d510(enemy)`, defaults daarna per subtype via tabel `0x41dc38`)

| off | default | sub 1 (type 4) | sub 2 (type 5) | sub 3 (type 6) | sub 4 (type 7) | betekenis (bewezen gebruik) |
|---|---|---|---|---|---|---|
| 0x00 | 200 | | | | | valversnelling g (Fall-object) |
| 0x04 | 50 | **30** | 30 | 30 | 30 | **straal** (vtbl[32]); ook afstand van het trefpunt vóór de vijand |
| 0x08 | 200 | 200 | 200 | 200 | 200 | **loopsnelheid** (dwalen/patrouille) |
| 0x0c | 600 | 600 | 600 | 600 | 600 | (ren-snelheid? nog niet gezien in type 4) |
| 0x10 | π/2 | | | | π/2 | **draaisnelheid** rad/s (H+8) |
| 0x14 | 2π | | | | 4π | snelle draaisnelheid |
| 0x18 | π | | | | π | = 2·P+0x10 |
| 0x1c | 800 | (Reset met pad: 10) | | | | |
| 0x20 | 800 | **1500** | 1500 | 1500 | 1500 | **zichtafstand** (3D) FindTarget |
| 0x24 | 600 | | | | 800 | max. hoogteverschil |dy| FindTarget |
| 0x28 | 140 | 140 | 140 | 140 | 140 | **hoogte** (vtbl[33]) |
| 0x34 | 1.0 | **1** | **1** | **2** | 1 | **levenspunten** |
| 0x38 | 3.0 | **1.5** | **1.0** | **0.5** | 1.5 | **afkoeltijd** (s) na een aanval |
| 0x3c | 1.0 | 1 | 1 | 1 | 1 | **schade aan de speler** |
| 0x54 | 400 | 400 | 400 | 400 | 150 | |
| 0x5c | 1000 | **800** | 800 | 800 | 800 | **stormloopsnelheid**; aanvalsbereik = `P+0x5c · T` |
| 0xc0 | 3000 | | | | | **activeringsafstand tot de camera** |

Overige defaults: `+0x2c=+0x30=10, +0x40=1, +0x44=600, +0x48=500, +0x4c=3, +0x50=800, +0x58=300, +0x60=1000, +0x64=1,
+0x68=0.5, +0x6c=100, +0x70=0, +0x84=+0x94=7, +0x88=100, +0x8c=300, +0x90=0.8, +0x9c..0xa4=1, +0xa8=+0xac=10, +0xb0=1,
+0xb4=3, +0xb8=500, +0xbc=3 (int)`. Subtype 5 (type 8): hp 2, afkoel 1.0, hoogte 130; subtype 6 (type 9): hp 3, schade 3;
subtype 7: straal 50, zicht 2500, hoogte 180, hp 2, schade 2; subtype 8: straal 60, hp 5, schade 3; subtype 9: hp 3, schade 2;
subtype 10: straal 80, hp 2; subtype 11: hp 5; subtype 12: hp 6, schade 4; subtype 13: hp 10, schade 4.

## 3. Per-frame basis

### 3.1 Think `vtbl[3]` = `0x41a320`

```c
void Enemy_Think(Enemy *e) {
    if (!g_active /*[0x4b178c]*/) return;
    Npc_Tick(e);                          /* 0x44e810 */
    if (g_cinematic /*[0x5d7b8d]*/) return;
    e->dt = World->dt;                    /* +0x114 = [0x509adc]+0x38 */
    e->vtbl[47](e);                       /* RegisterActor2 (0x4c4d80, max 32) */
    vec3 d = e->pos - cam->pos;           /* cam = 0x41fa30([0x4c737c]) + 0x90 */
    if (dot(d,d) < P->activeDist * P->activeDist /*3000²*/ || e->hp <= 0)
        e->vtbl[52](e);                   /* Update */
}
```
Vijanden verder dan **3000** van de camera staan dus stil (behalve als ze al dood zijn: de sterfanimatie loopt af).

### 3.2 FindTarget `vtbl[48](cat, subtype)` = `0x41af80` → `0x40c0d0`
Middelpunt `c` = eigen positie (`vtbl[34]`), of `+0x134` als vlag 2. Loopt over lijst 1 (`0x4c52d8[0x4c531c]`, paren van
8 bytes – daarin staat Perso): categorie moet `cat` zijn (en subtype als ≠ 0); kiest de **dichtste** met 3D-afstand
`< P+0x20` (1500) én `|dy| < P+0x24` (600). Geen zichtlijn- of hoektest. Alle aanroepen in type 4 zijn `(1, 0)` = de speler.

### 3.3 Grond volgen `vtbl[43]` = `0x41a4e0` (alleen als vlag 4)
```c
h2 = P->height * 0.5f;  p = pos + (0,h2,0);
cell = World_FindCell([0x50944c], &p);                    /* 0x428ce0 */
hit  = Probe_Test(&e->probe /*+0x178*/, cell, &p, h2, e->id);   /* 0x436dc0 → press/in/unpress-events */
flags.onGround = (hit != 0);   groundY = [0x53a568];
if (!(flags & 0x10)) {                                    /* zwaartekracht */
    if (onGround) { fall.t = 0; fall.v = 0; }             /* 0x440330 */
    else { fall.t += dt; fall.v += dt * fall.g /*200*/ - fall.v * 0.2f /*0x4a9760*/; }
    if (fall.v > 0) { pos.y -= fall.v; moved = 1; }       /* NB: v is eenheden per FRAME */
}
if (pos.y < groundY) { pos.y = groundY; flags |= 1; moved = 1; }
if (moved) { e->colCenter = pos + (0,h2,0); Instance_SetCell(e, &colCenter); /*0x4077f0*/ }
msgmask(e->id, 0x200) = onGround ? set (0x443e50) : clear (0x443e90);
```
De eindsnelheid van de val is `dt·g/0.2` per frame (bij 60 fps ≈ 16.7 eenheden/frame ≈ 1000 eenh/s).

### 3.4 Hoekregelaar H (`+0x120`, ctor `0x41b7f0(hoek0, draaisnelheid, 0, 1000.0)`)
`+0x00` bewegingshoek, `+0x04` doelhoek, `+0x08` draaisnelheid (rad/s, = `P+0x10` = **π/2**), `+0x0c` getekende hoeksnelheid
(0 = klaar), `+0x10/+0x14/+0x18` idem voor de **kijkhoek** (het model), `+0x1c` beginhoek, `+0x20` huidige snelheid,
`+0x24` doelsnelheid, `+0x28` versnelling (**1000 eenh/s²**).
* `0x41b980(hoek, snap)`: snap ⇒ beide hoeken = hoek; anders doel = hoek en snelheid = `0x440180(cur, doel)` (±1 langs de
  kortste weg) × draaisnelheid.
* `0x41b9d0(v, direct)`: doelsnelheid = v (en huidige ook als `direct`).
* Tick `0x41ba90(dt)`: per kanaal `stap = dt·ω`; als `|stap| ≥` hoekverschil (`0x4401c0`, kortste boog) ⇒ hoek = doel; anders
  `hoek += stap`, wrap naar [0, 2π]. Snelheid loopt lineair naar doelsnelheid met 1000/s².
* Hoek van a naar b `0x440210(a, b)`: `2π − (atan2(b.x−a.x, b.z−a.z) + π + π/2)`, genormaliseerd naar [0, 2π).
  Richtingsvector bij hoek: `(cos h, 0, sin h)` (`0x41b860` bewegingshoek, `0x41b8b0` kijkhoek).
* `vtbl[44]` = `0x41a680`: `f = −(cos k, 0, sin k)` (k = kijkhoek `H+0x10`); `up = (0,1,0)`; matrixrijen:
  `+0x28..0x30 = f × up` (`0x41af10`), `+0x34..0x3c = f`, `+0x40..0x48 = up`. De vijand kijkt dus langs **−rij(+0x34)**;
  PostLoad leidt de beginhoek daar ook uit af: `hoek0 = 0x440210(pos, pos − rij(+0x34))` (`0x419ec1`).

## 4. Toestandsmachine type 4/5/6 (`vtbl[52]` = `0x418cf0`, jump-table `0x419420`)

### 4.1 Toestanden

| # | code | naam | gedrag (`+0x11c`) | wat gebeurt er / overgang |
|---|---|---|---|---|
| 0 | `0x418e69` | **PATROUILLE** (pad) | Pad | als `cool < 0` en `FindTarget(1,0)` ⇒ thuispunt `+0x134` = eigen positie, `Dwalen.0x41c140(1, &thuis)`, → **2** |
| 1 | `0x418f23` | **ACHTERVOLGEN** | Achtervolgen | geen doel ⇒ **7**. `T = AnimLen(8) + 0.5·AnimLen(10)` → `+0x1d4`; als xz-afstand ≤ `P+0x5c·T` (800·T) **en** `dot(bewegingsrichting, normalize(doel−pos)) > 0.95` (`0x4a9c9c`) ⇒ snelheid := 800 (direct), → **4** |
| 2 | `0x418ee0` | OPGEMERKT | | doel ⇒ gedrag = Achtervolgen (`vtbl[6]()`, `0x41bc80(doel)`), → **1** (en meteen de code van 1); geen doel ⇒ **7** |
| 3 | `0x419245` | AANVAL MIS | Stilstaan | `+0x1cc -= dt`; ≤ 0 ⇒ `cool = P+0x38`, → **7** |
| 4 | `0x4190af` | **STORMLOOP** | Achtervolgen | geen doel of `+0x1d4 ≤ 0` ⇒ **5**; `+0x1d4 -= dt`; als `Touch(1,0)` (afstand < 30 + straal speler): `hit = speler->vtbl[39](this, P+0x3c /*1.0*/, &dir, &punt, 0)` met `dir = normalize_xz(speler−pos)`, `punt = pos + dir·P+4 + (0, h/2, 0)`; `hit` (speler dood) ⇒ **10**, anders `+0x1cc = AnimLen(10)`, gedrag = Stilstaan, → **3** |
| 5 | `0x41927c` | UITLOPEN start | Stilstaan | `+0x1d8 = AnimLen(9)`, → **6** |
| 6 | `0x4192a7` | UITLOPEN | Stilstaan | `+0x1d8 < 0` ⇒ `cool = P+0x38`, → **7**; anders `-= dt` |
| 7 | `0x418d77` | NAAR DWALEN | → Dwalen | snelheid := `P+0x08` (200, direct); gedrag = Dwalen (`vtbl[6]()`, `0x41c160`), → **8** |
| 8 | `0x418dc6` | **DWALEN** | Dwalen | als `cool < 0` en doel ⇒ **2**. Met TRAJ: xz-afstand tot thuispunt `< 10` (`0x4a9750`) ⇒ gedrag = Pad (`0x41cfb0`), → **0** |
| 9 | `0x4193b8` | **GERAAKT** | (ongewijzigd) | `hp ≤ 0` ⇒ `vtbl[57]()` (bonus), → **12**; anders als hit-timer `+0x158 ≤ 0` ⇒ **7** |
| 10 | `0x4192f2` | SPELER VERSLAGEN start | Stilstaan | `+0x1d0 = AnimLen(11)`; snelheid := 200; → **11** |
| 11 | `0x419334` | SPELER VERSLAGEN | Stilstaan | `+0x1d0 -= dt`; ≤ 0 ⇒ **7** |
| 12 | `0x41935c` | **DOOD** | Stilstaan | `+0x15c += dt`; als `vtbl[51]()` (duur sterfanimatie) ≤ `+0x15c` ⇒ `+0x10c \|= 1` (verwijderen); typewoord `&= ~0x400` (niet meer aanvalbaar) |

Vóór de switch: `Enemy::Update` (§4.2); `cool (+0x1c8)`: als ≥ 0 ⇒ `-= dt`; `+0x158` (hit-timer): als > 0 ⇒ `-= dt`.
Let op: er is **geen kijkhoek-/zichtkegel**; "zien" = binnen 1500 (3D) en |dy| < 600. De 0.95-test (≈ 18°) bepaalt alleen
wanneer de stormloop start.

### 4.2 `Enemy::Update` = `0x41a3e0`
```c
if (!(flags & 8)) {
    k = behav->vtbl[7]();                                 /* soort gedrag */
    if (k != 3 && k != 5) Sensor_Tick(e->sensor, e->vtbl[46]() /*3*/);   /* 0x41d4a0 */
}
behav->vtbl[4]();                                          /* gedrag-tick: stuurt H en verplaatst */
if (!(flags & 8)) e->vtbl[49]();                           /* leeg */
half = e->vtbl[51]() * 0.5f;                               /* sterfanimatie: tweede helft vertraagt */
if (e->deathT /*+0x15c*/ > half) {
    e->fade /*+0x6c*/ = (e->deathT - half) / (e->vtbl[51]() - half);  if (> 1.0f) = 1.0f;   /* uitfaden */
}
e->vtbl[43]();  /* grond */   e->vtbl[44]();  /* rotatie */
e->vtbl[45]();  /* animatie */ e->vtbl[55]();  /* cel */
```

**Correcties op §1.1/§2.1:** `inst+0x6c` is geen animatiesnelheid maar de **fade/transparantie** van de FadeInst-laag
(`0x44e810`: loopt met `+0x100` eenh/s naar doel `+0xfc`; tussen 0 en 0.9 (`0x4a94b8`) wordt renderflag 0x40 gezet) – de vijand
**vervaagt dus lineair van 0 naar 1 tijdens de tweede helft van de sterfduur**. En `vtbl[57]` = `0x41b000` laat **geen bonus**
vallen: `0x477610(rand()&1 ? 0 : 1, this)` maakt 5 deeltjes (levensduur 2.5, callback `0x477350`, sprite `0x1000b` of `0x1000a`)
die aan de vijand hangen = sterf-effect (sterretjes/veren). In de klassen 4..13 is geen bonus-drop gevonden.

### 4.3 Animaties type 4/5/6 (`vtbl[45]` = `0x4194f0`, tabel `0x41963c`; daarna altijd `AnimCtrl->Tick(dt)`)

Logische animaties = records van 0x1c bytes in `0x4b29b8` (`vtbl[0]` van de AnimCtrl = `0x419cb0`), formaat als bij Perso
`{int sub[4]; int prio; float speed; u8 restart}`; alle prio 1000 behalve 13 (2000):

| n | sub[] | speed | gebruikt voor |
|---|---|---|---|
| 1 / 2 | 16 / 17 | 3 | draaien naar de speler (teken van `H+0x0c`: > 0 ⇒ 1, anders 2) – toestand 1/2 zolang Achtervolgen nog niet loopt |
| 3 | 1,2,2,2 | 3 | **rennen** (achtervolgen, `Achtervolgen+0x30 == 1`) |
| 4 / 5 | 3,4,5,0 | 3 | lopen (dwaal-actie 6 / 7) |
| 6 / 7 | 4,4,5,0 / 4 | 3 | dwaal-actie 9 (naar huis lopen) / 10 (naar huis draaien) |
| 8 | 13 | **10** | **stormloop** (toestand 4) |
| 9 | 15 | 2 | uitlopen/remmen (toestand 5/6) |
| 10 | 14 | 3 | aanval mis / hap (toestand 3) |
| 11 | 18 | 1.5 | speler verslagen (toestand 10/11) |
| 12 | 11 | 4 | **geraakt** (toestand 9) |
| 13 | 12 | 3 | **dood** (toestand 12); `vtbl[51]()` = `AnimLen(13) + 1.0` s |
| 14..18 | 6..10 | 3 | idle-variaties (dwaal-actie 0..4) |
| 19/20/21 | 3,4,4,4 / 4 / 5 | 3 | pad-lopen: `0x419770` kiest 0x15/0x13/0x14 naar gelang de padsnelheid (mengt met `0x436bd0`) |
| 22 / 23 | 16 / 17 | 3 | op de plaats draaien (dwaal-actie 5; pad-substaat 1 en 7) |

`AnimLen(n,k)` = `0x436b90` (zie PERSO_JUMP §4). Duur "geraakt" `vtbl[53]` in toestand 9 = constante **0.25 s** (`0x4a9ca0`).
`vtbl[53](actie)` levert verder per toestand de lengte van de lopende animatie (tabel `0x419c18`; in toestand 7/8 per
dwaal-actie, tabel `0x419c4c`: actie 6 ⇒ som van AnimLen(4,0..2), actie 7 ⇒ som AnimLen(5,0..2), 0..4 ⇒ AnimLen(14..18)).

## 5. Gedragingen (Behav; basis-ctor `0x41b250`, vtable `0x4aa0b0`)

Velden: `+4` enemy, `+8` H (`enemy+0x120`), `+0x10` vec3 terugslagrichting, `+0x1c` terugslagtimer, `+0x20` vec3
platformverplaatsing. Vtable: `[0]` stap-lengte dit frame (float), `[1]` richting (vec3 uit), `[2]` hook na de sweep,
`[3]` OnBlocked, `[4]` **Tick**, `[6]` Init, `[7]` soort (Dwalen 0, Achtervolgen 1, Pad 3, Stilstaan 5).

### 5.1 Gemeenschappelijke verplaatsing `0x41b2c0` (= Tick van Stilstaan; door alle Ticks aangeroepen)
```c
from = pos;  h2 = P->height*0.5;  stepUp = min(P+0x30 /*10*/, h2);  maxDrop = P+0x2c /*10*/;
if (b->knockT <= 0)  to = from + b->vtbl[1]() * b->vtbl[0]();            /* normale stap */
else { b->knockT = max(0, b->knockT - dt);
       to = from + b->knockDir * (dt * P+0x44 /*600*/ * b->knockT); }     /* terugslag: v = 600·t_rest */
Probe_GetPlatformDelta(&e->probe, &b->platDelta);  to += platDelta;       /* 0x436d20: meebewegen met platform */
[0x4b3118] = P->radius;  Probe2_PushOut(&e->probe2, &from+h2, &push, -1); to.xz += push.xz;   /* 0x437040 */
Sweep(&res, &from, &to, stepUp, 30.0f);                                   /* 0x437580, zet [0x4b310c], [0x53a568] = grondhoogte */
b->vtbl[2](&res, &from, ...);
if (subtype >= 9) res.y = from.y;                                         /* vliegende types */
if ([0x4b310c] >= 0.8f /*0x4a987c*/ && res.y - groundY < maxDrop) {      /* vrije weg en geen afgrond > 10 */
    if (res.y - groundY < 1.0f && [0x53a554] == 2) Probe_Attach(...);     /* 0x436d80 */
    pos = res;  colCenter = res + (0,h2,0);  Instance_SetCell(e, &colCenter);
} else {                                                                  /* geblokkeerd: alleen platformdelta */
    pos += platDelta; ...; b->vtbl[3]();                                  /* OnBlocked */
}
```
Vijanden lopen dus **niet van randen** hoger dan `P+0x2c` = 10 (subtypes 9/10: 10000) en stappen max. 10 omhoog.

### 5.2 Stilstaan (`0x41b710`, vtable `0x4aa0d0`): stap = `dt · (+0x38 = 0)`, richting `+0x2c` = 0 ⇒ alleen terugslag/platform.

### 5.3 Achtervolgen (`0x41bbf0(enemy, vlucht)`, vtable `0x4aa0f8`; `+0x2c` doel, `+0x30` loopt, `+0x34` draaitimer, `+0x38` 1 = vluchten)
* Start `0x41bc80(doel)`: doelsnelheid H := **`P+0x0c` = 600** (direct), draaisnelheid := **`P+0x14` = 2π rad/s**;
  `a = Stuur()`; `a ≠ 0` ⇒ `+0x34 = a / draaisnelheid`, `+0x30 = 0` (eerst op de plaats draaien); anders `+0x30 = 1`.
* `Stuur` `0x41bd00`: `H.Tick(dt)`; doelhoek = hoek(eigen pos → doel) (bij vluchten omgekeerd) zonder snap; als de sensor
  (`enemy+0x124`, `0x41d2a0(hoek)`) die richting geblokkeerd meldt ⇒ doelhoek = `0x41d310(hoek)` (dichtstbijzijnde vrije van
  16 richtingen). Geeft het resterende hoekverschil.
* Tick `0x41bee0`: `Stuur()`; timer ≤ 0 en nog niet lopend ⇒ `+0x30 = 1`; anders timer −= dt; dan `0x41b2c0`.
  Stap `[0]` = `+0x30 ? dt·H.snelheid : 0`; richting `[1]` = `(cos, 0, sin)` van de bewegingshoek.
* Hook `[2]` (`0x41bdf0`): als de sweep-verplaatsing < 0.01 (`0x4a94f8`) ⇒ resultaat ± willekeurig (−16..15) in x en z
  (loswrikken). OnBlocked `[3]` (`0x41be90`): doelhoek = vrije sensorrichting, timer 0.

### 5.4 Dwalen (`0x41bf30(enemy, leash, &thuis)`, vtable `0x4aa118`)
`+0x30..0x4c` = 8 acties `(soort<<16)|gewicht`: acties 0..5 gewicht **10**, 6 en 7 gewicht **25**, soort 1
(= duur uit `enemy->vtbl[53](actie)`; soort 0 zou `2.0 + (rand()&0x1fff)/4096` s zijn). `+0x50` huidige actie, `+0x54`
resterende duur, `+0x58/+0x5c` ontwijk-afkoeling/-object, `+0x60/+0x69` thuiskeer-toestand, `+0x64` &thuispunt, `+0x68` leash aan.
* Kiezen `0x41c180(−1)`: was de vorige actie > 5 (lopen) ⇒ gewogen keuze uit de idle-acties **0..4**, anders uit **6..7**
  (lopen) – dus afwisselend *lopen* en *idle*. Bij actie 5 en 7: nieuwe richting = `Sensor.0x41d2c0()` (willekeurige vrije
  richting) of, als die < 0 geeft, een willekeurige hoek (`0x41ba10`: `(rand() % 6283)·0.001`).
* Stap `[0]` (`0x41c3a0`, bytetabel `0x41c40c`): acties 6, 7, 9, 10 ⇒ doelsnelheid H := **`P+0x08` = 200** (met versnelling
  1000/s²), stap = `dt·H.snelheid`; overige acties 0.
* Tick `0x41c640`: thuiskeer `0x41c500` (alleen met leash: xz-afstand (subtype ≥ 9: 3D) tot thuis `> P+0x1c` ⇒ doelhoek naar
  thuis, actie 10 = draaien (duur = hoek/draaisnelheid), daarna actie 9 = teruglopen); timer `0x41c370` (≤ 0 ⇒ nieuwe actie);
  draaisnelheid := `P+0x10` (π/2); `H.Tick(dt)` behalve in acties 0..4; dan `0x41c470`: verplaatsing + **ontwijken**:
  `Touch(0,0)` (raakt een willekeurige actor) en (ander object dan vorige keer of afkoeling verstreken) ⇒ doelhoek = weg van
  die actor, actie 7, afkoeling = duur.
* OnBlocked `0x41c420`: doelhoek = `Sensor.0x41d390()` (als ≥ 0), actie 5 (draaien).
* Type 4 met TRAJ zet in Reset `P+0x1c = 10` ⇒ na een achtervolging loopt hij (leash aan via `0x41c140(1,&thuis)`) terug naar
  het punt waar hij het pad verliet en hervat het pad zodra hij binnen 10 eenheden is (toestand 8 → 0).

### 5.5 Pad volgen (`0x41c6a0(enemy, traj)`, vtable `0x4aa13c`; patrouille langs TRAJ)
TRAJ (`inst+0x78`): `traj[0]` low16 = aantal punten, bit `0x10000` = **gesloten lus**; `traj+0x10` → records van 16 bytes met de
positie op `+4`. Velden: `+0x2c` substaat (**0** = segment lopen, **1** = draaien op een punt, **2..6** = idle-pauze,
**7** = omkeren na obstakel), `+0x34/+0x38` van-/naar-index, `+0x3c` richting ±1, `+0x40` vec3 padpositie, `+0x4c` laatste
wereldtijd, `+0x50` t in de substaat, `+0x58` duur van de substaat.
* Init `0x41c6e0`: snelheid H := `P+0x08` (direct); meer dan 1 punt ⇒ van = 0, naar = 1, hoek snap naar punt0→punt1.
* Tick `0x41cf00`: werkt met de **wereldtijd** `Wereld+0x30` (niet met dt); H-snelheid 0 ⇒ alleen tijd resetten. Zolang de
  verstreken tijd de rest van de substaat overschrijdt ⇒ `0x41c8b0` (volgende substaat). Segmentduur = `|naar − van| / H.snelheid`.
* Substaat 0 (`0x41ccfc`): `padpos = punt[van] + richting(hoek van→naar) · H.snelheid · t` (alleen x en z); hoek snap.
* Op een punt (`0x41c940`): indices doorschuiven (`0x41c7b0`: lus ⇒ modulo; open pad ⇒ **heen en weer**, richting −1 aan het
  eind); dan `r = rand() % 60` (`0x43ff10(60)`): als `r > 6` en `k = r % 5 + 1 ≠ 0` (en, alleen bij paden met > 1 punt, alleen direct
  na een segment) ⇒ **idle-substaat k+1 (2..6)** met duur `enemy->vtbl[53](substaat)` en doelhoek vast naar het volgende punt;
  anders **substaat 1**: draaien, duur = hoekverschil / draaisnelheid; de hoek wordt met `0x440280` geïnterpoleerd.
* Toepassen (`0x41cd68`): testpunt = `(padpos.x, y + P+0x30 + h/2, padpos.z)`; cel `0x428cc0`; boltest `0x434820(straal − 5)` /
  `0x434830(&punt, cel)`: botsing (`[0x53a554] ≠ 0`, behalve soort 3 met zichzelf of met een instantie waarvan `vtbl[25]()` ≠ 0) ⇒
  **omkeren** (`0x41cad0`: van/naar wisselen, richting omkeren, substaat 7 = draaien) en positie niet wijzigen. Anders
  `grond = 0x41a2a0()` (GetHeight `0x435650` onder pos + h/2); als `0 < grond − y < P+0x30` ⇒ `y = grond` (opstap); `pos.x/z = padpos`;
  platform-delta (alleen y) erbij; botsmiddelpunt + cel bijwerken. Dalen gebeurt via de zwaartekracht van §3.3.
* Er is dus **geen botsing/sweep** tijdens patrouille: de vijand schuift exact over de lijnstukken.

### 5.6 Obstakelsensor (`+0x124`, ctor `0x41cfc0`, tick `0x41d4a0(modus)`)
Reikwijdte `+4` pendelt per frame met ±10 tussen **100 en 200**; 16 richtingen (veelvouden van π/8 = `0x4aa15c`), per richting
een straaltest (`0x41d010`: cel `0x428cc0`, straal `0x435810`) waarvan het trefsoort in `+0x1c + i·8` komt; `0x41d430(modus)` zet
per richting een vrij-vlag (`+0xa4 + i`): modus-bit 1 blokkeert op trefsoort 2, bit 2 op trefsoort 3 (vijanden: modus 3 =
beide). Vraagfuncties: `0x41d2a0(hoek)` vrij?, `0x41d310(hoek)` dichtstbijzijnde vrije richting, `0x41d2c0()` / `0x41d390()`
willekeurige vrije richting (< 0 = geen). Niet tot op instructieniveau gevolgd.

## 6. Schade

### 6.1 Vijand raakt speler (type 4/5/6, toestand 4, `0x41911a..0x4191ec`)
Elke frame van de stormloop: `Touch(1, 0)` = 3D-afstand tussen de positie-pointers `< P+0x04 (30) + Perso.straal`. Dan
`Perso->vtbl[39](enemy, P+0x3c = 1.0, &dir, &punt, 0)` (= `0x44ca00`, PERSO_MOVE §4.4: knockback 500 eenh/s 0.2 s langs `dir`,
0.6 s onkwetsbaar, health −= 1). **De vijand roept zelf geen `vtbl[38]` aan.** Resultaat true (speler dood) ⇒ toestand 10/11
(animatie 11), anders toestand 3 (animatie 10) en daarna `P+0x38` s afkoeling waarin de speler genegeerd wordt.
Buiten toestand 4 doet aanraken **geen** schade.

### 6.2 Speler raakt vijand: `vtbl[39]` type 4/5/6 = `0x419480`
```c
bool Enemy4_TakeDamage(e, attacker, float dmg, vec3 *dir, vec3 *point, int kind) {
    if (e->hitT /*+0x158*/ > 0) return false;             /* nog in "geraakt": onkwetsbaar */
    e->state = 9;  e->anim->vtbl[4]();                    /* AnimCtrl reset */
    e->hitT = e->vtbl[53](0);                             /* 0.25 s */
    return Enemy_TakeDamage(e, attacker, dmg, dir, point, 0);   /* kind wordt 0: pik en stormloop identiek */
}
bool Enemy_TakeDamage(...) /* 0x41adc0 */ {
    if (!Behav_Knock(e->behav, dir)) return false;        /* 0x41b6b0: knockT > 0 ⇒ false; anders knockDir = *dir, knockT = e->vtbl[53](-1) */
    if (kind != 2) Effect_Hit(dmg, point);                /* 0x40c2d0 → 0x4750e0(&point) (sterretje; point == 0 ⇒ niets) */
    e->hp -= dmg;  return e->hp <= 0;
}
```
* `isPeck` wordt **genegeerd**. Verschil pik/stormloop zit alleen in `dir`: de pik geeft `dir = (0,0,0)` ⇒ geen terugslag; de
  stormloop geeft `normalize_xz` ⇒ terugslag met snelheid `600·t_rest` gedurende 0.25 s (≈ 18.75 eenheden totaal).
* Daarna toestand 9: `hp ≤ 0` ⇒ deeltjeseffect `vtbl[57]`, toestand 12: animatie 13, na `AnimLen(13)+1.0` s `+0x10c |= 1`;
  in de tweede helft fade-out via `+0x6c`. Bij het begin van toestand 12 wordt typewoord-bit 0x400 gewist en stopt
  RegisterActor2 (`0x419460`) ⇒ niet meer raakbaar. Het volgende frame roept `0x40bf60` `vtbl[29]` aan: `[0x4c532c]++`
  en `0x407850` (instantie uit de wereld). **Geen SetVar, geen scriptbericht, geen bonus.**
* `vtbl[37]` (waarschuwing) = `ret 4` en `vtbl[38]` = leeg voor type 4/5/6. `vtbl[36]` = `0x40c3a0` = `return 0`.
* `vtbl[40](&pos, r)` (`0x41ae20`, explosie): binnen r ⇒ `vtbl[39](0, hp, &weg, 0, 0)` = direct dood.
* `enemy+0x12c` uit EVENTS §4.2 hoort **niet** bij deze klassen (`+0x12c` is hier de y van de startpositie); `0x44d6e0` en de
  lijst `0x5e4880` horen bij type 40/120/121 (BONUS.md §7).

## 7. Berichten (`vtbl[22]` = `0x414530` → `Enemy::HandleMsg 0x41a740`)

Bericht = `{+0 id, +0xc a, +0x10 b}` (ints). `0x41a740` behandelt alleen id **6** en **11**; al het andere gaat naar de
FadeInst/Instance-handler `0x44e8f0` (o.a. bericht 56 = fade `+0x6c`, MESSAGES.md). Retourneert altijd 0.

| id | a | b | effect | code |
|---|---|---|---|---|
| 6 | aan ≠ 0 | – | **activeren**: als niet in de wereld (`+0x1c < 0`) ⇒ `0x407790(pos + (0, h/2, 0))` (in cel hangen) | `0x41abbf` |
| 6 | 0 | – | **deactiveren**: als in de wereld: staat hij op een press-collision (`+0x198 ≠ −1`) ⇒ `0x442100(col, id)` (UnPress-event) en `+0x198 = −1`; `0x407850` (uit de cellijst; `+0x1c = +0x18 = −1`) | `0x41abfd` |
| 11 | 0 | v | `P+0x1c = v` – leash-afstand tot thuispunt (Dwalen) | `0x41a9e4` |
| 11 | 1 | v | `P+0x20 = v` – **zichtafstand** | `0x41a9f5` |
| 11 | 2 | v | `P+0x2c = max(v, 1)` – max. afstap | `0x41aa06` |
| 11 | 3 | v | `P+0x30 = max(v, 1)` – max. opstap | `0x41aa38` |
| 11 | 4 | – | **`vtbl[17]()` Reset** (terug naar start, hp vol) | `0x41aa6a` |
| 11 | 5 | 0/1 | **"trajet aléatoire"**: 0 ⇒ `Dwalen.0x41c140(0,0)` (leash uit) en vlag 2 uit; 1 ⇒ leash aan rond `+0x134`. Zonder Dwalen-gedrag: (lege) foutmelding "Cet ennemi n'a pas de trajet aleatoire (patrouilleur…)" | `0x41aa76` |
| 11 | 6 | 0/1 | **"trajet de suivi"**: 1 ⇒ vlag 2 aan (FindTarget meet vanaf het thuispunt = bewaker), anders uit. Vereist Achtervolgen-gedrag (`+0x164`), anders melding "…pas de trajet de suivi (tete de turc…)" | `0x41aac8` |
| 11 | 7 | v | `P+0x08 = v` loopsnelheid, en H-snelheid direct := v | `0x41a95a` |
| 11 | 8 | v | `P+0x0c = v` rensnelheid, en H-snelheid direct := v | `0x41a982` |
| 11 | 9, 10 | graden | `P+0x10 = v · (1/180) · π` draaisnelheid (beide cases schrijven `P+0x10`) | `0x41a9aa`, `0x41a9c7` |
| 11 | 12 | v | `P+0x3c` schade | `0x41aaff` |
| 11 | 13 | v | `P+0x34` = hp-max **en** `hp (+0x150) = v` | `0x41ab10` |
| 11 | 14 | v | `P+0x24` max. \|dy\| voor zien | `0x41ab30` |
| 11 | 15 | v | `P+0x38` afkoeltijd (hele seconden) | `0x41ab41` |
| 11 | 18 | v | `+0x154 = v · 0.01` | `0x41a946` |
| 11 | 19 | w | gewicht w voor **alle** 8 dwaal-acties | `0x41a7d6` |
| 11 | 20..24 | w | gewicht dwaal-actie 0..4 (idles) | `0x41a856`… |
| 11 | 25 | w | gewicht actie 5 (draaien) | `0x41a928` |
| 11 | 26, 27 | w | gewicht actie 6, 7 (lopen) | `0x41a8ec`, `0x41a90a` |
| 11 | 30 | 0/1 | vlag 0x20 van `+0x174` | `0x41a7b2` |
| 11 | 31..36 | v | `P+0x40, +0x48, +0x4c, +0x50, +0x54, +0x58` (klassespecifiek, type 7+) | `0x41ab52`… |
| 11 | 37 | v | `P+0xc0` activeringsafstand tot de camera | `0x41a79e` |
| 11 | 11, 16, 17, 28, 29 | | genegeerd | `0x41ac2a` |

Er zijn in deze handler **geen acknowledge-variabelen**; de SetVar-paren `+0x230/+0x294/+0x24c` uit EVENTS §4.2 horen bij de
baasklassen (`0x40c730`, `0x40d850`, `0x40eb50`, eigen handlers `0x40d530`, `0x40e7e3`, `0x410052`, die daarna `0x41a740`
aanroepen). De vijand meldt alleen msgmask **0x200** (op de grond, elke frame in `0x41a4e0`) en wist **0x10** in Reset (`0x41a167`).

## 8. Type 7/8/9 – de schutter (ctor `0x416ca0(subtype)`, vtable `0x4a9dc0`, size 0x20c)

Volledig gelezen (Update `0x416fb0`, jump-table `0x417da0`, 17 toestanden; hulpfuncties `0x417df0..0x418820`). Zelfde basis en dezelfde vier
gedragingen als type 4 (PostLoad `0x416d90` is op de AnimCtrl-ctor na identiek aan `0x418ae0`; `[0x4c5330]++`).

### 8.1 Klassefabriek en parameters

Fabriek `0x403502` (bytetabel `0x403f3c`, sprongtabel `0x403e94`): **type 7 → case `0x40370c` → `push 4`**, **type 8 → `0x4036d3` → `push 5`**,
**type 9 → `0x403745` → `push 6`** (alle drie `new(0x20c)` + `0x416ca0(subtype)`). De ctor zet categorie 2 (`0x40c360(2)`), subtype
(`0x40c380`) en `+0x1c4 = 0`. De tegenstrijdigheid met OBJECTS.md §2.5 was een leesfout daar: "straal 50, zicht 2500, hoogte 180, hp 2,
schade 2" is **subtype 7** (= type 10, case `0x41d95c`); type 7 is subtype 4 (case `0x41d750`).

Definitieve P-waarden (`0x41d510`: defaults, daarna `0x41dc38[subtype−1]`; alles float tenzij vermeld):

| P+ | type 7 (sub 4, `0x41d750`) | type 8 (sub 5, `0x41d7da`) | type 9 (sub 6, `0x41d864`) | betekenis in deze klasse |
|---|---|---|---|---|
| 0x04 | 30 | 30 | 30 | straal |
| 0x28 | 140 | **130** | 140 | hoogte |
| 0x08 | 200 | 200 | 200 | loopsnelheid (scripts: `11 [inst, 7, 150]`) |
| 0x0c | 600 | 600 | 600 | rensnelheid = **snelheid van de stormloop** (`0x41bc80`) én basis van de uitwijksnelheid (×3) |
| 0x5c | 800 | 800 | 800 | **niet gelezen** in deze klasse (alleen type 4) |
| 0x10 / 0x14 / 0x18 | π/2 / **4π** / π | idem | idem | draaisnelheid dwalen / snel (Achtervolgen; richten = `P+0x14 · 4` = 16π rad/s) / – |
| 0x1c | 800 | 800 | **1000** | leash (Reset met TRAJ: 10; scripts `11 [inst, 0, v]`) |
| 0x20 | 1500 | 1500 | 1500 | zichtafstand 3D (scripts `11 [inst, 1, v]`: W1A 800) |
| 0x24 | **800** | 800 | 800 | max. \|dy\| |
| 0x34 | **1** | **2** | **3** | levenspunten |
| 0x38 | 1.5 | 1.0 | 1.5 | afkoeltijd `cool` |
| 0x3c | 1 | 1 | **3** | schade van de hap (stormloop) |
| 0x40 | 1 | 1 | **2** | **schade van het projectiel** → `T+0x30` |
| 0x4c | **2.0** | **1.5** | **2.0** | herlaadtijd (s); scripts `11 [inst, 33, v]` (W2D: 2 en 3) |
| 0x54 | **150** | **150** | **10** | hap-bereik (xz): binnen dit bereik stormloop i.p.v. schieten. Type 9 hapt dus praktisch nooit |
| 0x58 | 300 | 300 | 300 | uitwijkafstand (alleen type 9 gebruikt hem) |
| 0x60 | 1000 | 1000 | 1000 | projectielsnelheid → `T+0x20` |
| 0x64 | 1 | 1 | 0 | → `T+0x54` (begrenzing y; zonder effect omdat `dir0.y = 0`) |
| 0x68 | **0.2** | **0** | **0** | → `T+0x40` stuurfactor xz per 1/60 s: **alleen type 7 is doelzoekend** |
| 0x70 | 0 | 0 | 0 | → `T+0x3c` richthoogte **en** `T+0x44` verticale stuursnelheid (0 ⇒ geen verticaal sturen) |
| 0x74 (int) | **0** | **1** | **3** | → `T+0x60` visueel soort: 0/1 = **missile-model + rook** (`0x4700e0`; SoundFx **17** resp. **18**), 3 = **vuurbal** (`0x470af0`, SoundFx **20**) |
| 0x48 | 150 | 150 | 10 | gezet, **geen lezer** in `0x416ca0..0x4189f0` (bericht 11/32 schrijft hem; W2B gebruikt dat): onzeker |
| 0x6c | 100 | 100 | 100 | gezet, geen lezer gevonden: onzeker |
| 0x44 | 600 | 600 | 600 | terugslagfactor (default) |

Geen van de drie is ballistisch: zwaartekracht komt uit sjabloon 1 (= 0) en de startrichting is **horizontaal** (§8.2). Het projectiel
vliegt dus recht, op mondingshoogte, 1000 u/s, levensduur 15 s (sjabloon 1), straal 5, 0 stuiters; type 7 stuurt in xz bij
(`k = pow(0.8, dt·60)`, PROJECTILES.md §2.2; begrensd door `T+0x50 = 0`: nooit meer dan 90° van de startrichting af), y blijft constant.

### 8.2 Afvuren `vtbl[58](doel, Vec3 m[2])` = `0x418820`

```c
bool Shooter_Fire(Enemy *e, Inst *target, Vec3 *m /* m[0] = monding, m[1] = tweede punt */) {
    ProjT T = { 0x44a260-waarden inline };            /* 0x41883d..0x4188ba: straal 5, snelheid 1000, levensduur 5, schade 20, richth. 150, visueel 4, raakt-alles 1 */
    Proj_Template(1, &T);                             /* 0x449070: overschrijft alles met sjabloon 1 (PROJECTILES.md §1.1) */
    T.pos  = m[0];                                    /* 0x4188e6 */
    T.dir0 = (m[1].x - m[0].x, 0, m[1].z - m[0].z);   /* y = 0 (0x4188d4); genormaliseerd als lengte > 0 (0x418913) */
    T.target = target;  T.damage = P->+0x40;  T.owner = e;
    T.aim_h = T.vsteer = P->+0x70;  T.steer = P->+0x68;  T.lim_y = P->+0x64;
    T.speed = P->+0x60;  T.visual = P->+0x74;
    return Proj_Alloc(&T) != NULL;                    /* 0x41898c -> 0x4490a0 -> 0x449130: SoundFx 17/18/20 (3D op de vijand) + visual */
}
```
Er is **geen eigen geluid** en geen animatie-event: het schot wordt getimed door de toestandstimer `+0x1e4 = AnimLen(0x13)` (§8.4), het
geluid komt uit `0x449130`. De aanroeper (toestand 13) levert `m`: `m[0]` = beginpunt van de eerste marker-node met **typecode 1**
(`0x42f6b0(this, 1, m, 0)`, wereldruimte, in de actuele animatiepose; W1A model 45: node 65 aan bot 9), en overschrijft `m[1]` met
`m[0] + kijkrichting(H) · 10` (`0x41b8b0`, `0x4a9750`): de schietrichting is dus de **kijkrichting van de vijand**, niet de markerrichting
en niet de richting naar de speler (na toestand 12 kijkt hij wel vrijwel exact naar de speler).
Treft het projectiel iets, dan roept `0x44a0a0` `owner->vtbl[41](dood)` = `0x417fd0` aan: `dood` en toestand ≠ 10 ⇒ toestand **8** (juichen).

### 8.3 Velden (boven de Enemy-basis)

| off | betekenis |
|---|---|
| 0x1c0 | toestand 0..16 |
| 0x1c4 | AnimCtrl (0x54 B, ctor `0x4189b0` → basis `0x4369f0`, vtable `0x4a9eac`, `[0]` = `0x4189d0`: record `0x4b26a8 + n·0x1c`) |
| 0x1c8 | `cool`: zolang ≥ 0 wordt de speler niet opgemerkt (aftellen in de proloog zolang ≥ 0) |
| 0x1cc | timer toestand 9 (juichen) = AnimLen(11) |
| 0x1d0 | **herlaadtimer** (proloog: zolang ≥ 0 `−= dt`; na een schot `+= P+0x4c`) |
| 0x1d4 | resterende stormlooptijd = `afstand_xz / P+0x0c` |
| 0x1d8 | timer remmen (toestand 7) = AnimLen(9) |
| 0x1dc | `AnimLen(4, 1)` (Reset `0x416f8f`): lengte van één loop-cyclus, voor de pad-animatie |
| 0x1e0 | duur van de draai-animatie in toestand 11 |
| 0x1e4 | uithaal-timer (toestand 12) = AnimLen(0x13); type 9: daarna nog 0.1 s in toestand 13 |
| 0x1e8 | uitwijk-timer (toestand 16) = `P+0x58 / (3·P+0x0c)` = 0.1667 s |
| 0x1ec | vec3 genormaliseerde 3D-richting naar de speler bij de start van de stormloop (geen lezer gevonden) |
| 0x1f8 | timer toestand 1 (na de hap) = AnimLen(10) |
| 0x200 | vec3 aanvalsrichting van de speler (door `vtbl[37]`), in toestand 15 omgezet in de uitwijkvector |

Reset `vtbl[17]` = `0x416ed0`: `Enemy::Reset`, AnimCtrl-reset; met TRAJ toestand **0** (gedrag Pad, `P+0x1c = 10`), anders toestand **3**
(gedrag Dwalen, `vtbl[6]()` + `0x41c160`); alle timers 0 (dus ook herlaadtimer 0: het **eerste schot volgt direct** op het opmerken).

### 8.4 Toestandsmachine (`vtbl[52]` = `0x416fb0`)

Proloog: `Enemy::Update` (§4.2; de animatiekeuze `vtbl[45]` loopt dus vóór de switch, met de toestand van het vorige frame);
`reload ≥ 0 ⇒ −= dt`; `cool ≥ 0 ⇒ −= dt`; `hitT > 0 ⇒ −= dt`. `doel = FindTarget(1, 0)` (§3.2) wordt per toestand opnieuw gevraagd.
"→ 2*" = het gedeelde eind `0x4178ee`: toestand 2 **zonder** `cool` te zetten.

| # | code | naam | exact gedrag |
|---|---|---|---|
| 0 | `0x417146` | PATROUILLE | als `cool < 0` en doel ⇒ thuis `+0x134` = eigen positie, `Dwalen.0x41c140(1, &thuis)`, → **11** |
| 2 | `0x417057` | NAAR DWALEN | H-snelheid := `P+0x08` (direct), draaisnelheid := `P+0x10` (`0x41b940`), gedrag = Dwalen, `vtbl[6]()`, `0x41c160`, → **3** |
| 3 | `0x4170a3` | DWALEN | als `cool < 0` en doel ⇒ **11**. Daarna (ook als net 11 gezet is): met TRAJ en xz-afstand tot thuis `< 10` ⇒ gedrag = Pad (`0x41cfb0`), → **0** |
| 11 | `0x4171bd` | **WACHTEN/HERLADEN** | geen doel ⇒ 2*. Gedrag = Stilstaan met `Stil+0x38 = 0`; draaisnelheid := `P+0x14 · 4.0` (`0x4a94c0`); doelhoek := hoek(eigen pos → doel) zonder snap (`0x41ba60(own, tgt, 0)`); `+0x1e0 = boog(H.hoek, H.doelhoek) / P+0x14 · 4.0` (`0x4401c0`; letterlijk zo: delen door 4π, **maal** 4); `0x436bd0(2, +0x1e0, 1)` en `0x436bd0(1, +0x1e0, 1)` (draai-animaties zo schalen dat ze `+0x1e0` s duren). **`H.Tick` wordt hier niet aangeroepen** (Stilstaan-Tick `0x41bed0` = kale `0x41b2c0`; de enige `0x41ba90`-aanroep van de klasse staat in toestand 12): tijdens het herladen draait de vijand dus **niet** mee, hij speelt alleen de draai-animatie. Als `reload ≤ 0` ⇒ `+0x1e4 = AnimLen(0x13, 0)`, → **12** |
| 12 | `0x4172da` | **UITHALEN** | geen doel ⇒ 2*. xz-afstand `≤ P+0x54` ⇒ **5** (meteen return). Anders: `+0x1e4 > 0` ⇒ `−= dt`; anders (subtype 6: `+0x1e4 = 0.1` (`0x3dcccccd`)) → **13**. In beide gevallen daarna doelhoek := naar de speler en **`H.Tick(dt)`** (`0x417397`): hier draait hij, met 16π rad/s ≈ binnen enkele frames |
| 13 | `0x4173ca` | **VUREN** | geen doel ⇒ 2*. `0x42f6b0(this, 1, m, 0)` mislukt (geen marker typecode 1) ⇒ return (blijft in 13). `m[1] = m[0] + kijkrichting·10`. Subtype 6: `+0x1e4 −= dt`; nog `> 0` ⇒ return. Zichtlijn `0x497ed0(&(pos + (0, h/2, 0)), &m[0], −1)`: **van het eigen middelpunt naar de eigen monding** (steekt de monding door een muur/instantie?), niet naar de speler. `[0x4c4bd0]` = ruw botsresultaat (PERSO_MOVE.md: 1 = niets, **3 = wereldpolygoon, 4 = instantie**): bij 3 of 4 wordt het schot **overgeslagen**, anders `vtbl[58](doel, m)`. In beide gevallen `reload += P+0x4c`, → **11**. (Of de straal eigen hulls kan raken: onzeker.) |
| 5 | `0x417523` | STORMLOOP START | geen doel ⇒ 2*. xz-afstand `> P+0x54` ⇒ return (**blijft in 5**, staat stil met animatie 8 tot de speler weer binnen bereik of uit zicht is). Anders `+0x1ec = normalize(doel − pos)`; gedrag = Achtervolgen, `vtbl[6]()`, `0x41bc80(doel)` (snelheid `P+0x0c` = 600 direct, draaisnelheid `P+0x14`, eerst op de plaats draaien: §5.3); `+0x1d4 = afstand / P+0x0c`; `0x436bd0(8, +0x1d4, 1)` (stormloop-animatie duurt precies zo lang), → **6** |
| 6 | `0x417664` | STORMLOOP | geen doel ⇒ **14**; `+0x1d4 < 0` ⇒ **14**; `+0x1d4 −= dt`; `Touch(1, 0)` (`vtbl[31]`) niets ⇒ return. Anders exact als type 4: `dir = normalize_xz(doel − pos)`, `punt = pos + dir·P+0x04 + (0, h/2, 0)`, `dood = doel->vtbl[39](this, P+0x3c, &dir, &punt, 0)`; dood ⇒ **8**, anders `+0x1f8 = AnimLen(10, 0)`, → **1** |
| 1 | `0x4177f7` | NA DE HAP | gedrag = Stilstaan (`+0x38 = 0`), H-snelheid := `P+0x08` direct; `+0x1f8 > 0` ⇒ `−= dt`; anders `cool = P+0x38`, → **2** |
| 14 | `0x417883` | REMMEN START | gedrag = Stilstaan; `+0x1d8 = AnimLen(9, 0)`; toestand 7; H-snelheid := `P+0x08`; valt door in 7 |
| 7 | `0x4178cc` | REMMEN | `+0x1d8 < 0` ⇒ `cool = P+0x38`, → **2**; anders `−= dt` |
| 8 | `0x41792e` | JUICHEN START | `+0x1cc = AnimLen(11, 0)`; toestand 9; gedrag = Stilstaan; H-snelheid := `P+0x08`; valt door in 9 |
| 9 | `0x417977` | JUICHEN | `+0x1cc −= dt`; `≤ 0` ⇒ **2** (geen `cool`) |
| 4 | `0x417a19` | **GERAAKT** | `hp ≤ 0` ⇒ `vtbl[57]()` (sterf-deeltjes, §4.2), → **10**; anders `hitT ≤ 0` ⇒ **2**. Gedrag blijft wat het was |
| 10 | `0x4179b6` | **DOOD** | gedrag = Stilstaan (`+0x38 = 0`); `+0x15c += dt`; `vtbl[51]() ≤ +0x15c` ⇒ `+0x10c \|= 1`; elk frame typewoord `&= ~0x400` |
| 15 | `0x417a63` | **UITWIJKEN START** (alleen bereikbaar voor subtype 6) | zie §8.5 → **16** |
| 16 | `0x417ce4` | UITWIJKEN | `+0x1e8 > 0` ⇒ `−= dt`; anders `Stil+0x38 = 0`, **`reload = P+0x4c`**, `cool = P+0x38`, toestand **3**, H-snelheid := `P+0x08` direct, draaisnelheid := `P+0x10`, gedrag = Dwalen, `0x41c0c0(0)` (Dwalen-herstart met actie 0), `0x41c160` |

Cyclus van een schutter die de speler ziet: 3 → 11 (1 frame bij `reload ≤ 0`) → 12 (AnimLen(0x13) = 0.53 s uithalen, draait mee) → 13
(schot; type 9 0.1 s later) → 11 (`P+0x4c` s wachten zonder meedraaien) → 12 → … Komt de speler binnen `P+0x54` terwijl hij in **12**
zit ⇒ stormloop van hooguit `150/600 = 0.25 s`. In 11 en 13 wordt de afstand niet getest.

Overige slots: `vtbl[47]` = `0x417f20` (bytetabel `0x417f48`): RegisterActor2 (`0x40c0b0`) in alle toestanden behalve **10**.
`vtbl[51]` = `0x4149e0` = `AnimLen(13, 0) + 1.0`. `vtbl[53](x)` = `0x4184f0` (tabel `0x4187ac`): toestand 0 ⇒ `Pad+0x58`; 1 ⇒ AnimLen(10);
2/3 ⇒ per dwaal-actie (tabel `0x4187f0`: actie −1 ⇒ 0.0; 0..4 ⇒ AnimLen(14..18); 5 ⇒ AnimLen(26 of 27); 6 ⇒ ΣAnimLen(4, 0..2); 7 ⇒ ΣAnimLen(5, 0..2);
9 ⇒ ΣAnimLen(6, 0..2) − `inst+0xac / inst+0xa0`); **4 ⇒ 0.25** (`0x4a9ca0`); 5/6 ⇒ AnimLen(8); 7/14 ⇒ AnimLen(9); 8/9 ⇒ AnimLen(11); 10 ⇒ AnimLen(13);
11 ⇒ AnimLen(1 of 2); 12 ⇒ AnimLen(0x13); 13 ⇒ AnimLen(0x14); 15 ⇒ AnimLen(0); 16 ⇒ AnimLen(0x15).

### 8.5 Uitwijken (type 9): `vtbl[37]`, `vtbl[42]`, toestand 15/16

* **Aanroeper van `vtbl[37]`** (slot `+0x94`): alleen `0x457f90` in de aanvalscode van de Perso, aanvalstoestand 1 = **start van de
  lucht-pikduik** (`0x457eac`, PERSO_JUMP.md §2.4): dichtstbijzijnde aanvalbare instantie binnen 500 (`0x4632e0`), als die categorie 2 is
  wordt het richtpunt `pos + (0, 0.8·hoogte, 0)`; alleen als de speler meer dan **50 boven** dat richtpunt is: `atkDisp = richtpunt − spelerpos`
  (3D, **niet genormaliseerd**) en `doel->vtbl[37](&atkDisp)`. De gewone pik/stormloop op de grond waarschuwt dus niet.
  (De andere `call [r+0x94]` in de exe – `0x428eef..0x42a18a`, `0x44ea80`, `0x47ed03..` – zijn andere klassen.)
* `vtbl[37](Vec3 *d)` = `0x417ee0`: `if (subtype == 6) { +0x200 = *d; toestand = 15; }` – zonder verdere test (ook tijdens geraakt/juichen; in
  toestand 10 is bit 0x400 al weg, dus dan vindt de doelzoeker hem niet meer). Voor type 7/8 doet het niets.
* `vtbl[42]()` = `0x417ff0`: `toestand = 15` onvoorwaardelijk. **Geen aanroeper gevonden** (geen `call [r+0xa8]` op een Npc in de exe; alle andere
  vijandklassen hebben hier de lege `0x462c60`); `+0x200` houdt dan zijn oude waarde: onzeker/dode code.
* Toestand 15 (`0x417a63`):
```c
d = normalize_xz(e->+0x200) * P->+0x58;            /* 300; y = 0 */       e->+0x200 = d;
c[3] = pos + ( d.x, h/2,  d.z);                      /* van de speler af */
c[2] = pos + (-d.z, h/2,  d.x);                      /* zijwaarts */
c[1] = pos + ( d.z, h/2, -d.x);                      /* andere kant */
c[0] = pos + (-d.x, h/2, -d.z);                      /* naar de speler toe */
for (i = 3; i >= 0 && !Free(e, &c[i]); i--) ;  if (i < 0) i = 3;          /* 0x417bc5: eerste vrije, anders toch c[3] */
e->+0x1e8 = P->+0x58 / (P->+0x0c * 3.0f);          /* 0x4a988c: 300/1800 = 0.1667 s */
e->behav = Stil;  Stil->+0x38 = P->+0x0c * 3.0f;    /* stapsnelheid 1800 u/s */
Stil->+0x2c = normalize_xz(c[i] - pos);            /* staprichting */
e->state = 16;
H_SetAngle(H, angle(c[i] -> pos), snap = 1);       /* 0x41ba60(c[i], own, 1): kijkt TEGEN de sprongrichting in (bij c[3]: naar de speler) */
bool Free(e, Vec3 *c) {                            /* 0x417df0 */
    Ray(&(pos + (0, h/2, 0)), c, -1);              /* 0x4359b0 */    if ([0x53a554] != 0) return false;
    GetHeight(c, -1, 1);                           /* 0x435650 -> [0x53a568] */
    float drop = pos.y - [0x53a568];
    return drop <= P->+0x2c && -drop <= P->+0x30;  /* hoogstens 10 lager / 10 hoger */
}
```
  De verplaatsing zelf loopt via de gewone `0x41b2c0` (sweep, randen, terugslag) met stap `dt · 1800` ⇒ 300 eenheden in 0.167 s. Na afloop
  (toestand 16) is hij `P+0x4c` s ontwapend en `P+0x38` s blind. Wordt hij tijdens 16 toch geraakt (toestand 4, gedrag ongewijzigd), dan
  blijft `Stil+0x38 = 1800` staan tot een toestand het gedrag opnieuw zet: randgeval, letterlijk zo.

### 8.6 Animaties

Tabel `0x4b26a8` (28 records van 0x1c B, eindigt precies waar de type-4-tabel `0x4b29b8` begint), zelfde formaat
`{int sub[4]; int prio; float speed; u8 restart}`; `AnimLen(n, k) = duur(sub[k]) / speed`. Let op: `0x436bd0(n, T, k)` **schrijft**
`speed = Σ_{i<k} duur(sub[i]) / T` in de (globale, door alle vijanden van de klasse gedeelde) tabel.

| n | sub[] | prio | speed | restart | gebruikt voor |
|---|---|---|---|---|---|
| 0 | 0,0,0,0 | 1000 | 3 | 1 | toestand 15 (1 frame) |
| 1 / 2 | 16×4 / 17×4 | **900** | 3 (herschreven) | 1 | toestand 11: draaien/wachten; `H+0x0c > 0` (`0x41b900`) ⇒ 1, anders 2; duur `+0x1e0` |
| 3 | 1,2,2,2 | 1000 | 3 | 1 | (rennen; in deze klasse niet aangevraagd) |
| 4 / 5 | 3,4,5,0 | 1000 | 3 | 1 | dwaal-actie 6 / 7 (lopen) |
| 6 / 7 | 4,4,5,0 / 4×4 | 1000 | 3 | **0** | dwaal-actie 9 / 10 |
| 8 | 13,−1,−1,−1 | 1000 | 3 (herschreven: `duur(13) / +0x1d4`) | 1 | toestand 5/6 stormloop |
| 9 | 15,0,0,0 | 1000 | 3 | 1 | toestand 7/14 remmen |
| 10 | 14,0,0,0 | **1500** | **2** | 1 | toestand 1 hap |
| 11 | 18,0,0,0 | 1000 | 3 | 1 | toestand 8/9 juichen |
| 12 | 11,0,0,0 | 1000 | 4 | 1 | toestand 4 geraakt |
| 13 | 12,−1,−1,−1 | 2000 | 3 | 1 | toestand 10 dood |
| 14..18 | 6..10, 0, −1, −1 | 1000 | **2** | 1 | idle-variaties (dwaal-actie 0..4, pad-substaat 2..6) |
| **19** (0x13) | **21**,−1,−1,−1 | 1000 | 3 | 1 | toestand 12 **uithalen/richten** |
| **20** (0x14) | **22**, 0,−1,−1 | 1000 | 3 | 1 | toestand 13 **gooien/schieten** |
| **21** (0x15) | **19**, 0,−1,−1 | 1000 | 3 | 1 | toestand 16 **uitwijksprong** |
| 22 | 20, 0,−1,−1 | 1000 | 3 | 1 | niet aangevraagd in deze klasse (onzeker waarvoor) |
| 23 / 24 / 25 | 3,4,4,4 / 4×4 / 5,0,−1,−1 | 1000 | 3 | 1 | pad-lopen: aanzet / lus / stoppen |
| 26 / 27 | 16,0,−1,−1 / 17,0,−1,−1 | 1000 | 3 | 1 | op de plaats draaien (dwaal-actie 5, pad-substaat 1 en 7) |

Verschillen met de type-4-tabel: 1/2 (sub ×4, prio 900), 8 (speed 3 i.p.v. 10), 9 (3 i.p.v. 2), 10 (prio 1500, speed 2), 11 (3 i.p.v. 1.5),
14..18 (speed 2), nieuwe 19..22; de type-4-records 19..23 staan hier op 23..27.

`vtbl[45]` = `0x418000` (sprongtabel `0x418184`), daarna altijd `AnimCtrl->Tick(dt)`:

| toestand | 0 | 1 | 2, 3 | 4 | 5, 6 | 7, 14 | 8, 9 | 10 | 11 | 12 | 13 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| anim | pad (`0x4182e0`) | 10 | dwalen (`0x4181e0`) | 12 | 8 | 9 | 11 | 13 | 1 / 2 | **0x13** | **0x14** | 0 | **0x15** |

* Dwalen `0x4181e0` (tabel `0x4182ac`, actie uit `0x41c630`): 0..4 ⇒ 14..18; 5 ⇒ 26 (`H+0x0c > 0`) of 27; 6 ⇒ 4; 7 ⇒ 5; 8 ⇒ niets; 9 ⇒ 6; 10 ⇒ 7.
* Pad `0x4182e0` (tabel `0x4184d0`, substaat uit `0x4883c0`): 2..6 ⇒ 14..18; 1 en 7 ⇒ 26/27 met `0x436bd0(n, Pad+0x58, 1)` (draai duurt de hele substaat);
  0 (segment lopen, `0x4182ff`): `rest = Pad+0x58 − Pad+0x50`; `AnimLen(25) > rest` ⇒ 25 (stoppen); anders `AnimLen(23) > Pad+0x50` ⇒ 23 (aanzet); anders
  `x = Pad+0x58 − AnimLen(25) − AnimLen(23)`, `n = x / +0x1dc + 0.5`, `n > 1 ⇒ x /= floor(n)`, `x < 0.5 ⇒ 0`; `0x436bd0(24, x, 2)`; anim 24.
* Timing van het schot: toestand 13 duurt voor type 7/8 precies één frame; de gooi-animatie 0x14 loopt daarna door omdat de wacht-animaties 1/2
  **prio 900** hebben en `Tick` (`0x436a50`) een lagere prio pas toelaat als de instantie-animatie klaar is (`inst+0xc0 == 1`).
* Bij hoekverschil 0 in toestand 11 wordt `T = 0` ⇒ `speed = duur/0` (oneindig): in de port afvangen (pose vasthouden).
* W1A model 45 (23 anims), duur in s: 11 = 1.2, 12 = 4.4, 13 = 1.2, 14 = 2.0, 15 = 2.4, 16/17 = 1.6, 18 = 12.4, 19 = 1.2, 20 = 1.2, 21 = 1.6, 22 = 1.3 ⇒
  uithalen 0.533 s, gooien 0.433 s, uitwijken 0.4 s, hap 1.0 s, remmen 0.8 s, juichen 4.13 s, geraakt-anim 0.3 s, dood 1.467 + 1.0 = **2.467 s**.

### 8.7 Schade, dood, geluid

`vtbl[39]` = `0x417f60` is regel voor regel `0x419480` (§6.2) met toestand **4** i.p.v. 9: `hitT > 0` ⇒ false; toestand 4; AnimCtrl-reset;
`hitT = vtbl[53](0)` = 0.25 (toestand is dan al 4); `Enemy_TakeDamage(att, dmg, dir, punt, 0)` (terugslag 0.25 s, `hp −= dmg`). Geen extra
onkwetsbaarheid, geen verschil per subtype behalve hp (1/2/3). Dood: toestand 4 → `vtbl[57]` (5 deeltjes) → toestand 10, fade in de tweede
helft van `AnimLen(13) + 1.0`, dan `+0x10c |= 1` → `vtbl[29]`: `[0x4c532c]++`, `0x407850`. Geen bericht, geen bonus.

**Geluid:** in `0x416ca0..0x41dc80` (type 4..9 én de Enemy-basis) staat **geen enkele** `call 0x468a00` (SoundFx). Alle vijandgeluiden (geraakt,
dood, stormloop, hap, idle) zijn **animatie-events type 4** op de root-node van het model (SOUND.md §3, 3D op de instantie, toonhoogte ×
`vtbl[27]()` = `0x40d830` = `enemy+0x170` = 1.0 – dat is de lezer van `+0x170`). W1A model 45: anim 11 (geraakt) t = 0: refs 63/64/65 (elk ⅓),
anim 12 (dood) t = 0: 66/67/68, anim 13 (stormloop) t = 230: 62, anim 14 (hap): 71 en 61, anim 8 (idle): 72, loop/draai-anims: voetstappen;
anims 19..22 (uitwijken/richten/gooien) hebben **geen** events – het schotgeluid is SoundFx 17/18/20 uit `0x449130`. Er is geen "opgemerkt"-geluid.
Hetzelfde geldt voor type 4/5/6 (de port speelt deze events al via `anim_sounds` in `src/main_engine.c` zodra de juiste .ins-animatie op t = 0 start).
SoundFx 6 (`0x44d730`) hoort bij `Bom::Explode`, niet bij vijanden.

### 8.8 Voorkomen (uit de `1200`-berichten van elk level; totaal type 7: 29×, type 8: 74×, type 9: 33×)

Per level gebruikt één model; de scripts sturen na `1200` meestal `11 [inst, 0, leash]`, `11 [inst, 1, zicht]`, soms `[5, 1]` (leash aan),
`[6, 1]` (bewaker), `[7, 150]` (loopsnelheid), `[33, s]` (herlaadtijd), `[32, v]` (`P+0x48`), `[37, v]`.

| level | type | model | instanties: index (x, y, z) | 11-berichten |
|---|---|---|---|---|
| W1A | 7 | 45 | 312 (3701, 4, 2671) | leash 200, zicht 800 |
| K1A | 7 | 42 | 252 (3701, 4, 2671), 293 (4692, 808, 3673), 294 (3093, 808, 3750), 295 (779, −2007, −131), 436, 437, 456 | leash 100/200, zicht 800 |
| W1B | 7 | 29 | 278 (−8773, 2817, −2288), 316 (−458, 4922, −3421; zicht 50), 328 (−3263, 4598, −6421), 331 (−2974, 194, −5013), … 13 stuks | leash 100, zicht 800 |
| S1A | 7 | 40 | 322 (140, −1985, 601), 323 (893, −1990, 606), 325, 379, 510, 511, 517, 518 | zicht 800..1500, `[37, 10]` |
| W2A | 8 | 38 | 357 (9575, 157, −16400; bewaker), 359 (7425, 1369, −12682), 360 (7959, 1369, −12773), 373 (−97, 112, −5503), 375 (5422, 713, −2524; geen berichten), … 18 stuks | leash 200..400, zicht 500..800, loop 150 |
| K2A / S2A | 8 | 37 / 37 | K2A 341 (9575, 157, −16400), 344 (2342, 1335, −5107), … 16; S2A 13 stuks | idem |
| W2B | 8 | 38 | 228 (−1179, −415, −6341), 281 (1755, 778, −4806), 349 (2853, 602, 11155), … 10 stuks | `[32, 600]`, zicht 100..500 |
| W2D | 8 | 37 | 170 (−8918, 3506, −2826), 204 (−7863, 3377, 3067), … 17 stuks | zicht 1000, herladen 2 of 3 s |
| W3A | 9 | 50 | 549 (−10241, −254, 11011), 550 (−4036, −407, 2634; zicht 200) | leash 400, loop 150 |
| K3A | 9 | 52 | 485 (−10251, −254, 10870), 486 (−10241, −254, 11011), 487 (−3423, 799, 3514), 488 (9192, 1090, 3160), 489 | idem |
| W3B | 9 | 42 | 500 (−2496, −3628, −72), 503 (−3466, −290, 3815), 505, 506, 513, 719 | leash 400/800, zicht 600, loop 150/250 |
| W3D / S3A | 9 | 27 / 46 | W3D 193 (−5325, −9204, −23659), 317 (860, −3324, 3916), … 8; S3A 429 (−2427, 514, 864; `[33, 200]`, `[32, 800]`), … 12 stuks | zicht 600..2000 |

De bekeken modellen (W1A 45, K2A 37, K3A 52) hebben 23 animaties en een marker typecode 1 (monding; W1A/K2A node 65 aan bot 9, K3A node 74 aan
bot 26) plus typecode 0.

### 8.9 Recept type 7/8/9 (aansluitend op `src/enemy.c`)

`src/enemy.c` kent nu alleen de type-4-kolom. Uitbreiding:

1. **Parameters per type** i.p.v. `#define`: `{radius 30, height 140/130/140, walk 200, run 600, see 1500, dy 800 (type 4..6: 600), hp 1/2/3,
   cool 1.5/1.0/1.5, bite 1/1/3, shot_dmg 1/1/2, reload 2.0/1.5/2.0, melee 150/150/10, dodge 300, turn π/2, turn_fast 4π (type 4..6: 2π),
   leash 800/800/1000, proj_speed 1000, steer 0.2/0/0, visual 0/1/3}`; bericht 11 (§7) schrijft erin (n = 0, 1, 5, 6, 7, 32, 33, 37 komen voor).
   `enemies_add`: `type 7..9` ⇒ `hp` uit de tabel, begin-toestand 0 (TRAJ) of **3**; eigen toestandsnummers (enum hieronder) naast die van type 4.
2. **Animatietabel** naast `g_ea` (hoofd-.ins-anim, deler, hold): `WALK {4, 3, 0}`, `DASH {13, *, 0}` (deler = `duur(13) / dashT`),
   `BRAKE {15, 3, 1}`, `BITE {14, 2, 1}`, `WIN {18, 3, 1}`, `HIT {11, 4, 1}`, `DEAD {12, 3, 1}`, `IDLE {6, 2, 0}`, `TURN_L {16, *, 0}`, `TURN_R {17, *, 0}`
   (deler = `duur / T`, `T = 4·Δ/P14`; Δ = 0 ⇒ pose vasthouden), `AIM {21, 3, 1}`, `THROW {22, 3, 1}`, `DODGE {19, 3, 1}`. De gooi-animatie moet
   uitspelen terwijl de toestand al 11 is: `THROW` vasthouden tot `anim_time ≥ duur` (prio-regel §8.6) en pas dan `TURN_*` tonen.
3. **Toestanden** (pseudo-C, `see` = FindTarget met `dy 800`, `dxz` = xz-afstand, `to_player` = hoek):
```c
enum { S_PATH=0, S_BITE=1, S_TOWANDER=2, S_WANDER=3, S_HIT=4, S_DASH0=5, S_DASH=6, S_BRAKE=7, S_WIN=9, S_DEAD=10, S_WAIT=11, S_AIM=12, S_FIRE=13, S_DODGE0=15, S_DODGE=16 };
if (e->reload >= 0) e->reload -= dt;   /* naast cool en hit_t */
case S_PATH:   patrol(); if (cool < 0 && see) { home = pos; st = S_WAIT; } break;
case S_WANDER: wander(); if (cool < 0 && see) st = S_WAIT;  /* TRAJ: binnen 10 van home -> S_PATH */ break;
case S_WAIT:   if (!see) { st = S_TOWANDER; break; }  speed = 0;  anim = turn-anim naar teken van ang_diff(to_player, ang), duur 4*|diff|/P14;   /* NIET draaien */
               if (e->reload <= 0) { e->t = len(AIM); st = S_AIM; } break;
case S_AIM:    if (!see) { st = S_TOWANDER; break; }  if (dxz <= melee) { st = S_DASH0; break; }
               if (e->t > 0) e->t -= dt; else { if (type == 9) e->t = 0.1f; st = S_FIRE; }
               steer(e, to_player, 4*P14, dt);  anim = AIM;  break;
case S_FIRE:   if (!see) { st = S_TOWANDER; break; }  anim = THROW;
               if (!inst_vector(inst, 1, &m0, &unused)) break;
               if (type == 9 && (e->t -= dt) > 0) break;
               if (!segment_blocked(pos + (0,h/2,0), m0))         /* gel_ray_frac; instantie-hulls zodra beschikbaar */
                   shot_spawn(m0, (cos ang, 0, sin ang), owner = inst, target = player, dmg, steer, visual);   /* audio_fx 17 / 18 / 20 op de vijand */
               e->reload += P4c;  st = S_WAIT;  break;
case S_DASH0:  if (!see) { st = S_TOWANDER; break; }  if (dxz > melee) break;
               turn_t = |ang_diff| / P14;  speed = want = run;  e->atk_t = dxz / run;  DASH-deler = duur(13) / atk_t;  st = S_DASH;  break;
case S_DASH:   if (!see || e->atk_t < 0) { e->t = len(BRAKE); want = walk; st = S_BRAKE; break; }  e->atk_t -= dt;  steer(P14); move na turn_t;
               if (dist3 < radius + 69) { hit = player_hit(pl, bite, dir_xz); if (hit) { player_kill(pl, 3); e->t = len(WIN); st = S_WIN; } else { e->t = len(BITE); st = S_BITE; } }  break;
case S_BITE:   if (e->t > 0) e->t -= dt; else { cool = P38; st = S_TOWANDER; }  break;
case S_BRAKE:  if (e->t < 0) { cool = P38; st = S_TOWANDER; } else e->t -= dt;  break;
case S_WIN:    if ((e->t -= dt) <= 0) st = S_TOWANDER;  break;                  /* ook gezet door shot -> owner als player_hit true gaf (vtbl[41]) en st != S_DEAD */
case S_TOWANDER: speed = want = walk; wander_restart(); st = S_WANDER; break;
case S_HIT:    if (hp <= 0) { death_fx; attackable = 0; st = S_DEAD; } else if (hit_t <= 0) st = S_TOWANDER;  break;
case S_DEAD:   als type 4 toestand 12 (L = len(DEAD) + 1.0).
case S_DODGE0: d = norm_xz(e->warn) * 300; kandidaten pos+d, pos+(-d.z,d.x), pos+(d.z,-d.x), pos-d: eerste met vrije straal op h/2 en grond binnen ±10 (anders pos+d);
               e->dodge_dir = norm_xz(c - pos); e->ang = hoek(c -> pos) (snap); e->t = 300 / (3*run); st = S_DODGE;  break;
case S_DODGE:  anim = DODGE; if (e->t > 0) { e->t -= dt; enemy_move(e, pl, dodge_dir * 3*run*dt); } else { reload = P4c; cool = P38; want = speed = walk; st = S_WANDER; }  break;
```
4. **Hooks**: `enemy_take_damage` ⇒ toestand `S_HIT` (4) voor type 7..9, `removed`/`S_DEAD` i.p.v. 12. Nieuw `enemy_warn_dive(Enemy*, Vec3 d)` = `vtbl[37]`:
   alleen type 9 ⇒ `warn = d; st = S_DODGE0`; aanroepen uit `player.c` op het moment dat de lucht-pikduik start met een vijand als auto-aim-doel
   (binnen 500, speler > 50 boven `pos.y + 0.8·h`), met `d = (pos + (0, 0.8h, 0)) − spelerpos`.
5. **Projectiel**: `Shot` in `src/main_engine.c` uitbreiden met `speed`, `damage`, `steer`, `target`, `visual`, `owner_enemy`; per frame (alleen `steer > 0`):
   `k = powf(1 − steer, dt·60)`; `dir.xz = norm_xz(target − pos)·(1 − k) + dir.xz·k` (richthoogte 0), hernormaliseren, en terugzetten als
   `dot(dir.xz, dir0.xz) < 0`; y blijft 0. Treffer op de speler ⇒ `player_hit(dmg)`; bij dood `enemy → S_WIN`. Levensduur 15 s, eerste wereldtreffer = weg.
   Visual 0/1 = missile (PROJECTILES.md §5.3: lint 20 × 25, kop beeld 4 oranje, explosie `0x477060(2, …)`, model uit de type-41-pool; W1A heeft er 8),
   visual 3 = vuurbal (§5.5). Tot die visuals bestaan: de bol van visual 2 tonen met het juiste geluid.
6. **Test W1A**: instantie 312 (model 45) op (3701, 4, 2671), zicht door het script 800, leash 200: binnen 800 komen ⇒ 0.53 s uithalen, missile op
   mondingshoogte die in xz naar Woody buigt, daarna elke 2.0 s één schot; binnen 150 tijdens het uithalen ⇒ korte stormloop met hap (1 hartje);
   één pik = dood (hp 1), geluid 63..65 en 66..68 uit de animatie-events. K3A/W3A 549/550 voor het uitwijken van type 9 (lucht-pikduik van boven).

## 9. Recept: eenvoudigste vijand (type 4 zonder TRAJ) in C

```c
enum { E_WANDER=8, E_NOTICE=2, E_CHASE=1, E_DASH=4, E_MISS=3, E_BRAKE=6, E_HIT=9, E_WIN=11, E_DEAD=12 };
#define R 30.f      /* P+4  */  #define H 140.f    /* P+0x28 */  #define WALK 200.f /* P+8  */
#define RUN 600.f   /* P+0xc*/  #define DASH 800.f /* P+0x5c */  #define SEE 1500.f /* P+0x20, |dy|<600 */
#define TURN 1.5708f /* P+0x10; achtervolgen: 6.2832 */  #define ACC 1000.f  #define COOL 1.5f /* type5 1.0, type6 0.5 */
void enemy_update(Enemy *e, float dt) {
    if (dist2(e->pos, cam) >= 3000*3000 && e->hp > 0) return;
    if (e->cool >= 0) e->cool -= dt;   if (e->hitT > 0) e->hitT -= dt;
    Player *p = (dist(e->pos,pl->pos) < SEE && fabsf(pl->pos.y-e->pos.y) < 600) ? pl : NULL;
    switch (e->st) {
    case E_WANDER: wander_tick(e, dt);           /* afwisselend idle-anim (duur = animlengte) en 200 eenh/s rechtuit */
        if (e->cool < 0 && p) e->st = E_NOTICE;  break;
    case E_NOTICE: if (!p) { e->st = E_WANDER; break; }
        e->turnT = angdiff(e->ang, angle_to(e,p)) / 6.2832f; e->speed = RUN; e->st = E_CHASE;  /* fallthrough */
    case E_CHASE:  if (!p) { e->st = E_WANDER; break; }
        steer(e, angle_to(e,p), 6.2832f, dt); if ((e->turnT -= dt) <= 0) move(e, e->speed*dt);  /* sweep, geen afstap > 10 */
        e->atkT = animlen(8) + 0.5f*animlen(10);
        if (dist_xz(e,p) <= DASH*e->atkT && dot(dirvec(e->ang), norm_xz(p->pos - e->pos)) > 0.95f) { e->speed = DASH; e->st = E_DASH; }
        break;
    case E_DASH:   steer(...); move(e, e->speed*dt);
        if (!p || e->atkT <= 0) { e->t = animlen(9); e->st = E_BRAKE; break; }   e->atkT -= dt;
        if (dist(e->pos, p->pos) < R + p->radius) {
            vec3 d = norm_xz(p->pos - e->pos), pt = e->pos + d*R + (vec3){0,H/2,0};
            if (player_hit(p, e, 1.0f, &d, &pt, 0)) { e->t = animlen(11); e->st = E_WIN; }
            else { e->t = animlen(10); e->st = E_MISS; } }
        break;
    case E_MISS: case E_BRAKE: if ((e->t -= dt) <= 0) { e->cool = COOL; e->speed = WALK; e->st = E_WANDER; } break;
    case E_WIN:  if ((e->t -= dt) <= 0) { e->speed = WALK; e->st = E_WANDER; } break;
    case E_HIT:  if (e->hp <= 0) { spawn_death_fx(e); e->attackable = 0; e->st = E_DEAD; }
                 else if (e->hitT <= 0) e->st = E_WANDER;   break;
    case E_DEAD: e->deadT += dt; float L = animlen(13) + 1.0f;
                 if (e->deadT > L/2) e->fade = fminf(1, (e->deadT - L/2)/(L/2));  if (e->deadT >= L) remove(e); break;
    }
    knockback_and_gravity(e, dt);   /* terugslag: v = 600·knockT (0.25 s); val: v += 200·dt − 0.2·v per frame, y −= v */
}
bool enemy_take_damage(Enemy *e, void *att, float dmg, vec3 *dir, vec3 *pt, int isPeck) {
    if (e->hitT > 0 || e->knockT > 0) return false;
    e->st = E_HIT; e->hitT = e->knockT = 0.25f; e->knockDir = *dir; e->hp -= dmg;  return e->hp <= 0; }
```
(hp: type 4 = 1, type 5 = 1, type 6 = 2; met TRAJ komt daar toestand 0 = §5.5 bij en keert hij via het thuispunt terug.)

## 10. Open vragen
* Basis-slots 14..16, 23, 24 (`0x41ad80` vult `{pos.x, pos.y + h/2, pos.z, straal, h/2}` = botscilinder), 27/28/30, 50, 54 zijn niet
  benoemd; `vtbl[56]` = `0x41b030` (verplaatsing met `0x4359b0`/`0x436dc0`) is niet gelezen – het wordt in type 4 niet aangeroepen.
* `Enemy+0x14c`, `+0x154` (bericht 11/18, ×0.01): geen lezer gevonden in type 4. `+0x170` (1.0) = toonhoogtefactor van de animatiegeluiden (`vtbl[27]` = `0x40d830`, §8.7).
* Vlag 0x20 van `+0x174` (PostLoad zet, bericht 11/30 schakelt) en vlag 8: geen lezer/zetter gevonden in de gelezen code.
* `0x437580` (sweep), `0x437040` (push-out t.o.v. andere actoren?) en `[0x4b310c]` (fractie ≥ 0.8 = "vrij") zijn alleen aan de
  aanroepkant bekeken; `0x436d20/0x436d80` (platform) idem.
* De obstakelsensor (§5.6) is niet op instructieniveau gevolgd. Type 7/8/9: `vtbl[42]` (`0x417ff0`) heeft geen gevonden aanroeper; `P+0x48` en `P+0x6c`
  hebben geen lezer in de klasse; animatierecord 22 (sub 20) wordt nergens aangevraagd (§8).
* Parameters `P+0x44` (600) is bewezen de terugslagfactor; `P+0x48, +0x50, +0x58, +0x84..0xbc` horen bij types 10..13 (niet gelezen).
* Types 10..13 (eigen Update `0x415490`, `0x412310`, `0x4110c0`, `0x413ab0`; type 12 met msgmask 0x10 in `0x411729` en eigen
  `vtbl[31]/[40]`) zijn niet geanalyseerd; de hiërarchie, P-tabel en berichten van §1, §2.3 en §7 gelden er wel voor.
* Welke AnimCtrl-subanimatie-indices (`sub[]` 0..18) bij welke animatie in de modelbestanden horen, moet uit de W1A-modellen komen.
* De speler-dood na een vijandelijke treffer: de vijand roept geen `Perso->vtbl[38]` aan; waar de Perso dat zelf doet staat in
  PERSO_FRAME (§ rond `vtbl[38](3)`), niet hier geverifieerd.
