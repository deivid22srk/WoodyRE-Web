# BONUS.md — bonus/pickup/trigger instance classes (types 30, 34, 35, 36, 37, 38, 40)

All addresses are VAs in `game/Woody.exe`
(imagebase 0x400000). Base class instance: ctor `0x42e1a0`, vtable `0x4aa31c` (28 slots,
`+0x00..+0x6c`), message handler `0x42d5e0`.

## 0. Correction to docs/classmap_raw.txt

The factory `0x403502` (switch: byte table `0x403f3c`, jump table `0x403e94`, key = type-1)
overwrites the vtable for two types AFTER the shared ctor `0x44f300`; classmap_raw.txt misses that:

| type | case | alloc | ctor | actual vtable |
|---|---|---|---|---|
| 30 | `0x4039b6` | 0x108 | `0x44f3e0` | `0x4aad64` |
| 34 | `0x4039ed` | 0x108 | `0x44f500` | `0x4aadd8` |
| 35 | `0x403a24` | 0x108 | `0x44f300` + `mov [esi],0x4a9444` (`0x403a4e`) | **`0x4a9444`** (not `0x4aacf0`) |
| 36 | `0x403a59` | 0x10c | `0x44f670` | `0x4aae50` |
| 37 | `0x403a90` | 0x108 | `0x44f7d0` | `0x4aaec4` |
| 38 | `0x403ac7` | 0x108 | `0x44f300` + `mov [esi],0x4a93d0` (`0x403af1`) | **`0x4a93d0`** (handler `0x44f9b0`, not `0x44f350`) |
| 40 | `0x403afc` | 0x134 | `0x44d250` | `0x4aac74` |

So `0x4aacf0` is only the (abstract) bonus base class "CBonus"; no type uses it directly.

## 1. Class hierarchy and vtables

```
Instance (ctor 0x42e1a0, vtable 0x4aa31c, 28 slots, 0x104 B)
 └─ "FadeInst" (no own ctor; methods 0x44e7c0 Init, 0x44e7f0 SetFade, 0x44e810 Update, 0x44e8f0 handler)
     │   +0xfc f32 fade target, +0x100 f32 fade speed (units/s), uses inst+0x6c as alpha
     ├─ CBonus (ctor 0x44f300, vtable 0x4aacf0, 29 slots, 0x108 B; +0x104 u32 flags = 0)
     │    ├─ type 30  vtable 0x4aad64   kind 1  EXTRA LIFE
     │    ├─ type 35  vtable 0x4a9444   kind 3  (counter Perso+0x254; see §2.3)
     │    ├─ type 34  vtable 0x4aadd8   kind 4  REGULAR BONUS (25 = heart), 30 slots (+0x74 Respawn)
     │    ├─ type 36  vtable 0x4aae50   kind 2  UNIQUE/PERSISTENT item (savegame flag), 0x10c B
     │    ├─ type 37  vtable 0x4aaec4   kind 5  RACE BONUS (list 0x5e48d0, max 256), 30 slots (+0x74 Respawn)
     │    └─ type 38  vtable 0x4a93d0   kind 6  INVINCIBILITY (duration = message argument), handler 0x44f9b0
     ├─ type 40 BOMB (ctor 0x44d250, vtable 0x4aac74, 0x134 B, handler 0x451820) – NOT a pickup, see §7
     └─ types 120/121 "Exploding object (Chest)" (ctor 0x451650, vtable 0x4aafd0, handler 0x451820) – see §7
```

`kind` = `(inst+0x104 >> 5) & 0x1f`; the low bits 0x08 (all bonuses) resp. 0x03/0x09 are
class flags. Slot `+0x10` (`0x403fe0`) returns `&inst->flags104` (base `0x4078b0` returns NULL):
this is how other code recognizes "this is a bonus/kind object".

Differing vtable slots (all other slots = base class):

