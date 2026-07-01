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
#include "gfx_paint.h"              /* gfx_paint_canvas_pixels/width */
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

/* Icon-grid output: 8 categories x 25 cells.  engine_workforce_draw_row
 * reads g_workforce_grid only; g_workforce_indicator is computed faithfully
 * (matching retail's DAT_005540aa writes) but has no reader in this
 * closure — see engine_workforce_hud.h's design note. */
static uint8_t g_workforce_grid[8][25];
static uint8_t g_workforce_indicator[8][25];

/* Ancestor-owned seams (see engine_workforce_hud.h). */
static const uint8_t *g_workforce_icon_sheet;   /* DAT_005530c8 */
static int             g_workforce_panel_mode;   /* DAT_0053f050 */

void engine_workforce_hud_bind_icon_sheet(const uint8_t *sheet)
{
    g_workforce_icon_sheet = sheet;
}

void engine_workforce_hud_set_panel_mode(int mode)
{
    g_workforce_panel_mode = mode;
}

/* Scratch sprite-sheet buffers (FUN_004997bf's DAT_0053e918/DAT_005533c4)
 * plus a port-owned buffer for the vill.pl8 sheet + backdrop-capture reuse
 * retail does via the ancestor-owned DAT_004eabec (see engine_workforce_
 * hud.h's design note — no ancestor screen exists yet to own this). */
static uint8_t *g_workforce_villani1_buf;   /* DAT_0053e918, 250000 B alloc */
static uint8_t *g_workforce_villani2_buf;   /* DAT_005533c4, 150000 B alloc */
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
    free(g_workforce_villani1_buf);
    free(g_workforce_villani2_buf);
    free(g_workforce_scratch_buf);

    g_workforce_villani1_buf = malloc(WORKFORCE_VILLANI1_ALLOC);
    g_workforce_villani2_buf = malloc(WORKFORCE_VILLANI2_ALLOC);
    g_workforce_scratch_buf  = malloc(WORKFORCE_SCRATCH_ALLOC);

    if (!g_workforce_villani1_buf || !g_workforce_villani2_buf
        || !g_workforce_scratch_buf) {
        free(g_workforce_villani1_buf);
        free(g_workforce_villani2_buf);
        free(g_workforce_scratch_buf);
        g_workforce_villani1_buf = NULL;
        g_workforce_villani2_buf = NULL;
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

void engine_workforce_hud_capture_backdrop(uint32_t *dest, int x, int y,
                                           int width_dwords, int height,
                                           int src_stride)
{
    const uint8_t *canvas = gfx_paint_canvas_pixels();
    if (!canvas || !dest) return;
    int canvas_width = gfx_paint_canvas_width();
    const uint32_t *src =
        (const uint32_t *)(canvas + x + y * canvas_width);

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width_dwords; col++)
            dest[col] = src[col];
        dest += width_dwords;
        src = (const uint32_t *)((const uint8_t *)src + src_stride);
    }
}

void engine_workforce_fill_row_linear(int category, int fill_code, int count)
{
    if (category == 6) fill_code = 2;
    for (int cell = 0; cell < 25; cell++) {
        if (k_workforce_cell_threshold[cell] <= count) {
            g_workforce_grid[category][cell] = fill_code != 0
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
                g_workforce_grid[category][cell] = fill_byte;
            if (k_workforce_small_mag_threshold[cell] <= magnitude)
                g_workforce_indicator[category][cell] = indicator_code;
        }
    } else {
        for (int cell = 0; cell < position; cell++)
            g_workforce_grid[category][cell] = fill_byte;
        for (int cell = 24; 24 - magnitude < cell
                            && g_workforce_grid[category][cell] == 0
                            && cell >= 0; cell--) {
            g_workforce_grid[category][cell] = indicator_code;
        }
    }
}

void engine_workforce_compute_row(int army_key, int category,
                                  int position, int magnitude)
{
    int row = engine_workforce_row_sprite_index(army_key, category);
    int fill_byte = k_workforce_fill_byte[row];

    memset(g_workforce_grid[category], 0, 25);

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
    return g_workforce_icon_sheet;
}

