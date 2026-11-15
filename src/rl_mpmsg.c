/* rl_mpmsg.c — a retail-layout port, partition 'mpmsg' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * docs/findings/rl-mpmsg.md: the MP message layer's first half — the tick minima, the receive dispatcher 0x43eac1,
 * the MP state helpers, the payload copy helpers 0x4401ba / 0x4401f6, the per-player state hash, and the payload
 * builders / receivers of message ids 1..22 (tables 0x4d57f0 / 0x4d5980). */
#include "rl.h"
#include "rl_mpmsg_globals.h"
#include "rl_host_globals.h"      /* DAT_004d2164, the resend buffers 0x515b30, the hash regions 0x565700 / 0x569530 */
#include "rl_io_globals.h"        /* DAT_004e2a28 (our DPID), the receive buffer 0x515c50, 0x5530e0, 0x5530f0, 0x553248 */
#include "rl_ui2_globals.h"       /* the battle snapshot buffers 0x5158d0, their selectors 0x512f40 / 0x512f60, 0x512f90 */
#include "rl_render_globals.h"    /* the chat lines 0x5651e0 / 0x553e80, the counters */
#include "engine_bf_input_globals.h"
#include "rl_io_plat.h"           /* GetTickCount / timeGetTime */
#include "engine.h"               /* g_sim_mp_active, g_engine_state, the screen / redraw bridges */
#include "engine_combat.h"        /* g_combat_render_dirty (0x52afcc), g_combat_anim_deadline (0x568d9c), _scratch (0x515d60) */
#include "engine_title_render.h"  /* g_tr_p4_flash_timer (DAT_00568984) */
#include "engine_territory.h"     /* the bound dirty state (DAT_0053e9ac) */
#include "engine_army_ai.h"       /* engine_army_ai_set_ranking_dirty (DAT_0056d6a0) */

/* other partitions' generated globals whose headers conflict with the ones above (same types as there) */
extern uint8_t g_rl_4d5b90[256];     /* rl_glue: the message sizes by id (255 = undefined) */
extern int32_t g_rl_512f4c;          /* rl_glue: the send ring's slot */
extern int32_t g_rl_512f8c;          /* rl_glue: the payload write pointer */
extern int32_t g_rl_53e8a8;          /* rl_glue */
extern uint8_t g_rl_53f090[12];      /* rl_glue: the hash mismatch flags */
extern int32_t g_rl_53f068;          /* rl_ctl: the live player count */
extern int32_t g_rl_515914;          /* rl_ctl */
extern int32_t g_rl_515b20;          /* rl_ctl */
extern int32_t g_rl_5bb4a0;          /* rl_ctl */
extern int32_t g_rl_553260;          /* rl_turn */
extern int32_t g_rl_55ce88;          /* rl_turn */
extern int32_t g_rl_553ec8;          /* rl_quit */
extern int32_t g_rl_55ce94;          /* rl_skirmish */
extern int32_t g_rl_57c948;          /* rl_skirmish */
extern int32_t g_rl_5c9a74;          /* rl_ui2 (declared there; repeated for clarity) */

/* ---------------------------------------------------------------- retail records / globals */
#define PL8(p, va)   AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define AUX8(p, va)  AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))           /* player session */
#define AUX32(p, va) AT(int32_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))
#define AUXP(p, va)  ((int)(uintptr_t)&AUX8(p, va))
#define RSND(p, k)   AT(uint8_t, g_rl_515b30, 0x515b30u, 0x515b30 + 41 * (p) + (k))   /* resend countdowns 7 × 41 */
#define RCV(k)       AT(uint8_t, g_rl_515c50, 0x515c50u, 0x515c50 + (k))              /* the receive buffer */
#define HSUM(p, k)   AT(uint8_t, g_rl_569530, 0x569530u, 0x569530 + 10 * (p) + (k))   /* per-player hash sums [p][10] */
#define HREG(va, p, k) AT(uint8_t, g_rl_565700, 0x565700u, (int32_t)(va) + 600 * (p) + (k))   /* hash regions */
#define TICK(p)      g_bf_53e8d0[(p)]                                                 /* DAT_0053e8d0[p]: peer ticks */
#define SNAPSEL(p)   AT(int32_t, g_rl_512f60, 0x512f60u, 0x512f60 + 4 * (p))   /* DAT_00512f60[mode]: 1 / 2 */
#define SNAPSEL2(p)  AT(int32_t, g_rl_512f40, 0x512f40u, 0x512f40 + 4 * (p))   /* DAT_00512f40[mode] */
#define SNAPSUM1(p)  AT(int32_t, g_rl_512f90, 0x512f90u, 0x512f90 + 4 * (p))   /* DAT_00512f90[mode] */
#define SNAPSUM2(p)  AT(int32_t, g_rl_50d7b0, 0x50d7b0u, 0x50d7b0 + 4 * (p))   /* DAT_0050d7b0[mode] */
#define SNAPBUF(p, k) AT(int32_t, g_rl_5158d0, 0x5158d0u, 0x5158d0 + 8 * (p) + 4 * (k))   /* DAT_005158d0 / d4 [mode] */
#define LAYOUT(r, k) AT(int32_t, g_rl_4d5b10, 0x4d5b10u, 0x4d5b10 + 12 * (r) + 4 * (k))   /* {count, end, start} rows */
#define LOCAL   ((int)s_slot_0057c8cc)       /* DAT_0057c8cc: the local player */
#define OTHER   ((int)s_slot_0056d5cc)       /* DAT_0056d5cc: the sender's / other player */
#define MODE    ((int)s_slot_0057c8fc)       /* DAT_0057c8fc: 0 strategic, 2 battlefield */
#define MP_ON   (g_sim_mp_active != 0)       /* DAT_00553030 */
#define NPLAYERS (*(int32_t *)&s_slot_00553f98)   /* DAT_00553f98: players in the session */
#define PTR(x)  ((int)(uintptr_t)(x))
#define STR(va) PTR(&AT(uint8_t, g_rl_4d5ea4, 0x4d5ea4u, (va)))   /* the "ERR:" lines 0x4d5ea4.. */
#define OKSTR(va) PTR(&AT(uint8_t, g_rl_4d5f8c, 0x4d5f8cu, (va)))  /* the "OK :" lines 0x4d5f8c.. */
/* a realm is in play (+5), human (+4) and its session not gone (aux +0x26) — the minima's filter */
#define LIVE(p)  (PL8(p, 0x57bf05) != 0 && PL8(p, 0x57bf04) != 0 && AUX8(p, 0x553d76) == 0)
#define ALLY(p)  ((p) == (int)s_slot_005681ec || (p) == (int)s_slot_005681d8)   /* the battle's two sides */

/* ================================================================ tick minima, the session lookup */

/* Port of FUN_0043e330 (259 B) — the lowest peer tick DAT_0053e8d0[p] over the live human players p = 1..6 (slot 6
 * is past the 6-record tables: retail reads it anyway), skipping `except`; on the battlefield (mode 2) only the
 * battle's two sides count. DAT_0053f068 = the live players (all, `except` included). 10000000 when none. */
int rl_43e330(int except)
{
    int m = 10000000;
    g_rl_53f068 = 0;
    for (int i = 1; i <= 6; i++) {
        if (!LIVE(i))
            continue;
        g_rl_53f068++;
        if (i == except)
            continue;
        if (MODE == 2 && !ALLY(i))
            continue;
        if (TICK(i) < m)
            m = TICK(i);
    }
    return m;
}

/* Port of FUN_0043e433 (218 B) — on the battlefield: at most one live human among the battle's two sides (1), else
 * 0; off the battlefield 0. */
