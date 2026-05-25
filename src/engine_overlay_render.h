/*
 * OpenLords2 — strategic-mode OVERLAY pass renderer.
 *
 * Sibling of `engine_map_render` (the primary territory-sprite layer).
 * The overlay pass runs immediately AFTER the primary pass each frame
 * — the parent caller invokes `FUN_00405602` (this cluster's driver)
 * and then `FUN_00405487` (the primary cluster's driver) back-to-back.
 * Both passes walk the SAME 2-D tile array and consume the SAME
 * per-tile descriptor table — but each pass tests a different set of
 * descriptor flag bits (`0x80` and `0x40` here vs `0x20` for the
 * primary pass) and dispatches to a different per-tile drawer.
 *
 * Cluster (5 of 6 functions ported in this chip; the army drawer is
 * deferred to the next chip):
 *
 *   FUN_00405602 @ 0x405602 (608 B)  → engine_overlay_render_frame
 *     Outer driver: pre-clears the left strip (view-mode 1 only),
 *     iterates one top phantom row, one phase-1 row, then
 *     (row_count - 2) / 2 pairs of (phase-0, phase-1) rows, then one
 *     bottom phantom row.  Dispatches per-cell to the army (bit 0x80)
 *     and city (bit 0x40) drawers.
 *
 *   FUN_00405eb5 @ 0x405eb5 (247 B)  → engine_overlay_render_row_phase0
 *     Phase-0 row iterator.  All cells phase 0.  No left/right edge
 *     extras (unlike the primary pass).  Also fires the debug-overlay
 *     text helper between cells (quirk).
 *
 *   FUN_00405fac @ 0x405fac (497 B)  → engine_overlay_render_row_phase1
 *     Phase-1 row iterator.  First cell phase 1, middle cells phase 0
 *     (with debug text), last cell phase 2.  Debug text fires only on
 *     the middle cells (quirk).
 *
 *   FUN_004081a6 @ 0x4081a6 (658 B)  → engine_overlay_render_tile_city
 *     Per-tile drawer for descriptor flag bit 0x40 (cities / towns /
 *     settlements).  Computes a sprite index based on a per-cell size
 *     table at `0x500c30` capped by the active territory's max-size
 *     bytes at `+0x153 / +0x154`.  Dispatches via the RLE sprite
 *     family (gfx_dispatch_dispatch_sprite_rle).
 *
 *   FUN_00408c50 @ 0x408c50 (390 B)  → engine_overlay_render_debug_text
 *     Two-mode developer overlay: when `dev_flag_cell_value != 0` AND
 *     `dev_flag_descriptor_value == 0`, draws the cell's small-table
 *     value at (cursor_x + 0x18, cursor_y + 10).  When
 *     `dev_flag_descriptor_value != 0`, draws the per-tile
 *     descriptor's head-link byte (+5) at (cursor_x, cursor_y - 0x1e).
 *     Both paths route through gfx_text_label_render_dec, which is
 *     already ported.  No-op when the cell is empty (>= EMPTY).
 *
 * Deferred to next chip:
 *
 *   FUN_004071a0 @ 0x4071a0 (3179 B) → engine_overlay_render_tile_army
 *     Per-tile drawer for descriptor flag bit 0x80 (units / armies).
 *     Large state-mutating drawer with multiple unit categories.  In
 *     this chip exposed as a STUB that bumps a per-call counter so
 *     the row iterators can be tested for dispatch fidelity; the real
 *     port replaces the stub body in the next chip.
 *
 * See docs/findings/overlay-render.md for full call topology,
 * descriptor flag semantics, and the city drawer's sprite-index
 * derivation.  Shares storage layout knowledge with map-render.md.
 *
 * State model: this module holds its own statics for the camera,
 * tile-array binding, view-mode, active-territory, overlay blob,
 * per-cell size table, debug font/glyph-table, and dev flags.  The
 * cursor (g_cursor_x / g_cursor_y in gfx_glyph) is shared with the
 * primary pass (retail singleton); both passes write through it
 * sequentially, never concurrently.
 */

#ifndef OPENLORDS2_ENGINE_OVERLAY_RENDER_H
#define OPENLORDS2_ENGINE_OVERLAY_RENDER_H

#include <stdint.h>

#include "engine_dirty_rect.h"

/* ─────────────────────────────────────────────────────────────────
 * Bindings — install retail globals into this module's statics
 * ───────────────────────────────────────────────────────────────── */

/* Same 2-D int tile array consumed by the primary pass (retail
 * `DAT_0055cea0`, row stride 0x104).  Cell values >= EMPTY skip the
 * dispatch but the cursor still advances. */
void engine_overlay_render_bind_tile_array(const int32_t *cells,
                                           int            row_stride_bytes);

