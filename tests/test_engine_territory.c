/*
 * Unit tests for engine_territory — per-territory record helpers
 * (port of FUN_004651b9).
 */

#include "t.h"
#include "engine_territory.h"
#include "save_schema_globals.h"   /* s_slot_0053f23c — feature-density setting */

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

/* ── Overworld cell-grid resets ────────────────────────────────────── */
/* (reuse g_cells from the placer block; CELL_GRID×CELL_GRID cells) */

#include "engine_dirty_rect.h"

enum { N_CELLS = CELL_GRID * CELL_GRID };

static void cells_fill(uint8_t v)
{
    memset(g_cells, v, sizeof g_cells);
    engine_territory_bind_cells(g_cells);
}

/* clear_cell_field zeroes byte `field` of every cell, nothing else. */
int test_territory_clear_cell_field_zeroes_one_byte_all_cells(void)
{
    cells_fill(0xaa);
    engine_territory_clear_cell_field(3);
    for (int i = 0; i < N_CELLS; i++) {
        T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + 3], 0);
        /* every other byte in the cell survives */
        for (int b = 0; b < CELL_STRIDE; b++)
            if (b != 3)
                T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + b], 0xaa);
    }
    return 0;
}

/* Sweeping fields 0..5 (the call-site set) clears exactly those bytes
 * and leaves +6 / +7 untouched. */
int test_territory_clear_cell_field_sweep_0_to_5(void)
{
    cells_fill(0xaa);
    for (uint8_t f = 0; f <= 5; f++)
        engine_territory_clear_cell_field(f);
    for (int i = 0; i < N_CELLS; i++) {
        for (int b = 0; b <= 5; b++)
            T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + b], 0);
        T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + 6], 0xaa);
        T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + 7], 0xaa);
    }
    return 0;
}

/* Touches the last cell too (no off-by-one short of 4096). */
int test_territory_clear_cell_field_reaches_last_cell(void)
{
    cells_fill(0xff);
    engine_territory_clear_cell_field(0);
    T_ASSERT_EQ_I(g_cells[(N_CELLS - 1) * CELL_STRIDE + 0], 0);
    return 0;
}

int test_territory_clear_cell_field_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_clear_cell_field(0);   /* must not crash */
    return 0;
}

/* clear_cell_render_flags clears only bit 0x20 of byte +2; other bits of
 * +2 and all other bytes are preserved. */
int test_territory_clear_cell_render_flags_clears_bit20_byte2(void)
{
    cells_fill(0xff);
    engine_territory_set_dirty_state(NULL);
    engine_territory_clear_cell_render_flags();
    for (int i = 0; i < N_CELLS; i++) {
        T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + 2], 0xdf);   /* 0xff & ~0x20 */
        for (int b = 0; b < CELL_STRIDE; b++)
            if (b != 2)
                T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + b], 0xff);
    }
    return 0;
}

/* When a dirty state is bound, a full-screen repaint is requested. */
int test_territory_clear_cell_render_flags_marks_full_screen(void)
{
    cells_fill(0x00);
    engine_dirty_rect_state_t s = {
        .dirty_mark = 0, .dirty_xmin = 0x7fff, .dirty_xmax = -0x7fff,
        .dirty_ymin = 0x7fff, .dirty_ymax = -0x7fff, .below_status_redraw = 0,
    };
    engine_territory_set_dirty_state(&s);
    engine_territory_clear_cell_render_flags();
    /* full screen = (0,0)..(639,479), mark = 1 */
    T_ASSERT_EQ_I(s.dirty_mark, 1);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x27f);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    engine_territory_set_dirty_state(NULL);   /* don't leak into later tests */
    return 0;
}

/* Unbound dirty state + unbound cells: still safe. */
int test_territory_clear_cell_render_flags_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_set_dirty_state(NULL);
    engine_territory_clear_cell_render_flags();   /* must not crash */
    return 0;
}

/* ── Alternate-draw box stamp (port of FUN_0046e067) ───────────────── */

/* 1 iff cell (col,row) has bit 0x20 set on byte +2. */
static int box_marked(int col, int row)
{
    return (g_cells[cell_off(col, row) + 2] & 0x20) != 0;
}

/* A radius-1 box at an interior cell marks exactly the 3×3 neighbourhood
 * and nothing else; only bit 0x20 of byte +2 is touched. */
int test_territory_mark_draw_box_interior_3x3(void)
{
    cells_fill(0x00);
    engine_territory_mark_draw_box(20, 30, 1);
    for (int r = 0; r < CELL_GRID; r++)
        for (int c = 0; c < CELL_GRID; c++) {
            int want = (c >= 19 && c <= 21 && r >= 29 && r <= 31);
            T_ASSERT_EQ_I(box_marked(c, r), want);
        }
    return 0;
}

/* Only bit 0x20 is OR'd in; the rest of byte +2 and every other byte of
 * each touched cell survives. */
int test_territory_mark_draw_box_preserves_other_bits(void)
{
    cells_fill(0xdf);          /* every bit but 0x20 already set */
    engine_territory_mark_draw_box(10, 10, 0);   /* radius 0 → single cell */
    int off = cell_off(10, 10);
    T_ASSERT_EQ_I(g_cells[off + 2], 0xff);       /* 0xdf | 0x20 */
    for (int b = 0; b < CELL_STRIDE; b++)
        if (b != 2)
            T_ASSERT_EQ_I(g_cells[off + b], 0xdf);
    return 0;
}

/* At a corner the box is clamped, not rejected: a radius-2 box at (0,0)
 * marks only the in-grid quadrant (0..2 × 0..2). */
int test_territory_mark_draw_box_clamps_at_corner(void)
{
    cells_fill(0x00);
    engine_territory_mark_draw_box(0, 0, 2);
    for (int r = 0; r < CELL_GRID; r++)
        for (int c = 0; c < CELL_GRID; c++) {
            int want = (c <= 2 && r <= 2);
            T_ASSERT_EQ_I(box_marked(c, r), want);
        }
    return 0;
}

/* At the far edge the box shrinks against the high bound and never writes
 * past the last column/row (cell 4095 reachable, no overrun). */
int test_territory_mark_draw_box_clamps_at_far_edge(void)
{
    cells_fill(0x00);
    engine_territory_mark_draw_box(63, 63, 1);
    for (int r = 0; r < CELL_GRID; r++)
        for (int c = 0; c < CELL_GRID; c++) {
            int want = (c >= 62 && r >= 62);
            T_ASSERT_EQ_I(box_marked(c, r), want);
        }
    T_ASSERT_EQ_I(box_marked(63, 63), 1);   /* last cell stamped */
    return 0;
}

int test_territory_mark_draw_box_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_mark_draw_box(10, 10, 3);   /* must not crash */
    return 0;
}

/* ── Highlight-key sweep (port of FUN_0046dfd5) ────────────────────── */

/* Two cells carry key 5; each gets a 3×3 box. A cell with a different
 * key is ignored. Far-apart cells produce two disjoint 3×3 blocks. */
int test_territory_highlight_key_stamps_3x3_per_match(void)
{
    cells_fill(0x00);
    g_cells[cell_off(10, 10) + 7] = 5;
    g_cells[cell_off(40, 40) + 7] = 5;
    g_cells[cell_off(20, 20) + 7] = 6;   /* other key — must be skipped */
    engine_territory_set_dirty_state(NULL);

    engine_territory_highlight_key_cells(5);

    for (int r = 0; r < CELL_GRID; r++)
        for (int c = 0; c < CELL_GRID; c++) {
            int near10 = (c >= 9 && c <= 11 && r >= 9 && r <= 11);
            int near40 = (c >= 39 && c <= 41 && r >= 39 && r <= 41);
            T_ASSERT_EQ_I(box_marked(c, r), near10 || near40);
        }
    return 0;
}

/* No cell matches → no flag set anywhere. */
int test_territory_highlight_key_no_match_is_clean(void)
{
    cells_fill(0x00);
    g_cells[cell_off(10, 10) + 7] = 5;
    engine_territory_set_dirty_state(NULL);

    engine_territory_highlight_key_cells(7);   /* key 7 absent */

    for (int i = 0; i < N_CELLS; i++)
        T_ASSERT_EQ_I(g_cells[i * CELL_STRIDE + 2] & 0x20, 0);
    return 0;
}

/* A match at a corner clamps its box (no overrun) and still works. */
int test_territory_highlight_key_clamps_at_edge(void)
{
    cells_fill(0x00);
    g_cells[cell_off(0, 0) + 7] = 3;
    engine_territory_set_dirty_state(NULL);

    engine_territory_highlight_key_cells(3);

    for (int r = 0; r < CELL_GRID; r++)
        for (int c = 0; c < CELL_GRID; c++)
            T_ASSERT_EQ_I(box_marked(c, r), (c <= 1 && r <= 1));
    return 0;
}

/* Key 0 matches the default-zero +7 byte of every cell, so the whole
 * grid ends up flagged — a faithful (if degenerate) consequence. */
int test_territory_highlight_key_zero_matches_all(void)
{
    cells_fill(0x00);
    engine_territory_set_dirty_state(NULL);

    engine_territory_highlight_key_cells(0);

    for (int i = 0; i < N_CELLS; i++)
        T_ASSERT_EQ_I((g_cells[i * CELL_STRIDE + 2] & 0x20) != 0, 1);
    return 0;
}

/* With a dirty state bound the sweep ends in a full-screen repaint. */
int test_territory_highlight_key_marks_full_screen(void)
{
    cells_fill(0x00);
    engine_dirty_rect_state_t s = {
        .dirty_mark = 0, .dirty_xmin = 0x7fff, .dirty_xmax = -0x7fff,
        .dirty_ymin = 0x7fff, .dirty_ymax = -0x7fff, .below_status_redraw = 0,
    };
    engine_territory_set_dirty_state(&s);
    engine_territory_highlight_key_cells(9);   /* no match needed */
    T_ASSERT_EQ_I(s.dirty_mark, 1);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x27f);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    engine_territory_set_dirty_state(NULL);
    return 0;
}

int test_territory_highlight_key_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_set_dirty_state(NULL);
    engine_territory_highlight_key_cells(5);   /* must not crash */
    return 0;
}

/* ── Per-key cell roster rebuild (port of FUN_0046da4b) ─────────────── */

enum { ROSTER_ROWS = 17, ROSTER_NCOL = 20, SCORE_STRIDE = 0x180 };
static int32_t g_roster[ROSTER_ROWS * ROSTER_NCOL];   /* 340 i32 = g_misc_1360 */
static int16_t g_score[ROSTER_ROWS * SCORE_STRIDE];   /* 17 × 384 i16 = DAT_0053fa40 */

/* Mark cell (col,row) as an active feature of group-key `key`: +7 = key,
 * +1 bit 0x20 set. */
static void cell_feature(int col, int row, uint8_t key)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 1] |= 0x20;
}

static void roster_setup(void)
{
    memset(g_cells, 0, sizeof g_cells);
    memset(g_roster, 0, sizeof g_roster);
    memset(g_score, 0, sizeof g_score);
    engine_territory_bind_cells(g_cells);
    engine_territory_bind_cell_roster(g_roster);
    engine_territory_bind_cell_score_table(g_score);
}

static int32_t ros(int key, int col) { return g_roster[key * ROSTER_NCOL + col]; }

/* Active cells are appended to their key's row in cell byte-offset order. */
int test_territory_roster_indexes_active_cells_by_key(void)
{
    roster_setup();
    cell_feature(2, 0, 5);    /* off 16   */
    cell_feature(10, 0, 9);   /* off 80   */
    cell_feature(1, 1, 5);    /* off 520  */
    cell_feature(0, 2, 5);    /* off 1024 */
    cell_feature(5, 3, 9);    /* off 1576 */

    engine_territory_rebuild_cell_roster(0);

    T_ASSERT_EQ_I(ros(5, 0), cell_off(2, 0));
    T_ASSERT_EQ_I(ros(5, 1), cell_off(1, 1));
    T_ASSERT_EQ_I(ros(5, 2), cell_off(0, 2));
    T_ASSERT_EQ_I(ros(5, 3), 0);              /* row not over-filled */
    T_ASSERT_EQ_I(ros(9, 0), cell_off(10, 0));
    T_ASSERT_EQ_I(ros(9, 1), cell_off(5, 3));
    return 0;
}

/* key 0, key >= 0x12, and cells without the +1 0x20 flag are all skipped. */
int test_territory_roster_filters_inactive_cells(void)
{
    roster_setup();
    int o;
    o = cell_off(3, 0); g_cells[o + 7] = 0;  g_cells[o + 1] |= 0x20;  /* key 0   */
    o = cell_off(4, 0); g_cells[o + 7] = 18; g_cells[o + 1] |= 0x20;  /* key 18  */
    o = cell_off(5, 0); g_cells[o + 7] = 7;                           /* no flag */
    cell_feature(6, 0, 7);                                            /* counted */

    engine_territory_rebuild_cell_roster(0);

    T_ASSERT_EQ_I(ros(7, 0), cell_off(6, 0));
    T_ASSERT_EQ_I(ros(7, 1), 0);
    return 0;
}

/* Each key holds at most 20 cells; the 21st finds no free column and is
 * evicted: +0 → 0, +3 → 6, +1 → 0, +2 &= 0xe3. */
int test_territory_roster_caps_at_20_and_evicts_overflow(void)
{
    roster_setup();
    for (int c = 1; c <= 21; c++)
        cell_feature(c, 0, 4);
    int e = cell_off(21, 0);
    g_cells[e + 0] = 0x99;       /* type, must be cleared */
    g_cells[e + 2] = 0xff;       /* must become 0xe3      */

    engine_territory_rebuild_cell_roster(0);

    for (int i = 0; i < 20; i++)
        T_ASSERT_EQ_I(ros(4, i), cell_off(i + 1, 0));

    T_ASSERT_EQ_I(g_cells[e + 0], 0);
    T_ASSERT_EQ_I(g_cells[e + 3], 6);
    T_ASSERT_EQ_I(g_cells[e + 1], 0);
    T_ASSERT_EQ_I(g_cells[e + 2], 0xe3);
    return 0;
}

/* Step 1 clears roster rows 1..16 only; row 0 is left intact (and the
 * 340-i32 buffer has no row 17 — the <0x12 guard's would-be OOB row). */
int test_territory_roster_clears_rows_1_to_16_only(void)
{
    roster_setup();
    for (int i = 0; i < ROSTER_ROWS * ROSTER_NCOL; i++)
        g_roster[i] = 0x1234;

    engine_territory_rebuild_cell_roster(0);   /* no active cells → clear only */

    for (int c = 0; c < ROSTER_NCOL; c++)
        T_ASSERT_EQ_I(g_roster[0 * ROSTER_NCOL + c], 0x1234);   /* row 0 kept */
    for (int r = 1; r <= 16; r++)
        for (int c = 0; c < ROSTER_NCOL; c++)
            T_ASSERT_EQ_I(g_roster[r * ROSTER_NCOL + c], 0);
    return 0;
}

/* The score-table clear runs only for reset_score_table == 1, and only on
 * columns 0..19 of rows 1..16 (row 0 and column 20+ survive). */
int test_territory_roster_score_clear_gated_on_reset_flag(void)
{
    roster_setup();
    for (int i = 0; i < ROSTER_ROWS * SCORE_STRIDE; i++)
        g_score[i] = 0x55;

    engine_territory_rebuild_cell_roster(0);                  /* untouched */
    T_ASSERT_EQ_I(g_score[1 * SCORE_STRIDE + 0], 0x55);

    engine_territory_rebuild_cell_roster(1);
    T_ASSERT_EQ_I(g_score[0 * SCORE_STRIDE + 0], 0x55);       /* row 0 kept   */
    for (int r = 1; r <= 16; r++)
        for (int c = 0; c < 20; c++)
            T_ASSERT_EQ_I(g_score[r * SCORE_STRIDE + c], 0);
    T_ASSERT_EQ_I(g_score[1 * SCORE_STRIDE + 20], 0x55);      /* col 20 kept  */
    return 0;
}

/* No roster bound → whole rebuild is a no-op.  Score unbound + reset=1 →
 * the roster clear still runs (the secondary score clear is just skipped). */
int test_territory_roster_unbound_is_noop(void)
{
    roster_setup();
    engine_territory_bind_cell_roster(NULL);
    engine_territory_rebuild_cell_roster(1);                  /* must not crash */

    engine_territory_bind_cell_roster(g_roster);
    engine_territory_bind_cell_score_table(NULL);
    for (int i = 0; i < ROSTER_ROWS * ROSTER_NCOL; i++) g_roster[i] = 7;
    engine_territory_rebuild_cell_roster(1);                  /* score skip, no crash */
    T_ASSERT_EQ_I(g_roster[1 * ROSTER_NCOL], 0);              /* roster cleared */
    T_ASSERT_EQ_I(g_roster[0], 7);                            /* row 0 kept     */
    return 0;
}

/* ── Single-cell score clear (engine_territory_clear_cell_score,
 *    port of FUN_0046d9da) ─────────────────────────────────────────── */

/* The cell's slot in its key's roster row drives which score column is
 * zeroed; only that column is touched. */
