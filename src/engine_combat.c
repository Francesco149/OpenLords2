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
#include <string.h>

#include "engine_combat.h"
#include "save.h"                 /* g_per_territory_420, g_per_army_768, … */
#include "save_schema_globals.h"  /* s_slot_* combat / season / player scalars */
#include "engine_realm_upkeep.h"  /* clear_contract, disband, map link/unlink, … */
#include "engine_math.h"          /* percent_of / percent_div / lookup_threshold */
#include "engine_map_query.h"     /* nearest_feature_ring_at (FUN_0046733c) */
#include "engine_realm.h"         /* replan_economies / recompute_realm_income / … */
#include "engine_army_economy.h"  /* provision / plan_realm */
#include "engine_territory.h"     /* allocate_realm_workforce / highlight_key_cells */
#include "engine_ranking.h"       /* recompute_player_aggregate / check_elimination */
#include "engine_diplomacy.h"     /* battle_adjust */
#include "engine_army_ai.h"       /* run_turn (FUN_0049df48 ≡ FUN_0049a990) */
#include "game_init.h"            /* troop_pool_row / army_near_rawflag probes */

/* --- per-territory occupant ("army") record fields (stride 0x1a4) ---------
 * Shared with engine_realm_upkeep.c's UNIT_* layout; named here for the
 * combat-relevant subset.  Bytes unless marked. */
#define CSTRIDE      0x1a4
#define CREC(i)      (&g_per_territory_420[(i) * CSTRIDE])

#define C_OWNER      0x00    /* owning player id (i8)                         */
#define C_ACTIVE     0x08    /* occupant kind: 1 = a live placed unit (u8)    */
#define C_GARRISON   0x01    /* castle/garrison present → interactive battle (u8)*/
#define C_COUNT      0x02    /* battle troop count for sizing (u8; floored to 6)*/
#define C_VALUE      0x168   /* total head-count, re-summed here (i32)        */
#define C_COUNTS     0x16c   /* 7 × troop-class count (i16)                   */
#define C_RESULT     0x17a   /* 4 × battle-result accumulator (i16)           */
#define C_CON_CLASS  0x195   /* merc contingent's troop class (u8)            */
#define C_CON_AMT    0x196   /* merc contingent head-count (u8)               */
#define C_CON_SLOT   0x197   /* merc contingent pool slot in g_unit_table_20 (u8)*/

/* Post-battle resupply / condition fields (FUN_004a80db). */
#define C_COND_CNT   0x182   /* 3 supply groups × {count,out%,stock} stride 6 */
#define C_COND_OUT   0x184   /*   (i16 each); group g at base + g*6           */
#define C_COND_STOCK 0x186
#define C_COND_LEVEL 0x19c   /* averaged supply shortfall per head (i8)       */

/* Post-conquest garrison-halve scratch (FUN_004ad8cc). */
#define C_SCRATCH_1C 0x1c    /* cleared after conquest (u8)                   */
#define C_FLAG_14C   0x14c   /* cleared after conquest (u8)                   */

/* Per-class combat weight (DAT_004d4b98): peasant .. the heaviest unit. */
static const int32_t k_unit_strength[7] = { 2, 16, 8, 13, 9, 13, 22 };

/* Per-supply-group "required stock" rate (DAT_004de440, 3 × i32) — the
 * denominator multiplier in the post-battle condition recompute. */
static const int32_t k_cond_rate[3] = { 200, 200, 400 };

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
static int32_t  rd_i32(const uint8_t *r, int off) { return *(const int32_t *)(r + off); }
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

/* ===================================================================== *
 *  Combat-aftermath occupant fix-ups (FUN_004a80db / FUN_004ad8cc).      *
 * ===================================================================== */

/* Port of FUN_004a80db.  Post-battle supply/condition recompute for a surviving
 * occupant: for each of 3 supply groups, express the present stock (+0x186) as a
 * percent of the requirement (rate × count, +0x182) into the out% field (+0x184)
 * clamped to 100, accumulate the shortfall of every under-supplied group, then
 * store the shortfall averaged over the head-count (+0x168, rounded up) into the
 * condition byte (+0x19c).  Groups with no count zero their out%/stock fields.
 *
 * The int the binary returns is dead — both callers (FUN_004ab383) invoke it for
 * effect only, and on a nonzero remainder it is overwritten with rec*0x54
 * garbage — so this port returns void. */
void engine_combat_recompute_condition(int rec)
{
    uint8_t *r    = CREC(rec);
    int      head = rd_i32(r, C_VALUE);
    if (head <= 0)
        return;

    int shortfall = 0;
    for (int g = 0; g < 3; g++) {
        int cnt = rd_i16(r, C_COND_CNT + g * 6);
        if (cnt < 1) {
            wr_i16(r, C_COND_OUT   + g * 6, 0);
            wr_i16(r, C_COND_STOCK + g * 6, 0);
        } else {
            int stock = rd_i16(r, C_COND_STOCK + g * 6);
            int req   = k_cond_rate[g] * cnt;
            int pct   = engine_math_percent_div(stock, req);
            wr_i16(r, C_COND_OUT + g * 6, (int16_t)pct);
            if (pct < 100)
                shortfall += req - stock;
            else
                wr_i16(r, C_COND_OUT + g * 6, 100);
        }
    }

    int level = shortfall / head;
    if (shortfall % head != 0)
        level += 1;
    r[C_COND_LEVEL] = (uint8_t)level;
}

/* Port of FUN_004ad8cc.  Halve a conquered occupant's surviving troops: each of
 * the 7 class counts above 10 is halved (≤10 → wiped), the head-count (+0x168)
 * is re-summed and the merc contingent (+0x196) folded back in, two scratch
 * flags (+0x1c, +0x14c) are cleared, and the per-turn wage is recomputed. */
void engine_combat_halve_garrison(int rec)
{
    uint8_t *r   = CREC(rec);
    int      sum = 0;
    for (int c = 0; c < 7; c++) {
        int16_t v  = rd_i16(r, C_COUNTS + c * 2);
        int     nv = (v < 0xb) ? 0 : v / 2;
        wr_i16(r, C_COUNTS + c * 2, (int16_t)nv);
        sum += nv;
    }
    wr_i32(r, C_VALUE, sum + r[C_CON_AMT]);
    r[C_SCRATCH_1C] = 0;
    r[C_FLAG_14C]   = 0;
    engine_unit_recompute_wage(rec);
}

/* ===================================================================== *
 *  Combat-initiation SCC — the 5 mutually-recursive bodies              *
 *  (FUN_004ac3c6 / 00437535 / 004a7158 / 004ab383 / 004a72fe).          *
 * ===================================================================== */

/* --- additional per-territory occupant fields (g_per_territory_420) ----- */
#define C_COL       0x0a    /* overworld column (i8)                        */
#define C_ROW       0x0b    /* overworld row (i8)                           */
#define C_CELLOFS   0x0c    /* (row*0x40+col)*8 byte offset into cell grid  */
#define C_ARMYKEY   0x10    /* region key of the occupant (u8)              */
#define C_F153      0x153   /* per-battle "presence" accumulator (i8)       */
#define C_F154      0x154   /* baseline the +0x153 accumulator resets to(i8)*/
#define C_F167      0x167   /* secondary takeover flag (u8)                 */
#define C_F198      0x198   /* region-transfer pending flag (u8); on the    */
                            /*   garrison-place path holds the spawning key */
#define C_F199      0x199   /* survives-this-battle flag (u8)               */
#define C_F19A      0x19a   /* defender-occupant index when this army moves (u8)*/

/* --- occupant fields touched by the settlement-spawn cluster ------------- */
#define C_F06       0x06    /* live-unit flag (u8)                          */
#define C_ROLE      0x1a    /* placement role: 2 = rejected / 5 = garrison  */
#define C_F11       0x11    /* home army/region key (u8)                    */
#define C_HOMECOL   0x14    /* home column (i8, ← army seat +0x74)          */
#define C_HOMEROW   0x15    /* home row (i8, ← army seat +0x75)             */
#define C_F14F      0x14f   /* owner's roster load-balance slot (u8)        */
#define C_F150      0x150   /* placed/active flag (u8)                      */
#define C_F151      0x151   /* spawning army/region key (u8)                */
#define C_F164      0x164   /* spawn epoch (i16, ← DAT_00553edc)            */
#define C_F166      0x166   /* per-army settlement byte (u8, ← army +0x0c)  */

/* --- per-region army-economy record (g_per_army_768, stride 0x300) ------- */
#define RSTRIDE     0x300
#define REGREC(k)   (&g_per_army_768[(k) * RSTRIDE])
#define R_OWNER     0x05    /* region owning player (u8)                    */
#define R_F07       0x07    /* owner's banner/colour key (u8, ← strat +0xa) */
#define R_CAP_COL   0x6c    /* region capital cell column (u8)              */
#define R_CAP_ROW   0x6d    /* region capital cell row (u8)                 */
#define R_F0C       0x0c    /* region popularity reserve (i8)               */
#define R_F17       0x17    /* region popularity (i8)                       */
#define R_UNREST    0x20    /* region unrest level (i8, band 0..4)          */
#define R_CAP       0x24    /* region population capacity (i32)             */
#define R_F1B0      0x1b0   /* cleared on demolish (u8)                     */
#define R_PENDING   0x1bc   /* pending territory-transfer occupant (i32)    */
#define R_F297      0x297   /* 4 × i8 (stride 0x18) cleared on demolish     */

