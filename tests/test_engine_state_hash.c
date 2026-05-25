/*
 * test_engine_state_hash.c — coverage for engine_compute_state_hash,
 * the pure-C port of retail FUN_00440231 (per-tick MP state hash).
 *
 * Properties verified:
 *
 *   - All-zero input + neither-branch mode → bucket[0..8]==0,
 *     bucket[7]==1 (the always-on quirk), bucket[9] = tick LSB + 1,
 *     hash_region all zeros.
 *   - Strategic mode (0) with patterned input — independent hashes
 *     for each of the three source arrays land in bucket[1..3] and
 *     the right per-slot indices in hash_region.
 *   - Combat mode (2) with patterned input — bucket[5/6] hold the
 *     loop sums; bucket[7] is ALWAYS overwritten to 1 (quirk #1).
 *   - bucket[0] formula matches `sum(bucket[1..8])` after the per-mode
 *     loops + the bucket[7]=1 overwrite.
 *   - bucket[9] = (sim_tick_counter & 0x7f) + 1 — verify the mask
 *     boundary (0x7f → 0x80, 0x80 → 1) and the +1 sentinel.
 *   - hash_record byte-sum truncates at 256 (verify a record of
 *     256 × 0x01 hashes to 0).
 *   - notify_combat_snapshot fires iff mode == 2.
 *   - Per-mode unused chunks of hash_region stay 0 (e.g. strategic
 *     mode never writes hash_region[200..399] beyond slot 5).
 */

#include "t.h"
#include "engine.h"

#include <string.h>
#include <stdint.h>

/* Allocate the six source arrays once at file scope.  Tests overwrite
 * specific slots; the global allocation keeps stack frames small.
 * Each is sized to (max_slot+1) × stride, matching retail's storage. */
static uint8_t g_per_army_768          [17  * 768];      /* slots 0..16 */
static uint8_t g_player_strategic_352  [ 6  * 352];      /* slots 0..5  */
static uint8_t g_per_territory_420     [151 * 420];      /* slots 0..150*/
static uint8_t g_per_battle_hex_432    [81  * 432];      /* slots 0..80 */
static uint8_t g_per_combat_unit_76    [101 *  76];      /* slots 0..100*/
static uint8_t g_per_battle_hex_aux_52 [81  *  52];      /* slots 0..80 */

static void zero_all_sources(void)
{
    memset(g_per_army_768,          0, sizeof g_per_army_768);
    memset(g_player_strategic_352,  0, sizeof g_player_strategic_352);
    memset(g_per_territory_420,     0, sizeof g_per_territory_420);
    memset(g_per_battle_hex_432,    0, sizeof g_per_battle_hex_432);
    memset(g_per_combat_unit_76,    0, sizeof g_per_combat_unit_76);
    memset(g_per_battle_hex_aux_52, 0, sizeof g_per_battle_hex_aux_52);
}

static void fill_input(engine_state_hash_in_t *in,
                       int mode, int player_slot,
                       uint32_t sim_tick_counter)
{
    in->mode             = mode;
    in->player_slot      = player_slot;
    in->sim_tick_counter = sim_tick_counter;
    in->per_army_768          = g_per_army_768;
    in->player_strategic_352  = g_player_strategic_352;
    in->per_territory_420     = g_per_territory_420;
    in->per_battle_hex_432    = g_per_battle_hex_432;
    in->per_combat_unit_76    = g_per_combat_unit_76;
    in->per_battle_hex_aux_52 = g_per_battle_hex_aux_52;
}

int test_engine_state_hash_zeros_mode_other(void)
{
    /* mode==1 — neither hash branch runs.  Everything-zero input means
     * bucket[1..6,8] stay 0, bucket[7]=1, bucket[0]=1 (only bucket[7]
     * contributes), bucket[9]=1 (tick=0 → 0+1).  hash_region all 0. */
    zero_all_sources();
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 1, /*player_slot*/ 0, /*tick*/ 0);
    memset(&out, 0xaa, sizeof out);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.bucket[0], 1u);
    T_ASSERT_EQ_U(out.bucket[1], 0u);
    T_ASSERT_EQ_U(out.bucket[2], 0u);
    T_ASSERT_EQ_U(out.bucket[3], 0u);
    T_ASSERT_EQ_U(out.bucket[4], 0u);
    T_ASSERT_EQ_U(out.bucket[5], 0u);
    T_ASSERT_EQ_U(out.bucket[6], 0u);
    T_ASSERT_EQ_U(out.bucket[7], 1u);
    T_ASSERT_EQ_U(out.bucket[8], 0u);
    T_ASSERT_EQ_U(out.bucket[9], 1u);
    for (size_t i = 0; i < sizeof out.hash_region; i++) {
        T_ASSERT_EQ_U(out.hash_region[i], 0u);
    }
    T_ASSERT_EQ_I(out.notify_combat_snapshot, 0);
    return 0;
}

