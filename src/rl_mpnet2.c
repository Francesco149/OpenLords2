/* rl_mpnet2.c — a retail-layout port, partition 'mpnet2' (tools/bf/parts.py; docs/findings/rl-mpnet2.md). Retail
 * semantics on retail's layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The SIGS session's teardown (its __thiscall methods take the session as `self`, a retail pointer, as rl_mpnet.c),
 * leaving MP, the SierraNW callbacks (lobby chat, a player left), the DirectPlay dialogs and their enumeration
 * callbacks (__stdcall), the MP save pump, the type-20 popup, MP title / menu helpers. Win32 goes through retail's
 * IAT slots under rhost (IMP, rl_io_plat.h's rule); COM through the object's vtable. */
#include "rl.h"
#include "rl_mpnet2_globals.h"
#include "rl_io_plat.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include "engine.h"               /* g_sim_mp_active, engine_set_current_screen / _demand_redraw */
#include "engine_combat.h"        /* g_combat_audio_latch (DAT_005533f0) */
#include "engine_diplomacy_action.h"   /* g_diplo_action_mode (DAT_005651cc) */
#include "engine_army_state.h"
#include "engine_territory.h"
#include "engine_dirty_rect.h"    /* g_army_state_index (DAT_005533e8 = DAT_005533e0[2]) */
#include "save.h"
#include "save_schema_globals.h"
#include "engine_bf_input_globals.h"   /* g_bf_59154c, the default font */
#include "rl_render_globals.h"         /* the fonts, sheets, the localization blob */
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "localization.h"

#include <stdio.h>
#include <string.h>

/* other partitions' globals (their headers are partition-private: the declarations, as rl_mpnet.c does) */
extern uint8_t g_rl_4e2a28[0x8];      /* DAT_004e2a28 our DPID, DAT_004e2a2c the IDirectPlay2A (rl_io_globals.h) */
extern int32_t g_rl_4e2a30;           /* DAT_004e2a30: the SIGS session (rl_io_globals.h) */
extern int32_t g_rl_4e5a90[5];        /* DAT_004e5a90: the players' left / joined flags (rl_io_globals.h) */
extern uint8_t g_rl_4e5a00[4];        /* hWndMain (rl_misc_globals.h) */
extern int32_t g_rl_4e5a08;           /* DAT_004e5a08: the monitor thread's session (rl_mpnet_globals.h) */
extern int32_t g_rl_5530f0;           /* DAT_005530f0: the title page (rl_io_globals.h) */
extern uint8_t g_rl_4e5a18[1];        /* unk_4E5A18: the session name buffer (rl_ui2_globals.h) */
extern int32_t g_rl_4e5b08;           /* DAT_004e5b08: -> the application GUID (rl_ui2_globals.h) */
extern uint8_t g_rl_4e2a38[0x28];     /* "Lords2 Session1" (rl_ui2_globals.h) */
extern int32_t g_rl_543fcc;           /* DAT_00543fcc (rl_mpmsg2_globals.h) */
extern int32_t g_rl_5440bc;           /* DAT_005440bc: the chosen .skr's checksum (rl_skirmish_globals.h) */
extern int32_t g_rl_5691e4[3];        /* DAT_005691e4: the peer's .skr checksum (rl_mpmsg3_globals.h) */
extern int32_t g_rl_55cd34;           /* DAT_0055cd34 (rl_skirmish_globals.h) */
extern uint8_t g_rl_53f5fc[0x44];     /* byte_53F1E0 + 0x41c: the .skr file chosen (rl_skirmish_globals.h) */
extern int32_t g_rl_56899c;           /* DAT_0056899c: the skirmish page state (rl_skirmish_globals.h) */
extern uint8_t g_rl_5533e0[8];        /* DAT_005533e0[p]: a side's ready flag (rl_skirmish_globals.h) */
extern int32_t g_rl_57c948, g_rl_55ce94;   /* rl_skirmish_globals.h */
extern int32_t g_rl_57c964, g_rl_56894c;   /* rl_mpnet_globals.h */
extern int32_t g_rl_553ee0, g_rl_53e910, g_rl_57c954;   /* the event popup (rl_terr_globals.h) */
extern int32_t g_rl_553060, g_rl_553064, g_rl_553024, g_rl_552ff8;   /* the popup's box (rl_turn_globals.h) */
extern uint8_t g_rl_4d6f30[0x88];     /* DAT_004d6f30: the popups' rows / heights (rl_mpnet_globals.h) */
extern uint8_t g_rl_4eeb80[5912];     /* DAT_004eeb80: the 80 × 76 portrait (rl_glue_globals.h) */

#define HWNDM   ((HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u))
#define SELF_ID AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u)    /* DAT_004e2a28 */
#define DPLAY   AT(uint32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a2cu)   /* DAT_004e2a2c */
#define NW      g_rl_4e2a30
#define CSTR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define S32(self, off) (*(int32_t *)(uintptr_t)((uint32_t)(self) + (off)))   /* a session field */
#define VT(obj, off)   (*(uint32_t *)(uintptr_t)(*(uint32_t *)(uintptr_t)(obj) + (off)))
#define NWF0(self, off) ((int (__cdecl *)(void))(uintptr_t)(uint32_t)S32(self, off))
#define STR28(va) CSTR(g_rl_4e2890, 0x4e2890u, va)   /* the session's debug strings 0x4e2890.. */
#define STR2C(va) CSTR(g_rl_4e2ccc, 0x4e2cccu, va)   /* the session dialog's log lines 0x4e2ccc.. */
#define LOGS(p)   rl_4afab9((int)(uintptr_t)(p), 0, 0)

