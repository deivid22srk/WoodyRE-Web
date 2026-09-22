# ROCKET.md — berijdbare raket (klasse type 20), bomkanon (type 21) en Perso-toestand 8

Statische analyse van `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`, capstone). Elke bewering heeft een adres.
Floats zijn uit `.rdata` gelezen. Vervangt OBJECTS.md §2.6 (die sectie was "op hoofdlijnen"; twee dingen daarin waren fout, zie §9).

**Kernpunten vooraf (corrigeert de verwachting uit de opdracht):**

* De raket wordt **niet bestuurd**. Hij vliegt in een **rechte lijn** van zijn startpositie naar **punt 0 van zijn eigen TRAJ** (`inst+0x78`,
  uit de `.ins`), versnelt met 2000 u/s² tot `+0x11c` en ontploft na precies `+0x114` seconden vliegen. Geen enkele botsingstest tijdens de vlucht.
* `+0x114` = **totale vliegtijd in s** (bericht 55 sub 1, ×0.01), `+0x11c` = **maximumsnelheid in u/s** (bericht 55 sub 2).
* De speler kan alleen **afspringen** (actie 4 = sprong of actie 6 = aanval, net ingedrukt) en alleen tijdens de vlucht (objecttoestand 5, 6, 7).
  Wie blijft zitten **sterft** in de explosie (`Kill(6)`, straal 600). De laatste seconde knippert de raket rood als waarschuwing.
* De explosie raakt alleen de **geregistreerde actors** (Perso, Boss2, vijandtype 12), **geen** type-17-objecten en **geen** chests (§4.3).
  In W1A breekt de raket dus niets: het is puur **vervoer** over een afgrond.
* Type 21 is een ander ding: een **kanon dat een bom (klasse 40) recht wegschiet waar Woody op meerijdt** (W2A, W2B, W2D). Hangt aan het bomsysteem.

---

## 0. Recept voor de port (type 20, genoeg voor W1A / WWS / KWS / SWS / K1A / S1A)

### 0.1 Data

```c
typedef struct {
    Instance *inst;  int type;              /* 20 (21: zie §7, pas na het bomsysteem) */
    int   state;  float t, speed;           /* +0x128, +0x130, +0x12c */
    float fly_time, vmax;                   /* +0x114 = 10.0, +0x11c = 1000.0 (bericht 55) */
    Vec3  start_pos;  Quat start_rot;       /* +0x134, +0x140 (origineel 3x3) */
    Quat  q0, q1;                           /* +0x170, +0x180 */
    int   exhaust;                          /* rook-emitter +0x16c: toestand +8 (0 uit, 1 opstarten, 2 aan) */
} Rocket;
#define RK_TURN   2.0f    /* +0x10c */      #define RK_IGNITE 0.3f   /* +0x110 */
#define RK_WARN   1.0f    /* +0x118 */      #define RK_ACCEL  2000f  /* +0x120 */
#define RK_MOUNT  0.83f   /* +0x124 */      #define RK_BLAST  600f   /* 0x453593 */
#define RK_GONE   0.5f    /* 0x4a9014 */
```

### 0.2 Berichten (`0x453730`)

* `1200 [inst, 20]` → maak de `Rocket`, bewaar startpositie/-rotatie, `exhaust = 0`, `inst->flags8 |= 0x20`.
* `55 [inst, 1, v]` → `fly_time = v * 0.01f`; `55 [inst, 2, v]` → `vmax = v`; andere sub-nummers: niets.
* `29 [inst]` → `rocket_reset()`.
* `40 [inst]` → als `state == 0` **en** speler in Perso-toestand 0 **en** op de grond: `player_mount()` (§0.4), `rocket_reset()`, `t = 0`, `state = 1`,
  `inst` niet-botsbaar. Anders: niets (het script probeert het pas 3 s later opnieuw, §8).

```c
void rocket_reset(Rocket *r) {                 /* 0x452ae0 */
    inst.pos = start_pos; inst.rot = start_rot; recell; r->state = 0;
    inst.noncollide = 0;  fade_rate = 1.0f; fade_target = 0;      /* fade zelf blijft staan: na een explosie (fade 1) komt hij in 1 s terug */
    per uitlaat-marker: has_prev = 0;          /* exhaust-toestand wordt NIET teruggezet (zie onzeker 6) */
}
```

### 0.3 Denk-stap per frame (`0x452e10`), alleen als niet gepauzeerd en `state != 0`; de fade-update (`0x44e810`, INSTANCE.md §4) loopt altijd

```c
exhaust.draw_this_frame = 1;
switch (state) {
case 1: t += dt; if (t >= 0.83f) state = 2; break;                         /* Woody klimt erop */
case 2: speed = 0; t = 0;                                                  /* draaidoel bepalen, 1 frame */
        f  = normalize(traj.point[0] - inst.pos);                          /* 3D */
        Ym = -f;  Zm = normalize(up - Ym * dot(Ym, up));  Xm = cross(Ym, Zm);   /* rijen X,Y,Z = modelassen in wereldruimte (§3.2) */
        q0 = quat(inst.rot);  q1 = quat(rows Xm, Ym, Zm);  state = 3; break;
case 3: loopB_active = 1;  t = min(t + dt, 2.0f);
        inst.rot = slerp(q0, q1, t / 2.0f);
        if (t >= 2.0f) { t = 0; state = 4; audio_fx(16, inst); exhaust = 1; } break;
case 4: t += dt; if (t >= 0.3f) { t = 0; state = 5; audio_fx(10, inst); } break;
case 5: fly(dt); t += dt; loopA_active = 1;
        if (t >= fly_time - 1.0f) { state = 6; t = 0; } break;
case 6: fly(dt); loopA_active = 1; t += dt;                                /* knippert rood, §5.2 */
        if (t >= 1.0f) { audio_fx(6, inst); explosion_big(inst.pos); t = 0; state = 7; } break;
case 7: for (a in registered_actors) if (cat(a) == 1 || cat(a) == 2) a->Explode(&inst.pos, 600.0f);   /* speler: §0.4 */
        fade = fade_target = 1.0f;  /* direct onzichtbaar */  t = 0; state = 9; break;
case 9: t += dt; if (t >= 0.5f) rocket_reset(r); break;                    /* terug op de startplek, fade-in 1 s */
}
loop(A: SoundFx 11 op inst, B: SoundFx 15 op inst);   /* start bij het eerste actieve frame, stopt zodra een frame niet actief (SOUND.md §5) */

void fly(float dt) {                                                       /* 0x452cc0 */
    d = normalize(traj.point[0] - start_pos);                              /* vaste richting; bij lengte 0 niet genormaliseerd */
    speed = min(speed + dt * 2000.0f, vmax);
    inst.pos += d * (speed * dt);  inst.center = inst.pos;  recell;
}
```
Geen botsing, geen doel-aankomsttest: de raket vliegt door alles heen tot de tijd om is (W1A: hij schiet 190..725 eenheden voorbij het TRAJ-punt, §8.3).

