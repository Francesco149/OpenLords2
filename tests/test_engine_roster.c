/*
 * test_engine_roster.c — coverage for the player-roster trio
 * (player_roster_add / _clear / _compact), the pure-C ports of retail
 * FUN_0049bbe8 / FUN_0049bc1d / FUN_0049bc5f.
 *
 * The roster is g_player_roster[12]: 6 interleaved {army-id, seat} byte
 * pairs.  Pair i = { g_player_roster[2i], g_player_roster[2i+1] }.
 * g_player_roster_count (= s_slot_0053e8a4) tallies non-zero-seat adds.
 * The compactor drops pairs whose seat index exceeds the active-player
 * count (seats 1..5 with a g_player_strategic_352 occupancy flag set),
 * shifting later pairs down — with the non-convergent tail of quirk #106.
 *
 * Each test resets the shared globals first (they live in save.c /
 * save_schema_globals.c and persist between tests).
 */

#include <string.h>

#include "t.h"
#include "engine_roster.h"
#include "engine.h"        /* g_sim_mp_active */
#include "save.h"          /* g_player_strategic_352 */
#include "save_schema_globals.h" /* s_slot_0057c8cc / s_slot_00553fbc */

/* Zero the roster + count.  (Does not touch g_player_strategic_352;
 * compaction tests reset that explicitly.) */
static void roster_reset(void)
{
    memset(g_player_roster, 0, sizeof g_player_roster);
    g_player_roster_count = 0;
}

/* ─── player_roster_add ────────────────────────────────────────────── */

int test_roster_add_writes_pair_and_bumps_count(void)
{
    roster_reset();
    player_roster_add(7, 3);
    /* pair[3] = {army-id 7, seat 3} at bytes [6],[7]; count bumped. */
    T_ASSERT_EQ_I(g_player_roster[6], 7);
    T_ASSERT_EQ_I(g_player_roster[7], 3);
    T_ASSERT_EQ_U(g_player_roster_count, 1u);
    return 0;
}

int test_roster_add_seat_zero_does_not_bump_count(void)
{
    roster_reset();
    player_roster_add(42, 0);
    /* pair[0] = {42, 0}; count stays 0 (seat 0 is the reserved slot). */
    T_ASSERT_EQ_I(g_player_roster[0], 42);
    T_ASSERT_EQ_I(g_player_roster[1], 0);
    T_ASSERT_EQ_U(g_player_roster_count, 0u);
    return 0;
}

int test_roster_add_max_seat_index(void)
{
    roster_reset();
    player_roster_add(99, 5);
    /* pair[5] occupies the last two bytes [10],[11]. */
    T_ASSERT_EQ_I(g_player_roster[10], 99);
    T_ASSERT_EQ_I(g_player_roster[11], 5);
    T_ASSERT_EQ_U(g_player_roster_count, 1u);
    return 0;
}

int test_roster_add_count_accumulates_over_calls(void)
{
    roster_reset();
    player_roster_add(1, 1);
    player_roster_add(2, 2);
    player_roster_add(3, 0);   /* seat 0 — no bump */
    player_roster_add(4, 4);
    T_ASSERT_EQ_U(g_player_roster_count, 3u);
    /* Seat field always stores the seat index, id field the army id. */
    T_ASSERT_EQ_I(g_player_roster[2], 1);  T_ASSERT_EQ_I(g_player_roster[3], 1);
    T_ASSERT_EQ_I(g_player_roster[4], 2);  T_ASSERT_EQ_I(g_player_roster[5], 2);
    T_ASSERT_EQ_I(g_player_roster[0], 3);  T_ASSERT_EQ_I(g_player_roster[1], 0);
    T_ASSERT_EQ_I(g_player_roster[8], 4);  T_ASSERT_EQ_I(g_player_roster[9], 4);
    return 0;
}

/* ─── player_roster_clear ──────────────────────────────────────────── */

