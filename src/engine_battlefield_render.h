/*
 * OpenLords2 — combat-mode (battlefield) renderer.
 *
 * Sibling of `engine_map_render` and `engine_overlay_render` (the
 * strategic-mode primary + overlay passes).  This module owns the
 * cluster hung off the combat-mode frame driver at `FUN_004bc142 @
 * 0x4bc142` — invoked from the main-loop tick when
 * `DAT_0057c8fc == 2` (game-mode = combat).
 *
 * Chips 2 + 3 + 4 of the battlefield port (see
 * docs/findings/battlefield-render.md for the full chip plan):
 *
 *   FUN_004bc142 @ 0x4bc142 (143 B)  → engine_battlefield_render_frame
 *     Outer driver — sets camera globals, fans out to the zoom-
 *     dispatched terrain pass, the always-on middle pass, the zoom-
 *     dispatched overlay pass, the always-on late + final passes, and
 *     then decrements the force-redraw counter when non-zero.
 *
 *   FUN_004bcbd1 @ 0x4bcbd1 (11 B)   → internal no-op stub
 *     The zoom-16 second pass.  Retail body is literally `return;`.
 *     Ported as a true no-op (still bumps the per-pass counter so the
 *     test harness can confirm dispatch fidelity).
 *
 *   FUN_004bc8fc @ 0x4bc8fc (725 B)  → internal pass_terrain_zoom16
 *     Zoom-16 first pass (terrain blit).  Nested row/col loop over the
 *     visible grid.  For each dirty (or force-redraw) cell, stages
 *     sprite metadata from one of two sprite blobs and dispatches the
 *     16×16 raw blit primitive `gfx_sprite_blit_16x16_raw`.  Boundary
 *     cells are tagged with bit 0x02 of their flags byte (so they
 *     get re-painted next frame, handling scroll smear).
 *
 *   FUN_004bcbdc @ 0x4bcbdc (1187 B) → internal pass_terrain_zoom32
 *     Zoom-32 first pass — sibling of the zoom-16 terrain pass.  Uses
 *     the 32×32 raw blit primitive `gfx_sprite_blit_32x32_raw` for
 *     the first sprite per cell.  Adds two new per-cell behaviors
 *     absent from the zoom-16 pass:
 *
 *       1. A descriptor[+1] & 4 EARLY-SKIP gate: dirty cells with the
 *          bit set are silently dropped — NO drawing AND NO boundary
 *          marking.  Clean cells still get boundary marking.
 *       2. A SECOND sprite per cell, gated on descriptor[+0] != 0 and
 *          descriptor[+4] ∈ {1, 2, 3}.  The second sprite always uses
 *          the alt blob, picks slot 0x8b..0x9a by descriptor[+0]
 *          (clamped at 0x9a for values ≥ 0x10), and goes through the
 *          full clipped-blit dispatcher (clip_x → clip_y →
 *          apply_skip_and_dispatch_sprite).
 *
 *     The first sprite has a `[1, 0x4baf0]` src-offset gate — when the
 *     staged glyph_offset falls outside that band, the cell aborts
 *     immediately (skips boundary marking + skips the second sprite).
 *     Same gate is repeated on the alt-blob first-sprite path; both
 *     are preserved verbatim.
 *
 *   FUN_004bd07f @ 0x4bd07f (436 B) → internal pass_overlay_zoom32
 *     Zoom-32 second pass (overlay).  Outer shape mirrors the zoom-32
 *     terrain pass at 32 px cell pitch, but with three key differences:
 *
 *       1. Layer gate: drawing REQUIRES descriptor[+1] & 4.  Without
 *          that bit set, the cell is silently skipped — the
 *          complementary side of the terrain zoom-32 EARLY-SKIP
 *          (terrain only draws when the bit is CLEAR).  Net effect:
 *          per cell, exactly one of terrain-z32 or overlay-z32 may
 *          draw, picked by descriptor[+1] & 4.  Cells where neither
 *          draws (skip-bit set but cell is clean) still leave the
 *          dirty bit alone — both passes only clear it when they
 *          actually draw.
 *       2. Always uses the PRIMARY blob (DAT_004e5d78); the alt blob
 *          is not consulted (no (flags & 0x1c) selector here).
 *       3. NO boundary marking and NO second sprite — straight-line
 *          per-cell loop with no descriptor mutation beyond the dirty
 *          clear.  Same [1, 0x4baf0] glyph_offset gate as terrain-z32;
 *          out-of-band offsets just skip the blit (the only side-effect
 *          had already happened: dirty bit was cleared above the gate).
 *
 * The other three passes are NOT ported yet.  Each is wired here as a
 * static stub that bumps a per-pass call counter so the driver's
 * dispatch shape (zoom-16 vs zoom-32, always-on vs zoom-gated) can be
 * verified before the bodies land.  Replacing a stub with a real port
 * is a one-line change in `engine_battlefield_render.c`:
 *
 *   FUN_004bd355 @ 0x4bd355 (543 B)  middle pass       [stub]
 *   FUN_004bd233 @ 0x4bd233 (290 B)  late pass         [stub]
 *   FUN_004bf1f9 @ 0x4bf1f9 (433 B)  final pass        [stub]
 *
 * State model: this module owns its own statics for the camera (set
 * by the driver from its parameters, retail DAT_004e657c / DAT_004e6580),
 * the zoom level (retail DAT_004e656c), and the force-redraw counter
 * (retail DAT_0053e9ac), plus the terrain-pass bindings (descriptor
 * table, grid dimensions, viewport offset, camera row stride, sprite
 * blobs).  The cursor (gfx_glyph cursor_x / cursor_y) and sprite
 * metadata sinks (gfx_glyph / gfx_dispatch) are staged through the
 * shared dispatcher just before each blit — identical to retail's
 * direct writes to DAT_005c9258 / DAT_005aeb70 / etc.
 */

