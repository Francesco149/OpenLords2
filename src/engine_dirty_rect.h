/*
 * OpenLords2 — render-invalidation / dirty-rectangle primitives.
 *
 * Port of the FUN_00452306 cluster — retail's running "what needs to be
 * re-rendered next frame" bookkeeping.  A small handful of globals track
 * the bounding rectangle of dirty pixels and a "needs redraw" flag.
 *
 * Pure C; no callees, no Win32 deps.  Every read/write goes through the
 * state struct so the diff harness + host tests don't need globals.
 *
 *   FUN_00452306 @ 0x452306  → engine_dirty_rect_expand
 *   FUN_004523e6 @ 0x4523e6  → engine_dirty_rect_mark_right_half
 *   FUN_0045240a @ 0x45240a  → engine_dirty_rect_mark_full_screen
 *   FUN_00452436 @ 0x452436  → engine_dirty_rect_mark_tile_32
 *   FUN_00452489 @ 0x452489  → engine_dirty_rect_mark_tile_48
 *   FUN_004524dc @ 0x4524dc  → engine_dirty_rect_mark_tile_64    (and 452_52f, identical)
 *   FUN_00452582 @ 0x452582  → engine_dirty_rect_mark_tile_80    (and 452_660, identical)
 *   FUN_004525d5 @ 0x4525d5  → engine_dirty_rect_mark_tile_96
 *   FUN_00452628 @ 0x452628  → engine_dirty_rect_mark_tile_80x48_noclamp
 *   FUN_004526b3 @ 0x4526b3  → engine_dirty_rect_mark_tile_200x160
 *   FUN_0045270a @ 0x45270a  → engine_dirty_rect_mark_below_status_bar
 *   FUN_00452735 @ 0x452735  → engine_dirty_rect_mark_below_status_bar_full
 *   FUN_004527a6 @ 0x4527a6  → engine_dirty_rect_mark_box_tiled
 *
 * Two identical-pair wrappers (524dc / 5252f, 52582 / 52660) get one
 * port each; the retail addresses are duplicate codegen.
 *
 * One bare thunk (FUN_0043cc56 — 16 B `call FUN_0045270a; ret`) is
 * an address-distinct alias for engine_dirty_rect_mark_below_status_bar;
 * callers store the thunk's address in function-pointer tables where
 * the call target needs a distinct identity.  No semantic difference
 * from the underlying function — we don't define a separate symbol.
 */

#ifndef OPENLORDS2_ENGINE_DIRTY_RECT_H
#define OPENLORDS2_ENGINE_DIRTY_RECT_H

/* Screen bounds the clamp targets.  Retail hardcodes 640x480 (the only
 * resolution the engine ever runs at). */
#define ENGINE_DIRTY_RECT_SCREEN_X_MAX  0x27f   /* 639 — right-edge column */
#define ENGINE_DIRTY_RECT_SCREEN_Y_MAX  0x1df   /* 479 — bottom-edge row */

/* Status bar height the below-status presets skip past. */
#define ENGINE_DIRTY_RECT_STATUS_BAR_H  0x18    /* 24 px */

/* Right-half X start the right_half preset uses. */
#define ENGINE_DIRTY_RECT_RIGHT_HALF_X  0x140   /* 320 */

/* All state read or written by the cluster.
 *
 *   dirty_mark      ← DAT_0050d09c — 0 = clean, 1 = dirty, 2 = "force
 *                     full repaint" (engine reads this in the render
 *                     phase to decide whether to skip / partial-rebuild
 *                     / full-rebuild).
 *   dirty_x{min,max}← DAT_0050a4c4 / DAT_0050a4c8 — accumulated dirty
 *                     bounds in pixels.
 *   dirty_y{min,max}← DAT_0050a4bc / DAT_0050a4c0 — accumulated dirty
 *                     bounds in pixels.
 *   below_status_redraw  ← DAT_0053e9ac — separate "below-status-bar
 *                     UI needs repaint" flag.  Set by the two below-
 *                     status presets only; cleared elsewhere by the
 *                     code that consumes it.
 */
typedef struct {
    int dirty_mark;
    int dirty_xmin;
    int dirty_xmax;
    int dirty_ymin;
    int dirty_ymax;
    int below_status_redraw;
} engine_dirty_rect_state_t;

