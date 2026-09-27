# SPLASH.md — the water splash `0x478660`

Static analysis of `game/Woody.exe` (image base 0x400000; all addresses are VAs). Floats are read directly from the
PE sections. The splash of `Kill(7)` (drowning in a water volume, class 60, WATER.md §4) and of script message 1505.
**Ported** following §9 (`game_splash`, fx kinds 3..6 in `src/main_engine.c` `fx_update`, `case 1505`, `player_kill`);
checked again constant by constant against §2..§6 (rnd order, 0.3 / 500 / 0.002, 5 / 0.2, D = rnd·100 + 150, streak
u + 0.08 / u + 0.1 half width 4, ripple 25u + 5, ring 2R + 600u, colours not doubled): no difference.

Notation: `fx` = the effect pool `[0x5e823c]+0xdb8` (2000 records of 0x50 B, counter `[0x5e823c]+0x27eb8` = `fx+0x27100`,
driver `0x470c70`, BONUS.md §2.4), `S` = the sprite/line parameters `[0x5e823c]+0xb00`, `dt` = `[[0x509adc]+0x38]`,
`rnd()` = `0x43ff40` = `rand()/32767.0` (**[0, 1] inclusive**), `cos512[i]` = `[0x5e823c][i]` = `cos(2π·i/512)`
(512 floats). `[0x4b798c]` is a dword in .data with value **128** (never written): `−cos512[(a+128)&511] = sin(2π·a/512)`.
`ftol` = `0x499580` (**truncates**, temporarily sets the FPU to truncation mode); `fistp` = rounds with the active FPU
mode (**round to nearest**, default).

**Certainty.** Everything in §2–§6 has been read instruction by instruction (✔). Uncertain (≈) is marked explicitly.

---

## 0. Summary

`Splash(C, v, r)` puts **one emitter** (1.0 s) into the effect pool. That emitter draws nothing itself; it spawns

* in the first **0.3 s**, **500 drops per second** (≈ 140–150 total) on a ring of radius `R = r + rnd·50` around `C`:
  each drop is an **additive textured line** (bank 0 image **57**) that flies outward 150–250 units over 0.4–0.8 s
  along an arc of height `v·0.1` and, on landing, leaves behind a **small ripple ring** (bank 0 image **3**,
  0.6 s, flat on the water);
* every **0.2 s** (the first one immediately) a **large ring** (bank 0 image **58**, 0.7 s, flat on the water, additive) around `C`
  that grows from half diagonal `2R` to `2R + 600`; **5 rings** in total.

No sound, no camera effect, no light, no gravity (the arc is a sine, not integration), no ring decal on
the water surface itself (the rings and ripples ARE the "decals": flat sprites with normal (0, 1, 0)).

---

## 1. Signature and callers

```c
void Splash(const vec3 *C, float v /* "speed" */, float r /* radius */);   /* 0x478660, cdecl (caller cleans up 0xc) */
```

There are exactly **three** `call 0x478660` in the exe and **no** reference at all to `0x478660` as data (dword
search across the whole file: 0 hits). The callbacks `0x478360`/`0x477fa0`/`0x478290`/`0x4781b0` also each occur only
once as data (in their own spawner). So there is **no** splash for enemies, bombs, projectiles or "Perso steps into the
water" in the code: only drowning and the script.

| # | location | context | `C` | `v` | `r` |
|---|---|---|---|---|---|
| 1 | `0x44c33e` in `Kill` `0x44c110` (vtbl[38], branch `0x44c308`) | Perso `Kill(7)`, normal | `P.inst.pos + (0, 110, 0)` (`P+0xc..0x14`, `0x4aa390` = 110.0) | `0x44d170(P)` = `|P+0x204..0x20c| / P+0x2f8` = length of this frame's movement / dt (u/s) | **50.0** (`push 0x42480000`) |
| 2 | `0x44c65e` in race Kill `0x44c4c0` (branch `0x44c623`) | same for Perso subtype 4/5 (race, `0x44c16d`/`0x44c17d`) | same | same | **50.0** |
| 3 | `0x46ce27` in `0x46cdfd` | script message **1505** `[inst, f]` | `&level->inst[arg0 & 0xffffff]->pos` (`inst+0xc`, **no** offset) | **1000.0** (`push 0x447a0000`) | `(float)(int)f · 0.01` (`fild`, `0x4aa0ac` = 0.01) |

