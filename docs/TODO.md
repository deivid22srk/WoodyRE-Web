# TODO.md — wat nog niet geport is

Eén lijst van alles wat de port (`src/`) nog mist of vereenvoudigt, met de plek in de docs waar de analyse staat.
Bijwerken bij elke ronde. "Analyse klaar" = er staat een recept in de genoemde doc; "niet geanalyseerd" = eerst decompileren.

## Speler (Perso)

| wat | status | waar |
|---|---|---|
| Klim-over volgt de wortelbeweging van .ins-anim 15 (`0x44e290`); nu een lerp | zie OBJECTS.md §1.5 | `src/player.c` `climb_update` case 3 |
| Klimmen: geen botsing met de wereld (zijwaarts door muren) | analyse klaar | OBJECTS.md §1.3 |
| Bukken / plat liggen (actie 5, `0x465b10`): geport met toets **X** (Spatie is in de port springen): automaat 0x31/0x32/0x33 (met bom 0x4e/0x4f/0x50), LockMove, lichaamshoogte 61 (schoten, lasers, de Boss14-kegel en de wandsweep), opstaan-test voeten+61..+132 tegen wereld en press-knopen, geraakt 0x23/0x24, dood 0x2c, geen aanval/worp. Testoptie `--duck T LEN`, log `WOODY_DUCKLOG=1`. Niet: bukken in toestanden 3/4/5/7/8/9 (de automaat loopt daar in het origineel door), de horizontale nul-vector bij andere blokkades dan bukken. Rondkijken (actie 7): niet geanalyseerd | bukken geport | PERSO_DUCK.md, PERSO_MOVE.md |
| Race (toestand 1, types 18/19): geport = board onder de rijder (1120), rijden 1250 u/s + sturen + veto, springen, bukken, crash + race-kill, boost (1121), leunanimaties, racecamera. Niet: board-FX `+0x4b8`, regiolijst `[0x509adc]+0xc0`, geluiden 59/60, rumble, ster-effect, race-bonussen terug bij respawn (`0x44f8a0`), herstart vanuit het pauzemenu (`0x4560f0`), de kantel-filter `+0x210` (rijder en board blijven rechtop), crashstralen alleen tegen de wereld; het board volgt de rijder-anim (ook bij dood soort 1: 0x75 i.p.v. 0x76) | grotendeels | RACE.md §10 |
| Idle-variaties 0x59/0x5a (`0x464500`, slapen + zzz-ballon) geport; de reset op `P+0x550` en de bevriezing bij `P+0x5b4`/`P+0x694` niet | grotendeels | PERSO_MOVE.md §4.3 |
| Gescripte Perso-acties (bericht 1040/1140): 17/18 (deur in/uit, met fade), teleport (bericht 26), beide camera-acties en nu ook de **wortelbeweging** (`0x44e290`) van alle overige acties (10..16, 19, 74..78) geport; bericht 30 (LockMove) niet | grotendeels | PERSO_DEATH.md §1-2.1, CINEMATIC.md §6, OBJECTS.md §1.5 |
| Doodsanimaties per soort en hit-animatie geport. Tekstballon `0x478980` geport (dood 1, harde landing, zzz, bericht 1500; niet de harde landing in de race). Niet: skelet-flits (soort 2/9, `0x477e40`), gebukte varianten, race-variant `0x464a00`, wit knipperen bij de onkwetsbaarheidsbonus (`0x44cf50`) | deels | PERSO_DEATH.md §3-6 |
| Perso-toestand 8 (raket berijden, type 20, en de kanonbom van type 21): geport. Niet: afstap-anim (in het origineel ook nergens aangevraagd) | grotendeels | ROCKET.md §6-7 |
| Perso-toestand 6 (bom dragen): oppakken (aanval binnen 269), dragen op de 0x80-knoop, grond-/luchtworp (45° omhoog, 1000 u/s), loslaten bij dood/actie/teleport/lange val, geraakt met bom (0x21/0x22) geport. Bukken met bom (0x4e..0x50) geport. Niet: doelsturen van een teruggegooide bom naar de werper (`T.target`), de rondkijk-bug | grotendeels | BOMB_CARRY.md §1, §8 |
| Iris van de Game-sequentie `0x4459c0` (Game+4): levelstart 0 → 1 in 1 s, dood 1 → 0 in 1 s, 0.25 s zwart, respawn, 0 → 1 in 1 s; na de HUD getekend en ook tijdens cinematics getikt. Was in de port een helderheidsfade. De iris blijft rond op een niet-4:3-venster (port-keuze; het origineel draaide 4:3). De eerste levelload vervaagt nu ook 1 s in (`0x404332`). Niet: de pauzemenu-herstart `0x40584d` (race) | geport | PERSO_FRAME.md §4.1 |
| Checkpoints (1030): positie én kijkrichting (marker typecode 0, `0x44aa10`) geport; de respawn `0x445930` wist het zij-aanzicht en cut naar de volgcamera. Zonder dat bleef het vlak-slot staan, werd het checkpoint vóór de deur van een zij-aanzicht-stuk op het vlak geprojecteerd (in de leegte) en ging Woody eindeloos opnieuw dood (W1B, deur 392). Testhaak `WOODY_KILLAT=T` (bericht 1020 na T s). Niet: `SavePos.bin` (`0x44a920`/`0x44a810` met save = 1) | opgelost (issue #39) | PERSO_DEATH.md §3.4, GAMEFLOW.md §4.5 |
| Landingsring op de vloer onder een springende Woody (issue #1): geport, maar de tekenfunctie van het origineel is niet gevonden (kandidaat `0x44af90`, elk frame na het renderen) – straal, dikte, kleur en helderheid zijn van een schermafdruk geschat, niet gelezen | geport, maten onzeker | PERSO_JUMP.md §5 |
| Salto die wegvalt bij springen in de lucht (melding gebruiker) | niet te reproduceren; welke toets/timing? | — |
| Pikschakelaars: bericht 1042 doet nu de hele test van `0x445269` (markervector, toestand 0, op de grond) en remt de zojuist gestarte stormloop af (`0x458e40` = `player_brake_charge`). Zonder die rem ramde Woody elke schakelaar in plaats van hem te pikken | opgelost (issue #21) | OBJECTS.md §1.2 |
| Obstakelsensor van de Mover (`0x44b2e0`, `p+0x234`): een stormloop remt in het origineel ook af voor een steile rand of muur; in de port loopt hij door tot de botsingscode hem stopt, dus tegen een instantie zonder hull-node stáát hij er half in. Vereist eerst `0x497a30` en de betekenis van resultaattype 3/4 | niet geport | PERSO_FRAME.md §3, PERSO_JUMP.md §2.3 |
| Grondsoort `P+0x308` wordt gelezen (voor de voetstappen), maar soort 1 (glad/ijs) past de bijdraai-ramp `0x45a850` nog niet aan | deels | PERSO_MOVE.md §6.4, FOOTSTEPS.md §2 |
| Geometrie-queries lopen nu via de kd-boom van `.gel` (vloer, push-out, zichtlijnen); instantie-hulls worden per knoop met een wereld-bbox afgewezen | opgelost (issue #9) | FORMAT_GEL.md 5 |
| Snelle afwijzing van instanties in de botsingsqueries (vloer 4000, push-out en klimstraal 3000, een port-eigen versnelling) mat vanaf de `.ins`-oorsprong in plaats van de geanimeerde wortelknoop. De pendelplatforms van W1B (model 42, instanties 605/606) rijden 4400 eenheden van hun oorsprong en terug en verloren zo hun botsing aan het verre eind: 606 aan het begin van het parcours, 605 aan het eind. Nu vanaf `node_world[0]`, zoals de renderer al cullt. Testhaak `WOODY_POSAT="T x y z"` | opgelost (issue #27) | — |
| Schaduwen die aan- en uitspringen (W1B-start en overal). Twee oorzaken: (1) de belichte C-polygonen van half belichte faces werden gecullt op hun veld `+0x08`, dat het materiaalwoord van de ouder is en geen face-index, waardoor de belichte helft van vloeren met de zichtbare sectoren aan- en uitsprong; nu gekoppeld aan de coplanaire B-face die hun zwaartepunt bevat. (2) een werper (platform, vijand) wierp alleen schaduw als hij zelf in beeld was, terwijl `0x42b380` de schaduwpas voor elke instantie draait. Testhaak `WOODY_SHOTSEQ="prefix start stap aantal"` (reeks screenshots), `WOODY_SHLOG=2` (elke frame) | opgelost (issue #29) | LIGHTING.md 2, 4 |

## Camera

| wat | status | waar |
|---|---|---|
| Broodkruimelpad (`0x423ab0`) als de camera de speler niet meer ziet | niet geport | CAMERA.md |
| Railcamera mode 8 (540) | geport, niet in situ getest | CAMERA_SCRIPT.md |
| Mode 0x80 (camera uit de animatietrack) geldt nu ook voor de deuracties 17/18, niet alleen voor cinematics — het zijaanzicht waarin Woody de deur in loopt | opgelost (issue #8) | CAMERA_SCRIPT.md §4.3 |
| Zijaanzicht mode 0x20: teken van de zijkant | onzeker | CAMERA_SCRIPT.md |
| Zijaanzicht: het vlak-slot eindigt bij gescripte actie (1040), teleport (26), cinematic (`0x44de44`) en nu ook bij de respawn (Reset `0x44ab20` → `+0x4ec = 0` bij `0x44ad22`) | opgelost (issue #39) | CAMERA_SCRIPT.md §4.2, PERSO_DEATH.md §3.4 |
| `g_cam` (vlak-slot, railpointer, doodscamera) wordt bij een levelwissel niet teruggezet; in het origineel maakt de Game-ctor een nieuwe Perso (`+0x4ec = 0`). Alleen relevant als een level eindigt terwijl het zij-aanzicht aanstaat | controleren | — |

## Objectklassen

| type | wat | status | waar |
|---|---|---|---|
| 42 | lanceerder: sjabloon 1 (rechte lijn), visual uit bericht 1002 param 18, **bommenwerper (soort 0, klasse-40-bom)** en schiet-animatie (param 7/8) geport; niet: doelzoekend, stuiteren en zwaartekracht van gewone schoten, straal tegen instanties voor gewone schoten, lintkleur mogelijk te donker | grotendeels | PROJECTILES.md, BOMB.md §6 |
| 41 | missile-visual geport (`0x4700e0`: model uit de type-41-pool met `0x46d320`-oriëntatie, lint 20×25, kop, mondingsflits, uitlaat met rook, explosie soort 2). Niet: vuurbal (`0x470af0`), lint langs het werkelijk gevlogen pad, de drie gekruiste vlamquads | grotendeels | PROJECTILES.md §5.3-5.4, §7.1 |
| 7 / 8 / 9 | schutters: geport, inclusief het missile-projectiel van type 7/8. Niet: vuurbal-visual van type 9 (nu de energiebol), obstakelsensor, dwaal-gewichten (bericht 11/19..27), bericht 11/4 reset en 11/5-6, bericht 6 (aan/uit) | grotendeels | ENEMY.md §8 |
| 13 | spook: geport (half doorzichtig, zweeft op spelerhoogte, schiet om en om, duikt). Niet: vuurbal-visual (nu de energiebol), patrouille-animaties | grotendeels | ENEMY2.md §3 |
| 12 | stilstaande bommengooier (eindbaas W2B, 1×): het bommensysteem bestaat nu (`bomb_start`, `enemies_blast`), de klasse zelf nog niet; zonder hem is W2B niet uit te spelen | analyse klaar | ENEMY2.md §4, BOMB.md |
| 10 / 11 | vliegende schutter / bommenwerper te voet: in geen enkel level gebruikt | op hoofdlijnen | ENEMY2.md §5-6 |
| 14 | baas Buzz (`0x40eb50`, `src/boss.c`): brievenbus (60), gekoppelde schotel (59), modus 1 (W1B) volledig: hoog achtervolgen, schudden, stomp met kegel, laag kwetsbaar, 5 hp, rood knipperen, explosie + rookpluim op de schotel, HUD-baasbalk, camera-schok, einde ⇒ var 3 ⇒ outro + EndLevel. Vereenvoudigd: geen obstakelsensor, geen "vrij"-test na de bol-sweep (BOSS14.md §5.1), geen treffer-sterretje, kleur/alfa van de rook gereconstrueerd. Modus 2 (W2D/W3D/WWS, hupsen) zit in dezelfde code maar is **niet in die levels getest** | modus 1 geport, modus 2 ongetest | BOSS14.md |
| 15 / 16 | bazen (ctors `0x40d850`, `0x40c730`): W2D, W3D. Geen gedrag; ze krijgen wel de zwarte contourlijn (zie Effecten) | niet geanalyseerd | ENEMY2.md §1 |
| sterf-effect | 5 sterren/belletjes boven de stervende vijand (`0x477610`) | geport | PERSO_DEATH.md §7 |
| 40 | bom: pool van 16, lont + knipperen (zwart/rood, `tint_mode`), lont-lijn met vonk, sjabloon-0-vlucht (zwaartekracht, eindeloos stuiteren tegen wereld en press-nodes, 5 frames = ligt, uitrollen), explosie r 400 op speler (Kill(6)), alle vijanden en kisten, explosiesoort 0 (flits + ring) en 1, dispenser 1090, respawn ruimt op. Niet: treffer op vijand-subtype 8/12 (geen level), trefsoort 3 van de straal, deeltjes/scherven van soort 0 (`0x476cd0`, `0x476140`), het licht `0x498790`, meevoeren op een bewegend platform (`0x437040`) | grotendeels | BOMB.md |
| 120 / 121 | kist die door een bom opengaat: anim 0 ×3, explosie soort 1, fade 0.5/s, msgmask 0x20, bericht 29 = reset | geport | BOMB_CARRY.md §3 |
| 17 | géén breekbaar object: de door een baas meegesleepte instantie (bericht 59); bommen raken hem niet. Rook op typecode-9-markers in geen level gebruikt | analyse klaar | BOMB_CARRY.md §4.2, BOSS14.md §9.2 |
| 20 | berijdbare raket: geport (opstappen, draaien, ontsteking, rechte vlucht, rood knipperen, explosie r 600 = Kill(6), respawn met fade-in, geluiden 15/16/10/11/6). Vereenvoudigd: vlam als billboard i.p.v. drie gekruiste quads, explosie alleen de twee flitsen — die wel als de negen vlakke quads van `0x4762e0` (deeltjes `0x4767f0`/`0x4764f0` niet gedecompileerd), geen camerabotsing tijdens de rit (port-keuze), startoriëntatie niet tegen het origineel geverifieerd | grotendeels | ROCKET.md |
| 21 | bomkanon (W2A/W2B/W2D): geport in de raket-automaat (draaien, bom afvuren zonder zwaartekracht, meerijden, terugdraaien in toestand 8, Kill(6) via de bomexplosie). Port-afwijkingen: Reset reset alleen nog de eigen (bereden) bom; wie de explosie overleeft (WOODY_GOD) stapt af | geport | ROCKET.md §7, BOMB.md §7 |
| 90 | omgevingsvolume: modus 0 (1501/1504) geport - de vlinders van het titelscherm, met hun dwaalgedrag, landen en vleugelslag. Niet: modus 1 (`0x47e160`, langs de grondnormaal) en 2. Alleen House gebruikt klasse 90 | grotendeels | TITLE.md §3.1, OBJECTS.md §2.4 |
| 60 | watervolume: rooster met twee golvende lagen, glimlijn, kielzog, Kill(7) geport (`src/water.c`); de plons `0x478660` (Kill(7), race-Kill en bericht 1505: druppelstrepen, rimpels, 5 kringen) geport als `game_splash` | grotendeels | WATER.md, SPLASH.md |
| 50 / 51 / 52 | laser: kern, gloed, reizende puls, bliksemboog en inslag geport; de straal stopt nog alleen op wereldpolygonen, niet op instanties | grotendeels | OBJECTS.md §2.1 |
| berichten 16/18/19 (textuurframe-override) geport; 15/17 (UV-scroll-override) niet | textuurframe-/UV-override per instantie | grotendeels | INSTANCE.md §2 |
| 1201 / 1202 | typewoord-bit 0x400 (aanvalbaar doel) | analyse klaar | OBJECTS.md §3 |
| 110 | figuurtjes van de wereldkeuze-carrousel: de port verbergt ze (zie TITLE.md §3.2). Niet geport: de pagina zelf, de carrouselhoek (`0x451890`) en de transform per frame (`0x489210`) | verborgen | TITLE.md §3.2 |

## Effecten

| wat | status | waar |
|---|---|---|
| Deeltjes: de pickup-effecten (`0x4793d0`) zijn geport met hun eigen pool. Niet: rook en de explosiedeeltjes van de raket (`0x4767f0`/`0x4764f0`) | deels | BONUS.md §2.4 |
| Voetstappen: de trigger (loopcyclus 0.38/0.9, soort 2/3 op de grondsoort) en het landingsstof staan er; de effectfuncties `0x47cba0` en `0x476140` zelf zijn niet gedecompileerd, dus beeld, levensduur en kleur zijn een reconstructie | trigger geport, beeld gereconstrueerd | FOOTSTEPS.md |
| Pikinslag `0x479c80` (bij het klimmen en bij elke treffer van de aanvalsstraal): aanroepplekken en timing zijn bewezen, de functie zelf is niet gedecompileerd, dus het pikgat in de wand (eigen pool, `hud_world_gouge`) en de houtsnippers die eruit vallen (`hud_world_chip`) zijn een reconstructie — maar niet meer de lasertreffer die er eerst voor doorging | trigger geport, beeld gereconstrueerd (issue #4) | OBJECTS.md §1.6 |
| Zwarte contourlijn (SetFlags-bit 0x20) geport. Het origineel blaast alleen hoek 0/1/2 van een quad op (`0x43c42d`); de port alle hoeken. Afstand gemeten vanaf `inst+0x60` (geanimeerde wortel, `ins_anim_centre`) en de bit komt alleen van het script, zoals in het origineel (#35) | grotendeels | MODEL_RENDER.md §7 |
| Modelrendering: bitreplicatie in de RGB565-decode (het origineel laat de lage bits 0); diepte-sortering van de additieve modelbatches (modus 3; de vervaaglijst wel, zie hieronder) (`0x428d00`, 256 emmers achter-naar-voor) | niet geport | MODEL_RENDER.md recept 9-10 |
| Vervagende instanties (berichten 56/57, o.a. de verdwijnende platforms aan het eind van W1B) waren ondoorzichtig tot fade 0.98 en verdwenen dan ineens; nu de lijst `+0x1c4` van `0x428d00`: vertexalfa `1 − fade`, achter-naar-voor in 254 diepte-emmers, per emmer eerst alleen diepte en dan SRCALPHA/INVSRCALPHA, alfatest alleen op kleursleuteltexturen. Schaduw van een vervagende werper (`0x4388e0`) niet geport | grotendeels | MODEL_RENDER.md §8 |
| Mipmaps van de `.tex`-texturen: 4 niveaus met een 2×2-boxfilter (`0x47fa60`), MIPFILTER POINT (`0x47ed62`) = `GL_LINEAR_MIPMAP_NEAREST`; zonder was de verre sterrenwand van W1B een flikkerende ruis. Niet nagebootst: de 16-bits kwantisering tussen de niveaus | opgelost (issue #38) | SKY.md §8 |
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
| Titelmenu: pagina 0/1, attract-timer, 0x1c "Are you sure?", 0x1f intro, invoerranden (Enter los / Esc los), logo-fade, iris | geport | MENU_NEWGAME.md |
| Opties (pagina 0x1b): sfx/muziek/trilling, `woodyre.cfg`; trilling doet op pc niets (ook niet in het origineel) | geport | MENU_OPTIONS.md |
| Opties die het origineel niet heeft (beeldverhouding, resolutie/4K, issue #12) | nog te doen | MENU_OPTIONS.md §9.4 |
| Load game: pagina 7 / 0xa / 2 (slotkeuze) | geport | MENU_LOAD.md §1-3 |
| Wereldkeuze-carrousel (pagina 3) en high scores (pagina 4) | zie MENU_LOAD.md §4 | MENU_LOAD.md |
| Pauzemenu 0x18 (Continue / Options / Quit); niet: 0x19 "Start again" (checkpoint-herstart, resultaat 18) | grotendeels | MENU_NEWGAME.md, `0x4057f5` |
| Resultatenscherm na een level: parasol-prop (bericht 1142) met zijn animatie, de actieketen 0x4a..0x4e op de deurvector, pagina 0x1e (iris, optellende regels, HIGH SCORE, levelnaam, CLEARED!!, tikgeluid) en de opslaanketen 6 → 5 → 0x17 → 8/9 (MENU_LOAD.md §5) zijn geport. Niet: de grijze "W" rechtsboven uit het screenshot van issue #7 (niet gevonden, RESULTS.md §2.7); de wachtpagina 0xc | grotendeels | GAMEFLOW.md §5.1-5.3, RESULTS.md |
| Baas-levensbalk | niet geport | HUD_TEXT.md |
| HNM-logo's en -films | niet geport | — |
| Toetsen uit `Woody.cfg`, joystick | niet geport | — |
| Savegame: `woodyre.sav` heeft nu byte voor byte de indeling van `Woody.sav` (4 slots); een originele `Woody.sav` wordt geïmporteerd, nooit overschreven. De port schrijft na elk level automatisch in het slot waaruit gespeeld wordt (het origineel alleen via het menu) | bewust | MENU_LOAD.md §6-7 |
| Unieke-itembits per level (`rec+0x05`) worden niet bijgehouden | niet geport | MENU_LOAD.md §6, BONUS.md |
| Jackpot-gokkast in WWS (scriptobject 349): 1173 (heeft Woody een $?), 1171 ($ −1), 1170 (leven −1) geport en bericht 10 betaalt nu meermaals uit op dezelfde verborgen levensbonus 352 (er is geen "al gepakt"-test). Draaien = pik loslaten binnen volume 0x54 vóór marker 350; uitkomst 3× symbool 0 = +5 levens, 3× 1 = +3, 3× 2 = −5. Niet: de in-/uitschuif van de $-teller | geport | MESSAGES.md, BONUS.md §2.1 |

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
