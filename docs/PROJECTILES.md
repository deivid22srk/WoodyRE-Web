# PROJECTILES.md — projectielsysteem (lanceerder type 42, missile type 41, sjablonen, visuals)

Statische analyse van `game/Woody.exe` (disassemblage `out/disasm_full.txt`, `tools/drange.py`, `tools/funcinfo.py`). Elk feit heeft een adres;
"onzeker" = niet regel voor regel gelezen of geen lezer/schrijver gevonden. Sluit aan op OBJECTS.md §2.2 (lanceerder), EVENTS.md §3.2 (press-events),
ENEMY.md §8 (schietende vijand), BONUS.md §7 (bom), SOUND.md §5 (SoundFx).

## 0. Samenvatting (wat de port moet weten)

- Er is één globale pool van **200 projectielen** (`0x5d7d48`, 0x104 B per stuk, actief-byte `+0xe4`; OBJECTS.md noemde er 50: fout). Een projectiel is
  géén instantie en géén actor: een punt met snelheid. Update `0x4490f0(dt)` uit de framefunctie (`0x401e7e`, direct ná de VM-tick `0x4019c0`, niet tijdens pauze `app+0xf4 & 8`).
- Een projectiel wordt gemaakt uit een **parameterblok van 0x68 B** (sjabloon). Vier sjablonen staan in `0x5d7ba8 + soort·0x68`, gevuld door `0x448c70`
  (eenmalig, uit `0x404360`). De lanceerder heeft zijn eigen kopie op `this+0x108`; bericht 1002 schrijft daarin.
- **Bericht 1002 parameter 2 = levensduur in seconden (×0.01)**. W1A: 1.40 s (lanceerder 27: 1.50 s) bij snelheid 1000 → het schot vliegt 1400 (1500) eenheden
  en verdwijnt dan. Geen zwaartekracht, geen doel (target −1 → NULL) → **rechte lijn langs de typecode-0-marker**.
- **De W1A-lanceerders gebruiken het missile-model (type 41) NIET.** Sjabloon 1 heeft visueel soort 2 = `0x46f8a0`: een **energiebol** (twee tegengesteld
  draaiende additieve sprites, bank 0 beeld 6) met een **lint van 400 eenheden** (bank 0 beeld 0, breedte 60) en een flits bij de monding en bij de inslag.
  Het missile-model + rook (`0x4700e0`) hoort bij visueel soort 0/1, dat geen enkel script via 1002 kan kiezen (parameter 18 geeft alleen 2, 3, 4 of 0 — zie §3;
  0 komt in geen enkel level voor); het wordt alleen bereikt via parameterblokken die code zelf vult (vijanden `P+0x74`, klasse 20/21). De 8 type-41-instanties in W1A
  zijn dus voor de lanceerders irrelevant; ze moeten alleen verborgen worden (`0x472530`).
- Raken: gesweepte bol (straal 5) tegen de cilinder van elke actor in actorlijst 1 → `actor->vtbl[39](0, schade = 1.0, &richting, &pos, 0)` = `Perso::Hit`
  (1 hartje, knockback 500 u/s langs de vliegrichting, 0.6 s onkwetsbaar; PERSO_MOVE.md §4.4). Daarna verdwijnt het projectiel. Wereld/instantie geraakt
  (straal `0x4359b0` van oude naar nieuwe positie) → projectiel weg (max. stuiters = 0). De aanval van de speler kan een projectiel **niet** vernietigen
  (geen enkele externe aanroeper van de deactivering behalve de bomcode `0x44d339`/`0x44d7c2`).
- Geluid: bij het **aanmaken** van het projectiel (`0x449130`), SoundFx 17/18/19/20 afhankelijk van visueel soort, 3D op de eigenaar (de lanceerder).
  Visueel soort 2 → **SoundFx 19** (ref 74, vol 25). Geen inslaggeluid voor gewone projectielen (SoundFx 12 is alleen voor een meegedragen bom die landt).

## 1. Datastructuren

### 1.1 Parameterblok / sjabloon `T` (0x68 B)

Kopie in het projectiel op `P+0x48`, in de lanceerder op `L+0x108`. Standaardconstructor `0x44a260` (door klasse-ctors gebruikt; identieke waarden zet de
lanceerder-ctor `0x452140` en de schutter `0x418820` inline) en de vier sjablonen uit `0x448c70`:

| T+ | P+ | L+ | type | ctor `0x44a260` | soort 0 | **soort 1** | soort 2 | soort 3 | betekenis (lezer) |
|---|---|---|---|---|---|---|---|---|---|
| 0x00 | 0x48 | 0x108 | vec3 | – | 0 | 0 | 0 | 0 | startpositie (door `Fire` gezet) → `P+0xb0` |
| 0x0c | 0x54 | 0x114 | vec3 | – | 0 | 0 | 0 | 0 | startrichting `dir0` (genormaliseerd door `Fire`) → `P+0xbc`, `P+0xc8 = dir0·snelheid` |
| 0x18 | 0x60 | 0x120 | f32 | 5 | 30 | **5** | 5 | 5 | **straal** van het projectiel (treffertest `0x44a0a0`; ×2 met meegedragen instantie; ook de tolerantie van de press-probe) |
| 0x1c | 0x64 | 0x124 | f32 | 0 | 15 | **0** | 0 | 0 | **zwaartekracht**: `vel.y −= dt · g · 200` (`0x44945f`), ondergrens −800 |
| 0x20 | 0x68 | 0x128 | f32 | 1000 | 1500 | **1000** | 1000 | 1000 | **beginsnelheid** (eenheden/s) → `P+0xd8` |
| 0x24 | 0x6c | 0x12c | f32 | 1 | 0.95 | **1** | 1 | 1 | demping per 1/60 s **op de grond** (`P+0xec`): `vel *= pow(d, dt·60)` |
| 0x28 | 0x70 | 0x130 | f32 | 1 | 0.99 | **1** | 1 | 1 | demping per 1/60 s **in de lucht** |
| 0x2c | 0x74 | 0x134 | f32 | 5 | 2 | **15** | 15 | 15 | **levensduur** (s): `age ≥ T+0x2c` en geen meegedragen instantie → weg (`0x4493ec`) |
| 0x30 | 0x78 | 0x138 | f32 | 20 | 1000 | **1** | 1 | 1 | **schade** (2e argument van `vtbl[39]`; hartjes) |
| 0x34 | 0x7c | 0x13c | i32 | 0 | −1 | **0** | 0 | 0 | **max. aantal stuiters** tegen wereld/instanties; −1 = onbeperkt (`0x449dff`) |
| 0x38 | 0x80 | 0x140 | ptr | 0 | 0 | 0 | 0 | 0 | **doelinstantie** voor het doelzoeken (door `Fire` = `L+0x178`) |
| 0x3c | 0x84 | 0x144 | f32 | 150 | 125 | **125** | 150 | 25 | **richthoogte**: doelpunt = `target.pos + (0, h, 0)` (ook in `Fire` bij richtvlag) |
| 0x40 | 0x88 | 0x148 | f32 | 0 | 0.025 | **0.025** | 0.5 | 0.5 | **stuurfactor xz** per 1/60 s: `k = pow(1 − s, dt·60)`, `dir.xz = doel.xz·(1−k) + dir.xz·k` |
| 0x44 | 0x8c | 0x14c | f32 | 0 | 0 | **2.5** | 100 | 100 | **verticale stuursnelheid** (eenheden per 1/60 s, zie §2.2) |
| 0x48 | 0x90 | 0x150 | f32 | 0 | 2 | **15** | 15 | 15 | xz-sturen alleen zolang `age < T+0x48` |
| 0x4c | 0x94 | 0x154 | f32 | 0 | 0 | **15** | 15 | 15 | verticaal sturen alleen zolang `age < T+0x4c` |
| 0x50 | 0x98 | 0x158 | f32 | 0.7 | 0.7 | **0** | 0 | 0 | begrenzing xz: als `dot(vel.xz, dir0.xz) < T+0x50` → xz-snelheid van vóór het sturen terug (`0x4499cf`) |
| 0x54 | 0x9c | 0x15c | f32 | 0.7 | 0.7 | **0** | −1 | −1 | begrenzing y: als `1 + vel.y·dir0.y < T+0x54` → `vel.y` van vóór het sturen terug (`0x4498ec`; letterlijk zo, vermoedelijk een bug in het origineel) |
| 0x58 | 0xa0 | 0x160 | ptr | 0 | 0 | 0 | 0 | 0 | **eigenaar** (instantie): geluidsbron, wordt overgeslagen in de treffertest; lanceerder: `this` (`0x452352`) |
| 0x5c | 0xa4 | 0x164 | ptr | 0 | 0 | 0 | 0 | 0 | **meegedragen instantie** (bom type 40, §2.4); lanceerder: altijd 0 |
| 0x60 | 0xa8 | 0x168 | i32 | 4 | 4 | **2** | 2 | 2 | **visueel soort** 0..3, ≥ 4 = geen (jumptabel `0x4492b8`, §5) |
| 0x64 | 0xac | 0x16c | u8 | 1 | 0 | **1** | 1 | 1 | 1 = raakt **alle** actors; 0 = alleen categorie 2 met subtype 8 of 12 (`0x44a0bd`) |

Soort 0 is het "gegooide" sjabloon (zwaartekracht 15·200 = 3000, stuitert eindeloos, 2 s, schade 1000, onzichtbaar) dat de bomcode gebruikt; een lanceerder met
soort 0 gooit echte bommen (`Fire`, §4.3). Soorten 2 en 3 verschillen van 1 alleen in richthoogte en stuurwaarden (agressief doelzoeken).

### 1.2 Projectiel `P` (0x104 B, pool `0x5d7d48[200]`)

| off | inhoud |
|---|---|
| +0x00, +0x24 | twee press-probes (ctor `0x436cf0`), alleen gebruikt met een meegedragen instantie (§2.4) |
| +0x48..0xaf | kopie van `T` |
| +0xb0 | vec3 **positie** |
| +0xbc | vec3 **richting** (genormaliseerde snelheid; de visuals en de treffer-callback lezen deze) |
| +0xc8 | vec3 **snelheid** (eenheden/s) |
| +0xd4 | f32 **leeftijd** (s) |
| +0xd8 | f32 actuele snelheid `|vel|` |
| +0xdc | i32 aantal stuiters |
| +0xe0 | i32 **generatieteller** van het slot (`++` bij elke init; visuals vergelijken hem om hergebruik te zien) |
| +0xe4 | u8 **actief** |
| +0xe8 / +0xec | i32 / u8: frames op een press-node / "ligt op de grond" (alleen §2.4) |
| +0xf0..0xfc, +0x100 | vlak (n, d) van de laatste stuiter, u8 "heeft gestuiterd" (geen lezer gevonden: onzeker) |

API: `0x449070(soort, T* uit)` kopieert een sjabloon; `0x4490a0(T*)` = eerste vrije slot → `0x449130` init, **NULL als alle 200 bezet** (dan gebeurt er niets);
`0x4490e0(P*)`/`0x449120` = deactiveren (`+0xe4 = 0`, verder niets: de visual merkt het zelf, §5); `0x4492d0(T*)` = herinit zonder geluid/visual (bom, `0x44d4ad`);
`0x448c70` deactiveert ook alle slots.

Aanroepers van `0x4490a0`: lanceerder `0x45268a`, schutter-vijand `0x41898c` (ENEMY.md §8), `0x40d128` (klasse 17-familie `0x40cb80`), `0x414e99`, `0x4169ae`
(andere vijandklassen), bom `0x44d5ae`.

## 2. Projectiel-update

### 2.1 Init `0x449130(this = P, T* src)`

