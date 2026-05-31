/*
 * OpenLords2 — player standings / ranking recompute (implementation).
 *
 * Verbatim port of FUN_0049aa0e @ 0x49aa0e (2197 B).  Same phase order,
 * same comparison senses (strict `<`, so the first slot wins a tie),
 * same byte/int field widths, and the same dead score bands as the
 * binary — so the resulting save-state bytes match retail exactly.
 *
 * See engine_ranking.h for the high-level shape.
 */

#include <stdint.h>

#include "engine_ranking.h"
#include "save.h"                 /* g_player_strategic_352 (0x57bf00) */
#include "save_schema_globals.h"  /* the leaderboard result scalars + the board */

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) ---
 *
 * Field offsets within one 0x160-byte record, named by the role this
 * function uses them in.  Bytes unless marked i32.  Verified against the
 * decompile + r2 (the 11·32 = 0x160 stride, the metric offsets, and the
 * score multipliers/divisors). */
#define REC(p)        (&g_player_strategic_352[(p) * 0x160])

#define F_ACTIVE      0x04   /* slot-occupied flag                         */
#define F_HAS_DATA    0x05   /* human player_has_data flag                 */
#define F_OPP_TYPE    0x07   /* AI opponent-type code                      */
#define F_M_0C        0x0c   /* metric (byte): leader→0052f0a4, score ×2   */
#define F_M_10        0x10   /* metric (i32):  leader→0056d644, score ÷10  */
#define F_M_29        0x29   /* metric (byte): leader→00522f68             */
#define F_RANK        0x2b   /* output: 1-based finishing rank             */
#define F_M_2C        0x2c   /* metric (byte): leader→0056d634             */
#define F_M_4C        0x4c   /* metric (byte): leader→00568da0, score ×50  */
#define F_SCORE       0x50   /* output: composite score (i32)              */
#define F_M_54        0x54   /* metric (i32):  leader→005651c4, score ÷5   */
#define F_M_58        0x58   /* metric (byte): leader→00553c74, score ×2   */
#define F_M_60        0x60   /* score input (byte): score ×10              */
#define F_VICTORY     0xed   /* victory-event-fired latch                  */
#define F_M_118       0x118  /* metric (i32):  leader→0053e97c, score band */
#define F_VICT_ROT    0x159  /* rotating 0..3 counter for the victory msg  */

/* --- per-turn economic-summary metrics (same record) -----------------
 * Five i32 summary stats, each kept with a previous-turn companion at
 * +0x04 for the standings/graph trend display, plus the player's
 * cashflow total (+0x138 = sum of the six category sub-accounts at
 * +0x140..+0x154).  Maintained by the snapshot pass below; the total is
 * also resynced by the income/upkeep accumulators (see the helper). */
#define F_M_120       0x120  /* summary metric (i32),  prev at +0x124     */
#define F_M_128       0x128  /* summary metric (i32),  prev at +0x12c     */
#define F_M_130       0x130  /* summary metric (i32),  prev at +0x134     */
#define F_TOTAL       0x138  /* cashflow total (i32) = Σ F_CAT0[0..5]     */
#define F_CAT0        0x140  /* first of six i32 category sub-accounts    */
#define F_PREV        0x04   /* previous-turn companion = current + 4     */

/* Leaderboard board s_slot_00565410[48] = 6 × {i32 slot, i32 score}. */
#define BOARD_SLOT(i)   (*(int32_t *)&s_slot_00565410[(i) * 8 + 0])
#define BOARD_SCORE(i)  (*(int32_t *)&s_slot_00565410[(i) * 8 + 4])

static int32_t  rec_i32(int p, int off) { return *(int32_t *)&REC(p)[off]; }
static void     rec_set_i32(int p, int off, int32_t v) { *(int32_t *)&REC(p)[off] = v; }

/* Recompute one player's cashflow total (record +0x138) as the sum of
 * its six i32 category sub-accounts (record +0x140..+0x154).
 *
 *   FUN_004487a9 @ 0x4487a9 (112 B)
 *
 * Shared helper: besides the per-turn snapshot below, the per-category
 * income-credit accumulator (retail 0x448...) and the upkeep-deduction
 * pass (retail 0x4a9eb1) call it to resync the total after touching
 * a sub-account. */
void engine_ranking_recompute_player_total(int player)
{
    int32_t total = 0;
    for (int j = 0; j < 6; j++)
        total += rec_i32(player, F_CAT0 + j * 4);
    rec_set_i32(player, F_TOTAL, total);
}

