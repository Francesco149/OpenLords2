/*
 * Tests for engine_ranking_recompute (player standings, FUN_0049aa0e).
 *
 * The recompute reads the 6 per-player strategic records
 * (g_player_strategic_352, stride 0x160) and writes the leaderboard
 * result scalars, per-player scores/ranks, and — for a single survivor
 * — a victory event into the threaded event queue.  Each test seeds a
 * minimal world, runs the recompute, and asserts the documented
 * post-condition.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_ranking.h"
#include "engine_event_queue.h"
#include "save.h"                 /* g_player_strategic_352 */
#include "save_schema_globals.h"  /* leaderboard result scalars + board */

#define REC(p)        (&g_player_strategic_352[(p) * 0x160])
#define REC_I32(p,o)  (*(int32_t *)&g_player_strategic_352[(p) * 0x160 + (o)])

/* Field offsets (mirror engine_ranking.c). */
#define F_ACTIVE   0x04
#define F_HAS_DATA 0x05
#define F_OPP_TYPE 0x07
#define F_M_0C     0x0c
#define F_M_10     0x10
#define F_M_29     0x29
#define F_RANK     0x2b
#define F_M_2C     0x2c
#define F_M_4C     0x4c
#define F_SCORE    0x50
#define F_M_54     0x54
#define F_M_58     0x58
#define F_M_60     0x60
#define F_VICTORY  0xed
#define F_M_118    0x118
#define F_VICT_ROT 0x159
/* per-turn summary metrics (current; previous-turn companion = current+4) */
#define F_M_120    0x120
#define F_M_128    0x128
#define F_M_130    0x130
#define F_TOTAL    0x138
#define F_CAT0     0x140
#define F_PREV     0x04

static void clear_world(void)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_00565410, 0, sizeof s_slot_00565410);
    s_slot_0057c8cc = 0;
}

static void set_active(int p) { REC(p)[F_ACTIVE] = 1; }

/* Phase B — per-metric leader + max, with strict-`<` tie-break. */
int test_engine_ranking_leaders_and_ties(void)
{
    clear_world();
    set_active(1); set_active(2); set_active(3);

    REC(1)[F_M_29] = 10; REC(2)[F_M_29] = 30; REC(3)[F_M_29] = 20;  /* max 30 @2 */
    REC_I32(1, F_M_10) = 100; REC_I32(2, F_M_10) = 50; REC_I32(3, F_M_10) = 200; /* max 200 @3 */
    REC(1)[F_M_0C] = 5; REC(2)[F_M_0C] = 5; REC(3)[F_M_0C] = 1;     /* tie 5 → first @1 */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_U(s_slot_00522f68, 30); T_ASSERT_EQ_U(s_slot_00553028, 2);
    T_ASSERT_EQ_U(s_slot_0056d644, 200); T_ASSERT_EQ_U(s_slot_0056d6a8, 3);
    T_ASSERT_EQ_U(s_slot_0052f0a4, 5);  T_ASSERT_EQ_U(s_slot_0053f058, 1);
    return 0;
}

/* Phase B — active count excludes the local player. */
int test_engine_ranking_active_count_excludes_local(void)
{
    clear_world();
    set_active(1); set_active(2); set_active(3);
    s_slot_0057c8cc = 2;   /* local player = slot 2 */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_U(s_slot_0056d5d8, 2);   /* slots 1 and 3 */
    return 0;
}

/* Phase C — composite score = the weighted sum of the six inputs. */
int test_engine_ranking_score_weights(void)
{
    clear_world();
    set_active(1);
    REC(1)[F_M_60] = 2;        /* ×10 = 20 */
    REC_I32(1, F_M_10) = 100;  /* ÷10 = 10 */
    REC(1)[F_M_0C] = 3;        /* ×2  = 6  */
    REC(1)[F_M_58] = 4;        /* ×2  = 8  */
    REC_I32(1, F_M_54) = 50;   /* ÷5  = 10 */
    REC(1)[F_M_4C] = 1;        /* ×50 = 50 */
    REC_I32(1, F_M_118) = 0;   /* band: ≤2000 → +0 */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_I(REC_I32(1, F_SCORE), 20 + 10 + 6 + 8 + 10 + 50 + 0);
    return 0;
}

/* Phase C — the +0x118 band: only `>= 2001 → +50` is live; the higher
 * bands (+100 / +200) are dead code in the binary. */
