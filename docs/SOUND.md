# SOUND: geluidsmanager, scriptberichten 1600..1657, muziek (Music.bf), engine-geluiden, animatie-events

Statische analyse van `Woody.exe` (imagebase 0x400000). Elk feit heeft een adres; "onzeker" = niet hard bewezen.
Zie ook MESSAGES.md (routering), RCK.md (bank/`RckGet`), CINEMATIC.md (rtc-streams), FORMAT_INS.md (event-tracks).
Hulpscripts voor deze analyse stonden in de scratchmap (niet in de repo); de Music.bf-lezer in §4.2 is tegen
`extract/Music.bf` geverifieerd (directory exact 1417 bytes verbruikt, alle 49 entries beginnen met `RIFF` en
`RIFF-size + 8 == entry-size`).

## 0. Samenvatting

* Er zijn **drie objecten**:
  1. het *geluidssysteem* `[0x5e81b0]` = `app+0x1c` (0x10c B, ctor `0x4691a0`, vtable `0x4abaa0`): wrapper om de
     Cryo Sound Library (lib-object inline op `+0x24`), leest de cfg-globals en maakt de manager (`vt[0x3c]` = `0x4694b0`);
  2. de **geluidsmanager** `[0x4c2dd8]` = `[0x5e61a4]` = `app+0x20` (0x1b950 B, ctor `0x4696d0` → basis `0x4699e0` → `0x4687c0`,
     vtable `0x4abb08`; tussenklasse `0x4abbb8`, abstracte basis `0x4ab994`). Dit is het doel van de scriptberichten
     1600..1657 (`0x467fa0`) én van de muziek/rtc-aanroepen;
  3. de **SoundFx-wachtrij** `[0x5e48c8]` (0x20 B, `0x44e700` → `0x4670d0(0x20, 0x20)` → `0x468910`, vtable `0x4ab8ec`):
     engine-code roept `0x468a00(id, inst)` aan met een *event-id* 0..67; de tabel `0x5e5b28` (24 B/record, gevuld door de
     statische initialisator `0x4661a0`) vertaalt dat naar een geluid uit de **karakterbank** (bank 0) + volume + lus + keten.
* Geluids-id in scripts/records/animatie-events = **RckGet-ref** `bank<<24 | type<<16 | index` (`0x46c330` → `0x441580`).
  Scripts gebruiken `0x0100xxxx` = geluid xxxx van de **levelbank**; de engine-tabel en de animatie-events gebruiken
  kale indices = bank 0 = `Common/<karakter>.rck` (6413 van de 6422 animatie-events; 9 met `0x0100xxxx`).
* Volume is **0..100** (klem in `0x469c6b`/`0x469ded`/`0x46a078`), lineair: de lib zet `mB = −2000·log10(100/v)`
  (`0x48bf50`, `0x4abec0` = −2000) ⇒ amplitude = v/100; daarna × mastervolume sfx `[0x5e81ec]` = cfg/100.
* 3D: in deze build altijd **DirectSound3D-hardware** (`mgr+0x34` blijft 0, enige schrijver `0x469a54`); de engine bevat ook
  een volledig softwarepad (`0x46ba44`) met exact het model dat wij kunnen overnemen: `gain = 1` voor `d ≤ dmin`, anders
  `dmin / min(d, dmax)`, pan = `100·dot(rechts, richting)`. Afstanden in **meters = wereldeenheden × 0.01** (`0x4a94f8`).
  `dmax = 50·dmin`, afkapafstand `= 10·dmin` (voice wordt in 0.5 s uitgefaded en vrijgegeven, en komt terug met 0.2 s fade-in).
  Standaard `dmin = 2.0 m` ⇒ hoorbaar tot 20 m = 2000 eenheden. Geen doppler (snelheid nooit gezet).
* Max. **24 fysieke** stemmen (`0x46c160`: `mgr+0x3c < 0x18`), 512 logische (`0x46aa20`), geen prioriteiten: bij vol wordt een
  nieuwe 3D-stem niet gestart (blijft logisch en probeert het elk frame opnieuw), een 2D-stem blijft in de wachtlijst.
* **Muziek**: bericht **1655 [track]** = `PlayMusic(track)` (`vt[0x48]` = `0x469870` → `0x46c850`); `track` = index in de
  naamtabel `0x4b73a0` (49 namen, teller `0x4b7464`) → bestand in `Music.bf`. Elk levelscript stuurt dit in object 0.
  Looping stream (vlag 3), start op vol volume; stoppen met fade via `vt[0x4c]`. Titel: engine `0x404e30` (afwisselend
  Menu02/Menu).
* **Voetstappen en de meeste Perso/vijand-geluiden zijn animatie-events type 4** in het `.ins` (`0x43a8f0` → `0x4695f0`),
  geen code.

## 1. Scriptberichten 1600..1657 (`0x467fa0`, sprongtabel `0x4686b0`, 58 entries)

Berichtblok: `msg[0]` = id, `msg[2..]` = argumenten (`[esi+8]`, `+0xc`, …). `inst` = instantieref: `[0x50944c]->0x6c[arg & 0xffffff]`
(ook kale slotnummers). Schalen: `·0.01` = `0x4aa0ac`, `·(−0.01)` = `0x4ab990`. Constanten: `1e10` = `0x501502f9`,
`1e13` = `0x551184e7`: beide zijn de **maximale speelduur van een lus in seconden** (`voice+0x10`; test `start + dur ≤ nu`
⇒ "Killing by signal", `0x46b169`) = "oneindig". Er is geen ander verschil tussen 1e10 en 1e13.

