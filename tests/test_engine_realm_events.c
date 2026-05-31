/*
 * Tests for engine_realm_roll_events (FUN_00448819) — the per-realm
 * random-event ONSET pass run by the turn-advance engine.
 *
 * The pass reads/writes the per-army record block (g_per_army_768), the
 * per-player strategic block (g_player_strategic_352), the overworld-cell
 * grid (g_overworld_map) and the per-realm territory roster (g_misc_1360);
 * all are canonical save globals (linked from save.c), written directly here.
 *
 * Cursor walk: with rng_7bit = k, realm r reads deck slot (2k + r) mod 256.
 * For a single realm (count = 1) realm 1 reads deck[2k + 1]; the event ids
 * sit at odd deck indices, so k = (idx - 1) / 2 lands realm 1 on a chosen
 * event.  Deck indices below are from the baked table (Lords2.exe 0x4d6108).
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_realm_events.h"
#include "engine_territory.h"
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)   (&g_per_army_768[(key) * 0x300])
#define PREC(slot)  (&g_player_strategic_352[(slot) * 0x160])
#define AI32(p, o)  (*(int32_t *)((p) + (o)))
#define PI32(p, o)  (*(int32_t *)((p) + (o)))

/* Deck index → rng_7bit for a single realm hitting it as realm 1. */
#define RNG_FOR_IDX(idx)  (((idx) - 1) / 2)

/* One realm owned by player 1 (events enabled), era gate open, default
 * event-mode 1; clean per-army + per-player + map + roster state. */
static uint8_t *events_reset(void)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(g_misc_1360, 0, sizeof g_misc_1360);
    s_slot_0056d5dc = 1;        /* one realm                       */
    s_slot_00553edc = 0x4f5;    /* era counter past the 0x4f4 gate */
    s_slot_0057c934 = 1;        /* event mode                      */
    uint8_t *a = AREC(1);
    a[0x05] = 1;                /* owned by player 1               */
    PREC(1)[0x05] = 1;          /* player 1 has events enabled     */
    return a;
}

/* The four scratch bytes are cleared every realm regardless of trigger, and
 * no event fires while either gate is shut. */
int test_engine_events_scratch_and_gates(void)
{
    uint8_t *a = events_reset();
    a[0x1fb] = a[0x1fc] = a[0x1fd] = a[0x1a8] = 0xCD;
    uint8_t *a2 = AREC(2);                  /* realm beyond count */
    a2[0x1fb] = a2[0x1a8] = 0xCD;

    /* Gate 1 shut: era counter exactly at the threshold (not >). rng would
     * otherwise land realm 1 on 0x87 (idx 7). */
    s_slot_00553edc = 0x4f4;
    engine_realm_roll_events(RNG_FOR_IDX(7));
    T_ASSERT_EQ_U(a[0x1fb], 0);             /* scratch cleared anyway */
    T_ASSERT_EQ_U(a[0x1fc], 0);
    T_ASSERT_EQ_U(a[0x1fd], 0);
    T_ASSERT_EQ_U(a[0x1a8], 0);
    T_ASSERT_EQ_U(a[0x00], 0);              /* no event active */
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);
    T_ASSERT_EQ_U(a2[0x1fb], 0xCD);         /* realm past count untouched */
    T_ASSERT_EQ_U(a2[0x1a8], 0xCD);

    /* Gate 2 shut: era open but this owner has events disabled. */
    a = events_reset();
    PREC(1)[0x05] = 0;
    a[0x1fb] = 0xCD;
    engine_realm_roll_events(RNG_FOR_IDX(7));
    T_ASSERT_EQ_U(a[0x1fb], 0);
    T_ASSERT_EQ_U(a[0x00], 0);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);

    /* Deck slot empty (rng=0 → realm 1 reads idx 1, a zero slot): no fire. */
    a = events_reset();
    engine_realm_roll_events(0);
    T_ASSERT_EQ_U(a[0x00], 0);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);
    return 0;
}

/* A resource-credit event commits without any precondition: 0x131 (idx 151)
 * adds 500 to the owner's +0x118 account; the realm is marked active. */
int test_engine_events_commit_resource(void)
{
    uint8_t *a = events_reset();
    a[0x05] = 2;                            /* owned by player 2 */
    PREC(2)[0x05] = 1;
    PI32(PREC(2), 0x118) = 1000;

    engine_realm_roll_events(RNG_FOR_IDX(151));   /* deck[151] = 0x131 */

    T_ASSERT_EQ_U(a[0x00], 1);              /* event active */
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0x131);
    T_ASSERT_EQ_I(PI32(PREC(2), 0x118), 1500);
    return 0;
}

