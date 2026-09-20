# TODO.md — wat nog niet geport is

Eén lijst van alles wat de port (`src/`) nog mist of vereenvoudigt, met de plek in de docs waar de analyse staat.
Bijwerken bij elke ronde. "Analyse klaar" = er staat een recept in de genoemde doc; "niet geanalyseerd" = eerst decompileren.

## Speler (Perso)

| wat | status | waar |
|---|---|---|
| Klim-over volgt de wortelbeweging van .ins-anim 15 (`0x44e290`); nu een lerp | zie OBJECTS.md §1.5 | `src/player.c` `climb_update` case 3 |
| Klimmen: geen botsing met de wereld (zijwaarts door muren), geen vonken (`0x479c80`) | analyse klaar | OBJECTS.md §1.3 |
| Bukken (actie 5), rondkijken (actie 7) | niet geanalyseerd | PERSO_MOVE.md |
| Idle-variaties 0x59/0x5a | niet geport | PERSO_MOVE.md §4.3 |
| Gescripte Perso-acties (bericht 1040): 17/18 (deur in/uit, met fade) en teleport (bericht 26) geport; overige acties (10..16, 19: wortelbeweging) alleen stilstaan; bericht 30 (LockMove) niet. Na de teleport staat de volgcamera soms even in een muur (geen camerabotsing bij de cut) | grotendeels | PERSO_DEATH.md §1-2, CINEMATIC.md §6 |
| Doodsanimaties per soort en hit-animatie geport. Niet: tekstballon (soort 1, `0x478980`), skelet-flits (soort 2/9, `0x477e40`), waterplons (soort 7, `0x478660`), gebukte varianten, race-variant `0x464a00`, wit knipperen bij de onkwetsbaarheidsbonus (`0x44cf50`) | deels | PERSO_DEATH.md §3-6 |
| Perso-toestand 8 (raket berijden, type 20): geport. Niet: afstap-anim (in het origineel ook nergens aangevraagd), type 21 (meerijden op de kanonbom) | grotendeels | ROCKET.md §6 |
| Perso-toestand 6 (bom dragen) | op hoofdlijnen | BONUS.md §7 |
| Checkpoints: respawn-positie (1030) | controleren | GAMEFLOW.md |
| Salto die wegvalt bij springen in de lucht (melding gebruiker) | niet te reproduceren; welke toets/timing? | — |
| Obstakelsensor van de Mover | niet geport | PERSO_MOVE.md |
| Geometrie-queries zijn brute force, geen kd-tree | werkt, traag bij grote levels | — |

## Camera

| wat | status | waar |
|---|---|---|
| Broodkruimelpad (`0x423ab0`) als de camera de speler niet meer ziet | niet geport | CAMERA.md |
| Railcamera mode 8 (540) | geport, niet in situ getest | CAMERA_SCRIPT.md |
| Zijaanzicht mode 0x20: teken van de zijkant | onzeker | CAMERA_SCRIPT.md |

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
| 90 | omgevingsdeeltjes-volume (1501/1502) | analyse op hoofdlijnen | OBJECTS.md §2.4 |
| 60 | eigen handler (1503/1506) | niet geanalyseerd | OBJECTS.md §3 |
| 50 / 51 / 52 | laser: reizende puls, bliksemboog, inslag-sprite, straal tegen instanties | kern geport | OBJECTS.md §2.1 |
| berichten 15..19 | textuurframe-/UV-override per instantie | analyse klaar | INSTANCE.md §2 |
| 1201 / 1202 | typewoord-bit 0x400 (aanvalbaar doel) | analyse klaar | OBJECTS.md §3 |

## Effecten

| wat | status | waar |
|---|---|---|
| Deeltjes (vonken, rook, explosies, stof bij landen) | niet geport | — |
| Pickup-sprite: grootte (volle breedte?) en HUD-animatie bij oppakken | gok / niet geport | BONUS.md, HUD_TEXT.md |
| Lucht: links/rechts-oriëntatie van de kubus | niet geverifieerd | SKY.md |
| `.vis`-culling (nu frustum-cull per instantie) | niet geport | FORMAT_TEX_COL_VIS_LIT.md |
| Vsync / fps-begrenzing | niet aanwezig | — |

## 2D, menu's, spelverloop

| wat | status | waar |
|---|---|---|
| Titel: Opties doet niets, Stoppen slaat de bevestiging over, Laden gaat direct naar de hub, attract-timer | niet geport | TITLE.md |
| Pauzemenu, resultatenscherm, baas-levensbalk | niet geport | HUD_TEXT.md, GAMEFLOW.md |
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
