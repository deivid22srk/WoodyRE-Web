# Levelformaten .tex / .col / .vis / .lit

Afgeleid uit de disassembly van `Woody.exe` (`out/disasm_full.txt`). Alle waarden zijn
little-endian; `u32` = 32-bit unsigned, `i32` = signed, `f32` = IEEE single. Parser:
`tools/levelparse.py` (valideert alle 28 levels byte-exact, zie onderaan).

Context: de level-loader `0x426bd0` (level-object 0x78 bytes, globaal in `0x50944c` en
`0x4c4c0c`; het "wereld"-object en het level-object zijn hetzelfde object) laadt in deze
volgorde `.gel` (via `0x407ae0`), `.tex` (inline), `.ins` (`0x427290`), `.col` (`0x4271e0`).
Daarna laadt `0x4043d0` nog `.vis` (`0x408260`) en `.lit` (`0x40ac30`). Stream-klasse:
`[vt+8]` = read(buf, nbytes), `0x43fdb0` = fread(buf, size, count), `0x43fd90` =
fread(buf, 1, n) met returnwaarde (gebruikt voor de optionele `.lit`-trailer),
`[vt+0x18]` = totale bestandsgrootte (gebruikt door de `.col`-loader voor de allocatie).

Relevante tellingen uit `.gel` (loader `0x407ae0`, veldnamen = offsets in het wereld-object):

| Veld | Inhoud | Gebruikt door |
|---|---|---|
| `world+0x0c/+0x10` | aantal faces / array van face-polygonen | `.lit` (face-indices) |
| `world+0x04/+0x08` | aantal vertices / array (stride 0x30) | `.lit` polygonen |
| `world+0x18/+0x1c` | aantal "sector"-objecten (bladeren van de ruimtelijke boom, 0x4c bytes, vtable `0x4a94f4`) | `.col` (een record per sector) |
| `world+0x14` | `objects-1` splitsingsvlakken (16 B) van die boom | – |
| `world+0x20/+0x24` | aantal zichtbaarheidscellen / array (zelfde 0x4c-klasse) | `.vis`, `.lit`-trailer |
| `world+0x40` | objecttabel: pointers naar de `.ins`-objecten, index = object-id | `.col`, `.lit` (`object_id`) |

De `.gel`-loop die `tools/levelparse.py::parse_gel_counts` implementeert klopt byte-exact op
alle levels.

---

## 1. `.tex` – textures + materialen

Loader: inline in `0x426bd0`, bereik `0x426c90`–`0x427066`. Pixeldata wordt per frame door
`0x47f7f0(this=texture, stream, w, h, 3, alpha, 0)` → `0x47fa60(this=tex+0x70, stream, w, h,
mipmaps=3, alpha, srcformat=0, tex)` gelezen: `w*h` keer 2 bytes, per pixel via `0x47f090(v, 0)`
(case 0 = **RGB565** → ARGB8888) en terug naar het schermformaat via `0x47f170`. Er staan
**geen mipmaps** in het bestand (de engine genereert er 3 met een 2×2-boxfilter). Als de
alpha-vlag aan staat wordt elke pixel met `(argb & 0xF0F0F0) == 0xF000F0` (magenta) volledig
transparant, alle andere krijgen alpha 0xFF.

