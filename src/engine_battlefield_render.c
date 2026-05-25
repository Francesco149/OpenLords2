/*
 * OpenLords2 — combat-mode (battlefield) renderer (implementation).
 *
 * See engine_battlefield_render.h for the function map and chip plan;
 * see docs/findings/battlefield-render.md for the cluster survey,
 * sprite primitive notes, and storage layout.
 */

#include "engine_battlefield_render.h"

#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"

#include <stddef.h>
#include <stdint.h>

/* ──────────────────────────────────────────────────────────────────
 * Module state — retail globals owned by this cluster.
 * ────────────────────────────────────────────────────────────────── */

/* Camera (retail DAT_004e657c, DAT_004e6580).  Set by the frame
 * driver from its parameters.  Read by every pass that needs camera
 * coords; the cursor (gfx_glyph cursor_x/y) is set per-cell from
 * (viewport + 16 * col, viewport + 16 * row). */
static int g_camera_x;
static int g_camera_y;

/* Zoom level (retail DAT_004e656c).  Read by the frame driver to
 * pick the terrain + overlay passes. */
static int g_zoom;

/* Force-redraw counter (retail DAT_0053e9ac).  Driver decrements
 * post-frame when non-zero; terrain pass overrides per-cell dirty
 * bits when non-zero. */
static int g_force_redraw;

/* Descriptor table (retail DAT_004e6574 base, DAT_004e5d50 stride).
 * Non-const because the terrain pass mutates the flag byte (clears
 * dirty bit, sets boundary bit). */
static uint8_t *g_desc_base;
static int      g_desc_stride;

/* Visible grid dimensions (retail DAT_004e5d5c cols, DAT_004e5d4c rows)
 * and the descriptor row-wrap (retail DAT_004e5d70) — the offset
 * added at end of each visible row to reach the next visible row's
 * first cell. */
static int g_grid_cols;
static int g_grid_rows;
static int g_grid_row_wrap;

/* Screen viewport offset (retail DAT_004e6558 x, DAT_004e6554 y) —
 * pixel coords of the top-left of the visible grid. */
static int g_viewport_x;
static int g_viewport_y;

/* Camera row stride in cells (retail DAT_004e6578) — used to compute
 * the per-frame descriptor cursor. */
static int g_camera_row_stride;

/* Sprite blobs (retail DAT_004e5d78 primary, DAT_004e6560 alt) —
 * each cell's flags byte (bits 0x04..0x10) selects between them. */
static const uint8_t *g_blob_primary;
static const uint8_t *g_blob_alt;

/* Per-frame ephemeral state (mirrors retail running statics).
 *   DAT_004e6588  — descriptor cursor byte offset, advanced per cell.
 *   _DAT_004e5d64 — per-cell counter, incremented per cell (purpose
 *                   beyond debug instrumentation not yet known —
 *                   retail doesn't read it back inside this pass). */
static int g_desc_cursor_offset;
static int g_per_frame_cell_counter;

/* Per-blit dispatch counter — bumped each time the terrain pass
 * actually calls `gfx_sprite_blit_16x16_raw`. */
static int g_terrain_blit_count;

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

void engine_battlefield_render_bind_descriptor_table(uint8_t *base, int stride)
{
    g_desc_base   = base;
    g_desc_stride = stride;
}

void engine_battlefield_render_bind_grid(int cols, int rows, int row_wrap)
{
    g_grid_cols     = cols;
    g_grid_rows     = rows;
    g_grid_row_wrap = row_wrap;
}

void engine_battlefield_render_bind_viewport_offset(int x, int y)
{
    g_viewport_x = x;
    g_viewport_y = y;
}

void engine_battlefield_render_bind_camera_row_stride(int row_stride)
{
    g_camera_row_stride = row_stride;
}

void engine_battlefield_render_bind_blobs(const uint8_t *primary, const uint8_t *alt)
{
    g_blob_primary = primary;
    g_blob_alt     = alt;
}

int engine_battlefield_render_terrain_blit_count(void)
{
    return g_terrain_blit_count;
}

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

static void pass_terrain_zoom32(void)   { g_count_terrain_zoom32 += 1; }
static void pass_middle(void)           { g_count_middle += 1; }
static void pass_overlay_zoom16(void)   { g_count_overlay_zoom16 += 1; }
static void pass_overlay_zoom32(void)   { g_count_overlay_zoom32 += 1; }
static void pass_late(void)             { g_count_late += 1; }
static void pass_final(void)            { g_count_final += 1; }

