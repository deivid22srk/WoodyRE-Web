# Bestandsformaat `.gel` (levelgeometrie)

Afgeleid uit de loader op `0x407ae0` in Woody.exe (aangeroepen vanuit de
levellader `0x426bd0`, die daarna `.tex`, `.ins` en `.col` laadt; vervolgens
laadt `0x408260` de `.vis` en `0x40ac30` de `.lit`). Alle waarden little-endian,
floats IEEE single. Er is geen magic en geen versienummer. Parser:
`tools/gelparse.py` (consumeert alle 28 bestanden in `extract/Data` exact tot
het einde en controleert de interne consistentie).

Het bestand bestaat uit zeven secties die strikt na elkaar staan:

| # | Sectie | Engine-veld (gel-object, `ecx` van `0x407ae0`) |
|---|---|---|
| 1 | Polygonen (driehoeken) | `+0x0c` aantal, `+0x10` `ptr[]` naar records |
| 2 | Portaalpolygonen | `+0x2c` aantal, `+0x30` `ptr[]` (NULL als aantal 0) |
| 3 | Groepen/zones + portaallijsten | `+0x34` aantal, `+0x38` array (0x18 B/stuk) |
| 4 | Vertices | `+0x04` aantal, `+0x08` array (0x30 B/stuk) |
| 5 | kd-bladcellen | `+0x18` aantal, `+0x1c` `ptr[]` naar objecten (0x4c B) |
| 6 | kd-boom | `+0x14` array (16 B/knoop); aantal wordt niet bewaard |
| 7 | Sectoren | `+0x20` aantal, `+0x24` `ptr[]` naar objecten (0x4c B) |

Het gel-object is de basisklasse (vtable `0x4a94f0`) van het levelobject
(0x78 B, vtable `0x4aa220`) en staat in de globale `0x4c4c0c`. De velden
`+0x28` (per-sector `.vis`-lijsten), `+0x3c/+0x40` (aantal/array van
dynamische wereldobjecten) en `+0x44` worden door andere laders gevuld en
staan niet in `.gel`.

## 1. Polygonen

```
u32 count            -> gel+0x0c
u32 total_indices    (= som van nverts; alleen gebruikt voor de malloc-grootte)
count x record:
```

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | 4 | u32 | `nverts` (in alle 28 levels altijd 3) |
| 0x04 | 4 | u32 | `material`: bits 0..14 = index in de materiaaltabel van `<LVL>.tex` (`level+0x5c`, 0x24 B/stuk: 8 floats uv-projectie + texture-pointer op +0x20); bit 15 (`test ah,0x80`) = geen materiaal / niet renderen (bv. 0x8000 in Blackbox, 0xDAD6 in W3B/K3A) |
| 0x08 | 16 | 4 x f32 | vlak `(nx, ny, nz, d)`; test in `0x40a0c0`: `nx*x + ny*y + nz*z + d` |
| 0x18 | 4·n | u32[n] | vertexindices (in sectie 4) |

Engine-record (0x1c + 4·n bytes, `0x407bee`): `+0` nverts, `+4` framestempel
(runtime, init -1 door `0x408420`; gezet door renderer `0x42ac10` en collision
`0x407000` om dubbele verwerking te vermijden), `+8` material, `+0xc` vlak,
`+0x1c` indices. `gel_render.py` las de records 16 bytes verschoven: zijn
"twee extra dwords" zijn `nverts` en `material` van het volgende record.

Negatieve vertexindices komen in de bestanden niet voor; de renderer
(`0x42c38c`) interpreteert ze wel als dynamische vertices uit het
`.lit`-object.

## 2. Portaalpolygonen

```
u32 count            -> gel+0x2c   (mag 0 zijn; dan volgt toch nog u32 total_indices)
u32 total_indices
count x record:
```

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | 4 | u32 | `nverts` (altijd 3) |
| 0x04 | 16 | 4 x f32 | vlak |
| 0x14 | 4·n | u32[n] | vertexindices |

Zelfde engine-record als sectie 1, maar `material` wordt op -1 gezet
(`0x407cea`). Het zijn asvlak-uitgelijnde quads (twee driehoeken, vlakken
±x/±z) die als portalen tussen groepen dienen; ze worden door sectie 3
geadresseerd. 22 van de 28 levels hebben er 0.

