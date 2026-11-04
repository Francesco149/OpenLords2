/*
 * OpenLords2 — tiled stone-panel renderer.
 *
 * Draws the LotR2 UI panels (the gray stone frame behind menus, dialogs,
 * etc.) by tiling 16×16 sprites out of a panel sprite-sheet asset.  A
 * panel is two passes:
 *
 *   gfx_panel_draw_border  ← FUN_0040946d (856 B)  — the 9-patch border:
 *       four corners + the four edges (interior tiles are skipped).  Edge
 *       runs index the atlas modulo 12 so arbitrarily long edges tile
 *       from a 12-sprite strip.  `mode > 0` shifts every index by +0xcc
 *       (a second panel style); `mode == 2` swaps the two top corners for
 *       the 0x1c/0x28 variants.  Each tile is a transparent blit
 *       (gfx_sprite_blit) with the upstream clip applied, so partially
 *       off-screen panels clip cleanly.
 *
 *   gfx_panel_fill_interior ← FUN_004097c5 (367 B) — the interior fill:
 *       a full cols×rows grid of OPAQUE 16×16 tiles
 *       (gfx_sprite_blit_16x16_raw), indexed from a 12×12 atlas block
 *       starting at sprite 0x34.
 *
 *   gfx_panel_draw          ← FUN_00409346 (81 B)  — composes the two:
 *       border at (x, y) over cols×rows, then the interior fill inset by
 *       one tile (x+0x10, y+0x10) and two tiles smaller (cols-2, rows-2).
 *
 * All three drive the already-ported gfx_sprite blitters via the shared
 * gfx_glyph / gfx_dispatch state (cursor, descriptor, clip).  The panel
 * sprite-sheet is passed as a parameter (retail's DAT_004eabec etc).
 * Pure of Win32 beyond the gfx canvas binding.
 *
 * Per-tile descriptor layout in the asset (16 bytes at asset+8+idx*16):
 *   +0  short  width        +2  short  height_short
 *   +4  int    src_offset   +0xc byte  flag_a   (staged, unused by blit)
 *   +0xd byte  row_count    +0xe byte  flag_b   (staged, unused by blit)
 *
 * Deviation: on a bad src_offset retail logs via 0x4afab9 and sets
 * the error latch to 1 via FUN_00408fb0(1) before aborting.  We abort
 * (early return) faithfully but elide the latch write — there is no
 * public setter for an arbitrary latch value and nothing in the panel
 * path consumes latch==1.  A valid asset never trips this gate.
 */

#include <stddef.h>

#include "gfx_panel.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite.h"

/* Panel asset descriptor accessors (16-byte stride, 8-byte header). */
#define DESC(asset, idx) ((asset) + 8 + (idx) * 0x10)

/* Stage a tile descriptor into the gfx_glyph/gfx_dispatch sprite state,
 * apply the upstream clip, and return 0 on success or -1 if the
 * descriptor's src_offset is out of range (caller aborts). */
static int stage_tile(const uint8_t *asset, int idx, int cursor_x, int cursor_y)
{
    const uint8_t *d = DESC(asset, idx);
    int width  = (int)(int16_t)(d[0] | (d[1] << 8));
    int hshort = (int)(int16_t)(d[2] | (d[3] << 8));
    int offset = (int)((uint32_t)d[4] | ((uint32_t)d[5] << 8) |
                       ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24));
    int rows   = d[0xd];

    if (offset > 310000 || offset < 1) {
        return -1;   /* retail logs + latches error 1, then aborts */
    }

    gfx_glyph_set_dest(cursor_x, cursor_y);
    gfx_glyph_set_glyph(offset, width, rows);
    gfx_dispatch_set_glyph_height_short(hshort);
    return 0;
}

/* Common tail for the BORDER tiles: stage, clip, adjust source offset by
 * the clip skips, then dispatch the clip-appropriate transparent blit. */
