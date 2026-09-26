# STORM.md — class 80, the lightning rod, and the thunderstorm

Static analysis of instance class 80 (size `0x118`, factory `0x403cf5` in `0x403440`, ctor `0x451a90`, vtable `0x4ab0c4`),
the storm driver `0x451cc0` and its effects, and the port in `src/storm.c`. Until now this was a paragraph in
PERSO_FRAME.md §4.2 ("thunderstorm / periodic hazard with safe zones") and a TODO row; the class was not ported, so the
W3A/W3B/W3D/K3A/S3A storms never struck.

**What it is.** Class 80 is a **lightning rod** (W3A model 3: a thin spear on top of a stone pillar). The storm itself is
not an instance but three globals, switched on by game message **1100** from a trigger volume. Every *interval* seconds
(15 s, W3B 12 s) lightning strikes: if Woody stands inside the shelter sphere of a rod, the bolt hits the top of the rod
and sparks crawl along it; otherwise the bolt hits Woody and kills him (`Kill(9)`). 1.7 s before each strike a thunder
rumble warns, and the screen darkens over the last 2.5 s, then flashes 2 to 3 times.

| vtable `0x4ab0c4` | address | what |
|---|---|---|
| dtor | `0x451ad0` → `0x451af0` | **empties the whole rod list** (`[0x5e59e0] = 0`), then the base dtor `0x42ff30` |
| [1] | `0x451b10` | Init: FadeInst Init `0x44e7c0`, then type word `0x27`, radius 1000, height 400, glow 0, inside 0 |
| [2] | `0x42e2b0` | base anim/skeleton tick (the rod is drawn as a normal model) |
| [3] | `0x44e810` | FadeInst update (fade in/out, messages 56/57) |
| [22] | `0x451b50` | handler: message 54, then the FadeInst handler `0x44e8f0` |
| [26] | `0x452010` | render colour (`[0x5ac850] = 2`, additive): the rod's pulse and its "sheltering" colour, §6 |
| other | base | `[17]` Reset `0x42e250`, `[19]` `0x450cc0`, … |

## 1. Where it is used

Rods (SetTypeInstance 80 + message 54 at level init, `out/g_*.log` and `tools/ekodisasm.py extract/Data`):