int test_territory_clear_cell_score_zeroes_matching_slot(void)
{
    roster_setup();
    int off = cell_off(7, 3);
    g_cells[off + 7] = 5;                       /* key 5 */
    g_roster[5 * ROSTER_NCOL + 2] = off;        /* cell lives in column 2 */
    for (int c = 0; c < 20; c++)
        g_score[5 * SCORE_STRIDE + c] = 0x77;

    engine_territory_clear_cell_score(off);

    T_ASSERT_EQ_I(g_score[5 * SCORE_STRIDE + 2], 0);     /* matched col cleared */
    T_ASSERT_EQ_I(g_score[5 * SCORE_STRIDE + 1], 0x77);  /* neighbours kept     */
    T_ASSERT_EQ_I(g_score[5 * SCORE_STRIDE + 3], 0x77);
    return 0;
}

/* A cell absent from its key's roster row clears nothing (loop falls
 * through all 20 columns without a hit). */
int test_territory_clear_cell_score_absent_cell_is_noop(void)
{
    roster_setup();
    int off = cell_off(9, 1);
    g_cells[off + 7] = 4;                        /* key 4, but not in the row */
    for (int c = 0; c < 20; c++) {
        g_roster[4 * ROSTER_NCOL + c] = off + 8; /* some other offset */
        g_score[4 * SCORE_STRIDE + c] = 0x33;
    }

    engine_territory_clear_cell_score(off);

    for (int c = 0; c < 20; c++)
        T_ASSERT_EQ_I(g_score[4 * SCORE_STRIDE + c], 0x33);
    return 0;
}

/* The key is taken from the cell's own +7 byte, so the score row used is
 * the cell's key row — a slot with the same offset under a different key
 * is left alone. */
int test_territory_clear_cell_score_uses_cell_key_row(void)
{
    roster_setup();
    int off = cell_off(2, 2);
    g_cells[off + 7] = 9;                        /* key 9 */
    g_roster[9 * ROSTER_NCOL + 0] = off;         /* present in key 9 col 0 */
    g_roster[3 * ROSTER_NCOL + 0] = off;         /* decoy: same offset, key 3 */
    g_score[9 * SCORE_STRIDE + 0] = 0x11;
    g_score[3 * SCORE_STRIDE + 0] = 0x11;

    engine_territory_clear_cell_score(off);

    T_ASSERT_EQ_I(g_score[9 * SCORE_STRIDE + 0], 0);     /* cell's own key row */
    T_ASSERT_EQ_I(g_score[3 * SCORE_STRIDE + 0], 0x11);  /* decoy row untouched */
    return 0;
}

/* Unbound roster or score table → no-op, no crash. */
int test_territory_clear_cell_score_unbound_is_noop(void)
{
    roster_setup();
    int off = cell_off(1, 0);
    g_cells[off + 7] = 5;
    g_roster[5 * ROSTER_NCOL + 0] = off;
    g_score[5 * SCORE_STRIDE + 0] = 0x44;

    engine_territory_bind_cell_score_table(NULL);
    engine_territory_clear_cell_score(off);              /* must not crash */
    engine_territory_bind_cell_score_table(g_score);
    T_ASSERT_EQ_I(g_score[5 * SCORE_STRIDE + 0], 0x44);  /* still untouched */

    engine_territory_bind_cell_roster(NULL);
    engine_territory_clear_cell_score(off);              /* must not crash */
    T_ASSERT_EQ_I(g_score[5 * SCORE_STRIDE + 0], 0x44);
    return 0;
}

/* ── Cell terrain-type setter (engine_territory_set_cell_type,
 *    port of FUN_0046d7f4) ─────────────────────────────────────────── */

/* An ordinary type (< 0x13, not a named special): +0 = type, +3 = base
 * 0x58 (low 2 bits rebuilt to 0), +2 = bit0|bit3, no bit7.  Type != 0 so
 * the AI score is NOT cleared. */
int test_territory_set_cell_type_ordinary(void)
{
    roster_setup();
    int off = cell_off(4, 4);
    g_cells[off + 7] = 6;
    g_roster[6 * ROSTER_NCOL + 0] = off;
    g_score[6 * SCORE_STRIDE + 0] = 0x77;

    engine_territory_set_cell_type(off, 5, 0);

    T_ASSERT_EQ_I(g_cells[off + 0], 5);
    T_ASSERT_EQ_I(g_cells[off + 3], 0x58);
    T_ASSERT_EQ_I(g_cells[off + 2], 0x09);          /* 1 | 8 */
    T_ASSERT_EQ_I(g_score[6 * SCORE_STRIDE + 0], 0x77);  /* not cleared */
    return 0;
}

/* Type 0 empties the cell: base 0x50, and the AI score for the cell's
 * roster slot is cleared (the FUN_0046d9da call). */
int test_territory_set_cell_type_zero_clears_score(void)
{
    roster_setup();
    int off = cell_off(2, 1);
    g_cells[off + 0] = 5;            /* old type (not special) */
    g_cells[off + 3] = 0x03;         /* low bits 3 to preserve */
    g_cells[off + 2] = 0xff;
    g_cells[off + 7] = 4;
    g_roster[4 * ROSTER_NCOL + 0] = off;
    g_score[4 * SCORE_STRIDE + 0] = 0x77;

    engine_territory_set_cell_type(off, 0, 0);

    T_ASSERT_EQ_I(g_cells[off + 0], 0);
    /* lo = (0x03 - 0x50) & 3 = 3; +3 = 3 + 0x50 = 0x53 */
    T_ASSERT_EQ_I(g_cells[off + 3], 0x53);
    /* +2: 0xff|1=0xff &0xe3=0xe3 |8=0xeb &0x7f=0x6b */
    T_ASSERT_EQ_I(g_cells[off + 2], 0x6b);
    T_ASSERT_EQ_I(g_score[4 * SCORE_STRIDE + 0], 0);     /* cleared */
    return 0;
}

/* Type band 15..22 (0xe < type < 0x17) sets the +2 bit7 flag. */
int test_territory_set_cell_type_band_sets_bit7(void)
{
    roster_setup();
    int off = cell_off(6, 6);

    engine_territory_set_cell_type(off, 16, 0);     /* 16 < 0x13 → base 0x58 */

    T_ASSERT_EQ_I(g_cells[off + 0], 16);
    T_ASSERT_EQ_I(g_cells[off + 3], 0x58);
    T_ASSERT_EQ_I(g_cells[off + 2], 0x89);          /* 1 | 8 | 0x80 */
    return 0;
}

/* The 0x17/0x18 specials use bases 0x82/0x86 and contribute NO +2 low bits
 * (bits 2..4 stay clear); 0x17 is not in the bit7 band. */
int test_territory_set_cell_type_special_no_lowbits(void)
{
    roster_setup();
    int off = cell_off(3, 3);
    g_cells[off + 2] = 0xff;

    engine_territory_set_cell_type(off, 0x17, 0);

    T_ASSERT_EQ_I(g_cells[off + 0], 0x17);
    T_ASSERT_EQ_I(g_cells[off + 3], 0x82);
    /* +2: 0xff|1=0xff &0xe3=0xe3 |0=0xe3 &0x7f=0x63, no bit7 (0x17 !< 0x17) */
    T_ASSERT_EQ_I(g_cells[off + 2], 0x63);
    return 0;
}

/* An old type of 0x17/0x18 shifts the +3 low-bit correction constant to
 * 0x82 (≡ 2 mod 4); the variant tier adds variant*4 to the descriptor. */
int test_territory_set_cell_type_old_special_correction_and_variant(void)
{
    roster_setup();
    int off = cell_off(5, 5);
    g_cells[off + 0] = 0x18;         /* old special → corr = 0x82 */
    g_cells[off + 3] = 0x07;

    engine_territory_set_cell_type(off, 1, 2);       /* base 0x54, variant 2 */

    T_ASSERT_EQ_I(g_cells[off + 0], 1);
    /* lo = (0x07 - 0x82) & 3 = 0x85 & 3 = 1; +3 = 1 + 0x54 + 2*4 = 0x5d */
    T_ASSERT_EQ_I(g_cells[off + 3], 0x5d);
    return 0;
}

/* Unbound cell grid → no-op, no crash. */
int test_territory_set_cell_type_unbound_is_noop(void)
{
    roster_setup();
    engine_territory_bind_cells(NULL);
    engine_territory_set_cell_type(0, 5, 0);         /* must not crash */
    return 0;
}

/* ── Terrain-tile randomiser (engine_territory_randomise_special_tiles,
 *    port of FUN_0046dd34) ─────────────────────────────────────────── */
enum { TILE_ROWS = 0x81, TILE_COLS = 0x41, TILE_CELLS = TILE_ROWS * TILE_COLS };
enum { TILE_SENTINEL = 0xfff0006 };
static int32_t g_tiles[TILE_CELLS];

static void tiles_setup(uint32_t fill)
{
    for (int i = 0; i < TILE_CELLS; i++) g_tiles[i] = (int32_t)fill;
    engine_territory_bind_terrain_tiles(g_tiles);
}
static int32_t *tile_at(int row, int col) { return &g_tiles[row * TILE_COLS + col]; }

/* Independent reference: thread the single-LFSR state one advance and
 * return the 7-bit mirror — mirrors the production rng_next_single_7bit. */
static uint32_t ref_single_mirror(uint32_t *state)
{
    engine_rng_single_in_t  in  = { .state = *state };
    engine_rng_single_out_t out = {0};
    engine_rng_step_single(&in, &out);
    *state = out.new_state;
    return out.rng_7bit;
}

/* Sentinel cells are re-rolled in row-major scan order, each consuming one
 * RNG draw; the final threaded state matches that many advances. */
int test_territory_tiles_rerolls_sentinels_in_order(void)
{
    tiles_setup(0x12345);                       /* non-sentinel background */
    *tile_at(0, 5)  = TILE_SENTINEL;            /* visited 1st */
    *tile_at(2, 0)  = TILE_SENTINEL;            /* visited 2nd */
    *tile_at(2, 10) = TILE_SENTINEL;            /* visited 3rd */

    const uint32_t seed = 0x34518632u;
    engine_rng_single_in_t rng = { .state = seed };
    engine_territory_randomise_special_tiles(&rng);

    uint32_t st = seed;
    int32_t e0 = (int32_t)((ref_single_mirror(&st) & 0xfu) + TILE_SENTINEL);
    int32_t e1 = (int32_t)((ref_single_mirror(&st) & 0xfu) + TILE_SENTINEL);
    int32_t e2 = (int32_t)((ref_single_mirror(&st) & 0xfu) + TILE_SENTINEL);

    T_ASSERT_EQ_U((uint32_t)*tile_at(0, 5),  (uint32_t)e0);
    T_ASSERT_EQ_U((uint32_t)*tile_at(2, 0),  (uint32_t)e1);
    T_ASSERT_EQ_U((uint32_t)*tile_at(2, 10), (uint32_t)e2);
    T_ASSERT_EQ_U(rng.state, st);               /* threaded 3 advances */
    return 0;
}

/* Concrete pinned value: retail seed 0x34518632, first match → mirror 0x51,
 * & 0xf = 1 → 0xfff0006 + 1 = 0xfff0007. */
int test_territory_tiles_pinned_retail_seed(void)
{
    tiles_setup(0);                             /* 0 is non-sentinel */
    *tile_at(0, 0) = TILE_SENTINEL;             /* first cell visited */
    engine_rng_single_in_t rng = { .state = 0x34518632u };
    engine_territory_randomise_special_tiles(&rng);
    T_ASSERT_EQ_U((uint32_t)*tile_at(0, 0), 0xfff0007u);
    return 0;
}

/* Only the exact sentinel 0xfff0006 matches: the EMPTY base, the neighbours
 * 0xfff0005/0xfff0007, and a normal descriptor index are all left alone, and
 * with no match the RNG must not advance. */
int test_territory_tiles_leaves_non_sentinels(void)
{
    tiles_setup(0x111);
    *tile_at(1, 1) = (int32_t)0xfff0000u;       /* EMPTY base */
    *tile_at(1, 2) = (int32_t)0xfff0005u;       /* sentinel - 1 */
    *tile_at(1, 3) = (int32_t)0xfff0007u;       /* sentinel + 1 */
    *tile_at(1, 4) = (int32_t)0x0000abcdu;      /* normal index */
    engine_rng_single_in_t rng = { .state = 0x999u };
    engine_territory_randomise_special_tiles(&rng);
    T_ASSERT_EQ_U((uint32_t)*tile_at(1, 1), 0xfff0000u);
    T_ASSERT_EQ_U((uint32_t)*tile_at(1, 2), 0xfff0005u);
    T_ASSERT_EQ_U((uint32_t)*tile_at(1, 3), 0xfff0007u);
    T_ASSERT_EQ_U((uint32_t)*tile_at(1, 4), 0x0000abcdu);
    T_ASSERT_EQ_U(rng.state, 0x999u);           /* no draw consumed */
    return 0;
}

/* A whole grid of sentinels: every cell is re-rolled into [0xfff0006,
 * 0xfff0015] and the RNG advances exactly once per cell (8385 total). */
int test_territory_tiles_value_range_and_full_grid(void)
{
    tiles_setup((uint32_t)TILE_SENTINEL);
    engine_rng_single_in_t rng = { .state = 0x1u };
    engine_territory_randomise_special_tiles(&rng);
    for (int i = 0; i < TILE_CELLS; i++) {
        uint32_t v = (uint32_t)g_tiles[i];
        T_ASSERT(v >= 0xfff0006u && v <= 0xfff0015u);
    }
    uint32_t st = 0x1u;
    for (int i = 0; i < TILE_CELLS; i++) (void)ref_single_mirror(&st);
    T_ASSERT_EQ_U(rng.state, st);
    return 0;
}

/* The last cell (row 128, col 64 → i32 index 8384) is reached and processed,
 * confirming the 65-i32 row stride and inclusive bounds. */
int test_territory_tiles_reaches_last_cell(void)
{
    tiles_setup(0x55);
    *tile_at(TILE_ROWS - 1, TILE_COLS - 1) = TILE_SENTINEL;
    engine_rng_single_in_t rng = { .state = 0x34518632u };
    engine_territory_randomise_special_tiles(&rng);
    T_ASSERT_EQ_U((uint32_t)*tile_at(TILE_ROWS - 1, TILE_COLS - 1), 0xfff0007u);
    T_ASSERT_EQ_U((uint32_t)*tile_at(0, 0), 0x55u);   /* others untouched */
    return 0;
}

/* Unbound grid → no-op (no crash, no draw); rng NULL → no-op even with a
 * grid full of sentinels. */
int test_territory_tiles_unbound_is_noop(void)
{
    engine_territory_bind_terrain_tiles(NULL);
    engine_rng_single_in_t rng = { .state = 0x7u };
    engine_territory_randomise_special_tiles(&rng);
    T_ASSERT_EQ_U(rng.state, 0x7u);

    tiles_setup((uint32_t)TILE_SENTINEL);
    engine_territory_randomise_special_tiles(NULL);
    T_ASSERT_EQ_U((uint32_t)g_tiles[0], (uint32_t)TILE_SENTINEL);
    return 0;
}

/* ── Neighbour eligibility predicate + mask (engine_territory_tile_eligible
 *    / _compute_neighbour_mask, ports of FUN_0046de94 / FUN_0046de05) ──── */

enum { TILE_SPECIAL_BASE = 0xfff0000 };

/* Bind both grids: the 129×65 terrain tiles filled with `tile_fill` and a
 * zeroed 64×64×8 cell grid (the predicate reads cell flags for real-cell
 * tile values). */
static void elig_setup(uint32_t tile_fill)
{
    for (int i = 0; i < TILE_CELLS; i++) g_tiles[i] = (int32_t)tile_fill;
    memset(g_cells, 0, sizeof g_cells);
    engine_territory_bind_terrain_tiles(g_tiles);
    engine_territory_bind_cells(g_cells);
}

/* A real-cell tile (value < 0xfff0000 = the cell's byte-offset) is eligible
 * iff its flags byte (+1) lacks bit 0x4. */
int test_territory_elig_real_cell_flag_gate(void)
{
    elig_setup(TILE_SPECIAL_BASE);                 /* background: special, not sentinel → 0 */
    int off = cell_off(5, 0);                       /* a real cell's byte-offset = 5*8 = 40 */
    *tile_at(20, 10) = off;                         /* tile (col=10,row=20) points at it    */

    g_cells[off + 1] = 0x00;                        /* no bit 0x4 → eligible */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(10, 20), 1);
    g_cells[off + 1] = 0xfb;                        /* every bit but 0x4 → still eligible */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(10, 20), 1);
    g_cells[off + 1] = 0x04;                        /* bit 0x4 set → blocked */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(10, 20), 0);
    g_cells[off + 1] = 0xff;                        /* bit 0x4 among others → blocked */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(10, 20), 0);
    return 0;
}

/* Among special values (>= 0xfff0000) only the exact sentinel 0xfff0006 is
 * eligible; the base and the adjacent variants are not. */
int test_territory_elig_special_values(void)
{
    elig_setup(0);
    *tile_at(0, 0) = (int32_t)0xfff0006u;  T_ASSERT_EQ_I(engine_territory_tile_eligible(0, 0), 1);
    *tile_at(0, 0) = (int32_t)0xfff0000u;  T_ASSERT_EQ_I(engine_territory_tile_eligible(0, 0), 0);
    *tile_at(0, 0) = (int32_t)0xfff0005u;  T_ASSERT_EQ_I(engine_territory_tile_eligible(0, 0), 0);
    *tile_at(0, 0) = (int32_t)0xfff0007u;  T_ASSERT_EQ_I(engine_territory_tile_eligible(0, 0), 0);
    return 0;
}

/* Off-grid coordinates (col∉[0,65) or row∉[0,129)) are ineligible; the
 * inclusive in-bounds extremes (0,0) and (64,128) are accepted. */
