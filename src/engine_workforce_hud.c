/*
 * OpenLords2 — strategic-map workforce icon-grid overlay.  See
 * engine_workforce_hud.h for the full retail address map + design notes.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "engine_workforce_hud.h"
#include "save.h"                  /* g_per_army_768 */
#include "save_schema_globals.h"   /* s_slot_0053f25c */
#include "engine_map_scene.h"      /* engine_map_scene_tick */
#include "io_path.h"               /* io_path_chdir_base */
#include "io.h"                    /* io_read_file_at */
#include "gfx_globals.h"            /* the staged cursor / glyph dims the backdrop walk reads */
#include "gfx_paint.h"              /* gfx_paint_canvas_pixels/width */
#include "gfx_wrap.h"              /* the standings lines */
#include "gfx_sprite_render.h"     /* gfx_sprite_render, gfx_sprite_render_
                                       opaque_from_blob, gfx_sprite_render_
                                       set_blob, gfx_sprite_seal_draw */

#define ARMY_REC(key)  (&g_per_army_768[(key) * 0x300])

/* Per-army fields — see engine_workforce_hud.h's top comment for the
 * inferred-semantics caveat. */
#define ARMY_WF_SCALE      0xb8   /* DAT_0053fa68 */
#define ARMY_WF_ROW_STRIDE 0xc
#define ARMY_WF_CUR(row)   (0xc4 + (row) * ARMY_WF_ROW_STRIDE)  /* DAT_0053fa74 */
#define ARMY_WF_MIN(row)   (0xc8 + (row) * ARMY_WF_ROW_STRIDE)  /* DAT_0053fa78 */
#define ARMY_WF_MAX(row)   (0xcc + (row) * ARMY_WF_ROW_STRIDE)  /* DAT_0053fa7c */

/* Capital "special feature" present-flags (engine_territory.c's own
 * file-static #defines, mirrored here — see engine_workforce_hud.h). */
#define ARMY_FEAT_A_FLAG 0x2ad   /* DAT_0053fc5d — descriptor 0x1e present */
#define ARMY_FEAT_B_FLAG 0x2dd   /* DAT_0053fc8d — descriptor 0 present    */
#define ARMY_FEAT_C_FLAG 0x295   /* DAT_0053fc45 — descriptor 0x14 present */

static int32_t rd_i32(const uint8_t *rec, int off)
{
    int32_t v;
    memcpy(&v, rec + off, sizeof v);
    return v;
}

/* ─── Baked rdata tables (r2-extracted, byte-exact) ─────────────────────── */

/* DAT_004d6780 — workforce category (0..7) -> sprite-row index. */
static const int k_workforce_row_index[8] = { 5, 0, 1, 3, 6, 7, 8, 2 };

/* DAT_004d6808 — sprite-row index (0..8) -> default fill byte. */
static const int k_workforce_fill_byte[9] = { 4, 8, 10, 14, 16, 18, 20, 22, 2 };

/* DAT_004d6830 — per-cell fill-order threshold (engine_workforce_fill_row_linear). */
static const uint8_t k_workforce_cell_threshold[25] = {
    10, 22,  6, 19, 11, 25,  5, 15,  2, 23,  9, 14,  1,
    16,  7, 18,  4, 17,  3, 20, 13, 21,  8, 24, 12
};

/* DAT_004d67a0 — per-cell default colour (engine_workforce_fill_row_linear,
 * fill_code == 0). */
static const int k_workforce_cell_default_color[25] = {
     4,  6, 10, 16, 20, 22,  4,  6, 10, 16,  4,  6, 10,
    16, 20, 22,  4,  6, 10, 16,  4,  6, 10, 16, 20
};

/* DAT_004d6850 / DAT_004d6860 — small-value graduated thresholds
 * (engine_workforce_fill_row_dual, both position and magnitude < 13). */
static const uint8_t k_workforce_small_pos_threshold[15] = {
     8,  5,  2,  7, 10,  6,  3,  1,  4,  9, 11, 12, 13, 99, 99
};
static const uint8_t k_workforce_small_mag_threshold[15] = {
    99, 99, 99, 12, 10, 11,  8,  5,  4,  7,  9,  3,  1,  2,  6
};

/* DAT_004d85e8 / DAT_004d85e9 (interleaved) — per-cell (x,y) draw offset,
 * 25 cells. */