### 0.4 Speler (Perso-toestand 8, `0x465740` + `0x4657f0`)

```c
int player_mount(Player *p, Rocket *r) {           /* 0x465740 */
    if (p->state != 0 || !p->on_ground) return 0;
    p->ride = r; SetState(8); atk = 0; hit_by = 0;  /* +0x5b4, +0x5f0 */
    p->ride_p0 = p->pos; p->ride_q0 = quat(p->rot); p->ride_t = p->ride_T = 0.7f;   /* +0x6b4, +0x6c0, +0x6d0, +0x6d4 */
    reset wand- en grondcollider;  return 1;
}
/* elk frame in toestand 8; GEEN Perso_Move, GEEN MoveCollide, geen zwaartekracht, Orient laat de matrix met rust (0x44bd4b) */
seat = marker(r->inst, typecode 0, n 0).p[0];      /* wereldruimte, actuele pose; model 47: lokaal (0, -54.1, 23.2) */
if (p->ride_t <= 0) { p->pos = seat; p->rot = r->inst.rot; }
else { p->ride_t -= dt;
       if (p->ride_t < 0.7f - 0.3f) { u = (0.4f - p->ride_t) / 0.4f;       /* eerste 0.3 s: blijft staan */
            p->rot = slerp(p->ride_q0, quat(r->inst.rot), u);  p->pos = p->ride_p0 + (seat - p->ride_p0) * u; } }
switch (r->state) { case 1: Anim(0x3b); break;  case 2: case 3: Anim(0x3c); break;  case 4: Anim(0x3d); break;
                    case 5: Anim(0x3e); can_leave = 1; break;  case 6: case 7: can_leave = 1; ending = 1; break; }   /* 0, 8, 9: niets */
if ((JustPressed(4) || JustPressed(6)) && can_leave) { SetState(0); Jumper_Reset(); Jumper_ForceFall(1); set_dir = 1; }
if (set_dir || ending) Mover_SetFacing(normalize_xz(-p->rot.rowY));        /* = neusrichting van de raket, horizontaal */
volume_test(p, p->pos + (0, 20, 0));                                       /* 0x462760(p, 20.0): triggervolumes blijven werken */
```
* Explosie op de speler (`0x44d040`): als `|explosie − inst.pos|² < 600²`: `Hit(0, 0, &weg_xz, 0, 0)` (knockback, 0 schade), rumble, **`Kill(6)`**
  (PERSO_DEATH.md: anim 0x2b, fade na 2.5 s). Wie op tijd afsprong en > 600 weg is merkt niets.
* Na het afspringen: gewone val (Jumper-toestand 4), **geen meegenomen snelheid** (Mover-ramp is niet aangeraakt), kijkrichting = vliegrichting.
* Camera: volgcamera in **behind-mode** zolang toestand 8 (CAMERA.md §3.1, `0x4591ec`); `savedDir = Perso rij Y` = −vliegrichting (3D). Geen eigen afstanden.
* Port-afwijking die nodig is: met `WOODY_GOD` (of `+0x270 > 0`) wordt `Kill(6)` genegeerd en blijft het origineel voor altijd in toestand 8 op de
  gerespawnde raket zitten. Doe in de port: `if (r->state == 0 || r->state == 9) dismount` (zelfde code als afspringen).

### 0.5 Controle

`--pos 8845 1140 330` o.i.d. in W1A (volume 61 = kubus van 400 rond (8845, ~1260, 238), §8.2), aanval indrukken en **loslaten**.
Verwacht: 0.3 s stil, 0.4 s naar het zadel, op 0.83 s begint de raket 2.0 s te draaien (lus 15), 0.3 s ontsteking (geluid 16, uitlaat sputtert aan),
start (geluid 10, lus 11), 0.75 s versnellen tot 1500 u/s, op `t = 2.8 s` na de start rood knipperen, op 3.8 s explosie bij ≈ (9379, 2216, −4808).
Afspringen rond 2.9 s na de start (≈ 3750 eenheden ver) brengt je bij het TRAJ-punt (9306, 2065, −4104).

---

## 1. Klasse en struct

Ctor `0x452850(type)` (grootte 0x194, `0x403440`-tak van SetTypeInstance 20/21; OBJECTS.md §2.6): basis-Instance-ctor `0x42e1a0`, `+0x104 = 0`, `+0x128 = 0`,
`+0x16c = 0`, vtable `0x4ab1b8` (28 slots), `+0x164 = type`. Eén klasse voor beide types; alle verschillen zijn `cmp [this+0x164], 0x14 / 0x15`.

| vtbl | adres | wat |
|---|---|---|
| [1] | `0x452890` | Init (§2) |
| [3] | `0x452e10` | denk-stap (§3) |
| [4] | `0x403fe0` | `return this+0x104` (typewoord) |
| [17] (`+0x44`) | `0x452ae0` | Reset (§4.4); ook bericht 29 en zelf aangeroepen |
| [22] (`+0x58`) | `0x453730` | berichten (§2.1) |
| [26] (`+0x68`) | `0x4537d0` | render-kleur: rood knipperen in toestand 6 (§5.2) |
| — | `0x452bd0(out)` | **zitpositie** voor de Perso (geen vtable-slot; direct aangeroepen uit `0x4657f0`) |
| — | `0x452ca0(out)` | kopieert de 3×3-rotatie `+0x28..0x48` (9 floats) |

