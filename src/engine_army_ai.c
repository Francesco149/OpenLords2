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
#include "engine_army_state.h"     /* arm_movement_owned (FUN_004a4e3d — brain terminal check) */
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
                           int audience, int code, uint8_t flag5, uint8_t *s)
{
    uint8_t has_data[7];
    build_has_data(has_data);

    int variant = ((int)s[STRAT_MOOD_CYCLE] - 4) + (int)s[STRAT_PERSONALITY] * 4;
    engine_event_queue_post(evq, player, audience, code, variant, flag5, 0, 0, 0,
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
            propose_notify(evq, player, other, 0xc1, 1, s);
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
        propose_notify(evq, player, target, 0xc0, 1, s);
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
            propose_notify(evq, player, lord, 0xb5, 1, s);
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
                    propose_notify(evq, player, c, 0xb4, 0xb, s);
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
 * target's (retail FUN_00404f4c, engine_path_chebyshev_distance — only the
 * distance is used, so it is inlined), then: +0x28 if the target is a defended
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

        int dx = home_col - r[ARMY_CAP_COL]; if (dx < 0) dx = -dx;
        int dy = home_row - r[ARMY_CAP_ROW]; if (dy < 0) dy = -dy;
        int score = (dx < dy) ? dy : dx;          /* chebyshev */

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
 * Score = chebyshev distance (FUN_00404f4c, inlined) + 0x28 for a defended keep
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

        int dx = home_col - r[ARMY_CAP_COL]; if (dx < 0) dx = -dx;
        int dy = home_row - r[ARMY_CAP_ROW]; if (dy < 0) dy = -dy;
        int score = (dx < dy) ? dy : dx;          /* chebyshev */

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

/* ── Per-tick AI brain dispatcher (FUN_0049a581) ──────────────────────────
 *
 * Ranking-changed redraw cookie — retail _DAT_0056d6a0, set to 1 by the brain
 * terminal arm when a realm is parked at the turn-done sentinel (the field
 * ranking has settled → repaint).  Runtime-only render flag (NOT save-state);
 * the port has no consumer yet, so it is a faithful write-only mirror.
 * TODO(render): wire the dirty-rect repaint that consumes it. */
static int g_ai_ranking_dirty;   /* _DAT_0056d6a0 */

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
 * Steps 1/6/7/9/0xb are advance-only stubs (CALL_TRACE_ENTER_STUB markers +
 * the TODO notes below): the FSM still advances through them so the cursor and
 * dispatch structure track retail exactly (closing the slot-84 divergence the
 * trace studio flagged), while their per-realm state writes — FUN_004a277d /
 * 0049edc7 / 0049f93d / 0049f977 / 004a5667 and their ~18 KB of callee trees —
 * land incrementally bottom-up.  Steps 2/3/4/5/8/0xa/0xc/0xd/0xe are live.
 *
 * `evq` is the live turn event queue (steps 2/0xd + the init elimination post);
 * `mp_active` threads retail DAT_00553030 into the init elimination check. */
void engine_army_ai_run_per_tick(engine_event_queue_state_t *evq, int mp_active)
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
    case 0x1:
        /* TODO(brain step 1): FUN_004a277d (565 B) + 8-fn / 5.6 KB closure
         * (4a29b2 4a2cd6 4a2f61 4a30f6 4a3475 4a3551 4a38db + 4a1b29).  Runs
         * first for every realm; advance-only until ported. */
        CALL_TRACE_ENTER_STUB(0x004a277d);
        break;
    case 0x2:  engine_army_ai_run_diplomacy(sel, evq);      break; /* 0x4a0c1d */
    case 0x3:  engine_army_ai_set_rations_and_grant(sel);   break; /* 0x49d638 */
    case 0x4:  engine_army_ai_seed_buy_requests(sel);       break; /* 0x49e1bf */
    case 0x5:  engine_army_ai_run_player(sel);              break; /* 0x49dd01 */
    case 0x6:
        /* TODO(brain step 6): FUN_0049edc7 (872 B) + 436d02 (768 B) + 46841c. */
        CALL_TRACE_ENTER_STUB(0x0049edc7);
        break;
    case 0x7:
        /* TODO(brain step 7): FUN_0049f93d (47 B) + 49f12f/49f431 (~770 B ea.)
         * + 4a65a3/49f73f/49f850/4a5389/4a545f. */
        CALL_TRACE_ENTER_STUB(0x0049f93d);
        break;
    case 0x8:  engine_army_ai_step_noop();                  break; /* 0x49f96c */
    case 0x9:
        /* TODO(brain step 9): FUN_0049f977 (846 B) + 49fda5/4a65a3/4a6735/
         * 4a01ea/49fcc5/49ff71. */
        CALL_TRACE_ENTER_STUB(0x0049f977);
        break;
    case 0xa:  engine_army_ai_launch_attack(sel);           break; /* 0x4a0015 */
    case 0xb:
        /* TODO(brain step 0xb): FUN_004a5667 (325 B) + 14-fn / 6.3 KB closure
         * (the army-movement/deployment planner — pulls in path_field). */
        CALL_TRACE_ENTER_STUB(0x004a5667);
        break;
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
