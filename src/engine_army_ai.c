/*
 * OpenLords2 — per-realm AI economic-management layer.  See engine_army_ai.h.
 *
 * The AI "brain" the turn-advance engine runs once per turn for every realm an
 * AI player owns: reached from FUN_00448440 (the turn driver) → FUN_0049a990
 * (loop over players still in the game) → FUN_0049dd01 (per the player's realms)
 * → one of three personality dispatchers (FUN_004a4052/42e3/440f).  Each
 * dispatcher buys/sells resources to hit target stocks, sweeps the tax rate for
 * the best net, and assigns harvest/labour to feature cells, then re-plans the
 * realm's economy.
 *
 * Operates on the shared strategic globals directly (g_per_army_768 = the
 * per-realm record block, g_player_strategic_352 = the per-player block) plus
 * the cell roster / grid engine_territory owns, and calls into the deterministic
 * economy model (engine_army_economy_*), the territory cell mutators
 * (engine_territory_*), and the ranking accumulator (engine_ranking_*).
 */

#include <stdint.h>

#include "engine_army_ai.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352 */
#include "save_schema_globals.h"   /* s_slot_0057c934 (event season), 0057c92c (map season) */
#include "engine_army_economy.h"   /* engine_army_economy_provision / _plan_realm */
#include "engine_territory.h"      /* recensus_reduce / grade_feature_cells / allocate_workforce */
#include "engine_ranking.h"        /* engine_ranking_recompute_player_total */
#include "engine_math.h"           /* engine_math_percent_of (FUN_00404d6b) */
#include "engine_event_queue.h"    /* engine_event_queue_post (FUN_00472bc5) */
#include "engine_diplomacy.h"      /* clear_treaty / battle_adjust / best_ally / form_alliance / should_engage */
#include "engine_realm.h"          /* engine_realm_player_owns_neighbor (FUN_00467eb0) */
#include "engine_combat.h"         /* raise_region_unit / find_deploy_cell (step-0xa attack) */
#include "engine_path_field.h"     /* cost-field fill/trace/copy (brain step 0xb deployment) */
#include "engine_path.h"           /* engine_path_dist_chebyshev (FUN_00404f4c + its dx/dy globals) */
#include "engine_realm_upkeep.h"   /* unit_disband / clear_contract / total_wage (step-0xb disband) */
#include "engine_army_state.h"     /* arm_movement_owned (FUN_004a4e3d — brain terminal check) */
#include "game_init.h"             /* army_near_rawflag_only_bit1 (step-7 caravan settle probe) */
#include "engine_map_query.h"      /* nearest_feature_ring_at (step-6 keep-evict relocate) */
#include "call_trace.h"            /* CALL_TRACE_ENTER (per-tick brain dispatch marker) */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])

#define I32(p)  (*(int32_t *)(p))

/* ── Per-realm record fields (offsets into g_per_army_768 row `key`) ── */
#define ARMY_OWNER          0x005   /* DAT_0053f9b5 — owning player (0 = independent) */
#define ARMY_MORALE         0x00b   /* DAT_0053f9bb — signed morale/standing stat (i8) */
#define ARMY_APPROVAL       0x00c   /* DAT_0053f9bc — signed approval byte, capped 100 */
#define ARMY_KEEP_LEVEL     0x1c0   /* DAT_0053fb70 — keep level (u8; 0 = no keep) */
#define ARMY_FEED_COUNT     0x024   /* DAT_0053f9d4 — mouths to feed (tier denominator) */
#define ARMY_POP_SUPPORTED  0x030   /* DAT_0053f9e0 — percent-of-population scratch, bumped
                                     *   alongside the feed count by the difficulty grant */
#define ARMY_RATION         0x0b9   /* DAT_0053fa69 — realm ration/tax setting (HUD "ration %",
                                     *   consumed by the engine_realm income calc); set here */
/* Workforce target-ratio run (+0x130 .. +0x14c, eight i32) the AI seeds before
 * the workforce allocator (engine_territory_allocate_realm_workforce) runs. */
#define ARMY_WF_RATIO_0     0x130   /* DAT_0053fae0 */
#define ARMY_WF_RATIO_1     0x134   /* DAT_0053fae4 */
#define ARMY_WF_RATIO_2     0x138   /* DAT_0053fae8 */
#define ARMY_WF_RATIO_3     0x13c   /* DAT_0053faec */
#define ARMY_WF_RATIO_4     0x140   /* DAT_0053faf0 */
#define ARMY_WF_RATIO_5     0x144   /* DAT_0053faf4 */
#define ARMY_WF_RATIO_6     0x148   /* DAT_0053faf8 */
#define ARMY_WF_RATIO_7     0x14c   /* DAT_0053fafc */
#define ARMY_TIER_PROGRESS  0x194   /* DAT_0053fb44 — cumulative advancement (i32) */
#define ARMY_BUILD_DRAW_LO  0x1d0   /* DAT_0053fb80 — settlement build demand, pool +0x128 */
#define ARMY_BUILD_DRAW_HI  0x1d4   /* DAT_0053fb84 — settlement build demand, pool +0x130 */
#define ARMY_SETTLE_FLAG    0x1c3   /* DAT_0053fb73 — secondary settlement present */
#define ARMY_TIER_STEP      0x219   /* DAT_0053fbc9 — advancement tier byte (0..5) */
#define ARMY_CAP_COL        0x06c   /* DAT_0053fa1c — capital cell column (u8) */
#define ARMY_CAP_ROW        0x06d   /* DAT_0053fa1d — capital cell row (u8) */
#define ARMY_PENDING        0x1bc   /* DAT_0053fb6c — pending garrison occupant (i32) */
#define ARMY_BUILD_TAG      0x290   /* DAT_0053fc40 — rotating build-order tag (0..5); the AI
                                     *   pulls one per turn from its personality build column */
#define ARMY_BUILD_AFFORD   0x2c5   /* DAT_0053fc75 — set when the player can afford the current
                                     *   build tag's two pool costs (mark_build_affordable) */
#define ARMY_BUILD_PLAN_FLAG 0x1b0  /* DAT_0053fb60 — "build plan settled" flag, lit once the AI
                                     *   has enabled this realm's job-bucket gates (== WF_GATE_C3B) */
/* The four stride-0x18 settlement job-bucket descriptors (retail +0x295/+0x296/
 * +0x297 .. +0x2df).  +0x295 is the "building present" flag (== ARMY_FEAT_C_FLAG
 * for bucket 0), +0x296 a lock byte, +0x297 the workforce-allocator enable gate
 * (== WF_GATE_C6 for bucket 0). */
#define ARMY_BUILDGATE_PRESENT 0x295 /* DAT_0053fc45 — descriptor present (i8) */
#define ARMY_BUILDGATE_LOCK    0x296 /* DAT_0053fc46 — in-progress lock (i8; must be clear) */
#define ARMY_BUILDGATE_ENABLE  0x297 /* DAT_0053fc47 — workforce enable gate written here */
#define ARMY_BUILDGATE_STRIDE  0x18  /* per-bucket stride for the three fields above */

/* ── Per-player record fields (offsets into g_player_strategic_352 row p) ── */
#define STRAT_BUILD_ROTATE  0x06c   /* DAT_0057bf6c — per-player build-order rotation counter (i32,
                                     *   cycles 0..9, selects the personality build column) */
#define STRAT_TARGET_OWNER  0x048   /* DAT_0057bf48 — owner the target search is gated to when the
                                     *   gate flag is set */
#define STRAT_ATTACK_TARGET 0x04b   /* DAT_0057bf4b — chosen attack-target realm (step 0xa marches
                                     *   its mustered force here via the deploy-cell finder) */
#define STRAT_PINNED_ENEMY  0x0eb   /* DAT_0057bfeb — pinned war target player; overrides the
                                     *   relation scan in most_hostile_player when non-zero */
#define STRAT_ATTACK_COOLDOWN 0x15a /* DAT_0057c05a — turns until the AI may launch its next attack */
#define STRAT_MUSTER_REALM  0x0e5   /* DAT_0057bfe5 — richest owned realm; the muster source step
                                     *   0xa raises its offensive force from (also read by the
                                     *   spawn/seize paths) */
#define STRAT_WEAK_REALM    0x0e6   /* DAT_0057bfe6 — poorest owned realm by the inverse score; no
                                     *   shipped reader consumes it */
#define STRAT_POOL_128      0x128   /* DAT_0057c028 — resource pool drawn by +0x1d0 */
#define STRAT_POOL_130      0x130   /* DAT_0057c030 — resource pool drawn by +0x1d4 */
#define STRAT_CASH          0x118   /* DAT_0057c018 — player treasury */
#define STRAT_BUY_LEDGER_A  0x104   /* DAT_0057c004 — buy expenditure accumulator A */
#define STRAT_BUY_LEDGER_B  0x108   /* DAT_0057c008 — buy expenditure accumulator B */
#define STRAT_SELL_LEDGER_A 0x10c   /* DAT_0057c00c — sell revenue accumulator A */
#define STRAT_SELL_LEDGER_B 0x110   /* DAT_0057c010 — sell revenue accumulator B */
#define ARMY_TREASURY       0x1f4   /* DAT_0053fba4 — independent realm's own gold */
#define ARMY_PROVISION_RAW  0x224   /* DAT_0053fbd4 — commodity slot 1 stock */
#define ARMY_STOCK_A        0x250   /* DAT_0053fc00 — commodity slot 2 stock */
#define ARMY_TRADE_GATE     0x1a4   /* DAT_0053fb54 — byte: realm may trade when set */
#define ARMY_RACE_IDX       0x1a5   /* DAT_0053fb55 — territory-420 index for price lookup */
#define ARMY_CENSUS_HIGH    0x200   /* DAT_0053fbb0 — census 0x0f..0x16 bucket */
#define ARMY_OVERHEAD_HI    0x198   /* DAT_0053fb48 — overhead term 1 */
#define ARMY_OVERHEAD_LO    0x19c   /* DAT_0053fb4c — overhead term 2 */
#define ARMY_STRENGTH_BAND  0x15e   /* DAT_0053fb0e — 0..5 strength band the sweep classifies */
#define ARMY_PROVISION_NET  0x15d   /* DAT_0053fb0d — provision net the sweep maximises */
#define ARMY_TAX_RATE       0x15f   /* DAT_0053fb0f — tax-rate % the sweep selects */
#define ARMY_DENSITY_VALUE  0x25c   /* DAT_0053fc0c — graded density value */

/* g_per_territory_420 field holding a per-territory price-adjustment percent
 * (retail DAT_0052f216 = base 0x52f0b0 + 0x166), indexed by an army's +0x1a5. */
#define TERR420_PRICE_ADJ   0x166

/* Commodity base price: the int at s_slot_005532a0[slot * 0x10] (retail
 * DAT_005532a0, a 16-entry × 0x10-byte price table). */
#define COMMODITY_PRICE(slot) (*(int32_t *)&s_slot_005532a0[(slot) * 0x10])

#define ARMY_STRATEGY_TAG   0x008   /* DAT_0053f9b8 — strategy tag (0x32/0x14/0x28) */
#define ARMY_CENSUS_TYPE1   0x1ff   /* DAT_0053fbaf — census type-1 bucket */
#define ARMY_CENSUS_MID     0x201   /* DAT_0053fbb1 — census 0x02..0x0e bucket */
#define ARMY_YIELD_BONUS    0x208   /* DAT_0053fbb8 — signed yield-bonus (season-4 split) */
#define ARMY_DISPATCH_CODE  0x1fe   /* DAT_0053fbae — personality dispatch code latch */
#define ARMY_RESOURCE_SEL   0x290   /* DAT_0053fc40 — selects the cash-funded commodity */
#define ARMY_REVIEW_FLAG    0x1b0   /* DAT_0053fb60 — per-realm byte flag cleared at the
                                     *   start of the independent-realm AI pass */

/* ── Per-player strategic fields used by the resource orchestrator ── */
#define STRAT_BRAIN_STATE   0x000   /* DAT_0057bf00 — the per-tick brain FSM state /
                                     *   "turn-progress" field (i32).  engine_turn_reset_roster
                                     *   seeds it (0 = mid-turn → re-init, 999 = turn done); the
                                     *   per-tick dispatcher (FUN_0049a581) walks it 0→1→…→0xe→idle
                                     *   then parks it at 999 once movement drains. */
#define STRAT_ACTIVE        0x004   /* DAT_0057bf04 — in-game flag */
#define STRAT_ELIMINATED    0x005   /* DAT_0057bf05 — human-controlled byte (0 = AI).  The
                                     *   grant path above calls it "eliminated"; the FSM
                                     *   (FUN_0049a581) runs a slot's brain iff +4 != 0 &&
                                     *   +5 == 0, so +5 != 0 actually means "a human owns this
                                     *   slot".  The diplomacy step below sends proposals to
                                     *   exactly those human slots. */
#define STRAT_PERSONALITY   0x007   /* DAT_0057bf07 — AI personality 1..4 */
#define STRAT_RANK          0x02b   /* DAT_0057bf2b — 1-based finishing rank (engine_ranking
                                     *   F_RANK); 0 = unranked/inactive */
#define STRAT_DIPLO_GAUGE   0x060   /* DAT_0057bf60 — monotonic diplomacy-readiness counter;
                                     *   proposals gate above 0x28 (alliance) / 0x1c (war) */
#define STRAT_LORD          0x081   /* DAT_0057bf81 — this player's lord (diplomacy D_LORD) */
#define STRAT_PROPOSAL_GATE 0x01c   /* DAT_0057bf1c — set to 1 while an alliance proposal to a
                                     *   human is pending (diplomacy D_FLAG_1C) */
#define STRAT_REL           0x084   /* DAT_0057bf84 — relation toward other (i8; + other*0x10) */
#define STRAT_CONTACT       0x086   /* DAT_0057bf86 — lord-contact / betrayal-pressure accumulator
                                     *   toward other (u8; + other*0x10) */
#define STRAT_ALLY_CAND     0x080   /* DAT_0057bf80 — chosen alliance candidate (step 2) */
#define STRAT_ALLY_COOLDOWN 0x0ec   /* DAT_0057bfec — alliance-proposal cooldown counter (step 2) */
#define STRAT_PROPOSE_TICK  0x0e9   /* DAT_0057bfe9 — ticks toward the next proposal; fires at 8 */
#define STRAT_PROPOSE_PHASE 0x0ea   /* DAT_0057bfea — 0 → broadcast alliance next, 1 → war next */
#define STRAT_MOOD_CYCLE    0x159   /* DAT_0057c059 — notification mood-cycle 0..3 (D_MOODCYC) */
#define STRAT_AI_LEVEL      0x029   /* DAT_0057bf29 — AI difficulty/aggression level (seeded
                                     *   from +0x2a at player setup); gates + scales the grant */
#define STRAT_BUY_REQ_8     0x070   /* DAT_0057bf70 — explicit buy request, commodity 8 */
#define STRAT_BUY_REQ_6     0x074   /* DAT_0057bf74 — explicit buy request, commodity 6 */
#define STRAT_BUY_REQ_7     0x07c   /* DAT_0057bf7c — explicit buy request, commodity 7 */
#define STRAT_POOL_120      0x120   /* DAT_0057c020 — commodity-6 pool */
#define STRAT_TRADE_LOCK    0x158   /* DAT_0057c058 — when set, the orchestrator skips */

/* ── Diplomacy-inbox / pair fields (brain step 1, FUN_004a277d + handlers) ──
 * The per-pair sub-records share STRAT_REL/STRAT_CONTACT's stride (+ other*0x10
 * inside player p's row).  All five below are first touched by the step-1
 * message handlers. */
#define STRAT_AVG_TROOP     0x014   /* DAT_0057bf14 — average troop strength (ranking
                                     *   output F_M_14; tribute-affordability input) */
#define STRAT_PAIR_ALLIED   0x088   /* DAT_0057bf88 — pair: alliance-already-exists flag
                                     *   (u8; + other*0x10) */
#define STRAT_PAIR_CNT89    0x089   /* DAT_0057bf89 — pair: repeat-offer counter
                                     *   (u8; + other*0x10) */
#define STRAT_PAIR_MAXOFFER 0x08c   /* DAT_0057bf8c — pair: best offer seen so far
                                     *   (i32; + other*0x10) */
#define STRAT_PAIR_BYTE90   0x090   /* DAT_0057bf90 — pair: per-turn message scratch byte,
                                     *   reset after the inbox drains (u8; + other*0x10) */
#define STRAT_PAIR_GIFTCNT  0x091   /* DAT_0057bf91 — pair: gifts/tributes paid counter
                                     *   (u8; + other*0x10) */
#define STRAT_GIFT_LAST     0x0e8   /* DAT_0057bfe8 — last gift giver byte (per player) */

/* AI personality tuning table (retail DAT_004d8a58, const .data).  Four
 * personalities, each a 0xf0-byte (= 3 × 0x50) row; retail indexes row p as
 * (p*3 − 3)·0x50 = (p−1)·0xf0.  Verbatim image bytes; the AI reads the dispatch
 * code at +0 and five i32 economy targets at +0x78/+0x7c/+0x84/+0x88/+0x8c. */
#define AI_PERS_STRIDE      0xf0
#define AI_PERS_COUNT       4
#define AI_PERS_DISPATCH    0x00    /* dispatch-code byte (0 / 1 / 9) */
#define AI_PERS_BETRAY_THR  0x10    /* i32: betrayal-pressure threshold (step 2) */
#define AI_PERS_ALLY_PERIOD 0x14    /* i32: alliance-proposal cooldown period (step 2) */
#define AI_PERS_CASH_THRESH 0x78    /* cash floor to fund the discretionary buy */
#define AI_PERS_BUY_AMOUNT  0x7c    /* discretionary buy quantity */
#define AI_PERS_TARGET_8    0x84    /* commodity-8 target stock */
#define AI_PERS_TARGET_7    0x88    /* commodity-7 target stock */
#define AI_PERS_TARGET_6    0x8c    /* commodity-6 target stock */
#define AI_PERS_OFFER_BAND  0x08    /* DAT_004d8a60 — offer-tier band width (step-1 type 0) */
#define AI_PERS_GIFT_UNIT   0x0c    /* DAT_004d8a64 — per-gift display unit (step-1 types 5/6) */
#define AI_PERS_TRIBUTE_FLOOR 0x30  /* DAT_004d8a88 — avg-troop floor below which the AI
                                     *   yields to a tribute demand (step-1 types 5/6) */

_Alignas(int32_t) static const uint8_t g_ai_personality[AI_PERS_COUNT * AI_PERS_STRIDE] = {
    0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0xf4,0x01,0x00,0x00,0x05,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0xee,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
    0x05,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0x02,0x00,0x00,0x00,0x2c,0x01,0x00,0x00,0x06,0x00,0x00,0x00,
    0xe8,0x03,0x00,0x00,0x64,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfa,0x00,0x00,0x00,0xfa,0x00,0x00,0x00,0xfa,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x20,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbc,0x02,0x00,0x00,
    0xc8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe8,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x10,0x27,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0xe8,0x03,0x00,0x00,0x0a,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0x20,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
    0x05,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x78,0x00,0x00,0x00,
    0x03,0x00,0x00,0x00,0x2c,0x01,0x00,0x00,0x0a,0x00,0x00,0x00,
    0xdc,0x05,0x00,0x00,0x50,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x2c,0x01,0x00,0x00,0x2c,0x01,0x00,0x00,0x2c,0x01,0x00,0x00,
    0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x1c,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x8a,0x02,0x00,0x00,
    0x00,0x00,0x00,0x00,0xf4,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0xa0,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0xc8,0x00,0x00,0x00,
    0x40,0x06,0x00,0x00,0x0f,0x00,0x00,0x00,0x08,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0x84,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc8,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0xfa,0x00,0x00,0x00,0x05,0x00,0x00,0x00,
    0xc4,0x09,0x00,0x00,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf4,0x01,0x00,0x00,0xf4,0x01,0x00,0x00,0xf4,0x01,0x00,0x00,
    0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x17,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x58,0x02,0x00,0x00,
    0x00,0x00,0x00,0x00,0x2c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0xd0,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x09,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x32,0x00,0x00,0x00,
    0xdc,0x05,0x00,0x00,0x14,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0xe8,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
    0x04,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0xfa,0x00,0x00,0x00,
    0x05,0x00,0x00,0x00,0x96,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,
    0xa0,0x0f,0x00,0x00,0x96,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xe8,0x03,0x00,0x00,0xe8,0x03,0x00,0x00,0xe8,0x03,0x00,0x00,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x23,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x58,0x02,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x64,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xd0,0x07,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* Personality `p`'s row in the tuning table; `p` is 1..4 by construction (an AI
 * player is assigned one of the four at setup).  Clamped defensively — retail
 * never bounds-checks, but a stray index would read past this image array. */
static const uint8_t *personality_row(int p)
{
    if (p < 1) p = 1;
    if (p > AI_PERS_COUNT) p = AI_PERS_COUNT;
    return &g_ai_personality[(p - 1) * AI_PERS_STRIDE];
}
static int pers_i32(const uint8_t *row, int off) { return *(const int32_t *)(row + off); }

/* AI difficulty cash-grant tables (retail DAT_004dc1e0 / DAT_004dc230, adjacent
 * const .data — baked here as one verbatim 0xa0-byte image).  Each is five rows
 * of four i32 columns: row = personality (0 unused, 1..4), column = the global
 * difficulty s_slot_0053f23c (0..3).  The phase-1 grant indexes one as
 * base + difficulty*4 + personality*0x10.  AI level < 3 draws the DC230 table
 * (smaller grants); AI level >= 3 draws DC1E0 (larger). */
