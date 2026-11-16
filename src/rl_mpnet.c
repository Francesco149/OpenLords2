/* rl_mpnet.c — a retail-layout port, partition 'mpnet' (tools/bf/parts.py; docs/findings/rl-mpnet.md). Retail semantics
 * on retail's layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * The SIGS transport (the ASIGSSession class, 0x74 B, DAT_004e2a30): its methods are __thiscall in retail (ECX = the
 * session); here the session is the first argument (`self`, a retail pointer). The SierraNW exports it calls are the
 * pointers retail stored in the session (+44..+112, cdecl): rhost's fixed thunks (rh_net.c), the real DLL in the
 * standalone. Win32 goes through retail's IAT slots under rhost (rl_io_plat.h's rule): IMP below. */
#include "rl.h"
#include "rl_mpnet_globals.h"
#include "rl_io_plat.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include "engine.h"               /* g_sim_mp_active */
#include "engine_turn.h"          /* g_sim_turn_progress (DAT_00553fc8) */
#include "engine_combat.h"        /* g_combat_winner_owner (DAT_005440cc), g_combat_anim_deadline (DAT_00568d9c) */
#include "save.h"
#include "save_schema_globals.h"
#include "strutil.h"
#include "engine_title_render.h"       /* g_tr_selected_button (DAT_00553114) */
#include "engine_bf_input_globals.h"   /* g_bf_515920 (a peer's battle result), g_bf_59154c, g_bf_553c6c, the font */
#include "rl_render_globals.h"         /* the system files' buffers (fonts, sheets), the localization blob */
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_wrap.h"
#include "localization.h"

#include <string.h>

/* other partitions' globals (their headers are partition-private: the declarations, as rl_turn.c does) */
extern uint8_t g_rl_4e2a28[0x8];      /* DAT_004e2a28 our DPID, DAT_004e2a2c the IDirectPlay2A (rl_io_globals.h) */
extern int32_t g_rl_4e2a30;           /* DAT_004e2a30: the SIGS session (rl_io_globals.h) */
extern int32_t g_rl_4e5a90[5];        /* DAT_004e5a90: the players' joined flags (rl_io_globals.h) */
extern int32_t g_rl_568464;           /* DAT_00568464: a session is up (rl_io_globals.h) */
extern int32_t g_rl_553248;           /* DAT_00553248: we created the session (rl_io_globals.h) */
extern uint8_t g_rl_4e5a00[4];        /* hWndMain (rl_misc_globals.h) */
extern int32_t g_rl_553264;           /* DAT_00553264: the slow-link flag (rl_ui2_globals.h) */
extern int32_t g_rl_58fe10;           /* DAT_0058fe10 (rl_ui2_globals.h) */
extern int32_t g_rl_5c9a74;           /* DAT_005c9a74: the SIGS players' count (rl_ui2_globals.h) */
extern uint8_t g_rl_4e5ab0[0x50];     /* DAT_004e5ab0: the DPSESSIONDESC (rl_ui2_globals.h) */
extern int32_t g_rl_57cb38;           /* DAT_0057cb38: a skirmish session (rl_ctl_globals.h) */
extern uint8_t g_rl_512fb0[10400];    /* DAT_00512fb0: the MP send ring, 40 × 260 B (rl_glue_globals.h) */
extern int32_t g_rl_5530f0;          /* DAT_005530f0: the title page (rl_io_globals.h) */
extern int32_t g_rl_553260;           /* DAT_00553260 (rl_turn_globals.h) */
extern int32_t g_rl_553ee0, g_rl_53e910, g_rl_5440ac, g_rl_55ce54, g_rl_57c954;   /* the event popup (rl_terr_globals.h) */
extern int32_t g_rl_553060, g_rl_553064, g_rl_553024, g_rl_552ff8;   /* the popup's box (rl_turn_globals.h) */
extern uint8_t g_rl_4dda90[0xf0];     /* DAT_004dda90..: the popups' button tables (rl_turn_globals.h) */
extern uint8_t g_rl_4eeb80[5912];     /* DAT_004eeb80: the 80 × 76 portrait (rl_glue_globals.h) */
extern int32_t g_rl_57c8b8;           /* DAT_0057c8b8 (rl_glue_globals.h) */
extern int32_t g_rl_57c948, g_rl_55ce94;   /* rl_skirmish_globals.h */
extern uint8_t g_rl_57cb40[0x6];      /* DAT_0057cb40: the seats (rl_glue_globals.h) */
extern uint8_t g_rl_4d5508[0x54];     /* DAT_004d5508.. (rl_glue_globals.h): DAT_004d554c the shield pick */

#define HWNDM   ((HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u))
#define SELF_ID AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u)    /* DAT_004e2a28 */
#define DPLAY   AT(uint32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a2cu)   /* DAT_004e2a2c */
#define NW      g_rl_4e2a30
#define HOBJECT AT(uint32_t, g_rl_4e5a04, 0x4e5a04u, 0x4e5a04u)   /* hObject: "MonitorThread-GameStart" */
#define CSTR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define STR24(va) CSTR(g_rl_4e24cc, 0x4e24ccu, va)   /* the SIGS strings 0x4e24cc.. */
#define STR2C(va) CSTR(g_rl_4e2c20, 0x4e2c20u, va)   /* 0x4e2c20.. */
#define S32(self, off) (*(int32_t *)(uintptr_t)((uint32_t)(self) + (off)))   /* a session field */
#define VT(obj, off)   (*(uint32_t *)(uintptr_t)(*(uint32_t *)(uintptr_t)(obj) + (off)))

/* ---------------------------------------------------------------- Win32 through retail's IAT slots (rhost) */
#define IMP(T, slot, host) (rl_plat_bound() ? (T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot) : (T)(host))
static int w_MessageBoxA(HWND w, const char *t, const char *c, UINT u)
{ return IMP(int (WINAPI *)(HWND, LPCSTR, LPCSTR, UINT), 0x5cf538u, MessageBoxA)(w, t, c, u); }
static HMODULE w_LoadLibraryA(const char *n)
{ return IMP(HMODULE (WINAPI *)(LPCSTR), 0x5cf39cu, LoadLibraryA)(n); }
static FARPROC w_GetProcAddress(HMODULE m, const char *n)
{ return IMP(FARPROC (WINAPI *)(HMODULE, LPCSTR), 0x5cf3a8u, GetProcAddress)(m, n); }
static BOOL w_FreeLibrary(HMODULE m)
{ return IMP(BOOL (WINAPI *)(HMODULE), 0x5cf394u, FreeLibrary)(m); }
static HANDLE w_CreateEventA(void *sa, BOOL man, BOOL init, const char *n)
{ return IMP(HANDLE (WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR), 0x5cf3e4u, CreateEventA)(sa, man, init, n); }
static HANDLE w_CreateThread(void *sa, SIZE_T st, uint32_t fn, void *arg, DWORD fl, DWORD *tid)
{ return IMP(HANDLE (WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD), 0x5cf390u,
             CreateThread)(sa, st, (LPTHREAD_START_ROUTINE)(uintptr_t)fn, arg, fl, tid); }
