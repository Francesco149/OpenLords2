/*
 * OpenLords2 — the county control-panel detail screens (the 2×2 action
 * grid): population / happiness / tax / rations.
 *
 * Ports (see docs/findings/gameplay-and-screens.md for the screen map):
 *
 *   FUN_00438ceb @ 0x438ceb ( 326 B) → engine_county_action_grid_classify
 *   FUN_0043a8f2 @ 0x43a8f2 (  48 B) → (flip → 0x14, folded into the hook)
 *   FUN_0043a939 @ 0x43a939 (  23 B) → (flip → 0x15)
 *   FUN_0043a922 @ 0x43a922 (  23 B) → (flip → 0x16)
 *   FUN_0043a846 @ 0x43a846 ( 172 B) → (flip → 0x19 + voice-cue pick,
 *                                       engine_county_rations_voice_cue)
 *   FUN_004b3768 @ 0x4b3768 (  84 B) → (county voice cue s020_0N.wav —
 *                                       cue PLAYBACK unported, see below)
 *   FUN_004156a7 @ 0x4156a7 ( 627 B) → engine_county_graph_render
 *   FUN_0041ac95 @ 0x41ac95 ( 200 B) → engine_county_delta_label
 *   FUN_004110b1 @ 0x4110b1 (~700 B) → engine_county_panel_population
 *   FUN_0041152f @ 0x41152f ( 460 B) → engine_county_panel_tax
 *   FUN_004116fb @ 0x4116fb (~650 B) → engine_county_panel_happiness
 *   FUN_00411b72 @ 0x411b72 (1132 B) → engine_county_panel_rations
 *   FUN_0043a379 @ 0x43a379 ( 560 B) → engine_county_rations_input
 *   FUN_0043a5a9 @ 0x43a5a9 ( 647 B) → engine_county_ration_set
 *
 * These are the four detail panels a player opens from the right-HUD
 * control panel every turn (strategy-guide turn loop step 1): the 2×2
 * button grid at x∈(0x1e7,0x276), y∈(0xb5,0xf1) flips the screen byte to
 * 0x14 (population, top-left) / 0x16 (happiness, top-right) / 0x15 (tax,
 * bottom-left) / 0x19 (rations, bottom-right), each painting a parchment
 * panel over a fresh strategic-map composite.  All four share one input
 * shape (42ff10.c:445-560): HUD sub-handler chain, then ANY click closes
 * back to screen 0 (the whole case body ends in `goto` — the shared
 * tab-strip tail never runs on these screens).
 *
 * The population/happiness panels plot a per-season history bar graph
 * (FUN_004156a7) from the g_realm_history ring (save entry 10, retail
 * 0x56d8c0): 400 turn-slots × 16 county-keyed cells of 8 B (+0 i32
 * population, +4 u8 happiness).  NOTE the retail read base is
 * DAT_0056d8b8 = block − 8 with county keys ≥ 1, i.e. cell address =
 * block + slot*0x80 + (county−1)*8.  Sprites come from graphs.pl8
 * (background 0/1 = population/happiness sheet, bars = alternating
 * bucket/bucket+1 columns trimmed to height via
 * gfx_sprite_render_truncate_bottom).
 *
 * DEFERRED / not ported here:
 *   - The county voice cues (FUN_004b3768 s020_0N.wav on the population
 *     flip; s021_01/02.wav on the rations flip): the WAV cue-playback
 *     path (0x427990) has no port yet (audio.h) — the flips document
 *     the cue + set the g_combat_audio_latch flag, playback is a no-op
 *     (drop-in audio is off by default anyway).
 *   - The multiplayer arm of the ration set (0x43eda0(0x31,0) when
 *     DAT_00553030 != 0) — SP drop-in.
 *
 * Per-county record fields (COUNTY(key) = g_per_army_768 + key*0x300,
 * retail base DAT_0053f9b0; offsets verified against the four renderers):
 *   +0x05 u8  owner realm          +0x09 u8  health level (loc grp 0x14
 *                                             name; also the voice index)
 *   +0x0c s8  happiness this season    +0x0d s8  happiness last season
 *   +0x0f s8  tax→happiness county delta (added to realm +0x28 base)
 *   +0x10 s8  health→happiness delta   +0x11 s8  ration→happiness delta
 *   +0x12..+0x15 s8 happiness deltas: taxes / ration / health / army
 *   +0x16 s8  tax "other counties" delta  +0x17 s8 events delta
 *   +0x18 s8  average happiness
 *   +0x24 i32 population this season    +0x28 i32 population last season
 *   +0x30 i32 births   +0x34 i32 deaths (drawn negated)   +0x38 i32 army
 *   +0x3c i32 emigrants (negated; 0 → "No emigration.")
 *   +0x40 i32 immigrants (0 → "No immigration.")
 *   +0x58 u8  emigration destination county key
 *   +0xb9 u8  tax rate %            +0xc0 i32 expected tax revenue
 *   +0x15d u8 ration level ACHIEVED (loc grp 0x15 name; red if ≠ wanted)
 *   +0x15e u8 ration level WANTED   +0x15f s8 ration percent wanted 0..100
 *   +0x194 i32 ale→happiness delta  +0x250 i32 active gate (ration set)
 *   +0x16c/+0x170/+0x174/+0x178/+0x17c i32 food readouts (rations panel
 *     grid: growing/harvested columns; +0x17c doubles as the ration
 *     set's "fed outcome" probe)
 *   +0x198/+0x19c i32 foraging men (advanced mode only, summed)
 */