static const uint8_t k_workforce_cell_xy[25][2] = {
    { 0,0},{16,0},{32,0},{48,0},{64,0},
    { 4,12},{20,12},{36,12},{52,12},{68,12},
    { 8,24},{24,24},{40,24},{56,24},{72,24},
    { 0,36},{16,36},{32,36},{48,36},{64,36},
    { 4,48},{20,48},{36,48},{52,48},{68,48},
};

/* DAT_004d85a8 / DAT_004d85ac (interleaved i32) — per-row (x,y) base
 * position, 8 rows.  r2-cross-checked byte-exact via two independent
 * extraction methods (p8 hex dump + `pf` struct print). */
static const int k_workforce_row_xy[8][2] = {
    { 22, 50}, {147, 30}, {271, 26}, {268,188},
    {117,200}, { 15,245}, {146,120}, { 19,122},
};

/* ─── Runtime state ──────────────────────────────────────────────────────── */

/* Icon-grid output: 8 categories x 25 cells, read by engine_workforce_draw_row. */
/* The grid is retail's DAT_005540a0 (8 × 25 bytes, engine_workforce_hud_globals.c); the dual fill's "indicator"
 * writes land at DAT_005540aa — the SAME grid ten cells on (retail quirk: the indicator overwrites the row's cells
 * 10..24). */
#define WF_GRID(cat, cell) g_wf_grid[(cat) * 25 + (cell)]

/* Ancestor-owned seams (see engine_workforce_hud.h). */
/* the icon sheet: g_wf_icon_sheet (retail DAT_005530c8, read at the draw) */
/* the panel mode: g_wf_panel_mode (retail DAT_0053f050) */
/* the standings text (loc grp 0x42 / 0x16) — bound by the caller; NULL loc → skipped */
static localization_state_t *g_wf_loc;
static const uint8_t *g_wf_title_font, *g_wf_menu_font, *g_wf_glyph_table;
static engine_dirty_rect_state_t *g_wf_dirty;     /* FUN_0045270a's rectangle; NULL → no mark */
static int (*g_wf_ensure_fn)(void);               /* FUN_004997bf (the hybrid host: retail's) */
static int g_wf_scratch_in_blob;                  /* the vill / villtops sheets + the capture in DAT_004eabec's buffer */
void engine_workforce_hud_bind_text(localization_state_t *loc, const uint8_t *title_font, const uint8_t *menu_font,
                                    const uint8_t *glyph_table)
{
    g_wf_loc = loc;
    g_wf_title_font = title_font;
    g_wf_menu_font = menu_font;
    g_wf_glyph_table = glyph_table;
}
void engine_workforce_hud_bind_retail(engine_dirty_rect_state_t *dirty, int (*ensure)(void))
{
    g_wf_dirty = dirty;
    g_wf_ensure_fn = ensure;
    g_wf_scratch_in_blob = ensure != NULL;
}

static const uint8_t *g_wf_system_sheet;   /* &DAT_005bb540 (the seal) */
void engine_workforce_hud_bind_system_sheet(const uint8_t *sheet) { g_wf_system_sheet = sheet; }

void engine_workforce_hud_bind_icon_sheet(const uint8_t *sheet)
{
    g_wf_icon_sheet = sheet;
}

void engine_workforce_hud_set_panel_mode(int mode)
{
    g_wf_panel_mode = mode;
}

/* Scratch sprite-sheet buffers (FUN_004997bf's DAT_0053e918/DAT_005533c4)
 * plus a port-owned buffer for the vill.pl8 sheet + backdrop-capture reuse
 * retail does via the ancestor-owned DAT_004eabec (see engine_workforce_
 * hud.h's design note — no ancestor screen exists yet to own this). */
/* g_wf_villani1 / g_wf_villani2: retail's DAT_0053e918 / DAT_005533c4 (engine_workforce_hud_globals.c) */
static uint8_t *g_workforce_scratch_buf;    /* port-owned, sized for both
                                              vill.pl8 (120000 B) and the
                                              backdrop capture (120*4*320 =
                                              153600 B) */

#define WORKFORCE_VILLANI1_ALLOC 250000
#define WORKFORCE_VILLANI2_ALLOC 150000
#define WORKFORCE_SCRATCH_ALLOC  160000

