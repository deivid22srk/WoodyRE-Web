# MENU_OPTIONS.md — menupagina 0x1b "Options" (Woody.exe, build 17-10-2001)

Alles statisch uit `out/disasm_full.txt` (imagebase 0x400000); niet getraced. Basisklasse, knipperen, logo en invoer-acties:
TITLE.md §5.1/§5.2. Geluidsmanager: SOUND.md §2/§4. Adressen zonder toelichting zijn code in Woody.exe.

## 0. Samenvatting

- Pagina 0x1b is een **gewone lijstpagina** van de gemeenschappelijke klasse (TITLE §5.1): klasse `0x4601f0`, 0x20 B, vtable
  `0x4ab614`, itemtabel `0x4b5eb0` (5 items), y-fractie 0.4, lettergrootte 30, alles gecentreerd, wit.
- Items: kop **36 "Options"**, drie schuifregelaars **38 "Sound FX volume"**, **39 "Music volume"**, **132 "Vibration"**
  (itemvlag 0x10) en **4 "Continue"** (resultaat 5). Een regelaar wordt getekend als **één tekstregel**
  `"<naam> <waarde>%"`, bv. `Sound FX volume 80%` — geen balk, geen pijltjes.
- Links/rechts: waarde −5/+5, geklemd op 0..100, alleen bij "net ingedrukt" (geen herhaling bij vasthouden), geen geluid.
  Elke stap wordt **meteen** toegepast op de geluidsmotor (muziek ook tijdens het pauzemenu).
- **Continue** (bevestigen) houdt de nieuwe waarden; **terug** (actie 5) **zet de waarden van bij binnenkomst terug**. Beide gaan
  naar pagina 1 (titel) of naar het pauzemenu 0x18/0x19 (in een level).
- Opslag: de volumes zijn de Woody.cfg-globals `[0x4c2c50]` (sfx) en `[0x4c2c54]` (muziek), die bij het afsluiten met de hele
  cfg worden weggeschreven. Daarnaast bewaart **elk save-slot** in Woody.sav een kopie (muziek, sfx, vibratie) die bij "Load game"
  terugkomt. De "4" in `u32 x[4]` van Woody.sav = **de 4 save-slots**.
- **Vibration** schrijft `[joystick]+4` (float 0..1); de rumble-methode van de pc-joystickklasse is een lege functie
  (`0x467b20` = `ret 8`), dus op pc **doet deze optie niets hoorbaars/voelbaars**. Het item staat er altijd, ook zonder joystick.
- Geen subpagina's (geen toetsen-, controller- of taalpagina). Resolutie, detail, VSync en toetsen staan in Woody.cfg en worden
  alleen door de setup (`Detect.exe`/`Setup.dll`) gewijzigd, nooit door dit menu.

## 1. Klasse, constructor, vtable

Het menu-object (`0x445e30`, TITLE §5.1) maakt de pagina als **eerste** aan: `new(0x20)` → `0x4601f0` → `menu+0x6c`
(= `menu + 4·0x1b`, `0x445e8a`). Dat gebeurt één keer bij het booten (`0x404d10` aan het eind van `0x4023a0`).
Er is geen aparte basis-ctor; de ctor zet alleen de vtable en vult de itemtabel:

```c
Options::Options() {                          /* 0x4601f0 */
    vt = 0x4ab614;
    item[1].value = cfg_sfx_vol;              /* [0x4b5ecc] = [0x4c2c50] */
    item[2].value = cfg_music_vol;            /* [0x4b5edc] = [0x4c2c54] */
    if (g_joy) item[3].value = ftol(g_joy->vib * 100.0f);   /* [0x4b5eec]; g_joy = [0x5e618c], 0x4a9010 = 100 */
}
```
Velden: `+4` selectie, `+8` invoervertraging, `+0xc`, `+0x10` resultaat (basis); **`+0x14` sfx-backup, `+0x18` muziek-backup,
`+0x1c` vibratie-backup (float)**. De itemwaarden zelf staan in de **statische tabel** in .data, niet in het object.

