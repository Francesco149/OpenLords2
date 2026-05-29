/*
 * OpenLords2 — tiled stone-panel renderer.  See gfx_panel.c.
 *
 *   FUN_0040946d @ 0x40946d → gfx_panel_draw_border
 *   FUN_004097c5 @ 0x4097c5 → gfx_panel_fill_interior
 *   FUN_00409346 @ 0x409346 → gfx_panel_draw
 *
 * All take the panel sprite-sheet `asset` (retail DAT_004eabec etc) and
 * drive the shared gfx_glyph/gfx_dispatch sprite state; bind the canvas
 * via gfx_paint_bind_canvas first.
 */

#ifndef OPENLORDS2_GFX_PANEL_H
#define OPENLORDS2_GFX_PANEL_H

#include <stdint.h>

/* 9-patch border (corners + edges; interior skipped).  `mode > 0`
 * shifts every sprite index by +0xcc; `mode == 2` uses the alternate
 * top corners.  `cols`/`rows` are tile counts; tiles are 16 px. */
void gfx_panel_draw_border(const uint8_t *asset, int mode,
                           int x, int y, int cols, int rows);

/* Full cols×rows opaque interior fill from the 12×12 atlas at sprite
 * 0x34. */
void gfx_panel_fill_interior(const uint8_t *asset, int x, int y,
                             int cols, int rows);

/* Border (mode 0) over cols×rows at (x,y), plus the interior fill inset
 * one tile in and two tiles smaller. */
void gfx_panel_draw(const uint8_t *asset, int x, int y, int cols, int rows);

/* Raised "bevel" frame (FUN_004093e0): like gfx_panel_draw but the
 * border is mode 1 (+0xcc index shift → the chrome style block).  Used
 * with the panels.pl8 chrome sheet for button caps + option tabs. */
void gfx_panel_draw_bevel(const uint8_t *asset, int x, int y, int cols, int rows);

#endif /* OPENLORDS2_GFX_PANEL_H */