#define AI_GRANT_DC1E0  0x00    /* DAT_004dc1e0 — AI level >= 3 */
#define AI_GRANT_DC230  0x50    /* DAT_004dc230 — AI level <  3 */
_Alignas(int32_t) static const uint8_t g_ai_cash_grant[0xa0] = {
    /* 0x4dc1e0  DAT_004dc1e0 (>= 3) */
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x90,0x01,0x00,0x00, 0xbc,0x02,0x00,0x00, 0xb0,0x04,0x00,0x00,
    0x64,0x00,0x00,0x00, 0xf4,0x01,0x00,0x00, 0x20,0x03,0x00,0x00, 0x78,0x05,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x90,0x01,0x00,0x00, 0xbc,0x02,0x00,0x00, 0xb0,0x04,0x00,0x00,
    0xfa,0x00,0x00,0x00, 0x58,0x02,0x00,0x00, 0x4c,0x04,0x00,0x00, 0x08,0x07,0x00,0x00,
    /* 0x4dc230  DAT_004dc230 (< 3) */
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0xa0,0x00,0x00,0x00, 0xfa,0x00,0x00,0x00, 0x90,0x01,0x00,0x00,
    0x28,0x00,0x00,0x00, 0xb4,0x00,0x00,0x00, 0x2c,0x01,0x00,0x00, 0xf4,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00, 0xa0,0x00,0x00,0x00, 0xfa,0x00,0x00,0x00, 0x90,0x01,0x00,0x00,
    0x64,0x00,0x00,0x00, 0xf0,0x00,0x00,0x00, 0x90,0x01,0x00,0x00, 0x58,0x02,0x00,0x00,
};

/* Port of FUN_0045158b (147 B).  Seed the realm's eight workforce target ratios
 * (+0x130 run) to the AI's fixed defaults {0x21, 0x32, 0x11, 0x28, 0xf, 0xf,
 * 0xf, 0xf} before the workforce allocator (FUN_0044f6e7) redistributes its
 * population against them.  Called by every personality dispatcher. */
void engine_army_ai_seed_workforce_ratios(int key)
{
    uint8_t *r = ARMY_REC(key);
    I32(r + ARMY_WF_RATIO_0) = 0x21;
    I32(r + ARMY_WF_RATIO_1) = 0x32;
    I32(r + ARMY_WF_RATIO_2) = 0x11;
    I32(r + ARMY_WF_RATIO_6) = 0xf;
    I32(r + ARMY_WF_RATIO_5) = 0xf;
    I32(r + ARMY_WF_RATIO_4) = 0xf;
    I32(r + ARMY_WF_RATIO_7) = 0xf;
    I32(r + ARMY_WF_RATIO_3) = 0x28;
}

/* Share a newly-enabled slot claims, indexed by how many of the five ratios are
 * already active (DAT_004d6768 — only entries 0..5 are reachable, the table
 * tail {5,0} past that is never indexed because the five ratios cap the count at
 * 5 and the active==5 entry is 0). */
static const int32_t k_wf_policy_share[8] = { 100, 50, 33, 25, 20, 0, 5, 0 };

/* Port of FUN_004502ca (879 B).  See engine_army_ai.h for the overview. */
void engine_army_ai_rebalance_workforce(int key, int slot, int mode)
{
    uint8_t *r  = ARMY_REC(key);
    int32_t *wf = (int32_t *)(r + ARMY_WF_RATIO_0);   /* eight i32 slots at +0x130 */

    /* Only act on a real state change: enabling an inactive slot, or disabling
     * an active one.  (The de-Morgan form the binary emits.) */
    if (((wf[slot] == 0) || (mode != 1)) && ((wf[slot] != 0) || (mode != 0))) {
        int s6 = wf[6], s5 = wf[5], s4 = wf[4], s7 = wf[7], s3 = wf[3];
        unsigned active = (s6 != 0) + (s5 != 0) + (s4 != 0) + (s7 != 0) + (s3 != 0);

        if (mode == 1) {
            int share = k_wf_policy_share[active];
            int keep  = 100 - share;
            wf[6] = engine_math_percent_of(keep, s6);
            wf[5] = engine_math_percent_of(keep, s5);
            wf[4] = engine_math_percent_of(keep, s4);
            wf[7] = engine_math_percent_of(keep, s7);
            wf[3] = engine_math_percent_of(keep, s3);
            wf[slot] = share;
        } else { /* mode == 0 */
            int grow = 100 - wf[slot];
            wf[6] = engine_math_percent_div(s6, grow);
            wf[5] = engine_math_percent_div(s5, grow);
            wf[4] = engine_math_percent_div(s4, grow);
            wf[7] = engine_math_percent_div(s7, grow);
            wf[3] = engine_math_percent_div(s3, grow);
            wf[slot] = 0;
        }

        /* Re-normalise the five ratios to 100 by dumping the remainder onto the
         * currently-largest ratio (ties resolve to the lowest slot, 6 first). */
        int top = 6, topv = wf[6];
        for (int i = 3; i < 8; i++) {
            if (topv < wf[i]) { top = i; topv = wf[i]; }
        }
        wf[top] += ((((100 - wf[6]) - wf[5]) - wf[4]) - wf[7]) - wf[3];
    }
}

/* Port of FUN_00428c42 (365 B).  Advance the realm's "advancement" progress by a
 * 0..5 tier step sized by how `amount` (a gold spend) compares to the realm's
 * mouths-to-feed/10 (+0x24): each whole multiple up to 5 is one tier.  The step
 * is clamped so the tier byte +0x219 never exceeds 5, then folded into +0x219,
 * the approval byte +0xc (capped at 100), and the cumulative +0x194.  No-op when
 * `amount` <= 0.  Reached from the slot-4 ("training") arm of the buy/sell
 * executor. */
void engine_army_ai_advance_tier(int amount, int key)
{
    if (amount <= 0) return;

    uint8_t *r = ARMY_REC(key);
    int unit = I32(r + ARMY_FEED_COUNT) / 10;

    int step;
    if      (amount < unit * 5) {
        if      (amount < unit * 4) {
            if      (amount < unit * 3) {
                if      (amount < unit * 2) step = (amount < unit) ? 0 : 1;
                else                        step = 2;
            } else                          step = 3;
        } else                              step = 4;
    } else                                  step = 5;

    int headroom = 5 - r[ARMY_TIER_STEP];
    if (headroom < step) step = headroom;
    if (step < 0)        step = 0;

    r[ARMY_TIER_STEP]   += (uint8_t)step;
    r[ARMY_APPROVAL]    += (uint8_t)step;
    I32(r + ARMY_TIER_PROGRESS) += step;
    if ((int8_t)r[ARMY_APPROVAL] > 99) r[ARMY_APPROVAL] = 100;   /* retail 'c' = 99 */
}

/* Port of FUN_00450ccd (377 B).  Settle a realm's secondary-settlement build
 * demands against its owner's two resource pools: when the realm has a secondary
 * settlement (+0x1c3) and a non-independent owner, consume min(pool, demand)
 * from the owner's pool +0x130 (demand +0x1d4) and pool +0x128 (demand +0x1d0),
 * reducing both pool and demand by the consumed amount.  No-op for independent
 * realms or those without a secondary settlement.  Run in the buy/sell
 * executor's re-plan tail. */
void engine_army_ai_consume_build_demand(int key)
{
    uint8_t *r = ARMY_REC(key);
    if (r[ARMY_SETTLE_FLAG] == 0) return;
    int owner = r[ARMY_OWNER];
    if (owner == 0) return;

    uint8_t *s = STRAT_REC(owner);

    int pool_hi = I32(s + STRAT_POOL_130);
    if (pool_hi < I32(r + ARMY_BUILD_DRAW_HI)) {
        I32(s + STRAT_POOL_130)   -= pool_hi;
        I32(r + ARMY_BUILD_DRAW_HI) -= pool_hi;
    } else {
        I32(s + STRAT_POOL_130)   -= I32(r + ARMY_BUILD_DRAW_HI);
        I32(r + ARMY_BUILD_DRAW_HI) = 0;
    }

    int pool_lo = I32(s + STRAT_POOL_128);
    if (pool_lo < I32(r + ARMY_BUILD_DRAW_LO)) {
        I32(s + STRAT_POOL_128)   -= pool_lo;
        I32(r + ARMY_BUILD_DRAW_LO) -= pool_lo;
    } else {
        I32(s + STRAT_POOL_128)   -= I32(r + ARMY_BUILD_DRAW_LO);
        I32(r + ARMY_BUILD_DRAW_LO) = 0;
    }
}

/* Resolve a commodity `slot`'s stock field to the pointer the trade executor
 * adjusts: slots 1/2 are per-realm army-record stocks; slots 6..0xe are the
 * owning player's strategic-block pools.  Slot 4 ("training") and any unknown
 * slot have no field (NULL) — slot 4 is funded straight into the advancement
 * tier instead. */
static int32_t *commodity_field(int slot, int player, int realm)
{
    uint8_t *a = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);
    switch (slot) {
        case 1:    return (int32_t *)(a + ARMY_PROVISION_RAW);
        case 2:    return (int32_t *)(a + ARMY_STOCK_A);
        case 6:    return (int32_t *)(s + 0x120);
        case 7:    return (int32_t *)(s + STRAT_POOL_128);
        case 8:    return (int32_t *)(s + STRAT_POOL_130);
        case 9:    return (int32_t *)(s + 0x14c);
        case 0xa:  return (int32_t *)(s + 0x150);
        case 0xb:  return (int32_t *)(s + 0x144);
        case 0xc:  return (int32_t *)(s + 0x140);
        case 0xd:  return (int32_t *)(s + 0x148);
        case 0xe:  return (int32_t *)(s + 0x154);
        default:   return NULL;
    }
}

/* Port of FUN_004284ce (1858 B).  The AI's buy/sell transaction executor: move
 * `amount` units of commodity `slot` for realm `realm` owned by `player`
 * (0 = independent), then re-plan the realm's whole economy.  `amount` < 0
 * SELLS |amount| units at unit price `sell_price`, crediting the proceeds;
 * `amount` > 0 BUYS `amount` units at unit price `buy_price`, debiting the cost.
 * `flag` (retail param_1) is accepted to mirror the call ABI but is unused.
 *
 *  - Sell: when the realm is owned, require the stock field to cover the sale
 *    (else abort).  Reduce the stock, add price×qty to the treasury — the
 *    owner's cash (+0x118) plus its two revenue ledgers (+0x10c/+0x110), or the
 *    independent realm's own gold (+0x1f4).
 *  - Buy: when owned, require the treasury to cover cost = price×amount (else
 *    abort).  Add the stock (or, for slot 4, fund the advancement tier), debit
 *    the cost from cash (+0x118) into the two expenditure ledgers (+0x104/+0x108),
 *    or from the independent realm's gold.
 *
 * Then (only when amount != 0): resync the owner's cashflow total, settle the
 * realm's build demands, reduce a footprint when a slot-2 buy reshaped it, grade
 * its cells, and run the provision→plan→workforce→provision→plan re-plan chain
 * at the saved seasons.  No-op when amount == 0.  The downstream re-plan callees
 * each guard their own (separately-bound) state. */
