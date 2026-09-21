# Instantie-basisklasse: animatiesysteem, generieke berichten, beweging

Bron: disassembly van `game/Woody.exe` (imagebase 0x400000). Alle adressen zijn VA's.
Basisklasse: ctor `0x42e1a0`, vtable `0x4aa31c`, berichthandler `0x42d5e0` (vtable[22]).
Globale tijd `now` = float `[[0x509adc]+0x30]` (seconden); framenummer = `[[0x509adc]+0]`.
Berichtrecord `rec` (edi in de handler): `rec[0]` = id, `rec+8` = instantie, `rec+0xc` = arg1,
`rec+0x10` = arg2, `rec+0x14` = arg3, `rec+0x18` = arg4, `rec+0x1c` = arg5 (alles i32).
Returnwaarde van de handler: `al = 0` = verwerkt, `al = 1` = "nog niet, later opnieuw aanbieden" (alleen 12/13).

Constanten (uit de exe gelezen): `0x4aa138` = 1/4096 (0.000244140625), `0x4a94f8` = `0x4aa0ac` = 0.01,
`0x4a94c4` = 0.001, `0x4a9004` = 0.0, `0x4a900c` = 1.0, `0x4a9864` = 15.0, `0x4a9014` = 0.5, `0x4aa38c` = 0.98.

Animatietabel van het model: `S = inst+0xf8`, `S+4` = hoogste geldige anim-index (test `anim > S[4]` → fout
"L'anim %d n'existe pas dans cet objet..." `0x42df0e`), `S+8` → array van 8 bytes per animatie:
`{i32 nframes, i32 duration}`. Hieronder: `L(a) = S->anim[a].duration / 4096.0` (seconden op snelheid 1).

## 1. Animatiesysteem

### 1.1 Velden
| veld | type | betekenis | bewijs |
|---|---|---|---|
| +0x58 | u32 | framenummer van de laatste klok-update (klok draait max. 1× per frame) | `0x43eeee` |
| +0x5c | u32 | index van de eerste wereldmatrix van deze instantie in `[0x509adc]+0xa0` | `0x43f2bc` |
| +0x60..0x68 | vec3 | wereldpositie van de laatste niet-0x80-node (na animatie), gebruikt voor cel-herbepaling `0x4077f0(&+0x60)` | `0x43f33a` |
| +0x88 | i32 | status; de klok zet hem op 0 zodra snelheid ≠ 0, behalve als hij 2 is | `0x43ef8c`, `0x42d64d` |
| +0x9c | i32 | "einde bereikt in deze update": 0 aan het begin van elke lopende update, 1 zodra fase buiten [0,1) komt (wrap of einde). Berichten 1/2/3(n>10)/4 zetten hem op 0. 12/13 wachten tot hij ≠ 0 is | `0x43efb7`, `0x43f0c9`, `0x42d619` |
| +0xa0 | f32 | snelheid (teken = richting; 0 = stilstaan op `+0xac`) | `0x42e290` |
| +0xa4 | f32 | basissnelheid; `0x42e290(v)` zet +0xa0 = +0xa4 = v; bij slotwissel wordt +0xa0 = +0xa4 | `0x43f191` |
| +0xa8 | f32 | starttijd (s) van de huidige doorloop | |
| +0xac | f32 | huidige positie in de animatie in seconden-op-snelheid-1 = fase · L(slot0); bij snelheid 0 is dit de BRON van de fase | `0x43f208`, `0x43f227` |
| +0xb0 | i32 | slot0 = huidige animatie | |
| +0xb4/+0xb8/+0xbc | i32 | slot1..3 = wachtrij (−1 = leeg) | |
| +0xc0 | i32 | 1 als slot0 == slot1 (dus "loopt in een lus"), anders 0; ctor −1. GEEN "klaar"-vlag | `0x43f246` |
| +0xd0 | u32 | `(1 << round(min(fase,1)·15)) << 16`: fasemasker voor de renderer | `0x43f285` |

Ctor-init `0x42e250`: snelheid 0, +0xc0 = −1, slot0 = 0, slot1..3 = −1. `0x42e210`: +0x84 = −1,
`+0xd8 &= 0xc0`, +0x6c = 0, +0x7c = +0x80 = 0, +0xc4 = −1, +0xd4 = 0.

