/*
 * OpenLords2 — generic Yes/No confirm-dialog modal (screen 0x1e).
 *
 * Port of:
 *   FUN_0040e6f2 @ 0x40e6f2  (111 B) → engine_confirm_dialog_open
 *   FUN_0040ccfa @ 0x40ccfa  ( 94 B) → engine_confirm_dialog_render
 *   FUN_00434e1f @ 0x434e1f  ( 37 B) → the button-fire handler (static)
 *
 * This is the SAME generic modal the title-menu system already partially
 * ported for its own screens (FUN_004ba9c8's 0x1f arm lives in
 * engine_menu_input.c) — screen 0x1e is just another arm of that same
 * retail dispatcher/renderer pair (FUN_004ba9c8 / FUN_004ba26e), reusing
 * the ALREADY-PORTED hit-test + render primitives (engine_menu_button_
 * dispatch = FUN_0040da1e, engine_button_render_list = FUN_0040cfd2) over
 * the SAME 0x18-byte engine_button_rec_t table shape.
 *
 * The 2-entry button table (r2-verified byte-for-byte at retail 0x4dd310
 * via `r2 -q -c 'pxr 128 @ 0x4dd2e0' vendor/original/Lords2.exe`): index 0
 * is "Yes" (x=64,y=46, sprite 29, param0=1), index 1 is "No" (x=112,y=50,
 * sprite 31, param0=0).  Both records' retail `action` field holds the
 * SAME raw function address (FUN_00434e1f) — retail calls it directly as
 * a 0-arg function pointer, which isn't portable; the port instead gives
 * both records the same placeholder `action` code and routes through one
 * `engine_menu_button_action_fn` dispatcher that reads `param0` (the only
 * thing that actually differs between Yes and No).
 *
 * Retail's FUN_00434e1f does `DAT_0059151c = (u8)DAT_0059154c;
 * (*DAT_00552ff4)(); DAT_0057d340 = 2;` — i.e. it does NOT touch the
 * screen at all.  Screen restoration is each CALLBACK's own job (every
 * caller in engine_army_move_confirm.c starts by reading
 * engine_confirm_dialog_prev_screen()), exactly mirroring retail's
 * DAT_0057c90c usage — so this module does not attempt to restore the
 * screen itself either.
 *
 * Message text: `FUN_00402d37(10, msg_id, ox+0x10, oy+0x10, &DAT_005af8f0,
 * 0x3f)` — loc_index is the FIXED constant 10 (a dedicated "confirm
 * dialog messages" localization group), msg_id (the opener's caller-
 * supplied first arg, e.g. 0xd / 4 / 5 / 7 / 8 / 10 at various call
 * sites) is skip_strings, selecting which sub-string of that group to
 * show.  Font is fntl2_14.pl8 (DAT_005af8f0), same as gfx_load_menu_font().
 *
 * FUN_004b1de0 (called from the retail render arm) is a verified no-op
 * (11-byte `{ return; }`) — elided, matching the precedent already set in
 * engine_hud_panel.h.  DAT_004eb260 (a render-flag write with no found
 * reader) is likewise elided as present-time bookkeeping.
 */
#ifndef OPENLORDS2_ENGINE_CONFIRM_DIALOG_H
#define OPENLORDS2_ENGINE_CONFIRM_DIALOG_H

#include <stdint.h>

#include "localization.h"
#include "engine_menu_input.h"   /* engine_menu_click_in_t */

/* Retail DAT_004eac50 value while the dialog is open. */
#define ENGINE_CONFIRM_DIALOG_SCREEN 0x1e

/* Fires with NO arguments when the user answers — must call
 * engine_confirm_dialog_answer() itself to learn Yes(1)/No(0), and
 * engine_confirm_dialog_prev_screen() to know what screen to restore
 * (mirrors retail's DAT_0059151c / DAT_0057c90c mechanism exactly). */
typedef void (*engine_confirm_dialog_callback_fn)(void);

/* Port of FUN_0040e6f2.  `prev_screen` is the screen to stash for the
 * callback to read back later (retail: whatever DAT_004eac50 already was
 * at the call site, often already 0 by the time this fires).  Sets
 * *out_screen = ENGINE_CONFIRM_DIALOG_SCREEN and *out_demand_redraw = 2;
 * resets the answer flag to 0. */
void engine_confirm_dialog_open(int msg_id, int ox, int oy, int prev_screen,
                                engine_confirm_dialog_callback_fn callback,
                                int *out_screen, int *out_demand_redraw);

/* Port of the DAT_0059151c global — the last button's param0 (1 = Yes,
 * 0 = No), valid for a callback to read while it's running (set just
 * before the callback fires). */
int engine_confirm_dialog_answer(void);

/* Force the answer flag directly, for callers that invoke a would-be
 * dialog callback WITHOUT actually opening the dialog (retail's own
 * "no confirmation needed" fast path — e.g. FUN_004a9252 sets
 * DAT_0059151c = 1 unconditionally before deciding whether to call a
 * sibling directly or route it through engine_confirm_dialog_open). */
void engine_confirm_dialog_force_answer(int answer);

/* Port of the DAT_0057c90c global — the screen stashed by the most
 * recent engine_confirm_dialog_open call. */
int engine_confirm_dialog_prev_screen(void);

/* Input-tick hit-test — call while screen == ENGINE_CONFIRM_DIALOG_SCREEN
 * (mirrors FUN_004ba9c8's 0x1e arm: FUN_0040da1e(ox,oy,&table,2)).
 * Returns 1 if a button fired this frame (the stored callback has
 * already been invoked by the time this returns). */
int engine_confirm_dialog_input_step(const engine_menu_click_in_t *in);

/* Render-tick draw — call while screen == ENGINE_CONFIRM_DIALOG_SCREEN
 * (mirrors FUN_004ba26e's 0x1e arm: FUN_0040ccfa + FUN_0040cfd2). */
void engine_confirm_dialog_render(localization_state_t *loc,
                                  const uint8_t *chrome,
                                  const uint8_t *menu_font,
                                  const uint8_t *system_sheet,
                                  const unsigned char *glyph_metrics);

#endif /* OPENLORDS2_ENGINE_CONFIRM_DIALOG_H */
