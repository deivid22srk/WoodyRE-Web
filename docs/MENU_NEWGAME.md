# MENU_NEWGAME.md — titelmenu: pagina 0, 1, 0x1c, 0x1f, attract-timer en New game → WWS (Woody.exe, build 17-10-2001)

Status: **statische analyse, instructie voor instructie nagelezen**; adressen = Woody.exe (imagebase 0x400000), na te
lezen met `python tools/drange.py START END`. Aanvulling op en correctie van TITLE.md §4/§5, GAMEFLOW.md §2..§6 en
CINEMATIC.md §3.2. Script: `out/house_code.txt` (object 115), `out/wws_code.txt` (object 297). Onzekere punten zijn
gemarkeerd met **onzeker**. De opties- en laadpagina's (0x1b, 0xb/2/3/7/0xf..0x11) staan elders; hier alleen de haakjes
waar pagina 1 ze aanroept.

## 0. Samenvatting

- **Invoer in menu's is gemengd: bevestigen = Enter *loslaten* of de springtoets *indrukken*; terug = Esc *loslaten* of
  de duiktoets (actie 5) indrukken.** Enter zet via `0x4033fb` actie 0xc alleen op de frame dat de toets losgaat
  (`[app+8]->vt[5](0x1b)` = "net losgelaten", interne code 0x1b = DIK_RETURN); de springtoets zet actie 4 + 0xc bij
  indrukken. Esc: toetsenbord-code 0 (= DIK_ESCAPE) "net losgelaten" → `vt[14]` (terug) in `0x4465f7`, en actie 9.
- **Pagina 0** ("Press a key", knippert): bevestigen of Esc (actie 9 losgelaten) → pagina 1. Terug doet niets. De
  **attract-timer** (35 s, alleen op pagina 0, nooit door toetsen teruggezet) start bij 0 de House-intro als demo.
- **Pagina 1** (New game / Load game / Options / Quit): bij binnenkomst **SoundFx 63**, cursor op New game, **geen
  invoervertraging**, **geen iris** (de ctor zet `+0x38 = 0`). **Terug en Esc doen niets** (slot 20 = lege functie; de
  handler kijkt niet naar actie 9). Omhoog/omlaag lopen rond, zonder geluid.
- **Quit** gaat naar **pagina 0x1c "Are you sure?"** (kop + Yes + No, cursor op **No**). Yes op de titel = fade-out
  0,5 s + muziek uit in 0,45 s + afsluiten (`0x404cb0`); Yes in een level = `RequestLevel(0.5, 0, 0, 0)` (naar de titel);
  No of terug = pagina 1 (titel) / pauzemenu (level).
- **New game**: validate geeft **2** (`SetVar(app+0x8c, 1)` = House-script object 115 start de intro), een frame later
  **1** (pagina 0x1f, `app+0x94 = 1`). De intro duurt 0,5 s zwart + 53,0 s. **Overslaan = actie 6 (aanval) of 9 (Esc)
  *losgelaten*** — niet Enter, niet de springtoets. Einde: attract-timer = 35, **`0x44ffa0` wist alleen de actieve
  save-struct in het geheugen** (alle drie personages: 9 levens, health 3.0, alle levelrecords 0), **er wordt geen
  Woody.sav-slot gelezen, gekozen of geschreven en er is géén "overschrijven?"-vraag**; `RequestLevel(0 of 0.5, 1, 1, 0)`
  → WWS als Woody (personage volgt uit de leveltabel), spawn op de `.ins`-positie van WWS-instantie 0
  (−86.5, 87.5, −793.0), want `GetPrevLevel` = 0 (House) laat het hub-script niets verplaatsen.
- **Iris** = zwarte ring van 50 quads in **virtueel 640×480**, middelpunt (320, 240), binnenstraal `0,99·480·v`,
  buitenstraal 475,2, dekkend zwart (vertexkleur (0,0,0,1)), geklipt op 0..640 × 0..480. Alleen getekend als
  paneelpagina-veld `+0x38 == 1`: op pagina 1 pas nadat je "Load game" koos en terugkomt.

## 1. Invoer

### 1.1 Actie-object `[0x5e6188]` = `app+0x14` (14 × {float waarde, float vasthoudtijd, u32 toestand})
PERSO_MOVE §3.2 klopt: `0x467340(dt)` begin frame (toestand &= bit 31), `0x4673b0(i, v)` zet (1 = net ingedrukt, 2 =
vast), `0x467370` einde frame (bit 31 = net losgelaten, precies één frame). Getters `0x467400` vast, `0x467420` net
ingedrukt (`== 1`), `0x467440` net losgelaten (bit 31). **`SetState 0x401400` wist alle 14 acties** (`0x467320`), dus
een toestandswissel neemt geen randen mee.

### 1.2 Toetsenbord-object `[0x5e6194]` = `app+8` (ctor `0x467d70`, vtable `0x4ab974`, DirectInput)
- Poll `0x467ef0`: 256 bytes DIK-status → **interne code = tabel `0x4b6f88`[DIK]**, voor de lage codes `DIK − 1`
  (tabel[1] = 0, tabel[0x1c] = 0x1b; alleen DIK 1 en DIK 0x1c leveren 0 resp. 0x1b; Numpad-Enter = 0x69).
- `0x4675a0` (vt[3]): `vorige = huidig`, poll, rand[i] = `vorige[i] && !huidig[i]`.
- **vt[4] `0x4675f0` = ingedrukt, vt[5] `0x467610` = net losgelaten** (niet "ingedrukt" zoals TITLE.md aannam).

