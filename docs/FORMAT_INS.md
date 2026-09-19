# Bestandsformaat `.ins` (level-instances) – Woody Woodpecker: Escape from Buzz Buzzard Park

Afgeleid uit de disassembly van `Woody.exe` (MSVC 6): loader `0x427290` (`level::LoadIns(stream, closeFlag)`,
aangeroepen vanuit de level-loader `0x426bd0` op `0x4270eb`), sub-lezer `0x428bc0` (trajectorie),
instance-ctor `0x42e1a0`, `0x440370` (quaternion → 3×3-matrix), `0x407790` (wereldcel zoeken),
`0x437ca0` (padlengte), pose-evaluatie `0x43a2b0`/`0x43a3a0` met tracklezers `0x43a590` (positie),
`0x43a9c0` (rotatie), `0x43a7b0`/`0x43a820` (events), animatieklok `0x43eee0`/`0x43f074`,
volume-/collisiontests `0x430210`/`0x4303e0`, renderer `0x43b3f0`.
Parser: `tools/insparse.py` (alle 28 levels worden byte-exact geconsumeerd).

Conventies: little-endian, `u32` = 32-bit unsigned, `f32` = IEEE single, `vec3` = 3×f32.
Alle tellers zijn `u32`. **Node-indices in het bestand zijn 1-based**: node 0 is de (niet opgeslagen)
root van het model, node `k` (1 ≤ k ≤ B−1) is element `k−1` van de node-array (`-1` = geen).

## 1. Globale opbouw

```
u32  nslots        -> level+0x68  (aantal script-objecten; == "nobj" in de code-header)
u32  nmodels
nmodels × MODEL    (§2)
u32  ncameras
ncameras × CAMERA  (§7)
u32  trailer       (altijd 0; gelezen, niet gebruikt)
```

De loader maakt `level+0x6c` en `level+0x40` aan als `(nslots+16)` pointers (`level+0x74 = 16`
reserve, `level+0x70 = nslots`, `level+0x3c = nslots+16`) en vult alle slots met een lege
placeholder (0x28 bytes, vtable `0x4aa28c`, `+4` = index, `+0x1c` = −1). Instances en camera's
overschrijven daarna het slot `id & 0xffffff`.

Het `.ins`-bestand bevat **geen** verwijzing naar `.gel`-groepen: de geometrie van alle
bewegende/plaatsbare objecten (inclusief het Woody-karaktermodel met 142 nodes en 91 animaties,
dat in elk level als model 0 voorkomt) zit **inline** in het `.ins`.

## 2. MODEL (engine-struct `S`, 0x70 bytes, malloc op 0x427454)

Een model is een hiërarchie van nodes met eigen puntenlijst, polygonen en keyframe-animaties,
plus een lijst instances die het model in de wereld plaatsen.

| # | Grootte | Type | Betekenis | Engine |
|---|---|---|---|---|
| 1 | 4 | u32 `B` | aantal nodes **inclusief root**; er volgen `B−1` node-records | `S+0x64 = B−1`, `S+0x68` = node-array (0x90 bytes/node, met u32 count ervoor) |
| 2 | 4 | u32 `A` | aantal animaties | `S+4` |
| 3 | 4 | u32 | totaal aantal polygonen (== §2.5 `npolys_total`) | `S+0xc` |
| 4 | 4 | u32 | aantal punten (duplicaat van §2.4) | `S+0x1c` (wordt later overschreven) |
| 5 | 8·A | A × {u32 `nframes`, u32 `duration`} | animatietabel: `nframes` = lengte in frames (= tijdwaarde van het laatste keyframe), `duration` = speelduur in **1/4096 s** (`0x43eee0`: `fild [S+8+i*8+4]; fmul 1/4096`). Bijv. (1200, 0x6000) = 1200 frames in 6 s = 200 fps; (100, 0xA000) = 10 s | `S+8` |
| 6 | 4 | u32 | eerste top-level node (1-based); de overige top-level nodes volgen via `next_sibling` | `S+0x6c` |
| 7 | 4 | u32 | bounding-box node (1-based, flags 0x02). Modellen zonder bbox-node (alleen volumes) bevatten hier `B` (= buiten bereik) | `S+0x24` |
| 8 | 4+4n | u32 n, n×u32 | lijst **marker-nodes** (flags 0x20) | `S+0x50/0x54` |
| 9 | 4+4n | u32 n, n×u32 | lijst lichtnodes (flags 0x40); `n` bepaalt het aantal lichten dat per instance gekopieerd wordt | `S+0x28/0x2c` |
| 10 | 4+4n | u32 n, n×u32 | lijst **volume-nodes** (flags 0x08) = triggervolumes voor de VM | `S+0x48/0x4c` |
| 11 | 4+4n | u32 n, n×u32 | lijst **hull-nodes** (flags 0x04) | `S+0x38/0x3c` |
| 12 | 4+4n | u32 n, n×u32 | lijst **helper-nodes** (flags 0x10) | `S+0x40/0x44` |
| 13 | 4 | u32 `n58` | aantal **press-nodes** (flags 0x01) | `S+0x58` |
| 14 | 4 | u32 `ncol` | aantal collision-id's per instance (0 of 1) | `S+0x60` |
| 15 | 4·n58 | n58×u32 | lijst press-nodes | `S+0x5c` |
| 16 | 4+4n | u32 n, n×u32 | lijst **mesh-nodes** (flags 0x00) | `S+0x30/0x34` |
| 17 | var | (B−1) × NODE | zie §2.1 | |
| 18 | var | EXTRA-LIJSTEN | zie §2.2 | |
| 19 | var | PUNTEN | zie §2.3 | |
| 20 | var | POLYGONEN | zie §2.4 | |
| 21 | var | DRIEHOEKEN | zie §2.5 | |
| 22 | var | INSTANCES | zie §2.6 | |

