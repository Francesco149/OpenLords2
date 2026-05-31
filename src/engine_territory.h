/*
 * OpenLords2 — per-territory record helpers + overworld cell-grid resets.
 * See engine_territory.c.
 */

#ifndef OPENLORDS2_ENGINE_TERRITORY_H
#define OPENLORDS2_ENGINE_TERRITORY_H

#include <stdint.h>

#include "engine.h"            /* engine_rng_single_in_t */
#include "engine_dirty_rect.h"

/* Bind the per-territory record array (retail `DAT_0052f0b0`, 151
 * records × 0x1a4 bytes, slot 0 reserved). */
void engine_territory_bind_records(uint8_t *records);

/* Bind the 64×64×8 map-cell descriptor grid (retail `DAT_00522f90`),
 * mutable here because engine_territory_place_entity marks cells as it
 * claims them (cell +5 ← slot, +2 |= 1), and the cell resets below
 * bulk-clear it.  Same memory engine_map_query reads read-only. */
void engine_territory_bind_cells(uint8_t *cells);

/* Bind the render-invalidation state engine_territory_clear_cell_render_flags
 * marks full-screen.  Optional — leave unbound (NULL) and the repaint
 * request is skipped (host/headless). */
void engine_territory_set_dirty_state(engine_dirty_rect_state_t *state);

/* Bind the per-key cell roster (retail `DAT_0053ea00` = save entry 12,
 * g_misc_1360 — 17 rows × 20 i32) that engine_territory_rebuild_cell_roster
 * fills.  Required for the rebuild (NULL → no-op). */
void engine_territory_bind_cell_roster(int32_t *roster);

/* Bind the parallel per-key AI score table (retail `DAT_0053fa40`, 384
 * i16 per row) the rebuild zeroes on the new-game path.  Optional — NULL
 * skips that secondary clear. */
void engine_territory_bind_cell_score_table(int16_t *scores);

/* Bind the per-army record block (retail `DAT_0053f9b0` = save entry 4,
 * g_per_army_768 — 17 rows × 0x300 B, slot 0 reserved) that
 * engine_territory_build_army_adjacency writes the adjacency header of.
 * Required for the adjacency build (NULL → no-op).  The AI score table
 * (engine_territory_bind_cell_score_table) is a sub-view of this same
 * block at +0x90. */
void engine_territory_bind_army_records(uint8_t *records);

/* Bind the terrain-index render tile grid (retail `DAT_0055cea0`, 129×65
 * i32, row stride 0x104 B = 65 i32 contiguous) that the new-game tile
 * randomiser re-rolls.  Optional — NULL → randomiser is a no-op.  The same
 * memory the map-render modules bind read-only. */
void engine_territory_bind_terrain_tiles(int32_t *tiles);

/* Port of FUN_004651b9.  Zero the +0x14c and +0x153 bytes of every
 * territory record 1..150.  Used during new-game/scenario init. */
void engine_territory_clear_size_state(void);

/* Port of FUN_0046e1b0.  Allocate the first free record (slots 1..149)
 * and place a map entity at cell (col, row); returns 1 on success (cell
 * free + a slot available), else 0.  `kind` → record +0x08, `type` →
 * record +0x00.  See the .c for the full field map. */
int engine_territory_place_entity(int kind, int col, int row, int type);

/* The record slot the last successful place_entity allocated (retail
 * DAT_0052afe0).  Callers read it to stamp extra fields into the fresh
 * record; undefined before the first placement. */
int engine_territory_current_slot(void);

/* Port of FUN_0046c5b8.  Zero byte `field` (0..5 at the call sites) of
 * every overworld cell descriptor (all 4096 cells). */
void engine_territory_clear_cell_field(uint8_t field);

/* Port of FUN_0046df51.  Clear bit 0x20 of byte +2 of every overworld
 * cell, then mark the full screen dirty (if a dirty state is bound). */
void engine_territory_clear_cell_render_flags(void);

