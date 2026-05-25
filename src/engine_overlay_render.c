/*
 * OpenLords2 — strategic-mode OVERLAY pass renderer (implementation).
 *
 * See engine_overlay_render.h for the function map and bindings, and
 * docs/findings/overlay-render.md for the format/topology notes.
 */

#include "engine_overlay_render.h"

#include "engine_dirty_rect.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_text_label.h"

#include <stddef.h>
#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Module state
 * ────────────────────────────────────────────────────────────────── */

/* Tile array (DAT_0055cea0, stride 0x104). */
static const int32_t *g_tile_cells;
static int            g_tile_row_stride_bytes;

/* Per-tile descriptor table (DAT_00522f90 byte-indexed).  The city
 * drawer mutates this (clears bit 0x40 from descriptor[+2] post-
 * dispatch), so the pointer is non-const. */
static uint8_t       *g_tile_descriptors;

/* Per-territory record array (DAT_0052f0b0, slot 0 reserved). */
static const uint8_t *g_territory_records;

/* Overlay (RLE) sprite blob (DAT_0055ce5c). */
static const uint8_t *g_overlay_blob;

/* Per-cell sprite-size short table (DAT_00500c30). */
static const int16_t *g_per_cell_size_table;

/* Camera (DAT_0052afdc, DAT_00553244, DAT_005651b4, DAT_005651b8,
 * DAT_0053e8ac, DAT_0056d67c, DAT_00568220, DAT_0053f65c, DAT_00567950). */
static int g_cam_x0;
static int g_cam_y0;
static int g_cam_col_start;
static int g_cam_row_start;
static int g_cam_col_count;
static int g_cam_row_count;
static int g_cam_x_step;
static int g_cam_y_step;
static int g_cam_x_half_step;

/* Back-buffer canvas (DAT_004ea1a8 / DAT_004eb274 / DAT_004eac4c) —
 * used only by the view-mode-1 pre-clear strip. */
static uint8_t *g_canvas_pixels;
static int      g_canvas_pitch;
static int      g_canvas_height;

/* Debug-text font + char-glyph table (DAT_005af8f0 + char-table). */
static const uint8_t *g_debug_font;
static const uint8_t *g_debug_char_glyph_table;

/* Mode + active-territory + dev flags (DAT_0057cb18, DAT_0057c8c4,
 * DAT_005bb4a4, DAT_005bb4a5). */
static int g_view_mode;
static int g_active_territory;
static int g_dev_cell_value_mode;        /* DAT_005bb4a4 */
static int g_dev_descriptor_value_mode;  /* DAT_005bb4a5 */

/* Dirty-rect singleton binding. */
static engine_dirty_rect_state_t *g_dirty_state;

/* Walker state — retail DAT_0056d594 (cur_row), DAT_0056d598 (cur_col),
 * DAT_0056957c (col_y_accum).  Mutated by the iterators across calls.
 *
 * Each frame the driver resets all three.  Held in this module's
 * statics rather than shared with engine_map_render — retail conflates
 * but the two passes run sequentially, so isolation here doesn't
 * change observable behavior. */
static int g_cur_row;
static int g_cur_col;
static int g_col_y_accum;

/* Current cell scalar (retail DAT_00567954).  Set by the iterators
 * before each drawer dispatch. */
static int g_current_cell;

/* Error counter (retail _DAT_0058fea8). */
static int g_error_count;

/* Test instrumentation. */
static int g_army_call_count;
static int g_city_call_count;
static int g_debug_call_count;

/* ──────────────────────────────────────────────────────────────────
 * Retail constants
 * ────────────────────────────────────────────────────────────────── */

/* Clip Y bounds — same conventions as map_render (view-mode 1 uses a
 * 2-pixel-narrower bottom). */
#define OVERLAY_CLIP_Y_TOP                0x18
#define OVERLAY_CLIP_Y_BOTTOM_VIEW_1      0x1d8
#define OVERLAY_CLIP_Y_BOTTOM_DEFAULT     0x1da
#define OVERLAY_CLIP_X_RIGHT              0x1de