int rl_43e433(void)
{
    if (MODE != 2)
        return 0;
    int n = 0;
    for (int i = 1; i <= 6; i++)
        if (LIVE(i) && ALLY(i))
            n++;
    return n <= 1 ? 1 : 0;
}

/* Port of FUN_0043e50d (180 B) — the lowest peer tick over every live human player 1..6 (10000000 when none). */
int rl_43e50d(void)
{
    int m = 10000000;
    for (int i = 1; i <= 6; i++)
        if (LIVE(i) && TICK(i) < m)
            m = TICK(i);
    return m;
}

/* Port of FUN_0043e5c1 (167 B) — the link-speed handicap: the best machine score DAT_00553120[i] over the realms in
 * play (0..5), DAT_004d2160 = (100 - our score in percent of it (0x404dc1)) / 2 (truncated). Returns it. */
int rl_43e5c1(void)
{
    int best = 0;
    for (int i = 0; i < 6; i++)
        if (PL8(i, 0x57bf05) != 0 && g_rl_553120[i] > best)
            best = g_rl_553120[i];
    g_rl_4d2160 = 100 - rl_404dc1(g_rl_553120[LOCAL], best);
    g_rl_4d2160 = g_rl_4d2160 / 2;
    return g_rl_4d2160;
}

/* Port of FUN_0043e98b (87 B) — the player slot 1..5 whose session DPID (aux +0) is `dpid`; 0 when none. */
int rl_43e98b(int dpid)
{
    for (int i = 1; i < 6; i++)
        if (AUX32(i, 0x553d50) == dpid)
            return i;
    return 0;
}

/* ================================================================ the receive dispatcher, the id tables */

/* the payload builders (table 0x4d57f0) and receivers (0x4d5980) on the port side: a ported or stubbed rl_<va> by name,
 * else retail's function through engine_retail_call (the standalone stops there) */
typedef int (*mp_fn)(void);
#define MPF(va) rl_##va
static const mp_fn k_mp_build[100] = {
    [1] = MPF(441066), [2] = MPF(44121a), [3] = MPF(4413a4), [4] = MPF(441905), [5] = MPF(441b4e), [6] = MPF(441d2a),
    [7] = MPF(441f2a), [8] = MPF(441f76), [9] = MPF(4420ad), [10] = MPF(442309), [11] = MPF(4423b9), [12] = MPF(442448),
    [14] = MPF(4425bf), [15] = MPF(4429f2), [18] = MPF(442afd), [20] = MPF(442bad), [21] = MPF(442c3f), [22] = MPF(442cb9),
    [30] = MPF(4431ef), [34] = MPF(4433d9), [35] = MPF(44348c), [39] = MPF(443748), [40] = MPF(44381d), [41] = MPF(443990),
    [42] = MPF(443ad5), [43] = MPF(443c2f), [44] = MPF(443d0a), [47] = MPF(444060), [48] = MPF(444191), [49] = MPF(4441d7),
    [51] = MPF(4443fb), [59] = MPF(4449a0), [60] = MPF(444a2f), [64] = MPF(444d04), [73] = MPF(4458e5), [74] = MPF(445c99),
    [76] = MPF(445e6f), [77] = MPF(445f43), [84] = MPF(4462c5),
    /* partition mpmsg3 */
    [13] = MPF(44255f), [17] = MPF(442add), [23] = MPF(442d00), [24] = MPF(442dad), [25] = MPF(442e5a), [28] = MPF(443043),
    [29] = MPF(4430d5), [33] = MPF(44334e), [36] = MPF(44356d), [37] = MPF(4435ce), [45] = MPF(443e51), [46] = MPF(443fab),
    [50] = MPF(444346), [54] = MPF(44462f), [55] = MPF(44470a), [56] = MPF(4447ab), [57] = MPF(444860), [61] = MPF(444b07),
    [62] = MPF(444b27), [63] = MPF(444c66), [66] = MPF(444e3f), [67] = MPF(445063), [69] = MPF(445458), [71] = MPF(44562e),
    [72] = MPF(4457a1), [75] = MPF(445d97), [82] = MPF(4460f8), [83] = MPF(4461f4), [85] = MPF(44637e), [86] = MPF(4463d9),
    [87] = MPF(446683), [95] = MPF(446ba0), [96] = MPF(446bec),
    [38] = MPF(4436bb), [58] = MPF(444915),   /* mp-labor */
    [93] = MPF(4469bd), [94] = MPF(446ab8),   /* mp-skrpick */
    [16] = MPF(442a7f), [91] = MPF(44695f),   /* mp-desync */
};
static const mp_fn k_mp_recv[100] = {
    [1] = MPF(4410bd), [2] = MPF(4412cc), [3] = MPF(44164d), [4] = MPF(4419f9), [5] = MPF(441b86), [6] = MPF(441e1f),
    [7] = MPF(441f50), [8] = MPF(442014), [9] = MPF(4421c3), [10] = MPF(442361), [11] = MPF(44240a), [12] = MPF(4424d6),
    [14] = MPF(4427d1), [15] = MPF(442a25), [18] = MPF(442b08), [20] = MPF(442c01), [21] = MPF(442c82), [22] = MPF(442cd6),
    [30] = MPF(443286), [34] = MPF(44341b), [35] = MPF(4434eb), [39] = MPF(4437aa), [40] = MPF(4438c3), [41] = MPF(443a2d),
    [42] = MPF(443b8d), [43] = MPF(443ca6), [44] = MPF(443dba), [47] = MPF(444100), [48] = MPF(44419c), [49] = MPF(444280),
    [51] = MPF(444415), [59] = MPF(4449f1), [60] = MPF(444a9b), [64] = MPF(444d68), [73] = MPF(445960), [74] = MPF(445d23),
    [76] = MPF(445ea7), [77] = MPF(445f80), [84] = MPF(4462df),
    /* partition mpmsg3 */
    [13] = MPF(442579), [17] = MPF(442ae8), [23] = MPF(442d60), [24] = MPF(442e0d), [25] = MPF(442eec), [28] = MPF(443083),
    [29] = MPF(443143), [33] = MPF(44338b), [36] = MPF(443587), [37] = MPF(44363b), [45] = MPF(443f09), [46] = MPF(44400f),
    [50] = MPF(4443aa), [54] = MPF(4446a6), [55] = MPF(44475c), [56] = MPF(44480f), [57] = MPF(4448c4), [61] = MPF(444b17),
    [62] = MPF(444bd0), [63] = MPF(444cb5), [66] = MPF(444f96), [67] = MPF(4451cc), [69] = MPF(4454a9), [71] = MPF(4456c1),
    [72] = MPF(445851), [75] = MPF(445e0e), [82] = MPF(446171), [83] = MPF(44622c), [85] = MPF(446389), [86] = MPF(4464e4),
    [87] = MPF(44678d), [95] = MPF(446bba), [96] = MPF(446c1f),
    [38] = MPF(4436f9), [58] = MPF(444948),   /* mp-labor */
    [93] = MPF(4469ec), [94] = MPF(446b0f),   /* mp-skrpick */
    [16] = MPF(442aae), [91] = MPF(446983),   /* mp-desync */
};
extern int32_t g_rl_4d57f0[100];   /* rl_glue: retail's builder table */

/* message `id`'s payload builder (rl_43eda0 calls it with DAT_00512f8c at the ring slot's payload) */
int rl_mpmsg_build(int id)
{
    uint8_t k = (uint8_t)id;
    if (k < 100 && k_mp_build[k])
        return k_mp_build[k]();
    return (int)engine_retail_call((uint32_t)AT(int32_t, g_rl_4d57f0, 0x4d57f0u, 0x4d57f0 + 4 * k), 0);
}

