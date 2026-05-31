/*
 * OpenLords2 — per-realm strategic turn-state updates.  See engine_realm.h.
 */

#include <stdint.h>

#include "engine_realm.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352,
                                      g_per_territory_420, g_misc_1360,
                                      g_overworld_map */
#include "save_schema_globals.h"   /* s_slot_0056d5dc, s_slot_00553240,
                                      s_slot_0057c92c, s_slot_0057c934,
                                      s_slot_0057c8cc, s_slot_00554020,
                                      s_slot_0053f25c */
#include "engine_army_economy.h"   /* engine_army_economy_provision / _plan_production
                                      / _select_candidate */
#include "engine_math.h"           /* engine_math_percent_of / _lookup_threshold */
#include "engine_territory.h"      /* engine_territory_set_cell_type (0x46d7f4) */
#include "engine_event_queue.h"    /* engine_event_queue_post (0x472bc5) */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])
#define TERR_REC(idx)   (&g_per_territory_420[(idx) * 0x1a4])
/* Per-realm overworld-cell roster: 0x50-byte rows of 20 i32 cell offsets,
 * shared with engine_realm_events.c (retail 0x53ea00 = g_misc_1360). */
#define MISC_ROW(realm) ((int32_t *)&g_misc_1360[(realm) * 0x50])

/* Tax-policy → approval-modifier tables, baked from the retail .rdata region
 * 0x4d64a8..0x4d6548 (r2-verified).  Two logically-distinct tables that sit
 * back-to-back in memory, baked as ONE contiguous array because the producer
 * (engine_realm_apply_tax_approval) reads ACROSS the boundary (see quirk #149):
 *
 *   [0..29]  — the 2D tax-approval adjustment table (retail 0x4d64a8): 6
 *              tax-policy rows (stride 5 i32) × 5 standing-band columns, added
 *              to the realm's tax-approval value (+0x0b).
 *   [30..39] — the band-threshold table (retail 0x4d6520): five (cutoff, band)
 *              i32 pairs fed to engine_math_lookup_threshold.
 *
 * Retail's tax-policy 6 has NO row of its own; it indexes 6*5 = 30 entries in,
 * landing squarely in the threshold pairs (quirk #149), which is only faithful
 * if the two tables are one array. */
static const int32_t k_tax_approval_tbl[40] = {
    /* policy 0 */  -8, -10, -13, -16, -20,
    /* policy 1 */  -4,  -6,  -9, -12, -15,
    /* policy 2 */  -2,  -4,  -6,  -8, -12,
    /* policy 3 */   8,   4,   2,   1,  -1,
    /* policy 4 */  12,   8,   4,   2,   0,
    /* policy 5 */  20,  12,   6,   3,   1,
    /* threshold (cutoff, band) ×5 */
       10, 0,   35, 1,   65, 2,   90, 3,   100, 4,
};
#define K_TAX_THRESHOLD (&k_tax_approval_tbl[30])

/* Per-band approval modifier (retail 0x4d6548), indexed by the band 0..4 the
 * threshold lookup yields.  Retail byte-reads the low byte of each i32 entry. */
static const int8_t k_tax_band_modifier[5] = { -10, -5, 0, 1, 2 };

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

/* ─── the per-realm mood / standing machine (FUN_00449889) ───────────────
 * Three private helpers, then the pass.  All operate on the per-army record
 * block; the standing fields used here are:
 *   +0x05  owner (player slot)        +0x21b  standing level (band 0..5)
 *   +0x5a  neighbour count            +0x21c  previous standing level
 *   +0x5c  neighbour list (bytes)     +0x21d  mood accumulator (i8)
 *   +0x15b roster cursor (u8)         +0x21e  disposition byte (i8)
 *   +0x205 roster cursor wrap (u8)
 */