uint8_t engine_workforce_hud_grid_cell(int category, int cell)
{
    return g_workforce_grid[category][cell];
}

void engine_workforce_draw_row(int category, int x_base, int y_base)
{
    if (!g_workforce_icon_sheet) return;

    for (int cell = 0; cell < 25; cell++) {
        int highlight = 0;
        /* Retail's hovered-category highlight (DAT_0053e9a0/DAT_00553040):
         * no producer for either global yet in this port, so this branch
         * is always false today — kept so a future hover-state chip can
         * wire it without touching this function again. */

        uint8_t cell_val = g_workforce_grid[category][cell];
        if (cell_val != 0) {
            int x = x_base + k_workforce_cell_xy[cell][0] - 2;
            int y = y_base + k_workforce_cell_xy[cell][1] - 0x18;
            gfx_sprite_render_opaque_from_blob(g_workforce_icon_sheet,
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
    if (g_workforce_panel_mode != 1) return;

    if (full_redraw)
        engine_map_scene_tick(now_ms);

    io_path_chdir_base();
    if (!engine_workforce_hud_ensure_buffers()) return;

    io_read_file_at("villani1.pl8", g_workforce_villani1_buf,
                    WORKFORCE_VILLANI1_ALLOC, 0);
    io_read_file_at("villani2.pl8", g_workforce_villani2_buf,
                    WORKFORCE_VILLANI2_ALLOC, 0);
    /* The icon sheet (DAT_005530c8) is ancestor-bound in retail — see
     * engine_workforce_hud.h.  Default it to the just-loaded villani1
     * bank so this render works standalone; a real ancestor screen (once
     * ported) can still override via engine_workforce_hud_bind_icon_sheet
     * before calling this. */
    if (!g_workforce_icon_sheet)
        g_workforce_icon_sheet = g_workforce_villani1_buf;
    /* engine_dirty_rect_mark_below_status_bar (FUN_0045270a) — deferred,
     * see engine_workforce_hud.h. */

    /* DAT_0053f25c — "standings enabled" (s_slot_0053f25c elsewhere in
     * this codebase); shifts the whole panel down when on. */
    int y_origin = (s_slot_0053f25c == 1) ? 0x84 : 0x40;

    io_read_file_at("vill.pl8", g_workforce_scratch_buf, 120000, 0);
    gfx_sprite_render_set_blob(g_workforce_scratch_buf);
    gfx_sprite_render(0, 0x40, y_origin);

    /* DEFERRED: the "standings" decoration sub-branch (villtops.pl8 +
     * castle icon + 3 wrapped tax/population numbers) — see
     * engine_workforce_hud.h. */

    if (ARMY_REC(army_key)[ARMY_FEAT_C_FLAG] != 0)
        gfx_sprite_render_opaque_from_blob(g_workforce_villani2_buf,
                                           0x29, 0xac, y_origin + 0xe5);
    if (ARMY_REC(army_key)[ARMY_FEAT_B_FLAG] != 0)
        gfx_sprite_render_opaque_from_blob(g_workforce_villani2_buf,
                                           0x28, 0x4c, y_origin + 0xc);
    if (ARMY_REC(army_key)[ARMY_FEAT_A_FLAG] != 0)
        gfx_sprite_render_opaque_from_blob(g_workforce_villani2_buf,
                                           0x2b, 0x4c, y_origin + 0xc);

    engine_workforce_hud_capture_backdrop(
        (uint32_t *)g_workforce_scratch_buf,
        /*x=*/0, /*y=*/y_origin, /*width_dwords=*/0x78, /*height=*/0x140,
        /*src_stride=*/0xa0);

    engine_workforce_compute_rows(army_key);
    engine_workforce_draw_all_rows(y_origin);

    /* FUN_0040d1bc — the "click to continue" seal stamp.  Retail reads a
     * fixed system-sheet global (DAT_005bb540); no bound seam exists yet
     * in this closure — a future chip should thread the real system
     * sheet through once this render path gets a live caller wired to
     * one.  Skipped for now (NULL sheet is a documented no-op per
     * gfx_sprite_seal_draw's own guard). */
    gfx_sprite_seal_draw(NULL, 0x180, y_origin + 0x118, 1);
}