| slot | adres | functie |
|---|---|---|
| [0] | `0x4462d0` | destructor (basis) |
| [1] | `0x446640` | tekenen = basis-lijsttekenaar (**niet** `0x45bd10`, dus geen logo-fade-in) |
| [2] | `0x460460` | itemtabel → `0x4b5eb0` |
| [3] | `0x460230` | aantal → **5** |
| [4] | `0x460450` | pagina-id → **0x1b** |
| [5] | `0x446a80` | string van item i (basis: `RckGet(item.stringref)`) |
| [6] | `0x460470` | geeft **2** (basis `0x460010` geeft 3); geen aanroeper gevonden (**onzeker**, zie §10) |
| [7] | `0x446e10` | y-fractie **0.4** (`0x4aa394`) |
| [8] | `0x4462c0` | lettergrootte **30** (`0x4b39a8`) |
| [9] | `0x446aa0` | validate (basis): `flags & 1 ? item.result : 0` |
| [10] | `0x446970` | omlaag (basis, rondlopend, slaat vlag-2-items over) |
| [11] | `0x446920` | omhoog (basis) |
| [12] | `0x460310` | **rechts**: basis `0x4469d0` (+5) en daarna toepassen (§4) |
| [13] | `0x4603b0` | **links**: basis `0x446a20` (−5) en daarna toepassen (§4) |
| [14] | `0x4602a0` | **terug**: resultaat 0x18 + terugzetten (§4.3) |
| [15] | `0x460240` | **enter** (bij `0x446490(0x1b)`, `0x4464ae`): basis `0x4464c0` + backups (§4.1) |

## 2. Itemtabel `0x4b5eb0` (16 B per item: stringref, flags, resultaat, waarde)

| i | stringref | Engels | flags | resultaat | waarde (adres) | y (S = 30) |
|---|---|---|---|---|---|---|
| 0 | `0x20024` = 36 | Options | 2 (kop, niet kiesbaar) | 0 | 0 | 192.0 |
| 1 | `0x20026` = 38 | Sound FX volume | 0x10 (regelaar) | 0 | sfx 0..100 (`0x4b5ecc`) | 238.5 |
| 2 | `0x20027` = 39 | Music volume | 0x10 | 0 | muziek 0..100 (`0x4b5edc`) | 285.0 |
| 3 | `0x20084` = 132 | Vibration | 0x10 | 0 | vibratie 0..100 (`0x4b5eec`) | 331.5 |
| 4 | `0x20004` = 4 | Continue | 1 (kiesbaar) | **5** | 0 | 378.0 |

`y = 0.4·480 = 192`, cel = `62·30/40 = 46.5` (TITLE §5.1). Strings 37 "Volume", 133 "On" en 134 "Off" staan in de
Common-tabel maar worden door de exe **nergens** gerefereerd (geen dword `0x20025/0x20085/0x20086` in .data/.text als
operand; console-restant: PS2-vibratie aan/uit).

## 3. Tekenen (`0x446640`, basis; per frame vanuit `0x4464f0`)

1. Grootte S = 30; krimpt (−1, min. 15) zolang een item­**naam** (alleen `vt[5]`, zonder het `" 80%"`-achtervoegsel) ≥ 640 breed
   is (`0x446668..0x4466d8`). Met deze vijf strings blijft S = 30.
2. Per item i (y begint op `0.4·480`): kop-items (vlag 2) altijd; de andere pas als `+8 ≤ 0` (`0x44671a`). De enter zet
   `+8 = 0`, dus alles staat er meteen.