int engine_workforce_hud_ensure_buffers(void)
{
    /* Retail (FUN_004997bf) reallocates both buffers unconditionally on
     * every call — no existing-pointer guard in the decompile.  Ported
     * verbatim (free-then-realloc has the same net effect without leaking
     * across repeated calls within one process, which retail's raw
     * malloc-over-malloc does not guarantee either way). */
    free(g_wf_villani1);
    free(g_wf_villani2);
    free(g_workforce_scratch_buf);

    g_wf_villani1 = malloc(WORKFORCE_VILLANI1_ALLOC);
    g_wf_villani2 = malloc(WORKFORCE_VILLANI2_ALLOC);
    g_workforce_scratch_buf  = malloc(WORKFORCE_SCRATCH_ALLOC);

    if (!g_wf_villani1 || !g_wf_villani2
        || !g_workforce_scratch_buf) {
        free(g_wf_villani1);
        free(g_wf_villani2);
        free(g_workforce_scratch_buf);
        g_wf_villani1 = NULL;
        g_wf_villani2 = NULL;
        g_workforce_scratch_buf  = NULL;
        return 0;
    }
    return 1;
}

int engine_workforce_row_sprite_index(int army_key, int category)
{
    int row = k_workforce_row_index[category];
    if (category == 0
        && ARMY_REC(army_key)[ARMY_FEAT_B_FLAG] == 0
        && ARMY_REC(army_key)[ARMY_FEAT_A_FLAG] != 0) {
        row = 4;
    }
    return row;
}

/* Port of FUN_004b3f0a.  Retail's walk: src = Buf + cursor_x + cursor_y * xRight (DAT_00591524 / DAT_00591528,
 * the canvas pitch DAT_004eb274); per row `rep movsd` of glyph_w (DAT_005c9258, movsx) dwords, then src += the
 * residual stride; the row count is the descriptor height DAT_005aeb70 (movsx), a do-while (one row at least). */
void engine_workforce_hud_capture_backdrop(uint32_t *dest, int residual_stride)
{
    const uint8_t *src = gfx_paint_canvas_pixels();
    if (!src || !dest) return;
    src += g_gfx_cursor_x + (uint32_t)g_gfx_cursor_y * (uint32_t)g_gfx_canvas_width;
    int w = g_gfx_glyph_w, rows = g_gfx_glyph_height_short;
    do {
        /* retail copies dwords regardless of alignment (fine on x86); memcpy keeps the host sanitizers quiet */
        memcpy(dest, src, (size_t)w * 4);
        dest += w;
        src += (size_t)w * 4 + residual_stride;
    } while (--rows > 0);
}

/* Port of FUN_004b3ec0 — the write-back sibling (the same walk, the copy reversed).  The tooltip clear
 * (FUN_004770b8) and the armory march restore put the saved under-box pixels back through it. */
void engine_workforce_hud_restore_backdrop(const uint32_t *src, int residual_stride)
{
    uint8_t *dst = gfx_paint_canvas_pixels();
    if (!dst || !src) return;
    dst += g_gfx_cursor_x + (uint32_t)g_gfx_cursor_y * (uint32_t)g_gfx_canvas_width;
    int w = g_gfx_glyph_w, rows = g_gfx_glyph_height_short;
    do {
        memcpy(dst, src, (size_t)w * 4);
        src += w;
        dst += (size_t)w * 4 + residual_stride;
    } while (--rows > 0);
}

void engine_workforce_fill_row_linear(int category, int fill_code, int count)
{
    if (category == 6) fill_code = 2;
    for (int cell = 0; cell < 25; cell++) {
        if (k_workforce_cell_threshold[cell] <= count) {
            WF_GRID(category, cell) = fill_code != 0
                ? (uint8_t)fill_code
                : (uint8_t)k_workforce_cell_default_color[cell];
        }
    }
}