/* message `id`'s receiver (DAT_00515918 at the payload) */
int rl_mpmsg_recv(int id)
{
    uint8_t k = (uint8_t)id;
    if (k < 100 && k_mp_recv[k])
        return k_mp_recv[k]();
    return (int)engine_retail_call((uint32_t)AT(int32_t, g_rl_4d5980, 0x4d5980u, 0x4d5980 + 4 * k), 0);
}

/* Port of FUN_0043eac1 (735 B) — the receive dispatcher: the message in DAT_00515c50 ({id, size, checksum, seq | flags},
 * payload) from DPID `from`. DAT_0053e988 counts calls, DAT_00515878 = seq (low 6 bits). A message whose (sender slot,
 * seq) resend countdown 0x515b30 still runs is a duplicate: dropped. Else every countdown of that slot ticks down,
 * the copy flags (0x40: 2nd copy, 0x80: 3rd) raise DAT_0057c8f4 and count DAT_005656ec; an undefined id, a size
 * other than the id's (0x4d5b90), a bad checksum (0x440038) or a forced resend (DAT_0055ce68, counted down) clears the
 * countdown, counts DAT_0055326c and logs (0x4afab9); a good one sets the countdown 10 (slot 6: 0), DAT_00515b28 =
 * from, DAT_00515918 = the payload, calls the id's receiver (table 0x4d5980) and counts DAT_0053f060. No caller
 * reads EAX. */
int rl_43eac1(int from)
{
    g_rl_53e988++;
    int seq = RCV(3) & 0x3f;
    int flags = RCV(3) & 0xc0;
    g_rl_515878 = seq;
    int p = rl_43e98b(from);
    if (p != 0 && RSND(p, seq) != 0)
        return seq;
    for (int i = 0; i < 40; i++)
        if (RSND(p, i) != 0)
            RSND(p, i)--;
    if (flags != 0) {
        if (flags == 0x40 && g_rl_57c8f4 < 1)
            g_rl_57c8f4 = 1;
        else if (flags == 0x80 && g_rl_57c8f4 < 2)
            g_rl_57c8f4 = 2;
        g_rl_5656ec++;
    }
    uint8_t size = g_rl_4d5b90[RCV(0)];
    if (size == 255) {
        RSND(p, seq) = 0;
        g_rl_55326c++;
        return rl_4afab9(STR(0x4d5ea4), 0, from);   /* "ERR:Corrupt packet from " */
    }
    if (size != RCV(1)) {
        RSND(p, seq) = 0;
        g_rl_55326c++;
        return rl_4afab9(STR(0x4d5ec0), 0, from);   /* "ERR:Wrong size packet from " */
    }
    if (RCV(2) != (uint8_t)rl_440038(size, PTR(g_rl_515c50))) {
        RSND(p, seq) = 0;
        g_rl_55326c++;
        return rl_4afab9(STR(0x4d5edc), 0, from);   /* "ERR:Bad packet CRC from " */
    }
    if (g_rl_55ce68 != 0) {
        g_rl_55ce68--;
        RSND(p, seq) = 0;
        g_rl_55326c++;
        return rl_4afab9(STR(0x4d5ef8), 0, from);   /* "ERR:Resend Forced from " */
    }
    RSND(p, seq) = 10;
    if (p == 6)
        RSND(p, seq) = 0;
    g_rl_515b28 = from;
    g_rl_515918 = PTR(g_rl_515c50 + 4);
    int r = rl_mpmsg_recv(RCV(0));
    g_rl_53f060++;
    return r;
}

/* ================================================================ MP setup / state helpers */

/* Port of FUN_0043f01d (558 B) — reset the MP session state at the start of a game: the .rec replay buffer (0x446f06)
 * and 0x446f84, the send ring's slot, the turn / sync counters and flags, the chat lines (0x43f24b), the resend
 * buffers (0x43ff63), the hash sums (0x43ff08), our hash region (0x43ffc3), the peer ticks (0x43f304); DAT_0053e998 =
 * GetTickCount. Returns 0. */
int rl_43f01d(void)
{
    rl_446f06();
    rl_446f84();
    g_rl_512f4c = 1;
    g_rl_554038 = 0;
    g_rl_553ec8 = 0;
    g_rl_57cb3c = 0;
    g_rl_542cbc = 0;
    g_rl_553ff4 = 1;
    g_combat_render_dirty = 0;
    g_bf_553c6c = 0;
    g_rl_553214 = 0;
    g_bf_57c998[0] = 0;
    g_rl_567d04 = 0;
    g_rl_53e988 = 0;
    g_rl_569580 = 0;
    g_rl_55326c = 0;
    g_rl_53f060 = 0;
    g_rl_57c8f4 = 0;
    g_rl_5656ec = 0;
    g_rl_553f4c[0] = 0;
    g_rl_56d690[0] = 0;
    g_rl_553f7c = 0;
    g_rl_553260 = 0;
    g_rl_4d2164 = 0;
    g_tr_p4_flash_timer = 0;
    for (int i = 0; i < 10; i++)
        g_combat_anim_scratch[i] = 0;
    g_bf_55403c = 0;
    g_rl_568970 = 0;
    g_rl_567964 = 0;
    g_rl_53f05c = 0;
    g_rl_512fa8 = 0;
    g_rl_515914 = 0;
    g_rl_515b20 = 0;
    g_rl_515908 = 0;
    rl_43f24b();
    g_rl_515880 = 0;
    g_rl_5158b8 = 0;
    g_rl_53e998 = (int32_t)rl_plat_GetTickCount();
    g_rl_52eff8 = 0;
    g_rl_55cd38 = 0;
    g_rl_553fac = 0;
    g_rl_55ce88 = 0;
    g_rl_512f4c = 0;
    rl_43ff63();
    rl_43ff08();
    rl_43ffc3();
    bf_43f304();
    g_rl_568960 = 0;
    g_rl_568958 = 0;
    g_rl_5694f4 = 0;
    g_rl_5694f0 = 0;
    g_rl_569508 = 0;
    g_bf_5694fc = 0;
    g_bf_569514 = 0;
    g_bf_569510 = 0;
    return 0;
}

/* Port of FUN_0043f4b7 (270 B) — restore the battle from the current snapshot buffer (DAT_005158d0 / d4 by the
 * selector DAT_00512f60[mode]; its checksum slot DAT_00512f90 / DAT_0050d7b0[mode] zeroed): on the battlefield the
 * units 1.. (0x8700 B at 0x554630), the combat units 1.. (0x1db0 at 0x57a14c), the groups 1.. (0x1040 at 0x566554),
 * DAT_0058fe28, DAT_0058fe08 copied from it (0x4b3d5d). No caller reads EAX. */
int rl_43f4b7(void)
{
    int buf;
    if (SNAPSEL(MODE) == 1) {
        buf = SNAPBUF(MODE, 0);
        SNAPSUM1(MODE) = 0;
    } else {
        buf = SNAPBUF(MODE, 1);
        SNAPSUM2(MODE) = 0;
    }
    if (buf == 0 || MODE != 2)
        return MODE;
    rl_4b3d5d(PTR(&AT(uint8_t, g_per_battle_hex_432, 0x554480u, 0x554630u)), buf, 0x8700);
    buf += 0x8700;
    rl_4b3d5d(PTR(&AT(uint8_t, g_per_combat_unit_76, 0x57a100u, 0x57a14cu)), buf, 0x1db0);
    buf += 0x1db0;
    rl_4b3d5d(PTR(&AT(uint8_t, g_per_battle_hex_aux_52, 0x566520u, 0x566554u)), buf, 0x1040);
    buf += 0x1040;
    rl_4b3d5d(PTR(&s_slot_0058fe28), buf, 4);
    return rl_4b3d5d(PTR(&s_slot_0058fe08), buf + 4, 4);
}

