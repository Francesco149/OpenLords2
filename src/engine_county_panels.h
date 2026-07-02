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
 *     path (FUN_00427990) has no port yet (audio.h) — the flips document
 *     the cue + set the g_combat_audio_latch flag, playback is a no-op
 *     (drop-in audio is off by default anyway).
 *   - The multiplayer arm of the ration set (FUN_0043eda0(0x31,0) when
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
} engine_county_panels_ctx_t;

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