3. x: zonder uitlijnvlag gecentreerd, `x = 320 − w/2`. **Vlag 0x10** (`0x4467e0..0x44689c`) bouwt een nieuwe u16-string en
   centreert díe:
   ```c
   u16 num[..], buf[..], sp[2];
   font_itoa(font, item.value, num);            /* 0x441820: cijfers via Common-string 0, negatief -> 0 */
   strcpy16(buf, RckGet(item.stringref));       /* 0x441730(src, dst) */
   sp[0] = RckGet(0x20028)[1]; sp[1] = 0;       /* string 40 = "a a": het middelste teken is de spatie-glyph (code 18) */
   strcat16(buf, sp);                           /* 0x441770(src, dst) = dst += src */
   strcat16(buf, num);
   strcat16(buf, RckGet(0x20007));              /* string 7 = "%" */
   Measure(buf, &w, &h);  x = 0.5·640 − 0.5·w;  /* 0x44687a..0x44689c */
   ```
   Resultaat: `Sound FX volume 80%`, `Music volume 100%`, `Vibration 0%`. **Geen balk, geen pijlen, geen kleurverschil**;
   dezelfde lettergrootte en kleur (wit, `font+0x60 = 0xffffffff`) als de rest.
4. Knipperen (basis): het **geselecteerde item wordt niet getekend** zolang de knipperfase `[0x5d7b1c] < 0.25`
   (`0x4468ab`); de fase loopt 0..0.5 en valt terug naar 0 (2 Hz). Bij een regelaar knippert dus de hele regel inclusief
   getal. Na een ±5-stap zet de basis de fase op **0.25** (`0x446a08`, `0x446a57`): de regel is direct zichtbaar en verdwijnt
   pas 0.25 s later weer.
5. Daarna het logo (`0x446b00`, TITLE §5.2): omdat slot [1] de fade-in `0x446ac0` niet aanroept, **fadet het logo op deze
   pagina in 1.0 s uit** (v −= 5·dt) als je van pagina 1 komt; in een level (`app+0x68 ≠ 0`) wordt het niet getekend.
6. Geen iris (die hoort bij pagina 1, `+0x38`, TITLE §5.4).

## 4. Invoer

Via `0x4464f0` (TITLE §5.1): alleen als `+8 ≤ 0`; acties op `[0x5e6188]`, allemaal **"net ingedrukt"** (`0x467420`):
2 omhoog `vt[11]`, 3 omlaag `vt[10]`, 1 rechts `vt[12]`, 0 links `vt[13]`, 0xc bevestigen `vt[9]` (als `+0xc == 0`),
5 terug `vt[14]`, en `[0x5e6194]->vt[5](0)` ook `vt[14]`. **Geen auto-repeat**: een vastgehouden pijl geeft één stap.
Geen invoervertraging (enter zet `+8 = 0`). Geen enkel SoundFx-geluid op deze pagina (ook niet bij binnenkomst: de enter
roept `0x4464c0` aan, niet de paneel-enter `0x45b8c0` die SoundFx 0x3f speelt).

### 4.1 Enter `0x460240`
```c
base_enter();                       /* 0x4464c0: sel = 0, +8 = 0, +0xc = 0; item 0 is een kop -> vt[10]: sel = 1, fase 0 */
bak_sfx = cfg_sfx_vol;  bak_mus = cfg_music_vol;          /* +0x14, +0x18 */
item[2].value = cfg_music_vol;  item[1].value = cfg_sfx_vol;
if (g_joy) { bak_vib = g_joy->vib; item[3].value = ftol(g_joy->vib * 100); }   /* +0x1c */
```
De cursor begint dus altijd op **"Sound FX volume"**, onzichtbaar voor de eerste 0.25 s (fase 0).

### 4.2 Omhoog/omlaag
Basis: rondlopend en de kop (item 0) overslaand: omlaag vanaf Continue → Sound FX volume; omhoog vanaf Sound FX volume →
Continue. Omhoog zet de fase altijd op 0, omlaag op 0 bij een verplaatsing en 0.25 als er niets te verplaatsen viel.

