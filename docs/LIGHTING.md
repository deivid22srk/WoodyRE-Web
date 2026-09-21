# Verlichting en schaduw (`.lit`) – hoe het origineel het tekent

Statische analyse van `game/Woody.exe` (niets is op de draaiende game geverifieerd).
Bestandsformaat: zie `FORMAT_TEX_COL_VIS_LIT.md` §4; dit document beschrijft wat de engine er
per frame mee doet en corrigeert dat formaatdocument op drie punten (§2).
Data-controle: `python tools/litparse.py W1A` (per-licht statistiek + 5 controles, §6).

**Kernidee.** De engine gebruikt een *multipass-lightmap in de framebuffer*:

```
pixel = 2 · textuur · vertexkleur · ( AMB + Σ_lichten  C/255 · max(0, 1 − |P − L| / R) )      AMB = 76/255 ≈ 0.30
```

waarbij de som alleen loopt over lichten die het punt **zien**. Zichtbaarheid is vooraf
uitgerekend: lijst A = faces die het licht helemaal ziet, lijst C = de belichte *stukken* van
faces die gedeeltelijk in de schaduw liggen. Schaduw is dus geen donkere polygoon; schaduw is
de plek waar de additieve lichtpolygoon **ontbreekt**. De scherpe polygonale randen van de
platformschaduwen in W1A zijn de randen van de C-polygonen. Faces die door geen enkel licht
worden geraakt krijgen in één pas `textuur · vertexkleur · 0.6` (= 2·AMB, dus naadloos).
Er is **geen N·L** op de wereld, alleen lineaire afstandsval.

## Recept (in volgorde)

1. **Laden.** Per licht: positie `L`, kleur `C` (floats 0..255), bereik `R`, lijsten A, B, C,
   cel-ranges, BSP. Laad de tabel die in het formaatdocument "probes" heet als
   **extra-vertextabel**: index `i < 0` in een C-polygoon = record `−i−1` (positie = eerste
   3 floats; de u32 erachter is ongebruikt). Laad ook de trailer (lichten per cel) voor stap 5.
2. **Face-vlaggen per frame.** `lit[face] = 0`; voor elk licht, voor elke cel-range waarvan
   de cel zichtbaar is: alle faces van A en B → `lit[face] = 2`. (Een port zonder
   cel-zichtbaarheid mag dit één keer bij het laden doen voor alle lichten.)
3. **Wereld, onbelichte faces** (`lit == 0`): één pas, `textuur × vertexkleur × 0.6`,
   texenv MODULATE **1×** (niet 2×), opaak.
4. **Wereld, belichte faces** (`lit == 2`), vier stappen in deze volgorde (alles van alle
   faces per stap, want de framebuffer is de accumulator):
   1. *Ambient-vulling*: face zonder textuur, egale kleur `0x4C4C4C` (76,76,76), opaak,
      z-write aan.
   2. *Lichtpas*: `glDepthMask(0)`, `glEnable(GL_BLEND)`, `glBlendFunc(GL_ONE, GL_ONE)`,
      wrap **CLAMP**, MODULATE 1×, dieptetest LEQUAL. Per licht, per zichtbare cel-range:
      elke face van **A** (hele face) en elke polygoon van **C**. Per polygoon met vlak
      `(n,d)`:
      - `dist = n·L + d`; als `|dist| ≥ R` → overslaan; backface (camera achter het vlak) → overslaan;
      - `k = 1 − |dist|/R`; kleur van *alle* vertices = `((int)(C.r·k), (int)(C.g·k), (int)(C.b·k))`;
      - textuur = radiale gloed nr. `i = 15 − round(k · 15.49)` (16 texturen 32×32, §1.4);
      - UV: `F = L − n·dist`, `r = sqrt(R² − dist²)`, `s = 0.5/r`,
        `U = normalize(V2 − F)` (V2 = **derde** vertex van de polygoon), `W = n × U`,
        `u = 0.5 + s·(P − F)·W`, `v = 0.5 + s·(P − F)·U`.
      - Netto (textuur × kleur) is dat exact `C/255 · max(0, 1 − |P − L|/R)`; de keuze van U
        doet er niet toe want de textuur is rotatiesymmetrisch. Een port mag dus net zo goed
        één 2D-gloedtextuur per `i` genereren of de formule per pixel/vertex uitrekenen.
   3. *Geworpen schaduwen van instanties* (optioneel, §4): opake polygonen (blend uit,
      z-write uit) in kleur `0x4C4C4C` die de opgetelde lichten weer overschrijven.
   4. *Textuurpas*: dezelfde faces nogmaals met hun eigen textuur en vertexkleur (1.0×),
      `glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR)` (= 2 · src · dst), z-write uit, dieptetest
      LEQUAL/EQUAL.
   Alles wat transparant/additief is (water, effecten, modellen met alfa) komt hierna.
