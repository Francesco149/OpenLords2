/*
 * OpenLords2 — strategic-mode base-terrain renderer (pass 1).  See engine_map_terrain.h, docs/findings/map-render.md
 * ("Pass 1") and docs/findings/render-mapview.md (the retail-layout move, the rhost rows).
 *
 * Every function follows retail's control flow and global writes (IDA pseudocode + asm): the walker globals, the
 * staged descriptor (gfx_globals.h), the county vote counts, the overhang source offset, the error counter.
 */

#include "engine_map_terrain.h"

#include "call_trace.h"
#include "engine_mapview_globals.h"
#include "gfx_drawcap.h"
#include "gfx_globals.h"
#include "save.h"

#include <stddef.h>

/* The unrolled blitters, transliterated instruction by instruction from the retail asm (tools/gen_mapblit.py;
 * src/gen is derived, gitignored).  Ports of FUN_00452820 / FUN_0045337b / FUN_004538f6 (view 0 tile + edges),
 * FUN_00460501 / FUN_0046079d / FUN_004608de (view 1), FUN_00463b42 / FUN_00463c06 / FUN_00463c62 (view 2), and
 * the overhang strips FUN_00453e71 / FUN_00455f04 / FUN_00456fd8 / FUN_004580a9 / FUN_00459258 (view 0),
 * FUN_00460a1c / FUN_004611ef / FUN_00461623 / FUN_00461a54 / FUN_00461efb (view 1),
 * FUN_00463cbe / FUN_00463e91 / FUN_00463fa5 / FUN_004640b6 / FUN_00464209 (view 2). */
#include "gen/mapblit_helpers.h"
#include "gen/mapblit_452820.h"
#include "gen/mapblit_45337b.h"
#include "gen/mapblit_4538f6.h"
#include "gen/mapblit_460501.h"
#include "gen/mapblit_46079d.h"
#include "gen/mapblit_4608de.h"
#include "gen/mapblit_463b42.h"
#include "gen/mapblit_463c06.h"
#include "gen/mapblit_463c62.h"
#include "gen/mapblit_453e71.h"
#include "gen/mapblit_455f04.h"
#include "gen/mapblit_456fd8.h"
#include "gen/mapblit_4580a9.h"
#include "gen/mapblit_459258.h"
#include "gen/mapblit_460a1c.h"
#include "gen/mapblit_4611ef.h"
#include "gen/mapblit_461623.h"
#include "gen/mapblit_461a54.h"
#include "gen/mapblit_461efb.h"
#include "gen/mapblit_463cbe.h"
#include "gen/mapblit_463e91.h"
#include "gen/mapblit_463fa5.h"
#include "gen/mapblit_4640b6.h"
#include "gen/mapblit_464209.h"

/* ──────────────────────────────────────────────────────────────────
 * Port bindings (not retail globals)
 * ────────────────────────────────────────────────────────────────── */

static const int32_t *g_cells;              /* DAT_0055cea0 (retail indexes the array directly) */
static int            g_row_stride = 0x104;
static uint8_t       *g_desc = g_overworld_map;   /* DAT_00522f90 */
static int            g_own_gate;
static int           *g_gate = &g_own_gate; /* DAT_0053e9ac (the dirty-rect state's below_status_redraw) */
static engine_dirty_rect_state_t *g_dirty;

/* Stats (test-only). */
static int g_n_drawa, g_n_empty, g_n_drawb;

#define CELL_EMPTY        ((int32_t)ENGINE_MAP_TERRAIN_CELL_EMPTY)
#define SRC_OFFSET_MAX    310000          /* `cmp Offset, 4BAF0h; jle` */

/* ──────────────────────────────────────────────────────────────────
 * Bind / config (the standalone's seats and the host tests)
 * ────────────────────────────────────────────────────────────────── */

void engine_map_terrain_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_cells = cells;
    g_row_stride = row_stride_bytes;
}

void engine_map_terrain_bind_descriptors(const uint8_t *descriptors) { g_desc = (uint8_t *)(uintptr_t)descriptors; }

void engine_map_terrain_bind_blobs(const uint8_t *b0, const uint8_t *b1, const uint8_t *b2, const uint8_t *b3,
                                   const uint8_t *b4)
{
    g_mv_terrain_blob0 = b0; g_mv_terrain_blob1 = b1; g_mv_terrain_blob2 = b2;
    g_mv_terrain_blob3 = b3; g_mv_terrain_blob4 = b4;
}