/* Port of FUN_0043f8ff (58 B) — clear the per-player "ready" flags DAT_00515850[0..9]. */
int rl_43f8ff(void)
{
    for (int i = 0; i < 10; i++)
        g_rl_515850[i] = 0;
    return 10;
}

/* Port of FUN_0043fd2a (413 B) — MP: flip the battle snapshot selectors (DAT_00512f60[mode] 1 ↔ 2, DAT_00512f40[mode]
 * the other) and sum the bytes of the buffer just selected (DAT_005158d0 when 0x512f60 was 1, else d4) over the layout
 * rows 0x4d5b10 (strategic rows 1..3, battle rows 4..6: {count, end, start}; per record j = 1..count-1 the bytes
 * start..end-1 of each successive stride); the sum goes to DAT_00512f90[mode] (0x512f40 == 1) or DAT_0050d7b0[mode].
 * __usercall: retail's EAX in comes back out when MP is off; no caller reads it. */
int rl_43fd2a(void)
{
    if (!MP_ON)
        return 0;
    int buf;
    if (SNAPSEL(MODE) == 1) {
        SNAPSEL(MODE) = 2;
        SNAPSEL2(MODE) = 1;
        buf = SNAPBUF(MODE, 0);
    } else {
        SNAPSEL(MODE) = 1;
        SNAPSEL2(MODE) = 2;
        buf = SNAPBUF(MODE, 1);
    }
    if (buf == 0)
        return 0;
    const uint8_t *q = (const uint8_t *)(uintptr_t)buf;
    int sum = 0;
    for (int i = 1; i < 4; i++) {
        int r = MODE != 0 ? i + 3 : i;
        for (int j = 1; LAYOUT(r, 0) > j; j++) {
            q += LAYOUT(r, 2);
            for (int k = LAYOUT(r, 2); LAYOUT(r, 1) > k; k++)
                sum += *q++;
        }
    }
    if (SNAPSEL2(MODE) == 1)
        SNAPSUM1(MODE) = sum;
    else
        SNAPSUM2(MODE) = sum;
    return sum;
}

/* Port of FUN_0043fed2 (54 B) — clear the players' "joined" bytes DAT_005530e0[1..5]. */
int rl_43fed2(void)
{
    for (int i = 1; i < 6; i++)
        g_rl_5530e0[i] = 0;
    return 6;
}

/* ================================================================ checksums, the payload copies, the state hash */

/* Port of FUN_00440038 (81 B) — the message checksum: the byte sum (mod 256) of the `n` payload bytes after the 4-byte
 * header of `msg`. EAX: the sum in AL over the loop counter's final value (0, or n when n ≤ 0). */
int rl_440038(int n, int msg)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)msg;
    uint8_t s = 0;
    int c = n;
    for (int i = 4; c > 0; c--, i++)
        s = (uint8_t)(s + m[i]);
    return (int)(((uint32_t)c & 0xffffff00u) | s);
}

/* Port of FUN_0044015a (96 B) — the byte sum (mod 256) of rec[start .. len-1]. EAX: the sum in AL over the counter's
 * final value (0, or len - start when that is ≤ 0). */
int rl_44015a(int rec, int len, int start)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)rec;
    uint8_t s = 0;
    int c = len - start;
    for (int i = start; c > 0; c--, i++)
        s = (uint8_t)(s + m[i]);
    return (int)(((uint32_t)c & 0xffffff00u) | s);
}

/* Port of FUN_004401ba (60 B) — append `n` bytes from `src` to the payload being built (DAT_00512f8c advances).
 * EAX: the counter's final value (0, or n when n ≤ 0). */
int rl_4401ba(int src, int n)
{
    const uint8_t *s = (const uint8_t *)(uintptr_t)src;
    for (; n > 0; n--) {
        *(uint8_t *)(uintptr_t)g_rl_512f8c = *s++;
        g_rl_512f8c++;
    }
    return n;
}

/* Port of FUN_004401f6 (59 B) — take `n` bytes of the received payload (DAT_00515918 advances) into `dst`.
 * EAX: the counter's final value (0, or n when n ≤ 0). */
int rl_4401f6(int dst, int n)
{
    uint8_t *d = (uint8_t *)(uintptr_t)dst;
    for (; n > 0; n--) {
        *d++ = *(const uint8_t *)(uintptr_t)g_rl_515918;
        g_rl_515918++;
    }
    return n;
}

/* Port of FUN_00440231 (1077 B) — our per-player state hash (sent with messages 8 / 12): our hash region cleared
 * (0x43ffc3) and our sums DAT_00569530[local][0..9]; strategic (mode 0): per county 1..16 (0x53f9b0, 768 B from +5),
 * realm 1..5 (0x57bf00, 352 B from +6), territory 1..150 (0x52f0b0, 420 B) the record's byte sum (0x44015a) into the
 * region (+0 / +200 / +400 + index) and the class sum (sums [1] / [2] / [3]); battlefield (mode 2): units 1..80 (432 B
 * from +18), combat units 1..100 (76 B from +4), groups 1..80 (52 B) into sums [5] / [6] / [7]. Then sums[7] = 1,
 * sums[0] = the byte sum of sums[1..8], sums[9] = (DAT_0057c998 & 0x7f) + 1; on the battlefield the snapshot restore
 * 0x43f4b7. No caller reads EAX. */
int rl_440231(void)
{
    rl_43ffc3();
    for (int i = 0; i < 10; i++)
        HSUM(LOCAL, i) = 0;
    if (MODE == 0) {
        for (int n = 1; n < 17; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&AT(uint8_t, g_per_army_768, 0x53f9b0u, 0x53f9b0 + 768 * n)), 768, 5);
            HREG(0x565700, LOCAL, n) = v;
            HSUM(LOCAL, 1) = (uint8_t)(HSUM(LOCAL, 1) + v);
        }
        for (int n = 1; n < 6; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&PL8(n, 0x57bf00)), 352, 6);
            HREG(0x5657c8, LOCAL, n) = v;
            HSUM(LOCAL, 2) = (uint8_t)(HSUM(LOCAL, 2) + v);
        }
        for (int n = 1; n < 151; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b0 + 420 * n)), 420, 0);
            HREG(0x565890, LOCAL, n) = v;
            HSUM(LOCAL, 3) = (uint8_t)(HSUM(LOCAL, 3) + v);
        }
    } else if (MODE == 2) {
        for (int n = 1; n < 81; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&AT(uint8_t, g_per_battle_hex_432, 0x554480u, 0x554480 + 432 * n)), 432, 18);
            HREG(0x565700, LOCAL, n) = v;
            HSUM(LOCAL, 5) = (uint8_t)(HSUM(LOCAL, 5) + v);
        }
        for (int n = 1; n < 101; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&AT(uint8_t, g_per_combat_unit_76, 0x57a100u, 0x57a100 + 76 * n)), 76, 4);
            HREG(0x5657c8, LOCAL, n) = v;
            HSUM(LOCAL, 6) = (uint8_t)(HSUM(LOCAL, 6) + v);
        }
        for (int n = 1; n < 81; n++) {
            uint8_t v = (uint8_t)rl_44015a(PTR(&AT(uint8_t, g_per_battle_hex_aux_52, 0x566520u, 0x566520 + 52 * n)), 52, 0);
            HREG(0x565890, LOCAL, n) = v;
            HSUM(LOCAL, 7) = (uint8_t)(HSUM(LOCAL, 7) + v);
        }
    }
    HSUM(LOCAL, 7) = 1;
    HSUM(LOCAL, 0) = 0;
    for (int k = 1; k < 9; k++)
        HSUM(LOCAL, 0) = (uint8_t)(HSUM(LOCAL, 0) + HSUM(LOCAL, k));
    int r = (g_bf_57c998[0] & 0x7f) + 1;
    HSUM(LOCAL, 9) = (uint8_t)r;
    if (MODE == 2)
        return rl_43f4b7();
    return r;
}

