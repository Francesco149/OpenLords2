/* rh_win.c — the virtual window and message queue retail Lords2 runs against.
 *
 * Retail never sees a real HWND, a real message, or the real cursor:
 *   - its window is RH_FAKE_HWND (a constant); rhost owns a real window behind it (hidden and
 *     off-screen by default; visible + scaled with --show / --record);
 *   - Peek/Get/Post/Send/Dispatch/TranslateMessage work on a VIRTUAL queue. Only three sources
 *     feed it: retail's own Post/SendMessage, a fixed synthetic creation/activation sequence,
 *     and the input trace (replayed, or recorded from the real window — the same path);
 *   - GetCursorPos is the position of the last mouse message retail dispatched; the "screen" is
 *     640×480 with the client at (0,0); GDI/USER metrics are constants.
 * So the input → state mapping is a pure function of the trace, on any host.
 *
 * Trace (.jsonl): {"f": F, "m": WM_x, "w": wParam, "l": lParam} — the event is posted after
 * frame F-1 completes and dispatched before frame F runs (frame F is the first to see it).
 * Legacy {"t": ...} = {"f": ...}. "#" comments, {"meta": {...}} lines. Mouse lParam =
 * MAKELPARAM(x, y) in 640×480 game coordinates. */
#include "rhost.h"

#define LOGW(...) do { if (O.log_win) rh_log(__VA_ARGS__); } while (0)
#define FAKE_HDC    ((HDC)(uintptr_t)0x0dc00001u)
#define SCR_W 640
#define SCR_H 480

static WNDPROC g_retail_proc;        /* retail's class procedure (0x4b29be) */
static HWND g_real;                  /* rhost's real window */
static int g_created, g_visible, g_activated;
static LONG g_style, g_exstyle;
static uint32_t g_focus, g_cursor_handle, g_nfake;
static int g_cx = SCR_W / 2, g_cy = SCR_H / 2;      /* the virtual cursor (dispatched) */

HWND rh_win_real(HWND h) { return (uint32_t)(uintptr_t)h == RH_FAKE_HWND ? g_real : h; }

/* ================================================================ virtual queue */
#define QN 4096
static MSG g_q[QN]; static uint32_t g_qh, g_qt;
static uint32_t g_nposted, g_ndispatched;
static void q_put(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (g_qt - g_qh >= QN) rh_fatal("virtual message queue overflow");
    MSG *x = &g_q[g_qt++ % QN];
    x->hwnd = h; x->message = m; x->wParam = w; x->lParam = l; x->time = rh_vnow(); x->pt.x = g_cx; x->pt.y = g_cy;
    g_nposted++;
}
void rh_win_post(UINT m, WPARAM w, LPARAM l) { q_put((HWND)(uintptr_t)RH_FAKE_HWND, m, w, l); }

static LRESULT call_retail(UINT m, WPARAM w, LPARAM l)
{
    if (!g_retail_proc) return 0;
    if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) { g_cx = (short)LOWORD(l); g_cy = (short)HIWORD(l); }
    return CallWindowProcA(g_retail_proc, (HWND)(uintptr_t)RH_FAKE_HWND, m, w, l);
}