#ifndef OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H
#define OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H

#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────
 * Bindings — install retail globals into this module's statics
 * ───────────────────────────────────────────────────────────────── */

/* Bind the zoom level (retail `DAT_004e656c`).
 *
 *   0x10 → 16-px hex grid (zoom-1 / "small")
 *   0x20 → 32-px hex grid (zoom-2 / "large")
 *
 * Any other value causes the zoom-dispatched terrain + overlay passes
 * to be skipped (middle / late / final still fire) — matches retail's
 * if/else-if chain with no fall-through default. */
void engine_battlefield_render_set_zoom(int zoom);
int  engine_battlefield_render_zoom(void);

/* Bind the force-redraw counter (retail `DAT_0053e9ac`).  The frame
 * driver decrements this at end-of-frame when non-zero; when non-zero
 * the terrain pass overrides per-cell dirty bits, redrawing every
 * cell. */
void engine_battlefield_render_set_force_redraw(int counter);
int  engine_battlefield_render_force_redraw(void);

/* Bind the per-cell descriptor table for the battlefield grid.
 *
 *   base    retail `DAT_004e6574`  — pointer to the first entry.
 *   stride  retail `DAT_004e5d50`  — bytes per entry.
 *
 * Each entry holds (at least) two bytes consumed by the terrain pass:
 *
 *   +2  flag byte
 *         bit 0x01  — dirty (cell needs redraw); cleared after draw
 *         bit 0x02  — boundary (set after draw on grid-edge cells)
 *         bits 0x04..0x10  — sprite-blob selector
 *                              0 → primary blob (DAT_004e5d78)
 *                              4 → alt blob     (DAT_004e6560)
 *   +3  sprite index byte (into descriptor blob, slot * 16 + 8)
 *
 * Non-const because the terrain pass mutates the flag byte (clears
 * dirty, sets boundary). */
void engine_battlefield_render_bind_descriptor_table(uint8_t *base, int stride);

/* Bind the visible grid dimensions and row-wrap adjustment.
 *
 *   cols       retail `DAT_004e5d5c`  — visible columns per row.
 *   rows       retail `DAT_004e5d4c`  — visible rows.
 *   row_wrap   retail `DAT_004e5d70`  — descriptor offset added at
 *                                       end of each row, to skip the
 *                                       off-screen cells and reach
 *                                       the next visible row's first
 *                                       cell. */
void engine_battlefield_render_bind_grid(int cols, int rows, int row_wrap);

/* Bind the screen viewport offset — pixel coords of the top-left
 * corner of the visible grid.
 *
 *   x  retail `DAT_004e6558`  — initial cursor_x per row.
 *   y  retail `DAT_004e6554`  — initial cursor_y for the first row. */
void engine_battlefield_render_bind_viewport_offset(int x, int y);

/* Bind the camera row-stride — how many cells per row in the underlying
 * descriptor table (NOT necessarily the visible cols).  Used to
 * compute the per-frame descriptor cursor:
 *
 *   cursor_byte_off = (cam_x + cam_y * row_stride) * descriptor_stride
 *
 *   row_stride  retail `DAT_004e6578`. */
void engine_battlefield_render_bind_camera_row_stride(int row_stride);

/* Bind the two sprite blobs the terrain pass dispatches to.
 *
 *   primary  retail `DAT_004e5d78`  — used when (flags & 0x1c) == 0.
 *   alt      retail `DAT_004e6560`  — used when (flags & 0x1c) == 4.
 *                                     ALSO used unconditionally by the
 *                                     zoom-32 pass for its second-
 *                                     sprite-per-cell path.
 *
 * Either pointer may be NULL if the corresponding selector is never
 * exercised; cells that hit a NULL blob have their dispatch suppressed
 * entirely (the dirty bit is still cleared and boundary marking still
 * runs — matches retail's strict if/else-if chain with no default). */
