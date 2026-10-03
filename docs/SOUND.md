# SOUND: sound manager, script messages 1600..1657, music (Music.bf), engine sounds, animation events

Static analysis of `Woody.exe` (image base 0x400000). Every fact has an address; "uncertain" = not hard-proven.
See also MESSAGES.md (routing), RCK.md (bank/`RckGet`), CINEMATIC.md (rtc streams), FORMAT_INS.md (event tracks).
Helper scripts for this analysis lived in the scratch folder (not in the repo); the Music.bf reader in §4.2 was verified against
`extract/Music.bf` (directory uses exactly 1417 bytes, all 49 entries start with `RIFF` and
`RIFF-size + 8 == entry-size`).

## 0. Summary

* There are **three objects**:
  1. the *sound system* `[0x5e81b0]` = `app+0x1c` (0x10c B, ctor `0x4691a0`, vtable `0x4abaa0`): a wrapper around the
     Cryo Sound Library (lib object inline at `+0x24`), reads the cfg globals and creates the manager (`vt[0x3c]` = `0x4694b0`);
  2. the **sound manager** `[0x4c2dd8]` = `[0x5e61a4]` = `app+0x20` (0x1b950 B, ctor `0x4696d0` → base `0x4699e0` → `0x4687c0`,
     vtable `0x4abb08`; intermediate class `0x4abbb8`, abstract base `0x4ab994`). This is the target of script messages
     1600..1657 (`0x467fa0`) and of the music/rtc calls;
  3. the **SoundFx queue** `[0x5e48c8]` (0x20 B, `0x44e700` → `0x4670d0(0x20, 0x20)` → `0x468910`, vtable `0x4ab8ec`):
     engine code calls `0x468a00(id, inst)` with an *event id* 0..67; the table `0x5e5b28` (24 B/record, filled by the
     static initializer `0x4661a0`) translates it into a sound from the **character bank** (bank 0) + volume + loop + chain.
* Sound id in scripts/records/animation events = **RckGet ref** `bank<<24 | type<<16 | index` (`0x46c330` → `0x441580`).
  Scripts use `0x0100xxxx` = sound xxxx of the **level bank**; the engine table and the animation events use
  bare indices = bank 0 = `Common/<character>.rck` (6413 of the 6422 animation events; 9 with `0x0100xxxx`).
* Volume is **0..100** (clamped in `0x469c6b`/`0x469ded`/`0x46a078`), linear: the lib sets `mB = −2000·log10(100/v)`
  (`0x48bf50`, `0x4abec0` = −2000) ⇒ amplitude = v/100; then × sfx master volume `[0x5e81ec]` = cfg/100.
* 3D: in this build always **DirectSound3D** (`mgr+0x34` stays 0, only writer `0x469a54`): gain `dmin / min(max(d, dmin), dmax)`
  (DS3D rolloff 1), pan from the direction to the source on the listener's right axis = **screen right** (§2.3, verified
  statically). The engine also contains a dead software path (`0x46ba44`) with the same gain law and pan = `100·dot(right, dir)`,
  but it takes `dir` from the *absolute* source position (bug, never executed). Distances in **meters = world units × 0.01** (`0x4a94f8`).
  `dmax = 50·dmin`, cutoff distance `= 10·dmin` (a loop fades out over 0.5 s and is released, and comes back with a 0.2 s fade-in;
  a one-shot is cut and resumed at once). Default `dmin = 2.0 m` ⇒ audible up to 20 m = 2000 units. No doppler (velocity never set).
* **Listener = the camera** (`CamMgr+0x1dc`, set every frame by `0x4019b6`). **No occlusion**: the per-frame line-of-sight
  test `0x428cf0` is a stub (`xor eax, eax; ret 0x10`), so the "muffled" fade states 3/4 never occur (§2.1). What does silence
  sounds behind walls is the **instance list**: only 3D voices of instances in this frame's list `world+0x64` (the camera's `.vis`
  sectors and floor groups, `0x42a980` → `0x42a840`, INSTANCE.md §4.1) are processed; the others fall silent (§2.2 step 3). A
  stationary emitter (clock speed 0, e.g. a waterfall or machine decor) also drops out while its bounding sphere is off screen.
* **Queued plays** (1603..1605, 1611..1615, 1617..1619, 1624..1626, 1636..1638): a 2D voice waits behind the newest 2D voice,
  a 3D voice behind the instance's newest 3D voice (§2.5).
* Max **24 physical** voices (`0x46c160`: `mgr+0x3c < 0x18`), 512 logical (`0x46aa20`), no priorities: when full, a
  new 3D voice is not started (stays logical and retries every frame), a 2D voice stays in the wait list.
* **Music**: message **1655 [track]** = `PlayMusic(track)` (`vt[0x48]` = `0x469870` → `0x46c850`); `track` = index in the
  name table `0x4b73a0` (49 names, counter `0x4b7464`) → file in `Music.bf`. Every level script sends this in object 0.
  Looping stream (flag 3), starts at full volume; stopping fades via `vt[0x4c]`. Title: engine `0x404e30` (alternates
  Menu02/Menu).
* **Footsteps and most Perso/enemy sounds are type-4 animation events** in the `.ins` (`0x43a8f0` → `0x4695f0`),
  no code involved.

## 1. Script messages 1600..1657 (`0x467fa0`, jump table `0x4686b0`, 58 entries)

Message block: `msg[0]` = id, `msg[2..]` = arguments (`[esi+8]`, `+0xc`, …). `inst` = instance ref: `[0x50944c]->0x6c[arg & 0xffffff]`
(bare slot numbers also work). Scale factors: `·0.01` = `0x4aa0ac`, `·(−0.01)` = `0x4ab990`. Constants: `1e10` = `0x501502f9`,
`1e13` = `0x551184e7`: both are the **maximum play duration of a loop in seconds** (`voice+0x10`; test `start + dur ≤ now`
⇒ "Killing by signal", `0x46b169`) = "infinite". There is no other difference between 1e10 and 1e13.

Parameter **f** (pitch) has two forms (`0x469cbd`/`0x469e31`/`0x46a0bd`):
* `f > 0` → `0x46ac10`: playback frequency = `f × samplerate` (f = 1 ⇒ `voice+0x18 = 0` = original);
* `f ≤ 0` → `0x46ac80(−f)`: **−f is the desired duration in seconds**: factor = `natural duration / (−f)`.
  Messages with "dur" below send `arg·(−0.01)`, so `arg` = desired duration in 1/100 s.