void engine_workforce_fill_row_dual(int category, uint8_t fill_byte,
                                    uint8_t indicator_code,
                                    int position, int magnitude)
{
    if (category == 6) {
        fill_byte = 2;
        indicator_code = 2;
    }
    if (position < 13 && magnitude < 13) {
        for (int cell = 0; cell < 15; cell++) {
            if (k_workforce_small_pos_threshold[cell] <= position)
                WF_GRID(category, cell) = fill_byte;
            if (k_workforce_small_mag_threshold[cell] <= magnitude)
                WF_GRID(category, cell + 10) = indicator_code;   /* DAT_005540aa[25 · category + cell] */
        }
    } else {
        for (int cell = 0; cell < position; cell++)
            WF_GRID(category, cell) = fill_byte;
        for (int cell = 24; 24 - magnitude < cell
                            && WF_GRID(category, cell) == 0
                            && cell >= 0; cell--) {
            WF_GRID(category, cell) = indicator_code;
        }
    }
}

void engine_workforce_compute_row(int army_key, int category,
                                  int position, int magnitude)
{
    int row = engine_workforce_row_sprite_index(army_key, category);
    int fill_byte = k_workforce_fill_byte[row];

    memset(&WF_GRID(category, 0), 0, 25);

    if (position == 0 && magnitude == 0) return;

    if (magnitude == 0) {
        engine_workforce_fill_row_linear(category, fill_byte, position);
    } else if (position == 0) {
        if (magnitude < 1)
            engine_workforce_fill_row_linear(category, 1, -magnitude);
        else
            engine_workforce_fill_row_linear(category, 2, magnitude);
    } else {
        uint8_t fb = (uint8_t)fill_byte;
        if (magnitude < 1)
            engine_workforce_fill_row_dual(category, fb, 1, position, -magnitude);
        else
            engine_workforce_fill_row_dual(category, fb, 2, position, magnitude);
    }
}

void engine_workforce_compute_rows(int army_key)
{
    int scale = (uint8_t)ARMY_REC(army_key)[ARMY_WF_SCALE];
    if (scale == 0) return;

    for (int category = 0; category < 8; category++) {
        int row = engine_workforce_row_sprite_index(army_key, category);
        int32_t cur = rd_i32(ARMY_REC(army_key), ARMY_WF_CUR(row));
        int32_t lo  = rd_i32(ARMY_REC(army_key), ARMY_WF_MIN(row));
        int32_t hi  = rd_i32(ARMY_REC(army_key), ARMY_WF_MAX(row));

        int magnitude;
        if (cur < lo) {
            int mag = (lo - cur) / scale;
            if ((lo - cur) % scale != 0) mag++;
            magnitude = -mag;
        } else if (hi < cur) {
            int mag = (cur - hi) / scale;
            if (hi == 0 && cur % scale != 0) mag++;
            magnitude = mag;
        } else {
            magnitude = 0;
        }

        int position = cur / scale;
        if (cur % scale != 0) position++;
        if (hi < cur) position -= magnitude;

        engine_workforce_compute_row(army_key, category, position, magnitude);
    }
}

const uint8_t *engine_workforce_hud_icon_sheet(void)
{
    return g_wf_icon_sheet;
}

uint8_t engine_workforce_hud_grid_cell(int category, int cell)
{
    return WF_GRID(category, cell);
}

void engine_workforce_draw_row(int category, int x_base, int y_base)
{
    if (!g_wf_icon_sheet) return;

    for (int cell = 0; cell < 25; cell++) {
        /* The hovered category's highlight (DAT_0053e9a0 = category + 1, its per-cell bytes DAT_00553040): the
         * next frame. */
        int highlight = 0;
        if (g_wf_hover_category - 1 == category) {
            highlight = g_wf_hover_cells[cell] != 0;
        }

        uint8_t cell_val = WF_GRID(category, cell);
        if (cell_val != 0) {
            int x = x_base + k_workforce_cell_xy[cell][0] - 2;
            int y = y_base + k_workforce_cell_xy[cell][1] - 0x18;
            gfx_sprite_render_from_blob(g_wf_icon_sheet,                  /* FUN_0040a21a */
                                        cell_val - 1 + highlight,
                                        x, y);
        }
    }
}

void engine_workforce_draw_all_rows(int y_origin)
{
    for (int category = 0; category < 8; category++) {
        int x = k_workforce_row_xy[category][0] + 0x40;
        int y = k_workforce_row_xy[category][1] + y_origin;
        engine_workforce_draw_row(category, x, y);
    }
}