/* Port of FUN_00449d6e — the per-realm mood delta keyed on the turn's
 * event-mode (s_slot_0057c934, season 1..4) and the realm's disposition byte
 * (army +0x21e).  Only modes 2 and 4 contribute; everything else (and any
 * unhandled disposition value) returns 0.  Faithful quirks (#147): mode 2
 * never handles disposition 3 and has a dead duplicate `disp == 4` arm whose
 * −24 return is unreachable (the first `disp == 4` arm returns −12). */
static int realm_mood_delta(int realm)
{
    uint8_t disp = ARMY_REC(realm)[0x21e];
    int mode = (int)s_slot_0057c934;

    if (mode == 2) {
        if (disp == 0) return 4;
        if (disp == 1) return 2;
        if (disp == 2) return -8;
        if (disp == 4) return -12;
        /* retail's second `disp == 4 → -24` arm is dead code */
    } else if (mode == 4) {
        if (disp == 1) return -2;
        if (disp == 2) return -4;
        if (disp == 3) return -6;
        if (disp == 4) return -10;
    }
    return 0;
}

/* Port of FUN_0046942c — clear any of the realm's 20 roster cells whose
 * overworld type is one of the two standing markers (0x17 / 0x18) back to
 * type 0, via the territory module (a no-op when the cell grid is unbound). */
static void realm_clear_marker_cells(int realm)
{
    const int32_t *row = MISC_ROW(realm);
    for (int i = 0; i < 0x14; i++) {
        int32_t cell = row[i];
        if (cell != 0) {
            uint8_t type = g_overworld_map[cell];
            if (type == 0x17 || type == 0x18)
                engine_territory_set_cell_type(cell, 0, 0);
        }
    }
}

/* Port of FUN_00469a9c — advance the realm's round-robin roster cursor
 * (army +0x15b, wrapping once it reaches the per-realm limit +0x205) to the
 * next non-empty roster cell, probing at most 20 slots, and set that cell's
 * overworld type to `type` (a standing marker).  If all 20 probes land on an
 * empty slot, nothing is flipped.  The cursor is an unsigned byte and wraps
 * at the realm's roster count, so it never indexes past the realm's row. */
static void realm_mark_next_cell(int realm, int type)
{
    uint8_t       *a   = ARMY_REC(realm);
    const int32_t *row = MISC_ROW(realm);

    for (int i = 0; i < 0x14; i++) {
        a[0x15b]++;
        if (a[0x205] <= a[0x15b])
            a[0x15b] = 0;
        if (row[a[0x15b]] != 0) {
            engine_territory_set_cell_type(row[a[0x15b]], type, 0);
            return;
        }
    }
}

/* Add a signed byte delta into a realm's mood accumulator (+0x21d) with the
 * 8-bit wraparound retail gets from storing the char back each time. */
static void mood_add(uint8_t *a, int delta)
{
    a[0x21d] = (uint8_t)((int8_t)a[0x21d] + (int8_t)delta);
}

/* Notify the local player of a realm hitting a standing extreme.  Mirrors
 * retail's FUN_00472bc5(0, local_player, code, 0,0,0,0,0): target 0, audience
 * the local player (so the audience==local filter always publishes).  The
 * player_has_data[7] filter table is rebuilt from each player's strategic
 * +0x05 the same way the ranking producer does. */
static void mood_notify(engine_event_queue_state_t *evq, int code)
{
    uint8_t has_data[7];
    for (int i = 0; i < 6; i++)
        has_data[i] = STRAT_REC(i)[0x05];
    has_data[6] = 0;
    engine_event_queue_post(evq, 0, (int)s_slot_0057c8cc, code, 0,
                            0, 0, 0, 0, has_data, (int)s_slot_0057c8cc);
}

