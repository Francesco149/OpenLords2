/*
 * OpenLords2 — the merchant trade grid's MODEL (screen 0x0c): the per-good
 * figures, the buy/sell arrows and the commit.  The render (the content draw,
 * the open render, the per-frame refresh) is engine_trade.c.
 *
 * Split from engine_trade.c so the hybrid host (tools/rhost) links the model
 * without the gfx / io closure.  The figures are retail globals
 * (engine_economy_globals.c; rhost aliases them at retail's VAs).
 *
 * Retail address map:
 *   FUN_0043530e @ 0x43530e ( 43 B) → engine_trade_on_open (the UI reset; the
 *                                     screen flip + open render are the caller's);
 *                                     engine_trade_open in retail layout
 *   FUN_004357a6 @ 0x4357a6 (132 B) → engine_merchant_zone_click (retail layout)
 *   FUN_00428daf @ 0x428daf (801 B) → engine_trade_compute / _compute_current
 *   FUN_00435673 @ 0x435673 (296 B) → engine_trade_ale_delta_recompute
 *   FUN_00435339 @ 0x435339 (260 B) → engine_trade_arrow_up
 *   FUN_0043543d @ 0x43543d (260 B) → engine_trade_arrow_down
 *   FUN_004355db @ 0x4355db (152 B) → engine_trade_buy_all
 *   FUN_00435541 @ 0x435541 (154 B) → engine_trade_sell_all
 *   FUN_00435286 @ 0x435286 (108 B) → engine_trade_confirm (the caller flips screen);
 *                                     engine_trade_seal_confirm in retail layout
 *   FUN_004352f2 @ 0x4352f2 ( 28 B) → engine_trade_seal_cancel
 */

#include "engine_trade.h"

#include <stdint.h>

#include "engine.h"                 /* g_sim_mp_active (DAT_00553030) */
#include "engine_army_ai.h"         /* engine_army_ai_execute_trade (FUN_004284ce) */
#include "engine_combat.h"          /* g_combat_audio_latch (DAT_005533f0) */
#include "engine_menu_input.h"      /* engine_menu_button_accel (DAT_00591554) */
#include "save.h"                   /* g_per_army_768 / g_player_strategic_352 / g_per_territory_420 */
#include "save_schema_globals.h"    /* s_slot_0053e9bc / 0056d8ac / 0057c8cc / 0057c93c / 00553c64 / 005532a0 */

#define ARMY_REC(base, key)  ((base) + (size_t)(key) * 0x300)   /* g_per_army_768 */
#define STRAT_REC(base, key) ((base) + (size_t)(key) * 0x160)   /* strategic */
#define TERR_REC(base, key)  ((base) + (size_t)(key) * 0x1a4)   /* territory */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

/* One-shot: engine_trade_compute (FUN_00428daf) resets the running net/total,
 * so — like retail — it runs ONLY on the open render (FUN_00416308), never in
 * the per-frame refresh (FUN_004ba26e).  Set on open, cleared by the first
 * full render; guards a purchase-in-progress against a stray demand-redraw.
 * Port-only (no retail counterpart). */
int g_trade_needs_compute;

/* The arrows' 0 → positive net cue (0x427990; default: silent). */
static void (*g_trade_wav_fn)(const char *, int, int);
void engine_trade_bind_wav_fn(void (*fn)(const char *, int, int)) { g_trade_wav_fn = fn; }
static void trade_cue(const char *name)
{
    g_combat_audio_latch = 1;                       /* DAT_005533f0 */
    if (g_trade_wav_fn) g_trade_wav_fn(name, 1, 0);
}

void engine_trade_on_open(void)   /* FUN_0043530e's DAT_0053e9bc/0057c994 = 0 */
{
    s_slot_0053e9bc = 0;
    g_trade_error = 0;
    g_trade_needs_compute = 1;
}

/* The open render FUN_00416308 (default: none — engine.c renders on the screen flip). */
static void (*g_trade_open_render_fn)(void);
void engine_trade_bind_open_render_fn(void (*fn)(void)) { g_trade_open_render_fn = fn; }

/* ── FUN_0043530e in retail layout — open the trade grid: the UI reset, screen
 * 0x0c, the open render ───────────────────────────────────────────────────── */
void engine_trade_open(void)
{
    engine_trade_on_open();
    engine_set_current_screen(0x0c);
    if (g_trade_open_render_fn) g_trade_open_render_fn();   /* FUN_00416308 */
}

/* ── FUN_004357a6 in retail layout — the merchant scene's rug: the good under
 * the cursor (`grid` = the mercgrid.pl8 payload, 0x50 cells a row of 8×8 px) is
 * latched EVERY call (DAT_0056d8ac); an LMB press on a good opens the grid.
 * Returns 1 when it opened. ──────────────────────────────────────────────── */
