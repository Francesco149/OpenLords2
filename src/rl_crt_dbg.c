/* rl_crt_dbg.c — retail's statically linked MSVC 4.x DEBUG CRT: the report / message / exit / signal path: _CrtDbgReport and its window, _amsg_exit / _NMSG_WRITE, _exit / abort / raise, _flsbuf / _getbuf, the C++ unhandled-exception filter.
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-dbg.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot, an unported callee by bare VA (engine_retail_call).
 * docs/findings/crt-gap-dbg.md has the per-function notes (quirks, probe coverage).
 *
 * __alloca_probe 0x4caae0 (eax = size, moves esp: not a C function) has no port: its callers' big frames are C locals. */
#include "rl_crt_dbg.h"
#include "rl_crt.h"
#include "engine_retail_call.h"

#include <stdarg.h>
#include <string.h>
#include <windows.h>

#define V32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define U32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define U8(va)  (*(volatile uint8_t *)(uintptr_t)(va))
#define PA(p)   ((uint32_t)(uintptr_t)(p))   /* a host pointer as a 32-bit retail address */
#define IAT(T, slot) ((T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot))

/* ---------------------------------------------------------------- retail's globals */
#define G_AEXIT_RTN  0x4e45c8u   /* _aexit_rtn (→ _exit 0x4bf710) */
#define G_ERRMODE    0x4e45ccu   /* __error_mode (0 = _OUT_TO_DEFAULT) */
#define G_APPTYPE    0x4e45d0u   /* __app_type (2 = GUI) */
#define G_RPTBUSY    0x4e45d8u   /* _CrtDbgReport's recursion count (InterlockedIncrement; -1 idle) */
#define G_RPTMODE    0x4e45e0u   /* _CrtDbgMode[3]: 2 / 4 / 4 (1 file, 2 debug, 4 window) */
#define G_RPTFILE    0x4e45f0u   /* _CrtDbgFile[3]: -1 */
#define G_RPTTYPES   0x4e4600u   /* _CrtDbgModeMsg[3]: "Warning" / "Error" / "Assertion Failed" */
#define G_WSPRINTF   0x4e460cu   /* user32!wsprintfA (GetProcAddress, cached) */
#define G_XCPTTAB    0x4e54e8u   /* _XcptActTab: 12-byte { xcpt, signal, action } */
#define G_FIRSTFPE   0x4e5560u   /* _First_FPE_Indx (3) */
#define G_NUMFPE     0x4e5564u   /* _Num_FPE (7) */
#define G_XCPTCOUNT  0x4e5568u   /* _XcptActTabCount (10) */
#define G_FPECODE    0x4e556cu   /* _fpecode */
#define G_PXCPTINFO  0x4e5570u   /* _pxcptinfoptrs */
#define G_BADIOINFO  0x4e5580u   /* __badioinfo (fd -1) */
#define G_NMSGTAB    0x4e5588u   /* _NMSG_WRITE's 18 { number, text } */
#define G_ADBGMSG    0x4e5618u   /* _adbgmsg (0) */
#define G_SIGINT     0x4e5620u   /* ctrlc_action */
#define G_SIGBREAK   0x4e5624u   /* ctrlbreak_action */
#define G_SIGABRT    0x4e5628u   /* abort_action */
#define G_SIGTERM    0x4e562cu   /* term_action */
#define G_PFNMSGBOX  0x4e5634u   /* user32!MessageBoxA / GetActiveWindow / GetLastActivePopup (cached) */
#define G_PFNACTWIN  0x4e5638u
#define G_PFNPOPUP   0x4e563cu
#define G_CFLUSH     0x4e5960u   /* _cflush */
#define G_IOB1       0x4e5700u   /* &_iob[1] (stdout) */
#define G_IOB2       0x4e5720u   /* &_iob[2] (stderr) */
#define G_PTERMINATE 0x4e52c4u   /* __pTerminate */
#define G_UEF        0x4e5d34u   /* the previous unhandled-exception filter */
#define G_PIOINFO    0x5ced40u   /* __pioinfo[] */
#define G_NHANDLE    0x5cee40u   /* _nhandle */
#define G_RPTHOOK    0x5cee48u   /* _pfnReportHook */