### Bestandslayout

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | 4 | u32 | `group_count` – aantal texture-groepen (`[esp+0x1c]`) |
| 0x04 | 4 | u32 | `texture_count` – totaal aantal frames; engine alloceert `(texture_count+19)` texture-structs van 0x74 bytes (19 extra: 2×32×32, 1 speciale, 16 voor RCK-plaatjes) |
| 0x08 | … | | `group_count` × **groep**: |
| +0x00 | 4 | u32 | breedte (→ `tex+0x38`) – in de data 16/32/64/128/256 |
| +0x04 | 4 | u32 | hoogte (→ `tex+0x3c`), altijd = breedte |
| +0x08 | 4 | u32 | vlaggen (→ `tex+0x44`): bit 0 = colour-key alpha (wordt aan `0x47f7f0` meegegeven en in bit 0 van `tex+0x44` gezet); bits 1–2 (`&6`) worden in de `.ins`-loader (`0x42801a`) `<<4` in de face-flags gekopieerd; byte 1 (`tex+0x45`) `== 2` wordt in `0x42acea` getest (aparte rendermodus); byte 2 = intensiteit/alpha (0xFF = ondoorzichtig; 0x7f/0x99/0xb2/0xcc/0xe5 komen alleen samen met bit 1 voor); byte 3 (`tex+0x47`) = **grondtype** van de speler (`m_nGroundType`, `0x46295f`). Bit 1 = geblend renderen: de gloeitexturen (wit op zwart, bv. W1A g77 `0x00cc0002`) en de ventilatorwaas (g66 `0x007f0002`) kloppen met het origineel als ze additief (`ONE/ONE`, geen z-write) met intensiteit byte 2 worden getekend; `0x428ee0` kent drie doorgangen (ZERO/ONE, SRCALPHA/INVSRCALPHA, ONE/ONE), welke lijst bij welk bit hoort is nog niet gevolgd |
| +0x0c | 4 | f32 | `scroll_u`? (→ `tex+0x48`); vrijwel altijd 0.0, 3 groepen 0.05 |
| +0x10 | 4 | f32 | `scroll_v`? (→ `tex+0x4c`); idem |
| +0x14 | 4 | f32 | animatieduur (→ `tex+0x54`); in `0x47f290` als `duur * snelheid` gebruikt om het frame uit de tijd te berekenen (modes 1–6: eenmalig, omgekeerd, ping-pong, herhalend …). Meestal 1.0; bij niet-geanimeerde groepen soms rest-integers (32/64/128) |
| +0x18 | 4 | u32 | `frame_count` (→ `tex+0x58`); 1, 2, 4, 5, 8 of 17 |
| +0x1c | 4 | u32 | dword 8 (→ `tex+0x5c`); 0, 1 of ~9647 – geen gebruiker gevonden |
| +0x20 | 4 | u32 | dword 9 (→ `tex+0x50`); 1.0f, 0 of pointer-achtige waarden (0x0141xxxx) – geen gebruiker gevonden, vermoedelijk editor-restdata |
| +0x24 | `frame_count × w×h×2` | u16[] | frames, **RGB565**, rij voor rij, geen padding |
| … | 4 | u32 | `material_count` (na alle groepen) |
| … | `material_count × 0x34` | | **materiaal** (→ `level+0x5c`, records van 0x24 bytes in geheugen): |
| +0x00 | 4 | u32 | groepindex; engine slaat de pointer naar het eerste frame van die groep op in `mat+0x20` |
| +0x04 | 48 | f32[12] | `f[0..11]`: 4 rijen van 3 floats (3×3-matrix + translatierij, wereld→UV). De loader (`0x426fc2`–`0x427058`) bewaart alleen kolom 0 (`f0,f3,f6,f9` → `mat+0x00..0x0c`) en kolom 1 (`f1,f4,f7,f10` → `mat+0x10..0x1c`); kolom 2 (`f2,f5,f8,f11`) wordt weggegooid |

**UV-berekening** (`.ins`-loader, `0x4280c2`–`0x428110`): per face staat in `.ins` een u16
materiaalindex (`face+0x00`, `0x42800a`) en een u16 vertex-aantal (`face+0x02`) gevolgd door
u16 vertex-indices (vanaf `face+0x18`) in de vertex-array van het bijbehorende mesh (stride
0x28, positie op +0/+4/+8). Voor elke vertex `(x,y,z)` in **objectruimte**:

```
u = f0*x + f3*y + f6*z + f9        (mat+0x00..0x0c)
v = f1*x + f4*y + f7*z + f10       (mat+0x10..0x1c)
```

De uitkomst is direct de texturecoördinaat in herhalingen (1.0 = één keer de texture), er
wordt niet door de texturegrootte gedeeld. De face-vlag bit 0 of `face+1 & 0x80` (`0x427ff8`)
slaat de materiaalkoppeling over. `tools/levelparse.py::material_uv(material, x, y, z)`
implementeert dit.

Statische groep-test in de loader (`0x426e5a`): `tex+0x00 = 0` als `frame_count == 1` én
beide floats `+0x0c/+0x10 == 0.0`, anders `1` ("heeft update nodig"). Daarom is de duiding
van `+0x0c/+0x10` als UV-scrollsnelheid aannemelijk maar niet door rendercode bevestigd.

Faces in `.ins` verwijzen met een u16 naar deze materiaaltabel (`level+0x5c + idx*0x24`);
de renderer (`0x43b3f0`) kiest per face het frame `min(frame, frame_count-1)` uit de groep.

### Texture-struct (0x74 bytes, ctor `0x47f250`)

| Offset | Betekenis |
|---|---|
| +0x00 | 0 = statisch, 1 = geanimeerd/scrollend |
| +0x04 | -1 (ctor) |
| +0x08..+0x34 | runtime (genuld) |
| +0x38 / +0x3c | breedte / hoogte |
| +0x44 | vlaggen (header dword 3, bit 0 door loader overschreven met alpha-arg) |
| +0x48 / +0x4c | header float 4 / 5 |
| +0x50 | header dword 9 |
| +0x54 | animatieduur (header dword 6) |
| +0x58 | frame_count |
| +0x5c | header dword 8 |
| +0x60 | this (ctor) |
| +0x6c | -1 (ctor) |
| +0x70 | `IDirectDrawSurface7*` (gevuld door `0x47fa60`) |

