# MENU_LOAD.md — "Load game": savebestand, slotkeuze, wereldkeuze-carrousel (Woody.exe, build 17-10-2001)

Status: **statische analyse**; adressen = Woody.exe (imagebase 0x400000), na te lezen met
`python tools/drange.py START END`. Data: `extract/Data/House/House.ins` (`tools/insparse.py`), `House.rck` en
`Common/Woody.rck` (`tools/rckexport.py`), script `out/house_code.txt`. Aanvulling op TITLE.md §5, GAMEFLOW.md §5–§7,
HUD_TEXT.md §4.2/§8 en CAMERA.md §5.2. Onzekere punten zijn gemarkeerd met **onzeker**.

## 0. Samenvatting

- **Keten**: pagina 1 "Load game" → iris dicht (0,85 → 0 in 0,5 s, geen geluid) → resultaat 3 → `Woody.sav` bestaat?
  (`0x450aa0`) → nee: pagina **7** "No game saved on hard drive / Choose new game"; ja: pagina **0xb** (precies één
  frame) → `Woody.sav` lezen (`0x450be0`) → mislukt: pagina **0xa** "Load failed."; gelukt: pagina **2** (slotkeuze,
  2×2 raster, lege slots niet kiesbaar) → slot i → slot naar de actieve save-struct + volumes → pagina **3**
  (wereldkeuze-carrousel) → PLAY op een vrijgespeeld figuur → `RequestLevel(0.4, 1 / 0xb / 0x12 / 0x19, 1 of 3, 0)`.
  "Terug" op 2 en 3 gaat naar pagina 1 (iris gaat daar weer open 0 → 0,85); de foutpagina's kennen alleen "Continue".
- **Pagina 2**: vier panelen in de hoeken van een zwart scherm (iris-gat r ≈ 176 px in het midden toont het draaiende
  boomhuis). Per slot: een groen neon-paneel (House.rck afbeelding 0, additief) met drie cirkels voor de gezichtjes
  (Woody altijd, Knothead als W2D gehaald, Splinter als W3D gehaald), een gouden ring met het **percentage** in
  rood, het label "Save N" of "FREE"; een leeg slot krijgt een rood **kruis** (House.rck afbeelding 0, 63×63 opgerekt
  naar 108×108). Titel "Select game". Panelen schuiven in/uit (±300 px, 0,5 s, lineair).
- **Pagina 3**: de camera verandert **niet**. De acht klasse-110-instanties (4 figuren + 4 sokkels, House-slots
  105–114) worden **elke frame in camera-ruimte** gezet (`0x489210`): een cirkel met straal 150 op 560 vóór en 100
  onder het oog, 10° gekanteld, 90° per figuur, figuur op positie `pos` staat vooraan (+7°). Links/rechts draait de
  carrousel lineair 90° in 0,5 s; de selectie wisselt halverwege. Gesloten figuren worden getekend met lichtkleur ×0,1
  (bijna zwart silhouet); een nog niet onthuld personage is het "?"-figuur (model 13). Om de carrousel: naam boven,
  statistiekpaneel links (gezicht, levens, health-bolletjes, $, ladingen), rechts "Game cleared NN %" en "Location"
  (volgende level), onder "PLAY" / "SEE HIGH SCORES" en "Total Score :"; gele pijlen links en rechts.
- **Save-advies**: het originele formaat is een platte POD van 0x52c4 bytes zonder werkende checksum; de port kan het
  1-op-1 overnemen (4 slots) en optioneel een bestaande `Woody.sav` importeren (§7).

## 1. De keten van "Load game" tot de hub

### 1.1 Tijdlijn

| t | pagina / code | wat er gebeurt |
|---|---|---|
| 0 | pagina 1, validate `0x460132` | item 1: `+0x22 = +0x24 = 1` (sluiten, invoer op slot), `+0x18 = 1`, **`+0x38 = 1`** (iris aan), `+0x14 = 3`, `+0x28 = 0`, iris `0x4776b0(0.85 → 0, 0,5 s)`, logo-alfa `[0x5d7b28] = 0` (logo direct weg). **Geen geluid.** Omhoog/omlaag/terug geblokkeerd (`+0x24`) |
| 0,5 s | `0x45b990` → `vt[18]` `0x4601b0` → `0x45baa0` | resultaat 3 (uitgesteld, `+0x28 ≥ +0x34`) |
| 0,5 s | handler `0x4051da` | `r = [app+0x4c]->vt[3]()` = `0x450aa0` (`Woody.sav` openen met modus 5): **0** = bestaat, **7** = niet. Switch op `r − 3` (tabel `0x405bc0`): 3/6 → pagina 0xf (`app+0x58 = 0`), 4 → 0x11, 7 → **pagina 7**, 8 → 0x10 (`app+0x58 = 0`), al het andere (dus 0) → **pagina 0xb**, `app+0x5c = 0`. Op de pc geeft `0x450aa0` alleen 0 of 7 |
| +1 frame | pagina 0xb, handler `0x405276` | `app+0x5c` (wachtframes) is 0 ⇒ meteen `vt[5]()` = `0x450be0` lezen: **onwaar** (niet te openen, of versie ≠ 0x11004: "Save file Woody.sav is obsolete..." naar de uitgeschakelde logger) → **pagina 0xa**; waar → **pagina 2**. Pagina 0xb (1 item: lege string, kop) staat dus precies één frame in beeld |
| … | pagina 2 (§3) | slot kiezen; bevestigen zet resultaat 10 + i na 0,5 s |
| | handler `0x4052ac` | tabel `0x405bf0`/`0x405bd8`: 10..13 → slot 0..3; **24 → `0x4057b9`: `app+0x68 == 0` ⇒ pagina 1** (anders pauzemenu `0x404d80`); 14..23 niets. Slot s: `0x456df0(s, app+0x48)` (0x14a4 B slot → actieve save-struct), `[0x4c2c54] = 0x456e60(s)` (muziekvolume van dat slot), `[0x4c2c50] = 0x456e70(s)` (effectvolume), `[0x5e618c]+4 = 0x456e80(s)` (trilling, als het object bestaat), `0x468f50()->vt[0x54](sfx)`, `->vt[0x5c](muziek)`, **pagina 3** |
| … | pagina 3 (§4) | carrousel; PLAY op een vrijgespeeld figuur: resultaat 14..17 na 0,5 s |
| | handler `0x4056c8` | tabel `0x405cd0`/`0x405cb4` op `r − 4`: **4 → pagina 4** (high scores), **14 → `RequestLevel(0.4, 1, 1, 0)`** (WWS), **15 → (0.4, 0xb, 1, 0)** (KWS), **16 → (0.4, 0x12, 1, 0)** (SWS), **17 → (0.4, 0x19, 3, 0)** (BlackBox, state 3), **24 → pagina 1** |
| +0,4 s | `0x401590` | levelwissel (GAMEFLOW §4.2): `LoadLevel` zet `cfg+0x380 = byte_404830[level]` = 0 / 1 / 2 / 0 ⇒ de Perso leest **blok `cfg+0x380` van de zojuist geladen save-struct** (levens, $, ladingen, health; GAMEFLOW §6.2). Spawn = de `.ins`-positie van de Perso in de hub: `app+0x6c` = 0 (House), dus `1084 GetPrevLevel` geeft 0 en het hub-object 297 doet niets (GAMEFLOW §4.5). Fade-in 1,0 s |

Op pagina 3 zet de validate van PLAY bovendien **`pagina1+0x38 = 0`** (`0x45ef8a`): wie na het spelen terugkomt op
het titelscherm krijgt pagina 1 weer zonder iris.

