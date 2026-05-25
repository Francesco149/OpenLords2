/*
 * Tests for src/engine_event_queue.c — the FUN_00472bc5 producer
 * cluster.  Covers the record layout, init's partial-zero quirk (#12),
 * the three filter branches, scratch-update-on-filter-fail, wrap-
 * around, the publish_scratch bypass, and the init wrapper.
 */

#include "t.h"
#include "engine_event_queue.h"

#include <string.h>

/* Common: zero-init the whole state (matches retail loader BSS) before
 * each test.  Subsequent functions called per-test exercise the
 * behavior on top of this clean slate. */
static void zero_state(engine_event_queue_state_t *s)
{
    memset(s, 0, sizeof(*s));
}

/* A "broadcast" filter table where the post-side filter accepts any
 * audience that the caller specifies — set in tests via flag5..7 etc. */
static const uint8_t k_player_has_data_all[7] = { 1, 1, 1, 1, 1, 1, 1 };
static const uint8_t k_player_has_data_none[7] = { 0, 0, 0, 0, 0, 0, 0 };

/* ------------- layout & sizes ------------------------------------- */

int test_engine_event_queue_record_size_is_24(void)
{
    T_ASSERT_EQ_U(sizeof(engine_event_queue_record_t),
                  ENGINE_EVENT_QUEUE_RECORD_BYTES);
    return 0;
}

int test_engine_event_queue_capacity_is_50(void)
{
    T_ASSERT_EQ_I(ENGINE_EVENT_QUEUE_CAPACITY, 0x32);
    return 0;
}

/* ------------- init ----------------------------------------------- */

int test_engine_event_queue_init_clears_indices_and_flags(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    s.write_idx         = 12;
    s.read_idx          = 7;
    s.notify_flag       = 1;
    s.consumer_cooldown = 999;
    engine_event_queue_init(&s);
    T_ASSERT_EQ_I(s.write_idx,         0);
    T_ASSERT_EQ_I(s.read_idx,          0);
    T_ASSERT_EQ_I(s.notify_flag,       0);
    T_ASSERT_EQ_I(s.consumer_cooldown, 0);
    return 0;
}

int test_engine_event_queue_init_zeros_slot_0_fully(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    /* Pre-stamp slot 0 with junk in every field; init must clear all
     * of them (read_idx is reset to 0 before the loop, so the 6 read-
     * idx-targeted writes always hit slot 0). */
    s.ring[0].audience   = 0x11111111;
    s.ring[0].target     = 0x22222222;
    s.ring[0].code       = 0x33333333;
    s.ring[0].payload32a = 0x44444444;
    s.ring[0].pad16      = 0x55;
    s.ring[0].flag5      = 0x66;
    s.ring[0].flag6      = 0x77;
    s.ring[0].flag7      = 0x88;
    s.ring[0].payload32b = 0x7eeeeeee;

    engine_event_queue_init(&s);
    /* `audience` and `pad16` are NEITHER targeted by the read-idx
     * writes NOR by the param_1 write — they keep their pre-init
     * bytes.  Documented in quirk #12. */
    T_ASSERT_EQ_U((uint32_t)s.ring[0].audience, 0x11111111u);
    T_ASSERT_EQ_U(s.ring[0].pad16,              0x55u);
    /* The six retail-zeroed fields: */
    T_ASSERT_EQ_I(s.ring[0].target,     0);
    T_ASSERT_EQ_I(s.ring[0].code,       0);
    T_ASSERT_EQ_I(s.ring[0].payload32a, 0);
    T_ASSERT_EQ_I(s.ring[0].flag5,      0);
    T_ASSERT_EQ_I(s.ring[0].flag6,      0);
    T_ASSERT_EQ_I(s.ring[0].flag7,      0);
    T_ASSERT_EQ_I(s.ring[0].payload32b, 0);
    return 0;
}

