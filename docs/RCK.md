# RKET resource banks (`Common\*.rck`, `Data\<LVL>\<LVL>.rck`)

Afgeleid uit de loader `0x441230` in Woody.exe en geverifieerd met [tools/rckparse.py](../tools/rckparse.py):
alle 31 banken (3 in `Common`, 28 in `Data`) parseren tot exact het einde van het bestand.

## Container

| offset | type | betekenis |
|---|---|---|
| 0x00 | char[4] | `RKET` (anders: "Fichier de ressources invalide") |
| 0x04 | u32 | versie, moet 0 zijn ("trop vieille" / "trop récente") |
| 0x08 | u8[0x30] | bankheader (zie onder) |
| 0x38 | items | achter elkaar, per resourcetype in volgorde 0..3 |

Bankheader (offsets binnen de 0x30 bytes):

| offset | type | betekenis |
|---|---|---|
| +0x00 | u32 | build-stempel (`0x3bce1226` ≈ 21-10-2001) |
| +0x05 | u8 | banknummer: 0 = `Common` (karakterbank), ≠0 → slot 1 = levelbank. Waarden > 16 geven een fout |
| +0x18 | u32[4] | aantal items per type: 0 = geluid, 1 = afbeelding, 2 = string, 3 = font |

Item: `u32 size`, `u32 reserved` (in de bestanden staat hier een oud geheugenadres of een glyph-paar; de engine gebruikt het niet), gevolgd door `size` bytes payload. Een type zonder geregistreerde lader wordt met de standaardfunctie `0x4415e0` overgeslagen (leest de 8-byte kop, slaat `size` bytes over).

## Registratie en opzoeken

- `RckRegister(bank, type, load, loadArg, free, freeArg, begin, beginArg, end, endArg)` = `0x441670`; tabel `0x5bc830` + bank·0xb0, per type 0x2c bytes: `-4` aantal, `+0` itemarray, `+4` load, `+0xc` free, `+0x14` begin, `+0x1c` end.
- Registraties (in `0x4045c0..0x404760`): type 0 → geluidsmanager (`vtable+0x70` load, `+0x74` free; alleen als er een geluidsmanager is), type 1 → afbeelding `0x47f910` (bank 0) / `0x47f9a0` (bank 1), type 2 → string `0x43f570` (stringpool), type 3 → font `0x43fc90` (100-byte fontrecord via `0x43f9a0`).
- `RckGet(ref)` = `0x441580`: `ref = bank<<24 | type<<16 | index` (u16 index, u8 type, u8 bank). Dit is de encoding van de resource-verwijzingen in scripts/berichten: `0x0002xxxx` = string xxxx uit bank 0, `0x0102xxxx` = string uit de levelbank, enz.

## Payloads

| type | layout |
|---|---|
| 0 geluid | `u32 nbytes, u32 rate (22050/44100), u32 bits (16), u32 channels (1), PCM` |
| 1 afbeelding | `i16 w, i16 h, u16 bpp (24/32), u16 alpha (1 = surface met kleursleutel/alpha), u8[w·h·4] BGRA` — 2D sprites/menu's (16..256 px) |
| 2 string | `u16[]` glyph-indices (0..75) in de bitmapfont; geen tekst-encoding, de font bepaalt het teken |
| 3 font | 787 960 bytes: bitmapfont (Common heeft geen font, elke levelbank precies één) |

Inhoud per bank: `Common\Woody.rck` (en Knothead/Splinter: dezelfde structuur per speelbaar karakter) = 119 geluiden, 70 afbeeldingen, 135 strings; levelbanken = 0..32 geluiden, 0..72 afbeeldingen, 0..252 strings, 1 font.

**Er zitten geen 3D-meshes of animaties in de .rck-banken.** De karaktermodellen en hun animaties moeten dus in de levelbestanden (`.gel`/`.ins`) staan; de live trace (tools/wtrace.py) bevestigt dat een level alleen `code, .gel, .tex, .ins, .col, .vis, .lit, Common\<karakter>.rck, <LVL>.rck` opent.
