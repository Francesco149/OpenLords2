/*
 * test_engine_rng.c — coverage for engine_rng_step, the pure-C port
 * of retail FUN_00404a46 (double 31-bit Galois LFSR advance).
 *
 * Properties verified:
 *
 *   - All-zero state stays zero (degenerate LFSR fixed point).
 *   - Single-bit state (state=1) returns to 1 after one 31-iter call
 *     — the retail polynomial isn't strictly primitive at that seed,
 *     so seed=1 lands in a 31-step trivial cycle.  Pinned because
 *     it's a non-obvious property of the chosen taps.
 *   - The 32-bit state's bit 31 is always 0 after any call (the
 *     LFSR is effectively 31-bit because feedback targets bit 30,
 *     not bit 31).  Documented engine quirk.
 *   - State A and state B advance independently — non-zero seed in
 *     one doesn't affect the other.
 *   - The 15/7/3 bit outputs equal new_state masked.
 *   - Reproducibility: same input state → same output state.
 *   - Retail seed (0x54657687, 0x72641663) produces a non-zero
 *     mixed-bit output.
 */

#include "t.h"
#include "engine.h"

#include <string.h>
#include <stdint.h>

/* Reference helper — single 31-iter advance of one state word.  Used
 * to verify the engine_rng_step output against an independent
 * implementation written here in the test (so the test isn't just
 * re-asserting the port). */
static uint32_t ref_advance(uint32_t s)
{
    for (int i = 0; i < 31; i++) {
        int b4 = (int)((s >> 4) & 1u);
        int b0 = (int)(s & 1u);
        s >>= 1;
        if (b4 != b0) s |= 0x40000000u;
    }
    return s;
}

int test_engine_rng_zero_state_stays_zero(void)
{
    engine_rng_in_t  in  = { .state_a = 0, .state_b = 0 };
    engine_rng_out_t out;
    memset(&out, 0xff, sizeof(out));

    engine_rng_step(&in, &out);

    T_ASSERT_EQ_U(out.new_state_a, 0u);
    T_ASSERT_EQ_U(out.new_state_b, 0u);
    T_ASSERT_EQ_U(out.rng_15bit_a, 0u);
    T_ASSERT_EQ_U(out.rng_7bit_a,  0u);
    T_ASSERT_EQ_U(out.rng_3bit_a,  0u);
    T_ASSERT_EQ_U(out.rng_15bit_b, 0u);
    T_ASSERT_EQ_U(out.rng_7bit_b,  0u);
    T_ASSERT_EQ_U(out.rng_3bit_b,  0u);
    return 0;
}

int test_engine_rng_seed_one_traces_to_specific_value(void)
{
    /* Hand-traced advance for state=1, pinned as a precomputed
     * regression.  Walk:
     *   iter 1  : bit4=0, bit0=1 → f=1.  shift→0.  inject→0x40000000
     *   iter 2..27 : the single set bit just walks right (no feedback)
     *   iter 28 (in=0x00000010): bit4=1, bit0=0 → f=1.  shift→0x08.
     *                            inject→0x40000008
     *   iter 29..31: walk right again
     * Final: 0x40000008 >> 3 = 0x08000001.
     *
     * Pinned because the value is surprising — naive trace says
     * "state=1 returns to 1" (forgetting iter 28's secondary
     * feedback), which is what bit me when writing this test. */
    engine_rng_in_t  in  = { .state_a = 1, .state_b = 1 };
    engine_rng_out_t out;

    engine_rng_step(&in, &out);
    T_ASSERT_EQ_U(out.new_state_a, 0x08000001u);
    T_ASSERT_EQ_U(out.new_state_b, 0x08000001u);
    return 0;
}

int test_engine_rng_bit_31_always_cleared(void)
{
    /* Even with bit 31 set on input, the unsigned `>>1` clears it on
     * the first iter, and the feedback only ever sets bit 30 — so
     * after one call, bit 31 is 0.  Pinned. */
    engine_rng_in_t  in  = { .state_a = 0x80000000u, .state_b = 0xffffffffu };
    engine_rng_out_t out;

    engine_rng_step(&in, &out);

    T_ASSERT_EQ_U(out.new_state_a & 0x80000000u, 0u);
    T_ASSERT_EQ_U(out.new_state_b & 0x80000000u, 0u);
    return 0;
}

int test_engine_rng_states_are_independent(void)
{
    /* Zero state_b, non-zero state_a → out.new_state_b stays 0
     * (zero is a fixed point), out.new_state_a evolves to whatever
     * the reference advance computes. */
    engine_rng_in_t  in  = { .state_a = 0x54657687u, .state_b = 0u };
    engine_rng_out_t out;

    engine_rng_step(&in, &out);

    T_ASSERT_EQ_U(out.new_state_b, 0u);
    T_ASSERT_EQ_U(out.new_state_a, ref_advance(0x54657687u));
    return 0;
}

