/* rh_real.c — `--real`: the standalone on the REAL platform (the product: rhost.c -DRH_PRODUCT).
 *
 * rhost's deterministic layers (virtual clock, fake DirectDraw / DirectSound / window / message pump, heap arena, file
 * overlay, handle wrappers, waveOut mixer, DirectPlay / SIGS fakes, the Smacker stubs) are NOT installed: every import
 * slot holds the real system export (retail_map / port_map bind them; rh_import_policy returns NULL under --real), so
 * the port runs on real DirectDraw (retail's own fullscreen 640x480x8 bring-up, 0x42e5c0), real DirectSound, a real
 * window and message loop, real timeGetTime pacing, real files in the game folder, real smackw32 / DirectPlay.
 * What the host still provides (the hand-mapped image is not a loader module):
 *   - module identity: GetModuleHandleA(NULL) = 0x400000, GetModuleFileNameA = <game>\Lords2.exe, a retail-shaped
 *     command line (rhost.c, as in every run);
 *   - resources: retail loads its icon / cursors with hInstance 0x400000 and its four dialogs with hInstance NULL (=
 *     the process exe). Neither holds them here (port_map leaves .rsrc absent), so LoadIconA / LoadCursorA /
 *     DialogBoxParamA take them from <game>\Lords2.exe mapped by the loader AS A DATAFILE (LOAD_LIBRARY_AS_DATAFILE |
 *     AS_IMAGE_RESOURCE: never executable, outside the image) — the user's own asset data, like the .256 files.
 *     Without the file: the system arrow / no icon (the dialogs then fail as a missing template does);
 *   - Windows 11 has no real 8-bit display mode: the compatibility layer DWM8And16BitMitigation emulates it (as for
 *     OpenSummoners' 16-bit mode, quirk #177 there). Lords2.exe itself gets its shims from the system database by
 *     name; our exe does not, so the product relaunches itself once with __COMPAT_LAYER (--no-compat-layer: not);
 *   - diagnostics: OutputDebugStringA / MessageBoxA mirrored into rhost.log; a fatal stop shows a message box.
 * Test switches: --real-exit-ms N (ExitProcess after N ms of wall time, at a frame boundary), --real-shot-ms N (a GDI
 * capture of the screen to <run>\real_shot_<ms>.bmp at each listed time), --real-click MS:X,Y (SetCursorPos + SendInput). */
#include "rhost.h"
#include "rl_va.h"

static HMODULE g_res;          /* Lords2.exe as a resource datafile (NULL: absent) */
static char g_res_path[MAX_PATH];
static LARGE_INTEGER g_rt0, g_rqf;
static int g_shot_done, g_click_done;   /* shots / clicks done (in order) */

static int is_image(HINSTANCE h) { return (uintptr_t)h == 0x400000u; }

static HICON WINAPI real_LoadIconA(HINSTANCE h, LPCSTR n)
{
    if (!is_image(h)) return LoadIconA(h, n);
    HICON r = g_res ? LoadIconA(g_res, n) : NULL;
    return r ? r : LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
}
static HCURSOR WINAPI real_LoadCursorA(HINSTANCE h, LPCSTR n)
{
    if (!is_image(h)) return LoadCursorA(h, n);
    HCURSOR r = g_res ? LoadCursorA(g_res, n) : NULL;
    if (!r) rh_log("real: cursor %u not loaded (%s) — the system arrow\n", (unsigned)(uintptr_t)n, g_res ? "not in the exe" : "no Lords2.exe");
    return r ? r : LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
}
static INT_PTR WINAPI real_DialogBoxParamA(HINSTANCE h, LPCSTR t, HWND p, DLGPROC f, LPARAM l)
{
    HINSTANCE use = (!h || is_image(h)) ? (HINSTANCE)g_res : h;
    INT_PTR r = use ? DialogBoxParamA(use, t, p, f, l) : -1;
    rh_log("real: DialogBoxParamA(template %u) -> %ld\n", (unsigned)(uintptr_t)t, (long)r);
    return r;
}
static void WINAPI real_OutputDebugStringA(LPCSTR s) { rh_log("ods: %s\n", s ? s : ""); OutputDebugStringA(s); }
static int WINAPI real_MessageBoxA(HWND h, LPCSTR text, LPCSTR cap, UINT t)
{
    rh_log("MessageBoxA [%s] %s\n", cap ? cap : "", text ? text : "");
    return MessageBoxA(h, text, cap, t);
}