int test_engine_ranking_score_band_dead_arms(void)
{
    const struct { int32_t metric; int32_t want; } cases[] = {
        { 2000,  0  },   /* ≤2000 → +0  */
        { 2001,  50 },   /* ≥2001 → +50 */
        { 8000,  50 },   /* NOT +100 (dead) */
        { 20000, 50 },   /* NOT +200 (dead) */
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        clear_world();
        set_active(1);
        REC_I32(1, F_M_118) = cases[k].metric;

        engine_event_queue_state_t evq;
        engine_event_queue_init(&evq);
        engine_ranking_recompute(&evq);

        if (REC_I32(1, F_SCORE) != cases[k].want)
            T_FAIL("metric %d → score %d, want %d",
                   cases[k].metric, REC_I32(1, F_SCORE), cases[k].want);
    }
    return 0;
}

/* Phases D–F — descending sort, 1-based ranks, winner/last detection. */
int test_engine_ranking_sort_ranks_winner(void)
{
    clear_world();
    set_active(1); set_active(2); set_active(3);
    REC(1)[F_M_60] = 1;   /* score 10 */
    REC(2)[F_M_60] = 3;   /* score 30 */
    REC(3)[F_M_60] = 2;   /* score 20 */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    /* Descending by score: slot2(30) > slot3(20) > slot1(10). */
    T_ASSERT_EQ_U(REC(2)[F_RANK], 1);
    T_ASSERT_EQ_U(REC(3)[F_RANK], 2);
    T_ASSERT_EQ_U(REC(1)[F_RANK], 3);

    /* Board front = winner, indices sorted by score. */
    T_ASSERT_EQ_I(*(int32_t *)&s_slot_00565410[0 * 8], 2);
    T_ASSERT_EQ_I(*(int32_t *)&s_slot_00565410[1 * 8], 3);
    T_ASSERT_EQ_I(*(int32_t *)&s_slot_00565410[2 * 8], 1);

    T_ASSERT_EQ_U(s_slot_00553d24, 2);   /* winner (first ranked) */
    T_ASSERT_EQ_U(s_slot_00522d90, 1);   /* last ranked           */
    return 0;
}

/* Phase E — inactive slots get rank 0 and a zeroed score. */
int test_engine_ranking_inactive_cleared(void)
{
    clear_world();
    set_active(1); set_active(2);
    REC(1)[F_M_60] = 2;
    REC(2)[F_M_60] = 1;
    REC(3)[F_RANK] = 9;   /* dirty: must be cleared even though inactive */
    REC_I32(3, F_SCORE) = 1234;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_U(REC(3)[F_RANK], 0);
    T_ASSERT_EQ_I(REC_I32(3, F_SCORE), 0);
    T_ASSERT(REC(1)[F_RANK] != 0 && REC(2)[F_RANK] != 0);
    return 0;
}

/* Phase G — single AI survivor fires the 0xc3 victory event: latches the
 * victory flag, posts with the rot/opp-type payload, bumps+caps rot. */
int test_engine_ranking_victory_event_ai(void)
{
    clear_world();
    set_active(1);
    REC(1)[F_HAS_DATA] = 0;   /* AI */
    REC(1)[F_VICTORY]  = 0;
    REC(1)[F_OPP_TYPE] = 2;
    REC(1)[F_VICT_ROT] = 1;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    /* Broadcast (audience 0) → published. */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.notify_flag, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xc3);
    T_ASSERT_EQ_I(evq.ring[0].target, 1);
    T_ASSERT_EQ_I(evq.ring[0].audience, 0);
    /* payload = (rot - 4) + opp_type*4 = (1-4) + 2*4 = 5 */
    T_ASSERT_EQ_I(evq.ring[0].payload32a, 5);

    T_ASSERT_EQ_U(REC(1)[F_VICTORY], 1);
    T_ASSERT_EQ_U(REC(1)[F_VICT_ROT], 2);   /* 1 → 2, no wrap */
    return 0;
}

/* Phase G — rot wraps 3 → 0 after the increment. */
int test_engine_ranking_victory_rot_wrap(void)
{
    clear_world();
    set_active(1);
    REC(1)[F_HAS_DATA] = 0;
    REC(1)[F_VICT_ROT] = 3;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_U(REC(1)[F_VICT_ROT], 0);   /* 3 → 4 → wrap 0 */
    return 0;
}