#ifndef OPENLORDS2_ENGINE_COUNTY_PANELS_H
#define OPENLORDS2_ENGINE_COUNTY_PANELS_H

#include <stdint.h>

#include "engine_button.h"
#include "engine_dirty_rect.h"
#include "localization.h"

/* ── Shared render context ─────────────────────────────────────────────
 * All pointers may be NULL → the draw that needs them is skipped (same
 * NULL-guard convention as engine_terrain_info / engine_confirm_dialog). */
typedef struct engine_county_panels_ctx {
    const uint8_t *county_block;   /* g_per_army_768 (DAT_0053f9b0 base) */
    const uint8_t *realm_block;    /* g_player_strategic_352 (DAT_0057bf00) */
    const uint8_t *history;        /* g_realm_history (save entry 10) */
    int hist_head;                 /* s_slot_00568da8 — ring head slot */
    int hist_count;                /* s_slot_00553f30 — seasons recorded */
    int hist_start_year;           /* s_slot_00553228 — graph left-edge year */
    int year;                      /* s_slot_00553edc — current year */
    int county;                    /* s_slot_0057c93c — current county key */
    int map_id;                    /* s_slot_0053f034 — county-name index base */
    int advanced_mode;             /* DAT_0053f260 — army-foraging option */

    const uint8_t *chrome;         /* panels.pl8 (gfx_panel_draw sheet) */
    const uint8_t *misc_sheet;     /* misc_cty.pl8 (DAT_005530c8 icons) */
    const uint8_t *graphs;         /* graphs.pl8 (graph bg + bar sprites) */
    const uint8_t *system_sheet;   /* system2.pl8 (continue-seal) */
    const uint8_t *title_font;     /* fntl2_22 (DAT_005b2fa0) */
    const uint8_t *menu_font;      /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
    int (*map_scene)(void);        /* FUN_004050c0, the panels' map recomposite (rhost: retail's) — it may re-pick
                                    * the current county (DAT_0057c93c): returns it; NULL → none */
    int graphs_load;               /* 1: the graph loads graphs.pl8 into `graphs` itself (retail; rhost) */
    engine_dirty_rect_state_t *dirty; /* FUN_0045240a's rectangle; NULL → no mark */
} engine_county_panels_ctx_t;

/* FUN_00409397 — the chrome panel (border mode 0 + interior one tile in), the sheet DAT_0057d3d0. */
void engine_county_chrome_panel(const uint8_t *chrome, int x, int y, int cols, int rows);

/* FUN_0041a900 — a signed number with its era word (modes 0..3, see the .c); FUN_0041ab67 — "N <noun>" (loc grp 8). */
void engine_county_year_number(const engine_county_panels_ctx_t *ctx, int value, int x, int y, int mode);
void engine_county_num_units(const engine_county_panels_ctx_t *ctx, int value, int sub, int x, int y,
                             const uint8_t *font, uint8_t color);

/* FUN_0043aa83 / 0043ab25 (tax ±1) and FUN_0043a23f / 0043a307 (wanted ±1) whole: the step on ctx->county, the
 * panel's re-render, the redraw flags.  (engine_county_tax_step / _wanted_step: the step alone — the standalone's
 * router re-renders itself.) */