```c
probe_ctor(P+0); probe_ctor(P+0x24);
P->age = 0; P->bounces = 0; P->press_frames = 0; P->grounded = 0; P->bounced = 0; P->active = 1;
memcpy(P+0x48, src, 0x68);
P->pos = T.pos;  P->dir = normalize(T.dir0);  P->speed = T.speed;  P->vel = P->dir * T.speed;
P->generation++;
switch (T.visual) {                                   /* jumptabel 0x4492b8 */
case 0: SoundFx(17, T.owner); Visual_Missile(P); break;   /* 0x44924f -> 0x4700e0 */
case 1: SoundFx(18, T.owner); Visual_Missile(P); break;   /* 0x449257 */
case 2: SoundFx(19, T.owner); Visual_Bolt(P);    break;   /* 0x449277 -> 0x46f8a0 */
case 3: SoundFx(20, T.owner); Visual_Fireball(P);break;   /* 0x449297 -> 0x470af0 */
}                                                     /* >= 4: geen geluid, geen visual */
```
`SoundFx` = `0x468a00(id, inst)` op `[0x5e48c8]`: met instantie 3D (SOUND.md §5: 17 = ref 69, 18 = ref 70, vol 50; 19 = ref 74, 20 = ref 75, vol 25).

### 2.2 Per frame `0x4493c0(this = P, dt)`

```c
P->age += dt;
if (P->age >= T.life && !T.carried) { P->active = 0; return; }             /* 0x4493ec -> 0x449c9c */
if (T.carried) { carried->vtbl[2](1); if (carried is categorie 3 && carried->byte[0x132]) return; }   /* bom vastgepakt: stil */
float f = dt * 60.0f;                                                       /* 0x4aabbc */
float damp;
if (!P->grounded) { P->vel.y -= dt * T.gravity * 200.0f;                    /* 0x4aa164 */
                    if (P->vel.y < -800.0f) P->vel.y = -800.0f;             /* 0x4aabb8 */
                    damp = T.damp_air; }
else              { P->vel.y = 0; damp = T.damp_ground; }
P->vel *= pow(damp, f);                                                     /* 0x4995c0 = pow(st1, st0) */

if (T.target) {                                                             /* 0x4494df: doelzoeken */
    Vec3 old = P->vel, tgt = T.target->pos /*+0xc*/ + (0, T.aim_h, 0);
    if (P->age < T.t_xz) {                                                  /* 0x44950d */
        float s = |P->vel|;  Vec3 v = normalize(P->vel);
        Vec2 d = normalize_xz(tgt - P->pos);
        float k = pow(1.0f - T.steer, f);
        v.x = d.x*(1-k) + v.x*k;  v.z = d.z*(1-k) + v.z*k;                  /* y blijft */
        P->vel = normalize(v) * s;
    }
    if (P->age < T.t_y && T.vsteer != 0 && P->pos.y != tgt.y) {             /* 0x44969d */
        float dy = P->pos.y - tgt.y, step = f * T.vsteer;  if (dy > 0) step = -step;
        Vec3 q = P->pos + P->vel;  q.y += step;                             /* punt één seconde vooruit */
        int reached = (dy > 0) ? (q.y < tgt.y) : (q.y > tgt.y);  if (reached) q.y = tgt.y;
        float s = |P->vel|;  Vec3 v = normalize(q - P->pos);
        if (reached) { v.y *= 4.0f /*0x4a94c0*/; v = normalize(v); }
        P->vel = v * s;
    }
    if (1.0f + P->vel.y * T.dir0.y < T.lim_y)  { s = |vel|; P->vel.y = old.y;                 P->vel = normalize(P->vel) * s; }   /* 0x4498ec */
    if (P->vel.x*T.dir0.x + P->vel.z*T.dir0.z < T.lim_xz) { s = |vel|; P->vel.x = old.x; P->vel.z = old.z; P->vel = normalize(P->vel) * s; }
}
P->dir = normalize(P->vel);  P->speed = |P->vel|;
Move(P, P->vel * dt);                                                       /* 0x449cc0, §2.3 */
if (T.carried && P->active) { ... §2.4 ... }
```

Zonder doel (W1A): `pos += dir0 · 1000 · dt` tot `age ≥ life`.

### 2.3 Verplaatsen en botsen `0x449cc0(this = P, Vec3* disp)`

```c
Vec3 old = P->pos;  P->pos += *disp;
HitActors(P, &old, &P->pos);                                                /* 0x44a0a0, §2.5 -- vóór de wereldtest, ook als die daarna een muur vindt */
if (T.carried) { platform-meevoeren via probe P+0x24 (0x437040): pos.xz += delta; carried->flags |= 0x40 tijdens de straal (eigen hulls overslaan) }
[0x53a560] = 0;  Ray(&old, &P->pos, -1);                                    /* 0x4359b0: wereld + instanties, zelfde functie als de laser (OBJECTS.md §2.1) */
switch ([0x53a554]) {                                                       /* trefsoort, jumptabel 0x449ea0 */
case 0: break;                                                              /* niets */
case 2: if (hit_inst /*[0x53a560]*/ == T.carried && T.carried) break;       /* eigen bom: negeren; anders door naar 1 */
case 1: if (T.max_bounce != -1 && P->bounces >= T.max_bounce) { P->active = 0; break; }      /* 0x449dff */
        P->bounces++;  P->plane = [0x4b3108..0x4b3114];  P->bounced = 1;
        Bounce(P, disp, &old, &plane, t /*[0x53a558]*/, 1);                 /* 0x449eb0 */
        break;
case 3: if (hit_inst == T.carried && T.carried) break;
default: P->active = 0;                                                     /* 0x449e82 */
}
```
`Bounce` `0x449eb0`: trefpunt `h = old + normalize(disp)·max(|disp|·t − 0.01, 0)`; eindpunt gespiegeld in het vlak `pos += −2·(n·pos + d)·n` (`0x4a9504`);
`dir = normalize(pos − h)`, `vel = dir · P->speed`; `pos = h` (vlag 1). Geen energieverlies behalve de demping `T+0x24/0x28`.

Voor de W1A-lanceerder (`max_bounce = 0`): **eerste treffer met wereld of instantie = projectiel weg** (de visual toont dan zijn inslagflits, §5.2).
Let op: de straal begint op het marker-begin; ligt dat binnen een hull van de lanceerder zelf dan sterft het schot direct. In W1A gebeurt dat kennelijk niet
(marker = "loop", buiten de behuizing); niet geverifieerd: onzeker.

### 2.4 Meegedragen instantie (`T+0x5c`, alleen bommen; BONUS.md §7)