int test_roster_clear_zeroes_all_pairs_keeps_count(void)
{
    roster_reset();
    for (int i = 0; i < 12; i++) {
        g_player_roster[i] = (uint8_t)(0xA0 + i);
    }
    g_player_roster_count = 5;

    player_roster_clear();

    static const uint8_t zero[12] = {0};
    T_ASSERT_MEM_EQ(g_player_roster, zero, 12);
    /* clear() deliberately does NOT reset the count (the loader does
     * that separately before its add loop). */
    T_ASSERT_EQ_U(g_player_roster_count, 5u);
    return 0;
}

/* ─── player_roster_compact ────────────────────────────────────────── */

/* Set the active-player count by flagging seats via g_player_strategic_352.
 * `mask` bit s (s = 1..5) → mark seat s active; `use_bf07` picks which of
 * the two occupancy flags (+5 player_has_data vs +7 AI-type) to set. */
static void set_active_seats(unsigned mask, int use_bf07)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    for (int seat = 1; seat < 6; seat++) {
        if (mask & (1u << seat)) {
            g_player_strategic_352[seat * 0x160 + (use_bf07 ? 7 : 5)] = 1;
        }
    }
}

/* Helper: load all 6 pairs from a 12-byte literal. */
static void load_roster(const uint8_t pairs[12])
{
    memcpy(g_player_roster, pairs, 12);
}

int test_roster_compact_all_in_range_unchanged(void)
{
    /* active = 5; every pair's seat ≤ 5 → no removal. */
    set_active_seats(0x3E /* seats 1..5 */, 0);
    const uint8_t in[12] = { 0,0, 10,1, 20,2, 30,3, 40,4, 50,5 };
    load_roster(in);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, in, 12);
    return 0;
}

int test_roster_compact_empty_roster_unchanged(void)
{
    /* All-zero roster: every seat byte is 0, never > active. */
    set_active_seats(0x06 /* seats 1,2 */, 0);
    const uint8_t zero[12] = {0};
    load_roster(zero);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, zero, 12);
    return 0;
}

int test_roster_compact_removes_single_out_of_range_entry(void)
{
    /* active = 3.  pair[4] has seat 5 (> 3) and pair[5] is empty, so the
     * shift pulls the empty pair[5] into pair[4], cleanly dropping it. */
    set_active_seats(0x0E /* seats 1,2,3 */, 0);
    const uint8_t in[12]  = { 0,0, 10,1, 20,2, 30,3, 99,5, 0,0 };
    const uint8_t exp[12] = { 0,0, 10,1, 20,2, 30,3,  0,0, 0,0 };
    load_roster(in);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, exp, 12);
    return 0;
}

int test_roster_compact_cascade_shift_drops_middle_entry(void)
{
    /* active = 4.  pair[2] (seat 5) is out of range; the shift cascades
     * pairs 3,4,5 down into 2,3,4 leaving the in-range entries packed and
     * a zero pair trailing.  Single pass settles it (no quirk-#106 tail
     * because the offending entry is not at index 5). */
    set_active_seats(0x1E /* seats 1,2,3,4 */, 0);
    const uint8_t in[12]  = { 0,0, 11,1, 99,5, 33,3, 44,4, 0,0 };
    const uint8_t exp[12] = { 0,0, 11,1, 33,3, 44,4,  0,0, 0,0 };
    load_roster(in);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, exp, 12);
    return 0;
}

int test_roster_compact_non_convergent_tail_survives(void)
{
    /* Quirk #106: the only out-of-range entry sits at index 5 (seat 5,
     * active 2).  The shift loop (j = 5..4) copies nothing, so pair[5]
     * is never removed — yet `changed` stays true, spinning the pass
     * loop to its 6-pass cap.  Final state: roster UNCHANGED, the stale
     * entry surviving as a trailing duplicate. */
    set_active_seats(0x06 /* seats 1,2 */, 0);
    const uint8_t in[12] = { 0,0, 10,1, 20,2, 30,0, 40,0, 50,5 };
    load_roster(in);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, in, 12);
    return 0;
}