### 1.2 Wat "terug" doet
| pagina | terug (actie 5 of `[0x5e6194]->vt[5](0)`) |
|---|---|
| 1 tijdens het sluiten | geblokkeerd (`+0x24`) |
| 0xb | – (één frame) |
| 7, 0xa, 0xf, 0x10, 0x11 | list-klasse `vt[14]` = `0x446a70` → resultaat 24, maar de handlers (`0x405075` resp. `0x40524c`) reageren alleen op **5** = "Continue" ⇒ terug doet niets |
| 2 | `0x45bb30` → `0x45bb40`: **SoundFx 0x3f**, iris 0,37 → 0 in 0,5 s, panelen schuiven uit, na 0,5 s resultaat 24 → pagina 1 |
| 3 | idem (`0x45bb40`), bij de resultaatlevering (`0x45e870`) worden de 8 figuren verborgen → pagina 1 |
| 1 (terugkomst) | enter `0x460070`: SoundFx 0x3f, selectie 0, iris 0 → 0,85 in 0,5 s (want `+0x38` is nog 1), `+8 = 0` |

### 1.3 Foutpagina's (list-klasse, y-fractie 0,4, S = 30, geen iris)
Alle vijf: `vt[7]` = `0x446e10` (0,4 ⇒ y = 192), eerste kiesbare item = "Continue" (resultaat 5). Achtergrond: tabel
`0x405af8` geeft deze pagina's geval 3 = **halfzwart vlak `0x80000000` over het 3D-beeld** (ook in House).

| pagina | vtable | items (`0x0002xxxx`, vlag 2 = kop) | Continue → |
|---|---|---|---|
| 7 | `0x4aa988`, tabel `0x4b5828` | 64 "No game saved on hard drive", 65 "Choose new game", 66 "", 1 "", **4 Continue** | pagina 1 (`0x405075`) |
| 0xa | `0x4aa834`, `0x4b5918` | 60 "Load failed.", 1 "", 4 Continue | pagina 1 (`0x405075`) |
| 0xb | `0x4aa7b4`, `0x4b5948` | 1 "" (wachtpagina, 1 frame) | – |
| 0xf | `0x4aa6f4`, `0x4b59b8` | 77/78/79 (PS2: "memory card … is not inserted …"), 1, 4 | `app+0x58 == 0` ⇒ pagina 1, `== 1` ⇒ pagina 6 (`0x40524c`) |
| 0x10 | `0x4aa6b4`, `0x4b5a08` | 80..83 (PS2: "corrupted data …"), 1, 4 | idem |
| 0x11 | `0x4aa674`, `0x4b5a68` | 69/70 (PS2: "unformatted"), 1, 4 | pagina 1 |

0xf/0x10/0x11 zijn op de pc onbereikbaar (`0x450aa0` geeft nooit 3/4/6/8). `app+0x58` onthoudt of de fout uit de
laad- (0) of de opslaanketen (1) komt; `app+0x5c` = wachtframes vóór lezen/schrijven (laden 0, schrijven 2).

## 2. De twee basisklassen

### 2.1 Paneelpagina `0x45b830` (vtable `0x4ab2e4`; ook pagina 1 en 0x1e)
| veld | betekenis |
|---|---|
| `+0x14` | uitgesteld resultaat |
| `+0x18` | soort sluiten: 0 = terug (levert 24), 1 = bevestigen (levert `+0x14`) (`0x45baa0`) |
| `+0x1c` | tijd sinds openen/sluiten/draaien-begin (s) |
| `+0x20` / `+0x21` | "schuif uit" / "schuif in" (teksten van pagina 3) |
| `+0x22` | sluiten loopt; `+0x23` = openen loopt (tot `+0x1c ≥ 0,5`); `+0x24` = invoer op slot |
| `+0x28` | iristijd; `+0x2c` iris-object; `+0x30` irisdoel (**0,37**); `+0x34` duur (0,5); `+0x38` iris aan |

- **Enter `0x45b8c0`**: lijstbasis-reset (`0x4464c0`), `+8 = 0,5` (invoervertraging), **SoundFx 0x3f** (ref 111 in
  `Common/<figuur>.rck`, vol 50, 2D — TITLE §5.4), iris **0 → 0,37 in 0,5 s**, `+0x23 = +0x21 = 1`.
- **Bevestig-sluiten `0x45bae0`** / **terug-sluiten `0x45bb40`**: SoundFx 0x3f, iris 0,37 → 0 in 0,5 s, `+0x22 =
  +0x24 = +0x20 = 1`, `+0x1c = +0x28 = 0`, `+0x18` = 1 resp. 0.
- **Tekenen `0x45b990`** (`vt[1]`): eerste frame van het openen (`+0x28 == 0`): iris dicht tekenen; `+0x28 += dt`,
  `+0x1c += dt`; openen klaar bij `+0x1c ≥ 0,5`. `vt[16]` (voor-teken); iris animeren (`0x477920`, of een volledig
  zwart vlak als `+0x30 ≤ 0`); als `+0x22` en `+0x28 ≥ +0x34`: `vt[18]` (resultaat leveren); inhoud `vt[17]` alleen
  zolang niet (`+0x24` en `+0x28 ≥ +0x34`).
- Iris-gat bij 0,37: binnenstraal `0,37·0,99·480 = 175,8` px rond (320, 240); daarbuiten zwart (TITLE §5.4).

### 2.2 Slotlijst `0x45d2a0..0x45dcf0` (gedeeld door pagina 2 en 5)
- Enter `0x45d2b0` = paneel-enter + voor s = 0..3 (`0x45d2e0`): `slot = [0x5e5a48]->slot(s)` (`0x456da0`),
  bytes `+0x4c+s` / `+0x50+s` / `+0x54+s` = `0x4509b0(0/1/2)` (personage vrij: Woody altijd; Knothead = done(W2D);
  Splinter = done(W3D)), `+0x58+4s = 0x4501f0(slot)` = **percentage** (0 = leeg).
- Percentage `0x4501f0` = `(P0 + P1 + P2) / 3` (int, afgekapt) met `0x450050(c)` = som van gewichten van de gehaalde
  levels: Woody W1A 6, W1B 7, W2A 8, W2B 9, W2D 12, W3A 13, W3B 14, W3C 15, W3D 16; Knothead K1A 14, K1R 15, K2A 16,
  K2R 17, K3A 18, K3R 20; Splinter S1A..S3R idem (elk 100 totaal).
- Schuiven `0x45d330` (in `vt[17]`): zolang `+0x28 ≤ 0,5`: `off = sluiten ? +0x1c·(−600) : (0,5 − +0x1c)·(−600)`,
  anders 0; slots 1 en 3 krijgen x-verschuiving `off`, slots 2 en 4 `−off` (links resp. rechts het beeld in/uit),
  de titel `y = 415 − off` (van onder). Lineair.
- **Slot tekenen `0x45d530(i, xoff)`** (i = 1..4; posities tabel `0x4ab420`: **(16,48), (488,48), (16,324), (488,324)**):
  - `sel = (+0x3c == i)`; kleur `c = sel ? 0xfe808080 : 0xfe202020` (0x80 = 1,0; dus niet-gekozen op 25 %).
  - **Paneel** `RectVirtual(x, y, 137, 108, bron 0,0,137,108, c, House.rck afb. 0, vlag 4 = additief)`; groene
    neonrand: drie kleine cirkels (gezichtjes) boven een grote (percentage). Voor het gekozen slot knippert het:
    `+0x40 += dt`; ≤ 0,3 s: geen paneel, tekstkleur `0xfe202020`; 0,3..0,6 s: paneel met `0xfe808080`, tekst
    `0xfe808080`; boven 0,6 terug naar 0.
  - **Ring** `(x+45, y+50.5→50, 49×49)`, bron `(0, 108.5, 49, 49)` van House.rck afb. 0 (gouden ring), kleur `c`, additief.
  - Als het slot **niet leeg** is: gezichtjes uit `Common/<figuur>.rck` afbeelding **61**, 64×64, kleur `c`, vlag 8
    (alfa): Woody bron (0,0) op **(x+33, y−7)**; Knothead (indien vrij) bron **(0,63)** op **(x−4, y+8)**; Splinter
    (indien vrij) bron **(63,0)** op **(x+69, y+7)**.
  - Label (`0x45d3c0`): leeg ⇒ string **18 "FREE"**, anders **26+i−1 "Save i"**; grootte 28, verkleind tot breedte
    ≤ 137 (`0x45dc90`: −1 per stap, min 10); boven-slots: `x = slot.x + 68,5 − w/2`, `y = 48 + 108 = 156` (onder het
    paneel); onder-slots: `y = 324 − celhoogte` (boven het paneel). Kleur = de tekstkleur van hierboven.
  - **Percentage**: `itoa(p) + string 7 "%"`, grootte 16 (bij p ≥ 100: 12), gecentreerd op **(x+69,5, y+75)**,
    kleur `sel ? 0xfeff1400 : 0xfe3f0500` (rood). Wordt ook voor een leeg slot getekend ("0%").

