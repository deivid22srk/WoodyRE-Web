# PERSO_STATE9.md — Perso state 9: the results sequence in the hub (`0x453d90` / `0x454090`, message 1140)

Static analysis of `game/Woody.exe` (image base 0x400000; `out/disasm_full.txt`), tables read from the exe with a PE reader, level
scripts with `tools/ekodisasm.py`. Naming as in PERSO_FRAME.md / PERSO_STATE7.md: `p` = Perso, `P` = parameter block `p+0x110`,
`A` = anim controller `p+0x494`, App = `[0x4c2d00]`. Nothing here was traced live.

Related: GAMEFLOW.md §5.1..5.3 (what the sequence shows, the prop of 1142, the port's `results_update`), RESULTS.md (page 0x1e),
PERSO_FRAME.md §2.1 (the state dispatch), PERSO_DUCK.md §1.6, PERSO_LOOK.md, PERSO_SPECIAL.md.

## 0. Summary

* **What it is**: the Perso state of the results sequence after a level: lying under the parasol while page 0x1e counts, the
  cheer, the save question, the walk back into the hub. The state handler `0x454090` is a small automaton on `p+0x724` (0..5) that
  keeps chaining scripted actions (0x4a → 0x4b ... → 0x4e/0x4c → 0x4d ...) by calling the state-5 handler `0x44db50` itself.
* **Entry**: game message **1140 [door, var]** (`0x4449e3` → `0x453d90`). Every hub script sends it when the player comes back
  from a level (WWS 9 SENDs, one per door; KWS 6; SWS 6; no other level). So **every shipped hub reaches state 9**, once per return.
* **Exit**: only `0x454090` sub-state 5 (`0x4542af`: SetState(0), after the fade), or any foreign SetState (death, climbing grab -
  neither happens in a hub, §3.4).
* **Input is not blanked in the original**: the controller keeps running (the App's input step `0x402940` skips only App state 5),
  and every pre-step of Perso::Update runs with it. What that changes: ducking (invisible, but he can come out lying), the
  special-attack refusal (SoundFx 9), the look-around refusal (silent here: App state 0), the script key tests 1048..1050.
  Jump, walk and attack do nothing (no Perso_Move, attack trigger wants state 0). The port blanked the whole input; it now
  passes it through (§5).

## 1. All writers (certain)

`SetState 0x44c980` is the only writer of `+0x21c` in the Perso class (PERSO_STATE7.md §1.1). Its callers that push 9:

| address | function | caller | what |
|---|---|---|---|
| `0x453e05` | `0x453d90` | message 1140 (`0x444a2d`) | action 0x4a with the door vector, `+0x724 = 0`, `+0x728 = var` |
| `0x453fe9` / `0x454010` | `0x453fc0(n != 0)` | page 0x1e result 5 (`0x405919`) | cheer 0x4e (sub 2) / shrug 0x4c (sub 3) |
| `0x454041` | `0x454020` | `0x45414f` (sub 2/3 done) | action 0x4d, sub 4 |
| `0x45407b` | `0x454050` | page 6 "No" / done (`0x405367`, then App_SetState(1)) | fade-out 0.5 s (`0x401480`), `+0x744 = 0.5`, sub 5 |
| `0x4540f9..0x454105`, `0x454131`, `0x45418b`, `0x4541b9` | `0x454090` | itself | the action restarts of sub 0/1/4/5 |

Every one of them first starts the action with `0x44dda0(act, p+0x72c, 0)`, which does its own SetState(5), so `+0x220` (previous
state) is always 5. No other caller pushes 9 (the rest push 0, 3, 4, 5, 6, 7, 8 or a zeroed `edi`/`ebx`); `0x44c9f0` (restore
`+0x220`) is only called from state 3, which is only entered from 0 or 6. The page 0x1e handler (`0x4058cd`) dispatches on `+0x724`,
not on the Perso state: its OK calls `0x453fc0` whatever state the Perso is in.

Level use: `tools/ekodisasm.py` on the 28 `code` files, SEND ids from the pushes: 1140 only in WWS (9), KWS (6), SWS (6) - one per
door, in the object that reads `GetPrevLevel` (GAMEFLOW.md §4.5); 1142 (the prop) in WWS and SWS once, not in KWS (Knothead gets no
parasol: `+0x748` stays 0 and every prop call is skipped).

## 2. The frame: `0x454090` (certain)

Called from the dispatch of Perso::Update (`0x44b812`, table `0x44b950` slot 9), `doPost = false` (`0x44b80b`: no MoveCollide, no crush test).
Jump table `0x4542c4`: 0 → `0x4540c1`, 1 → `0x45410a`, 2/3 → `0x454138`, 4 → `0x454164`, 5 → `0x454192`.

```c
void Perso_State9(Perso *p)                                          /* 0x454090 */
{
    switch (p->sub /*+0x724*/) {
    case 0: if (p->prop /*+0x748*/) Inst_Place(p->prop, 0);          /* 0x4077f0: the parasol into its cell, every frame */
            Perso_State5(p);                                         /* 0x44db50: the action runs; at its end SetState(0) */
            if (p->state == 0) { Action(p, 0x4b); p->sub = 1; SetState(p, 9); }
            break;
    case 1: Perso_State5(p); if (p->state == 0) { Action(p, 0x4b); SetState(p, 9); } break;   /* lying, 0x4b again and again */
    case 2: case 3: Perso_State5(p); if (p->state == 0) Perso_ResultsSave(p); break;         /* 0x454020: 0x4d, sub 4, SetState(9) */
    case 4: Perso_State5(p); if (p->state == 0) { Action(p, 0x4d); SetState(p, 9); } break;   /* the save pages run */
    case 5: Perso_State5(p); if (p->state == 0) { Action(p, 0x4d); SetState(p, 9); }
            if ((p->fadeT /*+0x744*/ -= dt) <= 0) {
                if (p->prop) Inst_Remove(p->prop);                   /* 0x407850 */
                App_FadeIn(0.5f);                                    /* 0x401440 */
                Cam_Cut(2); CamMgr->0x368 = 0; Cam_SetMode(0, 0);    /* 0x41f9f0, 0x41f410 */
                SetVar(p->var /*+0x728*/, 1);                        /* 0x443ca0: the hub script goes on */
                SnapToGround(p);                                     /* 0x462990 */
                Mover_SetDir(M, p->doorP0 - p->doorP1);              /* 0x459ff0: away from the door */
                A->Reset(); A->Request(1);                           /* vt[4], vt[2](1): the idle record, prio 6500 */
                p->useRoot /*+0x550*/ = 0;
                SetState(p, 0);                                      /* 0x4542af */
            }
            break;
    }
}
/* Action(p, a) = 0x44dda0(a, p+0x72c, 0): SetState(5), placed on the door vector, camera on the action's track (GAMEFLOW.md 5.2) */
```

## 3. The rest of Perso::Update in state 9 (certain unless marked)

| step | state 9 | where |
|---|---|---|
| pre-steps `0x464ef0`, `0x465e50`, `0x457a50`, `0x44ba70`, `0x465b10`, `0x44b980`, `0x458bf0` | run (unless frozen `+0x690`) | `0x44b751..0x44b7b9` |
| climbing grab `0x464ef0` | no state test: attack just pressed facing a peckable wall within 169 would grab it (state 4) and stall the sequence; **not reachable**: the port's probe (`climb_ray`, the same 169 ray along the facing) at the results spot of all 21 hub doors (WWS 9, KWS 6, SWS 6) hits nothing at all (derived from the port's geometry) | §3.4 |
| second air action `0x465e50` | runs; on the ground (`+0x22c` = 1) it only clears its "used" flag | PERSO_JUMP.md §1.5 |
| attack controller `0x457a50` / trigger `0x457330` | idle: every SetState(9) clears `+0x5b4`, the trigger wants state 0 (`0x4573ad`); the bomb pick-up `0x463430` state 0 only | |
| ducking `0x465b10` | **runs**: `0x44dda0` sets `+0x22c = 1` (with a vector), so action 5 held goes down: sub-state, LockMove(dt), body height 61. The duck requests (1750) lose against the actions (6000), so nothing shows; the handlers never read `+0x694` | PERSO_DUCK.md §1.6 |
| look-around `0x44b980` | refused (state not 0/6). Its SoundFx 9 is only played when `App+0 != 0` (`0x44ba52`): 1140 opens page 0x1e with **App_SetState(0)** (`0x404df0`) and the App stays in 0 through the save pages until `0x405370` sets 1 together with `0x454050`: **silent** in sub 0..4, audible in sub 5 (the 0.5 s fade) | `0x44ba44..0x44ba61` |
| special attack `0x458bf0` | on the release of action 11: refused (state ≠ 0, `0x458c30`) → **SoundFx 9**, no App test (only the race riders are spared, `0x40c350` 4/5) | `0x458cb3` |
| side view `0x459c70` | only with `+0x4ec` (never in a hub) | |
| `Perso_MoveCollide 0x4624f0`, volume test, crush test | skipped (`doPost` false): no VolumeEnter/Leave while he lies there | `0x44b80b` |
| id flag 0x200 | from `+0x22c` (1) | `0x44b888` |
| animation `0x463e60` | table `0x463f14` slot 8 = `0x464630`, the state-0 function: without an attack and not ducking it requests the ground set (idle 0, prio 1100 ...), which the actions (6000) outrank; the idle variations `0x464500` are skipped (`+0x21c != 0`) and the count is not reset either (PERSO_MOVE.md §4.3). Ticks A | |
| landing ring `0x44af90` | runs (state 9 is not excluded), but `+0x22c` = 1: alpha 0 | PERSO_JUMP.md §5 |

### 3.1 Input (certain)
The App's per-frame input `0x402940` returns early only in App state 5 (waiting); in App state 0 (menu) it fills the controller as
in a game (INPUT.md §4). So the Perso sees the same keys the page reads: the jump key is also the confirm (action 0xc is set with
action 4), and **Enter**, the default look-around key (action 7), confirms on its release - which is also the look-around's
release edge; the refusal stays silent because the App is in state 0. Consequences in the original (derived from the above):
* holding **duck** (Space by default) while the results run puts him down unseen; when the sequence ends (SetState(0) in sub 5)
  with the key still held he stands in the hub lying (0x32), as after a door action (PERSO_DUCK.md §1.6);
* **releasing the special key** (RCtrl) plays the "can't" sound, as everywhere outside state 0;
* the hub script's key tests 1048/1049/1050 see the keys: in the door volume the attack key reaches 1042, which refuses (state ≠ 0).

### 3.2 Damage, death
`Hit 0x44ca00` has no state test, but no hub has an enemy. A Kill would set state 2: `0x454090` no longer runs, and the page's OK
(`0x453fc0`) would put him back into 9 (the sub-state automaton does not look at the state). Unreachable in the shipped hubs.

### 3.3 Camera
The camera belongs to the actions (the tail of `0x44dda0` puts it on each action's own track) until sub 5 cuts back to the follow
camera. The camera controller `0x459090` has no state-9 case.

### 3.4 Other exits
Death (§3.2) and the climbing grab (§3, unreachable). Messages that end any state (1044/1045 of state 7, 1040 actions) are not sent by
the hub scripts while the sequence runs (they wait for `var` of 1140).

## 4. Fields

| field | meaning | writers |
|---|---|---|
| `+0x724` | sub-state 0..5 | `0x453d90` (0), `0x4540f9` (1), `0x453fc0` (2/3), `0x454020` (4), `0x454050` (5) |
| `+0x728` | the script variable of 1140 (set to 1 at the end) | `0x453d90` |
| `+0x72c..0x740` | door vector P0 / P1: marker typecode 5 of the door instance (`0x444a0a`: `0x42f6b0(door, 5, buf, 0)`, no fallback; a door without one would pass the stale stack buffer; the port falls back to typecode 0, then the .ins facing) | `0x453d90` |
| `+0x744` | the fade-out timer of sub 5 (0.5 s) | `0x454050` |
| `+0x748` | the prop of message 1142 | `0x4449c2` |

## 5. Port (`src/main_engine.c`, `src/player.c`)

* The sequence itself was already ported as `results_begin` / `results_update` (GAMEFLOW.md §5.2); the Perso is in a scripted
  action (`player_script_action`, the port's state 5) from 1140 to the end of sub 5, with `g_res.on` standing for state 9.
* **Input**: the port used to blank the whole Perso input while `g_res.on` (`memset(&pin, 0, ...)`). It now passes it through, so
  the pre-steps behave as in the original: `duck_update(.., anim_owned = 1)` and `perso_keys_tail` run in the scripted-action branch
  of `player_update` (look refusal, special refusal with SoundFx 9, side view); jump / walk / attack are ignored there; the script
  key tests 1048..1050 see the keys.
* `Player.app_menu` = App state 0 (set by the app: `g_res.on && g_res.state < 5`, or any menu page): the look-around refusal is
  silent then (`0x44ba52`).
* Test: `WOODY_KEYS="3:V 4.5:RET 5.5:RET 14:DOWN 14.5:RET" WOODY_SNDLOG=1 WOODY_DUCKLOG=1 WOODY_ANIMLOG=1 woody WWS --prev W1A
  --stats 12 12 25 20 245 --special 4 --duck 6 14 --shot x.ppm 25` (run from a scratch directory: the port writes woodyre.cfg).
  Verified: V (look) at 3 s and every Enter give no sound, the special release at 4 s gives SoundFx 9 (ref 0x6b); the duck goes
  0 → 1 → 2 at 5.95 / 6.32 s and stays down under the actions (their records 27, 28, 30 run on untouched); "RESULTS done" at 14.5 s.
  (Right after it the hub's door cinematic freezes the Perso, and the port blanks the input there - `0x459090`'s frozen flag - so the
  duck is let go at 15.05 s; in the original the frozen Perso would keep lying until the cinematic ends.)

Not ported: the climbing grab out of state 9 (unreachable, §3), the death-and-OK revival (§3.2, unreachable).
