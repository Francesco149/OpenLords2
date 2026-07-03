/*
 * OpenLords2 — Send Supplies screen (screen 0x18).  See engine_sendsupplies.h
 * for the retail address map and the staging-array layout.
 *
 * The render is split the same three ways as every interactive strat panel:
 *   - engine_sendsupplies_render  (FUN_0041ad5d)  — the open/demand panel,
 *     drawn over a freshly recomposited map (the strat composite is the
 *     caller's, exactly as engine_county_panels);
 *   - engine_sendsupplies_refresh (FUN_0041aea2)  — the per-frame goods table;
 *   - the button/hotspot tables below, click-dispatched by engine.c's router.
 */

#include <stdint.h>
#include <string.h>

#include "engine_sendsupplies.h"

#include "engine_army_ai.h"        /* dispatch_caravan / caravan_order (FUN_004292af, DAT_005678c0) */
#include "engine_army_economy.h"   /* provision / plan_realm (FUN_0044df5f / FUN_004485a5) */
#include "engine_territory.h"      /* workforce / grade cells (FUN_0044f6e7 / FUN_0044d913) */
#include "engine_minimap.h"        /* type/region grids + plot (FUN_0040a01d / FUN_00410cbd) */
#include "save.h"                  /* g_per_army_768 (county records) */
#include "save_schema_globals.h"   /* s_slot_0057c8cc / _0057c934 / _0057c92c */

#include "gfx_panel.h"             /* bevel + interior fill (FUN_004093e0 / FUN_00409c93) */
#include "gfx_sprite_render.h"     /* raw grid blit (FUN_0040a01d) */
#include "gfx_line.h"              /* sunken frame (FUN_00403deb) */
#include "gfx_wrap.h"              /* localized line + wrap (FUN_00402d37 / FUN_0040328e) */
#include "gfx_text_label.h"        /* decimal figures (FUN_00402f64) */
#include "gfx_dispatch.h"          /* icon sprites (FUN_0040a21a) */

/* County record stride/offsets (retail DAT_0053f9b0 family, stride 0x300). */
#define CTY_REC(k)          (&g_per_army_768[(size_t)(k) * 0x300])
#define CTY_GRAIN_STOCK     0x224   /* DAT_0053fbd4 — grain (ARMY_PROVISION_RAW) */
#define CTY_CATTLE_STOCK    0x250   /* DAT_0053fc00 — cattle (ARMY_STOCK_A)      */

/* Destination county — retail DAT_00553c78 (transient UI state, not save-
 * tracked; seeded to the source on entry, re-pointed by target_select). */
static int g_send_target;

int engine_sendsupplies_target(void) { return g_send_target; }

static int32_t cty_i32(int county, int off)
{
    int32_t v;
    memcpy(&v, CTY_REC(county) + off, sizeof v);
    return v;
}

/* ── FUN_0043ae30 param-3 body — stage the county's stock ──────────────── */
void engine_sendsupplies_seed(int player, int source_county)
{
    int32_t *ord = engine_army_ai_caravan_order(player);
    ord[0] = cty_i32(source_county, CTY_GRAIN_STOCK);   /* grain have  */
    ord[1] = 0;                                         /* grain send  */
    ord[4] = cty_i32(source_county, CTY_CATTLE_STOCK);  /* cattle have */
    ord[5] = 0;                                         /* cattle send */
    g_send_target = source_county;                      /* DAT_00553c78 */
}

/* ── Arrows / all-toggle (retail FUN_0043b27a / b1ca / b32a) ────────────
 * `good` is retail DAT_0059154c (0 = grain, 2 = cattle); have = ord[good*2],
 * to-send = ord[good*2+1].  Steps ±1 below 11, ±10 at 11+ (retail's `< 0xb`
 * branch).  All operate on the LOCAL player's staging order (DAT_0057c8cc). */
void engine_sendsupplies_arrow_up(int good)     /* have → to-send */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[h] < 0xb) {
        if (ord[h] != 0) { ord[h] -= 1;  ord[s] += 1;  }
    } else {             ord[h] -= 10; ord[s] += 10; }
}

void engine_sendsupplies_arrow_down(int good)   /* to-send → have */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[s] < 0xb) {
        if (ord[s] != 0) { ord[s] -= 1;  ord[h] += 1;  }
    } else {             ord[s] -= 10; ord[h] += 10; }
}

void engine_sendsupplies_toggle_all(int good)   /* send-all / return-all */
{
    int32_t *ord = engine_army_ai_caravan_order((int)s_slot_0057c8cc);
    int h = good * 2, s = good * 2 + 1;
    if (ord[s] == 0) {
        if (ord[h] != 0) { ord[s] += ord[h]; ord[h] = 0; }
    } else {             ord[h] += ord[s]; ord[s] = 0; }
}