### 1.2 Klok `0x43eee0(this, ext)` — pseudo-C
```c
void anim_clock(Instance *I, void *ext) {
    if (I->last_frame == g->frame) return;  I->last_frame = g->frame;
    if (I->traj) traj_update(I->traj, I);              // 0x437da0, zie §3
    else if (ext) { /* pose van buitenaf: +0xd0 = ext[0]; 0x43f360 kopieert matrices */ return; }
    M = diag(scale +0x4c..0x54) * rot(+0x28..0x48); T = pos(+0xc..0x14);
    float phase;
    if (I->speed != 0) {
        if (I->status88 != 2) I->status88 = 0;
        I->ended = 0;                                   // +0x9c
        float L = S->anim[I->slot[0]].duration / 4096.0f;
        if (I->speed > 0) phase = (now - I->start) / (L / I->speed);
        else              phase = 1.0f - (now - I->start) / -(L / I->speed);
        if (!(phase >= 0 && phase < 1)) {               // 0x43f0c3
            I->ended = 1;
            if (I->slot[1] != -1) {                     // doorschakelen naar wachtrij
                float ratio = (float)dur[slot0] / (float)dur[slot1];
                float n = 0;
                if (phase < 0)  { n = -floor(phase); phase += n; }
                if (phase >= 1) { n =  floor(phase); phase -= n; }
                phase *= ratio;                          // resttijd omgerekend naar de nieuwe animatie
                if (n != 0) I->start += n * (L / fabs(I->speed));
                I->speed = I->base_speed;                // +0xa0 = +0xa4
                slot[0]=slot[1]; slot[1]=slot[2]; slot[2]=slot[3];   // slot[3] blijft staan
            } else if (I->speed < 0) { if (phase < 0) { set_speed(I,0); phase = 0; } }
            else                     { if (phase > 1) { set_speed(I,0); phase = 1; } }
        }
        I->pos_ac = (S->anim[I->slot[0]].duration / 4096.0f) * phase;
    } else {
        phase = I->pos_ac / (S->anim[I->slot[0]].duration / 4096.0f);
    }
    I->c0 = (I->slot[0] == I->slot[1]);
    float frame = S->anim[I->slot[0]].nframes * phase;   // NIET geklemd, gaat naar 0x43a2b0
    I->d0 = (1 << (int)(min(phase,1) * 15 + 0.5)) << 16;
    I->mat_index = g->nmat;  eval_tracks(S->nodes, S->nnodes, &M, frame, I->slot[0]);  // 0x43a2b0
    g->nmat += S->nmat;                                  // S+0x64
    if (!(I->flags8 & 0x20)) { I->center = wereldpositie laatste niet-0x80-node; recell(I, &I->center); } // 0x4077f0
}
```
Gevolgen:
- **Einde van een eenmalige animatie** (slot1 == −1): fase wordt op 1 (vooruit) of 0 (achteruit) geklemd, snelheid → 0,
  `+0xac` = L resp. 0. Het laatste (eerste) frame blijft dus staan; er is geen terugval naar een andere animatie.
- **Lus** = dezelfde animatie in slot0 en slot1 (bericht 4 vult alle vier). Bij het doorschakelen wordt `start`
  met een geheel aantal doorlopen opgeschoven, dus de lus is naadloos en tijd-exact.
- Er bestaat **geen ping-pong**; achteruit = negatieve snelheid (fase loopt 1 → 0).
- De klok wordt aangeroepen vanuit de render/update `0x42e2b0` als bit 0 van diens argument gezet is (`0x42e305`).

### 1.3 PlayAnim-berichten (allemaal in `0x42d5e0`)
`spd(a, dur) = L(a) / (dur·0.01)`: `dur` is de gewenste duur van één doorloop in 1/100 s; snelheid is dus
"animatielengte / gewenste duur" en één doorloop duurt `L/spd = dur·0.01` seconden.

| id | args | adres | werking |
|---|---|---|---|
| 1 | anim, t | `0x42de97` | bereikcheck; `set_speed(0)`; `+0xac = L(oude slot0) · t·0.001`; slot0 = anim; slot1..3 = −1; +0x9c = 0. Dus: zet stilstaand beeld op positie t (let op: L van de VORIGE slot0 wordt gebruikt, en de schaal is 0.001) |
| 2 | anim, flag, dur, off | `0x42de16` | slot0 = anim; `set_speed(spd(anim,dur))`; `start = now − off·0.001·dur·0.01` (begin op fractie off/1000); flag == 0 → `set_speed(−speed)` (achteruit); slot1..3 = −1; +0x9c = 0. Eenmalig |
| 3 | anim, flag, dur | `0x42d62d` | zie hieronder |
| 4 | anim, flag, dur | `0x42d8e7` | slot0..3 = anim (lus); `set_speed(spd(anim,dur))`; start = now; flag == 0 → snelheid negatief; +0x9c = 0 |
| 5 | – | `0x42d606` | `set_speed(0)`: bevriest op huidige `+0xac` |
| 12 | als 3 | `0x42d619` | als +0x9c == 0 → return 1 (opnieuw proberen); anders als 3 |
| 13 | als 4 | `0x42d8d3` | als +0x9c == 0 → return 1; anders als 4 |

`flag`: **1 = vooruit, 0 = achteruit** (geen loop-vlag: lus of eenmalig wordt bepaald door het bericht-id: 4/13 = lus, 2/3/12 = eenmalig).

