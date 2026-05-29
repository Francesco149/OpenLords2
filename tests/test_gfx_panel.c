/*
 * Unit tests for gfx_panel — the tiled stone-panel renderer
 * (ports of FUN_0040946d / FUN_004097c5 / FUN_00409346).
 *
 * Strategy: build a synthetic panel atlas where sprite index `i` is a
 * 16×16 tile whose every pixel is the marker value (i + 1) — non-zero so
 * both the transparent (border) and opaque (interior) blits copy it.
 * After drawing, the top-left pixel of each 16-px canvas cell tells us
 * which sprite index landed there, so we can assert the 9-patch border
 * selection, the interior atlas indexing, and cursor placement.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite.h"
#include "gfx_panel.h"

#include <string.h>

/* MAX_IDX covers the +0xcc "second style" block so mode>0 tiles resolve
 * (0x28 + 0xcc = 0xf4 is the highest index any test draws). */
enum { CANVAS_W = 128, CANVAS_H = 128, MAX_IDX = 0xf5, TILE = 0x10 };

static uint8_t g_canvas[CANVAS_W * CANVAS_H];

/* Atlas: 8-byte header, (MAX_IDX+1) × 16-byte descriptors, then
 * (MAX_IDX+1) × 256-byte tiles.  Tile i is filled with marker (i+1). */
static uint8_t g_atlas[8 + (MAX_IDX + 1) * 0x10 + (MAX_IDX + 1) * 256];

static void atlas_build(void)
{
    memset(g_atlas, 0, sizeof g_atlas);
    int pix_base = 8 + (MAX_IDX + 1) * 0x10;
    for (int i = 0; i <= MAX_IDX; i++) {
        uint8_t *d = g_atlas + 8 + i * 0x10;
        int off = pix_base + i * 256;
        d[0] = 0x10; d[1] = 0;          /* width  = 16 */
        d[2] = 0x10; d[3] = 0;          /* height_short = 16 */
        d[4] = off & 0xff;              /* src_offset (LE) */
        d[5] = (off >> 8) & 0xff;
        d[6] = (off >> 16) & 0xff;
        d[7] = (off >> 24) & 0xff;
        d[0xd] = 0x10;                  /* row_count = 16 */
        memset(g_atlas + off, i + 1, 256);   /* marker = i+1 */
    }
}

