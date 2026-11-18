/*
 * Tests for engine_combat.c — strategic-map army combat resolution
 * (FUN_004aa89f / FUN_00422c12 / FUN_004ab2aa / FUN_004aad07).
 *
 * Operates on the per-territory occupant block (g_per_territory_420) and the
 * mercenary-pool table (g_unit_table_20) — both canonical save globals.
 *
 * Per-class weights {2,16,8,13,9,13,22}; casualty table
 * {110→10,130→20,160→30,180→40,220→50,270→65,360→80,500→90,700→95,900→98}
 * default 100.  A strength is sum(count[c]*weight[c]) (+merc) then +20 (or
 * floored at 1 if zero).
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_combat.h"
#include "engine_army_ai.h"        /* engine_army_ai_launch_attack (step-0xa integration) */
#include "engine_realm_upkeep.h"   /* shares the merc-contract triple */
#include "engine_army_economy.h"   /* NULL the army-records bind (resign re-plan) */
#include "engine_map_query.h"      /* bind_cell_descriptors (transfer-army ring) */
#include "engine_territory.h"      /* bind_records / bind_cells (spawn place_entity) */
#include "game_init.h"             /* engine_game_init_troop_pool_row (muster pool) */
#include "save.h"
#include "save_schema_globals.h"   /* s_slot_0053f23c (wage divisor) */

#define CREC(i)    (&g_per_territory_420[(i) * 0x1a4])
#define I32(p, o)  (*(int32_t *)((p) + (o)))
#define I16(p, o)  (*(int16_t *)((p) + (o)))

#define C_OWNER     0x00
#define C_ACTIVE    0x08
#define C_VALUE     0x168
#define C_COUNTS    0x16c
#define C_RESULT    0x17a
#define C_CON_CLASS 0x195
#define C_CON_AMT   0x196
#define C_CON_SLOT  0x197

static void cb_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_unit_table_20, 0, sizeof g_unit_table_20);
}

static void set_counts(int rec, const int16_t c[7])
{
    uint8_t *r = CREC(rec);
    for (int i = 0; i < 7; i++)
        I16(r, C_COUNTS + i * 2) = c[i];
}

/* --- FUN_004aa89f: clear battle-result accumulators ----------------------- */
int test_engine_combat_clear_battle_result(void)
{
    cb_reset();
    uint8_t *r = CREC(3);
    I16(r, C_RESULT + 0) = 11;
    I16(r, C_RESULT + 2) = 22;
    I16(r, C_RESULT + 4) = 33;
    I16(r, C_RESULT + 6) = 44;
    I16(r, C_RESULT + 8) = 55;   /* +0x182 — must NOT be touched */
    I16(r, C_RESULT - 2) = 66;   /* +0x178 — must NOT be touched */

    engine_combat_clear_battle_result(3);

    T_ASSERT_EQ_I(I16(r, C_RESULT + 0), 0);
    T_ASSERT_EQ_I(I16(r, C_RESULT + 2), 0);
    T_ASSERT_EQ_I(I16(r, C_RESULT + 4), 0);
    T_ASSERT_EQ_I(I16(r, C_RESULT + 6), 0);
    T_ASSERT_EQ_I(I16(r, C_RESULT + 8), 55);
    T_ASSERT_EQ_I(I16(r, C_RESULT - 2), 66);
    return 0;
}

/* --- FUN_00422c12: load muster scratch ------------------------------------ */
int test_engine_combat_load_muster(void)
{
    cb_reset();
    int16_t ac[7] = { 10, 20, 30, 40, 50, 60, 70 };
    int16_t dc[7] = {  1,  2,  3,  4,  5,  6,  7 };
    set_counts(1, ac);
    set_counts(2, dc);
    /* attacker merc: 5 of class 2; defender merc: 9 of class 6 */
    CREC(1)[C_CON_CLASS] = 2;  CREC(1)[C_CON_AMT] = 5;
    CREC(2)[C_CON_CLASS] = 6;  CREC(2)[C_CON_AMT] = 9;

    engine_combat_load_muster(1, 2);

    for (int c = 0; c < 7; c++) {
        T_ASSERT_EQ_I(engine_combat_muster(0, c), ac[c] + (c == 2 ? 5 : 0));
        T_ASSERT_EQ_I(engine_combat_muster(1, c), dc[c] + (c == 6 ? 9 : 0));
    }
    return 0;
}

/* --- FUN_004ab2aa: weighted army strength --------------------------------- */
int test_engine_combat_army_strength(void)
{
    cb_reset();
    /* single peasant class: 10 * weight(2) = 20, + 20 presence = 40 */
    int16_t c1[7] = { 10, 0, 0, 0, 0, 0, 0 };
    set_counts(1, c1);
    T_ASSERT_EQ_I(engine_combat_army_strength(1), 40);

    /* empty stack floors at 1 (no +20) */
    cb_reset();
    T_ASSERT_EQ_I(engine_combat_army_strength(1), 1);

    /* merc: 1 of class 0 (w=2) + 5 mercs of class 6 (w=22) → 2 + 110 = 112, +20 */
    cb_reset();
    int16_t c3[7] = { 1, 0, 0, 0, 0, 0, 0 };
    set_counts(1, c3);
    CREC(1)[C_CON_CLASS] = 6;  CREC(1)[C_CON_AMT] = 5;
    T_ASSERT_EQ_I(engine_combat_army_strength(1), 2 + 22 * 5 + 0x14);

    /* full mixed stack matches the weighted sum */
    cb_reset();
    int16_t c4[7] = { 3, 1, 2, 1, 4, 1, 2 };
    set_counts(1, c4);
    int expect = 3*2 + 1*16 + 2*8 + 1*13 + 4*9 + 1*13 + 2*22 + 0x14;
    T_ASSERT_EQ_I(engine_combat_army_strength(1), expect);
    return 0;
}

/* --- FUN_004aad07: blowout — winner unscathed, loser annihilated ---------- */
int test_engine_combat_resolve_blowout(void)
{
    cb_reset();
    int16_t a[7] = { 100, 0, 0, 0, 0, 0, 0 };  /* str 220 */
    int16_t d[7] = {   1, 0, 0, 0, 0, 0, 0 };  /* str  22 */
    set_counts(1, a);
    set_counts(2, d);
    CREC(1)[C_OWNER] = 4;
    CREC(2)[C_OWNER] = 7;

    engine_combat_result_t out;
    engine_combat_resolve_casualties(1, 2, 0, 0, &out);

    /* ratio 1000 → fallback 100% survivors: attacker untouched */
    T_ASSERT_EQ_I(out.winner, 1);
    T_ASSERT_EQ_I(out.winner_owner, 4);
    T_ASSERT_EQ_I(I16(CREC(1), C_COUNTS), 100);
    T_ASSERT_EQ_I(I32(CREC(1), C_VALUE), 100);
    /* defender wiped */
    T_ASSERT_EQ_I(I16(CREC(2), C_COUNTS), 0);
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 0);
    return 0;
}

/* --- FUN_004aad07: close fight — pyrrhic winner --------------------------- */
int test_engine_combat_resolve_pyrrhic(void)
{
    cb_reset();
    int16_t a[7] = { 60, 0, 0, 0, 0, 0, 0 };   /* str 140 */
    int16_t d[7] = { 55, 0, 0, 0, 0, 0, 0 };   /* str 130 */
    set_counts(1, a);
    set_counts(2, d);

    engine_combat_resolve_casualties(1, 2, 0, 0, NULL);

    /* ratio percent_div(140,130)=107 → <110 → 10% survivors */
    T_ASSERT_EQ_I(I16(CREC(1), C_COUNTS), 6);   /* percent_of(60,10) */
    T_ASSERT_EQ_I(I32(CREC(1), C_VALUE), 6);
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 0);
    return 0;
}

/* --- FUN_004aad07: defender wins ------------------------------------------ */
int test_engine_combat_resolve_defender_wins(void)
{
    cb_reset();
    int16_t a[7] = {   1, 0, 0, 0, 0, 0, 0 };  /* str  22 */
    int16_t d[7] = { 100, 0, 0, 0, 0, 0, 0 };  /* str 220 */
    set_counts(1, a);
    set_counts(2, d);
    CREC(2)[C_OWNER] = 5;

    engine_combat_result_t out;
    engine_combat_resolve_casualties(1, 2, 0, 0, &out);

    T_ASSERT_EQ_I(out.winner, 2);
    T_ASSERT_EQ_I(out.winner_owner, 5);
    T_ASSERT_EQ_I(I16(CREC(2), C_COUNTS), 100);
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 100);
    T_ASSERT_EQ_I(I32(CREC(1), C_VALUE), 0);
    return 0;
}

/* --- FUN_004aad07: tie goes to the attacker (and is maximally pyrrhic) ----- */
int test_engine_combat_resolve_tie_to_attacker(void)
{
    cb_reset();
    int16_t a[7] = { 50, 0, 0, 0, 0, 0, 0 };   /* str 120 */
    int16_t d[7] = { 50, 0, 0, 0, 0, 0, 0 };   /* str 120 */
    set_counts(1, a);
    set_counts(2, d);

    engine_combat_result_t out;
    engine_combat_resolve_casualties(1, 2, 0, 0, &out);

    /* sa < sd is false on a tie → attacker wins; ratio 100 → <110 → 10% */
    T_ASSERT_EQ_I(out.winner, 1);
    T_ASSERT_EQ_I(I16(CREC(1), C_COUNTS), 5);   /* percent_of(50,10) */
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 0);
    return 0;
}

/* --- FUN_004aad07: quick-resolve difficulty multiplier flips the outcome --- */
int test_engine_combat_resolve_difficulty_mult(void)
{
    cb_reset();
    int16_t a[7] = { 50, 0, 0, 0, 0, 0, 0 };   /* str 120 */
    int16_t d[7] = { 50, 0, 0, 0, 0, 0, 0 };   /* str 120 */
    set_counts(1, a);
    set_counts(2, d);

    /* difficulty 4 → defender ×400% → sd=480 > sa=120 → defender wins.
     * ratio percent_div(480,120)=400 → <500 → 90% survivors. */
    engine_combat_result_t out;
    engine_combat_resolve_casualties(1, 2, 4, 1, &out);

    T_ASSERT_EQ_I(out.winner, 2);
    T_ASSERT_EQ_I(I16(CREC(2), C_COUNTS), 45);  /* percent_of(50,90) */
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 45);
    T_ASSERT_EQ_I(I32(CREC(1), C_VALUE), 0);
    return 0;
}

/* --- FUN_004aad07: merc contingent — winner scaled, loser contract freed --- */
int test_engine_combat_resolve_merc(void)
{
    cb_reset();
    int16_t a[7] = { 60, 0, 0, 0, 0, 0, 0 };
    int16_t d[7] = { 50, 0, 0, 0, 0, 0, 0 };
    set_counts(1, a);
    set_counts(2, d);
    /* attacker (winner) merc: 10 of class 6 → str 120 + 220 + 20 = 360 */
    CREC(1)[C_CON_CLASS] = 6;  CREC(1)[C_CON_AMT] = 10;
    /* defender (loser) merc: 4 of class 0, active + pool slot 5 → str 100+8+20=128 */
    CREC(2)[C_ACTIVE]    = 1;
    CREC(2)[C_CON_CLASS] = 0;  CREC(2)[C_CON_AMT] = 4;  CREC(2)[C_CON_SLOT] = 5;
    *(uint16_t *)&g_unit_table_20[5 * 0x14] = 0x1234;

    engine_combat_resolve_casualties(1, 2, 0, 0, NULL);

    /* ratio percent_div(360,128)=281 → <360 → 80% survivors */
    T_ASSERT_EQ_I(I16(CREC(1), C_COUNTS), 48);  /* percent_of(60,80) */
    T_ASSERT_EQ_I(CREC(1)[C_CON_AMT], 8);       /* percent_of(10,80) */
    T_ASSERT_EQ_I(I32(CREC(1), C_VALUE), 48 + 8);

    /* loser annihilated + merc contract released, pool slot freed */
    T_ASSERT_EQ_I(I16(CREC(2), C_COUNTS), 0);
    T_ASSERT_EQ_I(I32(CREC(2), C_VALUE), 0);
    T_ASSERT_EQ_I(CREC(2)[C_CON_CLASS], 0);
    T_ASSERT_EQ_I(CREC(2)[C_CON_AMT], 0);
    T_ASSERT_EQ_I(CREC(2)[C_CON_SLOT], 0);
    T_ASSERT_EQ_I(*(uint16_t *)&g_unit_table_20[5 * 0x14], 0);
    return 0;
}

/* --- battle sizing / setup (FUN_004a6a30) -------------------------------- */
#define C_GARRISON  0x01
#define C_COUNT     0x02