## 3. Pagina 2 — slotkeuze laden (klasse `0x45dd00`, 0x68 B, vtable `0x4ab468`)

| vt | functie | |
|---|---|---|
| [2] | `0x45dd20` → tabel `0x4b5e40` | 1 item: 25 "Select game" (wordt niet via de lijst getekend) |
| [9] validate | `0x45bad0` → [19] `0x45dd70` | alleen als `pct[sel] > 0`: bevestig-sluiten (`0x45d2a0` = `0x45bae0`, **SoundFx 0x3f**), `+0x14 = 10 + sel − 1`, en `0x45e620(pagina 3)` (carrousel terug op Woody, §4.2). Leeg slot: `+0x14 = 0`, er gebeurt niets (geen geluid) |
| [10] omlaag | `0x45dfb0` | 1 → 3, anders 4; 2 → 4, anders 3 (alleen naar niet-lege slots; niet tijdens sluiten) |
| [11] omhoog | `0x45e000` | 3 → 1, anders 2; 4 → 2, anders 1 |
| [12] rechts | `0x45df20` | 1 → 2, anders 4; 3 → 4, anders 2 |
| [13] links | `0x45df60` | 2 → 1, anders 3; 4 → 3, anders 1 |
| [14] terug | `0x45bb30` → `0x45bb40` | §1.2 |
| [15] enter | `0x45dd30` | slotlijst-enter (§2.2), `+0x40 = 0`; **selectie blijft staan van het vorige bezoek** (ctor: 1) en schuift door naar het eerste niet-lege slot ≥ de huidige, max 4 |
| [17] inhoud | `0x45de10` | slots (`0x45d330` → [21] `0x45e050`) + titel |
| [21] slot | `0x45e050` | `0x45d530` + voor een **leeg** slot het rode **kruis**: `RectVirtual(x+20, y, 108, 108, bron 0,160,63,63, 0xfe808080, House.rck afb. 0, vlag 8)` |

- Navigatie is een 2×2-raster, **zonder geluid**, lege slots worden overgeslagen en zijn niet te bevestigen.
- Titel: string 25 "Select game", grootte 30 (verkleind tot ≤ 330), `x = 320 − w/2`, `y = 415 − off`; eerst in
  `0xfe808080` op (x, y), daarover in **`0xfe801400`** (oranjerood) op `(x − 0,05·h, y − 0,05·h)` (schaduw
  rechtsonder), `h = 0x441980` = celhoogte.
- Geen logo (de fade-out van `0x446b00` loopt, maar pagina 1 zette de alfa al op 0), geen halfzwart vlak
  (tabel `0x405af8`: geval 0), het boomhuis draait door in het irisgat.
- Randgeval: staat de onthouden selectie voorbij het laatste gevulde slot, dan leest de lus `+0x68` (buiten het object)
  en eindigt hij op 4, mogelijk een leeg slot (**onzeker**, onschuldig: bevestigen doet dan niets).

## 4. Pagina 3 — wereldkeuze-carrousel (klasse `0x45e560`, 0x148 B, vtable `0x4ab560`, globaal `[0x5e5adc]`)

### 4.1 Velden
| veld | betekenis |
|---|---|
| `+4` | lijstselectie (0 = PLAY, 1 = SEE HIGH SCORES) |
| `+0x3c + 0x2c·k` … | **record k** = carrouselplek k+1 (k = 0..3), zie 4.3 |
| `+0xec..+0x100` | geregistreerde figuren: n=0 `+0xec`, n=1 `+0xf0`, n=2 `+0xf4`, n=4 eerst `+0xf8` dan `+0x100`, n=3 `+0xfc` |
| `+0x104..+0x110` | de vier sokkels (n=5), rang 1..4 in registratievolgorde |
| `+0x114` | geselecteerde plek 1..4 (int) |
| `+0x118` / `+0x11c` / `+0x120` | carrouselpositie (float) / startpositie / doel |
| `+0x124, +0x128, +0x12c, +0x130` | straal **150**, afstand **560**, hoogte **100**, hoekafstand **90°** (ctor / enter) |
| `+0x134` / `+0x138` | draaiduur **0,5 s** / draaitijd |
| `+0x13c` / `+0x13d` | draait naar links / naar rechts |
| `+0x140` / `+0x144` | stringrefs "wereld" / "deel" van de locatie (`0x45ec50`) |

Registratie (bericht **58** `(inst, n)` → `0x451960` → `0x45e6f0`, House-script objecten 105–114, alle drie ook
`1200 [slot, 110]`; 105–107 krijgen daarnaast `45 [slot, 32]` = SetFlags 0x20 = inktlijn):

| slot | model (House.ins) | n | rol |
|---|---|---|---|
| 105 | 0 (Woody, 91 anims) | 0 | figuur Woody, rang 1 |
| 106 | 10 (Knothead) | 1 | figuur Knothead, rang 2 |
| 107 | 11 (Splinter) | 2 | figuur Splinter, rang 3 |
| 108 | 12 (BlackBox, 2 anims) | 3 | figuur BlackBox, rang 4; `0x436ca0(1.0, {1,−1,−1,−1})` |
| 109, 110 | 13 ("?"-figuur, 1 anim) | 4 | plaatsvervanger rang 3 resp. 4 |
| 111–114 | 14 (sokkel, plat rood/geel) | 5 | sokkels rang 1..4 |

Elke levelload wist `0x446410` → `0x45e5f0` de registraties; het House-script zet ze opnieuw.

### 4.2 Enter `0x45e660` en reset `0x45e620`
`0x45e620` (ctor en de validate van pagina 2): `+0x118 = +0x120 = 1.0`, `+0x114 = 1` ⇒ **altijd beginnen op Woody**.
Enter: paneel-enter (**SoundFx 0x3f**, iris 0 → 0,37, invoer na 0,5 s), `+0x134 = 0,5`, `+0x138 = 0`, niet draaien,
`+0x130 = 90`; sokkels naar de records; records vullen (`0x45f310`, 4.3); locatie van Woody (`0x45ec50`); alle 8
record-instanties tonen (`0x4891d0(1)` = offsets resetten, lokale basis, plaatsen, in een cel hangen `0x4077f0`);
animatie van het geselecteerde figuur starten (`0x45f5d0`).

### 4.3 Records (`0x45f310`, uit de actieve save-struct `[0x5e5818]`)
| k | figuur (`+0x60`) | sokkel (`+0x64`) | vrij (`+0x4c`) | sprite (`+0x58`) | `+0x5c` | donker (`inst+0x188 = 1`) |
|---|---|---|---|---|---|---|
| 0 | 105 Woody | 111 | altijd | 0 | 0 | nooit |
| 1 | 106 Knothead | 112 | done(W2D) | 2 | 0 | beide als niet vrij |
| 2 | Knothead vrij ? **107 Splinter** : **109 "?"** | 113 | done(W3D) | 1 | 0 | niet vrij: figuur 109 normaal + sokkel donker; figuur 107 + sokkel donker |
| 3 | Splinter vrij ? **108 BlackBox** : **110 "?"** | 114 | done(S3R) (`0x4509e0(2, 0x18)`) | 1 | **1** | idem |