| slot | base | CBonus | 30 | 35 | 34 | 36 | 37 | 38 | meaning |
|---|---|---|---|---|---|---|---|---|---|
| +0x00 | `0x42e1f0` | `0x404010` | `0x404010` | `0x404010` | `0x44f530` | `0x44f6a0` | `0x44f860` | `0x404010` | scalar deleting dtor (34/36/37 reset their global counters to 0) |
| +0x04 | `0x42e210` | `0x44e7c0` | `0x44f400` | `0x44f480` | `0x44f570` | `0x44f6e0` | `0x44f8d0` | `0x44f970` | Init: bits 0..9 of `flags104` = 0x28 / 0x68 / 0x88 / 0x48 / 0xa8 / 0xc8 (column order 30/35/34/36/37/38); CBonus itself: FadeInst Init |
| +0x08 | `0x42e2b0` | `0x44f370` | = | = | = | = | = | = | Animate/Matrices(flags): lightweight variant, see §3 |
| +0x0c | nop `0x462c60` | nop | `0x44f460` | `0x44f4e0` | `0x44f630` | `0x44f770` | `0x44f9f0` | `0x44f9f0` | **Update per frame** (called by `0x42b400` for every instance in `world+0x64`): draws the halo sprite, §3 |
| +0x10 | `0x4078b0` | `0x403fe0` | = | = | = | = | = | = | `return &this->flags104` |
| +0x44 | `0x42e250` | nop | nop | nop | nop | nop | nop | nop | (base: reset anim/links) |
| +0x58 | `0x42d5e0` | `0x44f350` | = | = | = | = | = | `0x44f9b0` | message handler, §4 |
| +0x70 | – | `0x44f320` | `0x44f420` | `0x44f4a0` | `0x44f5c0` | `0x44f700` | `0x44f920` | `0x44f990` | **Collect()** (pick up), §2 |
| +0x74 | – | – | – | – | `0x44f590` | – | `0x44f8f0` | – | **Respawn()**: put back into the world, §2.5 |

### 1.1 Struct fields

| offset | type | class | meaning | evidence |
|---|---|---|---|---|
| +0x0c | vec3 | Instance | position (effect/HUD/halo position) | `0x44f440`, `0x44f468` |
| +0x18 / +0x1c | int | Instance | sector index / world cell; −1 = out of the world (= collected/hidden) | `0x407850`, `0x407790` |
| +0x5c | int | Instance | node_base (index in matrix buffer `[0x509adc]+0xa0`, 48 B per node) | `0x44f5db` |
| +0x6c | f32 | Instance | transparency 0..1 (message 56); > 0.9 ⇒ `inst+8 \|= 0x40` | `0x44e810` |
| +0xf8 | Model* | Instance | `model+0x4c` = list of volume nodes (1-based) | `0x44f5d0` |
| +0xfc | f32 | FadeInst | fade target (message 56, ×0.01) | `0x44e7f0` |
| +0x100 | f32 | FadeInst | fade speed per s (message 57, ×0.01; Init = 100.0) | `0x44e907`, `0x44e7cf` |
| +0x104 | u32 | CBonus | flags; bits 5..9 = kind (1..6), ctor sets 0 | `0x44f308`, `0x44f400` |
| +0x108 | int | type 36 | sequence number within the level = value of `[0x5e54f0]` at construction (0,1,2,…) | `0x44f67e` |

Note: the Init of the bonus subclasses (`0x44f400` etc.) does NOT call the FadeInst Init `0x44e7c0`
and their Update (`+0x0c`) does not call `0x44e810` ⇒ the fade (messages 56/57) does nothing on bonuses
except setting fields.

### 1.2 Globals

| address | meaning | set by |
|---|---|---|
| `0x5e54e4` | total number of type-34 bonuses in the level ("Woody Bonus" total) | ctor `0x44f50e` (++), dtor `0x44f558` (=0) |
| `0x5e54e8` | number of type-34 bonuses collected | Collect `0x44f5c0` (++), **every ctor sets 0** (`0x44f514`) |
| `0x5e54ec`, `0x5e4cd0` | debug: flag + pointer to the first type-34 that falls outside the world at Respawn | `0x44f5a9` |
| `0x5e54f0` | number of type-36 items in the level (handed out as a sequence number) | ctor `0x44f689`, dtor `0x44f6c6` |
| `0x5e54f4` | total number of type-37 race bonuses; list `0x5e48d0[256]` (`'Too much bonus race max is : %d'`) | ctor `0x44f803..0x44f817` |
| `0x5e54f8` | number of race bonuses collected | Collect `0x44f920` (++), `0x44f8a0` (=0), ctor (=0) |
| `0x5e54fc`, `0x5e50e4` | debug like `0x5e54ec`, for race | `0x44f909` |
| `[0x5d7afc]` → `[+0]` | Game → Perso* (the player receiving the bonus) | all Collects |
| `0x5d7b44` | HUD object | `0x44f334` |
| `0x5e48c8` | sound queue (`0x468a00(this, id, 0)` = push (id,0) into `+0x14[]`, counter +4, max +0xc) | |
| `0x5e5814` / `0x5e5818` | savegame manager (`+0x380` = active slot) / savegame buffer (0 = none) | `0x44fa13` |