### 4.3 Links/rechts `0x4603b0` / `0x460310`
```c
void right() {                      /* 0x460310; links = 0x4603b0 met base_left() */
    base_right();                   /* 0x4469d0: alleen als item[sel].flags & 0x10: value += 5, > 100 -> 100, fase = 0.25 */
    Item *it = &item[sel]; if (!(it->flags & 0x10)) return;
    switch (sel) {
    case 1: cfg_sfx_vol = it->value;                                  /* [0x4c2c50] */
            if (snd_sys()) snd_sys()->SetSfxVolume(cfg_sfx_vol);      /* [0x5e81b0]->vt[0x54] = 0x469570 */
            break;
    case 2: cfg_music_vol = it->value;                                /* [0x4c2c54] */
            if (snd_sys()) snd_sys()->SetMusicVolume(cfg_music_vol);  /* vt[0x5c] = 0x4695a0 */
            if (snd_mgr) snd_mgr->MusicUpdate();                      /* [0x5e61a4]->vt[0x44] = 0x46cbf0 -> 0x46c760 */
            break;
    case 3: if (g_joy) g_joy->vib = it->value * 0.01f; break;         /* 0x4aa0ac */
    }
}
```
Stap 5, klem 0..100 (`0x4469fd`, `0x446a4d`); geen wrap. De waarde wordt niet naar een veelvoud van 5 afgerond: een cfg-waarde
73 geeft 78, 83, … 98, 100. Bevestigen op een regelaar geeft resultaat 0 (vlag 1 ontbreekt) = niets.

### 4.4 Terug `0x4602a0` = annuleren
```c
result = 0x18;                                     /* 24 = terug */
cfg_sfx_vol = bak_sfx;  cfg_music_vol = bak_mus;
if (snd_sys()) { snd_sys()->SetSfxVolume(cfg_sfx_vol); snd_sys()->SetMusicVolume(cfg_music_vol); snd_mgr->MusicUpdate(); }
if (g_joy) g_joy->vib = bak_vib;
```
De tabelwaarden worden niet teruggezet, maar de volgende enter overschrijft ze. Alleen **Continue** bewaart dus de wijziging.
(Uitzondering: zonder joystick is er geen backup en blijft de statische vibratiewaarde gewoon staan.)

## 5. Wat de opties doen

### 5.1 Sfx en muziek
`0x469570(v)`: `[0x5e81cc] = v`, **`[0x5e81ec] = v·0.01`** (sfx-master). `0x4695a0(v)`: `[0x5e81d0] = v`, **`[0x5e81f0] =
v·0.01`** (muziekmaster). (`0x4695d0` = derde volume `[0x5e81f4]`, niet gebruikt door dit menu.) Dezelfde globals worden bij
de start van de geluidsmotor uit de cfg gezet (`0x4691e2..0x4692b9`, SOUND §2.4).

- **Sfx**: per 2D-stem `ftol(cur · [0x5e81ec])` (`0x46b791`), 3D in softwaremodus `cur · gain · [0x5e81ec]` (`0x46b6a7`),
  naar de lib `0x48f410` → `0x48f2a0`. Wordt toegepast in de per-frame-update van de manager; in het pauzemenu staan alle
  sfx-stemmen opgeschort (`0x404d8f`, `mgr+0x10 = 0`), dus een sfx-wijziging is daar pas na "Continue" te horen.
- **Muziek**: `0x46cb30`: doel = `[0x5e81f0]`, `vol = lerp(vol, doel, clamp((nu − t0)/T, 0, 1))` (`0x46cb70`) — buiten een
  fade is `t = 1`, dus meteen het doel — en `lib_SetVolume(ftol(vol·100))` (`0x48f4e0`). Het menu roept deze update zelf aan
  (`0x4602f2`, `0x460383`), omdat de manager-update in het pauzemenu stilligt; de muziekstream zelf loopt in de pauze door.
