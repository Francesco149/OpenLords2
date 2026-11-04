/*
 * OpenLords2 — strategic-mode OVERLAY pass renderer (pass 2: armies, cities, the dev overlay).
 *
 * See engine_overlay_render.h for the function map and bindings, docs/findings/overlay-render.md for the
 * format / topology notes and docs/findings/render-mapview.md for the retail-layout move.
 *
 * Retail layout: the camera / walker / modes / animation phases / error counter are the shared retail-layout
 * globals (engine_mapview_globals.h), the staged descriptor + cursor + its save slot the gfx layer's
 * (gfx_globals.h).  Every function follows retail's control flow and global writes (IDA pseudocode + asm).
 */

#include "engine_overlay_render.h"

#include "engine_dirty_rect.h"
#include "engine_mapview_globals.h"
#include "gfx_dispatch.h"
#include "gfx_globals.h"
#include "gfx_sprite_rle.h"
#include "gfx_text_label.h"
#include "save.h"

#include <stddef.h>
#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Port bindings (not retail globals)
 * ────────────────────────────────────────────────────────────────── */

static const int32_t *g_tile_cells;                                /* DAT_0055cea0 */
static int            g_tile_row_stride_bytes = 0x104;
static uint8_t       *g_tile_descriptors = g_overworld_map;        /* DAT_00522f90 (the city drawer writes it) */
static const uint8_t *g_territory_records = g_per_territory_420;   /* DAT_0052f0b0 */
static const uint8_t *g_army_records = g_per_army_768;             /* DAT_0053f9b0 */
static const int16_t *g_per_cell_size_table;                       /* DAT_00500c30 (the path field's i16 grid) */
static const uint8_t *g_text_font;              /* &DAT_005af8f0 the default font (label + dev overlay) */
static const uint8_t *g_text_glyphs;            /* DAT_004d71f0 render map */
static const unsigned char *g_text_metrics;     /* DAT_004d71d0 measure map */
static engine_dirty_rect_state_t *g_dirty_state;
static gfx_sprite_render_log_fn g_log;          /* 0x4afab9(fmt, 0, n) */

/* Test instrumentation. */
static int g_army_call_count;
static int g_city_call_count;
static int g_debug_call_count;

