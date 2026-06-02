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
#include "engine_territory.h"      /* engine_territory_bind_cells */
#include "engine_event_queue.h"    /* engine_event_queue_init / state */
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))
#define AW16(p, o)  (*(int16_t *)((p) + (o)))
#define MISC_ROW(realm) ((int32_t *)&g_misc_1360[(realm) * 0x50])

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

/* clear_turn_scratch (FUN_00448d16): the four target bytes
 * (+0x1fb/+0x1fc/+0x1fd/+0x1a8) zero for realms 1..count; their immediate
 * neighbours are preserved, and a realm past the count is left untouched. */
int test_engine_realm_clear_turn_scratch(void)
{
    memset(g_per_army_768, 0xCD, sizeof g_per_army_768);  /* dirty all rows */
    s_slot_0056d5dc = 2;        /* realms 1..2 in scope */

    engine_realm_clear_turn_scratch();

    for (int key = 1; key <= 2; key++) {
        uint8_t *r = AREC(key);
        T_ASSERT_EQ_U(r[0x1fb], 0);     /* cleared */
        T_ASSERT_EQ_U(r[0x1fc], 0);
        T_ASSERT_EQ_U(r[0x1fd], 0);
        T_ASSERT_EQ_U(r[0x1a8], 0);
        T_ASSERT_EQ_U(r[0x1fa], 0xCD);  /* neighbours preserved */
        T_ASSERT_EQ_U(r[0x1fe], 0xCD);
        T_ASSERT_EQ_U(r[0x1a7], 0xCD);
        T_ASSERT_EQ_U(r[0x1a9], 0xCD);
    }

    uint8_t *r3 = AREC(3);              /* realm 3 > count → untouched */
    T_ASSERT_EQ_U(r3[0x1fb], 0xCD);
    T_ASSERT_EQ_U(r3[0x1a8], 0xCD);
    return 0;
}

/* ─── engine_realm_update_mood (FUN_00449889) ───────────────────────────── */

/* Clean slate for the mood machine: one realm, event-mode 3 (no band-1/2
 * demotion and no event-mode mood delta), local player 7 (no realm owned by
 * it by default → no notifications), standings enabled.  Cell grid unbound so
 * marker flips are no-ops. */
static void mood_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(g_misc_1360, 0, sizeof g_misc_1360);
    engine_territory_bind_cells(NULL);
    s_slot_0056d5dc = 1;        /* realm count       */
    s_slot_0057c934 = 3;        /* event-mode        */
    s_slot_0057c8cc = 7;        /* local player      */
    s_slot_00554020 = 0;        /* previous focus    */
    s_slot_0053f25c = 1;        /* standings enabled */
}

/* The five accumulator bands and the two that also reset the accumulator;
 * mode 3 + rng_b = 96 makes the base delta exactly 0 (12 − 96/8) so the
 * accumulators stay as set, and mode 3 neither demotes nor adds a mood delta.
 * The previous-level snapshot (+0x21c ← +0x21b) is taken before reclassify. */
int test_engine_realm_mood_band_classify(void)
{
    mood_reset();
    s_slot_0056d5dc = 5;
    AREC(1)[0x21d] = 4;     AREC(1)[0x21b] = 2;   /* <5   → 5, acc→30 */
    AREC(2)[0x21d] = 19;    AREC(2)[0x21b] = 1;   /* <20  → 4         */
    AREC(3)[0x21d] = 69;    AREC(3)[0x21b] = 9;   /* <70  → 3         */
    AREC(4)[0x21d] = 94;    AREC(4)[0x21b] = 0;   /* <95  → 2         */
    AREC(5)[0x21d] = 95;    AREC(5)[0x21b] = 4;   /* >=95 → 1, acc→70 */

    engine_realm_update_mood(96, 0, NULL);

    T_ASSERT_EQ_U(AREC(1)[0x21b], 5);   T_ASSERT_EQ_U(AREC(1)[0x21d], 0x1e);
    T_ASSERT_EQ_U(AREC(2)[0x21b], 4);   T_ASSERT_EQ_U(AREC(2)[0x21d], 19);
    T_ASSERT_EQ_U(AREC(3)[0x21b], 3);   T_ASSERT_EQ_U(AREC(3)[0x21d], 69);
    T_ASSERT_EQ_U(AREC(4)[0x21b], 2);   T_ASSERT_EQ_U(AREC(4)[0x21d], 94);
    T_ASSERT_EQ_U(AREC(5)[0x21b], 1);   T_ASSERT_EQ_U(AREC(5)[0x21d], 0x46);

    /* previous-level snapshots = the pre-pass +0x21b values */
    T_ASSERT_EQ_U(AREC(1)[0x21c], 2);
    T_ASSERT_EQ_U(AREC(3)[0x21c], 9);
    T_ASSERT_EQ_U(AREC(5)[0x21c], 4);
    return 0;
}

/* Base mood delta per event-mode: BASE − rng_b/8 (signed truncating div),
 * applied once to every realm.  Realm 1 is kept off the focus (focus = realm
 * 2 via rng_a = 2) so it takes only the base dose; its accumulator is preset
 * to 50 so each result lands in a non-resetting band and is read back raw. */
int test_engine_realm_mood_base_delta_per_mode(void)
{
    struct { int mode; uint32_t rng_b; int expect; } cases[] = {
        { 1, 0,  58 },   /* 50 + (8 − 0)        */
        { 1, 80, 48 },   /* 50 + (8 − 10) = −2  */
        { 2, 0,  74 },   /* 50 + 24             */
        { 3, 0,  62 },   /* 50 + 12             */
        { 4, 0,  38 },   /* 50 + (−12)          */
        { 4, 24, 35 },   /* 50 + (−12 − 3)      */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mood_reset();
        s_slot_0056d5dc = 2;
        s_slot_0057c934 = cases[i].mode;
        AREC(1)[0x21d] = 50;
        engine_realm_update_mood(cases[i].rng_b, 2, NULL);
        T_ASSERT_EQ_I((int8_t)AREC(1)[0x21d], cases[i].expect);
    }
    return 0;
}

/* Focus realm takes a SECOND base dose; its neighbours (list at +0x5c,
 * count +0x5a) take a half-dose; the chosen focus is latched in
 * s_slot_00554020.  Mode 3, rng_b = 0 → base delta 12, half = 6, mood delta 0. */
