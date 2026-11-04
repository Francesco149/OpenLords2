/*
 * Unit tests for engine_workforce_hud — the strategic-map workforce
 * icon-grid overlay (FUN_00412143 + its 9 leaves).
 */

#include <string.h>

#include "t.h"
#include "engine_workforce_hud.h"
#include "gfx_globals.h"
#include "save.h"
#include "save_schema_globals.h"
#include "gfx_paint.h"
#include "gfx_sprite_render.h"

#define AREC(key) (&g_per_army_768[(key) * 0x300])
#define AI32(p, o) (*(int32_t *)((p) + (o)))

#define WF_SCALE 0xb8
#define WF_CUR(row) (0xc4 + (row) * 0xc)
#define WF_MIN(row) (0xc8 + (row) * 0xc)
#define WF_MAX(row) (0xcc + (row) * 0xc)
#define FEAT_A 0x2ad
#define FEAT_B 0x2dd
#define FEAT_C 0x295

static void wf_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    engine_workforce_hud_bind_icon_sheet(NULL);
    engine_workforce_hud_set_panel_mode(0);
    gfx_sprite_render_set_blob(NULL);
    gfx_paint_bind_canvas(NULL, 0, 0);
    /* g_workforce_grid/indicator are module statics with no reset hook
     * (retail's own DAT_005540a0 starts .data-zeroed and only a live
     * render call ever touches it — no reset exists there either); clear
     * all 8 rows via the public API so tests don't see leftovers from
     * whichever test ran previously in this same process. */
    for (int category = 0; category < 8; category++)
        engine_workforce_compute_row(0, category, 0, 0);
}

/* ─── engine_workforce_row_sprite_index ─────────────────────────────── */

int test_workforce_row_sprite_index_table_lookup(void)
{
    wf_reset();
    /* k_workforce_row_index = {5,0,1,3,6,7,8,2}, indexed by category. */
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 0), 5);
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 2), 1);
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 7), 2);
    return 0;
}

int test_workforce_row_sprite_index_special_case_overrides_category0(void)
{
    wf_reset();
    /* category 0 normally maps to row 5; overridden to row 4 when
     * FEAT_B is clear and FEAT_A is set. */
    AREC(1)[FEAT_B] = 0;
    AREC(1)[FEAT_A] = 1;
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 0), 4);
    return 0;
}

int test_workforce_row_sprite_index_special_case_needs_both_conditions(void)
{
    wf_reset();
    /* FEAT_B set -> no override, falls back to the table's row 5. */
    AREC(1)[FEAT_B] = 1;
    AREC(1)[FEAT_A] = 1;
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 0), 5);

    /* FEAT_A clear -> no override either. */
    AREC(1)[FEAT_B] = 0;
    AREC(1)[FEAT_A] = 0;
    T_ASSERT_EQ_I(engine_workforce_row_sprite_index(1, 0), 5);
    return 0;
}

/* ─── engine_workforce_fill_row_linear ──────────────────────────────── */

int test_workforce_fill_row_linear_default_color_below_threshold(void)
{
    wf_reset();
    /* k_workforce_cell_threshold's smallest value is 1 at cell index 12;
     * count=1 should fill exactly that one cell with its default color
     * (k_workforce_cell_default_color[12] == 10). */
    engine_workforce_fill_row_linear(0, 0, 1);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 10);
    /* every other cell's threshold is > 1, so still empty. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 8), 0);
    return 0;
}

int test_workforce_fill_row_linear_override_color(void)
{
    wf_reset();
    engine_workforce_fill_row_linear(0, 7, 1);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 7);
    return 0;
}

int test_workforce_fill_row_linear_category6_forces_fill_code_2(void)
{
    wf_reset();
    engine_workforce_fill_row_linear(6, 0, 1);
    /* fill_code forced to 2 regardless of the passed-in 0. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(6, 12), 2);
    return 0;
}

int test_workforce_fill_row_linear_full_count_fills_all_cells(void)
{
    wf_reset();
    engine_workforce_fill_row_linear(0, 9, 25);
    for (int i = 0; i < 25; i++)
        T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, i), 9);
    return 0;
}

/* ─── engine_workforce_fill_row_dual ────────────────────────────────── */

int test_workforce_fill_row_dual_small_values_uses_graduated_tables(void)
{
    wf_reset();
    /* k_workforce_small_pos_threshold's smallest is 1 at index 7. */
    engine_workforce_fill_row_dual(0, 5, 9, /*position=*/1, /*magnitude=*/0);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 7), 5);
    return 0;
}

int test_workforce_fill_row_dual_category6_forces_both_bytes(void)
{
    wf_reset();
    engine_workforce_fill_row_dual(6, 5, 9, 1, 0);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(6, 7), 2);
    return 0;
}

