/*
 * OpenLords2 — in-game (strategic) input dispatcher.
 *
 * Port of FUN_0042ff10 @ 0x42ff10 (the strategic + terrain-info + army-move
 * handoff screens) and FUN_00432640 @ 0x432640 (the cursor region
 * classifier).  See engine_ingame_input.h for the design + scope notes, and
 * docs/plans/ingame-input-handler.md / army-select-move.md for the frontier
 * plans.
 *
 * Branch shape + sub-handler call order are verbatim from the decompile
 * (docs/decompiled/by-address/42ff10.c lines 23-63 strategic, 174-200
 * terrain-info, 553-572 army-move handoff, 935-957 the shared tail + edge
 * latch).
 */

#include "engine_ingame_input.h"

#include "call_trace.h"

int engine_ingame_input_classify_region(const engine_ingame_region_in_t *in)
{
    CALL_TRACE_ENTER(0x00432640);
    /* Retail writes _DAT_005534f0 only for screen < 8 (leaving the prior
     * value stale otherwise).  No ported consumer reads it yet, so we just
     * return 0 for screen >= 8 — immaterial to behavior. */
    int region = 0;
    if (in->screen < 8) {
        if (in->cursor_y < 0x18) {
            region = 1;                      /* top bar */
        } else if (in->panel_x <= in->cursor_x &&
                   in->cursor_x < in->panel_w + in->panel_x) {
            if (in->game_mode == 0) {
                /* campaign HUD: the panel band starts 0x18 px higher */
                if (in->panel_y - 0x18 <= in->cursor_y &&
                    in->cursor_y < in->panel_h + in->panel_y) {
                    region = 2;              /* right panel */
                }
            } else if (in->panel_y <= in->cursor_y &&
                       in->cursor_y < in->panel_h + in->panel_y) {
                region = 2;
            }
        }
    }
    return region;
}

/* NULL-safe hook callers (a NULL hook = handler not yet ported). */
static int call_consume(engine_ingame_consume_fn fn,
                        const engine_ingame_input_in_t *in,
                        engine_ingame_state_t *st)
{
    return fn ? fn(in, st) : 0;
}

static void call_action(engine_ingame_action_fn fn,
                        const engine_ingame_input_in_t *in,
                        engine_ingame_state_t *st)
{
    if (fn) {
        fn(in, st);
    }
}

