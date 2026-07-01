/*
 * OpenLords2 — drop-in replacement for Lords2.exe (bootstrap skeleton).
 *
 * Boots, registers the WinLords2Class window class, opens a 640×480
 * window (hidden by default), runs a PeekMessage / DispatchMessage
 * loop with a Sleep(1) frame limiter, exits cleanly on WM_QUIT or
 * after `--frames N` iterations.  No DDraw yet, no game-state init
 * — those land in subsequent slices.
 *
 * Class registration + window creation are ported from retail's
 * `FUN_004b2258 @ 0x4b2258` (the "InitInstance"-equivalent).  See
 * `docs/findings/winmain-and-bootstrap.md` for the chain.
 *
 * The WndProc is ported from retail's `FUN_004b29be @ 0x4b29be`.  See
 * `docs/findings/wndproc.md` for the per-message breakdown.  Only the
 * structural messages (paint, destroy, move/size, close, activateapp,
 * displaychange, mouse-state cache) are fully implemented in this
 * pass — every game-state hotkey case is a labelled TODO referring
 * back to the retail FUN_004xxxxx so the port can resume once the
 * called subsystem lands.
 *
 * Deliberate deviations from retail (phase 1 — bootstrap defaults):
 *   - WS_VISIBLE is NEVER set in CreateWindowExA.  Retail bakes it
 *     into dwStyle (`0x90080000`) which makes the window appear
 *     during the create call.  We split it out so `--show-window`
 *     (a separate ShowWindow call) composes cleanly with hidden
 *     headless runs.
 *   - Default style is windowed overlapped (640×480 centred frame),
 *     not retail's fullscreen WS_POPUP.  `--fullscreen` opts into
 *     retail's full-screen-sized borderless popup if the user wants
 *     visual parity with the original.  Mirrors the
 *     project_drop_in_defaults memory.
 *   - Cursor + icon preloads from retail's FUN_004b2258 are skipped
 *     until we embed the .ico/.cur resources — `LoadCursor(hInst,
 *     MAKEINTRESOURCE(0x65))` returns NULL without an .rc.
 *   - Auto-cd into the directory of our own .exe at startup so the
 *     DDraw/DSound/Smacker DLL resolves no matter where the .exe was
 *     invoked from.  Footgun guard learned from OpenMare: without
 *     it, dev runs of the drop-in from outside the game dir silently
 *     picked up system32 DDraw instead of any wrapper sitting next
 *     to retail.
 *
 * Class name is preserved as "WinLords2Class" (matches retail) so
 * external tools — `FindWindowA("WinLords2Class", NULL)`, Frida
 * agents keyed on the class — locate us identically to Lords2.exe.
 *
 * Build: `make -C src` from inside `nix develop`.
 */

#define WIN32_LEAN_AND_MEAN
#include <ctype.h>
#include <windows.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "audio.h"
#include "call_trace.h"
#include "config.h"
#include "dev_hooks.h"
#include "engine.h"
#include "engine_workforce_hud.h"
#include "engine_dirty_rect.h"
#include "engine_hud_menubar.h"
#include "engine_map_init.h"
#include "engine_territory.h"
#include "engine_map_query.h"
#include "engine_map_render.h"
#include "engine_terrain_info.h"   /* forced terrain-info render (verification seat) */
#include "gfx_wrap.h"              /* gfx_wrap_set_tall_font_marker */
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_minimap.h"
#include "engine_hud_panel.h"
#include "engine_overlay_render.h"
#include "engine_pl8.h"
#include "engine_screen_dispatch.h"
#include "engine_screen_strat.h"
#include "engine_load_pump.h"
#include "engine_roster.h"         /* load-tail session re-arm (0043e9e2/0049ca32) */
#include "game_init.h"
#include "engine_npc_schedule.h"   /* engine_npc_schedule_set_localization (l2.eng) */
#include "engine_title_render.h"
#include "input_trace.h"
#include "state_dump.h"
#include "engine_menu_input.h"
#include "text_input.h"
#include "gfx.h"
#include "io.h"
#include "io_path.h"
#include "gfx_dispatch.h"
#include "gfx_drawcap.h"
#include "gfx_glyph.h"
#include "gfx_glyph_table.h"
#include "gfx_paint.h"
#include "gfx_sprite_render.h"
#include "gfx_caret.h"
#include "gfx_panel.h"
#include "gfx_text.h"
#include "save.h"
#include "save_schema_globals.h"   /* s_slot_0053e8a4 (roster size) */

/* Class name matches retail FUN_004b2258's `s_WinLords2Class_004df5b0`
 * so FindWindowA from external tools resolves to us identically.
 * Window title likewise matches retail's `s_Lords2_004df5c0` for the
 * same reason. */
#define OPENLORDS2_CLASS  "WinLords2Class"
#define OPENLORDS2_TITLE  "Lords2"

/* Retail's native render resolution.  Stored at DAT_004eb274 /
 * DAT_004eac4c.  Lords 2 was designed for 640×480×8 throughout. */
#define DEFAULT_WIDTH   640
#define DEFAULT_HEIGHT  480

/* 50 FPS = 20 ms per frame is a reasonable placeholder until we know
 * retail's actual frame-limiter constant. */
#define FRAME_PERIOD_MS  20

static HINSTANCE g_hInstance;
static HWND      g_hwnd;
static int       g_show_window;     /* --show-window: ShowWindow at startup */
static int       g_fullscreen;      /* --fullscreen: retail-style WS_POPUP full-screen popup */
static int       g_skip_cd;         /* --no-cd: skip auto-cd to exe dir */
static int       g_allow_multi;     /* --allow-multi: skip single-instance mutex */
static unsigned  g_max_frames;      /* --frames N: voluntary exit after N frames (0 = forever) */
static int       g_show_msgbox;     /* --show-msgbox: skip MessageBox→stderr hook */
/* --gfx-init: call gfx_init() after window creation.  Off by default
 * — would alter desktop cooperative-level state on every smoke run
 * if always on, and the headless harness has no way to verify the
 * resulting paint visually.  Manual visual validation is the use
 * case: `tools/run-openlords2.sh --gfx-init --show-window`. */
static int       g_gfx_init;
static int       g_gfx_active;
/* --title-bg: after gfx_init, load the page-1 menu backdrop (palette +
 * gateway.pl8) into the back buffer and present.  Bring-up shortcut for
 * the main-menu render chain before the boot→title state producer lands
 * — exercises the FUN_0041e61d asset-load path so the backdrop can be
 * captured/eyeballed.  Implies --gfx-init. */
static int       g_title_bg;
/* --title-menu: like --title-bg, but also render the stone panel on top
 * of the backdrop (the page-1 main-menu composition: bg + panel; the
 * title/button text no-ops until the font glyph data is extracted +
 * bound).  Implies --gfx-init. */
static int       g_title_menu;
/* --title-page N: which title page (DAT_005530f0) the bring-up renders.
 * 1 = main menu (default), 2 = single-player "Your options".  Implies
 * --title-menu (backdrop + panel + the page composition). */
static int       g_title_page_num = 1;
/* --title-nav DIR: scripted title-menu navigation demo.  Renders the
 * main menu, then drives the *real* ported input dispatch
 * (engine_menu_title_input) with synthetic clicks — "Single player"
 * (→ page 2), then "Back." (→ page 1) — re-rendering + capturing each
 * resulting page into DIR as nav_NN_pPAGE.bmp.  Exercises the
 * hover/click → page-transition → render chain end-to-end without the
 * live present loop.  Implies --title-menu + --gfx-init. */
static const char *g_title_nav_dir;
/* --title-name STR: override the page-4 name-box text for the --title-nav
 * step-4 capture (default "aaaaaa    ", retail's Custom-game default).
 * Lets a glyph-coverage sweep render arbitrary strings in the textbox. */
static const char *g_title_name;
/* --title-live: drive the engine into the live title-screen state
 * (g_sim_current_screen = 0x1f, page 1) ahead of the unported boot→title
 * transition (0x42b780), then let the normal main loop run.  A real
 * mouse navigates the menu: the per-frame button refresh (FUN_004ba26e
 * 0x1f arm) tracks the hover highlight; clicks fire page transitions that
 * full-redraw via the demand-redraw seam.  Implies --gfx-init +
 * --show-window. */
static int       g_title_live;
/* --title-type STR: in the live loop, post one WM_CHAR per frame for each
 * character of STR (starting at frame 8), exercising the real WndProc
 * keystroke route → text editor → page-4 name box.  A scripted stand-in
 * for a human at the keyboard, so the typing path can be frame-captured. */
static const char *g_title_type;
/* --strat-map DIR: bring up the in-game strategic-map BODY render in the
 * live drop-in, ahead of the unported new-game/load state producer + the
 * HUD chrome.  Loads the season sprite banks NATIVELY via engine_pl8
 * (the ported FUN_004984dc), seeds the live map arrays
 * (tile_cells/descriptors/territory/army) from the captured .bin dump in
 * DIR — the same snapshot the host scene harness uses — binds the three
 * ported map clusters at the captured camera, runs the ported map-body
 * driver (engine_map_scene_render_body, FUN_004050f6) into the locked
 * back buffer, dumps the raw 8bpp canvas to DIR/port_strat.raw (for a
 * palette-applied diff vs retail), presents, and captures port_strat.bmp.
 * Implies --gfx-init.  DIR is auto-translated Linux→WSL-UNC like
 * --capture-to. */
static const char *g_strat_map_dir;
static int         g_ti_cell = -1;     /* --ti-cell N: forced terrain-info render */
static int         g_force_popup_type  = -1;  /* --force-popup TYPE:CODE[:FLAG6[:PAYLOAD_A]] */
static int         g_force_popup_code  = 0;
static int         g_force_popup_flag6 = 0;
static int         g_force_popup_payload_a = 0;
static int         g_force_workforce_key = -1;  /* --force-workforce KEY */
static const char *g_strat_load_dir;   /* --strat-load DIR (native data path) */
static int         g_strat_load_live;  /* --strat-load-live: drive the live
                                        * load pump from the title menu */
static int         g_newgame_live;     /* --newgame-live: live page-7 Start →
                                        * genuine menu→ingame map transition */
static int         g_newgame;          /* --newgame [MAP_ID]: run the new-game
                                        * initialiser + render the first frame */
static int         g_newgame_map_id;   /* MAP_ID for --newgame (default 0) */
static int         g_playnow;          /* --playnow [IDX]: scenario config-apply
                                        * → first frame (scenario analogue) */
static int         g_playnow_idx;      /* scenario row for --playnow (default 0) */
static const char *g_input_segtrace;   /* --input-segtrace FILE: replay a TAS
                                        * input trace through the live tick */
static int         g_rng_seed_pin;     /* --rng-seed: pin the double-LFSR to
                                        * the canonical InitInstance seeds each
                                        * PRE-INGAME tick, mirroring the retail
                                        * agent's pinRngSeed — so both engines
                                        * enter ingame from the same RNG state
                                        * regardless of menu-path consumption.
                                        * (The single-LFSR is owner-local in
                                        * the port and already canonical.) */
static int         g_turbo_clock;      /* --turbo: enable the virtual sim
                                        * clock (mirrors the retail agent's
                                        * +step ms/tick) so the rate
                                        * controller grants the same
                                        * sub-steps and both sides stay
                                        * frame-exact through a running turn. */
static uint32_t    g_turbo_step = 17;  /* --turbo-step MS (agent default 17) */
/* --audio-init: call audio_init() after window creation.  Off by
 * default — SetCooperativeLevel(PRIORITY) modifies process audio
 * state and the headless smoke harness has no way to validate
 * output audibly.  The Frida harness already hooks
 * IDirectSound::SetCooperativeLevel to demote PRIORITY→NORMAL, so
 * once we cross-validate that path under our own code we can flip
 * the default to on.  Manual: `tools/run-openlords2.sh
 * --audio-init`. */
static int       g_audio_init;
static int       g_audio_active;
/* --capture-to DIR / --capture-every N : enable back-buffer
 * captures.  Implies --gfx-init; off when DIR is NULL.  Filenames
 * are `<DIR>/frame_NNNNN.bmp`, indexed by g_frame_counter. */
static const char *g_capture_dir;
static unsigned   g_capture_every = 25;
/* --save-sandbox DIR : redirect every bare save-file open (lastturn.sav,
 * Castles.dat, …) into DIR so a trace-capture run never mutates the real
 * game-dir saves (reproducibility — see save.h / save_set_sandbox + memory
 * project_save_redirect_reproducibility).  Linux-abs paths are auto-translated
 * to a WSL-UNC like --capture-to.  Off (CWD-relative) when NULL. */
static const char *g_save_sandbox;
/* --capture-raw : in addition to <label>.bmp, write <label>.raw — the Trace
 * Studio v3 frame payload (8bpp indices + palette) the native viewer ingests.
 * Off by default (normal runs keep just the BMP). */
static int        g_capture_raw;
/* --capture-draws : with --capture-raw, also emit <label>.draws — the Trace
 * Studio v3 per-draw write-journal (base plane + sprite/glyph deltas) that makes
 * a captured frame draw-steppable in the native viewer.  Implies --capture-raw.
 * Off by default (the draw journal does a full-canvas diff per draw). */
static int        g_capture_draws;
/* --capture-force-redraw : on each capture tick, force a full strategic
 * re-render so the map composites into the draw journal (cross-engine
 * map-draw diff in the aligned zone). Off by default. */
static int        g_capture_force_redraw;
/* --call-trace PATH / --call-trace-frames i,j,k : per-frame JSONL of
 * which annotated port functions ran, for tools/call_trace_diff.py.
 * Off when PATH is NULL; frames empty = every frame. */
static const char *g_call_trace_path;
static unsigned   g_call_trace_frames[64];
static size_t     g_call_trace_n_frames;
static int       g_shutdown;
static HANDLE    g_singleton_mutex;

/* Cached screen metrics — retail caches these at FUN_004b2258 entry
 * into DAT_004e65a0 / DAT_004ea838 and reuses across the DDraw
 * bring-up to centre the window. */
static int g_screen_cx;
static int g_screen_cy;

#define SINGLETON_MUTEX_NAME  "OpenLords2-SingleInstance"

/* Frame-counter — bumped once per main-loop iteration. */
static uint32_t  g_frame_counter;
static uint32_t  g_base_time_ms;

/* ---------------------------------------------------------------- *
 *  WndProc state mirror — retail keeps these in flat DAT_* globals  *
 *  scattered through .data.  See docs/findings/wndproc.md for the   *
 *  cross-reference; the comments here map each var to its retail    *
 *  address so future ports can global-replace the rename.           *
 * ---------------------------------------------------------------- */

/* Mouse-coord cache — retail DAT_004eabbe (lo) / DAT_004eabc0 (hi).
 * Updated by WM_MOUSEMOVE; consumed each iteration by
 * engine_tick_input_step (the FUN_004b191e port) in engine.c, so
 * non-static — see the extern decls in engine.c. */
int16_t g_mouse_x;
int16_t g_mouse_y;
/* Mouse button-down bitmask — retail DAT_004eabc2.  bit0=LMB, bit1=RMB. */
uint8_t g_mouse_buttons;
/* Double-click latch — retail DAT_004eada1.  bit0=LMB-dbl, bit1=RMB-dbl.
 * Cleared by engine_tick_input_step after consuming the bits each
 * iteration (matches retail's `DAT_004eada1 = 0` at the end of
 * FUN_004b191e). */
uint8_t g_mouse_dblclk;
/* Ctrl held — retail DAT_004df3a8.  Set by VK_CONTROL down, cleared by up. */
static int     g_ctrl_down;

/* Window-active flag — retail DAT_004e65a8.  Toggled by WM_ACTIVATEAPP
 * and the focus branch of WM_MOVE/WM_SIZE. */
static int     g_focused;
/* Surface-LOST flag — retail DAT_004e65d0.  Set by the render path
 * when DDraw reports DDERR_SURFACELOST; cleared on Restore() success
 * inside the focus-regained path.  We don't have DDraw yet so this
 * always reads 0, but the mirror is here so the port chain stays
 * linear. */
static int     g_surface_lost;
/* Deferred-shutdown state lives in engine.c now (g_shutdown_request,
 * mirror of retail DAT_005aeb68).  See engine.h for the meaning of
 * each value and which retail FUN_xxxxx consumes it. */

/* Client-area RECT in *screen* coords — retail DAT_004ea180/+4/+8/+c.
 * Recomputed on WM_MOVE/WM_SIZE.  Used by DDraw blit destinations
 * AND by engine_tick_input_step (the FUN_004b191e port) for the
 * viewport→game-coord mapping — non-static so engine.c can extern. */
RECT    g_client_screen;

/* Cached screen metrics from WM_DISPLAYCHANGE.
 *   g_screen_change_cx / cy mirror DAT_004e65a0 / DAT_004ea838.
 *   g_screen_bpp mirrors _DAT_004eabd8. */
static int     g_screen_change_cx;
static int     g_screen_change_cy;
static uint32_t g_screen_bpp;