int test_workforce_fill_row_dual_large_values_linear_fallback(void)
{
    wf_reset();
    /* fill_row_linear/fill_row_dual are leaves that assume the caller
     * (engine_workforce_compute_row) already cleared the row; called
     * directly here, so clear it the same way first (compute_row's own
     * memset, via a (0,0) call). */
    engine_workforce_compute_row(1, 0, 0, 0);

    /* position/magnitude >= 13 takes the plain linear-fill branch: left
     * `position` cells (0..position-1) get fill_byte; the right
     * `magnitude` cells from the end (24 downward, stopping at an
     * already-filled cell) get indicator_code. position=15 -> cells
     * 0..14 = fill_byte; magnitude=13 -> cells 24..15 = indicator_code
     * (only 10 of the 13 requested cells are still empty by the time the
     * downward scan reaches the already-filled cell 14, so it stops
     * there — retail's own early-stop-on-collision behavior). */
    engine_workforce_fill_row_dual(0, 3, 4, 15, 13);
    for (int i = 0; i < 15; i++)
        T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, i), 3);
    for (int i = 15; i < 25; i++)
        T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, i), 4);
    return 0;
}

/* ─── engine_workforce_compute_row ──────────────────────────────────── */

int test_workforce_compute_row_zero_position_and_magnitude_clears_row(void)
{
    wf_reset();
    engine_workforce_fill_row_linear(0, 9, 25);  /* pre-fill */
    engine_workforce_compute_row(1, 0, 0, 0);
    for (int i = 0; i < 25; i++)
        T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, i), 0);
    return 0;
}

int test_workforce_compute_row_position_only_uses_default_colors(void)
{
    wf_reset();
    /* category 1 -> row_sprite_index 0 (k_workforce_row_index[1]) ->
     * fill byte k_workforce_fill_byte[0] == 4. */
    engine_workforce_compute_row(1, 1, /*position=*/1, /*magnitude=*/0);
    /* position-only calls fill_row_linear(category, fill_byte, position) —
     * fill_byte is nonzero (4), so cells use the override color, not
     * their own default. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(1, 12), 4);
    return 0;
}

int test_workforce_compute_row_negative_magnitude_only(void)
{
    wf_reset();
    engine_workforce_compute_row(1, 1, /*position=*/0, /*magnitude=*/-1);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(1, 12), 1);
    return 0;
}

int test_workforce_compute_row_positive_magnitude_only(void)
{
    wf_reset();
    engine_workforce_compute_row(1, 1, /*position=*/0, /*magnitude=*/1);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(1, 12), 2);
    return 0;
}

/* ─── engine_workforce_compute_rows (full per-army pass) ────────────── */

int test_workforce_compute_rows_zero_scale_is_noop(void)
{
    wf_reset();
    AI32(AREC(1), WF_SCALE) = 0;
    engine_workforce_fill_row_linear(0, 9, 1);   /* pre-fill a marker */
    engine_workforce_compute_rows(1);
    /* scale==0 -> whole pass skipped, marker survives untouched. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 9);
    return 0;
}

int test_workforce_compute_rows_within_band_is_flat_fill(void)
{
    wf_reset();
    /* category 1 -> row 5 (k_workforce_row_index[1] == 0? no: index1==0 ->
     * row 0).  Use category 0 -> row_index[0] normally 5 (no FEAT override
     * here since FEAT bytes are 0). */
    AI32(AREC(1), WF_SCALE) = 10;
    AI32(AREC(1), WF_CUR(5)) = 50;   /* row 5 (category 0's row) */
    AI32(AREC(1), WF_MIN(5)) = 0;
    AI32(AREC(1), WF_MAX(5)) = 100;
    engine_workforce_compute_rows(1);
    /* cur=50 within [0,100] -> magnitude 0, position = ceil(50/10) = 5. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 18); /* k_workforce_fill_byte[5] */
    return 0;
}

int test_workforce_compute_rows_below_min_is_deficit(void)
{
    wf_reset();
    AI32(AREC(1), WF_SCALE) = 10;
    AI32(AREC(1), WF_CUR(5)) = 5;
    AI32(AREC(1), WF_MIN(5)) = 200;
    AI32(AREC(1), WF_MAX(5)) = 300;
    engine_workforce_compute_rows(1);
    /* cur < min -> deficit magnitude = -ceil((200-5)/10) = -20 (>= 13 in
     * magnitude, so fill_row_dual takes its large-value linear-fallback
     * branch): position = ceil(5/10) = 1 cell of fill_byte (18 =
     * k_workforce_fill_byte[5]) at the front, then indicator_code (1,
     * deficit) fills backward from cell 24. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 0), 18);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 24), 1);
    return 0;
}

int test_workforce_compute_rows_above_max_is_surplus(void)
{
    wf_reset();
    AI32(AREC(1), WF_SCALE) = 10;
    AI32(AREC(1), WF_CUR(5)) = 300;
    AI32(AREC(1), WF_MIN(5)) = 0;
    AI32(AREC(1), WF_MAX(5)) = 100;
    engine_workforce_compute_rows(1);
    /* cur > max -> surplus magnitude = (300-100)/10 = 20 (>= 13, same
     * large-value branch): position front-fills with fill_byte (18),
     * indicator_code (2, surplus) fills backward from cell 24. */
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 0), 18);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 24), 2);
    return 0;
}

