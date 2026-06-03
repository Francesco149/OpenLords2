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

### Frontier (next chips)

1. **Driver `FUN_004650b0` + the 4 display handlers** (0x46521f / 465486 /
   465622 / 465761) — the handlers' only remaining unported callee is
   `FUN_00465d28`; everything else is ported.  Needs a cell-grid binding in
   the driver's module (to read the cell region key +7) and the two facing
   tables baked.  Land with `FUN_00465d28` as a bound seam.
2. **Movement-step executor `FUN_00465d28`** (1441 B) — advances the occupant
   one step along its path (`FUN_0046f434`, ported) and dispatches the result
   code (`FUN_0046634d`, unported) into arrival / blocked / combat-engage
   (5 → `FUN_004296b5` + `FUN_004a6c68`) / region-event (6 → `FUN_0046873f` /
   `FUN_004686a0`) / (7 → `FUN_00468ae2`).  Deep: 7 unported callees, the
   real "armies move on the strategic map" engine.  Its own multi-chip arc.
3. **Wire the driver into `engine_tick`'s SP per-tick path** (engine.c, beside
   the existing `FUN_0040490d`/`FUN_0049a010` arm) + bind the rebellion seam
   (`engine_realm_bind_rebellion_fn`) to `engine_combat_spawn_rebellion` —
   deferred from the occupation-cluster chip because it needs the turn evq +
   mp_active context that the driver wiring establishes.