/* Retail's first WM_PAINT (UpdateWindow right after CreateWindowExA in 0x4b2258) runs 0x451f70 → 0x452160, which Blts
 * through the primary surface 0x4eabd0 — still NULL (DirectDraw comes later) — unless WM_ACTIVATEAPP set the active flag
 * 0x4e65a8 first, and 0x4b29be sets it only when the window IS the foreground window: retail started without the right
 * to take the foreground (a background launcher) faults at its first paint (quirk #242). The port keeps that; the host
 * makes sure the window is foreground before CreateWindowExA returns (input attached to the foreground thread). */
static HWND WINAPI real_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int h, HWND par,
                                        HMENU menu, HINSTANCE hi, LPVOID lp)
{
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), ft = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    int att = ft && ft != me && AttachThreadInput(me, ft, TRUE);
    HWND r = CreateWindowExA(ex, cls, name, st, x, y, w, h, par, menu, hi, lp);
    if (r && !par && GetForegroundWindow() != r) { SetForegroundWindow(r); BringWindowToTop(r); }
    if (att) AttachThreadInput(me, ft, FALSE);
    if (r && !par) rh_log("real: main window %p %s\n", (void *)r, GetForegroundWindow() == r ? "foreground" : "NOT foreground (retail faults at its first paint: quirk #242)");
    return r;
}

/* Retail code pointers handed to the OS that it calls on ANOTHER thread: the music streamer 0x426dd5 (winmm's timer
 * thread) and the MP monitor thread 0x4b7052 (CreateThread). Their VAs hold entry thunks into rhost's row wrappers, which
 * belong to the game thread (they move esp 256 KB down the game stack). Under --real they go to a host shim that calls
 * the port through rl_va_call on the OS thread's own stack (the port code itself is retail's: busy flag 0x4d4764 etc.). */
static int in_code(uintptr_t p) { return p >= RT_TEXT_LO && p < (uintptr_t)RT_TEXT_LO + RT_TEXT_SZ; }
typedef struct { uint32_t va; DWORD_PTR user; } real_cb_t;
int rl_426dd5(int id, int msg, int user, int d1, int d2);   /* src/rl_io.c: the music streamer */
int rl_4b7052(void);                                        /* src/rl_mpnet.c: the MP monitor thread */
static int real_has_port(uint32_t va) { return va == 0x426dd5u || va == 0x4b7052u || rl_va_lookup(va); }
static void CALLBACK real_timer_shim(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2)
{
    const real_cb_t *c = (const real_cb_t *)user;
    if (c->va == 0x426dd5u) rl_426dd5((int)id, (int)msg, (int)c->user, (int)d1, (int)d2);
    else rl_va_call(c->va, 5, (uint32_t)id, (uint32_t)msg, (uint32_t)c->user, (uint32_t)d1, (uint32_t)d2);
}
static MMRESULT WINAPI real_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT fl)
{
    if (!in_code((uintptr_t)cb)) return timeSetEvent(delay, res, cb, user, fl);
    real_cb_t *c = HeapAlloc(GetProcessHeap(), 0, sizeof *c);   /* (one per timer start: a few per session) */
    c->va = (uint32_t)(uintptr_t)cb; c->user = user;
    if (!real_has_port(c->va)) rh_fatal("real: timer callback %08x has no port (rl_va_map)", c->va);
    return timeSetEvent(delay, res, real_timer_shim, (DWORD_PTR)c, fl);
}
static DWORD WINAPI real_thread_shim(LPVOID p)
{
    real_cb_t c = *(real_cb_t *)p; HeapFree(GetProcessHeap(), 0, p);
    return c.va == 0x4b7052u ? (DWORD)rl_4b7052() : rl_va_call(c.va, 1, (uint32_t)c.user);
}
static HANDLE WINAPI real_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD fl, LPDWORD tid)
{
    if (!in_code((uintptr_t)fn)) return CreateThread(sa, st, fn, arg, fl, tid);
    real_cb_t *c = HeapAlloc(GetProcessHeap(), 0, sizeof *c);
    c->va = (uint32_t)(uintptr_t)fn; c->user = (DWORD_PTR)arg;
    if (!real_has_port(c->va)) rh_fatal("real: thread start %08x has no port (rl_va_map)", c->va);
    return CreateThread(sa, st, real_thread_shim, c, fl, tid);
}

