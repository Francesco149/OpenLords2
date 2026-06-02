/*
 * OpenLords2 — per-realm unit upkeep / wages / desertion turn pass and the
 * shared unit-lifecycle primitives it drives.  See engine_realm_upkeep.h.
 *
 * Verbatim port of FUN_004acbd4 @ 0x4acbd4 and its private helper cluster
 * (FUN_004ace5e morale sweep, FUN_004ad39a absorb/overflow sweep, the
 * FUN_004ad0e8/230/316/495 per-owner scans) plus the shared lifecycle
 * primitives (FUN_004ad52b wage, FUN_004ad16c sub-stock decay, FUN_004ac6fe
 * contract clear, FUN_0046ec6e map unlink, FUN_0046f0a9 remove, FUN_004aa039
 * disband, FUN_004aba5a absorb).  Same field widths, same signed-byte/idiv
 * arithmetic, same linked-list splice, so the resulting save-state bytes match
 * retail exactly.  Record/army/strategic strides and the three cross-block
 * index chains (army v*0x300+0x1bc, strategic owner*0x160+0x2d+rec[+0x14f])
 * were r2-confirmed against Lords2.exe.
 */

#include <stdint.h>
#include <string.h>

#include "engine_realm_upkeep.h"
#include "save.h"                  /* g_per_territory_420, g_player_strategic_352,
                                      g_per_army_768, g_unit_table_20,
                                      g_overworld_map */
#include "save_schema_globals.h"   /* s_slot_0053f23c, s_slot_0053f260,
                                      s_slot_0057c8cc */
#include "engine_realm.h"          /* engine_realm_collect_territory_income (0x4ad6c0),
                                      engine_realm_replan_economies (0x448545) */
#include "engine_army_economy.h"   /* engine_army_economy_stockpile_value (0x44e7b4) */
#include "engine_math.h"           /* engine_math_percent_of (0x404d6b) */

/* --- per-record occupant ("unit") fields (g_per_territory_420, stride 0x1a4,
 *     records 1..150) --------------------------------------------------------
 * The block is the same one the territory/map modules bind; this subsystem
 * uses the unit-occupant fields of each record.  Bytes unless marked. */
#define UNIT_STRIDE   0x1a4
#define UNIT_REC(i)   (&g_per_territory_420[(i) * UNIT_STRIDE])

#define U_OWNER      0x00   /* owning player id (i8)                          */
#define U_NEXT       0x04   /* map-occupant list "next" link (i16; low byte
                             * is the head field's width)                     */
#define U_LINKED_06  0x06   /* set 1 when record is the cell list head, 0 when
                             * appended to the tail (u8; FUN_0046eddf)        */
#define U_ACTIVE     0x08   /* active flag (u8)                               */
#define U_CELL       0x0c   /* overworld-map cell byte offset (i32)           */
#define U_TYPE       0x10   /* unit type / kind (u8)                          */
#define U_ARMYKEY    0x11   /* army key for absorb credit (u8)                */
#define U_STRAT_IDX  0x14f  /* index into owner's strategic +0x2d counters (u8)*/
#define U_STATE      0x155  /* per-record morale escalation counter (u8)      */
#define U_SCR_156    0x156  /* turn-scratch byte (u8)                         */
#define U_SCR_157    0x157  /* turn-scratch byte (u8)                         */
#define U_SCR_158    0x158  /* turn-scratch byte (u8)                         */
#define U_WAGE       0x15c  /* computed per-turn wage (i32)                   */
#define U_FLAG_167   0x167  /* pending-absorb flag (u8)                       */
#define U_VALUE      0x168  /* unit value / wealth (i32)                      */
#define U_SUBSTOCK   0x16c  /* 7 × sub-stockpile (i16)                        */
#define U_CON_195    0x195  /* contract byte (u8)                             */
#define U_CON_AMT    0x196  /* contract amount, refunded on clear (u8)        */
#define U_CON_SLOT   0x197  /* mercenary-pool slot in g_unit_table_20 (u8)    */
#define U_LINK_A     0x198  /* army back-link A → g_per_army_768+0x1bc (u8)   */
#define U_LINK_B     0x199  /* army back-link B (u8)                          */
#define U_LINK_C     0x19a  /* army back-link C, cleared via link-A record(u8)*/

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) --- */
#define STRAT_STRIDE 0x160
#define STRAT_REC(s) (&g_player_strategic_352[(s) * STRAT_STRIDE])
#define S_FLAG_05    0x05   /* human/events-on gate; selects wage divisor (u8)*/
#define S_CNT_2D     0x2d   /* per-type unit counter array (u8)               */
#define S_WAGE_SUM   0xfc   /* total wage owed this turn (i32)                */
#define S_RES_118    0x118  /* resource account wages are paid from (i32)     */
#define S_STATE_158  0x158  /* per-player payment escalation state (u8)       */

