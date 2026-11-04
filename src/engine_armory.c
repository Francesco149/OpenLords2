/*
 * OpenLords2 — the Armory / army-muster VIEW (screen 0x0a).  See
 * engine_armory.h for the retail address map + the per-record offsets.
 *
 * Every draw mirrors its decompile line 1:1 (filenames, coordinates, fonts,
 * colors).  The two .rdata layout tables are r2-extracted byte-exact.  The
 * counts are pure reads of the muster pool (engine_armory_muster_init fills
 * it on entry); nothing here computes.
 *
 * Deferred to the interactive army create/change overlay (screen 0x17,
 * FUN_00418653) and its own port: the animated torch/glint sweep (so the
 * four FUN_004b3f0a backdrop-save strips are skipped here), the arm_grid.pl8
 * hit-map load (input only), the FUN_004527a6 tooltip-hotspot registrations,
 * and the garrison-mercenary count contribution (DAT_0055446c / the 0x568dc0
 * merc table — all zero on the loaded save, so inert at this view).
 */

#include "engine_armory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine_math.h"             /* engine_math_percent_of (FUN_00404d6b) */
#include "engine_workforce_hud.h"    /* backdrop strip capture/restore
                                        (FUN_004b3f0a / FUN_004b3ec0) */
#include "game_init.h"               /* engine_game_init_troop_pool_row */
#include "gfx.h"                     /* backdrop/palette/lock, GFX_RENDER_W/H */
#include "gfx_line.h"                /* gfx_line_rect_bevel_dark_br */
#include "gfx_panel.h"               /* gfx_panel_draw / fill_interior */
#include "gfx_sprite_render.h"       /* gfx_sprite_render_from_blob, seal */
#include "gfx_text.h"                /* gfx_text_set_outline, line_x_acc */
#include "gfx_text_label.h"          /* gfx_text_label_render_dec */
#include "gfx_wrap.h"                /* gfx_wrap_localized_line[_centered] */
#include "io.h"                      /* io_read_file_at (FUN_004af8d9) */
#include "io_path.h"                 /* io_path_chdir_base (FUN_004af502) */

#define STRAT_STRIDE   0x160
#define POOL_STRIDE    0x80

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    __builtin_memcpy(&v, rec + off, sizeof v);
    return v;
}

static void put_i32(uint8_t *rec, int off, int32_t v)
{
    __builtin_memcpy(rec + off, &v, sizeof v);
}

/* ── The heraldry-colored interior sheet (arm_it_r<c>.pl8) ───────────────
 * retail loads it into the scratch DAT_0056d5b8 (0x4b320 B) indexed by the
 * player's heraldry byte (strat +0x0a); we keep a persistent buffer,
 * reloaded per render, and bind it as the sprite blob for the racks +
 * counts (the same pattern as engine_rankings' flags.pl8).  The filename
 * table (retail 0x4d2c88, 0x10-byte entries) selects the color; a failed
 * load falls back to base1a.pl8. */
#define ARMORY_INTERIOR_LEN 0x4b320
static uint8_t *g_armory_interior;

/* ── The torch-flame sheet (armtorch.pl8) ───────────────────────────────
 * retail loads it into the shared scratch DAT_005533c4 (150000 B); we keep a
 * persistent buffer loaded by the render, drawn by the per-frame refresh. */
#define ARMORY_TORCH_LEN 150000
static uint8_t *g_armory_torch;

/* ── The muster-figure click-zone grid (arm_grid.pl8) ───────────────────
 * retail loads it into the shared grid slot DAT_00542ce0 (0x12d8 B: 0x18
 * header + an 80×60 stride-0x50 byte map of weapon ids); same slot the
 * merchant's mercgrid.pl8 and the labor category grid use, different data
 * per screen — the port keeps a private buffer (engine_merchant.c
 * precedent). */
#define ARMORY_GRID_LEN 0x12d8
#define ARMORY_GRID_HDR 0x18
static uint8_t *g_armory_grid;

/* ── The per-weapon demo sheet (arm_cros/mace/swor/pike/bow/mail.pl8) ────
 * retail scratch DAT_0053e918 (250000 B), loaded by the assign render. */
#define ARMORY_WEAPON_LEN 250000
static uint8_t *g_armory_weapon_sheet;

/* ── The troop-march sprite sheet (trp_<w>_<c>.pl8) ─────────────────────
 * retail scratch DAT_0057a0c0 (0x4b320 B), loaded by the march trigger. */
#define ARMORY_TROOP_LEN 0x4b320
static uint8_t *g_armory_troop;

/* ── The four march backdrop strips (retail reuses DAT_004eabec) ────────
 * Captured by the open render (FUN_004b3f0a ×4) over the march band
 * (y 0xd8, 0x9e rows): segments at x 0 / 0xa0 / 0x140 / 0x1e0, widths
 * 0x3c / 0x3c / 0x3c / 0x28 dwords, packed at dword offsets 0 / 0x2508 /
 * 0x4a10 / 0x6f18 (retail's exact pointer arithmetic). */
#define ARMORY_STRIP_Y      0xd8
#define ARMORY_STRIP_ROWS   0x9e
#define ARMORY_STRIPS_DWORDS (0x6f18 + 0x28 * 0x9e)
static uint32_t *g_armory_strips;
static const struct { int x; int w_dwords; int off_dwords; } k_strip_seg[4] = {
    { 0x000, 0x3c, 0x0000 },
    { 0x0a0, 0x3c, 0x2508 },
    { 0x140, 0x3c, 0x4a10 },
    { 0x1e0, 0x28, 0x6f18 },
};