static DWORD w_WaitForSingleObject(HANDLE h, DWORD ms)
{ return IMP(DWORD (WINAPI *)(HANDLE, DWORD), 0x5cf398u, WaitForSingleObject)(h, ms); }
static BOOL w_ResetEvent(HANDLE h) { return IMP(BOOL (WINAPI *)(HANDLE), 0x5cf3b8u, ResetEvent)(h); }
static BOOL w_SetEvent(HANDLE h) { return IMP(BOOL (WINAPI *)(HANDLE), 0x5cf3ecu, SetEvent)(h); }
static void w_Sleep(DWORD ms) { IMP(void (WINAPI *)(DWORD), 0x5cf4acu, Sleep)(ms); }
static BOOL w_CloseHandle(HANDLE h) { return IMP(BOOL (WINAPI *)(HANDLE), 0x5cf3a0u, CloseHandle)(h); }
static BOOL w_IsIconic(HWND w) { return IMP(BOOL (WINAPI *)(HWND), 0x5cf54cu, IsIconic)(w); }
static BOOL w_ShowWindow(HWND w, int c) { return IMP(BOOL (WINAPI *)(HWND, int), 0x5cf53cu, ShowWindow)(w, c); }
static BOOL w_SetForegroundWindow(HWND w) { return IMP(BOOL (WINAPI *)(HWND), 0x5cf540u, SetForegroundWindow)(w); }
static HWND w_SetFocus(HWND w) { return IMP(HWND (WINAPI *)(HWND), 0x5cf568u, SetFocus)(w); }
static HMODULE w_GetModuleHandleA(const char *n) { return IMP(HMODULE (WINAPI *)(LPCSTR), 0x5cf3c0u, GetModuleHandleA)(n); }
static DWORD w_GetModuleFileNameA(HMODULE m, char *b, DWORD n)
{ return IMP(DWORD (WINAPI *)(HMODULE, LPSTR, DWORD), 0x5cf3bcu, GetModuleFileNameA)(m, b, n); }
static HWND w_GetDlgItem(HWND d, int id) { return IMP(HWND (WINAPI *)(HWND, int), 0x5cf520u, GetDlgItem)(d, id); }
static LRESULT w_SendMessageA(HWND w, UINT m, WPARAM wp, LPARAM lp)
{ return IMP(LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM), 0x5cf530u, SendMessageA)(w, m, wp, lp); }
static BOOL w_EndDialog(HWND d, INT_PTR r) { return IMP(BOOL (WINAPI *)(HWND, INT_PTR), 0x5cf534u, EndDialog)(d, r); }
static HRESULT w_DirectPlayEnumerateA(uint32_t cb, void *ctx)
{   /* dplayx is not linked in the standalone: no providers there */
    if (!rl_plat_bound()) return (HRESULT)0x80004005;
    return ((HRESULT (WINAPI *)(uint32_t, void *))(uintptr_t)*(volatile uint32_t *)(uintptr_t)0x5cf368u)(cb, ctx);
}

/* retail functions outside the partition (bare VAs; the ledger counts FUN_ tokens) */
#define VA_OPERATOR_NEW 0x4c3200u   /* ??2@YAPAXI@Z */
#define VA_SPLITPATH    0x4c2fb0u
#define VA_SPRINTF      0x4c2a80u

/* the SierraNW exports by their session slot */
typedef int (__cdecl *nw0_t)(void);
typedef int (__cdecl *nw1_t)(int);
typedef int (__cdecl *nw2_t)(int, int);
typedef int (__cdecl *nw3_t)(int, int, int);
typedef int (__cdecl *nw4_t)(int, int, int, int);
typedef int (__cdecl *nw7_t)(int, int, int, int, int, int, int);
#define NWF(T, self, off) ((T)(uintptr_t)(uint32_t)S32(self, off))

/* ================================================================ the ASIGSSession class */

/* Port of FUN_004b61b0 (314 B) — the session's constructor: its 29 dwords zeroed; returns the session. */
int rl_4b61b0(uint32_t self)
{
    for (int i = 0; i < 29; i++)
        S32(self, 4 * i) = 0;
    return (int)self;
}

/* Port of FUN_004b62ea (230 B) — Initialize(hwnd, dir, exe, game): the arguments into +20 / +28 / +32 / +24, the session
 * into DAT_004e5a08 (the monitor thread's), 0x4b64d8 (SNWValidate + SierraNW): -2 as is; -1 disconnected (+8 = 1);
 * another nonzero → "Failure has occured with Internet play." box; 0 → the game-start event hObject (manual reset) and
 * the wait 0x4b63d0 (its NAK → -1). Returns the load's result. */
int rl_4b62ea(uint32_t self, int hwnd, int dir, int exe, int game)
{
    S32(self, 20) = hwnd;
    S32(self, 28) = dir;
    S32(self, 32) = exe;
    S32(self, 24) = game;
    g_rl_4e5a08 = (int32_t)self;
    int r = rl_4b64d8(self);
    if (r != -2) {
        if (r == -1) {
            S32(self, 8) = 1;
        } else if (r != 0) {
            w_MessageBoxA((HWND)(uintptr_t)(uint32_t)S32(self, 20), STR24(0x4e24d4u), STR24(0x4e24ccu), 0x10);
        } else {
            HOBJECT = (uint32_t)(uintptr_t)w_CreateEventA(NULL, TRUE, FALSE, NULL);
            if (rl_4b63d0(self) == 0)
                return -1;
        }
    }
    return r;
}

/* Port of FUN_004b63d0 (211 B) — wait for the lobby's game start: hObject in 300 ms waits, pumping the message queue
 * between them (WM_QUIT → 0); then the event reset and +8 (disconnected) → "NewGame NAK", 0; else "NewGame ACK", 1. */
int rl_4b63d0(uint32_t self)
{
    MSG m;
    while (w_WaitForSingleObject((HANDLE)(uintptr_t)HOBJECT, 300) == WAIT_TIMEOUT) {
        while (rl_plat_PeekMessageA(&m, NULL, 0, 0, 0)) {
            if (m.message == WM_QUIT)
                return 0;
            if (rl_plat_GetMessageA(&m, NULL, 0, 0)) {
                rl_plat_TranslateMessage(&m);
                rl_plat_DispatchMessageA(&m);
            }
        }
    }
    w_ResetEvent((HANDLE)(uintptr_t)HOBJECT);
    if (S32(self, 8) != 0) {
        rl_plat_OutputDebugStringA(STR24(0x4e24fcu));   /* "NewGame NAK\n" */
        return 0;
    }
    rl_plat_OutputDebugStringA(STR24(0x4e250cu));       /* "NewGame ACK\n" */
    return 1;
}

/* Port of FUN_004b64d8 (636 B) — load the transport: SNWValid.dll!SNWValidate(game +24, ip_out "XXX.XXX.XXX.XXX", &a,
 * &b, dir +28, exe +32, hwnd +20) → -2 / -1 (+4 = 0) / 0 (+4 = 1) (a missing DLL or export: its box, -1); the DLL freed.
 * 0 → SierraNW.dll into +36, its exports (0x4b6754; a missing one: freed, box, -1), InitializeDLL(ip, a, b) (0: freed,
 * +36 = 0, -1) and the monitor thread StartAddress 0x4b7052 (+16 its handle). +4 = connected; returns the result. */
int rl_4b64d8(uint32_t self)
{
    DWORD tid;
    int connected = 0;
    char ip[16];
    memcpy(ip, STR24(0x4e251cu), 16);   /* "XXX.XXX.XXX.XXX" */
    int a = 0, b = 0, r = -1;
    S32(self, 36) = (int32_t)(uintptr_t)w_LoadLibraryA(STR24(0x4e252cu));   /* "SNWValid.dll" */
    if (S32(self, 36) != 0) {
        S32(self, 40) = (int32_t)(uintptr_t)w_GetProcAddress((HMODULE)(uintptr_t)(uint32_t)S32(self, 36), STR24(0x4e253cu));
        if (S32(self, 40) != 0)
            r = NWF(nw7_t, self, 40)(S32(self, 24), (int)(uintptr_t)ip, (int)(uintptr_t)&a, (int)(uintptr_t)&b,
                                     S32(self, 28), S32(self, 32), S32(self, 20));
        else
            w_MessageBoxA((HWND)(uintptr_t)(uint32_t)S32(self, 20), STR24(0x4e2560u), STR24(0x4e2548u), 0x10);
        w_FreeLibrary((HMODULE)(uintptr_t)(uint32_t)S32(self, 36));
        if (r == -1)
            connected = 0;
        else if (r == 0)
            connected = 1;
    } else {
        w_MessageBoxA((HWND)(uintptr_t)(uint32_t)S32(self, 20), STR24(0x4e25d4u), STR24(0x4e25c4u), 0x10);
    }
    if (r == 0) {
        S32(self, 36) = 0;
        S32(self, 36) = (int32_t)(uintptr_t)w_LoadLibraryA(STR24(0x4e2638u));   /* "SierraNW.dll" */
        if (S32(self, 36) != 0) {
            if (rl_4b6754(self) != 0) {
                if (NWF(nw3_t, self, 44)((int)(uintptr_t)ip, a, b) != 0) {
                    r = 0;
                    S32(self, 16) = (int32_t)(uintptr_t)w_CreateThread(NULL, 0, 0x4b7052u, NULL, 0, &tid);   /* StartAddress */
                } else {
                    w_FreeLibrary((HMODULE)(uintptr_t)(uint32_t)S32(self, 36));
                    S32(self, 36) = 0;
                    r = -1;
                    connected = 0;
                }
            } else {
                connected = 0;
                r = -1;
                w_FreeLibrary((HMODULE)(uintptr_t)(uint32_t)S32(self, 36));
                w_MessageBoxA((HWND)(uintptr_t)(uint32_t)S32(self, 20), STR24(0x4e2660u), STR24(0x4e2648u), 0x10);
            }
        } else {
            w_MessageBoxA((HWND)(uintptr_t)(uint32_t)S32(self, 20), STR24(0x4e26d4u), STR24(0x4e26c4u), 0x10);
            r = -1;
        }
    }
    S32(self, 4) = connected;
    return r;
}

