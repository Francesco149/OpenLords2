/*
 * OpenLords2 — drop-in replacement for Lords2.exe (bootstrap skeleton).
 *
 * Boots, registers a window class, opens a hidden 800x600 window, runs
 * a PeekMessage / DispatchMessage loop with a Sleep(1) frame limiter,
 * exits cleanly on WM_QUIT or after `--frames N` iterations.  No DDraw
 * yet, no game-state init — those land in subsequent slices.
 *
 * Deliberate deviations from retail (phase 1 — bootstrap defaults):
 *   - Window stays hidden by default.  Pass --show-window to display.
 *     This is the opposite default from a "real drop-in" so that running
 *     the binary under the dev loop never disrupts the host desktop.
 *   - Window class name is our own "OpenLords2Main" placeholder.  Retail
 *     class TBD once the Ghidra analysis lands.
 *   - Auto-cd into the directory of our own .exe at startup so the
 *     DDraw/DSound/Smacker DLL resolves no matter where the .exe was
 *     invoked from.  This matches retail behavior (the .exe is meant to
 *     sit inside the game directory and finds its data via relative paths
 *     from CWD per L2.INI's `CDDrive=.`, `WindowsDir=.`).  It's also a
 *     footgun guard learned from OpenMare: without it, dev runs of the
 *     drop-in from outside the game dir silently picked up system32
 *     DDraw instead of any wrapper sitting next to retail.
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

#define OPENLORDS2_CLASS  "OpenLords2Main"
#define OPENLORDS2_TITLE  "Lords of the Realm II"

/* Lords II's original resolution is 640x480 256-color.  The GOG patch
 * supports 800x600 too — TBD once we ship rendering.  For the bootstrap
 * window we just pick something reasonable. */
#define DEFAULT_WIDTH   800
#define DEFAULT_HEIGHT  600

/* 50 FPS = 20 ms per frame is a reasonable placeholder until we know
 * retail's actual frame-limiter constant. */
#define FRAME_PERIOD_MS  20

static HINSTANCE g_hInstance;
static HWND      g_hwnd;
static int       g_show_window;     /* --show-window: ShowWindow at startup */
static int       g_skip_cd;         /* --no-cd: skip auto-cd to exe dir */
static int       g_allow_multi;     /* --allow-multi: skip single-instance mutex */
static unsigned  g_max_frames;      /* --frames N: voluntary exit after N frames (0 = forever) */
static int       g_show_msgbox;     /* --show-msgbox: skip MessageBox→stderr hook */
static int       g_shutdown;
static HANDLE    g_singleton_mutex;

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

static int register_window_class(void)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = OPENLORDS2_CLASS;
    return RegisterClassA(&wc) != 0;
}

static int create_main_window(void)
{
    g_hwnd = CreateWindowExA(
        0,
        OPENLORDS2_CLASS,
        OPENLORDS2_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DEFAULT_WIDTH, DEFAULT_HEIGHT,
        NULL, NULL, g_hInstance, NULL);
    return g_hwnd != NULL;
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
