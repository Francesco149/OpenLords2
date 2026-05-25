/*
 * OpenLords2 — combat-mode (battlefield) renderer (implementation).
 *
 * See engine_battlefield_render.h for the function map and chip plan;
 * see docs/findings/battlefield-render.md for the cluster survey,
 * sprite primitive notes, and storage layout.
 */

#include "engine_battlefield_render.h"

/* ──────────────────────────────────────────────────────────────────
 * Module state — retail globals owned by this cluster.
 * ────────────────────────────────────────────────────────────────── */

/* Camera (retail DAT_004e657c, DAT_004e6580).  Set by the frame
 * driver from its parameters.  Read by every pass (currently stubs);
 * the camera ends up in shared retail globals because each pass reads
 * them directly rather than receiving them as arguments. */
static int g_camera_x;
static int g_camera_y;

/* Zoom level (retail DAT_004e656c).  Read by the frame driver to
 * pick the terrain + overlay passes. */
static int g_zoom;

/* Force-redraw counter (retail DAT_0053e9ac).  Driver decrements
 * post-frame when non-zero.  Per-pass override-dirty semantics live
 * inside the individual passes (stubs). */
static int g_force_redraw;

/* Per-pass call counters.  See the header for what each one
 * represents.  All seven pass entry points (5 stubs + 1 real no-op
 * + 1 implicitly the frame driver) bump exactly one counter per
 * dispatch.  Test-only — not part of retail. */
static int g_count_terrain_zoom16;  /* FUN_004bc8fc */
static int g_count_terrain_zoom32;  /* FUN_004bcbdc */
static int g_count_middle;          /* FUN_004bd355 */
static int g_count_overlay_zoom16;  /* FUN_004bcbd1 (real no-op port) */
static int g_count_overlay_zoom32;  /* FUN_004bd07f */
static int g_count_late;            /* FUN_004bd233 */
static int g_count_final;           /* FUN_004bf1f9 */

/* ──────────────────────────────────────────────────────────────────
 * Bind / config setters
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_set_zoom(int zoom)                  { g_zoom = zoom; }
int  engine_battlefield_render_zoom(void)                          { return g_zoom; }

void engine_battlefield_render_set_force_redraw(int counter)       { g_force_redraw = counter; }
int  engine_battlefield_render_force_redraw(void)                  { return g_force_redraw; }

int  engine_battlefield_render_camera_x(void)                      { return g_camera_x; }
int  engine_battlefield_render_camera_y(void)                      { return g_camera_y; }

/* ──────────────────────────────────────────────────────────────────
 * Per-pass stubs.
 *
 * Each stub stands in for a retail pass function that has not been
 * ported yet.  All they do is bump a per-pass call counter so the
 * frame driver's dispatch shape can be exercised by unit tests.
 *
 * `pass_overlay_zoom16` is the one stub that is also a faithful port
 * of the retail body — FUN_004bcbd1's retail decompilation is
 * literally `return;`.  It still bumps a counter for dispatch
 * verification, which is a test-only embellishment over retail.
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom16(void)   { g_count_terrain_zoom16 += 1; }
static void pass_terrain_zoom32(void)   { g_count_terrain_zoom32 += 1; }
static void pass_middle(void)           { g_count_middle += 1; }
static void pass_overlay_zoom16(void)   { g_count_overlay_zoom16 += 1; }
static void pass_overlay_zoom32(void)   { g_count_overlay_zoom32 += 1; }
static void pass_late(void)             { g_count_late += 1; }
static void pass_final(void)            { g_count_final += 1; }

/* ──────────────────────────────────────────────────────────────────
 * Frame driver — port of FUN_004bc142 @ 0x4bc142 (143 B).
 *
 * Retail decompilation (verbatim shape):
 *
 *   void FUN_004bc142(undefined4 param_1, undefined4 param_2) {
 *       DAT_004e657c = param_1;
 *       DAT_004e6580 = param_2;
 *       if (DAT_004e656c == 0x10) { FUN_004bc8fc(); }
 *       else if (DAT_004e656c == 0x20) { FUN_004bcbdc(); }
 *       FUN_004bd355();
 *       if (DAT_004e656c == 0x10) { FUN_004bcbd1(); }
 *       else if (DAT_004e656c == 0x20) { FUN_004bd07f(); }
 *       FUN_004bd233();
 *       FUN_004bf1f9();
 *       if (DAT_0053e9ac != 0) { DAT_0053e9ac--; }
 *   }
 *
 * No conditional fall-through default: any zoom value that is
 * neither 0x10 nor 0x20 simply skips both zoom-dispatched passes.
 * Middle / late / final always fire, and force-redraw is always
 * decremented post-frame when non-zero.
 * ────────────────────────────────────────────────────────────────── */

void engine_battlefield_render_frame(int camera_x, int camera_y)
{
    g_camera_x = camera_x;
    g_camera_y = camera_y;

    /* Zoom-dependent terrain pass. */
    if (g_zoom == 0x10) {
        pass_terrain_zoom16();
    } else if (g_zoom == 0x20) {
        pass_terrain_zoom32();
    }

    /* Always: middle pass. */
    pass_middle();

    /* Zoom-dependent overlay pass.  Note: zoom-16's pass is retail
     * FUN_004bcbd1, a true no-op — but it IS dispatched.  Skipping
     * the dispatch entirely would be subtly different from retail
     * if the pass ever grows a body (or if the counter is observed). */
    if (g_zoom == 0x10) {
        pass_overlay_zoom16();
    } else if (g_zoom == 0x20) {
        pass_overlay_zoom32();
    }

    /* Always: late + final passes. */
    pass_late();
    pass_final();

    /* Decrement force-redraw counter post-frame when non-zero.
     * The check-before-decrement matters: a 0 counter must NOT
     * underflow into negative territory (retail signed int). */
    if (g_force_redraw != 0) {
        g_force_redraw -= 1;
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Per-pass call-counter accessors
 * ────────────────────────────────────────────────────────────────── */

int engine_battlefield_render_count_terrain_zoom16(void)  { return g_count_terrain_zoom16; }
int engine_battlefield_render_count_terrain_zoom32(void)  { return g_count_terrain_zoom32; }
int engine_battlefield_render_count_middle(void)          { return g_count_middle; }
int engine_battlefield_render_count_overlay_zoom16(void)  { return g_count_overlay_zoom16; }
int engine_battlefield_render_count_overlay_zoom32(void)  { return g_count_overlay_zoom32; }
int engine_battlefield_render_count_late(void)            { return g_count_late; }
int engine_battlefield_render_count_final(void)           { return g_count_final; }

void engine_battlefield_render_reset_counts(void)
{
    g_count_terrain_zoom16 = 0;
    g_count_terrain_zoom32 = 0;
    g_count_middle         = 0;
    g_count_overlay_zoom16 = 0;
    g_count_overlay_zoom32 = 0;
    g_count_late           = 0;
    g_count_final          = 0;
}
