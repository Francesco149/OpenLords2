/*
 * Tests for engine_realm_pay_upkeep (FUN_004acbd4) and its shared
 * unit-lifecycle primitives (wage / contract / map-unlink / disband / absorb).
 *
 * Operates on the per-record occupant block (g_per_territory_420), the
 * per-player strategic block (g_player_strategic_352), the per-army block
 * (g_per_army_768), the mercenary-pool table (g_unit_table_20) and the
 * overworld map (g_overworld_map) — all canonical save globals written here.
 *
 * The pass calls into already-ported turn passes (income aggregation and
 * economy re-plan) as side effects; tests keep s_slot_0056d5dc small so those
 * stay benign and assert only the fields each primitive owns.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_realm_upkeep.h"
#include "engine_event_queue.h"
#include "save.h"
#include "save_schema_globals.h"

#define UREC(i)     (&g_per_territory_420[(i) * 0x1a4])
#define SREC(s)     (&g_player_strategic_352[(s) * 0x160])
#define AREC(k)     (&g_per_army_768[(k) * 0x300])
#define I32(p, o)   (*(int32_t *)((p) + (o)))
#define I16(p, o)   (*(int16_t *)((p) + (o)))

static void up_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_unit_table_20, 0, sizeof g_unit_table_20);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(g_misc_1360, 0, sizeof g_misc_1360);
    s_slot_0053f23c = 0;   /* difficulty / AI wage divisor selector */
    s_slot_0053f260 = 0;   /* morale-sweep gate (off by default)    */
    s_slot_0057c8cc = 1;   /* local player 1                        */
    s_slot_0056d5dc = 0;   /* keep replan_economies a no-op         */
}

/* --- wage computation (FUN_004ad52b) -------------------------------------- */
int test_engine_upkeep_wage_divisors(void)
{
    up_reset();
    uint8_t *r = UREC(1);
    r[0x00] = 2;              /* owner 2 */
    I32(r, 0x168) = 30;       /* value   */

    /* AI owner (strat+0x05 == 0): value / {3,5,10,10} by difficulty. */
    s_slot_0053f23c = 0;  T_ASSERT_EQ_I(engine_unit_recompute_wage(1), 10);
    T_ASSERT_EQ_I(I32(r, 0x15c), 10);                 /* written to +0x15c */
    s_slot_0053f23c = 1;  T_ASSERT_EQ_I(engine_unit_recompute_wage(1), 6);
    s_slot_0053f23c = 2;  T_ASSERT_EQ_I(engine_unit_recompute_wage(1), 3);
    s_slot_0053f23c = 9;  T_ASSERT_EQ_I(engine_unit_recompute_wage(1), 3);

    /* Human owner (strat+0x05 != 0): signed value/4 toward zero. */
    SREC(2)[0x05] = 1;
    I32(r, 0x168) = 30;   T_ASSERT_EQ_I(engine_unit_recompute_wage(1), 7);
    I32(r, 0x168) = -30;  T_ASSERT_EQ_I(engine_unit_recompute_wage(1), -7);
    return 0;
}

/* --- total wage over a player's active units (FUN_004ad495) --------------- */
int test_engine_upkeep_total_wage_filters(void)
{
    up_reset();
    s_slot_0053f23c = 0;                       /* /3 */
    UREC(1)[0x00] = 2; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 40;  /* 13 */
    UREC(2)[0x00] = 2; UREC(2)[0x08] = 1; I32(UREC(2), 0x168) = 81;  /* 27 */
    UREC(3)[0x00] = 2; UREC(3)[0x08] = 0; I32(UREC(3), 0x168) = 99;  /* inactive */
    UREC(4)[0x00] = 3; UREC(4)[0x08] = 1; I32(UREC(4), 0x168) = 99;  /* other owner */

    T_ASSERT_EQ_I(engine_unit_total_wage(2), 13 + 27);
    return 0;
}

