/*
 * Tests for src/engine_dirty_rect.c.  Covers each of the 13 wrappers
 * + the quirky y-max clamp condition + the dirty_mark monotonic-up-to-1
 * behaviour + below-status-bar overrides.
 */

#include "t.h"
#include "engine_dirty_rect.h"

/* Common fresh state — both bounds start at extreme values so any input
 * widens them.  Matches how retail's globals are initialised early in
 * the render-state reset path. */
static void reset_state(engine_dirty_rect_state_t *s)
{
    s->dirty_mark           = 0;
    s->dirty_xmin           = 0x7fff;
    s->dirty_xmax           = -0x7fff;
    s->dirty_ymin           = 0x7fff;
    s->dirty_ymax           = -0x7fff;
    s->below_status_redraw  = 0;
}

/* ------------- engine_dirty_rect_expand --------------------------- */

int test_engine_dirty_rect_expand_sets_mark_to_1_from_zero(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s, 100, 100, 200, 200);
    T_ASSERT_EQ_I(s.dirty_mark, 1);
    return 0;
}

int test_engine_dirty_rect_expand_preserves_mark_at_2(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    s.dirty_mark = 2;
    engine_dirty_rect_expand(&s, 100, 100, 200, 200);
    T_ASSERT_EQ_I(s.dirty_mark, 2);
    return 0;
}

int test_engine_dirty_rect_expand_widens_existing_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s, 100, 100, 200, 200);
    engine_dirty_rect_expand(&s,  50,  60, 150, 250);
    T_ASSERT_EQ_I(s.dirty_xmin,  50);
    T_ASSERT_EQ_I(s.dirty_ymin,  60);
    T_ASSERT_EQ_I(s.dirty_xmax, 200);
    T_ASSERT_EQ_I(s.dirty_ymax, 250);
    return 0;
}

int test_engine_dirty_rect_expand_does_not_shrink_existing_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s,  0,  0, 500, 400);
    engine_dirty_rect_expand(&s, 50, 50, 100, 100);   /* fully inside */
    T_ASSERT_EQ_I(s.dirty_xmin,   0);
    T_ASSERT_EQ_I(s.dirty_ymin,   0);
    T_ASSERT_EQ_I(s.dirty_xmax, 500);
    T_ASSERT_EQ_I(s.dirty_ymax, 400);
    return 0;
}

int test_engine_dirty_rect_expand_clamps_xmin_to_zero(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s, -100, 50, 200, 100);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    return 0;
}

int test_engine_dirty_rect_expand_clamps_ymin_to_zero(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s, 50, -100, 200, 100);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    return 0;
}

int test_engine_dirty_rect_expand_clamps_xmax_to_639(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_expand(&s, 0, 0, 1000, 400);
    T_ASSERT_EQ_I(s.dirty_xmax, ENGINE_DIRTY_RECT_SCREEN_X_MAX); /* 0x27f */
    return 0;
}

/* Quirk #11 — ymax clamp threshold is the X-bound, so values in
 * [0x1e0, 0x27f] slip through unclamped. */
int test_engine_dirty_rect_quirk_ymax_threshold_uses_x_bound(void)
{
    engine_dirty_rect_state_t s;

    /* Just past Y bound (480) but below X bound (639): NOT clamped. */
    reset_state(&s);
    engine_dirty_rect_expand(&s, 0, 0, 100, 500);
    T_ASSERT_EQ_I(s.dirty_ymax, 500);   /* would be 479 if condition were correct */

    /* At X bound (639): still NOT clamped (condition is strict <). */
    reset_state(&s);
    engine_dirty_rect_expand(&s, 0, 0, 100, 0x27f);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x27f);

    /* Above X bound (640): clamped to Y bound (479). */
    reset_state(&s);
    engine_dirty_rect_expand(&s, 0, 0, 100, 0x280);
    T_ASSERT_EQ_I(s.dirty_ymax, ENGINE_DIRTY_RECT_SCREEN_Y_MAX);

    return 0;
}

/* ------------- preset wrappers ------------------------------------- */

int test_engine_dirty_rect_right_half_marks_right_half(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_right_half(&s);
    T_ASSERT_EQ_I(s.dirty_xmin, 0x140);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x27f);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    T_ASSERT_EQ_I(s.dirty_mark, 1);
    return 0;
}

int test_engine_dirty_rect_full_screen_marks_whole_screen(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_full_screen(&s);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x27f);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    return 0;
}

int test_engine_dirty_rect_below_status_bar_sets_redraw_flag(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_below_status_bar(&s);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x1de);    /* note: NOT 0x27f */
    T_ASSERT_EQ_I(s.dirty_ymin, 0x18);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    T_ASSERT_EQ_I(s.dirty_mark, 1);
    T_ASSERT_EQ_I(s.below_status_redraw, 1);
    return 0;
}

int test_engine_dirty_rect_below_status_bar_full_overrides_mark_to_2(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_below_status_bar_full(&s);
    T_ASSERT_EQ_I(s.dirty_mark, 2);
    T_ASSERT_EQ_I(s.below_status_redraw, 1);
    /* And the box should still be the below-status region. */
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x1de);
    T_ASSERT_EQ_I(s.dirty_ymin, 0x18);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x1df);
    return 0;
}

/* ------------- tile-stride wrappers -------------------------------- */

