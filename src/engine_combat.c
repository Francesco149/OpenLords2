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
#define C_F198      0x198   /* region-transfer pending flag (u8)            */
#define C_F199      0x199   /* survives-this-battle flag (u8)               */
#define C_F19A      0x19a   /* defender-occupant index when this army moves (u8)*/

/* --- per-region army-economy record (g_per_army_768, stride 0x300) ------- */
#define RSTRIDE     0x300
#define REGREC(k)   (&g_per_army_768[(k) * RSTRIDE])
#define R_OWNER     0x05    /* region owning player (u8)                    */
#define R_F07       0x07    /* owner's banner/colour key (u8, ← strat +0xa) */
#define R_F0C       0x0c    /* region popularity reserve (i8)               */
#define R_F17       0x17    /* region popularity (i8)                       */
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

/* Deferred-audio + render-clock seams (all default NULL / silent). */
static void     (*g_combat_wav_fn)(const char *, int, int);
static void     (*g_combat_click_fn)(void);
static uint32_t (*g_combat_clock_fn)(void);

void engine_combat_bind_wav_fn(void (*fn)(const char *, int, int)) { g_combat_wav_fn = fn; }
void engine_combat_bind_click_fn(void (*fn)(void))                 { g_combat_click_fn = fn; }
void engine_combat_bind_clock_fn(uint32_t (*fn)(void))             { g_combat_clock_fn = fn; }

static void     combat_wav(const char *n, int a, int b) { if (g_combat_wav_fn) g_combat_wav_fn(n, a, b); }
static uint32_t combat_clock(void) { return g_combat_clock_fn ? g_combat_clock_fn() : 0; }

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