/* ---------------------------------------------------------------- Win32 through retail's IAT slots (rhost) */
#define IMP(T, slot, host) (rl_plat_bound() ? (T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot) : (T)(host))
static void w_OutputDebugStringA(const char *s) { IMP(void (WINAPI *)(LPCSTR), 0x5cf3b4u, OutputDebugStringA)(s); }
static BOOL w_FreeLibrary(HMODULE m) { return IMP(BOOL (WINAPI *)(HMODULE), 0x5cf394u, FreeLibrary)(m); }
static DWORD w_WaitForSingleObject(HANDLE h, DWORD ms)
{ return IMP(DWORD (WINAPI *)(HANDLE, DWORD), 0x5cf398u, WaitForSingleObject)(h, ms); }
static BOOL w_CloseHandle(HANDLE h) { return IMP(BOOL (WINAPI *)(HANDLE), 0x5cf3a0u, CloseHandle)(h); }
static BOOL w_GetExitCodeThread(HANDLE h, DWORD *c)
{ return IMP(BOOL (WINAPI *)(HANDLE, LPDWORD), 0x5cf3f0u, GetExitCodeThread)(h, c); }
static BOOL w_TerminateThread(HANDLE h, DWORD c) { return IMP(BOOL (WINAPI *)(HANDLE, DWORD), 0x5cf3a4u, TerminateThread)(h, c); }
static DWORD w_GetLastError(void) { return IMP(DWORD (WINAPI *)(void), 0x5cf3e8u, GetLastError)(); }
static DWORD w_FormatMessageA(DWORD fl, const void *src, DWORD id, DWORD lang, char *buf, DWORD n, va_list *args)
{ return IMP(DWORD (WINAPI *)(DWORD, LPCVOID, DWORD, DWORD, LPSTR, DWORD, va_list *), 0x5cf3b0u, FormatMessageA)(
      fl, src, id, lang, buf, n, args); }
static HLOCAL w_LocalFree(HLOCAL h) { return IMP(HLOCAL (WINAPI *)(HLOCAL), 0x5cf3f4u, LocalFree)(h); }
static HWND w_SetFocus(HWND w) { return IMP(HWND (WINAPI *)(HWND), 0x5cf568u, SetFocus)(w); }
static HWND w_GetDlgItem(HWND d, int id) { return IMP(HWND (WINAPI *)(HWND, int), 0x5cf520u, GetDlgItem)(d, id); }
static LRESULT w_SendMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{ return IMP(LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM), 0x5cf530u, SendMessageA)(w, m, wp, lp); }
static LRESULT w_SendDlgItemMessageA(HWND d, int id, UINT m, WPARAM wp, LPARAM lp)
{ return IMP(LRESULT (WINAPI *)(HWND, int, UINT, WPARAM, LPARAM), 0x5cf528u, SendDlgItemMessageA)(d, id, m, wp, lp); }
static BOOL w_EndDialog(HWND d, INT_PTR r) { return IMP(BOOL (WINAPI *)(HWND, INT_PTR), 0x5cf534u, EndDialog)(d, r); }
static UINT_PTR w_SetTimer(HWND w, UINT_PTR id, UINT ms, TIMERPROC fn)
{ return IMP(UINT_PTR (WINAPI *)(HWND, UINT_PTR, UINT, TIMERPROC), 0x5cf50cu, SetTimer)(w, id, ms, fn); }
static BOOL w_KillTimer(HWND w, UINT_PTR id) { return IMP(BOOL (WINAPI *)(HWND, UINT_PTR), 0x5cf524u, KillTimer)(w, id); }
static HRESULT w_DirectPlayCreate(uint32_t guid, uint32_t *dp, void *outer)
{   /* dplayx is not linked in the standalone: no DirectPlay there */
    if (!rl_plat_bound()) return (HRESULT)0x80004005;
    return ((HRESULT (WINAPI *)(uint32_t, uint32_t *, void *))(uintptr_t)*(volatile uint32_t *)(uintptr_t)0x5cf364u)(guid, dp, outer);
}
/* a COM method by vtable offset (stdcall, `this` first) */
typedef HRESULT (WINAPI *com1_t)(uint32_t);
#define COM1(obj, off) ((com1_t)(uintptr_t)VT(obj, off))(obj)

/* retail functions outside the partition (bare VAs; the ledger counts FUN_ tokens) */
#define VA_OPERATOR_DELETE 0x4c29d0u   /* ??3@YAXPAX@Z */

/* ================================================================ the ASIGSSession teardown */

/* Port of FUN_004b6ef7 (320 B) — wait for the monitor thread (*h, 8 s) and log how it went ("SIGSEndThread thread
 * terminated AOK" / "failed to terminate" (timeout) / "ABANDONED" / "FAILED Wait" + the system message); its exit code:
 * 0 → "Free Thread Resource", nonzero → "Forced thread termination" + TerminateThread(h — the HANDLE*'s value, a retail
 * bug) ; CloseHandle(*h) when GetExitCodeThread succeeded; *h = 0. __thiscall (ECX unused), returns 1. */