5. **Modellen (Woody, vijanden, instanties)**, §3: kies per instantie één licht uit de
   lichtlijst van zijn cel; per modeldeel een gladgestreken lichtvector
   `Ldir = 0.85·Ldir + 0.15·normalize(L − p)·(1 − |L − p|/R)` als het deel door dat licht
   gezien wordt (BSP-query), anders alleen `Ldir *= 0.85`; vertexkleur
   `= vcol·0.3 + max(0, N·Ldir)·C` (schaal 0..255), getekend met MODULATE **2×**. In de
   schaduw zakt een figuur dus in ~10 frames naar `0.6·vcol`.
   **Uitzondering: een vlak van een geblende textuurgroep (vlagbit 1) krijgt géén belichting.** `0x428020`
   kopieert de groepsvlaggen 1-2 bij het laden naar de polygoonvlaggen 0x20/0x40, `0x43d7b9` test `0x60` en
   zet `[0x5ac8d8] = 1`, en `0x43d91d` springt daarmee over de belichte RGB op `v+0x24..0x2c` heen: er wordt
   `0x00iiiiii` geschreven met `i = (int)(alpha · 0.5)` (`0x43d9a4`) en `alpha = (1 − inst+0x6c) · 255`
   (`0x43b504`). Voor een instantie die niet uitvervaagt is dat `i = 128`, onder MODULATE 2× dus precies
   `1.0 × textuur`. Neonreclame, het rode kruis / de groene pijl naast een deur en de lichtbalken erboven
   zijn daarom **altijd even fel**, waar ze ook staan. De intensiteitsbyte `tex+0x46` wordt nergens gelezen.
   Alleen modelvlakken doen dit: `.gel`-wereldpolygonen krijgen de vlaggen nooit (`0x42801a` zit in de
   `.ins`-lader) en geskinde driehoeken evenmin (`0x43e107` zet `[0x5ac8d8] = 0`). In de data van alle 28
   levels staat geen enkele wereldpolygoon of geskinde driehoek in een geblende groep.
6. Er is **geen blob-schaduw** (§4); de schaduw onder Woody is echte, vanuit het licht
   geprojecteerde modelgeometrie, en alleen als de detailoptie aan staat (§5).

## 1. Wereld

### 1.1 Frame-volgorde (`0x401ab0`)

| Adres | Aanroep | Wat |
|---|---|---|
| `0x401922` | `0x4843e0(0.3)` | per frame: `renderer+0x1ac = 0.6` (factor onbelichte faces), `renderer+0x1b0 = 0x4C4C4C` (`(int)(0.3·256)` = 76 in R, G en B) = **AMB**. Renderer = `[0x5e86ac]` = `[0x509adc]` (zelfde object, `0x42a451`/`0x48407e`) |
| `0x401d78` | `0x42b400` | objecten updaten; voor zichtbare licht-objecten (soort 2 = een `.lit`-lichtrecord, `0x40ae60`) `0x474a90`: lensflare met zichtlijntest `0x497ed0` – geen invloed op de wereldbelichting. **Onbereikbaar in het uitgeleverde spel**: `0x474a90` loopt alleen over de tabel `0x5e8428` (64 plaatsen), en die wordt uitsluitend gevuld door de handler van bericht **1510** (`0x46cf02`) – dat bericht komt in geen van de 28 levelscripts voor (wel alle andere ids 1500..1511). De tabel blijft dus leeg en de functie keert altijd meteen terug op `0x474abe`. Niet porten |
| `0x401d85` | `0x42abc0` | `lightsys+0x28[face] = 0` voor alle faces van de zichtbare sectoren |
| `0x401d91` | `0x42b380` | instanties tekenen; schaduwontvangende faces krijgen hier ook vlag 2 (`0x42eb73`, `0x42ecbc`) |
| `0x401d99` | `0x42b4e0` | **lichtpas** (onvoorwaardelijk, niet afhankelijk van een optie) |
| `0x401da1` | `0x42ac10` | basis-faces via `0x42b6c0(face)`; slaat vlag `== 1` over (`0x42ad0b`), maar er is geen schrijver van 1 gevonden (alleen 0 en 2) |
| `0x401756` | `0x4293f0` | flush van alle emmers met de renderstates (§1.5) |