Debug key 0x16 (`0x401af8..0x401b8a`) prints `'Woody Bonus : %d / %d'` (0x5e54e8 / 0x5e54e4),
`'Race Bonus : %d / %d'` (0x5e54f8 / 0x5e54f4) and `'-> Bonus Woody/Race with ID %x is outside of the world'`.
This confirms the names: **type 34 = "Bonus Woody", type 37 = "Bonus Race"**.

## 2. Picking up

### 2.1 Who tests? The SCRIPT, via the bonus model's volume (no distance test in the class)

There is no per-frame distance test in the bonus classes. (`0x451770`, referred to in EVENTS.md §4.1 as a
"bonus/switch object", belongs to the **Chest class** types 120/121, see §7.) Every
bonus object has this pattern in the level script (W1A object 62, `out/ekoasm/W1A.ekoasm` l. 732..757):

```
init:  SEND 1200 (obj 0x100003e, type 34)          ; SetTypeInstance
run:   VOL_FLAG4 <volslot>  ; JF end               ; volume.flags & 0x10
       SEND 10 (inst 0x100003e, 0, 0)              ; Collect
```

- Volume bit 0x10 is only set by **PersoEnter** (`0x4444e0`: `flags |= 0x36`; PersoIn
  `0x444530` only sets 0x24) ⇒ once, on entry.
- PersoEnter comes from the player's volume test (EVENTS.md §2): `0x462760(perso, 71.0)` →
  test point = **Perso.pos (+0x1f4) + (0, 71, 0)**, point-in-convex-polyhedron against the volume node(s)
  of the bonus model (`0x4303e0`), only for instances in the sector of the point and with
  `inst+0x1c != -1`. So there is **no radius**; the pickup distance = the shape of the volume node in
  the .ins model (scaled with inst+0x4c..).
- Message 10 → `0x44f350` → `vtable[+0x70]` (Collect). There is no "already collected" check; that is
  implicit: after Collect the instance is out of the world (`+0x1c = -1`) and no longer generates volume events.

### 2.2 Common ending: `0x44f320` CBonus::Collect

```c
void CBonus_Collect(CBonus *b) {                         /* 0x44f320 */
    HUD_Notify([0x5d7b44], (b->flags104 >> 5) & 0x1f, &b->pos);   /* 0x448510, §5 */
    Instance_RemoveFromWorld(b);                         /* 0x407850: out of the sector list, +0x1c = +0x18 = -1 */
}
```

No msgmask bit, no script variable, no scale animation: the object disappears immediately; the visible
"pickup" consists of the particle effect + the HUD icon.

### 2.3 Per type

| type | Collect | effect `0x4793d0(n, pos)` | Perso action | sound `0x468a00([0x5e48c8], id, 0)` | HUD kind |
|---|---|---|---|---|---|
| 30 extra life | `0x44f420` | 0 at inst.pos | `0x44c7a0(perso, +1)`: `+0x250 += 1` (min 1); savegame `slot+0xc = lives` | **0** | 1 |
| 35 special charge | `0x44f4a0` | 1 at inst.pos | `0x44c800`: `+0x254 += 1`; savegame `slot+0x14` | **2** (after the effect + base Collect) | 3 |
| 34 Bonus Woody | `0x44f5c0` | 2 at **world position of volume node 0** (`M[model->vol_nodes[0] + node_base - 1].t`) | `[0x5e54e8]++`; `0x44c8c0`: `+0x258++, +0x25c++, +0x71c++` | **3** | 4 |
| 36 unique item | `0x44f700` | 3 at inst.pos | `0x44c840(perso, +1)`: `+0x260 += 1` (min 0); savegame `slot+0x10`; save flag `0x450760(slot, level, b->index)` = 1 | **1** | 2 |
| 37 Bonus Race | `0x44f920` | 4 at inst.pos | `[0x5e54f8]++`; `0x44c8f0`: `+0x264++, +0x71c++` | **5** | 5 |
| 38 invincibility | handler `0x44f9c8` + `0x44f990` | 4 at inst.pos | `0x44c890(perso, arg·0.01)`: `+0x700 = +0x704 = T`, `+0x270 = max(+0x270, T)`, `+0x280 = max(+0x280, T)` | none | 6 (HUD does nothing: `0x448510` only knows 1..5) |