int test_engine_realm_mood_focus_and_neighbours(void)
{
    mood_reset();
    s_slot_0056d5dc = 4;
    AREC(2)[0x5a] = 2;          /* focus realm 2 has two neighbours */
    AREC(2)[0x5c] = 3;
    AREC(2)[0x5d] = 4;

    engine_realm_update_mood(0, 2, NULL);   /* rng_a low nibble 2 → focus 2 */

    T_ASSERT_EQ_U(s_slot_00554020, 2);      /* focus latched */
    T_ASSERT_EQ_I((int8_t)AREC(1)[0x21d], 12);   /* base only        */
    T_ASSERT_EQ_I((int8_t)AREC(2)[0x21d], 24);   /* base + focus     */
    T_ASSERT_EQ_I((int8_t)AREC(3)[0x21d], 18);   /* base + half      */
    T_ASSERT_EQ_I((int8_t)AREC(4)[0x21d], 18);   /* base + half      */
    return 0;
}

/* Focus selection fallback chain: an out-of-range nibble falls back to
 * previous-focus + 1, then to realm 1, all clamped into 1..count. */
int test_engine_realm_mood_focus_fallback(void)
{
    /* nibble 5 > count 3 → previous(1)+1 = 2 */
    mood_reset();
    s_slot_0056d5dc = 3;
    s_slot_00554020 = 1;
    engine_realm_update_mood(0, 5, NULL);
    T_ASSERT_EQ_U(s_slot_00554020, 2);

    /* nibble 5 > count 3, previous(3)+1 = 4 > count → realm 1 */
    mood_reset();
    s_slot_0056d5dc = 3;
    s_slot_00554020 = 3;
    engine_realm_update_mood(0, 5, NULL);
    T_ASSERT_EQ_U(s_slot_00554020, 1);

    /* in-range nibble used directly */
    mood_reset();
    s_slot_0056d5dc = 3;
    engine_realm_update_mood(0, 2, NULL);
    T_ASSERT_EQ_U(s_slot_00554020, 2);
    return 0;
}

/* The event-mode mood delta (FUN_00449d6e) folded into the focus realm:
 * mode 2 keys disposition 0→+4, 1→+2, 2→−8, 4→−12 (disposition 3 unhandled
 * → 0), and the second disp==4 arm is dead (−12, not −24).  Focus realm 2
 * accumulator = 0 + 24(base) + 24(focus base) + delta. */
int test_engine_realm_mood_delta_mode2(void)
{
    struct { uint8_t disp; int delta; } cases[] = {
        { 0, 4 }, { 1, 2 }, { 2, -8 }, { 3, 0 }, { 4, -12 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mood_reset();
        s_slot_0056d5dc = 2;
        s_slot_0057c934 = 2;            /* mode 2: base 24 */
        AREC(2)[0x21e] = cases[i].disp;
        engine_realm_update_mood(0, 2, NULL);   /* focus realm 2 */
        T_ASSERT_EQ_I((int8_t)AREC(2)[0x21d], 48 + cases[i].delta);
    }
    return 0;
}

/* Mode 4 mood delta: disposition 1→−2, 2→−4, 3→−6, 4→−10 (0/other → 0).
 * Focus realm 2 acc = 80 + (−12) + (−12) + delta = 56 + delta. */
int test_engine_realm_mood_delta_mode4(void)
{
    struct { uint8_t disp; int delta; } cases[] = {
        { 0, 0 }, { 1, -2 }, { 2, -4 }, { 3, -6 }, { 4, -10 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mood_reset();
        s_slot_0056d5dc = 2;
        s_slot_0057c934 = 4;            /* mode 4: base −12 */
        AREC(2)[0x21d] = 80;
        AREC(2)[0x21e] = cases[i].disp;
        engine_realm_update_mood(0, 2, NULL);
        T_ASSERT_EQ_I((int8_t)AREC(2)[0x21d], 56 + cases[i].delta);
    }
    return 0;
}

/* In modes 1 and 4 the two best bands are demoted toward collapse: band 1
 * always → 0, band 2 → 0 only when the accumulator still exceeds 74.  Mode 1
 * + rng_b = 64 makes the base delta 0 so the preset accumulators are kept. */
int test_engine_realm_mood_mode1_demotion(void)
{
    mood_reset();
    s_slot_0056d5dc = 3;
    s_slot_0057c934 = 1;
    AREC(1)[0x21d] = 95;        /* band 1            → 0 */
    AREC(2)[0x21d] = 80;        /* band 2, >74       → 0 */
    AREC(3)[0x21d] = 72;        /* band 2, not >74   → 2 */

    engine_realm_update_mood(64, 1, NULL);

    T_ASSERT_EQ_U(AREC(1)[0x21b], 0);
    T_ASSERT_EQ_U(AREC(2)[0x21b], 0);
    T_ASSERT_EQ_U(AREC(3)[0x21b], 2);
    return 0;
}

/* A collapsed (band 1) and a peaked (band 5) realm owned by the local player
 * each post a notification (codes 0x8f / 0x90) and stamp one roster cell with
 * a standing marker (0x18 / 0x17).  Mode 3 keeps the bands (no demotion) and
 * the base delta is 0 (rng_b = 96).  Cell grid bound to the overworld map so
 * the flips are observable. */
int test_engine_realm_mood_notify_and_markers(void)
{
    mood_reset();
    engine_territory_bind_cells(g_overworld_map);
    s_slot_0056d5dc = 2;
    s_slot_0057c8cc = 1;            /* local player owns both realms */
    AREC(1)[0x05] = 1;  AREC(1)[0x21d] = 95;   /* band 1 (collapse) */
    AREC(2)[0x05] = 1;  AREC(2)[0x21d] = 4;    /* band 5 (peak)     */

    /* Each realm's roster points its first slot at a distinct overworld cell;
     * cursor wrap = 1 keeps the round-robin pinned on slot 0. */
    AREC(1)[0x205] = 1;  MISC_ROW(1)[0] = 500;
    AREC(2)[0x205] = 1;  MISC_ROW(2)[0] = 600;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_realm_update_mood(96, 0, &evq);

    /* realm 1 processed first → ring[0] = 0x8f, realm 2 → ring[1] = 0x90 */
    T_ASSERT_EQ_I(evq.notify_flag, 1);
    T_ASSERT_EQ_I(evq.write_idx, 2);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x8f);
    T_ASSERT_EQ_I(evq.ring[0].audience, 1);
    T_ASSERT_EQ_I(evq.ring[1].code, 0x90);

    T_ASSERT_EQ_U(g_overworld_map[500], 0x18);   /* collapse marker */
    T_ASSERT_EQ_U(g_overworld_map[600], 0x17);   /* peak marker     */

    engine_territory_bind_cells(NULL);            /* restore global state */
    return 0;
}

/* A collapsed realm whose owner is NOT the local player posts nothing but
 * still stamps the collapse marker. */
int test_engine_realm_mood_marker_without_notify(void)
{
    mood_reset();
    engine_territory_bind_cells(g_overworld_map);
    s_slot_0057c8cc = 7;                    /* local player owns nothing */
    AREC(1)[0x05] = 1;  AREC(1)[0x21d] = 95;   /* band 1, owned by player 1 */
    AREC(1)[0x205] = 1; MISC_ROW(1)[0] = 700;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_realm_update_mood(96, 0, &evq);

    T_ASSERT_EQ_I(evq.notify_flag, 0);          /* no post */
    T_ASSERT_EQ_I(evq.write_idx, 0);
    T_ASSERT_EQ_U(g_overworld_map[700], 0x18);  /* marker still stamped */

    engine_territory_bind_cells(NULL);
    return 0;
}

/* The standing-marker clear (FUN_0046942c) resets any roster cell already of
 * type 0x17/0x18 back to 0 before re-marking.  A band-3 realm (no re-mark)
 * with a pre-existing 0x17 cell ends with that cell cleared. */
int test_engine_realm_mood_clears_old_markers(void)
{
    mood_reset();
    engine_territory_bind_cells(g_overworld_map);
    s_slot_0056d5dc = 1;
    AREC(1)[0x21d] = 40;                /* band 3 → no new marker */
    MISC_ROW(1)[0] = 800;
    g_overworld_map[800] = 0x17;        /* stale peak marker */

    engine_realm_update_mood(96, 0, NULL);

    T_ASSERT_EQ_U(g_overworld_map[800], 0);    /* cleared back to 0 */
    T_ASSERT_EQ_U(AREC(1)[0x21b], 3);
    engine_territory_bind_cells(NULL);
    return 0;
}

/* Standings disabled (s_slot_0053f25c == 0) forces every realm to the neutral
 * middle band (3) regardless of its accumulator. */
int test_engine_realm_mood_standings_disabled(void)
{
    mood_reset();
    s_slot_0056d5dc = 3;
    s_slot_0053f25c = 0;
    AREC(1)[0x21d] = 95;       /* would be band 1 */
    AREC(2)[0x21d] = 4;        /* would be band 5 */
    AREC(3)[0x21d] = 40;       /* band 3          */

    engine_realm_update_mood(96, 0, NULL);

    T_ASSERT_EQ_U(AREC(1)[0x21b], 3);
    T_ASSERT_EQ_U(AREC(2)[0x21b], 3);
    T_ASSERT_EQ_U(AREC(3)[0x21b], 3);
    return 0;
}

/* ─── engine_realm_accrue_income (FUN_0044b59b) + owner tally (FUN_0044b99a) ─ */

/* The owner tally sums each owned realm's +0x16 byte into the player's
 * strategic +0x28, clearing it first and ignoring realms owned by others. */
int test_engine_realm_owner_tally(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 3;
    AREC(1)[0x05] = 1; AREC(1)[0x16] = 10;
    AREC(2)[0x05] = 2; AREC(2)[0x16] = 99;   /* other owner → ignored */
    AREC(3)[0x05] = 1; AREC(3)[0x16] = 7;
    PREC(1)[0x28] = 0x55;                    /* dirty: must be cleared first */

    engine_realm_recompute_owner_tally(1);

    T_ASSERT_EQ_U(PREC(1)[0x28], 17);        /* 10 + 7 */
    return 0;
}

/* Income for an owned realm: rate by band, scaled by population then the tax
 * setting (+0xb9), stored in +0xbc and credited to the owner's three accounts.
 * pop 1000, band 3 (rate 640) → percent_of(1000,640)=6400; tax 50 →
 * percent_of(6400,50)=3200. */
int test_engine_realm_accrue_income_owned(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1;                 /* owned by player 1 */
    AI32(r, 0x24) = 1000;        /* population        */
    r[0x1c0] = 3;                /* standing band 3   */
    r[0xb9]  = 50;               /* tax setting 50%   */

    engine_realm_accrue_income();

    T_ASSERT_EQ_I(AI32(r, 0xbc), 3200);          /* computed income */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x118), 3200);   /* treasury  */
    T_ASSERT_EQ_I(AI32(PREC(1), 0xf4), 3200);    /* cashflow  */
    T_ASSERT_EQ_I(AI32(PREC(1), 0xf8), 3200);
    /* rate-adjust byte: (5 - 50) + tally(0) = -45 */
    T_ASSERT_EQ_I((int8_t)r[0x0e], -45);
    return 0;
}

