# De hemel (sky box) – texture-vlag `0x200`

Statische analyse van `game/Woody.exe` (`out/disasm_full.txt`); niets is op de draaiende game
geverifieerd. Numerieke controle op de data: `python tools/skycheck.py [LEVEL ...]` (§5).
Verwante documenten: `FORMAT_TEX_COL_VIS_LIT.md` §1 (groepvlaggen), `FORMAT_GEL.md` §1
(polygonen), `LIGHTING.md` §1.5 (emmers en renderstates van `0x4293f0`).

**Kernidee.** "Bit `0x200`" is geen bit maar **byte 1 van de groepvlaggen `== 2`**
(`cmp byte [tex+0x45], 2`, `0x42acea`). Een `.gel`-face waarvan het materiaal naar zo'n groep
wijst wordt **nooit getekend**; hij is alleen een schakelaar: "vanuit hier is de hemel te
zien". Staat er in een frame minstens één zo'n face in de zichtbare set, dan tekent de
wereld-renderer `0x42ac10` aan het eind vijf vaste quads: een **kubus met halve ribbe 50000
rond de camerapositie** (vier zijkanten + bovenkant, geen bodem), elk met een eigen texture.
Er is dus geen frame-keuze per face, geen UV-generatie uit het materiaal, de normaal of de
vertexposities, en geen tijd-animatie: de vijf "frames" van de groep zijn de vijf kubusvlakken.
De UV-matrix van het materiaal (`material_uv`) wordt voor deze faces niet gebruikt (in House zou
die u −134..67, v −63..38 geven: de fijne strepen van de port).

## Recept voor de port (kort)

1. Bij het laden: `sky_group` = de groep met `((flags >> 8) & 0xff) == 2` (hooguit één per
   level). Alle `.gel`-polygonen waarvan het materiaal naar die groep wijst **uit de
   wereld-batches laten** (niet tekenen, niet belichten). Collision blijft zoals hij is.
2. Bron van de vijf texturen: heeft de **levelbank** (`Data/<LVL>/<LVL>.rck`) **≥ 5
   afbeeldingen** (type 1), dan de afbeeldingen **3, 0, 1, 2, 4** van die bank; anders de
   frames **0, 1, 2, 3, 4** van de groep (in deze volgorde voor de quads A..E hieronder).
3. Per frame, als het level een sky-groep heeft: vijf quads rond de camerapositie `C`,
   `S = 50000`:

   | quad | vlak | frame | bank-afb. | v0 | v1 | v2 | v3 |
   |---|---|---|---|---|---|---|---|
   | A | `z = C.z+S` | 0 | 3 | (−,−,+) | (−,+,+) | (+,+,+) | (+,−,+) |
   | B | `x = C.x+S` | 1 | 0 | (+,−,+) | (+,+,+) | (+,+,−) | (+,−,−) |
   | C | `z = C.z−S` | 2 | 1 | (+,−,−) | (+,+,−) | (−,+,−) | (−,−,−) |
   | D | `x = C.x−S` | 3 | 2 | (−,−,−) | (−,+,−) | (−,+,+) | (−,−,+) |
   | E | `y = C.y+S` | 4 | 4 | (−,+,+) | (−,+,−) | (+,+,−) | (+,+,+) |

   (tekens = `C ± S` per as x,y,z). UV's, voor elke quad gelijk, met `hu = 0.5/breedte`,
   `hv = 0.5/hoogte` van de **groep**-texture (§2.1, ook als bankafbeeldingen getoond worden):
   `v0 = (hu, hv)`, `v1 = (hu, 1−hv)`, `v2 = (1−hu, 1−hv)`, `v3 = (1−hu, hv)`.
   Let op: **v = 0 (bovenste beeldrij) ligt op `y = C.y − S`**; de zijplaatjes staan in het
   bestand dus op hun kop (donkerblauw boven in het bestand = onder de horizon in de wereld,
   wolken onder in het bestand = boven de horizon). §5 bewijst dit aan de naden met het
   bovenvlak.
4. States: opaak, geen blending, geen alpha-test, kleur wit (geen vertexkleur, geen
   `.lit`-belichting, géén factor 0.6), texenv MODULATE 1×, geen culling, geen mist,
   bilineair. Het origineel tekent met z-test LEQUAL en z-write aan op afstand 50000 (dus
   achter alle levelgeometrie). Voor de port is gelijkwaardig en robuuster: **als eerste
   tekenen met `glDepthMask(0)` + `glDisable(GL_DEPTH_TEST)`**; dan maakt de kubusgrootte
   niet uit. Houd je de originele grootte aan, dan moet het far-vlak > `50000·√3 ≈ 86603`
   liggen (de port heeft `zf = 200000`, dat past).