void engine_map_terrain_set_blob_lengths(int n0, int n1, int n2, int n3, int n4)
{
    (void)n0; (void)n1; (void)n2; (void)n3; (void)n4;
}

void engine_map_terrain_bind_camera(int x0, int y0, int col_start, int row_start, int col_count, int row_count,
                                    int x_step, int y_step, int x_half_step)
{
    g_mv_cam_x0 = x0; g_mv_cam_y0 = y0;
    s_slot_005651b4 = (uint32_t)col_start; s_slot_005651b8 = (uint32_t)row_start;
    g_mv_col_count = col_count; g_mv_row_count = row_count;
    g_mv_x_step = x_step; g_mv_y_step = y_step; g_mv_x_half = x_half_step;
}

void engine_map_terrain_set_view_mode(int v)          { s_slot_0057cb18 = (uint32_t)v; }
void engine_map_terrain_set_render_mode(int m)        { s_slot_0053f264 = (uint32_t)m; }
void engine_map_terrain_bind_empty_gate(int *gate)    { g_gate = gate ? gate : &g_own_gate; }
void engine_map_terrain_set_empty_redraw_gate(int f)  { *g_gate = f; }
int  engine_map_terrain_empty_redraw_gate(void)       { return *g_gate; }
void engine_map_terrain_set_dirty_state(engine_dirty_rect_state_t *s) { g_dirty = s; }

int  engine_map_terrain_error_count(void)       { return g_mv_error_count; }
void engine_map_terrain_set_error_count(int n)  { g_mv_error_count = n; }

int  engine_map_terrain_drawa_count(void)       { return g_n_drawa; }
int  engine_map_terrain_empty_draw_count(void)  { return g_n_empty; }
int  engine_map_terrain_drawb_count(void)       { return g_n_drawb; }
int  engine_map_terrain_drawb_skipped(void)     { return 0; }
int  engine_map_terrain_oob_copies(void)        { return 0; }
void engine_map_terrain_reset_stats(void)       { g_n_drawa = g_n_empty = g_n_drawb = 0; }

/* ──────────────────────────────────────────────────────────────────
 * The blitters (retail signatures)
 * ────────────────────────────────────────────────────────────────── */

void engine_map_terrain_blit_452820(const uint8_t *b, int p) { mapblit_452820(b, p, 0); }
void engine_map_terrain_blit_45337b(const uint8_t *b, int p) { mapblit_45337b(b, p, 0); }
void engine_map_terrain_blit_4538f6(const uint8_t *b, int p) { mapblit_4538f6(b, p, 0); }
void engine_map_terrain_blit_460501(const uint8_t *b, int p) { mapblit_460501(b, p, 0); }
void engine_map_terrain_blit_46079d(const uint8_t *b, int p) { mapblit_46079d(b, p, 0); }
void engine_map_terrain_blit_4608de(const uint8_t *b, int p) { mapblit_4608de(b, p, 0); }
void engine_map_terrain_blit_463b42(const uint8_t *b, int p) { mapblit_463b42(b, p, 0); }
void engine_map_terrain_blit_463c06(const uint8_t *b, int p) { mapblit_463c06(b, p, 0); }
void engine_map_terrain_blit_463c62(const uint8_t *b, int p) { mapblit_463c62(b, p, 0); }
void engine_map_terrain_strip_453e71(const uint8_t *b, int y) { mapblit_453e71(b, y, 0); }
void engine_map_terrain_strip_455f04(const uint8_t *b, int y) { mapblit_455f04(b, y, 0); }
void engine_map_terrain_strip_456fd8(const uint8_t *b, int y) { mapblit_456fd8(b, y, 0); }
void engine_map_terrain_strip_4580a9(const uint8_t *b, int y, int s) { mapblit_4580a9(b, y, s); }
void engine_map_terrain_strip_459258(const uint8_t *b, int y, int s) { mapblit_459258(b, y, s); }
void engine_map_terrain_strip_460a1c(const uint8_t *b, int y) { mapblit_460a1c(b, y, 0); }
void engine_map_terrain_strip_4611ef(const uint8_t *b, int y) { mapblit_4611ef(b, y, 0); }
void engine_map_terrain_strip_461623(const uint8_t *b, int y) { mapblit_461623(b, y, 0); }
void engine_map_terrain_strip_461a54(const uint8_t *b, int y, int s) { mapblit_461a54(b, y, s); }
void engine_map_terrain_strip_461efb(const uint8_t *b, int y, int s) { mapblit_461efb(b, y, s); }
void engine_map_terrain_strip_463cbe(const uint8_t *b, int y) { mapblit_463cbe(b, y, 0); }
void engine_map_terrain_strip_463e91(const uint8_t *b, int y) { mapblit_463e91(b, y, 0); }
void engine_map_terrain_strip_463fa5(const uint8_t *b, int y) { mapblit_463fa5(b, y, 0); }
void engine_map_terrain_strip_4640b6(const uint8_t *b, int y, int s) { mapblit_4640b6(b, y, s); }
void engine_map_terrain_strip_464209(const uint8_t *b, int y, int s) { mapblit_464209(b, y, s); }