/* An unowned realm (owner 0) accrues into its own pool +0x1f4 instead of any
 * player treasury, and its rate-adjust byte folds in player 0's tally. */
int test_engine_realm_accrue_income_unowned(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 0;                 /* unowned */
    AI32(r, 0x24) = 1000;
    r[0x1c0] = 5;                /* band 5 → rate 800 */
    r[0xb9]  = 100;              /* 100% tax setting   */
    AI32(r, 0x1f4) = 200;        /* pre-existing pool  */

    engine_realm_accrue_income();

    /* percent_of(1000,800)=8000; percent_of(8000,100)=8000 */
    T_ASSERT_EQ_I(AI32(r, 0xbc), 8000);
    T_ASSERT_EQ_I(AI32(r, 0x1f4), 8200);   /* 200 + 8000 */
    return 0;
}

/* The +0x1a8 turn-scratch flag (set e.g. by event 0x13b) forces this turn's
 * rate — and therefore the income — to zero regardless of band/population. */
int test_engine_realm_accrue_income_scratch_zeroes(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0x1c0] = 3; r[0xb9] = 50;
    r[0x1a8] = 100;              /* income suppressed */

    engine_realm_accrue_income();

    T_ASSERT_EQ_I(AI32(r, 0xbc), 0);
    T_ASSERT_EQ_I(AI32(PREC(1), 0x118), 0);
    return 0;
}

/* The effective band is capped by +0x1c1 only when the +0x1c3 gate is set:
 * gate set + cap 1 < band 5 → rate uses band 1 (480) not band 5 (800);
 * gate set + cap 0 → band forced to 0 (rate 320). */
int test_engine_realm_accrue_income_band_cap(void)
{
    /* gate set, cap 1 caps band 5 → band 1, rate 480 */
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0xb9] = 100;
    r[0x1c0] = 5; r[0x1c1] = 1; r[0x1c3] = 1;
    engine_realm_accrue_income();
    T_ASSERT_EQ_I(AI32(r, 0xbc), 4800);          /* percent_of(1000,480) */

    /* gate set, cap 0 → band 0, rate 320 */
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0xb9] = 100;
    r[0x1c0] = 5; r[0x1c1] = 0; r[0x1c3] = 1;
    engine_realm_accrue_income();
    T_ASSERT_EQ_I(AI32(r, 0xbc), 3200);          /* percent_of(1000,320) */

    /* gate clear → cap ignored, band 5 stands (rate 800) */
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0xb9] = 100;
    r[0x1c0] = 5; r[0x1c1] = 1; r[0x1c3] = 0;
    engine_realm_accrue_income();
    T_ASSERT_EQ_I(AI32(r, 0xbc), 8000);          /* percent_of(1000,800) */
    return 0;
}