De parameter **f** (toonhoogte) heeft twee vormen (`0x469cbd`/`0x469e31`/`0x46a0bd`):
* `f > 0` → `0x46ac10`: afspeelfrequentie = `f × samplerate` (f = 1 ⇒ `voice+0x18 = 0` = origineel);
* `f ≤ 0` → `0x46ac80(−f)`: **−f is de gewenste duur in seconden**: factor = `natuurlijke duur / (−f)`.
  Berichten met "dur" hieronder sturen `arg·(−0.01)`, dus `arg` = gewenste duur in 1/100 s.

Managerfuncties (alle via `0x469c10` (2D), `0x469d60` (3D eenmalig), `0x46a010` (3D lus)):

| vt | adres | signatuur |
|---|---|---|
| +0x24 | `0x469b80` | Play2D(id, inst, f, vol) – eenmalig, direct |
| +0x28 | `0x469bb0` | Play2DLoop(id, inst, f, vol, maxdur) – `voice+8 = 2` |
| +0x2c | `0x469b50` | als +0x24 maar **in de wachtrij**: start pas als de vorige 2D-stem (`mgr+0x30`, keten `voice+0x40`) klaar is (`0x46af41`) |
| +0x30 | `0x469be0` | als +0x28, in de wachtrij |
| +0x34 | `0x469d30` | Play3D(id, inst, queue, vol, f) met standaard-dmin `vt[0x14]()` = 2.0 → `vt[0x38]` |
| +0x38 | `0x469d60` | Play3D(id, inst, queue, f, vol, dmin) – eenmalig; `queue=1`: achter de keten van deze instantie (start als de vorige klaar is, `0x469ebe`), `queue=0`: parallel |
| +0x3c | `0x469fd0` | (niet door scripts gebruikt) 3D-lus met standaard-dmin en vol 100 |
| +0x40 | `0x46a010` | Play3DLoop(id, inst, queue, f, maxdur, vol, dmin) – `voice+8 = 3` |

Gebruik = statische telling van `SEND` over alle 28 `extract/Data/*/code`.

| id | args | gebruik | aanroep | betekenis |
|---|---|---|---|---|
| 1600 | id, vol | 54 | `vt[0x24](id,0,1.0,vol)` `0x467fe4` | 2D eenmalig |
| 1601 | id, vol, f | 0 | `0x468001` | 2D, toonhoogte f·0.01 |
| 1602 | id, vol, dur | 101 | `0x468026` | 2D, uitgerekt tot dur/100 s (meestal 100; ook 300/330/500) |
| 1603..1605 | als 1600..1602 | 0 | `vt[0x2c]` | 2D in de wachtrij |
| 1606 | id, vol | 78 | `vt[0x28](id,0,1.0,vol,1e13)` `0x4680b2` | **2D-lus** (vol typisch 5..25) |
| 1607 / 1608,1610 | id, vol, f / id, vol, f, t | 0 | `0x4680d4` / `0x468107` | 2D-lus met f; 1608/1610 met maxdur t·0.01 |
| 1609 | id, vol, dur | 14 | `0x4680dd` | 2D-lus uitgerekt tot dur/100 s (KWS/SWS: 40) |
| 1611..1615 | als 1606..1610 | 0 | `vt[0x30]` | 2D-lus in de wachtrij |
| 1616 | inst, id, vol | 0 | `vt[0x24](id, inst, 1.0, vol)` `0x4681c0` | 2D met eigenaar (inst wordt alleen bewaard in `voice+0x64`) |
| 1617 / 1618,1619 | inst, id, vol / +dur | 0 | `vt[0x2c]` | idem in de wachtrij; 1618/19: f = −arg3 (let op: hier géén ·0.01, `0x46820a`) |
| **1620** | inst, id, vol | 507 | `vt[0x40](id,inst,0,1.0,1e10,vol,2.0)` `0x468236` | **3D-lus**, dmin 2 m |
| 1621 | inst, id, vol, f | 37 | `0x468273` | 3D-lus, toonhoogte f·0.01 |
| **1622** | inst, id, vol | 220 | `vt[0x34](id,inst,0,vol,1.0)` `0x4683f4` | **3D eenmalig**, dmin 2 m |
| 1623 | inst, id, vol, f | 9 | `0x468406` | 3D eenmalig, toonhoogte f·0.01 (150 = ×1.5) |
| 1624 | inst, id, vol | 1 | `0x46843a` | 3D eenmalig, in de instantie-wachtrij |
| 1625 / 1626 | inst, id, vol, f / dur | 0 | `0x468443` / `0x468450` | idem in de wachtrij |
| **1627** | inst, id, vol, dur | 314 | `0x468420` | 3D eenmalig, uitgerekt tot dur/100 s (110, 150, 100, 200 …) |
| **1628** | inst, id, t | 194 | `vt[0x58](id, inst, t·0.01)` `0x4685ab` → `0x46a670` → `0x46a2b0` | **stop** de 3D-stemmen van *(inst, id)*: spelende stemmen faden uit in t/100 s (toestand 5; `t < 0.01` ⇒ direct, vlag `voice+5`), nog niet spelende/wachtende knopen van dat sample worden verwijderd. Sleutel = **(instantie, sample)** |
| 1629 | inst, id, vol, dur, t | 2 | `0x4682b8` | 3D-lus uitgerekt tot dur/100 s, maxdur t/100 s (t < 0 ⇒ 1e10), dmin 2 m |
| **1630** | inst, id, vol, dmin | 239 | `vt[0x40](id,inst,0,1.0,1e13,vol,dmin·0.01)` `0x468332` | 3D-lus met eigen dmin (300..1000 ⇒ 3..10 m) |
| 1631 | inst, id, vol, f, dmin | 22 | `0x468354` | + toonhoogte |
| 1632 | inst, id, vol, dur, t, dmin | 6 | `0x4683a0` | + uitgerekt tot dur/100 s, maxdur t/100 s |
| 1633 | inst, id, vol, dmin | 17 | `vt[0x38](id,inst,0,1.0,vol,dmin·0.01)` `0x46848a` | 3D eenmalig met dmin |
| 1634 | inst, id, vol, f, dmin | 67 | `0x4684ac` | + toonhoogte |
| 1635 | inst, id, vol, dur, dmin | 23 | `0x4684d6` | + uitgerekt tot dur/100 s |
| 1636..1638 | als 1633..1635 | 0 | `0x468500`, `0x46851d`, `0x468564` | in de instantie-wachtrij |
| 1639..1645, 1647, 1648, 1651 | – | 0 | `0x4686aa` | niets |
| 1646 / 1656 | t | 0 | `vt[0x4c](t·0.01)` `0x468671` | **StopMusic** met fade t/100 s (0 = direct) |
| 1649 / 1650 | – | 0 | `0x41fa40` / `0x41fa50` op `[0x4c737c]` | camera-manager, geen geluid (onzeker wat) |
| **1652** | id, t, mask, 0 | 84 | `vt[0x80](id, t·0.01, mask, x)` `0x4685dc` → `0x46c390` | **stop 2D**: alle spelende niet-3D-stemmen van dit sample faden uit in t/100 s en stoppen; mask 3 = alle, **1 = alleen lussen**, 2 = alleen eenmalige. Sleutel = **alleen id** |
| 1653 | type, freq, amp | 0 | `vt[0x64]` + `vt[0x68]` `0x468603` | LFO voor de *volgende* stem (volume- resp. toonhoogte-modulatie, golfvorm 1..4: sinus/|sinus|/blok/ruis `0x4698f0`), daarna `0x41fa40` |
| 1654 | type, freq, amp | 0 | `0x46862c` | alleen `vt[0x64]`+`vt[0x68]` |
| **1655** | track | 26 | `vt[0x48](track)` `0x468689` | **PlayMusic** (§4) – *niet* StopSound zoals MESSAGES.md zei |
| 1657 | t | 3 | `vt[0x6c](t·0.01)` `0x468698` → `0x46c500` | fade-in-tijd voor de **eerstvolgende** gestarte stem (`[0x5e8230]` → `voice+0x54`; wordt bij elke Play gewist, `0x469c23`) |