/* Same byte-indexed per-tile descriptor table the primary pass uses
 * (retail `0x522f90`).  This pass reads:
 *
 *   `descriptors[cell + 0]` → first descriptor byte (DAT_00522f90);
 *                              the city drawer tests it against 0x14
 *                              when its raw-flag-bit 0x80 is set.
 *   `descriptors[cell + 1]` → raw flag byte (DAT_00522f91), bits
 *                              0x10 / 0x40 / 0x80 selected by the
 *                              city drawer to override sprite index.
 *   `descriptors[cell + 2]` → dispatch flag byte (DAT_00522f92);
 *                              bit 0x80 selects the army drawer,
 *                              bit 0x40 selects the city drawer.
 *                              CLEARED by the city drawer (& 0xbf)
 *                              after dispatch.
 *   `descriptors[cell + 5]` → debug head-link byte (DAT_00522f95),
 *                              displayed by debug-text mode 2.
 */
void engine_overlay_render_bind_tile_descriptors(uint8_t *descriptors);

/* Bind the RLE overlay sprite blob (retail `DAT_0055ce5c`).  Used by
 * the city drawer.  Same blob the primary pass's record-type-1/2
 * overlay path uses. */
void engine_overlay_render_bind_overlay_blob(const uint8_t *blob);

/* Bind the per-territory record array (retail `DAT_0052f0b0`, 151 ×
 * 0x1a4 B, slot 0 reserved).  Same memory the primary pass binds.
 *
 * The army drawer reads the per-territory record at fields:
 *
 *   `record[+0x02]`  → owner index byte (drives the "large army"
 *                       sprite index in the 0x80-only / bVar2 ≥ 13
 *                       branch).
 *   `record[+0x19a]` → linked next-record index byte (army-tag aux
 *                       header used by FUN_00407f82's label sprite).
 *   `record[+0x19c]` → label numeric value byte (the integer drawn
 *                       under the label sprite by FUN_00407f82).
 *
 * The army drawer accepts non-const access (it doesn't mutate any
 * territory field). */
void engine_overlay_render_bind_territory_records(const uint8_t *records);

/* Bind the per-army record array (retail `DAT_0053f9b0`, 17 × 0x300 B,
 * slot 0 reserved — same memory `g_per_army_768` covers).
 *
 * The army drawer indexes into this by `descriptors[cell + 7]` (the
 * per-tile descriptor's army-record index byte) and reads pairs of
 * 32-bit fields as (current, previous) animation drivers plus a
 * signed-byte "tier flag" per category:
 *
 *   chase  : (+0x2a0, +0x2a4) int pair; +0x296 i8 tier flag.
 *   fight  : (+0x2b8, +0x2bc) int pair; +0x2ae i8 tier flag.
 *   march  : (+0x2d0, +0x2d4) int pair; +0x2c6 i8 tier flag.
 *   die    : (+0x2e8, +0x2ec) int pair; +0x2de i8 tier flag.
 *
 * The "march" and "chase" labels are tentative — what matters here is
 * that each category has an independent (cur, prev) integer pair whose
 * difference picks an animation rate tier.
 *
 * The drawer additionally reads:
 *
 *   `army[+0x07]`  byte — "fortress slot" index (used by the 0x40
 *                          path with descriptor[+4]&0xf == 0).
 *   `army[+0x1ad]` byte — "march-out-of-territory" slot byte (used by
 *                          the 0x40 path with descriptor[+4]&0xf == 2).
 *   `army[+0x1bc]` int  — territory-link int (used by the 0x80-only
 *                          ≥13 branch as the territory index for the
 *                          large-army sprite lookup). */
void engine_overlay_render_bind_army_records(const uint8_t *records);

/* Bind the per-cell-size 16-bit short table (retail `DAT_00500c30`).
 * Indexed by `(cell + (cell >> 31 & 7)) >> 3` (i.e. cell/8 with the
 * 3-bit positive-bias rounding for negative cells).  Each entry is a
 * signed 16-bit short.  The city drawer uses `table[idx] - 1` as the
 * sprite-size selector before the territory cap. */
void engine_overlay_render_bind_per_cell_size_table(const int16_t *table);

/* Bind the camera parameters — identical to the primary pass's bind.
 * Both passes share the same retail camera globals. */
void engine_overlay_render_bind_camera(int x0,
                                       int y0,
                                       int col_start,
                                       int row_start,
                                       int col_count,
                                       int row_count,
                                       int x_step,
                                       int y_step,
                                       int x_half_step);

/* Bind the back-buffer canvas (just the pre-clear left strip is
 * drawn directly to pixels — the rest dispatches through gfx_glyph /
 * gfx_paint_canvas_*).  Passed for the view-mode-1 pre-clear; if NULL
 * the pre-clear becomes a no-op. */
void engine_overlay_render_bind_canvas(uint8_t *pixels,
                                       int      pitch,
                                       int      height);