/* retail's .rdata strings */
#define S_STRCHECK   0x4d087cu   /* "string != NULL" */
#define S_FMTCHECK   0x4d0860u   /* "format != NULL" */
#define S_SPRINTF_C  0x4d0870u   /* "sprintf.c" */
#define S_VSPRINTF_C 0x4d0f88u   /* "vsprintf.c" */
#define S_RPT_SDS    0x4d08c0u   /* "%s(%d) : %s" */
#define S_NL         0x4d08ccu   /* "\n" */
#define S_CR         0x4d08d0u   /* "\r" */
#define S_ASSERT_NF  0x4d08d4u   /* "Assertion failed!" */
#define S_ASSERT     0x4d08e8u   /* "Assertion failed: " */
#define S_TOOLONG    0x4d08fcu   /* "_CrtDbgReport: String too long or IO Error" */
#define S_SECOND     0x4d0928u   /* "Second Chance Assertion Failed: File %s, Line %d\n" */
#define S_WSPRINTFA  0x4d095cu   /* "wsprintfA" */
#define S_USER32     0x4d0968u   /* "user32.dll" */
#define S_DBGLIB     0x4d0974u   /* "Microsoft Visual C++ Debug Library" */
#define S_DEBUGFMT   0x4d0998u   /* "Debug %s!\n\nProgram: %s%s%s%s%s%s%s%s%s%s%s\n\n(Press Retry to debug the application)" */
#define S_MODULE     0x4d09ecu   /* "\nModule: " */
#define S_FILE       0x4d09f8u   /* "\nFile: " */
#define S_LINE       0x4d0a00u   /* "\nLine: " */
#define S_NLNL       0x4d0a08u   /* "\n\n" */
#define S_EXPRESSION 0x4d0a0cu   /* "Expression: " */
#define S_EMPTY      0x4d0a1cu   /* "" */
#define S_FORINFO    0x4d0a20u   /* "\n\nFor information on how your program can cause an assertion\n…" */
#define S_DOTS       0x4d0a94u   /* "..." */
#define S_PGMUNKNOWN 0x4d0a98u   /* "<program name unknown>" */
#define S_DBGRPT_C   0x4d0ab0u   /* "dbgrpt.c" */
#define S_USERMSG    0x4d0abcu   /* "szUserMessage != NULL" */
#define S_IOBFIELDS  0x4d0b24u   /* "(\"inconsistent IOB fields\", stream->_ptr - stream->_base >= 0)" */
#define S_FLSBUF_C   0x4d0b64u   /* "_flsbuf.c" */
#define S_STRNOTNULL 0x4d0b70u   /* "str != NULL" */
#define S_RTLIB      0x4d0f44u   /* "Microsoft Visual C++ Runtime Library" */
#define S_RUNTIMEERR 0x4d0f6cu   /* "Runtime Error!\n\nProgram: " */
#define S_POPUP      0x4d0f94u   /* "GetLastActivePopup" */
#define S_ACTWIN     0x4d0fa8u   /* "GetActiveWindow" */
#define S_MSGBOXA    0x4d0fb8u   /* "MessageBoxA" */
#define S_GETBUF_C   0x4d1000u   /* "_getbuf.c" */

/* the IAT slots */
#define I_LoadLibraryA         0x5cf39cu
#define I_GetProcAddress       0x5cf3a8u
#define I_DebugBreak           0x5cf3acu
#define I_OutputDebugStringA   0x5cf3b4u
#define I_GetModuleFileNameA   0x5cf3bcu
#define I_InterlockedIncrement 0x5cf420u
#define I_InterlockedDecrement 0x5cf428u
#define I_GetStdHandle         0x5cf42cu
#define I_WriteFile            0x5cf430u
#define I_IsBadCodePtr         0x5cf498u

static void int3(void) { __asm__ volatile("int3"); }
static LONG rpt_inc(void) { return IAT(LONG (WINAPI *)(LONG volatile *), I_InterlockedIncrement)((LONG volatile *)(uintptr_t)G_RPTBUSY); }
static void rpt_dec(void) { IAT(LONG (WINAPI *)(LONG volatile *), I_InterlockedDecrement)((LONG volatile *)(uintptr_t)G_RPTBUSY); }
static void odstr(uint32_t s) { IAT(void (WINAPI *)(LPCSTR), I_OutputDebugStringA)((LPCSTR)(uintptr_t)s); }
static DWORD module_name(char *buf) { return IAT(DWORD (WINAPI *)(HMODULE, LPSTR, DWORD), I_GetModuleFileNameA)(NULL, buf, 0x104); }
/* the CRT's asserts: _CrtDbgReport(_CRT_ASSERT, file, line, NULL, expr) == 1 → break */
#define ASSERT_RPT(file, line, expr) (rl_crt_dbgreport(2, (file), (line), 0, (expr)) == 1)

