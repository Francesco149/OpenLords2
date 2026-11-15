/* rl_mpmsg3.c — a retail-layout port, partition 'mpmsg3' (tools/bf/parts.py): the MP message layer's remaining payload
 * builders / receivers, more delayed-action handlers, the DirectPlay system-event logger 0x43e6a2 and the session
 * fallback 0x43e8f4 (docs/findings/rl-mpmsg3.md). Retail semantics on retail's layout: rl.h (record accessors,
 * globals). The protocol (send table DAT_004d57f0[id] / receive table DAT_004d5980[id], 0x4401ba / 0x4401f6, the
 * delayed actions 0x447136 → DAT_004d5c98[type]): rl_mpmsg2.c's header and docs/findings/rl-mpmsg2.md. Builders,
 * receivers and handlers are reached only through those tables; nobody reads their EAX. */
#include "rl.h"
#include "rl_mpmsg3_globals.h"
#include "rl_io_globals.h"        /* DAT_004eafa4, 0x5530f0, 0x553248, 0x57cb40, 0x57d354, 0x57c96c, 0x5aeb8c, 0x5c9274 */
#include "rl_host_globals.h"      /* DAT_00515b24, the save name DAT_0053f660 */
#include "rl_ui2_globals.h"       /* 0x53e9a0, 0x53e9ec, 0x5530b8, 0x553d30, 0x57a0dc, 0x57c988, 0x591558 */
#include "rl_render_globals.h"    /* 0x53f690 (troop rows), the builders' lead 0x544094, 0x553074, 0x553e80, 0x565440 */
#include "engine_bf_input_globals.h" /* DAT_0059151c, DAT_0059154c / 50 / 54, 0x553ed8, 0x57c90c */
#include "engine.h"               /* g_sim_mp_active */
#include "engine_combat.h"        /* 0x43b622 / 0x43be65 / 0x49cf20 / 0x437535 / 0x43b917, g_siege_*, DAT_0053f284 */
#include "engine_army_ai.h"       /* 0x438681 */
#include "engine_army_economy.h"  /* 0x44df5f, 0x4485a5 */
#include "engine_diplomacy.h"     /* 0x4a2621 */
#include "rl_cov3.h"              /* 0x4a1b29 ai_diplomacy_gift */
#include "engine_title_render.h"  /* 0x4ae539, 0x4ae5e2, g_tr_p4_flash_timer (DAT_00568984) */
#include "engine_battlefield_render.h" /* g_bfr_engine_state (DAT_0052af94) */
#include "engine_diplomacy_action.h" /* g_diplo_action_mode (DAT_005651cc) */
#include "gfx_globals.h"
#include "engine_map_query.h"
#include "engine_territory.h"      /* engine_territory_dirty_state (DAT_0053e9ac lives in its s_dirty mirror) */     /* g_cell_cursor_col / _row (DAT_00569570 / DAT_00569578) */          /* g_gfx_screen_byte (DAT_004eac50) */
/* other partitions' generated globals (their headers clash when included together): the declarations, verbatim */
extern uint8_t g_rl_50d7c0[0x5780];                                              /* rl_ui3: the action table */
extern uint8_t g_rl_512f9c[4];                                                   /* rl_mpmsg: the packing scratch */
extern int32_t g_rl_542cc0, g_rl_553e70;                                         /* rl_mpmsg: the due ticks */
extern int32_t g_rl_5158a4, g_rl_5158a8, g_rl_5158ac, g_rl_5158b0, g_rl_5158bc, g_rl_5158e8;   /* rl_ctl: args */
extern int32_t g_rl_51590c;                                                      /* rl_ctl: the running record */
extern int32_t g_rl_53e904[2], g_rl_53f080, g_rl_544090, g_rl_567d08, g_rl_568d98, g_rl_56d678, g_rl_57c8b8; /* rl_glue */
extern uint8_t g_rl_53f5fc[0x44];                                                /* rl_skirmish */
extern int32_t g_rl_5440bc, g_rl_5533e0[2], g_rl_55cd34, g_rl_56899c, g_rl_55ce94;   /* rl_skirmish */
extern int32_t g_rl_553234, g_rl_569520[4];                                      /* rl_mpmsg2 */
extern int32_t g_rl_53f0d8, g_rl_55416c;                                         /* rl_ctl: grid category, task */

#define P(x)          ((int)(uintptr_t)(x))
#define PUT(x, n)     rl_4401ba(P(x), (n))     /* append n bytes at DAT_00512f8c */
#define GET(x, n)     rl_4401f6(P(x), (n))     /* take n bytes from DAT_00515918 */
#define CPY(s, d, n)  rl_401136(P(s), P(d), (n))
#define LOCAL         ((int)s_slot_0057c8cc)   /* DAT_0057c8cc: the local player */
#define OTHER         ((int)s_slot_0056d5cc)   /* DAT_0056d5cc: the sender */
#define LEAD          g_rl_544094              /* DAT_00544094: the builders' lead */
#define DUE           g_rl_553e70              /* DAT_00553e70: a builder's due tick */
#define RDUE          g_rl_542cc0              /* DAT_00542cc0: a receiver's due tick */
#define NPLAYERS      (*(int32_t *)&s_slot_00553f98)   /* DAT_00553f98: players in the session */
#define ACT(type, due, ...) rl_447136((type), (due), __VA_ARGS__)
#define PL8(p, va)    AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))    /* realm record */
#define PX8(p, va)    AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))             /* realm aux */
#define AUXB(va)      AT(uint8_t, g_player_aux_44, 0x553d50u, (va))                                 /* an aux byte */
#define HX8(i, va)    AT(uint8_t, g_per_battle_hex_432, 0x554480u, (int32_t)(va) + 432 * (i))       /* battle hex */
#define TER(t, off)   (&AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b0 + 420 * (t) + (off)))  /* territory */
#define DANAME(j)     (&AT(uint8_t, g_rl_50d7c0, 0x50d7c0u, 0x50d7ec + 56 * (j)))   /* action record +0x2c: 10 bytes */
#define NAME10(p)     (&AT(uint8_t, g_bf_53e930, 0x53e930u, 0x53e930 + 10 * (p)))   /* DAT_0053e930[10 p] */
#define TROOPS(p)     (&AT(uint8_t, g_rl_53f690, 0x53f690u, 0x53f6a0 + 128 * (p)))  /* DAT_0053f6a0[32 p] */
#define SHIELD(p)     AT(uint8_t, g_rl_57cb40, 0x57cb40u, 0x57cb40 + (p))           /* DAT_0057cb40[p] (no bound) */
#define MPST(p)       AT(int32_t, g_rl_5533e0, 0x5533e0u, 0x5533e0 + 4 * (p))       /* DAT_005533e0[p] (no bound) */
#define D59154C       AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)
#define D591550       AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u)
#define D591554       AT(int32_t, g_bf_59154c, 0x59154cu, 0x591554u)
#define D553F14       AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f14u)
#define D553F18       AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f18u)
#define B59151C       (g_bf_59151c[0])
#define NETSYS(va)    P(&AT(uint8_t, g_rl_4d5d70, 0x4d5d70u, (va)))   /* the "OK :DP netsys" lines 0x4d5d70.. */
#define SV34(va)      P(&AT(uint8_t, g_rl_4d6004, 0x4d6004u, (va)))   /* "sva" / "svb" 0x4d6004.. */
#define SV56(va)      P(&AT(uint8_t, g_rl_4d60f8, 0x4d60f8u, (va)))   /* "sva" / "svb" 0x4d60f8.. */
#define SAVENAME      P(g_rl_53f660)                                  /* DAT_0053f660: the MP save file name */

/* ================================================================ the DirectPlay system events */

/* Port of FUN_0043e8f4 (151 B) — a session falls back to single play: `reset` == 1 first clears the shield slots
 * DAT_0057cb40[0..5], slot 1 = 1, DAT_0057d354 = 1, the local player DAT_0057c8cc = 1; then, the session still
 * flagged (DAT_0057a0f0), 0x4b743b, one player, DAT_0057a0f0 = 0, screen 31 (DAT_004eac50), 0x4978ad,
 * 0x497a34(1). No caller reads EAX. */