5. De hemel draait niet mee met de camera (alleen translatie): de kubus is as-uitgelijnd in
   wereldruimte en wordt gewoon door de view-matrix getransformeerd.
6. De tijd-animatie van wereld-groepen (`rnd_frame`, `tg->gl_tex = gl_frames[time…]`) kan weg:
   de enige `.gel`-faces met een meer-frame-groep zijn in alle 28 levels de sky-faces (§4.2).

## 1. Waar het gebeurt: `0x42ac10` (basis-faces van de wereld)

`0x42ac10(this = renderer)` wordt per frame aangeroepen vanuit `0x401da1` (`LIGHTING.md` §1.1).

| Adres | Wat |
|---|---|
| `0x42ac1d..0x42ac75` | frameteller `[0x4c4c24]++`; alle polygonen van de zichtbare groepen (`renderer+0x54/+0x58`, `gel+0x38`) krijgen die stempel in `poly+4` |
| `0x42ac94..0x42ad2f` | lus over de zichtbare sectoren (`renderer+0x48/+0x4c`, `gel+0x24`) en hun polygoonlijsten (`sector+0x08/+0x0c`); alleen polygonen met de stempel van dit frame, stempel daarna `−1` (elke polygoon één keer) |
| `0x42acd3..0x42acd9` | `material & 0x8000` → overslaan |
| `0x42acdb..0x42ace6` | `tex = [level+0x5c + material·0x24 + 0x20]` = pointer naar **frame 0** van de groep van het materiaal |
| `0x42acea` | `cmp byte [tex+0x45], 2` – byte 1 van de groepvlaggen. Gelijk → `[esp+0x10] = 1` (sky gezien), `ebx = tex`, **`jmp 0x42ad19`: de face wordt niet aan `0x42b6c0` gegeven, dus niet getekend** (`0x42acf0..0x42acfa`) |
| `0x42acfc..0x42ad14` | alle andere faces: `lightsys+0x28[face] == 1` → overslaan, anders `0x42b6c0(face)` |
| `0x42ad35` | na de lus: `cmp [esp+0x10], 1`; niet gelijk → klaar. Anders de sky box (§2) |

Gevolgen:
- De test is **gelijkheid met 2**, niet een bittest. Byte 1 `== 3` (W3A groep 77 `0x00ff0300`,
  S3A groep 75 `0x00ff0368`, K3A groep 76 `0x00ff0310`; 32×32, 17 frames) is dus **geen** sky,
  zie §4.1.
- `ebx` is de groep van de *laatst* geziene sky-face. In de data is er hooguit één sky-groep per
  level (`tools/skycheck.py`), dus dat maakt niet uit.
- De sky box verschijnt alleen als een sky-face in de zichtbare set zit (sector-/groepzicht uit
  `.vis`). Een port zonder `.vis`-culling tekent hem gewoon altijd als het level een sky-groep
  heeft; binnenruimtes zijn dicht, daar is hij toch niet te zien. (Onzeker: of er plekken zijn
  waar het origineel door een gat de wis-kleur laat zien in plaats van de hemel.)
- De sky-faces worden ook niet belicht: ze komen niet in `0x42b6c0` en dus niet in de emmers
  1/10. In de data staat geen enkele sky-face in een `.lit`-lijst A of B (gecontroleerd voor
  House, K1R, K2R, K3R, WWS, W2A, W2B, W2D, W3C, W3D), dus er komt ook geen lichtpolygoon op.

## 2. De kubus: `0x42ad40..0x42b373`

Vier vertices van 0x44 bytes op de stack (`esp+0x18`, `+0x5c`, `+0xa0`, `+0xe4`; na de
`push 0x40` op `0x42adf2` staan alle offsets 4 hoger in de listing). Layout van zo'n vertex,
af te lezen uit `0x439540`: `+0x00` positie (wereld), `+0x0c/+0x10/+0x14` view-ruimte,
`+0x18/+0x1c` scherm, `+0x20` 1/w, `+0x24/+0x28/+0x2c` kleur r,g,b (0..1), `+0x30` alpha,
`+0x38/+0x3c` u,v, `+0x40` clipcodes.