int rl_4b6ef7(uint32_t self, int ph)
{
    (void)self;
    HANDLE *h = (HANDLE *)(uintptr_t)(uint32_t)ph;
    DWORD code = w_WaitForSingleObject(*h, 8000);
    if (code == 0) {
        w_OutputDebugStringA(STR28(0x4e28bcu));
    } else if (code == WAIT_TIMEOUT) {
        w_OutputDebugStringA(STR28(0x4e28e4u));
    } else if (code == WAIT_ABANDONED) {
        w_OutputDebugStringA(STR28(0x4e2910u));
    } else if (code == WAIT_FAILED) {
        w_OutputDebugStringA(STR28(0x4e2930u));
        char *buf = NULL;
        w_FormatMessageA(0x1100, NULL, w_GetLastError(), 0x400, (char *)&buf, 0, NULL);
        w_OutputDebugStringA(buf);
        w_LocalFree((HLOCAL)buf);
    }
    if (w_GetExitCodeThread(*h, &code)) {
        if (code == 0) {
            w_OutputDebugStringA(STR28(0x4e2954u));
        } else {
            w_OutputDebugStringA(STR28(0x4e2978u));
            w_TerminateThread((HANDLE)h, 0xfffffffeu);
        }
        w_CloseHandle(*h);
    }
    *h = NULL;
    return 1;
}

/* Port of FUN_004b6a15 (161 B) — the session's Close: "ASIGSSession::Close"; +4 (connected) and +0 cleared; a monitor
 * thread (+16) → +8 = 1 (its stop flag) and the wait 0x4b6ef7(&+16); the SierraNW module loaded (+36) with CloseDLL
 * (+48) → CloseDLL(), "Unloading SIGS DLL", FreeLibrary, +36 = 0. Returns 1. */
int rl_4b6a15(uint32_t self)
{
    w_OutputDebugStringA(STR28(0x4e2890u));
    S32(self, 4) = 0;
    S32(self, 0) = 0;
    if (S32(self, 16) != 0) {
        S32(self, 8) = 1;
        rl_4b6ef7(self, (int)(self + 16));
    }
    if (S32(self, 36) != 0 && S32(self, 48) != 0) {
        NWF0(self, 48)();
        w_OutputDebugStringA(STR28(0x4e28a8u));
        w_FreeLibrary((HMODULE)(uintptr_t)(uint32_t)S32(self, 36));
        S32(self, 36) = 0;
    }
    return 1;
}

/* Port of FUN_004b64a3 (53 B) — the session's destructor body: Close (0x4b6a15) while connected (+4); the monitor's
 * session DAT_004e5a08 = 0. EAX: Close's 1, else the session. */
int rl_4b64a3(uint32_t self)
{
    int r = (int)self;
    if (S32(self, 4) != 0)
        r = rl_4b6a15(self);
    g_rl_4e5a08 = 0;
    return r;
}

/* Port of FUN_004b92a0 (57 B) — the session's scalar deleting destructor: 0x4b64a3; flags & 1 → operator delete.
 * __thiscall, returns the session. */
int rl_4b92a0(uint32_t self, int flags)
{
    rl_4b64a3(self);
    if (flags & 1)
        rl_crt_4c29d0(self);   /* operator delete (rl_crt.c) */
    return (int)self;
}

/* Port of FUN_004b6ab6 (47 B) — the session's game close: SierraNW +68 (GameClose); +0 and +12 cleared. Returns the
 * session. */
int rl_4b6ab6(uint32_t self)
{
    NWF0(self, 68)();
    S32(self, 0) = 0;
    S32(self, 12) = 0;
    return (int)self;
}

/* ================================================================ leaving MP, the SierraNW callbacks */

/* Port of FUN_004b743b (147 B) — leave the MP game: 0x43f43e; DirectPlay → DestroyPlayer(our DPID) (+36), the release
 * 0x4b7180(1), DAT_00553030 = 0; else SIGS → 0x42df30, screen 0x1f (the title), title page 1, a full redraw, the game
 * close 0x4b6ab6, DAT_00553030 = 0. EAX: the last call's (no reader). */
int rl_4b743b(void)
{
    int r = rl_43f43e();
    if (DPLAY != 0) {
        ((HRESULT (WINAPI *)(uint32_t, int32_t))(uintptr_t)VT(DPLAY, 36))(DPLAY, SELF_ID);
        r = rl_4b7180(1);
        g_sim_mp_active = 0;
    } else if (NW != 0) {
        rl_42df30();
        engine_set_current_screen(0x1f);
        g_rl_5530f0 = 1;
        engine_set_demand_redraw(2);
        r = rl_4b6ab6((uint32_t)NW);
        g_sim_mp_active = 0;
    }
    return r;
}

/* Port of FUN_004b756b (26 B) — 0x43f01d; returns 1. */
int rl_4b756b(void)
{
    rl_43f01d();
    return 1;
}

/* Port of FUN_004b79e3 (153 B) — TellMeSpecialChatReceived's callback (a lobby chat line): outside the battlefield
 * (DAT_0057c8fc != 2) → DAT_0056d5cc = DAT_00543fcc = 0, "%s: %s" with &arg_4 (SierraNW passes the sender's name BY
 * VALUE after the text: `name` is its address) and the text, cut at 200, into the MP message slot of realm 0 (save
 * 149); the type-20 popup: event 246 for our player + 1 (0x472bc5(0, me + 1, 246, 0, 20, 0, 0, 0)). EAX: no reader. */
int rl_4b79e3(int text, int name)
{
    if ((int32_t)s_slot_0057c8fc == 2)
        return 0;
    s_slot_0056d5cc = 0;
    g_rl_543fcc = 0;
    char src[512];
    snprintf(src, sizeof src, (const char *)g_rl_4e2c18, (const char *)(uintptr_t)(uint32_t)name,
             (const char *)(uintptr_t)(uint32_t)text);   /* retail's sprintf 0x4c2a80, "%s: %s" */
    src[200] = 0;
    strcpy((char *)s_slot_00567d10, src);
    int me = rl_4b6e1b((uint32_t)NW);
    return rl_472bc5(0, me + 1, 246, 0, 20, 0, 0, 0);
}