/* ─── engine_realm_recount_armies (FUN_00451061) ────────────────────────── */

/* Kind-3 territory records (placed armies) are tallied into their owning
 * realm; other kinds are ignored; the prior scratch is reset first. */
int test_engine_realm_recount_armies_basic(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    s_slot_0056d5dc = 2;

    TREC(5)[0x08]  = 3; TREC(5)[0x10]  = 1;    /* army → realm 1 */
    TREC(9)[0x08]  = 3; TREC(9)[0x10]  = 1;    /* army → realm 1 */
    TREC(12)[0x08] = 3; TREC(12)[0x10] = 2;    /* army → realm 2 */
    TREC(20)[0x08] = 2; TREC(20)[0x10] = 1;    /* kind 2 → ignored */

    /* dirty scratch that must be reset by pass 1 */
    AREC(1)[0x1a4] = 0x77; AREC(1)[0x1a5] = 0x77; AREC(1)[0x15c] = 0x77;

    engine_realm_recount_armies();

    T_ASSERT_EQ_U(AREC(1)[0x1a4], 2);          /* two armies        */
    T_ASSERT_EQ_I(AI32(AREC(1), 0x1a0), 2);
    T_ASSERT_EQ_U(AREC(1)[0x1a5], 9);          /* last army's index */
    T_ASSERT_EQ_U(AREC(1)[0x15c], 0);          /* flag cleared      */
    T_ASSERT_EQ_U(AREC(2)[0x1a4], 1);
    T_ASSERT_EQ_I(AI32(AREC(2), 0x1a0), 1);
    T_ASSERT_EQ_U(AREC(2)[0x1a5], 12);
    return 0;
}

/* Quirk #148: across two recounts the byte +0x1a4 stays the live count while
 * the i32 +0x1a0 accumulates (reset of +0x1a4 only, never +0x1a0). */
int test_engine_realm_recount_armies_i32_accumulates(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    s_slot_0056d5dc = 1;
    TREC(3)[0x08] = 3; TREC(3)[0x10] = 1;
    TREC(4)[0x08] = 3; TREC(4)[0x10] = 1;

    engine_realm_recount_armies();
    T_ASSERT_EQ_U(AREC(1)[0x1a4], 2);
    T_ASSERT_EQ_I(AI32(AREC(1), 0x1a0), 2);

    engine_realm_recount_armies();
    T_ASSERT_EQ_U(AREC(1)[0x1a4], 2);          /* byte recounted = 2 */
    T_ASSERT_EQ_I(AI32(AREC(1), 0x1a0), 4);    /* i32 accumulated = 4 */
    return 0;
}

/* ─── engine_realm_apply_tax_approval (FUN_0044bd9f) ─────────────────────── */

/* One owned, populated, human realm; clean per-army + per-player state. */
static uint8_t *tax_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1;                 /* owned by player 1 */
    PREC(1)[0x05] = 1;           /* player 1 is human */
    AI32(r, 0x24) = 1000;        /* non-zero population */
    return r;
}

/* Basic: policy/band adjustment folds into +0x0b, reclassifies the band, and
 * emits the per-band modifier.  policy 5, band 0 → +20; 75+20 = 95 → band 4. */
int test_engine_realm_tax_basic(void)
{
    uint8_t *r = tax_reset();
    r[0x0b] = 75;
    r[0x15d] = 5;                /* tax policy 5 */
    r[0x09] = 0;                 /* prior band column 0 → table[25] = +20 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 95);
    T_ASSERT_EQ_U(r[0x09], 4);   /* lookup(95) → band 4 */
    T_ASSERT_EQ_U(r[0x10], 2);   /* modifier[4] = 2     */
    return 0;
}

/* High clamp: 100+20 → clamp 100.  Doubles as the band-100 quirk: a realm at
 * exactly 100 lands in band 3 (fallback), not the top band 4. */
int test_engine_realm_tax_clamp_high_and_100_quirk(void)
{
    uint8_t *r = tax_reset();
    r[0x0b] = 100;
    r[0x15d] = 5;  r[0x09] = 0;  /* +20 → 120 → clamp 100 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 100);
    T_ASSERT_EQ_U(r[0x09], 3);   /* lookup(100) falls through to fallback 3 */
    T_ASSERT_EQ_U(r[0x10], 1);   /* modifier[3] = 1 */
    return 0;
}

/* Low clamp: 5 + (policy 0, band 0 = -8) → -3 → clamp 0 → band 0, mod -10. */
int test_engine_realm_tax_clamp_low(void)
{
    uint8_t *r = tax_reset();
    r[0x0b] = 5;
    r[0x15d] = 0;  r[0x09] = 0;  /* table[0] = -8 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 0);
    T_ASSERT_EQ_U(r[0x09], 0);            /* lookup(0) → band 0      */
    T_ASSERT_EQ_U(r[0x10], (uint8_t)-10); /* modifier[0] = -10 (0xf6) */
    return 0;
}

/* Zero-population, non-human owner → reset to 50 (overriding the adjustment). */
int test_engine_realm_tax_zero_pop_reset(void)
{
    uint8_t *r = tax_reset();
    r[0x05] = 2;  PREC(2)[0x05] = 0;      /* owner 2, non-human */
    AI32(r, 0x24) = 0;                     /* zero population    */
    r[0x0b] = 20;
    r[0x15d] = 3;  r[0x09] = 0;            /* +8 → 28, then overridden to 50 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 50);
    T_ASSERT_EQ_U(r[0x09], 2);            /* lookup(50) → band 2 */
    T_ASSERT_EQ_U(r[0x10], 0);            /* modifier[2] = 0     */
    return 0;
}

/* Zero population but a HUMAN owner is NOT reset — the adjustment stands. */
int test_engine_realm_tax_zero_pop_human_kept(void)
{
    uint8_t *r = tax_reset();
    AI32(r, 0x24) = 0;                     /* zero population, owner 1 human */
    r[0x0b] = 20;
    r[0x15d] = 3;  r[0x09] = 0;            /* +8 → 28 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 28);
    T_ASSERT_EQ_U(r[0x09], 1);            /* lookup(28) → band 1 */
    T_ASSERT_EQ_U(r[0x10], (uint8_t)-5);  /* modifier[1] = -5 (0xfb) */
    return 0;
}