/* Port of FUN_00449889 — see engine_realm.h for the full overview. */
void engine_realm_update_mood(uint32_t rng_7bit_b, uint32_t rng_7bit_a,
                              engine_event_queue_state_t *evq)
{
    int count = (int)s_slot_0056d5dc;
    int mode  = (int)s_slot_0057c934;

    /* 1. Snapshot each realm's current standing level into "previous". */
    for (int key = 1; key <= count; key++)
        ARMY_REC(key)[0x21c] = ARMY_REC(key)[0x21b];

    /* 2. This turn's base mood delta: a per-mode constant minus rng_b/8
     *    (signed division, truncating toward zero — rng_b is a 7-bit value).
     *    Retail leaves the delta uninitialised for mode ∉ {1,2,3,4}; the
     *    orchestrator only ever feeds modes 1..4, so we treat the impossible
     *    case as 0. */
    int delta;
    if      (mode == 1) delta = 8     - (int)rng_7bit_b / 8;
    else if (mode == 2) delta = 0x18  - (int)rng_7bit_b / 8;
    else if (mode == 3) delta = 0xc   - (int)rng_7bit_b / 8;
    else if (mode == 4) delta = -0xc  - (int)rng_7bit_b / 8;
    else                delta = 0;

    /* 3. Apply the base delta to every realm's mood accumulator. */
    for (int key = 1; key <= count; key++)
        mood_add(ARMY_REC(key), delta);

    /* 4. Pick a "focus" realm from rng_a (low nibble), falling back to the
     *    last focus + 1 then realm 1, and clamp into 1..count.  It takes an
     *    extra base dose; its neighbours take half-doses.  Each also folds in
     *    its event-mode mood delta. */
    int sel = (int)(rng_7bit_a & 0xf);
    if (count < sel) sel = (int)s_slot_00554020 + 1;
    if (count < sel) sel = 1;
    if (sel < 1)     sel = 1;
    s_slot_00554020 = (uint32_t)sel;

    {
        uint8_t *a = ARMY_REC(sel);
        mood_add(a, delta);
        mood_add(a, realm_mood_delta(sel));

        int n = a[0x5a];
        for (int i = 0; i < n; i++) {
            int nb = a[0x5c + i];
            if (nb == 0)
                break;
            uint8_t *na = ARMY_REC(nb);
            mood_add(na, delta / 2);
            mood_add(na, realm_mood_delta(nb));
        }
    }

    /* 5. Classify each realm's accumulator into a 5-band standing level, run
     *    the per-band side effects, and (in modes 1 and 4) demote the two
     *    best bands toward collapse. */
    for (int key = 1; key <= count; key++) {
        uint8_t *a = ARMY_REC(key);
        int8_t mood = (int8_t)a[0x21d];

        if (mood < 5) {
            a[0x21b] = 5;
            a[0x21d] = 0x1e;
        } else if (mood < 0x14) {
            a[0x21b] = 4;
        } else if (mood < 0x46) {
            a[0x21b] = 3;
        } else if (mood < 0x5f) {
            a[0x21b] = 2;
        } else {
            a[0x21b] = 1;
            a[0x21d] = 0x46;
        }

        if (mode == 4 || mode == 1) {
            if (a[0x21b] == 1)
                a[0x21b] = 0;
            else if (a[0x21b] == 2 && (int8_t)a[0x21d] > 0x4a)
                a[0x21b] = 0;
        }

        realm_clear_marker_cells(key);

        if (a[0x21b] == 1) {
            if ((uint32_t)a[0x05] == s_slot_0057c8cc)
                mood_notify(evq, 0x8f);
            realm_mark_next_cell(key, 0x18);
        }
        if (a[0x21b] == 5) {
            if ((uint32_t)a[0x05] == s_slot_0057c8cc)
                mood_notify(evq, 0x90);
            realm_mark_next_cell(key, 0x17);
        }
    }

    /* 6. Standings disabled (s_slot_0053f25c == 0) → force every realm to the
     *    neutral middle band. */
    if (s_slot_0053f25c == 0) {
        for (int key = 1; key <= count; key++)
            ARMY_REC(key)[0x21b] = 3;
    }
}

