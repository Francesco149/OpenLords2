/*
 * OpenLords2 — screen-0x10 army-move/merge confirm chain (the actual LIVE
 * trigger for the 0x11 event popup, and its 4 confirm-dialog siblings).
 *
 * Port of:
 *   FUN_00469e0e @ 0x469e0e  ( 72 B) → static leaf (attack-terrain gate)
 *   FUN_004a91ba @ 0x4a91ba  (152 B) → static leaf (path-cell overlay mark)
 *   FUN_004a8e0b @ 0x4a8e0b  (943 B) → engine_army_move_confirm_hover_tick
 *   FUN_004a9252 @ 0x4a9252  (288 B) → engine_army_move_confirm_click_resolve
 *   FUN_004a983b @ 0x4a983b  (195 B) → static callback (direct path + garrison-occupied, events 0x11e/0x113)
 *   FUN_004a960f @ 0x4a960f  (334 B) → static callback (empty-cell / new-claim, events 0x11d/0x11c)
 *   FUN_004a975d @ 0x4a975d  (222 B) → static callback (headcount-cap check, event 0x112)
 *   FUN_004a9372 @ 0x4a9372  (669 B) → static callback (garrison-merge-cap — THE 0x11 POPUP PRODUCER,
 *                                       also events 0xa4/0xa5/0xa7/0x11b/0x121)
 *   FUN_004a98fe @ 0x4a98fe  (412 B) → static engine_army_move_confirm_execute (merge/move EXECUTE primitive)
 *
 * See docs/plans/army-select-move.md for the full call-graph closure this
 * was scoped from.
 *
 * ── The two-tick split ──────────────────────────────────────────────────
 * Retail runs FUN_004a8e0b from the per-frame RENDER dispatcher
 * (FUN_004ba26e's screen-0x10 arm) and FUN_004a9252 from the per-frame
 * INPUT dispatcher (FUN_0042ff10's screen-0x10 arm, on a fresh LMB press)
 * — i.e. every frame the cursor is classified, and a click resolves
 * whatever the LAST classification left behind.  This module mirrors that
 * split: call engine_army_move_confirm_hover_tick() once per render tick
 * while screen == 0x10, and engine_army_move_confirm_click_resolve() once
 * per input tick's fresh-LMB-press case.
 *
 * ── Why the 4 siblings are bare `void(void)` callbacks ─────────────────
 * All 5 functions (the resolver + 4 siblings) share retail's identical
 * shape: `if (DAT_0059151c) { ...execute or post an event... }`.  The
 * resolver either (a) calls a sibling DIRECTLY with DAT_0059151c already
 * forced to 1 (no confirmation needed), or (b) opens the generic Yes/No
 * dialog (engine_confirm_dialog_open) with that SAME sibling function as
 * the callback — fired later, with DAT_0059151c set to whichever button
 * the user clicked.  So each sibling is written ONCE, as a zero-arg
 * function matching engine_confirm_dialog_callback_fn exactly, and reads
 * engine_confirm_dialog_answer() itself — exactly mirroring retail
 * (FUN_0043eda0's `DAT_0059151c` reads), rather than inventing a
 * boolean-parameter signature retail doesn't have.  The direct-call path
 * uses engine_confirm_dialog_force_answer(1) to set the SAME flag
 * retail's FUN_004a9252 sets unconditionally before branching.
 *
 * ── The async screen-reset poll ─────────────────────────────────────────
 * Every sibling's retail body starts with `DAT_004eac50 = DAT_0057c90c;`
 * (screen restore) — since every path through this subsystem stashes 0
 * there (the master dispatcher always resets screen to 0 before calling
 * FUN_004a9252), the observable effect is always "screen ends up at 0."
 * For the DIRECT path this is synchronous — engine_army_move_confirm_
 * click_resolve's own out-param reflects it immediately.  For the
 * DIALOG path the sibling fires many frames later (see engine_confirm_
 * dialog.h's press-animation note), so it can't flow through click_
 * resolve's original call; it's staged instead and drained via
 * engine_army_move_confirm_poll_screen_reset() (same shape as engine_
 * army_select_poll_confirm_result()).
 */
#ifndef OPENLORDS2_ENGINE_ARMY_MOVE_CONFIRM_H
#define OPENLORDS2_ENGINE_ARMY_MOVE_CONFIRM_H

#include <stdint.h>

#include "engine_event_queue.h"

/* ── Hover classifier (render-tick) ──────────────────────────────────── */

typedef struct engine_army_move_hover_in {
    int hover_valid;        /* DAT_00568974 < 0xfff0000 (cursor parked off
                             * the visible map when false) */
    int hover_cell;         /* DAT_0057a0e0 — the hovered cell index
                             * (0..4095), i.e. DAT_00568974 / tile_stride */
    int local_player;       /* DAT_0057c8cc */
    int armed_county;       /* DAT_0057c8c4 — the county the screen-0x10
                             * handoff was armed on (engine_army_select_
                             * state_t.armed_county) */
    int tile_stride;        /* DAT_0057d338 */
    int map_width;          /* DAT_0052afd4 */
} engine_army_move_hover_in_t;

typedef struct engine_army_move_hover_state {
    int handoff_cooldown;   /* DAT_00553ecc, in/out (decremented here each
                             * tick — same persisted value engine_army_
                             * select_state_t.handoff_cooldown threads) */
} engine_army_move_hover_state_t;

/* Port of FUN_004a8e0b (+ FUN_004a91ba, FUN_00469e0e).  Classifies
 * whatever is under the cursor into six scratch flags the click resolver
 * below reads, and arms it (sets the internal DAT_0057d358 flag) only
 * when a fresh path trace from the armed county to the hovered cell
 * succeeds.  Reads the ALREADY-FLOODED path field (engine_path_field_
 * cost/fill — engine_army_select_open_handoff floods it when arming), no
 * additional binding needed. */
void engine_army_move_confirm_hover_tick(const engine_army_move_hover_in_t *in,
                                         engine_army_move_hover_state_t *st);

/* ── Click resolver (input-tick) ─────────────────────────────────────── */

typedef struct engine_army_move_click_state {
    int screen;          /* out — always 0 after a sibling actually runs
                          * (retail's DAT_0057c90c restore, always 0 here
                          * — see file header); untouched if the hover
                          * tick hadn't armed anything this frame. */
    int demand_redraw;   /* out */
} engine_army_move_click_state_t;

/* Port of FUN_004a9252.  Call on a fresh LMB press while screen == 0x10
 * (mirrors the master dispatcher's own gate: turn-idle + handoff_cooldown
 * < 1 + edge_scroll didn't consume it — see docs/plans/army-select-
 * move.md's tier-5 table).  `armed_county` is the SAME value passed to
 * the hover tick (stable for the whole screen-0x10 session, so passed to
 * both rather than threaded internally).  No-op unless the hover tick
 * armed a classification this frame. */
void engine_army_move_confirm_click_resolve(int local_player, int armed_county,
                                            engine_army_move_click_state_t *st,
                                            engine_event_queue_state_t *evq);

/* Drain a screen-0 transition staged by a confirm-dialog callback that
 * fired on a LATER frame than click_resolve's own call (see file header).
 * Returns 1 (and *out_demand_redraw) if one is pending, else 0. */
int engine_army_move_confirm_poll_screen_reset(int *out_demand_redraw);

#endif /* OPENLORDS2_ENGINE_ARMY_MOVE_CONFIRM_H */