static void panel_reset(void)
{
    memset(g_canvas, 0, sizeof g_canvas);
    gfx_paint_bind_canvas(g_canvas, CANVAS_W, CANVAS_H);
    gfx_dispatch_set_clip_x_mode(0);
    gfx_dispatch_set_glyph_height_short(0);
    gfx_glyph_set_clip(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(0, 0, 0);
    gfx_glyph_set_color_modulation(0);
    atlas_build();
}

/* Marker at tile cell (col, row) with the panel anchored at (px, py). */
static uint8_t cell(int px, int py, int col, int row)
{
    int x = px + col * TILE;
    int y = py + row * TILE;
    return g_canvas[x + y * CANVAS_W];
}

/* ── border 9-patch selection (FUN_0040946d) ─────────────────────── */

int test_panel_border_3x3_corner_and_edge_indices(void)
{
    panel_reset();
    gfx_panel_draw_border(g_atlas, /*mode=*/0, /*x=*/0, /*y=*/0, 3, 3);

    /* corners */
    T_ASSERT_EQ_I(cell(0,0, 0,0), 0 + 1);      /* top-left  = sprite 0 */
    T_ASSERT_EQ_I(cell(0,0, 2,0), 1 + 1);      /* top-right = sprite 1 */
    T_ASSERT_EQ_I(cell(0,0, 0,2), 3 + 1);      /* bottom-left  = 3 */
    T_ASSERT_EQ_I(cell(0,0, 2,2), 2 + 1);      /* bottom-right = 2 */
    /* edges (c/r == 1) */
    T_ASSERT_EQ_I(cell(0,0, 1,0), 0x04 + 1);   /* top edge    = 0x04 */
    T_ASSERT_EQ_I(cell(0,0, 1,2), 0x10 + 1);   /* bottom edge = 0x10 */
    T_ASSERT_EQ_I(cell(0,0, 0,1), 0x1c + 1);   /* left edge   = 0x1c */
    T_ASSERT_EQ_I(cell(0,0, 2,1), 0x28 + 1);   /* right edge  = 0x28 */
    return 0;
}

int test_panel_border_skips_interior(void)
{
    panel_reset();
    gfx_panel_draw_border(g_atlas, 0, 0, 0, 3, 3);
    /* Center cell (1,1) is interior → never drawn → canvas stays 0. */
    T_ASSERT_EQ_I(cell(0,0, 1,1), 0);
    return 0;
}

int test_panel_border_mode2_swaps_top_corners(void)
{
    panel_reset();
    gfx_panel_draw_border(g_atlas, /*mode=*/2, 0, 0, 3, 3);
    /* mode 2 → top corners use 0x1c / 0x28 (not 0 / 1); then mode>0 adds
     * +0xcc to EVERY index, so the tiles drawn are 0xe8 / 0xf4. */
    T_ASSERT_EQ_I(cell(0,0, 0,0), 0x1c + 0xcc + 1);   /* 0xe8 */
    T_ASSERT_EQ_I(cell(0,0, 2,0), 0x28 + 0xcc + 1);   /* 0xf4 */
    /* bottom corners keep their indices (3 / 2) but also get +0xcc. */
    T_ASSERT_EQ_I(cell(0,0, 0,2), 3 + 0xcc + 1);      /* 0xcf */
    return 0;
}

int test_panel_border_long_edge_tiles_mod12(void)
{
    panel_reset();
    /* 15 cols → top edge cells c=1..13 use (c-1)%12 + 4, so c=13 wraps:
     * (13-1)%12 = 0 → sprite 4 again. */
    gfx_panel_draw_border(g_atlas, 0, 0, 0, 15, 3);
    T_ASSERT_EQ_I(cell(0,0, 1,0), 0x04 + 1);    /* (1-1)%12+4  = 4 */
    T_ASSERT_EQ_I(cell(0,0, 12,0), 0x0f + 1);   /* (12-1)%12+4 = 0xf */
    T_ASSERT_EQ_I(cell(0,0, 13,0), 0x04 + 1);   /* (13-1)%12+4 = 4 (wrap) */
    return 0;
}

int test_panel_border_anchored_offset(void)
{
    panel_reset();
    gfx_panel_draw_border(g_atlas, 0, /*x=*/0x20, /*y=*/0x10, 3, 3);
    T_ASSERT_EQ_I(cell(0x20, 0x10, 0, 0), 0 + 1);   /* corner at the anchor */
    T_ASSERT_EQ_I(cell(0x20, 0x10, 2, 2), 2 + 1);
    /* nothing at (0,0) of the canvas */
    T_ASSERT_EQ_I(g_canvas[0], 0);
    return 0;
}

/* ── interior fill (FUN_004097c5) ────────────────────────────────── */

int test_panel_fill_interior_atlas_indices(void)
{
    panel_reset();
    gfx_panel_fill_interior(g_atlas, 0, 0, 2, 2);
    /* idx = c%12 + (r%12)*12 + 0x34 */
    T_ASSERT_EQ_I(cell(0,0, 0,0), 0x34 + 1);   /* 0 + 0  + 0x34 */
    T_ASSERT_EQ_I(cell(0,0, 1,0), 0x35 + 1);   /* 1 + 0  + 0x34 */
    T_ASSERT_EQ_I(cell(0,0, 0,1), 0x40 + 1);   /* 0 + 12 + 0x34 */
    T_ASSERT_EQ_I(cell(0,0, 1,1), 0x41 + 1);   /* 1 + 12 + 0x34 */
    return 0;
}

/* ── composed panel (FUN_00409346) ───────────────────────────────── */

int test_panel_draw_border_plus_inset_fill(void)
{
    panel_reset();
    /* 4×4 panel: border over 4×4 at (0,0); interior fill 2×2 at (0x10,0x10). */
    gfx_panel_draw(g_atlas, 0, 0, 4, 4);
    /* border corner present */
    T_ASSERT_EQ_I(cell(0,0, 0,0), 0 + 1);
    /* interior fill begins one tile in, sprite 0x34 */
    T_ASSERT_EQ_I(cell(0x10, 0x10, 0, 0), 0x34 + 1);
    return 0;
}

int test_panel_bad_offset_aborts(void)
{
    panel_reset();
    /* Corrupt sprite 0's descriptor to an out-of-range offset; the very
     * first border tile should abort the draw with nothing rendered. */
    uint8_t *d = g_atlas + 8 + 0 * 0x10;
    d[4] = 0; d[5] = 0; d[6] = 0; d[7] = 0;   /* offset = 0 (< 1) */
    gfx_panel_draw_border(g_atlas, 0, 0, 0, 3, 3);
    T_ASSERT_EQ_I(cell(0,0, 0,0), 0);          /* nothing drawn */
    T_ASSERT_EQ_I(cell(0,0, 2,0), 0);          /* aborted before later tiles */
    return 0;
}
