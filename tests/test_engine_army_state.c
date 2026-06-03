/*
 * Tests for engine_army_state.c — the per-tick SP army-state machine
 * (FUN_004650b0 driver + the four state→display handlers FUN_0046521f /
 * FUN_00465486 / FUN_00465622 / FUN_00465761).
 *
 * Operates on the per-territory occupant block (g_per_territory_420, stride
 * 0x1a4, index 1..150) and the shared 8-byte cell descriptor grid (region key
 * at cell +7).  Facing-frame tables baked from the exe:
 *   march (states 1/2) DAT_004d6a78 = {0,1,2,1,0,1,2,1}
 *   idle  (states 3/4) DAT_004d6ab8 = {0,1,2,3,4,5,0,1}
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_army_state.h"
#include "engine_event_queue.h"
#include "save.h"
#include "save_schema_globals.h"   /* s_slot_0057c8cc (local player) */
#include "engine_path_field.h"     /* raid-planning path seams */

#define CREC(i)    (&g_per_territory_420[(i) * 0x1a4])
#define R_REC(k)   (&g_per_army_768[(k) * 0x300])
#define I32(p, o)  (*(int32_t *)((p) + (o)))

#define A_OWNER    0x00
#define A_READY    0x06
#define A_SPRITE   0x07
#define A_STATE    0x08
#define A_ANIM     0x09
#define A_CELL     0x0c
#define A_REGION   0x10
#define A_FACING   0x1b
#define A_SUBSTATE 0x14c
#define A_TICKS    0x154
#define A_TIMER    0x160
#define A_HEAD     0x168

static uint8_t g_test_cells[4096];

static void as_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(g_test_cells, 0, sizeof g_test_cells);
    engine_army_state_bind_cells(g_test_cells);
    engine_army_state_bind_move_fn(NULL);
    s_slot_0057c8cc = 1;       /* local player */
    g_army_state_abort = 0;
}

/* Put `occ` on a cell whose region key is `key`. */
static void place(int occ, int cell_off, int key)
{
    uint8_t *r = CREC(occ);
    I32(r, A_CELL) = cell_off;
    g_test_cells[cell_off + 7] = (uint8_t)key;
}

/* --- driver: live-record counting + dead-record skip ---------------------- */
int test_engine_army_state_driver_counts_live(void)
{
    as_reset();
    /* idx 1, 5, 150 live & idle (state 3); the rest dead (owner 0). */
    for (int i = 1; i <= 150; i++) place(i, i * 8, 0);
    CREC(1)[A_OWNER] = 1;   CREC(1)[A_STATE] = 3;
    CREC(5)[A_OWNER] = 2;   CREC(5)[A_STATE] = 3;
    CREC(150)[A_OWNER] = 3; CREC(150)[A_STATE] = 3;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_I(g_army_state_live_count, 3);
    return 0;
}

/* --- driver: state 0 and state >= 6 zero (disband) the record ------------- */
int test_engine_army_state_disband_out_of_range(void)
{
    as_reset();
    for (int s = 0; s <= 7; s++) {
        /* states 1..5 dispatch; 0 and 6,7 disband. */
        if (s >= 1 && s <= 5) continue;
        as_reset();
        uint8_t *r = CREC(4);
        r[A_OWNER] = 1; r[A_STATE] = (uint8_t)s;
        I32(r, A_HEAD) = 999; r[A_SPRITE] = 0xaa; I32(r, A_TIMER) = 50;
        place(4, 32, 7);

        engine_event_queue_state_t evq; engine_event_queue_init(&evq);
        engine_army_state_run(&evq, 0);

        /* whole 0x1a4 record zeroed */
        for (int b = 0; b < 0x1a4; b++)
            if (r[b] != 0) T_FAIL("state %d: byte %d not zeroed (%u)", s, b, r[b]);
    }
    return 0;
}