void engine_county_tax_step_render(const engine_county_panels_ctx_t *ctx, int dir);
void engine_county_wanted_step_render(const engine_county_panels_ctx_t *ctx, int dir);

/* ── FUN_004156a7 — seasonal history bar graph ─────────────────────────
 * Draw the graph frame + background sprite (`mode` 0 = population sheet
 * sprite 0, 1 = happiness sprite 1) at (x, y), then one bar per recorded
 * season from the history ring (walking `hist_count` slots from
 * `hist_head`, wrapping at 400).  Returns the series MAX (the population
 * panel prints it as "Greatest population N people.").  Bar layout: the
 * per-season column stride and sprite pair come from the baked bucket
 * tables (k_county_graph_* in the .c); bar height = value / y-scale,
 * bottom-anchored at y+0x9a. */
int engine_county_graph_render(const engine_county_panels_ctx_t *ctx,
                               int x, int y, int mode);

/* ── FUN_0041ac95 — "(±N☺)" happiness-delta label ──────────────────────
 * Draws "(" + the signed value (color_pos/color_neg) + the grp-0x17 face
 * icon + ")" at (x, y), preserving the caller's line-x accumulator around
 * the whole group (retail saves/restores DAT_005cd404). */
void engine_county_delta_label(const engine_county_panels_ctx_t *ctx,
                               int value, int x, int y,
                               const uint8_t *font,
                               uint8_t color_pos, uint8_t color_neg);

/* ── The four panel renderers ──────────────────────────────────────────
 * Each paints its parchment panel + content into the bound gfx canvas.
 * The strategic-map composite retail runs first (FUN_004050c0) is the
 * CALLER's job (engine.c's render seam composites, then calls these) —
 * same split as engine_terrain_info_render. */
void engine_county_panel_population(const engine_county_panels_ctx_t *ctx);
void engine_county_panel_tax(const engine_county_panels_ctx_t *ctx);
void engine_county_panel_happiness(const engine_county_panels_ctx_t *ctx);
void engine_county_panel_rations(const engine_county_panels_ctx_t *ctx);

/* ── FUN_00438ceb — the 2×2 action-grid hit-test ───────────────────────
 * Returns the target screen id (0x14 / 0x16 / 0x15 / 0x19) when an LMB
 * press lands in the grid AND the current county is the local player's
 * (owner == current_realm, or the spectate flag), else 0.  Pure —
 * the caller flips the screen + requests the redraw + plays the cue. */
typedef struct engine_county_grid_in {
    int lmb_pressed;               /* DAT_004eafb4 */
    int cursor_x, cursor_y;        /* DAT_004e65fc / DAT_004e65f4 */
    int county_owner;              /* COUNTY(cur)+0x05 */
    int current_realm;             /* DAT_0057c8cc */
    int spectate;                  /* DAT_0053f684 */
} engine_county_grid_in_t;

int engine_county_action_grid_classify(const engine_county_grid_in_t *in);

/* ── FUN_0043a846's voice-cue pick (the rations flip) ──────────────────
 * Returns 2 for s021_02.wav (achieved level is 0 — "your people starve"),
 * 1 for s021_01.wav (county active + nothing fed), else 0 (no cue).
 * Playback itself is unported (see header notes). */
int engine_county_rations_voice_cue(const uint8_t *county_rec);

/* ── FUN_0043a379 — rations-panel arrows / slider band ─────────────────
 * Consumes a press (or held-repeat) on the − / + arrows (±1) or the
 * absolute band (value = cursor_x − 0xe0).  Returns 1 when consumed;
 * when the value actually changes, *out_value / *out_is_drag carry the
 * FUN_0043a5a9 arguments (out_changed = 1).  Owner-gated like the grid. */
typedef struct engine_county_rations_in {
    int lmb_released;              /* DAT_004e65d8 — blocks the handler */
    int lmb_pressed;               /* DAT_004eafb4 */
    int click_repeat;              /* DAT_004eabc5 — delayed-click repeat edge */
    int lmb_held;                  /* DAT_004e65cc */
    int hold_repeat_gate;          /* DAT_004ea4b0 — hold-repeat enable */
    int cursor_x, cursor_y;        /* game-space cursor */
    int county_owner, current_realm;
    int ration_pct;                /* COUNTY(cur)+0x15f (current wanted %) */
} engine_county_rations_in_t;