/* Port of FUN_0044b99a — recompute a player's owned-realm tally.  Clears the
 * player's strategic +0x28 byte, then sums every realm's +0x16 byte over the
 * realms that player owns (army +0x05 == player).  The byte tally feeds the
 * per-realm tax-rate adjustment in engine_realm_accrue_income.  Shared with
 * the (unported) sibling 0x44b80b.  Field semantics (+0x16 / +0x28)
 * provisional; widths (both bytes) match the decompile. */
void engine_realm_recompute_owner_tally(int player)
{
    uint8_t *s = STRAT_REC(player);
    s[0x28] = 0;
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);
        if (r[0x05] == (uint8_t)player)
            s[0x28] = (uint8_t)(s[0x28] + r[0x16]);
    }
}

/* Port of FUN_0044b59b — the per-realm tax-income pass (a turn-advance pass of
 * 0x448440, run right after the mood machine).  For every realm 1..count it
 * recomputes the owner's realm tally, derives a per-capita tax rate from the
 * realm's standing band, scales it by population and the realm's tax setting,
 * and credits the resulting income to the owner's treasury accounts (or, for
 * an unowned realm, to a per-realm pool).  See engine_realm.h for the field
 * overview. */
void engine_realm_accrue_income(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);
        int owner = r[0x05];

        engine_realm_recompute_owner_tally(owner);

        /* Effective standing band (+0x1c0), optionally capped by +0x1c1 when
         * the +0x1c3 gate is set. */
        uint8_t band = r[0x1c0];
        uint8_t cap  = r[0x1c1];
        if (r[0x1c3] != 0) {
            if (cap == 0)        band = 0;
            else if (cap < band) band = cap;
        }

        /* Per-capita tax rate by band — unless the realm's +0x1a8 turn-scratch
         * is set (e.g. by event 0x13b), which zeroes income this turn. */
        int rate;
        if (r[0x1a8] == 0) {
            switch (band) {
            case 1:  rate = 0x1e0; break;   /* 480 */
            case 2:  rate = 0x230; break;   /* 560 */
            case 3:  rate = 0x280; break;   /* 640 */
            case 4:  rate = 0x2d0; break;   /* 720 */
            case 5:  rate = 800;   break;
            default: rate = 0x140; break;   /* 320 (band 0 / out of range) */
            }
        } else {
            rate = 0;
        }

        /* income = pop · rate% · taxsetting%  (two percent_of stages). */
        int income = engine_math_percent_of(*(int32_t *)(r + 0x24), rate);
        income = engine_math_percent_of(income, r[0xb9]);
        *(int32_t *)(r + 0xbc) = income;

        /* Per-realm rate adjustment byte: (5 − taxsetting) + owner tally. */
        r[0x0e] = (uint8_t)(5 - (int8_t)r[0xb9]);
        r[0x0e] = (uint8_t)((int8_t)STRAT_REC(owner)[0x28] + (int8_t)r[0x0e]);

        if (owner == 0) {
            *(int32_t *)(r + 0x1f4) += income;
        } else {
            uint8_t *s = STRAT_REC(owner);
            *(int32_t *)(s + 0x118) += income;
            *(int32_t *)(s + 0x0f4) += income;
            *(int32_t *)(s + 0x0f8) += income;
        }
    }
}

/* Per-tax-setting income-tally modifier (retail .data 0x4d63d8, indexed by the
 * realm's tax setting +0xb9 × 4 — the read takes the low byte of each i32
 * entry).  Indices 0..19 are zero; from index 20 (a 50%/maxed-out tax) the
 * modifier ramps negative, so a punishingly high tax rate subtracts from the
 * owning player's order tally.  The tax setting is incremented only while
 * < 0x32, so 0..50 is its live range; baked for exactly that range, r2-verified.
 * It lives in writable .data but nothing in the binary ever writes it, so it is
 * effectively a const lookup. */
