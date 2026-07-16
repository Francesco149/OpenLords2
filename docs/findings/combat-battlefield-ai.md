# Tactical battlefield AI — RE for exploit + kiting-bot spec

> 2026-07-09. READ-ONLY static RE of `docs/decompiled/all.c` (retail LotR2). Maps the
> real-time **tactical battlefield** sim/AI (distinct from the strategic instant resolver
> in `combat-resolution.md`). Companions: `battlefield-render.md` (draw), `combat-battlefield-input.md`
> (player input). All refs are `all.c` line numbers. Data-section tables (`PTR_FUN_*`, `.rdata`
> stat tables) are NOT in the decompile → flagged **LEAD** (need a live/Ghidra dump).

---

## 0. TL;DR — the exploit surface

The battlefield AI is **deterministic (no RNG anywhere)**, targets the **nearest enemy by
Manhattan distance**, **locks that target for ~500 ticks**, **only reacts to enemies within
~40 hexes** (0x28) / spiral-searches only **20 hexes** from group centre, and its ranged units
**stand and shoot — they never kite**. Every one of those is a repeatable, farmable weakness.

---

## 1. The sim / AI driver chain

Main loop `FUN_004b99c0` (@88532). Each sim sub-step (the `while(local_c<=local_8)` body,
@88749) runs the combat cluster when `DAT_0057c8fc==2` && `local_c!=0` (@88791):

```
FUN_00478952   88792  decrement a global cooldown
if DAT_004eac50 < 0x2b {          // combat screens 0x28/0x29/0x2a
  FUN_0040490d  88794  (shared w/ strategic — generic per-tick)
  FUN_00488dfe  88795  GROUP AGGREGATOR — bucket 0x50 unit records → 0x50 group records
  FUN_004822ed  88796  PER-UNIT SIM DRIVER — walk units, dispatch by movement state
  FUN_00485bb1  88797  PROJECTILE/EFFECT SIM — advance arrows/impacts, apply casualties
  FUN_00489401  88798  GROUP AI DRIVER — dispatch by stance + retarget on timer   ★
  FUN_004859e5  88799  terrain/fire spread pass
}
FUN_00477dfc   88801  BATTLE-END / winner check
```

Render is the separate `FUN_004bc142` (@88581) — do not confuse.

### 1a. Per-unit sim driver — `FUN_004822ed` (@64057)
Walks unit records 1..0x50 (base `DAT_005544ac`, stride **0x1b0**). For each live unit it
dispatches its **movement/animation state** (class byte `-0x1a`=`DAT_00554492`, 0..10) through
table **`PTR_FUN_004d9140`** (@64095, ≥0xb → `FUN_0046ebe4` disband). Those state handlers
(`FUN_004825d1`.., @64133+) set movement params then dispatch the **class-specific action** via
**`PTR_FUN_004d9170`** indexed by the **order subtype** `+0x05`=`DAT_005544b1` (0..0x11). These two
tables' *contents* are data-section → **LEAD** (handler addresses undumped), but the action
BODIES are in the decompile (below).

### 1b. Group AI driver — `FUN_00489401` (@66534) — the decision core
Walks group records 1..0x50 (base `DAT_00566520`, stride **0x34**). Per live group:
- `FUN_004891ad` (@66484) recompute group bbox/centroid.
- **Stance dispatch** (@66549-66559) by stance byte `+0x08`=`DAT_00566528`, via one of three tables
  (phase-gated): `PTR_FUN_004d91b8` (stances 0-4, interactive/`DAT_0053e8fc==0`),
  `PTR_FUN_004d91f8` (0-0xb, auto-resolve), `PTR_FUN_004d91d0` (0-8). Table contents = **LEAD**.
- **Formation-refresh timer** `+0x14`=`DAT_00566534` decremented each tick; on `<1` → reset **500**
  and `FUN_00489654`(gate) → `FUN_0048970e`(assign) (@66562-66570). The engine's debug label calls
  it `re_targ`, but it does **not** universally hold one group target: the assignment function first
  tries a formation layout and invokes the enemy-cell search only when that layout cannot fit.

Debug label @15047 literally reads the string `"re_targ"` from this field. It is also set to
**0x14 (20)** on a regroup (unit died/split; @60382/60983) and **0x32 (50)** mid-engagement
(@65012), so the formation-assignment cadence varies with battle state.

