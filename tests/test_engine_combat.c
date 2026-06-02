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
#include "engine_realm_upkeep.h"   /* shares the merc-contract triple */
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
        T_ASSERT_EQ_I(g_combat_muster_a[c], ac[c] + (c == 2 ? 5 : 0));
        T_ASSERT_EQ_I(g_combat_muster_b[c], dc[c] + (c == 6 ? 9 : 0));
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
    T_ASSERT_EQ_I(g_combat_local_role, 0);
    return 0;
}

/* Local player is the (garrisoned) attacker, defender open field. */
int test_engine_combat_size_local_attacks(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 1; CREC(1)[C_GARRISON] = 1; CREC(1)[C_COUNT] = 4;
    CREC(2)[C_OWNER] = 2; CREC(2)[C_GARRISON] = 0; CREC(2)[C_COUNT] = 0;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 1);
    T_ASSERT_EQ_I(g_combat_local_role, 1);
    T_ASSERT_EQ_I(g_combat_ctrl_408, 2);
    T_ASSERT_EQ_I(g_combat_ctrl_5b0, 2);
    T_ASSERT_EQ_I(g_combat_both_garrison, 0);
    T_ASSERT_EQ_I(g_combat_size_a, 4);
    T_ASSERT_EQ_I(g_combat_size_b, 6);          /* 0 count floored to 6 */
    T_ASSERT_EQ_I(g_combat_owner_a, 1);
    T_ASSERT_EQ_I(g_combat_owner_b, 2);
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
    T_ASSERT_EQ_I(g_combat_local_role, 2);
    T_ASSERT_EQ_I(g_combat_ctrl_408, 0);
    T_ASSERT_EQ_I(g_combat_size_a, 9);
    T_ASSERT_EQ_I(g_combat_size_b, 3);
    return 0;
}

/* Defender is local but the attacker has no garrison → role still 1. */
int test_engine_combat_size_defender_local_open_attacker(void)
{
    cb_reset();
    CREC(1)[C_OWNER] = 2; CREC(1)[C_GARRISON] = 0;
    CREC(2)[C_OWNER] = 1; CREC(2)[C_GARRISON] = 1;
    T_ASSERT_EQ_I(engine_combat_size_battle(1, 2, 1), 1);
    T_ASSERT_EQ_I(g_combat_local_role, 1);
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

/* --- FUN_004a80db: post-battle condition recompute ------------------------ */

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
