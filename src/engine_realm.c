/*
 * OpenLords2 — per-realm strategic turn-state updates.  See engine_realm.h.
 */

#include <stdint.h>

#include "engine_realm.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352,
                                      g_per_territory_420 */
#include "save_schema_globals.h"   /* s_slot_0056d5dc, s_slot_00553240,
                                      s_slot_0057c92c */
#include "engine_army_economy.h"   /* engine_army_economy_provision / _plan_production
                                      / _select_candidate */
#include "engine_math.h"           /* engine_math_percent_of */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])
#define TERR_REC(idx)   (&g_per_territory_420[(idx) * 0x1a4])

/* Port of FUN_0044baea — see engine_realm.h for the field overview. */
void engine_realm_advance_rating(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);

        r[0x0d] = r[0x0c];                          /* previous = current */

        /* Fold in the three signed adjustment sources, one at a time, with
         * 8-bit wrap between steps (retail stores the byte back each add). */
        int8_t v = (int8_t)r[0x0c];
        v = (int8_t)(v + (int8_t)r[0x0e]);
        v = (int8_t)(v + (int8_t)r[0x10]);
        v = (int8_t)(v + (int8_t)r[0x11]);
        r[0x0c] = (uint8_t)v;

        r[0x12] = r[0x0e];                          /* display mirrors */
        r[0x14] = r[0x10];
        r[0x13] = r[0x11];
        r[0x15] = 0;
        r[0x17] = 0;
        r[0x219] = 0;
        *(int32_t *)(r + 0x194) = 0;

        /* Zero-population realm whose owner is not a human player → reset 50. */
        if (*(int32_t *)(r + 0x24) == 0 &&
            STRAT_REC(r[0x05])[0x05] == 0) {
            r[0x0c] = 0x32;
            r[0x17] = 0x32;
        }
        /* Unowned realm below 75 → nudge up by 5. */
        if ((int8_t)r[0x0c] < 0x4b && r[0x05] == 0) {
            r[0x0c] = (uint8_t)((int8_t)r[0x0c] + 5);
            r[0x17] = 5;
        }
        /* Decay the separate +0x2f4 countdown. */
        if (*(int32_t *)(r + 0x2f4) != 0)
            *(int32_t *)(r + 0x2f4) -= 5;

        /* Clamp 0..100 (signed). */
        if ((int8_t)r[0x0c] < 0)    r[0x0c] = 0;
        if ((int8_t)r[0x0c] > 0x64) r[0x0c] = 100;

        /* Accumulate + running average over the turn count. */
        *(int32_t *)(r + 0x1c) += (int8_t)r[0x0c];
        r[0x18] = (uint8_t)(*(int32_t *)(r + 0x1c) / (int32_t)s_slot_00553240);
    }
}

/* Port of FUN_0044bf04 @ 0x44bf04 (118 B, __stdcall void) — the per-realm
 * provisioning settle pass run by the turn-advance engine (0x448440).  For
 * every realm 1..s_slot_0056d5dc it re-runs the per-army provisioning decision
 * (engine_army_economy_provision) at the season-prev phase code (s_slot_00554470,
 * save entry 24), then snapshots the realm's two settled economy fields into
 * their history slots: +0x18c ← +0x178 and +0x190 ← +0x17c. */
void engine_realm_settle_provisions(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);
        engine_army_economy_provision(key, (int)s_slot_00554470);  /* FUN_0044df5f */
        *(int32_t *)(r + 0x18c) = *(int32_t *)(r + 0x178);
        *(int32_t *)(r + 0x190) = *(int32_t *)(r + 0x17c);
    }
}