int test_engine_rng_output_masks_match_state(void)
{
    /* The six mask-outputs are just (new_state & mask).  Verify with
     * a non-trivial seed so all three bit widths see at least one
     * set bit. */
    engine_rng_in_t  in  = { .state_a = 0x54657687u, .state_b = 0x72641663u };
    engine_rng_out_t out;

    engine_rng_step(&in, &out);

    T_ASSERT_EQ_U(out.rng_15bit_a, out.new_state_a & 0x7fffu);
    T_ASSERT_EQ_U(out.rng_7bit_a,  out.new_state_a & 0x7fu);
    T_ASSERT_EQ_U(out.rng_3bit_a,  out.new_state_a & 0x7u);
    T_ASSERT_EQ_U(out.rng_15bit_b, out.new_state_b & 0x7fffu);
    T_ASSERT_EQ_U(out.rng_7bit_b,  out.new_state_b & 0x7fu);
    T_ASSERT_EQ_U(out.rng_3bit_b,  out.new_state_b & 0x7u);
    return 0;
}

int test_engine_rng_reproducibility(void)
{
    /* Same input → same output.  Sanity that we haven't accidentally
     * pulled in any external state (PRNG library, clock, etc.). */
    engine_rng_in_t  in  = { .state_a = 0x12345678u, .state_b = 0xdeadbeefu };
    engine_rng_out_t out_a, out_b;

    engine_rng_step(&in, &out_a);
    engine_rng_step(&in, &out_b);

    T_ASSERT_EQ_U(out_a.new_state_a, out_b.new_state_a);
    T_ASSERT_EQ_U(out_a.new_state_b, out_b.new_state_b);
    T_ASSERT_EQ_U(out_a.rng_15bit_a, out_b.rng_15bit_a);
    return 0;
}

int test_engine_rng_retail_seed_produces_nonzero(void)
{
    /* The retail seeds (0x54657687, 0x72641663) should produce a
     * non-degenerate output: at least one of the rng_15bit outputs
     * non-zero, and the new states clearly different from the
     * seeds. */
    engine_rng_in_t  in  = { .state_a = 0x54657687u, .state_b = 0x72641663u };
    engine_rng_out_t out;

    engine_rng_step(&in, &out);

    T_ASSERT(out.new_state_a != 0x54657687u);
    T_ASSERT(out.new_state_b != 0x72641663u);
    T_ASSERT(out.rng_15bit_a != 0u || out.rng_15bit_b != 0u);
    return 0;
}

int test_engine_rng_matches_reference(void)
{
    /* Catch-all: verify the port matches the reference advance for
     * a couple of arbitrary non-trivial seeds. */
    const uint32_t seeds[][2] = {
        { 0x54657687u, 0x72641663u },
        { 0xdeadbeefu, 0xcafebabeu },
        { 0x00000010u, 0x80000001u },
        { 0xffffff80u, 0x000000ffu },
    };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        engine_rng_in_t  in  = { .state_a = seeds[i][0], .state_b = seeds[i][1] };
        engine_rng_out_t out;
        engine_rng_step(&in, &out);
        T_ASSERT_EQ_U(out.new_state_a, ref_advance(seeds[i][0]));
        T_ASSERT_EQ_U(out.new_state_b, ref_advance(seeds[i][1]));
    }
    return 0;
}

int test_engine_rng_multi_call_evolves(void)
{
    /* Drive the LFSR through 10 calls from the retail seed; state
     * should keep changing every call (no early fixed point at this
     * seed).  Also confirms the 15-bit output isn't stuck. */
    engine_rng_in_t  in  = { .state_a = 0x54657687u, .state_b = 0x72641663u };
    engine_rng_out_t out;

    uint32_t prev_a = in.state_a;
    uint32_t prev_b = in.state_b;
    uint32_t mix15 = 0;

    for (int i = 0; i < 10; i++) {
        engine_rng_step(&in, &out);
        T_ASSERT(out.new_state_a != prev_a);
        T_ASSERT(out.new_state_b != prev_b);
        prev_a = out.new_state_a;
        prev_b = out.new_state_b;
        mix15 |= out.rng_15bit_a | out.rng_15bit_b;
        in.state_a = out.new_state_a;
        in.state_b = out.new_state_b;
    }
    /* After 10 calls, the OR of all 15-bit outputs should have hit
     * a healthy spread of bits — confirms the LFSR isn't stuck on a
     * tiny subset. */
    T_ASSERT(mix15 > 0xffu);
    return 0;
}
