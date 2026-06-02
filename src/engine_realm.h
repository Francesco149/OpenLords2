/*
 * OpenLords2 — per-realm strategic turn-state updates (the 0x448440
 * turn-advance engine's per-realm passes).  See engine_realm.c.
 *
 * These operate on the per-army/realm record block (g_per_army_768, save
 * entry 4, retail 0x53f9b0, 0x300-byte stride, slot 0 reserved) and the
 * per-player strategic block (g_player_strategic_352, entry 3, retail
 * 0x57bf00, 0x160 stride), both already named save globals — referenced
 * directly here (this module is one of their per-turn mutators), not bound.
 */

#ifndef OPENLORDS2_ENGINE_REALM_H
#define OPENLORDS2_ENGINE_REALM_H

#include <stdint.h>

#include "engine_event_queue.h"    /* engine_event_queue_state_t */

/* Port of FUN_0044baea @ 0x44baea (693 B, __stdcall void) — the per-realm
 * approval-rating roll-forward, one of the per-turn passes the turn-advance
 * orchestrator (0x448440) runs.  For every realm 1..s_slot_0056d5dc:
 *
 *   - snapshot the current rating (+0x0c) into "previous" (+0x0d), then fold
 *     in this turn's three signed adjustment sources (+0x0e, +0x10, +0x11) —
 *     each added in turn as a signed byte (8-bit wraparound between steps, as
 *     retail writes the byte back each time);
 *   - mirror the three raw adjustments into the display fields (+0x12/+0x14/
 *     +0x13) and clear the per-turn deltas (+0x15, +0x17, +0x219, +0x194);
 *   - a zero-population realm with a non-human owner resets to 50 (+0x0c =
 *     +0x17 = 0x32); an UNOWNED realm below 75 nudges up by 5;
 *   - decay the separate +0x2f4 countdown by 5 (floor at the != 0 guard);
 *   - clamp the rating to 0..100 (signed);
 *   - accumulate it into the lifetime sum (+0x1c) and store the running
 *     average +0x18 = sum / s_slot_00553240 (the turn counter).
 *
 * PRECONDITION: s_slot_00553240 (the turn counter, save entry 31) must be
 * >= 1 — retail's caller 0x448440 increments it before invoking this pass, so
 * the +0x18 average division is never by zero.  Field semantics (rating /
 * adjustment sources) are provisional; ported byte-exact from the decompile,
 * r2-verified for the signed arithmetic and the divide. */
void engine_realm_advance_rating(void);

/* Port of FUN_0044bf04 @ 0x44bf04 (118 B, __stdcall void) — the per-realm
 * provisioning settle pass (another 0x448440 turn-engine pass).  For every
 * realm 1..s_slot_0056d5dc it re-runs engine_army_economy_provision at the
 * season-prev phase (s_slot_00554470), then snapshots the settled economy
 * fields +0x178 → +0x18c and +0x17c → +0x190 (the per-realm history mirrors).
 * No-ops on its economy callee when the army-record block is unbound; the two
 * field copies operate on g_per_army_768 directly. */
void engine_realm_settle_provisions(void);