/* --- per-realm army record (g_per_army_768, stride 0x300) ----------------- */
#define ARMY_STRIDE  0x300
#define ARMY_REC(k)  (&g_per_army_768[(k) * ARMY_STRIDE])
#define A_ACC_24     0x24   /* absorb accumulator A (i32)                     */
#define A_ACC_38     0x38   /* absorb accumulator B (i32)                     */
#define A_LINK_1BC   0x1bc  /* back-reference to a unit record index (i32)    */

/* --- narrow accessors (the records are byte arrays) ----------------------- */
static int32_t  rd_i32(const uint8_t *p, int off) { return *(const int32_t *)(p + off); }
static void     wr_i32(uint8_t *p, int off, int32_t v) { *(int32_t *)(p + off) = v; }
static int16_t  rd_i16(const uint8_t *p, int off) { return *(const int16_t *)(p + off); }
static void     wr_i16(uint8_t *p, int off, int16_t v) { *(int16_t *)(p + off) = v; }

/* ===== shared unit-lifecycle primitives ================================== */

/* Port of FUN_004ad52b. */
int engine_unit_recompute_wage(int rec)
{
    uint8_t *r     = UNIT_REC(rec);
    int      owner = (int8_t)r[U_OWNER];
    int32_t  value = rd_i32(r, U_VALUE);
    int32_t  wage;

    if (STRAT_REC(owner)[S_FLAG_05] == 0) {
        /* AI owner: value / {3,5,10,10} by difficulty. */
        switch (s_slot_0053f23c) {
        case 0:  wage = value / 3;  break;
        case 1:  wage = value / 5;  break;
        case 2:  wage = value / 10; break;
        default: wage = value / 10; break;
        }
    } else {
        /* Human owner: signed value / 4, rounding toward zero (the bias-and-
         * shift form the binary emits). */
        wage = (value + ((value >> 31) & 3)) >> 2;
    }
    wr_i32(r, U_WAGE, wage);
    return wage;
}

/* Port of FUN_004ad495. */
int engine_unit_total_wage(int owner)
{
    int total = 0;
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if ((int8_t)r[U_OWNER] == owner && r[U_ACTIVE] == 1)
            total += engine_unit_recompute_wage(i);
    }
    return total;
}

/* Port of FUN_004ac6fe. */
void engine_unit_clear_contract(int rec)
{
    uint8_t *r = UNIT_REC(rec);
    if (r[U_ACTIVE] == 1 && r[U_CON_SLOT] != 0) {
        uint8_t slot = r[U_CON_SLOT];
        wr_i32(r, U_VALUE, rd_i32(r, U_VALUE) - (int)r[U_CON_AMT]);
        r[U_CON_AMT]  = 0;
        r[U_CON_195]  = 0;
        r[U_CON_SLOT] = 0;
        *(uint16_t *)&g_unit_table_20[slot * 0x14] = 0;
    }
}

