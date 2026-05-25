/*
 * OpenLords2 — combat-mode (battlefield) renderer.
 *
 * Sibling of `engine_map_render` and `engine_overlay_render` (the
 * strategic-mode primary + overlay passes).  This module owns the
 * cluster hung off the combat-mode frame driver at `FUN_004bc142 @
 * 0x4bc142` — invoked from the main-loop tick when
 * `DAT_0057c8fc == 2` (game-mode = combat).
 *
 * Chips 2 + 3 of the battlefield port (see
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
 * The other four passes are NOT ported yet.  Each is wired here as a
 * static stub that bumps a per-pass call counter so the driver's
 * dispatch shape (zoom-16 vs zoom-32, always-on vs zoom-gated) can be
 * verified before the bodies land.  Replacing a stub with a real port
 * is a one-line change in `engine_battlefield_render.c`:
 *
 *   FUN_004bcbdc @ 0x4bcbdc (1187 B) zoom-32 terrain   [stub]
 *   FUN_004bd355 @ 0x4bd355 (543 B)  middle pass       [stub]
 *   FUN_004bd07f @ 0x4bd07f (436 B)  zoom-32 overlay   [stub]
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
 *
 * Either pointer may be NULL if the corresponding selector is never
 * exercised; cells that hit a NULL blob have their dispatch suppressed
 * entirely (the dirty bit is still cleared and boundary marking still
 * runs — matches retail's strict if/else-if chain with no default). */
void engine_battlefield_render_bind_blobs(const uint8_t *primary,
                                          const uint8_t *alt);

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

#endif /* OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H */