| Adres | Wat |
|---|---|
| `0x42ad43..0x42ade2` | `+0x24..+0x30` van alle vier vertices `= 1.0` → kleur wit, alpha 1 |
| `0x42ad40`, `0x42ad63` | `hu = 0.5 / (float)[tex+0x38]` (`[0x4a9014] = 0.5`, breedte van de **groep**-texture, ook als daarna bankafbeeldingen gebruikt worden) |
| `0x42adfc`, `0x42ae0d` | `hv = 0.5 / (float)[tex+0x3c]` |
| `0x42adf4..0x42ae4d` | UV's, één keer gezet en voor alle vijf quads hergebruikt: v0 `(hu, hv)` (`0x42adf8`, `0x42ae13`), v1 `(hu, 1−hv)` (`0x42ae03`, `0x42ae1f`), v2 `(1−hu, 1−hv)` (`0x42ae34`, `0x42ae3f`), v3 `(1−hu, hv)` (`0x42ae46`, `0x42ae4d`); `[0x4a900c] = 1.0` |
| `0x42ae0a`, `0x42ae54` … | camera = `[renderer+8]`, positie op `+0x90/+0x94/+0x98` (dezelfde positie die `0x439540` voor zijn backface-test gebruikt, `0x43958d`); elk hoekpunt = camerapositie ± `[0x4aa2f8]` = **50000.0** per as |
| `0x42ae54..0x42af4e` | quad **A**, vlak `z+S`: v0 (−,−,+), v1 (−,+,+), v2 (+,+,+), v3 (+,−,+) |
| `0x42af53..0x42b051` | quad **B**, vlak `x+S`: (+,−,+), (+,+,+), (+,+,−), (+,−,−) |
| `0x42b056..0x42b15a` | quad **C**, vlak `z−S`: (+,−,−), (+,+,−), (−,+,−), (−,−,−) |
| `0x42b15f..0x42b264` | quad **D**, vlak `x−S`: (−,−,−), (−,+,−), (−,+,+), (−,−,+) |
| `0x42b269..0x42b36e` | quad **E**, vlak `y+S`: (−,+,+), (−,+,−), (+,+,−), (+,+,+) |

Er is geen zesde quad: onder de horizon (`y−S`) wordt niets getekend.

### 2.1 Texturekeuze per quad

Vóór elke aanroep: `cmp [0x5e8670], 5` (`esi = 5`, `0x42aded`; tests op `0x42af1f`, `0x42b023`,
`0x42b126`, `0x42b22f`, `0x42b339`).

| quad | `[0x5e8670] < 5`: frame van de groep | `[0x5e8670] ≥ 5`: levelbank-afbeelding |
|---|---|---|
| A | `ebx + 0` = frame 0 (`0x42af32`) | `[0x5e8674] + 0x15c` = afbeelding **3** (`0x42af36..0x42af3f`) |
| B | `ebx + 0x74` = frame 1 (`0x42b032`) | `[0x5e8674] + 0` = afbeelding **0** (`0x42b03d`) |
| C | `ebx + 0xe8` = frame 2 (`0x42b135`) | `+0x74` = afbeelding **1** (`0x42b143..0x42b14d`) |
| D | `ebx + 0x15c` = frame 3 (`0x42b23e`) | `+0xe8` = afbeelding **2** (`0x42b24c..0x42b255`) |
| E | `ebx + 0x1d0` = frame 4 (`0x42b348`) | `+0x1d0` = afbeelding **4** (`0x42b356..0x42b35f`) |

`[0x5e8674]` / `[0x5e8670]` = array (0x74-byte texture-structs) en aantal van de
**bank-1-afbeeldingen**, d.w.z. de type-1-items van de level-`.rck` (`0x47f9a0` voegt er één
toe en verhoogt de teller; `0x47f630` alloceert en zet de teller op 0, `0x47f6c0` geeft vrij;
zie `RCK.md` en `HUD_TEXT.md` §5.1). Frames van één groep liggen aaneengesloten met stride 0x74
(`FORMAT_TEX…` §1), vandaar `ebx + i·0x74`.

In de data sluit dat precies (`tools/skycheck.py`):