- **Curve** (`0x48bf50(min 0, max 100, v)`): `v == 0 → −10000` mB (stil), `v == 100 → 0`, anders
  **`mB = −2000·log10(100/v)`** (`0x4abec0` = −2000), daarna `× lib+0x48 / 100` (lib-master, init 100 `0x48b88b`) →
  `IDirectSoundBuffer::SetVolume` (vt 0x3c, `0x48f312`). −2000·log10(100/v) mB = 20·log10(v/100) dB: **lineaire amplitude
  v/100**. Voor sfx vermenigvuldigen stemvolume en master vóór de omzetting: amplitude = `stem/100 · sfx/100`.

### 5.2 Vibratie
`[0x5e618c]` = joystickobject (0x5c B, ctor `0x4678d0`, vtable `0x4ab90c`), alleen aanwezig als DirectInput bij
`EnumDevices(DIDEVTYPE_JOYSTICK, …, DIEDFL_ATTACHEDONLY)` een aangesloten joystick vond (`0x467926..0x467943`);
`+4` = vibratiesterkte, init **1.0** (`0x4674b0`). De enige "rumble"-aanroep `0x44d1b0` (Perso, niet bij `perso+0x21c == 2`)
gaat naar `vt[2]` = `0x467b20` = **`ret 8`**. Geen andere lezer van `+4` dan het menu en de save (§6.2). Zonder joystick toont
het item de statische beginwaarde **0%**, met joystick **100%**.

## 6. Opslag

### 6.1 Woody.cfg (setup + afsluiten)
`Woody.cfg` = `u32 0x19072001` + 0x11c B die 1:1 naar `0x4c2bd0` gaan (TRACING §2). Relevante velden (struct-offset /
bestandsoffset): `+0x68/0x6c` sfx aan `[0x4c2c38]`, `+0x6c/0x70` muziek aan `[0x4c2c3c]`, **`+0x80/0x84` sfx-volume
`[0x4c2c50]`**, **`+0x84/0x88` muziekvolume `[0x4c2c54]`**, `+0x88/0x8c` derde volume. Het spel schrijft **het hele blok** terug
bij het afsluiten (`0x401130`, na `App::Frame` = 1, GAMEFLOW §3): zo overleven de menuvolumes een herstart. Resolutie,
bpp, VSync, detail (`[0x4c2c0c]`), speakerconfig en de toetsen (`+0xac`, `+0xdc`) staan in hetzelfde blok maar **dit menu
raakt ze niet aan**; de enige schrijvers van `[0x4c2c50]/[0x4c2c54]` zijn het optiemenu en de slot-laadcode (§6.2).

Let op: de door `tools/native/mkcfg.c` gemaakte `game/Woody.cfg` heeft sfx/muziek **uit** en beide volumes **0** (alle
bytes 0x5c..0xab zijn 0); met die cfg toont de pagina `0%`. Welke standaardwaarden Detect.exe met een echte geluidskaart
schrijft is niet nagegaan (**onzeker**).

### 6.2 Woody.sav, per save-slot
Slot-manager `app+0x4c` (bestand begint op `mgr+4`, GAMEFLOW §6.1). Accessors: `0x456e40(mus, sfx, slot)` schrijft
`[mgr+0x5298+4·slot] = mus`, `[mgr+0x52a8+4·slot] = sfx`; `0x456e60(slot)` leest muziek, `0x456e70(slot)` sfx;
`0x456e80/0x456e90` lezen/schrijven de float `[mgr+0x52b8+4·slot]`. In **bestandsoffsets**:

| offset | inhoud |
|---|---|
| `0x5294` | `u32 musicvol[4]` (per slot) |
| `0x52a4` | `u32 sfxvol[4]` (per slot) |
| `0x52b4` | `float vibration[4]` (per slot, = joystick+4) |

(**GAMEFLOW §6.1 heeft sfx en muziek verwisseld**: `0x5294` is muziek, `0x52a4` sfx.)

- **Opslaan** (na een level, pagina 6 → 5): leeg slot `0x405536`: volumes + (als er een joystick is) vibratie in het slot,
  dan `0x456dc0` + schrijven; bezet slot na "overwrite?" `0x405609`: alleen de volumes (de vibratie wordt daar **niet**
  bijgewerkt). Het menu zelf schrijft nooit naar Woody.sav.