Manager functions (all via `0x469c10` (2D), `0x469d60` (3D one-shot), `0x46a010` (3D loop)):

| vt | address | signature |
|---|---|---|
| +0x24 | `0x469b80` | Play2D(id, inst, f, vol) – one-shot, immediate |
| +0x28 | `0x469bb0` | Play2DLoop(id, inst, f, vol, maxdur) – `voice+8 = 2` |
| +0x2c | `0x469b50` | like +0x24 but **queued**: starts only once the previous 2D voice (`mgr+0x30`, chain `voice+0x40`) is done (`0x46af41`) |
| +0x30 | `0x469be0` | like +0x28, queued |
| +0x34 | `0x469d30` | Play3D(id, inst, queue, vol, f) with default dmin `vt[0x14]()` = 2.0 → `vt[0x38]` |
| +0x38 | `0x469d60` | Play3D(id, inst, queue, f, vol, dmin) – one-shot; `queue=1`: behind this instance's chain (starts once the previous one is done, `0x469ebe`), `queue=0`: parallel |
| +0x3c | `0x469fd0` | (not used by scripts) 3D loop with default dmin and vol 100 |
| +0x40 | `0x46a010` | Play3DLoop(id, inst, queue, f, maxdur, vol, dmin) – `voice+8 = 3` |

Usage = static count of `SEND` across all 28 `extract/Data/*/code`.

| id | args | usage | call | meaning |
|---|---|---|---|---|
| 1600 | id, vol | 54 | `vt[0x24](id,0,1.0,vol)` `0x467fe4` | 2D one-shot |
| 1601 | id, vol, f | 0 | `0x468001` | 2D, pitch f·0.01 |
| 1602 | id, vol, dur | 101 | `0x468026` | 2D, stretched to dur/100 s (usually 100; also 300/330/500) |
| 1603..1605 | as 1600..1602 | 0 | `vt[0x2c]` | 2D queued |
| 1606 | id, vol | 78 | `vt[0x28](id,0,1.0,vol,1e13)` `0x4680b2` | **2D loop** (vol typically 5..25) |
| 1607 / 1608,1610 | id, vol, f / id, vol, f, t | 0 | `0x4680d4` / `0x468107` | 2D loop with f; 1608/1610 with maxdur t·0.01 |
| 1609 | id, vol, dur | 14 | `0x4680dd` | 2D loop stretched to dur/100 s (KWS/SWS: 40) |
| 1611..1615 | as 1606..1610 | 0 | `vt[0x30]` | 2D loop queued |
| 1616 | inst, id, vol | 0 | `vt[0x24](id, inst, 1.0, vol)` `0x4681c0` | 2D with owner (inst is only stored in `voice+0x64`) |
| 1617 / 1618,1619 | inst, id, vol / +dur | 0 | `vt[0x2c]` | same, queued; 1618/19: f = −arg3 (note: no ·0.01 here, `0x46820a`, so arg3 = seconds) |
| **1620** | inst, id, vol | 507 | `vt[0x40](id,inst,0,1.0,1e10,vol,2.0)` `0x468236` | **3D loop**, dmin 2 m |
| 1621 | inst, id, vol, f | 37 | `0x468273` | 3D loop, pitch f·0.01 |
| **1622** | inst, id, vol | 220 | `vt[0x34](id,inst,0,vol,1.0)` `0x4683f4` | **3D one-shot**, dmin 2 m |
| 1623 | inst, id, vol, f | 9 | `0x468406` | 3D one-shot, pitch f·0.01 (150 = ×1.5) |
| 1624 | inst, id, vol | 1 | `0x46843a` | 3D one-shot, queued on the instance |
| 1625 / 1626 | inst, id, vol, f / dur | 0 | `0x468443` / `0x468450` | same, queued |
| **1627** | inst, id, vol, dur | 314 | `0x468420` | 3D one-shot, stretched to dur/100 s (110, 150, 100, 200 …) |
| **1628** | inst, id, t | 194 | `vt[0x58](id, inst, t·0.01)` `0x4685ab` → `0x46a670` → `0x46a2b0` | **stop** the 3D voices of *(inst, id)*: playing voices fade out over t/100 s (state 5; `t < 0.01` ⇒ immediate, flag `voice+5`), not-yet-playing/queued nodes of that sample are removed. Key = **(instance, sample)** |
| 1629 | inst, id, vol, dur, t | 2 | `0x4682b8` | 3D loop stretched to dur/100 s, maxdur t/100 s (t < 0 ⇒ 1e10), dmin 2 m |
| **1630** | inst, id, vol, dmin | 239 | `vt[0x40](id,inst,0,1.0,1e13,vol,dmin·0.01)` `0x468332` | 3D loop with custom dmin (300..1000 ⇒ 3..10 m) |
| 1631 | inst, id, vol, f, dmin | 22 | `0x468354` | + pitch |
| 1632 | inst, id, vol, dur, t, dmin | 6 | `0x4683a0` | + stretched to dur/100 s, maxdur t/100 s |
| 1633 | inst, id, vol, dmin | 17 | `vt[0x38](id,inst,0,1.0,vol,dmin·0.01)` `0x46848a` | 3D one-shot with dmin |
| 1634 | inst, id, vol, f, dmin | 67 | `0x4684ac` | + pitch |
| 1635 | inst, id, vol, dur, dmin | 23 | `0x4684d6` | + stretched to dur/100 s |
| 1636..1638 | as 1633..1635 | 0 | `0x468500`, `0x46851d`, `0x468564` | queued on the instance |
| 1639..1645, 1647, 1648, 1651 | – | 0 | `0x4686aa` | nothing |
| 1646 / 1656 | t | 0 | `vt[0x4c](t·0.01)` `0x468671` | **StopMusic** with fade t/100 s (0 = immediate) |
| 1649 / 1650 | – | 0 | `0x41fa40` / `0x41fa50` on `[0x4c737c]` | set / clear `CamMgr+0x290`, which **nothing reads** (SETUP.md 5); port: a flag (`g_cam_hold`) |
| **1652** | id, t, mask, 0 | 84 | `vt[0x80](id, t·0.01, mask, x)` `0x4685dc` → `0x46c390` | **stop 2D**: all playing non-3D voices of this sample fade out over t/100 s and stop; mask 3 = all, **1 = loops only**, 2 = one-shots only. Key = **id only** |
| 1653 | type, freq, amp | 0 | `vt[0x64]` + `vt[0x68]` `0x468603` | LFO for the *next* voice (volume resp. pitch modulation, waveform 1..4: sine/|sine|/square/noise `0x4698f0`), then `0x41fa40` |
| 1654 | type, freq, amp | 0 | `0x46862c` | enters after 1653's `vt[0x64]` call: only `vt[0x68]`, then `0x41fa40` as well |
| **1655** | track | 26 | `vt[0x48](track)` `0x468689` | **PlayMusic** (§4) – *not* StopSound as MESSAGES.md said |
| 1657 | t | 3 | `vt[0x6c](t·0.01)` `0x468698` → `0x46c500` | fade-in time for the **next** started voice (`[0x5e8230]` → `voice+0x54`; cleared on every Play, `0x469c23`) |

