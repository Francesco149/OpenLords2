/*
 * OpenLords2 — strategic-map workforce icon-grid overlay.
 *
 * Port of the closure scoped in docs/plans/army-select-move.md ("Tier 2":
 * FUN_00412143) plus its 9 leaves:
 *
 *   FUN_004997bf @ 0x4997bf ( 151 B) → engine_workforce_hud_ensure_buffers
 *   FUN_004b3f0a @ 0x4b3f0a (  74 B) → engine_workforce_hud_capture_backdrop
 *   FUN_004517ca @ 0x4517ca ( 107 B) → engine_workforce_row_sprite_index
 *   FUN_004519ca @ 0x4519ca ( 147 B) → engine_workforce_fill_row_linear
 *   FUN_00451a5d @ 0x451a5d ( 349 B) → engine_workforce_fill_row_dual
 *   FUN_004518a5 @ 0x4518a5 ( 293 B) → engine_workforce_compute_row
 *   FUN_0045161e @ 0x45161e ( 428 B) → engine_workforce_compute_rows
 *   FUN_004126c9 @ 0x4126c9 ( 204 B) → engine_workforce_draw_row
 *   FUN_00412666 @ 0x412666 (  99 B) → engine_workforce_draw_all_rows
 *   FUN_00412143 @ 0x412143 ( 734 B) → engine_workforce_hud_render
 *
 * Renders the 8-category (farmer/rancher/woodcutter/... — category identity
 * not yet cross-checked against the UI labels) workforce icon grid for the
 * selected army's home county: each category is a 25-cell row; filled cells
 * show a little worker-icon sprite (villani1.pl8/villani2.pl8 banks), using
 * either the category's default per-cell colour or a deficit/surplus
 * override colour when the current stock (an unnamed per-army i32 triple,
 * see ARMY_WF_CUR/MIN/MAX below) falls outside its acceptable band.
 *
 * DEFERRED, not ported here (see engine_workforce_hud.c's render function
 * for the exact skip point):
 *   - The "standings" decoration sub-branch (retail `DAT_0053f25c == 1`):
 *     loads a 4th sheet (villtops.pl8) and draws 2 extra sprites + 3
 *     wrapped-text/number lines (a castle icon + tax/population figures).
 *     Separately gated, not part of the icon-grid itself.
 *   - engine_dirty_rect_mark_below_status_bar's call: needs a live
 *     engine_dirty_rect_state_t this call path doesn't have one of yet
 *     (the hook signature below is a bare army_key, no dirty-rect out-
 *     param) — affects redraw-scheduling only, not pixel output.
 *
 * Retail owns two globals this port can't yet reconstruct a producer for
 * (they belong to the not-yet-ported "manage county" screen these icons
 * are one panel of):
 *   DAT_005530c8  — the "active worker-icon" sprite blob.  Exposed here as
 *                   a bindable seam (engine_workforce_hud_bind_icon_sheet),
 *                   NULL by default (draws become no-ops, matching
 *                   gfx_sprite_render_opaque_from_blob's own NULL-blob
 *                   guard).
 *   DAT_0053f050  — the county-screen's active-tab selector (1 == the
 *                   workforce tab; other values gate unrelated tabs this
 *                   port hasn't touched).  Exposed as
 *                   engine_workforce_hud_set_panel_mode, default 0 (so
 *                   engine_workforce_hud_render no-ops until a caller
 *                   explicitly arms it — mirrors retail's own gate).
 *
 * Per-army fields this frontier touches (ARMY_REC(key) = g_per_army_768 +
 * key*0x300, see save.h):
 *   +0xb8  i32  ARMY_WF_SCALE     — per-icon count-scale divisor (retail
 *                                   DAT_0053fa68; 0 → the whole render is
 *                                   skipped, "no workforce assigned yet")
 *   +0xc4  i32  ARMY_WF_CUR(row)  — current stock, row stride 0xc (retail
 *   +0xc8  i32  ARMY_WF_MIN(row)    DAT_0053fa74/78/7c).  Names are
 *   +0xcc  i32  ARMY_WF_MAX(row)    inferred from the threshold compare
 *                                   in engine_workforce_compute_rows
 *                                   (deficit if CUR<MIN, surplus if
 *                                   MAX<CUR) — pending confirmation once
 *                                   a live producer is known.
 * Already-named (engine_territory.c): ARMY_FEAT_A/B/C_FLAG (+0x2ad/+0x2dd/
 * +0x295) — capital "special feature" present-flags; engine_workforce_hud
 * draws one fixed icon per flag when set, re-declared locally here (they're
 * file-static #defines in engine_territory.c, not exposed via its header).
 */