### 1.3 Wat de menu's lezen
| wat | bron | wanneer | standaardtoets (PERSO_MOVE §3.1) |
|---|---|---|---|
| bevestigen = actie **0xc** net ingedrukt | `0x402ef7`/`0x403163` (samen met actie 4) | springtoets **indrukken** | LCtrl, joystickknop van actie 4 |
| | `0x4033fb`: `[app+8]->vt[5](0x1b)` → `0x4673b0(0xc, 1.0)` | **Enter loslaten** (DIK_RETURN) | vast, niet configureerbaar |
| terug = `vt[14]` | actie **5** net ingedrukt (`0x4465e2`) | duiktoets indrukken | Spatie |
| | `[0x5e6194]->vt[5](0)` (`0x4465f7`) | **Esc loslaten** (DIK_ESCAPE) | vast |
| omhoog / omlaag | acties 2 / 3 net ingedrukt | | ↑ / ↓ (joystick-as) |
| rechts / links | acties 1 / 0 (alleen sliders, vlag 0x10) | | → / ← |
| pagina 0 → 1, intro overslaan | actie **9** net **losgelaten** (`0x405062`, `0x4050b5`) | | Esc |
| intro overslaan | actie **6** net **losgelaten** (`0x4050a7`) | | LShift (aanval) |
| pauzemenu in het spel | actie 9 net ingedrukt (`0x403334`), alleen state 1/3 | | Esc |

Enter (DIK_RETURN) is in de standaard-cfg óók actie 7; geen menu leest actie 7.

## 2. Menu-frame en de gemeenschappelijke paginaklasse

### 2.1 `0x404e90` (App-state 0)
```c
hud_show(app->hud, page in {0x18,0x19} && !app->blackbox ? 1 : 2);     // 0x448450: 2 = HUD weg
switch (tab_405ae0[tab_405af8[page]]) { ... }                          // pauze/overlay, zie tabel
Game_Frame();                                                          // 0x401ab0: wereld, VM-tick, cinematic
if (overlay) RectVirtual(0,0,640,480, 0x80000000 x4, blank, 8);        // halfzwart
res = Menu_Update(app->menu, dt);                                      // 0x446440 -> 0x4464f0 (§2.2)
if (app->closing /*+0xcc*/) return;                                    // tijdens een levelwissel/afsluit-fade: GEEN handler
handler_405b1c[page](res);                                             // 0x404fe4 (0), 0x40519f (1), 0x4057a5 (0x1c), 0x40508c (0x1f)
```
Tabel `0x405af8` → `0x405ae0`: pagina 0..4, 0x1d, 0x1f, 0x20 → gepauzeerd als level ≠ 0, geen overlay; 5, 0x1e → niet
gepauzeerd, geen overlay; 6, 8, 9, 0xc..0xe, 0x12..0x15, 0x17 → overlay, niet gepauzeerd; 7, 0xa, 0xb, 0xf..0x11 → overlay
+ pauze als level ≠ 0; 0x16, 0x21 → pauze + volledig zwart; **0x18..0x1c → overlay en pauze alleen als level ≠ 0**. In
House dus nooit pauze of overlay: de cirkelcamera loopt op alle titelpagina's door.

### 2.2 `0x4464f0(font, dt)` — per frame, huidige pagina
```c
page->result = 0;                                   // +0x10 elke frame gewist
g_dt = dt;  blink += dt; if (blink > 0.5f) blink = 0;           // [0x5d7b1c], 0x4b39a4 = 0.5
Font_SetSize(font, 30);  font->color = 0xffffffff;              // 0x4b39a8; font+0x60
page->vt[1]();                                      // tekenen (+ bij paneelpagina's: uitgesteld resultaat, §2.5)
Logo_Draw();                                        // 0x446b00, §2.6
if (page->delay /*+8*/ > 0) page->delay -= dt;
else {
    if (P(2)) vt[11]();  if (P(3)) vt[10]();  if (P(1)) vt[12]();  if (P(0)) vt[13]();   // P = 0x467420
    if (P(0xc) && !page->given /*+0xc*/) page->result = vt[9]();
    if (P(5)) vt[14]();
    if (kbd && kbd->vt[5](0 /*Esc*/)) vt[14]();     // alleen in deze tak
}
page->given = 0;  return page->result;
```
Enter-van-pagina (`0x4464a0` → `vt[15]`), basis `0x4464c0`: `+0xc = 0`, `sel = 0`, `+8 = 0`; is item 0 een kop
(vlag 2) dan `vt[10]()` (omlaag, zet de knipperfase op 0).

### 2.3 Lijst tekenen `0x446640` (basis vt[1])
```c
S = vt[8]();                                        // 30
for (i = 0; i < n; i++) while (!(640 > Measure(str(i), S)) && (S -= 1) >= 15) ;   // één S per pagina
y = vt[7]() * 480;
for (i = 0; i < n; i++) {
    if (!(flags[i] & 2) && page->delay > 0) return; // lus STOPT bij het eerste gewone item zolang de vertraging loopt
    Font_SetSize(flags & 0x20 ? S*0.8f : S);  w = Measure(str(i));
    x = flags&4 ? 640-w-6.4f : flags&8 ? 6.4f : flags&0x80 ? 160-w/2 : flags&0x40 ? x : 320-w/2;
    if (flags & 0x10) { ...slider-tekst... }        // niet op deze pagina's
    if (!(i == sel && blink < 0.25f)) Font_Draw(x, y, str(i));   // [0x5d7b00] = 0.5*0.5
    y += CellH() + Extra();                         // 0x441980 + 0x441a50
}
```

### 2.4 Navigatie (basisslots)
- **omhoog `0x446920`**: `sel--`, onder 0 → `count−1`, koppen overslaan; **zet de knipperfase altijd op 0**, ook als er
  niets verschoof (TITLE.md zei "0.25 bij mislukt" — dat geldt alleen voor omlaag).
- **omlaag `0x446970`**: `sel++`, boven → 0, koppen overslaan; fase = 0 als verschoven, anders 0,25.
- rechts/links `0x4469d0/0x446a20`: alleen items met vlag 0x10 (±5, 0..100), fase 0,25.
- terug `0x446a70`: `result = 24`.  validate `0x446aa0`: `flags & 1 ? item.result : 0`.
- Na een geslaagde stap is het nieuwe item dus eerst **0,25 s onzichtbaar**, dan 0,25 s zichtbaar.

