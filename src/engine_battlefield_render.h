/*
 * OpenLords2 — combat-mode (battlefield) renderer.
 *
 * Sibling of `engine_map_render` and `engine_overlay_render` (the
 * strategic-mode primary + overlay passes).  This module owns the
 * cluster hung off the combat-mode frame driver at `FUN_004bc142 @
 * 0x4bc142` — invoked from the main-loop tick when
 * `DAT_0057c8fc == 2` (game-mode = combat).
 *
 * Chip 2 of the battlefield port (see docs/findings/battlefield-render.md
 * for the full chip plan):
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
 * The other five passes are NOT ported yet.  Each is wired here as a
 * static stub that bumps a per-pass call counter so the driver's
 * dispatch shape (zoom-16 vs zoom-32, always-on vs zoom-gated) can be
 * verified before the bodies land.  Replacing a stub with a real port
 * is a one-line change in `engine_battlefield_render.c`:
 *
 *   FUN_004bc8fc @ 0x4bc8fc (725 B)  zoom-16 terrain   [stub]
 *   FUN_004bcbdc @ 0x4bcbdc (1187 B) zoom-32 terrain   [stub]
 *   FUN_004bd355 @ 0x4bd355 (543 B)  middle pass       [stub]
 *   FUN_004bd07f @ 0x4bd07f (436 B)  zoom-32 overlay   [stub]
 *   FUN_004bd233 @ 0x4bd233 (290 B)  late pass         [stub]
 *   FUN_004bf1f9 @ 0x4bf1f9 (433 B)  final pass        [stub]
 *
 * State model: this module owns its own statics for the camera (set
 * by the driver from its parameters, retail DAT_004e657c / DAT_004e6580),
 * the zoom level (retail DAT_004e656c), and the force-redraw counter
 * (retail DAT_0053e9ac).  No external bindings beyond those — until
 * the terrain/overlay passes are ported, the cluster needs no tile
 * array or sprite blob.
 */

#ifndef OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H
#define OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H

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
 * driver decrements this at end-of-frame when non-zero; per-pass
 * "override per-cell dirty" semantics live inside the individual
 * passes (currently stubs) and are not modeled here yet. */
void engine_battlefield_render_set_force_redraw(int counter);
int  engine_battlefield_render_force_redraw(void);

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
int  engine_battlefield_render_count_terrain_zoom16(void);  /* FUN_004bc8fc */
int  engine_battlefield_render_count_terrain_zoom32(void);  /* FUN_004bcbdc */
int  engine_battlefield_render_count_middle(void);          /* FUN_004bd355 */
int  engine_battlefield_render_count_overlay_zoom16(void);  /* FUN_004bcbd1 (no-op) */
int  engine_battlefield_render_count_overlay_zoom32(void);  /* FUN_004bd07f */
int  engine_battlefield_render_count_late(void);            /* FUN_004bd233 */
int  engine_battlefield_render_count_final(void);           /* FUN_004bf1f9 */
void engine_battlefield_render_reset_counts(void);

#endif /* OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H */