void engine_workforce_hud_render(int army_key, uint32_t now_ms,
                                 int full_redraw)
{
    if (g_wf_panel_mode != 1) return;      /* DAT_0053f050 */

    if (full_redraw)
        engine_map_scene_tick(now_ms);     /* FUN_004050c0 */

    io_path_chdir_base();
    if (g_wf_ensure_fn) {
        g_wf_ensure_fn();                  /* FUN_004997bf (retail's: its own heap) */
    } else if (!engine_workforce_hud_ensure_buffers()) {
        return;
    }

    io_read_file_at("villani1.pl8", g_wf_villani1, 0x249f0, 0);
    io_read_file_at("villani2.pl8", g_wf_villani2, 0x249f0, 0);
    /* The icon sheet DAT_005530c8 is the caller's; the standalone defaults it to the villani1 bank. */
    if (!g_wf_icon_sheet && !g_wf_scratch_in_blob)
        g_wf_icon_sheet = g_wf_villani1;
    if (g_wf_dirty)
        engine_dirty_rect_mark_below_status_bar(g_wf_dirty);   /* FUN_0045270a */

    /* DAT_0053f25c — the "standings" option (s_slot_0053f25c); the panel origin DAT_0057c908 moves down with it. */
    g_wf_y_origin = (s_slot_0053f25c == 1) ? 0x84 : 0x40;
    int y_origin = g_wf_y_origin;

    /* vill.pl8 → the scratch DAT_004eabec points at (the standalone: its own), drawn as the current sheet. */
    uint8_t *scratch = g_wf_scratch_in_blob ? (uint8_t *)(uintptr_t)g_gfx_current_blob : g_workforce_scratch_buf;
    io_read_file_at("vill.pl8", scratch, 0x1d4c0, 0);
    gfx_sprite_render_set_blob(scratch);
    gfx_sprite_render(0, 0x40, y_origin);

    const uint8_t *rec = ARMY_REC(army_key);
    if (s_slot_0053f25c == 1) {
        /* The standings head: the castle kind's banner (villtops.pl8, +0x21b) + its two lines, the tax line. */
        int kind = rec[0x21b];
        uint8_t color = kind < 4 ? 0x3f : 0x20;
        io_read_file_at("villtops.pl8", scratch, 0x25990, 0);
        gfx_sprite_render(kind, 0x40, 0x40);
        if (g_wf_loc) {
            g_gfx_line_x_acc = 0;
            gfx_wrap_localized_line(g_wf_loc, 0x42, 2 * kind, 0x54, 0x4e, g_wf_title_font, g_wf_glyph_table, color);
            gfx_wrap_localized_line(g_wf_loc, 0x42, 2 * kind + 1, g_gfx_line_x_acc + 0x54, 0x54, g_wf_menu_font,
                                    g_wf_glyph_table, color);
            gfx_wrap_localized_line(g_wf_loc, 0x16, (rd_i32(rec, 0x208) + 100) / 29, 0x54, 0x6e, g_wf_menu_font,
                                    g_wf_glyph_table, color);
        }
    }

    /* The capital's three feature icons (villani2 sprites 0x29 / 0x28 / 0x2b; FUN_0040a21a). */
    if (rec[ARMY_FEAT_C_FLAG] != 0)
        gfx_sprite_render_from_blob(g_wf_villani2, 0x29, 0xac, y_origin + 0xe5);
    if (rec[ARMY_FEAT_B_FLAG] != 0)
        gfx_sprite_render_from_blob(g_wf_villani2, 0x28, 0x4c, y_origin + 0xc);
    if (rec[ARMY_FEAT_A_FLAG] != 0)
        gfx_sprite_render_from_blob(g_wf_villani2, 0x2b, 0x4c, y_origin + 0xc);

    /* stage (0, y_origin) / 0x78 dwords × 0x140 rows, residual stride 0xa0 (480 + 160 = the 640 pitch) */
    g_gfx_cursor_x = 0; g_gfx_cursor_y = y_origin; g_gfx_glyph_w = 0x78; g_gfx_glyph_height_short = 0x140;
    engine_workforce_hud_capture_backdrop((uint32_t *)scratch, 0xa0);

    engine_workforce_compute_rows(army_key);
    engine_workforce_draw_all_rows(y_origin);

    /* FUN_0040d1bc(0x180, y + 0x118, 1) — the "click to continue" seal off the system sheet. */
    gfx_sprite_seal_draw(g_wf_system_sheet, 0x180, y_origin + 0x118, 1);
}