/* --- state 1 (march): sprite by head bucket + facing delta ---------------- */
int test_engine_army_state_march_sprite_buckets(void)
{
    as_reset();
    g_army_state_index = 0;
    static const struct { int head, base; } cases[] = {
        { 100, 0x48 }, { 300, 0x48 }, { 301, 0x60 }, { 600, 0x60 }, { 601, 0x78 },
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        as_reset();
        uint8_t *r = CREC(4);
        r[A_OWNER] = 1; r[A_STATE] = 1;
        r[A_ANIM] = 0;            /* phase = (0+1)&7 = 1 */
        r[A_FACING] = 2;          /* march table[2] = 2 */
        I32(r, A_HEAD) = cases[c].head;
        place(4, 16, 9);
        r[A_REGION] = 9;          /* == cell key → skip reconcile */

        engine_event_queue_state_t evq; engine_event_queue_init(&evq);
        engine_army_state_run(&evq, 0);

        T_ASSERT_EQ_U(r[A_TICKS], 0xf);
        int expect = 1 * 3 + cases[c].base + 2;
        T_ASSERT_EQ_U(r[A_SPRITE], (uint8_t)expect);
    }
    return 0;
}

/* --- state 1 (march): differing colour → region reconcile latches & notifies */
int test_engine_army_state_march_reconcile(void)
{
    as_reset();
    uint8_t *r = CREC(4);
    r[A_OWNER] = 1;              /* owner == local → reception is published */
    r[A_STATE] = 1;
    I32(r, A_HEAD) = 10;
    place(4, 24, 5);            /* cell region key 5 */
    r[A_REGION] = 9;            /* differs → enter_region runs */
    uint8_t *reg = R_REC(5);
    reg[0x05] = 0;             /* neutral region → reception notice */
    reg[0x0c] = 5;            /* hostile reserve → code 0x82 */
    I32(reg, 0x24) = 100;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_U(r[A_REGION], 5);          /* colour latched to the cell key */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x82);  /* neutral reception notice */
    return 0;
}

/* --- state 1 (march): sub-state advance + timer decrement ------------------ */
int test_engine_army_state_march_substate_and_timer(void)
{
    /* A_READY == 0 and sub-state != 2 → sub-state advances to 2. */
    as_reset();
    uint8_t *r = CREC(4);
    r[A_OWNER] = 1; r[A_STATE] = 1; I32(r, A_HEAD) = 10;
    place(4, 8, 0); r[A_REGION] = 0;
    r[A_READY] = 0; r[A_SUBSTATE] = 0; I32(r, A_TIMER) = 3;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);
    T_ASSERT_EQ_U(r[A_SUBSTATE], 2);
    T_ASSERT_EQ_I(I32(r, A_TIMER), 2);    /* 3 → 2 */

    /* A_READY != 0 → sub-state left alone; timer floored at 0. */
    as_reset();
    r = CREC(4);
    r[A_OWNER] = 1; r[A_STATE] = 1; I32(r, A_HEAD) = 10;
    place(4, 8, 0); r[A_REGION] = 0;
    r[A_READY] = 1; r[A_SUBSTATE] = 0; I32(r, A_TIMER) = 0;

    engine_army_state_run(&evq, 0);
    T_ASSERT_EQ_U(r[A_SUBSTATE], 0);
    T_ASSERT_EQ_I(I32(r, A_TIMER), 0);    /* stays 0 (not decremented) */
    return 0;
}

/* --- state 2 (settle): low head removes the unit -------------------------- */
int test_engine_army_state_settle_low_head_removes(void)
{
    as_reset();
    uint8_t *r = CREC(4);
    r[A_OWNER] = 1; r[A_STATE] = 2; I32(r, A_HEAD) = 29;  /* < 30 */
    place(4, 8, 0);

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_U(r[A_OWNER], 0);   /* engine_unit_remove zeroed the record */
    return 0;
}

/* --- state 2 (settle): healthy head → sprite base 0x90 + colour latch ------ */
int test_engine_army_state_settle_sprite_and_latch(void)
{
    as_reset();
    uint8_t *r = CREC(4);
    r[A_OWNER] = 1; r[A_STATE] = 2; I32(r, A_HEAD) = 50;
    r[A_ANIM] = 0; r[A_FACING] = 3;     /* march table[3] = 1 */
    place(4, 40, 6); r[A_REGION] = 9;   /* differs */
    R_REC(6)[0x05] = 0;                 /* neutral region → settle just latches */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_U(r[A_TICKS], 0xa);
    T_ASSERT_EQ_U(r[A_SPRITE], (uint8_t)(1 * 3 + 0x90 + 1));
    T_ASSERT_EQ_U(r[A_REGION], 6);      /* settle_region latched the cell key */
    return 0;
}