Bericht 3 (`0x42d62d`), pseudo-C:
```c
if (anim < 0 || anim > S->max_anim) { warn; return 0; }
if (dur <= 10) {                       // "spring direct naar begin/eind" — anim-argument wordt NIET in slot0 gezet
    if (I->status88 != 2) I->status88 = 0;
    set_speed(I, 0);
    I->pos_ac = flag ? L(I->slot[0]) : 0;          // flag=1 → laatste frame, flag=0 → eerste frame
    return 0;                                       // slots en +0x9c ongewijzigd
}
if (I->slot[0] == anim && I->speed != 0) {         // zelfde animatie loopt al: keer om / ga verder vanaf huidige fase
    float L = L(anim);
    if (I->speed > 0) {                            // loopt vooruit
        if (flag != 0) return 0;                   // al vooruit → niets doen
        float p = (now - I->start) / (L / I->speed);  p = 1 - (p - floor(p));   // resterende fractie
        set_speed(I, L / (dur*0.01f));
        I->start = now - (L / I->speed) * p;
        set_speed(I, -(L / (dur*0.01f)));          // nu achteruit vanaf dezelfde pose
    } else {                                       // loopt achteruit
        if (flag != 1) return 0;
        float p = (now - I->start) / -(L / I->speed);  p = p - floor(p);
        set_speed(I, L / (dur*0.01f));
        I->start = now - (L / I->speed) * (1 - p);
    }
    return 0;                                      // slots ongewijzigd
}
I->slot[0] = anim; set_speed(I, L(anim)/(dur*0.01f)); I->start = now;
if (flag == 0) set_speed(I, -I->speed);
I->slot[1] = I->slot[2] = I->slot[3] = -1;  I->ended = 0;
```
Bericht 3 is dus de "deur"-variant: eenmalig afspelen, en bij herhaald sturen met omgekeerde flag draait de lopende
animatie om vanaf de huidige pose (deur die halverwege weer dichtgaat).

## 2. Textuuroverrides per instantie: berichten 15..19 (struct op `inst+0xd8`)

De 15..18-"overgangen" uit MESSAGES.md zijn **textuuranimatie-overrides**. De polygoonrenderer `0x43b3f0`
roept per getextureerd polygoon `0x47f290(this = materiaal->textuur, &inst+0xd8, out, materiaal, now)` aan (`0x43c339`).

| veld | gezet door | betekenis |
|---|---|---|
| +0xd8 bits 0-2 | 16 / 18 | frame-modus B (0 = uit) |
| +0xd8 bits 3-5 | 15 / 17 | UV-scrollmodus A (0 = uit) |
| +0xd8 bits 6-7 | – | blijven behouden (19 en `0x42e210` doen `&= 0xc0`) |
| +0xd9 | 16/18 arg1 (byte) | in W1A altijd 0xff; niet gelezen in `0x47f290` (open) |
| +0xda | 15/17 arg1 (byte) | idem |
| +0xdc | 16/18 | starttijd B = now |
| +0xe0 | 16/18 arg3·0.01 | factor op de textuurduur: periode `P = tex+0x54 · (+0xe0)` |
| +0xe4 | 15/17 | starttijd A = now |
| +0xe8 | 15/17 arg3·0.01 | factor op de scrollsnelheid (`tex+0x48`, `tex+0x4c`) |
| +0xec | 15 arg4·0.01 | duur T2 (s) van de eindige scroll |

Frame-modus B (alleen als `tex+0x58` (frame_count n) ≠ 1; t = now − (+0xdc); jumptabel `0x47f604`):
| modus | bericht (arg2) | frame |
|---|---|---|
| 1 | 16 (1) | eenmalig vooruit: t ≥ P → n−1, anders `(int)(t/P·n)` |
| 2 | 16 (0) | eenmalig achteruit: t ≥ P → 0, anders `(int)((1−t/P)·n)` |
| 3 | 16 (2) | eenmalig heen-en-terug: t ≥ P → 0, anders k = `(int)(t/P·(2n−1))`, k ≥ n → k = 2n−1−k |
| 4 | 18 (1) | lus vooruit: `(int)(frac(t/P)·n)` |
| 5 | 18 (0) | lus achteruit: `(int)((1−frac(t/P))·n)` |
| 6 | 18 (2) | lus heen-en-terug: k = `(int)(frac(t/P)·(2n−1))`, gespiegeld als k ≥ n |
(`(int)` = `0x499580` ftol met de FPU-afronding; frames zijn 0x74 bytes per stuk vanaf de textuur.)
Zonder override (bits 0-2 = 0) geldt de normale globale textuuranimatie.

UV-scrollmodus A (alleen als `tex+0x48` of `tex+0x4c` ≠ 0; su/sv = tex-snelheid · (+0xe8); t = now − (+0xe4)):
modus 1 (15, arg2=1): offset += frac(su·min(t,T2)); modus 2 (15, arg2=0): offset −= …; modus 4 (17, arg2=1): eindeloos
vooruit offset += frac(su·t); modus 5 (17, arg2=0): eindeloos achteruit. De offset gaat in `materiaal+0xc` (u) en `+0x1c` (v);
de aanroeper zet na het tekenen de oude waarden terug (`0x43c369`).
Bericht 19 (`0x42db86`): `+0xd8 &= 0xc0` = beide overrides uit.

