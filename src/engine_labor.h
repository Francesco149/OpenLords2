/*
 * OpenLords2 — the per-task labor-allocation detail screen (0x0f).
 *
 * Opened from the workforce icon-grid overlay (screen 0x02,
 * engine_workforce_hud.c): clicking a task cell there flips to this
 * screen, which paints that one task's allocation detail (a tiled
 * parchment panel with worker-count readouts + the per-task economy
 * figures) over the retained workforce frame.  `g_labor_task`
 * (retail DAT_00553f54, 1..9) selects which task:
 *
 *   task 1  Grain farming     FUN_00413590 → engine_labor_panel_grain
 *   task 2  Cattle farming    FUN_00413b30 → engine_labor_panel_cattle
 *   task 3  Field reclamation FUN_004140f3 → engine_labor_panel_reclaim
 *   task 4  Castle building   FUN_00414220 → engine_labor_panel_castle
 *                             (→ FUN_0041dedb → engine_labor_castle_block)
 *   task 5  Iron mining       ┐
 *   task 6  Stone quarrying   ├ FUN_00412e6b → engine_labor_panel_mine
 *   task 7  Wood cutting      ┘ (the three share one sub-renderer)
 *   task 8  Blacksmith        FUN_00413155 → engine_labor_panel_smithy
 *   task 9  (idle)            — main renderer only
 *
 * Retail address map:
 *   FUN_00412b33 @ 0x412b33 (824 B) → engine_labor_render (dispatcher +
 *                                     common chrome; the icon cell, task
 *                                     name, worker count + seal)
 *   FUN_0041ab67 @ 0x41ab67 (215 B) → labor_num_units (static: a decimal
 *                                     value + its loc-group-8 unit noun,
 *                                     singular for ±1 else plural)
 *   FUN_00413590 / FUN_00413b30 / FUN_004140f3 / FUN_00414220 /
 *   FUN_0041dedb / FUN_00412e6b / FUN_00413155  — the sub-renderers.
 *   FUN_0043a123 @ 0x43a123 (145 B) → engine_labor_open_hit_test (the
 *                                     workforce-grid task hit-test that
 *                                     opens this screen)
 *
 * Retail's per-frame dispatch (FUN_0040f1a0) draws screen 0x0f with a
 * bare FUN_00412b33 call — NO strategic-map recomposite (unlike the
 * county panels, whose flip renderers call FUN_004050c0 themselves).
 * The parchment panel (25×N 16-px tiles via gfx_panel_draw) is opaque
 * and covers the workforce grid, so the panel content needs no fresh
 * backdrop; the map only shows outside the panel, retained from the
 * 0x02 frame this screen is reached through.
 *
 * DEFERRED (as elsewhere): the sound/anim cues (0x426216 /
 * 0x427990 — audio.h no-op) and any multiplayer arm.
 *
 * Per-county record fields (rec = g_per_army_768 + county*0x300; retail
 * base DAT_0053f9b0; offsets = DAT_0053fXXX − 0x53f9b0, kept RAW — the
 * decompile does not name them and neither do we):
 *   +0xc4/+0xc8/+0xcc i32  worker triple (allocated/wanted/available),
 *                          per task at +row*0xc where row = task−1
 *   +0x118 i32 blacksmith gold      +0x178 i32 grain tithe
 *   +0x17c i32 cattle tithe         +0x1a6 u8  castle "roof present" flag
 *   +0x1aa i16 seed/breed kind      +0x1c0 u8  castle build kind (tables)
 *   +0x1d0/+0x1d4 i32 castle stone/labor   +0xe8 i32 castle gold cost
 *   +0x204 u8  reclaim state        +0x208 i32 grain fields (yield base)
 *   +0x214 i32 reclaim active men   +0x224 i32 grain fields (worked)
 *   +0x22c i32 grain seed-for-next  +0x230/+0x240/+0x248/+0x24c i32 grain
 *   +0x250 i32 cattle herd          +0x25c i32 cattle age
 *   +0x258 i32 cattle net           +0x268/+0x26c i32 cattle in/out
 *   +0x270 i32 cattle births        +0x274 i32 cattle dairy
 *   +0x278 i32 grain seed sown      +0x2c4 u8  smithy weapon count
 *   +0x2fc i32 grain (season-4)     +0x290 u8  smithy/mine weapon sel
 *   +0x2d8 i32 smithy rate
 *   mine (task 5/6/7, slot s∈{1,3,0}): +0x294 u8 label +s*0x18,
 *     +0x2a8 i32 stock +s*0x18; rates +0x280/+0x284/+0x288/+0x28c
 */