/* Neither side garrisoned → no battle UI; returns 0, role stays 0. */
int test_engine_combat_size_no_garrison(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 1; CREC(2)[C_OWNER] = 2;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 0);
    T_ASSERT_EQ_I((int)s_slot_005533d0, 0);
    return 0;
}

/* Local player is the (garrisoned) attacker, defender open field. */
int test_engine_combat_size_local_attacks(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 1; CREC(1)[C_GARRISON] = 1; CREC(1)[C_COUNT] = 4;
    CREC(2)[C_OWNER] = 2; CREC(2)[C_GARRISON] = 0; CREC(2)[C_COUNT] = 0;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 1);
    T_ASSERT_EQ_I((int)s_slot_005533d0, 1);
    T_ASSERT_EQ_I(g_combat_ctrl_408, 2);
    T_ASSERT_EQ_I(g_combat_ctrl_5b0, 2);
    T_ASSERT_EQ_I(g_combat_both_garrison, 0);
    T_ASSERT_EQ_I((int)s_slot_00568934, 4);
    T_ASSERT_EQ_I((int)s_slot_00568938, 6);          /* 0 count floored to 6 */
    T_ASSERT_EQ_I((int)s_slot_005681ec, 1);
    T_ASSERT_EQ_I((int)s_slot_005681d8, 2);
    return 0;
}

/* Both garrisoned, local player defends → role 2, control flags stay clear. */
int test_engine_combat_size_local_defends(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 2; CREC(1)[C_GARRISON] = 1; CREC(1)[C_COUNT] = 9;
    CREC(2)[C_OWNER] = 1; CREC(2)[C_GARRISON] = 1; CREC(2)[C_COUNT] = 3;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 1);
    T_ASSERT_EQ_I(g_combat_both_garrison, 1);
    T_ASSERT_EQ_I((int)s_slot_005533d0, 2);
    T_ASSERT_EQ_I(g_combat_ctrl_408, 0);
    T_ASSERT_EQ_I((int)s_slot_00568934, 9);
    T_ASSERT_EQ_I((int)s_slot_00568938, 3);
    return 0;
}

/* Defender is local but the attacker has no garrison → role still 1. */
int test_engine_combat_size_defender_local_open_attacker(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 2; CREC(1)[C_GARRISON] = 0;
    CREC(2)[C_OWNER] = 1; CREC(2)[C_GARRISON] = 1;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 1);
    T_ASSERT_EQ_I((int)s_slot_005533d0, 1);
    T_ASSERT_EQ_I(g_combat_both_garrison, 0);
    return 0;
}

static int g_sfx_calls;
static void count_sfx(void) { g_sfx_calls++; }

/* The battle cue fires once iff a battle UI is set up (a side is garrisoned). */
int test_engine_combat_size_sfx_hook(void)
{
    cb_reset();
    g_sfx_calls = 0;
    engine_combat_bind_battle_sfx(count_sfx);

    engine_combat_size_battle(1, 2, 1);          /* no garrison → no cue */
    T_ASSERT_EQ_I(g_sfx_calls, 0);

    CREC(1)[C_GARRISON] = 1;
    engine_combat_size_battle(1, 2, 1);          /* garrison → one cue */
    T_ASSERT_EQ_I(g_sfx_calls, 1);

    engine_combat_bind_battle_sfx(NULL);         /* restore default */
    return 0;
}
#undef C_GARRISON
#undef C_COUNT

/* --- FUN_004a80db: siege-engine build-state recompute (slots {count, pct, progress}) */

#define C_COND_CNT   0x182
#define C_COND_OUT   0x184
#define C_COND_STOCK 0x186
#define C_COND_LEVEL 0x19c

/* head-count 0 → early return, condition byte untouched. */
int test_engine_combat_condition_zero_head(void)
{
    cb_reset();
    uint8_t *r = CREC(4);
    I32(r, C_VALUE)      = 0;
    r[C_COND_LEVEL]      = 77;
    I16(r, C_COND_OUT)   = 12;     /* must stay untouched */
    engine_combat_recompute_condition(4);
    T_ASSERT_EQ_U(r[C_COND_LEVEL], 77);
    T_ASSERT_EQ_I(I16(r, C_COND_OUT), 12);
    return 0;
}

/* Mixed groups: under-supplied (50 %), over-supplied (clamped 100), and a
 * second under-supplied group; shortfall averaged exactly over the head. */
int test_engine_combat_condition_mixed(void)
{
    cb_reset();
    uint8_t *r = CREC(4);
    I32(r, C_VALUE) = 100;
    /* g0: cnt 10 stock 1000, req 2000 → 50 %, shortfall 1000 */
    I16(r, C_COND_CNT   + 0) = 10; I16(r, C_COND_STOCK + 0) = 1000;
    /* g1: cnt 5  stock 2000, req 1000 → 200→clamp 100, no shortfall */
    I16(r, C_COND_CNT   + 6) = 5;  I16(r, C_COND_STOCK + 6) = 2000;
    /* g2: cnt 2  stock 400,  req 800  → 50 %, shortfall 400 */
    I16(r, C_COND_CNT   + 12) = 2; I16(r, C_COND_STOCK + 12) = 400;

    engine_combat_recompute_condition(4);

    T_ASSERT_EQ_I(I16(r, C_COND_OUT + 0), 50);
    T_ASSERT_EQ_I(I16(r, C_COND_OUT + 6), 100);
    T_ASSERT_EQ_I(I16(r, C_COND_OUT + 12), 50);
    /* shortfall 1400 / head 100 = 14, remainder 0 */
    T_ASSERT_EQ_U(r[C_COND_LEVEL], 14);
    return 0;
}

/* Nonzero remainder rounds the averaged shortfall up; a zero-count group has
 * its out%/stock fields zeroed. */
int test_engine_combat_condition_roundup_and_empty(void)
{
    cb_reset();
    uint8_t *r = CREC(4);
    I32(r, C_VALUE) = 99;
    I16(r, C_COND_CNT   + 0) = 10; I16(r, C_COND_STOCK + 0) = 1000;  /* sf 1000 */
    I16(r, C_COND_CNT   + 12) = 2; I16(r, C_COND_STOCK + 12) = 400;  /* sf 400  */
    /* g1: count 0 → out%/stock zeroed */
    I16(r, C_COND_CNT   + 6) = 0;  I16(r, C_COND_OUT + 6) = 55; I16(r, C_COND_STOCK + 6) = 66;

    engine_combat_recompute_condition(4);

    T_ASSERT_EQ_I(I16(r, C_COND_OUT + 6), 0);
    T_ASSERT_EQ_I(I16(r, C_COND_STOCK + 6), 0);
    /* shortfall 1400 / 99 = 14 r 14 → 15 */
    T_ASSERT_EQ_U(r[C_COND_LEVEL], 15);
    return 0;
}

#undef C_COND_CNT
#undef C_COND_OUT
#undef C_COND_STOCK
#undef C_COND_LEVEL

/* --- FUN_004ad8cc: post-conquest garrison halve -------------------------- */
int test_engine_combat_halve_garrison(void)
{
    cb_reset();
    uint8_t *r = CREC(5);
    static const int16_t c[7] = { 20, 10, 11, 100, 5, 0, 3 };
    set_counts(5, c);
    r[C_OWNER]    = 2;
    r[C_ACTIVE]   = 1;
    r[C_CON_AMT]  = 7;
    r[0x1c]       = 9;     /* scratch — must clear */
    r[0x14c]      = 9;     /* flag    — must clear */
    g_player_strategic_352[2 * 0x160 + 0x05] = 1;   /* human wage path: /4 */
    s_slot_0053f23c = 0;

    engine_combat_halve_garrison(5);

    /* >10 halved, ≤10 wiped: 10,0,5,50,0,0,0 → sum 65, + merc 7 = 72 */
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 0), 10);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 2), 0);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 4), 5);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 6), 50);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 8), 0);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 10), 0);
    T_ASSERT_EQ_I(I16(r, C_COUNTS + 12), 0);
    T_ASSERT_EQ_I(I32(r, C_VALUE), 72);
    T_ASSERT_EQ_U(r[0x1c], 0);
    T_ASSERT_EQ_U(r[0x14c], 0);
    T_ASSERT_EQ_I(I32(r, 0x15c), 18);   /* recomputed wage 72/4 */
    return 0;
}

/* ===================================================================== *
 *  Combat-initiation SCC — the pieces that mutate save state without     *
 *  pulling in the full economy / battlefield-render machinery.           *
 *  (The full auto-resolve chain is covered byte-exactly by the retail    *
 *  diff target `combat_dispatch`.)                                        *
 * ===================================================================== */

#define R_REC(k)   (&g_per_army_768[(k) * 0x300])
#define P_REC(s)   (&g_player_strategic_352[(s) * 0x160])

static void scc_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    memset(s_slot_00565410, 0, sizeof s_slot_00565410);
    g_combat_render_active = 0;
    s_slot_0057c8cc = 1;     /* local player */
    s_slot_0053f23c = 0;     /* difficulty base → active capture penalty = 10 */
    s_slot_0056d5dc = 6;     /* player count (keeps the victory popup off) */
}

/* FUN_004a72fe: a non-local owner captures an unowned region — reassign the
 * region, bump the conquest counters, decay the region's popularity by the
 * active penalty (10), and post the "territory captured" notice (0x74). */
int test_engine_combat_transfer_ownership_reassign(void)
{
    scc_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);

    uint8_t *no  = P_REC(2);
    uint8_t *reg = R_REC(5);
    no[0x05] = 1;            /* new owner active → penalty = f23c*0x14 + 10 = 10 */
    no[0x0a] = 7;            /* banner key → copied to region +0x07 */
    reg[0x05] = 0;           /* region currently unowned */
    reg[0x0c] = 30;          /* reserve ≥ penalty → both fields debited */
    reg[0x17] = 50;

    engine_combat_transfer_ownership(2, 5, &evq, 0);

    T_ASSERT_EQ_U(reg[0x05], 2);    /* owner reassigned */
    T_ASSERT_EQ_U(no[0x29], 1);     /* conquests bumped */
    T_ASSERT_EQ_U(no[0x2a], 1);     /* high-water updated */
    T_ASSERT_EQ_U(reg[0x0c], 20);   /* 30 - 10 */
    T_ASSERT_EQ_U(reg[0x17], 40);   /* 50 - 10 */
    T_ASSERT_EQ_U(reg[0x07], 7);    /* banner copied from new owner +0x0a */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x74);
    T_ASSERT_EQ_I(evq.ring[0].target, 2);
    return 0;
}

/* The popularity decay's reserve-underflow branch: when the reserve (+0x0c) is
 * below the penalty, the popularity (+0x17) drops only by the reserve and the
 * reserve floors to 0 (FUN_004a72fe, the `(char)+0x0c < local_8` arm). */
int test_engine_combat_transfer_ownership_reserve_underflow(void)
{
    scc_reset();
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);

    uint8_t *no  = P_REC(2);
    uint8_t *reg = R_REC(5);
    no[0x05] = 1;
    reg[0x05] = 0;
    reg[0x0c] = 5;           /* reserve < penalty (10) */
    reg[0x17] = 50;

    engine_combat_transfer_ownership(2, 5, &evq, 0);

    T_ASSERT_EQ_U(reg[0x05], 2);
    T_ASSERT_EQ_U(reg[0x0c], 0);    /* reserve floored */
    T_ASSERT_EQ_U(reg[0x17], 45);   /* 50 - reserve(5) */
    return 0;
}

/* FUN_00437535: when no free cell exists near the destination (no cell grid
 * bound → the ring probe misses), the marching army is disbanded rather than
 * placed.  The placement branch would stamp the occupant's cell offset (+0x0c)
 * non-zero; instead the disband branch runs, decrementing the owner's per-type
 * strategic unit counter (+0x2d) and leaving +0x0c at 0. */
int test_engine_combat_transfer_army_no_cell_disbands(void)
{
    scc_reset();
    engine_map_query_bind_cell_descriptors(NULL);   /* force a ring miss */
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);

    uint8_t *t = CREC(4);
    t[0x00] = 3;             /* occupant owner */
    t[0x0a] = 20;            /* col */
    t[0x0b] = 20;            /* row → placement would write (20*0x40+20)*8 ≠ 0 */
    I32(t, 0x0c) = 0;
    /* owner 3's per-type counter for this unit's type (rec +0x14f == 0). */
    P_REC(3)[0x2d] = 5;

    engine_combat_transfer_army(4, 5, &evq, 0);

    T_ASSERT_EQ_I(I32(t, 0x0c), 0);    /* placement skipped */
    T_ASSERT_EQ_U(P_REC(3)[0x2d], 4);  /* disband decremented the counter */
    return 0;
}

/* ---- region-occupation reception notice (FUN_004abf77) ------------------ *
 * Posts a population-reception message (0x82..0x86) to the occupying army's
 * owner when it sits on a NEUTRAL region, chosen from the region's popularity
 * reserve (+0x0c) and the head-count/capacity fill ratio.  Notification-only.
 * scc_reset() sets the local player to 1, so an army owned by 1 publishes.    */