/* Port of FUN_004b6754 (705 B) — resolve SierraNW's 18 exports from the module +36 into +44..+112; 1 when all are there. */
int rl_4b6754(uint32_t self)
{
    static const struct { uint16_t off; uint32_t name; } k[18] = {
        { 44, 0x4e2738u }, { 48, 0x4e2748u }, { 64, 0x4e2754u }, { 68, 0x4e276cu }, { 52, 0x4e2778u }, { 56, 0x4e2790u },
        { 72, 0x4e27a4u }, { 76, 0x4e27b8u }, { 80, 0x4e27c8u }, { 60, 0x4e27dcu }, { 84, 0x4e27ecu }, { 88, 0x4e27fcu },
        { 92, 0x4e2810u }, { 96, 0x4e2820u }, { 100, 0x4e2838u }, { 104, 0x4e2850u }, { 108, 0x4e285cu }, { 112, 0x4e2878u },
    };
    for (int i = 0; i < 18; i++)
        S32(self, k[i].off) = (int32_t)(uintptr_t)w_GetProcAddress((HMODULE)(uintptr_t)(uint32_t)S32(self, 36), STR24(k[i].name));
    for (int off = 44; off <= 112; off += 4)
        if (S32(self, off) == 0)
            return 0;
    return 1;
}

/* Port of FUN_004b6ae5 (140 B) — has the lobby started the game? Not loaded (+36) or not connected (+4): 0. Else
 * CheckGameConnectStatus (+64) == 1 → +0 = 1, 1; else +0 cleared, 0. */
int rl_4b6ae5(uint32_t self)
{
    int r = 0;
    if (S32(self, 36) == 0 || S32(self, 4) == 0)
        return 0;
    if (S32(self, 64) != 0) {
        if (NWF(nw0_t, self, 64)() == 1) {
            r = 1;
            S32(self, 0) = 1;
        } else if (S32(self, 0) != 0) {
            S32(self, 0) = 0;
        }
    }
    return r;
}

/* Port of FUN_004b6b71 (232 B) — receive one message into buf (size B): buf[0] = 0, *from = 0; CheckForAnyTCPMessage
 * (+52) == 0 (something waits) → for each player 0..GetNumberOfPlayers (inclusive): PeekForTCPMessage(buf, size, i) > 0
 * → too big: DebugBreak, 0; else RecvTCPMessage(buf, n, i) > 0 → *from = i, its length; else 0. Nothing: 0. */
int rl_4b6b71(uint32_t self, int buf, unsigned size, int pfrom)
{
    *(uint8_t *)(uintptr_t)(uint32_t)buf = 0;
    *(int32_t *)(uintptr_t)(uint32_t)pfrom = 0;
    if (S32(self, 52) != 0 && NWF(nw0_t, self, 52)() == 0) {
        int n = rl_4b6dc5(self);
        for (int i = 0; i <= n; i++) {
            int len = NWF(nw3_t, self, 56)(buf, (int)size, i);
            if (len > 0) {
                if (size < (unsigned)len) {
                    rl_plat_DebugBreak();
                    return 0;
                }
                len = NWF(nw3_t, self, 60)(buf, len, i);
                if (len > 0) {
                    *(int32_t *)(uintptr_t)(uint32_t)pfrom = i;
                    return len;
                }
                return 0;
            }
        }
    }
    return 0;
}

/* Port of FUN_004b6c59 (41 B) — SendTCPMessage(buf, len) (+76): a broadcast. */
int rl_4b6c59(uint32_t self, int buf, int len) { return NWF(nw2_t, self, 76)(buf, len); }

/* Port of FUN_004b6c82 (45 B) — SendTCPPointMessage(player, buf, len) (+80). */
int rl_4b6c82(uint32_t self, int player, int buf, int len) { return NWF(nw3_t, self, 80)(player, buf, len); }

/* Port of FUN_004b6ccb (35 B) — PingTime(2, -1) (+104). */
int rl_4b6ccb(uint32_t self) { return NWF(nw2_t, self, 104)(2, -1); }

/* Port of FUN_004b6cee (43 B) — TellMeSpecialChatReceived(1, cb, 0, 0) (+108). */
int rl_4b6cee(uint32_t self, int cb) { return NWF(nw4_t, self, 108)(1, cb, 0, 0); }

/* Port of FUN_004b6d19 (43 B) — TellMePlayerLeftGame(1, cb, 0, 0) (+112). */
int rl_4b6d19(uint32_t self, int cb) { return NWF(nw4_t, self, 112)(1, cb, 0, 0); }

/* Port of FUN_004b6d44 (129 B) — still connected? Not loaded (+36) or not connected (+4): 0. Else
 * CheckGameConnectStatus (+64) == -1 → +4 = 0, 0; else +4 = 1, 1 (also 1 with no export). */
int rl_4b6d44(uint32_t self)
{
    int r = 1;
    if (S32(self, 36) == 0 || S32(self, 4) == 0)
        return 0;
    if (S32(self, 64) != 0) {
        if (NWF(nw0_t, self, 64)() == -1) {
            r = 0;
            S32(self, 4) = 0;
        } else {
            S32(self, 4) = 1;
        }
    }
    return r;
}

/* Port of FUN_004b6dc5 (33 B) — GetNumberOfPlayers(0) (+72). */
int rl_4b6dc5(uint32_t self) { return NWF(nw1_t, self, 72)(0); }

/* Port of FUN_004b6de6 (53 B) — are we the session's creator? GetMyPlayerNumber() (+88) == 0. */
int rl_4b6de6(uint32_t self) { return NWF(nw0_t, self, 88)() == 0; }

/* Port of FUN_004b6e1b (28 B) — GetMyPlayerNumber() (+88). */
int rl_4b6e1b(uint32_t self) { return NWF(nw0_t, self, 88)(); }

/* Port of FUN_004b6e8e (105 B) — GetGameOption(name, out) (+92) into a 100 B buffer, copied (strncpy, count B) to dst;
 * none → dst[0] = 0. count > 100 (unsigned): 0 at once. Returns the export's result. */
int rl_4b6e8e(uint32_t self, int name, int dst, unsigned count)
{
    char src[100];
    if (count > 0x64)
        return 0;
    int r = NWF(nw2_t, self, 92)(name, (int)(uintptr_t)src);
    if (r != 0) {   /* the CRT's strncpy (0x4c28d0): pure — up to count B, NUL-padded, unterminated when full */
        char *d = (char *)(uintptr_t)(uint32_t)dst;
        unsigned i = 0;
        for (; i < count && src[i] != 0; i++)
            d[i] = src[i];
        for (; i < count; i++)
            d[i] = 0;
    } else
        *(char *)(uintptr_t)(uint32_t)dst = 0;
    return r;
}

