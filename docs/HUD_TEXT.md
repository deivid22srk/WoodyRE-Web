# HUD_TEXT.md — de 2D-laag: font, strings, tekstvak (1080), HUD, sprites

Alle adressen zijn VA's in `game/Woody.exe` (imagebase 0x400000), uit statische analyse
(`out/disasm_full.txt`). Geverifieerd met [tools/fontrender.py](../tools/fontrender.py) (rendert de font en
strings uit de banken naar PNG; uitvoer in `out/hud/`). Status per bewering: zonder markering = uit de code
gelezen; **onzeker** = afgeleid/niet volledig gevolgd.

Correctie op RCK.md vooraf: afbeeldingen (type 1) gebruiken **geen magenta kleursleutel**. Het veld `+6` van de
afbeeldingskop betekent "heeft alfakanaal" en de vierde byte van elke pixel is een echte 8-bit alfa (§5.1).
Het `reserved`-veld van een string-item is rommel (oude pointer / toevallig twee glyph-codes); geen kerning.

---------------------------------------------------------------------------------------------------

## 1. Font (rck type 3) en strings (rck type 2)

### 1.1 Bestandsindeling van de font-payload (lader `0x43f9a0`, via `0x43fc90`)

De lader leest: 8 bytes itemkop (`0x43f9d7`), **0x1c bytes kop** naar `font+4` (`0x43f9e6`), alloceert
`n·20` bytes (`0x441a30`), leest **n glyphrecords van 20 bytes** (`0x43f9fd`), en daarna `pages` keer een
oppervlak van `size × size` pixels met `0x47f8d0` → `0x480550` (4 bytes per pixel, `0x4806a6..0x480713`).

| offset | type | W1A-waarde | betekenis | bewijs |
|---|---|---|---|---|
| 0x00 | u32 | 75 | `n` = aantal glyphs (`font+4`) | `0x4418d4`, `0x441a38` |
| 0x04 | u32 | 3 | `pages` = aantal atlaspagina's (`font+8`) | `0x43fa00`, lus `0x43fa1b` |
| 0x08 | u32 | 256 | `size` = breedte = hoogte van een pagina (`font+0xc`) | `0x43fa4e` (twee keer gepusht) |
| 0x0c | f32 | 56.0 | `H` regelhoogte in atlaspixels (`font+0x10`) | `0x441960`, `0x441980`, `0x441a80` |
| 0x10 | f32 | 0.0 | ongebruikt (`font+0x14`) | geen lezer gevonden |
| 0x14 | f32 | 16.0 | `B` (`font+0x18`): `H − B` = 40 = nominale lettergrootte waarop `SetSize` normaliseert | `0x441a80` |
| 0x18 | f32 | −3.0 | `M` rand rond de glyph (outline/schaduw), `font+0x1c`; celhoogte = `H + 2·|M|` = 62 | `0x441980` |
| 0x1c | 20·n | | glyphrecords | |
| 0x1c+20n | `pages·size²·4` | | pagina's, **RGBA, 8 bit alfa, bovenste rij eerst** | §1.3 |

Totaal W1A: 0x1c + 75·20 + 3·256·256·4 = 787 960 ✔. Geen atlas per glyph: 3 pagina's van 256×256.

Glyphrecord (20 bytes):

| offset | type | betekenis | bewijs |
|---|---|---|---|
| +0x00 | f32 | `w` tekenbreedte (= `+0x0c`) | `0x441970` |
| +0x04 | f32 | `adv` pen-verplaatsing (steeds `w − 6` = `w + 2M`; de randen overlappen) | `0x441950` |
| +0x08 | u16 | `x` in de pagina | `0x43f94e` |
| +0x0a | u16 | `y` in de pagina (van boven) | `0x43f944` |
| +0x0c | u16 | `w` | `0x43f93b` |
| +0x0e | u16 | `h` (altijd 62) | `0x43f933` |
| +0x10 | u16 | pagina-index | `0x43f926` |
| +0x12 | u16 | rommel (0x97; enige verschil tussen de level- en hub-fonts) | niet gelezen |

**Geen kerning, geen glyph-paren.** Er zijn drie fontbestanden in omloop: alle A/B/C/D/R-levels + Blackbox
(md5 `082e7cd3…`), House/hubs/Lang (`eb802741…`, identiek op byte `+0x12` van elk record na) en Credits
(84 glyphs, 788 140 bytes: dezelfde eerste 75 + 9 extra). Eén glyphtabel volstaat dus voor de hele game.

### 1.2 Font-object in het geheugen (100 = 0x64 bytes, ctor `0x43f640` → basis `0x441b80`)

| offset | type | init | betekenis |
|---|---|---|---|
| +0x00 | vtable | `0x4aa3ec` | `[0]` = herbereken schalen `0x43f850`; `[1]` = SetMode `0x43f700`; `[2]` = cijferstring `0x43f880` |
| +0x04..+0x1c | | | de 0x1c-byte kop uit het bestand |
| +0x20 | ptr | | glyphrecords |
| +0x28 | f32 | 0 | extra regelafstand (nergens gezet) |
| +0x2c/+0x30 | f32 | 1024/768 | virtuele schermgrootte `VW,VH` (`0x441a90`) |
| +0x34/+0x38 | f32 | 640/480 | uitvoergrootte in pixels (`0x441ad0`) |
| +0x3c/+0x40 | f32 | 0 | offset vóór schalen (`0x441af0`) |
| +0x44/+0x48 | f32 | 0 | offset ná schalen (`0x441b10`) |
| +0x4c | f32 | 20 | lettergrootte `S` (`SetSize 0x441a60`); na het laden 30 (`0x43fa70`) |
| +0x50/+0x54 | f32 | | `sx = out.w/VW`, `sy = out.h/VH` (`0x43f850`) |
| +0x58 | f32 | | `k = S / (H − B)` = S/40: glyphschaal in virtuele eenheden |
| +0x5c | ptr | | array van `pages` oppervlakken (0x74 B elk) |
| +0x60 | u32 | `0x80ffffff` | tekenkleur ARGB (§5.2); de aanroeper zet hem vóór elke tekst |