/* Quirk #149: tax policy 6 has no table row and aliases into the threshold
 * table — policy 6 / band 2 reads the (cutoff 35) entry, adding 35 not a
 * coherent tax penalty. */
int test_engine_realm_tax_policy6_aliasing(void)
{
    uint8_t *r = tax_reset();
    r[0x0b] = 0;
    r[0x15d] = 6;  r[0x09] = 2;           /* table[6*5+2] = table[32] = 35 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_I((int8_t)r[0x0b], 35);   /* the aliased threshold cutoff */
    return 0;
}

/* The whole live realm count is processed, each from its own state. */
int test_engine_realm_tax_multi_realm(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 2;

    uint8_t *r1 = AREC(1), *r2 = AREC(2);
    r1[0x05] = 1; PREC(1)[0x05] = 1; AI32(r1, 0x24) = 100;
    r2[0x05] = 1;                    AI32(r2, 0x24) = 100;
    r1[0x0b] = 75; r1[0x15d] = 5; r1[0x09] = 0;   /* → 95, band 4 */
    r2[0x0b] = 5;  r2[0x15d] = 0; r2[0x09] = 0;   /* → 0,  band 0 */

    engine_realm_apply_tax_approval();

    T_ASSERT_EQ_U(r1[0x09], 4);
    T_ASSERT_EQ_U(r2[0x09], 0);
    return 0;
}

/* ─── engine_realm_spread_development (FUN_0044bf7a) ─────────────────────── */

/* Clean per-realm + roster + overworld state; one realm, standings enabled. */
static uint8_t *dev_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_misc_1360,    0, sizeof g_misc_1360);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    s_slot_0056d5dc = 1;       /* one realm            */
    s_slot_0053f25c = 1;       /* standings enabled    */
    return AREC(1);
}

/* The standing accumulator (+0x208) rolls forward by 6·(+0x1ff) − 3·(+0x201).
 * With no developable cells the spread/projection sub-passes leave the
 * projection metrics cleared (+0xe0 = −1, capacity 0). */
int test_engine_realm_dev_accumulator(void)
{
    uint8_t *r = dev_reset();
    r[0x1ff] = 10;  r[0x201] = 4;        /* 6*10 - 3*4 = 48 */

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AI32(r, 0x208), 48);
    T_ASSERT_EQ_I(AI32(r, 0xe0), -1);    /* projection reset marker */
    T_ASSERT_EQ_I(AI32(r, 0xe4), 0);     /* no developable cells    */
    return 0;
}

/* Standings disabled (s_slot_0053f25c == 0) forces the accumulator to 0. */
int test_engine_realm_dev_accumulator_standings_off(void)
{
    uint8_t *r = dev_reset();
    s_slot_0053f25c = 0;
    r[0x1ff] = 10;  r[0x201] = 4;

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AI32(r, 0x208), 0);
    return 0;
}

/* The accumulator clamps to [-100, 100]. */
int test_engine_realm_dev_accumulator_clamp(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_misc_1360,    0, sizeof g_misc_1360);
    s_slot_0056d5dc = 2;
    s_slot_0053f25c = 1;

    AREC(1)[0x1ff] = 50;                 /* 6*50 = 300 → clamp 100  */
    AREC(2)[0x201] = 50;                 /* -3*50 = -150 → clamp -100 */

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AI32(AREC(1), 0x208), 100);
    T_ASSERT_EQ_I(AI32(AREC(2), 0x208), -100);
    return 0;
}

/* Spread + projection.  A cell is developable when its overworld type is
 * >= 0x19 (already a dev-stage tile).  The pool (+0xdc) is distributed from the
 * highest-value cell, ≤200 per cell.  Three cells (vals 100/50/300, all type
 * 0x19), pool 50: argmax is slot2 (300) → 300+50 = 350, restamped 0x1a (still
 * >= 0x19, so still developable for the projection).  Projection then sees all
 * three cells: capacity = 3 × min(800−v, 200) = 200+200+200 = 600; turns to
 * fill the best cell = ceil((800−350)/50) = 9.  Verified byte-exact against
 * retail via tools/diff_test.py --functions dev_spread. */
int test_engine_realm_dev_spread_and_project(void)
{
    uint8_t *r = dev_reset();
    engine_territory_bind_cells(g_overworld_map);

    MISC_ROW(1)[0] = 100;  g_overworld_map[100] = 0x19;   /* developable (>= 0x19) */
    MISC_ROW(1)[1] = 200;  g_overworld_map[200] = 0x19;
    MISC_ROW(1)[2] = 300;  g_overworld_map[300] = 0x19;
    AW16(r, 0x90 + 0) = 100;
    AW16(r, 0x90 + 2) = 50;
    AW16(r, 0x90 + 4) = 300;
    AI32(r, 0xdc) = 50;                  /* pool */

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AW16(r, 0x90 + 0), 100);   /* slot0 untouched         */
    T_ASSERT_EQ_I(AW16(r, 0x90 + 2), 50);    /* slot1 untouched         */
    T_ASSERT_EQ_I(AW16(r, 0x90 + 4), 350);   /* argmax slot2 += pool 50 */
    T_ASSERT_EQ_U(g_overworld_map[300], 0x1a); /* 350 → stage 0x1a      */
    T_ASSERT_EQ_U(g_overworld_map[100], 0x19); /* untouched             */
    T_ASSERT_EQ_I(AI32(r, 0xe4), 600);       /* capacity over all 3 cells */
    T_ASSERT_EQ_I(AI32(r, 0x20c), 0);        /* none reached the cap      */
    T_ASSERT_EQ_I(AI32(r, 0x214), 9);        /* ceil(450/50)              */

    engine_territory_bind_cells(NULL);
    return 0;
}

/* Value above 800 spills the excess back into the pool (without lowering the
 * cell) and stamps the maxed cell type 1 — which is < 0x19, so it drops out of
 * "developable" for the projection.  slot0 (700) + 200 = 900 → spill 100 back;
 * pool flows on to slot1 (0) → 200 (stamped 0x1a).  Projection then sees only
 * slot1: capacity = min(800−200,200) = 200.  Verified against retail. */