### 1c. Formation assignment + conditional enemy-cell search — fully in-decompile
`FUN_0048970e` first calls `FUN_00489f9d`. A successful formation-fit takes
`FUN_00489896` and assigns members to the generated layout. A failed fit takes
`FUN_00489798`, which repeatedly calls the enemy-cell fallback below; stance 6 has a third branch.
- **`FUN_0048a38e` (@66903):** scan expanding **squares** around the group anchor, with
  `radius = 0; radius < 0x14` — radii **0..19**, not 20. Cells are visited in nested row/column
  order; the first enemy-occupied reachable cell wins (`FUN_0048a672` @66989). This is conditional
  fallback behavior, not a universal 20-hex vision system.
- **`FUN_00489a62` (@66713):** among the group's own members, pick the one nearest by Manhattan
  distance (`FUN_00404ead` = |dx|+|dy|) to the selected destination; excludes dead and already
  assigned members. The Manhattan comparison chooses the **own assignee**, not the enemy cell.
- **`FUN_00489b8d` (@66742):** the AI **order issuer** — writes that unit's fields directly
  (order subtype `+0x05`, destination hex `-0x08/-0x06`, path-reset flags). It does not go through the
  player click path.

---

## 2. Battlefield unit record  (base `DAT_005544ac`, stride 0x1b0, indices 1..0x50)

Offsets are relative to the `DAT_005544ac` anchor (record spans ~[-0x23 .. +0x196]).

| off    | w  | symbol            | meaning |
|--------|----|-------------------|---------|
| -0x23  | u8 | DAT_00554489      | **owning player id** (vs local `DAT_0057c8cc`) |
| -0x1a  | u8 | DAT_00554492      | **class/type byte** 0..10 (<7 = 7 foot classes; 7/8/9 = ranged/siege engines; 0xa special). Also movement-state dispatch idx + speed-table idx |
| -0x19  | u8 | DAT_00554493      | condition flag (fresh/tired; scales damage taken) |
| -0x17  | u8 | DAT_00554495      | "was hit" latch (set by projectile impact @70560) |
| -0x16  | i16| DAT_00554496      | last-attacker unit index |
| -0x10  | i32| DAT_0055449c      | **cell offset** (= (row*0x50+col)*8 into the grid) |
| -0xc   | i16| DAT_005544a0      | **position col** |
| -0xa   | i16| DAT_005544a2      | **position row** |
| -0x8   | i16| DAT_005544a4      | **destination/target hex col** (AI & player write here) |
| -0x6   | i16| DAT_005544a6      | **destination/target hex row** |
| +0x00  | u8 | DAT_005544ac      | active / owner-colour byte (0 = empty slot / dead-removed) |
| +0x04  | u8 | DAT_00554490      | current sprite frame (anim) |
| +0x05  | u8 | DAT_005544b1      | **order/action subtype** — dispatch idx into `PTR_FUN_004d9170`. **2=DEAD**, 3/9/10 attack modes, 4=melee-locked, 5=idle/stop, 8=advance, 0xb=re-acquire, 0xc=charge-to-hex, 0x11=FLEE |
| +0x08  | u8 | DAT_005544b4      | flags (bit1 = has-order) |
| +0x13e | u8 | DAT_005545ea      | **weapon/projectile type — 0 = MELEE, !=0 = RANGED** (value = projectile type spawned). ALSO used as morale band (tri-band 0/1-2/>2) |
| +0x13f | u8 | DAT_005545eb      | **reload PERIOD** (ticks to next shot) |
| +0x144 | i16| DAT_005545f0      | **weapon range** (`>>3` = hexes) |
| +0x146 | u8 | DAT_005545f2      | **armor / defense** (subtracted from incoming damage) |
| +0x148 | u8 | DAT_005545f4      | current **target unit index** (locked enemy) |
| +0x14c | i16| DAT_005545f8      | **group id** |
| +0x14e | u8 | DAT_005545fa      | formation slot |
| +0x150 | u8 | DAT_005545fc      | "assigned this pass" latch (target-picker bookkeeping) |
| +0x154 | i16| DAT_00554600      | **shot/windup timer** (counts up to `+0x13f`, then fires) |
| +0x156 | u8 | DAT_00554602      | **post-attack cooldown** (set 0x78 = 120 after a shot) |
| +0x16b | u8 | DAT_00554617      | elevation/hills tier (scales own outgoing power) |
| +0x16e | i16| DAT_0055461a      | **damage accumulator** (crosses T=100/160 → casualties) |
| +0x172 | i16| DAT_0055461e      | **weapon base power** |
| +0x174 | i16| DAT_00554620      | **troop count (= HP)**; <1 → order set to 2 (dead) |