| Levels | sky-groep | frames | level-`.rck` afbeeldingen | bron |
|---|---|---|---|---|
| House g0, K1R/S1R g3, K2R/S2R/K3R/S3R g0, WWS/KWS/SWS g35 | 128×128 | 5 | 0, 2 of 3 | frames 0..4 |
| W2A/K2A/S2A g5, W2B g1, W3C g14, W3D g23 (32×32), W2D g0 (128×128) | | 1 | precies 5 (128×128) | bankafbeeldingen 3,0,1,2,4 |

De één-frame-groepen zijn dus alleen een plaatshouder; hun eigen (32×32) texture wordt nooit
getoond. Er is geen level met een sky-groep met < 5 frames én < 5 bankafbeeldingen (de engine
zou dan voorbij de groep lezen). Omgekeerd hebben Blackbox (72), Credits (13) en Lang (6)
bankafbeeldingen maar geen sky-groep: daar gebeurt niets.

Let op de **0.5-texel-inzet bij bankafbeeldingen**: `hu/hv` komen van de groep-texture
(`ebx`), niet van de bankafbeelding. Voor W2D is dat 128 = 128 (exact); voor W2A/K2A/S2A/W2B/
W3C/W3D is de groep 32×32 en de afbeelding 128×128, dus de inzet is daar `0.5/32` = 2 texels
van de afbeelding. Een port die pixelgetrouw wil zijn neemt dus `hu = 0.5/groep.breedte`.

### 2.2 `0x439540(this = renderer, n = 4, verts, tex, flags = 0x40)`

| Adres | Wat |
|---|---|
| `0x439554` | `flags & 1` = backface-test; niet gezet → **geen culling** |
| `0x43960e..0x439630` | `flags & 0x70 == 0x40`: vertices staan in **wereldruimte**; `0x43964d..0x439689` transformeert ze met de view-matrix `[renderer+8]+0x30` (rijen op `+0x30/+0x40/+0x50`) |
| `0x43968c..0x4396eb` | clipcodes tegen de vier zijvlakken van het frustum (`±x' > z'`, `±y' > z'`); **geen near- en geen far-vlak** – de kubus kan niet door een far-clip verdwijnen |
| `0x439896..0x439968` | alles buiten → weg; anders clippen (`0x438cc0`, `0x438f00`, `0x439130`, `0x439340`) |
| `0x439999` | `flags & 8` niet gezet → pad `0x439b2a`: `rhw = 1/z'`, **`z = 1 − 12·rhw`** (`[0x4aa2fc] = 12.0`, `0x439b38..0x439b4a`), diffuse `= (alpha·r·255)<<16 | (alpha·g·255)<<8 | alpha·b·255` = `0x00ffffff` (`0x439b73..0x439be9`, `[0x4aa308] = 255`), specular `= 0` (`0x439bd2`), u,v gekopieerd (`0x439b63..0x439b70`) |
| `0x439bf6`, `0x439c1c..0x439c3c` | `flags & 4` niet gezet → **emmer 0**: `0x42b460(tex, 0)` |

Met `z' ∈ [50000, 86603]` is `z ≈ 0.99976..0.99986`: achter alle levelgeometrie (levels zijn
hooguit ~±20000 groot), vóór het wisvlak `z = 1`.

## 3. Renderstates

Emmer 0 wordt als allereerste geflusht (`0x4293f0`, `LIGHTING.md` §1.5, rij 1):

| State | Waarde | Adres |
|---|---|---|
| TSS0 COLOROP | MODULATE (4) – 1×, niet 2× | `0x4294b2` |
| ALPHATESTENABLE | 0 | `0x4294ca` |
| ALPHABLENDENABLE | 0 | `0x4294dd` |
| ZWRITEENABLE | 1 | `0x4294f0` |
| TSS0 ADDRESS | **WRAP** (1) | `0x429503` |
| SPECULARENABLE | 0 | `0x42951b` |
| ZENABLE / ZFUNC | 1 / LESSEQUAL (4) – device-init, nergens per emmer gewijzigd | `0x47ec3f` (ZFUNC) |
| CULLMODE | NONE (1) – device-init | `0x47ec86` |
| MAG / MIN / MIP-filter | LINEAR (2) / LINEAR (2) / POINT (2) – device-init, stage 0 | `0x47ed3c`, `0x47ed4c`, `0x47ed5c` |
| Mist | geen enkele `SetRenderState(0x1c FOGENABLE, …)` in de hele exe gevonden → uit | – |

