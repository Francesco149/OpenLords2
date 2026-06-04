/*
 * Tests for engine_diplomacy.c — inter-player diplomacy
 * (FUN_004a1a54 / FUN_004a24d1 / FUN_004a1ee1).
 *
 * State lives in g_player_strategic_352 (stride 0x160).  Per-other-player
 * relation sub-records start at +0x84 (stride 0x10): relation i8 +0x84, treaty
 * +0x85, war-counter +0x87, war-flag +0x88.  Scalars: lord +0x81, personality
 * +0x07, feud +0xeb, mood-cycle +0x159, active +0x04, AI gate +0x05.
 */

#include <stdint.h>
#include <string.h>

#include "t.h"
#include "engine_diplomacy.h"
#include "engine_event_queue.h"
#include "save.h"

#define SREC(s)    (&g_player_strategic_352[(s) * 0x160])
#define D_ACTIVE   0x04
#define D_AI       0x05
#define D_PERS     0x07
#define D_FLAG_1C  0x1c
#define D_FLAG_1E  0x1e
#define D_RANK     0x2b
#define D_LORD     0x81
#define D_REL      0x84
#define D_TREATY   0x85
#define D_CONTACT  0x86
#define D_WARCNT   0x87
#define D_WARFLAG  0x88
#define D_FEUD     0xeb
#define D_MOODCYC  0x159

static void dp_reset(void)
{
    memset(g_player_strategic_352, 0, sizeof g_player_strategic_352);
}

/* Make slot `s` an active AI realm. */
static void mk_ai(int s)   { SREC(s)[D_ACTIVE] = 1; SREC(s)[D_AI] = 0; }
/* Make slot `s` an active human realm. */
static void mk_human(int s){ SREC(s)[D_ACTIVE] = 1; SREC(s)[D_AI] = 1; }

/* --- FUN_004a1a54: clear lord/vassal treaty ------------------------------- */
int test_engine_diplomacy_clear_treaty(void)
{
    dp_reset();
    /* p1=2 is a vassal of p2=3; bond clears on p1's side only. */
    SREC(2)[D_LORD] = 3;
    SREC(2)[D_TREATY + 3 * 0x10] = 1;
    SREC(2)[D_FLAG_1E] = 9;
    SREC(3)[D_LORD] = 4;            /* p2's lord is someone else → untouched */
    SREC(3)[D_TREATY + 2 * 0x10] = 7;

    engine_diplomacy_clear_treaty(2, 3);

    T_ASSERT_EQ_U(SREC(2)[D_LORD], 0);
    T_ASSERT_EQ_U(SREC(2)[D_TREATY + 3 * 0x10], 0);
    T_ASSERT_EQ_U(SREC(2)[D_FLAG_1E], 0);
    T_ASSERT_EQ_U(SREC(3)[D_LORD], 4);
    T_ASSERT_EQ_U(SREC(3)[D_TREATY + 2 * 0x10], 7);
    return 0;
}

/* --- FUN_004a24d1: broadcast a relation penalty toward one player --------- */
int test_engine_diplomacy_broadcast_penalty(void)
{
    dp_reset();
    /* every other realm's relation toward target=3 drops by 15, floored -30 */
    SREC(1)[D_REL + 3 * 0x10] = 10;     /* → -5  */
    SREC(2)[D_REL + 3 * 0x10] = 0xea;   /* -22 → -37 → floor -30 (0xe2) */
    SREC(3)[D_REL + 3 * 0x10] = 99;     /* self — must NOT change */
    SREC(4)[D_REL + 3 * 0x10] = 0;      /* → -15 */

    engine_diplomacy_broadcast_penalty(3, 0x0f);

    T_ASSERT_EQ_I((int8_t)SREC(1)[D_REL + 3 * 0x10], -5);
    T_ASSERT_EQ_U(SREC(2)[D_REL + 3 * 0x10], 0xe2);
    T_ASSERT_EQ_U(SREC(3)[D_REL + 3 * 0x10], 99);
    T_ASSERT_EQ_I((int8_t)SREC(4)[D_REL + 3 * 0x10], -15);
    return 0;
}