- **Laden** (pagina 2, `0x4052db`): `[0x4c2c54] = musicvol[slot]`, `[0x4c2c50] = sfxvol[slot]`, `joy+4 = vibration[slot]`,
  dan SetSfxVolume/SetMusicVolume (`0x40531b..0x405347`; geen expliciete MusicUpdate, de manager doet dat in de volgende
  frame). "Load game" zet dus de geluidsinstellingen van dat slot terug, en die gaan bij afsluiten ook in Woody.cfg.
- New game (`0x44ffa0`) en de slot-ctor (`0x456d20`) initialiseren de arrays **niet** (**onzeker** wat er in nooit
  beschreven slots staat; het hele bestand wordt in één keer gelezen/geschreven).

## 7. Verlaten, terugkeer, overlay

- Handler `0x40575c` (sprongtabel `0x405b1c[0x1b]`): resultaat **5** (Continue) of **0x18** (terug), verder niets:
  - `app+0x68 == 0` (House/titel): `0x446490(1)` → pagina 1. De paneel-enter `0x45b8c0`/`0x460070` speelt **SoundFx 0x3f**,
    zet de cursor op item 0 **"New game"** (niet terug op "Options"), en het logo fadet weer in (1.0 s).
  - in een level: `0x404d80` = pauzemenu opnieuw openen: `app+0x20 ->vt[0x88]()` (Suspend, nogmaals), pagina **0x19** als
    `perso+0x21c == 1` en geen BlackBox (`0x41fa80(200.0)` op `[0x4c737c]`), anders **0x18**; state 0. Pauze-enter
    `0x45b390`: cursor op "Continue", logo `v = 0`.
- Bereikt vanaf pagina 1 resultaat 6 (`0x4051b0` e.v.) en vanaf het pauzemenu resultaat 6 (`0x40587a`).
- **Achtergrond** (`0x404e90`, bytetabel `0x405af8[0x1b] = 5` → `0x404ee8`): wereld gerenderd via `0x401ab0`;
  in een level wereld gepauzeerd (`app+0xf4 |= 8`) en een **halfzwarte quad** `RectVirtual(0,0,640,480, 0x80000000 ×4)`
  (`0x404f1a..0x404f53`); in House geen overlay en geen pauze (de titelcamera draait door). HUD: `0x448450(2)` = verborgen
  (alleen 0x18/0x19 krijgen toestand 1, de uitgebreide HUD), dus vanuit de pauze verdwijnt de HUD op de optiepagina.
- Esc (actie 9) doet op deze pagina niets.

## 8. Subpagina's

Geen. Geen enkel item heeft een ander resultaat dan 5, en de handler kent alleen 5/0x18. Toetsen, controller-modus,
resolutie en detail worden op pc alleen in Detect.exe (`Setup.dll`, `DefaultControlSettings`/`SaveConfig`) ingesteld;
taal is per installatie (HUD_TEXT §1.5); de taal-/memorycardpagina's 0x16/0x21 zijn console-restanten.

## 9. Recept voor de port

### 9.1 Toestand
```c
/* src/main_engine.c */
typedef struct { uint32_t magic; int32_t sfx, music, vib; /* port-only, na het origineel: */ int32_t aspect, res, fullscreen, vsync; } Options;
static Options g_opt = { OPT_MAGIC, 100, 70, 100, 0, 0, 0, 1 };   /* defaults: sfx 1.0 / muziek 0.7 = huidige audio.c-waarden */
static int32_t g_opt_bak[3];                                        /* +0x14 / +0x18 / +0x1c */
```
Bewaar dit in een **eigen `woodyre.cfg`** (analoog aan Woody.cfg: lezen bij boot vóór `audio_init`, schrijven bij het
afsluiten én bij Continue), **niet** in `woodyre.sav`: die heeft een vaste binaire struct met magic `WSV2` die bij elke
formaatwijziging wordt weggegooid (`save_read`), en nieuwe port-opties zouden dan de voortgang ongeldig maken. Een tekstformaat
`key=value` (onbekende sleutels negeren, ontbrekende = default) laat later aspect/resolutie toevoegen zonder versiebump.
De per-slot-kopie van het origineel (§6.2) is optioneel: de port heeft één save; wie het wil nabootsen zet
`sfx/music/vib` in `g_save` en past ze toe in het "Load game"-pad.