/* ── FUN_0043b412 — retarget the shipment by clicking the thumbnail ─────── */
int engine_sendsupplies_target_select(int cursor_x, int cursor_y, int lmb_pressed)
{
    /* Retail gates on DAT_004eafb4 (the LMB-press edge). */
    if (!lmb_pressed) {
        return 0;
    }
    const int ox = 0x60, oy = 0x68;                 /* FUN_0043b412(0x60,0x68) */
    if (cursor_x < ox || cursor_x >= ox + 0x80) {
        return 0;
    }
    if (cursor_y < oy || cursor_y >= oy + 0x80) {
        return 0;
    }
    /* retail index: DAT_004e65fc + ((DAT_004e65f4 - oy) * 0x80 - ox). */
    uint8_t cell = g_minimap_region_grid[cursor_x + (cursor_y - oy) * 0x80 - ox];
    if (cell == 0) {
        return 0;
    }
    g_send_target = cell;
    return 1;
}

/* ── FUN_0043b145 — commit: dispatch the caravan + recompute the source ─── */
void engine_sendsupplies_commit(int player, int source, uint8_t target)
{
    engine_army_ai_dispatch_caravan(player, source, target);      /* FUN_004292af */
    engine_army_economy_provision(source, (int)s_slot_0057c934);  /* FUN_0044df5f */
    engine_territory_allocate_realm_workforce(source);            /* FUN_0044f6e7 */
    engine_territory_grade_feature_cells(source);                 /* FUN_0044d913 */
    engine_army_economy_plan_realm(source, (int)s_slot_0057c92c); /* FUN_004485a5 */
    engine_territory_allocate_realm_workforce(source);            /* (retail repeats */
    engine_territory_grade_feature_cells(source);                 /*  the recompute  */
    engine_army_economy_plan_realm(source, (int)s_slot_0057c92c); /*  triplet twice) */
}

/* ── one localized single-line render bound to the ctx (FUN_00402d37) ───── */
static void ss_loc(const engine_sendsupplies_ctx_t *ctx, int grp, int sub,
                   int x, int y, const uint8_t *font)
{
    gfx_wrap_localized_line(ctx->loc, grp, sub, x, y,
                            font, ctx->glyph_table, 0x3f);
}

/* ── FUN_0041ad5d — the open render (over the recomposited map) ─────────── */
void engine_sendsupplies_render(const engine_sendsupplies_ctx_t *ctx)
{
    /* FUN_004050c0 (map recomposite) is the caller's — engine.c runs the
     * strat composite before this, exactly as the county detail panels.
     * FUN_0045240a (dirty mark) is seam-owned; omitted like every panel. */

    /* FUN_004093e0(0x40,0x30,0x16,0x16) — the raised parchment panel. */
    if (ctx->chrome) {
        gfx_panel_draw_bevel(ctx->chrome, 0x40, 0x30, 0x16, 0x16);
    }

    /* FUN_00410a5d(county, 0x60, 0x68) — the county thumbnail (source until a
     * target is chosen, else the target): the terrain grid raw-blit backdrop
     * (FUN_0040a01d) + the political plot (FUN_00410cbd mode 0), both at the
     * internal (−2,+3) offset retail applies. */
    int cty = (ctx->target == 0) ? ctx->county : ctx->target;
    gfx_sprite_render_raw(g_minimap_type_grid, 0x60 - 2, 0x68 + 3, 0x80, 0x80);
    engine_minimap_plot((unsigned)cty, 0x60 - 2, 0x68 + 3, 0);

    /* Title + labels (grp 0x21) and the From/To county names (grp 0x64,
     * indexed map_id*0x14 + county). */
    ss_loc(ctx, 0x21, 0, 0x50, 0x44, ctx->title_font);   /* "Send supplies" */
    /* FUN_0040328e(0x21,7,0xf0,0x70,0xb0,100,0,0,menu,0x3f) — the wrapped
     * "Click on a county to supply." instruction (the measure pass uses the
     * glyph-WIDTH map, not the render map — else the wrap point is off). */
    gfx_wrap_localized(ctx->loc, 0x21, 7, 0xf0, 0x70, 0xb0, 100, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics, 0x3f);
    ss_loc(ctx, 0x21, 1, 0xf0, 0x9e, ctx->menu_font);    /* "Supply from:" */
    ss_loc(ctx, 0x64, ctx->map_id * 0x14 + ctx->county,
           0xf0, 0xb0, ctx->menu_font);                  /* source county name */
    ss_loc(ctx, 0x21, 2, 0xf0, 0xc2, ctx->menu_font);    /* "To:" */
    ss_loc(ctx, 0x64, ctx->map_id * 0x14 + ctx->target,
           0xf0, 0xd4, ctx->menu_font);                  /* target county name */
    ss_loc(ctx, 0x21, 6, 0x60, 0x160, ctx->menu_font);   /* "Dispatch shipment?" */
}