Dus: opaak, wit × textuur, **geen** `.lit`-factor (de 0.6 van onbelichte faces zit in
`0x42b6c0`, dat hier niet doorlopen wordt), geen vertexkleur, geen mist. Adresmodus is WRAP;
het zijn de half-texel-inzet-UV's die voorkomen dat bilineair filteren over de rand heen de
overkant binnenhaalt (op `u = hu` valt het monster precies op het midden van texel 0). De
groep-texturen hebben 3 door de engine gegenereerde mipmaps (`0x47f7f0(…, 3, …)`, `0x426e9f`;
details en de port in §8); op 128×128 over ~90° beeldhoek wordt mip 0 gebruikt zodra het
venster breder dan ~256 px is, dus mip-bleeding speelt voor de kubus in de praktijk niet.
Onzeker: of de bankafbeeldingen (`0x480780`) mipmaps krijgen.

Voor OpenGL 1.1: `GL_REPEAT` + `GL_LINEAR` met de inzet-UV's geeft voor de kubus hetzelfde
beeld (`GL_CLAMP` van 1.1 mengt de randkleur erin en is dus slechter; `GL_CLAMP_TO_EDGE`
`0x812F` mag als de driver het kent, is niet nodig). De port gebruikt voor de groepframes
sinds issue #38 gewoon dezelfde gemipmapte texturen als de wereld (§8), net als het origineel.

## 4. De overige vlagbits en de andere vragen

### 4.1 Byte 1 van de vlaggen (`tex+0x45`)

Enige lezer in de hele exe: `0x42acea` (`== 2`). Waarden in de data: 0 (bijna alles), 2 (sky),
3 (drie groepen: W3A g77, S3A g75, K3A g76; 32×32, 17 frames, duur 1.6 s). Byte 1 `== 3` heeft
voor de engine **geen betekenis**. W3A g77 bestaat uit 16 frames van een groene, golvende
vloeistof plus een 17e egaal lichtgroen frame (`python tools/skycheck.py --frames W3A 77` →
`out/frames_W3A_g77.png`); geen enkele `.gel`-face gebruikt hem (materiaal 18859), hij hoort
bij `.ins`-modellen en wordt via de model-route `0x47f290` geanimeerd. Onzeker: waarvoor de
editor "3" gebruikte (vermoedelijk een type-keuzelijst: 0 normaal, 2 sky, 3 animatie).

### 4.2 Tijd-animatie en scroll van wereld-faces

- `0x42b6c0` (basis-face) geeft in beide paden `[materiaal+0x20]` = **frame 0** aan de emmer
  (`0x42c2f4..0x42c304`; het belichte pad springt er met `push 1` op `0x42bef2` naartoe). In
  `0x42b6c0..0x42c320` wordt `tex+0x48/+0x4c` (scroll), `+0x54` (duur) of `+0x58` (frames) nergens
  gelezen. **`.gel`-faces animeren en scrollen dus niet.**
- De frame-/scroll-berekening `0x47f290(this = tex, animstate, uv_out, materiaal, tijd)` heeft
  precies één aanroeper: `0x43c341`, de model-/instantie-renderer. De animatietoestand komt van
  de instantie (`inst+0xd8`: modus `&7`, starttijd `+4`, snelheid `+8`; `0x47f2bd..0x47f2f1`).
- Data: de enige `.gel`-faces met een meer-frame-groep of scroll ≠ 0 zijn in alle 28 levels de
  sky-faces (House 104, K1R/S1R 577, K2R/S2R 1429, K3R/S3R 2082, WWS/KWS/SWS 523). De
  `anim_duration` van sky-groepen is rommel (1.0, 0.0 of denormalen als `1.79e-43`) en wordt
  voor de sky nooit gelezen.

### 4.3 Lage bits `0x08..0x80` van byte 0 (`tex+0x44`)

Alle lezers/schrijvers van `byte [tex+0x44]` in de exe:

| Adres | Wat |
|---|---|
| `0x426e97`, `0x47f7fd..0x47f81e` | loader: bit 0 (colour key) doorgeven / terugschrijven |
| `0x42801a..0x42802b` | `.ins`-loader: `(& 6) << 4` naar de face-vlaggen (bits 1–2) |
| `0x428f6b` … `0x4297fd`, `0x429a6b`, `0x429c51`, `0x429e5b`, `0x42a1b7` | flush: bit 0 → ALPHATESTENABLE aan/uit |
| `0x42b4b2..0x42b4b8` | `0x42b460` (texture in dit frame voor het eerst in een emmer): **`and 0xf7`** – wist bit 3 en nult de emmerlijsten `tex+0x08..+0x34` |