/* ─── engine_workforce_hud_ensure_buffers ───────────────────────────── */

int test_workforce_ensure_buffers_succeeds(void)
{
    T_ASSERT_EQ_I(engine_workforce_hud_ensure_buffers(), 1);
    /* re-calling (retail's unconditional-realloc quirk) must not crash
     * or leak-detect under ASan. */
    T_ASSERT_EQ_I(engine_workforce_hud_ensure_buffers(), 1);
    return 0;
}

/* ─── engine_workforce_hud_capture_backdrop ─────────────────────────── */

int test_workforce_capture_backdrop_copies_canvas_rect(void)
{
    static uint8_t canvas[64 * 8];
    for (unsigned i = 0; i < sizeof canvas; i++) canvas[i] = (uint8_t)i;
    gfx_paint_bind_canvas(canvas, 64, 8);

    uint32_t dest[2 * 4] = {0};
    /* 2 dwords wide, 4 rows tall, starting at (0,0), staged in the gfx globals; the residual stride 56
     * (64 - 8: the rest of one canvas row). */
    g_gfx_cursor_x = 0; g_gfx_cursor_y = 0; g_gfx_glyph_w = 2; g_gfx_glyph_height_short = 4;
    engine_workforce_hud_capture_backdrop(dest, 56);

    uint32_t expect0, expect1;
    memcpy(&expect0, canvas, 4);
    memcpy(&expect1, canvas + 4, 4);
    T_ASSERT_EQ_U(dest[0], expect0);
    T_ASSERT_EQ_U(dest[1], expect1);

    uint32_t expect_row1_0;
    memcpy(&expect_row1_0, canvas + 64, 4);
    T_ASSERT_EQ_U(dest[2], expect_row1_0);

    gfx_paint_bind_canvas(NULL, 0, 0);
    return 0;
}

int test_workforce_capture_backdrop_null_canvas_is_noop(void)
{
    gfx_paint_bind_canvas(NULL, 0, 0);
    uint32_t dest[4] = { 1, 2, 3, 4 };
    g_gfx_cursor_x = 0; g_gfx_cursor_y = 0; g_gfx_glyph_w = 4; g_gfx_glyph_height_short = 1;
    engine_workforce_hud_capture_backdrop(dest, 0);
    T_ASSERT_EQ_U(dest[0], 1);
    return 0;
}

/* ─── engine_workforce_hud_render gating ────────────────────────────── */

int test_workforce_hud_render_panel_mode_gate(void)
{
    wf_reset();
    engine_workforce_hud_set_panel_mode(0);
    /* Not mode 1 -> the whole render is a no-op; if it weren't gated,
     * this would malloc buffers and touch file IO.  Verify indirectly:
     * grid stays whatever it was (pre-marked) since compute_rows is
     * never reached. */
    engine_workforce_fill_row_linear(0, 7, 1);
    engine_workforce_hud_render(1, 0, 0);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 7);
    return 0;
}

int test_workforce_hud_render_mode1_runs_compute_pass(void)
{
    wf_reset();
    engine_workforce_hud_set_panel_mode(1);
    AI32(AREC(2), WF_SCALE) = 10;
    AI32(AREC(2), WF_CUR(5)) = 50;
    AI32(AREC(2), WF_MIN(5)) = 0;
    AI32(AREC(2), WF_MAX(5)) = 100;
    engine_workforce_hud_render(2, 0, 0);
    T_ASSERT_EQ_I(engine_workforce_hud_grid_cell(0, 12), 18);
    return 0;
}

int test_workforce_hud_render_defaults_icon_sheet_when_unbound(void)
{
    wf_reset();
    T_ASSERT(engine_workforce_hud_icon_sheet() == NULL);
    engine_workforce_hud_set_panel_mode(1);
    engine_workforce_hud_render(2, 0, 0);
    T_ASSERT(engine_workforce_hud_icon_sheet() != NULL);
    return 0;
}

int test_workforce_hud_render_respects_pre_bound_icon_sheet(void)
{
    wf_reset();
    static const uint8_t fake_sheet[4] = {0};
    engine_workforce_hud_bind_icon_sheet(fake_sheet);
    engine_workforce_hud_set_panel_mode(1);
    engine_workforce_hud_render(2, 0, 0);
    T_ASSERT(engine_workforce_hud_icon_sheet() == fake_sheet);
    return 0;
}
