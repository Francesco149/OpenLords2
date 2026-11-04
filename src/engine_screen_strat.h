/*
 * OpenLords2 — strategic-map screen-id-0 render arm.
 *
 *   FUN_0040f5fd @ 0x40f5fd (470 B) → engine_screen_strat_render
 *
 * The screen-dispatch (FUN_0040f1a0) arm for screen-state 0 (the in-game
 * strategic overworld).  Composes the full strategic-map frame from the
 * already-ported leaves, in retail draw order:
 *
 *   FUN_004b1867   clear the back buffer            (gfx canvas memset)
 *   FUN_0040a567   right-panel sand sprites 0x36/0x39/0x3b
 *   FUN_004050c0   map body (engine_map_scene)
 *   FUN_00419c78   top menu bar + realm-status tail (engine_hud_menubar)
 *   FUN_0040f7d3   right-panel territory-info panel (engine_hud_panel)
 *   FUN_0041a734   "End turn" button strip
 *   FUN_00410aa9   right-panel political minimap     (engine_minimap)
 *
 * This is a faithful port of FUN_0040f5fd's full-redraw (param_1 == 1)
 * path.  It is SELF-CONTAINED: every input is gathered from the engine's
 * own getters / save-schema globals, so the only precondition is that the
 * data path has run — the save is loaded (save_load), the season sprite
 * banks are loaded (engine_pl8_load_strategic_banks), the render arrays
 * are built + finalised (engine_map_init + the load-finalize tail), and
 * the minimap grids are loaded (engine_minimap_load_grids).  The
 * --strat-load bring-up does exactly that setup before calling here.
 *
 * The CALLER owns the DirectDraw lock / present, exactly like the title
 * arm: lock the canvas (gfx_lock_canvas), call this, unlock + present
 * (gfx_tick_present).  FUN_0040f5fd's own present tail (FUN_00452160 /
 * FUN_004b0ab5) maps to the loop's present step; its full-redraw prep
 * (0x4b1897 / 0x4b11ce) maps to the gfx layer's lock path.
 */

#ifndef OPENLORDS2_ENGINE_SCREEN_STRAT_H
#define OPENLORDS2_ENGINE_SCREEN_STRAT_H

/* Compose one full strategic-map frame into the (already-locked) gfx
 * canvas.  `redraw_mode` is retail FUN_0040f5fd's param_1 — only the
 * full-redraw (1) path is ported; the value is accepted for dispatch
 * signature parity and otherwise unused (every call repaints the whole
 * frame).  No-op when the strategic data path has not run
 * (engine_pl8_data_ready() == 0, retail's DAT_0053f050 == 1 gate). */
void engine_screen_strat_render(int redraw_mode);

/* FUN_00419c78 — the top menu bar (File/Options/Help parchment + realm
 * status tail).  Exposed for the labor screen's blacksmith arm, which
 * repaints it over the smithy background. */
void engine_screen_strat_render_menubar(void);

/* Select the map-body animation mode (see engine_screen_strat.c).  0 (default)
 * = PINNED at anim frame 0 — the byte-exact frame the single-shot bring-up
 * seats + capture-parity harness expect.  1 = LIVE — the in-game loop owns the
 * anim counters (advanced at the tick's FUN_004050c0 seam) so the map animates
 * and reflects a season/bank change.  Set by the in-game tick; never by a seat. */
void engine_screen_strat_set_anim_live(int live);

#endif /* OPENLORDS2_ENGINE_SCREEN_STRAT_H */
