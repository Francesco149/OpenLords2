/*
 * OpenLords2 — the diplomacy-action screen (screen 0x1a).
 *
 * Opened from the Noble Faces roster (screen 0x0b): each diplomacy-action
 * button (gift / compliment / insult / alliance …, retail DAT_004dd940) runs an
 * entry function FUN_00436141 … 0x43631e that sets screen 0x1a and a MODE
 * (retail DAT_005651cc, 0..6), recomposites the map, and lets the render
 * dispatcher FUN_0041789b draw the mode's sub-screen over it:
 *
 *   mode 0        FUN_00417960   GIFT  — send crowns to the partner   [PORTED]
 *   modes 1..4    0x417aef   message screens (compliment/insult/…) [DEFERRED]
 *   modes 5..6    0x417cef   ask-ally-to-attack target select      [DEFERRED]
 *
 * This port covers the GIFT screen (mode 0) end-to-end: the panel render, the
 * up/down amount arrows + Yes/No seals (retail DAT_004dd9d0), and the confirm
 * that enqueues the action + transfers the gold (engine_diplomacy_queue_action =
 * FUN_004a2621).  Modes 1..6 need two further subsystems and are DEFERRED:
 *   - the message modes read a runtime-composed message buffer (retail
 *     0x53f1e0, all-zero statically) rendered by 0x417bd3;
 *   - the ally-attack modes drive a target list (retail 0x5530b0).
 *
 * Like the Court / Noble Faces this draws OVER the retained MAP frame — retail's
 * render dispatch case (FUN_0040f1a0 @ 0x40f4d4) is a bare FUN_0041789b and the
 * mode renderers do NOT recomposite; the 0x0b→0x1a entry does (FUN_004050c0),
 * so the port's demand arm recomposites the strat frame first, then the panel.
 *
 * Retail address map:
 *   FUN_0041789b @ 0x41789b       → engine_diplomacy_action_render (mode switch)
 *   FUN_00417960 @ 0x417960 (399 B)→ the gift panel (mode 0)
 *   FUN_00436372 @ 0x436372       → engine_diplomacy_action_arrow (±10 gold)
 *   FUN_00436408 @ 0x436408       → engine_diplomacy_action_seal (every mode; the
 *                                   model, engine_diplomacy_action_model.c);
 *                                   engine_diplomacy_action_confirm (mode-0 apply)
 *   FUN_004ba26e case 0x1a        → engine_diplomacy_action_refresh (button strip)
 *   DAT_004dd9d0                  → engine_diplomacy_action_buttons (mode 0, 4 recs)
 *   DAT_005651cc                  → engine_diplomacy_action_mode (transient UI)
 *   DAT_0057a0f8 (save entry 144) → the staged gift amount (s_slot_0057a0f8)
 *   DAT_00553fbc (save entry 143) → the partner (s_slot_00553fbc)
 */

#ifndef OPENLORDS2_ENGINE_DIPLOMACY_ACTION_H
#define OPENLORDS2_ENGINE_DIPLOMACY_ACTION_H

#include <stdint.h>

#include "engine_button.h"
#include "engine_event_queue.h"
#include "localization.h"
#include "text_input.h"

/* ── Render context (same NULL-guard convention as engine_noble_faces) ──── */
typedef struct engine_diplomacy_action_ctx {
    const uint8_t *strategic_block;   /* g_player_strategic_352 (DAT_0057bf00) */
    const uint8_t *realm_names;       /* g_player_aux_44 + 4 (DAT_00553d54, stride 0x2c) */
    int            local_player;      /* s_slot_0057c8cc */

    const uint8_t *chrome;            /* panels.pl8 — the panel bevel */
    const uint8_t *misc_sheet;        /* misc_cty.pl8 (DAT_005530c8) — button sheet A */
    const uint8_t *system_sheet;      /* system2.pl8 — the seal + arrow/seal sprites */
    const uint8_t *menu_font;         /* fntl2_14 (DAT_005af8f0) — every line */
    const uint8_t *glyph_table;         /* render map (gfx_glyph_render_map) */
    const unsigned char *glyph_metrics; /* measure map (gfx_glyph_measure_map) */
    localization_state_t *loc;
} engine_diplomacy_action_ctx_t;

/* ── FUN_0041789b — mode dispatch: draw the current mode's sub-screen ───── */
void engine_diplomacy_action_render(const engine_diplomacy_action_ctx_t *ctx);

/* ── FUN_004ba26e case 0x1a — per-frame button strip (+ message re-render) ─ */
void engine_diplomacy_action_refresh(const engine_diplomacy_action_ctx_t *ctx);

/* The current mode (retail DAT_005651cc, transient UI — NOT save-tracked) and
 * the "an action was issued" latch DAT_0052afa4: retail globals
 * (engine_economy_globals.c), driven by the model (engine_diplomacy_action_model.c). */
extern int32_t g_diplo_action_mode;    /* DAT_005651cc */
extern int32_t g_diplo_action_taken;   /* DAT_0052afa4 */
int  engine_diplomacy_action_mode(void);
void engine_diplomacy_action_set_mode(int mode);

/* The mutable button table + count for the CURRENT mode (gift = DAT_004dd9d0,
 * 4 recs; message/ally = DAT_004dda30, 2 recs).  Dispatched by the engine.c
 * router over the same table the refresh paints (engine_menu_button_dispatch). */
engine_button_rec_t *engine_diplomacy_action_buttons(void);
int engine_diplomacy_action_widget_count(void);

/* FUN_00436372 — a gift up (param0 == 1) / down (param0 == 0) arrow: step the
 * staged gift amount (s_slot_0057a0f8) by ±10, clamped to [0, local cash];
 * demand redraw 2. */
void engine_diplomacy_action_arrow(int param0);

/* FUN_00436408 confirm side (mode 0): enqueue the gift + transfer the gold
 * (engine_diplomacy_queue_action).  The caller (router) does the screen flip. */
void engine_diplomacy_action_confirm(void);

/* FUN_00436408 in retail layout — the Yes (yes != 0) / No seal for every mode:
 * No → the roster (0x0b); Yes → clear the editor (`ed`, may be NULL), close to
 * the map, copy the mode's message (1..4), validate the target county (5 / 6:
 * notices 0xf0..0xf4 via `q`), refuse a compliment-3 to a vassal (0xdb), and
 * queue the action (FUN_004a2621). */
void engine_diplomacy_action_seal(int yes, text_input_state_t *ed,
                                  engine_event_queue_state_t *q);

#endif /* OPENLORDS2_ENGINE_DIPLOMACY_ACTION_H */