Typical script patterns: `1620 inst id vol` … later `1628 inst id 10` (fade loop out over 0.1 s); `1606 id 5` … `1652 id 20 1 0`.
A volume/pitch *update* message doesn't exist in the scripts (the manager does have `vt[0x5c]` SetVolume and `vt[0x60]` SetPitch,
used only by engine code).

## 2. The manager `[0x4c2dd8]` (vtable `0x4abb08`)

| vt | address | function |
|---|---|---|
| +0x04 | `0x46a7e0` | **Update(a, instList, n)**: clock (`0x468840`: `mgr+0x14` = now = `[0x509adc]+0x30`, `+0x1c` = dt), debug text, if sfx on (`[0x5e81b4]==1`) `0x46a8a0`, if music on (`[0x5e81b8]==1`) `0x46c760`. Skipped if `mgr+0x10 == 0` (pause) |
| +0x08..+0x1c | `0x4688a0..f0` | get/set default dmin (`mgr+4` = 2.0), dmax (`+8` = 2000.0), cutoff (`+0xc` = 10.0) (`0x4687d4`) |
| +0x20 | `0x46b020` | SetInstanceCount(n): table `mgr+0x1b910` (per-instance list of 3D nodes); on level load `0x40459a` |
| +0x24..+0x40 | | Play variants, §1 |
| +0x44 | `0x46cbf0` | separate music update (options menu `0x4602f2`) |
| +0x48 | `0x469870` | PlayMusic(track) (temporarily switches the lib to the BigFile `mgr+0x1b94c`, `0x491080`) |
| +0x4c | `0x46c940` | StopMusic(fade) |
| +0x50 / +0x54 | `0x46c9f0` / `0x46ca90` | PauseMusic(fade) / ResumeMusic(fade) – pause menu `0x4053e9` / `0x405698` with 0.5 s; cinematic 0.45 s |
| +0x58 | `0x46a670` | Stop3D(id, inst, fade) |
| +0x5c | `0x46a430` | SetVolume(id, instIdx or −1, vol): `voice+0x44` of all voices with this sample (−1 = all 2D, logical + physical) |
| +0x60 | `0x46a500` | SetPitch(id, instIdx or −1, factor): `voice+0x1c` (multiplies the frequency, `0x46b693`) |
| +0x64 / +0x68 | `0x46c420` / `0x46c490` | LFO volume / pitch for the next voice |
| +0x6c | `0x46c500` | fade-in for the next voice |
| +0x70 / +0x74 | `0x46c190` / `0x46c240` | rck loader/release of type 0 (reads 8 B item header + 16 B `{nbytes, rate, bits, channels}` + PCM, `0x46c280`/`0x46c300`; counts `[0x5e822c]` = sound memory used, max `0x6acfc0` = 7 MB) |
| +0x7c | `0x46c340` | FadeOutAll2D(t) |
| +0x80 | `0x46c390` | Stop2D(id, fade, mask) |
| +0x84 | `0x46c5e0` | GetDuration(id) in s = bytes / bytes-per-second |
| +0x88 | `0x46c630` | **Suspend**: stops all physical voices; 2D goes back to the logical list, 3D nodes to "not playing" (pause `0x404d8f`, then `mgr+0x10 = 0`) |
| +0x8c | `0x46c6e0` | StopAll (Suspend + release all logical voices and 3D nodes) – level exit `0x4049e0` |
| +0x90 | `0x46c740` | `mgr+0x2c` = "locked" sample (3D voices of that sample survive "not processed", `0x46b4aa`) |
| +0x94 / +0x98 / +0x9c | `0x46cc00` / `0x4698b0` / `0x46cc60` | select / start / stop rtc stream (§6) |

### 2.1 Voice (0xc0 B, pool `mgr+0x908`, 512 entries; `0x46a6b0` alloc, `0x46a7a0` free)

`+8` type: 0 = 2D, 1 = 3D, 2 = 2D loop, 3 = 3D loop · `+0xc` start time · `+0x10` maxdur (loop) · `+0x14` start offset in bytes
(`0x46ab40`: a loop that becomes audible later starts at `(now − start) mod duration`) · `+0x18` frequency (0 = original) · `+0x1c`
pitch factor · `+0x20` lib handle · `+0x24` sample · `+0x28/+0x34` LFOs · `+0x40` queued 2D successor (§2.5) · `+0x44` **volume 0..100** · `+0x48` current volume ·
`+0x4c` target · `+0x50` fade state · `+0x54` fade duration · `+0x58` elapsed · `+0x5c` start volume · `+0x60` 3D node ·
`+0x64` instance · `+0x68` dmin · `+0x6c` dmax · `+0x70` cutoff · `+0x74` pan · `+0x78` distance gain.

