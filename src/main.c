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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dev_hooks.h"

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

static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
static int  register_window_class(void);
static int  create_main_window(void);
static void parse_cmdline(LPSTR);
static void init_exe_dir(void);
static int  acquire_singleton(void);
static void release_singleton(void);
static void frame_limiter(void);
static void main_loop_body(void);
static void log_line(const char *fmt, ...);

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

    g_base_time_ms = timeGetTime();

    while (!g_shutdown && (g_max_frames == 0 || g_frame_counter < g_max_frames)) {
        main_loop_body();
        g_frame_counter++;
        frame_limiter();
    }

    log_line("main loop exited after %u frame(s)", g_frame_counter);

    DestroyWindow(g_hwnd);
    UnregisterClassA(OPENLORDS2_CLASS, g_hInstance);

    timeEndPeriod(1);

    if (!g_show_msgbox) dev_hooks_uninstall_messagebox();
    release_singleton();

    return 0;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        g_shutdown = 1;
        return 0;
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

static void log_line(const char *fmt, ...)
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
