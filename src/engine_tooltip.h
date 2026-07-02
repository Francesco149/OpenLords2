/*
 * OpenLords2 — the tooltip subsystem ("Tool tips" in Help Options).
 *
 * Ports:
 *   FUN_00476e95 @ 0x476e95 ( 421 B) → engine_tooltip_tick
 *   FUN_00477131 @ 0x477131 ( 280 B) → (arm half of the tick)
 *   FUN_004770b8 @ 0x4770b8 ( 121 B) → (clear half of the tick)
 *   FUN_00477249 @ 0x477249 ( 109 B) → (position clamp, folded in)
 *   FUN_004772b6 @ 0x4772b6 ( 106 B) → engine_tooltip_mode_for_screen
 *                                       + the classify dispatch
 *   FUN_00477320 @ 0x477320 (1082 B) → engine_tooltip_classify_strategic
 *   FUN_004777aa @ 0x4777aa           (battle-screen classifier — DEFERRED;
 *                                       mode-2 screens return tip 0)
 *
 * Behavior (retail tick FUN_004b99c0 line 112 — AFTER every per-screen
 * render, so the box paints over the finished frame):
 *   - Gated on the "Tool tips" option (DAT_0053f250).
 *   - No tip shown: once the mouse has been idle (no DAT_004ea4b0
 *     activity) ≥ 1000 ms, classify the hovered UI element by screen
 *     mode (k_tooltip_mode[screen], 0x4d6fb8: 1 = the strategic-family
 *     classifier below, 2 = the battle classifier, 0 = no tooltips) and,
 *     when a tip applies, place the box right+below the cursor (+0x1e;
 *     flipped left at x ≥ 0x141 (−0xdc) / up at y ≥ 0xf1 (−0x1e), both
 *     clamped to [0, 0x1b8]), then SAVE the 192×40 px backdrop under it
 *     (retail buffer DAT_004e6980).
 *   - Tip shown: any mouse activity clears it and restores the backdrop.
 *   - While shown, the box renders EVERY frame: measure the string
 *     (loc group 0xdc, sub = tip index) with the no-shadow single-pass
 *     mode, size the box (12 − (0xb0 − max_line_x)/16 16-px column
 *     groups wide; 0x16 px tall for one line else 0x28), fill white
 *     (0x20), draw the text (width 0xb0), and outline in 0x3f.
 *
 * The strategic classifier (mode 1, all the county/map screens) is pure
 * right-HUD geometry: the minimap (tip 1) + the side icon column (2-5 /
 * 0x1f by panel sub-mode), the 2×2 action grid (6/7/8/9 + the gutter's
 * 0x22, owner-gated), the workforce strip (10, owner-gated), the two
 * dynamic row lists (labor rows → 0xf/0x10/0x11 by kind; production
 * rows → 0x12..0x16 by kind), the bottom icon row (0xb/0xc/0xd/0x20/
 * 0x21) and End-Turn (0xe).  The dynamic-list inputs have no port
 * producer yet — pass zero row heights and the branches fall through
 * to "no tip", exactly like retail with an empty list.
 */

#ifndef OPENLORDS2_ENGINE_TOOLTIP_H
#define OPENLORDS2_ENGINE_TOOLTIP_H

#include <stdint.h>

#include "localization.h"

/* ── Classifier (FUN_00477320) ────────────────────────────────────────── */
typedef struct engine_tooltip_classify_in {
    int cursor_x, cursor_y;        /* DAT_004e65fc / DAT_004e65f4 */
    int panel_mode;                /* DAT_0057a0c4 — right-panel sub-mode */
    int county_owner;              /* COUNTY(cur)+0x05 */
    int current_realm;             /* DAT_0057c8cc */
    /* Labor rows (x < 0x230, y ∈ [0x130, 0x1ae)). */
    int labor_row_h;               /* DAT_0053e970 (0 → branch inert) */
    int labor_count;               /* DAT_00553c60 */
    const uint8_t *labor_kinds;    /* DAT_0053f690 (kind per row) */
    /* Production rows (x ≥ 0x230, same band). */
    int prod_row_h;                /* DAT_0053f04c (0 → branch inert) */
    int prod_count;                /* DAT_005440b4 */
    const uint8_t *prod_kinds;     /* DAT_00553fd0 */
} engine_tooltip_classify_in_t;

int engine_tooltip_classify_strategic(const engine_tooltip_classify_in_t *in);

/* k_tooltip_mode[screen] (0x4d6fb8, 0x48 entries): 0 = none, 1 =
 * strategic classifier, 2 = battle classifier (deferred). */
int engine_tooltip_mode_for_screen(int screen);

/* ── Tick state + context ─────────────────────────────────────────────── */
typedef struct engine_tooltip_state {
    int tip;                       /* DAT_004eac00 — active tip index (0 none) */
    int x, y;                      /* DAT_004eb268 / DAT_004eb270 — box pos */
    uint32_t idle_stamp_ms;        /* _DAT_004ea830 — idle-window base */
    uint32_t backdrop[0x30 * 0x28];/* DAT_004e6980 — 192×40 px save-under */
} engine_tooltip_state_t;

typedef struct engine_tooltip_ctx {
    int enabled;                   /* DAT_0053f250 — the "Tool tips" option */
    int screen;                    /* DAT_004eac50 */
    int mouse_event;               /* DAT_004ea4b0 — any activity this frame */
    uint32_t now_ms;               /* timeGetTime() */
    const engine_tooltip_classify_in_t *cin;
    localization_state_t *loc;     /* l2.eng (group 0xdc = tip strings) */
    const uint8_t *menu_font;      /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
} engine_tooltip_ctx_t;

/* One tooltip frame (FUN_00476e95): arm / clear / render.  Draws into
 * the bound gfx canvas; call it AFTER the frame's other renders (retail
 * tick order).  Safe with a NULL canvas (all draws are guarded). */
void engine_tooltip_tick(engine_tooltip_state_t *st,
                         const engine_tooltip_ctx_t *ctx);

#endif /* OPENLORDS2_ENGINE_TOOLTIP_H */
