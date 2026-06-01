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
#include "engine_dirty_rect.h"
#include "engine_hud_menubar.h"
#include "engine_map_init.h"
#include "engine_territory.h"
#include "engine_map_render.h"
#include "engine_map_scene.h"
#include "engine_map_terrain.h"
#include "engine_overlay_render.h"
#include "engine_pl8.h"
#include "engine_title_render.h"
#include "engine_menu_input.h"
#include "text_input.h"
#include "gfx.h"
#include "io.h"
#include "io_path.h"
#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_glyph_table.h"
#include "gfx_paint.h"
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
static const char *g_strat_load_dir;   /* --strat-load DIR (native data path) */
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

    while (!g_shutdown && (g_max_frames == 0 || g_frame_counter < g_max_frames)) {
        main_loop_body();
        g_frame_counter++;
        frame_limiter();
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
     * bring-up so the menu text shadows match retail.  See quirk #114. */
    gfx_text_set_language_tag(0x1f);

    /* Init the shared gfx render state to its retail BSS-zero values
     * (the panel renderer assumes these, same as the panel unit test). */
    gfx_dispatch_set_clip_x_mode(0);
    gfx_dispatch_set_glyph_height_short(0);
    gfx_glyph_set_clip(0, 0, 0);
    gfx_glyph_set_dest(0, 0);
    gfx_glyph_set_glyph(0, 0, 0);
    gfx_glyph_set_color_modulation(0);
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
    engine_map_terrain_set_sky_gate_flag(0);
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
             engine_map_terrain_sky_count(),
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

    /* 2. Native sprite banks for the loaded season. */
    if (!engine_pl8_load_strategic_banks(NULL)) {
        log_line("strat-load: engine_pl8 load failed — aborting");
        return;
    }

    /* 3. Build the derived render arrays (tile_cells) + camera natively. */
    engine_map_init_strategic(NULL, map_id);
    int32_t *cells = engine_map_init_tile_cells();

    /* 3b. Load finalize — retail FUN_004976a1's terrain tail run after
     * save_load: autotile the terrain sprite +3 (FUN_0046dbf5), randomise the
     * special sea tiles (FUN_0046dd34), rebuild the per-key cell roster
     * (FUN_0046da4b(0)).  Reproduces retail's immediate post-load descriptor
     * table byte-exact (test_map_finalize_load_path_diagnostic: +3=100%).
     * The post-load WARMUP region->owner remap (+7) is a separate turn pass,
     * not yet ported — territory owner tints reflect the immediate-load state. */
    engine_territory_bind_cells(g_overworld_map);
    engine_territory_bind_terrain_tiles(cells);
    engine_territory_bind_cell_roster((int32_t *)g_misc_1360);
    engine_rng_single_in_t strat_rng = { 0x34518632u };  /* InitInstance seed */
    engine_territory_classify_feature_tiles(&strat_rng);   /* FUN_0046dbf5 */
    engine_territory_randomise_special_tiles(&strat_rng);   /* FUN_0046dd34 */
    engine_territory_rebuild_cell_roster(0);                /* FUN_0046da4b(0) */

    /* 4. Self-check: the native arrays must byte-match the captured dumps. */
    int all_ok = 1;
    all_ok &= strat_check_dump(dir, "tile_cells.bin",
                               (const uint8_t *)cells, 32768);
    all_ok &= strat_check_dump(dir, "descriptors.bin", g_overworld_map, 32768);
    all_ok &= strat_check_dump(dir, "territory_records.bin",
                               g_per_territory_420, 63420);
    all_ok &= strat_check_dump(dir, "army_records.bin", g_per_army_768, 13056);
    log_line("strat-load: native-vs-dump check %s", all_ok ? "PASSED" : "FAILED");

    /* 5. Render — same cluster wiring as --strat-map but bound to the native
     * arrays.  Camera: the view-mode-0 scalars come from the ported
     * FUN_00451fcc; the load leaves the scroll at the engine default
     * (FUN_00498270 sets col 20 / row 74), which is where retail's post-load
     * map frame sits. */
    const int CAM_X0 = engine_map_init_cam_x0();
    const int CAM_Y0 = engine_map_init_cam_y0();
    const int COL_START = engine_map_init_col_start();   /* 20 (load default) */
    const int ROW_START = engine_map_init_row_start();   /* 74 (load default) */
    const int COL_COUNT = engine_map_init_col_count();
    const int ROW_COUNT = engine_map_init_row_count();
    const int X_STEP = engine_map_init_x_step();
    const int Y_STEP = engine_map_init_y_step();
    const int X_HALF = engine_map_init_x_half_step();
    const int Y_VIEWPORT = engine_map_init_y_viewport();
    enum { VIEW = 0, RENDER = 0, ROT = 0 };

    if (!gfx_lock_canvas()) {
        log_line("strat-load: gfx_lock_canvas failed");
        return;
    }
    int cw = gfx_paint_canvas_width();
    int ch = gfx_paint_canvas_height();
    uint8_t *canvas = gfx_paint_canvas_pixels();
    if (canvas) memset(canvas, 0, (size_t)cw * ch);

    engine_map_terrain_bind_tile_array(cells, 0x104);
    engine_map_terrain_bind_descriptors(g_overworld_map);
    engine_map_terrain_bind_blobs(engine_pl8_bank(0), engine_pl8_bank(1),
                                  engine_pl8_bank(2), engine_pl8_bank(3),
                                  engine_pl8_bank(4));
    engine_map_terrain_set_blob_lengths((int)engine_pl8_bank_size(0),
                                        (int)engine_pl8_bank_size(1),
                                        (int)engine_pl8_bank_size(2),
                                        (int)engine_pl8_bank_size(3),
                                        (int)engine_pl8_bank_size(4));
    engine_map_terrain_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                   COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_map_terrain_set_view_mode(VIEW);
    engine_map_terrain_set_render_mode(RENDER);
    engine_map_terrain_set_sky_gate_flag(0);
    engine_map_terrain_set_error_count(0);
    engine_map_terrain_reset_stats();

    static engine_dirty_rect_state_t dirty;
    dirty.dirty_mark = 0;
    dirty.dirty_xmin = cw - 1; dirty.dirty_xmax = 0;
    dirty.dirty_ymin = ch - 1; dirty.dirty_ymax = 0;
    dirty.below_status_redraw = 0;
    gfx_dispatch_set_alt_clip(0, 0, 0);

    engine_overlay_render_bind_tile_array(cells, 0x104);
    engine_overlay_render_bind_tile_descriptors(g_overworld_map);
    engine_overlay_render_bind_overlay_blob(engine_pl8_bank(7));
    engine_overlay_render_bind_territory_records(g_per_territory_420);
    engine_overlay_render_bind_army_records(g_per_army_768);
    engine_overlay_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                      COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_overlay_render_bind_canvas(canvas, cw, ch);
    engine_overlay_render_set_dirty_state(&dirty);
    engine_overlay_render_set_view_mode(VIEW);
    engine_overlay_render_set_render_mode(RENDER);
    engine_overlay_render_set_animation_tick(0);
    engine_overlay_render_set_active_territory(0);
    engine_overlay_render_set_dev_flags(0, 0);
    engine_overlay_render_set_error_count(0);
    engine_overlay_render_reset_call_counts();

    engine_map_render_set_dirty_state(&dirty);
    engine_map_render_bind_tile_array(cells, 0x104);
    engine_map_render_bind_tile_descriptors(g_overworld_map);
    engine_map_render_bind_territory_records(g_per_territory_420);
    engine_map_render_bind_blobs(engine_pl8_bank(5), engine_pl8_bank(6),
                                 engine_pl8_bank(7));
    engine_map_render_bind_camera(CAM_X0, CAM_Y0, COL_START, ROW_START,
                                  COL_COUNT, ROW_COUNT, X_STEP, Y_STEP, X_HALF);
    engine_map_render_set_view_mode(VIEW);
    engine_map_render_set_render_mode(RENDER);
    engine_map_render_set_rotation_offset(ROT);
    engine_map_render_set_y_viewport_offset(Y_VIEWPORT);
    engine_map_render_set_error_count(0);
    engine_map_render_reset_call_count();

    engine_map_scene_set_anim_counters(0, 0);
    engine_map_scene_render_body();

    log_line("strat-load: rendered body — drawA=%d drawB=%d sky=%d "
             "army=%d feat=%d (oob=%d err=%d)",
             engine_map_terrain_drawa_count(),
             engine_map_terrain_drawb_count(),
             engine_map_terrain_sky_count(),
             engine_overlay_render_army_call_count(),
             engine_map_render_call_count(),
             engine_map_terrain_oob_copies(),
             engine_map_terrain_error_count());

    /* Menu-bar chrome (state-free; the data-dependent tail is still
     * deferred — see engine_hud_menubar.h). */
    {
        const uint8_t *chrome = gfx_load_chrome_sheet();
        const uint8_t *font   = gfx_load_menu_font();
        size_t loc_size = 0;
        const uint8_t *locblob = config_localization_data(&loc_size);
        if (chrome && font && locblob) {
            engine_set_current_screen(0);
            gfx_dispatch_set_alt_clip(0, 0, 0);
            localization_state_t loc;
            loc.blob = (unsigned char *)locblob;
            loc.walker_offset = 0;
            int16_t menu_tbl[24];
            engine_hud_menubar_render_chrome(
                chrome, font, gfx_glyph_render_map(), gfx_glyph_measure_map(),
                &loc, engine_hud_menubar_default_table(menu_tbl));
        }
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
        engine_tick_guard();
        if (g_capture_dir
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
        else if (!strcmp(a, "--audio-init"))     g_audio_init  = 1;
        else if (!strcmp(a, "--capture-to") && i + 1 < ntok) {
            g_capture_dir = tokens[++i];
            g_gfx_init = 1;     /* capture implies gfx */
        } else if (!strncmp(a, "--capture-to=", 13)) {
            g_capture_dir = a + 13;
            g_gfx_init = 1;
        } else if (!strcmp(a, "--capture-every") && i + 1 < ntok) {
            if (!parse_uint(tokens[++i], &g_capture_every)
                    || g_capture_every == 0) {
                fprintf(stderr,
                        "[openlords2] bad --capture-every (need >0)\n");
                g_capture_every = 25;
            }
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