/* --- per-player strategic record (g_player_strategic_352, stride 0x160) -- */
#define PSTRIDE     0x160
#define PREC(s)     (&g_player_strategic_352[(s) * PSTRIDE])
#define P_SLOT      0x04    /* player-slot in-game flag (u8; ≠ +0x05)       */
#define P_ACTIVE    0x05    /* realm in-game / has-data flag (u8)           */
#define P_STATUS    0x07    /* player status (0 = live; 6 = resigned)       */
#define P_F0A       0x0a    /* banner/colour key (u8)                       */
#define P_CONQ      0x29    /* conquests this game (u8)                     */
#define P_CONQMAX   0x2a    /* conquest high-water mark (u8)                */
#define P_TERRCNT   0x60    /* territory count (u8)                         */
#define P_NOTIFY_ROT 0x159  /* rotating 0..3 notification-variant counter (u8)*/

/* Deferred-audio + render-clock seams (all default NULL / silent). */
static void     (*g_combat_wav_fn)(const char *, int, int);
static void     (*g_combat_click_fn)(void);
static uint32_t (*g_combat_clock_fn)(void);

void engine_combat_bind_wav_fn(void (*fn)(const char *, int, int)) { g_combat_wav_fn = fn; }
void engine_combat_bind_click_fn(void (*fn)(void))                 { g_combat_click_fn = fn; }
void engine_combat_bind_clock_fn(uint32_t (*fn)(void))             { g_combat_clock_fn = fn; }

static void     combat_wav(const char *n, int a, int b) { if (g_combat_wav_fn) g_combat_wav_fn(n, a, b); }
static uint32_t combat_clock(void) { return g_combat_clock_fn ? g_combat_clock_fn() : 0; }

/* Port of FUN_0046d42c (456 B).  Scan the 4×4 cell block anchored two cells up
 * and left of region `key`'s capital cell (record +0x6c col / +0x6d row) — i.e.
 * columns [cap_col-2 .. cap_col+1], rows [cap_row-2 .. cap_row+1], the asymmetric
 * box retail uses (covers 2 cells left/up but only 1 right/down) — and return the
 * index of the strongest friendly live unit there: an occupant whose owner
 * (+0x00, sign-extended) equals the region owner (+0x05), whose kind byte (+0x08)
 * is 1, with the greatest head-count (+0x168, strictly).  Returns 0 when the
 * capital sits within two cells of the 0..63 grid edge (the whole box must fit)
 * or no qualifying unit is present.  combat_b (FUN_004a6c68) parks the result in
 * the defender slot DAT_00568218.
 *
 * Retail reuses engine_path's find-nearest scratch DAT_00569570/78 as the loop
 * counters (a codegen artifact, quirk #130); the answer is the returned occupant
 * index, never the scratch, so we use locals rather than clobber those globals. */
int engine_combat_find_capital_defender(int key)
{
    uint8_t *reg = REGREC(key);
    int cap_col = reg[R_CAP_COL];
    int cap_row = reg[R_CAP_ROW];

    if (cap_col - 2 < 0)     return 0;
    if (cap_col + 2 >= 0x41) return 0;
    if (cap_row - 2 < 0)     return 0;
    if (cap_row + 2 >= 0x41) return 0;

    int region_owner = reg[R_OWNER];
    int best = 0, best_val = 0;
    int idx = ((cap_row - 2) * 0x40 + (cap_col - 2)) * 8;  /* byte offset of box origin */
    for (int row = cap_row - 2; row < cap_row + 2; row++) {
        for (int col = cap_col - 2; col < cap_col + 2; col++) {
            unsigned occ = g_overworld_map[idx + 5];          /* cell +5 head-link */
            if (occ != 0
                && (int)(int8_t)CREC(occ)[C_OWNER] == region_owner
                && CREC(occ)[C_ACTIVE] == 1
                && best_val < *(const int32_t *)(CREC(occ) + C_VALUE)) {
                best     = (int)occ;
                best_val = *(const int32_t *)(CREC(occ) + C_VALUE);
            }
            idx += 8;
        }
        idx += (0x40 - 4) * 8;   /* 0x1e0: advance to the next row, same start column */
    }
    return best;
}

/* Resign-time interactive UI cleanup seam (retail 0x47fe0b, game-mode-gated
 * on s_slot_0057c8fc == 2).  The real function sweeps a 0x50-entry stride-0x1b0
 * on-screen element table (0x5544ac) clearing the resigned player's entries and
 * releasing a UI resource — interactive-only render state, deferred like the
 * other UI/audio seams.  NULL default → no-op (correct for the headless turn
 * driver, which never has game-mode 2). */
static void (*g_resign_ui_fn)(int player);
void engine_combat_bind_resign_ui_fn(void (*fn)(int player)) { g_resign_ui_fn = fn; }

int g_combat_render_active;      /* DAT_0056d5c8 */
int g_combat_active_terr;        /* DAT_00553f8c */
int g_combat_winner_owner;       /* DAT_005440cc — winner owner (UI, not save) */

/* Interactive battlefield-render handoff scalars (retail runtime globals; not
 * save state).  Written on the interactive/animation path, read by the future
 * battle-UI wiring; inert on the headless auto-resolve path the diff exercises. */
static int      g_combat_screen_state;   /* DAT_004eac50 0x12 interactive / 0x13 anim */
static int      g_combat_screen_mode;    /* DAT_0057d340 */
static int      g_combat_anim_a;         /* DAT_00553c70 */
static int      g_combat_anim_b;         /* DAT_00553d20 */
static uint32_t g_combat_anim_clock;     /* _DAT_005689a0 / _DAT_0057a0d0 */
static int      g_combat_audio_latch;    /* _DAT_005533f0 */
static int      g_combat_render_dirty;   /* DAT_0052afcc */
static int      g_combat_flag_530d0;     /* DAT_005530d0 */
static int      g_combat_flag_553210;    /* DAT_00553210 — UI dirty: a local-owned
                                          * army's region relationship changed */
static int      g_combat_flag_567d0c;    /* DAT_00567d0c */
static int      g_combat_flag_53f284;    /* DAT_0053f284 — quick-anim suppressor */
static uint32_t g_combat_anim_t0;        /* DAT_0053e994 — animation start time */
static uint32_t g_combat_anim_deadline;  /* _DAT_00568d9c */
static int      g_combat_anim_scratch[10]; /* DAT_00515d60[10] */
static int      g_combat_anim_53fc8;     /* DAT_00553fc8 */

/* Build the per-player has-data filter table the event queue post() consults
 * (retail (&DAT_0057bf05)[i*0x160], one byte per slot, last slot inert). */
static void build_has_data(uint8_t hd[7])
{
    for (int i = 0; i < 6; i++)
        hd[i] = PREC(i)[P_ACTIVE];
    hd[6] = 0;
}

/* Run the resolver (FUN_004aad07) over the parked combatant scalars and
 * publish its winner outputs (DAT_0057c924 winner / DAT_005440cc owner), the
 * way retail's resolver writes them in-line.  Clears the render-handoff flag
 * (DAT_0056d5c8) exactly as the resolver does at entry. */
static void resolve_combat(void)
{
    g_combat_render_active = 0;
    engine_combat_result_t res;
    engine_combat_resolve_casualties((int)s_slot_00568210, (int)s_slot_00568218,
                                     (int)s_slot_0055401c, (int)s_slot_0053e8fc, &res);
    s_slot_0057c924       = (uint32_t)res.winner;
    g_combat_winner_owner = res.winner_owner;
}

/* Port of FUN_004aa181 (1353 B) — merge occupant `src`'s contingent into `dst`.
 * Runs only when the combined head-count stays under 0x5dd (1501) and at most
 * one of the two carries a mercenary contingent.  Sums the head-count (+0x168),
 * the 7 troop-class counts (+0x16c) and the 3 supply-group {count,out,stock}
 * shorts (+0x182/+0x184/+0x186, stride 6); keeps the larger (signed) battle-
 * presence accumulator (+0x153); folds the survive/move linkage bytes
 * (+0x199/+0x19a, dst preferred), re-homing the survivor back-reference through
 * the region's pending-unit slot (g_per_army_768 +0x1bc); copies src's merc
 * triple (+0x195/+0x196/+0x197) when src is the one carrying it; recomputes
 * dst's supply condition when it now holds the survive flag; and disbands src.
 * When BOTH carry a merc contingent the merge is refused and dst's owner is
 * notified (event 0xa7).  Reached via the garrison-placement path FUN_004a79a3. */