static const int32_t k_taxsetting_tally_modifier[51] = {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   /*  0.. 9 */
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   /* 10..19 */
     -1, -1, -1, -1,                          /* 20..23 */
     -2, -2, -2, -2,                          /* 24..27 */
     -3, -3, -3, -3,                          /* 28..31 */
     -4, -4, -4,                              /* 32..34 */
     -5, -5, -5,                              /* 35..37 */
     -6, -6,                                  /* 38..39 */
     -7, -7,                                  /* 40..41 */
     -8, -8,                                  /* 42..43 */
     -9, -10, -11, -12, -13, -14, -15,        /* 44..50 */
};

/* Port of FUN_0044ba35 (181 B) — refresh every player's projected-income total.
 * Clears the income-summary field +0x15c for players 1..5, then folds each
 * realm's projected income (+0xc0) into its owning player's total (owner byte
 * +0x05 != 0).  Exclusive to engine_realm_recompute_realm_income. */
static void realm_refresh_player_income_totals(void)
{
    for (int p = 1; p < 6; p++)
        *(int32_t *)(STRAT_REC(p) + 0x15c) = 0;
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r     = ARMY_REC(key);
        int      owner = r[0x05];
        if (owner != 0)
            *(int32_t *)(STRAT_REC(owner) + 0x15c) += *(int32_t *)(r + 0xc0);
    }
}

/* Port of FUN_0044b80b — see engine_realm.h for the field overview. */
void engine_realm_recompute_realm_income(int realm)
{
    uint8_t *r = ARMY_REC(realm);

    /* Effective standing band (+0x1c0), optionally capped by +0x1c1 when the
     * +0x1c3 gate is set — identical rule to engine_realm_accrue_income. */
    uint8_t band = r[0x1c0];
    uint8_t cap  = r[0x1c1];
    if (r[0x1c3] != 0) {
        if (cap == 0)        band = 0;
        else if (cap < band) band = cap;
    }

    /* Per-capita tax rate by band.  Note: unlike the accrual pass this has NO
     * +0x1a8 event-scratch gate — the projection is always computed. */
    int rate;
    switch (band) {
    case 1:  rate = 0x1e0; break;   /* 480 */
    case 2:  rate = 0x230; break;   /* 560 */
    case 3:  rate = 0x280; break;   /* 640 */
    case 4:  rate = 0x2d0; break;   /* 720 */
    case 5:  rate = 800;   break;
    default: rate = 0x140; break;   /* 320 (band 0 / out of range) */
    }

    /* income = pop · rate% · taxsetting%, stored to the PROJECTED field +0xc0
     * (distinct from the accrual field +0xbc). */
    uint8_t tax    = r[0xb9];
    int     income = engine_math_percent_of(*(int32_t *)(r + 0x24), rate);
    income = engine_math_percent_of(income, tax);
    *(int32_t *)(r + 0xc0) = income;

    /* +0x0f = 5 − taxsetting (signed byte); +0x16 = the tax-setting tally
     * modifier the owner-tally pass sums into the owner's +0x28. */
    r[0x0f] = (uint8_t)(5 - (int8_t)tax);
    r[0x16] = (uint8_t)k_taxsetting_tally_modifier[tax];

    engine_realm_recompute_owner_tally(r[0x05]);
    realm_refresh_player_income_totals();
}

/* Port of FUN_00451061 — the per-realm army recount pass (a turn-advance pass
 * of 0x448440).  Resets each realm's army-count scratch, then walks the
 * territory-record block counting every placed army (record kind +0x08 == 3,
 * the kind engine_territory_place_entity stamps for the army-start path) into
 * its owning realm (record +0x10).  See engine_realm.h.
 *
 * QUIRK (#148): the byte count +0x1a4 is reset (pass 1) then recounted, but the
 * i32 count +0x1a0 is incremented in lockstep WITHOUT being reset, so it
 * accumulates across turns while the byte holds the live count. */