int test_territory_elig_off_grid_bounds(void)
{
    elig_setup((uint32_t)0xfff0006u);              /* every in-grid tile eligible */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(-1, 0), 0);
    T_ASSERT_EQ_I(engine_territory_tile_eligible(TILE_COLS, 0), 0);     /* col 65 */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(0, -1), 0);
    T_ASSERT_EQ_I(engine_territory_tile_eligible(0, TILE_ROWS), 0);     /* row 129 */
    T_ASSERT_EQ_I(engine_territory_tile_eligible(0, 0), 1);
    T_ASSERT_EQ_I(engine_territory_tile_eligible(TILE_COLS - 1, TILE_ROWS - 1), 1);
    return 0;
}

/* Even-row (row & 1 == 0) neighbour set: the 8 offsets
 * {1,-1}{1,0}{1,1}{0,2}{0,1}{-1,0}{0,-1}{0,-2}.  All eligible → mask all 1;
 * clearing the i3 neighbour (col+0,row+2) flips exactly that entry. */
int test_territory_mask_even_row(void)
{
    elig_setup((uint32_t)0xfff0006u);              /* everything eligible */
    int32_t mask[8];
    engine_territory_compute_neighbour_mask(10, 4, mask);   /* row 4 = even */
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(mask[i], 1);

    *tile_at(6, 10) = (int32_t)0xfff0000u;         /* (col=10,row=6) = i3 offset {0,+2} */
    engine_territory_compute_neighbour_mask(10, 4, mask);
    int32_t want[8] = { 1, 1, 1, 0, 1, 1, 1, 1 };
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(mask[i], want[i]);
    return 0;
}

/* Odd-row parity selects the *other* offset table.  The odd neighbour set of
 * (10,5) is {0,-1}{1,0}{0,1}{0,2}{-1,1}{-1,0}{-1,-1}{0,-2}; lighting only
 * those gives mask all 1.  Lighting (11,4) — an even-table-only neighbour
 * that the odd table never reads — leaves the mask all 0, proving parity. */
int test_territory_mask_odd_row_uses_parity_table(void)
{
    const int oc[8][2] = { {0,-1},{1,0},{0,1},{0,2},{-1,1},{-1,0},{-1,-1},{0,-2} };
    int32_t mask[8];

    /* Background 0xfff0000 is special-but-not-sentinel → ineligible, and (unlike
     * tile value 0, a real cell at offset 0) needs no cell-grid setup. */
    elig_setup((uint32_t)0xfff0000u);
    for (int i = 0; i < 8; i++) *tile_at(5 + oc[i][1], 10 + oc[i][0]) = (int32_t)0xfff0006u;
    engine_territory_compute_neighbour_mask(10, 5, mask);    /* row 5 = odd */
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(mask[i], 1);

    elig_setup((uint32_t)0xfff0000u);
    *tile_at(4, 11) = (int32_t)0xfff0006u;         /* (col=11,row=4): even i0, not odd */
    engine_territory_compute_neighbour_mask(10, 5, mask);
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(mask[i], 0);
    return 0;
}

/* At the (0,0) corner the offset table pushes four neighbours off-grid; those
 * read as ineligible (0) while the in-grid neighbours stay 1. */