void engine_combat_merge_stack(int dst, int src, engine_event_queue_state_t *evq)
{
    uint8_t *d = CREC(dst);
    uint8_t *s = CREC(src);

    if (rd_i32(d, C_VALUE) + rd_i32(s, C_VALUE) >= 0x5dd)
        return;

    if (d[C_CON_AMT] != 0 && s[C_CON_AMT] != 0) {
        /* both carry mercenaries → merge refused, notify dst's owner (0xa7) */
        uint8_t hd[7]; build_has_data(hd);
        engine_event_queue_post(evq, 0, (int8_t)d[C_OWNER], 0xa7, 0, 0, 0, 0, 0,
                                hd, (int)s_slot_0057c8cc);
        return;
    }

    /* keep the larger (signed) battle-presence accumulator */
    if ((int8_t)d[C_F153] < (int8_t)s[C_F153])
        d[C_F153] = s[C_F153];

    /* fold the survive-linkage byte: keep dst's if set, else inherit src's;
     * src's is cleared.  When the survivor index is live, re-home its back-
     * reference (the region's pending-unit slot → +0x19a) onto dst. */
    uint8_t survivor = d[C_F199] != 0 ? d[C_F199] : s[C_F199];
    d[C_F199] = survivor;
    s[C_F199] = 0;
    if (survivor != 0)
        CREC(rd_i32(REGREC(survivor), R_PENDING))[C_F19A] = (uint8_t)dst;

    /* fold the move-linkage byte the same way (dst preferred) */
    if (d[C_F19A] == 0)
        d[C_F19A] = s[C_F19A];

    /* inherit src's mercenary contingent when src is the one carrying it */
    if (s[C_CON_AMT] != 0) {
        d[C_CON_AMT]   = s[C_CON_AMT];
        d[C_CON_CLASS] = s[C_CON_CLASS];
        d[C_CON_SLOT]  = s[C_CON_SLOT];
    }

    /* sum head-count, the 7 troop-class counts, the 3 supply-group triples */
    wr_i32(d, C_VALUE, rd_i32(d, C_VALUE) + rd_i32(s, C_VALUE));
    for (int k = 0; k < 7; k++)
        wr_i16(d, C_COUNTS + k * 2,
               rd_i16(d, C_COUNTS + k * 2) + rd_i16(s, C_COUNTS + k * 2));
    for (int k = 0; k < 3; k++)
        wr_i16(d, C_COND_CNT + k * 6,
               rd_i16(d, C_COND_CNT + k * 6) + rd_i16(s, C_COND_CNT + k * 6));
    for (int k = 0; k < 3; k++)
        wr_i16(d, C_COND_OUT + k * 6,
               rd_i16(d, C_COND_OUT + k * 6) + rd_i16(s, C_COND_OUT + k * 6));
    for (int k = 0; k < 3; k++)
        wr_i16(d, C_COND_STOCK + k * 6,
               rd_i16(d, C_COND_STOCK + k * 6) + rd_i16(s, C_COND_STOCK + k * 6));

    if (d[C_F199] != 0)
        engine_combat_recompute_condition(dst);   /* FUN_004a80db */
    engine_unit_disband(src);                      /* FUN_004aa039 */
}

/* Port of FUN_004ac3c6 — demolish realm `key`. */
void engine_combat_demolish_realm(int key, engine_event_queue_state_t *evq, int mp_active)
{
    uint8_t *reg = REGREC(key);
    reg[R_OWNER] = 0;
    reg[R_F07]   = 0;
    for (int i = 0; i < 4; i++)
        reg[R_F297 + i * 0x18] = 0;
    reg[R_F1B0] = 0;

    engine_territory_allocate_realm_workforce(key);            /* FUN_0044f6e7 */
    engine_army_economy_provision(key, (int)s_slot_0057c934);  /* FUN_0044df5f */
    engine_army_economy_plan_realm(key, (int)s_slot_0057c92c); /* FUN_004485a5 */
    engine_realm_recompute_realm_income(key);                  /* FUN_0044b80b */

    int32_t pending = rd_i32(reg, R_PENDING);
    if (pending != 0)
        engine_combat_transfer_army((unsigned)pending, key, evq, mp_active);
}

/* Port of FUN_00437535 — move occupant `occupant` to a free cell near realm
 * `key`, dispatching combat if the destination is defended. */
void engine_combat_transfer_army(unsigned occupant, int key,
                                 engine_event_queue_state_t *evq, int mp_active)
{
    uint8_t *t = CREC((int)occupant);

    int col = 0, row = 0;
    int hit = engine_map_query_nearest_feature_ring_at(
                  (int8_t)t[C_COL], (int8_t)t[C_ROW], &col, &row);  /* FUN_0046733c */
    if (hit == 0) {
        engine_unit_disband((int)occupant);                        /* FUN_004aa039 */
        return;
    }

    engine_unit_map_unlink((int)occupant);                         /* FUN_0046ec6e */
    wr_i32(t, C_CELLOFS, (row * 0x40 + col) * 8);
    t[C_COL]  = (uint8_t)col;
    t[C_ROW]  = (uint8_t)row;
    t[C_F198] = 0;
    wr_i32(REGREC(key), R_PENDING, 0);
    engine_unit_map_link((int)occupant);                           /* FUN_0046eddf */

    uint8_t def = t[C_F19A];
    if (def != 0 &&
        engine_combat_dispatch((int)occupant, def, evq, mp_active) != 0)
        s_slot_0056822c = (uint32_t)key;

    engine_realm_collect_territory_income();                       /* FUN_004ad6c0 */
    g_combat_flag_530d0 = 1;
    if (REGREC(key)[R_OWNER] == (uint8_t)s_slot_0057c8cc && key == (int)s_slot_0057c93c)
        g_combat_flag_530d0 = 1;
}

/* Port of FUN_004a7158 — combat dispatch. */
int engine_combat_dispatch(int attacker, int defender,
                           engine_event_queue_state_t *evq, int mp_active)
{
    s_slot_0053e8fc = 0;   /* quick-resolve cleared on every dispatch */
    s_slot_0056822c = (uint8_t)CREC(g_combat_active_terr)[C_ARMYKEY];
    s_slot_00568210 = (uint32_t)attacker;
    s_slot_00568218 = (uint32_t)defender;

    engine_combat_clear_battle_result(attacker);                   /* FUN_004aa89f */
    engine_combat_clear_battle_result((int)s_slot_00568218);

    int sized = engine_combat_size_battle((int)s_slot_00568210, (int)s_slot_00568218,
                                          (int)s_slot_0057c8cc);    /* FUN_004a6a30 */
    if (sized == 0) {
        resolve_combat();                                          /* FUN_004aad07 */
        engine_combat_aftermath(0, evq, mp_active);                /* FUN_004ab383 */
        return 0;
    }

    if (g_combat_flag_53f284 == 0 && g_combat_both_garrison == 0) {
        /* Quick-anim path: resolve head-less, then play back the result. */
        engine_combat_load_muster((int)s_slot_00568210, (int)s_slot_00568218);
        resolve_combat();
        engine_combat_aftermath(0, evq, mp_active);
        g_combat_screen_state = 0x13;
        g_combat_anim_clock   = combat_clock();
        g_combat_anim_a       = (int)s_slot_00568210;
        g_combat_anim_b       = (int)s_slot_00568218;
        g_combat_screen_mode  = 2;
        return 0;
    }

    /* Fully interactive battlefield: hand off to the renderer (s080 cue). */
    g_combat_screen_state = 0x12;
    g_combat_anim_clock   = combat_clock();
    g_combat_screen_mode  = 2;
    g_combat_audio_latch  = 1;
    if (g_combat_local_role == 1)
        combat_wav("s080_03.wav", 1, 0);
    else if (g_combat_local_role == 2)
        combat_wav("s080_01.wav", 1, 0);
    else
        combat_wav("s080_02.wav", 1, 0);
    return 1;
}

/* Shared aftermath tail: clear both result records, optionally halve the
 * captured garrison, then disband-or-spare the loser and run the elimination /
 * diplomacy follow-ups.  `loser`/`winner` are occupant indices. */
static void aftermath_settle_loser(int loser, int winner,
                                   engine_event_queue_state_t *evq, int mp_active)
{
    engine_combat_clear_battle_result((int)s_slot_00568210);
    engine_combat_clear_battle_result((int)s_slot_00568218);
    if (g_combat_render_active == 1)
        engine_combat_halve_garrison(loser);                       /* FUN_004ad8cc */

    uint8_t *L          = CREC(loser);
    int      loser_own  = (int8_t)L[C_OWNER];

    if (L[C_F199] == 0 || rd_i32(L, C_VALUE) == 0) {
        if (g_combat_render_active == 1) {
            if (rd_i32(L, C_VALUE) < 0x32) {
                uint8_t hd[7]; build_has_data(hd);
                engine_event_queue_post(evq, 0, loser_own, 0x120, 0, 0, 0, 0, 0,
                                        hd, (int)s_slot_0057c8cc);
                engine_unit_disband(loser);                        /* FUN_004aa039 */
            } else {
                L[C_F199] = 0;
            }
        } else {
            engine_unit_disband(loser);
        }
    } else {
        L[C_F199] = 0;
    }

    engine_ranking_check_elimination((unsigned)loser_own, evq, mp_active); /* FUN_0049b42b */
    if (loser_own != 0)
        engine_diplomacy_battle_adjust(loser_own, (int8_t)CREC(winner)[C_OWNER],
                                       0x14, evq);                 /* FUN_004a1ee1 */
}

/* Port of FUN_004ab383 — battle aftermath / takeover.  `render` is non-zero
 * only when entered from the battlefield renderer's teardown (the dispatch
 * always passes 0). */
