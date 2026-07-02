/*
 * OpenLords2 — in-game (strategic) input dispatcher.
 *
 * Port of retail's per-tick in-game input/UI dispatcher
 *   FUN_0042ff10 @ 0x42ff10 (8140 B)
 * a SCREEN switch keyed on DAT_004eac50 (the current-screen byte,
 * engine-quirk #197).  Retail's render tick (FUN_004b99c0) calls it once
 * per frame before the render phase; it routes every in-game click —
 * HUD buttons, the right panel, End-Turn, and map-tile clicks (army
 * select on LMB, terrain-info on RMB).
 *
 * SCOPE OF THIS PORT (the skeleton — docs/plans/ingame-input-handler.md):
 *   - the faithful prologue latches + the region classifier (FUN_00432640)
 *   - the dismiss-chip / UI-router subsumption (the dispatcher calls the
 *     open-scroll dismiss handler FIRST, exactly like retail; the port's
 *     engine_popup_scroll_input_step binds in as `dismiss_chip`)
 *   - the STRATEGIC screen (DAT_004eac50 == 0) case, in full: the eight
 *     HUD/edge sub-handlers (call order + short-circuit preserved) followed
 *     by the map-area fall-through (dismiss / view-mode-2 jump / army-select
 *     on LMB / terrain-info on RMB), gated on the turn handshake + the
 *     map-ready cursor gate (DAT_005533b0, produced by
 *     engine_camera_cursor_to_cell)
 *   - the matched TERRAIN-INFO screen (DAT_004eac50 == 4) case: any click
 *     (turn-busy / edge-scroll / generic-click / RMB) closes the popup
 *     (screen → 0 + FUN_0043cc56 cleanup) — the close half of the strategic
 *     RMB-open, so the port has the full open↔close pair
 *   - the ARMY-MOVE/MERGE HANDOFF screen (DAT_004eac50 == 0x10): edge-
 *     scroll, then a fresh LMB press (gated on the handoff cooldown)
 *     resolves the move/merge target via engine_army_move_confirm_
 *     click_resolve — see docs/plans/army-select-move.md's tier-5 table
 *   - the four COUNTY DETAIL PANEL screens (0x14 population / 0x15 tax /
 *     0x16 happiness / 0x19 rations — engine_county_panels.h): the shared
 *     HUD chain + any-click-closes shape of 42ff10.c:445-560, with the
 *     rations stepper (FUN_0043a379) slotted third on 0x19 only
 *   - the shared tail (right-panel tab strip + the screen-0x0f commit) and
 *     the tail edge latch.
 * The ~25 OTHER screen cases (battle sub-modes, market/report
 * sub-screens) are deferred — they belong to screens the port does not
 * render yet; each lands with its screen.  See the plan doc +
 * docs/findings/gameplay-and-screens.md for the full map.
 *
 * DESIGN.  The dispatcher is a state machine over many retail globals, and
 * its sub-handlers mutate the same state it reads.  Modeled as: a per-frame
 * INPUT snapshot (read-only), a MUTABLE state struct (the screen + redraw +
 * latches the dispatcher and its handlers read/write), and a HOOKS struct
 * of sub-handler / action callbacks.  The dispatcher calls the hooks in
 * retail's exact order with retail's short-circuit semantics; a NULL hook
 * is treated as "not consumed / no-op" (the skeleton's default for the
 * not-yet-ported handlers).  Pure + unit-testable: bind recording hooks to
 * assert call order + state transitions.  engine.c wires the live globals.
 */

#ifndef OPENLORDS2_ENGINE_INGAME_INPUT_H
#define OPENLORDS2_ENGINE_INGAME_INPUT_H

/* ── Region classifier — port of FUN_00432640 @ 0x432640 (262 B) ───────
 * Classifies the cursor into a coarse UI region while on a low screen
 * (screen < 8): 0 = map area, 1 = top bar (cursor_y < 0x18), 2 = right
 * panel (cursor in the panel rect).  Returns 0 for screen >= 8.  The
 * panel-Y test differs by game mode: mode 0 extends the top of the panel
 * band up by 0x18 (the campaign HUD), other modes use the plain rect.
 * Retail stores the result in _DAT_005534f0; consumed by the HUD button
 * sub-handlers (not yet ported), so inert in the skeleton but computed for
 * fidelity + future use. */