## 3. Groepen (zones) en portaallijsten

```
u32 count                         -> gel+0x34
count x u32 end                   exclusieve eindindex in sectie 1
count x { u32 k ; k x { u32 portal ; u32 group } }
```

Groep *i* bevat de polygonen `[end[i-1], end[i])` (met `end[-1] = 0`); de
laatste `end` is gelijk aan het aantal polygonen. Per groep volgt een lijst
van `k` paren `(portaalindex in sectie 2, index van de buurgroep)`; in alle
levels geldt `portal < count_2` en `group < count_3`.

Engine-record (0x18 B, `0x407d52`/`0x407d95`): `+0` framestempel (runtime,
init -1 door `0x40ab00`; "groep zichtbaar in dit frame", `0x42aa9b`),
`+4` eerste polygoon, `+8` laatste polygoon (`end-1`), `+0xc` k,
`+0x10` `u32* portal`, `+0x14` `u32* group`.

Gebruik: de `.vis`-lijsten (per sector, per vloerpolygoon) leveren paren
`(sector, groep)`; de renderer `0x42a980` markeert die groepen en `0x42ac10`
stempelt alle polygonen in het bereik van elke zichtbare groep. `0x40a26a`
zoekt de groep van een polygoonindex. Een koppeling van `.ins`-instanties naar
deze groepen is in de `.ins`-lader (`0x427290`) niet gevonden; die gebruikt
alleen `0x407790` (sector van een punt).

## 4. Vertices

```
u32 count            -> gel+0x04
count x 16 B:  f32 x, f32 y, f32 z, u32 colour
```

`colour` heeft in alle levels het hoogste byte 0. De bytevolgorde is **R,G,B,0**
(dus `0x00BBGGRR` als little-endian u32, géén D3DCOLOR) en **128 = neutraal**: de
renderer moduleert de textuur met 2× de vertexkleur (D3D `MODULATE2X`; `0x808080`
is de meest voorkomende waarde, `0x00fefe` = geel, `0x0000fe` = rood). Gecontroleerd
aan een screenshot van het origineel (W1A: gele randen, rood startplatform).
Engine-record
0x30 B: `+0` xyz, `+0x28` colour, `+0x2c` framestempel (runtime, init -1;
transformatiecache), de rest wordt bij het laden niet geschreven.
y is de verticale as (zie `0x40a0c0`, dat de vloer via `ny > 0` zoekt).

## 5. kd-bladcellen (en 7. sectoren: identiek record)

```
u32 count            -> gel+0x18 (cellen) / gel+0x20 (sectoren)
count x record:
```

| Offset | Grootte | Type | Betekenis | Engine-offset (object 0x4c B, vtable `0x4a94f4`) |
|---|---|---|---|---|
| 0x00 | 4 | u32 | `npoly` | `+0x08` |
| 0x04 | 4·npoly | u32[] | polygoonindices (sectie 1) die deze cel snijden | `+0x0c` (malloc) |
| .. | 24 | 6 x f32 | bbox `xmin, xmax, ymin, ymax, zmin, zmax` (volgorde bewezen door `0x406e50`) | `+0x28..+0x3c` |
| .. | 24 | 6 x i32 | buurlink per bbox-vlak, volgorde `-x, +x, -y, +y, -z, +z` | `+0x10..+0x24` |
| .. | 4 | u32 | `nnodes` | (niet bewaard) |
| .. | 16·nnodes | knoop[] | lokale kd-subbomen voor de buurlinks (zie 6 voor de knoopindeling) | `+0x48` (malloc, NULL als 0) |

Buurlink-codering (`0x40a0c0`, dat link 2 = `-y` volgt om de vloer te vinden):

* `0x80000000` (INT_MIN): geen buur (wereldrand).
* `< 0`: precies één buurcel, index `~link`.
* `>= 0`: wortel van een subboom in de lokale knooparray; de engine roept
  `0x40ab60(&nodes[link], punt)` aan. **Kindindices in zo'n subboom zijn
  relatief t.o.v. de wortel** (`ecx*16 + edi`), dus kind *k* = `nodes[link+k]`.
  Elke subboom is een aaneengesloten blok; alle knopen van een cel worden door
  precies één van de zes links gebruikt. De bladeren `~c` zijn celindices in
  dezelfde array (cellen resp. sectoren). Gecontroleerd: elke zo gevonden buur
  grenst exact aan het betreffende bbox-vlak (0 fouten in alle levels).

