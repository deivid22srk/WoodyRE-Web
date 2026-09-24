# WATER.md — klasse 60, het watervolume

Statische analyse van de instantieklasse 60 (`size 0x158`, ctor inline in `0x403440` op `0x403c79`, vtable `0x4a9194`) en de
port in `src/water.c`. Aanleiding: in W2A bewoog het water niet en verdronk Woody er niet in. Vier W2A-instanties
(183..186, model 25) krijgen `SetTypeInstance 60` en direct daarna bericht 1506; in totaal gebruiken 16 levels de klasse
(K2A/K2R/K3A/K3R/KWS, S2A/S2R/S3A/S3R/SWS, W2A/W2B/W2D/W3A/W3B/WWS).

| vtable | adres | wat |
|---|---|---|
| [1] | `0x473120` | Init: het rooster over de bovenkant bouwen (alleen als `+0x14c` gezet is, anders basis `0x42e210`) |
| [2] | `0x4738c0` | Draw(bits): bit 1 = middelpunt/straal voor de zichtbaarheid (`+0x88..+0x98`, gemiddelde van de 8 boxpunten × 0.125), bit 4 = het oppervlak tekenen |
| [3] | `0x4747f0` | Update: kielzog-effecten en de verdrinkingstest |
| [22] | `0x474a40` | handler: 29, 33, 35 genegeerd (geeft 0), rest naar de basis `0x42d5e0` |
| dtor | `0x474250` | geeft de vijf roosterbuffers vrij als `nc·nr < 0x190` |

Het model (W2A model 25) is een doos: node 0 mesh (12 driehoeken, boven- en onderkant met textuurgroep 56), node 1 kind 2
(bbox), node 2 kind 1 (press). **De doos zelf wordt nooit getekend** (Draw vervangt de modelrenderer; `0x42e2b0` wordt alleen
met bit 1 aangeroepen) en **botst nooit**: Init zet `inst+8 |= 0x40` (`0x473876`).

## 1. Bericht 1506 SetWaterVolumeParameter (`0x46ce38` → `0x474690`)

`1506 [inst, a, b, c, d]` → `+0x13c = a·0.01` (celgrootte), `+0x140 = b` (**geheel getal**, geen ×0.01: aantal vakken langs
de korte kant), `+0x144 = c·0.01` (golfamplitude), `+0x148 = d·0.01` (alfa), `+0x14c = 1`, dan `vtable[1]` (Init).
MESSAGES.md had b als ×0.01; dat is fout (`0x46ce4e`: `mov ecx,[eax+0x10]` gaat ongeschaald de stack op).
W2A: `[1000000, 2, 3500, 40]` voor 183..185 (cel 10000, 2 vakken, 35, 0.40), `[130000, 4, 2000, 50]` voor 186; de assert-tekst
van de klasse noemt dezelfde tweede set als voorbeeld.

## 2. Init `0x473120`

1. `M = 0x42f7e0(0, 0, &M, 1)`: de track van de **eerste gewone top-node** (vanaf `S+0x6c`, volgende broer zolang de vlaggen
   ≠ 0) op animatie 0, fase 0, maal de instantiematrix (met schaal). De dozen hebben hun echte plaats in die track: W2A 183
   staat daardoor 340 lager dan zijn `.ins`-positie (bovenkant y = −89, bodem van het meer −1000, de steiger 114).
   Met alleen de instantiematrix lag het oppervlak boven de steiger en verdronk Woody bij de start.
2. Hoeken: van elke polygoon van de eerste mesh-node (`S+0x34`, 1-gebaseerd) met **normaal-z ≥ 0.1 in node-ruimte**
   (`[poly+0x10]`; de dozen zijn z-omhoog gemodelleerd en door de instantie rechtop gezet) de eerste drie indices, uniek, maximaal 4.
   De eerste polygoon zonder bit 15 in het materiaal levert de textuur (`+0x110`).