In de data is elke lage flag-byte 1:1 gekoppeld aan één lijst (gecontroleerd over alle levels):
0x00→mesh, 0x01→press, 0x02→bbox (geen lijst, `S+0x24`), 0x04→hull, 0x08→volume, 0x10→helper,
0x20→marker, 0x40→licht, 0x80→dummy (geen lijst).

### 2.1 NODE (engine-struct `N`, 0x90 bytes)

| # | Grootte | Type | Betekenis | Engine |
|---|---|---|---|---|
| 1 | 4 | u32 `flags` | bits 0–7: nodesoort (zie boven); bits 8–15: **typecode** (waargenomen 1,2,4,5,6,7,9; `0x42f6b0` zoekt nodes op typecode, `0x436dc0` eist code 1 voor press-nodes); bits 16–31: sub-index in `inst+0x70` (alleen typecode 1) | `N+0` |
| 2 | var | – | afhankelijk van `flags`: | |
| | 4 | u32 | **gewoon** (flags & 0x70 == 0, of flags & 0x20 gezet zonder 0x40/0x10): `npolys` | `N+4`; `N+0x10 = 0` |
| | | | – bij marker-nodes (0x20) is dit veld in de data een **f32** (≈45.0) en worden geen polygonen gelezen | |
| | 12 | f32, u32, u32 | **licht** (0x40): intensiteit (engine × 3.0), kleur `0x00RRGGBB`, extra (0 of 4). Komt niet in `N` maar in een tijdelijke lijst → per instance `inst+0x74` (16 B: R,G,B,pad, f32 int·3, u32 extra), `inst+0x88 = 2` | |
| | 12 | f32, f32, u32 | **helper** (0x10): `v_c`, `v_8`, `mode` (0/1/2). Engine slaat `1/v_c` in `N+0xc`, `1/v_8` in `N+8`, `mode` in `N+4`; renderer `0x43b7a3` schakelt op `mode` en past de schalen toe op de vertices van de **ouder**-mesh (`N+0x84` moet de mesh zijn) | |
| 3 | 4 | u32 `npoints` | aantal punten van deze node (aaneengesloten blok in de modelpuntenlijst) | `N+0x14`; `N+0x18` = startindex (afgeleid), `N+0x1c` = pointer |
| 4 | 12 | vec3 `pivot` | draaipunt; de loader trekt het van alle punten van de node af, berekent `N+0x2c` = max |p| (boundingradius) en `N+0x30..0x38` = zwaartepunt | `N+0x20..0x28` |
| 5 | 12 | u32 `a`, u32 `b`, u32 `c` | omvang van de track-pool: `a` dwords positieframes, `b` dwords rotatieframes, `c` **bytes** eventrecords | |
| 6 | (a+b+c/4)·4 | bytes | **track-pool** (alleen als a+b+c ≠ 0): [positieframes][rotatieframes][events] | `N+0x88` |
| 7 | 8·A | A × {u32 off, u32 cnt} | alleen als `a≠0`: per animatie offset (dwords, t.o.v. begin pool) en aantal positieframes. Frame = {f32 t, f32 x,y,z} (16 B), lineair geïnterpoleerd (`0x43a590`) | `N+0x70` |
| 8 | 8·A | A × {u32 off, u32 cnt} | alleen als `b≠0`: per animatie offset (t.o.v. begin rotatiesectie; engine telt `a` erbij) en aantal rotatieframes. Frame = {f32 t, f32 qx,qy,qz,qw} (20 B), geïnterpoleerd via `0x440a80`, → matrix via `0x440370`. Uitzondering: dummy-nodes (flags 0x80) hebben 3 dwords per frame (inhoud 0) en worden door `0x43a3f2` nooit geëvalueerd | `N+0x74` |
| 9 | 8·A | A × {u32 off, u32 cnt} | alleen als `c≠0`: per animatie offset (t.o.v. begin eventsectie; engine telt `a+b` erbij) en aantal events. Record = {u32 type, f32 t, …}, grootte afhankelijk van type: 3 → 15 dwords (bevat 3×4-matrix op +0xc, vervangt de node-matrix), 4 → 9 dwords, 5 → 6 dwords (`0x43a7b0`). In de data komen alleen 4 en 5 voor | `N+0x78` |
| 10 | 4 | i32 `first_child` | eerste kind (1-based) of −1 | `N+0x7c` |
| 11 | 4 | i32 `next_sibling` | volgende broer (1-based) of −1 | `N+0x80` |