int test_engine_state_hash_strategic_zeros(void)
{
    /* mode==0 with all-zero source arrays — strategic-loop bucket
     * sums are 0, so bucket[1..3] = 0; bucket[7]=1; bucket[0] = 1. */
    zero_all_sources();
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 3, /*tick*/ 0x77);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.bucket[1], 0u);
    T_ASSERT_EQ_U(out.bucket[2], 0u);
    T_ASSERT_EQ_U(out.bucket[3], 0u);
    T_ASSERT_EQ_U(out.bucket[7], 1u);
    T_ASSERT_EQ_U(out.bucket[0], 1u);
    /* 0x77 & 0x7f = 0x77, +1 = 0x78 */
    T_ASSERT_EQ_U(out.bucket[9], 0x78u);
    /* hash_region entirely zero (all-zero hashes). */
    for (size_t i = 0; i < sizeof out.hash_region; i++) {
        T_ASSERT_EQ_U(out.hash_region[i], 0u);
    }
    T_ASSERT_EQ_I(out.notify_combat_snapshot, 0);
    return 0;
}

int test_engine_state_hash_strategic_loop1_per_army(void)
{
    /* Inject one nonzero byte into per_army_768 slot 3, offset 5 (the
     * first hashed byte) — the per-slot hash for slot 3 is exactly
     * that byte's value; everything else stays 0; bucket[1] equals
     * that byte. */
    zero_all_sources();
    /* slot 3 is at index 3 * 0x300 = 0x900; offset 5 from there. */
    g_per_army_768[3 * 0x300 + 5] = 0x42;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[3], 0x42u);  /* per_army loop writes [1..16] */
    T_ASSERT_EQ_U(out.hash_region[1], 0u);
    T_ASSERT_EQ_U(out.hash_region[2], 0u);
    T_ASSERT_EQ_U(out.bucket[1],      0x42u);
    /* bucket[2] / [3] still 0 (other arrays empty). */
    T_ASSERT_EQ_U(out.bucket[2], 0u);
    T_ASSERT_EQ_U(out.bucket[3], 0u);
    /* bucket[0] = 0x42 + 0 + 0 + 0 + 0 + 0 + 1 + 0 = 0x43 */
    T_ASSERT_EQ_U(out.bucket[0], 0x43u);
    return 0;
}

int test_engine_state_hash_strategic_loop2_player_strategic(void)
{
    /* Inject into player_strategic_352 slot 4, two bytes at offsets 6 + 200.
     * Both bytes contribute to slot 4's hash; sum = 0x10 + 0x20 = 0x30. */
    zero_all_sources();
    g_player_strategic_352[4 * 0x160 + 6  ] = 0x10;
    g_player_strategic_352[4 * 0x160 + 200] = 0x20;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    /* second loop writes into hash_region[200..399] at indices 1..5;
     * slot 4 lands at hash_region[200 + 4] = hash_region[204] */
    T_ASSERT_EQ_U(out.hash_region[204], 0x30u);
    T_ASSERT_EQ_U(out.bucket[2],        0x30u);
    /* first-loop chunk unchanged (all-zero per_army) */
    for (int i = 0; i < 200; i++) {
        T_ASSERT_EQ_U(out.hash_region[i], 0u);
    }
    /* third-loop chunk unchanged (all-zero per_territory) */
    for (int i = 400; i < 600; i++) {
        T_ASSERT_EQ_U(out.hash_region[i], 0u);
    }
    return 0;
}

int test_engine_state_hash_strategic_loop3_per_territory_no_offset(void)
{
    /* per_territory uses offset=0, so every byte of the slot's 420-byte
     * record contributes.  Inject 3 bytes into slot 75 totalling 6;
     * bucket[3] += 6; hash_region[400+75] = 6. */
    zero_all_sources();
    g_per_territory_420[75 * 0x1a4 +   0] = 1;
    g_per_territory_420[75 * 0x1a4 + 100] = 2;
    g_per_territory_420[75 * 0x1a4 + 419] = 3;  /* last byte of the record */

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[400 + 75], 6u);
    T_ASSERT_EQ_U(out.bucket[3],             6u);
    return 0;
}