| level | rods | radius / height | storm on (1100) | storm off (1101) |
|---|---|---|---|---|
| W3A | 12, 13, 15, 609 (model 3) | 400/500, 400/500, 600/500, 500/500 | entering volume 67 (script 333), interval 1500 | leaving volume 67 |
| W3B | 31 | mostly 400/500; 600, 800; one 400/**10** | entering volumes 68, 69, 79, 149, 156, 168, 384, script 496; guarded by var 18 = 0 (so 1100 is sent once), interval **1200** | entering volumes 46, 80, 129, 169; scripts 467 (twice) and 474; var 18 := 0 |
| W3D | 6 | | entering volume 108, 1500 | leaving volume 108 |
| K3A | 4 | | entering volume 25 (script 254), 1500 | leaving volume 25 |
| S3A | 4 | | entering volume 25 (script 248), 1500 | leaving volume 25 |

The volume scripts are all `VOL_FLAG4 v → SEND 1100 [1500]; VOL_FLAG3 v → SEND 1101 [0]`. W3A's volume 67 is instance
333 (model 33, centre (−10928, −24, 613), scale 9.6×16.2×3.3); Woody walks into it about 2 s after the level start.

## 2. The class

Ctor `0x451a90`: Instance ctor `0x42e1a0`, `+0x104 = 0`, vtable, then `0x5e58cc[n++] = this` while `n < 64` (silently
ignored beyond that). Init `0x451b10`:

| offset | type | meaning | default |
|---|---|---|---|
| `+0x104` | u32 | type word: `(w & ~0x3d8) | 0x27` = category 7, subtype 1 | 0x27 |
| `+0x108` | f32 | **radius** of the shelter sphere (message 54 mode 1; raw, `fild`, no ×0.01) | 1000 (`0x447a0000`) |
| `+0x10c` | f32 | **height** of the rod (message 54 mode 2, raw) | 400 (`0x43c80000`) |
| `+0x110` | f32 | glow: 0..1, how "sheltering" the rod looks (§6) | 0 |
| `+0x114` | u8 | Woody was found inside this frame (set by `0x451cc0`, cleared by vt[26]) | 0 |

Handler `0x451b50`: `id == 54`: `mode = arg1` (`+0xc`), `v = (float)arg2` (`+0x10`); mode 1 → `+0x108 = v`, mode 2 →
`+0x10c = v`; every message then goes to `0x44e8f0` (fade 56/57, base). MESSAGES.md had mode 1 as unknown.

Category 7 is only used by the camera: its line-of-sight filter `0x422140` lets a hit on a category-7 instance through
(CAMERA.md §3.6), so a rod never blocks the view. The rod has normal collision otherwise.

## 3. The shelter test `0x451be0(pos, out)`

```c
Rod *ShelterAt(const Vec3 *pos, Vec3 *top)             /* 0x451be0 */
{
    for (int i = 0; i < g_nrods /*[0x5e59e0]*/; i++) {
        Rod *r = g_rods[i];                             /* 0x5e58cc */
        *top = r->pos;                                  /* inst +0xc */
        Vec3 c = { r->pos.x, r->pos.y + r->height * 0.5f /*0x4a9014*/, r->pos.z };
        Vec3 d = *pos - c;
        if (d.x*d.x + d.y*d.y + d.z*d.z < r->radius * r->radius) {   /* strict; 3D */
            top->y += r->height;                        /* 0x451c9b: the tip of the rod */
            return r;
        }
    }
    return NULL;
}
```

A **sphere** of radius `+0x108` around the middle of the rod, first match in list order. (PERSO_FRAME.md §4.2 said
`dx²+dz² < +0x108`; wrong on both counts.) `pos` is the Perso position (feet, `vtbl[34]` `0x44c030`: `+0x1f4`, or `+0x544`
while carried by a moving object).

## 4. The storm `0x451ba0` / `0x451bd0` / `0x451cc0`

Globals: `[0x5e59ec]` u8 on, `[0x5e59e4]` timer, `[0x5e59e8]` interval.

```c
void Storm_Start(float interval)                        /* 0x451ba0, game message 1100 (0x444b79: arg0 * 0.01) */
{
    g_interval = interval; g_timer = 1.0f; g_on = 1;    /* the first strike 1 s after the start */
    SkyFlash_Start(interval, 1);                        /* 0x46e030, §5.1 */
}
void Storm_Stop(void)                                   /* 0x451bd0: message 1101 (0x444b93), level load (0x4043a8 -> 0x451b90), */
{                                                       /* Game states 0 and 3 = Woody dead (0x445a04, 0x445a8c) */
    g_skyflash_on = 0;                                  /* 0x46e3a0 */
    g_on = 0;
}