int test_engine_event_queue_init_quirk_slots_1_to_49_keep_non_code(void)
{
    /* The partial-zero quirk: init's 50-iter loop only zeros `code`
     * (+8) per-slot using the loop index; the other six writes always
     * target slot[read_idx] = slot[0].  So slots 1..49 keep their
     * pre-init bytes in everything but `code`. */
    engine_event_queue_state_t s;
    zero_state(&s);
    for (int i = 1; i < ENGINE_EVENT_QUEUE_CAPACITY; i++) {
        s.ring[i].audience   = 0xdeadbe00u | (uint32_t)i;
        s.ring[i].target     = 0xbaad0000 | i;
        s.ring[i].code       = 0xfacefeed;            /* will be cleared */
        s.ring[i].payload32a = 0xcafebabe;            /* will SURVIVE */
        s.ring[i].pad16      = (uint8_t)(0xa0 + i);   /* will SURVIVE */
        s.ring[i].flag5      = (uint8_t)(0xb0 + i);   /* will SURVIVE */
        s.ring[i].flag6      = (uint8_t)(0xc0 + i);
        s.ring[i].flag7      = (uint8_t)(0xd0 + i);
        s.ring[i].payload32b = 0x12345678;
    }
    engine_event_queue_init(&s);
    for (int i = 1; i < ENGINE_EVENT_QUEUE_CAPACITY; i++) {
        T_ASSERT_EQ_U((uint32_t)s.ring[i].code,       0u);
        T_ASSERT_EQ_U((uint32_t)s.ring[i].audience,   0xdeadbe00u | (uint32_t)i);
        T_ASSERT_EQ_U((uint32_t)s.ring[i].target,     (uint32_t)(0xbaad0000 | i));
        T_ASSERT_EQ_U((uint32_t)s.ring[i].payload32a, 0xcafebabeu);
        T_ASSERT_EQ_U(s.ring[i].pad16,                (uint8_t)(0xa0 + i));
        T_ASSERT_EQ_U(s.ring[i].flag5,                (uint8_t)(0xb0 + i));
        T_ASSERT_EQ_U(s.ring[i].flag6,                (uint8_t)(0xc0 + i));
        T_ASSERT_EQ_U(s.ring[i].flag7,                (uint8_t)(0xd0 + i));
        T_ASSERT_EQ_U((uint32_t)s.ring[i].payload32b, 0x12345678u);
    }
    return 0;
}

int test_engine_event_queue_init_wrapper_matches_init(void)
{
    engine_event_queue_state_t a, b;
    zero_state(&a);
    zero_state(&b);
    a.write_idx = b.write_idx = 5;
    a.notify_flag = b.notify_flag = 1;

    engine_event_queue_init(&a);
    engine_event_queue_init_wrapper(&b);

    T_ASSERT_MEM_EQ(&a, &b, sizeof(a));
    return 0;
}

/* ------------- post: filter branches ------------------------------ */

int test_engine_event_queue_post_broadcast_audience_publishes(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/  3, /*audience*/ 0,
        /*code*/    0x101, /*payload32a*/ 0x202,
        /*flag5*/   1, /*flag6*/ 2, /*flag7*/ 3,
        /*payload32b*/ 0x303,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(ok);
    T_ASSERT_EQ_I(s.write_idx, 1);
    T_ASSERT_EQ_I(s.notify_flag, 1);
    T_ASSERT_EQ_I(s.ring[0].audience,   0);
    T_ASSERT_EQ_I(s.ring[0].target,     3);
    T_ASSERT_EQ_I(s.ring[0].code,       0x101);
    T_ASSERT_EQ_I(s.ring[0].payload32a, 0x202);
    T_ASSERT_EQ_I(s.ring[0].flag5,      1);
    T_ASSERT_EQ_I(s.ring[0].flag6,      2);
    T_ASSERT_EQ_I(s.ring[0].flag7,      3);
    T_ASSERT_EQ_I(s.ring[0].payload32b, 0x303);
    return 0;
}

int test_engine_event_queue_post_audience_matches_local_publishes(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 4, /*audience*/ 2,
        0x10, 0x20, 1, 2, 3, 0x30,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(ok);
    T_ASSERT_EQ_I(s.write_idx, 1);
    T_ASSERT_EQ_I(s.ring[0].audience, 2);
    return 0;
}

int test_engine_event_queue_post_audience_mismatches_local_skips(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 1, /*audience*/ 5,
        0x10, 0x20, 1, 2, 3, 0x30,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(!ok);
    T_ASSERT_EQ_I(s.write_idx, 0);
    T_ASSERT_EQ_I(s.notify_flag, 0);
    /* Ring slot 0 stays empty. */
    T_ASSERT_EQ_I(s.ring[0].code, 0);
    return 0;
}