| offset | type | default | betekenis | adres |
|---|---|---|---|---|
| +0x08 | flags | `\|= 0x20` | 0x20 = niet her-cellen op animatie; **0x40 = niet-botsbaar** tijdens de rit | `0x4529b5`, `0x452aac`, `0x452b92` |
| +0x6c / +0xfc / +0x100 | f32 | 0 / 0 / 100 | FadeInst: transparantie, doel, snelheid (INSTANCE.md §4) | `0x44e7c0` |
| +0x78 | TRAJ* | .ins | **punt 0 = vliegdoel/-richting** (`[[this+0x78]+0x10]+4..0xc`) | `0x452ebf`, `0x452ce7` |
| +0x104 | u32 | | typewoord: type 20 → `(w & 0xfffffc44) \| 0x44` (categorie 4, subtype 2); type 21 → `\| 0x24` (categorie 4, subtype 1) | `0x4528cb`, `0x45299f` |
| +0x108 | f32 | 0 | ongebruikt (eerste veld van het parameterblok) | `0x453790` |
| +0x10c | f32 | **2.0** | draaitijd (toestand 3 en 8) | `0x453796` |
| +0x110 | f32 | **0.3** | ontstekingstijd (toestand 4) | `0x45379d` |
| +0x114 | f32 | **10.0** | **totale vliegtijd** (toestand 5 + 6); bericht 55 sub 1 (`v · 0.01`, `0x4aa0ac`); type 21: levensduur van de bom | `0x4537b9`, `0x453765` |
| +0x118 | f32 | **1.0** | duur van de waarschuwingsfase (toestand 6) | `0x4537c0` |
| +0x11c | f32 | **1000.0** | **maximumsnelheid** u/s; bericht 55 sub 2 (`v`, geen schaal); type 21: snelheid van de bom | `0x4537a4`, `0x453757` |
| +0x120 | f32 | **2000.0** | versnelling u/s² | `0x4537ab` |
| +0x124 | f32 | **0.83** (`0x3f547ae1`) | wachttijd na opstappen (toestand 1) | `0x4537c0` |
| +0x128 | int | 0 | **toestand** 0..9 | |
| +0x12c | f32 | | actuele snelheid | `0x452d5f` |
| +0x130 | f32 | | toestandstimer | |
| +0x134 | vec3 | Init | startpositie | `0x4529bd` |
| +0x140..0x160 | 9×f32 | Init | startrotatie (kopie van `+0x28..0x48`) | `0x4529c3..0x452a20` |
| +0x164 | int | ctor | 20 of 21 | |
| +0x168 | Bom* | 0 | type 21: de afgevuurde bom (klasse 40) | `0x453434` |
| +0x16c | Exhaust* | type 20 | rook-/uitlaatobject 0x34 B (§5.1); type 21: NULL | `0x452987` |
| +0x170 / +0x180 | quat | | slerp-begin / -eind | `0x453147`, `0x45315b` |
| +0x190 / +0x191 | u8 | | geluidsbronnen (SOUND.md §5 "bron"): lus SoundFx **11** / **15** | `0x4536ce`, `0x4536e8` |

## 2. Init `0x452890` en berichten

1. `0x44e7c0` (FadeInst-Init: `+0x100 = 100`, `+0xfc = 0`, `+0x6c = 0`).
2. Type 20: typewoord, en een uitlaatobject zoals de missile (PROJECTILES.md §5.4; hier inline `0x4528d8..0x452979`): `new(0x34)`, `+0 = this`,
   `+4` = aantal marker-nodes **typecode 9** (telt met `GetVector(this, 9, ·, n)` `0x42f6b0` tot die 0 geeft), `+0x24 = new(12·n)`, `+0x2c = new(n)`,
   `+0xc = 0`, `+8 = 2`, fasen 0, **`+0x28 = 6`** (groottetabel-index; de missile heeft 3), in lijst `[0x5e8564]`. Direct daarna **`+8 = 0`** (uit; `0x45298f`).
   Type 21: alleen het typewoord.
3. `inst+8 |= 0x20`; startpositie `+0xc..0x14` → `+0x134`, rotatie `+0x28..0x48` → `+0x140`; `+0x168 = 0`; parameterblok-defaults `0x453790` (tabel §1).

### 2.1 Handler `0x453730`

| bericht | code | effect |
|---|---|---|
| **29** (0x1d) | `0x453783` | `this->vtbl[17]()` = Reset |
| **40** (0x28) | `0x453779` → `0x452a50` | opstappen, zie hieronder |
| **55** (0x37) | `0x45374e` | `arg[1] == 1`: `+0x114 = (float)arg[2] · 0.01`; `== 2`: `+0x11c = (float)arg[2]`; anders niets |
| overig | `0x453745` | `0x44e8f0` (FadeInst: 56/57, daarna basis) |

`0x452a50` (bericht 40): alleen als `+0x128 == 0`. Loopt de Npc-tabel `0x4c4e00[0x4c5318]` af tot de eerste met categorie 1 (`0x40c340`, = de Perso),
roept `Perso::0x465740(this)`; bij `true`: `this->vtbl[17]()` (Reset), `+0x130 = 0`, **`+0x128 = 1`**, `inst+8 |= 0x40` (niet-botsbaar), beide
geluidsbronnen geïnitialiseerd (`0x468e10`). Geen Perso gevonden of `false` ⇒ niets.

De klasse zet **geen msgmask-bits**, stuurt **geen VM-events**, raakt **HUD noch tekstvak** aan en roept de **CamMgr niet** aan (geen `0x443e50`,
`0x4c737c`, `0x456ed0` in `0x452850..0x4538ff`). De tekst "To take a ride…" is volledig script (§8).

## 3. Denk-stap `0x452e10` (jumptabel `0x453708` op `+0x128 − 1`)

Begin: `0x44e810` (fade + animatie-events, altijd). Stop als pauzebyte `[0x5e48cc]` of `+0x128 == 0`. `dt = [0x509adc]+0x38`. Als er een
uitlaatobject is: `exhaust+0xc = 1` ("teken mij dit frame") — dus in **elke** toestand ≠ 0. Einde (`0x4536ba`, elk frame): `0x468e50(&+0x190, this, 11, fx, −1.0)`
en `0x468e50(&+0x191, this, 15, fx, −1.0)` (lussen starten/stoppen naar gelang `0x468e40` dit frame is aangeroepen).