void engine_battlefield_render_bind_blobs(const uint8_t *primary,
                                          const uint8_t *alt);

/* Bind the cell sub-band width (retail `DAT_004e5d3c`).  Hex tiles
 * aren't square — the cell rect is divided into three vertical bands,
 * each `band_width` pixels wide, used by the variant screen→hex
 * converters (`pixel_to_col_left_biased` / `pixel_to_col_right_biased`)
 * to pick the neighboring hex column when the pixel falls in the
 * first / last band of a cell.  For a 32-px cell with a typical hex
 * shape, `band_width` is ~8 (one quarter of the cell), so:
 *
 *   residue ∈ [0, band_width)            → first band  (left neighbor)
 *   residue ∈ [band_width, band_width*3) → middle band (this cell)
 *   residue ∈ [band_width*3, zoom)       → third band  (right neighbor)
 *
 * Not consumed by the basic `pixel_to_col` / `pixel_to_row` pair. */
void engine_battlefield_render_bind_cell_band_width(int band_width);

/* Bind the clip rect used by the zoom-32 pass's second-sprite-per-cell
 * path.  Retail passes these inline to FUN_0040464c / FUN_0040477b:
 *
 *   FUN_0040464c(DAT_004e6564, DAT_004e5d54)   — clip_x bounds
 *   FUN_0040477b(DAT_004e5d48, DAT_004e5d6c)   — clip_y bounds
 *
 *   left    retail `DAT_004e6564`
 *   right   retail `DAT_004e5d54`
 *   top     retail `DAT_004e5d48`
 *   bottom  retail `DAT_004e5d6c`
 *
 * Not consumed by the zoom-16 pass (which never goes through the
 * clipped blitter family). */
void engine_battlefield_render_bind_clip_rect(int left, int top,
                                              int right, int bottom);

/* ─────────────────────────────────────────────────────────────────
 * Frame render
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_004bc142.  Render one battlefield frame at camera
 * (`camera_x`, `camera_y`).
 *
 *   1. Set camera globals (retail DAT_004e657c / DAT_004e6580).
 *   2. Dispatch the zoom-dependent terrain pass:
 *        zoom == 0x10 → FUN_004bc8fc (stub)
 *        zoom == 0x20 → FUN_004bcbdc (stub)
 *   3. Always: middle pass (FUN_004bd355, stub).
 *   4. Dispatch the zoom-dependent overlay pass:
 *        zoom == 0x10 → FUN_004bcbd1 (real no-op, bumps counter)
 *        zoom == 0x20 → FUN_004bd07f (stub)
 *   5. Always: late pass (FUN_004bd233, stub).
 *   6. Always: final pass (FUN_004bf1f9, stub).
 *   7. Decrement force-redraw counter if non-zero. */
void engine_battlefield_render_frame(int camera_x, int camera_y);

/* Camera accessors (retail DAT_004e657c, DAT_004e6580).  Set by
 * `engine_battlefield_render_frame` from its parameters. */
int engine_battlefield_render_camera_x(void);
int engine_battlefield_render_camera_y(void);

/* ─────────────────────────────────────────────────────────────────
 * Coordinate helpers — small pure functions for input handling.
 * Each is a faithful 1:1 port of a tiny retail helper at the head of
 * the battlefield cluster.  They read the same module statics the
 * passes do (viewport, zoom, grid dims, camera_row_stride, cell band
 * width), so the bindings used to drive a render frame also drive
 * these queries.
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_004bc222 @ 0x4bc222 (131 B).  Test whether the pixel
 * coord (px, py) falls inside the visible grid rectangle.  Returns
 * 1 if inside, 0 otherwise.
 *
 *   inside = px ∈ [viewport_x, viewport_x + cols*zoom)
 *         ∧ py ∈ [viewport_y, viewport_y + rows*zoom)
 *
 * Uses `cols` (DAT_004e5d5c), `rows` (DAT_004e5d4c), `zoom`
 * (DAT_004e656c), and the viewport (DAT_004e6558, DAT_004e6554). */
int engine_battlefield_render_screen_in_grid(int px, int py);

/* Port of FUN_004bc2a5 @ 0x4bc2a5 (32 B).  Map a screen pixel X to a
 * visible-grid hex column.  Integer division — no sub-cell band
 * correction:  `col = (px - viewport_x) / zoom`.
 *
 * No clamping to grid extents — caller is expected to either pre-check
 * with `screen_in_grid` or accept out-of-range values. */
