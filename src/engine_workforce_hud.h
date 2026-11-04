/*
 * OpenLords2 — strategic-map workforce icon-grid overlay.
 *
 * Port of the closure scoped in docs/plans/army-select-move.md ("Tier 2":
 * FUN_00412143) plus its 9 leaves:
 *
 *   FUN_004997bf @ 0x4997bf ( 151 B) → engine_workforce_hud_ensure_buffers
 *   FUN_004b3f0a @ 0x4b3f0a (  74 B) → engine_workforce_hud_capture_backdrop
 *   FUN_004b3ec0 @ 0x4b3ec0 (  74 B) → engine_workforce_hud_restore_backdrop
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
 * The render is retail's whole (the "standings" head with villtops.pl8 and its three lines included); its state
 * is on retail's layout (the globals below): the icon sheet DAT_005530c8, the tab selector DAT_0053f050 (1 == the
 * workforce tab; engine_workforce_hud_set_panel_mode), the panel origin DAT_0057c908, the villani banks
 * DAT_0053e918 / DAT_005533c4, the grid DAT_005540a0.
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

#include "engine_dirty_rect.h"
#include "localization.h"

/* --- Retail-layout globals (engine_workforce_hud_globals.c; the hybrid host aliases each at its VA) --- */
extern uint8_t g_wf_grid[8 * 25];           /* DAT_005540a0 the icon grid (category · 25 + cell) */
extern const uint8_t *g_wf_icon_sheet;      /* DAT_005530c8 the worker-icon sheet */
extern uint8_t g_wf_hover_category;         /* DAT_0053e9a0 the hovered category + 1 (0: none) */
extern uint8_t g_wf_hover_cells[25];        /* DAT_00553040 its per-cell highlight bytes */
extern int32_t g_wf_panel_mode;             /* DAT_0053f050 the county screen's tab (1: the workforce) */
extern int32_t g_wf_y_origin;               /* DAT_0057c908 the panel's y origin */
extern uint8_t *g_wf_villani1;              /* DAT_0053e918 villani1.pl8 (FUN_004997bf allocates it) */
extern uint8_t *g_wf_villani2;              /* DAT_005533c4 villani2.pl8 */

/* The render's text (the standings lines; NULL loc → skipped), its system sheet (the seal), and — the hybrid host —
 * the dirty rectangle + retail's buffer allocator FUN_004997bf (bound: the vill.pl8 sheets and the backdrop live in
 * the buffer DAT_004eabec points at, as retail's). */
void engine_workforce_hud_bind_text(localization_state_t *loc, const uint8_t *title_font, const uint8_t *menu_font,
                                    const uint8_t *glyph_table);
void engine_workforce_hud_bind_system_sheet(const uint8_t *sheet);
void engine_workforce_hud_bind_retail(engine_dirty_rect_state_t *dirty, int (*ensure)(void));

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

/* Port of FUN_004b3f0a.  Copy the staged rectangle out of the canvas into `dest`: retail reads its origin and size
 * from the gfx globals the blitters share — the cursor (g_gfx_cursor_x / _y, DAT_00591524 / DAT_00591528), the
 * width in DWORDS (g_gfx_glyph_w, DAT_005c9258) and the row count (g_gfx_glyph_height_short, DAT_005aeb70; a
 * do-while: one row at least) — and advances the canvas pointer by `residual_stride` bytes after each row's
 * copy (the pitch minus the row's 4 * width).  Callers stage those globals first, as retail's do. */
void engine_workforce_hud_capture_backdrop(uint32_t *dest, int residual_stride);

/* Port of FUN_004b3ec0 — the write-back sibling (the staged rectangle from `src` back into the canvas).  The
 * tooltip clear (FUN_004770b8) and the armory march restore use it. */
void engine_workforce_hud_restore_backdrop(const uint32_t *src, int residual_stride);

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
 * tables (k_workforce_small_pos/mag_threshold): the position cells at the
 * row's start, the "indicator" cells at DAT_005540aa — retail's grid
 * DAT_005540a0 ten cells on (QUIRK: the indicator overwrites the row's
 * cells 10..24, which the draw then shows).  Otherwise (either >= 13)
 * falls back to a plain linear fill from each end. */
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

/* Port of FUN_00412143.  `full_redraw` mirrors retail's param_1 (drives one extra
 * engine_map_scene_tick call when nonzero, matching the one live call
 * site currently known — engine_army_select's workforce_render hook,
 * which always fires with the retail-equivalent of param_1==1).
 * `army_key` replaces retail's implicit DAT_0057c93c (the globally-
 * selected army) — an explicit-input deviation, consistent with this
 * port's general preference for explicit over ambient state. */
void engine_workforce_hud_render(int army_key, uint32_t now_ms,
                                 int full_redraw);

#endif /* OPENLORDS2_ENGINE_WORKFORCE_HUD_H */