static BOOL WINAPI my_PeekMessageA(MSG *msg, HWND h, UINT lo, UINT hi, UINT rm)
{
    (void)h;
    for (uint32_t i = g_qh; i != g_qt; i++) {
        MSG *x = &g_q[i % QN];
        if ((lo || hi) && (x->message < lo || x->message > hi)) continue;
        *msg = *x;
        if (rm & PM_REMOVE) {   /* remove entry i, keep order */
            for (uint32_t k = i; k != g_qh; k--) g_q[k % QN] = g_q[(k - 1) % QN];
            g_qh++;
        }
        return TRUE;
    }
    return FALSE;
}
static BOOL WINAPI my_GetMessageA(MSG *msg, HWND h, UINT lo, UINT hi)
{
    if (!my_PeekMessageA(msg, h, lo, hi, PM_REMOVE))
        rh_fatal("GetMessageA on an empty virtual queue (frame %u): a modal loop waits for input", g_frame);
    return msg->message != WM_QUIT;
}
static BOOL WINAPI my_TranslateMessage(const MSG *m) { (void)m; return FALSE; }   /* WM_CHAR comes from the trace */
static LRESULT WINAPI my_DispatchMessageA(const MSG *m)
{
    g_ndispatched++;
    if ((uint32_t)(uintptr_t)m->hwnd != RH_FAKE_HWND) return 0;
    if (O.log_input && m->message != WM_MOUSEMOVE) rh_log("input f%u dispatch %04x w=%08lx l=%08lx\n", g_frame, m->message, (unsigned long)m->wParam, (unsigned long)m->lParam);
    return call_retail(m->message, m->wParam, m->lParam);
}
static BOOL WINAPI my_PostMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LOGW("win: retail PostMessage %04x w=%08lx (f%u)\n", m, (unsigned long)w, g_frame);
    q_put(h, m, w, l);
    return TRUE;
}
static LRESULT WINAPI my_SendMessageA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if ((uint32_t)(uintptr_t)h == RH_FAKE_HWND) return call_retail(m, w, l);
    rh_log("win: SendMessageA to foreign hwnd %p msg %04x\n", (void *)h, m);
    return 0;
}
static void WINAPI my_PostQuitMessage(int code) { LOGW("win: PostQuitMessage(%d) (f%u)\n", code, g_frame); q_put(NULL, WM_QUIT, (WPARAM)code, 0); }
/* DefWindowProc for the fake window: the few defaults retail relies on, nothing OS-side */
static LRESULT WINAPI my_DefWindowProcA(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)h; (void)l;
    switch (m) {
    case WM_NCCREATE: return TRUE;
    case WM_CLOSE: call_retail(WM_DESTROY, 0, 0); return 0;    /* DestroyWindow */
    case WM_SYSCOMMAND: if ((w & 0xfff0) == SC_CLOSE) q_put((HWND)(uintptr_t)RH_FAKE_HWND, WM_CLOSE, 0, 0); return 0;
    case WM_ERASEBKGND: return 1;
    default: return 0;
    }
}

