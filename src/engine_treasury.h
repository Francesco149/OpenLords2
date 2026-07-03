/*
 * OpenLords2 — the Treasury / Court screen (screen 0x09).
 *
 * The "Court of <lord>" report inside the merchant county-interior: the
 * player's gold, raw materials (iron/stone/wood), weapon stocks, and the
 * two seasonal totals (army wages, expected tax revenue).  Reached from
 * the county-interior tab strip (retail's merchant button table
 * DAT_004dd808 → FUN_0043ae30 param 2), which just sets screen = 9 and
 * requests a redraw — NO backdrop / palette / data load.  It draws OVER
 * the retained merchant scene (merchant.pl8 / merchant.256), so retail's
 * FUN_0040f1a0 case 9 is a bare FUN_00416925() with no scene reload.
 *
 * Retail address map:
 *   FUN_00416925 @ 0x416925 (974 B) → engine_treasury_render (the panel:
 *                                     bevel, seal, Court-of + name, Gold,
 *                                     Iron/Stone/Wood + icons, 6 weapons,
 *                                     two totals; sets widget count = 1)
 *   FUN_004ba26e case 9             → engine_treasury_refresh (the per-frame
 *                                     seal-button strip, DAT_004dd928[0])
 *
 * Every figure is read from SAVED state — there is no model to compute.
 * Per-record field offsets (all read-only):
 *   strategic (g_player_strategic_352, stride 0x160, key = s_slot_0057c8cc)
 *     +0x0fc i32 total army wages      +0x118 i32 gold (crowns)
 *     +0x120 iron   +0x128 stone   +0x130 wood       (icons 0x2c/0x2d/0x2e)
 *     +0x140 + i*4  weapon[i] i=0..5                  (icons 0x30 + i)
 *     +0x15c i32 expected tax revenue
 *   realm names (g_player_aux_44 + 4 = retail DAT_00553d54, stride 0x2c)
 *     the "Court of <name>" string, key = local player
 */

#ifndef OPENLORDS2_ENGINE_TREASURY_H
#define OPENLORDS2_ENGINE_TREASURY_H

#include <stdint.h>

#include "engine_button.h"
#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_trade / engine_labor: a
 * NULL pointer skips the draw that needs it). */
typedef struct engine_treasury_ctx {
    const uint8_t *strategic_block;    /* g_player_strategic_352 (0x57bf00) */
    const uint8_t *realm_names;        /* g_player_aux_44 + 4 (0x553d54, stride 0x2c) */
    int local_player;                  /* s_slot_0057c8cc (spectate → same) */

    const uint8_t *chrome;             /* panels.pl8 (gfx_panel_draw_bevel sheet) */
    const uint8_t *misc_sheet;         /* misc_cty.pl8 (DAT_005530c8 resource/weapon icons) */
    const uint8_t *system_sheet;       /* system2.pl8 (decorative seal + the per-frame button) */
    const uint8_t *title_font;         /* fntl2_22 (DAT_005b2fa0) */
    const uint8_t *menu_font;          /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
} engine_treasury_ctx_t;

/* ── FUN_00416925 — the OPEN render of the Court/Treasury (screen 0x09) ──
 * Draws the panel over the retained merchant frame (NO backdrop/palette),
 * sets the router widget count (DAT_00568468 = 1), then paints the single
 * per-frame seal button so the open frame carries it (retail's per-frame
 * FUN_004ba26e paints it the same tick, AFTER this demand render). */
void engine_treasury_render(const engine_treasury_ctx_t *ctx);

/* ── FUN_004ba26e case 9 — the per-frame seal-button refresh ─────────────
 * Re-renders the single router widget (the continue seal, DAT_004dd928[0])
 * over the retained panel every tick so its press-flash animates.  NO panel
 * redraw.  Called every tick while screen 0x09 is open. */
void engine_treasury_refresh(const engine_treasury_ctx_t *ctx);

/* The screen-9 router widget table (retail DAT_004dd928) + its live count
 * (DAT_00568468, set to 1 by the render).  Exposed for the input router
 * (the seal click → FUN_004351c4, deferred: it flips to Rankings 0x20). */
engine_button_rec_t *engine_treasury_buttons(void);
int  engine_treasury_widget_count(void);

#endif /* OPENLORDS2_ENGINE_TREASURY_H */
