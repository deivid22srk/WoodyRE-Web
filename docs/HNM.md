# HNM films (the startup logos)

The game plays three films, all at boot: `\Logo\Cryo.hnm` (the DreamCatcher logo), `\Logo\Eko.hnm` ("A game by" + Eko
System) and `\Logo\Universal.hnm`. There are no other films: the exe's only film paths are the three entries of table
`0x4b3960`, the player singleton `0x426b00` has no other user than the logo player, and the intro, the world intros and
the ending are in-engine cinematics (docs/CINEMATIC.md). The format is Cryo's **HNM6** (640x480, 16-bit RGB565 output,
DCT key blocks + motion compensation) with **CRYO_APC** 4-bit ADPCM sound at 44.1 kHz.

Port: `src/hnm.c` / `src/hnm.h` (decoder, plain C99), `logos_play` in `src/main_engine.c` (player loop),
`rnd_film_frame` in `src/render_gl.c` (display), `audio_pcm_*` in `src/audio.c` (sound), `src/hnmtest.c` (dump tool).

## 1. Playback in Woody.exe

### 1.1 Flow
- Boot (end of `0x4023a0`, docs/GAMEFLOW.md 3): `app+0x88` = logo player (`0x445cd0`: `+0` = the HNM player singleton
  `0x426b00`, `+4` = file stream, `+8` = logo index). Without the dev flag `[cfg+0x384] & 4`: `app+0x98 = 35.0`, state 2
  (`0x401400(2)`), `0x445d00(0)` (`0x402649`). With the dev flag: level picker, no logos.
- `0x445d00(i)`: `+8 = i`; a new stream object (`0x43fcc0`, 8 bytes) opens `game dir + table[i]` (`0x44ff00` concatenates
  `[0x5e5814]+4` and the entry; the stream's vt[0] gets `(path, 5, 1)`); player vt[1] (Open) then vt[2] (Play).
- State 2 = `0x401500`, every frame: action 9 **held** (`0x467400(9)`; Esc / joystick button 5 in the shipped
  Woody.cfg, docs/INPUT.md) → `0x445da0` (player vt[4] Stop, stream vt[4] + delete). Then `0x445de0` = player vt[5]
  (IsPlaying); when it is over: `0x445da0`, and index 0 → `0x445d00(1)`, 1 → `0x445d00(2)`, 2 → `LoadLevel(0)` + title
  (`0x404e30`) (the `vt[1]() == 2` console branch is dead, docs/GAMEFLOW.md 3). The next film starts in the frame the
  previous one was stopped, so holding Esc for a few frames skips all three; Enter does not skip.
  *Port extra:* each press of Esc, Enter, Space or the jump / attack key ends the running film and the next one starts
  (one press = one film; Esc is counted per press as well, so holding it no longer runs through all three), and `logos=0`
  in woodyre.cfg leaves them out altogether. Likewise Enter / jump also skip the New-game intro and the title's attract
  cinematic (page 0x1f), which the original only lets the attack key or Esc break off.
- Order: **Cryo, Eko, Universal** (`0x4b3960` = `0x4b3990` `\Logo\Cryo.hnm`, `0x4b3980` `\Logo\Eko.hnm`, `0x4b396c`
  `\Logo\Universal.hnm`). Durations: 25.5 s, 9.7 s, 20.7 s.

### 1.2 The player object (vtable `0x4aa1e8`, 0xbc bytes, ctor `0x425fa0`)
| vt | address | |
|---|---|---|
| 0 | `0x426070` | destructor |
| 1 | `0x426340` → `0x426760` | Open(stream): creates the Cryo player core (`0x4929a0`, 0x488 bytes, `+0x7c`), a DirectDraw off-screen surface 16 bit 565 (masks `0xf800/0x7e0/0x1f`) for the frames, opens the file (`0x493560`, returns width/height), starts it (`0x493640`) |
| 2 | `0x4260b0` | Play (first call: `0x493690` with the window; paused: resume) |
| 3 | `0x426110` | pause toggle (`0x493ea0` / `0x493e30`) |
| 4 | `0x426160` | Stop (`0x493f10`, `0x493db0`, delete the core, release the surfaces) |
| 5 | `0x426390` | IsPlaying; honours the end flag `+0xb8` |

The core runs its own thread and window class `_CMWC_`; it posts message `0x8000` to the window procedure `0x4263d0`:
wParam 2 = a frame is ready → Blt of the frame surface to the back buffer's screen rect (display object `[0x5e8650]`
`+0x364`), stretched, then Flip; in a 32-bit display mode (`[0x5093d4]`) the 565 pixels go through the table
`0x4c93d0` first (built in `0x425fa0`: `r5 << 3 | 7`, `g6 << 2 | 3`, `b5 << 3 | 7`). wParam 3 = the film ended →
`0x426250` sets `+0xb8`, and the next IsPlaying stops it.