/* ================================================================ the transport's users */

/* the session's scalar deleting destructor 0x4b92a0(self, 1) (rl mpnet2) */
static void nw_delete(uint32_t self) { rl_4b92a0(self, 1); }
/* close the session and delete it (retail's inlined `Close(); delete s; s = 0`) */
static void nw_close_delete(void)
{
    rl_4b6a15((uint32_t)NW);   /* Close (rl mpnet2) */
    uint32_t s = (uint32_t)NW;
    if (s != 0)
        nw_delete(s);
    NW = 0;
}

/* Port of FUN_004b7a9a (1246 B) — open (or resume) the SIGS session: DAT_004e2a60 set → cleared, 0x4b6ab6 on the old
 * session; none → operator new(0x74) + the constructor 0x4b61b0 into DAT_004e2a30. A new session: Initialize (0x4b62ea:
 * hWndMain, the exe's drive + dir ("%s%s" of _splitpath of GetModuleFileNameA("lords2.exe")), the path, "Lords2"); a
 * resumed one: the game-start wait 0x4b63d0 (1 → 0, else -1). -2 → exit(0). Nonzero → "InitializeSIGS - got
 * FAILED_TO_UPDATE", closed + deleted. 0 and still connected (0x4b6d44): the slow-link flag DAT_00553264 = ping ≥ 100
 * (negative: DebugBreak), DAT_0057cb38 = the "GameType" option ("Skirmish" 1, "Full" 0, else DebugBreak), the joined
 * flags DAT_004e5a90 cleared, DAT_00553248 = 0, DAT_00568464 = 1, our DPID DAT_004e2a28 = player number + 1; the
 * creator: DAT_00553248 = 1 when DAT_005c9a74 is 1..2, else DAT_0058fe10 = 1 and the session dropped. Not connected:
 * dropped. A new session's callbacks: special chat 0x4b79e3, player left 0x4b7a7c. Returns DAT_00568464 = (session). */
int rl_4b7a9a(void)
{
    char gametype[32], dirbuf[260], path[312], drive[4], dir[256];
    int r = 0;
    if (g_rl_4e2a60 != 0) {
        g_rl_4e2a60 = 0;
        rl_4b6ab6((uint32_t)NW);   /* (rl mpnet2) */
    }
    int resumed = 0;
    if (NW != 0) {
        resumed = 1;
    } else {
        uint32_t p = engine_retail_call(VA_OPERATOR_NEW, 1, 0x74u);   /* TODO(rl-extern): operator new */
        NW = p != 0 ? rl_4b61b0(p) : 0;
    }
    if (NW != 0) {
        if (resumed != 0) {
            r = rl_4b63d0((uint32_t)NW) != 0 ? 0 : -1;
        } else {
            HMODULE m = w_GetModuleHandleA(STR2C(0x4e2c20u));   /* "lords2.exe" */
            if (m == NULL)
                rl_plat_DebugBreak();
            w_GetModuleFileNameA(m, path, 0x104);
            engine_retail_call(VA_SPLITPATH, 5, (uint32_t)(uintptr_t)path, (uint32_t)(uintptr_t)drive,   /* TODO(rl-extern) */
                               (uint32_t)(uintptr_t)dir, 0u, 0u);
            engine_retail_call(VA_SPRINTF, 4, (uint32_t)(uintptr_t)dirbuf, (uint32_t)(uintptr_t)STR2C(0x4e2c2cu),   /* "%s%s" */
                               (uint32_t)(uintptr_t)drive, (uint32_t)(uintptr_t)dir);
            r = rl_4b62ea((uint32_t)NW, (int)(uintptr_t)HWNDM, (int)(uintptr_t)dirbuf, (int)(uintptr_t)path,
                          (int)(uintptr_t)STR2C(0x4e2c34u));   /* "Lords2" */
        }
        if (r == -2)
            rl_crt_4bf6f0(0);
        if (r != 0) {
            rl_plat_OutputDebugStringA(STR2C(0x4e2c5cu));   /* "InitializeSIGS - got FAILED_TO_UPDATE\n" */
            nw_close_delete();
        } else if (rl_4b6d44((uint32_t)NW) != 0) {
            int ping = rl_4b6ccb((uint32_t)NW);
            if (ping >= 0)
                g_rl_553264 = ping >= 100;
            else
                rl_plat_DebugBreak();
            g_rl_57cb38 = 0;
            if (rl_4b6e8e((uint32_t)NW, (int)(uintptr_t)STR2C(0x4e2c3cu), (int)(uintptr_t)gametype, 0x20) != 0) {   /* "GameType" */
                if (rl_crt_4c2170((uint32_t)(uintptr_t)gametype, (uint32_t)(uintptr_t)STR2C(0x4e2c48u)) == 0)        /* "Skirmish" */
                    g_rl_57cb38 = 1;
                else if (rl_crt_4c2170((uint32_t)(uintptr_t)gametype, (uint32_t)(uintptr_t)STR2C(0x4e2c54u)) == 0)   /* "Full" */
                    g_rl_57cb38 = 0;
                else
                    rl_plat_DebugBreak();
            }
            for (int i = 0; i < 5; i++)
                g_rl_4e5a90[i] = 0;
            g_rl_553248 = 0;
            g_rl_568464 = 1;
            SELF_ID = rl_4b6e1b((uint32_t)NW) + 1;
            if (rl_4b6de6((uint32_t)NW)) {
                if (g_rl_5c9a74 >= 1 && g_rl_5c9a74 <= 2) {
                    g_rl_553248 = 1;
                } else {
                    g_rl_58fe10 = 1;
                    nw_close_delete();
                }
            }
        } else {
            nw_close_delete();
        }
    }
    if (NW != 0 && resumed == 0) {
        rl_4b6cee((uint32_t)NW, 0x4b79e3);   /* the special-chat callback */
        rl_4b6d19((uint32_t)NW, 0x4b7a7c);   /* the player-left callback */
    }
    if (NW != 0) {
        g_rl_568464 = 1;
        return 1;
    }
    g_rl_568464 = 0;
    return 0;
}

/* Port of FUN_004b7052 (288 B) — the monitor thread (StartAddress; rhost runs it as a coroutine): "MonitorThread-Birth",
 * then until the session DAT_004e5a08's +8: disconnected (0x4b6d44 == 0) → "…-Disconnect", +8 = 1, SetEvent(hObject),
 * out; the lobby started the game (0x4b6ae5) → "…-GameStart", SetEvent, hWndMain restored (IsIconic → SW_RESTORE),
 * brought forward and focused, then 350 ms sleeps while it lasts, "…-GameEnd"; a 350 ms sleep. "…-Death",
 * CloseHandle(hObject), 0. */
int rl_4b7052(void)
{
    rl_plat_OutputDebugStringA(CSTR(g_rl_4e29a4, 0x4e29a4u, 0x4e29a4u));   /* "MonitorThread-Birth\n" */
    while (S32(g_rl_4e5a08, 8) == 0) {
        if (rl_4b6d44((uint32_t)g_rl_4e5a08) == 0) {
            rl_plat_OutputDebugStringA(CSTR(g_rl_4e29a4, 0x4e29a4u, 0x4e29bcu));   /* "MonitorThread-Disconnect\n" */
            S32(g_rl_4e5a08, 8) = 1;
            w_SetEvent((HANDLE)(uintptr_t)HOBJECT);
            break;
        }
        if (rl_4b6ae5((uint32_t)g_rl_4e5a08) != 0) {
            rl_plat_OutputDebugStringA(CSTR(g_rl_4e29a4, 0x4e29a4u, 0x4e29d8u));   /* "MonitorThread-GameStart\n" */
            w_SetEvent((HANDLE)(uintptr_t)HOBJECT);
            if (w_IsIconic(HWNDM))
                w_ShowWindow(HWNDM, SW_RESTORE);
            w_SetForegroundWindow(HWNDM);
            w_SetFocus(HWNDM);
            while (rl_4b6ae5((uint32_t)g_rl_4e5a08) != 0)
                w_Sleep(350);
            rl_plat_OutputDebugStringA(CSTR(g_rl_4e29a4, 0x4e29a4u, 0x4e29f4u));   /* "MonitorThread-GameEnd\n" */
        }
        w_Sleep(350);
    }
    rl_plat_OutputDebugStringA(CSTR(g_rl_4e29a4, 0x4e29a4u, 0x4e2a0cu));   /* "MonitorThread-Death\n" */
    w_CloseHandle((HANDLE)(uintptr_t)HOBJECT);
    return 0;
}

