/*
 * OpenLords2 — the Send Supplies screen (screen 0x18).
 *
 * Reached from the strategic map's bottom-right county-action tab strip
 * (retail table 0x4dc680 rec2 → FUN_0043ae30 param 3, owner-gated — the
 * same strip that opens the Armory (rec0) and Court/Treasury (rec1)).  It
 * draws OVER a recomposited MAP frame (FUN_0041ad5d opens with FUN_004050c0,
 * exactly like the county detail panels), so the port's demand arm runs the
 * strat composite first, then paints the parchment panel on top.
 *
 * The screen stages a goods shipment (grain + cattle) from the current county
 * to a target county, then — on the "Dispatch shipment?" seal — spawns a
 * type-4 supply CARAVAN (an army-movement entity) that carries the load and
 * debits it from the source immediately.  That commit reuses the SAME executor
 * the AI ships caravans through (engine_army_ai_dispatch_caravan = FUN_004292af)
 * over the SAME staging array (g_caravan_order / retail DAT_005678c0), so the
 * player and AI paths are byte-identical — mirroring how the trade grid reuses
 * engine_army_ai_execute_trade.
 *
 * Retail address map:
 *   FUN_0041ad5d @ 0x41ad5d (325 B) → engine_sendsupplies_render  (the panel:
 *                                     bevel, county thumbnail, title, From/To +
 *                                     county names, "Dispatch shipment?")
 *   FUN_0041aea2 @ 0x41aea2 (369 B) → engine_sendsupplies_refresh (the per-frame
 *                                     goods table: Grain / Cattle have + to-send
 *                                     figures + icons; FUN_004ba26e case 0x18)
 *   FUN_0043b27a / FUN_0043b1ca     → arrow_up / arrow_down (have↔to-send ±1/±10)
 *   FUN_0043b32a                    → toggle_all (send-all / return-all)
 *   FUN_0043b412 @ 0x43b412         → target_select (click the thumbnail → county)
 *   FUN_0043b145 @ 0x43b145 (133 B) → commit (dispatch caravan + recompute source)
 *   FUN_0043ae30 param 3            → seed (stage the county's grain/cattle)
 *   FUN_0043ae30 @ 0x43ae30 (540 B) → engine_county_action_open (the whole tab
 *                                     strip, retail layout)
 * The model (all but the two renders + the button tables) is
 * engine_sendsupplies_model.c.
 *
 * The staging array g_caravan_order[player] (retail &DAT_005678c0, stride 0x18
 * = 6 int32) holds, per player: [0/1] grain have/to-send, [2/3] sheep (unused —
 * no UI arm), [4/5] cattle have/to-send.  The good index (retail DAT_0059154c)
 * is 0 (grain) or 2 (cattle); have = ord[good*2], to-send = ord[good*2+1].
 */

#ifndef OPENLORDS2_ENGINE_SENDSUPPLIES_H
#define OPENLORDS2_ENGINE_SENDSUPPLIES_H

#include <stdint.h>

#include "engine_button.h"
#include "engine_event_queue.h"
#include "engine_menu_input.h"
#include "localization.h"

/* Retail globals (engine_economy_globals.c), driven by the model
 * (engine_sendsupplies_model.c). */
extern int32_t g_send_target;   /* DAT_00553c78 — the shipment's destination county */
extern int32_t g_castle_tier;   /* DAT_0056d898 — the castle screen's selected tier (the castle tab) */

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_treasury / engine_trade:
 * a NULL asset pointer skips the draw that needs it). */