/* _CrtDbgReport as C code calls it (variadic: i686 cdecl, every argument a dword — the va_list is the stack address
 * of the first one, as retail's ArgList) */
int rl_crt_dbgreport(int type, uint32_t file, int line, uint32_t mod, uint32_t fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = rl_crt_4c3950(type, file, line, mod, fmt, PA(ap));
    va_end(ap);
    return r;
}
/* _snprintf likewise */
static int snprintf_(uint32_t buf, uint32_t n, uint32_t fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = rl_crt_4ca900(buf, n, fmt, PA(ap));
    va_end(ap);
    return r;
}

/* ================================================================ strings / numbers */

/* Port of FUN_004c2090 (224 B) — strcat (the dword-at-a-time scan / copy: no byte past the terminator is written).
 * Returns the destination. */
uint32_t rl_crt_4c2090(uint32_t d, uint32_t s)
{
    strcat((char *)(uintptr_t)d, (const char *)(uintptr_t)s);
    return d;
}

/* Port of FUN_004ca630 (181 B) — xtoa: v as unsigned in radix r (digits 0-9a-z), '-' first and v negated when neg.
 * EAX (void in the CRT source; returned for the shadow's comparison) = the swap loop's last right-hand pointer. */
uint32_t rl_crt_4ca630(uint32_t v, uint32_t buf, uint32_t radix, int neg)
{
    uint32_t p = buf;
    if (neg != 0) { U8(p) = '-'; p++; v = 0u - v; }
    uint32_t first = p;
    do {
        uint32_t digit = v % radix;
        v /= radix;
        U8(p) = (uint8_t)(digit > 9 ? digit + 0x57 : digit + 0x30);
        p++;
    } while (v != 0);
    U8(p) = 0;
    p--;
    do {
        uint8_t t = U8(p);
        U8(p) = U8(first);
        U8(first) = t;
        p--;
        first++;
    } while (first < p);
    return p;
}

/* Port of FUN_004ca5d0 (88 B) — _itoa: negative only in radix 10. Returns buf. */
uint32_t rl_crt_4ca5d0(int v, uint32_t buf, int radix)
{
    if (radix == 10 && v < 0) rl_crt_4ca630((uint32_t)v, buf, (uint32_t)radix, 1);
    else rl_crt_4ca630((uint32_t)v, buf, (uint32_t)radix, 0);
    return buf;
}

/* ================================================================ stdio */

/* the string FILE _snprintf / _vsnprintf build on their stack: _ptr, _cnt, _base, _flag (the rest unread) */
static int strout(uint32_t buf, uint32_t n, uint32_t fmt, uint32_t argp, uint32_t srcfile, int line0)
{
    uint32_t file[8] = { 0 };
    uint32_t f = PA(file);
    if (buf == 0 && ASSERT_RPT(srcfile, line0, S_STRCHECK)) int3();
    if (fmt == 0 && ASSERT_RPT(srcfile, line0 + 1, S_FMTCHECK)) int3();
    file[3] = 0x42;   /* _IOWRT | _IOSTRG */
    file[2] = buf;
    file[0] = file[2];
    file[1] = n;
    int r = rl_crt_4c68f0(f, fmt, argp);
    if (--V32(f + 4) < 0)
        rl_crt_4c6650(0, f);
    else {
        U8(U32(f)) = 0;
        U32(f) += 1;
    }
    return r;
}

/* Port of FUN_004ca900 (235 B) — _snprintf (argp: &format + 4): __output into a string FILE of n bytes; the
 * terminator only if it fits (else _flsbuf on the string stream: _IOERR). Asserts sprintf.c 93 / 94. Returns
 * __output's count (-1 when the output did not fit). */
int rl_crt_4ca900(uint32_t buf, uint32_t n, uint32_t fmt, uint32_t argp)
{
    return strout(buf, n, fmt, argp, S_SPRINTF_C, 93);
}

/* Port of FUN_004ca9f0 (229 B) — _vsnprintf: the same with a va_list; asserts vsprintf.c 90 / 91. */
int rl_crt_4ca9f0(uint32_t buf, uint32_t n, uint32_t fmt, uint32_t argp)
{
    return strout(buf, n, fmt, argp, S_VSPRINTF_C, 90);
}