int engine_merchant_zone_click(const uint8_t *grid, int cursor_x, int cursor_y, int lmb_pressed)
{
    s_slot_0056d8ac = grid[(cursor_y / 8) * 0x50 + cursor_x / 8];
    if (s_slot_0056d8ac == 0) return 0;
    if (lmb_pressed == 0) return 0;
    engine_trade_open();
    return 1;
}

/* ── FUN_00428daf — compute the per-good trade figures ────────────────── */
void engine_trade_compute(const engine_trade_ctx_t *ctx)
{
    int good = ctx->good;

    g_trade_net = 0;      /* DAT_00554170 */
    g_trade_total = 0;    /* DAT_00553f64 */
    g_trade_ale = 0;      /* DAT_0053e8c0 */

    /* Category by good range (DAT_00568948): food 1..5, raw 6..8, weapon 9+. */
    if (good < 6)      g_trade_category = 0;
    else if (good < 9) g_trade_category = 1;
    else               g_trade_category = 2;

    /* How much of the good you currently hold (DAT_0053e9e4), then NEGATE
     * it (retail line 58) — the content negates it back when drawing. */
    const uint8_t *cty  = ARMY_REC(ctx->county_block, ctx->county);
    const uint8_t *strat = STRAT_REC(ctx->strategic_block, ctx->local_player);
    int amount;
    switch (good) {
    case 1:    amount = rec_i32(cty,  0x224); break;   /* grain  (county) */
    case 2:    amount = rec_i32(cty,  0x250); break;   /* cattle (county) */
    case 4:    amount = 0;                    break;   /* ale — no stock */
    case 6:    amount = rec_i32(strat, 0x120); break;  /* iron  */
    case 7:    amount = rec_i32(strat, 0x128); break;  /* stone */
    case 8:    amount = rec_i32(strat, 0x130); break;  /* timber */
    case 9:    amount = rec_i32(strat, 0x14c); break;  /* pikes */
    case 10:   amount = rec_i32(strat, 0x150); break;  /* bows  */
    case 0xb:  amount = rec_i32(strat, 0x144); break;  /* maces */
    case 0xc:  amount = rec_i32(strat, 0x140); break;  /* crossbows */
    case 0xd:  amount = rec_i32(strat, 0x148); break;  /* swords */
    case 0xe:  amount = rec_i32(strat, 0x154); break;  /* mail  */
    default:   amount = g_trade_amount;       break;   /* goods 3 / 5: retail stores nothing, so
                                                          the negate flips the previous value */
    }
    g_trade_amount = -amount;

    /* The merchant's offer for this good (saved slot 82), then the county
     * markup (a percentage of the offer, min 1, 0 for ale) → the max
     * tradeable quantity, and how many you can afford. */
    int offer = rec_i32(ctx->merchant_offers, good * 0x10);   /* s_slot_005532a0 */
    g_trade_offer = offer;

    const uint8_t *terr = TERR_REC(ctx->territory_block, ctx->trade_county);
    int pct = terr[0x166];                          /* DAT_0052f216 (byte) */
    int markup = (pct * offer) / 100;               /* FUN_00404d6b */
    if (markup < 1) markup = 1;
    if (good == 4) markup = 0;
    g_trade_max = markup + offer;                   /* DAT_0055cd70 */

    int gold = rec_i32(strat, 0x118);               /* DAT_0057c018 */
    g_trade_afford = (g_trade_max == 0) ? 0 : gold / g_trade_max;   /* DAT_0057a0fc */
}

/* FUN_00428daf in retail layout: the figures of the current good / county / player */
void engine_trade_compute_current(void)
{
    engine_trade_ctx_t ctx = {
        .county_block = g_per_army_768, .strategic_block = g_player_strategic_352,
        .territory_block = g_per_territory_420, .merchant_offers = (const uint8_t *)s_slot_005532a0,
        .county = (int)s_slot_0057c93c, .trade_county = (int)s_slot_00553c64,
        .local_player = (int)s_slot_0057c8cc, .good = (int)s_slot_0056d8ac,
    };
    engine_trade_compute(&ctx);
}

/* ── FUN_00435673 — recompute the ale-gift happiness delta ──────────────
 * Runs after every arrow.  Tiers the running total (0..5) against a tenth of
 * the county's field +0x24, then clamps to the happiness headroom
 * (5 − county[+0x219]).  Computed for every good; only DRAWN for good 4
 * (ale), where trade_content shows it as the "(±N☺)" gift preview. */
void engine_trade_ale_delta_recompute(void)
{
    const uint8_t *cty = g_per_army_768 + (size_t)s_slot_0057c93c * 0x300;

    g_trade_ale = 0;                                /* DAT_0053e8c0 */
    if (g_trade_total != 0) {
        int step = rec_i32(cty, 0x24) / 10;         /* DAT_0053f9d4 = base+0x24 */
        int t = g_trade_total;                      /* DAT_00553f64 */
        if      (t >= step * 5) g_trade_ale = 5;
        else if (t >= step * 4) g_trade_ale = 4;
        else if (t >= step * 3) g_trade_ale = 3;
        else if (t >= step * 2) g_trade_ale = 2;
        else if (t >= step)     g_trade_ale = 1;
        else                    g_trade_ale = 0;
    }
    int headroom = 5 - cty[0x219];                  /* DAT_0053fbc9 (byte) */
    if (headroom < g_trade_ale) {
        g_trade_ale = headroom;
    }
}