/* --- states 3 & 4 (idle): sprite ×6 + facing-idle + direct colour set ------ */
int test_engine_army_state_idle_sets_region(void)
{
    for (int st = 3; st <= 4; st++) {
        as_reset();
        uint8_t *r = CREC(4);
        r[A_OWNER] = 1; r[A_STATE] = (uint8_t)st;
        r[A_ANIM] = 1; r[A_FACING] = 5;     /* phase (1+1)&7=2; idle table[5]=5 */
        place(4, 48, 7); r[A_REGION] = 0;   /* set directly to cell key */

        engine_event_queue_state_t evq; engine_event_queue_init(&evq);
        engine_army_state_run(&evq, 0);

        T_ASSERT_EQ_U(r[A_TICKS], 0xa);
        T_ASSERT_EQ_U(r[A_SPRITE], (uint8_t)(2 * 6 + 5));
        T_ASSERT_EQ_U(r[A_REGION], 7);
    }
    return 0;
}

/* --- movement seam: invoked on sub-state 2, sees the current index --------- */
static int s_move_calls;
static int s_move_seen_index;
static void counting_move(engine_event_queue_state_t *evq, int mp_active)
{
    (void)evq; (void)mp_active;
    s_move_calls++;
    s_move_seen_index = g_army_state_index;
}

int test_engine_army_state_move_seam_invoked(void)
{
    as_reset();
    s_move_calls = 0; s_move_seen_index = -1;
    engine_army_state_bind_move_fn(counting_move);

    uint8_t *r = CREC(7);
    r[A_OWNER] = 1; r[A_STATE] = 3; r[A_SUBSTATE] = 2;
    place(7, 8, 0);

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_I(s_move_calls, 1);
    T_ASSERT_EQ_I(s_move_seen_index, 7);
    return 0;
}

/* --- abort flag: a seam that raises it stops the walk early ---------------- */
static void aborting_move(engine_event_queue_state_t *evq, int mp_active)
{
    (void)evq; (void)mp_active;
    g_army_state_abort = 1;
}

int test_engine_army_state_abort_breaks_walk(void)
{
    as_reset();
    engine_army_state_bind_move_fn(aborting_move);

    /* idx 4 aborts during its handler → idx 5 never counted. */
    CREC(4)[A_OWNER] = 1; CREC(4)[A_STATE] = 3; CREC(4)[A_SUBSTATE] = 2; place(4, 8, 0);
    CREC(5)[A_OWNER] = 1; CREC(5)[A_STATE] = 3; CREC(5)[A_SUBSTATE] = 2; place(5, 16, 0);

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_army_state_run(&evq, 0);

    T_ASSERT_EQ_I(g_army_state_live_count, 1);   /* stopped after idx 4 */
    return 0;
}

/* engine_army_state_arm_movement (FUN_004a4f5b): arm sub-state 2 for every
 * occupant whose state byte matches AND whose sub-state is non-zero; return 1
 * if any was armed.  Matching is purely on state + non-zero sub-state — owner
 * is not consulted, and a zero sub-state is left untouched. */
int test_engine_army_state_arm_movement_matches_state(void)
{
    as_reset();

    /* idx 2: state 3, sub-state 1 (non-zero)   → armed to 2 */
    CREC(2)[A_STATE] = 3; CREC(2)[A_SUBSTATE] = 1;
    /* idx 3: state 3, sub-state 0              → left at 0 (not pending) */
    CREC(3)[A_STATE] = 3; CREC(3)[A_SUBSTATE] = 0;
    /* idx 4: state 4, sub-state 5              → wrong state, untouched */
    CREC(4)[A_STATE] = 4; CREC(4)[A_SUBSTATE] = 5;
    /* idx 5: state 3, sub-state 7 (non-zero)   → armed to 2 (owner unset) */
    CREC(5)[A_STATE] = 3; CREC(5)[A_SUBSTATE] = 7;

    int r = engine_army_state_arm_movement(3);

    T_ASSERT_EQ_I(r, 1);                       /* something armed */
    T_ASSERT_EQ_I(CREC(2)[A_SUBSTATE], 2);     /* matched + pending → armed */
    T_ASSERT_EQ_I(CREC(3)[A_SUBSTATE], 0);     /* zero sub-state untouched */
    T_ASSERT_EQ_I(CREC(4)[A_SUBSTATE], 5);     /* wrong state untouched */
    T_ASSERT_EQ_I(CREC(5)[A_SUBSTATE], 2);     /* matched + pending → armed */
    return 0;
}