/* --- contract clear / refund (FUN_004ac6fe) ------------------------------- */
int test_engine_upkeep_clear_contract(void)
{
    up_reset();
    uint8_t *r = UREC(1);
    r[0x08] = 1;                       /* active */
    r[0x195] = 0xAA; r[0x196] = 12; r[0x197] = 5;   /* contract: amt 12, slot 5 */
    I32(r, 0x168) = 100;
    I16(&g_unit_table_20[0], 5 * 0x14) = 0x1234;    /* pool slot occupied */

    engine_unit_clear_contract(1);
    T_ASSERT_EQ_I(I32(r, 0x168), 88);              /* value refunded by amt */
    T_ASSERT_EQ_U(r[0x195], 0);
    T_ASSERT_EQ_U(r[0x196], 0);
    T_ASSERT_EQ_U(r[0x197], 0);
    T_ASSERT_EQ_I(I16(&g_unit_table_20[0], 5 * 0x14), 0);  /* pool slot freed */

    /* No contract slot → no-op. */
    I32(r, 0x168) = 50; r[0x197] = 0; r[0x196] = 7;
    engine_unit_clear_contract(1);
    T_ASSERT_EQ_I(I32(r, 0x168), 50);
    T_ASSERT_EQ_U(r[0x196], 7);
    return 0;
}

/* --- overworld occupant list unlink (FUN_0046ec6e) ------------------------ */
int test_engine_upkeep_map_unlink(void)
{
    const int CELL = 0x100;

    /* Build list head→1→2→3→0 at one cell.  +0x0c is the cell byte offset;
     * g_overworld_map[cell+5] is the head; +0x04 is the next link. */
#define BUILD_LIST()                                                  \
    do {                                                              \
        up_reset();                                                   \
        for (int i = 1; i <= 3; i++) I32(UREC(i), 0x0c) = CELL;       \
        g_overworld_map[CELL + 5] = 1;                                \
        I16(UREC(1), 0x04) = 2;                                       \
        I16(UREC(2), 0x04) = 3;                                       \
        I16(UREC(3), 0x04) = 0;                                       \
    } while (0)

    /* Unlink the head: head advances to rec1.next, rec1.next cleared. */
    BUILD_LIST();
    engine_unit_map_unlink(1);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 2);
    T_ASSERT_EQ_I(I16(UREC(1), 0x04), 0);

    /* Unlink a middle node: predecessor splices over it. */
    BUILD_LIST();
    engine_unit_map_unlink(2);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 1);
    T_ASSERT_EQ_I(I16(UREC(1), 0x04), 3);    /* 1 now points past 2 to 3 */
    T_ASSERT_EQ_I(I16(UREC(2), 0x04), 0);

    /* Not in the list: list unchanged, only the target's next is cleared. */
    BUILD_LIST();
    I16(UREC(5), 0x04) = 0x55; I32(UREC(5), 0x0c) = CELL;
    engine_unit_map_unlink(5);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 1);
    T_ASSERT_EQ_I(I16(UREC(1), 0x04), 2);
    T_ASSERT_EQ_I(I16(UREC(3), 0x04), 0);

    /* Self-loop guard: a node pointing to itself is broken, walk terminates. */
    BUILD_LIST();
    I16(UREC(1), 0x04) = 1;                  /* head points to itself */
    engine_unit_map_unlink(2);               /* 2 not reachable now   */
    T_ASSERT_EQ_I(I16(UREC(1), 0x04), 0);    /* self-loop cleared     */
#undef BUILD_LIST
    return 0;
}

/* --- map link: append to a cell's occupant list (FUN_0046eddf) ------------ */
int test_engine_upkeep_map_link(void)
{
    const int CELL = 0x200;

    /* Empty cell: the record becomes the list head, flagged head (+0x06 = 1). */
    up_reset();
    I32(UREC(1), 0x0c) = CELL;
    engine_unit_map_link(1);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 1);
    T_ASSERT_EQ_U(UREC(1)[0x06], 1);

    /* Non-empty head→1→0: append rec 2; 1.next=2, rec2 flagged tail (+0x06=0). */
    up_reset();
    I32(UREC(1), 0x0c) = CELL; I32(UREC(2), 0x0c) = CELL;
    g_overworld_map[CELL + 5] = 1; I16(UREC(1), 0x04) = 0;
    engine_unit_map_link(2);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 1);   /* head unchanged */
    T_ASSERT_EQ_I(I16(UREC(1), 0x04), 2);          /* 1 → 2 */
    T_ASSERT_EQ_I(I16(UREC(2), 0x04), 0);
    T_ASSERT_EQ_U(UREC(2)[0x06], 0);

    /* Walk to the tail of head→1→2→0 and append rec 3. */
    up_reset();
    for (int i = 1; i <= 3; i++) I32(UREC(i), 0x0c) = CELL;
    g_overworld_map[CELL + 5] = 1; I16(UREC(1), 0x04) = 2; I16(UREC(2), 0x04) = 0;
    engine_unit_map_link(3);
    T_ASSERT_EQ_I(I16(UREC(2), 0x04), 3);

    /* An active army member already holding a back-link is left out entirely. */
    up_reset();
    I32(UREC(1), 0x0c) = CELL;
    UREC(1)[0x08] = 1;          /* U_ACTIVE == 1 */
    UREC(1)[0x198] = 5;         /* U_LINK_A != 0 */
    engine_unit_map_link(1);
    T_ASSERT_EQ_U(g_overworld_map[CELL + 5], 0);   /* not linked */
    return 0;
}

