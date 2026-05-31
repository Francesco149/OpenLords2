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

#endif /* OPENLORDS2_ENGINE_REALM_H */
