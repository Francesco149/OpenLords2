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