Bij boot: `audio_master(g_opt.sfx * 0.01f, g_opt.music * 0.01f)` — **lineair is exact** wat het origineel doet (§5.1: mB =
20·log10(v/100)·100). `audio.c` vermenigvuldigt nu al stem·`A.m_sfx` en stream·`A.m_mus`; `audio_master` wordt echter nog
nergens aangeroepen. Vibratie: waarde bewaren en tonen; alleen doorgeven als er ooit rumble komt (origineel: no-op).

### 9.2 Pagina (hud.c)
`page_items` kent nu geen vlaggen. Voeg een variant met itemvlaggen toe, met de bestaande `row_*`-hulpjes (die al de
string-40-spatietruc gebruiken):
```c
typedef struct { uint32_t id; uint32_t flags; int value; } MenuItem;          /* flags: 1 kiesbaar, 2 kop, 0x10 regelaar */
static void page_items_ex(const MenuItem *it, int n, float yfrac, int sel)   /* 0x446640 */
{
    float S = 30.0f;
    for (int i = 0; i < n; i++) { const uint16_t *s = hud_string(it[i].id); if (!s) continue;
        while (S > 15.0f) { font_size(S); if (font_measure(s) < 640.0f) break; S -= 1.0f; } }   /* alleen de naam */
    font_size(S);
    float y = yfrac * 480.0f, cell = font_cell();
    for (int i = 0; i < n; i++, y += cell) {
        row_reset(); row_str(it[i].id);
        if (it[i].flags & 0x10) { row_space(); row_num(it[i].value); row_str(7); }          /* "naam 80%" */
        if (i == sel && H.menu_t < 0.25f) continue;
        font_draw(320 - font_measure(g_row) * 0.5f, y, g_row, 0xff808080);
    }
    font_size(17.0f);
}
void hud_menu_page_ex(const MenuItem *it, int n, float yfrac, int sel, float dt);  /* tikt H.menu_t zoals hud_menu_page */
void hud_menu_blink(float phase);                                                   /* 0 of 0.25 na een stap (§3.4, §4.2) */
```
(`row_reset`/`row_str`/`row_space`/`row_num` staan nu ná `page_items` in hud.c, bij het resultatenscherm; naar boven
verplaatsen.) Logo: `hud_title_draw` zet nu
`logo_v = 0` zodra het logo niet gewenst is; voor pagina 0x1b moet het **uitfaden**: `logo_v -= 5·dt` (min 0) en blijven
tekenen zolang `logo_v > 0` (0x446b00). Items tekenen vóór het logo.

### 9.3 Logica (main_engine.c, titelblok bij `title_page`)
```c
static const MenuItem k_opt_base[5] = { {36,2,0}, {38,0x10,0}, {39,0x10,0}, {132,0x10,0}, {4,1,0} };
enter (pagina 1, item 2 "Options"):  title_page = 0x1b; opt_sel = 1; hud_menu_blink(0);
                                     g_opt_bak = {sfx, music, vib};              /* geen audio_fx */
per frame (alleen "net ingedrukt", geen herhaling):
  up/down: rondlopend over 1..4 (item 0 overslaan); blink = 0 (down zonder beweging: 0.25)
  left/right op 1..3: v = clamp(v -/+ 5, 0, 100); blink = 0.25; meteen toepassen:
       audio_master(g_opt.sfx*0.01f, g_opt.music*0.01f)
  confirm op 4 (Continue): opt_save(); terug
  back (Esc/Backspace; origineel actie 5):  g_opt = bak; audio_master(...); terug
terug: g_level == 0 ? (title_page = 1, title_sel = 0, audio_fx(63)) : pauzemenu (0x18/0x19, sel 0)
tekenen: g_level == 0 ? geen overlay : halfzwart vlak 0x80000000 + HUD verbergen; hud_menu_page_ex(items, 5, 0.4f, opt_sel, dt)
```
De port heeft nog geen pauzemenu (alleen de P-toets); zodra 0x18/0x19 er zijn, leidt hun "Options" (resultaat 6) hierheen.
Let op de toetsen: in de port is Spatie nu ook "bevestigen"; in het origineel is actie 5 (terug) standaard Spatie en
bevestigen de sprongtoets. Kies voor de hele port één vaste set (voorstel: Enter/Spatie = bevestigen, Esc/Backspace = terug).