Typische scriptpatronen: `1620 inst id vol` … later `1628 inst id 10` (lus uit in 0.1 s); `1606 id 5` … `1652 id 20 1 0`.
Een volume/pitch-*update*-bericht bestaat niet in de scripts (de manager heeft wel `vt[0x5c]` SetVolume en `vt[0x60]` SetPitch,
alleen door engine-code gebruikt).

## 2. De manager `[0x4c2dd8]` (vtable `0x4abb08`)

| vt | adres | functie |
|---|---|---|
| +0x04 | `0x46a7e0` | **Update(a, instList, n)**: klok (`0x468840`: `mgr+0x14` = nu = `[0x509adc]+0x30`, `+0x1c` = dt), debugtekst, als sfx aan (`[0x5e81b4]==1`) `0x46a8a0`, als muziek aan (`[0x5e81b8]==1`) `0x46c760`. Overgeslagen als `mgr+0x10 == 0` (pauze) |
| +0x08..+0x1c | `0x4688a0..f0` | set/get standaard dmin (`mgr+4` = 2.0), dmax (`+8` = 2000.0), afkap (`+0xc` = 10.0) (`0x4687d4`) |
| +0x20 | `0x46b020` | SetInstanceCount(n): tabel `mgr+0x1b910` (per instantie een lijst 3D-knopen); bij level-load `0x40459a` |
| +0x24..+0x40 | | Play-varianten, §1 |
| +0x44 | `0x46cbf0` | muziek-update los (optiemenu `0x4602f2`) |
| +0x48 | `0x469870` | PlayMusic(track) (schakelt de lib tijdelijk naar de BigFile `mgr+0x1b94c`, `0x491080`) |
| +0x4c | `0x46c940` | StopMusic(fade) |
| +0x50 / +0x54 | `0x46c9f0` / `0x46ca90` | PauseMusic(fade) / ResumeMusic(fade) – pauzemenu `0x4053e9` / `0x405698` met 0.5 s; cinematic 0.45 s |
| +0x58 | `0x46a670` | Stop3D(id, inst, fade) |
| +0x5c | `0x46a430` | SetVolume(id, instIdx of −1, vol): `voice+0x44` van alle stemmen met dit sample (−1 = alle 2D, logisch + fysiek) |
| +0x60 | `0x46a500` | SetPitch(id, instIdx of −1, factor): `voice+0x1c` (vermenigvuldigt de frequentie, `0x46b693`) |
| +0x64 / +0x68 | `0x46c420` / `0x46c490` | LFO volume / toonhoogte voor de volgende stem |
| +0x6c | `0x46c500` | fade-in voor de volgende stem |
| +0x70 / +0x74 | `0x46c190` / `0x46c240` | rck-lader/vrijgave van type 0 (leest 8 B itemkop + 16 B `{nbytes, rate, bits, channels}` + PCM, `0x46c280`/`0x46c300`; telt `[0x5e822c]` = gebruikt geluidsgeheugen, max. `0x6acfc0` = 7 MB) |
| +0x7c | `0x46c340` | FadeOutAll2D(t) |
| +0x80 | `0x46c390` | Stop2D(id, fade, mask) |
| +0x84 | `0x46c5e0` | GetDuration(id) in s = bytes / bytes-per-seconde |
| +0x88 | `0x46c630` | **Suspend**: alle fysieke stemmen stoppen; 2D terug naar de logische lijst, 3D-knopen op "niet spelend" (pauze `0x404d8f`, daarna `mgr+0x10 = 0`) |
| +0x8c | `0x46c6e0` | StopAll (Suspend + alle logische stemmen en 3D-knopen vrijgeven) – level verlaten `0x4049e0` |
| +0x90 | `0x46c740` | `mgr+0x2c` = "gelockt" sample (3D-stemmen van dat sample overleven "niet verwerkt", `0x46b4aa`) |
| +0x94 / +0x98 / +0x9c | `0x46cc00` / `0x4698b0` / `0x46cc60` | rtc-stream kiezen / starten / stoppen (§6) |