/* Set up army `a` (owner = local) on neutral region `r` with the given
 * head-count, capacity and popularity reserve; returns the chosen event code
 * (or -1 if nothing was published). */
static int notify_occ_code(int head, int cap, int8_t pop)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 1;                 /* army owner = local player → published */
    I32(a, 0x168) = head;        /* C_VALUE head-count */
    r[0x05] = 0;                 /* neutral region */
    r[0x0c] = (uint8_t)pop;      /* popularity reserve (signed) */
    I32(r, 0x24) = cap;          /* region capacity */
    engine_combat_notify_occupation(4, 5, &evq);
    return evq.write_idx == 1 ? evq.ring[0].code : -1;
}

/* Hostile population (reserve < 0x0a) → 0x82 regardless of fill. */
int test_engine_combat_notify_occupation_hostile(void)
{
    T_ASSERT_EQ_I(notify_occ_code(60, 100, 5), 0x82);
    return 0;
}

/* Wary population (0x0a <= reserve < 0x1e) → 0x83 regardless of fill. */
int test_engine_combat_notify_occupation_wary(void)
{
    T_ASSERT_EQ_I(notify_occ_code(60, 100, 0x14), 0x83);
    return 0;
}

/* Healthy population (reserve >= 0x1e): the message tracks the fill ratio.
 *   fill < 0x14            → 0x86   (head 10 / cap 100 = 10%)
 *   0x14 <= fill < 0x32    → 0x85   (head 30 / cap 100 = 30%)
 *   0x32 <= fill < 0x65    → 0x84   (head 60 / cap 100 = 60%)
 *   fill >= 0x65 (head>=cap) → 0x83 (clamped to 0x65) */
int test_engine_combat_notify_occupation_fill_tiers(void)
{
    T_ASSERT_EQ_I(notify_occ_code(10,  100, 0x1e), 0x86);
    T_ASSERT_EQ_I(notify_occ_code(30,  100, 0x1e), 0x85);
    T_ASSERT_EQ_I(notify_occ_code(60,  100, 0x1e), 0x84);
    T_ASSERT_EQ_I(notify_occ_code(100, 100, 0x1e), 0x83);
    return 0;
}

/* An OWNED region (R_OWNER != 0) gets no message — the function early-returns. */
int test_engine_combat_notify_occupation_owned_region_silent(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 1;  I32(a, 0x168) = 60;
    r[0x05] = 3;                 /* region already owned */
    r[0x0c] = 5;  I32(r, 0x24) = 100;
    engine_combat_notify_occupation(4, 5, &evq);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* ---- army-enters-region reconcile (FUN_004abb36) ------------------------ *
 * Latches the army's effective-region colour (+0x10), fires the neutral-region
 * reception notice, and posts the 0xaa arrival notice when the army reaches an
 * OWNED destination region (+0x151 == region).                                */

/* Neutral region: colour latched, a reception notice published (army owner is
 * local), and NO 0xaa arrival notice (that path is owned-region only). */
int test_engine_combat_army_enter_neutral(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 1;                 /* army owner = local */
    a[0x151] = 5;                /* destination key = this region */
    I32(a, 0x168) = 10;
    r[0x05] = 0;                 /* neutral */
    r[0x0c] = 5;                 /* hostile reserve → reception code 0x82 */
    I32(r, 0x24) = 100;

    engine_combat_army_enter_region(4, 5, &evq);

    T_ASSERT_EQ_U(a[0x10], 5);             /* effective-region colour latched */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x82); /* reception, not arrival */
    return 0;
}

/* Owned destination region: the army (owner 3) marching to region 5 which the
 * local player (1) owns → the 0xaa arrival notice is posted to the local owner
 * and the owner's rotating notification counter (+0x159) advances 0 → 1.  The
 * neutral reception path early-returns (region is owned). */
int test_engine_combat_army_enter_owned_arrival(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 3;                 /* army owner (not local) */
    a[0x151] = 5;                /* destination = this region */
    r[0x05] = 1;                 /* region owned by local player → publishes */
    P_REC(3)[0x159] = 0;
    P_REC(3)[0x07]  = 0;

    engine_combat_army_enter_region(4, 5, &evq);

    T_ASSERT_EQ_U(a[0x10], 5);
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xaa);
    T_ASSERT_EQ_I(evq.ring[0].target, 3);   /* army owner */
    T_ASSERT_EQ_U(P_REC(3)[0x159], 1);      /* rotation advanced */
    return 0;
}

/* Owned region the army is NOT marching to (+0x151 != region) → no 0xaa, and
 * (region owned) no reception notice either: silent but colour still latched. */
int test_engine_combat_army_enter_owned_not_destination_silent(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 3;
    a[0x151] = 9;                /* destination is some OTHER region */
    r[0x05] = 1;

    engine_combat_army_enter_region(4, 5, &evq);

    T_ASSERT_EQ_U(a[0x10], 5);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* The rotating notification counter wraps 3 → 0 (not 4). */
int test_engine_combat_army_enter_rotation_wraps(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *a = CREC(4), *r = R_REC(5);
    a[0x00] = 3;  a[0x151] = 5;  r[0x05] = 1;
    P_REC(3)[0x159] = 3;         /* about to wrap */

    engine_combat_army_enter_region(4, 5, &evq);

    T_ASSERT_EQ_U(P_REC(3)[0x159], 0);      /* 3 + 1 → wrap to 0 */
    T_ASSERT_EQ_I(evq.ring[0].code, 0xaa);
    return 0;
}

/* ---- rebellion spawn (FUN_004ac185) + army settle (FUN_004abd0f) -------- *
 * spawn_rebellion places a rebel stack (kind 2 / type 6) on an owned region
 * with a dry population reserve; settle_region is the per-tick state-2 handler
 * that warns the owner and may trigger the rebellion.                         */

enum { OCC_GRID = 0x40, OCC_STRIDE = 8 };
static uint8_t g_occ_cells[OCC_GRID * OCC_GRID * OCC_STRIDE];
static int occ_cell_off(int col, int row) { return (row * OCC_GRID + col) * OCC_STRIDE; }

/* An UNOWNED region never rebels — returns 0 and allocates no occupant. */
int test_engine_combat_spawn_rebellion_unowned(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    R_REC(5)[0x05] = 0;                 /* unowned */
    T_ASSERT_EQ_I(engine_combat_spawn_rebellion(5, &evq, 0), 0);
    return 0;
}

/* An owned region with no placeable cell near its seat (no map-query grid) —
 * both probes miss → 0. */
int test_engine_combat_spawn_rebellion_no_cell(void)
{
    scc_reset();
    engine_map_query_bind_cell_descriptors(NULL);   /* force probe misses */
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    R_REC(5)[0x05] = 2;                 /* owned */
    T_ASSERT_EQ_I(engine_combat_spawn_rebellion(5, &evq, 0), 0);
    return 0;
}

/* Bind the cell grid + records and seat region 5 at (10,10) with a single
 * rawflag-bit0 free cell there, so the probe hits and place_entity succeeds. */
static void rebellion_fixture(void)
{
    scc_reset();
    engine_army_economy_bind_army_records(NULL);    /* neuter demolish re-plan */
    memset(g_occ_cells, 0, sizeof g_occ_cells);
    engine_territory_bind_cells(g_occ_cells);
    engine_map_query_bind_cell_descriptors(g_occ_cells);
    engine_territory_bind_records(g_per_territory_420);

    uint8_t *r = R_REC(5);
    r[0x05] = 2;                        /* owned region */
    r[0x6c] = 10; r[0x6d] = 10;         /* region seat (col,row) */
    I32(r, 0x24) = 100;                 /* capacity → rebel head = 30% = 30 */
    g_occ_cells[occ_cell_off(10, 10) + 1] = 0x01;   /* rawflag bit0 set, free */
    s_slot_00553edc = 7;               /* season → stamped into +0x164 */
}

/* Full success path: rebel occupant created at slot 1 with 30% of capacity in
 * troop-class 0, the spawn turn + timer stamped, and the capacity debited. */
int test_engine_combat_spawn_rebellion_success(void)
{
    rebellion_fixture();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    T_ASSERT_EQ_I(engine_combat_spawn_rebellion(5, &evq, 0), 1);

    uint8_t *c = CREC(1);              /* first free record slot */
    T_ASSERT_EQ_U(c[0x00], 6);         /* rebel faction (type, set by placer)  */
    T_ASSERT_EQ_U(c[0x10], 5);         /* effective region                     */
    T_ASSERT_EQ_U(c[0x150], 1);
    T_ASSERT_EQ_U(c[0x166], 0x32);     /* spawn timer                          */
    T_ASSERT_EQ_I(I16(c, 0x164), 7);   /* spawn turn = season                  */
    T_ASSERT_EQ_I(I32(c, 0x168), 30);  /* head = 30% of capacity 100           */
    T_ASSERT_EQ_I(I16(c, 0x16c), 30);  /* all of it in class 0                 */
    T_ASSERT_EQ_I(I16(c, 0x17a), 30);  /* result accumulator                   */
    T_ASSERT_EQ_I(I32(R_REC(5), 0x24), 70);   /* capacity debited 100 - 30     */
    return 0;
}

/* settle on a NEUTRAL region: latch colour, no notifications. */
int test_engine_combat_settle_neutral(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    CREC(4)[0x00] = 1;
    R_REC(5)[0x05] = 0;                 /* neutral */
    engine_combat_army_settle_region(4, 5, &evq, 0);
    T_ASSERT_EQ_U(CREC(4)[0x10], 5);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* settle on an owned region, healthy reserve (>= 0x1e) → 0x9c, reserve -10. */
int test_engine_combat_settle_owned_calm(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *r = R_REC(5);
    r[0x05] = 1;                        /* owned by local → publishes */
    r[0x0c] = 40; r[0x17] = 50;
    engine_combat_army_settle_region(4, 5, &evq, 0);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x9c);
    T_ASSERT_EQ_U(r[0x0c], 30);        /* 40 - 10 */
    T_ASSERT_EQ_U(r[0x17], 40);        /* 50 - 10 */
    return 0;
}

/* settle on an owned region, wary reserve (0x0a..0x1d) → 0x9b + unrest latch. */
int test_engine_combat_settle_owned_wary(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *r = R_REC(5);
    r[0x05] = 1;
    r[0x0c] = 20; r[0x17] = 50; r[0x20] = 0;
    engine_combat_army_settle_region(4, 5, &evq, 0);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x9b);
    T_ASSERT_EQ_U(r[0x20], 1);         /* unrest latched */
    T_ASSERT_EQ_U(r[0x0c], 10);        /* 20 - 10 */
    return 0;
}

/* settle on an owned region, dry reserve (< 0x0a), no rebellion possible
 * (no map grid) → 0x9a posted, rebellion fails, reserve drains into the value
 * and floors at 0 (the < 0x0a debit arm). */
int test_engine_combat_settle_owned_hostile_no_rebellion(void)
{
    scc_reset();
    engine_map_query_bind_cell_descriptors(NULL);   /* spawn_rebellion → 0 */
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *r = R_REC(5);
    r[0x05] = 1;
    r[0x0c] = 5; r[0x17] = 50; r[0x20] = 2;
    engine_combat_army_settle_region(4, 5, &evq, 0);
    T_ASSERT_EQ_I(evq.ring[0].code, 0x9a);
    T_ASSERT_EQ_U(r[0x0c], 0);         /* reserve drained */
    T_ASSERT_EQ_U(r[0x17], 45);        /* 50 - reserve(5) */
    T_ASSERT_EQ_U(r[0x20], 2);         /* unrest untouched (rebellion failed) */
    return 0;
}

/* settle when the army's colour already equals the region → only the final
 * non-negative clamp runs; no notification, no debit. */
int test_engine_combat_settle_same_colour_noop(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *r = R_REC(5);
    r[0x05] = 1; r[0x0c] = 40; r[0x17] = 50;
    CREC(4)[0x10] = 5;                  /* already this region */
    engine_combat_army_settle_region(4, 5, &evq, 0);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    T_ASSERT_EQ_U(r[0x0c], 40);        /* untouched */
    return 0;
}

/* ---- army-stack merge (FUN_004aa181 / engine_combat_merge_stack) --------- *
 * Merge occupant `src` into `dst`: sum head-count (+0x168) / 7 troop shorts
 * (+0x16c) / 3 siege-engine slot triples (+0x182/+0x184/+0x186), keep the larger presence
 * accumulator (+0x153), inherit src's merc triple, then disband src.  The
 * disband re-plan tail is held to a no-op by leaving the army-records bind
 * NULL (same as the resign tests).                                            */

static void merge_reset(void)
{
    scc_reset();
    engine_army_economy_bind_army_records(NULL);   /* neuter disband's re-plan */
}