Per-class stats (`+0x13e/13f/144/146/172`) are loaded at spawn (`FUN_00480599` @63356 → writes
@63490-63497) from two `.rdata` tables: per-class `DAT_004d96d0` (stride 0x14) → weapon-type idx →
per-weapon `DAT_004d97b0` (stride 0x14). **Exact numeric range/reload/power per class = LEAD** (dump
`0x4d96d0` + `0x4d97b0`).

### Group record  (base `DAT_00566520`, stride 0x34)
`+0x00` owner · `+0x02` member count · `+0x03` flag · `+0x08` **stance byte** · `+0x04/+0x06`
member index range [first,last] · `+0x0f` group shot-cooldown · `+0x14` **formation refresh** ·
`+0x16/+0x18` special high-morale **charge waypoint** (not a universal target) ·
`+0x1e/+0x20` centroid · `+0x22/+0x24` **group anchor** ·
`+0x2a` **routed flag** (`DAT_0056654a`).

---

## 3. Per-class AI behaviour + exploit hinges

### Assignment cadence — two clocks, not a 500-tick universal target lock
The interactive stance handlers advance their own `+0x1c` clock and execute a canned stance step
after 200 ticks. Separately, `+0x14` refreshes formation assignments at 500 nominal ticks (or 20/50
after battle-state changes). Live telemetry plus the decompile showed that `+0x16/+0x18` remains
zero for ordinary melee assignments; the actual destinations are per-unit `-0x08/-0x06`.
> **CORRECTION (2026-07-16):** do not describe `+0x14` as a single locked target or
> `+0x16/+0x18` as its coordinates. The attempted target-lock filming fixture exposed that bad
> model before it reached the video.

### Movement / pursuit — no hard leash, but a reaction radius
No max-pursuit-distance / return-home constant was found. There is a group-wide reaction radius
**0x28=40** in `FUN_004956cc` (@71205), while `FUN_0048a38e` has a separate 0..19 square search
used only when formation fitting fails. Do not combine those into a single visibility/leash claim.

### Ranged vs melee — the weapon field, and NO kiting
Ranged iff `+0x13e`=`DAT_005545ea != 0`. Classes **7/8/9** are the ranged/siege engines (need line
placement, `FUN_0049207e` @63464). Among foot classes 0..6 (strategic weights {2,16,8,13,9,13,22}),
**archer/crossbow = the two weight-13 rows (idx 3 & 5)** — **LEAD**, confirm against the weapon-type
column of `0x4d96d0`.
- **Melee handler `FUN_00483ce1` (@64641):** adjacency probe `FUN_00494e3f` (@71066) over the 6 hex
  neighbours → if enemy adjacent (**range 1**), lock both into order-4 melee exchange; else step in.
- **Ranged handlers `FUN_00483337` (@64479) / `FUN_00484bf9` (@64998):** range test `DAT_005545f0>>3`
  vs distance; **out of range → step toward** (only movement); **in range → STAND, aim, fire on
  cooldown**. Windup `+0x154` counts to reload `+0x13f`, fires (spawns projectile into `DAT_0057a108`
  array, type=`+0x13e`, power=`+0x172` scaled by elevation), sets **cooldown 0x78=120**.
- **NO retreat-when-enemy-near branch exists anywhere** in the ranged path (exhaustively traced).
  A ranged unit closes to range then plants; when a meleer reaches it, it gets pinned in order-4.
> **HINGE (the big one):** AI ranged units are static shooters. They will not back-pedal to keep
> spacing. YOUR archers can freely out-range and kite theirs; and you can bait their archers into
> melee by walking a decoy into them.

#### Presentation-safe ranged proof fixture (2026-07-16)

`tools/film_unit_behavior.py` now separates a logic fixture from a filmable
fixture. Overwriting the scratch combat records' class words after
`FUN_0042b7f7` is sufficient to exercise combat logic, but it is **not**
presentation-safe: battlefield entry has already selected the sprite/movie bank,
palette, and map features. Replacement classes can therefore animate through
stale resources, producing the dark palette and giant unrelated animation seen
in the rejected b20 capture.