/* ──────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────────── */

/* `dword_55CEA0[65 * row + col]` */
static int32_t cell_at(int col, int row)
{
    const uint8_t *p = (const uint8_t *)g_cells + row * g_row_stride + col * 4;
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* Stage descriptor `16 * DAT_005c9288 + 8 + blob` into the dispatcher's globals (every drawer inlines this). */
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

/* The bank a descriptor's +2 bits 0x1c select (NULL: none — retail returns without drawing). */
static const uint8_t *bank(unsigned sel)
{
    switch (sel) {
    case 0x00: return g_mv_terrain_blob0;
    case 0x04: return g_mv_terrain_blob1;
    case 0x08: return g_mv_terrain_blob2;
    case 0x0c: return g_mv_terrain_blob3;
    case 0x10: return g_mv_terrain_blob4;
    default:   return NULL;
    }
}

/* `if (DAT_0053f264 == 1) DAT_005c9288 = 0; else DAT_005c9288 = cell - 0xfff0000;` */
static void empty_index(void)
{
    g_gfx_last_glyph_index = (g_mv_render_mode == 1) ? 0 : g_mv_cell - CELL_EMPTY;
}

/* The county vote (FUN_00405ae9 / FUN_00405c2f): flag 0x40 cells weigh 5. */
static void vote(void)
{
    if (g_mv_cell_flags & 0x40)
        g_mv_county_votes[g_mv_cell_county] += 5;
    else
        ++g_mv_county_votes[g_mv_cell_county];
}

/* ──────────────────────────────────────────────────────────────────
 * Drawers
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_004063c1 — the ground diamond. */
void engine_map_terrain_drawer_a(int a1)
{
    int32_t cell = g_mv_cell;
    g_mv_cell_county = g_desc[cell + 7];
    g_mv_cell_flags = g_desc[cell + 1];
    g_gfx_last_glyph_index = g_desc[cell + 3];
    unsigned sel = g_desc[cell + 2] & 0x1c;
    if (g_mv_render_mode == 1 && (g_desc[cell + 2] & 0x20) == 0) {
        sel = 0;
        g_gfx_last_glyph_index = 0;
    }
    if (sel > 0x10) return;   /* no bank: retail returns without drawing */
    const uint8_t *blob = bank(sel);
    if (g_desc[cell + 2] & 1) {   /* first paint: clear the bit, mark the 80×48 box */
        g_desc[cell + 2] &= (uint8_t)~1u;
        if (g_dirty) engine_dirty_rect_mark_tile_80x48_noclamp(g_dirty, g_gfx_cursor_x >> 4, g_gfx_cursor_y >> 4);
    }
    if (!blob) return;        /* (port guard: an unbound bank in a host test) */
    stage(blob);
    if (g_gfx_glyph_offset > SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) {
        ++g_mv_error_count;
        return;
    }
    g_n_drawa++;
    if (a1 == 3) {
        if (g_mv_view_mode == 0) engine_map_terrain_blit_45337b(blob, 0);
        else if (g_mv_view_mode == 2) engine_map_terrain_blit_463c06(blob, 0);
    } else if (a1 == 4) {
        if (g_mv_view_mode == 0) engine_map_terrain_blit_4538f6(blob, 0);
        else if (g_mv_view_mode == 2) engine_map_terrain_blit_463c62(blob, 0);
    } else {
        if (g_mv_view_mode == 0) engine_map_terrain_blit_452820(blob, a1);
        else if (g_mv_view_mode == 2) engine_map_terrain_blit_463b42(blob, a1);
    }
}

/* Port of FUN_00406673 — the overhang strip (the part of a tall sprite that rises above its diamond). */
void engine_map_terrain_drawer_b(int a1)
{
    int32_t cell = g_mv_cell;
    g_gfx_last_glyph_index = g_desc[cell + 3];
    unsigned sel = g_desc[cell + 2] & 0x1c;
    if (g_mv_render_mode == 1 && (g_desc[cell + 2] & 0x20) == 0) return;
    if (sel > 0x10) return;
    const uint8_t *blob = bank(sel);
    if (!blob) return;        /* (port guard) */
    stage(blob);
    int view = g_mv_view_mode;
    g_mv_overhang_off = g_gfx_glyph_offset + (view == 0 ? 900 : view == 1 ? 196 : 36);
    if (g_gfx_glyph_offset > SRC_OFFSET_MAX || g_gfx_glyph_offset < 0 || g_gfx_glyph_h > 200 || g_gfx_glyph_h < 0) {
        ++g_mv_error_count;
        return;
    }
    int y = g_mv_col_y_accum;
    uint8_t kind = g_gfx_flag_a;
    if (a1 == 3) {
        if (g_gfx_glyph_h == 0) return;
        if (kind == 2) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_455f04(blob, y);
            else if (view == 1) engine_map_terrain_strip_4611ef(blob, y);
            else engine_map_terrain_strip_463e91(blob, y);
        } else if (kind == 4) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_459258(blob, y, 2);
            else if (view == 1) engine_map_terrain_strip_461efb(blob, y, 2);
            else engine_map_terrain_strip_464209(blob, y, 2);
        }
    } else if (a1 == 4) {
        if (g_gfx_glyph_h == 0) return;
        if (kind == 2) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_456fd8(blob, y);
            else if (view == 1) engine_map_terrain_strip_461623(blob, y);
            else engine_map_terrain_strip_463fa5(blob, y);
        }
        if (g_gfx_flag_a == 3) {   /* re-read: retail tests the global again after the kind-2 strip */
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_4580a9(blob, y, 2);
            else if (view == 1) engine_map_terrain_strip_461a54(blob, y, 2);
            else engine_map_terrain_strip_4640b6(blob, y, 2);
        }
    } else if (a1 != 2 && g_gfx_glyph_h != 0) {
        if (kind == 2) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_453e71(blob, y);
            else if (view == 1) engine_map_terrain_strip_460a1c(blob, y);
            else engine_map_terrain_strip_463cbe(blob, y);
        } else if (kind == 3) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_4580a9(blob, y, 0);
            else if (view == 1) engine_map_terrain_strip_461a54(blob, y, 0);
            else engine_map_terrain_strip_4640b6(blob, y, 0);
        } else if (kind == 4) {
            g_n_drawb++;
            if (view == 0) engine_map_terrain_strip_459258(blob, y, 0);
            else if (view == 1) engine_map_terrain_strip_461efb(blob, y, 0);
            else engine_map_terrain_strip_464209(blob, y, 0);
        }
    }
}

