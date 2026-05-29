/*
 * Tests for engine_npc_schedule_init (port of FUN_0049caaa).
 *
 * The routine walks the five AI seats (1..5) of g_player_strategic_352
 * and, for each unassigned seat under the worker-count limit, picks a
 * free job slot (1..5), then that job's first free work-location from
 * the per-job candidate table, copies a localized name, and finally
 * writes a per-job work-position.  Expected values below are read
 * straight off the retail .rdata table at 0x4dc17c (mirrored in
 * engine_npc_schedule.c):
 *
 *   variant 0 candidates   job1={2,1,3,4} job2={1,3,4,2} job3={2,1,3,4}
 *                          job4={4,3,2,1} job5={3,2,4,1}
 *   variant 2 candidates   job1={1,3,4,2} job2={4,1,2,3} job3={3,2,1,4}
 *                          job4={1,4,2,3} job5={2,1,3,4}
 *   work-position (x,y)    job1=(14,15) job2=(251,13) job3=(58,32)
 *                          job4=(5,253) job5=(4,240)  job0=(4,2)
 *
 * g_sim_mp_active is engine.c's runtime MP flag (retail DAT_00553030);
 * engine.c is not in the host link (it pulls Win32), so we supply the
 * definition here and drive it directly.
 */

#include <string.h>

#include "t.h"
#include "engine_npc_schedule.h"

/* Stand-in for engine.c's runtime flag (see file header). */
int g_sim_mp_active;

/* Strategic / aux record strides + field offsets, mirrored from the
 * module so the tests read the same bytes the port writes. */
#define ST 0x160
#define AX 0x2c

/* Reset all globals the routine reads/writes to a known SP, no-blob,
 * all-seats-unassigned baseline.  Caller overrides the bits it tests. */
static void npc_reset(unsigned limit)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_player_aux_44, 0, sizeof g_player_aux_44);
    g_sim_mp_active   = 0;
    s_slot_0053f268   = limit;   /* worker-count limit  */
    s_slot_0055302c   = 1;       /* != 0..3 ⇒ variant-0 candidate table */
    s_slot_0053f034   = 0;
    engine_npc_schedule_set_localization(NULL, 0);  /* skip name copy */
}

/* ─── basic assignment, variant-0 table ────────────────────────────── */

int test_npc_assigns_jobs_locs_and_positions(void)
{
    npc_reset(5);
    engine_npc_schedule_init();

    /* job slots 1..5 handed out in seat order. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x0a], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x0a], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x0a], 3);
    T_ASSERT_EQ_I(g_player_strategic_352[4*ST + 0x0a], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x0a], 5);

    /* work-locations: first-free walk of each job's candidate list.
     * seat1 job1 {2,..}→2; seat2 job2 {1,..}→1; seat3 job3 {2,1,3,..}→3;
     * seat4 job4 {4,..}→4; seat5 job5 {3,2,4,1} all taken → stays 0. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x07], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x07], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x07], 3);
    T_ASSERT_EQ_I(g_player_strategic_352[4*ST + 0x07], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x07], 0);

    /* finalize: schedule-valid flags + per-job work-position. */
    for (int s = 1; s < 6; s++) {
        T_ASSERT_EQ_I(g_player_strategic_352[s*ST + 0x29], 1);
        T_ASSERT_EQ_I(g_player_strategic_352[s*ST + 0x2a], 1);
    }
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x08], 14);
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x09], 15);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x08], 251);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x09], 13);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x08], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x09], 240);
    return 0;
}

/* ─── worker-count limit ────────────────────────────────────────────── */

int test_npc_worker_limit_stops_assignment(void)
{
    npc_reset(2);          /* only two NPCs may be scheduled */
    engine_npc_schedule_init();

    /* seats 1,2 assigned; seats 3..5 hit the limit → [+0x04] zeroed,
     * no job/work-location written (both stay 0). */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x0a], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x0a], 2);
    for (int s = 3; s < 6; s++) {
        T_ASSERT_EQ_I(g_player_strategic_352[s*ST + 0x0a], 0);
        T_ASSERT_EQ_I(g_player_strategic_352[s*ST + 0x07], 0);
        T_ASSERT_EQ_I(g_player_strategic_352[s*ST + 0x04], 0);
    }
    /* finalize still runs for the limited seats: job 0 → pos (4,2). */
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x08], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x09], 2);
    return 0;
}

