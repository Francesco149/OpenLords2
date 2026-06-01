/*
 * OpenLords2 — title-menu file-load confirm pump.
 *
 *   FUN_004ad9f0 @ 0x4ad9f0 (774 B) → engine_load_pump_tick
 *
 * The deferred load/save confirm pump retail runs every frame from
 * inside the title-menu UI dispatcher (FUN_004ba9c8).  When the page-3
 * "Loading a game." list's Load button latches a request
 * (DAT_005cd41c = 100, our engine_menu_input filelist-load-request), the
 * pump arms a ~150-frame confirm timer, builds the save's filename
 * (selected name field + extension), and — when the timer expires on the
 * title screen — performs the load and transitions the engine into the
 * in-game strategic map.
 *
 * Only the SINGLE-PLAYER title-load path (retail DAT_00553030 == 0,
 * DAT_004eac50 == 0x1f, not the in-game 0x35/0x36 save/load dialog) is
 * ported.  The actual load + map setup is delegated to a bound hook
 * (the drop-in binds it to save_load + engine_map_post_load) so this
 * module stays out of the Win32 gfx / persistence link for host tests;
 * the engine-state transition (FUN_004329ec SP branch) is done inline.
 *
 * Deferred (verification-neutral): the multiplayer arms (DAT_00553030 !=
 * 0), the in-game save/load dialog states '5'/'6', the missing-file
 * retry (FUN_004af767), the post-load sync wait FUN_004adcf6 (a no-op
 * for SP — returns 1 immediately), the load sound cue (s040_01.wav), and
 * the render-neutral UI flags FUN_00451f8a sets.
 */

#ifndef OPENLORDS2_ENGINE_LOAD_PUMP_H
#define OPENLORDS2_ENGINE_LOAD_PUMP_H

/* Performs the actual load: receives the fully-built save filename
 * (e.g. "lastturn.sav", CWD-relative) and must deserialise it + run the
 * strategic-map setup (retail FUN_004adfe6 → FUN_004976a1).  Returns 0
 * on success, non-zero on failure (mirrors save_load / FUN_004adfe6).
 * The drop-in binds this to save_load + engine_map_post_load; the
 * host-test build leaves it at the default no-op (returns 1 — "no loader",
 * never reached because no test arms the request). */
typedef int (*engine_load_pump_load_fn)(const char *filename);
void engine_load_pump_bind_load_fn(engine_load_pump_load_fn fn);

/* One pump step — call once per frame from the live tick.  Self-gates on
 * the filelist-load request latch + the confirm timer; a no-op until the
 * Load button latches a request. */
void engine_load_pump_tick(void);

/* Test instrumentation. */
int  engine_load_pump_timer(void);          /* retail DAT_0057d3c4 */
const char *engine_load_pump_filename(void);/* retail DAT_004ead60 */
void engine_load_pump_reset(void);

#endif /* OPENLORDS2_ENGINE_LOAD_PUMP_H */
