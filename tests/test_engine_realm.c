/*
 * Tests for engine_realm_advance_rating (FUN_0044baea) — the per-realm
 * approval-rating roll-forward run by the turn-advance engine.
 *
 * The pass reads/writes the per-army record block (g_per_army_768) and
 * reads the per-player strategic block (g_player_strategic_352) + the army
 * count / turn counter scalars; all are the canonical save globals (linked
 * from save.c), written directly by these tests.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_realm.h"
#include "engine_army_economy.h"   /* NULL the provision bind to isolate copies */
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))

/* One realm, owned by a human, turn 1; clean per-army + per-player state. */
static uint8_t *realm_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;       /* one realm */
    s_slot_00553240 = 1;       /* turn counter (divisor) ≥ 1 */
    return AREC(1);
}

/* Basic roll-forward: prev snapshot, three signed adjustments folded in,
 * display mirrors, per-turn delta clears, +0x2f4 decay, accumulate + average. */
int test_engine_realm_rating_rollforward(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 40;                     /* current rating */
    r[0x0e] = 3; r[0x10] = 2; r[0x11] = 1;   /* adjustment sources */
    r[0x05] = 1;                      /* owned by player 1 */
    PREC(1)[0x05] = 1;                /* player 1 is human */
    AI32(r, 0x24) = 100;             /* non-zero population */
    AI32(r, 0x2f4) = 20;             /* countdown */
    r[0x15] = r[0x17] = r[0x219] = 0xAA;     /* dirty the per-turn deltas */
    AI32(r, 0x194) = 0x1234;

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(r[0x0d], 40);       /* previous = old current */
    T_ASSERT_EQ_I(r[0x0c], 46);       /* 40 + 3 + 2 + 1 */
    T_ASSERT_EQ_I(r[0x12], 3);        /* display mirrors */
    T_ASSERT_EQ_I(r[0x14], 2);
    T_ASSERT_EQ_I(r[0x13], 1);
    T_ASSERT_EQ_I(r[0x15], 0);        /* per-turn deltas cleared */
    T_ASSERT_EQ_I(r[0x17], 0);
    T_ASSERT_EQ_I(r[0x219], 0);
    T_ASSERT_EQ_I(AI32(r, 0x194), 0);
    T_ASSERT_EQ_I(AI32(r, 0x2f4), 15); /* 20 - 5 decay */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 46);  /* lifetime sum */
    T_ASSERT_EQ_I(r[0x18], 46);        /* sum / turn(1) */
    return 0;
}

/* A zero-population, non-human-owned realm resets to 50, and (being unowned)
 * then takes the +5 nudge → 55 with delta 5. */
int test_engine_realm_rating_zero_pop_reset(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 40;
    r[0x05] = 0;                      /* unowned (owner slot 0) */
    AI32(r, 0x24) = 0;               /* zero population */
    /* PREC(0)[0x05] is 0 (reserved slot) → non-human → reset applies */

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(r[0x0c], 55);       /* reset 50, then unowned nudge +5 */
    T_ASSERT_EQ_I(r[0x17], 5);        /* nudge delta overwrote the 0x32 reset */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 55);
    T_ASSERT_EQ_I(r[0x18], 55);
    return 0;
}

/* The unowned-below-75 nudge fires at 70→75, but NOT at exactly 75. */
int test_engine_realm_rating_unowned_nudge_boundary(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 70;
    r[0x05] = 0;                      /* unowned */
    AI32(r, 0x24) = 100;             /* non-zero pop → no reset */
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 75);       /* 70 < 75 → +5 */
    T_ASSERT_EQ_I(r[0x17], 5);

    r = realm_reset();
    r[0x0c] = 75;
    r[0x05] = 0;
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 75);       /* 75 < 75 false → no nudge */
    T_ASSERT_EQ_I(r[0x17], 0);
    return 0;
}

/* Signed 8-bit accumulation: an adjustment that overflows past +127 wraps
 * negative and is caught by the low clamp → 0; the high clamp pins >100. */