int test_engine_state_hash_strategic_hash_offset_skips_per_army_first_5(void)
{
    /* per_army uses offset=5 — bytes [0..4] of the record do NOT
     * contribute to the hash.  Place 0xff in those bytes; verify
     * the hash stays 0. */
    zero_all_sources();
    for (int j = 0; j < 5; j++) {
        g_per_army_768[7 * 0x300 + j] = 0xff;
    }
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[7], 0u);   /* slot 7 of the first chunk */
    T_ASSERT_EQ_U(out.bucket[1],      0u);
    return 0;
}

int test_engine_state_hash_strategic_hash_offset_skips_player_strategic_first_6(void)
{
    zero_all_sources();
    for (int j = 0; j < 6; j++) {
        g_player_strategic_352[2 * 0x160 + j] = 0xff;
    }
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 0, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[200 + 2], 0u);
    T_ASSERT_EQ_U(out.bucket[2],            0u);
    return 0;
}

int test_engine_state_hash_combat_basic(void)
{
    /* mode==2 — combat path.  Inject one byte into each of the three
     * combat arrays in different slots; verify they land in the right
     * 200-byte chunks + the right buckets. */
    zero_all_sources();
    /* per_battle_hex_432: slot 5, offset 18 + 100 = byte index 118
     * within the record (well past the hash offset). */
    g_per_battle_hex_432[5 * 0x1b0 + 18 + 100] = 0x11;
    /* per_combat_unit_76: slot 99, offset 4 + 50. */
    g_per_combat_unit_76[99 * 0x4c + 4 + 50] = 0x22;
    /* per_battle_hex_aux_52: slot 1, byte 30. */
    g_per_battle_hex_aux_52[1 * 0x34 + 30] = 0x33;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 2, /*slot*/ 2, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    /* First combat loop (per_battle_hex_432) → bucket[5] + hash_region[0..199].
     * Slot 5 byte = 0x11. */
    T_ASSERT_EQ_U(out.hash_region[5],   0x11u);
    T_ASSERT_EQ_U(out.bucket[5],        0x11u);
    /* Second combat loop (per_combat_unit_76) → bucket[6] + hash_region[200..399].
     * Slot 99 byte = 0x22 → hash_region[200 + 99] = 0x22. */
    T_ASSERT_EQ_U(out.hash_region[200 + 99], 0x22u);
    T_ASSERT_EQ_U(out.bucket[6],             0x22u);
    /* Third combat loop (per_battle_hex_aux_52) → hash_region[400..599];
     * the hash byte WRITES are preserved (quirk #1 only overwrites
     * bucket[7], not the region bytes). */
    T_ASSERT_EQ_U(out.hash_region[400 + 1], 0x33u);
    /* Quirk #1 — bucket[7] is ALWAYS 1 even in combat mode (the
     * per_battle_hex_aux loop sum is overwritten). */
    T_ASSERT_EQ_U(out.bucket[7], 1u);
    /* Strategic buckets untouched. */
    T_ASSERT_EQ_U(out.bucket[1], 0u);
    T_ASSERT_EQ_U(out.bucket[2], 0u);
    T_ASSERT_EQ_U(out.bucket[3], 0u);
    /* bucket[0] = b[5] + b[6] + b[7]=1 = 0x11 + 0x22 + 1 = 0x34. */
    T_ASSERT_EQ_U(out.bucket[0], 0x34u);
    /* notify_combat_snapshot fires. */
    T_ASSERT_EQ_I(out.notify_combat_snapshot, 1);
    return 0;
}

int test_engine_state_hash_quirk_bucket7_always_one(void)
{
    /* The per_battle_hex_aux third loop accumulates into bucket[7],
     * but bucket[7] = 1 fires AFTER.  Confirm: even with a slot that
     * hashes to a non-1 value, bucket[7] post-call is 1. */
    zero_all_sources();
    g_per_battle_hex_aux_52[7 * 0x34 + 0] = 0xaa;   /* slot 7, byte 0 */

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, /*mode*/ 2, /*slot*/ 0, /*tick*/ 0);
    engine_compute_state_hash(&in, &out);

    /* The per-slot hash byte is still written: */
    T_ASSERT_EQ_U(out.hash_region[400 + 7], 0xaau);
    /* But bucket[7] is overwritten to 1: */
    T_ASSERT_EQ_U(out.bucket[7], 1u);
    /* bucket[0] sum excludes the aux loop sum (it's 1, not 0xaa). */
    T_ASSERT_EQ_U(out.bucket[0], 1u);
    return 0;
}