/* Two merc-free stacks merge: dst gains src's head-count, troop counts and
 * siege-engine slot triples, keeps the larger presence byte, and src is disbanded. */
int test_engine_combat_merge_sums_and_disbands(void)
{
    merge_reset();
    uint8_t *d = CREC(3), *s = CREC(5);
    d[0x00] = 2; I32(d, C_VALUE) = 100; d[0x153] = 5;
    I16(d, C_COUNTS) = 10; I16(d, C_COUNTS + 2) = 5;
    I16(d, 0x182) = 4; I16(d, 0x184) = 2; I16(d, 0x186) = 8;   /* siege-engine slot 0 */
    s[0x00] = 2; s[0x08] = 1; I32(s, C_VALUE) = 50; s[0x153] = 9;
    I16(s, C_COUNTS) = 3; I16(s, C_COUNTS + 2) = 2; I16(s, C_COUNTS + 4) = 1;
    I16(s, 0x182) = 1; I16(s, 0x184) = 1; I16(s, 0x186) = 2;
    P_REC(2)[0x2d] = 5;                                        /* disband decrements this */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_combat_merge_stack(3, 5, &evq);

    T_ASSERT_EQ_I(I32(d, C_VALUE), 150);     /* head-count summed */
    T_ASSERT_EQ_I(I16(d, C_COUNTS), 13);
    T_ASSERT_EQ_I(I16(d, C_COUNTS + 2), 7);
    T_ASSERT_EQ_I(I16(d, C_COUNTS + 4), 1);
    T_ASSERT_EQ_I(d[0x153], 9);              /* max presence kept */
    T_ASSERT_EQ_I(I16(d, 0x182), 5);         /* siege-engine slot 0 summed */
    T_ASSERT_EQ_I(I16(d, 0x184), 3);
    T_ASSERT_EQ_I(I16(d, 0x186), 10);
    T_ASSERT_EQ_U(CREC(5)[0x00], 0);         /* src disbanded (record cleared) */
    T_ASSERT_EQ_U(P_REC(2)[0x2d], 4);        /* disband decremented owner counter */
    return 0;
}

/* When both stacks carry a mercenary contingent the merge is refused: neither
 * record is touched and nothing is disbanded. */
int test_engine_combat_merge_refused_both_merc(void)
{
    merge_reset();
    uint8_t *d = CREC(3), *s = CREC(5);
    d[0x00] = 2; I32(d, C_VALUE) = 100; d[C_CON_AMT] = 4;
    s[0x00] = 2; s[0x08] = 1; I32(s, C_VALUE) = 50; s[C_CON_AMT] = 3;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_combat_merge_stack(3, 5, &evq);

    T_ASSERT_EQ_I(I32(d, C_VALUE), 100);     /* unchanged */
    T_ASSERT_EQ_U(CREC(5)[0x00], 2);         /* src NOT disbanded */
    return 0;
}

/* Combined head-count >= 0x5dd (1501) gates the whole merge out. */
int test_engine_combat_merge_capacity_gate(void)
{
    merge_reset();
    uint8_t *d = CREC(3), *s = CREC(5);
    d[0x00] = 2; I32(d, C_VALUE) = 1000;
    s[0x00] = 2; s[0x08] = 1; I32(s, C_VALUE) = 600;

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_combat_merge_stack(3, 5, &evq);

    T_ASSERT_EQ_I(I32(d, C_VALUE), 1000);    /* no-op */
    T_ASSERT_EQ_U(CREC(5)[0x00], 2);         /* src untouched */
    return 0;
}

/* src carries a merc contingent and dst does not → dst inherits src's merc
 * triple (class / amount / pool slot).  Quirk #168: because src keeps its own
 * +0x197 slot, the subsequent disband(src) → clear_contract frees that same
 * g_unit_table_20 slot — so dst inherits a contract whose pool entry is gone. */
int test_engine_combat_merge_inherits_src_merc(void)
{
    merge_reset();
    uint8_t *d = CREC(3), *s = CREC(5);
    d[0x00] = 2; I32(d, C_VALUE) = 100;
    s[0x00] = 2; s[0x08] = 1; I32(s, C_VALUE) = 50;
    s[C_CON_AMT] = 7; s[C_CON_CLASS] = 3; s[C_CON_SLOT] = 4;
    *(uint16_t *)&g_unit_table_20[4 * 0x14] = 0xbeef;   /* the live pool slot */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_combat_merge_stack(3, 5, &evq);

    T_ASSERT_EQ_I(d[C_CON_AMT], 7);
    T_ASSERT_EQ_I(d[C_CON_CLASS], 3);
    T_ASSERT_EQ_I(d[C_CON_SLOT], 4);
    /* quirk #168: src's disband freed the inherited pool slot out from under dst */
    T_ASSERT_EQ_I(*(uint16_t *)&g_unit_table_20[4 * 0x14], 0);
    return 0;
}

/* dst holds the survive-linkage byte (+0x199): the merge keeps it, re-homes the
 * survivor's back-reference (region pending slot → +0x19a) onto dst, and runs
 * the siege-engine build-state recompute. */
int test_engine_combat_merge_rehomes_survivor(void)
{
    merge_reset();
    uint8_t *d = CREC(3), *s = CREC(5);
    d[0x00] = 2; I32(d, C_VALUE) = 80; d[0x199] = 2;   /* survive flag = region 2 */
    s[0x00] = 2; s[0x08] = 1; I32(s, C_VALUE) = 40;
    I32(R_REC(2), 0x1bc) = 7;                          /* region 2's pending unit = 7 */

    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    engine_combat_merge_stack(3, 5, &evq);

    T_ASSERT_EQ_I(I32(d, C_VALUE), 120);     /* still merged */
    T_ASSERT_EQ_U(d[0x199], 2);              /* survive flag kept */
    T_ASSERT_EQ_U(CREC(7)[0x19a], 3);        /* back-reference re-homed to dst */
    return 0;
}

/* ---- settlement-spawn cluster (FUN_004a551b / 4a9a9a / 4a79a3) ----------- *
 * Garrison placement (engine_combat_place_garrison) homes a freshly-spawned   *
 * occupant onto a realm's keep cell, capping the combined head-count against   *
 * the keep-level table {2500,150,200,200,400,600,0,50,75,100}.  The unit-spawn *
 * (engine_combat_spawn_unit) allocates an occupant near the army's seat and    *
 * fills it from the owner's primed muster pool, debiting population + cashflow. *
 * The economy re-plan tails are held to no-ops with the army-records bind NULL.*/

/* Reset + neuter the economy re-plan tail (collect_territory_income / spawn). */
static void spawn_reset(void)
{
    scc_reset();
    engine_army_economy_bind_army_records(NULL);
}

/* Over the keep-level cap → occupant rejected (role 2), nothing else placed. */
int test_engine_combat_place_garrison_over_cap(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    a[0x1c0] = 1;                       /* keep level 1 → cap k_garrison_cap[1]=150 */
    uint8_t *c = CREC(4);
    I32(c, C_VALUE) = 200;              /* 200 > 150 → reject */
    c[0x14c] = 7;                       /* must be cleared */

    engine_combat_place_garrison(4, 5, &evq);

    T_ASSERT_EQ_U(c[0x1a], 2);          /* role = rejected */
    T_ASSERT_EQ_U(c[0x14c], 0);         /* cleared */
    T_ASSERT_EQ_I(I32(R_REC(5), 0x1bc), 0);   /* no garrison pending set */
    return 0;
}

/* Under cap, no garrison pending → occupant becomes the garrison (role 5) on
 * the keep's seat cell, and the realm's pending slot points at it. */
int test_engine_combat_place_garrison_becomes_garrison(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    a[0x1c0] = 2;                       /* keep level 2 → cap 200 */
    a[0x05] = 3;                        /* race (≠ local player) → no view dirty */
    a[0x74] = 10;                       /* seat col */
    a[0x75] = 12;                       /* seat row */
    uint8_t *c = CREC(4);
    I32(c, C_VALUE) = 100;              /* 100 < 200 → placed */
    c[0x153] = 4;                       /* presence accumulator, += 5 */

    engine_combat_place_garrison(4, 5, &evq);

    T_ASSERT_EQ_I(I32(R_REC(5), 0x1bc), 4);   /* garrison pending = this occupant */
    T_ASSERT_EQ_U(c[0x1a], 5);          /* role = garrison */
    T_ASSERT_EQ_U(c[0x14], 10);         /* home col ← seat */
    T_ASSERT_EQ_U(c[0x0a], 10);         /* col ← home col */
    T_ASSERT_EQ_U(c[0x15], 12);         /* home row ← seat */
    T_ASSERT_EQ_U(c[0x0b], 12);         /* row ← home row */
    T_ASSERT_EQ_I(I32(c, 0x0c), (12 * 0x40 + 10) * 8);   /* cell offset */
    T_ASSERT_EQ_U(c[0x150], 1);
    T_ASSERT_EQ_U(c[0x06], 1);
    T_ASSERT_EQ_U(c[0x153], 9);         /* 4 + 5 */
    T_ASSERT_EQ_U(c[0x151], 5);         /* spawning key */
    T_ASSERT_EQ_U(c[0x198], 5);         /* spawning key */
    return 0;
}

/* Under cap WITH a garrison pending → the occupant is merged into the pending
 * stack (head-count summed, source disbanded) rather than placed afresh. */
int test_engine_combat_place_garrison_merges_when_pending(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    a[0x1c0] = 4;                       /* keep level 4 → cap 400 */
    I32(a, 0x1bc) = 3;                  /* garrison pending = occupant 3 (dst) */

    uint8_t *dst = CREC(3), *src = CREC(4);
    dst[0x00] = 2; I32(dst, C_VALUE) = 100;
    src[0x00] = 2; src[0x08] = 1; I32(src, C_VALUE) = 50;   /* 150 < 400 */
    P_REC(2)[0x2d] = 5;                 /* disband decrements this */

    engine_combat_place_garrison(4, 5, &evq);

    T_ASSERT_EQ_I(I32(dst, C_VALUE), 150);   /* merged head-count */
    T_ASSERT_EQ_U(P_REC(2)[0x2d], 4);        /* src disbanded */
    return 0;
}

/* ---- region-event-hi "enter keep" chain (FUN_004686a0 + siege chain) ----- *
 * The dispatcher routes a live placed stack entering a keep region: owner ==   *
 * region race → reinforce (place_garrison); else → begin siege.  The siege path *
 * links besieger↔defender and seeds the besieger's three siege-engine slots by  *
 * the owner race's unit-category {race1→8, race2→9, race3/4→7, ...}.            */

/* A dead occupant (C_ACTIVE != 1) is ignored entirely. */
int test_engine_combat_region_event_hi_dead_noop(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *c = CREC(4);
    c[0x08] = 0;                         /* not a live unit */
    c[0x00] = 3;
    R_REC(5)[0x05] = 9;                  /* region race differs → would siege */
    I32(R_REC(5), 0x1bc) = 6;            /* a defender exists */

    engine_combat_region_event_hi(4, 5, &evq);

    T_ASSERT_EQ_U(c[0x150], 0);          /* nothing placed */
    T_ASSERT_EQ_U(c[0x199], 0);          /* no siege link */
    return 0;
}

/* Owner matches the region race → reinforce: the live stack homes onto the keep
 * as its garrison (place_garrison, role 5, pending slot points at it). */
int test_engine_combat_region_event_hi_reinforces(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    a[0x05] = 3;                         /* region race */
    a[0x1c0] = 2;                        /* keep level 2 → cap 200 */
    a[0x74] = 10; a[0x75] = 12;          /* seat */
    uint8_t *c = CREC(4);
    c[0x08] = 1;                         /* live unit */
    c[0x00] = 3;                         /* owner == region race → reinforce */
    I32(c, C_VALUE) = 100;               /* under cap → placed */

    engine_combat_region_event_hi(4, 5, &evq);

    T_ASSERT_EQ_I(I32(a, 0x1bc), 4);     /* garrison pending = this occupant */
    T_ASSERT_EQ_U(c[0x1a], 5);           /* role = garrison */
    return 0;
}

/* Owner differs from the region race → siege: the besieger links to the region's
 * defending garrison (occ +0x150/+0x199, defender +0x19a). */
int test_engine_combat_region_event_hi_begins_siege(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    a[0x05] = 9;                         /* region race ≠ owner → siege */
    I32(a, 0x1bc) = 6;                   /* defending garrison = occupant 6 */
    uint8_t *c = CREC(4);
    c[0x08] = 1;                         /* live unit */
    c[0x00] = 3;                         /* owner */
    c[0x01] = 0;                         /* viewer-interactive path */
    I32(c, C_VALUE) = 0;                 /* head 0 → recompute_condition no-op */
    P_REC(3)[0x07] = 2;                  /* owner race 2 → unit type 9 */

    engine_combat_region_event_hi(4, 5, &evq);

    T_ASSERT_EQ_U(c[0x150], 1);          /* besieger placed */
    T_ASSERT_EQ_U(c[0x199], 5);          /* besieged region recorded */
    T_ASSERT_EQ_U(CREC(6)[0x19a], 4);    /* defender back-links to besieger */
    return 0;
}

