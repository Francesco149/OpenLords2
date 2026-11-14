/* rl_mpmsg2.c — a retail-layout port, partition 'mpmsg2' (tools/bf/parts.py): the MP message layer's second half
 * (docs/findings/rl-mpmsg2.md). Retail semantics on retail's layout: rl.h (record accessors, globals).
 *
 * - Payload builders (the send table DAT_004d57f0[id], called by 0x43eda0 with the payload pointer DAT_00512f8c set
 *   to the ring slot + 4) append fields with 0x4401ba; receivers (the receive table DAT_004d5980[id], called by the
 *   dispatcher 0x43eac1 with DAT_00515918 = DAT_00515c50 + 4) take them in the same order with 0x4401f6.
 * - Most messages schedule a delayed action: 0x447136(type, due tick, args …) files a record in the 400 × 56 B table
 *   DAT_0050d7c0 (+0 due, +4..+0x28 ten args, +0x36 live, +0x37 type); 0x4472ce (partition ctl) runs it once due,
 *   loading the args into DAT_005158e8 / DAT_005158a8 / DAT_005158a4 / DAT_005158b0 / DAT_005158ac … and calling
 *   the handler DAT_004d5c98[type]. The due tick: 0x446e21(lead) = the highest per-player tick DAT_0053e8d0 + lead.
 * Builders, receivers and handlers are reached only through those tables (entry-patched rows); nobody reads their
 * EAX (0x43eda0 drops it; 0x43eac1 returns it to 0x4b8c3a, which drops it; 0x4472ce drops it). */
#include "rl.h"
#include "rl_mpmsg2_globals.h"
/* other partitions' generated globals (their headers clash when included together): the declarations, verbatim */
extern int32_t g_rl_5158a4, g_rl_5158a8, g_rl_5158ac, g_rl_5158b0, g_rl_5158e8;   /* rl_ctl: the action parameters */
extern int32_t g_rl_553534;                                                      /* rl_glue */
extern int32_t g_rl_544094;                                                      /* rl_render: the builders' lead */
extern uint8_t g_rl_53f690[0x320];                                               /* rl_render: DAT_0053f6a0 troop rows */
extern int32_t g_rl_51587c, g_rl_512f6c, g_rl_512fa0, g_rl_56d6a4;               /* rl_render */
extern uint8_t g_rl_56d5a8[0x8];                                                 /* rl_host: DAT_0056d5a8 / ac */
extern int32_t g_rl_553f44, g_rl_51591c, g_rl_515b24;                            /* rl_host */
extern int32_t g_rl_56795c, g_rl_53f018;                                         /* rl_ui2 */
extern int32_t g_rl_5533e0[2];                                                   /* rl_ui2 / rl_skirmish */
extern uint8_t g_rl_50d7c0[0x5780];                                              /* rl_ui3: the action table */
extern uint8_t g_rl_5530e0[8];                                                   /* rl_io */
#include "rl_io_plat.h"          /* timeGetTime */
#include "engine.h"              /* g_sim_mp_active */
#include "engine_bf_input_globals.h" /* DAT_00515920, DAT_0059154c / 50 */
#include "engine_combat.h"       /* g_combat_render_dirty (DAT_0052afcc), g_combat_flag_530d0, 0x4a9a9a */
#include "engine_army_ai.h"      /* 0x4284ce, 0x436d02, the ranking cookie DAT_0056d6a0 (a mirror) */
#include "engine_realm.h"        /* 0x44b80b */
#include "engine_sendsupplies.h" /* 0x43b145, g_castle_tier (DAT_0056d898), g_send_target (DAT_00553c78) */
#include "engine_title_render.h" /* 0x4ae5e2, the title page / tabs / shields (g_tr_*) */
#include "engine_trade.h"        /* g_trade_net / _max / _offer (DAT_00554170 / 0055cd70 / 0057c8c0) */
#include "engine_armory.h"       /* g_arm_merc (DAT_0055446c), g_arm_happy_cost (DAT_00565400) */
#include "engine_mapview_globals.h" /* g_mv_ti_head (DAT_005681e0), g_mv_active_county (DAT_0057c8c4) */
#include "engine_diplomacy_action.h" /* g_diplo_action_mode (DAT_005651cc) */

#include "gfx_globals.h"         /* g_gfx_screen_byte (DAT_004eac50) */

/* partition mpmsg's generated globals (rl_mpmsg_globals.h; rhost: aliased in rh_hooks_rl_mpmsg2.c until mpmsg
 * emits its own) */
extern uint8_t g_rl_512f9c[4];   /* DAT_00512f9c: a 4-byte packing scratch */
extern int32_t g_rl_515960[12];  /* DAT_00515960: the per-player lead average */
extern int32_t g_rl_542cc0;      /* DAT_00542cc0: a receiver's due tick */
extern int32_t g_rl_553e70;      /* DAT_00553e70: a builder's due tick */

