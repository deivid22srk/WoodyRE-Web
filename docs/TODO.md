# TODO.md — wat nog niet geport is

Eén lijst van alles wat de port (`src/`) nog mist of vereenvoudigt, met de plek in de docs waar de analyse staat.
Bijwerken bij elke ronde. "Analyse klaar" = er staat een recept in de genoemde doc; "niet geanalyseerd" = eerst decompileren.

## Speler (Perso)

| wat | status | waar |
|---|---|---|
| Klim-over volgt de wortelbeweging van .ins-anim 15 (`0x44e290`); nu een lerp | zie OBJECTS.md §1.5 | `src/player.c` `climb_update` case 3 |
| Klimmen: geen botsing met de wereld (zijwaarts door muren) | analyse klaar | OBJECTS.md §1.3 |
| Bukken (actie 5), rondkijken (actie 7) | niet geanalyseerd | PERSO_MOVE.md |
| Idle-variaties 0x59/0x5a | niet geport | PERSO_MOVE.md §4.3 |
| Gescripte Perso-acties (bericht 1040/1140): 17/18 (deur in/uit, met fade), teleport (bericht 26), beide camera-acties en nu ook de **wortelbeweging** (`0x44e290`) van alle overige acties (10..16, 19, 74..78) geport; bericht 30 (LockMove) niet | grotendeels | PERSO_DEATH.md §1-2.1, CINEMATIC.md §6, OBJECTS.md §1.5 |
| Doodsanimaties per soort en hit-animatie geport. Niet: tekstballon (soort 1, `0x478980`), skelet-flits (soort 2/9, `0x477e40`), waterplons (soort 7, `0x478660`), gebukte varianten, race-variant `0x464a00`, wit knipperen bij de onkwetsbaarheidsbonus (`0x44cf50`) | deels | PERSO_DEATH.md §3-6 |
| Perso-toestand 8 (raket berijden, type 20): geport. Niet: afstap-anim (in het origineel ook nergens aangevraagd), type 21 (meerijden op de kanonbom) | grotendeels | ROCKET.md §6 |
| Perso-toestand 6 (bom dragen) | op hoofdlijnen | BONUS.md §7 |
| Checkpoints: respawn-positie (1030) | controleren | GAMEFLOW.md |
| Landingsring op de vloer onder een springende Woody (issue #1): geport, maar de tekenfunctie van het origineel is niet gevonden (kandidaat `0x44af90`, elk frame na het renderen) – straal, dikte, kleur en helderheid zijn van een schermafdruk geschat, niet gelezen | geport, maten onzeker | PERSO_JUMP.md §5 |
| Salto die wegvalt bij springen in de lucht (melding gebruiker) | niet te reproduceren; welke toets/timing? | — |
| Pikschakelaars: bericht 1042 doet nu de hele test van `0x445269` (markervector, toestand 0, op de grond) en remt de zojuist gestarte stormloop af (`0x458e40` = `player_brake_charge`). Zonder die rem ramde Woody elke schakelaar in plaats van hem te pikken | opgelost (issue #21) | OBJECTS.md §1.2 |
| Obstakelsensor van de Mover (`0x44b2e0`, `p+0x234`): een stormloop remt in het origineel ook af voor een steile rand of muur; in de port loopt hij door tot de botsingscode hem stopt, dus tegen een instantie zonder hull-node stáát hij er half in. Vereist eerst `0x497a30` en de betekenis van resultaattype 3/4 | niet geport | PERSO_FRAME.md §3, PERSO_JUMP.md §2.3 |
| Grondsoort `P+0x308` wordt gelezen (voor de voetstappen), maar soort 1 (glad/ijs) past de bijdraai-ramp `0x45a850` nog niet aan | deels | PERSO_MOVE.md §6.4, FOOTSTEPS.md §2 |
| Geometrie-queries lopen nu via de kd-boom van `.gel` (vloer, push-out, zichtlijnen); instantie-hulls worden per knoop met een wereld-bbox afgewezen | opgelost (issue #9) | FORMAT_GEL.md 5 |

## Camera

| wat | status | waar |
|---|---|---|
| Broodkruimelpad (`0x423ab0`) als de camera de speler niet meer ziet | niet geport | CAMERA.md |
| Railcamera mode 8 (540) | geport, niet in situ getest | CAMERA_SCRIPT.md |
| Mode 0x80 (camera uit de animatietrack) geldt nu ook voor de deuracties 17/18, niet alleen voor cinematics — het zijaanzicht waarin Woody de deur in loopt | opgelost (issue #8) | CAMERA_SCRIPT.md §4.3 |
| Zijaanzicht mode 0x20: teken van de zijkant | onzeker | CAMERA_SCRIPT.md |
| Zijaanzicht: het vlak-slot eindigt nu bij gescripte actie (1040), teleport (26) en cinematic (`0x44de44`); niet bij dood/respawn op een checkpoint buiten het zijaanzicht | controleren | CAMERA_SCRIPT.md |

## Objectklassen

| type | wat | status | waar |
|---|---|---|---|
| 42 | lanceerder: alleen sjabloon 1 / visual 2 (energiebol, rechte lijn) geport; niet: doelzoekend, stuiteren, zwaartekracht, bommenwerper (soort 0), schiet-animatie (param 7/8), straal tegen instanties, lintkleur mogelijk te donker | deels | PROJECTILES.md |
| 41 | missile-visual (`0x4700e0`: model uit de pool, lint, uitlaat, explosie) en vuurbal (`0x470af0`); alleen gebruikt door vijanden / type 20-21. Instanties worden wel verborgen | analyse klaar | PROJECTILES.md |
| 7 / 8 / 9 | schutters: geport. Niet: missile-/vuurbal-visual van hun projectiel (nu de energiebol), obstakelsensor, dwaal-gewichten (bericht 11/19..27), bericht 11/4 reset en 11/5-6, bericht 6 (aan/uit) | grotendeels | ENEMY.md §8 |
| 13 | spook: geport (half doorzichtig, zweeft op spelerhoogte, schiet om en om, duikt). Niet: vuurbal-visual, patrouille-animaties | grotendeels | ENEMY2.md §3 |
| 12 | stilstaande bommengooier (eindbaas W2B, 1×): **geblokkeerd op het bommensysteem (type 40)**; zonder hem is W2B niet uit te spelen | analyse klaar | ENEMY2.md §4 |
| 10 / 11 | vliegende schutter / bommenwerper te voet: in geen enkel level gebruikt | op hoofdlijnen | ENEMY2.md §5-6 |
| 14 / 15 / 16 | bazen (ctors `0x40eb50`, `0x40d850`, `0x40c730`): W1B, W2D, W3D, WWS | niet geanalyseerd | ENEMY2.md §1 |
| sterf-effect | 5 sterren/belletjes boven de stervende vijand (`0x477610`) | geport | PERSO_DEATH.md §7 |
| 40 | bom (pool 16, oppakken, explosie r 400) | analyse op hoofdlijnen | BONUS.md §7, OBJECTS.md §3 |
| 120 / 121 | kist die door een bom opengaat (msgmask 0x20) | analyse op hoofdlijnen | OBJECTS.md §3 |
| 17 | breekbaar object voor bommen | analyse op hoofdlijnen | OBJECTS.md §3 |
| 20 | berijdbare raket: geport (opstappen, draaien, ontsteking, rechte vlucht, rood knipperen, explosie r 600 = Kill(6), respawn met fade-in, geluiden 15/16/10/11/6). Vereenvoudigd: vlam als billboard i.p.v. drie gekruiste quads, explosie alleen de twee flitsen (deeltjes `0x4767f0`/`0x4764f0` niet gedecompileerd), geen camerabotsing tijdens de rit (port-keuze), startoriëntatie niet tegen het origineel geverifieerd | grotendeels | ROCKET.md |
| 21 | bomkanon (W2A/W2B/W2D): Woody rijdt mee op een afgeschoten bom; **geblokkeerd op het bommensysteem (type 40)** | analyse klaar | ROCKET.md §7 |
| 90 | omgevingsvolume: modus 0 (1501/1504) geport - de vlinders van het titelscherm, met hun dwaalgedrag, landen en vleugelslag. Niet: modus 1 (`0x47e160`, langs de grondnormaal) en 2. Alleen House gebruikt klasse 90 | grotendeels | TITLE.md §3.1, OBJECTS.md §2.4 |
| 60 | eigen handler (1503/1506) | niet geanalyseerd | OBJECTS.md §3 |
| 50 / 51 / 52 | laser: reizende puls, bliksemboog, inslag-sprite, straal tegen instanties | kern geport | OBJECTS.md §2.1 |
| berichten 16/18/19 (textuurframe-override) geport; 15/17 (UV-scroll-override) niet | textuurframe-/UV-override per instantie | grotendeels | INSTANCE.md §2 |
| 1201 / 1202 | typewoord-bit 0x400 (aanvalbaar doel) | analyse klaar | OBJECTS.md §3 |
| 110 | figuurtjes van de wereldkeuze-carrousel: de port verbergt ze (zie TITLE.md §3.2). Niet geport: de pagina zelf, de carrouselhoek (`0x451890`) en de transform per frame (`0x489210`) | verborgen | TITLE.md §3.2 |

## Effecten

| wat | status | waar |
|---|---|---|
| Deeltjes: de pickup-effecten (`0x4793d0`) zijn geport met hun eigen pool. Niet: rook en de explosiedeeltjes van de raket (`0x4767f0`/`0x4764f0`) | deels | BONUS.md §2.4 |
| Voetstappen: de trigger (loopcyclus 0.38/0.9, soort 2/3 op de grondsoort) en het landingsstof staan er; de effectfuncties `0x47cba0` en `0x476140` zelf zijn niet gedecompileerd, dus beeld, levensduur en kleur zijn een reconstructie | trigger geport, beeld gereconstrueerd | FOOTSTEPS.md |
| Pikinslag `0x479c80` (bij het klimmen en bij elke treffer van de aanvalsstraal): aanroepplekken en timing zijn bewezen, de functie zelf is niet gedecompileerd, dus het pikgat in de wand (eigen pool, `hud_world_gouge`) en de houtsnippers die eruit vallen (`hud_world_chip`) zijn een reconstructie — maar niet meer de lasertreffer die er eerst voor doorging | trigger geport, beeld gereconstrueerd (issue #4) | OBJECTS.md §1.6 |
| Zwarte contourlijn (SetFlags-bit 0x20) geport. Het origineel blaast alleen hoek 0/1/2 van een quad op (`0x43c42d`); de port alle hoeken, en de afstandsreferentie is de instantie-translatie i.p.v. `inst+0x60` | grotendeels | MODEL_RENDER.md §7 |
| Modelrendering: mipmaps (het origineel bouwt er 4 met een boxfilter, MIPFILTER POINT); bitreplicatie in de RGB565-decode (het origineel laat de lage bits 0); diepte-sortering van de geblende modelbatches (`0x428d00`, 256 emmers achter-naar-voor) | niet geport | MODEL_RENDER.md recept 9-10 |
| Pickup-sprite: de grootte is opgelost - `sprite+0x264` is de halve **diagonaal** (`0x470fee`), dus elke additieve sprite in de port was `1/sqrt(2)` te klein. Rechtgezet in `hud_world_fx` en `hud_world_sprite` | opgelost | BONUS.md §2.4 |
| HUD-animaties bij het oppakken (invliegend icoon + spoor + getal-pop) en de W-zwerm die 25 bonussen uitbetaalt: geport. Niet: de in-/uitschuif van de $-teller (`+0x3b/+0x3c`), de pauze-HUD (`+0x3d/+0x3e`) en de "min 1" van $ en lading (`+0x3f/+0x41`) | grotendeels | HUD_TEXT.md §4.6 |
| De vormuitbarsting van type 30/35 spuwt in het origineel 4 resp. 8 deeltjes per **frame**; de port normaliseert dat op 60 Hz zodat de dichtheid niet met het frametempo meeloopt | bewuste afwijking | BONUS.md §2.4 |
| Geblende modelvlakken (neonreclame, het kruis/de pijl naast een deur, lichtbalken, lampgloed) werden door de belichting gehaald en waren in de schaduw zwart; het origineel tekent ze onbelicht op `1.0 × textuur` | opgelost (issue #5) | LIGHTING.md recept 5 |
| Wereldvlakken met materiaalbit 15 (onzichtbaar, alleen collision) werden als vlakke grijze platen getekend, o.a. de "glasplaat" in W1A; het origineel slaat ze over vóór de vlaktekenaar (`0x42acd3`). Ook geen lichtvlek en geen schaduw meer op zo'n vlak | opgelost (issue #2) | FORMAT_GEL.md §1 |
| Lensflare `0x474a90` van licht-objecten: **niet porten**, de registratietabel wordt alleen door bericht 1510 gevuld en dat komt in geen enkel level voor | n.v.t. | LIGHTING.md §1.1 |
| Lucht: links/rechts-oriëntatie van de kubus | niet geverifieerd | SKY.md |
| `.vis`-culling: sector van de camera -> `.vis`-lijst -> frustum op de sectorboxen; alleen de vlakken van die sectoren gaan naar de kaart, instanties erbuiten krijgen geen licht, schaduw of tekenbeurt | opgelost (issue #9) | FORMAT_TEX_COL_VIS_LIT.md 3 |
| Vsync / fps-begrenzing | niet aanwezig | — |
| `.col` (objecten per bladcel) wordt niet geladen; de port beslist per instantie met `gel_sectors_in_box` op zijn bolstraal welke sectoren hij raakt | bewuste afwijking | FORMAT_TEX_COL_VIS_LIT.md 2 |

## 2D, menu's, spelverloop

| wat | status | waar |
|---|---|---|
| Titel: Opties doet niets, Stoppen slaat de bevestiging over, Laden gaat direct naar de hub, attract-timer | niet geport | TITLE.md |
| Resultatenscherm na een level (paneel, juichen, score, "opslaan?"): geport volgens GAMEFLOW §5.2. Niet: de echte layout van het paneel (`0x454963..0x455d97`), de slotkeuze-pagina's en de pauze-/overlayvlaggen van pagina 0x1e | grotendeels | GAMEFLOW.md §5.1-5.2, HUD_TEXT.md §6 |
| Pauzemenu, baas-levensbalk | niet geport | HUD_TEXT.md, GAMEFLOW.md |
| HNM-logo's en -films | niet geport | — |
| Toetsen uit `Woody.cfg`, joystick | niet geport | — |
| Savegame: eigen `woodyre.sav`, niet het originele formaat | bewust | GAMEFLOW.md |

## Geluid

| wat | status | waar |
|---|---|---|
| Occlusie (dempen achter muren), variant-wachtrij | niet geport | SOUND.md |
| Balans/panning | op het gehoor te controleren | SOUND.md |

## Scripts / VM

| wat | status | waar |
|---|---|---|
| Ongeveer de helft van de ~140 game-berichten is nog `?`/`~` | lopend | MESSAGES.md |
| Engine→VM-events buiten volumes/press-nodes/msgmasks | deels | EVENTS.md |