/* Port of FUN_0042a7f1 — an empty / off-map cell: bank 0 at the empty marker's sprite index. */
void engine_map_terrain_empty_cell(int a1)
{
    if (!g_mv_terrain_blob0) return;   /* (port guard) */
    stage(g_mv_terrain_blob0);
    if (g_gfx_glyph_offset > SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) { ++g_mv_error_count; return; }
    g_n_empty++;
    if (g_mv_view_mode == 0) engine_map_terrain_blit_452820(g_mv_terrain_blob0, a1);
    else if (g_mv_view_mode == 1) engine_map_terrain_blit_460501(g_mv_terrain_blob0, a1);
    else engine_map_terrain_blit_463b42(g_mv_terrain_blob0, a1);
}

/* Port of FUN_0042a8d1 — … the shifted row's first (left-edge) cell. */
void engine_map_terrain_empty_left(void)
{
    if (!g_mv_terrain_blob0) return;
    stage(g_mv_terrain_blob0);
    if (g_gfx_glyph_offset > SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) { ++g_mv_error_count; return; }
    g_n_empty++;
    if (g_mv_view_mode == 0) engine_map_terrain_blit_45337b(g_mv_terrain_blob0, 0);
    else if (g_mv_view_mode == 1) engine_map_terrain_blit_46079d(g_mv_terrain_blob0, 0);
    else engine_map_terrain_blit_463c06(g_mv_terrain_blob0, 0);
}