#define P(x)          ((int)(uintptr_t)(x))
#define PUT(x, n)     rl_4401ba(P(x), (n))     /* append n bytes at DAT_00512f8c */
#define GET(x, n)     rl_4401f6(P(x), (n))     /* take n bytes from DAT_00515918 */
#define CPY(s, d, n)  rl_401136(P(s), P(d), (n))
#define LOCAL         ((int)s_slot_0057c8cc)   /* DAT_0057c8cc: the local player */
#define NOW           (g_bf_57c998[0])         /* DAT_0057c998: the tick */
#define CO8(c, va)    AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))            /* county record */
#define PL8(p, va)    AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))    /* realm record */
#define PL32(p, va)   AT(int32_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define PX8(p, va)    AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))             /* realm aux */
#define DA32(j, va)   AT(int32_t, g_rl_50d7c0, 0x50d7c0u, (int32_t)(va) + 56 * (j))                 /* action table */
#define DA8(j, va)    AT(uint8_t, g_rl_50d7c0, 0x50d7c0u, (int32_t)(va) + 56 * (j))
#define D59154C       AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)
#define D591550       AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u)
#define D56D5A8       AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5a8u)
#define D56D5AC       AT(int32_t, g_rl_56d5a8, 0x56d5a8u, 0x56d5acu)
#define TROOPS(p)     (&AT(uint8_t, g_rl_53f690, 0x53f690u, 0x53f6a0 + 128 * (p)))   /* DAT_0053f6a0[32 p] */

/* ================================================================ the delayed-action machinery */

/* Port of FUN_00446e21 (229 B) — the due tick of a new action: lead + the highest per-player tick DAT_0053e8d0[i]
 * over the players alive (+5), human (+4) and not out (aux +0x26); in the battle (DAT_0057c8fc == 2) only the two
 * sides DAT_005681ec / DAT_005681d8 count. */
int rl_446e21(int lead)
{
    int best = 0;
    for (int i = 0; i < 6; i++) {
        if (PL8(i, 0x57bf05) != 0 && PL8(i, 0x57bf04) != 0 && PX8(i, 0x553d76) == 0 &&
            (s_slot_0057c8fc != 2 || i == (int)s_slot_005681ec || i == (int)s_slot_005681d8) &&
            g_bf_53e8d0[i] > best)
            best = g_bf_53e8d0[i];
    }
    return lead + best;
}

/* Port of FUN_00446f84 (162 B) — reset the lead statistics: the 5-sample ring DAT_00515890 and its cursor / count,
 * the local player's average DAT_00515960 = -1, the late-action count / sum / average (DAT_0051587c, DAT_00512f54,
 * DAT_00512f50), the builders' lead DAT_00544094 = 50. EAX: the local player. */
int rl_446f84(void)
{
    for (int i = 0; i < 5; i++)
        g_rl_515890[i] = 0;
    g_rl_515884 = 0;
    g_rl_512fa4 = 0;
    g_rl_515960[LOCAL] = -1;
    g_rl_51587c = 0;
    g_rl_512f54 = 0;
    g_rl_512f50 = 0;
    g_rl_544094 = 50;
    return LOCAL;
}

/* Port of FUN_00447026 (158 B) — record an action's lead (due − DAT_0057c998) in the 5-sample ring; from the fifth
 * sample on the local player's average DAT_00515960 = sum / 5 (idiv). EAX: the average, else the lead. */
int rl_447026(int due)
{
    int lead = due - NOW;
    g_rl_515890[g_rl_515884++] = lead;
    if (g_rl_515884 >= 5)
        g_rl_515884 = 0;
    if (++g_rl_512fa4 >= 5) {
        int sum = 0;
        for (int k = 0; k < 5; k++)
            sum += g_rl_515890[k];
        g_rl_515960[LOCAL] = sum / 5;
        return sum / 5;
    }
    return lead;
}

/* Port of FUN_004476e4 (92 B) — the first free action record (+0x36 == 0), or -1. */
int rl_4476e4(void)
{
    for (int i = 0; i < 400; i++)
        if (DA8(i, 0x50d7f6) == 0)
            return i;
    return -1;
}

/* Port of FUN_00447740 (92 B) — how many action records are live. */
int rl_447740(void)
{
    int n = 0;
    for (int i = 0; i < 400; i++)
        if (DA8(i, 0x50d7f6) != 0)
            n++;
    return n;
}

/* Port of FUN_00447136 (408 B) — file a delayed action: the free record DAT_0051591c (0x4476e4; none: EAX -1,
 * nothing else); DAT_00512f6c counts up; a due tick in the future feeds the lead ring (0x447026), one not after
 * DAT_0057c998 is late (0x4470c4: logs "ERR: putting todo", and unless type 27 the late statistics + message 90
 * outside the battle); the record: live, type (AL), due, the ten args; DAT_00515b24 = the record, DAT_00512fa0 =
 * the live count (0x447740), which is EAX. */