static int blit_border_tile(const uint8_t *asset, int idx,
                            int cursor_x, int cursor_y)
{
    if (stage_tile(asset, idx, cursor_x, cursor_y) != 0) {
        return -1;
    }

    gfx_dispatch_clip_x(0, 0x280);
    gfx_dispatch_clip_y(0, 0x1e0);

    /* offset += width * top_skip + left_skip  (skips are the clip-induced
     * source advance; both 0 for a fully on-screen tile). */
    int width  = gfx_glyph_glyph_w();
    int offset = gfx_glyph_glyph_offset();
    int rows   = gfx_glyph_glyph_h();
    offset += width * gfx_dispatch_top_skip() + gfx_dispatch_left_skip();
    gfx_glyph_set_glyph(offset, width, rows);

    if (gfx_dispatch_clip_x_mode() == 1) {
        gfx_sprite_blit_left_clipped(asset);
    } else if (gfx_dispatch_clip_x_mode() == 2) {
        gfx_sprite_blit_right_clipped(asset);
    } else if (gfx_dispatch_clip_y_mode() != 5) {
        gfx_sprite_blit(asset);
    }
    return 0;
}

/* Port of FUN_0040946d. */
void gfx_panel_draw_border(const uint8_t *asset, int mode,
                           int x, int y, int cols, int rows)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx;

            if (r == 0 && c == 0) {
                idx = (mode == 2) ? 0x1c : 0;            /* top-left */
            } else if (r == 0 && c == cols - 1) {
                idx = (mode == 2) ? 0x28 : 1;            /* top-right */
            } else if (r == rows - 1 && c == 0) {
                idx = 3;                                 /* bottom-left */
            } else if (r == rows - 1 && c == cols - 1) {
                idx = 2;                                 /* bottom-right */
            } else if (r == 0 && mode != 2) {
                idx = (c - 1) % 0xc + 4;                 /* top edge */
            } else if (r == rows - 1) {
                idx = (c - 1) % 0xc + 0x10;              /* bottom edge */
            } else if (c == 0) {
                idx = (r - 1) % 0xc + 0x1c;              /* left edge */
            } else if (c == cols - 1) {
                idx = (r - 1) % 0xc + 0x28;              /* right edge */
            } else {
                continue;                                /* interior: skip */
            }

            if (mode > 0) {
                idx += 0xcc;                             /* second style */
            }

            if (blit_border_tile(asset, idx,
                                 c * 0x10 + x, r * 0x10 + y) != 0) {
                return;                                  /* bad offset → abort */
            }
        }
    }
}

/* Port of FUN_004097c5. */
void gfx_panel_fill_interior(const uint8_t *asset, int x, int y,
                             int cols, int rows)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c % 0xc + (r % 0xc) * 0xc + 0x34;
            if (stage_tile(asset, idx, c * 0x10 + x, r * 0x10 + y) != 0) {
                return;                                  /* bad offset → abort */
            }
            gfx_sprite_blit_16x16_raw(asset);
        }
    }
}

/* Port of FUN_00409346. */
void gfx_panel_draw(const uint8_t *asset, int x, int y, int cols, int rows)
{
    gfx_panel_draw_border(asset, 0, x, y, cols, rows);
    gfx_panel_fill_interior(asset, x + 0x10, y + 0x10, cols - 2, rows - 2);
}

/* Port of FUN_004093e0 @ 0x4093e0 (73 B).  The raised "bevel" frame the
 * chrome UI (button caps, the page-7 option tabs) draws from panels.pl8.
 * Identical composition to gfx_panel_draw except the border is mode 1 —
 * which shifts every sprite index by +0xcc, selecting the second
 * (raised/chrome) style block in the sheet.  Retail hardcodes the chrome
 * sheet 0x57d3d0 (0x409934/0x409c93 are the 0040946d/004097c5
 * twins with that asset baked in); the caller passes the bound blob. */
void gfx_panel_draw_bevel(const uint8_t *asset, int x, int y, int cols, int rows)
{
    gfx_panel_draw_border(asset, 1, x, y, cols, rows);
    gfx_panel_fill_interior(asset, x + 0x10, y + 0x10, cols - 2, rows - 2);
}
