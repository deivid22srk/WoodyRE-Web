# Engine → script VM: gameplay events (Woody.exe, build 17-10-2001)

Status: **complete list of the engine's event sources in §6 (all callers, with the port status).** All addresses refer to Woody.exe
(imagebase 0x400000) and can be re-read with `python tools/drange.py START END` and
`python tools/funcinfo.py out/disasm_full.txt ADDR`.

Goal: precisely document which messages the engine sends to the EKO VM as a result of
gameplay, with which arguments, at which point in the frame, so that `src/main_engine.c` can
replicate this 1:1 with the API from `src/ekovm.h`.

## 0. Summary

- There are only **three kinds** of engine→VM signals: (1) volume Enter/In/Leave (+ Perso variants),
  (2) collision Press/In/UnPress (+ Perso variants), (3) `SetVar(var, value)` on a script variable.
  In addition there is a per-object **message mask** (`msgmask`, bits 0x10/0x20/0x200) that the engine
  sets/clears and that scripts read with opcode `MSGTEST`. There are no separate messages for "animation
  done", "camera done", "bonus picked up" or "timer": that all goes through volumes/collisions, `SetVar`
  or the mask.
- Actors that cause volume events: the **player** (Perso variants, `0x4303e0`) and the
  **camera** (regular variants, `0x430210`). Enemies/objects do not do this.
- Actors that cause collision events: player (Perso variants via `0x436f00`), enemies and
  projectiles (regular variants via `0x436dc0`). "Collision" means: the actor **is standing on** a
  press node (typecode 1) of an instance; it's a byproduct of the ground test (`GetHeight`).
- All signals are given within the frame before the VM tick (`0x4019c0`); the VM processes them in
  the same tick (watchers woken via `0x443d20/0x443db0` → `0x442210`).

## 1. The two engine → VM channels

1. **Volume events via the callback table** `0x5cc360[id]` (dispatcher `0x441c90`).
   The only caller of `0x441c90` is the volume test `0x430210` (with id 0x65/0x66/0x67 = 101/102/103).
   The callbacks live in `0x441ed0`:
   `0x5cc4f0 (100) = 0x441d50 SetVar` (never called anywhere), `0x5cc4f4 (101) = 0x441da0 Enter`,
   `0x5cc4f8 (102) = 0x441e70 Leave`, `0x5cc4fc (103) = 0x441e10 In`.
   The dispatcher: `if (n < 0x500 && id < 2001) callback[id](&record[0x5cc330 + 48*n], &args)`,
   `n = [0x5ce2a4]` (never changed anywhere → always 0; the record is therefore a scratch record of 48
   bytes, not a queue). The record is filled as `{id, nargs=3, inst_id, volid, actor_id}`
   (`0x441da0`: `[esi]=0x65, [esi+4]=3, [esi+8]=inst, [esi+0xc]=vol, [esi+0x10]=actor`) but only
   `vol` and `actor` are used.
2. **Direct calls** to the VM helper functions:
   - Perso volume: `0x441f00 PersoEnter`, `0x441f40 PersoIn`, `0x441f80 PersoLeave`
     (each: `slot = 0x443d10(vol & 0xffffff)` → helper(slot, actor & 0xffffff) → `0x443d20(vol)` = wake watchers);
   - collision: `0x442080 Press`, `0x4420c0 In`, `0x442100 UnPress`, `0x441fc0 PersoPress`,
     `0x442000 PersoIn`, `0x442040 PersoUnpress` (`slot = 0x443d90(col & 0xffffff)`, helper, `0x443db0(col)`);
   - variables: `0x443ca0 SetVar(var, value)` (var = id & 0xffffff; wakes watchers via `0x443ce0`);
   - message mask: `0x443e50 msgmask_set(obj, bits)` / `0x443e90 msgmask_clear(obj, bits)` /
     `0x443ed0 msgmask_test(obj, bits)`;
   - `0x443ff0 actor_leave_all(actor)`: for every volume `actor` is in (`0x4444b0`) → `0x444550 PersoLeave` + wake watchers.