int rl_447136(int type, int due, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11)
{
    int r = rl_4476e4();
    g_rl_51591c = r;
    if (r == -1)
        return r;
    ++g_rl_512f6c;
    if (due > NOW)
        rl_447026(due);
    else
        engine_retail_call(0x4470c4u, 2, (uint32_t)type, (uint32_t)due);   /* TODO(rl-link): 0x4470c4 (no partition) */
    DA8(g_rl_51591c, 0x50d7f6) = 1;
    DA8(g_rl_51591c, 0x50d7f7) = (uint8_t)type;
    DA32(g_rl_51591c, 0x50d7c0) = due;
    DA32(g_rl_51591c, 0x50d7c4) = a2;
    DA32(g_rl_51591c, 0x50d7c8) = a3;
    DA32(g_rl_51591c, 0x50d7cc) = a4;
    DA32(g_rl_51591c, 0x50d7d0) = a5;
    DA32(g_rl_51591c, 0x50d7d4) = a6;
    DA32(g_rl_51591c, 0x50d7d8) = a7;
    DA32(g_rl_51591c, 0x50d7dc) = a8;
    DA32(g_rl_51591c, 0x50d7e0) = a9;
    DA32(g_rl_51591c, 0x50d7e4) = a10;
    DA32(g_rl_51591c, 0x50d7e8) = a11;
    g_rl_515b24 = g_rl_51591c;
    int n = rl_447740();
    g_rl_512fa0 = n;
    return n;
}

/* ================================================================ payload builders / receivers (by message id) */

/* Port of FUN_004431ef (151 B) — builder 30: the due tick (0x446e21(DAT_00544094 + 10) → DAT_00553e70), packed
 * with the local player into 4 bytes (3 + 1, DAT_00512f9c), then DAT_0057c934 (1 byte); files action 1 for us. */