static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static int  register_window_class(void);
static int  create_main_window(void);
static void parse_cmdline(LPSTR);
static void init_exe_dir(void);
static int  acquire_singleton(void);
static void release_singleton(void);
static void frame_limiter(void);
static void main_loop_body(void);
static void bringup_title_menu(int with_panel);
static void bringup_title_nav(const char *dir);
static void bringup_title_live(void);
static void bringup_strat_map(const char *dir);
static void bringup_strat_load(const char *dir);
static void bringup_strat_load_live(void);
static void bringup_newgame(int map_id);
static void bringup_playnow(int scenario_idx);
static void newgame_dump_first_frame(const char *base);
static void bringup_newgame_live(void);
static void newgame_enter_live(int mode);
static void bringup_input_segtrace(void);
static void engine_map_post_load(void);
static int  drop_in_load(const char *filename);
static const char *translate_wsl_path(const char *p, char *buf, size_t cap);
void log_line(const char *fmt, ...);   /* non-static — gfx.c links to this */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrev; (void)nCmdShow;
    g_hInstance = hInst;

    parse_cmdline(lpCmdLine);

    /* Install MessageBox→stderr hook as early as possible — any modal
     * popup between here and uninstall (in shutdown) gets redirected
     * instead of blocking the process invisibly.  See dev_hooks.h. */
    if (!g_show_msgbox) {
        dev_hooks_install_messagebox();
    }

    /* Single-instance enforcement.  Catches the dev-loop foot-gun of
     * accidentally leaving a stray .exe alive from a previous SIGKILL'd
     * run.  See OpenMare PROGRESS.md 2026-05-24 SGL for the original
     * incident motivating this. */
    if (!g_allow_multi && !acquire_singleton()) {
        return 2;
    }

    /* Anchor CWD to the .exe's directory so DDraw / DSound / DPlay /
     * Smacker DLLs and the game's data files resolve relative to the
     * drop-in's location (which, in a real drop-in scenario, IS the
     * retail game directory). */
    if (!g_skip_cd) init_exe_dir();

    /* Remember the anchored cwd as the asset base directory — retail's
     * FUN_0040e9ab captures it here with __getcwd(&DAT_0058fe30, 100),
     * just before the first asset load.  The asset-load retry chain
     * reverts to it via io_path_chdir_base (0x4af502). */
    io_path_capture_base();

    log_line("OpenLords2 starting (show_window=%d, max_frames=%u)",
             g_show_window, g_max_frames);

    /* Pre-init slice from retail FUN_0040e9ab — load l2.eng
     * (120 KB localization blob), parse l2.ini → sierra.ini for
     * CD/sound/video/CPU settings.  Tolerates missing files.  All
     * downstream consumers (none yet) read from g_config and the
     * config_localization_data accessor. */
    config_init();

    timeBeginPeriod(1);

    if (!register_window_class()) {
        log_line("RegisterClassA failed: %lu", GetLastError());
        timeEndPeriod(1);
        return 1;
    }

    if (!create_main_window()) {
        log_line("CreateWindowExA failed: %lu", GetLastError());
        timeEndPeriod(1);
        return 1;
    }

    if (g_show_window) {
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    }

    if (g_gfx_init) {
        if (gfx_init(g_hwnd, g_fullscreen)) {
            g_gfx_active = 1;
            /* Route screen-dispatch arm 0 (the in-game strategic map,
             * FUN_0040f5fd) to the ported renderer.  Dormant until the
             * strat data path has run (engine_screen_strat_render gates on
             * engine_pl8_data_ready); the bring-up seats below set that up
             * explicitly; the live load pump (engine_load_pump_tick) sets
             * it up when a title-menu Load request resolves. */
            engine_screen_dispatch_bind_strat_fn(engine_screen_strat_render);
            /* Route the title-menu load-confirm pump's load action
             * (retail FUN_004adfe6 → FUN_004976a1) to the drop-in's
             * save_load + engine_map_post_load. */
            engine_load_pump_bind_load_fn(drop_in_load);
            /* Wire the per-turn driver into the new-game initialiser's
             * deferred seam (game_init step 17): a fresh game ticks the
             * world to turn 1 exactly as retail's FUN_00497ced does by
             * calling FUN_00448440 once.  Inert until a new-game path
             * (engine_game_start) actually runs — no current live seat
             * drives it, so this only takes effect once new-game seating
             * is wired (Phase B).  Harmless to bind early. */
            engine_game_init_bind_turn_driver_fn(engine_turn_advance);
            /* Bring-up: render the page-1 main menu into the back
             * buffer before the first present, so frame 0 shows the
             * menu rather than the background-erase fill.  Exercises the
             * FUN_0041e61d / FUN_0041e7e1 render chain ahead of the
             * boot→title state producer. */
            if (g_strat_map_dir) {
                /* Live strategic-map BODY render from the captured map
                 * state + natively-loaded sprite banks (engine_pl8). */
                static char strat_buf[MAX_PATH];
                g_strat_map_dir = translate_wsl_path(g_strat_map_dir,
                                                     strat_buf,
                                                     sizeof strat_buf);
                bringup_strat_map(g_strat_map_dir);
            } else if (g_strat_load_dir) {
                /* Native data path: load lastturn.sav + build the render
                 * arrays ourselves (no captured dumps), validate vs the
                 * dumps in DIR, then render. */
                static char strat_load_buf[MAX_PATH];
                g_strat_load_dir = translate_wsl_path(g_strat_load_dir,
                                                      strat_load_buf,
                                                      sizeof strat_load_buf);
                bringup_strat_load(g_strat_load_dir);
            } else if (g_strat_load_live) {
                /* Live LOAD path — drive the engine into the title state,
                 * simulate a page-3 Load request on lastturn, then let the
                 * main loop tick the real load-confirm pump
                 * (engine_load_pump_tick) so the genuine menu→ingame
                 * transition loads the save + renders the strategic map. */
                bringup_strat_load_live();
            } else if (g_newgame_live) {
                /* Live NEW-GAME path — drive the engine into the page-7 config
                 * screen, then (in main_loop_body) fire the genuine "Start"
                 * click so the wired Start handler → game-start seam runs the
                 * new-game init + flips to ingame; the main loop then renders
                 * the strategic map natively.  New-game analogue of
                 * --strat-load-live. */
                bringup_newgame_live();
            } else if (g_newgame) {
                /* New-game path — run the ported new-game initialiser
                 * (engine_game_init) + game-start over the config-screen
                 * preconditions, then render the resulting first strategic
                 * frame.  The new-game analogue of --strat-load. */
                bringup_newgame(g_newgame_map_id);
            } else if (g_playnow) {
                /* Scenario / Play Now! path — run the scenario config-apply
                 * (engine_game_init_apply_scenario_config) over the preset
                 * table row, then engine_game_init + game-start, then render
                 * the first frame.  The scenario analogue of --newgame. */
                bringup_playnow(g_playnow_idx);
            } else if (g_input_segtrace) {
                /* TAS input-trace replay — drive the engine into the live
                 * title state, then (in main_loop_body) replay the trace's
                 * WM_* ops through the genuine WndProc + capture frames on
                 * its {capture} ops.  The SAME trace drives retail via
                 * frida_capture.py --input-segtrace, for a 1:1 diff.  The
                 * exe is chdir'd to the game dir, so translate the (absolute)
                 * trace path to a native \\wsl.localhost UNC the GUI exe
                 * can fopen. */
                static char segtrace_buf[MAX_PATH];
                g_input_segtrace = translate_wsl_path(g_input_segtrace,
                                                      segtrace_buf,
                                                      sizeof segtrace_buf);
                bringup_input_segtrace();
            } else if (g_title_live) {
                /* Live interactive menu — drive the engine into the
                 * title state, then let the main loop tick it so a real
                 * mouse navigates the menu. */
                bringup_title_live();
            } else if (g_title_nav_dir) {
                /* Scripted navigation demo — drives the real ported input
                 * dispatch with synthetic clicks, capturing each page. */
                CreateDirectoryA(g_title_nav_dir, NULL);
                bringup_title_nav(g_title_nav_dir);
            } else if (g_title_bg || g_title_menu) {
                bringup_title_menu(g_title_menu);
            }
            /* Bring-up tail — force the first present immediately so
             * frame 0 captures and the visible window show the
             * initial back-buffer state rather than the
             * background-erase black.  Mirrors retail's pattern of
             * pairing FUN_0045240a with an immediate FUN_00452160(1)
             * after major state changes (gfx_init already marks all
             * dirty at its tail). */
            gfx_tick_present(1);
        } else {
            log_line("gfx_init failed — continuing without DDraw");
        }
    }

    /* DSound bring-up — retail calls this from WinMain *after*
     * InitInstance (FUN_004b2258) returns, i.e. after the window
     * exists but independent of DDraw.  See
     * docs/findings/ddraw-dsound-bringup.md. */
    if (g_audio_init) {
        if (audio_init(g_hwnd)) {
            g_audio_active = 1;
        } else {
            log_line("audio_init failed — continuing without DSound");
        }
    }

    /* Make the capture dir if needed.  ERROR_ALREADY_EXISTS is fine.
     *
     * Auto-translate a Linux-style absolute path ("/tmp/foo") into
     * a WSL UNC path ("\\wsl.localhost\<distro>\tmp\foo") so the
     * Windows-side exe can write through.  Only kicks in if
     * WSL_DISTRO_NAME is in the env (the flake propagates it via
     * WSLENV).  Without that, the user has to provide a native
     * Windows path. */
    static char capture_dir_buf[MAX_PATH];
    if (g_capture_dir && g_capture_dir[0] == '/') {
        const char *distro = getenv("WSL_DISTRO_NAME");
        if (distro && distro[0]) {
            int n = wsprintfA(capture_dir_buf,
                              "\\\\wsl.localhost\\%s%s", distro,
                              g_capture_dir);
            (void)n;
            /* Backslashify path separators after the UNC prefix. */
            for (char *p = capture_dir_buf; *p; p++)
                if (*p == '/') *p = '\\';
            g_capture_dir = capture_dir_buf;
        }
    }
    if (g_capture_dir) {
        if (!CreateDirectoryA(g_capture_dir, NULL)
                && GetLastError() != ERROR_ALREADY_EXISTS) {
            log_line("CreateDirectory(%s) failed: %lu — captures disabled",
                     g_capture_dir, GetLastError());
            g_capture_dir = NULL;
        } else {
            log_line("captures → %s every %u frame(s)",
                     g_capture_dir, g_capture_every);
        }
    }

    /* Save sandbox: same Linux-abs → WSL-UNC translation the capture dir
     * uses (a native Windows path passes through untouched), then arm the
     * save.c redirect so bare save opens land in the sandbox, not the real
     * game dir.  Set before the main loop so the trace's load click — which
     * runs drop_in_load → save_load("lastturn.sav") during the loop —
     * resolves into the sandbox. */
    if (g_save_sandbox) {
        static char save_sandbox_buf[MAX_PATH];
        if (g_save_sandbox[0] == '/') {
            const char *distro = getenv("WSL_DISTRO_NAME");
            if (distro && distro[0]) {
                wsprintfA(save_sandbox_buf, "\\\\wsl.localhost\\%s%s",
                          distro, g_save_sandbox);
                for (char *p = save_sandbox_buf; *p; p++)
                    if (*p == '/') *p = '\\';
                g_save_sandbox = save_sandbox_buf;
            }
        }
        save_set_sandbox(g_save_sandbox);
        log_line("save sandbox → %s (real game-dir saves protected)",
                 g_save_sandbox);
    }

    if (g_call_trace_path) {
        /* Same Linux-abs → WSL-UNC translation the capture dir uses, so
         * harness scripts can pass repo-relative "/opt/src/..." paths. */
        static char call_trace_buf[MAX_PATH];
        if (g_call_trace_path[0] == '/') {
            const char *distro = getenv("WSL_DISTRO_NAME");
            if (distro && distro[0]) {
                wsprintfA(call_trace_buf, "\\\\wsl.localhost\\%s%s",
                          distro, g_call_trace_path);
                for (char *p = call_trace_buf; *p; p++)
                    if (*p == '/') *p = '\\';
                g_call_trace_path = call_trace_buf;
            }
        }
        call_trace_init_from_cli(g_call_trace_path,
                                 g_call_trace_frames, g_call_trace_n_frames);
        log_line("call-trace → %s (%u frame filter%s)",
                 g_call_trace_path,
                 (unsigned)g_call_trace_n_frames,
                 g_call_trace_n_frames ? "" : ", every frame");
    }

    g_base_time_ms = timeGetTime();

    /* --turbo: enable the virtual sim clock so the rate controller grants
     * the same sub-steps per tick as the retail --turbo seat (otherwise
     * the port's real sub-ms frames starve the 12 ms gate and the turn
     * barely advances — see engine.c's virtual-clock block). */
    if (g_turbo_clock)
        engine_virtual_clock_enable(g_turbo_step);

    while (!g_shutdown && (g_max_frames == 0 || g_frame_counter < g_max_frames)) {
        main_loop_body();
        g_frame_counter++;
        if (!g_turbo_clock)
            frame_limiter();   /* turbo runs flat-out (virtual clock paces the sim) */
    }

    log_line("main loop exited after %u frame(s)", g_frame_counter);

    /* Smoke summary: the engine-tick chain bumps its own frame
     * counter (independently of g_frame_counter, which counts Win32
     * pump iterations) and the FPS counter publishes a current FPS
     * value at every 1-second boundary.  Print both at shutdown so
     * the harness can verify the per-tick driver is alive without
     * polling internal state mid-run.  Average FPS is the
     * cumulative-FPS / cumulative-seconds ratio retail's session-diag
     * uses (FUN_004b2636); skipped if no second boundary has elapsed
     * yet (e.g. very short --frames runs). */
    {
        uint32_t cum_secs = engine_get_cumulative_seconds();
        uint32_t avg_fps  = cum_secs ? (engine_get_cumulative_fps() / cum_secs) : 0;
        log_line("engine_ticks=%u current_fps=%u avg_fps=%u (over %u s)",
                 engine_get_frame_counter(),
                 engine_get_current_fps(),
                 avg_fps,
                 cum_secs);
    }

    /* Final sim-state summary (in-game seats only) — turn/season/phase +
     * each roster slot's ready value, so a --newgame-live/--turbo run's
     * natural turn-advance progress (or stall) is greppable from the log
     * without attaching a debugger or capturing pixels.  Ready values:
     * 0 = brain-uninitialised, 1..14 = AI brain ladder, 999 = parked/done. */
    if (g_engine_state == 3) {
        int32_t ready[6] = {0};
        for (int s = 1; s < 6; s++)
            memcpy(&ready[s], &g_player_strategic_352[s * 0x160], sizeof ready[s]);
        log_line("sim-state: turn=%u phase=%u season(prev/cur/next)=%u/%u/%u "
                 "year=%u ai_cursor=%u not_ready=%u ready=[%d,%d,%d,%d,%d]",
                 (unsigned)s_slot_00553240, (unsigned)s_slot_00569584,
                 (unsigned)s_slot_00554470, (unsigned)s_slot_0057c934,
                 (unsigned)s_slot_0057c92c, (unsigned)s_slot_00553edc,
                 (unsigned)s_slot_0057c944, (unsigned)s_slot_00554004,
                 ready[1], ready[2], ready[3], ready[4], ready[5]);
    }

    if (g_audio_active) {
        audio_shutdown();
        g_audio_active = 0;
    }
    if (g_gfx_active) {
        gfx_shutdown();
        g_gfx_active = 0;
    }

    call_trace_shutdown();

    DestroyWindow(g_hwnd);
    UnregisterClassA(OPENLORDS2_CLASS, g_hInstance);

    timeEndPeriod(1);

    if (!g_show_msgbox) dev_hooks_uninstall_messagebox();
    release_singleton();

    return 0;
}

