/*
 * OpenLords2 — framebuffer pixel painters (leaf primitives).
 *
 * Five leaves at the bottom of the rendering stack: every higher-
 * level draw call (lines, boxes, glyphs, tiles) eventually reaches
 * one of these to poke a single (x,y) or 2x1 horizontal pair into
 * the back buffer.  Pure C — no DDraw, no Win32; the engine's
 * runtime wires the back-buffer surface into these via
 * `gfx_paint_bind_canvas` once per Lock.
 *
 * Retail address map:
 *
 *   FUN_004b3f54 @ 0x4b3f54 ( 38 B) → gfx_paint_pixel
 *   FUN_004b3f7a @ 0x4b3f7a ( 41 B) → gfx_paint_2x1
 *   FUN_004b3fa3 @ 0x4b3fa3 ( 61 B) → gfx_paint_2x1_under
 *   FUN_004039c7 @ 0x4039c7 (101 B) → gfx_paint_pixel_clipped
 *   FUN_00403a2c @ 0x403a2c ( 99 B) → gfx_paint_2x1_clipped
 *
 * Canvas model (mirrors retail's globals):
 *
 *   `pixels` (DAT_004ea1a8)   — base pointer to the locked back
 *                                buffer's pixel data.  Retail's
 *                                FUN_0042ec91 reads lpSurface from
 *                                a DDraw Lock and stores it here;
 *                                the back buffer is then Unlocked,
 *                                with retail relying on system-
 *                                memory surfaces having stable
 *                                lpSurface across Unlock cycles.
 *   `width` (DAT_004eb274)    — pitch AND right-clip bound.  At
 *                                640×480×8 these are the same value
 *                                (no row padding).
 *   `height` (DAT_004eac4c)   — bottom-clip bound only; not used
 *                                by the unclipped painters.
 *
 * Quirks preserved verbatim:
 *
 *   - The clipped 2x1 painter (gfx_paint_2x1_clipped) bounds-checks
 *     only the LEFT pixel, not the right one.  When called with
 *     x == width-1, the right pixel writes one byte past the row
 *     into the start of the next row.  See engine-quirks.md #26.
 *
 *   - The unclipped painters do NO bounds check at all.  Callers
 *     are expected to clip before calling, OR (more commonly) to
 *     have a parent wrapper that batches the clip — the
 *     line-drawer FUN_00403c49 calls the clipped wrapper inside
 *     its loop body but the loop bound itself already comes from
 *     the engine's clipping range.
 *
 *   - gfx_paint_2x1_under is "paint-over-transparent": writes only
 *     where the existing pixel is 0.  Used by sprite blits that
 *     want to layer onto an already-painted background without
 *     overwriting it.
 */

#ifndef OPENLORDS2_GFX_PAINT_H
#define OPENLORDS2_GFX_PAINT_H

#include <stdint.h>

/* Bind the canvas the painters write into.  Width is used as both
 * the row pitch and the right-clip bound (retail conflates them via
 * DAT_004eb274); height is the bottom-clip bound (DAT_004eac4c).
 *
 * Pass pixels=NULL to unbind (subsequent painter calls become no-ops
 * to keep tests defensive against accidental writes through a stale
 * binding).  Width and height should be > 0 when pixels is non-NULL;
 * negative or zero dimensions clamp painters to no-op via the bounds
 * check (clipped variants) or undefined behavior (unclipped variants,
 * mirroring retail). */
void gfx_paint_bind_canvas(uint8_t *pixels, int width, int height);

/* Accessors so peer drawers (glyph blitter, line drawer, sprite
 * blitters) can compute their own dest pointer / right-clip bound
 * without each one re-binding its own private canvas reference.
 * Retail conflates these into the shared DAT_004ea1a8 / DAT_004eb274 /
 * DAT_004eac4c globals; the accessors give other modules read-only
 * access to the same state without duplicating bind plumbing. */
uint8_t *gfx_paint_canvas_pixels(void);
int      gfx_paint_canvas_width(void);
int      gfx_paint_canvas_height(void);

/* Port of `FUN_004b3f54 @ 0x4b3f54`.  Write `color` at (x,y) of the
 * bound canvas.  No bounds check — caller must clip first.
 *
 * Retail formula: `pixels[x + y*width] = color`. */
void gfx_paint_pixel(int x, int y, uint8_t color);

/* Port of `FUN_004b3f7a @ 0x4b3f7a`.  Write `color` at (x,y) AND
 * (x+1, y).  No bounds check.  Two-byte wide horizontal brush.
 *
 * Retail formula: `pixels[x + y*width]     = color;
 *                  pixels[x + y*width + 1] = color;` */
void gfx_paint_2x1(int x, int y, uint8_t color);

/* Port of `FUN_004b3fa3 @ 0x4b3fa3`.  Paint-under variant of the
 * 2x1: writes pixel iff the existing value is 0.  Each of the two
 * pixels is tested independently — if the left pixel is non-zero
 * but the right is 0, only the right pixel is written.
 *
 * Used by sprite-over-background paint paths where 0 is the
 * transparent index. */
void gfx_paint_2x1_under(int x, int y, uint8_t color);

/* Port of `FUN_004039c7 @ 0x4039c7`.  Bounds-checked wrapper around
 * gfx_paint_pixel: writes iff `0 <= x < width && 0 <= y < height`.
 * The retail bound comparison is `x < DAT_004eb274` /
 * `y < DAT_004eac4c` — these are the canvas dimensions passed to
 * gfx_paint_bind_canvas. */
void gfx_paint_pixel_clipped(int x, int y, uint8_t color);

/* Port of `FUN_00403a2c @ 0x403a2c`.  Bounds-checked wrapper around
 * gfx_paint_2x1.  The bound check is on (x,y) ONLY — the right
 * pixel at (x+1, y) is NOT separately tested.  When called with
 * x == width-1, the right pixel writes one column past the row edge
 * (which in pitch=width canvases means the start of the next row).
 * See engine-quirks.md #26. */
void gfx_paint_2x1_clipped(int x, int y, uint8_t color);

#endif /* OPENLORDS2_GFX_PAINT_H */