3. `0x4742f0(c, 4)` sorteert half: pas 1 `i = 0..2`: wissel `c[i]`↔`c[0]` als `P[i].x < P[0].x && P[i].y > P[0].y`; pas 2
   `i = 1..2`: wissel met `c[1]` als `x > && y >`; pas 3 wisselt `c[2]` met zichzelf. `c[3]` wordt nooit bekeken.
4. `A = M·P[c0]`, `E1 = M·P[c1] − A`, `E2 = M·P[c3] − A`; `len1 = |E1|` (`+0x11c`), `len2 = |E2|` (`+0x120`);
   `+0x13c = min(+0x13c, len1, len2)`.
5. `n1 = round(+0x140 · len1 / +0x13c)`, `n2` idem met len2 (fistp, minimaal 1); `nc = n1+1` (`+0x114`), `nr = n2+1` (`+0x118`).
   `nc·nr > 400` → `"Un volume d'eau a trop de face"` en klaar (in de data nooit).
6. Hoekpunt `k = j·nc + i`, `s = i/(nc−1)`, `t = j/(nr−1)`: `v = A + s·E1 + t·E2 + (0, 10, 0)` (`+0xfc`),
   `uv = (s·len1, t·len2) / +0x13c` (`+0x100`: de textuur herhaalt elke `+0x13c` eenheden), fase `= ftol(rand·512)` (`+0x10c`).
7. Driehoeken per vak: `(k, k+1, k+nc)` en `(k+nc, k+1, k+nc+1)` (`+0x108`).
8. Middelpunt `+0x130 = v[0] + 0.5·E1 + 0.5·E2`; `inst+8 |= 0x40`; kielzogtimer `+0x154 = rand·5`.

## 3. Draw bit 4 (`0x473a58..0x474229`)

Tabellen in het subsysteem `[0x5e823c]` (gevuld op `0x40248f`/`0x402520`): `+0` = `cos(i·2π/512)`, 512 stuks;
`+0x900` = `0.5·cos(i·2π/512)^8`, 128 stuks.

* **Licht** (`0x474128`): `d` = genormaliseerde xz-richting camera → middelpunt, `L = len1 + len2`;
  zonpunt `+0x124 = middelpunt + (L·d.x, 0.25·L, L·d.z)` — achter het water gezien vanaf de camera.
* **Kleur per hoekpunt** (`0x474490`): `a = norm(camera − v)`, `b = norm(v − zon)`, gespiegeld `r = (b.x, −b.y, b.z)`;
  `a·r < 0` → waarde 1, anders `|a × r|` (sinus van de hoek). RGB = `tabel900[round(waarde·127)] + 0.4`, alfa = `+0x148`.
  Een glimlijn waar de weerspiegelde zon het oog raakt: 0.9 recht erin, 0.4 daarbuiten.
* **Twee lagen**, elk alle driehoeken; eerst laag B, dan A. Alleen **binnenpunten** bewegen (niet rij 0, niet de laatste rij,
  niet `k % nc == 0`, niet `(k+1) % nc == 0`), met `c = cos[ph & 511]`, `s = −cos[(ph+128) & 511] = sin`, amplitude `+0x144`:
  A = `(x + c·amp, y, z + s·amp)`, uv; B = `(x − c·amp, y − 5, z − s·amp)`, `uv + (0.23, 0.85)`. De twee lagen draaien dus
  tegen elkaar in; met de grove roosters van W2A (3×5, 3×7, 3×3, 12×8) is het effect vooral een wervelende textuur.
* Driehoeken via `0x472040` (clip) naar **textuurlijst 8** (`0x42b460(tex, 8)`): in de flush `0x4293f0` blend aan,
  SRCALPHA/INVSRCALPHA, **z-write uit**, na de modellen (lijst 11) en vóór de additieve lijst 3 en de fadelijst; MODULATE2X
  (LIGHTING.md §1.5). Kleurbytes = `float·255` (`0x4aa308`). Geen culling.