/* Port of FUN_004b7514 (87 B) — MP (DAT_00553030): lock new players out — DirectPlay's SetSessionDesc (+124) with the
 * session desc DAT_004e5ab0 (flags 1) when there is one — and log "OK :DP New players locked out." (0x4afab9, its
 * result). SP: nothing (EAX is the caller's; no caller reads it). */
int rl_4b7514(void)
{
    if (g_sim_mp_active == 0)
        return 0;
    if (DPLAY != 0)
        ((HRESULT (WINAPI *)(uint32_t, void *, uint32_t))(uintptr_t)VT(DPLAY, 124))(DPLAY, g_rl_4e5ab0, 1);
    return rl_4afab9((int)(uintptr_t)g_rl_4e2ac4, 0, 0);
}

/* Port of FUN_004b7f78 (715 B) — the provider dialog's DLGPROC (template 0x81): WM_MOVE → 0x451f70, 0. WM_INITDIALOG:
 * the list 1001 (none: EndDialog 1, TRUE) into hWnd DAT_004e5aa4, the DirectPlay providers (DirectPlayEnumerateA,
 * callback 0x4b82bf), the "Sierra Internet Gaming System" row (item data 0) selected into wParam DAT_004e5a10, focus,
 * 0x451f70, FALSE. WM_COMMAND: IDOK with a selection → its item data: a provider GUID → "OK :Got lpGUID.", 0x4b8243,
 * EndDialog 1; none (SIGS) → EndDialog 1003; IDCANCEL → "OK :Canceled netplay.", EndDialog 0; LBN_SELCHANGE → the
 * selection (control lParam into hWnd); LBN_DBLCLK → as IDOK on the control. */
int rl_4b7f78(int hdlg, unsigned msg, unsigned wp, int lp)
{
    HWND dlg = (HWND)(uintptr_t)(uint32_t)hdlg, ctl = (HWND)(uintptr_t)(uint32_t)lp;
    uint32_t *lbox = &AT(uint32_t, g_rl_4e5aa4, 0x4e5aa4u, 0x4e5aa4u);
    int32_t *sel = &AT(int32_t, g_rl_4e5a10, 0x4e5a10u, 0x4e5a10u);
    if (msg == WM_MOVE) {
        rl_451f70();
        return 0;
    }
    if (msg == WM_INITDIALOG) {
        *lbox = (uint32_t)(uintptr_t)w_GetDlgItem(dlg, 1001);
        if (*lbox == 0) {
            w_EndDialog(dlg, 1);
            return 1;
        }
        w_DirectPlayEnumerateA(0x4b82bfu, (void *)(uintptr_t)*lbox);
        *sel = (int32_t)w_SendMessageA((HWND)(uintptr_t)*lbox, LB_ADDSTRING, 0, (LPARAM)STR2C(0x4e2c84u));
        if (*sel != -1)
            w_SendMessageA((HWND)(uintptr_t)*lbox, LB_SETITEMDATA, (WPARAM)*sel, 0);
        w_SetFocus((HWND)(uintptr_t)*lbox);
        w_SendMessageA((HWND)(uintptr_t)*lbox, LB_SETCURSEL, (WPARAM)*sel, 0);
        rl_451f70();
        return 0;
    }
    if (msg != WM_COMMAND)
        return 0;
    unsigned code = wp >> 16;
    if (code == 0) {
        if ((wp & 0xffff) != IDOK) {
            if ((wp & 0xffff) == IDCANCEL) {
                rl_4afab9((int)(uintptr_t)STR2C(0x4e2cb4u), 0, 0);   /* "OK :Canceled netplay." */
                w_EndDialog(dlg, 0);
                return 1;
            }
            return 0;
        }
        if (*sel != -1) {
            LRESULT d = w_SendMessageA((HWND)(uintptr_t)*lbox, LB_GETITEMDATA, (WPARAM)*sel, 0);
            if (d != 0) {
                rl_4afab9((int)(uintptr_t)STR2C(0x4e2ca4u), 0, 0);   /* "OK :Got lpGUID." */
                rl_4b8243((int)d);   /* the provider's connection (rl mpnet2) */
                w_EndDialog(dlg, 1);
            } else {
                w_EndDialog(dlg, 1003);
            }
            return 1;
        }
        return 0;
    }
    if (code == LBN_SELCHANGE) {
        *sel = (int32_t)w_SendMessageA(ctl, LB_GETCURSEL, 0, 0);
        *lbox = (uint32_t)lp;
        return 0;
    }
    if (code != LBN_DBLCLK)
        return 0;
    *sel = (int32_t)w_SendMessageA(ctl, LB_GETCURSEL, 0, 0);
    if (*sel == -1)
        return 0;
    LRESULT d = w_SendMessageA(ctl, LB_GETITEMDATA, (WPARAM)*sel, 0);
    if (d != 0) {
        rl_4b8243((int)d);   /* (rl mpnet2) */
        w_EndDialog(dlg, 1);
    } else {
        w_EndDialog(dlg, 1003);
    }
    return 1;
}

/* Port of FUN_004b90fb (414 B) — send ring slot `slot` (DAT_00512fb0 + 260 slot, len B) to player dpid `to` (0 = all):
 * DirectPlay Send (+104: from our DPID DAT_004e2a28, DPSEND_GUARANTEED) with its errors logged (DPERR_INVALIDOBJECT
 * 0x88770082 / 0x887700e6 / 0x8877010e: "ERR:DP bad send - …"); else SIGS: SendTCPPointMessage(to - 1) (0x4b6c82) or
 * the broadcast (0x4b6c59), a failure logged. The 4th argument (reliable, pushed by 0x43eda0) is unused. */
int rl_4b90fb(int to, int slot, int len, int reliable)
{
    (void)reliable;
    uint8_t *buf = g_rl_512fb0 + 260 * slot;
    if (DPLAY != 0) {
        int32_t hr = (int32_t)((HRESULT (WINAPI *)(uint32_t, int32_t, int32_t, uint32_t, void *, int32_t))(uintptr_t)VT(DPLAY, 104))(
            DPLAY, SELF_ID, to, 1, buf, len);
        if (hr > (int32_t)0x88770096u) {
            if (hr == (int32_t)0x887700e6u)
                rl_4afab9((int)(uintptr_t)CSTR(g_rl_4e2fa8, 0x4e2fa8u, 0x4e2fe4u), 0, 0);   /* "ERR:DP bad send - send too big" */
            else if (hr == (int32_t)0x8877010eu)
                rl_4afab9((int)(uintptr_t)CSTR(g_rl_4e2fa8, 0x4e2fa8u, 0x4e2fa8u), 0, 0);   /* "… busy" */
        } else if (hr == (int32_t)0x88770082u) {
            rl_4afab9((int)(uintptr_t)CSTR(g_rl_4e2fa8, 0x4e2fa8u, 0x4e2fc0u), 0, 0);       /* "… invalid object" */
        }
    } else if (NW != 0) {
        if (to != 0) {
            if (rl_4b6c82((uint32_t)NW, to - 1, (int)(uintptr_t)buf, len) == 0)
                rl_4afab9((int)(uintptr_t)CSTR(g_rl_4e2fa8, 0x4e2fa8u, 0x4e3024u), 0, 0);   /* "ERR:SIGS bad send - point" */
        } else if (rl_4b6c59((uint32_t)NW, (int)(uintptr_t)buf, len) == 0) {
            rl_4afab9((int)(uintptr_t)CSTR(g_rl_4e2fa8, 0x4e2fa8u, 0x4e3004u), 0, 0);       /* "ERR:SIGS bad send - broadcast" */
        }
    }
    return 0;
}

