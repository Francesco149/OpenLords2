/*
 * OpenLords2 — strategic-mode overworld map renderer.
 *
 * Port of the four-function map-render cluster:
 *
 *   FUN_00405487 @ 0x405487 (379 B)  → engine_map_render_frame
 *     Outer frame driver — drives one top phantom row, then
 *     interleaves phase0/phase1 row iterators down the camera
 *     viewport, then renders one bottom phantom row.
 *
 *   FUN_00405862 @ 0x405862 (333 B)  → engine_map_render_row_phase0
 *     Phase-0 row iterator.  Draws one optional left-edge cell at
 *     phase 0, then `column_count` cells with phase 0 each at the
 *     usual X stride, then one optional right-edge cell.  Advances
 *     the camera by one Y step.
 *
 *   FUN_004059af @ 0x4059af (314 B)  → engine_map_render_row_phase1
 *     Phase-1 row iterator.  First cell is rendered with phase 1
 *     (the left-edge half-step variant), then the remaining cells
 *     with phase 0, then one trailing cell with phase 0.  Advances
 *     the camera by one Y step.
 *
 *   FUN_00408438 @ 0x408438 (2072 B) → engine_map_render_tile
 *     The heart of the cluster — walks the per-tile linked list of
 *     territory records, looks up the per-record (dx, dy) from a
 *     view-mode-selected palette table, stages the primary sprite
 *     descriptor, dispatches one of the three first-family blitters,
 *     and (for records of type 1/2) stages + dispatches an overlay
 *     sprite from a separate RLE-encoded blob.
 *
 * See docs/findings/map-render.md for the full call topology, the
 * per-territory record format, the per-tile-bytecode descriptor
 * format, and the palette table layout.
 *
 * State model: this module holds its own statics for the camera,
 * view-mode, render-mode, table-binding, and blob bases.  The
 * binding setters install pointers + parameters; the four render
 * functions consume that state plus the dispatcher state held by
 * gfx_dispatch / gfx_glyph / gfx_paint / engine_dirty_rect.
 */

#ifndef OPENLORDS2_ENGINE_MAP_RENDER_H
#define OPENLORDS2_ENGINE_MAP_RENDER_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* Bind the 2-D tile-bytecode array iterated by the row helpers.
 *
 *   `cells`         retail `DAT_0055cea0` — base pointer of the
 *                   row-major int array.  Each cell is a 4-byte
 *                   tile-bytecode; values >= ENGINE_MAP_RENDER_CELL_EMPTY
 *                   are treated as "no territory here, skip".
 *   `row_stride_b`  retail `0x104` — byte stride per map row
 *                   (NOT cells; multiplied by the row index in retail).
 */
void engine_map_render_bind_tile_array(const int32_t *cells,
                                       int            row_stride_bytes);

/* Bind the per-tile-bytecode descriptor table base (retail
 * `0x522f90`).  Two byte fields per entry are read:
 *
 *   `descriptors[cell_value + 2]` → per-cell flag byte (bit 0x20
 *                                    is the only one consumed)
 *   `descriptors[cell_value + 5]` → head-of-linked-list index into
 *                                    g_per_territory_420
 *
 * `cell_value` is used as a raw byte offset into `descriptors` — no
 * stride multiplication.  The caller must size the array to cover
 * all `cell_value` values that the bound tile array could produce.
 */
void engine_map_render_bind_tile_descriptors(const uint8_t *descriptors);

/* Bind the three sprite blob bases:
 *
 *   `primary`  retail `DAT_00553224` — used by record types 0/1/2/3.
 *   `alt`      retail `DAT_0056d8bc` — used by record type 4 only.
 *   `overlay`  retail `DAT_0055ce5c` — RLE-encoded blob used for
 *               the second sprite drawn over record types 1/2.
 *
 * Any of the three may be NULL if the corresponding record-type
 * code path is not exercised.  Records that hit an unbound blob
 * will read NULL[N] — caller is responsible.
 */
void engine_map_render_bind_blobs(const uint8_t *primary,
                                  const uint8_t *alt,
                                  const uint8_t *overlay);

/* Bind the per-territory record array.  Retail uses the global at
 * `DAT_0052f0b0` (151 × 420 = 63420 bytes, slot 0 reserved).  Pass
 * the same memory the save schema entry 5 points at. */
void engine_map_render_bind_territory_records(const uint8_t *records);