#ifndef OPENLORDS2_ENGINE_LABOR_H
#define OPENLORDS2_ENGINE_LABOR_H

#include <stdint.h>

#include "localization.h"

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + the gfx
 * loaders.  NULL pointers → the draw that needs them is skipped (same
 * NULL-guard convention as engine_county_panels). */
typedef struct engine_labor_ctx {
    const uint8_t *county_block;   /* g_per_army_768 (DAT_0053f9b0 base) */
    int county;                    /* s_slot_0057c93c — current county key */
    int task;                      /* g_labor_task (DAT_00553f54), 1..9 */
    int advanced_mode;             /* s_slot_0053f25c — extra-readouts flag */
    int season;                    /* s_slot_0057c92c — grain season 1..4 */
    int prod_scale;                /* engine_army_economy_prod_scale() */

    const uint8_t *chrome;         /* panels.pl8 (gfx_panel_draw sheet) */
    const uint8_t *misc_sheet;     /* misc_cty.pl8 (DAT_005530c8 icons) */
    const uint8_t *system_sheet;   /* system2.pl8 (continue-seal) */
    const uint8_t *title_font;     /* fntl2_22 (DAT_005b2fa0) */
    const uint8_t *menu_font;      /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
} engine_labor_ctx_t;

/* ── FUN_0041dedb — one castle-status block (the castle panel's, and the terrain-info county summary's 0x41da2f):
 * the wall strength / cost tables DAT_004d8a24 / DAT_004d8a0c by the county's castle kind (+0x1c0), the stone /
 * labour stocks, the build progress; at (bx + 0x60, by + 0x10·row + 0x68 …). */
void engine_labor_castle_block(const engine_labor_ctx_t *ctx, int county, int bx, int by, int row);

/* ── The selected-task global (retail DAT_00553f54) ────────────────────
 * UI state, not saved: set by the open hit-test / the labor steppers,
 * read by the renderer.  Owned here so the input seam and the render
 * seam agree on one source of truth. */
int  engine_labor_task(void);
void engine_labor_set_task(int task);

/* ── FUN_00412b33 — the screen-0x0f renderer ───────────────────────────
 * Paints the whole labor panel for the selected task into the bound gfx
 * canvas: the parchment tiles + icon cell + task name + worker count,
 * then the task-specific sub-renderer, then the "click to continue"
 * seal.  A no-op when task == 0.  The caller locks the canvas (and does
 * NOT recomposite the map — see the header note). */
void engine_labor_render(const engine_labor_ctx_t *ctx);

/* ── FUN_0043a123 — the workforce-grid open hit-test ───────────────────
 * On screen 0x02, an LMB inside the icon grid over a task cell that is
 * present for the current county returns 1 and latches g_labor_task to
 * that task (task index + 1); else 0.  `lmb_click_fired` is DAT_004eabf0
 * (the delayed-click gate, g_input_lmb_click_fired); cursor is the
 * game-space cursor; camera_y is the workforce panel top (DAT_0057c908).
 *
 * The task-cell map the hit-test indexes (retail DAT_00542cf8) is a
 * CONSTANT baked in engine_labor_region_grid.h — see that header. */
typedef struct engine_labor_open_in {
    int lmb_click_fired;           /* DAT_004eabf0 (g_input_lmb_click_fired) */
    int cursor_x, cursor_y;        /* DAT_004eac04 / DAT_004eac08 */
    int camera_y;                  /* DAT_0057c908 — workforce panel top */
    const uint8_t *county_block;   /* g_per_army_768 */
    int county;                    /* s_slot_0057c93c */
} engine_labor_open_in_t;

int engine_labor_open_hit_test(const engine_labor_open_in_t *in);

#endif /* OPENLORDS2_ENGINE_LABOR_H */
