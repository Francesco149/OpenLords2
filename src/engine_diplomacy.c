/*
 * OpenLords2 — inter-player diplomacy.  See engine_diplomacy.h.
 *
 * Verbatim ports of FUN_004a1a54 / FUN_004a24d1 / FUN_004a1ee1, operating
 * purely on the strategic block (g_player_strategic_352, stride 0x160).  The
 * post-battle adjustment posts diplomacy notifications through the already-
 * ported event queue; the event-queue state, the per-player has-data filter
 * table and the local-player slot are threaded in as parameters (the engine.c
 * file-statics they would read are not linked into the host TU).
 */

#include <stdint.h>

#include "engine_diplomacy.h"
#include "save.h"                 /* g_player_strategic_352 */
#include "save_schema_globals.h"  /* s_slot_0057c8cc (local player) */

#define STRAT_STRIDE 0x160
#define STRAT_REC(s) (&g_player_strategic_352[(s) * STRAT_STRIDE])

#define D_ACTIVE      0x04   /* realm in-game flag (u8)                       */
#define D_AI          0x05   /* AI gate: 0 = AI-controlled (u8)              */
#define D_PERSONALITY 0x07   /* AI personality (u8)                          */
#define D_FLAG_1C     0x1c   /* per-player gate byte; an ally candidate needs it 0 */
#define D_FLAG_1E     0x1e   /* cleared when a lord/vassal bond breaks (u8)  */
#define D_RANK        0x2b   /* 1-based finishing rank (engine_ranking F_RANK) */
#define D_LORD        0x81   /* this player's lord/vassal id (u8)            */
#define D_REL         0x84   /* relation toward other (i8; + other*0x10)     */
#define D_TREATY      0x85   /* treaty flag toward other (u8; + other*0x10)  */
#define D_CONTACT     0x86   /* lord-contact counter (u8; + other*0x10)      */
#define D_WARCNT      0x87   /* war-declaration escalation (u8; + other*0x10)*/
#define D_WARFLAG     0x88   /* war-declared flag (u8; + other*0x10)         */
#define D_FEUD        0xeb   /* primary feud target (u8)                     */
#define D_MOODCYC     0x159  /* notification mood-cycle counter 0..3 (u8)    */

/* Port of FUN_004a1a54. */
void engine_diplomacy_clear_treaty(int p1, int p2)
{
    uint8_t *s1 = STRAT_REC(p1);
    uint8_t *s2 = STRAT_REC(p2);

    if (s1[D_LORD] == p2) {
        s1[D_TREATY + p2 * 0x10] = 0;
        s1[D_LORD]    = 0;
        s1[D_FLAG_1E] = 0;
    }
    if (s2[D_LORD] == p1) {
        s2[D_TREATY + p1 * 0x10] = 0;
        s2[D_LORD]    = 0;
        s2[D_FLAG_1E] = 0;
    }
}

/* Port of FUN_004a24d1. */
void engine_diplomacy_broadcast_penalty(int target, int amount)
{
    for (int other = 1; other < 6; other++) {
        if (other == target)
            continue;
        uint8_t *s = STRAT_REC(other);
        int off = D_REL + target * 0x10;
        s[off] = (uint8_t)(s[off] - amount);
        if ((int8_t)s[off] < -0x1e)
            s[off] = 0xe2;
    }
}

/* Advance a player's notification mood-cycle counter (+0x159), wrapping 0..3. */
static void mood_cycle_bump(uint8_t *s)
{
    s[D_MOODCYC] += 1;
    if (s[D_MOODCYC] > 3)
        s[D_MOODCYC] = 0;
}

/* Post one diplomacy notification (retail FUN_00472bc5(p1,p2,code,variant,1,0,
 * 0,0)).  The variant selector is (mood-cycle − 4) + personality·4, read from
 * the CURRENT counter before mood_cycle_bump advances it. */