/* Port of FUN_0046da4b.  Rebuild the per-key cell roster (bound via
 * engine_territory_bind_cell_roster) from the overworld grid: index every
 * active feature-cell (+7 key 1..16, +1 bit 0x20 set) into its key row,
 * capped at 20 per key; cells beyond the cap are evicted.  Pass 1 to also
 * zero the AI score table (new-game), 0 to leave it (mid-game recompute). */
void engine_territory_rebuild_cell_roster(int reset_score_table);

/* Port of FUN_0046d9da.  Clear the AI-score entry for a single cell: scan
 * the cell's key row (key = its +7 byte) of the roster (bound via
 * engine_territory_bind_cell_roster) for the column holding cell_off, and
 * zero the matching i16 in the AI score table (engine_territory_bind_cell_
 * score_table).  No-op if the cell is not in its roster row, or if either
 * table is unbound.  cell_off is the cell's byte-offset into the grid. */
void engine_territory_clear_cell_score(int cell_off);

/* Port of FUN_0046d7f4.  Set an overworld cell's terrain type (byte +0) and
 * re-derive its +3 render-descriptor byte (type base + variant*4, keeping
 * the low 2 bits) and +2 flag byte (bit0 set; bits 2..4 = this type's
 * contribution; bit7 set only for the type band 15..22).  When type is 0
 * the cell's AI score is also cleared (engine_territory_clear_cell_score).
 * Requires the cell grid bound (engine_territory_bind_cells); variant is the
 * render tier (retail call sites pass 0). */
void engine_territory_set_cell_type(int cell_off, int type, char variant);

/* Port of FUN_0046dd34.  New-game terrain-tile randomiser: sweep the bound
 * terrain-index grid (engine_territory_bind_terrain_tiles) and re-roll every
 * cell holding the special sentinel 0xfff0006 to 0xfff0006 + (rng & 0xf),
 * advancing the single-LFSR RNG (threaded through *rng) once per match.
 * No-op if the grid or rng is unbound/NULL. */
void engine_territory_randomise_special_tiles(engine_rng_single_in_t *rng);

/* Port of FUN_0046de94.  Per-neighbour terrain eligibility predicate over
 * the bound terrain-tile grid (engine_territory_bind_terrain_tiles) and cell
 * grid (engine_territory_bind_cells): returns 1 if tile (col, row) is a real
 * cell whose flags byte lacks bit 0x4, or the special sentinel 0xfff0006;
 * 0 if off-grid (col∉[0,65) || row∉[0,129)) or any other value. */
int engine_territory_tile_eligible(int col, int row);

/* Port of FUN_0046de05.  Fill mask[8] with the eligibility (0/1) of the 8
 * hex neighbours of tile (col, row) — the neighbour offsets are chosen by
 * row parity (the strategic map is a doubled-row hex grid).  This is the
 * adjacency mask the (still-unported) feature classifier 0x46c2de consumes;
 * retail passes it through the shared scratch global DAT_00543fe0. */
void engine_territory_compute_neighbour_mask(int col, int row, int32_t mask[8]);

/* Feature/tile the classifier emits for a neighbour mask (retail globals
 * DAT_0057c8a4 / DAT_0053e9c0 / DAT_00568200). */
typedef struct {
    int type;     /* DAT_0057c8a4 — matched entry +8: the tile/feature variant code */
    int aux;      /* DAT_0053e9c0 — matched entry +9 (0 throughout the retail table) */
    int variant;  /* DAT_00568200 — the entry's post-rotation variant counter (+0xb) */
} engine_territory_feature_pick_t;

/* Port of FUN_0046c2de.  Match the 8-neighbour eligibility mask (from
 * engine_territory_compute_neighbour_mask) against retail's 48-entry hex
 * auto-tile pattern table, write the first matching entry's feature into
 * *out, and advance that entry's rotating variant counter.  Returns the
 * matched entry index + 1, or 0 if nothing matched (*out left untouched —
 * unreachable with the shipped table, which covers all 256 masks). */
int engine_territory_pick_feature(const int32_t mask[8],
                                  engine_territory_feature_pick_t *out);