/* Port of FUN_004b7a7c (30 B) — TellMePlayerLeftGame's callback: DAT_004e5a90[player] = 1 (the pump 0x4b8c3a turns it
 * into 0x43e6a2(2, player + 1)). Returns the player. */
int rl_4b7a7c(int player)
{
    g_rl_4e5a90[player] = 1;
    return player;
}

/* ================================================================ the DirectPlay dialogs and callbacks */

/* Port of FUN_004b8243 (124 B) — the provider's connection: DirectPlayCreate(guid, &dp, 0) → QueryInterface(IID
 * 0x4d0058 = IID_IDirectPlay2A, &DAT_004e2a2c), Release. Nothing for a null guid. EAX: no reader. */
int rl_4b8243(int guid)
{
    uint32_t dp = 0;
    if (guid == 0)
        return 0;
    HRESULT hr = w_DirectPlayCreate((uint32_t)guid, &dp, NULL);
    if (hr == 0 && dp != 0) {
        ((HRESULT (WINAPI *)(uint32_t, const void *, uint32_t *))(uintptr_t)VT(dp, 0))(dp, g_rl_4d0058, &DPLAY);
        return COM1(dp, 8);
    }
    return hr;
}

/* Port of FUN_004b82bf (118 B) — DirectPlayEnumerateA's callback (guid, name, major, minor, list box): the provider's
 * name added (LB_ADDSTRING), its row's item data = the guid pointer; focus; LB_SETCURSEL 0.
 * __stdcall, returns 1 (go on). */
int rl_4b82bf(int guid, int name, int major, int minor, int lbox)
{
    (void)major; (void)minor;
    HWND w = (HWND)(uintptr_t)(uint32_t)lbox;
    LRESULT row = w_SendMessageA(w, LB_ADDSTRING, 0, (LPARAM)name);
    if (row != -1)
        w_SendMessageA(w, LB_SETITEMDATA, (WPARAM)row, (LPARAM)guid);
    w_SetFocus(w);
    w_SendMessageA(w, LB_SETCURSEL, 0, 0);
    return 1;
}

/* Port of FUN_004b8335 (260 B) — the host / join dialog's DLGPROC (template 0x6c): WM_MOVE → 0x451f70, FALSE;
 * WM_INITDIALOG → 0x451f70, TRUE; WM_COMMAND (the whole wParam): 1004 (Host) → EndDialog 1004; 1002 (Join) or IDOK →
 * EndDialog 1002; IDCANCEL → EndDialog 2; TRUE after an EndDialog, else FALSE. */
int rl_4b8335(int hdlg, unsigned msg, unsigned wp, int lp)
{
    (void)lp;
    HWND dlg = (HWND)(uintptr_t)(uint32_t)hdlg;
    if (msg == WM_MOVE) {
        rl_451f70();
        return 0;
    }
    if (msg == WM_INITDIALOG) {
        rl_451f70();
        return 1;
    }
    if (msg != WM_COMMAND)
        return 0;
    if (wp == 1004) {
        w_EndDialog(dlg, 1004);
        return 1;
    }
    if (wp == 1002 || wp == 1) {
        w_EndDialog(dlg, 1002);
        return 1;
    }
    if (wp == 2) {
        w_EndDialog(dlg, 2);
        return 1;
    }
    return 0;
}

/* Port of FUN_004b8439 (382 B) — the session name dialog's DLGPROC (template 0x74): WM_MOVE → 0x451f70, FALSE.
 * WM_INITDIALOG: the edit 1000 into DAT_004e5b00 (none: EndDialog 1, TRUE), its text "Lords2 Session1" (WM_SETTEXT),
 * focus, 0x451f70, FALSE. WM_COMMAND (the whole wParam): IDOK → its length (EM_LINELENGTH); 1..99 → the byte
 * 0x4e5a19 = the length (the high byte of EM_GETLINE's capacity WORD at 0x4e5a18), EM_GETLINE into 0x4e5a18,
 * EndDialog 1, 0x451f70, TRUE; else FALSE. IDCANCEL → EndDialog -1, 0x451f70, TRUE. */
int rl_4b8439(int hdlg, unsigned msg, unsigned wp, int lp)
{
    (void)lp;
    HWND dlg = (HWND)(uintptr_t)(uint32_t)hdlg;
    if (msg == WM_MOVE) {
        rl_451f70();
        return 0;
    }
    if (msg == WM_INITDIALOG) {
        g_rl_4e5b00 = (int32_t)(uintptr_t)w_GetDlgItem(dlg, 1000);
        if (g_rl_4e5b00 == 0) {
            w_EndDialog(dlg, 1);
            return 1;
        }
        w_SendDlgItemMessageA(dlg, 1000, WM_SETTEXT, 0, (LPARAM)CSTR(g_rl_4e2a38, 0x4e2a38u, 0x4e2a38u));
        w_SetFocus((HWND)(uintptr_t)(uint32_t)g_rl_4e5b00);
        rl_451f70();
        return 0;
    }
    if (msg != WM_COMMAND)
        return 0;
    if (wp == 1) {
        LRESULT n = w_SendDlgItemMessageA(dlg, 1000, EM_LINELENGTH, 0, 0);
        if (n <= 0 || n >= 100)
            return 0;
        g_rl_4e5a19 = (uint8_t)n;
        w_SendDlgItemMessageA(dlg, 1000, EM_GETLINE, 0, (LPARAM)g_rl_4e5a18);
        w_EndDialog(dlg, 1);
        rl_451f70();
        return 1;
    }
    if (wp == 2) {
        w_EndDialog(dlg, -1);
        rl_451f70();
        return 1;
    }
    return 0;
}