/* No occupant of the requested state with a pending sub-state → returns 0
 * (the phase-advance gate the turn driver uses). */
int test_engine_army_state_arm_movement_none_returns_zero(void)
{
    as_reset();
    CREC(2)[A_STATE] = 4; CREC(2)[A_SUBSTATE] = 1;   /* state 4, not 3 */
    CREC(3)[A_STATE] = 3; CREC(3)[A_SUBSTATE] = 0;   /* state 3 but not pending */

    int r = engine_army_state_arm_movement(3);

    T_ASSERT_EQ_I(r, 0);
    T_ASSERT_EQ_I(CREC(2)[A_SUBSTATE], 1);     /* untouched */
    T_ASSERT_EQ_I(CREC(3)[A_SUBSTATE], 0);     /* untouched */
    return 0;
}

#define A_CONTROL 0x01     /* control-type byte: 0 = local/viewer */
#define A_F199    0x199    /* besieged-region link (besieger) */
#define A_F19A    0x19a    /* besieged mark (defender) */
#define R_PENDING 0x1bc    /* per-army pending-occupant slot (i32) */

/* engine_army_state_arm_movement_owned (FUN_004a4e3d): the owner-gated sibling.
 * Arms sub-state 2 only when state + owner (+0x00) + control (+0x01 == 0) all
 * match and the sub-state is pending. */
int test_engine_army_state_arm_movement_owned_gates(void)
{
    as_reset();

    /* idx 2: state 2, owner 6, control 0, pending → armed */
    CREC(2)[A_STATE] = 2; CREC(2)[A_OWNER] = 6; CREC(2)[A_CONTROL] = 0; CREC(2)[A_SUBSTATE] = 1;
    /* idx 3: wrong owner (5) → rejected */
    CREC(3)[A_STATE] = 2; CREC(3)[A_OWNER] = 5; CREC(3)[A_CONTROL] = 0; CREC(3)[A_SUBSTATE] = 1;
    /* idx 4: control byte set (AI/remote) → rejected */
    CREC(4)[A_STATE] = 2; CREC(4)[A_OWNER] = 6; CREC(4)[A_CONTROL] = 1; CREC(4)[A_SUBSTATE] = 1;
    /* idx 5: right owner+control but sub-state 0 → rejected */
    CREC(5)[A_STATE] = 2; CREC(5)[A_OWNER] = 6; CREC(5)[A_CONTROL] = 0; CREC(5)[A_SUBSTATE] = 0;

    int r = engine_army_state_arm_movement_owned(2, 6);

    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(CREC(2)[A_SUBSTATE], 2);     /* armed */
    T_ASSERT_EQ_I(CREC(3)[A_SUBSTATE], 1);     /* wrong owner */
    T_ASSERT_EQ_I(CREC(4)[A_SUBSTATE], 1);     /* AI/remote control */
    T_ASSERT_EQ_I(CREC(5)[A_SUBSTATE], 0);     /* not pending */
    return 0;
}

/* State == 1 fires the siege-link teardown (engine_combat_break_siege_link):
 * the matched occupant's +0x199 and the resolved defender's +0x19a are cleared. */
int test_engine_army_state_arm_movement_owned_state1_breaks_siege(void)
{
    as_reset();

    /* Besieger occ 2: state 1, owner 3, control 0, pending; besieging region 9. */
    CREC(2)[A_STATE] = 1; CREC(2)[A_OWNER] = 3; CREC(2)[A_CONTROL] = 0;
    CREC(2)[A_SUBSTATE] = 1; CREC(2)[A_F199] = 9;
    /* Region 9's per-army record points its pending slot at defender occ 7. */
    I32(R_REC(9), R_PENDING) = 7;
    /* Defender occ 7 carries the besieged mark. */
    CREC(7)[A_F19A] = 2;

    int r = engine_army_state_arm_movement_owned(1, 3);

    T_ASSERT_EQ_I(r, 1);
    T_ASSERT_EQ_I(CREC(2)[A_SUBSTATE], 2);     /* armed to movement */
    T_ASSERT_EQ_I(CREC(2)[A_F199], 0);         /* besieger link cleared */
    T_ASSERT_EQ_I(CREC(7)[A_F19A], 0);         /* defender mark cleared */
    return 0;
}