/* --log-win: the input messages the game's loop takes (diagnostics) */
static BOOL WINAPI real_PeekMessageA(LPMSG m, HWND h, UINT lo, UINT hi, UINT rm)
{
    BOOL r = PeekMessageA(m, h, lo, hi, rm);
    if (r && (rm & PM_REMOVE) && ((m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST && m->message != WM_MOUSEMOVE) ||
                                  (m->message >= WM_KEYFIRST && m->message <= WM_KEYLAST) || m->message == WM_ACTIVATEAPP))
        rh_log("real: msg %04x w=%08x l=%08x hwnd=%p frame %u\n", m->message, (unsigned)m->wParam, (unsigned)m->lParam, (void *)m->hwnd, g_frame);
    return r;
}
static BOOL WINAPI real_GetMessageA(LPMSG m, HWND h, UINT lo, UINT hi)
{
    BOOL r = GetMessageA(m, h, lo, hi);
    if (r > 0 && ((m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST && m->message != WM_MOUSEMOVE) ||
                  (m->message >= WM_KEYFIRST && m->message <= WM_KEYLAST)))
        rh_log("real: getmsg %04x w=%08x l=%08x frame %u\n", m->message, (unsigned)m->wParam, (unsigned)m->lParam, g_frame);
    return r;
}

/* before anything is mapped: run under DWM8And16BitMitigation (the parent waits and returns the child's code) */
void rh_real_compat(int argc, char **argv)
{
    (void)argc; (void)argv;
    char cur[1024] = "";
    GetEnvironmentVariableA("__COMPAT_LAYER", cur, sizeof cur);
    if (strstr(cur, "DWM8And16BitMitigation")) return;
    char v[1100];
    snprintf(v, sizeof v, "%s%sDWM8And16BitMitigation", cur, cur[0] ? " " : "");
    SetEnvironmentVariableA("__COMPAT_LAYER", v);
    STARTUPINFOA si = { .cb = sizeof si };
    PROCESS_INFORMATION pi; DWORD code = 1;
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return;   /* run as is */
    AllowSetForegroundWindow(pi.dwProcessId);   /* (the parent was launched in the foreground: the child's window may take it) */
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    ExitProcess(code);
}

void rh_real_install(const char *gamedir)
{
    QueryPerformanceFrequency(&g_rqf); QueryPerformanceCounter(&g_rt0);
    snprintf(g_res_path, sizeof g_res_path, "%s\\Lords2.exe", gamedir);
    g_res = LoadLibraryExA(g_res_path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!g_res) g_res = LoadLibraryExA(g_res_path, NULL, LOAD_LIBRARY_AS_DATAFILE);   /* (older loaders) */
    rh_log("real: resources from %s: %s\n", g_res_path, g_res ? "mapped as a datafile" : "absent (system cursor, no dialogs)");
    char cl[1024] = ""; GetEnvironmentVariableA("__COMPAT_LAYER", cl, sizeof cl);
    rh_log("real: __COMPAT_LAYER=%s\n", cl);
    iat_override("LoadIconA", real_LoadIconA);
    iat_override("LoadCursorA", real_LoadCursorA);
    iat_override("DialogBoxParamA", real_DialogBoxParamA);
    iat_override("OutputDebugStringA", real_OutputDebugStringA);
    iat_override("MessageBoxA", real_MessageBoxA);
    iat_override("CreateWindowExA", real_CreateWindowExA);
    iat_override("timeSetEvent", real_timeSetEvent);
    iat_override("CreateThread", real_CreateThread);
    if (O.log_win) { iat_override("PeekMessageA", real_PeekMessageA); iat_override("GetMessageA", real_GetMessageA); }
}