/* Port of retail FUN_004b29be @ 0x4b29be — the WinLords2Class
 * WndProc.  Structural messages (paint, destroy, move/size, close,
 * activateapp, displaychange, mouse-state cache) are fully ported;
 * game-state hotkeys and Smacker-aware paint are stubs until the
 * underlying subsystems land.  See docs/findings/wndproc.md.
 *
 * Retail returns from this function fall into two patterns:
 *   - explicit `return` (WM_PAINT, VK_ESCAPE in WM_KEYDOWN) — bypass
 *     DefWindowProcA;
 *   - implicit fall-through — every other branch ends at the tail
 *     which writes the mouse cache and calls DefWindowProcA.
 * The mirror of `DAT_004eabbe/004eabc0` is updated *only* by
 * WM_MOUSEMOVE, so we just touch g_mouse_x/y there and call
 * DefWindowProcA from the bottom. */
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    /* --- WM_PAINT (0x0f) -------------------------------------------
     * Retail: if a Smacker movie is playing (DAT_00516a4c != 0), call
     *   FUN_0042e3ba which black-fills the dirty rect and SmackGotos
     *   to the current frame.  Otherwise plain Begin/EndPaint, and if
     *   not focused, repaint via FUN_00451f70.  Returns 1, bypassing
     *   DefWindowProcA.  We have neither Smacker nor the full
     *   surface-blit path yet, so just satisfy the OS's invalidation
     *   bookkeeping with a Begin/EndPaint pair. */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        /* TODO(gfx): when surfaces exist, call the "blit back→primary
         * at g_client_screen.left/top" helper here if !g_focused. */
        return 1;
    }

    /* --- WM_ERASEBKGND (0x14) -------------------------------------
     * Suppress the default background-erase.  Class hbrBackground is
     * set to BLACK_BRUSH so DefWindowProc would fill the client area
     * with black on every WM_PAINT preamble — and since we redraw
     * via DDraw Blt from gfx_present each frame, the black fill
     * would (depending on timing) flicker through or completely
     * occlude our render.  Returning non-zero tells the OS we
     * handled the erase. */
    case WM_ERASEBKGND:
        return 1;

    /* --- WM_CREATE (0x01) — no-op in retail, no-op here. ------ */
    case WM_CREATE:
        return 0;

    /* --- WM_DESTROY (0x02) -----------------------------------------
     * Retail tears down every subsystem here: Smacker, palette,
     * DSound, DPlay, then PostQuitMessage(0).  We have none of those
     * yet — just PostQuitMessage and let the main loop exit. */
    case WM_DESTROY:
        /* TODO(subsys-teardown): once DSound/DPlay/Smacker are ported,
         * call the equivalents of FUN_0042dfe2, FUN_004b28cd(1),
         * FUN_00425d35, 0x4b7180(1) before PostQuitMessage. */
        PostQuitMessage(0);
        g_shutdown = 1;
        return 0;

    /* --- WM_MOVE (0x03) / WM_SIZE (0x05) ---------------------------
     * Retail recomputes g_client_screen, toggles g_focused based on
     * IsIconic, logs the move, and triggers a full redraw if the
     * engine is in any state other than "menu". */
    case WM_MOVE:
    case WM_SIZE:
        if (!IsIconic(hwnd)) {
            g_focused = 1;
            /* Retail logs "OK :Unpaused." here via FUN_004b26f8 when
             * the foreground window is ours.  Clears surface-LOST. */
            g_surface_lost = 0;
        } else {
            g_focused = 0;
            g_surface_lost = 0;
            log_line("OK :Minimizing.");
            log_line("OK :Paused.");
            InvalidateRect(hwnd, NULL, TRUE);
        }
        if (g_fullscreen) {
            SetRect(&g_client_screen, 0, 0,
                    GetSystemMetrics(SM_CXSCREEN),
                    GetSystemMetrics(SM_CYSCREEN));
        } else {
            GetClientRect(hwnd, &g_client_screen);
            ClientToScreen(hwnd, (LPPOINT)&g_client_screen.left);
            ClientToScreen(hwnd, (LPPOINT)&g_client_screen.right);
        }
        log_line("OK :Window moved.");
        engine_set_demand_redraw(2);
        /* TODO(sim): retail re-blits / calls FUN_00451f70 here based
         * on engine state — wire in once the render path exists. */
        return 0;

    /* --- WM_CLOSE (0x10) -------------------------------------------
     * Retail just sets DAT_005aeb68 = 3 and falls through to
     * DefWindowProcA — the default proc handles WM_CLOSE by calling
     * DestroyWindow, which sends WM_DESTROY → PostQuitMessage.  The
     * 3-then-1 state dance is the structural seam for the sim to
     * ack the drain (engine_tick_guard does the 3→1 transition);
     * by the time it matters, DestroyWindow has already started
     * tearing the window down.  Same shape as retail. */
    case WM_CLOSE:
        g_shutdown_request = 3;
        break;

    /* --- WM_ACTIVATEAPP (0x1c) -------------------------------------
     * Retail toggles g_focused based on wp + foreground + iconic
     * status, logs "OK :Active." or "OK :Not active.", and on
     * focus-regained-with-lost-surfaces runs the DDraw restore path
     * (FUN_004b2730 → on failure FUN_004b2922 = gfx_shutdown +
     * gfx_init).  We port the same escalation: try gfx_restore
     * first; if it fails, tear down and re-init. */
    case WM_ACTIVATEAPP: {
        HWND fg = GetForegroundWindow();
        if (wp == 0 || fg != hwnd || IsIconic(hwnd))
            g_focused = 0;
        else
            g_focused = 1;
        log_line(g_focused ? "OK :Active." : "OK :Not active.");
        if (g_focused && g_gfx_active && gfx_surface_lost()) {
            if (!gfx_restore()) {
                /* Escalate — full re-init.  Matches retail's outer
                 * FUN_004b2922 wrapper around FUN_004b2730. */
                gfx_shutdown();
                g_gfx_active = 0;
                if (gfx_init(g_hwnd, g_fullscreen)) {
                    g_gfx_active = 1;
                    g_surface_lost = 0;
                    engine_set_demand_redraw(2);
                } else {
                    log_line("WM_ACTIVATEAPP: gfx re-init failed "
                             "after Restore failed — DDraw is down");
                }
            } else {
                g_surface_lost = 0;
                engine_set_demand_redraw(2);
            }
        }
        return 0;
    }

    /* --- WM_DISPLAYCHANGE (0x7e) — cache new screen metrics. ----- */
    case WM_DISPLAYCHANGE:
        g_screen_bpp        = (uint32_t)wp;
        g_screen_change_cx  = LOWORD(lp);
        g_screen_change_cy  = HIWORD(lp);
        return 0;

    /* --- WM_KEYDOWN (0x100) ----------------------------------------
     * Retail dispatches a fat switch on virtual key.  Almost every
     * case calls game-state code we don't have.  Port only the
     * subset the bootstrap can sensibly honour:
     *   VK_ESCAPE → request shutdown (retail posts a menu first
     *               when engine state == ingame; we have no menu).
     *   VK_F1     → WinHelp on l2help.hlp (matches retail exactly).
     *   VK_F5     → recentre to 640×480 (windowed only).
     *   VK_CONTROL→ track g_ctrl_down for the digit-key modifier.
     * Other keys: noted and ignored. */
    case WM_KEYDOWN:
        switch (wp) {
        case VK_CONTROL:
            g_ctrl_down = 1;
            break;
        case VK_ESCAPE:
            /* Retail: outside ingame, VK_ESCAPE sets DAT_005aeb68 = 1
             * directly (FUN_004343f8 is the ingame-menu hook we
             * don't have yet).  The main-loop tail then PostMessages
             * WM_CLOSE to escalate to DefWindowProc → DestroyWindow.
             * No inline DestroyWindow — the escalator does the
             * teardown one frame later. */
            log_line("VK_ESCAPE → shutdown request");
            g_shutdown_request = 1;
            return 0;
        case VK_F1:
            WinHelpA(hwnd, "l2help.hlp", HELP_CONTEXT, 1);
            break;
        case VK_F5:
            if (!g_fullscreen) {
                RECT rc = { 0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT };
                DWORD exStyle = (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE);
                DWORD style   = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
                BOOL  hasMenu = (GetMenu(hwnd) != NULL);
                AdjustWindowRectEx(&rc, style, hasMenu, exStyle);
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                int x = (g_screen_cx - w) / 2;
                int y = (g_screen_cy - h) / 2;
                SetWindowPos(hwnd, NULL, x, y, w, h,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        /* --- Text-editor edit keys (retail WM_KEYDOWN arm of
         * FUN_004b29be).  Route to the bound title editor — the only
         * editable field wired so far is the page-4 name box.  Backspace
         * and delete gate internally on the editor's `enabled` flag
         * (set per-frame only on page 4); the cursor moves and the
         * insert-mode toggle mutate the bound editor unconditionally, as
         * retail does (harmless off-page — the editor isn't rendered). */
        case VK_BACK:    /* 0x08 → FUN_00401c70 */
        case VK_DELETE:  /* 0x2e → FUN_00401c93 → FUN_00401dc8 */
        case VK_LEFT:    /* 0x25 → FUN_00401cbc */
        case VK_RIGHT:   /* 0x27 → FUN_00401cda */
        case VK_HOME:    /* 0x24 → FUN_00401cfc */
        case VK_END:     /* 0x23 → FUN_00401d11 */
        case VK_INSERT: {/* 0x2d → FUN_00401ca3 */
            text_input_state_t *ed = engine_title_render_choose_title_editor();
            if (ed) {
                switch (wp) {
                case VK_BACK:   text_input_backspace(ed);          break;
                case VK_DELETE: text_input_delete_char(ed);        break;
                case VK_LEFT:   text_input_cursor_left(ed);        break;
                case VK_RIGHT:  text_input_cursor_right(ed);       break;
                case VK_HOME:   text_input_cursor_home(ed);        break;
                case VK_END:    text_input_cursor_end(ed);         break;
                case VK_INSERT: text_input_toggle_insert_mode(ed); break;
                }
            }
            break;
        }
        default:
            /* TODO(input): VK_RETURN (commit), digits/F2..F12 and the
             * Alt-1..5 chords map to game-state functions in retail.
             * See docs/findings/wndproc.md for the full VK→FUN_xxxxx map. */
            break;
        }
        return 0;

    /* --- WM_KEYUP (0x101) — only VK_CONTROL is special. --------- */
    case WM_KEYUP:
        if (wp == VK_CONTROL) g_ctrl_down = 0;
        return 0;

    /* --- WM_CHAR (0x102) -------------------------------------------
     * Retail's WM_CHAR arm (FUN_004b29be case 0x102) feeds the typed
     * character to FUN_00401a20 (text_input_filtered_keypress), which
     * filters it against the field's allowed set and inserts it into
     * the global editor.  The insert is gated internally on the
     * editor's `enabled` flag — set per-frame only on pages with a live
     * text field (page 4 today), so keystrokes elsewhere are dropped. */
    case WM_CHAR: {
        text_input_state_t *ed = engine_title_render_choose_title_editor();
        if (ed) {
            text_input_filtered_keypress(ed, (unsigned char)wp);
        }
        return 0;
    }

    /* --- WM_SYSCHAR (0x105) — Alt-key chords, not text. ----------- */
    case WM_SYSCHAR:
        /* TODO(input): port the Alt-1..5 / Alt-X mappings from
         * FUN_004b29be (WM_SYSKEYDOWN/SYSCHAR arm). */
        return 0;

    /* --- Mouse cache ------------------------------------------------ */
    case WM_MOUSEMOVE:
        g_mouse_x = (int16_t)LOWORD(lp);
        g_mouse_y = (int16_t)HIWORD(lp);
        return 0;
    case WM_LBUTTONDOWN:    g_mouse_buttons |= 0x01; return 0;
    case WM_LBUTTONUP:      g_mouse_buttons &= ~0x01; return 0;
    case WM_LBUTTONDBLCLK:  g_mouse_dblclk  |= 0x01; return 0;
    case WM_RBUTTONDOWN:    g_mouse_buttons |= 0x02; return 0;
    case WM_RBUTTONUP:      g_mouse_buttons &= ~0x02; return 0;
    case WM_RBUTTONDBLCLK:  g_mouse_dblclk  |= 0x02; return 0;

    /* --- Palette messages (0x30f, 0x311) ---------------------------
     * Only matters in windowed 8bpp once we have a DDraw palette.
     * No-op until then. */
    case WM_PALETTECHANGED:
    case WM_QUERYNEWPALETTE:
        /* TODO(gfx): port FUN_0042f1df once IDirectDrawPalette
         * exists.  Guarded on (!g_fullscreen && g_primary_surface). */
        break;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Port of retail FUN_004b2258 @ 0x4b2258 class-registration block.
 *
 * Retail uses CS_DBLCLKS, no class cursor (NULL — the game's own
 * cursor manager picks one in WM_SETCURSOR via DAT_004ea834 et al),
 * BLACK_BRUSH background to avoid the WM_ERASEBKGND flash on
 * activation, and icon ID 0x65 / cursor IDs 0x65..0x71 from the .rc.
 *
 * We skip the icon + cursor preloads until we embed an .rc — without
 * resources, LoadIcon/LoadCursor return NULL which is harmless. */
static int register_window_class(void)
{
    WNDCLASSA wc = {0};
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = g_hInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = OPENLORDS2_CLASS;
    return RegisterClassA(&wc) != 0;
}

/* Port of retail FUN_004b2258 @ 0x4b2258 window-creation block.
 *
 * Retail call:
 *     CreateWindowExA(
 *         0x40000,                          // WS_EX_APPWINDOW
 *         "WinLords2Class", "Lords2",
 *         0x90080000,                       // WS_POPUP|WS_VISIBLE|WS_SYSMENU
 *         0, 0,
 *         GetSystemMetrics(SM_CXSCREEN),
 *         GetSystemMetrics(SM_CYSCREEN),
 *         NULL, NULL, hInst, NULL);
 *
 * With --fullscreen we mirror this exactly, sans WS_VISIBLE (we
 * always defer the show to a separate ShowWindow call so --hidden
 * composes cleanly).
 *
 * Without --fullscreen we default to a 640×480 centred overlapped
 * window with WS_OVERLAPPEDWINDOW = WS_OVERLAPPED|WS_CAPTION|
 * WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX = 0xcf0000
 * — which is the *exact* style retail's DDraw bring-up transforms
 * the popup into when running in the (now-dead-code) 8bpp-desktop
 * windowed branch.  Drop-in default per the project_drop_in_defaults
 * memory. */
static int create_main_window(void)
{
    g_screen_cx = GetSystemMetrics(SM_CXSCREEN);
    g_screen_cy = GetSystemMetrics(SM_CYSCREEN);

    DWORD dwExStyle = WS_EX_APPWINDOW;
    DWORD dwStyle;
    int x, y, w, h;

    if (g_fullscreen) {
        /* Retail-style fullscreen borderless popup. */
        dwStyle = WS_POPUP | WS_SYSMENU;        /* WS_VISIBLE stripped */
        x = 0;
        y = 0;
        w = g_screen_cx;
        h = g_screen_cy;
    } else {
        /* Windowed default — 640×480 client area, frame adjusted out,
         * centred on the primary monitor. */
        dwStyle = WS_OVERLAPPEDWINDOW;
        RECT rc = { 0, 0, DEFAULT_WIDTH, DEFAULT_HEIGHT };
        AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        x = (g_screen_cx - w) / 2;
        y = (g_screen_cy - h) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }

    g_hwnd = CreateWindowExA(
        dwExStyle,
        OPENLORDS2_CLASS,
        OPENLORDS2_TITLE,
        dwStyle,
        x, y, w, h,
        NULL, NULL, g_hInstance, NULL);
    if (!g_hwnd) return 0;

    /* Retail calls UpdateWindow + SetFocus immediately after create. */
    UpdateWindow(g_hwnd);
    SetFocus(g_hwnd);
    return 1;
}

/* Bring-up: render the page-1 main menu directly, ahead of the
 * boot→title state producer (0x42b780) and the menu state tick.
 * Loads the gateway backdrop (palette + full-screen image) into the
 * back buffer, then — if with_panel — composes the stone panel + the
 * button bevels on top via the ported title-render chain inside one
 * back-buffer lock.  The title/button label TEXT no-ops here: it rests
 * on the font glyph data (DAT_005b2fa0 / DAT_005af8f0) which is static
 * binary in Lords2.exe and not yet extracted/bound, so the localized
 * line seek returns nothing and draws nothing.  Once the fonts are
 * bound the same chain renders the text with no further wiring. */
/* Bookkeeping dirty-rect state the title chain marks into.  No visual
 * effect (gfx owns the present-side dirty rect) — just keeps the marks
 * from dereferencing NULL.  File-static so the static bring-up and the
 * scripted-nav demo share one. */
static engine_dirty_rect_state_t g_title_dirty;

/* Bind the page-independent title render state: panel sheet, fonts,
 * char→glyph tables, localization blob, screen-state tag, and the
 * shared gfx render-state defaults.  Idempotent; safe to call once
 * before rendering any title page. */
static void title_bind_render_state(void)
{
    const uint8_t *sheet = gfx_load_panel_sheet();
    engine_title_render_set_dirty_state(&g_title_dirty);
    engine_title_render_bind_panel_asset(sheet);
    engine_title_render_bind_system_asset(gfx_load_system_sheet());
    engine_title_render_bind_misc_asset(gfx_load_misc_sheet());
    /* Chrome sheet (panels.pl8) + active palette — the page-7 option-tab
     * bevels + the roster dim.  Page-independent; load once here. */
    engine_title_render_bind_chrome_asset(gfx_load_chrome_sheet());
    engine_title_render_bind_palette(gfx_palette_rgb());
    engine_title_render_bind_panel_fn(gfx_panel_draw);
    /* Background hook stays the default no-op stub — the backdrop is
     * already in the back buffer from gfx_title_background_load. */

    /* Text inputs: the two fonts (menu/default + large title), the
     * embedded char→glyph tables, and the localization blob (l2.eng,
     * loaded by config_init).  With all three bound the ported title
     * chain renders the "Lords of the Realm 2" title, subtitle, and
     * button labels — no further wiring. */
    engine_title_render_bind_fonts(gfx_load_title_font(),
                                   gfx_load_menu_font());
    engine_title_render_bind_glyph_tables(gfx_glyph_render_map(),
                                          gfx_glyph_measure_map());
    size_t loc_size = 0;
    const uint8_t *loc = config_localization_data(&loc_size);
    engine_title_render_bind_localization((unsigned char *)loc);

    /* Screen-state byte (retail DAT_004eac50) — 0x1f on the title screen.
     * gfx_text reads it as the shadow-color discriminator: on the title
     * screen the text shadow pair is the gray (0x36, 0x2c) rather than the
     * default colorful (0x10, 0x1f).  The boot→title state producer
     * (0x42b780, unported) is the real writer; set it here for the
     * bring-up so the menu text shadows match retail.  See quirk #114.
     * Route through the engine setter so both screen-byte mirrors track
     * (it forwards to gfx_text — see engine_set_current_screen). */
    engine_set_current_screen(0x1f);

    /* Init the shared gfx render state to its retail BSS-zero values
     * (the panel renderer assumes these, same as the panel unit test). */
    gfx_dispatch_set_clip_x_mode(0);
    gfx_dispatch_set_glyph_height_short(0);
    gfx_glyph_set_clip(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(0, 0, 0);
    gfx_glyph_set_color_modulation(0);

    /* Game-start seam: wire the page-7 "Start" action to the live new-game
     * enter so any live-menu seat (--title-live, --newgame-live) can launch a
     * game through the genuine menu input.  Host tests leave the default
     * counting stub bound. */
    engine_menu_input_bind_game_start_fn(newgame_enter_live);
}

/* Load the backdrop for the current page (engine_title_render_page) and
 * render the full title scene into the locked back buffer.  Assumes
 * title_bind_render_state has been called.  Returns 0 if the backdrop
 * load failed. */
static int title_render_current_page(void)
{
    int page = engine_title_render_page();
    /* Backdrop: palette + page-appropriate .pl8 raw into the back buffer
     * (page 1/2 both use gateway; gfx_title_background_load routes by page). */
    if (!gfx_title_background_load(page)) {
        log_line("title_render_current_page: background load failed (page %d)",
                 page);
        return 0;
    }
    if (gfx_lock_canvas()) {
        engine_title_render_scene(0x1f, page);
        gfx_unlock_canvas();
    }
    return 1;
}

/* Seed the page-7 (custom-game setup) runtime state for the bring-up,
 * with the exact retail values captured via `frida_capture.py
 * --dump-globals` (run gt-p7-state).  The page-7 state is produced by the
 * scenario-setup flow (unported), so rather than drive it we force the
 * captured snapshot — same approach as the page-3/4 bring-ups.
 *
 *   country list  DAT_0050a460 = [0,1,2,…]  count 0x2c, scroll 0, sel 0
 *   tab values    (resolved 0x53f288.. via 0x433a70's permutation)
 *   roster        seats 1..5: the human "aaaaaa" (type 1) + 4 AI
 *   enabled       DAT_005530e0 all 0 → the human seat is dimmed
 *   preview       DAT_00569590 — the 128×128 map thumbnail, loaded from
 *                 the game dir if present (p7preview.bin), else left blank
 */
static void seed_page7_state(void)
{
    static const uint8_t k_country[64] = {
        0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
        0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,
    };
    engine_title_render_set_country_list(k_country, /*scroll=*/0,
                                         /*sel=*/0, /*count=*/0x2c);

    static const int k_tabs[12] = { 0, 0, 3, 0, 0, 0, 3, 2, 2, 1, 6, 1 };
    engine_title_render_set_tab_values(k_tabs, /*active=*/-1);

    /* Roster size (retail DAT_0053e8a4) — the seeded snapshot has 5 AI seats
     * available, so the tab-2 (player count) dropdown shows all options and
     * the Defaults clamp (FUN_004ae5e2) leaves the default "3" intact. */
    s_slot_0053e8a4 = 5;

    static const engine_title_setup_player_t k_players[6] = {
        { 0, 0, 0,    0, 0 },                 /* seat 0 — unused */
        { 1, 0, 0x0e, 1, 0 },                 /* seat 1 — human "aaaaaa" */
        { 0, 1, 0xfb, 2, 0 },                 /* seat 2 — The Knight */
        { 0, 2, 0x3a, 3, 0 },                 /* seat 3 — The Baron */
        { 0, 4, 0x05, 4, 0 },                 /* seat 4 — The Bishop */
        { 0, 3, 0x04, 5, 0 },                 /* seat 5 — The Countess */
    };
    static uint8_t k_names[6 * 0x2c];
    memset(k_names, 0, sizeof k_names);
    memcpy(k_names + 1 * 0x2c, "aaaaaa    ", 10);
    memcpy(k_names + 2 * 0x2c, "The Knight", 10);
    memcpy(k_names + 3 * 0x2c, "The Baron", 9);
    memcpy(k_names + 4 * 0x2c, "The Bishop", 10);
    memcpy(k_names + 5 * 0x2c, "The Countess", 12);
    engine_title_render_set_players(k_players, k_names);

    /* The 128×128 map preview (DAT_00569590) is rendered by the
     * scenario-load path; load the captured snapshot from the game dir if
     * it was placed there (dev convenience), else leave the panel blank. */
    static uint8_t k_preview[0x4000];
    if (io_read_file_at("p7preview.bin", k_preview, sizeof k_preview, 0)
            == (int)sizeof k_preview) {
        engine_title_render_set_preview(k_preview);
    } else {
        engine_title_render_set_preview(NULL);
    }
}

/* Seed the page-4 ("Choose your title and your shield") render state for a
 * live-input demo: the tall layout (gates 0/0 → Back/Continue shown),
 * Continue enabled, player 1 owning shield slot 1 (flashing), the default
 * name, and the live text editor.  The editor is initialised exactly as
 * retail's page-2→4 transition does it (FUN_00432cc8:
 * FUN_00402009(&DAT_0053f1e0, 0x10, 0xc0, 0)): max 0x10 chars, soft pixel
 * cap 0xc0, mode 0 (punctuation allowed, no case folding).  The host
 * WndProc routes WM_CHAR / edit keys into this editor and the page-4 input
 * arm streams it into the name box each frame. */
static void seed_page4_state(void)
{
    static const uint8_t k_p4_name[] = "aaaaaa    ";   /* DAT_0053f1e0 */
    static const unsigned char k_p4_slots[6] = { 0, 1, 0, 0, 0, 0 };
    static text_input_state_t k_p4_editor;             /* DAT_005cd550 */

    const uint8_t *name = g_title_name
        ? (const uint8_t *)g_title_name : k_p4_name;
    text_input_init(&k_p4_editor, (const char *)name,
                    /*max_length=*/0x10, /*max_pixel_width=*/0xc0,
                    /*initial_mode=*/0);
    k_p4_editor.insert_mode = 0;   /* DAT_005c9280 — overwrite (separate global) */

    engine_title_render_set_choose_title_gates(0, 0);
    engine_title_render_set_choose_title_continue_disabled(0);
    engine_title_render_set_choose_title_shields(k_p4_slots, /*selected=*/1,
                                                 /*flash=*/1, /*flash_timer=*/0);
    engine_title_render_set_choose_title_name(name);
    engine_title_render_set_choose_title_editor(&k_p4_editor);
    engine_title_render_bind_caret_metrics_table(gfx_glyph_measure_map());
}

/* Seed the page-3 ("Loading a game." file list) live state: a few demo
 * save entries, the selected-name field, and the one shared title editor
 * behind it.  Retail seeds this editor at the page-2→3 transition with
 * FUN_00402009(&DAT_004ea130, 8, 0xa0, 1): max 8 chars (the DOS 8.3 save
 * name), soft pixel cap 0xa0, mode 1.  The WndProc routes WM_CHAR / edit
 * keys into this editor (the same route page 4 uses — there is one
 * editor) and the page-3 input arm streams it into the filename field +
 * runs the Load/Cancel/scroll button strip. */
static void seed_page3_state(void)
{
    static const char *k_names[4] = { "lastturn", "old_turn", "safeturn", "test" };
    static uint8_t k_entries[4 * 0x41];
    static text_input_state_t k_p3_editor;          /* DAT_005cd550 */
    static const char k_seed[] = "lastturn";

    for (int e = 0; e < 4; e++) {
        int j = 0;
        for (; k_names[e][j]; j++) k_entries[e * 0x41 + j] = (uint8_t)k_names[e][j];
        k_entries[e * 0x41 + j] = 0;
    }

    text_input_init(&k_p3_editor, k_seed, /*max_length=*/8,
                    /*max_pixel_width=*/0xa0, /*initial_mode=*/1);
    k_p3_editor.insert_mode = 0;

    engine_title_render_set_filelist(k_entries, 0x41, 4, 0);
    engine_title_render_set_filelist_topname((const uint8_t *)k_seed);
    engine_title_render_set_filelist_editor(&k_p3_editor);
    engine_title_render_set_filelist_show_status(0);
    engine_title_render_bind_caret_metrics_table(gfx_glyph_measure_map());
}

static void bringup_title_menu(int with_panel)
{
    engine_title_render_set_page(g_title_page_num);

    if (!with_panel) {
        /* --title-bg: backdrop only, no panel/text composition. */
        if (!gfx_title_background_load(engine_title_render_page()))
            log_line("bringup_title_menu: background load failed");
        return;
    }

    title_bind_render_state();
    if (g_title_page_num == 7) {
        seed_page7_state();
    } else if (g_title_page_num == 9) {
        /* Page-9 dropdown over the page-7 setup screen (its backdrop).
         * Open the first option tab (tab 0, 2 values) with its current
         * value (k_tabs[0] = 0) highlighted — the state FUN_00433998
         * publishes on a tab click. */
        seed_page7_state();
        engine_title_render_set_option_detail(/*tab=*/0, /*count=*/2,
                                              /*bookmark=*/7);
        engine_title_render_set_selected_button(1);
    }
    title_render_current_page();
}

/* Scripted title-menu navigation demo (--title-nav DIR).  Drives the
 * real ported input dispatch with synthetic clicks and captures each
 * resulting page, proving the hover/click → page-transition → render
 * chain end-to-end.  A "click" is one engine_menu_title_input call with
 * the cursor parked on the target button and the LMB press edge set —
 * exactly what engine_compute_input_state produces for a real click. */
static void title_nav_click(int x, int y)
{
    engine_menu_click_in_t in = {0};
    in.cursor_x = x;
    in.cursor_y = y;
    in.lmb_newly_pressed = 1;   /* DAT_004eafb4 — the click edge */
    engine_menu_title_input(&in);
}

static void bringup_title_nav(const char *dir)
{
    /* Button x-centre + per-row y-centres match the rendered geometry
     * (x=0xe0, w=0xc0 → centre 0x140; rows at 0x5b/0x7f/.../0xeb,
     * h=0x18 → +0xc for the centre). */
    enum { BTN_CX = 0x140 };
    const int row0 = 0x5b + 0xc;   /* button 1 centre */
    const int row4 = 0xeb + 0xc;   /* button 5 centre ("Back." on page 2) */

    char path[MAX_PATH];

    title_bind_render_state();

    /* Step 0 — main menu (page 1). */
    engine_title_render_set_page(1);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_00_p1_mainmenu.bmp", dir);
    gfx_capture_backbuffer(path);
    log_line("title-nav: page %d → %s", engine_title_render_page(), path);

    /* Step 1 — click "Single player" (page 1 button 1) → page 2. */
    title_nav_click(BTN_CX, row0);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_01_p%d_options.bmp", dir, engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: clicked Single player → page %d → %s",
             engine_title_render_page(), path);

    /* Step 2 — click "Back." (page 2 button 5) → page 1. */
    title_nav_click(BTN_CX, row4);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_02_p%d_mainmenu.bmp", dir, engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: clicked Back → page %d → %s",
             engine_title_render_page(), path);

    /* Step 3 — page 1 → "Single player" (page 2) → "Play Now!" (page 2
     * button 1) → page 5, the "Play Now!" campaign chooser.  Retail's
     * neutral page-5 view (cursor off the two side-by-side buttons) shows no
     * highlight; the click frame consumes input before the hover refresh, so
     * park the selection neutral here to mirror that groundtruth (page 5's
     * hover arm clears it the same way once the cursor moves off a button). */
    title_nav_click(BTN_CX, row0);                 /* page 1 → 2 */
    title_nav_click(BTN_CX, row0);                 /* page 2 "Play Now!" → 5 */
    engine_title_render_set_selected_button(0);    /* neutral capture */
    title_render_current_page();
    wsprintfA(path, "%s\\nav_03_p%d_playnow.bmp", dir, engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: clicked Single player → Play Now! → page %d → %s",
             engine_title_render_page(), path);

    /* Step 4 — page 4, "Choose your title and your shield" (the Custom-
     * game setup screen).  The page-2 → page-4 transition action and the
     * name/shield defaults are seeded by save-load / indirect writes the
     * decompile doesn't spell out, so rather than drive the (unported)
     * input arm we force the page with the exact retail state captured via
     * `frida_capture.py --dump-globals` (run gt-p4-state): name "aaaaaa  ",
     * shield slot 1 selected (flash on, timer 0), gates 0/0 (tall layout +
     * Back/Continue), Continue enabled, overwrite-mode editor at cursor 0.
     * The caret is reset so the single static render lands it in the
     * off-phase, matching the mid-blink-off groundtruth frame. */
    static const uint8_t k_p4_name_default[] = "aaaaaa    ";  /* DAT_0053f1e0 */
    const uint8_t *k_p4_name = g_title_name
        ? (const uint8_t *)g_title_name : k_p4_name_default;
    static const unsigned char k_p4_shield_slots[6] = {0, 1, 0, 0, 0, 0};
    static text_input_state_t k_p4_editor;             /* DAT_005cd550 */
    {
        int i = 0;
        for (; k_p4_name[i] && i < (int)sizeof k_p4_editor.buf - 1; i++)
            k_p4_editor.buf[i] = k_p4_name[i];
        k_p4_editor.buf[i] = 0;
    }
    k_p4_editor.cursor = 0;
    k_p4_editor.insert_mode = 0;

    engine_title_render_set_choose_title_gates(0, 0);
    engine_title_render_set_choose_title_continue_disabled(0);
    engine_title_render_set_choose_title_shields(k_p4_shield_slots,
                                                 /*selected=*/1,
                                                 /*flash=*/1,
                                                 /*flash_timer=*/0);
    engine_title_render_set_choose_title_name(k_p4_name);
    engine_title_render_set_choose_title_editor(&k_p4_editor);
    engine_title_render_bind_caret_metrics_table(gfx_glyph_measure_map());
    gfx_caret_reset();
    engine_title_render_set_selected_button(0);    /* neutral capture */

    engine_title_render_set_page(4);
    title_render_current_page();
    /* Steady state: the per-frame title-update ladder re-renders the name
     * box every tick with the text shadow ON (FUN_0041f321 standalone, not
     * inside FUN_0041ef57's no-shadow bracket).  A captured retail frame
     * shows that shadowed name box, so reproduce it here before capture. */
    if (gfx_lock_canvas()) {
        engine_title_render_refresh_choose_title();
        gfx_unlock_canvas();
    }
    wsprintfA(path, "%s\\nav_04_p%d_choosetitle.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: forced page %d (choose title) → %s",
             engine_title_render_page(), path);

    /* Step 5 — page 3, "Loading a game." (the save/load file list).
     * Seeded from a --dump-globals capture (run gt-p3-state): 4 entries
     * (lastturn / old_turn→"old turn" / safeturn / test) in scan order,
     * top field "lastturn", first-visible 0, no selection highlight
     * (param 999), bottom status off (DAT_0057d3c4 == 0).  The gauntlet
     * action buttons are a separate per-frame overlay, not part of the
     * page-3 draw path (FUN_004148e4 → FUN_004149ec) ported here. */
    static const char *k_fl_names[4] = {
        "lastturn", "old_turn", "safeturn", "test",
    };
    static uint8_t k_fl_entries[4 * 0x41];
    for (int e = 0; e < 4; e++) {
        int j = 0;
        for (; k_fl_names[e][j]; j++) k_fl_entries[e * 0x41 + j] = k_fl_names[e][j];
        k_fl_entries[e * 0x41 + j] = 0;
    }
    static const uint8_t k_fl_topname[] = "lastturn";
    static text_input_state_t k_fl_editor;
    {
        int j = 0;
        for (; k_fl_topname[j]; j++) k_fl_editor.buf[j] = k_fl_topname[j];
        k_fl_editor.buf[j] = 0;
    }
    k_fl_editor.cursor = 0;
    k_fl_editor.insert_mode = 0;

    engine_title_render_set_filelist(k_fl_entries, 0x41, 4, 0);
    engine_title_render_set_filelist_topname(k_fl_topname);
    engine_title_render_set_filelist_editor(&k_fl_editor);
    engine_title_render_set_filelist_show_status(0);
    gfx_caret_reset();

    engine_title_render_set_page(3);
    title_render_current_page();
    /* Steady state: retail's per-frame UI refresh (FUN_004ba26e, 0x1f /
     * page-3 arm) repaints the list content + the gauntlet button strip
     * over the static frame every tick, so a captured frame shows the
     * buttons.  The full-scene draw above omits them; add the overlay. */
    if (gfx_lock_canvas()) {
        engine_title_render_filelist_buttons();
        gfx_unlock_canvas();
    }
    wsprintfA(path, "%s\\nav_05_p%d_loadgame.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: forced page %d (load game) → %s",
             engine_title_render_page(), path);

    /* Step 6 — page 9, the per-option detail dropdown (FUN_0041fdd6).
     * Reached from page 7 by clicking an option tab; the dropdown overlays
     * the page-7 setup screen (its backdrop) with the edited tab suppressed.
     * Force page 9 with the state FUN_00433998 publishes for tab 0 (2
     * values), its current value (k_tabs[0] = 0) highlighted. */
    seed_page7_state();
    engine_title_render_set_option_detail(/*tab=*/0, /*count=*/2,
                                          /*bookmark=*/7);
    engine_title_render_set_selected_button(1);
    engine_title_render_set_page(9);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_06_p%d_optiondetail.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: forced page %d (option detail) → %s",
             engine_title_render_page(), path);

    /* Step 7 — the page-9 input arm end-to-end: open tab 0's dropdown from
     * page 7 by clicking the tab, then pick the *second* value row, which
     * writes that value back to the option and closes the dropdown to page
     * 7.  Tab 0 ("Advanced Farming") seeds value 0 ("off"); picking row 2
     * sets value 1 ("on"), so the captured page-7 setup screen shows tab
     * 0's value cap flipped off→on — the visible proof the dropdown values
     * are now selectable (FUN_00433ce4 hover + FUN_00433a23 pick). */
    seed_page7_state();
    engine_title_render_set_page(7);

    /* Click option tab 0 (box (170,280)-(266,328) → centre (218,304)). */
    title_nav_click(218, 304);
    log_line("title-nav: clicked tab 0 → page %d (tab %d, %d values)",
             engine_title_render_page(),
             engine_title_render_option_detail_tab(),
             engine_title_render_option_detail_count());

    /* Click value row 2 (tab-0 panel (0xaa,0x118), row 1 box y∈[312,328)
     * → (218,320)).  Pick writes value 1 and returns to the bookmark. */
    title_nav_click(218, 320);
    log_line("title-nav: picked row 2 → page %d, tab 0 value = %d",
             engine_title_render_page(), engine_title_render_tab_value(0));

    title_render_current_page();
    wsprintfA(path, "%s\\nav_07_p%d_optionpicked.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: page %d (option picked: tab 0 = on) → %s",
             engine_title_render_page(), path);

    /* Step 8 — the Defaults button (FUN_004ae539 + FUN_004ae5e2).  Scramble
     * every option tab away from its default and capture the "before", then
     * click Defaults (box (321,181)-(397,236) → centre (359,208)) and capture
     * the "after": every value cap snaps back to the single-player defaults
     * {0,0,3,0,0,0,3,2,2,1,6,1}, with the player count clamped to the live
     * 5-seat roster (so tab 2 stays 3).  The two frames differ on all twelve
     * caps — the visible proof the Defaults dispatch resets the options. */
    seed_page7_state();
    engine_title_render_set_page(7);
    static const int k_scrambled[12] = { 1, 1, 0, 1, 3, 3, 0, 0, 4, 2, 0, 0 };
    engine_title_render_set_tab_values(k_scrambled, -1);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_08_p%d_scrambled.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: scrambled page-7 options (all 12 off-default) → %s",
             path);

    /* Click Defaults → apply_defaults + clamp(player count). */
    title_nav_click(359, 208);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_09_p%d_defaults.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: clicked Defaults → page %d "
             "(tab0=%d tab2=%d tab8=%d tab10=%d) → %s",
             engine_title_render_page(),
             engine_title_render_tab_value(0),
             engine_title_render_tab_value(2),
             engine_title_render_tab_value(8),
             engine_title_render_tab_value(10), path);

    /* Step 10 — the page-4 input arm end-to-end (FUN_004ba9c8 page-4 arm).
     * Reach page 4 the real way — page 2 → "Custom game" — so the load-dir
     * flag (DAT_0057d320) = 0 and the substate (DAT_0055302c) = 2, the
     * single-player Continue → page-7 path.  Then drive two real clicks:
     *   (a) click shield 3 — the flashing shield jumps slot 1 → slot 3;
     *   (b) click Continue — advances to the page-7 game-setup screen.
     * Captures the page-4 frame with shield 3 selected, then the page-7
     * frame Continue lands on, proving the choose-title buttons are live. */
    engine_title_render_set_page(2);
    title_nav_click(BTN_CX, 0xc7 + 0xc);           /* "Custom game" (row 3) → page 4 */
    seed_page4_state();                            /* render state for the screen */
    engine_title_render_set_selected_button(0);    /* neutral capture */

    /* Click shield 3 (box (0x120,0x8c)-(0x160,0xd2) → centre (0x140,0xaf)). */
    title_nav_click(0x140, 0xaf);
    gfx_caret_reset();
    if (gfx_lock_canvas()) {
        engine_title_render_refresh_choose_title();
        gfx_unlock_canvas();
    }
    title_render_current_page();
    wsprintfA(path, "%s\\nav_10_p%d_shieldpick.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: page %d, clicked shield 3 (slot1=%d slot3=%d) → %s",
             engine_title_render_page(),
             engine_title_render_choose_title_slot_owner(1),
             engine_title_render_choose_title_slot_owner(3), path);

    /* Click Continue (box (0x150,0xd7)-(0x210,0xf0) → centre (0x1b0,0xe3)). */
    seed_page7_state();                            /* page-7 backdrop content */
    title_nav_click(0x1b0, 0xe3);
    title_render_current_page();
    wsprintfA(path, "%s\\nav_11_p%d_continue.bmp", dir,
              engine_title_render_page());
    gfx_capture_backbuffer(path);
    log_line("title-nav: clicked Continue → page %d (game setup) → %s",
             engine_title_render_page(), path);
}