void engine_army_ai_execute_trade(int flag, int amount, int slot,
                                  int buy_price, int sell_price,
                                  int player, int realm)
{
    (void)flag;
    if (amount == 0) return;

    uint8_t *a = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);
    int32_t *fld = commodity_field(slot, player, realm);

    if (amount < 0) {
        int qty  = -amount;
        int gold = sell_price * qty;
        if (player != 0 && fld != NULL && *fld < qty) return;   /* can't cover sale */

        if (fld != NULL) *fld += amount;                        /* amount < 0 → reduce */
        if (player == 0) {
            I32(a + ARMY_TREASURY) += gold;
        } else {
            I32(s + STRAT_CASH)        += gold;
            I32(s + STRAT_SELL_LEDGER_B) += gold;
            I32(s + STRAT_SELL_LEDGER_A) += gold;
        }
    } else {
        int cost = buy_price * amount;
        if (player != 0 && I32(s + STRAT_CASH) < cost) return;   /* can't afford */

        if (slot == 4)        engine_army_ai_advance_tier(cost, realm);
        else if (fld != NULL) *fld += amount;

        if (player == 0) {
            I32(a + ARMY_TREASURY) -= cost;
        } else {
            I32(s + STRAT_CASH)        -= cost;
            I32(s + STRAT_BUY_LEDGER_B) += cost;
            I32(s + STRAT_BUY_LEDGER_A) += cost;
        }
    }

    engine_ranking_recompute_player_total(player);
    engine_army_ai_consume_build_demand(realm);
    if (slot == 2 && amount >= 0) engine_territory_recensus_reduce(realm);
    engine_territory_grade_feature_cells(realm);
    engine_army_economy_provision(realm, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_provision(realm, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Per-unit BUY price for commodity `slot` in `realm`: the base price marked up
 * by the realm's territory-420 price-adjustment percent (floor 1), or just the
 * base for slot 4 (the markup is 0).  *base_out receives the un-marked-up base
 * (the SELL price).  Shared by the three order helpers. */
static int trade_unit_price(int realm, int slot, int *base_out)
{
    uint8_t *a = ARMY_REC(realm);
    int base = COMMODITY_PRICE(slot);
    int adjpct = g_per_territory_420[a[ARMY_RACE_IDX] * 0x1a4 + TERR420_PRICE_ADJ];
    int adj = engine_math_percent_of(base, adjpct);
    if (adj < 1) adj = 1;
    if (slot == 4) adj = 0;
    *base_out = base;
    return adj + base;
}

/* Port of FUN_004a4a3f (211 B).  Issue a SELL order: dump `amount` units of
 * `slot` at the base price through the trade executor (flag 1, signed negative).
 * No affordability gate — selling is always allowed.  No-op unless the realm's
 * trade gate (+0x1a4) is set. */
void engine_army_ai_sell_order(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int base;
    int unit = trade_unit_price(realm, slot, &base);
    engine_army_ai_execute_trade(1, -amount, slot, unit, base, a[ARMY_OWNER], realm);
}

/* Port of FUN_004a4b12 (298 B).  Issue a BUY order for `amount` units of `slot`,
 * but only when the buyer can afford the whole order up front: the independent
 * realm's gold (+0x1f4) for player 0, else the owner's cash (+0x118).  No-op
 * unless the trade gate is set. */
void engine_army_ai_buy_order(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int player = a[ARMY_OWNER];
    int base;
    int unit = trade_unit_price(realm, slot, &base);
    int cost = unit * amount;

    int afford = (player == 0)
        ? (cost <= I32(a + ARMY_TREASURY))
        : (cost <= I32(STRAT_REC(player) + STRAT_CASH));
    if (afford)
        engine_army_ai_execute_trade(0, amount, slot, unit, base, player, realm);
}

/* Port of FUN_004a4c41 (311 B).  BUY as much of `amount` units of `slot` as the
 * buyer can afford, halving the order until it fits the treasury (or hits 0).
 * No-op unless the trade gate is set. */
void engine_army_ai_buy_order_scaled(int realm, int amount, int slot)
{
    uint8_t *a = ARMY_REC(realm);
    if (a[ARMY_TRADE_GATE] == 0) return;

    int player = a[ARMY_OWNER];
    int base;
    int unit = trade_unit_price(realm, slot, &base);
    int treasury = (player == 0)
        ? I32(a + ARMY_TREASURY)
        : I32(STRAT_REC(player) + STRAT_CASH);

    while (amount > 0) {
        if (unit * amount <= treasury) {
            engine_army_ai_execute_trade(0, amount, slot, unit, base, player, realm);
            return;
        }
        amount /= 2;
    }
}

/* Port of FUN_004a4694 (238 B).  Re-balance the realm's high-tier cell footprint
 * against a target `high_target`: when the graded density (+0x25c) is low (< 0xb)
 * and the realm is high-cell-rich but stock-poor, shed one high cell; when the
 * density is high (> 10) and the realm is below its high-cell target, add one.
 * Either way the band is rewritten via demolish_band/convert_or_demolish_band,
 * then the realm is re-censused and re-graded. */
void engine_army_ai_balance_high_cells(int realm, int high_target)
{
    uint8_t *a = ARMY_REC(realm);
    int high = a[ARMY_CENSUS_HIGH];

    if (I32(a + ARMY_DENSITY_VALUE) < 0xb) {
        if (high > 2 && I32(a + ARMY_STOCK_A) < 10) {
            engine_territory_demolish_band(realm, 0x13);
            engine_territory_convert_or_demolish_band(realm, 0x13, high - 1);
        }
    } else if (I32(a + ARMY_DENSITY_VALUE) > 10 && high < high_target) {
        engine_territory_demolish_band(realm, 0x13);
        engine_territory_convert_or_demolish_band(realm, 0x13, high + 1);
    }
    engine_territory_census_army_terrain(realm);
    engine_territory_grade_feature_cells(realm);
}

/* Port of FUN_004a4782 (701 B).  Classify the realm's economic strength into a
 * 0..5 band (+0x15e) from its appraisal value vs a cost basis (mouths-to-feed
 * +0x24, plus the two overhead terms when the global mode s_slot_0053f260 == 1),
 * then sweep the tax rate (+0x15f) to maximise the provision net (+0x15d):
 *   sweep == 0   → forward sweep 0..100
 *   sweep == 100 → backward sweep 100..0
 *   else         → fix the rate at `sweep`
 * Each trial calls the economy provisioner at the map season; the best rate is
 * left in +0x15f with a final provision run there. */
void engine_army_ai_tax_sweep(int realm, int sweep)
{
    uint8_t *a = ARMY_REC(realm);
    int appraisal = engine_army_economy_appraisal_value(realm);
    int stock_a   = engine_army_economy_stock_a_value(realm);

    int basis = I32(a + ARMY_FEED_COUNT);
    if (s_slot_0053f260 == 1)
        basis += I32(a + ARMY_OVERHEAD_LO) + I32(a + ARMY_OVERHEAD_HI);

    if      (appraisal < basis / 2)  a[ARMY_STRENGTH_BAND] = 0;
    else if (appraisal < basis)      a[ARMY_STRENGTH_BAND] = 1;
    else if (appraisal < basis * 2)  a[ARMY_STRENGTH_BAND] = 2;
    else if (basis * 3 < stock_a)    a[ARMY_STRENGTH_BAND] = 5;
    else if (basis * 2 < stock_a)    a[ARMY_STRENGTH_BAND] = 4;
    else if (basis * 8 < appraisal)  a[ARMY_STRENGTH_BAND] = 5;
    else if (basis * 6 < appraisal)  a[ARMY_STRENGTH_BAND] = 4;
    else                             a[ARMY_STRENGTH_BAND] = 3;

    int8_t best_net = 0;
    int best_rate = sweep;
    if (sweep == 0) {
        for (int rate = 0; rate < 0x65; rate++) {
            a[ARMY_TAX_RATE] = (uint8_t)rate;
            engine_army_economy_provision(realm, (int)s_slot_0057c92c);
            if (best_net < (int8_t)a[ARMY_PROVISION_NET]) {
                best_net = (int8_t)a[ARMY_PROVISION_NET];
                best_rate = rate;
            }
        }
        a[ARMY_TAX_RATE] = (uint8_t)best_rate;
    } else if (sweep == 100) {
        for (int rate = 100; rate >= 0; rate--) {
            a[ARMY_TAX_RATE] = (uint8_t)rate;
            engine_army_economy_provision(realm, (int)s_slot_0057c92c);
            if (best_net < (int8_t)a[ARMY_PROVISION_NET]) {
                best_net = (int8_t)a[ARMY_PROVISION_NET];
                best_rate = rate;
            }
            a[ARMY_TAX_RATE] = (uint8_t)best_rate;   /* retail resets in-loop (dead but faithful) */
        }
    } else {
        a[ARMY_TAX_RATE] = (uint8_t)sweep;
    }
    engine_army_economy_provision(realm, (int)s_slot_0057c92c);
}

/* The cash-funded commodity the resource orchestrator buys, selected by the
 * realm's +0x290 byte. */
static int resource_selector(int sel)
{
    switch (sel) {
        case 0:  return 0xc;
        case 1:  return 0xb;
        case 2:  return 0xd;
        case 3:  return 9;
        case 4:  return 10;
        default: return 0xe;
    }
}

/* Port of FUN_0049e39b (994 B).  Reconcile a realm's three pooled commodities
 * (8/6/7) toward the owning player's personality targets — buy the player's
 * explicit request when one is queued (+0x70/+0x74/+0x7c), else sell any surplus
 * over the personality target — then fund one discretionary buy of the
 * +0x290-selected commodity when the player's cash clears the personality's
 * threshold.  No-op unless the realm's trade gate (+0x1a4) is set and the
 * player's trade lock (+0x158) is clear. */
void engine_army_ai_reconcile_resources(int player, int realm)
{
    uint8_t *a = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);
    if (a[ARMY_TRADE_GATE] == 0 || s[STRAT_TRADE_LOCK] != 0) return;

    const uint8_t *row = personality_row(s[STRAT_PERSONALITY]);

    /* commodity 8 (pool +0x130) */
    if (I32(s + STRAT_BUY_REQ_8) == 0) {
        int target = pers_i32(row, AI_PERS_TARGET_8);
        if (target < I32(s + STRAT_POOL_130))
            engine_army_ai_sell_order(realm, I32(s + STRAT_POOL_130) - target, 8);
    } else {
        engine_army_ai_buy_order_scaled(realm, I32(s + STRAT_BUY_REQ_8), 8);
    }

    /* commodity 6 (pool +0x120) */
    if (I32(s + STRAT_BUY_REQ_6) == 0) {
        int target = pers_i32(row, AI_PERS_TARGET_6);
        if (target < I32(s + STRAT_POOL_120))
            engine_army_ai_sell_order(realm, I32(s + STRAT_POOL_120) - target, 6);
    } else {
        engine_army_ai_buy_order_scaled(realm, I32(s + STRAT_BUY_REQ_6), 6);
    }

    /* commodity 7 (pool +0x128) */
    if (I32(s + STRAT_BUY_REQ_7) == 0) {
        int target = pers_i32(row, AI_PERS_TARGET_7);
        if (target < I32(s + STRAT_POOL_128))
            engine_army_ai_sell_order(realm, I32(s + STRAT_POOL_128) - target, 7);
    } else {
        engine_army_ai_buy_order_scaled(realm, I32(s + STRAT_BUY_REQ_7), 7);
    }

    /* one discretionary cash-funded buy */
    int slot = resource_selector(a[ARMY_RESOURCE_SEL]);
    if (pers_i32(row, AI_PERS_CASH_THRESH) < I32(s + STRAT_CASH))
        engine_army_ai_buy_order_scaled(realm, pers_i32(row, AI_PERS_BUY_AMOUNT), slot);
}

/* Total developed feature cells (census TYPE1 + MID + HIGH). */
static int feature_cell_total(const uint8_t *a)
{
    return a[ARMY_CENSUS_TYPE1] + a[ARMY_CENSUS_MID] + a[ARMY_CENSUS_HIGH];
}

/* Port of FUN_004a4052 (657 B).  Personality "0" dispatch (balanced/builder):
 * top up commodity-1 stock toward 600/100, tag strategy 0x32, seed ratios + run
 * the workforce, sweep tax (forward when stock-poor, backward otherwise), shed a
 * high band, and — in autumn (season 4) — clear and rebuild the mid band by a
 * yield-bonus-scaled count, expand developments when stock-rich, then re-plan. */
void engine_army_ai_dispatch_balanced(int realm)
{
    uint8_t *a = ARMY_REC(realm);
    engine_army_ai_reconcile_resources(a[ARMY_OWNER], realm);

    if (I32(a + ARMY_PROVISION_RAW) < 600) engine_army_ai_buy_order(realm, 400, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 600) engine_army_ai_buy_order(realm, 200, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 600) engine_army_ai_buy_order(realm, 100, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 0x32, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 0x19, 1);

    int total = feature_cell_total(a);
    a[ARMY_STRATEGY_TAG] = 0x32;
    engine_army_ai_seed_workforce_ratios(realm);
    engine_territory_allocate_realm_workforce(realm);

    if (I32(a + ARMY_STOCK_A) < 0x65) engine_army_ai_tax_sweep(realm, 0);
    else                              engine_army_ai_tax_sweep(realm, 100);

    engine_territory_demolish_band(realm, 0x13);
    if (s_slot_0057c934 == 4) {
        engine_territory_demolish_band(realm, 2);
        int n;
        if      ((int8_t)a[ARMY_YIELD_BONUS] < -0x14) n = total / 2 - 1;
        else if ((int8_t)a[ARMY_YIELD_BONUS] < -0x32) n = total / 2 - 2;
        else                                          n = total / 2;
        if (s_slot_0053f25c == 0) n = total - 1;
        engine_territory_convert_or_demolish_band(realm, 2, n);
    }
    if (I32(a + ARMY_STOCK_A) > 10) {
        engine_territory_convert_or_demolish_band(realm, 0x13, 1);
        engine_territory_census_army_terrain(realm);
        engine_territory_grade_feature_cells(realm);
    }
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Port of FUN_004a42e3 (300 B).  Personality "1" dispatch (compact/defensive):
 * a small commodity-2 top-up and a single big commodity-1 buy, tag strategy
 * 0x14, seed ratios + workforce, forward tax sweep, demolish the mid band, then
 * re-balance the high band toward (cells − 1) and re-plan. */
void engine_army_ai_dispatch_compact(int realm)
{
    uint8_t *a = ARMY_REC(realm);
    engine_army_ai_reconcile_resources(a[ARMY_OWNER], realm);

    if (I32(a + ARMY_STOCK_A) < 0x29)      engine_army_ai_buy_order(realm, 10, 2);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 400, 1);

    int total = feature_cell_total(a);
    a[ARMY_STRATEGY_TAG] = 0x14;
    engine_army_ai_seed_workforce_ratios(realm);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_ai_tax_sweep(realm, 0);
    engine_territory_demolish_band(realm, 2);
    engine_army_ai_balance_high_cells(realm, total - 1);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Port of FUN_004a440f (645 B).  Personality "9" dispatch (expansionist):
 * top up commodity-1 toward 300 across a wide buy ladder, tag strategy 0x28,
 * seed ratios + workforce, forward tax sweep, the same autumn mid-band rebuild
 * (scaled by total/3), re-balance the high band toward total/3, then re-plan. */
void engine_army_ai_dispatch_expansionist(int realm)
{
    uint8_t *a = ARMY_REC(realm);
    engine_army_ai_reconcile_resources(a[ARMY_OWNER], realm);

    if (I32(a + ARMY_STOCK_A) < 0xb)       engine_army_ai_buy_order(realm, 10, 2);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 800, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 400, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 200, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 100, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 0x32, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 300) engine_army_ai_buy_order(realm, 0x19, 1);

    int total = feature_cell_total(a);
    a[ARMY_STRATEGY_TAG] = 0x28;
    engine_army_ai_seed_workforce_ratios(realm);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_ai_tax_sweep(realm, 0);

    if (s_slot_0057c934 == 4) {
        engine_territory_demolish_band(realm, 2);
        int n;
        if      ((int8_t)a[ARMY_YIELD_BONUS] < -0x14) n = total / 3 - 1;
        else if ((int8_t)a[ARMY_YIELD_BONUS] < -0x32) n = total / 3 - 2;
        else                                          n = total / 3;
        if (s_slot_0053f25c == 0) n = total / 2;
        engine_territory_convert_or_demolish_band(realm, 2, n);
    }
    engine_army_ai_balance_high_cells(realm, total / 3);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
}

/* Port of FUN_0049dd01 (583 B).  Run the per-realm AI for every realm owned by
 * `player`: develop empty cells toward a population-scaled target (more cells as
 * +0x24 grows), latch the personality dispatch code (+0x1fe) from the tuning
 * table, run the matching dispatcher, then re-allocate the workforce. */
void engine_army_ai_run_player(int player)
{
    int count = (int)s_slot_0056d5dc;
    for (int realm = 1; realm <= count; realm++) {
        uint8_t *a = ARMY_REC(realm);
        if (a[ARMY_OWNER] != player) continue;

        int cells = feature_cell_total(a);
        int pop = I32(a + ARMY_FEED_COUNT);
        if      (cells == 0)                       engine_territory_develop_empty_cells(realm, 1);
        else if (cells < 3 && pop > 200)           engine_territory_develop_empty_cells(realm, 1);
        else if (cells < 5 && pop > 400)           engine_territory_develop_empty_cells(realm, 1);
        else if (cells < 7 && pop > 600)           engine_territory_develop_empty_cells(realm, 1);
        else if (cells < 9 && pop > 1000)          engine_territory_develop_empty_cells(realm, 1);
        else if (pop > 0x4b0)                       engine_territory_develop_empty_cells(realm, 2);

        uint8_t code = personality_row(STRAT_REC(player)[STRAT_PERSONALITY])[AI_PERS_DISPATCH];
        a[ARMY_DISPATCH_CODE] = code;
        if      (code == 0) engine_army_ai_dispatch_balanced(realm);
        else if (code == 1) engine_army_ai_dispatch_compact(realm);
        else if (code == 9) engine_army_ai_dispatch_expansionist(realm);

        engine_territory_allocate_realm_workforce(realm);
    }
}

/* Port of FUN_0049a990 (126 B).  The turn driver's first call: run the per-realm
 * AI for every player slot 1..5 still in the game (strategic +0x4 set, +0x5
 * clear). */
void engine_army_ai_run_turn(void)
{
    for (int p = 1; p < 6; p++) {
        uint8_t *s = STRAT_REC(p);
        if (s[STRAT_ACTIVE] != 0 && s[STRAT_ELIMINATED] == 0)
            engine_army_ai_run_player(p);
    }
}

/* Port of FUN_0049d638 (1737 B).  Phase-1 AI init, run once per player slot by
 * the strategic per-tick FSM (FUN_0049a581, dispatch step 3; also called with
 * `player` == 0 for independent realms).  Two passes:
 *
 *   1. Ration ladder — for every realm owned by `player`, map its approval byte
 *      (+0xc, signed) to a ration/tax setting (+0xb9) through a threshold
 *      ladder.  Player 0 (independent realms) uses a fixed 8-step ladder; an AI
 *      player picks one of three ladders by the +4 selector int of its
 *      personality row (retail DAT_004d8a5c-based read).
 *
 *   2. Difficulty cash/resource grant — gated on an active, non-eliminated
 *      player whose AI level (+0x29) is non-zero.  Credits the treasury (+0x118)
 *      a personality×difficulty grant from the DC230 (level < 3) or DC1E0
 *      (level >= 3) table, then for AI levels 1..4 tops up each owned realm's
 *      population (+0x24 and its +0x30 mirror), commodity stock (+0x250) and
 *      provision raw (+0x224) by difficulty-scaled steps — level >= 5 takes the
 *      cash only (the per-realm multipliers stay 0). */
void engine_army_ai_set_rations_and_grant(int player)
{
    int count = (int)s_slot_0056d5dc;
    int diff  = (int)s_slot_0053f23c;

    /* Pass 1 — per-realm ration ladder. */
    for (int realm = 1; realm <= count; realm++) {
        uint8_t *a = ARMY_REC(realm);
        if (a[ARMY_OWNER] != player) continue;
        int ap = (int8_t)a[ARMY_APPROVAL];

        if (player == 0) {
            if      (ap < 20) a[ARMY_RATION] = 0;
            else if (ap < 40) a[ARMY_RATION] = 1;
            else if (ap < 50) a[ARMY_RATION] = 2;
            else if (ap < 60) a[ARMY_RATION] = 3;
            else if (ap < 70) a[ARMY_RATION] = 4;
            else if (ap < 80) a[ARMY_RATION] = 6;
            else if (ap < 90) a[ARMY_RATION] = 8;
            else              a[ARMY_RATION] = 12;
            continue;
        }

        int sel = pers_i32(personality_row(STRAT_REC(player)[STRAT_PERSONALITY]), 4);
        if (sel == 0) {
            if      (ap < 30) a[ARMY_RATION] = 0;
            else if (ap < 50) a[ARMY_RATION] = 2;
            else if (ap < 65) a[ARMY_RATION] = 4;
            else if (ap < 80) a[ARMY_RATION] = 10;
            else              a[ARMY_RATION] = 15;
        } else if (sel == 1) {
            if      (ap < 30) a[ARMY_RATION] = 0;
            else if (ap < 50) a[ARMY_RATION] = 1;
            else if (ap < 65) a[ARMY_RATION] = 3;
            else if (ap < 80) a[ARMY_RATION] = 7;
            else              a[ARMY_RATION] = 12;
        } else if (sel == 2) {
            if      (ap < 60) a[ARMY_RATION] = 0;
            else if (ap < 70) a[ARMY_RATION] = 1;
            else if (ap < 80) a[ARMY_RATION] = 2;
            else if (ap < 90) a[ARMY_RATION] = 3;
            else if (ap < 95) a[ARMY_RATION] = 8;
            else              a[ARMY_RATION] = 10;
        }
    }

    /* Pass 2 — difficulty grant.  Only for an active, non-eliminated AI player
     * with a level set (player 0 and humans fall out here). */
    uint8_t *s = STRAT_REC(player);
    int level = s[STRAT_AI_LEVEL];
    if (s[STRAT_ACTIVE] == 0 || s[STRAT_ELIMINATED] != 0 || level == 0)
        return;

    int pers = s[STRAT_PERSONALITY];
    const uint8_t *grant = &g_ai_cash_grant[(level < 3) ? AI_GRANT_DC230 : AI_GRANT_DC1E0];
    I32(s + STRAT_CASH) += *(const int32_t *)(grant + diff * 4 + pers * 0x10);

    int pop_step = 0, stock_step = 0, prov_step = 0;
    if (level < 3) {
        pop_step   = diff * 0x14;
        stock_step = diff * 5;
        prov_step  = diff * 0x28;
    } else if (level < 5) {
        pop_step   = diff * 10;
        stock_step = diff * 2;
        prov_step  = diff * 0x14;
    }

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *a = ARMY_REC(realm);
        if (a[ARMY_OWNER] != player) continue;
        if (I32(a + ARMY_FEED_COUNT) > 0x14) {
            I32(a + ARMY_FEED_COUNT)    += pop_step;
            I32(a + ARMY_POP_SUPPORTED) += pop_step;
        }
        if (I32(a + ARMY_STOCK_A) > 10)
            I32(a + ARMY_STOCK_A) += stock_step;
        if (I32(a + ARMY_PROVISION_RAW) > 0x32)
            I32(a + ARMY_PROVISION_RAW) += prov_step;
    }
}

/* Census-bucket sum (type-1 + mid + high) — the cell-footprint proxy the
 * independent-realm dispatchers use in place of feature_cell_total. */
static int census_sum(const uint8_t *a)
{
    return (int)a[ARMY_CENSUS_TYPE1] + (int)a[ARMY_CENSUS_MID] + (int)a[ARMY_CENSUS_HIGH];
}

/* Port of FUN_004a3c67 (620 B).  Dispatch-code-0 manager for an INDEPENDENT
 * realm (run by engine_army_ai_run_independent, owner 0).  The balanced-style
 * variant: there is no market reconcile — a provision shortfall is met by
 * crediting the realm's own gold pool (+0x1f4) and a self-funded buy ladder —
 * and the season-4 cell reshape sizes off the census sum rather than the live
 * cell total.  Otherwise mirrors engine_army_ai_dispatch_balanced. */
void engine_army_ai_dispatch_independent_balanced(int realm)
{
    uint8_t *a = ARMY_REC(realm);

    if (I32(a + ARMY_PROVISION_RAW) < 100) I32(a + ARMY_TREASURY) += 100;
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 400, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 200, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 100, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 0x32, 1);

    int sum = census_sum(a);
    a[ARMY_STRATEGY_TAG] = 0;
    engine_army_ai_seed_workforce_ratios(realm);
    engine_territory_allocate_realm_workforce(realm);

    if (I32(a + ARMY_STOCK_A) < 0x65) engine_army_ai_tax_sweep(realm, 0);
    else                              engine_army_ai_tax_sweep(realm, 100);

    engine_territory_demolish_band(realm, 0x13);
    if (s_slot_0057c934 == 4) {
        engine_territory_demolish_band(realm, 2);
        int n;
        if      ((int8_t)a[ARMY_YIELD_BONUS] < -0x14) n = sum / 2 - 1;
        else if ((int8_t)a[ARMY_YIELD_BONUS] < -0x32) n = sum / 2 - 2;
        else                                          n = sum / 2;
        if (s_slot_0053f25c == 0) n = sum - 3;
        engine_territory_convert_or_demolish_band(realm, 2, n);
    }
    if (I32(a + ARMY_STOCK_A) > 10) {
        engine_territory_convert_or_demolish_band(realm, 0x13, 1);
        engine_territory_census_army_terrain(realm);
        engine_territory_grade_feature_cells(realm);
    }
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Port of FUN_004a3ed3 (383 B).  Dispatch-code-1 manager for an INDEPENDENT
 * realm — the compact-style variant: top up commodity stock and provisions via
 * self-funded buys, then reshape toward the existing census footprint
 * (balance_high_cells target = census sum − 1).  No market reconcile. */
void engine_army_ai_dispatch_independent_compact(int realm)
{
    uint8_t *a = ARMY_REC(realm);

    if (I32(a + ARMY_STOCK_A) < 0xb)       engine_army_ai_buy_order(realm, 10, 2);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 400, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 200, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 100, 1);
    if (I32(a + ARMY_PROVISION_RAW) < 100) engine_army_ai_buy_order(realm, 0x32, 1);

    int sum = census_sum(a);
    a[ARMY_STRATEGY_TAG] = 0;
    engine_army_ai_seed_workforce_ratios(realm);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_ai_tax_sweep(realm, 0);
    engine_territory_demolish_band(realm, 2);
    engine_army_ai_balance_high_cells(realm, sum - 1);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(realm);
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
}

/* Port of FUN_0049dfc6 (505 B).  The per-realm AI driver for INDEPENDENT realms
 * (always called with player 0; the sibling of engine_army_ai_run_player for
 * owned realms).  For each realm owned by `player`: clear the +0x1b0 review
 * flag, develop empty cells toward a census-scaled target (the same
 * population-gated ladder as run_player, sized off the census sum), run the
 * realm's dispatch code (+0x1fe → 0/1 independent manager), then re-allocate the
 * workforce. */
void engine_army_ai_run_independent(int player)
{
    int count = (int)s_slot_0056d5dc;
    for (int realm = 1; realm <= count; realm++) {
        uint8_t *a = ARMY_REC(realm);
        if (a[ARMY_OWNER] != player) continue;

        a[ARMY_REVIEW_FLAG] = 0;
        int sum = census_sum(a);
        int pop = I32(a + ARMY_FEED_COUNT);
        if      (sum == 0)                 engine_territory_develop_empty_cells(realm, 1);
        else if (sum < 3 && pop > 200)     engine_territory_develop_empty_cells(realm, 1);
        else if (sum < 5 && pop > 400)     engine_territory_develop_empty_cells(realm, 1);
        else if (sum < 7 && pop > 600)     engine_territory_develop_empty_cells(realm, 1);
        else if (sum < 9 && pop > 1000)    engine_territory_develop_empty_cells(realm, 1);
        else if (pop > 0x4b0)              engine_territory_develop_empty_cells(realm, 2);

        uint8_t code = a[ARMY_DISPATCH_CODE];
        if      (code == 0) engine_army_ai_dispatch_independent_balanced(realm);
        else if (code == 1) engine_army_ai_dispatch_independent_compact(realm);

        engine_territory_allocate_realm_workforce(realm);
    }
}

/* Port of FUN_0049d5e0.  Return the key of the first realm (1..count) owned by
 * `owner`, or 0 if the player owns none.  A generic scan over g_per_army_768's
 * owner byte (+0x005) used by the brain steps to test a player's holdings. */
int engine_army_ai_first_owned_realm(int owner)
{
    int count = (int)s_slot_0056d5dc;
    for (int realm = 1; realm <= count; realm++) {
        if (ARMY_REC(realm)[ARMY_OWNER] == (uint8_t)owner)
            return realm;
    }
    return 0;
}

/* Port of FUN_0049e1bf — AI brain step 4 (seed explicit buy requests).  Clears
 * the player's four buy-request ints (+0x70 run), then — for an AI above level
 * 1, or whose first owned realm is prosperous (approval +0xc > 14 AND morale
 * +0xb > 19) — tops the two staple commodities to a floor when their pool runs
 * short (+0x120 < 0x32 → +0x74 = 0x32; +0x130 < 100 → +0x70 = 100) and adds
 * every developed, keep-bearing owned realm's build demand (+0x1d4 → +0x70,
 * +0x1d0 → +0x7c) to the requests.  The strategic resource orchestrator drains
 * these +0x70/+0x74/+0x7c requests when it next reconciles the player's market. */
void engine_army_ai_seed_buy_requests(int player)
{
    uint8_t *s = STRAT_REC(player);

    for (int i = 0; i < 4; i++)
        I32(s + STRAT_BUY_REQ_8 + i * 4) = 0;

    int gate = s[STRAT_AI_LEVEL] > 1;
    if (!gate) {
        int r = engine_army_ai_first_owned_realm(player);
        if (r > 0) {
            uint8_t *a = ARMY_REC(r);
            gate = (int8_t)a[ARMY_APPROVAL] > 0x0e && (int8_t)a[ARMY_MORALE] > 0x13;
        }
    }
    if (!gate)
        return;

    if (I32(s + STRAT_POOL_120) < 0x32)
        I32(s + STRAT_BUY_REQ_6) = 0x32;
    if (I32(s + STRAT_POOL_130) < 100)
        I32(s + STRAT_BUY_REQ_8) = 100;

    int count = (int)s_slot_0056d5dc;
    for (int r = 1; r <= count; r++) {
        uint8_t *a = ARMY_REC(r);
        if (a[ARMY_OWNER] == (uint8_t)player &&
            a[ARMY_KEEP_LEVEL] != 0 && a[ARMY_SETTLE_FLAG] != 0) {
            I32(s + STRAT_BUY_REQ_8) += I32(a + ARMY_BUILD_DRAW_HI);
            I32(s + STRAT_BUY_REQ_7) += I32(a + ARMY_BUILD_DRAW_LO);
        }
    }
}

/* Build the per-player has-data filter table the event-queue producer expects:
 * has_data[i] = g_player_strategic_352[i].+5 for slots 0..5, slot 6 inert (the
 * producer reads only index `target`, which is 1..5 here).  Mirrors retail
 * (&DAT_0057bf05)[i * 0x160]. */
static void build_has_data(uint8_t has_data[7])
{
    for (int i = 0; i < 6; i++)
        has_data[i] = STRAT_REC(i)[STRAT_ELIMINATED];
    has_data[6] = 0;
}

/* Post one diplomacy proposal (retail FUN_00472bc5(player, audience, code,
 * variant, 1, 0, 0, 0)) and advance the mood-cycle (+0x159, wrapping 0..3).
 * The variant selector is (mood-cycle − 4) + personality·4, read from the
 * CURRENT counter before it advances. */