/* begin_siege guard rejections: no defender, siege-block (+0x1c2), the
 * "+0x1c3 == 1 && unbuilt keep" lockout, and an already-besieged defender. */
int test_engine_combat_begin_siege_guards(void)
{
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    /* (a) no defending garrison → no-op */
    spawn_reset();
    I32(R_REC(5), 0x1bc) = 0;
    engine_combat_begin_siege(4, 5, &evq);
    T_ASSERT_EQ_U(CREC(4)[0x199], 0);

    /* (b) siege-block flag set → no-op */
    spawn_reset();
    I32(R_REC(5), 0x1bc) = 6;
    R_REC(5)[0x1c2] = 1;
    engine_combat_begin_siege(4, 5, &evq);
    T_ASSERT_EQ_U(CREC(4)[0x199], 0);

    /* (c) +0x1c3 == 1 with unbuilt keep (+0x1c1 == 0) → no-op */
    spawn_reset();
    I32(R_REC(5), 0x1bc) = 6;
    R_REC(5)[0x1c3] = 1; R_REC(5)[0x1c1] = 0;
    engine_combat_begin_siege(4, 5, &evq);
    T_ASSERT_EQ_U(CREC(4)[0x199], 0);

    /* (d) defending garrison already besieged (+0x19a != 0) → no-op */
    spawn_reset();
    I32(R_REC(5), 0x1bc) = 6;
    CREC(6)[0x19a] = 7;
    engine_combat_begin_siege(4, 5, &evq);
    T_ASSERT_EQ_U(CREC(4)[0x199], 0);
    return 0;
}

/* Siege board seed, race 1 (unit type 8) → lane-1 count = 4; all other lanes 0.
 * Head-count 0 keeps recompute_condition a no-op so the seeds are observable. */
int test_engine_combat_begin_siege_board_type8(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    I32(R_REC(5), 0x1bc) = 6;            /* defender */
    uint8_t *c = CREC(4);
    c[0x00] = 3;                         /* owner */
    c[0x01] = 0;                         /* interactive path → board built */
    c[0x19c] = 7;                        /* condition level must clear */
    I32(c, C_VALUE) = 0;                 /* recompute no-op */
    P_REC(3)[0x07] = 1;                  /* owner race 1 → unit type 8 */

    engine_combat_begin_siege(4, 5, &evq);

    T_ASSERT_EQ_U(c[0x19c], 0);          /* condition level cleared */
    T_ASSERT_EQ_I(I16(c, 0x182), 0);     /* lane 0 count */
    T_ASSERT_EQ_I(I16(c, 0x188), 4);     /* lane 1 count = 4 (type 8) */
    T_ASSERT_EQ_I(I16(c, 0x18e), 0);     /* lane 2 count */
    return 0;
}

/* Siege board seed, race 3 (unit type 7) with a high keep (+0x1c0 > 3) in late
 * season (>= 3) → lane 0 = 3 and lane 2 = 1. */
int test_engine_combat_begin_siege_board_type7_keep(void)
{
    spawn_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);

    uint8_t *a = R_REC(5);
    I32(a, 0x1bc) = 6;                   /* defender */
    a[0x1c0] = 4;                        /* keep level 4 (> 3) */
    uint8_t *c = CREC(4);
    c[0x00] = 3;                         /* owner */
    c[0x01] = 0;                         /* interactive path */
    I32(c, C_VALUE) = 0;                 /* recompute no-op */
    P_REC(3)[0x07] = 3;                  /* owner race 3 → unit type 7 */
    s_slot_0057c934 = 4;                 /* season >= 3 */

    engine_combat_begin_siege(4, 5, &evq);

    T_ASSERT_EQ_I(I16(c, 0x182), 3);     /* lane 0 count = 3 (type 7) */
    T_ASSERT_EQ_I(I16(c, 0x188), 2);     /* lane 1 count = 2 (base) */
    T_ASSERT_EQ_I(I16(c, 0x18e), 1);     /* lane 2 count = 1 (keep>3 & late season) */
    return 0;
}

/* Bind a shared cell grid + occupant records so engine_combat_spawn_unit can
 * run end-to-end: probe → place_entity → populate from the muster pool. */
enum { SPAWN_GRID = 0x40, SPAWN_STRIDE = 8, SPAWN_COORD_OFF = 0x6c };
static uint8_t g_spawn_cells[SPAWN_GRID * SPAWN_GRID * SPAWN_STRIDE];

/* Full spawn: occupant created near army `army`'s seat, value/counts copied from
 * the owner's primed muster pool, population + cashflow debited, roster bumped. */
int test_engine_combat_spawn_unit_populates(void)
{
    spawn_reset();
    memset(g_spawn_cells, 0, sizeof g_spawn_cells);
    /* a free, plantable bit0 cell at the army's seat (flag 1: probe bit0 hit +
     * place_entity unblocked since (1 & 0xfc) == 0). */
    g_spawn_cells[(20 * SPAWN_GRID + 20) * SPAWN_STRIDE + 1] = 1;
    engine_map_query_bind_cell_descriptors(g_spawn_cells);
    engine_territory_bind_cells(g_spawn_cells);
    engine_territory_bind_records(g_per_territory_420);

    int army = 2, owner = 2;
    R_REC(army)[SPAWN_COORD_OFF + army * 3]     = 20;   /* army seat col */
    R_REC(army)[SPAWN_COORD_OFF + army * 3 + 1] = 20;   /* army seat row */
    R_REC(army)[0x05] = owner;                          /* race */
    R_REC(army)[0x0c] = 9;                              /* +0x166 source byte  */
    I32(R_REC(army), 0x24) = 500;                       /* population */
    I32(R_REC(army), 0x38) = 500;                       /* army change */

    P_REC(owner)[0x05] = 1;             /* owner active (→ occupant garrison byte) */
    P_REC(owner)[0x0a] = 4;             /* → occupant count byte */

    /* Prime the owner's muster pool: value at +0x70, per-class counts at slot+0. */
    uint8_t *pool = engine_game_init_troop_pool_row(owner);
    memset(pool, 0, 0x80);
    *(int32_t *)(pool + 0x70) = 120;    /* staged head-count → occupant +0x168 */
    for (int k = 0; k < 7; k++)
        *(int32_t *)(pool + k * 0x10) = k + 1;   /* class counts → +0x16c.. */
    for (int i = 1; i < 7; i++)
        *(int32_t *)(pool + i * 0x10) = i + 1;   /* (cashflow debit reads these) */

    int ret = engine_combat_spawn_unit(owner, army, 0, 0);
    T_ASSERT(ret != 0);

    uint8_t *c = CREC(ret);
    T_ASSERT_EQ_U(c[0x06], 1);
    T_ASSERT_EQ_U(c[0x150], 1);
    T_ASSERT_EQ_U(c[0x01], 1);          /* garrison ← owner active */
    T_ASSERT_EQ_U(c[0x10], army);
    T_ASSERT_EQ_U(c[0x11], army);
    T_ASSERT_EQ_U(c[0x02], 4);          /* count ← owner +0x0a */
    T_ASSERT_EQ_U(c[0x166], 9);
    T_ASSERT_EQ_I(I32(c, C_VALUE), 120);            /* ← pool +0x70 */
    T_ASSERT_EQ_I(I16(c, C_COUNTS + 2), 2);         /* class-1 count */
    /* population debited by the staged head-count (pool +0x70). */
    T_ASSERT_EQ_I(I32(R_REC(army), 0x24), 380);
    T_ASSERT_EQ_I(I32(R_REC(army), 0x38), 380);

    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(NULL);
    return 0;
}

/* engine_combat_muster_garrison (FUN_004a5389): size a keep garrison at pct% of
 * the county's population, distribute the head across troop classes 5/1/3/2/6 from
 * the owner's primed category reserves, then spawn.  population 200 @ pct 50 →
 * head 100; cats {1:40,2:30,3:20,4:0,5:50,6:10} fill in order 5,1,3,2,6 against
 * the shrinking slot-0 pool: class5←50 (pool 100→50), class1←40 (→10), class3←10
 * (→0), class2/6←0 (pool drained). */
int test_engine_combat_muster_garrison_distributes(void)
{
    spawn_reset();
    memset(g_spawn_cells, 0, sizeof g_spawn_cells);
    g_spawn_cells[(20 * SPAWN_GRID + 20) * SPAWN_STRIDE + 1] = 1;   /* free bit0 cell */
    engine_map_query_bind_cell_descriptors(g_spawn_cells);
    engine_territory_bind_cells(g_spawn_cells);
    engine_territory_bind_records(g_per_territory_420);

    int region = 2, owner = 2;
    R_REC(region)[SPAWN_COORD_OFF + region * 3]     = 20;   /* region seat col */
    R_REC(region)[SPAWN_COORD_OFF + region * 3 + 1] = 20;   /* region seat row */
    R_REC(region)[0x05] = owner;                            /* region owner */
    I32(R_REC(region), 0x24) = 200;                         /* population (+0x24) */

    /* Owner's six cashflow-category reserves (strat +0x140..+0x154 = cats 1..6). */
    const int32_t cats[6] = { 40, 30, 20, 0, 50, 10 };
    for (int i = 0; i < 6; i++)
        I32(P_REC(owner), 0x140 + i * 4) = cats[i];

    int ret = engine_combat_muster_garrison(region, 50);
    T_ASSERT(ret != 0);

    uint8_t *c = CREC(ret);
    T_ASSERT_EQ_I(I32(c, C_VALUE), 100);            /* staged head ← percent_of(200,50) */
    T_ASSERT_EQ_I(I16(c, C_COUNTS + 5 * 2), 50);    /* class 5 filled first */
    T_ASSERT_EQ_I(I16(c, C_COUNTS + 1 * 2), 40);    /* class 1 */
    T_ASSERT_EQ_I(I16(c, C_COUNTS + 3 * 2), 10);    /* class 3 (pool ran dry) */
    T_ASSERT_EQ_I(I16(c, C_COUNTS + 2 * 2), 0);     /* class 2 got nothing */

    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(NULL);
    return 0;
}

/* No reachable free cell near the army's seat → spawn returns 0, no record. */
int test_engine_combat_spawn_unit_no_cell(void)
{
    spawn_reset();
    engine_map_query_bind_cell_descriptors(NULL);   /* every probe misses */
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(g_per_territory_420);

    int ret = engine_combat_spawn_unit(2, 2, 0, 0);
    T_ASSERT_EQ_I(ret, 0);

    engine_territory_bind_records(NULL);
    return 0;
}

/* ---- defender muster prime (FUN_004aaa80) -------------------------------- *
 * Reset the region owner's muster pool and seed it from the owner's six       *
 * cashflow-category reserves (strat +0x140..+0x154), parking the staged head  *
 * `count` in contingent slots 0 and 7 (fields +0x00 / +0x08).                 */
int test_engine_combat_prime_muster(void)
{
    int region = 3, owner = 2;
    int32_t count = 99;

    memset(R_REC(region), 0, 0x300);
    R_REC(region)[0x05] = owner;                       /* region owner */

    memset(P_REC(owner), 0, 0x160);
    const int32_t res[6] = { 10, 20, 30, 40, 50, 60 }; /* cats 1..6 */
    for (int i = 0; i < 6; i++)
        I32(P_REC(owner), 0x140 + i * 4) = res[i];

    /* Dirty the pool so the clears are observable; +0x0c is never touched. */
    uint8_t *pool = engine_game_init_troop_pool_row(owner);
    memset(pool, 0xAA, 0x80);

    engine_combat_prime_muster(region, count);

    /* Slots 1..6: +0x00 cleared, +0x04 and +0x08 = the matching reserve. */
    for (int i = 1; i < 7; i++) {
        T_ASSERT_EQ_I(I32(pool, i * 0x10 + 0x00), 0);
        T_ASSERT_EQ_I(I32(pool, i * 0x10 + 0x04), res[i - 1]);
        T_ASSERT_EQ_I(I32(pool, i * 0x10 + 0x08), res[i - 1]);
    }
    /* Slot 0 + slot 7 hold the staged head at +0x00 and +0x08; their +0x04 is
     * cleared (outside the 1..6 reserve copy). */
    T_ASSERT_EQ_I(I32(pool, 0x00), count);
    T_ASSERT_EQ_I(I32(pool, 0x04), 0);
    T_ASSERT_EQ_I(I32(pool, 0x08), count);
    T_ASSERT_EQ_I(I32(pool, 0x70), count);
    T_ASSERT_EQ_I(I32(pool, 0x74), 0);
    T_ASSERT_EQ_I(I32(pool, 0x78), count);
    return 0;
}

