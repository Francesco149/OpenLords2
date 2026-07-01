/*
 * OpenLords2 — strategic-map army-select / move dispatcher (map LMB on an
 * owned army or county, screen 0).  Port of:
 *
 *   FUN_004a8426 @ 0x4a8426  (148 B) → engine_army_select_territory_link_check
 *   FUN_00448648 @ 0x448648  ( 97 B) → static leaf in the .c
 *   FUN_0043d309 @ 0x43d309  (675 B) → engine_army_select_assign_task
 *   FUN_0046f68e @ 0x46f68e  (114 B) → static leaf in the .c
 *   FUN_004a8d9c @ 0x4a8d9c  (111 B) → static leaf in the .c
 *   FUN_0043723a @ 0x43723a  (245 B) → engine_army_select_open_handoff
 *   FUN_004371ce @ 0x4371ce  (108 B) → engine_army_select_confirm_or_open
 *   FUN_0043ce1a @ 0x43ce1a (1263 B) → engine_army_select_dispatch
 *
 * See docs/plans/army-select-move.md for the full call-graph closure this
 * was scoped from (32 functions across 4 modules).
 *
 * Like engine_realm.c / engine_combat.c (and unlike engine_territory.c /
 * engine_terrain_info.c's bind-a-pointer style), this module reads/writes
 * the per-army and per-territory record arrays DIRECTLY via the shared
 * externs (g_per_army_768, g_per_territory_420 — save.h) through local
 * ARMY_REC/TERR_REC macros, because engine_army_select_dispatch's own
 * retail body re-reads a territory's link byte AFTER a call that may have
 * just mutated it (engine_army_select_territory_link_check) — a snapshot-
 * via-input-struct can't express that; direct access can, and matches
 * the majority convention for turn/gameplay-logic modules in this port.
 *
 * `engine_army_select_dispatch` is the `army_select` action hook
 * (engine_ingame_input.h) — fires on a map LMB release while idle.  The
 * caller supplies a FRESH terrain-compute snapshot each call (mirrors
 * engine.c's ti_compute — retail re-resolves on every click, independent
 * of whatever terrain-info's own resolve last saw).
 *
 * DEFERRED in this port: the workforce icon-grid overlay (FUN_00412143,
 * retail's `else if (...) { ...; FUN_00412143(1); }` branch) — modeled as
 * a hook (NULL-able) pending a render-parity pass; see
 * engine_workforce_hud.h for the scoped-but-unwired render port.  The
 * Yes/No confirm-dialog opener (FUN_0040e6f2) is ALSO deferred — see
 * engine_army_select_confirm_or_open's doc.
 */

#ifndef OPENLORDS2_ENGINE_ARMY_SELECT_H
#define OPENLORDS2_ENGINE_ARMY_SELECT_H

#include <stdint.h>

#include "engine_event_queue.h"
#include "engine_dirty_rect.h"

/* Port of FUN_004a8426.  Sanity-check territory `county_key`'s army-link
 * byte (TERR_REC[+0x199]): if the linked army (the byte's value, used as
 * an army key) has no target territory (ARMY_REC[+0x1bc] == 0) OR that
 * target territory's own back-reference (TERR_REC[+0x198]) doesn't point
 * to the SAME army, clear the link.  Bumps a shared tick counter
 * (DAT_0052afac, no known reader) unconditionally.  +0x199 is also
 * documented elsewhere (engine_combat.h) as a "besieged-region byte" —
 * ported faithfully either way; semantic overlap (if real) is a question
 * for a future r2 trace, not a blocker (mirrors the 0x12 popup's resolved
 * +0x1c0/+0x1c1 dual-use worry — see engine-quirks #210). */
void engine_army_select_territory_link_check(int county_key);

/* Port of FUN_0043d309 (+ leaf FUN_00448648).  Toggle army `army_key`'s
 * workforce assignment for `task` (0..4): task 4 is the GARRISON toggle
 * (ARMY_REC[+0x1bb]); tasks 0..3 toggle one of four 0x18-strided task-flag
 * bytes (ARMY_REC[+0x297 + task*0x18]) and map to workforce slots
 * {0→6, 1→4, 2→7, 3→5} (task 4 → slot 3) — the job-slot
 * numbering engine_army_ai_rebalance_workforce uses; exact real-world
 * labels (lumberjack/farmer/quarry/...) are unconfirmed, ported by
 * numeric slot per retail.  Re-plans the army's economy 3x (provision +
 * plan_realm, sandwiching a workforce-allocate + stamp-cell pass) and
 * notifies every OTHER army of the same owner sharing the toggled task
 * class (FUN_00448648 → engine_army_economy_plan_resource(slot=2, rec_idx=7,
 * target=0xf, divisor=4) per matching army).  Posts event code
 * `task*2 + new_flag_state + 0xe6` (task 4 instead posts -1/-2 in that
 * slot, i.e. 0xe5/0xe4) when the army belongs to the local player. */
