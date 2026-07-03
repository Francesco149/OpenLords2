/*
 * OpenLords2 — the Armory / army-muster VIEW (screen 0x0a).
 *
 * The county-interior "armory" scene: a full-screen room (armoury.pl8 over
 * armoury.256) decorated with the player's HERALDRY-colored interior sheet
 * (arm_it_r<c>.pl8), the weapons the player owns hung on the racks, the
 * bottom muster tally (men available + a "0" per owned weapon type), the
 * three action labels ("Create" / "Change" / "Cancel"), and the continue
 * seal.  Reached from the map's bottom-right county-action tab strip (table
 * 0x4dc680 rec0 → FUN_0043ae30 param 1, owner-gated): that opens the
 * INTERACTIVE army create/change overlay (screen 0x17, FUN_00418653, drawn
 * OVER this backdrop — deferred); RMB-ing out of 0x17 lands on this bare
 * 0x0a view.  Unlike 0x17 this is a STATIC scene — the torch/glint sweep
 * (FUN_00418fc5 restore + FUN_004190db, gated DAT_005679d0>0) is a 0x17-only
 * animation, so the 0x0a render skips the four backdrop-save strips.
 *
 * Retail address map:
 *   FUN_00417ea7 @ 0x417ea7 (591 B) → engine_armory_render (the scene:
 *                                     palette + backdrop + interior + racks
 *                                     (FUN_00418426) + counts (FUN_004181eb)
 *                                     + Create/Change/Cancel labels + seal)
 *   FUN_004aa90a @ 0x4aa90a (374 B) → engine_armory_muster_init (the on-entry
 *                                     muster-pool setup, run before the render)
 *
 * The counts are pure reads of the per-player troop muster pool
 * (g_troop_pool, retail DAT_0053f6a0, 0x80 stride) that muster_init fills
 * from saved state — there is no model computed at render time.  Per-record
 * field offsets (all read-only at render):
 *   strategic (g_player_strategic_352, stride 0x160, key = local player)
 *     +0x00a u8  heraldry / interior color (0..5 → arm_it_r/y/k/p/b)
 *     +0x13c + t*4  weapon[t] stock, t=1..6  (== treasury's +0x140 + i*4)
 *   muster pool (g_troop_pool, stride 0x80, key = county owner == local)
 *     entry e at +e*0x10:  +0 count (drawn)   +4 stock gate (>0 → shown)
 *     entry 0 = men, entries 1..6 = the six weapon types, entry 7 = total
 */

#ifndef OPENLORDS2_ENGINE_ARMORY_H
#define OPENLORDS2_ENGINE_ARMORY_H

#include <stdint.h>

#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_treasury / engine_rankings:
 * a NULL pointer skips the draw that needs it).  The heraldry-colored
 * interior sheet is loaded by the render itself (indexed off the strategic
 * block), so it is NOT carried here. */
typedef struct engine_armory_ctx {
    const uint8_t *strategic_block;    /* g_player_strategic_352 (0x57bf00) */
    const uint8_t *troop_pool;         /* g_troop_pool (retail 0x53f6a0, stride 0x80) */
    int local_player;                  /* s_slot_0057c8cc (== the county owner) */

    const uint8_t *system_sheet;       /* system2.pl8 (the continue seal) */
    const uint8_t *menu_font;          /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
} engine_armory_ctx_t;

/* ── FUN_004aa90a — the on-entry muster-pool setup ──────────────────────
 * Zeroes the county owner's muster row, seeds the weapon stock gates (+4/+8)
 * from the owner's strategic weapon stocks (rec +0x13c + t*4, t=1..6), and
 * writes `population` (the men conscriptable at the current % — 0 on first
 * entry) into the men/total entries.  Run once when the armory opens, BEFORE
 * the render (the render only reads the pool back). */
void engine_armory_muster_init(int county_owner, int population,
                               const uint8_t *owner_strat_rec);

/* ── FUN_00417ea7 — the OPEN render of the Armory view (screen 0x0a) ─────
 * A FULL-SCREEN scene: loads armoury.pl8 into the back buffer + the player's
 * arm_it_r<c>.pl8 interior sheet, draws the owned-weapon racks, the muster
 * counts, the three action labels and the seal, then applies armoury.256 as
 * the active palette last (so it lands on the settled frame — the screen-0
 * close arm restores base01.256, same as the merchant/rankings scenes). */
void engine_armory_render(const engine_armory_ctx_t *ctx);

#endif /* OPENLORDS2_ENGINE_ARMORY_H */