Fade states (`0x46b530`, table `0x46b7c8`): 0 = none (`cur = vol`); 1 = fade-in `cur = lerp(start, vol, t/T)`;
2 = "killing softly" to 0 (T = 0.5 `0x4abbac`); 3 = ducked to `0.5·vol` (T = 0.25 `0x4abbb0`);
4 = back from ducked to vol (T = 0.25 `0x4abbb4`); 5 = fade out then stop (Stop messages).
State 3/4 would come from **occlusion**: `0x46b920` (per 3D voice per frame) calls `0x428cf0(world [0x50944c], &listenerPos,
&srcPos, listenerCell mgr+0x24, −1)`; blocked ⇒ state 3 (to `0.5·vol` over 0.25 s), clear again ⇒ state 4. But **`0x428cf0` is
`xor eax, eax; ret 0x10`** in this build: never blocked, so states 3/4 never happen and there is no occlusion to port.
`mgr+0x24` = `0x428ce0(listenerPos)` = FindCell (`0x40ab60`) of the camera (`0x46a9a2`), only used by that stub.

### 2.2 Per frame (`0x46a8a0`)

1. `0x46c510`: clean up voices ended by the lib (`0x48d8a0`).
2. Listener = object `mgr+0x28` = **`CamMgr+0x1dc`** (the camera Repere, CAMERA.md §1.1), set every frame by the frame
   function: `0x4019b6` = `0x4883a0(app+0x20, [0x4c737c]+0x1dc)` (a folded one-line setter `[ecx+0x28] = arg`). Position
   `+0x90..0x98` × 0.01 (`0x48f9f0`), orientation `0x48f8e0(front, top)` with **front = column 2** `(m[2],m[5],m[8])` and
   **top = column 1** `(m[1],m[4],m[7])` (`0x46a909..0x46a982`; the lib stores them at `DS3DLISTENER` `+0x78`/`+0x84` =
   vOrientFront/vOrientTop, committed by `0x48fcf0` → `SetAllParameters`). Camera space is x = right, y = down, z = forward
   (CAMERA.md §5.2), so top points *down*; that only flips elevation, the left/right axis is `top × front = column 0` (§2.3).
3. Update is called by the frame at `0x401ee7`: `app+0x1c->vt[0x1c]` (`0x469080`) → `mgr->vt[4](list = [0x509adc]+0x64,
   n = [0x509adc]+0x60, 0)` = the per-frame **instance list** (`0x42a980` → `0x42a840`, rebuilt every frame at frame step 9; INSTANCE.md §4.1,
   BONUS.md §3.1). Both this call and the listener setter only run if `[0x5e5814]+0x384 & 2`, which `0x44fe2e..0x44fe5f` sets to
   `(sfx on | music on | [0x4c2c40]) & 1` = "sound on". `0x42a840` also leaves out a stationary instance (sphere cached, `+0x88 == 1`)
   whose bounding sphere is outside the view frustum (`0x437b00`) and, **in the race levels only** (the flag of `0x42a840` = a race region
   list is given, `0x42a8c8`), one with `dist² + r² > 1.21e8` (11000 units, `0x42a907`, `0x4aa2f4`). An instance whose floor group `+0x18`
   is not stamped this frame is never listed either (`0x42a867..0x42a87d`).
   The list marks 3D nodes as "processed this frame" (`node+8 = mgr+0x38`, `0x46a9c0`); for the voice of a not-processed node:
   a **loop** fades out over 0.5 s ("Killing softly cause not processed", `0x46b19c`), then stops as inaudible (node kept); a
   **one-shot** is stopped at once and its node kept (log text "Killing cause locked", `0x46b4b4`) – unless its sample is the
   locked one `mgr+0x2c`, then the node is removed (`0x46b513`; the two log texts look swapped). Only instances of the list get
   their nodes (re)started (step 6), so a sound played on an instance outside the list never starts while it stays outside.
4. `0x46bbb0`: 2D voices: update fade, done/inaudible (`cur < 5` with target 0) ⇒ stop.
5. `0x46b080`: 3D voices: `d = |source − listener|`; `d > cutoff·100` ⇒ fade out/stop; inaudible ⇒ stop (node stays);
   maxdur elapsed or kill flag ⇒ stop + remove node (+ start the next one in the chain).
6. `0x46bcf0`: per instance in the list: clean up finished one-shot nodes (`0x46a190`: not playing and `start + natural
   duration ≤ now`), and (re)start not-playing nodes within `cutoff·100` (`0x46be50`: loops with 0.2 s fade-in `0x4abba8` at
   offset `(now − start) mod duration`; one-shots at full volume at offset `now − start`), provided < 24 voices; a playing loop
   that is still fading out (state 2) and back in range gets the 0.2 s fade-in (`0x46be05`).
7. `0x46ace0`: start waiting 2D voices.

Volume to the lib: `ftol(cur · [0x5e81ec])` (2D `0x46b791`; 3D in software mode `cur · gain(+0x78) · master` `0x46b69f`).

### 2.3 3D model

Hardware path (the only one taken): per voice `SetPosition(pos·0.01)` (`0x490100`, buffer params at `voice+0x7c`, committed by
`0x4901b0`), `SetMinDistance(+0x68)` (`0x490020`), `SetMaxDistance(+0x6c)` (`0x48ffb0`); listener rolloff/distance/doppler
factors 1.0 (`0x48f830`) ⇒ `gain = dmin/d` between dmin and dmax, 1 below dmin, constant beyond dmax. The lib's coordinate hook
`lib+0x30` is off (`0x48b82e` → `0x48f7c0(1)`; mode 2 would negate y via `0x48f810`), so world coordinates go to DS3D as they
are. **Left/right**: DS3D's right axis is `top × front` (the product that gives +x for its defaults top (0,1,0), front (0,0,1));
with top = column 1 and front = column 2 of a proper rotation that is column 0 `(m[0],m[3],m[6])` = camera-space +x = **screen
right** – the same axis the software path uses. So a source to the right of the picture is louder on the right channel.

Software path (`0x46ba44`, dead: `mgr+0x34` = 0):

```c
d = len(src - lis) * 0.01f;                 /* 0x4aa0ac */
if (dmin <= 0) dmin = 1;  if (dmax < dmin) dmax = 1e7f;   /* 0x46ba8a, 0x46baa1 (0x4b189680) */
if (d > dmax) d = dmax;
gain = (d > dmin && d < dmax) ? dmin / d : 1.0f;   /* 0x46bad8: at d == dmax the test fails -> 1.0 (unreachable: cutoff 10 dmin < dmax) */
pan  = ftol(100 * dot(normalize(right), normalize(src)));   /* 0x46bb36: reloads the raw source position, not src - lis (bug) */
```
Pan → lib (`0x48f520`): the other channel is attenuated by `−2000·log10(100/(100−|pan|))` mB (linear `(100−|pan|)/100`);
`lib+0x44` would swap the sign (0 in this build).