/* Port of FUN_004cc590 (66 B) — _isatty: FDEV of fd's osfile; an fd >= _nhandle (unsigned: -1 too) → 0. */
int rl_crt_4cc590(int fd)
{
    if ((uint32_t)fd >= U32(G_NHANDLE)) return 0;
    uint32_t info = U32(G_PIOINFO + ((uint32_t)(fd & ~31) >> 3)) + 8u * (uint32_t)(fd & 31);
    return (int)(int8_t)U8(info + 4) & 0x40;
}

/* Port of FUN_004cc4d0 (188 B) — _getbuf: ++_cflush; a 4 KB _CRT_BLOCK (_IOMYBUF) or, failing that, the 2-byte
 * _charbuf (_IONBF); _ptr = _base, _cnt = 0. Asserts _getbuf.c 46. Returns the stream. */
uint32_t rl_crt_4cc4d0(uint32_t f)
{
    if (f == 0 && ASSERT_RPT(S_GETBUF_C, 46, S_STRNOTNULL)) int3();
    U32(G_CFLUSH) += 1;
    U32(f + 8) = rl_crt_4bfa30(0x1000, 2, S_GETBUF_C, 59);
    if (U32(f + 8) != 0) {
        U32(f + 12) |= 8u;
        U32(f + 24) = 0x1000;
    } else {
        U32(f + 12) |= 4u;
        U32(f + 8) = f + 20;
        U32(f + 24) = 2;
    }
    U32(f) = U32(f + 8);
    U32(f + 4) = 0;
    return f;
}

/* Port of FUN_004c6650 (660 B) — _flsbuf: write ch to a full stream. Not writable (_IOWRT | _IORW clear) or a string
 * stream → _IOERR, -1; a read stream must be at EOF (else _IOERR). No buffer yet → _getbuf (stdout / stderr only when
 * not a tty). Buffered: flush _ptr - _base bytes (none: seek to the end of an FAPPEND fd) and store ch first; unbuffered:
 * write ch alone. Returns ch (unsigned char) or -1 (_IOERR). Asserts _flsbuf.c 105 / 160. */
int rl_crt_4c6650(int ch, uint32_t f)
{
    if (f == 0 && ASSERT_RPT(S_FLSBUF_C, 105, S_STRNOTNULL)) int3();
    int fd = V32(f + 16);
    if ((U32(f + 12) & 0x82) == 0 || (U32(f + 12) & 0x40) != 0) {
        U32(f + 12) |= 0x20u;
        return -1;
    }
    if (U32(f + 12) & 1) {
        U32(f + 4) = 0;
        if ((U32(f + 12) & 0x10) == 0) {
            U32(f + 12) |= 0x20u;
            return -1;
        }
        U32(f) = U32(f + 8);
        U32(f + 12) &= ~1u;
    }
    U32(f + 12) |= 2u;
    U32(f + 12) &= ~0x10u;
    U32(f + 4) = 0;
    int n = 0, wrote = 0;
    if ((U32(f + 12) & 0x10c) == 0 && ((f != G_IOB1 && f != G_IOB2) || rl_crt_4cc590(fd) == 0))
        rl_crt_4cc4d0(f);
    if (U32(f + 12) & 0x108) {
        if ((int32_t)(U32(f) - U32(f + 8)) < 0 && ASSERT_RPT(S_FLSBUF_C, 160, S_IOBFIELDS)) int3();
        n = (int32_t)(U32(f) - U32(f + 8));
        U32(f) = U32(f + 8) + 1;
        V32(f + 4) = V32(f + 24) - 1;
        if (n > 0)
            wrote = rl_crt_4cc0b0((uint32_t)fd, U32(f + 8), (uint32_t)n);
        else {
            uint32_t info = fd == -1 ? G_BADIOINFO
                                     : U32(G_PIOINFO + ((uint32_t)(fd & ~31) >> 3)) + 8u * (uint32_t)(fd & 31);
            if ((int8_t)U8(info + 4) & 0x20) rl_crt_4cc3b0((uint32_t)fd, 0, 2);
        }
        U8(U32(f + 8)) = (uint8_t)ch;
    } else {
        n = 1;
        uint32_t c = (uint32_t)ch;
        wrote = rl_crt_4cc0b0((uint32_t)fd, PA(&c), 1);
    }
    if (wrote != n) {
        U32(f + 12) |= 0x20u;
        return -1;
    }
    return ch & 0xff;
}

/* ================================================================ the report path (dbgrpt.c) */

/* Port of FUN_004c37d0 (17 B) — _CrtDbgBreak: DebugBreak. */
void rl_crt_4c37d0(void)
{
    IAT(void (WINAPI *)(void), I_DebugBreak)();
}

