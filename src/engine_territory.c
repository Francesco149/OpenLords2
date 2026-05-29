/*
 * OpenLords2 — per-territory record helpers.
 *
 * Operates on the per-territory record array (retail `DAT_0052f0b0`,
 * 151 records × 0x1a4 bytes, slot 0 reserved) — the same memory the
 * strategic-map overlay/primary passes bind.  Bound here once via
 * engine_territory_bind_records.  Pure C; no Win32, no save state.
 *
 *   FUN_004651b9 @ 0x4651b9 (91 B) → engine_territory_clear_size_state
 */

#include <stddef.h>

#include "engine_territory.h"

#define TERRITORY_RECORD_STRIDE 0x1a4
#define TERRITORY_RECORD_COUNT  0x97   /* iterate 1..0x96 (1..150) */

/* Byte fields cleared per record.  +0x153 is the territory's max-size
 * byte the overlay city drawer caps against (see engine_overlay_render.h
 * record[+0x153]); +0x14c is the adjacent companion field. */
#define TERRITORY_FIELD_14C 0x14c
#define TERRITORY_FIELD_153 0x153

static uint8_t *g_records;   /* DAT_0052f0b0 */

void engine_territory_bind_records(uint8_t *records)
{
    g_records = records;
}

/* Port of FUN_004651b9.  Zero the +0x14c and +0x153 bytes of every
 * territory record 1..150 (slot 0 skipped).  Part of the new-game /
 * scenario init sequence (state-7 branch of the game-state driver). */
void engine_territory_clear_size_state(void)
{
    if (g_records == NULL) return;

    for (int i = 1; i < TERRITORY_RECORD_COUNT; i++) {
        uint8_t *rec = g_records + i * TERRITORY_RECORD_STRIDE;
        rec[TERRITORY_FIELD_14C] = 0;
        rec[TERRITORY_FIELD_153] = 0;
    }
}