/* ── FUN_0041aea2 — the per-frame goods table (grain + cattle rows) ─────── */
void engine_sendsupplies_refresh(const engine_sendsupplies_ctx_t *ctx)
{
    const int32_t *ord = engine_army_ai_caravan_order(ctx->local_player);

    /* FUN_00409c93(0x50,0x100,0x14,5) — repaint the goods-table interior;
     * FUN_004527a6 (dirty-hotspot register) is UI-only, omitted. */
    if (ctx->chrome) {
        gfx_panel_fill_interior(ctx->chrome, 0x50, 0x100, 0x14, 5);
    }
    /* FUN_00403deb(0x50,0x100,0x140,0x50) — the sunken table frame. */
    gfx_line_rect_bevel_dark_br(0x50, 0x100, 0x140, 0x50);

    /* Grain row (grp 0x21 sub 3): have | icon 0x21 | to-send. */
    ss_loc(ctx, 0x21, 3, 0x58, 0x10c, ctx->menu_font);
    gfx_text_label_render_dec(ord[0], ' ', " ", 0xa0, 0x10c,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_dispatch_sprite(ctx->misc_sheet, 0x21, 0xfa, 0x108);
    }
    gfx_text_label_render_dec(ord[1], ' ', " ", 0x150, 0x10c,
                              ctx->menu_font, ctx->glyph_table, 0x3f);

    /* Cattle row (grp 0x21 sub 5): have | icon 0x26 | to-send. */
    ss_loc(ctx, 0x21, 5, 0x58, 0x130, ctx->menu_font);
    gfx_text_label_render_dec(ord[4], ' ', " ", 0xa0, 0x130,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    if (ctx->misc_sheet) {
        gfx_dispatch_sprite(ctx->misc_sheet, 0x26, 0xf8, 0x12c);
    }
    gfx_text_label_render_dec(ord[5], ' ', " ", 0x150, 0x130,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
}

/* ── The click tables (retail DAT_004dd538 / DAT_004dc7e8), r2-extracted ──
 * Fields: { x, y, sprite, kind, action, state, pressed, pad_e, type, param0,
 * param1 } — same 0x18-byte record the trade/armory tables use.  The seals
 * are type 5 (press-latch); the arrows type 4 (hold auto-repeat).  param0 is
 * the seal mode (1 = send, 0 = cancel) / the arrow's good index (0/2). */
static engine_button_rec_t k_sendsupplies_buttons[6] = {
    { 0x140, 0x156, 0x1d, 0x20, 0x43b04c, 0, 0, 0, 5, 1, 0 },  /* Send seal   */
    { 0x168, 0x15a, 0x1f, 0x20, 0x43b04c, 0, 0, 0, 5, 0, 0 },  /* Cancel seal */
    { 0x0d8, 0x108, 0x1b, 0x18, 0x43b1ca, 0, 0, 0, 4, 0, 0 },  /* grain down  */
    { 0x128, 0x108, 0x19, 0x18, 0x43b27a, 0, 0, 0, 4, 0, 0 },  /* grain up    */
    { 0x0d8, 0x12c, 0x1b, 0x18, 0x43b1ca, 0, 0, 0, 4, 2, 0 },  /* cattle down */
    { 0x128, 0x12c, 0x19, 0x18, 0x43b27a, 0, 0, 0, 4, 2, 0 },  /* cattle up   */
};

engine_button_rec_t *engine_sendsupplies_buttons(void) { return k_sendsupplies_buttons; }

/* The two "send-all / return-all" boxes over the grain/cattle rows (retail
 * DAT_004dc7e8, walked by FUN_0040e3ee at origin 0x40,0x30; action FUN_0043b32a,
 * param = good index). */
static const engine_menu_hotspot_t k_sendsupplies_all_hotspots[2] = {
    { 0xbe, 0xd8, 0xdc, 0xf5,  NULL, 1, 0 },   /* grain  */
    { 0xbe, 0xf6, 0xdc, 0x113, NULL, 1, 2 },   /* cattle */
};

const engine_menu_hotspot_t *engine_sendsupplies_all_hotspots(void)
{
    return k_sendsupplies_all_hotspots;
}