/* Port of FUN_0042a9ab — … the shifted row's last (right-edge) cell. */
void engine_map_terrain_empty_right(void)
{
    if (!g_mv_terrain_blob0) return;
    stage(g_mv_terrain_blob0);
    if (g_gfx_glyph_offset > SRC_OFFSET_MAX || g_gfx_glyph_offset < 0) { ++g_mv_error_count; return; }
    g_n_empty++;
    if (g_mv_view_mode == 0) engine_map_terrain_blit_4538f6(g_mv_terrain_blob0, 0);
    else if (g_mv_view_mode == 1) engine_map_terrain_blit_4608de(g_mv_terrain_blob0, 0);
    else engine_map_terrain_blit_463c62(g_mv_terrain_blob0, 0);
}

/* ──────────────────────────────────────────────────────────────────
 * Row iterators
 * ────────────────────────────────────────────────────────────────── */

/* One inner cell of either row iterator (phase 0, gated empty cells, the county vote). */
static void inner_cell(void)
{
    g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
    gfx_drawcap_draw(GFX_DRAWCAP_OP_SPRITE, g_mv_cell, g_gfx_cursor_x, g_gfx_cursor_y);
    if (g_mv_cell < CELL_EMPTY) {
        engine_map_terrain_drawer_a(0);
        engine_map_terrain_drawer_b(0);
        g_gfx_cursor_x += g_mv_x_step;
        vote();
    } else {
        empty_index();
        if (*g_gate != 0 || g_gfx_last_glyph_index >= 7)
            engine_map_terrain_empty_cell(0);
        g_gfx_cursor_x += g_mv_x_step;
    }
}

/* Port of FUN_00405ae9 — aligned row: col_count cells from col_start, phase 0. */
void engine_map_terrain_row_aligned(void)
{
    CALL_TRACE_ENTER(0x00405ae9);
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    for (int i = 0; i < g_mv_col_count; i++)
        inner_cell();
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* Port of FUN_00405c2f — half-shifted row: the left-edge half (phase 3), col_count - 1 full cells, the right-edge
 * half (phase 4).  The edge cells cast no county vote. */
void engine_map_terrain_row_shifted(void)
{
    CALL_TRACE_ENTER(0x00405c2f);
    g_mv_cur_col = g_mv_col_start;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cell = cell_at(g_mv_col_start, g_mv_cur_row);
    g_mv_cur_col = g_mv_col_start + 1;
    if (g_mv_cell < CELL_EMPTY) {
        engine_map_terrain_drawer_a(3);
        engine_map_terrain_drawer_b(3);
    } else {
        empty_index();
        if (*g_gate != 0 || g_gfx_last_glyph_index >= 7)
            engine_map_terrain_empty_left();
    }
    g_gfx_cursor_x += g_mv_x_half;
    for (int i = 0; g_mv_col_count - 1 > i; ++i)
        inner_cell();
    g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
    if (g_mv_cell < CELL_EMPTY) {
        engine_map_terrain_drawer_a(4);
        engine_map_terrain_drawer_b(4);
    } else {
        empty_index();
        if (*g_gate != 0 || g_gfx_last_glyph_index >= 7)
            engine_map_terrain_empty_right();
    }
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    g_mv_col_y_accum += g_mv_y_step;
}

/* A phantom row (phase 2 top / 1 bottom): every empty cell draws (no gate), no vote. */
static void phantom_row(int phase)
{
    for (int v = 0; v < g_mv_col_count; ++v) {
        g_mv_cell = cell_at(g_mv_cur_col++, g_mv_cur_row);
        if (g_mv_cell < CELL_EMPTY) {
            engine_map_terrain_drawer_a(phase);
            engine_map_terrain_drawer_b(phase);
        } else {
            empty_index();
            engine_map_terrain_empty_cell(phase);
        }
        g_gfx_cursor_x += g_mv_x_step;
    }
}

/* Port of FUN_0040526e — one pass-1 frame. */
void engine_map_terrain_frame(void)
{
    CALL_TRACE_ENTER(0x0040526e);
    g_gfx_cursor_y = g_mv_cam_y0;
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_row = g_mv_row_start;
    g_mv_col_y_accum = 0;
    g_mv_cur_col = g_mv_col_start;
    phantom_row(2);
    g_gfx_cursor_y += g_mv_y_step;
    ++g_mv_cur_row;
    engine_map_terrain_row_shifted();
    for (int i = 0; (g_mv_row_count - 2) / 2 > i; ++i) {
        engine_map_terrain_row_aligned();
        engine_map_terrain_row_shifted();
    }
    g_gfx_cursor_x = g_mv_cam_x0;
    g_mv_cur_col = g_mv_col_start;
    phantom_row(1);
}