/* Port of FUN_0044a6ba @ 0x44a6ba (903 B, __stdcall void) — the per-realm
 * emigration / popularity-migration pass (another 0x448440 turn-engine pass).
 * Population drifts away from a realm toward whichever of its neighbours has
 * the highest approval rating (+0x0c).  Three passes over realms 1..count:
 *
 *   1. Clear this turn's migration scratch for every realm: the outflow
 *      amount (+0x3c), the inflow total (+0x40), the top-contributor amount
 *      (+0x44), the 16-slot source-realm list (+0x48..+0x57), the chosen
 *      destination (+0x58) and the top-contributor key (+0x59).
 *
 *   2. For each realm, scan its neighbour list (+0x5c.., count at +0x5a) for
 *      the highest-rated neighbour.  If one out-rates this realm, the number
 *      of emigrants is
 *          rate   = percent_of(rating_gap, (100 - rating) / 3)
 *          amount = percent_of(population (+0x24), rate),  capped at 100,
 *      and HALVED for an unowned realm (owner byte +0x05 == 0).  When the
 *      amount is positive it is recorded as our outflow (+0x3c), added to the
 *      destination's inflow total (+0x40), the destination is stamped in
 *      +0x58, and our key is registered in the destination's source list.
 *
 *   3. For each realm that received emigrants (+0x40 > 0), record the single
 *      largest contributor's amount (+0x44) and key (+0x59).
 *
 * Operates on g_per_army_768 only (all fields are per-realm record offsets);
 * the sole callee is the ported percentage helper engine_math_percent_of.
 * Field semantics (rating / population / neighbour list) are provisional but
 * the arithmetic is byte-exact, r2-verified.  NOTE the pass-2 source-list
 * registration has NO loop break (engine-quirks): the first realm to feed a
 * destination fills all 16 slots with its own key, so later feeders find none
 * free and pass 3 only ever sees that first contributor. */
void engine_realm_migrate_population(void);

/* Port of FUN_004ad6c0 @ 0x4ad6c0 (524 B, __stdcall void) — the per-realm
 * territory-income aggregation + economy re-plan pass (another 0x448440
 * turn-engine pass).  Three passes with FIXED bounds (16 realms, 150
 * territories — not the live realm count):
 *
 *   1. Clear both per-realm income accumulators (+0x198, +0x19c) for realms
 *      1..16.
 *
 *   2. Scan territories 1..150 in g_per_territory_420 (save entry 5, retail
 *      0x52f0b0, 0x1a4 stride).  A territory counts only if its status byte
 *      (+0x08) is 1 or 2 AND its gate flag (+0x198) is zero.  Its value
 *      (+0x168) is added to the owning realm's (key at +0x10) accumulator —
 *      +0x198 when the realm's owner (+0x05) matches the territory's owner
 *      (+0x00) OR is named by that owner's strategic +0x81 ("own / allied"),
 *      otherwise +0x19c ("foreign").
 *
 *   3. Re-run the per-realm economy plan for realms 1..16 at the current
 *      season s_slot_0057c92c (entry 25): engine_army_economy_provision →
 *      _plan_production → _select_candidate.
 *
 * Touches only the shared save blocks (g_per_territory_420, g_per_army_768,
 * g_player_strategic_352) and the three already-ported economy callees — no
 * new globals.  Field semantics (territory status / owner / value) are
 * provisional; offsets and the fixed bounds are r2-verified.  The economy
 * callees no-op when their army-record block is unbound, so passes 1-2 can be
 * exercised in isolation. */
void engine_realm_collect_territory_income(void);

/* Port of FUN_004ae7dd.  Per-turn realm stat-history recorder (a turn-advance
 * pass of 0x448440): snapshot every realm's population (army +0x24) and approval
 * (army +0x0c) into the current slot of the history ring g_realm_history (save
 * entry 10, a 400-turn × 16-realm grid of 8-B records), then advance the three
 * ring cursors — count (saturates at 400), oldest page (wraps at 400) and write
 * index (wraps at 400).  This is the data the population/popularity history
 * graphs plot.  Touches only g_realm_history, g_per_army_768 and the three
 * cursor scalars (entries 66/67/68). */
void engine_realm_record_history(void);

/* Port of FUN_00448d16.  Per-realm mid-turn scratch reset (a turn-advance pass
 * of 0x448440): for realms 1..s_slot_0056d5dc, zero four turn-accumulator bytes
 * (+0x1fb/+0x1fc/+0x1fd/+0x1a8) in each army record so they start clean for the
 * second workforce-allocation dispatch.  Touches only g_per_army_768. */
void engine_realm_clear_turn_scratch(void);