static void propose_notify(engine_event_queue_state_t *evq, int player,
                           int audience, int code, uint8_t flag5, uint8_t flag6,
                           uint8_t *s)
{
    uint8_t has_data[7];
    build_has_data(has_data);

    int variant = ((int)s[STRAT_MOOD_CYCLE] - 4) + (int)s[STRAT_PERSONALITY] * 4;
    engine_event_queue_post(evq, player, audience, code, variant, flag5, flag6, 0, 0,
                            has_data, (int)s_slot_0057c8cc);

    s[STRAT_MOOD_CYCLE] += 1;
    if (s[STRAT_MOOD_CYCLE] > 3)
        s[STRAT_MOOD_CYCLE] = 0;
}

/* Port of FUN_004a13a6 — AI brain step 0xd (diplomacy proposals).  Runs only
 * for a #1-or-unranked player (+0x2b < 2).  Alternates, via the +0xea phase
 * flag, between two proposal modes once the per-tick +0xe9 counter reaches 8:
 *
 *   phase 0 (alliance): once the diplomacy gauge (+0x60) tops 0x28, broadcast a
 *     0xc1 (alliance) proposal to every OTHER in-game human slot (+4 != 0 &&
 *     +5 != 0), then flip the phase to 1.
 *   phase 1 (war): once the gauge tops 0x1c, send a 0xc0 (war) proposal to the
 *     last-ranked (weakest) player s_slot_00522d90 — provided that player is a
 *     human (+5 != 0) and is not already this AI's lord (+0x81) — then flip the
 *     phase back to 0.
 *
 * The proposals surface as UI notifications, so they route through the ported
 * event-queue producer; the queue state is threaded in (the engine.c file-
 * statics it would otherwise read are not linked into the host TU). */
void engine_army_ai_propose_diplomacy(int player, engine_event_queue_state_t *evq)
{
    int target = (int)s_slot_00522d90;          /* retail latches DAT_00522d90 up front */
    uint8_t *s = STRAT_REC(player);

    if (s[STRAT_RANK] >= 2)
        return;

    if (s[STRAT_PROPOSE_PHASE] == 0) {
        /* Phase 0 — broadcast alliance proposals. */
        if (s[STRAT_DIPLO_GAUGE] <= 0x27)
            return;
        s[STRAT_PROPOSE_TICK] += 1;
        if (s[STRAT_PROPOSE_TICK] < 8)
            return;

        for (int other = 1; other < 6; other++) {
            if (other == player)
                continue;
            uint8_t *o = STRAT_REC(other);
            if (o[STRAT_ACTIVE] == 0 || o[STRAT_ELIMINATED] == 0)
                continue;
            propose_notify(evq, player, other, 0xc1, 1, 0, s);
        }

        s[STRAT_PROPOSE_TICK]  = 0;
        s[STRAT_PROPOSE_PHASE] = 1;
    } else {
        /* Phase 1 — targeted war proposal against the weakest player. */
        if (s[STRAT_DIPLO_GAUGE] <= 0x1b)
            return;
        if (STRAT_REC(target)[STRAT_ELIMINATED] == 0)
            return;
        if (s[STRAT_LORD] == target)
            return;
        s[STRAT_PROPOSE_TICK] += 1;
        if (s[STRAT_PROPOSE_TICK] < 8)
            return;

        s[STRAT_PROPOSE_TICK]  = 0;
        s[STRAT_PROPOSE_PHASE] = 0;
        propose_notify(evq, player, target, 0xc0, 1, 0, s);
    }
}

/* Port of FUN_004a0c1d — AI brain step 2 (per-turn diplomacy decision).  Three
 * passes over `player`'s strategic record:
 *
 *  1. Warm relations: the relation toward every AI-controlled slot (+0x84,
 *     stride 0x10) drifts +1 per turn toward the +30 ceiling.
 *  2. Lord upkeep: if `player` has a lord (+0x81), a lord that has dropped out
 *     (+0x04 == 0) has its treaty cleared outright; otherwise betrayal pressure
 *     (+0x86) accumulates from a soured relation (< -10 → +10), a thinning field
 *     (s_slot_00554004 < 3 → +25), and the lord being the leader
 *     (s_slot_00553d24); once the pressure tops the personality threshold (+0x10)
 *     the AI betrays — clears the treaty, takes the relation hit, posts 0xb5.
 *  3. Ally seeking: a lordless, non-leader (rank != 1) player past year 1268
 *     (s_slot_00553edc > 0x4f4) picks the best ally candidate into +0x80; once
 *     its personality cooldown (+0x14) elapses it bonds with an AI candidate
 *     directly (engine_diplomacy_form_alliance) or posts a 0xb4 proposal to a
 *     lordless human candidate, latching the +0x1c pending gate.
 *
 * Notifications route through the event-queue producer; the queue state, the
 * has-data filter and the local-player slot are threaded in. */
void engine_army_ai_run_diplomacy(int player, engine_event_queue_state_t *evq)
{
    uint8_t *s = STRAT_REC(player);

    /* Pass 1 — warm relations toward AI players, capped at +30. */
    for (int i = 1; i < 6; i++) {
        if (STRAT_REC(i)[STRAT_ELIMINATED] == 0) {
            int rel = STRAT_REL + i * 0x10;
            s[rel] += 1;
            if ((int8_t)s[rel] > 0x1e)
                s[rel] = 0x1e;
        }
    }

    /* Pass 2 — lord upkeep / betrayal. */
    if (s[STRAT_LORD] != 0) {
        int betray_thr =
            pers_i32(personality_row(s[STRAT_PERSONALITY]), AI_PERS_BETRAY_THR);
        int lord = s[STRAT_LORD];

        if (STRAT_REC(lord)[STRAT_ACTIVE] == 0) {
            engine_diplomacy_clear_treaty(player, lord);
            return;
        }

        int pres = STRAT_CONTACT + lord * 0x10;
        if ((int8_t)s[STRAT_REL + lord * 0x10] < -10)
            s[pres] += 0x0a;
        if ((int)s_slot_00554004 < 3)
            s[pres] += 0x19;
        if (s_slot_00553d24 == (uint32_t)lord) {
            uint8_t gauge = STRAT_REC(lord)[STRAT_DIPLO_GAUGE];
            if (gauge < 0x15) {
                /* Retail nests two further comparisons here, both unreachable
                 * when gauge < 0x15 (the < 0x22 test is always true, the > 0x32
                 * test always false) — preserved verbatim as dead arms so the
                 * structure matches the binary; only the >= 0x15 → +1 path and
                 * this empty < 0x15 path actually fire. */
                if (gauge < 0x22) {
                    if (gauge > 0x32)
                        s[pres] += 4;
                } else {
                    s[pres] += 2;
                }
            } else {
                s[pres] += 1;
            }
        }

        if (betray_thr < (int)s[pres]) {
            engine_diplomacy_clear_treaty(player, lord);
            engine_diplomacy_battle_adjust(player, lord, 5, evq);
            propose_notify(evq, player, lord, 0xb5, 1, 0, s);
        }
    }

    /* Pass 3 — seek a new alliance. */
    if (s[STRAT_LORD] == 0 && (int)s_slot_00553edc > 0x4f4 && s[STRAT_RANK] != 1) {
        int cand = engine_diplomacy_best_ally_candidate(player);
        s[STRAT_ALLY_CAND] = (uint8_t)cand;
        if (s[STRAT_ALLY_CAND] != 0) {
            int period =
                pers_i32(personality_row(s[STRAT_PERSONALITY]), AI_PERS_ALLY_PERIOD);
            s[STRAT_ALLY_COOLDOWN] += 1;
            if (period <= (int)(int8_t)s[STRAT_ALLY_COOLDOWN]) {
                s[STRAT_ALLY_COOLDOWN] = 0;
                int c = s[STRAT_ALLY_CAND];
                if (STRAT_REC(c)[STRAT_ELIMINATED] == 0) {
                    engine_diplomacy_form_alliance(player, c);
                } else if (STRAT_REC(c)[STRAT_LORD] == 0) {
                    s[STRAT_PROPOSAL_GATE] = 1;
                    propose_notify(evq, player, c, 0xb4, 0xb, 0, s);
                }
            }
        }
    }
}

/* Port of FUN_0049f96c — AI brain step 8.  A no-op in the shipped build (the
 * substate exists in the dispatch ladder but its body returns immediately). */
void engine_army_ai_step_noop(void)
{
}

/* Build-order cost table (retail DAT_004d8990, six 8-byte entries — the next
 * const table begins at 0x4d89c0).  Indexed by a realm's current build tag
 * (ARMY_BUILD_TAG, 0..5); [tag][0] is the cost charged against the player's
 * +0x130 pool, [tag][1] the cost against the +0x120 pool. */
static const int32_t g_ai_build_cost[6][2] = {
    {  6, 10 },   /* tag 0 */
    {  4,  4 },   /* tag 1 */
    {  3, 10 },   /* tag 2 */
    {  6,  3 },   /* tag 3 */
    { 13,  0 },   /* tag 4 */
    {  4, 18 },   /* tag 5 */
};

/* Personality-row byte offset of the build tag selected by rotation counter
 * 0..9 (retail's DAT_004d8aa8/aac/ab0/ab4/ab8/abc columns, which sit at +0x50
 * .. +0x64 inside each 0xf0-byte personality row).  Counters 0..3 and 4..7 both
 * map to the first four columns; 8/9 add the last two. */
static const int g_ai_build_col_off[10] = {
    0x50, 0x54, 0x58, 0x5c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64,
};

/* Port of FUN_0049ed13 — flag whether `player` can afford `realm`'s current
 * build order.  Clears the realm's affordability flag, looks up the two pool
 * costs of its current build tag, runs the slot-2 resource planner
 * (engine_army_economy_plan_resource for slot 2), then re-sets the flag iff the
 * player's +0x130 and +0x120 pools both cover the respective costs. */
void engine_army_ai_mark_build_affordable(int player, int realm)
{
    uint8_t *r = ARMY_REC(realm);
    uint8_t *s = STRAT_REC(player);

    r[ARMY_BUILD_AFFORD] = 0;

    int tag = r[ARMY_BUILD_TAG];
    int cost_130 = g_ai_build_cost[tag][0];
    int cost_120 = g_ai_build_cost[tag][1];

    engine_army_economy_plan_resource(realm, 2, 7, 0xf, 4);

    if (cost_130 <= I32(s + STRAT_POOL_130) && cost_120 <= I32(s + STRAT_POOL_120))
        r[ARMY_BUILD_AFFORD] = 1;
}

/* Port of FUN_0049e77d — AI brain step 0xc (per-turn build-order planning).
 * Three passes over `player`'s owned realms:
 *
 *  1. Rotate build orders: each owned realm is stamped with the build tag from
 *     the player's personality row column selected by the per-player rotation
 *     counter (+0x6c, cycling 0..9); the counter advances and wraps at 10.  Each
 *     realm's affordability flag is then refreshed (mark_build_affordable).
 *  2. Enable job-bucket gates: for each of the four stride-0x18 settlement
 *     buckets, clear the workforce enable gate, then re-light it when the bucket
 *     descriptor is present (+0x295 != 0) and unlocked (+0x296 == 0) — subject,
 *     when a secondary settlement exists (+0x1c3), to a per-bucket filter that
 *     drops buckets 1 and 2, gates bucket 0 on build-draw-hi (+0x1d4 > 0) and
 *     bucket 3 on build-draw-lo (+0x1d0 > 0).  Mark the realm's plan settled.
 *  3. Run the workforce allocator then the per-realm economy orchestrator for
 *     each owned realm (the now-enabled gates steer both). */
void engine_army_ai_run_build_orders(int player)
{
    int count = (int)s_slot_0056d5dc;

    /* Pass 1 — rotate each owned realm's build tag, refresh affordability. */
    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)
            continue;

        uint8_t *s = STRAT_REC(player);
        int rot = I32(s + STRAT_BUILD_ROTATE);
        /* Retail's switch only assigns for rotation codes 0..9 (else the tag is
         * left unchanged); the counter is kept in that range by the wrap below. */
        if ((unsigned)rot < 10) {
            const uint8_t *row =
                &g_ai_personality[(s[STRAT_PERSONALITY] - 1) * AI_PERS_STRIDE];
            r[ARMY_BUILD_TAG] = row[g_ai_build_col_off[rot]];
        }

        I32(s + STRAT_BUILD_ROTATE) = rot + 1;
        if (I32(s + STRAT_BUILD_ROTATE) >= 10)
            I32(s + STRAT_BUILD_ROTATE) = 0;

        engine_army_ai_mark_build_affordable(player, realm);
    }

    /* Pass 2 — enable this player's realm job-bucket gates. */
    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)
            continue;

        for (int slot = 0; slot < 4; slot++) {
            uint8_t *g = r + slot * ARMY_BUILDGATE_STRIDE;
            g[ARMY_BUILDGATE_ENABLE] = 0;
            if ((int8_t)g[ARMY_BUILDGATE_PRESENT] != 0 &&
                (int8_t)g[ARMY_BUILDGATE_LOCK] == 0 &&
                (r[ARMY_SETTLE_FLAG] == 0 ||
                 (slot != 1 && slot != 2 &&
                  (slot != 0 || I32(r + ARMY_BUILD_DRAW_HI) > 0) &&
                  (slot != 3 || I32(r + ARMY_BUILD_DRAW_LO) > 0)))) {
                g[ARMY_BUILDGATE_ENABLE] = 1;
            }
        }
        r[ARMY_BUILD_PLAN_FLAG] = 1;
    }

    /* Pass 3 — allocate workforce + run the economy orchestrator per realm. */
    for (int realm = 1; realm <= count; realm++) {
        if (ARMY_REC(realm)[ARMY_OWNER] != player)
            continue;
        engine_territory_allocate_realm_workforce(realm);
        engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);
    }
}

/* Port of FUN_004a0aaa — score `player`'s owned realms to pick its muster
 * source.  Each owned realm gets a base score (provision +0x24 ÷ 500, doubled)
 * nudged ±1 by its approval (+0xc: < 0x28 → −1, > 0x50 → +1).  The base then
 * forks into two scores by comparing the realm's stockpile value
 * (engine_army_economy_stockpile_value) against its provision: a thin realm
 * (stockpile < provision) is a poor muster source (rich −2 / weak +2); a flush
 * realm (provision×4 < stockpile) is the best (rich +1 / weak −2).  The realm
 * with the greatest "rich" score lands in +0xe5 — the muster realm brain step
 * 0xa (and the spawn/seize paths) raise the AI's force from; the greatest "weak"
 * score lands in +0xe6 (no shipped reader).  Ties resolve to the higher index.
 *
 * NB: the Ghidra decompile transposes the two score variables — the binary
 * (r2-verified) is what is transcribed here: stockpile-below-provision favours
 * the +0xe6 score, not +0xe5. */
void engine_army_ai_pick_muster_realms(int player)
{
    int best_rich = 0, rich_realm = 0;     /* → +0xe5 */
    int best_weak = 0, weak_realm = 0;     /* → +0xe6 */
    int count = (int)s_slot_0056d5dc;

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)
            continue;

        int stock = engine_army_economy_stockpile_value(realm);
        int feed  = I32(r + ARMY_FEED_COUNT);

        int base = (feed / 500) * 2;
        if ((int8_t)r[ARMY_APPROVAL] < 0x28)
            base -= 1;
        else if ((int8_t)r[ARMY_APPROVAL] > 0x50)
            base += 1;

        int rich = base, weak = base;
        if (stock < feed) {
            rich -= 2;
            weak += 2;
        } else if (feed * 4 < stock) {
            rich += 1;
            weak -= 2;
        }

        if (best_rich <= rich) { best_rich = rich; rich_realm = realm; }
        if (best_weak <= weak) { best_weak = weak; weak_realm = realm; }
    }

    STRAT_REC(player)[STRAT_MUSTER_REALM] = (uint8_t)rich_realm;
    STRAT_REC(player)[STRAT_WEAK_REALM]   = (uint8_t)weak_realm;
}

/* Port of FUN_004a0649 — pick `player`'s best adjacent enemy attack target,
 * scored from `from_realm`'s capital.  A candidate must be a realm not owned by
 * `player` that `player` would engage (engine_diplomacy_should_engage) and that
 * borders one of `player`'s realms (engine_realm_player_owns_neighbor).  Its
 * score starts at the king-move distance from `from_realm`'s capital to the
 * target's (retail FUN_00404f4c — engine_path_dist_chebyshev, which also leaves
 * |dx|/|dy| in DAT_00591560/0058fea0 as retail does), then: +0x28 if the target is a defended
 * keep (+0x1c0 set and a pending garrison +0x1bc); −0x14 if its stock (+0x250) is
 * ≥ 0xc9 else −0xa if > 100; −0xa if its provision (+0x224) is ≥ 0x3e9 else −5 if
 * > 500.  The lowest-scoring candidate below the 0x80 ceiling wins (richer,
 * closer, weaker → lower).  Returns the target realm, or 0. */
int engine_army_ai_pick_attack_target(int player, int from_realm)
{
    int best_score = 0x80;
    int best_realm = 0;
    int count = (int)s_slot_0056d5dc;

    int home_col = ARMY_REC(from_realm)[ARMY_CAP_COL];
    int home_row = ARMY_REC(from_realm)[ARMY_CAP_ROW];

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        int owner = r[ARMY_OWNER];
        if (owner == player)
            continue;
        if (!engine_diplomacy_should_engage(player, owner))
            continue;
        if (!engine_realm_player_owns_neighbor((unsigned)player, realm))
            continue;

        int score = engine_path_dist_chebyshev(home_col, home_row,        /* FUN_00404f4c */
                                               r[ARMY_CAP_COL], r[ARMY_CAP_ROW]);

        if (r[ARMY_KEEP_LEVEL] != 0 && I32(r + ARMY_PENDING) != 0)
            score += 0x28;

        int stock = I32(r + ARMY_STOCK_A);
        if (stock < 0xc9) {
            if (stock > 100)
                score -= 0xa;
        } else {
            score -= 0x14;
        }

        int prov = I32(r + ARMY_PROVISION_RAW);
        if (prov < 0x3e9) {
            if (prov > 500)
                score -= 5;
        } else {
            score -= 0xa;
        }

        if (score < best_score) {
            best_score = score;
            best_realm = realm;
        }
    }
    return best_realm;
}

/* Port of FUN_004a03f2 — select `player`'s attack target into +0x4b (the realm
 * step 0xa marches its mustered force at) and return whether one was found.  The
 * richer sibling of engine_army_ai_pick_attack_target, scored the same way from
 * `from_realm`'s capital but with a three-tier provision bonus, and with two
 * candidate-filter modes:
 *
 *   forced_owner != 0 — consider only realms owned by `forced_owner` (the
 *     should-engage / border checks are skipped).
 *   forced_owner == 0 — consider realms `player` would engage
 *     (engine_diplomacy_should_engage) that border a player realm
 *     (engine_realm_player_owns_neighbor); when `gate_owner` is set, additionally
 *     restrict to realms owned by the stored owner gate +0x48.
 *
 * Score = chebyshev distance (FUN_00404f4c, engine_path_dist_chebyshev) + 0x28 for a defended keep
 * (+0x1c0 set & pending +0x1bc), − stock bonus (+0x250: ≥0xc9 −0x14, >100 −0xa),
 * − provision bonus (+0x224: ≥0x9c5 −0x14, ≥0x4b1 −0xa, >500 −5).  Lowest score
 * under the 0x80 ceiling wins; the winner is stored in +0x4b. */
int engine_army_ai_select_target(int player, int from_realm, int gate_owner,
                                 int forced_owner)
{
    int best_score = 0x80;
    int best_realm = 0;
    int count = (int)s_slot_0056d5dc;

    int home_col = ARMY_REC(from_realm)[ARMY_CAP_COL];
    int home_row = ARMY_REC(from_realm)[ARMY_CAP_ROW];

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        int owner = r[ARMY_OWNER];

        int eligible;
        if (forced_owner == 0) {
            eligible =
                (gate_owner == 0 ||
                 STRAT_REC(player)[STRAT_TARGET_OWNER] == owner) &&
                engine_diplomacy_should_engage(player, owner) &&
                engine_realm_player_owns_neighbor((unsigned)player, realm);
        } else {
            eligible = (owner == forced_owner);
        }
        if (!eligible)
            continue;

        int score = engine_path_dist_chebyshev(home_col, home_row,        /* FUN_00404f4c */
                                               r[ARMY_CAP_COL], r[ARMY_CAP_ROW]);

        if (r[ARMY_KEEP_LEVEL] != 0 && I32(r + ARMY_PENDING) != 0)
            score += 0x28;

        int stock = I32(r + ARMY_STOCK_A);
        if (stock < 0xc9) {
            if (stock > 100)
                score -= 0xa;
        } else {
            score -= 0x14;
        }

        int prov = I32(r + ARMY_PROVISION_RAW);
        if (prov < 0x9c5) {
            if (prov < 0x4b1) {
                if (prov > 500)
                    score -= 5;
            } else {
                score -= 0xa;
            }
        } else {
            score -= 0x14;
        }

        if (score < best_score) {
            best_score = score;
            best_realm = realm;
        }
    }

    STRAT_REC(player)[STRAT_ATTACK_TARGET] = (uint8_t)best_realm;
    return STRAT_REC(player)[STRAT_ATTACK_TARGET] != 0;
}

/* Port of FUN_004a0309 — return the player `player` is most hostile toward.  When
 * a war target is pinned (+0xeb != 0) that player is returned outright; otherwise
 * scan the other in-game slots (+0x04 active) for the one `player`'s relation
 * toward (+0x84 + slot*0x10, signed) is most negative, requiring it below −10.
 * Returns 0 when no slot is hated that strongly.  Brain step 0xa feeds the result
 * to engine_army_ai_select_target as the forced target owner. */