/* Port of FUN_0046ec6e — unlink the unit from its overworld cell's occupant
 * list.  The list head is a byte (g_overworld_map[cell+5]); the per-record
 * "next" link (+0x04) is an i16. */
void engine_unit_map_unlink(int rec)
{
    uint8_t *r    = UNIT_REC(rec);
    int      cell = rd_i32(r, U_CELL);
    unsigned head = g_overworld_map[cell + 0x05];
    int      guard = 0;

    if (head == 0)
        return;

    if (head == (unsigned)rec) {
        /* rec is the head: head := rec.next (truncated to the byte width). */
        g_overworld_map[cell + 0x05] = r[U_NEXT];
    } else {
        unsigned cur = head;
        while ((int)rd_i16(UNIT_REC(cur), U_NEXT) != rec && guard < 10) {
            if ((int)rd_i16(UNIT_REC(cur), U_NEXT) == (int)cur)
                wr_i16(UNIT_REC(cur), U_NEXT, 0);
            cur = (uint16_t)rd_i16(UNIT_REC(cur), U_NEXT);
            if (cur == 0) {
                wr_i16(r, U_NEXT, 0);
                return;
            }
            guard++;
        }
        wr_i16(UNIT_REC(cur), U_NEXT, rd_i16(r, U_NEXT));
    }
    wr_i16(r, U_NEXT, 0);
}

/* Port of FUN_0046eddf — link the unit into its overworld cell's occupant list
 * (the inverse of engine_unit_map_unlink).  The list head is the cell byte
 * g_overworld_map[cell+5]; the per-record "next" link (+0x04) is an i16.  A
 * record that is an active army member already carrying an army back-link
 * (U_ACTIVE == 1 AND U_LINK_A != 0) is left out of the map list entirely.
 * Otherwise the record is appended: it becomes the head when the list is empty
 * (U_LINKED_06 stays 1), else it is spliced onto the tail after at most a
 * 10-hop walk (U_LINKED_06 cleared to 0). */
void engine_unit_map_link(int rec)
{
    uint8_t *r    = UNIT_REC(rec);
    int      cell = rd_i32(r, U_CELL);
    unsigned head = g_overworld_map[cell + 0x05];
    int      guard = 0;

    if (r[U_ACTIVE] == 1 && r[U_LINK_A] != 0)
        return;

    r[U_LINKED_06] = 1;
    if (head == 0) {
        g_overworld_map[cell + 0x05] = (uint8_t)rec;
    } else {
        unsigned cur = head;
        while (rd_i16(UNIT_REC(cur), U_NEXT) != 0 && guard < 10) {
            cur = (uint16_t)rd_i16(UNIT_REC(cur), U_NEXT);
            guard++;
        }
        wr_i16(UNIT_REC(cur), U_NEXT, (int16_t)rec);
        r[U_LINKED_06] = 0;
    }
}

/* Port of FUN_0046f0a9. */
void engine_unit_remove(int rec)
{
    engine_unit_clear_contract(rec);
    engine_unit_map_unlink(rec);
    memset(UNIT_REC(rec), 0, UNIT_STRIDE);     /* FUN_0046ead0 */
    engine_realm_collect_territory_income();   /* FUN_004ad6c0 */
}

/* Port of FUN_004aa039. */
void engine_unit_disband(int rec)
{
    uint8_t *r     = UNIT_REC(rec);
    int      owner = (int8_t)r[U_OWNER];

    if (r[U_LINK_A] == 0) {
        uint8_t b = r[U_LINK_B];
        if (b != 0) {
            r[U_LINK_B] = 0;
            int32_t j = rd_i32(ARMY_REC(b), A_LINK_1BC);
            UNIT_REC(j)[U_LINK_C] = 0;
        }
    } else {
        wr_i32(ARMY_REC(r[U_LINK_A]), A_LINK_1BC, 0);
    }

    STRAT_REC(owner)[S_CNT_2D + r[U_STRAT_IDX]] -= 1;

    engine_unit_remove(rec);

    wr_i32(STRAT_REC(owner), S_WAGE_SUM, engine_unit_total_wage(owner));
}

