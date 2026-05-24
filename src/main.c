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

#include "dev_hooks.h"
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
 * Updated by WM_MOUSEMOVE; read by sim/render code (not ported yet). */
static int16_t g_mouse_x;
static int16_t g_mouse_y;
/* Mouse button-down bitmask — retail DAT_004eabc2.  bit0=LMB, bit1=RMB. */
static uint8_t g_mouse_buttons;
/* Double-click latch — retail DAT_004eada1.  bit0=LMB-dbl, bit1=RMB-dbl. */
static uint8_t g_mouse_dblclk;
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
/* Deferred-shutdown state — retail DAT_005aeb68.
 *   3 = WM_CLOSE received, drain sim before destroying window
 *   1 = sim ack'd the close, escalate next frame
 *   4 = sim asked for shutdown itself (no WM_CLOSE)
 * Today we have no main-loop escalator, so WM_CLOSE also force-quits
 * by setting g_shutdown directly.  Once FUN_004b99c0 lands, the
 * inline DestroyWindow() in wndproc disappears and this flag drives
 * the dance from the main loop instead. */
static int     g_shutdown_request;

/* Client-area RECT in *screen* coords — retail DAT_004ea180/+4/+8/+c.
 * Recomputed on WM_MOVE/WM_SIZE.  Used by DDraw blit destinations. */
static RECT    g_client_screen;

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
        } else {
            log_line("gfx_init failed — continuing without DDraw");
        }
    }

    g_base_time_ms = timeGetTime();

    while (!g_shutdown && (g_max_frames == 0 || g_frame_counter < g_max_frames)) {
        main_loop_body();
        g_frame_counter++;
        frame_limiter();
    }

    log_line("main loop exited after %u frame(s)", g_frame_counter);

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
         * FUN_00425d35, FUN_004b7180(1) before PostQuitMessage. */
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
     * Retail just sets DAT_005aeb68 = 3 — a "drain then quit" flag
     * consumed by the sim/render driver (see main-loop-tick.md).
     * Until that escalator is ported, we also DestroyWindow inline so
     * the drop-in still quits cleanly on Alt+F4 / taskbar-close. */
    case WM_CLOSE:
        g_shutdown_request = 3;
        DestroyWindow(hwnd);    /* TODO(sim): remove once escalator exists */
        return 0;

    /* --- WM_ACTIVATEAPP (0x1c) -------------------------------------
     * Retail toggles g_focused based on wp + foreground + iconic
     * status, logs "OK :Active." or "OK :Not active.", and on
     * focus-regained-with-lost-surfaces runs the DDraw restore path.
     * We don't have surfaces yet, so just track the flag and log. */
    case WM_ACTIVATEAPP: {
        HWND fg = GetForegroundWindow();
        if (wp == 0 || fg != hwnd || IsIconic(hwnd))
            g_focused = 0;
        else
            g_focused = 1;
        log_line(g_focused ? "OK :Active." : "OK :Not active.");
        /* TODO(gfx): if (g_surface_lost && g_focused) → DDraw Restore
         * via the FUN_004b2730 / FUN_004b2922 path. */
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
            log_line("VK_ESCAPE → shutdown request");
            g_shutdown_request = 1;
            DestroyWindow(hwnd);    /* TODO(sim): remove once escalator exists */
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
    /* Per-frame present — mirror of the inner Blt of retail's
     * FUN_00452160.  No rate-limiter yet (retail gates on a 50-tick
     * counter), no Smacker handoff yet.  Once those land, this
     * call moves into a fuller render-driver port. */
    if (g_gfx_active) gfx_present();
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

/* Chdir into the directory of our own .exe.  This is what makes a
 * drop-in actually drop-in: when the user double-clicks Lords2.exe
 * (replaced by openlords2.exe), the OS sets CWD to wherever the
 * shortcut points, NOT necessarily to the exe's location.  Without
 * this, a launcher that runs the exe from /tmp/ would resolve DLL
 * imports against /tmp/ first.  Same plumbing OpenMare's init_game_dir
 * implements; here we always derive the path from GetModuleFileName
 * rather than reading $OPENMARE_GAME_DIR — the drop-in is meant to
 * work in real deploys where the dev env vars aren't set. */
static void init_exe_dir(void)
{
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        log_line("GetModuleFileName failed: %lu", GetLastError());
        return;
    }
    /* Strip the basename. */
    char *slash = strrchr(buf, '\\');
    if (!slash) slash = strrchr(buf, '/');
    if (!slash) return;
    *slash = 0;

    if (!SetCurrentDirectoryA(buf)) {
        log_line("SetCurrentDirectory(%s) failed: %lu", buf, GetLastError());
        return;
    }
    /* Also restrict the DLL search to safe + the exe dir, so explicit
     * LoadLibrary calls and static imports both prefer game-dir DLLs
     * over system32. */
    if (!SetDllDirectoryA(buf)) {
        log_line("SetDllDirectory(%s) failed: %lu", buf, GetLastError());
    }
    log_line("cwd anchored to %s", buf);
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
