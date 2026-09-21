# Voetstappen: het stap-effect `0x47cba0` en het landingsstof `0x476140`

Status: **de aanroepkant is uit de disassembly bekend, de twee effectfuncties zelf zijn niet gelezen.** In de
werkomgeving waarin dit geport is stond `game/Woody.exe` niet, dus `0x47cba0` en `0x476140` konden niet
gedecompileerd worden. Wat de port *wanneer* en *waarmee* tekent is daarom gesplitst: de trigger en de
argumenten komen uit de disassembly (PERSO_MOVE.md §4.3 en §6.4, PERSO_JUMP.md §4), het beeld zelf is een
reconstructie met de sprite-primitieven die de port al heeft. §5 zegt per punt wat nog nagemeten moet worden.

## 1. Wanneer vuurt het origineel een voetstap af?

In de grond-animatiefunctie `0x463f40` (Perso-animatietoestand, `0x463e60` → tabel `0x463f14`), tak
Mover-fase 2 = "op snelheid", dus tijdens de **loopcyclus** (logische animatie 3, `.ins`-animatie 2). De
cyclusduur is `len3 / max(0.5, clamp(M+0x44 / M+0x48, 0, 1))` (`0x436c20`), de cyclus loopt dus op halve
snelheid bij ≤ 50 % van de maximumsnelheid — de stappen volgen daardoor vanzelf het looptempo.

| gebeurtenis | constante | aanroep |
|---|---|---|
| cyclusfractie passeert **0.38** | `0x4ab278` | `0x47cba0(pos, normaal, richting, voet, soort)` |
| cyclusfractie passeert **0.90** | `0x4a94b8` | idem, andere voet |

Argumenten van `0x47cba0`: positie, grondnormaal, looprichting, links/rechts-vlag en de **soort 2 of 3**;
soort 3 als de grondsoort `P+0x308 == 2` (`0x464231`), anders 2.

Het **geluid** van een stap komt hier niet vandaan: dat zijn de type-4 events op de root-node van de
animatie (`0x42f5e0` → `0x43a8f0` → `0x4695f0`, docs/SOUND.md §3, in de port `anim_sounds` in
`src/main_engine.c`). Die staan los van dit effect en werkten al.

## 2. Grondsoort `P+0x308` (`0x4628e0`, lezer `0x46295f` = `m_nGroundType`)

Bij elke grondmeting: 0, behalve als de vloer een **wereldpolygoon** is (`[0x53a554] == 1`) waarvan het
materiaalveld `poly+8` bit 15 niet gezet heeft. Dan is de soort byte 3 van het vlaggenwoord van de
textuurgroep achter dat materiaal (`level+0x5c`, records van 0x24 B, `+0x20` = textuurobject → `tex+0x47`,
docs/FORMAT_TEX_COL_VIS_LIT.md §1):

| soort | betekenis | gebruik in het origineel |
|---|---|---|
| 0 | gewoon | – |
| 1 | glad / ijs | trage bijdraai-ramp in `0x45a850` (0.75 s / 1.0 s i.p.v. 0.25 / 0.1) — **niet geport** |
| 2 | stof / zand / sneeuw | voetstap-soort 3 i.p.v. 2 (`0x464231`), stofwolk bij landen (`0x464486`) |

Een vloer die van een instantie-node komt (lift, platform, kist) heeft geen grondsoort: die hoort bij de
wereldgeometrie.

## 3. Landingsstof

Springer-toestand 6 (`geland`, één frame, PERSO_JUMP.md §1.1) in `0x4642f0`: als `P+0x308 == 2` volgt
`0x476140(&pos + (0,30,0), &P+0x458, 3, 0.25, 1.5)` en **geen** animatie. `P+0x458` is de vloernormaal
(PERSO_MOVE.md §2, veldtabel). Hetzelfde `0x476140` maakt ook de scherven van een raketexplosie (PROJECTILES.md §5.3: explosie soort 0, "met normaal"),
het is dus een algemene deeltjesuitbarsting; alleen het eerste argumentpaar (punt, normaal) en het aantal 3
zijn hier met zekerheid te duiden, `0.25` en `1.5` niet.

## 4. Wat de port doet (`src/player.c`, `src/main_engine.c`, `src/hud.c`)