W1A-voorbeeld: `16 [inst, 0xffff, 1, 0x28]` = speel de textuurframes eenmalig vooruit in 0.4 × de textuurduur;
`18 [inst, 0xffff, 1, 0x64]` = lus vooruit op normale snelheid.

## 3. Padvolger (TRAJ op `inst+0x78`): berichten 42, 43, 44, 46 — de enige engine-gestuurde verplaatsing in de basisklasse

TRAJ-object `T` (zie FORMAT_INS §3): `T+0` = vlaggen|npoints, `T+4` = starttijd, `T+8` = omlooptijd (s), `T+0xc` = totale
padlengte (`0x437ca0`, som van de npoints−1 segmenten; die functie wist ook bits 17, 21, 22), `T+0x10` → punten (16 B: unk, x, y, z).

| bit in `T+0` | betekenis | gezet door |
|---|---|---|
| 0-15 | npoints | loader |
| 16 | "closed" uit het bestand (door de padvolger NIET gebruikt: er is geen sluitsegment) | loader |
| 17 (0x20000) | actief | 42/43 zetten, 44 (`0x437d90`) wist, einde van niet-lus wist |
| 18 (0x40000) | achteruit (arg `a ≠ 1`) | 42/43 |
| 19 (0x80000) | lus | 43 altijd; 42 wist hem |
| 20 (0x100000) | ping-pong (richting keert om bij oneven omloopnummer) | 43 als `c == 1`; 42 laat hem ongemoeid |
| 21 (0x200000) | oriënteer instantie langs het pad | 46 `a == 1` (`0x4381e0`) |
| 22 (0x400000) | oriëntatie omgekeerd | 46 `b == 1` |

| id | args | adres | werking |
|---|---|---|---|
| 42 | a, f | `0x42dcb2` → `0x437d10(a≠1, f·0.01)` | eenmalig pad afleggen in f/100 s; start = now; daarna `inst.pos` = eerste punt (a == 1) of laatste punt (a ≠ 1) en `0x4077f0(0)` (cel opnieuw bepalen) |
| 43 | a, f, c | `0x42dcdd` → `0x437d50(a≠1, f·0.01, c==1)` | als 42 maar in een lus (omlooptijd f/100 s), c == 1 = heen-en-weer |
| 44 | – | `0x42dd9a` → `0x437d90` | stop (bit 17 wissen); positie blijft staan |
| 46 | a, b | `0x42dc7d` → `0x4381e0` | oriëntatievlaggen |
Alle vier doen niets als `inst+0x78 == NULL`.

Update `0x437da0(T, inst)`, aangeroepen aan het begin van de animatieklok (`0x43ef05`), dus 1× per frame dat de instantie
wordt bijgewerkt:
```c
if (!(T->flags & ACTIVE)) return;
float t = now - T->start, frac; double ip = 0;
if (!(T->flags & LOOP) && t > T->dur) { frac = 1; T->flags &= ~ACTIVE; }
else frac = modf(t / T->dur, &ip);                       // 0x49a23b
bool fwd = !(T->flags & REVERSE);
if ((T->flags & PINGPONG) && ((int)ip & 1)) fwd = !fwd;
float d = frac * T->length;  int i0 = -1, i1 = -1;
if (fwd) for (i = 0, acc = 0, rem = d; i < n-1; i++) { len = |P[i+1]-P[i]|; acc += len; if (d <= acc) { pos = P[i] + unit(P[i+1]-P[i]) * rem; i0=i; i1=i+1; break; } rem -= len; }
else     for (i = n-1, acc = 0, rem = d; i > 0; i--) { len = |P[i-1]-P[i]|; acc += len; if (d <= acc) { pos = P[i] + unit(P[i-1]-P[i]) * rem; i0=i; i1=i-1; break; } rem -= len; }
// geen treffer (d voorbij het eind door afronding) -> positie blijft ongewijzigd, i0 = i1 = -1
inst->pos = pos;                                          // +0xc..0x14
if ((T->flags & ORIENT) && i0 != -1) {
    vec3 dir = (T->flags & ORIENT_REV) ? P[i1]-P[i0] : P[i0]-P[i1];  dir.y = 0; normalize(dir);
    row0 = cross(dir, (0,1,0)); row1 = dir; row2 = (0,1,0);           // -> inst+0x28, +0x34, +0x40 (0x41af10)
}
```
Eenheden: tijd in 1/100 s, posities zijn de padpunten uit het `.ins` (wereldcoördinaten). Geen versnelling/easing.
De test is `d <= cumulatieve lengte` (`0x437ecb` / `0x438037`); de rest `rem` wordt apart bijgehouden op de FPU-stack. Netto: constante
snelheid langs de booglengte, lineair per segment. `inst->pos` wordt alleen bij een treffer geschreven.

W1A: `43 [inst, 1, 2500, 0]` = lus van 25 s vooruit; type 51: `43 [inst, 1, 550, 1]` = heen-en-weer, 5.5 s per enkele reis.

## 4. Bericht 6 (tonen/verbergen) en de wereldcel

