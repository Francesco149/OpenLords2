/*
 * OpenLords2 — player standings / ranking recompute.
 *
 *   FUN_0049aa0e @ 0x49aa0e (2197 B) → engine_ranking_recompute
 *
 * Recomputes the whole-game standings from the 6 per-player strategic
 * records (`g_player_strategic_352`, retail 0x57bf00, stride 0x160).  It
 * is NOT init-only: five callers (new-game init 0x497ced, plus
 * 0x49b42b / 0x49cebe / 0x49a010 / 0x435211 — turn-end / event paths),
 * so it lives in its own module rather than game_init.c.
 *
 * In one pass it produces five kinds of output:
 *
 *   1. Per-metric leaders — for each of 8 strategic metrics, the max
 *      value and the slot holding it, into a contiguous block of
 *      save-schema scalars (entries 153..168), plus an active-player
 *      count that excludes the local player (entry 124).
 *
 *   2. Per-player composite score — a weighted sum written to record
 *      field +0x50 of each active player (see the weight table in the
 *      .c).  Reset to 0 for every slot, summed only for active ones.
 *
 *   3. A 5-entry {slot, score} leaderboard (`s_slot_00565410`, entry
 *      150, laid out as 6×8 with a trailing zero sentinel) sorted
 *      descending by score with a bubble pass.
 *
 *   4. Per-player rank — record field +0x2b ← 1-based finishing
 *      position; inactive slots get rank 0 and drop out of the board.
 *
 *   5. Winner / last-place detection (entries 151 / 152) and, when only
 *      one ranked player remains, a victory / notification event posted
 *      to the event queue.
 *
 * The victory event is the function's only side effect outside the
 * save-state globals, so the event-queue state is threaded in as a
 * pointer (the producer `engine_event_queue_post`, retail FUN_00472bc5).
 * At new-game with multiple players the winner != last-place, so the
 * event branch does not fire; it fires later when the field narrows to
 * one survivor.
 *
 * Reference: docs/findings/game-start.md §"FUN_0049aa0e"; the per-player
 * record field map lives in the .c.
 */

#ifndef OPENLORDS2_ENGINE_RANKING_H
#define OPENLORDS2_ENGINE_RANKING_H

#include "engine_event_queue.h"   /* engine_event_queue_state_t, _post */

/* Recompute the standings (see file header).  `evq` is the live event
 * queue — only touched on the victory branch.  Reads/writes the
 * per-player strategic table and the leaderboard save scalars directly;
 * pure given those globals + `evq`. */
void engine_ranking_recompute(engine_event_queue_state_t *evq);

/* Recompute one player's cashflow total (strategic record +0x138) as the
 * sum of its six i32 category sub-accounts (+0x140..+0x154).  Shared by
 * the per-turn snapshot below and the income/upkeep accumulators.
 *   FUN_004487a9 @ 0x4487a9. */
void engine_ranking_recompute_player_total(int player);

/* Per-turn pass (first in the turn-advance sequence): for player slots
 * 1..5, roll five summary metrics into their previous-turn companions
 * (+0x04) for the standings/graph trend display, then resync each
 * player's cashflow total from its category breakdown.
 *   FUN_004486a9 @ 0x4486a9. */
void engine_ranking_snapshot_player_metrics(void);

#endif /* OPENLORDS2_ENGINE_RANKING_H */