/* --- siege-engine construction (FUN_004a8507 / FUN_004a84ba) -------------- */

/* Siege-build slot accessors: 3 × { i16 count, i16 pct, i16 value } @ +0x182. */
#define A_SIEGE_REMAIN  0x19c
#define SLOT_COUNT(p, s) (*(int16_t *)((p) + 0x182 + (s) * 6 + 0))
#define SLOT_PCT(p, s)   (*(int16_t *)((p) + 0x182 + (s) * 6 + 2))
#define SLOT_VALUE(p, s) (*(int16_t *)((p) + 0x182 + (s) * 6 + 4))

/* A live, state-1, besieging stack with `head` build budget and one ordered
 * engine in slot 0 (target 200). */
static uint8_t *siege_setup(int occ, int head)
{
    uint8_t *r = CREC(occ);
    r[A_OWNER] = 4; r[A_STATE] = 1; r[A_F199] = 9;
    I32(r, A_HEAD) = head;
    SLOT_COUNT(r, 0) = 1;        /* one engine, target = 200 * 1 = 200 */
    return r;
}

/* Each of the four gates (dead / wrong-state / not-besieging / no head-count)
 * short-circuits the build step to a no-op returning 0. */
int test_engine_army_state_siege_build_gates(void)
{
    as_reset();

    siege_setup(2, 50); CREC(2)[A_OWNER] = 0;          /* dead record    */
    T_ASSERT_EQ_I(engine_army_state_siege_build_step(2), 0);
    T_ASSERT_EQ_I(SLOT_VALUE(CREC(2), 0), 0);

    siege_setup(3, 50); CREC(3)[A_STATE] = 2;          /* not state 1    */
    T_ASSERT_EQ_I(engine_army_state_siege_build_step(3), 0);

    siege_setup(4, 50); CREC(4)[A_F199] = 0;           /* not besieging  */
    T_ASSERT_EQ_I(engine_army_state_siege_build_step(4), 0);

    siege_setup(5, 0);                                 /* no head-count  */
    T_ASSERT_EQ_I(engine_army_state_siege_build_step(5), 0);
    T_ASSERT_EQ_I(SLOT_VALUE(CREC(5), 0), 0);
    return 0;
}

/* One under-funded slot: the whole head budget goes in, value advances by the
 * budget, percent is recomputed, and the remaining byte is ceil(gap / head). */
int test_engine_army_state_siege_build_partial(void)
{
    as_reset();
    uint8_t *r = siege_setup(2, 50);   /* target 200, value 0, budget 50 */

    int done = engine_army_state_siege_build_step(2);

    T_ASSERT_EQ_I(done, 0);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 0), 50);              /* 0 + 50          */
    T_ASSERT_EQ_I(SLOT_PCT(r, 0), 25);               /* 50*100/200      */
    T_ASSERT_EQ_I(r[A_SIEGE_REMAIN], 3);             /* ceil(150/50)    */
    return 0;
}

/* The tick a slot reaches its target: value clamps to target, percent hits 100,
 * the remaining byte zeroes, and the step returns 1 ("built"). */
int test_engine_army_state_siege_build_completes(void)
{
    as_reset();
    uint8_t *r = siege_setup(2, 50);
    SLOT_VALUE(r, 0) = 190; SLOT_PCT(r, 0) = 95;      /* 10 from target  */

    int done = engine_army_state_siege_build_step(2);

    T_ASSERT_EQ_I(done, 1);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 0), 200);            /* clamped         */
    T_ASSERT_EQ_I(SLOT_PCT(r, 0), 100);
    T_ASSERT_EQ_I(r[A_SIEGE_REMAIN], 0);
    return 0;
}

