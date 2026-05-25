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
 *   FUN_004bd355 @ 0x4bd355 (543 B)  → internal pass_middle (chip 5a.i)
 *     Middle pass — always-on, zoom-independent.  Three behaviors over
 *     the terrain/overlay passes:
 *
 *       1. Three setup helpers called once each at the head of the pass
 *          build, sort, and render a Y-ordered list of visible entities.
 *
 *          - chip 5a.ii.a (ported now) — FUN_004bd938 (visible-list
 *            build), FUN_004bda92 (bubble sort, 500-iter safety cap),
 *            FUN_004bdb95 (per-entity render loop).  The per-entity
 *            draw (FUN_004bdc31) stays as a per-entity stub for chip
 *            5a.ii.a; chip 5a.ii.b ports its body.
 *       2. Iterates an OVER-SCAN extension of the visible grid: starts
 *          one cell west / one cell north of the viewport, ends up to
 *          two cells past the south-east corner, clamped to the total
 *          grid extent (`camera_row_stride` × `total_rows`).
 *       3. Per-cell dispatch:
 *            - descriptor[+2] & 0x80 SET, descriptor[+3] == 0 → call
 *              FUN_004bd574 (player-marker draw) AND re-mark the cell
 *              dirty (set descriptor[+2] |= 1).  Re-dirtying is unique
 *              to this branch — the unit-key path below does NOT
 *              re-dirty.
 *            - descriptor[+2] & 0x80 SET, descriptor[+3] != 0 → call
 *              FUN_004bd759(descriptor[+3]) (unit-key sprite draw).
 *            - descriptor[+6] != 0 → call FUN_004beed4(descriptor[+6])
 *              (effect-chain walk).  Independent of the bit-0x80 gate;
 *              evaluated for every cell.
 *
 *     The three draw leaves (FUN_004bd574 / FUN_004bd759 / FUN_004beed4)
 *     stay as counter-stubs for chip 5a.i; chip 5a.iii ports them.
 *
 * The other two passes are NOT ported yet.  Each is wired here as a
 * static stub that bumps a per-pass call counter so the driver's
 * dispatch shape (zoom-16 vs zoom-32, always-on vs zoom-gated) can be
 * verified before the bodies land.  Replacing a stub with a real port
 * is a one-line change in `engine_battlefield_render.c`:
 *
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

#include "engine_dirty_rect.h"

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

/* Bind the total row count of the underlying grid (retail
 * `DAT_004e6570`).  Used by the middle pass to clamp the over-scan
 * extension so it doesn't walk past the last valid row.  The
 * over-scan analog for columns clamps against `camera_row_stride`,
 * which is already bound separately.
 *
 * Not consumed by terrain / overlay passes, which iterate exactly the
 * visible grid (`cols * rows`).  The middle pass extends iteration by
 * 1 cell west/north and up to 2 cells east/south, clamped to the total
 * extent. */
void engine_battlefield_render_bind_total_rows(int total_rows);

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