int test_engine_realm_rating_signed_clamp(void)
{
    uint8_t *r = realm_reset();
    r[0x0c] = 100;
    r[0x0e] = 30;                     /* 100 + 30 = 130 → int8 wrap -126 */
    r[0x05] = 1; PREC(1)[0x05] = 1;   /* owned human → no reset/nudge */
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 0);        /* wrapped negative → clamped to 0 */
    T_ASSERT_EQ_I(AI32(r, 0x1c), 0);

    r = realm_reset();
    r[0x0c] = 98;
    r[0x0e] = 5;                      /* 103 → high clamp */
    r[0x05] = 1; PREC(1)[0x05] = 1;
    AI32(r, 0x24) = 100;
    engine_realm_advance_rating();
    T_ASSERT_EQ_I(r[0x0c], 100);
    return 0;
}

/* The running average divides the lifetime sum by the turn counter, and the
 * sum carries across turns. */
int test_engine_realm_rating_average_over_turns(void)
{
    uint8_t *r = realm_reset();
    s_slot_00553240 = 4;             /* turn 4 */
    r[0x0c] = 46;
    r[0x05] = 1; PREC(1)[0x05] = 1;
    AI32(r, 0x24) = 100;
    AI32(r, 0x1c) = 300;             /* prior accumulated sum */

    engine_realm_advance_rating();

    T_ASSERT_EQ_I(AI32(r, 0x1c), 346); /* 300 + 46 */
    T_ASSERT_EQ_I(r[0x18], 86);        /* 346 / 4 = 86 (trunc) */
    return 0;
}

/* engine_realm_settle_provisions (FUN_0044bf04): with the economy provision
 * callee NULL-bound (no-op), the per-realm history snapshots still copy
 * +0x178 → +0x18c and +0x17c → +0x190 for every realm. */
int test_engine_realm_settle_provisions_snapshots(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    s_slot_00554470 = 2;                       /* phase arg (unused — bind NULL) */
    engine_army_economy_bind_army_records(NULL);   /* provision → no-op */

    AI32(AREC(1), 0x178) = 111; AI32(AREC(1), 0x17c) = 222;
    AI32(AREC(2), 0x178) = 333; AI32(AREC(2), 0x17c) = 444;
    AI32(AREC(1), 0x18c) = 0x7777; AI32(AREC(1), 0x190) = 0x7777;  /* dirty */

    engine_realm_settle_provisions();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x18c), 111);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x190), 222);
    T_ASSERT_EQ_I(AI32(AREC(2), 0x18c), 333);
    T_ASSERT_EQ_I(AI32(AREC(2), 0x190), 444);
    return 0;
}

/* --- engine_realm_migrate_population (FUN_0044a6ba) ------------------------ */

/* Basic emigration: realm 1 (rating 10, pop 100, owned) has one neighbour,
 * realm 2 (rating 40), which out-rates it.  Emigrant count =
 * percent_of(100, percent_of(40-10, (100-10)/3)) = percent_of(100, 9) = 9.
 * The amount is our outflow (+0x3c) and the destination's inflow (+0x40); our
 * key fills every slot of the destination's source list; pass 3 records us as
 * its top contributor (+0x44 amount, +0x59 key). */
int test_engine_realm_migrate_basic(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    uint8_t *r1 = AREC(1), *r2 = AREC(2);

    r1[0x0c] = 10; r1[0x05] = 1; AI32(r1, 0x24) = 100;
    r1[0x5a] = 1; r1[0x5c] = 2;          /* one neighbour: realm 2 */
    r2[0x0c] = 40; r2[0x05] = 1;         /* receiver; no neighbours → won't emigrate */

    engine_realm_migrate_population();

    T_ASSERT_EQ_I(AI32(r1, 0x3c), 9);    /* our outflow */
    T_ASSERT_EQ_I(r1[0x58], 2);          /* destination = realm 2 */
    T_ASSERT_EQ_I(AI32(r1, 0x40), 0);    /* we received nothing */
    T_ASSERT_EQ_I(AI32(r2, 0x40), 9);    /* dest inflow total */
    T_ASSERT_EQ_I(r2[0x48], 1);          /* source list slot 0 = us */
    T_ASSERT_EQ_I(r2[0x57], 1);          /* ...and the last slot too (fill-all) */
    T_ASSERT_EQ_I(AI32(r2, 0x44), 9);    /* top-contributor amount */
    T_ASSERT_EQ_I(r2[0x59], 1);          /* top-contributor key */
    return 0;
}

/* The emigrant count caps at 100 and is then halved for an unowned realm.
 * realm 1 (rating 10, pop 1000, owner 0) → percent_of(1000, 18) = 180,
 * capped to 100, halved to 50. */