### 1.2 `0x42b4e0` – lichtpas

Lus over alle lichten (`lightsys+0x04`, 0x40 B per licht; `S = light+0x3c`). Per cel-range
(0x1c B, `S+0x04`) alleen als `range.cel` in de lijst zichtbare cellen staat
(`renderer+0x58`, aantal `+0x54`; `0x42b548`).

| Lijst | Code | Actie |
|---|---|---|
| A (`S+0x0c`) | `0x42b56b` | één keer per licht per frame (stempel `face+4 == [0x4c4c24]`): vlag `= 2`; `0x498830(tmp, face, &light+0x0c, light+0x2c)`; `0x42c320(face, tmp, &light+0x30)` |
| B (`S+0x14`) | `0x42b5e3` | alleen vlag `= 2` – niets getekend |
| C (`S+0x1c`) | `0x42b60f` | `0x498830(tmp, poly, pos, bereik)`; `0x42c320(poly, tmp, kleur)` |

### 1.3 `0x498830` / `0x498871` / `0x498890` – lichtbol op het vlak projecteren

`dist = n·L + d` (`0x49883c`); `dist ≥ R` of `dist ≤ −R` → `tmp+0x20 = 0`, klaar.
Anders `s = 0.5 / sqrt(R² − dist²)` (`0x498890..0x4988b4`, `[0x4a9014] = 0.5`),
`F = L − n·dist`, referentievertex = index op `poly+0x24` (derde index; negatief → extra
vertex, `0x49891a`), `U = normalize(V − F)`, `W = n × U` (`0x498a0e..0x498a83`).
Uit: `tmp+0x00..0x08 = W·s`, `tmp+0x0c = 0.5 − F·(W·s)` (rij voor `u`);
`tmp+0x10..0x18 = U·s`, `tmp+0x1c = 0.5 − F·(U·s)` (rij voor `v`);
`tmp+0x20 = k = 1 − |dist|/R` (`0x498b54..0x498b7c`).

### 1.4 `0x42c320` – lichtpolygoon uitgeven

- Backface-test tegen de camera (`0x42c33c`), transformatie + clipping tegen de vier
  frustumvlakken (`0x42c910/0x42cb90/0x42ce00/0x42d070`).
- Vertex `i < 0` → `lightsys+0x0c + 0x30·(−i−1)` (`0x42c3a2`, `0x42c7cf`); de 0x30-byte
  records hebben dezelfde layout als een wereld-vertex (positie, getransformeerd `+0x0c`,
  scherm `+0x18`, clipvlaggen `+0x24`, frame-stempel `+0x2c`).
- Kleur `(int)(C.r·k)<<16 | (int)(C.g·k)<<8 | (int)(C.b·k)` voor alle vertices
  (`0x42c5da..0x42c63d`); `u,v` uit de twee rijen van `tmp` (`0x42c78b..0x42c89c`).
- Textuur `[0x5e8678] + 0x74·(15 − round(k·15.49))` (`0x42c621..0x42c66a`,
  `[0x4aa304] = 15.49`), emmer **4** (`0x42c8ee`).

Lichttexturen: 16 × 32×32, aangemaakt in `0x426f2b` (`0x47f870(i)` → `0x480090`):
`a = i/16` (`[0x4abd9c] = 0.0625`), `px = ((x + 0.5 − 16)/16)·sqrt(1 − a²)`, `py` idem,
`d = min(1, sqrt(px² + py² + a²))`, `grijs = (1 − d) · (i ≠ 0 ? 1/(1 − a) : 1) · 255`.
Textuur `i` is dus de doorsnede van de lineaire lichtbol op hoogte `a·R`, genormaliseerd
op zijn maximum `1 − a`; vermenigvuldigd met de vertexkleur `C·k` (`k ≈ 1 − a`) geeft dat
`C · (1 − afstand/R)`.