/* Bind the camera parameters.
 *
 *   `x0`            retail `DAT_0052afdc` — initial cursor X for
 *                   each row's first cell.
 *   `y0`            retail `DAT_00553244` — initial cursor Y for
 *                   the top phantom row.
 *   `col_start`     retail `DAT_005651b4` — starting column index
 *                   into each row of the 2-D tile array.
 *   `row_start`     retail `DAT_005651b8` — starting row index
 *                   into the 2-D tile array.
 *   `col_count`     retail `DAT_0053e8ac` — columns per row.
 *   `row_count`     retail `DAT_0056d67c` — total visible rows
 *                   (frame driver iterates (row_count - 2) / 2
 *                   row-pairs between the phantom rows).
 *   `x_step`        retail `DAT_00568220` — full-width X advance
 *                   per column.
 *   `y_step`        retail `DAT_0053f65c` — Y advance per row.
 *   `x_half_step`   retail `DAT_00567950` — half-width X shift the
 *                   per-tile drawer applies for phase != 1.
 */
void engine_map_render_bind_camera(int x0,
                                   int y0,
                                   int col_start,
                                   int row_start,
                                   int col_count,
                                   int row_count,
                                   int x_step,
                                   int y_step,
                                   int x_half_step);

/* Bind the row-Y-offset accumulator the drawer reads (retail
 * `DAT_0057c97c`) and its companion column-Y accumulator the
 * iterators advance (retail `DAT_0056957c`).  Both are reset by
 * `engine_map_render_frame` to retail's initial values; the
 * setter is exposed for tests that drive the inner functions
 * directly. */
void engine_map_render_set_y_accumulator(int y_acc);

/* Bind the view-mode discriminator (retail `DAT_0057cb18` ∈ {0,1,2}). */
void engine_map_render_set_view_mode(int view_mode);

/* Bind the per-view-mode Y viewport offset (retail `DAT_0057c97c`).
 * Added to each record's palette-derived dy before centering.  Retail
 * stages this at view-mode-change time to one of {30, 14, 6}
 * depending on which scrollable map view is active. */
void engine_map_render_set_y_viewport_offset(int y_off);

/* Bind the rotation offset (retail `DAT_00522f7c`) — subtracted
 * from each record's orientation byte (with wrap-add 8) to produce
 * the palette-table row index. */
void engine_map_render_set_rotation_offset(int rotation_offset);

/* Bind the render-mode flag (retail `DAT_0053f264`).  When this
 * equals 1, only tiles whose descriptor flag byte has bit `0x20`
 * set are drawn (a "fog of war / explored" mask).  Set to anything
 * other than 1 (e.g. 0) to disable the gate. */
void engine_map_render_set_render_mode(int mode_flag);

/* Bind the overlay sprite base index (retail `DAT_0057d390`).
 * Combined with the per-record overlay multiplier to produce the
 * sprite-blob index for the optional second sprite. */
void engine_map_render_set_overlay_base_index(int base_index);

/* Bind the dirty-rect state pointer the per-tile drawer marks.
 * Retail uses a singleton; the port routes through this setter so
 * tests can drive a local state without touching the singleton. */
void engine_map_render_set_dirty_state(engine_dirty_rect_state_t *state);

/* Sentinel: tile-cell values >= this are treated as "empty" and
 * skipped by the row iterators (retail `0xfff0000`). */
#define ENGINE_MAP_RENDER_CELL_EMPTY  0xfff0000

/* Render error counter — bumped by `engine_map_render_tile` each
 * time it encounters an invalid sprite descriptor (src_offset >
 * 310000 OR sprite_w < 1).  Retail global `_DAT_0058fea8`. */
int  engine_map_render_error_count(void);
void engine_map_render_set_error_count(int n);

/* Drawer call counter — bumped each time `engine_map_render_tile`
 * is invoked by the row iterators (and the row iterators count
 * empty-cell skips toward this counter too).  Test-only;
 * NOT a retail global. */
int  engine_map_render_call_count(void);
void engine_map_render_reset_call_count(void);

/* ─────────────────────────────────────────────────────────────────
 * Render functions — port of the four retail leaves.
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_00405487.  Renders one frame of the map: top phantom
 * row + (row_count - 2)/2 pairs of (phase0_row, phase1_row) + bottom
 * phantom row.  Walks the camera (cursor_x, cursor_y) downward by
 * `y_step` per row. */
void engine_map_render_frame(void);

/* Port of FUN_00405862.  Phase-0 row iterator. */
void engine_map_render_row_phase0(void);

/* Port of FUN_004059af.  Phase-1 row iterator (first tile drawn at
 * phase 1). */
void engine_map_render_row_phase1(void);

/* Port of FUN_00408438.  Per-tile drawer.  Walks the per-tile
 * linked list of territory records and dispatches each record's
 * primary + overlay sprites.
 *
 * `phase` ∈ {0, 1, 2}:
 *   0 → x_anchor_offset = +x_half_step
 *   1 → x_anchor_offset = -2
 *   2 → x_anchor_offset = +x_half_step - 2
 */
void engine_map_render_tile(int phase);

#endif /* OPENLORDS2_ENGINE_MAP_RENDER_H */