`SetMode(3)` (`0x43f7e6`, aangeroepen na het laden `0x43fa69`): `VW,VH = 640,480`, uitvoer = echte
schermresolutie `[0x5e8650]+4/+8`, offsets 0. **Alle tekstcoördinaten zijn dus 640×480 virtueel, linksboven =
(0,0), y omlaag**, en schalen mee met de resolutie. (Modes 0/1/2 = pixels / y gespiegeld / x gespiegeld worden
door de game niet gebruikt; de HUD zet dezelfde waarden als mode 3 nog eens expliciet, `0x447126..0x447157`.)

Afgeleide maten (virtuele eenheden), met `k = S/40`:

| functie | formule | S = 17 (HUD) | S = 23 |
|---|---|---|---|
| `0x441980` celhoogte (= hoogte van elke getekende glyph én regelhoogte in het tekstvak) | `(2|M| + extra + H)·k` = 62k | 26.35 | 35.65 |
| `0x441960` regelsprong bij code 4000 | `(extra + H)·k` = 56k | 23.8 | 32.2 |
| `0x441950` advance | `adv·k` | | |
| `0x441970` glyphbreedte | `w·k` | | |

### 1.3 Pixelformaat, kleur, alfa

Pagina's: 4 bytes per pixel, **bovenste rij eerst**, bestandsvolgorde **R, G, B, A** (`0x4806a6..0x480713`:
`byte0<<16 | byte1<<8 | byte2 | byte3<<24` = D3D-ARGB) — anders dan de afbeeldingen (B, G, R, A, §5.1). In de data is RGB overal wit en zit de vorm volledig in **A** (29–36 verschillende
alfawaarden per pagina = anti-aliasing + zachte rand). Port: laad als RGBA8, teken met
`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, vermenigvuldig met de tekstkleur.

Voor de font maakt de R/B-volgorde niets uit (RGB = wit). De engine converteert naar 16 bit (ARGB4444) tenzij het device 32-bit
texturen meldt (`device+0x1c`, `0x480584`); een port gebruikt gewoon 8888.

### 1.4 Strings en speciale codes

Een string-item is een rij **u16-codes, afgesloten met 0** (de 0 zit in de payload). De lader `0x43f440`
kopieert de payload letterlijk in een stringpool; `RckGet` (`0x43fb30` → `0x441580`) geeft de pointer.

Klassen (`0x441870`): `0` = einde; `1..2000` = glyph; `2001..3000` en `3001..3999` = gereserveerde klassen
(worden als glyph opgezocht maar vallen buiten `n` → overgeslagen); **`4000` (0xfa0) = nieuwe regel**
(`0x4419b5`: `x = 0` — echt nul, niet de begin-x — en `y += 56k`); `≥ 4001` = genegeerd. In de meegeleverde
banken komt 4000 **niet** voor: meerregelige tekst is altijd opgesplitst in meerdere strings.

**Glyph = code − 1** (`0x4418dd`: record = `glyphs + code·20 − 20`). Let op: RCK.md noemde dit "indices 0..75";
het zijn codes 1..75, 0 is de terminator. Er is geen spatie-uitzondering: spatie is gewoon glyph 18 (leeg, adv 13).

Code → teken (afgeleid door de font te renderen, `out/hud/font_chart.png`; de volgorde is die van eerste
voorkomen in de Engelse stringtabel, dus per taalversie anders):

```
 1-10  0 1 2 3 4 5 6 7 8 9        31 +    41 K    51 w    61 M    71 ,
11 Q   16 r   21 s   26 N         32 L    42 B    52 m    62 f    72 $
12 u   17 e   22 ?   27 %         33 E    43 H    53 d    63 .    73 z
13 i   18 ' ' 23 C   28 =         34 R    44 I    54 l    64 h    74 J
14 t   19 y   24 n   29 /         35 D    45 G    55 c    65 (    75 x
15 A   20 o   25 Y   30 :         36 !    46 F    56 v    66 )    --- alleen Credits-font: ---
                                  37 S    47 a    57 W    67 ®    76 &   77 -   78 é   79 ô   80 j
                                  38 U    48 g    58 p    68 q    81 ç   82 "   83 Z   84 °
                                  39 T    49 P    59 V    69 '
                                  40 O    50 k    60 X    70 b
```
Ontbrekend in de gewone font: `j`, `Z`, `-`, `&`, `"`. Als C-tabel: zie `GLYPHS` in tools/fontrender.py.

**Getallen** worden met `0x441820(font, waarde, u16 *buf)` → `0x4417d0` (recursief, basis 10) gemaakt: elk cijfer
is `digits[waarde % 10]` met `digits` = **Common-string 0** (`0x20000`, `"0123456789"`, via vtable[2] `0x43f880` →
`0x43f520`). Negatieve waarden worden 0 (`0x441828`). Hulpjes: `0x441730` strcpy16, `0x441770` strcat16,
`0x441710` strlen16. Cijfers zijn dus **font-glyphs, geen sprites**.

### 1.5 Talen

Er is **één taal per installatie**. Elke bank bevat precies één stringtabel (Common: 135 strings, identiek in
Woody/Knothead/Splinter.rck; level: 0–1; Credits: 252) en de font is per taal gegenereerd (glyphvolgorde =
eerste voorkomen in de Engelse tekst: "0123456789", "Quit", "Are you sure?", …). Er is geen index-offset per taal
in de exe: `RckGet` indexeert rechtstreeks (`0x441580`). Het level `Lang` (`\Data\Lang\Lang.gel`, string
`0x4b1314`) heeft 6 afbeeldingen (vlaggen) en geen strings; het kiest vermoedelijk welke set banken de
installer/launcher gebruikt — **onzeker, niet gevolgd**; in deze (Engelse) build is er niets te kiezen.