void engine_realm_recount_armies(void)
{
    /* Pass 1 — clear each realm's army-count scratch (and the unrelated +0x15c
     * flag retail piggybacks onto this reset). */
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);
        r[0x1a4] = 0;       /* live army count (byte)          */
        r[0x15c] = 0;       /* unrelated per-realm flag         */
        r[0x1a5] = 0;       /* last counted army's record index */
    }

    /* Pass 2 — tally each kind-3 territory record into its owner realm. */
    for (int t = 1; t < 0x97; t++) {
        uint8_t *terr = TERR_REC(t);
        if (terr[0x08] == 3) {
            uint8_t *r = ARMY_REC(terr[0x10]);
            r[0x1a4] = (uint8_t)(r[0x1a4] + 1);
            *(int32_t *)(r + 0x1a0) += 1;
            r[0x1a5] = (uint8_t)t;
        }
    }
}

/* Port of FUN_0044bd9f — see engine_realm.h for the field overview. */
void engine_realm_apply_tax_approval(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r     = ARMY_REC(key);
        int      owner = r[0x05];

        int policy = (int8_t)r[0x15d];      /* tax policy (0..6)               */
        int band   = (int8_t)r[0x09];       /* column = last turn's band       */

        /* Fold the policy/band adjustment into the tax-approval value (+0x0b),
         * truncating to a signed byte as retail stores AL back. */
        r[0x0b] = (uint8_t)((int8_t)r[0x0b] + k_tax_approval_tbl[policy * 5 + band]);

        /* Clamp 0..100 (signed). */
        if ((int8_t)r[0x0b] < 0)    r[0x0b] = 0;
        if ((int8_t)r[0x0b] > 0x64) r[0x0b] = 100;

        /* Zero-population realm with a non-human owner → reset to 50. */
        if (*(int32_t *)(r + 0x24) <= 0 && STRAT_REC(owner)[0x05] == 0)
            r[0x0b] = 0x32;

        /* Reclassify into a 5-band level and emit the per-band modifier the
         * approval roll-forward (engine_realm_advance_rating) folds into the
         * main rating via its +0x10 adjustment source. */
        int new_band = engine_math_lookup_threshold((int8_t)r[0x0b],
                                                    K_TAX_THRESHOLD, 5, 3);
        r[0x09] = (uint8_t)new_band;
        r[0x10] = (uint8_t)k_tax_band_modifier[new_band];
    }
}

/* ─── the per-realm territory-development spread pass (FUN_0044bf7a) ──────────
 * The turn-advance engine's per-realm "growth distribution" pass.  Each realm
 * owns up to 20 developable overworld cells, listed as cell offsets in its
 * g_misc_1360 roster row (MISC_ROW); the per-realm word array at +0x90 holds
 * each cell's development VALUE (0..800), and +0xdc is the surplus "pool" to be
 * spread across them this turn.  An overworld cell is developable only when it
 * is non-empty and its terrain-type byte (g_overworld_map[cell]) is >= 0x19 —
 * i.e. it is already one of the development-stage tiles (0x19..0x1c).  As a
 * cell's value climbs the pass restamps it 0x19/0x1a/0x1b/0x1c, and a maxed
 * cell (value >= 800) is stamped 0x01 — which drops it BELOW 0x19, so a fully
 * developed cell stops qualifying on later turns.  Field semantics (the +0x90 values, the
 * +0xdc pool, the +0x1ff/+0x201/+0x208 accumulator and the projection outputs)
 * are provisional; all offsets and the band cut-offs are r2-verified against
 * the 0x300 record stride. */

/* Ports of FUN_0044c5fc / FUN_0044c53b — find the developable roster cell with
 * the highest development value (+0x90 word), write its slot index to +0x210,
 * and return it.  The two retail variants differ ONLY in the value used when no
 * cell qualifies: 0x44c5fc defaults to 99 (a "no cell" sentinel FUN_0044c278
 * tests for), 0x44c53b defaults to 0.  Unified here behind a default param. */
