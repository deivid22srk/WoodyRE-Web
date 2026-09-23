# Modelrendering (`.ins`-instances): typecodes, oogtextuur/knipperen, 0xFFFF en UV's

Statisch afgeleid uit `Woody.exe`: instance-renderer `0x43b3f0` (enige aanroeper `0x42f0fe` in de per-instance
draw `0x42e2b0`), polygoontekenaar `0x43d790`, driehoektekenaar `0x43e0f0`, textuurframe-kiezer `0x47f290`,
`.ins`-loader `0x427290` (punten `0x427b69`–`0x427f40`, polygonen `0x427fa0`–`0x4283b6`, driehoeken
`0x428460`–`0x4285d5`), matrixhulpen `0x440fc0` (inverse met schaal) en `0x4408e0` (A·B, rijvectoren).
Numerieke controle: `tools/modeluv.py`, `tools/helperuv.py`. Node-nummers hieronder zijn **1-based** (zoals in
het bestand / `insparse.py`); in de C-port (0-based array) is dat steeds één lager (Woody: ogen = port-nodes 62/65,
"witte vierkantjes" = 64/67, helpers = 63/66).

## Recept (wat er in een port moet veranderen)

1. **Mesh-nodes met typecode 2 nooit tekenen.** (`0x43b6c2`: `cmp byte [node+1], 2 ; je volgende node`.) Dat zijn
   de twee witte vierkanten voor Woody's ogen. Het zijn geen oogleden en ze worden nergens anders getekend.