* Daarna per hoekpunt `fase += ftol((rand·30 + 250)·dt)` (fistp, afronden): ~265/512 omwenteling per seconde, alleen in
  frames waarin het water getekend wordt.

## 4. Update `0x4747f0`

1. `+0x154 −= dt`; onder 0: kielzog op een willekeurig punt `v0 + rand·len1·norm(v1−v0) + rand·len2·norm(v_nc−v0)` (de
   eerste driehoek), hoogte `v0.y`, richting `(cos a, 0, sin a)` met `a = ftol(rand·512)`: `0x472ec0(pos, dir)`;
   dan `+0x154 += rand·2`.
2. Verdrinken: `p = Perso.pos + (0, 120, 0)` (`0x4abcb4`); `0x4746e0` transformeert p naar node-ruimte (`0x440fc0`, de
   actuele nodematrix) en test alle vlakken van de mesh-node: overal `n·p + d ≤ 0` → binnen. Dan `+0x14d = 1`,
   **`0x44d160` = `Perso->vtbl[0x98](7)` = Kill(7)** en `+0x150 = 3.0`. Elk frame opnieuw; de Perso negeert het als hij al
   aan het verdrinken is. Voeten 120 onder het oppervlak is dus dood: in W2A y < −209.

### 4.1 Kielzog (`0x472ec0` → `0x472f50` → `0x473050`, deeltjeslijst `[0x5e823c]+0xdb8`, max 2000)

Emitter: leeft 1.0 s, beweegt 300 eenheden/s (`0x4a986c`) langs `dir` en laat elke 0.1 s een merkteken achter.
Merkteken: leeft 0.5 s, `p = leeftijd/0.5`; sprite `0x470f10` met vlaggen 0x12 (eigen kleur, geen billboard → plat op normaal
(0, 1, 0), additief), beeld `0x1003a` = bank 0 beeld 58, kleur 0.8, alfa `(1−p)·0.7`, halve diagonaal `10 + 70·p`,
rotatie `+0x224` willekeurig maar zonder vlag 4 ongebruikt.

## 5. Port (`src/water.c`)

* `water_add` bij `SetTypeInstance 60`, `water_param` bij 1506 (Init, met `M` = `node_world` na `ins_pose(inst, 0, 0)`),
  `water_update` elk niet-gepauzeerd frame (fases, kielzog, Kill(7) via `player_kill`), `water_draw` via de nieuwe haak
  `Renderer.post_models` (na de modelpassen, vóór de fadelijst), `water_fx_draw` bij de effectsprites (`hud_world_fx_plane`,
  fx-slot 11 = beeld 58). `draw_instance` slaat type 60 over.
* Afwijkingen: de fase loopt als float door (geen ftol per frame, dus onafhankelijk van de framerate) en ook buiten beeld;
  de binnentest gebruikt de matrix van Init i.p.v. de actuele (geen enkele waterdoos is geanimeerd); MODULATE2X via
  `GL_COMBINE` + `RGB_SCALE 2` (zonder die extensie: kleur ×2, geklemd).
* Test: `extract/Data W2A` (start op de steiger, water rondom); verdrinken `--pos -700 200 500` (Kill 7 rond 0.6 s,
  respawn); animatie `WOODY_SHOTSEQ="out/wq 1.0 0.25 6" --cam -600 300 -600 200 -35`; `WOODY_WATERLOG=1` = rooster per volume
  en elke seconde het aantal kielzoggen/merktekens.

## 6. Open

* Bericht 1505 (`0x478660(&inst.pos, 1000, f·0.01)`, een plons op een instantie; W2A 10×) en de waterplons van Kill(7)
  zelf (`0x478660`, PERSO_DEATH.md §4.3) zijn niet geport.
* Wat `+0x14d` en `+0x150` (3.0) na het verdrinken doen: geen lezer gevonden in de klasse zelf.
* Niet vergeleken met het draaiende origineel.