Per vrij record verder: `+0x3c` levens, `+0x40` health (float), `+0x44` unieke items, `+0x48` ladingen (blok k van de
save; record 3 = BlackBox krijgt geen statistieken), `+0x50` percentage `0x450050(k)`, `+0x54` locatie = `0x450790(k)` =
**het eerste nog niet gehaalde level** van dat personage (W1A..W3D / K1A..K3R / S1A..S3R; alles gehaald: 0x19 of 0x18).
Een naam wordt dus pas onthuld als het personage ervóór vrij is (zie 4.6), een gesloten personage staat er als donker
silhouet. **"Donker"** = `vt[26]` `0x451a40`: `[0x5ac850] = 1`, `[0x5ac854..5c] = 0.1` ⇒ de renderer vermenigvuldigt
de belichte vertexkleur met **0,1** (`0x43bdfc`; modus 2 = optellen, zie LIGHTING.md en PERSO_DEATH.md voor `[0x5ac850]`).

### 4.4 Plaatsing in camera-ruimte (`0x451890` → `0x489780`, `0x489210`)
Elke frame (`vt[16]` = `0x45e800`: `+0x138 += dt`, dan `0x45edc0`) voor elk van de 8 instanties
`0x451890(pos = +0x118, 90, 150, 560, 100)`:
```c
f     = rank - pos;                                  // rank = inst+0x18c (1..4)
theta = (f * 90.0f + 7.0f) * PI/180;                 // +0x174 in graden; de voorste staat 7° rechtsom
off   = ( 150*sin(theta),  26.047f*cos(theta),  -150*cos(theta) );   // 26.047 = 150*sin(10°): ring 10° gekanteld
inst+0xfc = 560 (afstand), inst+0x100 = 100 (hoogte), inst+0x104..0x10c = off
local_rot(inst+0x14c) = R(-10°, theta, 0) * Basis   // 0x489780; Basis +0x128 = rijen (1,0,0),(0,0,-1),(0,1,0)
```
`vt[3]` van klasse 110 (`0x451a30` → `0x489650` → `0x489210`, door `0x42b400` elke frame voor elke instantie):
```c
P = ( off.x, off.y + 100, off.z + 560 );             // "ontwerp"-camera-ruimte: x rechts, y OMLAAG, z vooruit
world = campos + Rcam * ( P.x / 1.0, P.y / 1.3333, P.z / 1.2 );   // [0x5e86ac]+0x154..0x17c = inverse van (R · diag(1/sx, 1/sy, zoom))
inst.rows = normalize(Rcam * local_rot-kolommen / scale)          // oriëntatie = camera ∘ lokaal
inst.pos  = world; relink in cel (0x4077f0)
```
De inverse-matrix bevat de projectieschaal (CAMERA.md §5.2: `sx = 1`, `sy = 0,75`, `zoom = 1,2` zonder
letterbox), dus op het scherm geldt eenvoudig **NDC = (P.x / P.z, P.y / P.z)**, `scherm = (320 + 320·ndc.x,
240 + 240·ndc.y)`:

| plek | θ | P | scherm (px) | afstand (wereld) |
|---|---|---|---|---|
| voor (geselecteerd) | 7° | (18,3, 125,9, 411,1) | (334, 313) | 343 |
| rechts | 97° | (148,9, 96,8, 578,3) | (402, 280) | 482 |
| achter | 187° | (−18,3, 74,1, 708,9) | (312, 265) | 591 |
| links | 277° | (−148,9, 103,2, 541,7) | (232, 286) | 451 |

Alles ligt binnen het irisgat (r ≈ 176 rond (320,240)). De modellen zijn z-omhoog (zoals de Perso: rij 2 = omhoog);
met de basis gaat model-z naar camera-omhoog en model-y van de camera af, dus het voorste figuur kijkt de camera aan;
elk figuur draait met θ mee (kijkt naar buiten), plus 10° kanteling. Volgorde en teken van de hoeken in `0x489780`
zijn niet tot op het bit nagelopen (**onzeker**; zie §9 voor het port-alternatief).

**Port** (`car_place` in `src/main_engine.c`, geverifieerd op screenshots): positie precies zoals hierboven (camera-ruimte
→ wereld met `R·x − U·(0,75·y) + F·(z / 1,2)`, `R`/`U`/`F` = rechts/omhoog/vooruit van de titelcamera). Oriëntatie als
zuivere rotatie, zonder de niet-uniforme schaal op de rijen: model-x → `(cos θ, 0, sin θ)`, model-y → `(−sin θ, 0, cos θ)`,
model-z → `(0, −1, 0)` (omhoog), daarna 10° om de camera-x-as zodat de voorkant van de ring zakt (dezelfde kanteling
als de offsets `26,047·cos θ`). Resultaat: figuren rechtop, elk kijkt van het ringmidden naar buiten, het voorste
recht in de lens, de sokkels als ellipsen van bovenaf; Woody staat met zijn voeten op zijn sokkel. Het "?"-figuur en de
BlackBox-kast zweven in frame 0 van hun anim 0 een stuk boven hun sokkel (zo staat het in de data; niet te
vergelijken zonder beeld van het origineel). De plaatsing draait elke frame na de titelbaan-camera en vóór het renderen.

**De camera gaat nergens heen.** Pagina 3 roept geen `SetMode` aan; de titelcamera (mode 0x80, cirkel om het
boomhuis, TITLE §2) draait gewoon door, de carrousel hangt vast vóór de lens en het decor draait erachter. De
bestandsposities van 105–114 (x ≈ −10500, y ≈ 4000, z ≈ 4000–5000) en de intro-vector (−11299, −7, 601) van slot 115
spelen geen rol.

### 4.5 Invoer en draaien
| actie | functie | effect |
|---|---|---|
| 1 rechts | `vt[12]` `0x45efb0` | alleen als niet draaiend en niet sluitend: `a = +0x118` (4 → 0), doel `a + 1`, `+0x13d = 1`, `0x45f690` (vorig figuur: anim-wachtrij `{1,0,0,0}` = na anim 1 terug naar anim 0; BlackBox: `0x436ca0`), `+0x1c = 0`, `+0x20 = 1` (teksten schuiven uit) |
| 0 links | `vt[13]` `0x45f050` | spiegelbeeld: `a` (1 → 5), doel `a − 1` |
| 2/3 omhoog/omlaag | `0x45bb90`/`0x45bba0` | lijst PLAY ↔ SEE HIGH SCORES (rondlopend, niet tijdens sluiten) |
| 0xc bevestigen | `vt[9]` `0x45ee50` | zie 4.7 |
| 5 terug | `0x45bb30` | §1.2 |

**Animatie** (`0x45f1d0` rechts, `0x45f0f0` links, elke frame): `pos = start ± t/0,5` — **lineair, 90° in 0,5 s =
180°/s, geen easing**. Bij `t ≥ 0,25`: `+0x114` = het doel (5 → 1, 0 → 4), locatie-strings (`0x45ec50`), `+0x21 = 1`,
`+0x20 = 0` (teksten schuiven weer in). Bij `t ≥ 0,5`: `pos = doel` (genormaliseerd naar 1..4), `+0x1c = 0`,
`+0x21 = 0`, en `0x45f5d0`: het nieuwe figuur krijgt **klok = nu, snelheid 3,0, wachtrij {1, 2, 2, 2}** = .ins-anim 1
één keer, daarna anim 2 in een lus (model 0: anim 1 = 160 frames = 0,27 s op snelheid 3, anim 2 = 360 frames = 0,6 s);
BlackBox: `0x436ca0(1.0, {1,−1,−1,−1})`. Tijdens het draaien kan niet opnieuw gedraaid worden; er is **geen
menugeluid** bij draaien (eventuele geluiden zitten in de animaties zelf).