/* --- FUN_004a1ee1: guards (no-op for human/inactive/out-of-range) --------- */
int test_engine_diplomacy_battle_adjust_guards(void)
{
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);

    dp_reset();
    mk_human(2);                          /* p1 must be AI → no-op */
    SREC(2)[D_REL + 3 * 0x10] = 20;
    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);
    T_ASSERT_EQ_I((int8_t)SREC(2)[D_REL + 3 * 0x10], 20);

    dp_reset();                            /* inactive p1 → no-op */
    SREC(2)[D_ACTIVE] = 0; SREC(2)[D_AI] = 0;
    SREC(2)[D_REL + 3 * 0x10] = 20;
    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);
    T_ASSERT_EQ_I((int8_t)SREC(2)[D_REL + 3 * 0x10], 20);

    dp_reset();                            /* p2 == p1 → no-op */
    mk_ai(2);
    SREC(2)[D_REL + 2 * 0x10] = 20;
    engine_diplomacy_battle_adjust(2, 2, 0x14, &evq);
    T_ASSERT_EQ_I((int8_t)SREC(2)[D_REL + 2 * 0x10], 20);
    return 0;
}

/* Plain relation hit (no betrayal, no war), clamped to [-30,+30]. */
int test_engine_diplomacy_battle_adjust_plain(void)
{
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    dp_reset();
    mk_ai(2);
    mk_ai(3);                              /* p2 is AI → no war escalation */
    SREC(2)[D_REL + 3 * 0x10] = 10;        /* 10 − 20 = −10 */

    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);

    T_ASSERT_EQ_I((int8_t)SREC(2)[D_REL + 3 * 0x10], -10);
    T_ASSERT_EQ_U(SREC(2)[D_WARFLAG + 3 * 0x10], 0);
    T_ASSERT_EQ_U(SREC(2)[D_WARCNT + 3 * 0x10], 0);
    return 0;
}

/* Betrayal path: p2 is p1's vassal (s2 lord == p1) → treaty broken on p2's
 * record, p1 flags war (pers != 4) + sets its feud, delta bumped by 15. */
int test_engine_diplomacy_battle_adjust_betrayal(void)
{
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    dp_reset();
    mk_ai(2);
    mk_ai(3);
    SREC(3)[D_LORD] = 2;                   /* p2=3 is vassal of p1=2 */
    SREC(3)[D_TREATY + 2 * 0x10] = 1;
    SREC(3)[D_FLAG_1E] = 9;
    SREC(2)[D_PERS] = 1;                   /* != 4 → war flagged */
    /* 40 −15 (broadcast also hits p1's own relation toward p2) −35 (20+15) = −10 */
    SREC(2)[D_REL + 3 * 0x10] = 40;

    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);

    T_ASSERT_EQ_U(SREC(3)[D_LORD], 0);                 /* treaty broken (p2 side) */
    T_ASSERT_EQ_U(SREC(3)[D_TREATY + 2 * 0x10], 0);
    T_ASSERT_EQ_U(SREC(3)[D_FLAG_1E], 0);
    T_ASSERT_EQ_U(SREC(2)[D_WARFLAG + 3 * 0x10], 1);   /* war flagged */
    T_ASSERT_EQ_U(SREC(2)[D_FEUD], 3);                 /* feud target */
    T_ASSERT_EQ_I((int8_t)SREC(2)[D_REL + 3 * 0x10], -10);
    T_ASSERT_EQ_U(SREC(2)[D_MOODCYC], 1);              /* bumped once */
    return 0;
}

/* War-declaration escalation: AI p1 bottoms its relation toward HUMAN p2 with
 * no war flagged → first step sets the war counter to 1 (code 0xbd). */
int test_engine_diplomacy_battle_adjust_war_escalate(void)
{
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    dp_reset();
    mk_ai(2);
    mk_human(3);                           /* p2 human → escalation eligible */
    SREC(2)[D_REL + 3 * 0x10] = (uint8_t)(-20);  /* −20 − 20 = −40 → floor −30 */

    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);

    T_ASSERT_EQ_U(SREC(2)[D_REL + 3 * 0x10], 0xe2); /* −30 */
    T_ASSERT_EQ_U(SREC(2)[D_WARCNT + 3 * 0x10], 1); /* first escalation */
    T_ASSERT_EQ_U(SREC(2)[D_MOODCYC], 1);
    return 0;
}