int test_engine_state_hash_bucket9_formula(void)
{
    /* Verify bucket[9] = (sim_tick_counter & 0x7f) + 1 across boundaries. */
    zero_all_sources();
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 1, 0, 0);
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.bucket[9], 1u);                /* 0 → 1 */

    in.sim_tick_counter = 0x7f;
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.bucket[9], 0x80u);             /* 0x7f → 0x80 */

    in.sim_tick_counter = 0x80;
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.bucket[9], 1u);                /* 0x80 → mask is 0, +1 */

    in.sim_tick_counter = 0xff;
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.bucket[9], 0x80u);             /* 0xff → mask 0x7f, +1 */

    /* High bits of the u32 don't contribute (cast to byte first). */
    in.sim_tick_counter = 0xffffff7fu;
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.bucket[9], 0x80u);             /* low byte = 0x7f */
    return 0;
}

int test_engine_state_hash_byte_sum_wraps_at_256(void)
{
    /* Hash truncates to u8 every iteration.  Per_territory uses
     * offset=0 so every byte of the 420-byte slot is summed.  Set
     * 256 bytes of 0x01 + the rest 0 → wraps to 0. */
    zero_all_sources();
    for (int j = 0; j < 256; j++) {
        g_per_territory_420[10 * 0x1a4 + j] = 0x01;
    }
    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 0, 0, 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[400 + 10], 0u);   /* 256 mod 256 = 0 */
    T_ASSERT_EQ_U(out.bucket[3],             0u);
    /* Confirm we're actually summing — add one more byte and the sum is 1. */
    g_per_territory_420[10 * 0x1a4 + 256] = 0x01;
    engine_compute_state_hash(&in, &out);
    T_ASSERT_EQ_U(out.hash_region[400 + 10], 1u);
    T_ASSERT_EQ_U(out.bucket[3],             1u);
    return 0;
}

int test_engine_state_hash_bucket_sum_wraps(void)
{
    /* bucket[0] = sum(bucket[1..8]) and the sum itself truncates to u8.
     * Build three loop sums totalling > 256 to verify the wrap. */
    zero_all_sources();
    /* per_army slot 1: 100, slot 2: 100 → bucket[1] = 200 */
    g_per_army_768[1 * 0x300 + 5] = 100;
    g_per_army_768[2 * 0x300 + 5] = 100;
    /* player_strategic slot 1: 100 → bucket[2] = 100 */
    g_player_strategic_352[1 * 0x160 + 6] = 100;
    /* per_territory slot 1: 100 → bucket[3] = 100 */
    g_per_territory_420[1 * 0x1a4] = 100;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 0, 0, 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.bucket[1], 200u);
    T_ASSERT_EQ_U(out.bucket[2], 100u);
    T_ASSERT_EQ_U(out.bucket[3], 100u);
    /* bucket[0] = 200 + 100 + 100 + 0 + 0 + 0 + 1 + 0 = 401 → wraps to 145 */
    T_ASSERT_EQ_U(out.bucket[0], (uint8_t)(401u));
    return 0;
}

int test_engine_state_hash_slot_boundaries_per_army(void)
{
    /* Slot 0 and slot 17 (out of the 1..16 range) MUST be ignored.
     * Set both to nonzero; bucket[1] and the corresponding hash_region
     * entries must stay 0. */
    zero_all_sources();
    g_per_army_768[0 * 0x300 + 5] = 0x55;        /* slot 0 — skipped */
    /* Slot 17 is past the bound; we'd have to be ALLOCATING 18 slots
     * to test it (our array is sized for 17, indices 0..16).  Set slot
     * 16 (the LAST valid slot per retail's `local_8 < 0x11`) instead
     * to confirm it IS hashed. */
    g_per_army_768[16 * 0x300 + 5] = 0x77;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 0, 0, 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[0],  0u);     /* slot 0 not written */
    T_ASSERT_EQ_U(out.hash_region[16], 0x77u);  /* slot 16 hashed */
    T_ASSERT_EQ_U(out.bucket[1],       0x77u);
    return 0;
}