Nagelopen (port: `car_anim_sel` / `car_anim_prev`):
- `0x436ca0(inst, f, s0..s3)` = klok `inst+0xa8` = nu, wachtrij `+0xb0..+0xbc` = s0..s3, snelheid `0x42e290(f · [0x4a988c])`
  met `[0x4a988c]` = **3,0**; de BlackBox speelt anim 1 dus ook op snelheid 3 en blijft daarna op het laatste frame staan.
- `0x45f5d0` (nieuw figuur): eerst snelheid 3,0 en klok = nu, dan per rol (`inst+0x184`): 0..2 → wachtrij {1,2,2,2},
  3 → `0x436ca0(1.0, {1,−1,−1,−1})`, 4 ("?") → niets meer (anim 0 één keer op snelheid 3).
- `0x45f690` (vertrekkend figuur): rol 3 → `0x436ca0(1.0, {1,−1,−1,−1})` (herstart); anders alleen **slot 0 = 1 en
  slots 1..3 = 0 op de lopende klok** (geen herstart, snelheid blijft 3): de fase van anim 1 volgt uit de klok, daarna
  anim 0 in een lus.
- De andere figuren houden hun ctor-toestand (anim 0, snelheid 0 = stilstaand frame 0) tot ze een keer voor staan.

### 4.6 2D-laag (`vt[17]` = `0x45e8a0`)
`off` = schuif: `+0x21 ? (0,5 − +0x1c)·(−600) : +0x20 ? +0x1c·(−600) : 0` (in/uit bij openen en rond elke draai).

| element | functie | inhoud / plaats |
|---|---|---|
| naam | `0x45fe30(off)` | plek 1 "WOODY" (30), 2 "KNOTHEAD" (31) als Woody vrij (altijd), 3 "SPLINTER" (32) als Knothead vrij, 4 "BONUS" (33) als Splinter vrij, anders **34 "???"**; grootte 30, `x = 320 − w/2`, `y = 16 + off`; schaduw `0xfe808080` op +0,05·h, tekst `0xfe801400` |
| statistiek links (alleen vrij en `+0x5c == 0`, dus niet BlackBox) | `0x45f6f0(off)` | HUD-sprites (tabel `0x4ab658`, vlag 8): gezicht `+0x58` op (16+off, 113); getalplaatje 8 op (56+off, 153) met **levens − 1**; health: sprite 7 × `ftol(health)` op (31+off+24i, 78); sprite 3 ($) op (22+off, 213), plaatje op (56+off, 258) met **unieke items**; sprite 6 (lading) op (22+off, 308), plaatje op (56+off, 353) met **ladingen**. Getallen (`0x45eb00`/`0x4605b0`): rood `0xfeff0000`, grootte 17 (≥ 100: 22,5), gecentreerd op (72+off, 169 / 274 / 369) |
| rechts (idem) | `0x45f790(−off)` | midden x = 565 − off: 43 "Game cleared" (20, wit `0xfeffffff`) op y 110; eronder "NN%" (50, `0xfeff1400`); als NN < 100: 44 "Location" (20, wit) op y 240, dan wereld (25, `0xfeff1400`) op y 270 en deel eronder. Alles verkleind tot breedte ≤ 115 |
| lijst | `0x446640` (alleen vrij, niet openend/sluitend) | items `0x4b5e50` 42 "PLAY" (resultaat via validate) en `0x4b5e60` 41 "SEE HIGH SCORES" (vlag 0x21: ×0,8); op plek met BlackBox-figuur wordt item 1 = string 1 / kop (`0x45e800`) en `+4 = 0`. S = 30·0,8 = 24, y-fractie `0,85 + schuif·0,2/0,5` (`0x45ffa0`), gecentreerd, gekozen item knippert (TITLE §5.1) |
| pijlen | `0x45fac0` | Common afb. 63 bron (0,96,31,31) = **HUD-sprite 15, een geel pijl-contour** (alfa 0 in de data, dus alleen additief zichtbaar: lost HUD_TEXT §9 punt 6 op), verdubbeld tot 62×62, **additief**; rechts op (470 + s, 209), links gespiegeld op (108 − s, 209), `s = (sluiten ? +0x28 : 0,5 − +0x28)·600`. Grijswaarde 128; de pijl in de draairichting dimt tijdens het draaien naar 32 en terug (`128 − t·96/0,25`, daarna `(t − 0,25)·96/0,25 + 32`) |
| totaalscore | `0x45e8a0` | 130 "Total Score :" (15, `0xfeffffff`) op (16, 410), daaronder het getal `0x450a10` (som van alle `best` + `save+0x14a0`) |

Details (nagelopen voor de port, `hud_carousel` in `src/hud.c`):
- Getallen links (`0x45eb00` → `0x45f2a0`): grootte 17; een waarde ≥ 100 zet `30 × 0,75 = 22,5` en die maat **blijft
  staan** voor de volgende getallen van dezelfde frame.
- Rechts (`0x45f790`): "NN%" staat op `110 + celhoogte` van de (verkleinde) grootte-20-regel; het deel (`+0x144`) wordt
  op de maat van de wereldregel gemeten en getekend (geen eigen verkleining), op `270 + celhoogte`.
- Naam (`0x45fe30`): de grijze kopie `0xfe808080` op `(x + 0,05·h, y + 0,05·h)`, daarover `0xfe801400` op `(x, y)`.
- Pijlen: bron `[0x4ab784..0x4ab794]` = (0, 96, 31, 31), afbeelding 63; grijs `128 − t·96/0,25` bij uitschuiven,
  `(t − 0,25)·96/0,25 + 32` bij inschuiven, alleen voor de pijl van de draairichting. **Port-keuze**: de bron een halve
  texel ingekort, omdat de rij erboven (y 95) wit is en de bilineaire filter op 2× schaal er een lijn boven de pijl van
  maakte.
- Totaalscore: string 130 grootte 15 op (16, 410), het getal op (16, 410 + celhoogte), beide `0xfeffffff`.
- De lijst op pagina 3 zit niet in de gewone lijstnavigatie van de port: omhoog/omlaag wisselt PLAY ↔ SEE HIGH SCORES,
  op de BlackBox-plek is item 1 een kop en blijft de selectie op PLAY.

Locatie-strings `0x45ec50(level)` (tabel `0x45ed60`): W1A/K1A/S1A = 47 "Space" + 51 "Part A"; W1B = Space + 52
"Part B"; K1R/S1R = Space + 55 "Race"; W2A/K2A/S2A = 48 "Pirate" + Part A; W2B = Pirate + Part B; **W2D = Pirate +
53 "Part C"**; K2R/S2R = Pirate + Race; W3A/K3A/S3A = 49 "House" + Part A; W3B/W3C/W3D = House + Part B/C/D;
K3R/S3R = House + Race; hubs: niets (de functie laat dan de vorige strings staan; ze worden alleen bij NN < 100
getekend). Volledig: W1A 47+51, W1B 47+52, W2A 48+51, W2B 48+52, W2D 48+53, W3A 49+51, W3B 49+52, W3C 49+53, W3D 49+54
("Part D"), K1A/S1A 47+51, K1R/S1R 47+55, K2A/S2A 48+51, K2R/S2R 48+55, K3A/S3A 49+51, K3R/S3R 49+55.