## 2. Volume events (world_volume, VM id 0x03xxxxxx)

### 2.1 Where in the frame

**Player.** Frame function `0x401ab0` → `0x44b530(perso, 1)` (Perso update, `0x401d07`) → after the
movement `0x4624f0(perso)` (`0x44b859`, ground test + collision, see §3.3) → at the end
`0x462760(perso, 71.0)` (`0x462748`): test point = `perso->pos (+0x1f4)` with `y += 71.0`
(≈ the body's midpoint above the feet), then **`0x4347b0(&point, -1, perso)`** → per candidate instance
`0x4303e0(inst, point, perso)` → `PersoEnter/PersoIn/PersoLeave(volid, perso->id /*+4*/)`.

**Camera.** `0x459090` (camera controller, this+4 = perso; via vtable) → `0x41eff0(dt)` =
Camera::Update (`this = [0x4c737c]`, mode in `+0x134`). At the end (`0x41f379..0x41f3cf`): if
`[cam+0x664]` (the camera object from the `.ins`, set by the camera loader `0x498dad`; `+4` = id
`0x01000000|slot`, `+0xc` = position) is not NULL: position `cam+0x1d0..0x1d8` → `obj+0xc`,
`0x4077f0(obj, 0)` (spatial update), **`0x434740(&obj->pos, -1, obj)`** → `0x430210(inst, pos, obj)`
→ `Enter/In/Leave(inst_id, volid, obj->id, 0)` via `0x441c90`. These are the only callers of
`0x434740`/`0x4347b0`: only the player and the camera are volume actors.

### 2.2 `0x434740` / `0x4347b0 (float* pos, int unused, Actor* actor)` – finding candidates

Identical functions; only the callee differs (`0x430210` resp. `0x4303e0`).

```c
World* w = [0x4c4c0c];
int leaf = 0x408180(w, pos);        // BSP descent: 16 B nodes at w+0x14; 0x40ab10(node,pos) = plane distance;
                                    // > 0 -> child [node+8], otherwise [node+0xc]; negative child = leaf, return -1-child
Sector* s = w->sectors /*+0x1c*/[leaf];
for (i = 0; i < s->ninst /*+0x40*/; i++) {
    Instance* inst = w->instances /*+0x40*/[ s->inst_ids /*+0x44*/[i] & 0xffff ];
    if ((inst->flags /*+8*/ & 0x1f) == 1)        // object type 1 = "instance" (cameras are type 3)
        test(inst, pos, actor);                  // 0x430210 or 0x4303e0
}
```

### 2.3 `0x430210` / `0x4303e0 (this = Instance*, float* pos, Actor* actor)` – point-in-polyhedron

No bounding-radius test; per volume node the point is transformed into the node's local
space and tested against all planes. The two functions are instruction-for-instruction
identical except for the messages sent.

```c
if (inst->cell /*+0x1c*/ == -1) return;          // instance outside the world / inactive
if (inst->frame_stamp /*+0x58*/ != [[0x509adc]]) // world matrices for this frame not computed yet?
    inst->vtable[2](1);                          // refresh matrices
Model* m = inst->model /*+0xf8*/;
int nvol = m->nvol /*+0x48*/;                    // number of volume nodes (FORMAT_INS §2 #10)
for (i = 0; i < nvol; i++) {
    int nodeidx = m->vol_nodes /*+0x4c*/[i];     // 1-based
    Node* node = m->nodes /*+0x68*/ + (nodeidx - 1) * 0x90;
    // world matrix (3x4, 48 B) of the node: [[0x509adc]+0xa0][inst->node_base /*+0x5c*/ + nodeidx - 1]
    Mat34* M = &[[0x509adc]+0xa0][inst->node_base + nodeidx - 1];
    // 0x440fc0(M, sx, sy, sz, out): inverse of M with scale inst+0x4c/+0x50/+0x54
    // (out = 9 floats rows r0,r1,r2 at esp+0x24.., translation at esp+0x48..)
    Mat34 inv; 0x440fc0(M, inst->sx, inst->sy, inst->sz, &inv);
    float lx = inv.r0x*pos.x + inv.r0y*pos.y + inv.r0z*pos.z + inv.tx;   // 0x4302aa..0x430310
    float ly = inv.r1x*pos.x + inv.r1y*pos.y + inv.r1z*pos.z + inv.ty;
    float lz = inv.r2x*pos.x + inv.r2y*pos.y + inv.r2z*pos.z + inv.tz;
    int nplanes = node->npolys /*+4*/, passed = 0;
    Poly* p = node->polys /*+0x10*/;             // polygons with plane (n, d) at +8..+0x14 (FORMAT_INS §2.4)
    for (; passed < nplanes; passed++) {         // 0x430316..0x43034f
        float d = lx*p->nx /*+8*/ + ly*p->ny /*+0xc*/ + lz*p->nz /*+0x10*/ + p->d /*+0x14*/;
        if (d > 0.0f) break;                     // fcomp against [0x4a9004]=0.0: outside this plane -> outside
        p = (Poly*)(((char*)p + 2 * p->nverts /*u16 +2*/ + 0x1a) & ~3);   // next polygon
    }
    int inside = (passed == nplanes);
    uint32_t volid = inst->vmids /*+0x70*/[i];   // 0x03xxxxxx
    int was_in = 0x443e20(actor->id /*+4*/, volid);   // = eko_vol_has_actor_f1 (actor in list without Leave flag)
    // 0x430210 (camera):
    if (!was_in) { if (inside) 0x441c90(0x65, inst->id, volid, actor->id, 0); }
    else if (inside)          0x441c90(0x67, inst->id, volid, actor->id, 0);
    else                      0x441c90(0x66, inst->id, volid, actor->id, 0);
    // 0x4303e0 (player): same logic with 0x441f00 / 0x441f40 / 0x441f80 (volid, actor->id)
}
```

Note: this happens **every frame** and without a cache: as long as the player stands in the volume, an
`In` is sent every frame; the frame he leaves it gives one `Leave` (after that `was_in` is 0 because the
Leave flag is set, see `eko_vol_has_actor_f1`), and on entering one `Enter`.

### 2.4 Mapping onto the C VM

| event | exe | C API (`src/ekovm.h`) |
|---|---|---|
| camera enters a volume | `0x441c90(0x65,…)` → `0x441da0` → `0x4443f0` | `eko_vol_enter(vm, volid & 0xffffff, actor_id & 0xffffff)` |
| camera stays in the volume (every frame) | `0x67` → `0x441e10` → `0x444430` | `eko_vol_in(vm, volid, actor_id)` (warns `world_volume::MessageIn sans Enter prealable`) |
| camera leaves the volume | `0x66` → `0x441e70` → `0x444470` | `eko_vol_leave(vm, volid, actor_id)` |
| player enters | `0x441f00` → `0x4444e0` | `eko_vol_perso_enter(vm, volid, perso_id)` |
| player stays (every frame) | `0x441f40` → `0x444530` | `eko_vol_perso_in(vm, volid, perso_id)` |
| player leaves | `0x441f80` → `0x444550` | `eko_vol_perso_leave(vm, volid, perso_id)` |
| player dies/teleports/reloads | `0x443ff0(perso_id)` | `eko_actor_leave_all(vm, perso_id)` |

`actor_id` = `obj+4` = `0x01000000 | slot` (instance or camera slot in `level[0x6c]`); the VM
masks with `& 0xffffff`. The C implementation of the flags lives in `src/ekovm.c` (lines 144-191).

## 3. Collision events (world_collision, VM id 0x07xxxxxx)

### 3.1 Mechanism: "standing on a press node"

A collision is not detected with a dedicated geometry test but via the **ground test**
`0x435650` ("GetHeight", also used by the movement code): the engine looks for the ground
under a point and stores the result in globals:

| global | meaning |
|---|---|
| `0x53a554` | type of the hit: 0 = nothing (`'GetHeight return : NotFound'`), 1 = terrain (`0x4356f7`), **2 = press node of an instance** (`0x431654` / `0x431d01` in `0x431200` / `0x431840`), 3 = hull (`0x430a3e`) |
| `0x53a560` | instance hit |
| `0x53a58c` | index in `model->press_nodes` (S+0x5c) of the node hit |
| `0x53a568` | height (y) of the hit point |
| `0x4b3108..0x4b3114` | plane (normal, d) of the hit point |

Which instances participate: the frame function builds, every frame (`0x401c6a..0x401cbc`), the list
`0x4c3bb4[0x4c4bec]` (max 0x3ff) of instances with `flags&0x20` (active), type 1 and
`model->npress /*+0x58*/ != 0`.

### 3.2 `0x436dc0(this = Probe*, float tol, float* pos, int unused, uint32 actor)` – generic

`Probe` is a small struct in the owner (+0 flag, +8/+0x14 contact info, **+0x20 = current
collision id or -1**). Returns 1 if the point is on/near the ground, otherwise 0.

```c
0x435650(pos, tol, 1);                                   // GetHeight below pos
if (pos->y - (hit_y + tol) < 0) {                        // foot is (almost) on the ground
    if (hit_type == 2) {                                 // ground = press node of an instance
        0x436d80(this, hit_inst, hit_idx, pos);          // store contact info (+0x431700)
        Model* m = hit_inst->model;
        if (m->ncol /*+0x60*/ != 0) {
            Node* node = m->nodes + (m->press_nodes /*+0x5c*/[hit_idx] - 1) * 0x90;
            uint32_t f = node->flags /*+0*/;
            if ((f & 0xff00) == 0x100) {                 // typecode 1 = world_collision
                int k = (int)f >> 16;                    // sub-index
                uint32_t colid = hit_inst->vmids /*+0x70*/[m->nvol /*+0x48*/ + k];   // 0x07xxxxxx
                if (this->cur == colid) { 0x4420c0 In(colid, actor);   return 1; }
                else { 0x442080 Press(colid, actor); this->cur = colid; return 1; }   // 0x436e87
            }
        }
    } else this->flag = 0;                               // 0x436d10
    /* 0x436ea9: */ if (this->cur != -1) { 0x442100 UnPress(this->cur, actor); this->cur = -1; }
    return 1;
}
this->flag = 0;                                          // 0x436ed0: in the air
if (this->cur != -1) { 0x442100 UnPress(this->cur, actor); this->cur = -1; }
return 0;
```

NB: on a transition from collision A to collision B in a single frame, only `Press(B)` is
sent, no `UnPress(A)` (`this->cur` gets overwritten).

Callers (with `actor = own instance id`):
- enemy classes: `0x410900`, `0x414f10`, `0x416a66`, `0x41a010`, `0x41a4e0` (from `0x410b78`/`0x416a10`), `0x41b030`;
- **projectiles**: `0x4490f0(dt)` (from `0x401ab0` at `0x401e7e`, after the VM tick) loops over the 50
  fixed projectile objects `0x5d7d48 + 0x104*i` (active if byte +0xe4) → `0x4493c0(obj, dt)`;
  after the fall (`0x449bc9..0x449c04`) `0x436dc0(&obj->probe, tol=obj+0x60, pos, -1, actor=obj->inst->id)`.
  If the projectile sits still for 5 frames (`+0xe8 >= 5`) it stops (`+0xec`).

### 3.3 `0x436f00` – Perso variant (player)

Byte-for-byte identical to `0x436dc0` but with `0x435b70` (wrapper around `0x435650`) and the
Perso helper functions `0x441fc0 PersoPress` / `0x442000 PersoIn` / `0x442040 PersoUnpress`.
Only caller: `0x4624f0` (Perso ground test, this = Perso, from `0x44b530`). Call at
`0x4626e1..0x4626f4`: `0x436f00(this = &perso->probe /*+0x298*/, tol = [esp+0x10] (walk threshold),
pos = copy of perso->pos (+0x1f4) with y += perso->0x1f8 offset, -1, actor = perso->id /*+4*/)`.
The result (on ground yes/no) goes to `perso+0x22c` (see §4 msgmask 0x200).

### 3.4 Mapping onto the C VM

| event | exe | C API |
|---|---|---|
| enemy/projectile lands on a collision | `0x442080` → `0x4445e0` | `eko_col_press(vm, colid & 0xffffff, actor & 0xffffff)` |
| … still there (every frame) | `0x4420c0` → `0x444610` | `eko_col_in(…)` |
| … leaves the collision (or goes airborne) | `0x442100` → `0x444650` | `eko_col_unpress(…)` |
| player lands on it | `0x441fc0` → `0x444690` | `eko_col_perso_press(…)` |
| player still there (every frame) | `0x442000` → `0x4446d0` | `eko_col_perso_in(…)` |
| player leaves | `0x442040` → `0x4446f0` | `eko_col_perso_unpress(…)` (warns `world_collision::MessagePersoUnpress sans Press prealable`) |

## 4. Other engine → VM signals

### 4.1 Per-script-object message mask (`msgmask`, opcode 58 `MSGTEST`, 59 `MSGCLEAR`)

`0x443e50(obj_id, bits)` sets bits and wakes the object's watchers; `0x443e90` clears bits (and
also wakes them). `obj_id` = `inst+4`. Bits found:

| bit | object | set | cleared | meaning |
|---|---|---|---|---|
| 0x200 | player | `0x44b89c` (in `0x44b530`, every frame if `perso+0x22c` ≠ 0) | `0x44b8bf` (otherwise) | **player is on the ground** (result of `0x436f00`) |
| 0x200 | enemy | `0x410993`, `0x41514d`, `0x416c69`, `0x41a642` (`this+0x174` bit 0 = result of `0x436dc0`) | `0x4109ab`, `0x415169`, `0x416c85`, `0x41a65e` | enemy is on the ground |
| 0x10 | player | `0x44c77c` in `0x44c730` (losing a life; sets `perso+0x278 = 2`) | `0x44b5de` (2 frames later, `+0x278` counts down) | **player has died** (2-frame pulse) |
| 0x10 | enemy | `0x411729` (`this+0x10c \|= 1`, after timer `+0x15c` runs out) | `0x4110ab` (reset), `0x41a167` (landing, `+0x174 = (…&~0x10)\|4`) | enemy state (probably "hit/ready"); exact semantics open |
| 0x20 | instance | `0x4309ac`, `0x430a64` in `0x4305c0` (sphere/segment test of a moving object against an instance's press nodes, if flag 0x10 is set in the call) | `0x430acf` (no hit) | **instance touched/pushed** by the player (presumed; `0x4305c0` is called via vtable) |
| 0x20 | bonus/switch object (class with `+0x100` = scale) | `0x451814` in `0x4517d0` (effect `0x477060`, scale 0.5) | `0x45175c` in `0x451730` (scale 100, reset) | object "taken/activated" (`0x451770` first tests `msgmask & 0x20`; if not set and distance to point < r → `vtable[0x74]`) |

C API: `eko_msgmask_set(vm, obj_id & 0xffffff, bits)` / `eko_msgmask_clear(…)`.

### 4.2 `SetVar` by the engine (all 21 callers of `0x443ca0`)

| source | var | value | when |
|---|---|---|---|
| `0x442d30` | – | – | this is the VM's own **STOREVAR opcode handler** (table `0x5d0430`), not an engine event |
| `0x441d87` | – | – | callback 100 (`0x441d50`), never called anywhere |
| `0x444a12`, `0x445275`, `0x44549d`, `0x445511`, `0x445574`, `0x4455be` | from message | 0 / 1 | **replies** to script→engine messages in `0x444870` (1082 LevelIsEnable, 1084, 1085, 1140 SaveAuto, 1173; see MESSAGES.md): synchronous in the same tick |
| `0x405029` (`0x404e90`), `0x4051b9` | `game+0x8c` (message 1160 a) | 1 | main loop: when timer `game+0x98` crosses 0 (transition/menu state), resp. in state switch `0x405ba4` |
| `0x405143` (`0x40510c`) | `game+0x8c` | 4 | when var ≠ 4: cancel timers of object `game+0x90` (message 1160 b) (`0x444380`, `0x4441d0`) and var = 4 (leaving/restarting the level) |
| `0x44eb22` (`0x44eab0`, cinematic object `level+0x64`) | `cin+0x10c` (message 1130) | `(int)(t·100)` | every frame during a **real-time cinematic**: elapsed time in 1/100 s |
| `0x44d70a` (`0x44d6e0`) | `bomb+0x12c` (the variable of message 1090) | 1 | **class-40 bomb explodes** (`+0x108 = 6`, BOMB.md); called by Boss2's crushers (`0x40e930`), by a landing projectile (`0x449c72`), and via vtable (`0x44a186`, `0x44d93e`). (Corrected: this is the bomb, not an enemy) |
| `0x44d380` (`0x44d370`, via `0x44db10`) | `bomb+0x12c` | 1 | every bomb in flight discarded without exploding (list `0x5e4880`), by Boss2 and the Perso Reset `0x44ab20` |
| `0x44d612` (`0x44d5d0`) | from message 1090 | 1 | the dispenser found no free bomb: the variable is set at once |
| `0x40d3ff` (`0x40cb80`) | `enemy+0x294` | 3 | enemy class: phase/state switch |
| `0x40e6c8` (`0x40dd30`, **Boss2**) | `boss+0x24c` | 3 | Boss2 next attack phase / death (`'Boss2 -> Vie:%f AttackPhase:%d'`) |
| `0x40fc6f` (`0x40eec0`) | `enemy+0x230` | 3 | enemy class: state reached |
| `0x410c03`, `0x410c49` (`0x410be0`) | `enemy+0x230` | -1 | **acknowledge**: script writes 1 or 2 into the var (command), engine reads (`0x443cd0`), sets state `+0x228` and writes -1 back |
| `0x454235` (`0x454164` in `0x454090`) | `perso+0x728` (message 1140 SaveAuto) | 1 | end of the player's **SaveAuto sequence** (animation 0x4b/0x4d, timer `+0x744`, fade, camera reset) |

The var ids `+0x294`, `+0x230`, `+0x24c` are set by class-specific messages
`(inst, var)` (`0x40d7aa`, `0x4100b0`, `0x40e7e3`: `var = record[0xc] & 0xffffff`); `+0x12c` and
`+0x10c` by 1130/1160-style game messages. C API: `eko_set_var(vm, var & 0xffffff, value)`.

### 4.3 `actor_leave_all` (removing the player from all volumes)

`0x443ff0(perso_id)` is called on: losing a life `0x44c730` (from the death sequence
`0x4459c0`, states 0→4), teleportation `0x44cde9` (message 26/30 on the Perso class, `'Unknow
teleportation mode !'`), `0x44ddd8` (Perso state switch via jump table `0x44dfdc`) and reloading
`SavePos.bin` (`0x44a7f5`). C API: `eko_actor_leave_all(vm, perso_id)`.

### 4.4 What does NOT exist

- No engine message for "animation done" or "camera at end of path": scripts use
  `DELAY`/`DURING` and the var replies for that.
- No engine message "bonus picked up": the bonus counters (`0x5e54e4..`, `'Woody Bonus : %d / %d'`)
  are only printed by debug key 0x16 (`0x401ad4..0x401bb9`, including `'Bonus … is outside of
  the world'` for bonuses with cell −1); picking it up itself is script work via volumes (player Enter on
  the bonus volume → script sends messages to the engine).
- Timer events come exclusively from the VM itself (`0x442350`/`0x4423a0`).

## 5. Frame order (0x401ab0, relevant to the events)

1. `0x401c6a`: build the list of active press instances (`0x4c3bb4`).
2. `0x401d07`: **Perso update** `0x44b530` → movement → `0x4624f0` (ground test → `0x436f00` →
   PersoPress/PersoIn/PersoUnpress; `0x437180` movement collision) → `0x462760` (volume test →
   PersoEnter/PersoIn/PersoLeave) → set/clear msgmask 0x200.
3. `0x401d5a..0x401da1`: matrices, level/instance updates (`0x42b400`, `0x42abc0`, `0x42b380`,
   `0x42b4e0`, `0x42ac10`; this includes the enemy updates → `0x436dc0` → Press/In/UnPress and the
   camera controller → `0x41eff0` → camera volume events).
4. `0x401e6a`: **VM tick** `0x4019c0` (`0x444050(time)`, `0x442240`, then routing outgoing messages).
5. `0x401e7e`: projectiles `0x4490f0(dt)` (→ Press/In/UnPress by projectiles; these only reach the
   scripts on the *next* tick).
6. `0x401e9a`: death sequence `0x4459c0(level, dt)` (→ `0x44c730`: `actor_leave_all`, msgmask 0x10).

For `src/main_engine.c`: each frame do the player's ground test and volume test first, then the
other actors, then `eko_tick`, then projectiles.

## 6. Every engine → VM event source, and what the port does (checked against all callers)

Callers were listed with a scan of `out/disasm_full.txt` for `call` to each VM entry point (a few lines of Python
over the call operands), and the script side with a scan of all 28 `code` files for the opcodes that read each kind of state
(`MSGTEST`, `COL_*`, `VOL_ACTOR_*`).

| source (caller) | VM call | what | shipped scripts that read it | port |
|---|---|---|---|---|
| `0x4303e0` (Perso volume test, from `0x462760`) | `0x441f00/40/80` Perso Enter/In/Leave | player volumes (§2) | all levels | ported (`player_volumes_y`) |
| `0x430210` (from Camera::Update `0x41f379`) | `0x441c90` 101/102/103 | the **camera** as a volume actor, only once a script sends message **800** (`CamMgr+0x664`); the object takes the camera position every frame | K2R (camera 483: volumes 72/74 → 670 camera height 10/150 and 660, volumes 136/137 → vars 33/35), S2R (camera 328: volume 14 → 670/660), W2B (camera 395) | **ported this round**: `player_volumes_actor` (player.c) with the VM's own "was inside" list (`0x443e20`), called after `cam_update`; log `WOODY_CAMVOL=1` |
| `0x436f00` (Perso ground probe, from `0x4624f0`) | `0x441fc0/0x442000/0x442040` | player on a press node with a world_collision (§3.3) | race boosters (`COL_B3_BIT0` in K1R..S3R), W3B tiles 81..84, `COL_FLAG5` | ported |
| `0x436dc0` (generic probe): enemies `0x410900`, `0x414f10`, `0x416a66`, `0x41a010`, `0x41a4e0`, `0x41b030`, Buzz `0x428ce0` path, bombs `0x4493c0` | `0x442080/0x4420c0/0x442100` Press/In/UnPress | an enemy or a bomb standing on a world_collision | only `COL_B3_BIT0` (flags2 bit 0 = "pressed while empty") can see a non-Perso press; the tested collisions are race boosters and the W3B tiles 81..84 | **not ported** (enemy.c / bomb flight send nothing); no case found where an enemy or bomb reaches those collisions, unverified |
| `0x41ac11` in the enemy message-11 handler `0x41a78b` | `0x442100` UnPress | an enemy given a new path by message 11 leaves its current collision | as above | not ported (enemy.c owns message 11) |
| `0x44b89c` / `0x44b8bf` (Perso update) | msgmask 0x200 set/clear | player on the ground | **none** (no `MSGTEST 0x200` in any script) | ported (harmless) |
| `0x410993`, `0x41514d`, `0x416c69`, `0x41a642` / `…ab`, `0x415169`, `0x416c85`, `0x41a65e` | msgmask 0x200 | enemy on the ground | none | not ported (not needed) |
| `0x44c77c` / `0x44b5de` | msgmask 0x10 | player lost a life (2-frame pulse) | `MSGTEST 16` on the player in W1B, W2B, W2D, W3B, W3D, WWS and on the race riders of K1R..S3R | ported (player.c) |
| `0x411729` / `0x4110ab`, `0x41a167` | msgmask 0x10 | enemy "dead" flag (`+0x10c \|= 1`) | only on the W2B bomb thrower (type 12, slot 533) → 1083 | ported for type 12 (enemy.c); other types not needed |
| `0x4309ac`, `0x430a64` / `0x430acf` (`0x4305c0`) | msgmask 0x20 | instance touched by a moving sphere (`0x4305c0` has no direct caller; dead code per OBJECTS.md §7) | none | not needed |
| `0x451814` / `0x45175c` (chests 120/121) | msgmask 0x20 | chest opened / reset | `MSGTEST 32` on W2B chests 506, 507, 523, 545 | ported |
| `0x443ff0` from `0x44c730`, `0x44cde9`, `0x44ddd8`, `0x44a7f5` | leave_all | the player leaves every volume (death, teleport 26, state switch, SavePos load) | implicit | ported for death and 26; state switch `0x44ddd8` and SavePos not |
| replies in `0x444870` (1082, 1084, 1085, 1140, 1173, 1042, 1048..1050) | SetVar | answers to script messages | – | ported |
| `0x405029`, `0x405143`, `0x4051b9` | SetVar | House intro variable (1160) | House | ported |
| `0x44eb22` | SetVar | real-time cinematic start: `var = ftol(t0·100)` (t0 = the /Rtc/ stream position, 0) — once, not every frame (corrects §4.2) | every cinematic (1130) | ported |
| `0x44d70a`, `0x44d380`, `0x44d612` | SetVar | bomb variables (above) | W2A/W2B/K2A/S2A dispensers | ported |
| `0x40d3ff`, `0x40e6c8`, `0x40fc6f`, `0x410c03`, `0x410c49` | SetVar | boss mailboxes (Boss16, Boss2, Buzz; BOSS14.md §7, BOSS15_16.md) | W1B, W2D, W3D, WWS | ported (boss.c) |
| `0x454235` | SetVar | end of the results sequence (1140) | hubs | ported |
| `0x441d87` (callback 100), `0x442d4c` (STOREVAR) | SetVar | not engine events | – | – |

So the only engine → VM event that a shipped script waits for and the port did not raise was the camera actor of
message 800; the remaining gaps (non-Perso collision presses, enemy msgmasks, leave_all on a Perso state switch) have
no reader in the scripts that could be found statically.

## 7. Open questions

1. `0x4305c0` (sphere/segment vs press nodes, msgmask 0x20 on the instance) is called via a vtable;
   the exact caller (presumably the player's movement collision `0x437180`/`0x437040`) and the
   meaning of flag 0x10 in the call have not been verified (the exe isn't in the repo,
   so vtables can't be looked up).
2. The message ids that register `enemy+0x294 / +0x230 / +0x24c` (class-specific cases
   `0x40d7aa`, `0x4100b0`, `0x40e7e3`) have not been determined (jump tables live in data).
3. Semantics of msgmask 0x10 on enemies (`0x411729`).
4. `0x441e10` (callback 103, In) does not mask the actor id with `0xffffff` before `0x444430`
   is called; `0x444430` masks it itself anyway (see `eko_vol_in`). No functional difference.
5. The volume test uses `inst->cell == -1` as the exclusion criterion; whether deactivated
   instances (message 6 with on=0 → `0x407850`) therefore also give no events is plausible but
   not checked.
6. The 50 projectile objects (`0x5d7d48`, 0x104 B) — which class creates them (bombs/throws) has
   not been determined; only the Press behavior has been traced.