- `Perso+0x25c` ≥ 25 ⇒ in the Perso update (PERSO_FRAME.md §2.1, `0x44b6d1`): sound 4, health +1
  (max 5.0) or otherwise sound 0 + extra life; `+0x25c -= 25`. `+0x258` is the non-decreasing total.
- `Perso+0x254` (type 35) is consumed in `0x458bf0` (input action 0xb, `0x458c44`): if > 0 →
  `-1`, state `+0x750 = 1`, animation 0x13, invincible for the duration of anim 0x13 (`0x44cd10`);
  if 0 → sound 9. So it is ammo for a special action.
- `Perso+0x704` > 0 makes the player blink white (PERSO_MOVE.md §4.4); `+0x270` = invincible.
- Savegame layout: `slotbase = [0x5e5818] + slot·0x6dc` (slot = `[0x5e5814]+0x380`); `+0xc` lives,
  `+0x10` type-36 counter, `+0x14` type-35 counter, `+0x21 + level·0x3c + index` = byte "type-36 item
  #index of this level has been collected" (`0x450730` read / `0x450760` set; level = `[0x4c2d00]+0x68`;
  max 60 per level).

### 2.4 Effects `0x4793d0(n, pos)` (emitter pool `[0x5e823c]+0xdb8`, 80 B per entry, max 2000)

The pool is a bump allocator with no free list (counter at `+0x27100`); when full ⇒ `0x4793d0` silently drops the effect.
The driver is `0x470c70` (from `0x46d040`, `0x401dfa`: after the world, before the HUD); it re-reads the
boundary every iteration, so a particle created this frame is already drawn this frame, and cleanup is done by
swapping with the last one. `dt` is not a parameter: every callback reads `[[0x509adc]+0x38]` itself.

**Correction to earlier readings: `0x478f70` is not a star ring and `0x4792d0` does not look different. Neither
one draws anything** — both are just emitters, and both drop the same particle `0x4791f0` into the
pool. So all five effects consist of a single primitive; they only differ in *where* and *how often*.

| n | type | emitter lifetime | origin | rate | particle | placement |
|---|---|---|---|---|---|---|
| 0 | 30 extra life | 2.0 s | pos + (0,50,0) | **4 per frame** | 0.4 s | rotating **tetrahedron** (table `0x4b7990` rows 8..11: top r = 20, 3 base points r ≈ 44), scaled by (1−u) |
| 1 | 35 charge | 2.0 s | pos + (0,50,0) | **8 per frame** | 0.4 s | rotating **cube corners** (rows 0..7, ±40 per axis, r = 69.3), scaled by (1−u) |
| 2 | 34 Bonus Woody | 1.0 s | world position of volume node 0 | 50/s | 0.2 s | box: x,z ∈ ±30, y ∈ +25..+50 |
| 3 | 36 unique item | 1.0 s | inst.pos | 50/s | 0.2 s | same |
| 4 | 37/38 race + invincibility | 1.0 s | inst.pos | 50/s | 0.2 s | same |

**The particle `0x4791f0`** — a camera-facing quad at a **fixed** world point (never velocity, gravity
or drift): image `0x10004` = bank 0 image 4 (64×64, bpp 24, soft grey glow), color (0.5,0.5,0.5), **constant alpha 1.0**,
**additive ONE/ONE** (flag 7, bit 3 off; `0x479249..0x479295`). On the additive path the colour byte is `a·c·128` = 64
(`0x481e5e`), drawn under MODULATE2X: the particle adds **texture × 0.5**, not the full texture (PARTICLES.md §1.1; the
"0.5 = full white" rule holds only for the alpha-blended flag-8 sprites such as the halo below). So fading in and out is the
**size**, not the alpha:

> `size(u) = 30 · sin(π · ⌊255u⌋ / 256)` and `rotation = ⌊45u⌋` in 1/512 turn (31.6° over the entire lifetime).

Note: `sprite+0x264` is half the **diagonal** of the quad (`0x470fee..0x4710b3`: every corner is
`(size·cos θ, size·sin θ)` with θ = rot ± 45°), so the side is `1.4142 × size`. This applies to every
sprite with mode 0x12/0x1b, including the halo of §3.1.

