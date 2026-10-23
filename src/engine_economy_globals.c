/*
 * OpenLords2 — the economy / UI-action passes' runtime scratch retail keeps in
 * .data (not save state): the caravan staging order, the trade grid's figures,
 * the diplomacy action's mode and latch, the Send Supplies target, the castle
 * tab's tier.  Each is one retail
 * global, named by its VA in the comment; the owning module's header declares
 * it.
 *
 * Own translation unit, like engine_realm_globals.c: the hybrid host
 * (tools/rhost) leaves this file out and binds every symbol to retail's own
 * address instead (rh_hooks_economy.c), so port code linked into rhost reads
 * and writes retail's memory.
 */

#include <stdint.h>

#include "engine_army_ai.h"
#include "engine_trade.h"
#include "engine_diplomacy_action.h"
#include "engine_sendsupplies.h"

/* engine_army_ai.c — the per-player goods-shipment staging order (stride 0x18) */
int32_t g_caravan_order[8][6];   /* DAT_005678c0 */

/* engine_trade_model.c — the trade grid's figures (FUN_00428daf and the arrows) */
int32_t g_trade_amount;          /* DAT_0053e9e4 — -(how much of the good you hold) */
int32_t g_trade_offer;           /* DAT_0057c8c0 — the merchant's per-good offer */
int32_t g_trade_max;             /* DAT_0055cd70 — offer + county markup */
int32_t g_trade_afford;          /* DAT_0057a0fc — gold / max (affordable qty) */
int32_t g_trade_net;             /* DAT_00554170 — running buy(+)/sell(-) amount */
int32_t g_trade_total;           /* DAT_00553f64 — running total cost / receipt */
int32_t g_trade_ale;             /* DAT_0053e8c0 — the ale-gift delta (good 4) */
int32_t g_trade_category;        /* DAT_00568948 — 0 food / 1 raw / 2 weapon */
int32_t g_trade_error;           /* DAT_0057c994 — 0 none / 1 no-crowns / -1 no-goods */

/* engine_diplomacy_action_model.c — the diplomacy-action screen */
int32_t g_diplo_action_mode;     /* DAT_005651cc — the action mode (0 gift .. 6) */
int32_t g_diplo_action_taken;    /* DAT_0052afa4 — an action was issued (the end-turn pass reads it) */

/* engine_sendsupplies_model.c — the shipment's destination; the county-action tabs */
int32_t g_send_target;           /* DAT_00553c78 */
int32_t g_castle_tier;           /* DAT_0056d898 — the castle screen's selected tier */