#ifndef OPENLORDS2_ENGINE_WORKFORCE_HUD_H
#define OPENLORDS2_ENGINE_WORKFORCE_HUD_H

#include <stdint.h>

/* --- Seams the (unported) ancestor "manage county" screen would bind. -- */

/* DAT_005530c8: the sprite blob engine_workforce_draw_row reads.  NULL
 * (the default) makes engine_workforce_hud_render fall back to the
 * villani1.pl8 bank it loads for itself — an ancestor screen (once
 * ported) can call this first to override that default explicitly. */
void engine_workforce_hud_bind_icon_sheet(const uint8_t *sheet);

/* DAT_0053f050: the county screen's active-tab selector.  engine_workforce_
 * hud_render no-ops unless this is 1 (the workforce tab).  Default 0. */
void engine_workforce_hud_set_panel_mode(int mode);

/* --- Tier 0/1 leaves (exposed for host testing; also used internally). -- */

/* Port of FUN_004517ca.  Resolve category `category` (0..7) to a sprite-row
 * index (0..8) via the baked k_workforce_row_index table, with one special
 * case: category 0 maps to row 4 instead of the table's row 5 when the
 * army has ARMY_FEAT_B_FLAG clear AND ARMY_FEAT_A_FLAG set. */
int engine_workforce_row_sprite_index(int army_key, int category);

/* Port of FUN_004997bf.  Lazily (re-)allocate the two scratch sprite-load
 * buffers (villani1.pl8 target, 250000 B; villani2.pl8 target, 150000 B).
 * Retail reallocates unconditionally on every call (verified against the
 * decompile — no existing-pointer guard); ported faithfully.  Returns 1 on
 * success, 0 if either allocation fails (retail message-boxes + exits;
 * the port logs and returns instead of calling _exit). */
int engine_workforce_hud_ensure_buffers(void);

/* Port of FUN_004b3f0a.  Copy a `width_dwords`-by-`height` rectangle out of
 * the bound gfx_paint canvas at (x, y) into `dest`, advancing the canvas
 * read pointer by `src_stride` bytes after each row.  Retail reads (x, y,
 * width, height) from shared scratch globals other blit primitives
 * (gfx_glyph/gfx_dispatch) also use; ported with them as explicit
 * parameters instead — this is the only call site, and it avoids
 * coupling this module to gfx_glyph's cursor state for no benefit. */
void engine_workforce_hud_capture_backdrop(uint32_t *dest, int x, int y,
                                           int width_dwords, int height,
                                           int src_stride);

/* Port of FUN_004b3ec0 — the write-back sibling (restore the captured
 * rect into the canvas).  Used by the tooltip clear (FUN_004770b8). */
void engine_workforce_hud_restore_backdrop(const uint32_t *src, int x, int y,
                                           int width_dwords, int height,
                                           int dst_stride);

/* Port of FUN_004519ca.  Fill icon-grid row `category`'s cells (up to 25)
 * whose baked threshold (k_workforce_cell_threshold) is <= `count`: with
 * `fill_code` if nonzero, else with that cell's own default colour
 * (k_workforce_cell_default_color).  category==6 forces fill_code to 2
 * (retail quirk, ported verbatim). */