int engine_army_ai_most_hostile_player(int player)
{
    uint8_t *s = STRAT_REC(player);
    if (s[STRAT_PINNED_ENEMY] != 0)
        return s[STRAT_PINNED_ENEMY];

    int worst = 0;
    int worst_rel = -10;
    for (int p = 1; p < 6; p++) {
        if (p == player)
            continue;
        if (STRAT_REC(p)[STRAT_ACTIVE] == 0)
            continue;
        int rel = (int8_t)s[STRAT_REL + p * 0x10];
        if (rel < worst_rel) {
            worst_rel = rel;
            worst = p;
        }
    }
    return worst;
}

/* New-occupant record (g_per_territory_420, stride 0x1a4) fields brain step 0xa
 * stamps onto the force it raises. */
#define OCC_REC(i)    (&g_per_territory_420[(i) * 0x1a4])
#define OCC_DEST_COL  0x016   /* DAT_0052f0c6 — march destination column */
#define OCC_DEST_ROW  0x017   /* DAT_0052f0c7 — march destination row */
#define OCC_ORDER     0x01a   /* DAT_0052f0ca — order code (7 = march to attack) */
#define OCC_PLACED    0x150   /* DAT_0052f200 — placed flag (cleared so it deploys) */
#define OCC_TARGET    0x151   /* DAT_0052f201 — target realm the march heads for */
#define AI_PERS_ATTACK_CD 0x74 /* personality-row byte: post-attack cooldown reload */

/* Port of FUN_004a0015 — AI brain step 0xa (launch an offensive).  Gated by the
 * per-player attack cooldown (+0x15a): while it is non-zero it just ticks down.
 * Otherwise, find the most-hostile enemy (most_hostile_player); if one exists,
 * select a target realm of that enemy near the AI's muster realm (+0xe5) via
 * select_target (forced owner = the enemy, gate on).  When a target is found and
 * the muster realm is prosperous (provision +0x24 ≥ 400 and approval +0xc ≥ 25),
 * raise a field force there (raise_region_unit), stamp it with a march order
 * (+0x1a = 7), point it at the chosen deploy cell (find_deploy_cell → +0x16/+0x17)
 * with the target realm recorded (+0x151) and its placed flag cleared (+0x150),
 * and reload the attack cooldown from the personality row (+0x74). */
void engine_army_ai_launch_attack(int player)
{
    uint8_t *s = STRAT_REC(player);

    if (s[STRAT_ATTACK_COOLDOWN] != 0) {
        s[STRAT_ATTACK_COOLDOWN] -= 1;
        return;
    }

    int enemy = engine_army_ai_most_hostile_player(player);
    if (enemy <= 0)
        return;

    int muster = s[STRAT_MUSTER_REALM];
    if (!engine_army_ai_select_target(player, muster, 1, enemy))
        return;
    if (I32(ARMY_REC(muster) + ARMY_FEED_COUNT) < 400)
        return;
    if ((int8_t)ARMY_REC(muster)[ARMY_APPROVAL] < 0x19)
        return;

    int occ = engine_combat_raise_region_unit(muster);
    if (occ == 0)
        return;

    int target = s[STRAT_ATTACK_TARGET];
    uint8_t *c = OCC_REC(occ);
    c[OCC_ORDER] = 7;

    int col = 0, row = 0;
    engine_combat_find_deploy_cell(occ, target, &col, &row);
    c[OCC_DEST_COL] = (uint8_t)col;
    c[OCC_DEST_ROW] = (uint8_t)row;
    c[OCC_PLACED]   = 0;
    c[OCC_TARGET]   = (uint8_t)target;

    s[STRAT_ATTACK_COOLDOWN] =
        g_ai_personality[(s[STRAT_PERSONALITY] - 1) * AI_PERS_STRIDE + AI_PERS_ATTACK_CD];
}

/* ── Brain step 1: diplomacy-inbox processor (FUN_004a277d + 7 handlers) ────
 *
 * Step 1 runs first for every realm (state 0 init → state 1).  It drains the
 * realm's five-slot message inbox (s_slot_0053f0f0, save entry 146: 6 rows ×
 * 0x28, each row five 8-byte entries — +0 sender, +1 type, +2 byte param,
 * +4 i32 value), dispatching each message to its type handler, then clears the
 * inbox and resets the per-pair +0x90 scratch byte.  The handlers adjust the
 * receiver's per-pair relation (+0x84, clamped to ±30) and post a UI
 * notification (event codes 0xab..0xc5) through the event-queue producer; types
 * 2/3/4 also forge or break treaties, types 5/6 (tribute) read the cached 7-bit
 * RNG byte (DAT_0058fd60, threaded in) for a probabilistic yield. */

/* Retail's relation clamp, applied after every +0x84 adjust (verbatim in each
 * handler): values >= +31 pin to +30, values <= -31 pin to -30. */
static void adjust_rel(int p1, int p2, int delta)
{
    uint8_t *r = &STRAT_REC(p1)[STRAT_REL + p2 * 0x10];
    *r = (uint8_t)(*r + delta);
    int8_t v = (int8_t)*r;
    if (v < 0x1f) {
        if (v < -0x1e)
            *r = (uint8_t)0xe2;       /* -30 */
    } else {
        *r = 0x1e;                    /* +30 */
    }
}

/* Port of FUN_004a1b29 — transfer `amount` cash p2→p1 (a no-op when amount is
 * 0, as the tribute handlers call it), bump the pair gift counter, sour the
 * relation by 4 (low clamp only), and stash the giver byte. */
static void ai_diplomacy_gift(int p1, int p2, uint8_t giver, int amount)
{
    uint8_t *s2 = STRAT_REC(p2);
    if (amount <= I32(s2 + STRAT_CASH)) {
        uint8_t *s1 = STRAT_REC(p1);
        I32(s2 + STRAT_CASH) -= amount;
        I32(s1 + STRAT_CASH) += amount;
        s1[STRAT_PAIR_GIFTCNT + p2 * 0x10] += 1;
        uint8_t *r = &s1[STRAT_REL + p2 * 0x10];
        *r = (uint8_t)(*r - 4);
        if ((int8_t)*r < -0x1e)
            *r = (uint8_t)0xe2;
        s1[STRAT_GIFT_LAST] = giver;
    }
}

/* type 0 — FUN_004a29b2: evaluate a resource offer of `value` against the
 * running best-offer (+0x8c) plus a personality band; warm or cool relations
 * by tier. */
static void ai_inbox_eval_offer(engine_event_queue_state_t *evq, int p1, int p2, int value)
{
    uint8_t *s = STRAT_REC(p1);
    if (STRAT_REC(p2)[STRAT_ELIMINATED] == 0)
        return;

    int32_t *maxoffer = (int32_t *)&s[STRAT_PAIR_MAXOFFER + p2 * 0x10];
    int band = pers_i32(personality_row(s[STRAT_PERSONALITY]), AI_PERS_OFFER_BAND);

    int delta, code;
    if (value < *maxoffer + band / 2)   { delta = -8; code = 0xad; }
    else if (value < *maxoffer + band)  { delta =  5; code = 0xac; }
    else                                { delta = 10; code = 0xab; }

    if (*maxoffer < value)
        *maxoffer = value;

    adjust_rel(p1, p2, delta);
    propose_notify(evq, p1, p2, code, 1, 0, s);
}

/* type 1 — FUN_004a2cd6: repeat-offer response keyed on the pair repeat
 * counter (+0x89): the first two are welcomed warmly, the third cools. */
static void ai_inbox_eval_repeat(engine_event_queue_state_t *evq, int p1, int p2)
{
    uint8_t *s = STRAT_REC(p1);
    if (STRAT_REC(p2)[STRAT_ELIMINATED] == 0)
        return;

    uint8_t cnt = s[STRAT_PAIR_CNT89 + p2 * 0x10];
    int delta, code;
    if (cnt < 3) {
        if (cnt < 2) { delta = 0xf; code = 0xae; }
        else         { delta = 8;   code = 0xaf; }
    } else           { delta = -4;  code = 0xb0; }

    adjust_rel(p1, p2, delta);
    propose_notify(evq, p1, p2, code, 1, 0, s);
}

/* type 2 — FUN_004a2f61: a treaty break that still notifies (code 0xb1);
 * relation drops 20. */
static void ai_inbox_break_notify(engine_event_queue_state_t *evq, int p1, int p2)
{
    uint8_t *s = STRAT_REC(p1);
    if (STRAT_REC(p2)[STRAT_ELIMINATED] == 0)
        return;

    engine_diplomacy_clear_treaty(p1, p2);       /* FUN_004a1a54 */
    adjust_rel(p1, p2, -0x14);
    propose_notify(evq, p1, p2, 0xb1, 1, 0, s);
}

/* type 3 — FUN_004a30f6: respond to an alliance proposal from p2.  Reject
 * (0xb2) / accept+forge (0xb3) / already-have-lord (0xc5) / already-allied
 * (0xc4), with a probabilistic accept in the marginal relation band. */
static void ai_inbox_alliance_response(engine_event_queue_state_t *evq, int p1, int p2,
                                       uint32_t rng_7bit_b)
{
    uint8_t *s  = STRAT_REC(p1);
    int8_t  rel = (int8_t)s[STRAT_REL + p2 * 0x10];

    int code;
    if (s[STRAT_PAIR_ALLIED + p2 * 0x10] != 0) {
        code = 0xc4;
    } else if ((int)s_slot_00554004 < 3) {
        code = 0xb2;
    } else if (s[STRAT_LORD] == 0) {                       /* p1 has no lord */
        if (STRAT_REC(p2)[STRAT_LORD] != 0) {              /* p2 already has one */
            code = 0xb2;
        } else if (rel < 0x0b) {
            if (rel < -10)                          code = 0xb2;
            else if ((int)rng_7bit_b < rel * 3 + 0x2d) code = 0xb3;
            else                                    code = 0xb2;
        } else {
            code = 0xb3;
        }
    } else {
        code = 0xc5;                                       /* p1 already has a lord */
    }

    if (s[STRAT_ALLY_COOLDOWN] != 0)
        s[STRAT_ALLY_COOLDOWN] -= 1;

    if (code == 0xb2) {
        adjust_rel(p1, p2, -2);
    } else if (code == 0xb3) {
        adjust_rel(p1, p2, 4);
        engine_diplomacy_form_alliance(p1, p2);            /* FUN_004a1774 */
    } else {
        adjust_rel(p1, p2, -1);
    }
    propose_notify(evq, p1, p2, code, 1, 0, s);
}

/* type 4 — FUN_004a3475: a silent treaty break (no notification); relation
 * drops 15. */
static void ai_inbox_break_silent(int p1, int p2)
{
    engine_diplomacy_clear_treaty(p1, p2);       /* FUN_004a1a54 */
    adjust_rel(p1, p2, -0xf);
}

/* types 5/6 — FUN_004a3551 / FUN_004a38db: a tribute demand from p2.  When p2
 * is p1's lord, yield (0xb7/0xba) if too poor or cold, pay a token gift
 * (0xb8/0xbb) on a favourable RNG roll, else stage the demand amount and refuse
 * (0xb9/0xbc).  The two differ only in event codes, the contact-counter bump,
 * and the RNG multiplier — kept as separate verbatim ports. */
static void ai_inbox_tribute(engine_event_queue_state_t *evq, int p1, int p2,
                             int param, uint32_t rng_7bit_b)
{
    uint8_t       *s   = STRAT_REC(p1);
    const uint8_t *row = personality_row(s[STRAT_PERSONALITY]);
    int8_t  rel  = (int8_t)s[STRAT_REL + p2 * 0x10];
    uint8_t flag6 = (uint8_t)param;

    int code;
    if (s[STRAT_LORD] == (uint8_t)p2) {
        if (I32(s + STRAT_AVG_TROOP) < pers_i32(row, AI_PERS_TRIBUTE_FLOOR)) code = 0xb7;
        else if (rel < 0x0a)                                                code = 0xb7;
        else if ((int)rng_7bit_b < rel * 4)                                 code = 0xb8;
        else                                                                code = 0xb9;
    } else {
        code = 0xb7;
    }

    if (code == 0xb7) {
        s[STRAT_CONTACT + p2 * 0x10] += 1;
        propose_notify(evq, p1, p2, 0xb7, 1, flag6, s);
    } else if (code == 0xb8) {
        propose_notify(evq, p1, p2, 0xb8, 1, flag6, s);
        ai_diplomacy_gift(p1, p2, flag6, 0);
    } else {
        s_slot_0055ce50 = (uint32_t)param;
        s_slot_00567958 = (uint32_t)(pers_i32(row, AI_PERS_GIFT_UNIT)
                                     * (int)s[STRAT_PAIR_GIFTCNT + p2 * 0x10]);
        propose_notify(evq, p1, p2, 0xb9, 10, flag6, s);
    }
}

static void ai_inbox_demand(engine_event_queue_state_t *evq, int p1, int p2,
                            int param, uint32_t rng_7bit_b)
{
    uint8_t       *s   = STRAT_REC(p1);
    const uint8_t *row = personality_row(s[STRAT_PERSONALITY]);
    int8_t  rel  = (int8_t)s[STRAT_REL + p2 * 0x10];
    uint8_t flag6 = (uint8_t)param;

    int code;
    if (s[STRAT_LORD] == (uint8_t)p2) {
        if (I32(s + STRAT_AVG_TROOP) < pers_i32(row, AI_PERS_TRIBUTE_FLOOR)) code = 0xba;
        else if (rel < 0x0a)                                                code = 0xba;
        else if ((int)rng_7bit_b < rel * 2)                                 code = 0xbb;
        else                                                                code = 0xbc;
    } else {
        code = 0xba;
    }

    if (code == 0xba) {
        s[STRAT_CONTACT + p2 * 0x10] += 2;
        propose_notify(evq, p1, p2, 0xba, 1, flag6, s);
    } else if (code == 0xbb) {
        propose_notify(evq, p1, p2, 0xbb, 1, flag6, s);
        ai_diplomacy_gift(p1, p2, flag6, 0);
    } else {
        s_slot_0055ce50 = (uint32_t)param;
        s_slot_00567958 = (uint32_t)(pers_i32(row, AI_PERS_GIFT_UNIT)
                                     * (int)s[STRAT_PAIR_GIFTCNT + p2 * 0x10]);
        propose_notify(evq, p1, p2, 0xbc, 10, flag6, s);
    }
}

/* Port of FUN_004a277d (565 B) — brain step 1.  Dispatch then drain `self`'s
 * five-slot diplomacy inbox, then reset the per-pair +0x90 scratch byte. */
static void engine_army_ai_step1_process_inbox(int self,
                                               engine_event_queue_state_t *evq,
                                               uint32_t rng_7bit_b)
{
    CALL_TRACE_ENTER(0x004a277d);

    /* Pass 1 — dispatch each queued message (stop at the first empty slot;
     * skip self-addressed entries without breaking). */
    for (int i = 0; i < 5; i++) {
        uint8_t *e = &s_slot_0053f0f0[self * 0x28 + i * 8];
        unsigned sender = e[0];
        if (sender == 0)
            break;
        if (sender != (unsigned)self) {
            int value = *(int32_t *)(e + 4);
            int param = e[2];
            switch ((int8_t)e[1]) {           /* message type */
            case 0: ai_inbox_eval_offer(evq, self, (int)sender, value);             break;
            case 1: ai_inbox_eval_repeat(evq, self, (int)sender);                   break;
            case 2: ai_inbox_break_notify(evq, self, (int)sender);                  break;
            case 3: ai_inbox_alliance_response(evq, self, (int)sender, rng_7bit_b); break;
            case 4: ai_inbox_break_silent(self, (int)sender);                       break;
            case 5: ai_inbox_tribute(evq, self, (int)sender, param, rng_7bit_b);    break;
            case 6: ai_inbox_demand(evq, self, (int)sender, param, rng_7bit_b);     break;
            default: break;
            }
        }
    }

    /* Pass 2 — clear self's inbox: zero +0/+1/+2 and the i32 at +4, leaving
     * byte +3 untouched (matches FUN_004a2584 / clear_grid_53f0f0). */
    for (int i = 0; i < 5; i++) {
        uint8_t *e = &s_slot_0053f0f0[self * 0x28 + i * 8];
        e[0] = 0; e[1] = 0; e[2] = 0;
        *(uint32_t *)(e + 4) = 0;
    }

    /* Pass 3 — reset the per-pair +0x90 scratch byte for all six counterparts. */
    for (int other = 0; other < 6; other++)
        STRAT_REC(self)[STRAT_PAIR_BYTE90 + other * 0x10] = 0;
}

/* ── Brain step 9: offensive-campaign planner (FUN_0049f977 + 6 callees) ────
 *
 * Step 9 is the AI's persistent army-campaign driver.  Each call either ticks a
 * patience counter toward the next campaign, or — once a target is committed —
 * raises/picks a force from the player's muster realm (+0xe5) and stamps it with
 * a march order toward a chosen deployment cell.  Unlike step 0xa (a one-shot
 * cooldown-gated raid) it carries a multi-turn campaign target in +0xe8 and
 * sizes the force against a personality "force budget".  It runs every turn in
 * headless SP, so the trace-studio oracle exercises it directly.
 *
 * The two finders below leave their chosen cell in this find-nearest scratch
 * (retail DAT_00569570 col / DAT_00569578 row — shared scalar globals); the
 * deployment assigner copies it into the army's destination fields immediately
 * after.  File-static because every consumer lives in this TU. */
/* The finders leave their result in retail's shared cell cursor DAT_00569570 /
 * DAT_00569578 — engine_map_query's g_cell_cursor_col/row. */

/* Per-player strategic-record fields step 9 adds (offsets into g_player_strategic_352). */
#define STRAT_CAMPAIGN_TICK   0x045  /* DAT_0057bf45 — patience counter toward the next campaign */
#define STRAT_CAMPAIGN_TARGET 0x0e8  /* DAT_0057bfe8 — committed multi-turn campaign target realm */
#define STRAT_FORCE_ACC       0x138  /* DAT_0057c038 — accumulated force credit (i32; gates a muster) */
#define STRAT_RESERVE_140     0x140  /* DAT_0057c040 — funding reserve A (i32; personality 3) */
#define STRAT_RESERVE_14C     0x14c  /* DAT_0057c04c — funding reserve B (i32; personality 4) */
#define STRAT_RESERVE_150     0x150  /* DAT_0057c050 — funding reserve C (i32; personality 4) */

/* Personality-row fields step 9 reads (offsets into a personality_row()).  The
 * +0x30 force budget shares its byte with AI_PERS_TRIBUTE_FLOOR (step 1). */
#define AI_PERS_CAMPAIGN_PERIOD 0x28 /* i32: ticks between campaigns (vs STRAT_CAMPAIGN_TICK) */
#define AI_PERS_FORCE_BUDGET    0x30 /* i32: force budget compared to the muster realm's feed count */
#define AI_PERS_MUSTER_PCT      0x40 /* i32: provision percent the campaign muster raises */
#define AI_PERS_FORCE_THRESH    0x68 /* i32: STRAT_FORCE_ACC floor below which a funded muster is skipped */

/* Per-realm record fields step 9 adds: the FUN_004a65a3 fallback anchor cell. */
#define ARMY_SETTLE_COL 0x074        /* DAT_0053fa24 — settled-cell fallback column */
#define ARMY_SETTLE_ROW 0x075        /* DAT_0053fa25 — settled-cell fallback row */

/* Occupant-record fields step 9 adds (the existing OCC_DEST_COL/ROW/ORDER/PLACED/
 * TARGET are defined alongside step 0xa above). */
#define OCC_COL      0x00a           /* DAT_0052f0ba — occupant's current overworld column (i8) */
#define OCC_ROW      0x00b           /* DAT_0052f0bb — occupant's current overworld row (i8) */
#define OCC_CAMPAIGN 0x19b           /* DAT_0052f24b — campaign target realm stamped on the order */

/* Overworld cell descriptor (g_overworld_map, 8 B/cell): +0 terrain, +1 flag
 * bits, +7 owning region.  The finders gate on two flag bits: 0x40 (any owned
 * cell, FUN_004a6735) and 0x80 (the retail region-event bit, paired with a
 * cropland-class terrain window 0x15..0x19, FUN_004a65a3). */
#define CELL_TERRAIN 0
#define CELL_FLAGS   1
#define CELL_REGION  7

/* Port of FUN_004a6735 — find the nearest cell owned by `region` carrying flag
 * bit 0x40, by Manhattan distance to occupant `occ`'s current seat (OCC +0xa/+0xb).
 * Leaves the chosen column/row in the find-nearest scratch; on no match falls
 * back to the region's capital cell (ARMY +0x6c/+0x6d).  The distance is the
 * |dx|+|dy| return of FUN_00404ead (engine_path_dist_manhattan — which also
 * leaves |dx|/|dy| in DAT_00591560/0058fea0, as retail does).  retail decodes
 * the winning byte offset with signed gymnastics that reduce to a plain
 * (ofs>>3)&0x3f / (ofs>>3)>>6 for the always-non-negative offset. */
int engine_army_ai_find_owned_cell(int occ, unsigned region)   /* retail's EAX: the row, or region << 8 */
{
    int ax = (int8_t)OCC_REC(occ)[OCC_COL];
    int ay = (int8_t)OCC_REC(occ)[OCC_ROW];

    int best_dist = 1000, best_ofs = 0, ofs = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, ofs += 8) {
            if ((uint8_t)g_overworld_map[ofs + CELL_REGION] != region)  continue;
            if ((g_overworld_map[ofs + CELL_FLAGS] & 0x40) == 0)        continue;
            int dist = engine_path_dist_manhattan(ax, ay, col, row);   /* FUN_00404ead */
            if (dist < best_dist) { best_dist = dist; best_ofs = ofs; }
        }
    }
    if (best_ofs == 0) {
        g_cell_cursor_col = (uint8_t)ARMY_REC(region)[ARMY_CAP_COL];
        g_cell_cursor_row = (uint8_t)ARMY_REC(region)[ARMY_CAP_ROW];
        return (int)(region << 8);
    }
    int idx = best_ofs >> 3;
    g_cell_cursor_col = idx & 0x3f;
    g_cell_cursor_row = idx >> 6;
    return idx >> 6;
}