/* ── Armory scratch state (retail globals; none are save-schema slots) ── */
static int g_pct;           /* DAT_0056d65c — conscription % (persists)   */
static int g_conscripts;    /* DAT_00543fd8 — men at the current %        */
static int g_happy_cost;    /* DAT_00565400 — happiness cost              */
static int g_mode;          /* DAT_00522f58 — overlay button count (1/3)  */
static int g_merc;          /* DAT_0055446c — hire-mercenaries toggle     */
static int g_weapon;        /* DAT_00553f20 — assign-screen weapon (1..6) */
static int g_march_active;  /* DAT_005679d0 */
static int g_march_x;       /* DAT_0056d630 */
static int g_march_y;       /* DAT_0056d62c */
static int g_march_target;  /* DAT_00568228 */
static int g_march_land;    /* DAT_005681f8 — salute frame counter        */
static int g_march_sprite;  /* DAT_0052f008 */
static int g_march_walk;    /* DAT_0057cb10 — walk-cycle 0..7             */
static int g_march_skip_arm_tick;  /* port-order compensation, see trigger */

int  engine_armory_overlay_mode(void) { return g_mode; }
int  engine_armory_conscripts(void)   { return g_conscripts; }
int  engine_armory_pct(void)          { return g_pct; }
void engine_armory_set_merc(int on)   { g_merc = on ? 1 : 0; }
int  engine_armory_merc(void)         { return g_merc; }
int  engine_armory_happy_cost(void)   { return g_happy_cost; }
int  engine_armory_weapon(void)       { return g_weapon; }
int  engine_armory_march_active(void) { return g_march_active; }
static const char *const k_interior_file[6] = {
    "arm_it_r.pl8",   /* 0 */
    "arm_it_r.pl8",   /* 1 */
    "arm_it_y.pl8",   /* 2 */
    "arm_it_k.pl8",   /* 3 */
    "arm_it_p.pl8",   /* 4 */
    "arm_it_b.pl8",   /* 5 */
};

/* ── .rdata layout tables (r2-extracted byte-exact) ─────────────────────
 * k_rack   — FUN_00418426 wall-rack icons, one {icon,x,y} per weapon type
 *            1..6 (drawn when the player owns >0 of that weapon).
 * k_count  — FUN_004181eb bottom muster tally, one {icon,sx,sy,tx,ty} per
 *            entry 0..6 (entry 0 = men; entries 1..6 = the six weapons).
 * (retail 0x4d2d88 and 0x4d2ce8 respectively.) */
static const int k_rack[6][3] = {
    { 0, 0x3b, 0xb2 }, { 1, 0x9d, 0xf0 }, { 2, 0x1f0, 0xeb },
    { 3, 0xc7, 0x82 }, { 4, 0x175, 0xe8 }, { 5, 0x122, 0xb6 },
};
static const int k_count[7][5] = {
    { 6, 0x0b,  0x18c, 0x0c,  0x1c2 },   /* men     */
    { 7, 0x55,  0x18c, 0x56,  0x1c2 },   /* weapon 1 */
    { 8, 0xa2,  0x18c, 0xa3,  0x1c2 },   /* weapon 2 */
    { 0xc, 0x1ce, 0x18c, 0x1cf, 0x1c2 }, /* weapon 3 */
    { 9, 0xed,  0x18c, 0xee,  0x1c2 },   /* weapon 4 */
    { 0xb, 0x183, 0x18c, 0x184, 0x1c2 }, /* weapon 5 */
    { 0xa, 0x137, 0x18c, 0x138, 0x1c2 }, /* weapon 6 */
};

/* ── FUN_004aa90a — the on-entry muster-pool setup ──────────────────────*/
void engine_armory_muster_init(int county_owner, int population,
                               const uint8_t *owner_strat_rec)
{
    uint8_t *row = engine_game_init_troop_pool_row(county_owner);

    for (int e = 0; e < 8; e++) {
        put_i32(row, e * 0x10 + 0, 0);
    }
    for (int e = 0; e < 8; e++) {
        put_i32(row, e * 0x10 + 4, 0);
    }
    /* Weapon stock gates (+4) + a working copy (+8), from the owner's
     * strategic weapon stocks (rec +0x13c + t*4, t=1..6). */
    for (int t = 1; t < 7; t++) {
        put_i32(row, t * 0x10 + 4, rec_i32(owner_strat_rec, 0x13c + t * 4));
    }
    for (int t = 1; t < 7; t++) {
        put_i32(row, t * 0x10 + 8, rec_i32(owner_strat_rec, 0x13c + t * 4));
    }
    /* The men count (+0/+8 of entry 0) and the army total (entry 7). */
    put_i32(row, 0x00, population);
    put_i32(row, 0x08, population);
    put_i32(row, 0x70, population);
    put_i32(row, 0x78, population);
    /* Retail tail: deselect the assign weapon + kill any live march
     * (DAT_00553f20 = 0; DAT_005679d0 = 0; _DAT_0057c8e4 = 0 — the glint
     * phase is write-only in this family, not mirrored). */
    g_weapon = 0;
    g_march_active = 0;
}

/* ── FUN_00418426 — the owned-weapon wall racks ─────────────────────────*/
static void armory_racks(const engine_armory_ctx_t *ctx)
{
    const uint8_t *strat =
        ctx->strategic_block + (size_t)ctx->local_player * STRAT_STRIDE;
    for (int t = 1; t < 7; t++) {
        if (rec_i32(strat, 0x13c + t * 4) > 0) {
            gfx_sprite_render_from_blob(g_armory_interior, k_rack[t - 1][0],
                                        k_rack[t - 1][1], k_rack[t - 1][2]);
        }
    }
}

/* ── FUN_004181eb — the bottom muster tally ─────────────────────────────
 * Entry 0 (men) always draws; entries 1..6 draw only when their stock gate
 * (pool +e*0x10 +4) is > 0.  The count is pool +e*0x10 +0 (the merc
 * contingent add is deferred — inert at the loaded save).  The
 * per-readout tooltip-hotspot register (FUN_004527a6) is UI-only, omitted. */