/* Port of FUN_004cb010 (223 B) — __crtMessageBoxA: user32 loaded on first use (MessageBoxA, GetActiveWindow,
 * GetLastActivePopup cached in .data; no user32 / no MessageBoxA → 0); the owner = the active window's last active
 * popup. Returns MessageBoxA's result. */
int rl_crt_4cb010(uint32_t text, uint32_t caption, uint32_t type)
{
    uint32_t owner = 0;
    if (U32(G_PFNMSGBOX) == 0) {
        HMODULE h = IAT(HMODULE (WINAPI *)(LPCSTR), I_LoadLibraryA)((LPCSTR)(uintptr_t)S_USER32);
        if (h == NULL) return 0;
        FARPROC (WINAPI *gpa)(HMODULE, LPCSTR) = IAT(FARPROC (WINAPI *)(HMODULE, LPCSTR), I_GetProcAddress);
        U32(G_PFNMSGBOX) = PA(gpa(h, (LPCSTR)(uintptr_t)S_MSGBOXA));
        if (U32(G_PFNMSGBOX) == 0) return 0;
        U32(G_PFNACTWIN) = PA(gpa(h, (LPCSTR)(uintptr_t)S_ACTWIN));
        U32(G_PFNPOPUP) = PA(gpa(h, (LPCSTR)(uintptr_t)S_POPUP));
    }
    if (U32(G_PFNACTWIN) != 0) owner = ((uint32_t (WINAPI *)(void))(uintptr_t)U32(G_PFNACTWIN))();
    if (owner != 0 && U32(G_PFNPOPUP) != 0) owner = ((uint32_t (WINAPI *)(uint32_t))(uintptr_t)U32(G_PFNPOPUP))(owner);
    return ((int (WINAPI *)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)U32(G_PFNMSGBOX))(owner, text, caption,
                                                                                                 type);
}

/* Port of FUN_004c3d40 (813 B) — _CrtMessageWindow: "Debug <type>!" box (Abort / Retry / Ignore): the program path
 * and the module name cut to their last 64 characters with "..." written over the cut (IN PLACE: the caller's module
 * string changes), file / line / message lines present only when given. Abort → raise(SIGABRT), _exit(3). Returns
 * 1 for Retry (the caller breaks), else 0. Asserts dbgrpt.c 474 (msg NULL — then dereferences it anyway). */
int rl_crt_4c3d40(int type, uint32_t file, uint32_t linestr, uint32_t mod, uint32_t msg)
{
    char dest[4096], fname[260];
    if (msg == 0 && ASSERT_RPT(S_DBGRPT_C, 474, S_USERMSG)) rl_crt_4c37d0();
    if (module_name(fname) == 0) rl_crt_4c2080(PA(fname), S_PGMUNKNOWN);
    uint32_t str = PA(fname);
    if (rl_crt_4bf620(str) > 0x40) {
        str += rl_crt_4bf620(str) - 0x40;
        rl_crt_4c28d0(str, S_DOTS, 3);
    }
    uint32_t m = mod;
    if (m != 0 && rl_crt_4bf620(m) > 0x40) {
        m = mod + rl_crt_4bf620(mod) - 0x40;
        rl_crt_4c28d0(m, S_DOTS, 3);
    }
    int has_msg = U8(msg) != 0;
    if (snprintf_(PA(dest), 0x1000, S_DEBUGFMT, U32(G_RPTTYPES + 4u * (uint32_t)type), str,
                  m != 0 ? S_MODULE : S_EMPTY, m != 0 ? m : S_EMPTY,
                  file != 0 ? S_FILE : S_EMPTY, file != 0 ? file : S_EMPTY,
                  linestr != 0 ? S_LINE : S_EMPTY, linestr != 0 ? linestr : S_EMPTY,
                  has_msg ? S_NLNL : S_EMPTY, has_msg && type == 2 ? S_EXPRESSION : S_EMPTY, has_msg ? msg : S_EMPTY,
                  type == 2 ? S_FORINFO : S_EMPTY) < 0)
        rl_crt_4c2080(PA(dest), S_TOOLONG);
    int r = rl_crt_4cb010(PA(dest), S_DBGLIB, 0x12012);   /* MB_TASKMODAL | MB_ICONHAND | MB_ABORTRETRYIGNORE | MB_SETFOREGROUND */
    if (r == IDABORT) {
        rl_crt_4cad80(22);
        rl_crt_4bf710(3);
    }
    return r == IDRETRY;
}