int test_engine_state_hash_player_slot_irrelevant_to_computation(void)
{
    /* player_slot only selects WHICH retail-side region the outputs
     * land in.  In the pure port, the output struct's bucket / hash_region
     * is the same regardless of player_slot — confirm by running with
     * two different slots and comparing the outputs. */
    zero_all_sources();
    g_per_army_768[3 * 0x300 + 5] = 0x99;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out1, out2;

    fill_input(&in, 0, /*slot*/ 0, /*tick*/ 0x40);
    engine_compute_state_hash(&in, &out1);

    fill_input(&in, 0, /*slot*/ 5, /*tick*/ 0x40);
    engine_compute_state_hash(&in, &out2);

    T_ASSERT_MEM_EQ(out1.bucket,      out2.bucket,      10);
    T_ASSERT_MEM_EQ(out1.hash_region, out2.hash_region, 600);
    return 0;
}

int test_engine_state_hash_output_writes_cover_all_strategic_slots(void)
{
    /* Confirm the strategic loops hit every documented slot index by
     * stamping unique nonzero values into each slot's first hashed
     * byte and reading them back from hash_region. */
    zero_all_sources();

    /* per_army slots 1..16 → bytes at offset 5; values 1..16. */
    for (int i = 1; i <= 16; i++) {
        g_per_army_768[i * 0x300 + 5] = (uint8_t)i;
    }
    /* player_strategic slots 1..5 → offset 6; values 0x10, 0x20, ..., 0x50. */
    for (int i = 1; i <= 5; i++) {
        g_player_strategic_352[i * 0x160 + 6] = (uint8_t)(i * 0x10);
    }
    /* per_territory slots 1..150 → offset 0; values i (mod 256). */
    for (int i = 1; i <= 150; i++) {
        g_per_territory_420[i * 0x1a4] = (uint8_t)i;
    }

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 0, 0, 0);
    engine_compute_state_hash(&in, &out);

    /* hash_region byte at slot N of chunk 1 = N */
    for (int i = 1; i <= 16; i++) {
        T_ASSERT_EQ_U(out.hash_region[i], (uint8_t)i);
    }
    T_ASSERT_EQ_U(out.hash_region[0],  0u);
    T_ASSERT_EQ_U(out.hash_region[17], 0u);  /* slot 17 not hashed */
    /* chunk 2 = i*0x10 at slots 1..5 */
    for (int i = 1; i <= 5; i++) {
        T_ASSERT_EQ_U(out.hash_region[200 + i], (uint8_t)(i * 0x10));
    }
    T_ASSERT_EQ_U(out.hash_region[200 + 6], 0u);  /* slot 6 not hashed */
    /* chunk 3 = i at slots 1..150 */
    for (int i = 1; i <= 150; i++) {
        T_ASSERT_EQ_U(out.hash_region[400 + i], (uint8_t)i);
    }
    T_ASSERT_EQ_U(out.hash_region[400 + 151], 0u);  /* slot 151 not hashed */
    return 0;
}

int test_engine_state_hash_combat_slot_boundaries(void)
{
    /* Confirm slot 0 and slot N+1 are not hashed in combat mode. */
    zero_all_sources();
    /* per_battle_hex_432: slots 1..80 hashed.  Set slot 80 (last valid)
     * and slot 0 (skipped); confirm slot 0 doesn't contribute. */
    g_per_battle_hex_432[0  * 0x1b0 + 18] = 0xff;   /* skipped */
    g_per_battle_hex_432[80 * 0x1b0 + 18] = 0x05;
    /* per_combat_unit_76: slots 1..100; slot 100 = last. */
    g_per_combat_unit_76[100 * 0x4c + 4] = 0x07;

    engine_state_hash_in_t  in;
    engine_state_hash_out_t out;
    fill_input(&in, 2, 0, 0);
    engine_compute_state_hash(&in, &out);

    T_ASSERT_EQ_U(out.hash_region[0],         0u);
    T_ASSERT_EQ_U(out.hash_region[80],        0x05u);
    T_ASSERT_EQ_U(out.bucket[5],              0x05u);
    T_ASSERT_EQ_U(out.hash_region[200 + 100], 0x07u);
    T_ASSERT_EQ_U(out.bucket[6],              0x07u);
    return 0;
}
