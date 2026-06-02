/*
 * OpenLords2 — title-menu input + page navigation.
 *
 * Ports the title-screen hotspot hit-test + click-dispatch chain for
 * the two rendered title pages (page 1 = main menu, page 2 = "Your
 * options"):
 *
 *   FUN_004ba9c8 (0x1f arm)   per-page input dispatcher
 *     ├─ FUN_0040e3ee         click hit-test + action dispatch
 *     ├─ FUN_0040e5e7         hover hit-test → selected-button highlight
 *     └─ FUN_00432b05 / cc8   per-page click actions (the page transitions)
 *
 * The input edge flags + game-coord cursor this consumes are produced
 * upstream by engine_compute_input_state (FUN_004b191e); engine.c's
 * per-tick wrapper bundles them into engine_menu_click_in_t and calls
 * engine_menu_title_input once per frame while the title screen is up.
 *
 * The page selector (DAT_005530f0) and selected-button (DAT_00553114)
 * live in engine_title_render; this module reads/writes them through
 * its public API.  Engine-side effects (demand-redraw cookie, quit
 * latch) go through bound hooks so the module is unit-testable in
 * isolation.  See docs/findings/title-menu-screens.md.
 */

#ifndef OPENLORDS2_ENGINE_MENU_INPUT_H
#define OPENLORDS2_ENGINE_MENU_INPUT_H

#include <stdint.h>

/* A title-menu hotspot — the 24-byte retail table entry, modeled with
 * named fields (the drop-in builds the tables itself, so it need not
 * mirror the raw short[0xc] layout).  Bounding box is half-open:
 * x0 <= cursor_x < x1, y0 <= cursor_y < y1.
 *
 *   x0,y0,x1,y1   retail short[0..3]  — box left/top/right/bottom
 *   action        retail +0x8         — click callback (gets `param`)
 *   button_type   retail +0xf         — edge gate: 1=press, 3=release, 2=held
 *   param         retail +0x10        — action arg (stored to DAT_0059154c)
 *
 * (Retail also has a per-entry "clicked" latch at +0xd and a second
 *  param at +0x14 → DAT_00591550; neither is read by the title-menu
 *  click actions, so the drop-in keeps the tables const and omits them.) */
typedef struct engine_menu_hotspot {
    int16_t x0, y0, x1, y1;
    void  (*action)(int param);
    uint8_t button_type;
    int     param;
} engine_menu_hotspot_t;

/* Resolved per-frame input the hit-tests consume.  All fields come
 * from engine_compute_input_state's output (the retail globals are
 * noted) — bundled by engine.c's wrapper. */
typedef struct engine_menu_click_in {
    int cursor_x;            /* DAT_004e65fc — game-coord cursor X */
    int cursor_y;            /* DAT_004e65f4 — game-coord cursor Y */
    int lmb_newly_pressed;   /* DAT_004eafb4 — LMB press edge (type 1) */
    int lmb_newly_released;  /* DAT_004e65d8 — LMB release edge (type 3) */
    int lmb_held;            /* DAT_004e65cc — LMB held (type 2) */
    int lmb_dblclk;          /* DAT_004eabc5 — LMB double-click (type 2) */
    int tick_320ms;          /* DAT_0057d3c8 — 320 ms tick (type 2 repeat) */
    int repeat_tick;         /* DAT_004ea128 != 0 — per-frame auto-repeat
                              * gate (retail's ≥30 ms frame delta); 1 on a
                              * real input frame, 0 for a one-shot synthetic
                              * click.  Gates the type-4 hold auto-repeat. */
} engine_menu_click_in_t;

/* ── hit-test primitives ──────────────────────────────────────────── */

/* Hover hit-test — port of FUN_0040e5e7.  Returns the 1-based index of
 * the first hotspot whose box contains (cursor_x, cursor_y), or 0 if
 * none.  Pure. */
int engine_menu_hotspot_hover(int ox, int oy,
                              const engine_menu_hotspot_t *table, int count,
                              int cursor_x, int cursor_y);

/* Click hit-test + dispatch — port of FUN_0040e3ee.  Walks the table;
 * for the first hotspot under the cursor whose button_type edge is
 * satisfied by `in`, invokes its action(param) and returns the 1-based
 * index.  Returns 0 if no hotspot fired. */