void engine_combat_aftermath(int render, engine_event_queue_state_t *evq, int mp_active)
{
    int a = (int)s_slot_00568210;
    int d = (int)s_slot_00568218;

    if (render != 0) {
        uint32_t now = combat_clock();
        g_combat_anim_deadline = now + (uint32_t)(((int)s_slot_0053f26c - (int)g_combat_anim_t0) * -1000);
        s_slot_005440c8        = g_combat_anim_t0;
        for (int i = 0; i < 10; i++)
            g_combat_anim_scratch[i] = 0;
        g_combat_anim_53fc8 = 0;
    }

    if (CREC(a)[C_GARRISON] != 0 || CREC(d)[C_GARRISON] != 0)
        g_combat_render_dirty = 1;

    if ((int)s_slot_0057c924 == a) {
        /* Attacker won. */
        if ((int8_t)CREC(d)[C_OWNER] == (int)s_slot_0057c8cc)
            combat_wav("ff_lose.wav", 0, 0);
        if (CREC(d)[C_F198] != 0) {
            wr_i32(REGREC((int)s_slot_0056822c), R_PENDING, 0);
            engine_combat_transfer_ownership((unsigned)(int8_t)CREC(a)[C_OWNER],
                                             s_slot_0056822c, evq, mp_active);
        }
        if (CREC(d)[C_F167] != 0)
            engine_combat_transfer_ownership((unsigned)(int8_t)CREC(a)[C_OWNER],
                                             s_slot_0056822c, evq, mp_active);

        if (s_slot_0053e8fc == 0) engine_combat_recompute_condition(a); /* FUN_004a80db */
        else                      CREC(a)[C_F199] = 0;
        CREC(a)[C_F19A]  = 0;
        CREC(a)[C_F153]  = (uint8_t)((int8_t)CREC(a)[C_F153] + 8);
        if (CREC(a)[C_GARRISON] == 0)
            CREC(a)[C_F153] = (uint8_t)((int8_t)CREC(a)[C_F154] - 1);

        aftermath_settle_loser(d, a, evq, mp_active);
    } else if ((int)s_slot_0057c924 == d) {
        /* Defender won. */
        if (CREC(a)[C_F198] != 0)
            wr_i32(REGREC((int)s_slot_0056822c), R_PENDING, 0);
        if ((int8_t)CREC(a)[C_OWNER] == (int)s_slot_0057c8cc)
            combat_wav("ff_lose.wav", 0, 0);

        if (s_slot_0053e8fc != 0) CREC(d)[C_F199] = 0;
        if (s_slot_0053e8fc == 0) engine_combat_recompute_condition(d);
        else                      CREC(d)[C_F19A] = 0;
        if (CREC(d)[C_GARRISON] == 0)
            CREC(d)[C_F153] = (uint8_t)((int8_t)CREC(d)[C_F153] + 7);

        aftermath_settle_loser(a, d, evq, mp_active);
    }

    engine_realm_refresh_wage_bills();   /* FUN_004ad426 */
    engine_realm_replan_economies();     /* FUN_00448545 */
    engine_army_ai_run_turn();           /* FUN_0049df48 ≡ FUN_0049a990 */
}