/* Third escalation step (counter 2 → 3) also flags war + sets the feud. */
int test_engine_diplomacy_battle_adjust_war_final(void)
{
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    dp_reset();
    mk_ai(2);
    mk_human(3);
    SREC(2)[D_REL + 3 * 0x10] = 0xe2;      /* already −30 */
    SREC(2)[D_WARCNT + 3 * 0x10] = 2;      /* poised for the final step */

    engine_diplomacy_battle_adjust(2, 3, 0x14, &evq);

    T_ASSERT_EQ_U(SREC(2)[D_WARCNT + 3 * 0x10], 3);
    T_ASSERT_EQ_U(SREC(2)[D_WARFLAG + 3 * 0x10], 1);
    T_ASSERT_EQ_U(SREC(2)[D_FEUD], 3);
    return 0;
}

/* --- FUN_004a16f7: should-engage predicate -------------------------------- */

/* Neutral defender (0) always engages; a genuinely different player engages. */
int test_engine_diplomacy_should_engage_hostile(void)
{
    dp_reset();
    SREC(2)[D_LORD] = 4;     /* 2's lord is 4, not 3 */
    T_ASSERT_EQ_I(engine_diplomacy_should_engage(2, 0), 1);   /* neutral */
    T_ASSERT_EQ_I(engine_diplomacy_should_engage(2, 3), 1);   /* different */
    return 0;
}

/* Same owner never engages (a stack can't fight itself). */
int test_engine_diplomacy_should_engage_self(void)
{
    dp_reset();
    T_ASSERT_EQ_I(engine_diplomacy_should_engage(2, 2), 0);
    return 0;
}

/* The aggressor never fights its own lord; the contact counter (+0x86) bumps. */
int test_engine_diplomacy_should_engage_lord(void)
{
    dp_reset();
    SREC(2)[D_LORD] = 3;                       /* 2 is a vassal of 3 */
    SREC(2)[D_CONTACT + 3 * 0x10] = 7;

    T_ASSERT_EQ_I(engine_diplomacy_should_engage(2, 3), 0);
    T_ASSERT_EQ_U(SREC(2)[D_CONTACT + 3 * 0x10], 8);   /* counter bumped */
    /* a different defender that is NOT the lord still engages */
    T_ASSERT_EQ_I(engine_diplomacy_should_engage(2, 4), 1);
    return 0;
}

/* --- FUN_004a1847: per-turn lord/vassal treaty reconciliation ------------- */
#define MATRIX(i, j)  (SREC(i)[D_TREATY + (j) * 0x10])

/* A mutual bond (each names the other) is confirmed: lords kept, matrix set
 * both ways; a stale matrix entry with no backing bond is wiped. */
int test_engine_diplomacy_reconcile_mutual(void)
{
    dp_reset();
    mk_ai(1); mk_ai(2); mk_ai(3);
    SREC(1)[D_LORD] = 2; SREC(2)[D_LORD] = 1;
    MATRIX(3, 1) = 1;                          /* stale, unbacked → wiped */

    engine_diplomacy_reconcile_treaties();

    T_ASSERT_EQ_I(SREC(1)[D_LORD], 2);
    T_ASSERT_EQ_I(SREC(2)[D_LORD], 1);
    T_ASSERT_EQ_I(MATRIX(1, 2), 1);
    T_ASSERT_EQ_I(MATRIX(2, 1), 1);
    T_ASSERT_EQ_I(MATRIX(3, 1), 0);            /* wiped */
    return 0;
}

/* A one-sided declaration onto an UNBONDED, live player imposes the bond: the
 * target gains this player as its lord (snapshot keeps it from re-processing). */
int test_engine_diplomacy_reconcile_imposes_on_unbonded(void)
{
    dp_reset();
    mk_ai(1); mk_ai(2);
    SREC(1)[D_LORD] = 2;                       /* 2 declares nothing */

    engine_diplomacy_reconcile_treaties();

    T_ASSERT_EQ_I(SREC(1)[D_LORD], 2);
    T_ASSERT_EQ_I(SREC(2)[D_LORD], 1);         /* bond imposed back */
    T_ASSERT_EQ_I(MATRIX(1, 2), 1);
    T_ASSERT_EQ_I(MATRIX(2, 1), 1);
    return 0;
}

