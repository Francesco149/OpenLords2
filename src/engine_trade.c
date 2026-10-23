/*
 * OpenLords2 — the merchant trade grid (screen 0x0c).  See engine_trade.h
 * for the retail address map, the per-record field offsets, and the scope
 * (RENDER; the buy/sell arrows are deferred).
 *
 * The model (the figures, the arrows, the commit) is engine_trade_model.c.
 *
 * Every draw mirrors its decompile line 1:1 (coordinates, fonts, colors,
 * and the line-x-accumulator chaining).  Record offsets are kept RAW with
 * the retail DAT_ base cited; the one .rdata table is r2-extracted
 * byte-exact.
 */

#include "engine_trade.h"

#include <stdint.h>
#include <stdlib.h>

#include "engine_button.h"          /* engine_button_render_list (FUN_0040cfd2) */
#include "engine_dirty_rect.h"      /* (dirty marks are seam-owned; see notes) */
#include "gfx.h"                    /* load_background_image, lock/unlock canvas */
#include "gfx_line.h"               /* gfx_line_rect_bevel_dark_tl / _br */
#include "gfx_panel.h"              /* gfx_panel_draw_bevel / _fill_interior */
#include "gfx_sprite_render.h"      /* gfx_sprite_render[_set_blob], seal */
#include "gfx_text.h"               /* line-x acc + uppercase highlight */
#include "gfx_text_label.h"         /* gfx_text_label_render_dec */
#include "gfx_wrap.h"               /* gfx_wrap_localized_line / _localized */
#include "io.h"                     /* io_read_file_at (FUN_004af8d9) */
#include "io_path.h"                /* io_path_chdir_base (FUN_004af502) */
#include "save.h"                   /* g_per_army_768 (DAT_0053f9b0) */
#include "save_schema_globals.h"    /* s_slot_0056d8ac / 0057c8cc / 0057c93c */

#define STRAT_REC(base, key) ((base) + (size_t)(key) * 0x160)   /* strategic */

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

/* ── icontrad.pl8 icon-sheet scratch (retail's shared DAT_004eabec) ─────
 * A persistent 160000-byte buffer, reloaded on each render — the same
 * pattern engine_labor.c uses for iconvill.pl8.  (Labor + trade never
 * render the same frame, but each keeps its own buffer for clarity.) */
#define TRADE_SCRATCH_LEN 160000
static uint8_t *g_trade_scratch;
static int trade_ensure_scratch(void)
{
    if (!g_trade_scratch) {
        g_trade_scratch = malloc(TRADE_SCRATCH_LEN);
    }
    return g_trade_scratch != NULL;
}

/* The trade UI figures (g_trade_*: retail globals, engine_economy_globals.c)
 * are computed and driven by the model (engine_trade_model.c: FUN_00428daf
 * and the arrows); the content draw reads them back.  The buy/sell MODE is
 * retail's DAT_0053e9bc — a shared save-schema scalar (slot 80), so it uses
 * the s_slot_ extern (never a private copy) to keep the state oracle + any
 * save in sync ([[project_shared_save_globals]]). */
static int g_trade_widget_count;  /* DAT_00553f58 — router table entry count */

/* ── FUN_0041ab67 — decimal value + its grp-8 unit noun ─────────────────
 * (singular sub for ±1, else plural sub+1).  Preserves the caller's
 * line-x accumulator around the pair.  Identical to labor_num_units, but
 * bound to this screen's ctx. */
static void trade_num_units(const engine_trade_ctx_t *ctx, int value,
                            int unit_sub, int x, int y,
                            const uint8_t *font, uint8_t color)
{
    int saved = gfx_text_line_x_acc();
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(value, '@', "", x, y,
                              font, ctx->glyph_table, color);
    int sub = (value == 1 || value == -1) ? unit_sub : unit_sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, sub,
                            x + gfx_text_line_x_acc(), y,
                            font, ctx->glyph_table, color);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* Convenience: one localized single-line render bound to the ctx. */
static void trade_loc(const engine_trade_ctx_t *ctx, int grp, int sub,
                      int x, int y, const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line(ctx->loc, grp, sub, x, y,
                            font, ctx->glyph_table, color);
}