`0x42d985`: `on ≠ 0` en `inst+0x1c < 0` → `0x407790(0)`; `on == 0` en `inst+0x1c ≥ 0` → `0x407850()`.
- `0x407850`: haalt de instantie uit de enkelvoudig gelinkte lijst van zijn cel (`cel+0x44` = kop, `inst+0x24` = volgende),
  zet `+0x24 = 0`, `+0x1c = −1`, `+0x18 = −1`.
- `0x407790(p)`: alleen als `+0x1c == −1`: `+0x1c = 0x4081c0(world, p ? p : &inst.pos)` (cel zoeken), `+0x18 = 0x40a0c0(p, −1)`,
  en als de cel ≠ −1: vooraan in de cellijst hangen.
- `0x4077f0(p)` = verbergen + opnieuw tonen (her-cellen na verplaatsing).
De renderer/updater `0x42e2b0` keert direct terug als `+0x1c == −1` (`0x42e2c3`): geen klok, geen tekenen, geen volume-/pressupdate.
Alles wat per cel over `cel+0x44` loopt (tekenen, botsing van de speler tegen hulls) ziet een verborgen instantie dus niet meer.
Verborgen = onzichtbaar EN zonder botsing. Animatietijd loopt wel door (de klok rekent met absolute tijd).

## 5. Berichten 56 / 57: transparantie-fade (`+0x6c`)

`+0x6c` = **transparantie** (0 = normaal, 1 = weg). Lezers:
- `0x43b504` (polygoonrenderer): `alpha = (1 − [+0x6c]) · 255`; als alpha < 252 → transparante tekenmodus.
- `0x42e374` (`0x42e2b0`): `+0x6c > 0.98` → instantie wordt helemaal niet getekend.
- `0x42e2cc`: alleen bij `+0x6c < 0.01` wordt de vroege zichtbaarheidstest `0x42f3d0` gedaan; `0x42e69a`/`0x42eb7a`: bij `> 0.01`
  wordt in de extra tekenpas (zie §6, bit 1) de vlakkleur met `+0x6c` vermenigvuldigd (pad `0x4388e0` i.p.v. `0x4385f0`).

Basisklasse (`0x42de00`, ook types zonder SetTypeInstance): **56: `+0x6c = v·0.01` direct**; 57 bestaat niet (default, genegeerd).

Klassen met tussenhandler `0x44e8f0` (type 70 = vtable `0x4a9124`, en alle klassen die naar `0x44e8f0` doorvallen: 17, 20/21, 30-38, 40,
42, 50-52, 80, 120/121) hebben twee extra velden; init `0x44e7c0`: `+0x100 = 100.0`, `+0xfc = 0`, `+0x6c = 0`:
- **56** (`0x44e91b` → `0x44e7f0(v·0.01, 0)`): `+0xfc` (doel) = v·0.01; `+0x6c` zelf blijft staan (2e arg 0; met 1 zou hij direct gezet worden).
- **57** (`0x44e907`): `+0x100` (fade-snelheid per seconde) = v·0.01.
- per frame (vtable[3] van type 70 = `0x44e810`; niet als byte `[0x5e48cc]` ≠ 0):
```c
float old = I->fade;                                    // +0x6c
if (I->fade < I->target)      { I->fade += dt * I->rate; if (I->fade > I->target) I->fade = I->target; }   // dt = [0x509adc]+0x38
else if (I->fade > I->target) { I->fade -= dt * I->rate; if (I->fade < I->target) I->fade = I->target; }
if (I->fade > 0.9f && old <= 0.9f) I->flags8 |= 0x40;   // niet-botsbaar (zie PERSO_MOVE: +8 & 0x40)
if (I->fade < 0.9f && old >= 0.9f) I->flags8 &= ~0x40;
anim_events(I);                                         // 0x42f5e0
```
Let op: de andere klassen hebben een eigen vtable[3]; of die `0x44e810` aanroepen is per klasse na te gaan (open). Voor type 70 is het bewezen.
De standaardsnelheid 100/s maakt een 56 zonder 57 praktisch direct. W1A: `56 [inst,100]` + `57 [inst,800]` = uitfaden naar onzichtbaar
met 8/s (⅛ s); de 180 berichten 56 in 6 s zijn het script dat type-70-objecten op afstand in- en uitfadet.

Type-tabel-correctie t.o.v. classmap_raw.txt (ctor-inlining in `0x403440`): type 41 → vtable `0x4a9360` (`0x403b5d`), type 60 →
`0x4a9194` met eigen handler `0x474a40` (`0x403ca3`), **type 70 → `0x4a9124`, handler `0x44e8f0`, update `0x44e810`** (`0x403cea`),
type 90 → `0x4a90ac`, basishandler, eigen vtable[3] `0x472560` (`0x403d56`).

## 6. Bericht 45: SetFlags (`+0xf0 |= bits & 0x23`, `0x42ddb4`; alleen zetten, nooit wissen; loader zet +0xf0 = 0 op `0x428711`)