/* Translate a Linux-absolute path ("/opt/src/…") to its WSL-UNC form
 * ("\\wsl.localhost\<distro>\opt\src\…") so the Windows-side exe can
 * reach repo files through the 9P share.  Mirrors the inline logic the
 * capture-dir / call-trace paths use.  Non-absolute or non-WSL paths are
 * returned unchanged (copied into buf). */
static const char *translate_wsl_path(const char *p, char *buf, size_t cap)
{
    if (!p) return p;
    const char *distro = getenv("WSL_DISTRO_NAME");
    if (p[0] == '/' && distro && distro[0]) {
        wsprintfA(buf, "\\\\wsl.localhost\\%s%s", distro, p);
        for (char *q = buf; *q; q++)
            if (*q == '/') *q = '\\';
        return buf;
    }
    /* Copy through so callers can treat the result uniformly. */
    lstrcpynA(buf, p, (int)cap);
    return buf;
}

/* Read DIR/<name> (DIR already WSL-translated) into a caller buffer via
 * the Win32 file helper.  Returns bytes read (0 on failure, logged). */
static int strat_slurp(const char *dir, const char *name,
                       void *buf, size_t cap)
{
    char path[MAX_PATH];
    wsprintfA(path, "%s\\%s", dir, name);
    int n = io_read_file_at(path, buf, cap, 0);
    if (n == 0)
        log_line("strat-map: read %s failed", path);
    return n;
}

/* Live strategic-map BODY render (--strat-map DIR).  Stands in for the
 * unported new-game/load state producer + the screen-dispatch arm-0
 * composition (FUN_0040f5fd): native-loads the season sprite banks via
 * engine_pl8, seeds the captured live map arrays, binds the three ported
 * clusters at the captured camera, and runs the ported map-body driver
 * (engine_map_scene_render_body) into the locked back buffer.  Then dumps
 * the raw 8bpp canvas for a palette-applied diff vs retail, presents, and
 * captures a BMP.  The HUD chrome (menu bar / realm panel) is still
 * unported and absent — this validates the map BODY end-to-end in the
 * live drop-in with its OWN sprite data. */
