/*
 * OpenLords2 — strategic-map army combat resolution.  See engine_combat.h.
 *
 * Verbatim port of FUN_004aa89f / FUN_00422c12 / FUN_004ab2aa / FUN_004aad07.
 * All four operate purely on the per-territory occupant records of
 * g_per_territory_420 (stride 0x1a4) plus, on annihilation, the mercenary-pool
 * table g_unit_table_20 (through the already-ported engine_unit_clear_contract,
 * FUN_004ac6fe).  No RNG, no audio, no rendering — the resolution is a
 * deterministic function of the two combatants' troop counts, so the resulting
 * save-state bytes match retail exactly.
 *
 * The two const tables are baked verbatim from Lords2.exe:
 *   k_unit_strength  ← DAT_004d4b98 (7 × i32)  — per-class combat weight
 *   k_casualty_table ← DAT_004de710 (10 pairs) — strength-ratio → survivor %
 */

#include <stdint.h>

#include "engine_combat.h"
#include "save.h"                 /* g_per_territory_420, g_unit_table_20 */
#include "engine_realm_upkeep.h"  /* engine_unit_clear_contract (FUN_004ac6fe) */
#include "engine_math.h"          /* percent_of / percent_div / lookup_threshold */

/* --- per-territory occupant ("army") record fields (stride 0x1a4) ---------
 * Shared with engine_realm_upkeep.c's UNIT_* layout; named here for the
 * combat-relevant subset.  Bytes unless marked. */
#define CSTRIDE      0x1a4
#define CREC(i)      (&g_per_territory_420[(i) * CSTRIDE])

#define C_OWNER      0x00    /* owning player id (i8)                         */
#define C_GARRISON   0x01    /* castle/garrison present → interactive battle (u8)*/
#define C_COUNT      0x02    /* battle troop count for sizing (u8; floored to 6)*/
#define C_VALUE      0x168   /* total head-count, re-summed here (i32)        */
#define C_COUNTS     0x16c   /* 7 × troop-class count (i16)                   */
#define C_RESULT     0x17a   /* 4 × battle-result accumulator (i16)           */
#define C_CON_CLASS  0x195   /* merc contingent's troop class (u8)            */
#define C_CON_AMT    0x196   /* merc contingent head-count (u8)               */

/* Per-class combat weight (DAT_004d4b98): peasant .. the heaviest unit. */
static const int32_t k_unit_strength[7] = { 2, 16, 8, 13, 9, 13, 22 };

/* Strength-ratio (winner/loser × 100) → winner survivor percent
 * (DAT_004de710, walked as 10 {threshold, value} pairs by lookup_threshold;
 * default 100 when the ratio exceeds the last threshold). */
static const int k_casualty_table[20] = {
    110, 10,  130, 20,  160, 30,  180, 40,  220, 50,
    270, 65,  360, 80,  500, 90,  700, 95,  900, 98,
};

int32_t g_combat_muster_a[7];   /* retail 0x568420 */
int32_t g_combat_muster_b[7];   /* retail 0x56843c */

static int16_t  rd_i16(const uint8_t *r, int off) { return *(const int16_t *)(r + off); }
static void     wr_i16(uint8_t *r, int off, int16_t v) { *(int16_t *)(r + off) = v; }
static void     wr_i32(uint8_t *r, int off, int32_t v) { *(int32_t *)(r + off) = v; }

/* Port of FUN_004aa89f. */
void engine_combat_clear_battle_result(int rec)
{
    uint8_t *r = CREC(rec);
    wr_i16(r, C_RESULT + 0, 0);
    wr_i16(r, C_RESULT + 2, 0);
    wr_i16(r, C_RESULT + 4, 0);
    wr_i16(r, C_RESULT + 6, 0);
}

/* Port of FUN_00422c12. */
void engine_combat_load_muster(int attacker, int defender)
{
    uint8_t *a = CREC(attacker);
    uint8_t *d = CREC(defender);
    for (int c = 0; c < 7; c++) {
        int va = rd_i16(a, C_COUNTS + c * 2);
        if (a[C_CON_CLASS] == c)
            va += a[C_CON_AMT];
        g_combat_muster_a[c] = va;

        int vd = rd_i16(d, C_COUNTS + c * 2);
        if (d[C_CON_CLASS] == c)
            vd += d[C_CON_AMT];
        g_combat_muster_b[c] = vd;
    }
}

/* Port of FUN_004ab2aa. */
int engine_combat_army_strength(int rec)
{
    uint8_t *r = CREC(rec);
    int total = 0;
    for (int c = 0; c < 7; c++)
        total += (int)rd_i16(r, C_COUNTS + c * 2) * k_unit_strength[c];
    if (r[C_CON_AMT] != 0)
        total += k_unit_strength[r[C_CON_CLASS]] * (unsigned)r[C_CON_AMT];
    if (total < 1)
        total = 1;
    else
        total += 0x14;
    return total;
}

/* DAT_0055401c difficulty (0..4) → defender-strength multiplier percent.
 * Only consulted on the quick-resolve path (DAT_0053e8fc != 0). */
static int difficulty_mult(int difficulty)
{
    switch (difficulty) {
    case 0:  return 0xa0;   /* 160 */
    case 1:  return 200;
    case 2:  return 0xfa;   /* 250 */
    case 3:  return 0x140;  /* 320 */
    default: return 400;
    }
}