/* The trade grid's up/down buy/sell arrow widgets — retail's button table
 * DAT_004dd838, r2-extracted byte-exact.  Painted every frame by the
 * per-frame refresh (FUN_004ba26e case 0x0c: FUN_0040cfd2(0x30,0x50,
 * &DAT_004dd838, DAT_00553f58)).  Buy mode shows the first 4 (up / down /
 * buy-all / sell-all, kind 0x18 = 24×24 system sprites); sell mode adds
 * two more (kind 0x20 = 32×32).  The action field is the retail callee
 * address, dispatched by trade_button_action (engine.c); the renderer reads
 * only x/y/sprite/kind/state/type.  MUTABLE like retail's .data table —
 * engine_menu_button_dispatch animates state/pressed/repeat in place and the
 * renderer reads them back for the press flash (both walk THIS one array). */
static engine_button_rec_t k_trade_buttons[6] = {
    { 0x60, 0x88, 0x15, 0x18, 0x435339, 0, 0, 0, 4, 0, 0 },
    { 0x78, 0x88, 0x17, 0x18, 0x43543d, 0, 0, 0, 4, 0, 0 },
    { 0x98, 0x88, 0x48, 0x18, 0x4355db, 0, 0, 0, 4, 0, 0 },
    { 0xb0, 0x88, 0x46, 0x18, 0x435541, 0, 0, 0, 4, 0, 0 },
    { 0x110, 0xa8, 0x1d, 0x20, 0x435286, 0, 0, 0, 4, 0, 0 },
    { 0x138, 0xa8, 0x1f, 0x20, 0x4352f2, 0, 0, 0, 4, 0, 0 },
};

engine_button_rec_t *engine_trade_buttons(void) { return k_trade_buttons; }
int engine_trade_widget_count(void) { return g_trade_widget_count; }

/* iVar1 per good: DAT_004d2b80 first-int, stride 0x14, index good-1
 * (r2-extracted byte-exact).  The secondary icon drawn beside the "and N"
 * holdings line; 0 for sheep/ale/wool → no icon. */
static const int k_trade_good_icon[14] =
    { 0x0e, 0x0f, 0, 0, 0, 0x10, 0x11, 0x12, 0x16, 0x17, 0x14, 0x13, 0x15, 0x18 };

/* ── FUN_0041635f — the trade-grid content draw ─────────────────────────
 * dirty-rect marks (FUN_004527a6) are OMITTED — seam-owned, as in
 * engine_labor / engine_county_panels (the port repaints the full frame,
 * so per-widget marks are inert for pixels). */
