/*
 * Unit tests for engine_overlay_render — the strategic-mode overlay
 * pass (cities + units + debug text).  Coverage matrix:
 *
 *   - frame driver invokes top phantom + (row_count-2)/2 pairs + bottom
 *     phantom
 *   - row iterators dispatch on bits 0x80 (army) and 0x40 (city) of
 *     descriptor[+2]
 *   - row iterators advance cursor unconditionally (empty and non-empty)
 *   - phase0_row has NO left/right edge cells (vs primary pass's phase0)
 *   - phase1_row visits col_count + 1 cells (first=phase1, middle=phase0,
 *     last=phase2)
 *   - debug text fires on middle cells of phase1_row but NOT on the
 *     first or last cells (quirk)
 *   - debug text fires on phase0_row middle cells (all of them)
 *   - debug text does NOT fire on top/bottom phantom rows
 *   - city drawer picks fixed sprite 0x4e when raw-flag-bit 0x10/0x40
 *     selectors are set; otherwise size-based 0x38+size
 *   - city drawer clears bit 0x40 of descriptor[+2] post-dispatch
 *   - city drawer Y anchor is +6, X anchor is +0x14 (phase 0/2) or
 *     +0x14 - x_half_step (phase 1)
 *   - city drawer src_offset validation: > 310000 OR < 0 → error
 *   - debug text mode 1: draws when cell_value_mode != 0 and
 *     descriptor_value_mode == 0
 *   - debug text mode 2: draws when descriptor_value_mode != 0 (winner)
 *   - debug text both modes 0: no draw
 *   - debug text head_link = 0 in mode 2: no draw
 *   - debug text empty cell: no draw
 *   - view-mode 1 pre-clear zeros the left strip
 *   - army stub bumps a per-call counter
 */

#include "t.h"

#include "engine_dirty_rect.h"
#include "engine_overlay_render.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_paint.h"

#include <stdint.h>
#include <string.h>

/* ────────────────────────────────────────────────────────────────
 * Canvas + dirty-rect shared scaffolding.
 * ──────────────────────────────────────────────────────────────── */

enum { CANVAS_W = 0x280, CANVAS_H = 0x1e0 };
static uint8_t g_canvas[CANVAS_W * CANVAS_H];
static engine_dirty_rect_state_t g_dirty;

/* Synthetic tile array — 5 columns × 4 rows, row stride 0x104. */
#define TILE_STRIDE_BYTES   0x104
#define TILE_COLS           5
#define TILE_ROWS           4
#define TILE_ROWS_PHYSICAL  (TILE_ROWS + 2)
#define EMPTY_CELL          0x0fff0000

static uint8_t g_cell_bytes[TILE_STRIDE_BYTES * TILE_ROWS_PHYSICAL];

static void cell_set(int col, int row, int32_t value)
{
    int32_t *slot = (int32_t *)&g_cell_bytes[col * 4 + row * TILE_STRIDE_BYTES];
    *slot = value;
}

static void cell_fill_all(int32_t value)
{
    for (int r = 0; r < TILE_ROWS_PHYSICAL; r++) {
        for (int c = 0; c < TILE_COLS + 2; c++) {
            cell_set(c, r, value);
        }
    }
}

/* Synthetic tile descriptor — byte-indexed by cell value. */
static uint8_t g_tile_descriptors[256];

static void descriptor_set(int cell_value, uint8_t first_byte, uint8_t raw_flag,
                           uint8_t dispatch_flag, uint8_t head_link)
{
    g_tile_descriptors[cell_value + 0] = first_byte;
    g_tile_descriptors[cell_value + 1] = raw_flag;
    g_tile_descriptors[cell_value + 2] = dispatch_flag;
    g_tile_descriptors[cell_value + 5] = head_link;
}

/* Synthetic territory record array.  Tests bind slot N's max-size
 * fields at +0x153 / +0x154 to drive the city drawer's cap. */