/* Apply a resolved outcome: the winner keeps `surv` percent of its troops +
 * merc; the loser is wiped (counts zeroed, merc contract released).  Mirrors
 * the per-branch body of FUN_004aad07 — both branches are byte-identical apart
 * from which index is winner vs loser. */
static void apply_outcome(int winner, int loser, int surv)
{
    uint8_t *w = CREC(winner);
    uint8_t *l = CREC(loser);

    /* Winner: scale each class count + the merc contingent, then re-sum the
     * head-count into +0x168 (retail clears it first, then accumulates). */
    wr_i32(w, C_VALUE, 0);
    for (int c = 0; c < 7; c++)
        wr_i16(w, C_COUNTS + c * 2,
               (int16_t)engine_math_percent_of(rd_i16(w, C_COUNTS + c * 2), surv));
    if (w[C_CON_AMT] != 0)
        w[C_CON_AMT] = (uint8_t)engine_math_percent_of(w[C_CON_AMT], surv);

    int total = 0;
    for (int c = 0; c < 7; c++)
        total += (int)rd_i16(w, C_COUNTS + c * 2);
    if (w[C_CON_AMT] != 0)
        total += w[C_CON_AMT];
    wr_i32(w, C_VALUE, total);

    /* Loser: annihilated. */
    for (int c = 0; c < 7; c++)
        wr_i16(l, C_COUNTS + c * 2, 0);
    if (l[C_CON_AMT] != 0)
        engine_unit_clear_contract(loser);
    wr_i32(l, C_VALUE, 0);
}

/* Port of FUN_004aad07.  (DAT_0056d5c8 = 0 at retail entry is combat-render
 * handoff state consumed by the not-yet-ported aftermath FUN_004ab383; it is
 * not part of this resolver's save-state surface, so it is not modelled.) */
void engine_combat_resolve_casualties(int attacker, int defender,
                                      int difficulty, int quick_resolve,
                                      engine_combat_result_t *out)
{
    int sa = engine_combat_army_strength(attacker);
    int sd = engine_combat_army_strength(defender);

    if (quick_resolve != 0)
        sd = engine_math_percent_of(sd, difficulty_mult(difficulty));

    int ratio = (sa < sd) ? engine_math_percent_div(sd, sa)
                          : engine_math_percent_div(sa, sd);
    int surv = engine_math_lookup_threshold(ratio, k_casualty_table, 10, 100);

    int winner, loser;
    if (sa < sd) { winner = defender; loser = attacker; }
    else         { winner = attacker; loser = defender; }

    if (out) {
        out->winner       = winner;
        out->winner_owner = (int8_t)CREC(winner)[C_OWNER];
    }

    apply_outcome(winner, loser, surv);
}

/* ===================================================================== *
 *  Battle sizing / setup (FUN_004a6a30) — see engine_combat.h.           *
 * ===================================================================== */

int g_combat_both_garrison;   /* DAT_004ee89c */
int g_combat_local_role;      /* DAT_005533d0 */
int g_combat_ctrl_408;        /* DAT_00554408 */
int g_combat_ctrl_5b0;        /* DAT_0056d5b0 */
int g_combat_size_a;          /* DAT_00568934 */
int g_combat_size_b;          /* DAT_00568938 */
int g_combat_owner_a;         /* DAT_005681ec */
int g_combat_owner_b;         /* DAT_005681d8 */

/* Optional battle-cue hook standing in for retail FUN_00427990(ff_batl.wav).
 * The cue-playback path is deferred project-wide (see audio.c), so this is
 * NULL by default and the cue is silently skipped. */
static void (*g_battle_sfx_fn)(void);

void engine_combat_bind_battle_sfx(void (*fn)(void)) { g_battle_sfx_fn = fn; }

/* Port of FUN_004a6a30. */
int engine_combat_size_battle(int attacker, int defender, int local_player)
{
    uint8_t *a = CREC(attacker);
    uint8_t *d = CREC(defender);

    g_player_strategic_352[5]  = 0;   /* retail DAT_0057bf05 (neutral slot +5) */
    g_combat_local_role        = 0;
    g_combat_ctrl_408          = 0;
    g_combat_ctrl_5b0          = 0;
    g_combat_both_garrison     = 0;

    /* No garrison on either side → the caller auto-resolves (no battle UI). */
    if (a[C_GARRISON] == 0 && d[C_GARRISON] == 0)
        return 0;

    if (a[C_GARRISON] != 0 && d[C_GARRISON] != 0)
        g_combat_both_garrison = 1;

    if (g_battle_sfx_fn)
        g_battle_sfx_fn();            /* ff_batl.wav (deferred audio seam) */

    /* Which side, if any, the local player controls.  A defending local player
     * still "attacks" (role 1) when the attacker has no garrison; otherwise it
     * is the defending side (role 2). */
    if ((int8_t)a[C_OWNER] == local_player)
        g_combat_local_role = 1;
    if ((int8_t)d[C_OWNER] == local_player)
        g_combat_local_role = (a[C_GARRISON] == 0) ? 1 : 2;

    if (g_combat_local_role == 1) {
        g_combat_ctrl_408 = 2;
        g_combat_ctrl_5b0 = 2;
    }

    g_combat_size_a = a[C_COUNT]; if (g_combat_size_a == 0) g_combat_size_a = 6;
    g_combat_size_b = d[C_COUNT]; if (g_combat_size_b == 0) g_combat_size_b = 6;
    g_combat_owner_a = (int8_t)a[C_OWNER];
    g_combat_owner_b = (int8_t)d[C_OWNER];
    return 1;
}