int test_territory_mask_clamps_off_grid(void)
{
    elig_setup((uint32_t)0xfff0006u);              /* every in-grid tile eligible */
    int32_t mask[8];
    engine_territory_compute_neighbour_mask(0, 0, mask);     /* even-row corner */
    /* offsets {1,-1}{1,0}{1,1}{0,2}{0,1}{-1,0}{0,-1}{0,-2} from (0,0):
     * off-grid at i0 (1,-1), i5 (-1,0), i6 (0,-1), i7 (0,-2). */
    int32_t want[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    for (int i = 0; i < 8; i++) T_ASSERT_EQ_I(mask[i], want[i]);
    return 0;
}

/* ── Feature-variant classifier (engine_territory_pick_feature, port of
 *    FUN_0046c2de) — tested against the embedded retail pattern table ──── */

/* All-eligible neighbours match entry 0 (pattern 1·2·1·2·1·2·1·2 → the four
 * primary directions eligible).  type 0x4e; cap 2 → first variant 1. */
int test_territory_feature_all_eligible(void)
{
    engine_territory_reset_feature_counters();
    int32_t mask[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    engine_territory_feature_pick_t out = {0};
    T_ASSERT_EQ_I(engine_territory_pick_feature(mask, &out), 1);   /* entry 0 +1 */
    T_ASSERT_EQ_I(out.type, 0x4e);
    T_ASSERT_EQ_I(out.aux, 0);
    T_ASSERT_EQ_I(out.variant, 1);
    return 0;
}

/* All-ineligible neighbours fall through to the catch-all entry 46 (all-zero
 * pattern, the isolated tile).  type 0x16; cap 8 → first variant 1. */
int test_territory_feature_all_ineligible(void)
{
    engine_territory_reset_feature_counters();
    int32_t mask[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    engine_territory_feature_pick_t out = {0};
    T_ASSERT_EQ_I(engine_territory_pick_feature(mask, &out), 47);  /* entry 46 +1 */
    T_ASSERT_EQ_I(out.type, 0x16);
    T_ASSERT_EQ_I(out.variant, 1);
    return 0;
}

/* The first matching row wins: only-pos0 eligible skips the pos0-requiring
 * entry 0 (which also needs pos2/4/6) and lands on entry 15 (type 0x26). */
int test_territory_feature_first_match_wins(void)
{
    engine_territory_reset_feature_counters();
    int32_t mask[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    engine_territory_feature_pick_t out = {0};
    T_ASSERT_EQ_I(engine_territory_pick_feature(mask, &out), 16);  /* entry 15 +1 */
    T_ASSERT_EQ_I(out.type, 0x26);
    return 0;
}

/* The per-entry variant counter rotates (++ then wrap at FT_CAP) and persists
 * across calls.  Entry 0 (cap 2) → 1,0,1,0; entry 46 (cap 8) → 1,2,…,7,0,1. */
int test_territory_feature_counter_rotates(void)
{
    engine_territory_reset_feature_counters();
    int32_t elig[8]   = { 1, 1, 1, 1, 1, 1, 1, 1 };  /* entry 0, cap 2 */
    int32_t inelig[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };  /* entry 46, cap 8 */
    engine_territory_feature_pick_t out = {0};

    const int want0[4] = { 1, 0, 1, 0 };
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_I(engine_territory_pick_feature(elig, &out), 1);
        T_ASSERT_EQ_I(out.variant, want0[i]);
    }
    const int want46[10] = { 1, 2, 3, 4, 5, 6, 7, 0, 1, 2 };
    for (int i = 0; i < 10; i++) {
        T_ASSERT_EQ_I(engine_territory_pick_feature(inelig, &out), 47);
        T_ASSERT_EQ_I(out.variant, want46[i]);
    }
    return 0;
}

/* A cap-1 entry (entry 1, type 0x4b) emits variant 0 on every match: the
 * counter increments to 1, then 1 >= cap(1) wraps it straight back to 0. */
int test_territory_feature_cap1_variant_always_zero(void)
{
    engine_territory_reset_feature_counters();
    int32_t mask[8] = { 1, 0, 1, 0, 1, 0, 0, 0 };  /* pos0/2/4 elig, pos6 inelig → entry 1 */
    engine_territory_feature_pick_t out = {0};
    for (int i = 0; i < 3; i++) {
        T_ASSERT_EQ_I(engine_territory_pick_feature(mask, &out), 2);   /* entry 1 +1 */
        T_ASSERT_EQ_I(out.type, 0x4b);
        T_ASSERT_EQ_I(out.variant, 0);
    }
    return 0;
}

/* The mask the neighbour sweep produces feeds straight into the classifier:
 * an interior all-eligible grid → compute_neighbour_mask → entry 0. */
int test_territory_feature_consumes_neighbour_mask(void)
{
    elig_setup((uint32_t)0xfff0006u);              /* every in-grid tile eligible */
    engine_territory_reset_feature_counters();
    int32_t mask[8];
    engine_territory_compute_neighbour_mask(10, 4, mask);   /* interior, all neighbours elig */
    engine_territory_feature_pick_t out = {0};
    T_ASSERT_EQ_I(engine_territory_pick_feature(mask, &out), 1);
    T_ASSERT_EQ_I(out.type, 0x4e);
    return 0;
}

/* ── Terrain auto-tiling sweep (engine_territory_classify_feature_tiles,
 *    port of FUN_0046dbf5) ─────────────────────────────────────────────── */

/* A single real border cell (flags bit 0x4 set) with all-eligible neighbours
 * (a sentinel background) → entry 0 (type 0x4e, cap-2 variant 1).  The result
 * lands in the cell's +3 byte = (0x4e + 1) & 0xff; the tile grid and RNG are
 * untouched (type != 0x16). */
int test_territory_classify_real_border_cell(void)
{
    elig_setup((uint32_t)0xfff0006u);              /* all sentinel → all neighbours eligible */
    engine_territory_reset_feature_counters();
    int off = cell_off(8, 0);                       /* a real cell's byte-offset = 64 */
    g_cells[off + 1] = 0x04;                        /* bit 0x4 set → this cell is a border */
    *tile_at(4, 10) = off;                          /* tile (col=10,row=4) = real-cell pointer */

    engine_rng_single_in_t rng = { .state = 0x34518632u };
    engine_territory_classify_feature_tiles(&rng);

    T_ASSERT_EQ_I(g_cells[off + 3], (uint8_t)(0x4e + 1));   /* type+variant → +3 */
    T_ASSERT_EQ_U((uint32_t)*tile_at(4, 10), (uint32_t)off);/* real tile not rewritten */
    T_ASSERT_EQ_U(rng.state, 0x34518632u);                 /* no RNG draw */
    return 0;
}

/* A single special non-sentinel border tile with all-eligible neighbours →
 * entry 0 (type 0x4e, variant 1) → the tile is rewritten to
 * 0xfff0000 + type + variant.  RNG untouched. */
int test_territory_classify_special_border_tile(void)
{
    elig_setup((uint32_t)0xfff0006u);
    engine_territory_reset_feature_counters();
    *tile_at(4, 10) = (int32_t)0xfff0010u;          /* special, != sentinel → border */

    engine_rng_single_in_t rng = { .state = 0x999u };
    engine_territory_classify_feature_tiles(&rng);

    T_ASSERT_EQ_U((uint32_t)*tile_at(4, 10), 0xfff0000u + 0x4e + 1);   /* 0xfff004f */
    T_ASSERT_EQ_U(rng.state, 0x999u);
    return 0;
}

/* An all-sentinel grid has no border cells: the sweep touches nothing and
 * draws no RNG. */
int test_territory_classify_skips_eligible_cells(void)
{
    elig_setup((uint32_t)0xfff0006u);
    engine_rng_single_in_t rng = { .state = 0x7u };
    engine_territory_classify_feature_tiles(&rng);
    T_ASSERT_EQ_U((uint32_t)*tile_at(0, 0), 0xfff0006u);
    T_ASSERT_EQ_U((uint32_t)*tile_at(64, 32), 0xfff0006u);
    T_ASSERT_EQ_U(rng.state, 0x7u);                 /* no draw */
    return 0;
}

/* The isolated catch-all (type 0x16) re-rolls its variant from the RNG.  Make
 * the corner cell (0,0) — the first cell swept — a special border whose four
 * in-grid neighbours are all ineligible (the other four are off-grid), so it
 * matches entry 46.  (0,0) consumes the FIRST RNG draw, so its variant is
 * predictable even though later border cells draw more. */
int test_territory_classify_isolated_uses_rng(void)
{
    elig_setup((uint32_t)0xfff0006u);
    engine_territory_reset_feature_counters();
    *tile_at(0, 0) = (int32_t)0xfff0010u;           /* corner special border, swept first */
    /* (0,0)'s in-grid even-row neighbours: (col1,row0)=tile_at(0,1),
     * (col1,row1)=tile_at(1,1), (col0,row2)=tile_at(2,0), (col0,row1)=tile_at(1,0) */
    *tile_at(0, 1) = (int32_t)0xfff0000u;
    *tile_at(1, 1) = (int32_t)0xfff0000u;
    *tile_at(2, 0) = (int32_t)0xfff0000u;
    *tile_at(1, 0) = (int32_t)0xfff0000u;

    const uint32_t seed = 0x34518632u;
    engine_rng_single_in_t rng = { .state = seed };
    engine_territory_classify_feature_tiles(&rng);

    uint32_t st = seed;
    uint32_t variant = ref_single_mirror(&st) & 7u;            /* the FIRST draw */
    T_ASSERT_EQ_U((uint32_t)*tile_at(0, 0), 0xfff0000u + 0x16 + variant);
    T_ASSERT(rng.state != seed);                                /* RNG advanced */
    return 0;
}

/* Unbound grid → no-op (no RNG draw); NULL rng → no-op even with a border
 * cell present (no write). */
int test_territory_classify_unbound_is_noop(void)
{
    engine_territory_bind_terrain_tiles(NULL);
    engine_rng_single_in_t rng = { .state = 0x7u };
    engine_territory_classify_feature_tiles(&rng);
    T_ASSERT_EQ_U(rng.state, 0x7u);

    elig_setup((uint32_t)0xfff0006u);
    *tile_at(4, 10) = (int32_t)0xfff0010u;          /* a real border tile */
    engine_territory_classify_feature_tiles(NULL);  /* NULL rng → no-op */
    T_ASSERT_EQ_U((uint32_t)*tile_at(4, 10), 0xfff0010u);   /* untouched */
    return 0;
}

/* ── Per-army territory adjacency (ports of FUN_0046c080 + FUN_00467ca6) ── */

enum { ARMY_ROWS = 17, ARMY_STRIDE = 0x300, ADJ_COUNT = 0x5a, ADJ_LIST = 0x5c };
/* 4-aligned: the capital-region passes write i32 fields into this block (the
 * real save block is 4-aligned). */
_Alignas(int32_t) static uint8_t g_army[ARMY_ROWS * ARMY_STRIDE];   /* DAT_0053f9b0, g_per_army_768 */

static void adj_setup(void)
{
    memset(g_cells, 0, sizeof g_cells);
    memset(g_army, 0, sizeof g_army);
    engine_territory_bind_cells(g_cells);
    engine_territory_bind_army_records(g_army);
}

/* Set the owning-army key (cell byte +7) of cell (col,row). */
static void cell_key(int col, int row, uint8_t key) { g_cells[cell_off(col, row) + 7] = key; }

static int  adj_count(int key)          { return g_army[key * ARMY_STRIDE + ADJ_COUNT]; }
static int  adj_list(int key, int slot) { return g_army[key * ARMY_STRIDE + ADJ_LIST + slot]; }

/* FUN_0046c080: the four orthogonal neighbour keys come back in N,E,S,W
 * order from an interior cell. */
int test_territory_neighbour_keys_interior_order(void)
{
    adj_setup();
    cell_key(10,  9, 1);   /* N */
    cell_key(11, 10, 2);   /* E */
    cell_key(10, 11, 3);   /* S */
    cell_key( 9, 10, 4);   /* W */
    int32_t out[4];
    engine_territory_load_neighbour_keys(10, 10, cell_off(10, 10), out);
    T_ASSERT_EQ_I(out[0], 1);
    T_ASSERT_EQ_I(out[1], 2);
    T_ASSERT_EQ_I(out[2], 3);
    T_ASSERT_EQ_I(out[3], 4);
    return 0;
}

/* FUN_0046c080: edge neighbours read 0 and the off-grid slot is never
 * sourced from a wrapped row/column. */
int test_territory_neighbour_keys_edges_zero(void)
{
    adj_setup();
    /* Top-left corner: N (row 0) and W (col 0) are off-grid → 0. */
    cell_key(1, 0, 5);     /* E of (0,0) */
    cell_key(0, 1, 6);     /* S of (0,0) */
    int32_t out[4];
    engine_territory_load_neighbour_keys(0, 0, cell_off(0, 0), out);
    T_ASSERT_EQ_I(out[0], 0);   /* N off-grid */
    T_ASSERT_EQ_I(out[1], 5);   /* E */
    T_ASSERT_EQ_I(out[2], 6);   /* S */
    T_ASSERT_EQ_I(out[3], 0);   /* W off-grid */

    /* Bottom-right corner: E (col 0x3f) and S (row 0x3f) off-grid → 0. */
    cell_key(0x3f, 0x3e, 7);    /* N of (0x3f,0x3f) */
    cell_key(0x3e, 0x3f, 8);    /* W of (0x3f,0x3f) */
    engine_territory_load_neighbour_keys(0x3f, 0x3f, cell_off(0x3f, 0x3f), out);
    T_ASSERT_EQ_I(out[0], 7);   /* N */
    T_ASSERT_EQ_I(out[1], 0);   /* E off-grid */
    T_ASSERT_EQ_I(out[2], 0);   /* S off-grid */
    T_ASSERT_EQ_I(out[3], 8);   /* W */
    return 0;
}

/* FUN_00467ca6: a cell's distinct in-range neighbour keys are recorded in
 * its own army row, in N,E,S,W scan order; same-key and 0 neighbours are
 * skipped. */
int test_territory_adjacency_records_distinct_neighbours(void)
{
    adj_setup();
    cell_key(10, 10, 5);   /* centre army */
    cell_key(10,  9, 3);   /* N → recorded */
    cell_key(11, 10, 9);   /* E → recorded */
    cell_key(10, 11, 5);   /* S → same key, skipped */
    /* W (9,10) left 0 → skipped */

    engine_territory_build_army_adjacency();

    T_ASSERT_EQ_I(adj_count(5), 2);
    T_ASSERT_EQ_I(adj_list(5, 0), 3);   /* N before E */
    T_ASSERT_EQ_I(adj_list(5, 1), 9);
    T_ASSERT_EQ_I(adj_list(5, 2), 0);
    /* Adjacency is symmetric: 3 and 9 each list 5 once. */
    T_ASSERT_EQ_I(adj_count(3), 1);
    T_ASSERT_EQ_I(adj_list(3, 0), 5);
    T_ASSERT_EQ_I(adj_count(9), 1);
    T_ASSERT_EQ_I(adj_list(9, 0), 5);
    return 0;
}

/* FUN_00467ca6: a neighbour key already in the list is not duplicated, even
 * across several bordering cells. */
int test_territory_adjacency_dedups(void)
{
    adj_setup();
    cell_key(20, 20, 7);  cell_key(21, 20, 8);   /* 7 borders 8 (E) */
    cell_key(20, 22, 7);  cell_key(21, 22, 8);   /* 7 borders 8 again (E) */

    engine_territory_build_army_adjacency();

    T_ASSERT_EQ_I(adj_count(7), 1);
    T_ASSERT_EQ_I(adj_list(7, 0), 8);
    T_ASSERT_EQ_I(adj_list(7, 1), 0);
    return 0;
}

/* FUN_00467ca6: an army bordering every other in-range key fills the list.
 * With the +7 field gated to 1..16 (key 17 can never be a swept centre —
 * quirk #132), the most distinct OTHER keys any army can have is 15, so the
 * 16-entry cap's silent-drop arm is never reached in valid data. */
int test_territory_adjacency_fills_list(void)
{
    adj_setup();
    /* A vertical strip of army 8; each cell's east neighbour is a distinct
     * other key drawn from {1..16}\{8} (15 keys), in scan (row) order. */
    for (int r = 0; r < 15; r++) {
        cell_key(10, r, 8);
        cell_key(11, r, (uint8_t)(r < 7 ? r + 1 : r + 2));   /* 1..7, 9..16 */
    }

    engine_territory_build_army_adjacency();

    T_ASSERT_EQ_I(adj_count(8), 15);
    for (int r = 0; r < 15; r++)
        T_ASSERT_EQ_I(adj_list(8, r), (r < 7 ? r + 1 : r + 2));
    T_ASSERT_EQ_I(adj_list(8, 15), 0);   /* 16th slot never filled */
    return 0;
}

/* Both ports no-op when their backing memory is unbound. */
int test_territory_adjacency_unbound_is_noop(void)
{
    int32_t out[4] = { 9, 9, 9, 9 };
    engine_territory_bind_cells(NULL);
    engine_territory_load_neighbour_keys(10, 10, cell_off(10, 10), out);
    T_ASSERT_EQ_I(out[0], 0);
    T_ASSERT_EQ_I(out[3], 0);
    engine_territory_build_army_adjacency();   /* cells unbound → no crash */

    adj_setup();
    engine_territory_bind_army_records(NULL);
    cell_key(10, 10, 5);
    cell_key(11, 10, 6);
    engine_territory_build_army_adjacency();   /* army block unbound → no-op */
    return 0;
}

/* ── Per-army feature seeding (engine_territory_seed_army_features,
 *    port of FUN_00467a36) ─────────────────────────────────────────── */

#define ARMY_ACTIVE 0x205   /* per-army active-cell tally byte */

static void seed_setup(uint32_t density)
{
    memset(g_cells, 0, sizeof g_cells);
    memset(g_army, 0, sizeof g_army);
    engine_territory_bind_cells(g_cells);
    engine_territory_bind_army_records(g_army);
    engine_territory_bind_cell_roster(NULL);       /* type-0 score clear → no-op */
    engine_territory_bind_cell_score_table(NULL);
    s_slot_0053f23c = density;
}

static int seed_type(int col, int row) { return g_cells[cell_off(col, row) + 0]; }
static int army_active(int key)        { return g_army[key * ARMY_STRIDE + ARMY_ACTIVE]; }

/* Density 2: the army's first 4 active cells (scan order) get the rich type
 * 0x14, the next 2 get type 1, the rest type 0; the tally counts them all.
 * Also checks the derived +3/+2 bytes the setter produces for a 0x14 cell. */
int test_territory_seed_density2_tiers(void)
{
    seed_setup(2);
    for (int c = 1; c <= 7; c++)        /* row 0 cols 1..7 → ascending offsets */
        cell_feature(c, 0, 3);

    engine_territory_seed_army_features();

    for (int c = 1; c <= 4; c++) T_ASSERT_EQ_I(seed_type(c, 0), 0x14);   /* idx 0..3 */
    T_ASSERT_EQ_I(seed_type(5, 0), 1);                                   /* idx 4    */
    T_ASSERT_EQ_I(seed_type(6, 0), 1);                                   /* idx 5    */
    T_ASSERT_EQ_I(seed_type(7, 0), 0);                                   /* idx 6    */
    T_ASSERT_EQ_I(army_active(3), 7);

    /* A 0x14 cell's setter-derived bytes: +3 base 'h'=0x68, +2 = 1|8|0x80
     * (type 0x14 is in the bit7 band 15..22). */
    T_ASSERT_EQ_I(g_cells[cell_off(1, 0) + 3], 0x68);
    T_ASSERT_EQ_I(g_cells[cell_off(1, 0) + 2], 0x89);
    return 0;
}

/* Density 1 cutoffs (idx<6 → 0x14, idx<8 → 1, else 0) and density 3 (idx<4
 * → 0x14, else 0 — no type-1 tier at all). */
int test_territory_seed_density1_and_3_cutoffs(void)
{
    seed_setup(1);
    for (int c = 1; c <= 9; c++) cell_feature(c, 0, 2);
    engine_territory_seed_army_features();
    for (int c = 1; c <= 6; c++) T_ASSERT_EQ_I(seed_type(c, 0), 0x14);   /* idx 0..5 */
    T_ASSERT_EQ_I(seed_type(7, 0), 1);                                   /* idx 6    */
    T_ASSERT_EQ_I(seed_type(8, 0), 1);                                   /* idx 7    */
    T_ASSERT_EQ_I(seed_type(9, 0), 0);                                   /* idx 8    */

    seed_setup(3);
    for (int c = 1; c <= 6; c++) cell_feature(c, 0, 2);
    engine_territory_seed_army_features();
    for (int c = 1; c <= 4; c++) T_ASSERT_EQ_I(seed_type(c, 0), 0x14);   /* idx 0..3 */
    T_ASSERT_EQ_I(seed_type(5, 0), 0);                                   /* idx 4 → 0 (no tier-1) */
    T_ASSERT_EQ_I(seed_type(6, 0), 0);                                   /* idx 5    */
    return 0;
}

/* The default arm (density not 1/2/3, here 0): idx<8 → 0x14, else → 1 — no
 * cell is ever flattened to type 0. */
int test_territory_seed_default_density(void)
{
    seed_setup(0);
    for (int c = 1; c <= 10; c++) cell_feature(c, 0, 4);
    engine_territory_seed_army_features();
    for (int c = 1; c <= 8; c++) T_ASSERT_EQ_I(seed_type(c, 0), 0x14);   /* idx 0..7 */
    T_ASSERT_EQ_I(seed_type(9, 0), 1);                                   /* idx 8    */
    T_ASSERT_EQ_I(seed_type(10, 0), 1);                                  /* idx 9    */
    return 0;
}

/* Only active feature-cells (key 1..17 AND +1 bit 0x20) are seeded; key-0,
 * out-of-range, and unflagged cells are ignored and keep their type. */
int test_territory_seed_filters_inactive(void)
{
    seed_setup(2);
    cell_feature(1, 0, 5);                          /* active → seeded */
    int o;
    o = cell_off(2, 0); g_cells[o + 7] = 0;  g_cells[o + 1] |= 0x20; g_cells[o + 0] = 0x42; /* key 0 */
    o = cell_off(3, 0); g_cells[o + 7] = 18; g_cells[o + 1] |= 0x20; g_cells[o + 0] = 0x43; /* key 18 */
    o = cell_off(4, 0); g_cells[o + 7] = 5;                          g_cells[o + 0] = 0x44; /* no flag */

    engine_territory_seed_army_features();

    T_ASSERT_EQ_I(seed_type(1, 0), 0x14);           /* the lone active cell */
    T_ASSERT_EQ_I(seed_type(2, 0), 0x42);           /* untouched */
    T_ASSERT_EQ_I(seed_type(3, 0), 0x43);
    T_ASSERT_EQ_I(seed_type(4, 0), 0x44);
    T_ASSERT_EQ_I(army_active(5), 1);               /* only the active one tallied */
    return 0;
}

/* Each army gets its own running index (both start at the rich type), and
 * step 1 zeroes the +0x205 tally of rows 1..16 before counting. */
int test_territory_seed_per_key_independent_and_resets_tally(void)
{
    seed_setup(3);
    for (int k = 1; k <= 16; k++)
        g_army[k * ARMY_STRIDE + ARMY_ACTIVE] = 0x55;   /* stale → must be reset */

    cell_feature(1, 0, 6);    /* key 6, idx 0 → 0x14 */
    cell_feature(2, 0, 9);    /* key 9, idx 0 → 0x14 (independent of key 6) */
    cell_feature(3, 0, 6);    /* key 6, idx 1 → 0x14 */

    engine_territory_seed_army_features();

    T_ASSERT_EQ_I(seed_type(1, 0), 0x14);
    T_ASSERT_EQ_I(seed_type(2, 0), 0x14);
    T_ASSERT_EQ_I(seed_type(3, 0), 0x14);
    T_ASSERT_EQ_I(army_active(6), 2);
    T_ASSERT_EQ_I(army_active(9), 1);
    T_ASSERT_EQ_I(army_active(7), 0);               /* reset, no cells */
    return 0;
}

/* No-op when the cell grid or the army block is unbound. */
int test_territory_seed_unbound_is_noop(void)
{
    seed_setup(2);
    engine_territory_bind_cells(NULL);
    engine_territory_seed_army_features();           /* cells unbound → no crash */

    seed_setup(2);
    cell_feature(1, 0, 3);
    engine_territory_bind_army_records(NULL);
    engine_territory_seed_army_features();           /* army unbound → no-op */
    T_ASSERT_EQ_I(seed_type(1, 0), 0);               /* type unchanged (cell_feature left +0 at 0) */
    return 0;
}

/* ── Per-army terrain census (port of FUN_00469b8d) ─────────────────
 * Reuse g_cells / g_roster / g_army.  A "tile of TYPE t in army K's
 * roster column c" = a cell whose +0 byte is t, with that cell's
 * byte-offset stored in g_roster[K][c]. */

enum { CEN_TYPE1 = 0x1ff, CEN_HIGH = 0x200, CEN_MID = 0x201,
       CEN_HOLE = 0x202, CEN_SPECIAL = 0x203, CEN_OVER = 0x204 };

static void census_setup(void)
{
    memset(g_cells, 0, sizeof g_cells);
    memset(g_roster, 0, sizeof g_roster);
    memset(g_army, 0, sizeof g_army);
    engine_territory_bind_cells(g_cells);
    engine_territory_bind_cell_roster(g_roster);
    engine_territory_bind_army_records(g_army);
}

/* Put a tile of TYPE `t` into army `key`'s roster column `col`, backed by a
 * distinct cell (one per (key,col) so offsets never collide and 0 is never
 * a real offset — cell (0,0) is avoided by starting at index 1). */
static void census_tile(int key, int col, uint8_t t)
{
    int cell = key * ROSTER_NCOL + col + 1;   /* >= 1 → offset != 0 */
    int off  = cell * CELL_STRIDE;
    g_cells[off] = t;
    g_roster[key * ROSTER_NCOL + col] = off;
}

static int census(int key, int off) { return g_army[key * ARMY_STRIDE + off]; }

/* Each TYPE band lands in its own counter, and TYPE 0 / 0x17 / 0x18 all
 * share the +0x203 bucket. */
int test_territory_census_buckets_by_type(void)
{
    census_setup();
    census_tile(5, 0, 1);       /* +0x1ff */
    census_tile(5, 1, 0x10);    /* 0x0f..0x16 → +0x200 */
    census_tile(5, 2, 0x05);    /* 0x02..0x0e → +0x201 */
    census_tile(5, 3, 0);       /* special   → +0x203 */
    census_tile(5, 4, 0x17);    /* special   → +0x203 */
    census_tile(5, 5, 0x18);    /* special   → +0x203 */
    census_tile(5, 6, 0x19);    /* >= 0x19   → +0x204 */
    census_tile(5, 7, 0xff);    /* >= 0x19   → +0x204 */

    engine_territory_census_army_terrain(5);

    T_ASSERT_EQ_I(census(5, CEN_TYPE1),   1);
    T_ASSERT_EQ_I(census(5, CEN_HIGH),    1);
    T_ASSERT_EQ_I(census(5, CEN_MID),     1);
    T_ASSERT_EQ_I(census(5, CEN_SPECIAL), 3);
    T_ASSERT_EQ_I(census(5, CEN_OVER),    2);
    T_ASSERT_EQ_I(census(5, CEN_HOLE),    0);   /* +0x202 never touched */
    return 0;
}

/* Band boundaries: 1|2|0xe|0xf|0x16|0x17|0x18|0x19 land exactly where the
 * retail ladder puts them. */
int test_territory_census_band_boundaries(void)
{
    census_setup();
    census_tile(3, 0, 1);       /* TYPE1 */
    census_tile(3, 1, 2);       /* MID lower edge */
    census_tile(3, 2, 0x0e);    /* MID upper edge */
    census_tile(3, 3, 0x0f);    /* HIGH lower edge */
    census_tile(3, 4, 0x16);    /* HIGH upper edge */
    census_tile(3, 5, 0x17);    /* SPECIAL */
    census_tile(3, 6, 0x18);    /* SPECIAL */
    census_tile(3, 7, 0x19);    /* OVER lower edge */

    engine_territory_census_army_terrain(3);

    T_ASSERT_EQ_I(census(3, CEN_TYPE1),   1);
    T_ASSERT_EQ_I(census(3, CEN_MID),     2);
    T_ASSERT_EQ_I(census(3, CEN_HIGH),    2);
    T_ASSERT_EQ_I(census(3, CEN_SPECIAL), 2);
    T_ASSERT_EQ_I(census(3, CEN_OVER),    1);
    return 0;
}

/* Empty roster columns (offset 0) are skipped; only the army's own row is
 * tallied. */
int test_territory_census_skips_empty_and_other_rows(void)
{
    census_setup();
    census_tile(4, 0, 5);       /* MID */
    census_tile(4, 5, 5);       /* MID — gap of empty columns 1..4 between */
    census_tile(7, 0, 5);       /* a different army's tile — must not count */

    engine_territory_census_army_terrain(4);

    T_ASSERT_EQ_I(census(4, CEN_MID), 2);
    /* Army 7 untouched (we never censused it). */
    T_ASSERT_EQ_I(census(7, CEN_MID), 0);
    return 0;
}

/* The five counters are reset at entry, even with no roster cells. */
int test_territory_census_clears_stale_counters(void)
{
    census_setup();
    g_army[6 * ARMY_STRIDE + CEN_TYPE1]   = 0x42;
    g_army[6 * ARMY_STRIDE + CEN_HIGH]    = 0x42;
    g_army[6 * ARMY_STRIDE + CEN_MID]     = 0x42;
    g_army[6 * ARMY_STRIDE + CEN_SPECIAL] = 0x42;
    g_army[6 * ARMY_STRIDE + CEN_OVER]    = 0x42;
    g_army[6 * ARMY_STRIDE + CEN_HOLE]    = 0x42;   /* +0x202 must survive */

    engine_territory_census_army_terrain(6);        /* empty roster row */

    T_ASSERT_EQ_I(census(6, CEN_TYPE1),   0);
    T_ASSERT_EQ_I(census(6, CEN_HIGH),    0);
    T_ASSERT_EQ_I(census(6, CEN_MID),     0);
    T_ASSERT_EQ_I(census(6, CEN_SPECIAL), 0);
    T_ASSERT_EQ_I(census(6, CEN_OVER),    0);
    T_ASSERT_EQ_I(census(6, CEN_HOLE),    0x42);    /* untouched */
    return 0;
}

/* No-op when the army block is unbound; counters merely cleared when the
 * roster or cell grid is unbound. */
int test_territory_census_unbound(void)
{
    census_setup();
    census_tile(2, 0, 5);
    engine_territory_bind_army_records(NULL);
    engine_territory_census_army_terrain(2);        /* army unbound → no crash, no write */
    T_ASSERT_EQ_I(g_army[2 * ARMY_STRIDE + CEN_MID], 0);

    census_setup();
    g_army[2 * ARMY_STRIDE + CEN_MID] = 0x42;
    census_tile(2, 0, 5);
    engine_territory_bind_cell_roster(NULL);
    engine_territory_census_army_terrain(2);        /* roster unbound → counters cleared only */
    T_ASSERT_EQ_I(census(2, CEN_MID), 0);
    return 0;
}

/* The all-realms dispatcher censuses realms 1..s_slot_0056d5dc inclusive: with
 * the count at 2, realms 1 and 2 are tallied but realm 3 (a tile waiting in its
 * roster, its counter pre-dirtied) is never visited. */
int test_territory_census_all_realms_bound(void)
{
    census_setup();
    census_tile(1, 0, 1);       /* TYPE1 → +0x1ff */
    census_tile(2, 0, 1);
    census_tile(3, 0, 1);
    g_army[3 * ARMY_STRIDE + CEN_TYPE1] = 0x42;   /* must survive if unvisited */
    s_slot_0056d5dc = 2;

    engine_territory_census_all_realms();

    T_ASSERT_EQ_I(census(1, CEN_TYPE1), 1);
    T_ASSERT_EQ_I(census(2, CEN_TYPE1), 1);
    T_ASSERT_EQ_I(census(3, CEN_TYPE1), 0x42);    /* realm 3 > count → not censused */
    return 0;
}

/* ── Secondary-settlement stamp (port of FUN_0046826c) ──────────────
 * Reuse census_setup (binds g_cells + g_army).  Set the realm's secondary
 * cell offset (+0x78) and the two tier fields (+0x1c3 / +0x1c4), stamp,
 * then read the 2×2 block the shared stamp_block laid down. */

enum { SETTLE_FLAG = 0x1c3, SETTLE_SIZE = 0x1c4, SETTLE_OFF = 0x78 };

static void sec_setup(int key, int col, int row)
{
    census_setup();
    *(int32_t *)&g_army[key * ARMY_STRIDE + SETTLE_OFF] = cell_off(col, row);
}

/* Descriptor (+3) of the block cell at (col,row). */
static int desc(int col, int row) { return g_cells[cell_off(col, row) + 3]; }
static int ctype(int col, int row) { return g_cells[cell_off(col, row) + 0]; }

/* Tier 'P' (no secondary settlement, +0x1c3 == 0), level 0: base 0x50,
 * type 0x15, 2×2 gradient {0,2,1,3}. */
int test_territory_secondary_tier_none(void)
{
    sec_setup(5, 20, 20);
    /* +0x1c3 left 0 → 'P' tier */
    engine_territory_stamp_secondary_keep(5, 0);

    T_ASSERT_EQ_I(desc(20, 20), 0x50);
    T_ASSERT_EQ_I(desc(21, 20), 0x52);
    T_ASSERT_EQ_I(desc(20, 21), 0x51);
    T_ASSERT_EQ_I(desc(21, 21), 0x53);
    T_ASSERT_EQ_I(ctype(20, 20), 0x15);
    T_ASSERT_EQ_I(ctype(21, 21), 0x15);
    return 0;
}

/* Tier '(' (settlement present, size < 0x32), level 1: base 0x28 + 4 = 0x2c,
 * type 0x16. */
int test_territory_secondary_tier_small(void)
{
    sec_setup(3, 8, 8);
    g_army[3 * ARMY_STRIDE + SETTLE_FLAG] = 1;     /* present  */
    g_army[3 * ARMY_STRIDE + SETTLE_SIZE] = 0x31;  /* < 0x32   */
    engine_territory_stamp_secondary_keep(3, 1);

    T_ASSERT_EQ_I(desc(8, 8), 0x2c);
    T_ASSERT_EQ_I(desc(9, 9), 0x2c + 3);
    T_ASSERT_EQ_I(ctype(8, 8), 0x16);
    return 0;
}

/* Tier '<' (settlement present, size >= 0x32), level 4: base 0x3c + 16 = 0x4c,
 * type 0x19 (the level cap). */
int test_territory_secondary_tier_large_and_level_cap(void)
{
    sec_setup(2, 30, 12);
    g_army[2 * ARMY_STRIDE + SETTLE_FLAG] = 1;
    g_army[2 * ARMY_STRIDE + SETTLE_SIZE] = 0x32;  /* >= 0x32 → large */
    engine_territory_stamp_secondary_keep(2, 4);

    T_ASSERT_EQ_I(desc(30, 12), 0x4c);
    T_ASSERT_EQ_I(ctype(30, 12), 0x19);

    /* level 5 still caps the TYPE at 0x19 (base keeps climbing: 0x3c+20). */
    sec_setup(2, 30, 12);
    g_army[2 * ARMY_STRIDE + SETTLE_FLAG] = 1;
    g_army[2 * ARMY_STRIDE + SETTLE_SIZE] = 0x40;
    engine_territory_stamp_secondary_keep(2, 5);
    T_ASSERT_EQ_I(ctype(30, 12), 0x19);
    T_ASSERT_EQ_I(desc(30, 12), 0x3c + 20);
    return 0;
}

/* No-op when the army block is unbound (no crash, no cell write). */
int test_territory_secondary_unbound_is_noop(void)
{
    sec_setup(4, 16, 16);
    engine_territory_bind_army_records(NULL);
    engine_territory_stamp_secondary_keep(4, 0);
    T_ASSERT_EQ_I(g_cells[cell_off(16, 16) + 3], 0);   /* untouched */
    return 0;
}

/* ── Per-realm population/job-slot allocator (port of FUN_0044f6e7) ──
 * Reuse census_setup (binds g_army, zeroed).  Set the realm's total (+0x24),
 * split percentage (+0x08), bucket caps/ratios and building gates, run the
 * allocator, then read the eight counters and the +0x124 leftover. */

enum {
    WF_TOTAL = 0x24, WF_PCT = 0x08, WF_FOLD = 0xb8,
    WF_C0 = 0xc4, WF_C1 = 0xd0, WF_C2 = 0xdc, WF_C3 = 0xe8, WF_C4 = 0xf4,
    WF_C5 = 0x100, WF_C6 = 0x10c, WF_C7 = 0x118, WF_LEFT = 0x124,
    WF_CAP0 = 0xcc, WF_CAP1 = 0xd8, WF_CAP2 = 0xe4, WF_CAP3 = 0xf0,
    WF_CAP4 = 0xfc, WF_CAP5 = 0x108, WF_CAP6 = 0x114, WF_CAP7 = 0x120,
    WF_R0 = 0x130, WF_R1 = 0x134, WF_R2 = 0x138, WF_R3 = 0x13c,
    WF_R4 = 0x140, WF_R5 = 0x144, WF_R6 = 0x148, WF_R7 = 0x14c,
    WF_G6 = 0x297, WF_G4 = 0x2af, WF_G7 = 0x2c7, WF_G5 = 0x2df,
    WF_G3A = 0x1c3, WF_G3B = 0x1b0,
};

static void wf_set(int key, int off, int32_t v)
{
    *(int32_t *)&g_army[key * ARMY_STRIDE + off] = v;
}
static int32_t wf_get(int key, int off)
{
    return *(int32_t *)&g_army[key * ARMY_STRIDE + off];
}

/* Phase 1: pool2 (= total when pct 0) is pre-filled toward percent_of(pool2,
 * ratio_i) for the three ungated buckets; with ample caps and ratios summing
 * to 100, the greedy step consumes pool2 exactly and nothing spills. */
int test_territory_workforce_phase1_greedy(void)
{
    census_setup();
    wf_set(5, WF_TOTAL, 100);
    g_army[5 * ARMY_STRIDE + WF_PCT] = 0;          /* pool1 = 0, pool2 = 100 */
    wf_set(5, WF_R0, 20); wf_set(5, WF_R1, 30); wf_set(5, WF_R2, 50);
    wf_set(5, WF_CAP0, 1000); wf_set(5, WF_CAP1, 1000); wf_set(5, WF_CAP2, 1000);

    engine_territory_allocate_realm_workforce(5);

    T_ASSERT_EQ_I(wf_get(5, WF_C0), 20);
    T_ASSERT_EQ_I(wf_get(5, WF_C1), 30);
    T_ASSERT_EQ_I(wf_get(5, WF_C2), 50);
    T_ASSERT_EQ_I(wf_get(5, WF_C6), 0);            /* pool1 empty → phase 2 idle */
    T_ASSERT_EQ_I(wf_get(5, WF_LEFT), 0);
    return 0;
}

/* The +0x08 percentage splits total into pool1 (→ phase-2 building buckets)
 * and pool2 (→ phase-1 buckets); each phase greedily fills to its ratios. */
int test_territory_workforce_pool_split(void)
{
    census_setup();
    wf_set(3, WF_TOTAL, 200);
    g_army[3 * ARMY_STRIDE + WF_PCT] = 50;         /* pool1 = 100, pool2 = 100 */
    wf_set(3, WF_R0, 50); wf_set(3, WF_R1, 50);
    wf_set(3, WF_CAP0, 1000); wf_set(3, WF_CAP1, 1000);
    /* phase-2 buckets 6 and 4, each gated present, ratios 50/50 of pool1 */
    wf_set(3, WF_R6, 50); wf_set(3, WF_R4, 50);
    wf_set(3, WF_CAP6, 1000); wf_set(3, WF_CAP4, 1000);
    g_army[3 * ARMY_STRIDE + WF_G6] = 1;
    g_army[3 * ARMY_STRIDE + WF_G4] = 1;

    engine_territory_allocate_realm_workforce(3);

    T_ASSERT_EQ_I(wf_get(3, WF_C0), 50);
    T_ASSERT_EQ_I(wf_get(3, WF_C1), 50);
    T_ASSERT_EQ_I(wf_get(3, WF_C6), 50);
    T_ASSERT_EQ_I(wf_get(3, WF_C4), 50);
    T_ASSERT_EQ_I(wf_get(3, WF_LEFT), 0);
    return 0;
}

/* A phase-2 bucket whose "present" flag is clear has its cap forced to 0, so
 * it accepts nothing even with a non-zero ratio — that share lands in the
 * +0x124 leftover instead. */
int test_territory_workforce_building_gate(void)
{
    census_setup();
    wf_set(4, WF_TOTAL, 100);
    g_army[4 * ARMY_STRIDE + WF_PCT] = 100;        /* all of total → pool1 */
    wf_set(4, WF_R6, 50); wf_set(4, WF_R5, 50);
    wf_set(4, WF_CAP6, 50);                        /* exactly the greedy target */
    wf_set(4, WF_CAP5, 1000);
    g_army[4 * ARMY_STRIDE + WF_G6] = 1;           /* bucket 6 present  */
    g_army[4 * ARMY_STRIDE + WF_G5] = 0;           /* bucket 5 absent → cap 0 */

    engine_territory_allocate_realm_workforce(4);

    T_ASSERT_EQ_I(wf_get(4, WF_C6), 50);
    T_ASSERT_EQ_I(wf_get(4, WF_C5), 0);            /* gated off despite ratio 50 */
    T_ASSERT_EQ_I(wf_get(4, WF_LEFT), 50);         /* bucket-5 share spills here */
    return 0;
}

/* Phase-1 round-robin deals the unspent pool in a 2 : 3 : 1 weighting across
 * buckets 0,1,2.  With zero ratios (no greedy pre-fill) and ample caps, the
 * whole pool flows through the round-robin: 8 → 4 : 3 : 1. */
int test_territory_workforce_round_robin_weights(void)
{
    census_setup();
    wf_set(6, WF_TOTAL, 8);
    g_army[6 * ARMY_STRIDE + WF_PCT] = 0;
    wf_set(6, WF_CAP0, 100); wf_set(6, WF_CAP1, 100); wf_set(6, WF_CAP2, 100);

    engine_territory_allocate_realm_workforce(6);

    T_ASSERT_EQ_I(wf_get(6, WF_C0), 4);            /* 2 per pass + the partial pass */
    T_ASSERT_EQ_I(wf_get(6, WF_C1), 3);
    T_ASSERT_EQ_I(wf_get(6, WF_C2), 1);
    T_ASSERT_EQ_I(wf_get(6, WF_LEFT), 0);
    return 0;
}

/* A small phase-1 remainder (< the +0xb8 threshold byte) is folded into pool1
 * before phase 2, so it can still feed the building buckets.  Here phase 1
 * caps out at 30 (3 × 10), leaving 70 < threshold 100 → folded into pool1,
 * which bucket 6 then absorbs (without the fold it would be stranded). */
int test_territory_workforce_small_remainder_fold(void)
{
    census_setup();
    wf_set(2, WF_TOTAL, 100);
    g_army[2 * ARMY_STRIDE + WF_PCT] = 0;          /* pool2 = 100, pool1 = 0 */
    wf_set(2, WF_CAP0, 10); wf_set(2, WF_CAP1, 10); wf_set(2, WF_CAP2, 10);
    g_army[2 * ARMY_STRIDE + WF_FOLD] = 100;       /* 70 < 100 → fold */
    wf_set(2, WF_R6, 100); wf_set(2, WF_CAP6, 1000);
    g_army[2 * ARMY_STRIDE + WF_G6] = 1;

    engine_territory_allocate_realm_workforce(2);

    T_ASSERT_EQ_I(wf_get(2, WF_C0), 10);
    T_ASSERT_EQ_I(wf_get(2, WF_C1), 10);
    T_ASSERT_EQ_I(wf_get(2, WF_C2), 10);
    T_ASSERT_EQ_I(wf_get(2, WF_C6), 70);           /* the folded remainder */
    T_ASSERT_EQ_I(wf_get(2, WF_LEFT), 0);
    return 0;
}

/* A cap carrying retail's 999999 "unlimited" sentinel is treated as 0, so its
 * bucket never fills and the would-be allocation lands in the leftover. */
int test_territory_workforce_unlimited_sentinel_cap(void)
{
    census_setup();
    wf_set(7, WF_TOTAL, 100);
    g_army[7 * ARMY_STRIDE + WF_PCT] = 0;
    wf_set(7, WF_R0, 100);
    wf_set(7, WF_CAP0, 999999);                    /* sentinel → 0 */

    engine_territory_allocate_realm_workforce(7);

    T_ASSERT_EQ_I(wf_get(7, WF_C0), 0);
    T_ASSERT_EQ_I(wf_get(7, WF_LEFT), 100);
    return 0;
}

/* No-op when the army-record block is unbound (no crash, record untouched). */
int test_territory_workforce_unbound_is_noop(void)
{
    census_setup();
    wf_set(2, WF_LEFT, 0x42);
    wf_set(2, WF_TOTAL, 100);
    engine_territory_bind_army_records(NULL);
    engine_territory_allocate_realm_workforce(2);  /* must not crash */
    engine_territory_bind_army_records(g_army);
    T_ASSERT_EQ_I(wf_get(2, WF_LEFT), 0x42);       /* untouched */
    return 0;
}

/* The all-realms dispatcher allocates for realms 1..s_slot_0056d5dc inclusive:
 * with the count at 2, realms 1 and 2 are processed (a phase-1 greedy fill
 * lands the ratio in counter 0) but realm 3's pre-dirtied counter is never
 * visited. */
int test_territory_workforce_all_realms_bound(void)
{
    census_setup();
    for (int k = 1; k <= 3; k++) {
        wf_set(k, WF_TOTAL, 100);
        g_army[k * ARMY_STRIDE + WF_PCT] = 0;      /* pool2 = 100 */
        wf_set(k, WF_R0, 100);
        wf_set(k, WF_CAP0, 1000);
    }
    wf_set(3, WF_C0, 0x42);                        /* must survive if unvisited */
    s_slot_0056d5dc = 2;

    engine_territory_allocate_all_workforce();

    T_ASSERT_EQ_I(wf_get(1, WF_C0), 100);
    T_ASSERT_EQ_I(wf_get(2, WF_C0), 100);
    T_ASSERT_EQ_I(wf_get(3, WF_C0), 0x42);         /* realm 3 > count → untouched */
    return 0;
}

/* ── Band retype of an army's feature cells (port of FUN_00469d21) ──
 * Reuse census_setup (binds g_cells + g_army).  A "feature cell" for the
 * retype carries owner key at +7, the active flag (bit 0x20) at +1, and a
 * TYPE byte at +0.  engine_territory_set_cell_type rewrites +0/+2/+3; the
 * tests assert the +0 TYPE byte (the observable the retype steers). */

enum { BAND_RETYPE_FLAG = 0x1a7 };

static void band_cell(int col, int row, uint8_t key, uint8_t type)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 1] |= 0x20;
    g_cells[o + 0] = type;
}

/* Only cells matching all three predicates (owner key, +1 bit 0x20, TYPE in
 * [lo,hi]) are retyped; everything else is left alone. */
int test_territory_retype_band_filters(void)
{
    census_setup();
    band_cell(5, 5, 3, 0x13);          /* in band, owned, flagged → retyped */
    band_cell(6, 5, 3, 0x15);          /* in band                  → retyped */
    band_cell(7, 5, 3, 0x12);          /* TYPE below band          → skipped */
    band_cell(8, 5, 4, 0x14);          /* different owner          → skipped */
    int o = cell_off(9, 5);            /* right owner+type but unflagged      */
    g_cells[o + 7] = 3; g_cells[o + 0] = 0x14;   /* no bit 0x20 → skipped */

    engine_territory_retype_army_band(3, 0x16, 0, 0x13, 0x16);

    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 0], 0x16);
    T_ASSERT_EQ_I(g_cells[cell_off(6, 5) + 0], 0x16);
    T_ASSERT_EQ_I(g_cells[cell_off(7, 5) + 0], 0x12);   /* untouched */
    T_ASSERT_EQ_I(g_cells[cell_off(8, 5) + 0], 0x14);   /* untouched */
    T_ASSERT_EQ_I(g_cells[cell_off(9, 5) + 0], 0x14);   /* untouched */
    return 0;
}