/* Validation gate — retail FUN_004081a6 uses `src_off < 0x4baf1`
 * (= 310001).  Map-render uses `src_off > 310000`.  Functionally
 * identical for integer offsets. */
#define OVERLAY_SRC_OFFSET_MAX            310000

/* Pre-clear strip (view-mode 1) — Y range [0x18, 0x1bc) at column 0,
 * 4 pixels wide each row.  Retail FUN_00405602 lines 11-13. */
#define OVERLAY_PRE_CLEAR_Y_TOP           0x18
#define OVERLAY_PRE_CLEAR_Y_BOTTOM        0x1bc
#define OVERLAY_PRE_CLEAR_WIDTH           4

/* City drawer hard-coded constants. */
#define OVERLAY_CITY_SPRITE_BASE          0x38
#define OVERLAY_CITY_SPRITE_LARGE         0x4e
#define OVERLAY_CITY_X_ANCHOR             0x14
#define OVERLAY_CITY_Y_ANCHOR             6

/* Per-territory record fields the city drawer reads (offsets from
 * `g_territory_records + idx * 0x1a4`). */
#define OVERLAY_TERR_REC_STRIDE           0x1a4
#define OVERLAY_TERR_MAX_SIZE_LO_OFF      0x153
#define OVERLAY_TERR_MAX_SIZE_HI_OFF      0x154

/* Debug-text drawer constants — FUN_00408c50. */
#define OVERLAY_DEBUG_CELL_X_OFFSET       0x18
#define OVERLAY_DEBUG_CELL_Y_OFFSET       10
#define OVERLAY_DEBUG_DESC_Y_OFFSET       (-0x1e)
#define OVERLAY_DEBUG_NEG_COLOR           0xf9
#define OVERLAY_DEBUG_POS_COLOR           0x20
#define OVERLAY_DEBUG_DESC_COLOR          0xf9

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_tile_cells            = cells;
    g_tile_row_stride_bytes = row_stride_bytes;
}

void engine_overlay_render_bind_tile_descriptors(uint8_t *descriptors)
{
    g_tile_descriptors = descriptors;
}

void engine_overlay_render_bind_overlay_blob(const uint8_t *blob)
{
    g_overlay_blob = blob;
}

void engine_overlay_render_bind_territory_records(const uint8_t *records)
{
    g_territory_records = records;
}

void engine_overlay_render_bind_per_cell_size_table(const int16_t *table)
{
    g_per_cell_size_table = table;
}

void engine_overlay_render_bind_camera(int x0, int y0,
                                       int col_start, int row_start,
                                       int col_count, int row_count,
                                       int x_step, int y_step,
                                       int x_half_step)
{
    g_cam_x0          = x0;
    g_cam_y0          = y0;
    g_cam_col_start   = col_start;
    g_cam_row_start   = row_start;
    g_cam_col_count   = col_count;
    g_cam_row_count   = row_count;
    g_cam_x_step      = x_step;
    g_cam_y_step      = y_step;
    g_cam_x_half_step = x_half_step;
}

void engine_overlay_render_bind_canvas(uint8_t *pixels, int pitch, int height)
{
    g_canvas_pixels = pixels;
    g_canvas_pitch  = pitch;
    g_canvas_height = height;
}

void engine_overlay_render_bind_debug_font(const uint8_t *font_base,
                                           const uint8_t *char_glyph_table)
{
    g_debug_font             = font_base;
    g_debug_char_glyph_table = char_glyph_table;
}

void engine_overlay_render_set_dirty_state(engine_dirty_rect_state_t *s) { g_dirty_state = s; }
void engine_overlay_render_set_view_mode(int mode)                       { g_view_mode = mode; }
void engine_overlay_render_set_active_territory(int idx)                 { g_active_territory = idx; }
void engine_overlay_render_set_dev_flags(int cell_v, int desc_v)
{
    g_dev_cell_value_mode       = cell_v;
    g_dev_descriptor_value_mode = desc_v;
}

int  engine_overlay_render_error_count(void)            { return g_error_count; }
void engine_overlay_render_set_error_count(int n)       { g_error_count = n; }