#define CELL_EMPTY                        ((int32_t)ENGINE_OVERLAY_RENDER_CELL_EMPTY)
#define OVERLAY_CLIP_Y_TOP                0x18
#define OVERLAY_CLIP_Y_BOTTOM_VIEW_1      0x1d8
#define OVERLAY_CLIP_Y_BOTTOM_DEFAULT     0x1da
#define OVERLAY_CLIP_X_RIGHT              0x1de
#define OVERLAY_SRC_OFFSET_MAX            310000
#define OVERLAY_ARMY_SRC_OFFSET_MAX       70000
#define OVERLAY_ERR_NO_DATA               0x4d2050u   /* aErrTopItNoData */
#define OVERLAY_ERR_BAD_DATA_HIGH         0x4d2064u   /* aErrTopItBadDat   (offset > 70000) */
#define OVERLAY_ERR_BAD_DATA_LOW          0x4d207cu   /* aErrTopItBadDat_0 (offset < 1) */

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters (the standalone's seats, the host tests)
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_tile_cells = cells;
    g_tile_row_stride_bytes = row_stride_bytes;
}
void engine_overlay_render_bind_tile_descriptors(uint8_t *descriptors) { g_tile_descriptors = descriptors; }
void engine_overlay_render_bind_overlay_blob(const uint8_t *blob)      { g_mv_overlay_blob = blob; }
void engine_overlay_render_bind_territory_records(const uint8_t *r)    { g_territory_records = r; }
void engine_overlay_render_bind_army_records(const uint8_t *r)         { g_army_records = r; }
void engine_overlay_render_bind_per_cell_size_table(const int16_t *t)  { g_per_cell_size_table = t; }
void engine_overlay_render_bind_camera(int x0, int y0, int col_start, int row_start, int col_count, int row_count,
                                       int x_step, int y_step, int x_half_step)
{
    g_mv_cam_x0 = x0; g_mv_cam_y0 = y0;
    s_slot_005651b4 = (uint32_t)col_start; s_slot_005651b8 = (uint32_t)row_start;
    g_mv_col_count = col_count; g_mv_row_count = row_count;
    g_mv_x_step = x_step; g_mv_y_step = y_step; g_mv_x_half = x_half_step;
}
void engine_overlay_render_bind_canvas(uint8_t *pixels, int pitch, int height)
{
    (void)pixels; (void)pitch; (void)height;   /* the pre-clear draws into the gfx canvas (Buf / xRight) */
}
void engine_overlay_render_bind_debug_font(const uint8_t *font_base, const uint8_t *char_glyph_table)
{
    g_text_font = font_base;
    g_text_glyphs = char_glyph_table;
    if (!g_text_metrics) g_text_metrics = char_glyph_table;
}
void engine_overlay_render_bind_text_tables(const uint8_t *font, const uint8_t *render_map,
                                            const unsigned char *measure_map)
{
    g_text_font = font; g_text_glyphs = render_map; g_text_metrics = measure_map;
}
void engine_overlay_render_set_log(gfx_sprite_render_log_fn fn)         { g_log = fn; }
void engine_overlay_render_set_dirty_state(engine_dirty_rect_state_t *s) { g_dirty_state = s; }
void engine_overlay_render_set_view_mode(int mode)                       { s_slot_0057cb18 = (uint32_t)mode; }
void engine_overlay_render_set_active_territory(int idx)                 { g_mv_active_county = idx; }
void engine_overlay_render_set_dev_flags(int cell_v, int desc_v)
{
    g_mv_dev_cell_value = (uint8_t)cell_v;
    g_mv_dev_desc_value = (uint8_t)desc_v;
}
void engine_overlay_render_set_render_mode(int mode)                     { s_slot_0053f264 = (uint32_t)mode; }
void engine_overlay_render_set_overlay_base_index(int base)              { g_mv_overlay_base = base; }
void engine_overlay_render_set_arrow_marker_base_index(int base)         { g_mv_arrow_base = base; }
void engine_overlay_render_set_animation_tick(int tick)                  { g_mv_anim_tick = tick; }
void engine_overlay_render_set_animation_rate_flags(int t0, int t1, int t2, int t3)
{
    g_mv_rate_tier0 = t0; g_mv_rate_tier1 = t1; g_mv_rate_tier2 = t2; g_mv_rate_tier3 = t3;
}