static int realm_dev_best_cell(int realm, int default_idx)
{
    uint8_t       *a    = ARMY_REC(realm);
    const int32_t *row  = MISC_ROW(realm);
    const int16_t *vals = (const int16_t *)(a + 0x90);

    int best   = -1;
    int result = default_idx;
    for (int i = 0; i < 0x14; i++) {
        int32_t cell = row[i];
        if (cell == 0 || g_overworld_map[cell] < 0x19)
            continue;
        if (vals[i] > best) {
            best   = vals[i];
            result = i;
        }
    }
    *(int32_t *)(a + 0x210) = result;
    return result;
}

/* Port of FUN_0044bfd5 — roll a realm's standing accumulator (+0x208) forward
 * by 6·(+0x1ff) − 3·(+0x201) (both unsigned-byte inputs), clamp it to
 * [-100, 100], and force it to 0 while standings are disabled (s_slot_0053f25c
 * == 0, the same gate the mood machine honours). */
static void realm_dev_roll_accumulator(int realm)
{
    uint8_t *a   = ARMY_REC(realm);
    int32_t *acc = (int32_t *)(a + 0x208);

    *acc += 6 * a[0x1ff];
    *acc -= 3 * a[0x201];

    if (*acc < -100) *acc = -100;
    if (*acc > 100)  *acc = 100;

    if (s_slot_0053f25c == 0)
        *acc = 0;
}

/* Port of FUN_0044c093 — distribute the realm's pool (+0xdc) across its
 * developable cells, starting from the highest-value cell and walking the
 * 20-slot roster circularly.  Each visited cell takes up to 200 from the pool;
 * any value above 800 spills back into the pool; the cell's stored value (+0x90
 * word) is updated and its overworld terrain type is stamped to the matching
 * development stage (<200 → 0x19, <400 → 0x1a, <600 → 0x1b, <800 → 0x1c, else
 * 0x01).  Stops once the pool is exhausted (or after 20 visits).
 *
 * QUIRK #150: retail calls the default-99 argmax (FUN_0044c5fc) first and then
 * immediately the default-0 argmax (FUN_0044c53b), overwriting +0x210 before
 * the first result is ever read — so the first call is dead work here.  The
 * port keeps it to match the writes to +0x210. */
static void realm_dev_spread_pool(int realm)
{
    uint8_t       *a    = ARMY_REC(realm);
    const int32_t *row  = MISC_ROW(realm);
    int16_t       *vals = (int16_t *)(a + 0x90);

    realm_dev_best_cell(realm, 99);             /* FUN_0044c5fc — result unused */
    int pool = *(int32_t *)(a + 0xdc);
    int slot = realm_dev_best_cell(realm, 0);   /* FUN_0044c53b — start cell    */
    if (slot >= 0x14)
        return;

    for (int n = 0; n < 0x14; n++) {
        int32_t cell = row[slot];
        if (cell != 0 && g_overworld_map[cell] >= 0x19) {
            int v = vals[slot];
            if (pool > 0xc8) { v += 0xc8; pool -= 0xc8; }
            else             { v += pool;  pool  = 0;   }
            if (v > 0x320)
                pool += v - 0x320;              /* overflow above 800 spills back */

            int type;
            if      (v < 0xc8)  type = 0x19;
            else if (v < 0x190) type = 0x1a;
            else if (v < 0x258) type = 0x1b;
            else if (v < 0x320) type = 0x1c;
            else                type = 0x01;
            engine_territory_set_cell_type(cell, type, 0);

            vals[slot] = (int16_t)v;
            if (pool <= 0)
                return;
        }
        if (++slot >= 0x14)
            slot = 0;
    }
}