int test_engine_dirty_rect_tile_32_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_32(&s, 5, 3);
    T_ASSERT_EQ_I(s.dirty_xmin, 5 * 0x10);
    T_ASSERT_EQ_I(s.dirty_ymin, 3 * 0x10);
    T_ASSERT_EQ_I(s.dirty_xmax, 5 * 0x10 + 0x20);
    T_ASSERT_EQ_I(s.dirty_ymax, 3 * 0x10 + 0x20);
    return 0;
}

int test_engine_dirty_rect_tile_48_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_48(&s, 1, 1);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x40);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x40);
    return 0;
}

int test_engine_dirty_rect_tile_64_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_64(&s, 2, 4);
    T_ASSERT_EQ_I(s.dirty_xmin, 0x20);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x20 + 0x40);
    T_ASSERT_EQ_I(s.dirty_ymin, 0x40);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x40 + 0x40);
    return 0;
}

int test_engine_dirty_rect_tile_80_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_80(&s, 0, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x50);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x50);
    return 0;
}

int test_engine_dirty_rect_tile_96_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_96(&s, 1, 1);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x10 + 0x60);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x10 + 0x60);
    return 0;
}

int test_engine_dirty_rect_tile_200x160_box(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_200x160(&s, 1, 2);
    T_ASSERT_EQ_I(s.dirty_xmin, 0x10);
    T_ASSERT_EQ_I(s.dirty_ymin, 0x20);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x10 + 200);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x20 + 0xa0);
    return 0;
}

/* Tile wrappers clamp negative inputs to 0 BEFORE multiplying. */
int test_engine_dirty_rect_tile_64_negative_inputs_clamped(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_tile_64(&s, -5, -3);
    /* After clamping, (col,row) = (0,0), so box = (0,0)..(0x40,0x40). */
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x40);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x40);
    return 0;
}

/* mark_tile_80x48_noclamp explicitly lacks the negative-input prelude. */
int test_engine_dirty_rect_tile_80x48_noclamp_skips_prelude(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    /* col=-1, row=-1.  Raw x2/y2 = -16 + {0x50, 0x30} = {0x40, 0x20} —
     * still positive.  x1/y1 = -16, which expand()'s post-clamp lifts
     * back up to 0.  The observable: clamp-to-zero on xmin/ymin still
     * fires (via expand), but the wrapper itself does no prelude. */
    engine_dirty_rect_mark_tile_80x48_noclamp(&s, -1, -1);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);    /* from expand's post-clamp */
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 0x40);
    T_ASSERT_EQ_I(s.dirty_ymax, 0x20);
    return 0;
}

/* ------------- engine_dirty_rect_mark_box_tiled -------------------- */

int test_engine_dirty_rect_box_tiled_pixel_origin_box(void)
{
    /* Pixel start (100, 50), 3 tiles wide, 4 tiles tall.  Width =
     * 48 px, height = 64 px.  Box (100, 50)..(148, 114). */
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_box_tiled(&s, 100, 50, 3, 4, /*force*/ 1);
    T_ASSERT_EQ_I(s.dirty_xmin, 100);
    T_ASSERT_EQ_I(s.dirty_ymin, 50);
    T_ASSERT_EQ_I(s.dirty_xmax, 148);
    T_ASSERT_EQ_I(s.dirty_ymax, 114);
    T_ASSERT_EQ_I(s.dirty_mark, 1);     /* force=1 → no escalation */
    return 0;
}

int test_engine_dirty_rect_box_tiled_force_full_escalates_mark(void)
{
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_box_tiled(&s, 0, 0, 1, 1, /*force*/ 2);
    T_ASSERT_EQ_I(s.dirty_mark, 2);     /* force > 1 → mark=2 override */
    return 0;
}

int test_engine_dirty_rect_box_tiled_clamps_negative_topleft(void)
{
    /* Negative pixel origin — both clamped to 0 before computing the
     * bottom-right.  Box: (0, 0)..(0 + 2*16, 0 + 3*16) = (0,0)..(32,48). */
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_box_tiled(&s, -10, -20, 2, 3, /*force*/ 1);
    T_ASSERT_EQ_I(s.dirty_xmin, 0);
    T_ASSERT_EQ_I(s.dirty_ymin, 0);
    T_ASSERT_EQ_I(s.dirty_xmax, 32);
    T_ASSERT_EQ_I(s.dirty_ymax, 48);
    return 0;
}

int test_engine_dirty_rect_box_tiled_force_zero_no_escalation(void)
{
    /* force_full = 0 (not > 1) → mark stays whatever expand stamped. */
    engine_dirty_rect_state_t s;
    reset_state(&s);
    engine_dirty_rect_mark_box_tiled(&s, 10, 10, 1, 1, /*force*/ 0);
    T_ASSERT_EQ_I(s.dirty_mark, 1);     /* just the expand-stamped value */
    return 0;
}

int test_engine_dirty_rect_box_tiled_escalation_overrides_preexisting(void)
{
    /* Pre-existing mark=2 — escalation still writes 2 (idempotent). */
    engine_dirty_rect_state_t s;
    reset_state(&s);
    s.dirty_mark = 2;
    engine_dirty_rect_mark_box_tiled(&s, 0, 0, 1, 1, /*force*/ 2);
    T_ASSERT_EQ_I(s.dirty_mark, 2);
    return 0;
}