/* Port of FUN_004c3950 (998 B) — _CrtDbgReport (argp: the address of the first variadic argument). type 0..2 (else
 * -1). A nested assert (the busy count > 0 after the increment) → "Second Chance Assertion Failed" through user32's
 * wsprintfA (GetProcAddress, cached) to the debugger, _CrtDbgBreak, -1 — the count stays incremented if user32 /
 * wsprintfA cannot be had. Else the message: the formatted text (4077 bytes max), asserts prefixed "Assertion failed: "
 * ("Assertion failed!" without a format) and "\r\n" / "\n" terminated, "file(line) : " in front; the report hook may
 * take it (returns its *ret); else per _CrtDbgMode[type]: file (WriteFile), debugger, window (_CrtMessageWindow's
 * result is returned). Returns 0 otherwise. */
int rl_crt_4c3950(int type, uint32_t file, int line, uint32_t mod, uint32_t fmt, uint32_t argp)
{
    char source[4096], out[4096], dest[4096], num[20];
    int32_t ret;
    DWORD wrote;
    memset(source, 0, sizeof source);
    memset(out, 0, sizeof out);
    memset(dest, 0, sizeof dest);
    if (type < 0 || type >= 3) return -1;
    if (type == 2 && rpt_inc() > 0) {
        if (U32(G_WSPRINTF) == 0) {
            HMODULE h = IAT(HMODULE (WINAPI *)(LPCSTR), I_LoadLibraryA)((LPCSTR)(uintptr_t)S_USER32);
            if (h == NULL) return -1;
            U32(G_WSPRINTF) = PA(IAT(FARPROC (WINAPI *)(HMODULE, LPCSTR), I_GetProcAddress)(h, (LPCSTR)(uintptr_t)S_WSPRINTFA));
            if (U32(G_WSPRINTF) == 0) return -1;
        }
        ((int (__cdecl *)(uint32_t, uint32_t, ...))(uintptr_t)U32(G_WSPRINTF))(PA(out), S_SECOND, file, line);
        odstr(PA(out));
        rpt_dec();
        rl_crt_4c37d0();
        return -1;
    }
    if (fmt != 0 && rl_crt_4ca9f0(PA(dest), 0xfed, fmt, argp) < 0)
        rl_crt_4c2080(PA(dest), S_TOOLONG);
    if (type == 2)
        rl_crt_4c2080(PA(source), fmt != 0 ? S_ASSERT : S_ASSERT_NF);
    rl_crt_4c2090(PA(source), PA(dest));
    if (type == 2) {
        if (U32(G_RPTMODE + 4u * (uint32_t)type) & 1) rl_crt_4c2090(PA(source), S_CR);
        rl_crt_4c2090(PA(source), S_NL);
    }
    if (file != 0) {
        if (snprintf_(PA(out), 0x1000, S_RPT_SDS, file, (uint32_t)line, PA(source)) < 0)
            rl_crt_4c2080(PA(out), S_TOOLONG);
    } else
        rl_crt_4c2080(PA(out), PA(source));
    if (U32(G_RPTHOOK) != 0
        && ((int (__cdecl *)(int, uint32_t, uint32_t))(uintptr_t)U32(G_RPTHOOK))(type, PA(out), PA(&ret)) != 0) {
        if (type == 2) rpt_dec();
        return ret;
    }
    uint32_t mode = U32(G_RPTMODE + 4u * (uint32_t)type);
    if ((mode & 1) && U32(G_RPTFILE + 4u * (uint32_t)type) != 0xffffffffu)
        IAT(BOOL (WINAPI *)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED), I_WriteFile)(
            (HANDLE)(uintptr_t)U32(G_RPTFILE + 4u * (uint32_t)type), out, rl_crt_4bf620(PA(out)), &wrote, NULL);
    if (U32(G_RPTMODE + 4u * (uint32_t)type) & 2) odstr(PA(out));
    if (U32(G_RPTMODE + 4u * (uint32_t)type) & 4) {
        uint32_t linestr = line != 0 ? rl_crt_4ca5d0(line, PA(num), 10) : 0;
        ret = rl_crt_4c3d40(type, file, linestr, mod, PA(dest));
        if (type == 2) rpt_dec();
        return ret;
    }
    if (type == 2) rpt_dec();
    return 0;
}

/* ================================================================ runtime errors / exit / signals */

