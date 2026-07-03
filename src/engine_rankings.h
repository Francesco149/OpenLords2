/*
 * OpenLords2 — the Rankings / "Greatest Nobles" scoreboard (screen 0x20).
 *
 * The standings screen reached from the Treasury/Court seal (retail
 * FUN_004351c4 sets screen = 0x20).  A full-screen scene: it loads its own
 * grtnoble.pl8 backdrop + grtnoble.256 palette + flags.pl8 banner sheet
 * (NOT drawn over a retained frame, unlike the Court), draws one banner +
 * a shaded score bar per player scaled to the current ranking category,
 * then the category caption + the leader's name (or "undecided." on a tie)
 * and the bottom-right continue seal.
 *
 * Retail address map:
 *   FUN_0041593b @ 0x41593b (673 B) → engine_rankings_render (the scene:
 *                                     asset loads, per-player banner+bar
 *                                     loop, caption + winner name, seal)
 *   FUN_00415bdc @ 0x415bdc (614 B) → rankings_compute (winner + the 0..100
 *                                     bar-height percentages + tie flags)
 *   FUN_00415e42 @ 0x415e42 (348 B) → rankings_metric (the per-category
 *                                     per-player raw stat, all saved reads)
 *   FUN_00404dc1                    → engine_math_percent_div (a*100/b)
 *   FUN_0040a682                    → gfx_sprite_render (banner from the
 *                                     flags.pl8 blob) / FUN_00403a8f →
 *                                     gfx_line_draw (the 4px bar columns)
 *
 * There is NO per-frame refresh (FUN_004ba26e has no case 0x20): the
 * category tabs are baked into the backdrop and a tab click just re-runs
 * this render.  Every figure is read from SAVED state — no model computes.
 *
 * The seven ranking categories (grp 0x23) and their strategic-record field
 * (g_player_strategic_352, stride 0x160, per player 1..5):
 *   0 "Most counties,"   +0x29 u8      4 "Happiest people," +0x0c u8
 *   1 "Most castles,"    +0x4c u8      5 "Most people,"     +0x10 u32
 *   2 "Most troops,"     +0x54 u32     6 "Greatest noble,"  turn-gated
 *   3 "Most crowns,"     +0x118 u32 (gold)                  (+0x2b u8)
 *   7 "undecided."  = the tie caption (no winner name)
 */

#ifndef OPENLORDS2_ENGINE_RANKINGS_H
#define OPENLORDS2_ENGINE_RANKINGS_H

#include <stdint.h>

#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_merchant / engine_trade:
 * a NULL loc skips the whole draw).  The full-screen assets (grtnoble
 * backdrop/palette, flags sheet) are loaded inside the render, not passed. */
typedef struct engine_rankings_ctx {
    const uint8_t *strategic_block;    /* g_player_strategic_352 (0x57bf00) */
    const uint8_t *realm_names;        /* g_player_aux_44 + 4 (0x553d54, stride 0x2c) */
    int category;                      /* s_slot_0055ce7c (0..6, DAT_0055ce7c) */
    int turn;                          /* s_slot_00553edc (year, category 6 gate) */

    const uint8_t *system_sheet;       /* system2.pl8 (the continue seal) */
    const uint8_t *menu_font;          /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    localization_state_t *loc;
} engine_rankings_ctx_t;

/* ── FUN_0041593b — the full Rankings scene render (screen 0x20) ─────────
 * Loads grtnoble.pl8/.256 + flags.pl8, computes the standings, draws the
 * banners + bars + caption + winner name + seal.  Applies the palette last
 * (as the backdrop indices are painted first), matching engine_merchant. */
void engine_rankings_render(const engine_rankings_ctx_t *ctx);

#endif /* OPENLORDS2_ENGINE_RANKINGS_H */
