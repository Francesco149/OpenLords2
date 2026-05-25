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

/* Per-army record array (DAT_0053f9b0, slot 0 reserved). */
static const uint8_t *g_army_records;

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

/* Army-drawer state (DAT_0053f264, DAT_0057d390, _DAT_0057d38c,
 * DAT_0053e9a4, and 4 rate tier flags). */
static int g_render_mode;                /* DAT_0053f264 — dev-mode filter */
static int g_overlay_base_index;         /* DAT_0057d390 */
static int g_arrow_marker_base_index;    /* _DAT_0057d38c */
static int g_animation_tick;             /* DAT_0053e9a4 — [0..0xf] cycle */
static int g_anim_rate_tier_0;           /* DAT_0058fd08 (< 10) */
static int g_anim_rate_tier_1;           /* DAT_0057d3c8 (< 0x19) */
static int g_anim_rate_tier_2;           /* DAT_0057d3ac (< 0x32) */
static int g_anim_rate_tier_3;           /* DAT_00591510 (≥ 0x32) */

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

/* Army drawer constants — FUN_004071a0.
 *
 * src_off gate is TIGHTER than the primary pass's and the city
 * drawer's.  Retail: `src_off < 0x11171` (= 70001) AND `src_off ≥ 1`.
 * Sprites past offset 70000 of the overlay blob are flagged as
 * errors.  The primary pass uses 310000 — see quirk #71. */
#define OVERLAY_ARMY_SRC_OFFSET_MAX       70000
#define OVERLAY_ARMY_TICK_MAX             0xf
#define OVERLAY_ARMY_TICK_BASE_SPRITE     0x28
#define OVERLAY_ARMY_TIER_THRESHOLD       3   /* tier-byte ≥ 3 → dispatch */

#define OVERLAY_ARMY_REC_STRIDE           0x300
#define OVERLAY_ARMY_REC_FORTRESS_OFF     0x07
#define OVERLAY_ARMY_REC_MARCH_OUT_OFF    0x1ad
#define OVERLAY_ARMY_REC_TERRITORY_LINK_OFF  0x1bc

/* The four "category band" offsets — band B uses (cur,prev,tier). */
#define OVERLAY_ARMY_BAND0_CUR_OFF        0x2b8
#define OVERLAY_ARMY_BAND0_PREV_OFF       0x2bc
#define OVERLAY_ARMY_BAND0_TIER_OFF       0x2ae
#define OVERLAY_ARMY_BAND1_CUR_OFF        0x2e8
#define OVERLAY_ARMY_BAND1_PREV_OFF       0x2ec
#define OVERLAY_ARMY_BAND1_TIER_OFF       0x2de
#define OVERLAY_ARMY_BAND2_CUR_OFF        0x2d0
#define OVERLAY_ARMY_BAND2_PREV_OFF       0x2d4
#define OVERLAY_ARMY_BAND2_TIER_OFF       0x2c6
#define OVERLAY_ARMY_BAND3_CUR_OFF        0x2a0
#define OVERLAY_ARMY_BAND3_PREV_OFF       0x2a4
#define OVERLAY_ARMY_BAND3_TIER_OFF       0x296

/* Per-territory fields the army-label sub-renderer reads. */
#define OVERLAY_ARMY_TERR_OWNER_OFF       0x02
#define OVERLAY_ARMY_TERR_NEXT_OFF        0x19a
#define OVERLAY_ARMY_TERR_LABEL_OFF       0x19c

/* End-frame transition values (cat 3, 6, 9, 12) written to
 * descriptor[+3] BEFORE the tier+tick gating decides whether to
 * dispatch.  See quirk: end-frame is committed even when the
 * dispatch is suppressed. */
#define OVERLAY_ARMY_END_FRAME_BAND0      0x2e
#define OVERLAY_ARMY_END_FRAME_BAND1      0x09
#define OVERLAY_ARMY_END_FRAME_BAND2      0x13
#define OVERLAY_ARMY_END_FRAME_BAND3      0x1d