Na `Move` (`0x449b35`): `carried->pos = P->pos + (0, 1, 0)`, `carried+0x60 = P->pos`, `0x4077f0` (her-cellen); buiten de wereld (`cell < 0`) → als het een bom is
(typewoord categorie 3, subtype 1) `Bom::Explode 0x44d6e0`, projectiel weg. Anders de **press-probe** `0x436dc0(P, −1, &(pos + (0, r, 0)), r, carried->id)` (EVENTS.md §3.2):
staat het projectiel op een press-node dan de eerste keer SoundFx 12 (3D op de bom), teller `P+0xe8++`; na **5 frames** `grounded = 1`, `vel.y = 0`, `pos.y = [0x53a568]`
(grondhoogte). Dit is de enige bron van "Press/UnPress door projectielen": **gewone (lanceerder-)projectielen sturen nooit press-events**.

### 2.5 Actors raken `0x44a0a0(this = P, Vec3* old, Vec3* new)`

```c
for (i = 0; i < [0x4c531c]; i++) {                       /* actorlijst 1 van het vorige frame, 8 B per entry (OBJECTS.md §2.1; max 8) */
    Actor *a = list[i].actor;
    if (!T.hits_all && !(cat(a) == 2 && (subtype(a) == 8 || subtype(a) == 12))) continue;   /* 0x40c340 / 0x40c350 */
    if (a == T.owner) continue;
    Cyl c; a->vtbl[24](&c);                              /* Perso 0x44cd60: midden = pos + (0, h/2, 0), straal 69, halve hoogte h/2 */
    float r = T.radius; if (T.carried) r *= 2;
    if (!SweepSphereCyl(old, new, r, &(c.x, c.y - c.hh, c.z), c.r, 2*c.hh)) continue;       /* 0x433920: xz-kwadratische vergelijking met R = r + c.r, y-bereik, bolkappen 0x433bc0 */
    if (T.owner && cat(T.owner) in {1, 2}) {             /* geschoten door speler of vijand */
        bool res = 1;
        if (!T.carried) res = a->vtbl[39](T.owner, T.damage, &P->dir, new, 0);
        T.owner->vtbl[41](res);                          /* vijand: "speler verslagen" -> toestand 8 (ENEMY.md §8) */
    } else
        a->vtbl[39](0, T.damage, &P->dir, new, 0);       /* lanceerder (categorie 6): resultaat genegeerd */
    if (T.carried is bom) Bom::Explode(T.carried);
    P->active = 0;  return;                              /* eerste treffer beëindigt de lus */
}
```
`Perso::vtbl[39]` = `0x44ca00` (PERSO_MOVE.md §4.4): genegeerd bij onkwetsbaarheid; knockback 500 u/s gedurende 0.2 s langs `P->dir` (dus **met het schot mee**),
hit-animatie, 0.6 s onkwetsbaar, `health −= 1.0`. Het resultaat wordt hier weggegooid, maar de Perso-update doodt zelf bij `health ≤ 0` (`vtbl[38](3)`, PERSO_FRAME.md r. 168).
In de port: `if (player_hit(p, dmg, dir)) player_kill(p, 3);`. Effectieve trefstraal tegen de speler: 5 + 69 = **74** in xz, y van voeten − 5 tot voeten + 193 + 5.

## 3. Bericht 1002: alle 20 parameters (`0x452360`, jumptabel `0x4524fc`)

| n | veld | schaal | W-gebruik (alle levels) | betekenis |
|---|---|---|---|---|
| 0 | `L+0x128` = T+0x20 | rauw | 500, 2500, 3000 | snelheid (u/s) |
| 1 | `L+0x124` = T+0x1c | rauw | 2, 10 | zwaartekracht (×200 u/s²) |
| **2** | `L+0x134` = T+0x2c **en** `L+0x184` | ×0.01 | 4..1500 (220×) | **levensduur in s**; `L+0x184` heeft geen lezer in de klasse (onzeker) |
| 3 | `L+0x13c` = T+0x34 | int | – | max. stuiters |
| 4 | `L+0x138` = T+0x30 | rauw | 100 | schade |
| 5 | `L+0x144` = T+0x3c | rauw | 40, 100 | richthoogte |
| 6 | `L+0x188` | ×0.01 | – | geen lezer gevonden (onzeker) |
| 7 | `L+0x17c` | int | 49× | schiet-animatie (index in het model) |
| 8 | `L+0x180` | ×0.01 | 49× | duur van die animatie in s |
| 9 | `L+0x120` = T+0x18 | rauw | – | straal |
| 10 / 11 | `L+0x12c` / `L+0x130` = T+0x24 / 0x28 | ×0.01 | – | demping grond / lucht |
| 12 | `L+0x148` = T+0x40 | ×0.001 | 0, 50 | stuurfactor xz |
| 13 | `L+0x14c` = T+0x44 | ×0.1 | 0, 100 | verticale stuursnelheid |
| 14 / 15 | `L+0x150` / `L+0x154` = T+0x48 / 0x4c | ×0.01 | 6, 50 | stuurtijd xz / y (s) |
| 16 | `L+0x15c` = T+0x54 | ×0.01 | 90, 95 | begrenzing y |
| 17 | `L+0x158` = T+0x50 | ×0.01 | 95, 100 | begrenzing xz |
| 18 | `L+0x168` = T+0x60 | tabel `0x45254c`: 0→2, 1→3, 2→4, 3→0; > 3 genegeerd | 1 (90×), 8, 9 (genegeerd) | visueel soort: 0 = bol, 1 = vuurbal, 2 = onzichtbaar, 3 = missile |
| 19 | `L+0x199` | bool | 1 (9×) | `Fire` richt op het doel i.p.v. langs de marker |

Let op de volgorde: **1001 overschrijft het hele blok** (sjabloon-kopie + reset), dus scripts sturen eerst 1001 en dan 1002. W1A stuurt alleen `1002 [inst, 2, 140|150]`.

## 4. Lanceerder (type 42)

