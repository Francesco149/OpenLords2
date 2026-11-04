/*
 * FPS counter — pure-C port of retail FUN_004b27bf @ 0x4b27bf (143 B).
 *
 * Maintains a rolling 1-second FPS measurement:
 *
 *   DAT_004eada4 (frames_this_second) increments on every call.
 *   DAT_004eaba0 (second_window_start_ms) is the start of the current
 *       1-second measurement window.
 *   When `now_ms - second_window_start_ms > 1000`:
 *     - current FPS = frames_this_second * 1000 / window_elapsed
 *     - cumulative FPS += current FPS
 *     - cumulative seconds += 1
 *     - frames_this_second = 0
 *     - window starts a new second
 *
 * Returns ms since the previous frame (always — independent of the
 * rollover branch).  Retail's caller in FUN_004b99c0 discards the
 * return value, but downstream the cumulative FPS / seconds pair gets
 * read by 0x4b2636 (session diag — reports average FPS over the
 * whole session).
 *
 * Retail global ↔ field mapping (mirror is in engine.c):
 *
 *   _DAT_004e59e0   in->last_render_ms / out->new_last_render_ms
 *   DAT_004eaba0    in->second_window_start_ms / out->new_second_window_start_ms
 *   DAT_004eada4    in->frames_this_second / out->new_frames_this_second
 *   DAT_004ea174    in->current_fps / out->new_current_fps
 *   DAT_004ea0f4    in->cumulative_fps / out->new_cumulative_fps
 *   DAT_004ea83c    in->cumulative_seconds / out->new_cumulative_seconds
 *
 * Two quirks worth knowing:
 *
 * 1. The rollover gate is `1000 < (int)(now - window_start)` — a
 *    SIGNED compare.  If timeGetTime wraps (49.7 days), the result
 *    can read as negative and the rollover never fires.  Same boundary
 *    behaviour as engine_compute_frame_delta's 30 ms gate; preserved
 *    here for parity.
 *
 * 2. Retail calls timeGetTime TWICE on a rollover frame — once at
 *    function entry (used for the delta + the rollover gate) and once
 *    inside the rollover branch to set the new window-start base.
 *    Our pure function takes both values as separate inputs
 *    (`now_ms` + `rollover_now_ms`) so the wrapper makes the two
 *    real calls and preserves the 2-call shape.  In practice the two
 *    timestamps differ by < 1 ms; the effect is a tiny drift in the
 *    window-start basis vs. real wall-clock time, bounded by however
 *    long the function itself takes to execute.  Behavioural delta
 *    is negligible but the shape is faithful.
 *
 * Reference: docs/findings/main-loop-tick.md §"render-phase tail".
 */

#include "engine.h"

void engine_compute_fps(const engine_fps_in_t *in, engine_fps_out_t *out)
{
    /* Frame count for the in-progress 1-second window. */
    uint32_t frames = in->frames_this_second + 1;

    /* ms since previous frame — this is the return value retail's
     * caller discards.  Computed against last_render_ms; the
     * baseline is unconditionally advanced to now_ms. */
    out->delta_ms             = in->now_ms - in->last_render_ms;
    out->new_last_render_ms   = in->now_ms;

    /* Default writebacks for fields that only change on rollover.
     * Pre-populating them here keeps the rollover branch focused on
     * the actual transitions. */
    out->new_frames_this_second  = frames;
    out->new_second_window_start_ms = in->second_window_start_ms;
    out->new_current_fps         = in->current_fps;
    out->new_cumulative_fps      = in->cumulative_fps;
    out->new_cumulative_seconds  = in->cumulative_seconds;
    out->window_rolled_over      = 0;

    /* Signed `1000 < (int)elapsed` — see quirk (1) above. */
    int32_t elapsed = (int32_t)(in->now_ms - in->second_window_start_ms);
    if (1000 < elapsed) {
        /* Retail: `DAT_004ea174 = (uint)(frames * 1000) / elapsed;`
         * Compute as uint32 throughout — the cast in retail forces
         * the multiplication into unsigned, so a frames_this_second
         * value high enough to overflow uint32 wraps silently.  At
         * any realistic FPS this can't happen (60 FPS × 1000 = 60k). */
        uint32_t fps = (uint32_t)(frames * 1000u) / (uint32_t)elapsed;
        out->new_current_fps             = fps;
        out->new_second_window_start_ms  = in->rollover_now_ms;
        out->new_frames_this_second      = 0;
        out->new_cumulative_fps          = in->cumulative_fps + fps;
        out->new_cumulative_seconds      = in->cumulative_seconds + 1;
        out->window_rolled_over          = 1;
    }
}