/* A bond whose lord is already bonded to a THIRD player is severed. */
int test_engine_diplomacy_reconcile_severs_taken_lord(void)
{
    dp_reset();
    mk_ai(1); mk_ai(2); mk_ai(3);
    SREC(1)[D_LORD] = 2;                       /* 1 → 2 */
    SREC(2)[D_LORD] = 3;                       /* but 2 → 3 (not 1) */
    MATRIX(1, 2) = 1;                          /* stale link to be cleared */

    engine_diplomacy_reconcile_treaties();

    T_ASSERT_EQ_I(SREC(1)[D_LORD], 0);         /* severed */
    T_ASSERT_EQ_I(MATRIX(1, 2), 0);
    return 0;
}

/* A bond onto a lower-indexed, already-marked-inactive lord is severed (the
 * "settled" flag is only reliable for lords processed earlier in the pass). */
int test_engine_diplomacy_reconcile_severs_dead_lord(void)
{
    dp_reset();
    /* player 1 inactive (processed first → settled[1]=1); player 2 vassal of 1 */
    SREC(1)[D_ACTIVE] = 0;
    mk_ai(2);
    SREC(2)[D_LORD] = 1;
    MATRIX(2, 1) = 1;

    engine_diplomacy_reconcile_treaties();

    T_ASSERT_EQ_I(SREC(2)[D_LORD], 0);         /* severed: lord 1 is dead */
    T_ASSERT_EQ_I(MATRIX(2, 1), 0);
    return 0;
}

/* --- FUN_004a1241: best alliance candidate -------------------------------- */
int test_engine_diplomacy_best_ally_candidate(void)
{
    dp_reset();
    /* player 1 is the chooser; default relations 0 (> -11), no wars. */
    mk_ai(2); SREC(2)[D_RANK] = 3;           /* eligible, rank 3 */
    mk_ai(3); SREC(3)[D_RANK] = 2;           /* eligible, rank 2 (better) */
    mk_ai(4); SREC(4)[D_RANK] = 1;           /* leader → excluded */
    mk_ai(5); SREC(5)[D_RANK] = 2; SREC(5)[D_LORD] = 4;  /* has lord → excluded */

    T_ASSERT_EQ_I(engine_diplomacy_best_ally_candidate(1), 3);
    return 0;
}

/* Each disqualifier (relation floor, war flag, +0x1c gate) drops a candidate. */
int test_engine_diplomacy_best_ally_candidate_excludes(void)
{
    dp_reset();
    uint8_t *s = SREC(1);
    mk_ai(2); SREC(2)[D_RANK] = 2; s[D_REL + 2 * 0x10] = (uint8_t)(-11); /* not > -11 */
    mk_ai(3); SREC(3)[D_RANK] = 2; s[D_WARFLAG + 3 * 0x10] = 1;          /* at war */
    mk_ai(4); SREC(4)[D_RANK] = 2; SREC(4)[D_FLAG_1C] = 1;               /* gate set */

    T_ASSERT_EQ_I(engine_diplomacy_best_ally_candidate(1), 0);

    /* Loosen relation to exactly -10 → candidate 2 now qualifies. */
    s[D_REL + 2 * 0x10] = (uint8_t)(-10);
    T_ASSERT_EQ_I(engine_diplomacy_best_ally_candidate(1), 2);
    return 0;
}

/* --- FUN_004a1774: form mutual alliance bond ------------------------------ */
int test_engine_diplomacy_form_alliance(void)
{
    dp_reset();
    SREC(2)[D_FLAG_1E] = 5;             /* pre-set; bond formation clears it */
    SREC(3)[D_FLAG_1E] = 5;
    SREC(2)[D_CONTACT + 3 * 0x10] = 9;  /* pre-set contact; cleared */
    SREC(3)[D_CONTACT + 2 * 0x10] = 9;

    engine_diplomacy_form_alliance(2, 3);

    T_ASSERT_EQ_I(SREC(2)[D_TREATY + 3 * 0x10], 1);
    T_ASSERT_EQ_I(SREC(3)[D_TREATY + 2 * 0x10], 1);
    T_ASSERT_EQ_I(SREC(2)[D_CONTACT + 3 * 0x10], 0);
    T_ASSERT_EQ_I(SREC(3)[D_CONTACT + 2 * 0x10], 0);
    T_ASSERT_EQ_I(SREC(2)[D_LORD], 3);
    T_ASSERT_EQ_I(SREC(3)[D_LORD], 2);
    T_ASSERT_EQ_I(SREC(2)[D_FLAG_1E], 0);
    T_ASSERT_EQ_I(SREC(3)[D_FLAG_1E], 0);
    return 0;
}