/* Port of FUN_004aba5a. */
void engine_unit_absorb(int rec)
{
    uint8_t *r = UNIT_REC(rec);
    if (r[U_FLAG_167] == 0)
        return;

    if (r[U_FLAG_167] < 2) {
        uint8_t k     = r[U_ARMYKEY];
        int32_t value = rd_i32(r, U_VALUE);
        wr_i32(ARMY_REC(k), A_ACC_24, rd_i32(ARMY_REC(k), A_ACC_24) + value);
        wr_i32(ARMY_REC(k), A_ACC_38, rd_i32(ARMY_REC(k), A_ACC_38) + value);
        engine_unit_remove(rec);
        engine_realm_replan_economies();       /* FUN_00448545 */
    } else {
        r[U_FLAG_167] = 0;
    }
}

/* ===== private helpers =================================================== */

/* Port of FUN_004ad16c — decay each of the 7 sub-stockpiles by 10% (only when
 * above 10) and subtract the loss from the unit value. */
static void unit_decay_substocks(int rec)
{
    uint8_t *r = UNIT_REC(rec);
    for (int j = 0; j < 7; j++) {
        int16_t v    = rd_i16(r, U_SUBSTOCK + j * 2);
        int     loss = 0;
        if (v > 10)
            loss = engine_math_percent_of((int)v, 10);
        wr_i16(r, U_SUBSTOCK + j * 2, (int16_t)(v - (int16_t)loss));
        wr_i32(r, U_VALUE, rd_i32(r, U_VALUE) - loss);
    }
}

/* Port of FUN_004ad0e8 — decay sub-stocks for every active unit of `owner`. */
static void unit_decay_owner(int owner)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if ((int8_t)r[U_OWNER] == owner && r[U_ACTIVE] == 1)
            unit_decay_substocks(i);
    }
}

/* Port of FUN_004ad316 — disband every active unit of `owner`. */
static void unit_disband_owner(int owner)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if ((int8_t)r[U_OWNER] == owner && r[U_ACTIVE] == 1)
            engine_unit_disband(i);
    }
}

/* Port of FUN_004ad230 — settle (clear) the contract of every active,
 * contracted unit of `owner`; disband any whose value then reaches 0.  Returns
 * 1 iff at least one contracted unit was found. */
static int unit_settle_contracts(int owner)
{
    int found = 0;
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if ((int8_t)r[U_OWNER] == owner && r[U_ACTIVE] == 1 && r[U_CON_AMT] != 0) {
            found = 1;
            engine_unit_clear_contract(i);
            if (rd_i32(r, U_VALUE) == 0)
                engine_unit_disband(i);
        }
    }
    return found;
}

/* Port of FUN_004ad39a — for every active unit: run its pending absorb, then
 * disband it if its type byte exceeds 0x10. */
static void unit_absorb_overflow_sweep(void)
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if (r[U_ACTIVE] == 1) {
            engine_unit_absorb(i);
            if (r[U_TYPE] > 0x10)
                engine_unit_disband(i);
        }
    }
}

/* Rebuild the 7-entry audience filter table the event-queue producer expects:
 * one byte per player slot from each strategic record's +0x05, slot 6 zeroed
 * (matches the ranking/mood producers). */
static void upkeep_has_data(uint8_t out[7])
{
    for (int i = 0; i < 6; i++)
        out[i] = STRAT_REC(i)[S_FLAG_05];
    out[6] = 0;
}

/* Post a notification mirroring retail FUN_00472bc5(0, audience, code, ...) —
 * target is always 0 in this subsystem, audience-filtered to the local player. */
static void upkeep_post(engine_event_queue_state_t *evq, int audience, int code,
                        int32_t p32a, uint8_t f5, uint8_t f6, uint8_t f7,
                        int32_t p32b, const uint8_t has_data[7])
{
    engine_event_queue_post(evq, 0, audience, code, p32a, f5, f6, f7, p32b,
                            has_data, (int)s_slot_0057c8cc);
}