2. **Skinned driehoeken (`Model.tris`) hebben expliciete UV's**, geen projectie: de "matrix" van hun materiaal
   is een tabel met drie UV-paren. Bestandsvertex j (j = 0,1,2 in bestandsvolgorde) krijgt
   `(u, v) = (f[3j], f[3j+1])`. In de port (`t->i2` = 1e dword, `t->i1` = 2e, `t->i0` = 3e):
   `i2 → (m[0], m[1])`, `i1 → (m[3], m[4])`, `i0 → (m[6], m[7])`. **Dit is bug B** (ruis op het shirt van de
   vijanden: model 42 in W1A heeft 299 getextureerde driehoeken; de planaire formule geeft daar UV's van −25..+34).
3. **Node-polygonen: planaire projectie op het punt *minus de node-pivot***, dus op dezelfde `lp` die de port al
   voor de positie gebruikt: `u = m0·lp.x + m3·lp.y + m6·lp.z + m9`, `v = m1·lp.x + m4·lp.y + m7·lp.z + m10`
   (pivot = die van de node waartoe het punt behoort). Bij de meeste modellen is de pivot 0 en maakt het niets uit;
   bij Woody's ogen en snavel wel.
4. **Mesh-node met een helper-kind (kind 0x10, `parent` = de mesh):** in het gewone pad komen de UV's van *alle*
   polygonen van die mesh niet uit het materiaal maar uit de helper: breng het (pivot-relatieve) punt naar
   de lokale ruimte van de helpernode, `q = p · W_mesh · W_helper⁻¹`, en neem
   mode 0: `(a,b) = (q.y, q.z)`, mode 1: `(q.x, q.z)`, mode 2: `(q.x, q.y)`;
   `u = 0.5 − a / v_8`, `v = b / v_c − 0.5` (`v_c`, `v_8` = 1e en 2e float van de helper in het bestand;
   Woody: 35 en 40, mode 1). Textuur = **frame 0** van de groep. De helpernode is geanimeerd → zo beweegt de pupil.
5. **Oogleden/knipperen = animatie-events, geen timer.** Lees per frame de event-track van **node 1** van het
   model voor de lopende animatie; het laatste event type 5 met `t ≤ huidig frame` levert vier frame-indices
   `e[2], e[3], e[4], e[5]` voor mesh-nodes met typecode **5, 6, 7, 8** (geen event → 0). Index 0 = ogen open (alleen
   stap 4). Index k ≠ 0: teken de polygonen van die node **twee keer**: één laag met textuurframe `k`
   (als `k < frame_count`, anders frame 0) en de gewone materiaal-UV's van stap 3 (ooglid, colour-key), en één laag
   met frame 0 en de helper-UV's van stap 4 (oogbol). Ooglid bovenop.
   Woody's idle (anim 0, 1200 frames in 6 s): t=600 → 1, t=640 → 2, t=660 → 1, t=700 → 0: **één knipper per 6 s-lus,
   op 3.0 s, 0.5 s lang** (half 0.2 s, dicht 0.1 s, half 0.2 s).
6. **Geen automatische textuurframe-cyclus op modellen.** Zonder bericht 16/18 (`inst+0xd8` bits 0-2 = 0) wordt
   altijd frame 0 gebruikt, ongeacht `frame_count`/`anim_duration`. Bind voor instances dus `gl_frames[0]`
   i.p.v. de globaal cyclende `gl_tex` (de override-modi staan in INSTANCE.md §2).
7. **Materiaal met bit 15 = vlakke kleur ARGB1555**, écht getekend: `R = (m>>10)&31`, `G = (m>>5)&31`, `B = m&31`,
   elk `× 8/255 ×` de belichte vertexkleur (0..255). `0xFFFF` = wit (0.973) — Woody's handschoenen. De vele
   0xFFFF-polygonen in kind-4/kind-2-nodes worden niet getekend omdat de renderer alleen de mesh-lijst (kind 0) afloopt.
8. Texturen moeten **herhalen** (wrap): de UV's van Woody's ogen liggen in [−1, 0].
9. **Backface-culling doet de engine zelf, per polygoon; het apparaat staat op `D3DCULL_NONE`** (`0x47ec8b`),
   want polygoonvlag `0x2` markeert een dubbelzijdige polygoon die wél getekend moet worden. Node-polygonen
   (`0x43bf65`): breng de camera naar de lokale ruimte van de node en sla de polygoon over als
   `n·cam_lokaal + d ≤ 0` met het vlak dat de loader uit de rustpose bouwt (`0x4280c2`–`0x428375`): over elk
   drietal opeenvolgende hoeken P, Q, R het drietal met de langste `n = (R−Q) × (R−P)` boven 0.01, genormaliseerd,
   `d = −n·R`; geen enkel drietal zo lang → (1, 0, 0, 0). **Alleen de winding beslist**; de vertexnormalen worden
   niet bekeken (ze zijn op sommige modellen onzin: W1A-model 18, de glazen liftplaat van issue #2, heeft elk vlak
   twee keer, getextureerd en als omgekeerde 0xFFFF-kopie, en een keuze op de normalensom hield van elk paar de
   verkeerde over). Skinned driehoeken
   (`0x43c1a4`), elke frame in wereldruimte: `n = (A−B) × (A−C)`, tekenen als `n·(camera − A) > 0`.
   Zonder dit worden ook de achterkanten getekend; die krijgen `ndl = 0` en dus alleen de ambient-term
   (0.6 × vertexkleur), en precies op de silhouetrand — waar voor- en achterkant dezelfde diepte hebben —
   kan de donkere winnen: een donker randje om elk figuur.
10. **Alfatest**: `ALPHAREF = 0x7f`, `ALPHAFUNC = GREATEREQUAL` (`0x47ec50`, `0x47ec5c`), aan/uit per **textuur**
   (de colour-key-vlag van de `.tex`-groep, `tex+0x44 & 1`), niet per pas (`0x429a6b`). Filter: MAG/MIN LINEAR,
   MIP POINT (`0x47ed42`–`0x47ed62`) over 4 zelfgebouwde niveaus (SKY.md §8; port: `GL_LINEAR_MIPMAP_NEAREST`),
   adressering WRAP behalve voor de lichtvlekken (CLAMP, `0x429597`).
   Bij het omzetten van een colour-key-textuur gooit het origineel de magenta wég: de texel wordt
   `ARGB 0x00000000`, dus **zwart met alfa 0** (`0x47fc1e`). Blijft de magenta staan, dan mengt het filter die
   met de ondoorzichtige buren en krijgt elke alfarand een roze zoom.
11. **Zwarte contourlijn** (`0x43ea30`, gevoed door de twee achterkantlijsten die `0x43b3f0` aanlegt): dit is de
   inktlijn om de figuren in het origineel. Zie §7.

## 1. Welke nodes worden getekend; typecodes

`0x43b3f0` loopt uitsluitend over de mesh-lijst `S+0x30/0x34` (kind 0x00; `0x43b40a`–`0x43b420`, lus `0x43b67d`–
`0x43c158`). Hull- (0x04), bbox- (0x02), press-, volume-, marker- en helpernodes komen daar niet in voor en worden dus
nooit getekend, wat hun materiaal ook is.

Typecode (`node+1`, flags bits 8-15) in de renderer:

| typecode | kind | gedrag | adres |
|---|---|---|---|
| 2 | 0x00 mesh | node overslaan (modus 1 reserveert alleen zijn 16-byte lichtslot) | `0x43b6ae`, `0x43b6c2`; idem in het niet-tekenpad `0x42f170` |
| 5, 6, 7, 8 | 0x00 mesh | textuurframe uit event type 5: `idx = typecode − 5` in `[esp+0x90..0x9c]` | `0x43bf03`–`0x43bf3a` |
| overig / 0 | 0x00 mesh | gewoon pad | `0x43c065` |

Verdere vergelijkingen met typecode 2 bestaan niet (`cmp byte [reg+1], 2` komt alleen op `0x42f170` en `0x43b6c2`
voor). In alle 28 levels komen op mesh-nodes alleen typecode 2 (50×), 5 (48×) en 6 (48×) voor. De overige
waargenomen codes zitten op andere nodesoorten en raken de renderer niet: press 1/4, marker 1/5/9, licht 5/7,
dummy 1 (markers worden via `0x42f6b0(typecode, n, out)` opgezocht; zie FORMAT_INS.md §6).

Woody (model 0): node 63 (tc 6, pivot (−7.96, 24.98, −12), 15 polys materiaal 0x458d) met kinderen 65 (tc 2,
2 driehoeken 0xFFFF, 24.4 × 31.8 vlak) en 64 (helper mode 1, 35/40); node 66 (tc 5, 0x458e) met 68 (tc 2) en 67
(helper). Groep 64: 64×64, colour-key, 5 frames: **0 = oog (wit + groene iris), 1 = ooglid half, 2 = ooglid dicht,
3 en 4 = schuine "boze" oogleden** (magenta = transparant).

## 2. Frame-keuze: event type 5 (`0x43b58b`–`0x43b62c`)

```
N1   = eerste node van de node-array (S+0x68)            ; 0x43b58b
ev   = N1+0x78 [anim = inst+0xb0] -> (off, cnt) in N1+0x88 ; 0x43b595-0x43b5ce
tcur = inst+0xac / (duration/4096) * nframes             ; 0x43b5ae-0x43b5bd  (0x4aa138 = 1/4096)
f[0..3] = 0
for elk event (in volgorde) zolang event.t <= tcur:       ; 0x43b5d7-0x43b5e3
    type 5: f[0..3] = event dwords 2,3,4,5 ; 6 dwords     ; 0x43b5f2-0x43b61d
    type 4: 9 dwords overslaan ; type 3: 15 dwords          ; 0x43b61f / 0x43b624
```
Bij het tekenen van een mesh-node met typecode 5..8 en `k = f[typecode−5] ≠ 0` (`0x43bf2d`–`0x43bf3a`):
per voorwaarts gericht (of dubbelzijdig, polyflag 0x2) getextureerd polygoon
`tex = (k < tex+0x58) ? groep + k·0x74 : groep` (`0x43bfb0`–`0x43bfc8`) en `0x43d790(poly, tex, materiaal, vlag)`
(`0x43bfcb`); heeft de mesh een helper, dan daarna nog eens `0x43d790(poly, materiaal->tex (frame 0), helpermatrix)`
voor dezelfde polygonen (`0x43c00c`–`0x43c056`). Vlakke-kleurpolygonen worden in dit pad overgeslagen (`0x43bf97`).

Data (W1A, model 0, event-track van node 1; 14 van de 91 animaties hebben type-5-events):
anim 0 (idle, 1200 fr / 6.0 s): (600: 1,1) (640: 2,2) (660: 1,1) (700: 0,0); anim 20/22/26: (0: 0,0) (3: 2,2)
(10 of 7: 0,0); anim 31: (0: 2,2) = ogen dicht; één animatie gebruikt (4, 3) = boze blik (typecode-5-oog = node 66 frame 4, typecode-6-oog = node 63 frame 3).
Ook andere modellen gebruiken het (455 type-5-events buiten model 0 over alle levels).
Er is geen random/timer-knipperlogica in de renderer; `Perso` (`0x44a2d0`/`0x44cda0`) is hiervoor niet nodig.

## 3. Geen auto-cyclus op modellen (`0x47f290`)

Gewone polygonen worden verzameld (`0x43c10b`) en daarna getekend met
`0x47f290(this = materiaal->tex, &inst+0xd8, out, materiaal, now)` (`0x43c314`–`0x43c35d`; enige aanroeper).
`0x47f290`: `tex+0 == 0` (statische groep) → klaar; anders `[0x5e8688] = this` (= frame 0) en alleen als
`inst+0xd8 & 7 ≠ 0` én `frame_count ≠ 1` wordt een ander frame gekozen (`0x47f2bd`–`0x47f2d4`, jumptabel `0x47f604`).
De ctor-reset `0x42e218` zet `+0xd8 &= 0xc0`. Multi-frame groepen met `anim_duration > 0` cyclen op instances dus
**niet** vanzelf; alleen na bericht 16/18 (INSTANCE.md §2). De zin "zonder override geldt de normale globale
textuuranimatie" in INSTANCE.md §2 klopt voor instances niet: er is dan gewoon frame 0.
Het helper-pad (`0x43c0e0`) en het typecode-pad roepen `0x47f290` niet aan.

## 4. UV-generatie

### 4.1 Node-polygonen: planair op pivot-relatieve punten
De loader trekt per node, vóórdat hij de polygonen van die node leest, de pivot van alle punten van de node af
(`0x427daf`–`0x427df9`: `P -= N+0x20..0x28`). De UV-berekening in de loader (`0x4280e4`–`0x428110`) dient alleen om
`P+0xc` (UV-splitsvlag, drempel 0.015 = `0x4aa288`) te zetten; er worden geen UV's opgeslagen.
De echte UV's ontstaan per frame in `0x43d790` (`0x43da31`–`0x43da6c`), tenzij `poly+1 & 0x80` (vlakke kleur):
```
P = [renderer+0x14] + idx·0x28        ; renderer+0x14 = S+0x20 (0x42e3de; 0x509adc en 0x5e86ac zijn hetzelfde object, 0x484068/0x48407e)
u = M[0]·P.x + M[1]·P.y + M[2]·P.z + M[3]        ; M = 3e argument (materiaal: f0,f3,f6,f9)
v = M[4]·P.x + M[5]·P.y + M[6]·P.z + M[7]        ; (f1,f4,f7,f10)
```
`P` is het rustpose-punt relatief t.o.v. de pivot, niet het geanimeerde punt → de textuur zit vast aan de node.
Geen per-polygoonvlag verandert de formule: polyflag 0x1 (`0x427ff8`) laat in de loader alleen de materiaalkoppeling
weg (komt in de data niet voor), 0x2 = dubbelzijdig (geen backface-test, `0x43bf65`/`0x43c094`), bits 0x60 =
textuurvlag `&6` → gemengde tekenmodus 3 (`0x43d7b9`). Er is geen aparte materiaaltabel voor modellen (`level+0x5c`,
`0x43bf9c`/`0x43c314`).

Controle (`python tools/modeluv.py W1A -m 0 -n 61 63 66`): met pivot-aftrek vallen beide ogen binnen één tegel en
zijn ze symmetrisch (L: u −0.67..−0.24, v −0.80..−0.04; R: u −0.79..−0.33, v −0.82..−0.06); zonder aftrek loopt het
rechteroog over de tegelrand (v −1.18..−0.41). Snavelvlak node 61: v −0.81..−0.29 i.p.v. −0.43..+0.08.
Vijand (model 42): alle pivots 0, polygoon-UV's 0.01..0.99 met beide formules (elk polygoon heeft een eigen materiaal).

### 4.2 Skinned driehoeken: expliciete UV's uit de materiaalentry
Loader `0x428460`: bestands-dwords d0,d1,d2,mat → `poly+0x1c = d0`, `+0x1a = d1`, `+0x18 = d2`. Tekenaar `0x43e0f0`:
`ptr = materiaal+8` (`0x43e13c`); voor engine-vertex k = 0,1,2 (`0x43e39a`–`0x43e3ab`): `u = [ptr]`, `v = [ptr+0x10]`,
`ptr −= 4`. Dus engine-vertex k: `u = mat[2−k]`, `v = mat[4+2−k]`; met `mat+0..0xc = f0,f3,f6,f9` en
`mat+0x10..0x1c = f1,f4,f7,f10` (FORMAT_TEX §1) is dat: **bestandsvertex j → (f[3j], f[3j+1])**; rij 3 en kolom 2 van
de "matrix" zijn bij deze materialen 0. Er wordt geen puntpositie gebruikt.

Controle (`python tools/modeluv.py W1A -s 282`): model 42 (slots 282, 293, 313, 314, 396, 494), 299 driehoeken,
groep 105: expliciet u 0.28..0.98, v 0.02..0.98, max. spanwijdte per driehoek 0.19; planair −25..+34, spanwijdte 37.
Gedeelde punten: 347 keer dezelfde UV bij deze toewijzing, tegen ≤ 100 bij elke andere permutatie van de drie rijen
(de rest zijn echte UV-naden). Woody heeft 712 driehoeken waarvan 4 getextureerd (groep 63), de rest vlakke kleur.

### 4.3 Helper-projectie (kind 0x10) — `0x43b6ce`–`0x43b908`
Voor elke mesh-node zoekt de renderer in de helperlijst `S+0x40/0x44` een helper met `N+0x84 == mesh` (`0x43b716`–
`0x43b746`). Gevonden: `X = 0x440fc0(W_helper, schaal)` (inverse), `H = 0x4408e0: W_mesh · X` (`0x43b74d`–`0x43b798`),
en een 8-float "pseudomateriaal" op `[esp+0x70]` (s8 = `N+8` = 1/v_8, sc = `N+0xc` = 1/v_c, H rij-major 4×3):

| mode (`N+4`) | u-rij | v-rij |
|---|---|---|
| 0 (`0x43b878`) | −s8·(H1,H4,H7), 0.5 − s8·H10 | sc·(H2,H5,H8), sc·H11 − 0.5 |
| 1 (`0x43b83f`) | −s8·(H0,H3,H6), 0.5 − s8·H9 | sc·(H2,H5,H8), sc·H11 − 0.5 |
| 2 (`0x43b7ba`) | −s8·(H0,H3,H6), 0.5 − s8·H9 | sc·(H1,H4,H7), sc·H10 − 0.5 |

In het gewone pad wordt elk zichtbaar polygoon van zo'n mesh direct getekend met dit pseudomateriaal en
`materiaal->tex` = frame 0 (`0x43c0d8`–`0x43c106`). Controle (`python tools/helperuv.py W1A 0 63 0`): u 0.35..0.83,
v −0.82..0.03 over alle keyframes van de helper (de helper verschuift o.a. tussen t=540 en 560 → pupil kijkt opzij).
In alle levels: 96 helpers mode 1, 1× mode 2.

## 5. Materiaal 0xFFFF / bit 15 (`0x43db83`–`0x43df09`, driehoeken `0x43e4c5` e.v.)
`test byte [poly+1], 0x80` → textuur = standaardtextuur `[0x5e8684]`, geen UV-berekening, en per vertex
`kleur = (int)(c5 · 0.0313725 (0x4aa3e8 = 8/255) · vertexlicht)` met c5 = bits 10-14 (R), 5-9 (G), 0-4 (B)
(`0x43dc65`–`0x43dd3f`). Dat is ARGB1555 met bit 15 als vlag, niet RGB565 (FORMAT_INS.md §2.4 is op dit punt onjuist);
de port (`argb1555_to_rgb`) doet het al goed. 0xFFFF is dus zichtbaar wit; er bestaat geen "niet tekenen"-waarde.

## 7. De zwarte contourlijn (`0x43ea30`)

Dit is de inktlijn om Woody en de andere figuren. Het is **geen lijnprimitief en geen crease-lijst**, maar de
achterkant van het model nog een keer, opgeblazen: een klassieke back-face hull.

**Poort (de twee enige aanroepers, `0x43c5ac` en `0x43c5d1`, aan het eind van `0x43b3f0`)** over de twee lijsten
die de renderer tijdens het gewone tekenen heeft aangelegd: achterwaartse driehoeken (`0x43c289`) en
achterwaartse node-polygonen (`0x43c0c7`).

**Voorwaarden** (alle drie in de proloog van `0x43b3f0`):

1. `inst+0xf0 & 0x20` — SetFlags-bit 0x20, bericht 45 (`0x43b423`). Het levelscript zet die per instantie:
   2 tot 51 per level (W1A 9, K2A 29, W3D 51; Blackbox en Credits geen). Instantie 0 (de speler) zit erbij in
   House, W1A, W3C, W3D en WWS; in W2B/W3A is het `…0001`, W2D `…0003`, W1B `…0008`.
2. `[0x4c2c0c] == 2` (`0x43b43a`) — de detailoptie uit `Woody.cfg` (bestandsoffset 0x40), in de meegeleverde cfg 2.
3. De breedte moet positief zijn (hieronder).

**Breedte** (`0x43b447..0x43b4fe`), met `d` = afstand van de camera tot `inst+0x60` (de geanimeerde skeletwortel,
INSTANCE.md §1.1; port `ins_anim_centre()`). **Niet** de instantiepositie: Buzz in W1A (slot 276) staat 1800 eenheden
van de plek waar zijn filmpje (deur 321) hem heen laat lopen, en gemeten vanaf `inst+0xc` viel hij buiten 1500 en
verloor hij zijn contour (issue #35):

| d | w (wereldeenheden) |
|---|---|
| 0 … 750 | `d / 300` (0 → 2.5) |
| 750 … 1500 | `5 − d/300` (2.5 → 0) |
| > 1500 | geen contour |

Constanten: `[0x4aa3e4] = 1/300`, `[0x4aa3e0] = 2.5`, `[0x4a9884] = 5.0`. Omdat `w ∝ d` is de lijn tot 750
eenheden **even dik in beeldpunten** (met de projectie van de port ongeveer `hoogte/540` px).

**Geometrie** (`0x43c49a..0x43c56a`): per vertex van een achterwaartse primitief
`p' = M_node · ((p − pivot) + w · n)` met `n` de **genormaliseerde** vertexnormaal (de loader normaliseert bij het
inlezen, `0x427c01`; de port doet dat niet en moet het zelf doen). Let op: de polygoonlus markeert alleen index
0, 1 en 2 (`0x43c42d`), dus in het origineel blijft de vierde hoek van een quad op het oppervlak liggen tenzij een
buurprimitief hem ook markeert. De port schuift alle hoeken op.

**Kleur en diepte** (`0x43ecd3..0x43ed17`, `0x43edf0`): vlak **zwart**, alfa = `2 × (1 − inst+0x6c)` begrensd op
255 — met z-write aan en zonder blending op de opake lijst is dat gewoon zwart; alleen de vervaag-lijst
(`renderer+0x1c4`, als `(1−fade)·255 < 252`) mengt echt. De diepte is `1 − 12·rhw`, **exact dezelfde als het
model** (geen bias, in tegenstelling tot de schaduw die er `3/65536` af haalt). Omdat batches vooraan gelinkt
worden, komt de contour vóór het model in de flush: buiten de silhouetrand blijft de hull staan, en waar hij door
een holle plooi heen steekt wint hij de dieptetest — dáár komen de lijnen om een snuit of een vinger vandaan.

**Wat wel en niet meedoet**: alle achterwaartse skinned driehoeken; node-polygonen alleen als ze niet
dubbelzijdig zijn (vlag 0x2) en geen blendvlaggen hebben (`flags & 0x60`, `0x43c0c2`). Typecode-2-nodes en de
ooglid-laag (typecode 5..8, `0x43bf65`) doen niet mee.

**De bit komt alleen van het script.** Elk level stuurt direct na bericht 1200 (SetTypeInstance) bericht 45 met
0x21 naar elke actor die een contour heeft, de bazen inbegrepen (W1B Buzz slot 405, W3D 775/790/801). Over alle 28
levels krijgen alleen deze actoren géén 0x20, en dat is authentiek: Woody in Blackbox/Credits/Lang, de eindbaas van
W2B (type 12) en de drie spoken van W3B (type 13). De port zette de bit vroeger zelf op elke actorklasse omdat Buzz
zonder lijn stond; dat was echter de afstandsfout hierboven (zijn geanimeerde wortel staat in W1B 3400 eenheden van
`inst+0xc`), en die hack is weg. `WOODY_SHLOG=1` schrijft per seconde één regel voor elke getekende actor die géén
contour krijgt, met de afstand en de reden (geen bit 0x20, of verder dan 1500).
## Onzeker
- Tekenvolgorde van de twee ooglagen: `0x43d790` tekent niet direct maar vult batches per (textuur, modus)
  (`renderer+0x1b8`, lijsten `+0x1c0`); een afgesloten batch wordt vooraan gelinkt, zodat de later afgesloten
  oogbol-batch vermoedelijk eerst en het ooglid erna getekend wordt. De flush zelf is niet gevolgd; logisch moet het
  ooglid bovenop liggen (zelfde diepte → in een port `GL_LEQUAL` of polygon offset).
- Doel van de typecode-2-vlakken (nooit getekend, geen andere lezer gevonden): vermoedelijk editor-/exportrest.
- `0x440fc0` is alleen voor uniforme schaal volledig gelezen (getransponeerde rotatie × 1/s²); bij instances met
  schaal ≠ 1 kan de helper-UV afwijken van "lokale helperruimte".
- Typecode 7 en 8 (f[2], f[3]) komen in de data op mesh-nodes niet voor; de event-dwords 4 en 5 zijn overal 0.
- Wrap/clamp-state van de textuur is niet uit de D3D-calls afgelezen; de data (UV's in [−1,0]) vereist herhalen.
- Hoe wereldpolygonen (`.gel`) hun frame kiezen is hier niet onderzocht (`0x47f290` wordt alleen voor instances
  aangeroepen).
- Waar `[0x4c2c0c]` geschreven wordt is niet gevonden: het blok komt als geheel uit `Woody.cfg`.