/* Step categories' frame-wrap parameters: `if (++frame > LIMIT)
 * frame = WRAP_TO`.  Note BAND0 covers TWO step categories (0 and 2)
 * — both wrap with the same limit/target. */
#define OVERLAY_ARMY_STEP_LIMIT_BAND0     0x2d
#define OVERLAY_ARMY_STEP_WRAP_BAND0      0x1e
#define OVERLAY_ARMY_STEP_LIMIT_BAND1     0x04
#define OVERLAY_ARMY_STEP_WRAP_BAND1      0x01
#define OVERLAY_ARMY_STEP_LIMIT_BAND2     0x12
#define OVERLAY_ARMY_STEP_WRAP_BAND2      0x0a
#define OVERLAY_ARMY_STEP_LIMIT_BAND3     0x1c
#define OVERLAY_ARMY_STEP_WRAP_BAND3      0x14

/* Arrow-marker branch limits.  bVar2 must be in [0x10, 0x16] AND
 * either (< 0x13 AND bVar2-0x10 ≤ 2) OR (≥ 0x13 AND bVar2-0x14 ≤ 2)
 * — together implying bVar2 ∈ {0x10, 0x11, 0x12} ∪ {0x14, 0x15, 0x16}.
 * Note 0x13 is the explicit split; values 0x13 itself fall into the
 * upper sub-branch but (0x13 - 0x14) wraps to 0xff > 2 so it skips. */
#define OVERLAY_ARMY_ARROW_LO_BASE_ADD    0x67
#define OVERLAY_ARMY_ARROW_HI_BASE_ADD    0x55
#define OVERLAY_ARMY_ARROW_PER_INDEX_MULT 6

/* Sprite indices used by specific 0x40 path. */
#define OVERLAY_ARMY_MARCH_OUT_SPRITE     0x81

/* Label sub-renderer (FUN_00407f82) — fixed sprite + (sprite_w-
 * centered) decimal label.  Only fires in view_mode == 0. */
#define OVERLAY_ARMY_LABEL_SPRITE         0x82
#define OVERLAY_ARMY_LABEL_SRC_OFFSET_MAX 310000  /* 0x4baf1 */
#define OVERLAY_ARMY_LABEL_TEXT_Y_OFFSET  10
#define OVERLAY_ARMY_LABEL_DIRTY_X_BUMP   4
#define OVERLAY_ARMY_LABEL_TEXT_COLOR     0xf9
#define OVERLAY_ARMY_LABEL_TOP_CLIP_Y     0x18

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