/* Port of FUN_004a72fe — transfer region `key`'s ownership to `new_owner`. */
void engine_combat_transfer_ownership(unsigned new_owner, unsigned key,
                                      engine_event_queue_state_t *evq, int mp_active)
{
    int local = (int)s_slot_0057c8cc;

    g_combat_flag_567d0c = 1;
    if ((int)new_owner == local)
        engine_territory_highlight_key_cells(key);                 /* FUN_0046dfd5 */
    engine_ranking_recompute_player_aggregate(new_owner);          /* FUN_0049d1e0 */

    uint8_t *no  = PREC((int)new_owner);
    uint8_t *reg = REGREC((int)key);
    uint8_t  region_owner = reg[R_OWNER];
    uint8_t  key_lo       = (uint8_t)key;

    int can_hold = (no[P_CONQ] == 0) ||
                   (engine_realm_player_owns_neighbor(new_owner, (int)key) != 0); /* FUN_00467eb0 */

    if (can_hold) {
        no[P_CONQ] += 1;
        uint8_t hd[7]; build_has_data(hd);

        if ((int)new_owner == local) {
            if (g_combat_click_fn) g_combat_click_fn();             /* FUN_00499aca */
            if ((unsigned)no[P_CONQ] == (unsigned)s_slot_0056d5dc - 1U)
                engine_event_queue_post(evq, 0, local, 0x7b, 0, 0xd, key_lo, region_owner, 0, hd, local);
            else if (no[P_CONQMAX] < no[P_CONQ]) {
                if (no[P_CONQMAX] < 2)
                    engine_event_queue_post(evq, 0, local, 0x75, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else if (no[P_CONQMAX] < 3)
                    engine_event_queue_post(evq, 0, local, 0x76, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else if (no[P_TERRCNT] < 0x1a)
                    engine_event_queue_post(evq, 0, local, 0x77, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else if (no[P_TERRCNT] < 0x29)
                    engine_event_queue_post(evq, 0, local, 0x78, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else if (no[P_TERRCNT] < 0x3d)
                    engine_event_queue_post(evq, 0, local, 0x79, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else if (no[P_TERRCNT] < 0x51)
                    engine_event_queue_post(evq, 0, local, 0x7a, 0, 0xd, key_lo, region_owner, 0, hd, local);
                else
                    engine_event_queue_post(evq, 0, local, 0x7d, 0, 0xd, key_lo, region_owner, 0, hd, local);
            } else {
                engine_event_queue_post(evq, 0, local, 0x7e, 0, 0xd, key_lo, region_owner, 0, hd, local);
            }
        } else if (region_owner == (uint8_t)local) {
            engine_event_queue_post(evq, (int)new_owner, region_owner, 0x73, 0, 0, key_lo, 0, 0, hd, local);
        } else if (region_owner == 0) {
            engine_event_queue_post(evq, (int)new_owner, local, 0x74, 0, 0, key_lo, 0, 0, hd, local);
        } else {
            engine_event_queue_post(evq, (int)new_owner, local, 0x72, 0, 0, key_lo, region_owner, 0, hd, local);
        }

        if (region_owner != 0)
            engine_ranking_check_elimination((unsigned)region_owner, evq, mp_active); /* FUN_0049b42b */

        reg[R_OWNER] = (uint8_t)new_owner;

        int adj = (no[P_ACTIVE] == 0) ? 0x1e : ((int)s_slot_0053f23c * 0x14 + 10);
        if ((int8_t)reg[R_F0C] < adj) {
            reg[R_F17] = (uint8_t)(reg[R_F17] - reg[R_F0C]);
            reg[R_F0C] = 0;
        } else {
            reg[R_F0C] = (uint8_t)(reg[R_F0C] - (uint8_t)adj);
            reg[R_F17] = (uint8_t)(reg[R_F17] - (uint8_t)adj);
        }
        reg[R_F07] = no[P_F0A];
        if (no[P_CONQMAX] < no[P_CONQ])
            no[P_CONQMAX] = no[P_CONQ];
    } else {
        if ((int)new_owner == local) {
            uint8_t hd[7]; build_has_data(hd);
            engine_event_queue_post(evq, 0, local, 0x81, 0, 0, key_lo, 0, 0, hd, local);
        }
        engine_combat_demolish_realm((int)key, evq, mp_active);    /* FUN_004ac3c6 (recurse) */
    }
}

/* ===================================================================== *
 *  Region-occupation helpers (FUN_004abXX / FUN_004ac185) — reached from  *
 *  per-tick army-state machine (FUN_004650b0's state handlers) when a    *
 *  live army sits on a region and its effective owner/popularity needs   *
 *  reconciling.  These mutate the region's effective-owner colour byte   *
 *  (occupant +0x10) and the region popularity reserve, and notify the    *
 *  controlling player.                                                   *
 * ===================================================================== */

/* Port of FUN_004abf77 @ 0x4abf77 (526 B, __cdecl void(int army,int region)).
 * When an army occupies a NEUTRAL region (R_OWNER == 0), post a one-shot
 * "reception" notification to the army's owner describing how the local
 * population receives the occupation.  The message code is chosen from the
 * region's popularity reserve (+0x0c) and, once popularity is healthy
 * (>= 0x1e), from how fully the army's head-count fills the region's capacity
 * (+0x24):
 *   pop <  0x0a                       → 0x82 (hostile)
 *   pop <  0x1e                       → 0x83 (wary)
 *   pop >= 0x1e, fill <  0x14         → 0x86
 *   pop >= 0x1e, 0x14 <= fill < 0x32  → 0x85
 *   pop >= 0x1e, 0x32 <= fill < 0x65  → 0x84
 *   pop >= 0x1e, fill >= 0x65         → 0x83
 * `fill` is percent_div(head, capacity) clamped to 0x65 (head >= capacity).
 * Notification-only: no save-state mutation.  All callees ported
 * (engine_math_percent_div = FUN_00404dc1, engine_event_queue_post). */
void engine_combat_notify_occupation(int army, int region,
                                     engine_event_queue_state_t *evq)
{
    if (REGREC(region)[R_OWNER] != 0)
        return;                       /* only neutral regions get the message */

    int head = *(const int32_t *)(CREC(army)  + C_VALUE);
    int cap  = *(const int32_t *)(REGREC(region) + 0x24);   /* region capacity */
    int fill = (head < cap) ? engine_math_percent_div(head, cap) : 0x65;

    int     owner = (int8_t)CREC(army)[C_OWNER];
    int8_t  pop   = (int8_t)REGREC(region)[R_F0C];
    int     code;
    if      (pop < 0x0a)   code = 0x82;
    else if (pop < 0x1e)   code = 0x83;
    else if (fill < 0x65) {
        if      (fill < 0x32) code = (fill < 0x14) ? 0x86 : 0x85;
        else                  code = 0x84;
    }
    else                   code = 0x83;

    uint8_t hd[7]; build_has_data(hd);
    engine_event_queue_post(evq, 0, owner, code, 0, 2, (uint8_t)region, 0, 0,
                            hd, (int)s_slot_0057c8cc);
}

/* Port of FUN_004abb36 @ 0x4abb36 (473 B, __cdecl void(int army,uint region)).
 * Reconcile an army's occupation of `region` against that region's true owner.
 * Latches the army's effective-region colour (+0x10), fires the neutral-region
 * reception notice (engine_combat_notify_occupation), and — when the region's
 * true owner differs from the army's owner — flags the local-view dirty bit
 * (DAT_00553210, only for a local-owned army) and, if the region is owned and
 * the army's destination key (+0x151) is this region, posts the "your army has
 * reached enemy/owned territory" arrival notice (0xaa) to the army's owner,
 * advancing that owner's rotating notification-variant counter (+0x159, 0..3).
 * Also raises the combat-focus flag (DAT_005530d0) when the region belongs to
 * the local player's currently-selected realm.  Reached from the per-tick
 * army-state machine's "marching/occupying" handler (FUN_0046521f). */
void engine_combat_army_enter_region(int army, unsigned region,
                                     engine_event_queue_state_t *evq)
{
    CREC(army)[C_ARMYKEY] = (uint8_t)region;       /* effective region colour */
    engine_combat_notify_occupation(army, (int)region, evq);   /* FUN_004abf77 */

    if (REGREC(region)[R_OWNER] == (uint8_t)s_slot_0057c8cc &&
        (int)s_slot_0057c93c == (int)region)
        g_combat_flag_530d0 = 1;

    int army_owner = (int8_t)CREC(army)[C_OWNER];
    if ((unsigned)REGREC(region)[R_OWNER] != (unsigned)army_owner) {
        if (army_owner == (int)s_slot_0057c8cc)
            g_combat_flag_553210 = 1;

        if (REGREC(region)[R_OWNER] != 0 &&
            CREC(army)[C_F151] == (uint8_t)region) {
            uint8_t *po = PREC(army_owner);
            uint8_t hd[7]; build_has_data(hd);
            engine_event_queue_post(evq, army_owner, REGREC(region)[R_OWNER], 0xaa,
                                    (po[P_NOTIFY_ROT] - 4) + po[P_STATUS] * 4,
                                    1, (uint8_t)region, 0, 0,
                                    hd, (int)s_slot_0057c8cc);
            po[P_NOTIFY_ROT]++;
            if (po[P_NOTIFY_ROT] > 3)
                po[P_NOTIFY_ROT] = 0;
        }
    }
}

/* Port of FUN_004ac185 @ 0x4ac185 (577 B, __cdecl int(int region)).  Spawn a
 * rebellion army on a populated, owned region whose population reserve has run
 * dry.  Returns 1 when a rebel stack is created, 0 otherwise.
 *
 *   - An UNOWNED region (R_OWNER == 0) never rebels → 0.
 *   - The region must have a placeable cell within Chebyshev radius 1..3 of its
 *     seat, found by the two map-feature probes (rawflag bit 0, then "0-or-2");
 *     each leaves the matched cell in the find-nearest scratch (DAT_00569570/78).
 *     No match → 0.
 *   - A free record slot must exist at that cell (engine_territory_place_entity,
 *     kind 2 / type 6 = the rebel "peasant" faction).  No slot → 0.
 *
 * On success it demolishes the region's standing economy (the conquered realm
 * is gone), then fills the new rebel occupant: head-count = 30% of the region's
 * capacity (+0x24), all of it in troop-class 0, with the result accumulator
 * (+0x17a) and class-0 count (+0x16c) seeded to the head-count; stamps the spawn
 * turn (+0x164 ← season s_slot_00553edc) and a 0x32 timer (+0x166); and debits
 * the spawned head-count from the region capacity.  `mp_active` is forwarded to
 * the demolish path.  All callees ported. */
int engine_combat_spawn_rebellion(int region, engine_event_queue_state_t *evq,
                                  int mp_active)
{
    if (REGREC(region)[R_OWNER] == 0)
        return 0;                          /* unowned regions never rebel */

    int col = 0, row = 0;
    int hit = engine_game_init_army_near_rawflag_bit0(region, &col, &row);
    if (hit == 0)
        hit = engine_game_init_army_near_rawflag_only_bit1(region, &col, &row);
    if (hit == 0)
        return 0;                          /* no placeable cell near the seat */

    if (engine_territory_place_entity(2, col, row, 6) == 0)
        return 0;                          /* cell taken / record table full  */
    int slot = engine_territory_current_slot();

    engine_combat_demolish_realm(region, evq, mp_active);   /* FUN_004ac3c6 */

    uint8_t *c = CREC(slot);
    c[0x150]      = 1;
    c[0x01]       = 0;
    c[C_ARMYKEY]  = (uint8_t)region;                        /* +0x10 region    */
    *(int16_t *)(c + 0x164) = (int16_t)s_slot_00553edc;     /* spawn turn      */
    c[0x166]      = 0x32;                                   /* spawn timer     */
    c[0x02]       = 0;

    int head = engine_math_percent_of(*(int32_t *)(REGREC(region) + R_CAP), 0x1e);
    *(int32_t *)(c + C_VALUE) = head;                       /* +0x168 head     */
    for (int i = 0; i < 7; i++)
        *(int16_t *)(c + C_COUNTS + i * 2) = 0;
    *(int16_t *)(c + C_COUNTS) = (int16_t)head;             /* class-0 count   */
    *(int16_t *)(c + C_RESULT) = (int16_t)head;             /* +0x17a result   */
    c[0x14f]      = 0;

    *(int32_t *)(REGREC(region) + R_CAP) -= head;           /* debit capacity  */
    return 1;
}

/* Port of FUN_004abd0f @ 0x4abd0f (616 B, __cdecl void(int army,uint region)).
 * Reconcile an army SETTLING onto a region (the per-tick driver's state-2
 * handler).  On a NEUTRAL region it just latches the army's effective-region
 * colour (+0x10).  On an OWNED region it acts only when the army's colour is
 * not already this region: it warns the region owner about the occupation,
 * with the message and reaction scaled by the region's popularity reserve
 * (+0x0c):
 *   reserve <  0x0a → 0x9a + try to spawn a rebellion (engine_combat_spawn_
 *                     rebellion); on success reset unrest (+0x20) and restore
 *                     +0x1e to both popularity fields;
 *   reserve <  0x1e → 0x9b + latch unrest (+0x20 = 1 if currently 0);
 *   reserve >= 0x1e → 0x9c.
 * Then it debits the popularity reserve/value by 10 (or, when the reserve is
 * below 10, drains the reserve into the value and floors the reserve at 0),
 * and finally clamps the reserve non-negative. */
void engine_combat_army_settle_region(int army, unsigned region,
                                      engine_event_queue_state_t *evq,
                                      int mp_active)
{
    uint8_t *r = REGREC(region);

    if (r[R_OWNER] == 0) {
        CREC(army)[C_ARMYKEY] = (uint8_t)region;
        return;
    }

    if (CREC(army)[C_ARMYKEY] != (uint8_t)region) {
        CREC(army)[C_ARMYKEY] = (uint8_t)region;

        uint8_t hd[7]; build_has_data(hd);
        int local = (int)s_slot_0057c8cc;

        if ((int8_t)r[R_F0C] < 0x0a) {
            engine_event_queue_post(evq, 0, r[R_OWNER], 0x9a, 0, 3,
                                    (uint8_t)region, 0, 0, hd, local);
            if (engine_combat_spawn_rebellion((int)region, evq, mp_active) != 0) {
                r[R_UNREST] = 0;
                r[R_F0C] = (uint8_t)(r[R_F0C] + 0x1e);
                r[R_F17] = (uint8_t)(r[R_F17] + 0x1e);
            }
        } else if ((int8_t)r[R_F0C] < 0x1e) {
            engine_event_queue_post(evq, 0, r[R_OWNER], 0x9b, 0, 3,
                                    (uint8_t)region, 0, 0, hd, local);
            if (r[R_UNREST] == 0)
                r[R_UNREST] = 1;
        } else {
            engine_event_queue_post(evq, 0, r[R_OWNER], 0x9c, 0, 3,
                                    (uint8_t)region, 0, 0, hd, local);
        }

        if ((int8_t)r[R_F0C] < 0x0a) {
            r[R_F17] = (uint8_t)(r[R_F17] - r[R_F0C]);
            r[R_F0C] = 0;
        } else {
            r[R_F0C] = (uint8_t)(r[R_F0C] - 10);
            r[R_F17] = (uint8_t)(r[R_F17] - 10);
        }
    }

    if ((int8_t)r[R_F0C] < 0)
        r[R_F0C] = 0;
}

/* ===================================================================== *
 *  Multiplayer player-resignation pass (FUN_0049b2a3 + FUN_0049cf20).    *
 *                                                                        *
 *  Runs only in multiplayer.  For each in-game player slot that has not  *
 *  already resigned, it removes the player from the game: demolish every *
 *  realm they own, disband every placed unit, notify, then wipe their    *
 *  strategic + aux records.  Reached from the turn orchestrator          *
 *  (FUN_00448440) right after the season rollover.                       *
 * ===================================================================== */

/* Port of FUN_0049cf20 — remove player `player` from the game. */
void engine_combat_resign_player(int player, engine_event_queue_state_t *evq,
                                 int mp_active)
{
    int local = (int)s_slot_0057c8cc;

    /* Interactive on-screen cleanup, game-mode 2 only — deferred UI seam. */
    if (s_slot_0057c8fc == 2 && g_resign_ui_fn)
        g_resign_ui_fn(player);                        /* 0x47fe0b (deferred UI seam) */

    /* Demolish every realm this player owns (army records 1..16). */
    for (int v = 1; v < 0x11; v++)
        if (REGREC(v)[R_OWNER] == (uint8_t)player)
            engine_combat_demolish_realm(v, evq, mp_active);   /* FUN_004ac3c6 */

    /* Disband every live placed unit this player owns (occupant records). */
    for (int v = 1; v < 0x97; v++)
        if (CREC(v)[C_ACTIVE] == 1 && (int8_t)CREC(v)[C_OWNER] == player)
            engine_unit_disband(v);                            /* FUN_004aa039 */

    /* Recount survivors AFTER the destroy loops above (quirk #167): both loops
     * recount over the very tables the demolish/disband loops just cleared —
     * the realm recount walks 1..s_slot_0056d5dc (≤16, the realm array holds
     * only 17 records) which the 1..16 demolish loop fully covers, and the unit
     * recount repeats the disband loop's exact predicate over memset records.
     * So `survivors` is always 0 here and the 0x102 "resigned" branch below is
     * dead — every removed player is reported eliminated (0xe0).  Ported
     * verbatim regardless. */
    int survivors = 0;
    for (int v = 1; v <= (int)s_slot_0056d5dc; v++)
        if (REGREC(v)[R_OWNER] == (uint8_t)player)
            survivors += 3;
    for (int v = 1; v < 0x97; v++)
        if ((int8_t)CREC(v)[C_OWNER] == player && CREC(v)[C_ACTIVE] == 1)
            survivors += 1;

    /* Notification (read the slot byte before the record is wiped). */
    uint8_t hd[7]; build_has_data(hd);
    if (PREC(player)[P_SLOT] == 0 || survivors == 0)
        engine_event_queue_post(evq, player, local, 0xe0, 1, 0xe, 0, 0, 0,
                                hd, local);            /* "eliminated" */
    else
        engine_event_queue_post(evq, 0, local, 0x102, 0, 0, 0, (uint8_t)player, 0,
                                hd, local);            /* "resigned" (rare) */

    /* Wipe the strategic record (the explicit +0x04/+0x0a clears are redundant
     * with the full record clear that follows — ported verbatim). */
    PREC(player)[P_SLOT] = 0;
    PREC(player)[P_F0A]  = 0;
    memset(PREC(player), 0, PSTRIDE);                  /* FUN_0046ea61 */

    /* Reset the per-player aux slot (g_player_aux_44, stride 0x2c). */
    uint8_t *aux = &g_player_aux_44[player * 0x2c];
    aux[0x26] = 0;
    *(int32_t *)&aux[0x00] = 0;
    aux[0x25] = 0;
    aux[0x27] = 1;

    g_combat_screen_mode = 2;                          /* DAT_0057d340 */
}

/* Port of FUN_0049b2a3 — multiplayer player-resignation sweep. */
void engine_combat_mp_resign_sweep(engine_event_queue_state_t *evq, int mp_active)
{
    if (!mp_active)                                    /* DAT_00553030 gate */
        return;
    for (int player = 1; player < 6; player++) {
        /* aux +0x00 set (e.g. connected/human) → leave alone. */
        if (*(int32_t *)&g_player_aux_44[player * 0x2c] != 0)
            continue;
        if (PREC(player)[P_STATUS] != 0)               /* already resigned */
            continue;
        if (PREC(player)[P_ACTIVE] == 0)               /* not in-game */
            continue;
        engine_combat_resign_player(player, evq, mp_active);
        s_slot_00553f98 -= 1;                          /* active-player count-- */
        PREC(player)[P_STATUS] = 6;                    /* mark resigned */
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * Settlement-spawn cluster (reached from the per-turn pass FUN_004508de).
 *
 * When a realm finishes building a keep upgrade the turn driver spawns a fresh
 * garrison unit into the realm's secondary settlement.  The cluster:
 *   engine_combat_settlement_find_slot  (FUN_004a551b) — size the garrison from
 *       the keep-capacity tables, prime the owner's muster pool, then spawn,
 *   engine_combat_spawn_unit            (FUN_004a9a9a) — allocate a fresh
 *       occupant record, copy the muster pool into it, debit the owner, re-plan,
 *   engine_combat_place_garrison        (FUN_004a79a3) — home the new occupant
 *       onto the keep cell (or merge it into the existing garrison).
 * Three exclusive helpers below feed engine_combat_spawn_unit.
 * ════════════════════════════════════════════════════════════════════════ */

/* --- army-economy record (g_per_army_768) fields the cluster reads -------- */
#define A_RACE      0x05    /* owning player / race (u8)                    */
#define A_F0C       0x0c    /* per-army settlement byte (i8)                */
#define A_F15       0x15    /* paired settlement byte (i8)                  */
#define A_PROV24    0x24    /* provision / treasury figure A (i32)          */
#define A_F38       0x38    /* provision / treasury figure B (i32)          */
#define A_SEATCOL   0x74    /* seat column (i8)                             */
#define A_SEATROW   0x75    /* seat row (i8)                                */
#define A_F1AD      0x1ad   /* mustered-unit scratch index (u8)             */
#define A_PENDING   0x1bc   /* pending garrison occupant index (i32)        */
#define A_KEEPLVL   0x1c0   /* keep level (u8)                              */
#define A_BUILTLVL  0x1c1   /* currently-built keep level (u8)              */
#define A_F2F4      0x2f4   /* post-spawn refresh marker (i32)              */

/* --- strategic record (g_player_strategic_352) fields -------------------- */
#define P_F150      0x150   /* running garrison-headcount reserve (i32)     */
#define P_WAGE      0xfc    /* total wage bill (i32)                        */
#define P_F118      0x118   /* treasury / cashflow figure (i32, per owner)  */
#define P_LOAD      0x2d    /* 0x18-byte roster load-balance array (u8 each)*/
#define P_CAT       0x13c   /* cashflow category base; cats 1..6 at +i*4    */

/* --- per-owner troop muster pool (g_troop_pool, stride 0x80) ------------- */
#define M_SLOT      0x00    /* 8 contingent slots × 0x10; +0 = count (i32)  */
#define M_F50       0x50    /* staged garrison size A (i32)                 */
#define M_F70       0x70    /* staged garrison size B (i32) → occupant value*/

/* --- g_unit_table_20 (retail 0x568dc0, 13 × 20 B) ------------------------ */
#define U_STRIDE    0x14
#define U_BACKREF   0x00    /* occupant back-reference (i16)                */
#define U_F03       0x03    /* cleared on attach (u8)                       */
#define U_CLASS     0x05    /* contingent troop class (u8)                  */
#define U_AMT       0x08    /* head-count / value (i32; low byte = count)   */
#define U_COST      0x0c    /* cashflow cost charged to the owner (i32)     */

/* Keep-capacity table (retail .rdata 0x4d8a3c, 12 × i32).  FUN_004a551b reads
 * it from base 0x4d8a40 (= &k_keep_tbl[1]) indexed by (keeplvl-1) → k_keep_tbl
 * [keeplvl], and from base 0x4d8a3c indexed by the built level. */
static const int32_t k_keep_tbl[12] = {
    0, 50, 150, 150, 200, 300, 0, 1, 2, 100, 500, 5,
};

/* Garrison head-count cap by keep level (retail .rdata 0x4d8a0c, 10 × i32). */
static const int32_t k_garrison_cap[10] = {
    2500, 150, 200, 200, 400, 600, 0, 50, 75, 100,
};

/* Port of FUN_004a9f18 (90 B) — debit the owner's just-staged garrison size
 * (muster pool row +0x70) from realm `key`'s two provision figures
 * (+0x24 / +0x38).  Pool row selected by the realm's race (+0x05). */
static void spawn_debit_provision(int key)
{
    uint8_t *a = REGREC(key);
    const uint8_t *pool = engine_game_init_troop_pool_row(a[A_RACE]);
    int32_t staged = *(const int32_t *)(pool + M_F70);
    wr_i32(a, A_PROV24, rd_i32(a, A_PROV24) - staged);
    wr_i32(a, A_F38,    rd_i32(a, A_F38)    - staged);
}

/* Port of FUN_004a9f72 (199 B) — pick player `slot`'s least-loaded roster
 * entry (the lowest of the 0x18-byte load array at strat +0x2d), bump it by 2,
 * and return its index.  Returns 0 for out-of-range players. */
static int spawn_pick_roster_slot(int slot)
{
    if (slot < 1 || slot > 5)
        return 0;
    uint8_t *load = PREC(slot) + P_LOAD;
    int best = 0;
    unsigned lo = 0x101;            /* retail sentinel above any u8 */
    for (int i = 0; i < 0x18; i++) {
        if (load[i] < lo) {
            best = i;
            lo = load[i];
        }
    }
    load[best] = (uint8_t)(load[best] + 2);
    return best;
}

/* Port of FUN_004a9eb1 (103 B) — debit the owner's muster-pool per-slot stock
 * (rows 1..6 of the pool) from its six cashflow categories (strat +0x140..),
 * then resync the cashflow total.  `slot` indexes both pool and strat. */
static void spawn_debit_cashflow(int slot)
{
    uint8_t *strat = PREC(slot);
    const uint8_t *pool = engine_game_init_troop_pool_row(slot);
    for (int i = 1; i < 7; i++) {
        int32_t *cat = (int32_t *)(strat + P_CAT + i * 4);
        *cat -= *(const int32_t *)(pool + i * 0x10);
    }
    engine_ranking_recompute_player_total(slot);   /* FUN_004487a9 */
}

/* Port of FUN_004ac7f3 (273 B) — attach mustered g_unit_table_20 unit `unit`
 * to occupant `occ` as its mercenary contingent: copy class/amount, fold the
 * unit's value into the occupant head-count (+0x168), set the back-reference,
 * charge the owner's treasury (strat +0x118) the unit cost, and clear the
 * source army's mustered-unit scratch (+0x1ad). */
static void spawn_attach_muster_unit(int occ, int unit)
{
    uint8_t *c = CREC(occ);
    uint8_t *u = &g_unit_table_20[unit * U_STRIDE];

    int8_t owner = (int8_t)c[C_OWNER];
    uint8_t army = c[C_F11];

    c[C_CON_SLOT]  = (uint8_t)unit;             /* +0x197 */
    c[C_CON_CLASS] = u[U_CLASS];                /* +0x195 ← unit +0x05 */
    c[C_CON_AMT]   = u[U_AMT];                  /* +0x196 ← unit +0x08 low byte */
    wr_i32(c, C_VALUE, rd_i32(c, C_VALUE) + *(int32_t *)(u + U_AMT));

    *(int16_t *)(u + U_BACKREF) = (int16_t)occ;
    u[U_F03] = 0;

    int32_t *treasury = (int32_t *)(PREC(owner) + P_F118);
    *treasury -= *(int32_t *)(u + U_COST);

    REGREC(army)[A_F1AD] = 0;
}

/* Port of FUN_004a9a9a (1047 B) — spawn a fresh occupant record near army
 * `army`'s map seat, populated from owner `owner`'s primed muster pool.  Returns
 * the created occupant index (DAT_0052afe0) on success, else 0.  `attach_unit`
 * non-zero attaches the army's mustered unit (+0x1ad) as a merc contingent;
 * `supply_cost` is debited from the army's settlement byte pair (+0x0c/+0x15).
 * The settlement path passes attach_unit=0, supply_cost=0. */
int engine_combat_spawn_unit(int owner, int army, int attach_unit, int supply_cost)
{
    /* Refuse unless a free, plantable map cell lies within radius 1..3 of the
     * army's seat (FUN_00428007 bit0, else FUN_00428078 flag∈{0,2}).  The found
     * cell is reported in col/row — retail leaves it in DAT_00569570/78. */
    int col = 0, row = 0;
    if (!engine_game_init_army_near_rawflag_bit0(army, &col, &row) &&
        !engine_game_init_army_near_rawflag_only_bit1(army, &col, &row))
        return 0;

    if (!engine_territory_place_entity(1, col, row, owner))   /* FUN_0046e1b0 */
        return 0;

    int slot = engine_territory_current_slot();               /* DAT_0052afe0 */
    uint8_t *c = CREC(slot);
    uint8_t *a = REGREC(army);

    c[C_F06]      = 1;
    c[C_F150]     = 1;
    c[C_GARRISON] = PREC(owner)[P_ACTIVE];        /* +0x01 ← strat owner +0x05 */
    c[C_ARMYKEY]  = (uint8_t)army;                /* +0x10 */
    c[C_F11]      = (uint8_t)army;                /* +0x11 */
    wr_i16(c, C_F164, (int16_t)s_slot_00553edc);  /* +0x164 spawn epoch */
    c[C_F166]     = a[A_F0C];                      /* +0x166 ← army +0x0c */

    if (owner == 0) {
        c[C_COUNT] = 0;                            /* +0x02 */
        c[C_OWNER] = 6;                            /* +0x00 */
    } else {
        c[C_COUNT] = PREC(owner)[P_F0A];           /* +0x02 ← strat owner +0x0a */
    }

    spawn_debit_provision(army);                   /* FUN_004a9f18 */

    const uint8_t *pool = engine_game_init_troop_pool_row(owner);
    wr_i32(c, C_VALUE, *(const int32_t *)(pool + M_F70));   /* +0x168 */
    for (int k = 0; k < 7; k++)
        wr_i16(c, C_COUNTS + k * 2,
               (int16_t)*(const int32_t *)(pool + k * 0x10 + M_SLOT));

    c[C_F14F] = (uint8_t)spawn_pick_roster_slot(army);       /* FUN_004a9f72 */
    spawn_debit_cashflow(owner);                             /* FUN_004a9eb1 */

    if (attach_unit != 0)
        spawn_attach_muster_unit(slot, REGREC(army)[A_F1AD]); /* FUN_004ac7f3 */

    engine_realm_collect_territory_income();                 /* FUN_004ad6c0 */
    engine_unit_recompute_wage(slot);                        /* FUN_004ad52b */

    /* Re-plan the army's economy twice (retail repeats the trio verbatim). */
    for (int pass = 0; pass < 2; pass++) {
        engine_territory_allocate_realm_workforce(army);             /* 44f6e7 */
        engine_army_economy_provision(army, (int)s_slot_0057c934);   /* 44df5f */
        engine_army_economy_plan_realm(army, (int)s_slot_0057c92c);  /* 4485a5 */
    }

    /* Debit the supply cost from the army's settlement byte pair. */
    if ((int8_t)a[A_F0C] < supply_cost) {
        a[A_F15] = (uint8_t)((int8_t)a[A_F15] - (int8_t)a[A_F0C]);
        a[A_F0C] = 0;
    } else {
        a[A_F0C] = (uint8_t)((int8_t)a[A_F0C] - (int8_t)supply_cost);
        a[A_F15] = (uint8_t)((int8_t)a[A_F15] - (int8_t)supply_cost);
    }

    wr_i32(a, A_F2F4, 0xf);
    *(int32_t *)(PREC(owner) + P_WAGE) = engine_unit_total_wage(owner); /* 4ad495 */

    if ((int)s_slot_0057c8cc == owner) {           /* spawn belongs to viewer */
        engine_territory_mark_draw_box((int8_t)c[C_COL], (int8_t)c[C_ROW], 6);
        engine_territory_mark_full_screen_dirty(); /* FUN_0045240a */
    }
    return 1;
}

/* Port of FUN_004a551b (332 B) — size the garrison for realm `key` from the
 * keep-capacity tables, prime the owner's muster pool, and spawn the unit.
 * Returns the new occupant index, or 0 when no garrison is due (capacity used
 * up, or the staged size exceeds half the realm's provision +0x24). */
int engine_combat_settlement_find_slot(int key)
{
    PREC(0)[P_ACTIVE] = 0;          /* reserved slot 0 guard (DAT_0057bf05) */

    uint8_t *a = REGREC(key);
    int race = a[A_RACE];
    int size = k_keep_tbl[a[A_KEEPLVL]];           /* 0x4d8a40[keeplvl-1] */

    if (a[A_BUILTLVL] != 0) {
        size -= k_keep_tbl[a[A_BUILTLVL]];         /* 0x4d8a3c[builtlvl] */
        if (size <= 0)
            return 0;
    }

    if (rd_i32(a, A_PROV24) / 2 < size)
        return 0;

    uint8_t *pool = engine_game_init_troop_pool_row(race);
    for (int c = 0; c < 8; c++)
        *(int32_t *)(pool + c * 0x10 + M_SLOT) = 0;
    *(int32_t *)(PREC(race) + P_F150) += size;
    *(int32_t *)(pool + M_F50) = size;
    *(int32_t *)(pool + M_F70) = size;

    if (!engine_combat_spawn_unit(race, key, 0, 0))
        return 0;
    return engine_territory_current_slot();        /* DAT_0052afe0 */
}

/* Port of FUN_004a79a3 (767 B) — home freshly-spawned occupant `occ` onto realm
 * `key`'s keep cell.  Caps the combined head-count against the keep-level table;
 * if over cap the occupant is rejected (role 2).  Otherwise, if no garrison is
 * pending it becomes the garrison (role 5) on the seat cell, else it is merged
 * into the existing garrison.  Re-collects territory income and latches the
 * view-dirty flag when the keep belongs to the viewing player's selection. */
void engine_combat_place_garrison(unsigned occ, int key,
                                  engine_event_queue_state_t *evq)
{
    uint8_t *c = CREC((int)occ);
    uint8_t *a = REGREC(key);
    int32_t pending = rd_i32(a, A_PENDING);

    c[C_F150] = 1;
    int32_t head = rd_i32(c, C_VALUE);
    if (pending != 0)
        head += rd_i32(CREC(pending), C_VALUE);

    if (k_garrison_cap[a[A_KEEPLVL]] < head) {
        c[C_ROLE]     = 2;                          /* rejected */
        c[C_FLAG_14C] = 0;
        return;
    }

    if (rd_i32(a, A_PENDING) == 0) {
        wr_i32(a, A_PENDING, (int32_t)occ);
        c[C_ROLE]    = 5;                            /* garrison */
        c[C_F198]    = (uint8_t)key;
        c[C_HOMECOL] = a[A_SEATCOL];
        c[C_COL]     = c[C_HOMECOL];
        c[C_HOMEROW] = a[A_SEATROW];
        c[C_ROW]     = c[C_HOMEROW];
        engine_unit_map_unlink((int)occ);           /* FUN_0046ec6e */
        wr_i32(c, C_CELLOFS, ((int8_t)c[C_ROW] * 0x40 + (int8_t)c[C_COL]) * 8);
        c[C_SCRATCH_1C] = 0;
        c[C_FLAG_14C]   = 0;
        c[C_F150]       = 1;
        c[C_F06]        = 1;
        c[C_F153]       = (uint8_t)((int8_t)c[C_F153] + 5);
        c[C_F151]       = (uint8_t)key;
    } else {
        engine_combat_merge_stack(pending, (int)occ, evq);   /* FUN_004aa181 */
    }

    engine_realm_collect_territory_income();        /* FUN_004ad6c0 */
    if (a[A_RACE] == (uint8_t)s_slot_0057c8cc && (int)s_slot_0057c93c == key)
        g_combat_flag_530d0 = 1;
}

/* ════════════════════════════════════════════════════════════════════════
 * Combat-resolution defender muster (reached from combat_b FUN_004a6c68).
 *
 * Before a contested region's fight is sized, the resolver stages the
 * defending side's troops in the owner's muster pool.  The garrison/auto-
 * defender builder FUN_004a50ae (not yet ported) first resets the pool grid
 * and seeds it from the owner's six cashflow-category reserves, then
 * redistributes/recruits from it before spawning the defending unit.
 * ════════════════════════════════════════════════════════════════════════ */

/* Extra muster sub-row fields this path stages (slot stride 0x10). */
#define M_RES_A     0x04    /* per-slot reserve copy A (i32, 0x53f6a4)      */
#define M_RES_B     0x08    /* per-slot reserve copy B (i32, 0x53f6a8)      */

/* Port of FUN_004aaa80 (344 B, __cdecl void(int region, int32_t count)).
 * Reset region `region`'s owner muster pool and seed it for a staged defender
 * of `count` head.  Clears the +0x00 and +0x04 fields of all eight contingent
 * slots, copies the owner's six cashflow-category reserves (strat P_CAT cats
 * 1..6, retail +0x140..+0x154) into slots 1..6 fields +0x04 and +0x08, then
 * parks `count` in slots 0 and 7 fields +0x00 and +0x08.  Pool row + strat
 * reserves both indexed by the region owner (+0x05). */
void engine_combat_prime_muster(int region, int32_t count)
{
    int slot = REGREC(region)[A_RACE];
    uint8_t       *pool  = engine_game_init_troop_pool_row(slot);
    const uint8_t *strat = PREC(slot);

    for (int i = 0; i < 8; i++)
        *(int32_t *)(pool + i * 0x10 + M_SLOT)  = 0;
    for (int i = 0; i < 8; i++)
        *(int32_t *)(pool + i * 0x10 + M_RES_A) = 0;
    for (int i = 1; i < 7; i++)
        *(int32_t *)(pool + i * 0x10 + M_RES_A) =
            *(const int32_t *)(strat + P_CAT + i * 4);
    for (int i = 1; i < 7; i++)
        *(int32_t *)(pool + i * 0x10 + M_RES_B) =
            *(const int32_t *)(strat + P_CAT + i * 4);

    *(int32_t *)(pool + 0x00 + M_SLOT)  = count;   /* slot 0 +0x00 (0x53f6a0) */
    *(int32_t *)(pool + 0x00 + M_RES_B) = count;   /* slot 0 +0x08 (0x53f6a8) */
    *(int32_t *)(pool + 0x70 + M_SLOT)  = count;   /* slot 7 +0x00 (0x53f710) */
    *(int32_t *)(pool + 0x70 + M_RES_B) = count;   /* slot 7 +0x08 (0x53f718) */
}

/* Percentage→muster-cost curve (retail .rdata 0x4d8778, 102 × i32, indices
 * 0..101).  A clamped diminishing-returns ramp: input percent maps to a supply
 * cost that saturates at 101.  Indexed by the percentage arg below. */
static const int32_t k_muster_cost[102] = {
      0,   1,   1,   1,   2,   2,   3,   3,   4,   4,
      5,   5,   6,   6,   7,   7,   8,   8,   9,   9,
     10,  11,  13,  15,  17,  19,  21,  23,  25,  27,
     29,  31,  34,  37,  40,  44,  48,  52,  56,  60,
     64,  68,  72,  75,  78,  80,  82,  84,  86,  88,
     90,  91,  92,  93,  94,  95,  96,  97,  98,  99,
    100, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101, 101, 101, 101, 101, 101, 101, 101, 101,
    101, 101,
};

/* Port of FUN_004a50ae (731 B, __cdecl int(int region, int pct, int min_prov,
 * int mode)).  Raise a defending garrison unit in region `region`, sized at
 * `pct` percent of the region's provision (+0x24), and spawn it.  Returns the
 * new occupant index (DAT_0052afe0) or 0 when the region's provision is below
 * `min_prov` (or no map cell is free for the spawn).  Three staging modes:
 *
 *   mode 2 — fresh garrison: spawn it free (supply cost 0), seat three strat
 *     reserve cells (+0x144/+0x14c/+0x150) at 500, then distribute the sized
 *     head into troop-class slots 2/4/5 by size band, leaving the remainder in
 *     slot 0.
 *   mode 1 — top-up: recruit from the owner's primed reserves (slot +0x08),
 *     moving 10 head at a time into the class counts (slot +0x00) until the
 *     staged budget (slot 7 +0x00) is spent or 50 sweeps elapse.
 *   mode 0 — plain spawn at the muster-cost supply price.
 *
 * combat_b (FUN_004a6c68) calls it with mode 2/1/0 to materialise a region's
 * auto-defender when no live capital garrison is found. */
int engine_combat_muster_defender(int region, int pct, int min_prov, int mode)
{
    g_player_strategic_352[5] = 0;                  /* DAT_0057bf05 = 0 */

    int owner = REGREC(region)[A_RACE];
    int prov  = rd_i32(REGREC(region), A_PROV24);   /* region +0x24 */
    if (prov < min_prov)
        return 0;

    int32_t supply = k_muster_cost[pct];            /* DAT_004d8778[pct] */
    int     sized  = engine_math_percent_of(prov, pct);
    engine_combat_prime_muster(region, sized);

    uint8_t *pool  = engine_game_init_troop_pool_row(owner);
    uint8_t *strat = PREC(owner);

    if (mode == 2) {
        *(int32_t *)(strat + 0x150) = 500;          /* DAT_0057c050 */
        *(int32_t *)(strat + 0x14c) = 500;          /* DAT_0057c04c */
        *(int32_t *)(strat + 0x144) = 500;          /* DAT_0057c044 */
        supply = 0;

        /* Distribute the sized head into class slots by size band; the leftover
         * stays in slot 0 (debited by the staged amount). */
        if (sized < 0x1e0) {
            if (sized < 0x168) {
                if (sized < 0xf0) {
                    if (sized >= 0x78) {
                        *(int32_t *)(pool + 5 * 0x10)  = 0x3c;
                        *(int32_t *)(pool + 0 * 0x10) -= 0x3c;
                    }
                } else {
                    *(int32_t *)(pool + 5 * 0x10)  = 0x50;
                    *(int32_t *)(pool + 4 * 0x10)  = 0x28;
                    *(int32_t *)(pool + 0 * 0x10) -= 0x78;
                }
            } else {
                *(int32_t *)(pool + 5 * 0x10)  = 100;
                *(int32_t *)(pool + 4 * 0x10)  = 0x46;
                *(int32_t *)(pool + 0 * 0x10) -= 0xaa;
            }
        } else {
            *(int32_t *)(pool + 5 * 0x10)  = 0x96;
            *(int32_t *)(pool + 4 * 0x10)  = 100;
            *(int32_t *)(pool + 2 * 0x10)  = 0x32;
            *(int32_t *)(pool + 0 * 0x10) -= 300;
        }
    } else if (mode == 1) {
        int32_t budget = *(int32_t *)(pool + M_F70);   /* slot 7 +0x00 = sized */
        int  sweep = 0;
        bool moved = true;
        while (sweep < 0x32 && moved) {
            moved = false;
            for (int c = 1; c < 7; c++) {
                if (budget < 10)
                    goto spawn;
                if (*(int32_t *)(pool + c * 0x10 + M_RES_B) > 9) {
                    *(int32_t *)(pool + c * 0x10 + M_SLOT)  += 10;
                    *(int32_t *)(pool + c * 0x10 + M_RES_B) -= 10;
                    *(int32_t *)(pool + 0 * 0x10)           -= 10;
                    budget -= 10;
                    moved = true;
                }
            }
            sweep++;
        }
    }

spawn:
    if (!engine_combat_spawn_unit(owner, region, 0, supply))
        return 0;
    return engine_territory_current_slot();         /* DAT_0052afe0 */
}