### 1.5 `0x42b6c0` – basis-face, en de emmers in `0x4293f0`

`0x42b6c0` kijkt naar de vlag (`0x42ba31..0x42ba3f`):

- vlag ≠ 2 (`0x42bef9`): één polygoon, vertexkleur × `renderer+0x1ac` (0.6; `0x42bfb5`…),
  eigen textuur, emmer **10** (`0x42c2ec`).
- vlag = 2: (a) polygoon met de blanco textuur `[0x5e8684]`, kleur `renderer+0x1b0`
  (AMB), `u = v = 0.5`, emmer **10** (`0x42ba45..0x42bb99`); (b) polygoon met eigen
  textuur en onverzwakte vertexkleur, emmer **1** (`0x42bb9e..0x42bef4`).

Emmer `n` = lijst `tex+8+4n` (`0x42b460`). Volgorde en states in `0x4293f0`
(D3D7-renderstates: 0x0e ZWRITEENABLE, 0x0f ALPHATESTENABLE, 0x13 SRCBLEND, 0x14 DESTBLEND,
0x1b ALPHABLENDENABLE, 0x1d SPECULARENABLE; TSS 1 = COLOROP, 0x0c = ADDRESS):

| # | Emmer | Adres | States | Inhoud |
|---|---|---|---|---|
| 1 | 0, 10 | `0x4294ad..0x429564` | COLOROP MODULATE(4), blend uit, zwrite aan, ADDRESS WRAP | opake wereld: onbelichte faces en de AMB-vulling |
| 2 | **4** | `0x429572..0x429600` | zwrite uit, ADDRESS **CLAMP(3)**, blend aan, SRC = **ONE(2)**, DEST = **ONE(2)** | lichtpolygonen (A en C) |
| 3 | 2 | `0x42960e..0x42965e` | blend uit, zwrite uit, SPECULAR aan | half-doorzichtige-instantie-schaduw: `AMB (specular) + lichttextuur × C·k·…` (§4) |
| 4 | 5 | `0x42966c..0x429696` | blend uit, zwrite uit, SPECULAR uit | instantie-schaduw: egaal AMB (§4) |
| 5 | **1** | `0x4296a4..0x429732` | ADDRESS WRAP, blend aan, SRC = **DESTCOLOR(9)**, DEST = **SRCCOLOR(3)** | textuurpas van de belichte faces = 2·src·dst |
| 6 | – | `0x429740` | als `device+0x20 ≠ 0`: COLOROP = **MODULATE2X(5)** voor alles hierna | |
| 7 | lijst `+0x1c0`, 11, 8 (SRCALPHA/INVSRCALPHA), 3 (ONE/ONE), `0x428d00`, 9 | `0x4297ae..0x429a07` | | modellen, transparant, effecten |

## 2. Lijsten A, B, C en de extra vertices (correcties op het formaatdocument)