/* Bind the per-entity table consumed by the middle pass's setup
 * helpers (chip 5a.ii.a).  Retail layout:
 *
 *   base    address of entity[0]'s record (entity ids 1..max_id are
 *           the iteration range; id 0 is reserved/sentinel and never
 *           visited).  Each record is `stride` bytes.
 *   stride  per-entry byte count (retail
 *           `ENGINE_BATTLEFIELD_ENTITY_STRIDE_RETAIL` = 0x1b0).
 *   max_id  last valid entity id, inclusive (retail
 *           `ENGINE_BATTLEFIELD_ENTITY_MAX_ID_RETAIL` = 0x50 = 80).
 *
 * Field offsets within each record (consumed by chip 5a.ii.a):
 *
 *   +0x20  short  x coord (DAT_005544a0)        — hex column
 *   +0x22  short  y coord (DAT_005544a2)        — hex row
 *   +0x2c  byte   alive flag (DAT_005544ac)     — zero ⇒ skipped
 *
 * Further fields (consumed by chip 5a.ii.b / FUN_004bdc31 port):
 *
 *   +0x00  void*  primary sprite blob ptr (DAT_00554480, 32-bit in
 *                 retail; the port reads it as a host-width pointer so
 *                 64-bit host tests can store real pointers).
 *   +0x04  int    secondary sprite blob ptr (DAT_00554484; reserved
 *                 for chip 5a.iv — FUN_004be4df type-6 secondary draw).
 *   +0x10  byte   primary sprite frame (DAT_00554490)
 *   +0x11  byte   secondary sprite frame (DAT_00554491)
 *   +0x12  byte   entity type (DAT_00554492)
 *   +0x18  byte   facing direction (DAT_00554498)
 *   +0x31  byte   subtype/variant (DAT_005544b1)
 *   +0x32  byte   animation frame within facing (DAT_005544b2)
 *
 * A NULL base pointer disables the build helper — visible-entity
 * count comes out as 0 and the render loop is a no-op.  Tests that
 * only exercise the per-cell dispatch (chip 5a.i) can skip this
 * binding. */
void engine_battlefield_render_bind_entity_table(uint8_t *base,
                                                 int stride, int max_id);

/* Retail constants for the entity table.  Exposed so tests can size
 * fixtures matching the retail layout. */
#define ENGINE_BATTLEFIELD_ENTITY_STRIDE_RETAIL  0x1b0
#define ENGINE_BATTLEFIELD_ENTITY_MAX_ID_RETAIL  0x50

#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB    0x00
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_BLOB  0x04
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_FRAME   0x10
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME 0x11
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE            0x12
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING          0x18
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_X               0x20
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y               0x22
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE           0x2c
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE         0x31
#define ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME      0x32

/* Bind the per-entity animation-offset lookup tables (chip 5a.ii.b).
 *
 *   z16  retail `DAT_004e3bf0`  — used at zoom 0x10.
 *   z32  retail `DAT_004e4030`  — used at zoom 0x20.
 *
 * Both tables share the same layout:
 *
 *   entry(facing, frame) = base + facing * 0x88 + frame * 8
 *
 * Each entry is a little-endian (int x_off, int y_off) pair (8 bytes).
 * Facing and frame are read as SIGNED 8-bit values from the entity
 * record (`+0x18` facing, `+0x32` anim_frame) — negative values index
 * BACKWARDS into the table (filed as the corresponding quirk on the
 * 5a.ii.b dispatch).
 *
 * Per-facing capacity is 17 frames (0x88 / 8); per-table size is
 * facings × 0x88.  Retail's tables hold 8 facings each, so 0x440 bytes
 * per table (the two tables sit adjacent in retail).
 *
 * Either pointer may be NULL — entities are then drawn at their
 * un-adjusted base anchor (no per-frame animation offset).  Tests
 * that don't care about animation can pass NULL for both. */
void engine_battlefield_render_bind_anim_offset_tables(const uint8_t *z16,
                                                       const uint8_t *z32);

/* Per-facing / per-frame strides for the anim-offset tables. */
#define ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE 0x88
#define ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE  8

/* Bind the dirty-rect state used by the per-entity draw (chip 5a.ii.b)
 * for the per-entity 5×5 / 9×9 box mark (retail `FUN_004527a6` call).
 * NULL ⇒ skip dirty-rect mark (rendering still happens). */
void engine_battlefield_render_set_dirty_state(engine_dirty_rect_state_t *state);

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