| toestand | code | type 20 | type 21 | overgang |
|---|---|---|---|---|
| 0 | — | rust (denk-stap doet niets) | idem | bericht 40 → 1 |
| **1** opstappen | `0x452e83` | `t += dt` | idem | `t ≥ +0x124` (0.83) → 2 (timer loopt door: niet gewist) |
| **2** doel bepalen | `0x452eb3` | §3.2; `speed = 0`, `t = 0` | idem | direct → 3 |
| **3** draaien | `0x45317d` | lus 15 actief; `t = min(t + dt, +0x10c)`; `rot = matrix(slerp(q0, q1, t / 2.0))` (`0x440dd0`, `0x440370`; CAMERA.md §3.4 voor de helpers) | idem | `t ≥ 2.0` → `t = 0`, 4, **SoundFx 16** (`0x45321a`, 3D op de instantie), `exhaust+8 = 1` (opstarten) |
| **4** ontsteking | `0x453239` | `t += dt` | idem | `t ≥ +0x110` (0.3) → `t = 0`, 5; type 20: **SoundFx 10** (`0x453468`); type 21: bom afvuren + **SoundFx 14** (§7) |
| **5** vlucht | `0x453472` | `Fly(dt)` (§3.3), lus 11 actief, `t += dt` | alleen `t += dt` (Fly en lus zijn type-20-only) | `t ≥ +0x114 − +0x118` → `t = 0`, 6 |
| **6** waarschuwing | `0x4534d4` | `Fly(dt)`, lus 11, `t += dt`, knippert rood (§5.2) | `t += dt` | `t ≥ +0x118` (1.0) → type 20: **SoundFx 6** (`0x45352c`) + `0x477060(1, &pos, 0)` (grote explosie, §5.3); beide: `t = 0`, 7 |
| **7** | `0x453555` | explosieschade (§4.3), `0x44e7f0(1.0, 1)` = transparantie én doel direct 1.0 (onzichtbaar), `t = 0` → **9** | `q0 = quat(rot)`, `q1 = quat(+0x140 startrotatie)`, `t = 0` → **8** | 1 frame |
| **8** terugdraaien | `0x453613` | (komt niet voor) | slerp in 2.0 s terug naar de startrotatie | `t ≥ 2.0` → `t = 0`, **0** (zonder Reset: `+0x168` en `0x40`-vlag blijven staan tot de volgende 40) |
| **9** weg | `0x453696` | `t += dt` | (komt niet voor) | `t ≥ 0.5` (`0x4a9014`) → `vtbl[17]()` Reset → 0 |

Totale tijdlijn type 20 vanaf bericht 40: 0.83 + (1 frame) + 2.0 + 0.3 = **3.13 s tot de start**, dan `+0x114` s vlucht, 0.5 s weg, daarna 1 s fade-in op de startplek.

### 3.2 Draaidoel (toestand 2, `0x452ebf..0x453178`)

```c
d  = -normalize(P0 - pos);                 /* P0 = TRAJ-punt 0; ×−1 = 0x4a9500; lengte 0 → niet genormaliseerd */
v  = (0, -1, 0);                           /* 0x43ff80(0, -1.0, 0) */
A  = normalize(cross(d, v));               /* 0x41af10: out = this × arg */
B  = normalize(cross(d, A));               /* = "omhoog" loodrecht op d:  normalize(up − d·(d·up)) */
A2 = normalize(cross(d, B));               /* = −A = cross(v, d) */
M  = rows { A2, d, B };                    /* esp+0xac.. ; rij 0 = model-X, rij 1 = model-Y, rij 2 = model-Z (INSTANCE.md: rotatie in rijen) */
q0 (+0x170) = quat(inst.rot);   q1 (+0x180) = quat(M);        /* 0x4404b0 */
```
Dus: **model −Y = vliegrichting**, model +Z = omhoog, model +X = Y × Z. Dat klopt met model 47: marker typecode 0 loopt van (0, −54.1, 23.2) naar (0, −154.1, 23.2)
(−Y = neus), uitlaat-marker typecode 9 van (0, 115.9, 23.2) naar (0, 155.9, 23.2) (+Y = staart), bbox x ±42, y −241..118, z −19..68. Het is dezelfde asconventie als de
Perso zelf (rij Y = −kijkrichting, PERSO_FRAME §2.4), daarom kan de Perso de matrix van de raket 1-op-1 overnemen.
Verticaal doel (d ∥ y): A heeft lengte 0 en blijft 0 ⇒ ontaarde matrix; komt in de data niet voor.

### 3.3 `Fly(dt)` `0x452cc0` (alleen type 20)

`dir = normalize(P0 − +0x134)` (**startpositie**, dus elke frame dezelfde richting; lengte ≤ 0 → ongenormaliseerd); `speed = min(speed + dt · +0x120, +0x11c)`
(`0x452d5f..0x452d88`); `pos += dir · speed · dt`; `inst+0x60..0x68 = pos`; `0x4077f0(this, 0)` (her-cellen). **Geen** straal-/boltest tegen wereld of instanties,
geen test op aankomst bij P0. Afgelegde weg na T s: `vmax²/4000 + vmax·(T − vmax/2000)`.

## 4. Opstappen, zitpositie, schade, reset

### 4.1 Zitpositie `0x452bd0(out)`

Type 20: altijd `GetVector(this, 0, v, 0)` → `out = v[0]` (beginpunt van de eerste marker-node typecode 0, wereldruimte, actuele pose; draait dus mee in toestand 3).
Type 21 (jumptabel `0x452c84`, bytes `0x452c8c` = 0,0,0,0,1,1,1): toestand 1..4 → marker 0; toestand 5..7 → als `+0x168`: **positie van de bom** (`bom+0xc`), anders `out`
onaangeroerd; overige toestanden: `out` onaangeroerd.

### 4.2 Wat de raket "ziet"