/* --- disband: army back-links + strat counter + wage refresh (FUN_004aa039) */
int test_engine_upkeep_disband_links(void)
{
    /* link-A path: +0x198 != 0 → army[A]+0x1bc zeroed. */
    up_reset();
    uint8_t *r = UREC(1);
    r[0x00] = 2; r[0x08] = 1;
    r[0x14f] = 4;                      /* strat counter index */
    r[0x198] = 3;                      /* link A */
    I32(AREC(3), 0x1bc) = 99;
    SREC(2)[0x2d + 4] = 5;
    engine_unit_disband(1);
    T_ASSERT_EQ_I(I32(AREC(3), 0x1bc), 0);
    T_ASSERT_EQ_U(SREC(2)[0x2d + 4], 4);          /* counter decremented */
    T_ASSERT_EQ_U(r[0x00], 0);                    /* record zeroed       */
    T_ASSERT_EQ_U(r[0x08], 0);
    T_ASSERT_EQ_I(I32(SREC(2), 0xfc), 0);         /* wage sum refreshed  */

    /* link-B path: +0x198 == 0, +0x199 != 0 → record[army[B]+0x1bc]+0x19a = 0. */
    up_reset();
    r = UREC(1);
    r[0x00] = 2; r[0x08] = 1;
    r[0x198] = 0; r[0x199] = 7;
    I32(AREC(7), 0x1bc) = 2;           /* points at record 2 */
    UREC(2)[0x19a] = 0xAB;
    engine_unit_disband(1);
    T_ASSERT_EQ_U(UREC(2)[0x19a], 0);
    T_ASSERT_EQ_U(r[0x199], 0);
    return 0;
}

/* --- absorb back into army (FUN_004aba5a) --------------------------------- */
int test_engine_upkeep_absorb(void)
{
    /* flag == 1: fold value into army +0x24/+0x38 then remove the unit. */
    up_reset();
    uint8_t *r = UREC(1);
    r[0x08] = 1;
    r[0x167] = 1;                      /* absorb-now flag */
    r[0x11]  = 4;                      /* army key        */
    I32(r, 0x168) = 50;
    I32(AREC(4), 0x24) = 10;
    I32(AREC(4), 0x38) = 20;
    engine_unit_absorb(1);
    T_ASSERT_EQ_I(I32(AREC(4), 0x24), 60);
    T_ASSERT_EQ_I(I32(AREC(4), 0x38), 70);
    T_ASSERT_EQ_U(r[0x08], 0);                    /* removed (record zeroed) */

    /* flag >= 2: just clears the flag, no army change, record kept. */
    up_reset();
    r = UREC(1);
    r[0x08] = 1; r[0x167] = 3; r[0x11] = 4; I32(r, 0x168) = 50;
    I32(AREC(4), 0x24) = 10;
    engine_unit_absorb(1);
    T_ASSERT_EQ_U(r[0x167], 0);
    T_ASSERT_EQ_I(I32(AREC(4), 0x24), 10);
    T_ASSERT_EQ_U(r[0x08], 1);

    /* flag == 0: no-op. */
    up_reset();
    r = UREC(1);
    r[0x08] = 1; r[0x167] = 0; I32(r, 0x168) = 50; r[0x11] = 4;
    I32(AREC(4), 0x24) = 10;
    engine_unit_absorb(1);
    T_ASSERT_EQ_I(I32(AREC(4), 0x24), 10);
    T_ASSERT_EQ_U(r[0x08], 1);
    return 0;
}

