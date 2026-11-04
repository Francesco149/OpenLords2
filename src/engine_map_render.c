/*
 * OpenLords2 — strategic-mode overworld feature walker (pass 3).  See engine_map_render.h,
 * docs/findings/map-render.md and docs/findings/render-mapview.md.
 *
 * Retail layout: the camera, walker, view / render mode, rotation, the feature banks and the error counter are the
 * shared retail-layout globals (engine_mapview_globals.h); the staged descriptor, cursor and its save slot are the
 * gfx layer's (gfx_globals.h).  Each function follows retail's control flow and global writes.
 */

#include "engine_map_render.h"

#include "call_trace.h"
#include "engine_dirty_rect.h"
#include "engine_mapview_globals.h"
#include "gfx_dispatch.h"
#include "gfx_drawcap.h"
#include "gfx_globals.h"
#include "gfx_sprite.h"
#include "gfx_sprite_rle.h"
#include "save.h"

#include <stddef.h>

/* ──────────────────────────────────────────────────────────────────
 * Palette tables — six 128-byte signed (X, Y) displacement tables, contiguous in retail's .rdata at
 * 0x4d8108..0x4d8408, indexed `[view][axis][rotation * 16 + frame]` with NO bound (a frame offset outside 0..15
 * reads the neighbouring row / table).  EXACT copies of the retail bytes; rhost points the lookup at retail's own.
 * ────────────────────────────────────────────────────────────────── */