### 4.1 Velden buiten het blok
`+0x170` i32 resterend aantal (−1 = eindeloos), `+0x174` f32 interval T, `+0x178` doel, `+0x17c` schiet-anim (−1), `+0x180` animduur, `+0x18c` soort (1001),
`+0x190` f32 t0, `+0x194` f32 tijd van het laatste schot, `+0x198` u8 actief, `+0x199` u8 richtvlag. Reset `vtbl[17]` `0x452260`: anim −1, `+0x180 = 0`,
`+0x184 = +0x188 = −1`, doel 0, aantal −1, T 0, vlaggen 0. Ctor `0x452140`: blok = waarden van `0x44a260`, daarna `0x452330(1)` → **sjabloon 1**, `L+0x160 (eigenaar) = this`.

### 4.2 Starten en denk-stap
```c
void Start(L, int count, float T, Inst* target) {        /* 0x4522b0; 1000 -> (1, 1.0, target), 1003 -> (count, t*0.01, target); target -1 -> NULL (0x444d65) */
    L->target = target;  L->T = (T < 0.2f) ? 0.2f : T;  L->count = count;      /* 0x4a9760 */
    L->t0 = now + dt;  L->last = now - L->T;  L->active = 1;                   /* now = [0x509adc]+0x30, dt = +0x38 */
}
void Think(L) {                                          /* 0x452780, vtbl[3] */
    FadeThink(L);                                        /* 0x44e810 */
    if (!L->active) return;
    float t = now - L->t0;
    if (floor(t / L->T) <= floor((t - dt) / L->T)) return;                     /* 0x499ede: alleen op een intervalgrens */
    if (now - L->last < L->T - 0.2f) return;
    L->count--;  L->last = now;  Fire(L);
    if (L->count == 0) L->active = 0;                                          /* -1 telt eindeloos verder af */
}
```
Gevolg: `t0 = now + dt`, dus bij de eerstvolgende denk-stap is `t ≈ 0` → `floor` springt van −1 naar 0 → **eerste schot direct (1–2 frames na het bericht), daarna elke T s**,
uitgelijnd op het startmoment (niet op het laatste schot). Alle W1A-lanceerders starten bij levelstart en vuren dus **gelijktijdig op t = 0, 3, 6, … s**.
Onzeker: of de denk-stap ook loopt als de sector van de lanceerder niet zichtbaar is (OBJECTS.md §2 zegt ja, BONUS.md §3.1 zegt dat `0x42b400` alleen zichtbare sectoren afloopt;
de grenslogica met `floor` is in elk geval bestand tegen overgeslagen frames: hoogstens één schot per gemiste periode).

### 4.3 `Fire` `0x452560`
```c
Vec3 v[2];  GetVector(L, 0, v, 0);                       /* 0x42f6b0: eerste marker-node typecode 0, wereldruimte */
Vec3 d = (L->aim && L->target) ? L->target->pos + (0, L->T.aim_h, 0) - v[0] : v[1] - v[0];
d = normalize(d);
L->T.target = L->target;  L->T.pos = v[0];  L->T.dir0 = d;
if (L->kind != 0) Projectile_Alloc(&L->T);               /* 0x4490a0: geluid + visual in 0x449130 */
else { Bomb* b = Bomb_Throw(&L->T, 0, -1, 0);            /* 0x44d5d0 */
       if (b) { b->+0x120 = L;  SoundFx(14, L); } }      /* 0x4526f6: alleen de bommenwerper heeft een eigen vuurgeluid */
if (L->anim != -1 && L->anim < L->model->nanims) {       /* 0x4526fb */
    L->+0xb0 = L->anim;  L->+0xb4 = L->+0xb8 = L->+0xbc = -1;  L->+0xa8 = now;      /* animatieketen van de basisklasse (INSTANCE.md) */
    SetAnimSpeed(L, model->anim[L->anim].len /*int, +4 van het 8-byte record*/ * (1/4096.0f) / L->anim_dur);   /* 0x42e290, 0x4aa138 */
}
```
Vuurpositie = marker-begin, richting = marker (of doel + richthoogte). De animatie wordt zo geschaald dat ze precies `param 8 · 0.01` s duurt. W1A gebruikt geen animatie.

## 5. Visuals

Alle visuals zijn records (80 B) in de algemene effectpool `[0x5e823c]+0xdb8` (max 2000; `+0` leeftijd, `+4` levensduur (−1 = vrijgeven), `+0x4c` updatefunctie;
dezelfde pool als BONUS.md §2.4). Ze tekenen via de sprite-primitief `0x470f10(S = [0x5e823c]+0xb00, vlaggen)` (`S+0x208` pos, `+0x214..0x21c` RGB, `+0x220` alpha,
`+0x224` rotatie in 1/512 omwenteling, `+0x228` beeldref `0x1000N` = bank 0 beeld N, `+0x260` modus 0x12, `+0x264` grootte = volle breedte zoals bij de pickups) en de
lijn-primitief `0x471a10` (OBJECTS.md §2.1). Spritevlaggen: bit 2 (4) = rotatie gebruiken, bit 3 (8) = niet-additief; **zonder bit 3 → submit-vlag 4 = additief ONE/ONE**
(`0x4719b7`). Alle sprites hieronder gebruiken vlag 7 of 6 → additief. `costab[i] = cos(2π·i/512)` (`[0x5e823c][i]`), dus `costab[(int)(u·128)] = cos(u·π/2)`.

### 5.1 Visueel soort 2 — energiebol `0x46f8a0` (**dit gebruikt W1A**)

Bij het aanmaken twee records:
1. **Flits** `0x46f180`, 0.4 s, op de startpositie (zie 5.2).
2. **Bol + lint** `0x46f2d0` (levensduur 10000 s, eindigt zichzelf): `fx+0xc = P`, `fx+0x40 = P->generation`, lint uit pool `0x5e82c8` (`0x47d380`; N = 11 punten, allemaal
   geïnitialiseerd op de startpositie `0x47d160`), `fx+0x3c = 400 / ((N−1)·speed)` = 0.04 s tussen lintpunten, `fx+0x10 = 400 / (speed·10)` = 0.04 s per getekend segment
   (`0x4a964c` = 400, `0x4a9750` = 10) → **lint van 10 segmenten × 40 = 400 eenheden** achter de bol (groeit vanaf de monding).