int test_engine_event_queue_post_target_zero_filter(void)
{
    /* target == 0 → broadcast-target arm.  Audience filter still
     * applies: audience == local publishes, otherwise skip. */
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 0, /*audience*/ 2,
        0xab, 0xcd, 0, 0, 0, 0xef,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(ok);

    zero_state(&s);
    ok = engine_event_queue_post(&s,
        /*target*/ 0, /*audience*/ 4,
        0xab, 0xcd, 0, 0, 0, 0xef,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(!ok);
    return 0;
}

int test_engine_event_queue_post_target_empty_filter(void)
{
    /* target != 0 but player_has_data[target] == 0 — same filter
     * outcome as target == 0. */
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 5, /*audience*/ 0,
        0x10, 0, 0, 0, 0, 0,
        k_player_has_data_none, /*local_player*/ 2);
    T_ASSERT(ok);  /* audience == 0 always publishes */

    zero_state(&s);
    ok = engine_event_queue_post(&s,
        /*target*/ 5, /*audience*/ 3,
        0x10, 0, 0, 0, 0, 0,
        k_player_has_data_none, /*local_player*/ 2);
    T_ASSERT(!ok);  /* audience 3 != local 2 */

    zero_state(&s);
    ok = engine_event_queue_post(&s,
        /*target*/ 5, /*audience*/ 2,
        0x10, 0, 0, 0, 0, 0,
        k_player_has_data_none, /*local_player*/ 2);
    T_ASSERT(ok);   /* audience == local publishes */
    return 0;
}

int test_engine_event_queue_post_scratch_updated_even_on_filter_fail(void)
{
    /* The filter-fail path still updates scratch — that's how the
     * save-load path's publish_scratch() can later commit. */
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 1, /*audience*/ 99,
        0xfeed, 0xface, 0xa1, 0xa2, 0xa3, 0xbeef,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(!ok);
    T_ASSERT_EQ_I(s.write_idx, 0);
    T_ASSERT_EQ_I(s.scratch.audience,   99);
    T_ASSERT_EQ_I(s.scratch.target,     1);
    T_ASSERT_EQ_U(s.scratch.code,       0xfeedu);
    T_ASSERT_EQ_U(s.scratch.payload32a, 0xfaceu);
    T_ASSERT_EQ_U(s.scratch.flag5,      0xa1u);
    T_ASSERT_EQ_U(s.scratch.flag6,      0xa2u);
    T_ASSERT_EQ_U(s.scratch.flag7,      0xa3u);
    T_ASSERT_EQ_U(s.scratch.payload32b, 0xbeefu);
    return 0;
}

/* ------------- ring buffer wrap-around ---------------------------- */

int test_engine_event_queue_post_wraps_write_idx(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    /* Fill the ring until the index points at the last slot (49). */
    s.write_idx = 0x31;
    bool ok = engine_event_queue_post(&s,
        0, 0, 0xabc, 0, 0, 0, 0, 0,
        k_player_has_data_all, /*local_player*/ 0);
    T_ASSERT(ok);
    /* After committing slot 49, write_idx becomes 50 → wraps to 0. */
    T_ASSERT_EQ_I(s.write_idx, 0);
    T_ASSERT_EQ_I(s.ring[0x31].code, 0xabc);
    return 0;
}

int test_engine_event_queue_publish_scratch_bumps_and_notifies(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    s.scratch.code       = 0xc0de;
    s.scratch.payload32a = 0x1234;
    s.scratch.flag5      = 0x55;
    s.write_idx = 5;
    s.notify_flag = 0;

    engine_event_queue_publish_scratch(&s);
    T_ASSERT_EQ_I(s.write_idx,   6);
    T_ASSERT_EQ_I(s.notify_flag, 1);
    T_ASSERT_EQ_U(s.ring[5].code,       0xc0deu);
    T_ASSERT_EQ_U(s.ring[5].payload32a, 0x1234u);
    T_ASSERT_EQ_U(s.ring[5].flag5,      0x55u);
    return 0;
}