| bit | lezers | effect |
|---|---|---|
| 1 | `0x42b3d6` (wereld-tekenlus `0x42b380`) | instantie van soort `(+8 & 0x1f) == 1` wordt met `vtable[2](7)` i.p.v. `(5)` getekend als de detailoptie `[0x4c2c0c]` ≠ 0: extra pas (arg-bit 2) in `0x42e2b0` tegen de vlakkentabel `[0x4c4cac]+4` (64 B/entry: vlak, kleur +0x30, object +0x3c) – schaduw of spiegeling, niet verder uitgezocht |
| 1 | `0x42efd7` | verbiedt het cachen van de bounding-sphere (`+0x88 = 1`, `+0x8c..+0x98`) voor stilstaande instanties |
| 2 | geen lezer gevonden | – |
| 0x20 | `0x43b423` (polygoonrenderer) | **de zwarte contourlijn**: alleen als `[0x4c2c0c] == 2` (detailoptie uit `Woody.cfg`, in de meegeleverde cfg 2) wordt na het model de achterkant nog eens getekend, elke vertex naar buiten geschoven langs zijn normaal met `w = afstand/300`, boven 2.5 aflopend als `5 − afstand/300` en boven 1500 eenheden helemaal weg (`0x43b4ce..0x43b4f3`). Zie MODEL_RENDER.md §11 |
Alle drie zijn puur visueel; voor een herimplementatie volstaat opslaan. De klasse type 40 zet bit 1 zelf (`0x44d304`).

`+0x88`-status: 0 = bounding-sphere opnieuw berekenen, 1 = gecachet in `+0x8c..0x94` (middelpunt) / `+0x98` (straal) (`0x42efe0`),
2 = speciale toestand gezet door `0x45f39a` en door klassen 50-52 (`0x450db2`); blijft behouden door klok en bericht 3.

## 7. Klassespecifieke berichten uit de W1A-lijst (kort)

Gecorrigeerde typetabel (de vtable wordt in `0x403440` na de ctor overschreven; classmap_raw.txt mist dit):
50 → `0x4a92ec` (handler `0x451040`), 51 → `0x4a9278` (`0x4514d0`), 52 → `0x4a9204` (`0x451610`), 35 → `0x4a9444`, 38 → `0x4a93d0`
(handler `0x44f9b0`), 121 → `0x4a9034`. Ketting: `0x4514d0`/`0x451610` → `0x451040` → `0x44e8f0` → `0x42d5e0`.

| id | klasse | adres | werking |
|---|---|---|---|
| 50 | 50-52 | `0x45108a` | byte `+0x110 = (v == 1)`: zone aan/uit |
| 51 | 50-52 | `0x451059` | voor alle `+0x10c` records (0x30 B op `+0x108`, een per marker-node met typecode 0, `0x450dd0`): float `rec+0x18 = v` (rauw, geen ×0.01) = straal |
| 52 | 51 | `0x4514e2` | float `+0x114 = v` (rauw; standaard 400.0, `0x45129e`) = lengte van de straal vanaf de marker |
| 53 | 52 | `0x451622` | `+0x114` = instantiepointer van arg1 (doelinstantie) |
| 55 | 20/21 | `0x45374e` | mode 1: `+0x114 = v·0.01` (s); mode 2: `+0x11c = v` (rauw; bovengrens van de opgebouwde snelheid `+0x12c`, `0x452d75`) |
| 1501 | 90 | `0x46cd07` | `+0xfc = v` (modus 0/1/2 van de "instance d'environnement", update `0x472560`) |
| 1502 | 90 | `0x46cd2e` | **kleur**, geen doelpunt: `+0x110..0x118 = (r,g,b)/255` (`0x4abc5c` = 1/255), `+0x11c = w·0.01`, `+0x100 = flag`, `+0x108 = 0`, daarna `vtable[0x1c]` = `0x472b30` (herinitialisatie) |

Klassen 50-52 zijn gevarenzones: de update `0x450f20` (→ fade `0x44e810`, daarna alleen als `+0x110` gezet is) test per record alle actors
uit `0x4c52d8` tegen het lijnstuk van het record (`0x433de0`, straal `rec+0x18 · 0.85`) en roept bij een treffer `actor->vtable[0x98/4](2)` aan.
Type 90 is een omgevings-/deeltjesvolume (string "une instance d'environnement n'a pas de volume", `0x4725c5`); 1501/1502 verplaatsen dus niets.

## 8. Beweging van instanties: overzicht

1. **Animatietracks** (klok §1): alle node-beweging binnen het model; de instantiepositie zelf verandert niet, maar `+0x60..0x68`
   (wereldpositie van de laatste gewone node) wordt gebruikt om de instantie opnieuw in een cel te hangen. Draaiende ventilatoren,
   deuren en liften zijn dus gewoon animaties die met 3/4 gestart worden (lus = 4, open/dicht = 3 met flag 1/0).
2. **Padvolger** (§3): enige generieke verplaatsing van `inst.pos`; constante snelheid over de booglengte, tijd in 1/100 s voor het hele pad,
   opties lus / heen-en-weer / orienteren. Gestart met 42/43, gestopt met 44.
