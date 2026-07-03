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
#include <stdlib.h>

#include "game_init.h"               /* engine_game_init_troop_pool_row */
#include "gfx.h"                     /* backdrop/palette/lock, GFX_RENDER_W/H */
#include "gfx_sprite_render.h"       /* gfx_sprite_render_from_blob, seal */
#include "gfx_text.h"                /* gfx_text_set_outline */
#include "gfx_text_label.h"          /* gfx_text_label_render_dec */
#include "gfx_wrap.h"                /* gfx_wrap_localized_line_centered */
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
    /* DAT_00553f20 (blacksmith weapon), DAT_005679d0 (torch-anim arm),
     * _DAT_0057c8e4 (glint phase) are 0x17-overlay state — deferred. */
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
static void armory_counts(const engine_armory_ctx_t *ctx)
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
    armory_counts(ctx);      /* FUN_004181eb */

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
    /* FUN_004b11ce (transient palette/canvas prep) + FUN_004996c8 (the
     * strategic-map teardown + armory buffer alloc) are DEFERRED — the port
     * keeps the map resident and uses the persistent scratch below, exactly
     * as engine_merchant / engine_rankings defer the retail free/malloc. */

    /* armoury.pl8: the 640×480 backdrop straight into the back buffer
     * (FUN_00408fcb — self-locks).  arm_grid.pl8 (the hit-map) is input-only
     * and not loaded here. */
    gfx_load_background_image("armoury.pl8");

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

    if (gfx_lock_canvas()) {
        armory_scene(ctx);
        gfx_unlock_canvas();
    }

    /* armoury.256 → the active palette last (retail applies FUN_004b0ab5
     * after the draws, so it lands on the settled frame; the screen-0 close
     * arm restores base01.256, same as merchant / rankings). */
    gfx_load_palette("armoury.256");
}