/* lo == 2 with the realm's +0x1a7 flag set: the first match keeps the
 * requested type, every later match collapses to base type 2. */
int test_territory_retype_band_lo2_collapses_rest(void)
{
    census_setup();
    g_army[3 * ARMY_STRIDE + BAND_RETYPE_FLAG] = 1;
    band_cell(5, 5, 3, 3);             /* first match (lowest offset) */
    band_cell(6, 5, 3, 4);             /* later → collapses to 2      */
    band_cell(7, 5, 3, 5);             /* later → collapses to 2      */

    engine_territory_retype_army_band(3, 5, 0, 2, 0xe);

    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 0], 5);      /* first keeps type arg */
    T_ASSERT_EQ_I(g_cells[cell_off(6, 5) + 0], 2);
    T_ASSERT_EQ_I(g_cells[cell_off(7, 5) + 0], 2);
    return 0;
}

/* lo == 2 but the +0x1a7 flag clear: no collapse — every match takes the
 * requested type. */
int test_territory_retype_band_lo2_no_flag_keeps_all(void)
{
    census_setup();
    /* +0x1a7 left 0 */
    band_cell(5, 5, 3, 3);
    band_cell(6, 5, 3, 4);
    band_cell(7, 5, 3, 5);

    engine_territory_retype_army_band(3, 5, 0, 2, 0xe);

    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 0], 5);
    T_ASSERT_EQ_I(g_cells[cell_off(6, 5) + 0], 5);
    T_ASSERT_EQ_I(g_cells[cell_off(7, 5) + 0], 5);
    return 0;
}

