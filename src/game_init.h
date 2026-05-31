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

#include "engine.h"   /* engine_rng_in_t — the threaded LFSR state (RNG seam) */

/* Table-clear phase of 0x497ced — zero the per-army, unit-type and
 * per-territory save blocks.  Runs once at new-game start, ahead of the
 * scalar-seed and per-subsystem seed phases. */
void engine_game_init_clear_tables(void);

/* Render/anim gate counter — retail DAT_0053f0c4.  NOT a save scalar;
 * the scalar-seed phase resets it to 0 at new-game.  Its only reader in
 * retail is the unported strategic-map render/anim path (0x41e1dd, which
 * gates a draw on `== 10`), so it is parked here, owned by game_init,
 * until that subsystem lands and claims it. */
extern int g_anim_gate_53f0c4;

/* Scalar-seed phase of 0x497ced — the 20 dword world-state globals
 * written between the NPC-schedule seed and the first heavy seeder
 * (17 save-schema scalars + the 2 turn-handshake runtime globals + the
 * anim-gate counter above).  Seeds the calendar block (year + season,
 * each with prev/next mirrors) and zeroes the rest. */
void engine_game_init_seed_scalars(void);

/* Leaf seeder FUN_0042847c — reset the 16-entry value table at save
 * entry 82 (s_slot_005532a0, 16 records × 16 B) to its defaults: each
 * record's first dword from the retail DAT_004d8910 table, second dword
 * (+4) zeroed, the remaining 8 B per record untouched.  Reused by the
 * new-game init and three other reset paths (0x43723a/0x49a010/0x4a98fe). */
void engine_game_init_seed_value_table(void);

/* Heavy seeder FUN_00497e65 — new-game randomiser.  One retail unit that
 * consumes a contiguous run of RNG draws (so `rng` is threaded in/out)
 * and produces three pieces of randomised new-game state, in this order:
 *
 *   1. Player roster shuffle.  Re-seats the `active` occupied roster
 *      pairs (g_player_roster, seats 1..5; seat 0 — the reserved front
 *      slot — is excluded from both the count and the shuffle) into
 *      random array positions, carrying each pair's {army-id, seat-id}
 *      payload together.  Assumes the roster has already been compacted
 *      (player_roster_compact runs earlier in setup), so the occupied
 *      pairs are contiguous at indices 1..active.
 *
 *   2. 48-entry permutation table (save entry 178, s_slot_0057cae0):
 *      a near-random placement of the identity values 0..47, each
 *      dropped into a random empty slot (never its own index).
 *
 *   3. 48-entry value/threshold table (save entry 191, s_slot_00553080):
 *      each cell a categorical roll of (rng & 7) → 0 (roll 5), 100
 *      (roll 6), else roll·5+6 {6,11,16,21,26,41}.
 *
 * Both 48-tables are indexed in-game by the shared cursor DAT_005653f8
 * (the permutation feeds DAT_00553528; the value table is compared as a
 * threshold by the AI handlers at 0x48acd2 / 0x48b02b).
 *
 * Pure given `rng`: the live orchestrator (0x497ced, step 5) passes
 * the engine's threaded LFSR state so the draw sequence continues from
 * the prior seeders; tests pass a fixed seed for determinism. */
void engine_game_init_seat_shuffle(engine_rng_in_t *rng);

/* Leaf seeder FUN_004ac904 — seed the unit/opponent-type table (save
 * entry 6, g_unit_table_20, 13 records × 20 B) from six retail .rdata
 * const columns.  The row count comes from the scenario/config selector
 * s_slot_0056d5dc (entry 77) via a lookup table, clamped 1..12 and
 * persisted to s_slot_00554030 (entry 122).  Clears the block first
 * (re-issuing the FUN_0046f2ae unit-table clearer), then fills records
 * 1..count; record 0 and the untouched per-record bytes stay cleared. */
void engine_game_init_seed_unit_table(void);