/* Port of FUN_004bbf89 (73 B) — the keep-alive: ≥ 2000 ms (timeGetTime) since DAT_004e3bc0 → MP message 0x54
 * (0x43eda0), the stamp renewed; returns the time, else the elapsed ms. */
int rl_4bbf89(void)
{
    uint32_t t = rl_plat_timeGetTime();
    uint32_t r = t - (uint32_t)g_rl_4e3bc0;
    if ((int32_t)(t - (uint32_t)g_rl_4e3bc0) >= 2000) {
        rl_43eda0(0x54, 0);
        g_rl_4e3bc0 = (int32_t)t;
        return (int)t;
    }
    return (int)r;
}

/* Port of FUN_004bbfd2 (73 B) — as 0x4bbf89 with DAT_004e3bc4, 1000 ms and message 0x0e. */
int rl_4bbfd2(void)
{
    uint32_t t = rl_plat_timeGetTime();
    uint32_t r = t - (uint32_t)g_rl_4e3bc4;
    if ((int32_t)(t - (uint32_t)g_rl_4e3bc4) >= 1000) {
        rl_43eda0(0x0e, 0);
        g_rl_4e3bc4 = (int32_t)t;
        return (int)t;
    }
    return (int)r;
}

/* ================================================================ MP state helpers */

#define LOCAL ((int32_t)s_slot_0057c8cc)
#define TR16(t, va) AT(int16_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (t))   /* a territory record */
#define TR32(t, va) AT(int32_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (t))
#define TR8(t, va)  AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (t))
#define TRP(t, va)  (&TR8(t, va))
#define OVW8(off, va) AT(uint8_t, g_overworld_map, 0x522f90u, (int32_t)(va) + (int32_t)(off))
#define PL8(p, va)  AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define AUX8(p, va) AT(uint8_t, g_player_aux_44, 0x553d50u, (int32_t)(va) + 44 * (p))

/* Port of FUN_0043306a (92 B) — the local player's shield pick (MP setup): DAT_0057cb41 = the local player (byte), its
 * aux +0x25 and its realm record's shield +0x0a = DAT_004d554c. EAX: (44 × player & ~0xff) | the shield (no reader). */
int rl_43306a(void)
{
    AT(uint8_t, g_rl_57cb40, 0x57cb40u, 0x57cb41u) = (uint8_t)LOCAL;
    AUX8(LOCAL, 0x553d75) = AT(uint8_t, g_rl_4d5508, 0x4d5508u, 0x4d554cu);
    uint8_t v = AUX8(LOCAL, 0x553d75);
    PL8(LOCAL, 0x57bf0a) = v;
    return (int)(((uint32_t)(44 * LOCAL) & ~0xffu) | v);
}

/* Port of FUN_0046f2f1 (220 B) — rebuild the map's army links: the 4096 cells' link bytes +5 / +0xd / +0x15 / +0x1d of
 * every 32 B row cleared (the 8 B descriptors of g_overworld_map, four per row), every territory's +4 head cleared
 * (1..150), then each territory with an army (+0 != 0, signed) relinked (engine_unit_map_link 0x46eddf). EAX: no reader. */
int rl_46f2f1(void)
{
    for (int i = 0; i < 0x8000; i += 32) {
        OVW8(i, 0x522f95) = 0;
        OVW8(i, 0x522f9d) = 0;
        OVW8(i, 0x522fa5) = 0;
        OVW8(i, 0x522fad) = 0;
    }
    for (int j = 1; j < 151; j++)
        TR16(j, 0x52f0b4) = 0;
    for (int k = 1; k < 151; k++)
        if ((int8_t)TR8(k, 0x52f0b0) != 0)
            engine_retail_call(0x46eddfu, 1, (uint32_t)k);   /* TODO(rl-link): engine_unit_map_link */
    return 0;
}

/* Port of FUN_0047f937 (714 B) — apply a peer's battle result (the MP message's 42 B at DAT_00515920): DAT_0059154c,
 * DAT_0057c924 (the winner), DAT_005440cc (its owner) cleared; the attacker / defender territories' (DAT_00568210 /
 * DAT_00568218) +0x168 and +0x196 cleared, their 11 unit words +0x16c zeroed; then from the message: attacker +0x168
 * (2 B), +0x196 (2), the 7 unit words +0x16c (2 each), the same for the defender at +20, DAT_0059154c (2), the winner
 * (1) and its owner (1). */
int rl_47f937(void)
{
    const uint8_t *m = g_bf_515920;
    AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu) = 0;
    s_slot_0057c924 = 0;
    g_combat_winner_owner = 0;
    int a = (int)s_slot_00568210, d = (int)s_slot_00568218;
    TR32(a, 0x52f218) = 0;
    TR32((int)s_slot_00568218, 0x52f218) = 0;
    TR8((int)s_slot_00568210, 0x52f246) = 0;
    TR8((int)s_slot_00568218, 0x52f246) = 0;
    for (int i = 0; i < 11; i++)
        TR16((int)s_slot_00568210, 0x52f21c + 2 * i) = 0;
    for (int j = 0; j < 11; j++)
        TR16((int)s_slot_00568218, 0x52f21c + 2 * j) = 0;
    a = (int)s_slot_00568210;
    d = (int)s_slot_00568218;
    strutil_bounded_copy(TRP(a, 0x52f218), m + 0, 2);
    strutil_bounded_copy(TRP(a, 0x52f246), m + 2, 2);
    for (int k = 0; k < 7; k++)
        strutil_bounded_copy(TRP(a, 0x52f21c + 2 * k), m + 2 * k + 4, 2);
    strutil_bounded_copy(TRP(d, 0x52f218), m + 20, 2);
    strutil_bounded_copy(TRP(d, 0x52f246), m + 22, 2);
    for (int n = 0; n < 7; n++)
        strutil_bounded_copy(TRP(d, 0x52f21c + 2 * n), m + 2 * n + 24, 2);
    strutil_bounded_copy(&AT(uint8_t, g_bf_59154c, 0x59154cu, 0x59154cu), m + 38, 2);
    strutil_bounded_copy(&s_slot_0057c924, m + 40, 1);
    strutil_bounded_copy(&g_combat_winner_owner, m + 41, 1);
    return 1;
}

/* Port of FUN_0049b973 (374 B) — reset the turn's run state: DAT_0056d6a4 = 0, the tick cadence (0x40489e), the army
 * links (0x46f2f1), DAT_0057c998 and the 10 per-player ticks DAT_0053e8d0 cleared. fresh (a0) != 0: the deadline
 * DAT_005440c8 = DAT_0053f26c + 100, DAT_00568d9c = now, the turn handshake DAT_00553fc8 = 2, the restart count
 * DAT_00553f7c = 0, DAT_00553260 counted down. 0 (an MP restart): DAT_00553c6c = now, the turn's snapshot reloaded
 * (save_load: "lastturn.sv[ab]" the first time, then "old_turn.sv[ab]"; a = the host DAT_00553248), DAT_00553260 = 2,
 * the event 0x103 posted to the local player, DAT_00553fc8 = DAT_0055403c = 0, the deadline + 10, DAT_00568d9c = now,
 * DAT_00553f7c + 1. Returns the last timeGetTime. */