/* Port of FUN_0044a6ba — see engine_realm.h for the field overview. */
void engine_realm_migrate_population(void)
{
    int count = (int)s_slot_0056d5dc;

    /* Pass 1 — clear this turn's migration scratch for every realm. */
    for (int key = 1; key <= count; key++) {
        uint8_t *r = ARMY_REC(key);
        *(int32_t *)(r + 0x44) = 0;     /* top-contributor amount */
        *(int32_t *)(r + 0x40) = 0;     /* inflow total */
        *(int32_t *)(r + 0x3c) = 0;     /* outflow amount */
        for (int i = 0; i < 0x10; i++)
            r[0x48 + i] = 0;            /* source-realm list */
        r[0x58] = 0;                    /* chosen destination */
        r[0x59] = 0;                    /* top-contributor key */
    }

    /* Pass 2 — each realm sheds emigrants toward its most-popular neighbour. */
    for (int key = 1; key <= count; key++) {
        uint8_t *r = ARMY_REC(key);

        /* Highest-rated neighbour (baseline = this realm's own rating). */
        int best_rating = (int8_t)r[0x0c];
        int dest = 0;
        for (int i = 0; i < (int)r[0x5a]; i++) {
            int nb = r[0x5c + i];
            int nr = (int8_t)ARMY_REC(nb)[0x0c];
            if (best_rating < nr) {
                best_rating = nr;
                dest = nb;
            }
        }

        int amount;
        if ((int8_t)r[0x0c] < best_rating) {
            int gap    = best_rating - (int8_t)r[0x0c];
            int factor = (100 - (int8_t)r[0x0c]) / 3;
            int rate   = engine_math_percent_of(gap, factor);
            amount = engine_math_percent_of(*(int32_t *)(r + 0x24), rate);
            if (amount > 100)  amount = 100;
            if (r[0x05] == 0)  amount /= 2;     /* unowned realm: halve */
        } else {
            amount = 0;
        }

        if (amount > 0) {
            uint8_t *d = ARMY_REC(dest);
            *(int32_t *)(r + 0x3c) = amount;        /* our outflow */
            *(int32_t *)(d + 0x40) += amount;       /* dest inflow total */
            r[0x58] = (uint8_t)dest;                /* our destination */
            /* Register us as a source in dest's 16-slot list.  NB: retail has
             * NO break here, so the first feeder fills every empty slot. */
            for (int i = 0; i < 0x10; i++) {
                if (d[0x48 + i] == 0)
                    d[0x48 + i] = (uint8_t)key;
            }
        }
    }

    /* Pass 3 — record each recipient's single largest contributor. */
    for (int key = 1; key <= count; key++) {
        uint8_t *r = ARMY_REC(key);
        if (*(int32_t *)(r + 0x40) <= 0)
            continue;
        int best = 0;
        for (int i = 0; i < 0x10; i++) {
            uint8_t src = r[0x48 + i];
            if (src == 0)
                break;
            int contrib = *(int32_t *)(ARMY_REC(src) + 0x3c);
            if (best < contrib) {
                best = contrib;
                *(int32_t *)(r + 0x44) = contrib;
                r[0x59] = src;
            }
        }
    }
}

/* Port of FUN_004ad6c0 — see engine_realm.h for the field overview. */
void engine_realm_collect_territory_income(void)
{
    /* Pass 1 — clear both per-realm income accumulators (realms 1..16). */
    for (int key = 1; key < 0x11; key++) {
        uint8_t *r = ARMY_REC(key);
        *(int32_t *)(r + 0x198) = 0;
        *(int32_t *)(r + 0x19c) = 0;
    }

    /* Pass 2 — fold each eligible territory's value (+0x168) into its owning
     * realm's "own/allied" (+0x198) or "foreign" (+0x19c) accumulator. */
    for (int t = 1; t < 0x97; t++) {
        uint8_t *terr = TERR_REC(t);

        uint8_t status = terr[0x08];
        if ((status != 1 && status != 2) || terr[0x198] != 0)
            continue;                       /* not a counted territory */

        int      owner_realm = terr[0x10];          /* owning realm key */
        int      terr_owner  = (int8_t)terr[0x00];  /* territory's owner field */
        int32_t  value       = *(int32_t *)(terr + 0x168);

        uint8_t *r = ARMY_REC(owner_realm);
        int      realm_owner = r[0x05];

        /* Own (realm's owner == territory's) or allied (territory owner's
         * strategic +0x81 names the realm's owner) → +0x198, else +0x19c. */
        if (realm_owner == terr_owner ||
            STRAT_REC(terr_owner)[0x81] == realm_owner)
            *(int32_t *)(r + 0x198) += value;
        else
            *(int32_t *)(r + 0x19c) += value;
    }

    /* Pass 3 — re-run the per-realm economy plan (provision → production →
     * candidate) for every realm 1..16 at the current season (s_slot_0057c92c). */
    int phase = (int)s_slot_0057c92c;
    for (int key = 1; key < 0x11; key++) {
        engine_army_economy_provision(key, phase);         /* FUN_0044df5f */
        engine_army_economy_plan_production(key, phase);   /* FUN_0044d374 */
        engine_army_economy_select_candidate(key, phase);  /* FUN_0044dd4d */
    }
}