static void diplo_notify(engine_event_queue_state_t *evq, int p1, int p2,
                         int code, uint8_t *s1)
{
    uint8_t has_data[7];
    for (int i = 0; i < 6; i++)
        has_data[i] = STRAT_REC(i)[D_AI];
    has_data[6] = 0;

    int variant = ((int)s1[D_MOODCYC] - 4) + (int)s1[D_PERSONALITY] * 4;
    engine_event_queue_post(evq, p1, p2, code, variant, 1, 0, 0, 0,
                            has_data, (int)s_slot_0057c8cc);
}

/* Port of FUN_004a1ee1. */
void engine_diplomacy_battle_adjust(int p1, int p2, int amount,
                                    engine_event_queue_state_t *evq)
{
    if (!(p1 > 0 && p1 < 6 && p2 > 0 && p2 < 6 && p2 != p1))
        return;

    uint8_t *s1 = STRAT_REC(p1);
    if (!(s1[D_ACTIVE] != 0 && s1[D_AI] == 0))
        return;
    uint8_t *s2 = STRAT_REC(p2);

    /* Betrayal: p2 was p1's lord. */
    if (s2[D_LORD] == p1) {
        engine_diplomacy_clear_treaty(p1, p2);
        if (s1[D_PERSONALITY] != 4) {
            s1[D_WARFLAG + p2 * 0x10] = 1;
            if (s1[D_FEUD] == 0)
                s1[D_FEUD] = (uint8_t)p2;
        }
        engine_diplomacy_broadcast_penalty(p2, 0x0f);
        diplo_notify(evq, p1, p2, 0xb6, s1);
        mood_cycle_bump(s1);
        amount += 0x0f;
    }

    /* Relation hit toward p2, clamped to [-30, +30]. */
    int rel = D_REL + p2 * 0x10;
    s1[rel] = (uint8_t)(s1[rel] - amount);
    if ((int8_t)s1[rel] < -0x1e)
        s1[rel] = 0xe2;
    else if ((int8_t)s1[rel] > 0x1d)
        s1[rel] = 0x1e;

    /* Escalating war declaration when the relation bottoms out against a human
     * p2 with no war yet flagged. */
    if (s1[D_WARFLAG + p2 * 0x10] == 0 && (int8_t)s1[rel] == -0x1e &&
        s2[D_AI] != 0) {
        uint8_t *wc = &s1[D_WARCNT + p2 * 0x10];
        if (*wc == 0) {
            *wc = 1;
            diplo_notify(evq, p1, p2, 0xbd, s1);
            mood_cycle_bump(s1);
        } else if (*wc == 1) {
            *wc = 2;
            diplo_notify(evq, p1, p2, 0xbe, s1);
            mood_cycle_bump(s1);
        } else if (*wc == 2) {
            *wc = 3;
            s1[D_WARFLAG + p2 * 0x10] = 1;
            if (s1[D_FEUD] == 0)
                s1[D_FEUD] = (uint8_t)p2;
            diplo_notify(evq, p1, p2, 0xbf, s1);
            mood_cycle_bump(s1);
        }
    }
}

/* Port of FUN_004a16f7. */
int engine_diplomacy_should_engage(int aggressor, int defender)
{
    if (defender == 0)
        return 1;                              /* neutral / unowned cell → engage */

    uint8_t *s = STRAT_REC(aggressor);
    if (s[D_LORD] == defender) {
        /* Defender is the aggressor's lord: never fight him; just record the
         * contact (per-lord counter at +0x86, indexed by defender). */
        s[D_CONTACT + defender * 0x10]++;
        return 0;
    }
    if (aggressor == defender)
        return 0;                              /* same stack owner → no fight */

    return 1;                                  /* different player → engage */
}

/* Port of FUN_004a1847 (525 B) — per-turn lord/vassal treaty reconciliation
 * over players 1..5.  Wipe the whole treaty matrix (+0x85, stride 0x10), then
 * validate each player's declared lord (+0x81): a bond survives only when it is
 * mutual (the lord is alive and either unbonded or already bonded back to this
 * player), in which case both ends are re-pinned and the matrix entries on both
 * sides set; otherwise the bond is severed (this player's lord cleared, its
 * matrix entry toward the lord cleared) and the player is treated as "settled"
 * for the rest of this pass so nobody can still bind to it.  Pure scalar/record
 * bookkeeping on g_player_strategic_352. */