void engine_army_select_assign_task(int army_key, int task,
                                    int local_player,
                                    int season_event_mode, int season_map,
                                    engine_event_queue_state_t *evq);

/* Port of FUN_0043723a.  The "arm the screen-0x10 move/merge handoff" step.
 * `ready` (retail DAT_0059151c, a confirm-dialog-system "armed" flag with
 * no producer in this session's call graph — see docs/plans/army-select-
 * move.md) gates the real handoff: 0 just resets *out_screen to 0 (the
 * conservative default until the confirm-dialog click system is traced/
 * wired); nonzero rebuilds the strategic move-cost field, arms screen
 * 0x10 on `county_key` (TERR_REC[county_key]) with a 0x28-tick cooldown
 * (*out_handoff_cooldown — the gate FUN_0042ff10's screen-0x10 case checks
 * before calling FUN_004a9252, so the SAME click that armed the handoff
 * can't also resolve as the destination click), clears the path overlay
 * AND the territory's +0x14e byte, floods a fresh path field from the
 * territory's (+0x0a,+0x0b) position, and re-derives a shared cell-index/
 * col/row scratch from its +0x0c cell offset (out params; no traced
 * consumer in this scope beyond the values themselves). */
/* `*out_armed_county` is set to `county_key` whenever `ready` actually
 * arms the handoff (retail DAT_0057c8c4 = DAT_0052afbc) — the screen-0x10
 * hover classifier / click resolver (engine_army_move_confirm.h) read
 * this back across later frames to know which county the pending move
 * targets.  Left untouched on the not-ready path (retail doesn't write
 * DAT_0057c8c4 there either). */
void engine_army_select_open_handoff(int county_key, int ready,
                                     int local_player, int tile_stride,
                                     int map_width,
                                     int *out_screen, int *out_demand_redraw,
                                     int *out_handoff_cooldown,
                                     int *out_cell_col, int *out_cell_row,
                                     int *out_armed_county);

/* Port of FUN_004371ce.  The county-click "no pending link" branch: marks
 * a below-status repaint (FUN_0043cc56 → engine_dirty_rect_mark_below_
 * status_bar, NULL-safe), then reads TERR_REC[county_key][+0x199] itself
 * (direct access — see file header).  No link → opens the handoff
 * DIRECTLY with ready FORCED to 1 (retail sets DAT_0059151c = 1
 * unconditionally just before this branch, and nothing resets it before
 * FUN_0043723a runs synchronously in the same call — so "ready" is not an
 * external unknown on this path).  Has a link → opens a Yes/No confirm
 * dialog (engine_confirm_dialog_open, msg_id 0xd) with a callback that
 * re-invokes engine_army_select_open_handoff reading the user's answer as
 * `ready` — mirrors retail's FUN_0040e6f2(0xd,0xa0,0xa0,FUN_0043723a)
 * exactly.  Because the callback can fire many frames later (after the
 * confirm dialog's press-animation settles), its outcome can't flow back
 * through THIS call's out-params — it's staged instead and must be
 * drained via engine_army_select_poll_confirm_result() on a later tick
 * (see that function's doc).  Returns 1 if the handoff fired directly
 * (out-params valid immediately), 0 if a confirm dialog was opened
 * instead (out-params still set to the dialog's screen/redraw; the
 * REAL handoff outcome arrives later via the poll function). */
int engine_army_select_confirm_or_open(int county_key,
                                       int local_player, int tile_stride,
                                       int map_width,
                                       int *out_screen, int *out_demand_redraw,
                                       int *out_handoff_cooldown,
                                       int *out_cell_col, int *out_cell_row,
                                       int *out_armed_county,
                                       engine_dirty_rect_state_t *dirty);

/* Drain a confirm-dialog result staged by the callback registered in
 * engine_army_select_confirm_or_open (see its doc).  Call once per input
 * tick (e.g. right after the ui_router hook fires) while a confirm
 * dialog might be resolving.  Returns 1 and fills the out-params if a
 * result is pending (clearing the pending flag); returns 0 (out-params
 * untouched) otherwise.  The caller is expected to apply *out_screen /
 * *out_demand_redraw to its dispatch state and persist *out_handoff_
 * cooldown / *out_armed_county the same way it does for the direct-open
 * path (mirrors engine.c's g_army_select_handoff_cooldown convention). */
int engine_army_select_poll_confirm_result(int *out_screen,
                                           int *out_demand_redraw,
                                           int *out_handoff_cooldown,
                                           int *out_armed_county);