int engine_battlefield_render_pixel_to_col(int px);

/* Port of FUN_004bc2c5 @ 0x4bc2c5 (32 B).  Sibling of `pixel_to_col`
 * on the Y axis:  `row = (py - viewport_y) / zoom`. */
int engine_battlefield_render_pixel_to_row(int py);

/* Port of FUN_004bc2e5 @ 0x4bc2e5 (97 B).  Variant of `pixel_to_col`
 * that biases towards the RIGHT neighbor when the pixel falls in the
 * third sub-band of the cell:
 *
 *   col = (px - viewport_x) / zoom
 *   if ((px - viewport_x) % zoom >= band_width * 3) col += 1
 *   if (col < 1) col = 0
 *
 * The < 1 → 0 clamp ensures negative columns floor to 0 (no left-of-
 * grid columns ever emerge from this variant).  Used for input
 * coordinate quantization where right-half pixels should attribute
 * to the right-adjacent hex. */
int engine_battlefield_render_pixel_to_col_right_biased(int px);

/* Port of FUN_004bc346 @ 0x4bc346 (102 B).  Variant of `pixel_to_col`
 * that biases towards the LEFT neighbor when the pixel falls in the
 * first sub-band of the cell:
 *
 *   col = (px - viewport_x) / zoom
 *   if ((px - viewport_x) % zoom < band_width) col -= 1
 *   if (col >= camera_row_stride) col = camera_row_stride - 1
 *
 * Asymmetry vs. `pixel_to_col_right_biased`: this variant DOES clamp
 * the upper bound (to `camera_row_stride - 1`) but does NOT clamp the
 * lower bound.  Negative columns can propagate (left-of-grid hits)
 * even after the -1 bias. */
int engine_battlefield_render_pixel_to_col_left_biased(int px);

/* ─────────────────────────────────────────────────────────────────
 * Test instrumentation — per-pass call counters
 * ───────────────────────────────────────────────────────────────── */

/* Per-pass call counters.  Bumped each time the named pass is
 * dispatched by the frame driver.  Not retail — purely for unit
 * tests to verify the driver's dispatch shape.  Each name encodes
 * the retail symbol so future ports can track which counters
 * should disappear (a real port doesn't need a counter — the
 * port's own side effects are observable). */
int  engine_battlefield_render_count_terrain_zoom16(void);  /* FUN_004bc8fc — port; bumped per frame */
int  engine_battlefield_render_count_terrain_zoom32(void);  /* FUN_004bcbdc */
int  engine_battlefield_render_count_middle(void);          /* FUN_004bd355 */
int  engine_battlefield_render_count_overlay_zoom16(void);  /* FUN_004bcbd1 (no-op) */
int  engine_battlefield_render_count_overlay_zoom32(void);  /* FUN_004bd07f */
int  engine_battlefield_render_count_late(void);            /* FUN_004bd233 */
int  engine_battlefield_render_count_final(void);           /* FUN_004bf1f9 */
void engine_battlefield_render_reset_counts(void);

/* Per-blit dispatch counter — bumped each time the terrain pass
 * actually calls `gfx_sprite_blit_16x16_raw` (i.e. cell was dirty
 * OR force-redraw was on, AND the flag-selected blob was bound).
 * Distinct from the per-frame `count_terrain_zoom16`: that one bumps
 * once per FUN_004bc8fc invocation, this one bumps once per per-cell
 * blit.  Test-only — not part of retail. */
int  engine_battlefield_render_terrain_blit_count(void);

/* Per-blit counters for the zoom-32 first pass.  Both bump only when
 * the corresponding blit is actually dispatched (after the src-offset
 * gate and any blob-selector skip).
 *
 *   first  — bumped per cell that successfully dispatches the 32×32
 *            raw blit (`gfx_sprite_blit_32x32_raw`) for the primary
 *            sprite.
 *   second — bumped per cell that successfully dispatches the clipped
 *            blit for the second sprite (gated on descriptor[+0] != 0
 *            AND descriptor[+4] ∈ {1, 2, 3} AND y_clip_mode != 5). */
int  engine_battlefield_render_terrain_zoom32_first_blit_count(void);
int  engine_battlefield_render_terrain_zoom32_second_blit_count(void);

/* Per-blit counter for the zoom-32 overlay pass (FUN_004bd07f).  Bumps
 * once per cell that actually dispatches a 32×32 raw blit (after the
 * descriptor[+1] & 4 layer gate, dirty/forced gate, AND the [1, 0x4baf0]
 * glyph_offset gate).  Distinct from the per-pass `count_overlay_zoom32`
 * which bumps once per FUN_004bd07f invocation. */
int  engine_battlefield_render_overlay_zoom32_blit_count(void);

#endif /* OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H */