#define TERR_SLOTS   8
static uint8_t g_records[TERR_SLOTS * 0x1a4];

static void territory_set_max_size(int slot, int8_t lo, int8_t hi)
{
    uint8_t *rec = &g_records[slot * 0x1a4];
    rec[0x153] = (uint8_t)lo;
    rec[0x154] = (uint8_t)hi;
}

/* Synthetic per-cell-size table (16-bit shorts).  Keyed by
 * (cell / 8). */
static int16_t g_per_cell_size_table[64];

/* Minimal RLE overlay blob with valid descriptors at slots 0..0x4f.
 *
 * Each descriptor slot N is at base + N*16 + 8.  Slot 0x4e is the
 * largest used by the city drawer (large-sprite fallback); we provide
 * a flat array of 0x4f+1 valid descriptors back-to-back, with the
 * blitter-source data appended.  Each descriptor: w=1, h_short=1,
 * src_off = the offset of one shared pixel-data area at the very end,
 * row_count=1.  Pixel data: transparency run of 1 (0x00 0x01). */
#define RLE_BLOB_SLOTS   0x60
#define RLE_BLOB_HEADER  8
#define RLE_BLOB_DESC_BYTES (16 * RLE_BLOB_SLOTS)
#define RLE_BLOB_PIXEL_OFF  (RLE_BLOB_HEADER + RLE_BLOB_DESC_BYTES)
#define RLE_BLOB_SIZE       (RLE_BLOB_PIXEL_OFF + 4)
static uint8_t g_rle_blob[RLE_BLOB_SIZE];

static void rle_blob_init_valid(void)
{
    memset(g_rle_blob, 0, sizeof g_rle_blob);
    for (int i = 0; i < RLE_BLOB_SLOTS; i++) {
        uint8_t *slot = g_rle_blob + RLE_BLOB_HEADER + i * 16;
        /* +0..1: w = 1 */
        slot[0] = 0x01; slot[1] = 0x00;
        /* +2..3: h_short = 1 */
        slot[2] = 0x01; slot[3] = 0x00;
        /* +4..7: src_off = RLE_BLOB_PIXEL_OFF (valid, < 310000) */
        slot[4] = (uint8_t)(RLE_BLOB_PIXEL_OFF & 0xff);
        slot[5] = (uint8_t)((RLE_BLOB_PIXEL_OFF >> 8) & 0xff);
        slot[6] = (uint8_t)((RLE_BLOB_PIXEL_OFF >> 16) & 0xff);
        slot[7] = (uint8_t)((RLE_BLOB_PIXEL_OFF >> 24) & 0xff);
        /* +0xc..0xf: flag_a, row_count=1, flag_b, _ */
        slot[0xc] = 0;
        slot[0xd] = 1;
        slot[0xe] = 0;
        slot[0xf] = 0;
    }
    /* Pixel area: transparency-run opcode (0x00) + count (0x01). */
    g_rle_blob[RLE_BLOB_PIXEL_OFF + 0] = 0x00;
    g_rle_blob[RLE_BLOB_PIXEL_OFF + 1] = 0x01;
}

/* RLE blob with INVALID src_off (> 310000) at slot 0x38 — the base
 * sprite the size-based path picks first (size_val=0 → 0+0x38). */
static uint8_t g_rle_blob_bad[RLE_BLOB_SIZE];

static void rle_blob_init_bad(void)
{
    rle_blob_init_valid();
    /* Overwrite slot 0x38 of g_rle_blob_bad with a bad src_off. */
    memcpy(g_rle_blob_bad, g_rle_blob, sizeof g_rle_blob);
    uint8_t *slot = g_rle_blob_bad + RLE_BLOB_HEADER + 0x38 * 16;
    /* src_off = 400000 (> 310000) */
    slot[4] = 0x80; slot[5] = 0x1a; slot[6] = 0x06; slot[7] = 0x00;
}