int  engine_overlay_render_error_count(void)            { return g_mv_error_count; }
void engine_overlay_render_set_error_count(int n)       { g_mv_error_count = n; }
int  engine_overlay_render_army_call_count(void)        { return g_army_call_count; }
int  engine_overlay_render_city_call_count(void)        { return g_city_call_count; }
int  engine_overlay_render_debug_call_count(void)       { return g_debug_call_count; }
void engine_overlay_render_reset_call_counts(void)
{
    g_army_call_count = 0;
    g_city_call_count = 0;
    g_debug_call_count = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────────── */

static int32_t cell_at(int col, int row)
{
    return *(const int32_t *)((const uint8_t *)g_tile_cells + col * 4 + row * g_tile_row_stride_bytes);
}

/* `word_500C30[cell / 8]` (C division: rounds toward zero, as retail's cdq / and 7 / sar 3) */
static int size_at(int32_t cell)
{
    return g_per_cell_size_table ? g_per_cell_size_table[cell / 8] : 0;
}

static const uint8_t *desc(void) { return g_tile_descriptors + g_mv_cell; }
static const uint8_t *terr(int i) { return g_territory_records + i * 0x1a4; }
static const uint8_t *army(int i) { return g_army_records + i * 0x300; }
static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void stage(const uint8_t *blob)
{
    const uint8_t *d = blob + 16 * g_gfx_last_glyph_index + 8;
    g_gfx_glyph_w = (int16_t)(d[0] | (d[1] << 8));
    g_gfx_glyph_height_short = (int16_t)(d[2] | (d[3] << 8));
    g_gfx_glyph_offset = rd32(d + 4);
    g_gfx_flag_a = d[12];
    g_gfx_glyph_h = d[13];
    g_gfx_flag_b = d[14];
}

static void mark(int px, int x, int y)
{
    if (!g_dirty_state) return;
    if (px == 48) engine_dirty_rect_mark_tile_48(g_dirty_state, x >> 4, y >> 4);
    else engine_dirty_rect_mark_tile_32(g_dirty_state, x >> 4, y >> 4);
}

/* clip to the map viewport, then the RLE blit the clip picked (every overlay drawer inlines this) */
static void clip_and_blit_rle(void)
{
    gfx_dispatch_clip_x(g_mv_cam_x0, OVERLAY_CLIP_X_RIGHT);
    if (g_mv_view_mode == 1)
        gfx_dispatch_clip_y(OVERLAY_CLIP_Y_TOP, OVERLAY_CLIP_Y_BOTTOM_VIEW_1);
    else
        gfx_dispatch_clip_y(OVERLAY_CLIP_Y_TOP, OVERLAY_CLIP_Y_BOTTOM_DEFAULT);
    if (g_gfx_y_clip_mode != 5) {
        if (g_gfx_x_clip_mode == 1) gfx_sprite_rle_blit_left_clipped(g_mv_overlay_blob);
        else if (g_gfx_x_clip_mode == 2) gfx_sprite_rle_blit_right_clipped(g_mv_overlay_blob);
        else gfx_sprite_rle_blit(g_mv_overlay_blob);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_004b4056 — four pixels of one colour at (x, y) (the view-mode-1 left-strip clear).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_fill4(int x, int y, int color)
{
    uint8_t *p = g_gfx_canvas_pixels + x + (uint32_t)g_gfx_canvas_width * (uint32_t)y;
    p[0] = p[1] = p[2] = p[3] = (uint8_t)color;
}

/* ──────────────────────────────────────────────────────────────────
 * City drawer — port of FUN_004081a6 @ 0x4081a6 (658 B).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_tile_city(int a1)
{
    g_city_call_count += 1;
    uint8_t *d = g_tile_descriptors + g_mv_cell;
    d[2] &= (uint8_t)~0x40u;
    uint8_t v6 = d[1];
    int v4 = v6 & 0x50;
    if ((v6 & 0x80) && d[0] != 20)
        v4 = 1;
    int v5 = size_at(g_mv_cell) - 1;
    const uint8_t *t = terr(g_mv_active_county);
    if ((int8_t)t[0x154] - (int8_t)t[0x153] < v5)
        v5 = 0;
    g_gfx_last_glyph_index = v4 ? 78 : v5 + 56;
    int v3 = 20;
    if (a1 == 1)
        v3 = 20 - g_mv_x_half;
    if (!g_mv_overlay_blob) return;   /* (port guard: an unbound sheet in a host test) */
    stage(g_mv_overlay_blob);
    if (g_gfx_glyph_offset > OVERLAY_SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) {
        ++g_mv_error_count;
        return;
    }
    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
    g_gfx_cursor_x += v3;
    g_gfx_cursor_y += 6;
    mark(32, g_gfx_cursor_x, g_gfx_cursor_y);
    clip_and_blit_rle();
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* ──────────────────────────────────────────────────────────────────
 * Army label — port of FUN_00407f82 @ 0x407f82 (548 B): sprite 130 at (dx, dy) and the county's garrison number
 * centred under it (view mode 0 only).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_army_label(int a1, int a2, int a3)
{
    if (g_mv_view_mode != 0) return;
    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
    g_gfx_cursor_x += a2;
    g_gfx_cursor_y += a3;
    g_gfx_last_glyph_index = 130;
    stage(g_mv_overlay_blob);
    if (g_gfx_glyph_offset > OVERLAY_SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) {
        ++g_mv_error_count;
        return;
    }
    clip_and_blit_rle();
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
    g_gfx_no_shadow = 1;
    if (a3 + g_gfx_saved_cursor_y + 10 > 24 && g_text_font && g_text_glyphs)
        gfx_text_label_render_dec_centered(a1, ' ', " ", a2 + g_gfx_cursor_x, a3 + g_gfx_cursor_y + 10, g_gfx_glyph_w,
                                           g_text_font, g_text_glyphs, g_text_metrics, 249);
    mark(32, a2 + g_gfx_cursor_x + 4, a3 + g_gfx_cursor_y + 10);
    g_gfx_no_shadow = 0;
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* ──────────────────────────────────────────────────────────────────
 * Army drawer — port of FUN_004071a0 @ 0x4071a0 (3224 B).
 *
 * Dispatch on descriptor +1 (0x40 the keep / march-out, 0x10 the battle flag, 0x20 the arrows, else the unit
 * categories on +0); the walk-cycle categories write the next frame to descriptor +3 and return; the rest stage a
 * sprite and fall into the common tail (LABEL_118): stage from the overlay sheet, gate 0 < offset <= 70000 (log +
 * latch on failure), dirty mark per the branch's kind, clip, RLE blit, cursor restore.  The 0x40 branch with a
 * +4 nibble outside {0, 2} reaches the tail with the previously staged sprite index (quirk #70).
 * ────────────────────────────────────────────────────────────────── */

static int rate_gate(int diff)
{
    if (diff >= 10) {
        if (diff >= 25) {
            if (diff >= 50) return g_mv_rate_tier3 != 0;
            return g_mv_rate_tier2 != 0;
        }
        return g_mv_rate_tier1 != 0;
    }
    return g_mv_rate_tier0 != 0;
}

/* a walk-cycle category: advance descriptor +3 when the rate gate (by the band's cur - prev distance) is open */
static void walk(int v7, int cur_off, int prev_off, uint8_t limit, uint8_t wrap)
{
    uint8_t *d = g_tile_descriptors + g_mv_cell;
    uint8_t v10 = d[3];
    int diff = rd32(army(v7) + cur_off) - rd32(army(v7) + prev_off);
    if (rate_gate(diff) && (uint8_t)++v10 >= limit)
        v10 = wrap;
    d[3] = v10;
}

void engine_overlay_render_tile_army(int a1)
{
    g_army_call_count += 1;
    uint8_t *d = g_tile_descriptors + g_mv_cell;
    int v22 = 0;
    if (g_mv_render_mode == 1 && (d[2] & 0x20) == 0)
        return;
    int v5 = 0, v6 = 0;
    if ((d[1] & 0xf0) == 0)
        return;
    uint8_t v15 = d[0];
    int v7 = d[7];
    int view = g_mv_view_mode;
    if (d[1] & 0x40) {
        uint8_t v8 = d[4] & 0xf;
        if (v8 != 0) {
            if (v8 == 2) {
                if (army(v7)[0x1ad] == 0) return;
                v22 = 0;
                if (view != 0) { if (view == 2) { v6 = 6; v5 = -21; } }
                else { v6 = 16; v5 = -18; }
                g_gfx_last_glyph_index = 129;
            }
        } else {
            if (army(v7)[7] == 0) return;
            v22 = 2;
            if (view != 0) { if (view == 2) { v6 = 6; v5 = -21; } }
            else { v6 = 26; v5 = -28; }
            g_gfx_last_glyph_index = g_mv_overlay_base + 8 * army(v7)[7] - 8;
        }
        goto tail;
    }
    if (d[1] & 0x10) {
        if (view == 2) return;
        if (v15 != 19 || g_mv_anim_tick >= 16) return;
        g_gfx_last_glyph_index = g_mv_anim_tick + 40;
        v6 = 12; v5 = -14; v22 = 1;
        goto tail;
    }
    if (d[1] & 0x20) {
        if (view == 2) return;
        if (v15 < 0xf || v15 > 0x16) return;
        v22 = 2;
        if (v15 >= 0x13) {
            v6 = 4; v5 = -4;
            uint8_t v17 = (uint8_t)(v15 - 20);
            if (v17 > 2) return;
            g_gfx_last_glyph_index = g_mv_arrow_base + 6 * v17 + 85;
        } else {
            v6 = 0; v5 = 0;
            uint8_t v16 = (uint8_t)(v15 - 16);
            if (v16 > 2) return;
            g_gfx_last_glyph_index = g_mv_arrow_base + 6 * v16 + 103;
        }
        goto tail;
    }
    if (v15 > 0xc) {
        if (v15 > 0x19 || v15 == 20) return;
        int v4 = rd32(army(v7) + 0x1bc);
        if (v4 == 0) return;
        int v9 = terr(v4)[0x19a];
        if (v9 != 0) {
            if (view != 0) { if (view == 2) engine_overlay_render_army_label(terr(v9)[0x19c], 2, -40); }
            else engine_overlay_render_army_label(terr(v9)[0x19c], 8, -56);
        }
        v22 = 2;
        if (view != 0) { if (view == 2) { v6 = 6; v5 = -21; } }
        else { v6 = 26; v5 = -28; }
        g_gfx_last_glyph_index = g_mv_overlay_base + 8 * terr(v4)[2] - 8;
        goto tail;
    }
    if (view == 2) return;
    /* the unit categories 0..12: (step, end) per band — 0..3 band 0x2b8, 4..6 0x2e8, 7..9 0x2d0, 10..12 0x2a0 */
    {
        static const struct { uint8_t end_cat, step_skip, end_frame; int tier, cur, prev; uint8_t limit, wrap; } k_band[4] = {
            {  3,  1, 46, 0x2ae, 0x2b8, 0x2bc, 0x2e, 30 },
            {  6,  4,  9, 0x2de, 0x2e8, 0x2ec, 5,    1 },
            {  9,  7, 19, 0x2c6, 0x2d0, 0x2d4, 0x13, 10 },
            { 12, 10, 29, 0x296, 0x2a0, 0x2a4, 0x1d, 20 },
        };
        int b = v15 > 3 ? (v15 > 6 ? (v15 > 9 ? 3 : 2) : 1) : 0;
        if (v15 == k_band[b].end_cat) {
            d[3] = k_band[b].end_frame;
            if ((int8_t)army(v7)[k_band[b].tier] < 3 || g_mv_anim_tick >= 16) return;
            g_gfx_last_glyph_index = g_mv_anim_tick + 40;
            v6 = 12; v5 = -14; v22 = 1;
            goto tail;
        }
        if (v15 != k_band[b].step_skip)
            walk(v7, k_band[b].cur, k_band[b].prev, k_band[b].limit, k_band[b].wrap);
        return;
    }
tail:
    if (a1 == 1)
        v6 -= g_mv_x_half;
    if (g_mv_overlay_blob == NULL) {
        if (g_log) g_log(OVERLAY_ERR_NO_DATA, g_gfx_last_glyph_index + 1);
        g_gfx_last_error = 1;
        return;
    }
    stage(g_mv_overlay_blob);
    if (g_gfx_glyph_offset > OVERLAY_ARMY_SRC_OFFSET_MAX) {
        if (g_log) g_log(OVERLAY_ERR_BAD_DATA_HIGH, g_gfx_last_glyph_index + 1);
        g_gfx_last_error = 1;
        return;
    }
    if (g_gfx_glyph_offset <= 0) {
        if (g_log) g_log(OVERLAY_ERR_BAD_DATA_LOW, g_gfx_last_glyph_index + 1);
        g_gfx_last_error = 1;
        return;
    }
    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
    g_gfx_cursor_x += v6;
    g_gfx_cursor_y += v5;
    if (v22 == 1) mark(32, g_gfx_cursor_x, g_gfx_cursor_y);
    else if (v22 == 2) mark(48, g_gfx_cursor_x, g_gfx_cursor_y);
    clip_and_blit_rle();
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* ──────────────────────────────────────────────────────────────────
 * Dev overlay — port of FUN_00408c50 @ 0x408c50 (390 B).
 * ────────────────────────────────────────────────────────────────── */

void engine_overlay_render_debug_text(void)
{
    g_debug_call_count += 1;
    if (g_mv_dev_desc_value != 0) {
        if (g_mv_cell < CELL_EMPTY) {
            g_gfx_saved_cursor_x = g_gfx_cursor_x;
            g_gfx_saved_cursor_y = g_gfx_cursor_y;
            int v = desc()[5];
            if (v != 0) {
                if (g_text_font && g_text_glyphs)
                    gfx_text_label_render_dec(v, ' ', " ", g_gfx_cursor_x, g_gfx_cursor_y - 30, g_text_font,
                                              g_text_glyphs, 249);
                g_gfx_cursor_x = g_gfx_saved_cursor_x;
                g_gfx_cursor_y = g_gfx_saved_cursor_y;
            }
        }
    } else if (g_mv_dev_cell_value != 0 && g_mv_cell < CELL_EMPTY) {
        g_gfx_saved_cursor_x = g_gfx_cursor_x;
        g_gfx_saved_cursor_y = g_gfx_cursor_y;
        int v2 = size_at(g_mv_cell), v1;
        if (v2 >= 0) v1 = 32;
        else { v2 = -v2; v1 = 249; }
        if (g_text_font && g_text_glyphs)
            gfx_text_label_render_dec(v2, ' ', " ", g_gfx_cursor_x + 24, g_gfx_cursor_y + 10, g_text_font, g_text_glyphs,
                                      (uint8_t)v1);
        g_gfx_cursor_x = g_gfx_saved_cursor_x;
        g_gfx_cursor_y = g_gfx_saved_cursor_y;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Row iterators + frame driver — ports of FUN_00405eb5 / FUN_00405fac / FUN_00405602.
 * ────────────────────────────────────────────────────────────────── */

/* the per-cell dispatch: bit 0x80 → army, bit 0x40 → city (both may fire) */
static void dispatch_cell(int phase)
{
    if ((int8_t)g_tile_descriptors[g_mv_cell + 2] < 0)
        engine_overlay_render_tile_army(phase);
    if (g_tile_descriptors[g_mv_cell + 2] & 0x40)
        engine_overlay_render_tile_city(phase);
}

/* Port of FUN_00405eb5 — phase-0 row (the dev overlay after each drawn cell). */
void engine_overlay_render_row_phase0(void)
{
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    for (int v1 = 0; v1 < g_mv_col_count; ++v1) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) {
            dispatch_cell(0);
            engine_overlay_render_debug_text();
        }
        g_gfx_cursor_x += g_mv_x_step;
    }
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* Port of FUN_00405fac — phase-1 row: first cell phase 1, then x_half; col_count - 1 cells (dev overlay); the
 * last cell phase 2. */
void engine_overlay_render_row_phase1(void)
{
    g_mv_cur_col = g_mv_col_start;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cell = cell_at(g_mv_col_start, g_mv_cur_row);
    g_mv_cur_col = g_mv_col_start + 1;
    if (g_mv_cell < CELL_EMPTY)
        dispatch_cell(1);
    g_gfx_cursor_x += g_mv_x_half;
    for (int i = 0; g_mv_col_count - 1 > i; ++i) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) {
            dispatch_cell(0);
            engine_overlay_render_debug_text();
        }
        g_gfx_cursor_x += g_mv_x_step;
    }
    g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
    if (g_mv_cell < CELL_EMPTY)
        dispatch_cell(2);
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* Port of FUN_00405602 — the view-mode-1 left-strip clear, the top phantom row, the phase-1 row, the pairs, the
 * bottom phantom row (the phantom rows run no dev overlay). */
void engine_overlay_render_frame(void)
{
    if (g_mv_view_mode == 1)
        for (int i = 24; i < 444; ++i)
            engine_overlay_render_fill4(0, i, 0);
    g_gfx_cursor_y = g_mv_cam_y0;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_row = g_mv_row_start;
    g_mv_col_y_accum = 0;
    g_mv_cur_col = g_mv_col_start;
    for (int v1 = 0; v1 < g_mv_col_count; ++v1) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY)
            dispatch_cell(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    engine_overlay_render_row_phase1();
    for (int j = 0; (g_mv_row_count - 2) / 2 > j; ++j) {
        engine_overlay_render_row_phase0();
        engine_overlay_render_row_phase1();
    }
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    for (int v2 = 0; v2 < g_mv_col_count; ++v2) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY)
            dispatch_cell(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
    ++g_mv_cur_row;
    g_gfx_cursor_y += g_mv_y_step;
    g_mv_col_y_accum = 0;
}
