/*
 * OpenLords2 — per-realm random-event onset pass.  See engine_realm_events.h.
 *
 * Verbatim port of the turn-START event roll FUN_00448819 @ 0x448819 and its
 * 24 per-event handlers (ids 0x87..0x8e and 0x12e..0x13d) plus the two
 * territory-probe helpers (FUN_0044c77a / FUN_0044c80a).  Same field widths,
 * same signed-byte arithmetic, same per-mode magnitude tables, and the same
 * commit/cancel structure as the binary, so the resulting save-state bytes
 * match retail exactly.
 */

#include <stdint.h>

#include "engine_realm_events.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352,
                                      g_overworld_map, g_misc_1360 */
#include "save_schema_globals.h"   /* s_slot_0056d5dc, s_slot_00553edc,
                                      s_slot_0057c934 */
#include "engine_territory.h"      /* engine_territory_set_cell_type (0x46d7f4) */

/* --- per-realm army record (g_per_army_768, stride 0x300) ---------------
 * Field offsets named by the role this pass uses them in.  Bytes unless
 * marked i32/i16.  r2-verified against the 0x300 stride. */
#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])

#define A_EV_ACTIVE  0x00   /* event-active flag (u8)                      */
#define A_OWNER      0x05   /* owning player id  (u8)                      */
#define A_STAT_0B    0x0b   /* signed morale stat (i8)                     */
#define A_STAT_0C    0x0c   /* signed approval stat (i8)                   */
#define A_POP        0x24   /* population (i32)                            */
#define A_STAT_C0    0xc0   /* signed-gated stat (i32)                     */
#define A_SCR_1A8    0x1a8  /* turn-scratch byte (u8)                      */
#define A_EV_ID      0x1aa  /* current event id (i16)                      */
#define A_SCR_1FB    0x1fb  /* turn-scratch colour byte (u8)              */
#define A_SCR_1FC    0x1fc  /* turn-scratch colour byte (u8)              */
#define A_SCR_1FD    0x1fd  /* turn-scratch colour byte (u8)              */
#define A_STAT_224   0x224  /* gated stat (i32)                            */
#define A_STAT_250   0x250  /* gated stat (i32)                            */

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) -
 * Indexed by the realm's OWNER byte.  The resource cells the events
 * credit/debit overlap the cashflow accounts the ranking snapshot tracks. */
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])

#define S_EVENTS_ON  0x05   /* per-owner events-enabled gate (u8)         */
#define S_RES_118    0x118  /* resource account (i32)                     */
#define S_RES_128    0x128  /* resource account (i32)                     */
#define S_RES_130    0x130  /* resource account (i32)                     */
#define S_CAT_140    0x140  /* first of the cashflow category cells (i32) */

/* --- per-realm territory roster (g_misc_1360, retail 0x53ea00) ----------
 * 0x50-byte rows of 20 i32 overworld-cell byte-offsets, indexed by realm. */
#define ROSTER_ROW     0x50
#define ROSTER_COLS    20

static int32_t *roster_row(int realm)
{
    return (int32_t *)&g_misc_1360[realm * ROSTER_ROW];
}

static int32_t  army_i32(uint8_t *a, int off) { return *(int32_t *)(a + off); }
static int32_t  strat_i32(uint8_t *s, int off) { return *(int32_t *)(s + off); }
static void     strat_set_i32(uint8_t *s, int off, int32_t v) { *(int32_t *)(s + off) = v; }

/* The 256-entry event "deck" (retail const table DAT_004d6108).  Mostly
 * zero; 26 slots hold an event id.  Baked verbatim from Lords2.exe @0x4d6108
 * (r2 `p8 512`).  The cursor seeded by the RNG walks this table one slot per
 * realm; a non-zero slot is the event rolled for that realm. */
static const uint16_t EVENT_DECK[256] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0087, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008d,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0089,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008e,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x012e, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0138,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x012f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013a,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0130, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0088,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0131, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x008a,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0133, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0134, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0135,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0136, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0137,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0139, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013c,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0132, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013d,
};