int rl_4431ef(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094 + 10);
    CPY(&g_rl_553e70, g_rl_512f9c, 3);
    CPY(&s_slot_0057c8cc, &g_rl_512f9c[3], 1);
    PUT(g_rl_512f9c, 4);
    PUT(&s_slot_0057c934, 1);
    return rl_447136(1, g_rl_553e70, LOCAL, (int)s_slot_0057c934, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443286 (156 B) — receiver 30: the due tick (3 bytes → DAT_00542cc0), the sender (1 → DAT_0056d5cc),
 * its DAT_0057c934 (1 byte); action 1 for the sender. */
int rl_443286(void)
{
    int v = 0;
    s_slot_0056d5cc = 0;
    g_rl_542cc0 = 0;
    GET(g_rl_512f9c, 4);
    GET(&v, 1);
    CPY(g_rl_512f9c, &g_rl_542cc0, 3);
    CPY(&g_rl_512f9c[3], &s_slot_0056d5cc, 1);
    return rl_447136(1, g_rl_542cc0, (int)s_slot_0056d5cc, v, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004433d9 (66 B) — builder 34: DAT_005681e0 (1 byte), DAT_0056795c (4), DAT_0059154c (1); redraw
 * DAT_0052afcc = 1. */
int rl_4433d9(void)
{
    PUT(&g_mv_ti_head, 1);
    PUT(&g_rl_56795c, 4);
    int r = PUT(&D59154C, 1);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_0044341b (113 B) — receiver 34: (1, 4, 1 bytes) → 0x438bec(DAT_00553234, a, b); DAT_0052afcc = 1. */
int rl_44341b(void)
{
    int b = 0, c = 0;
    g_rl_553234 = 0;
    GET(&g_rl_553234, 1);
    GET(&b, 4);
    GET(&c, 1);
    int r = rl_438bec(g_rl_553234, b, c);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_0044348c (95 B) — builder 35: the selected county DAT_0057c93c and its byte +0xb9 (2 bytes). */
int rl_44348c(void)
{
    CPY(&s_slot_0057c93c, g_rl_512f9c, 1);
    CPY(&CO8((int)s_slot_0057c93c, 0x53fa69), &g_rl_512f9c[1], 1);
    int r = PUT(g_rl_512f9c, 2);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_004434eb (130 B) — receiver 35: county DAT_00553234's byte +0xb9 = the value; its income
 * recomputed (0x44b80b). */
int rl_4434eb(void)
{
    int v = 0;
    g_rl_553234 = 0;
    GET(g_rl_512f9c, 2);
    CPY(g_rl_512f9c, &g_rl_553234, 1);
    CPY(&g_rl_512f9c[1], &v, 1);
    CO8(g_rl_553234, 0x53fa69) = (uint8_t)v;
    engine_realm_recompute_realm_income(g_rl_553234);   /* 0x44b80b */
    g_combat_render_dirty = 1;
    return 0;
}

/* Port of FUN_00443748 (98 B) — builder 39: DAT_0057c93c, DAT_00553534 (1 byte each) → 0x43933b(both). */
int rl_443748(void)
{
    CPY(&s_slot_0057c93c, g_rl_512f9c, 1);
    CPY(&g_rl_553534, &g_rl_512f9c[1], 1);
    PUT(g_rl_512f9c, 2);
    int r = rl_43933b((int)s_slot_0057c93c, g_rl_553534);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_004437aa (115 B) — receiver 39: 0x43933b(county, value). */
int rl_4437aa(void)
{
    int v = 0;
    g_rl_553234 = 0;
    GET(g_rl_512f9c, 2);
    CPY(g_rl_512f9c, &g_rl_553234, 1);
    CPY(&g_rl_512f9c[1], &v, 1);
    int r = rl_43933b(g_rl_553234, v);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_0044381d (166 B) — builder 40: a trade (DAT_0053e9bc 1, DAT_00554170 4, DAT_0056d8ac 1, DAT_0055cd70 4,
 * DAT_0057c8c0 4, the local player 1, DAT_0057c93c 1) and runs it here (0x4284ce). */
int rl_44381d(void)
{
    PUT(&s_slot_0053e9bc, 1);
    PUT(&g_trade_net, 4);
    PUT(&s_slot_0056d8ac, 1);
    PUT(&g_trade_max, 4);
    PUT(&g_trade_offer, 4);
    PUT(&s_slot_0057c8cc, 1);
    PUT(&s_slot_0057c93c, 1);
    engine_army_ai_execute_trade((int)s_slot_0053e9bc, g_trade_net, (int)s_slot_0056d8ac, g_trade_max, g_trade_offer,
                                 LOCAL, (int)s_slot_0057c93c);   /* 0x4284ce */
    return 0;
}

/* Port of FUN_004438c3 (205 B) — receiver 40: the trade, run for the sender (DAT_0056d5cc, DAT_00553234). */
int rl_4438c3(void)
{
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    g_rl_553234 = 0;
    s_slot_0056d5cc = 0;
    GET(&a, 1);
    GET(&b, 4);
    GET(&c, 1);
    GET(&d, 4);
    GET(&e, 4);
    GET(&s_slot_0056d5cc, 1);
    GET(&g_rl_553234, 1);
    engine_army_ai_execute_trade(a, b, c, d, e, (int)s_slot_0056d5cc, g_rl_553234);   /* 0x4284ce */
    return 0;
}

/* Port of FUN_00443990 (157 B) — builder 41: the local player (1), the due tick (4), DAT_0057c8c4 (1), DAT_0056d5ac
 * (2), DAT_0056d5a8 (2); action 12 (due, c4, ac, a8, local). */
int rl_443990(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&s_slot_0057c8cc, 1);
    PUT(&g_rl_553e70, 4);
    PUT(&g_mv_active_county, 1);
    PUT(&D56D5AC, 2);
    PUT(&D56D5A8, 2);
    return rl_447136(12, g_rl_553e70, g_mv_active_county, D56D5AC, D56D5A8, LOCAL, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443a2d (168 B) — receiver 41: action 12 for the sender (DAT_0056d5cc; DAT_00569520 the county). */
int rl_443a2d(void)
{
    int b = 0, c = 0;
    g_rl_569520[0] = 0;
    s_slot_0056d5cc = 0;
    GET(&s_slot_0056d5cc, 1);
    GET(&g_rl_542cc0, 4);
    GET(&g_rl_569520[0], 1);
    GET(&c, 2);
    GET(&b, 2);
    return rl_447136(12, g_rl_542cc0, g_rl_569520[0], c, b, (int)s_slot_0056d5cc, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443ad5 (184 B) — builder 42: the due tick, the local player, DAT_0057c93c, DAT_0055446c,
 * DAT_00565400 (4 each), the local player's troop row DAT_0053f6a0 (128 B); action 13. */
int rl_443ad5(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&g_rl_553e70, 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_0057c93c, 4);
    PUT(&g_arm_merc, 4);
    PUT(&g_arm_happy_cost, 4);
    PUT(TROOPS(LOCAL), 128);
    return rl_447136(13, g_rl_553e70, LOCAL, (int)s_slot_0057c93c, g_arm_merc, g_arm_happy_cost, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443b8d (162 B) — receiver 42: action 13 for the sender (its troop row overwritten). */
int rl_443b8d(void)
{
    int d = 0, e = 0;   /* retail: uninitialized, both written whole */
    GET(&g_rl_542cc0, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&g_rl_553234, 4);
    GET(&d, 4);
    GET(&e, 4);
    GET(TROOPS((int)s_slot_0056d5cc), 128);
    return rl_447136(13, g_rl_542cc0, (int)s_slot_0056d5cc, g_rl_553234, d, e, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443c2f (119 B) — builder 43: the due tick, DAT_0057c93c, DAT_0056d898 (4 each); action 16. */
int rl_443c2f(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&g_rl_553e70, 4);
    PUT(&s_slot_0057c93c, 4);
    PUT(&g_castle_tier, 4);
    return rl_447136(16, g_rl_553e70, (int)s_slot_0057c93c, g_castle_tier, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443ca6 (100 B) — receiver 43: action 16. */
int rl_443ca6(void)
{
    int c = 0;
    GET(&g_rl_542cc0, 4);
    GET(&g_rl_553234, 4);
    GET(&c, 4);
    return rl_447136(16, g_rl_542cc0, g_rl_553234, c, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443d0a (176 B) — builder 44: builder 41's fields at 4 bytes each + DAT_00553f44; action 12 with a
 * sixth arg. */
int rl_443d0a(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&g_rl_553e70, 4);
    PUT(&g_mv_active_county, 4);
    PUT(&D56D5AC, 4);
    PUT(&D56D5A8, 4);
    PUT(&g_rl_553f44, 4);
    return rl_447136(12, g_rl_553e70, g_mv_active_county, D56D5AC, D56D5A8, LOCAL, g_rl_553f44, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443dba (151 B) — receiver 44. */
int rl_443dba(void)
{
    int b = 0, c = 0, f = 0;   /* retail: uninitialized, written whole */
    GET(&s_slot_0056d5cc, 4);
    GET(&g_rl_542cc0, 4);
    GET(&g_rl_569520[0], 4);
    GET(&c, 4);
    GET(&b, 4);
    GET(&f, 4);
    return rl_447136(12, g_rl_542cc0, g_rl_569520[0], c, b, (int)s_slot_0056d5cc, f, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444060 (160 B) — builder 47: due = 0x446e21(25), the local player, DAT_0057c93c, DAT_00553c78
 * (4 each), the local caravan row DAT_005678c0[6 p] (24 B); action 20. */
int rl_444060(void)
{
    g_rl_553e70 = rl_446e21(25);
    PUT(&g_rl_553e70, 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_0057c93c, 4);
    PUT(&g_send_target, 4);
    PUT(&AT(int32_t, g_caravan_order, 0x5678c0u, 0x5678c0 + 24 * LOCAL), 24);
    return rl_447136(20, g_rl_553e70, LOCAL, (int)s_slot_0057c93c, g_send_target, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444100 (145 B) — receiver 47 (DAT_0052afd8 the target). */
int rl_444100(void)
{
    GET(&g_rl_542cc0, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&g_rl_553234, 4);
    GET(&g_rl_52afd8, 4);
    GET(&AT(int32_t, g_caravan_order, 0x5678c0u, 0x5678c0 + 24 * (int)s_slot_0056d5cc), 24);
    return rl_447136(20, g_rl_542cc0, (int)s_slot_0056d5cc, g_rl_553234, g_rl_52afd8, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444191 (11 B) — builder 48: no payload. */
int rl_444191(void)
{
    return 0;
}

/* Port of FUN_0044419c (59 B) — receiver 48: outside the battle, with DAT_005533d0 == 1, answer with message 51. */
int rl_44419c(void)
{
    if (s_slot_0057c8fc != 2 && s_slot_005533d0 == 1)
        rl_43eda0(0x33, 0);
    return 0;
}

/* Port of FUN_004441d7 (169 B) — builder 49: DAT_0057c93c, DAT_0059154c, DAT_00591550, the county's +0x15e (1 byte
 * each) → the county's rations (0x43a5a9). */
int rl_4441d7(void)
{
    CPY(&s_slot_0057c93c, g_rl_512f9c, 1);
    CPY(&D59154C, &g_rl_512f9c[1], 1);
    CPY(&D591550, &g_rl_512f9c[2], 1);
    CPY(&CO8((int)s_slot_0057c93c, 0x53fb0e), &g_rl_512f9c[3], 1);
    PUT(g_rl_512f9c, 4);
    int r = rl_43a5a9((int)s_slot_0057c93c, D59154C, D591550);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_00444280 (198 B) — receiver 49: the county's +0x15e, then its rations. */
int rl_444280(void)
{
    int b = 0, c = 0, d = 0;
    g_rl_553234 = 0;
    GET(g_rl_512f9c, 4);
    CPY(g_rl_512f9c, &g_rl_553234, 1);
    CPY(&g_rl_512f9c[1], &b, 1);
    CPY(&g_rl_512f9c[2], &c, 1);
    CPY(&g_rl_512f9c[3], &d, 1);
    CO8(g_rl_553234, 0x53fb0e) = (uint8_t)d;
    int r = rl_43a5a9(g_rl_553234, b, c);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_004443fb (26 B) — builder 51: the 42-byte block DAT_00515920. */
int rl_4443fb(void)
{
    return PUT(g_bf_515920, 42);
}

/* Port of FUN_00444415 (100 B) — receiver 51: in the battle, unless on screen 43, the block → 0x47f937; then
 * DAT_0059154c == 1 → screen 43, DAT_00568470 = 5000. */
int rl_444415(void)
{
    if (s_slot_0057c8fc != 2)
        return 0;
    if (g_gfx_screen_byte == 43)
        return 43;
    GET(g_bf_515920, 42);
    int r = rl_47f937();
    if (D59154C == 1) {
        g_gfx_screen_byte = 43;
        s_slot_00568470 = 5000;
    }
    return r;
}

/* Port of FUN_004449a0 (81 B) — builder 59: the due tick; action 49. */
int rl_4449a0(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&g_rl_553e70, 4);
    return rl_447136(49, g_rl_553e70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004449f1 (62 B) — receiver 59: action 49. */
int rl_4449f1(void)
{
    GET(&g_rl_542cc0, 4);
    return rl_447136(49, g_rl_542cc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444a2f (108 B) — builder 60: the battle (DAT_005653f8, the 48-byte DAT_0057cae0, DAT_0056d6b0,
 * DAT_0053f654); DAT_0053f018 = 0; enter the field (0x4778a0); log "OK :Bat taken sent" (0x4afab9). */
int rl_444a2f(void)
{
    PUT(&s_slot_005653f8, 4);
    PUT(s_slot_0057cae0, 48);
    PUT(&s_slot_0056d6b0, 4);
    PUT(&s_slot_0053f654, 4);
    g_rl_53f018 = 0;
    bf_4778a0();
    return rl_4afab9(P(g_rl_4d6014), 0, (int)s_slot_005653f8 + 1);
}

/* Port of FUN_00444a9b (108 B) — receiver 60: the battle; the field; "OK :Bat taken get". */
int rl_444a9b(void)
{
    GET(&s_slot_005653f8, 4);
    GET(s_slot_0057cae0, 48);
    GET(&s_slot_0056d6b0, 4);
    GET(&s_slot_0053f654, 4);
    g_rl_53f018 = 0;
    bf_4778a0();
    return rl_4afab9(P(g_rl_4d6014 + 0x14), 0, (int)s_slot_005653f8 + 1);
}

/* Port of FUN_00444d04 (100 B) — builder 64: the local player, the due tick (4 each); action 30. */
int rl_444d04(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&g_rl_553e70, 4);
    return rl_447136(30, g_rl_553e70, LOCAL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444d68 (81 B) — receiver 64: action 30 for the sender. */
int rl_444d68(void)
{
    GET(&s_slot_0056d5cc, 4);
    GET(&g_rl_542cc0, 4);
    return rl_447136(30, g_rl_542cc0, (int)s_slot_0056d5cc, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004458e5 (123 B) — builder 73: the local player, DAT_00553fbc, DAT_005651cc, DAT_0057a0f8,
 * DAT_005530b0 (4 each), the local player's 202-byte record DAT_00567d10[202 p]. */
int rl_4458e5(void)
{
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_00553fbc, 4);
    PUT(&g_diplo_action_mode, 4);
    PUT(&s_slot_0057a0f8, 4);
    PUT(&s_slot_005530b0, 4);
    return PUT(&s_slot_00567d10[202 * LOCAL], 202);
}

/* Port of FUN_00445960 (825 B) — receiver 73: sender DAT_0056d5cc, target, kind, DAT_00543fcc, a value, the sender's
 * 202-byte record; posts the event (0x472bc5) for the target: kind 0 → 245 (arg DAT_00543fcc), 1..4 → 246..249,
 * 5..10 → 250..255 (the value as the sixth arg), 11 / 12 → 256 / 257, else none. The target being us:
 * DAT_0059154c = us, and kind 0 → message 74, kind 4 → message 75. */
int rl_445960(void)
{
    int target, kind, value;   /* retail: uninitialized, written whole */
    GET(&s_slot_0056d5cc, 4);
    GET(&target, 4);
    GET(&kind, 4);
    GET(&g_rl_543fcc, 4);
    GET(&value, 4);
    GET(&s_slot_00567d10[202 * (int)s_slot_0056d5cc], 202);
    int from = (int)s_slot_0056d5cc;
    if (kind == 0)
        rl_472bc5(from, target, 245, g_rl_543fcc, 12, 0, 0, 0);
    else if (kind >= 1 && kind <= 4)
        rl_472bc5(from, target, 245 + kind, 0, 12, 0, 0, 0);
    else if (kind >= 5 && kind <= 10)
        rl_472bc5(from, target, 245 + kind, 0, 12, value, 0, 0);
    else if (kind == 11 || kind == 12)
        rl_472bc5(from, target, 245 + kind, 0, 12, 0, 0, 0);
    if (LOCAL == target) {
        D59154C = target;
        if (kind == 0)
            rl_43eda0(0x4a, 0);
        if (kind == 4)
            rl_43eda0(0x4b, 0);
    }
    return 0;
}

/* Port of FUN_00445c99 (138 B) — builder 74: the due tick, DAT_0059154c, DAT_0056d5cc, DAT_00543fcc (4 each);
 * action 43. */
int rl_445c99(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    PUT(&g_rl_553e70, 4);
    PUT(&D59154C, 4);
    PUT(&s_slot_0056d5cc, 4);
    PUT(&g_rl_543fcc, 4);
    return rl_447136(43, g_rl_553e70, D59154C, (int)s_slot_0056d5cc, g_rl_543fcc, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00445d23 (116 B) — receiver 74: action 43. */
int rl_445d23(void)
{
    int a, b;   /* retail: uninitialized, written whole */
    GET(&g_rl_542cc0, 4);
    GET(&a, 4);
    GET(&b, 4);
    GET(&g_rl_543fcc, 4);
    return rl_447136(43, g_rl_542cc0, a, b, g_rl_543fcc, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00445e6f (56 B) — builder 76: DAT_0053f034 (the map), DAT_0053e90c, DAT_0057cacc (4 each). */
int rl_445e6f(void)
{
    PUT(&s_slot_0053f034, 4);
    PUT(&g_tr_country_scroll, 4);
    return PUT(&g_tr_country_sel, 4);
}

/* Port of FUN_00445ea7 (156 B) — receiver 76: the host's map choice: the minimap / strategic map / build cells of
 * map DAT_0053f034 (0x46a037, 0x467770, 0x4298c1(0)), the player count clamped (0x4ae5e2), 0x433cc1; on title page 8
 * the country list and tabs redrawn; DAT_0057d340 = 2. */
int rl_445ea7(void)
{
    GET(&s_slot_0053f034, 4);
    GET(&g_tr_country_scroll, 4);
    GET(&g_tr_country_sel, 4);
    rl_46a037((int)s_slot_0053f034);
    rl_467770((int)s_slot_0053f034);
    rl_4298c1(0);
    engine_title_render_setup_clamp_player_count((int)s_slot_0053e8a4);   /* 0x4ae5e2 */
    int r = rl_433cc1();
    if (g_tr_page == 8) {
        g_tr_country_dirty = 1;
        g_tr_tabs_dirty = 1;
    }
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_00445f43 (61 B) — builder 77: the 6 shield slots DAT_0057cb40, realm DAT_0056d5cc's byte +0x0a. */
int rl_445f43(void)
{
    PUT(g_tr_p4_shield_slots, 6);
    return PUT(&PL8((int)s_slot_0056d5cc, 0x57bf0a), 1);
}

/* Port of FUN_00445f80 (190 B) — receiver 77: the shield slots, the LOCAL realm's +0x0a (copied to its aux +0x25);
 * 0x433cc1, the NPC schedule (0x49caaa); the local realm human (+5 = 2), DAT_005530e0[p] = 1, the shield flash on,
 * its timer 0, title page 6, DAT_0057d340 = 2. EAX: the local player. */
int rl_445f80(void)
{
    GET(g_tr_p4_shield_slots, 6);
    GET(&PL8(LOCAL, 0x57bf0a), 1);
    PX8(LOCAL, 0x553d75) = PL8(LOCAL, 0x57bf0a);
    rl_433cc1();
    rl_49caaa();
    PL8(LOCAL, 0x57bf05) = 2;
    g_rl_5530e0[LOCAL] = 1;
    g_tr_p4_shield_flash = 1;
    g_tr_p4_flash_timer = 0;
    g_tr_page = 6;
    g_bf_57d340 = 2;
    return LOCAL;
}

/* Port of FUN_004462c5 (26 B) — builder 84: DAT_0057a0f0 (4). */
int rl_4462c5(void)
{
    return PUT(&s_slot_0057a0f0, 4);
}

/* Port of FUN_004462df (159 B) — receiver 84: DAT_0057a0f0; on the client (DAT_00553248 == 0) on title page 6:
 * 0x433551, DAT_0057d340 = 2, and a non-zero value marks DAT_005533e0[local] and answers message 85. (Retail
 * compares DAT_005c9a74 with 2 and jumps over the arm that would set title page 10: dead code.) */
int rl_4462df(void)
{
    GET(&s_slot_0057a0f0, 4);
    if (g_tr_p4_gate_b == 0 && g_tr_page == 6) {
        rl_433551();
        g_bf_57d340 = 2;
        if (s_slot_0057a0f0 != 0) {
            AT(int32_t, g_rl_5533e0, 0x5533e0u, 0x5533e0 + 4 * LOCAL) = 1;   /* unbounded: p ≥ 2 runs past the array */
            rl_43eda0(0x55, 0);
        }
    }
    return 0;
}

/* ================================================================ delayed-action handlers (DAT_004d5c98[type]) */

/* Port of FUN_004479de (11 B) — handler 0: nothing. */
int rl_4479de(void)
{
    return 0;
}

/* Port of FUN_004479e9 (112 B) — handler 1 (message 30): when the sender's DAT_0057c934 (+4 arg) equals ours, realm
 * DAT_005158e8's +0 = 999, the ranking cookie DAT_0056d6a0 and DAT_005530d0 set. Retail first compares an
 * UNINITIALIZED local ([ebp-4]: the residue of 0x4472ce's previous callee) with the local player — equal →
 * DAT_0055403c = 2 — and then stores the realm in it: rl_4479e9_res (in / out; rhost moves it, offset 8 below e0). */
uint32_t rl_4479e9_res;
int rl_4479e9(void)
{
    if ((int)s_slot_0057c934 != g_rl_5158a8)
        return g_rl_5158a8;
    if ((int)rl_4479e9_res == LOCAL)
        g_bf_55403c = 2;
    rl_4479e9_res = (uint32_t)g_rl_5158e8;
    PL32(g_rl_5158e8, 0x57bf00) = 999;
    engine_army_ai_set_ranking_dirty(1);
    g_combat_flag_530d0 = 1;
    return 352 * g_rl_5158e8;
}

/* Port of FUN_00447b8e (64 B) — handler 7: DAT_0056d6a4 = 1, DAT_0052afe4 = timeGetTime(); message 8 twice, 50 ms
 * apart. */
int rl_447b8e(void)
{
    g_rl_56d6a4 = 1;
    g_rl_52afe4 = (int32_t)rl_plat_timeGetTime();
    rl_43eda0(8, 0);
    bf_sleep(50);
    rl_43eda0(8, 0);
    return 0;
}

/* Port of FUN_00447c68 (49 B) — handler 12: 0x4a98fe(b0, e8, a8, a4, ac). */
int rl_447c68(void)
{
    return rl_4a98fe(g_rl_5158b0, g_rl_5158e8, g_rl_5158a8, g_rl_5158a4, g_rl_5158ac);
}

/* Port of FUN_00447c99 (53 B) — handler 13: spawn the unit (0x4a9a9a(e8, a8, a4, b0)); DAT_005530d0 = 1. */
int rl_447c99(void)
{
    int r = engine_combat_spawn_unit(g_rl_5158e8, g_rl_5158a8, g_rl_5158a4, g_rl_5158b0);   /* 0x4a9a9a */
    g_combat_flag_530d0 = 1;
    return r;
}

/* Port of FUN_00447d22 (31 B) — handler 16: build the keep (0x436d02(e8, a8)). */
int rl_447d22(void)
{
    engine_army_ai_build_keep(g_rl_5158e8, g_rl_5158a8);   /* 0x436d02 */
    return 0;
}

/* Port of FUN_00447db6 (37 B) — handler 20: the shipment (0x43b145(e8, a8, a4)). */
int rl_447db6(void)
{
    engine_sendsupplies_commit(g_rl_5158e8, g_rl_5158a8, (uint8_t)g_rl_5158a4);   /* 0x43b145 */
    return 0;
}

/* Port of FUN_0044800e (25 B) — handler 30: 0x47a76d(e8). */
int rl_44800e(void)
{
    return bf_47a76d(g_rl_5158e8);
}

/* Port of FUN_00448339 (152 B) — handler 43: move a4 from realm a8's +0x118 to realm e8's when a8 has that much
 * (signed ≥); realm e8's +0x8c + 16·a8 = the amount. Its three locals (e8, a4, a8 at 8 / 12 / 16 below e0) stay as
 * residue: rl_448339_res. EAX: the amount, else 352·a8. */
uint32_t rl_448339_res[3];
int rl_448339(void)
{
    int to = g_rl_5158e8, from = g_rl_5158a8, n = g_rl_5158a4;
    rl_448339_res[0] = (uint32_t)to;
    rl_448339_res[1] = (uint32_t)n;
    rl_448339_res[2] = (uint32_t)from;
    if (PL32(from, 0x57c018) < n)
        return 352 * from;
    PL32(to, 0x57c018) += n;
    PL32(from, 0x57c018) -= n;
    PL32(to, 0x57bf8c + 16 * from) = n;
    return n;
}

/* Port of FUN_00448422 (26 B) — handler 49: 0x435211; DAT_0057d340 = 2. */
int rl_448422(void)
{
    int r = rl_435211();
    g_bf_57d340 = 2;
    return r;
}
