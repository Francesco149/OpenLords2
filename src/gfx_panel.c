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
 * Retail shape (rhost shadow, render-under-rhost.md): both passes save
 * the glyph cursor in DAT_00591508 / DAT_00591504 on entry and put it
 * back on exit; every tile writes the last-glyph index DAT_005c9288, the
 * cursor and the WHOLE descriptor (incl. the two flag bytes) before the
 * src_offset gate; a bad offset logs (FUN_004afab9, the pass's own
 * message) and latches error 1 (FUN_00408fb0) and returns WITHOUT
 * restoring the cursor.  The chrome twins FUN_00409934 / FUN_00409c93
 * are the same bodies with the chrome sheet (DAT_0057d3d0) baked in and
 * their own messages: gfx_panel_draw_border_chrome / _fill_interior_chrome.
 */

#include <stddef.h>

#include "gfx_panel.h"
#include "gfx_globals.h"
#include "gfx_glyph.h"
#include "gfx_dispatch.h"
#include "gfx_sprite.h"
#include "gfx_sprite_render.h"   /* gfx_sprite_render_log_error (FUN_004afab9) */

/* Panel asset descriptor accessors (16-byte stride, 8-byte header). */
#define DESC(asset, idx) ((asset) + 8 + (idx) * 0x10)

/* The four retail bodies' error messages (FUN_004afab9's format VA:
 * offset > 310000, offset < 1). */
typedef struct { uint32_t hi, lo; } panel_msgs_t;
static const panel_msgs_t k_border_msgs        = { 0x4d21e4, 0x4d21fc };   /* FUN_0040946d */
static const panel_msgs_t k_fill_msgs          = { 0x4d2214, 0x4d222c };   /* FUN_004097c5 */
static const panel_msgs_t k_border_chrome_msgs = { 0x4d2244, 0x4d225c };   /* FUN_00409934 */
static const panel_msgs_t k_fill_chrome_msgs   = { 0x4d2274, 0x4d228c };   /* FUN_00409c93 */

/* Stage tile `idx` at the cursor (cursor_x, cursor_y) the way retail does
 * — last-glyph index, cursor, every descriptor field — then gate its
 * src_offset.  Returns 0, or -1 after logging + latching error 1 (the
 * caller aborts without restoring the cursor). */
static int stage_tile(const uint8_t *asset, int idx, int cursor_x, int cursor_y, const panel_msgs_t *m)
{
    g_gfx_last_glyph_index = idx;
    g_gfx_cursor_x = cursor_x;
    g_gfx_cursor_y = cursor_y;
    const uint8_t *d = DESC(asset, idx);
    g_gfx_glyph_w            = (int16_t)(d[0] | (d[1] << 8));
    g_gfx_glyph_height_short = (int16_t)(d[2] | (d[3] << 8));
    g_gfx_glyph_offset       = (int32_t)((uint32_t)d[4] | ((uint32_t)d[5] << 8) |
                                         ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24));
    g_gfx_flag_a             = d[0xc];
    g_gfx_glyph_h            = d[0xd];
    g_gfx_flag_b             = d[0xe];

    if (g_gfx_glyph_offset > 310000) {
        gfx_sprite_render_log_error(m->hi, g_gfx_last_glyph_index + 1);
        g_gfx_last_error = 1;
        return -1;
    }
    if (g_gfx_glyph_offset < 1) {
        gfx_sprite_render_log_error(m->lo, g_gfx_last_glyph_index + 1);
        g_gfx_last_error = 1;
        return -1;
    }
    return 0;
}

/* FUN_0040946d / FUN_00409934. */
static void draw_border(const uint8_t *asset, int mode, int x, int y, int cols, int rows, const panel_msgs_t *m)
{
    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
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

            if (stage_tile(asset, idx, c * 0x10 + x, r * 0x10 + y, m) != 0) {
                return;                                  /* bad offset → abort */
            }

            gfx_dispatch_clip_x(0, 0x280);
            gfx_dispatch_clip_y(0, 0x1e0);

            /* offset += width * top_skip + left_skip (the clip-induced
             * source advance; both 0 for a fully on-screen tile). */
            g_gfx_glyph_offset += g_gfx_top_skip * g_gfx_glyph_w;
            g_gfx_glyph_offset += g_gfx_left_skip;

            if (g_gfx_x_clip_mode == 1) {
                gfx_sprite_blit_left_clipped(asset);
            } else if (g_gfx_x_clip_mode == 2) {
                gfx_sprite_blit_right_clipped(asset);
            } else if (g_gfx_y_clip_mode != 5) {
                gfx_sprite_blit(asset);
            }
        }
    }
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* FUN_004097c5 / FUN_00409c93. */
static void fill_interior(const uint8_t *asset, int x, int y, int cols, int rows, const panel_msgs_t *m)
{
    g_gfx_saved_cursor_x = g_gfx_cursor_x;
    g_gfx_saved_cursor_y = g_gfx_cursor_y;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c % 0xc + (r % 0xc) * 0xc + 0x34;
            if (stage_tile(asset, idx, c * 0x10 + x, r * 0x10 + y, m) != 0) {
                return;                                  /* bad offset → abort */
            }
            gfx_sprite_blit_16x16_raw(asset);
        }
    }
    g_gfx_cursor_x = g_gfx_saved_cursor_x;
    g_gfx_cursor_y = g_gfx_saved_cursor_y;
}

/* Port of FUN_0040946d. */
void gfx_panel_draw_border(const uint8_t *asset, int mode, int x, int y, int cols, int rows)
{
    draw_border(asset, mode, x, y, cols, rows, &k_border_msgs);
}

/* Port of FUN_004097c5. */
void gfx_panel_fill_interior(const uint8_t *asset, int x, int y, int cols, int rows)
{
    fill_interior(asset, x, y, cols, rows, &k_fill_msgs);
}

/* Port of FUN_00409934 — FUN_0040946d with the chrome sheet baked in. */
void gfx_panel_draw_border_chrome(const uint8_t *chrome, int mode, int x, int y, int cols, int rows)
{
    draw_border(chrome, mode, x, y, cols, rows, &k_border_chrome_msgs);
}

/* Port of FUN_00409c93 — FUN_004097c5 with the chrome sheet baked in. */
void gfx_panel_fill_interior_chrome(const uint8_t *chrome, int x, int y, int cols, int rows)
{
    fill_interior(chrome, x, y, cols, rows, &k_fill_chrome_msgs);
}

/* Port of FUN_00409346. */
void gfx_panel_draw(const uint8_t *asset, int x, int y, int cols, int rows)
{
    gfx_panel_draw_border(asset, 0, x, y, cols, rows);
    gfx_panel_fill_interior(asset, x + 0x10, y + 0x10, cols - 2, rows - 2);
}

/* Port of FUN_004093e0 @ 0x4093e0 (73 B).  The raised "bevel" frame the
 * chrome UI (button caps, the page-7 option tabs) draws from panels.pl8:
 * the chrome twins FUN_00409934 (mode 1 — every sprite index +0xcc, the
 * second / raised style block) and FUN_00409c93.  Retail hardcodes the
 * chrome sheet DAT_0057d3d0; the caller passes the bound blob. */
void gfx_panel_draw_bevel(const uint8_t *chrome, int x, int y, int cols, int rows)
{
    gfx_panel_draw_border_chrome(chrome, 1, x, y, cols, rows);
    gfx_panel_fill_interior_chrome(chrome, x + 0x10, y + 0x10, cols - 2, rows - 2);
}