/* Reset the event so it does not fire: clear the id and the active flag. */
static void cancel_event(uint8_t *a)
{
    *(int16_t *)(a + A_EV_ID) = 0;
    a[A_EV_ACTIVE] = 0;
}

/* Probe the realm's territory roster for the first cell whose overworld type
 * (g_overworld_map[+0]) is 0 and claim it (type 1).  Port of FUN_0044c77a —
 * returns 1 on a hit, 0 if no eligible cell.  The flip routes through the
 * territory module's set-cell-type (which no-ops when the cell grid is
 * unbound); the eligibility read is on g_overworld_map directly, as retail. */
int engine_realm_events_claim_empty(int realm)
{
    int32_t *row = roster_row(realm);
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t cell = row[col];
        if (cell != 0 && g_overworld_map[cell] == 0) {
            engine_territory_set_cell_type(cell, 1, 0);
            return 1;
        }
    }
    return 0;
}

/* Probe the roster for the first cell whose type is in the band (2, 0xf) and
 * vacate it (type 0).  Port of FUN_0044c80a — returns 1 on a hit, 0 if none. */
int engine_realm_events_vacate_owned(int realm)
{
    int32_t *row = roster_row(realm);
    for (int col = 0; col < ROSTER_COLS; col++) {
        int32_t cell = row[col];
        if (cell != 0 && (uint8_t)g_overworld_map[cell] > 2 &&
            (uint8_t)g_overworld_map[cell] < 0xf) {
            engine_territory_set_cell_type(cell, 0, 0);
            return 1;
        }
    }
    return 0;
}

/* ─── per-event handlers ─────────────────────────────────────────────────
 * Each takes the realm key; derives the army record, owner and (where used)
 * the owner's strategic record.  `mode` is the turn's event-mode
 * (s_slot_0057c934, 1..4).  Commit or cancel exactly as retail. */

static void ev_87(int r) {  /* FUN_00448e12 */
    uint8_t *a = ARMY_REC(r); int mode = (int)s_slot_0057c934;
    if (army_i32(a, A_STAT_224) < 0x32) { cancel_event(a); return; }
    if      (mode == 4) a[A_SCR_1FC] = 0xd8;
    else if (mode == 1) a[A_SCR_1FC] = 0xe2;
    else if (mode == 2) a[A_SCR_1FC] = 0xe7;
    else if (mode == 3) a[A_SCR_1FC] = 0xd3;
}

static void ev_88(int r) {  /* FUN_00448ecf */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_250) < 0x28) { cancel_event(a); return; }
    a[A_SCR_1FD] = 0xec;
}

static void ev_89(int r) {  /* FUN_00448f1f */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_250) < 0x28) { cancel_event(a); return; }
    a[A_SCR_1FD] = 0xd8;
}

static void ev_8a(int r) {  /* FUN_00448f6f */
    uint8_t *a = ARMY_REC(r); int mode = (int)s_slot_0057c934;
    if (army_i32(a, A_POP) < 100) { cancel_event(a); return; }
    if      (mode == 4) a[A_SCR_1FB] = 0xd8;
    else if (mode == 1) a[A_SCR_1FB] = 0xe2;
    else if (mode == 2) a[A_SCR_1FB] = 0xec;
    else if (mode == 3) a[A_SCR_1FB] = 0xe2;
    int8_t v = (int8_t)((int8_t)a[A_STAT_0B] - 0x14);
    if (v > 0x19) v = 0x19;
    if (v < 0)    v = 0;
    a[A_STAT_0B] = (uint8_t)v;
}

static void ev_8b(int r) {  /* FUN_00449093 */
    uint8_t *a = ARMY_REC(r); int mode = (int)s_slot_0057c934;
    if (army_i32(a, A_STAT_224) < 0x32) { cancel_event(a); return; }
    if      (mode == 4) a[A_SCR_1FC] = 0xf;
    else if (mode == 1) a[A_SCR_1FC] = 0x28;
    else if (mode == 2) a[A_SCR_1FC] = 0x23;
    else if (mode == 3) a[A_SCR_1FC] = 0x19;
}