3. Er is in de basisklasse **geen** "lineair naar doel" of "roteer met constante snelheid": geen enkel bericht in `0x42d5e0` schrijft
   `+0xc..0x14` of `+0x28..0x48` behalve 42/43 (padbegin/-eind). 1502 is een kleur (§7). Overige verplaatsing zit in klassen
   (vijanden 4-16 bericht 11, Perso, type 20/21, type 60).

De klok (en dus ook de padvolger) draait alleen voor instanties met een cel (`+0x1c ≠ −1`), een keer per frame, vanuit de wereld-tekenlus
`0x42b380` → `vtable[2](5 of 7)` = `0x42e2b0` voor ALLE instanties in `wereld+0x64` (niet alleen zichtbare). `vtable[3]` (denk-stap; basis = leeg
`0x462c60`) wordt door `0x42b400` voor alle instanties aangeroepen, ook verborgen.

## 9. Veldentabel basisklasse (0x104 bytes)

| veld | betekenis |
|---|---|
| +0x00 | vtable (`0x4aa31c`; [2] = update/teken `0x42e2b0`, [3] = denk-stap, [8] = botsing `0x433140`, [17] = anim-reset `0x42e250`, [22] = berichten) |
| +0x04 | id `0x01000000 + slot` |
| +0x08 | vlaggen: bits 0-4 soort (1 = instantie, 2 = type 60, 3 = camera); 0x20 = geen her-cellen op geanimeerde positie (`0x43f2ed`); 0x40 = niet-botsbaar |
| +0x0c..0x14 | positie |
| +0x18 | resultaat van `0x40a0c0(pos, −1)` (−1 als verborgen) |
| +0x1c | wereldcel (−1 = verborgen/buiten de wereld) |
| +0x24 | volgende instantie in de cellijst (`cel+0x44`) |
| +0x28..0x48 | 3×3 rotatie (rijen) |
| +0x4c..0x54 | schaal x, y, z |
| +0x58 | framenummer laatste klok |
| +0x5c | eerste wereldmatrix-index |
| +0x60..0x68 | geanimeerd middelpunt (cel-bepaling) |
| +0x6c | transparantie 0..1 (§5) |
| +0x70 | VM-id's van volumes/collisions |
| +0x74 | 16-byte records per licht-/subonderdeel (bericht 14) |
| +0x78 | TRAJ / padvolger (§3) |
| +0x7c, +0x80 | runtime (0 bij init; +0x7c = resultaat `0x42f490`) |
| +0x84 | −1 bij init; ≠ 0 vereist voor sphere-cache |
| +0x88 | sphere-cache-status (§6) |
| +0x8c..0x98 | gecachete bounding-sphere (xyz, r) |
| +0x9c | animatie-einde-vlag (§1) |
| +0xa0 / +0xa4 | snelheid / basissnelheid |
| +0xa8 | starttijd |
| +0xac | positie in animatie (s) |
| +0xb0..0xbc | slots 0..3 |
| +0xc0 | slot0 == slot1 |
| +0xc4 / +0xc8 / +0xcc | framenummer / animatie / framepositie van de vorige event-scan (`0x42f5e0`: events tussen vorige en huidige framepositie → `0x43a880` → `0x4695f0`) |
| +0xd0 | fasemasker `(1<<round(fase·15))<<16`; renderer en botsing (`inst+0xd0 & hull-id & 0xffff0000`, zie PERSO_MOVE) zetten daarmee nodes per animatiefase aan/uit |
| +0xd4 | kop van de link-lijst (bericht 34) |
| +0xd8..0xec | textuuroverride (§2) |
| +0xf0 | SetFlags-bits (§6) |
| +0xf4 | runtime-state per mesh-node |
| +0xf8 | model `S` |
| +0xfc / +0x100 | (afgeleide klassen via `0x44e8f0`) fade-doel / fade-snelheid per s |

## 10. Berichtentabel (basis `0x42d5e0` + tussenhandler `0x44e8f0`)