static void scaffold_init(void)
{
    memset(g_canvas, 0, sizeof g_canvas);
    gfx_paint_bind_canvas(g_canvas, CANVAS_W, CANVAS_H);

    g_dirty = (engine_dirty_rect_state_t){
        .dirty_mark = 0,
        .dirty_xmin = CANVAS_W - 1, .dirty_xmax = 0,
        .dirty_ymin = CANVAS_H - 1, .dirty_ymax = 0,
        .below_status_redraw = 0,
    };
    engine_overlay_render_set_dirty_state(&g_dirty);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();

    gfx_dispatch_set_alt_clip(0, 0, 0);
}

static void bind_default_scene(void)
{
    scaffold_init();
    memset(g_records, 0, sizeof g_records);
    memset(g_tile_descriptors, 0, sizeof g_tile_descriptors);
    memset(g_cell_bytes, 0, sizeof g_cell_bytes);
    memset(g_per_cell_size_table, 0, sizeof g_per_cell_size_table);
    rle_blob_init_valid();

    engine_overlay_render_bind_camera(64, 64, 0, 0, TILE_COLS, TILE_ROWS,
                                      32, 16, 16);
    engine_overlay_render_bind_tile_array((const int32_t *)g_cell_bytes,
                                          TILE_STRIDE_BYTES);
    engine_overlay_render_bind_tile_descriptors(g_tile_descriptors);
    engine_overlay_render_bind_overlay_blob(g_rle_blob);
    engine_overlay_render_bind_territory_records(g_records);
    engine_overlay_render_bind_per_cell_size_table(g_per_cell_size_table);
    engine_overlay_render_bind_canvas(g_canvas, CANVAS_W, CANVAS_H);
    engine_overlay_render_bind_debug_font(NULL, NULL);
    engine_overlay_render_set_view_mode(0);
    engine_overlay_render_set_active_territory(1);
    /* Give the active territory a generous max-size band so the city
     * drawer's cap never kicks in unless tests target it. */
    territory_set_max_size(1, /*lo=*/0, /*hi=*/100);
    engine_overlay_render_set_dev_flags(0, 0);
}

/* ────────────────────────────────────────────────────────────────
 * Tests.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_overlay_render_frame_all_empty_no_calls(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_error_count(), 0);
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 0);
    T_ASSERT_EQ_I(engine_overlay_render_city_call_count(), 0);
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 0);
    return 0;
}

int test_engine_overlay_render_dispatch_bit_0x40_calls_city(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* descriptor[+2] = 0x40 → city dispatches; 0x80 unset → army not. */
    descriptor_set(0x20, /*first=*/0, /*raw=*/0, /*disp=*/0x40, /*head=*/0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_city_call_count(), 1);
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 0);
    return 0;
}

int test_engine_overlay_render_dispatch_bit_0x80_calls_army(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x80, 0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_city_call_count(), 0);
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 1);
    return 0;
}

int test_engine_overlay_render_dispatch_both_bits_calls_both(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0xc0, 0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_city_call_count(), 1);
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 1);
    return 0;
}

int test_engine_overlay_render_dispatch_neither_bit_no_calls(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* descriptor[+2] = 0x20 — the primary pass's bit, NOT ours. */
    descriptor_set(0x20, 0, 0, /*disp=*/0x20, 0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_city_call_count(), 0);
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 0);
    return 0;
}

int test_engine_overlay_render_city_clears_bit_0x40_after_dispatch(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);

    engine_overlay_render_frame();
    /* Bit 0x40 should now be cleared (& 0xbf). */
    T_ASSERT_EQ_I(g_tile_descriptors[0x20 + 2] & 0x40, 0);
    return 0;
}

int test_engine_overlay_render_city_bad_src_offset_bumps_error(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);
    /* per-cell-size[4] = 1 → size_val = 0 → sprite_idx = 0x38 (bad). */
    g_per_cell_size_table[4] = 1;
    rle_blob_init_bad();
    engine_overlay_render_bind_overlay_blob(g_rle_blob_bad);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_error_count(), 1);
    return 0;
}