Frames van één groep liggen aaneengesloten in de array (stride 0x74); de groeptabel op de
stack (`esp+0x3a0`) bevat de pointer naar frame 0 van elke groep.

### Onzeker
- Betekenis van dword 8 (`+0x5c`) en dword 9 (`+0x50`): geen lezer in de code gevonden.
- `+0x0c/+0x10` als UV-scroll: alleen indirect (statisch-test) onderbouwd.
- Vlaggen-bytes 1–3 slechts gedeeltelijk geduid.

---

## 2. `.col` – objectlijsten per sector

Loader `0x4271e0(this=level, stream)`. Alloceert één pool ter grootte van het bestand
(`[vt+0x18]/4` dwords + marge) en vult voor elk object `i` in `level+0x1c[i]` (aantal
`level+0x18`, de sector-objecten uit `.gel`) `obj+0x40` = aantal en `obj+0x44` = pointer in de
pool.

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | … | | `world+0x18` × **sector-record** (volgorde = `.gel` objectvolgorde): |
| +0x00 | 4 | u32 | `n` (→ `sector+0x40`) |
| +0x04 | `4n` | u32[] | verwijzingen (→ `sector+0x44`): `(mask << 16) \| object_index` |

Gebruik (`0x4071ae`, `0x407502`, `0x42aa0b`, `0x434771`, `0x497a9c`, …): `object_index =
v & 0xFFFF` indexeert de objecttabel `world+0x40` (de `.ins`-objecten; `[obj+8] & 0x1f` =
objecttype), de volledige u32 wordt als argument aan de collision-methode `vtable+0x20` van dat
object doorgegeven; `0x407282` maakt zelf zulke waarden met `id | 0xFFFF0000`. De hoge 16 bits
zijn dus een masker (0xFFFF = "alles"; in de data ook 0x7FFF, 0x03C0, 0x0780, …), vermoedelijk
welke sub-delen van het object in deze sector liggen.

Validatie: recordaantal == `.gel` objects op alle levels; `object_index` < aantal
`.ins`-objecten (1e u32 van `.ins`) op alle levels.

### Onzeker
- Exacte semantiek van het 16-bit masker.

---

## 3. `.vis` – zichtbaarheid per cel

Loader `0x408260(this=world, path)`. `world+0x28` = array van `world+0x20` pointers; per cel
wordt `(entry_count + total_pairs)*8 + 4` bytes gealloceerd en gevuld als
`{u32 entry_count, entries…}`.

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | … | | `world+0x20` × **cel**: |
| +0x00 | 4 | u32 | `entry_count` (A) – in de data 1 of 2 |
| +0x04 | 4 | u32 | `total_pairs` (B) = som van alle `pair_count` (alleen voor allocatie) |
| +0x08 | … | | A × **entry**: |
| ++0x00 | 4 | u32 | `id` – in de data 0 of 1 |
| ++0x04 | 4 | u32 | `pair_count` |
| ++0x08 | `8·pair_count` | (u32,u32)[] | `(cel-index, vlag)`; cel-index < `world+0x20`, vlag 0/1 |

Interpretatie: per cel één of twee lijsten van vanuit die cel zichtbare cellen (PVS) met een
vlag per cel. Validatie: celaantal == `.gel` cells op alle levels.

### Onzeker
- Buiten loader en destructor (`0x407a80`) is geen code gevonden die `world+0x28` leest; de
  betekenis van `id` (0/1) en de vlag is dus niet uit code bevestigd.

---

## 4. `.lit` – lichtsysteem (voorberekende verlichting en schaduw-BSP's)

Loader `0x40ac30(this=lightsys (0x2c bytes), path, 0x10, 0x400)`, object in globaal
`0x4c4cac`. Bij een verkeerde magic blijft `0x4c4cac` 0 en meldt `0x404536`
"Can't load lightsystem : please rebuild lights !!!".