/* No-op when the cell grid or army-record block is unbound. */
int test_territory_retype_band_unbound_is_noop(void)
{
    census_setup();
    band_cell(5, 5, 3, 0x14);
    engine_territory_bind_army_records(NULL);
    engine_territory_retype_army_band(3, 0x16, 0, 0x13, 0x16);  /* must not crash */
    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 0], 0x14);           /* untouched */

    census_setup();
    band_cell(5, 5, 3, 0x14);
    engine_territory_bind_cells(NULL);
    engine_territory_retype_army_band(3, 0x16, 0, 0x13, 0x16);
    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 0], 0x14);
    return 0;
}

/* ── Population-density grader (port of FUN_0044d913) ───────────────
 * Reuse census_setup.  Set the realm's load (+0x250) and census HIGH count
 * (+0x200), place a band-[0x13,0x16] feature cell, grade, then read the tier
 * applied to the cell and the value stored at +0x25c. */

enum { GRADE_LOAD = 0x250, GRADE_HIGH = 0x200, GRADE_VALUE = 0x25c };

static void grade_set(int key, int load, int high)
{
    *(int32_t *)&g_army[key * ARMY_STRIDE + GRADE_LOAD] = load;
    g_army[key * ARMY_STRIDE + GRADE_HIGH] = (uint8_t)high;
}
static int grade_value(int key)
{
    return *(int32_t *)&g_army[key * ARMY_STRIDE + GRADE_VALUE];
}

/* No load → tier 0x13 applied to the realm's band cells, value 10. */
int test_territory_grade_no_load(void)
{
    census_setup();
    grade_set(3, 0, 5);                /* load 0 → density 0 */
    band_cell(0, 3, 3, 0x15);          /* a band cell to receive the tier */

    engine_territory_grade_feature_cells(3);

    T_ASSERT_EQ_I(g_cells[cell_off(0, 3) + 0], 0x13);
    T_ASSERT_EQ_I(grade_value(3), 10);
    return 0;
}

/* The density quotient (load / HIGH-count) selects the tier and value on two
 * parallel ladders. */
int test_territory_grade_density_tiers(void)
{
    census_setup();
    grade_set(3, 10, 5);  band_cell(0, 3, 3, 0x14);   /* density 2  → tier 0x14 */
    grade_set(4, 80, 5);  band_cell(0, 4, 4, 0x14);   /* density 16 → tier 0x15 */
    grade_set(5, 120, 5); band_cell(0, 5, 5, 0x14);   /* density 24 → tier 0x16 */
    grade_set(6, 200, 5); band_cell(0, 6, 6, 0x14);   /* density 40 → tier 0x16 */

    engine_territory_grade_feature_cells(3);
    engine_territory_grade_feature_cells(4);
    engine_territory_grade_feature_cells(5);
    engine_territory_grade_feature_cells(6);

    T_ASSERT_EQ_I(g_cells[cell_off(0, 3) + 0], 0x14);
    T_ASSERT_EQ_I(grade_value(3), 10);
    T_ASSERT_EQ_I(g_cells[cell_off(0, 4) + 0], 0x15);
    T_ASSERT_EQ_I(grade_value(4), 0x14);
    T_ASSERT_EQ_I(g_cells[cell_off(0, 5) + 0], 0x16);
    T_ASSERT_EQ_I(grade_value(5), 0x1e);           /* density 24 → value 0x1e */
    T_ASSERT_EQ_I(g_cells[cell_off(0, 6) + 0], 0x16);
    T_ASSERT_EQ_I(grade_value(6), 0x28);           /* density 40 → value 0x28 */
    return 0;
}

/* Load present but no high-terrain cell → saturated density 1000 (tier 0x16),
 * and the value is forced to 0x28. */
int test_territory_grade_saturated_no_high(void)
{
    census_setup();
    grade_set(2, 50, 0);               /* high == 0 */
    band_cell(0, 2, 2, 0x14);

    engine_territory_grade_feature_cells(2);

    T_ASSERT_EQ_I(g_cells[cell_off(0, 2) + 0], 0x16);
    T_ASSERT_EQ_I(grade_value(2), 0x28);
    return 0;
}

/* No-op when the army-record block is unbound. */
int test_territory_grade_unbound_is_noop(void)
{
    census_setup();
    grade_set(2, 50, 5);
    band_cell(0, 2, 2, 0x14);
    engine_territory_bind_army_records(NULL);
    engine_territory_grade_feature_cells(2);       /* must not crash */
    T_ASSERT_EQ_I(g_cells[cell_off(0, 2) + 0], 0x14);   /* untouched */
    return 0;
}

/* ── Capital-region setup: block stamp + capital placement ──────────
 * (port of FUN_0046ac22 / FUN_00467fd1, the leaves of the 0x468d4f
 * castle-setup subtree).  Reuse the module-level g_cells + cell_off. */

static void cap_setup(void)
{
    memset(g_cells, 0, sizeof g_cells);
    engine_territory_bind_cells(g_cells);
}

/* stamp_block lays a 2×2 descriptor gradient (base + {0,2,1,3}), sets +0 to
 * the type, and rewrites +2: bit0 set, bits 2..4 cleared then OR'd with the
 * flag argument. */
int test_territory_stamp_block_2x2_gradient(void)
{
    cap_setup();
    int o = cell_off(5, 5);
    /* pre-dirty +2 of the top-left cell to prove the |1 / &0xe3 path */
    g_cells[o + 2] = 0xff;

    engine_territory_stamp_block('A', 2, o, 0x10, 0x07);

    /* descriptors: base 'A'=0x41 + gradient {0,2,1,3} over (5,5)(6,5)(5,6)(6,6) */
    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 3], 0x41);
    T_ASSERT_EQ_I(g_cells[cell_off(6, 5) + 3], 0x43);
    T_ASSERT_EQ_I(g_cells[cell_off(5, 6) + 3], 0x42);
    T_ASSERT_EQ_I(g_cells[cell_off(6, 6) + 3], 0x44);
    /* +0 type and +2 flags on every block cell */
    int cells[4] = { cell_off(5,5), cell_off(6,5), cell_off(5,6), cell_off(6,6) };
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_I(g_cells[cells[i] + 0], 0x07);
        T_ASSERT_EQ_I(g_cells[cells[i] + 2] & 1, 1);          /* bit0 set     */
        T_ASSERT_EQ_I(g_cells[cells[i] + 2] & 0x1c, 0x10);    /* flag 0x10 -> bit4 only */
    }
    /* top-left's +2: 0xff |1 = 0xff, &0xe3 = 0xe3, |0x10 = 0xf3 */
    T_ASSERT_EQ_I(g_cells[cell_off(5, 5) + 2], 0xf3);
    return 0;
}

/* The block stays a contiguous 2×2 square — cells outside it are untouched. */
int test_territory_stamp_block_is_square_not_strip(void)
{
    cap_setup();
    engine_territory_stamp_block('A', 2, cell_off(5, 5), 0xc, 0);
    /* (7,5) and (5,7) are just outside the 2×2 footprint */
    T_ASSERT_EQ_I(g_cells[cell_off(7, 5) + 3], 0);
    T_ASSERT_EQ_I(g_cells[cell_off(5, 7) + 3], 0);
    T_ASSERT_EQ_I(g_cells[cell_off(7, 5) + 2], 0);
    return 0;
}

int test_territory_stamp_block_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_stamp_block('A', 2, 0, 0xc, 0);   /* must not crash */
    return 0;
}

/* Mark a cell as capital-eligible for `key`: +7 = key, +1 bit 0x40, +0 = 0. */
static void cap_cell(int col, int row, uint8_t key)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 1] |= 0x40;
    g_cells[o + 0] = 0;
}

/* With ≥4 eligible cells, place_capital stamps the 2×2 capital on the 1st,
 * sets +2 bit0x80 on the 1st and 3rd, returns the 1st's offset, and leaves
 * the cursor at the 4th cell's (col,row). */
int test_territory_place_capital_stamps_first_returns_first(void)
{
    cap_setup();
    cap_cell(10, 0, 3);   /* 1st — offset 80 */
    cap_cell(20, 0, 3);   /* 2nd */
    cap_cell(30, 0, 3);   /* 3rd */
    cap_cell(40, 0, 3);   /* 4th */

    int ret = engine_territory_place_capital(3);

    T_ASSERT_EQ_I(ret, cell_off(10, 0));               /* 1st match's offset */
    /* capital stamped: 2×2 gradient from '/'=0x2f, +2 bits set, +0 = 0 */
    T_ASSERT_EQ_I(g_cells[cell_off(10, 0) + 3], 0x2f); /* '/'+0 */
    T_ASSERT_EQ_I(g_cells[cell_off(11, 0) + 3], 0x31); /* '/'+2 */
    T_ASSERT_EQ_I(g_cells[cell_off(10, 1) + 3], 0x30); /* '/'+1 */
    /* +2 bit0x80 on 1st and 3rd (the 2nd/4th are not OR'd 0x80 by the finder) */
    T_ASSERT_EQ_I(g_cells[cell_off(10, 0) + 2] & 0x80, 0x80);
    T_ASSERT_EQ_I(g_cells[cell_off(30, 0) + 2] & 0x80, 0x80);
    T_ASSERT_EQ_I(g_cells[cell_off(20, 0) + 2] & 0x80, 0);
    T_ASSERT_EQ_I(g_cells[cell_off(40, 0) + 2] & 0x80, 0);
    /* cursor parked at the 4th cell */
    T_ASSERT_EQ_I(engine_territory_cursor_col(), 40);
    T_ASSERT_EQ_I(engine_territory_cursor_row(), 0);
    return 0;
}

/* Fewer than 4 eligible cells → returns 0, cursor untouched. */
int test_territory_place_capital_under_four_returns_zero(void)
{
    cap_setup();
    cap_cell(10, 0, 3);
    cap_cell(20, 0, 3);
    cap_cell(30, 0, 3);            /* only 3 */

    int ret = engine_territory_place_capital(3);
    T_ASSERT_EQ_I(ret, 0);
    return 0;
}

/* Only cells matching all three predicates (key, +1 bit0x40, +0 == 0) count;
 * wrong key / missing flag / nonzero type are skipped. */
int test_territory_place_capital_filters(void)
{
    cap_setup();
    cap_cell(10, 0, 3);                                   /* good */
    int o;
    o = cell_off(11, 0); g_cells[o + 7] = 9; g_cells[o + 1] |= 0x40;  /* wrong key */
    o = cell_off(12, 0); g_cells[o + 7] = 3;                          /* no 0x40 flag */
    o = cell_off(13, 0); g_cells[o + 7] = 3; g_cells[o + 1] |= 0x40; g_cells[o + 0] = 5; /* +0 nonzero */
    cap_cell(14, 0, 3);                                   /* good (2nd) */
    cap_cell(15, 0, 3);                                   /* good (3rd) */
    cap_cell(16, 0, 3);                                   /* good (4th) */

    int ret = engine_territory_place_capital(3);
    T_ASSERT_EQ_I(ret, cell_off(10, 0));
    T_ASSERT_EQ_I(engine_territory_cursor_col(), 16);     /* 4th valid = col 16 */
    return 0;
}

int test_territory_place_capital_unbound_returns_zero(void)
{
    engine_territory_bind_cells(NULL);
    T_ASSERT_EQ_I(engine_territory_place_capital(3), 0);
    return 0;
}

/* ── Capital-region: collect claimed cells (port of FUN_00468c41) ──── */

enum { ARMY_CELL_LIST = 0x80 };
static int32_t army_list(int key, int n)
{
    int32_t v;
    memcpy(&v, &g_army[key * ARMY_STRIDE + ARMY_CELL_LIST + n * 4], 4);
    return v;
}

/* Cells flagged claimed (+1 bit 0x10) for the key are appended to the army's
 * +0x80 i32 list in scan order; each gets +6 ← +3 and +0 ← 0. */
int test_territory_collect_keyed_appends_and_resets(void)
{
    adj_setup();
    int o;
    o = cell_off(3, 0); g_cells[o + 7] = 5; g_cells[o + 1] |= 0x10; g_cells[o + 3] = 0x77; g_cells[o + 0] = 0x42;
    o = cell_off(9, 0); g_cells[o + 7] = 5; g_cells[o + 1] |= 0x10; g_cells[o + 3] = 0x88; g_cells[o + 0] = 0x43;
    o = cell_off(1, 1); g_cells[o + 7] = 5; g_cells[o + 1] |= 0x10; g_cells[o + 3] = 0x99; g_cells[o + 0] = 0x44;

    engine_territory_collect_keyed_cells(5);

    T_ASSERT_EQ_I(army_list(5, 0), cell_off(3, 0));
    T_ASSERT_EQ_I(army_list(5, 1), cell_off(9, 0));
    T_ASSERT_EQ_I(army_list(5, 2), cell_off(1, 1));
    /* +6 ← +3, +0 ← 0 on each collected cell */
    T_ASSERT_EQ_I(g_cells[cell_off(3, 0) + 6], 0x77);
    T_ASSERT_EQ_I(g_cells[cell_off(3, 0) + 0], 0);
    T_ASSERT_EQ_I(g_cells[cell_off(9, 0) + 6], 0x88);
    T_ASSERT_EQ_I(g_cells[cell_off(1, 1) + 0], 0);
    return 0;
}

/* Only cells with BOTH the 0x10 flag and the matching key are collected. */
int test_territory_collect_keyed_filters(void)
{
    adj_setup();
    int o;
    o = cell_off(2, 0); g_cells[o + 7] = 5; g_cells[o + 1] |= 0x10;   /* good (1st) */
    o = cell_off(3, 0); g_cells[o + 7] = 9; g_cells[o + 1] |= 0x10;   /* wrong key  */
    o = cell_off(4, 0); g_cells[o + 7] = 5;                            /* no flag    */
    o = cell_off(5, 0); g_cells[o + 7] = 5; g_cells[o + 1] |= 0x10;   /* good (2nd) */

    engine_territory_collect_keyed_cells(5);

    T_ASSERT_EQ_I(army_list(5, 0), cell_off(2, 0));
    T_ASSERT_EQ_I(army_list(5, 1), cell_off(5, 0));
    T_ASSERT_EQ_I(army_list(5, 2), 0);              /* list ends (pre-zeroed) */
    /* the wrong-key cell stays untouched (still owned by 9, type unset) */
    T_ASSERT_EQ_I(g_cells[cell_off(3, 0) + 0], 0);  /* never written a type → 0 */
    return 0;
}

int test_territory_collect_keyed_unbound_is_noop(void)
{
    adj_setup();
    engine_territory_bind_cells(NULL);
    engine_territory_collect_keyed_cells(5);        /* must not crash */
    adj_setup();
    engine_territory_bind_army_records(NULL);
    engine_territory_collect_keyed_cells(5);        /* must not crash */
    return 0;
}

/* ── Capital-region: mark feature cells (port of FUN_00468e61) ──────── */

enum { FEAT_A_FLAG = 0x2ad, FEAT_A_OFF = 0x2b0,
       FEAT_B_FLAG = 0x2dd, FEAT_B_OFF = 0x2e0,
       FEAT_C_FLAG = 0x295, FEAT_C_OFF = 0x298 };
static int32_t army_i32(int key, int field)
{
    int32_t v;
    memcpy(&v, &g_army[key * ARMY_STRIDE + field], 4);
    return v;
}

/* Mark cell (col,row) as an in-capital cell of `key` with descriptor `desc`:
 * +7 = key, +2 = 0xc (bits 2,3 set / bit4 clear), +3 = desc. */
static void feat_cell(int col, int row, uint8_t key, uint8_t desc)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 2] = 0xc;
    g_cells[o + 3] = desc;
}

/* Each descriptor routes to its slot, stamping the slot flag+offset, the
 * cell's +0 type, and +2 bit 0x80. */
int test_territory_mark_features_routes_by_descriptor(void)
{
    adj_setup();
    feat_cell(2, 0, 4, 0x1e);   /* → slot A */
    feat_cell(5, 0, 4, 0x14);   /* → slot C */

    engine_territory_mark_capital_features(4);

    T_ASSERT_EQ_I(g_army[4 * ARMY_STRIDE + FEAT_A_FLAG], 1);
    T_ASSERT_EQ_I(army_i32(4, FEAT_A_OFF), cell_off(2, 0));
    T_ASSERT_EQ_I(g_cells[cell_off(2, 0) + 0], 1);
    T_ASSERT_EQ_I(g_cells[cell_off(2, 0) + 2] & 0x80, 0x80);

    T_ASSERT_EQ_I(g_army[4 * ARMY_STRIDE + FEAT_C_FLAG], 1);
    T_ASSERT_EQ_I(army_i32(4, FEAT_C_OFF), cell_off(5, 0));
    T_ASSERT_EQ_I(g_cells[cell_off(5, 0) + 0], 0x0a);
    return 0;
}

