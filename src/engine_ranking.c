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
#include "engine_math.h"          /* engine_math_percent_div (FUN_00404dc1) */

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

/* Two more output fields the aggregate recompute (below) fills; the rest of
 * its outputs reuse the metric offsets above. */
#define F_M_14        0x14   /* output: average troop strength (i32)       */
#define F_M_18        0x18   /* output: previous-turn total strength (i32)  */

/* --- aggregate-source records the recompute reads from -------------------
 * Per-realm "army" record (g_per_army_768, retail 0x53f9b0, stride 0x300):
 *   +0x05 owning player id (u8)   +0x24 troop strength (i32)
 *   +0x0b standing byte           +0x0c rating byte (i8)
 * Per-territory record (g_per_territory_420, retail 0x52f0b0, stride 0x1a4):
 *   +0x00 owning player id (i8)   +0x08 record kind (1 = settled territory)
 *   +0x168 territory value (i32) */
#define AREC(k)       (&g_per_army_768[(k) * 0x300])
#define A_OWNER       0x05
#define A_STANDING    0x0b
#define A_RATING      0x0c
#define A_STRENGTH    0x24
#define TREC(t)       (&g_per_territory_420[(t) * 0x1a4])
#define T_OWNER       0x00
#define T_KIND        0x08
#define T_VALUE       0x168

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

/* Port of FUN_0049d1e0 @ 0x49d1e0 (1024 B) — recompute one player's aggregate
 * strategic metrics from the realms and territories it owns.  These are exactly
 * the per-metric inputs engine_ranking_recompute later reduces to leaders and
 * composite scores: army count (+0x29), total / average / previous-turn troop
 * strength (+0x10 / +0x14 / +0x18), an averaged rating (+0x0c) and standing
 * (+0x58), the realm-share percent (+0x60), and the territory count (+0x2c) and
 * total value (+0x54).
 *
 * Player 0 (the neutral/unowned slot) only gets the four head fields cleared —
 * the whole aggregation is gated on player != 0.  The +0x58 byte accumulator
 * wraps mod 256 exactly as the binary's 8-bit add does, then is averaged with
 * unsigned division; the rating accumulator is a signed int averaged with
 * signed division.  Reads only the realm count + the army/territory blocks. */