/* Port of FUN_004ca330 (537 B) — _NMSG_WRITE: runtime error message `num` (_RT_* table, 18 entries; the search reads
 * the dword past the table: num 0 "matches" _adbgmsg's slot with a NULL text). Not 252 (_RT_CRNL): reported as
 * _CRT_ERROR first. Console (error mode 1, or default with a console app): WriteFile to fd 2's handle (or
 * GetStdHandle(STD_ERROR_HANDLE)) — returns WriteFile's result; GUI: "Runtime Error!" box (path cut to its last 60
 * characters) — returns __crtMessageBoxA's. EAX otherwise: the table index (18 when not found / num 252 on GUI). */
uint32_t rl_crt_4ca330(int num)
{
    char box[160], fname[260];
    uint32_t i, r;
    for (i = 0; i < 0x12; i++)
        if (V32(G_NMSGTAB + 8u * i) == num) break;
    r = i;
    if (V32(G_NMSGTAB + 8u * i) != num) return r;
    uint32_t text = U32(G_NMSGTAB + 8u * i + 4);
    if (num != 252) {
        r = (uint32_t)rl_crt_dbgreport(1, 0, 0, 0, text);
        if (r == 1) int3();
    }
    if (V32(G_ERRMODE) == 1 || (V32(G_ERRMODE) == 0 && V32(G_APPTYPE) == 1)) {
        HANDLE h;
        uint32_t blk = U32(G_PIOINFO);
        if (blk == 0 || V32(blk + 16) == -1)
            h = IAT(HANDLE (WINAPI *)(DWORD), I_GetStdHandle)(STD_ERROR_HANDLE);
        else
            h = (HANDLE)(uintptr_t)U32(blk + 16);
        DWORD wrote;
        return (uint32_t)IAT(BOOL (WINAPI *)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED), I_WriteFile)(
            h, (LPCVOID)(uintptr_t)text, rl_crt_4bf620(text), &wrote, NULL);
    }
    if (num == 252) return i;
    if (module_name(fname) == 0) rl_crt_4c2080(PA(fname), S_PGMUNKNOWN);
    uint32_t str = PA(fname);
    if (rl_crt_4bf620(str) + 1 > 0x3c) {
        str += rl_crt_4bf620(PA(fname)) - 0x3b;
        rl_crt_4c28d0(str, S_DOTS, 3);
    }
    rl_crt_4c2080(PA(box), S_RUNTIMEERR);
    rl_crt_4c2090(PA(box), str);
    rl_crt_4c2090(PA(box), S_NLNL);
    rl_crt_4c2090(PA(box), text);
    return (uint32_t)rl_crt_4cb010(PA(box), S_RTLIB, 0x12010);   /* MB_TASKMODAL | MB_ICONHAND | MB_SETFOREGROUND */
}

/* Port of FUN_004ca2d0 (95 B) — _FF_MSGBANNER: console (error mode 1, or default with a console app): the "\r\n" +
 * _adbgmsg + "runtime error " banner. */
void rl_crt_4ca2d0(void)
{
    if (V32(G_ERRMODE) == 1 || (V32(G_ERRMODE) == 0 && V32(G_APPTYPE) == 1)) {
        rl_crt_4ca330(252);
        if (U32(G_ADBGMSG) != 0) ((void (__cdecl *)(void))(uintptr_t)U32(G_ADBGMSG))();
        rl_crt_4ca330(255);
    }
}

/* Port of FUN_004c35c0 (47 B) — _amsg_exit: the banner (error mode 1 only), the message, then _aexit_rtn(255)
 * (→ _exit: never returns; retail would run into the next function if it did). */
void rl_crt_4c35c0(int num)
{
    if (V32(G_ERRMODE) == 1) rl_crt_4ca2d0();
    rl_crt_4ca330(num);
    ((void (__cdecl *)(int))(uintptr_t)U32(G_AEXIT_RTN))(255);
}

/* Port of FUN_004bf710 (27 B) — _exit: doexit(code, 1, 0) (never returns). */
void rl_crt_4bf710(int code)
{
    rl_crt_4bf770(code, 1, 0);
}

/* Port of FUN_004cafa0 (104 B) — siglookup: the _XcptActTab entry whose signal is sig (the first of them), else 0.
 * Quirk: the loop stops at the end and then tests the slot PAST the table (_First_FPE_Indx / _Num_FPE): sig 7 → the
 * address 0x4e5560. */
uint32_t rl_crt_4cafa0(int sig)
{
    uint32_t p = G_XCPTTAB;
    do {
        if (V32(p + 4) == sig) break;
        p += 12;
    } while (G_XCPTTAB + 12u * U32(G_XCPTCOUNT) > p);
    return V32(p + 4) == sig ? p : 0;
}