/* Port of FUN_004b8b87 (179 B) — EnumSessions' callback (session desc, timeout, flags, list box): DPESC_TIMEDOUT
 * (flags & 1) → 0 (stop). Else the session's GUID (desc +8) into 0x4e5a80, its name (desc +48) added (LB_ADDSTRING);
 * a row → DAT_004e2a34 = 1 (one found) and its item data 0; focus; LB_SETCURSEL 0. __stdcall, returns 1 (go on). */
int rl_4b8b87(int sd, int timeout, int flags, int lbox)
{
    (void)timeout;
    if (flags & 1)
        return 0;
    HWND w = (HWND)(uintptr_t)(uint32_t)lbox;
    const uint32_t *d = (const uint32_t *)(uintptr_t)(uint32_t)sd;
    uint32_t *g = (uint32_t *)g_rl_4e5a80;
    g[0] = d[2];
    g[1] = d[3];
    g[2] = d[4];
    g[3] = d[5];
    LRESULT row = w_SendMessageA(w, LB_ADDSTRING, 0, (LPARAM)d[12]);
    if (row != -1) {
        g_rl_4e2a34 = 1;
        w_SendMessageA(w, LB_SETITEMDATA, (WPARAM)row, 0);
    }
    w_SetFocus(w);
    w_SendMessageA(w, LB_SETCURSEL, 0, 0);
    return 1;
}