**Port** (`audio.c voice_geom`): the hardware model with the software path's pan law, direction relative to the listener:
`gain = dmin / clamp(d, dmin, dmax)`, `pan = dot(right, src − lis)/|src − lis|`, `L = (pan > 0 ? 1 − pan : 1)`, `R = (pan < 0 ? 1 + pan : 1)`,
right = `cam_right()` = the GL view's x axis (`render_gl.c`), i.e. screen right. The exact DS3D panning curve of the sound
card/driver is not in the exe; the linear law of `0x48f520` is the game's own choice for its software path.

`0x46b7e0` (when `[0x5e81c0]` ≠ 0): mirrors the source through the listener's median plane – normal `n = normalize(column 1) ×
normalize(column 2)` = the right axis, `src += −2·dot(n, src − lis)·n` (`0x4a9504` = −2) – i.e. **swaps left and right**
("reverse stereo"). `[0x5e81c0]` = `[0x4c2c44]` = Woody.cfg struct `+0x74` (file `+0x78`) = Detect.exe's Sound page checkbox
**"Invert Left/Right"** (control 0x475, default 0; SETUP.md 3.1). Called with every position commit (`0x46b9e1`, `0x46bf38`,
`0x46c066`), so it applies to all 3D voices. **Ported**: `voice_geom` negates the pan (the reflection keeps the distance and
flips the right component), `audio_reverse_stereo`, woodyre.cfg `reverse_stereo=`, `WOODY_REVSTEREO=0/1`.

### 2.4 Configuration (`0x4691e2..0x4692b9`, from Woody.cfg globals)

`[0x4c2c38]` sfx on → `0x5e81b4` · `[0x4c2c3c]` music on → `0x5e81b8` · `[0x4c2c40]` "Cinematic" → `0x5e81bc` (read only by
the HNM film player, `0x426a57`: film sound on/off) · OR of the three → `0x5e81e8` (lib init `0x469360`, otherwise silent) ·
`[0x4c2c44]` reverse stereo → `0x5e81c0` (§2.3) · `[0x4c2c50]` **sfx volume 0..100** → `0x5e81ec = v·0.01` · `[0x4c2c54]`
**music volume** → `0x5e81f0` · `[0x4c2c58]` third ("cinematic") volume → `0x5e81f4`: **dead**, no reader, no Detect.exe
slider, the setter `vt[0x64]` is never called · `[0x4c2c5c]` output device index → system+0x108 · `[0x4c2c68]` speaker
config 1..8 (`0x4693b0`). Setters for the options menu: `vt[0x54/0x5c/0x64]` of the system (`0x469570`, `0x4695a0`,
`0x4695d0`). Which Detect.exe control writes each field: SETUP.md 1.

### 2.5 Queued plays

* **2D** (`0x469c10` with queue = 1, vt `0x2c`/`0x30`): every 2D Play stores its voice in `mgr+0x30` (`0x469d05`/`0x469d17`).
  A queued Play with `mgr+0x30 ≠ 0` only hangs the new voice on `[mgr+0x30]+0x40` (no list); otherwise it starts like an
  unqueued one. When a 2D voice ends (lib done `0x46c5af`, or faded/killed `0x46bc35`, both → `0x46af00`) its `+0x40` voice
  gets `start = now` and goes on the wait list (`0x46af41`), and `mgr+0x30` is cleared if it was that voice (`0x46af35`).
  Stop2D (`0x46c390`) only walks the wait and physical lists, so a parked voice survives it and starts when its predecessor stops.
* **3D** (`0x469d60`/`0x46a010` with queue = 1): 3D nodes (alloc `0x46bc70`, free `0x46bca0`: `+5` playing, `+8` processed stamp, `+0xc` next in the instance's list, `+0x10` queued
  successor, `+0x14` voice) of an instance form a list (head `mgr+0x1b910[inst]`;
  queue = 0 pushes at the front, `0x469fb1`); a queued node is appended to the `+0x10` chain of the **head** node, walking
  it to the end, with `start = start_last + duration_last / pitch_last` (`0x469eda..0x469fa2`). When a node is removed
  (`0x46a5d0`: voice ended `0x46c585`, killed/maxdur `0x46b3ab`, or `0x46b523`) its `+0x10` successor takes its place in the
  list, and `0x46bcf0` starts it the same frame. Stop3D (`0x46a2b0`) removes queued nodes of that sample from the chains
  (`0x46a3e0`). Two original bugs, not reproduced: `0x46a190` (an expired, never-started one-shot at the head) drops its
  successor (`0x46a22b`), and the kill path `0x46b3cf` pushes the successor at the head a second time after `0x46a5d0`
  already did, which links it to itself.
* Only one script uses it (1624, 1 SEND in all levels, §1). The port parks the voice silently (`audio_play_q`, `wait`/`next`)
  and starts it at offset 0 when its predecessor ends or stops.

## 3. Animation events type 4 = sound (`.ins`)

`0x42f5e0` (tail of the instance's vtbl[3] update, e.g. `0x44e8df`, which only runs for the instances of the frame's list
world+0x64; it always stores `+0xc4/+0xc8/+0xcc` = frame / animation / time but only collects events when `inst+0xc4 == frame−1`,
so an instance that comes back into the list starts without a backlog) →
`0x43a880(model, animPrev, animNow, tPrev, tNow, buf, wrap)` → `0x43a8f0` collects the events of the **root node** with
`tPrev ≤ t < tNow` (on wrap: `[tPrev, end)` + `[0, tNow)`) → `0x4695f0(inst, buf, n)`.

The port once scanned every visible instance of the level: in W3B 17 far-away instances of one model (ref 0x4b) and the
script's ambient loops then filled its 96-voice pool and Woody's own plays (jump, peck, duck, splash, death) were dropped at random.
`anim_sounds` now has the list gate above, and the pool is the original's 512 logical voices (§2.1).

Order matters: the clock `0x43eee0` steps a logical-animation chain to its next part itself (slot step `0x43f0c9`) BEFORE this
scan, so when a chain part ends the scan sees `animPrev != animNow` (a new animation, events from 0) and never a wrap of the
old part. The port once stepped the chain after the scan: the clock past the end of the peck dive's get-up (`.ins` 16, logical
0xd = 16 → 0) read as a wrap and played its t = 0 sound (ref 0x19) a second time when Woody stood up (`player_anim_settle` /
`enemies_anim_settle` now run right after each instance's clock).

Record (9 dwords, FORMAT_INS.md §7.3):

| dword | type | meaning | evidence |
|---|---|---|---|
| 0 | u32 | 4 | `0x43a94f` |
| 1 | f32 | time in the animation (same unit as the keyframes) | `0x43a95a` |
| 2 | u32 | sound ref (RckGet; usually bank 0) | `0x46968a` |
| 3, 4 | f32 | chance window `[lo, hi)` in percent: one draw `r = max(0, rand01·100 − 1)` **per call** (`0x469600`), the record plays if `lo ≤ r < hi` ⇒ variants are mutually exclusive | `0x469646..0x46965f` |
| 5 | f32 | volume 0..100 (data: 50, sometimes 20) | `0x469680` |
| 6 | i32 | pitch in % (100; 90..120) × `inst->vt[0x6c]()` (base `0x403fd0` = 1.0; enemies `0x40d830` = `enemy+0x170`) | `0x469668` |
| 7 | f32 | dmin in cm (200 ⇒ 2 m) | `0x469695` |
| 8 | u32 | 0 | |

If the instance is the **Perso** (`[0x53a34c]`, set in SetTypeInstance `0x40361e`) ⇒ **2D** `vt[0x24](ref, 0, pitch, vol)`
(`0x469690`), otherwise 3D `vt[0x38](ref, inst, 0, pitch, vol, dmin·0.01)` (`0x4696b6`).
Examples from the Woody model (W1A model 0): run animation 49: t=6 and t=15 → refs 13/14/15/16 each 25 % = **footsteps**;
anim 22/26/64/67/68 → 55/56/57 (jump grunts); anim 53 → 17..20 (50 % at vol 50, 50 % at vol 20); anim 57 → 21..23; etc.
Type 5 (6 dwords) is skipped here (`0x43a955`) and is thus not a sound.

## 4. Music

### 4.1 Playback

`0x46c850(track)`: only if music is on and `0 ≤ track < 49`; if something is already playing ⇒ `vt[0x4c](0)` (stop
immediately, no crossfade); volume = music master; `0x490f30(name, 3, &handle)` opens the stream **with flag 3 = loop**
(rtc uses flag 0 = one-shot, `0x46cc38`; the exact meaning of the bits is otherwise uncertain). Volume per frame
(`0x46cb30`/`0x46cb70`): `vol = lerp(vol, target, (now − t0)/T)`, target = `[0x5e81f0]`, to the lib as `ftol(vol·100)` (`0x48f4e0`).
Level change: `RequestLevel 0x404b60` → `vt[0x4c](fade·0.9)` + `vt[0x9c]()` (`0x404b95`; 1.5 s fade ⇒ music off in 1.35 s);
same at `0x404cde`. The new track only starts via 1655 in the new level script (object 0, during init).
Pause menu: `vt[0x50](0.5)` / `vt[0x54](0.5)`. Title screen `0x404e30`: counter `app+0x54` toggles 0/1: **track 48 (Menu02)**
at 0, **track 0 (Menu)** at 1. House/Lang have no 1655 (House = title/hub scenery ⇒ menu music from the engine).

| level | 1655 track | file | | level | track | file |
|---|---|---|---|---|---|---|
| WWS / KWS / SWS (hubs) | 1 | /Game/WS.wav | | W1B | 41 | /Game/1B.wav |
| W1A / K1A / S1A, Credits | 2 | /Game/1A.wav | | W2B | 42 | /Game/2B.wav |
| W2A / K2A / S2A, Blackbox | 3 | /Game/2A.wav | | W3B | 43 | /Game/3B.wav |
| W3A / K3A / S3A | 4 | /Game/3A.wav | | W3C | 45 | /Game/3C.wav |
| K1R / S1R | 5 | /Game/1R.wav | | W2D | 46 | /Game/2D.wav |
| K2R / S2R | 6 | /Game/2R.wav | | W3D | 47 | /Game/3D.wav |
| K3R / S3R | 7 | /Game/3R.wav | | title | 48 / 0 | Menu02 / Menu |

Track 44 (/Game/2C.wav) is used by no script. Full table `0x4b73a0`: 0 Menu, 1 WS, 2 1A, 3 2A, 4 3A, 5 1R,
6 2R, 7 3R, 8 /Rtc/Menu, 9..22 /Rtc/Woody/{W1A, W1Ba, W1Bb, W2B, W2Da, W2Db, W2Dc, W3B, W3Da..d, WWSa, WWSb},
23..32 /Rtc/Knothead/{K1A, K1R, K2A, K2B, K2D, K2R, K3A, K3B, K3R, KWS}, 33..40 /Rtc/Splinter/{S1R, S2A, S2B, S2R, S3A, S3D,
S3R, SWS}, 41 1B, 42 2B, 43 3B, 44 2C, 45 3C, 46 2D, 47 3D, 48 Menu02.

### 4.2 Music.bf (CryoBF 2.01) – verified

Opened in the manager ctor `0x469762` (`Music.bf`, otherwise `<cd>\Music.bf` `0x469781`).

```
0x00 char[16] "CryoBF - 2.01.0\x1a"
0x10 u32 0, u32 0
0x18 u32 dirOffset   (306843484; the directory runs to the end of the file, 1417 B)
0x1c u32 dataOffset  (32)
dir:  u32 n; n × entry
entry: u32 nameLen; char name[nameLen] (no \0); u32 kind
       kind 1 = directory:     u32 nChildren; nChildren × entry (recursive)
       kind 2 = file: u32 size; u32 size2 (= size, uncompressed); u32 offset  → file at dataOffset + offset