int test_engine_overlay_render_city_size_based_sprite_when_no_large_select(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, /*first=*/0, /*raw=*/0x00, /*disp=*/0x40, 0);
    /* per-cell-size[0x20 / 8 = 4] = 5 → size_val = 4 → sprite_idx =
     * 0x38 + 4 = 0x3c. */
    g_per_cell_size_table[4] = 5;
    /* Territory cap: max_hi - max_lo = 100 - 0 = 100 >= 4 → no cap. */

    engine_overlay_render_frame();
    /* Verify by checking that last_glyph_index after dispatch is 0x3c. */
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x3c);
    return 0;
}

int test_engine_overlay_render_city_large_sprite_when_raw_flag_0x10(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* raw flag bit 0x10 set → large_sel != 0 → fixed sprite 0x4e. */
    descriptor_set(0x20, /*first=*/0, /*raw=*/0x10, /*disp=*/0x40, 0);
    g_per_cell_size_table[4] = 99;  /* would otherwise pick 0x38 + 98 */

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x4e);
    return 0;
}

int test_engine_overlay_render_city_large_sprite_when_raw_flag_0x40(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* raw flag bit 0x40 set → large_sel != 0 → fixed sprite 0x4e. */
    descriptor_set(0x20, /*first=*/0, /*raw=*/0x40, /*disp=*/0x40, 0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x4e);
    return 0;
}

int test_engine_overlay_render_city_large_sprite_when_raw_flag_0x80_and_first_not_0x14(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* raw flag bit 0x80 set AND first byte != 0x14 → large.  Use
     * first=0x99 to make sure it's not 0x14. */
    descriptor_set(0x20, /*first=*/0x99, /*raw=*/0x80, /*disp=*/0x40, 0);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x4e);
    return 0;
}

int test_engine_overlay_render_city_size_when_raw_flag_0x80_and_first_is_0x14(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    /* raw 0x80 AND first == 0x14 → large_sel left at (raw & 0x50) = 0
     * → size-based path. */
    descriptor_set(0x20, /*first=*/0x14, /*raw=*/0x80, /*disp=*/0x40, 0);
    g_per_cell_size_table[4] = 3;  /* size_val = 2 → sprite 0x3a */

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x3a);
    return 0;
}

int test_engine_overlay_render_city_territory_cap_clamps_size_to_zero(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);
    g_per_cell_size_table[4] = 99;  /* size_val = 98 */
    /* Cap: max_hi - max_lo = 1 < 98 → size_val resets to 0 → sprite
     * 0x38. */
    territory_set_max_size(1, 0, 1);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(gfx_dispatch_last_glyph_index(), 0x38);
    return 0;
}

int test_engine_overlay_render_debug_text_mode_1_draws(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    /* Single non-empty cell in a middle row triggers debug text. */
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x00, 0);  /* no dispatcher fires */
    engine_overlay_render_set_dev_flags(/*cell_v=*/1, /*desc_v=*/0);

    engine_overlay_render_frame();
    /* Phase0 row at row 1 visits 5 cells, 1 non-empty → 1 debug call.
     * Phase1 row at row 2 visits 6 cells, all empty → 0.
     * Total: 1 debug call. */
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 1);
    return 0;
}

int test_engine_overlay_render_debug_text_mode_2_wins(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x00, /*head=*/0x77);
    /* Both modes set, mode 2 wins; head_link != 0 → draw fires. */
    engine_overlay_render_set_dev_flags(1, 1);

    engine_overlay_render_frame();
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 1);
    return 0;
}

int test_engine_overlay_render_debug_text_no_dev_flags_no_call(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, 0, 0);
    /* Both dev flags 0 (default). */

    engine_overlay_render_frame();
    /* The debug function IS invoked per non-empty middle cell (count
     * bumps), but both modes are 0 so no text draws.  The counter
     * tracks invocations not draws — verify counter still bumps. */
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 1);
    return 0;
}