/* Init-only seeder FUN_0049b35e — purge (memset 0x160) the strategic
 * record of any player slot 1..5 (g_player_strategic_352, entry 3) that
 * is unoccupied (+0x4 clear) or occupied but uninitialised (+0x5 and
 * +0x7 both clear).  Scrubs leftover/garbage player state at new-game. */
void engine_game_init_purge_inactive_records(void);

/* Init-only seeder FUN_004a1c53 — reset the per-player sub-tables at
 * new-game: zero entry 144, selectively clear the entry-146 6×5 grid
 * (FUN_004a2584), recompute the first-active-non-local-player cursor
 * (player_first_active_nonlocal), then reset a 5-entry sub-table (five
 * 0x10-B elements at record offset 0x94) plus two standalone bytes for
 * each player slot 1..5 — AI players seed each element's lead byte to 5. */
void engine_game_init_reset_player_subtables(void);

/* Init-only seeder FUN_004ae74d — reset the castle-shadow table (save
 * entry 10, g_castles_shadow, 6400 × 8-B records) at new-game: for every
 * record zero the +0 dword and the +4 byte (leaving +5..+7, where +5 is
 * the per-castle-slot back-pointer, intact), then zero the three world
 * scalars at entries 66/67/68.  A direct callee of the 0x497ced
 * orchestrator. */
void engine_game_init_clear_castle_shadow(void);

/* Army-neighbourhood probes FUN_00428007 / FUN_00428078 — shared
 * new-game / AI-setup utilities (callees of the 0x497ced → 0x427ed0
 * chain plus three other setup paths: 0x4ac185 / 0x4292af / 0x4a9a9a).
 * Each looks up army `id`'s map coordinate from the 3-byte-stride
 * sub-table at offset 0x6c of save entry 4 (g_per_army_768) and reports
 * whether a matching map cell lies within Chebyshev radius 1..3, using
 * the two engine_map_query box predicates:
 *
 *   _bit0       → rawflag bit 0 set   (FUN_00428007 → FUN_0046cfbd)
 *   _only_bit1  → rawflag is 0 or 2   (FUN_00428078 → FUN_0046d130)
 *
 * On a match the found cell is reported via *out_col / *out_row (pass
 * NULL to ignore); FUN_00427ed0 uses it to place the army's start
 * marker AT that cell.
 */
int engine_game_init_army_near_rawflag_bit0(int id, int *out_col, int *out_row);
int engine_game_init_army_near_rawflag_only_bit1(int id,
                                                 int *out_col, int *out_row);

/* Init-only seeder FUN_00427ed0 — place each army's strategic-map start
 * marker.  For every army id in save entry 139 (s_slot_00569518, up to 6,
 * zero-terminated) it probes for a nearby map feature and, on a hit,
 * allocates a type-6 entity at the matched cell (engine_territory_place_entity)
 * and stamps the army's id/index into the new record; bumps the placed
 * count at entry 78.  A direct 0x497ced new-game callee. */
void engine_game_init_place_army_starts(void);

/* ──────────────────────────────────────────────────────────────────
 * New-game per-realm / country setup pass (FUN_0049bd99)
 * ────────────────────────────────────────────────────────────────── */

/* The five new-game "preset selection" inputs the config-screen apply
 * functions (retail FUN_00499dc3 / FUN_00499e5d, both unported) latch from
 * the player's scenario/race/handicap choices.  They are NOT save-schema
 * scalars (they are recomputed each new-game), and 0x49bd99 is their sole
 * consumer, so this module owns them.  Set them before
 * engine_game_init_setup_realms; the live owner wires them to the config
 * apply, host tests call this directly.
 *
 *   economy_preset  (DAT_0053f27c) — row index into the per-army economy
 *                                    seed table (gold/stock/population +
 *                                    two bytes), applied to EVERY army.
 *   resource_preset (DAT_0053f270) — row index into the per-player
 *                                    resource-pool table (6 i32 → +0x140).
 *   troop_preset    (DAT_0053f274) — row index into the per-player troop
 *                                    table (7 i32); 0 disables the troop /
 *                                    starting-army block entirely.
 *   gold_base       (DAT_0053f278) — flat starting-gold base added to each
 *                                    player record +0x118.
 *   keep_level      (DAT_0053f280) — secondary-settlement tier byte stamped
 *                                    into each realm (+0x1c0); 0 = none. */
