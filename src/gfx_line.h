/*
 * OpenLords2 — line + axis-rect drawers (Bresenham + axis-aligned).
 *
 * Sits one layer above `gfx_paint_pixel_clipped` (and `gfx_paint_2x1_clipped`
 * is unused here — line drawer paints one pixel per step).  Provides
 * the engine's straight-line primitive plus the 3 retail rectangle
 * wrappers built on top of it.
 *
 * Retail address map:
 *
 *   FUN_004037ee @ 0x4037ee (347 B) — Bresenham setup (private helper)
 *                                     Computes abs(dx), abs(dy), reorders
 *                                     endpoints so iteration runs from the
 *                                     smaller-of-the-dominant-axis to the
 *                                     larger; picks per-axis step direction.
 *
 *   FUN_00403949 @ 0x403949 (126 B) — Bresenham accumulator update
 *                                     (private helper).  Steep- and shallow-
 *                                     mode err update.
 *
 *   FUN_00403a8f @ 0x403a8f (442 B) → gfx_line_draw
 *                                     Solid Bresenham line (1-pixel-wide).
 *                                     Vertical / horizontal / shallow /
 *                                     steep cases dispatched by endpoint
 *                                     equality + dominant-axis test.
 *
 *   FUN_00403c49 @ 0x403c49 (171 B) → gfx_line_draw_dotted
 *                                     Dotted variant — ONLY handles vertical
 *                                     and horizontal lines (paints every
 *                                     other pixel via step=+2).  Diagonals
 *                                     are silently dropped.
 *
 *   FUN_00403cf4 @ 0x403cf4 (247 B) → gfx_line_rect
 *                                     Solid axis-aligned rectangle outline,
 *                                     all 4 edges the same color.
 *
 *   FUN_00403deb @ 0x403deb (249 B) → gfx_line_rect_bevel_dark_br
 *                                     3D bevel rect: top+right edges at
 *                                     color 0x10 (bright), bottom+left
 *                                     edges at color 0x1f (dark).  Light
 *                                     source from the upper-right corner.
 *
 *   FUN_00403ee4 @ 0x403ee4 (249 B) → gfx_line_rect_bevel_palette_b
 *                                     Same shape, palette B: top+right at
 *                                     0x35, bottom+left at 0x28.  Distinct
 *                                     bevel color pair for a different UI
 *                                     element.
 *
 *   FUN_00403fdd @ 0x403fdd (249 B) → gfx_line_rect_bevel_dark_tl
 *                                     Inverse of bevel_dark_br: top+right
 *                                     at 0x1f (dark), bottom+left at 0x10
 *                                     (bright).  Light source from the
 *                                     bottom-left.
 *
 * Canvas binding:
 *
 *   The pixel painters (gfx_paint_pixel_clipped) read the canvas from
 *   the gfx_paint module's bound canvas.  This module reads the canvas
 *   bounds from gfx_paint_canvas_width() / gfx_paint_canvas_height()
 *   for the rect-clamp pre-checks — retail reads DAT_004eb274 /
 *   DAT_004eac4c directly.
 *
 * Quirks preserved:
 *
 *   - The Bresenham loop MUTATES the abs(dx)/abs(dy) BSS slots as it
 *     advances — they're decremented every iteration.  This is an
 *     unusual variant where the accumulator's "increment" depends on
 *     the CURRENT (mutated) dx/dy values.  See engine-quirks.md #38.
 *
 *   - The 3 bevel rects' bounds-clamp uses `if/else if` chains rather
 *     than the solid rect's `if`/`if` — so a rect with x=0 (in-canvas)
 *     never has its right edge re-clamped to fit the canvas.  Per-pixel
 *     clipping via the inner painter still drops the off-canvas pixels,
 *     so no visible difference; just wasted iterations.  See
 *     engine-quirks.md #39.
 *
 *   - The dotted-line variant ONLY handles vertical and horizontal
 *     lines — diagonals fall through both branches and draw nothing.
 *     Retail never invokes the dotted variant with non-axis endpoints,
 *     so this is "load-bearing only for the axis path".  See
 *     engine-quirks.md #40.
 *
 * Pure C; no Win32 deps.
 */

#ifndef OPENLORDS2_GFX_LINE_H
#define OPENLORDS2_GFX_LINE_H

#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Solid line
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00403a8f.  Draw a 1-pixel-wide line from (x1,y1) to
 * (x2,y2) in `color`.  Endpoint order is irrelevant — the setup
 * reorders so iteration always runs in the positive direction of the
 * dominant axis.  Per-pixel clipping against the gfx_paint canvas
 * happens inside `gfx_paint_pixel_clipped`. */
void gfx_line_draw(int x1, int y1, int x2, int y2, uint8_t color);

/* Port of FUN_00403c49.  Dotted axis-aligned line — paints every
 * other pixel on the dominant axis.  ONLY supports vertical
 * (x1==x2) and horizontal (y1==y2) lines; diagonal endpoints draw
 * NOTHING (silent no-op).  See quirk #40. */
void gfx_line_draw_dotted(int x1, int y1, int x2, int y2, uint8_t color);

/* ──────────────────────────────────────────────────────────────────
 * Axis-aligned rectangle outlines (4 lines stitched into a frame)
 * ────────────────────────────────────────────────────────────────── */

/* Port of FUN_00403cf4.  Solid rectangle outline.  All 4 edges drawn
 * in `color`.  Position+size given as (x, y, w, h) — clamped against
 * the bound canvas before drawing (negative x/y → 0; right/bottom
 * extents clamped to canvas dimensions). */
void gfx_line_rect(int x, int y, int w, int h, uint8_t color);

/* Port of FUN_0040437d.  Solid FILLED axis-aligned rectangle — `h`
 * stacked horizontal lines.  Guarded on w>0 && h>0, then clamped
 * against the bound canvas like gfx_line_rect.  Used for the
 * text-input caret (FUN_0040acce). */
void gfx_line_fill_rect(int x, int y, int w, int h, uint8_t color);

/* Port of FUN_00403deb.  3D bevel rectangle with light source from
 * the upper-right corner: top + right edges at color 0x10, bottom +
 * left edges at color 0x1f.  Position+size semantics match
 * gfx_line_rect, except the clamp is `if/else if` (quirk #39). */
void gfx_line_rect_bevel_dark_br(int x, int y, int w, int h);

/* Port of FUN_00403ee4.  Same bevel shape as bevel_dark_br but with
 * palette B: top+right at 0x35, bottom+left at 0x28.  Used for a
 * different UI element (palette differentiates frame contexts). */
void gfx_line_rect_bevel_palette_b(int x, int y, int w, int h);

/* Port of FUN_00403fdd.  Inverse of bevel_dark_br: top+right at
 * color 0x1f (dark), bottom+left at color 0x10 (bright).  Light
 * source from the bottom-left corner. */
void gfx_line_rect_bevel_dark_tl(int x, int y, int w, int h);

#endif /* OPENLORDS2_GFX_LINE_H */