const char *rh_real_res_path(void) { return g_res ? g_res_path : NULL; }

static double real_ms(void)
{
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (double)(q.QuadPart - g_rt0.QuadPart) * 1000.0 / (double)g_rqf.QuadPart;
}

/* the screen through GDI (the desktop DC: the emulated 8-bit mode is composited) → a 24-bit BMP */
static void real_shot(void)
{
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
    BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = w, .biHeight = h, .biPlanes = 1,
                                     .biBitCount = 24, .biCompression = BI_RGB } };
    void *bits = NULL;
    HBITMAP bm = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bm && bits) {
        HGDIOBJ o = SelectObject(mdc, bm);
        BitBlt(mdc, 0, 0, w, h, sdc, 0, 0, SRCCOPY);
        GdiFlush();
        SelectObject(mdc, o);
        uint32_t stride = ((uint32_t)w * 3 + 3) & ~3u, n = stride * (uint32_t)h;
        char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\real_shot_%u.bmp", O.rundir, O.real_shot_ms[g_shot_done - 1]);
        FILE *f = fopen(p, "wb");
        if (f) {
            BITMAPFILEHEADER fh = { .bfType = 0x4d42, .bfSize = (DWORD)(sizeof fh + sizeof bi.bmiHeader + n),
                                    .bfOffBits = (DWORD)(sizeof fh + sizeof bi.bmiHeader) };
            fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(bits, 1, n, f); fclose(f);
        }
        rh_log("real: shot %dx%d -> %s (frame %u)\n", w, h, p, g_frame);
    }
    if (bm) DeleteObject(bm);
    DeleteDC(mdc); ReleaseDC(NULL, sdc);
}

/* frame boundary (frame_wrap, the game's stack) */
void rh_real_frame(void)
{
    if (!O.real_exit_ms && !O.n_real_shot && !O.n_real_click) return;
    double t = real_ms();
    static double up_at; static DWORD up_flag;   /* the release, a later frame: retail takes click edges per frame (0x4b191e) */
    if (up_at && t >= up_at) {
        INPUT u = { .type = INPUT_MOUSE, .mi = { .dwFlags = up_flag } };
        SendInput(1, &u, sizeof u); up_at = 0;
    }
    if (!up_at && g_click_done < O.n_real_click && t >= O.real_click[g_click_done][0]) {   /* through the OS: cursor + button */
        const uint32_t *c = O.real_click[g_click_done++];
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        LONG ax = (LONG)((c[1] * 65535u + (unsigned)sw / 2) / (unsigned)(sw > 1 ? sw - 1 : 1));
        LONG ay = (LONG)((c[2] * 65535u + (unsigned)sh / 2) / (unsigned)(sh > 1 ? sh - 1 : 1));
        INPUT in[2] = { { .type = INPUT_MOUSE, .mi = { .dx = ax, .dy = ay, .dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE } },
                        { .type = INPUT_MOUSE, .mi = { .dwFlags = c[3] ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN } } };
        SendInput(2, in, sizeof in[0]);
        up_at = t + 120; up_flag = c[3] ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        rh_log("real: click%s %u,%u at %u ms (frame %u, screen %02x)\n", c[3] ? " (right)" : "", c[1], c[2], c[0], g_frame, G8(L2_SCREEN));
    }
    if (g_shot_done < O.n_real_shot && t >= O.real_shot_ms[g_shot_done]) { g_shot_done++; real_shot(); }
    if (O.real_exit_ms && t >= O.real_exit_ms) {
        rh_log("real: --real-exit-ms %u reached at frame %u (screen %02x)\nRESULT status=ok reason=real-exit frame=%u\n",
               O.real_exit_ms, g_frame, G8(L2_SCREEN), g_frame);
        ExitProcess(0);
    }
}