/* Port of FUN_004407e5 (426 B) — the turn-end sync check (0x49b8e6): DAT_00553fa8 / DAT_0053e8a8 / the mismatch
 * flags DAT_0053f090[0..9] cleared; single player 1. MP: 0 while a human realm 1..5 has not reported (DAT_00515d60[p]);
 * then DAT_0052afcc = DAT_00553214 = 0 and every human realm's hash sum [0] against ours: all equal → the ready flags
 * cleared (0x43f8ff), DAT_0057cb3c counted, 1; any differs → DAT_00554038 counted, 2. */
int rl_4407e5(void)
{
    int differ = 0;
    g_rl_553fa8 = 0;
    g_rl_53e8a8 = 0;
    for (int i = 0; i < 10; i++)
        g_rl_53f090[i] = 0;
    if (!MP_ON)
        return 1;
    for (int j = 1; j < 6; j++)
        if (PL8(j, 0x57bf05) != 0 && PL8(j, 0x57bf04) != 0 && g_combat_anim_scratch[j] == 0)
            return 0;
    g_combat_render_dirty = 0;
    g_rl_553214 = 0;
    int ours = HSUM(LOCAL, 0);
    for (int k = 1; k < 6; k++)
        if (PL8(k, 0x57bf05) != 0 && PL8(k, 0x57bf04) != 0 && HSUM(k, 0) != ours)
            differ = 1;
    if (differ != 0) {
        g_rl_554038++;
        return 2;
    }
    rl_43f8ff();
    g_rl_57cb3c++;
    return 1;
}

/* 0x440dd2's uninitialized local [ebp-0xc] (the reference sum byte; read before any write on the strategic path):
 * the hook loads it from the residue (e0 - 0x10) and puts the final dword back */
uint32_t rl_440dd2_res;

/* Port of FUN_00440dd2 (638 B) — compare hash sum `k` across the players (0x440666 with k = 0): first the turn bytes
 * [9] of the live human players on the battle's sides (any 0 or differing: 2); then on the battlefield the sums [k]
 * of those players — the first is the reference; on the strategic map the sums [k] of every live human player
 * against the reference byte retail never sets on that path (its stack residue). A mismatch sets DAT_0053f090[k] and
 * returns 0; all equal 1. */
int rl_440dd2(int k)
{
    uint8_t ref9 = 0, refk = (uint8_t)rl_440dd2_res;
    int have = 0;
    for (int p = 1; p < 6; p++) {
        if (PL8(p, 0x57bf05) == 0 || PL8(p, 0x57bf04) == 0 || !ALLY(p))
            continue;
        if (have == 0) {
            ref9 = HSUM(p, 9);
            have = 1;
        } else {
            if (HSUM(p, 9) == 0)
                return 2;
            if (HSUM(p, 9) != ref9)
                return 2;
        }
    }
    if (MODE == 2) {
        have = 0;
        for (int p = 1; p < 6; p++) {
            if (PL8(p, 0x57bf05) == 0 || PL8(p, 0x57bf04) == 0 || !ALLY(p))
                continue;
            if (have == 0) {
                refk = HSUM(p, k);
                rl_440dd2_res = (rl_440dd2_res & 0xffffff00u) | refk;
                have = 1;
            } else if (HSUM(p, k) != refk) {
                g_rl_53f090[k] = 1;
                return 0;
            }
        }
    } else {
        for (int p = 1; p < 6; p++) {
            if (PL8(p, 0x57bf05) != 0 && PL8(p, 0x57bf04) != 0 && HSUM(p, k) != refk) {
                g_rl_53f090[k] = 1;
                return 0;
            }
        }
    }
    return 1;
}

/* ================================================================ payload builders / receivers, ids 1..22 */

/* Port of FUN_00441066 (87 B) — builder 1 (join request): our DPID DAT_004e2a28, our name DAT_0053f1e0 (32 B), our
 * shield DAT_005c9a74, our machine score DAT_004d2168 + DAT_004d216c. */
int rl_441066(void)
{
    int32_t score = g_rl_4d2168 + g_rl_4d216c;
    rl_4401ba(PTR(g_rl_4e2a28), 4);
    rl_4401ba(PTR(g_bf_53f1e0), 32);
    rl_4401ba(PTR(&g_rl_5c9a74), 4);
    return rl_4401ba(PTR(&score), 4);
}

/* Port of FUN_004410bd (349 B) — receiver 1 (a player joins): DPID, name, shield, score. Ignored in game
 * (DAT_0052af94 3). Shield 1 or 2 sets DAT_005bb4a0. The session creator (DAT_00553248 1) with more than 2 players
 * and DAT_005bb4a0 clear refuses (message 81 to the DPID); otherwise the player count grows, the newcomer's slot
 * DAT_0056d5cc = it, its score DAT_00553120[slot], ours refreshed, its resend countdown 10, seated (0x49bae9), the
 * roster rebuilt (0x49caaa); the creator then sends 2 (to it), 76, 9, 3. */
int rl_4410bd(void)
{
    int32_t dpid, shield, score;
    uint8_t name[32];
    rl_4401f6(PTR(&dpid), 4);
    rl_4401f6(PTR(name), 32);
    rl_4401f6(PTR(&shield), 4);
    rl_4401f6(PTR(&score), 4);
    if (g_engine_state == 3)
        return 0;
    if (shield == 1 || shield == 2)
        g_rl_5bb4a0 = 1;
    if (g_rl_553248 != 1 || NPLAYERS <= 2 || g_rl_5bb4a0 == 1) {
        NPLAYERS++;
        s_slot_0056d5cc = (uint32_t)NPLAYERS;
        g_rl_553120[NPLAYERS] = score;
        g_rl_553120[LOCAL] = g_rl_4d2168 + g_rl_4d216c;
        RSND(OTHER, g_rl_515878) = 10;
        rl_49bae9(dpid, OTHER, PTR(name));
        rl_49caaa();
        if (g_rl_553248 == 1) {
            rl_43eda0(2, dpid);
            rl_43eda0(76, 0);
            rl_43eda0(9, 0);
            rl_43eda0(3, 0);
        }
    } else {
        rl_43eda0(81, dpid);
    }
    return 0;
}

/* Port of FUN_0044121a (178 B) — builder 2 (the creator's welcome): our DPID, name, our slot, the newcomer's slot
 * DAT_0056d5cc, the slow-link flag DAT_00553264, both scores, our joined byte; then seats us (0x49bae9). */
int rl_44121a(void)
{
    rl_4401ba(PTR(g_rl_4e2a28), 4);
    rl_4401ba(PTR(g_bf_53f1e0), 32);
    rl_4401ba(PTR(&s_slot_0057c8cc), 4);
    rl_4401ba(PTR(&s_slot_0056d5cc), 4);
    rl_4401ba(PTR(&g_rl_553264), 4);
    rl_4401ba(PTR(&g_rl_553120[LOCAL]), 4);
    rl_4401ba(PTR(&g_rl_553120[OTHER]), 4);
    rl_4401ba(PTR(&g_rl_5530e0[LOCAL]), 1);
    return rl_49bae9(*(int32_t *)g_rl_4e2a28, LOCAL, PTR(g_bf_53f1e0));
}