/* Two active slots split the budget evenly; the third (no engines ordered) is
 * inert. */
int test_engine_army_state_siege_build_even_split(void)
{
    as_reset();
    uint8_t *r = siege_setup(2, 100);
    SLOT_COUNT(r, 1) = 1;            /* slot 1 also one engine (target 200) */
    /* slot 2 left at count 0 → inactive */

    int done = engine_army_state_siege_build_step(2);

    T_ASSERT_EQ_I(done, 0);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 0), 50);             /* 100/2 each      */
    T_ASSERT_EQ_I(SLOT_VALUE(r, 1), 50);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 2), 0);              /* inactive        */
    T_ASSERT_EQ_I(SLOT_PCT(r, 0), 25);
    T_ASSERT_EQ_I(SLOT_PCT(r, 1), 25);
    return 0;
}

/* When one slot's even share overshoots its target, the leftover budget tops up
 * the other unfinished slot in the second pass. */
int test_engine_army_state_siege_build_topup(void)
{
    as_reset();
    uint8_t *r = siege_setup(2, 100);
    SLOT_VALUE(r, 0) = 195; SLOT_PCT(r, 0) = 97;      /* 5 from target   */
    SLOT_COUNT(r, 1) = 1;                             /* fresh slot      */

    int done = engine_army_state_siege_build_step(2);

    /* share = 50.  Pass1: slot0 +5 → 200 (spent 5), slot1 +50 → 50 (spent 50),
     * budget left = 100-55 = 45.  Pass2: slot1 += 45 → 95. */
    T_ASSERT_EQ_I(done, 0);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 0), 200);
    T_ASSERT_EQ_I(SLOT_PCT(r, 0), 100);
    T_ASSERT_EQ_I(SLOT_VALUE(r, 1), 95);
    T_ASSERT_EQ_I(SLOT_PCT(r, 1), 47);               /* 95*100/200 = 47 */
    return 0;
}

/* The scanner resumes from the persistent cursor, advances each record's build,
 * and parks on the first that completes (returning 1); a dry pass walks the
 * cursor past record 150 and returns 0. */
int test_engine_army_state_scan_siege_built(void)
{
    as_reset();

    /* Record 5 is one tick from done; records before it are not besieging. */
    uint8_t *r5 = siege_setup(5, 50);
    SLOT_VALUE(r5, 0) = 190; SLOT_PCT(r5, 0) = 95;

    s_slot_0057cad4 = 1;
    int hit = engine_army_state_scan_siege_built();

    T_ASSERT_EQ_I(hit, 1);
    T_ASSERT_EQ_I((int)s_slot_0057cad4, 5);          /* parked on record 5 */
    T_ASSERT_EQ_I(CREC(5)[A_SIEGE_REMAIN], 0);

    /* From just past it, nothing else is besieging → dry pass past 150. */
    s_slot_0057cad4 = 6;
    int dry = engine_army_state_scan_siege_built();
    T_ASSERT_EQ_I(dry, 0);
    T_ASSERT_EQ_I((int)s_slot_0057cad4, 0x97);       /* walked off the end */
    return 0;
}

/* --- raider targeting + movement planning (FUN_004ac5ba / FUN_004ac499) ---- */

#define A_REGION_O   0x10
#define A_DEST_COL_O 0x116
#define A_DEST_ROW_O 0x117
#define A_RAID_FLAG_O 0x150
#define A_RAID_DEST_O 0x151
#define A_PLAN_PEND_O 0x14e
#define A_POS_COL_O  0x0a
#define A_POS_ROW_O  0x0b
#define RAID_REC(k)  (&g_per_army_768[(k) * 0x300])

static void raid_reset(int realms)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_0056d5dc = (uint32_t)realms;
    s_slot_0057c934 = 0;                 /* not force-mode */
    g_raider_target_cursor = 0;
}

/* Set realm `k`'s capital cell (record +0x6c col / +0x6d row). */
static void set_capital(int k, int col, int row)
{
    RAID_REC(k)[0x6c] = (uint8_t)col;
    RAID_REC(k)[0x6d] = (uint8_t)row;
}

/* A fresh stack steps the shared cursor, skips its own region, and copies the
 * chosen realm's capital into its destination. */
