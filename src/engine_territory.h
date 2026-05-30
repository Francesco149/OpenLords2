/*
 * OpenLords2 — per-territory record helpers + overworld cell-grid resets.
 * See engine_territory.c.
 */

#ifndef OPENLORDS2_ENGINE_TERRITORY_H
#define OPENLORDS2_ENGINE_TERRITORY_H

#include <stdint.h>

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

#endif /* OPENLORDS2_ENGINE_TERRITORY_H */