int test_engine_realm_dev_spread_overflow(void)
{
    uint8_t *r = dev_reset();
    engine_territory_bind_cells(g_overworld_map);

    MISC_ROW(1)[0] = 100;  g_overworld_map[100] = 0x19;
    MISC_ROW(1)[1] = 200;  g_overworld_map[200] = 0x19;
    AW16(r, 0x90 + 0) = 700;
    AW16(r, 0x90 + 2) = 0;
    AI32(r, 0xdc) = 300;

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AW16(r, 0x90 + 0), 900);   /* +200, spill not subtracted */
    T_ASSERT_EQ_I(AW16(r, 0x90 + 2), 200);   /* received the carried pool  */
    T_ASSERT_EQ_U(g_overworld_map[100], 1);  /* maxed → stage 1            */
    T_ASSERT_EQ_U(g_overworld_map[200], 0x1a);
    T_ASSERT_EQ_I(AI32(r, 0x20c), 0);        /* dry-run: maxed cell already type 1, excluded */
    T_ASSERT_EQ_I(AI32(r, 0xe4), 200);       /* only slot1 still developable */

    engine_territory_bind_cells(NULL);
    return 0;
}

/* Two realms are developed independently; the pool of each lands in its own
 * highest-value cell. */
int test_engine_realm_dev_multi_realm(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_misc_1360,    0, sizeof g_misc_1360);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    s_slot_0056d5dc = 2;
    s_slot_0053f25c = 1;
    engine_territory_bind_cells(g_overworld_map);

    MISC_ROW(1)[0] = 100; g_overworld_map[100] = 0x19; AW16(AREC(1), 0x90) = 10;
    AI32(AREC(1), 0xdc) = 50;
    MISC_ROW(2)[0] = 300; g_overworld_map[300] = 0x19; AW16(AREC(2), 0x90) = 20;
    AI32(AREC(2), 0xdc) = 50;

    engine_realm_spread_development();

    T_ASSERT_EQ_I(AW16(AREC(1), 0x90), 60);   /* 10 + 50 */
    T_ASSERT_EQ_I(AW16(AREC(2), 0x90), 70);   /* 20 + 50 */

    engine_territory_bind_cells(NULL);
    return 0;
}

/* ─── engine_realm_recompute_realm_income (FUN_0044b80b) ─────────────────── */

/* The projected-income recompute: same band→rate→income arithmetic as the
 * accrual pass but written to the PROJECTED field +0xc0, plus +0x0f = 5−tax,
 * the tax tally byte +0x16, the owner tally +0x28, and the player income
 * summary +0x15c.  pop 1000, band 3 (rate 640) → percent_of(1000,640)=6400;
 * tax 50 → percent_of(6400,50)=3200.  Tax 50 → +0x16 = -15 (low byte of the
 * tally-modifier table at its maxed-out index). */
int test_engine_realm_recompute_income_basic(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1;                 /* owned by player 1 */
    AI32(r, 0x24) = 1000;        /* population        */
    r[0x1c0] = 3;                /* standing band 3   */
    r[0xb9]  = 50;               /* tax setting 50 (the max)  */
    AI32(r, 0xbc) = 0x7777;      /* accrual field must NOT be touched */

    engine_realm_recompute_realm_income(1);

    T_ASSERT_EQ_I(AI32(r, 0xc0), 3200);          /* projected income */
    T_ASSERT_EQ_I(AI32(r, 0xbc), 0x7777);        /* accrual field untouched */
    T_ASSERT_EQ_I((int8_t)r[0x0f], -45);         /* 5 - 50 */
    T_ASSERT_EQ_I((int8_t)r[0x16], -15);         /* tally modifier @ tax 50 */
    T_ASSERT_EQ_U(PREC(1)[0x28], (uint8_t)-15);  /* owner tally = sum of +0x16 */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x15c), 3200);   /* player income summary */
    return 0;
}

/* No event-scratch gate: unlike the accrual pass, +0x1a8 does NOT suppress the
 * projection.  Low tax (≤19) leaves the tally byte +0x16 at zero. */
int test_engine_realm_recompute_income_no_scratch_gate(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0x1c0] = 5; r[0xb9] = 10;
    r[0x1a8] = 100;              /* would suppress in the accrual pass — ignored here */

    engine_realm_recompute_realm_income(1);

    /* band 5 → rate 800; percent_of(1000,800)=8000; percent_of(8000,10)=800 */
    T_ASSERT_EQ_I(AI32(r, 0xc0), 800);
    T_ASSERT_EQ_I((int8_t)r[0x16], 0);           /* tax 10 → modifier 0 */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x15c), 800);
    return 0;
}

/* The same band-cap rule as the accrual pass: +0x1c1 caps +0x1c0 only when the
 * +0x1c3 gate is set. */
int test_engine_realm_recompute_income_band_cap(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 1;
    uint8_t *r = AREC(1);
    r[0x05] = 1; AI32(r, 0x24) = 1000; r[0xb9] = 50;
    r[0x1c0] = 5; r[0x1c1] = 1; r[0x1c3] = 1;    /* cap 1 < band 5 → band 1 */
    engine_realm_recompute_realm_income(1);
    T_ASSERT_EQ_I(AI32(r, 0xc0), 2400);          /* percent_of(1000,480)=4800; ·50%=2400 */

    r[0x1c1] = 0;                                /* cap 0 → band 0 (rate 320) */
    engine_realm_recompute_realm_income(1);
    T_ASSERT_EQ_I(AI32(r, 0xc0), 1600);          /* percent_of(1000,320)=3200; ·50%=1600 */
    return 0;
}

/* Player income summary aggregates only owned realms across the realm count;
 * unowned realms (owner 0) are excluded; the summary is cleared each call. */
int test_engine_realm_recompute_income_player_summary(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 3;
    AREC(1)[0x05] = 1; AI32(AREC(1), 0x24) = 1000; AREC(1)[0x1c0] = 3; AREC(1)[0xb9] = 50;
    AREC(2)[0x05] = 1; AI32(AREC(2), 0x24) = 1000; AREC(2)[0x1c0] = 3; AREC(2)[0xb9] = 50;
    AREC(3)[0x05] = 0; AI32(AREC(3), 0x24) = 1000; AREC(3)[0x1c0] = 3; AREC(3)[0xb9] = 50;
    AI32(PREC(1), 0x15c) = 0x999;                /* dirty: must be cleared */

    /* Recompute all three; the last call's refresh re-sums every realm's +0xc0. */
    engine_realm_recompute_realm_income(1);
    engine_realm_recompute_realm_income(2);
    engine_realm_recompute_realm_income(3);

    /* realms 1+2 owned by player 1 (3200 each), realm 3 unowned → excluded. */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x15c), 6400);
    return 0;
}

/* ─── engine_realm_replan_economies (FUN_00448545) ──────────────────────── */

/* The per-turn dispatcher loops realms 1..count running provision + plan_realm
 * (both no-op with the army records unbound) + the income recompute.  With the
 * economy callees neutralised, the observable effect is each realm's projected
 * income field +0xc0 and the player summary +0x15c. */