int test_roster_compact_active_count_uses_bf07_flag(void)
{
    /* The active count counts seats with EITHER occupancy flag set;
     * here only the +7 (AI-type) flag is used.  active = 2 (seats 1,2).
     * pair[3] (seat 3 > 2) at a non-tail index with an empty pair[4]
     * sandwiched — confirm the +7 flag drives the same removal the +5
     * flag would. */
    set_active_seats(0x06 /* seats 1,2 */, 1 /* use +7 */);
    const uint8_t in[12]  = { 0,0, 10,1, 20,2, 30,3, 0,0, 0,0 };
    const uint8_t exp[12] = { 0,0, 10,1, 20,2,  0,0, 0,0, 0,0 };
    load_roster(in);
    player_roster_compact();
    T_ASSERT_MEM_EQ(g_player_roster, exp, 12);
    return 0;
}

int test_roster_compact_no_active_players_drops_all_nonzero_seats(void)
{
    /* active = 0: every pair with a non-zero seat (indices 1..5) is out
     * of range.  Index-5 entry triggers the quirk-#106 tail, but the
     * front entries still get shifted out across the 6 passes.  Trace it
     * exactly. */
    set_active_seats(0x00 /* none */, 0);
    const uint8_t in[12] = { 0,0, 10,1, 20,2, 30,3, 40,4, 50,5 };
    load_roster(in);
    player_roster_compact();
    /* Hand-traced: with active=0, i=1 fires every pass and cascades the
     * later pairs forward; index 5 keeps re-triggering `changed`.  After
     * the 6-pass cap the front pairs (1..4) collapse to copies of pair[5]
     * and pair[5] keeps its stale value. */
    const uint8_t exp[12] = { 0,0, 50,5, 50,5, 50,5, 50,5, 50,5 };
    T_ASSERT_MEM_EQ(g_player_roster, exp, 12);
    return 0;
}

/* ─── player_seat_assign (FUN_0049bae9) ────────────────────────────── */

/* Aux record stride 0x2c (g_player_aux_44), strategic stride 0x160
 * (g_player_strategic_352).  Helpers index a seat's records. */
#define AUX(seat)   (&g_player_aux_44[(seat) * 0x2c])
#define STRAT(seat) (&g_player_strategic_352[(seat) * 0x160])

static void seat_reset(void)
{
    memset(g_player_aux_44, 0, sizeof g_player_aux_44);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    g_sim_mp_active = 0;
}

/* Retail always hands a ≥31-byte name buffer (0x53f1e0 / a join packet
 * field), never a short C string — strutil_bounded_copy reads exactly
 * 0x1f bytes, so the source must be that wide.  This 32-byte zero-padded
 * field stands in for one. */
static void name_field(char dst[32], const char *s)
{
    memset(dst, 0, 32);
    memcpy(dst, s, strlen(s));
}

int test_seat_assign_single_player_full(void)
{
    seat_reset();
    STRAT(2)[0x0a] = 0x37;                 /* assigned job-slot id */
    char nm[32]; name_field(nm, "Edward");
    player_seat_assign(5, 2, nm);

    T_ASSERT_EQ_I(STRAT(2)[0x05], 1);      /* player_has_data */
    T_ASSERT_EQ_I(AUX(2)[0x27], 1);        /* aux in-use flag */
    T_ASSERT_EQ_S((const char *)&AUX(2)[0x04], "Edward");  /* name copied */
    T_ASSERT_EQ_I(*(int32_t *)AUX(2), 5);  /* id stamped */
    T_ASSERT_EQ_I(AUX(2)[0x25], 0x37);     /* single-player job-slot mirror */
    return 0;
}