**The shape burst `0x478f70`** rotates the shape every frame with three integer angles
`(⌊255.5u⌋, ⌊408.8u⌋, ⌊511u⌋)` via `0x46d220` (orthonormal, det +1) and scales it by `1 − u`: 8 (resp. 4)
spiral trails that implode at a point 50 above the bonus, ≈ 960 (resp. 480) particles per pickup at 60 fps.
The rate is **frame-rate dependent** — there is no limiter in it.

**The glitter box `0x4792d0`** is not: `acc += dt; n = ⌊acc·50⌋; acc −= n·0.02` (the `d8 e9` at `0x479318` is
`FSUBR`), so 50 particles per second, ~10 alive at once. `0x43ff40` = `rand()/32767` ⇒ **[0,1] inclusive**,
called three times per particle in the order x, y, z.
### 2.5 Coming back (respawn)

- **Type 34**: `Respawn` `0x44f590` = `0x407790(this, NULL)` (back into the sector of its own position;
  debug logging if that fails). There is **no caller at all** of this slot for type 34
  found (all `call [reg+0x74]`: `0x40bfe1`, `0x4517c5`, `0x4687bb`, `0x46cdc0`, `0x44f8bc` belong
  to other classes or to type 37) ⇒ Woody bonuses do not come back within a level, not even
  on death. Only the script can put them back with message 6 (on=1 → `0x407790`). The counter
  `0x5e54e8` is never decreased.
- **Type 37**: `0x44f8a0` = `[0x5e54f8] = 0; for (i < [0x5e54f4]) list[i]->Respawn()` – the only
  caller `0x45617c` in `0x456150` (Perso, from `0x44ab20` = restart): restarting the race;
  along with that `Perso+0x264 = Perso+0x71c = Perso+0x4e0`.
- **Type 36**: permanent. Update `0x44f770` tests the save flag (`0x450730`) every frame; if set →
  `0x407850` (the object disappears immediately after loading a level in which it was already collected).
  The flag is `rec+0x05+n` of the current level in the save block of the Perso's character (`[0x5e5814]+0x380`),
  n = `+0x108`, the order in which the level script made its type-36 objects (message 1200). Port: `g_uniq` /
  `uniq_flag` / `uniq_update` in `src/main_engine.c` (registered next to the 1200 handler, set in the Collect of
  message 10, tested every frame); verified on W1A (slot 0x124 = item 0).
- Types 30/35/38: no respawn code.

## 3. Per-frame behavior

### 3.1 Update (slot +0x0c) = halo sprite `0x479530(n, pos)`

```c
void Bonus_Update(CBonus *b) {            /* 0x44f460 (30, n=0), 0x44f4e0 (35, n=1), 0x44f630 (34, n=2, pos = volume node 0),
                                             0x44f770 (36, n=3, first tests the save flag), 0x44f9f0 (37 and 38, n=4) */
    static const int sprite[5] = { 0x10013, 0x10015, 0x10014, 0x1002e, 0x10017 };  /* jump table 0x479654 */
    Sprite *s = [0x5e823c] + 0xb00;
    s->tex   /*+0x228*/ = sprite[n];
    s->pos   /*+0x208*/ = pos + (0, 50.0f, 0);              /* 0x4a9030 */
    s->rgb   /*+0x214..*/ = (0.5f, 0.5f, 0.5f);  s->a /*+0x220*/ = 1.0f;
    s->mode  /*+0x260*/ = 0x12;
    float w = sintab[(int)(T * 256.0f) & 0x1ff];            /* T = [0x5e85d0], += dt, modulo 2.0 s (0x46d040); 512-float table at [0x5e823c] */
    s->size  /*+0x264*/ = w * w * 60.0f + 50.0f;            /* 0x4ab284, 0x4a9030: pulses 50..110, period 1 s */
    DrawSprite(s, 0x1b);                                    /* 0x470f10 */
}
```

Update is called by `0x42b400` (frame `0x401d78`, this = App+0x28 = `[0x509adc]`, `+0x38` = dt)
for every instance in the list `+0x64` (count `+0x60`). That list is **rebuilt every frame**
(`0x42a980` sets `+0x60 = 0`; `0x42a931..0x42a948` adds the entities from the visible sectors'
sector lists `sector+0x44 → inst+0x24`). So a collected bonus
(`0x407850`: out of the sector list) no longer gets an Update ⇒ no halo; a bonus in a non-
visible sector doesn't either (and type 36 only tests its save flag once its sector becomes visible).

### 3.2 Rotating / bobbing / magnet / scale

