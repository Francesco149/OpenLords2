# Strategic-map combat resolution

The first chip of the **combat-initiation subsystem** — the turn driver's last
unported frontier, reached through the chokepoint
`FUN_004ac3c6 → FUN_00437535 → FUN_004a7158`.  `FUN_004a7158` is the combat
entry: it parks the two combatant territory indices in `DAT_00568210`
(attacker) / `DAT_00568218` (defender), pre-clears battle accumulators, sizes
up the fight (`FUN_004a6a30`), then runs the **resolver** `FUN_004aad07` and the
**aftermath/render** `FUN_004ab383`.

Ported here is the resolver CORE — the part that mutates the per-territory
occupant records.  The audio (`ff_batl.wav` in `FUN_004a6a30`, `ff_lose.wav` /
`s080_*.wav` in `FUN_004ab383`/`FUN_004a7158`), the battle render
(`FUN_004ab383`, 1751 B, → engine_battlefield_render), and the
territory-takeover aftermath (ownership transfer `FUN_004a72fe`, army
elimination `FUN_004aa039`, notification `0x120`) are deliberately left as
separate, human-verification-gated chips.

## Module: `src/engine_combat.c`

| retail            | port                                  | role                                   |
|-------------------|---------------------------------------|----------------------------------------|
| `FUN_004aa89f`    | `engine_combat_clear_battle_result`   | zero the 4 result shorts (+0x17a)      |
| `FUN_00422c12`    | `engine_combat_load_muster`           | copy 7 counts → scratch 0x568420/0x56843c, fold merc into its class |
| `FUN_004ab2aa`    | `engine_combat_army_strength`         | weighted strength + merc, floored 1 / +20 |
| `FUN_004aad07`    | `engine_combat_resolve_casualties`    | the resolver                           |

`FUN_004ac6fe` (merc-contract release on annihilation) was already ported as
`engine_unit_clear_contract` in `engine_realm_upkeep.c` — the resolver reuses
it (the combat "active-class bonus" IS the mercenary contingent: the same
+0x195 class / +0x196 count / +0x197 pool-slot triple the upkeep pass manages).

## Combatant record fields (g_per_territory_420, stride 0x1a4)

Shared with `engine_realm_upkeep.c`'s `UNIT_*` layout:

| off    | width | meaning                                         |
|--------|-------|-------------------------------------------------|
| +0x00  | i8    | owning player id                                |
| +0x08  | u8    | active flag (gates merc-contract release)       |
| +0x168 | i32   | total head-count (re-summed by the resolver)    |
| +0x16c | 7×i16 | per-class troop counts                          |
| +0x17a | 4×i16 | battle-result accumulators (cleared by 4aa89f)  |
| +0x195 | u8    | merc contingent's troop class (0..6)            |
| +0x196 | u8    | merc contingent head-count                      |
| +0x197 | u8    | merc pool slot in g_unit_table_20               |

## The model (deterministic — NO RNG)

Weighted strength = `sum(count[c] * weight[c])` over 7 classes, plus the merc
contingent at its class weight; then floored at 1, or — if positive — bumped by
a flat +20 "presence" bonus.  Weights baked from `DAT_004d4b98`:

```
{ 2, 16, 8, 13, 9, 13, 22 }     /* peasant .. heaviest */
```

The side with the larger weighted strength **wins**; ties go to the attacker
(the test is `attacker_strength < defender_strength`).  The **loser is
annihilated** — all 7 counts zeroed, merc contract released (pool slot freed),
+0x168 = 0.  The **winner keeps a casualty-table fraction** of its troops + merc:

```
ratio = (winner_strength * 100) / loser_strength          /* >= 100 */
surv% = lookup_threshold(ratio, table, default 100)       /* DAT_004de710 */
```

Casualty table (`{threshold, survivor%}` pairs):

```
110→10  130→20  160→30  180→40  220→50
270→65  360→80  500→90  700→95  900→98   (else 100)
```

So a **close fight is pyrrhic** (ratio just over 100 → winner keeps 10 %) and a
**blowout is unscathed** (ratio ≥ 900 → 100 %).  An exact tie has ratio 100 →
also 10 % survivors (maximally pyrrhic).

### Quick-resolve difficulty multiplier

When `DAT_0053e8fc` (the quick-resolve flag) is set — the interactive path
clears it to 0, so this only fires on auto-resolve — the **defender's** strength
is first multiplied by an AI-difficulty factor from `DAT_0055401c`:

```
difficulty 0 → 160 %   1 → 200 %   2 → 250 %   3 → 320 %   else → 400 %
```

This can flip the outcome (e.g. an even matchup becomes a defender win on the
hardest setting).

## Verification