/* Port of FUN_004412cc (216 B) — receiver 2: the creator's DPID / name, its slot (DAT_0056d5cc), our slot
 * (DAT_0057c8cc), the slow-link flag, both scores, its joined byte; both seated (0x49bae9), a full repaint mark
 * (0x45240a), DAT_0057c978 = 1. */
int rl_4412cc(void)
{
    int32_t dpid;
    uint8_t name[32];
    rl_4401f6(PTR(&dpid), 4);
    rl_4401f6(PTR(name), 32);
    rl_4401f6(PTR(&s_slot_0056d5cc), 4);
    rl_4401f6(PTR(&s_slot_0057c8cc), 4);
    rl_4401f6(PTR(&g_rl_553264), 4);
    rl_4401f6(PTR(&g_rl_553120[OTHER]), 4);
    rl_4401f6(PTR(&g_rl_553120[LOCAL]), 4);
    rl_4401f6(PTR(&g_rl_5530e0[OTHER]), 1);
    rl_49bae9(dpid, OTHER, PTR(name));
    rl_49bae9(*(int32_t *)g_rl_4e2a28, LOCAL, PTR(g_bf_53f1e0));
    int r = rl_45240a();
    g_rl_57c978 = 1;
    return r;
}

/* the roster block of messages 3 / 14 (0x4413a4 / 0x4425bf and their receivers): the player count, realms 1..5's +0x0a
 * and +5, sessions 1..5's +0x25 and DPID, (3 only:) sessions 1..5's names (20 B), the setup choices DAT_0057cb40 (6) */
static void roster_send(int names)
{
    rl_4401ba(PTR(&s_slot_00553f98), 4);
    for (int p = 1; p < 6; p++) rl_4401ba(PTR(&PL8(p, 0x57bf0a)), 1);
    for (int p = 1; p < 6; p++) rl_4401ba(PTR(&PL8(p, 0x57bf05)), 1);
    for (int p = 1; p < 6; p++) rl_4401ba(AUXP(p, 0x553d75), 1);
    for (int p = 1; p < 6; p++) rl_4401ba(AUXP(p, 0x553d50), 4);
    for (int p = 1; names && p < 6; p++) rl_4401ba(AUXP(p, 0x553d54), 20);
    rl_4401ba(PTR(g_rl_57cb40), 6);
}
static void roster_recv(int names)
{
    rl_4401f6(PTR(&s_slot_00553f98), 4);
    for (int p = 1; p < 6; p++) rl_4401f6(PTR(&PL8(p, 0x57bf0a)), 1);
    for (int p = 1; p < 6; p++) rl_4401f6(PTR(&PL8(p, 0x57bf05)), 1);
    for (int p = 1; p < 6; p++) rl_4401f6(AUXP(p, 0x553d75), 1);
    for (int p = 1; p < 6; p++) rl_4401f6(AUXP(p, 0x553d50), 4);
    for (int p = 1; names && p < 6; p++) rl_4401f6(AUXP(p, 0x553d54), 20);
    rl_4401f6(PTR(g_rl_57cb40), 6);
}

/* Port of FUN_004413a4 (681 B) — builder 3 (the roster): the roster block + the joined bytes DAT_005530e0 (6); then
 * sessions 0..5's +0x14 cleared and DAT_005158f0[1..5] = sessions 1..5's DPIDs. */
int rl_4413a4(void)
{
    roster_send(1);
    rl_4401ba(PTR(g_rl_5530e0), 6);
    for (int i = 0; i < 6; i++)
        AUX8(i, 0x553d64) = 0;
    for (int j = 1; j < 6; j++)
        g_rl_5158f0[j] = AUX32(j, 0x553d50);
    return g_rl_5158f0[5];
}

/* Port of FUN_0044164d (696 B) — receiver 3: the roster block + joined bytes; sessions' +0x14 cleared, the DPID copies
 * DAT_005158f0, the roster rebuilt (0x49caaa), redraw 2. */
int rl_44164d(void)
{
    roster_recv(1);
    rl_4401f6(PTR(g_rl_5530e0), 6);
    for (int i = 0; i < 6; i++)
        AUX8(i, 0x553d64) = 0;
    for (int j = 1; j < 6; j++)
        g_rl_5158f0[j] = AUX32(j, 0x553d50);
    int r = rl_49caaa();
    engine_set_demand_redraw(2);
    return r;
}

/* the common tail of messages 4's builder / receiver (a new game starts) */
static int newgame_tail(void)
{
    if (engine_current_screen() != 41)
        engine_set_current_screen(0);
    g_engine_state = 3;
    rl_43e5c1();
    rl_451f8a();
    g_rl_5aeb8c = 0;
    s_slot_005440c8 = s_slot_0053f26c;
    g_combat_anim_deadline = rl_plat_timeGetTime();
    for (int i = 0; i < 65; i++)
        g_rl_553e80[i] = 0;
    for (int j = 0; j < 65; j++)
        g_rl_565440[j] = 0;
    return 65;
}

/* Port of FUN_00441905 (244 B) — builder 4 (start the game): DAT_0058fe28 / DAT_0058fe08 sent; the custom config
 * applied (0x499dc3), "OK :Start new game - preloaded = " logged with DAT_005aeb8c + 1, a fresh game set up (0x497ced)
 * unless preloaded; the screen 0 (unless 41), state 3, the handicap (0x43e5c1), a full repaint (0x451f8a),
 * DAT_005440c8 = DAT_0053f26c, DAT_00568d9c = timeGetTime, both chat input lines cleared. */
int rl_441905(void)
{
    rl_4401ba(PTR(&s_slot_0058fe28), 4);
    rl_4401ba(PTR(&s_slot_0058fe08), 4);
    rl_499dc3();
    rl_4afab9(OKSTR(0x4d5f8c), 0, g_rl_5aeb8c + 1);
    if (g_rl_5aeb8c == 0)
        rl_497ced();
    return newgame_tail();
}

/* Port of FUN_004419f9 (341 B) — receiver 4: DAT_0058fe28 / 08, the config applied (0x499dc3). Still on the title's
 * page 4 (DAT_005530f0): "OK :DP Oh dear - Game started without us !!", page 1, redraw 1, message 36 and the session
 * closed (0x4b743b). Else as the builder (preloaded: the system sheets 0 and 1 reloaded, 0x499a1c). */
int rl_4419f9(void)
{
    rl_4401f6(PTR(&s_slot_0058fe28), 4);
    rl_4401f6(PTR(&s_slot_0058fe08), 4);
    rl_499dc3();
    if (g_rl_5530f0 == 4) {
        rl_4afab9(OKSTR(0x4d5fb0), 0, 0);
        g_rl_5530f0 = 1;
        engine_set_demand_redraw(1);
        rl_43eda0(36, 0);
        return (int)engine_retail_call(0x4b743bu, 0);   /* TODO(rl-extern): 0x4b743b (close the MP session) */
    }
    rl_4afab9(OKSTR(0x4d5fe0), 0, g_rl_5aeb8c + 1);
    if (g_rl_5aeb8c != 0) {
        rl_499a1c(0);
        rl_499a1c(1);
    } else {
        rl_497ced();
    }
    return newgame_tail();
}

/* Port of FUN_00441b4e (56 B) — builder 5 (a shield choice): our slot, the chosen shield DAT_0059154c (the button
 * router's param0), our name (32 B). */
