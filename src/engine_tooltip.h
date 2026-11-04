/*
 * OpenLords2 — the tooltip subsystem ("Tool tips" in Help Options).
 *
 * Ports:
 *   FUN_00476e95 @ 0x476e95 ( 421 B) → engine_tooltip_tick
 *   FUN_00477131 @ 0x477131 ( 280 B) → engine_tooltip_arm      (the arm half of the tick)
 *   FUN_004770b8 @ 0x4770b8 ( 121 B) → engine_tooltip_clear    (the clear half)
 *   FUN_00477249 @ 0x477249 ( 109 B) → engine_tooltip_clamp    (the position clamp)
 *   FUN_004772b6 @ 0x4772b6 ( 106 B) → engine_tooltip_classify (the mode dispatch; engine_tooltip_mode_for_screen
 *                                       is its table)
 *   FUN_00477320 @ 0x477320 (1162 B) → engine_tooltip_classify_strategic
 *   FUN_004777aa @ 0x4777aa ( 246 B) → engine_tooltip_classify_battle (the battle-screen right HUD)
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

#include "engine_dirty_rect.h"
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
    const int32_t *labor_kinds;    /* DAT_0053f690 (kind per row, dwords) */
    /* Production rows (x ≥ 0x230, same band). */
    int prod_row_h;                /* DAT_0053f04c (0 → branch inert) */
    int prod_count;                /* DAT_005440b4 */
    const int32_t *prod_kinds;     /* DAT_00553fd0 (dwords) */
} engine_tooltip_classify_in_t;

int engine_tooltip_classify_strategic(const engine_tooltip_classify_in_t *in);

/* FUN_004777aa — the battle-screen classifier (mode 2): the right HUD's bands by cursor alone (tips 23..30). */
int engine_tooltip_classify_battle(const engine_tooltip_classify_in_t *in);

/* k_tooltip_mode[screen] (0x4d6fb8, 0x48 entries): 0 = none, 1 =
 * strategic classifier, 2 = battle classifier. */
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
    int canvas_repainted;          /* PORT-ONLY compensation (quirk #214):
                                    * 1 = a full-frame repaint ran earlier
                                    * THIS tick.  Retail's tick order is
                                    * renders (line 36/42) → tooltip (112)
                                    * → INPUT DISPATCH (192), so a click
                                    * that flips the screen repaints only
                                    * NEXT tick and the clear's backdrop
                                    * restore always lands on the frame the
                                    * backdrop came from.  The port runs
                                    * input first; when the repaint has
                                    * already replaced the frame, the saved
                                    * backdrop is stale — the clear SKIPS
                                    * the restore (the repaint already
                                    * erased the box) and a shown tip
                                    * RE-CAPTURES before re-rendering. */
    uint32_t now_ms;               /* timeGetTime(), when `clock` is NULL */
    uint32_t (*clock)(void);       /* the arm's timeGetTime() (rhost: retail's virtual clock); NULL → now_ms */
    engine_dirty_rect_state_t *dirty; /* FUN_004527a6's rectangle (rhost: retail's); NULL → no marks */
    const engine_tooltip_classify_in_t *cin;
    localization_state_t *loc;     /* l2.eng (group 0xdc = tip strings) */
    const uint8_t *menu_font;      /* fntl2_14 (DAT_005af8f0) */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
} engine_tooltip_ctx_t;

/* One tooltip frame (FUN_00476e95): clear (a tip shown) or arm, then the box render while a tip is shown.
 * Draws into the gfx canvas; call it AFTER the frame's other renders (retail tick order).  Safe with a NULL
 * canvas (all draws are guarded). */
void engine_tooltip_tick(engine_tooltip_state_t *st,
                         const engine_tooltip_ctx_t *ctx);

/* FUN_00477131: activity restarts the 1 s idle window; an elapsed window classifies the hovered element, places
 * the box, saves the 192×40 backdrop under it (FUN_004b3f0a) and marks it dirty. */
void engine_tooltip_arm(engine_tooltip_state_t *st, const engine_tooltip_ctx_t *ctx);
/* FUN_004770b8: activity hides the tip and restores the backdrop (FUN_004b3ec0). */
void engine_tooltip_clear(engine_tooltip_state_t *st, const engine_tooltip_ctx_t *ctx);
/* FUN_00477249: clamp the box position to [0, 0x1b8]. */
void engine_tooltip_clamp(engine_tooltip_state_t *st);
/* FUN_004772b6: the screen's classifier (k_tooltip_mode): 1 → the strategic one, 2 → the battle one. */
int  engine_tooltip_classify(const engine_tooltip_ctx_t *ctx);

#endif /* OPENLORDS2_ENGINE_TOOLTIP_H */
