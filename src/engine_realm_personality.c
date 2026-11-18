/*
 * OpenLords2 — AI realms' banner colour / personality seat setup
 * (implementation).
 *
 * Verbatim port of FUN_0049caaa @ 0x49caaa.  Same loop bounds, same
 * write order, same (absent) bounds checks — so the resulting byte
 * state of g_player_strategic_352 / g_player_aux_44 matches retail.
 *
 * See engine_realm_personality.h for the retail global ↔ storage mapping.
 */

#include "engine_realm_personality.h"

#include "localization.h"  /* localization_state_t, localization_seek_skip */
#include "strutil.h"       /* strutil_bounded_copy (FUN_00401136) */

/* Personality-preference / colour-pair tables — retail .data from the
 * label 0x4dc17c, dumped verbatim (96 bytes: k_realm_personality_pref
 * 0x4dc180 (u8[4][5][4]) and k_realm_colour_pair 0x4dc1d0).  Indexed by
 * absolute byte offset from 0x4dc17c so the same blob serves both lookups
 * retail performs:
 *
 *   personality preference   table[variant*0x14 + colour*4 + slot]
 *                            (variant ∈ 0..3, colour ∈ 1..5, slot ∈ 0..3)
 *   colour-pair byte a       table[0x52 + colour*2]   (retail DAT_004dc1ce)
 *   colour-pair byte b       table[0x53 + colour*2]   (retail DAT_004dc1cf)
 *
 * Variant 0 (offsets 0x00..0x13) doubles as the MP / s_slot_0055302c == 1
 * set; the colour-pair labels overlap variant-3's tail bytes, exactly as
 * in the retail image. */
#define DC_BASE          0x4dc17c
#define DC_OFF(va)       ((va) - DC_BASE)
#define DC_PAIR_A_BASE   DC_OFF(0x4dc1ce)   /* 0x52 */
#define DC_PAIR_B_BASE   DC_OFF(0x4dc1cf)   /* 0x53 */

static const uint8_t s_personality_pref[96] = {
    0,0,0,0, 2,1,3,4, 1,3,4,2, 2,1,3,4,        /* variant 0 (colours 0..3) */
    4,3,2,1, 3,2,4,1, 1,3,4,2, 4,1,3,2,        /* variant 1 */
    2,1,3,4, 4,2,3,1, 1,3,2,4, 1,3,4,2,        /* variant 2 */
    4,1,2,3, 3,2,1,4, 1,4,2,3, 2,1,3,4,        /* variant 3 (head) */
    3,2,4,1, 2,1,3,4, 3,1,2,4, 4,3,2,1,        /* variant 3 (tail) */
    1,3,4,2,                                   /* 0x50..0x53 */
    14,15, 251,13, 58,32, 5,253, 4,240,        /* 0x54..0x5d: colour pairs, colours 1..5 */
    0,0,                                       /* 0x5e..0x5f */
};

/* Strategic-record stride 0x160, aux-record stride 0x2c. */
#define STRAT(seat)  (&g_player_strategic_352[(seat) * 0x160])
#define AUX(seat)    (&g_player_aux_44[(seat) * 0x2c])

/* Localization blob source — see header. */
static const uint8_t *s_loc_blob;
static size_t         s_loc_size;

void engine_realm_personality_set_localization(const uint8_t *blob, size_t size)
{
    s_loc_blob = blob;
    s_loc_size = size;
}

void engine_realm_personality_init(void)
{
    uint8_t colour_taken[8];   /* acStack_14 — banner colours 1..5 reserved */
    uint8_t pers_taken[8];     /* acStack_20 — personalities 1..4 reserved */
    int     assigned;          /* local_8  — realms assigned so far */
    int     seat;              /* local_18 */
    int     i;                 /* local_c  */

    for (i = 0; i < 6; i++) colour_taken[i] = 0;
    for (i = 0; i < 5; i++) pers_taken[i] = 0;

    /* Pre-reserve colours held by players with a fixed colour:
     * aux[+0x26] == 0  &&  aux[+0x25] != 0  ⇒  colour aux[+0x25] is taken. */
    for (i = 1; i < 6; i++) {
        if (AUX(i)[0x26] == 0 && AUX(i)[0x25] != 0) {
            colour_taken[AUX(i)[0x25]] = 1;
        }
    }

    assigned = 0;
    for (seat = 1; seat < 6; seat++) {
        STRAT(seat)[0x07] = 0;                  /* clear the personality */

        if (STRAT(seat)[0x05] != 0) {           /* seat already in use — skip */
            continue;
        }

        if (assigned >= (int)s_slot_0053f268) { /* opponent limit reached */
            STRAT(seat)[0x04] = 0;
            continue;
        }
        assigned++;

        /* Assign the first free banner colour (1..5). */
        for (i = 1; i < 6; i++) {
            if (colour_taken[i] == 0) {
                STRAT(seat)[0x0a] = (uint8_t)i;
                colour_taken[i] = 1;
                break;
            }
        }

        /* Assign the first not-yet-taken personality from this colour's
         * preference list.  SP with s_slot_0055302c != 1 indexes the
         * variant (s_slot_0053f034 & 3) table; otherwise variant 0. */
        for (i = 0; i < 4; i++) {
            uint8_t colour = STRAT(seat)[0x0a];
            uint8_t cand;
            if (g_sim_mp_active == 0 && s_slot_0055302c != 1) {
                cand = s_personality_pref[(s_slot_0053f034 & 3) * 0x14 + colour * 4 + i];
            } else {
                cand = s_personality_pref[colour * 4 + i];
            }
            if (pers_taken[cand] == 0) {
                STRAT(seat)[0x07] = cand;
                pers_taken[cand] = 1;
                break;
            }
        }

        /* Copy the personality's localized 16-byte name from L2.ENG
         * section 7, walking forward `personality` sub-strings.  Retail reads
         * the global walker; we run a local walker over the injected
         * blob (skipped when none is set — see engine-quirks #107). */
        if (s_loc_blob != NULL) {
            localization_state_t loc = { (unsigned char *)s_loc_blob, 0 };
            localization_seek_skip(&loc, 7, STRAT(seat)[0x07]);
            strutil_bounded_copy(&AUX(seat)[0x04], s_loc_blob + loc.walker_offset, 0x10);
        }
    }

    /* Finalize: every seat 1..5 gets its colour's two colour-pair bytes,
     * and +0x2a / +0x29 (counties owned) seeded with 1.  Reads the pair
     * table by the (possibly stale, for in-use seats) colour — retail
     * does the same, with no bounds check. */
    for (seat = 1; seat < 6; seat++) {
        uint8_t colour = STRAT(seat)[0x0a];
        STRAT(seat)[0x2a] = 1;
        STRAT(seat)[0x29] = STRAT(seat)[0x2a];
        STRAT(seat)[0x08] = s_personality_pref[DC_PAIR_A_BASE + colour * 2];
        STRAT(seat)[0x09] = s_personality_pref[DC_PAIR_B_BASE + colour * 2];
    }
}