- **Bit 3 (`0x08`)**: wordt per frame gewist bij het eerste gebruik; er is **geen code die het
  zet of test** (de enige `or [..+0x44]` in de exe, `0x4a172e`, zit in de CRT). Dood
  runtime-bit; de waarde in het bestand is betekenisloos.
- **Bits 4–7 (`0x10..0x80`)**: geen lezer gevonden (ook geen dword-lezer van `tex+0x44` in de
  rendercode `0x426000..0x440000` / `0x47e000..0x486000`). In de data zien ze eruit als
  restdata van de editor: in K3A en K3R heeft vrijwel elke groep een andere waarde in de hoge
  nibble (0x10, 0x20, … 0xf0, zonder verband met het soort texture), in S1R hebben alle groepen
  `0xc0`. **Geen** scroll, environment mapping, no-fog, no-z, dubbelzijdig of draw-first: die
  bestaan in deze renderer niet als texture-vlag (culling staat altijd uit, mist bestaat niet,
  de tekenvolgorde ligt vast in de emmers). Onzeker blijft alleen of een niet-gevonden
  indirecte lezer bestaat; de sky-route gebruikt ze aantoonbaar niet.

## 5. Numerieke controle (`tools/skycheck.py`)

Het script past het recept toe: zoekt de sky-groep, telt de (niet getekende) sky-faces, kiest
de bron volgens §2.1, bouwt de vijf quads en vergelijkt langs elke gedeelde kubusribbe de
randtexels van de twee aangrenzende texturen (RMS over 64 monsters, schaal 0..255; ter
controle ook met één van beide randen gespiegeld). Daarna schrijft het
`out/sky_<LVL>_equirect.png` (360°×180°, midden = kijkrichting +z, +x links) en
`out/sky_<LVL>_cross.png`.

House (frames 0..4 van groep 0):

```
A z=+S | B x=+S   RMS 0.4   gespiegeld 30.2        C z=-S | D x=-S   RMS 1.5   gespiegeld 42.9
A z=+S | D x=-S   RMS 0.8   gespiegeld 29.6        C z=-S | E y=+S   RMS 1.4   gespiegeld 15.1
A z=+S | E y=+S   RMS 0.4   gespiegeld  0.8        D x=-S | E y=+S   RMS 0.5   gespiegeld 15.5
B x=+S | C z=-S   RMS 0.6   gespiegeld 32.5
B x=+S | E y=+S   RMS 0.4   gespiegeld  0.6
```

Alle acht naden sluiten tot op ~1/255. De vier naden met het bovenvlak E bewijzen dat de
**onderste** beeldrij (v = 1) van de zijplaatjes aan de bovenkant (`y+S`) ligt, dus dat de
plaatjes "op hun kop" in het bestand staan, en dat de zijvolgorde 0→1→2→3 = +z → +x → −z → −x
is. De equirect-preview toont een doorlopende hemel: wolken boven een lichte horizonband,
donkerblauw eronder, en een zwart gat waar geen bodemquad is.

W2D (één-frame-groep, bankafbeeldingen 3,0,1,2,4): slechtste naad RMS 4.7, gespiegeld
50..217 → de volgorde 3,0,1,2,4 klopt. Overige levels (slechtste naad): K1R/S1R/K3R/S3R 0.0,
K2R/S2R/WWS/KWS/SWS 4.9, W2B 6.7, W3C/W3D 7.4, W2A/K2A/S2A 27.9 (de naden met E zijn daar
0.6..4.5; de uitschieter zit in de zijnaden A|B, C|D, waar de harde horizonrand tussen twee
plaatjes één texel verspringt – een oneffenheid in de assets, onder de horizon van het level).

W3A heeft geen groep met byte 1 `== 2` en dus geen sky box; groep 77 (byte 1 `== 3`) is de
geanimeerde vloeistof van §4.1.

## 6. Recept voor de port (`src/render_gl.c`)

1. **Laden** (waar nu de wereld-batches gebouwd worden, lus over `gel->polys`):
   `sky = ((gflags >> 8) & 0xff) == 2` → `continue` (face in geen enkele batch, ook niet in
   `litb` en niet in de lichtpolygonen van lijst A/C). Onthoud `r->sky_group = grp`.
