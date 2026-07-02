/*
 * OpenLords2 — the merchant trade grid (screen 0x0c).
 *
 * The buy/sell face of the merchant county-interior.  Reached from the
 * merchant scene (screen 8, engine_merchant.c) by clicking a good's zone
 * on the market rug: retail's FUN_004357a6 indexes the SAME baked region
 * grid the labor screen uses (engine_labor_region_grid.h, DAT_00542cf8)
 * to turn the click into a good id (DAT_0056d8ac, 1..14), then FUN_0043530e
 * flips to screen 0x0c and paints this grid.
 *
 * Retail address map:
 *   FUN_00416308 @ 0x416308 ( 87 B) → engine_trade_render (entry: compute
 *                                     state, load merchant.pl8 backdrop +
 *                                     icontrad.pl8 icon sheet, draw content)
 *   FUN_00428daf @ 0x428daf (801 B) → engine_trade_compute (the per-good
 *                                     trade figures: how much you have, the
 *                                     merchant's offer, the affordable qty)
 *   FUN_0041635f @ 0x41635f (1478 B)→ (static) the content draw
 *
 * NO palette load: FUN_00416308 does NOT swap the palette (unlike the
 * merchant scene), so the trade grid inherits merchant.256 — which is
 * always active because this screen is only ever reached FROM the merchant.
 *
 * Trade FIGURES are all read from SAVED state — there is no runtime price
 * model to port.  The merchant's per-good offer table is save slot 82
 * (s_slot_005532a0, stride 0x10); the player's resource stocks are the
 * per-player strategic record; the county's grain/cattle are the county
 * record.  The compute is a handful of reads + a percentage.
 *
 * The interactive part (the up/down buy/sell arrows in the router table
 * DAT_004dd838, FUN_004ba9c8 case 0x0c) is DEFERRED — this ports the
 * RENDER.  On a fresh open the trade UI state is all zero (buy mode, net
 * 0, no error), which is the path the arrows later drive.
 *
 * Per-record field offsets (all read-only here):
 *   county  (g_per_army_768,        stride 0x300, key = s_slot_0057c93c)
 *     +0x224 i32 grain   (good 1)      +0x250 i32 cattle herd (good 2)
 *   player  (g_player_strategic_352, stride 0x160, key = s_slot_0057c8cc)
 *     +0x118 i32 gold (crowns)
 *     +0x120 iron?  +0x128 stone  +0x130 timber   (goods 6/7/8)
 *     +0x140 maces  +0x144 wool?  +0x148 swords   (goods 0xc/0xb/0xd)
 *     +0x14c pikes  +0x150 bows   +0x154 mail     (goods 9/10/0xe)
 *     (the per-good stock; the exact weapon labels are grp-6 names)
 *   territory (g_per_territory_420,  stride 0x1a4, key = trade_county)
 *     +0x166 u8  trade quantity percentage (merchant markup)
 */

#ifndef OPENLORDS2_ENGINE_TRADE_H
#define OPENLORDS2_ENGINE_TRADE_H

#include <stdint.h>

#include "engine_button.h"
#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + the gfx
 * loaders (same NULL-guard convention as engine_labor / engine_county_
 * panels: a NULL pointer skips the draw that needs it). */
typedef struct engine_trade_ctx {
    const uint8_t *county_block;       /* g_per_army_768 (DAT_0053f9b0) */
    const uint8_t *strategic_block;    /* g_player_strategic_352 (0x57bf00) */
    const uint8_t *territory_block;    /* g_per_territory_420 (DAT_0052f0b0) */
    const uint8_t *merchant_offers;    /* s_slot_005532a0 (slot 82, stride 0x10) */

    int county;                        /* s_slot_0057c93c — current county key */
    int trade_county;                  /* DAT_00553c64 — the merchant's county */
    int local_player;                  /* s_slot_0057c8cc */
    int good;                          /* s_slot_0056d8ac (DAT_0056d8ac), 1..14 */

    const uint8_t *chrome;             /* panels.pl8 (gfx_panel_draw sheet) */
    const uint8_t *system_sheet;       /* system2.pl8 (continue-seal) */
    const uint8_t *title_font;         /* fntl2_22 (DAT_005b2fa0) */
    const uint8_t *menu_font;          /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
} engine_trade_ctx_t;

/* ── FUN_0043530e — the merchant→grid transition's UI reset ─────────────
 * Resets buy/sell mode + the error notice to their neutral (buy) state.
 * Called when a merchant good-zone click opens the grid. */
void engine_trade_on_open(void);

/* ── FUN_00428daf — compute the per-good trade figures ──────────────────
 * Reads the ctx's saved state, writes the module-static trade figures
 * (read back by the render + the future buy/sell arms).  Also resets the
 * transient trade UI state (net = 0, etc.) — retail does this on every
 * (re)entry to the grid. */
void engine_trade_compute(const engine_trade_ctx_t *ctx);

/* ── FUN_00416308 — render the trade grid (screen 0x0c) ─────────────────
 * compute → merchant.pl8 backdrop → icontrad.pl8 icon sheet → content.
 * Paints over the whole frame (opaque backdrop); the caller locks the
 * canvas.  Does NOT touch the palette (inherits merchant.256). */
void engine_trade_render(const engine_trade_ctx_t *ctx);

/* ── FUN_004ba26e case 0x0c — the per-frame grid refresh ─────────────────
 * Redraws the content + arrow strip over the retained frame with NO compute
 * / NO backdrop reload, so the arrows' running net/total and the button
 * press-flash animate.  Called every tick while screen 0x0c is open (the
 * unconditional per-frame render seam), alongside engine_trade_render (which
 * runs only on the open/demand redraw). */
void engine_trade_refresh(const engine_trade_ctx_t *ctx);

/* ── Buy/sell interaction (router table DAT_004dd838, FUN_004ba9c8 case 0xc) ──
 * The up/down/buy-all/sell-all arrows drive the running net (DAT_00554170),
 * total (DAT_00553f64), buy/sell mode (DAT_0053e9bc), and error notice
 * (DAT_0057c994); the "Complete?" seal commits.  Dispatched by the input
 * seam (engine.c ti_ui_router) over these records via
 * engine_menu_button_dispatch, then re-rendered by engine_trade_render.
 *
 * engine_trade_buttons()      the mutable 6-record widget table (shared with
 *                             the renderer; index 4/5 are the sell-mode seal).
 * engine_trade_widget_count() the live count (4 buy-neutral / 6 with a net). */
engine_button_rec_t *engine_trade_buttons(void);
int  engine_trade_widget_count(void);

void engine_trade_arrow_up(void);     /* FUN_00435339 */
void engine_trade_arrow_down(void);   /* FUN_0043543d */
void engine_trade_buy_all(void);      /* FUN_004355db */
void engine_trade_sell_all(void);     /* FUN_00435541 */
void engine_trade_confirm(void);      /* FUN_00435286 (commit; caller flips screen) */

#endif /* OPENLORDS2_ENGINE_TRADE_H */