The bonus classes contain **no** code for rotation, bob, magnet or scale animation. Slot +0x08
(`0x44f370`) is a stripped-down version of the base Animate `0x42e2b0`: (a) if transparency
`+0x6c < 0.01` → cache test `0x42f3d0`; (b) flag 1 → `0x43eee0` (standard keyframe animation +
node matrices, rot 3×3 × scale +0x4c/+0x50/+0x54); (c) flag 4 → `0x42f460`/`0x42f490`
(render-cache class by size). So any rotation/bobbing comes from the model's **keyframe
animation** (animation state `+0xa0..+0xb0`, started by the .ins record or message 4), not
from code. That type 34 places the halo and effect at the *volume node* instead of at `inst.pos`
suggests that for that model the node moves (animated bobbing).

## 4. Messages

Chain: `0x44f9b0` (type 38 only) → `0x44f350` (CBonus) → `0x44e8f0` (FadeInst) → `0x42d5e0` (base).
Message record: `[0]` id, `[8]` inst id, `[0xc]` first argument.

| id | args | handler | meaning |
|---|---|---|---|
| 10 | inst, a, b | `0x44f362` (types 30, 34..37) | **Collect**: `this->vtable[+0x70]()`; a and b are ignored (script sends 0, 0) |
| 10 | inst, a, b | `0x44f9c8` (type 38) | `Perso_SetInvincible(a · 0.01 s)` (`0x44c890`, const `0x4aa0ac` = 0.01) and then Collect |
| 56 | inst, v | `0x44e91b` | fade target `+0xfc = v·0.01` via `0x44e7f0(v, 0)` (2nd arg 0 ⇒ transparency `+0x6c` not set directly). On bonuses without an effect (§1.1); also works on type 40 / chests |
| 57 | inst, v | `0x44e907` | fade speed `+0x100 = v·0.01` per second |
| 29 | inst | `0x451832` (types 40, 120, 121) | `this->vtable[+0x44]()`: type 40 → `0x44d320` (deactivate bomb: release projectile `+0x124`, state `+0x108 = 0`, bytes `+0x131..0x133 = 0`, out of the world); chest → `0x451730` (reset: anim reset `0x42e250`, back into the world `0x4077f0`, fade speed 100, fade target 0, clear msgmask 0x20) |
| other | | `0x42d5e0` | base messages (6 = show/hide also works on bonuses: on=1 is the only way to put a type-34 back) |

Addition to MESSAGES.md: 10 is therefore not "volumes/triggers" but **"pick up bonus"**; the
usage count 0 in MESSAGES.md is wrong for W1A (every bonus object sends it, with `SEND 4`).

## 5. HUD link

`0x448510(hud = [0x5d7b44], kind, pos)` (jump table `0x448604`, kind 1..5):

| kind | type | HUD function (on `hud+0x30`) | HUD flag = 1 |
|---|---|---|---|
| 1 | 30 | `0x461300(pos, hud+8)` | `hud+0x39` |
| 2 | 36 | `0x461420(pos)` | `hud+0x38` |
| 3 | 35 | `0x461560(pos)` | `hud+0x3a` |
| 4 | 34 | `0x4616a0(pos)`; first clears byte `hud+0x10` | `hud+0x36` |
| 5 | 37 | `0x461760(pos)` | `hud+0x37` |

The `0x4613xx` functions start a HUD animation with the 3D position as source: the icon flies in 0.2 s linearly
to its fixed HUD slot while growing from nothing to full size, with a trail behind it, and then the number pops.
Fully worked out in [HUD_TEXT.md](HUD_TEXT.md) §4.6. The HUD values themselves come from
`0x44ae60` (Perso → HUD) every frame: `0x448380(+0x25c)`, `0x4482c0(lives−1)`, `0x448440(health)`,
`hud+0x24 = +0x264`, `0x448300(+0x254)`, `0x448340(+0x260)`.

HUD text (`0x447660`): mode `hud+4 == 0` (normal) draws `"[0x5e54e8] / [0x5e54e4]"` (collected /
total type 34; `0x4479d6..0x447a0d`), except in levels (`App+0x68`) 1, 11 and 18; mode 1 (race)
draws `"hud+0x24 / [0x5e54f4]"` (`0x4477c8..0x4477fc`). `0x44a6a0` gives the total
(`0x5e54f4` as Perso kind 4/5, otherwise `0x5e54e4`) to the Perso accumulator `+0x710` (`0x453c80`).

## 6. Pseudo-C (summary)