2. **Texturen**: als de level-`.rck` ≥ 5 afbeeldingen heeft, upload afbeeldingen 3,0,1,2,4
   (BGRA, rij 0 = bovenste rij, zelfde oriëntatie als de `.tex`-frames: de naden met E sluiten
   in W2D) als `sky_tex[0..4]`; anders `sky_tex[i] = groups[sky_group].gl_frames[i]` (die
   hebben, zoals elke `.tex`-textuur, mipmaps: §8). `GL_LINEAR`, `GL_REPEAT`. De renderer heeft daarvoor toegang tot de levelbank
   nodig (nu in `hud.c`/`main_engine.c`): geef de vijf afbeeldingen door bij `rnd_init` of via
   een `rnd_set_sky(images)`.
3. **Tekenen**, in `rnd_frame` direct na het zetten van projectie en view, vóór de wereld:
   ```
   glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
   glDisable(GL_CULL_FACE); glEnable(GL_TEXTURE_2D); glTexEnvi(..., GL_MODULATE); glColor3f(1,1,1);
   hu = 0.5f / group.width; hv = 0.5f / group.height;          /* van de GROEP, ook bij bankafbeeldingen */
   voor quad q in A..E: bind sky_tex[q];
       uv = (hu,hv) (hu,1-hv) (1-hu,1-hv) (1-hu,hv);  pos = cam.pos + S * teken[q][k]   /* tabel bovenaan */
   glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
   ```
   `S = 50000` (of elke andere waarde tussen near en far/√3: zonder dieptetest is het beeld
   identiek). Niet door `rnd_fade`/letterbox heen breken: de kubus is gewone 3D-inhoud en valt
   onder dezelfde fade en hetzelfde viewport als de wereld.
4. **Weg**: de tijd-cyclus `tg->gl_tex = tg->gl_frames[…time…]` voor wereld-batches (§4.2);
   wereld-batches binden altijd frame 0. Modellen houden hun eigen route (`set_material(…,
   frame)`).
5. Wis-kleur: onder de horizon tekent het origineel niets; wat daar te zien is, is de
   wis-kleur van het origineel (niet onderzocht). In de levels ligt daar geometrie.

## 7. Open vragen / onzeker

- De wis-kleur van de framebuffer in het origineel (zichtbaar onder `y−S` als een level daar
  open is, en overal waar sky-faces niet in de zichtbare set zitten).
- Of de bankafbeeldingen mipmaps krijgen (`0x480780` niet gelezen); voor de kubus irrelevant,
  die wordt op mip 0 bemonsterd.
- W1B (§8): het beeld van het origineel is niet naast de port gelegd; dat de verre sterrenwanden
  daar vrijwel zwart zijn volgt uit de mipmap-keten en MIPFILTER POINT, niet uit een screenshot.
- Links/rechts-oriëntatie op het scherm is niet apart geverifieerd: het recept gebruikt
  wereldcoördinaten rechtstreeks en erft dus de (al tegen een screenshot gecontroleerde)
  conventie van de port "rechtshandig, y omhoog, +x links van +z". De naden zijn onafhankelijk
  van die keuze. Een screenshot van het titelscherm van het origineel (zon/wolkposities) zou dit
  afsluiten.
- Betekenis van byte 1 `== 3` en van de bits `0x10..0x80` voor de *editor*; voor de engine
  hebben ze geen effect (§4.1, §4.3).

## 8. W1B: sterrenhemel zonder sky-groep, en de mipmaps (issue #38)

Issue #38: "Sky in sideways part W1B looks glitched" – in het zij-aanzicht bij de deur op
(−11231, 187, −13722) (marker `0x19b`, script `1088 0x100019b 2`; testen met
`WOODY_SIDE=0x19b … --pos -11231 187 -13722`, camera (−12231, 527, −13422) kijkend naar +x)
was de hemel een flikkerende ruis van losse pixels.

**W1B heeft geen sky-groep** (`tools/skycheck.py W1B`: geen groep met byte 1 `== 2`), dus er is
geen kubus. De "hemel" is gewone `.gel`-geometrie: **groep 52** (128×128, vlaggen `0x00ff0000`,
één frame, 171 sterren van één texel op een zwarte achtergrond) op 156 polygonen die dozen om de
speelruimte vormen, o.a. x −13455..−9484, y 253..1253, z −15772..−8019 rond deze plek. Alle
materialen van die groep hebben `|∂u/∂x| = 0.01`: **één herhaling per 100 eenheden, 1.28
texels per eenheid**. Vanuit de zijcamera ligt de wand op x = −9484 zo'n 2750 eenheden verderop;
bij 800 px beeldhoogte en ~84° verticale beeldhoek is één pixel daar ≈ 6 eenheden ≈ 8 texels.