/* Per-turn maintenance over player slots 1..5: roll each of five summary
 * metrics into its previous-turn companion (+0x04) for the standings /
 * graph trend display, then resync the cashflow total from its category
 * breakdown.
 *
 *   FUN_004486a9 @ 0x4486a9 (256 B)
 *
 * Runs first in the turn-advance sequence (retail 0x448440), before
 * the per-realm economy passes repopulate the category sub-accounts.  No
 * active-slot filter — all five slots roll unconditionally.  The
 * +0x138 → +0x13c copy must precede the recompute so the companion keeps
 * last turn's total, not this turn's fresh sum. */
void engine_ranking_snapshot_player_metrics(void)
{
    for (int p = 1; p < 6; p++) {
        rec_set_i32(p, F_M_118 + F_PREV, rec_i32(p, F_M_118));
        rec_set_i32(p, F_TOTAL + F_PREV, rec_i32(p, F_TOTAL));
        rec_set_i32(p, F_M_120 + F_PREV, rec_i32(p, F_M_120));
        rec_set_i32(p, F_M_128 + F_PREV, rec_i32(p, F_M_128));
        rec_set_i32(p, F_M_130 + F_PREV, rec_i32(p, F_M_130));
        engine_ranking_recompute_player_total(p);
    }
}

void engine_ranking_recompute(engine_event_queue_state_t *evq)
{
    /* --- Phase A: clear the leaderboard result scalars (retail order). */
    s_slot_00553d24 = 0;   /* winner (first ranked)            */
    s_slot_00522d90 = 0;   /* last ranked                      */
    s_slot_0056d5d8 = 0;   /* active count (excludes local)    */
    s_slot_0056d6a8 = 0;   s_slot_0056d644 = 0;   /* +0x10 leader / max */
    s_slot_0053f058 = 0;   s_slot_0052f0a4 = 0;   /* +0x0c leader / max */
    s_slot_00544098 = 0;   s_slot_00553c74 = 0;   /* +0x58 leader / max */
    s_slot_0053e9cc = 0;   s_slot_0053e97c = 0;   /* +0x118 leader / max */
    s_slot_00553028 = 0;   s_slot_00522f68 = 0;   /* +0x29 leader / max */
    s_slot_00553034 = 0;   s_slot_00568da0 = 0;   /* +0x4c leader / max */
    s_slot_0055ce80 = 0;   s_slot_005651c4 = 0;   /* +0x54 leader / max */
    s_slot_0056d5bc = 0;   s_slot_0056d634 = 0;   /* +0x2c leader / max */

    /* --- Phase B: per-metric max + holder over active slots 1..5. */
    for (int p = 1; p < 6; p++) {
        if (REC(p)[F_ACTIVE] == 0)
            continue;

        if (s_slot_0057c8cc != (uint32_t)p)
            s_slot_0056d5d8++;

        if ((int32_t)s_slot_00522f68 < REC(p)[F_M_29]) {
            s_slot_00522f68 = REC(p)[F_M_29];  s_slot_00553028 = (uint32_t)p;
        }
        if ((int32_t)s_slot_0056d644 < rec_i32(p, F_M_10)) {
            s_slot_0056d644 = (uint32_t)rec_i32(p, F_M_10);  s_slot_0056d6a8 = (uint32_t)p;
        }
        if ((int32_t)s_slot_0052f0a4 < REC(p)[F_M_0C]) {
            s_slot_0052f0a4 = REC(p)[F_M_0C];  s_slot_0053f058 = (uint32_t)p;
        }
        if ((int32_t)s_slot_00553c74 < REC(p)[F_M_58]) {
            s_slot_00553c74 = REC(p)[F_M_58];  s_slot_00544098 = (uint32_t)p;
        }
        if ((int32_t)s_slot_0053e97c < rec_i32(p, F_M_118)) {
            s_slot_0053e97c = (uint32_t)rec_i32(p, F_M_118);  s_slot_0053e9cc = (uint32_t)p;
        }
        if ((int32_t)s_slot_0056d634 < REC(p)[F_M_2C]) {
            s_slot_0056d634 = REC(p)[F_M_2C];  s_slot_0056d5bc = (uint32_t)p;
        }
        if ((int32_t)s_slot_005651c4 < rec_i32(p, F_M_54)) {
            s_slot_005651c4 = (uint32_t)rec_i32(p, F_M_54);  s_slot_0055ce80 = (uint32_t)p;
        }
        if ((int32_t)s_slot_00568da0 < REC(p)[F_M_4C]) {
            s_slot_00568da0 = REC(p)[F_M_4C];  s_slot_00553034 = (uint32_t)p;
        }
    }

    /* --- Phase C: composite score → field +0x50.  Reset is
     * unconditional; the weighted sum runs only for active slots.
     *
     * The +0x118 band is ported verbatim from the binary.  Only the
     * `>= 2001 → +50` arm is reachable: the outer `< 2001` guard makes
     * the inner `>= 5001 → +100` and `> 10000 → +200` adds dead code
     * present in the .exe (r2-confirmed at 0x49aed1).  Preserved so the
     * byte shape matches. */
    for (int p = 1; p < 6; p++) {
        rec_set_i32(p, F_SCORE, 0);
        if (REC(p)[F_ACTIVE] == 0)
            continue;

        int32_t score = 0;
        score += REC(p)[F_M_60] * 10;
        score += rec_i32(p, F_M_10) / 10;
        score += REC(p)[F_M_0C] * 2;
        score += REC(p)[F_M_58] * 2;
        score += rec_i32(p, F_M_54) / 5;
        score += REC(p)[F_M_4C] * 0x32;

        int32_t m = rec_i32(p, F_M_118);
        if (m < 0x7d1) {              /* m <= 2000 — inner arms unreachable */
            if (m < 0x1389) {
                if (m > 10000) score += 200;   /* dead */
            } else {
                score += 100;                  /* dead */
            }
        } else {
            score += 0x32;            /* m >= 2001 → +50 (only live band) */
        }

        rec_set_i32(p, F_SCORE, score);
    }

    /* --- Phase D: build the {slot, score} board for slots 1..5, then a
     * descending bubble sort.  Entry 5 is a zero sentinel the inner
     * compare reads as the (local_8+1)th neighbour. */
    for (int i = 0; i < 5; i++) {
        BOARD_SLOT(i)  = i + 1;
        BOARD_SCORE(i) = rec_i32(i + 1, F_SCORE);
    }
    BOARD_SCORE(5) = 0;
    BOARD_SLOT(5)  = 0;

    for (int pass = 0; pass < 5; pass++) {
        for (int i = 0; i < 5; i++) {
            if (BOARD_SCORE(i) < BOARD_SCORE(i + 1)) {
                int32_t ts = BOARD_SLOT(i + 1);
                int32_t tc = BOARD_SCORE(i + 1);
                BOARD_SLOT(i + 1)  = BOARD_SLOT(i);
                BOARD_SCORE(i + 1) = BOARD_SCORE(i);
                BOARD_SLOT(i)  = ts;
                BOARD_SCORE(i) = tc;
            }
        }
    }

    /* --- Phase E: stamp 1-based rank into +0x2b; inactive slots get
     * rank 0 and are dropped from the board. */
    for (int i = 0; i < 5; i++) {
        int slot = BOARD_SLOT(i);
        if (REC(slot)[F_ACTIVE] == 0) {
            REC(slot)[F_RANK] = 0;
            BOARD_SLOT(i) = 0;
        } else {
            REC(slot)[F_RANK] = (uint8_t)(i + 1);
        }
    }

    /* --- Phase F: first (winner) + last ranked slot. */
    for (int i = 0; i < 5; i++) {
        if (BOARD_SLOT(i) != 0) {
            if (s_slot_00553d24 == 0)
                s_slot_00553d24 = (uint32_t)BOARD_SLOT(i);
            s_slot_00522d90 = (uint32_t)BOARD_SLOT(i);
        }
    }
    int winner = (int)s_slot_00553d24;   /* retail iVar3, latched in the loop */

    /* --- Phase G: a single survivor (winner == last) fires a victory /
     * notification event.  Build the player_has_data[7] filter the
     * ported producer expects; only index `target` (1..5 here) is read,
     * so slot 6 — which would overrun the 6-slot table — stays 0. */
    if (s_slot_00522d90 == s_slot_00553d24) {
        uint8_t has_data[7];
        for (int i = 0; i < 6; i++) has_data[i] = REC(i)[F_HAS_DATA];
        has_data[6] = 0;
        const int local_player = (int)s_slot_0057c8cc;

        if (REC(winner)[F_VICTORY] == 0 && REC(winner)[F_HAS_DATA] == 0) {
            REC(winner)[F_VICTORY] = 1;
            int32_t payload = ((int)REC(winner)[F_VICT_ROT] - 4)
                            + (int)REC(winner)[F_OPP_TYPE] * 4;
            engine_event_queue_post(evq, winner, 0, 0xc3, payload,
                                    1, 0, 0, 0, has_data, local_player);
            REC(winner)[F_VICT_ROT]++;
            if (REC(winner)[F_VICT_ROT] > 3)
                REC(winner)[F_VICT_ROT] = 0;
        } else {
            REC(winner)[F_VICTORY] = 1;
            engine_event_queue_post(evq, 0, (int)s_slot_0057c8cc, 0xe1, 0,
                                    0xe, 0, 0, 0, has_data, local_player);
        }
    }
}