typedef struct engine_sendsupplies_ctx {
    int county;                 /* s_slot_0057c93c — source (current) county */
    int target;                 /* g_send_target (DAT_00553c78) — destination */
    int map_id;                 /* s_slot_0053f034 — county-name group base */
    int local_player;           /* s_slot_0057c8cc — owner of the staging order */

    const uint8_t *chrome;      /* panels.pl8 — bevel + goods-table interior */
    const uint8_t *misc_sheet;  /* misc_cty.pl8 (DAT_005530c8) — grain/cattle icons */
    const uint8_t *title_font;  /* fntl2_22 (DAT_005b2fa0) — the title line */
    const uint8_t *menu_font;   /* fntl2_14 (DAT_005af8f0) — everything else */
    const uint8_t *glyph_table;         /* render map (gfx_glyph_render_map) */
    const unsigned char *glyph_metrics; /* measure map (gfx_glyph_measure_map) — wrap widths */
    localization_state_t *loc;
} engine_sendsupplies_ctx_t;

/* ── FUN_0041ad5d — the OPEN render (drawn over the recomposited map) ──── */
void engine_sendsupplies_render(const engine_sendsupplies_ctx_t *ctx);

/* ── FUN_0041aea2 — the per-frame goods table (grain/cattle figures) ───── */
void engine_sendsupplies_refresh(const engine_sendsupplies_ctx_t *ctx);

/* ── Interaction (all mutate g_caravan_order[local player]) ────────────── */
void engine_sendsupplies_arrow_up(int good);     /* FUN_0043b27a: have→send */
void engine_sendsupplies_arrow_down(int good);   /* FUN_0043b1ca: send→have */
void engine_sendsupplies_toggle_all(int good);   /* FUN_0043b32a: all / none */

/* FUN_0043ae30 param-3 body: stage the county's grain/cattle into the order,
 * zero the to-send columns, and default the target to the source county. */
void engine_sendsupplies_seed(int player, int source_county);

/* FUN_0043b412(ox, oy) (the caller: 0x60, 0x68): on an LMB press inside the
 * 0x80×0x80 thumbnail at (ox, oy), map the cursor to the region grid and, if it
 * names a county, retarget the shipment.  Returns 1 when a target was selected
 * (consumes the click). */
int engine_sendsupplies_target_select(int ox, int oy, int cursor_x, int cursor_y,
                                      int lmb_pressed);

/* FUN_0043ae30 — the county-action tab strip's action (`action` = the tab's
 * param, retail DAT_0059154c): 1 armory, 2 treasury, 3 send supplies, 4 castle,
 * 5 diplomacy; owner-gated tabs post notice 0x70 through `q`.  Retail layout
 * (sets the screen byte / demand redraw); engine.c's tab arms are the port's
 * own router.  Seams (default none): the WAV cue (0x427990), the army voice
 * cue (0x4b3714), the armory's entry (conscripts FUN_00435ebc, muster pool
 * FUN_004aa90a, march / merc off DAT_005679d0 / DAT_0055446c). */
void engine_county_action_open(int action, engine_event_queue_state_t *q);
void engine_county_action_bind_wav_fn(void (*fn)(const char *, int, int));
void engine_county_action_bind_voice_fn(void (*fn)(int));
void engine_county_action_bind_armory_fn(void (*fn)(int county));

/* FUN_0043b145: dispatch the staged caravan from `source` to `target` and
 * recompute the source county's economy (the same 6-step recompute retail
 * runs after the shipment).  Caller gates target != source. */
void engine_sendsupplies_commit(int player, int source, uint8_t target);

/* Current destination county (retail DAT_00553c78). */
int engine_sendsupplies_target(void);

/* The 6-button widget table (retail DAT_004dd538): [0] Send seal / [1] Cancel
 * seal (both action 0x43b04c, param 1/0) + grain/cattle down/up arrows. */
engine_button_rec_t *engine_sendsupplies_buttons(void);

/* The 2 "send-all / return-all" thumbnail-row hotspots (retail DAT_004dc7e8,
 * FUN_0040e3ee at origin 0x40,0x30): grain (param 0) / cattle (param 2). */
const engine_menu_hotspot_t *engine_sendsupplies_all_hotspots(void);

#endif /* OPENLORDS2_ENGINE_SENDSUPPLIES_H */