int rl_441b4e(void)
{
    rl_4401ba(PTR(&s_slot_0057c8cc), 4);
    rl_4401ba(PTR(g_bf_59154c), 4);
    return rl_4401ba(PTR(g_bf_53f1e0), 32);
}

/* Port of FUN_00441b86 (420 B) — receiver 5: the sender's slot (DAT_0056d5cc), its shield, its name (into its session
 * +4, 31 B). The creator checks the shield (0x49ca60: taken → message 17 back to DAT_00515b28); free: the realm's +0x0a
 * and session +0x25 = the shield, the slot's old choice freed in DAT_0057cb40[1..5] and the shield's taken, 0x433cc1,
 * the roster rebuilt, realm +5 = 2, messages 76, 9, 77 (to the sender), 3, redraw 2, its joined byte set. */
int rl_441b86(void)
{
    uint8_t name[32];
    int32_t shield;
    rl_4401f6(PTR(&s_slot_0056d5cc), 4);
    rl_4401f6(PTR(&shield), 4);
    rl_4401f6(PTR(name), 32);
    rl_401136(PTR(name), AUXP(OTHER, 0x553d54), 31);
    if (g_rl_553248 == 0)
        return 0;
    if (rl_49ca60(shield, OTHER) != 0)
        return rl_43eda0(17, g_rl_515b28);
    PL8(OTHER, 0x57bf0a) = (uint8_t)shield;
    AUX8(OTHER, 0x553d75) = PL8(OTHER, 0x57bf0a);
    for (int i = 1; i < 6; i++)
        if (g_rl_57cb40[i] == (uint32_t)OTHER)
            g_rl_57cb40[i] = 0;
    AT(uint8_t, g_rl_57cb40, 0x57cb40u, 0x57cb40 + shield) = (uint8_t)OTHER;
    rl_433cc1();
    rl_49caaa();
    PL8(OTHER, 0x57bf05) = 2;
    rl_43eda0(76, 0);
    rl_43eda0(9, 0);
    rl_43eda0(77, g_rl_515b28);
    rl_43eda0(3, 0);
    engine_set_demand_redraw(2);
    g_rl_5530e0[OTHER] = 1;
    return 0;
}

/* Port of FUN_00441d2a (245 B) — builder 6 (a chat line): our slot, the typed line DAT_00553e80 (65 B). On the MP
 * setup page (screen 31) the line also goes into our chat history (sender DAT_0053e880[DAT_0055cd80], text
 * 0x5651e0 + 65 × it via 0x401136; the slot wraps at 8) and DAT_00553fc4 = 1; the typed line is cleared and the text
 * editor reset (0x4020ae) either way. */
int rl_441d2a(void)
{
    rl_4401ba(PTR(&s_slot_0057c8cc), 4);
    rl_4401ba(PTR(g_rl_553e80), 65);
    if (engine_current_screen() == 31) {
        AT(int32_t, g_rl_53e880, 0x53e880u, 0x53e880 + 4 * g_rl_55cd80) = LOCAL;
        rl_401136(PTR(g_rl_553e80), PTR(&AT(uint8_t, g_rl_5651e0, 0x5651e0u, 0x5651e0 + 65 * g_rl_55cd80)), 65);
        for (int i = 0; i < 65; i++)
            g_rl_553e80[i] = 0;
        if (++g_rl_55cd80 >= 8)
            g_rl_55cd80 = 0;
        g_rl_553fc4 = 1;
        return rl_4020ae();
    }
    for (int j = 0; j < 65; j++)
        g_rl_553e80[j] = 0;
    return rl_4020ae();
}

/* Port of FUN_00441e1f (267 B) — receiver 6: ignored on screens 46, 47, 10, 23, 13; on the MP setup page (31) into the
 * chat history (the sender, the line; the slot wraps at 8; DAT_00553fc4 = 1); elsewhere the in-game chat line: the
 * sender into DAT_0055325c, the text into DAT_00565440 (65 B), shown for DAT_0057a0cc = 2000. */
int rl_441e1f(void)
{
    int s = engine_current_screen();
    if (s == 46 || s == 47 || s == 10 || s == 23 || s == 13)
        return s;
    if (s == 31) {
        rl_4401f6(PTR(&AT(int32_t, g_rl_53e880, 0x53e880u, 0x53e880 + 4 * g_rl_55cd80)), 4);
        int r = rl_4401f6(PTR(&AT(uint8_t, g_rl_5651e0, 0x5651e0u, 0x5651e0 + 65 * g_rl_55cd80)), 65);
        if (++g_rl_55cd80 >= 8)
            g_rl_55cd80 = 0;
        g_rl_553fc4 = 1;
        return r;
    }
    rl_4401f6(PTR(&g_rl_55325c), 4);
    int r = rl_4401f6(PTR(g_rl_565440), 65);
    g_rl_57a0cc = 2000;
    return r;
}

/* Port of FUN_00441f2a (38 B) — builder 7 (I have joined): our slot; our joined byte DAT_005530e0[local] = 1. */
int rl_441f2a(void)
{
    rl_4401ba(PTR(&s_slot_0057c8cc), 4);
    g_rl_5530e0[LOCAL] = 1;
    return LOCAL;
}

/* Port of FUN_00441f50 (38 B) — receiver 7: the sender's slot (DAT_0056d5cc); its joined byte = 1. */
int rl_441f50(void)
{
    rl_4401f6(PTR(&s_slot_0056d5cc), 4);
    g_rl_5530e0[OTHER] = 1;
    return OTHER;
}

/* the tick stamp of messages 8 / 10 / 12: DAT_0053e8d0[local] = DAT_0057c998 (our tick); its low 3 bytes and our slot
 * byte packed into DAT_00512f9c (0x401136) and sent */
static int tick_send(void)
{
    TICK(LOCAL) = g_bf_57c998[0];
    rl_401136(PTR(&g_bf_57c998[0]), PTR(g_rl_512f9c), 3);
    rl_401136(PTR(&s_slot_0057c8cc), PTR(&g_rl_512f9c[3]), 1);
    return rl_4401ba(PTR(g_rl_512f9c), 4);
}
/* … received: the 4 bytes into DAT_00512f9c, the tick's 3 bytes into DAT_00542cc0, the slot byte into DAT_0056d5cc
 * (low bytes only: the upper bytes keep what they held), DAT_0053e8d0[sender] = DAT_00542cc0 */
static void tick_recv(void)
{
    rl_4401f6(PTR(g_rl_512f9c), 4);
    rl_401136(PTR(g_rl_512f9c), PTR(&g_rl_542cc0), 3);
    rl_401136(PTR(&g_rl_512f9c[3]), PTR(&s_slot_0056d5cc), 1);
    TICK(OTHER) = g_rl_542cc0;
}

/* Port of FUN_00441f76 (158 B) — builder 8 (end of turn): our state hash (0x440231), the tick stamp, our 2-byte
 * DAT_00515960[local] and our 10 hash sums; DAT_00515d60[local] = 1 (reported). */
int rl_441f76(void)
{
    rl_440231();
    tick_send();
    rl_4401ba(PTR(&g_rl_515960[LOCAL]), 2);
    rl_4401ba(PTR(&HSUM(LOCAL, 0)), 10);
    g_combat_anim_scratch[LOCAL] = 1;
    return LOCAL;
}

/* Port of FUN_00442014 (153 B) — receiver 8: the tick stamp, the sender's DAT_00515960 (2 B) and hash sums (10 B);
 * DAT_00515d60[sender] = 1. */