/* a DPSESSIONDESC (80 B) zeroed with dwSize 80 and the application GUID (*DAT_004e5b08) at +24 */
static void sd_init(uint32_t sd[20])
{
    memset(sd, 0, 80);
    sd[0] = 80;
    const uint32_t *app = (const uint32_t *)(uintptr_t)(uint32_t)g_rl_4e5b08;
    sd[6] = app[0];
    sd[7] = app[1];
    sd[8] = app[2];
    sd[9] = app[3];
}
/* EnumSessions(sd, 0, 0x4b8b87, the list box DAT_004e5aa8, DPENUMSESSIONS_AVAILABLE) (+52), focus on the list */
static void enum_sessions(void)
{
    uint32_t sd[20];
    sd_init(sd);
    sd[1] = 0;
    ((HRESULT (WINAPI *)(uint32_t, void *, uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)VT(DPLAY, 52))(
        DPLAY, sd, 0, 0x4b8b87u, (uint32_t)g_rl_4e5aa8, 1);
    w_SetFocus((HWND)(uintptr_t)(uint32_t)g_rl_4e5aa8);
}
/* Close (+16), Release (+8), DAT_004e2a2c = 0 */
static void dp_drop(void)
{
    COM1(DPLAY, 16);
    COM1(DPLAY, 8);
    DPLAY = 0;
}

/* Port of FUN_004b85b7 (1488 B) — the session list dialog's DLGPROC (template 0x82). Its timer id is a local set to 0
 * at every entry (KillTimer(dlg, 0)). WM_MOVE → 0x451f70, FALSE. WM_INITDIALOG: the list 1024 into DAT_004e5aa8 (none:
 * EndDialog 1, TRUE), EnumSessions (callback 0x4b8b87), 0x451f70; none found yet (DAT_004e2a34 != 1) → SetTimer(dlg, 0,
 * 3000) + "OK:DP Got timer= " (id + 1); FALSE. WM_TIMER: one found → KillTimer + "OK:DP Killed timer " (its result +
 * 1); else EnumSessions again; FALSE. WM_COMMAND by HIWORD: LBN_SELCHANGE (1) → DAT_004e5b04 = the selection,
 * DAT_004e5aa8 = the control, FALSE; 0 with IDCANCEL → Close / Release / DAT_004e2a2c = 0, KillTimer + log, EndDialog
 * 0, 0x451f70, TRUE; 0 (any other id) or LBN_DBLCLK (2, the control and its selection first) with a selection →
 * Open(sd: the chosen GUID 0x4e5a80 at +8, the app GUID at +24, DPOPEN_JOIN) (+96), the result logged ("OK:DP open
 * session - success" / one of the ERR lines); a failure → Close / Release / 0, EndDialog 0; then 0x451f70, KillTimer +
 * log, EndDialog 1, TRUE. Other HIWORDs / no selection: FALSE. */
int rl_4b85b7(int hdlg, unsigned msg, unsigned wp, int lp)
{
    HWND dlg = (HWND)(uintptr_t)(uint32_t)hdlg;
    UINT_PTR timer = 0;
    if (msg == WM_INITDIALOG) {
        g_rl_4e5aa8 = (int32_t)(uintptr_t)w_GetDlgItem(dlg, 1024);
        if (g_rl_4e5aa8 == 0) {
            w_EndDialog(dlg, 1);
            return 1;
        }
        enum_sessions();
        rl_451f70();
        if (g_rl_4e2a34 != 1) {
            timer = w_SetTimer(dlg, 0, 3000, NULL);
            rl_4afab9((int)(uintptr_t)STR2C(0x4e2cccu), 0, (int)timer + 1);   /* "OK:DP Got timer= " */
        }
        return 0;
    }
    if (msg == WM_MOVE) {
        rl_451f70();
        return 0;
    }
    if (msg == WM_TIMER) {
        if (g_rl_4e2a34 == 1) {
            BOOL k = w_KillTimer(dlg, timer);
            rl_4afab9((int)(uintptr_t)STR2C(0x4e2ce0u), 0, k + 1);   /* "OK:DP Killed timer " */
        } else {
            enum_sessions();
        }
        return 0;
    }
    if (msg != WM_COMMAND)
        return 0;
    unsigned code = (wp >> 16) & 0xffff;
    if (code == 1) {
        g_rl_4e5b04 = (int32_t)w_SendMessageA((HWND)(uintptr_t)(uint32_t)lp, LB_GETCURSEL, 0, 0);
        g_rl_4e5aa8 = lp;
        return 0;
    }
    if (code == 0) {
        if ((wp & 0xffff) == 2) {
            dp_drop();
            BOOL k = w_KillTimer(dlg, timer);
            rl_4afab9((int)(uintptr_t)STR2C(0x4e2cf4u), 0, k + 1);   /* "OK:DP Killed timer " */
            w_EndDialog(dlg, 0);
            rl_451f70();
            return 1;
        }
    } else if (code != 2) {
        return 0;
    }
    if (code == 2) {
        g_rl_4e5aa8 = lp;
        g_rl_4e5b04 = (int32_t)w_SendMessageA((HWND)(uintptr_t)(uint32_t)g_rl_4e5aa8, LB_GETCURSEL, 0, 0);
    }
    if (g_rl_4e5b04 == -1)
        return 0;
    uint32_t sd[20];
    sd_init(sd);
    memcpy(&sd[2], g_rl_4e5a80, 16);
    int32_t hr = (int32_t)((HRESULT (WINAPI *)(uint32_t, void *, uint32_t))(uintptr_t)VT(DPLAY, 96))(DPLAY, sd, 1);
    uint32_t line;
    switch ((uint32_t)hr) {
    case 0x00000000u: line = 0x4e2d08u; break;   /* success */
    case 0x88770014u: line = 0x4e2d28u; break;   /* active players */
    case 0x88770005u: line = 0x4e2d50u; break;   /* already initialized */
    case 0x80004005u: line = 0x4e2d7cu; break;   /* generic */
    case 0x88770078u: line = 0x4e2d9cu; break;   /* invalid flags */
    case 0x88770082u: line = 0x4e2dc0u; break;   /* invalid object */
    case 0x80070057u: line = 0x4e2de8u; break;   /* invalid params */
    case 0x887700fau: line = 0x4e2e10u; break;   /* unavailable */
    case 0x80004001u: line = 0x4e2e34u; break;   /* unsupported */
    case 0x88770118u: line = 0x4e2e58u; break;   /* user cancel */
    default:          line = 0x4e2e7cu; break;   /* unknown */
    }
    LOGS(STR2C(line));
    if (hr != 0) {
        dp_drop();
        w_EndDialog(dlg, 0);
    }
    rl_451f70();
    BOOL k = w_KillTimer(dlg, timer);
    rl_4afab9((int)(uintptr_t)STR2C(0x4e2e9cu), 0, k + 1);   /* "OK:DP Killed timer " */
    w_EndDialog(dlg, 1);
    return 1;
}

/* ================================================================ MP save, popups, title / menu helpers */

#define LOCAL       ((int32_t)s_slot_0057c8cc)
#define PL8(p, va)  AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define AUX8(p, va) AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))

/* Port of FUN_0049792e (262 B) — the MP lobby's game start (and the frame guard's MP save pump): DAT_0053f658 = 0,
 * DAT_0057c944 = 1, DAT_0053f06c = 7, DAT_00569584 = 1, DAT_0053f678 = 4; 0x43fed2; the text field byte_53F1E0 (16,
 * 200); realms 1..5: has-data (+5 of the record) = !aux +0x26 && aux +0x27; then engine_npc_schedule_init (its EAX). */
int rl_49792e(void)
{
    s_slot_0053f658 = 0;
    s_slot_0057c944 = 1;
    s_slot_0053f06c = 7;
    s_slot_00569584 = 1;
    s_slot_0053f678 = 4;
    rl_43fed2();
    rl_402009((int)(uintptr_t)g_bf_53f1e0, 0x10, 200, 0);   /* text_input_init (rl ui4) */
    rl_401f49();                                           /* text_input_recompute_length (rl ui4) */
    for (int i = 1; i < 6; i++) {
        if (AUX8(i, 0x553d76) != 0)
            PL8(i, 0x57bf05) = 0;
        else
            PL8(i, 0x57bf05) = AUX8(i, 0x553d77) != 0;
    }
    return rl_49caaa();   /* engine_npc_schedule_init (rl turn) */
}

/* Port of FUN_00479e0c (132 B) — the first live battle unit (1..80, +0x2c) whose owner (+0x09) is `owner`: its
 * WORD +0x178 (sign-extended); none → 0. */
int rl_479e0c(int owner)
{
    for (int i = 1; i < 81; i++)
        if (BU8(i, 0x5544ac) != 0 && (int)BU8(i, 0x554489) == owner)
            return BS16(i, 0x5545f8);
    return 0;
}

/* Port of FUN_0043695d (96 B) — accept a peer's diplomacy offer: dismiss the popup (0x476768), the partner
 * DAT_00553fbc = DAT_0057c964, DAT_005530b0 = DAT_0056894c, the gift DAT_0057a0f8 = 0, the diplomacy mode 9 (a yes,
 * DAT_0059154c) / 10; MP message 73 (0x43eda0(73, 0): its EAX). */
int rl_43695d(void)
{
    rl_476768();   /* engine_popup_scroll_dismiss (rl turn) */
    s_slot_00553fbc = (uint32_t)g_rl_57c964;
    s_slot_005530b0 = (uint32_t)g_rl_56894c;
    s_slot_0057a0f8 = 0;
    g_diplo_action_mode = AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu) != 0 ? 9 : 10;
    return rl_43eda0(73, 0);
}