/* Phase G — a human survivor (or already-latched) takes the 0xe1 branch:
 * targeted at the local player, rot untouched. */
int test_engine_ranking_victory_event_human(void)
{
    clear_world();
    set_active(1);
    REC(1)[F_HAS_DATA] = 1;     /* human → else branch */
    REC(1)[F_VICT_ROT] = 1;
    s_slot_0057c8cc = 1;        /* local player; audience == local → published */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe1);
    T_ASSERT_EQ_I(evq.ring[0].target, 0);
    T_ASSERT_EQ_I(evq.ring[0].audience, 1);
    T_ASSERT_EQ_U(REC(1)[F_VICTORY], 1);
    T_ASSERT_EQ_U(REC(1)[F_VICT_ROT], 1);   /* untouched in this branch */
    return 0;
}

/* Phase G — with more than one ranked player the winner != last, so no
 * victory event is posted. */
int test_engine_ranking_no_event_multiplayer(void)
{
    clear_world();
    set_active(1); set_active(2);
    REC(1)[F_M_60] = 2;
    REC(2)[F_M_60] = 1;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_ranking_recompute(&evq);

    T_ASSERT_EQ_I(evq.write_idx, 0);
    T_ASSERT_EQ_I(evq.notify_flag, 0);
    return 0;
}

/* --- FUN_004487a9: total = Σ of the six category sub-accounts, the
 * stale prior total is overwritten. */
int test_engine_ranking_recompute_player_total(void)
{
    clear_world();
    for (int j = 0; j < 6; j++)
        REC_I32(2, F_CAT0 + j * 4) = (j + 1) * 100;   /* 100+...+600 = 2100 */
    REC_I32(2, F_TOTAL) = 999999;                     /* stale, must be replaced */

    engine_ranking_recompute_player_total(2);

    T_ASSERT_EQ_I(REC_I32(2, F_TOTAL), 2100);
    return 0;
}

/* --- FUN_004486a9: each summary metric rolls into its +4 companion and
 * the total resyncs from categories.  The +0x138→+0x13c copy precedes
 * the recompute, so the companion holds last turn's total (555), not the
 * fresh category sum (60). */
int test_engine_ranking_snapshot_player_metrics(void)
{
    clear_world();
    REC_I32(3, F_M_118) = 11;
    REC_I32(3, F_M_120) = 22;
    REC_I32(3, F_M_128) = 33;
    REC_I32(3, F_M_130) = 44;
    REC_I32(3, F_TOTAL) = 555;                       /* old total */
    for (int j = 0; j < 6; j++) REC_I32(3, F_CAT0 + j * 4) = 10;  /* sum 60 */

    engine_ranking_snapshot_player_metrics();

    T_ASSERT_EQ_I(REC_I32(3, F_M_118 + F_PREV), 11);
    T_ASSERT_EQ_I(REC_I32(3, F_M_120 + F_PREV), 22);
    T_ASSERT_EQ_I(REC_I32(3, F_M_128 + F_PREV), 33);
    T_ASSERT_EQ_I(REC_I32(3, F_M_130 + F_PREV), 44);
    T_ASSERT_EQ_I(REC_I32(3, F_TOTAL + F_PREV), 555);   /* old total, not 60 */
    T_ASSERT_EQ_I(REC_I32(3, F_TOTAL), 60);             /* resynced from categories */
    T_ASSERT_EQ_I(REC_I32(3, F_M_118), 11);             /* current itself unchanged */
    return 0;
}

/* --- FUN_004486a9 has no active-slot filter: all of slots 1..5 roll
 * unconditionally, and slot 0 (loop starts at 1) is left untouched. */
int test_engine_ranking_snapshot_all_slots_no_filter(void)
{
    clear_world();
    for (int p = 0; p <= 5; p++)
        for (int j = 0; j < 6; j++) REC_I32(p, F_CAT0 + j * 4) = 1;  /* sum 6 */
    REC_I32(0, F_TOTAL) = 12345;                     /* slot 0 sentinel */

    engine_ranking_snapshot_player_metrics();

    for (int p = 1; p <= 5; p++)
        T_ASSERT_EQ_I(REC_I32(p, F_TOTAL), 6);       /* every slot rolled, no filter */
    T_ASSERT_EQ_I(REC_I32(0, F_TOTAL), 12345);       /* slot 0 untouched */
    return 0;
}