int test_engine_realm_replan_economies_dispatches(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    s_slot_0056d5dc = 2;
    s_slot_0057c934 = 1;
    s_slot_0057c92c = 1;
    engine_army_economy_bind_army_records(NULL);   /* provision + plan → no-op */

    AREC(1)[0x05] = 1; AI32(AREC(1), 0x24) = 1000; AREC(1)[0x1c0] = 3; AREC(1)[0xb9] = 50;
    AREC(2)[0x05] = 2; AI32(AREC(2), 0x24) = 500;  AREC(2)[0x1c0] = 5; AREC(2)[0xb9] = 40;

    engine_realm_replan_economies();

    T_ASSERT_EQ_I(AI32(AREC(1), 0xc0), 3200);      /* 1000·640%·50% */
    T_ASSERT_EQ_I(AI32(AREC(2), 0xc0), 1600);      /* 500·800%·40% = 4000·40% */
    T_ASSERT_EQ_I(AI32(PREC(1), 0x15c), 3200);
    T_ASSERT_EQ_I(AI32(PREC(2), 0x15c), 1600);

    engine_army_economy_bind_army_records(g_per_army_768);   /* restore */
    return 0;
}

/* ─── engine_realm_advance_population (FUN_00449ef3) ─────────────────────── */

/* Reset to clean per-army state, unbind the territory cell grid so the two
 * overworld-graphics helpers no-op, and return realm 1's record. */
static uint8_t *pop_reset(int realms, int season)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = realms;
    s_slot_0057c934 = season;          /* just-ended season → death modifier */
    engine_territory_bind_cells(NULL); /* stamp / village helpers no-op */
    return AREC(1);
}

/* Basic growth: pop 100, full-standing birth band (100%), region 1, season 1.
 * base rate lookup(100)=30; deaths = 20(region)+4(season) = 24%; births 30 +1
 * nudge (birth-rate >= death-rate) = 31; pop 100 + 31 - 24 = 107. */
int test_engine_realm_population_basic_growth(void)
{
    uint8_t *r = pop_reset(1, 1);
    AI32(r, 0x24) = 100;          /* population */
    r[0x0c] = 80;                 /* standing → 100% birth band */
    r[0x09] = 1;                  /* region */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(AI32(r, 0x28), 100);   /* prev snapshot */
    T_ASSERT_EQ_I(AI32(r, 0x30), 31);    /* births */
    T_ASSERT_EQ_I(AI32(r, 0x34), 24);    /* deaths */
    T_ASSERT_EQ_I(AI32(r, 0x24), 107);   /* population */
    T_ASSERT_EQ_I(AI32(r, 0x2f8), 0);    /* no event delta */
    T_ASSERT_EQ_I(r[0x5b], 1);           /* dominant change = births */
    T_ASSERT_EQ_I(AI32(r, 0x2c), 7);     /* |growth| percent */
    T_ASSERT_EQ_I(r[0xb8], 5);           /* tier = (107-1)/25 + 1 */
    return 0;
}

/* Population crash: a tiny realm whose deaths exceed births floors to 0 — and
 * retail stores the (negative) leftover into the deaths field +0x34.  region 0
 * adds +2 deaths, low standing → 25% birth band. */
int test_engine_realm_population_crash_quirk(void)
{
    uint8_t *r = pop_reset(1, 2);
    AI32(r, 0x24) = 2;
    r[0x0c] = 0;                  /* standing → 25% birth band */
    r[0x09] = 0;                  /* region 0 → +2 deaths */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(AI32(r, 0x30), 0);     /* births zeroed on crash */
    T_ASSERT_EQ_I(AI32(r, 0x34), -1);    /* deaths = negative leftover (quirk) */
    T_ASSERT_EQ_I(AI32(r, 0x24), 0);     /* population floored to 0 */
    T_ASSERT_EQ_I(r[0xb8], 1);           /* tier 1 */
    T_ASSERT_EQ_I(r[0x5b], 0);           /* no dominant category */
    return 0;
}

/* Positive event modifier (+0x1fb) adds bounded births: pop 1000, 120% band,
 * base lookup(1000)=11 → birth-rate 13 → births 131; evt = 10% of 131 + 10 =
 * 23 (under the 20%-of-pop=200 cap); deaths 50. */
int test_engine_realm_population_event_birth_bonus(void)
{
    uint8_t *r = pop_reset(1, 3);
    AI32(r, 0x24) = 1000;
    r[0x0c] = 100;                /* standing → 120% birth band */
    r[0x09] = 3;
    r[0x1fb] = 10;                /* positive event modifier */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(AI32(r, 0x34), 50);    /* deaths */
    T_ASSERT_EQ_I(AI32(r, 0x2f8), 23);   /* event delta */
    T_ASSERT_EQ_I(AI32(r, 0x30), 154);   /* births 131 + 23 */
    T_ASSERT_EQ_I(AI32(r, 0x24), 1104);  /* 1000 + 154 - 50 */
    T_ASSERT_EQ_I(r[0xb8], 45);          /* tier = (1104-1)/25 + 1 */
    return 0;
}

/* Negative event modifier exceeding the 20%-of-pop cap is clamped: pop 100,
 * cap = 20; raw evt = 50% of deaths(24) + 10 = 22 → clamped to 20; deaths
 * 24 + 20 = 44; pop 100 + 31 - 44 = 87 → deaths now dominate. */
int test_engine_realm_population_event_death_capped(void)
{
    uint8_t *r = pop_reset(1, 1);
    AI32(r, 0x24) = 100;
    r[0x0c] = 80;
    r[0x09] = 1;
    r[0x1fb] = (uint8_t)(-50);    /* large negative modifier */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(AI32(r, 0x30), 31);    /* births */
    T_ASSERT_EQ_I(AI32(r, 0x2f8), 20);   /* event delta capped at 20% of pop */
    T_ASSERT_EQ_I(AI32(r, 0x34), 44);    /* deaths 24 + 20 */
    T_ASSERT_EQ_I(AI32(r, 0x24), 87);    /* 100 + 31 - 44 */
    T_ASSERT_EQ_I(r[0x5b], 2);           /* deaths dominate */
    return 0;
}

/* Emigration (+0x3c) leaves and immigration (+0x40) arrives after the natural
 * birth/death step: pop 500, 50% band, base lookup(500)=15 → birth-rate 7 →
 * births 35; deaths 100 +1 = 101; 500 - 66 - 50 + 30 = 414. */