static void ev_8c(int r) {  /* FUN_00449150 */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_250) < 0x28) { cancel_event(a); return; }
    a[A_SCR_1FD] = 0xf6;
}

static void ev_8d(int r) {  /* FUN_004491a0 */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_250) < 0x28) { cancel_event(a); return; }
    a[A_SCR_1FD] = 0x19;
}

static void ev_8e(int r) {  /* FUN_004491f0 */
    uint8_t *a = ARMY_REC(r); int mode = (int)s_slot_0057c934;
    if (army_i32(a, A_POP) < 100)        { cancel_event(a); return; }
    if ((int8_t)a[A_STAT_0C] < 0x1e)     { cancel_event(a); return; }
    if      (mode == 4) a[A_SCR_1FB] = 0x1e;
    else if (mode == 1) a[A_SCR_1FB] = 0x3c;
    else if (mode == 2) a[A_SCR_1FB] = 0x32;
    else if (mode == 3) a[A_SCR_1FB] = 0x28;
}

static void ev_12e(int r) {  /* FUN_004492e7 */
    uint8_t *a = ARMY_REC(r);
    if ((int8_t)a[A_STAT_0B] < 0x51) a[A_STAT_0B] = (uint8_t)((int8_t)a[A_STAT_0B] + 0x14);
    else cancel_event(a);
}

static void ev_12f(int r) {  /* FUN_0044934a */
    uint8_t *a = ARMY_REC(r);
    if (engine_realm_events_claim_empty(r) == 0) cancel_event(a);
}

static void ev_130(int r) {  /* FUN_0044938c */
    uint8_t *a = ARMY_REC(r); uint8_t *s = STRAT_REC(a[A_OWNER]);
    int off = S_CAT_140 + 4 + (r & 3) * 4;        /* DAT_0057c044 + (r&3)*4 */
    strat_set_i32(s, off, strat_i32(s, off) + 0x19);
}

static void ev_131(int r) {  /* FUN_004493c8 */
    uint8_t *s = STRAT_REC(ARMY_REC(r)[A_OWNER]);
    strat_set_i32(s, S_RES_118, strat_i32(s, S_RES_118) + 500);
}

static void ev_132(int r) {  /* FUN_00449400 */
    uint8_t *s = STRAT_REC(ARMY_REC(r)[A_OWNER]);
    strat_set_i32(s, S_RES_118, strat_i32(s, S_RES_118) + 1000);
}

static void ev_133(int r) {  /* FUN_00449438 */
    uint8_t *a = ARMY_REC(r);
    if ((int8_t)a[A_STAT_0C] < 0x60) a[A_STAT_0C] = (uint8_t)((int8_t)a[A_STAT_0C] + 5);
    else cancel_event(a);
}

static void ev_134(int r) {  /* FUN_0044949b */
    uint8_t *a = ARMY_REC(r);
    if ((int8_t)a[A_STAT_0C] < 0x5b) a[A_STAT_0C] = (uint8_t)((int8_t)a[A_STAT_0C] + 10);
    else cancel_event(a);
}

static void ev_135(int r) {  /* FUN_004494fe */
    uint8_t *s = STRAT_REC(ARMY_REC(r)[A_OWNER]);
    strat_set_i32(s, S_RES_128, strat_i32(s, S_RES_128) + 100);
}

static void ev_136(int r) {  /* FUN_00449533 */
    uint8_t *a = ARMY_REC(r);
    if (engine_realm_events_vacate_owned(r) == 0) cancel_event(a);
}

static void ev_137(int r) {  /* FUN_00449575 */
    uint8_t *a = ARMY_REC(r);
    if ((int8_t)a[A_STAT_0B] < 0x14)     { cancel_event(a); return; }
    if (army_i32(a, A_STAT_250) < 0x14)  { cancel_event(a); return; }
    a[A_STAT_0B] = (uint8_t)((int8_t)a[A_STAT_0B] - 0x14);
}

static void ev_138(int r) {  /* FUN_0044960f */
    uint8_t *a = ARMY_REC(r); uint8_t *s = STRAT_REC(a[A_OWNER]);
    if (strat_i32(s, S_RES_118) < 500) { cancel_event(a); return; }
    strat_set_i32(s, S_RES_118, strat_i32(s, S_RES_118) - 500);
}