| id | args | adres | exacte werking |
|---|---|---|---|
| 1 | anim, t | `0x42de97` | stilstaand beeld: snelheid 0, `+0xac = L(oud slot0)·t/1000`, slot0 = anim, wachtrij leeg |
| 2 | anim, dir, dur, off | `0x42de16` | eenmalig, doorlooptijd dur/100 s, start op fractie off/1000, dir 0 = achteruit |
| 3 | anim, dir, dur | `0x42d62d` | eenmalig; dur ≤ 10 → spring naar eind (dir≠0) / begin (dir=0) van de HUIDIGE animatie; zelfde animatie loopt al → omkeren vanaf huidige pose of negeren |
| 4 | anim, dir, dur | `0x42d8e7` | lus (alle 4 slots), doorlooptijd dur/100 s |
| 5 | – | `0x42d606` | pauze (snelheid 0) |
| 6 | on | `0x42d985` | cel aan-/afkoppelen = tonen/verbergen incl. botsing (§4) |
| 7 | – | dispatcher `0x4012f0` | wachtende 12/13 annuleren |
| 12 / 13 | als 3 / 4 | `0x42d619` / `0x42d8d3` | wacht (return 1 → retry-lijst `0x401250`) tot `+0x9c ≠ 0`, dan als 3 / 4 |
| 14 | b0,b1,b2,f,d | `0x42db9e` | per 16-byte record op +0x74: bytes 0..2, float +4, dword +8; 0xffff = ongewijzigd |
| 15 / 17 | x, mode, f, t2 | `0x42d9c3` / `0x42daae` | UV-scroll eindig / eindeloos (§2) |
| 16 / 18 | x, mode, f | `0x42da32` / `0x42db0f` | textuurframes eenmalig / lus (§2) |
| 19 | – | `0x42db86` | textuuroverrides uit |
| 34 | other | `0x42dc21` | link-paar toevoegen (tabel `[0x50944c]+0x50`, teller +0x4c), `+0xd4` = nieuwe kop |
| 42 / 43 / 44 / 46 | zie §3 | | padvolger |
| 45 | bits | `0x42ddb4` | `+0xf0` krijgt de bits `& 0x23` erbij |
| 56 | v | `0x42de00` / `0x44e91b` | basis: `+0x6c = v/100`; afgeleid: fade-doel `+0xfc = v/100` |
| 57 | v | `0x44e907` | alleen afgeleid: fade-snelheid `+0x100 = v/100` per s |

## 11. Recept voor herimplementatie in C

```c
typedef struct {
    int   slot[4];            // init {0,-1,-1,-1}
    float speed, base_speed;  // 0
    float start, pos;         // +0xa8, +0xac
    int   ended;              // +0x9c
    float fade, fade_target, fade_rate;   // 0, 0, 100; target/rate alleen voor klassen achter 0x44e8f0 (o.a. type 70)
    int   has_fader;          // klasse != basis
    int   hidden;             // cel == -1
    int   noncollide;         // +8 & 0x40
    unsigned setflags;        // +0xf0
    unsigned phase_mask;      // +0xd0
    struct { unsigned char mode; float t0B, facB, t0A, facA, t2A; } tex;   // +0xd8..0xec
    struct { unsigned flags; float t0, dur; } traj;   // bits 17..22 zoals §3; punten/lengte uit het .ins
} InstAnim;
```
- `on_msg`: implementeer 1, 2, 3, 4, 5, 12, 13 exact volgens §1.3 (let op `dur·0.01`, `off·0.001`, `dur ≤ 10`, omkeerlogica van 3);
  12/13: als `!ended` → bericht in een retry-lijst (max 32 in het origineel) en elk frame opnieuw aanbieden; 7 wist die lijst voor de instantie.
- Per frame per niet-verborgen instantie: eerst `traj_update`, dan de klok van §1.2 (fase → `frame = nframes·fase`), dan tracks evalueren.
  De formule "fase = (now−start)/(duration/4096/speed)" alleen is onvolledig: voeg klemmen (eenmalig, snelheid → 0), slot-doorschuiven (lus),
  negatieve snelheid (fase = 1 − …) en het pad "snelheid 0 → fase = pos/L" toe.
- 6: verbergen = niet tekenen, niet botsen, geen volumes; tonen = cel opnieuw bepalen op `inst.pos`.
- 56/57: basisinstanties direct, type 70 (en andere afgeleiden) via doel + snelheid; teken met alpha = 1 − fade, sla over bij fade > 0.98,
  botsing uit bij fade > 0.9 (flankgestuurd, zie §5).
- 16/18/15/17/19: textuurframe-/UV-override per instantie (§2).
- 42/43/44/46: padvolger (§3). 45: alleen opslaan. 50/51/52/55/1501/1502: klasse-eigen, geen beweging.

## 12. Open vragen
1. `+0xd9`/`+0xda` (arg1 van 15..18, in W1A 0xffff): geen lezer gevonden in `0x47f290`; mogelijk een textuurfilter elders in `0x43b3f0`.
2. SetFlags bit 2: geen lezer gevonden. Bit 1: is de extra pas een schaduw of een spiegeling (tabel `[0x4c4cac]+4`)? Bit 0x20: aard van de effectpas.
3. Exacte kleurmenging in `0x4388e0` bij fade > 0.01 in de extra pas.
4. `+0x88 == 2`: betekenis van deze toestand in `0x42e2b0`.
5. Afrondingsmodus van `0x499580` (ftol) bij de textuurframe-index en het fasemasker (afkappen of afronden).
6. Klasse 20/21 (bericht 55), type 60 (`0x474a40`, 1503/1506) en vijandbericht 11 zijn hier niet uitgewerkt.
7. `0x40a0c0` (`+0x18`): wat dit tweede cel-achtige veld precies is.
8. De padvolger gebruikt het "closed"-bit (16) niet; of gesloten paden in de data het eerste punt herhalen is niet gecontroleerd.