| Wat | Betekenis | Bewijs |
|---|---|---|
| **A** | faces die het licht volledig ziet; hele face krijgt de lichtpolygoon | `0x42b56b`; data: licht ligt altijd vóór het vlak en binnen bereik, zwaartepunt volgens de BSP altijd belicht (W1A 5385/5385) |
| **B** | **ouder-faces van de C-polygonen** (gedeeltelijk belicht); alleen nodig om ze vlag 2 te geven zodat ze in multipass getekend worden. Niet "schaduwwerpers" | `0x42b5e3`; data: elke C-polygoon is coplanair met een B-face, A ∩ B = ∅ |
| **C** | de belichte restpolygonen van B-faces, vooraf geclipt door de lichtbouwtool | `0x42b60f` |
| C-veld `+0x08` ("face") | **geen face-index**: het staat op de plek van het materiaalwoord van een `.gel`-face en is in ~93 % van de gevallen exact het materiaalwoord van de ouder; de lichtpas leest het niet | `0x42c320` leest `poly+8` nergens; `litparse.py` controle 3 |
| **extra vertices** (negatieve indices) | **staan in het bestand**: het is de tabel "probes" (`lightsys+0x08/+0x0c`, 16 B in het bestand → 0x30 B in geheugen). Index `i` → record `−i−1`. Het zijn dus geen lichtsamples; de u32 "kleur" wordt door geen enkele gevonden lezer gebruikt | `0x42c3a2`, `0x42c7cf`, `0x49891a`; data: alle negatieve indices < aantal records en elk punt ligt op het vlak van zijn polygoon |
| volledig beschaduwde faces | staan in geen enkele lijst → vlag 0 → `vcol × 0.6` | |
| BSP (`S+0x20/+0x24`) | schaduw-BSP voor **punt**queries door modellen (§3), niet gebruikt voor de wereld | `0x40b540`: alleen aangeroepen vanuit `0x42e463`, `0x42f20c`, `0x43ba38` |

## 3. Dynamische objecten (Woody, vijanden, instanties)

Alles in de instantie-tekenfunctie `0x42e2b0`/`0x42e374` (arg-bits: 2 = schaduw werpen,
4 = model tekenen) en de modelrenderer `0x43b3f0`.

**Lichtkeuze** (`0x42e3e4..0x42e573`): lichtlijst van de **sector** van de instantie
(`lightsys+0x10[inst+0x1c]` = `{n, index…}`). `n == 0` → geen licht (bit 2 vervalt).
`n == 1` → dat licht. `n > 1`: per licht `f = 0x40b540(S, inst+0x60)` (instantiepositie):

- `f == −1` en `|L − p|² < R²` → dit licht, klaar (`0x42e4c2`);
- `f ≠ −1` en `vlak(f)·p > 0` (punt vóór de bladface = belicht) → dit licht, klaar (`0x42e541`);
- anders (in schaduw): onthoud het licht met de grootste (minst negatieve) vlakafstand; dat
  wordt gekozen als geen enkel licht het punt ziet.