/* Core: expand the running dirty rectangle to include (x1, y1)..(x2, y2).
 *
 *   - dirty_mark = max(dirty_mark, 1).
 *   - dirty_xmin = min(dirty_xmin, x1);  dirty_xmax = max(dirty_xmax, x2).
 *   - dirty_ymin = min(dirty_ymin, y1);  dirty_ymax = max(dirty_ymax, y2).
 *   - Clamp xmin/ymin up to 0, xmax down to ENGINE_DIRTY_RECT_SCREEN_X_MAX.
 *   - QUIRK: the ymax upper-bound check compares against
 *     ENGINE_DIRTY_RECT_SCREEN_X_MAX (639) not the Y bound — values in
 *     [0x1e0, 0x27f] slip through without being clamped down.  The
 *     assignment IS y-bound (0x1df) when the broken condition fires.
 *     Preserved verbatim — logged as quirk #11 in engine-quirks.md.
 */
void engine_dirty_rect_expand(engine_dirty_rect_state_t *state,
                              int x1, int y1, int x2, int y2);

/* ----------- preset wrappers --------------------------------------- */

/* Right half of the 640x480 screen: (0x140, 0)..(0x27f, 0x1df). */
void engine_dirty_rect_mark_right_half(engine_dirty_rect_state_t *state);

/* Full screen: (0, 0)..(0x27f, 0x1df). */
void engine_dirty_rect_mark_full_screen(engine_dirty_rect_state_t *state);

/* Below status bar: (0, 0x18)..(0x1de, 0x1df).  Also sets
 * below_status_redraw = 1.  Note xmax is 0x1de NOT 0x27f — only the
 * left ~75% of the screen below the status bar is marked. */
void engine_dirty_rect_mark_below_status_bar(engine_dirty_rect_state_t *state);

/* Same area as mark_below_status_bar, plus forces dirty_mark = 2 (full
 * repaint, not just "dirty").  Used after major state transitions where
 * a partial paint would alias against stale pixels. */
void engine_dirty_rect_mark_below_status_bar_full(engine_dirty_rect_state_t *state);

/* ----------- tile-stride box wrappers ------------------------------ */

/* All of these expand the dirty rectangle around a 0x10-pixel-stride
 * tile coord — `(col, row)` measured in 16-pixel cells.  Negative
 * inputs are clamped to 0 (except for mark_tile_80x48_noclamp).
 *
 * Each wrapper picks a box size matched to a specific sprite/widget
 * size used by the engine (32, 48, 64, 80, 96 pixels square + two
 * non-square variants used for cursors and panels). */

void engine_dirty_rect_mark_tile_32      (engine_dirty_rect_state_t *state,
                                          int col, int row);
void engine_dirty_rect_mark_tile_48      (engine_dirty_rect_state_t *state,
                                          int col, int row);
void engine_dirty_rect_mark_tile_64      (engine_dirty_rect_state_t *state,
                                          int col, int row);
void engine_dirty_rect_mark_tile_80      (engine_dirty_rect_state_t *state,
                                          int col, int row);
void engine_dirty_rect_mark_tile_96      (engine_dirty_rect_state_t *state,
                                          int col, int row);
void engine_dirty_rect_mark_tile_200x160 (engine_dirty_rect_state_t *state,
                                          int col, int row);

/* 80x48 box around (col, row), WITHOUT the negative-input clamp the
 * other tile_* wrappers have.  Negative col/row produce negative
 * x1/y1 which the core's clamp-to-zero handles, but the box's
 * x2/y2 = (col*16 + 0x50)/(row*16 + 0x30) can also go negative for
 * extreme inputs and won't be clamped to 0 (only to the upper bounds).
 * Retail's FUN_00452628 codegen really doesn't have the clamp prelude.
 */
void engine_dirty_rect_mark_tile_80x48_noclamp(engine_dirty_rect_state_t *state,
                                               int col, int row);

/* ----------- parametric pixel/tile-stride wrapper ------------------ */

/* Generic parametric box wrapper — port of FUN_004527a6.  Unlike the
 * tile_* wrappers above which take a (col, row) tile coordinate and
 * scale by 16, this one takes a PIXEL-coordinate top-left plus a
 * tile-stride width/height (in 16-pixel units).  The clamp-to-zero
 * for the top-left applies to BOTH axes here.
 *
 * Box covered: (px, py)..(px + w_tiles * 16, py + h_tiles * 16),
 * clipped through the same x/y bounds as expand().
 *
 * The fifth argument is an escalation flag.  Values > 1 set
 * dirty_mark = 2 AFTER the expand call (overriding the mark=1 the
 * expand stamped).  Values 0 or 1 leave dirty_mark whatever expand
 * set it to (max of pre-existing and 1).  Used by callers that need
 * to force a full-repaint when they touch certain UI panels.
 *
 * Retail call sites pass param_5 in {1, 2}; we accept any int but
 * only `> 1` triggers the escalation. */
void engine_dirty_rect_mark_box_tiled(engine_dirty_rect_state_t *state,
                                      int px, int py,
                                      int w_tiles, int h_tiles,
                                      int force_full);

#endif /* OPENLORDS2_ENGINE_DIRTY_RECT_H */