/* ================================================================ window APIs (fake HWND) */
static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM w, LPARAM l);
static ATOM WINAPI my_RegisterClassA(const WNDCLASSA *wc)
{
    if (!g_retail_proc) g_retail_proc = wc->lpfnWndProc;
    rh_log("win: RegisterClassA '%s' proc %p (virtual)\n", HIWORD((uintptr_t)wc->lpszClassName) ? wc->lpszClassName : "#", (void *)wc->lpfnWndProc);
    static ATOM a = 0xc0de;
    return a++;
}
static void activate_once(void)
{
    if (g_activated) return;
    g_activated = 1;
    LOGW("win: synthetic WM_ACTIVATEAPP(1) (f%u)\n", g_frame);
    call_retail(WM_ACTIVATEAPP, 1, 0);
}
static HWND WINAPI my_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID lp)
{
    (void)cls; (void)p; (void)m; (void)i;
    if (g_created) rh_fatal("CreateWindowExA: a second window (%s)", name ? name : "");
    g_created = 1; g_style = (LONG)st; g_exstyle = (LONG)ex;
    rh_log("win: CreateWindowExA '%s' style %08lx %dx%d at %d,%d -> fake hwnd %08x\n", name ? name : "", (unsigned long)st, w, h, x, y, RH_FAKE_HWND);
    /* the creation messages a WS_POPUP window of the screen's size gets, in order */
    CREATESTRUCTA cs = { lp, i, m, p, h, w, y, x, (LONG)st, name, cls, ex };
    call_retail(WM_NCCREATE, 0, (LPARAM)&cs);
    call_retail(WM_CREATE, 0, (LPARAM)&cs);
    call_retail(WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, h));
    call_retail(WM_MOVE, 0, MAKELPARAM(x, y));
    if (st & WS_VISIBLE) { g_visible = 1; activate_once(); }
    return (HWND)(uintptr_t)RH_FAKE_HWND;
}
static BOOL WINAPI my_ShowWindow(HWND h, int cmd)
{
    (void)h;
    BOOL was = g_visible;
    g_visible = cmd != SW_HIDE;
    if (g_visible) activate_once();
    return was;
}
static BOOL WINAPI my_UpdateWindow(HWND h) { (void)h; return TRUE; }
static BOOL WINAPI my_InvalidateRect(HWND h, const RECT *r, BOOL e) { (void)h; (void)r; (void)e; return TRUE; }
static HDC WINAPI my_BeginPaint(HWND h, PAINTSTRUCT *ps) { (void)h; memset(ps, 0, sizeof *ps); ps->hdc = FAKE_HDC; return FAKE_HDC; }
static BOOL WINAPI my_EndPaint(HWND h, const PAINTSTRUCT *ps) { (void)h; (void)ps; return TRUE; }
static HWND WINAPI my_SetFocus(HWND h) { uint32_t was = g_focus; g_focus = (uint32_t)(uintptr_t)h; return (HWND)(uintptr_t)was; }
static HWND WINAPI my_GetForegroundWindow(void) { return (HWND)(uintptr_t)RH_FAKE_HWND; }
static BOOL WINAPI my_SetForegroundWindow(HWND h) { (void)h; return TRUE; }
static BOOL WINAPI my_IsIconic(HWND h) { (void)h; return FALSE; }
static HMENU WINAPI my_GetMenu(HWND h) { (void)h; return NULL; }
static LONG WINAPI my_GetWindowLongA(HWND h, int i)
{
    (void)h;
    if (i == GWL_STYLE) return g_style;
    if (i == GWL_EXSTYLE) return g_exstyle;
    if (i == GWL_WNDPROC) return (LONG)(uintptr_t)g_retail_proc;
    return 0;
}
static LONG WINAPI my_SetWindowLongA(HWND h, int i, LONG v)
{
    (void)h;
    LONG old = my_GetWindowLongA(h, i);
    if (i == GWL_STYLE) g_style = v;
    else if (i == GWL_EXSTYLE) g_exstyle = v;
    else if (i == GWL_WNDPROC) g_retail_proc = (WNDPROC)(uintptr_t)v;
    else rh_log("win: SetWindowLongA index %d ignored\n", i);
    LOGW("win: SetWindowLongA %d = %08lx (f%u)\n", i, (unsigned long)v, g_frame);
    return old;
}
static BOOL WINAPI my_SetWindowPos(HWND h, HWND a, int x, int y, int cx, int cy, UINT fl)
{ (void)h; (void)a; LOGW("win: SetWindowPos %d,%d %dx%d fl %x (virtual, no messages)\n", x, y, cx, cy, fl); return TRUE; }
static BOOL WINAPI my_GetWindowRect(HWND h, RECT *r) { (void)h; SetRect(r, 0, 0, SCR_W, SCR_H); return TRUE; }
static BOOL WINAPI my_GetClientRect(HWND h, RECT *r) { (void)h; SetRect(r, 0, 0, SCR_W, SCR_H); return TRUE; }
static BOOL WINAPI my_ClientToScreen(HWND h, POINT *p) { (void)h; (void)p; return TRUE; }
static BOOL WINAPI my_GetCursorPos(POINT *p) { p->x = g_cx; p->y = g_cy; return TRUE; }
static BOOL WINAPI my_AdjustWindowRectEx(RECT *r, DWORD st, BOOL menu, DWORD ex) { (void)r; (void)st; (void)menu; (void)ex; return TRUE; }
static int WINAPI my_GetSystemMetrics(int i)
{
    switch (i) {
    case SM_CXSCREEN: case SM_CXFULLSCREEN: return SCR_W;
    case SM_CYSCREEN: case SM_CYFULLSCREEN: return SCR_H;
    case SM_CXCURSOR: case SM_CYCURSOR: case SM_CXICON: case SM_CYICON: return 32;
    case SM_MOUSEPRESENT: return 1;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    default: rh_log("win: GetSystemMetrics(%d) = 0 (virtual)\n", i); return 0;
    }
}
static BOOL WINAPI my_SystemParametersInfoA(UINT a, UINT u, PVOID p, UINT w)
{
    (void)u; (void)w;
    if (a == SPI_GETWORKAREA && p) { SetRect((RECT *)p, 0, 0, SCR_W, SCR_H); return TRUE; }
    rh_log("win: SystemParametersInfoA(%u) unsupported\n", a);
    return FALSE;
}
static HCURSOR WINAPI my_LoadCursorA(HINSTANCE i, LPCSTR n) { (void)i; (void)n; return (HCURSOR)(uintptr_t)(0x00c10000u + 4u * ++g_nfake); }
static HICON WINAPI my_LoadIconA(HINSTANCE i, LPCSTR n) { (void)i; (void)n; return (HICON)(uintptr_t)(0x00c10000u + 4u * ++g_nfake); }
static HCURSOR WINAPI my_SetCursor(HCURSOR c) { uint32_t was = g_cursor_handle; g_cursor_handle = (uint32_t)(uintptr_t)c; return (HCURSOR)(uintptr_t)was; }
static BOOL WINAPI my_WinHelpA(HWND h, LPCSTR f, UINT c, ULONG_PTR d) { (void)h; (void)f; (void)c; (void)d; return TRUE; }
static int WINAPI my_MessageBoxA(HWND h, LPCSTR text, LPCSTR cap, UINT t)
{ (void)h; (void)t; rh_log("MessageBoxA [%s] %s (frame %u)\n", cap ? cap : "", text ? text : "", g_frame); return IDOK; }
static INT_PTR WINAPI my_DialogBoxParamA(HINSTANCE i, LPCSTR t, HWND p, DLGPROC fn, LPARAM l)
{ (void)i; (void)p; (void)l; rh_log("DialogBoxParamA(template %p, proc %p): not shown, -1 (frame %u)\n", (void *)t, (void *)fn, g_frame); return -1; }
static HWND WINAPI my_GetDlgItem(HWND d, int id) { (void)d; (void)id; return NULL; }
static LRESULT WINAPI my_SendDlgItemMessageA(HWND d, int id, UINT m, WPARAM w, LPARAM l) { (void)d; (void)id; (void)m; (void)w; (void)l; return 0; }
static BOOL WINAPI my_EndDialog(HWND d, INT_PTR r) { (void)d; (void)r; return TRUE; }
static UINT_PTR WINAPI my_SetTimer(HWND h, UINT_PTR id, UINT ms, TIMERPROC fn) { (void)h; (void)fn; rh_log("win: SetTimer %u ms (virtual, never fires)\n", ms); return id ? id : 1; }
static BOOL WINAPI my_KillTimer(HWND h, UINT_PTR id) { (void)h; (void)id; return TRUE; }
/* GDI on the fake screen DC */
static HDC WINAPI my_GetDC(HWND h) { (void)h; return FAKE_HDC; }
static int WINAPI my_ReleaseDC(HWND h, HDC dc) { (void)h; (void)dc; return 1; }
static int WINAPI my_GetDeviceCaps(HDC dc, int i)
{
    (void)dc;
    switch (i) {
    case BITSPIXEL: return 32;          /* not 8 bpp: retail takes its fullscreen path (every GOG user's) */
    case PLANES: return 1;
    case HORZRES: return SCR_W;
    case VERTRES: return SCR_H;
    case NUMCOLORS: return -1;
    case RASTERCAPS: return RC_BITBLT | RC_STRETCHBLT | RC_DI_BITMAP | RC_DIBTODEV;
    default: rh_log("win: GetDeviceCaps(%d) = 0 (virtual)\n", i); return 0;
    }
}
static UINT WINAPI my_GetPaletteEntries(HPALETTE p, UINT s, UINT n, LPPALETTEENTRY e) { (void)p; (void)s; (void)n; (void)e; return 0; }
static UINT WINAPI my_SetSystemPaletteUse(HDC dc, UINT u) { (void)dc; (void)u; return SYSPAL_STATIC; }
static HGDIOBJ WINAPI my_GetStockObject(int i) { return (HGDIOBJ)(uintptr_t)(0x0bb00000u + (uint32_t)i); }
static BOOL WINAPI my_PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop) { (void)dc; (void)x; (void)y; (void)w; (void)h; (void)rop; return TRUE; }