/* Port of FUN_004ace5e — per-unit morale/supply escalation sweep, gated on the
 * scalar s_slot_0053f260.  When a unit's value can no longer cover its army's
 * stockpile value, escalate a per-record counter (+0x155): warn at 1
 * (code 0x116), warn + decay at 2..4 (0x117), warn + disband at >=5 (0x118). */
static void unit_morale_sweep(engine_event_queue_state_t *evq, const uint8_t has_data[7])
{
    for (int i = 1; i < 0x97; i++) {
        uint8_t *r = UNIT_REC(i);
        if (r[U_OWNER] == 0 || r[U_ACTIVE] != 1)
            continue;

        r[U_SCR_156] = 0;
        r[U_SCR_157] = 0;
        r[U_SCR_158] = 0;

        if (s_slot_0053f260 == 0) {
            r[U_STATE] = 0;
            continue;
        }

        uint8_t type  = r[U_TYPE];
        int     floor = engine_army_economy_stockpile_value((int)type);
        if (rd_i32(r, U_VALUE) < floor) {
            r[U_STATE] = 0;
        } else if (r[U_LINK_A] == 0) {
            r[U_STATE]++;
            int audience = (int8_t)r[U_OWNER];
            if (r[U_STATE] == 1) {
                upkeep_post(evq, audience, 0x116, 0, 0x10, type, 0, 0, has_data);
            } else if (r[U_STATE] < 5) {
                unit_decay_substocks(i);
                upkeep_post(evq, audience, 0x117, 0, 0x10, type, 0, 0, has_data);
            } else {
                upkeep_post(evq, audience, 0x118, 0, 0x10, type, 0, 0, has_data);
                engine_unit_disband(i);
            }
        } else {
            r[U_STATE] = 0;
        }
    }
}

/* ===== the turn pass ===================================================== */

/* Port of FUN_004acbd4. */
void engine_realm_pay_upkeep(engine_event_queue_state_t *evq)
{
    uint8_t has_data[7];
    upkeep_has_data(has_data);

    unit_absorb_overflow_sweep();        /* FUN_004ad39a */
    unit_morale_sweep(evq, has_data);    /* FUN_004ace5e */

    for (int k = 1; k < 6; k++) {
        uint8_t *s    = STRAT_REC(k);
        int32_t  wage = engine_unit_total_wage(k);
        wr_i32(s, S_WAGE_SUM, wage);

        int32_t res = rd_i32(s, S_RES_118);
        if (res < wage) {
            if (s[S_STATE_158] == 0) {
                int settled = unit_settle_contracts(k);
                upkeep_post(evq, k, settled == 0 ? 0x10e : 0xa0,
                            0, 0, 0, 0, 0, has_data);
                s[S_STATE_158] = 1;
            } else if (s[S_STATE_158] < 4) {
                upkeep_post(evq, k, 0x11f, 0, 0, 0, 0, 0, has_data);
                unit_decay_owner(k);
                s[S_STATE_158]++;
            } else if (s[S_STATE_158] < 5) {
                upkeep_post(evq, k, 0x10f, 0, 0, 0, 0, 0, has_data);
                unit_decay_owner(k);
                s[S_STATE_158]++;
            } else {
                unit_disband_owner(k);
                if ((int)s_slot_0057c8cc == k)
                    upkeep_post(evq, k, 0x110, 0, 0, 0, 0, 0, has_data);
                else
                    upkeep_post(evq, (int)s_slot_0057c8cc, 0x111,
                                0, 0, 0, (uint8_t)k, 0, has_data);
                s[S_STATE_158] = 0;
            }
        } else {
            wr_i32(s, S_RES_118, res - wage);
            s[S_STATE_158] = 0;
        }
    }
}