```c
enum { SND_LIFE = 0, SND_ITEM36 = 1, SND_CHARGE = 2, SND_BONUS = 3, SND_25BONUS = 4, SND_RACE = 5 };
#define PICKUP_TEST_Y      71.0f   /* test point = perso.pos + (0,71,0), 0x462760 */
#define BONUS_PER_HEART    25      /* 0x44b6d1 */
#define HEALTH_MAX         5.0f
#define HALO_Y             50.0f
#define HALO_SIZE_MIN      50.0f
#define HALO_SIZE_AMP      60.0f   /* size = 50 + 60*sin^2(2*pi*t/2s) */
#define HALO_RGB           0.5f
#define GLITTER_LIFE       1.0f
#define GLITTER_RATE       50.0f   /* per second, particle 0.2 s */
#define BURST_LIFE         2.0f   /* the shape burst of type 30 and 35 */
#define FX_PARTICLE_MAX    30.0f  /* half diagonal; side = 1.4142 x that */

/* script: if (volume.flags & 0x10 /*PersoEnter*/) send(10, inst, 0, 0); */
bool Bonus_OnMessage(CBonus *b, const Msg *m) {          /* 0x44f350 / 0x44f9b0 */
    if (m->id != 10) return FadeInst_OnMessage(b, m);    /* 56, 57, base */
    if (b->type == 38) Perso_SetInvincible(perso, m->arg0 * 0.01f);
    switch (b->type) {
    case 30: Perso_AddLives(perso, 1); Sound(0); Effect(0, b->pos); break;
    case 35: perso->charges++; save.charges = perso->charges; Effect(1, b->pos); break; /* Sound(2) afterwards */
    case 34: g_bonusTaken++; Effect(2, VolNodePos(b)); perso->bonusTotal++; perso->bonusCount++; perso->stat71c++; Sound(3); break;
    case 36: Effect(3, b->pos); perso->items++; save.items = perso->items; Sound(1); save.taken[level][b->index] = 1; break;
    case 37: g_raceTaken++; Effect(4, b->pos); /* Collect */ perso->raceCount++; perso->stat71c++; Sound(5); break;
    case 38: Effect(4, b->pos); break;
    }
    HUD_Notify(hud, kind(b), &b->pos);                   /* 0x448510 */
    World_Remove(b);                                     /* 0x407850: cell = -1 ⇒ invisible, no volume events */
    if (b->type == 35) Sound(2);
    return false;
}
void Bonus_Update(CBonus *b) {                           /* slot +0x0c, every frame */
    if (b->type == 36 && save.taken[level][b->index]) { World_Remove(b); return; }
    DrawHalo(haloSprite[b->type], (b->type == 34 ? VolNodePos(b) : b->pos) + (0, HALO_Y, 0),
             HALO_SIZE_MIN + HALO_SIZE_AMP * sq(sin512[(int)(T * 256) & 511]));
}
/* Perso update: */ if (perso->bonusCount >= 25) { Sound(4); if (health < 5) health += 1; else { Sound(0); lives++; } perso->bonusCount -= 25; }
```

## 7. Type 40 and the Chest class (not pickups)

- **Type 40 = bomb** (ctor `0x44d250`): pool `0x5e4880[16]`, counter `0x5e487c`; globals
  `[0x5e48c0] = 4.0`, `[0x5e48c4] = 2.0`. Init `0x44d2e0`: FadeInst Init, kind bits 0x23, `inst+8 |= 0x20`,
  `+0xf0 |= 1`, `+0x124 = 0`. Fields: `+0x108` state 0..6, `+0x10c/+0x110` timers, `+0x124`
  projectile object (`0x4490a0`), `+0x12c` script var (gets 1 on explosion, `0x44d70a`),
  `+0x131` "in use". Game message 1090 → `0x44d5d0` looks for a free bomb (`'Pas de bombes, ou plus
  assez de bombes dans ce niveau...'`) and starts it with `0x44d4d0`. Explosion `0x44d6e0` → state 6,
  SetVar, **`0x44d650`: radius 400.0** (`0x43c80000`): all type-17 objects with state 1/2 get
  `vtable[+0xa0](&pos, 400)`, all chests get `vtable[+0x70](&pos, 400)`; sound, effect `0x477060`,
  fade out. Update = `0x44e810` (fade). Message 29 = deactivate (§4).