int rl_49b973(int fresh)
{
    uint32_t t;
    g_rl_56d6a4 = 0;
    rl_40489e();
    rl_46f2f1();
    g_bf_57c998[0] = 0;
    for (int i = 0; i < 10; i++)
        g_bf_53e8d0[i] = 0;
    if (fresh != 0) {
        s_slot_005440c8 = s_slot_0053f26c + 100;
        t = rl_plat_timeGetTime();
        g_combat_anim_deadline = t;
        g_sim_turn_progress = 2;
        g_rl_553f7c = 0;
        if (g_rl_553260 != 0)
            g_rl_553260--;
        return (int)t;
    }
    g_bf_553c6c = (int32_t)rl_plat_timeGetTime();
    /* "old_turn.sva" / "old_turn.svb" once restarted, else "lastturn.sva" / "lastturn.svb" */
    uint32_t va = g_rl_553f7c != 0 ? (g_rl_553248 != 0 ? 0x4dc340u : 0x4dc350u) : (g_rl_553248 != 0 ? 0x4dc320u : 0x4dc330u);
    rl_4adfe6((int)(uintptr_t)CSTR(g_rl_4dc320, 0x4dc320u, va));
    g_rl_553260 = 2;
    rl_472bc5(0, LOCAL, 0x103, 0, 0, 0, 0, 0);
    g_sim_turn_progress = 0;
    g_bf_55403c = 0;
    s_slot_005440c8 = s_slot_0053f26c + 10;
    t = rl_plat_timeGetTime();
    g_combat_anim_deadline = t;
    g_rl_553f7c++;
    return (int)t;
}

/* Port of FUN_0049ca60 (74 B) — is seat a0 taken by another (DAT_0057cb40[a0] != 0 and != a1)? */
int rl_49ca60(int seat, int who)
{
    unsigned v = AT(uint8_t, g_rl_57cb40, 0x57cb40u, 0x57cb40 + seat);
    if (v == (unsigned)who)
        return 0;
    return v != 0;
}

/* Port of FUN_004b3d5d (112 B) — copy n bytes src → dst: 32 B blocks as 8 dwords, then bytes (forward). EAX: the last
 * moved value (no reader). */
int rl_4b3d5d(int src, int dst, int n)
{
    const uint8_t *s = (const uint8_t *)(uintptr_t)(uint32_t)src;
    uint8_t *d = (uint8_t *)(uintptr_t)(uint32_t)dst;
    uint32_t v = 0;
    while (n >= 32) {
        for (int i = 0; i < 8; i++) {
            memcpy(&v, s + 4 * i, 4);
            memcpy(d + 4 * i, &v, 4);
        }
        s += 32;
        d += 32;
        n -= 32;
    }
    while (n > 0) {
        v = (v & ~0xffu) | *s;
        *d++ = *s++;
        n--;
    }
    return (int)v;
}

/* Port of FUN_00433551 (159 B) — the MP setup's client (DAT_00553248 == 0) on title page 6: a loaded game
 * (DAT_0057a0f0) → page 11, the save list (0x42b919, 0x42b7f7), the text field " " (44, 400); else page 8 and the
 * field " " (40, 280); the length recomputed, a full redraw. EAX: the length's / the caller's (no reader). */
int rl_433551(void)
{
    if (g_rl_553248 != 0 || g_rl_5530f0 != 6)
        return 0;
    if (s_slot_0057a0f0 != 0) {
        g_rl_5530f0 = 11;
        rl_42b919();
        rl_42b7f7();
        rl_402009((int)(uintptr_t)CSTR(g_rl_4d56f0, 0x4d56f0u, 0x4d56f0u), 0x2c, 400, 0);
    } else {
        g_rl_5530f0 = 8;
        rl_402009((int)(uintptr_t)CSTR(g_rl_4d56f0, 0x4d56f0u, 0x4d56f4u), 0x28, 280, 0);
    }
    int r = rl_401f49();
    g_bf_57d340 = 2;
    return r;
}

/* ================================================================ MP title pages and the MP message popup (the gfx layer) */

#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define CHROME     ((const uint8_t *)g_rl_57d3d0)        /* &DAT_0057d3d0: the chrome sheet */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define METRICS    ((const unsigned char *)g_rl_4d71d0)  /* DAT_004d71d0: the measurers' table */
#define NAME(p)    (&g_player_aux_44[44 * (p) + 4])     /* a realm's name (aux +4) */
#define STRVA(blk, blkva, va) ((int)(uintptr_t)&AT(uint8_t, blk, blkva, va))

/* the localized-text walker over retail's blob / pointer, loaded and stored around each call (rl_turn.c's pattern) */
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
static void wrap_buf(const uint8_t *buf, int x, int y, int w)   /* 0x40352f, the default font */
{
    gfx_wrap_buffer(buf, x, y, w, FONT, GLYPHS, METRICS, 0x3f);
}
static void button_strip(int table_va)   /* 0x40cfd2 through retail, as rl_turn.c does */
{
    engine_retail_call(0x40cfd2u, 4, 0u, 0u, (uint32_t)STRVA(g_rl_4dda90, 0x4dda90u, table_va), 2u);
}

/* Port of FUN_0041f3e9 (184 B) — the MP setup page's frame: the panel (DAT_004eabec) 32 × 8 at (64, 50) for the
 * session's creator (DAT_00553248), else 24 × 6 at (128, 50); its text group 39 with the uppercase highlight — the
 * creator: line 0 centered in the tall font (64, 80, 512); the others: the wrapped paragraph 3 (160, 80, 320 wide) —
 * then the game-type buttons (0x41f4a1). EAX: 0x41f4a1's (no reader). */
int rl_41f3e9(void)
{
    if (g_rl_553248 != 0)
        gfx_panel_draw(g_gfx_current_blob, 64, 50, 32, 8);
    else
        gfx_panel_draw(g_gfx_current_blob, 128, 50, 24, 6);
    g_gfx_uppercase_highlight = 1;
    if (g_rl_553248 != 0) {
        wrap_line_c(39, 0, 64, 80, 512, FONT_TALL, 0x3f);
    } else {
        gfx_wrap_localized(loc_in(), 39, 3, 160, 80, 320, 100, 0, 0, FONT, GLYPHS, METRICS, 0x3f);
        loc_out();
    }
    g_gfx_uppercase_highlight = 0;
    return rl_41f4a1();
}

/* Port of FUN_0041f4a1 (241 B) — the creator's game-type buttons (DAT_00553248 only): no shadow, two bevelled boxes
 * (110, 116) / (366, 116) 164 × 24 with group 39's lines 1 / 2 centered (160 wide), the selected one (DAT_00553114 1 / 2)
 * in 0xf9, else 0x3f; the dirty mark (112, 215, 14 × 3). EAX: the caller's when not the creator (no reader). */
int rl_41f4a1(void)
{
    if (g_rl_553248 == 0)
        return 0;
    g_gfx_no_shadow = 1;
    int c = g_tr_selected_button == 1 ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(110, 116, 164, 24);
    wrap_line_c(39, 1, 112, 122, 160, FONT, c);
    c = g_tr_selected_button == 2 ? 0xf9 : 0x3f;
    gfx_line_rect_bevel_palette_b(366, 116, 164, 24);
    wrap_line_c(39, 2, 368, 122, 160, FONT, c);
    int r = rl_4527a6(112, 215, 14, 3, 1);
    g_gfx_no_shadow = 0;
    return r;
}

/* Port of FUN_0041f77a (243 B) — the custom / MP setup page's labels: no shadow; group 11 line 12 centered at
 * (165, 198, 76); MP and the creator: lines 13..15 at x 243 / 321 / 399; then DAT_0057c948 = DAT_0055ce94 =
 * DAT_00553fc4 = 1, the country list (0x41f98b), the roster (0x41fbcb), the tabs (0x41f86d, a0), 0x41ff75(0),
 * 0x420147(0) (its EAX). */