int engine_county_rations_input(const engine_county_rations_in_t *in,
                                int *out_value, int *out_is_drag,
                                int *out_changed);

/* The whole retail functions (the hybrid host's rows): FUN_00438ceb — the hit-test + its flip; FUN_0043a379 — the
 * decision + DAT_00591550 / DAT_0059154c + the change (FUN_0043a5a9 whole: engine_county_ration_set_render). */
int engine_county_action_grid_click(const engine_county_grid_in_t *in, void (*flip)(int screen));
int engine_county_rations_click(const struct engine_county_panels_ctx *ctx, const engine_county_rations_in_t *in,
                                int32_t *drag_out, int32_t *value_out, int mp_active, void (*mp_notify)(void));
int engine_county_ration_set_render(const struct engine_county_panels_ctx *ctx, int county, int value, int is_drag);

/* ── The panel arrow-button widgets (session-22 second pass) ───────────
 * The up/down steppers on the tax + rations panels are NOT drawn by the
 * flip renderers: retail's per-frame UI refresh (FUN_004ba26e) paints
 * them every frame via the button-strip renderer —
 *     screen 0x15: FUN_0040cfd2(0, 0, &DAT_004dd790, 2)
 *     screen 0x19: FUN_0040cfd2(0, 0, &DAT_004dd7c0, 2) + FUN_00411fde()
 * — and the UI router (FUN_004ba9c8) dispatches their clicks via
 * FUN_0040da1e over the SAME tables.  Both tables are 2 type-4
 * (press + hold-to-auto-repeat) kind-0x18 (system-sheet 24 px) records,
 * extracted from .rdata:
 *   0x4dd790: up (0xc0,0xa2) spr 0x15 → FUN_0043aa32 (tax rate +1, cap 50)
 *             down (0xe0,0xa2) spr 0x17 → FUN_0043aad5 (ported in engine.c cp_button_action; tax rate −1, floor 0)
 *   0x4dd7c0: up (0x158,0x82) spr 0x15 → FUN_0043a1e9 (wanted level +1, cap 5)
 *             down (0x170,0x82) spr 0x17 → FUN_0043a2b2 (wanted level −1)
 * The tables are exposed MUTABLE (the dispatch animates state/pressed in
 * place, exactly like retail's .data tables).  The `action` field holds
 * the retail callee address as an opaque code for the dispatch callback. */
engine_button_rec_t *engine_county_tax_buttons(void);      /* DAT_004dd790 */
engine_button_rec_t *engine_county_wanted_buttons(void);   /* DAT_004dd7c0 */

/* FUN_0043aa83 / FUN_0043ab25 — step the county's tax rate (+0xb9) by
 * dir (+1/−1), clamped to [0, 0x32], then recompute the realm's projected
 * income (engine_realm_recompute_realm_income).  The caller re-renders. */
void engine_county_tax_step(int county, int dir);

/* FUN_0043a23f / FUN_0043a307 — step the county's WANTED ration level
 * (+0x15e, the loc-grp-0x15 name index) by dir, clamped to [0, 5], then
 * rerun provision + plan_realm.  The caller re-renders. */
void engine_county_wanted_step(int county, int dir);

/* FUN_00411fde — the rations panel's percent slider, drawn per-frame
 * after the wanted-arrow buttons: chrome bevel trough, system-sheet side
 * arrows (0x4a/0x4b at the FUN_0043a379 hit rects), the 100-px track
 * (dark top line / 0x3f fill / light bottom line), and the knob (0x4c)
 * at x = wanted-percent + 0xdc. */
void engine_county_ration_slider_render(const engine_county_panels_ctx_t *ctx);

/* ── FUN_0043a5a9 — apply a ration change ──────────────────────────────
 * Writes the wanted percent, reruns the provisioning pass, and — for an
 * arrow step that did not change the "fed" outcome (+0x37c) — keeps
 * stepping ±1 (clamped 0..100) until the outcome flips (the "snap to the
 * next effective level" behavior), then reruns the realm economy passes
 * twice (provision → plan_realm → allocate → provision → plan_realm is
 * the retail tail order: allocate+plan ×2).  Mutates g_per_army_768 via
 * the already-ported economy functions; no-op when the block is unbound.
 * Returns 1 when the current county was set (caller re-renders). */
int engine_county_ration_set(int county, int value, int is_drag);

#endif /* OPENLORDS2_ENGINE_COUNTY_PANELS_H */