int rl_43e8f4(int reset)
{
    int r = 0;
    if (reset == 1) {
        for (int i = 0; i < 6; i++)
            SHIELD(i) = 0;
        r = 6;
        SHIELD(1) = 1;
        g_rl_57d354 = 1;
        s_slot_0057c8cc = 1;
    }
    if (s_slot_0057a0f0 != 0) {
        rl_4b743b();
        s_slot_00553f98 = 1;
        s_slot_0057a0f0 = 0;
        g_gfx_screen_byte = 31;
        rl_4978ad();
        return rl_497a34(1);
    }
    return r;
}

/* Port of FUN_0043e6a2 (594 B) — a DirectPlay system message: logs "OK :DP netsys - …" (0x4afab9) for kinds 0..8.
 * Kind 2 (player deleted): in the battle (DAT_0052af94 > 2) the player slot of DPID `dpid` (0x43e98b, unchecked)
 * resigns (0x49cf20), the session shrinks, the slot is out (aux +0x26) and its shield slot cleared, 0x43e8f4(0);
 * else a known slot loses its shield, 0x49bb9d(slot), the session shrinks, 0x49caaa, DAT_0057d340 = 1; one player
 * left → 0x4b743b, 0x43e8f4(1), title page 1, DAT_0057a0f0 / DAT_0057c8fc = 0, the misc / system sheets,
 * DAT_0057d340 = 2; more → the host (DAT_00553248) sends message 3. Kind 4 (we are the host): DAT_00553248 = 1,
 * 0x43e8f4(0). No caller reads EAX. */
int rl_43e6a2(int kind, int dpid)
{
    switch (kind) {
    case 0: return rl_4afab9(NETSYS(0x4d5d70), 0, 0);   /* "OK :DP netsys - connect" */
    case 1: return rl_4afab9(NETSYS(0x4d5d88), 0, 0);   /* "… - add player" */
    case 2: {
        int r = rl_4afab9(NETSYS(0x4d5da4), 0, 0);      /* "… - delete player" */
        if ((int32_t)g_bfr_engine_state > 2) {
            int p = rl_43e98b(dpid);
            engine_combat_resign_player(p, rl_evq_load(), g_sim_mp_active);   /* 0x49cf20 (row combat_resign_player) */
            rl_evq_store();
            NPLAYERS--;
            PX8(p, 0x553d76) = 1;
            SHIELD(p) = 0;
            return rl_43e8f4(0);
        }
        int p = rl_43e98b(dpid);
        if (p == 0)
            return r;
        SHIELD(p) = 0;
        engine_retail_call(0x49bb9du, 1, (uint32_t)p);   /* TODO(rl-link): 0x49bb9d (in no partition) */
        NPLAYERS--;
        rl_49caaa();
        g_bf_57d340 = 1;
        if (NPLAYERS > 1) {
            if (g_rl_553248 != 0)
                return rl_43eda0(3, 0);
            return r;
        }
        rl_4b743b();
        rl_43e8f4(1);
        g_rl_5530f0 = 1;
        s_slot_0057a0f0 = 0;
        s_slot_0057c8fc = 0;
        rl_499034();
        r = rl_499a1c(0);
        g_bf_57d340 = 2;
        return r;
    }
    case 3: return rl_4afab9(NETSYS(0x4d5dc4), 0, 0);   /* "… - session lost" */
    case 4:
        rl_4afab9(NETSYS(0x4d5de4), 0, 0);              /* "… - host" */
        g_rl_553248 = 1;
        return rl_43e8f4(0);
    case 5: return rl_4afab9(NETSYS(0x4d5dfc), 0, 0);   /* "… - add player to group" */
    case 6: return rl_4afab9(NETSYS(0x4d5e20), 0, 0);   /* "… - delete player from group" */
    case 7: return rl_4afab9(NETSYS(0x4d5e4c), 0, 0);   /* "… - set player or group data" */
    case 8: return rl_4afab9(NETSYS(0x4d5e78), 0, 0);   /* "… - set player or group name" */
    default: return kind;
    }
}

/* ================================================================ payload builders / receivers (by message id) */

/* Port of FUN_0044255f (26 B) — builder 13: the player count DAT_0053e8a4 (4). */
int rl_44255f(void)
{
    return PUT(&s_slot_0053e8a4, 4);
}

/* Port of FUN_00442579 (70 B) — receiver 13: the player count (4 → DAT_0053e8a4); the title setup's defaults
 * (0x4ae539), the count clamped (0x4ae5e2), 0x433cc1; the tabs redraw (DAT_0055ce94 = 1), DAT_0057d340 = 2. */