/* The arrows' shared tail: the running total from the net (sell at the
 * merchant offer, buy at the county-marked max), the mode, the 0 → positive
 * cue, the ale delta. */
static void trade_arrow_tail(int prev_net, const char *cue)
{
    g_trade_total = (g_trade_net >= 0) ? g_trade_net * g_trade_max      /* DAT_00553f64 */
                                       : -(g_trade_offer * g_trade_net);
    s_slot_0053e9bc = (uint32_t)(g_trade_net != 0);                     /* DAT_0053e9bc */
    if (g_trade_net > 0 && prev_net <= 0) trade_cue(cue);
    engine_trade_ale_delta_recompute();                                 /* FUN_00435673 */
}

/* ── FUN_00435339 — up arrow: raise the net by the accel step ───────────
 * Steps by 1 until the press-repeat accel (DAT_00591554) reaches 0x2c, then
 * by 10; clamps to the affordable quantity (error 1 = "not enough crowns"). */
void engine_trade_arrow_up(void)
{
    int prev = g_trade_net;
    g_trade_error = 0;                                          /* DAT_0057c994 */
    g_trade_net += (engine_menu_button_accel() < 0x2c) ? 1 : 10;
    if (g_trade_afford < g_trade_net) {
        g_trade_net = g_trade_afford;
        if (g_trade_afford == 0) {
            g_trade_error = 1;
        }
    }
    trade_arrow_tail(prev, "s068_01.wav");
}

/* ── FUN_0043543d — down arrow: lower the net by the accel step ──────────
 * Clamps to the amount held (negated); error −1 = "no goods to sell". */
void engine_trade_arrow_down(void)
{
    int prev = g_trade_net;
    g_trade_error = 0;
    g_trade_net -= (engine_menu_button_accel() < 0x2c) ? 1 : 10;
    if (g_trade_net < g_trade_amount) {
        g_trade_net = g_trade_amount;
        if (g_trade_amount == 0) {
            g_trade_error = -1;
        }
    }
    trade_arrow_tail(prev, "s068_02.wav");
}

/* ── FUN_004355db — "buy all": net = the affordable quantity (total at the
 * max price whatever its sign) ─────────────────────────────────────────── */
void engine_trade_buy_all(void)
{
    int prev = g_trade_net;
    g_trade_error = 0;
    g_trade_net = g_trade_afford;
    g_trade_total = g_trade_afford * g_trade_max;
    s_slot_0053e9bc = (uint32_t)(g_trade_afford != 0);
    if (g_trade_afford > 0 && prev <= 0) trade_cue("s068_01.wav");
    engine_trade_ale_delta_recompute();
}

/* ── FUN_00435541 — "sell all": net = the amount held, negated (total at the
 * offer whatever its sign) ─────────────────────────────────────────────── */
void engine_trade_sell_all(void)
{
    int prev = g_trade_net;
    g_trade_error = 0;
    g_trade_net = g_trade_amount;
    g_trade_total = -(g_trade_offer * g_trade_amount);
    s_slot_0053e9bc = (uint32_t)(g_trade_amount != 0);
    if (g_trade_amount > 0 && prev <= 0) trade_cue("s068_02.wav");
    engine_trade_ale_delta_recompute();
}

/* ── FUN_00435286 — commit the transaction (the "Complete?" seal) ───────
 * Single-player applies the buy/sell directly through the shared executor
 * (FUN_004284ce, the same the AI trades through); the caller then returns to
 * the merchant scene.  The multiplayer send (0x43eda0(0x28,0)) is
 * deferred.  Returns nothing — the screen flip is the input hook's job. */
void engine_trade_confirm(void)
{
    if (g_sim_mp_active == 0) {
        engine_army_ai_execute_trade((int)s_slot_0053e9bc, g_trade_net,
                                     (int)s_slot_0056d8ac, g_trade_max,
                                     g_trade_offer, (int)s_slot_0057c8cc,
                                     (int)s_slot_0057c93c);     /* FUN_004284ce */
    }
    /* else: MP command 0x43eda0(0x28,0) — deferred. */
}

/* ── FUN_00435286 / FUN_004352f2 in retail layout — the "Complete?" seal and
 * the cancel seal: commit (single player), then back to the merchant scene
 * (DAT_004eac50 = 8) with a full demand redraw (DAT_0057d340 = 2).  The MP
 * send 0x43eda0(0x28, 0) is deferred (rhost: DirectPlay is stubbed). */
void engine_trade_seal_confirm(void)
{
    engine_trade_confirm();
    engine_trade_seal_cancel();
}

void engine_trade_seal_cancel(void)
{
    engine_set_current_screen(8);
    engine_set_demand_redraw(2);
}