/* Reset every pattern-table entry's rotating variant counter to 0.  Retail
 * never resets these (the table is process-lifetime .data); the harness and
 * tests use it to seat a deterministic starting state. */
void engine_territory_reset_feature_counters(void);

/* Port of FUN_0046dbf5.  New-game terrain auto-tiling pass (sibling of
 * engine_territory_randomise_special_tiles): sweep the bound terrain grid and
 * rewrite every feature-border cell (a real cell with flags bit 0x4 set, or a
 * non-sentinel special tile) with the transition tile its 8-neighbour
 * adjacency selects from the pattern table — real cells into the cell's +3
 * byte, special cells into the tile itself; isolated tiles (type 0x16) take a
 * random variant from the single-LFSR RNG (threaded through *rng).  Mutates
 * the grid in place in row-major order.  No-op if the grid or rng is
 * unbound/NULL. */
void engine_territory_classify_feature_tiles(engine_rng_single_in_t *rng);

/* Port of FUN_0046c080.  Load the group-key (cell byte +7) of the four
 * orthogonal neighbours of the cell at byte-offset cursor_off (= (row*64 +
 * col)*8) of the bound cell grid into out[] in N, E, S, W order; an
 * off-grid edge neighbour reads 0.  Shared with the castle-border subtree
 * (0x468d4f → 0x46902a).  Zeroes out[] if the cell grid is unbound. */
void engine_territory_load_neighbour_keys(int col, int row, int cursor_off,
                                          int32_t out[4]);

/* Port of FUN_00467ca6.  Build the per-army territory-adjacency lists in
 * the bound army-record block (engine_territory_bind_army_records): for
 * every cell whose owning-army key (+7) is in 1..17, record each distinct
 * neighbouring army key into that army's row — a 16-entry list at +0x5c
 * with a count at +0x5a.  No-op if the cell grid or army block is unbound. */
void engine_territory_build_army_adjacency(void);

/* Port of FUN_00467a36.  New-game per-army feature-seeding pass: for every
 * active feature-cell (+7 key 1..17, +1 bit 0x20 set), assign a feature
 * terrain type (0x14 / 1 / 0) by the army's running active-cell index and
 * the global density setting (s_slot_0053f23c), finalising via
 * engine_territory_set_cell_type; also tallies active cells per army at row
 * +0x205.  Needs the cell grid and army-record block bound. */
void engine_territory_seed_army_features(void);

/* ──────────────────────────────────────────────────────────────────
 * Per-army capital-region setup (the 0x468d4f subtree)
 * ────────────────────────────────────────────────────────────────── */

/* The "current cell" col/row scratch registers (retail DAT_0057c8d0 /
 * DAT_0056846c) the capital-region passes leave behind.  Exposed for tests;
 * production code in this module reads the statics directly. */
int engine_territory_cursor_col(void);   /* DAT_0057c8d0 */
int engine_territory_cursor_row(void);   /* DAT_0056846c */

/* Port of FUN_0046ac22.  Stamp a `size`×`size` block of grid cells starting
 * at byte-offset `off`: per cell set +2 |= 1, clear +2 bits 2..4, write the
 * +3 descriptor as base + a per-cell gradient offset (retail's N×N fill-order
 * tables; size 1 writes just base), OR in `flag_bits` to +2, and write `type`
 * to +0.  Shared cell-grid mutator; no-op if the cell grid is unbound. */
void engine_territory_stamp_block(int base, int size, int off,
                                  uint8_t flag_bits, uint8_t type);

/* Port of FUN_00467fd1.  Find army `key`'s capital cell (+7 == key, +1 bit
 * 0x40 set, +0 == 0) by row-major scan; on the 1st such cell stamp a 2×2
 * capital footprint and return its byte-offset (after also touching the 3rd
 * and 4th matches and leaving the cursor col/row at the 4th).  Returns 0 if
 * fewer than 4 eligible cells exist or the grid is unbound. */
int engine_territory_place_capital(unsigned key);

#endif /* OPENLORDS2_ENGINE_TERRITORY_H */
