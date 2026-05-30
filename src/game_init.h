/*
 * OpenLords2 — new-game / world-state initialiser.
 *
 *   0x497ced (376 B, the new-game initialiser) → engine_game_init  [in progress]
 *
 * The single-player Start path (page-7 "Start" button → 0x4335f0 →
 * 0x4329ec) calls 0x497ced once to wipe and seed every live
 * world-state table before the engine flips to the ingame state.  It is
 * a ~25-call orchestrator: a table-clear phase, a scalar-seed phase,
 * the NPC-schedule seed (FUN_0049caaa, already ported), then a long
 * tail of per-subsystem seeders (army/castle/territory/AI/map).
 *
 * This module grows one phase at a time.  Landed so far:
 *
 *   TABLE-CLEAR PHASE — engine_game_init_clear_tables()
 *       Zeroes the three big per-record save blocks that every later
 *       seeder writes into:
 *
 *         FUN_0046f1dc  → g_per_army_768      (17 × 768 B, retail 0x53f9b0)
 *         FUN_0046f2ae  → g_unit_table_20     (13 × 20 B,  retail 0x568dc0)
 *         FUN_0046f3cd  → g_per_territory_420 (151 × 420 B, retail 0x52f0b0)
 *
 *       Each retail clearer loops its record count and memsets one
 *       record per iteration via a fixed-length zeroer (FUN_0046ea28 /
 *       FUN_0046eb09 / FUN_0046ead0 — all just `memset(p, 0, len)`).
 *       The loop bounds × stride cover the full named block, so the net
 *       effect is a whole-block clear; the per-record structure is kept
 *       for provenance and to match retail's call shape exactly.
 *
 * The scalar-seed phase, the heavy per-subsystem seeders, and the
 * 0x4329ec dispatch / engine-state flip are deferred to later
 * chips — see docs/PROGRESS.md.
 */

#ifndef OPENLORDS2_GAME_INIT_H
#define OPENLORDS2_GAME_INIT_H

/* Table-clear phase of 0x497ced — zero the per-army, unit-type and
 * per-territory save blocks.  Runs once at new-game start, ahead of the
 * scalar-seed and per-subsystem seed phases. */
void engine_game_init_clear_tables(void);

#endif /* OPENLORDS2_GAME_INIT_H */