static void bringup_strat_map(const char *dir)
{
    /* The captured strat-terrain1 frame's state: season 1d (winter),
     * normal view.  These drive engine_pl8's table offset and the
     * blitters' view-mode arm. */
    s_slot_0057c934 = 4;   /* DAT_0057c934 — season → *1d sprite banks */
    s_slot_0057cb18 = 0;   /* DAT_0057cb18 — view-mode normal */

    /* Native sprite load — the ported FUN_004984dc, reading the real
     * .pl8 files from the game dir (cwd, anchored by init_exe_dir). */
    if (!engine_pl8_load_strategic_banks(NULL)) {
        log_line("strat-map: engine_pl8 load failed — aborting bring-up");
        return;
    }
    log_line("strat-map: loaded 8 sprite banks natively (season 1d)");

    /* Seed the live map arrays from the captured dump (the same snapshot
     * the host scene harness binds).  The new-game/load path that
     * normally populates these is unported. */
    static uint8_t cells[0x8000];      /* tile_cells  (32768) */
    static uint8_t descr[0x8000];      /* descriptors (32768) */
    static uint8_t terr[0x10000];      /* territory_records (65536) */
    static uint8_t army[0x12710];      /* army_records (~71248) */
    if (strat_slurp(dir, "tile_cells.bin",  cells, sizeof cells) == 0 ||
        strat_slurp(dir, "descriptors.bin", descr, sizeof descr) == 0 ||
        strat_slurp(dir, "territory_records.bin", terr, sizeof terr) == 0 ||
        strat_slurp(dir, "army_records.bin", army, sizeof army) == 0) {
        log_line("strat-map: missing captured map state in %s — aborting", dir);
        return;
    }
    /* Also mirror the per-army AND per-territory rows into the LIVE
     * g_per_army_768 / g_per_territory_420 globals (unlike cells/descr/
     * terr/army above, which feed explicit binds — see bringup_strat_
     * load's analogous self-check copy).  Code that reads these globals
     * directly rather than through a bind (e.g. the --force-popup
     * verification seat's event-popup render arms — the army mirror was
     * added for the 0x12 arm, session 12; the territory mirror for the
     * 0x11 arm, session 13, which reads TERR_REC(payload_a) directly)
     * would otherwise see an all-zero array in this lighter-weight
     * bring-up. */
    memcpy(g_per_army_768, army, sizeof g_per_army_768);
    memcpy(g_per_territory_420, terr, sizeof g_per_territory_420);

    /* Captured strat-terrain1 camera scalars (globals_dump.json). */
    enum { CAM_X0 = 0, CAM_Y0 = 9, COL_START = 31, ROW_START = 78,
           COL_COUNT = 8, ROW_COUNT = 30, X_STEP = 60, Y_STEP = 15,
           X_HALF = 30, VIEW = 0, RENDER = 0, ROT = 0,
           OVERLAY_BASE = 0, Y_VIEWPORT = 30, ARROW_BASE = 0 };

    if (!gfx_lock_canvas()) {
        log_line("strat-map: gfx_lock_canvas failed");
        return;
    }
    int cw = gfx_paint_canvas_width();
    int ch = gfx_paint_canvas_height();
    uint8_t *canvas = gfx_paint_canvas_pixels();
    if (canvas) memset(canvas, 0, (size_t)cw * ch);

    /* ---- Cluster 1: pass-1 base terrain ---- */
    engine_map_terrain_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_terrain_bind_descriptors(descr);
    engine_map_terrain_bind_blobs(engine_pl8_bank(0), engine_pl8_bank(1),
                                  engine_pl8_bank(2), engine_pl8_bank(3),
                                  engine_pl8_bank(4));
    engine_map_terrain_set_blob_lengths((int)engine_pl8_bank_size(0),
                                        (int)engine_pl8_bank_size(1),
                                        (int)engine_pl8_bank_size(2),
                                        (int)engine_pl8_bank_size(3),
                                        (int)engine_pl8_bank_size(4));
    engine_map_terrain_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                   COL_COUNT, ROW_COUNT, X_STEP, Y_STEP,
                                   X_HALF);
    engine_map_terrain_set_view_mode(VIEW);
    engine_map_terrain_set_render_mode(RENDER);
    engine_map_terrain_set_empty_redraw_gate(0);
    engine_map_terrain_set_error_count(0);
    engine_map_terrain_reset_stats();

    static engine_dirty_rect_state_t dirty;
    dirty.dirty_mark = 0;
    dirty.dirty_xmin = cw - 1; dirty.dirty_xmax = 0;
    dirty.dirty_ymin = ch - 1; dirty.dirty_ymax = 0;
    dirty.below_status_redraw = 0;
    gfx_dispatch_set_alt_clip(0, 0, 0);

    /* ---- Cluster 2: overlay ---- */
    engine_overlay_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_overlay_render_bind_tile_descriptors(descr);
    engine_overlay_render_bind_overlay_blob(engine_pl8_bank(7));
    engine_overlay_render_bind_territory_records(terr);
    engine_overlay_render_bind_army_records(army);
    engine_overlay_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                      COL_COUNT, ROW_COUNT, X_STEP, Y_STEP,
                                      X_HALF);
    engine_overlay_render_bind_canvas(canvas, cw, ch);
    engine_overlay_render_set_dirty_state(&dirty);
    engine_overlay_render_set_view_mode(VIEW);
    engine_overlay_render_set_render_mode(RENDER);
    engine_overlay_render_set_animation_tick(0);
    engine_overlay_render_set_active_territory(0);
    engine_overlay_render_set_dev_flags(0, 0);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();

    /* ---- Cluster 3: feature walker ---- */
    engine_map_render_set_dirty_state(&dirty);
    engine_map_render_bind_tile_array((const int32_t *)cells, 0x104);
    engine_map_render_bind_tile_descriptors(descr);
    engine_map_render_bind_territory_records(terr);
    engine_map_render_bind_blobs(engine_pl8_bank(5), engine_pl8_bank(6),
                                 engine_pl8_bank(7));
    engine_map_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                  COL_COUNT, ROW_COUNT, X_STEP, Y_STEP,
                                  X_HALF);
    engine_map_render_set_view_mode(VIEW);
    engine_map_render_set_render_mode(RENDER);
    engine_map_render_set_rotation_offset(ROT);
    engine_map_render_set_y_viewport_offset(Y_VIEWPORT);
    engine_map_render_set_error_count(0);
    engine_map_render_reset_call_count();

    /* Compose the three clusters via the ported map-body driver. */
    engine_map_scene_set_anim_counters(OVERLAY_BASE << 4, ARROW_BASE << 4);
    engine_map_scene_render_body();

    log_line("strat-map: rendered body — drawA=%d drawB=%d sky=%d "
             "army=%d feat=%d (oob=%d err=%d)",
             engine_map_terrain_drawa_count(),
             engine_map_terrain_drawb_count(),
             engine_map_terrain_empty_draw_count(),
             engine_overlay_render_army_call_count(),
             engine_map_render_call_count(),
             engine_map_terrain_oob_copies(),
             engine_map_terrain_error_count());

    /* ---- HUD chrome: top menu bar (parchment + bevel + File/Options/
     * Help).  The state-free front of FUN_0040f5fd's FUN_00419c78 call;
     * draws into the same locked back buffer over the map body's top
     * rows.  Assets: the chrome sheet (panels.pl8 → DAT_0057d3d0), the
     * menu font (fntl2_14.pl8 → DAT_005af8f0), the char→glyph render /
     * measure maps, and the l2.eng localization blob (config_init).  The
     * data-dependent tail (realm flags / year / season / gold) is
     * deferred — see engine_hud_menubar.h. */
    {
        const uint8_t *chrome = gfx_load_chrome_sheet();
        const uint8_t *font   = gfx_load_menu_font();
        size_t loc_size = 0;
        const uint8_t *locblob = config_localization_data(&loc_size);
        if (chrome && font && locblob) {
            /* Screen byte 0 (strategic map) — gfx_text reads it as the
             * shadow-color discriminator (in-game gets the default pair,
             * not the title's gray). */
            engine_set_current_screen(0);
            gfx_dispatch_set_alt_clip(0, 0, 0);

            localization_state_t loc;
            loc.blob = (unsigned char *)locblob;
            loc.walker_offset = 0;

            int16_t menu_tbl[24];
            engine_hud_menubar_render_chrome(
                chrome, font,
                gfx_glyph_render_map(), gfx_glyph_measure_map(),
                &loc, engine_hud_menubar_default_table(menu_tbl));
            log_line("strat-map: menu bar drawn (parchment + File/Options/Help)");
        } else {
            log_line("strat-map: menu-bar assets missing "
                     "(chrome=%p font=%p loc=%p) — skipped",
                     (void *)chrome, (void *)font, (void *)locblob);
        }
    }

    /* Forced terrain-info panel render (verification seat) — set
     * OPENLORDS2_TI_CELL=<cell-index> to compute the terrain attrs for that
     * descriptor cell and paint the screen-4 "Here Be Dragons" panel over the
     * rendered map.  Bypasses the live input/popup path (blocked on the
     * report-screen popup frontier) so the panel render can be eyeballed vs
     * retail.  cell-index = row*0x40 + col (descriptor grid is 64×64). */
    {
        if (g_ti_cell >= 0) {
            int cell = g_ti_cell;
            engine_map_terrain_in_t cin;
            cin.cell_grid = descr;
            cin.army_recs = army;
            cin.tile_off  = cell * 8;
            engine_map_terrain_out_t cout;
            memset(&cout, 0, sizeof cout);
            engine_map_query_terrain_info(&cin, &cout);

            engine_terrain_info_attr_t a;
            memset(&a, 0, sizeof a);
            a.flags        = cout.flags;
            a.terrain_type = cout.terrain_type;
            a.head_link    = cout.head_link;
            a.owner        = cout.army_byte5;
            a.derived_bit  = cout.derived_bit;
            a.player_id    = (int)s_slot_0057c8cc;
            a.army_block   = army;

            size_t ti_loc_size = 0;
            const uint8_t *ti_locblob = config_localization_data(&ti_loc_size);
            localization_state_t ti_loc;
            ti_loc.blob = (unsigned char *)ti_locblob;
            ti_loc.walker_offset = 0;

            engine_terrain_info_render_ctx_t tctx;
            memset(&tctx, 0, sizeof tctx);
            tctx.chrome        = gfx_load_chrome_sheet();
            tctx.system_sheet  = gfx_system_sheet_current();
            tctx.title_font    = gfx_load_title_font();
            tctx.menu_font     = gfx_load_menu_font();
            tctx.glyph_table   = gfx_glyph_render_map();
            tctx.glyph_metrics = gfx_glyph_measure_map();
            tctx.icon_sheet    = gfx_load_terrain_icon_sheet();
            tctx.map_id        = (int)s_slot_0053f034;
            tctx.loc           = &ti_loc;
            if (tctx.title_font) {
                gfx_wrap_set_tall_font_marker(tctx.title_font);
            }
            engine_terrain_info_render(&a, &tctx);
            log_line("strat-map: forced terrain-info cell=%d flags=0x%02x type=%d "
                     "head=%d county=%d", cell, cout.flags, cout.terrain_type,
                     cout.head_link, cout.county_id);
        }
    }

    /* Dump the raw 8bpp canvas for the palette-applied parity diff. */
    if (canvas) {
        char path[MAX_PATH];
        wsprintfA(path, "%s\\port_strat.raw", dir);
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wn = 0;
            WriteFile(h, canvas, (DWORD)(cw * ch), &wn, NULL);
            CloseHandle(h);
            log_line("strat-map: raw canvas → %s (%d bytes)", path, cw * ch);
        } else {
            log_line("strat-map: could not open %s for raw dump", path);
        }
    }

    gfx_unlock_canvas();

    /* Forced popup-arm render (verification seat, --force-popup) — paints
     * over the just-rendered map.  Self-contained lock/unlock (the
     * renderer takes its own gfx_lock_canvas), so it must run after the
     * block above has released its lock.  The renderer reads the
     * resident in-game system sheet (gfx_system_sheet_current, retail
     * DAT_005bb540) — engine_tick's per-frame "ensure resident" call
     * (FUN_00499a1c(1)) never ran in this single-shot seat, so load it
     * explicitly or the renderer bails silently on a NULL asset. */
    if (g_force_popup_type >= 0) {
        gfx_load_system_sheet_select(1);
        engine_dev_force_popup_render(g_force_popup_type, g_force_popup_code,
                                      (uint8_t)g_force_popup_flag6,
                                      g_force_popup_payload_a);
        log_line("strat-map: forced popup type=%d code=%d flag6=%d",
                 g_force_popup_type, g_force_popup_code, g_force_popup_flag6);
    }

    /* Forced workforce icon-grid render (verification seat,
     * --force-workforce) — same self-contained lock/unlock shape as the
     * --force-popup block above (engine_workforce_hud_render takes its
     * own gfx_lock_canvas via the paint/sprite primitives it calls). */
    if (g_force_workforce_key >= 0) {
        int key = g_force_workforce_key;
        /* g_per_army_768, NOT the local `army` buffer above — the memcpy
         * mirroring `army` into it already ran earlier in this function,
         * and engine_workforce_hud_render reads g_per_army_768 directly. */
        uint8_t *rec = &g_per_army_768[key * 0x300];
        /* No known live producer for these fields yet (see
         * engine_workforce_hud.h) — inject plausible synthetic values so
         * the grid shows something regardless of what's in the captured
         * save data. category 0 -> row 5 -> scale/cur/min/max below;
         * every other category's row defaults to 0/0/0/0 (scale==0 ->
         * that row renders empty, which is a real, faithfully-modeled
         * state, not a bug). */
        *(int32_t *)(rec + 0xb8) = 10;    /* ARMY_WF_SCALE, row-independent */
        *(int32_t *)(rec + 0xc4 + 5 * 0xc) = 60;   /* ARMY_WF_CUR(5) */
        *(int32_t *)(rec + 0xc8 + 5 * 0xc) = 0;    /* ARMY_WF_MIN(5) */
        *(int32_t *)(rec + 0xcc + 5 * 0xc) = 100;  /* ARMY_WF_MAX(5) */
        gfx_lock_canvas();
        engine_workforce_hud_set_panel_mode(1);
        engine_workforce_hud_render(key, 0, 0);
        gfx_unlock_canvas();
        log_line("strat-map: forced workforce render army_key=%d", key);
    }

    /* Present + capture a BMP (surface palette → may be off-color; the
     * authoritative diff uses the raw dump + the captured retail .pal). */
    gfx_tick_present(1);
    {
        char path[MAX_PATH];
        wsprintfA(path, "%s\\port_strat.bmp", dir);
        gfx_capture_backbuffer(path);
        log_line("strat-map: backbuffer → %s", path);
    }
}

/* Compare a natively-produced array against the captured retail dump
 * (<dir>/<name>) over the first `len` bytes.  Logs MATCH or the first
 * differing byte offset.  Missing dump → logged + skipped (returns 1). */
static int strat_check_dump(const char *dir, const char *name,
                            const uint8_t *native, int len)
{
    char path[MAX_PATH];
    wsprintfA(path, "%s\\%s", dir, name);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        log_line("strat-load: dump %s absent — skipping check", name);
        return 1;
    }
    static uint8_t buf[0x12710];
    int n = (len < (int)sizeof buf) ? len : (int)sizeof buf;
    DWORD got = 0;
    ReadFile(h, buf, (DWORD)n, &got, NULL);
    CloseHandle(h);
    if ((int)got < n) {
        log_line("strat-load: dump %s short (%lu/%d) — partial check", name,
                 got, n);
        n = (int)got;
    }
    int first = -1;
    for (int i = 0; i < n; i++) {
        if (native[i] != buf[i]) { first = i; break; }
    }
    if (first < 0) {
        log_line("strat-load: %-22s NATIVE == retail dump (%d bytes) OK", name, n);
        return 1;
    }
    log_line("strat-load: %-22s MISMATCH at byte %d (native=0x%02x dump=0x%02x)",
             name, first, native[first], buf[first]);
    return 0;
}

/* Native strategic-map data path (--strat-load DIR).  Loads lastturn.sav +
 * builds the render arrays natively (engine_map_init), replacing the
 * --strat-map shim's captured-dump seeds, validates the native arrays
 * byte-exact against the dumps in DIR, then renders the body + menu bar.
 * DIR supplies only the retail dumps used for the self-check; all rendered
 * data is produced from the loaded save + L2_maps.dat. */
/* Port of FUN_004976a1 @ 0x4976a1 — the map-setup orchestrator that
 * retail's save-load core (FUN_004adfe6) runs immediately after a save's
 * schema table has been deserialised.  Builds the strategic-map render
 * arrays from the freshly-loaded schema globals, finalises the terrain
 * descriptors, loads the seasonal sprite banks + minimap political grids,
 * and switches the screen state to the in-game strategic map (0).  The
 * CALLER owns save_load (the schema globals are already populated on
 * entry) and any canvas lock + present / demand-redraw kick.
 *
 * This ports retail's DETERMINISTIC, render-affecting body.  The
 * peripheral state retail also touches in FUN_004976a1 is DEFERRED —
 * none of it changes the rendered frame (proven byte-exact without it)
 * and the timing scalars are non-deterministic, which would break the
 * byte-exact reproduction gate:
 *   - timing/anim scalars DAT_0056d8b4 / DAT_0052afcc / DAT_0055403c /
 *     DAT_005440c8 / DAT_00568d9c / DAT_00553fc8 (timeGetTime-seeded);
 *   - FUN_0043e9e2 per-realm turn-start reset + FUN_0049ca32 visibility
 *     bit flags (game state the loaded save already carries);
 *   - the immediate render FUN_0040f5fd(1) — the caller renders via the
 *     screen-0 dispatch arm (engine_screen_strat_render);
 *   - post-load audio cues FUN_00427d19 / FUN_00499d7d / FUN_00499aca;
 *   - the DAT_00553030 multiplayer tail (FUN_0043f4b7 / FUN_0043fd2a);
 *   - the dirty-rect full mark FUN_0045240a and DAT_0056d6a0 = 1 (the
 *     drop-in presents the whole back buffer, so the present region is
 *     moot — the caller sets demand-redraw to drive the live repaint).
 *
 * Banks/minimap are loaded from the default dir (NULL / ".") exactly as
 * the --strat-load bring-up did; the drop-in runs from the install dir.
 */
