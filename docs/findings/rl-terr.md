# rl terr — the strategic map's territory layer on retail's layout (2026-11-04)

Partition `terr` of family `rl` (round 3: functions whose old port was not on retail's layout or not linked in rhost;
design `battlefield-sim-port.md`). 71 functions, 22913 B, the 0x465486..0x47309d band: the overworld build, the
cell edits, the map / AI queries, the record clears, the strategic path field, the battlefield's greedy walk, the
event queue and its popup step, the army-state handlers 2..4. All in `src/rl_terr.c`; rows
`tools/rhost/rh_hooks_rl_terr.c`.

## Status

All 71 ported; 68 with a row, verified over the whole corpus (0 divergences, hybrid PASS); the 3 army-state handlers
have no row (§rhost limits: the paged shadow is not transparent for them) and verify under `--shadow-full`. Route (a) — a linked old port already on retail's layout, wrapped — for 7: 0x46cfbd / 0x46733c /
0x46d2a3 (engine_map_query: retail's grid bound, the cell cursor a mirror) and 0x46f700 / 0x4701ac / 0x4707be /
0x4708d0 (engine_path_field: retail's fields / queues / waypoint lists bound, its bookkeeping g_pf_* mirrors).
Route (b) — a fresh port from the asm — for the other 64 (the old ports drop retail's scratch globals, run on
file-static copies (the capital cursor DAT_0057c8d0 / DAT_0056846c, the queue struct, an rng struct, the terrain
compute's in / out structs), are static in engine_army_ai.c, or sit in unlinked modules: game_init.c,
engine_map_init.c, engine_strat_cost.c, engine_army_move_confirm.c, engine.c).

## What is where

| group | functions |
|---|---|
| overworld build (new game) | 0x4676e0 the sequence; 0x467770 l2_maps.dat → cells, 0x46797d the terrain plane → tiles, 0x46a037 the minimap grids; 0x46c5b8 / 0x46c6ba / 0x46df51 the unrolled cell clears; 0x46dbf5 / 0x46de05 / 0x46de94 / 0x46c2de auto-tiling, 0x46dd34 the special-tile re-roll; 0x467ca6 county adjacency (0x46c080 neighbour keys); 0x468d4f capitals (0x467fd1 capital + 0x46ac22 keep stamp, 0x468c41 villages, 0x468e61 special towns, 0x46902a the port (0x46c147), 0x468121 secondary keep); 0x467a36 feature seeds; 0x46da4b the cell roster |
| cell edits | 0x46d7f4 cell type (0x46d9da AI score), 0x4684c6 villages, 0x46942c / 0x46958f / 0x46965a / 0x4697cd / 0x46988d / 0x469a9c / 0x469d21 the county roster cycles and bands, 0x46921d re-census, 0x46dfd5 county highlight, 0x46e1b0 the entity placer |
| queries | 0x46cfbd / 0x46733c / 0x46d2a3 boxes and rings, 0x46d5fe terrain info, 0x4673b8 / 0x467532 nearest army, 0x467e2c / 0x467f2e county adjacency, 0x46841c keep eviction |
| record clears | 0x46ea28 / 0x46ea61 / 0x46ead0 / 0x46eb09 memsets; 0x46f1dc / 0x46f2ae / 0x46f3cd the tables |
| path field | 0x46f700 flood, 0x4701ac trace, 0x4707be copy, 0x4708d0 clear, 0x46f68e overlay reset, 0x470678 neighbour min, 0x46ff43 the move-cost grid |
| battlefield walk | 0x472392 greedy walk, 0x47265d nearest reachable, 0x472875 stash |
| event queue | 0x472ad0 / 0x472ae0 init, 0x472b40 slot zero, 0x472bc5 post, 0x472e46 the popup step |
| army state | 0x465486 settle, 0x465622 / 0x465761 idle (DAT_004d6a50 entries 2..4) |

## How the ports reach the rest

- Mirrors (rh_hooks_ai.c k_mirror_va) through their owners: the cell cursor g_cell_cursor_col / _row (the sweeps'
  counters, 0x47265d's box), engine_territory_set_current_slot (0x46e1b0's record scan DAT_0052afe0), g_pf_* (0x46f68e),
  g_army_state_index (the handlers).
- Linked verified ports by name: engine_territory_mark_draw_box (0x46e067), engine_territory_census_army_terrain
  (0x469b8d), engine_army_move_arrival (0x467196), engine_diplomacy_should_engage (0x4a16f7), engine_path_direction_8way
  (0x46f434), engine_unit_remove (0x46f0a9), engine_realm_collect_territory_income (0x4ad6c0); the two that post take
  the port's queue struct — engine_combat_army_settle_region (0x4abd0f), engine_army_move_run (0x465d28): loaded from
  retail's queue globals around the call and stored back (terr_evq_load / _store).
- Linked, unverified, through route (a): engine_map_query_box_any_rawflag_only_bit1 (0x46d130, in no partition) under
  0x46733c.
- Other partitions by name: rl_404b2c / rl_404f4c / rl_404d6b / rl_404dc1 / rl_4290d0 / rl_429153 / rl_4291b3 (ui4),
  rl_45240a (ctl), rl_47309e / rl_476768 / rl_49bbe8 / rl_49bc1d (turn), rl_4af8d9 / rl_4b3e51 (host), rl_4af767 /
  rl_4afab9 (io).
- No TODO(rl-extern) left.

## Globals

`src/rl_terr_globals.{h,c}` + `tools/rhost/rh_rl_terr_alias.h`: generated (`--part-globals terr`, `dedup_globals.py`:
none shared), then by hand: the march facings DAT_004d6a78 widened to 8 (IDA sized 5), the staging record
DAT_00553510..0x553527 merged into one 24-B block (retail copies it whole), the neighbour keys DAT_00553280..0x55329f
merged into int[8]; added: DAT_00568500[0x430] — the event ring past the save entry g_misc_128 (retail's ring is 50
× 24 B; the save holds its first 128 B). Foreign: rl_ui2's DAT_0055cea0 / DAT_0056795c / DAT_00553f78 / DAT_005651bc,
rl_render's DAT_00553f54 / DAT_00569590, rl_io's DAT_004d6af8, the battlefield's DAT_004f9680 — declared locally.

## Quirks

1. **0x472b40 zeroes the slot argument's code (+8) but every other field of the READ slot DAT_00553ec4**: the init
   loop (0x472ae0) sweeps 0..49 with the read index at 0, so slots 1..49 keep their payloads.
2. **The cell sweeps' counters are live state**: 0x46c5b8 / 0x46c6ba count 8-cell passes in the cell cursor
   DAT_00569570 (8) / 78 (64) and the byte cursor DAT_0053e8b8 (0x8000); 0x468e61 / 0x46902a / 0x467ca6 sweep on
   DAT_00569570 / 78 + DAT_005533cc; the load / tile passes count in DAT_0053e974.
3. **County 17 indexes past the 17-row tables** (0x46da4b's roster row 17 = 0x53ef50.., 0x467a36's count 0x542eb5):
   keys 1..17 pass retail's `<= 0x11` tests.
4. **0x46d5fe reads the owner of county 32** (+5 of record 32, past the table) before mapping 32 to 0.
5. **0x46c2de's rule tables are state**: a matched rule's variant byte +11 advances in .data (DAT_004d7310, the
   battlefield's DAT_004d7610 / 0x4d7930); no match leaves the previous outputs (DAT_0057c8a4 / DAT_00568200) for
   0x46dbf5 to reuse.
6. **0x46f700 reads the row above row 0 of the move-cost grid** (DAT_004f4000 + 2 i) — DAT_004f4070 (the flood's own
   source cell) lies there.
7. **0x4701ac's waypoint index is unbounded** (a trace over 150 steps writes into the next slot's list).
8. **0x472bc5's MP relay loop is dead** (its gate local is 0): IDA's pseudocode drops it.
9. **0x472e46 in single player holds a popup at cooldown 1** (only MP closes it by time: kind 4 at 0, others at 1600).

## rhost limits hit

- **The paged shadow's page-fault frames clobber stack residue** (rh_state.c shadow_paged): the first write to a
  protected page faults, and the exception dispatch writes its CONTEXT / EXCEPTION_RECORD below the faulting esp. The
  army-state handlers write the army record (+0x154) before calling the executor 0x465d28, which reads its
  uninitialized waypoint locals (quirk #229) from just below the handler's esp: in the shadow, retail's run reads the
  fault frame instead of the pure run's residue. bot3-s104-t32 f19934: rl_465622 retail moved army 2 east, the port
  did not; a pure run's dump at f19934 matches the port (0x52f3fc / 0x52f400 / 0x52f404); the shadow pass then left
  the corpus (rng 7c9b6556 vs 5929ac9d). Under `--shadow-full` (copy shadow, no faults) all three verify over the
  corpus (§Evidence), so the table carries no row for them. Shared-file need: a per-row copy-shadow flag (rh_hooks.h /
  rh_state.c), or pre-faulting a row's pages before its retail side runs.
- **A row nested under another row is shadowed in one pass only** (its first depth): the 54 scenarios that reach
  0x46dbf5 / 0x46dd34 / 0x46da4b under 0x4676e0 verify them through 0x4676e0's shadow, the row itself in the 3 where it
  is reached at depth 1 (the per-table iterations shadowed each at its own depth).

## Evidence

- Whole corpus, every rl row of the tree (`RHOST_JOBS=3 rhost.py verify --target wine -j 3 --hooks '^rl_'`, 59 scenarios,
  2026-11-04 20:35–22:26, after the seams below were switched): **VERIFY: 412/412 reached with 0 divergences, 0 never
  called; hybrid PASS; 0 failed run(s)**. This partition's 68 rows: 2 576 315 shadowed calls (+ 5.7 M unshadowed
  samples of the 5 RH_SH_SAMPLE leaves 0x46c080 / 0x46c147 / 0x46de94 / 0x46de05 / 0x46c2de), 0 divergences, 0 skipped.
  A row nested under another row of the same table (0x46dbf5 / 0x46dd34 / 0x46da4b / 0x46c2de under 0x4676e0 …) is
  shadowed in its first pass only (3 scenarios); the outer shadow covers the subtree.
- Iterations with only this table (each row at its own depth): newgame-playnow / bot-s1-t8 / endturn-12 / custom-tour /
  bot3-s104-t32, and bot3cen-s209-t44 / grain-and-save / econ-tour / bot3-s103-t36: 68/68 reached, 0 divergences,
  hybrid PASS.
- Thinnest reach: 0x46965a (grain-and-save, 1 call), 0x46958f (4 scenarios), 0x467532 (5), 0x46921d (15),
  0x47265d (13), 0x472392 / 0x472875 (16).
- The handlers, `--extra=--shadow-full --hooks '^rl_465(486|622|761)$'` (a build with their rows; the copy shadow takes
  no page faults), whole corpus 2026-11-04 22:36 – 11-05 00:20: **VERIFY: 3/3 reached with 0 divergences; hybrid
  PASS; 0 failed run(s)** — rl_465486 4 374 calls / 35 scenarios, rl_465622 2 990 187 / 54, rl_465761 2 953 / 3.

## Seams switched to the ports

rl_io.c: 0x46ea61 (the realm clear), 0x46a037 / 0x467770 (the load path); rl_render.c: 0x46dbf5 / 0x46dd34; rl_ui2.c:
0x472ae0, 0x472bc5; rl_ui3.c / rl_glue.c: 0x472bc5; engine_bf_action.c / engine_bf_strike.c / engine_bf_setup.c: 0x46c2de.
engine_bf_move.c's static wrappers around engine_path.c's 0x472392 / 0x47265d / 0x472875 (load / store a struct) are
not engine_retail_call seams: left as they are (rl_472392 / rl_47265d / rl_472875 can replace them).