### 2.1 Stem (0xc0 B, pool `mgr+0x908`, 512 stuks; `0x46a6b0` alloc, `0x46a7a0` free)

`+8` type: 0 = 2D, 1 = 3D, 2 = 2D-lus, 3 = 3D-lus · `+0xc` starttijd · `+0x10` maxdur (lus) · `+0x14` startoffset in bytes
(`0x46ab40`: een lus die later hoorbaar wordt start op `(nu − start) mod duur`) · `+0x18` frequentie (0 = origineel) · `+0x1c`
pitchfactor · `+0x20` lib-handle · `+0x24` sample · `+0x28/+0x34` LFO's · `+0x44` **volume 0..100** · `+0x48` huidig volume ·
`+0x4c` doel · `+0x50` fadetoestand · `+0x54` fadeduur · `+0x58` verstreken · `+0x5c` startvolume · `+0x60` 3D-knoop ·
`+0x64` instantie · `+0x68` dmin · `+0x6c` dmax · `+0x70` afkap · `+0x74` pan · `+0x78` afstandsgain.

Fadetoestanden (`0x46b530`, tabel `0x46b7c8`): 0 = geen (`cur = vol`); 1 = fade-in `cur = lerp(start, vol, t/T)`;
2 = "killing softly" naar 0 (T = 0.5 `0x4abbac`); 3 = gedempt naar `0.5·vol` (T = 0.25 `0x4abbb0`);
4 = terug van gedempt naar vol (T = 0.25 `0x4abbb4`); 5 = uitfaden en daarna stoppen (Stop-berichten).
Toestand 3/4 komt van **occlusie**: `0x46b920` doet per frame een zichtlijntest luisteraar → bron (`0x428cf0`, wereld
`[0x50944c]`); geblokkeerd ⇒ volume naar de helft. (Voor de port optioneel.)

### 2.2 Per frame (`0x46a8a0`)

1. `0x46c510`: door de lib beëindigde stemmen opruimen (`0x48d8a0`).
2. Luisteraar = object `mgr+0x28`: positie `+0x90..0x98` × 0.01, oriëntatie uit de matrix op `+0`: vectoren
   `(m[1],m[4],m[7])` en `(m[2],m[5],m[8])` (`0x46a909..0x46a943` → `0x48f8e0`), "rechts" = `(m[0],m[3],m[6])` (`0x46baf8`).
   Die layout (3×3 + positie op +0x90) is de **camera-Repere** (CAMERA.md: `Repere+0x90` = camerapositie) ⇒ de luisteraar
   is de **camera**, niet de speler. Waar `mgr+0x28` gezet wordt is niet gevonden (onzeker; geen directe schrijver via
   `[0x5e61a4]`/`[0x4c2dd8]`).
3. De meegegeven instantielijst markeert 3D-knopen als "verwerkt dit frame" (`node+8 = mgr+0x38`); een 3D-lus van een
   niet-verwerkte (niet actieve/zichtbare) instantie wordt in 0.5 s uitgefaded ("Killing softly cause not processed",
   `0x46b19c`). Welke lijst de aanroeper meegeeft is niet nagegaan (onzeker; vermoedelijk de actieve instanties `0x4c2d08`).
4. `0x46bbb0`: 2D-stemmen: fade bijwerken, klaar/onhoorbaar (`cur < 5` met doel 0) ⇒ stoppen.
5. `0x46b080`: 3D-stemmen: `d = |bron − luisteraar|`; `d > afkap·100` ⇒ uitfaden/stoppen; onhoorbaar ⇒ stoppen (knoop blijft);
   maxdur verstreken of killvlag ⇒ stoppen + knoop weg (+ volgende in de keten starten).
6. `0x46bcf0`: per instantie in de lijst: afgelopen eenmalige knopen opruimen (`0x46a190`: `start + duur ≤ nu`), en
   niet-spelende knopen binnen `afkap·100` **(her)starten** (`0x46be50`; lussen met fade-in 0.2 s `0x4abba8`), mits < 24 stemmen.