typedef struct engine_ingame_region_in {
    int screen;              /* DAT_004eac50 */
    int cursor_x, cursor_y;  /* DAT_004e65fc / DAT_004e65f4 */
    int game_mode;           /* DAT_0057c8fc */
    int panel_x, panel_w;    /* DAT_0056d698 / DAT_0056d694 */
    int panel_y, panel_h;    /* DAT_0056d69c / DAT_0056d674 */
} engine_ingame_region_in_t;

int engine_ingame_input_classify_region(const engine_ingame_region_in_t *in);

/* ── Dispatcher input snapshot (read-only within a dispatch) ──────────── */
typedef struct engine_ingame_input_in {
    engine_ingame_region_in_t region_in; /* prologue classify (FUN_00432640) */
    int rmb_released;     /* DAT_004e6900 — RMB release edge */
    int lmb_released;     /* DAT_004e65d8 — LMB release edge */
    int lmb_pressed;      /* DAT_004eafb4 — LMB press edge */
    int rmb_pressed;      /* DAT_004eabe0 — RMB press edge */
    int lmb_held;         /* DAT_004e65cc — LMB held */
    int extended_pause;   /* DAT_0053e910 — a modal scroll is open (sim frozen) */
    int map_ready;        /* DAT_005533b0 — cursor over a valid map tile */
    int view_mode;        /* DAT_0057cb18 — strategic view mode (2 = alt) */
    int game_mode;        /* DAT_0057c8fc — game-mode enum (0 = campaign) */
    int turn_progress;    /* DAT_00553fc8 — turn handshake 0/1/2 */
    int turn_busy;        /* DAT_0055403c — turn-processing busy flag */
    int timeout_reached;  /* DAT_00553018 — turn-timer expired */
    int handoff_cooldown; /* DAT_00553ecc — the screen-0x10 handoff's "not
                           * yet" gate (engine_army_select_state_t.
                           * handoff_cooldown, persisted by the caller);
                           * only read on the screen-0x10 arm. */
} engine_ingame_input_in_t;

/* ── Mutable engine state the dispatcher + its handlers read/write ─────
 * Initialise `screen`, `afa8`, `prev_cac4` from the live globals before a
 * dispatch; read back `screen` / `demand_redraw` / `empty_redraw_gate`
 * after.  The latch mirrors (latch_*, edge_4eabb4, region, hold_56d64c)
 * have no ported consumer yet — modeled for structural fidelity. */
typedef struct engine_ingame_state {
    int screen;            /* DAT_004eac50  (in/out) */
    int demand_redraw;     /* DAT_0057d340  (out; retail sets 1/2) */
    int afa8;              /* DAT_0052afa8  (in/out; "consume next RMB" latch) */
    int prev_cac4;         /* DAT_0057cac4  (in/out; reset to 0 in prologue) */
    int empty_redraw_gate; /* DAT_0053e9ac  (out; tail sets 2) */
    int hold_56d64c;       /* DAT_0056d64c  (out; cleared off the map) */
    int latch_afa0;        /* DAT_0052afa0  (prologue latch of prev_cac4) */
    int latch_565488;      /* DAT_00565488  (prologue 0; tail falling-edge) */
    int latch_5533a4;      /* DAT_005533a4  (prologue 8) */
    int edge_4eabb4;       /* DAT_004eabb4  (tail clears to 0) */
    int region;            /* _DAT_005534f0 (classify result) */
    int returned_early;    /* 1 iff the view-mode-2 bare-return path fired */
} engine_ingame_state_t;

/* ── Sub-handler / action hooks ────────────────────────────────────────
 * `consume` hooks return non-zero to consume the click (short-circuit the
 * rest of the chain); `action` hooks are void side effects.  Both receive
 * the input snapshot + the mutable state so a ported handler can change the
 * screen / request a redraw.  Any NULL pointer is treated as not-consumed /
 * no-op.  Each maps to the retail handler in the comment. */