/* A type-0 (descriptor 0) cell fills slot B — but only while slot A is unset.
 * A 0x1e cell earlier in scan order suppresses the type-0 slot. */
int test_territory_mark_features_type0_gated_by_slotA(void)
{
    /* case 1: no 0x1e — the descriptor-0 cell takes slot B */
    adj_setup();
    feat_cell(3, 0, 6, 0);
    engine_territory_mark_capital_features(6);
    T_ASSERT_EQ_I(g_army[6 * ARMY_STRIDE + FEAT_B_FLAG], 1);
    T_ASSERT_EQ_I(army_i32(6, FEAT_B_OFF), cell_off(3, 0));
    T_ASSERT_EQ_I(g_cells[cell_off(3, 0) + 0], 4);

    /* case 2: a 0x1e cell precedes the type-0 cell → slot B suppressed */
    adj_setup();
    feat_cell(1, 0, 6, 0x1e);   /* lower offset → seen first, sets slot A */
    feat_cell(8, 0, 6, 0);      /* gated off */
    engine_territory_mark_capital_features(6);
    T_ASSERT_EQ_I(g_army[6 * ARMY_STRIDE + FEAT_A_FLAG], 1);
    T_ASSERT_EQ_I(g_army[6 * ARMY_STRIDE + FEAT_B_FLAG], 0);   /* never set */
    T_ASSERT_EQ_I(g_cells[cell_off(8, 0) + 0], 0);            /* type untouched */
    return 0;
}

/* Cells outside the capital region (+2 != 0xc) or of the wrong key are
 * skipped. */
int test_territory_mark_features_filters(void)
{
    adj_setup();
    int o;
    o = cell_off(2, 0); g_cells[o + 7] = 4; g_cells[o + 2] = 0x08; g_cells[o + 3] = 0x1e; /* bits != 0xc */
    o = cell_off(3, 0); g_cells[o + 7] = 9; g_cells[o + 2] = 0x0c; g_cells[o + 3] = 0x1e; /* wrong key */

    engine_territory_mark_capital_features(4);
    T_ASSERT_EQ_I(g_army[4 * ARMY_STRIDE + FEAT_A_FLAG], 0);   /* nothing marked */
    return 0;
}

int test_territory_mark_features_unbound_is_noop(void)
{
    adj_setup();
    engine_territory_bind_cells(NULL);
    engine_territory_mark_capital_features(4);
    adj_setup();
    engine_territory_bind_army_records(NULL);
    engine_territory_mark_capital_features(4);
    return 0;
}

/* ── Capital-region: classify neighbour + coastal port ──────────────
 * (ports of FUN_0046c147 / FUN_0046902a). */

enum { PORT_FLAG = 0x2c5, PORT_OFF = 0x2c8 };

/* A neighbour whose +1 carries bit 0x20 classifies as 0x20 (coast); bit 1 is
 * always dropped. */
int test_territory_classify_coast_neighbour(void)
{
    adj_setup();
    int c = cell_off(5, 5);
    int n = cell_off(5, 4);                  /* north neighbour */
    g_cells[n + 1] = 0x22;                   /* coast bit + bit1 (dropped) */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(0, 5, 5, c), 0x20);
    return 0;
}

/* The 8↔0x10 remap keyed on the neighbour's +2 bits 2..4. */
int test_territory_classify_8_and_10_remap(void)
{
    adj_setup();
    int c = cell_off(5, 5);
    int e = cell_off(6, 5);                  /* east neighbour */

    g_cells[e + 1] = 8; g_cells[e + 2] = 0;          /* 8, bits!=4 → 0x10, then bits!=8 → 0 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(2, 5, 5, c), 0);

    g_cells[e + 1] = 8; g_cells[e + 2] = 4;          /* 8, bits==4 → stays 8 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(2, 5, 5, c), 8);

    g_cells[e + 1] = 0x10; g_cells[e + 2] = 8;       /* 0x10, bits==8 → stays 0x10 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(2, 5, 5, c), 0x10);

    g_cells[e + 1] = 0x10; g_cells[e + 2] = 0;       /* 0x10, bits!=8 → 0 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(2, 5, 5, c), 0);
    return 0;
}

/* Off-grid neighbours read as 0 (no out-of-bounds access). */
int test_territory_classify_edges_are_zero(void)
{
    adj_setup();
    /* poke a coast flag everywhere so a missing guard would surface */
    memset(g_cells, 0x20, sizeof g_cells);
    engine_territory_bind_cells(g_cells);
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(0, 0, 0, cell_off(0, 0)), 0);     /* N of row 0 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(6, 0, 0, cell_off(0, 0)), 0);     /* W of col 0 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(2, 0x3f, 0, cell_off(0x3f, 0)), 0); /* E of col 63 */
    T_ASSERT_EQ_I(engine_territory_classify_border_neighbour(4, 0, 0x3f, cell_off(0, 0x3f)), 0); /* S of row 63 */
    return 0;
}

/* Set up an empty key-owned cell with a same-key coast neighbour to its east. */
static void port_candidate(int col, int row, uint8_t key)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 1] = 0;
    int e = cell_off(col + 1, row);          /* east coast neighbour */
    g_cells[e + 7] = key;
    g_cells[e + 1] = 0x20;
}

/* A single qualifying empty cell becomes the port (type 7, descriptor 0x0a,
 * +1 bit 0x80, +2 = 0xc|0x80) and is recorded in the army row. */
int test_territory_port_places_single_candidate(void)
{
    adj_setup();
    port_candidate(10, 10, 4);
    engine_territory_place_capital_port(4);

    int o = cell_off(10, 10);
    T_ASSERT_EQ_I(g_cells[o + 0], 7);
    T_ASSERT_EQ_I(g_cells[o + 3], 0x0a);
    T_ASSERT_EQ_I(g_cells[o + 1] & 0x80, 0x80);
    T_ASSERT_EQ_I(g_cells[o + 2] & 0x1c, 0xc);
    T_ASSERT_EQ_I(g_cells[o + 2] & 0x80, 0x80);
    T_ASSERT_EQ_I(g_army[4 * ARMY_STRIDE + PORT_FLAG], 1);
    T_ASSERT_EQ_I(army_i32(4, PORT_OFF), o);
    return 0;
}

/* The candidate nearest (Chebyshev) to the capital cursor wins.  Seed the
 * cursor by running place_capital (its 4th eligible cell → cursor). */
int test_territory_port_picks_nearest_to_cursor(void)
{
    adj_setup();
    /* 4 capital-eligible cells → cursor parks at the 4th, (23,0) */
    for (int col = 20; col <= 23; col++) {
        int o = cell_off(col, 0);
        g_cells[o + 7] = 4; g_cells[o + 1] |= 0x40; g_cells[o + 0] = 0;
    }
    T_ASSERT_EQ_I(engine_territory_place_capital(4), cell_off(20, 0));
    T_ASSERT_EQ_I(engine_territory_cursor_col(), 23);
    T_ASSERT_EQ_I(engine_territory_cursor_row(), 0);

    port_candidate(30, 5, 4);    /* dist to (23,0) = max(7,5)  = 7  */
    port_candidate(10, 30, 4);   /* dist to (23,0) = max(13,30)= 30 */

    engine_territory_place_capital_port(4);

    T_ASSERT_EQ_I(g_cells[cell_off(30, 5) + 0], 7);    /* near → port */
    T_ASSERT_EQ_I(g_cells[cell_off(10, 30) + 0], 0);   /* far → untouched */
    T_ASSERT_EQ_I(army_i32(4, PORT_OFF), cell_off(30, 5));
    return 0;
}

/* No qualifying cell (neighbour not same-key, or not coast) → nothing placed. */
int test_territory_port_no_candidate_noop(void)
{
    adj_setup();
    /* empty key cell, but its east neighbour is a different key */
    int o = cell_off(10, 10); g_cells[o + 7] = 4; g_cells[o + 1] = 0;
    int e = cell_off(11, 10); g_cells[e + 7] = 9; g_cells[e + 1] = 0x20;

    engine_territory_place_capital_port(4);
    T_ASSERT_EQ_I(g_cells[o + 0], 0);
    T_ASSERT_EQ_I(g_army[4 * ARMY_STRIDE + PORT_FLAG], 0);
    return 0;
}

int test_territory_port_unbound_is_noop(void)
{
    adj_setup();
    engine_territory_bind_cells(NULL);
    engine_territory_place_capital_port(4);
    adj_setup();
    engine_territory_bind_army_records(NULL);
    engine_territory_place_capital_port(4);
    return 0;
}

/* ── Capital-region: secondary cluster (port of FUN_00468121) ───────── */

/* Mark cell (col,row) as a secondary candidate of `key`: +7=key, +1 bit 0x80,
 * +0 = 0, +3 = a distinct descriptor to verify the +6 copy. */
static void sec_cell(int col, int row, uint8_t key, uint8_t desc)
{
    int o = cell_off(col, row);
    g_cells[o + 7] = key;
    g_cells[o + 1] |= 0x80;
    g_cells[o + 0] = 0;
    g_cells[o + 3] = desc;
}

/* With ≥4 candidates: the first four get +0 ← 0x14 and +6 ← +3; the 1st gets
 * +2 bit 0x80 and seats the cursor; the 5th (past the 4th return) is left
 * untouched; the return value is the 1st match's offset. */
int test_territory_secondary_marks_first_four(void)
{
    cap_setup();
    sec_cell(1, 0, 7, 0xa1);
    sec_cell(2, 0, 7, 0xa2);
    sec_cell(3, 0, 7, 0xa3);
    sec_cell(4, 0, 7, 0xa4);
    sec_cell(5, 0, 7, 0xa5);   /* 5th — never reached */

    int ret = engine_territory_place_secondary(7);

    T_ASSERT_EQ_I(ret, cell_off(1, 0));
    for (int c = 1; c <= 4; c++) {
        T_ASSERT_EQ_I(g_cells[cell_off(c, 0) + 0], 0x14);
        T_ASSERT_EQ_I(g_cells[cell_off(c, 0) + 6], 0xa0 + c);   /* +6 ← +3 */
    }
    T_ASSERT_EQ_I(g_cells[cell_off(1, 0) + 2] & 0x80, 0x80);    /* 1st only */
    T_ASSERT_EQ_I(g_cells[cell_off(2, 0) + 2] & 0x80, 0);
    T_ASSERT_EQ_I(g_cells[cell_off(5, 0) + 0], 0);              /* untouched */
    /* cursor on the 1st match */
    T_ASSERT_EQ_I(engine_territory_cursor_col(), 1);
    T_ASSERT_EQ_I(engine_territory_cursor_row(), 0);
    return 0;
}

/* Fewer than four → returns 0, but found cells are still marked and the
 * cursor is seated on the 1st. */
int test_territory_secondary_under_four_marks_partial(void)
{
    cap_setup();
    sec_cell(8, 2, 7, 0xb0);
    sec_cell(9, 2, 7, 0xb1);

    int ret = engine_territory_place_secondary(7);
    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_cells[cell_off(8, 2) + 0], 0x14);
    T_ASSERT_EQ_I(g_cells[cell_off(9, 2) + 0], 0x14);
    T_ASSERT_EQ_I(engine_territory_cursor_col(), 8);
    T_ASSERT_EQ_I(engine_territory_cursor_row(), 2);
    return 0;
}

/* All three predicates required (key, +1 bit 0x80, +0 == 0). */
int test_territory_secondary_filters(void)
{
    cap_setup();
    int o;
    o = cell_off(1, 0); g_cells[o + 7] = 9; g_cells[o + 1] |= 0x80;   /* wrong key */
    o = cell_off(2, 0); g_cells[o + 7] = 7;                            /* no 0x80   */
    o = cell_off(3, 0); g_cells[o + 7] = 7; g_cells[o + 1] |= 0x80; g_cells[o + 0] = 4; /* +0 != 0 */

    int ret = engine_territory_place_secondary(7);
    T_ASSERT_EQ_I(ret, 0);
    T_ASSERT_EQ_I(g_cells[cell_off(3, 0) + 0], 4);   /* unchanged */
    return 0;
}

int test_territory_secondary_unbound_returns_zero(void)
{
    engine_territory_bind_cells(NULL);
    T_ASSERT_EQ_I(engine_territory_place_secondary(7), 0);
    return 0;
}

/* ── Capital-region orchestrator (port of FUN_00468d4f) ─────────────── */

enum { CAP_COL = 0x6c, CAP_ROW = 0x6d, CAP_OFF = 0x70,
       SEC_COL = 0x74, SEC_ROW = 0x75, SEC_OFF = 0x78 };

/* End-to-end: army 1 gets a capital (4 eligible cells) and a secondary
 * cluster; the orchestrator stashes both offsets + cursor col/rows into the
 * army-1 record.  Army 2 has no eligible cells → only +0x70 (=0) is written
 * and the rest of its row is left alone.  The loop stops at s_slot_0056d5dc,
 * so army 3 is never touched. */
int test_territory_setup_capitals_end_to_end(void)
{
    adj_setup();

    /* army 1 capital: 4 eligible cells at row 5, cols 5..8 → 4th seats cursor */
    for (int col = 5; col <= 8; col++) {
        int o = cell_off(col, 5);
        g_cells[o + 7] = 1; g_cells[o + 1] |= 0x40; g_cells[o + 0] = 0;
    }
    /* army 1 secondary: 4 cells (+1 bit 0x80, +0 == 0) at row 10, cols 10..13 */
    for (int col = 10; col <= 13; col++) {
        int o = cell_off(col, 10);
        g_cells[o + 7] = 1; g_cells[o + 1] |= 0x80; g_cells[o + 0] = 0;
    }
    /* sentinel in army 3's capital-offset field — must survive (loop stops at 2) */
    int32_t sentinel = 0x0badf00d;
    memcpy(&g_army[3 * ARMY_STRIDE + CAP_OFF], &sentinel, 4);

    s_slot_0056d5dc = 2;
    engine_territory_setup_capitals();

    /* army 1: capital offset + cursor (the 4th eligible cell, (8,5)) */
    T_ASSERT_EQ_I(army_i32(1, CAP_OFF), cell_off(5, 5));
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + CAP_COL], 8);
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + CAP_ROW], 5);
    /* army 1: secondary offset + cursor (the 1st secondary cell, (10,10)) */
    T_ASSERT_EQ_I(army_i32(1, SEC_OFF), cell_off(10, 10));
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + SEC_COL], 10);
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + SEC_ROW], 10);

    /* army 2: no capital → +0x70 == 0, secondary fields untouched (0) */
    T_ASSERT_EQ_I(army_i32(2, CAP_OFF), 0);
    T_ASSERT_EQ_I(army_i32(2, SEC_OFF), 0);
    T_ASSERT_EQ_I(g_army[2 * ARMY_STRIDE + CAP_COL], 0);

    /* army 3: never processed → sentinel intact */
    T_ASSERT_EQ_U((uint32_t)army_i32(3, CAP_OFF), 0x0badf00d);
    return 0;
}

/* The capital cursor (capital finder's 4th cell) survives through the collect/
 * mark/port passes unchanged, then place_secondary overwrites it — so +0x6c/6d
 * (capital cursor) and +0x74/75 (secondary cursor) differ. */
int test_territory_setup_capitals_cursor_handoff(void)
{
    adj_setup();
    for (int col = 5; col <= 8; col++) {       /* capital → cursor (8,5) */
        int o = cell_off(col, 5);
        g_cells[o + 7] = 1; g_cells[o + 1] |= 0x40;
    }
    int o = cell_off(20, 20);                  /* a single secondary cell → cursor (20,20) */
    g_cells[o + 7] = 1; g_cells[o + 1] |= 0x80;

    s_slot_0056d5dc = 1;
    engine_territory_setup_capitals();

    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + CAP_COL], 8);    /* capital cursor */
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + CAP_ROW], 5);
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + SEC_COL], 20);   /* secondary cursor */
    T_ASSERT_EQ_I(g_army[1 * ARMY_STRIDE + SEC_ROW], 20);
    T_ASSERT_EQ_I(army_i32(1, SEC_OFF), 0);                 /* <4 secondary → 0 */
    return 0;
}

int test_territory_setup_capitals_unbound_is_noop(void)
{
    adj_setup();
    s_slot_0056d5dc = 1;
    engine_territory_bind_cells(NULL);
    engine_territory_setup_capitals();
    adj_setup();
    engine_territory_bind_army_records(NULL);
    engine_territory_setup_capitals();
    return 0;
}

/* ─── strategic-map record loader (FUN_00467770) ──────────────────────── */

#include "engine_roster.h"          /* g_player_roster, g_player_roster_count */

#define MAP_REC_BYTES 0x6000         /* six 0x1000-byte cell-linear planes      */
_Alignas(int32_t) static uint8_t g_maprec[MAP_REC_BYTES];
static engine_category_buckets_state_t g_mapbuckets;

/* Cell-linear index (row*0x40 + col) — the loader's running offset. */
static int cell_off_lin(int col, int row) { return row * CELL_GRID + col; }