De volledige Common-tabel staat in §8.

---------------------------------------------------------------------------------------------------

## 2. Tekst tekenen

### 2.1 `Font::Draw` = `0x43f890(font, float *x, float *y, const u16 *str)` (thiscall, `ret 0xc`)

Geen schaal-, kleur-, uitlijn- of wrap-argumenten: grootte via `SetSize 0x441a60(S)`, kleur via `font+0x60`,
uitlijnen doet de aanroeper met `Measure`. `*x`/`*y` worden bijgewerkt (pen).

```c
void Font_Draw(Font *f, float *x, float *y, const u16 *s) {
    for (; *s; ) {
        float x0 = *x;
        Glyph *g = Font_Advance(f, *s++, x, y);          /* 0x4419a0: *x += adv*k, of newline; NULL = niets tekenen */
        if (!g) continue;
        float h = Font_CellH(f);                          /* 0x441980 = 62k */
        float w = g->w * f->k;                            /* 0x441970 */
        float px = x0, py = *y;                           /* linksboven van de cel */
        Font_ToScreen(f, &px, &py, 1);                    /* 0x441900: (p + off1)*s + off2 → pixels */
        Font_ToScreen(f, &w,  &h,  0);                    /*           alleen *s */
        Quad2D_Pixels(renderer, ftol(px), ftol(py), ftol(w), ftol(h),          /* 0x481040, §5.3 */
                      g->x, page->h - g->y, g->wpx, -g->hpx, f->colour, f->page[g->page], 8 /*alpha blend*/);
    }
}
```
`y` is de **bovenkant van de 62k hoge cel**, niet de basislijn. De bron-y wordt gespiegeld doorgegeven
(`pageH − y`, hoogte negatief) omdat `0x481040` v = 1 − y/H rekent; netto komt bestandsrij `y` bovenaan.
`ftol` (`0x499580`) = afkappen volgens de FPU-modus (de game laat die op afronden staan: **onzeker**).

### 2.2 `Font::Measure` = `0x441b30(font, const u16 *str, float *w, float *h)`

Zet `*w = *h = 0` en roept per code `0x4419a0` aan: `*w` = som van de advances (na een 4000 opnieuw vanaf 0,
dus breedte van de **laatste** regel), `*h` = aantal newlines × 56k. De hoogte van één regel moet de aanroeper
zelf uit `0x441980` halen.

### 2.3 Centreren

* Getal in een HUD-rondje: `0x4605b0(cx, cy, waarde, font, &x, &y)`: `x = cx − w/2`, `y = cy − cel/2`
  (`0x4605fc..0x460612`). `0x448660(anker, waarde, font, &x, &y)` haalt `(cx, cy)` uit de ankertabel
  `0x5d7b50` en verkleint bij `waarde ≥ 100` de font tot `17 × 0.75` (`0x448676`, `0x4aabb4`).
* Tekstvak: per regel `x = 320 − w/2` (§3). Er is **geen automatische regelafbreking** en geen max-breedte;
  het tekstvak verkleint in plaats daarvan de font.

---------------------------------------------------------------------------------------------------

## 3. Bericht 1080: tekstvak

Handler: game-handler `0x444870` → `0x44511f` → `0x456ed0(box = game+0x18-object, record)`; elke frame
`0x4571c0(box, dt)` vanuit het einde van de game-update `0x4459c0` (`0x445bb5`). Bevestigt en preciseert
CINEMATIC.md §8.

### 3.1 Argumenten