/* ─── pre-reserved fixed job ────────────────────────────────────────── */

int test_npc_prereserved_job_is_skipped(void)
{
    npc_reset(5);
    /* aux[2] has a fixed job 3 ([+0x26]==0 && [+0x25]!=0). */
    g_player_aux_44[2*AX + 0x25] = 3;
    g_player_aux_44[2*AX + 0x26] = 0;
    engine_npc_schedule_init();

    /* job 3 is unavailable, so the free-slot walk skips it:
     * seat1→1, seat2→2, seat3→4, seat4→5, seat5→none (all taken). */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x0a], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x0a], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x0a], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[4*ST + 0x0a], 5);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x0a], 0);
    return 0;
}

/* ─── owned seat skipped ────────────────────────────────────────────── */

int test_npc_owned_seat_skipped(void)
{
    npc_reset(5);
    g_player_strategic_352[2*ST + 0x05] = 1;   /* seat 2 already owned */
    engine_npc_schedule_init();

    /* seat 2 keeps its (zero) job and gets no work-location; the job
     * slots flow to the remaining NPCs: seat1→1, seat3→2, seat4→3. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x0a], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x0a], 0);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x07], 0);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x0a], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[4*ST + 0x0a], 3);
    return 0;
}

/* ─── difficulty-variant candidate table ────────────────────────────── */

int test_npc_difficulty_variant_table(void)
{
    npc_reset(5);
    s_slot_0055302c = 0;   /* variant flag clear → use difficulty table */
    s_slot_0053f034 = 2;   /* variant 2 */
    engine_npc_schedule_init();

    /* Variant-2 first-free walk:
     *   seat1 job1 {1,..}→1; seat2 job2 {4,..}→4; seat3 job3 {3,..}→3;
     *   seat4 job4 {1,4,2,..}→2; seat5 job5 {2,1,3,4} all taken → 0. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x07], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x07], 4);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x07], 3);
    T_ASSERT_EQ_I(g_player_strategic_352[4*ST + 0x07], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[5*ST + 0x07], 0);
    return 0;
}

/* ─── mp_active forces variant-0 table ──────────────────────────────── */

int test_npc_mp_active_uses_variant0(void)
{
    npc_reset(5);
    s_slot_0055302c = 0;   /* would select difficulty table in SP … */
    s_slot_0053f034 = 2;
    g_sim_mp_active  = 1;   /* …but MP overrides back to variant 0. */
    engine_npc_schedule_init();

    /* Same work-locations as the variant-0 baseline test. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x07], 2);
    T_ASSERT_EQ_I(g_player_strategic_352[2*ST + 0x07], 1);
    T_ASSERT_EQ_I(g_player_strategic_352[3*ST + 0x07], 3);
    return 0;
}

/* ─── localized name copy ───────────────────────────────────────────── */

int test_npc_copies_localized_name(void)
{
    /* Minimal L2.ENG image: 8-byte header, primary offset table at
     * +8 (4-byte stride, 24-bit LE), section 7 pointed at +0x40.
     * Section 7 holds two short sub-strings then the 16-byte name. */
    static unsigned char blob[0x80];
    memset(blob, 0, sizeof blob);
    blob[8 + 7*4] = 0x40;                       /* section 7 → offset 0x40 */
    memcpy(blob + 0x40, "AB\0CD\0EFGHIJKLMNOPQRST", 22);

    npc_reset(1);          /* only seat 1 is scheduled → one copy */
    engine_npc_schedule_set_localization(blob, sizeof blob);
    engine_npc_schedule_init();

    /* seat1 job1 work-location = 2 ⇒ seek section 7, walk 2 boundaries
     * → land on 'E', copy 16 bytes into aux[1][+0x04..+0x13]. */
    T_ASSERT_EQ_I(g_player_strategic_352[1*ST + 0x07], 2);
    T_ASSERT_MEM_EQ(g_player_aux_44 + 1*AX + 0x04, "EFGHIJKLMNOPQRST", 16);
    /* seats 2..5 hit the worker limit → no copy, aux name stays zero. */
    for (int b = 0; b < 16; b++) {
        T_ASSERT_EQ_I(g_player_aux_44[2*AX + 0x04 + b], 0);
    }
    return 0;
}