Per frame zolang `P->active && P->generation == fx+0x40` (`0x46f428`): `t += dt` (modulo 2.0 s), om de 0.04 s een lintpunt `(P->pos, P->dir)` (`0x47d090`), `fx+0x24 = P->pos`. Tekenen:
```c
/* kop (0x46f67a), op P->pos, beeld 6, wit */
sprite(img 6, rgb 1,1,1, alpha 1.0, size 50, rot = (int)(t * 255.5),              flags 7);   /* 0x4abc94 */
sprite(img 6, rgb 1,1,1, alpha 0.5, size 80, rot = (int)((1 - t*0.5) * 511.0),    flags 7);   /* 0x4abc90: draait tegengesteld */
/* lint (0x46f73a): 10 segmenten, i = 0 aan de kop, u0 = i*0.1, u1 = (i+1)*0.1 */
line(p[i], p[i+1], half_width 30, tex bank 0 beeld 0 (ref 0x10000),
     rgba0 = (0.25*(1-u0), 0.4*(1-u0), 0.45, cos(u0*pi/2)),                        /* 0x4a9ca0, 0x4aa394, 0x3ee66666 */
     rgba1 = (0.25*(1-u1), 0.4*(1-u1), 0.45, cos(u1*pi/2)), flags 0xe00);          /* getextureerd + eigen breedte + eigen kleuren; additief */
```
De punten `p[i]` zijn het lint bemonsterd op `i · fx+0x10` s terug in de tijd (interpolatie `0x46d0d0`). Bij een recht schot: `p[i] = pos − dir · min(i·40, afgelegde weg)`.

Zodra het projectiel weg is (of het slot hergebruikt): één keer (`fx+0x44`) een **inslagflits** `0x46f180` op de laatste positie `fx+0x24` (`0x46f36d`) — geen explosie, geen geluid —
de kop wordt niet meer getekend en het lint krimpt: `fx+0x10 −= dt · 3.0 · 0.1` (`0x4a988c`, `0x4a974c`), dus van 0.04 naar 0 in **0.133 s**; bij `≤ 0.0001` (`0x4abc98`) lint vrijgeven
(`0x47d3f0`) en record weg (`0x46f87c`). Dit gebeurt ook bij het gewone einde van de levensduur: **elk schot eindigt met een flits**.

### 5.2 Flits `0x46f180` (monding én inslag van soort 2), 0.4 s, `u = t/0.4`
```c
size = 200 * cos(u*pi/2);                                                          /* 0x4aa164 */
sprite(img 6, wit, alpha 1 - u,       size, rot = (int)(t * 256),           flags 7);   /* 0x4aa3ac */
sprite(img 4, wit, alpha 0.5 - 0.5*u, size, rot = (int)((1 - t*0.5) * 512), flags 7);   /* 0x4a9874 */
```

### 5.3 Visueel soort 0/1 — missile `0x4700e0` (niet door W1A-lanceerders)

Twee records: mondingsflits `0x46fa40` (0.4 s: beeld 32, wit, `size = 200·cos(u·π/2)`, `alpha = 0.5 − 0.5u`, `rot = (int)((1 − t/2)·512)`, vlag 7) en de missile `0x46fb30`:
lint uit pool `0x5e82e8` (N = 21; `fx+0x3c = 500/((N−1)·speed)`, `fx+0x10 = 500/(speed·20)` → **20 segmenten × 25 = 500 eenheden**, `0x4a9998` = 500, `0x4a9994` = 20),
`fx+0x48 = Missile_Take(P)` (`0x4722f0`) en bij succes `rook->state (+8) = 2`.

Per frame (levend): lintpunten als 5.1; kop: `sprite(img 4, rgb (1, 0.58, 0), alpha 1 − frac(t), size 40, rot (int)(t·255.5), flags 7)` op `P->pos + P->dir·55` (`0x4abc9c`);
lint: 20 segmenten, halve breedte 7, beeld 0, `rgb = 1 − u`, `alpha = cos(u·π/2)`, `u = i·0.05`, vlaggen 0xe00. Het model zelf volgt het projectiel via zijn eigen denk-stap (5.4).
Einde (`0x46fbca`): **explosie `0x477060(2, &laatste_pos, &(0,1,0))`**, `rook->state = 0`, `Missile_Release(inst)` (`0x472370`), lint krimpt `dt·3·0.05` per s (0.025 → 0 in 0.167 s).

Explosie soort 2 (`0x47717a`): één record `0x4762e0`, 0.3 s, `R = 400`: negen **vlak-georiënteerde** quads (spritevlag 2 = normaal uit `S+0x230`, geen billboard) met normalen
(1,0,0), (0,1,0), (0,0,1), (.7,0,.7), (−.7,0,.7), (.7,.7,0), (−.7,.7,0), (0,.7,.7), (0,−.7,.7); beeld 12, wit, `size = R·(0.3 + 0.7·sin(u·π/2))` (120 → 400), `alpha = 0.3·cos(u·π/2)`
(`0x4abd0c` = −128, `0x4abd10` = −0.7, `0x4abd14` = −0.3, `0x4aab98` = 0.3). Soort 1 = grote explosie (`0x476b50`, `0x476cd0`, `0x4762e0` met R = 1400 én 400), soort 0 = met normaal (`0x4765f0`, `0x476710`,
`0x476cd0`, scherven `0x476140`): niet gelezen. De explosie doet **geen schade** (schade komt alleen uit §2.5).

### 5.4 Type 41 (missile-instantie) en de pool

- Registratie: `0x403b5d` (SetTypeInstance 41) zet vtable `0x4a9360` en voegt de instantie toe aan `0x5e8344[]`, totaal `[0x5e840c]` (max 0x32 = 50, anders de Franse foutmelding);
  `[0x5e8410]` = aantal in gebruik. Beide tellers op 0 in `0x404360` (`0x4043c1`) en `0x46d1f3` (effecten opruimen).