/* Port of FUN_00449889 @ 0x449889 (1253 B, void) — the per-realm mood /
 * standing machine, a turn-advance pass of 0x448440 (run right after the
 * random-event onset roll).  It maintains each realm's "standing" band — the
 * popularity/order level that drives the unrest-marker overworld cells and the
 * collapse / triumph notifications.  Over realms 1..s_slot_0056d5dc:
 *
 *   1. Snapshot each realm's current standing level (army +0x21b) into its
 *      previous-level field (+0x21c).
 *
 *   2. Derive this turn's base mood delta from the cached 7-bit RNG byte and
 *      the event-mode (s_slot_0057c934, the season 1..4): a per-mode constant
 *      (8 / 24 / 12 / −12) minus rng_b/8.  Apply it to every realm's mood
 *      accumulator (+0x21d, a signed byte with 8-bit wrap).
 *
 *   3. Pick a "focus" realm — rng_a's low nibble, else the previous focus
 *      (s_slot_00554020) + 1, else realm 1, clamped to 1..count — and give it
 *      an extra base dose plus its neighbours (list at +0x5c, count +0x5a)
 *      half-doses; each also folds in its event-mode mood delta (FUN_00449d6e,
 *      keyed on the realm's disposition byte +0x21e).
 *
 *   4. Classify each accumulator into a 5-band standing level (+0x21b): <5 → 5
 *      (and reset the accumulator to 30), <20 → 4, <70 → 3, <95 → 2, else 1
 *      (and reset to 70).  In modes 1 and 4 the two best bands are demoted to
 *      0: band 1 always, band 2 when the accumulator still exceeds 74.
 *
 *   5. Per realm: clear its standing-marker overworld cells (FUN_0046942c,
 *      types 0x17/0x18 → 0); if it collapsed (band 1) notify the local player
 *      with event code 0x8f and stamp one roster cell as a 0x18 marker; if it
 *      peaked (band 5) notify with 0x90 and stamp a 0x17 marker
 *      (FUN_00469a9c).  Notifications fire only for the local player's own
 *      realm and route through engine_event_queue_post.
 *
 *   6. If standings are disabled (s_slot_0053f25c == 0), force every realm to
 *      the neutral middle band (3).
 *
 * Two runtime inputs are passed in rather than read from their Win32-only
 * engine.c file-statics (which the host test build does not link): the cached
 * 7-bit RNG bytes rng_7bit_b (retail DAT_0058fd60) and rng_7bit_a (DAT_0058fd7c).
 * The event-queue state `evq` receives the collapse/triumph notifications; it
 * is only dereferenced on the local player's own realm reaching an extreme, so
 * tests that avoid that case may pass NULL.  Field semantics (standing band /
 * mood accumulator / disposition) are provisional; offsets r2-verified against
 * the 0x300 stride.  Touches g_per_army_768, the overworld cells (g_misc_1360 /
 * g_overworld_map, via the territory module) and the focus scalar
 * s_slot_00554020; reads s_slot_0056d5dc / 0057c934 / 0057c8cc / 0053f25c. */
void engine_realm_update_mood(uint32_t rng_7bit_b, uint32_t rng_7bit_a,
                              engine_event_queue_state_t *evq);

/* Port of FUN_0044b99a @ 0x44b99a (155 B, __cdecl void) — recompute a player's
 * owned-realm tally.  Clears the player's strategic +0x28 byte, then sums every
 * realm's +0x16 byte over the realms owned by that player (army +0x05).  The
 * tally feeds the per-realm tax-rate adjustment in engine_realm_accrue_income;
 * it is also called by the (unported) sibling 0x44b80b, so it is exposed rather
 * than kept static.  Touches only g_player_strategic_352 / g_per_army_768;
 * field semantics (+0x16 source, +0x28 target) are provisional, byte widths
 * match the decompile. */
void engine_realm_recompute_owner_tally(int player);