### 2.5 Paneelpagina (basis `0x45b830`, vtable `0x4ab2e4`) — pagina 1 erft hiervan
| veld | betekenis |
|---|---|
| `+0x14` | uitgesteld resultaat |
| `+0x18` | soort: 0 → levert 24 (terug), 1 → levert `+0x14` |
| `+0x1c` | tijd sinds enter; `+0x23`/`+0x21` gaan uit na 0,5 s (`0x4ab2e0`) |
| `+0x22` | "sluit": wacht tot `+0x28 ≥ +0x34`, dan `vt[18]` (levert het resultaat, `0x45baa0`) |
| `+0x23` | "opent" (eerste 0,5 s) |
| `+0x24` | **vergrendeld**: validate/omhoog/omlaag/terug doen niets (`0x4600e0`, `0x45bb90/0x45bba0/0x45bb30`) |
| `+0x28` | tijd sinds validate/enter |
| `+0x2c` | iris-object (0x10 B) · `+0x30` iris-"open"-waarde · `+0x34` duur (0,5) · `+0x38` byte iris actief |

Ctor `0x45b830`: `+0x38 = 1`; **ctor pagina 1 `0x460040`: `+0x38 = 0`**.
Enter basis `0x45b8c0`: basis-enter, `+0x22 = +0x24 = 0`, `+0x30 = 0.37`, `+0x34 = 0.5`, **`+8 = 0.5`** (invoervertraging),
`+0x28 = 0`, **`SoundFx(0x3f, 0)`** (`0x45b8ef`, 2D), iris(0 → 0.37, 0.5 s), `+0x1c = 0`, `+0x23 = +0x21 = 1`, `+0x20 = 0`.
Tekenen `0x45b990`:
```c
if (opens && t28 == 0 && iris_on) Iris_Draw(0);         // eerste frame: volledig zwart
t28 += dt;  t1c += dt;  if (opens && t1c >= 0.5f) opens = opt21 = 0;
vt[16]();                                                // pagina 1: 0x4600b0 = lijst + logo-fade-in (§2.6)
if (iris_on) { if (open30 > 0) Iris_Anim(dt); else RectVirtual(0,0,640,480, 0xfe000000 x4); }
if (closing && t28 >= dur) vt[18]();                     // resultaat afleveren -> +0x10, +0xc = 1 (blokkeert bevestigen)
if (!locked || t28 < dur) vt[17]();                      // pagina 1: leeg
```

### 2.6 Logo (`0x446b00`, na `vt[1]`, alleen als `app+0x68 == 0`)
`v = [0x5d7b28]`, `A = −ftol(v·0.2·−254)` (dus `trunc(50.8·v)`), `RectVirtual(216,16,209,247, 0,0,209,247,
0x808080|A<<24, bank-1-afbeelding 1, 8)`, daarna `v −= 5·dt` (ondergrens 0). Pagina 0 (`0x45bd10`) en pagina 1
(`0x4600b0`, **niet** zolang `+0x22` of zolang de iris actief is én `+0x23`) doen vóór het tekenen `v += 10·dt`
(bovengrens 5). Netto: 1,0 s in op pagina 0/1, **1,0 s uit op elke andere pagina (dus ook 0x1c)**; direct 0 bij New
game en Load game (`0x460127`, `0x460150`) en bij het binnenkomen van 0x18/0x19/0x1e/0x1f (`0x45b390`).

### 2.7 Iris (`0x4776b0` zet, `0x477920` animeert, `0x4776d0` tekent, `0x482cf0` 2D-polygoon)
```c
void Iris_Set(Iris *s, float from, float to, float dur) { s->from = from; s->to = to; s->dur = dur; s->t = 0; }  // 0x4776b0
bool Iris_Anim(Iris *s, float dt) {                                 // 0x477920
    s->t += dt; float f = s->t / s->dur; if (f > 1) f = 1;
    Iris_Draw(s->from - (s->from - s->to) * f);  return f >= 1; }
void Iris_Draw(float v) {                                           // 0x4776d0
    for (float i = 0; i < 1.0f; i += 0.02f) {                       // 50 segmenten (0x4aa1b0)
        a0 = i*512, a1 = (i+0.02f)*512;                             // cos-tabel [0x5e823c], sin = cos(a+128)*-1
        c = 0.99f*cos, s = 0.99f*sin;                               // 0x4ab7d8 / 0x4abd44
        quad { (320+480v·c0, 240+480v·s0), (320+480v·c1, 240+480v·s1),
               (320+480·c1,  240+480·s1),  (320+480·c0,  240+480·s0) }, kleur (0,0,0,1) op vertex+0x24..0x30
        0x482cf0(4, verts, tex 0 -> [0x5e8684], vlag 8);
    }
}
```
`0x482cf0` klipt elke polygoon op x 0..640 (`0x4aaba8`) en y 0..480 (`0x4ab248`) en schaalt dan met `W/640`, `H/480`:
**virtuele 640×480-coördinaten**. Kleur ×255 (`0x4aa308`) → dekkend zwart. De schermhoeken liggen op 400 van het
midden, de buitenrand op 475,2: bij `v = 0.85` is het gat 403,9 → onzichtbaar; bij 0 → volledig zwart.
Tekenvolgorde binnen de menulaag: items → iris → logo.

## 3. Pagina 0 — titel (vtable `0x4aa9c8`, 0x14 B, geen ctor, handler `0x404fe4`)
- Items `0x4b5da8`: `{0x20015 "Press a key", vlag 1, resultaat 5}`; y = 0,7·480 = **336**, gecentreerd, S = 30, wit
  (`0xffffffff`). Het is het geselecteerde item ⇒ **knippert**: 0,25 s weg / 0,25 s zichtbaar.
- Enter = basis `0x4464c0` (geen geluid, geen vertraging); tekenen `0x45bd10` = lijst + logo-fade-in.
- Handler:
```c
bool was = attract > 0;  attract -= dt;                             // app+0x98, 0x4a9004 = 0
if (attract <= 0) {
    if (was) SetVar(app->introVar /*+0x8c*/, 1);                    // zelfde start als New game
    if (attract < -0.5f) { app->newGame = 0; Menu_SetPage(0x1f); }  // 0x4a94bc
    return;                                                         // invoer (ook resultaat 5) genegeerd
}
if (Released(9)) Menu_SetPage(1);                                   // Esc
if (res == 5)    Menu_SetPage(1);                                   // bevestigen; resultaat 24 (terug) genegeerd
```
Toetsen die werken: Enter (loslaten), springtoets (LCtrl, indrukken), joystickknop van actie 4, Esc (loslaten). Andere
toetsen doen niets, ondanks de tekst.

