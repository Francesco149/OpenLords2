/*
 * Frame-delta generator — pure-C port of retail FUN_004b20ed
 * @ 0x4b20ed (68 B).
 *
 * Computes ms since the last accepted frame, with a 30 ms low-pass:
 * deltas below 0x1e get reported as 0 and the baseline timestamp is
 * NOT advanced, so the next call sees a larger delta from the same
 * baseline.  Downstream sprite-animation updaters (e.g. retail
 * `FUN_0040da1e`, the per-actor walk-cycle advancer) consume the
 * result via the `DAT_004ea128` mirror to decide whether to bump
 * their animation phase this frame.
 *
 * The 30 ms gate is deliberately *signed* (`iVar2 < 0x1e`).  A
 * negative delta — caused by `timeGetTime` wraparound after 49.7
 * days, or by debug code seeding the baseline to a future time —
 * also triggers the suppress branch.  This differs from
 * `engine_compute_phase_ticks`, which special-cases the negative
 * branch as "wrap → fire".  Tests pin both behaviours.
 *
 * Pure function: every input explicit, every state mutation
 * returned in the output struct.  No globals.  The Win32-coupled
 * wrapper in engine.c reads/writes the file-static mirrors.
 *
 * Retail global ↔ field mapping (mirror is in engine.c):
 *
 *   _DAT_004e59ec   in->last_frame_ms / out->new_last_frame_ms
 *   DAT_004ea128    out->delta_ms     (mirror; consumed by downstream
 *                                      animation updaters)
 *
 * Reference: docs/findings/main-loop-tick.md §"pre-tick block".
 */

#include "engine.h"

void engine_compute_frame_delta(const engine_delta_in_t *in,
                                engine_delta_out_t *out)
{
    int32_t delta = (int32_t)(in->now_ms - in->last_frame_ms);
    if (delta < 0x1e) {
        out->delta_ms          = 0;
        out->new_last_frame_ms = in->last_frame_ms;   /* hold baseline */
    } else {
        out->delta_ms          = delta;
        out->new_last_frame_ms = in->now_ms;          /* advance */
    }
}