int test_engine_realm_migrate_cap_and_unowned(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    uint8_t *r1 = AREC(1), *r2 = AREC(2);

    r1[0x0c] = 10; r1[0x05] = 0; AI32(r1, 0x24) = 1000;   /* unowned */
    r1[0x5a] = 1; r1[0x5c] = 2;
    r2[0x0c] = 70; r2[0x05] = 1;

    engine_realm_migrate_population();

    T_ASSERT_EQ_I(AI32(r1, 0x3c), 50);   /* 180 → cap 100 → halved 50 */
    T_ASSERT_EQ_I(AI32(r2, 0x40), 50);
    return 0;
}

/* No neighbour out-rates this realm → no emigration, all scratch stays clear. */
int test_engine_realm_migrate_no_higher_neighbour(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    uint8_t *r1 = AREC(1), *r2 = AREC(2);

    r1[0x0c] = 80; r1[0x05] = 1; AI32(r1, 0x24) = 100;
    r1[0x5a] = 1; r1[0x5c] = 2;
    r2[0x0c] = 30; r2[0x05] = 1;         /* lower-rated; no neighbours */

    engine_realm_migrate_population();

    T_ASSERT_EQ_I(AI32(r1, 0x3c), 0);
    T_ASSERT_EQ_I(r1[0x58], 0);
    T_ASSERT_EQ_I(AI32(r2, 0x40), 0);
    return 0;
}

/* The pass-2 source-list registration has no loop break, so the FIRST realm to
 * feed a destination claims all 16 slots and later feeders are never recorded.
 * realms 1 (amount 10) and 3 (amount 42) both emigrate to realm 2; both
 * amounts sum into the inflow total, but realm 3 is absent from the source
 * list, so pass 3 reports realm 1 (10) as the top contributor — NOT realm 3
 * (42) which it would be without the quirk. */
int test_engine_realm_migrate_source_list_no_break(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;
    uint8_t *r1 = AREC(1), *r2 = AREC(2), *r3 = AREC(3);

    r1[0x0c] = 10; r1[0x05] = 1; AI32(r1, 0x24) = 50;     /* → 10 emigrants */
    r1[0x5a] = 1; r1[0x5c] = 2;
    r2[0x0c] = 80; r2[0x05] = 1;                          /* receiver */
    r3[0x0c] = 10; r3[0x05] = 1; AI32(r3, 0x24) = 200;    /* → 42 emigrants */
    r3[0x5a] = 1; r3[0x5c] = 2;

    engine_realm_migrate_population();

    T_ASSERT_EQ_I(AI32(r1, 0x3c), 10);
    T_ASSERT_EQ_I(AI32(r3, 0x3c), 42);
    T_ASSERT_EQ_I(AI32(r2, 0x40), 52);   /* both contributions sum into inflow */
    T_ASSERT_EQ_I(r2[0x48], 1);          /* realm 1 filled the whole list */
    T_ASSERT_EQ_I(r2[0x57], 1);
    T_ASSERT_EQ_I(AI32(r2, 0x44), 10);   /* quirk: realm 1, not realm 3 (42) */
    T_ASSERT_EQ_I(r2[0x59], 1);
    return 0;
}

/* --- engine_realm_collect_territory_income (FUN_004ad6c0) ------------------ */

#define TREC(i)  (&g_per_territory_420[(i) * 0x1a4])

/* Clean per-army / per-territory / per-player state with the economy callees
 * NULL-bound, so pass 3 (the per-realm re-plan) no-ops and the two income
 * accumulators reflect only passes 1-2. */
static void income_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0057c92c = 1;
    engine_army_economy_bind_army_records(NULL);   /* re-plan → no-op */
}

/* Set up territory `t` as a counted record: status (+0x08), owner (+0x00),
 * owning-realm key (+0x10), value (+0x168), gate flag (+0x198) clear. */
static void terr_set(int t, uint8_t status, int8_t owner, uint8_t realm,
                     int32_t value)
{
    uint8_t *p = TREC(t);
    p[0x08] = status;
    p[0x00] = (uint8_t)owner;
    p[0x10] = realm;
    *(int32_t *)(p + 0x168) = value;
    p[0x198] = 0;
}