static void engine_map_post_load(void)
{
    int map_id = (int)s_slot_0053f034;
    s_slot_0057cb18 = 0;                        /* view-mode normal (retail
                                                 * reads DAT_0057cb18; our SP
                                                 * saves carry 0) */

    /* Retail's load tail (FUN_004976a1) re-arms the per-player session
     * state BEFORE rebuilding the map arrays — all save-state (blocks
     * 2/3), so the state oracle sees it:
     *   1. every ACTIVE player's turn-progress int (+0) = 1 (mid-turn);
     *   2. FUN_0043e9e2 — aux zero + human-slot deactivate + current-
     *      player activate + session stamp;
     * (the wall-clock timer scalars it also seeds stay deferred —
     * non-deterministic, runtime-only). */
    for (int p = 1; p < 6; p++) {
        if (g_player_strategic_352[p * 0x160 + 0x04] != 0)
            *(int32_t *)(g_player_strategic_352 + p * 0x160) = 1;
    }
    player_session_activate_current();          /* FUN_0043e9e2 */

    /* Derived render arrays (tile_cells) + camera from the loaded save
     * (retail FUN_004298c1 build_cells + FUN_00451fcc camera). */
    engine_map_init_strategic(NULL, map_id);
    int32_t *cells = engine_map_init_tile_cells();

    /* Restore the save-carried scroll origin (schema entries 16/17 =
     * DAT_005651b4 / DAT_005651b8).  Retail's load (FUN_004976a1) never
     * runs FUN_00498270, so the save's camera survives; the port reuses
     * engine_map_init_strategic (which resets it to the new-game default
     * 20/74), then restores here so the map renders at the SAVED scroll
     * — matching retail instead of the default view. */
    engine_map_init_set_scroll((int)s_slot_005651b4, (int)s_slot_005651b8);

    /* Minimap political grids (FUN_0046a037). */
    if (!engine_minimap_load_grids(".", map_id))
        log_line("post-load: minimap grid load failed (map_id=%d)", map_id);

    /* Bind ALL sim-runtime pointer seams (territory + map-query as before,
     * plus the army-state/army-move executors and economy aliases) to the
     * loaded save blocks — the load route enters the same live sim the
     * new-game route does, so it needs the same process-level wiring.
     * Without this the substate-0 trio's army-state driver would walk
     * occupants with a NULL cell grid.  Then finalize the terrain
     * descriptors: autotile the terrain sprite +3 (FUN_0046dbf5),
     * randomise the special sea tiles (FUN_0046dd34), rebuild the per-key
     * cell roster (FUN_0046da4b(0)) — reproduces retail's immediate
     * post-load descriptor table byte-exact. */
    engine_game_init_bind_runtime_tables(cells);
    engine_rng_single_in_t strat_rng = { 0x34518632u };  /* InitInstance seed */
    engine_territory_classify_feature_tiles(&strat_rng);   /* FUN_0046dbf5 */
    engine_territory_randomise_special_tiles(&strat_rng);   /* FUN_0046dd34 */
    engine_territory_rebuild_cell_roster(0);                /* FUN_0046da4b(0) */
    player_seed_visibility_bits();                          /* FUN_0049ca32 */

    /* Screen state → in-game strategic map (retail DAT_004eac50 = 0). */
    engine_set_current_screen(0);

    /* Seasonal sprite banks (FUN_004984dc) — sets the data-ready gate
     * (DAT_0053f050) the screen-0 render arm checks. */
    if (!engine_pl8_load_strategic_banks(NULL))
        log_line("post-load: pl8 bank load failed");
}

/* Load action bound to the title-menu load-confirm pump
 * (engine_load_pump_tick).  Retail FUN_004adfe6 → FUN_004976a1: read the
 * save into the schema globals, then run the strategic-map setup.  CWD is
 * the game dir, so the save filename + "Castles.dat" resolve relative to
 * it exactly as retail's save_load does.  Returns 0 on success. */
static int drop_in_load(const char *filename)
{
    /* Reproducibility: save_load rewrites its castles_path from the .sav
     * tail, but the in-game render reads the in-memory castle block (schema
     * block 1), not the file — the --strat-load seat proves a scratch path
     * works.  Write the derived Castles to a scratch temp file so a trace
     * replay NEVER overwrites the install's Castles.dat (the host tests +
     * the load-route reference depend on it).  See memory
     * project_save_redirect_reproducibility — the full save sandbox (agent
     * CreateFile redirect + pinned reference .sav) is the follow-up that
     * also makes the .sav LOAD deterministic. */
    char castles_scratch[MAX_PATH], tmp_dir[MAX_PATH];
    DWORD tn = GetTempPathA(sizeof tmp_dir, tmp_dir);
    if (tn == 0 || tn >= sizeof tmp_dir)
        lstrcpyA(tmp_dir, ".\\");
    wsprintfA(castles_scratch, "%s_ol2_load_castles.dat", tmp_dir);
    if (save_load(filename, castles_scratch) != 0) {
        log_line("load-pump: save_load(%s) FAILED", filename);
        return 1;
    }
    /* The live event queue's ring head IS save block 11 — seed it from
     * the loaded bytes before anything posts/consumes (retail's ring is
     * the save storage itself; the port's queue struct must catch up). */
    engine_event_queue_seed_from_save();
    /* Same stale-mirror class for the double-LFSR: save_load filled the
     * schema slots 18/19 (the .sav carries the live RNG state) but the
     * engine's working mirrors still hold the boot seeds — sync them or
     * the first ingame stir advances from the WRONG state (retail's
     * post-load state is exactly the save's, +1 per sub-step stir). */
    engine_rng_global_set(s_slot_0058fe28, s_slot_0058fe08);
    engine_map_post_load();
    /* Restore the strategic-map palette.  The strategic map renders under
     * the game's base palette base01.256 (installed at gfx_init; the
     * seasonal variation lives in the sprite banks, not the palette).
     * When we arrive here from the live title menu, the page-background
     * load has swapped in the menu palette (gateway.256), so the sand HUD
     * parchment + map indices would present as magenta.  Reinstall the
     * base palette on map entry.  (The --strat-load seat never shows the
     * menu, so base01.256 is already active there — this is the menu→map
     * transition's responsibility.) */
    if (!gfx_load_palette("base01.256"))
        log_line("load-pump: base01.256 palette restore failed");
    log_line("load-pump: loaded %s + map setup (screen=%d data_ready=%d)",
             filename, engine_current_screen(), engine_pl8_data_ready());
    return 0;
}

static void bringup_strat_load(const char *dir)
{
    /* 1. Load the real save into the schema globals (descriptors / territory
     * / realm structs / scalars incl. season + map id). */
    char castles_path[MAX_PATH];
    wsprintfA(castles_path, "%s\\_load_castles.dat", dir);
    if (save_load("lastturn.sav", castles_path) != 0) {
        log_line("strat-load: save_load(lastturn.sav) failed — aborting");
        return;
    }
    int map_id = (int)s_slot_0053f034;
    int season = (int)s_slot_0057c934;
    s_slot_0057cb18 = 0;                       /* view-mode normal */
    log_line("strat-load: loaded lastturn.sav (map_id=%d season=%d)",
             map_id, season);

    /* 2. Map setup — retail FUN_004976a1 (sprite banks, derived render
     * arrays + camera, terrain descriptor finalize, minimap political
     * grids, screen state → 0).  Factored into engine_map_post_load so the
     * SAME orchestration feeds the live load pump; see its definition
     * above for the deferred-state notes. */
    (void)season;
    engine_map_post_load();
    int32_t *cells = engine_map_init_tile_cells();

    /* 4. Self-check: the native arrays must byte-match the captured dumps. */
    int all_ok = 1;
    all_ok &= strat_check_dump(dir, "tile_cells.bin",
                               (const uint8_t *)cells, 32768);
    all_ok &= strat_check_dump(dir, "descriptors.bin", g_overworld_map, 32768);
    all_ok &= strat_check_dump(dir, "territory_records.bin",
                               g_per_territory_420, 63420);
    all_ok &= strat_check_dump(dir, "army_records.bin", g_per_army_768, 13056);
    log_line("strat-load: native-vs-dump check %s", all_ok ? "PASSED" : "FAILED");

    /* 6. Render the strategic-map frame via the ported screen-id-0 arm
     * (engine_screen_strat_render = FUN_0040f5fd).  This is the SAME
     * composition the live screen-dispatch arm 0 runs — the bring-up just
     * supplies the data path (save + banks + arrays + grids) and owns the
     * canvas lock + present, exactly as the live tick loop does.  Camera /
     * view-mode-0 scalars come from the ported FUN_00451fcc; the load
     * leaves the scroll at the engine default (col 20 / row 74), where
     * retail's post-load map frame sits. */
    if (!gfx_lock_canvas()) {
        log_line("strat-load: gfx_lock_canvas failed");
        return;
    }
    int cw = gfx_paint_canvas_width();
    int ch = gfx_paint_canvas_height();
    uint8_t *canvas = gfx_paint_canvas_pixels();

    engine_screen_strat_render(1);

    log_line("strat-load: rendered body — drawA=%d drawB=%d sky=%d "
             "army=%d feat=%d (oob=%d err=%d)",
             engine_map_terrain_drawa_count(),
             engine_map_terrain_drawb_count(),
             engine_map_terrain_empty_draw_count(),
             engine_overlay_render_army_call_count(),
             engine_map_render_call_count(),
             engine_map_terrain_oob_copies(),
             engine_map_terrain_error_count());
    {
        int realm = (int)s_slot_0057c8cc;
        int gold = (int)( g_player_strategic_352[realm * 0x160 + 0x118]
                        | g_player_strategic_352[realm * 0x160 + 0x119] << 8
                        | g_player_strategic_352[realm * 0x160 + 0x11a] << 16
                        | g_player_strategic_352[realm * 0x160 + 0x11b] << 24);
        log_line("strat-load: status — realm=%d year=%d season=%d gold=%d; "
                 "minimap cur_terr=%d; panel terr=%d owner=%d",
                 realm, (int)s_slot_00553edc, (int)s_slot_0057c934, gold,
                 (int)s_slot_0057c93c, (int)s_slot_0057c93c,
                 g_per_army_768[(int)s_slot_0057c93c * 0x300 + 5]);
    }

    if (canvas) {
        char path[MAX_PATH];
        wsprintfA(path, "%s\\port_strat.raw", dir);
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wn = 0;
            WriteFile(h, canvas, (DWORD)(cw * ch), &wn, NULL);
            CloseHandle(h);
            log_line("strat-load: raw canvas → %s (%d bytes)", path, cw * ch);
        }
    }

    gfx_unlock_canvas();
    gfx_tick_present(1);
    {
        char path[MAX_PATH];
        wsprintfA(path, "%s\\port_strat.bmp", dir);
        gfx_capture_backbuffer(path);
        log_line("strat-load: backbuffer → %s", path);
    }
}

/* Banks seam for the new-game orchestrator (the engine_game_init step that
 * runs retail's FUN_004984dc).  Loads the seasonal .pl8 sprite banks from
 * the install dir, setting the render data-ready gate the screen-0 arm
 * checks (engine_pl8_data_ready). */
static void newgame_load_banks(void)
{
    if (!engine_pl8_load_strategic_banks(NULL))
        log_line("newgame: pl8 bank load failed");
}

/* Shared new-game world build + game-start.  Assumes the config (realm
 * presets, player config, config substate scalars) is ALREADY set by the
 * caller — the only difference between the headless --newgame seat and the
 * live page-7 "Start" path is where that config comes from (the seat hard-codes
 * it; the live path reads the config screen via engine_game_init_apply_custom_
 * config).  This routine is the common tail both run: build the map cells +
 * camera from L2_maps.dat[map_id], bind the map record (the FUN_00467770
 * plane-unpack seam) + l2.eng localization + the minimap political grids, then
 * run engine_game_start (FUN_004329ec SP branch → FUN_00497ced + flip to the
 * ingame screen/state), and bridge the capital-centred scroll into the render
 * camera (see the scroll-storage-split note, engine-quirks #176).  Returns 1
 * on success, 0 if the map files can't be read. */
static int newgame_build_world_and_start(int map_id)
{
    s_slot_0053f034 = (uint32_t)map_id;             /* map id */
    s_slot_0057cb18 = 0;                            /* view-mode normal */

    /* tile_cells + camera from L2_maps.dat[map_id] (retail FUN_004298c1
     * build_cells + FUN_00451fcc camera, run by the config screen). */
    if (engine_map_init_build_cells(".", map_id, 0) == 0) {
        log_line("newgame: build_cells failed (L2_maps.dat read, map=%d)",
                 map_id);
        return 0;
    }
    engine_map_init_set_camera(0);

    /* Bind the loaded L2_maps.dat[map_id] record — the seam for FUN_00467770's
     * file read, which setup_overworld plane-unpacks into the terrain
     * descriptors.  One block = 0x80c1 B at map_id*0x80c1. */
    static uint8_t maprec[0x80c1];
    if (io_read_file_at("L2_maps.dat", maprec, sizeof maprec,
                        map_id * (long)sizeof maprec) == 0) {
        log_line("newgame: L2_maps.dat record read failed (map=%d)", map_id);
        return 0;
    }
    engine_territory_bind_strat_map_record(maprec);

    /* l2.eng localization blob for the NPC-schedule job names (retail
     * config_localization_data; NULL skips the loc walk — engine-quirks #107). */
    size_t loc_sz = 0;
    const uint8_t *loc = config_localization_data(&loc_sz);
    engine_npc_schedule_set_localization(loc, loc_sz);

    /* Minimap political grids (retail FUN_0046a037, run by the scenario
     * apply — NOT inside FUN_00497ced). */
    if (!engine_minimap_load_grids(".", map_id))
        log_line("newgame: minimap grid load failed (map=%d)", map_id);

    /* Run the new-game initialiser + game-start.  Bind the banks seam for the
     * orchestrator's render-data load; the turn-driver seam is bound in
     * WinMain.  engine_game_start flips g_sim_current_screen 0x1f→0 +
     * g_engine_state→3, which is what drives the live menu→map transition. */
    engine_game_init_bind_banks_fn(newgame_load_banks);
    engine_game_start();                            /* FUN_004329ec (SP) */
    engine_game_init_bind_banks_fn(NULL);

    /* Push the capital-centred scroll the new-game init computed (entries
     * 16/17 = DAT_005651b4/b8, written by recenter_on_capital inside
     * engine_game_start → setup_realms) into the render camera, so the first
     * strategic frame is centred on the player's capital — exactly as the LOAD
     * path does (engine_map_post_load).  See engine-quirks #176 (scroll-storage
     * split: the renderer reads engine_map_init statics, the save the s_slot
     * mirror). */
    engine_map_init_set_scroll((int)s_slot_005651b4, (int)s_slot_005651b8);
    return 1;
}

/* In-strategic-map active flag (retail DAT_0052afa4 = 1, set by the page-7
 * Start / Play Now! tail).  No ported reader yet — the live menu→map transition
 * is driven by g_sim_current_screen (flipped by engine_game_start) — but set it
 * faithfully so a future ported reader finds it.  File-static: only the
 * integration layer writes it today. */
static int g_sim_game_active;   /* DAT_0052afa4 */

/* Game-start seam binding — bound to engine_menu_input's page-7 "Start" action
 * (and reachable from any live-menu seat).  Reproduces retail's FUN_004335f0
 * arm-2 tail (config-apply FUN_00499dc3 + opening-stage load FUN_004329ec +
 * DAT_0052afa4 = 1) over the ported world build.  `mode` 0 = custom game
 * (page-7 "Start", config from the option tabs via FUN_00499dc3); `mode` 1 =
 * scenario / "Play Now!" (config from the scenario preset table via
 * FUN_00499e5d, selected by the difficulty picker's s_slot_0053f640 campaign
 * flag + s_slot_0053f258 index). */
static void newgame_enter_live(int mode)
{
    const uint8_t *prefs = config_player_prefs(NULL);

    if (mode == 1) {
        /* Scenario / Play Now!: presets come from the scenario/campaign preset
         * table (campaign flag + index latched by the difficulty picker).  This
         * sets the AI count (s_slot_0053f268) directly from the table, so it
         * runs BEFORE set_player_config, which then rewrites the same AI count
         * as count - human (count sized to the table AI count + the human). */
        engine_game_init_apply_scenario_config((int)s_slot_0053f640,
                                               (int)s_slot_0053f258);
        engine_game_init_set_player_config((int)s_slot_0053f268 + 1,
                                           /*human*/1, /*cur_player*/1, prefs);
        s_slot_0053f0d4 = 0xa;
        s_slot_0055302c = 2;
        if (!newgame_build_world_and_start((int)s_slot_0053f034))
            return;
        goto entered;
    }

    /* The config-screen player setup (FUN_0049792e) is deferred, so seed the
     * player slots here: a single-player custom game is 1 human + N AI, where
     * the page-7 player-count tab gives the total (selector + 2). */
    int total = engine_title_render_tab_value(2) + 2;
    engine_game_init_set_player_config(total, /*human*/1, /*cur_player*/1, prefs);

    /* Config-apply (FUN_00499dc3): the five realm presets + the standings/mode/
     * render-mode/budget config scalars + the AI count, all read from the
     * page-7 option tabs. */
    engine_game_init_apply_custom_config();

    /* Config substate scalars the deferred screen-entry would set (entries
     * 207 / 197) — mirror the headless seat so a save of the live game matches. */
    s_slot_0053f0d4 = 0xa;
    s_slot_0055302c = 2;

    /* Build the world + game-start (shared with the --newgame seat).  The map
     * id is whatever the config screen latched (country select → s_slot_0053f034). */
    if (!newgame_build_world_and_start((int)s_slot_0053f034))
        return;

entered:
    /* Restore the strategic-map palette (base01.256).  Arriving from the live
     * config screen, the page-background load swapped in the menu palette
     * (gateway.256), so the terrain indices would present as magenta — the
     * same menu→map transition fix the LOAD path applies in engine_map_post_load
     * (engine-quirks #156: the palette is screen-state-owned).  The --newgame
     * seat never shows the menu, so it doesn't need this. */
    if (!gfx_load_palette("base01.256"))
        log_line("newgame-live: base01.256 palette restore failed");

    g_sim_game_active = 1;   /* DAT_0052afa4 — now in the strategic map */
    log_line("newgame-live: started (map=%d realms=%d screen=%d state=%d "
             "data_ready=%d year=%d season=%d)",
             (int)s_slot_0053f034, (int)s_slot_0056d5dc, engine_current_screen(),
             g_engine_state, engine_pl8_data_ready(),
             (int)s_slot_00553edc, (int)s_slot_0057c934);
}

/* Render the first strategic frame via the screen-0 dispatch arm
 * (engine_screen_strat_render = FUN_0040f5fd) — the SAME composition the live
 * loop runs — and dump it for byte-validation as <base>.raw / <base>.bmp
 * (written to the install dir; the GUI exe can't write WSL UNC paths).  Shared
 * by the --newgame and --playnow seats.  Owns the canvas lock + present. */
static void newgame_dump_first_frame(const char *base)
{
    if (!gfx_lock_canvas()) {
        log_line("%s: gfx_lock_canvas failed", base);
        return;
    }
    int cw = gfx_paint_canvas_width();
    int ch = gfx_paint_canvas_height();
    uint8_t *canvas = gfx_paint_canvas_pixels();

    engine_screen_strat_render(1);

    log_line("%s: rendered body — drawA=%d drawB=%d sky=%d army=%d "
             "feat=%d (oob=%d err=%d)", base,
             engine_map_terrain_drawa_count(),
             engine_map_terrain_drawb_count(),
             engine_map_terrain_empty_draw_count(),
             engine_overlay_render_army_call_count(),
             engine_map_render_call_count(),
             engine_map_terrain_oob_copies(),
             engine_map_terrain_error_count());

    gfx_unlock_canvas();
    gfx_tick_present(1);

    if (canvas) {
        char fn[64];
        snprintf(fn, sizeof fn, "%s.raw", base);
        HANDLE h = CreateFileA(fn, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wn = 0;
            WriteFile(h, canvas, (DWORD)(cw * ch), &wn, NULL);
            CloseHandle(h);
            log_line("%s: raw canvas → %s (%d bytes)", base, fn, cw * ch);
        }
    }
    {
        char fn[64];
        snprintf(fn, sizeof fn, "%s.bmp", base);
        gfx_capture_backbuffer(fn);
        log_line("%s: backbuffer → %s", base, fn);
    }
}