/* Port of FUN_0043338e (211 B) — the DirectPlay "Full game / Skirmish" page's choice (DAT_0059154c): DAT_0057a0f0 =
 * 0; 2 (Skirmish) with ≤ 2 players (DAT_00553f98) → title page 11 (retail tests DAT_005c9a74 == 2 for page 10 and jumps
 * past it: always 11), the save list (0x42b919, 0x42b7f7), the field " " (44, 400), MP message 84; 1 (Full) → page 8,
 * the field " " (40, 280), MP message 84; a full redraw. EAX: 0x43eda0's, else the caller's (no reader). */
int rl_43338e(void)
{
    int r = 0;
    int32_t pick = AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu);
    s_slot_0057a0f0 = 0;
    if (pick == 2 && (int32_t)s_slot_00553f98 <= 2) {
        g_rl_5530f0 = 11;
        rl_42b919();
        rl_42b7f7();
        rl_402009((int)(uintptr_t)CSTR(g_rl_4d56dc, 0x4d56dcu, 0x4d56dcu), 0x2c, 400, 0);
        rl_401f49();
        r = rl_43eda0(84, 0);
    } else if (pick == 1) {
        g_rl_5530f0 = 8;
        rl_402009((int)(uintptr_t)CSTR(g_rl_4d56dc, 0x4d56dcu, 0x4d56e0u), 0x28, 280, 0);
        rl_401f49();
        r = rl_43eda0(84, 0);
    }
    g_bf_57d340 = 2;
    return r;
}

/* Port of FUN_0042d40b (159 B) — the skirmish .skr check: the base directory (0x4af502), its checksum (0x42d857); the
 * file (byte_53F1E0 + 0x41c) there (0x4af767) and a checksum DAT_005440bc equal to the peer's DAT_005691e4 →
 * DAT_0055cd34 = 0, else 1. EAX: 0x4af767's, or DAT_005440bc when that is nonzero. */
int rl_42d40b(void)
{
    rl_4af502();   /* io_path_chdir_base (rl host) */
    rl_42d857();
    int r = rl_4af767((int)(uintptr_t)g_rl_53f5fc);
    int same = 0;
    if (r != 0 && g_rl_5440bc != 0) {
        r = g_rl_5440bc;
        same = g_rl_5691e4[0] == g_rl_5440bc;
    }
    g_rl_55cd34 = same ? 0 : 1;
    return r;
}

/* Port of FUN_0040ec2c (152 B) — one message pump step: a message waiting → GetMessage (WM_QUIT: 0) / Translate /
 * Dispatch; none → the MP receive pump 0x4b8c3a. The quit request byte 0x5aeb68 → WM_CLOSE to the main window, cleared.
 * Returns 1. */
int rl_40ec2c(void)
{
    MSG m;
    if (rl_plat_PeekMessageA(&m, NULL, 0, 0, 0)) {
        if (!rl_plat_GetMessageA(&m, NULL, 0, 0))
            return 0;
        rl_plat_TranslateMessage(&m);
        rl_plat_DispatchMessageA(&m);
    } else {
        rl_4b8c3a();
    }
    if (g_gfx_last_error == 1) {
        rl_plat_PostMessageA(HWNDM, WM_CLOSE, 0, 0);
        g_gfx_last_error = 0;
    }
    return 1;
}

