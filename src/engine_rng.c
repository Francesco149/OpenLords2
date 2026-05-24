/*
 * Double-LFSR RNG advance — pure-C port of retail FUN_00404a46 @
 * 0x404a46 (230 B).  Called from the sub-step body's SP-only step
 * (gated on DAT_00553030 == 0) and from ~10 other sites scattered
 * across the sim.
 *
 * Two independent 32-bit Galois LFSRs with taps at bits 0 and 4.
 * On each iteration:
 *
 *   f       = bit_4(state) XOR bit_0(state)   // feedback
 *   state >>= 1                                // logical shift right
 *   if (f) state |= (1u << 30)                 // inject into bit 30
 *
 * 31 iterations per call.  Over those 31 iters every bit of the
 * original 31-bit state gets consumed; the new state's bits 30..0
 * each hold one feedback output (most recent at bit 0, oldest at
 * bit 30).  Bit 31 is always 0 after the first iter's shift (uint
 * shift is logical).
 *
 * After the advance, six output mirrors are produced — three for
 * each state, masked at 15 / 7 / 3 bits.  Retail consumers
 * (FUN_00404b99 is the bounded-bitmask sampler; the wider call sites
 * read the 7-bit and 3-bit fields for cheap categorical rolls) read
 * these directly rather than running the LFSR themselves; one call
 * site (FUN_00404b99) reruns the advance up to 10 times if the
 * 15-bit value is above its bit-mask ceiling.
 *
 * Pure function: every input explicit, every state mutation returned
 * in out.  The Win32 wrapper in engine.c holds the state mirrors +
 * output mirrors and threads them through.
 *
 * Retail seeds (set in FUN_004b2258 / InitInstance at lines
 * 83635-83636): state_a = 0x54657687, state_b = 0x72641663.
 *
 * Engine quirk preserved: the LFSR feedback target is bit 30 (mask
 * 0x40000000), not bit 31.  Combined with the unsigned right-shift
 * that clears bit 31, this means the high bit of state is always 0
 * after the first call — the LFSR is effectively 31-bit, not 32-bit.
 * Either an author preference or a leftover from a signed-int era.
 *
 * Reference: docs/findings/main-loop-tick.md §"sub-step body".
 */

#include "engine.h"

#include <stdint.h>

void engine_rng_step(const engine_rng_in_t *in, engine_rng_out_t *out)
{
    uint32_t a = in->state_a;
    uint32_t b = in->state_b;

    for (int i = 0; i < 31; i++) {
        /* Feedback = bit_4 XOR bit_0.  Retail computes this as
         * `(bit_4 >> 4) != bit_0`, which is the same predicate. */
        uint32_t fa = ((a >> 4) & 1u) ^ (a & 1u);
        uint32_t fb = ((b >> 4) & 1u) ^ (b & 1u);

        a >>= 1;
        b >>= 1;

        if (fa) a |= 0x40000000u;   /* bit 30 — retail's literal */
        if (fb) b |= 0x40000000u;
    }

    out->new_state_a = a;
    out->new_state_b = b;

    out->rng_15bit_a = a & 0x7fffu;
    out->rng_7bit_a  = a & 0x7fu;
    out->rng_3bit_a  = a & 0x7u;
    out->rng_15bit_b = b & 0x7fffu;
    out->rng_7bit_b  = b & 0x7fu;
    out->rng_3bit_b  = b & 0x7u;
}