void Storm_Update(float dt)                             /* 0x451cc0, first thing in the Game tick 0x4459c0 (not while paused) */
{
    if (!g_on) return;
    int above = g_timer > 1.7f;                         /* 0x4ab13c */
    g_timer -= dt;
    if (!(g_timer > 1.7f) && above) SoundFx(8, NULL);   /* 0x451d2d: 2D thunder rumble (sound 109, SOUND.md) */
    for (each actor a in list 1 [0x4c52d8, 0x4c531c]) if (Category(a) == 1) {   /* the Perso only (0x40c340) */
        Vec3 top; Rod *r = ShelterAt(a->vtbl[34](), &top);
        if (r) r->inside = 1;                           /* +0x114 */
    }
    if (g_timer > 0) return;                            /* 0x4a9004 */
    g_timer += g_interval;
    SkyFlash_Start(g_timer, 0);
    for (each actor a in list 1) if (Category(a) == 1) {
        Vec3 top; Rod *r = ShelterAt(a->vtbl[34](), &top);
        Vec3 pos = *a->vtbl[34]();
        if (!r) {
            a->vtbl[38](9);                             /* Kill(9): the lightning death (PERSO_DEATH.md) */
            top = pos + (0, 180, 0);                    /* 0x4a9dbc: his chest */
            SoundFx(7, a);                              /* 3D, on the Perso (sound 11) */
        }
        Vec3 sky = pos + (0, 2000, 0);                  /* 0x4ab138: always straight above Woody */
        Bolt(sky, top); Bolt(sky, top);                 /* 0x46def0 twice, §5.2 */
        if (r) {
            SoundFx(7, r);                              /* on the rod */
            float span = r->height - 80.0f;             /* 0x4ab134 */
            for (int i = 0; i < 3; i++) {
                Vec3 A = r->pos + (0, 80 + rnd()*span, 0), B = ..., C = ...;   /* rnd order A, B, C */
                Arc(B, C, A, rnd() + 0.2f);             /* 0x46df80(p1, p2, p3, delay), §5.3 */
            }
            Vec3 lo = r->pos + (0, 80, 0), hi = r->pos + (0, r->height, 0);
            Arc(hi, hi, lo, 0.0f);                      /* sweeps down the rod at once */
            Arc(lo, lo, hi, 0.4f);                      /* and back up 0.4 s later */
        }
    }
}
```

* **Who is struck.** Actor list 1 is filled by the actors' own updates of the previous frame (`0x40c080`). The Perso
  registers only when not frozen (`+0x690 == 0`, a cinematic), with no attached object (`+0x26c == 0`) and not in
  state 5 (scripted animation) (`0x44b699`); Boss2 (`0x40dd58`) and enemy type 12 (`0x4110e6`) register too, but they
  are not category 1 and the storm skips them. So a strike during a cinematic or a door action strikes nobody (the flash still happens).
* **Timing.** Timer 1.0 at the start ⇒ strike at +1 s, then every `interval` s; the warning when the timer crosses
  1.7, i.e. 1.7 s before each strike except the first. W3A/W3D/K3A/S3A: 15 s, W3B: 12 s.
* **Death.** `Kill(9)` makes the Game tick go to state 3 on the same frame (`0x4459c0` state 2 tests `vtbl[36]`), and
  state 3 stops the storm on the next frame, which also cuts the sky flash of that strike after one frame. After the
  respawn the storm only comes back when the script sends 1100 again (the volume is left on death, `0x44c730`, and
  re-entered if the checkpoint lies inside it).
* The rods do nothing themselves: no update of their own besides the fade, the strike is decided entirely by `0x451cc0`.

## 5. The effects

### 5.1 Sky flash `0x46e030` / `0x46e0d0`

State: `[0x5e827c]` on, `[0x5e8280]` t (counts down), `[0x5e8284]` t0, `[0x4b7a20]` n (flashes), `[0x4b7a24]` k,
`[0x4b7a2c]` rem, `[0x4b7a30]` dur, `[0x4b7a28]` intensity, `[0x5e8288]` the colour last drawn.

```c
void SkyFlash_Start(float t, int first)                 /* 0x46e030 */
{
    if (first) { F.t = 2.5f /*0x40200000*/; F.t0 = t; } else F.t = F.t0 = t;
    F.n = 2 - _ftol(rnd() * -2.0f /*0x4a9504*/);        /* truncation: 2, or 3 (4 only for rnd == 1.0) */
    NewFlash(); F.k = 1; F.on = 1;
}
void NewFlash(void)
{
    float d = rnd() * 0.1f; if (d < 0.05f) d -= 0.1f;   /* 0x4a9008, 0x4aab4c */
    F.rem = F.dur = d + 0.3f;                           /* 0x4aab98: 0.20..0.25 or 0.35..0.40 s */
    F.inten = rnd() * 40.0f;                            /* 0x4ab294 */
}
void SkyFlash_Draw(int paused)                          /* 0x46e0d0, called by the app right after the effects 0x46d040, before the HUD */
{
    if (!F.on) return;
    if (!paused) {
        float x = F.t0 - 1.8f;                          /* 0x4ab290: the flashes occupy the first 1.8 s */
        if (F.t < x) {                                  /* darkening towards the next strike */
            float a = 0;
            if (F.t < 2.5f) a = (2.5f - F.t) * 200.0f * 0.4f;   /* 0x4aa3e0, 0x4aa164, 0x4abc78: 0 -> 200 over the last 2.5 s */
            else if (F.t < 0) F.on = 0;                 /* dead code: F.t < 0 implies F.t < 2.5 */
            F.col = fistp(a) << 24;                     /* black */
        } else if (F.k < F.n) {                         /* flashes back to back ... */
            Colour(); F.rem -= dt;
            if (F.rem <= 0) { NewFlash(); F.k++; }
        } else if (F.rem > 0 && x + F.rem + 0.5f > F.t) { Colour(); F.rem -= dt; }   /* ... and the last one at the end of the 1.8 s */
        else F.col = 0;
        F.t -= dt;
    }
    RectVirtual(0, 0, 640, 480, F.col x4, blank, flag 8);   /* 0x480a10: alpha blend (HUD_TEXT.md 5.3), skipped when alpha < 2 */
}
void Colour(void)                                       /* 0x46e2a0: a flash is dark -> white -> dark */
{
    float d = F.dur, r = F.rem, grey, a;
    if (d * 2/3 < r)      { grey = (d - r) * 384 / d;  a = (d - r) * F.inten * 3 / d; }   /* 0x4a975c, 0x4aabac, 0x4a988c */
    else if (d * 1/3 < r) { grey = 128;                a = F.inten; }                      /* 0x4a9880 */
    else                  { grey = r * 384 / d;        a = F.inten * r * 3 / d; }
    F.col = fistp(a + 160) << 24 | fistp(grey) * 0x010101;   /* 0x4ab238; grey 128 = white (0x80 = 1.0, MODULATE2X) */
}
```

At `Storm_Start` the flash is armed with t = 2.5 and t0 = interval, so the screen starts darkening (alpha 0 → 80 over
the second before the first strike). After a strike t = t0 = the time to the next one: 1.8 s of 2..3 strobes at alpha
160..200, then clear, then the darkening over the last 2.5 s up to alpha 200 (78 % black) at the next strike.

### 5.2 The bolt `0x46def0(a, b)` → callback `0x46d520`

A particle in the pool `[0x5e823c]+0xdb8` (2000 × 0x50 B, `+0x27100` count): `+0` age, `+4` life **1.8 s**
(`0x3fe66666`), `+8` a, `+0x14` b, `+0x20` regen clock, `+0x24` a polyline from the pool `0x5e8270` (`0x47d380`),
`+0x28` dirty = 1, `+0x4c` callback. Per frame (dt = `[0x509adc]+0x38`):

```c
void Bolt_Tick(P *o)                                    /* 0x46d520 */
{
    o->age += dt; o->clock += dt;
    if (o->age / o->life >= 1) { FreePolyline(o->line); o->life = -1; return; }
    float len = |a - b|; int n = _ftol(len * (1/150.0f) /*0x4abc6c*/); float step = 1.0f / n;
    if (n > 40) { n = 40; step = 0.025f; }
    if (o->clock > o->life * 0.1f) { o->clock -= o->life * 0.1f; o->dirty = 1; }   /* new zigzag every 0.18 s */
    if (o->dirty) {
        w = norm(b - a); Basis(w, &u, &v);              /* 0x46d320 */
        Clear(o->line); prev = a;
        for (i = 0; i < n; i++) {
            p = (i == n-1) ? b : a + w * (i+1) * step * len + u * (rnd()*180 - 90) + v * (rnd()*180 - 90);   /* 0x4a9dbc, 0x4abc68 */
            AddSegment(o->line, prev, p, kind = _ftol(rnd() * 4));   /* 0x47d160 */
            prev = p;
        }
        o->dirty = 0;
    }
    float alpha = (rnd() + 1) * 0.5f;                   /* one flicker value per frame */
    for (i = 0; i < n; i++) {
        S.p0 = seg[i].a; S.p1 = seg[i].b; S.rgba0 = S.rgba1 = (1, 1, 1, alpha); S.halfWidth = 30;
        S.tex = 0x1001e;                                /* bank 0 image 30 */
        SpriteUV(S, seg[i].kind, 1);                    /* 0x470d80: 0 plain, 1 v mirrored, 2 u mirrored, 3 both */
        Line(&S, 0xe00);                                /* 0x471a10: own width, own colours, textured; additive */
        S.pos = b; S.rgb = (0.8, 0.8, 1.0); S.alpha = 0.7f; S.+0x260 = 0x12; S.tex = 0x10006;   /* bank 0 image 6 */
        S.size = rnd() * 20 + 25;                       /* 0x4a9994, 0x4abc64 */
        DrawSprite(&S, 3);                              /* 0x470f10: billboard, own colour — n times per frame at b */
    }
}
```

Two bolts per strike from 2000 above Woody down to his chest (+180) or to the tip of the rod: 1820 units ⇒ 12 segments,
jagged ±90 across, redrawn every 0.18 s, flickering, with a bright blue-white glow at the struck point.

### 5.3 The arc along the rod `0x46df80(p1, p2, p3, delay)` → callback `0x46da00`

`+4` life **0.75 s** (`0x3f400000`), `+8` p1, `+0x14` p2, `+0x20` p3, `+0x2c` polyline, `+0x30` dirty = 1, `+0x34` phase 0,
`+0x38` delay.

```c
void Arc_Tick(P *o)                                     /* 0x46da00 */
{
    o->age += dt;
    if (o->age < o->delay) return;
    float u = (o->age - o->delay) / o->life;
    if (u >= 1) { free; return; }
    int ph = u <= 0.25f ? 1 : u <= 0.5f ? 2 : u <= 0.75f ? 3 : 4;   /* 0x4a9ca0, 0x4a9014, 0x4aabb4 */
    if (ph != o->phase) { o->phase = ph; o->dirty = 1; }             /* a new shape each quarter */
    if (o->dirty) {                                     /* 14 points in a local frame: x, y across, z = 0..1 along */
        float ox = rnd()*100 - 50, oy = rnd()*100 - 50;               /* 0x4a9010, 0x4a9030: the bulge */
        pt[0] = (0,0,0); pt[13] = (0,0,1);
        for (i = 1; i < 13; i++) { t = i / 13.0f;       /* 0x4abc74 */
            s = -cos[(128 - _ftol(t * -256)) & 511];    /* 0x4b798c = 128, 0x4abc70 = -256: = sin(pi t) */
            pt[i] = (rnd()*10 + ox*s - 5, rnd()*10 + oy*s - 5, t); }  /* 0x4a9750, 0x4a9884 */
        o->dirty = 0;
    }
    Vec3 P = p2 + (p3 - p2) * u;                        /* the far end slides from p2 to p3 */
    Vec3 D = P - p1; float L = |D|; w = D / L; Basis(w, &bu, &bv);
    float alpha = (rnd() + 1) * 0.5f;
    for (i = 1; i < 14; i++)                            /* 13 lines 0x471a10(0xe00): tex 0x10000 (bank 0 image 0), half width 3, */
        Line(p1 + bu*pt[i-1].x + bv*pt[i-1].y + w*pt[i-1].z*L,       /* colour (0.55, 0.85, 0.92, alpha) at both ends, additive */
             p1 + bu*pt[i].x   + bv*pt[i].y   + w*pt[i].z*L);
}
```

All the storm's arcs run along the rod's own axis (x, z of the rod, y between +80 and the tip): a pale cyan spark that
bows out up to 50 units sideways, grows from p1 towards the moving end and changes shape four times in 0.75 s.

`Basis` `0x46d320` (OBJECTS.md §2.1): `u = norm(w.z, 0, −w.x)`, `v = w × u`; for a vertical `w` (`|w.x|, |w.z| < 0.001`)
`v = norm(0, w.z, −w.y)`, `u = v × w`.

## 6. The rod's colour `0x452010` (vt[26])

Called by the renderer for every drawn rod (dt = `[0x509adc]+0x38`, T = `[0x5e85d0]`, the effect clock, += dt mod 2 s):

```c
void Rod_Colour(Rod *r)                                 /* 0x452010 */
{
    if (r->inside) { r->glow += dt; if (r->glow > 1) r->glow = 1; r->inside = 0; }
    else if (r->glow > 0) r->glow -= dt;
    float p = cos[fistp(T * 0.5f * 512) & 511] * 250 * 0.5f + 128;  /* 0x4a9874, 0x4ab144, 0x4a9020: 3..253, period 2 s */
    if (r->glow <= 0) col = (p, p, p);
    else { g = min(r->glow, 1); col = ((1 - g) * p, p * 0.843137f /*0x4ab140*/, p); }
    [0x5ac850] = 2; [0x5ac854..5c] = col; [0x5ac860] = 0;   /* ADD col (255 = 1.0) to the lit vertex colour */
}
```

The rod always pulses (grey added, 2 s period); while Woody stands in its sphere **during a storm** the red drains out
within 1 s and it pulses cyan, draining back when he leaves.

## 7. Port (`src/storm.c`)

* `storm_add` on SetTypeInstance 80 (`main_engine.c` next to `water_add`), `storm_zone_param` on message 54,
  `storm_start` / `storm_stop` on game messages 1100 / 1101, `storm_reset` at level load.
* `storm_update(dt, player, frozen)` right before `player_game_tick` (not while paused); `frozen` = cinematic running
  or camera mode 4. The storm is stopped after the tick when the Game state was 0 or 3 before it, exactly as `0x4459c0`.
  The Perso counts as an actor unless frozen or in a scripted action (`script_act`, state 5).
* `storm_fx_draw` with the other world effects (bolts: `hud_world_streak_flip` with bank 0 image 30, new fx slot 19,
  and `hud_world_fx` image 6; arcs: `hud_world_streak` image 0); `storm_overlay_draw` first thing after `hud_begin`
  (`hud_rect`, the same ARGB convention as RectVirtual).
* Rod colour: `Instance.tint_mode = 2`, `tint_rgb = col / 255`.
* **Simplified / assumed:** the rod colour is updated every logic frame instead of only when the rod is drawn (differs
  only for a rod off screen); the actor list has no one-frame lag and the Perso's `+0x26c` (attached object) is not
  tested; the random numbers come from a local LCG, not the CRT `rand()` sequence; which uv corner each 0x470d80 mode
  mirrors is taken from the mode tables (`0x470de2`, `0x470e0c`, `0x470e3a`) without tracing the line quad's corner
  order; the glow sprite's `+0x260 = 0x12` is not decoded (drawn additive like the other fx sprites); the skeleton
  flash of the Kill(9) death (`0x477e40`) is ported since (PARTICLES.md §6).
* Log: `WOODY_STORMLOG=1` (rods and their parameters, start/stop, warning, every strike with "sheltered" or "HIT",
  glow per second).

**Test** (W3A, `extract/Data`):
* sheltered: `WOODY_STORMLOG=1 woody.exe extract/Data W3A --walk 2.5 --shot out/x.ppm 20` — storm on at 2.1 s,
  strikes at 3.1 and 18.1 s ("sheltered by a rod", rod 15, radius 600), thunder warning at 16.4 s.
  Screenshots: `--pos -12305 -368 -451 --yaw 60` + `WOODY_SHOTSEQ="out/rod 0.95 0.1 10"` (flash, bolt on the rod tip,
  cyan rod with arcs).
* hit: `WOODY_GOD=1 ... W3A --pos -11968 -368 956 --yaw -60` + `WOODY_SHOTSEQ="out/hit 0.97 0.12 10"` — on the walkway
  inside volume 67 but outside every rod: "PLAYER killed (kind 9)" at 1.0 s, two bolts into Woody, iris closes.
  (`WOODY_GOD` only blocks the enemy's hits, not Kill.)

## 8. Open questions

1. Not traced live in the original (all of the above is static); the flash strobes (grey 0 at alpha 160 at the start and
   end of each flash) look harsh but are what `0x46e2a0` computes.
2. The meaning of sprite field `+0x260` (0x12 here) and of the second argument of `0x470d80` (1 = the line's vertex set
   `+0x100..0x1c0`?).
3. W3B's rod with height 10 (a flat shelter sphere of radius 400 around a point 5 above its origin) — which instance and why.