static void trade_content(const engine_trade_ctx_t *ctx)
{
    int good = ctx->good;
    const uint8_t *tf = ctx->title_font;
    const uint8_t *mf = ctx->menu_font;
    const uint8_t *strat = STRAT_REC(ctx->strategic_block, ctx->local_player);

    /* FUN_004093e0(0x30,0x40,0x22,0x10) — the raised bevel panel. */
    if (ctx->chrome) {
        gfx_panel_draw_bevel(ctx->chrome, 0x30, 0x40, 0x22, 0x10);
    }
    /* FUN_0040d1bc(0x22c,0x114,0) — the "click to continue" seal (mode 0). */
    if (ctx->system_sheet) {
        gfx_sprite_seal_draw(ctx->system_sheet, 0x22c, 0x114, 0);
    }
    /* FUN_00403fdd(0x44,0x66,0x32,0x32) — the good-icon frame. */
    gfx_line_rect_bevel_dark_tl(0x44, 0x66, 0x32, 0x32);
    /* FUN_0040a682(good-1,0x45,0x67) — the good icon from icontrad.pl8. */
    gfx_sprite_render_set_blob(g_trade_scratch);
    gfx_sprite_render(good - 1, 0x45, 0x67);

    gfx_text_set_uppercase_highlight(1);     /* DAT_0058fe2c = 1 */
    gfx_text_set_line_x_acc(0);              /* DAT_005cd404 = 0 */

    /* Header verb (grp 0x44): buy → "Trading", sell → "Selling"/"Buying". */
    if (s_slot_0053e9bc == 0) {
        trade_loc(ctx, 0x44, 2, 0x88, 0x68, tf, 0x3f);          /* Trading */
    } else if (g_trade_net < 0) {
        trade_loc(ctx, 0x44, 6, 0x88, 0x68, tf, 0x3f);          /* Selling */
    } else {
        trade_loc(ctx, 0x44, 5, 0x88, 0x68, tf, 0x3f);          /* Buying */
    }
    /* The good name (grp 6, 1-indexed) chained past the verb. */
    trade_loc(ctx, 6, good, gfx_text_line_x_acc() + 0x88, 0x68, tf, 0x3f);

    /* Ale gift delta (good 4) — DEFERRED: g_trade_ale is always 0 on the
     * ported (fresh-open) path, so FUN_0041ac95 never fires here.  Drawing
     * it would need the county-panels delta-label ctx; faithfully omitted
     * until the ale buy arm lands. */

    gfx_text_set_line_x_acc(0);

    /* Sell-mode price line (buy mode skips it). */
    if (s_slot_0053e9bc != 0) {
        if (g_trade_net < 1) {
            trade_loc(ctx, 0x44, 0xa, 0x88, 0x88, mf, 0x3f);    /* Selling price */
            trade_num_units(ctx, g_trade_offer, 0,
                            gfx_text_line_x_acc() + 0x88, 0x88, mf, 0x3f);
        } else {
            trade_loc(ctx, 0x44, 9, 0x88, 0x88, mf, 0x3f);      /* Buying price */
            trade_num_units(ctx, g_trade_max, 0,
                            gfx_text_line_x_acc() + 0x88, 0x88, mf, 0x3f);
        }
    }

    gfx_text_set_line_x_acc(0);

    /* "You have." + your gold, then "and" + the good holdings + its icon,
     * then "my Lord." — the holdings readout line. */
    trade_loc(ctx, 0x44, 0xc, 0x58, 0xb0, mf, 0x3f);            /* You have. */
    trade_num_units(ctx, rec_i32(strat, 0x118), 0,             /* gold + Crowns */
                    gfx_text_line_x_acc() + 0x58, 0xb0, mf, 0x3f);

    int icon = k_trade_good_icon[good - 1];
    if (icon != 0) {
        trade_loc(ctx, 0x44, 0xb, gfx_text_line_x_acc() + 0x58, 0xb0, mf, 0x3f);  /* and */
        gfx_text_label_render_dec(-g_trade_amount, '@', "",
                                  gfx_text_line_x_acc() + 0x58, 0xb0,
                                  mf, ctx->glyph_table, 0x3f);
        gfx_sprite_render_set_blob(g_trade_scratch);
        gfx_sprite_render(icon, gfx_text_line_x_acc() + 0x58, 0xaa);
        gfx_text_set_line_x_acc(gfx_text_line_x_acc() + 0x24);
    }
    trade_loc(ctx, 0x44, 0xd, gfx_text_line_x_acc() + 0x58, 0xb0, mf, 0x3f);      /* my Lord. */

    gfx_text_set_uppercase_highlight(0);     /* DAT_0058fe2c = 0 */

    /* The lower prompt box: parchment interior + bottom bevel. */
    if (ctx->chrome) {
        gfx_panel_fill_interior(ctx->chrome, 0x50, 0xd0, 0x1d, 5);   /* FUN_00409c93 */
    }
    gfx_line_rect_bevel_dark_br(0x50, 0xd0, 0x1d0, 0x4c);            /* FUN_00403deb */

    if (s_slot_0053e9bc == 0) {
        /* Buy mode — the router has 4 arrow widgets; a wrapped prompt. */
        g_trade_widget_count = 4;                                   /* DAT_00553f58 */
        int sub;
        if (g_trade_error == 1)       sub = 0x12;   /* not enough crowns */
        else if (g_trade_error == -1) sub = 0x10;   /* no goods to sell */
        else if (good == 4)           sub = 0x13;   /* Buy ale for your county */
        else                          sub = 0x11;   /* use the up/down arrows */
        gfx_wrap_localized(ctx->loc, 0x44, sub, 0x60, 0xf6,
                           0x1c0, 100, 0, 0,
                           mf, ctx->glyph_table, ctx->glyph_metrics, 0x3f);
    } else {
        /* Sell mode — 6 widgets; "Complete this Purchase?/Sale? N" + total. */
        g_trade_widget_count = 6;
        gfx_text_set_line_x_acc(0);
        trade_loc(ctx, 0x44, 7, 0x60, 0x100, mf, 0x3f);            /* Complete this */
        if (g_trade_net < 0) {
            trade_loc(ctx, 0x44, 0xf, gfx_text_line_x_acc() + 0x60, 0x100, mf, 0xfc);  /* Sale? */
        } else {
            trade_loc(ctx, 0x44, 0xe, gfx_text_line_x_acc() + 0x60, 0x100, mf, 0xf9);  /* Purchase? */
        }
        if (g_trade_net < 0) {
            gfx_text_label_render_dec(-g_trade_net, '@', "", 0x60, 0xe0,
                                      mf, ctx->glyph_table, 0x3f);
        } else {
            gfx_text_label_render_dec(g_trade_net, '@', "", 0x60, 0xe0,
                                      mf, ctx->glyph_table, 0x3f);
        }
        gfx_text_set_line_x_acc(0);
        if (g_trade_net < 1) {
            trade_loc(ctx, 0x44, 1, 0x100, 0xe0, mf, 0x3f);        /* We receive */
        } else {
            trade_loc(ctx, 0x44, 8, 0x100, 0xe0, mf, 0x3f);        /* Total cost of */
        }
        trade_num_units(ctx, g_trade_total, 0,
                        gfx_text_line_x_acc() + 0x100, 0xe0, mf, 0x3f);
    }
}