/* ---- defender muster/spawn (FUN_004a50ae) -------------------------------- *
 * Raise a defending garrison sized at pct% of the county's population, stage    *
 * its troops in the owner's muster pool, then spawn.  Below the population floor *
 * it returns 0 untouched.  Mode 2 seeds three strat reserve cells and          *
 * distributes the sized head into class slots by band; mode 1 recruits 10-at-  *
 * a-time from the primed reserves.  The staging is observable even when the     *
 * spawn itself fails (no free cell bound).                                      */
int test_engine_combat_muster_defender_below_min(void)
{
    int region = 4, owner = 3;
    memset(R_REC(region), 0, 0x300);
    R_REC(region)[0x05] = owner;
    I32(R_REC(region), 0x24) = 30;                 /* population below floor */

    /* min_pop 40 > population 30 → bail before any staging or spawn. */
    int ret = engine_combat_muster_defender(region, 40, 40, 2);
    T_ASSERT_EQ_I(ret, 0);
    return 0;
}

int test_engine_combat_muster_defender_mode2_distributes(void)
{
    int region = 4, owner = 3;
    memset(R_REC(region), 0, 0x300);
    R_REC(region)[0x05] = owner;
    I32(R_REC(region), 0x24) = 1000;               /* population */
    memset(P_REC(owner), 0, 0x160);

    uint8_t *pool = engine_game_init_troop_pool_row(owner);
    memset(pool, 0, 0x80);

    /* No cell grid bound → spawn fails, return 0, but the staging stands.
     * sized = 1000 * 40 / 100 = 400 → band 0x168..0x1df. */
    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    int ret = engine_combat_muster_defender(region, 40, 40, 2);
    T_ASSERT_EQ_I(ret, 0);

    /* Three strat reserve cells seeded to 500. */
    T_ASSERT_EQ_I(I32(P_REC(owner), 0x144), 500);
    T_ASSERT_EQ_I(I32(P_REC(owner), 0x14c), 500);
    T_ASSERT_EQ_I(I32(P_REC(owner), 0x150), 500);
    /* Class distribution for the 360..479 band: slot5=100, slot4=70. */
    T_ASSERT_EQ_I(I32(pool, 5 * 0x10), 100);
    T_ASSERT_EQ_I(I32(pool, 4 * 0x10), 0x46);
    /* slot 0 starts at the staged head (400) and is debited 0xaa (170). */
    T_ASSERT_EQ_I(I32(pool, 0 * 0x10), 400 - 0xaa);
    return 0;
}

int test_engine_combat_muster_defender_mode1_recruits(void)
{
    int region = 4, owner = 3;
    memset(R_REC(region), 0, 0x300);
    R_REC(region)[0x05] = owner;
    I32(R_REC(region), 0x24) = 100;                /* population */
    memset(P_REC(owner), 0, 0x160);
    /* Owner reserves cats 1..6 (strat +0x140..+0x154) = 100 each → seeded into
     * muster slots 1..6 +0x08 by prime_muster. */
    for (int i = 0; i < 6; i++)
        I32(P_REC(owner), 0x140 + i * 4) = 100;

    uint8_t *pool = engine_game_init_troop_pool_row(owner);
    memset(pool, 0, 0x80);

    /* sized = 100 * 40 / 100 = 40 budget → 4 moves of 10 (slots 1..4), then the
     * budget hits 0 and the sweep bails.  Spawn fails (no cell) → return 0. */
    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    int ret = engine_combat_muster_defender(region, 40, 40, 1);
    T_ASSERT_EQ_I(ret, 0);

    for (int c = 1; c <= 4; c++) {
        T_ASSERT_EQ_I(I32(pool, c * 0x10 + 0x00), 10);   /* recruited */
        T_ASSERT_EQ_I(I32(pool, c * 0x10 + 0x08), 90);   /* reserve drained */
    }
    T_ASSERT_EQ_I(I32(pool, 5 * 0x10 + 0x00), 0);        /* slots 5/6 untouched */
    T_ASSERT_EQ_I(I32(pool, 5 * 0x10 + 0x08), 100);
    T_ASSERT_EQ_I(I32(pool, 0 * 0x10), 0);               /* 40 staged − 40 = 0 */
    return 0;
}

int test_engine_combat_muster_defender_spawns(void)
{
    spawn_reset();
    memset(g_spawn_cells, 0, sizeof g_spawn_cells);
    g_spawn_cells[(20 * SPAWN_GRID + 20) * SPAWN_STRIDE + 1] = 1;
    engine_map_query_bind_cell_descriptors(g_spawn_cells);
    engine_territory_bind_cells(g_spawn_cells);
    engine_territory_bind_records(g_per_territory_420);

    int army = 2, owner = 2;
    R_REC(army)[SPAWN_COORD_OFF + army * 3]     = 20;
    R_REC(army)[SPAWN_COORD_OFF + army * 3 + 1] = 20;
    R_REC(army)[0x05] = owner;
    I32(R_REC(army), 0x24) = 500;                  /* population (≥ min_pop) */
    I32(R_REC(army), 0x38) = 500;
    P_REC(owner)[0x05] = 1;

    /* mode 0: plain spawn at the happiness-cost price; returns the new occupant. */
    int ret = engine_combat_muster_defender(army, 40, 40, 0);
    T_ASSERT(ret != 0);
    T_ASSERT_EQ_I(ret, engine_territory_current_slot());

    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(NULL);
    return 0;
}

/* ---- combat_b: contest region (FUN_004a6c68) ---------------------------- *
 * An attacker stepping into a foreign region selects/raises its defender, then  *
 * resolves the fight — or seizes the region outright when it is undefended.      *
 * These cover combat_b's own logic: the three entry guards, the bookkeeping     *
 * latches, and the defender-selection routing into the no-defender transfer.    *
 * The defender-found resolve tail (size→resolve→aftermath→absorb) is exercised   *
 * by the resolver's own tests and the live seam.                                 */
int test_engine_combat_contest_region_same_owner_noop(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *att = CREC(4);
    att[0x00] = 2;                  /* owner */
    att[0x08] = 1;                  /* live unit */
    R_REC(5)[0x05] = 2;             /* region held by the same player */
    engine_combat_contest_region(4, 5, &evq, 0);
    T_ASSERT_EQ_U(att[0x150], 0);   /* no bookkeeping → bailed at the guard */
    T_ASSERT_EQ_U(att[0x153], 0);
    return 0;
}

int test_engine_combat_contest_region_dead_attacker_noop(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *att = CREC(4);
    att[0x00] = 2; att[0x08] = 0;   /* owned but not a live unit */
    R_REC(5)[0x05] = 3;
    engine_combat_contest_region(4, 5, &evq, 0);
    T_ASSERT_EQ_U(att[0x150], 0);
    return 0;
}

int test_engine_combat_contest_region_enemy_garrison_noop(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *att = CREC(4);
    att[0x00] = 2; att[0x08] = 1;
    uint8_t *reg = R_REC(5);
    reg[0x05]    = 3;               /* enemy region */
    reg[0x1c0]   = 1;               /* fortified (keep present) */
    I32(reg, 0x1bc) = 7;            /* pending garrison = occupant 7 */
    CREC(7)[0x00] = 3;              /* pending owner ≠ attacker → blocked */
    engine_combat_contest_region(4, 5, &evq, 0);
    T_ASSERT_EQ_U(att[0x150], 0);
    return 0;
}

int test_engine_combat_contest_region_undefended_seizes(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *att = CREC(4);
    att[0x00] = 2; att[0x08] = 1;
    P_REC(2)[0x05] = 1;             /* attacker realm active (capture penalty) */
    uint8_t *reg = R_REC(5);
    reg[0x05] = 0;                  /* neutral */
    reg[0x0c] = 5;                  /* reserve < 0x0b → no garrison raised */
    reg[0x17] = 40;
    s_slot_00568218 = 0;            /* clear any persisted defender */

    engine_combat_contest_region(4, 5, &evq, 0);

    T_ASSERT_EQ_U(reg[0x05], 2);                /* seized by the attacker's owner */
    T_ASSERT_EQ_U(att[0x153], 8);               /* presence accumulator += 8 */
    T_ASSERT_EQ_U(att[0x150], 1);               /* placed flag */
    T_ASSERT_EQ_I((int)s_slot_0056822c, 5);     /* battle region latched */
    T_ASSERT_EQ_I((int)s_slot_00568218, 0);     /* no defender */
    return 0;
}

int test_engine_combat_contest_region_neutral_muster_fails_seizes(void)
{
    scc_reset();
    engine_event_queue_state_t evq; engine_event_queue_init(&evq);
    uint8_t *att = CREC(4);
    att[0x00] = 2; att[0x08] = 1;
    P_REC(2)[0x05] = 1;
    uint8_t *reg = R_REC(5);
    reg[0x05] = 0;                  /* neutral */
    reg[0x0c] = 20;                 /* reserve ≥ 0x0b → difficulty muster attempted */
    reg[0x17] = 40;
    I32(reg, 0x24) = 10;            /* population < muster floor (0x28) → muster 0 */
    s_slot_0053f23c = 1;            /* difficulty 1 → the 0x28 pct branch */
    s_slot_00568218 = 0;

    engine_combat_contest_region(4, 5, &evq, 0);

    T_ASSERT_EQ_U(reg[0x05], 2);                /* muster produced nothing → seized */
    T_ASSERT_EQ_I((int)s_slot_00568218, 0);
    return 0;
}

/* ---- player resignation (FUN_0049cf20 / FUN_0049b2a3) -------------------- *
 * resign_player demolishes a player's realms (R_REC +0x05 owner zeroed) and  *
 * disbands their placed units (CREC), then wipes the strategic record and    *
 * the per-player aux slot (g_player_aux_44, stride 0x2c).  The economy        *
 * re-plan is held to a no-op by leaving the army-records bind NULL.          */

/* the battlefield partition (engine_bf_input.c, bf_47fe0b) is not linked into the host tests: these never run a field
 * battle (DAT_0057c8fc != 2), so the resign pass never calls it */
int bf_47fe0b(int p);
int bf_47fe0b(int p) { (void)p; return 0; }

static void resign_reset(void)
{
    scc_reset();
    memset(g_player_aux_44, 0, sizeof g_player_aux_44);
    engine_army_economy_bind_army_records(NULL);
    s_slot_0057c8fc = 0;       /* no field battle → 0x47fe0b (bf_47fe0b) not called */
}

#define AUX(p)  (&g_player_aux_44[(p) * 0x2c])

/* A player owning two realms and one placed unit is eliminated: realms'
 * owner byte zeroed, unit disbanded, strategic record wiped, aux reset, and
 * the "eliminated" notice (code 0xe0) posted to the local player. */
int test_engine_combat_resign_eliminates(void)
{
    resign_reset();
    s_slot_0056d5dc = 8;                 /* ≤ 16 → all realms hit by the demolish loop */
    R_REC(2)[0x05] = 3;                  /* player 3 owns realms 2 and 3 */
    R_REC(3)[0x05] = 3;
    uint8_t *u = CREC(5);
    u[0x08] = 1;                         /* a live placed unit */
    u[0x00] = 3;                         /* owned by player 3 */
    P_REC(3)[0x05] = 1;                  /* in-game */

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_combat_resign_player(3, &evq, 1);

    T_ASSERT_EQ_U(R_REC(2)[0x05], 0);    /* realms demolished */
    T_ASSERT_EQ_U(R_REC(3)[0x05], 0);
    T_ASSERT_EQ_U(CREC(5)[0x00], 0);     /* unit disbanded (record cleared) */
    T_ASSERT_EQ_U(P_REC(3)[0x05], 0);    /* strategic record wiped */
    T_ASSERT_EQ_U(AUX(3)[0x27], 1);      /* aux slot reset */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe0);
    T_ASSERT_EQ_I(evq.ring[0].target, 3);
    T_ASSERT_EQ_I(evq.ring[0].audience, 1);
    return 0;
}

/* Quirk #167: the survivor recount runs AFTER the demolish/disband loops over
 * the same tables, so it is always 0 — even with the slot byte (+0x04) set,
 * which would otherwise select the 0x102 "resigned" branch, a player owning
 * realms is still reported eliminated (0xe0) because those realms were already
 * demolished before the recount.  The 0x102 branch is dead code. */
int test_engine_combat_resign_recount_always_zero(void)
{
    resign_reset();
    s_slot_0056d5dc = 8;
    R_REC(2)[0x05] = 3; R_REC(3)[0x05] = 3;   /* player 3 owns two realms (≤16) */
    P_REC(3)[0x04] = 1;                  /* slot byte set → 0x102 IF any survived */
    P_REC(3)[0x05] = 1;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_combat_resign_player(3, &evq, 1);

    T_ASSERT_EQ_U(R_REC(2)[0x05], 0);    /* demolished before the recount */
    T_ASSERT_EQ_U(R_REC(3)[0x05], 0);
    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xe0);   /* eliminated, not 0x102 */
    return 0;
}