7. `0x46ace0`: wachtende 2D-stemmen starten.

Volume naar de lib: `ftol(cur · [0x5e81ec])` (2D `0x46b791`; 3D in softwaremodus `cur · gain(+0x78) · master` `0x46b69f`).

### 2.3 3D-model

Hardwarepad (actief): per stem `SetPosition(pos·0.01)` (`0x490100`), `SetMinDistance(+0x68)` (`0x490020`),
`SetMaxDistance(+0x6c)` (`0x48ffb0`), DS3D-standaard rolloff ⇒ `gain = dmin/d` tussen dmin en dmax, constant daarbuiten.
Softwarepad (`0x46ba44`, identiek model, gebruik dit in de port):

```c
d = len(src - lis) * 0.01f;                 /* 0x4aa0ac */
if (dmin <= 0) dmin = 1;  if (dmax < dmin) dmax = 1e7f;   /* 0x46ba8a, 0x46baa1 (0x4b189680) */
if (d > dmax) d = dmax;
gain = (d > dmin) ? dmin / d : 1.0f;        /* 0x46bad8 */
pan  = ftol(100 * dot(normalize(right), normalize(src - lis)));   /* -100..100, 0x46bb8d */
```
Pan → lib (`0x48f520`): het andere kanaal wordt met `−2000·log10(100/(100−|pan|))` mB verzwakt (lineair `(100−|pan|)/100`).
`0x46b7e0` verschuift de bronpositie bij `[0x5e81c0]` (cfg `0x4c2c44`) ≠ 0 langs de kijkas (factor −2, `0x4a9504`); doel
onzeker (3D-"focus"-optie), voor de port weglaten.

### 2.4 Configuratie (`0x4691e2..0x4692b9`, uit Woody.cfg-globals)

`[0x4c2c38]` sfx aan → `0x5e81b4` · `[0x4c2c3c]` muziek aan → `0x5e81b8` · `[0x4c2c40]` → `0x5e81bc` · OR van de drie →
`0x5e81e8` (lib init `0x469360`, anders stil) · `[0x4c2c50]` **sfx-volume 0..100** → `0x5e81ec = v·0.01` · `[0x4c2c54]`
**muziekvolume** → `0x5e81f0` · `[0x4c2c58]` derde volume → `0x5e81f4` (geen lezer gevonden) · `[0x4c2c68]` speakerconfig
1..8 (`0x4693b0`). Setters voor het optiemenu: `vt[0x54/0x5c/0x64]` van het systeem (`0x469570`, `0x4695a0`, `0x4695d0`).

## 3. Animatie-events type 4 = geluid (`.ins`)

`0x42f5e0` (instantie-animatietick, alleen als de instantie dit frame getekend is: `inst+0xc4 == frame−1`) →
`0x43a880(model, animVorig, animNu, tVorig, tNu, buf, wrap)` → `0x43a8f0` verzamelt de events van de **root-node** met
`tVorig ≤ t < tNu` (bij wrap: `[tVorig, einde)` + `[0, tNu)`) → `0x4695f0(inst, buf, n)`.

Record (9 dwords, FORMAT_INS.md §7.3):

| dword | type | betekenis | bewijs |
|---|---|---|---|
| 0 | u32 | 4 | `0x43a94f` |
| 1 | f32 | tijd in de animatie (zelfde eenheid als de keyframes) | `0x43a95a` |
| 2 | u32 | geluids-ref (RckGet; meestal bank 0) | `0x46968a` |
| 3, 4 | f32 | kansvenster `[lo, hi)` in procenten: één trekking `r = max(0, rand01·100 − 1)` **per aanroep** (`0x469600`), het record speelt als `lo ≤ r < hi` ⇒ varianten sluiten elkaar uit | `0x469646..0x46965f` |
| 5 | f32 | volume 0..100 (data: 50, soms 20) | `0x469680` |
| 6 | i32 | toonhoogte in % (100; 90..120) × `inst->vt[0x6c]()` (basis `0x403fd0` = 1.0; vijanden `0x40d830` = `enemy+0x170`) | `0x469668` |
| 7 | f32 | dmin in cm (200 ⇒ 2 m) | `0x469695` |
| 8 | u32 | 0 | |

Is de instantie de **Perso** (`[0x53a34c]`, gezet in SetTypeInstance `0x40361e`) ⇒ **2D** `vt[0x24](ref, 0, pitch, vol)`
(`0x469690`), anders 3D `vt[0x38](ref, inst, 0, pitch, vol, dmin·0.01)` (`0x4696b6`).
Voorbeelden Woody-model (W1A model 0): ren-animatie 49: t=6 en t=15 → refs 13/14/15/16 elk 25 % = **voetstappen**;
anim 22/26/64/67/68 → 55/56/57 (sprongkreten); anim 53 → 17..20 (50 % op vol 50, 50 % op vol 20); anim 57 → 21..23; enz.
Type 5 (6 dwords) wordt hier overgeslagen (`0x43a955`) en is dus geen geluid.

## 4. Muziek

### 4.1 Afspelen