static void ev_139(int r) {  /* FUN_00449688 */
    uint8_t *a = ARMY_REC(r); uint8_t *s = STRAT_REC(a[A_OWNER]);
    int off = S_CAT_140 + ((r & 3) + 1) * 4;
    if (strat_i32(s, off) < 0x19) { cancel_event(a); return; }
    strat_set_i32(s, off, strat_i32(s, off) - 0x19);
}

static void ev_13a(int r) {  /* FUN_0044970d */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_250) < 0x14) { cancel_event(a); return; }
    a[A_SCR_1FD] = 99;
}

static void ev_13b(int r) {  /* FUN_0044975d */
    uint8_t *a = ARMY_REC(r);
    if (army_i32(a, A_STAT_C0) < 10) { cancel_event(a); return; }
    a[A_SCR_1A8] = 100;
}

static void ev_13c(int r) {  /* FUN_004497ad */
    uint8_t *a = ARMY_REC(r); uint8_t *s = STRAT_REC(a[A_OWNER]);
    if (strat_i32(s, S_RES_130) < 300) { cancel_event(a); return; }
    strat_set_i32(s, S_RES_130, strat_i32(s, S_RES_130) - 300);
}

static void ev_13d(int r) {  /* FUN_00449826 */
    uint8_t *a = ARMY_REC(r);
    if ((int8_t)a[A_STAT_0C] < 7) { cancel_event(a); return; }
    a[A_STAT_0C] = (uint8_t)((int8_t)a[A_STAT_0C] - 7);
}

void engine_realm_event_apply(int realm, int eid)
{
    switch (eid) {
    case 0x87:  ev_87(realm);  break;
    case 0x88:  ev_88(realm);  break;
    case 0x89:  ev_89(realm);  break;
    case 0x8a:  ev_8a(realm);  break;
    case 0x8b:  ev_8b(realm);  break;
    case 0x8c:  ev_8c(realm);  break;
    case 0x8d:  ev_8d(realm);  break;
    case 0x8e:  ev_8e(realm);  break;
    case 0x12e: ev_12e(realm); break;
    case 0x12f: ev_12f(realm); break;
    case 0x130: ev_130(realm); break;
    case 0x131: ev_131(realm); break;
    case 0x132: ev_132(realm); break;
    case 0x133: ev_133(realm); break;
    case 0x134: ev_134(realm); break;
    case 0x135: ev_135(realm); break;
    case 0x136: ev_136(realm); break;
    case 0x137: ev_137(realm); break;
    case 0x138: ev_138(realm); break;
    case 0x139: ev_139(realm); break;
    case 0x13a: ev_13a(realm); break;
    case 0x13b: ev_13b(realm); break;
    case 0x13c: ev_13c(realm); break;
    case 0x13d: ev_13d(realm); break;
    default: break;   /* deck never holds an unmapped id */
    }
}

/* Port of FUN_00448819 — see engine_realm_events.h. */
void engine_realm_roll_events(uint32_t rng_7bit)
{
    int cursor = (int)(rng_7bit * 2);             /* DAT_0058fd60 * 2 */

    for (int r = 1; r <= (int)s_slot_0056d5dc; r++) {
        uint8_t *a = ARMY_REC(r);

        /* Unconditional per-realm turn-scratch reset. */
        a[A_SCR_1FB] = 0;
        a[A_SCR_1FC] = 0;
        a[A_SCR_1FD] = 0;
        a[A_SCR_1A8] = 0;

        cursor++;
        if (cursor > 0xff) cursor = 0;

        uint8_t owner = a[A_OWNER];
        if (STRAT_REC(owner)[S_EVENTS_ON] != 0 &&
            s_slot_00553edc > 0x4f4 &&
            EVENT_DECK[cursor] != 0) {
            a[A_EV_ACTIVE] = 1;
            *(int16_t *)(a + A_EV_ID) = (int16_t)EVENT_DECK[cursor];
            engine_realm_event_apply(r, EVENT_DECK[cursor]);
        }
    }
}