| Offset | Grootte | Type | Betekenis |
|---|---|---|---|
| 0x00 | 4 | u32 | magic `0x20010822` (datum 22-08-2001) |
| 0x04 | 4 | u32 | `light_count` (→ `lightsys+0x00`; lichten zijn 0x40-byte objecten, ctor `0x40b450`, array `lightsys+0x04`) |
| 0x08 | … | | `light_count` × **licht**: |
| +0x00 | 4 | u32 | `object_id` (→ `light+0x04`); `id & 0xFFFFFF` = index in `world+0x40`, hoogste byte = type (altijd 1). Als `world+0x40` bestaat: `world+0x40[idx] = light` |
| +0x04 | 4 | u32 | → `light+0x28` (2, één keer 3) |
| +0x08 | 12 | f32[3] | positie (→ `light+0x0c..0x14`) |
| +0x14 | 12 | f32[3] | kleur R,G,B als 0..255 (→ `light+0x30..0x38`) |
| +0x20 | 4 | f32 | bereik/radius (→ `light+0x2c`; bv. 6000, 50000) |
| | | | daarna sub-struct **S** (`light+0x3c`, 0x28 bytes, malloc): |
| +0x24 | 4 | u32 | `nA` (→ `S+0x08`) |
| | `4·nA` | u32[] | lijst A (→ `S+0x0c`): oplopend gesorteerde **face-indices** (`world+0x10`), de door dit licht beschenen faces (`0x497c2c` loopt ze af en test het facevlak) |
| | 4 | u32 | `nB` (→ `S+0x10`) |
| | `4·nB` | u32[] | lijst B (→ `S+0x14`): oplopend gesorteerde face-indices (geen deelverzameling van A) |
| | 4 | u32 | `nC` (→ `S+0x18`) |
| | 4 | u32 | `total_indices` = som van alle `n` hieronder (voor de pool-allocatie `(7·nC + total)*4`) |
| | … | | nC × **polygoon** (zelfde formaat als `.gel`-faces): `u32 n`, `f32[4]` vlak (nx,ny,nz,d) → `P+0x0c`, `u32 face` → `P+0x08`, `i32[n]` vertex-indices → `P+0x1c` (positief = `.gel`-vertex, negatief = extra vertex uit clipping; `P+0x04` wordt op -1 gezet) |
| | 4 | u32 | `nD` (→ `S+0x00`) |
| | `16·nD` | u32[4] | **bereik per cel** (→ `S+0x04`, records van 0x1c): `{cel, nA_i, nB_i, nC_i}`; de engine berekent cumulatieve offsets in `+0x08/+0x10/+0x18` – de lijsten A/B/C zijn dus per cel gegroepeerd |
| | 4 | u32 | `nE` (aantal wordt niet bewaard) |
| | `12·nE` | u32[3] | **BSP-knopen** (→ `S+0x20`): `{vlakindex, front, back}`; kind = `(v & 0xF)`: 0 → knoop `v>>4`, 1 → blad met face `v>>4`, anders leeg. `0x40b540(S, punt)` doorloopt de boom (vlak·p + d ≥ 0 → front) en geeft de face terug |
| | 4 | u32 | `nF` (aantal wordt niet bewaard) |
| | `16·nF` | f32[4] | **BSP-vlakken** (→ `S+0x24`), genormaliseerde (nx,ny,nz,d) |
| … | 4 | u32 | `probe_count` (→ `lightsys+0x08`) |
| … | `16·probe_count` | | **probe** (→ `lightsys+0x0c`, records van 0x30): `f32[3]` positie, `u32` kleur 0x00RRGGBB (→ `+0x28`); `+0x2c` = -1 (runtime). Gebruikt in `0x42c320`/`0x498890` (afstand tot een punt) |
| … | 4 | u32 | *(optioneel, via `0x43fd90`)* `total` – aantal dwords van de trailer |
| … | `4·total` | u32[] | trailer: `world+0x20` × `{u32 n, u32 light_index[n]}` – lichten per cel (→ `lightsys+0x10`). Ontbreekt de trailer, dan `lightsys+0x10 = 0` |

Overige lightsys-velden (`+0x14..+0x28`) worden na het laden gealloceerd (afhankelijk van de
argumenten 0x10/0x400 en `world+0x0c`).

### Onzeker
- Onderscheid tussen lijst A en B (beiden face-indices); B is mogelijk de schaduwwerpende
  faces.
- Betekenis van `light+0x28` (2/3) en van het typebyte in `object_id`.
- Wat de probes (0x30-records) precies zijn (lichtsamples / ambient-punten).
- Wat het `cel`-veld in de bereik-records exact is (waarden < `world+0x20`, consistent met cel).

---

## Validatie

`python tools/levelparse.py` parseert alle vier bestanden van alle 28 levels, controleert dat
elke parser het bestand volledig consumeert en kruist met `.gel`: `.col`-records ==
objecten, `.vis`-cellen == cellen, `.lit`-trailer-lijsten == cellen, alle face-indices <
faceaantal. Resultaat: 28/28 OK.

`python tools/levelparse.py --dump-tex W1A out/tex_W1A` schrijft van elke groep het eerste
frame als PNG (RGBA; magenta-key toegepast als vlag-bit 0 gezet is).
