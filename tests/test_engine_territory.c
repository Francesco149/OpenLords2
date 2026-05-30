/*
 * Unit tests for engine_territory — per-territory record helpers
 * (port of FUN_004651b9).
 */

#include "t.h"
#include "engine_territory.h"

#include <string.h>

enum { STRIDE = 0x1a4, RECORDS = 151 };
static uint8_t g_recs[RECORDS * STRIDE];

static void recs_fill(uint8_t v)
{
    memset(g_recs, v, sizeof g_recs);
    engine_territory_bind_records(g_recs);
}

static uint8_t fld(int rec, int off) { return g_recs[rec * STRIDE + off]; }

int test_territory_clears_size_bytes_1_to_150(void)
{
    recs_fill(0xaa);
    engine_territory_clear_size_state();
    for (int i = 1; i <= 150; i++) {
        T_ASSERT_EQ_I(fld(i, 0x14c), 0);
        T_ASSERT_EQ_I(fld(i, 0x153), 0);
    }
    return 0;
}

int test_territory_skips_record_0(void)
{
    recs_fill(0xaa);
    engine_territory_clear_size_state();
    T_ASSERT_EQ_I(fld(0, 0x14c), 0xaa);
    T_ASSERT_EQ_I(fld(0, 0x153), 0xaa);
    return 0;
}

int test_territory_leaves_other_fields_untouched(void)
{
    recs_fill(0xaa);
    engine_territory_clear_size_state();
    /* Neighbouring bytes within a record must survive. */
    T_ASSERT_EQ_I(fld(7, 0x14b), 0xaa);
    T_ASSERT_EQ_I(fld(7, 0x14d), 0xaa);
    T_ASSERT_EQ_I(fld(7, 0x152), 0xaa);
    T_ASSERT_EQ_I(fld(7, 0x154), 0xaa);
    return 0;
}

int test_territory_does_not_touch_record_151(void)
{
    recs_fill(0xaa);
    engine_territory_clear_size_state();
    /* Loop bound is 1..150; record 150 is the last touched. */
    T_ASSERT_EQ_I(fld(150, 0x14c), 0);
    /* (record 151 doesn't exist in the array; 150 is the highest index) */
    return 0;
}

int test_territory_unbound_is_noop(void)
{
    engine_territory_bind_records(NULL);
    engine_territory_clear_size_state();   /* must not crash */
    return 0;
}

/* ── Entity placer (port of FUN_0046e1b0) ──────────────────────────── */

enum { CELL_GRID = 0x40, CELL_STRIDE = 8 };
static uint8_t g_cells[CELL_GRID * CELL_GRID * CELL_STRIDE];

/* Reset records + cell grid (all free) and bind both. */
static void placer_setup(void)
{
    memset(g_recs, 0, sizeof g_recs);
    memset(g_cells, 0, sizeof g_cells);
    engine_territory_bind_records(g_recs);
    engine_territory_bind_cells(g_cells);
}

static int cell_off(int col, int row) { return (row * CELL_GRID + col) * CELL_STRIDE; }

/* A free grid: placing into slot 1 succeeds, stamps the full field map,
 * claims the cell, and parks the current-slot cursor at 1. */
int test_territory_place_fills_record_and_claims_cell(void)
{
    placer_setup();
    int col = 20, row = 30;

    T_ASSERT_EQ_I(engine_territory_place_entity(3, col, row, 6), 1);
    T_ASSERT_EQ_I(engine_territory_current_slot(), 1);

    /* record field map */
    T_ASSERT_EQ_I(fld(1, 0x00), 6);          /* type   */
    T_ASSERT_EQ_I(fld(1, 0x08), 3);          /* kind   */
    T_ASSERT_EQ_I(fld(1, 0x0a), col);
    T_ASSERT_EQ_I(fld(1, 0x14), col);
    T_ASSERT_EQ_I(fld(1, 0x16), col);
    T_ASSERT_EQ_I(fld(1, 0x0b), row);
    T_ASSERT_EQ_I(fld(1, 0x15), row);
    T_ASSERT_EQ_I(fld(1, 0x17), row);
    T_ASSERT_EQ_I(fld(1, 0x12), (uint8_t)(col << 4));
    T_ASSERT_EQ_I(fld(1, 0x13), (uint8_t)(row << 4));
    T_ASSERT_EQ_I(fld(1, 0x09), 1);
    T_ASSERT_EQ_I(fld(1, 0x14b) & 1, 1);
    /* cell byte offset stored as i32 at +0x0c */
    T_ASSERT_EQ_I(*(int32_t *)&g_recs[1 * STRIDE + 0x0c], cell_off(col, row));

    /* cell grid marked: +5 ← slot, +2 |= 1 */
    T_ASSERT_EQ_I(g_cells[cell_off(col, row) + 5], 1);
    T_ASSERT_EQ_I(g_cells[cell_off(col, row) + 2] & 1, 1);
    return 0;
}

/* The placer takes the first record whose type byte (+0) is zero,
 * skipping in-use slots. */
int test_territory_place_takes_first_free_slot(void)
{
    placer_setup();
    g_recs[1 * STRIDE + 0] = 9;   /* slot 1 in use */
    g_recs[2 * STRIDE + 0] = 9;   /* slot 2 in use */

    T_ASSERT_EQ_I(engine_territory_place_entity(3, 5, 5, 6), 1);
    T_ASSERT_EQ_I(engine_territory_current_slot(), 3);
    T_ASSERT_EQ_I(fld(3, 0x00), 6);
    return 0;
}

/* An already-claimed cell (head-link +5 != 0) is refused. */
int test_territory_place_rejects_occupied_cell(void)
{
    placer_setup();
    g_cells[cell_off(8, 8) + 5] = 0x42;   /* cell taken */
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 8, 8, 6), 0);
    /* no record allocated */
    T_ASSERT_EQ_I(fld(1, 0x00), 0);
    return 0;
}

/* A cell whose raw-flag has any of bits 2..7 set is "blocked". */
int test_territory_place_rejects_blocked_rawflag(void)
{
    placer_setup();
    g_cells[cell_off(8, 8) + 1] = 0x04;   /* bit 2 set → blocked */
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 8, 8, 6), 0);
    /* low 2 bits are fine, though */
    g_cells[cell_off(9, 9) + 1] = 0x03;
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 9, 9, 6), 1);
    return 0;
}

/* Off-grid coordinates are refused without touching anything. */
int test_territory_place_rejects_off_grid(void)
{
    placer_setup();
    T_ASSERT_EQ_I(engine_territory_place_entity(3, -1, 0, 6), 0);
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 0x40, 0, 6), 0);
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 0, -1, 6), 0);
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 0, 0x40, 6), 0);
    return 0;
}

/* With all 149 allocatable slots in use the placer reports failure. */
int test_territory_place_table_full_returns_0(void)
{
    placer_setup();
    for (int i = 1; i < 0x96; i++)
        g_recs[i * STRIDE + 0] = 9;        /* every slot 1..149 in use */
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 4, 4, 6), 0);
    return 0;
}

/* place_entity is a no-op (0) when either binding is missing. */
int test_territory_place_unbound_returns_0(void)
{
    engine_territory_bind_records(NULL);
    engine_territory_bind_cells(g_cells);
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 1, 1, 6), 0);

    engine_territory_bind_records(g_recs);
    engine_territory_bind_cells(NULL);
    T_ASSERT_EQ_I(engine_territory_place_entity(3, 1, 1, 6), 0);
    return 0;
}