/* Port of FUN_0044b59b @ 0x44b59b (624 B, __cdecl void) — the per-realm
 * tax-income pass, a turn-advance pass of 0x448440 run right after the mood
 * machine (FUN_00449889).  For every realm 1..s_slot_0056d5dc:
 *
 *   1. Recompute the owning player's realm tally (engine_realm_recompute_owner_
 *      tally, FUN_0044b99a).
 *
 *   2. Pick the effective standing band: army +0x1c0, but when the +0x1c3 gate
 *      is set it is capped by +0x1c1 (→ 0 if that cap is 0, else min(band, cap)).
 *
 *   3. Map the band to a per-capita tax rate (1→480, 2→560, 3→640, 4→720,
 *      5→800, else 320 basis points) — unless the realm's +0x1a8 turn-scratch
 *      byte is set (an event such as 0x13b sets it), which forces the rate to 0
 *      and so zeroes the realm's income this turn.
 *
 *   4. income = percent_of(population +0x24, rate) then percent_of(that, the
 *      realm's tax setting +0xb9); store it in +0xbc.
 *
 *   5. Write the per-realm rate-adjustment byte +0x0e = (5 − taxsetting) +
 *      the owner's tally (+0x28), as signed-byte arithmetic.
 *
 *   6. Credit the income: an unowned realm (owner 0) accrues into its own pool
 *      +0x1f4; an owned realm credits the owner's three treasury/cashflow
 *      accounts +0x118, +0xf4 and +0xf8.
 *
 * Touches only the shared save blocks (g_per_army_768, g_player_strategic_352)
 * and the army count; uses engine_math_percent_of (0x404d6b).  No new globals.
 * Field semantics (standing band / tax setting / treasury accounts) are
 * provisional; offsets r2-verified against the 0x300 / 0x160 strides. */
void engine_realm_accrue_income(void);

/* Port of FUN_0044b80b @ 0x44b80b (399 B, __cdecl) — recompute ONE realm's
 * PROJECTED tax income and refresh the per-player income totals.  This is the
 * live "recompute the economy" path: it runs both inside the per-turn re-plan
 * pass (engine_realm_replan_economies, FUN_00448545) and directly from the
 * management UI / strategic events whenever a realm's tax setting changes (six
 * retail call sites).  It is the sibling of engine_realm_accrue_income but
 * deliberately differs: it does NOT honour the +0x1a8 event-scratch gate and
 * credits NO treasury — it only fills the projection fields.  For realm `realm`:
 *
 *   1. Effective standing band: army +0x1c0, capped by +0x1c1 when the +0x1c3
 *      gate is set (→ 0 if the cap is 0, else min(band, cap)) — the same rule
 *      as the accrual pass.
 *   2. Map the band to a per-capita rate (1→480, 2→560, 3→640, 4→720, 5→800,
 *      else 320) and compute income = percent_of(population +0x24, rate) then
 *      percent_of(that, tax setting +0xb9); store it in the PROJECTED field
 *      +0xc0 (distinct from the accrual field +0xbc).
 *   3. +0x0f = 5 − taxsetting (signed byte); +0x16 = the tax-setting tally
 *      modifier (low byte of a baked .data table indexed by the tax setting) —
 *      the byte engine_realm_recompute_owner_tally sums into the owner's +0x28.
 *   4. Recompute the owning player's tally (engine_realm_recompute_owner_tally,
 *      FUN_0044b99a), then refresh all players' projected-income totals
 *      (FUN_0044ba35: clear +0x15c for players 1..5, re-sum each realm's +0xc0).
 *
 * Touches only g_per_army_768 / g_player_strategic_352 and the realm count; uses
 * engine_math_percent_of.  Field semantics provisional; offsets and the rate /
 * tally tables r2-verified against the 0x300 / 0x160 strides. */
void engine_realm_recompute_realm_income(int realm);

