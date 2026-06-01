/*
 * OpenLords2 — top-level screen-state render dispatch.
 *
 *   FUN_0040f1a0 @ 0x40f1a0 (1117 B) → engine_screen_dispatch
 *
 * The big switch on the screen-state byte (DAT_004eac50) that the
 * per-tick driver fires from its render phase via the demand-redraw
 * seam (`if (DAT_0057d340) FUN_0040f1a0(DAT_0057d340)`).  Each arm calls
 * the renderer for one screen.  Only the title screen (0x1f) is ported
 * so far — it routes to engine_title_render_scene; every other state is
 * funnelled through a single bound hook (the unported per-screen
 * renderers) that receives the screen-state and the redraw-mode arg.
 * Screen-state 0 (the in-game strategic map → FUN_0040f5fd) has its own
 * dedicated hook so the drop-in can wire the ported strategic-map arm
 * (engine_screen_strat_render) without dragging the Win32 gfx layer into
 * the host-test link.  After the dispatch, DAT_004eb260 is cleared (owned
 * by engine_title_render) exactly as retail does unconditionally.
 */

#ifndef OPENLORDS2_ENGINE_SCREEN_DISPATCH_H
#define OPENLORDS2_ENGINE_SCREEN_DISPATCH_H

/* Hook for the not-yet-ported per-screen renderers.  Receives the
 * screen-state byte and the redraw-mode arg (retail param_1).  Default
 * is a no-op-plus-record stub. */
typedef void (*engine_screen_dispatch_other_fn)(int screen_state, int redraw_mode);
void engine_screen_dispatch_bind_other_fn(engine_screen_dispatch_other_fn fn);

/* Hook for screen-state 0 — the in-game strategic-map arm (FUN_0040f5fd).
 * Bound by the drop-in to engine_screen_strat_render; the host-test build
 * leaves it at the default no-op-plus-record stub (the real renderer pulls
 * in the Win32 gfx layer).  Receives the redraw-mode arg (retail param_1). */
typedef void (*engine_screen_dispatch_strat_fn)(int redraw_mode);
void engine_screen_dispatch_bind_strat_fn(engine_screen_dispatch_strat_fn fn);

/* Test instrumentation: the (screen_state, redraw_mode) the other-screen
 * hook was last called with, or (0,0) if never since reset; and the
 * redraw-mode the strat (screen-0) hook was last called with. */
int  engine_screen_dispatch_last_other_state(void);
int  engine_screen_dispatch_last_other_redraw(void);
int  engine_screen_dispatch_last_strat_redraw(void);
void engine_screen_dispatch_reset(void);

/* Port of FUN_0040f1a0.  Dispatch the renderer for `screen_state`
 * (0x1f → the title scene with `page`; 0 → the strategic-map hook with
 * `redraw_mode`; all others → the generic hook with `redraw_mode`), then
 * clear DAT_004eb260. */
void engine_screen_dispatch(int screen_state, int page, int redraw_mode);

#endif /* OPENLORDS2_ENGINE_SCREEN_DISPATCH_H */