9 host unit tests (`tests/test_engine_combat.c`) cover the leaves + every
resolver branch (blowout / pyrrhic / defender-win / tie / difficulty-flip /
merc).  The **retail diff target `combat_resolve`** proves byte-exactness:
inject g_per_territory_420 + g_unit_table_20 + the four combat scalars, run
retail `FUN_004aad07`, read back both blocks + the winner/owner scalars.

**72/72 byte-exact vs retail** (24 vectors seed 0 + 48 vectors seed 7,
2026-06-01, cutestation.soy).  Vectors keep the merc class in 0..6 (it indexes
the 7-entry weight table) and the pool slot in 0..12 (g_unit_table_20 has 13
slots); the resolver touches only the two combatant records, so the other 149
records stay zero on both sides.

---

## Combat-init SCC prerequisites (2026-06-02)

The combat-initiation chokepoint is a 5-function mutually-recursive cluster:
`FUN_004ac3c6` demolish → `FUN_00437535` transfer → `FUN_004a7158` dispatch →
`FUN_004ab383` aftermath → `FUN_004a72fe` ownership-transfer (→ back to
`FUN_004ac3c6`).  Assembling these bodies is the A/V-gated frontier (battle SFX
s080/ff_batl/ff_lose, the battlefield render handoff via screen-state 0x12/0x13,
and ~15 ownership-transfer popups).  Before that assembly, every pure-logic leaf
they reach is now ported and — where it mutates save state — retail-diffed:

### Aftermath occupant fix-ups (engine_combat.c)

| retail         | port                                  | role |
|----------------|---------------------------------------|------|
| `FUN_004a80db` | `engine_combat_recompute_condition`   | post-battle supply/condition recompute (quirk #163) |
| `FUN_004ad8cc` | `engine_combat_halve_garrison`        | halve a conquered occupant's troops, recompute wage |

`combat_condition` and `combat_halve` diff targets: **64/64 byte-exact each**
(seed 7).  `FUN_004ad426` → `engine_realm_refresh_wage_bills`
(engine_realm_upkeep.c) refreshes every realm's wage bill; host-test-only (loops
the already-diffed `engine_unit_total_wage`).  `FUN_0049df48` (the aftermath's
AI re-dispatch) is byte-identical to `FUN_0049a990` → reuse
`engine_army_ai_run_turn`, no separate chip.

### Diplomacy (engine_diplomacy.c — new module)