### 4.7 Bevestigen (`0x45ee50`)
- **PLAY** (item 0): alleen als het record vrij is en niet al sluitend: iris 0,37 → 0 in 0,5 s (`+0x28 = 0`), **geen
  geluid**, `+0x22 = +0x24 = 1`, `+0x18 = 1`, `+0x14 = 14 + k` (Woody 14, Knothead 15, Splinter 16, BlackBox 17),
  `+0x1c = 0`, `+0x20 = 1`, `pagina1+0x38 = 0`. Na 0,5 s `vt[18]` = `0x45e870`: resultaat leveren **en de 8 instanties
  verbergen** (`0x4891d0(0)` → `0x407850`).
- **SEE HIGH SCORES** (item 1): vrij, niet sluitend, niet plek 4: idem maar `+0x14 = 4` en `[0x5e5a8c]+0x3c = k`
  (pagina 4 = high scores van personage k, klasse `0x45bfb0`, niet verder ontleed; terug → pagina 3, `0x405749`).
  **Port**: niet geport — het item doet niets (alleen een regel in de console). Pagina 4 is geen kleine klasse: vtable
  `0x4ab368`, enter `0x45bfd0`, tekenen `0x45bff0` → `0x45c090` … `0x45cd50` (~1500 instructies, een tabel per level).
- Gesloten figuur: niets (geen geluid).

## 5. Pagina 5 (slotkeuze opslaan) en 0x17 — kort

Klasse `0x45e1f0` (0x68 B, vtable `0x4ab4c0`), zelfde slotlijst als pagina 2 met deze verschillen:
- Enter `0x45e230`: `+0x38 = 0` tijdens de basis-enter, dan iris **1,0 → 0,37** in 0,5 s (je komt uit het spel) en
  `+0x38 = 1`. Titel string **24 "Select save"** (`0x45e2e0`).
- **Alle slots kiesbaar**, raster zonder overslaan (`0x45e430` omlaag 1,2 → +2; `0x45e450` omhoog 3,4 → −2;
  `0x45e3f0` rechts 1,3 → +1; `0x45e410` links 2,4 → −1). Slottekenen = alleen `0x45d530` (**geen kruis**; leeg =
  "FREE" + "0%").
- Validate `0x45e270`: bevestig-sluiten (SoundFx 0x3f) + `+0x14 = 10 + sel − 1`, maar de iris gaat daarna weer **open**
  (0,37 → 1,0 in 0,5 s).
- Handler `0x4054ac`: 24 (terug) → pagina 6; slot s → `app+0x60 = s`; slot bezet (`0x4501f0 ≠ 0`) → **pagina 0x17**
  (61 "Are you sure you want to overwrite this save?" / 5 Yes / 6 No, handler `0x405586`: Ja → schrijven, **Nee en
  terug → pagina 6**); leeg → volumes `0x456e40(muziek, sfx, s)` + trilling `0x456e90`, `0x456dc0(save → slot s)`,
  pagina 0xc met `app+0x5c = 2` → na 2 frames `vt[4]` schrijven → 8 "Game Saved" / 9 "Save failed.".
- Pagina 6 "Ja" → `vt[3]`: bestand bestaat niet ⇒ `0x456e20` (4 slots leegmaken) → pagina 5; bestaat ⇒ pagina 0xe
  (1 frame) → lezen: gelukt → pagina 5, mislukt → pagina 6. Muziekobject `[0x5e61a4]->vt[0x50](0.5)` bij het openen
  van pagina 5 en `vt[0x54](0.5)` na een geslaagde schrijfactie (betekenis **onzeker**, vermoedelijk dempen/herstellen).
- **Correctie op GAMEFLOW §5**: pagina 8 "Game Saved" + Continue gaat **niet** terug naar 6 maar verlaat het menu
  (`0x4056c0` → `0x405364`: `0x454050(perso)` + state 1); alleen pagina 9 "Save failed." gaat terug naar 6.

## 6. `Woody.sav` byte-exact

Werkmap, **0x52c4 bytes**, little-endian, één `fread`/`fwrite` van `mgr+4` (`0x450be0`, `0x450b30`):

| offset | type | inhoud |
|---|---|---|
| 0x0000 | u32 | versie **0x00011004** (bij schrijven altijd gezet; bij lezen ≠ ⇒ mislukt) |
| 0x0004 + s·0x14a4 | 4 × slot | s = 0..3, elk een kopie van de actieve save-struct |
| 0x5294 | u32[4] | **muziekvolume** per slot (`[0x4c2c54]`, opties-item 39 "Music volume") |
| 0x52a4 | u32[4] | **effectvolume** per slot (`[0x4c2c50]`, item 38 "Sound FX volume") |
| 0x52b4 | f32[4] | trilling per slot (`[0x5e618c]+4`, item 132) |

(GAMEFLOW §6.1 noemt 0x5294 "sfxvol" en 0x52a4 "musicvol": dat is andersom, zie de opties-itemtabel `0x4b5ec0`.)

Slot / save-struct (0x14a4 B):

| offset | type | inhoud |
|---|---|---|
| +0x0000 | i32 | "checksum" (zie onder) |
| +0x0004 | u32 | 0x11004 |
| +0x0008 | u32 | 0 |
| +0x000c + c·0x6dc | blok | c = 0 Woody, 1 Knothead, 2 Splinter |
| blok+0x00 | i32 | levens (nieuw: 9) |
| blok+0x04 | i32 | unieke items |
| blok+0x08 | i32 | speciale ladingen |
| blok+0x0c | f32 | health (nieuw: 3.0; ≤ 0 wordt 1.0 bij laden) |
| blok+0x10 + L·0x3c | record | L = levelindex 0..28 |
| rec+0x00 | i32 | beste score |
| rec+0x04 | u8 | done |
| rec+0x05 | u8[32] | "uniek item n van dit level gepakt" (`0x450730`/`0x450760`) |
| rec+0x25 | u8[3] | opvulling |
| rec+0x28 | i32 | stat[0] (`app+0x74`, totaal vijanden) |
| rec+0x2c | i32 | **stat[2]** (`app+0x7c`, verslagen vijanden) |
| rec+0x30 | i32 | **stat[1]** (`app+0x78`, totaal bonus) |
| rec+0x34 | i32 | stat[3] (`app+0x80`, gepakte bonus) |
| rec+0x38 | f32 | tijd |
| +0x14a0 | i32 | extra score (opgeteld in `0x450a10`; geen schrijver gevonden) |

Let op de volgorde op +0x2c/+0x30 (`0x4503b0` schrijft `app+0x7c`, `0x4503e0` `app+0x78`; lezers `0x4502c0`/`0x4502f0`).

**Checksum**: `0x450030` = som van alle 0x14a4 bytes als *signed char* (met +0 eerst op 0). Enige aanroeper is de
reset `0x44ffa0`; er is **geen verificatie** bij het lezen en geen herberekening bij het schrijven. Een verse struct
geeft altijd **432** (0x1b0: versie 4+16+1, per blok levens 9 + health-bytes 0x40+0x40); omdat de actieve struct
alleen via reset (boot, New game, game over) of een slotkopie ontstaat, staat er in de praktijk overal 432 of wat
het bronslot had. Een port mag het veld dus negeren; wie byte-identiek wil schrijven zet 432.

**Leeg slot** = een gereset slot (levens 9, health 3, alles 0) ⇒ percentage 0 ⇒ "FREE". De volumes van nooit
beschreven slots zijn ongeïnitialiseerd geheugen (de slot-manager wordt met `new` gemaakt; **onzeker** of dat nul is).

## 7. Advies voor de port: vier slots

De port heeft nu één `woodyre.sav` met `{u32 'WSV2'; SaveChar chr[3]}` (`src/main_engine.c` rond `g_save`), waarbij
`SaveChar` een deelverzameling van het originele blok is (geen `rec+0x05` unieke-itembits, `stats` in de volgorde van
`app+0x74`).