/* the gfx layer's state structs (rl_mpnet.c / rl_skirmish.c's helpers) */
#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define CHROME     ((const uint8_t *)g_rl_57d3d0)        /* &DAT_0057d3d0: the chrome sheet */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define METRICS    ((const unsigned char *)g_rl_4d71d0)  /* DAT_004d71d0: the measurers' table */
#define MISC       ((const uint8_t *)(uintptr_t)(uint32_t)g_bf_5530c8)   /* DAT_005530c8: the misc / icon sheet */
static localization_state_t s_loc;
static localization_state_t *loc_in(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
static void loc_out(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}
static void wrap_line(int idx, int skip, int x, int y, const uint8_t *font)   /* 0x402d37 */
{
    gfx_wrap_localized_line(loc_in(), idx, skip, x, y, font, GLYPHS, 0x3f);
    loc_out();
}
static void wrap_line_c(int idx, int skip, int x, int y, int w, const uint8_t *font, int color)   /* 0x402c5e */
{
    gfx_wrap_localized_line_centered(loc_in(), idx, skip, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void bf_minimap_scroll(int a) { engine_retail_call(0x4bc1d1u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_battlefield_render_minimap_scroll */
/* DAT_005533e0[p]: a side's ready flag — [2] IS DAT_005533e8, engine_army_state's g_army_state_index (a mirror) */
static int32_t ready(int p)
{ return p == 2 ? (int32_t)g_army_state_index : AT(int32_t, g_rl_5533e0, 0x5533e0u, 0x5533e0u + 4 * p); }

/* Port of FUN_00420630 (403 B) — the MP skirmish setup page (the caller's argument is unused): the misc sheet's
 * backdrop (sprite 0 at 0, 0); no shadow; the buttons (group 11: 37 at 461; 39 / 38 at 519 by the page state
 * DAT_0056899c; 36 at 577 when the opponent DAT_0056d5cc is ready: 1 → colour 63, 2 → 249); the redraw latches
 * (DAT_0057c948, DAT_0055ce94, the below-status redraw DAT_0053e9ac), the minimap strip 0x4bc1d1(80), the battle list
 * (0x421005, 0x4207c3, 0x420d40), the custom armies (page state: 0x42130f(6, 162) + 0x420de4) or the title / sides
 * (0x421231, 0x4209c1); shadow back, DAT_00553fc4 = 1, 0x41ff75(1), 0x420147(1) (its EAX: no reader). */
int rl_420630(int a0)
{
    (void)a0;
    gfx_sprite_render_from_blob(MISC, 0, 0, 0);
    g_gfx_no_shadow = 1;
    wrap_line_c(11, 37, 461, 440, 56, FONT, 63);
    if (g_rl_56899c != 0)
        wrap_line_c(11, 39, 519, 440, 56, FONT, 63);
    else
        wrap_line_c(11, 38, 519, 440, 56, FONT, 63);
    int32_t rd = ready((int32_t)s_slot_0056d5cc);
    if (rd == 1)
        wrap_line_c(11, 36, 577, 440, 56, FONT, 63);
    else if (ready((int32_t)s_slot_0056d5cc) == 2)
        wrap_line_c(11, 36, 577, 440, 56, FONT, 249);
    g_rl_57c948 = 1;
    g_rl_55ce94 = 1;
    engine_territory_dirty_state()->below_status_redraw = 1;   /* DAT_0053e9ac (a mirror: rh_hooks_ai.c) */
    bf_minimap_scroll(80);
    rl_421005();
    rl_4207c3();
    rl_420d40();
    if (g_rl_56899c == 0) {
        rl_421231();
        rl_4209c1();
    } else {
        rl_42130f(6, 162);
        rl_420de4();
    }
    g_gfx_no_shadow = 0;
    g_rl_553fc4 = 1;
    rl_41ff75(1);
    return rl_420147(1);
}

/* Port of FUN_00476488 (648 B) — the popup panel 20 (rl_47309e): a lobby chat line "From Beyond", kind = DAT_0053e910 -
 * 245, sender DAT_00553ee0. At the open (DAT_0057c954 == 2000) "ff_msg.wav" (DAT_005533f0 = 0) and the sender's
 * portrait (0x475d73). The box: x 16, w 448, y / h = 16 × the row DAT_004d6f30[kind]; the bevel panel, the sender's
 * shield, the seal, the portrait frame, group 109's line 0 + "Beyond" (tall font), the event's line 0; kinds 1 / 2:
 * the sender's message (save 149, 202 B per realm) in a box. EAX: the last draw's (no reader). */
int rl_476488(void)
{
    if (g_rl_57c954 == 2000) {
        g_combat_audio_latch = 0;
        rl_427990((int)(uintptr_t)CSTR(g_rl_4d7080, 0x4d7080u, 0x4d7080u), 1, 0);   /* "ff_msg.wav" */
    }
    if (g_rl_57c954 == 2000)
        rl_475d73(g_rl_553ee0);
    int kind = g_rl_53e910 - 245;
    g_rl_553060 = 16;
    g_rl_553024 = 448;
    g_rl_553064 = 16 * AT(int32_t, g_rl_4d6f30, 0x4d6f30u, 0x4d6f30 + 8 * kind);
    g_rl_552ff8 = 16 * AT(int32_t, g_rl_4d6f30, 0x4d6f30u, 0x4d6f34 + 8 * kind);
    int who = g_rl_553ee0;
    gfx_panel_draw_bevel(CHROME, g_rl_553060, g_rl_553064, g_rl_553024 / 16, g_rl_552ff8 / 16);
    gfx_sprite_render_from_blob(CHROME, PL8(who, 0x57bf0a) + 0xff, g_rl_553024 + g_rl_553060 - 30, g_rl_553064 + 18);
    gfx_sprite_seal_draw(SYSTEM, g_rl_553024 + g_rl_553060 - 48, g_rl_553064 + g_rl_552ff8 - 48, 0);
    gfx_line_rect_bevel_dark_br(g_rl_553060 + 15, g_rl_553064 + 17, 0x52, 0x4e);
    gfx_sprite_render_raw(g_rl_4eeb80, g_rl_553060 + 16, g_rl_553064 + 18, 0x50, 0x4c);
    g_gfx_line_x_acc = 0;
    wrap_line(109, 0, g_rl_553060 + 108, g_rl_553064 + 32, FONT_TALL);
    gfx_text_render_line((const uint8_t *)CSTR(g_rl_4d7080, 0x4d7080u, 0x4d708cu), g_gfx_line_x_acc + g_rl_553060 + 108,
                         g_rl_553064 + 32, FONT_TALL, GLYPHS, 0x3f);   /* "Beyond" */
    g_gfx_line_x_acc = 0;
    wrap_line(g_rl_53e910, 0, g_rl_553060 + 108, g_rl_553064 + 64, FONT);
    if (kind == 1 || kind == 2) {
        gfx_line_rect_bevel_dark_br(g_rl_553060 + 32, g_rl_553064 + 104, g_rl_553024 - 64, 90);
        gfx_wrap_buffer(s_slot_00567d10 + 202 * who, g_rl_553060 + 40, g_rl_553064 + 108, g_rl_553024 - 80, FONT, GLYPHS,
                        METRICS, 0x3f);
    }
    return 0;
}