`SEND 7: 1080, hAlign, vAlign, var, id1, id2, id3` (record: `+8` hAlign, `+0xc` vAlign, `+0x10` var,
`+0x14..` id's; de lus stopt bij de eerste `−1` of na 3 regels, `0x456f07`).

| arg | betekenis |
|---|---|
| hAlign | 0 = gecentreerd `x = 320 − w/2`; 1 = links `x = 16`; 2 = rechts `x = 640 − w − 16` (`0x45707b..0x4570bf`) |
| vAlign | 0 = midden `y0 = 240 − tot/2`; 1 = boven `y0 = 16`; 2 = onder `y0 = 480 − tot − 16` (`0x456fe5..0x45702b`), `tot = regels × cel` |
| var | scriptvariabele (`0x02000000 | n`, de engine maskeert `& 0xffffff`, `0x456ef5`): **sluitvlag**, de engine leest hem alleen (`0x457245` → `0x443cd0`) |
| id1..3 | stringrefs. In alle 53 aanroepen in de 28 levels: bank 0 (`0x0002xxxx` = `131072 + n`). `0x20001` (lege string) = **afstandsregel**: de achtergrond begint pas onder die regel (`0x457105`) |

Alle 53 aanroepen gebruiken `hAlign 0, vAlign 2`. Voorbeelden: House-intro `[0,2,var4, 0x20001, 107, 108]`
("If you ever want to see…" / "I want $1,000,000!"); WWS-deuren `[0,2,var22, 0x20001, 47 "Space", 51 "Part A"]`;
W1A `[0,2,var54, 0x20001, 122, 123]` (tutorial); KWS/SWS `[0,2,var24, 113, 114, 115]` (drie regels, geen
afstandsregel).

### 3.2 Opbouw (`0x456ed0`)

```c
box->font = RckGet(0x01030000);                   /* font 0 van de levelbank */
box->size = 25;
for (each line)                                   /* 0x456f3b */
    for (;;) {
        Font_SetSize(font, box->size);
        Measure(line, &w, &h);
        if (640 > w) break;                       /* past (vergelijking met de volle 640, geen marge) */
        box->size -= 1;
        if (box->size < 17) break;                /* 0x4ab2b8 */
    }
box->size -= 2;                                   /* 0x456fa7 — MAAR de font staat nog op de laatst gemeten grootte */
cel = Font_CellH(font);  tot = n * cel;           /* dus layout op (size+2), tekenen op size: tekst valt ~8 % smaller uit dan het vak */
y = y0(vAlign, tot);  top = y;
for (each line) { Measure; x = x(hAlign, w); minx = min(minx, x); maxx = max(maxx, x + w);
                  line.x = x; line.y = y;  y += cel;  if (line.id == 0x20001) top = y; }
box->rect = { left = minx − 16, top = top − 16, right = maxx + 16, bottom = y + 16 };   /* ints, 0x457158.. */
box->t = 0; box->state = 1;
```
Een nieuw 1080 terwijl er nog een vak staat overschrijft het gewoon (geen wachtrij).

### 3.3 Per frame (`0x4571c0`)

| state | gedrag |
|---|---|
| 1 | fade-in **0.5 s**: alfa = `ftol(2t · 254)`; bij `t ≥ 0.5` → state 2 |
| 2 | kleur `0xfeffffff`; **blijft staan tot `*var != 0`** → state 3, `t = 0`. Geen timer, geen toets |
| 3 | fade-out 0.5 s: alfa = `(1 − 2t) · 254`; bij `t ≥ 0.5` → state 0 |
| 0 | niets |

Tekenen: eerst de achtergrond `RectVirtual(left, top, right−left, bottom−top)` (`0x480a10`, blanco textuur
`[0x5e8684]`, vlag 8) met alle vier hoeken `(kleur >> 1) & 0x7f000000` = **zwart met de halve tekstalfa**
(max 0x7f ≈ 50 %); daarna de regels met `Font_Draw`, kleur wit × alfa.

**Het spel pauzeert niet**, de speler blijft bestuurbaar, er is geen toets om weg te klikken. Het script regelt
de levensduur: `var = 0` → `SEND 1080` → later `var = 1` (House: vaste tijden T+300…; hubs: bij het verlaten
van het volume). Het script "wacht" niet op de variabele; het zet hem.

### 3.4 Verwante berichten

Een scan van alle `SEND`s in de 28 levels (tools/ekodisasm.py) vindt **geen ander bericht met een stringref**.
1081–1085 (levelflow), 1088 (zijcamera), 1090 (deeltjes) staan in MESSAGES.md. 1086, 1087, 1089, 1091–1099
komen in geen enkel script voor. Bericht **1172** hoort wél bij de HUD: zie §4.5.

---------------------------------------------------------------------------------------------------

## 4. De HUD

Object: 0x54 bytes, ctor `0x4470d0`, `[0x5d7b44]`, per level (`app+0x30`); font `[0x5d7b48]` = `0x01030000`
(levelbank), animator-object 0x88 B op `hud+0x30` (ctor `0x460620`). Tekenen: `0x447210` vanuit de hoofdlus
(`0x401e55`). Alle HUD-afbeeldingen komen uit **bank 0 = `Common\<karakter>.rck`, afbeeldingen 61..64**
(128×128, met alfa; `out/rck/Woody/image_061…064`, overzicht `out/hud/hud_atlas_61_64.png`).

### 4.1 Wanneer

`0x401e19..0x401e55`: `teken = !cinematic_loopt (0x44f2e0)`; en als `app+0x70 == 0` ook nog
`camMgr+0x138 != 2` (val-dood-camera). Als `app+0x70 != 0` (bericht 1172 dit frame) wordt de HUD altijd getekend
en eerst `0x4484a0` aangeroepen (§4.5). `app+0x70` wordt elk frame gewist (`0x401e60`).
`hud+0` (toestand, `0x448450`): 0 = spel, 1 = pauzemenu pagina 0x18/0x19 (uitgebreide HUD), 2 = verborgen
(overige menu's en app-state 3 = BlackBox; `0x40165c`, `0x404eba`). Bij toestand 2 tekent `0x447210` niets.

De basis-HUD staat **permanent** in beeld (geen in-/uitschuiven); alleen de $- en ladingtellers (§4.4 rij 9–12)
zijn tijdelijk.

### 4.2 Sprites: tabel `0x4ab658` (20 bytes: `f32 x, y, w, h; u32 ref`), tekenaar `0x460480`

`0x460480(sprite, float x, float y, float schaal, flags)` → `RectVirtual(x, y, w·schaal, h·schaal, bron x,y,w,h,
4× 0xfe808080, oppervlak = [0x5e866c] + (ref & 0xffff)·0x74, flags)`. Bron-coördinaten tellen **van linksboven**
in de rechtopstaande afbeelding (zoals rckexport ze wegschrijft).

| # | bron (x,y,w,h) | afb. | wat |
|---|---|---|---|
| 0 | 0,0,64,64 | 61 | gezicht karakter 0 |
| 1 | 63,0,64,64 | 61 | gezicht karakter 1 |
| 2 | 0,63,64,64 | 61 | gezicht karakter 2 |
| 3 | 63,63,64,64 | 61 | groene **$** (uniek item, type 36) |
| 4 | 0,0,94,94 | 62 | grote **W** (bonus type 34) |
| 5 | 0,0,94,94 | 63 | finishvlag (race-bonus type 37) |
| 6 | 0,0,63,90 | 64 | silhouet + ster (lading, type 35) |
| 7 | 0,90,23,23 | 64 | **W-balletje** = 1 health |
| 8 | 27,90,34,34 | 64 | wit rondje met zwarte rand (getalplaatje) |
| 9 | 97,103,29,23 | 64 | rood vakje met zwart gat (leeg health-slot) |
| 10 | 5,116,123,12 | 62 | krachtmeter: oranje vulling met "MAX" |
| 11 | 5,94,123,22 | 62 | krachtmeter: donkere balk/gloed met "MAX" |
| 12 | 63,0,64,64 | 64 | gezicht Buzz Buzzard (baas) |
| 13 | 111,111,16,16 | 63 | blauw **B**-balletje (baas-health) |
| 14 | 97,84,19,15 | 64 | klein rood vakje (baas-slot) |
| 15 | 0,96,31,31 | 63 | (lijkt leeg in de data; **onzeker**) |

`hud+8` (welk gezicht) komt uit het Perso-kind `(perso+0x104 >> 5) & 0x1f` (jumptabel `0x44af78`):
kind 1 → 0, kind 2 en 4 → 1, kind 3 en 5 → 2 (`0x44af58`, `0x44af70`, `0x44af64`). De plaatjes zijn Woody
(linksboven), Knothead (rechtsboven), Splinter (linksonder); dat kind 2/4 = Knothead en 3/5 = Splinter is
afgeleid uit de volgorde van de plaatjes en strings 30–32, niet uit de Perso-code (**onzeker**).

### 4.3 Vaste posities (640×480)

Slots `0x4b3a10` (paren x,y; `0x448620(sprite, slot)` tekent op schaal 1, vlag 8):

| slot | x,y | slot | x,y |
|---|---|---|---|
| 0 | 16,16 | 4 | 16,221 |
| 1 | 16,66 | 5 | 50,266 |
| 2 | 16,136 | 6 | 544,30 |
| 3 | 50,181 | 7 | 584,70 |

Getal-ankers `0x5d7b50` (init `0x447000`): midden van het rondje (sprite 8, 34×34) in slot 1/3/5/7 =
`slot + 17 − 1`: **A0 = (32,82), A1 = (66,197), A2 = (66,282), A3 = (600,86)**.
Overige (init `0x446f10`): health-rij `x = 559 − 29·i`, `y = 51`; balken `y = 51`, hoogte 23; krachtmeter
vulling (0,426), balk (0,421); fontgrootte HUD **17** (`0x4b3a88`).

### 4.4 Elementen (volgorde = tekenvolgorde; `0x447260` balken → `0x447660` sprites+tekst → `0x447d70` meter → `0x4480d0` animaties)

| # | element | wat / waar | waarde | wanneer |
|---|---|---|---|---|
| 1 | blauwe balk links | rect (0,51) 256×23, blanco textuur, links `0x800000ff` → rechts alfa 0 (`0x4472d8`) | | altijd |
| 2 | rode balk rechts, spel | rect (384,51) 27×23 rood alfa 0 → 19; dan 5 slots sprite 9 op `x = 556 − 29i` (i = 1..5), 29×23, alfa links/rechts = `(x − 384)·128/175`; dan rect (556,51) 84×23 `0x80ff0000` (`0x44737e..0x44764c`) | | `hud+4 == 0` |
| 2' | rode balk rechts, race | rect (384,51) 172×23 rood alfa 0 → 0x80, + dezelfde eindrect; **geen slots** (`0x4472f1`) | | `hud+4 == 1` |
| 3 | gezicht | sprite `hud+8` in slot 6 (544,30) | | altijd |
| 4 | levens | sprite 8 in slot 7 (584,70) + getal op A3, **rood `0xfeff0000`**, S = 17 | `hud+0x20` = `perso+0x250 − 1` (min 0) | getal verborgen tijdens leven-animaties (`hud+0x39`, `+0x40`, `+0x35`) |
| 5 | bonus-icoon | spel: sprite 4 (W) in slot 0 (16,16); race: sprite 5 (vlag) | | `hud+0xc == 0` en geen $-animatie (`+0x3b/+0x3c`) |
| 6 | bonus-rondje + getal | sprite 8 in slot 1 (16,66), getal op A0 rood | spel: `hud+0x14` = `perso+0x25c` (0..24, 25 = hartje); race: `hud+0x24` = `perso+0x264` | getal weg tijdens zijn pickup-animatie (`+0x36` / `+0x37`) en tijdens `+0x34/+0x35` |
| 7 | "gepakt / totaal" | tekst wit `0xfe808080`, S = 17, `x = 94 + 16 = 110`, `y = 51 + 12 − cel/2` (verticaal midden van de balk); string = num + Common-string 9 "/" + num | spel: `[0x5e54e8] / [0x5e54e4]`; race: `hud+0x24 / [0x5e54f4]` | spel: niet in levelindex 1, 11, 18 (`0x4479bb`) |
| 8 | health | sprite 7 op `(559 − 29·i, 51)` voor `i = 1..ftol(hud+0x28)`; als `hud+0x34` (nieuw hartje vliegt binnen) wordt de laatste overgeslagen | `hud+0x28` = `perso+0x24c` (float, max 5) | alleen `hud+4 == 0` |
| 9 | $-icoon | sprite 3 in slot 2 (16,136) | | `hud+0 == 1` (pauze) of `hud+0xc > 0` (1172), niet tijdens schuifanimaties |
| 10 | $-getal | sprite 8 in slot 3 (50,181) + getal op A1 rood | `hud+0x18` = `perso+0x260` (type 36) | idem |
| 11 | lading-icoon | sprite 6 in slot 4 (16,221) | | alleen `hud+0 == 1` (pauze) |
| 12 | lading-getal | sprite 8 in slot 5 (50,266) + getal op A2 rood | `hud+0x1c` = `perso+0x254` (type 35) | idem |
| 13 | krachtmeter | sprite 10 op (0,426) en sprite 11 op (0,421), **beide afgesneden op breedte `f·104`** (bron én doel); bij `f ≥ 1`: sprite 10 volle 123 breed en sprite 11 knippert (0.15 s uit / 0.15 s aan, timer `hud+0x44`) | `f = hud+0x2c = perso+0x5e0 × 2/3` (oplaadtijd, max 1.5 s, `0x4574e7`) | altijd getekend, bij f = 0 onzichtbaar |

Getallen ≥ 100 worden op S = 12.75 getekend (§2.3). De data komen elk frame uit `0x44ae60` (Perso → HUD);
`hud+4` = 1 als `perso+0x21c == 1` of `perso+0x4d8` (race-/surflevels).

**Race-timer/positie**: in `0x447660` is er geen; het race-HUD is alleen de vlag + "n / totaal". Een timer
("Seconds", string 128) bestaat alleen in het resultatenscherm (§6). **Baas-health**: niet in `0x447660` maar in een eigen object (`animator+0x44`, tekenaar `0x47b0b0(cur, max)`
via `0x462450`), actief zolang `hud+0x48`; de baasklassen zetten het elk frame met `0x4484d0(aan, cur, max)`
(`0x40cbf1`, `0x40dd80`, `0x40fd82`; start `0x462470` → `0x47aca0`). Rechtsonder, zelfde opbouw als de
health-balk: basis `X = 559 − 3 = 556` (`0x47acaa`), rij-y = 424 (`0x4b3a84`, `0x47ac03`);
`max` slots sprite 14 (19×15) op `x = X − 19·i − 2` met oplopende alfa (kleur `a<<24 | 0x808080`, `0x47adb7..`),
eindrect `0x80ff0000` van `X − 2` tot 640 (`0x47ae6e`), `cur` B-balletjes sprite 13 (16×16) op `x = X − 19·i`
(`0x47b043`), en het Buzz-gezicht sprite 12 op (559, 400) (`0x47aee0`). Eerst schuift de balk van rechts binnen
(timer `+0x20` tot duur `+0x24`, `0x47b0ee`); de precieze y van balletjes t.o.v. slots is **onzeker**.

### 4.5 Bericht 1172 en de tijdelijke tellers

`1172` zet `app+0x70 = 1` voor één frame → `0x4484a0`: als `hud+0xc == 0` en er geen uitschuif-animatie loopt,
start de inschuif-animatie (`0x4618d0`, vlag `+0x3b`); `hud+0xc = 2`. `0x447210` telt `hud+0xc` elk frame af; zodra
hij 0 wordt start de uitschuif-animatie (`0x461a70`, vlag `+0x3c`). Het script moet 1172 dus **elk frame** sturen
zolang de $-teller zichtbaar moet zijn (hub: bij de Jackpot-deur). Tijdens `hud+0xc > 0` verdwijnt het
bonus-icoon (rij 5–7) en staat de $-teller er.

### 4.6 Animaties (animator `hud+0x30`, aangestuurd uit `0x4480d0`; **slechts op hoofdlijnen gevolgd**)

Elke vlag `hud+0x34..0x41` hoort bij één lopende animatie; de `0x46xxxx`-functie geeft 1 terug zolang hij loopt.

| vlag | start | per frame | wat |
|---|---|---|---|
| +0x34 | `0x448380` als de bonusteller daalt (25 → hartje) en health < 5: `0x460bc0` | `0x460bb0` | W vliegt van het icoon naar het nieuwe health-slot `x = 559 − 30·(health+1) − 16` |
| +0x35 | idem bij health = 5 (→ extra leven): `0x460cc0` | `0x460be0` | W vliegt naar het levens-rondje |
| +0x36 | pickup type 34 (`0x448510` kind 4 → `0x4616a0(pos)`) | `0x4611b0` | 3D-positie → icoon, duur-constante 0.2 |
| +0x37 | type 37 → `0x461760` | `0x461270` | idem naar de vlag |
| +0x38 | type 36 → `0x461420` | `0x460db0` | idem naar de $ |
| +0x39 | type 30 → `0x461300` | `0x460d20` | idem naar het gezicht |
| +0x3a | type 35 → `0x461560` | `0x460fb0` | idem naar het silhouet |
| +0x3b/+0x3c | §4.5 | `0x461820` / `0x461a00` | $-teller in/uit |
| +0x3d/+0x3e | `0x448450(1)` / terug | `0x461b60` / `0x461da0` | uitgebreide HUD (pauze) in/uit |
| +0x3f/+0x40/+0x41 | waarde gedaald ($ `0x462330`, levens `0x4622e0`, lading `0x462380`) | `0x461f70` / `0x461f00` / `0x462020` | "min 1"-animatie |

Voor een eerste port kunnen ze allemaal weg: teken dan de getallen altijd.

---------------------------------------------------------------------------------------------------

## 5. Afbeeldingen en de 2D-blit

### 5.1 Afbeeldings-payload (lader `0x47f910` bank 0 / `0x47f9a0` bank 1 → `0x480780`)

`i16 w, i16 h, u16 bpp (24 of 32), u16 heeftAlfa (0/1), u8[w·h·4]`. Pixel = bytes **B, G, R, A**
(`0x48092f..0x48099b`), **onderste rij eerst** (de blit rekent `v = 1 − y/H`, §5.3). `heeftAlfa == 1` → textuur met
alfakanaal (ARGB4444 als het device dat kan, `0x48082a`), A is een echte 8-bit alfa (tot 256 niveaus in de data,
geen magenta). `heeftAlfa == 0` (bpp 24): A-byte is 0 in het bestand en **moet als dekkend behandeld worden**.
De kleursleutel-tak (`tex+0x44 & 1`, `0x4809a5`) staat voor rck-afbeeldingen altijd uit (`0x47f926`).
Oppervlakken: array van 0x74-byte objecten, bank 0 `[0x5e866c]` (aantal `[0x5e8668]`), bank 1 `[0x5e8674]`;
`+0x38/+0x3c` = w/h.

### 5.2 Kleurconventie

Vertexkleur = D3D-ARGB. **RGB: 0x80 = 1.0** (PS2-erfenis): als het device geen MODULATE2X heeft
(`device+0x20 == 0`) verdubbelt de blit RGB in software met verzadiging (`0x480a72..0x480b2e`, `0x48106b`);
anders staat COLOROP op MODULATE2X (`0x429740`, LIGHTING.md). **Alfa: gewoon 0..255**, de game gebruikt 0xfe als
"vol" en 0x80 als half. `0xfe808080` = wit dekkend; `0xfeff0000` = rood; de font-default `0x80ffffff` = wit, half.
Port: `rgb = min(1, 2·c/255)`, `a = c/255`.

### 5.3 De twee blits (renderer `[0x5e86ac]`)

* **`0x480a10` RectVirtual**`(x, y, w, h, sx, sy, sw, sh, c0, c1, c2, c3, surface, flags)` — ints, `ret 0x38`.
  Doel in **640×480 virtueel** (× `W/640` `0x4ab3d4`, × `H/480` `0x4abda0`): schaalt met de resolutie.
  Hoekkleuren: **c0 = linksboven, c1 = linksonder, c2 = rechtsonder, c3 = rechtsboven** (vertexvulling
  `0x480efc..0x48100e`). `surface == 0` → blanco witte textuur `[0x5e8684]`. Overgeslagen als alle vier alfa's < 2.
* **`0x481040` QuadPixels**`(x, y, w, h, sx, sy, sw, sh, colour, surface, flags)` — doel in **echte pixels**, één
  kleur, `ret 0x2c`; alleen door `Font_Draw` gebruikt.

Beide: `u = (sx + 0.5)/texW`, `v = 1 − (sy + 0.5)/texH` (halve-texel-offset, `0x4a9014`), software-clipping tegen
het scherm met UV-correctie, `z = 1/65536`, `rhw = 1`, 6 vertices in een batch per (textuur, modus).
`flags & 8` → modus 2 = **SRCALPHA / INVSRCALPHA**, z-write uit; `flags & 4` → modus 3 = **additief ONE/ONE**;
anders modus 0 = dekkend (lijst `+0x1c0`). Bewijs: lijsten `renderer+0x1c8` / `+0x1cc` worden in `0x428d00` in 256
diepte-emmers gesorteerd en in `0x428ee0` getekend met states 5/6 resp. 2/2 (`0x4290bf`, `0x429182`). 2D-quads
hebben diepte 0 → laatste emmer → **na alle 3D-transparantie, in aanroepvolgorde**. Alles in de HUD/tekst gebruikt
vlag 8. Textuurfilter: niet afgelezen (**onzeker**; de zachte alfaranden vragen om bilineair).

### 5.4 Tekenvolgorde van een frame

1. 3D-wereld (de letterbox is een **viewport-schaal**, geen zwarte quads: CAMERA_SCRIPT.md §2.4; de 2D-laag
   blijft volledig 640×480 en het tekstvak met vAlign 2 valt in de onderste zwarte band).
2. HUD `0x447210` (`0x401e55`).
3. Tekstvak `0x4571c0` (einde van `0x4459c0`, `0x401e9a`) → ligt óver de HUD.
4. Pauze: bevroren spelbeeld + HUD, dan volscherm `0x80000000` (50 % zwart, `0x404f53`), dan het menu `0x446440`.
5. Helemaal bovenop: de 4 fade-objecten `app+0x9c..` (`0x445bf0`, `0x4016e4`): zwarte 640×480-rect, alfa
   `254·t/duur` (of omgekeerd).

---------------------------------------------------------------------------------------------------

## 6. Resultatenscherm, pauzemenu, mask.bin (kort)

* **Menu-object** `app+0x3c` (0x94 B, ctor `0x445e30`, vtable rond `0x4ab600`; pagina's via `0x446490`, GAMEFLOW.md
  §6). Het tekent met dezelfde `Font_Draw`/`RectVirtual`; tekstaanroepen in `0x4468cf`, `0x45bca8..0x462258`.
  Pauze = pagina 0x18/0x19: strings 4 "Continue", 36 "Options", 2 "Quit", 3 "Are you sure?", 5/6 "Yes"/"No";
  HUD in toestand 1 (alle tellers zichtbaar, §4.4 rij 9–12) onder een 50 % zwarte laag. Layout **niet uitgewerkt**.
* **Resultaten** (na EndLevel): strings 12 "CLEARED!!", 13 "RESULTS", 14 "TOTAL", 15 "OK", 17 "HIGH SCORE",
  46 "points", 127 "Level", 128 "Seconds", 129 "Final Score", 130 "Total Score :", en de tekens 7 "%", 8 "=",
  10 ":", 11 "+". Tekenaars: het blok `0x454963..0x455d97` (Perso-eindsequentie, 20+ `Measure`/`Draw`-paren) —
  **niet uitgewerkt**.
* **`extract/Game/mask.bin`** (786 432 B): geladen door de ctor `0x488790` (`0x4887f6`, pad `\Game\mask.bin`)
  als **4 blokken van 0x30000 = 196 608 bytes** (`obj+0x20`, `+0x30020`, `+0x60020`, `+0x90020`; pointers op
  `obj+0xc0020..`). Hetzelfde object houdt 8 afbeeldingsrefs van de **levelbank** `0x01010000..7` en tekent 256×256
  tegels (`0x4888db` e.v.); `0x488610` test posities tegen de rechthoek 64..576 × 40..460. Het hoort dus bij het
  minispel **Blackbox** (72 afbeeldingen in Blackbox.rck), niet bij de HUD. 196 608 = 512×384 → vermoedelijk 4
  maskers van 512×384 × 1 byte voor dat speelveld (**onzeker**). Voor de HUD/tekst-port niet nodig.

---------------------------------------------------------------------------------------------------

## 7. Recept voor de port (src/render_gl.c, src/main_engine.c; virtueel 640×480)

1. **Banken**: laad `Common/<karakter>.rck` afbeeldingen 61–64 en de strings; laad de font uit de levelbank.
   Afbeeldingen: BGRA → RGBA, rijen omdraaien (onderste eerst), bij `heeftAlfa == 0` alfa = 255. Font: 3 pagina's
   256×256 BGRA, niet omdraaien. Texturen: `GL_CLAMP_TO_EDGE`, `GL_LINEAR`, geen mipmaps.
2. **2D-pass** na de 3D-scène: ortho 0..640 × 0..480 (y omlaag), diepte-test uit, `glBlendFunc(SRC_ALPHA,
   ONE_MINUS_SRC_ALPHA)`. Eén functie `quad2d(x,y,w,h, sx,sy,sw,sh, c[4], tex)` met hoekvolgorde LB-boven,
   L-onder, R-onder, R-boven; kleur `rgb = min(255, 2·rgb)`, alfa ongewijzigd; `u = (sx+0.5)/W`, `v = (sy+0.5)/H`
   (in rechtopstaande textuurruimte). `tex = 0` → 1×1 wit.
3. **Font**: `k = S/40`; cel = 62k; per code: 0 stop, 4000 → `x = 0; y += 56k`, 1..n → quad
   `(x, y, w·k, 62k)` uit `(g.x, g.y, g.w, 62)` van pagina `g.page`, dan `x += adv·k`. Omdat de port al in 640×480
   virtueel tekent vervalt de sx/sy-stap. `measure()` = som van `adv·k`.
4. **Getallen**: `itoa` → codes `digit + 1` (cijfers zijn code 1..10), of via string 0.
5. **Tekstvak** (§3): struct `{state, t, size, var, n, line[3]{id,x,y}, rect}`; `msg_1080` doet de layout
   (let op: meten op `size`, tekenen op `size − 2`); per frame na de HUD: fade 0.5 s in, wachten op `vars[var] != 0`,
   0.5 s uit; achtergrond zwart met alfa/2, marge 16; afstandsregel `0x20001`.
6. **HUD** (§4.3/4.4), in deze volgorde: blauwe balk, rode balk + 5 slots (of race-variant), gezicht + levens,
   W/vlag + rondje + getal + "n / totaal", health-balletjes, (pauze of 1172:) $ en lading, krachtmeter. Getallen
   rood `(255,0,0)`, S = 17 (≥ 100: 12.75), gecentreerd op het anker. Sla de HUD over tijdens cinematics en de
   val-dood-camera. Animaties (§4.6) later.
7. **1172**: `hud.show_unique = 2` per ontvangst; elk frame aftellen.
8. **Volgorde**: 3D → HUD → tekstvak → (pauze-overlay + menu) → fades.

## 8. Common-stringtabel (Engels; index = lage 16 bits van `0x0002xxxx`)

0 "0123456789" · 1 "" (afstandsregel) · 2 Quit · 3 Are you sure? · 4 Continue · 5 Yes · 6 No · 7 % · 8 = · 9 / ·
10 : · 11 + · 12 CLEARED!! · 13 RESULTS · 14 TOTAL · 15 OK · 16 BACK · 17 HIGH SCORE · 18 FREE · 19 Start again ·
20 Press start · 21 Press a key · 22 New game · 23 Load game · 24 Select save · 25 Select game · 26–29 Save 1..4 ·
30 WOODY · 31 KNOTHEAD · 32 SPLINTER · 33 BONUS · 34 ??? · 35 Do you want to save? · 36 Options · 37 Volume ·
38 Sound FX volume · 39 Music volume · 40 "a a" · 41 SEE HIGH SCORES · 42 PLAY · 43 Game cleared · 44 Location ·
45 HIGH SCORES · 46 points · 47 Space · 48 Pirate · 49 House · 50 Mini Game · 51–54 Part A..D · 55 Race ·
56 GAME OVER · 57 NOT · 58 ACCESSIBLE · 59 Save failed. · 60 Load failed. · 61 Are you sure you want to overwrite
this save? · 62 Cancel · 63 Select · 64 No game saved on hard drive · 65 Choose new game · 66 "" · 67 Game Saved ·
68 Game Loaded · 69–100 PS2-memorycardteksten (ongebruikt op pc) · 101–103 Controller disconnected… ·
104–106 "If you want to leave the game / keep moving in this / direction." · 107 If you ever want to see Knothead
and Splinter again, · 108 I want $1,000,000! · 109 I'm holding them prisoner · 110 in my theme park. · 111 They'll
be no cash for you Buzz! · 112 I'm off to save Knothead and Splinter... · 113–115 To go through the door / stand
in front of it / and press the ACTION button. · 116–118 To play the Jackpot game, find some / dollars and press
the / ACTION button in front of the big slot machine. · 119–121 Behind this door is the Jackpot game. / Press the
ACTION button / to enter. · 122–123 To climb onto the wooden blocks / press the ACTION button repeatedly. ·
124–126 To take a ride on the rocket / stand next to it and / press the ACTION button. · 127 Level · 128 Seconds ·
129 Final Score · 130 Total Score : · 131 THE END · 132 Vibration · 133 On · 134 Off.
Levelbanken: één string "TOTO" (testrest). Volledige dump: `python tools/fontrender.py string <bank> all out.png <textbank>`.

## 9. Open vragen

1. HUD-animaties (§4.6): banen, duur en easing van de `0x4606xx..0x4624xx`-functies zijn niet uitgewerkt.
2. Baas-health (`0x47aca0..0x47b165`): hoofdlijnen gelezen; inschuifduur en exacte y-offsets niet.
3. Of Perso-kind 2/4 echt Knothead en 3/5 Splinter is (§4.2).
4. Resultatenscherm (`0x4549xx..0x455dxx`) en menu-layout (`0x45bxxx..0x4622xx`): alleen gelokaliseerd.
5. Textuurfilter van de 2D-laag en de afrondingsmodus van `ftol` (`0x499580`).
6. Sprite 15 (0,96,31,31 in afbeelding 63) lijkt leeg; gebruiker niet gevonden.
7. Rol van het level `Lang` bij de taalkeuze; mask.bin-indeling (4 × 512×384?).
8. Krachtmeter: dat sprite 11 óók wordt afgesneden staat in de code, maar het beoogde uiterlijk is niet tegen het
   origineel gecontroleerd.