## 4. Pagina 1 — hoofdmenu (vtable `0x4ab5c0`, 0x3c B, ctor `0x460040`, handler `0x40519f`)
Items `0x4b5e70`, y = 0,55·480 = **264**, dan per regel `CellH + Extra` (46,5 bij S = 30), gecentreerd, wit:

| i | string | item-resultaat | validate `0x4600e0` (tabel `0x460194`) | handler |
|---|---|---|---|---|
| 0 | 22 New game | 1 | `+0x14 = 1`, `+0x38 = 0`, `+0x28 = +0x34 + 1 = 1.5`, **logo 0**, **return 2** | 2 → `SetVar(app+0x8c, 1)` (`0x4051b0`); volgende frame 1 → `app+0x94 = 1`, pagina **0x1f** (`0x4051c5`) |
| 1 | 23 Load game | 3 | `+0x38 = 1`, `+0x14 = 3`, `+0x28 = 0`, iris(0.85 → 0, 0.5 s), **logo 0**, return 0 | na 0,5 s 3 → `0x4051da` (laadpagina's) |
| 2 | 36 Options | 6 | `+0x28 = 1.5`, `+0x14 = 6`, `+0x38 = 0` | volgende frame 6 → pagina **0x1b** |
| 3 | 2 Quit | 7 | `+0x28 = 1.5`, `+0x14 = 7`, `+0x38 = 0` | volgende frame 7 → pagina **0x1c** |

Alle vier zetten eerst `+0x22 = +0x24 = 1`, `+0x18 = 1` (`0x4600f9..0x4600ff`); bij `+0x24` al gezet doet validate niets.
`vt[18] = 0x4601b0`: resultaat afleveren (`0x45baa0`) en bij resultaat 1 `+0x38 = 0`.
- Enter `0x460070`: paneel-enter (sel 0, **SoundFx 63**, …), dan **`+8 = 0`** (direct bedienbaar, lijst meteen zichtbaar),
  `+0x30 = 0.85`, iris(0 → 0.85, 0.5 s). Dat geluid speelt **elke** keer dat pagina 1 opent: vanaf pagina 0, na "No" op
  0x1c, na terugkomst uit Options/Load.
- Omhoog/omlaag rondlopend (4 items, geen koppen); geblokkeerd na een validate (`+0x24`).
- **Terug (Spatie, Esc loslaten) doet niets** (`0x45bb30` → slot 20 = `0x462c60` = `ret`); de handler kijkt niet naar
  actie 9. Er is geen weg van pagina 1 terug naar pagina 0.
- Iris op pagina 1: ctor `+0x38 = 0` en enter raakt `+0x38` niet ⇒ niet getekend, tenzij een eerdere "Load game" hem op
  1 liet staan; dan opent hij bij terugkomst van 0 naar 0,85 in 0,5 s en is de logo-fade-in de eerste 0,5 s uit.

## 5. Pagina 0x1c — "Are you sure?" (vtable `0x4aab08`, 0x14 B, handler `0x4057a5`)
- Items `0x4b5db8`: `{3 "Are you sure?", vlag 2 (kop)}`, `{5 "Yes", 1, 8}`, `{6 "No", 1, 9}`; y = 0,55 → **264, 310,5,
  357**; S = 30, gecentreerd. Tekenen = basislijst (**geen logo-fade-in ⇒ het logo vervaagt in 1 s**), geen geluid,
  geen iris, `+8 = 0`.
- Enter `0x45bd40`: basis-enter (item 0 is kop ⇒ omlaag ⇒ sel 1, knipperfase 0), dan **`sel = 2` ("No")**. "No" is
  dus eerst 0,25 s onzichtbaar.
- Omhoog/omlaag: tussen Yes en No (kop overgeslagen), rondlopend.
- Handler:
```c
if (res == 8) {                                          // Yes
    if (app->level == 0 || (cfg->dev & 4)) Quit(0.5f);   // 0x404cb0
    else RequestLevel(0.5f, 0, 0, 0);                    // naar House / titel (pagina 0 + menumuziek)
} else if (res == 9 || res == 24) {                      // No of terug (Spatie / Esc los)
    if (app->level == 0) Menu_SetPage(1);                // SoundFx 63 opnieuw
    else Pause_Open();                                   // 0x404d80: pagina 0x18/0x19, geluid onderbroken
}
```
`Quit 0x404cb0(0.5)`: `+0xcc = +0xcd = 1`, `+0xd4 = 0`, `+0xd0 = 0.5`, fade naar zwart 0,5 s (`0x401480`), muziek
`vt[0x4c](0.45)` + rtc-stream stop `vt[0x9c]`. Tijdens de fade draaien er geen pagina-handlers (`0x404f71`). Na de fade:
unload, `app+4 = 1` → WinMain schrijft Woody.cfg en sluit. In een level ligt er (tabel `0x405af8`) een halfzwart vlak
onder de pagina en staat de wereld stil; in House niet.

## 6. Attract-timer en pagina 0x1f

### 6.1 Timer `app+0x98`
Gezet op **35,0** (`0x4a9000`) bij boot (`0x402653`) en aan het eind van pagina 0x1f (`0x4050c2`), **nergens anders**:
niet bij het laden van House, niet bij een toets, niet bij het wisselen naar pagina 1. Hij loopt alleen op pagina 0 en
alleen als er geen levelwissel/afsluit-fade loopt. Wie na een level terugkeert naar de titel krijgt de restwaarde.
Verloop op pagina 0: bij de overgang naar ≤ 0 `SetVar(var1, 1)` (onvoorwaardelijk: het script-blok reageert op elke
wijziging naar 1, ook na een eerdere intro met var1 = 4), bij < −0,5 pagina 0x1f met `app+0x94 = 0`.

### 6.2 Pagina 0x1f (vtable `0x4aa4b4`, 0x14 B, handler `0x40508c`)
Item `0x4b5cd8`: `{0x20001 "" , vlag 1, resultaat 9}` op y = 0,6 (`0x4a9650`) — onzichtbaar; enter `0x45b390` =
basis-enter + logo 0; tekenen = basislijst (geen logo-fade-in). Bevestigen levert 9, dat de handler negeert.
```c
int v = *GetVar(app->introVar);                           // 0x443cd0
if (v != 4 && !Released(6) && !Released(9)) return;       // actie 6 = aanval (LShift), 9 = Esc; LOSLATEN
app->attract = 35.0f;
if (app->newGame /*+0x94*/) {                             // --- New game ---
    Save_Reset(app->save /*+0x48*/);                      // 0x44ffa0, §7.3
    RequestLevel(v == 4 ? 0.0f : 0.5f, 1 /*WWS*/, 1 /*spel*/, 0);
    return;                                               // script, cinematic, tekstvak lopen door tot de unload
}
if (v != 4) {                                             // --- attract afgebroken ---
    Eko_CancelTimers(app->introObj /*+0x90*/);            // 0x444380 + 0x4441d0: DELAY- en DURING-lijst van object 115
    SetVar(app->introVar, 4);
    Cin_Abort(game + 0x64);                               // 0x44f290: reset + muziek hervatten (timer*0.9) + rtc-stream stop
    Perso->vt[0x44]();                                    // 0x44ab20: reset, toestand 0 -> 0x44e690 start actie 0x49 opnieuw
}
for (i = 0; i < 4; i++) Fader_Clear(&app->fader[i]);      // 0x445bc0
TextBox_Close(game + 0x18);                               // 0x456ec0
DrawBlack();  FadeIn(0.5f);                               // 0x4014c0, 0x401440
TitlePage();                                              // 0x404e30: menumuziek wisselen + pagina 0 + state 0
```
`app+0x90` = tweede argument van 1160 & 0xffffff = **0x73 = scriptobject 115** (`0x44496d`).

### 6.3 Menumuziek `0x404e30`
`app+0x54` wordt bij elke LoadLevel op 0 gezet (`0x4041b0`). `0x404e30`: `n = app+0x54 + 1; if (n == 2) n = 0;` →
`n == 1` ⇒ **PlayMusic(0) "Menu"**, `n == 0` ⇒ **PlayMusic(48) "Menu02"**. Na elke load van House (boot, terug uit een
level) is het dus eerst **track 0**; alleen een tweede aanroep zonder load (einde van een attract) geeft 48, de derde weer 0.
Aanroepers: `0x401575` (boot), `0x4017c9` (na een levelwissel met state 0 en pagina 0), `0x405196` (einde attract).

## 7. New game, van bevestigen tot WWS

### 7.1 Tijdlijn
| wanneer | wat | adres |
|---|---|---|
| frame N | bevestigen op "New game" (Enter los / springtoets in). Validate → **2**; logo `v = 0` (weg); handler `SetVar(var1, 1)` | `0x46010f`, `0x4051b0` |
| frame N+1 | `Game_Frame` draait vóór het menu: VM-tick, object 115: `var1 = 2`, `var2 = −1`, `1131 [0, 72]`, 8× `1132` (122, 123, 120, 119, 121, 116 anim 0; 117, 118 anim 72), **`1130 [0x73, 8, var2]`**; `DELAY 100` → na 1 s `57 [0,10000]` + `56 [0,0]` (Woody-instantie direct dekkend) | `out/house_code.txt` 1243..1361 |
| | 1130: cin-toestand 1, fade naar zwart 0,4 s, menumuziek **pauzeren** in 0,45 s (`vt[0x50]`), rtc-track 8 `/Rtc/Menu.wav` klaarzetten | CINEMATIC §2 |
| | menu: pagina 1 levert **1** → `app+0x94 = 1`, pagina 0x1f (enter: logo 0). Pagina 1 is precies één frame langer zichtbaar | `0x4051c5` |
| +0,5 s | cin-toestand 2: fade-in 0,5 s, **`/Rtc/Menu.wav`** start, `var2 = 0`, Woody (inst 0) op de vector van slot 115, anim 72 op snelheid 3, camera mode 0x80 **met** letterbox; Perso-update overgeslagen; de cirkelcamera stopt | CINEMATIC §3 |
| +9,0 s / +11,9 s | `1151 [300]` fade-out 3 s; acteurs wisselen, `1152` 1,15 s zwarte frames | T = 900 / +290 |
| +42,0 / 46,0 / 49,5 s | tekstvakken 1080 onderaan gecentreerd: 107+108, 109+110, 111+112 (sluiten op 45,0 / 49,0 / 52,5 s) | T = 4200..5250 |
| +52,5 s | cin-toestand 3: fade naar zwart 0,5 s | CINEMATIC §3 |
| +53,0 s | script: `var1 = 4`, `DURING 10` `1152` (0,1 s zwart), var4 = 1 (tekst dicht); cin → toestand 4 (Perso-reset, menumuziek hervatten 0,45 s) | T = 5300 |
| volgende frame(s) | pagina 0x1f ziet `var1 == 4`: attract = 35, `Save_Reset`, **`RequestLevel(0, 1, 1, 0)`** (geen extra fade: het beeld is al zwart) | `0x4050ff` |
| overslaan | op elk moment na frame N+1: **actie 6 of 9 loslaten** → attract = 35, `Save_Reset`, **`RequestLevel(0.5, 1, 1, 0)`**: 0,5 s fade-out over de doorlopende cinematic (tekst, rtc-muziek lopen door), muziek `StopMusic(0.45)` + rtc stop | `0x405786` |

Enter en de springtoets slaan **niet** over (Enter → actie 0xc → validate van 0x1f = 9, genegeerd).

### 7.2 `RequestLevel(fade, 1, 1, 0)` en aankomst
`0x404b60`: `+0xd4 = +0xcc = 1`, `+0xd0 = fade`, fade-out (`0x401480`), `vt[0x4c](fade·0.9)` (muziek stop) + `vt[0x9c]()`
(rtc stop), doel = (1, state 1, pagina 0). Na de fade (`0x401590`): unload House, `LoadLevel(1)`: `app+0x6c = 0`,
`app+0x68 = 1`, **personage = `byte_404830[1]` = 0 = Woody** (`cfg+0x380`), `app+0x54 = 0`, script-init, Perso-init
uit de save-struct (blok 0: 9 levens, health 3.0, 0 items, 0 ladingen), fade-in **1,0 s** (`0x401440(1.0)`) en de
eerste 4 frames zwart; daarna `SetState(1)` (alle acties gewist, huidige menupagina = NULL).
**Spawn**: WWS-instantie 0 (`1200 [0x1000000, 1]`, model 0) op haar `.ins`-positie **(−86.49, 87.48, −792.99)** met de
`.ins`-richting (`0x44a3d0`). Object 297: `1655 [1]` (/Game/WS.wav), `DELAY 1` → `1084 GetPrevLevel(var51)` = **0**
(House) ⇒ geen `1140` (geen deur-plaatsing, geen resultatenscherm); de WWS-cinematic (object 344, `var59 = 1`) start
alleen als `var51 == 10` (terug uit W3D). Dus: Woody staat gewoon op het hub-startpunt, volgcamera.

### 7.3 `0x44ffa0` Save_Reset (actieve save-struct `app+0x48`, 0x14a4 B)
```c
s->ver = 0x11004;  s->u8 = 0;                                   // +4, +8
for (c = 0; c < 3; c++) {                                       // Woody, Knothead, Splinter (+0xc + c*0x6dc)
    blk->lives = 9;  blk->unique = 0;  blk->charges = 0;  blk->health = 3.0f;
    for (L = 0; L < 29; L++) {                                  // rec = blk + 0x10 + L*0x3c
        rec->best = 0;  rec->done = 0;  memset(rec->got, 0, 32);   // +0, +4, +5..+0x24
        memset(rec->stats, 0, 20);                              // +0x28..+0x38 (bytes +0x25..+0x27 blijven staan)
    }
}
s->u14a0 = 0;  s->checksum = signed_byte_sum(s);                // 0x450030
```
Dat is alles. **Niet aangeraakt**: de slot-manager `app+0x4c` (4 slots uit Woody.sav), het bestand Woody.sav, de
opslaan-slotindex `app+0x60` (wordt pas op pagina 5 gekozen), `cfg+0x380` (het personage volgt uit het level). Er is dus
**geen slotkeuze en geen overschrijf-vraag bij New game**; die komt pas bij het eerste opslaan na een level
(pagina 6 → 5 → bezet slot → 0x17 "…overwrite this save?"). Een oude Woody.sav blijft ongemoeid tot de speler daar
"Yes" kiest.

## 8. Correcties op bestaande docs

1. TITLE.md §5.1 / §7.4: `[0x5e6194]->vt[5](0)` is **Esc *losgelaten*** (vt[5] = losgelaten-rand `0x467610`, interne
   code 0 = DIK_ESCAPE). Enter werkt als bevestigen via `0x4033fb` (**Enter losgelaten** → actie 0xc), niet via de cfg.
2. TITLE.md §5.1: "een mislukte [verplaatsing] op 0.25" geldt alleen voor **omlaag**; omhoog zet de fase altijd op 0.
   Ook: de lijst **stopt** bij het eerste niet-kop-item zolang `+8 > 0` (latere koppen worden dan ook niet getekend).
3. TITLE.md §5.4: de iris-kleur is **vastgesteld**: dekkend zwart (vertexkleur (0,0,0,1) × 255, `0x482fb7..`),
   coördinaten virtueel 640×480, geklipt op het virtuele scherm (`0x482d25..0x482d8c`). Bovendien tekent het paneel op
   de eerste frame (`+0x28 == 0`, iris actief) de iris op 0 = volledig zwart.
4. TITLE.md §5.4 / §6: pagina 1 enter zet `+8 = 0`, maar de **paneel-basis zet 0,5 s** (`0x45b8e0`); dat geldt voor
   andere paneelpagina's (o.a. 0x1e), niet voor 0, 1, 0x1c, 0x1f (allemaal 0).
5. TITLE.md §6-recept: "Yes = fade 0.5 s + afsluiten" alleen op de titel; in een level = naar de titel. "No" en terug →
   pagina 1 met SoundFx 63.
6. TITLE.md §4.2 / GAMEFLOW §5: toets 6/9 = **loslaten**; bij New game (`+0x94 = 1`) wordt er **niets** gestopt (geen
   `0x444380`, geen `0x44f290`, geen Perso-reset): alleen save-reset + RequestLevel. CINEMATIC.md §3.2 ("alleen de
   House-intro kan worden afgebroken ... script-object stoppen ...") geldt alleen voor het attract-pad.
7. SOUND.md §4 ("track 48 bij 0, track 0 bij 1") klopt, maar de volgorde in de praktijk is: na elke House-load eerst
   **track 0**, pas na een attract 48 (§6.3).
8. GAMEFLOW §4.2/§5: `SetState 0x401400` wist ook alle invoeracties (`0x467320`) en zet bij state 1 de huidige
   menupagina op NULL en het geluid weer aan (`+0x10 = 1` van `[app+0x20]`).
9. GAMEFLOW §5-tabel pagina 0: "Toets 9 → pagina 1" = Esc **losgelaten**; pagina 0x1c: resultaat 24 (terug) = "No".

## 9. De port vergeleken (`src/main_engine.c` ~r. 1397..1425, `src/hud.c` r. 592..636, `src/render_gl.c` r. 21)

| # | origineel | port nu | recept |
|---|---|---|---|
| 1 | Esc = actie 9 / menu-terug (loslaten); pauzemenu in het spel | `WM_KEYDOWN VK_ESCAPE → w->quit = 1` (render_gl.c) | quit-regel weg (of voorlopig alleen `g_level != 0` zolang het pauzemenu 0x18 niet geport is); Esc als rand lezen (hieronder) |
| 2 | bevestigen = Enter **los** of springtoets **in** | Enter of Spatie **in** | `m_ok = REL(VK_RETURN) \|\| PRS(VK_SPACE)` (Spatie = de springtoets van de port) |
| 3 | pagina 0: ook Esc los → pagina 1 | `(win.keys[VK_ESCAPE] && 0)` | `if (m_ok \|\| REL(VK_ESCAPE)) page1_enter();` |
| 4 | attract-timer 35 s, alleen pagina 0 | ontbreekt | §9.2 |
| 5 | pagina 1: terug/Esc doet niets | idem (geen code) | niets doen — maar Esc mag de port niet meer sluiten (#1) |
| 6 | Quit → pagina 0x1c, cursor No; Yes = fade 0,5 s + muziek 0,45 s + afsluiten | `win.quit = 1` direct | §9.3 |
| 7 | New game: `SetVar(var1, 1)` **altijd** | alleen als `*iv == 0`, anders intro overgeslagen | `if (iv) eko_set_var(&L.vm, g_intro_var, 1); else new_game_pending = 2;` |
| 8 | intro overslaan: actie 6 of 9 **losgelaten** | Enter/Spatie ingedrukt | `if (REL(VK_ESCAPE) \|\| REL(VK_CONTROL) \|\| REL(VK_SHIFT))` (de aanvalstoetsen van de port = actie 6) |
| 9 | natuurlijk einde: `RequestLevel(0, …)` | `request_level(1, 0.5f)` | `request_level(1, *iv == 4 ? 0.0f : 0.5f)` (request_level klemt al op 0,01) |
| 10 | Save_Reset in het geheugen, **geen** schrijfactie | `save_reset(); save_write();` — overschrijft woodyre.sav meteen | `save_write()` weghalen; Load game moet dan het bestand zelf opnieuw lezen (`save_read()`), zie de laad-analyse |
| 11 | menumuziek: na elke House-load track 0, na een attract 48 | `title_n++ & 1 ? 0 : 48`, statisch over loads ⇒ de eerste titel speelt 48 | §9.4 |
| 12 | logo: +10·dt/−5·dt, 1 s uit op andere pagina's, direct 0 bij New game/Load/0x1f | `else H.logo_v = 0` (altijd direct) | §9.5 |
| 13 | knipperfase: omhoog → 0, omlaag → 0 / 0,25; >0,5 → 0 | geen reset bij navigeren; `-= 0.5` | `hud_menu_blink(0.0f)` na elke stap (omlaag zonder verschuiving 0,25); wrap `if (t > 0.5f) t = 0` |
| 14 | SoundFx 63 bij **elke** binnenkomst van pagina 1 | alleen 0 → 1 | via `page1_enter()` (ook na 0x1c-No, Options, Load) |
| 15 | pagina 1 levert New game/Options/Quit één frame later | direct | optioneel: één frame uitstel (`p1_defer`); onzichtbaar verschil behalve één extra frame met menu |
| 16 | na afloop/afbreken van een attract: cirkel weer op fase 0; Woody alleen zichtbaar in cin 2..3 | `title_t` loopt door; `visible = state >= 2`; orbit alleen bij `state < 2` | `title_t = 0` bij het attract-einde; `visible = (state == 2 \|\| state == 3)`; orbit bij `state != 2 && state != 3` |
| 17 | menutekstkleur `0xffffffff` (MODULATE2X ⇒ textuur ×2, verzadigd) | `0xff808080` (×1; `set_col` klemt op 1) | **onzeker**: alleen zichtbaar als de fontglyphs niet wit zijn; zo ja: fontquads met `GL_COMBINE`/`RGB_SCALE 2` |
| 18 | iris | ontbreekt | pas nodig bij Load-terugkeer; `hud_iris(v)` §9.6 |

`--enter T` (synthetisch bevestigen) en de port-toets "L = continue" zijn testhaken; laten staan.

### 9.1 Randen
```c
/* main_engine.c, bij de andere *_prev-arrays; einde van de frame: memcpy(key_prev, win.keys, sizeof key_prev) */
static unsigned char key_prev[256];
#define PRS(k) (win.keys[k] && !key_prev[k])                 /* 0x467420 */
#define REL(k) (!win.keys[k] && key_prev[k])                 /* 0x467440 / toetsenbord vt[5] */
int m_ok   = REL(VK_RETURN) || PRS(VK_SPACE) || syn;          /* actie 0xc */
int m_back = REL(VK_ESCAPE);                                  /* vt[14]; + PRS(duiktoets) zodra actie 5 bestaat */
int m_up = PRS(VK_UP) || PRS('W'), m_dn = PRS(VK_DOWN) || PRS('S');
int a9_rel = REL(VK_ESCAPE), a6_rel = REL(VK_CONTROL) || REL(VK_SHIFT);
```

### 9.2 Titel-toestandsmachine (vervangt r. 1411..1422)
```c
static float g_attract = 35.0f;          /* app+0x98: NIET resetten in level_load */
static int g_newgame, g_intro_obj;       /* app+0x94; 1160 arg 2 & 0xffffff (case 1160: g_intro_obj = m->args[1] & 0xffffff) */
static int g_quitting; static float g_quit_t;

static void page1_enter(void) { title_page = 1; title_sel = 0; audio_fx(63, NULL, NULL); }   /* 0x460070 */

if (g_level == 0 && !fly && g_next_level < 0 && !g_quitting) {        /* 0x404f71: geen handler tijdens een fade */
    int32_t *iv = ...;                                                 /* zoals nu */
    switch (title_page) {
    case 0: { int was = g_attract > 0; g_attract -= dt;
        if (g_attract <= 0) { if (was && iv) eko_set_var(&L.vm, g_intro_var, 1);
                              if (g_attract < -0.5f) { g_newgame = 0; title_page = 0x1f; hud_logo_off(); } }
        else if (m_ok || a9_rel) page1_enter();
        break; }
    case 1:
        if (m_up) { title_sel = (title_sel + 3) % 4; hud_menu_blink(0); }
        if (m_dn) { title_sel = (title_sel + 1) % 4; hud_menu_blink(0); }
        if (m_ok) switch (title_sel) {
            case 0: hud_logo_off(); g_newgame = 1; title_page = 0x1f;
                    if (iv) eko_set_var(&L.vm, g_intro_var, 1); break;     /* resultaat 2, dan 1 */
            case 1: /* Load game: laad-analyse */ break;
            case 2: /* Options 0x1b: opties-analyse */ break;
            case 3: title_page = 0x1c; title_sel = 2; hud_menu_blink(0); break;
        }
        break;                                                         /* terug: niets */
    case 0x1c:
        if (m_up || m_dn) { title_sel = title_sel == 1 ? 2 : 1; hud_menu_blink(0); }
        if (m_ok && title_sel == 1) { g_quitting = 1; g_quit_t = 0.5f; fade_start(0.5f, 1); audio_music_stop(0.45f); audio_rtc(-1); }
        else if ((m_ok && title_sel == 2) || m_back) page1_enter();
        break;
    case 0x1f: {
        int v = iv ? *iv : 4;
        if (v != 4 && !a6_rel && !a9_rel) break;
        g_attract = 35.0f;
        if (g_newgame) { save_reset(); request_level(1, v == 4 ? 0.0f : 0.5f); g_newgame = 0; break; }
        if (v != 4) { eko_cancel_timers(&L.vm, g_intro_obj); eko_set_var(&L.vm, g_intro_var, 4);
                      memset(&g_cin, 0, sizeof g_cin); audio_rtc(-1); audio_music_pause(0, 0.45f); }
        memset(&g_sfade, 0, sizeof g_sfade); hud_text_reset(); g_black_frame = 1; fade_start(0.5f, 0);
        title_t = 0; title_page0();                                    /* §9.4 */
        break; }
    }
}
if (g_quitting && (g_quit_t -= dt) <= 0) win.quit = 1;                /* 0x404cb0 -> app+4 */
```
In een level (pauzemenu, als dat geport wordt) gaat 0x1c-Yes naar `request_level(0, 0.5f)` en No/terug naar het
pauzemenu. `new_game_pending` vervalt; de `syn`-testhaak kan `m_ok` blijven zetten.

### 9.3 hud.c: pagina 0x1c tekenen
In `hud_title_draw`: `else if (page == 0x1c) { static const uint32_t items[3] = { 3, 5, 6 }; page_items(items, 3, 0.55f, sel); }`
(kop 3 wordt nooit geselecteerd, dus nooit verborgen). Pagina 0x1f tekent niets.

### 9.4 Menumuziek
```c
static int g_title_music;                                   /* app+0x54; in level_load op 0 */
static void title_page0(void) {                             /* 0x404e30 */
    if (++g_title_music == 2) g_title_music = 0;
    audio_music(g_title_music == 1 ? 0 : 48);
    title_page = 0; title_sel = 0;
}
```
In `level_load`: `g_title_music = 0;` en voor `g_level == 0` `title_page0()` i.p.v. `audio_music((title_n++ & 1) ? 0 : 48)`.

### 9.5 Logo
```c
void hud_logo_off(void) { H.logo_v = 0; }
/* in hud_title_draw, vervangt de laatste regels: */
if (want_logo /* page 0 of 1 */) { H.logo_v += 10 * dt; if (H.logo_v > 5) H.logo_v = 5; }   /* 0x446ac0, vóór het tekenen */
... tekenen met alpha = (int)(50.8f * H.logo_v) ...
H.logo_v -= 5 * dt; if (H.logo_v < 0) H.logo_v = 0;                                        /* 0x446b00, na het tekenen */
```
`want_logo` = pagina 0 of 1 (en op pagina 1 niet terwijl de iris opent). Bij New game, Load game en pagina 0x1f
`hud_logo_off()`. Knipperfase: `void hud_menu_blink(float t) { H.menu_t = t; }` en in `hud_title_draw` /
`hud_menu_page` `H.menu_t += dt; if (H.menu_t > 0.5f) H.menu_t = 0;`.

### 9.6 Iris (voor de laad-terugkeer)
```c
void hud_iris(float v)                                       /* 0x4776d0, virtueel 640x480 */
{
    glDisable(GL_TEXTURE_2D); glColor4f(0, 0, 0, 1); glBegin(GL_QUADS);
    for (int k = 0; k < 50; k++) {
        float a0 = k * (2 * 3.14159265f / 50), a1 = (k + 1) * (2 * 3.14159265f / 50), r = 0.99f * 480.0f;
        glVertex2f(320 + r * v * cosf(a0), 240 + r * v * sinf(a0)); glVertex2f(320 + r * v * cosf(a1), 240 + r * v * sinf(a1));
        glVertex2f(320 + r * cosf(a1), 240 + r * sinf(a1));         glVertex2f(320 + r * cosf(a0), 240 + r * sinf(a0));
    }
    glEnd();                                                  /* klippen op 0..640 x 0..480 doet de viewport/scissor */
}
```
Animatie: `v = from − (from − to)·min(t/dur, 1)`; pagina-1-enter (0 → 0,85, 0,5 s) alleen als de iris-vlag aan staat.

## 10. Onzeker

1. De standaardtoetsen (Esc = actie 9, LShift = 6, Spatie = 5, LCtrl = 4) komen uit PERSO_MOVE §3.1 (cfg-standaard);
   een andere Woody.cfg verandert ze. Alleen Enter (actie 0xc bij loslaten) en Esc (menu-terug bij loslaten) zijn vast.
2. Frame-volgorde: `SetVar(var1, 1)` in frame N wordt pas in de VM-tick van `Game_Frame` in frame N+1 gezien (menu-update
   komt ná `0x401ab0`); niet dynamisch gecontroleerd.
3. Natuurlijk einde: welke van `var1 = 4` (script, 5300 ticks) en het einde van de cinematic (53,0 s) eerst valt is
   afhankelijk van de afronding van de VM-klok; het beeld is dan in beide gevallen al zwart.
4. `Cin_Abort 0x44f290` gebruikt `+0x118` na de reset (niet gewist) voor de hervat-fade van de muziek; direct daarna
   vervangt `0x404e30` de track toch.
5. Menutekstkleur `0xffffffff`: dat de fontquads onder MODULATE2X ×2 helderder worden volgt uit HUD_TEXT §5.2, niet uit
   een eigen spoor door `0x43f890`; of dat zichtbaar is hangt af van de glyphkleuren.
6. Blend-toestand van `0x482cf0` met vlag 8 (aangenomen: dezelfde alfa-blend als RectVirtual vlag 8); bij alfa 255 maakt
   het niet uit.
7. Slot 6 van de paginavtables (basis `0x460010` = 3, pagina 0x1c `0x460470` = 2) is niet geduid; op deze pagina's
   niet gebruikt in de gelezen paden.
8. De richting waarin Woody in WWS start (quat van instantie 0) is niet uitgerekend; de port neemt al de `.ins`-waarde.