The production preset instead enters a natural retail 28-v-28 battle, selects the
already-loaded player archer formation and enemy mace formation, and moves those
complete formations with their occupancy cells updated. Moving a single record
is invalid for this proof because `FUN_00479e90` orders at formation scope; the
remaining members and stale formation centroid keep the isolated record from
executing a coherent volley. The selected formations are staged 8–10 hexes apart
on the verified bright western meadow, inside the natural 15-hex bow range. Unit
classes, sprite banks, health, stats, targets, and every unselected army record
remain untouched.

The checked capture contract requires at least 20 aligned `shotq` frames,
observes action 3, enemy damage, zero archer return damage, and damage before
contact, and stops before emitting empty-target telemetry. Retail occasionally
clears the battle early; that short run now exits nonzero instead of becoming a
technically true but editorially weak proof. The promoted run records 47 frames,
archers 200→200 and maces 75→1 at separation 8–10.

### Attack resolution — deterministic, threshold casualties
Impact resolves in `FUN_00492c8b` (damage block @70554-70658). `dmg = power / elevation_factor
(÷5/÷3/÷2/>>2) − armor(+0x146)`, capped by weather bracket. Accrues into `+0x16e`; when it crosses
`T` (**100** vs foot, **160/0xa0** vs engine): `T..2T→−1, 2T..3T→−2, 3T..4T→−3, ≥4T→−4` troops
(@70623-70643), accumulator reset. Troop count `+0x174 < 1` → order=2 (dead, `FUN_0047a814` @61079).
Melee tick damage is the same shape at fixed weather-keyed rates (`FUN_00494908` @70963).
**NO `rand`/`srand`/LCG anywhere in the file.** The only "variable" is `DAT_00522f6c`, a weather/
ground bracket set ONCE per battle by a pure table lookup (`FUN_00404e4b` @2431). ⇒ same setup +
same orders + same tick = **byte-identical casualties, every time.**
> **HINGE:** fully deterministic → a bot can pre-compute exchanges; no "bad luck" to hedge against.

### Morale / retreat / rout — farmable
- Per-unit flee: `FUN_00489b8d` sets **order 0x11 (flee)** when morale band `0<+0x13e<3` AND being
  engaged (@66776); **order 0xc (charge-to-hex)** when band `>2` (@66782). (0xc read = high-morale
  press; **LEAD** — verify live.)
- **Group rout CASCADES:** `FUN_0048c974` (@67881) flips **every member to flee-order 0x11 at once**
  (@67899) when the group has an enemy in range 0x28; `FUN_0048c8af` (@67860) disperses (order 8 +
  routed flag `+0x2a`=1). Triggered by the stance handlers when the per-side **panic budget**
  `DAT_00553070` (incremented +3/+0x14 as casualties mount, @67221-67311) goes positive — **LEAD**
  on exact overflow.
> **HINGE:** morale is a fixed per-unit band, not a die-roll — low-morale (peasant) stacks flee
> predictably once engaged; concentrate fire to push a side's panic budget and rout the whole group.

### Group stances (byte `+0x08`) — not always advancing
Set by `FUN_00480662` deploy (@63406-63444) + `FUN_0048143b` class map (@63640): **1** aggressive-
advance (veterans), **2** default engage, **3** basic advance, **5/6/7/8** = class-7/8/9/10 standoff/
bombard (sit on waypoints), **9/10** = explicit DEFENSIVE HOLD (@63440/63444). Handlers pick pursue
(`FUN_0048c56f` @67797 "advance to halfway toward enemy"), waypoint-hold (`FUN_0048b9c1` @67533),
objective-seek (`FUN_0048bac4` @67557), or withdraw (`FUN_0048b7fc` @67473, moves AWAY from a point).
Aggressive groups mostly advance only in the early phase then anchor/withdraw under pressure.
> **HINGE:** only stance 1/2/3 groups chase a decoy. Ranged/defensive (5-10) groups won't budge —
> so pull their meleers with a decoy while their archers sit, then free-fire the isolated archers.

---

## 4. Kiting-bot spec (read → decide → order, per tick)

The AI writes unit fields directly (not via clicks), so the bot should too. The player can only
command GROUPS via the shared executor `FUN_00479e90` (@60886); AI-style PER-UNIT micro (which the
human UI cannot do) is done by poking the unit record like `FUN_00489b8d`.