void engine_ingame_input_dispatch(const engine_ingame_input_in_t *in,
                                  engine_ingame_state_t *st,
                                  const engine_ingame_hooks_t *hooks)
{
    CALL_TRACE_ENTER(0x0042ff10);
    static const engine_ingame_hooks_t empty_hooks = {0};
    if (!hooks) {
        hooks = &empty_hooks;
    }

    /* ── Prologue latches ──
     * DAT_0052afa0 = DAT_0057cac4; DAT_0057cac4 = 0;
     * DAT_00565488 = 0; DAT_005533a4 = 8; */
    st->latch_afa0     = st->prev_cac4;
    st->prev_cac4      = 0;
    st->latch_565488   = 0;
    st->latch_5533a4   = 8;
    st->returned_early = 0;

    /* Region classify (FUN_00432640, prologue). */
    st->region = engine_ingame_input_classify_region(&in->region_in);

    /* Turn handshake idle gate (the strategic block runs only while idle;
     * the terrain-info close fires when busy):
     *   idle ⇔ turn_progress == 0 && (turn_busy == 0 || timeout_reached). */
    int turn_idle = (in->turn_progress == 0 &&
                     (in->turn_busy == 0 || in->timeout_reached != 0));

    /* Dismiss chip FIRST, then the UI router — either consuming skips the
     * whole per-screen body straight to the tail edge latch. */
    if (call_consume(hooks->dismiss_chip, in, st) == 0 &&
        call_consume(hooks->ui_router, in, st) == 0) {

        /* DAT_0052afa8 (afa8) + RMB-released → swallow the RMB this frame. */
        if (st->afa8 == 0 || in->rmb_released == 0) {

            /* `consumed` marks retail's `goto LAB_00431f25` (jump past the
             * shared tail block to the edge latch). */
            int consumed = 0;

            if (st->screen == 0) {
                /* ───────── STRATEGIC SCREEN (DAT_004eac50 == 0) ───────── */
                if (call_consume(hooks->edge_scroll, in, st) != 0 ||
                    call_consume(hooks->minimap_drag, in, st) != 0 ||
                    call_consume(hooks->hud_btn_a, in, st) != 0) {
                    consumed = 1;
                } else if (turn_idle) {
                    if (call_consume(hooks->kbd_table, in, st) != 0 ||
                        call_consume(hooks->hud_btn_b, in, st) != 0 ||
                        call_consume(hooks->action_grid, in, st) != 0 ||
                        call_consume(hooks->scrollbar, in, st) != 0 ||
                        call_consume(hooks->panel_toggles, in, st) != 0) {
                        consumed = 1;
                    } else if (in->extended_pause != 0 && in->rmb_released != 0) {
                        /* open scroll + RMB anywhere → dismiss it */
                        call_action(hooks->dismiss_on_map, in, st);
                        consumed = 1;
                    } else if (in->map_ready != 0 && in->lmb_pressed != 0 &&
                               in->view_mode == 2) {
                        /* view-mode-2 LMB over map → jump to screen 0.
                         * Retail BARE-RETURNS here (skips the tail latch). */
                        st->screen = 0;
                        call_action(hooks->special_mode2_jump, in, st);
                        st->returned_early = 1;
                        return;
                    } else if (in->map_ready != 0 && in->lmb_released != 0) {
                        /* map LMB → army select / move */
                        call_action(hooks->army_select, in, st);
                        consumed = 1;
                    } else if (in->map_ready != 0 && in->rmb_released != 0) {
                        /* map RMB → terrain-info ("Here Be Dragons") */
                        st->screen = 4;
                        call_action(hooks->terrain_info_open, in, st);
                        consumed = 1;
                    } else if (in->map_ready == 0 || in->lmb_held == 0) {
                        st->hold_56d64c = 0;
                    }
                }
            } else if (st->screen == 4) {
                /* ───────── TERRAIN-INFO SCREEN (DAT_004eac50 == 4) ─────────
                 * Any click closes the popup (screen → 0 + cleanup). */
                if (!turn_idle) {
                    st->screen = 0;
                    call_action(hooks->terrain_info_close, in, st);
                    consumed = 1;
                } else if (call_consume(hooks->edge_scroll, in, st) != 0) {
                    st->screen = 0;
                    call_action(hooks->terrain_info_close, in, st);
                    consumed = 1;
                } else if (call_consume(hooks->generic_click, in, st) != 0) {
                    st->screen = 0;
                    call_action(hooks->terrain_info_close, in, st);
                    consumed = 1;
                } else if (call_consume(hooks->ti_subhandlers, in, st) != 0) {
                    /* sub-interaction consumed without closing */
                    consumed = 1;
                } else if (in->rmb_released != 0) {
                    st->screen = 0;
                    call_action(hooks->terrain_info_close, in, st);
                    consumed = 1;
                }
            } else if (st->screen == 0x10) {
                /* ───────── ARMY-MOVE/MERGE HANDOFF SCREEN (DAT_004eac50 == 0x10) ─────────
                 * Armed by army_select's re-click handoff (engine_army_
                 * select_open_handoff); the SECOND click here resolves
                 * the move/merge target (engine_army_move_confirm_
                 * click_resolve).  No HUD sub-handlers on this screen —
                 * just edge-scroll, then the fresh-click resolve gated by
                 * the handoff cooldown (so the SAME click that armed the
                 * handoff can't also resolve as the destination click). */
                if (!turn_idle) {
                    st->screen = 0;
                    st->demand_redraw = 2;
                    consumed = 1;
                } else if (call_consume(hooks->edge_scroll, in, st) != 0) {
                    consumed = 1;
                } else if (in->lmb_pressed != 0 && in->handoff_cooldown < 1) {
                    st->screen = 0;
                    st->hold_56d64c = 1;
                    call_action(hooks->army_move_click_resolve, in, st);
                    consumed = 1;
                } else if (in->rmb_released != 0) {
                    st->screen = 0;
                    st->demand_redraw = 2;
                    consumed = 1;
                }
            } else if (st->screen == 0x14 || st->screen == 0x15 ||
                       st->screen == 0x16 || st->screen == 0x19) {
                /* ───────── COUNTY DETAIL PANELS (population 0x14 / tax
                 * 0x15 / happiness 0x16 / rations 0x19) ─────────
                 * One shared shape (42ff10.c:445-560): while turn-idle,
                 * run hud_btn_b → hud_btn_a → [0x19 only: the rations
                 * stepper FUN_0043a379] → action-grid → scrollbar →
                 * panel-toggles → minimap-drag; if none consume, ANY
                 * click closes back to the strategic screen (RMB
                 * directly; LMB via the generic-click test).  Turn-busy
                 * closes too.  NOTE the retail case bodies end in
                 * `goto LAB_00431f25` unconditionally — the shared
                 * tab-strip tail below never runs on these screens,
                 * hence `consumed = 1` on every path. */
                consumed = 1;
                if (!turn_idle) {
                    st->screen = 0;
                    st->demand_redraw = 2;
                } else if (call_consume(hooks->hud_btn_b, in, st) == 0 &&
                           call_consume(hooks->hud_btn_a, in, st) == 0 &&
                           (st->screen != 0x19 ||
                            call_consume(hooks->rations_arrows, in, st) == 0) &&
                           call_consume(hooks->action_grid, in, st) == 0 &&
                           call_consume(hooks->scrollbar, in, st) == 0 &&
                           call_consume(hooks->panel_toggles, in, st) == 0 &&
                           call_consume(hooks->minimap_drag, in, st) == 0) {
                    if (in->rmb_released != 0) {
                        st->screen = 0;
                        st->demand_redraw = 2;
                    } else if (call_consume(hooks->generic_click, in, st) != 0) {
                        st->screen = 0;
                        st->demand_redraw = 2;
                    }
                }
            } else if (st->screen == 2) {
                /* ───────── WORKFORCE ICON GRID (DAT_004eac50 == 2) ─────────
                 * 42ff10.c:65-113.  Turn-busy closes to the map; else run the
                 * HUD sub-handlers, then a seal click (generic_click) or RMB
                 * closes the grid, and a task-cell LMB opens the labor detail
                 * (labor_open → screen 0x0f).  FUN_004393eb (→ screen 5) and
                 * FUN_00439df0 are deferred.  If nothing consumes, the click
                 * falls through to the shared tail (retail's DAT_004e65f0=1
                 * "unconsumed" latch). */
                if (!turn_idle) {
                    st->screen = 0;
                    st->demand_redraw = 2;
                    consumed = 1;
                } else if (call_consume(hooks->hud_btn_b, in, st) != 0 ||
                           call_consume(hooks->hud_btn_a, in, st) != 0 ||
                           call_consume(hooks->action_grid, in, st) != 0 ||
                           call_consume(hooks->scrollbar, in, st) != 0 ||
                           call_consume(hooks->panel_toggles, in, st) != 0 ||
                           call_consume(hooks->minimap_drag, in, st) != 0) {
                    consumed = 1;
                } else if (call_consume(hooks->generic_click, in, st) != 0) {
                    st->screen = 0;              /* seal click → close grid */
                    st->demand_redraw = 2;
                    consumed = 1;
                } else if (call_consume(hooks->labor_open, in, st) != 0) {
                    st->demand_redraw = 2;       /* task cell → 0x0f (set in hook) */
                    consumed = 1;
                } else if (in->rmb_released != 0) {
                    st->screen = 0;              /* RMB → close grid */
                    st->demand_redraw = 2;
                    consumed = 1;
                }
            } else if (st->screen == 0x0f) {
                /* ───────── LABOR DETAIL (DAT_004eac50 == 0x0f) ─────────
                 * 42ff10.c:114-128.  A seal click (generic_click) or RMB
                 * closes back to the workforce grid (the came-from flag
                 * DAT_005533f4 is always 0 for the only opener, screen 2),
                 * then the commit tail (FUN_00427d19 + FUN_0041438c border
                 * repaint). */
                if (call_consume(hooks->generic_click, in, st) != 0 ||
                    in->rmb_released != 0) {
                    st->screen = 2;
                    st->demand_redraw = 2;
                    call_action(hooks->screen0f_commit, in, st);
                    consumed = 1;
                }
            } else if (st->screen == 8) {
                /* ───────── MERCHANT county-interior (DAT_004eac50 == 8) ─────────
                 * 42ff10.c:222.  The DEFAULT county-interior view (opened by
                 * army_select's castle click).  Turn-busy, or any seal
                 * (generic_click) / RMB click, closes back to the strategic
                 * map.  The county-interior TAB switches (→ workforce grid 2,
                 * armory 0x0a, …) are handled earlier by the widget router
                 * (FUN_004ba9c8), not in this screen case.
                 *
                 * Retail also reloads the workforce-grid buffer on close
                 * (vill_gd8.pl8 → DAT_00542ce0, three turn-state variants) —
                 * DEFERRED: the port's workforce grid reads the baked region
                 * grid, and the merchant trade state isn't ported yet. */
                if (!turn_idle) {
                    st->screen = 0;
                    st->demand_redraw = 1;
                    consumed = 1;
                } else if (call_consume(hooks->generic_click, in, st) != 0) {
                    st->screen = 0;
                    st->demand_redraw = 1;
                    consumed = 1;
                } else if (in->rmb_released != 0) {
                    st->screen = 0;
                    st->demand_redraw = 1;
                    consumed = 1;
                }
            }
            /* (other screen cases deferred — see header) */

            /* ── Shared tail: right-panel tab strip ──
             * Runs on a button press edge for any screen but 0x12, only
             * when no screen case consumed (retail reaches it by NOT
             * having taken a `goto`). */
            if (!consumed) {
                if ((in->lmb_pressed != 0 || in->rmb_pressed != 0) &&
                    st->screen != 0x12 &&
                    call_consume(hooks->tab_strip, in, st) != 0) {
                    if (st->screen == 0x0f) {
                        call_action(hooks->screen0f_commit, in, st);
                    }
                    if (in->game_mode == 0) {
                        st->screen = 0;
                    }
                    st->empty_redraw_gate = 2;
                }
            }
        } else {
            /* afa8 latch consumes this RMB + requests a redraw. */
            st->afa8          = 0;
            st->demand_redraw = 2;
        }
    }

    /* ── Tail edge latch (LAB_00431f25) ──
     * Falling-edge detect on DAT_0057cac4 (reset to 0 in the prologue;
     * latch_afa0 holds its entry value).  Inert until a producer drives
     * DAT_0057cac4 in the port. */
    if (st->prev_cac4 != st->latch_afa0 && st->latch_afa0 == 1) {
        st->latch_565488 = 1;
    }
    st->edge_4eabb4 = 0;
}