### 1.3 Inside the core
- Chunk dispatch `0x492d29` (type = the two letters read as a LE word): `AA` / `BB` → sound `0x492db0` (the first block:
  "SendFirstSoundBlock" `0x4941f0` → ADPCM library 1.50 check `0x4a5580(0xb4)`, header `0x4a55a0`: magic `CRYO_APC`,
  version `"1.20"` formatted with `%1d.%02d`); `IX` / `IV` → video (`0x496a60`, `0x4969e0`). Other types are ignored.
- Decoder DLL (`0x4971e4..0x4973c5`): the header width must be 512, 640, 800 or 1024 (else error -14); `LoadLibrary`
  `CM6_<width>x16.DLL`, fallback `CM6_<width>.DLL`; `GetProcAddress` of `HNMPI_Init`, `HNMPI_DecodeFrame`,
  `HNMPI_Cleanup`. The game ships the four 16-bit DLLs; the logos use `CM6_640x16.dll`.
- Frame rate: without sound 15 fps (`0x4935e3`: 15.0f, or a header word when non-zero); with sound the film follows the
  sound (every superchunk after the first carries exactly one frame of it).

## 2. File format
All little endian.

| offset | size | Cryo / Eko / Universal | meaning |
|---|---|---|---|
| 0x00 | 4 | `HNM6` | magic |
| 0x04 | 2 | 0 | ? |
| 0x06 | 1 | 0x40 / 0xc0 / 0xc0 | sound flags: 0x80 = stereo; `(b >> 4 & 6) * 11025` = 44100 (the reading of ScummVM's HNM decoder, consistent with the files; the port takes the sound format from the APC header) |
| 0x07 | 1 | 0x10 | bits per pixel |
| 0x08 | 2+2 | 640, 480 | width, height |
| 0x0c | 4 | | file size |
| 0x10 | 4 | 383 / 145 / 517 | frames |
| 0x14 | 4 | `V108` / `V109` / `V109` | encoder version |
| 0x18 | 2+2 | 13,2 / 13,2 / 8,2 | ? |
| 0x1c | 4 | 614400 | bytes of one decoded frame (640 x 480 x 2) |
| 0x20 | 32 | `Pascal URRO  R&D-Copyright CRYO-` | |

From 0x40: one **superchunk** per frame: u32 (low 24 bits = size including these 4 bytes), then chunks until the end of
the superchunk. The file ends with a superchunk word of 0. A **chunk**: u32 size (header included, padding excluded),
2 type letters, u16 flags; the next chunk starts at the size rounded up to 4. The high byte of the flags is the number of
padding bytes inside the size (Cryo's 1470-byte sound blocks: size 1480, flags 0x0200; the others are multiples of 4, flags 0).

| type | |
|---|---|
| `AA` | sound. The first one (frame 0) starts with the 32-byte APC header and holds the sound of 32 frames (pre-buffer); frames 1..n-33 carry one frame each; the last 32 frames none |
| `IX` | one video frame (section 3) |

Sound per frame: Cryo 1470 bytes mono (2940 samples = 1/15 s), Eko 2940 bytes stereo (1/15 s), Universal 1764 bytes
stereo (1/25 s): **15, 15 and 25 fps**.

## 3. The IX frame (CM6_640x16.dll)
`HNMPI_Init(?, bpp, height, width, "HNM6")` (`0x10001000`): width must be 640, height even, bpp 15 (`0x10007717`) or
16 (`0x10007700`); builds the tables. `HNMPI_DecodeFrame(data, prev, cur)` (`0x10001060` → `0x10007730`).

Header, 6 dwords: `quality`, then the offsets (from the chunk payload start) of the four streams and the end:
| stream | from | reader |
|---|---|---|
| block tree bits | +4 | LE dwords, most significant bit first (`add ebp, ebp`), 32 per dword |
| motion words | +8 | u16 each |
| short-motion words | +0xc | 12 bit each, two per 3 bytes (low 12 bits first, `0x10001232`) |
| coefficient codes | +0x10 | 4 bit each, low nibble first (`0x10006450` unpacks them into a byte buffer) |
| end | +0x14 | |

`quality < 0` = key frame (tree `0x100022ac`, only the frame being built is referenced), else an inter frame (tree
`0x1000119c`, the previous frame too). |quality| clamped to 0..100 (`0x10006318`); `qf = q < 50 ? 5000 / q : 200 - 2q`;
per coefficient `clamp((T[n] * qf + 50) / 100, 8, 255) * AAN[n] >> 13` with the JPEG luma / chroma tables `0x1001aa0c` /
`0x1001ab0c` and the AAN factors `0x1001a90c`, stored in zigzag order (`0x1001a70c`).

### 3.1 Block trees
The frame is walked in 8x8 blocks, rows top to bottom. Bits (first bit left):

Key frame: 8x8 `111` motion, `110` key block, `10` four 4x4, `01` two 4x8 (left, right), `00` two 8x4 (top, bottom).
4x8 / 8x4: `1` motion, `0` two 4x4. 4x4: `11` four 2x2, `10` two 2x4, `01` two 4x2, `00` motion. 2x4 / 4x2: `1` two
2x2, `0` small motion. 2x2: always small motion.

Inter frame: 8x8 `1111` never coded (the DLL runs into `pushal; ret`, `0x100016c3`), `1110` four 4x4, `110` skip,
`101` two 4x8, `100` two 8x4, `011` key block, `010` motion, `00` short motion. 4x8 / 8x4: `11` skip, `10` two 4x4,
`01` motion, `00` short motion. 4x4: `111` four 2x2, `110` two 2x4, `101` two 4x2, `100` skip, `01` motion, `00` short
motion. 2x4 / 4x2: `111` two 2x2, `110` skip, `10` small motion, `0` short motion. 2x2: `11` skip, `10` small motion,
`0` short motion.

### 3.2 Copies
All sources are addressed linearly (pitch = 640 pixels), so an x past the line end lands on the next line; (bx, by) =
the corner of the enclosing 8x8 block, (x, y) = the sub-block.
- **skip**: the same place in the previous frame.
- **motion** (word m): x = `bx + 128 - (m >> 7 & 0xff)` wrapped once into 0..639 (table `0x100181b0`), y = `by + offy - (m & 0x7f)`.
  Key frame: from the frame being built, offy 0 for 8x8 else 4, transform = 2 tree bits + bit 15 of m. Inter frame:
  3 tree bits of transform; bit 15 set = from the frame being built (offy as before, `0x100023f9`), clear = from the
  previous frame with offy 64.
- **small motion**: transform = `m >> 12 & 7`; from the frame being built (key frames, or bit 15 set): x = `bx + 63 - (m & 0x7f)`,
  y = `by + 6 - (m >> 7 & 0x1f)`; else from the previous frame: x = `bx + 31 - (m & 0x3f)`, y = `by + 31 - (m >> 6 & 0x3f)`. No wrap.
- **short motion** (12-bit k): previous frame at (x, y) + spiral[k] (`0x100130d0`, built in `0x10001090`): square rings
  around the block from the outside in, 4096 entries; index 0 = (+1, 0), 1 = (0, 0), 2 = (0, +1), 3 = (+1, +1), ...
- Transforms (per-size function tables from `0x100170d0`; checked for 8x8): dst(x, y) = src(X, Y) with 0 (x, y),
  1 (w-1-x, y), 2 (x, h-1-y), 3 (w-1-x, h-1-y), 4 (h-1-y, w-1-x), 5 (y, w-1-x), 6 (h-1-y, x), 7 (y, x). The straight
  copy moves 4 pixels (2 dwords) at a time, row by row.

### 3.3 Key blocks (`0x10007018`)
Three 8x8 planes Y, U, V (`0x100191e4`, +0x100, +0x200), luma quantisation for Y, chroma for U and V. Each plane:
a DC = signed byte from two nibbles (high first), then codes until 64 coefficients (jump table `0x1001ac0c`):
| code | |
|---|---|
| 0 | rest zero |
| 1 | skip 5 |
| 2..7 | skip 1 / 1 / 2 / 2 / 3 / 3, then +1 / -1 / +1 / -1 / +1 / -1 |
| 8 | nibble b: skip `(b >> 2 & 3) + 1`, then `(b & 3) + 2` values from pairs: each nibble gives `S2a[n]`, `S2b[n]` (`0x1001ac6c` / `0x1001acac`: +-1, +-2) |
| 9 | nibble b: skip `(b >> 2 & 3) + 1`, then `(b & 3) + 1` values `S4[n]` (`0x1001acec`: 1..8, -8..-1) |
| 10 | one pair |
| 11, 12, 13 | 1, 2, 3 values S4 |
| 14 | skip 1 |
| 15 | a signed byte (two nibbles) |

The three planes lie in one array of 3 x 64 (cleared once per key block) and the position runs over all of it: a
handler writes, advances, and then tests for a multiple of 64 (`test esi, 0xff`); code 0 ends the plane, the skips
(1, 14) never test. So a skip onto 64 does not end a plane, and a write run past 64 continues into the next plane's
coefficients; the next plane starts at its own base regardless (`[0x100191cc] + 0x100`). The logos never do either.

IDCT (`0x1000705e`): the AAN butterfly with integer shifts, only over the rows / columns that received a coefficient
(mask `0x10019e0c`); DC only (mask 0x0101) = fill. The pass order is chosen by a **signed** byte compare of the row
mask against the column mask (`0x10007079`: `cmp bl, al; jl`), so a block with a coefficient in row 7 (0x80 < 0) always
goes rows first; with a single row / column the second pass is a copy.

Colour (`0x10006d98`), per pixel, coefficients scaled by 16:
```
cr = ((V >> 4) * 16 + 8) / 10          // table 0x1001ad2c, C division
cb = (U >> 4) / 3                      // table 0x1001b92c
y  = (Y >> 4) + 128
R = Rt[(y + cr) >> 2]   G = Gt[(y - (cr >> 1) - cb) >> 2]   B = Bt[(y + (U >> 3)) >> 2]
```
`Rt[i] = min(i >> 1, 31) << 11`, `Gt[i] = min(i, 62) << 5` (G never reaches 63), `Bt[i] = min(i >> 1, 31)`, for i in
0..127; the tables sit one after the other with 64 zero entries in front of each (`0x1001c52c`), so negative indices
give 0 and an index of 128..191 reads the next table's zeros (black for that channel; does not occur in the logos).

## 4. CRYO_APC sound
Header (32 bytes, at the start of the first `AA` payload): `CRYO_APC` + `1.20`, u32 samples per channel, u32 rate,
s32 left and right start values, u32 flags (bit 0 = stereo). Then 4-bit IMA ADPCM, high nibble first; stereo: high =
left, low = right. Decoder `0x4a56d0` / `0x4a5870`: `diff = step >> 3 (+ step if b&4) (+ step >> 1 if b&2) (+ step >> 2 if b&1)`,
negated if b&8; the predictor is a 32-bit register that is neither clamped nor truncated, the output is its low word;
index += `{-1,-1,-1,-1,2,4,6,8}[b & 7]` (`0x4c2bb0`) clamped to 0..88; steps `0x4c2a40` (the IMA table, 7..32767);
start index 0 (step 7, `0x4c261c`). The last sound blocks run past the header's sample count; the rest is not played.

## 5. The port
- `hnm_open` / `hnm_next` read a superchunk at a time and decode the frame into one of two RGB565 buffers (each with 144
  lines of margin above and below: motion may point outside the picture; the DLL just reads whatever memory lies there).
  The pitch is the frame width, so the decoder is not tied to 640.
- `logos_play` (`main_engine.c`) runs before the House level is loaded: three films, 4:3 kept with black bars (the
  original stretches to the screen, which is 4:3 there), frames paced by the sound clock (samples per superchunk), sound
  through `audio_pcm_open/push/close` at full gain. The original's film buffer gets no SetVolume/SetPan, so neither the
  Sound Fx nor the Music volume applies; its only switch is Detect's **"Cinematic"** box (Woody.cfg `+0x70` → `[0x5e81bc]`,
  `0x426a57`: without it the player gets no DirectSound object and the film is mute), ported as woodyre.cfg `film_sound=`
  (SETUP.md 3.3). Action 9 held (Esc) skips a film, as in `0x401500`.
- They play only when booting to the title: no level argument, no `--shot` / `--enter` / `WOODY_KEYS` /
  `WOODY_SHOTSEQ`, no `--nologo` or `WOODY_NOLOGO`; `--logo` forces them. Test hooks: `WOODY_LOGOSHOT="file.ppm T"`,
  `WOODY_LOGOESC="T1 T2 .."` (Esc at those seconds since the first film started).
- `out/hnmtest.exe file.hnm [prefix [frame ...]]` decodes a film without a window: prints format, fps and decode speed,
  writes the listed frames as PPM and the sound as WAV.
- Not ported: the `IV` chunk type (accepted by `0x492d29`, absent from the files), the 15-bit and the 512/800/1024-wide
  DLL variants (same code, different constants), pause.