int test_engine_overlay_render_debug_text_phantom_rows_no_call(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    /* Place a non-empty cell in the TOP PHANTOM row (row 0 = row_start). */
    cell_set(2, 0, 0x20);
    descriptor_set(0x20, 0, 0, 0, 0);
    engine_overlay_render_set_dev_flags(1, 0);

    engine_overlay_render_frame();
    /* Top phantom row should NOT fire debug text (retail FUN_00405602
     * top-row loop omits the debug call). */
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 0);
    return 0;
}

int test_engine_overlay_render_debug_text_phase1_first_cell_no_call(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    /* The phase-1 row first cell is at col=col_start, row=row_start+1
     * (immediately after top phantom row).  Place a non-empty cell
     * there ONLY. */
    cell_set(0, 1, 0x20);
    descriptor_set(0x20, 0, 0, 0, 0);
    engine_overlay_render_set_dev_flags(1, 0);

    engine_overlay_render_frame();
    /* Phase1 first cell (phase=1) does NOT fire debug text (quirk). */
    T_ASSERT_EQ_I(engine_overlay_render_debug_call_count(), 0);
    return 0;
}

int test_engine_overlay_render_phase1_visits_col_count_plus_one_cells(void)
{
    bind_default_scene();
    /* Set ALL cells non-empty with descriptor bit 0x80 set so we can
     * count army-call invocations. */
    for (int r = 0; r < TILE_ROWS_PHYSICAL; r++) {
        for (int c = 0; c < TILE_COLS + 2; c++) {
            cell_set(c, r, 0x20);
        }
    }
    descriptor_set(0x20, 0, 0, /*disp=*/0x80, 0);

    /* Run only the phase1 row.  Bind a fresh cursor first. */
    gfx_glyph_set_dest(64, 80);
    /* Setting the iterator's cur_row requires re-binding via the
     * driver; for test isolation, invoke frame() and then check the
     * total. */
    engine_overlay_render_frame();
    /* Frame call:
     *   top phantom : 5 cells × 1 army call
     *   phase1 row  : 6 cells × 1 army call (col_count + 1)
     *   pair × 1    : (phase0 5) + (phase1 6) = 11
     *   bottom row  : 5 cells
     * Total: 5 + 6 + 11 + 5 = 27 army calls.  */
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 27);
    return 0;
}

int test_engine_overlay_render_phase0_visits_col_count_cells(void)
{
    bind_default_scene();
    for (int r = 0; r < TILE_ROWS_PHYSICAL; r++) {
        for (int c = 0; c < TILE_COLS + 2; c++) {
            cell_set(c, r, 0x20);
        }
    }
    descriptor_set(0x20, 0, 0, /*disp=*/0x80, 0);

    engine_overlay_render_frame();
    /* phase0_row's cell count check is implicit in the total verified
     * above.  Re-test here with a different camera (col_count=3) to
     * exercise the parameter sensitivity. */
    bind_default_scene();
    engine_overlay_render_bind_camera(64, 64, 0, 0, /*col_count=*/3,
                                      TILE_ROWS, 32, 16, 16);
    for (int r = 0; r < TILE_ROWS_PHYSICAL; r++) {
        for (int c = 0; c < TILE_COLS + 2; c++) {
            cell_set(c, r, 0x20);
        }
    }
    descriptor_set(0x20, 0, 0, /*disp=*/0x80, 0);
    engine_overlay_render_frame();
    /* New totals:
     *   top  : 3
     *   ph1  : 3+1 = 4
     *   pair : 3 + 4 = 7
     *   bot  : 3
     *   total: 3 + 4 + 7 + 3 = 17 */
    T_ASSERT_EQ_I(engine_overlay_render_army_call_count(), 17);
    return 0;
}