**Kill(7)** only comes from one place: the water volume (`0x4747f0` → `0x44d160` = `P->vtbl[0x98](7)`, WATER.md §4.2; all
`vtbl[0x98]` calls with a constant have been traced). Kill itself only lets kind 7 through if the Perso is not
already drowning (`0x44c1e6..0x44c20d`, race `0x44c4e3..0x44c4f8`), so the splash occurs **once**, even though the
water tests every frame. Also part of Kill (not of the splash): `+0x288 = 0` (race: 4.0), the camera freezes in place and
watches the player fall (`0x459030`, PERSO_DEATH §4.4), `0x462c90(J)` (stops falling further). Typically: falling at 1000–1500 u/s ⇒
`h = 1.0..1.5` ⇒ drop arc height 100–150; `R ∈ [50, 100]`. `C.y` = feet + 110; the water kills if feet + 120 is below
the top, and the grate sits 10 above that top (WATER.md §2 point 6), so `C` lies **≥ 20 units below the
drawn surface** (plus that frame's fall). Because the water does not write z and is drawn before the
additive list 3, you see the rings through the water.

**Message 1505** (`0x46cdfd`: `fild [msg+0xc]; fmul 0.01; …; push 1000.0; push &inst->pos`): 45 times across 7 levels (MESSAGES.md
says "usage 0" and WATER.md "W2A 10×"; both wrong). All cases, from `tools/ekodisasm.py extract/Data`:

| level | trigger | 1505 args | `R` | sound the script plays alongside it (SOUND.md) |
|---|---|---|---|---|
| W2A (17×, obj 130–144, 148, 159) | `COL_FLAG5 0..16` (Woody on collision slot n) | `[0x1000082.., 20000]` | 200..250 | 1622 `[inst, 0x1000002, 25]` (3D, 6× incl. one with vol 200), rest none |
| W2B (9×, obj 18–28) | `COL_FLAG5 16..26` | `[.., 20000]` | 200..250 | 1627 `[inst, 0x1000001, 130]` |
| W2D (10×, obj 291–329) | `VOL_FLAG4 65..83` | `[292.., 20000]` (7×), `[306/308/330, 10000]` (3×) | 200..250 / 100..150 | 1622 `[250.., 0x1000000, var28]` |
| K2A (5×, obj 552–556) | `COL_FLAG5 28..32` | `[0x1000228.., 20000]` | 200..250 | 1622 `[inst, 0x1000000, 100]` |
| S2A (2×, obj 534–535) | `COL_FLAG5 17..18` | `[0x1000216.., 20000]` | 200..250 | 1622 `[inst, 0x1000000, 100]` |
| K2R (1×, obj 495) | `VOL_FLAG4 138` | `[318, 50]` | **0.5..50.5** | 1600 `[0x1000000, 50]` (2D) |
| S2R (1×, obj 397) | `VOL_FLAG4 49` | `[422, 50]` | **0.5..50.5** | 1600 `[0x1000000, 50]` (2D) |

The `COL_FLAG5` cases are (presumably) sinking floating platforms: right next to the 1505, the script starts
animation `3 [inst, 1, 1, 50]` (W2A, before) or `3 [inst, 1, 1, 600]` (K2A/S2A, after) and resets it after a `DELAY`. The `VOL_FLAG4` cases in the race levels K2R/S2R are drowning spots (small splash + 2D sound). The **sound
therefore always comes from the script, never from `0x478660`.** The second argument without the `0x1000000` flag
(W2D/K2R/S2R) is simply a bare instance slot (`& 0xffffff`).

---

## 2. The spawner `0x478660` ✔

```c
void Splash(const vec3 *C, float v, float r)                  /* 0x478660 */
{
    FxRec *e = FxAlloc();          /* inline: n = [fx+0x27100]; if (n >= 2000) return; [fx+0x27100] = n+1; e = fx + n*0x50 */
    if (!e) return;                /* full ⇒ silently nothing */
    e->age   /*+0x00*/ = 0;
    e->life  /*+0x04*/ = 1.0f;                     /* 0x3f800000 imm */
    e->cb    /*+0x4c*/ = Splash_Emit;              /* 0x478360 */
    e->C     /*+0x08..0x10*/ = *C;
    e->h     /*+0x1c*/ = v * 0.001f;               /* 0x4aa0f4 = 0.001 */
    e->accD  /*+0x14*/ = 0;                        /* drop accumulator */
    e->accK  /*+0x18*/ = 0.2f;                     /* ring accumulator, 0x3e4ccccd imm ⇒ first ring on the first frame */
    e->R     /*+0x20*/ = rnd() * 50.0f + r;        /* 0x4a9030 = 50.0; the only rnd() call */
}
```

Emitter record layout: `+0` age, `+4` lifetime, `+8..+0x10` C, `+0x14` accD, `+0x18` accK, `+0x1c` h,
`+0x20` R, `+0x4c` callback. The driver `0x470c70` calls `cb(rec)` (cdecl) as long as `life > 0` and only cleans up a
record with `life ≤ 0` on the **next** pass (swapping with the last one); it re-reads the boundary every iteration, so
records spawned during the pass already get their first update **the same frame** (age = dt at the first draw).

## 3. Emitter update `0x478360` ✔

```c
void Splash_Emit(FxRec *e)                                    /* 0x478360 */
{
    e->age += dt;  e->accD += dt;  e->accK += dt;
    float u = e->age / e->life;                               /* life = 1.0 */
    if (!(u < 1.0f)) { e->life = -1.0f; return; }             /* 0x47864f: no spawns in the last frame */
    if (u < 0.3f) {                                           /* 0x4aab98 = 0.3 */
        int n = ftol(e->accD * 500.0f);                       /* 0x4a9998 = 500 */
        e->accD -= n * 0.002f;                                /* 0x4abd54 = 0.002 */
        for (; n > 0; n--) SpawnDrop(e);                      /* 0x4783e8; pool full ⇒ skip this one, loop continues */
    }
    int n = ftol(e->accK * 5.0f);                             /* 0x4a9884 = 5.0 */
    e->accK -= n * 0.2f;                                      /* 0x4a9760 = 0.2 */
    for (; n > 0; n--) SpawnRing(e);                          /* 0x4785cc */
}
```

* Drops: 500/s while `age < 0.3` ⇒ at 60 fps 8 per frame (first frame `ftol(8.33) = 8`), **≈ 141–150 total**.
* Rings: at `age ≈ dt, 0.2, 0.4, 0.6, 0.8` ⇒ **5 rings** (a 6th only if float rounding keeps `age` just under 1.0
  while `accK` reaches 0.2 — in practice this does not happen).

### 3.1 SpawnDrop (inline `0x4783e8..0x47858f`) ✔ — seven `rnd()` calls in this order

```c
FxRec *d = FxAlloc(); if (!d) continue;
d->age  = 0;
d->life = (rnd() + 1.0f) * 0.4f;                    /* #1; 0x4a900c = 1.0, 0x4aa394 = 0.4 ⇒ 0.4..0.8 s */
d->cb   = Drop_Update;                              /* 0x477fa0 */
int a   = ftol(rnd() * 511.0f);                     /* #2; 0x4abc90 = 511 ⇒ 0..511 (511 only when rand()==32767) */
float c = cos512[a & 511], s = -cos512[(a + 128) & 511];         /* s = sin(2πa/512) */
d->p0.x /*+0x08*/ = (rnd() * 20.0f + e->R) * c + e->C.x;        /* #3; 0x4a9994 = 20 */
d->p0.y /*+0x0c*/ = e->C.y;
d->p0.z /*+0x10*/ = (rnd() * 20.0f + e->R) * s + e->C.z;        /* #4: own jitter per axis (not purely radial) */
d->dir  /*+0x14..0x1c*/ = (c·R, 0, s·R);  if (|dir| > 0) dir /= |dir|;   /* ⇒ (c, 0, s); at R == 0 it stays (0,0,0) */
d->f24  /*+0x24*/ = (rnd() + 1.0f) * 10.0f;         /* #5; 0x4a9750 = 10; NO reader */
d->i20  /*+0x20*/ = fistp(rnd() * 511.0f);          /* #6; int; NO reader */
d->h    /*+0x28*/ = e->h;
d->D    /*+0x2c*/ = rnd() * 100.0f + 200.0f - 50.0f;  /* #7; 0x4a9010 = 100, 0x4aa164 = 200, 0x4a9030 = 50 ⇒ 150..250 */
```

### 3.2 SpawnRing (inline `0x4785cc..0x47863f`) ✔

```c
FxRec *k = FxAlloc(); if (!k) continue;
k->age = 0;  k->life = 0.7f;                         /* 0x3f333333 imm */
k->cb  = Ring_Update;                                /* 0x4781b0 */
k->pos /*+0x08..0x10*/ = e->C;
k->R   /*+0x14*/ = e->R;
k->rot /*+0x18*/ = fistp(rnd() * 512.0f);            /* 0x4a9874 = 512 ⇒ int 0..512, 1/512 revolution */
```

## 4. The drop `0x477fa0` ✔ (a line, not a sprite)

```c
void Drop_Update(FxRec *d)                                    /* 0x477fa0 */
{
    d->age += dt;
    float w = d->age / d->life;
    if (!(w < 1.0f)) goto land;                               /* 0x478100 */
    int i0 = fistp(w * 255.0f);                               /* 0x4aa308 = 255 */
    S->p0 /*+0x278*/ = { d->p0.x + d->dir.x * d->D * w,
                         d->p0.y - cos512[(i0 + 128) & 511] * d->h * 100.0f,     /* = p0.y + sin(2π·i0/512)·100h; 0x4a9010 */
                         d->p0.z + d->dir.z * d->D * w };
    float w8 = w + 0.08f;                                     /* 0x4aace4 = 0.08 */
    int i1 = fistp((w + 0.1f) * 255.0f);                      /* 0x4a9008 = 0.1 (note: y uses +0.1, x/z +0.08) */
    S->p1 /*+0x284*/ = { d->p0.x + d->dir.x * d->D * w8,
                         d->p0.y - cos512[(i1 + 128) & 511] * d->h * 100.0f,
                         d->p0.z + d->dir.z * d->D * w8 };
    S->rgba0.a /*+0x29c*/ = 0.0f;                             /* rgb0 (+0x290..0x298) is NOT set: leftover from the previous line */
    S->rgba1   /*+0x2a0..0x2ac*/ = (0.5f, 0.5f, 0.5f, 0.65f);   /* 0x3f000000 ×3, 0x3f266666 */
    S->hw      /*+0x2b0*/ = 4.0f;                             /* 0x40800000: half-width ⇒ 8-wide band */
    S->tex     /*+0x2b4*/ = 0x10039;                          /* bank 0 image 57 */
    Line(S, 0xe00);                                           /* 0x471a10: 0x800 own colors, 0x400 own width, 0x200 textured */
    return;
land:                                                         /* 0x478100: ripple at the end point, drop gone */
    FxRec *r = FxAlloc();
    if (r) { r->age = 0; r->life = 0.6f;                      /* 0x3f19999a */
             r->cb = Ripple_Update;                           /* 0x478290 */
             r->pos = { d->p0.x + d->dir.x * d->D * w,        /* same point as S->p0 with this w (≥ 1, so slightly past) */
                        d->p0.y - cos512[(fistp(w * 255.0f) + 128) & 511] * d->h * 100.0f,
                        d->p0.z + d->dir.z * d->D * w }; }
    d->life = -1.0f;                                          /* even if the pool was full */
}
```

* **Path**: horizontal linear from `p0` to `p0 + D·dir` (150–250 units over 0.4–0.8 s ⇒ 190–625 u/s), vertical
  `100·h·sin(π·round(255w)/256)` — a half sine, peak `100h` at `w ≈ 0.5`, in steps of 1/255 (fistp). No gravity.
* **Streak**: from `P(w)` (tail, alpha 0) to the point 0.08 further along the path (head, alpha 0.65); the head's y
  corresponds to `w + 0.1`, so the streak stands slightly steeper than the path. Length ≈ `0.08·D` = 12–20 horizontal. For
  `w > 0.9`, `w + 0.1` goes past 1 ⇒ `sin < 0` ⇒ the head dips just below `C.y`.
* **Line primitive `0x471a10`** (OBJECTS.md §2.1): both points to view space, perpendicular to the projected segment,
  camera-facing quad `p0 ± n·hw`, `p1 ± n·hw`; vertices v0/v1 at p0 with rgba0, v2/v3 at p1 with rgba1. UV (fixed,
  `0x470d80(0, 1)` on `0x470d6c`): v0 `(0,0)`, v1 `(0,1)`, v2 `(1,1)`, v3 `(1,0)` ⇒ **u runs from p0 (0) to p1 (1)**.
  Submitted with `0x481560(4 vertices, …, surface, 0x24)`: flag 4 = **additive ONE/ONE**, z-test on, z-write off (list 3).
* That rgb0 is left unset is harmless: on the additive path rgb gets multiplied by alpha (§7) and alpha0 = 0.

## 5. The ripple `0x478290` ✔ (for every drop that lands)

```c
void Ripple_Update(FxRec *r)                                  /* 0x478290, life 0.6 s */
{
    r->age += dt;  float u = r->age / r->life;
    if (!(u < 1.0f)) { r->life = -1.0f; return; }
    S->pos    /*+0x208*/ = r->pos;
    S->normal /*+0x230*/ = (0, 1.0f, 0);
    S->rgb    /*+0x214*/ = (0.65f, 0.65f, 0.8f);             /* 0x3f266666 ×2, 0x3f4ccccd */
    S->a      /*+0x220*/ = (1.0f - u) * 0.3f;                /* 0x4a900c, 0x4aab98 */
    S->mode   /*+0x260*/ = 0x12;
    S->tex    /*+0x228*/ = 0x10003;                           /* bank 0 image 3 */
    S->size   /*+0x264*/ = u * 25.0f + 5.0f;                  /* 0x4abc64 = 25, 0x4a9884 = 5; half DIAGONAL */
    DrawSprite(S, 2);                                         /* 0x470f10: own color; no bit 0 ⇒ flat in the plane ⊥ normal; no bit 2 ⇒ rotation ignored; no bit 3 ⇒ additive */
}
```

## 6. The ring `0x4781b0` ✔ (5× per splash, around the center)

```c
void Ring_Update(FxRec *k)                                    /* 0x4781b0, life 0.7 s */
{
    k->age += dt;  float u = k->age / k->life;
    if (!(u < 1.0f)) { k->life = -1.0f; return; }
    S->pos    = k->pos;                                       /* = C */
    S->normal = (0, 1.0f, 0);
    S->rgb    = (0.65f, 0.65f, 0.8f);
    S->a      = (1.0f - u) * 0.3f;
    S->mode   = 0x12;
    S->size   = 2.0f * k->R + u * 600.0f;                     /* fadd st0,st0 ; 0x4ab3bc = 600 */
    S->tex    = 0x1003a;                                      /* bank 0 image 58 (= same ring as the wake, WATER.md §4.1) */
    S->rot    /*+0x224*/ = k->rot;
    DrawSprite(S, 6);                                         /* own color + rotation, flat, additive */
}
```

**Size.** `size` is the half **diagonal** (corners at `size·(cos, sin)(rot ± 45°)`, BONUS.md §2.4), side = `1.4142·size`.
Image 58 (and 3) is a thin white ring on black, 64×64, bpp 24 (no alpha); the brightness peak lies at radius **21.5 px of
32** ⇒ visible ring radius ≈ `0.672 · size/√2` = **0.475·size**. Ring: radius ≈ `0.95·R` at the start (exactly where
the drops leave from) to `0.95·R + 285` at the end; Kill(7): 47–95 → 330–380; message 1505 with 20000: 190–238 → 475–523.
Ripple: radius 2.4 → 14.3. The rotation has no visual effect (rotationally symmetric image).

**Images** (Common/Woody.rck, Knothead.rck and Splinter.rck are byte-identical for these three): **3** = **58** (byte-identical,
64×64, average 46.5/255), **57** = soft white drop/streak, 32×32, bpp 24, average 152.5/255.

## 7. Color and blending (✔, applies to ALL additive sprites and lines)

The submit path `0x481560` chooses based on its 4th argument (sprite `0x4719b2..0x4719c9`: `0x20 | (bit 3 ? 8 : 4)`, line: `0x24`):

* **flag 8** (alpha blend, `0x481a05..`): color byte = `c·255` (`0x4aa308`), alpha byte = `a·255` ⇒ with MODULATE2X, rgb 0.5 = 1.0.
* **flag 4** (additive, `0x481d8c` → `0x481e5e..0x481f37`): color byte = **`a · c · 128`** (`0x4a9020` = 128.0), no
  alpha byte; blend ONE/ONE (`0x429182`/`0x429198`: SRCBLEND = DESTBLEND = 2). With MODULATE2X (LIGHTING.md §1.5 step 6) the
  contribution is thus **`texture · rgb · alpha · 1.004`**: alpha acts as brightness and rgb 0.5 here is **half**, not fully white.

For the splash: ring/ripple peak `(0.65, 0.65, 0.8)·0.3 = (0.195, 0.195, 0.24)` × texture, linear to 0 — a faint
blue-white ring; drop head `0.5·0.65 = 0.325` grey, tail 0.

> Side finding, settled 2026-09-26 (PARTICLES.md §1.1): BONUS.md §2.4 had "color (0.5,0.5,0.5) = full white", which only
> applies to alpha-blended sprites. The pickup particle `0x4791f0` is additive (flags 7, rgb 0.5, alpha 1, `0x479249..0x479295`)
> ⇒ colour byte 64 ⇒ texture × 0.5. The port drew it with rgb 1 (2× too bright) and now passes 0.5 (`fx_update` kind 2). The
> path was traced to the device: the sprite batches are flushed by `0x428d00` from `0x4299b6`, after `0x429758` set COLOROP
> MODULATE2X, and nothing in between changes COLOROP of stage 0. The wake in `src/water.c` (raw 0.8) is correct.

## 8. Constants

| address | value | usage |
|---|---|---|
| `0x4aa0f4` | 0.001 | `h = v·0.001` |
| `0x4a9030` | 50.0 | `R = r + rnd·50`; −50 in D |
| imm `0x3e4ccccd` | 0.2 | accK start |
| `0x4a900c` | 1.0 | emitter lifetime, `u < 1` tests, `1 − u`, `rnd + 1` |
| `0x4aab98` | 0.3 | drop window; ring/ripple alpha factor |
| `0x4a9998` | 500.0 | drops per s |
| `0x4abd54` | 0.002 | 1/500 |
| `0x4a9884` | 5.0 | rings per s; +5 ripple size |
| `0x4a9760` | 0.2 | 1/5 |
| `0x4aa394` | 0.4 | drop lifetime `(rnd+1)·0.4` |
| `0x4abc90` | 511.0 | drop angle, `+0x20` |
| `0x4a9994` | 20.0 | radial jitter |
| `0x4a9750` | 10.0 | `+0x24` (unused) |
| `0x4a9010` | 100.0 | `D` range; arc `h·100` |
| `0x4aa164` | 200.0 | `D = rnd·100 + 200 − 50` |
| `0x4a9004` | 0.0 | normalization test |
| `0x4a9874` | 512.0 | ring rotation |
| `0x4aa308` | 255.0 | `round(255·w)` |
| `0x4aace4` | 0.08 | x/z lead of the head |
| `0x4a9008` | 0.1 | y lead of the head |
| imm | 0.5 ×3, 0.65, 4.0, `0x10039` | line rgb1, alpha1, half-width, image 57 |
| imm `0x3f19999a` | 0.6 | ripple lifetime |
| imm `0x3f333333` | 0.7 | ring lifetime |
| imm `0x3f266666`, `0x3f4ccccd` | 0.65, 0.8 | ring/ripple rgb |
| `0x4abc64` | 25.0 | ripple growth |
| `0x4ab3bc` | 600.0 | ring growth |
| `[0x4b798c]` | int 128 | quarter turn in `cos512` |
| `0x4aa390` | 110.0 | Kill(7): `C = pos + (0,110,0)` |
| `0x4aa0ac` | 0.01 | message 1505: `r = f·0.01` |
| `0x4a9020` | 128.0 | additive submit path: color byte `a·c·128` |

## 9. Port recipe (carried out)

Everything goes into the existing pickup pool of `src/main_engine.c` (`FxRec g_fx[2000]`, `fx_new`, `fx_update`) — the same
pool as the original, and `fx_update` already processes records created during the pass in the same frame.

**`src/hud.c` / `src/hud.h`**
1. `fx_slot`: give image 57 its own slot (`image == 57 ? 14 : …`), `GLuint fx[14]` → `fx[15]`, and the release loop in
   `hud_free` (`i < 14`) → `i < 15`. Image 3 does not need to be loaded: it is byte-identical to 58 (slot 11) — pass `0x3a`
   for the ripple with a comment.
2. A textured line per image: `void hud_world_streak(int image, const float *a, const float *b, const float *eye, float hw, const float *rgb, float alpha_a, float alpha_b)`
   = `world_line(a, b, eye, hw, rgb, alpha_a, alpha_b, H.fx[fx_slot(image)])` (+ declaration in hud.h). `world_line` has
   the correct UV (u 0 at a, 1 at b), ONE/ONE and `glColor3f(rgb·alpha)` — exactly §7.
3. `hud_world_fx_plane` suffices for the ring and ripple (normal (0,1,0); the rotation may be dropped, the image is round).

**`src/main_engine.c`**
4. Extend `FxRec`: `Vec3 dir; float D, h, R, acc2; int rot;` and kinds `3` = emitter (`0x478360`), `4` = drop
   (`0x477fa0`), `5` = ripple (`0x478290`), `6` = ring (`0x4781b0`). `fx_new` sets the new fields to 0.
5. Spawner, declared in `src/player.h` next to `game_land_dust` (player.c calls it):
   ```c
   void game_splash(Vec3 c, float speed, float radius)                           /* 0x478660 (docs/SPLASH.md) */
   {
       FxRec *e = fx_new(1.0f, c, 3); if (!e) return;
       e->h = speed * 0.001f; e->acc = 0; e->acc2 = 0.2f; e->R = fx_rnd() * 50.0f + radius;
   }
   ```
6. In `fx_update`. That already computes `u = (age += dt) / life`, keeps the record at `u < 1` (`continue`) and otherwise
   swaps it with the last one (`i--`) — matching the original's `!(u < 1) ⇒ life = −1`. `fx_new` writes into a fixed
   array, so `e` stays valid; a record created during the loop still gets updated this frame, like in `0x470c70`.
   Before the existing `if (u < 1.0f)`:
   ```c
   if (e->kind == 4 && u >= 1.0f) fx_new(0.6f, drop_pt(e, u, u), 5);   /* 0x478100: a drop that lands leaves a ripple (at w >= 1) */
   ```
   and inside `if (u < 1.0f)` three extra branches:
   ```c
   } else if (e->kind == 3) {                                         /* 0x478360: the emitter, draws nothing */
       e->acc += dt; e->acc2 += dt;
       if (u < 0.3f) {
           int n = (int)(e->acc * 500.0f); e->acc -= n * 0.002f;      /* ftol truncates */
           while (n-- > 0) {
               FxRec *d = fx_new((fx_rnd() + 1.0f) * 0.4f, e->pos, 4); if (!d) continue;   /* rand #1; p0.y = C.y */
               int a = (int)(fx_rnd() * 511.0f);                                            /* rand #2 */
               float c = cos512(a), s = sin512(a);
               d->pos.x = (fx_rnd() * 20.0f + e->R) * c + e->pos.x;                         /* rand #3 */
               d->pos.z = (fx_rnd() * 20.0f + e->R) * s + e->pos.z;                         /* rand #4 */
               d->dir = e->R > 0 ? (Vec3){ c, 0, s } : (Vec3){ 0, 0, 0 };
               fx_rnd(); fx_rnd();                                    /* rand #5, #6: +0x24/+0x20, never read */
               d->h = e->h; d->D = fx_rnd() * 100.0f + 150.0f;         /* rand #7 */
           }
       }
       int n = (int)(e->acc2 * 5.0f); e->acc2 -= n * 0.2f;
       while (n-- > 0) { FxRec *k = fx_new(0.7f, e->pos, 6); if (k) { k->R = e->R; k->rot = (int)lrintf(fx_rnd() * 512.0f); } }
   } else if (e->kind == 4) {                                         /* 0x477fa0: an additive streak along a half-sine arc */
       Vec3 a = drop_pt(e, u, u), b = drop_pt(e, u + 0.08f, u + 0.1f);
       hud_world_streak(57, &a.x, &b.x, eye, 4.0f, grey05, 0.0f, 0.65f);
   } else if (e->kind == 5) {                                         /* 0x478290: ripple where a drop landed */
       hud_world_fx_plane(0x3a /* image 3 == image 58 */, &e->pos.x, up, u * 25.0f + 5.0f, blue, (1.0f - u) * 0.3f);
   } else if (e->kind == 6) {                                         /* 0x4781b0: the growing ring around the center */
       hud_world_fx_plane(0x3a, &e->pos.x, up, 2.0f * e->R + 600.0f * u, blue, (1.0f - u) * 0.3f);
   }
   ```
   with `grey05 = {0.5, 0.5, 0.5}`, `blue = {0.65, 0.65, 0.8}` (raw, §7: NO ×2), `up = {0, 1, 0}`,
   `cos512(i) = cos(2π·(i & 511)/512)`, `sin512(i) = −cos512(i + 128)` and
   `drop_pt(e, wx, wy) = { pos.x + dir.x·D·wx, pos.y + sin512(lrintf(255·wy))·h·100, pos.z + dir.z·D·wx }`
   (`lrintf` = fistp, rounds; a drop's `e->pos` is its `p0`). Dropping the rotation on the ring is fine
   (round image); anyone who wants it: give `hud_world_fx_plane` a `turns` argument (`rot/512`).
7. `fx_update(float dt)` → `fx_update(float dt, const float *eye)`; the call at the draw line (currently `fx_update(paused ? 0 : dt)`,
   ±l. 2649) becomes `fx_update(paused ? 0 : dt, &cam.pos.x)`. That sits within `hud_world_sprites_begin/end`, as needed.
8. Message 1505 in `on_msg` (next to 1506):
   `case 1505: if (in && m->nargs > 1) game_splash(in->position, 1000.0f, (float)(int32_t)m->args[1] * 0.01f); break;   /* 0x46cdfd */`

**`src/player.c`** (`player_kill`, both branches, right after the "already drowning" test):
9. `if (kind == 7) game_splash((Vec3){ p->pos.x, p->pos.y + 110.0f, p->pos.z }, sqrtf(p->vel.x * p->vel.x + p->vel.y * p->vel.y + p->vel.z * p->vel.z), 50.0f);`
   — `p->vel` = last frame's movement/dt (`src/player.c` ±l. 1405) = `0x44d170`. Race branch the same (`0x44c65e`).

**Not needed**: `src/water.c` does not change (Kill(7) already goes through `player_kill`); no sound to add (it comes from the
scripts: 1622/1627/1600 are already handled by the sound manager).

**Test**: `extract/Data W2A --pos -700 200 500` (drowning, WATER.md §5): one splash ≈ 20 below the surface;
W2A the floating platforms (COL_FLAG5 0..16) for message 1505 with R = 200..250.

## 10. Certain / uncertain

* ✔ All calculation rules, constants, record fields, order of the `rnd()` calls, images, sprite flags, the additive
  submit path (`a·c·128`) and the line's UV.
* ✔ There are no other callers (three `call`s, no data references).
* ≈ `P.inst.pos` (`P+0xc`) vs. `P+0x1f4`: the Kill branch reads `+0xc..0x14`; assumed that this is this frame's foot
  position (in the port `p->pos`).
* ≈ Whether the Perso movement `+0x204` at the moment of Kill(7) is already this frame's value (order of Perso update vs.
  water Update) — the difference is at most one frame's worth of velocity.
* ≈ Not compared against the running original (no trace/screenshot).
* Corrections to earlier docs: MESSAGES.md 1505 "usage 0" → 45× (7 levels); WATER.md §6 "W2A 10×" → 17×. PERSO_DEATH.md
  §4.3 is correct, with the addition: separate jitter per axis, `dir` is the normalized (c, 0, s), `+0x20`/`+0x24` unused,
  the line's rgb0 unset but with no effect, 5 rings, and §7 above (alpha = brightness on the additive path).