void engine_game_init_set_realm_presets(int economy_preset, int resource_preset,
                                        int troop_preset, int gold_base,
                                        int keep_level);

/* Optional battlefield-spawn seam — retail 0x4a9a9a(player, army, 0, 0),
 * the troop-muster → battlefield unit spawn (with its placement SFX).  The
 * spawn pulls in the unported 0x4a9xx/0x4acxx subtree and needs audible human
 * verification, so it is deferred: bind it to wire the real spawn, NULL skips
 * it.  Retail also relies on this call advancing the entity placer's
 * current-slot cursor (DAT_0052afe0), which the next line reads for the
 * player's starting-gold bonus — with the hook unbound that read uses
 * whatever slot the prior placement left (documented seam). */
void engine_game_init_bind_troop_spawn(void (*fn)(int player, int army));

/* The per-player troop muster pool (retail DAT_0053f6a0, 6 rows × 0x80 B,
 * runtime-only — NOT persisted).  Row `player` (1..5) holds seven unit-type
 * counts at +0x00,+0x10,…,+0x60 and their running total at +0x70.
 * engine_game_init_setup_realms is its producer (the recruitment/muster UI,
 * still unported, is the other reader).  Exposed for tests + the future UI;
 * returns the base of row `player`. */
uint8_t *engine_game_init_troop_pool_row(int player);

/* Port of FUN_0049bd99 @ 0x49bd99 (3063 B, __stdcall void) — the new-game
 * per-realm/country setup pass; a direct 0x497ced new-game callee, run right
 * after the roster compaction (0x49bc5f) and before the per-player subtable
 * reset (0x4a1c53).
 *
 * Two phases plus a finalise:
 *   1. Army seed — for every army 1..s_slot_0056d5dc, copy one economy preset
 *      row (economy_preset index, retail DAT_0053f9b0 fields +0x224/+0x228 gold,
 *      +0x250/+0x254 stock, +0x24/+0x28 population, bytes +0xb/+0xc/+0xd) into
 *      its record, then run the terrain census (engine_territory_census_army_
 *      terrain) and feature grading (engine_territory_grade_feature_cells).
 *   2. Player init — for each player slot 1..5 of g_player_strategic_352 that
 *      is active (human +0x5 set, or AI +0x7 set), zero its 0x160 record's
 *      working fields and seat defaults, map it to its army via g_player_roster,
 *      seed starting gold (+0x118), troops (troop_preset → the muster pool +
 *      army population, then the deferred spawn), the resource pool defaults
 *      (+0x120/+0x128/+0x130 = 0x32, +0x140.. from resource_preset), run the
 *      per-realm economy passes (allocate_realm_workforce, provision, plan_realm
 *      — each twice), and stamp the secondary keep (keep_level → engine_territory_
 *      stamp_secondary_keep).
 *   3. Finalise — give every army with no human/AI owner a +100 gold/stock
 *      bonus, then recenter + highlight the strategic map on the local player's
 *      army (engine_territory_recenter_on_capital / _highlight_key_cells).
 *
 * Requires the per-army block (engine_*_bind_army_records) and the world tables
 * bound; the economy/territory callees are individually NULL-safe.  Presets
 * come from engine_game_init_set_realm_presets; the troop spawn from
 * engine_game_init_bind_troop_spawn. */
void engine_game_init_setup_realms(void);

#endif /* OPENLORDS2_GAME_INIT_H */