Reached from the aftermath's final `FUN_004a1ee1(loser_owner, attacker_owner,
0x14)`.  All three functions touch only `g_player_strategic_352`; notifications
route through the already-ported event queue.

| retail         | port                                  | role |
|----------------|---------------------------------------|------|
| `FUN_004a1a54` | `engine_diplomacy_clear_treaty`       | break a lord/vassal bond |
| `FUN_004a24d1` | `engine_diplomacy_broadcast_penalty`  | penalise one player with everyone |
| `FUN_004a1ee1` | `engine_diplomacy_battle_adjust`      | post-battle relation hit (quirk #164) |

`diplomacy_adjust` diff target (inject strat, args=[p1,p2,amount] cc=mscdecl):
**64/64 byte-exact** (seed 7) — also covers the two callees.

### Still unported (the SCC bodies themselves — next, A/V-gated)

The 5 recursive bodies, plus the audio seams `FUN_00499aca` (click) and
`FUN_00427990` (WAV), and the interactive-battle screen-state handoff in
`FUN_004a7158` (`DAT_004eac50` 0x12/0x13, `timeGetTime`).  The auto-resolve path
(neither side garrisoned) is pure logic and should diff; the interactive path
hands off to the existing battlefield renderer and needs human A/V verification.

---

## Real-time army-state machine — region-occupation cluster (2026-06-03)

The per-tick SP driver `FUN_004650b0` (one of the three updaters
`FUN_0040490d`/`FUN_0049a010`/`FUN_004650b0` the main loop `FUN_004b99c0`
runs each sim sub-step when `DAT_0057c8fc == 0`) walks all 150 occupant
records (`g_per_territory_420`, stride 0x1a4) and dispatches each **live**
one (owner +0x00 != 0) through a 6-entry handler table `PTR_FUN_004d6a50`
indexed by the occupant **state byte** (+0x08); state 0 or > 5 falls to the
record-zeroing disband `FUN_0046ead0`.  The handlers update the occupant's
display sprite byte (+0x07) for the walk-cycle, sync its effective-region
colour (+0x10) against the cell's region key (`g_cells[+0x0c cell] + 7`),
decrement a timer (+0x160), and — when the occupant's sub-state (+0x14c) is 2
— run the movement-step executor `FUN_00465d28`.

State→handler table (`0x4d6a50`):

| state | handler        | role                                            |
|------:|----------------|-------------------------------------------------|
| 1     | `FUN_0046521f` | marching/occupying — anim by income bucket; region-owner reconcile via `FUN_004abb36` |
| 2     | `FUN_00465486` | settling — low head → `FUN_0046f0a9`; else reconcile via `FUN_004abd0f` |
| 3,4   | `FUN_00465622` / `FUN_00465761` | idle/in-transit — set region colour directly (byte-identical handlers) |

Facing→frame tables: `0x4d6a78` = {0,1,2,1,0,1,2,1}, `0x4d6ab8` =
{0,1,2,3,4,5,0,1} (4-byte stride, indexed by occupant facing +0x1b).

### Ported so far (region-occupation reconcile helpers)

These are the handlers' occupation callees — all now in `engine_combat.c`,
host-tested:

| retail         | port                                  | role |
|----------------|---------------------------------------|------|
| `FUN_004abf77` | `engine_combat_notify_occupation`     | neutral-region population-reception notice (0x82..0x86) |
| `FUN_004abb36` | `engine_combat_army_enter_region`     | state-1 reconcile: latch colour, notify, owned-arrival 0xaa |
| `FUN_004ac185` | `engine_combat_spawn_rebellion`       | spawn a rebel stack (kind 2/type 6) on a dry-reserve owned region |
| `FUN_004abd0f` | `engine_combat_army_settle_region`    | state-2 reconcile: warn owner (0x9a/0x9b/0x9c), maybe rebel, decay popularity |

### Driver + display handlers — DONE (2026-06-03)

Ported as the new module `engine_army_state.c` (driver `engine_army_state_run`
+ the four handlers), host-tested (10 tests, 1900 total green):

| retail         | port (engine_army_state.c)        | role |
|----------------|-----------------------------------|------|
| `FUN_004650b0` | `engine_army_state_run`           | walk records 1..150, count live, dispatch by state / disband 0,≥6 |
| `FUN_0046521f` | `handler_march` (state 1)         | sprite by head bucket (≤300→0x48, ≤600→0x60, else 0x78) + facing; enter_region reconcile; sub-state advance |
| `FUN_00465486` | `handler_settle` (state 2)        | head<30 → `engine_unit_remove`; else sprite base 0x90 + settle_region reconcile |
| `FUN_00465622` / `FUN_00465761` | `handler_idle` (states 3/4, retail byte-identical) | sprite ((anim+1)&7)·6 + idle-facing; region colour set directly |

Display formula (all states): `sprite[+0x07] = (((anim[+0x09]+1)&7) · K) + base
+ facing_table[facing[+0x1b]]`, where K=3/base∈{0x48,0x60,0x78} for state 1,
K=3/base=0x90 for state 2, K=6/base=0 for states 3/4.  Facing tables baked:
`k_facing_march` = DAT_004d6a78 {0,1,2,1,0,1,2,1}, `k_facing_idle` =
DAT_004d6ab8 {0,1,2,3,4,5,0,1}.  Every state decrements the +0x160 timer
(floored 0) and, when sub-state +0x14c == 2, calls the bound movement seam.

Bound seams: `engine_army_state_bind_cells` (DAT_00522f90, region key at cell
+7 — wired in `game_init`), `engine_army_state_bind_move_fn` (FUN_00465d28,
default NULL no-op).  Driver globals exposed: `g_army_state_index`
(DAT_005533e8, the seam reads it), `g_army_state_live_count` (DAT_00553f08),
`g_army_state_abort` (DAT_0050a49e — a seam may raise it to halt the walk).
The state-0 table slot is a no-op stub (never dispatched: state 0 disbands);
slot 5 is null in retail (state 5 never occurs).

### Destination-cell evaluator — DONE (2026-06-03)

The bottom of the movement executor: the sub-tree that answers "what is in the
cell my stack is about to enter?".  Ported as the new module
`engine_army_move.c` (+ the leaf relations predicate in `engine_diplomacy.c`),
host-tested (11 + 3 tests):

| retail         | port (engine_army_move.c)         | role |
|----------------|-----------------------------------|------|
| `FUN_00466893` | `engine_army_move_eval_dir`       | pick the neighbour cell in compass direction 0..7 over the 64×64 / 8-byte grid (row stride 0x200; deltas ±8 col, ±0x200 row, ±0x1f8/0x208 diag), grid-edge-guard it, then classify |
| `FUN_00466c3c` | `classify_cell` (static)          | latch the cell scratch (flags +1, region +7, terrain +0, offset, occupant +5 → `g_combat_active_terr`) and map the movement flags to a code (priority bit0→3, bit6→5, bit7→6, bit4→7, bit5→8, else 1); 0x80 suppressed on terrain 0x14 / the stack's +0x14e; occupied cell → `eval_occupied` |
| `FUN_004658c1` | `eval_occupied` (static)          | resolve a collision: same owner → `engine_combat_merge_stack` (partner/eligibility guards); hostile (via the predicate) state-1/2 → `engine_combat_dispatch` (interactive → raise abort + bump waypoint; auto-resolved + self destroyed → 10); hostile state-4 garrison → relation hit + notify 0xa2/0xa1 + `engine_unit_remove`, code 10 |
| `FUN_004a16f7` | `engine_diplomacy_should_engage`  | engage predicate: neutral/different owner → 1; self or own lord (+0x81) → 0 (lord case bumps the +0x86 contact counter) |

Outcome codes returned up to the executor: 1 open · 3 rough-base · 5 combat ·
6 region-event · 7 special · 8 impassable · 10 arrival · 0 grid edge.  Cell
grid bound via `engine_army_move_bind_cells` in `game_init`; the cell scratch
(`g_army_move_cell_flags/region/terrain/off`, retail DAT_0050a4a4/49c/49d/4a0)
is exposed for the executor.  The live combat-engage and merge *invocations*
are exercised by the combat module's own tests and will be diffed against
retail once the executor lands; this chip's tests cover the decision routing.

### Commit-step + classifier + blocked-branch — DONE (2026-06-03)

The classifier and its two action helpers (it commits the move when the step is
legal, or runs the blocked-branch when it isn't):

| retail         | port                                  | role |
|----------------|---------------------------------------|------|
| `FUN_00466d84` | `engine_army_move_commit_step`        | unlink → per-state footstep SFX (seam) → apply the latched +0x09 direction's col/row/cell delta → relink → set sticky rough flag (+0x14d) from the new cell's bit0 |
| `FUN_0046634d` | `engine_army_move_classify_step`      | per-step result classifier (spine) |
| `FUN_0046673c` | `blocked_branch` (static)             | impassable-on-foreign-region effect |
| `FUN_00469e0e` | `engine_army_move_terrain_passable`   | terrain predicate (2 ≤ t < 0x17) |
| `FUN_00469e5b` | `decay_region_cell` (static)          | scorched-earth resource decay |

`engine_army_move_commit_step` reads the committed direction from record +0x09
(retail aliases the army-state anim byte; the classifier latches it there) and
plays the footstep through the `engine_army_move_bind_sfx_fn` seam (retail
0x426120, default NULL no-op — drop-in stays silent until audio is wired, like
the combat seams).

`engine_army_move_classify_step(dir, evq, mp_active)` is the spine the executor
(`FUN_00465d28`) calls each tick.  Until the stack's **step-eval** is armed
(record flag +0x14b bit0) it runs a stall/retry hysteresis: each tick bumps the
retry counter (+0x14a) and, once it clears a direction-dependent budget (3
normal, 0 once the previous step was rough — read from the sticky rough flag
+0x14d), it shuffles the facing (+0x1b), advances the stall accumulator (+0x149;
+2 single-player, +4 MP — `DAT_00553030`/`g_sim_mp_active` threaded as the
`mp_active` param), and at 0x10 arms the step-eval (returns 2).  Disarmed → 1.

Once armed it calls `engine_army_move_eval_dir` and post-processes: an aborted
walk (`g_army_state_abort`) reports 10; a grid edge (0) passes through; codes
> 4 (5 combat / 6 region-event / 7 special / 10 arrival) return straight up for
the executor to dispatch; an impassable step (8) runs the blocked branch and is
then treated as open (1); a rough step (3) latches the sticky rough flag.  Any
committable outcome (1/3) bumps the path-progress accumulator (+0x153; +3
normal, +1 rough), disarms the step-eval, latches the committed direction at
+0x09, re-primes the stall accumulator to 1, and commits the move.

`blocked_branch` (only when the stack is not idle, state 3/4): +0x153 += 3, then
— iff the obstacle belongs to a **foreign** region (`g_per_army_768[region*0x300
+5]` ≠ mover owner) and the cell is real land (`engine_army_move_terrain_passable`)
— levies a relation penalty (`engine_diplomacy_battle_adjust(region_owner,
mover_owner, 10, evq)`, only when the mover's `P_ACTIVE` byte is set), scorch-
decays the cell, and plays the blocked cue through a *second* SFX seam
`engine_army_move_bind_alert_sfx_fn` (retail 0x426216, distinct DirectSound
play from the footstep family; default NULL no-op).  `decay_region_cell` drains
a census-proportional share of the foreign region's stockpile — mid tier
(terrain < 0x0f, fields +0x201/+0x206/+0x234/+0x244) or high tier (terrain ≥
0x0f, fields +0x200/+0x250/+0x27c) — via `engine_math_percent_div/_of`, then
flattens the cell to terrain 0 (`engine_territory_set_cell_type`).  The army
then commits the step into the now-cleared cell (bulldoze-through).

Ported into `engine_army_move.c` (+ header), host-tested (8 new tests, 1926
total green): terrain predicate range, hysteresis (no-wrap / wrap +2 / MP +4 /
rough-budget / 0x10 arm), open commit, rough commit, combat/region/special
pass-through, grid-edge no-op, blocked scorched-earth (decay arithmetic +
flatten + alert + push-through), and the same-region no-scorch guard.  All
offsets r2-verified against `FUN_0046634d`.

### Movement-step executor `FUN_00465d28` → `engine_army_move_run` (ported)

The inner loop the army-state driver runs when an occupant's sub-state (+0x14c)
is 2.  Each entry resets the per-occupant timer (+0x160 = 4) and walks the stack
toward its waypoints, looping until the per-tick **step budget** (+0x154, set by
the state handler to 0xf march / 0xa settle) is consumed by the path-progress
accumulator (+0x153), the path ends, or an outcome interrupts:

- While the step-eval is disarmed (+0x14b bit0 = 0) the loop spins the
  classifier's stall/retry hysteresis — the classifier ignores the direction
  until armed — then on arming loads the next waypoint off the per-record list
  (+0x1d col / +0x1e row, pairs indexed by the waypoint counter +0x1c) and
  evaluates the step toward it.
- The **local player's** marching stack (state 1, owner == `s_slot_0057c8cc`)
  stamps a 13×13 alternate-draw box (`engine_territory_mark_draw_box`, retail
  `FUN_0046e067`) and marks the screen dirty (`..._mark_full_screen_dirty`,
  `FUN_0045240a`) each armed pass — pure render, both already ported.
- **Budget spent**, or a turn commit in progress (`g_sim_turn_progress` 1/2),
  snaps +0x153 to the budget, recenters the strategic-map surface on the stack
  (camera/present seam `FUN_00426360`, gated on the live-order byte +0x01), and
  finalises via the **arrival** seam (`FUN_00467196`) unless the record's +0x06
  byte suppresses it.  Same path when the waypoint counter hits 0.
- A classifier outcome of **5** runs the two **combat** seams (`FUN_004296b5`
  engage + `FUN_004a6c68` resolve, both with the latched cell region) and aborts
  the whole army walk (`g_army_state_abort`) if the battle screen 0x12 is up; **6**
  runs a **region-event** seam picked by the destination terrain (`FUN_0046873f`
  for terrain < 0x10, `FUN_004686a0` for 0x15..0x19); **7** runs the
  **special-tile** seam (`FUN_00468ae2`); **10** ends the walk (arrival or
  collision-destroyed).

`evq` / `mp_active` are threaded straight into `engine_army_move_classify_step`;
the army-state driver's move_fn seam now carries them (signature changed from
argless).  The deeper callees above are bind-able seams (NULL = no-op,
`engine_army_move_bind_present_fn` / `_arrive_fn` / `_combat_fns` / `_region_fns`
/ `_special_fn`), so until each is ported the executor faithfully **steps and
commits** the stack across the grid (real save-state mutation via
`engine_army_move_commit_step`) without firing those events — exactly the staged
shape the move_fn seam itself had before this chip.  The gameplay seams
(combat / region / special) carry `evq` / `mp_active`; only the present seam is a
bare render call.  Bound to the driver via
`engine_army_state_bind_move_fn(engine_army_move_run)` in `game_init`.

#### Special-tile plunder `FUN_00468ae2` → `engine_army_move_special_tile` (ported)

The first dispatcher callee filled in (outcome code 7), bound into the executor's
special seam in `game_init`.  A non-idle stack (state ≠ 3/4) that steps onto an
**untouched special tile** (cell terrain +0 == 0x10) in a **foreign** region
(region owner +0x05 ≠ mover owner) plunders it: +7 to the stack's path-progress
accumulator (+0x153), flip the tile to terrain 0x13 with a 0x3c countdown stamped
at cell +3, drain a quarter of the region's +0x24 stockpile (`x -= x/4`, round
toward zero — matches retail's `cdq` / `and 3` / `sar 2` signed div), a relation
hit (`engine_diplomacy_battle_adjust(region_owner, mover_owner, 0x14, evq)`,
retail `FUN_004a1ee1`), and the blocked-step alert cue (id 3, the existing
`engine_army_move_bind_alert_sfx_fn` seam).  Same-owner / idle / already-flipped
tiles are a no-op.  Host-tested (4 new tests, 1939 total green): full plunder
(flip + countdown + 1/4 drain + progress + cue), same-owner no-op, idle no-op,
already-plundered no-op.  Offsets + the signed-div drain r2-verified against
`FUN_00468ae2`.

#### Region-event low-terrain develop `FUN_0046873f` → `engine_army_move_region_event_lo` (ported)

The second dispatcher callee (outcome code 6, **low** terrain — the executor
routes here when the cell terrain is < 0x10), bound into the executor's region-lo
seam in `game_init` (`engine_army_move_bind_region_fns(…, NULL)`; the region-hi
seam `FUN_004686a0` stays NULL until ported).  A non-idle stack (state ≠ 3/4)
that steps onto a **foreign** region's (owner +0x05 ≠ mover owner) *undeveloped*
resource tile **develops** it.  The cell terrain picks one of four resource bands
— `{0,1,2}→3` (regrow 0x2e), `{4,5}→6` (regrow 9), `{7,8}→9` (regrow 0x13),
`{10,11}→0xc` (regrow 0x1d) — flips the cell terrain to the band sentinel, stamps
the regrow countdown at cell +3, resets the region's matching resource node (a
`+0x2xx` triple `u8:=3 / u8:=0 / i32:=0` plus a `+0xfx` pair `i32:=-1 / i32:=0`,
all at g_per_army_768 + region·0x300), plays the alert cue (id 3) and bumps
path-progress (+0x153) by 7.  Already-developed sentinels (3/6/9/0xc) and terrain
≥ 0xd (incl. the 0xd..0xf low band the executor still routes here) are a no-op,
matching retail's nested `!=` guards inside each `<` range.  All 20 region-node
offsets + the `shl 8` / SIB×3 = ·0x300 region stride r2-verified against
`FUN_0046873f`.  Host-tested (5 new tests, 1944 total green): all four bands
(terrain + countdown + node triple/pair + progress + cue), sentinel no-op (×4),
terrain≥0xd no-op, same-owner no-op, idle no-op.

#### Arrival / waypoint re-path `FUN_00467196` → `engine_army_move_arrival` (ported)

The executor's *arrival seam* (`army_move_arrive`), invoked when a stack reaches
its path end / spends its step budget.  It probes for the nearest map feature
from the stack's current cell — `engine_map_query_nearest_feature_ring_at`
(`FUN_0046733c`), whose out-params surface the matched cell, i.e. the position
retail leaves in its `DAT_00569570/78` find-nearest scratch (quirk #130: the port
threads it explicitly instead of via the global).  On a hit (ring distance ≠ 0)
the matched cell becomes the new destination (+0x14/+0x15), the path-progress
accumulator (+0x153) is debited by the ring distance (signed `< dist → 0` clamp),
sub-state (+0x14c) is set to 2 (re-march) and the region-event suppressor (+0x14e)
cleared; the stack then **re-paths** via the cost-field pathfinder (see
[engine_path_field.h]) — `fill` from the current cell, `trace` from the new
destination, `copy_record` on a successful trace.  No feature in range → a no-op.

Note the feature probe's real semantics: `only_bit1` (mask 0xfd, expect 0) matches
*empty* cells, so on an open map the probe always resolves to ring distance 1 at
the nearest cell; a *bit0* feature one ring out is what a deliberate re-path
targets.  All record offsets r2-verified against `FUN_00467196`.  Host-tested
(3 new tests, 1955 total green): bit0-feature re-path (dest + sub-state +
suppressor + progress debit + a copied route), the progress `< dist → 0` clamp,
and the no-feature no-op.

**Not yet wired into the executor's `g_arrive_fn` seam** — binding it live needs
the per-cell move-cost grid populator (retail 0x46ee31) so the flood reads real
terrain; until then the seam stays the existing NULL no-op (no behaviour change).

Ported into `engine_army_move.c` (+ header), host-tested (9 new tests, 1935
total green): budget-spent arrival (clamp + present + arrive + timer reset),
+0x06 arrival suppression, turn-progress forced arrival, open-step walk (commit
steps the stack east + disarms), already-on-waypoint immediate stop, combat
dispatch (both seams + cell region), combat abort on battle screen 0x12, region-
event lo/hi terrain bands, and special-tile dispatch.  All offsets, the +0x160
store width, the waypoint indexing, and the 14-call dispatch order r2-verified
against `FUN_00465d28`.

#### Settle-deposit / combat-engage `FUN_004296b5` → `engine_army_move_combat_engage` (ported)

The **first** of the two seams the executor fires on a combat outcome (code 5):
combat_a (this), then combat_b (`FUN_004a6c68`, still unported).  Despite the
"combat" label it only acts on a stack in **state 4** whose target settle region
(record +0x167) is the very region it is entering — that stack is **absorbed**.
It deposits the two contingent values it carries into the region's economy
(`region +0x224` provision-raw `+= (i16) stack +0x16c`; `region +0x250` stock-A
`+= (i16) stack +0x170`), and if the stock-A total goes positive runs the
recensus/reduce (`engine_territory_recensus_reduce`).  The realm is then fully
recomputed in retail's exact order — grade feature cells, provision
(`s_slot_0057c934`), plan-realm (`s_slot_0057c92c`), allocate workforce, then
provision + plan-realm **again** — the stack is removed
(`engine_unit_remove`), and a demand-redraw (`DAT_0057d340 = 2`, a new bind-able
seam) is raised iff the absorbed region is the on-screen one (`s_slot_0057c93c`).
Any other state / mismatched region → no-op.  The two `add dword` deposits (both
`movsx`-widened i16→i32), the `region·0x300` SIB stride, the `jle`-gated recensus
and the full 6-call recompute order all r2-verified against `FUN_004296b5`.

Ported into `engine_army_move.c` (+ header).  **Not yet bound live** into the
executor's `g_combat_a_fn` seam — firing only half the combat pair (deposit
without `FUN_004a6c68`'s actual resolution on a contested step) would be wrong,
so `engine_army_move_bind_combat_fns` stays NULL until combat_b lands (mirrors
how the arrival seam waits on the move-cost grid).  Host-tested (4 new tests,
1959 total green): happy-path absorb (both deposits + removal + redraw mode 2),
off-screen region (deposit + removal but **no** redraw), wrong-state no-op, and
settle-region≠entered-region no-op.  The recompute chain delegates to
already-tested ports, bound NULL in the test so the deposit lands observably.

#### Capital-defender finder `FUN_0046d42c` → `engine_combat_find_capital_defender` (ported)

A leaf dependency of the combat-resolve back half `FUN_004a6c68` (combat_b parks
its result in the defender slot `DAT_00568218`).  Given a region key it scans the
**4×4 cell block** anchored two cells up and left of the region's capital cell
(record +0x6c col / +0x6d row) — columns `[cap_col-2 .. cap_col+1]`, rows
`[cap_row-2 .. cap_row+1]` — and returns the strongest friendly live unit there:
the occupant whose owner (+0x00, sign-extended) equals the region owner (+0x05),
whose kind byte (+0x08) is 1, with the greatest head-count (+0x168, strict `<`).
Returns 0 when the capital is within two cells of the 0..63 grid edge (the whole
box must fit) or nothing qualifies.  The box is **asymmetric** — 2 cells left/up
but only 1 right/down — a retail quirk (the loop runs `[cap-2, cap+2)` on both
axes); ported verbatim.  Reads `g_overworld_map` (cell +5 head-link) directly.
Retail reuses engine_path's find-nearest scratch `DAT_00569570/78` as the loop
counters (codegen artifact, quirk #130); the answer is the returned index, never
the scratch, so the port uses locals.  Capital offsets (+0x6c/+0x6d), the signed
`movsx` owner compare, the +0x168 dword strength and the `+8` / `+0x1e0` row
stride r2-verified against `FUN_0046d42c`.  Ported into `engine_combat.c` (+
header), host-tested (5 new tests, 1964 total green): strongest-in-box pick,
owner/active/out-of-box filtering, the asymmetric-box boundary (cap+2 excluded /
cap-2 included), all four grid-edge rejections, and the empty box.

#### Defender muster prime `FUN_004aaa80` → `engine_combat_prime_muster` (ported)

The deepest leaf of the combat-resolve back half: `FUN_004a6c68` → `FUN_004a50ae`
(garrison/auto-defender builder, 731 B, still unported) → **this**.  Before the
builder redistributes/recruits a defending unit it resets the region owner's
**troop muster pool** (`g_troop_pool`, the same per-player grid `game_init`'s
recruitment seeds, 8 contingent slots × 0x10) and seeds it from the owner's six
**cashflow-category reserves** (strat +0x140..+0x154 = `P_CAT` cats 1..6).  Given
a region key and a staged head-count it: clears the +0x00 and +0x04 fields of all
eight slots; copies reserve `cat[i]` into slots 1..6 fields **+0x04 and +0x08**
(two identical copies — the builder draws one down, keeps the other as the
original cap); then parks the staged head in slots **0 and 7** fields +0x00 and
+0x08 (slot 7 = the pool's +0x70 budget cell, reused here as a second head
marker).  Pool row and strat reserves both indexed by the region owner byte
(g_per_army_768 +0x05).  The `uVar1*0x160` strat stride (`0xB*0x20` SIB),
`uVar1*0x80` pool stride, the asymmetric clear (+0x00/+0x04 wiped, +0x08 left for
the slot-0/7 head stores) and the `+0x70`/`+0x78` budget-cell reuse all
r2-verified against `FUN_004aaa80`.  Ported into `engine_combat.c` (+ header),
host-tested (1 new test, 1965 total green): slot-1..6 reserve copy + +0x00 clear,
slot-0/7 head park, and the +0x04 clear on slots 0/7.

#### Defender muster/spawn `FUN_004a50ae` → `engine_combat_muster_defender` (ported)

The garrison/auto-defender builder combat_b reaches when no live capital garrison
is found.  Given a region, a percentage, a provision floor and a mode, it raises
a defending unit sized at `pct`% of the region's provision (+0x24) and spawns it.
Guard: provision < floor → return 0.  Otherwise it looks up a supply cost from
the percentage→cost curve (retail .rdata 0x4d8778, a 102-entry clamped ramp
saturating at 101), computes `sized = percent_of(provision, pct)`, primes the
owner muster pool (`engine_combat_prime_muster`), then stages by mode:

* **mode 2** (fresh garrison) — spawn free (cost 0), seat three strat reserve
  cells (+0x144/+0x14c/+0x150 = 500), and distribute the `sized` head into troop-
  class slots by size band (`< 0x78` none; `0x78..0xef` slot5=60; `0xf0..0x167`
  slot5=80/slot4=40; `0x168..0x1df` slot5=100/slot4=70; `≥ 0x1e0` slot5=150/
  slot4=100/slot2=50), debiting the leftover from slot 0.
* **mode 1** (top-up) — recruit 10 head at a time from the primed reserves (slot
  +0x08 → slot +0x00, debiting slot 0) round-robin over classes 1..6 until the
  staged budget (slot 7 +0x00) drops below 10 or 50 sweeps elapse.
* **mode 0** — plain spawn at the muster-cost supply price.

Then `engine_combat_spawn_unit(owner, region, 0, supply)`; on success returns the
new occupant index (retail reads `DAT_0052afe0` = `engine_territory_current_slot`
after the spawn — the port's spawn_unit returns 1/0, so the index comes from
current_slot), else 0.  The `cmp 0x78` band edge (not `> 0x77`), the four band
constants, the 0x1f4=500 reserve seeds, the `+= 0xa / -= 0xa` recruit deltas, the
slot-7 budget read and the `percent_of` + cost-table indexing all r2-verified
against `FUN_004a50ae`.  The 102-entry curve is embedded verbatim as
`k_muster_cost`.  Ported into `engine_combat.c` (+ header), host-tested (4 new
tests, 1969 total green): provision-floor bail, the mode-2 360..479 band
distribution + reserve seeds, the mode-1 four-move recruit drain, and the full
mode-0 happy path (occupant index == current_slot) over the shared spawn harness.

This makes `FUN_004a6c68` (combat_b, 1264 B) a **leaf** — all its non-seam
callees are now ported.

### Frontier (next chips)

1. **The remaining executor dispatcher callees** — port each and bind it into
   the executor through the seam it already calls: `FUN_004a6c68` (combat resolve
   — the 1264 B back half of the code-5 pair; once it lands, bind **both** combat
   seams together via `engine_army_move_bind_combat_fns`).  Its two unported deps
   are now both **done**: `FUN_0046d42c` (capital-defender finder) and
   `FUN_004aaa80` (defender muster prime) are ported (above).  `FUN_004a50ae`
   (731 B garrison/auto-defender builder) is **done** too
   (`engine_combat_muster_defender`, above) — so `FUN_004a6c68` (combat_b, 1264 B)
   is now a **leaf** and is the next chip: port it, then bind **both** combat
   seams together via `engine_army_move_bind_combat_fns`.  Then `FUN_004686a0`
   (region-event handler, **high**
   terrain — needs its two callees `FUN_004a78ea` / `FUN_004a7ca2` first, which
   chain into `FUN_004a79a3` / `FUN_004a7e0a`), and `FUN_00426360` (DirectDraw
   surface present/scroll — render seam, lands with the strategic-map camera
   cluster).  **Done**: special-tile `FUN_00468ae2`, region-event-lo
   `FUN_0046873f`, arrival `FUN_00467196`, settle-deposit `FUN_004296b5`,
   capital-defender `FUN_0046d42c`, muster-prime `FUN_004aaa80`, defender
   muster/spawn `FUN_004a50ae`.
2. **Wire `engine_army_state_run` into `engine_tick`'s SP per-tick path**
   (engine.c, beside the existing `FUN_0040490d`/`FUN_0049a010` arm) + bind the
   rebellion seam (`engine_realm_bind_rebellion_fn`) to
   `engine_combat_spawn_rebellion` — deferred from this chip because it needs
   the turn evq + mp_active context that the driver wiring establishes.  The
   cell-grid bind is already in `game_init`.