typedef int  (*engine_ingame_consume_fn)(const engine_ingame_input_in_t *in,
                                         engine_ingame_state_t *st);
typedef void (*engine_ingame_action_fn)(const engine_ingame_input_in_t *in,
                                        engine_ingame_state_t *st);

typedef struct engine_ingame_hooks {
    engine_ingame_consume_fn dismiss_chip;       /* FUN_0047685d open-scroll dismiss */
    engine_ingame_consume_fn ui_router;          /* FUN_004ba9c8 dialog/UI router (0 in-game) */
    engine_ingame_consume_fn edge_scroll;        /* FUN_00432221 edge-scroll */
    engine_ingame_consume_fn minimap_drag;       /* FUN_00439079 minimap drag/rclick */
    engine_ingame_consume_fn hud_btn_a;          /* FUN_00432967 HUD button hit-test */
    engine_ingame_consume_fn kbd_table;          /* FUN_0040deca(&DAT_004dc428,3) */
    engine_ingame_consume_fn hud_btn_b;          /* FUN_0043292d HUD button hit-test */
    engine_ingame_consume_fn action_grid;        /* FUN_00438ceb 4-button action grid */
    engine_ingame_consume_fn rations_arrows;     /* FUN_0043a379 rations-panel stepper
                                                  * (screen 0x19 only, third in its
                                                  * chain — see the county-panel case) */
    engine_ingame_consume_fn scrollbar;          /* FUN_00439122 right-panel scrollbar */
    engine_ingame_consume_fn panel_toggles;      /* FUN_00438e3b right-panel toggles */
    engine_ingame_consume_fn tab_strip;          /* FUN_004323fe right-panel tab strip (tail) */
    engine_ingame_consume_fn generic_click;      /* FUN_0040e7e4 generic click-consumed test (screen 4) */
    engine_ingame_consume_fn ti_subhandlers;     /* FUN_00438990 / FUN_00438a91 / FUN_00437002
                                                  * (screen-4 sub-interactions, checked as a unit;
                                                  *  grouped here, all deferred — split when ported) */
    engine_ingame_action_fn  dismiss_on_map;     /* FUN_00476768 + FUN_0045270a */
    engine_ingame_action_fn  special_mode2_jump; /* FUN_004350a1 (view-mode-2 LMB jump) */
    engine_ingame_action_fn  army_select;        /* FUN_0043ce1a army select/move (map LMB) */
    engine_ingame_action_fn  terrain_info_open;  /* FUN_0043caf4 terrain-info open (map RMB) */
    engine_ingame_action_fn  terrain_info_close; /* FUN_0043cc56 terrain-info cleanup (screen-4 close) */
    engine_ingame_consume_fn labor_open;          /* FUN_0043a123 workforce-grid → labor
                                                    * detail open (screen 0x02); sets the
                                                    * task + flips to 0x0f when a task cell
                                                    * is hit */
    engine_ingame_action_fn  screen0f_commit;    /* FUN_00427d19 + FUN_0041438c (tail, screen 0xf) */
    engine_ingame_action_fn  army_move_click_resolve; /* FUN_004a9252 screen-0x10
                                                        * click resolve (map LMB,
                                                        * fresh press + cooldown
                                                        * elapsed) */
} engine_ingame_hooks_t;

/* Run one in-game input frame (retail FUN_0042ff10).  Reads `in`, mutates
 * `*st`, fires hooks from `*hooks` (NULL hooks treated as 0/no-op; pass
 * NULL for `hooks` to run the pure skeleton with every handler inert).
 * Void, like retail — outcomes are in `*st`. */
void engine_ingame_input_dispatch(const engine_ingame_input_in_t *in,
                                  engine_ingame_state_t *st,
                                  const engine_ingame_hooks_t *hooks);

#endif /* OPENLORDS2_ENGINE_INGAME_INPUT_H */