/* ================================================================ the real window (rhost's) */
#define REC_MAX 65536
static struct { uint32_t f; UINT m; WPARAM w; LPARAM l; } *g_rec; static uint32_t g_nrec;
static FILE *g_recf;
static int g_rec_live;                 /* record: real input accepted (after any --trace prefix) */
static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CLOSE:
        if (O.record || O.show) rh_finish("window-closed");
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (O.show || O.record) rh_win_present(); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE; } break;
    default: break;
    }
    int key = (m >= WM_KEYFIRST && m <= WM_KEYLAST);
    int mouse = (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL);
    if (key || mouse) {
        if (O.record && g_rec_live && g_nrec < REC_MAX) {
            LPARAM gl = l;
            if (mouse) {
                int x = (short)LOWORD(l) / O.scale, y = (short)HIWORD(l) / O.scale;
                if (x < 0) x = 0; if (y < 0) y = 0; if (x >= SCR_W) x = SCR_W - 1; if (y >= SCR_H) y = SCR_H - 1;
                gl = MAKELPARAM(x, y);
                if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN) SetCapture(h);
                if ((m == WM_LBUTTONUP || m == WM_RBUTTONUP) && !(w & (MK_LBUTTON | MK_RBUTTON))) ReleaseCapture();
            }
            /* coalesce mouse moves within one frame (the last position wins) */
            if (m == WM_MOUSEMOVE && g_nrec && g_rec[g_nrec - 1].m == WM_MOUSEMOVE && g_rec[g_nrec - 1].f == g_frame + 1)
                { g_rec[g_nrec - 1].w = w; g_rec[g_nrec - 1].l = gl; }
            else { g_rec[g_nrec].f = g_frame + 1; g_rec[g_nrec].m = m; g_rec[g_nrec].w = w; g_rec[g_nrec].l = gl; g_nrec++; }
        }
        if (key && (m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_SYSCHAR)) return 0;   /* no real menu mode */
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}
static void real_window(void)
{
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = host_wndproc; wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "OL2RhostWindow";
    RegisterClassA(&wc);
    if (O.show || O.record) {
        RECT r = { 0, 0, SCR_W * O.scale, SCR_H * O.scale };
        DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&r, st, FALSE);
        g_real = CreateWindowExA(0, wc.lpszClassName, O.record ? "Lords of the Realm II — rhost (recording)" : "Lords of the Realm II — rhost",
                                 st | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    } else {
        g_real = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, "ol2-rhost", WS_POPUP,
                                 -8000, -8000, SCR_W, SCR_H, NULL, NULL, wc.hInstance, NULL);
    }
    if (!g_real) rh_fatal("cannot create rhost's window (err %lu)", GetLastError());
    if (O.show || O.record) {   /* the launcher's STARTUPINFO (Start-Process -WindowStyle Hidden) eats the first ShowWindow */
        ShowWindow(g_real, SW_SHOWNORMAL);
        ShowWindow(g_real, SW_SHOWNORMAL);
        UpdateWindow(g_real);
        RECT r; GetClientRect(g_real, &r);
        rh_log("win: rhost window visible=%d client %ldx%ld\n", IsWindowVisible(g_real), r.right, r.bottom);
    }
}

