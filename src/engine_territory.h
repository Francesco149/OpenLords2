/*
 * OpenLords2 — per-territory record helpers.  See engine_territory.c.
 */

#ifndef OPENLORDS2_ENGINE_TERRITORY_H
#define OPENLORDS2_ENGINE_TERRITORY_H

#include <stdint.h>

/* Bind the per-territory record array (retail `DAT_0052f0b0`, 151
 * records × 0x1a4 bytes, slot 0 reserved). */
void engine_territory_bind_records(uint8_t *records);

/* Bind the 64×64×8 map-cell descriptor grid (retail `DAT_00522f90`),
 * mutable here because engine_territory_place_entity marks cells as it
 * claims them (cell +5 ← slot, +2 |= 1).  Same memory engine_map_query
 * reads read-only. */
void engine_territory_bind_cells(uint8_t *cells);

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

#endif /* OPENLORDS2_ENGINE_TERRITORY_H */