/* Port of FUN_004a65a3 — like engine_army_ai_find_owned_cell but gated on flag
 * bit 0x80 AND a cropland-class terrain window (0x14 < terrain < 0x1a), with the
 * fallback anchor at ARMY +0x74/+0x75. */
int engine_army_ai_find_settled_cell(int occ, unsigned region)   /* retail's EAX: the row, or region << 8 */
{
    int ax = (int8_t)OCC_REC(occ)[OCC_COL];
    int ay = (int8_t)OCC_REC(occ)[OCC_ROW];

    int best_dist = 1000, best_ofs = 0, ofs = 0;
    for (int row = 0; row < 0x40; row++) {
        for (int col = 0; col < 0x40; col++, ofs += 8) {
            if ((uint8_t)g_overworld_map[ofs + CELL_REGION] != region)  continue;
            if ((g_overworld_map[ofs + CELL_FLAGS] & 0x80) == 0)        continue;
            int terr = (uint8_t)g_overworld_map[ofs + CELL_TERRAIN];
            if (terr <= 0x14 || terr >= 0x1a)                           continue;
            int dist = engine_path_dist_manhattan(ax, ay, col, row);   /* FUN_00404ead */
            if (dist < best_dist) { best_dist = dist; best_ofs = ofs; }
        }
    }
    if (best_ofs == 0) {
        g_cell_cursor_col = (uint8_t)ARMY_REC(region)[ARMY_SETTLE_COL];
        g_cell_cursor_row = (uint8_t)ARMY_REC(region)[ARMY_SETTLE_ROW];
        return (int)(region << 8);
    }
    int idx = best_ofs >> 3;
    g_cell_cursor_col = idx & 0x3f;
    g_cell_cursor_row = idx >> 6;
    return idx >> 6;
}

/* Port of FUN_0049fda5 — stamp occupant `occ` with the campaign march order for
 * `player`.  Order code 2 (advance) by default; 6 (a lord-directed move) when a
 * campaign target is active and that target realm is owned by the player's lord.
 * The placed flag is cleared so the move executor redeploys it, the attack
 * target (+0x4b) is recorded at +0x151, and the destination cell is chosen by
 * one of the two finders: the settled-cell finder when the muster realm holds a
 * keep with a pending garrison, otherwise the owned-cell finder. */
