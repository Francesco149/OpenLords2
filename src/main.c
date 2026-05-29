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
#include "config.h"
#include "dev_hooks.h"
#include "engine.h"
#include "gfx.h"

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

/* "Engine wants the next frame redrawn" marker — retail DAT_0057d340
 * is set to 2 after WM_MOVE/WM_SIZE; the render driver consumes it.
 * Mirrored so the port stays mechanical when the driver lands. */
static int     g_render_dirty;

static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static int  register_window_class(void);
static int  create_main_window(void);
static void parse_cmdline(LPSTR);
static void init_exe_dir(void);
static int  acquire_singleton(void);
static void release_singleton(void);
static void frame_limiter(void);
static void main_loop_body(void);
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
        g_render_dirty = 2;
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
                    g_render_dirty = 2;
                } else {
                    log_line("WM_ACTIVATEAPP: gfx re-init failed "
                             "after Restore failed — DDraw is down");
                }
            } else {
                g_surface_lost = 0;
                g_render_dirty = 2;
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
        default:
            /* TODO(input): VK_BACK/RETURN/HOME/END/arrows/INS/DEL/
             * digits/F2..F12 all map to game-state functions in
             * retail.  See docs/findings/wndproc.md for the full
             * VK→FUN_xxxxx mapping. */
            break;
        }
        return 0;

    /* --- WM_KEYUP (0x101) — only VK_CONTROL is special. --------- */
    case WM_KEYUP:
        if (wp == VK_CONTROL) g_ctrl_down = 0;
        return 0;

    /* --- WM_CHAR (0x102), WM_SYSCHAR (0x105) ----------------------
     * Retail dispatches to game-state code.  Stubbed. */
    case WM_CHAR:
    case WM_SYSCHAR:
        /* TODO(input): port the per-screen H/V hotkeys and the
         * Alt-1..5 / Alt-X mappings from FUN_004b29be. */
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

static void main_loop_body(void)
{
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
        } else {
            fprintf(stderr, "[openlords2] unknown arg: %s\n", a);
        }
    }
}