/* Bind the debug-text font (retail `DAT_005af8f0`) and its char→glyph
 * mapping table.  Pass NULL/NULL to suppress the debug overlay's
 * text-render path (the dispatch still runs; the renderer becomes a
 * no-op via gfx_text_render_line's null-table guard).  Color is the
 * palette index the rendered digits use (retail uses '\xf9' for
 * negative values, ' ' for non-negative in mode 1; '\xf9' fixed in
 * mode 2). */
void engine_overlay_render_bind_debug_font(const uint8_t *font_base,
                                           const uint8_t *char_glyph_table);

/* Bind the dirty-rect state pointer the per-tile drawers mark.  Same
 * singleton the primary pass binds. */
void engine_overlay_render_set_dirty_state(engine_dirty_rect_state_t *state);

/* Set the view mode discriminator (retail `DAT_0057cb18` ∈ {0,1,2}).
 * View-mode 1 triggers the pre-clear of the left 4-pixel strip and
 * a narrower Y clip bottom (0x1d8 vs 0x1da for view-modes 0/2). */
void engine_overlay_render_set_view_mode(int view_mode);

/* Set the active territory index (retail `DAT_0057c8c4`) — selects
 * which territory's max-size bytes (+0x153 / +0x154) the city drawer
 * uses as its sprite-size cap.  Range [1, 150]. */
void engine_overlay_render_set_active_territory(int territory_idx);

/* Set the render-mode flag (retail `DAT_0053f264`).  When this is `1`
 * AND descriptor[+2]&0x20 is clear, the army drawer skips the cell
 * entirely (this is the "dev-mode foggable-only filter" — armies on
 * cells without bit 0x20 are suppressed).  Default 0 — production-mode
 * shows all armies.  Same retail global the primary pass uses. */
void engine_overlay_render_set_render_mode(int mode);

/* Set the overlay sprite base index (retail `DAT_0057d390`).  The army
 * drawer uses this as the base for the "large army" sprite picked when
 * descriptor[+1]&0x40 fires the 0-low-nibble fortress branch and for
 * the 0x80-only ≥13 territory-color marker (`base + 8*color - 8`).
 * Same retail global the primary pass binds. */
void engine_overlay_render_set_overlay_base_index(int base);

/* Set the arrow-marker base sprite index (retail `_DAT_0057d38c`,
 * 4-byte int, NOT the byte at 0x57d390).  Used by the 0x80-only +
 * 0x20-flagged "directional arrow" branch:
 *
 *   first row (bVar2 in 0x10..0x12): `base + 6*(bVar2-0x10) + 0x67`
 *   second row (bVar2 in 0x14..0x16): `base + 6*(bVar2-0x14) + 0x55`
 *
 * Default 0. */
void engine_overlay_render_set_arrow_marker_base_index(int base);

/* Set the global animation tick counter (retail `DAT_0053e9a4`).  The
 * "transition-end" branches (categories 3, 6, 9, 12 and the
 * descriptor[+1]&0x10 / bVar2==0x13 branch) gate on `tick <= 0xf` and
 * use `tick + 0x28` as the sprite index.  Range expected [0, 0xf]
 * (cycle); values > 0xf skip the dispatch. */
void engine_overlay_render_set_animation_tick(int tick);

/* Set the four animation-rate tier flags (retail `DAT_0058fd08`,
 * `DAT_0057d3c8`, `DAT_0057d3ac`, `DAT_00591510`).  Selected by the
 * `(cur - prev)` distance computed from the per-army record pair:
 *
 *   distance <  10  → tier 0 flag (`DAT_0058fd08`)
 *   distance < 0x19 → tier 1 flag (`DAT_0057d3c8`)
 *   distance < 0x32 → tier 2 flag (`DAT_0057d3ac`)
 *   distance ≥ 0x32 → tier 3 flag (`DAT_00591510`)
 *
 * Non-zero ⇒ animation advances this frame; zero ⇒ frame held.  Each
 * is an independent retail flag that the per-frame tick logic flips. */
void engine_overlay_render_set_animation_rate_flags(int tier0,
                                                    int tier1,
                                                    int tier2,
                                                    int tier3);

/* Set the dev-mode debug overlay flags.
 *
 *   `cell_value_mode`        retail `DAT_005bb4a4` — when 1 (and
 *                            `descriptor_value_mode` == 0), the debug
 *                            text helper draws the cell's per-cell
 *                            size-table value at the bumped cursor.
 *   `descriptor_value_mode`  retail `DAT_005bb4a5` — when 1, the debug
 *                            text helper draws the per-tile-byte
 *                            descriptor's head-link byte at the
 *                            negative-Y-shifted cursor.  Mutually
 *                            exclusive (`descriptor_value_mode` wins).
 *
 * Both default to 0 — no debug overlay drawn.  Setting either to 1
 * is a developer-build behavior; production builds keep both at 0. */