- **Types 120/121 = "Exploding objects (as Chest)"** (ctor `0x451650`, vtable `0x4aafd0`, list
  `0x5e581c[32]`): `0x451770(pos, r)` = if msgmask 0x20 is not yet set and `|pos − inst.pos|² < r²`
  → `vtable[+0x74]` = `0x4517d0`: `0x436ca0(this, 1.0, 0, -1, -1, -1)`, effect `0x477060(1, &pos, 0)`,
  fade speed 0.5/s towards transparency 1.0, **set msgmask 0x20** (the script reacts to that).
  `0x451730` = reset. This corrects EVENTS.md §4.1: the row "bonus/switch object" is the chest;
  `+0x100 = 0.5` is a fade speed, not a scale, and the "distance to point" is the bomb explosion.

## 8. Recipe for reimplementation in C (src/main_engine.c / src/player.c)

1. On message 1200 (SetTypeInstance) with type 30/34/35/36/37/38: mark the instance as a bonus,
   store `type`; type 34: `bonus_total++` and `bonus_taken = 0`; type 37: `race_total++`, in the list;
   type 36: `index = item36_count++`.
2. The player's existing volume test (point = pos + (0,71,0), instances with cell ≠ −1 only)
   already yields `eko_vol_perso_enter`; the script does the rest and sends message 10. Nothing extra
   to test in C.
3. Implement instance message 10 for these types according to §6 (`Bonus_OnMessage`): counters on the
   player (`lives`, `charges`, `bonusTotal/bonusCount`, `items`, `raceCount`), global counters,
   sound id 0/2/3/1/5, then make the instance inactive (no longer drawn, no longer in the
   volume test). Type 38: `invincible = max(invincible, arg·0.01)`, blink timer = arg·0.01.
4. In the player update: `if (bonusCount >= 25) { health<5 ? health++ : lives++; bonusCount -= 25; }`.
5. Per frame for every active bonus: additive billboard at pos + (0,50,0), color 0.5, half diagonal
   `50 + 60·sin²(π·t)` (t in s; period 1 s), sprite id per type 0x10013/0x10015/0x10014/0x1002e/0x10017.
   Type 34: use the world position of the model's first volume node instead of inst.pos.
6. On pickup (§2.4): one kind of particle, an additive camera-facing quad from bank 0 image 4, white color,
   alpha 1, `size(u) = 30·sin(π·⌊255u⌋/256)` (half diagonal!), rotation `⌊45u⌋/512` turn. For 34/36/37/38:
   1 s, 50 per second in the box ±30 × +25..+50 × ±30, each 0.2 s. For 30/35: 2 s, each frame 4 (tetrahedron)
   resp. 8 (cube) at a time on a rotating shape that shrinks with `1−u` towards the point 50 above the bonus, each 0.4 s. That
   rate is per *frame* in the original; normalize to 60 Hz if your frame rate varies.
7. Rotating/bobbing: play the model's keyframe animation as for every instance; no
   extra code.
8. No respawn on death for 30/34/35/36/38. Race restart: put all type 37 back in the world,
   `race_taken = 0`, `raceCount` = the stored value.
9. Type 36: hide at level start (first Update) if `save.taken[level][index]`.
10. HUD animations on pickup and on paying out 25 W's: see [HUD_TEXT.md](HUD_TEXT.md) §4.6.
11. HUD: show `bonus_taken / bonus_total` (not in levels 1, 11, 18); in race mode
    `raceCount / race_total`.

## 9. Open questions

1. The shape/size of the volume node of the bonus models (.ins) has not been measured; it determines
   the actual pickup distance.
2. The exact visibility criterion by which `0x42a858..` fills the per-frame list (which sectors)
   has not been worked out; and `0x479530` writes into the same sprite record
   `[0x5e823c]+0xb00` every time and draws immediately (`0x470f10(…, 0x1b)`), the meaning of flag 0x1b is open.
3. ~~The HUD animations~~ — worked out and ported, see [HUD_TEXT.md](HUD_TEXT.md) §4.6.
4. Meaning of type 36 in gameplay terms (which item; sprite 0x1002e) and of `Perso+0x71c`.
5. Exactly what action `0x458bf0` is (consumes `Perso+0x254`, animation 0x13).
6. ~~Star ring `0x478f70`~~ — it is not a ring but a rotating cube/tetrahedron of spark sources, see §2.4.
7. Type 40: states 1..5 (`0x44d850..0x44d96e`) only examined globally.
8. Whether the bonus models in the .ins have an active animation (rotating/bobbing) has not been checked.