**READ each tick** (per your units + visible enemies, over records base `DAT_005544ac` stride 0x1b0):
- own/enemy: `+0x00` owner, `+0x05` order (skip 2=dead), `-0xc/-0xa` pos, `+0x174` troops.
- weapon: `+0x13e` type (0=melee), `+0x144>>3` range, `+0x156` cooldown, `+0x13f` reload.
- enemy group centroid `+0x1e/+0x20`, formation-refresh timer `+0x14`, and each unit's live
  destination `-0x08/-0x06`.

**DECIDE:**
1. **Kite:** keep each own archer at `dist_to_nearest_enemy ∈ (their_range, my_range]`. Since enemy
   ranged won't advance and enemy melee blind-chases, retreat one hex whenever an enemy melee unit
   closes to `my_range-1`; free-fire otherwise.
2. **Spacing:** use player-legal formation orders to keep archers at their verified weapon range;
   do not depend on an unproven universal target-lock window.
3. **Rout-farm:** concentrate archer fire on the lowest-morale (`+0x13e` band, peasant) enemy stack
   to trip the group cascade (`FUN_0048c974`), then mop up.

**ISSUE (order primitives — cross-ref `combat-battlefield-input.md`):**
- **Group move/attack (player-legal):** set `DAT_0053e984`=group, `DAT_00553f88`=col, `DAT_00553f94`
  =row, `DAT_005533b8`=attack-flag; call `FUN_00479e90(group,col,row,attackFlag,team,0)` directly
  (bypasses the UI gating in `FUN_0043c634`). Or replay the RMB click chain (`FUN_0043c634` @27031).
- **Per-unit micro (AI-style, poke):** write `(&DAT_005544b1)[u*0x1b0]`=order subtype,
  `(&DAT_005544a4/a6)[u*0x1b0]`=dest hex, then clear the path cache (`DAT_005545e4/f6`) — exactly
  `FUN_00489b8d` @66790-66801.
- **Poll battle end:** `DAT_0053f028` (attacker total troops) / `DAT_00553c58` (defender) < 1 →
  `FUN_00477dfc` flips screen to 0x2b then the 0x13 result. Don't click-and-assume.

---

## 5. Ranked exploitable weaknesses

1. **Static ranged AI (never kites)** — your archers out-range and out-kite theirs for free; bait
   their archers into melee. *Highest value, fully code-grounded.*
2. **Deterministic, RNG-free damage** — pre-computable, repeatable; no variance to hedge.
3. **Blind nearest-Manhattan + 500-tick target lock** — decoy-drag advancing melee stacks across a
   chokepoint; they walk toward stale positions and won't switch to a closer threat mid-window.
4. **20-hex spiral / 40-hex reaction radius** — stay outside it and the group is inert; a decoy
   inside it triggers pursuit on demand.
5. **Predictable per-band morale + group rout cascade** — focus-fire low-morale stacks to rout a
   whole group at once (`FUN_0048c974`).
6. **Ranged unit pins in melee (order-4) on contact** — a cheap fast decoy permanently neutralises
   an enemy archer stack by parking adjacent.

## 6. LEADs — confirm live before scripting into the video
- `PTR_FUN_004d9140` / `d9170` / `d91b8` / `d91d0` / `d91f8` handler addresses (data-section).
- `.rdata` stat tables `0x4d96d0` (per-class) + `0x4d97b0` (per-weapon) → exact range/reload/power,
  and **which class indices (3 & 5?) are archer vs crossbow**.
- Map which stance handlers rewrite group anchors and per-unit destinations between the 200-tick
  stance cadence and the 500/20/50 formation-refresh cadence.
- **Ranges/cooldowns in real ticks** (range = `+0x144>>3` hexes; cooldown 0x78=120 ticks; reload
  `+0x13f`) — confirm tick rate for real-time seconds.
- Morale order `0xc` semantics (charge vs something else) and the **panic-budget `DAT_00553070`**
  overflow that forces a full-army rout.
- Whether `DAT_00553030` (local vs networked) is 0 so orders execute synchronously.

> 📍 Drivers: `FUN_004822ed`(64057) unit sim · `FUN_00489401`(66534) group AI · `FUN_00489a62`(66713)
> target pick · `FUN_00489b8d`(66742) order issue · `FUN_00483ce1`(64641) melee · `FUN_00483337`
> (64479)/`FUN_00484bf9`(64998) ranged · `FUN_00492c8b`(70409) damage · `FUN_00477dfc`(59979) end.