void engine_armory_counts_render(const engine_armory_ctx_t *ctx)
{
    const uint8_t *row =
        ctx->troop_pool + (size_t)ctx->local_player * POOL_STRIDE;
    for (int e = 0; e <= 6; e++) {
        if (e != 0 && rec_i32(row, e * 0x10 + 4) <= 0) {
            continue;
        }
        gfx_sprite_render_from_blob(g_armory_interior, k_count[e][0],
                                    k_count[e][1], k_count[e][2]);
        gfx_text_label_render_dec(rec_i32(row, e * 0x10 + 0), '@', "",
                                  k_count[e][3], k_count[e][4],
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
    }
}

/* ── FUN_00417ea7 body — the armory scene draw ──────────────────────────
 * Caller holds the canvas lock; the backdrop is already blitted and the
 * interior sheet loaded.  Retail's dirty mark (FUN_0045240a) + input tick
 * (FUN_00452160) are seam-owned and OMITTED (the port repaints the full
 * frame), as in engine_rankings / engine_treasury. */
static void armory_scene(const engine_armory_ctx_t *ctx)
{
    /* FUN_0040d1bc(W-0x1c,H-0x70,1) — the continue seal (drawn first, over
     * the backdrop, exactly as retail). */
    if (ctx->system_sheet) {
        gfx_sprite_seal_draw(ctx->system_sheet,
                             GFX_RENDER_W - 0x1c, GFX_RENDER_H - 0x70, 1);
    }

    armory_racks(ctx);       /* FUN_00418426 */

    /* Capture the four troop-march backdrop strips (FUN_004b3f0a ×4) —
     * retail snapshots the muster band (y 0xd8, 0x9e rows) right after the
     * racks, before the counts/labels (which draw below the band anyway). */
    if (!g_armory_strips) {
        g_armory_strips = malloc(ARMORY_STRIPS_DWORDS * sizeof(uint32_t));
    }
    if (g_armory_strips) {
        for (int s = 0; s < 4; s++) {
            engine_workforce_hud_capture_backdrop(
                g_armory_strips + k_strip_seg[s].off_dwords,
                k_strip_seg[s].x, ARMORY_STRIP_Y,
                k_strip_seg[s].w_dwords, ARMORY_STRIP_ROWS, GFX_RENDER_W);
        }
    }

    engine_armory_counts_render(ctx);      /* FUN_004181eb */

    /* The three action labels — grp 0x45 "Create" / "Change" / "Cancel",
     * centered in a 100px box at x=0x21e, color 0x37, outline mode
     * (DAT_0058fe9c = 1). */
    gfx_text_set_outline(1);
    for (int i = 0; i < 3; i++) {
        gfx_wrap_localized_line_centered(ctx->loc, 0x45, 6 + i, 0x21e,
                                         0x194 + i * 0x1a, 100, ctx->menu_font,
                                         ctx->glyph_table, ctx->glyph_metrics,
                                         0x37);
    }
    gfx_text_set_outline(0);
}

void engine_armory_render(const engine_armory_ctx_t *ctx)
{
    if (!ctx->loc) {
        return;
    }

    io_path_chdir_base();                       /* FUN_004af502 */
    /* 0x4b11ce (transient palette/canvas prep) + 0x4996c8 (the
     * strategic-map teardown + armory buffer alloc) are DEFERRED — the port
     * keeps the map resident and uses the persistent scratch below, exactly
     * as engine_merchant / engine_rankings defer the retail free/malloc. */

    /* armoury.pl8: the 640×480 backdrop straight into the back buffer
     * (FUN_00408fcb — self-locks). */
    gfx_load_background_image("armoury.pl8");

    /* arm_grid.pl8 → the muster-figure click zones (input only, retail
     * loads it into the shared DAT_00542ce0 slot right after the backdrop). */
    if (!g_armory_grid) {
        g_armory_grid = malloc(ARMORY_GRID_LEN);
    }
    if (g_armory_grid &&
        !io_read_file_at("arm_grid.pl8", g_armory_grid, ARMORY_GRID_LEN, 0)) {
        free(g_armory_grid);
        g_armory_grid = NULL;
    }

    /* arm_it_r<c>.pl8 → the resident interior sheet, color-indexed by the
     * player's heraldry (strat +0x0a); base1a.pl8 on a failed load. */
    if (!g_armory_interior) {
        g_armory_interior = malloc(ARMORY_INTERIOR_LEN);
    }
    if (!g_armory_interior) {
        return;
    }
    int color = ctx->strategic_block[(size_t)ctx->local_player * STRAT_STRIDE + 0x0a];
    if (color < 0 || color > 5) {
        color = 0;
    }
    if (!io_read_file_at(k_interior_file[color], g_armory_interior,
                         ARMORY_INTERIOR_LEN, 0)) {
        io_read_file_at("base1a.pl8", g_armory_interior,
                        ARMORY_INTERIOR_LEN, 0);
    }

    /* armtorch.pl8 → the resident torch sheet for the per-frame flame refresh
     * (retail loads it into DAT_005533c4 here in FUN_00417ea7). */
    if (!g_armory_torch) {
        g_armory_torch = malloc(ARMORY_TORCH_LEN);
    }
    if (g_armory_torch) {
        io_read_file_at("armtorch.pl8", g_armory_torch, ARMORY_TORCH_LEN, 0);
    }

    if (gfx_lock_canvas()) {
        armory_scene(ctx);
        gfx_unlock_canvas();
    }

    /* armoury.256 → the active palette last (retail applies FUN_004b0ab5
     * after the draws, so it lands on the settled frame; the screen-0 close
     * arm restores base01.256, same as merchant / rankings). */
    gfx_load_palette("armoury.256");
}

/* ── FUN_00419243 — the two animated torch flames ───────────────────────
 * Drawn over the retained backdrop every tick.  The armtorch frames are
 * self-covering (opaque within their bounding box), so redrawing frame N over
 * N-1 replaces it — no backdrop restore needed.  The march restore/step
 * siblings (FUN_00418fc5/FUN_004190db) bracket this in the per-frame arm.
 * CALLER holds the canvas lock (the seam arm locks once around the retail
 * FUN_004ba26e sequence).  The per-readout tooltip-hotspot registers
 * (FUN_004527a6) are UI-only. */
void engine_armory_refresh(int torch_frame)
{
    if (!g_armory_torch) {
        return;
    }
    gfx_sprite_render_from_blob(g_armory_torch, torch_frame, 0x9a, 0x76);
    gfx_sprite_render_from_blob(g_armory_torch, torch_frame + 0xd,
                                0x19a, 0x76);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Screen 0x17 — the army create/change overlay
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── FUN_0041ab67 — a decimal value + its loc-group-8 unit noun ─────────
 * (same replica as engine_labor's labor_num_units; kept file-local, the
 * retail original is a shared leaf).  Preserves the caller's line-x
 * accumulator around the pair. */
static void armory_num_units(const engine_armory_ctx_t *ctx, int value,
                             int unit_sub, int x, int y,
                             const uint8_t *font, uint8_t color)
{
    int saved = gfx_text_line_x_acc();          /* iVar1 = DAT_005cd404 */
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(value, '@', "", x, y,   /* &DAT_004d41f4 = "" */
                              font, ctx->glyph_table, color);
    int sub = (value == 1 || value == -1) ? unit_sub : unit_sub + 1;
    gfx_wrap_localized_line(ctx->loc, 8, sub,
                            x + gfx_text_line_x_acc(), y,
                            font, ctx->glyph_table, color);
    gfx_text_set_line_x_acc(gfx_text_line_x_acc() + saved);
}

/* ── FUN_0041ac3e — the loc-group-8 unit noun alone (sing./plural) ───────*/
static void armory_unit_noun(const engine_armory_ctx_t *ctx, int value,
                             int unit_sub, int x, int y,
                             const uint8_t *font, uint8_t color)
{
    gfx_wrap_localized_line(ctx->loc, 8,
                            (value == 1) ? unit_sub : unit_sub + 1,
                            x, y, font, ctx->glyph_table, color);
}

/* ── DAT_004d8778 — happiness cost per conscription % (r2-extracted) ─────*/
static const int k_conscript_happy[101] = {
      0,   1,   1,   1,   2,   2,   3,   3,   4,   4,
      5,   5,   6,   6,   7,   7,   8,   8,   9,   9,
    0xa, 0xb, 0xd, 0xf, 0x11, 0x13, 0x15, 0x17, 0x19, 0x1b,
    0x1d, 0x1f, 0x22, 0x25, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c,
    0x40, 0x44, 0x48, 0x4b, 0x4e, 0x50, 0x52, 0x54, 0x56, 0x58,
    0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
    0x64, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65,
    0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65,
    0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65,
    0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65, 0x65,
    0x65,
};

/* ── FUN_00435ebc — conscription % → men + happiness cost ────────────────*/
void engine_armory_conscript_update(const uint8_t *county_rec, int pct)
{
    g_conscripts = engine_math_percent_of(rec_i32(county_rec, 0x24), pct);
    g_happy_cost = k_conscript_happy[pct];
    if (pct != 0) {
        g_happy_cost += rec_i32(county_rec, 0x2f4);
    }
    if (g_happy_cost > 100) g_happy_cost = 100;
    if (g_happy_cost < 0)   g_happy_cost = 0;

    if ((int8_t)county_rec[0x0c] < 1) {
        /* A county already at rock-bottom happiness conscripts nothing. */
        g_conscripts = 0;
        g_happy_cost = 0;
        return;
    }
    /* Step the EFFECTIVE pct down until happiness stays >= 1.  The slider
     * global (g_pct) is NOT written back — retail leaves the knob where
     * the user put it. */
    while ((int8_t)county_rec[0x0c] - g_happy_cost < 1) {
        pct = pct - 1;
        g_conscripts = engine_math_percent_of(rec_i32(county_rec, 0x24), pct);
        g_happy_cost = k_conscript_happy[pct];
        if (pct != 0) {
            g_happy_cost += rec_i32(county_rec, 0x2f4);
        }
        if (g_happy_cost > 100) g_happy_cost = 100;
        if (g_happy_cost < 0)   g_happy_cost = 0;
    }
}

/* ── FUN_00435cef — the conscription slider hit-test ─────────────────────*/
int engine_armory_slider_click(const uint8_t *county_rec,
                               int cursor_x, int cursor_y,
                               int lmb_pressed, int lmb_repeat, int lmb_held)
{
    int ybase = county_rec[0x1ad] != 0 ? 0x90 : 0xb0;
    if (cursor_x < 0x80 || cursor_x >= 0x176) {
        return 0;
    }
    if (cursor_y < ybase || cursor_y >= ybase + 0x30) {
        return 0;
    }
    if (cursor_x < 0xc4) {
        if (lmb_pressed == 0 && lmb_repeat == 0) {
            return 0;
        }
        g_pct = g_pct - 1;
    } else if (cursor_x < 0x129) {
        if (lmb_held == 0) {
            return 0;                     /* the track is a DRAG target */
        }
        g_pct = cursor_x - 0xc4;
    } else {
        if (lmb_pressed == 0 && lmb_repeat == 0) {
            return 0;
        }
        g_pct = g_pct + 1;
    }
    if (g_pct < 0)   g_pct = 0;
    if (g_pct > 100) g_pct = 100;
    engine_armory_conscript_update(county_rec, g_pct);
    return 1;                             /* caller: demand_redraw = 2 */
}

/* The overlay button strip (retail DAT_004dd340, r2-extracted): the confirm
 * seal (type 5 press-latch) + the mercenary Yes/No gauntlets. */
static engine_button_rec_t k_overlay_buttons[3] = {
    { 0x1e0, 0x150, 0x21, 0x18, 0x435cbf, 0, 0, 0, 5, 0, 0 },
    { 0x160, 0x100, 0x1d, 0x20, 0x435c89, 0, 0, 0, 5, 1, 0 },
    { 0x190, 0x104, 0x1f, 0x20, 0x435c89, 0, 0, 0, 5, 0, 0 },
};

engine_button_rec_t *engine_armory_overlay_buttons(void)
{
    return k_overlay_buttons;
}

/* ── FUN_00418653 — the create/change overlay render ─────────────────────*/
void engine_armory_overlay_render(const engine_armory_ctx_t *ctx)
{
    const uint8_t *strat =
        ctx->strategic_block + (size_t)ctx->local_player * STRAT_STRIDE;
    int army = ctx->county_rec[0x1ad];          /* existing army id (0 = none) */
    int rows, ybase;

    if (army == 0) {
        g_mode = 1;                             /* DAT_00522f58 — confirm only */
        rows   = 0xd;
        ybase  = 0xa0;
    } else {
        rows  = 0x10;
        ybase = 0x80;
        if (rec_i32(strat, 0x118) <
            rec_i32(ctx->unit_table, army * 0x14 + 0xc)) {
            g_mode = 1;                         /* can't afford the change */
        } else {
            g_mode = 3;                         /* + the merc Yes/No pair */
        }
    }

    if (!gfx_lock_canvas()) {
        return;
    }
    /* FUN_004b1de0 — verified no-op.  FUN_0045240a — dirty mark, seam-owned. */

    /* FUN_00409397(0x50, ybase-0x10, 0x1e, rows+1) — the parchment panel. */
    if (ctx->chrome) {
        gfx_panel_draw(ctx->chrome, 0x50, ybase - 0x10, 0x1e, rows + 1);
    }

    /* Title: "Muster troops in " + the county name (grp 100). */
    gfx_text_set_line_x_acc(0);
    gfx_wrap_localized_line(ctx->loc, 0x45, 0x10, 0x70, ybase - 8,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 100,
                            ctx->map_id * 0x14 + ctx->county,
                            gfx_text_line_x_acc() + 0x70, ybase - 8,
                            ctx->menu_font, ctx->glyph_table, 0x3f);

    /* The slider: groove bevel + the five system-sheet chrome sprites (the
     * knob at pct + 0xc4). */
    gfx_line_rect_bevel_dark_br(0xc3, ybase + 0x28, 0x6f, 4);
    if (ctx->system_sheet) {
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x51, 0x80, ybase + 0x10);
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x4e, 0xb9, ybase + 0x21);
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x50, g_pct + 0xc4,
                                    ybase + 0x1a);
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x4f, 0x132, ybase + 0x21);
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x52, 0x145, ybase + 0x10);
    }

    /* Population remaining (left) / conscripts (right). */
    gfx_text_label_render_dec(rec_i32(ctx->county_rec, 0x24) - g_conscripts,
                              '@', "", 0x80, ybase + 0x44,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_text_label_render_dec(g_conscripts, '@', "", 0x145, ybase + 0x44,
                              ctx->menu_font, ctx->glyph_table, 0x3f);

    /* The six weapon stocks (interior-sheet icons 0xf..0x14). */
    for (int i = 0; i < 6; i++) {
        gfx_sprite_render_from_blob(g_armory_interior, i + 0xf,
                                    i * 0x48 + 0x70,
                                    (rows - 4) * 0x10 + ybase - 4);
        gfx_text_label_render_dec(rec_i32(strat, 0x140 + i * 4), ' ', " ",
                                  i * 0x48 + 0x90, (rows - 4) * 0x10 + ybase,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
    }

    /* Happiness readout: cost < 1 → the "stays at N" pair (subs 0xc/0xd),
     * else the "drops to N" pair (subs 0xa/0xb). */
    if (g_happy_cost < 1) {
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x45, 0xc, 0x188, ybase + 0x18,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x45, 0xd, 0x188, ybase + 0x30,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec((int)(int8_t)ctx->county_rec[0x0c], '@', "",
                                  gfx_text_line_x_acc() + 0x188, ybase + 0x30,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
    } else {
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x45, 0xa, 0x188, ybase + 0x18,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_set_line_x_acc(0);
        gfx_wrap_localized_line(ctx->loc, 0x45, 0xb, 0x188, ybase + 0x30,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec((int)(int8_t)ctx->county_rec[0x0c] -
                                  g_happy_cost, '@', "",
                                  gfx_text_line_x_acc() + 0x188, ybase + 0x30,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
    }
    /* The happiness face icon (system sprite 0x53) after the number. */
    if (ctx->system_sheet) {
        gfx_sprite_render_from_blob(ctx->system_sheet, 0x53,
                                    gfx_text_line_x_acc() + 0x188,
                                    ybase + 0x30);
    }

    if (army == 0) {
        /* CREATE mode: the info text in a 400×100 box + its frame. */
        gfx_wrap_localized(ctx->loc, 0x45, 4, 0x80, ybase + 0x60, 400, 100,
                           0, 0, ctx->menu_font, ctx->glyph_table,
                           ctx->glyph_metrics, 0x3f);
        gfx_line_rect_bevel_dark_br(0x70, ybase + 0x58, 0x1a0, 0x32);
    } else {
        /* CHANGE mode: the existing army's size/name row, its change cost,
         * and — when affordable — the gold readout + merc toggle text. */
        gfx_line_rect_bevel_dark_br(0x70, ybase + 0x58, 0x1a0, 0x60);
        const uint8_t *unit = ctx->unit_table + (size_t)army * 0x14;
        int size = rec_i32(unit, 8);
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec(size, '@', "", 0x70, ybase + 0x60,
                                  ctx->title_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x10, army,
                                gfx_text_line_x_acc() + 0x70, ybase + 0x60,
                                ctx->title_font, ctx->glyph_table, 0x3f);
        armory_unit_noun(ctx, size, unit[5] * 2 + 0x34,
                         gfx_text_line_x_acc() + 0x70, ybase + 0x60,
                         ctx->title_font, 0x3f);

        int cost = rec_i32(unit, 0xc);
        gfx_text_set_line_x_acc(0);
        gfx_text_label_render_dec(cost, '@', "", 0x70, ybase + 0x7c,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x45, 0,
                                gfx_text_line_x_acc() + 0x70, ybase + 0x7c,
                                ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_text_label_render_dec(cost / 2, '@', "",
                                  gfx_text_line_x_acc() + 0x70, ybase + 0x7c,
                                  ctx->menu_font, ctx->glyph_table, 0x3f);
        gfx_wrap_localized_line(ctx->loc, 0x45, 1,
                                gfx_text_line_x_acc() + 0x70, ybase + 0x7c,
                                ctx->menu_font, ctx->glyph_table, 0x3f);

        if (rec_i32(strat, 0x118) < cost) {
            gfx_wrap_localized(ctx->loc, 0x45, 3, 0x80, ybase + 0x94,
                               400, 100, 0, 0, ctx->menu_font,
                               ctx->glyph_table, ctx->glyph_metrics, 0x3f);
        } else {
            gfx_text_set_line_x_acc(0);
            gfx_wrap_localized_line(ctx->loc, 0x45, 0xf, 0x72, ybase + 0x92,
                                    ctx->menu_font, ctx->glyph_table, 0x3f);
            armory_num_units(ctx, rec_i32(strat, 0x118), 0,
                             gfx_text_line_x_acc() + 0x72, ybase + 0x92,
                             ctx->menu_font, 0x3f);
            gfx_wrap_localized_line(ctx->loc, 0x45, 2, 0x92, ybase + 0xa4,
                                    ctx->menu_font, ctx->glyph_table, 0x3f);
            gfx_wrap_localized_line(ctx->loc, 0x12, g_merc == 0 ? 1 : 0,
                                    0x1d0, ybase + 0x98,
                                    ctx->title_font, ctx->glyph_table, 0x3f);
        }
    }

    /* Bottom line: "N men already under arms" + the confirm label. */
    gfx_text_set_line_x_acc(0);
    gfx_text_label_render_dec(rec_i32(strat, 0x138), '@', "", 0x70,
                              (rows - 2) * 0x10 + ybase + 4,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x45, 0xe,
                            gfx_text_line_x_acc() + 0x70,
                            (rows - 2) * 0x10 + ybase + 4,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x45, 9, 0x180,
                            (rows - 2) * 0x10 + ybase + 4,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    /* DAT_004eb260 = 1 — seam-owned redraw bookkeeping. */

    gfx_unlock_canvas();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Screen 0x0d — the weapon assign overlay
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── FUN_0043582a head — the arm_grid.pl8 muster-figure zone lookup ──────*/
int engine_armory_zone_at(int cursor_x, int cursor_y)
{
    if (!g_armory_grid) {
        return 0;
    }
    /* grid[(x>>3) + (y>>3)*0x50] with retail's round-toward-zero shifts. */
    int cx = (cursor_x + ((cursor_x >> 31) & 7)) >> 3;
    int cy = (cursor_y + ((cursor_y >> 31) & 7)) >> 3;
    return g_armory_grid[ARMORY_GRID_HDR + cx + cy * 0x50];
}

/* ── FUN_004358b0 — muster-figure / tally-readout click ──────────────────*/
int engine_armory_zone_select(int zone, const engine_armory_ctx_t *ctx)
{
    int owner = ctx->county_rec[0x05];
    uint8_t *row = engine_game_init_troop_pool_row(owner);
    if (rec_i32(row, zone * 0x10 + 8) <= 0) {
        return 0;                          /* the weapon has no stock */
    }
    /* Commit the march check for the PREVIOUSLY selected weapon, then
     * select the new one and snapshot its count (the next compare). */
    engine_armory_march_trigger(g_weapon, ctx);
    g_weapon = zone;
    put_i32(row, zone * 0x10 + 0xc, rec_i32(row, zone * 0x10 + 0));
    return 1;                              /* caller: screen 0x0d + render */
}

/* The per-weapon demo sheets (FUN_004184c6's filename dispatch). */
static const char *const k_weapon_sheet_file[7] = {
    "arm_cros.pl8",                        /* default (weapon out of range) */
    "arm_cros.pl8", "arm_mace.pl8", "arm_swor.pl8",
    "arm_pike.pl8", "arm_bow.pl8",  "arm_mail.pl8",
};

/* ── FUN_004184c6 — the assign overlay OPEN render ───────────────────────*/
void engine_armory_assign_render(const engine_armory_ctx_t *ctx)
{
    io_path_chdir_base();                  /* FUN_004af502 */
    /* FUN_004b1de0 no-op; FUN_0045240a dirty mark, seam-owned. */

    if (!g_armory_weapon_sheet) {
        g_armory_weapon_sheet = malloc(ARMORY_WEAPON_LEN);
    }
    if (g_armory_weapon_sheet) {
        int w = (g_weapon >= 1 && g_weapon <= 6) ? g_weapon : 0;
        io_read_file_at(k_weapon_sheet_file[w], g_armory_weapon_sheet,
                        ARMORY_WEAPON_LEN, 0);
    }

    if (!gfx_lock_canvas()) {
        return;
    }
    if (ctx->chrome) {                     /* FUN_00409397(0x60,4,0x1a,7) */
        gfx_panel_draw(ctx->chrome, 0x60, 4, 0x1a, 7);
    }
    if (ctx->system_sheet) {               /* FUN_0040d1bc(0x1e4,0x58,0) */
        gfx_sprite_seal_draw(ctx->system_sheet, 0x1e4, 0x58, 0);
    }
    gfx_line_rect_bevel_dark_br(0x65, 9, 0x66, 0x66);
    if (g_armory_weapon_sheet) {           /* the demo figure, frame 0 */
        gfx_sprite_render_from_blob(g_armory_weapon_sheet, 0, 0x66, 10);
    }
    gfx_unlock_canvas();
    /* Retail tail-calls FUN_00418e2d here — the caller runs
     * engine_armory_assign_refresh right after (same frame).
     * DAT_004eb260 = 1 — seam-owned. */
}

/* ── FUN_00418e2d — the assign overlay per-frame refresh ─────────────────
 * CALLER holds the canvas lock. */
void engine_armory_assign_refresh(const engine_armory_ctx_t *ctx,
                                  int demo_frame)
{
    if (g_armory_weapon_sheet) {
        gfx_sprite_render_from_blob(g_armory_weapon_sheet, demo_frame,
                                    0x66, 10);
    }
    int owner = ctx->county_rec[0x05];
    const uint8_t *row = ctx->troop_pool + (size_t)owner * POOL_STRIDE;
    int cur = rec_i32(row, g_weapon * 0x10 + 0);
    int avail = rec_i32(row, g_weapon * 0x10 + 8) - cur;
    if (rec_i32(row, 0) < avail) {
        avail = rec_i32(row, 0);           /* capped by the unassigned men */
    }

    gfx_text_set_line_x_acc(0);
    if (ctx->chrome) {                     /* FUN_00409c93(0xec,0xc,0xf,2) */
        gfx_panel_fill_interior_chrome(ctx->chrome, 0xec, 0xc, 0xf, 2);
    }
    armory_num_units(ctx, cur, g_weapon * 2 + 0x34, 0xec, 0xd,
                     ctx->title_font, 0x3f);

    gfx_text_set_line_x_acc(0);
    if (ctx->chrome) {                     /* FUN_00409c93(0xd8,0x4c,0x10,2) */
        gfx_panel_fill_interior_chrome(ctx->chrome, 0xd8, 0x4c, 0x10, 2);
    }
    gfx_text_label_render_dec(avail, '@', "", 0xd8, 0x54,
                              ctx->menu_font, ctx->glyph_table, 0x3f);
    gfx_wrap_localized_line(ctx->loc, 0x45, 5,
                            gfx_text_line_x_acc() + 0xd8, 0x54,
                            ctx->menu_font, ctx->glyph_table, 0x3f);
    /* FUN_004527a6 dirty marks — seam-owned. */
}

/* The assign button strip (retail DAT_004dd8c8, r2-extracted; origin
 * (0x60,4)): +1 / −1 / none / all — type-4 hold-to-repeat gauntlets. */
static engine_button_rec_t k_assign_buttons[4] = {
    { 0x0b0, 0x2c, 0x44, 0x20, 0x43593a, 0, 0, 0, 4, 0, 0 },
    { 0x0d0, 0x2c, 0x42, 0x20, 0x4359bc, 0, 0, 0, 4, 0, 0 },
    { 0x0f0, 0x2c, 0x3a, 0x20, 0x435a0c, 0, 0, 0, 4, 0, 0 },
    { 0x110, 0x2c, 0x3c, 0x20, 0x435a61, 0, 0, 0, 4, 0, 0 },
};

engine_button_rec_t *engine_armory_assign_buttons(void)
{
    return k_assign_buttons;
}

/* ── the four assign actions (FUN_0043593a / 4359bc / 435a0c / 435a61) ───*/
void engine_armory_assign_plus(uint8_t *pool_row)
{
    if (rec_i32(pool_row, g_weapon * 0x10) <
            rec_i32(pool_row, g_weapon * 0x10 + 8) &&
        rec_i32(pool_row, 0) != 0) {
        put_i32(pool_row, g_weapon * 0x10,
                rec_i32(pool_row, g_weapon * 0x10) + 1);
        put_i32(pool_row, 0, rec_i32(pool_row, 0) - 1);
    }
}

void engine_armory_assign_minus(uint8_t *pool_row)
{
    if (rec_i32(pool_row, g_weapon * 0x10) > 0) {
        put_i32(pool_row, g_weapon * 0x10,
                rec_i32(pool_row, g_weapon * 0x10) - 1);
        put_i32(pool_row, 0, rec_i32(pool_row, 0) + 1);
    }
}

void engine_armory_assign_none(uint8_t *pool_row)
{
    while (rec_i32(pool_row, g_weapon * 0x10) > 0) {
        put_i32(pool_row, g_weapon * 0x10,
                rec_i32(pool_row, g_weapon * 0x10) - 1);
        put_i32(pool_row, 0, rec_i32(pool_row, 0) + 1);
    }
}

void engine_armory_assign_all(uint8_t *pool_row)
{
    while (rec_i32(pool_row, 0) != 0 &&
           rec_i32(pool_row, g_weapon * 0x10) <
               rec_i32(pool_row, g_weapon * 0x10 + 8)) {
        put_i32(pool_row, g_weapon * 0x10,
                rec_i32(pool_row, g_weapon * 0x10) + 1);
        put_i32(pool_row, 0, rec_i32(pool_row, 0) - 1);
    }
}

/* The 0x0a/0x0d view hotspot table (retail DAT_004dc938, r2-extracted).
 * Actions are engine.c-owned (they navigate); the params carry the retail
 * FUN_0040e3ee DAT_0059154c value: recs 0..5 = the tally weapon id (screen
 * order 1,2,4,6,5,3), recs 6..8 = Create/Change/Cancel (1/2/3). */
static const engine_menu_hotspot_t k_view_hotspots[9] = {
    { 0x055, 0x18c, 0x0a1, 0x1e0, NULL, 1, 1 },
    { 0x0a2, 0x18c, 0x0ec, 0x1e0, NULL, 1, 2 },
    { 0x0ed, 0x18c, 0x136, 0x1e0, NULL, 1, 4 },
    { 0x137, 0x18c, 0x182, 0x1e0, NULL, 1, 6 },
    { 0x183, 0x18c, 0x1cd, 0x1e0, NULL, 1, 5 },
    { 0x1cd, 0x18c, 0x21a, 0x1e0, NULL, 1, 3 },
    { 0x21e, 0x18c, 0x27a, 0x1a8, NULL, 1, 1 },
    { 0x21e, 0x1a9, 0x27a, 0x1c2, NULL, 1, 2 },
    { 0x21e, 0x1c2, 0x27a, 0x1df, NULL, 1, 3 },
};

const engine_menu_hotspot_t *engine_armory_view_hotspots(void)
{
    return k_view_hotspots;
}

/* ═══════════════════════════════════════════════════════════════════════
 * The troop-march animation
 * ═══════════════════════════════════════════════════════════════════════ */

/* March target x per weapon (retail .rdata DAT_004de6f0, r2-extracted). */
static const int k_march_target[8] = {
    0, 0x2d, 0x78, 0x1ea, 0xaa, 0x17c, 0x10e, 0x32,
};

/* The troop sheet filename parts (retail table 0x4de450: stride 0x70 per
 * heraldry color, 0x10 per weapon — composed here, same selection). */
static const char *const k_troop_tag[7] =
    { "xb", "xb", "ma", "sw", "pi", "ar", "kn" };
static const char k_troop_color[6] = { 'r', 'r', 'y', 'k', 'p', 'b' };

/* ── FUN_004aabd8 — arm the march when the previous weapon GAINED men ────*/
void engine_armory_march_trigger(int prev_weapon,
                                 const engine_armory_ctx_t *ctx)
{
    if (prev_weapon <= 0) {
        return;
    }
    int owner = ctx->county_rec[0x05];
    const uint8_t *row = ctx->troop_pool + (size_t)owner * POOL_STRIDE;
    if (rec_i32(row, prev_weapon * 0x10 + 0xc) >=
        rec_i32(row, prev_weapon * 0x10 + 0)) {
        return;                            /* no men added — no march */
    }
    if (g_march_active >= 1) {
        return;                            /* one troop at a time */
    }
    g_march_active = 1;
    g_march_target = k_march_target[prev_weapon];
    g_march_land   = 1;
    g_march_x      = -0x50;
    g_march_y      = 0xd8;
    g_march_sprite = 0;
    g_march_walk   = 0;
    /* _DAT_0057c8e4 = 0 — glint phase, write-only in this family. */

    /* Port-order compensation (quirk #214 family): retail's per-frame
     * refresh (FUN_004ba26e, tick line 99) runs BEFORE the input dispatch
     * (FUN_0042ff10, line 192), so a march armed by this click first
     * advances/draws on the NEXT tick's refresh.  The port's input step
     * runs before its render seam, which would advance the march one
     * refresh early — with the every-other-tick 20 ms move cadence that
     * compounds to a permanent 8 px lead.  Skip the arm tick's step. */
    g_march_skip_arm_tick = 1;

    if (!g_armory_troop) {
        g_armory_troop = malloc(ARMORY_TROOP_LEN);
    }
    if (!g_armory_troop) {
        g_march_active = 0;
        return;
    }
    int color = ctx->strategic_block[(size_t)ctx->local_player * STRAT_STRIDE
                                     + 0x0a];
    if (color < 0 || color > 5) {
        color = 0;
    }
    char name[16];
    snprintf(name, sizeof name, "trp_%s_%c.pl8",
             k_troop_tag[prev_weapon], k_troop_color[color]);
    if (!io_read_file_at(name, g_armory_troop, ARMORY_TROOP_LEN, 0)) {
        io_read_file_at("trp_xb_b.pl8", g_armory_troop, ARMORY_TROOP_LEN, 0);
    }
}

/* ── FUN_00418fc5 — restore the backdrop strip under the troop ───────────
 * CALLER holds the canvas lock. */
void engine_armory_march_restore(void)
{
    if (g_march_active <= 0 || !g_armory_strips) {
        return;
    }
    int seg;
    if (g_march_x < 0xa0)        seg = 0;
    else if (g_march_x < 0x140)  seg = 1;
    else if (g_march_x < 0x1e0)  seg = 2;
    else                         seg = 3;
    engine_workforce_hud_restore_backdrop(
        g_armory_strips + k_strip_seg[seg].off_dwords,
        k_strip_seg[seg].x, ARMORY_STRIP_Y,
        k_strip_seg[seg].w_dwords, ARMORY_STRIP_ROWS, GFX_RENDER_W);
}

/* ── FUN_004190db — advance + draw the marching troop ────────────────────
 * CALLER holds the canvas lock. */
void engine_armory_march_step(int tick_20ms, int tick_80ms)
{
    if (g_march_active <= 0) {
        return;
    }
    if (g_march_skip_arm_tick) {
        /* Retail never runs the step on the arm tick (its refresh already
         * ran before the arming click) — no advance, no draw. */
        g_march_skip_arm_tick = 0;
        return;
    }
    if (g_march_x >= 0x280) {
        g_march_active = 0;                /* walked off — despawn */
        return;
    }
    if (tick_20ms != 0) {
        if (tick_80ms != 0) {
            if (g_march_target <= g_march_x && g_march_land != 0) {
                g_march_land = g_march_land + 1;
            }
            g_march_walk = g_march_walk + 1;
            if (g_march_walk > 7) {
                g_march_walk = 0;
            }
        }
        if (g_march_x < g_march_target || g_march_land == 0) {
            /* Walking: frames 0..7 inbound, +0xd outbound (past the rack). */
            if (g_march_x < g_march_target) {
                g_march_x += 4;
                g_march_sprite = g_march_walk;
            } else {
                g_march_x += 4;
                g_march_sprite = g_march_walk + 0xd;
            }
        } else {
            /* Saluting at the rack: frames 8..0xc, then walk out. */
            g_march_sprite = g_march_land / 3 + 8;
            if (g_march_sprite > 0xc) {
                g_march_walk = 0;
                g_march_land = 0;
            }
        }
    }
    if (g_armory_troop) {                  /* FUN_0040a3d0 */
        gfx_sprite_render_rle_from_blob(g_armory_troop, g_march_sprite,
                                        g_march_x, g_march_y);
    }
    /* FUN_004527a6 dirty mark — seam-owned. */
}