> **Sector, geen cel.** De trailer van de `.lit` (FORMAT_TEX_COL_VIS_LIT.md §4, "lichten per
> cel") heeft in alle 28 levels precies zoveel lijsten als de `.gel` **sectoren** heeft, niet
> zoveel als er cellen zijn: House 33 lijsten / 33 sectoren / 2530 cellen, W1A·WWS·W3D·K1A 128
> lijsten / 128 sectoren / 6245–7691 cellen. `world+0x20` is dus het sectoraantal en `inst+0x1c`
> de sectorindex; de sector van een punt komt van `0x4081c0` (daal in de hoofd-kd-boom tot een
> knoop een sectorindex draagt), niet van de bladcelquery `0x408180`. Indexeren met de cel zou
> ver buiten de tabel lezen. Dit is gemeten, niet uit de disassembly gelezen.

**Puntquery `0x40b540(S, p)`**: loop vanaf knoop 0; `vlak·p + d > 0` → `front`, anders
`back`; kind `& 0xF`: 0 = knoop `>>4`, 1 = blad met face `>>4`, anders −1. Interpretatie
door alle drie de aanroepers: **belicht ⇔ resultaat −1 óf p ligt vóór het vlak van de
bladface** (`0x43ba3d..0x43ba80`, `0x42f211..0x42f254`). (Met `litparse.py` nagerekend op de
zwaartepunten van A-faces en C-polygonen: 100 % / 99,9 % belicht.)

**Lichtvector per modeldeel** (`0x43b912..0x43bc4c`; gecachete variant voor stilstaande
instanties `0x42f110`, zonder demping):

```
p      = wereldpositie van het deel (matrix-translatie +0x24..+0x2c)
Ldir  *= 0.85                                   ; [0x4aa3d8], elke frame
als licht gekozen en punt belicht (query hierboven) en |L − p| < R:
    Ldir += 0.15 · normalize(L_lokaal − p_lokaal) · (1 − |L − p|/R)     ; [0x4aa1c8]
    Lkleur = C (floats 0..255)                  ; part+0x0c..+0x14
```

Opslag: per deel 0x18 B op `inst+0xf4 + deeloffset` (`Ldir` 3 floats, kleur 3 floats).

**Vertexkleur** (`0x43bce4..0x43bdbd`): `ndl = N·Ldir` (vertexnormaal `+0x10`, vertexkleur
`+0x1c..+0x24`):
`uit = vcol · renderer+0x1ac · 0.5 (= vcol·0.3) + (ndl > 0 ? ndl·Lkleur : 0)`, per kanaal,
daarna begrensd. Modellen worden na stap 6 van §1.5 getekend (MODULATE2X), dus effectief
`0.6·vcol + 2·ndl·C`. Er is geen aparte per-instantie "kleur"; het zit in de per-deel
lichtvector. `[0x5ac850]` (1/2) telt daarna nog `[0x5ac854]` bij de kleur op (`0x43bdce`;
flits/highlight, niet uitgezocht).

## 4. Schaduw onder figuren: geen blob, wel geprojecteerde geometrie

- Er is geen blob-textuur in gebruik: de twee kandidaten `[0x5e867c]` (64×64
  alfa-verloop, `0x480310`) en `[0x5e8680]` worden alleen geschreven, nooit gelezen.
- Wel: arg-bit 2 van `0x42e2b0` (`0x42e651..0x42ec3a`). Met het gekozen licht wordt per
  modeldeel de omtrek bepaald (`0x43aaa0`), tegen de wereld geclipt via de licht-BSP
  (`0x40bb40`, `0x40bda0` → lijst ontvangende polygonen `{n, soort, face, n×16 B}`), en
  elke vertex vanuit het licht op het facevlak geprojecteerd:
  `P' = L + (P − L) · (−(n·L + d)) / (n·(P − L))` (`0x42eac1..0x42eb53`). De ontvangende
  face krijgt vlag 2 (`0x42eb73`).
- Tekenen: `inst+0x6c` (transparantie) ≤ 0.01 → `0x4385f0`: egale kleur AMB, blanco
  textuur, emmer 5 (opaak, overschrijft de opgetelde lichten → na de textuurpas ziet het
  eruit als een onbelichte face, d.w.z. *alle* lichten weg, niet alleen het gekozen licht).
  Transparantie > 0.01 → `0x4388e0`: emmer 2, lichttextuur, diffuus = `C·k·transparantie`,
  specular = AMB (schaduw wordt lichter naarmate de instantie vervaagt). Transparantie
  > 0.98 → instantie helemaal niet getekend (`0x42e374`).
- Geen hoogte-fade of grondzoeker: de schaduw valt waar de projectie een face raakt; het
  bereik is impliciet dat van het licht.
- Er wordt **nergens getest of het gekozen licht de werper ziet**. Bij `n == 1` wordt dat licht
  zonder enige test genomen (`0x42e422`), bij `n > 1` is er altijd de terugvalkeuze
  (`0x42e524`); alleen een **lege** sectorlijst haalt bit 2 weg (`0x42e56a`). Een figuur die in
  de schaduw staat werpt dus nog steeds een schaduw, vanuit dat terugvallicht. Verder valt de
  hele tekenfunctie af bij `inst+0x1c == -1` (geen sector, `0x42e2c3`).

### Wat de port anders doet (`cast_shadow`/`draw_cast_shadows` in `src/render_gl.c`)

| Origineel | Port |
|---|---|
| werper = omtrek (`0x43aaa0`) van de **hull-nodes** (`S+0x3c`, nodevlag 0x04; Woody 43 van 142), voorgefilterd met de omtrek van de bbox-node | alle polygonen van elke mesh-node + alle skin-driehoeken, per driehoek geprojecteerd |
| ontvangers uit de **licht-BSP** (`0x40bb40`/`0x40bda0`), al tot convexe polygonen geclipt | lijsten A en B van het licht, geclipt met de stencilbuffer. A ∪ B is niet dezelfde verzameling: een volledig beschaduwde face staat in geen van beide |
| enige tests: bladsoort ≠ 2, ≥ 1 vertex vóór het ontvangstvlak, camera vóór dat vlak | plus zelfbedachte grenzen (`s > 40`, een bolstraal-`reach`-test, `k` buiten 1..100). Ze zijn er omdat de port A/B afloopt in plaats van de BSP, en kunnen geldige schaduwen laten vallen |
| `0.01 < transparantie ≤ 0.98` → doorschijnende schaduw (emmer 2, `C·k·transparantie`) | altijd de opake AMB-variant tot 0.98 |
| werper zonder animatie herbruikt zijn polygonen (`0x42f3d0`/`0x42f460`) | elke frame opnieuw |

## 5. Detailoptie `[0x4c2c0c]`

| Waarde | Effect | Adres |
|---|---|---|
| 0 | speler getekend met arg 4 (geen geworpen schaduw); instanties arg 5 | `0x42b380..0x42b39d` |
| ≠ 0 | speler arg 6 (schaduw + model); instanties met SetFlags-bit 1 (`inst+0xf0 & 1`) en soort 1: arg 7 i.p.v. 5 | `0x42b3a2`, `0x42b3cc..0x42b3df` |
| 2 | bovendien de effectpas van SetFlags-bit 0x20 (zie `INSTANCE.md` §6) | `0x43b423` |

De wereld-lichtpas (§1) en de modelbelichting (§3) hangen **niet** van de optie af. Dit
beantwoordt ook open punt 2 van `INSTANCE.md`: de "extra pas tegen tabel `[0x4c4cac]+4`" is
de geworpen schaduw, en die tabel is de lichtentabel.

## 6. Controle tegen W1A (`python tools/litparse.py W1A`)

7 lichten, 1987 extra vertices, 20382 faces. Per licht o.a.: licht 0 `R = 2500`,
kleur (168,236,255), A = 1626, B = 232, C = 304; licht 4 `R = 2450`, A = 570, B = 299,
C = 442. Controles: (1) 0 negatieve indices buiten de extra-tabel; (2) 0 extra vertices
buiten het polygoonvlak; (3) 0 C-polygonen zonder coplanaire B-face, A ∩ B = ∅
(C-veld `+8` = materiaalwoord van die B-face in 1538 van 1654 gevallen); (4) 0 A-faces met het licht achter
het vlak of buiten bereik; (5) BSP-query: C-zwaartepunten 1653/1654 belicht, A 5385/5385.

## Onzeker

- **Dynamische lichten**: `0x498790(lightsys, soort, &pos, &kleur, straal)` zet een record
  (0x2c B) in `lightsys+0x18` (aantal `+0x14`); aanroepers `0x40c6dc`, `0x40cce7`,
  `0x40cd9d`, `0x40cea4`, `0x40d1da`, `0x4766ee`, `0x477dc6` (effecten/projectielen). De
  *lezer* van die tabel is niet gevonden; onbekend of en hoe ze de wereld of modellen
  belichten.
- `light+0x28` (2, één keer 3) en het typebyte van `object_id`: geen lezer gevonden in de
  lichtpas.
- C-veld `+0x08`: 7 % wijkt een paar eenheden af van het materiaalwoord van de ouder
  (vermoedelijk hernummerde materiaaltabel na de lichtbouw); functioneel irrelevant.
- Vlagwaarde 1 in `lightsys+0x28` (wordt getest in `0x42ad0b`, nergens gezet).
- `[0x5ac860]` = 1 met vector `[0x5ac864..0x5ac86c]` (`0x42ed16`): alternatieve
  lichtrichting voor modellen (menu/cutscene?), en `[0x5ac850]/[0x5ac854]` (kleur-optelling)
  zijn niet uitgezocht.
- De exacte begrenzing/afronding van de model-vertexkleur na `0x43bdbd` en de details van
  de omtrek-/clipfuncties `0x43aaa0`, `0x40b8f0`, `0x40bbc0` (alleen nodig voor geworpen
  instantie-schaduwen) zijn niet uitgewerkt.
- Of `device+0x20` (MODULATE2X-ondersteuning, `0x429745`) op elke kaart gezet is; zo niet,
  dan zijn modellen half zo helder. Aanname in dit document: gezet.
- Framebuffer-verzadiging: de optelling klemt op 1.0 vóór de ×2-textuurpas; een port die de
  formule in één pas uitrekent moet `min(1, AMB + Σ)` nemen om hetzelfde te krijgen.