/* ── FUN_004ba26e case 0x0c — the per-frame grid refresh ────────────────
 * Redraws the content (FUN_0041635f) + the buy/sell arrow strip
 * (FUN_0040cfd2(0x30,0x50,…)) over the RETAINED grid frame — NO compute, NO
 * backdrop reload.  Retail runs this every tick while the grid is open, so
 * the running net/total the arrows set (and the press-flash) stay current;
 * the content's own panel fills erase the previous frame's figures.  Skips
 * until the icon sheet is loaded (the first-open full render loads it). */
void engine_trade_refresh(const engine_trade_ctx_t *ctx)
{
    if (!ctx->loc || !g_trade_scratch) {
        return;
    }
    if (gfx_lock_canvas()) {
        trade_content(ctx);
        engine_button_render_list(0x30, 0x50, k_trade_buttons,
                                  g_trade_widget_count,
                                  NULL, ctx->system_sheet, NULL);
        gfx_unlock_canvas();
    }
}

/* ── FUN_00416308 — the OPEN render of the trade grid (screen 0x0c) ──────
 * The full render retail runs from FUN_0043530e (the merchant→grid open):
 * compute the figures + reset the running net (FUN_00428daf), reload the
 * merchant.pl8 backdrop + icontrad.pl8 icon sheet, then the shared content +
 * arrow draw.  The per-frame refresh above takes over from the next tick. */
void engine_trade_render(const engine_trade_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }

    /* FUN_00428daf resets the running net/total, so retail runs it ONLY here
     * (the open render), never in the per-frame refresh — a purchase in
     * progress must survive the redraws the arrows drive.  The one-shot flag
     * also stops a stray demand-redraw from wiping it. */
    if (g_trade_needs_compute) {
        engine_trade_compute(ctx);          /* FUN_00428daf */
        g_trade_needs_compute = 0;
    }

    io_path_chdir_base();                   /* FUN_004af502 */
    /* FUN_004b1de0() verified no-op; FUN_0045240a() full-screen dirty mark
     * is redraw bookkeeping owned by the caller's seam (as in labor). */

    /* merchant.pl8: the same 640×480 backdrop as the merchant scene, into
     * the back buffer (FUN_00408fcb — self-locks).  NO palette load: the
     * grid inherits merchant.256, always active when reached from the
     * merchant. */
    gfx_load_background_image("merchant.pl8");

    /* icontrad.pl8: the trade icon sheet, into scratch (FUN_004af8d9). */
    if (!trade_ensure_scratch()) {
        return;
    }
    io_read_file_at("icontrad.pl8", g_trade_scratch, TRADE_SCRATCH_LEN, 0);

    engine_trade_refresh(ctx);
    /* DAT_004eb260 = 1 — scene flag with no ported consumer (as merchant). */
}