/* Own-owned territory (realm owner == territory owner) → +0x198 accumulator. */
int test_engine_realm_income_own(void)
{
    income_reset();
    AREC(1)[0x05] = 5;                 /* realm 1 owned by player 5 */
    terr_set(1, 1, 5, 1, 100);         /* territory of player 5, in realm 1 */

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x198), 100);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x19c), 0);
    return 0;
}

/* Allied territory (territory owner's strategic +0x81 names the realm owner)
 * → still the +0x198 accumulator. */
int test_engine_realm_income_allied(void)
{
    income_reset();
    AREC(1)[0x05] = 7;                 /* realm 1 owned by player 7 */
    PREC(5)[0x81] = 7;                 /* player 5 is allied to player 7 */
    terr_set(1, 2, 5, 1, 60);          /* status 2 also counts */

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x198), 60);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x19c), 0);
    return 0;
}

/* Foreign territory (neither own nor allied) → +0x19c accumulator; values from
 * several territories accumulate. */
int test_engine_realm_income_foreign_and_accumulate(void)
{
    income_reset();
    AREC(1)[0x05] = 7;
    PREC(5)[0x81] = 9;                 /* player 5 allied to 9, not 7 */
    terr_set(1, 1, 5, 1, 40);          /* foreign → +0x19c */
    terr_set(2, 1, 7, 1, 25);          /* own (owner 7) → +0x198 */
    terr_set(3, 1, 5, 1, 10);          /* foreign → +0x19c (accumulate) */

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x198), 25);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x19c), 50);
    return 0;
}

/* Gates: a non-{1,2} status and a set gate flag (+0x198) both exclude the
 * territory entirely. */
int test_engine_realm_income_gates(void)
{
    income_reset();
    AREC(1)[0x05] = 5;
    terr_set(1, 3, 5, 1, 100);         /* status 3 → excluded */
    terr_set(2, 1, 5, 1, 200);
    TREC(2)[0x198] = 1;                /* gate flag set → excluded */

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x198), 0);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x19c), 0);
    return 0;
}

/* Pass 1 clears realms 1..16 (fixed bound, not the live count); realm 0 is
 * left alone, and the territory scan starts at index 1 (index 0 is skipped). */
int test_engine_realm_income_clears_fixed_bounds(void)
{
    income_reset();
    AI32(AREC(0), 0x198) = 0x1111; AI32(AREC(0), 0x19c) = 0x2222;  /* untouched */
    AI32(AREC(1), 0x198) = 0x3333;
    AI32(AREC(16), 0x19c) = 0x4444;
    /* Territory at index 0 must be ignored even though it is "counted". */
    terr_set(0, 1, 3, 2, 500);
    AREC(2)[0x05] = 3;                 /* would match if index 0 were scanned */

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x198), 0);       /* cleared */
    T_ASSERT_EQ_I(AI32(AREC(16), 0x19c), 0);      /* cleared (bound reaches 16) */
    T_ASSERT_EQ_I(AI32(AREC(2), 0x198), 0);       /* territory 0 not scanned */
    T_ASSERT_EQ_I(AI32(AREC(0), 0x198), 0x1111);  /* realm 0 untouched */
    T_ASSERT_EQ_I(AI32(AREC(0), 0x19c), 0x2222);
    return 0;
}

/* Pass 3 wiring: with the economy block bound, the re-plan runs for realms
 * 1..16 — each callee clears a field up front (plan +0x22c, select +0x258),
 * so those land 0 for visited realms while realm 0 (never visited) stays
 * dirty.  +0xb8 left 0 → plan/select take the gated-off path (clear only). */
int test_engine_realm_income_replan_wiring(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0057c92c = 1;
    engine_army_economy_bind_army_records(g_per_army_768);

    AI32(AREC(0), 0x22c) = 0x55;   AI32(AREC(0), 0x258) = 0x55;   /* not visited */
    AI32(AREC(1), 0x22c) = 0x66;   AI32(AREC(1), 0x258) = 0x66;
    AI32(AREC(16), 0x22c) = 0x77;  AI32(AREC(16), 0x258) = 0x77;

    engine_realm_collect_territory_income();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x22c), 0);    /* plan_production cleared it */
    T_ASSERT_EQ_I(AI32(AREC(1), 0x258), 0);    /* select_candidate cleared it */
    T_ASSERT_EQ_I(AI32(AREC(16), 0x22c), 0);
    T_ASSERT_EQ_I(AI32(AREC(16), 0x258), 0);
    T_ASSERT_EQ_I(AI32(AREC(0), 0x22c), 0x55); /* realm 0 untouched */

    engine_army_economy_bind_army_records(NULL);
    return 0;
}