/* Port of FUN_0044c278 — project (without mutating the cell values) how the
 * realm's pool would spread, leaving four planning metrics in the record.
 * (The retail caller also passes the season, which this pass never reads.)
 *
 *   1. +0xe4  total fill capacity: Σ over developable cells of min(800−value,
 *             200); also resets +0xe0 to −1 and clears +0x20c / +0x214.
 *   2. Bail if no developable cell exists (+0x210 still the default-99 sentinel
 *      from FUN_0044c5fc).
 *   3. +0x20c  count of cells that would reach the 800 cap, by dry-running the
 *      same circular distribution FUN_0044c093 performs (pool unchanged on the
 *      record; the spill-back is tracked only in a local).
 *   4. +0x214  turns to fill the best cell: ceil((800 − best-cell value) /
 *      min(pool, 200)); left 0 when the pool is non-positive. */
static void realm_dev_project_pool(int realm)
{
    uint8_t       *a    = ARMY_REC(realm);
    const int32_t *row  = MISC_ROW(realm);
    const int16_t *vals = (const int16_t *)(a + 0x90);

    realm_dev_best_cell(realm, 99);             /* FUN_0044c5fc — sets the +0x210 sentinel */

    /* 1. Total fill capacity (headroom to 800, capped at 200/cell). */
    int capacity = 0;
    for (int i = 0; i < 0x14; i++) {
        int32_t cell = row[i];
        if (cell == 0 || g_overworld_map[cell] < 0x19)
            continue;
        int head = 0x320 - vals[i];
        if (head > 0xc8)
            head = 0xc8;
        capacity += head;
    }
    *(int32_t *)(a + 0xe0)  = -1;
    *(int32_t *)(a + 0xe4)  = capacity;
    *(int32_t *)(a + 0x20c) = 0;
    *(int32_t *)(a + 0x214) = 0;

    if (*(int32_t *)(a + 0x210) >= 0x63)        /* no developable cell → done */
        return;

    /* 2-3. Dry-run distribution, counting cells that would hit the 800 cap. */
    realm_dev_best_cell(realm, 0);              /* FUN_0044c53b — start cell */
    int pool = *(int32_t *)(a + 0xdc);
    int slot = *(int32_t *)(a + 0x210);
    for (int n = 0; n < 0x14; n++) {
        int32_t cell = row[slot];
        if (cell != 0 && g_overworld_map[cell] >= 0x19) {
            int v = vals[slot];
            if (pool > 0xc8) { v += 0xc8; pool -= 0xc8; }
            else             { v += pool;  pool  = 0;   }
            if (v >= 0x320) {
                pool += v - 0x320;
                *(int32_t *)(a + 0x20c) += 1;
            }
            if (pool <= 0)
                break;
        }
        if (++slot >= 0x14)
            slot = 0;
    }

    /* 4. Turns to fill the best cell with the (per-turn-capped) pool. */
    pool = *(int32_t *)(a + 0xdc);
    slot = *(int32_t *)(a + 0x210);
    int head = 0x320 - vals[slot];
    if (pool <= 0)
        return;
    if (pool > 0xc8)
        pool = 0xc8;
    *(int32_t *)(a + 0x214) = head / pool;
    if (head % pool != 0)
        *(int32_t *)(a + 0x214) += 1;
}

/* Port of FUN_0044bf7a — see engine_realm.h for the overview. */
void engine_realm_spread_development(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        realm_dev_roll_accumulator(key);   /* FUN_0044bfd5 */
        realm_dev_spread_pool(key);        /* FUN_0044c093 */
        realm_dev_project_pool(key);       /* FUN_0044c278 */
    }
}

/* Port of FUN_00448545 — see engine_realm.h for the overview. */
void engine_realm_replan_economies(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        engine_army_economy_provision(key, (int)s_slot_0057c934);  /* FUN_0044df5f */
        engine_army_economy_plan_realm(key, (int)s_slot_0057c92c); /* FUN_004485a5 */
        engine_realm_recompute_realm_income(key);                  /* FUN_0044b80b */
    }
}