/* Per-leaf call counters for the middle-pass.  Three of the six leaves
 * have real ports as of chip 5a.ii.a (build / sort / render);
 * the per-entity draw and the three per-cell draw leaves are still
 * stubs.  Each counter bumps once per dispatch — they let tests verify
 * the driver's per-cell dispatch shape without depending on real leaf
 * bodies.
 *
 *   build / sort / render — three setup helpers (FUN_004bd938 /
 *                           FUN_004bda92 / FUN_004bdb95) called once
 *                           each at the head of the middle pass.
 *                           Chip 5a.ii.a ports their bodies; counters
 *                           still bump once per pass for cross-checking.
 *   player_marker         — FUN_004bd574, dispatched when descriptor[+2]
 *                           has bit 0x80 set AND descriptor[+3] == 0.
 *                           Chip 5a.iii ports the body.
 *   unit_key              — FUN_004bd759(descriptor[+3]), dispatched
 *                           when bit 0x80 set AND descriptor[+3] != 0.
 *                           Chip 5a.iii ports the body.
 *   effect_chain          — FUN_004beed4(descriptor[+6]), dispatched
 *                           per cell whenever descriptor[+6] != 0
 *                           (independent of the bit-0x80 gate).
 *                           Chip 5a.iii ports the body. */
int  engine_battlefield_render_middle_build_count(void);
int  engine_battlefield_render_middle_sort_count(void);
int  engine_battlefield_render_middle_render_count(void);
int  engine_battlefield_render_middle_player_marker_count(void);
int  engine_battlefield_render_middle_unit_key_count(void);
int  engine_battlefield_render_middle_effect_chain_count(void);

/* Visible-entity list accessors (chip 5a.ii.a).  Populated by the
 * build helper (`FUN_004bd938`) at the head of every middle-pass call;
 * Y-sorted in place by the sort helper (`FUN_004bda92`); walked by the
 * render-loop helper (`FUN_004bdb95`).  After a frame render (or a
 * test-only middle-pass call) the list reflects the most recent
 * dispatch.
 *
 *   count       — number of valid entries, EXCLUSIVE of the in-band
 *                 sentinel.  Retail `DAT_004e5d38`.
 *   id(idx)     — entity id at sorted position idx ∈ [0, count).
 *                 Retail `DAT_004e5d80[idx*2]` (short).
 *   y(idx)      — entity y coord at sorted position idx ∈ [0, count).
 *                 Retail `DAT_004e5d82[idx*2]` (short).
 *
 * Out-of-range indices return 0 (defensive — retail would just
 * silently read past the buffer).  The sentinel at index `count`
 * (retail value y=9999, id=0) is NOT exposed; tests can verify it
 * indirectly by tearing down the binding and watching count reset to
 * zero. */
int engine_battlefield_render_visible_entity_count(void);
int engine_battlefield_render_visible_entity_id(int idx);
int engine_battlefield_render_visible_entity_y(int idx);

/* The number of outer iterations the bubble sort performed during the
 * most recent middle-pass dispatch (retail `_DAT_004e655c`, reset to 0
 * at the head of `FUN_004bda92`).  0 means the list was already sorted
 * (or contained 0/1 entries); a positive value indicates at least one
 * pass ran.  Capped at 500 by the safety guard. */
int engine_battlefield_render_middle_sort_passes(void);

/* Per-entity draw cross-check instrumentation (chip 5a.ii.a +
 * 5a.ii.b).  The per-entity render loop (`FUN_004bdb95`) computes
 * (px_x, px_y) from each visible entity's hex coords and then
 * dispatches the per-entity draw (`FUN_004bdc31`).  After chip 5a.ii.b,
 * the per-entity draw is a real port, but the count + last-* capture
 * stay as cross-check state:
 *
 *   count       — bumped once per call (once per visible entity per
 *                 frame, regardless of which gates fire inside).
 *   last_id     — entity id passed to the draw.
 *   last_px_x,  — staged cursor at entry (the pre-anim-offset anchor
 *   last_px_y     computed by the render loop).
 *
 * NOTE: as of chip 5a.ii.b, the per-entity draw applies an animation
 * offset to the cursor and may apply a type-9/type-10 Y bonus before
 * the actual blit.  The last_px_x / last_px_y values here capture the
 * ENTRY cursor (the render loop's output), NOT the post-anim-offset
 * value the blit uses.  Tests that want the post-adjust cursor must
 * compute it themselves from the entity's facing/frame indices and
 * the bound anim table. */