Afgeleid door de loader: `N+0x84` = ouder (loop over de kindlijst van elke node), `N+0x8c` = 0, `N+0x6c` = 0.
De animatiehiërarchie wordt door `0x43a3a0` recursief afgelopen (kind via `+0x7c`, broers via `+0x80`);
de wereldmatrices staan in `[0x509adc]+0xa0` per node (0x30 bytes, basis `inst+0x5c`).

### 2.2 EXTRA-LIJSTEN (`node+0x8c`)

Aantal = `len(hull-nodes)` + 1 als het model mesh-, hull- of press-nodes heeft (loader `0x427acb`).
Per lijst: `u32 node` (1-based), `u32 cnt`, `cnt × u32`. In de data zijn dit de hull-nodes plus de
bbox-node; de inhoud zijn puntindices binnen de node (voor een 8-puntsdoos bijv. 12 driehoeken =
36 indices, maar ook lengtes als 52 komen voor, dus geen zuivere driehoeklijst). Alleen gebruikt door
`0x4738c0`/`0x494f5d` (geen directe aanroepers gevonden; vermoedelijk debug/editor).

### 2.3 PUNTEN (engine 0x28 bytes/punt, `S+0x20`)

```
u32  npoints              -> S+0x1c
npoints × vec3 position   -> P+0   (loader maakt ze relatief t.o.v. de node-pivot)
npoints × vec3 normal     -> P+0x10 (wordt genormaliseerd)
npoints × vec3 color      -> P+0x1c (vertexkleur 0..255; verdubbeld als [0x5e8650]+0x20 == 0)
```
`P+0xc` = UV-splitsvlag (runtime), `P+0x28..` UV's worden door de loader per materiaal berekend
(planaire projectie, zie §5).

### 2.4 POLYGONEN

```
u32  npolys_total        (== som van npolys van alle mesh-nodes)
u32  nindices_total      (== som van alle nverts)
per node zonder flags&0x70, in nodevolgorde, per polygoon:
   u32 material   -> low16 in poly+0
   u32 flags      -> low8 | nodeflags in poly+4 (bits 5-6 worden door textuurflags&6<<4 vervangen)
   u32 nverts     -> low16 in poly+2
   nverts × u32   -> u16 indices in poly+0x18.. (index in de modelpuntenlijst, absoluut)
```
Engine-polygoon: `0x18 + 2·nverts` bytes, afgerond op 4; `poly+8..0x14` = vlak (n, d), door de loader
uit de twee grootste randvectoren berekend. `material` bit 15 gezet → vlakke kleur **RGB565** (geen
textuur, 0xFFFF = wit); bit 15 vrij → index in de **materiaaltabel aan het eind van het `.tex`**
(`level+0x5c`, 36 B/entry: 8 floats UV-projectie + textuurpointer; ingelezen op `0x426f54`, count
komt exact overeen met de hoogste index in het `.ins`). Polygoonflag bit 0 (0x1) slaat de
UV-berekening over; waargenomen flagwaarden 0, 2, 8.