Niets: geen afstandstest tot de speler, geen botsing. Activeren gaat alleen via bericht 40 (script). De twee press-nodes van model 47 (nodes 2 en 3, typecode 0, geen
collision-id's in W1A) doen alleen mee aan de gewone instantiebotsing, en die staat uit (`0x40`) van bericht 40 tot de Reset.

### 4.3 Explosieschade (toestand 7, `0x453560..0x4535a9`)

Voor elke actor in lijst 1 `0x4c52d8[0x4c531c]` (paren van 8 B; geregistreerd via `0x40c080` — **alleen** door de Perso `0x44b6b0`, Boss2 `0x40dd58` en vijandtype 12 `0x4110e6`)
met categorie 2 of 1: `actor->vtbl[40](&this->pos, 600.0)` (`0x44160000`).
* Perso `vtbl[40]` = `0x44d040(pos, r)`: `|pos − Perso.inst.pos(+0xc)|² < r²` ⇒ richting `normalize_xz(Perso.pos − pos)`, `vtbl[39](0, 0, &richting, 0, 0)` (Hit met 0 schade:
  knockback/anim, PERSO_MOVE §4.4), rumble `0x44d1b0(+0x1d4, +0x1d0)`, **`vtbl[38](6)` = Kill(6)** (genegeerd als `+0x270 > 0` of al dood).
* Vijanden `vtbl[40]` = `0x41ae20` (ENEMY.md §7): binnen r direct dood — maar gewone vijanden staan niet in deze lijst, dus in de praktijk alleen Boss2/type 12.
* Type-17-breekbare objecten en chests 120/121 worden alleen door de **bom** geraakt (`0x44d650`: eigen lijsten, straal 400; BONUS.md §7), **niet** door de raket.
  `0x477060` zelf doet geen schade (PROJECTILES.md §5.3).

### 4.4 Reset `0x452ae0` (vtbl[17])

Positie, rotatie en centrum (`+0x60`) terug naar de startwaarden, `+0x128 = 0`, `0x4077f0(this, 0)`; als `+0x168` (type 21): `bom->vtbl[17]()` (bom deactiveren) en `+0x168 = 0`;
`inst+8 &= ~0x40`; **`+0x100 = 1.0`** (fade-snelheid 1/s) en `0x44e7f0(0, 0)` (doel 0, transparantie blijft): na een explosie (transparantie 1.0) wordt de raket in **1 s**
weer zichtbaar; alle `exhaust+0x2c[i] = 0` ("geen vorige markerpositie"). `+0x12c/+0x130` worden niet gewist (toestand 2 doet dat).
Let op: Reset wordt óók aan het begin van elke rit aangeroepen (`0x452a9c`), vóór `+0x128 = 1`.

## 5. Effecten en geluid

### 5.1 Uitlaat (marker typecode 9)

Tekenfunctie `0x475440`, lijst `[0x5e8564]`, beschreven in PROJECTILES.md §5.4 (gloed beeld 32 ×2, vlam beeld 31 als drie gekruiste quads, rook beeld 14 met 200 wolkjes/s,
bank 0). Verschillen voor de raket:
* groottetabel `0x4abcc8` met **index 6** ⇒ vlam **100** (+ `rand·10 − 5`), gloed 1 **70**, gloed 2 **60** (missile, index 3: 45 / 40 / 35).
* emitter-toestand `+8`: 0 tot het einde van het draaien; dan **1 = opstarten**: timer `+0x1c` loopt 1.0 s, schaalfactor `s` voor de groottes (`0x475560..0x475601`):
  `0 < τ < 0.15` → `τ·6.667`; `0.3 < τ < 0.45` → `(τ − 0.3)·6.667`; `0.85 < τ < 1.0` → `(τ − 0.85)·6.667`; anders 0 (drie "sputters"; `0x4aa1c8`, `0x4aab98`, `0x4ab79c`,
  `0x4aa3d8`, `0x4abd04`); bij `τ ≥ 1` → toestand **2 = aan** (`0x475542`). Omdat de ontsteking maar 0.3 s duurt valt de tweede en derde sputter al in de vlucht.
* getekend wordt alleen in frames waarin de denk-stap `+0xc = 1` zet (raket-toestand ≠ 0).

### 5.2 Rood knipperen (vtbl[26] `0x4537d0`)

Alleen type 20, toestand 6, niet gepauzeerd: `k = (int)(t · 20.0) & 1` (`0x4a9994`, `0x499580`). `[0x5ac850] = 1` (= **vermenigvuldig** de vertexkleur, `0x43bdfc`),
`[0x5ac860] = 0`, kleur `[0x5ac854..c]` = `k == 0` → **(1, 0, 0)**, anders (1, 1, 1). Dus 10 Hz rood/normaal gedurende de laatste seconde. Anders `[0x5ac850] = 0`.
(Correctie op LIGHTING.md §3: modus 1 = vermenigvuldigen, modus 2 = optellen, `0x43bdd3..0x43be1d`.)

### 5.3 Explosie `0x477060(1, &pos, 0)` (soort 1, `0x477089`)

Vier effectrecords (pool `[0x5e823c]+0xdb8`, 0x50 B, max 2000): `0x476b50` (0.2 s: 60 deeltjes/s ≈ 12 stuks, elk record `0x4767f0`, levensduur 2.0 s, willekeurige richting
`normalize(2r−1, 2r−0.5, 2r−1)`, startpunt `pos + richting·300`), `0x476cd0` (0.2 s: 400/s ≈ 80 stuks, record `0x4764f0`), `0x4762e0` met **R = 1400** (0.3 s) en — doorvallend
in soort 2 — `0x4762e0` met **R = 400** (0.3 s). `0x4762e0` = negen vlakke quads beeld 12, `size = R·(0.3 + 0.7·sin(u·π/2))`, `alpha = 0.3·cos(u·π/2)` (PROJECTILES.md §5.3).
De deeltjes-callbacks `0x4767f0` / `0x4764f0` zijn niet gelezen (onzeker 3); de port tekent voorlopig alleen de twee flitsen (1400 en 400) — sinds de missile-port wél
als de negen vlakke quads van `0x4762e0` zelf (`hud_world_fx_plane`), niet meer als één billboard met ×3 helderheid. Hetzelfde effect wordt door chests (`0x4517d0`)
en bommen gebruikt, dus hoort in een gedeelde effectfunctie: in `src/main_engine.c` is dat `blast_add` / `fx_smoke_draw`, die de raket en de missiles nu delen.

### 5.4 Geluiden (SoundFx-tabel SOUND.md §5; alle 3D op de raket-instantie)

| id | ref (Common-bank) | vol | wanneer | adres |
|---|---|---|---|---|
| 15 (lus) | 7 | 50 | tijdens het draaien (toestand 3); bron `+0x191` | `0x45317d`, `0x4536e8` |
| 16 | 8 | 50 | einde draaien = ontsteking | `0x45321a` |
| 10 | 10 | 50 | start (type 20) | `0x453468` |
| 14 | 9 | 52 | bom afgevuurd (type 21) | `0x45344a` |
| 11 (lus) | 5 | 50 | tijdens de vlucht (toestand 5 en 6, type 20); bron `+0x190` | `0x45349d`, `0x4534f5`, `0x4536ce` |
| 6 | 4 | 50 | explosie (type 20) | `0x45352c` |

### 5.5 Animaties van het raketmodel

Geen: de klasse start nooit een `.ins`-animatie (geen `0x436ca0`/PlayAnim-aanroep). Model 47 heeft één animatie van 10 s met constante sporen (alle nodes 1 sleutel);
draaien en vliegen gebeurt volledig op `inst.pos`/`inst.rot`.

## 6. Perso-kant

### 6.1 `0x465740(obj)` → bool

`+0x21c != 0` ⇒ false; `!onGround` (`0x44bcf0`) ⇒ false. Anders: `+0x6b0 = obj`, `SetState(8)` (`0x44c980`), `+0x5f0 = 0` (laatste aanvaller), `+0x5b4 = 0` (aanval-subtoestand),
`+0x6b4..0x6bc = pos (+0x1f4)`, `+0x6c0..0x6cc = quat(rot +0x28)` (`0x4404b0`), **`+0x6d0 = +0x6d4 = 0.7`** (`0x3f333333`), colliders `+0x298` en `+0x2bc` gereset (`0x436d10`).

Nieuwe Perso-velden: `+0x6b0` berijdbaar object, `+0x6b4` vec3 startpositie, `+0x6c0` quat startrotatie, `+0x6d0` resterende overgangstijd, `+0x6d4` overgangsduur.

### 6.2 Toestand 8 per frame `0x4657f0` (uit `0x44b822`; `doPost = false` ⇒ geen `0x44bb20` Perso_Move, geen `0x4624f0` MoveCollide, geen celwissel `0x462a40`)

1. **Aanhechten** (`0x465811..0x465966`): `+0x6d0 ≤ 0` ⇒ `pos = obj.Seat()` (`0x452bd0`), `rot = obj.rot` (`0x452ca0`). Anders `+0x6d0 −= dt` (`+0x2f8`); zolang
   `+0x6d0 ≥ +0x6d4 − 0.3` (`0x4aab98`): niets (Woody blijft staan, anim 0x3b begint); daarna `u = ((+0x6d4 − 0.3) − +0x6d0) / (+0x6d4 − 0.3)`,
   `rot = matrix(slerp(+0x6c0, quat(obj.rot), u))`, `pos = +0x6b4 + (Seat − +0x6b4)·u`. De **Perso volgt de raket** (positie én volledige 3×3), nooit andersom.
   `Perso_Orient` (`0x44bd30`) slaat toestand 8 over (`0x44bd4b`), `0x44bf10` kopieert de positie naar de instantie.
2. **Animatie** naar objecttoestand (jumptabel `0x465af0` op `obj+0x128 − 1`), verzoek aan controller `+0x494`:

   | objecttoestand | type 20 | type 21 | afspringen mag |
   |---|---|---|---|
   | 1 | **0x3b** | **0x36** | nee |
   | 2, 3 | **0x3c** | **0x37** | nee |
   | 4 | **0x3d** | **0x38** | nee |
   | 5 | **0x3e** | **0x39** | **ja** |
   | 6, 7 | (geen nieuw verzoek) | | **ja**, en elk frame kijkrichting zetten |
   | 0, 8, 9 | niets | | nee |

3. **Afspringen** (`0x465a0e..0x465a55`): `JustPressed(4)` (sprong) of `JustPressed(6)` (aanval) (`0x467420`) én "mag": `SetState(0)`, `Jumper_Reset` (`0x462c90`),
   `Jumper_ForceFall(J, 1)` (`0x463170`: Jumper-toestand 4 = vallen). Er is **geen** links/rechts/omhoog/omlaag-sturing: geen enkele `Held`/`Analog`-aanroep in de functie.
4. Na afspringen, of elk frame in objecttoestand 6/7: `Mover_SetFacing(M, normalize_xz(−rot.rij1))` (`0x465a5f..0x465ac5`, `0x459ff0`) = de horizontale vliegrichting.
5. Altijd: **`0x462760(p, 20.0)`** (`0x465ad2`): volumetest op `pos + (0, 20, 0)` (normaal 71.0, EVENTS.md §2) ⇒ triggervolumes (Enter/In/Leave, script 1020-afgronden!) werken door.

Logische animaties (tabel `0x4b6180`, record `{sub[4], prio, speed, restart}`; alle prio **1800**, speed **3.0**, restart 1; duur = `.ins`-duur / 3):

| log. | .ins-keten | duur (Woody-model, W1A) | gebruik |
|---|---|---|---|
| 0x3b | 84 | 2.6 s / 3 = **0.867 s** | type 20 opstappen (raket wacht 0.83 s) |
| 0x3c | 80 | 6.0 / 3 = **2.0 s** | type 20 zitten tijdens het draaien |
| 0x3d | 81 → 82 (lus) | 0.567 s, dan 0.2 s-lus | type 20 ontsteking (wordt na 0.3 s al vervangen door 0x3e) |
| 0x3e | 82 (lus) | 0.2 s | type 20 vlucht |
| 0x3f | 83 → 7 | 0.2 s | type 20 afstappen — **nergens aangevraagd** (geen `push 0x3f` met een animatie-aanroep in de exe) |
| 0x36 | 45 | 2.5 / 3 = 0.833 s | type 21 opstappen |
| 0x37 | 41 | 4.0 / 3 = 1.333 s | type 21 draaien |
| 0x38 | 42 → 43 (lus) | 0.333 s, dan 0.133 s-lus | type 21 ontsteking |
| 0x39 | 43 (lus) | 0.133 s | type 21 vlucht |
| 0x3a | 44 → 7 | 0.233 s | type 21 afstappen — nergens aangevraagd |

De wortelsporen van 80..84 bevatten zelf de zithouding (wortel ≈ (0, 5.75, 41.6), rotatie ≈ 63° om X), dus positie = marker, rotatie = raketmatrix is alles wat de port
hoeft te doen. `Perso_AnimState` (`0x463e60`, tabel `0x463f14` index 7) vraagt in toestand 8 zelf niets aan, tikt alleen de controllers.

### 6.3 Wat loopt er nog meer in toestand 8

* De stappen vóór de toestandsswitch (PERSO_FRAME §2.1) lopen door, maar doen niets: de aanval-trigger `0x457330` eist toestand 0 (`0x4573ad`), de aanval-controller
  `0x457a50` stopt bij `+0x5b4 == 0`, `0x44ba70` roept `0x463430` alleen in toestand 0.
* Timers, 25-bonussen-regel, `RegisterActor` (nodig: daardoor staat de Perso in de explosielijst), HUD-update: normaal.
* **Geraakt worden** tijdens de rit: `Hit` `0x44ca00` verandert toestand 8 niet (alleen 4 → 0, `0x44cbba`); knockback wordt gezet maar niet uitgevoerd (geen Perso_Move);
  health daalt gewoon; health 0 ⇒ `Kill(3)` ⇒ toestand 2, de raket vliegt leeg verder, ontploft en reset.
* **Dood** tijdens de rit (Kill(1) uit een afgrondvolume, of wat dan ook): toestand 2; `+0x6b0` blijft staan maar wordt niet meer gelezen. De raket krijgt niets te horen.
* Onkwetsbaarheid na de rit: geen. Wie binnen 600 van de explosie is sterft (tenzij `+0x270 > 0`).

## 7. Type 21: bomkanon (W2A 411, W2B 214/295/297/299/307/512, W2D 178; modellen 44 / 36 / 40)

Zelfde automaat, met deze verschillen (alle takken op `+0x164`):
* geen uitlaat, geen lussen-geluid 11, geen `Fly`, geen explosie, geen rood knipperen, geen toestand 9.
* Toestand 4 → 5 (`0x453279..0x45345a`): `m = GetVector(this, 0, ·, 0)`; `dir = normalize(m[1] − m[0])`; projectielblok `T` (PROJECTILES.md §1.1) = standaard-ctor inline,
  dan **sjabloon 0** (`0x449070(0, &T)`: de "gegooide bom", straal 30, schade 1000, onbeperkt stuiteren, onzichtbaar), daarna overschreven: `T.pos = m[0]`, `T.dir0 = dir`,
  **`T+0x1c` zwaartekracht = 0**, **`T+0x20` snelheid = `+0x11c`**, **`T+0x28` luchtdemping = 1.0**, **`T+0x2c` levensduur = `+0x114`**. `bom = 0x44d5d0(&T, 0, −1, 1)`
  (eerste vrije klasse-40-bom uit `0x5e4880[]`; `0x44d4d0`: lont `+0x10c = T.life`, `+0x110 = min(2.0, life)`, **niet** op de grond gezet, scriptvar −1, `+0x128 = 1`),
  `+0x168 = bom`, **`bom+0x133 = 1`** ("bereden": de bom blijft niet-botsbaar `0x44d894` en kan niet opgepakt worden `0x4634ab`), SoundFx 14.
  Geen vrije bom ⇒ `0x44d5d0` geeft NULL en de schrijfactie naar `NULL+0x133` crasht het origineel (levels hebben dus altijd bommen).
* Zitpositie in toestand 5..7 = positie van de bom ⇒ **Woody rijdt op de bom mee**; rotatie blijft die van het kanon. Afspringen zoals bij type 20.
* Toestand 7 → 8: kanon draait in 2.0 s terug naar `+0x140`, dan toestand 0 (geen Reset: de `0x40`-vlag en `+0x168` blijven tot de volgende bericht-40-Reset).
* De bom ontploft zelf (klasse 40, `0x44d6e0`, straal 400: type-17-objecten, chests **en** alle Npc's categorie 1/2 uit `0x4c4e00` via `vtbl[40](pos, 400)` ⇒ ook de Perso ⇒
  `Kill(6)` als Woody er nog op zit). Dit is het mechanisme waarmee in W2x muren/kisten worden opgeblazen.

Porten pas na het bomsysteem (TODO.md: klasse 40).

## 8. Scriptkant

### 8.1 Gebruik in alle levels (`tools/ekodisasm.py` over alle 28 `code`-bestanden; `1200 [inst, 20|21]`)

| level | inst | type | model | positie | TRAJ-punt 0 | 55 sub 1 (s) | 55 sub 2 (u/s) |
|---|---|---|---|---|---|---|---|
| **W1A** | 323 | 20 | 47 | (8845, 1137, 185) | (9306, 2065, −4104) | 3.80 | 1500 |
| **W1A** | 326 | 20 | 47 | (8138, 2014, −3080) | (8195, 2448, 656) | **4.20** | **1000** |
| WWS / KWS / SWS | 214 / 210 / 210 | 20 | 48 / 47 / 47 | (−4659, 654, 2129) | (−4654, 1202, −1448) | 2.50 | 1500 |
| K1A | 366 | 20 | 44 | (5401, 1855, −7678) | (−1722, 2006, −7679) (+1 punt) | 3.50 | 2500 |
| S1A | 419 / 425 / 483 | 20 | 42 | (8153, 2007, −3085) / (9432, 1887, −932) / (−1209, 1948, −7396) | (9788, 2058, −1027) / (7833, 2077, −461) / (5795, 2997, −7485) | 3.2 / 2.6 / 3.5 | 1000 / 800 / 2500 |
| W2A | 411 | 21 | 44 | (9882, 139, −12797) | (11263, 651, −17032) | 3.2 | 1300 |
| W2B | 214, 295, 297, 299, 307, 512 | 21 | 36 | — | — | 1.5..4.0 | 1200..1500 |
| W2D | 178 | 21 | 40 | (−11096, 3524, −2405) | (−9905, 4325, 1118) | 2.75 | 1500 |

Alleen punt 0 van het TRAJ wordt gelezen; een tweede punt (vaak ≈ de startpositie) is ongebruikt. Elke instantie krijgt in alle levels alleen **1200, 55, 55 en 40**
(geen 29, geen 56/57, geen MSGTEST).

### 8.2 W1A script-objecten (`out/w1a_code.txt`; 0x1000143 = inst 323, 0x1000146 = inst 326)

Object 323 (init): `SEND 1200 [323, 20]`, `SEND 55 [323, 1, 380]`, `SEND 55 [323, 2, 1500]`. Object 326: `1200 [326, 20]`, `55 [326, 1, 420]`, `55 [326, 2, 1000]`.
Geen per-frame-code.

Object **324** (= instantie 324, model 29, volumedrager op (8845, 1260, 238); **volume 61** = kubus van 400: x 8645..9045, z 38..438, y-band van 400 rond de raket):
```
init:  var62 = 0; var63 = 1; var64 = 1; var65 = 1
frame: if VOL_FLAG5(61)                      SEND 1050 [var62, 2]          ; Perso in het volume: var62 = "aanval net LOSGELATEN" (GAMEFLOW.md 1048..1050)
       if var62 == 1 && var63 == 1 {         var63 = 0;  DELAY 300 { var63 = 1 }          ; 3 s herhaalblokkade
                                             SEND 40 [323];  var62 = 0 }
       if VOL_FLAG5(61) && var64 == 1 && var65 == 1 {  var64 = 0; var65 = 0;
                                             SEND 1080 [0, 2, var64, str 124, str 125, str 126] }   ; "To take a ride on the rocket / stand next to it and / press the ACTION button"
       if var64 == 0 && VOL_FLAG3(61) {      var64 = 1;  DELAY 60 { var65 = 1 } }         ; bij verlaten: tekstvak dicht (var != 0, HUD_TEXT.md §3), na 0.6 s weer toegestaan
```
Object **327** (instantie 327, model 29 op (8150, 2095, −3178); **volume 62**: x 7950..8350, z −3378..−2978): identiek zonder tekstvak: `1050 [var66, 2]`, `var67`-blokkade 3 s,
`SEND 40 [326]`.

Verder verwijst **niets** in W1A naar 323/326 (geen 29, geen camera, geen MSGTEST, geen deur/muur): de enige treffers op `0x1000143`/`0x1000146` zijn de zes regels hierboven
en `PUSH 323`/`PUSH 326` in objecten 324/327.

### 8.3 Vluchten in W1A (uit §3.3; afgelegde weg = `vmax²/4000 + vmax·(T − vmax/2000)`)

| raket | richting (genormaliseerd) | afstand tot P0 | vliegweg | explosiepunt | rol |
|---|---|---|---|---|---|
| 323 | (0.104, 0.210, −0.972) | 4412 | 562 + 4575 = **5137** in 3.8 s | ≈ (9379, 2216, −4808) | omhoog (+930) en 4300 naar −z |
| 326 | (0.015, 0.115, 0.993) | 3762 | 250 + 3700 = **3950** in 4.2 s | ≈ (8197, 2468, 842) | terug naar +z, +430 omhoog |

De raket passeert P0 dus 0.48 s (323) resp. 0.19 s (326) vóór de explosie; het knipperen begint 1.0 s ervoor. De speler moet rond P0 afspringen en is dan nog
> 600 van het explosiepunt verwijderd bij 323 (725), maar **niet** vanzelf bij 326 (190 + wat hij zelf nog aflegt): daar moet hij eerder springen. De raket "opent" niets.

## 9. Correcties op eerdere documenten

* OBJECTS.md §2.6: "spawnt zelf een projectiel/bom" geldt alleen voor **type 21**; "raakt actors" = alleen de geregistreerde lijst `0x4c52d8` (in W1A: de speler zelf).
  De parameters van 326 zijn `55 [.,1,420]` en `55 [.,2,1000]`, niet 380/1500.
* PERSO_FRAME §3: toestand 8 = "berijdt object `+0x6b0` (klasse 20/21)", niet "rail?".
* LIGHTING.md §3: `[0x5ac850] == 1` vermenigvuldigt de vertexkleur met `[0x5ac854..c]`, `== 2` telt op (§5.2).
* BONUS.md §7: de bomexplosie `0x44d650` loopt óók de Npc-tabel `0x4c4e00` af (categorie 1/2, `vtbl[40](pos, 400)`), niet alleen type 17 + chests.

## 10. Onzeker / niet nagelopen

1. **Asrichting van de startoriëntatie**: met de quaternion uit de `.ins` "zoals opgeslagen" wijst de neus (−Y) van 323 naar (0, −0.38, +0.92), dus wég van het doel en de raket
   draait in toestand 3 ruim 150°; met de geconjugeerde lezing wijst hij naar (0, −0.38, −0.92). Welke klopt volgt uit de bestaande instantie-conventie van de port
   (`inst->quat`); niet visueel geverifieerd. De doel-matrix van §3.2 is wel eenduidig (rijen = modelassen).
2. Het kanaal van de knipperkleur: `[0x5ac854]` is de eerste component van de vertexkleur `+0x24`; aangenomen rood (vertexkleuren zijn R,G,B).
3. Explosie soort 1: de deeltjesrecords `0x4767f0` (12 stuks, 2 s) en `0x4764f0` (80 stuks) zijn niet gedecompileerd (beeldnummers, groottes, zwaartekracht onbekend);
   alleen de emitters en de twee flitsen (R 1400 / 400, beeld 12) zijn zeker.
4. Camera: toestand 8 zet alleen de behind-vlag (`0x4591ec`); de opgeslagen richting is rij 1 van de raketmatrix en heeft hier een **y-component**. Hoe `0x424760` daarmee
   omgaat (negeert y of kantelt de camera mee) is niet nagelopen. Er zijn geen aparte afstanden/hoogtes voor de rit.
5. De snelheid na het afspringen: Mover-ramp A wordt nergens gewist of gezet; aangenomen ≈ 0 omdat de speler stilstond/aanviel bij het opstappen. Een stormloop (aanval
   losgelaten = ook de trigger van de stormloop, PERSO_JUMP §2.2) kan in hetzelfde frame gestart zijn vóór bericht 40 aankomt; `0x465740` wist `+0x5b4`, maar of de
   ramp dan nog 700 u/s bevat bij het afspringen is niet getest.
6. Uitlaat-toestand `+8` wordt door Reset niet teruggezet naar 0: bij een **tweede** rit brandt de uitlaat (toestand 2) al vanaf het opstappen. Zo staat het in de code
   (`0x452baa..0x452bcb` wist alleen `+0x2c[]`); of dat in het spel zichtbaar is, is niet geverifieerd. De port mag `exhaust = 0` zetten in de Reset.
7. De rook-/vlamdetails van `0x475440` zijn overgenomen uit PROJECTILES.md §5.4 ("op hoofdlijnen gelezen"); alleen de index-6-groottes en de opstart-ramp zijn hier nagelopen.
8. Type 21: niet dieper gelezen dan §7 (bom-update `0x44d870`, wat er met de Perso gebeurt als het kanon al in toestand 8/0 is terwijl hij nog in toestand 8 zit —
   vermoedelijk is de bom dan al ontploft en is hij dood of afgesprongen).
9. Blijven zitten zonder te sterven (`+0x270 > 0`, cheat): het origineel blijft dan in toestand 8 hangen op de gerespawnde raket (objecttoestand 0/9 ⇒ geen uitgang). Zie de
   port-afwijking in §0.4.
10. Alles is statische analyse; niets is met `tools/wtrace.py` tegen het draaiende origineel gecontroleerd (tijden 0.83 / 2.0 / 0.3 s zijn goed te meten aan geluid 16 en 10).