- Init `0x4723f0`: rook-/uitlaatobject (0x34 B) `inst+0x100`: `+0` inst, `+4` aantal marker-nodes **typecode 9** (geteld met `GetVector(inst, 9, ·, n)`), `+8` toestand
  (0 uit, 1 opstarten 1 s, 2 aan, 3 dubbel), `+0xc` u8 "teken mij dit frame", `+0x10/0x14/0x18` fasen, `+0x20` rook-accumulator, `+0x24` vorige markerposities, `+0x28 = 3`
  (groottetabel-index), `+0x2c` per marker "heeft vorige positie"; gelinkt in lijst `[0x5e8564]`; `inst->flags |= 0x20`.
- `Missile_Take(P)` `0x4722f0`: `if (used >= total) return NULL;` anders `inst = pool[used]`, `inst+0xfc = P`, direct positioneren (`0x4724e0`), alle `+0x2c[i] = 0`, `used++`.
  **Pool leeg → geen model**, de rest van de visual (lint, kop, explosie) loopt gewoon door (`fx+0x48 = NULL`).
- `Missile_Release(inst)` `0x472370`: zoek in `pool[0..used)`, wissel met `pool[used−1]`, `used−−`, `0x407850(inst)` (uit de cellen = onzichtbaar).
- Denk-stap `0x4723d0` → `0x4724e0`: `inst->pos (+0xc) = P->pos`, `inst+0x60 = P->pos`, **oriëntatie `0x46d320(&inst->rot /*+0x28, 3 rijen*/, &P->dir)`**, `0x4077f0(inst, 0)`; daarna `rook->draw = 1`.
- `0x472530` (uit `0x46d120`, levelstart, na het legen van de effectpool `0x470d50`): `0x407850` op **alle** geregistreerde missiles → onzichtbaar tot `Take`.

Oriëntatie `0x46d320(out, d)` (d genormaliseerd):
```c
if (|d.x| < 0.001 && |d.z| < 0.001) {            /* verticaal, 0x46d375 */
    Vec3 v = normalize((0, d.z, -d.y));  row0 = cross(v, d);  row1 = v;  row2 = d;
} else {                                          /* 0x46d417 */
    Vec3 a = normalize((d.z, 0, -d.x));   row0 = a;  row1 = cross(d, a);  row2 = d;
}
```
Rijen = modelassen in wereldruimte (INSTANCE.md: `+0x28..0x48` 3×3 rotatie in rijen), dus de **+Z-as van het model wijst in de vliegrichting**, +X = horizontaal
`(d.z, 0, −d.x)`, +Y = `d × X` (voor d = (0,0,1): identiteit).

Uitlaat `0x475440` (lijst `[0x5e8564]`, lus `0x46d0ab`, alleen als `+0xc`), per typecode-9-marker (begin `m`, richting uit de beweging van de marker sinds het vorige frame, anders de
markervector), tabel `0x4abcc8` index 3 → 45 / 40 / 35 — op hoofdlijnen gelezen:
gloed 1 `sprite(img 32, wit, alpha 1 − 0.2·sin(π·f1), size 40, rot 512 − f1·512, flags 7)` (`f1 += dt·0.05`), gloed 2 idem size 35, `f2 += dt·0.15`, vlag 6;
vlam = drie om de as gedraaide quads (0, 85, 170 /512 omw. + `f3·512`, `f3 += dt·3`) beeld 31, alpha 0.8, grootte `45 + rand·10 − 5`, modus 0x13, vlag 0x62 (eigen assen `S+0x23c..0x25c`);
rook = **200 wolkjes/s** (`(int)(acc·200)`, `0x4aa164`; `0x4abd08` = 0.005 aftrek per wolkje) verdeeld over de afgelegde weg, elk een record `0x475380`: 0.2 s, beeld 14, wit,
`alpha = 0.3·(1 − u)`, `size = 15·(1 + u)`, willekeurige rotatie, vlag 7 (additief). Toestand 1 schaalt alles met `min(1, …)·6.67`-rampen, toestand 3 ×2 (klasse 20/21).

### 5.5 Visueel soort 3 — vuurbal `0x470af0` (1002 `[18, 1]`, 90× in latere levels; kort)
Record `0x470420` met lint uit pool `0x5e8310` (lintlengte 1000 eenheden in 32 segmenten: `0x4aa188` = 1000, `0x4ab5b8` = 32), halve lintbreedte 70 (beeld 0), kop twee sprites beeld 12 (alpha 0.7, grootte 140 en 70),
vonkenrecords `0x4702b0`, en bij het einde `0x477060` (explosie). Geluid SoundFx 20. Niet verder gelezen: onzeker.

## 6. Overige gebruikers (kort)
- **Schutter-vijand** `0x418820` (ENEMY.md §8): blok = ctor-waarden, dan `0x449070(1, ·)`; richting = mondingsvector **horizontaal** genormaliseerd (y = 0); overschrijft schade `= Pe+0x40`,
  doel, eigenaar = vijand, richthoogte én verticale stuursnelheid `= Pe+0x70`, stuurfactor `= Pe+0x68`, `T+0x54 = Pe+0x64`, snelheid `= Pe+0x60`, visueel `= Pe+0x74` (per subtype gezet in `0x41d510`:
  o.a. `0x41d84d` = 1 → missile; welke subtypes: niet uitgezocht). Hier komt het missile-model dus wél voor.
- **Bom** `0x44d3a0`/`0x44d4d0`: sjabloon 0 + `T+0x5c = bom`, §2.4.
- **Klasse 20/21** `0x452e10`, Perso `0x463530`/`0x463c90`, klasse 17-familie `0x40cb80`, vijanden `0x411e44`, `0x413516`, `0x414c96`, `0x41677e`: kopiëren een sjabloon; niet gevolgd.

## 7. Recept voor de port

Doel: W1A-lanceerders 26/27/28/194/195 (eindeloos, elke 3 s, recht langs de marker, 1.4/1.5 s). Alles past in `src/main_engine.c` naast de `Laser`-array.