/* New-game world-state producer + first-frame render (--newgame [MAP_ID]).
 * The new-game analogue of bringup_strat_load: instead of deserialising a
 * save it RUNS the ported new-game initialiser (engine_game_init =
 * FUN_00497ced) + game-start (engine_game_start = FUN_004329ec) over the
 * config-screen preconditions, then renders the first strategic frame
 * through the SAME screen-0 dispatch arm (engine_screen_strat_render).
 *
 * Mirrors retail's page-7 "Start" path: the config screen (scenario apply
 * FUN_00499e5d / custom apply FUN_00499dc3, both unported) sets the map id
 * + the five realm presets + builds the cells/camera; FUN_004335f0 →
 * FUN_004329ec then runs FUN_00497ced and flips to the ingame state.  This
 * seat supplies those preconditions inline.
 *
 * Presets come from the scenario table (retail 0x4d8e18, indexed by the
 * config screen's scenario selector; FUN_00499e5d reads a 0x20-stride row).
 * The map-0 row (record 4 @ 0x4d8e98) = gold 5000 / keep 4 / economy 2 /
 * resource 1 / troop 0.  troop_preset 0 disables the starting-army block,
 * so the deferred troop-spawn seam (audible, human-verification-gated) is
 * not exercised for the first frame.  Other map ids render the same terrain
 * (the presets drive the per-realm economy, not the descriptor build).
 *
 * Banks (FUN_004984dc, the render data-ready gate) ride the orchestrator's
 * bound banks seam; the minimap political grids (FUN_0046a037) are loaded
 * here exactly as retail's scenario-apply does — the new-game path does not
 * route through engine_map_post_load.  The per-turn driver (FUN_00448440)
 * is already bound to the orchestrator's step-17 seam in WinMain, so a
 * fresh game ticks to turn 1 during init, matching retail. */
static void bringup_newgame(int map_id)
{
    /* 1. Config-screen preconditions — supplied inline (the seat does not run
     * the config screen).  Realm presets = the retail values for a DEFAULT
     * single-player custom game on map 0 (ground truth
     * runs/capture/newgame-cfgdump1): economy 1 / resource 2 / troop 0 / gold
     * 1000 / keep 3.  The live page-7 path derives these from the config screen
     * instead (engine_game_init_apply_custom_config); this seat hard-codes them
     * so it can validate the world build without the config-screen UI.
     * set_realm_presets latches the init-read statics; they feed the per-army/
     * territory economy seeding (persisted), so they must match for the save to
     * byte-match. */
    engine_game_init_set_realm_presets(/*econ*/1, /*res*/2, /*troop*/0,
                                       /*gold_base*/1000, /*keep_level*/3);

    /* Players: a default custom game is 1 human (slot 1) + 4 AI (slots 2..5),
     * local player = 1.  set_player_config seeds the few config-screen-owned
     * fields + the AI count (s_slot_0053f268); the ported initialiser derives
     * the rest (roster from the map, work-locations from the NPC scheduler). */
    {
        /* Human player's name/title from the lords2.inf prefs (loaded by
         * config_init), matching retail's SP seat (save entry 2). */
        const uint8_t *prefs = config_player_prefs(NULL);
        engine_game_init_set_player_config(/*count*/5, /*human_count*/1,
                                           /*cur_player*/1,
                                           /*player_name*/prefs);
    }

    /* Two more config-screen substate scalars the headless seat skips.
     * Both are set by the new-game screen entry the live menu path runs but
     * this seat doesn't, and both persist to the save:
     *   - entry 207 (0x53f0d4) = 0xa — retail FUN_00497a34 @ 0x497abc.
     *   - entry 197 (0x55302c) =  2  — the SP "full-game" substate the live
     *     menu path (engine_menu_input.c) latches on entry. */
    s_slot_0053f0d4 = 0xa;
    s_slot_0055302c = 2;

    /* 2. Build the world + game-start (shared with the live Start path). */
    if (!newgame_build_world_and_start(map_id))
        return;

    log_line("newgame: init done (map=%d realms=%d screen=%d state=%d "
             "data_ready=%d year=%d season=%d)",
             map_id, (int)s_slot_0056d5dc, engine_current_screen(),
             g_engine_state, engine_pl8_data_ready(),
             (int)s_slot_00553edc, (int)s_slot_0057c934);

    /* Dump the new-game state to a save so retail can be driven to LOAD the
     * EXACT same world (a deterministic truth|mine|diff: retail's renderer
     * over the port's generated state).  save_write persists all schema
     * globals incl. the scroll (entries 16/17) + livestock state, so retail's
     * load lands on the same frame.  Dev-only side effect. */
    if (save_write("port_newgame.sav", "Castles.dat") != 0)
        log_line("newgame: save_write failed");
    else
        log_line("newgame: wrote port_newgame.sav (castle tail from Castles.dat)");

    /* 3. Render the first strategic frame + dump it for byte-validation. */
    newgame_dump_first_frame("port_newgame");
}

/* Scenario / "Play Now!" world-state producer + first-frame render
 * (--playnow [IDX]).  The scenario analogue of bringup_newgame: where that seat
 * hard-codes the realm presets, this sources them from the ported scenario
 * config-apply (engine_game_init_apply_scenario_config = FUN_00499e5d) — the
 * SAME function the live Play Now! button now runs — driven off the scenario
 * preset table row IDX (campaign flag 0 = the skirmish/deathmatch table; rows
 * 0..7).  Row 4 = map 0 (the England default); other rows pick a different map
 * + difficulty.  The map id, presets and AI count all come from the table.
 *
 * Run (space-free capture path — the exe arg tokenizer splits on spaces):
 *   /tmp/ol2.exe --playnow 4 --frames 5 --no-cd --allow-multi
 * Writes port_playnow.{sav,raw,bmp} for retail-load + byte-validation. */
static void bringup_playnow(int scenario_idx)
{
    /* 1. Scenario config-apply: presets from the table, not hard-coded.  Latch
     * the selector globals the difficulty picker would set (campaign flag 0,
     * index = IDX), then apply — which writes the map id + presets + AI count
     * (s_slot_0053f268) into the live config. */
    s_slot_0053f640 = 0;                            /* skirmish/deathmatch table */
    s_slot_0053f258 = (uint32_t)scenario_idx;
    engine_game_init_apply_scenario_config(0, scenario_idx);

    /* 2. Players: 1 human + the table's AI count (just latched into
     * s_slot_0053f268).  set_player_config rewrites the AI count to the same
     * value (count - human) + seeds the human slot/name + config substate
     * scalars — same as the --newgame seat. */
    {
        const uint8_t *prefs = config_player_prefs(NULL);
        engine_game_init_set_player_config((int)s_slot_0053f268 + 1,
                                           /*human_count*/1, /*cur_player*/1,
                                           prefs);
    }
    s_slot_0053f0d4 = 0xa;
    s_slot_0055302c = 2;

    /* 3. Build the world + game-start (shared tail).  The map id is the table
     * row's, latched into s_slot_0053f034 by the scenario apply. */
    int map_id = (int)s_slot_0053f034;
    if (!newgame_build_world_and_start(map_id))
        return;

    log_line("playnow: init done (scenario=%d map=%d realms=%d ai=%d screen=%d "
             "state=%d data_ready=%d year=%d season=%d)",
             scenario_idx, map_id, (int)s_slot_0056d5dc, (int)s_slot_0053f268,
             engine_current_screen(), g_engine_state, engine_pl8_data_ready(),
             (int)s_slot_00553edc, (int)s_slot_0057c934);

    if (save_write("port_playnow.sav", "Castles.dat") != 0)
        log_line("playnow: save_write failed");
    else
        log_line("playnow: wrote port_playnow.sav (castle tail from Castles.dat)");

    newgame_dump_first_frame("port_playnow");
}

/* Page-background reload hook (engine → gfx).  The engine_tick title
 * render step calls this before the canvas lock on each full redraw; the
 * engine hook is `void(*)(int)` but gfx_title_background_load returns int,
 * so wrap it.  Routes the .256 palette + .pl8 backdrop for `page` into
 * the back buffer (gateway / custom / skirmish family). */
static void title_bg_reload(int page)
{
    if (!gfx_title_background_load(page))
        log_line("title_bg_reload: background load failed (page %d)", page);
}

/* Live interactive title menu (--title-live).  Binds the page-independent
 * render state + the background-reload hook, sets the viewport game-coord
 * extents so the cursor maps 1:1 on the 640×480 menu, and puts the engine
 * into the title-screen state (screen 0x1f, page 1) with an initial
 * full-redraw request.  From here the normal main loop ticks the engine:
 * WndProc feeds mouse state, engine_tick resolves input → menu hover/click
 * → the locked title render step paints.  Stands in for the unported
 * boot→title transition (0x42b780). */
static void bringup_title_live(void)
{
    title_bind_render_state();
    engine_bind_title_bg_reload_fn(title_bg_reload);
    engine_set_viewport_game_extents(640, 480);   /* DAT_004eb274/DAT_004eac4c */
    /* Start on the requested page (--title-page N, default 1).  Page 7 (the
     * game-setup screen) needs its runtime state seeded — the same snapshot
     * the static page-7 bring-up uses — so its country list / roster / tabs
     * have content to interact with. */
    engine_title_render_set_page(g_title_page_num);
    if (g_title_page_num == 7) {
        seed_page7_state();
    } else if (g_title_page_num == 4) {
        /* Page-4 choose-title screen: seed shields + name + the live
         * editor so the name box is typeable on arrival (the boot→title
         * transition that normally seeds this is unported). */
        seed_page4_state();
        gfx_caret_reset();
    } else if (g_title_page_num == 3) {
        /* Page-3 file list: seed the save entries + filename editor so the
         * Load/Cancel/scroll buttons and the typeable filename field are
         * live on arrival (the page-2→3 transition is unported). */
        seed_page3_state();
        gfx_caret_reset();
    }
    engine_set_current_screen(0x1f);               /* DAT_004eac50 */
    engine_set_demand_redraw(2);                   /* first frame = full scene */
    log_line("title-live: engine in title state (screen 0x1f, page %d) — "
             "move the mouse over the menu; click to navigate.",
             g_title_page_num);
}

/* --strat-load-live: exercise the LIVE load path end-to-end.  Drive the
 * engine into the title state (like --title-live), then simulate a
 * page-3 "Load" click on the lastturn save — seed the selected-name
 * field (retail DAT_004ea130) and latch the load request
 * (DAT_005cd41c = 100).  The main loop then ticks the real ported
 * load-confirm pump (engine_load_pump_tick = FUN_004ad9f0), which arms a
 * ~150-frame confirm timer and, on expiry, runs drop_in_load (save_load +
 * engine_map_post_load) and transitions the engine to ingame — so the
 * genuine game loop reaches the screen-0 dispatch arm and paints the
 * strategic map natively.  Run with enough frames to clear the timer,
 * e.g. --frames 200. */
static void bringup_strat_load_live(void)
{
    title_bind_render_state();
    engine_bind_title_bg_reload_fn(title_bg_reload);
    engine_set_viewport_game_extents(640, 480);
    engine_title_render_set_page(1);
    engine_set_current_screen(0x1f);               /* DAT_004eac50 = 0x1f */
    engine_set_demand_redraw(2);

    engine_title_render_set_filelist_topname((const uint8_t *)"lastturn");
    engine_menu_input_set_filelist_load_request(100);   /* DAT_005cd41c = 100 */
    log_line("strat-load-live: title state up; latched Load(lastturn). The "
             "load-confirm pump will load + render the strategic map in "
             "~150 frames.");
}

/* --newgame-live: exercise the LIVE new-game "Start" path end-to-end through
 * the genuine menu input.  Drive the engine into the title state with the
 * page-7 config screen seeded (the same snapshot --title-live --title-page 7
 * uses: country list + SP-default option tabs + a 5-seat roster so the Start
 * gate passes), then main_loop_body fires the page-7 "Start" click on frame 5.
 * engine_menu_title_input routes it through the wired Start handler → the bound
 * game-start seam (newgame_enter_live) → world build + engine_game_start, which
 * flips g_sim_current_screen 0x1f→0.  The real main loop then ticks the screen-0
 * dispatch arm and paints the strategic map natively — the new-game analogue of
 * --strat-load-live.  Run with a few frames (e.g. --frames 10). */
static void bringup_newgame_live(void)
{
    title_bind_render_state();   /* also binds the game-start seam */
    engine_bind_title_bg_reload_fn(title_bg_reload);
    engine_set_viewport_game_extents(640, 480);
    engine_title_render_set_page(7);               /* straight to the config screen */
    seed_page7_state();                            /* country list + tabs + roster */
    s_slot_0053f034 = 0;                           /* default map 0 */
    engine_set_current_screen(0x1f);
    engine_set_demand_redraw(2);
    log_line("newgame-live: config screen up; the Start click fires on frame 5 "
             "→ genuine menu→ingame map transition.");
}

/* --input-segtrace FILE: replay a TAS input trace through the live tick.
 * Drives the engine into the live title state (page 1, with the page-3
 * file list pre-seeded so the trace's menu-nav reaches a populated save
 * list), then main_loop_body replays the trace's WM_* ops through the
 * genuine WndProc and captures frames on its {capture} ops.  The same
 * trace drives retail via frida_capture.py --input-segtrace, so the two
 * runs reach comparable frames for a 1:1 diff.  See input_trace.{c,h}. */
static void bringup_input_segtrace(void)
{
    if (input_trace_load(g_input_segtrace) != 0) {
        log_line("input-segtrace: failed to load trace '%s'", g_input_segtrace);
        g_shutdown = 1;
        return;
    }
    title_bind_render_state();
    engine_bind_title_bg_reload_fn(title_bg_reload);
    engine_set_viewport_game_extents(640, 480);   /* cursor maps 1:1 */
    /* The capture window is created hidden (show_window=0), so it never
     * receives WM_SIZE to populate g_client_screen — the cursor→game
     * mapping (game = cached * game_extent / client_width) would divide
     * by a zero width.  Pin it to the 640×480 logical client so the
     * replayed WM_MOUSEMOVE client coords map 1:1 (the cursor cache stays
     * disabled on the title screen, so the WndProc-cached coords are what
     * the mapping reads — engine-quirks #120). */
    SetRect(&g_client_screen, 0, 0, 640, 480);
    engine_title_render_set_page(1);               /* trace navigates from page 1 */
    seed_page3_state();                            /* file list ready for page 3 */
    engine_set_current_screen(0x1f);               /* DAT_004eac50 = 0x1f */
    engine_set_demand_redraw(2);
    /* No real mouse drives this hidden window — bypass GetCursorPos in
     * engine_tick_input_step so edge-scroll's raw-cursor hit-test reads
     * the trace's synthesized WM_MOUSEMOVE position instead of whatever
     * the real desktop cursor happens to be (project_headless_cursor_gap). */
    engine_set_input_cursor_override(1);
    log_line("input-segtrace: replaying '%s' (%d ops) through the live tick.",
             g_input_segtrace, input_trace_op_count());
}

/* The TAS anchor for the current engine state (input_trace's {wait}
 * targets).  Mirrors tools/frida/openlords2-agent.js anchorTick: keyed on
 * the engine state (g_engine_state) + screen id (engine_current_screen).
 * Only the anchors the load flow needs are emitted; extend as needed. */
static const char *current_tas_anchor(void)
{
    int screen = engine_current_screen();
    if (g_engine_state == 3 && screen == 0)
        return "MAP_FREEROAM";
    if (screen == 0x1f)
        return "TITLE";
    return "";
}

/* Per-frame TAS replay state (--input-segtrace): the actions due this
 * frame (messages posted at the top of main_loop_body, captures taken
 * after the tick) + the previous anchor for edge detection. */
static input_trace_actions_t g_seg_actions;
static const char           *g_seg_prev_anchor = "";

/* Append one JSON line to <capture-dir>/<name> (trace_studio's machine-
 * readable anchor/capture streams; mirrors the retail harness's
 * anchors.jsonl / captures.jsonl so the two sides pair by label). */
static void segtrace_jsonl(const char *name, const char *json_line)
{
    char path[MAX_PATH];
    wsprintfA(path, "%s\\%s", g_capture_dir ? g_capture_dir : ".", name);
    FILE *f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s\n", json_line);
    fclose(f);
}