int rl_442579(void)
{
    GET(&s_slot_0053e8a4, 4);
    engine_title_render_setup_apply_defaults();                            /* 0x4ae539 */
    engine_title_render_setup_clamp_player_count((int)s_slot_0053e8a4);    /* 0x4ae5e2 */
    int r = rl_433cc1();
    g_rl_55ce94 = 1;
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_00442add (11 B) — builder 17: no payload. */
int rl_442add(void)
{
    return 0;
}

/* Port of FUN_00442ae8 (21 B) — receiver 17: DAT_00568984 = 0. */
int rl_442ae8(void)
{
    g_tr_p4_flash_timer = 0;
    return 0;
}

/* Port of FUN_00442d00 (96 B) — builder 23: the due tick (0x446e21(DAT_00544094)) 4, the save name DAT_0053f660 16;
 * action 3 (write the MP save) for us. */
int rl_442d00(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(g_rl_53f660, 16);
    return ACT(3, DUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442d60 (77 B) — receiver 23: the due tick, the save name; action 3. */
int rl_442d60(void)
{
    GET(&RDUE, 4);
    GET(g_rl_53f660, 16);
    return ACT(3, RDUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442dad (96 B) — builder 24: as builder 23, action 2 (load the MP save). */
int rl_442dad(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(g_rl_53f660, 16);
    return ACT(2, DUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442e0d (77 B) — receiver 24: action 2. */
int rl_442e0d(void)
{
    GET(&RDUE, 4);
    GET(g_rl_53f660, 16);
    return ACT(2, RDUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442e5a (146 B) — builder 25: the due tick (4; no action filed); in a session (DAT_0057a0f0):
 * DAT_0053f238 = 0, screen 46 (DAT_004eac50, DAT_0057c90c), 0x42c64f, DAT_0057c8fc = 0, DAT_0052af94 = 2,
 * DAT_0057d340 = 2; else 0x46f21f, DAT_005c9274 = 1, DAT_005aeb8c = 0. */
int rl_442e5a(void)
{
    int r;
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    if (s_slot_0057a0f0 != 0) {
        g_bf_53f238 = 0;
        g_gfx_screen_byte = 46;
        g_bf_57c90c = 46;
        r = rl_42c64f();
        s_slot_0057c8fc = 0;
        g_bfr_engine_state = 2;
        g_bf_57d340 = 2;
    } else {
        r = rl_46f21f();
        g_rl_5c9274 = 1;
        g_rl_5aeb8c = 0;
    }
    return r;
}

/* Port of FUN_00442eec (115 B) — receiver 25: the due tick (DAT_00542cc0, unused); as builder 25 but without
 * DAT_0057c90c. */
int rl_442eec(void)
{
    int r;
    GET(&RDUE, 4);
    if (s_slot_0057a0f0 != 0) {
        g_bf_53f238 = 0;
        g_gfx_screen_byte = 46;
        r = rl_42c64f();
        s_slot_0057c8fc = 0;
        g_bfr_engine_state = 2;
        g_bf_57d340 = 2;
    } else {
        r = rl_46f21f();
        g_rl_5c9274 = 1;
        g_rl_5aeb8c = 0;
    }
    return r;
}

/* Port of FUN_00443043 (64 B) — builder 28: DAT_0059154c 4, the load result DAT_0059151c 4; a failed load (2) →
 * DAT_005aeb8c = 1. */
int rl_443043(void)
{
    PUT(&D59154C, 4);
    PUT(g_bf_59151c, 4);
    if (B59151C == 2)
        g_rl_5aeb8c = 1;
    return B59151C;
}

/* Port of FUN_00443083 (82 B) — receiver 28: the sender (→ DAT_0056d5cc), its load result → DAT_00553d30[sender];
 * 2 counts DAT_0057a0dc; DAT_005aeb8c = 1. */
int rl_443083(void)
{
    int v;
    GET(&s_slot_0056d5cc, 4);
    GET(&v, 4);
    g_rl_553d30[OTHER] = v;
    if (v == 2)
        ++g_rl_57a0dc;
    g_rl_5aeb8c = 1;
    return v;
}

/* Port of FUN_004430d5 (110 B) — builder 29: the save name 16; its extension (DAT_004eafa4) "sva" (host,
 * DAT_00553248) or "svb", replaced in the name (0x4af62c strip, 0x4af675 append). */
int rl_4430d5(void)
{
    PUT(g_rl_53f660, 16);
    if (g_rl_553248 != 0)
        CPY(SV34(0x4d6004), g_rl_4eafa4, 3);
    else
        CPY(SV34(0x4d6008), g_rl_4eafa4, 3);
    rl_4af62c(SAVENAME);
    return rl_4af675(SAVENAME);
}

/* Port of FUN_00443143 (172 B) — receiver 29: the save name, the extension as builder 29; DAT_0059154c = the local
 * player; DAT_0059151c = the load (0x4adfe6) result: 1 loaded, 2 failed; message 28 back. */
int rl_443143(void)
{
    GET(g_rl_53f660, 16);
    if (g_rl_553248 != 0)
        CPY(SV34(0x4d600c), g_rl_4eafa4, 3);
    else
        CPY(SV34(0x4d6010), g_rl_4eafa4, 3);
    rl_4af62c(SAVENAME);
    rl_4af675(SAVENAME);
    D59154C = LOCAL;
    if (rl_4adfe6(SAVENAME) != 0)
        B59151C = 1;
    else
        B59151C = 2;
    return rl_43eda0(0x1c, 0);
}

/* Port of FUN_0044334e (61 B) — builder 33: DAT_0057c93c 1, DAT_005530b8 1; both: 0x43a997(them). */
int rl_44334e(void)
{
    PUT(&s_slot_0057c93c, 1);
    PUT(g_rl_5530b8, 1);
    return rl_43a997((int)s_slot_0057c93c, g_rl_5530b8[0]);
}

/* Port of FUN_0044338b (78 B) — receiver 33: DAT_00553234 (zeroed, 1 byte), a byte; 0x43a997(them). */
int rl_44338b(void)
{
    int v = 0;
    g_rl_553234 = 0;
    GET(&g_rl_553234, 1);
    GET(&v, 1);
    return rl_43a997(g_rl_553234, v);
}

/* Port of FUN_0044356d (26 B) — builder 36: the local player 4. */
int rl_44356d(void)
{
    return PUT(&s_slot_0057c8cc, 4);
}

/* Port of FUN_00443587 (71 B) — receiver 36: the sender (→ DAT_0056d5cc) leaves: realm +4 = 0 (not human), aux +0x26
 * = 1 (out). */
int rl_443587(void)
{
    GET(&s_slot_0056d5cc, 4);
    PL8(OTHER, 0x57bf04) = 0;
    PX8(OTHER, 0x553d76) = 1;
    return 44 * OTHER;
}

/* Port of FUN_004435ce (109 B) — builder 37: DAT_0057c93c 1, DAT_0053e9a0 1, DAT_0053e9ec 1, DAT_0057c988 2; both:
 * 0x439b52(DAT_0057c93c, the bytes - 1, DAT_0057c988). */
int rl_4435ce(void)
{
    PUT(&s_slot_0057c93c, 1);
    PUT(g_rl_53e9a0, 1);
    PUT(g_rl_53e9ec, 1);
    PUT(g_rl_57c988, 2);
    return rl_439b52((int)s_slot_0057c93c, g_rl_53e9a0[0] - 1, g_rl_53e9ec[0] - 1, g_rl_57c988[0]);
}

/* Port of FUN_0044363b (128 B) — receiver 37: DAT_00553234 (zeroed) 1, three zeroed locals 1 / 1 / 2;
 * 0x439b52(them, - 1 on the bytes). */
int rl_44363b(void)
{
    int a = 0, b = 0, c = 0;
    g_rl_553234 = 0;
    GET(&g_rl_553234, 1);
    GET(&a, 1);
    GET(&b, 1);
    GET(&c, 2);
    return rl_439b52(g_rl_553234, a - 1, b - 1, c);
}

/* Port of FUN_004436bb (62 B) — builder 38 (the workforce grid's double-click, 0x439df0 in MP): the selected county
 * DAT_0057c93c 1, the category DAT_0053f0d8 1; both: 0x439edb(county, category − 1) at once. */
int rl_4436bb(void)
{
    PUT(&s_slot_0057c93c, 1);
    PUT(&g_rl_53f0d8, 1);
    return rl_439edb((int)s_slot_0057c93c, g_rl_53f0d8 - 1);
}

/* Port of FUN_004436f9 (79 B) — receiver 38: DAT_00553234 (zeroed) 1, a zeroed local 1; 0x439edb(them, b − 1). */
int rl_4436f9(void)
{
    int v = 0;
    g_rl_553234 = 0;
    GET(&g_rl_553234, 1);
    GET(&v, 1);
    return rl_439edb(g_rl_553234, v - 1);
}

/* Port of FUN_00444915 (51 B) — builder 58 (a map click on an own county's task site, 0x43ce1a in MP, after its own
 * 0x43d309): DAT_0057c93c 1, the task DAT_0055416c 1; DAT_0052afcc = 1. EAX: the last append's. */
int rl_444915(void)
{
    PUT(&s_slot_0057c93c, 1);
    int r = PUT(&g_rl_55416c, 1);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_00444948 (88 B) — receiver 58: DAT_00553234 (zeroed) 1, a zeroed local 1; 0x43d309(them, task);
 * DAT_0052afcc = 1. */
int rl_444948(void)
{
    int v = 0;
    g_rl_553234 = 0;
    GET(&g_rl_553234, 1);
    GET(&v, 1);
    int r = rl_43d309(g_rl_553234, v);
    g_combat_render_dirty = 1;
    return r;
}

/* Port of FUN_00443e51 (184 B) — builder 45: due, the local player, DAT_00553074, DAT_0053f080, DAT_00568d98 (4
 * each), our troop row DAT_0053f6a0[32 p] 128; action 18 for us. */
int rl_443e51(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&g_rl_553074, 4);
    PUT(&g_rl_53f080, 4);
    PUT(&g_rl_568d98, 4);
    PUT(TROOPS(LOCAL), 128);
    return ACT(18, DUE, LOCAL, g_rl_553074, g_rl_53f080, g_rl_568d98, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443f09 (162 B) — receiver 45: due, the sender, DAT_00569520 and two more (4 each), the sender's troop
 * row; action 18. */
int rl_443f09(void)
{
    int a, b;
    GET(&RDUE, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&g_rl_569520[0], 4);
    GET(&a, 4);
    GET(&b, 4);
    GET(TROOPS(OTHER), 128);
    return ACT(18, RDUE, OTHER, g_rl_569520[0], a, b, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00443fab (100 B) — builder 46: due, DAT_0053e904 (4 each); action 19. */
int rl_443fab(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&g_rl_53e904[0], 4);
    return ACT(19, DUE, g_rl_53e904[0], 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044400f (81 B) — receiver 46: due, DAT_00569520; action 19. */
int rl_44400f(void)
{
    GET(&RDUE, 4);
    GET(&g_rl_569520[0], 4);
    return ACT(19, RDUE, g_rl_569520[0], 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444346 (100 B) — builder 50: due, DAT_0059154c; action 23. */
int rl_444346(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&D59154C, 4);
    return ACT(23, DUE, D59154C, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004443aa (81 B) — receiver 50: due, a value; action 23. */
int rl_4443aa(void)
{
    int v;
    GET(&RDUE, 4);
    GET(&v, 4);
    return ACT(23, RDUE, v, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044462f (119 B) — builder 54: due, the siege attacker DAT_0055cd58, its region DAT_0052f0a8; action
 * 26. */
int rl_44462f(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&g_siege_sel_attacker, 4);
    PUT(&g_siege_sel_region, 4);
    return ACT(26, DUE, g_siege_sel_attacker, g_siege_sel_region, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004446a6 (100 B) — receiver 54: due, DAT_00569520, DAT_00553234; action 26. */
int rl_4446a6(void)
{
    GET(&RDUE, 4);
    GET(&g_rl_569520[0], 4);
    GET(&g_rl_553234, 4);
    return ACT(26, RDUE, g_rl_569520[0], g_rl_553234, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044470a (82 B) — builder 55: the besieger DAT_00568ed0, DAT_00544090, DAT_00567d08 (4 each); both:
 * 0x43b7c4(them) at once. */
int rl_44470a(void)
{
    PUT(&g_siege_besieger, 4);
    PUT(&g_rl_544090, 4);
    PUT(&g_rl_567d08, 4);
    return rl_43b7c4(g_siege_besieger, g_rl_544090, g_rl_567d08);
}

/* Port of FUN_0044475c (79 B) — receiver 55: DAT_00569520 and two values; 0x43b7c4(them). */
int rl_44475c(void)
{
    int a, b;
    GET(&g_rl_569520[0], 4);
    GET(&a, 4);
    GET(&b, 4);
    return rl_43b7c4(g_rl_569520[0], a, b);
}

/* Port of FUN_004447ab (100 B) — builder 56: due, the besieger; action 36. */
int rl_4447ab(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&g_siege_besieger, 4);
    return ACT(36, DUE, g_siege_besieger, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044480f (81 B) — receiver 56: due, DAT_00569520; action 36. */
int rl_44480f(void)
{
    GET(&RDUE, 4);
    GET(&g_rl_569520[0], 4);
    return ACT(36, RDUE, g_rl_569520[0], 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444860 (100 B) — builder 57: due, the besieger; action 37. */
int rl_444860(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&g_siege_besieger, 4);
    return ACT(37, DUE, g_siege_besieger, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004448c4 (81 B) — receiver 57: due, DAT_00569520; action 37. */
int rl_4448c4(void)
{
    GET(&RDUE, 4);
    GET(&g_rl_569520[0], 4);
    return ACT(37, RDUE, g_rl_569520[0], 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* the battlefield's AUTO_RESOLVE button 0x43b622 / its end 0x43be65 (old ports with rows battle_auto_resolve /
 * battle_end; they post through the event queue) */
static void auto_resolve(void)
{
    engine_combat_auto_resolve_button(rl_evq_load(), g_sim_mp_active);
    rl_evq_store();
}
static void battle_end(void)
{
    engine_combat_battlefield_end(rl_evq_load(), g_sim_mp_active);
    rl_evq_store();
}

/* Port of FUN_00444b07 (16 B) — builder 61: no payload; auto-resolve (0x43b622). */
int rl_444b07(void)
{
    auto_resolve();
    return 0;
}

/* Port of FUN_00444b17 (16 B) — receiver 61: auto-resolve (0x43b622). */
int rl_444b17(void)
{
    auto_resolve();
    return 0;
}

/* the battlefield's pause toggle shared by messages 62 / 63: DAT_0053f238 set → inverted (~), DAT_0053e9ac = 2, the
 * full-screen redraw (0x45240a), DAT_00553ed8 = 1 */
static void unpause(void)
{
    if (g_bf_53f238 != 0) {
        g_bf_53f238 = ~g_bf_53f238;
        engine_territory_dirty_state()->below_status_redraw = 2;   /* DAT_0053e9ac (the s_dirty mirror) */
        rl_45240a();
        g_bf_553ed8 = 1;
    }
}

/* Port of FUN_00444b27 (169 B) — builder 62: the unpause; due, the local player, DAT_005653f4 (4 each); action 39. */
int rl_444b27(void)
{
    unpause();
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_005653f4, 4);
    return ACT(39, DUE, LOCAL, (int)s_slot_005653f4, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444bd0 (150 B) — receiver 62: the unpause; due, the sender, a value; action 39. */
int rl_444bd0(void)
{
    int v;
    unpause();
    GET(&RDUE, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&v, 4);
    return ACT(39, RDUE, OTHER, v, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00444c66 (79 B) — builder 63: no payload; the unpause; in the battle (DAT_0057c8fc == 2) its end
 * (0x43be65). */
int rl_444c66(void)
{
    unpause();
    if (s_slot_0057c8fc == 2)
        battle_end();
    return 0;
}

/* Port of FUN_00444cb5 (79 B) — receiver 63: as builder 63. */
int rl_444cb5(void)
{
    unpause();
    if (s_slot_0057c8fc == 2)
        battle_end();
    return 0;
}

/* Port of FUN_00444e3f (343 B) — builder 66: battle hex DAT_0056d5d0's +0x0a loses the bits of realm (its +0x2c)'s
 * +0xee, its +0x09 = 0; 0x479276(local); the due tick packed with the local player (3 + 1, DAT_00512f9c) 4, our
 * 10-byte DAT_0053e930 row 10; action 33 for us, the row copied into the record (+0x2c). */
int rl_444e3f(void)
{
    int h = g_bf_56d5d0;
    uint8_t m = (uint8_t)~PL8(HX8(h, 0x5544ac), 0x57bfee);
    HX8(h, 0x55448a) &= m;
    HX8(h, 0x554489) = 0;
    rl_479276(LOCAL);
    DUE = rl_446e21(LEAD);
    CPY(&DUE, g_rl_512f9c, 3);
    CPY(&s_slot_0057c8cc, &g_rl_512f9c[3], 1);
    PUT(g_rl_512f9c, 4);
    PUT(NAME10(LOCAL), 10);
    ACT(33, DUE, LOCAL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return CPY(NAME10(LOCAL), DANAME(g_rl_515b24), 10);
}

/* Port of FUN_00444f96 (205 B) — receiver 66: the packed due tick / sender, the sender's DAT_0053e930 row; action
 * 33, the row into the record. */
int rl_444f96(void)
{
    s_slot_0056d5cc = 0;
    RDUE = 0;
    GET(g_rl_512f9c, 4);
    CPY(g_rl_512f9c, &RDUE, 3);
    CPY(&g_rl_512f9c[3], &s_slot_0056d5cc, 1);
    GET(NAME10(OTHER), 10);
    ACT(33, RDUE, OTHER, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return CPY(NAME10(OTHER), DANAME(g_rl_515b24), 10);
}

/* Port of FUN_00445063 (361 B) — builder 67: 0x479276(local); the packed due tick / local player 4, our DAT_0053e930
 * row 10, DAT_00553f14 2, DAT_00553f18 2, DAT_005533b8 2, DAT_0053e874 1, DAT_0059154c 1, DAT_00591550 2,
 * DAT_00591554 1; DAT_00591554 == 0 → action 34 for us (+ the row into the record). */
int rl_445063(void)
{
    rl_479276(LOCAL);
    DUE = rl_446e21(LEAD);
    CPY(&DUE, g_rl_512f9c, 3);
    CPY(&s_slot_0057c8cc, &g_rl_512f9c[3], 1);
    PUT(g_rl_512f9c, 4);
    PUT(NAME10(LOCAL), 10);
    PUT(&D553F14, 2);
    PUT(&D553F18, 2);
    PUT(&g_bf_5533b8, 2);
    PUT(&g_bf_53e874[0], 1);
    PUT(&D59154C, 1);
    PUT(&D591550, 2);
    int r = PUT(&D591554, 1);
    if (D591554 == 0) {
        ACT(34, DUE, LOCAL, D553F14, D553F18, g_bf_5533b8, g_bf_53e874[0], D59154C, 0, 0, 0, 0);
        return CPY(NAME10(LOCAL), DANAME(g_rl_515b24), 10);
    }
    return r;
}

/* Port of FUN_004451cc (471 B) — receiver 67: the packed due tick / sender, its row, six zeroed locals (2, 2, 2, 1, 1
 * by width), the sender's sequence (2) and a kind (1). A sequence equal to the last one DAT_00553100[sender] (a
 * signed word) is a repeat: nothing. Else a gap (last < seq - 1) counts DAT_004d2158, kind 1 / 2 count
 * DAT_004d2150 / DAT_004d2154, the sequence is stored, action 34 for the sender (+ the row into the record). */
int rl_4451cc(void)
{
    int k = 0, seq = 0, b2 = 0, b1 = 0, a3 = 0, a2 = 0, a1 = 0;
    s_slot_0056d5cc = 0;
    RDUE = 0;
    GET(g_rl_512f9c, 4);
    CPY(g_rl_512f9c, &RDUE, 3);
    CPY(&g_rl_512f9c[3], &s_slot_0056d5cc, 1);
    GET(NAME10(OTHER), 10);
    GET(&a1, 2);
    GET(&a2, 2);
    GET(&a3, 2);
    GET(&b1, 1);
    GET(&b2, 1);
    GET(&seq, 2);
    GET(&k, 1);
    int last = g_bf_553100[OTHER];
    if (last == seq)
        return last;
    if (g_bf_553100[OTHER] < seq - 1)
        ++g_rl_4d2158;
    if (k == 1)
        ++g_rl_4d2150;
    else if (k == 2)
        ++g_rl_4d2154;
    g_bf_553100[OTHER] = (int16_t)seq;
    ACT(34, RDUE, OTHER, a1, a2, a3, b1, b2, 0, 0, 0, 0);
    return CPY(NAME10(OTHER), DANAME(g_rl_515b24), 10);
}

/* Port of FUN_00445458 (81 B) — builder 69: due; action 38. */
int rl_445458(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    return ACT(38, DUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004454a9 (62 B) — receiver 69: due; action 38. */
int rl_4454a9(void)
{
    GET(&RDUE, 4);
    return ACT(38, RDUE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004454e7 (198 B) — builder 70 (the type-10 popup's yes in MP, 0x4367ff: pay an ally its price for an
 * attack): due, the local player's ally (realm +0x81, 4 bytes from there), the local player, the target county
 * DAT_0055ce50, the price DAT_00567958 (4 each); action 40 for us with the ally as the +0x81 BYTE. */
int rl_4454e7(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&PL8(LOCAL, 0x57bf81), 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_0055ce50, 4);
    PUT(&s_slot_00567958, 4);
    return ACT(40, DUE, PL8(LOCAL, 0x57bf81), LOCAL, (int)s_slot_0055ce50, (int)s_slot_00567958, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004455ad (129 B) — receiver 70: due, the ally (as the sender wrote it: 4 bytes from realm +0x81), the
 * payer, the county, the price; action 40 with them. */
int rl_4455ad(void)
{
    int ally, payer, county, price;
    GET(&RDUE, 4);
    GET(&ally, 4);
    GET(&payer, 4);
    GET(&county, 4);
    GET(&price, 4);
    return ACT(40, RDUE, ally, payer, county, price, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044562e (147 B) — builder 71 (an alliance answer): due, the partner DAT_0057c8b8, the local player,
 * the answer DAT_0056d678 (4 each); yes (1) → action 41(partner, local). */
int rl_44562e(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&g_rl_57c8b8, 4);
    PUT(&s_slot_0057c8cc, 4);
    int r = PUT(&g_rl_56d678, 4);
    if (g_rl_56d678 == 1)
        return ACT(41, DUE, g_rl_57c8b8, LOCAL, 0, 0, 0, 0, 0, 0, 0, 0);
    return r;
}

/* Port of FUN_004456c1 (224 B) — receiver 71: due, the partner, the sender, the answer; yes → action 41; the
 * partner us → event 256 (yes) / 257 (no) from the sender (0x472bc5, kind 12). */
int rl_4456c1(void)
{
    int partner, ans;
    GET(&RDUE, 4);
    GET(&partner, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&ans, 4);
    if (ans == 1)
        ACT(41, RDUE, partner, OTHER, 0, 0, 0, 0, 0, 0, 0, 0);
    if (LOCAL == partner)
        return rl_472bc5(OTHER, LOCAL, ans == 1 ? 256 : 257, 0, 12, 0, 0, 0);
    return partner;
}

/* Port of FUN_004457a1 (176 B) — builder 72 (a diplomacy action): due, the local player, DAT_00553fbc, the action
 * DAT_005651cc, DAT_0057a0f8, DAT_005530b0 (4 each); action 42 for us. */
int rl_4457a1(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_00553fbc, 4);
    PUT(&g_diplo_action_mode, 4);
    PUT(&s_slot_0057a0f8, 4);
    PUT(&s_slot_005530b0, 4);
    return ACT(42, DUE, LOCAL, (int)s_slot_00553fbc, g_diplo_action_mode, (int)s_slot_0057a0f8, (int)s_slot_005530b0,
               0, 0, 0, 0, 0);
}

/* Port of FUN_00445851 (148 B) — receiver 72: due, the sender, four values; action 42. */
int rl_445851(void)
{
    int a, b, c, d;
    GET(&RDUE, 4);
    GET(&s_slot_0056d5cc, 4);
    GET(&a, 4);
    GET(&b, 4);
    GET(&c, 4);
    GET(&d, 4);
    return ACT(42, RDUE, OTHER, a, b, c, d, 0, 0, 0, 0, 0);
}

/* Port of FUN_00445d97 (119 B) — builder 75: due, DAT_0059154c, DAT_0056d5cc; action 44. */
int rl_445d97(void)
{
    DUE = rl_446e21(LEAD);
    PUT(&DUE, 4);
    PUT(&D59154C, 4);
    PUT(&s_slot_0056d5cc, 4);
    return ACT(44, DUE, D59154C, OTHER, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00445e0e (97 B) — receiver 75: due, two values; action 44. */
int rl_445e0e(void)
{
    int a, b;
    GET(&RDUE, 4);
    GET(&a, 4);
    GET(&b, 4);
    return ACT(44, RDUE, a, b, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_004460f8 (121 B) — builder 82: the local player, territory DAT_00553efc (4 each), its +360 4 and
 * +364 22. */
int rl_4460f8(void)
{
    PUT(&s_slot_0057c8cc, 4);
    PUT(&s_slot_00553efc, 4);
    PUT(TER(s_slot_00553efc, 360), 4);
    return PUT(TER(s_slot_00553efc, 364), 22);
}

/* Port of FUN_00446171 (131 B) — receiver 82: the sender, the territory (→ DAT_00553f34), its +360 / +364;
 * DAT_0057d340 = 2. */
int rl_446171(void)
{
    GET(&s_slot_0056d5cc, 4);
    GET(&s_slot_00553f34, 4);
    GET(TER(s_slot_00553f34, 360), 4);
    int r = GET(TER(s_slot_00553f34, 364), 22);
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_004461f4 (56 B) — builder 83: the local player, DAT_0059154c, DAT_00591550 (4 each). */
int rl_4461f4(void)
{
    PUT(&s_slot_0057c8cc, 4);
    PUT(&D59154C, 4);
    return PUT(&D591550, 4);
}

/* Port of FUN_0044622c (153 B) — receiver 83: the sender, DAT_0059154c, a kind; DAT_00591558 = 1; kind 1..4 →
 * 0x43d929 / 0x43d9cd / 0x43dc1d / 0x43dd83; DAT_0057d340 = 2. */
int rl_44622c(void)
{
    int k;
    GET(&s_slot_0056d5cc, 4);
    GET(&D59154C, 4);
    int r = GET(&k, 4);
    g_rl_591558 = 1;
    switch (k) {
    case 1: r = rl_43d929(); break;
    case 2: r = rl_43d9cd(); break;
    case 3: r = rl_43dc1d(); break;
    case 4: r = rl_43dd83(); break;
    default: break;
    }
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_0044637e (11 B) — builder 85: no payload. */
int rl_44637e(void)
{
    return 0;
}

/* Port of FUN_00446389 (80 B) — receiver 85: our DAT_005533e0 == 1 → messages 86 and 82; the sender's = 1 (no
 * bound: quirk 3 of rl-mpmsg2.md); DAT_0057d340 = 2. */
int rl_446389(void)
{
    if (MPST(LOCAL) == 1) {
        rl_43eda0(0x56, 0);
        rl_43eda0(0x52, 0);
    }
    MPST(OTHER) = 1;
    g_bf_57d340 = 2;
    return OTHER;
}

/* Port of FUN_004463d9 (267 B) — builder 86 (the skirmish setup): DAT_0053e91c, DAT_0056899c (4), the aux bytes
 * 0x553da4 / 0x553dd0 (1), DAT_0053e8fc, DAT_0053ef5c, DAT_00522f60, DAT_0057c910, DAT_0057c96c, DAT_0056d590,
 * DAT_0053f0d4, DAT_0056d5c0, DAT_0053f64c, DAT_0053e9a8 (4 each), the 65-byte DAT_0053f5fc, DAT_005440bc 4. */
int rl_4463d9(void)
{
    PUT(&s_slot_0053e91c, 4);
    PUT(&g_rl_56899c, 4);
    PUT(&AUXB(0x553da4), 1);
    PUT(&AUXB(0x553dd0), 1);
    PUT(&s_slot_0053e8fc, 4);
    PUT(&s_slot_0053ef5c, 4);
    PUT(&s_slot_00522f60, 4);
    PUT(&s_slot_0057c910, 4);
    PUT(&g_rl_57c96c, 4);
    PUT(&s_slot_0056d590, 4);
    PUT(&s_slot_0053f0d4, 4);
    PUT(&s_slot_0056d5c0, 4);
    PUT(&s_slot_0053f64c, 4);
    PUT(&s_slot_0053e9a8, 4);
    PUT(g_rl_53f5fc, 65);
    return PUT(&g_rl_5440bc, 4);
}

/* the skirmish setup screens' redraw 0x42b9c4, 0x42ba40, 0x42bf46 */
static int setup_redraw(void)
{
    rl_42b9c4();
    rl_42ba40();
    return rl_42bf46();
}

/* Port of FUN_004464e4 (415 B) — receiver 86: the fields as builder 86 (the 65 bytes into a local, the last dword
 * into DAT_005691e4); DAT_005533e0[local] = DAT_005533e0[sender] = 1; 0x42d40b; DAT_0055cd34 set → message 94, else
 * the 65 bytes into DAT_0053f5fc, 0x42d4aa, the redraw, DAT_0057d340 = 2; then 0x498dcb, the redraw again,
 * DAT_0057d340 = 2. */
int rl_4464e4(void)
{
    uint8_t buf[68];
    GET(&s_slot_0053e91c, 4);
    GET(&g_rl_56899c, 4);
    GET(&AUXB(0x553da4), 1);
    GET(&AUXB(0x553dd0), 1);
    GET(&s_slot_0053e8fc, 4);
    GET(&s_slot_0053ef5c, 4);
    GET(&s_slot_00522f60, 4);
    GET(&s_slot_0057c910, 4);
    GET(&g_rl_57c96c, 4);
    GET(&s_slot_0056d590, 4);
    GET(&s_slot_0053f0d4, 4);
    GET(&s_slot_0056d5c0, 4);
    GET(&s_slot_0053f64c, 4);
    GET(&s_slot_0053e9a8, 4);
    GET(buf, 65);
    GET(&g_rl_5691e4[0], 4);
    MPST(LOCAL) = 1;
    MPST(OTHER) = 1;
    rl_42d40b();
    if (g_rl_55cd34 != 0) {
        rl_43eda0(0x5e, 0);
    } else {
        CPY(buf, g_rl_53f5fc, 65);
        rl_42d4aa();
        setup_redraw();
        g_bf_57d340 = 2;
    }
    rl_498dcb();
    int r = setup_redraw();
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_004469bd (47 B) — builder 93 (the MP skirmish setup's user-map list: another *.skr picked, 0x434174
 * already loaded it): its name DAT_0053f5fc 65, DAT_005440bc 4. */
int rl_4469bd(void)
{
    PUT(g_rl_53f5fc, 65);
    return PUT(&g_rl_5440bc, 4);
}

/* the user-map fallback of 93 / 94: a missing file (DAT_0055cd34 == 1) while the user map is the mode
 * (DAT_0053e91c == 3) drops to mode 0 and redraws */
static int skr_fallback(void)
{
    int r = 0;
    if (g_rl_55cd34 == 1 && s_slot_0053e91c == 3) {
        s_slot_0053e91c = 0;
        r = setup_redraw();
        g_bf_57d340 = 2;
    }
    return r;
}

/* Port of FUN_004469ec (204 B) — receiver 93: the name (65, a local), DAT_005691e4 4; 0x42d40b (list the *.skr);
 * found (DAT_0055cd34 0) → mode 3, the name into DAT_0053f5fc, 0x42d4aa, the redraw, DAT_0057d340 = 2; else the
 * fallback; message 94 back either way. */
int rl_4469ec(void)
{
    uint8_t buf[68];
    GET(buf, 65);
    GET(&g_rl_5691e4[0], 4);
    rl_42d40b();
    if (g_rl_55cd34 == 0) {
        s_slot_0053e91c = 3;
        CPY(buf, g_rl_53f5fc, 65);
        rl_42d4aa();
        setup_redraw();
        g_bf_57d340 = 2;
    } else {
        skr_fallback();
    }
    return rl_43eda0(0x5e, 0);
}

/* Port of FUN_00446ab8 (87 B) — builder 94 (93's / 86's answer): DAT_0055cd34 4; the fallback. */
int rl_446ab8(void)
{
    int r = PUT(&g_rl_55cd34, 4);
    if (g_rl_55cd34 == 1 && s_slot_0053e91c == 3)
        r = skr_fallback();
    return r;
}

/* Port of FUN_00446b0f (145 B) — receiver 94: the peer's DAT_0055cd34 4; 0 (it has the file) → mode 3, 0x42d4aa,
 * the redraw, DAT_005530f0 = DAT_00553e5c; 1 with mode 3 → mode 0, the redraw; DAT_0057d340 = 2. */
int rl_446b0f(void)
{
    int r = GET(&g_rl_55cd34, 4);
    if (g_rl_55cd34 == 0) {
        s_slot_0053e91c = 3;
        rl_42d4aa();
        setup_redraw();
        r = g_rl_553e5c;
        g_rl_5530f0 = g_rl_553e5c;
    } else if (g_rl_55cd34 == 1 && s_slot_0053e91c == 3) {
        s_slot_0053e91c = 0;
        r = setup_redraw();
    }
    g_bf_57d340 = 2;
    return r;
}

/* the battlefield resync (mp-desync): 0x440666's sum mismatch → 0x43f5c5 (snapshot restore) → message 16; three
 * turn-byte mismatches in a row → message 91 */
extern int32_t g_rl_515850[10];   /* rl_mpmsg: the players' resync flags DAT_00515850[p] */
extern int32_t g_rl_512f88;       /* rl_glue: the turn-byte mismatch count */
#define RESYNC(p)     AT(int32_t, g_rl_515850, 0x515850u, 0x515850 + 4 * (p))   /* DAT_00515850[p] (no bound) */

/* Port of FUN_0043f855 (170 B) — the resync is complete when every live human player (1..5: realm +5, +4, not out:
 * aux +0x26) has its flag DAT_00515850[p]: then DAT_00553fa8 = 0 and the ready flags cleared (0x43f8ff). EAX: the
 * first player still without it, else 0x43f8ff's. No caller reads EAX. */
int rl_43f855(void)
{
    for (int p = 1; p < 6; p++)
        if (PL8(p, 0x57bf05) != 0 && PL8(p, 0x57bf04) != 0 && PX8(p, 0x553d76) == 0 && RESYNC(p) == 0)
            return p;
    g_rl_553fa8 = 0;
    return rl_43f8ff();
}

extern int32_t g_rl_553ec8;       /* rl_quit: the resync round count */
#define SNAP_ON(m)    AT(int32_t, g_rl_512f40, 0x512f40u, 0x512f40 + 4 * (m))   /* DAT_00512f40[mode]: the snapshot pair's selector */
#define SNAP_A(m)     AT(int32_t, g_rl_5158d0, 0x5158d0u, 0x5158d0 + 8 * (m))   /* DAT_005158d0[2 mode] */
#define SNAP_B(m)     AT(int32_t, g_rl_5158d0, 0x5158d0u, 0x5158d4 + 8 * (m))   /* DAT_005158d4[2 mode] */

/* Port of FUN_0047ebe8 (332 B) — rebuild the battlefield's occupancy after a snapshot restore: 0x46c7fc(5) / (6); every
 * cell's flag 0x80 (+1 of the 80 × 80 cells at 0x5440e0, 8 B each) cleared with the walk counters DAT_00569578 /
 * DAT_00569570 (engine_map_query's g_cell_cursor_row / _col: the one copy) / DAT_005533cc left at 80 / 80 / 51200; every live unit 1..80 (+0x2c) back on its cell (+0x1c → cell
 * +5 = the unit), a unit with +0x194 re-placed at (+0x20, +0x22) by 0x49207e; the groups 0x485bb1; DAT_0053e9ac = 1.
 * EAX: 0x485bb1's. */
int rl_47ebe8(void)
{
    bf_46c7fc(5);
    bf_46c7fc(6);
    for (g_cell_cursor_row = 0, g_bf_5533cc = 0; g_cell_cursor_row < 80; g_cell_cursor_row++)
        for (g_cell_cursor_col = 0; g_cell_cursor_col < 80; g_cell_cursor_col++, g_bf_5533cc += 8)
            BCELL8(g_bf_5533cc, 0x5440e1) &= 0x7f;
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544ac) == 0)
            continue;
        BCELL8(BS32(i, 0x55449c), 0x5440e5) = (uint8_t)i;
        if (BU8(i, 0x554614) != 0)
            bf_49207e(BS16(i, 0x5544a0), BS16(i, 0x5544a2));
    }
    int r = bf_485bb1();
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac: the s_dirty mirror is the one copy */
    return r;
}

/* Port of FUN_0043f5c5 (542 B) — the resync's snapshot restore (0x440666 when the hash sums differ): the snapshot
 * DAT_005158d0 / d4[2 mode] (DAT_00512f40[mode] picks; none → 0). On the battlefield (mode 2) it holds the units
 * (0x554630, 0x8700), the combat units (0x57a14c, 0x1db0), the hexes' aux (0x566554, 0x1040), DAT_0058fe28 and
 * DAT_0058fe08 (4 each, the last two from the same offset + 0 / + 4); then the tick cadence 0x40489e, the occupancy
 * 0x47ebe8, 0x480f8b; the 4th resync in a row (DAT_00553214 ≥ 4): DAT_00553ec8 counted, DAT_00553214 = 0, the clock
 * DAT_005653f8 down (≤ 0 → 46), DAT_0053f018 counted, and with DAT_0057a0f0 clear past the first the last turn
 * reloaded (save_load "lastturn.sva" host / ".svb"), else the field rebuilt 0x4778a0; event 259 to us; the 3rd:
 * 0x479a71 for both battle sides. Any mode: DAT_00554038 counted, the hold-off DAT_0052afcc = 1, the replay /
 * state-hash clears 0x43ff63 / 0x446f06 / 0x43ff08 / 0x43ffc3, the tick reset 0x43f304, message 16. EAX: 1. */
int rl_43f5c5(void)
{
    int m = (int)s_slot_0057c8fc;
    int src = SNAP_ON(m) == 1 ? SNAP_A(m) : SNAP_B(m);
    if (src == 0)
        return 0;
    if (s_slot_0057c8fc == 2) {
        rl_4b3d5d(src, P(&AT(uint8_t, g_per_battle_hex_432, 0x554480u, 0x554630)), 0x8700);
        src += 0x8700;
        rl_4b3d5d(src, P(&AT(uint8_t, g_per_combat_unit_76, 0x57a100u, 0x57a14c)), 0x1db0);
        src += 0x1db0;
        rl_4b3d5d(src, P(&AT(uint8_t, g_per_battle_hex_aux_52, 0x566520u, 0x566554)), 0x1040);
        src += 0x1040;
        rl_4b3d5d(src, P(&s_slot_0058fe28), 4);
        rl_4b3d5d(src + 4, P(&s_slot_0058fe08), 4);
        rl_40489e();
        rl_47ebe8();
        bf_480f8b();
        if (g_rl_553214 >= 4) {
            g_rl_553ec8++;
            g_rl_553214 = 0;
            if ((int32_t)s_slot_005653f8 > 0)
                s_slot_005653f8--;
            else
                s_slot_005653f8 = 46;
            g_rl_53f018++;
            if (s_slot_0057a0f0 == 0 && g_rl_53f018 > 1)
                rl_4adfe6(P(&AT(uint8_t, g_rl_4d5f28, 0x4d5f28u, g_rl_553248 != 0 ? 0x4d5f28 : 0x4d5f38)));
            else
                bf_4778a0();
            rl_472bc5(0, LOCAL, 259, 0, 0, 0, 0, 0);
        } else if (g_rl_553214 >= 3) {
            bf_479a71((int)s_slot_005681ec);
            bf_479a71((int)s_slot_005681d8);
        }
    }
    g_rl_554038++;
    g_combat_render_dirty = 1;
    rl_43ff63();
    rl_446f06();
    rl_43ff08();
    rl_43ffc3();
    bf_43f304();
    rl_43eda0(16, 0);
    return 1;
}

/* Port of FUN_00442a7f (47 B) — builder 16 (sent by 0x43f5c5 after the snapshot restore): our player DAT_0057c8cc 4;
 * our flag DAT_00515850[local] = 1; 0x43f855. */
int rl_442a7f(void)
{
    PUT(&s_slot_0057c8cc, 4);
    RESYNC(LOCAL) = 1;
    return rl_43f855();
}

/* Port of FUN_00442aae (47 B) — receiver 16: the sender into DAT_0056d5cc 4; its flag DAT_00515850[sender] = 1;
 * 0x43f855. */
int rl_442aae(void)
{
    GET(&s_slot_0056d5cc, 4);
    RESYNC(OTHER) = 1;
    return rl_43f855();
}

/* Port of FUN_0044695f (36 B) — builder 91 (0x440666: the turn bytes differed a third time): no payload;
 * DAT_00512f88 = 0, the replay / state-hash clears 0x43ff63 / 0x446f06, the tick reset 0x43f304. */
int rl_44695f(void)
{
    g_rl_512f88 = 0;
    rl_43ff63();
    rl_446f06();
    return bf_43f304();
}

/* Port of FUN_00446983 (36 B) — receiver 91: the same as its builder. */
int rl_446983(void)
{
    g_rl_512f88 = 0;
    rl_43ff63();
    rl_446f06();
    return bf_43f304();
}

/* Port of FUN_00446683 (266 B) — builder 87: the aux bytes 0x553da4 / 0x553dd0 (1), DAT_0053e8fc, DAT_0053ef5c (4);
 * DAT_005533e0[local] = [DAT_0056d5cc] = 0 (host, DAT_00553248) / 2 (client); the chat lines DAT_00553e80 /
 * DAT_00565440 (65 each) cleared; DAT_005533d0 = 1, DAT_0057d340 = 2. */
int rl_446683(void)
{
    PUT(&AUXB(0x553da4), 1);
    PUT(&AUXB(0x553dd0), 1);
    PUT(&s_slot_0053e8fc, 4);
    PUT(&s_slot_0053ef5c, 4);
    if (g_rl_553248 != 0) {
        MPST(LOCAL) = 0;
        MPST(OTHER) = 0;
    } else {
        MPST(LOCAL) = 2;
        MPST(OTHER) = 2;
    }
    for (int i = 0; i < 65; i++)
        g_rl_553e80[i] = 0;
    for (int j = 0; j < 65; j++)
        g_rl_565440[j] = 0;
    s_slot_005533d0 = 1;
    g_bf_57d340 = 2;
    return 65;
}

/* Port of FUN_0044678d (281 B) — receiver 87: the fields as builder 87; host → DAT_005533e0[local] = [sender] = 2;
 * client → DAT_00591558 = 1, 0x43d5b7, both 0; the chat lines cleared; DAT_005533d0 = 1, DAT_0057d340 = 2. */
int rl_44678d(void)
{
    GET(&AUXB(0x553da4), 1);
    GET(&AUXB(0x553dd0), 1);
    GET(&s_slot_0053e8fc, 4);
    GET(&s_slot_0053ef5c, 4);
    if (g_rl_553248 != 0) {
        MPST(LOCAL) = 2;
        MPST(OTHER) = 2;
    } else {
        g_rl_591558 = 1;
        rl_43d5b7();
        MPST(LOCAL) = 0;
        MPST(OTHER) = 0;
    }
    for (int i = 0; i < 65; i++)
        g_rl_553e80[i] = 0;
    for (int j = 0; j < 65; j++)
        g_rl_565440[j] = 0;
    s_slot_005533d0 = 1;
    g_bf_57d340 = 2;
    return 65;
}

/* Port of FUN_00446ba0 (26 B) — builder 95: DAT_0056899c 4. */
int rl_446ba0(void)
{
    return PUT(&g_rl_56899c, 4);
}

/* Port of FUN_00446bba (50 B) — receiver 95: DAT_0056899c; the aux bytes 0x553da4 / 0x553dd0 = 2; DAT_0057d340 =
 * 2. */
int rl_446bba(void)
{
    int r = GET(&g_rl_56899c, 4);
    AUXB(0x553da4) = 2;
    AUXB(0x553dd0) = 2;
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_00446bec (51 B) — builder 96: the aux bytes 0x553da4 / 0x553dd0 (1 each). */
int rl_446bec(void)
{
    PUT(&AUXB(0x553da4), 1);
    return PUT(&AUXB(0x553dd0), 1);
}

/* Port of FUN_00446c1f (66 B) — receiver 96: the two aux bytes; 0x42bf46; DAT_0057d340 = 2. */
int rl_446c1f(void)
{
    GET(&AUXB(0x553da4), 1);
    GET(&AUXB(0x553dd0), 1);
    int r = rl_42bf46();
    g_bf_57d340 = 2;
    return r;
}

/* ================================================================ the delayed-action handlers (by type) */
#define A4   g_rl_5158e8   /* record +4 */
#define A8   g_rl_5158a8   /* +8 */
#define AC   g_rl_5158a4   /* +0xc */
#define A10  g_rl_5158b0   /* +0x10 */
#define A14  g_rl_5158ac   /* +0x14 */
#define A18  g_rl_5158bc   /* +0x18 */

/* Port of FUN_00447a59 (108 B) — action 2: the MP save's extension "sva" (host) / "svb" into DAT_004eafa4, the name
 * DAT_0053f660 re-extended, loaded (0x4adfe6). */
int rl_447a59(void)
{
    if (g_rl_553248 != 0)
        CPY(SV56(0x4d60f8), g_rl_4eafa4, 3);
    else
        CPY(SV56(0x4d60fc), g_rl_4eafa4, 3);
    rl_4af62c(SAVENAME);
    rl_4af675(SAVENAME);
    return rl_4adfe6(SAVENAME);
}

/* Port of FUN_00447ac5 (108 B) — action 3: as action 2, written (0x4ade93). */
int rl_447ac5(void)
{
    if (g_rl_553248 != 0)
        CPY(SV56(0x4d6100), g_rl_4eafa4, 3);
    else
        CPY(SV56(0x4d6104), g_rl_4eafa4, 3);
    rl_4af62c(SAVENAME);
    rl_4af675(SAVENAME);
    return rl_4ade93(SAVENAME);
}

/* Port of FUN_00447d72 (43 B) — action 18: 0x437fd7(+4, +8, +0xc, +0x10). */
int rl_447d72(void)
{
    return rl_437fd7(A4, A8, AC, A10);
}

/* Port of FUN_00447d9d (25 B) — action 19: disband occupant +4 into its realm (0x438681). */
int rl_447d9d(void)
{
    engine_army_ai_disband_into_realm(A4);
    return 0;
}

/* Port of FUN_00447e0b (313 B) — action 23: toggle option +4: 1 → DAT_0053f25c, 2 → DAT_0053f260 (then every realm
 * 1..DAT_0056d5dc re-provisions 0x44df5f(i, DAT_0057c934) and re-plans 0x4485a5(i, DAT_0057c92c)), 3 →
 * DAT_0053f264 (each "== 1 ? 0 : 1"), 4 → DAT_0053f284 ("== 0 ? 1 : 0"); DAT_0057d340 = 2. */
int rl_447e0b(void)
{
    int r = 0;
    switch (A4) {
    case 1:
        s_slot_0053f25c = s_slot_0053f25c == 1 ? 0 : 1;
        break;
    case 2:
        s_slot_0053f260 = s_slot_0053f260 == 1 ? 0 : 1;
        for (int i = 1; i <= (int)s_slot_0056d5dc; i++) {
            engine_army_economy_provision(i, (int)s_slot_0057c934);    /* 0x44df5f */
            engine_army_economy_plan_realm(i, (int)s_slot_0057c92c);   /* 0x4485a5 */
        }
        r = (int)s_slot_0056d5dc;
        break;
    case 3:
        s_slot_0053f264 = s_slot_0053f264 == 1 ? 0 : 1;
        break;
    case 4:
        g_combat_flag_53f284 = g_combat_flag_53f284 == 0 ? 1 : 0;
        break;
    default:
        break;
    }
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_00447f82 (31 B) — action 26: move occupant +4 to realm +8's seat (0x437535). */
int rl_447f82(void)
{
    engine_combat_transfer_army((unsigned)A4, A8, rl_evq_load(), g_sim_mp_active);
    rl_evq_store();
    return 0;
}

/* the 10-byte DAT_0053e930 row swap of actions 33 / 34: 0x479276(local), our row saved in DAT_0053f0b0, the running
 * record's (DAT_0051590c, +0x2c) into player `p`'s row, 0x47935b(p) */
static void row_in(int p)
{
    rl_479276(LOCAL);
    CPY(NAME10(LOCAL), g_rl_53f0b0, 10);
    CPY(DANAME(g_rl_51590c), NAME10(p), 10);
    rl_47935b(p);
}
/* … and back: our saved row restored, 0x47935b(local) */
static int row_out(void)
{
    CPY(g_rl_53f0b0, NAME10(LOCAL), 10);
    return rl_47935b(LOCAL);
}

/* Port of FUN_00448076 (193 B) — action 33: player +4's row from the record, 0x478987(+4), DAT_0053e9ac = 1, our row
 * back. */
int rl_448076(void)
{
    int p = A4;
    row_in(p);
    bf_478987(p);
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac (the s_dirty mirror) */
    return row_out();
}

/* Port of FUN_00448137 (269 B) — action 34: player +4's row from the record, 0x478987 / 0x478f0b(+4), 0x481b9a; the
 * unit 0x479e0c(+4): 0x4891ad(it), 0x479e90(it, +8, +0xc, +0x10, +0x14, +0x18); our row back. */
int rl_448137(void)
{
    int p = A4;
    row_in(p);
    bf_478987(p);
    bf_478f0b(p);
    rl_481b9a();
    int u = rl_479e0c(p);
    bf_4891ad(u);
    bf_479e90(u, A8, AC, A10, A14, A18);
    return row_out();
}

/* Port of FUN_0044825d (25 B) — action 36: break occupant +4's siege link (0x43b917). */
int rl_44825d(void)
{
    engine_combat_break_siege_link(A4);
    return 0;
}

/* Port of FUN_00448276 (25 B) — action 37: 0x43b98a(+4). */
int rl_448276(void)
{
    return rl_43b98a(A4);
}

/* Port of FUN_0044828f (16 B) — action 38: 0x496b9f. */
int rl_44828f(void)
{
    return bf_496b9f();
}

/* Port of FUN_0044829f (31 B) — action 39: 0x43bbd7(+4, +8) (the battlefield's end 0x43be65; its arguments unused). */
int rl_44829f(void)
{
    return bf_43bbd7(A4, A8);
}

/* Port of FUN_004482be (43 B) — action 40: the payer +8 gives the ally +4 the price +0x10 for county +0xc
 * (ai_diplomacy_gift 0x4a1b29: the ally's campaign target). */
int rl_4482be(void)
{
    return rl_4a1b29(A4, A8, AC, A10);
}

/* Port of FUN_004482e9 (31 B) — action 41: an alliance 0x4a1774(+4, +8). */
int rl_4482e9(void)
{
    return rl_4a1774(A4, A8);
}

/* Port of FUN_00448308 (49 B) — action 42: a diplomacy action 0x4a2621(+4, +8, +0xc, +0x10, +0x14). */
int rl_448308(void)
{
    engine_diplomacy_queue_action(A4, A8, AC, A10, A14);
    return 0;
}

/* Port of FUN_004483d1 (31 B) — action 44: a treaty cleared 0x4a1a54(+4, +8). */
int rl_4483d1(void)
{
    return rl_4a1a54(A4, A8);
}