```
Tree: `Game/` (16), `Rtc/` {`Knothead/` (10), `Menu.wav`, `Splinter/` (8), `Woody/` (14)}; path = `/Game/1A.wav`.
Every file is a canonical 44-byte-header WAV (PCM 16 bit stereo; `data` at +36, PCM at +44). **Not everything is 44.1 kHz**:
`3A, 3B, 3C, 3D` and **all /Rtc/** are 22050 Hz; the rest 44100 Hz. Duration: level music 103..128 s, Menu 38 s, WS 69 s, rtc 4..53 s.

## 5. Engine sounds: SoundFx `[0x5e48c8]`

`0x468a00(id, inst)` puts (id, inst) on the wait list (max 0x20, `+0x14`); `0x468ba0(dt)` (main loop `0x401ec2`, not during
pause unless `app+0xe4`) moves them to the active list (`+0x18`, 16 B: id, record*, timer, inst) and plays the record
`rec = 0x5e5b28 + 24·id` (`0x4670f0`): `{i32 ref, f32 vol, f32 pitch, f32 maxdur, i32 next, f32 delay}`:
`maxdur > 0` ⇒ loop (`vt[0x40]` with inst / `vt[0x28]` without, `0x468cbb`), otherwise one-shot (`vt[0x38]` / `vt[0x24]`);
with `inst` thus 3D (dmin 2 m), with `inst = 0` 2D. `pitch` is replaced by `this+0x1c` if that ≠ 1.0 (`0x468c82`); `+0x1c`
is set to 1.0 by the base ctor `0x468910` (`0x468951`) and never written again (SETUP.md 3.2), so the override is dead. After starting: `next < 0` ⇒ item removed (loops too: they keep running in the manager); otherwise after `delay` s
(`delay < 0` ⇒ sample duration `vt[0x84]`) moves on to record `next`. `0x468a30(id, inst)` = stop (`vt[0x58](ref, inst, 0)` or
`vt[0x80](ref, 0, 3, 0)`); `0x468980` = stop everything (level exit `0x44e7b0`).
Helper "source" object (1 flag byte, e.g. `Perso+0x4a4`): `0x468e40` = "active this frame", `0x468e50(src, inst, id, fx, vol)` starts
the loop on the first active frame, sets the volume if `vol ≥ 0` (`0x468ad0` → `vt[0x5c]`), and stops it as soon as a frame
was not active; `0x468e20` = force stop.

Table (emulation of `0x4661a0..0x4670c4`; ref = index in `Common/<character>.rck`, vol 0..100, pitch always 1.0):

| id | ref | vol | loop | callers (address of the `call`) – event |
|---|---|---|---|---|
| 0 | 0 | 50 | | `0x44f43b` bonus type 30 (extra life); `0x44b71a` 25 bonuses at full health |
| 1 | 2 | 50 | | `0x44f72a` bonus type 36 |
| 2 | 3 | 50 | | `0x44f4ce` bonus type 35 (after finishing) |
| 3 | 1 | 50 | | `0x44f616` bonus type 34 (regular bonus) |
| 4 | 108 | 50 | | `0x44b6e4` 25 bonuses collected |
| 5 | 106 | 50 | | `0x44f95c` bonus type 37 (race) |
| 6 | 4 | 50 | | `0x44d730` object/enemy destroyed (3D, inst); `0x45352c` class 20/21; BlackBox `0x484ca1..0x484df5` (the cage blast), `0x4856f3` (dynamite), 2D (BLACKBOX.md §9) |
| 7 | 11 | 100 | | `0x451e53`, `0x451ebb` lightning strike (3D) |
| 8 | 109 | 100 | | `0x451d2d` thunder announcement |
| 9 | 107 | 50 | | "not allowed": `0x44ba61`, `0x458cd5`, `0x45917b`, `0x4597b7` |
| 10 | 10 | 50 | | `0x453468` class 20/21 (projectile thrower) |
| 11 | 5 | 50 | ∞ | `0x4536ce` class 20/21, source `+0x190` |
| 12 | 12 | 53 | | `0x449c24` projectile impact (3D at `obj+0xa4`) |
| 13 | 6 | 51 | ∞ | – (no caller found) |
| 14 | 9 | 52 | | `0x4526f6` thrower fires; `0x45344a` |
| 15 | 7 | 50 | ∞ | `0x4536e8` class 20/21, source `+0x191` |
| 16 | 8 | 50 | | `0x45321a` class 20/21 |
| 17, 18 | 69, 70 | 50 | | `0x449263` projectile (`0x449130`, 2 variants); 18 also `0x487854` (BlackBox: Buzz's shot) |
| 19, 20 | 74, 75 | 25 | | `0x449283`, `0x4492a3` projectile |
| 21..24 | 17..20 | 50 | | 21..24: `0x48676b` BlackBox jump, random |
| 25→26→27→28→25 | 13, 14, 15, 16 | 50 | chain, delay 0.3 s | `0x4866c6` (source `+0x84` of BlackBox Woody, `0x486560`): his footsteps while walking; `0x468b40(25, 4)` shuffles the `next` fields of 25..28 every walking frame |
| 29..31 | 21..23 | 50 | | `0x486a2a/45/60` BlackBox landing |
| 32, 33 | 32, 34 | 50 | | `0x48671d` BlackBox duck, `0x4867cb` BlackBox death |
| 34, 35 | 55, 56 | 50 | | `0x4867b5` BlackBox hit (34..36 random) |
| 36 | 57 | 50 | | – |
| 37 | 12 | 50 | | `0x4855f7` BlackBox stone breaks |
| 38 | 9 | 50 | | `0x40e4db` Boss2 |
| 39 | 76 | 50 | ∞ (1e17) | `0x410553` (source), stopped `0x40fc9d`/`0x40fcde` – boss type 14 |
| 40..43 | 77, 78, 80, 79 | 50 | | `0x40f9e5` (40), `0x40f291`/`0x40fb78` (41), `0x40fef1` (42), `0x40fcc6` (43) – boss type 14, mode `+0x228 == 1` |
| 44 | 81 | 50 | ∞ | `0x40ddd4` start / `0x40e147` stop (Boss2, 3D); `0x410569`, `0x40fcf3` |
| 45..48 | 82, 83, 85, 84 | 50 | | as 40..43 for mode ≠ 1; 47 also `0x40e91f`; 48 also `0x40e5f5`, `0x40fcde` |
| 49 | 12 | 100 | | `0x410a77` |
| 50..54 | 86..90 | 50 | | `0x41132a`, `0x4113fb`, `0x411a56`, `0x41178f`, `0x411af2` (enemy type 12) |
| 55..57 | 96, 97, 98 | 50 | | `0x45752b..0x457552`: air attack, `0x37 + rand(0,3)` |
| 58 | 95 | 50 | | `0x465f7b`, `0x465fc4` air dash / double jump |
| 59 | 101 | 80 | | `0x4560ce` (Perso `0x456000`) |
| 60 | 102 | 15 | ∞ (1e20) | `0x456246` Perso source `+0x4a4` in state 1 (race/vehicle); stopped `0x401cdf` (cinematic), `0x44c527`, `0x456163`, `0x456bb6` |
| 61 | 110 | 50 | ∞ | `0x4546ea` source `obj+0x68` (`0x454610`) |
| 62 | 110 | 50 | | – |
| 63 | 111 | 50 | | `0x45b8ef`, `0x45baf4`, `0x45bb54` |
| 64 | 112 | 50 | | – |
| 65, 66 | 116, 117 | 50 | | `0x40d4f5` (65), `0x40cd30` start / `0x40d4e6` stop (66) – boss type 16 |
| 67 | 118 | 50 | | `0x40ced7` boss type 16 |

The event names at the enemy/menu addresses are derived from the surrounding class and partly uncertain; the addresses and
ids are solid. Damage, death, landing and walking of the Perso have **no** code call: those are animation events (§3).

## 6. Cinematic audio (rtc)

Data: `/Rtc/...wav` in Music.bf (22050 Hz stereo). Message 1130 arg 2 = track (8 = House intro, 9.. per level, table §4.1).
`vt[0x94](track)` (`0x46cc00`): stop the previous rtc, `mgr+0x1b918 = track`. `vt[0x98](&t0)` (`0x4698b0` → `0x46cc20`):
`0x490f30(name, 0, &mgr+0x1b91c)` = one-shot stream, **at full lib volume, without the music fader** (no volume is
set; uncertain whether the lib applies the music master; Detect's "Cinematic" switch `[0x5e81bc]` is not consulted, it only
gates the HNM films, SETUP.md 3.3), `t0 = 0`, returns 1. `vt[0x9c]` (`0x46cc60`): stop + close. The
level music around it is paused with `vt[0x50](0.45)` / `vt[0x54](0.45)` (CINEMATIC.md §2/§3); the Perso loop 60 is
stopped (`0x401cdf`).

## 7. Port recipe

1. **Banks**: load type-0 items of `Common/<character>.rck` (bank 0) and `<LVL>.rck` (bank 1); ref decoding
   `bank = ref>>24, index = ref & 0xffff`.
2. **Play API**: `play(ref, inst|NULL, loop, vol/100, pitch, dmin)`; pitch argument `f ≤ 0` ⇒ `pitch = duration/(−f)`.
   3D voices key on (inst, ref); 2D on ref. Get position per frame from the instance (`inst->vt[0x54]` = world position).
3. **Messages** per §1; minimally 1600, 1602, 1606, 1609, 1620..1624, 1627..1635, 1652, 1655, 1657. Stop = fade to 0 over
   t s then release; `1657` = one-shot fade-in for the next play.
4. **3D**: formula §2.3 with listener = camera (position + right vector), `dmax = 50·dmin`, let a loop lapse above
   `10·dmin` (fade 0.5 s) and restart it (fade-in 0.2 s, offset `(now − start) mod duration`) once it comes back
   into range; a one-shot is cut and resumed at once. Constant-gain pan: `L = (pan>0 ? (100−pan)/100 : 1)`, `R` mirrored.
   Max 24 voices, no stealing (the port mixes up to 96). **No occlusion** (§2.1); instead the 3D voices of owners outside the
   frame's instance list fall silent the same way (§2.2 step 3): port `audio_update(snd_owner_active)` after the draw, with
   `snd_owner_active` = `visible && listed` (`rnd_instance_list`, built at the start of the frame: `.vis` entry sectors + floor
   groups, message-34 links, the frustum test of stationary instances and the race 11000 test). Queued plays per §2.5.
5. **Animation events** type 4 fired from the animation tick (window `[tPrev, tNow)`, wrap), one random draw per tick,
   Perso = 2D, rest 3D, pitch × `enemy+0x170`.
6. **SoundFx table** §5 as a constant array; `fx_play(id, inst)`, `fx_stop(id, inst)`, chain 25..28 with 0.3 s, source helper for
   loops (start/stop on "active this frame").
7. **Music**: read the Music.bf directory (§4.2), skip the WAV header (44 B), take the rate from the header (resample 22050!),
   loop; 1655 starts immediately at music volume; level change fades out `0.9·fade`; pause 0.5 s fade + pausing; title alternates 48/0.
8. **rtc**: on cinematic start pause music (0.45 s), play the rtc stream once, then stop and resume music at the end.
9. **Pause**: suspend all sfx voices (`vt[0x88]`), skip the SoundFx tick; master volumes from Woody.cfg (×0.01).

## 8. Open questions

1. ~~Who sets the listener and who calls Update~~: answered in §2.2 (`0x4019b6`, `0x401ee7`, list `world+0x64`).
2. Exact meaning of stream flag 3 vs. 0 in `0x490f30` (assumed: 3 = loop) and whether rtc streams follow the music master.
3. ~~`this+0x1c` of SoundFx (global pitch) and `[0x5e81f4]` (third volume)~~: both dead (SETUP.md 3.2): `+0x1c` stays at
   the ctor's 1.0, `[0x5e81f4]` is written from cfg `+0x88` (Setup default 100, no Detect.exe slider) and by the never
   called `vt[0x64]`, and read nowhere.
4. The event names of the menu/HUD ids 21..37 and of the bosses (39..54, 63..67) – addresses are correct, meaning not verified.
5. ~~1649/1650~~: set / clear `CamMgr+0x290`, a flag nothing reads (SETUP.md 5); used by no script.
6. ~~Occlusion and `0x46b7e0`~~: occlusion is a stub in this build (§2.1); `0x46b7e0` = reverse stereo (§2.3), written by
   Detect.exe's "Invert Left/Right" (SETUP.md 3.1), ported. Open: the exact DS3D panning curve (driver-side, not in the exe).
7. MESSAGES.md §Sound is wrong on several points (1655 = music, 1628 = stop, 1622/1623 are 3D) and should refer to this document instead.