static void engine_army_ai_assign_deployment(int player, int occ)
{
    uint8_t *s = STRAT_REC(player);
    uint8_t  target = s[STRAT_ATTACK_TARGET];
    unsigned region = target;
    uint8_t *c = OCC_REC(occ);

    if (s[STRAT_CAMPAIGN_TARGET] == 0) {
        c[OCC_ORDER] = 2;
    } else {
        c[OCC_CAMPAIGN] = s[STRAT_CAMPAIGN_TARGET];
        c[OCC_ORDER] = (ARMY_REC(region)[ARMY_OWNER] == s[STRAT_LORD]) ? 6 : 2;
    }
    c[OCC_PLACED] = 0;
    c[OCC_TARGET] = target;

    if (ARMY_REC(region)[ARMY_KEEP_LEVEL] == 0 || I32(ARMY_REC(region) + ARMY_PENDING) == 0)
        engine_army_ai_find_owned_cell(occ, region);    /* FUN_004a6735 */
    else
        engine_army_ai_find_settled_cell(occ, region);  /* FUN_004a65a3 */
    c[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
    c[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
}

/* Port of FUN_0049ff71 — validate a committed campaign target `target` for
 * `player`.  Drops the target (returns 0) when the player has no lord, when the
 * target realm belongs to the player's lord and is undefended (overhead-lo == 0),
 * or when the target realm is the player's own; otherwise keeps it. */
static int engine_army_ai_validate_campaign_target(int target, int player)
{
    uint8_t *s = STRAT_REC(player);
    if (s[STRAT_LORD] == 0)
        target = 0;
    else if (s[STRAT_LORD] == ARMY_REC(target)[ARMY_OWNER]) {
        if (I32(ARMY_REC(target) + ARMY_OVERHEAD_LO) == 0)
            target = 0;
    } else if ((uint8_t)ARMY_REC(target)[ARMY_OWNER] == (uint8_t)player)
        target = 0;
    return target;
}

/* Port of FUN_004a01ea — set `player`'s target-owner gate (+0x48) for the
 * campaign target search.  A pinned enemy (+0xeb) wins outright; otherwise scan
 * the field leader(s) (rank < 2) and adopt one as the target owner, bailing if
 * the leader is the player itself, inactive, or below the diplomacy-gauge floor
 * (+0x60 < 40). */
static void engine_army_ai_pick_target_owner(int player)
{
    uint8_t *s = STRAT_REC(player);
    s[STRAT_TARGET_OWNER] = 0;
    if (s[STRAT_PINNED_ENEMY] != 0) {
        s[STRAT_TARGET_OWNER] = s[STRAT_PINNED_ENEMY];
        return;
    }
    for (int other = 1; other < 6; other++) {
        if ((uint8_t)STRAT_REC(other)[STRAT_RANK] < 2) {
            if (player == other)                                       return;
            if (STRAT_REC(other)[STRAT_ACTIVE] == 0)                   return;
            if ((uint8_t)STRAT_REC(other)[STRAT_DIPLO_GAUGE] < 0x28)   return;
            s[STRAT_TARGET_OWNER] = (uint8_t)other;
        }
    }
}

/* Port of FUN_0049fcc5 — top up `player`'s funding reserves for an easy-AI
 * campaign.  Only the two low difficulty levels (+0x29 < 2), before the late-era
 * cutoff (turn counter < 0x4f9), and only for personalities 3 and 4: personality
 * 4 banks +100 into reserves B and C, personality 3 banks +100 into reserve A.
 * Returns 1 when it funded, 0 otherwise. */
static int engine_army_ai_fund_campaign(int player)
{
    uint8_t *s = STRAT_REC(player);
    if ((uint8_t)s[STRAT_AI_LEVEL] >= 2)   return 0;
    if ((int)s_slot_00553edc >= 0x4f9)     return 0;
    if (s[STRAT_PERSONALITY] == 4) {
        I32(s + STRAT_RESERVE_14C) += 100;
        I32(s + STRAT_RESERVE_150) += 100;
        return 1;
    }
    if (s[STRAT_PERSONALITY] == 3) {
        I32(s + STRAT_RESERVE_140) += 100;
        return 1;
    }
    return 0;
}

/* Port of FUN_0049f977 — AI brain step 9 (offensive-campaign planner).  See the
 * block comment above.  Two arms select/commit a target and a force budget; a
 * third raises (muster) or picks (pick_best_unit) the force and hands it to
 * engine_army_ai_assign_deployment, clearing the campaign target on success. */
void engine_army_ai_run_campaign(int player)
{
    CALL_TRACE_ENTER(0x0049f977);

    uint8_t *s = STRAT_REC(player);
    const uint8_t *pr = personality_row(s[STRAT_PERSONALITY]);
    int budget = I32(pr + AI_PERS_FORCE_BUDGET);

    if (s[STRAT_CAMPAIGN_TARGET] == 0) {
        if (s[STRAT_PINNED_ENEMY] == 0) {
            /* No campaign and no pinned enemy: tick patience; bail until it fires. */
            s[STRAT_CAMPAIGN_TICK]++;
            if ((int)s[STRAT_CAMPAIGN_TICK] < I32(pr + AI_PERS_CAMPAIGN_PERIOD))
                return;
            s[STRAT_CAMPAIGN_TICK] = 0;
        } else {
            budget /= 2;                       /* pinned enemy: commit at half budget */
        }
    } else {
        /* Re-validate the committed target (it may have been conquered/allied). */
        s[STRAT_CAMPAIGN_TARGET] = (uint8_t)engine_army_ai_validate_campaign_target(
            (int8_t)s[STRAT_CAMPAIGN_TARGET], player);
    }

    unsigned muster = s[STRAT_MUSTER_REALM];
    if (s[STRAT_CAMPAIGN_TARGET] == 0) {
        /* Pick a fresh target: gated to the field leader first, then unrestricted. */
        engine_army_ai_pick_target_owner(player);
        if (!engine_army_ai_select_target(player, (int)muster, 1, 0) &&
            !engine_army_ai_select_target(player, (int)muster, 0, 0))
            return;                            /* no viable target */
    } else {
        s[STRAT_ATTACK_TARGET] = s[STRAT_CAMPAIGN_TARGET];
        budget /= 2;                           /* committed campaign: half budget */
    }

    int occ;
    if (budget < I32(ARMY_REC(muster) + ARMY_FEED_COUNT)) {
        /* Muster realm can bear the budget: raise a fresh force there. */
        if (engine_army_ai_fund_campaign(player) == 0 &&
            I32(s + STRAT_FORCE_ACC) < I32(pr + AI_PERS_FORCE_THRESH))
            return;                            /* unfunded and below the force floor */
        if (engine_combat_muster_defender((int)muster, I32(pr + AI_PERS_MUSTER_PCT), 100, 1) == 0)
            return;                            /* muster failed */
        occ = engine_territory_current_slot(); /* the freshly-mustered occupant */
    } else {
        /* Realm too lean: redirect the best existing unit toward the target. */
        occ = engine_combat_pick_best_unit(player, s[STRAT_ATTACK_TARGET], 200);
    }

    if (occ != 0) {
        engine_army_ai_assign_deployment(player, occ);
        s[STRAT_CAMPAIGN_TARGET] = 0;          /* campaign launched — clear the commit */
    }
}

/* ── Brain step 0xb: army-movement / deployment planner (FUN_004a5667 + 13 callees)
 *
 * Step 0xb walks every army (occupant record) the realm owns and, per the army's
 * current order code (+0x1a), recomputes its march destination, then runs the
 * cost-field pathfinder from the army's seat to that destination and copies the
 * resulting waypoint chain into the army record.  The order code is set by the
 * other brain steps (step 0xa = 7 attack-march, step 9 = 2 advance / 6 lord-move)
 * and self-rewritten by the handlers here (e.g. a finished siege downgrades to 2).
 *
 * Occupant-record fields step 0xb adds (g_per_territory_420, stride 0x1a4). */
#define OCC_OWNER        0x000  /* DAT_0052f0b0 — owning player (i8; 0 = empty slot) */
#define OCC_ACTIVE       0x008  /* DAT_0052f0b8 — live-army flag (1 = present) */
#define OCC_HOME_REGION  0x010  /* DAT_0052f0c0 — home region key (u8; == combat C_ARMYKEY) */
#define OCC_TARGET_REGION 0x011 /* DAT_0052f0c1 — secondary/march region key (u8) */
#define OCC_PATH_OK      0x14c  /* DAT_0052f1fc — set when a path to the destination was found */
#define OCC_PATH_FAIL    0x14e  /* DAT_0052f1fe — cleared before each path attempt */
#define OCC_HEAD         0x168  /* DAT_0052f218 — total head-count (i32; == combat C_VALUE) */
#define OCC_GOODS        0x16e  /* DAT_0052f21e — six i16 carried-goods (poured back on disband) */
#define OCC_IDLE         0x198  /* DAT_0052f248 — region-transfer-pending gate (0 = idle; C_F198) */
#define OCC_HELD         0x199  /* DAT_0052f249 — survives-battle / held-this-tick gate (C_F199) */

/* Per-realm record fields step 0xb adds (g_per_army_768, stride 0x300). */
#define ARMY_POP_BASE      0x038  /* DAT_0053f9e8 — population figure topped up on disband */
#define ARMY_ADJ_LIST      0x05c  /* DAT_0053fa0c — 16-entry region adjacency list */
#define ARMY_GARRISON_HEAD 0x124  /* DAT_0053fad4 — garrison head figure topped up on disband */
#define ARMY_SIEGE_BLOCK   0x1c2  /* DAT_0053fb72 — siege-block byte (must be clear to garrison) */
#define ARMY_SIEGE_MODE    0x1c3  /* DAT_0053fb73 — siege-mode byte (== combat A_F1C3; the same
                                   *   byte engine_army_ai's ARMY_SETTLE_FLAG names in the build
                                   *   context — retail overloads it) */

/* Per-player record fields step 0xb adds (g_player_strategic_352, stride 0x160). */
#define STRAT_WAGE_TOTAL 0x0fc  /* DAT_0057bffc — recomputed total unit wage on disband (i32) */
#define STRAT_RESERVE_144 0x144 /* DAT_0057c044 — funding reserve (i32) */
#define STRAT_RESERVE_148 0x148 /* DAT_0057c048 — funding reserve (i32) */
#define STRAT_RESERVE_154 0x154 /* DAT_0057c054 — funding reserve (i32) */

/* Find-nearest-army scratch: retail reuses DAT_00553f8c — the combat occupied-cell
 * register, engine_combat's g_combat_active_terr — as the scanner's loop var +
 * result index.  The uses are in disjoint sim phases, but the value the scan
 * leaves is live state, so the AI counts in the same global (quirk #226). */

/* Port of FUN_00467f2e — does region `region` border a region owned by someone
 * other than its own owner? (a frontier check; walks the +0x5c adjacency list). */
static int engine_army_ai_region_borders_enemy(int region)
{
    for (int i = 0; i < 16; i++) {
        unsigned nb = ARMY_REC(region)[ARMY_ADJ_LIST + i];
        if (nb == 0)
            break;
        if (ARMY_REC(nb)[ARMY_OWNER] != 0 &&
            ARMY_REC(region)[ARMY_OWNER] != ARMY_REC(nb)[ARMY_OWNER])
            return 1;
    }
    return 0;
}

/* Port of FUN_004673b8 — find the nearest live, idle, enemy-owned army to `occ`
 * by Chebyshev (king-move) distance, leaving its index in g_combat_active_terr and
 * returning the distance (1000 if none).  The distance is FUN_00404f4c, which
 * leaves |dx| / |dy| in its globals (quirks #226 / #235). */
static int engine_army_ai_nearest_enemy_army(int occ)
{
    int best = 1000, best_idx = 0;
    int ax = (int8_t)OCC_REC(occ)[OCC_COL], ay = (int8_t)OCC_REC(occ)[OCC_ROW];
    for (g_combat_active_terr = 1; g_combat_active_terr < 0x97; g_combat_active_terr++) {
        uint8_t *o = OCC_REC(g_combat_active_terr);
        if (o[OCC_OWNER] == 0)                                continue;
        if (OCC_REC(occ)[OCC_OWNER] == o[OCC_OWNER])          continue;
        if (o[OCC_ACTIVE] != 1)                               continue;
        if (o[OCC_IDLE] != 0)                                 continue;
        int d = engine_path_dist_chebyshev(ax, ay, (int8_t)o[OCC_COL], (int8_t)o[OCC_ROW]);   /* FUN_00404f4c */
        if (d < best) { best_idx = g_combat_active_terr; best = d; }
    }
    g_combat_active_terr = best_idx;
    return best;
}

/* Port of FUN_00467532 — find the nearest live, idle, engageable army whose home
 * region is `region` to `occ` (Chebyshev), leaving its index in g_combat_active_terr and
 * returning the distance.  Engageability is engine_diplomacy_should_engage. */
static int engine_army_ai_nearest_engageable_army(int occ, unsigned region)
{
    int best = 1000, best_idx = 0;
    int ax = (int8_t)OCC_REC(occ)[OCC_COL], ay = (int8_t)OCC_REC(occ)[OCC_ROW];
    for (g_combat_active_terr = 1; g_combat_active_terr < 0x97; g_combat_active_terr++) {
        uint8_t *o = OCC_REC(g_combat_active_terr);
        if (o[OCC_OWNER] == 0)                                continue;
        if (o[OCC_ACTIVE] != 1)                               continue;
        if (engine_diplomacy_should_engage((int8_t)OCC_REC(occ)[OCC_OWNER],
                                           (int8_t)o[OCC_OWNER]) == 0) continue;
        if (o[OCC_IDLE] != 0)                                 continue;
        if ((uint8_t)o[OCC_HOME_REGION] != region)            continue;
        int d = engine_path_dist_chebyshev(ax, ay, (int8_t)o[OCC_COL], (int8_t)o[OCC_ROW]);   /* FUN_00404f4c */
        if (d < best) { best_idx = g_combat_active_terr; best = d; }
    }
    g_combat_active_terr = best_idx;
    return best;
}

/* Port of FUN_004a64ca — set occupant `occ`'s march destination toward region
 * `region`: clear the placed flag, pick a deploy cell (settled-cell finder when
 * the region holds a keep with a pending garrison, else the owned-cell finder),
 * and copy the find-nearest scratch into the destination fields.  Identical to
 * brain step 9's assign-deployment tail. */
int engine_army_ai_set_deploy_cell(int occ, unsigned region)   /* retail's EAX: the finder's, low byte = the row */
{
    int r;
    OCC_REC(occ)[OCC_PLACED] = 0;
    if (ARMY_REC(region)[ARMY_KEEP_LEVEL] == 0 || I32(ARMY_REC(region) + ARMY_PENDING) == 0)
        r = engine_army_ai_find_owned_cell(occ, region);
    else
        r = engine_army_ai_find_settled_cell(occ, region);
    OCC_REC(occ)[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
    OCC_REC(occ)[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
    return (int)(((uint32_t)r & ~0xffu) | (uint8_t)g_cell_cursor_row);
}

/* Port of FUN_00438681 — disband occupant `occ` back into a realm: pour its
 * carried goods into the owner's funding reserves and its troops into the chosen
 * region's population, recompute that realm's economy, destroy the occupant, and
 * re-plan twice.  The destination region is the army's secondary region (+0x11)
 * if the owner holds it, else its home region (+0x10). */
void engine_army_ai_disband_into_realm(int occ)
{
    int owner = (int8_t)OCC_REC(occ)[OCC_OWNER];
    engine_unit_clear_contract(occ);                        /* FUN_004ac6fe */

    uint8_t *s = STRAT_REC(owner);
    for (int g = 0; g < 6; g++)
        I32(s + STRAT_RESERVE_140 + g * 4) +=
            *(int16_t *)(OCC_REC(occ) + OCC_GOODS + g * 2);

    unsigned region = (uint8_t)OCC_REC(occ)[OCC_TARGET_REGION];
    if ((uint8_t)ARMY_REC(region)[ARMY_OWNER] != (unsigned)owner)
        region = (uint8_t)OCC_REC(occ)[OCC_HOME_REGION];

    int head = I32(OCC_REC(occ) + OCC_HEAD);
    I32(ARMY_REC(region) + ARMY_FEED_COUNT)    += head;
    I32(ARMY_REC(region) + ARMY_GARRISON_HEAD) += head;
    I32(ARMY_REC(region) + ARMY_POP_BASE)      += head;

    engine_army_economy_plan_realm((int)region, (int)s_slot_0057c92c);  /* FUN_004485a5 */
    engine_unit_disband(occ);                                           /* FUN_004aa039 */
    if ((unsigned)I32(ARMY_REC(region) + ARMY_PENDING) == (unsigned)occ)
        I32(ARMY_REC(region) + ARMY_PENDING) = 0;
    I32(STRAT_REC(owner) + STRAT_WAGE_TOTAL) = engine_unit_total_wage(owner); /* FUN_004ad495 */

    for (int rep = 0; rep < 2; rep++) {
        engine_territory_allocate_realm_workforce((int)region);             /* FUN_0044f6e7 */
        engine_army_economy_provision((int)region, (int)s_slot_0057c934);   /* FUN_0044df5f */
        engine_army_economy_plan_realm((int)region, (int)s_slot_0057c92c);  /* FUN_004485a5 */
    }
}

/* Port of FUN_004a58f4 — order 7 (attack-march): unless the army is held, point
 * it at the deploy cell toward its target realm (+0x151) via the combat
 * find-deploy-cell scanner and clear the placed flag.  Returns 1 when it set a
 * destination (army not held), else 0. */
int engine_army_ai_order_attack_march(int occ)
{
    if (OCC_REC(occ)[OCC_HELD] != 0)
        return 0;
    int col = 0, row = 0;
    engine_combat_find_deploy_cell(occ, (uint8_t)OCC_REC(occ)[OCC_TARGET], &col, &row);
    OCC_REC(occ)[OCC_DEST_COL] = (uint8_t)col;
    OCC_REC(occ)[OCC_DEST_ROW] = (uint8_t)row;
    OCC_REC(occ)[OCC_PLACED]   = 0;
    return 1;
}

/* Port of FUN_004a5b1f — order 2 (advance/seek).  Held armies skip.  Otherwise:
 * if a hostile army is within 6 tiles (and outranks the local advance / isn't a
 * lord's), march to intercept it; else validate or re-pick a target realm and
 * deploy toward it.  Returns 1 when it set a destination, 0 when no target. */
int engine_army_ai_order_advance(int occ)
{
    if (OCC_REC(occ)[OCC_HELD] != 0)
        return 0;

    int owner = (int8_t)OCC_REC(occ)[OCC_OWNER];

    if (engine_army_ai_nearest_enemy_army(occ) < 6) {
        uint8_t *n = OCC_REC(g_combat_active_terr);
        if ((n[OCC_ORDER] != 2 || (int8_t)OCC_REC(occ)[OCC_COL] < (int8_t)n[OCC_COL]) &&
            (uint8_t)STRAT_REC(owner)[STRAT_LORD] != (uint8_t)n[OCC_OWNER]) {
            OCC_REC(occ)[OCC_PLACED]   = 0;
            OCC_REC(occ)[OCC_DEST_COL] = n[OCC_COL];
            OCC_REC(occ)[OCC_DEST_ROW] = n[OCC_ROW];
            return 1;
        }
    }

    unsigned target = (uint8_t)OCC_REC(occ)[OCC_TARGET];
    if (target != 0 &&
        (uint8_t)ARMY_REC(target)[ARMY_OWNER] != (unsigned)owner &&
        engine_realm_player_owns_neighbor((unsigned)owner, (int)target) != 0) {
        engine_army_ai_set_deploy_cell(occ, target);        /* existing target still good */
        return 1;
    }

    int home = (uint8_t)OCC_REC(occ)[OCC_HOME_REGION];
    if ((uint8_t)ARMY_REC(home)[ARMY_OWNER] == (unsigned)owner ||
        engine_realm_player_owns_neighbor((unsigned)owner, home) == 0) {
        /* retail's && short-circuit: only re-pick (reassigning `home`) when the
         * home region does NOT border an enemy. */
        int take = 0;
        if (engine_army_ai_region_borders_enemy(home) == 0) {
            home = engine_army_ai_pick_attack_target(owner, home);
            if (home != 0)
                take = 1;
        }
        if (take) {
            OCC_REC(occ)[OCC_TARGET] = (uint8_t)home;
            engine_army_ai_set_deploy_cell(occ, (unsigned)home);
            return 1;
        }
        int t = engine_army_ai_pick_attack_target(owner, home);
        if (t == 0)
            return 0;
        OCC_REC(occ)[OCC_TARGET] = (uint8_t)t;
        engine_army_ai_set_deploy_cell(occ, (unsigned)t);
        return 1;
    } else {
        if (STRAT_REC(owner)[STRAT_LORD] == ARMY_REC(home)[ARMY_OWNER]) {
            home = engine_army_ai_pick_attack_target(owner, home);
            OCC_REC(occ)[OCC_TARGET] = (uint8_t)home;
        }
        engine_army_ai_set_deploy_cell(occ, (unsigned)home);
        return 1;
    }
}

/* Port of FUN_004a599d — order 6 (lord-directed move).  Validate the campaign
 * target (+0x19b); if it cleared, downgrade to order 2 and run the advance
 * handler; else intercept the nearest engageable army in that region when within
 * 21 tiles.  Returns 1 when it set a destination, else 0. */
static int engine_army_ai_order_lord_move(int occ)
{
    if (OCC_REC(occ)[OCC_HELD] != 0)
        return 0;
    if (OCC_REC(occ)[OCC_CAMPAIGN] != 0) {
        int v = engine_army_ai_validate_campaign_target(
            (int8_t)OCC_REC(occ)[OCC_CAMPAIGN], (int8_t)OCC_REC(occ)[OCC_OWNER]);
        OCC_REC(occ)[OCC_CAMPAIGN] = (uint8_t)v;
    }
    if ((uint8_t)OCC_REC(occ)[OCC_CAMPAIGN] == 0) {
        OCC_REC(occ)[OCC_ORDER] = 2;
        return engine_army_ai_order_advance(occ);
    }
    if (engine_army_ai_nearest_engageable_army(occ, (uint8_t)OCC_REC(occ)[OCC_CAMPAIGN]) < 0x15) {
        OCC_REC(occ)[OCC_PLACED]   = 0;
        OCC_REC(occ)[OCC_DEST_COL] = OCC_REC(g_combat_active_terr)[OCC_COL];
        OCC_REC(occ)[OCC_DEST_ROW] = OCC_REC(g_combat_active_terr)[OCC_ROW];
        return 1;
    }
    return 0;
}

/* Port of FUN_004a5f0a — order 3 (intercept).  Unless held: if a engageable army
 * in the secondary region (+0x11) is within 31 tiles (and outranks / isn't a
 * lord's), march to it; else deploy toward that region.  Always returns 1 unless
 * held. */
int engine_army_ai_order_intercept(int occ)
{
    if (OCC_REC(occ)[OCC_HELD] != 0)
        return 0;
    unsigned region = (uint8_t)OCC_REC(occ)[OCC_TARGET_REGION];
    int owner = (int8_t)OCC_REC(occ)[OCC_OWNER];
    if (engine_army_ai_nearest_engageable_army(occ, region) < 0x1f) {
        uint8_t *n = OCC_REC(g_combat_active_terr);
        if ((n[OCC_ORDER] != 2 || (int8_t)OCC_REC(occ)[OCC_COL] < (int8_t)n[OCC_COL]) &&
            (uint8_t)STRAT_REC(owner)[STRAT_LORD] != (uint8_t)n[OCC_OWNER]) {
            OCC_REC(occ)[OCC_PLACED]   = 0;
            OCC_REC(occ)[OCC_DEST_COL] = n[OCC_COL];
            OCC_REC(occ)[OCC_DEST_ROW] = n[OCC_ROW];
            return 1;
        }
    }
    engine_army_ai_set_deploy_cell(occ, region);
    return 1;
}

/* Port of FUN_004a6270 — order 4 (garrison).  If the target keep can hold this
 * army (spare cap ≥ head, no siege, owned by us), deploy toward its settled
 * cell; otherwise find another garrison realm (retarget + deploy), or disband
 * the army back into a realm when none can hold it.  Always returns 1. */
int engine_army_ai_order_garrison(int occ)
{
    unsigned region = (uint8_t)OCC_REC(occ)[OCC_TARGET];
    int pending_head = 0;
    int pending = I32(ARMY_REC(region) + ARMY_PENDING);
    if (pending != 0)
        pending_head = I32(OCC_REC(pending) + OCC_HEAD);

    int cap = engine_combat_garrison_cap(ARMY_REC(region)[ARMY_KEEP_LEVEL]);
    if (cap - pending_head < I32(OCC_REC(occ) + OCC_HEAD) ||
        ARMY_REC(region)[ARMY_SIEGE_BLOCK] != 0 ||
        ARMY_REC(region)[ARMY_SIEGE_MODE] == 1 ||
        ARMY_REC(region)[ARMY_KEEP_LEVEL] == 0 ||
        (uint8_t)ARMY_REC(region)[ARMY_OWNER] != (unsigned)(int8_t)OCC_REC(occ)[OCC_OWNER]) {
        int realm = engine_combat_find_garrison_realm((int8_t)OCC_REC(occ)[OCC_OWNER], occ);
        if (realm == 0) {
            engine_army_ai_disband_into_realm(occ);
        } else {
            OCC_REC(occ)[OCC_TARGET] = (uint8_t)realm;
            OCC_REC(occ)[OCC_PLACED] = 0;
            engine_army_ai_find_settled_cell(occ, (unsigned)realm);
            OCC_REC(occ)[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
            OCC_REC(occ)[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
        }
    } else {
        OCC_REC(occ)[OCC_PLACED] = 0;
        engine_army_ai_find_settled_cell(occ, region);
        OCC_REC(occ)[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
        OCC_REC(occ)[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
    }
    return 1;
}

/* Port of FUN_004a60b9 — order 5 (siege).  If the target realm is now ours, or
 * has no keep / is already under siege / carries a siege block, hand the army to
 * the combat transfer executor and downgrade to order 2 (returning 0); only when
 * the keep is besiegable do we deploy toward an owned cell and return 1. */
static int engine_army_ai_order_siege(int occ, engine_event_queue_state_t *evq, int mp_active)
{
    unsigned region = (uint8_t)OCC_REC(occ)[OCC_TARGET];
    if ((uint8_t)ARMY_REC(region)[ARMY_OWNER] == (unsigned)(int8_t)OCC_REC(occ)[OCC_OWNER])
        return 0;
    if (ARMY_REC(region)[ARMY_KEEP_LEVEL] == 0 ||
        ARMY_REC(region)[ARMY_SIEGE_MODE] == 1) {
        engine_combat_transfer_army((unsigned)occ, (int)region, evq, mp_active);
        OCC_REC(occ)[OCC_ORDER] = 2;
        return 0;
    }
    if (ARMY_REC(region)[ARMY_SIEGE_BLOCK] == 0) {
        OCC_REC(occ)[OCC_ORDER] = 2;
        engine_combat_transfer_army((unsigned)occ, (int)region, evq, mp_active);
        OCC_REC(occ)[OCC_PLACED] = 0;
        engine_army_ai_find_owned_cell(occ, region);
        OCC_REC(occ)[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
        OCC_REC(occ)[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
        return 1;
    }
    engine_combat_transfer_army((unsigned)occ, (int)region, evq, mp_active);
    OCC_REC(occ)[OCC_ORDER] = 2;
    return 0;
}

/* Port of FUN_004a57ac — dispatch occupant `occ` to its order handler by the
 * order code (+0x1a); an unknown code resets the order to 2 and returns 0.
 * Threads evq/mp_active down to the siege handler (which calls the combat
 * transfer executor). */
static int engine_army_ai_dispatch_order(int occ, engine_event_queue_state_t *evq, int mp_active)
{
    switch (OCC_REC(occ)[OCC_ORDER]) {
    case 7:  return engine_army_ai_order_attack_march(occ);
    case 6:  return engine_army_ai_order_lord_move(occ);
    case 2:  return engine_army_ai_order_advance(occ);
    case 3:  return engine_army_ai_order_intercept(occ);
    case 4:  return engine_army_ai_order_garrison(occ);
    case 5:  return engine_army_ai_order_siege(occ, evq, mp_active);
    default: OCC_REC(occ)[OCC_ORDER] = 2; return 0;
    }
}

/* Port of FUN_004a5667 — AI brain step 0xb (army-movement planner).  For every
 * live army `realm` owns, recompute its march order; when the handler set a
 * destination, fill the cost field from the army's seat (slot 0), trace the
 * cheapest descent to the destination, and — if reachable — copy the path into
 * the army's waypoint record and flag the path-OK byte. */
void engine_army_ai_run_movement(int realm, engine_event_queue_state_t *evq, int mp_active)
{
    CALL_TRACE_ENTER(0x004a5667);

    for (int occ = 1; occ < 0x97; occ++) {
        uint8_t *c = OCC_REC(occ);
        if (c[OCC_ACTIVE] != 1)                 continue;
        if ((int8_t)c[OCC_OWNER] != realm)      continue;
        if (engine_army_ai_dispatch_order(occ, evq, mp_active) == 0)
            continue;

        c[OCC_PATH_FAIL] = 0;
        engine_path_field_fill(0, (int8_t)c[OCC_COL], (int8_t)c[OCC_ROW], 0);
        if (engine_path_field_trace(0, (int8_t)c[OCC_DEST_COL], (int8_t)c[OCC_DEST_ROW]) != 0) {
            engine_path_field_copy_record(0, occ);
            c[OCC_PATH_OK] = 1;
        }
    }
}

/* ── Brain step 7: defensive deployment planner (FUN_0049f93d + 6 callees) ──
 *
 * Step 7 is the AI's defence layer.  It first re-scores the realms to refresh the
 * muster realm (engine_army_ai_pick_muster_realms = FUN_004a0aaa), then runs two
 * planners:
 *   garrison_keeps   (FUN_0049f12f) — for each owned keep that is unbesieged and
 *     big enough (population ≥ the personality's keep floor), muster a garrison
 *     up to the keep's head cap and stamp it order 4 at a settled cell.
 *   reinforce_realms (FUN_0049f431) — for each owned realm where the enemy army
 *     strength present outweighs the player's, muster a defender; if it is the
 *     muster realm or critically outnumbered the defender stays put (order 3),
 *     otherwise it heads for the muster realm and the realm ships its surplus
 *     provisions/stock there (a goods caravan) or sells them.
 *
 * Personality-row columns step 7 reads (offsets into a personality_row()). */
#define AI_PERS_KEEP_MIN_POP     0x70  /* i32: min realm population to bother garrisoning a keep */
#define AI_PERS_REINFORCE_RATION 0x9c  /* u8: ration the AI sets on a reinforced realm */

/* Caravan occupant fields (offsets into an OCC_REC; the carrier the caravan
 * dispatcher spawns).  +0x166/+0x167 record the source/destination realm; the
 * three i16 at +0x16c..+0x170 are the carried provisions/?/stock load. */
#define OCC_CARAVAN_SRC   0x166  /* DAT_0052f216 — source realm (u8) */
#define OCC_CARAVAN_DST   0x167  /* DAT_0052f217 — destination realm (u8) */
#define OCC_CARAVAN_LOAD  0x16c  /* DAT_0052f21c — first of three i16 cargo cells */

/* Per-player staged goods-shipment order (retail DAT_005678c0, stride 0x18 = six
 * i32; the trade/gift UI shares it, indexed by the local player).  reinforce_realms
 * stages a realm's surplus into fields [1]/[5] (provisions/stock) and the caravan
 * dispatcher reads [1]/[3]/[5].  The Send Supplies UI (engine_sendsupplies) now
 * stages fields [0]/[4] (have) and moves them into [1]/[5] (to-send) via the
 * arrows, then ships through engine_army_ai_dispatch_caravan — so the accessor
 * below hands out the same array the AI uses (byte-identical player/AI paths). */
/* g_caravan_order — DAT_005678c0, defined in engine_economy_globals.c (the hybrid host aliases it at retail's VA) */

/* Accessor for the shared staging order — see engine_sendsupplies.c. */
int32_t *engine_army_ai_caravan_order(int player)
{
    return g_caravan_order[player];
}

/* Port of FUN_0049f73f (273 B) — sum the head-count of every live army standing
 * in realm `realm` that is owned neither by `player` nor by `player`'s lord (the
 * enemy strength bearing on the realm). */
static int engine_army_ai_realm_enemy_strength(int player, int realm)
{
    uint8_t *s = STRAT_REC(player);
    int total = 0;
    for (int occ = 1; occ < 0x97; occ++) {
        uint8_t *c = OCC_REC(occ);
        if (c[OCC_ACTIVE] != 1)                         continue;
        if (c[OCC_OWNER] == 0)                          continue;
        if ((uint8_t)c[OCC_HOME_REGION] != (uint8_t)realm)  continue;
        if ((int8_t)c[OCC_OWNER] == player)             continue;
        if ((uint8_t)s[STRAT_LORD] == (uint8_t)(int8_t)c[OCC_OWNER]) continue;
        total += I32(c + OCC_HEAD);
    }
    return total;
}

/* Port of FUN_0049f850 (237 B) — sum the head-count of `player`'s own live armies
 * standing in realm `realm` (the friendly strength already defending it). */
static int engine_army_ai_realm_own_strength(int player, int realm)
{
    int total = 0;
    for (int occ = 1; occ < 0x97; occ++) {
        uint8_t *c = OCC_REC(occ);
        if (c[OCC_ACTIVE] != 1)                         continue;
        if (c[OCC_OWNER] == 0)                          continue;
        if ((uint8_t)c[OCC_HOME_REGION] != (uint8_t)realm)  continue;
        if (player != (int8_t)c[OCC_OWNER])             continue;
        total += I32(c + OCC_HEAD);
    }
    return total;
}

/* Port of FUN_004292af (361 B) — dispatch a goods caravan for `player`: find a
 * settle cell near `realm`'s capital, spawn a type-4 carrier there loaded with
 * the staged goods order (g_caravan_order[player]) bound for `dest`, and debit
 * the shipped provisions/stock from `realm`.  No-op if no settle cell is found.
 * Retail ignores the placer's return and always stamps the current-slot record
 * (engine_territory_current_slot), as engine_game_init_place_army_starts does. */
void engine_army_ai_dispatch_caravan(int player, int realm, uint8_t dest)
{
    int col, row;
    if (!engine_army_state_find_settle_spot(realm, &col, &row) &&        /* FUN_00428007 */
        !engine_game_init_army_near_rawflag_only_bit1(realm, &col, &row)) /* FUN_00428078 */
        return;

    engine_territory_place_entity(4, col, row, player);   /* FUN_0046e1b0 (return ignored) */

    int      cur = engine_territory_current_slot();       /* DAT_0052afe0 */
    uint8_t *c   = OCC_REC(cur);
    int32_t *ord = g_caravan_order[player];

    c[OCC_PLACED]       = 0;
    c[OCC_HOME_REGION]  = (uint8_t)realm;
    c[OCC_CARAVAN_SRC]  = (uint8_t)realm;
    c[OCC_CARAVAN_DST]  = dest;
    *(int16_t *)(c + OCC_CARAVAN_LOAD + 0) = (int16_t)ord[1];
    *(int16_t *)(c + OCC_CARAVAN_LOAD + 2) = (int16_t)ord[3];
    *(int16_t *)(c + OCC_CARAVAN_LOAD + 4) = (int16_t)ord[5];

    I32(ARMY_REC(realm) + ARMY_PROVISION_RAW) -= ord[1];
    I32(ARMY_REC(realm) + ARMY_STOCK_A)       -= ord[5];
}

/* Port of FUN_0049f12f (765 B) — garrison `player`'s keeps.  For each owned realm
 * with a keep that is unbesieged, not flagged settled, populous enough for the
 * personality, and with both funding reserves above 0x13: size the garrison need
 * as the keep's head cap minus any pending garrison, scale it down by how large a
 * fraction of the realm's population it would consume, muster that force, and
 * stamp it order 4 (garrison) bound for a settled cell in the realm. */
static void engine_army_ai_garrison_keeps(int player)
{
    uint8_t *s     = STRAT_REC(player);
    int      count = (int)s_slot_0056d5dc;

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)                 continue;
        if (r[ARMY_KEEP_LEVEL] == 0)                 continue;
        if (r[ARMY_SETTLE_FLAG] == 1)                continue;
        if (r[ARMY_SIEGE_BLOCK] != 0)                continue;
        if (I32(personality_row(s[STRAT_PERSONALITY]) + AI_PERS_KEEP_MIN_POP)
                > I32(r + ARMY_FEED_COUNT))          continue;
        if (I32(s + STRAT_RESERVE_150) + I32(s + STRAT_RESERVE_140) <= 0x13) continue;

        int pending_head = 0;
        if (I32(r + ARMY_PENDING) != 0)
            pending_head = I32(OCC_REC(I32(r + ARMY_PENDING)) + OCC_HEAD);

        int need = engine_combat_garrison_cap(r[ARMY_KEEP_LEVEL]) - pending_head;
        if (need <= 0x13)                            continue;

        int pop = I32(r + ARMY_FEED_COUNT);
        int pct = engine_math_percent_div(need, pop);
        if (pct >= 0x50)                             continue;

        /* Scale the garrison down the larger a fraction of the realm it is. */
        if (pct < 0x41) {
            if (pct < 0x32) {
                if (pct < 0x1e) {
                    if (pct > 0xe) need /= 2;        /* 15..29% */
                } else      need /= 3;               /* 30..49% */
            } else          need /= 4;               /* 50..64% */
        } else              need /= 5;               /* 65..79% */

        pct = engine_math_percent_div(need, pop);
        if (engine_combat_muster_garrison(realm, pct) == 0) continue;

        int      cur = engine_territory_current_slot();
        uint8_t *c   = OCC_REC(cur);
        c[OCC_ORDER]  = 4;                           /* garrison */
        c[OCC_PLACED] = 0;
        c[OCC_TARGET] = (uint8_t)realm;
        engine_army_ai_find_settled_cell(cur, realm);   /* FUN_004a65a3 */
        c[OCC_DEST_COL] = (uint8_t)g_cell_cursor_col;
        c[OCC_DEST_ROW] = (uint8_t)g_cell_cursor_row;
    }
}

/* Port of FUN_0049f431 (782 B) — reinforce `player`'s threatened realms.  For each
 * owned realm with no keep (or no pending garrison) where the enemy strength
 * present exceeds the player's by more than 100, muster a defender: if the realm
 * is the muster realm or the threat is below the player's defence threshold the
 * defender garrisons in place (order 3); otherwise it marches to the muster realm
 * (its target region) and the realm consolidates — shipping its surplus
 * provisions/stock to the muster realm as a caravan, or (if a trade gate is set)
 * selling them. */
static void engine_army_ai_reinforce_realms(int player)
{
    uint8_t *s     = STRAT_REC(player);
    int      count = (int)s_slot_0056d5dc;

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)                                 continue;
        if (r[ARMY_KEEP_LEVEL] != 0 && I32(r + ARMY_PENDING) != 0)   continue;

        int enemy = engine_army_ai_realm_enemy_strength(player, realm);
        int own   = engine_army_ai_realm_own_strength(player, realm);
        if (enemy <= 99 || own >= enemy - 100)                       continue;

        int thresh = I32(s + STRAT_FORCE_ACC) + (I32(r + ARMY_FEED_COUNT) * 2) / 3;
        if (I32(r + ARMY_FEED_COUNT) < 200)
            thresh = 0;

        if (enemy < thresh || (uint8_t)s[STRAT_MUSTER_REALM] == (uint8_t)realm) {
            if (engine_combat_muster_defender(realm, 0x32, 100, 1) != 0)
                OCC_REC(engine_territory_current_slot())[OCC_ORDER] = 3;
            continue;
        }

        if (engine_combat_muster_defender(realm, 0x32, 100, 0) != 0) {
            uint8_t *c = OCC_REC(engine_territory_current_slot());
            c[OCC_TARGET_REGION] = s[STRAT_MUSTER_REALM];
            c[OCC_ORDER]         = 3;
        }
        r[ARMY_RATION]       = (uint8_t)personality_row(s[STRAT_PERSONALITY])[AI_PERS_REINFORCE_RATION];
        r[ARMY_STRATEGY_TAG] = 100;

        if (r[ARMY_TRADE_GATE] == 0) {
            if (I32(r + ARMY_PROVISION_RAW) > 10 || I32(r + ARMY_STOCK_A) > 5) {
                g_caravan_order[player][1] = I32(r + ARMY_PROVISION_RAW);
                g_caravan_order[player][0] = 0;
                g_caravan_order[player][5] = I32(r + ARMY_STOCK_A);
                g_caravan_order[player][4] = 0;
                engine_army_ai_dispatch_caravan(player, realm, s[STRAT_MUSTER_REALM]);
            }
        } else {
            engine_army_ai_sell_order(realm, I32(r + ARMY_PROVISION_RAW), 1);
            engine_army_ai_sell_order(realm, I32(r + ARMY_STOCK_A), 2);
        }
    }
}

/* Port of FUN_0049f93d (47 B) — AI brain step 7 entry.  Refresh the muster realm,
 * then run the keep-garrison and realm-reinforcement planners. */
void engine_army_ai_run_garrison(int player)
{
    CALL_TRACE_ENTER(0x0049f93d);
    engine_army_ai_pick_muster_realms(player);   /* FUN_004a0aaa */
    engine_army_ai_garrison_keeps(player);        /* FUN_0049f12f */
    engine_army_ai_reinforce_realms(player);      /* FUN_0049f431 */
}

/* ── Brain step 6: AI keep-builder (FUN_0049edc7 + 5 callees) ────────────────
 *
 * For each owned realm big enough with no keep yet, build the highest keep tier
 * the treasury affords.  The construction primitive (build_keep = FUN_00436d02)
 * is shared with the human build-keep action; it sets the keep level, debits the
 * two resource pools, evicts any armies off the 2×2 keep footprint, and re-plans
 * the realm's economy.  No new render state — only save-schema realm/strat fields.
 *
 * Keep cost table (retail .rdata 0x4d89b8, six 8-byte entries indexed by keep
 * level 0..5; +0 = the pool-130 cost, +4 = the pool-128 cost).  Building level
 * `lvl` costs entry[lvl]; an upgrade refunds the old level's entry. */
static const int32_t k_keep_cost[6][2] = {
    {   4,   18 },   /* level 0 (base; never built, indexes the refund of a razed keep) */
    { 400,   40 },   /* level 1 */
    { 800,   80 },   /* level 2 */
    { 200, 1000 },   /* level 3 */
    { 400, 2000 },   /* level 4 */
    { 800, 3000 },   /* level 5 */
};

/* Keep build-work figure by tier (retail .rdata 0x4d89e8, 8-byte stride; only the
 * +0 word is read).  Seeds the realm's keep-progress total when construction
 * starts. */
static const int32_t k_keep_work[5] = { 200, 400, 800, 1500, 2500 };

/* Per-realm record fields step 6 adds (offsets into g_per_army_768 row). */
#define ARMY_KEEP_ANCHOR_CELL 0x078  /* DAT_0053fa28 — keep footprint anchor cell byte-offset (i32) */
#define ARMY_KEEP_WORK_UNIT   0x0e8  /* DAT_0053fa98 — per-turn keep build-work unit (rate divisor) */
#define ARMY_KEEP_RATE_LO     0x0ec  /* DAT_0053fa9c — keep build-rate scratch (i32; −1 sentinel) */
#define ARMY_KEEP_RATE_HI     0x0f0  /* DAT_0053faa0 — keep build-rate scratch (i32) */
#define ARMY_KEEP_TURNS       0x1a6  /* DAT_0053fb56 — turns to finish the keep (u8; ceil work/unit) */
#define ARMY_KEEP_PREV_LEVEL  0x1c1  /* DAT_0053fb71 — keep level before an upgrade (u8) */
#define ARMY_KEEP_FLAG_1C4    0x1c4  /* DAT_0053fb74 — keep-construction flag, cleared on start */
#define ARMY_KEEP_WORK_TOTAL  0x1cc  /* DAT_0053fb7c — total keep build work (i32; the k_keep_work seed) */
#define ARMY_KEEP_WORK_TOTAL2 0x1d8  /* DAT_0053fb88 — copy of the build-work total */
#define ARMY_KEEP_COST_TOT_LO 0x1dc  /* DAT_0053fb8c — total pool-128 cost (completion denominator) */
#define ARMY_KEEP_COST_TOT_HI 0x1e0  /* DAT_0053fb90 — total pool-130 cost (completion denominator) */

/* Per-player record field step 6 adds. */
#define STRAT_KEEP_COUNT      0x04d  /* DAT_0057bf4d — number of keeps the player has built (u8) */

/* Occupant fields step 6's army-relocate adds (offsets into an OCC_REC). */
#define OCC_MOVE_DEST_COL     0x014  /* DAT_0052f0c4 — move-executor destination column (u8) */
#define OCC_MOVE_DEST_ROW     0x015  /* DAT_0052f0c5 — move-executor destination row (u8) */
#define OCC_MOVE_BUDGET       0x153  /* DAT_0052f203 — remaining move budget this turn (i8) */

/* Personality-row columns step 6 reads (offsets into a personality_row()). */
#define AI_PERS_KEEP_MAX          0x90  /* i32: max keeps the personality will build */
#define AI_PERS_KEEP_BUILD_MINPOP 0xc8  /* i32: min realm population to build a keep */
#define AI_PERS_KEEP_COST_1       0xcc  /* i32: tier-1 cash the personality requires */
#define AI_PERS_KEEP_COST_2       0xd0  /* i32: tier-2 */
#define AI_PERS_KEEP_COST_3       0xd4  /* i32: tier-3 */
#define AI_PERS_KEEP_COST_4       0xd8  /* i32: tier-4 */
#define AI_PERS_KEEP_COST_5       0xdc  /* i32: tier-5 */

/* Port of FUN_00467196 (422 B) — relocate army `occ` off the cell it stands on:
 * find the nearest map feature ring to its seat, dock its move budget by the ring
 * distance, set the new destination, and route there via the cost-field
 * pathfinder.  No-op if no ring cell is found. */
static void engine_army_ai_relocate_army(int occ)
{
    uint8_t *c = OCC_REC(occ);
    int col, row;
    int d = engine_map_query_nearest_feature_ring_at(   /* FUN_0046733c */
                (int8_t)c[OCC_COL], (int8_t)c[OCC_ROW], &col, &row);
    if (d == 0)
        return;

    if ((int8_t)c[OCC_MOVE_BUDGET] < d)
        c[OCC_MOVE_BUDGET] = 0;
    else
        c[OCC_MOVE_BUDGET] = (uint8_t)((int8_t)c[OCC_MOVE_BUDGET] - d);

    c[OCC_PATH_OK]       = 2;
    c[OCC_MOVE_DEST_COL] = (uint8_t)col;
    c[OCC_MOVE_DEST_ROW] = (uint8_t)row;
    c[OCC_PATH_FAIL]     = 0;

    engine_path_field_fill(0, (int8_t)c[OCC_COL], (int8_t)c[OCC_ROW], 0);   /* FUN_0046f700 */
    if (engine_path_field_trace(0, (int8_t)c[OCC_MOVE_DEST_COL],
                                   (int8_t)c[OCC_MOVE_DEST_ROW]) != 0)      /* FUN_004701ac */
        engine_path_field_copy_record(0, occ);                             /* FUN_004707be */
}

/* Port of FUN_0046841c (170 B) — evict any armies standing on the 2×2 keep
 * footprint anchored at the realm's keep cell: relocate each off the footprint and
 * flag its path stale.  The footprint walks (anchor, anchor+1 col) × (two rows). */
static void engine_army_ai_evict_keep_footprint(int realm)
{
    int cell = I32(ARMY_REC(realm) + ARMY_KEEP_ANCHOR_CELL);   /* byte offset into the grid */
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            unsigned occ = (uint8_t)g_overworld_map[cell + 5];  /* head-link byte */
            if (occ != 0) {
                engine_army_ai_relocate_army((int)occ);         /* FUN_00467196 */
                OCC_REC(occ)[OCC_PATH_FAIL] = 1;
            }
            cell += 8;
        }
        cell += 0x1f0;                                          /* next row, same column */
    }
}

/* Port of FUN_00450fb4 (173 B) — the keep's construction completion percentage:
 * the smaller of the two pools' (100 − remaining/total %).  0 when no keep is
 * under construction (settle flag clear). */
static int engine_army_ai_keep_completion(int realm)
{
    uint8_t *r = ARMY_REC(realm);
    if (r[ARMY_SETTLE_FLAG] == 0)
        return 0;

    int hi = engine_math_percent_div(I32(r + ARMY_BUILD_DRAW_HI), I32(r + ARMY_KEEP_COST_TOT_HI));
    int lo = engine_math_percent_div(I32(r + ARMY_BUILD_DRAW_LO), I32(r + ARMY_KEEP_COST_TOT_LO));
    lo = 100 - lo;
    if (100 - hi < lo)
        lo = 100 - hi;
    return lo;
}

/* Port of FUN_00450e46 (366 B) — recompute the realm's keep-build rate: while a
 * keep is under construction, latch the per-turn work figure and the turns-to-
 * finish (ceil total-work / work-unit), or 100 turns when complete / no unit. */
static void engine_army_ai_update_keep_rate(int realm)
{
    uint8_t *r = ARMY_REC(realm);
    I32(r + ARMY_KEEP_RATE_LO) = -1;
    I32(r + ARMY_KEEP_RATE_HI) = 0;
    if (r[ARMY_SETTLE_FLAG] == 0)
        return;

    (void)engine_math_percent_div(I32(r + ARMY_KEEP_WORK_TOTAL),     /* retail's dead pure call */
                                  I32(r + ARMY_KEEP_WORK_TOTAL2));
    int pct = engine_army_ai_keep_completion(realm);                  /* FUN_00450fb4 */

    I32(r + ARMY_KEEP_RATE_LO) = -1;
    I32(r + ARMY_KEEP_RATE_HI) = (pct < 100) ? 0 : I32(r + ARMY_KEEP_WORK_TOTAL);

    if (pct < 100 || I32(r + ARMY_KEEP_WORK_UNIT) < 1) {
        r[ARMY_KEEP_TURNS] = 100;
    } else {
        int turns = I32(r + ARMY_KEEP_WORK_TOTAL) / I32(r + ARMY_KEEP_WORK_UNIT);
        if (I32(r + ARMY_KEEP_WORK_TOTAL) % I32(r + ARMY_KEEP_WORK_UNIT) != 0)
            turns++;
        r[ARMY_KEEP_TURNS] = (uint8_t)(char)turns;
    }
}

/* Port of FUN_00436d02 (768 B) — build (or upgrade) realm `realm`'s keep to level
 * `tier`+1.  Charges the level's two pool costs (refunding the old level on an
 * upgrade), stamps the keep fields + build-work totals, evicts armies off the
 * footprint, and re-plans the realm's economy/workforce/income.  Shared with the
 * human build-keep action (retail callers DAT_0057c93c/DAT_005158e8). */
void engine_army_ai_build_keep(int realm, int tier)
{
    uint8_t *r     = ARMY_REC(realm);
    int      owner = r[ARMY_OWNER];
    uint8_t *s     = STRAT_REC(owner);

    int cost_hi = k_keep_cost[tier + 1][0];   /* pool-130 cost */
    int cost_lo = k_keep_cost[tier + 1][1];   /* pool-128 cost */

    if (r[ARMY_KEEP_LEVEL] != 0) {            /* upgrade: refund the old level */
        r[ARMY_KEEP_PREV_LEVEL] = r[ARMY_KEEP_LEVEL];
        cost_hi -= k_keep_cost[r[ARMY_KEEP_LEVEL]][0];
        cost_lo -= k_keep_cost[r[ARMY_KEEP_LEVEL]][1];
        if (cost_hi < 0) { I32(s + STRAT_POOL_130) -= cost_hi; cost_hi = 0; }
        if (cost_lo < 0) { I32(s + STRAT_POOL_128) -= cost_lo; cost_lo = 0; }
    }

    r[ARMY_KEEP_LEVEL]      = (uint8_t)(tier + 1);
    r[ARMY_SIEGE_BLOCK]     = 0;
    r[ARMY_SETTLE_FLAG]     = 1;
    r[ARMY_BUILD_PLAN_FLAG] = 1;
    I32(r + ARMY_KEEP_WORK_TOTAL)  = k_keep_work[tier];
    I32(r + ARMY_KEEP_WORK_TOTAL2) = k_keep_work[tier];
    I32(r + ARMY_KEEP_COST_TOT_LO) = cost_lo;
    I32(r + ARMY_KEEP_COST_TOT_HI) = cost_hi;
    r[ARMY_KEEP_FLAG_1C4] = 0;

    engine_territory_stamp_secondary_keep(realm, tier);   /* FUN_0046826c */
    engine_army_ai_evict_keep_footprint(realm);           /* FUN_0046841c */
    engine_army_ai_rebalance_workforce(realm, 3, 1);      /* FUN_004502ca */

    /* Debit the build cost from the owner's two resource pools; the unpaid
     * remainder becomes the realm's settlement build demand. */
    if (I32(s + STRAT_POOL_128) < cost_lo) { cost_lo -= I32(s + STRAT_POOL_128); I32(s + STRAT_POOL_128) = 0; }
    else                                   { I32(s + STRAT_POOL_128) -= cost_lo; cost_lo = 0; }
    if (I32(s + STRAT_POOL_130) < cost_hi) { cost_hi -= I32(s + STRAT_POOL_130); I32(s + STRAT_POOL_130) = 0; }
    else                                   { I32(s + STRAT_POOL_130) -= cost_hi; cost_hi = 0; }
    I32(r + ARMY_BUILD_DRAW_LO) = cost_lo;
    I32(r + ARMY_BUILD_DRAW_HI) = cost_hi;

    engine_army_ai_update_keep_rate(realm);                       /* FUN_00450e46 */
    engine_territory_allocate_realm_workforce(realm);             /* FUN_0044f6e7 */
    engine_army_economy_plan_realm(realm, (int)s_slot_0057c92c);  /* FUN_004485a5 */
    engine_realm_recompute_realm_income(realm);                   /* FUN_0044b80b */
}

/* Port of FUN_0049edc7 (872 B) — AI brain step 6 (keep-builder).  For each owned
 * realm big enough (population ≥ the personality keep-build floor) with no keep
 * yet, and while the player is under its keep cap, build the highest keep tier the
 * treasury (cash +0x118) affords from the personality's five cost columns. */
void engine_army_ai_run_keep_builder(int player)
{
    CALL_TRACE_ENTER(0x0049edc7);

    uint8_t       *s     = STRAT_REC(player);
    const uint8_t *pr    = personality_row(s[STRAT_PERSONALITY]);
    int            count = (int)s_slot_0056d5dc;

    for (int realm = 1; realm <= count; realm++) {
        uint8_t *r = ARMY_REC(realm);
        if (r[ARMY_OWNER] != player)                                      continue;
        if (I32(pr + AI_PERS_KEEP_BUILD_MINPOP) > I32(r + ARMY_FEED_COUNT)) continue;
        if (r[ARMY_KEEP_LEVEL] != 0)                                      continue;
        if ((int)(uint8_t)s[STRAT_KEEP_COUNT] >= I32(pr + AI_PERS_KEEP_MAX)) continue;

        int cash = I32(s + STRAT_CASH);
        int c5 = I32(pr + AI_PERS_KEEP_COST_5);
        int c4 = I32(pr + AI_PERS_KEEP_COST_4);
        int c3 = I32(pr + AI_PERS_KEEP_COST_3);
        int c2 = I32(pr + AI_PERS_KEEP_COST_2);
        int c1 = I32(pr + AI_PERS_KEEP_COST_1);

        int tier = 0;                          /* build the highest affordable tier */
        if      (c5 != 0 && c5 <= cash) tier = 5;
        else if (c4 != 0 && c4 <= cash) tier = 4;
        else if (c3 != 0 && c3 <= cash) tier = 3;
        else if (c2 != 0 && c2 <= cash) tier = 2;
        else if (c1 != 0 && c1 <= cash) tier = 1;

        if (tier != 0)
            engine_army_ai_build_keep(realm, tier - 1);
    }
}

/* ── Per-tick AI brain dispatcher (FUN_0049a581) ──────────────────────────
 *
 * Ranking-changed redraw cookie — retail _DAT_0056d6a0, set to 1 by the brain
 * terminal arm when a realm is parked at the turn-done sentinel (the field
 * ranking has settled → repaint).  Runtime-only render flag (NOT save-state);
 * the port has no consumer yet, so it is a faithful write-only mirror.
 * TODO(render): wire the dirty-rect repaint that consumes it. */
static int g_ai_ranking_dirty;   /* _DAT_0056d6a0 */
int  engine_army_ai_ranking_dirty(void)       { return g_ai_ranking_dirty; }
void engine_army_ai_set_ranking_dirty(int v)  { g_ai_ranking_dirty = v; }

/* Port of FUN_0049a581 (1039 B) — the per-tick AI "brain" FSM dispatcher,
 * reached from the strategic sub-step driver (engine_turn_tick_substate0,
 * phase 4) once per sub-step.  It does NOT loop over all realms: it advances a
 * persistent round-robin cursor (s_slot_0057c944, slot 84) by one in-game realm
 * per call and runs a single FSM transition on that realm, so the per-turn AI
 * work is amortised one state-per-tick across the whole human-interactive phase.
 *
 *   - round-robin: walk the cursor (1..5, wrapping) up to 5 times to find the
 *     next in-game realm (+0x4 != 0); if none, return.
 *   - state 0 → first-tick init: re-check elimination + recompute the player's
 *     ranking aggregate, clear the proposal gate (+0x1c), set state := 1.
 *   - human-owned (+0x5 != 0, quirk #190) or already parked (state >= 999) →
 *     return without AI work.
 *   - else dispatch the current state through the 14-task ladder (one task per
 *     tick), then state++.  After the ladder (states >= sel*2+15), once the
 *     realm's owned-army movement has drained (arm_movement_owned(1, sel) == 0)
 *     the realm is parked at the 999 "turn done" sentinel + the ranking redraw
 *     cookie is set.
 *
 * All fourteen brain steps (1..0xe) are now live ports — the per-tick dispatcher
 * is fully assembled, no advance-only stubs remain.  Step 8 is a no-op in retail
 * too (engine_army_ai_step_noop); the rest run their faithful per-realm logic.
 *
 * `evq` is the live turn event queue (steps 1/2/0xd + the init elimination
 * post); `mp_active` threads retail DAT_00553030 into the init elimination
 * check; `rng_7bit_b` is the freshly-stirred 7-bit RNG byte (retail
 * DAT_0058fd60) the step-1 alliance/tribute handlers roll against. */
void engine_army_ai_run_per_tick(engine_event_queue_state_t *evq, int mp_active,
                                 uint32_t rng_7bit_b)
{
    CALL_TRACE_ENTER(0x0049a581);

    /* Round-robin: advance the persistent cursor (1..5, wrapping) up to 5
     * times looking for the next in-game realm. */
    int sel = (int)s_slot_0057c944;
    for (int i = 1; i < 6; i++) {
        sel = (int)s_slot_0057c944;
        if (++s_slot_0057c944 >= 6)
            s_slot_0057c944 = 1;
        if (STRAT_REC(sel)[STRAT_ACTIVE] != 0)
            break;
    }

    uint8_t *rec = STRAT_REC(sel);
    if (rec[STRAT_ACTIVE] == 0)
        return;                                       /* no in-game realm found */

    int32_t *state = (int32_t *)(rec + STRAT_BRAIN_STATE);
    if (*state == 0) {
        /* First-tick init for this realm. */
        engine_ranking_check_elimination((unsigned)sel, evq, mp_active); /* 0x49b42b */
        engine_ranking_recompute_player_aggregate((unsigned)sel);        /* 0x49d1e0 */
        rec[STRAT_PROPOSAL_GATE] = 0;                 /* +0x1c byte */
        *state = 1;
    }

    if (rec[STRAT_ELIMINATED] != 0)
        return;                                       /* human-owned (quirk #190) */
    if (*state >= 0x3e7)
        return;                                       /* already parked (turn done) */

    switch (*state) {
    case 0x1:  engine_army_ai_step1_process_inbox(sel, evq, rng_7bit_b); break; /* 0x4a277d */
    case 0x2:  engine_army_ai_run_diplomacy(sel, evq);      break; /* 0x4a0c1d */
    case 0x3:  engine_army_ai_set_rations_and_grant(sel);   break; /* 0x49d638 */
    case 0x4:  engine_army_ai_seed_buy_requests(sel);       break; /* 0x49e1bf */
    case 0x5:  engine_army_ai_run_player(sel);              break; /* 0x49dd01 */
    case 0x6:  engine_army_ai_run_keep_builder(sel);       break; /* 0x49edc7 */
    case 0x7:  engine_army_ai_run_garrison(sel);            break; /* 0x49f93d */
    case 0x8:  engine_army_ai_step_noop();                  break; /* 0x49f96c */
    case 0x9:  engine_army_ai_run_campaign(sel);            break; /* 0x49f977 */
    case 0xa:  engine_army_ai_launch_attack(sel);           break; /* 0x4a0015 */
    case 0xb:  engine_army_ai_run_movement(sel, evq, mp_active); break; /* 0x4a5667 */
    case 0xc:  engine_army_ai_run_build_orders(sel);        break; /* 0x49e77d */
    case 0xd:  engine_army_ai_propose_diplomacy(sel, evq);  break; /* 0x4a13a6 */
    case 0xe:  engine_ranking_recompute_player_aggregate((unsigned)sel); break; /* 0x49d1e0 */
    default:   break;   /* states 15.. — idle ramp toward the terminal threshold */
    }

    /* Terminal check: once the state crosses the per-slot threshold (sel*2+15)
     * and the realm's owned-army movement has drained, park at the 999 sentinel
     * and flag the ranking redraw cookie. */
    if (*state >= sel * 2 + 0xf) {
        if (engine_army_state_arm_movement_owned(1, sel) == 0) {  /* 0x4a4e3d */
            *state = 0x3e7;
            g_ai_ranking_dirty = 1;                   /* _DAT_0056d6a0 */
        }
    }
    (*state)++;
}