Het hoge 16-bits woord van het knooptype is in deze lokale bomen 0 of een
rommelwaarde (per level één constante, bv. 1134 in W1A) en wordt nooit gelezen.

Runtime-velden van het object: `+0x04` framestempel (init -1 door de virtuele
init `0x406e40`), `+0x40/+0x44` aantal/array van in de cel geregistreerde
dynamische objecten (indices in `gel+0x40`, laag 16 bits), bij sectoren is
`+0x44` de kop van de gelinkte lijst van entiteiten in de sector (`0x407790`
voegt in, `0x407850` verwijdert; entiteit `+0x1c` = sector, `+0x18` =
vloerpolygoon, `+0x24` = volgende).

Voor sectoren geldt bovendien: de bbox is exact de vereniging van de bboxen
van de bladcellen onder de sectorwortel in sectie 6, en de polygoonlijst is
gelijk aan (in enkele gevallen een superset van) de vereniging van de
cel-polygoonlijsten.

## 6. kd-boom

```
u32 count            (in alle levels = aantal cellen - 1)
count x 16 B:
```

| Offset | Type | Betekenis |
|---|---|---|
| 0x00 | i32 | laag 16 bits (tekenuitgebreid, `0x40ab10`): as 0=x, 1=y, 2=z; hoog 16 bits (`sar 16`): -1 voor gewone knoop, anders **sectorindex** (deze knoop is de wortel van de subboom van die sector) |
| 0x04 | f32 | `d`; test `punt[as] + d <= 0` (dus `d = -splitwaarde`) |
| 0x08 | i32 | kind als test waar; `>= 0` knoopindex (absoluut, in deze array), `< 0` blad = cel `~kind` |
| 0x0c | i32 | kind als test onwaar; zelfde codering |

Wortel is knoop 0. `0x408180` daalt af tot een blad en geeft de celindex
(sectie 5) terug; `0x4081c0` daalt af tot de eerste knoop met hoog woord
>= 0 en geeft dat woord (sectorindex, sectie 7) terug. Elke sector heeft
precies één wortelknoop. Gecontroleerd: het middelpunt van elke cel-bbox daalt
af naar die cel zelf (100 %).

## Overzicht van de bestandsvolgorde

```
[1] u32 npoly, u32 nidx, npoly x { u32 n, u32 material, f32 plane[4], u32 idx[n] }
[2] u32 nport, u32 nidx, nport x { u32 n, f32 plane[4], u32 idx[n] }
[3] u32 ngroup, u32 end[ngroup], ngroup x { u32 k, k x { u32 portal, u32 group } }
[4] u32 nvert, nvert x { f32 x, y, z, u32 colour }
[5] u32 ncell, ncell x { u32 np, u32 poly[np], f32 bbox[6], i32 link[6], u32 nn, node[nn] }
[6] u32 nnode, nnode x { i32 type, f32 d, i32 child_le, i32 child_gt }
[7] u32 nsect, nsect x { zelfde record als [5] }
```

## Validatie

`python tools/gelparse.py` parseert alle 28 `.gel`-bestanden; elk wordt exact
tot het laatste byte geconsumeerd, en de volgende invarianten houden overal:
indices binnen bereik, `end[-1] == npoly`, portaalparen binnen bereik,
sectorwortels = precies 0..nsect-1, buurbomen zonder cycli en zonder
ongebruikte knopen, elke buur grenst aan het juiste vlak.

## Nog onzeker

* Betekenis van bit 15 van `material`: zeker "niet via de materiaaltabel
  renderen" (`0x42acd6`, `0x462948`), maar of dit "onzichtbaar/alleen
  collision" of iets anders betekent is niet nagegaan; overige bits >15 komen
  niet voor.
* Waarom sommige sectoren méér polygonen bevatten dan hun bladcellen samen
  (2 van 128 in W1A, 1 van 193 in W3B).
* Het hoge woord van het knooptype in de lokale buurbomen (rommel of
  betekenisvol voor de tool; de engine leest het niet).
* Of `.ins`-instanties ergens naar groepen (sectie 3) verwijzen; in de
  `.ins`-lader is dat niet gevonden, de `.vis` verwijst er wel naar.