**Aanbeveling: neem het originele formaat 1-op-1 over als in-memory model en bestandsformaat**, maar onder de eigen
naam `woodyre.sav`:
1. Eén `#pragma pack(1)`-struct `WoodySav { u32 version; SaveSlot slot[4]; u32 music[4], sfx[4]; f32 vib[4]; }`
   met `static_assert(sizeof == 0x52c4)` en `SaveSlot { i32 sum; u32 ver, zero; SaveBlock chr[3]; i32 extra; }`
   (0x14a4), `SaveBlock { i32 lives, unique, charges; f32 health; SaveRec rec[29]; }` (0x6dc),
   `SaveRec { i32 best; u8 done, uniq[32], pad[3]; i32 s0, s2, s1, s3; f32 time; }` (0x3c).
   De actieve struct (`g_save`) wordt een `SaveSlot`; de huidige velden mappen direct (`done[L]` → `rec[L].done`, …).
2. Versie 0x11004 is de magic. Het origineel doet niets met de checksum, dus lezen van een echte `Woody.sav` werkt
   zonder meer: bied **import** aan — ontbreekt `woodyre.sav` en staat er een `Woody.sav` naast `game/Woody.exe`
   (of in de werkmap), lees die dan. Nooit de originele `Woody.sav` overschrijven (de gebruiker heeft hem misschien
   nog nodig voor het origineel); schrijven alleen naar `woodyre.sav`.
3. Een bestaande `WSV2`-file één keer omzetten naar slot 0 (volumes = huidige opties), zodat niemand zijn voortgang
   kwijtraakt; daarna nooit meer WSV2 schrijven.
4. De port-extra's die niet in het origineel bestaan (bijv. `g_unlock_all`) horen in `Woody.cfg`/de commandoregel, niet
   in de save. Dan blijft een port-save ook door het origineel leesbaar (bestand hernoemen volstaat).

Een eigen formaat heeft geen voordeel: alles wat de port bewaart zit al in het origineel, en de unieke-itembits
(`rec+0x05`) heeft de port straks toch nodig (BONUS.md).

## 8. Port-recept

Uitgangspunt: `src/main_engine.c` (titelmenu: `title_page`, `title_sel`, `cont`, `request_level`) en `src/hud.c`
(`page_items`, `hud_menu_page`, `hud_title_draw`, `quad`, `k_spr`). Iris en paneel-basis zijn gedeeld met
Options/New game: één implementatie.

```c
/* ---- hud.c: House.rck afbeelding 0 als menutextuur (level_item laadt index 0..4 nu als hemel, zonder alfa) */
if (type == 1 && index == 0) { /* zoals het logo via common_item: RGBA, bovenste rij eerst, alfa behouden */ H.slotsheet = ...; }

/* ---- gedeelde paneelpagina (0x45b830) */
typedef struct { float t, ti; int opening, closing, lock, slide_out, slide_in, kind, result; float iris_v, iris_from, iris_to, iris_t; int iris_on; } Panel;
static void iris_set(Panel *p, float from, float to) { p->iris_from = from; p->iris_to = to; p->iris_t = 0; }
static void panel_enter(Panel *p) { memset(p, 0, sizeof *p); p->opening = p->slide_in = 1; p->iris_on = 1; iris_set(p, 0, 0.37f); audio_fx(63, NULL, NULL); /* invoer pas na 0,5 s */ }
static void panel_close(Panel *p, int kind, int result, int sound) { if (sound) audio_fx(63, NULL, NULL); iris_set(p, 0.37f, 0); p->closing = p->lock = p->slide_out = 1; p->t = p->ti = 0; p->kind = kind; p->result = result; }
/* per frame: ti += dt; t += dt; if (opening && t >= .5f) opening = slide_in = 0;
   iris_v = lerp(from, to, min(iris_t/0.5,1)); ring tekenen: zwart, binnenstraal iris_v*0.99*480, buitenstraal 475, midden (320,240), 50 segmenten;
   if (closing && ti >= .5f) { closing = 0; return kind ? result : 24; }   inhoud tekenen zolang !(lock && ti >= .5f) */

/* ---- slotlijst (0x45d530) in 640x480 virtueel; sheet = H.slotsheet (256x256) */
static const float SX[4] = { 16, 488, 16, 488 }, SY[4] = { 48, 48, 324, 324 };
float slide = p->ti <= 0.5f ? (p->closing ? p->t * -600 : (0.5f - p->t) * -600) : 0;   /* 0x45d330 */
for (int i = 0; i < 4; i++) {
    int sel = cur == i + 1; uint32_t c = sel ? 0xfe808080 : 0xfe202020, tc = c;
    float x = SX[i] + ((i & 1) ? -slide : slide), y = SY[i];
    if (!sel) quad_add(x, y, 137, 108, sheet, 0, 0, 137, 108, c);                  /* additief (vlag 4) */
    else { blink += dt; if (blink > 0.3f) { if (blink > 0.6f) blink = 0; quad_add(x, y, 137, 108, sheet, 0, 0, 137, 108, 0xfe808080); tc = 0xfe808080; } else tc = 0xfe202020; }
    quad_add(x + 45, y + 50, 49, 49, sheet, 0, 108, 49, 49, c);                    /* gouden ring */
    if (pct[i]) { face(0, x + 33, y - 7, c); if (open1[i]) face(2, x - 4, y + 8, c); if (open2[i]) face(1, x + 69, y + 7, c); }   /* k_spr 0/2/1, alfa */
    label = pct[i] ? STR(26 + i) : STR(18);  size 28 fit 137;  lx = SX-kolom + xoff + 68.5 - w/2;  ly = i < 2 ? 156 : 324 - font_cell();  kleur tc
    snprintf("%d", pct[i]) + STR(7); size pct >= 100 ? 12 : 16; centreer op (x + 69.5, y + 75); kleur sel ? 0xfeff1400 : 0xfe3f0500
    if (page == 2 && !pct[i]) quad_alpha(x + 20, y, 108, 108, sheet, 0, 160, 63, 63, 0xfe808080);                   /* kruis */
}
titel = page == 2 ? STR(25) : STR(24); size 30 fit 330; x = 320 - w/2; y = 415 - slide;
font_draw(x, y, s, 0xfe808080); font_draw(x - .05f*font_cell(), y - .05f*font_cell(), s, 0xfe801400);

/* ---- percentage (0x4501f0), vrij (0x4509b0) */
static const int8_t W[29] = { [2]=6,[3]=7,[4]=8,[5]=9,[6]=12,[7]=13,[8]=14,[9]=15,[10]=16, [12]=14,[13]=15,[14]=16,[15]=17,[16]=18,[17]=20, [19]=14,[20]=15,[21]=16,[22]=17,[23]=18,[24]=20 };
int pct_char(const SaveSlot *s, int c) { int p = 0; for (int L = 0; L < 29; L++) if (char_of_level(L) == c && s->chr[c].rec[L].done) p += W[L]; return p; }
int pct_slot(const SaveSlot *s) { return (pct_char(s,0) + pct_char(s,1) + pct_char(s,2)) / 3; }
int char_open(const SaveSlot *s, int c) { return c == 0 || s->chr[0].rec[c == 1 ? 6 : 10].done; }

/* ---- main_engine.c: de keten (vervangt "if (cont ...) request_level(1, 0.5f)") */
case 1 "Load game": iris 0.85 -> 0 (0,5 s), logo_v = 0; na 0,5 s:
    if (!file_exists("woodyre.sav") && !import_available()) page = 7;               /* 64/65/66/""/Continue, y 0.4, halfzwart vlak */
    else if (!sav_read_all(&g_file)) page = 0xa;                                    /* 60 "Load failed." */
    else page = 2;                                                                  /* pagina 0xb: 1 frame, mag weg */
page 7 / 0xa: alleen confirm op Continue -> page = 1 (enter: fx 63, iris 0 -> 0.85)
page 2: panel; confirm op niet-leeg slot -> panel_close(1, 10+s, 1); carousel_pos = 1;
        resultaat 10+s: g_save = g_file.slot[s]; opties.music = g_file.music[s]; opties.sfx = g_file.sfx[s]; page = 3
        resultaat 24: page = 1
page 3: resultaat 14..17: request_level((int[]){1, 11, 18, 25}[r - 14], 0.4f);  /* BlackBox: state 3 = het BlackBox-object, nog niet geport */
        resultaat 4: high-scorepagina (nog niet geport: negeren of eenvoudige lijst uit rec[].best)
        resultaat 24: page = 1

/* ---- carrousel: instanties */
static Instance *CAR_FIG[4], *CAR_PED[4];          /* uit 58-registratie, of vast: slot 105..114 */
void carousel_fill(void) {
    int o1 = char_open(&g_save, 1), o2 = char_open(&g_save, 2), o3 = g_save.chr[2].rec[24].done;
    CAR_FIG[0] = slot(105); CAR_FIG[1] = slot(106); CAR_FIG[2] = o1 ? slot(107) : slot(109); CAR_FIG[3] = o2 ? slot(108) : slot(110);
    for (k) CAR_PED[k] = slot(111 + k);
    dark: fig1/ped1 = !o1;  k=2: fig = !o2 && o1 (placeholder nooit donker), ped = !o2;  k=3: fig = !o3 && o2, ped = !o3
}
/* in de 1200-afhandeling blijft "type 110 -> visible = 0"; pagina 3 zet alleen deze 8 aan en na de resultaatlevering weer uit */
void carousel_place(const FreeCamera *cam, float pos) {                    /* na cam_update, vóór het renderen, elke frame */
    Vec3 F = { sinf(cam->yaw)*cosf(cam->pitch), sinf(cam->pitch), cosf(cam->yaw)*cosf(cam->pitch) };
    Vec3 R = normalize(cross(F, (Vec3){0,1,0})), D = cross(F, R);           /* rechts, OMLAAG (camera-ruimte van het origineel) */
    for (int k = 0; k < 4; k++) {
        float th = ((k + 1 - pos) * 90.0f + 7.0f) * (float)M_PI / 180.0f;
        float px = 150*sinf(th), py = 100 + 26.047f*cosf(th), pz = 560 - 150*cosf(th);
        Vec3 w = cam->pos + R*px + D*(py * 0.75f) + F*(pz / 1.2f);          /* sy 0.75, zoom 1.2 (titel: geen letterbox) */
        /* oriëntatie: model-z = -D (omhoog), model +y = weg van de ring naar achteren: -(sin th * R - cos th * F),
           dus het figuur kijkt (model -y) naar buiten; plus 10° voorover (onzeker, mag eerst weg) */
        place(CAR_FIG[k], w, ...); place(CAR_PED[k], w, ...);
    }
}
/* draaien: pos lineair ±1 in 0,5 s; bij t >= 0.25 sel = doel (1..4 rondlopend); bij 0.5 pos = doel;
   nieuw figuur: inst->slot = {1,2,2,2}, a_speed = 3.0, a_start = nu (BlackBox: {1,-1,-1,-1}); oud figuur: slot = {1,0,0,0} */
/* donker: naast tint_red in render_gl.c (regel ~559) een tint_scale (0.1) op de belichte vertexkleur = vt[26] 0x451a40 */
```

