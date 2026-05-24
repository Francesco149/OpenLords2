/*
 * Animation-phase tick generator — pure-C port of retail FUN_004bbc80
 * @ 0x4bbc80 (465 B).
 *
 * Maintains nine "did event X just fire" flags that downstream
 * sprite/UI updaters read once per frame to drive their animation
 * cadence.  The base tick fires every 20 ms (clamped from
 * `timeGetTime` jitter); the 80 ms tick fires every 4 base ticks;
 * the longer ticks (160/320/640/1040/1280/1920/2560 ms) fire on
 * per-counter rollovers gated off the 80 ms tick.
 *
 * Pure function — every input explicit, every state mutation
 * returned in the output struct.  No globals.  The Win32-coupled
 * wrapper in engine.c reads/writes file-static mirrors.
 *
 * Retail flag ↔ field mapping (mirrors are in engine.c):
 *
 *   DAT_0058fcb0   out->tick_20ms        every 20 ms (base tick)
 *   DAT_00591510   out->tick_80ms        every 80 ms  (4 base ticks)
 *   DAT_0057d3ac   out->tick_160ms       (every 2 of the 80ms ticks)
 *   DAT_0057d3c8   out->tick_320ms       (every 4 of the 80ms ticks)
 *   DAT_0058fd08   out->tick_640ms       (every 8 of the 80ms ticks)
 *  _DAT_005aeb28   out->tick_1040ms      (every 13 of the 80ms ticks) — sticky
 *   DAT_005aeb3c   out->tick_1280ms      (every 16)
 *   DAT_005aeb38   out->tick_1920ms      (every 24)
 *   DAT_005aeb30   out->tick_2560ms      (every 32)
 *
 * Sticky semantics: retail clears 8 of 9 flags at entry, but NOT
 * the 1040 ms flag (_DAT_005aeb28).  Downstream consumers
 * read+clear it themselves (one-shot delivery).  Our wrapper
 * preserves the same shape — the writeback for that flag is OR'd
 * into the live global rather than overwritten, and consumers are
 * responsible for clearing.  The pure function still sets the
 * flag to 1 on rollover; the in→out semantics for sticky-ness
 * live entirely in the Win32 wrapper.
 *
 * Counter rollover semantics: retail increments first, then tests
 * `if (N < counter)` — so a counter going 0→1→2 with threshold 1
 * fires on the SECOND increment (when counter=2, 1 < 2).  In other
 * words a "fires every K" counter has threshold K-1 (the K-th
 * increment trips the check, fires the flag, resets to 0).
 *
 * Reference: docs/findings/main-loop-tick.md §"FUN_004bbc80".
 */

#include "engine.h"

/* Threshold constants — keep these here (not in the header) since
 * they're internal arithmetic, not part of the contract.  Values
 * mirror retail's exact comparison constants. */
#define BASE_TICK_PERIOD_MS  0x13     /* 20 ms minus 1 — `if (0x13 < delta)` */
#define BASE_TICKS_PER_80MS  3        /* `if (3 < counter)` */
#define CYCLE_THRESHOLD_2    1
#define CYCLE_THRESHOLD_4    3
#define CYCLE_THRESHOLD_8    7
#define CYCLE_THRESHOLD_13   0xc
#define CYCLE_THRESHOLD_16   0xf
#define CYCLE_THRESHOLD_24   0x17
#define CYCLE_THRESHOLD_32   0x1f

void engine_compute_phase_ticks(const engine_phase_in_t *in,
                                engine_phase_out_t *out)
{
    /* Default outputs — all flags zero, all counters/timestamps
     * carried through unchanged.  Branches below selectively
     * overwrite. */
    out->tick_20ms     = 0;
    out->tick_80ms     = 0;
    out->tick_160ms    = 0;
    out->tick_320ms    = 0;
    out->tick_640ms    = 0;
    out->tick_1040ms   = 0;
    out->tick_1280ms   = 0;
    out->tick_1920ms   = 0;
    out->tick_2560ms   = 0;

    out->new_last_base_tick_ms = in->last_base_tick_ms;
    out->new_base_tick_counter = in->base_tick_counter;
    out->new_counter_2         = in->counter_2;
    out->new_counter_4         = in->counter_4;
    out->new_counter_8         = in->counter_8;
    out->new_counter_13        = in->counter_13;
    out->new_counter_16        = in->counter_16;
    out->new_counter_24        = in->counter_24;
    out->new_counter_32        = in->counter_32;

    int32_t delta = (int32_t)(in->now_ms - in->last_base_tick_ms);

    /* Base 20 ms tick.  Retail: `(0x13 < delta) || (delta < 0)`.
     * The negative branch catches `timeGetTime` wraparound or
     * a debug-seeded future timestamp. */
    if (BASE_TICK_PERIOD_MS < delta || delta < 0) {
        out->new_base_tick_counter = in->base_tick_counter + 1;
        out->tick_20ms             = 1;
        out->new_last_base_tick_ms = in->now_ms;
    }

    /* 80 ms tick — fires when the base counter has caught up to 4
     * (i.e. four 20 ms ticks accumulated since the last 80 ms fire).
     * Retail tests `if (3 < counter)` *after* the conditional
     * increment above — so we look at the (possibly bumped) value. */
    if (BASE_TICKS_PER_80MS < (int32_t)out->new_base_tick_counter) {
        out->new_base_tick_counter = 0;
        out->tick_80ms             = 1;

        /* 160 ms (×2 of 80 ms). */
        out->new_counter_2 = in->counter_2 + 1;
        if (CYCLE_THRESHOLD_2 < (int32_t)out->new_counter_2) {
            out->new_counter_2 = 0;
            out->tick_160ms    = 1;
        }

        /* 320 ms (×4 of 80 ms). */
        out->new_counter_4 = in->counter_4 + 1;
        if (CYCLE_THRESHOLD_4 < (int32_t)out->new_counter_4) {
            out->new_counter_4 = 0;
            out->tick_320ms    = 1;
        }

        /* 640 ms (×8 of 80 ms). */
        out->new_counter_8 = in->counter_8 + 1;
        if (CYCLE_THRESHOLD_8 < (int32_t)out->new_counter_8) {
            out->new_counter_8 = 0;
            out->tick_640ms    = 1;
        }

        /* 1040 ms (×13 of 80 ms) — STICKY downstream (wrapper
         * doesn't clear before this call).  The pure function
         * still SETS the flag on rollover; the sticky-vs-fresh
         * semantics live in the Win32 wrapper's read-clear-write
         * pattern for the other 8 flags vs the OR-in-only pattern
         * for this one. */
        out->new_counter_13 = in->counter_13 + 1;
        if (CYCLE_THRESHOLD_13 < (int32_t)out->new_counter_13) {
            out->new_counter_13 = 0;
            out->tick_1040ms    = 1;
        }

        /* 1280 ms (×16). */
        out->new_counter_16 = in->counter_16 + 1;
        if (CYCLE_THRESHOLD_16 < (int32_t)out->new_counter_16) {
            out->new_counter_16 = 0;
            out->tick_1280ms    = 1;
        }

        /* 1920 ms (×24). */
        out->new_counter_24 = in->counter_24 + 1;
        if (CYCLE_THRESHOLD_24 < (int32_t)out->new_counter_24) {
            out->new_counter_24 = 0;
            out->tick_1920ms    = 1;
        }

        /* 2560 ms (×32). */
        out->new_counter_32 = in->counter_32 + 1;
        if (CYCLE_THRESHOLD_32 < (int32_t)out->new_counter_32) {
            out->new_counter_32 = 0;
            out->tick_2560ms    = 1;
        }
    }
}