int rl_41f77a(int tab)
{
    g_gfx_no_shadow = 1;
    wrap_line_c(11, 12, 165, 198, 76, FONT, 0x3f);
    if (g_sim_mp_active != 0 && g_rl_553248 != 0) {
        wrap_line_c(11, 13, 243, 198, 76, FONT, 0x3f);
        wrap_line_c(11, 14, 321, 198, 76, FONT, 0x3f);
        wrap_line_c(11, 15, 399, 198, 76, FONT, 0x3f);
    }
    g_rl_57c948 = 1;
    g_rl_55ce94 = 1;
    g_rl_553fc4 = 1;
    rl_41f98b();                   /* rl_link: engine_title_render_setup_country_list */
    rl_41fbcb();                   /* rl_link: engine_title_render_setup_roster */
    rl_41f86d((int)tab);    /* rl_link: engine_title_render_setup_tabs */
    rl_41ff75(0);
    return rl_420147(0);
}

/* 0x475e07's stack residue (quirk #240: its [ebp-8] is read before any store): the row's adapter passes the dword at
 * e0 - 12 and puts what the function leaves at e0 - 12 / e0 - 8. A port caller passes rl_475e07_res_in, the port's
 * model of what retail's frame leaves in that slot (engine_tick's body esp - 92): the map scene's imprint 0x405ac5
 * (set by rl_4b99c0 after the scene; the last row's last tile call 0x405ac0 in 0x4059af), the popup tail's
 * DAT_00553064 push (set by rl_47309e). Never 3 in either: the liege dismiss below is dead under both. */
int rl_475e07_res_set, rl_475e07_res_kind, rl_475e07_res_who;
int rl_475e07_res_in;

/* Port of FUN_00475e07 (1665 B) — the popup panel 12 (rl_47309e): the MP messages between humans, kind = DAT_0053e910 -
 * 245 (0..10), sender DAT_00553ee0. First the uninitialized local `kind` (stack residue, `kind_in`): 3 with a
 * liege (+0x81 of the local realm) → dismiss the popup (0x476768) instead. At the open (DAT_0057c954 == 2000)
 * "ff_msg.wav" (DAT_005533f0 = 0) and the sender's portrait (0x475d73). The box: x 16, w 448, y / h = 16 × the row
 * DAT_004d6f30[kind]; the bevel panel, the sender's shield, the seal, the portrait frame, group 109's line 0 + the
 * sender's name (tall font), the event's line 0; by kind: 0 line 1 + a unit count (DAT_005440ac, 0x41ab67); 1 / 2 the
 * sender's message (save 149, 202 B per realm) in a box; 3 (DAT_0057c8b8 = sender) line 1, the message box, the buttons
 * 0x4ddac0; 4 line 1; 5 / 6 (DAT_00554008 / DAT_0057c964 = sender, DAT_0056894c = the county DAT_0055ce54) line 1, the
 * county's name (group 100), its shield (0x410c71), the buttons 0x4ddaf0 / 0x4ddb20; 7..10 as 5 without the buttons;
 * else line 1. */
int rl_475e07_r(int kind_in)
{
    rl_475e07_res_set = 0;
    if (kind_in == 3 && PL8(LOCAL, 0x57bf81) != 0)
        return rl_476768();
    if (g_rl_57c954 == 2000) {
        g_combat_audio_latch = 0;
        rl_427990(STRVA(g_rl_4d7074, 0x4d7074u, 0x4d7074u), 1, 0);   /* "ff_msg.wav" */
    }
    if (g_rl_57c954 == 2000)
        rl_475d73(g_rl_553ee0);
    int kind = g_rl_53e910 - 245;
    rl_475e07_res_set = 1;
    rl_475e07_res_kind = kind;
    g_rl_553060 = 16;
    g_rl_553024 = 448;
    g_rl_553064 = 16 * AT(int32_t, g_rl_4d6f30, 0x4d6f30u, 0x4d6f30 + 8 * kind);
    g_rl_552ff8 = 16 * AT(int32_t, g_rl_4d6f30, 0x4d6f30u, 0x4d6f34 + 8 * kind);
    int who = g_rl_553ee0;
    rl_475e07_res_who = who;
    gfx_panel_draw_bevel(CHROME, g_rl_553060, g_rl_553064, g_rl_553024 / 16, g_rl_552ff8 / 16);
    gfx_sprite_render_from_blob(CHROME, PL8(who, 0x57bf0a) + 0xff, g_rl_553024 + g_rl_553060 - 30, g_rl_553064 + 18);
    gfx_sprite_seal_draw(SYSTEM, g_rl_553024 + g_rl_553060 - 48, g_rl_553064 + g_rl_552ff8 - 48, 0);
    gfx_line_rect_bevel_dark_br(g_rl_553060 + 15, g_rl_553064 + 17, 0x52, 0x4e);
    gfx_sprite_render_raw(g_rl_4eeb80, g_rl_553060 + 16, g_rl_553064 + 18, 0x50, 0x4c);
    g_gfx_line_x_acc = 0;
    wrap_line(109, 0, g_rl_553060 + 108, g_rl_553064 + 32, FONT_TALL);
    gfx_text_render_line(NAME(who), g_gfx_line_x_acc + g_rl_553060 + 108, g_rl_553064 + 32, FONT_TALL, GLYPHS, 0x3f);
    g_gfx_line_x_acc = 0;
    wrap_line(g_rl_53e910, 0, g_rl_553060 + 108, g_rl_553064 + 64, FONT);
    const uint8_t *msg = s_slot_00567d10 + 202 * who;
    switch (kind) {
    case 0:
        g_gfx_line_x_acc = 0;
        wrap_line(g_rl_53e910, 1, g_rl_553060 + 32, g_rl_553064 + 104, FONT);
        return rl_41ab67((int)g_rl_5440ac, 0, g_gfx_line_x_acc + g_rl_553060 + 32, g_rl_553064 + 104,   /* rl_link: labor_num_units */
                         (int)(uintptr_t)FONT, 0x3f);
    case 1:
    case 2:
        gfx_line_rect_bevel_dark_br(g_rl_553060 + 32, g_rl_553064 + 104, g_rl_553024 - 64, 90);
        wrap_buf(msg, g_rl_553060 + 40, g_rl_553064 + 108, g_rl_553024 - 80);
        return 0;
    case 3:
        g_rl_57c8b8 = who;
        wrap_line(g_rl_53e910, 1, g_rl_553060 + 160, g_rl_553064 + 104, FONT);
        gfx_line_rect_bevel_dark_br(g_rl_553060 + 32, g_rl_553064 + 136, g_rl_553024 - 64, 90);
        wrap_buf(msg, g_rl_553060 + 40, g_rl_553064 + 140, g_rl_553024 - 80);
        button_strip(0x4ddac0);
        return 0;
    case 4:
        wrap_line(g_rl_53e910, 1, g_rl_553060 + 32, g_rl_553064 + 104, FONT);
        return 0;
    case 5:
    case 6:
        if (kind == 5)
            g_rl_554008 = who;
        else
            g_rl_57c964 = who;
        g_rl_56894c = g_rl_55ce54;
        wrap_line(g_rl_53e910, 1, g_rl_553060 + 32, g_rl_553064 + 104, FONT);
        wrap_line(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, g_rl_553060 + 32, g_rl_553064 + 120, FONT);
        rl_410c71(g_rl_55ce54, g_rl_553060 + 256, g_rl_553064 + 64);
        button_strip(kind == 5 ? 0x4ddaf0 : 0x4ddb20);
        return 0;
    default:
        break;
    }
    wrap_line(g_rl_53e910, 1, g_rl_553060 + 32, g_rl_553064 + 104, FONT);
    if (kind != 7 && kind != 8 && kind != 9 && kind != 10)
        return 0;
    wrap_line(100, g_rl_55ce54 + 20 * (int32_t)s_slot_0053f034, g_rl_553060 + 32, g_rl_553064 + 120, FONT);
    return rl_410c71(g_rl_55ce54, g_rl_553060 + 256, g_rl_553064 + 64);
}
/* a port caller: the residue is the port's model of retail's frame (rl_475e07_res_in) */
int rl_475e07(void) { return rl_475e07_r(rl_475e07_res_in); }