2D van pagina 3: volg de tabel in §4.6 letterlijk (sprites uit `k_spr`, pijl = sprite 15 additief ×2, getallen rood).
Volgorde: 3D (met figuren) → iris → 2D-teksten/pijlen → faders.

**Stand van de port (pagina 3 geport)**: `src/main_engine.c` `g_car` + `car_fill` / `car_place` / `car_rotate` /
`carousel_enter` / `carousel_update` / `carousel_draw` / `carousel_frame` (elke frame na `menu_update` en de
titelcamera, vóór `rnd_frame`); records via de vaste slots 105..114 (`slot_instance`), niet via bericht 58; `level_free`
vergeet de pointers (`car_forget`). Donker = `Instance.tint_scale = 0,1` (renderhaak naast `tint_red` in
`src/render_gl.c`). 2D = `hud_carousel(HudCarousel *)` in `src/hud.c`. Na PLAY laadt `level_load` het personage uit het
level (`char_of_level`: KWS → Knothead, blok 1 van de save; geverifieerd: KWS met Knothead, 7 levens (HUD toont 6) + 5 hartjes uit een
testsave). Gevonden en verholpen: de hoofdlus kopieerde elke frame de levens/health/items/ladingen van de House-Perso in
`g_save.chr[g_char]`, dus "Load game" → WWS begon altijd met 9 levens / 3 health; het origineel schrijft alleen via
setters bij een wijziging, dus op niveau 0 slaat de port die kopie nu over.

## 9. Onzeker / open

1. **Zichtbaarheid van de klasse-110-figuren buiten pagina 3.** `vt[3]` (`0x489650`) plaatst ze elke frame
   opnieuw vóór de camera én hangt ze opnieuw in een cel (`0x4077f0`); het "verbergen" van `0x4891d0(0)` (`0x407850`,
   cel losmaken) zou daardoor een frame later al ongedaan zijn, en de figuren die niet in een record zitten (107/109
   of 108/110) staan op camera-ruimte (0, 0, 300). Dat het origineel op de titel en na "terug" geen figuren toont
   (TITLE §3.2) volgt dus niet uit deze code — ergens zit nog een poort (cel-zoektocht `0x4081c0` die buiten de
   kd-sectoren −1 geeft? de zichtbare-sectorlijst?). Port: alleen de 8 record-instanties tonen, alleen op pagina 3
   (gedaan; verborgen bij de resultaatlevering en bij elke andere pagina).
   Controle met `tools/wtrace.py`: breakpoint `0x4077f0` met `ecx` = instantie 105, `inst+0x1c` loggen op pagina 1.
2. Volgorde/teken van de drie hoeken in `0x489780` (pitch −10°, yaw θ) en de invloed van de niet-uniforme schaal
   op de oriëntatierijen (`0x4894xx` normaliseert achteraf). De port-lezing (§4.4) ziet er goed uit; bit-exact is ze niet
   nagelopen.
3. Welke gezichten horen bij welk personage: de code gebruikt voor Knothead (0,63) en voor Splinter (63,0) van
   afbeelding 61 (zowel slotpaneel als statistiek: `+0x58` = 2 resp. 1). HUD_TEXT §4.2 noemt sprite 1 = "karakter 1";
   dat label is mogelijk verwisseld.
4. Wat .ins-animaties 1 en 2 van de figuren inhoudelijk zijn (sprongetje / juichlus?) en of snelheid 3 klopt met wat
   je ziet (0,27 s + lus van 0,6 s lijkt snel).
5. `vt[6]` van pagina 3 (`0x45fff0`: 2 als het gekozen record vrij is, anders 1): geen lezer gevonden.
6. Muziekobject `[0x5e61a4]->vt[0x50]/vt[0x54](0.5)` in de opslaanketen (dempen?); `[0x5e618c]` (trilling) op de pc.
7. `save+0x14a0` ("extra score"): geen schrijver gevonden.
8. Pagina 4 (high scores, klasse `0x45bfb0`) is niet ontleed en niet geport (§4.7); "SEE HIGH SCORES" doet in de
   port niets.
9. De exacte afronding van `fistp` bij 108,5 (ringbron) en 50,5 (ring-y): afhankelijk van de FPU-afrondingsmodus
   (standaard: naar even ⇒ 108 resp. 50).
10. Alles is statisch; aanbevolen controle met `tools/wtrace.py`: breakpoints op `0x4051da`, `0x4052db` (slot),
    `0x45ee50` (PLAY) en `0x404b60` tijdens Load game → slot 1 → WOODY → PLAY.