/* Port of FUN_00451061 @ 0x451061 (239 B, void) — the per-realm army recount
 * pass, a turn-advance pass of 0x448440.  Two passes over the save state:
 *
 *   1. For realms 1..s_slot_0056d5dc, clear the live army-count byte +0x1a4,
 *      the last-army-index byte +0x1a5, and an unrelated per-realm flag +0x15c.
 *
 *   2. For territory records 1..150 in g_per_territory_420, every record whose
 *      kind byte +0x08 == 3 (a placed army — the kind the army-start path
 *      stamps via engine_territory_place_entity) is tallied into its owning
 *      realm (record +0x10): bump the realm's byte count +0x1a4 and i32 count
 *      +0x1a0, and record the army's record index in +0x1a5.
 *
 * QUIRK #148: pass 1 resets the byte count +0x1a4 but NOT the i32 +0x1a0, yet
 * pass 2 increments both — so +0x1a4 holds the live per-turn count while +0x1a0
 * accumulates monotonically across turns.  Touches only g_per_army_768 and
 * g_per_territory_420; no callees, no new globals.  Field semantics provisional;
 * offsets r2-verified against the 0x300 / 0x1a4 strides. */
void engine_realm_recount_armies(void);

/* Port of FUN_0044bd9f @ 0x44bd9f (357 B, __stdcall void) — the per-realm
 * tax-policy → approval-modifier pass, a turn-advance pass of 0x448440.  It
 * turns the realm's tax-rate policy into the approval modifier that the
 * approval roll-forward (engine_realm_advance_rating) folds into the main
 * rating.  For every realm 1..s_slot_0056d5dc:
 *
 *   1. Look up a signed adjustment in a 2D table keyed by [tax policy +0x15d]
 *      (0..6) × [the realm's current standing band +0x09], and add it to the
 *      tax-approval value (+0x0b) as signed-byte arithmetic.
 *   2. Clamp +0x0b to 0..100.
 *   3. A zero-population realm with a non-human owner (strategic +0x05 == 0)
 *      resets to 50.
 *   4. Reclassify +0x0b into a band 0..4 via engine_math_lookup_threshold
 *      (cutoffs 10/35/65/90/100, fallback band 3) and store it back to +0x09;
 *      then emit the per-band modifier (+0x10 ← {-10,-5,0,1,2}[band]) the
 *      approval roll-forward reads as its +0x10 adjustment source.
 *
 * The +0x09 column is the PREVIOUS turn's band (read before the rewrite), so
 * this turn's classification becomes next turn's lookup column.
 *
 * QUIRK #149: tax policy 6 has no row in the 2D table and aliases into the
 * adjacent band-threshold table (the two are baked as one contiguous array to
 * reproduce this), and the threshold lookup's fallback means a realm at exactly
 * 100 tax-approval lands in band 3 — one band below a realm at 99.  Touches
 * only g_per_army_768 / g_player_strategic_352; uses engine_math_lookup_threshold.
 * Field semantics provisional; offsets and tables r2-verified. */
void engine_realm_apply_tax_approval(void);