int test_engine_event_queue_publish_scratch_wraps_too(void)
{
    engine_event_queue_state_t s;
    zero_state(&s);
    s.write_idx = 0x31;
    engine_event_queue_publish_scratch(&s);
    T_ASSERT_EQ_I(s.write_idx, 0);
    return 0;
}

int test_engine_event_queue_publish_scratch_does_not_touch_other_slots(void)
{
    /* Bytewise: the memcpy hits exactly 24 bytes at ring[write_idx]
     * — no spillover into neighboring slots. */
    engine_event_queue_state_t s;
    zero_state(&s);
    s.ring[0].code = 0xaaaa;
    s.ring[2].code = 0xbbbb;
    s.write_idx = 1;
    s.scratch.code = 0xc0de;

    engine_event_queue_publish_scratch(&s);
    T_ASSERT_EQ_U(s.ring[0].code, 0xaaaau);
    T_ASSERT_EQ_U(s.ring[1].code, 0xc0deu);
    T_ASSERT_EQ_U(s.ring[2].code, 0xbbbbu);
    return 0;
}

/* ------------- post commits scratch bytes verbatim ---------------- */

int test_engine_event_queue_post_pad16_stays_zero_through_publish(void)
{
    /* post() never touches scratch.pad16 — only init() zeros it (on
     * slot 0 only).  Verify the publish path doesn't smuggle stale
     * bytes into pad16. */
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        0, 0, 0x123, 0x456, 1, 2, 3, 0x789,
        k_player_has_data_all, 0);
    T_ASSERT(ok);
    T_ASSERT_EQ_U(s.ring[0].pad16, 0u);
    return 0;
}

int test_engine_event_queue_slot_zero_via_consumer_idx_matches(void)
{
    /* When the consumer (the only non-init caller) invokes slot_zero
     * with `slot == read_idx`, the partial-zero quirk vanishes — slot
     * `read_idx` gets every field cleared. */
    engine_event_queue_state_t s;
    zero_state(&s);
    s.read_idx = 7;
    s.ring[7].target     = 1;
    s.ring[7].code       = 2;
    s.ring[7].payload32a = 3;
    s.ring[7].pad16      = 4;
    s.ring[7].flag5      = 5;
    s.ring[7].flag6      = 6;
    s.ring[7].flag7      = 7;
    s.ring[7].payload32b = 8;
    s.ring[7].audience   = 9;

    engine_event_queue_slot_zero(&s, 7);

    /* All 6 read-idx-targeted fields + the param_1-targeted `code`
     * are zeroed.  `audience` and `pad16` stay as-is (no write
     * targets them). */
    T_ASSERT_EQ_I(s.ring[7].target,     0);
    T_ASSERT_EQ_I(s.ring[7].code,       0);
    T_ASSERT_EQ_I(s.ring[7].payload32a, 0);
    T_ASSERT_EQ_I(s.ring[7].flag5,      0);
    T_ASSERT_EQ_I(s.ring[7].flag6,      0);
    T_ASSERT_EQ_I(s.ring[7].flag7,      0);
    T_ASSERT_EQ_I(s.ring[7].payload32b, 0);
    T_ASSERT_EQ_I(s.ring[7].audience,   9);
    T_ASSERT_EQ_I(s.ring[7].pad16,      4);
    return 0;
}

int test_engine_event_queue_post_filter_target_populated_each_audience(void)
{
    /* Third filter arm: target != 0 AND player_has_data[target] != 0.
     * Sub-branches:
     *   - audience == local_player: publish (initial assignment)
     *   - audience != local_player AND audience != 0: skip
     *   - audience == 0: publish (body override) */
    engine_event_queue_state_t s;
    zero_state(&s);
    bool ok = engine_event_queue_post(&s,
        /*target*/ 4, /*audience*/ 2,
        0x10, 0, 0, 0, 0, 0,
        k_player_has_data_all, /*local_player*/ 2);
    T_ASSERT(ok);

    zero_state(&s);
    ok = engine_event_queue_post(&s,
        4, 5, 0x10, 0, 0, 0, 0, 0,
        k_player_has_data_all, 2);
    T_ASSERT(!ok);

    zero_state(&s);
    ok = engine_event_queue_post(&s,
        4, 0, 0x10, 0, 0, 0, 0, 0,
        k_player_has_data_all, 2);
    T_ASSERT(ok);
    return 0;
}