/* The sweep is a no-op outside multiplayer. */
int test_engine_combat_resign_sweep_sp_noop(void)
{
    resign_reset();
    P_REC(2)[0x05] = 1;                  /* eligible but SP */
    s_slot_00553f98 = 4;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_combat_mp_resign_sweep(&evq, 0);

    T_ASSERT_EQ_U(P_REC(2)[0x05], 1);    /* untouched */
    T_ASSERT_EQ_U(s_slot_00553f98, 4);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* In multiplayer the sweep resigns only eligible slots: in-game (+0x05),
 * status 0 (+0x07) and aux +0x00 == 0.  A status-marked slot and an
 * aux-flagged slot are both skipped; the resigned slot is marked status 6
 * and the active-player count is decremented once. */
int test_engine_combat_resign_sweep_mp(void)
{
    resign_reset();
    P_REC(2)[0x05] = 1;                  /* eligible */
    P_REC(3)[0x05] = 1; P_REC(3)[0x07] = 5;   /* skipped: status != 0 */
    P_REC(4)[0x05] = 1; AUX(4)[0x00] = 1;     /* skipped: aux +0x00 set */
    s_slot_00553f98 = 3;

    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_combat_mp_resign_sweep(&evq, 1);

    T_ASSERT_EQ_U(P_REC(2)[0x05], 0);    /* player 2 resigned (record wiped) */
    T_ASSERT_EQ_U(P_REC(2)[0x07], 6);    /* marked resigned */
    T_ASSERT_EQ_U(P_REC(3)[0x05], 1);    /* skipped, untouched */
    T_ASSERT_EQ_U(P_REC(4)[0x05], 1);    /* skipped, untouched */
    T_ASSERT_EQ_U(s_slot_00553f98, 2);   /* one player removed */
    T_ASSERT_EQ_I(evq.write_idx, 1);
    return 0;
}

/* ==========================================================================
 * FUN_0046d42c — capital-defender finder (engine_combat_find_capital_defender)
 * ========================================================================== */

#define R_REC(k)     (&g_per_army_768[(k) * 0x300])
#define R_CAP_COL    0x6c
#define R_CAP_ROW    0x6d

/* Reset the blocks the finder reads and stage region 6 with a capital at
 * (cap_col, cap_row), owner 2. */
static void cd_reset(int cap_col, int cap_row)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    uint8_t *reg = R_REC(6);
    reg[0x05]      = 2;                  /* region owner                       */
    reg[R_CAP_COL] = (uint8_t)cap_col;
    reg[R_CAP_ROW] = (uint8_t)cap_row;
}

/* Place occupant `occ` at grid (col,row) with owner/active/value. */
static void cd_place(int occ, int col, int row, int owner, int active, int value)
{
    g_overworld_map[(row * 0x40 + col) * 8 + 5] = (uint8_t)occ;
    uint8_t *r = CREC(occ);
    r[C_OWNER]  = (uint8_t)owner;
    r[C_ACTIVE] = (uint8_t)active;
    I32(r, C_VALUE) = value;
}

/* --- picks the strongest friendly live unit inside the box ----------------- */
int test_engine_combat_capital_defender_strongest(void)
{
    cd_reset(10, 10);                       /* box cols 8..11, rows 8..11      */
    cd_place(3,  8,  8, 2, 1, 50);          /* in box (left/up edge)           */
    cd_place(4, 11, 11, 2, 1, 100);         /* in box (right/down edge), bigger */
    cd_place(7,  9,  9, 2, 1, 70);          /* in box, middling                */

    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 4);
    return 0;
}

/* --- skips hostile-owner, inactive, and out-of-box units ------------------- */
int test_engine_combat_capital_defender_filters(void)
{
    cd_reset(10, 10);
    cd_place(3,  9,  9, 5, 1, 900);         /* hostile owner → ignored         */
    cd_place(4, 10, 10, 2, 0, 900);         /* not active (+0x08 != 1) → ignored */
    cd_place(5, 12, 10, 2, 1, 900);         /* col cap+2 → outside box (right)  */
    cd_place(6,  8, 10, 2, 1, 40);          /* col cap-2 → inside box (left)    */

    /* Only occupant 6 qualifies, despite the bigger ignored ones. */
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 6);
    return 0;
}

/* --- asymmetric box: cap+2 column/row excluded, cap-2 included ------------- */
int test_engine_combat_capital_defender_asymmetric(void)
{
    cd_reset(10, 10);
    cd_place(5, 10, 12, 2, 1, 900);         /* row cap+2 → outside box (down)   */
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);  /* nothing in box */

    cd_place(6, 10,  8, 2, 1, 30);          /* row cap-2 → inside box (up)      */
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 6);
    return 0;
}

/* --- capital within two cells of the grid edge → 0 (box must fit) ---------- */
int test_engine_combat_capital_defender_edge(void)
{
    cd_reset(1, 10);                        /* cap_col-2 < 0                    */
    cd_place(3, 1, 10, 2, 1, 100);
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);

    cd_reset(0x3f, 10);                     /* cap_col+2 >= 0x41                */
    cd_place(3, 0x3f, 10, 2, 1, 100);
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);

    cd_reset(10, 1);                        /* cap_row-2 < 0                    */
    cd_place(3, 10, 1, 2, 1, 100);
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);

    cd_reset(10, 0x3f);                     /* cap_row+2 >= 0x41                */
    cd_place(3, 10, 0x3f, 2, 1, 100);
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);
    return 0;
}

/* --- empty box → 0 --------------------------------------------------------- */
int test_engine_combat_capital_defender_empty(void)
{
    cd_reset(20, 20);
    T_ASSERT_EQ_I(engine_combat_find_capital_defender(6), 0);
    return 0;
}

/* ── FUN_004aca2b: per-turn mercenary-offer rotation ───────────────────────
 *
 * g_unit_table_20 slot fields (stride 0x14): +0x00 back-ref (free when 0),
 * +0x03 published offer, +0x04 realm cursor, +0x06 countdown, +0x07 period.
 * The reverse map lands in g_per_army_768[v*0x300 + 0x1ad]. */

#define U_REC(i)    (&g_unit_table_20[(i) * 0x14])
#define MR_REC(k)   (&g_per_army_768[(k) * 0x300])

static void mr_reset(int pool_slots, int realms)
{
    memset(g_unit_table_20, 0, sizeof g_unit_table_20);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    s_slot_00554030 = (uint32_t)pool_slots;   /* merc-pool slot count */
    s_slot_0056d5dc = (uint32_t)realms;       /* realm count          */
}

/* A free slot whose countdown hasn't expired only counts down + steps the
 * cursor; nothing is published, so no realm is offered anything. */
int test_engine_combat_merc_rotate_no_publish(void)
{
    mr_reset(1, 4);
    uint8_t *u = U_REC(1);
    u[0x04] = 2;  u[0x06] = 3;  u[0x07] = 5;   /* cursor 2, count 3, period 5 */

    engine_combat_rotate_merc_offers();

    T_ASSERT_EQ_I(u[0x03], 0);                 /* offer cleared, not republished */
    T_ASSERT_EQ_I(u[0x06], 2);                 /* countdown 3 → 2  */
    T_ASSERT_EQ_I(u[0x04], 3);                 /* cursor 2 → 3     */
    for (int v = 1; v <= 4; v++)
        T_ASSERT_EQ_I(MR_REC(v)[0x1ad], 0);
    return 0;
}

/* The turn the countdown expires: publish the cursor as the offer, reload the
 * countdown from the period, step the cursor once more, and stamp the offering
 * slot into the offered realm's +0x1ad. */
int test_engine_combat_merc_rotate_publishes(void)
{
    mr_reset(1, 4);
    uint8_t *u = U_REC(1);
    u[0x04] = 2;  u[0x06] = 1;  u[0x07] = 5;   /* expires this turn */

    engine_combat_rotate_merc_offers();

    /* count 1→0 (dec), cursor 2→3 (pre-clamp), expired → offer=3, count=5, cursor=4 */
    T_ASSERT_EQ_I(u[0x03], 3);                 /* offer = published cursor */
    T_ASSERT_EQ_I(u[0x06], 5);                 /* reloaded from period     */
    T_ASSERT_EQ_I(u[0x04], 4);                 /* stepped past the offer   */
    T_ASSERT_EQ_I(MR_REC(3)[0x1ad], 1);        /* realm 3 ← slot 1         */
    T_ASSERT_EQ_I(MR_REC(1)[0x1ad], 0);
    T_ASSERT_EQ_I(MR_REC(2)[0x1ad], 0);
    T_ASSERT_EQ_I(MR_REC(4)[0x1ad], 0);
    return 0;
}

/* The cursor wraps back to 1 once it steps past the last realm. */
int test_engine_combat_merc_rotate_cursor_wraps(void)
{
    mr_reset(1, 4);
    uint8_t *u = U_REC(1);
    u[0x04] = 4;  u[0x06] = 2;  u[0x07] = 5;   /* cursor at last realm */

    engine_combat_rotate_merc_offers();

    T_ASSERT_EQ_I(u[0x04], 1);                 /* 4 → 5 → wrap → 1 */
    T_ASSERT_EQ_I(u[0x06], 1);                 /* not yet expired  */
    T_ASSERT_EQ_I(u[0x03], 0);
    return 0;
}

/* An in-use slot (back-ref != 0) is skipped entirely — no countdown/cursor
 * change — and never matches as an offer holder even if its +0x03 lines up. */
int test_engine_combat_merc_rotate_skips_in_use(void)
{
    mr_reset(1, 4);
    uint8_t *u = U_REC(1);
    *(int16_t *)(u + 0x00) = 7;                /* slot occupied  */
    u[0x03] = 3;  u[0x04] = 2;  u[0x06] = 1;  u[0x07] = 5;

    engine_combat_rotate_merc_offers();

    T_ASSERT_EQ_I(u[0x04], 2);                 /* untouched */
    T_ASSERT_EQ_I(u[0x06], 1);
    T_ASSERT_EQ_I(u[0x03], 3);
    T_ASSERT_EQ_I(MR_REC(3)[0x1ad], 0);        /* in-use slot is not an offer */
    return 0;
}

/* With two free slots publishing the same realm this turn, the reverse map
 * records the lowest-indexed offering slot per realm, 0 for unoffered realms. */
int test_engine_combat_merc_rotate_reverse_map(void)
{
    mr_reset(3, 4);
    /* Slots 1 & 2 both expire onto realm 3 (cursor 2 → +1 → publish 3); slot 3
     * expires onto realm 4 (cursor 3 → +1 → publish 4). */
    U_REC(1)[0x04] = 2; U_REC(1)[0x06] = 1; U_REC(1)[0x07] = 9;
    U_REC(2)[0x04] = 2; U_REC(2)[0x06] = 1; U_REC(2)[0x07] = 9;
    U_REC(3)[0x04] = 3; U_REC(3)[0x06] = 1; U_REC(3)[0x07] = 9;

    engine_combat_rotate_merc_offers();

    T_ASSERT_EQ_I(U_REC(1)[0x03], 3);
    T_ASSERT_EQ_I(U_REC(2)[0x03], 3);
    T_ASSERT_EQ_I(U_REC(3)[0x03], 4);
    T_ASSERT_EQ_I(MR_REC(3)[0x1ad], 1);        /* realm 3 ← lowest slot (1) */
    T_ASSERT_EQ_I(MR_REC(4)[0x1ad], 3);        /* realm 4 ← slot 3          */
    T_ASSERT_EQ_I(MR_REC(1)[0x1ad], 0);
    T_ASSERT_EQ_I(MR_REC(2)[0x1ad], 0);
    return 0;
}

/* ---- raise region unit (FUN_004a5003) ----------------------------------- *
 * A ~50-head field unit mustered from a region: population 500 → pct = 5000/500
 * = 10 → sized = percent_of(500, 10) = 50 head, primed into the pool and spawned
 * at the muster-cost price.  Same spawn path as muster_defender mode 0. */
int test_engine_combat_raise_region_unit_spawns(void)
{
    spawn_reset();
    memset(g_spawn_cells, 0, sizeof g_spawn_cells);
    g_spawn_cells[(20 * SPAWN_GRID + 20) * SPAWN_STRIDE + 1] = 1;   /* free seat cell */
    engine_map_query_bind_cell_descriptors(g_spawn_cells);
    engine_territory_bind_cells(g_spawn_cells);
    engine_territory_bind_records(g_per_territory_420);

    int region = 2, owner = 2;
    R_REC(region)[SPAWN_COORD_OFF + region * 3]     = 20;   /* seat col */
    R_REC(region)[SPAWN_COORD_OFF + region * 3 + 1] = 20;   /* seat row */
    R_REC(region)[0x05] = owner;                            /* region owner */
    I32(R_REC(region), 0x24) = 500;                         /* population → pct 10 */
    I32(R_REC(region), 0x38) = 500;
    P_REC(owner)[0x05] = 1;                                 /* owner active */

    int ret = engine_combat_raise_region_unit(region);
    T_ASSERT(ret != 0);
    T_ASSERT_EQ_I(ret, engine_territory_current_slot());
    T_ASSERT_EQ_I(I32(CREC(ret), C_VALUE), 50);            /* sized 50 head */

    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(NULL);
    return 0;
}