`0x46c850(track)`: alleen als muziek aan en `0 ≤ track < 49`; loopt er al iets ⇒ `vt[0x4c](0)` (direct stoppen, geen
crossfade); volume = muziekmaster; `0x490f30(naam, 3, &handle)` opent de stream **met vlag 3 = lus** (rtc gebruikt vlag 0 =
eenmalig, `0x46cc38`; betekenis van de bits verder onzeker). Volume per frame (`0x46cb30`/`0x46cb70`):
`vol = lerp(vol, doel, (nu − t0)/T)`, doel = `[0x5e81f0]`, naar de lib als `ftol(vol·100)` (`0x48f4e0`).
Levelwissel: `RequestLevel 0x404b60` → `vt[0x4c](fade·0.9)` + `vt[0x9c]()` (`0x404b95`; 1.5 s-fade ⇒ muziek 1.35 s uit);
idem `0x404cde`. De nieuwe track start pas door 1655 in het nieuwe levelscript (object 0, tijdens init).
Pauzemenu: `vt[0x50](0.5)` / `vt[0x54](0.5)`. Titelscherm `0x404e30`: teller `app+0x54` wisselt 0/1: **track 48 (Menu02)**
bij 0, **track 0 (Menu)** bij 1. House/Lang hebben geen 1655 (House = titel/hub-decor ⇒ menumuziek van de engine).

| level | 1655-track | bestand | | level | track | bestand |
|---|---|---|---|---|---|---|
| WWS / KWS / SWS (hubs) | 1 | /Game/WS.wav | | W1B | 41 | /Game/1B.wav |
| W1A / K1A / S1A, Credits | 2 | /Game/1A.wav | | W2B | 42 | /Game/2B.wav |
| W2A / K2A / S2A, Blackbox | 3 | /Game/2A.wav | | W3B | 43 | /Game/3B.wav |
| W3A / K3A / S3A | 4 | /Game/3A.wav | | W3C | 45 | /Game/3C.wav |
| K1R / S1R | 5 | /Game/1R.wav | | W2D | 46 | /Game/2D.wav |
| K2R / S2R | 6 | /Game/2R.wav | | W3D | 47 | /Game/3D.wav |
| K3R / S3R | 7 | /Game/3R.wav | | titel | 48 / 0 | Menu02 / Menu |

Track 44 (/Game/2C.wav) wordt door geen script gebruikt. Volledige tabel `0x4b73a0`: 0 Menu, 1 WS, 2 1A, 3 2A, 4 3A, 5 1R,
6 2R, 7 3R, 8 /Rtc/Menu, 9..22 /Rtc/Woody/{W1A, W1Ba, W1Bb, W2B, W2Da, W2Db, W2Dc, W3B, W3Da..d, WWSa, WWSb},
23..32 /Rtc/Knothead/{K1A, K1R, K2A, K2B, K2D, K2R, K3A, K3B, K3R, KWS}, 33..40 /Rtc/Splinter/{S1R, S2A, S2B, S2R, S3A, S3D,
S3R, SWS}, 41 1B, 42 2B, 43 3B, 44 2C, 45 3C, 46 2D, 47 3D, 48 Menu02.

### 4.2 Music.bf (CryoBF 2.01) – geverifieerd

Geopend in de manager-ctor `0x469762` (`Music.bf`, anders `<cd>\Music.bf` `0x469781`).

