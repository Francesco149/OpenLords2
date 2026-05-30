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