int test_seat_assign_id_999_keeps_existing(void)
{
    seat_reset();
    *(int32_t *)AUX(3) = 0x1234;           /* pre-existing id */
    char nm[32]; name_field(nm, "x");
    player_seat_assign(999, 3, nm);
    /* 999 sentinel: id field untouched, but occupancy/name still written. */
    T_ASSERT_EQ_I(*(int32_t *)AUX(3), 0x1234);
    T_ASSERT_EQ_I(STRAT(3)[0x05], 1);
    T_ASSERT_EQ_I(AUX(3)[0x27], 1);
    return 0;
}

int test_seat_assign_multiplayer_skips_job_mirror(void)
{
    seat_reset();
    g_sim_mp_active = 1;
    STRAT(4)[0x0a] = 0x55;                 /* would mirror in single-player */
    AUX(4)[0x25]   = 0xaa;                 /* sentinel: must stay untouched */
    char nm[32]; name_field(nm, "y");
    player_seat_assign(7, 4, nm);
    T_ASSERT_EQ_I(AUX(4)[0x25], 0xaa);     /* multiplayer leaves it alone */
    T_ASSERT_EQ_I(*(int32_t *)AUX(4), 7);  /* id still stamped */
    return 0;
}

int test_seat_assign_name_copy_is_bounded_31(void)
{
    seat_reset();
    /* 31-byte src copied with NO NUL terminator; byte 31 of dst stays. */
    AUX(1)[0x04 + 0x1f] = 0x7e;            /* sentinel just past the field */
    char src[32];
    memset(src, 'A', sizeof src);          /* 32 'A's, no NUL */
    player_seat_assign(0, 1, src);
    for (int i = 0; i < 0x1f; i++) {
        T_ASSERT_EQ_I(AUX(1)[0x04 + i], 'A');
    }
    T_ASSERT_EQ_I(AUX(1)[0x04 + 0x1f], 0x7e);  /* not overwritten */
    return 0;
}

/* ── player_first_active_nonlocal (FUN_004a1e6c) ──────────────────── */

/* Flag the given slots (1..5) occupied via the strategic +0x4 flag;
 * clears the whole strategic block first. */
static void set_active_slots(const int *slots, int n)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    for (int i = 0; i < n; i++)
        g_player_strategic_352[slots[i] * 0x160 + 0x04] = 1;
}

/* Picks the lowest occupied slot that isn't the local player. */
int test_first_active_nonlocal_skips_local(void)
{
    int slots[] = { 1, 2, 3 };
    set_active_slots(slots, 3);
    s_slot_0057c8cc = 1;                 /* local = slot 1 */
    s_slot_00553fbc = 0x77;              /* clobber result */

    player_first_active_nonlocal();

    T_ASSERT_EQ_U(s_slot_00553fbc, 2);   /* slot 1 is local → first is 2 */
    return 0;
}

/* Lowest occupied slot wins when the local player isn't among them. */
int test_first_active_nonlocal_lowest_occupied(void)
{
    int slots[] = { 3, 5 };
    set_active_slots(slots, 2);
    s_slot_0057c8cc = 1;                 /* local not occupied */

    player_first_active_nonlocal();

    T_ASSERT_EQ_U(s_slot_00553fbc, 3);
    return 0;
}

/* No occupied non-local slot → result 0.  (Only the local player is
 * occupied.) */
int test_first_active_nonlocal_none_yields_zero(void)
{
    int slots[] = { 4 };
    set_active_slots(slots, 1);
    s_slot_0057c8cc = 4;                 /* the only active slot is local */
    s_slot_00553fbc = 0x77;

    player_first_active_nonlocal();

    T_ASSERT_EQ_U(s_slot_00553fbc, 0);
    return 0;
}

/* Slot 0 is never considered even if its +0x4 flag is set. */
int test_first_active_nonlocal_ignores_slot0(void)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    g_player_strategic_352[0 * 0x160 + 0x04] = 1;   /* slot 0 occupied */
    s_slot_0057c8cc = 9;                 /* local = none of these */
    s_slot_00553fbc = 0x77;

    player_first_active_nonlocal();

    T_ASSERT_EQ_U(s_slot_00553fbc, 0);   /* slot 0 ignored, none else → 0 */
    return 0;
}