**Trigger — overgenomen uit de disassembly.** `player_update` kijkt na `anim_request` naar de fractie van de
loopcyclus (alleen bij logische animatie 3 en op de grond) en roept bij het passeren van 0.38 en 0.9
`game_footstep(pos, grondnormaal, kijkrichting, voet, soort)` aan; `phase_passed` vangt de omloop, zodat er
precies twee stappen per cyclus komen, ook bij lage framesnelheden. De grondsoort komt uit `ground_type()`,
die de materiaalindex van de laatst geraakte **wereld**polygoon (`g_ground_mat`, gezet in `world_ground`)
via `TexFile.materials[i].group` naar `TexGroup.flags >> 24` volgt. Landen op soort 2 roept
`game_land_dust(pos + (0,30,0), grondnormaal)` aan, precies waar het origineel `0x476140` aanroept.

**Beeld — reconstructie.** `game_footstep` legt een afdruk in het grondvlak (`hud_world_decal`, een sprite
zonder vlagbit 0: de quad ligt in het vlak met de meegegeven normaal, gedraaid op de looprichting en in u
gespiegeld voor de andere voet — vlag 0x40, zie de spritevlaggen in PERSO_DEATH.md) naast de Perso-positie,
22 eenheden opzij:

| | soort 2 (gewone grond) | soort 3 (stof/zand/sneeuw) |
|---|---|---|
| afdruk | grootte 30, sterkte 0.22, 0.6 s | grootte 36, sterkte 0.6, 4 s |
| stof | geen | één wolkje (beeld 14) schuin achteruit, 0.45 s |

De afdruk staat de eerste helft van zijn leven stil en vervaagt daarna. Er zijn 48 afdrukken en 64
stofdeeltjes; de oudste afdruk maakt plaats als de ring vol is. Landen spuwt 3 wolkjes rondom (het aantal
uit de aanroep), 0.5 s, naar buiten en omhoog.

Twee bewuste keuzes, omdat het origineel hier niet gelezen kon worden:

1. **Welke afbeelding.** Onbekend. De port pakt bank 0 beeld 14 (het zachte wolkje van de raketrook) en
   tekent de afdruk **vermenigvuldigend**: `dst · (1 − rgb·sterkte)`. Dat beeld is wit op zwart met alfa 1,
   dus een gewone alfablend zou een donkere *vierkant* op de grond zetten; zo blijft alleen de wolkvorm over
   als donkere veeg. `WOODY_STEPIMG=<n>` kiest een ander bank-0-beeld (het wordt dan als fx-slot 10 geladen);
   zodra bekend is welk beeld het origineel gebruikt, hoort daar waarschijnlijk ook een andere blendmodus bij.
2. **De voetafstand van 22 eenheden opzij.** Het origineel geeft alleen een links/rechts-vlag mee; waar
   `0x47cba0` de afdruk precies neerzet (en of het de voet-node van het skelet pakt) is niet bekend.

`WOODY_FXLOG=1` logt elke stap en elke landing met positie en soort.

## 5. Open punten

1. **`0x47cba0` decompileren**: beeldnummer(s), grootte, kleur, levensduur, blendmodus, of de afdruk ook op
   gewone grond (soort 2) verschijnt en wat het verschil tussen soort 2 en 3 precies is, en waar de afdruk
   t.o.v. de Perso-positie terechtkomt. Zonder dat blijft §4 een reconstructie.
2. **`0x476140` decompileren**: betekenis van `0.25` en `1.5`, het deeltje zelf (beeld, zwaartekracht,
   levensduur), en of het aantal 3 een aantal per aanroep of per seconde is.
3. **Grondsoort 1 (glad)**: `P+0x308` wordt nu wél gelezen, maar `0x45a850` (trage bijdraai-ramp op ijs) is
   niet geport — zie TODO.md.
4. **Waar staan de stap-effecten in de levels?** Met de exe erbij is `tools/funcinfo.py 0x47cba0` (aanroepers
   en strings) en `tools/drange.py` het startpunt; `tools/levelparse.py` kan per level opsommen welke textuurgroepen
   een grondtype-byte ≠ 0 hebben, en dat zegt meteen in welke levels de afdrukken te zien horen te zijn.