/* Port of FUN_0044bf7a @ 0x44bf7a (91 B, __stdcall void) — the per-realm
 * territory-development spread pass of the turn-advance engine (0x448440).  For
 * every realm 1..s_slot_0056d5dc it runs three sub-passes (the retail dispatcher
 * also threads the season into the third, which never reads it):
 *
 *   1. FUN_0044bfd5 — roll the standing accumulator +0x208 forward by
 *      6·(+0x1ff) − 3·(+0x201), clamp to [-100, 100], force to 0 while
 *      standings are disabled (s_slot_0053f25c == 0).
 *   2. FUN_0044c093 — distribute the surplus pool +0xdc across the realm's
 *      developable overworld cells (its g_misc_1360 roster row), starting from
 *      the highest-value cell and walking the 20-slot roster circularly: up to
 *      200 per cell, spill back anything over 800, update the per-cell value
 *      (+0x90 word) and stamp the cell's overworld terrain type to the matching
 *      development stage (0x19/0x1a/0x1b/0x1c/0x01), stopping when the pool runs
 *      out.  A cell is developable only when non-empty and its terrain type is
 *      >= 0x19 (already a development-stage tile); a maxed cell is stamped 0x01,
 *      which is < 0x19 and so stops qualifying on later turns.
 *   3. FUN_0044c278 — project that distribution without mutating the cells,
 *      leaving four planning metrics: +0xe4 total fill capacity, +0x20c count
 *      of cells that would reach the 800 cap, +0x214 turns to fill the best
 *      cell, and +0xe0 reset to −1.  Bails early (leaving the metrics cleared)
 *      when the realm has no developable cell.
 *
 * The shared highest-value-cell helper (retail FUN_0044c5fc / FUN_0044c53b)
 * writes the winning slot index to +0x210; its two retail variants differ only
 * in the no-cell default (99 sentinel vs 0).  QUIRK #150: FUN_0044c093 invokes
 * both back-to-back, so the first (sentinel) result is overwritten unused.
 * Touches g_per_army_768, the realm roster rows g_misc_1360, and the overworld
 * cells (g_overworld_map, via engine_territory_set_cell_type); reads the realm
 * count and the standings gate.  Field semantics provisional; offsets and band
 * cut-offs r2-verified.  The cell-type stamps update overworld graphics and so
 * want eventual visual verification, but the arithmetic is deterministic and
 * unit-tested in isolation (set_cell_type no-ops when the cell grid is unbound). */
void engine_realm_spread_development(void);

/* Port of FUN_00448545 @ 0x448545 (96 B, __stdcall void) — the per-turn economy
 * re-plan pass of the turn-advance engine (0x448440).  For every realm
 * 1..s_slot_0056d5dc it re-runs the provisioning decision (engine_army_economy_
 * provision, FUN_0044df5f) at the current event-mode season (s_slot_0057c934),
 * re-plans the realm's resource production (engine_army_economy_plan_realm,
 * FUN_004485a5) at the map season (s_slot_0057c92c), then recomputes its
 * projected tax income (engine_realm_recompute_realm_income, FUN_0044b80b).  No
 * new globals; the two economy callees no-op when the army-record block is
 * unbound. */
void engine_realm_replan_economies(void);

/* Port of FUN_00449ef3 @ 0x449ef3 (1991 B, __stdcall void) — the per-realm
 * population-growth turn pass of the turn-advance engine (0x448440).  For each
 * realm 1..s_slot_0056d5dc it advances the head-count (+0x24) by births and
 * deaths derived from a population-bracket base birth-rate (rdata 0x4d6308)
 * scaled 25/50/75/100/120% by the realm's standing band (+0x0c), a
 * region+season death-rate (rdata 0x4d63a8/0x4d63c0, the latter keyed by the
 * just-ended season s_slot_0057c934), a bounded signed event modifier (+0x1fb,
 * up to 20% of pop), then emigration (+0x3c) and immigration (+0x40).  Records
 * the previous head-count (+0x28), the dominant change category (+0x5b), the
 * |growth| percent (+0x2c) and the population tier (+0xb8), and refreshes the
 * overworld graphics — the capital footprint descriptor by population band via
 * engine_territory_stamp_block and the four village settlement tiles via
 * engine_territory_update_village_tiles.  Touches g_per_army_768 and the
 * overworld cell grid g_overworld_map; reads the realm count + season scalar. */
void engine_realm_advance_population(void);