/* ---- find deploy cell (FUN_004a689d) ------------------------------------- */

/* Place a deployable, passable overworld cell owned by `region`. */
static void deploy_cell_set(int col, int row, int region)
{
    int o = (row * 0x40 + col) * 8;
    g_overworld_map[o + 0] = 10;            /* terrain in [3,0x16] */
    g_overworld_map[o + 1] = 0x20;          /* deployable flag bit */
    g_overworld_map[o + 7] = (uint8_t)region;
}

/* Nearest qualifying cell to the army's seat is returned (col, row). */
int test_engine_combat_find_deploy_cell_nearest(void)
{
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    int army = 2, region = 3;
    CREC(army)[0x0a] = 10;                 /* army seat */
    CREC(army)[0x0b] = 10;

    deploy_cell_set(12, 10, region);        /* dist 2 — nearer */
    deploy_cell_set(20, 20, region);        /* dist 20 — farther */

    int col = -1, row = -1;
    engine_combat_find_deploy_cell(army, region, &col, &row);
    T_ASSERT_EQ_I(col, 12);
    T_ASSERT_EQ_I(row, 10);
    return 0;
}

/* A cell owned by a different region, or non-deployable, is ignored. */
int test_engine_combat_find_deploy_cell_filters(void)
{
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    int army = 2, region = 3;
    CREC(army)[0x0a] = 5;
    CREC(army)[0x0b] = 5;

    deploy_cell_set(6, 5, 4);               /* wrong owner — ignored */
    int o = (5 * 0x40 + 7) * 8;             /* right owner but no deploy flag */
    g_overworld_map[o + 0] = 10;
    g_overworld_map[o + 7] = (uint8_t)region;
    deploy_cell_set(30, 5, region);         /* the only qualifier (dist 25) */

    int col = -1, row = -1;
    engine_combat_find_deploy_cell(army, region, &col, &row);
    T_ASSERT_EQ_I(col, 30);
    T_ASSERT_EQ_I(row, 5);
    return 0;
}

/* No qualifying cell → fall back to the region's capital cell (+0x6c/+0x6d). */
int test_engine_combat_find_deploy_cell_fallback(void)
{
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    int army = 2, region = 3;
    CREC(army)[0x0a] = 5;
    CREC(army)[0x0b] = 5;
    R_REC(region)[0x6c] = 42;               /* capital col */
    R_REC(region)[0x6d] = 17;               /* capital row */

    int col = -1, row = -1;
    engine_combat_find_deploy_cell(army, region, &col, &row);
    T_ASSERT_EQ_I(col, 42);
    T_ASSERT_EQ_I(row, 17);
    return 0;
}

/* ---- pick best available unit (FUN_004a0917) ---------------------------- *
 * Occupant fields beyond the few the test header names: home region +0x10,
 * placed +0x150, region-transfer pending +0x198, survives-battle +0x199. */
static void pick_unit_reset(void)
{
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_per_army_768, 0, sizeof g_per_army_768);
}

/* A unit whose home region IS the target scores 300 and is returned. */
int test_engine_combat_pick_best_unit_same_region(void)
{
    pick_unit_reset();
    uint8_t *c = CREC(1);
    c[C_OWNER] = 2;                     /* owner match */
    c[0x150]   = 1;                     /* placed */
    I32(c, C_VALUE) = 50;              /* head ≥ min */
    c[0x10]    = 5;                     /* home region == target */

    T_ASSERT_EQ_I(engine_combat_pick_best_unit(2, 5, 10), 1);
    return 0;
}

/* A unit whose home region is adjacent to the target scores 100; a non-adjacent,
 * non-home unit scores 0 and is never selected. */
int test_engine_combat_pick_best_unit_adjacent(void)
{
    pick_unit_reset();
    uint8_t *c1 = CREC(1);
    c1[C_OWNER] = 2; c1[0x150] = 1; I32(c1, C_VALUE) = 50;
    c1[0x10] = 9;                      /* home region 9: not target, not adjacent */

    uint8_t *c2 = CREC(2);
    c2[C_OWNER] = 2; c2[0x150] = 1; I32(c2, C_VALUE) = 50;
    c2[0x10] = 8;                      /* home region 8 ... */
    g_per_army_768[8 * 0x300 + 0x5c] = 5;   /* ... adjacency list lists region 5 */

    T_ASSERT_EQ_I(engine_combat_pick_best_unit(2, 5, 10), 2);
    return 0;
}

/* Equal affinity → the stronger unit (more weighted strength) wins, even at a
 * higher index (strict > keeps the earlier on a true tie). */
int test_engine_combat_pick_best_unit_strength_breaks_tie(void)
{
    pick_unit_reset();
    uint8_t *c1 = CREC(1);
    c1[C_OWNER] = 2; c1[0x150] = 1; I32(c1, C_VALUE) = 50;
    c1[0x10] = 5;                      /* 300, strength 1 → +0 */

    uint8_t *c2 = CREC(2);
    c2[C_OWNER] = 2; c2[0x150] = 1; I32(c2, C_VALUE) = 50;
    c2[0x10] = 5;                      /* 300, plus strength bonus */
    I16(c2, C_COUNTS) = 10;            /* class-0 ×10 → strength 40 → +5 */

    T_ASSERT_EQ_I(engine_combat_pick_best_unit(2, 5, 10), 2);
    return 0;
}

/* Every disqualifier — wrong owner, pending transfer (+0x198), survives flag
 * (+0x199), unplaced (+0x150 == 0), under-strength — is filtered out → 0. */
int test_engine_combat_pick_best_unit_filters(void)
{
    pick_unit_reset();
    uint8_t *c1 = CREC(1);             /* wrong owner */
    c1[C_OWNER] = 3; c1[0x150] = 1; I32(c1, C_VALUE) = 50; c1[0x10] = 5;
    uint8_t *c2 = CREC(2);             /* transfer pending */
    c2[C_OWNER] = 2; c2[0x150] = 1; c2[0x198] = 1; I32(c2, C_VALUE) = 50; c2[0x10] = 5;
    uint8_t *c3 = CREC(3);             /* survives-battle flag set */
    c3[C_OWNER] = 2; c3[0x150] = 1; c3[0x199] = 1; I32(c3, C_VALUE) = 50; c3[0x10] = 5;
    uint8_t *c4 = CREC(4);             /* not placed */
    c4[C_OWNER] = 2; I32(c4, C_VALUE) = 50; c4[0x10] = 5;
    uint8_t *c5 = CREC(5);             /* head below min */
    c5[C_OWNER] = 2; c5[0x150] = 1; I32(c5, C_VALUE) = 5; c5[0x10] = 5;

    T_ASSERT_EQ_I(engine_combat_pick_best_unit(2, 5, 10), 0);
    return 0;
}

/* ---- find a realm to garrison a unit (FUN_004a07f2) ---------------------- */

/* A keep-bearing owned realm with room returns immediately. */
int test_engine_combat_find_garrison_realm_found(void)
{
    pick_unit_reset();
    s_slot_0056d5dc = 1;
    uint8_t *a = R_REC(1);
    a[0x05] = 2;                       /* owner */
    a[0x1c0] = 4;                      /* keep level 4 → garrison cap 400 */
    I32(CREC(3), 0x168) = 100;        /* unit 3 head 100 ≤ 400 */

    T_ASSERT_EQ_I(engine_combat_find_garrison_realm(2, 3), 1);
    return 0;
}

/* A pending garrison eats the keep's capacity; the search skips that realm and
 * picks the next one with room. */
int test_engine_combat_find_garrison_realm_pending_blocks(void)
{
    pick_unit_reset();
    s_slot_0056d5dc = 2;
    uint8_t *a1 = R_REC(1);
    a1[0x05] = 2; a1[0x1c0] = 1;       /* cap 150 */
    I32(a1, 0x1bc) = 7;                /* pending garrison occupant 7 ... */
    I32(CREC(7), 0x168) = 100;        /* ... head 100 → only 50 spare */
    uint8_t *a2 = R_REC(2);
    a2[0x05] = 2; a2[0x1c0] = 4;       /* cap 400 */
    I32(CREC(3), 0x168) = 100;        /* unit 100 > 50 (realm 1) but ≤ 400 (realm 2) */

    T_ASSERT_EQ_I(engine_combat_find_garrison_realm(2, 3), 2);
    return 0;
}

/* Wrong owner, no keep, siege-mode (+0x1c3 == 1) and siege-block (+0x1c2 != 0)
 * realms are all rejected → 0. */
int test_engine_combat_find_garrison_realm_filters(void)
{
    pick_unit_reset();
    s_slot_0056d5dc = 4;
    R_REC(1)[0x05] = 3; R_REC(1)[0x1c0] = 4;                  /* wrong owner */
    R_REC(2)[0x05] = 2; R_REC(2)[0x1c0] = 0;                  /* no keep */
    R_REC(3)[0x05] = 2; R_REC(3)[0x1c0] = 2; R_REC(3)[0x1c3] = 1;   /* siege mode */
    R_REC(4)[0x05] = 2; R_REC(4)[0x1c0] = 2; R_REC(4)[0x1c2] = 1;   /* siege block */
    I32(CREC(5), 0x168) = 10;          /* small unit that would otherwise fit */

    T_ASSERT_EQ_I(engine_combat_find_garrison_realm(2, 5), 0);
    return 0;
}

/* ---- AI brain step 0xa: launch an attack (FUN_004a0015) end-to-end ------- *
 * Integration: cooldown ready + pinned enemy + prosperous muster realm + a
 * spawn seat → the step raises a force, stamps it with a march order to the
 * selected target realm, and reloads the personality cooldown. */
int test_engine_combat_ai_launch_attack(void)
{
    spawn_reset();
    memset(g_spawn_cells, 0, sizeof g_spawn_cells);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
    g_spawn_cells[(20 * SPAWN_GRID + 20) * SPAWN_STRIDE + 1] = 1;   /* free spawn seat */
    engine_map_query_bind_cell_descriptors(g_spawn_cells);
    engine_territory_bind_cells(g_spawn_cells);
    engine_territory_bind_records(g_per_territory_420);

    int player = 2, muster = 2, enemy = 4;
    P_REC(player)[0x07]  = 1;          /* personality 1 → cooldown reload 6 */
    P_REC(player)[0x15a] = 0;          /* ready */
    P_REC(player)[0x0eb] = enemy;      /* pinned war target */
    P_REC(player)[0x0e5] = muster;     /* muster realm */
    P_REC(player)[0x05]  = 1;          /* owner active (spawn garrison source) */

    s_slot_0056d5dc = 4;
    R_REC(muster)[0x05] = player;
    R_REC(muster)[SPAWN_COORD_OFF + muster * 3]     = 20;   /* spawn seat */
    R_REC(muster)[SPAWN_COORD_OFF + muster * 3 + 1] = 20;
    I32(R_REC(muster), 0x24) = 500;    /* population ≥ 400 */
    I32(R_REC(muster), 0x38) = 500;
    R_REC(muster)[0x0c] = 30;          /* approval ≥ 25 */

    R_REC(3)[0x05] = enemy;            /* enemy realm select_target picks */
    R_REC(3)[0x6c] = 22; R_REC(3)[0x6d] = 20;

    int o = (20 * 0x40 + 22) * 8;      /* a deployable cell owned by realm 3 */
    g_overworld_map[o + 0] = 10; g_overworld_map[o + 1] = 0x20; g_overworld_map[o + 7] = 3;

    engine_army_ai_launch_attack(player);

    int occ = engine_territory_current_slot();
    T_ASSERT(occ != 0);
    T_ASSERT_EQ_I(CREC(occ)[0x1a], 7);        /* march order */
    T_ASSERT_EQ_I(CREC(occ)[0x151], 3);       /* target realm recorded */
    T_ASSERT_EQ_U(CREC(occ)[0x150], 0);       /* placed flag cleared */
    T_ASSERT_EQ_I(P_REC(player)[0x4b], 3);    /* selected target */
    T_ASSERT_EQ_I(P_REC(player)[0x15a], 6);   /* cooldown reloaded (personality 1) */

    engine_map_query_bind_cell_descriptors(NULL);
    engine_territory_bind_cells(NULL);
    engine_territory_bind_records(NULL);
    return 0;
}