void engine_ranking_recompute_player_aggregate(unsigned player)
{
    uint8_t *p = REC(player);

    p[F_M_29] = 0;
    rec_set_i32(player, F_M_18, rec_i32(player, F_M_10));   /* prev = current  */
    rec_set_i32(player, F_M_10, 0);
    p[F_M_0C] = 0;
    p[F_M_58] = 0;

    if (player == 0)
        return;

    int rating_sum = 0;
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        const uint8_t *r = AREC(key);
        if (r[A_OWNER] == player) {
            p[F_M_29]++;
            *(int32_t *)&p[F_M_10] += *(const int32_t *)&r[A_STRENGTH];
            rating_sum += (int8_t)r[A_RATING];
            p[F_M_58] = (uint8_t)(p[F_M_58] + r[A_STANDING]);
        }
    }

    int n = p[F_M_29];
    rec_set_i32(player, F_M_14, n == 0 ? 0 : rec_i32(player, F_M_10) / n);
    p[F_M_60] = n == 0 ? 0 : (uint8_t)engine_math_percent_div(n, (int)s_slot_0056d5dc);
    p[F_M_0C] = n == 0 ? 0 : (uint8_t)(rating_sum / n);
    p[F_M_58] = n == 0 ? 0 : (uint8_t)(p[F_M_58] / n);

    p[F_M_2C] = 0;
    rec_set_i32(player, F_M_54, 0);
    for (int t = 1; t < 0x97; t++) {
        const uint8_t *terr = TREC(t);
        if (terr[T_KIND] == 1 && (int8_t)terr[T_OWNER] == (int)player) {
            p[F_M_2C]++;
            *(int32_t *)&p[F_M_54] += *(const int32_t *)&terr[T_VALUE];
        }
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

/* Optional game-over-screen hook for engine_ranking_check_elimination — retail
 * FUN_0043eda0(0x24, 0), the local-player defeat screen.  NULL (the default)
 * means no screen change, so the elimination logic + notifications run headless
 * (the diff / host tests bind it to NULL).  It is the function's only side
 * effect outside the save-state globals + the event ring. */
static void (*s_game_over_fn)(void);

void engine_ranking_bind_game_over_fn(void (*fn)(void)) { s_game_over_fn = fn; }

/* Port of FUN_0049b42b @ 0x49b42b (680 B) — re-evaluate whether `player` has
 * been eliminated and, if so, fire the defeat notification (and, for the local
 * player in a multiplayer game, the game-over screen).  The exact mirror of
 * engine_ranking_recompute's victory branch: same rotating message counter
 * (+0x159) and opponent-type payload (+0x07), elimination codes 0xe0/0xc2 in
 * place of victory's 0xe1/0xc3.
 *
 * A player's "liveness score" (+0x04, also the active-slot flag the ranking
 * recompute reads) is rebuilt as 3 per owned army + 1 per owned settled
 * territory (kind +0x08 == 1).  Zero ⇒ eliminated:
 *   - local player  → defeat notification 0xe0 (audience = local);
 *   - an AI player (+0x05 has-data == 0) → flavoured elimination 0xc2 with the
 *     same `(rot − 4) + opp_type·4` payload as a victory message;
 *   - a non-local human keeps quiet but still rolls its message counter.
 * The rotating counter advances (wrap 0..3) on every elimination regardless of
 * which branch posted.  The field is then re-ranked via engine_ranking_recompute,
 * and finally, if the local player was the one eliminated in a multiplayer game
 * (retail DAT_00553030 / g_sim_mp_active, threaded as `mp_active`), the bound
 * game-over hook fires.
 *
 * Slots outside 1..5 or already-inactive (+0x04 == 0) are no-ops. */
void engine_ranking_check_elimination(unsigned player,
                                      engine_event_queue_state_t *evq,
                                      int mp_active)
{
    if (!(player > 0 && player < 6) || REC(player)[F_ACTIVE] == 0)
        return;

    uint8_t *p = REC(player);
    p[F_ACTIVE] = 0;
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++)
        if (AREC(key)[A_OWNER] == player)
            p[F_ACTIVE] = (uint8_t)(p[F_ACTIVE] + 3);
    for (int t = 1; t < 0x97; t++)
        if ((int8_t)TREC(t)[T_OWNER] == (int)player && TREC(t)[T_KIND] == 1)
            p[F_ACTIVE] = (uint8_t)(p[F_ACTIVE] + 1);

    if (p[F_ACTIVE] == 0) {
        uint8_t has_data[7];
        for (int i = 0; i < 6; i++) has_data[i] = REC(i)[F_HAS_DATA];
        has_data[6] = 0;
        const int local_player = (int)s_slot_0057c8cc;

        if (s_slot_0057c8cc == player) {
            engine_event_queue_post(evq, local_player, local_player, 0xe0, 0,
                                    0xe, 0, 0, 0, has_data, local_player);
        } else if (p[F_HAS_DATA] == 0) {
            int32_t payload = ((int)p[F_VICT_ROT] - 4) + (int)p[F_OPP_TYPE] * 4;
            engine_event_queue_post(evq, (int)player, 0, 0xc2, payload,
                                    0xe, 0, 0, 0, has_data, local_player);
        }
        p[F_VICT_ROT]++;
        if (p[F_VICT_ROT] > 3)
            p[F_VICT_ROT] = 0;
    }

    engine_ranking_recompute(evq);

    if (p[F_ACTIVE] == 0 && s_slot_0057c8cc == player && mp_active != 0
        && s_game_over_fn)
        s_game_over_fn();
}