static const int8_t g_palette_tables[3][2][128] = {
    /* view-mode 0 */
    {
        /* X table @ 0x4d8108 */
        {
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,-48,-44,-40,-36,-32,-28,-24,-20,-16,-12, -8, -4,  0,  4,  0,
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
             0, 56, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12,  8,  4,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8188 */
        {
             0, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,-12,-13,-14,  0, -7, -8, -9,-10,-11,  0, -2, -3, -4, -5, -6,
             0,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,
             0,-12,-13,-14,  0, -7, -8, -9,-10,-11,  0, -2, -3, -4, -5, -6,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
             0, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,
        },
    },
    /* view-mode 1 */
    {
        /* X table @ 0x4d8208 */
        {
             0,-13,-12, -8, -7, -7, -8, -8, -9,-10,-11,-12,-13,-13,-12,  0,
             0,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,  0,  0,
             0,-13,-12, -8, -7, -7, -8, -8, -9,-10,-11,-12,-13,-13,-12,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, 13, 12,  8,  7,  7,  8,  8,  9, 10, 11, 12, 13, 13, 12,  0,
             0, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,  0,  0,
             0, 13, 12,  8,  7,  7,  8,  8,  9, 10, 11, 12, 13, 13, 12,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8288 */
        {
             0,  6,  4,  4,  4,  3,  3,  3,  3,  3,  2,  2,  2,  1,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0, -1, -1, -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -6, -6,
             0, -6, -4, -4, -4, -3, -3, -3, -3, -3, -2, -2, -2, -1, -1,  0,
             0, -1, -1, -2, -2, -2, -3, -3, -3, -3, -4, -4, -4, -4, -6, -6,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  6,  4,  4,  4,  3,  3,  3,  3,  3,  2,  2,  2,  1,  1,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
    },
    /* view-mode 2 */
    {
        /* X table @ 0x4d8308 */
        {
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
        /* Y table @ 0x4d8388 */
        {
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        },
    },
};

/* ──────────────────────────────────────────────────────────────────
 * Port bindings (not retail globals)
 * ────────────────────────────────────────────────────────────────── */

static const int32_t *g_tile_cells;                                /* DAT_0055cea0 */
static int            g_tile_row_stride_bytes = 0x104;
static const uint8_t *g_tile_descriptors = g_overworld_map;        /* DAT_00522f90 */
static const uint8_t *g_territory_records = g_per_territory_420;   /* DAT_0052f0b0 */
static const int8_t  *g_palette = &g_palette_tables[0][0][0];      /* 0x4d8108 */
static engine_dirty_rect_state_t *g_dirty_state;

/* Test instrumentation — FUN_00408438 invocations. */
static int g_call_count;

#define MAP_RENDER_CLIP_Y_TOP                0x18
#define MAP_RENDER_CLIP_Y_BOTTOM_VIEW_1      0x1d8
#define MAP_RENDER_CLIP_Y_BOTTOM_DEFAULT     0x1da
#define MAP_RENDER_CLIP_X_RIGHT              0x1de
#define MAP_RENDER_SRC_OFFSET_MAX            310000
#define MAP_RENDER_TRAILING_COL_GATE         0x40      /* FUN_00405862: `if (DAT_0056d598 < 64)` */
#define CELL_EMPTY                           ((int32_t)ENGINE_MAP_RENDER_CELL_EMPTY)

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_tile_cells            = cells;
    g_tile_row_stride_bytes = row_stride_bytes;
}

void engine_map_render_bind_tile_descriptors(const uint8_t *descriptors) { g_tile_descriptors = descriptors; }
void engine_map_render_bind_palette(const int8_t *tables) { g_palette = tables ? tables : &g_palette_tables[0][0][0]; }

void engine_map_render_fill_hover_in(engine_camera_hover_in_t *in)
{
    in->x_origin   = g_mv_cam_x0;
    in->y_origin   = g_mv_cam_y0;
    in->x_step     = g_mv_x_step;
    in->half_step  = g_mv_x_half;
    in->y_step     = g_mv_y_step;
    in->col_count  = g_mv_col_count;
    in->row_count  = g_mv_row_count;
    in->scroll_col = g_mv_col_start;
    in->scroll_row = g_mv_row_start;
    in->tile_grid  = g_tile_cells;
}

const uint8_t *engine_map_render_tile_descriptors(void) { return g_tile_descriptors; }

void engine_map_render_bind_blobs(const uint8_t *primary, const uint8_t *alt, const uint8_t *overlay)
{
    g_mv_feature_blob     = primary;
    g_mv_feature_alt_blob = alt;
    g_mv_overlay_blob     = overlay;
}

void engine_map_render_bind_territory_records(const uint8_t *records) { g_territory_records = records; }

void engine_map_render_bind_camera(int x0, int y0, int col_start, int row_start, int col_count, int row_count,
                                   int x_step, int y_step, int x_half_step)
{
    g_mv_cam_x0 = x0; g_mv_cam_y0 = y0;
    s_slot_005651b4 = (uint32_t)col_start; s_slot_005651b8 = (uint32_t)row_start;
    g_mv_col_count = col_count; g_mv_row_count = row_count;
    g_mv_x_step = x_step; g_mv_y_step = y_step; g_mv_x_half = x_half_step;
}

void engine_map_render_set_y_accumulator(int y_acc)        { g_mv_col_y_accum = y_acc; }
void engine_map_render_set_view_mode(int mode)             { s_slot_0057cb18 = (uint32_t)mode; }
void engine_map_render_set_rotation_offset(int rot)        { g_mv_rotation = rot; }
void engine_map_render_set_render_mode(int mode)           { s_slot_0053f264 = (uint32_t)mode; }
void engine_map_render_set_overlay_base_index(int idx)     { g_mv_overlay_base = idx; }
void engine_map_render_set_dirty_state(engine_dirty_rect_state_t *s) { g_dirty_state = s; }
void engine_map_render_set_y_viewport_offset(int y_off)    { g_mv_y_viewport = y_off; }

int  engine_map_render_error_count(void)                   { return g_mv_error_count; }
void engine_map_render_set_error_count(int n)              { g_mv_error_count = n; }

int  engine_map_render_call_count(void)                    { return g_call_count; }
void engine_map_render_reset_call_count(void)              { g_call_count = 0; }

/* ──────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────────── */

/* `dword_55CEA0[65 * row + col]` (col may be -1: FUN_00405862 reads `dword_55CE9C[65 * row + col_start]`) */
static int32_t cell_at(int col, int row)
{
    return *(const int32_t *)((const uint8_t *)g_tile_cells + col * 4 + row * g_tile_row_stride_bytes);
}

/* Stage descriptor `16 * DAT_005c9288 + 8 + blob` (every drawer inlines it). */
static void stage(const uint8_t *blob)
{
    const uint8_t *d = blob + 16 * g_gfx_last_glyph_index + 8;
    g_gfx_glyph_w = (int16_t)(d[0] | (d[1] << 8));
    g_gfx_glyph_height_short = (int16_t)(d[2] | (d[3] << 8));
    g_gfx_glyph_offset = (int32_t)((uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16)
                                   | ((uint32_t)d[7] << 24));
    g_gfx_flag_a = d[12];
    g_gfx_glyph_h = d[13];
    g_gfx_flag_b = d[14];
}

static void mark(int px)   /* the 32 / 48 / 64 px box at the cursor (FUN_00452436 / 00452489 / 004524dc) */
{
    if (!g_dirty_state) return;
    int c = g_gfx_cursor_x >> 4, r = g_gfx_cursor_y >> 4;
    if (px == 64) engine_dirty_rect_mark_tile_64(g_dirty_state, c, r);
    else if (px == 48) engine_dirty_rect_mark_tile_48(g_dirty_state, c, r);
    else engine_dirty_rect_mark_tile_32(g_dirty_state, c, r);
}

static void clip(void)
{
    gfx_dispatch_clip_x(g_mv_cam_x0, MAP_RENDER_CLIP_X_RIGHT);
    if (g_mv_view_mode == 1)
        gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP, MAP_RENDER_CLIP_Y_BOTTOM_VIEW_1);
    else
        gfx_dispatch_clip_y(MAP_RENDER_CLIP_Y_TOP, MAP_RENDER_CLIP_Y_BOTTOM_DEFAULT);
}

/* ──────────────────────────────────────────────────────────────────
 * Per-tile drawer — port of FUN_00408438 @ 0x408438 (2072 B): walk the cell's feature-record list (descriptor +5
 * = head, record +4 = next, self-link ends it), draw each record's sprite and its optional RLE overlay.
 * ────────────────────────────────────────────────────────────────── */

void engine_map_render_tile(int a1)
{
    CALL_TRACE_ENTER(0x00408438);
    g_call_count += 1;
    int32_t cell = g_mv_cell;
    int v5 = g_tile_descriptors[cell + 5];
    if (g_mv_render_mode == 1 && (g_tile_descriptors[cell + 2] & 0x20) == 0)
        return;
    while (v5 != 0) {
        const uint8_t *rec = g_territory_records + v5 * 0x1a4;
        const uint8_t *blob = (rec[8] == 4) ? g_mv_feature_alt_blob : g_mv_feature_blob;
        int kind = 0;
        int rot = (int)(int8_t)rec[9] - g_mv_rotation;
        if (rot < 0) rot += 8;
        int frame = (int8_t)rec[0x149];
        int dx = 0, dy = 0;   /* (retail leaves them uninitialized for a view mode outside 0..2) */
        if (g_mv_view_mode >= 0 && g_mv_view_mode <= 2) {
            const int8_t *t = g_palette + g_mv_view_mode * 0x100;
            dx = t[16 * rot + frame];
            dy = t[0x80 + 16 * rot + frame];
        }
        if (a1 == 1) dx -= 2;
        else if (a1 == 2) dx += g_mv_x_half - 2;
        else dx += g_mv_x_half;
        dy += g_mv_y_viewport;
        switch (rec[8]) {
        case 3: dy -= 2; dx -= 4; kind = 3; break;
        case 1: dy -= 4; kind = 3; break;
        case 2: dy -= 4; kind = 3; break;
        case 4: dy -= 2; dx -= 4; kind = 3; break;
        default: break;
        }
        g_gfx_last_glyph_index = rec[7];
        stage(blob);
        if (g_gfx_glyph_offset > MAP_RENDER_SRC_OFFSET_MAX) { ++g_mv_error_count; return; }
        if (g_gfx_glyph_w <= 0) { ++g_mv_error_count; return; }
        g_gfx_saved_cursor_x = g_gfx_cursor_x;
        g_gfx_saved_cursor_y = g_gfx_cursor_y;
        g_gfx_cursor_x += dx;
        g_gfx_cursor_y += dy;
        g_gfx_cursor_x -= g_gfx_glyph_w >> 1;
        g_gfx_cursor_y -= g_gfx_glyph_height_short;
        mark(kind == 3 ? 64 : 32);
        int v13 = g_gfx_cursor_x, v12 = g_gfx_cursor_y;
        clip();
        g_gfx_glyph_offset += g_gfx_top_skip * g_gfx_glyph_w;
        g_gfx_glyph_offset += g_gfx_left_skip;
        gfx_drawcap_draw(GFX_DRAWCAP_OP_SPRITE, rec[7], v13, v12);
        if (g_gfx_y_clip_mode != 5) {
            if (g_gfx_x_clip_mode == 1) gfx_sprite_blit_left_clipped(blob);
            else if (g_gfx_x_clip_mode == 2) gfx_sprite_blit_right_clipped(blob);
            else gfx_sprite_blit(blob);
        }
        g_gfx_last_glyph_index = 999;
        if (rec[8] == 1 && rec[2] != 0) {
            g_gfx_cursor_x = v13; g_gfx_cursor_y = v12;
            g_gfx_last_glyph_index = g_mv_overlay_base + 8 * rec[2] - 8;
            if (g_mv_view_mode == 0) { g_gfx_cursor_x += 18; g_gfx_cursor_y -= 21; }
            else if (g_mv_view_mode == 2) { g_gfx_cursor_x += 24; g_gfx_cursor_y -= 21; }
            mark(32);
        } else if (rec[8] == 2) {
            g_gfx_cursor_x = v13; g_gfx_cursor_y = v12;
            g_gfx_last_glyph_index = g_mv_overlay_base + 121;
            if (g_mv_view_mode == 0) { g_gfx_cursor_x += 18; g_gfx_cursor_y -= 18; }
            else if (g_mv_view_mode == 2) { g_gfx_cursor_x += 4; g_gfx_cursor_y -= 4; }
            mark(32);
        }
        if (g_gfx_last_glyph_index != 999) {
            stage(g_mv_overlay_blob);
            if (g_gfx_glyph_offset > MAP_RENDER_SRC_OFFSET_MAX) { ++g_mv_error_count; return; }
            if (g_gfx_glyph_w <= 0) { ++g_mv_error_count; return; }
            clip();
            gfx_drawcap_draw(GFX_DRAWCAP_OP_SPRITE, g_gfx_last_glyph_index, g_gfx_cursor_x, g_gfx_cursor_y);
            if (g_gfx_y_clip_mode != 5) {
                if (g_gfx_x_clip_mode == 1) gfx_sprite_rle_blit_left_clipped(g_mv_overlay_blob);
                else if (g_gfx_x_clip_mode == 2) gfx_sprite_rle_blit_right_clipped(g_mv_overlay_blob);
                else gfx_sprite_rle_blit(g_mv_overlay_blob);
            }
            mark(48);
        }
        g_gfx_cursor_x = g_gfx_saved_cursor_x;
        g_gfx_cursor_y = g_gfx_saved_cursor_y;
        int next = (int16_t)(rec[4] | (rec[5] << 8));
        if (next == v5) return;
        v5 = next;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Row iterators + frame driver
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00405862 — phase-0 row: an extra left cell (col_start - 1, when col_start > 0), col_count cells,
 * an extra right cell while the column is < 64. */
void engine_map_render_row_phase0(void)
{
    CALL_TRACE_ENTER(0x00405862);
    if (g_mv_col_start > 0) {
        g_gfx_cursor_x = g_mv_cam_x0 - g_mv_x_step;
        g_mv_cell = cell_at(g_mv_col_start - 1, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
    }
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    for (int v1 = 0; g_mv_col_count > v1; ++v1) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
    if (g_mv_cur_col < MAP_RENDER_TRAILING_COL_GATE) {
        g_mv_cell = cell_at(g_mv_cur_col, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
    }
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* Port of FUN_004059af — phase-1 row: the first cell at phase 1, then x_half; col_count - 1 cells; one more. */
void engine_map_render_row_phase1(void)
{
    CALL_TRACE_ENTER(0x004059af);
    g_mv_cur_col = g_mv_col_start;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cell = cell_at(g_mv_col_start, g_mv_cur_row);
    g_mv_cur_col = g_mv_col_start + 1;
    if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(1);
    g_gfx_cursor_x += g_mv_x_half;
    for (int i = 0; g_mv_col_count - 1 > i; ++i) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
    g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
    if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* Port of FUN_00405487 — top phantom row, the phase-1 row, (row_count - 2) / 2 pairs, the bottom phantom row. */
void engine_map_render_frame(void)
{
    CALL_TRACE_ENTER(0x00405487);
    g_gfx_cursor_y = g_mv_cam_y0;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_row = g_mv_row_start;
    g_mv_col_y_accum = 0;
    g_mv_cur_col = g_mv_col_start;
    for (int v1 = 0; v1 < g_mv_col_count; ++v1) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    engine_map_render_row_phase1();
    for (int i = 0; (g_mv_row_count - 2) / 2 > i; ++i) {
        engine_map_render_row_phase0();
        engine_map_render_row_phase1();
    }
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    for (int v2 = 0; v2 < g_mv_col_count; ++v2) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) engine_map_render_tile(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
}