int  engine_overlay_render_army_call_count(void)        { return g_army_call_count; }
int  engine_overlay_render_city_call_count(void)        { return g_city_call_count; }
int  engine_overlay_render_debug_call_count(void)       { return g_debug_call_count; }
void engine_overlay_render_reset_call_counts(void)
{
    g_army_call_count  = 0;
    g_city_call_count  = 0;
    g_debug_call_count = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Internal helpers
 * ────────────────────────────────────────────────────────────────── */

/* Read the int cell at (col, row) of the bound tile array.  Retail
 * formula: `*(int *)(base + col * 4 + row * row_stride_bytes)`. */
static int read_cell(int col, int row)
{
    return *(const int32_t *)((const uint8_t *)g_tile_cells
                              + col * 4
                              + row * g_tile_row_stride_bytes);
}

/* Per-cell size-table index — retail FUN_004081a6 line 18 / FUN_00408c50
 * line 16.  Formula:
 *
 *   `cell_div_8 = (cell + (cell >> 31 & 7)) >> 3`
 *
 * Equivalent to `cell / 8` for non-negative cells; for negative cells
 * adds a 3-bit positive bias so the divide-by-8 rounds toward zero. */
static int per_cell_size_idx(int cell)
{
    /* Replicate `(cell + (cell>>31 & 7)) >> 3` faithfully.  C's `>>`
     * on signed types is implementation-defined for negatives, but the
     * retail compiler uses arithmetic shift here.  Manual mask: */
    int bias = (cell < 0) ? 7 : 0;
    return (cell + bias) >> 3;
}

/* Pre-clear left strip (view-mode 1).  Retail loop:
 *   for (y = 0x18; y < 0x1bc; y++) write 4 zero bytes at (0, y).
 * Equivalent to FUN_004b4056 (4-byte horizontal fill) inlined.
 * No-op if canvas is unbound or Y range is out of bounds. */
static void pre_clear_left_strip(void)
{
    if (g_canvas_pixels == NULL || g_canvas_pitch <= 0) return;
    for (int y = OVERLAY_PRE_CLEAR_Y_TOP; y < OVERLAY_PRE_CLEAR_Y_BOTTOM; y++) {
        if (y < 0 || y >= g_canvas_height) continue;
        uint8_t *row = g_canvas_pixels + y * g_canvas_pitch;
        for (int x = 0; x < OVERLAY_PRE_CLEAR_WIDTH; x++) {
            if (x < g_canvas_pitch) row[x] = 0;
        }
    }
}

/* Apply the clip rect (X = [cam_x0, OVERLAY_CLIP_X_RIGHT],
 * Y = [OVERLAY_CLIP_Y_TOP, OVERLAY_CLIP_Y_BOTTOM_{VIEW_1|DEFAULT}]).
 * Shared by the city drawer (and, in the next chip, the army drawer). */
static void apply_overlay_clip(void)
{
    gfx_dispatch_clip_x(g_cam_x0, OVERLAY_CLIP_X_RIGHT);
    if (g_view_mode == 1) {
        gfx_dispatch_clip_y(OVERLAY_CLIP_Y_TOP,
                            OVERLAY_CLIP_Y_BOTTOM_VIEW_1);
    } else {
        gfx_dispatch_clip_y(OVERLAY_CLIP_Y_TOP,
                            OVERLAY_CLIP_Y_BOTTOM_DEFAULT);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * City drawer — port of FUN_004081a6 @ 0x4081a6 (658 B).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_tile_city(int phase)
{
    g_city_call_count += 1;

    int cell = g_current_cell;
    if (g_tile_descriptors == NULL) return;

    /* Clear bit 0x40 from descriptor[+2] — retail line 13.  This
     * marks the cell as "city already dispatched this frame", so
     * the next bit-0x40 sweep won't re-fire.  The bit is re-set
     * elsewhere when the city changes state. */
    g_tile_descriptors[cell + 2] = (uint8_t)(g_tile_descriptors[cell + 2] & 0xbf);

    /* Compute `large_sprite_select` (= local_14):
     *
     *   `raw = descriptor[+1]`
     *   `large = raw & 0x50`              (bits 0x40 OR 0x10)
     *   if (raw & 0x80) AND (descriptor[+0] != 0x14): large = 1
     *
     * `large != 0` means "draw fixed castle sprite 0x4e instead of
     * size-based sprite".  Retail lines 14-17. */
    uint8_t raw_flag    = g_tile_descriptors[cell + 1];
    uint8_t first_byte  = g_tile_descriptors[cell + 0];
    int     large_sel   = (int)(raw_flag & 0x50);
    if ((raw_flag & 0x80) != 0 && first_byte != 0x14) {
        large_sel = 1;
    }

    /* Per-cell-size-table value, minus 1 (retail line 18).  Capped at
     * 0 when the territory's (max_size_hi - max_size_lo) is smaller. */
    int size_idx = per_cell_size_idx(cell);
    int size_val = 0;
    if (g_per_cell_size_table != NULL) {
        size_val = (int)g_per_cell_size_table[size_idx] - 1;
    }
    if (g_territory_records != NULL && g_active_territory > 0) {
        const uint8_t *terr = g_territory_records
                              + g_active_territory * OVERLAY_TERR_REC_STRIDE;
        int max_lo = (int)(int8_t)terr[OVERLAY_TERR_MAX_SIZE_LO_OFF];
        int max_hi = (int)(int8_t)terr[OVERLAY_TERR_MAX_SIZE_HI_OFF];
        if (max_hi - max_lo < size_val) {
            size_val = 0;
        }
    } else {
        /* No territory binding ⇒ no cap; retail would crash here, but
         * tests can drive the iterators without binding records. */
    }

    int sprite_idx = (large_sel == 0) ? (size_val + OVERLAY_CITY_SPRITE_BASE)
                                      : OVERLAY_CITY_SPRITE_LARGE;

    /* X anchor — retail line 30-33.  Phase 1 shifts by -x_half_step;
     * phases 0/2 use the bare anchor. */
    int x_anchor = OVERLAY_CITY_X_ANCHOR;
    if (phase == 1) {
        x_anchor = OVERLAY_CITY_X_ANCHOR - g_cam_x_half_step;
    }

    /* Stage the descriptor from the overlay blob.  Mirrors retail's
     * inline staging (lines 34-40) via the shared dispatcher helper. */
    if (g_overlay_blob == NULL) return;
    gfx_dispatch_set_last_glyph_index(sprite_idx);
    gfx_dispatch_stage_sprite_descriptor(g_overlay_blob, sprite_idx);

    int src_off = gfx_glyph_glyph_offset();

    /* Validation gate — retail lines 41-44.  src_off < 0 OR src_off
     * >= 0x4baf1 bumps the error counter and skips the dispatch.
     * Retail's `< 0x4baf1` is the same threshold as map_render's
     * `> 310000` (since 0x4baf1 = 310001). */
    if (src_off > OVERLAY_SRC_OFFSET_MAX || src_off < 0) {
        g_error_count += 1;
        return;
    }

    /* Save cursor, apply anchor offsets, mark dirty rect, clip,
     * dispatch RLE, restore cursor.  Retail lines 46-71. */
    int saved_x = gfx_glyph_cursor_x();
    int saved_y = gfx_glyph_cursor_y();

    int new_x = saved_x + x_anchor;
    int new_y = saved_y + OVERLAY_CITY_Y_ANCHOR;
    gfx_glyph_set_dest(new_x, new_y);

    engine_dirty_rect_mark_tile_32(g_dirty_state, new_x >> 4, new_y >> 4);

    apply_overlay_clip();

    gfx_dispatch_dispatch_sprite_rle(g_overlay_blob);

    /* Restore cursor for the next cell. */
    gfx_glyph_set_dest(saved_x, saved_y);
}

/* ──────────────────────────────────────────────────────────────────
 * Army drawer — STUB port of FUN_004071a0 @ 0x4071a0 (3179 B).
 *
 * The real port lands in the next chip.  This stub bumps a per-call
 * counter so the row iterators can be tested for dispatch fidelity.
 * Signature matches the final port.
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_tile_army(int phase)
{
    (void)phase;
    g_army_call_count += 1;
}

/* ──────────────────────────────────────────────────────────────────
 * Debug overlay text — port of FUN_00408c50 @ 0x408c50 (390 B).
 *
 * Two mutually exclusive modes:
 *
 *   - `descriptor_value_mode != 0` (winner): draws the per-tile
 *     descriptor's head-link byte (+5) at (cursor_x, cursor_y - 0x1e)
 *     with color 0xf9.  Skips draw if head-link is 0.
 *   - `cell_value_mode != 0` AND `descriptor_value_mode == 0`: draws
 *     the per-cell-size table value (signed) at (cursor_x + 0x18,
 *     cursor_y + 10).  Color is 0xf9 if value is negative (with
 *     absolute value shown), else 0x20 (with positive value).
 *
 * Both branches save/restore the cursor.  Both no-op when the cell
 * is >= EMPTY (out of bounds).  Both routed through
 * gfx_text_label_render_dec.
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_debug_text(void)
{
    g_debug_call_count += 1;

    /* Mode 2 wins (retail's outer-if condition tests descriptor_value
     * first; cell_value path is the else branch). */
    if (g_dev_descriptor_value_mode == 0) {
        /* Mode 1 — cell value rendered. */
        if (g_dev_cell_value_mode == 0) return;
        if ((uint32_t)g_current_cell >= ENGINE_OVERLAY_RENDER_CELL_EMPTY) return;
        if (g_per_cell_size_table == NULL) return;

        int saved_x = gfx_glyph_cursor_x();
        int saved_y = gfx_glyph_cursor_y();

        int idx       = per_cell_size_idx(g_current_cell);
        int raw_value = (int)g_per_cell_size_table[idx];
        int value     = raw_value;
        uint8_t color = OVERLAY_DEBUG_POS_COLOR;
        if (value < 0) {
            value = -value;
            color = OVERLAY_DEBUG_NEG_COLOR;
        }

        gfx_text_label_render_dec(value,
                                  ' ',
                                  (const char *)"",
                                  saved_x + OVERLAY_DEBUG_CELL_X_OFFSET,
                                  saved_y + OVERLAY_DEBUG_CELL_Y_OFFSET,
                                  g_debug_font,
                                  g_debug_char_glyph_table,
                                  color);

        gfx_glyph_set_dest(saved_x, saved_y);
        return;
    }

    /* Mode 2 — descriptor head-link byte rendered. */
    if ((uint32_t)g_current_cell >= ENGINE_OVERLAY_RENDER_CELL_EMPTY) return;
    if (g_tile_descriptors == NULL) return;

    uint8_t head_link = g_tile_descriptors[g_current_cell + 5];
    if (head_link == 0) return;

    int saved_x = gfx_glyph_cursor_x();
    int saved_y = gfx_glyph_cursor_y();

    gfx_text_label_render_dec((int)head_link,
                              ' ',
                              (const char *)"",
                              saved_x,
                              saved_y + OVERLAY_DEBUG_DESC_Y_OFFSET,
                              g_debug_font,
                              g_debug_char_glyph_table,
                              OVERLAY_DEBUG_DESC_COLOR);

    gfx_glyph_set_dest(saved_x, saved_y);
}

/* ──────────────────────────────────────────────────────────────────
 * Per-cell dispatch — invoked by all three iterators.
 *
 * Tests bit 0x80 (army) and bit 0x40 (city) of descriptor[+2].  Both
 * may fire on the same cell.  `phase` is forwarded to each drawer.
 * ────────────────────────────────────────────────────────────────── */
static void dispatch_cell(int phase)
{
    if (g_tile_descriptors == NULL) return;
    uint8_t desc_flag = g_tile_descriptors[g_current_cell + 2];
    if ((desc_flag & 0x80) != 0) {
        engine_overlay_render_tile_army(phase);
    }
    if ((desc_flag & 0x40) != 0) {
        engine_overlay_render_tile_city(phase);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Phase-0 row iterator — port of FUN_00405eb5 @ 0x405eb5 (247 B).
 *
 * `col_count` cells, all phase 0, each with a between-cell debug
 * overlay call.  No left/right edge extras (unlike the primary
 * pass's phase0 row).  Advances cur_row, cursor_y, col_y_accum.
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_row_phase0(void)
{
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_col = g_cam_col_start;

    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = read_cell(g_cur_col, g_cur_row);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
            dispatch_cell(0);
            engine_overlay_render_debug_text();
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
        } else {
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
        }
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row     += 1;
    g_col_y_accum += g_cam_y_step;
}

/* ──────────────────────────────────────────────────────────────────
 * Phase-1 row iterator — port of FUN_00405fac @ 0x405fac (497 B).
 *
 * First cell phase 1 (no debug text), then col_count - 1 middle cells
 * phase 0 (WITH debug text), then one trailing cell phase 2 (no debug
 * text).  Total cells visited: col_count + 1.
 *
 * Advances cur_row, cursor_y, col_y_accum.
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_row_phase1(void)
{
    /* First cell — phase 1, no debug text. */
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_current_cell = read_cell(g_cam_col_start, g_cur_row);
    g_cur_col = g_cam_col_start + 1;
    if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
        dispatch_cell(1);
    }
    /* Half-step X advance — UNCONDITIONAL.  Retail line 19. */
    gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_half_step);

    /* Middle cells — phase 0, WITH debug text. */
    for (int c = 0; c < g_cam_col_count - 1; c++) {
        g_current_cell = read_cell(g_cur_col, g_cur_row);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
            dispatch_cell(0);
            engine_overlay_render_debug_text();
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
        } else {
            gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
        }
    }

    /* Trailing cell — phase 2, no debug text, NO X advance. */
    g_current_cell = read_cell(g_cur_col, g_cur_row);
    g_cur_col += 1;
    if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
        dispatch_cell(2);
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row     += 1;
    g_col_y_accum += g_cam_y_step;
}