void engine_diplomacy_reconcile_treaties(void)
{
    int settled[6];   /* retail aiStack_34: inactive OR severed this pass */
    int lord[6];      /* retail auStack_1c: each player's declared lord    */

    /* Wipe the treaty matrix for every (player, other) pair. */
    for (int i = 1; i < 6; i++)
        for (int j = 1; j < 6; j++)
            STRAT_REC(i)[D_TREATY + j * 0x10] = 0;

    for (int i = 1; i < 6; i++)
        settled[i] = 0;
    for (int i = 1; i < 6; i++)
        lord[i] = STRAT_REC(i)[D_LORD];

    for (int i = 1; i < 6; i++) {
        uint8_t *si = STRAT_REC(i);
        if (si[D_ACTIVE] == 0) {
            settled[i] = 1;                    /* inactive realm — no bonds */
            continue;
        }

        unsigned p = (unsigned)lord[i];
        if (p == 0)
            continue;                          /* no declared lord */

        if (settled[p] == 1 || (lord[p] != 0 && lord[p] != i)) {
            /* Lord is gone or already bonded elsewhere → sever this end. */
            si[D_LORD] = 0;
            si[D_TREATY + p * 0x10] = 0;
            settled[i] = 1;
            lord[i] = 0;
        } else {
            /* Mutual bond confirmed → re-pin both ends and the matrix. */
            uint8_t *sp = STRAT_REC(p);
            si[D_LORD] = (uint8_t)p;
            sp[D_LORD] = (uint8_t)i;
            si[D_TREATY + p * 0x10] = 1;
            sp[D_TREATY + i * 0x10] = 1;
        }
    }
}

/* Port of FUN_004a1241.  Pick the best alliance candidate for `player`: scan the
 * other slots 1..5 for an in-game, non-leader (rank != 1), lordless player whose
 * +0x1c gate is clear, toward whom `player`'s relation (+0x84) is > -11 and no
 * war is flagged (+0x88), and return the one with the best (lowest) rank — or 0
 * when none qualifies.  Pure read over g_player_strategic_352. */
int engine_diplomacy_best_ally_candidate(int player)
{
    uint8_t *s = STRAT_REC(player);
    int best = 0;
    uint8_t best_rank = 6;

    for (int i = 1; i < 6; i++) {
        uint8_t *si = STRAT_REC(i);
        if (si[D_ACTIVE] != 0 && i != player &&
            si[D_RANK] != 1 && si[D_LORD] == 0 && si[D_FLAG_1C] == 0 &&
            (int8_t)s[D_REL + i * 0x10] > -0x0b &&
            s[D_WARFLAG + i * 0x10] == 0 &&
            si[D_RANK] < best_rank) {
            best = i;
            best_rank = si[D_RANK];
        }
    }
    return best;
}

/* Port of FUN_004a1774.  Forge a mutual alliance bond between p1 and p2: set the
 * treaty flag (+0x85) and clear the contact counter (+0x86) both ways, pin each
 * as the other's lord (+0x81), and clear the bond-break flag (+0x1e) on both.
 * Pure write over g_player_strategic_352. */
void engine_diplomacy_form_alliance(int p1, int p2)
{
    uint8_t *s1 = STRAT_REC(p1);
    uint8_t *s2 = STRAT_REC(p2);

    s1[D_TREATY + p2 * 0x10] = 1;
    s1[D_CONTACT + p2 * 0x10] = 0;
    s2[D_TREATY + p1 * 0x10] = 1;
    s2[D_CONTACT + p1 * 0x10] = 0;
    s1[D_LORD] = (uint8_t)p2;
    s2[D_LORD] = (uint8_t)p1;
    s1[D_FLAG_1E] = 0;
    s2[D_FLAG_1E] = 0;
}