/* Port of FUN_0044b042 @ 0x44b042 (704 B, void) — the coalition-group builder,
 * the producer half of the turn-driver's coalition pass FUN_0044ae3c (its
 * consumer half, the disconnected-group elimination FUN_0044ae51, reaches the
 * combat-init chokepoint FUN_004ac3c6 + posts popups, so it is deferred).
 *
 * It partitions every active army (per-army owner byte +0x05 != 0) into
 * "coalitions" = the adjacency-connected components of each owner's armies,
 * where adjacency is army B appearing in army A's +0x5c list (FUN_00467e2c).
 * The result lands in the runtime group table (retail 0x568240, 17 groups ×
 * 0x1c B: +0x00 i32 strength, +0x04 owner key, +0x05.. 20 member army ids),
 * rebuilt from scratch each call.  Algorithm: clear the table; count active
 * armies; run a guarded fixpoint (≤99 rounds) that each round either joins
 * ungrouped armies into an adjacent same-owner group (FUN_0044b391) or, if none
 * joined, seeds one new group from the first ungrouped army (FUN_0044b302),
 * skipping already-grouped armies (FUN_0044b4d0); finally tally the active
 * group count (retail _DAT_00553ef4) and sum each group's member populations
 * (army +0x24) into its strength field.
 *
 * Touches only g_per_army_768 and the realm count s_slot_0056d5dc; the group
 * table + count are module-private runtime scratch exposed for inspection (and
 * the future FUN_0044ae51 port) through the accessors below.  Field semantics
 * (owner / population / adjacency list) are provisional; offsets r2-verified
 * against the 0x300 / 0x1c strides. */
void engine_realm_build_coalitions(void);

/* Inspectors for the coalition table built by engine_realm_build_coalitions
 * (groups 0..16; member slots 0..19).  Group key 0 marks an empty group. */
int engine_realm_coalition_active_count(void);
int engine_realm_coalition_group_key(int group);
int engine_realm_coalition_group_strength(int group);
int engine_realm_coalition_group_member(int group, int slot);

/* Bind the optional rebellion-spawn hook used by engine_realm_escalate_unrest
 * (retail FUN_004ac185).  It returns nonzero when a rebellion is spawned for
 * the realm, which resets that realm's unrest.  The real function reaches the
 * deferred combat-init chokepoint FUN_004ac3c6, so this is a seam — NULL (the
 * default) means no rebellion fires. */
void engine_realm_bind_rebellion_fn(int (*fn)(int realm));

/* Port of FUN_0044aa41 @ 0x44aa41 (1019 B, void) — the per-realm unrest /
 * rebellion-escalation turn pass of the turn-advance engine (0x448440).  For
 * every realm 1..s_slot_0056d5dc it advances the realm's unrest level (+0x20,
 * band 0..4) from its approval rating (+0x0c, signed) and owner:
 *
 *   - An UNOWNED realm (owner +0x05 == 0) is forced to no unrest.
 *   - An AI realm (owning player's strategic +0x05 == 0) drifts silently:
 *     approval >= 41 → calm (0); 11..40 → unrest eases by one; below 11 a
 *     starving realm (approval < 1) climbs by one (cap 4).
 *   - A HUMAN realm warns first: the first time approval dips below 30 it posts
 *     a single warning (event 0x92) and latches +0x21 (cleared once approval
 *     rises back above 29); thereafter approval < 25 raises unrest one band
 *     (posting the band notification 0x96..0x99) and approval >= 25 restores
 *     order (0).
 *
 *   Whenever unrest is RAISED, a realm now at the top band (>3) may erupt into
 *   a rebellion via the bound rebellion hook (FUN_004ac185), which resets it.
 *
 * Notifications route through engine_event_queue_post (audience = the realm's
 * owner).  Touches only g_per_army_768 / g_player_strategic_352 + the realm
 * count.  Field semantics (unrest band / warning latch) are provisional; the
 * thresholds and branch structure are ported byte-faithfully from the decompile
 * (its unreachable approval<1→band-4 arm under the approval<25 gate is elided).
 * The rebellion hook is a deferred bound seam (combat-init). */
void engine_realm_escalate_unrest(engine_event_queue_state_t *evq);

#endif /* OPENLORDS2_ENGINE_REALM_H */