/* Port of FUN_004ae7dd (204 B) — the per-turn realm stat-history recorder, a
 * pass of the turn-advance engine 0x448440 (FUN_00448440 calls it last, right
 * after the second workforce dispatch).  Once per turn it snapshots every
 * realm's population and approval into the history ring g_realm_history (save
 * entry 10) at the current write slot, then advances the ring cursors.  The
 * recorded data is what the in-game population / popularity history graphs plot.
 *
 * The ring is a 400-turn × 16-realm grid of 8-byte records.  The write slot is
 * s_slot_0055300c (0..399); within it realm `key` (1..16) owns the record at
 * g_realm_history + slot*0x80 + (key-1)*8 — equivalently record index
 * slot*16 + (key-1).  (Retail addresses it off base − 8 with a 1-based key, so
 * key*8 lands the same offset; we index the array directly to stay in-bounds.)
 * Each record takes the realm's population (army row +0x24, dword) at +0 and
 * its approval rating (army row +0x0c, byte) at +4; +5..+7 are left untouched —
 * the init seeder (engine_game_init_clear_realm_history) clears them, the
 * producer never writes them.
 *
 * After recording, three cursors advance: the entry count s_slot_00553f30
 * saturates at 400; once it would exceed 400 the oldest-page pointer
 * s_slot_00568da8 walks forward, wrapping at 400; the write index
 * s_slot_0055300c advances and wraps at 400.  r2 @0x4ae7dd confirms the slot
 * stride (0x80 = 16×8), the {dword,byte} record fields, the army source
 * offsets (+0x24 / +0x0c off the 0x53f9b0 / 0x300 block), and the three 0x190
 * (400) bound checks.  Touches only the shared save block g_realm_history, the
 * army records g_per_army_768, and the three ring-cursor scalars — no new
 * globals. */
void engine_realm_record_history(void)
{
    int slot = (int)s_slot_0055300c;
    for (int key = 1; key < 0x11; key++) {              /* realms 1..16 */
        const uint8_t *r = ARMY_REC(key);
        uint8_t *rec = g_realm_history + slot * 0x80 + (key - 1) * 8;
        *(int32_t *)(rec + 0) = *(const int32_t *)(r + 0x24);  /* population */
        rec[4]                = r[0x0c];                        /* approval   */
    }

    if (++s_slot_00553f30 > 400) {                     /* count saturates at 400 */
        s_slot_00553f30 = 400;
        if (++s_slot_00568da8 >= 400)                  /* oldest page wraps at 400 */
            s_slot_00568da8 = 0;
    }
    if (++s_slot_0055300c >= 400)                      /* write index wraps at 400 */
        s_slot_0055300c = 0;
}

/* Port of FUN_00448d16 (104 B).  Per-realm mid-turn scratch reset: for
 * realms 1..s_slot_0056d5dc, zero four turn-accumulator bytes in each
 * army record — the three consecutive +0x1fb/+0x1fc/+0x1fd and the lone
 * +0x1a8.  These are a subset of the per-realm bytes the turn-start pass
 * (retail 0x448819) clears; the turn-advance engine (retail 0x448440)
 * re-zeroes them here, after the territory-income/economy re-plan and
 * just before the second workforce-allocation dispatch, so the counters
 * start clean for that pass.  Touches only g_per_army_768. */
void engine_realm_clear_turn_scratch(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);
        r[0x1fb] = 0;
        r[0x1fc] = 0;
        r[0x1fd] = 0;
        r[0x1a8] = 0;
    }
}