### 2.5 DRIEHOEKEN (`S+0x14`/`S+0x18`, 32 B per engine-polygoon)

```
u32 ntris
ntris × { u32 i2, u32 i1, u32 i0, u32 material }   (volgorde in bestand: laatste index eerst)
```
Zelfde materiaalcodering; UV's komen bij deze driehoeken rechtstreeks uit de materiaalentry
(`entry+8-4k` / `entry+0x18-4k`). Alleen het Woody-model (712 stuks) en enkele andere gebruiken dit.

### 2.6 INSTANCES (engine-object 0xfc bytes, vtable `0x4aa31c`, ctor `0x42e1a0`)

```
u32 ninstances                        -> S+0
per instance:
   u32   unk0          gelezen in een lokale variabele, niet gebruikt (0; 1× per level 8209, soms 22)
   TRAJ  trajectory    zie §6                                        -> inst+0x78
   vec3  position                                                    -> inst+0xc
   4×f32 quaternion (x,y,z,w)  -> 3×3 rotatiematrix (0x440370)       -> inst+0x28..0x4c
   3×f32 scale (x,y,z), meestal 1.0                                  -> inst+0x4c,0x50,0x54
   u32   id            0x01000000 | slot                             -> inst+4; level[0x6c][slot] = level[0x40][slot] = inst
   (nvol + ncol) × u32 VM-id's                                       -> inst+0x70
```
`nvol` = aantal volume-nodes (§2 #10), `ncol` = `S+0x60`. De eerste `nvol` id's zijn
`0x03000000 | world_volume-index` (één per volume-node, in lijstvolgorde), de laatste `ncol`
`0x07000000 | world_collision-index` (geselecteerd via bits 16–31 van de press-node-flags,
`0x436dc0`). Verder zet de loader `inst+0xf8 = S`, `inst+0xf4` = `len(mesh-nodes)` × 28 B
runtime-state, `inst+8` = flags (`&0xc1|1`), `inst+0x1c` = wereldcel via `0x407790`
(log "An object outside the world" als −1), `inst+0x74/0x88` = lichten (§2.1).

## 3. Trajectorie (sub-lezer `0x428bc0`, engine-object 0x14 bytes)

```
u32 npoints          (0 → geen object, pointer NULL)
npoints × { u32 unk (altijd 0), f32 x, f32 y, f32 z }   -> T+0x10 (16 B/punt)
u32 closed           -> bit 16 van T+0 (gesloten pad)
```
`T+0` low16 = npoints, `T+0xc` = totale padlengte (som van segmenten, `0x437ca0`; gebruikt alleen +4..+0xc).

## 4. CAMERA (engine-object 0x2c bytes, ctor `0x498b90`, vtable `0x4ac040`; met pad `0x4aa224`)

```
vec3 position        -> cam+0xc..0x14
u32  id              0x01000000 | slot  -> cam+4; level[0x6c][slot] = level[0x40][slot] = cam
TRAJ trajectory      -> cam+0x28 (als aanwezig krijgt de camera vtable 0x4aa224)
```
`cam+8 = (cam+8 & ~0x1c) | 3`, `cam+0x1c` wereldcel via `0x407790` ("A camera outside the world").
Camera's delen dus de objectslots met instances (zelfde tag 0x01).

## 5. Betekenis van de VM-verwijzingen

| Tag | Betekenis | Bron |
|---|---|---|
| `0x01000000 \| i` | script-object *i* = instance **of camera** in `level[0x6c][i]`; `nslots` == `nobj` in de `code`-header (bijv. 507 in W1A). Niet alle slots zijn in het `.ins` gevuld (placeholders blijven staan) | loader, `0x4012fb` e.a. maskeren `& 0xffffff` |
| `0x02000000 \| i` | **scriptvariabele** *i*: in elke level is de hoogste voorkomende index < `nvars` uit de code-header (bijv. W1A: max 154, nvars 161); komt niet in het `.ins` voor | code-bestanden |
| `0x03000000 \| i` | **world_volume** *i*: staat per instance in `inst+0x70[k]` voor volume-node *k*; de indices zijn 0..nvol−1 aaneengesloten en `nvol` komt overeen met de code-header (W1A 218/218; W2B, W2D, W3B, K3A, S3A, W3A hebben 1–4 volumes meer in de code dan in het `.ins`). `0x430210` test per frame of een actor binnen alle vlakken van de volume-node ligt (convexe polyeder in instance-ruimte, met schaal) en stuurt bericht 0x65/0x66/0x67 (Enter/Leave/In) met (instance-id, volume-id, actor-id); `0x443e20` indexeert de tabel `[0x5d0544]` (16 B/entry) met `id & 0xffffff` | |
| `0x07000000 \| i` | **world_collision** *i*: `inst+0x70[nvol + sub_index]`; `0x4303e0`/`0x436dc0` sturen Press/UnPress/In (`0x441f00`, `0x4420c0`, …) via tabel `0x443d90`. Aantal komt overeen met `ncol` in de code-header | |

De **geometrie van de volumes** wordt dus wel degelijk door het `.ins` gedefinieerd: elke
volume-node is een convex polyeder (meestal een doos van 8 punten/6 polygonen, pivot 0) in de
modelhiërarchie, geplaatst via de instance-matrix. Het `code`-bestand bevat alleen de watcher-lijsten.

## 6. Overige engine-details

- Animatieklok (`0x43eee0`): fase = (now − `inst+0xa8`) / (duration/4096 / `inst+0xa0`); `inst+0xac` = fase·duration; frame = fase·nframes. `inst+0xb0` = huidige animatie-index, `inst+0xb4` = volgende.
- De trackverwijzingen (`off`, `cnt`) met `cnt = 0` betekenen: geen keyframes voor deze animatie (node blijft op de laatst berekende/identiteitsmatrix).
- Bounding-box node (`S+0x24`) wordt gebruikt voor zwaartepunt/bounds (`0x42e580`, `0x42eedd`); mesh-nodes uit `S+0x30` fungeren ook als botsingsbollen (radius `N+0x2c`) tegen de wereld (`0x42f110`).
- Marker-nodes (`S+0x50`): 2 punten; `0x42f6b0(typecode, n, out)` levert de getransformeerde punten van de *n*-de marker met die typecode (aanhechtpunten voor script/effecten).
- Materiaaltabel in `.tex`: na `u32 nouter, u32 ntotal` en de textures (36 B header + frames·w·h·2) volgt `u32 nmat` en `nmat × {u32 textuur, 12×f32}`; de engine bewaart f0,f3,f6,f9 (u-rij) en f1,f4,f7,f10 (v-rij).

## 7. Onzekerheden / open vragen

1. Typecodes in flag-bits 8–15 (1,2,4,5,6,7,9): alleen code 1 (press) en de opzoekfunctie `0x42f6b0` zijn geverifieerd; de betekenis van de andere codes (aanhechtpunten voor hand/voeten? camera-doelen?) is niet bepaald.
2. Helper-nodes (0x10): `mode` 0/1/2 en de twee schalen worden in de renderer op de oudermesh toegepast (vermoedelijk textuurcoördinaat-/effectprojectie, waarden als 35×40, 47×47); exacte functie niet uitgezocht.
3. Event-records type 4 (9 dwords: `{4, t, u32, u32, f32, f32, u32=100, f32=200, u32}`) en type 5 (6 dwords: `{5, t, u32, u32, 0, 0}`) op de root-node van het Woody-model: waarschijnlijk geluid/voetstap/effect-triggers; semantiek van de parameters onbekend. Type 3 (matrix-override) komt in de data niet voor.
4. Lichtnode `extra` (0 of 4) en het per-instance veld `unk0` (8209 bij precies één instance per level, 22 in de hub-levels) zijn door de loader niet gebruikt; betekenis onbekend.
5. De extra-lijsten (`node+0x8c`) hebben geen door de loader afgedwongen structuur; hun gebruikers `0x4738c0`/`0x494f5d` hebben geen aanroepers in de disassembly.
6. Polygoonflag bits (0x2, 0x8) en de UV-generatie-uitzondering (bit 0) zijn alleen uit de loader afgeleid; rendergedrag niet gecontroleerd.
7. Het eerste dword van elk trajectoriepunt is in alle data 0 en wordt door de padlengteberekening overgeslagen; mogelijk tijd/snelheid.
8. Enkele levels hebben meer world_volumes/collisions in de code-header dan in het `.ins` (bijv. W2B 215 vs 211); de ontbrekende worden mogelijk elders (Woody.rck/`.gel`?) of niet gedefinieerd.