static void main_loop_body(void)
{
    /* Scripted WM_CHAR typing demo (--title-type STR): post one character
     * per frame into the real WndProc, starting at frame 8, so the captured
     * frames show the name box filling in through the genuine keystroke
     * route.  Overwrite mode replaces from the cursor, so a 6-char STR fully
     * replaces the "aaaaaa" seed. */
    if (g_title_type && g_hwnd && g_frame_counter >= 8) {
        unsigned idx = g_frame_counter - 8;
        if (idx < strlen(g_title_type)) {
            PostMessageA(g_hwnd, WM_CHAR,
                         (WPARAM)(unsigned char)g_title_type[idx], 0);
        }
    }

    /* --newgame-live: fire the page-7 "Start" click on frame 5, routed through
     * the genuine menu input handler (engine_menu_title_input → Start gate →
     * game-start seam → newgame_enter_live → engine_game_start, which flips
     * g_sim_current_screen 0x1f→0).  After it returns the live tick's screen-0
     * dispatch paints the strategic map.  One-shot (frame == 5). */
    if (g_newgame_live && g_frame_counter == 5) {
        engine_menu_click_in_t click;
        memset(&click, 0, sizeof click);
        click.cursor_x = 281;          /* page-7 Start box (243,181)-(319,236) centre */
        click.cursor_y = 208;
        click.lmb_newly_pressed = 1;   /* press edge → type-1 button action */
        engine_menu_title_input(&click);
        log_line("newgame-live: fired page-7 Start click at frame 5.");
    }

    /* RNG pin (--rng-seed): hold the double-LFSR at the canonical seeds
     * while pre-ingame, exactly like the retail agent does each tick —
     * the menu path consumes RNG pulls, so without the pin the two
     * engines enter ingame from different states (state-parity slots
     * 18/19).  Ingame, the live RNG runs free on both sides. */
    if (g_rng_seed_pin && g_engine_state != 3)
        engine_rng_global_set(0x54657687u, 0x72641663u);

    /* TAS input-trace replay (--input-segtrace): post the trace's due
     * WM_* ops through the genuine WndProc this frame, so the engine
     * resolves the press edges exactly as for a real click.  The anchor
     * edge (engine-state / screen transition) drives the trace's {wait}
     * ops; captures are taken after the tick (below) so they grab the
     * rendered frame.  g_seg_actions carries the captures to that point. */
    if (g_input_segtrace && g_hwnd) {
        const char *anchor = current_tas_anchor();
        const char *edge = (strcmp(anchor, g_seg_prev_anchor) != 0)
                         ? anchor : "";
        g_seg_prev_anchor = anchor;
        if (edge[0]) {
            char line[96];
            wsprintfA(line, "{\"anchor\":\"%s\",\"frame\":%u}",
                      edge, g_frame_counter);
            segtrace_jsonl("anchors.jsonl", line);
        }
        input_trace_step((int)g_frame_counter, edge, &g_seg_actions);
        if (g_seg_actions.set_auto_dismiss >= 0)
            engine_set_popup_auto_dismiss(g_seg_actions.set_auto_dismiss);
        for (int i = 0; i < g_seg_actions.msg_count; i++)
            PostMessageA(g_hwnd, g_seg_actions.msgs[i].msg,
                         (WPARAM)g_seg_actions.msgs[i].wparam,
                         (LPARAM)g_seg_actions.msgs[i].lparam);
    }

    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_shutdown = 1;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    /* Per-frame engine dispatch — port of retail's
     *   FUN_0040e8bb (surface-lost guard + savepump dispatcher)
     *     ↓
     *   FUN_0040e890 (fast-skip dispatcher)
     *     ↓
     *   FUN_004b99c0 (per-tick driver — scaffold-only today;
     *                 wraps the same rate-gated present that the
     *                 previous direct-call version invoked).
     *
     * The guard returns 0 when the surface is still lost after a
     * Restore attempt or when shutdown_request has latched to 1;
     * in both cases the present is skipped, matching retail. */
    if (g_gfx_active) {
        /* Draw-step capture: arm BEFORE the tick renders so the dispatch hooks
         * record this frame's draws (the journal is gone by post-tick capture
         * time — the back buffer only holds the final composite). */
        int cap_this_frame = g_capture_draws && g_input_segtrace
                          && g_seg_actions.cap_count > 0;
        if (cap_this_frame)
            gfx_drawcap_begin_frame();
        /* --capture-force-redraw: on a capture tick, force a full strategic
         * re-render (g_demand_redraw=2) so the WHOLE map composites this tick —
         * the map-tile/terrain draws then land in the draw journal (in steady
         * state the map sits in the base + only the HUD redraws). The final
         * pixels match the incremental result; it just makes the map render
         * draw-attributable, and being a normal post-anchor capture tick it
         * stays port↔retail aligned (unlike the load-flip, which is inside the
         * non-aligned load-stretch). */
        if (g_capture_force_redraw && g_input_segtrace
            && g_seg_actions.cap_count > 0)
            engine_set_demand_redraw(2);
        engine_tick_guard();
        if (g_input_segtrace) {
            /* Take the trace's {capture} frames (labeled), now that the
             * tick has rendered + presented this frame. */
            for (int i = 0; i < g_seg_actions.cap_count; i++) {
                char path[MAX_PATH];
                const char *lbl = g_seg_actions.caps[i].label[0]
                                ? g_seg_actions.caps[i].label : "capture";
                wsprintfA(path, "%s\\%s.bmp",
                          g_capture_dir ? g_capture_dir : ".", lbl);
                gfx_capture_backbuffer(path);
                if (g_capture_raw) {
                    /* Trace Studio v3 raw payload (8bpp indices + palette) —
                     * the native viewer's container ingest reads this. */
                    wsprintfA(path, "%s\\%s.raw",
                              g_capture_dir ? g_capture_dir : ".", lbl);
                    gfx_capture_backbuffer_raw(
                        path, (unsigned)g_seg_actions.caps[i].frame);
                }
                if (g_capture_draws && gfx_drawcap_has_base()) {
                    /* Per-draw write-journal sidecar (base + ordered deltas) —
                     * makes this frame draw-steppable in the native viewer. */
                    wsprintfA(path, "%s\\%s.draws",
                              g_capture_dir ? g_capture_dir : ".", lbl);
                    gfx_drawcap_write(
                        path, (unsigned)g_seg_actions.caps[i].frame);
                }
                /* Save-schema snapshot at the same instant (trace_studio
                 * state parity; the retail agent mirrors this). */
                wsprintfA(path, "%s\\%s.state.json",
                          g_capture_dir ? g_capture_dir : ".", lbl);
                state_dump_write(path);
                {
                    char line[128];
                    wsprintfA(line, "{\"label\":\"%s\",\"frame\":%d}",
                              lbl, g_seg_actions.caps[i].frame);
                    segtrace_jsonl("captures.jsonl", line);
                }
                log_line("input-segtrace: captured '%s' (frame %d) → %s",
                         lbl, g_seg_actions.caps[i].frame, path);
            }
            if (cap_this_frame)
                gfx_drawcap_end_frame();
            if (input_trace_done())
                g_shutdown = 1;          /* trace exhausted → end the run */
        } else if (g_capture_dir
                && (g_frame_counter % g_capture_every) == 0) {
            char path[MAX_PATH];
            wsprintfA(path, "%s\\frame_%05u.bmp",
                      g_capture_dir, g_frame_counter);
            gfx_capture_backbuffer(path);
            wsprintfA(path, "%s\\frame_%05u-win.bmp",
                      g_capture_dir, g_frame_counter);
            gfx_capture_window(path);
        }
    }
    /* Deferred-shutdown escalator — port of retail's main-loop tail
     *
     *   if (DAT_005aeb68 == 1) {
     *       PostMessageA(hwnd, WM_CLOSE, 0, 0);
     *       DAT_005aeb68 = 0;
     *   }
     *
     * The state reaches 1 either via VK_ESCAPE (sets it directly)
     * or via the 3→1 transition that engine_tick_guard does after
     * a WM_CLOSE.  In the WM_CLOSE case DefWindowProc has already
     * started tearing the window down, so PostMessage WM_CLOSE
     * here is a no-op race — harmless either way.  In the
     * VK_ESCAPE case it's the actual escalator: next iteration
     * processes the posted WM_CLOSE, the WndProc falls through to
     * DefWindowProcA, and the OS teardown chain runs. */
    if (g_shutdown_request == 1 && g_hwnd) {
        PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
        g_shutdown_request = 0;
    }

    /* Flush this iteration's call-trace rows (no-op unless --call-trace).
     * Safe even when engine_tick early-returned this frame. */
    call_trace_end_frame();
}

static void frame_limiter(void)
{
    /* Placeholder limiter — real port will read the engine's own
     * timing globals.  Sleep(1) keeps the CPU from pegging while we
     * wait for the next iteration; the actual frame pacing arrives
     * once we port FUN_004bd180-equivalent. */
    Sleep(1);
    (void)FRAME_PERIOD_MS;
}

void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[openlords2] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static int acquire_singleton(void)
{
    g_singleton_mutex = CreateMutexA(NULL, FALSE, SINGLETON_MUTEX_NAME);
    if (!g_singleton_mutex) {
        log_line("CreateMutex failed: %lu", GetLastError());
        return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log_line("another OpenLords2 instance is already running "
                 "(mutex %s held). Pass --allow-multi to override.",
                 SINGLETON_MUTEX_NAME);
        CloseHandle(g_singleton_mutex);
        g_singleton_mutex = NULL;
        return 0;
    }
    return 1;
}

static void release_singleton(void)
{
    if (g_singleton_mutex) {
        CloseHandle(g_singleton_mutex);
        g_singleton_mutex = NULL;
    }
}

/* Anchor CWD + DLL search to the game data directory.
 *
 * Resolution order:
 *
 *   1. $OPENLORDS2_GAME_DIR — wins if set.  The dev flake exports
 *      this and propagates it across the WSL→Windows boundary via
 *      WSLENV/p (auto path-translation), so the .exe receives a
 *      native Windows path.  This is the path that lets the
 *      smoke harness copy our exe to /tmp/ for a fresh PE mapping
 *      while keeping data-file lookups (palettes, .pl8 atlases,
 *      .smk movies) pointed at the real game install.
 *
 *   2. GetModuleFileName parent dir — fallback for the production
 *      drop-in scenario where the .exe lives next to the data and
 *      the env var isn't set.
 *
 * Same shape as OpenMare's init_game_dir (cf. feedback_no_subagents
 * memory: OpenMare is the canonical harness/workflow reference). */
static void init_exe_dir(void)
{
    char buf[MAX_PATH];
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    int from_env = (env && env[0]);

    if (from_env) {
        /* WSLENV/p already translated /mnt/X/… to X:\… for us, but
         * defensive copy + bound check anyway. */
        size_t n = strlen(env);
        if (n == 0 || n >= sizeof(buf)) {
            log_line("OPENLORDS2_GAME_DIR too long (%zu chars) — "
                     "falling back to exe dir", n);
            from_env = 0;
        } else {
            memcpy(buf, env, n + 1);
        }
    }
    if (!from_env) {
        DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
        if (n == 0 || n >= sizeof(buf)) {
            log_line("GetModuleFileName failed: %lu", GetLastError());
            return;
        }
        char *slash = strrchr(buf, '\\');
        if (!slash) slash = strrchr(buf, '/');
        if (!slash) return;
        *slash = 0;
    }

    if (!SetCurrentDirectoryA(buf)) {
        log_line("SetCurrentDirectory(%s) failed: %lu", buf, GetLastError());
        return;
    }
    /* Also restrict the DLL search to safe + this dir, so explicit
     * LoadLibrary calls and static imports both prefer game-dir DLLs
     * over system32. */
    if (!SetDllDirectoryA(buf)) {
        log_line("SetDllDirectory(%s) failed: %lu", buf, GetLastError());
    }
    log_line("cwd anchored to %s (%s)", buf,
             from_env ? "from $OPENLORDS2_GAME_DIR" : "from exe dir");
}

static int parse_uint(const char *s, unsigned *out)
{
    if (!s || !*s) return 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end) return 0;
    *out = (unsigned)v;
    return 1;
}

/* Tiny ad-hoc cmdline parser — just enough for the bootstrap flags.
 * No quote handling; we control argv in the dev recipe. */
static void parse_cmdline(LPSTR lpCmd)
{
    char *p = lpCmd ? lpCmd : (char *)"";
    /* Tokenize on whitespace.  Modify in place — Windows gives us a
     * heap-allocated copy of the cmdline as LPSTR. */
    char *tokens[64] = {0};
    int   ntok = 0;
    while (*p && ntok < (int)(sizeof(tokens)/sizeof(tokens[0]))) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tokens[ntok++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = 0; p++; }
    }
    for (int i = 0; i < ntok; i++) {
        const char *a = tokens[i];
        if (!strcmp(a, "--show-window"))   g_show_window = 1;
        else if (!strcmp(a, "--fullscreen")) g_fullscreen = 1;
        else if (!strcmp(a, "--no-cd"))    g_skip_cd     = 1;
        else if (!strcmp(a, "--allow-multi"))    g_allow_multi = 1;
        else if (!strcmp(a, "--show-msgbox"))    g_show_msgbox = 1;
        else if (!strcmp(a, "--gfx-init"))       g_gfx_init    = 1;
        else if (!strcmp(a, "--title-bg")) {     /* bring-up: load the menu backdrop */
            g_title_bg = 1;
            g_gfx_init = 1;     /* needs DDraw */
        }
        else if (!strcmp(a, "--title-menu")) {   /* bring-up: backdrop + panel */
            g_title_menu = 1;
            g_gfx_init   = 1;   /* needs DDraw */
        }
        else if (!strcmp(a, "--title-page") && i + 1 < ntok) {
            uint32_t p = 0;
            if (parse_uint(tokens[++i], &p) && p >= 1) g_title_page_num = (int)p;
            g_title_menu = 1;   /* implies the full page composition */
            g_gfx_init   = 1;
        }
        else if (!strcmp(a, "--title-nav") && i + 1 < ntok) {
            g_title_nav_dir = tokens[++i];
            g_title_menu = 1;   /* reuse the page-render bring-up setup */
            g_gfx_init   = 1;
        }
        else if (!strcmp(a, "--title-name") && i + 1 < ntok) {
            g_title_name = tokens[++i];   /* page-4 name-box text override */
        }
        else if (!strcmp(a, "--title-live")) {   /* live interactive menu */
            g_title_live  = 1;
            g_gfx_init    = 1;   /* needs DDraw */
            g_show_window = 1;   /* interactive — must be visible */
        }
        else if (!strcmp(a, "--title-type") && i + 1 < ntok) {
            g_title_type = tokens[++i];   /* scripted WM_CHAR typing demo */
        }
        else if (!strcmp(a, "--strat-map") && i + 1 < ntok) {
            g_strat_map_dir = tokens[++i];   /* live strategic-map render */
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--strat-load") && i + 1 < ntok) {
            g_strat_load_dir = tokens[++i];  /* native load→build→render */
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--strat-load-live")) {
            g_strat_load_live = 1;           /* live load pump → ingame map */
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--newgame-live")) {
            g_newgame_live = 1;              /* live page-7 Start → ingame map */
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--newgame")) {
            g_newgame = 1;                   /* new-game init → first frame */
            g_newgame_map_id = 0;            /* default map 0 (matches lastturn) */
            /* Optional MAP_ID: consume the next token only if it is a
             * non-negative integer (so "--newgame" alone still works). */
            if (i + 1 < ntok) {
                const char *m = tokens[i + 1];
                int allnum = m[0] != '\0';
                for (const char *p = m; *p; p++)
                    if (*p < '0' || *p > '9') { allnum = 0; break; }
                if (allnum) g_newgame_map_id = atoi(tokens[++i]);
            }
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--playnow")) {
            g_playnow = 1;                   /* scenario apply → first frame */
            g_playnow_idx = 0;               /* default scenario row 0 (map 17) */
            /* Optional IDX: consume the next token only if it is a non-negative
             * integer (so "--playnow" alone still works). */
            if (i + 1 < ntok) {
                const char *m = tokens[i + 1];
                int allnum = m[0] != '\0';
                for (const char *p = m; *p; p++)
                    if (*p < '0' || *p > '9') { allnum = 0; break; }
                if (allnum) g_playnow_idx = atoi(tokens[++i]);
            }
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--input-segtrace") && i + 1 < ntok) {
            g_input_segtrace = tokens[++i];  /* TAS trace replay → ingame */
            g_gfx_init = 1;                  /* needs DDraw */
        }
        else if (!strcmp(a, "--rng-seed"))   g_rng_seed_pin = 1;
        else if (!strcmp(a, "--turbo"))      g_turbo_clock = 1;
        else if (!strcmp(a, "--turbo-step") && i + 1 < ntok) {
            if (!parse_uint(tokens[++i], &g_turbo_step) || g_turbo_step == 0) {
                fprintf(stderr, "[openlords2] bad --turbo-step (need >0)\n");
                g_turbo_step = 17;
            }
        }
        else if (!strcmp(a, "--audio-init"))     g_audio_init  = 1;
        else if (!strcmp(a, "--capture-to") && i + 1 < ntok) {
            g_capture_dir = tokens[++i];
            g_gfx_init = 1;     /* capture implies gfx */
        } else if (!strncmp(a, "--capture-to=", 13)) {
            g_capture_dir = a + 13;
            g_gfx_init = 1;
        } else if (!strcmp(a, "--ti-cell") && i + 1 < ntok) {
            g_ti_cell = (int)strtol(tokens[++i], NULL, 0);
        } else if (!strcmp(a, "--force-popup") && i + 1 < ntok) {
            /* TYPE:CODE[:FLAG6[:PAYLOAD_A]] — e.g. 16:278 (0x10:0x116) for
             * the unit-funding-warning arm.  Verification seat: paints the
             * popup over the --strat-map render, bypassing the live
             * event producer/consumer so the layout can be eyeballed
             * without constructing the triggering game state. */
            const char *spec = tokens[++i];
            char *end = NULL;
            g_force_popup_type = (int)strtol(spec, &end, 0);
            if (end && *end == ':') {
                g_force_popup_code = (int)strtol(end + 1, &end, 0);
                if (end && *end == ':') {
                    g_force_popup_flag6 = (int)strtol(end + 1, &end, 0);
                    if (end && *end == ':')
                        g_force_popup_payload_a = (int)strtol(end + 1, NULL, 0);
                }
            }
        } else if (!strcmp(a, "--force-workforce") && i + 1 < ntok) {
            /* KEY — verification seat: force the workforce icon-grid
             * overlay for army KEY over the --strat-map render, injecting
             * synthetic ARMY_WF_SCALE/CUR/MIN/MAX (no known live producer
             * yet — see engine_workforce_hud.h) so the grid shows
             * something regardless of what the captured save data has. */
            g_force_workforce_key = (int)strtol(tokens[++i], NULL, 0);
        } else if (!strcmp(a, "--save-sandbox") && i + 1 < ntok) {
            g_save_sandbox = tokens[++i];
        } else if (!strncmp(a, "--save-sandbox=", 15)) {
            g_save_sandbox = a + 15;
        } else if (!strcmp(a, "--capture-every") && i + 1 < ntok) {
            if (!parse_uint(tokens[++i], &g_capture_every)
                    || g_capture_every == 0) {
                fprintf(stderr,
                        "[openlords2] bad --capture-every (need >0)\n");
                g_capture_every = 25;
            }
        } else if (!strcmp(a, "--capture-raw")) {
            g_capture_raw = 1;     /* also emit <label>.raw (Trace Studio v3) */
        } else if (!strcmp(a, "--capture-draws")) {
            g_capture_draws = 1;   /* per-draw write-journal; implies --capture-raw */
            g_capture_raw = 1;
        } else if (!strcmp(a, "--capture-force-redraw")) {
            g_capture_force_redraw = 1;   /* full strat re-render on capture ticks */
        }
        else if (!strcmp(a, "--frames") && i + 1 < ntok) {
            if (!parse_uint(tokens[++i], &g_max_frames)) {
                fprintf(stderr, "[openlords2] bad --frames value\n");
            }
        } else if (!strncmp(a, "--frames=", 9)) {
            if (!parse_uint(a + 9, &g_max_frames)) {
                fprintf(stderr, "[openlords2] bad --frames= value\n");
            }
        } else if (!strcmp(a, "--call-trace") && i + 1 < ntok) {
            g_call_trace_path = tokens[++i];
        } else if (!strncmp(a, "--call-trace=", 13)) {
            g_call_trace_path = a + 13;
        } else if ((!strcmp(a, "--call-trace-frames") && i + 1 < ntok)
                   || !strncmp(a, "--call-trace-frames=", 20)) {
            /* Comma-separated sim-frame list, e.g. 10,11,12.  Parsed in
             * place; non-numeric tokens are skipped with a warning. */
            char *list = (a[19] == '=') ? (char *)(a + 20) : tokens[++i];
            for (char *t = strtok(list, ","); t; t = strtok(NULL, ",")) {
                unsigned v;
                if (g_call_trace_n_frames
                        >= sizeof(g_call_trace_frames)
                           / sizeof(g_call_trace_frames[0])) {
                    fprintf(stderr, "[openlords2] too many "
                            "--call-trace-frames (max 64)\n");
                    break;
                }
                if (parse_uint(t, &v)) {
                    g_call_trace_frames[g_call_trace_n_frames++] = v;
                } else {
                    fprintf(stderr, "[openlords2] bad "
                            "--call-trace-frames entry: %s\n", t);
                }
            }
        } else {
            fprintf(stderr, "[openlords2] unknown arg: %s\n", a);
        }
    }
}
