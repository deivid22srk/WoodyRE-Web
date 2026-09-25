# RKET resource banks (`Common\*.rck`, `Data\<LVL>\<LVL>.rck`)

Derived from the loader `0x441230` in Woody.exe and verified with [tools/rckparse.py](../tools/rckparse.py):
all 31 banks (3 in `Common`, 28 in `Data`) parse to exactly the end of the file.

## Container

| offset | type | meaning |
|---|---|---|
| 0x00 | char[4] | `RKET` (otherwise: "Fichier de ressources invalide") |
| 0x04 | u32 | version, must be 0 ("trop vieille" / "trop récente") |
| 0x08 | u8[0x30] | bank header (see below) |
| 0x38 | items | consecutive, per resource type in order 0..3 |

Bank header (offsets within the 0x30 bytes):

| offset | type | meaning |
|---|---|---|
| +0x00 | u32 | build stamp (`0x3bce1226` ≈ 21-10-2001) |
| +0x05 | u8 | bank number: 0 = `Common` (character bank), ≠0 → slot 1 = level bank. Values > 16 raise an error |
| +0x18 | u32[4] | item count per type: 0 = sound, 1 = image, 2 = string, 3 = font |

Item: `u32 size`, `u32 reserved` (in the files this holds an old memory address or a glyph pair; the engine doesn't use it), followed by `size` bytes of payload. A type without a registered loader is skipped with the default function `0x4415e0` (reads the 8-byte header, skips `size` bytes).

## Registration and lookup

- `RckRegister(bank, type, load, loadArg, free, freeArg, begin, beginArg, end, endArg)` = `0x441670`; table `0x5bc830` + bank·0xb0, per type 0x2c bytes: `-4` count, `+0` item array, `+4` load, `+0xc` free, `+0x14` begin, `+0x1c` end.
- Registrations (in `0x4045c0..0x404760`): type 0 → sound manager (`vtable+0x70` load, `+0x74` free; only if a sound manager exists), type 1 → image `0x47f910` (bank 0) / `0x47f9a0` (bank 1), type 2 → string `0x43f570` (string pool), type 3 → font `0x43fc90` (100-byte font record via `0x43f9a0`).
- `RckGet(ref)` = `0x441580`: `ref = bank<<24 | type<<16 | index` (u16 index, u8 type, u8 bank). This is the encoding of resource references in scripts/messages: `0x0002xxxx` = string xxxx from bank 0, `0x0102xxxx` = string from the level bank, etc.

## Payloads

| type | layout |
|---|---|
| 0 sound | `u32 nbytes, u32 rate (22050/44100), u32 bits (16), u32 channels (1), PCM` |
| 1 image | `i16 w, i16 h, u16 bpp (24/32), u16 alpha (1 = surface with color key/alpha), u8[w·h·4] BGRA` — 2D sprites/menus (16..256 px) |
| 2 string | `u16[]` glyph indices (0..75) in the bitmap font; no text encoding, the font determines the character |
| 3 font | 787,960 bytes: bitmap font (Common has no font, each level bank exactly one) |

Contents per bank: `Common\Woody.rck` (and Knothead/Splinter: same structure per playable character) = 119 sounds, 70 images, 135 strings; level banks = 0..32 sounds, 0..72 images, 0..252 strings, 1 font.

**There are no 3D meshes or animations in the .rck banks.** The character models and their animations must therefore be in the level files (`.gel`/`.ins`); the live trace (tools/wtrace.py) confirms that a level only opens `code, .gel, .tex, .ins, .col, .vis, .lit, Common\<character>.rck, <LVL>.rck`.