int engine_menu_hotspot_click(int ox, int oy,
                              const engine_menu_hotspot_t *table, int count,
                              const engine_menu_click_in_t *in);

/* Button-record hit-test + dispatch — port of FUN_0040da1e, the other
 * hit-test family (the one paired with the FUN_0040cfd2 button renderer,
 * over the 0x18-byte engine_button_rec_t tables).  Unlike the short[]
 * hotspot path it (a) runs a per-button press-frame animation countdown
 * every call (so a clicked button un-presses after its flash), (b) hit-
 * tests a square box whose side is the record's `kind` field (0x18 = 24,
 * 0x20 = 32 px), and (c) supports type-4 press + hold-to-auto-repeat
 * (the scroll arrows) and type-5 fire-on-release-animation-end.
 *
 * `dispatch` is invoked with the fired record's action code + its two
 * params (retail stores them to DAT_0059154c / DAT_00591550 first, then
 * calls the action through the record's function pointer).  Mutates the
 * records' state/pressed/repeat fields in place.  Returns the 1-based
 * index of the record that fired, else 0. */
struct engine_button_rec;
typedef void (*engine_menu_button_action_fn)(uint32_t action,
                                             int param0, int param1);
int engine_menu_button_dispatch(int ox, int oy,
                                struct engine_button_rec *recs, int count,
                                const engine_menu_click_in_t *in,
                                engine_menu_button_action_fn dispatch);

/* ── per-page dispatcher ──────────────────────────────────────────── */

/* Port of FUN_004ba9c8's 0x1f arm for the two rendered pages.  Runs the
 * click dispatch (which may transition the page / quit), then refreshes
 * the hover highlight.  Returns 1 if a click was consumed this frame
 * (retail's "stop further input processing"), else 0.  Pages other than
 * 1 and 2 are not handled here (they route through the deferred page
 * hook in engine_title_render). */
int engine_menu_title_input(const engine_menu_click_in_t *in);

/* Outer transition-dispatcher arm (FUN_0042ff10): the page-3 file-list
 * grid selector (FUN_0043400b(1)).  Retail runs it after FUN_004ba9c8
 * returns 0; call it from the live tick when engine_menu_title_input did
 * not consume the click.  Self-gates on screen 0x1f + page 3.  Returns
 * non-zero when the grid pick consumed the click. */
int engine_menu_input_filelist_grid_step(const engine_menu_click_in_t *in);

/* ── engine-side-effect hooks ─────────────────────────────────────── */

/* Demand-redraw request (retail DAT_0057d340 = mode).  Default stub
 * records the last mode; engine.c binds engine_set_demand_redraw. */
typedef void (*engine_menu_redraw_fn)(int mode);
void engine_menu_input_bind_redraw_fn(engine_menu_redraw_fn fn);

/* Quit request (retail DAT_005aeb68 = 1 on "Exit game").  Default stub
 * records the request; engine.c binds it to g_shutdown_request. */
typedef void (*engine_menu_quit_fn)(void);
void engine_menu_input_bind_quit_fn(engine_menu_quit_fn fn);

/* Single-player availability gate — retail DAT_005c9a74.  "Single
 * player" routes to page 2 only when this is in [1,2]; otherwise retail
 * routes to the (deferred) profile-select page 10.  Defaults to 1 so
 * the drop-in reaches "Your options".  Setter exists for tests. */
void engine_menu_input_set_sp_available(int value);

/* Read-only view of the gate — the CD-audio probe
 * (io_path_probe_cd_audio, retail FUN_004aef7e) is the producer.
 * Exposed for tests/diff. */
int  engine_menu_input_sp_available(void);

/* Test introspection: last demand-redraw mode / quit count seen by the
 * default stubs (only meaningful when no real hook is bound). */
int  engine_menu_input_last_redraw_mode(void);
int  engine_menu_input_quit_count(void);
void engine_menu_input_reset_counters(void);

/* DAT_005cd41c — the page-3 "Load" request latch (FUN_004342f3 writes
 * 100).  The load-confirm pump (engine_load_pump_tick) consumes it via
 * the setter; the setter also lets a bring-up seat simulate the click. */
int  engine_menu_input_filelist_load_request(void);
void engine_menu_input_set_filelist_load_request(int v);

#endif /* OPENLORDS2_ENGINE_MENU_INPUT_H */