int test_engine_realm_population_migration_applied(void)
{
    uint8_t *r = pop_reset(1, 2);
    AI32(r, 0x24) = 500;
    r[0x0c] = 50;                 /* standing → 50% birth band */
    r[0x09] = 1;
    AI32(r, 0x3c) = 50;           /* emigration */
    AI32(r, 0x40) = 30;           /* immigration */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(AI32(r, 0x30), 35);    /* births */
    T_ASSERT_EQ_I(AI32(r, 0x34), 101);   /* deaths */
    T_ASSERT_EQ_I(AI32(r, 0x24), 414);   /* 500 + 35 - 101 - 50 + 30 */
    T_ASSERT_EQ_I(r[0x5b], 2);           /* deaths dominate */
    T_ASSERT_EQ_I(r[0xb8], 17);          /* tier = (414-1)/25 + 1 */
    return 0;
}

/* With the overworld cell grid + army records bound, the pass refreshes the
 * capital footprint (low-pop descriptor '/') and the village tiles (tier 0 →
 * all four settlement cells reverted to bare terrain). */
int test_engine_realm_population_refreshes_overworld(void)
{
    static uint8_t cells[0x1000 * 8];
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(cells, 0, sizeof cells);
    s_slot_0056d5dc = 1;
    s_slot_0057c934 = 1;
    engine_territory_bind_cells(cells);
    engine_territory_bind_army_records(g_per_army_768);

    uint8_t *r = AREC(1);
    AI32(r, 0x24) = 100;          /* low pop → '/' capital, village tier 0 */
    r[0x0c] = 80; r[0x09] = 1;
    int cap_off = (5 * 0x40 + 5) * 8;
    AI32(r, 0x70) = cap_off;      /* capital cell offset */
    int s0 = (10 * 0x40 + 10) * 8;
    AI32(r, 0x80) = s0;           /* settlement cell 0 (slots 1..3 → cell 0) */
    cells[s0 + 0] = 0x13;         /* a standing village */

    engine_realm_advance_population();

    T_ASSERT_EQ_I(cells[cap_off + 2] & 0x1c, 0x0c);  /* capital-region bits */
    T_ASSERT_EQ_I(cells[cap_off + 3], '/');          /* low-pop descriptor */
    T_ASSERT_EQ_I(cells[s0 + 0], 0);                 /* village tier 0 → reverted */

    engine_territory_bind_cells(NULL);
    engine_territory_bind_army_records(NULL);
    return 0;
}

/* ---- engine_realm_build_coalitions (FUN_0044b042) ------------------------ *
 * Partitions active armies into adjacency-connected components per owner.    *
 * Per-army fields: owner +0x05 (0 = inactive), strength +0x24, adjacency     *
 * list +0x5c (16 bytes, 0-terminated).                                       */

static void army_setup(int key, uint8_t owner, int32_t strength)
{
    uint8_t *r = AREC(key);
    r[0x05] = owner;
    AI32(r, 0x24) = strength;
}

/* Append `nbr` to army `key`'s +0x5c adjacency list (first empty slot). */
static void army_add_adj(int key, uint8_t nbr)
{
    uint8_t *adj = &AREC(key)[0x5c];
    for (int i = 0; i < 0x10; i++)
        if (adj[i] == 0) { adj[i] = nbr; return; }
}

/* True if group g lists army `m` in any member slot. */
static int coal_has(int g, int m)
{
    for (int s = 0; s < 0x14; s++)
        if (engine_realm_coalition_group_member(g, s) == m) return 1;
    return 0;
}

/* One owner, a chain 1→2→3 (each adjacent to the previous) collapses into a
 * single coalition group. */
int test_engine_realm_coalition_single_owner_chain(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;
    army_setup(1, 1, 10); army_setup(2, 1, 20); army_setup(3, 1, 30);
    army_add_adj(2, 1);                 /* 2 adjacent to 1 */
    army_add_adj(3, 2);                 /* 3 adjacent to 2 */

    engine_realm_build_coalitions();

    T_ASSERT_EQ_I(engine_realm_coalition_active_count(), 1);
    T_ASSERT_EQ_I(engine_realm_coalition_group_key(0), 1);
    T_ASSERT(coal_has(0, 1) && coal_has(0, 2) && coal_has(0, 3));
    return 0;
}

/* Same owner but two non-adjacent clusters {1,2} and {3,4} → two groups. */
int test_engine_realm_coalition_disconnected_split(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 4;
    army_setup(1, 1, 0); army_setup(2, 1, 0);
    army_setup(3, 1, 0); army_setup(4, 1, 0);
    army_add_adj(2, 1);                 /* cluster A */
    army_add_adj(4, 3);                 /* cluster B */

    engine_realm_build_coalitions();

    T_ASSERT_EQ_I(engine_realm_coalition_active_count(), 2);
    T_ASSERT(coal_has(0, 1) && coal_has(0, 2));
    T_ASSERT(coal_has(1, 3) && coal_has(1, 4));
    return 0;
}

/* Two different owners never share a group, even when cross-listed. */
int test_engine_realm_coalition_separate_owners(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    army_setup(1, 1, 0); army_setup(2, 2, 0);
    army_add_adj(1, 2); army_add_adj(2, 1);

    engine_realm_build_coalitions();

    T_ASSERT_EQ_I(engine_realm_coalition_active_count(), 2);
    T_ASSERT_EQ_I(engine_realm_coalition_group_key(0), 1);
    T_ASSERT_EQ_I(engine_realm_coalition_group_key(1), 2);
    T_ASSERT(coal_has(0, 1) && coal_has(1, 2));
    return 0;
}

/* A group's strength field sums its members' +0x24 populations. */
int test_engine_realm_coalition_strength_sum(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 2;
    army_setup(1, 1, 100); army_setup(2, 1, 250);
    army_add_adj(2, 1);

    engine_realm_build_coalitions();

    T_ASSERT_EQ_I(engine_realm_coalition_active_count(), 1);
    T_ASSERT_EQ_I(engine_realm_coalition_group_strength(0), 350);
    return 0;
}

/* Inactive armies (owner byte 0) are skipped entirely. */
int test_engine_realm_coalition_inactive_skipped(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = 3;
    army_setup(1, 1, 100); army_setup(2, 0, 999); army_setup(3, 1, 50);
    army_add_adj(3, 1);                 /* 3 adjacent to 1; 2 inactive */

    engine_realm_build_coalitions();

    T_ASSERT_EQ_I(engine_realm_coalition_active_count(), 1);
    T_ASSERT_EQ_I(engine_realm_coalition_group_strength(0), 150);
    T_ASSERT(coal_has(0, 1) && coal_has(0, 3));
    T_ASSERT(!coal_has(0, 2));
    return 0;
}