int rl_442014(void)
{
    tick_recv();
    rl_4401f6(PTR(&g_rl_515960[OTHER]), 2);
    rl_4401f6(PTR(&HSUM(OTHER, 0)), 10);
    g_combat_anim_scratch[OTHER] = 1;
    return OTHER;
}

/* the game options of message 9: DAT_0053f29c, a4, 88, a0, 90, 98, b0, 94, a8, ac, 8c, b4, then the player count */
static const uint16_t k_opts9[12] = {
    0xf29c, 0xf2a4, 0xf288, 0xf2a0, 0xf290, 0xf298, 0xf2b0, 0xf294, 0xf2a8, 0xf2ac, 0xf28c, 0xf2b4,
};
#define OPT9(i) PTR(&AT(int32_t, &g_rl_53f288, 0x53f288u, 0x530000 + k_opts9[i]))

/* Port of FUN_004420ad (278 B) — builder 9 (the game options): the 12 option dwords 0x53f288.. and the player count. */
int rl_4420ad(void)
{
    for (int i = 0; i < 12; i++)
        rl_4401ba(OPT9(i), 4);
    return rl_4401ba(PTR(&s_slot_00553f98), 4);
}

/* Port of FUN_004421c3 (326 B) — receiver 9: the options and the player count; 0x433cc1; on the title's page 8:
 * DAT_0057c948 = DAT_0055ce94 = 1, redraw 2. */
int rl_4421c3(void)
{
    for (int i = 0; i < 12; i++)
        rl_4401f6(OPT9(i), 4);
    rl_4401f6(PTR(&s_slot_00553f98), 4);
    int r = rl_433cc1();
    if (g_rl_5530f0 == 8) {
        g_rl_57c948 = 1;
        g_rl_55ce94 = 1;
        engine_set_demand_redraw(2);
    }
    return r;
}

/* Port of FUN_00442309 (88 B) — builder 10 (a tick): the tick stamp. */
int rl_442309(void)
{
    return tick_send();
}

/* Port of FUN_00442361 (88 B) — receiver 10: the tick stamp. */
int rl_442361(void)
{
    tick_recv();
    return g_rl_542cc0;
}

/* Port of FUN_004423b9 (81 B) — builder 11: DAT_00553e70 = 0x446e21(DAT_00544094) sent, then 0x447136(0, it, 0 …). */
int rl_4423b9(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094);
    rl_4401ba(PTR(&g_rl_553e70), 4);
    return rl_447136(0, g_rl_553e70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_0044240a (62 B) — receiver 11: DAT_00542cc0 received, 0x447136(0, it, 0 …). */
int rl_44240a(void)
{
    rl_4401f6(PTR(&g_rl_542cc0), 4);
    return rl_447136(0, g_rl_542cc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442448 (142 B) — builder 12 (the hash, mid-turn): as builder 8 without the reported flag. */
int rl_442448(void)
{
    rl_440231();
    tick_send();
    rl_4401ba(PTR(&g_rl_515960[LOCAL]), 2);
    return rl_4401ba(PTR(&HSUM(LOCAL, 0)), 10);
}

/* Port of FUN_004424d6 (137 B) — receiver 12: as receiver 8 without the reported flag. */
int rl_4424d6(void)
{
    tick_recv();
    rl_4401f6(PTR(&g_rl_515960[OTHER]), 2);
    return rl_4401f6(PTR(&HSUM(OTHER, 0)), 10);
}

/* Port of FUN_004425bf (530 B) — builder 14 (the roster without the names, 51 B): the roster block + joined bytes; DAT_005158f0[1..5]
 * = the DPIDs (the sessions' +0x14 are not cleared here). */
int rl_4425bf(void)
{
    roster_send(0);
    rl_4401ba(PTR(g_rl_5530e0), 6);
    for (int i = 1; i < 6; i++)
        g_rl_5158f0[i] = AUX32(i, 0x553d50);
    return g_rl_5158f0[5];
}

/* Port of FUN_004427d1 (545 B) — receiver 14: the roster block + joined bytes; the DPID copies, the roster rebuilt
 * (0x49caaa), redraw 2. */
int rl_4427d1(void)
{
    roster_recv(0);
    rl_4401f6(PTR(g_rl_5530e0), 6);
    for (int i = 1; i < 6; i++)
        g_rl_5158f0[i] = AUX32(i, 0x553d50);
    int r = rl_49caaa();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_004429f2 (51 B) — builder 15 (who I am): our slot and DPID; DAT_0056d6a0 = 1. */
int rl_4429f2(void)
{
    rl_4401ba(PTR(&s_slot_0057c8cc), 4);
    int r = rl_4401ba(PTR(g_rl_4e2a28), 4);
    engine_army_ai_set_ranking_dirty(1);
    return r;
}

/* Port of FUN_00442a25 (90 B) — receiver 15: the sender's slot (DAT_0056d5cc) and its DPID into its session +0; its
 * realm human (+4 = 1); DAT_0056d6a0 = 1. */
int rl_442a25(void)
{
    rl_4401f6(PTR(&s_slot_0056d5cc), 4);
    rl_4401f6(AUXP(OTHER, 0x553d50), 4);
    PL8(OTHER, 0x57bf04) = 1;
    engine_army_ai_set_ranking_dirty(1);
    return 352 * OTHER;
}

/* Port of FUN_00442afd (11 B) — builder 18 (reload the old turn): no payload. */
int rl_442afd(void)
{
    return 0;
}

/* Port of FUN_00442b08 (16 B) — receiver 18: the F8 old-turn reload (0x43f7e3). */
int rl_442b08(void)
{
    return rl_43f7e3();
}

/* Port of FUN_00442bad (84 B) — builder 20: DAT_00553e70 = 0x446e21(DAT_00544094) + 50 sent, 0x447136(7, it, 0 …). */
int rl_442bad(void)
{
    g_rl_553e70 = rl_446e21(g_rl_544094) + 50;
    rl_4401ba(PTR(&g_rl_553e70), 4);
    return rl_447136(7, g_rl_553e70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442c01 (62 B) — receiver 20: DAT_00542cc0 received, 0x447136(7, it, 0 …). */
int rl_442c01(void)
{
    rl_4401f6(PTR(&g_rl_542cc0), 4);
    return rl_447136(7, g_rl_542cc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

/* Port of FUN_00442c3f (67 B) — builder 21 (toggle DAT_0053f238): DAT_0053f238 = ~itself, sent; the below-status
 * redraw DAT_0053e9ac = 2, the whole screen dirty (0x45240a), DAT_00553ed8 = 1. */
int rl_442c3f(void)
{
    g_bf_53f238 = ~g_bf_53f238;
    rl_4401ba(PTR(&g_bf_53f238), 4);
    engine_territory_dirty_state()->below_status_redraw = 2;
    int r = rl_45240a();
    g_bf_553ed8 = 1;
    return r;
}

/* Port of FUN_00442c82 (55 B) — receiver 21: DAT_0053f238 received; the same redraw marks. */
int rl_442c82(void)
{
    rl_4401f6(PTR(&g_bf_53f238), 4);
    engine_territory_dirty_state()->below_status_redraw = 2;
    int r = rl_45240a();
    g_bf_553ed8 = 1;
    return r;
}

/* Port of FUN_00442cb9 (29 B) — builder 22: the byte DAT_0059151c. */
int rl_442cb9(void)
{
    return rl_4401ba(PTR(g_bf_59151c), 1);
}

/* Port of FUN_00442cd6 (42 B) — receiver 22: one byte → 0x49b973(it). */
int rl_442cd6(void)
{
    uint8_t b[4];
    rl_4401f6(PTR(b), 1);
    return rl_49b973(b[0]);
}
