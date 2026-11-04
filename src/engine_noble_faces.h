/*
 * OpenLords2 — the Noble Faces screen (screen 0x0b): the diplomacy
 * partner-select roster.
 *
 * Reached from the strategic map's bottom-right county-action tab strip
 * (retail table 0x4dc680 rec4 → FUN_0043ae30 param 5), the SAME strip that
 * opens the Armory (rec0, param 1), Court/Treasury (rec1, param 2) and Send
 * Supplies (rec2, param 3).  Unlike the owner-gated Armory/Send-Supplies tabs,
 * the diplomacy arm (FUN_0043611b) is a bare screen set — no county gate.
 *
 * Like the Court it draws OVER the retained MAP frame (the entry sets
 * DAT_0057d340 = 2, so the port's demand arm recomposites the strat frame
 * first, then paints the parchment panel on top — no backdrop/palette swap).
 *
 * The panel shows one portrait cell per LIVING non-local player (their face
 * from faces.pl8, a mood icon, war/alliance status icons and — for AI lords —
 * a relation meter bar), the current partner's realm name, and a set of
 * diplomacy-action buttons whose membership depends on the standing relation
 * (no-lord / I-am-lord / has-other-lord / allied).  Selecting an action opens
 * the diplomacy-action screen 0x1a (FUN_00436141 … 0x43631e) — DEFERRED
 * to a later port; this screen renders byte-exact and closes back to the map.
 *
 * Retail address map:
 *   FUN_00416cf3 @ 0x416cf3 (1275 B) → engine_noble_faces_render (the panel:
 *                                      bevel, seal, faces.pl8 load, the face
 *                                      grid, the partner name, the action
 *                                      labels + relation-state button count)
 *   FUN_004171ee @ 0x4171ee (1709 B) → engine_noble_faces_draw_face (one portrait cell)
 *   FUN_004ba26e case 0x0b           → engine_noble_faces_refresh (the per-
 *                                      frame action-button sprite strip,
 *                                      FUN_0040cfd2 over DAT_004dd940)
 *   FUN_004a1e6c                     → player_first_active_nonlocal (default
 *                                      partner pick; already ported, roster)
 *   DAT_004dd940                     → engine_noble_faces_buttons (6 records,
 *                                      the 0x1a-mode entry actions)
 *
 * The relationship state (retail DAT_00553f38, save entry 142) and the current
 * partner (DAT_00553fbc, entry 143) are save-schema globals threaded via the
 * shared externs, so both engines stay save-identical; the action-button count
 * (DAT_00553ee8) is transient UI (engine_noble_faces_globals.c).
 */

#ifndef OPENLORDS2_ENGINE_NOBLE_FACES_H
#define OPENLORDS2_ENGINE_NOBLE_FACES_H

#include <stdint.h>

#include "engine_button.h"
#include "engine_dirty_rect.h"
#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_treasury / engine_rankings:
 * a NULL asset pointer skips the draw that needs it).  strategic_block is
 * NON-const: the per-face draw lazily normalizes each noble's mood byte
 * (+0x0a) in place, exactly as retail FUN_004171ee does. */
typedef struct engine_noble_faces_ctx {
    uint8_t       *strategic_block;   /* g_player_strategic_352 (DAT_0057bf00) */
    const uint8_t *realm_names;       /* g_player_aux_44 + 4 (DAT_00553d54, stride 0x2c) */
    int            local_player;      /* s_slot_0057c8cc */

    const uint8_t *chrome;            /* panels.pl8 — the panel bevel */
    const uint8_t *misc_sheet;        /* misc_cty.pl8 (DAT_005530c8) — mood + decor icons */
    const uint8_t *system_sheet;      /* system2.pl8 — the seal + action-button sprites */
    const uint8_t *title_font;        /* fntl2_22 (DAT_005b2fa0) — the partner name */
    const uint8_t *menu_font;         /* fntl2_14 (DAT_005af8f0) — everything else */
    const uint8_t *glyph_table;         /* render map (gfx_glyph_render_map) */
    const unsigned char *glyph_metrics; /* measure map (gfx_glyph_measure_map) — action wrap */
    localization_state_t *loc;
    uint8_t       *faces_scratch;     /* the scratch DAT_004eabec points at (rhost); NULL → the port's own */
    engine_dirty_rect_state_t *dirty; /* FUN_0045240a's rectangle; NULL → no mark */
} engine_noble_faces_ctx_t;

/* Retail-layout globals (engine_noble_faces_globals.c; rhost aliases them at their VAs). */
extern engine_button_rec_t g_noble_buttons[6];   /* DAT_004dd940: the 6 diplomacy-action buttons */
extern int32_t g_noble_action_count;             /* DAT_00553ee8: how many of them show */

/* ── FUN_004171ee — one noble's portrait cell (player p, grid row) ─────── */
void engine_noble_faces_draw_face(const engine_noble_faces_ctx_t *ctx, int p, int row);

/* ── FUN_00416cf3 — the OPEN render (drawn over the recomposited map) ──── */
void engine_noble_faces_render(const engine_noble_faces_ctx_t *ctx);

/* ── FUN_004ba26e case 0x0b — the per-frame action-button sprite strip ─── */
void engine_noble_faces_refresh(const engine_noble_faces_ctx_t *ctx);

/* The 6-button action table (retail DAT_004dd940): x=0x190, y=0x66+i*0x32,
 * sprite 0x40 (system sheet), type 5 (press-latch); the click actions are the
 * 0x1a-mode entry functions.  The live count (0/3/4/6, by relationship state)
 * is engine_noble_faces_button_count(), set by the render. */
engine_button_rec_t *engine_noble_faces_buttons(void);
int engine_noble_faces_button_count(void);

#endif /* OPENLORDS2_ENGINE_NOBLE_FACES_H */