void engine_workforce_fill_row_linear(int category, int fill_code, int count);

/* Port of FUN_00451a5d.  Two-sided variant: only called when both a
 * position and a magnitude are nonzero.  category==6 forces both fill
 * bytes to 2 (same quirk as engine_workforce_fill_row_linear).  When both
 * position and magnitude are < 13, uses two small graduated threshold
 * tables (k_workforce_small_pos/mag_threshold) against TWO separate
 * 8x25-cell grids (the "position" grid engine_workforce_draw_row reads,
 * and a second "indicator" grid nothing in this closure reads back —
 * computed faithfully anyway in case a future chip needs it).  Otherwise
 * (either >= 13) falls back to a plain linear fill from each end. */
void engine_workforce_fill_row_dual(int category, uint8_t fill_byte,
                                    uint8_t indicator_code,
                                    int position, int magnitude);

/* Port of FUN_004518a5.  Resolve category `category`'s fill colour via
 * engine_workforce_row_sprite_index + k_workforce_fill_byte, clear its
 * grid row, then dispatch to engine_workforce_fill_row_linear (one of
 * position/magnitude is 0) or engine_workforce_fill_row_dual (both
 * nonzero) — matching retail's exact 3-way branch, including the sign
 * flip when magnitude is negative (a deficit). */
void engine_workforce_compute_row(int army_key, int category,
                                  int position, int magnitude);

/* Port of FUN_0045161e.  For each of the 8 categories, read ARMY_WF_SCALE/
 * CUR/MIN/MAX for `army_key`'s resolved sprite-row and derive (position,
 * magnitude) — the current stock scaled onto the 25-cell row, and the
 * signed deficit/surplus magnitude relative to the [MIN,MAX] band — then
 * call engine_workforce_compute_row.  No-ops entirely if ARMY_WF_SCALE is
 * 0 (no workforce assigned to this army's county yet). */
void engine_workforce_compute_rows(int army_key);

/* Port of FUN_004126c9.  Draw category `category`'s row of up to 25 icon
 * sprites from the bound icon sheet (engine_workforce_hud_bind_icon_sheet)
 * at k_workforce_cell_xy-offset positions from (x_base, y_base); skips
 * empty cells.  The one retail quirk ported verbatim: if `category` is the
 * currently-hovered category (an unresolved producer — see
 * DAT_0053e9a0/DAT_00553040 in the .c) the cell's sprite index gets +1
 * (a highlight/blink variant). */
void engine_workforce_draw_row(int category, int x_base, int y_base);

/* Port of FUN_00412666.  Draw all 8 categories at their baked
 * k_workforce_row_xy positions (offset +0x40 in x, +`y_origin` in y). */
void engine_workforce_draw_all_rows(int y_origin);

/* Test-visibility accessor: the currently bound icon sheet (NULL if
 * unbound). */
const uint8_t *engine_workforce_hud_icon_sheet(void);

/* Test-visibility accessor: the computed icon-grid cell value for
 * (category, cell) — what engine_workforce_draw_row reads to decide
 * whether/what to draw.  0 == empty. */
uint8_t engine_workforce_hud_grid_cell(int category, int cell);

/* Port of FUN_00412143 (core path — see the DEFERRED list above for what's
 * skipped).  `full_redraw` mirrors retail's param_1 (drives one extra
 * engine_map_scene_tick call when nonzero, matching the one live call
 * site currently known — engine_army_select's workforce_render hook,
 * which always fires with the retail-equivalent of param_1==1).
 * `army_key` replaces retail's implicit DAT_0057c93c (the globally-
 * selected army) — an explicit-input deviation, consistent with this
 * port's general preference for explicit over ambient state. */
void engine_workforce_hud_render(int army_key, uint32_t now_ms,
                                 int full_redraw);

#endif /* OPENLORDS2_ENGINE_WORKFORCE_HUD_H */