/* ── Per-turn realm stat-history recorder (FUN_004ae7dd) ──────────────
 * Writes each realm's population (army +0x24) and approval (army +0x0c) into
 * the current slot of the history ring g_realm_history, then advances the three
 * ring cursors.  Record for (slot, realm key) sits at slot*0x80 + (key-1)*8;
 * +0 dword = population, +4 byte = approval, +5..+7 untouched. */

#define HREC(slot, key)  (g_realm_history + (slot) * 0x80 + ((key) - 1) * 8)

/* Realms 1 and 16 land in the right records of the chosen slot; the +5..+7
 * tail is preserved, and the count / write-index cursors each advance once
 * (page untouched while the ring is not yet full). */
int test_engine_realm_history_records_slot(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_realm_history, 0xCD, sizeof g_realm_history);  /* dirty → tail visible */
    s_slot_0055300c = 2;        /* write slot 2  */
    s_slot_00553f30 = 0;        /* count         */
    s_slot_00568da8 = 0;        /* oldest page   */

    AI32(AREC(1),  0x24) = 0x11223344;  AREC(1)[0x0c]  = 0x55;
    AI32(AREC(16), 0x24) = 0x0a0b0c0d;  AREC(16)[0x0c] = 0x77;

    engine_realm_record_history();

    uint8_t *r1 = HREC(2, 1);
    T_ASSERT_EQ_I(*(int32_t *)(r1 + 0), 0x11223344);   /* population */
    T_ASSERT_EQ_U(r1[4], 0x55);                         /* approval   */
    T_ASSERT_EQ_U(r1[5], 0xCD);                         /* +5 tail preserved */

    uint8_t *r16 = HREC(2, 16);
    T_ASSERT_EQ_I(*(int32_t *)(r16 + 0), 0x0a0b0c0d);
    T_ASSERT_EQ_U(r16[4], 0x77);

    T_ASSERT_EQ_U(s_slot_00553f30, 1);   /* count 0 → 1            */
    T_ASSERT_EQ_U(s_slot_0055300c, 3);   /* write index 2 → 3      */
    T_ASSERT_EQ_U(s_slot_00568da8, 0);   /* page unchanged (not full) */
    return 0;
}

/* Count reaching exactly 400 does NOT advance the page — the engine clamps
 * only when count would *exceed* 400 (the `> 400` boundary, not `>=`). */
int test_engine_realm_history_count_boundary(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0055300c = 0;
    s_slot_00553f30 = 399;
    s_slot_00568da8 = 7;

    engine_realm_record_history();

    T_ASSERT_EQ_U(s_slot_00553f30, 400);  /* 399 → 400 exactly */
    T_ASSERT_EQ_U(s_slot_00568da8, 7);    /* page NOT advanced  */
    return 0;
}

/* Once the ring is full (count already 400), each turn clamps count, walks the
 * oldest page forward, and wraps the write index from 399 back to 0. */
int test_engine_realm_history_cursor_wrap(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0055300c = 399;      /* last slot → wraps    */
    s_slot_00553f30 = 400;      /* already saturated    */
    s_slot_00568da8 = 5;        /* oldest page advances */

    engine_realm_record_history();

    T_ASSERT_EQ_U(s_slot_00553f30, 400);  /* clamped              */
    T_ASSERT_EQ_U(s_slot_00568da8, 6);    /* page 5 → 6           */
    T_ASSERT_EQ_U(s_slot_0055300c, 0);    /* write index wrapped  */
    return 0;
}

/* The oldest-page pointer wraps at 400 too (399 → 0). */
int test_engine_realm_history_page_wraps(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0055300c = 10;
    s_slot_00553f30 = 400;      /* saturated → page walks */
    s_slot_00568da8 = 399;      /* last page → wraps      */

    engine_realm_record_history();

    T_ASSERT_EQ_U(s_slot_00568da8, 0);    /* page wrapped         */
    T_ASSERT_EQ_U(s_slot_0055300c, 11);   /* write index advanced */
    return 0;
}