/* ── Main dispatcher — FUN_0043ce1a ───────────────────────────────────── */

typedef struct engine_army_select_in {
    int game_mode;          /* DAT_0057c8fc (s_slot) — only fires when == 0 */
    int turn_progress;      /* DAT_00553fc8 */
    int turn_busy;          /* DAT_0055403c */
    int timeout_reached;    /* DAT_00553018 */
    int game_active;        /* DAT_0052afa4 — no producer wired to this
                             * consumer yet (mirrors ti_open's same gap);
                             * default 0.  Retail also clears this to 0 on
                             * the branch it gates — moot while it's always
                             * 0 in, so not modeled as an output. */
    int view_mode;          /* DAT_0057cb18 (s_slot) — only fires when != 2 */
    int extended_pause;     /* DAT_0053e910 */
    int local_player;       /* DAT_0057c8cc (s_slot) */
    int popup_type;         /* DAT_005440a4 / g_popup_type — paused-click
                             * dismiss keep-list check (FUN_00476710) */
    int tile_stride;        /* DAT_0057d338 */
    int map_width;          /* DAT_0052afd4 */
    int season_event_mode;  /* DAT_0057c934 — fed to assign_task's provision call */
    int season_map;         /* DAT_0057c92c — fed to assign_task's plan_realm calls */
    uint32_t now_ms;         /* timeGetTime()-equivalent, fed to engine_map_scene_tick */

    /* Terrain-compute output for the clicked tile (engine_map_query_
     * terrain_info, called by the caller exactly like ti_compute does —
     * see engine_map_query.h for field provenance). */
    int head_link;           /* DAT_005681e0 — army/head-link at this cell */
    int county_id;           /* DAT_0052afbc */
    int terrain_type;        /* DAT_0057cb2c — drives the auto task pick */
    int flags;                /* _DAT_005440b0 */
} engine_army_select_in_t;

typedef struct engine_army_select_state {
    int screen;              /* DAT_004eac50 (in/out) */
    int demand_redraw;       /* DAT_0057d340 (out) */
    int hold_pending;        /* DAT_0056d64c (in/out — dispatcher's
                              * st->hold_56d64c; a "swallow one click" cookie:
                              * in!=0 consumes the click and clears itself,
                              * out 0 always once consumed) */
    int hud_outcome;          /* DAT_00553c64 (out — screen-8 path only) */
    int handoff_cooldown;     /* DAT_00553ecc (out — set by the screen-0x10
                              * handoff; the gate FUN_0042ff10's screen-0x10
                              * case checks before calling FUN_004a9252) */
    int armed_county;         /* DAT_0057c8c4 (in/out, like handoff_cooldown
                              * — the county key the screen-0x10 handoff
                              * was armed on; only written when this call
                              * actually arms/re-arms a handoff, so the
                              * caller must seed it from its own persisted
                              * copy and it passes through unchanged
                              * otherwise) */
} engine_army_select_state_t;

typedef void (*engine_army_select_workforce_render_fn)(int army_key);
typedef void (*engine_army_select_dismiss_fn)(void);
typedef void (*engine_army_select_map_tick_fn)(uint32_t now_ms);

typedef struct engine_army_select_hooks {
    /* FUN_00412143(1) — the workforce icon-grid overlay.  DEFERRED (see
     * engine_workforce_hud.h); NULL = inert (matches every other
     * not-yet-wired hook in this codebase). */
    engine_army_select_workforce_render_fn workforce_render;
    /* FUN_00476768 (engine_popup_scroll_dismiss) — Win32-glue-layer
     * function this pure module can't call directly; NULL = inert. */
    engine_army_select_dismiss_fn dismiss_on_paused_click;
    /* FUN_004050c0 (engine_map_scene_tick) — unlike engine_territory_
     * recenter_on_tile (which already no-ops on unbound state), this
     * triggers a FULL unguarded render pass (engine_map_scene_render_body
     * → engine_map_terrain_frame, which dereferences its tile array with
     * no NULL check) — wrong to call unconditionally from a logic module
     * a host test should be able to exercise without standing up the
     * whole render pipeline.  NULL = inert. */
    engine_army_select_map_tick_fn map_scene_tick;
} engine_army_select_hooks_t;

/* Run one army-select dispatch.  Pure aside from the direct ARMY_REC/
 * TERR_REC access (see file header), the `evq` event-post side channel,
 * and the optional workforce_render hook. */
void engine_army_select_dispatch(const engine_army_select_in_t *in,
                                 engine_army_select_state_t *st,
                                 const engine_army_select_hooks_t *hooks,
                                 engine_event_queue_state_t *evq);

#endif /* OPENLORDS2_ENGINE_ARMY_SELECT_H */