void engine_overlay_render_bind_army_records(const uint8_t *records)
{
    g_army_records = records;
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

void engine_overlay_render_set_render_mode(int mode)                     { g_render_mode = mode; }
void engine_overlay_render_set_overlay_base_index(int base)              { g_overlay_base_index = base; }
void engine_overlay_render_set_arrow_marker_base_index(int base)         { g_arrow_marker_base_index = base; }
void engine_overlay_render_set_animation_tick(int tick)                  { g_animation_tick = tick; }
void engine_overlay_render_set_animation_rate_flags(int t0, int t1, int t2, int t3)
{
    g_anim_rate_tier_0 = t0;
    g_anim_rate_tier_1 = t1;
    g_anim_rate_tier_2 = t2;
    g_anim_rate_tier_3 = t3;
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
 * Army drawer — port of FUN_004071a0 @ 0x4071a0 (3179 B).
 *
 * Top-level dev filter: skip cell when `render_mode == 1` AND
 * descriptor[+2]&0x20 is clear.  Then dispatch on `descriptor[+1]`
 * flag bits in priority order (0x40 > 0x10 > 0x20 > 0x80-only):
 *
 *  - 0x40 → fortress/march-out path, gated on descriptor[+4]&0xf
 *           ∈ {0, 2}.
 *  - 0x10 → animation-end path (descriptor[+0] == 0x13 required).
 *  - 0x20 → directional-arrow path (descriptor[+0] ∈ {0x10..0x12,
 *           0x14..0x16}).
 *  - 0x80-only → unit-category path on descriptor[+0]:
 *      0..12  : per-band animation state machine (chase/fight/march/
 *               die — labels tentative).  Step categories WRITE BACK
 *               new frame to descriptor[+3] and RETURN; end-frame
 *               categories conditionally dispatch.
 *      13..0x19 (excl. 0x14): "large army at territory" sprite plus
 *               an optional label sub-render (via tile_army_label).
 *
 * Common dispatch tail (when any branch sets a sprite_idx and falls
 * through): stages descriptor, validates src_off ∈ [1, 70000]
 * (tighter than the primary pass's [1, 310000] — quirk #71), shifts
 * X anchor by -x_half_step when phase==1, marks dirty rect with the
 * branch-selected kind (32-px for 1, 48-px for 2), applies the clip,
 * dispatches via the RLE family.
 *
 * Stale-sprite quirk: the 0x40 branch with descriptor[+4]&0xf NOT in
 * {0,2} sets no sprite_idx, no offsets, but still falls into the
 * dispatch tail — dispatching whatever sprite the dispatcher last
 * staged.  See quirk #70.
 *
 * Tier-byte "frame committed even when skipped": end-frame categories
 * (3, 6, 9, 12) and the 0x10 path WRITE the new frame to descriptor
 * [+3] before checking the tier-byte / tick gates.  A failed gate
 * still leaves the frame committed.  See quirk #72.
 * ────────────────────────────────────────────────────────────────── */

/* Internal: load 32-bit signed int from per-army record `army_idx`'s
 * field at offset `field_off`.  Returns 0 when records unbound or
 * army_idx == 0 (reserved slot). */
static int32_t army_rec_i32(int army_idx, int field_off)
{
    if (g_army_records == NULL || army_idx == 0) return 0;
    const uint8_t *p = g_army_records + army_idx * OVERLAY_ARMY_REC_STRIDE + field_off;
    return (int32_t)((uint32_t)p[0]
                   | ((uint32_t)p[1] << 8)
                   | ((uint32_t)p[2] << 16)
                   | ((uint32_t)p[3] << 24));
}

/* Internal: load byte from per-army record `army_idx`'s field at
 * offset `field_off`.  Returns 0 when records unbound. */
static uint8_t army_rec_u8(int army_idx, int field_off)
{
    if (g_army_records == NULL) return 0;
    return g_army_records[army_idx * OVERLAY_ARMY_REC_STRIDE + field_off];
}

/* Internal: load byte from per-territory record `terr_idx`'s field
 * at offset `field_off`.  Returns 0 when records unbound. */
static uint8_t territory_rec_u8(int terr_idx, int field_off)
{
    if (g_territory_records == NULL) return 0;
    return g_territory_records[terr_idx * OVERLAY_TERR_REC_STRIDE + field_off];
}

/* Pick the animation-rate tier flag matching `distance`.  Mirrors the
 * if/else-if chain repeated in retail at every step category. */
static int pick_rate_tier(int distance)
{
    if (distance < 10)        return g_anim_rate_tier_0;
    else if (distance < 0x19) return g_anim_rate_tier_1;
    else if (distance < 0x32) return g_anim_rate_tier_2;
    else                      return g_anim_rate_tier_3;
}

/* Per-band step shape (cat 0/2/5/8/11): read cur/prev pair, pick tier,
 * step frame, wrap.  Always writes back and returns — never dispatches. */
static void army_step_band(int army_idx,
                           int cur_off, int prev_off,
                           int frame_limit, int frame_wrap_to)
{
    int distance = army_rec_i32(army_idx, cur_off)
                 - army_rec_i32(army_idx, prev_off);
    int rate     = pick_rate_tier(distance);

    int frame    = g_tile_descriptors[g_current_cell + 3];
    if (rate != 0) {
        frame += 1;
        if (frame > frame_limit) frame = frame_wrap_to;
    }
    g_tile_descriptors[g_current_cell + 3] = (uint8_t)frame;
}

/* Per-band end-frame shape (cat 3/6/9/12): commit end-frame to
 * descriptor[+3], then return non-zero with sprite_idx if the
 * tier+tick gates pass (caller falls through to dispatch).  Returns
 * 0 when gates fail (caller returns without dispatch). */
static int army_end_frame_band(int army_idx, int end_frame, int tier_off,
                               int *out_sprite_idx,
                               int *out_dx, int *out_dy, int *out_kind)
{
    /* Commit end-frame regardless of gate outcome (quirk #72). */
    g_tile_descriptors[g_current_cell + 3] = (uint8_t)end_frame;

    int8_t tier = (int8_t)army_rec_u8(army_idx, tier_off);
    if (tier < OVERLAY_ARMY_TIER_THRESHOLD)        return 0;
    if (g_animation_tick > OVERLAY_ARMY_TICK_MAX)  return 0;

    *out_sprite_idx = g_animation_tick + OVERLAY_ARMY_TICK_BASE_SPRITE;
    *out_dx         = 0xc;
    *out_dy         = -0xe;
    *out_kind       = 1;
    return 1;
}

/* Label sub-renderer — port of FUN_00407f82 @ 0x407f82 (548 B).
 *
 * View-mode-0-only.  Stages sprite 0x82 from the overlay blob,
 * validates src_off ∈ [1, 310000] (LOOSER than the army drawer's
 * dispatch tail — quirk #71), dispatches via the RLE family with
 * the view-0 clip, then draws a centered decimal label below
 * (using the same font as the debug-text helper).  Marks dirty
 * rect at (cursor + (dx + 4, dy + 10)) post-render.
 *
 * Parameters mirror retail: `label_value` is the integer drawn
 * below the sprite, `dx` / `dy` are the cursor offsets applied
 * before staging.  Cursor is saved/restored faithfully. */
static void army_render_label(int label_value, int dx, int dy)
{
    if (g_view_mode != 0)           return;
    if (g_overlay_blob == NULL)     return;

    int saved_x = gfx_glyph_cursor_x();
    int saved_y = gfx_glyph_cursor_y();
    gfx_glyph_set_dest(saved_x + dx, saved_y + dy);

    gfx_dispatch_set_last_glyph_index(OVERLAY_ARMY_LABEL_SPRITE);
    gfx_dispatch_stage_sprite_descriptor(g_overlay_blob,
                                         OVERLAY_ARMY_LABEL_SPRITE);

    int src_off = gfx_glyph_glyph_offset();
    if (src_off >= OVERLAY_ARMY_LABEL_SRC_OFFSET_MAX) {
        g_error_count += 1;
        gfx_glyph_set_dest(saved_x, saved_y);
        return;
    }
    if (src_off < 0) {
        g_error_count += 1;
        gfx_glyph_set_dest(saved_x, saved_y);
        return;
    }

    apply_overlay_clip();
    gfx_dispatch_dispatch_sprite_rle(g_overlay_blob);

    /* Restore cursor BEFORE the text label render (the label is
     * positioned absolutely from the saved cursor, not the bumped
     * one).  Mirrors retail lines 43-44. */
    gfx_glyph_set_dest(saved_x, saved_y);

    /* Text label — skipped when the label Y is at/above the top
     * clip (retail line 47: `0x18 < saved_y + dy + 10`). */
    int label_y = saved_y + dy + OVERLAY_ARMY_LABEL_TEXT_Y_OFFSET;
    if (label_y > OVERLAY_ARMY_LABEL_TOP_CLIP_Y
        && g_debug_font != NULL && g_debug_char_glyph_table != NULL) {
        int sprite_w = gfx_glyph_glyph_w();
        gfx_text_label_render_dec_centered(label_value,
                                           ' ',
                                           (const char *)" ",
                                           saved_x + dx,
                                           label_y,
                                           sprite_w,
                                           g_debug_font,
                                           g_debug_char_glyph_table,
                                           g_debug_char_glyph_table,
                                           OVERLAY_ARMY_LABEL_TEXT_COLOR);
    }

    /* Dirty-rect mark — at (cursor + dx + 4, cursor + dy + 10),
     * even though the cursor is already restored.  Retail line 50
     * reads DAT_00591524 / DAT_00591528 AFTER restoring (i.e. uses
     * the saved coords). */
    engine_dirty_rect_mark_tile_32(g_dirty_state,
                                   (saved_x + dx + OVERLAY_ARMY_LABEL_DIRTY_X_BUMP) >> 4,
                                   (saved_y + dy + OVERLAY_ARMY_LABEL_TEXT_Y_OFFSET) >> 4);
}

/* Apply the army drawer's dispatch tail (shared by every code path
 * that sets a sprite_idx).  Replicates the retail lines 295-352. */
static void army_dispatch_tail(int phase, int sprite_idx,
                               int dx, int dy, int kind)
{
    if (phase == 1) dx -= g_cam_x_half_step;

    if (g_overlay_blob == NULL) {
        g_error_count += 1;
        return;
    }

    gfx_dispatch_set_last_glyph_index(sprite_idx);
    gfx_dispatch_stage_sprite_descriptor(g_overlay_blob, sprite_idx);

    int src_off = gfx_glyph_glyph_offset();
    if (src_off > OVERLAY_ARMY_SRC_OFFSET_MAX) {
        g_error_count += 1;
        return;
    }
    if (src_off < 1) {
        g_error_count += 1;
        return;
    }

    int saved_x = gfx_glyph_cursor_x();
    int saved_y = gfx_glyph_cursor_y();
    int new_x   = saved_x + dx;
    int new_y   = saved_y + dy;
    gfx_glyph_set_dest(new_x, new_y);

    if (kind == 1) {
        engine_dirty_rect_mark_tile_32(g_dirty_state, new_x >> 4, new_y >> 4);
    } else if (kind == 2) {
        engine_dirty_rect_mark_tile_48(g_dirty_state, new_x >> 4, new_y >> 4);
    }

    apply_overlay_clip();
    gfx_dispatch_dispatch_sprite_rle(g_overlay_blob);

    gfx_glyph_set_dest(saved_x, saved_y);
}

void engine_overlay_render_tile_army(int phase)
{
    g_army_call_count += 1;

    if (g_tile_descriptors == NULL) return;
    int cell = g_current_cell;

    /* Top-level dev-mode filter (retail line 20). */
    uint8_t disp_flag = g_tile_descriptors[cell + 2];
    if (g_render_mode == 1 && (disp_flag & 0x20) == 0) return;

    int dx        = 0;
    int dy        = 0;
    int kind      = 0;            /* dirty-rect mark kind: 0/1/2 */
    int sprite_idx_valid = 0;
    int sprite_idx       = 0;

    uint8_t raw_flag = g_tile_descriptors[cell + 1];
    if ((raw_flag & 0xf0) == 0) return;

    uint8_t  first_byte = g_tile_descriptors[cell + 0];
    uint8_t  army_byte  = g_tile_descriptors[cell + 7];
    int      army_idx   = army_byte;

    if ((raw_flag & 0x40) == 0) {
        if ((raw_flag & 0x10) == 0) {
            if ((raw_flag & 0x20) == 0) {
                /* ────── bVar1 = 0x80 only ────── */
                if (first_byte < 13) {
                    /* Unit-category state machine (cat 0..12). */
                    if (g_view_mode == 2) return;

                    if (first_byte < 4) {
                        if (first_byte != 3) {
                            if (first_byte == 1) return;
                            /* cat 0 or 2 — step BAND0. */
                            army_step_band(army_idx,
                                           OVERLAY_ARMY_BAND0_CUR_OFF,
                                           OVERLAY_ARMY_BAND0_PREV_OFF,
                                           OVERLAY_ARMY_STEP_LIMIT_BAND0,
                                           OVERLAY_ARMY_STEP_WRAP_BAND0);
                            return;
                        }
                        /* cat 3 — end-frame BAND0. */
                        if (!army_end_frame_band(army_idx,
                                                 OVERLAY_ARMY_END_FRAME_BAND0,
                                                 OVERLAY_ARMY_BAND0_TIER_OFF,
                                                 &sprite_idx, &dx, &dy, &kind)) {
                            return;
                        }
                        sprite_idx_valid = 1;
                    } else if (first_byte < 7) {
                        if (first_byte != 6) {
                            if (first_byte == 4) return;
                            /* cat 5 — step BAND1. */
                            army_step_band(army_idx,
                                           OVERLAY_ARMY_BAND1_CUR_OFF,
                                           OVERLAY_ARMY_BAND1_PREV_OFF,
                                           OVERLAY_ARMY_STEP_LIMIT_BAND1,
                                           OVERLAY_ARMY_STEP_WRAP_BAND1);
                            return;
                        }
                        /* cat 6 — end-frame BAND1. */
                        if (!army_end_frame_band(army_idx,
                                                 OVERLAY_ARMY_END_FRAME_BAND1,
                                                 OVERLAY_ARMY_BAND1_TIER_OFF,
                                                 &sprite_idx, &dx, &dy, &kind)) {
                            return;
                        }
                        sprite_idx_valid = 1;
                    } else if (first_byte < 10) {
                        if (first_byte != 9) {
                            if (first_byte == 7) return;
                            /* cat 8 — step BAND2. */
                            army_step_band(army_idx,
                                           OVERLAY_ARMY_BAND2_CUR_OFF,
                                           OVERLAY_ARMY_BAND2_PREV_OFF,
                                           OVERLAY_ARMY_STEP_LIMIT_BAND2,
                                           OVERLAY_ARMY_STEP_WRAP_BAND2);
                            return;
                        }
                        /* cat 9 — end-frame BAND2. */
                        if (!army_end_frame_band(army_idx,
                                                 OVERLAY_ARMY_END_FRAME_BAND2,
                                                 OVERLAY_ARMY_BAND2_TIER_OFF,
                                                 &sprite_idx, &dx, &dy, &kind)) {
                            return;
                        }
                        sprite_idx_valid = 1;
                    } else {
                        /* first_byte in {10, 11, 12}. */
                        if (first_byte != 12) {
                            if (first_byte == 10) return;
                            /* cat 11 — step BAND3. */
                            army_step_band(army_idx,
                                           OVERLAY_ARMY_BAND3_CUR_OFF,
                                           OVERLAY_ARMY_BAND3_PREV_OFF,
                                           OVERLAY_ARMY_STEP_LIMIT_BAND3,
                                           OVERLAY_ARMY_STEP_WRAP_BAND3);
                            return;
                        }
                        /* cat 12 — end-frame BAND3. */
                        if (!army_end_frame_band(army_idx,
                                                 OVERLAY_ARMY_END_FRAME_BAND3,
                                                 OVERLAY_ARMY_BAND3_TIER_OFF,
                                                 &sprite_idx, &dx, &dy, &kind)) {
                            return;
                        }
                        sprite_idx_valid = 1;
                    }
                } else {
                    /* first_byte ≥ 13: "large army at territory" branch. */
                    if (first_byte > 0x19) return;
                    if (first_byte == 0x14) return;
                    int terr_idx = army_rec_i32(army_idx,
                                                OVERLAY_ARMY_REC_TERRITORY_LINK_OFF);
                    if (terr_idx == 0) return;
                    int next_byte = (int)territory_rec_u8(terr_idx,
                                                          OVERLAY_ARMY_TERR_NEXT_OFF);
                    if (next_byte != 0) {
                        int label = (int)territory_rec_u8(next_byte,
                                                          OVERLAY_ARMY_TERR_LABEL_OFF);
                        if (g_view_mode == 0) {
                            army_render_label(label, 8, -0x38);
                        } else if (g_view_mode == 2) {
                            army_render_label(label, 2, -0x28);
                        }
                    }
                    kind = 2;
                    if (g_view_mode == 0) {
                        dx = 0x1a; dy = -0x1c;
                    } else if (g_view_mode == 2) {
                        dx = 6;    dy = -0x15;
                    }
                    int owner = (int)territory_rec_u8(terr_idx,
                                                      OVERLAY_ARMY_TERR_OWNER_OFF);
                    sprite_idx = owner * 8 - 8 + g_overlay_base_index;
                    sprite_idx_valid = 1;
                }
            } else {
                /* ────── bVar1 = 0x20 (and not 0x10, 0x40) — arrow. ────── */
                if (g_view_mode == 2) return;
                if (first_byte < 0xf || first_byte > 0x16) return;
                kind = 2;
                if (first_byte < 0x13) {
                    int delta = first_byte - 0x10;
                    if (delta < 0 || delta > 2) return;
                    sprite_idx = delta * OVERLAY_ARMY_ARROW_PER_INDEX_MULT
                               + g_arrow_marker_base_index
                               + OVERLAY_ARMY_ARROW_LO_BASE_ADD;
                } else {
                    dx = 4; dy = -4;
                    int delta = first_byte - 0x14;
                    /* Retail: `if (2 < (byte)(bVar2 - 0x14)) return;` —
                     * (byte) cast of negative wraps to 0xff which is > 2,
                     * so 0x13 (delta = -1 ⇒ 0xff) is filtered out here. */
                    if (delta < 0 || delta > 2) return;
                    sprite_idx = delta * OVERLAY_ARMY_ARROW_PER_INDEX_MULT
                               + g_arrow_marker_base_index
                               + OVERLAY_ARMY_ARROW_HI_BASE_ADD;
                }
                sprite_idx_valid = 1;
            }
        } else {
            /* ────── bVar1 = 0x10 (and not 0x40) — animation-end. ────── */
            if (g_view_mode == 2) return;
            if (first_byte != 0x13) return;
            if (g_animation_tick > OVERLAY_ARMY_TICK_MAX) return;
            sprite_idx = g_animation_tick + OVERLAY_ARMY_TICK_BASE_SPRITE;
            dx = 0xc; dy = -0xe; kind = 1;
            sprite_idx_valid = 1;
        }
    } else {
        /* ────── bVar1 = 0x40 — fortress / march-out. ────── */
        uint8_t disp4 = g_tile_descriptors[cell + 4];
        uint8_t nibble = (uint8_t)(disp4 & 0xf);
        if (nibble == 0) {
            uint8_t fortress = army_rec_u8(army_idx, OVERLAY_ARMY_REC_FORTRESS_OFF);
            if (fortress == 0) return;
            kind = 2;
            if (g_view_mode == 0)      { dx = 0x1a; dy = -0x1c; }
            else if (g_view_mode == 2) { dx = 6;    dy = -0x15; }
            sprite_idx = (int)fortress * 8 - 8 + g_overlay_base_index;
            sprite_idx_valid = 1;
        } else if (nibble == 2) {
            uint8_t mout = army_rec_u8(army_idx, OVERLAY_ARMY_REC_MARCH_OUT_OFF);
            if (mout == 0) return;
            kind = 0;
            if (g_view_mode == 0)      { dx = 0x10; dy = -0x12; }
            else if (g_view_mode == 2) { dx = 6;    dy = -0x15; }
            sprite_idx = OVERLAY_ARMY_MARCH_OUT_SPRITE;
            sprite_idx_valid = 1;
        }
        /* Other low-nibble values: fall through to the dispatch tail
         * with no sprite_idx set — uses whatever the dispatcher last
         * staged.  See quirk #70. */
    }

    /* Common dispatch tail.  Faithful to retail: the tail runs even
     * when sprite_idx wasn't set in the bVar1 = 0x40 fall-through —
     * in that path retail dispatches whatever DAT_005c9288 already
     * held from the most-recent staging.  See quirk #70 — we pull
     * the stale index from the dispatcher's `last_glyph_index`. */
    if (!sprite_idx_valid) {
        sprite_idx = gfx_dispatch_last_glyph_index();
    }
    army_dispatch_tail(phase, sprite_idx, dx, dy, kind);
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
