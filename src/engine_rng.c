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
 * (0x404b99 is the bounded-bitmask sampler; the wider call sites
 * read the 7-bit and 3-bit fields for cheap categorical rolls) read
 * these directly rather than running the LFSR themselves; one call
 * site (0x404b99) reruns the advance up to 10 times if the
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

/*
 * engine_rng_step_single — pure-C port of retail FUN_00404b2c @
 * 0x404b2c (109 B).  A single 31-bit Galois LFSR, structurally
 * identical to one lane of engine_rng_step (the FUN_00404a46 double
 * LFSR) above: same taps (bits 0 and 4), same bit-30 feedback inject,
 * same 31 iterations per call.  It runs on its own state word
 * (DAT_00591564) and produces a single 7-bit output mirror
 * (DAT_005c9a84 = new_state & 0x7f) rather than the double LFSR's six.
 *
 * Retail call shape:  FUN_00404b2c(); v = DAT_005c9a84 & 7;
 * — i.e. the advance is invoked for its side effect on the 7-bit
 * mirror, which callers mask further.  Six call sites: the new-game
 * overworld-grid seeders 0x46dd34 (ported — see
 * engine_territory_randomise_special_tiles) and 0x46dbf5 (its still-unported
 * sibling, both inside the 0x4676e0 setup sequence), plus the strategic
 * samplers at 0x47* (unported).
 *
 * Same engine quirk as the double LFSR: the feedback target is bit 30
 * (mask 0x40000000), not bit 31, so combined with the unsigned `>>1`
 * the high bit of state is always 0 after the first call — the LFSR
 * is effectively 31-bit.  Preserved verbatim for byte-shape parity.
 *
 * Pure: input state explicit, new state + 7-bit mirror returned in
 * out.  Consumers thread the state word through a pointer (see
 * engine_territory_randomise_special_tiles, the first one); a stateful
 * Win32 wrapper holding the live mirror lands only when a tick/sampler
 * consumer needs the state without an orchestrator to thread it.
 *
 * Retail seed (FUN_004b2258 / InitInstance @ line 83637): 0x34518632.
 */
void engine_rng_step_single(const engine_rng_single_in_t *in,
                            engine_rng_single_out_t *out)
{
    uint32_t s = in->state;

    for (int i = 0; i < 31; i++) {
        uint32_t f = ((s >> 4) & 1u) ^ (s & 1u);
        s >>= 1;
        if (f) s |= 0x40000000u;   /* bit 30 — retail's literal */
    }

    out->new_state = s;
    out->rng_7bit  = s & 0x7fu;
}