int engine_battlefield_render_middle_per_entity_count(void);
int engine_battlefield_render_middle_per_entity_last_id(void);
int engine_battlefield_render_middle_per_entity_last_px_x(void);
int engine_battlefield_render_middle_per_entity_last_px_y(void);

/* Per-entity TYPE-SPECIAL stubs (chip 5a.ii.b).  The per-entity draw
 * dispatches three type-special leaves at different points in its
 * body:
 *
 *   type6  — FUN_004be4df, PRE-call before the main draw.  Fires
 *            whenever entity[+0x12] == 6.  Likely a shadow/ground
 *            effect that draws beneath the main sprite.
 *   type7  — FUN_004be7be, POST-call after the main draw.  Fires
 *            whenever entity[+0x12] == 7.  Gated by the same glyph-
 *            offset check that gates the main draw body.
 *   type9_sub_e — FUN_004beab9, POST-call after the main draw.  Fires
 *                 when entity[+0x12] == 9 AND entity[+0x31] == 0x0e.
 *                 Same glyph-offset gate.
 *
 * Each stub captures a counter + the last entity id passed.  When chip
 * 5a.iv lands the three real ports, the counters stay as cross-check
 * state. */
int engine_battlefield_render_middle_entity_type6_count(void);
int engine_battlefield_render_middle_entity_type6_last_id(void);
int engine_battlefield_render_middle_entity_type7_count(void);
int engine_battlefield_render_middle_entity_type7_last_id(void);
int engine_battlefield_render_middle_entity_type9_sub_count(void);
int engine_battlefield_render_middle_entity_type9_sub_last_id(void);

/* No-primary-blob counter (chip 5a.ii.b).  When an entity's primary
 * sprite blob pointer (+0x00) is NULL, retail logs via FUN_004afab9
 * and skips the main draw (the type-7 / type-9 post-calls are also
 * skipped, since they sit inside the no-blob else branch — but the
 * type-6 PRE-call DOES fire before the no-blob check).  The port
 * counts the no-blob path so tests can verify the gate. */
int engine_battlefield_render_middle_entity_no_blob_count(void);
int engine_battlefield_render_middle_entity_no_blob_last_id(void);

/* Per-entity blit dispatch counter (chip 5a.ii.b).  Bumped each time
 * the per-entity draw actually calls gfx_dispatch_dispatch_sprite_rle
 * (i.e. after the glyph-offset gate, the y_clip_mode != 5 gate).  Not
 * to be confused with `count`, which bumps once per entity dispatched
 * (regardless of whether the blit fires). */
int engine_battlefield_render_middle_entity_blit_count(void);

/* The unit_key and effect_chain stubs each capture the LAST byte they
 * were called with — descriptor[+3] for unit_key, descriptor[+6] for
 * effect_chain.  Tests use these to verify the driver passes the right
 * byte from the descriptor.  Initialized to -1 and reset to -1 by
 * `engine_battlefield_render_reset_counts`. */
int  engine_battlefield_render_middle_unit_key_last_arg(void);
int  engine_battlefield_render_middle_effect_chain_last_arg(void);

/* Per-cell counter incremented by the middle pass for each cell it
 * visits (after gating).  This is the same `_DAT_004e5d64` global that
 * the zoom-16 terrain pass also drives.  Exposed for chip 5a.i tests
 * to verify cell-loop iteration counts (rows × cols of the over-scan
 * extension). */
int  engine_battlefield_render_per_frame_cell_counter(void);

/* TEST-ONLY: run just the middle pass with the given camera.
 *
 * Bypasses the frame driver's terrain / overlay / late / final
 * dispatch, so tests can inspect cursor and per-cell counter state
 * without contamination from sibling passes that also mutate them.
 * Sets the camera globals first (same effect as the frame driver's
 * per-frame camera write) so the over-scan extension is computed from
 * the test's chosen camera. */
void engine_battlefield_render_run_pass_middle_for_test(int camera_x, int camera_y);

#endif /* OPENLORDS2_ENGINE_BATTLEFIELD_RENDER_H */