void engine_overlay_render_set_dev_flags(int cell_value_mode,
                                         int descriptor_value_mode);

/* ─────────────────────────────────────────────────────────────────
 * Counters / instrumentation
 * ───────────────────────────────────────────────────────────────── */

/* Render-error counter (retail `_DAT_0058fea8`).  Conflated with the
 * primary pass's counter — both modules bump the same retail global.
 * The port keeps each module's count private so tests can isolate. */
int  engine_overlay_render_error_count(void);
void engine_overlay_render_set_error_count(int n);

/* Per-dispatcher call counts.  Not retail globals — strictly test
 * instrumentation.  Each is bumped once per drawer invocation. */
int  engine_overlay_render_army_call_count(void);
int  engine_overlay_render_city_call_count(void);
int  engine_overlay_render_debug_call_count(void);
void engine_overlay_render_reset_call_counts(void);

/* Sentinel: tile-cell values >= this are treated as "empty" and
 * skipped by the row iterators (shared with the primary pass). */
#define ENGINE_OVERLAY_RENDER_CELL_EMPTY  0xfff0000

/* ─────────────────────────────────────────────────────────────────
 * Render functions
 * ───────────────────────────────────────────────────────────────── */

/* Port of FUN_00405602.  Driver entry point: renders one full overlay
 * frame.  Resets the cursor, clears col_y_accum, sweeps the top
 * phantom row, calls phase1_row, loops (row_count - 2) / 2 pairs of
 * (phase0_row, phase1_row), then sweeps the bottom phantom row. */
void engine_overlay_render_frame(void);

/* Port of FUN_00405eb5.  Phase-0 row iterator — `col_count` cells at
 * phase 0, each with a between-cell debug-text call. */
void engine_overlay_render_row_phase0(void);

/* Port of FUN_00405fac.  Phase-1 row iterator — one phase-1 first
 * cell, (col_count - 1) phase-0 middle cells (with debug text), one
 * phase-2 trailing cell. */
void engine_overlay_render_row_phase1(void);

/* Port of FUN_004081a6.  Per-tile city/town/settlement drawer.
 * `phase` is 0/1/2 mirroring the row iterators; the X anchor offset
 * is +0x14 for phase != 1, +0x14 - x_half_step for phase 1.  Reads
 * the per-cell-size table, applies the active territory's max-size
 * cap, picks fixed sprite 0x4e when the descriptor's raw-flag-bit
 * 0x10/0x40/0x80 selectors are set, otherwise picks 0x38 + size.
 * Stages from the RLE overlay blob and dispatches via the RLE
 * sprite-family blitters. */
void engine_overlay_render_tile_city(int phase);

/* Port of FUN_004071a0 (3179 B).  Per-tile army / units drawer for
 * descriptor flag bit 0x80.  Top-level dev filter: skip cell when
 * `render_mode == 1` AND descriptor[+2]&0x20 is clear.  Then dispatch
 * branch on `descriptor[+1]` flag bits in priority order:
 *
 *   bit 0x40        → fortress / march-out path (gated on
 *                      descriptor[+4]&0xf == 0 or 2; reads
 *                      per-army[+0x07] or [+0x1ad]).
 *   bit 0x10        → animation-end path (gated on
 *                      descriptor[+0] == 0x13).
 *   bit 0x20        → directional-arrow path (gated on
 *                      descriptor[+0] in 0x10..0x16 excl 0x13).
 *   only bit 0x80   → unit-category path:
 *                      descriptor[+0] is the category byte:
 *                        <13 — animated category (chase/fight/march/
 *                              die — see findings doc); writes the
 *                              new animation frame back to
 *                              descriptor[+3].
 *                        13..0x19 (excl. 0x14) — large-army sprite
 *                              at a territory; calls the territory-
 *                              label sub-renderer (FUN_00407f82).
 *
 * Common dispatch tail: stages from the overlay blob; validates
 * src_off < 0x11171 (= 70001) — a TIGHTER bound than the primary
 * pass's 310001.  If `phase == 1` shifts X anchor by -x_half_step.
 * Marks dirty rect (32-px for kind=1, 48-px for kind=2). */
void engine_overlay_render_tile_army(int phase);

/* Port of FUN_00408c50.  Two-mode debug overlay text helper.  Reads
 * the current cell (via the iterators' `g_current_cell`) and the
 * mode flags set via `engine_overlay_render_set_dev_flags`; selects
 * which value to render (per-cell size table OR descriptor +5 byte)
 * and dispatches gfx_text_label_render_dec at a mode-specific offset
 * from the current cursor. */
void engine_overlay_render_debug_text(void);

#endif /* OPENLORDS2_ENGINE_OVERLAY_RENDER_H */