/* Write plane `p` (0..5) of cell-linear index `lin` in the record buffer. */
static void rec_plane(int lin, int p, uint8_t v) { g_maprec[lin + p * 0x1000] = v; }

/* Bind a zeroed grid + record + buckets and poison everything the loader is
 * supposed to clear (counts, roster, bucket table) so the clears are
 * observable. */
static void map_setup(void)
{
    memset(g_cells,   0, sizeof g_cells);
    memset(g_maprec,  0, sizeof g_maprec);
    memset(&g_mapbuckets, 0xaa, sizeof g_mapbuckets);
    memset(g_player_roster, 0xaa, sizeof g_player_roster);
    engine_territory_bind_cells(g_cells);
    engine_territory_bind_strat_map_record(g_maprec);
    engine_territory_bind_category_buckets(&g_mapbuckets);
    s_slot_0056d5dc       = 0x55;
    g_player_roster_count = 0x55;
}

/* Plain (plane-4 == 0) cells copy planes 0,1,2,3,5 into cell bytes +1,+2,
 * +3,+4,+7 and write nothing to bucket/roster. */
int test_strat_map_unpacks_planes_into_cells(void)
{
    map_setup();
    int lin = 5;                              /* cell (col=5,row=0) */
    rec_plane(lin, 0, 0x11);
    rec_plane(lin, 1, 0x22);
    rec_plane(lin, 2, 0x33);
    rec_plane(lin, 3, 0x44);
    rec_plane(lin, 4, 0x00);                  /* plane4 == 0 → no marker */
    rec_plane(lin, 5, 0x03);
    engine_territory_load_strat_map();

    int o = cell_off(5, 0);
    T_ASSERT_EQ_I(g_cells[o + 1], 0x11);
    T_ASSERT_EQ_I(g_cells[o + 2], 0x22);
    T_ASSERT_EQ_I(g_cells[o + 3], 0x33);
    T_ASSERT_EQ_I(g_cells[o + 4], 0x44);
    T_ASSERT_EQ_I(g_cells[o + 7], 0x03);
    /* +0, +5, +6 untouched by the loader */
    T_ASSERT_EQ_I(g_cells[o + 0], 0);
    T_ASSERT_EQ_I(g_cells[o + 5], 0);
    /* plane4 == 0 → bucket row 0 + roster stay clear/empty */
    T_ASSERT_EQ_I(g_mapbuckets.buckets[0][0], 0);
    T_ASSERT_EQ_U(g_player_roster_count, 0u);
    return 0;
}

/* Player/army count = max plane-5 value across cells, but only counting the
 * 0..0x10 player band (plane5 > 0x10 markers never raise it). */
int test_strat_map_player_count_is_max_plane5_in_band(void)
{
    map_setup();
    rec_plane(cell_off_lin(1, 0), 5, 0x03);
    rec_plane(cell_off_lin(2, 0), 5, 0x09);   /* the in-band max          */
    rec_plane(cell_off_lin(3, 0), 5, 0x05);
    rec_plane(cell_off_lin(4, 0), 5, 0x12);   /* > 0x10 → must NOT count   */
    engine_territory_load_strat_map();

    T_ASSERT_EQ_U(s_slot_0056d5dc, 9u);
    T_ASSERT_EQ_I(g_cells[cell_off(4, 0) + 7], 0x12);  /* still stored on the cell */
    return 0;
}

/* plane4 != 0 routes the cell to a category bucket (plane0 bit 0x40) or the
 * player roster (plane0 bit 0x80); 0x40 wins when both are set. */
int test_strat_map_seeds_buckets_and_roster(void)
{
    map_setup();
    /* bucket: value=plane5=7, bucket=plane4=2 (→ row 1) */
    rec_plane(cell_off_lin(6, 0), 0, 0x40);
    rec_plane(cell_off_lin(6, 0), 4, 0x02);
    rec_plane(cell_off_lin(6, 0), 5, 0x07);
    /* roster: army_id=plane5=5, seat=plane4=3 */
    rec_plane(cell_off_lin(7, 0), 0, 0x80);
    rec_plane(cell_off_lin(7, 0), 4, 0x03);
    rec_plane(cell_off_lin(7, 0), 5, 0x05);
    /* both bits set → 0x40 wins → bucket (value=9, bucket=4 → row 3), NOT roster */
    rec_plane(cell_off_lin(9, 0), 0, 0xC0);
    rec_plane(cell_off_lin(9, 0), 4, 0x04);
    rec_plane(cell_off_lin(9, 0), 5, 0x09);
    engine_territory_load_strat_map();

    T_ASSERT_EQ_I(g_mapbuckets.buckets[1][0], 0x07);   /* bucket 2 */
    T_ASSERT_EQ_I(g_mapbuckets.buckets[3][0], 0x09);   /* bucket 4 (the 0x40-wins cell) */
    T_ASSERT_EQ_I(g_player_roster[3 * 2 + 0], 0x05);   /* roster pair 3 = {army_id, seat} */
    T_ASSERT_EQ_I(g_player_roster[3 * 2 + 1], 0x03);
    T_ASSERT_EQ_U(g_player_roster_count, 1u);           /* only the 0x80 cell seated */
    return 0;
}

/* The loader resets the counts and clears the bucket table + roster up front
 * (poisoned 0x55/0xaa state must be gone), independent of any cell data. */
int test_strat_map_clears_counts_and_tables(void)
{
    map_setup();                              /* counts poisoned to 0x55 */
    engine_territory_load_strat_map();        /* record is all-zero      */

    T_ASSERT_EQ_U(s_slot_0056d5dc, 0u);
    T_ASSERT_EQ_U(g_player_roster_count, 0u);
    T_ASSERT_EQ_I(g_mapbuckets.buckets[2][5], 0);   /* a poisoned slot, now cleared */
    T_ASSERT_EQ_I(g_mapbuckets.digest[4], 0);
    T_ASSERT_EQ_I(g_player_roster[5 * 2], 0);       /* a poisoned pair, now cleared */
    return 0;
}

/* Unbound grid or record skips the unpack loop but still performs the
 * up-front clears; an unbound bucket table must not crash the append path. */
int test_strat_map_unbound_is_noop(void)
{
    /* cells NULL → loop skipped, clears still run */
    map_setup();
    engine_territory_bind_cells(NULL);
    engine_territory_load_strat_map();
    T_ASSERT_EQ_U(s_slot_0056d5dc, 0u);
    T_ASSERT_EQ_U(g_player_roster_count, 0u);

    /* record NULL → same */
    map_setup();
    engine_territory_bind_strat_map_record(NULL);
    engine_territory_load_strat_map();
    T_ASSERT_EQ_U(s_slot_0056d5dc, 0u);

    /* buckets NULL → the 0x40 append branch must be a no-op, not a crash */
    map_setup();
    engine_territory_bind_category_buckets(NULL);
    rec_plane(cell_off_lin(6, 0), 0, 0x40);
    rec_plane(cell_off_lin(6, 0), 4, 0x02);
    rec_plane(cell_off_lin(6, 0), 5, 0x07);
    engine_territory_load_strat_map();
    T_ASSERT_EQ_I(g_cells[cell_off(6, 0) + 7], 0x07);   /* unpack still ran */
    return 0;
}

/* ─── overworld-grid setup orchestrator (FUN_004676e0) ────────────────── */

/* End-to-end with only the cell grid + record + buckets bound (the army /
 * terrain / roster passes self-skip): the six clear passes wipe cell bytes
 * 0..5 (but not +6/+7), load_strat_map then writes +1..+4/+7 and the player
 * count, and the digest is refreshed from the seeded buckets.  rng = NULL so
 * the two grid-randomiser passes no-op. */
int test_setup_overworld_end_to_end(void)
{
    map_setup();                       /* cells zeroed + bound, record zeroed, buckets bound */
    memset(g_cells, 0xff, sizeof g_cells);   /* poison every cell byte */
    engine_territory_bind_cells(g_cells);

    int lin = 10;                      /* cell (col=10,row=0): a bucket-route feature */
    rec_plane(lin, 0, 0x40);
    rec_plane(lin, 4, 0x02);           /* bucket index 2 */
    rec_plane(lin, 5, 0x07);           /* value 7, also the in-band count */

    engine_territory_setup_overworld(NULL);

    int o = cell_off(10, 0);
    T_ASSERT_EQ_I(g_cells[o + 0], 0x00);   /* byte 0 cleared by clear_cell_field(0) */
    T_ASSERT_EQ_I(g_cells[o + 5], 0x00);   /* byte 5 cleared */
    T_ASSERT_EQ_I(g_cells[o + 6], 0xff);   /* byte 6 never cleared → survives poison */
    T_ASSERT_EQ_I(g_cells[o + 1], 0x40);   /* plane0 loaded */
    T_ASSERT_EQ_I(g_cells[o + 7], 0x07);   /* plane5 loaded */
    T_ASSERT_EQ_U(s_slot_0056d5dc, 7u);    /* player count = max in-band plane5 */
    T_ASSERT_EQ_I(g_mapbuckets.buckets[1][0], 0x07);   /* bucket 2 seeded by the load */
    T_ASSERT_EQ_I(g_mapbuckets.digest[1], 0x07);       /* digest refreshed (FUN_004291b3) */
    return 0;
}

/* The single-LFSR rng is threaded to the grid passes: with a sentinel tile
 * and a non-NULL rng the randomiser re-rolls it and advances the state; with
 * NULL the pass is skipped and the sentinel survives. */
int test_setup_overworld_threads_rng(void)
{
    map_setup();
    tiles_setup(0x12345);                  /* background the grid passes ignore */
    *tile_at(3, 3) = TILE_SENTINEL;        /* the one re-rollable tile          */

    const uint32_t seed = 0x34518632u;
    engine_rng_single_in_t rng = { .state = seed };
    engine_territory_setup_overworld(&rng);
    T_ASSERT((uint32_t)*tile_at(3, 3) != (uint32_t)TILE_SENTINEL);   /* re-rolled */
    T_ASSERT(rng.state != seed);                                     /* advanced  */

    /* NULL rng → randomiser pass skipped, sentinel survives. */
    tiles_setup(0x12345);
    *tile_at(3, 3) = TILE_SENTINEL;
    map_setup();
    tiles_setup(0x12345);
    *tile_at(3, 3) = TILE_SENTINEL;
    engine_territory_setup_overworld(NULL);
    T_ASSERT_EQ_U((uint32_t)*tile_at(3, 3), (uint32_t)TILE_SENTINEL);
    return 0;
}

/* Nothing bound → every pass self-skips, no crash, and the counts the loader
 * resets are still zeroed. */
int test_setup_overworld_all_unbound_is_noop(void)
{
    engine_territory_bind_cells(NULL);
    engine_territory_bind_strat_map_record(NULL);
    engine_territory_bind_category_buckets(NULL);
    engine_territory_bind_terrain_tiles(NULL);
    engine_territory_bind_army_records(NULL);
    s_slot_0056d5dc = 0x33;
    engine_territory_setup_overworld(NULL);
    T_ASSERT_EQ_U(s_slot_0056d5dc, 0u);
    return 0;
}

/* ── Recenter-on-tile / -on-capital (ports of FUN_0043278b / 00432746) ── */

static engine_dirty_rect_state_t g_recenter_dirty;
static int g_recenter_cursor_flag;

/* Fresh tile grid (all `fill`), normal view mode, scroll cleared, redraw
 * sinks bound and zeroed. */
static void recenter_setup(uint32_t fill)
{
    tiles_setup(fill);
    s_slot_0057cb18 = 0;          /* normal view mode      */
    s_slot_005651b4 = 0;          /* scroll col            */
    s_slot_005651b8 = 0;          /* scroll row            */
    s_slot_0057c93c = 0;          /* selected army         */
    g_recenter_dirty = (engine_dirty_rect_state_t){0};
    g_recenter_cursor_flag = 0;
    engine_territory_set_dirty_state(&g_recenter_dirty);
    engine_territory_bind_cursor_cache_flag(&g_recenter_cursor_flag);
}

static void recenter_teardown(void)
{
    engine_territory_set_dirty_state(NULL);
    engine_territory_bind_cursor_cache_flag(NULL);
}

/* A hit centres the scroll on (col-4, evenrow-12), latches both redraw
 * flags, and returns 2. */
int test_territory_recenter_hit_sets_scroll_and_flags(void)
{
    recenter_setup(0);
    *tile_at(40, 30) = 0x12345;

    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 2);
    T_ASSERT_EQ_U(s_slot_005651b4, 26u);          /* 30 - 4            */
    T_ASSERT_EQ_U(s_slot_005651b8, 28u);          /* (40 & ~1) - 12    */
    T_ASSERT_EQ_I(g_recenter_dirty.below_status_redraw, 1);
    T_ASSERT_EQ_I(g_recenter_cursor_flag, 1);
    recenter_teardown();
    return 0;
}

/* The row is floored to even before the -12: an odd row lands on the same
 * scroll as its even predecessor. */
int test_territory_recenter_floors_odd_row(void)
{
    recenter_setup(0);
    *tile_at(41, 30) = 0x12345;
    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 2);
    T_ASSERT_EQ_U(s_slot_005651b8, 28u);          /* (41 & ~1) - 12 = 28 */
    recenter_teardown();
    return 0;
}

/* Negative scroll targets are clamped up to 0 (the count-independent
 * lower clamp). */
int test_territory_recenter_clamps_negative_to_zero(void)
{
    recenter_setup(0);
    *tile_at(2, 1) = 0x12345;     /* col-4 = -3, row-12 = -10 */
    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 2);
    T_ASSERT_EQ_U(s_slot_005651b4, 0u);
    T_ASSERT_EQ_U(s_slot_005651b8, 0u);
    recenter_teardown();
    return 0;
}

/* When the view mode is not normal the scroll assignment is gated off,
 * but the clamp still runs and the flags still latch (return 2). */
int test_territory_recenter_view_mode_gates_scroll_only(void)
{
    recenter_setup(0);
    s_slot_0057cb18 = 1;          /* non-normal view */
    s_slot_005651b4 = 7;          /* pre-existing scroll preserved */
    s_slot_005651b8 = 9;
    *tile_at(40, 30) = 0x12345;

    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 2);
    T_ASSERT_EQ_U(s_slot_005651b4, 7u);           /* unchanged */
    T_ASSERT_EQ_U(s_slot_005651b8, 9u);
    T_ASSERT_EQ_I(g_recenter_dirty.below_status_redraw, 1);
    T_ASSERT_EQ_I(g_recenter_cursor_flag, 1);
    recenter_teardown();
    return 0;
}

/* Row-major scan: the first matching cell (lowest row, then lowest col)
 * wins when the value appears twice. */
int test_territory_recenter_takes_first_match(void)
{
    recenter_setup(0);
    *tile_at(50, 50) = 0x12345;
    *tile_at(20, 30) = 0x12345;   /* earlier in row-major order */
    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 2);
    T_ASSERT_EQ_U(s_slot_005651b4, 26u);          /* col 30 - 4 */
    T_ASSERT_EQ_U(s_slot_005651b8, 8u);           /* row 20 - 12 */
    recenter_teardown();
    return 0;
}

/* No match → 0, nothing written. */
int test_territory_recenter_miss_returns_zero(void)
{
    recenter_setup(0x999);        /* fill non-matching */
    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 0);
    T_ASSERT_EQ_U(s_slot_005651b4, 0u);
    T_ASSERT_EQ_U(s_slot_005651b8, 0u);
    T_ASSERT_EQ_I(g_recenter_dirty.below_status_redraw, 0);
    T_ASSERT_EQ_I(g_recenter_cursor_flag, 0);
    recenter_teardown();
    return 0;
}

int test_territory_recenter_unbound_tiles_returns_zero(void)
{
    engine_territory_bind_terrain_tiles(NULL);
    T_ASSERT_EQ_I(engine_territory_recenter_on_tile(0x12345), 0);   /* no crash */
    return 0;
}

/* recenter_on_capital reads army record +0x70, recenters on that tile
 * value, and latches the selected-army global. */
int test_territory_recenter_capital_centres_and_selects(void)
{
    recenter_setup(0);
    memset(g_army, 0, sizeof g_army);
    engine_territory_bind_army_records(g_army);
    int32_t cap = 0xABCDE;
    memcpy(&g_army[1 * ARMY_STRIDE + CAP_OFF], &cap, 4);   /* +0x70 */
    *tile_at(40, 30) = cap;

    engine_territory_recenter_on_capital(1);
    T_ASSERT_EQ_U(s_slot_005651b4, 26u);
    T_ASSERT_EQ_U(s_slot_005651b8, 28u);
    T_ASSERT_EQ_U(s_slot_0057c93c, 1u);           /* selected army */
    recenter_teardown();
    return 0;
}

/* A zero capital offset → no recenter, no selection. */
int test_territory_recenter_capital_zero_offset_is_noop(void)
{
    recenter_setup(0);
    memset(g_army, 0, sizeof g_army);
    engine_territory_bind_army_records(g_army);   /* +0x70 stays 0 */
    s_slot_0057c93c = 0x55;                        /* sentinel must survive */

    engine_territory_recenter_on_capital(1);
    T_ASSERT_EQ_U(s_slot_005651b4, 0u);
    T_ASSERT_EQ_U(s_slot_0057c93c, 0x55u);        /* unchanged */
    recenter_teardown();
    return 0;
}

int test_territory_recenter_capital_unbound_is_noop(void)
{
    engine_territory_bind_army_records(NULL);
    engine_territory_recenter_on_capital(1);      /* must not crash */
    return 0;
}