void rh_win_present(void)
{
    if (!g_real || !(O.show || O.record)) return;
    const uint8_t *pix; int pitch, w, h; const PALETTEENTRY *pal;
    if (rh_dd_present(&pix, &pitch, &w, &h, &pal) != 8) return;
    static struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi;
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = pitch; bi.h.biHeight = -h; bi.h.biPlanes = 1; bi.h.biBitCount = 8; bi.h.biCompression = BI_RGB;
    for (int i = 0; i < 256; i++) { bi.c[i].rgbRed = pal[i].peRed; bi.c[i].rgbGreen = pal[i].peGreen; bi.c[i].rgbBlue = pal[i].peBlue; bi.c[i].rgbReserved = 0; }
    HDC dc = GetDC(g_real);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, w * O.scale, h * O.scale, 0, 0, w, h, pix, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_real, dc);
}

/* ================================================================ the trace */
typedef struct { uint32_t f; UINT m; uint32_t w, l; } ev_t;
static ev_t *g_ev; static uint32_t g_nev, g_iev, g_last;
static const char *num_after(const char *s, const char *key)
{
    const char *p = strstr(s, key);
    return p ? p + strlen(key) : NULL;
}
static void trace_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) rh_fatal("cannot read trace %s", path);
    char line[1024]; uint32_t cap = 1024, ln = 0;
    g_ev = malloc(cap * sizeof *g_ev);
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (*s != '{' || strstr(s, "\"meta\"")) continue;
        const char *pf = num_after(s, "\"f\":"); if (!pf) pf = num_after(s, "\"t\":");
        const char *pm = num_after(s, "\"m\":");
        if (!pf || !pm) { rh_log("trace line %u: not an event (skipped): %s", ln, s); continue; }
        const char *pw = num_after(s, "\"w\":"), *pl = num_after(s, "\"l\":");
        if (g_nev == cap) { cap *= 2; g_ev = realloc(g_ev, cap * sizeof *g_ev); }
        ev_t *e = &g_ev[g_nev++];
        e->f = (uint32_t)strtoul(pf, NULL, 10); e->m = (UINT)strtoul(pm, NULL, 10);
        e->w = pw ? (uint32_t)strtoul(pw, NULL, 10) : 0; e->l = pl ? (uint32_t)strtoul(pl, NULL, 10) : 0;
    }
    fclose(f);
    for (uint32_t i = 1; i < g_nev; i++)   /* stable insertion sort by frame */
        for (uint32_t k = i; k && g_ev[k - 1].f > g_ev[k].f; k--) { ev_t t = g_ev[k]; g_ev[k] = g_ev[k - 1]; g_ev[k - 1] = t; }
    g_last = g_nev ? g_ev[g_nev - 1].f : 0;
    rh_log("trace: %u events, last frame %u (%s)\n", g_nev, g_last, path);
}
uint32_t rh_trace_last_frame(void) { return g_last; }
static void rec_write(uint32_t f, UINT m, uint32_t w, uint32_t l)
{
    if (g_recf) { fprintf(g_recf, "{\"f\": %u, \"m\": %u, \"w\": %u, \"l\": %u}\n", f, m, w, l); fflush(g_recf); }
}
static void inject(uint32_t f, UINT m, uint32_t w, uint32_t l)
{
    q_put((HWND)(uintptr_t)RH_FAKE_HWND, m, (WPARAM)w, (LPARAM)l);
    rec_write(f, m, w, l);
}
/* the control channel's input (rh_bot.c): same path, same record */
void rh_win_inject(UINT m, uint32_t w, uint32_t l) { inject(g_frame, m, w, l); }