/* ──────────────────────────────────────────────────────────────────
 * Terrain zoom-16 pass — port of FUN_004bc8fc @ 0x4bc8fc (725 B).
 *
 * Nested row/col loop over the visible grid.  For each cell:
 *
 *   1. Read flag byte at descriptor[+2].
 *   2. If force-redraw counter non-zero OR dirty bit set:
 *      a. Clear dirty bit on the descriptor.
 *      b. Read sprite index from descriptor[+3].
 *      c. Pick blob:  (flags & 0x1c) == 0 → primary, == 4 → alt,
 *         anything else → no blit dispatch (still mark + advance).
 *      d. Stage cursor at (viewport_x + col*0x10, viewport_y + row*0x10).
 *      e. Stage sprite metadata via gfx_dispatch_stage_sprite_descriptor.
 *      f. Stage the last-glyph-index global (matches retail's direct
 *         DAT_005c9288 = sprite_idx write).
 *      g. Dispatch 16×16 raw blit.
 *   3. Boundary marking: if cell is at any grid edge (first/last row
 *      or first/last col), OR bit 0x02 into the flags byte.  This
 *      forces re-paint of edge cells next frame.
 *   4. Advance descriptor cursor by `descriptor_stride`.
 *   5. End of row: advance descriptor cursor by `row_wrap`.
 *
 * The cursor (gfx_glyph cursor_x/y) is staged per-cell.  Cursor X
 * resets to `viewport_x` at the start of each row and advances by
 * 0x10 (16 pixels) per cell.  Cursor Y starts at `viewport_y` and
 * advances by 0x10 per row.
 * ────────────────────────────────────────────────────────────────── */

static void pass_terrain_zoom16(void)
{
    g_count_terrain_zoom16 += 1;

    if (!g_desc_base) {
        /* No descriptor table bound — nothing to iterate.  Retail
         * would dereference a NULL global here; in our port we just
         * short-circuit so tests can exercise the frame driver
         * without binding the full state. */
        return;
    }

    /* Per-frame state initialization.  Retail:
     *   DAT_004e6588 = (cam_x + cam_y * row_stride) * desc_stride;
     *   _DAT_004e5d64 = 0;
     *   DAT_00591528 = DAT_004e6554;  (cursor_y = viewport_y)
     */
    g_desc_cursor_offset = (g_camera_x + g_camera_y * g_camera_row_stride)
                         * g_desc_stride;
    g_per_frame_cell_counter = 0;
    int cursor_y = g_viewport_y;

    for (int row = 0; row < g_grid_rows; row++) {
        /* Retail: DAT_00591524 = DAT_004e6558; (cursor_x = viewport_x) */
        int cursor_x = g_viewport_x;

        for (int col = 0; col < g_grid_cols; col++) {
            uint8_t *flag_ptr = &g_desc_base[g_desc_cursor_offset + 2];
            uint8_t  flags    = *flag_ptr;

            if (g_force_redraw != 0 || (flags & 0x01) != 0) {
                /* Clear dirty bit; the cell will be redrawn now. */
                *flag_ptr = flags & 0xfe;

                uint8_t sprite_idx = g_desc_base[g_desc_cursor_offset + 3];
                uint8_t blob_sel   = flags & 0x1c;

                const uint8_t *blob = NULL;
                if (blob_sel == 0) {
                    blob = g_blob_primary;
                } else if (blob_sel == 4) {
                    blob = g_blob_alt;
                }
                /* No fall-through default: any other selector skips
                 * dispatch entirely, matching retail's strict if/else-if
                 * chain. */

                if (blob) {
                    /* Stage cursor — retail writes DAT_00591524 /
                     * DAT_00591528 inline; our equivalent is gfx_glyph's
                     * cursor_x / cursor_y setters. */
                    gfx_glyph_set_cursor_x(cursor_x);
                    gfx_glyph_set_cursor_y(cursor_y);

                    /* Stage the last-glyph-index global — retail's
                     * DAT_005c9288 = sprite_idx direct write. */
                    gfx_dispatch_set_last_glyph_index(sprite_idx);

                    /* Stage the 6-field sprite metadata from the blob
                     * header at slot `sprite_idx`.  Inlined in retail
                     * (six direct DAT_* writes); functionally identical
                     * to the dispatcher's stage helper which reads the
                     * same 16-byte slot layout at +0xc..+0xe. */
                    gfx_dispatch_stage_sprite_descriptor(blob, sprite_idx);

                    /* Dispatch the raw 16×16 blit.  Reads cursor +
                     * glyph_offset from gfx_glyph; reads the source
                     * bytes from `blob + glyph_offset`. */
                    gfx_sprite_blit_16x16_raw(blob);
                    g_terrain_blit_count += 1;
                }
            }

            /* Boundary marking — any cell at the grid edge gets bit
             * 0x02 set on its flags byte.  Retail's chain of four
             * mutually-exclusive if/else-if blocks (col==0, row==0,
             * col==cols-1, row==rows-1) all write the same value;
             * we collapse to one logical OR.  Note: the assignment
             * reads the post-clear flag byte, so the boundary bit
             * coexists with the cleared dirty bit. */
            if (col == 0 || row == 0
                || col == g_grid_cols - 1
                || row == g_grid_rows - 1) {
                g_desc_base[g_desc_cursor_offset + 2] |= 0x02;
            }

            /* Advance descriptor cursor + per-cell counter + cursor_x. */
            g_desc_cursor_offset    += g_desc_stride;
            g_per_frame_cell_counter += 1;
            cursor_x                += 0x10;
        }

        /* End-of-row: skip past off-screen cells to next visible row's
         * first cell, and advance cursor_y. */
        g_desc_cursor_offset += g_grid_row_wrap;
        cursor_y             += 0x10;
    }
}

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
    g_terrain_blit_count   = 0;
}