int test_engine_army_state_raid_assign_skips_own_region(void)
{
    raid_reset(3);
    set_capital(1, 11, 21);
    set_capital(2, 12, 22);
    set_capital(3, 13, 23);

    /* Stack in region 1: cursor 0→1 lands on own region → advance to 2. */
    CREC(5)[A_REGION_O] = 1;
    int got = engine_army_state_raid_assign_target(5);

    T_ASSERT_EQ_I(got, 1);
    T_ASSERT_EQ_I(g_raider_target_cursor, 2);
    T_ASSERT_EQ_I(CREC(5)[A_RAID_DEST_O], 2);
    T_ASSERT_EQ_I(CREC(5)[A_DEST_COL_O], 12);
    T_ASSERT_EQ_I(CREC(5)[A_DEST_ROW_O], 22);
    T_ASSERT_EQ_I(CREC(5)[A_RAID_FLAG_O], 0);
    return 0;
}

/* Cursor wraps back to 1 once it would step past the last realm. */
int test_engine_army_state_raid_assign_wraps(void)
{
    raid_reset(2);
    set_capital(1, 5, 6);
    set_capital(2, 7, 8);
    g_raider_target_cursor = 2;          /* next step → 3 → wrap → 1 */

    CREC(4)[A_REGION_O] = 2;             /* region 2 != cursor 1, no extra skip */
    int got = engine_army_state_raid_assign_target(4);

    T_ASSERT_EQ_I(got, 1);
    T_ASSERT_EQ_I(g_raider_target_cursor, 1);
    T_ASSERT_EQ_I(CREC(4)[A_RAID_DEST_O], 1);
    T_ASSERT_EQ_I(CREC(4)[A_DEST_COL_O], 5);
    return 0;
}

/* With an existing target and no force-mode, the target byte is kept but the
 * destination is recomputed from the SHARED cursor's current realm — the retail
 * quirk where dest can disagree with the stored target. */
int test_engine_army_state_raid_assign_keeps_target_dest_from_cursor(void)
{
    raid_reset(3);
    set_capital(1, 11, 21);
    set_capital(2, 12, 22);
    set_capital(3, 13, 23);
    g_raider_target_cursor = 3;          /* cursor parked on realm 3 */

    CREC(6)[A_RAID_DEST_O] = 2;          /* stack already targets realm 2 */
    int got = engine_army_state_raid_assign_target(6);

    T_ASSERT_EQ_I(got, 1);
    T_ASSERT_EQ_I(g_raider_target_cursor, 3);         /* unchanged (no reassign) */
    T_ASSERT_EQ_I(CREC(6)[A_RAID_DEST_O], 2);         /* target byte kept */
    T_ASSERT_EQ_I(CREC(6)[A_DEST_COL_O], 13);         /* but dest = realm 3 cap! */
    T_ASSERT_EQ_I(CREC(6)[A_DEST_ROW_O], 23);
    return 0;
}

/* Force-mode 2 reassigns even when the stack already has a target. */
int test_engine_army_state_raid_assign_force_mode(void)
{
    raid_reset(3);
    set_capital(1, 11, 21);
    g_raider_target_cursor = 3;
    s_slot_0057c934 = 2;                 /* force re-target */

    CREC(7)[A_REGION_O] = 0;             /* region 0 never equals a 1.. cursor */
    CREC(7)[A_RAID_DEST_O] = 2;
    int got = engine_army_state_raid_assign_target(7);

    T_ASSERT_EQ_I(got, 1);
    T_ASSERT_EQ_I(g_raider_target_cursor, 1);         /* 3 → 4 → wrap → 1 */
    T_ASSERT_EQ_I(CREC(7)[A_RAID_DEST_O], 1);
    T_ASSERT_EQ_I(CREC(7)[A_DEST_COL_O], 11);
    return 0;
}

/* The phase-5 sweep routes a settling (state 2) stack toward its target capital
 * — arming its sub-state — and leaves non-state-2 stacks untouched. */