```
0x00 char[16] "CryoBF - 2.01.0\x1a"
0x10 u32 0, u32 0
0x18 u32 dirOffset   (306843484; directory loopt tot het einde van het bestand, 1417 B)
0x1c u32 dataOffset  (32)
dir:  u32 n; n × entry
entry: u32 nameLen; char name[nameLen] (geen \0); u32 kind
       kind 1 = map:     u32 nChildren; nChildren × entry (recursief)
       kind 2 = bestand: u32 size; u32 size2 (= size, ongecomprimeerd); u32 offset  → bestand op dataOffset + offset
```
Boom: `Game/` (16), `Rtc/` {`Knothead/` (10), `Menu.wav`, `Splinter/` (8), `Woody/` (14)}; pad = `/Game/1A.wav`.
Elk bestand is een canonieke 44-byte-header-WAV (PCM 16 bit stereo; `data` op +36, PCM op +44). **Niet alles is 44.1 kHz**:
`3A, 3B, 3C, 3D` en **alle /Rtc/** zijn 22050 Hz; de rest 44100 Hz. Duur: levelmuziek 103..128 s, Menu 38 s, WS 69 s, rtc 4..53 s.

## 5. Engine-geluiden: SoundFx `[0x5e48c8]`

`0x468a00(id, inst)` zet (id, inst) in de wachtrij (max 0x20, `+0x14`); `0x468ba0(dt)` (hoofdlus `0x401ec2`, niet tijdens
pauze tenzij `app+0xe4`) verplaatst ze naar de actieve lijst (`+0x18`, 16 B: id, record*, timer, inst) en speelt het record
`rec = 0x5e5b28 + 24·id` (`0x4670f0`): `{i32 ref, f32 vol, f32 pitch, f32 maxdur, i32 next, f32 delay}`:
`maxdur > 0` ⇒ lus (`vt[0x40]` met inst / `vt[0x28]` zonder, `0x468cbb`), anders eenmalig (`vt[0x38]` / `vt[0x24]`);
met `inst` dus 3D (dmin 2 m), met `inst = 0` 2D. `pitch` wordt vervangen door `this+0x1c` als die ≠ 1.0 (`0x468c82`; geen
schrijver gevonden). Na het starten: `next < 0` ⇒ item weg (lussen ook: ze lopen door in de manager); anders na `delay` s
(`delay < 0` ⇒ sampleduur `vt[0x84]`) door naar record `next`. `0x468a30(id, inst)` = stop (`vt[0x58](ref, inst, 0)` of
`vt[0x80](ref, 0, 3, 0)`); `0x468980` = alles stoppen (level verlaten `0x44e7b0`).
Hulpobject "bron" (1 byte vlaggen, o.a. `Perso+0x4a4`): `0x468e40` = "dit frame actief", `0x468e50(src, inst, id, fx, vol)` start
de lus bij de eerste actieve frame, zet bij `vol ≥ 0` het volume (`0x468ad0` → `vt[0x5c]`), en stopt haar zodra een frame
niet actief was; `0x468e20` = geforceerd stoppen.

Tabel (emulatie van `0x4661a0..0x4670c4`; ref = index in `Common/<karakter>.rck`, vol 0..100, pitch overal 1.0):

| id | ref | vol | lus | aanroepers (adres van de `call`) – gebeurtenis |
|---|---|---|---|---|
| 0 | 0 | 50 | | `0x44f43b` bonus type 30 (extra leven); `0x44b71a` 25 bonussen bij volle health |
| 1 | 2 | 50 | | `0x44f72a` bonus type 36 |
| 2 | 3 | 50 | | `0x44f4ce` bonus type 35 (na afloop) |
| 3 | 1 | 50 | | `0x44f616` bonus type 34 (gewone bonus) |
| 4 | 108 | 50 | | `0x44b6e4` 25 bonussen verzameld |
| 5 | 106 | 50 | | `0x44f95c` bonus type 37 (race) |
| 6 | 4 | 50 | | `0x44d730` object/vijand vernietigd (3D, inst); `0x45352c` klasse 20/21; menu's `0x484ca1..0x484df5`, `0x4856f3` (2D, bevestigen) |
| 7 | 11 | 100 | | `0x451e53`, `0x451ebb` blikseminslag (3D) |
| 8 | 109 | 100 | | `0x451d2d` donder-aankondiging |
| 9 | 107 | 50 | | "mag niet": `0x44ba61`, `0x458cd5`, `0x45917b`, `0x4597b7` |
| 10 | 10 | 50 | | `0x453468` klasse 20/21 (projectielwerper) |
| 11 | 5 | 50 | ∞ | `0x4536ce` klasse 20/21, bron `+0x190` |
| 12 | 12 | 53 | | `0x449c24` projectiel-inslag (3D op `obj+0xa4`) |
| 13 | 6 | 51 | ∞ | – (geen aanroeper gevonden) |
| 14 | 9 | 52 | | `0x4526f6` werper vuurt; `0x45344a` |
| 15 | 7 | 50 | ∞ | `0x4536e8` klasse 20/21, bron `+0x191` |
| 16 | 8 | 50 | | `0x45321a` klasse 20/21 |
| 17, 18 | 69, 70 | 50 | | `0x449263` projectiel (`0x449130`, 2 varianten); 18 ook `0x487854` (menu) |
| 19, 20 | 74, 75 | 25 | | `0x449283`, `0x4492a3` projectiel |
| 21..24 | 17..20 | 50 | | 23/24: `0x48676b` menu (cursor?) – onzeker |
| 25→26→27→28→25 | 13, 14, 15, 16 | 50 | keten, delay 0.3 s | `0x4866c6` (bron `+0x84` in de menu/HUD-klasse `0x486560`): voetstap-cyclus – onzeker waarvoor |
| 29..31 | 21..23 | 50 | | `0x486a2a/45/60` |
| 32, 33 | 32, 34 | 50 | | `0x48671d`, `0x4867cb` |
| 34, 35 | 55, 56 | 50 | | `0x4867b5` |
| 36 | 57 | 50 | | – |
| 37 | 12 | 50 | | `0x4855f7` |
| 38 | 9 | 50 | | `0x40e4db` Boss2 |
| 39 | 76 | 50 | ∞ (1e17) | `0x410553` (bron), gestopt `0x40fc9d`/`0x40fcde` – baas type 14 |
| 40..43 | 77, 78, 80, 79 | 50 | | `0x40f9e5` (40), `0x40f291`/`0x40fb78` (41), `0x40fef1` (42), `0x40fcc6` (43) – baas type 14, modus `+0x228 == 1` |
| 44 | 81 | 50 | ∞ | `0x40ddd4` start / `0x40e147` stop (Boss2, 3D); `0x410569`, `0x40fcf3` |
| 45..48 | 82, 83, 85, 84 | 50 | | als 40..43 voor modus ≠ 1; 47 ook `0x40e91f`; 48 ook `0x40e5f5`, `0x40fcde` |
| 49 | 12 | 100 | | `0x410a77` |
| 50..54 | 86..90 | 50 | | `0x41132a`, `0x4113fb`, `0x411a56`, `0x41178f`, `0x411af2` (vijand type 12) |
| 55..57 | 96, 97, 98 | 50 | | `0x45752b..0x457552`: luchtaanval, `0x37 + rand(0,3)` |
| 58 | 95 | 50 | | `0x465f7b`, `0x465fc4` luchtdash / dubbele sprong |
| 59 | 101 | 80 | | `0x4560ce` (Perso `0x456000`) |
| 60 | 102 | 15 | ∞ (1e20) | `0x456246` Perso-bron `+0x4a4` in toestand 1 (race/voertuig); gestopt `0x401cdf` (cinematic), `0x44c527`, `0x456163`, `0x456bb6` |
| 61 | 110 | 50 | ∞ | `0x4546ea` bron `obj+0x68` (`0x454610`) |
| 62 | 110 | 50 | | – |
| 63 | 111 | 50 | | `0x45b8ef`, `0x45baf4`, `0x45bb54` |
| 64 | 112 | 50 | | – |
| 65, 66 | 116, 117 | 50 | | `0x40d4f5` (65), `0x40cd30` start / `0x40d4e6` stop (66) – baas type 16 |
| 67 | 118 | 50 | | `0x40ced7` baas type 16 |

De gebeurtenisnamen bij de vijand/menu-adressen zijn afgeleid uit de omliggende klasse en deels onzeker; de adressen en
id's zijn hard. Schade, dood, landen en lopen van de Perso hebben **geen** code-aanroep: dat zijn animatie-events (§3).

## 6. Cinematic-audio (rtc)

Data: `/Rtc/...wav` in Music.bf (22050 Hz stereo). Bericht 1130 arg 2 = track (8 = House-intro, 9.. per level, tabel §4.1).
`vt[0x94](track)` (`0x46cc00`): vorige rtc stoppen, `mgr+0x1b918 = track`. `vt[0x98](&t0)` (`0x4698b0` → `0x46cc20`):
`0x490f30(naam, 0, &mgr+0x1b91c)` = eenmalige stream, **op vol lib-volume, zonder de muziekfader** (er wordt geen volume
gezet; onzeker of de lib de muziekmaster toepast), `t0 = 0`, geeft 1. `vt[0x9c]` (`0x46cc60`): stoppen + sluiten. De
levelmuziek wordt eromheen gepauzeerd met `vt[0x50](0.45)` / `vt[0x54](0.45)` (CINEMATIC.md §2/§3); de Perso-lus 60 wordt
gestopt (`0x401cdf`).

## 7. Recept voor de port

1. **Banken**: type-0-items van `Common/<karakter>.rck` (bank 0) en `<LVL>.rck` (bank 1) laden; ref-decodering
   `bank = ref>>24, index = ref & 0xffff`.
2. **Play-API**: `play(ref, inst|NULL, loop, vol/100, pitch, dmin)`; pitch-argument `f ≤ 0` ⇒ `pitch = duur/(−f)`.
   3D-stemmen sleutelen op (inst, ref); 2D op ref. Positie per frame uit de instantie halen (`inst->vt[0x54]` = wereldpositie).
3. **Berichten** volgens §1; minimaal 1600, 1602, 1606, 1609, 1620..1624, 1627..1635, 1652, 1655, 1657. Stop = fade naar 0 in
   t s en dan vrijgeven; `1657` = eenmalige fade-in voor de volgende play.
4. **3D**: formule §2.3 met luisteraar = camera (positie + rechts-vector), `dmax = 50·dmin`, stem laten vervallen boven
   `10·dmin` (fade 0.5 s) en lussen opnieuw starten (fade-in 0.2 s, offset `(nu − start) mod duur`) zodra ze weer binnen
   bereik komen. Constant-gain pan: `L = (pan>0 ? (100−pan)/100 : 1)`, `R` spiegelbeeld. Max. 24 stemmen, geen stealing.
5. **Animatie-events** type 4 afvuren vanuit de animatietick (venster `[tVorig, tNu)`, wrap), één random trekking per tick,
   Perso = 2D, rest 3D, pitch × `enemy+0x170`.
6. **SoundFx-tabel** §5 als constante array; `fx_play(id, inst)`, `fx_stop(id, inst)`, keten 25..28 met 0.3 s, bron-helper voor
   lussen (start/stop op "actief dit frame").
7. **Muziek**: Music.bf-directory lezen (§4.2), WAV-header overslaan (44 B), rate uit de header nemen (22050 resamplen!),
   lus; 1655 start direct op muziekvolume; levelwissel fade-uit `0.9·fade`; pauze 0.5 s fade + pauzeren; titel afwisselend 48/0.
8. **rtc**: bij cinematic-start muziek pauzeren (0.45 s), rtc-stream eenmalig afspelen, aan het eind stoppen en muziek hervatten.
9. **Pauze**: alle sfx-stemmen opschorten (`vt[0x88]`), SoundFx-tick overslaan; mastervolumes uit Woody.cfg (×0.01).

## 8. Open vragen

1. Wie zet de luisteraar `mgr+0x28` en wie roept `vt[4]` (Update) aan met welke instantielijst? (Layout wijst op de camera-Repere;
   aanroeper niet gevonden.)
2. Exacte betekenis van streamvlag 3 vs 0 in `0x490f30` (aangenomen: 3 = lus) en of rtc-streams de muziekmaster volgen.
3. `this+0x1c` van SoundFx (globale pitch) en `[0x5e81f4]` (derde volume) hebben geen gevonden schrijver/lezer.
4. De gebeurtenisnamen van de menu-/HUD-id's 21..37 en van de bazen (39..54, 63..67) – adressen kloppen, betekenis niet geverifieerd.
5. 1649/1650 (`0x41fa40`/`0x41fa50` op `[0x4c737c]`): camera-gerelateerd, niet uitgezocht; door geen script gebruikt.
6. Occlusie (§2.1, toestand 3/4) en de positieverschuiving `0x46b7e0`: nodig voor de port? Waarschijnlijk niet hoorbaar belangrijk.
7. MESSAGES.md §Geluid is op meerdere punten onjuist (1655 = muziek, 1628 = stop, 1622/1623 zijn 3D) en moet naar dit document verwijzen.