### 9.4 Plek voor de nieuwe opties (issue #12: beeldverhouding, 4K)
Het font heeft alle benodigde glyphs voor bv. `Display`, `Aspect ratio 16:9`, `Resolution 3840x2160`, `Fullscreen`
(`x` = code 75, `:` = 30; ontbrekend zijn alleen `j Z - & "`), en strings 133 "On" / 134 "Off" / 16 "BACK" bestaan al.
Port-eigen teksten moeten via de ASCII→code-tabel (HUD_TEXT §1.4) naar u16 worden omgezet.

Voorstel dat de originele indeling intact laat: **de vijf items blijven op y 192..378**; voeg **één item na Continue** toe
(i = 5, y = 424.5, onderkant cel 471 < 480), bv. `Display` (vlag 1, port-resultaat) dat naar een **port-eigen pagina**
(id ≥ 0x40, zelfde `page_items_ex`, y-fractie 0.4) gaat met: beeldverhouding (4:3 / 16:9 / venster volgen), resolutie
(lijst van de monitormodi), volledig scherm, VSync, en "Continue"/"BACK". Een keuze-item krijgt een nieuwe vlag (bv. 0x100:
`naam + spatie + keuzetekst`, links/rechts wisselt) naast 0x10. Een compat-schakelaar kan het extra item verbergen voor een
pixel-exacte originele pagina. Resolutie/aspect horen in `woodyre.cfg` (zoals Woody.cfg ze voor het origineel bevat), niet
in de save. Terugzetten bij "terug" geldt ook voor deze pagina (backup bij enter), maar een modewissel pas toepassen bij
Continue.

## 10. Onzeker

1. Slot [6] (`0x460470` → 2, basis `0x460010` → 3): geen aanroeper van `vt+0x18` op een menupagina gevonden in
   `0x401000..0x406200` en `0x445e00..0x463000`; betekenis onbekend (misschien dood).
2. Standaard sfx-/muziekvolume in Woody.cfg zoals Detect.exe het met een echte geluidskaart schrijft (de mkcfg-cfg heeft 0/0 en
   geluid uit). De port-defaults 100/70 zijn een eigen keuze.
3. Of een al spelende 2D-stem (zonder fade) zijn nieuwe sfx-volume elk frame krijgt of pas bij de volgende `0x46b780`-aanroep;
   de exacte aanroeper-voorwaarden van `0x46b6xx/0x46b78x` in `0x46bbb0` zijn niet gevolgd.
4. De "terug"-toets via `[0x5e6194]->vt[5](0)` (TITLE §10 punt 4) en de standaardtoets van actie 5.
5. Inhoud van de volume-arrays in Woody.sav voor nooit beschreven slots (niet geïnitialiseerd, §6.2).
6. Of de vibratiewaarde op een andere manier (bv. via Setup.dll/force feedback buiten de exe) nog iets doet; in de exe is de
   rumble-methode leeg.
7. Alles is statisch; een trace (breakpoints op `0x460310`, `0x4602a0`, `0x469570`, `0x4695a0`) zou de stappen en de
   revert bevestigen.