static int16_t raid_terrain[ENGINE_PATH_FIELD_CELLS + 0x83];
int test_engine_army_state_plan_raids_routes_state2(void)
{
    raid_reset(2);
    memset(raid_terrain, 0, sizeof raid_terrain);
    engine_path_field_bind_terrain(raid_terrain);
    s_slot_0057c8cc = 1;

    set_capital(1, 11, 10);              /* realm 1 capital one cell east */
    /* one-cost step east from (10,10) so the flood + trace connect */
    raid_terrain[10 * ENGINE_PATH_FIELD_DIM + 10 + 0x42] = 1;

    /* Stack 3: settling, region 2 (so cursor 0→1 targets realm 1), at (10,10). */
    CREC(3)[A_STATE] = 2; CREC(3)[A_REGION_O] = 2;
    CREC(3)[A_POS_COL_O] = 10; CREC(3)[A_POS_ROW_O] = 10;
    /* Stack 4: not settling → skipped. */
    CREC(4)[A_STATE] = 1; CREC(4)[A_SUBSTATE] = 0;

    engine_army_state_plan_raids();

    T_ASSERT_EQ_I(CREC(3)[A_RAID_DEST_O], 1);         /* targeted realm 1 */
    T_ASSERT_EQ_I(CREC(3)[A_SUBSTATE], 1);            /* armed (path found) */
    T_ASSERT_EQ_I(CREC(3)[A_PLAN_PEND_O], 0);
    T_ASSERT_EQ_I(CREC(4)[A_SUBSTATE], 0);            /* untouched */
    return 0;
}

/* --- phase-3 capital-march planner (FUN_00429418) ------------------------- */

#define A_TARGET_REALM_O  0x167
#define A_MOVE_DEST_COL_O 0x14
#define A_MOVE_DEST_ROW_O 0x15
#define A_PATH_COL_O      0x16
#define A_PATH_ROW_O      0x17

static int16_t cap_terrain[ENGINE_PATH_FIELD_CELLS + 0x83];

/* A state-4 (idle/in-transit) stack is routed to its target realm's capital:
 * the executor destination (+0x14/+0x15) and path-trace goal (+0x16/+0x17) are
 * copied from the capital cell, the target byte (+0x151) is republished, the
 * retarget/plan flags (+0x150/+0x14e) are cleared, and a found path arms the
 * executor (+0x14c = 1).  Non-state-4 stacks are left untouched. */
int test_engine_army_state_plan_capital_marches_routes_state4(void)
{
    raid_reset(2);
    memset(cap_terrain, 0, sizeof cap_terrain);
    engine_path_field_bind_terrain(cap_terrain);
    s_slot_0057c8cc = 1;

    set_capital(1, 11, 10);              /* target realm 1 capital one cell east */
    cap_terrain[10 * ENGINE_PATH_FIELD_DIM + 10 + 0x42] = 1;

    CREC(3)[A_STATE] = 4;
    CREC(3)[A_TARGET_REALM_O] = 1;
    CREC(3)[A_POS_COL_O] = 10; CREC(3)[A_POS_ROW_O] = 10;
    CREC(3)[A_RAID_FLAG_O] = 9;          /* stale → must be cleared */
    CREC(3)[A_PLAN_PEND_O] = 9;          /* stale → must be cleared */

    /* Stack 4: not idle → skipped entirely. */
    CREC(4)[A_STATE] = 1; CREC(4)[A_SUBSTATE] = 0;

    engine_army_state_plan_capital_marches();

    T_ASSERT_EQ_I(CREC(3)[A_MOVE_DEST_COL_O], 11);
    T_ASSERT_EQ_I(CREC(3)[A_MOVE_DEST_ROW_O], 10);
    T_ASSERT_EQ_I(CREC(3)[A_PATH_COL_O], 11);
    T_ASSERT_EQ_I(CREC(3)[A_PATH_ROW_O], 10);
    T_ASSERT_EQ_I(CREC(3)[A_RAID_DEST_O], 1);
    T_ASSERT_EQ_I(CREC(3)[A_RAID_FLAG_O], 0);
    T_ASSERT_EQ_I(CREC(3)[A_PLAN_PEND_O], 0);
    T_ASSERT_EQ_I(CREC(3)[A_SUBSTATE], 1);            /* armed (path found) */
    T_ASSERT_EQ_I(CREC(4)[A_SUBSTATE], 0);            /* untouched */
    return 0;
}