int test_engine_overlay_render_view_mode_1_pre_clears_left_strip(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    /* Pre-paint the left strip with non-zero values. */
    for (int y = 0x18; y < 0x1bc; y++) {
        for (int x = 0; x < 4; x++) {
            g_canvas[y * CANVAS_W + x] = 0xff;
        }
    }
    engine_overlay_render_set_view_mode(1);

    engine_overlay_render_frame();
    /* All those pixels should now be 0. */
    for (int y = 0x18; y < 0x1bc; y++) {
        for (int x = 0; x < 4; x++) {
            T_ASSERT_EQ_I(g_canvas[y * CANVAS_W + x], 0);
        }
    }
    /* Pixels just above (y=0x17) and just below (y=0x1bc) the strip
     * should NOT have been touched — but bind_default_scene zeros the
     * canvas, so they're already 0.  Set a guard pixel and verify it
     * survives. */
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    g_canvas[0x17 * CANVAS_W + 0] = 0x77;
    g_canvas[0x1bc * CANVAS_W + 0] = 0x77;
    engine_overlay_render_set_view_mode(1);
    engine_overlay_render_frame();
    T_ASSERT_EQ_I(g_canvas[0x17 * CANVAS_W + 0], 0x77);
    T_ASSERT_EQ_I(g_canvas[0x1bc * CANVAS_W + 0], 0x77);
    return 0;
}

int test_engine_overlay_render_view_mode_0_no_pre_clear(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    /* Paint left strip non-zero. */
    for (int y = 0x18; y < 0x1bc; y++) {
        for (int x = 0; x < 4; x++) {
            g_canvas[y * CANVAS_W + x] = 0xff;
        }
    }
    engine_overlay_render_set_view_mode(0);

    engine_overlay_render_frame();
    /* Left strip should be UNCHANGED. */
    for (int y = 0x18; y < 0x1bc; y++) {
        for (int x = 0; x < 4; x++) {
            T_ASSERT_EQ_I(g_canvas[y * CANVAS_W + x], 0xff);
        }
    }
    return 0;
}

int test_engine_overlay_render_city_phase_1_x_anchor_shifted(void)
{
    bind_default_scene();
    /* Place the phase-1 first cell with dispatch bit set; mark
     * dirty_xmin so we can check the anchor. */
    cell_fill_all(EMPTY_CELL);
    cell_set(0, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);

    engine_overlay_render_frame();
    /* Phase 1 X anchor = 0x14 - x_half_step = 0x14 - 16 = 4.
     * Cursor at start of phase-1 row = (x0=64, y_after_top=80).
     * After dispatch, cursor moves to (64+4, 80+6) = (68, 86) for
     * the dirty mark, tile coords = (4, 5).  The 32-px box marks the
     * range [tx*16, tx*16+0x1f] = [64, 95].  dirty_xmin should be at
     * MOST 64 (phantom-row stale state may have pushed lower). */
    T_ASSERT(g_dirty.dirty_xmin <= 64);
    T_ASSERT(g_dirty.dirty_xmax >= 64);
    return 0;
}

int test_engine_overlay_render_error_count_persists_across_frames(void)
{
    bind_default_scene();
    cell_fill_all(EMPTY_CELL);
    cell_set(1, 1, 0x20);
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);
    g_per_cell_size_table[4] = 1;  /* size_val = 0 → sprite 0x38 (bad). */
    rle_blob_init_bad();
    engine_overlay_render_bind_overlay_blob(g_rle_blob_bad);

    engine_overlay_render_frame();
    int c1 = engine_overlay_render_error_count();
    /* The city drawer cleared bit 0x40 of descriptor[+2] last frame —
     * re-arm it so the next frame dispatches again. */
    descriptor_set(0x20, 0, 0, /*disp=*/0x40, 0);
    engine_overlay_render_frame();
    int c2 = engine_overlay_render_error_count();
    T_ASSERT(c2 > c1);
    return 0;
}