/* 0x87 (idx 7): commits a per-mode colour byte at +0x1fc when the gated stat
 * +0x224 ≥ 0x32, else cancels (id + active flag both back to 0 even though
 * the dispatcher set them). */
int test_engine_events_cancel_and_mode(void)
{
    /* Cancel: +0x224 below threshold. */
    uint8_t *a = events_reset();
    AI32(a, 0x224) = 0x31;
    engine_realm_roll_events(RNG_FOR_IDX(7));
    T_ASSERT_EQ_U(a[0x00], 0);              /* handler cancelled */
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);
    T_ASSERT_EQ_U(a[0x1fc], 0);             /* colour byte untouched */

    /* Commit, mode 2 → +0x1fc = 0xe7. */
    a = events_reset();
    s_slot_0057c934 = 2;
    AI32(a, 0x224) = 0x32;
    engine_realm_roll_events(RNG_FOR_IDX(7));
    T_ASSERT_EQ_U(a[0x00], 1);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0x87);
    T_ASSERT_EQ_U(a[0x1fc], 0xe7);

    /* Commit, mode 4 → +0x1fc = 0xd8. */
    a = events_reset();
    s_slot_0057c934 = 4;
    AI32(a, 0x224) = 0x100;
    engine_realm_roll_events(RNG_FOR_IDX(7));
    T_ASSERT_EQ_U(a[0x1fc], 0xd8);
    return 0;
}

/* 0x8a (idx 39): population-gated, sets a per-mode +0x1fb byte then decays the
 * signed +0x0b morale stat by 0x14 with a [0, 0x19] clamp. */
int test_engine_events_pop_gate_and_clamp(void)
{
    /* Population below 100 → cancel. */
    uint8_t *a = events_reset();
    AI32(a, 0x24) = 99;
    engine_realm_roll_events(RNG_FOR_IDX(39));
    T_ASSERT_EQ_U(a[0x00], 0);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);

    /* Commit, mode 4, mid-range stat: 0x20 - 0x14 = 0x0c (in range). */
    a = events_reset();
    s_slot_0057c934 = 4;
    AI32(a, 0x24) = 100;
    a[0x0b] = 0x20;
    engine_realm_roll_events(RNG_FOR_IDX(39));
    T_ASSERT_EQ_U(a[0x00], 1);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0x8a);
    T_ASSERT_EQ_U(a[0x1fb], 0xd8);
    T_ASSERT_EQ_I((int8_t)a[0x0b], 0x0c);

    /* Lower clamp: 5 - 0x14 = -15 → 0. */
    a = events_reset();
    AI32(a, 0x24) = 100;
    a[0x0b] = 5;
    engine_realm_roll_events(RNG_FOR_IDX(39));
    T_ASSERT_EQ_I((int8_t)a[0x0b], 0);

    /* Upper clamp: 0x40 - 0x14 = 0x2c > 0x19 → 0x19. */
    a = events_reset();
    AI32(a, 0x24) = 100;
    a[0x0b] = 0x40;
    engine_realm_roll_events(RNG_FOR_IDX(39));
    T_ASSERT_EQ_I((int8_t)a[0x0b], 0x19);
    return 0;
}

/* 0x12f (idx 119): scans the realm's territory roster for an empty overworld
 * cell (type 0) and claims it (type 1); cancels when none is eligible. */
int test_engine_events_territory_claim(void)
{
    /* Eligible cell present: roster col 0 → cell offset 8, map[8] == 0. */
    uint8_t *a = events_reset();
    engine_territory_bind_cells(g_overworld_map);
    ((int32_t *)&g_misc_1360[1 * 0x50])[0] = 8;   /* realm 1, roster slot 0 */
    g_overworld_map[8] = 0;                        /* empty cell */

    engine_realm_roll_events(RNG_FOR_IDX(119));    /* deck[119] = 0x12f */

    T_ASSERT_EQ_U(a[0x00], 1);                      /* committed */
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0x12f);
    T_ASSERT_EQ_U(g_overworld_map[8], 1);          /* cell claimed (type 1) */

    /* No eligible cell (empty roster) → cancel. */
    a = events_reset();
    engine_realm_roll_events(RNG_FOR_IDX(119));
    T_ASSERT_EQ_U(a[0x00], 0);
    T_ASSERT_EQ_I(*(int16_t *)(a + 0x1aa), 0);

    engine_territory_bind_cells(NULL);             /* restore global state */
    return 0;
}
