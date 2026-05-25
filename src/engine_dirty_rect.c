/*
 * Render-invalidation primitives — pure-C port of the FUN_00452306
 * cluster.  See engine_dirty_rect.h for the address map and quirks.
 */

#include "engine_dirty_rect.h"

void engine_dirty_rect_expand(engine_dirty_rect_state_t *s,
                              int x1, int y1, int x2, int y2)
{
    /* Bump dirty_mark up to 1 (preserve a higher pre-existing value,
     * e.g. 2 from a prior force-full-repaint call). */
    if (s->dirty_mark < 1) {
        s->dirty_mark = 1;
    }

    /* Expand x bounds. */
    if (x1 < s->dirty_xmin) s->dirty_xmin = x1;
    if (s->dirty_xmax < x2) s->dirty_xmax = x2;

    /* Expand y bounds. */
    if (y1 < s->dirty_ymin) s->dirty_ymin = y1;
    if (s->dirty_ymax < y2) s->dirty_ymax = y2;

    /* Clamp xmin/ymin up to 0. */
    if (s->dirty_xmin < 0) s->dirty_xmin = 0;
    if (s->dirty_ymin < 0) s->dirty_ymin = 0;

    /* Clamp xmax down to the right-edge. */
    if (ENGINE_DIRTY_RECT_SCREEN_X_MAX < s->dirty_xmax) {
        s->dirty_xmax = ENGINE_DIRTY_RECT_SCREEN_X_MAX;
    }

    /* QUIRK: the upper-bound check for ymax uses the X-bound (0x27f)
     * as the comparison threshold even though the assigned clamp value
     * is the Y-bound (0x1df).  Values in [0x1e0, 0x27f] are therefore
     * NOT clamped — they slip past as overshoot rows.  See quirk #11
     * in engine-quirks.md.  Preserved verbatim. */
    if (ENGINE_DIRTY_RECT_SCREEN_X_MAX < s->dirty_ymax) {
        s->dirty_ymax = ENGINE_DIRTY_RECT_SCREEN_Y_MAX;
    }
}

/* ------------------------------------------------------------------ */
/*  Preset wrappers                                                   */
/* ------------------------------------------------------------------ */

void engine_dirty_rect_mark_right_half(engine_dirty_rect_state_t *s)
{
    engine_dirty_rect_expand(s,
                             ENGINE_DIRTY_RECT_RIGHT_HALF_X, 0,
                             ENGINE_DIRTY_RECT_SCREEN_X_MAX,
                             ENGINE_DIRTY_RECT_SCREEN_Y_MAX);
}

void engine_dirty_rect_mark_full_screen(engine_dirty_rect_state_t *s)
{
    engine_dirty_rect_expand(s, 0, 0,
                             ENGINE_DIRTY_RECT_SCREEN_X_MAX,
                             ENGINE_DIRTY_RECT_SCREEN_Y_MAX);
}

void engine_dirty_rect_mark_below_status_bar(engine_dirty_rect_state_t *s)
{
    engine_dirty_rect_expand(s, 0, ENGINE_DIRTY_RECT_STATUS_BAR_H,
                             0x1de, ENGINE_DIRTY_RECT_SCREEN_Y_MAX);
    s->below_status_redraw = 1;
}

void engine_dirty_rect_mark_below_status_bar_full(engine_dirty_rect_state_t *s)
{
    engine_dirty_rect_expand(s, 0, ENGINE_DIRTY_RECT_STATUS_BAR_H,
                             0x1de, ENGINE_DIRTY_RECT_SCREEN_Y_MAX);
    /* This overrides the dirty_mark=1 that expand just stamped.  Setting
     * to 2 after the expand call (not before) means the override is
     * unconditional. */
    s->dirty_mark = 2;
    s->below_status_redraw = 1;
}

/* ------------------------------------------------------------------ */
/*  Tile-stride box wrappers                                          */
/* ------------------------------------------------------------------ */

/* Shared body for the negative-clamping tile wrappers.  Retail inlines
 * an identical clamp + arithmetic prelude in each wrapper. */
static void mark_tile_box(engine_dirty_rect_state_t *s,
                          int col, int row, int w, int h)
{
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    engine_dirty_rect_expand(s,
                             col * 0x10, row * 0x10,
                             col * 0x10 + w, row * 0x10 + h);
}

void engine_dirty_rect_mark_tile_32(engine_dirty_rect_state_t *s, int col, int row)
{
    mark_tile_box(s, col, row, 0x20, 0x20);
}

void engine_dirty_rect_mark_tile_48(engine_dirty_rect_state_t *s, int col, int row)
{
    mark_tile_box(s, col, row, 0x30, 0x30);
}

void engine_dirty_rect_mark_tile_64(engine_dirty_rect_state_t *s, int col, int row)
{
    mark_tile_box(s, col, row, 0x40, 0x40);
}

void engine_dirty_rect_mark_tile_80(engine_dirty_rect_state_t *s, int col, int row)
{
    mark_tile_box(s, col, row, 0x50, 0x50);
}

void engine_dirty_rect_mark_tile_96(engine_dirty_rect_state_t *s, int col, int row)
{
    mark_tile_box(s, col, row, 0x60, 0x60);
}

void engine_dirty_rect_mark_tile_200x160(engine_dirty_rect_state_t *s,
                                         int col, int row)
{
    /* Retail FUN_004526b3 — 200x160 box (the 200 in source is decimal
     * `200` = 0xc8, not the more-natural 0xc8 hex).  Used for one of
     * the larger UI panels. */
    mark_tile_box(s, col, row, 200, 0xa0);
}

void engine_dirty_rect_mark_tile_80x48_noclamp(engine_dirty_rect_state_t *s,
                                               int col, int row)
{
    /* Retail FUN_00452628 — no negative-input clamp prelude.  Just
     * compute the box from raw col/row.  expand()'s clamp-to-zero on
     * xmin/ymin still catches negative x1/y1, but x2/y2 can stay
     * negative if col/row are negative enough — preserved verbatim. */
    engine_dirty_rect_expand(s,
                             col * 0x10, row * 0x10,
                             col * 0x10 + 0x50, row * 0x10 + 0x30);
}

void engine_dirty_rect_mark_box_tiled(engine_dirty_rect_state_t *s,
                                      int px, int py,
                                      int w_tiles, int h_tiles,
                                      int force_full)
{
    /* Negative-input clamp on the top-left (same shape as the
     * mark_tile_X wrappers but applied to PIXEL coords, not tile
     * coords).  Retail clamps before computing the bottom-right so
     * the box stays size-correct after the clamp. */
    if (px < 0) px = 0;
    if (py < 0) py = 0;

    engine_dirty_rect_expand(s, px, py,
                             px + w_tiles * 0x10,
                             py + h_tiles * 0x10);

    /* Escalation: force-full-repaint mark.  Retail writes the
     * literal `2` directly to the dirty_mark global, overriding
     * whatever expand() just stamped. */
    if (1 < force_full) {
        s->dirty_mark = 2;
    }
}