**A. Data**
```c
typedef struct { float radius, gravity, speed, damp_ground, damp_air, life, damage; int max_bounce; float aim_h; int visual, hits_all; } ProjT;   /* doelzoeken weglaten tot een level het nodig heeft */
static const ProjT PROJ_KIND1 = { 5, 0, 1000, 1, 1, 15, 1, 0, 125, 2, 1 };
typedef struct { Instance *inst, *target; ProjT t; int kind, count, active, aim, anim; float T, t0, last, anim_dur; } Launcher;   static Launcher g_launchers[32];
typedef struct { int active; ProjT t; Vec3 pos, dir, start; float age, dead_t; const Instance *owner; } Proj;                       static Proj g_projs[200];
```
**B. Berichten** (`case 1200`: type 42 → `Launcher` met `t = PROJ_KIND1`, `kind = 1`, `count = −1`, `anim = −1`; bij levelwissel `g_nlaunchers = 0` en alle projectielen uit):
- `1001 [inst, k]`: reset (count −1, T 0, target 0, aim 0, anim −1, active 0) + sjabloon k (k = 0: bommenwerper → voorlopig niets afvuren).
- `1002 [inst, n, v]`: tabel §3; minimaal n = 2 → `t.life = v·0.01`, 0 → speed, 1 → gravity, 4 → damage, 9 → radius, 18 → visual via {2,3,4,0}, 19 → aim, 7/8 → anim.
- `1003 [inst, target|−1, count, t]`: `T = max(t·0.01, 0.2)`, `t0 = now + dt`, `last = now − T`, `active = 1`. `1000` = `(1, 1.0, target)`. `1004`: `active = 0`.
- Type 41 bij `1200`: instantie onzichtbaar maken (`visible = 0`, `0x472530`); verder niets nodig voor W1A.

**C. Per frame, ná de VM-tick, niet tijdens pauze/cinematic-freeze** (volgorde origineel: instantie-denkstappen → … → VM → projectielen):
1. Lanceerders: `Think` uit §4.2 letterlijk (floor-grens + `T − 0.2`). `Fire`: `inst_vector(inst, 0, &p0, &dir)` (bestaat al voor de lasers), vrij slot zoeken (geen → overslaan),
   `pos = start = p0`, `dir`, `age = 0`, `owner = inst`; `audio_fx(19, inst, &inst->position.x)` (visual 2; 17/18 voor 0/1, 20 voor 3); flits (E) op `p0`.
2. Projectielen: `age += dt; if (age >= life) dood;` `old = pos; pos += dir·speed·dt` (+ zwaartekracht/demping uit §2.2 als `gravity != 0`);
   eerst spelertest: segment `old→pos` tegen de spelercilinder met straal **69 + 5** en y-bereik `[feet − 5, feet + 193 + 5]` (hergebruik de segment-cilinder-code van `laser_hits_player`
   met een andere straal; niet ×0.85) → `if (player_hit(p, t.damage, dir)) player_kill(p, 3);` dood. Niet testen als de speler dood is (`dead_kind`) — actorlijst 1 bevat hem dan niet.
   Dan wereld: `f = gel_ray_frac(gel, old, pos); if (f <= 1) { pos = old + (pos−old)·f; dood; }` (max_bounce 0; instantie-hulls erbij zodra de port daar een straal voor heeft).
   Vijanden worden door `hits_all = 1` ook geraakt (`vtbl[39]`), optioneel via de bestaande vijand-schadefunctie in `src/enemy.c`.
3. "dood" = `active = 0`, `dead_t = 0`, inslagflits (E) op `pos`; het lint nog 0.133 s laten krimpen.

**D. Tekenen** (na de 3D-scène, bij de lasers; additief, diepte-test aan, diepte-schrijven uit):
- `hud_world_beam` uitbreiden met een beeldparameter (nu vast bank 0 beeld 1; het lint gebruikt **beeld 0**) en per-eind RGB, of een variant `hud_world_beam_img`.
  Lint: `L = min(400, |pos − start|)` (na de dood `L ·= max(0, 1 − dead_t/0.133)`), 10 segmenten van `L/10` vanaf de kop naar achteren, halve breedte 30,
  kleur per knooppunt `(0.25(1−u), 0.4(1−u), 0.45) · cos(u·π/2)`, `u = i/10`.
- `hud_world_sprite` uitbreiden tot een algemene additieve, geroteerde sprite `(bank 0 beeld N, pos, size, rgba, rot/512 omw.)` (nu alleen de 5 pickup-beelden, +50 hoogte en alpha-blend).
  Kop (alleen levend): beeld 6, size 50, alpha 1, `rot = t·255.5`; beeld 6, size 80, alpha 0.5, `rot = (1 − t/2)·511` (`t` = leeftijd modulo 2 s).
**E. Flits** (kleine array van `{pos, t}`), 0.4 s: `size = 200·cos(u·π/2)`; beeld 6 alpha `1 − u` rot `t·256`; beeld 4 alpha `0.5 − 0.5u` rot `(1 − t/2)·512`.

**F. Test**: W1A, lanceerder 26 op (1067, 1114, 2648) schiet naar −x; elke 3 s een blauwwitte bol met lint die 1400 eenheden vliegt (tot x ≈ −333 of de eerste muur) en met een flits dooft;
27 op (−283, 1120, 2915) naar +x (1500 eenheden), 28 op (1065, 1115, 3254) naar −x; alle drie tegelijk. Erin lopen kost 1 hartje en duwt de speler met het schot mee.

## 8. Open vragen
1. `L+0x184` (kopie van parameter 2) en `L+0x188` (parameter 6): geen lezer gevonden.
2. Loopt `Think` ook voor lanceerders in niet-zichtbare sectoren (§4.2)?
3. Raakt de straal `0x4359b0` de eigen hulls van de lanceerder (§2.3)? Geen uitsluiting in de code voor projectielen zonder meegedragen instantie.
4. Exacte betekenis van spritevlag-bits 1 en 2 en modus 0x12/0x13 van `0x470f10`; kleurschaal van de lijn-primitief (0.5 = neutraal bij de laser-default: is 0.45 hier "bijna vol"?).
5. Visueel soort 3 (`0x470420`), explosiesoorten 0/1, en welke vijand-subtypes `Pe+0x74 = 0/1` (missile) krijgen.
6. `P+0xf0..0x100` (stuitervlak): geen lezer gevonden.