De port had **geen mipmaps**: `upload_texture` zette `GL_LINEAR` en bemonsterde dus uit mip 0
één willekeurige texel op de acht, die bij elke camerabeweging een andere ster raakte of miste –
de ruis uit het issue (en minder opvallend op elke verre vloer).

Het origineel (alles statisch gelezen):

| Adres | Wat |
|---|---|
| `0x426e9f..0x426eaa` | `.tex`-loader: per frame `0x47f7f0(stream, w, h, 3, colourkey, 0)` |
| `0x47f82c` | → `0x47fa60(stream, w, h, mips = 3, colourkey, 0, tex)` |
| `0x47fa93..0x47fa9d`, `0x47fac2` | `mips ≠ 0` → DDSD-vlaggen `0x21007` (+`DDSD_MIPMAPCOUNT`), `dwMipMapCount = 4` |
| `0x47fb4f..0x47fb5c` | caps `0x401008` = `TEXTURE \| MIPMAP \| COMPLEX` |
| `0x47fc80..0x47fed1` | lus van 3: `GetAttachedSurface` naar het volgende niveau, halve breedte/hoogte (`0x47fd2b`, `0x47fd2d`), per doeltexel het **2×2-boxgemiddelde** van de vier bronpixels, alfa inbegrepen (`0x47fd83..0x47fe17`: `Σ (p & 0xfcfcfc) >> 2` voor RGB, `Σ ((p >> 2) & 0x3fc00000)` voor A), terug naar het surfaceformaat (`0x47f170`) |
| `0x47f1fd` | formaat 3 (colour key) = ARGB1555: alfa blijft alleen als bovenste bit over, dus een gemiddelde texel is ondoorzichtig als ≥ 3 van de 4 bronnen dat zijn |
| `0x47ed3a..0x47ed62` | device-init, stage 0: `SetTextureStageState` (IDirect3DDevice7 `+0x94`) MAGFILTER 2 = `D3DTFG_LINEAR`, MINFILTER 2 = `D3DTFN_LINEAR`, **MIPFILTER 2 = `D3DTFP_POINT`** (D3D7: NONE 1, POINT 2, LINEAR 3); dezelfde drie voor stage 1 op `0x47edc4..0x47ede6`. Geen andere schrijver van 0x10/0x11/0x12 in de exe, geen LOD-bias (0x13) of MAXMIPLEVEL (0x14) |

Dus: 4 niveaus (128, 64, 32, 16 voor een 128×128-groep), bilineair binnen het dichtstbijzijnde
niveau = OpenGL `GL_LINEAR_MIPMAP_NEAREST`. Dit geldt voor **elke** `.tex`-textuur (wereld,
modellen en de sky-kubus); de bankafbeeldingen (`0x480780`) en de gegenereerde 32×32-texturen
(`0x47f840`) lopen via een andere route.

Voor de W1B-hemel betekent dat: vanaf ~4 texels per pixel wordt niveau 2 of 3 gekozen, waarin
een ster van één texel over 16 of 64 texels is uitgesmeerd; de verre sterrenwanden zijn dan een
vrijwel zwart, rustig vlak en alleen dichtbij (het plafond op y = 1253 recht boven de speler)
blijven losse sterren zichtbaar. Geen flikkering.

**Port** (`src/render_gl.c`, `upload_texture` + `box_halve`): mip 0 zoals voorheen, daarna
steeds de halve maat met hetzelfde 2×2-boxgemiddelde (alfa van colour-key-texturen terug naar
0/255 met drempel 128, zoals de 1555-bit), `GL_TEXTURE_MAX_LEVEL` (`0x813D`) = 3,
`GL_LINEAR_MIPMAP_NEAREST` / `GL_LINEAR`. De keten wordt tot 1×1 doorgerekend zodat de textuur
ook onder een strikte GL 1.1 compleet is; `MAX_LEVEL` beperkt het gebruik tot de vier niveaus
van het origineel. Niet nagebootst: de 16-bits kwantisering tussen de niveaus.