/* frame boundary: g_frame was just incremented — post every event of frame g_frame */
void rh_win_frame(void)
{
    static uint32_t pump;
    if (g_real && (O.show || O.record || (++pump & 15) == 0)) {
        MSG m;
        while (PeekMessageA(&m, g_real, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
    }
    while (g_iev < g_nev && g_ev[g_iev].f <= g_frame) {
        ev_t *e = &g_ev[g_iev++];
        inject(g_frame, e->m, e->w, e->l);
    }
    if (O.record) {
        if (!g_rec_live && g_iev >= g_nev) {
            g_rec_live = 1;
            rh_log("record: live from frame %u (prefix of %u events replayed)\n", g_frame, g_nev);
        }
        for (uint32_t i = 0; i < g_nrec; i++) inject(g_frame, g_rec[i].m, (uint32_t)g_rec[i].w, (uint32_t)g_rec[i].l);
        g_nrec = 0;
    }
    if (O.show || O.record) rh_win_present();
}

const char *rh_trace_status(void)
{
    static char b[96];
    snprintf(b, sizeof b, "trace_events=%u/%u complete=%d posted=%u dispatched=%u", g_iev, g_nev, g_iev >= g_nev, g_nposted, g_ndispatched);
    return b;
}
void rh_trace_finish(void)
{
    if (!g_recf) return;
    fprintf(g_recf, "{\"meta\": {\"frames\": %u}}\n", g_frame);   /* replay runs to where this run ended */
    fclose(g_recf); g_recf = NULL;
    rh_log("trace written to %s\n", O.out_trace);
}

void rh_win_install(void)
{
    iat_override("PeekMessageA", my_PeekMessageA);
    iat_override("GetMessageA", my_GetMessageA);
    iat_override("TranslateMessage", my_TranslateMessage);
    iat_override("DispatchMessageA", my_DispatchMessageA);
    iat_override("PostMessageA", my_PostMessageA);
    iat_override("SendMessageA", my_SendMessageA);
    iat_override("PostQuitMessage", my_PostQuitMessage);
    iat_override("DefWindowProcA", my_DefWindowProcA);
    iat_override("RegisterClassA", my_RegisterClassA);
    iat_override("CreateWindowExA", my_CreateWindowExA);
    iat_override("ShowWindow", my_ShowWindow);
    iat_override("UpdateWindow", my_UpdateWindow);
    iat_override("InvalidateRect", my_InvalidateRect);
    iat_override("BeginPaint", my_BeginPaint);
    iat_override("EndPaint", my_EndPaint);
    iat_override("SetFocus", my_SetFocus);
    iat_override("GetForegroundWindow", my_GetForegroundWindow);
    iat_override("SetForegroundWindow", my_SetForegroundWindow);
    iat_override("IsIconic", my_IsIconic);
    iat_override("GetMenu", my_GetMenu);
    iat_override("GetWindowLongA", my_GetWindowLongA);
    iat_override("SetWindowLongA", my_SetWindowLongA);
    iat_override("SetWindowPos", my_SetWindowPos);
    iat_override("GetWindowRect", my_GetWindowRect);
    iat_override("GetClientRect", my_GetClientRect);
    iat_override("ClientToScreen", my_ClientToScreen);
    iat_override("GetCursorPos", my_GetCursorPos);
    iat_override("AdjustWindowRectEx", my_AdjustWindowRectEx);
    iat_override("GetSystemMetrics", my_GetSystemMetrics);
    iat_override("SystemParametersInfoA", my_SystemParametersInfoA);
    iat_override("LoadCursorA", my_LoadCursorA);
    iat_override("LoadIconA", my_LoadIconA);
    iat_override("SetCursor", my_SetCursor);
    iat_override("WinHelpA", my_WinHelpA);
    iat_override("MessageBoxA", my_MessageBoxA);
    iat_override("DialogBoxParamA", my_DialogBoxParamA);
    iat_override("GetDlgItem", my_GetDlgItem);
    iat_override("SendDlgItemMessageA", my_SendDlgItemMessageA);
    iat_override("EndDialog", my_EndDialog);
    iat_override("SetTimer", my_SetTimer);
    iat_override("KillTimer", my_KillTimer);
    iat_override("GetDC", my_GetDC);
    iat_override("ReleaseDC", my_ReleaseDC);
    iat_override("GetDeviceCaps", my_GetDeviceCaps);
    iat_override("GetPaletteEntries", my_GetPaletteEntries);
    iat_override("SetSystemPaletteUse", my_SetSystemPaletteUse);
    iat_override("GetStockObject", my_GetStockObject);
    iat_override("PatBlt", my_PatBlt);
    real_window();
    if (O.trace) trace_load(O.trace);
    if (O.record) {
        g_rec = calloc(REC_MAX, sizeof *g_rec);
        g_rec_live = g_nev == 0;
    }
    if (O.out_trace) {   /* every injected event (trace prefix, human, bot): the replayable record of this run */
        g_recf = fopen(O.out_trace, "w");
        if (!g_recf) rh_fatal("cannot write %s", O.out_trace);
        fprintf(g_recf, "{\"meta\": {\"game\": \"lords2\", \"step_ms\": %u, \"vbase\": %u, \"source\": \"%s\"}}\n",
                O.step_ms, O.vbase, O.record ? "human" : O.bot ? "bot" : "replay");
    }
    /* frame-0 events: queued before retail starts (dispatched by its first pump) */
    while (g_iev < g_nev && g_ev[g_iev].f == 0) { ev_t *e = &g_ev[g_iev++]; inject(0, e->m, e->w, e->l); }
}