/* Port of FUN_004cad80 (533 B) — raise: SIGINT 2 / SIGILL 4 / SIGFPE 8 / SIGSEGV 11 / SIGTERM 15 / SIGBREAK 21 /
 * SIGABRT 22 (else -1). SIG_IGN → 0; SIG_DFL → _exit(3). Else the action is reset to SIG_DFL (SIGFPE: every FPE entry
 * of the table) and called — SIGFPE with (8, _fpecode = 0x8c), _pxcptinfoptrs cleared around ILL / FPE / SEGV and
 * restored with _fpecode after. Returns 0. */
int rl_crt_4cad80(int sig)
{
    uint32_t slot;
    switch (sig) {
    case 2: slot = G_SIGINT; break;
    case 4: case 8: case 11: slot = rl_crt_4cafa0(sig) + 8; break;
    case 15: slot = G_SIGTERM; break;
    case 21: slot = G_SIGBREAK; break;
    case 22: slot = G_SIGABRT; break;
    default: return -1;
    }
    uint32_t fn = U32(slot);
    if (fn == 1) return 0;
    if (fn == 0) rl_crt_4bf710(3);
    uint32_t keep_info = 0, keep_code = 0;
    if (sig == 8 || sig == 11 || sig == 4) {
        keep_info = U32(G_PXCPTINFO);
        U32(G_PXCPTINFO) = 0;
        if (sig == 8) {
            keep_code = U32(G_FPECODE);
            U32(G_FPECODE) = 0x8c;
        }
    }
    if (sig == 8) {
        for (int i = V32(G_FIRSTFPE); V32(G_NUMFPE) + V32(G_FIRSTFPE) > i; i++)
            U32(G_XCPTTAB + 8u + 12u * (uint32_t)i) = 0;
    } else
        U32(slot) = 0;
    if (sig == 8)
        ((void (__cdecl *)(int, int))(uintptr_t)fn)(8, V32(G_FPECODE));
    else
        ((void (__cdecl *)(int))(uintptr_t)fn)(sig);
    if (sig == 8 || sig == 11 || sig == 4) {
        U32(G_PXCPTINFO) = keep_info;
        if (sig == 8) U32(G_FPECODE) = keep_code;
    }
    return 0;
}

/* Port of FUN_004ccb00 (33 B) — abort: "abnormal program termination" (_NMSG_WRITE 10), raise(SIGABRT), _exit(3). */
void rl_crt_4ccb00(void)
{
    rl_crt_4ca330(10);
    rl_crt_4cad80(22);
    rl_crt_4bf710(3);
}

/* Port of FUN_004c8700 (140 B) — terminate: __pTerminate (when set), then abort. Retail calls the handler inside a
 * __try / __except(1) (a fault in it is swallowed, then abort); the port has no SEH frame — __pTerminate is 0 in
 * retail (nothing sets it), so the frame is never exercised. */
void rl_crt_4c8700(void)
{
    if (U32(G_PTERMINATE) != 0) ((void (__cdecl *)(void))(uintptr_t)U32(G_PTERMINATE))();
    rl_crt_4ccb00();
}

/* Port of FUN_004ccac0 (54 B) — _ValidateExecute: !IsBadCodePtr(fn) (1 / 0). */
int rl_crt_4ccac0(uint32_t fn)
{
    return IAT(BOOL (WINAPI *)(FARPROC), I_IsBadCodePtr)((FARPROC)(uintptr_t)fn) != 0 ? 0 : 1;
}

/* Port of FUN_004cc980 (145 B) — __CxxUnhandledExceptionFilter (__stdcall, 1 argument: retail's entry is `ret 4`): a
 * C++ exception (0xe06d7363, 3 parameters, magic 0x19930520) → terminate (never returns; EAX 1 after it); else the
 * previous filter (G_UEF) when it is valid code, else 0 (EXCEPTION_CONTINUE_SEARCH). */
int32_t rl_crt_4cc980(uint32_t ep)
{
    uint32_t rec = U32(ep);
    if (U32(rec) == 0xe06d7363u && U32(rec + 16) == 3 && U32(rec + 20) == 0x19930520u) {
        rl_crt_4c8700();
        return 1;
    }
    if (U32(G_UEF) != 0 && rl_crt_4ccac0(U32(G_UEF)) != 0)
        return ((LONG (WINAPI *)(uint32_t))(uintptr_t)U32(G_UEF))(ep);
    return 0;
}
