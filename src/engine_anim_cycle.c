/*
 * 16-frame animation cycle counter — pure-C port of retail
 * FUN_00451f1a @ 0x451f1a (81 B).
 *
 * Bumps a single counter mod 16 every time the 80 ms phase tick
 * fires.  Downstream consumers — e.g. retail `FUN_004071a0` — use
 * the counter as a sprite-strip frame index (`counter + 0x28` in
 * that caller; 16 contiguous frames starting at PL8 index 0x28).
 * Full cycle is 16 × 80 ms = 1280 ms.
 *
 * Pure function: every input explicit, every state mutation
 * returned in the output struct.  The Win32-coupled wrapper in
 * engine.c reads the 80 ms phase tick global + counter mirror,
 * calls this, writes the new counter back.
 *
 * Retail global ↔ field mapping (mirror is in engine.c):
 *
 *   DAT_0053e9a4   in->counter / out->new_counter
 *   DAT_00591510   in->tick_80ms_fired  (same global mirrored as
 *                                        g_phase_tick_80ms — produced
 *                                        by engine_compute_phase_ticks)
 *
 * Engine quirk preserved: retail clamps the counter both upward
 * (`0xf < c → c = 0`, the actual rollover) and downward (`c < 0 →
 * c = 0`).  The downward clamp is provably unreachable — the
 * counter starts at 0 in .bss, is only ever bumped by 1, only ever
 * reset to 0 by the upward clamp.  No other writer in the binary
 * (we grepped: only FUN_00451f1a writes; FUN_004071a0 only reads).
 * Likely a defensive habit from the author or a leftover from an
 * earlier signed-int variant.  We keep it for byte-shape parity
 * with retail and document it in engine-quirks.
 *
 * Reference: docs/findings/main-loop-tick.md §"pre-tick block".
 */

#include "engine.h"

void engine_compute_anim_cycle(const engine_anim_cycle_in_t *in,
                               engine_anim_cycle_out_t *out)
{
    int c = in->counter;
    if (in->tick_80ms_fired) {
        c += 1;
        if (0xf < c) c = 0;   /* upper clamp / rollover at 16 */
        if (c < 0)   c = 0;   /* defensive lower clamp (unreachable) */
    }
    out->new_counter = c;
}