/* ──────────────────────────────────────────────────────────────────
 * Outer frame driver — port of FUN_00405602 @ 0x405602 (608 B).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_frame(void)
{
    /* View-mode-1 left-strip pre-clear (retail lines 10-14). */
    if (g_view_mode == 1) {
        pre_clear_left_strip();
    }

    /* Reset cursor + walker state. */
    gfx_glyph_set_cursor_y(g_cam_y0);
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_row     = g_cam_row_start;
    g_col_y_accum = 0;
    g_cur_col     = g_cam_col_start;

    /* Top phantom row — col_count cells, phase 0, NO debug text.
     * Cursor walks the normal X stride unconditionally (retail
     * lines 20-35). */
    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = read_cell(g_cur_col, g_cur_row);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
            dispatch_cell(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_cam_y_step);
    g_cur_row += 1;

    /* One phase-1 row, then alternating pairs. */
    engine_overlay_render_row_phase1();

    int pair_count = (g_cam_row_count - 2) / 2;
    for (int r = 0; r < pair_count; r++) {
        engine_overlay_render_row_phase0();
        engine_overlay_render_row_phase1();
    }

    /* Bottom phantom row — mirrors top.  cur_row left over from the
     * pair loop's last phase1_row advance.  Retail lines 43-60. */
    gfx_glyph_set_cursor_x(g_cam_x0);
    g_cur_col = g_cam_col_start;

    for (int c = 0; c < g_cam_col_count; c++) {
        g_current_cell = read_cell(g_cur_col, g_cur_row);
        g_cur_col += 1;
        if ((uint32_t)g_current_cell < ENGINE_OVERLAY_RENDER_CELL_EMPTY) {
            dispatch_cell(0);
        }
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_cam_x_step);
    }

    /* Final state cleanup — retail line 61-63.  Note: col_y_accum is
     * reset to 0 again at the end of the frame, but cur_row is NOT
     * reset (it advances past the bottom phantom row's row).  This
     * mirrors retail; the next frame's driver re-binds these anyway. */
    g_col_y_accum = 0;
}