/* --- pay pass: affordable path + state-0 escalation onset (FUN_004acbd4) --- */
int test_engine_upkeep_pay_afford_and_state0(void)
{
    engine_event_queue_state_t evq;

    /* Affordable: resource >= total wage → pay it, reset state. */
    up_reset();
    engine_event_queue_init(&evq);
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 40;  /* wage 13 */
    I32(SREC(1), 0x118) = 100;                                       /* resource */
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(I32(SREC(1), 0xfc), 13);          /* wage total recorded */
    T_ASSERT_EQ_I(I32(SREC(1), 0x118), 87);         /* paid: 100 - 13      */
    T_ASSERT_EQ_U(SREC(1)[0x158], 0);               /* state reset         */
    T_ASSERT_EQ_I(evq.notify_flag, 0);              /* no warning posted   */

    /* Can't pay, state 0, no contracted units → post 0x10e, state → 1. */
    up_reset();
    engine_event_queue_init(&evq);
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400; /* wage 133 */
    I32(SREC(1), 0x118) = 10;
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x10e);
    T_ASSERT_EQ_I(evq.ring[0].audience, 1);
    T_ASSERT_EQ_U(SREC(1)[0x158], 1);

    /* Can't pay, state 0, a contracted unit → settle path posts 0xa0. */
    up_reset();
    engine_event_queue_init(&evq);
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400;
    UREC(1)[0x196] = 5; UREC(1)[0x197] = 2;         /* contract amt 5, slot 2 */
    I32(SREC(1), 0x118) = 10;
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xa0);
    T_ASSERT_EQ_U(SREC(1)[0x158], 1);
    return 0;
}

/* --- pay pass: mid escalation states + final disband-all (FUN_004acbd4) ---- */
int test_engine_upkeep_pay_escalation_and_disband(void)
{
    engine_event_queue_state_t evq;

    /* State in [1,3] → post 0x11f, decay, state++. */
    up_reset();
    engine_event_queue_init(&evq);
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400;
    I32(SREC(1), 0x118) = 10; SREC(1)[0x158] = 1;
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x11f);
    T_ASSERT_EQ_U(SREC(1)[0x158], 2);

    /* State 4 → post 0x10f, state → 5. */
    up_reset();
    engine_event_queue_init(&evq);
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400;
    I32(SREC(1), 0x118) = 10; SREC(1)[0x158] = 4;
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x10f);
    T_ASSERT_EQ_U(SREC(1)[0x158], 5);

    /* State >= 5, local player owns the realm → disband all, post 0x110. */
    up_reset();
    engine_event_queue_init(&evq);
    s_slot_0057c8cc = 1;
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400;
    I32(SREC(1), 0x118) = 10; SREC(1)[0x158] = 5;
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x110);
    T_ASSERT_EQ_U(SREC(1)[0x158], 0);
    T_ASSERT_EQ_U(UREC(1)[0x08], 0);                /* unit disbanded */

    /* State >= 5, a different player is local → post 0x111 to local player,
     * carrying the disbanded realm id in flag7. */
    up_reset();
    engine_event_queue_init(&evq);
    s_slot_0057c8cc = 2;                            /* local is player 2 */
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; I32(UREC(1), 0x168) = 400;
    I32(SREC(1), 0x118) = 10; SREC(1)[0x158] = 5;
    SREC(2)[0x05] = 1;                              /* player 2 active in filter */
    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x111);
    T_ASSERT_EQ_I(evq.ring[0].audience, 2);
    return 0;
}

/* --- morale/supply sweep escalation (FUN_004ace5e via the pass) ----------- */
int test_engine_upkeep_morale_sweep(void)
{
    engine_event_queue_state_t evq;

    up_reset();
    engine_event_queue_init(&evq);
    s_slot_0053f260 = 1;                            /* morale sweep enabled */
    /* Unit whose value covers its army's (zero) stockpile floor, not folded
     * into an army (+0x198 == 0): the per-record counter escalates.  Keep the
     * owner solvent so the per-player loop posts nothing. */
    UREC(1)[0x00] = 1; UREC(1)[0x08] = 1; UREC(1)[0x198] = 0;
    I32(UREC(1), 0x168) = 40;
    I32(SREC(1), 0x118) = 100000;                   /* easily affordable */

    engine_realm_pay_upkeep(&evq);
    T_ASSERT_EQ_U(UREC(1)[0x155], 1);               /* per-record state → 1 */
    T_ASSERT_EQ_I(evq.ring[0].code, 0x116);         /* first morale warning */

    /* Advance to state 4 then 5: the unit is disbanded at >= 5. */
    UREC(1)[0x155] = 5;
    engine_realm_pay_upkeep(&evq);
    /* The 0x118 (disband) warning is the most recent morale post; the unit is
     * gone afterward. */
    T_ASSERT_EQ_U(UREC(1)[0x08], 0);
    return 0;
}
