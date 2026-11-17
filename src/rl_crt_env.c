/* rl_crt_env.c — retail's statically linked MSVC 4.x DEBUG CRT: the locale / environment / wide-char / file paths (_isctype, __crtLCMapStringA, CPtoLCID / setSBCS, atol, wcstombs / wctomb, __crtsetenv / __wtomb_environ, _fcloseall / fclose, _commit, _chsize / _setmode, _validdrive).
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-env.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot.
 * Notes per function: docs/findings/crt-gap-env.md. */
#include "rl_crt_env.h"
#include "rl_crt.h"
#include "rl_crt_dbgheap.h"
#include "rl_crt_dbg.h"
#include "rl_io.h"   /* rl_4c05a0 = _free_dbg */

#include <string.h>
#include <windows.h>

#define V32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define U32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define U16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define U8(va)  (*(volatile uint8_t *)(uintptr_t)(va))
#define IAT(T, slot) ((T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot))
#define PTR(va) ((void *)(uintptr_t)(uint32_t)(va))

/* ---------------------------------------------------------------- retail's globals */
#define G_ERRNO      0x4e4530u   /* errno */
#define G_DOSERRNO   0x4e4534u   /* _doserrno */
#define G_ENVIRON    0x4e4558u   /* _environ */
#define G_INITENV    0x4e455cu   /* __initenv */
#define G_WENVIRON   0x4e4560u   /* _wenviron */
#define G_PCTYPE     0x4e4e38u   /* _pctype */
#define G_MBCURMAX   0x4e5044u   /* __mb_cur_max */
#define G_MBCTYPE    0x4e52d0u   /* _mbctype[257] (+1 = by character) */
#define G_MBCP       0x4e53d4u   /* __mbcodepage */
#define G_MBLCID     0x4e53d8u   /* __mblcid */
#define G_MBULINFO   0x4e53e0u   /* __mbulinfo: 6 words */
#define G_STRTYPE_F  0x4e5644u   /* aw_str.c's f_use (0 = not probed, 1 = W, 2 = A) */
#define G_LCCTYPE    0x4e5658u   /* __lc_handle[LC_CTYPE] */
#define G_LCCP       0x4e5668u   /* __lc_codepage */
#define G_LCMAP_F    0x4e564cu   /* aw_map.c's f_use */
#define G_CMPSTR_F   0x4e598cu   /* aw_cmp.c's f_use */
#define G_PIOB       0x5cdd24u   /* __piob */
#define G_NSTREAM    0x5ced30u   /* _nstream */
#define G_PIOINFO    0x5ced40u   /* __pioinfo[64] */
#define G_NHANDLE    0x5cee40u   /* _nhandle */

/* retail's .rdata strings */
#define S_STR_NULL   0x4d0b70u   /* "str != NULL" */
#define S_AW_STR_C   0x4d0fc4u   /* "aw_str.c" */
#define S_A_SPACE    0x4d0fd0u   /* the 1-char probe string the f_use tests pass (narrow) */
#define S_W_SPACE    0x4d0fd4u   /* … (wide) */
#define S_AW_MAP_C   0x4d0fd8u   /* "aw_map.c" */
#define S_WCSTOMBS_C 0x4d0fe4u   /* "wcstombs.c" */
#define S_PWCS_NULL  0x4d0ff0u   /* "pwcs != NULL" */
#define S_WTOMBENV_C 0x4d1014u   /* "wtombenv.c" */
#define S_AW_CMP_C   0x4d1038u   /* "aw_cmp.c" */
#define S_CCHCOUNT   0x4d1044u   /* "cchCount1==0 && cchCount2==1 || cchCount1==1 && cchCount2==0" */
#define S_SETENV_C   0x4d1084u   /* "setenv.c" */
#define S_FCLOSE_C   0x4d1090u   /* "fclose.c" */
#define S_FREEBUF_C  0x4d109cu   /* "_freebuf.c" */
#define S_STREAM_NUL 0x4d10a8u   /* "stream != NULL" */
#define S_CHSIZE_C   0x4d10b8u   /* "chsize.c" */
#define S_SIZE0      0x4d10c4u   /* "size >= 0" */

/* callees in other partitions (bare VAs: linked by the integrator) */

/* the IAT slots */
#define I_SetEndOfFile           0x5cf3f8u
#define I_LCMapStringW           0x5cf3fcu
#define I_LCMapStringA           0x5cf400u
#define I_GetCPInfo              0x5cf404u
#define I_GetLastError           0x5cf3e8u
#define I_GetDriveTypeA          0x5cf45cu
#define I_MultiByteToWideChar    0x5cf494u
#define I_FlushFileBuffers       0x5cf4a0u
#define I_SetEnvironmentVariableA 0x5cf4a4u
#define I_CompareStringW         0x5cf4b0u
#define I_CompareStringA         0x5cf4b4u
#define I_GetStringTypeW         0x5cf4bcu
#define I_GetStringTypeA         0x5cf4d0u
#define I_WideCharToMultiByte    0x5cf4e0u

typedef int (WINAPI *mb2wc_t)(UINT, DWORD, LPCSTR, int, LPWSTR, int);
typedef int (WINAPI *wc2mb_t)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL);
#define MB2WC IAT(mb2wc_t, I_MultiByteToWideChar)
#define WC2MB IAT(wc2mb_t, I_WideCharToMultiByte)

static void int3(void) { __asm__ volatile("int3"); }
#define REPORT(n_, ...) (rl_crt_dbgreport(__VA_ARGS__) == 1)   /* n_: the argument count */
#define FREE_CRT(p) rl_4c05a0((int)(p), 2)   /* _free_dbg(p, _CRT_BLOCK) */

static uint32_t pio(uint32_t fd) { return U32(G_PIOINFO + ((uint32_t)((int32_t)(fd & 0xffffffe0u) >> 3))) + 8 * (fd & 0x1f); }
#define OSFILE(fd) U8(pio(fd) + 4)
/* fd < _nhandle (unsigned) && FOPEN */
static int fd_open(uint32_t fd) { return fd < U32(G_NHANDLE) && (OSFILE(fd) & 1); }

/* ================================================================ ctype / mbctype */

/* Port of FUN_004c1c80 (105 B) — _validdrive: drive 0 (the current one) is valid; else GetDriveTypeA("X:\\") > 1
 * (not DRIVE_UNKNOWN / DRIVE_NO_ROOT_DIR). */
int rl_crt_4c1c80(uint32_t drive)
{
    if (drive == 0) return 1;
    char root[4] = { (char)(drive + 0x40), ':', '\\', 0 };
    UINT t = IAT(UINT (WINAPI *)(LPCSTR), I_GetDriveTypeA)(root);
    return !(t == 0 || t == 1);
}

/* Port of FUN_004c5710 (182 B) — _isctype: c in [-1, 255] from _pctype; else the DBCS pair (high byte a lead in
 * _pctype's _LEADBYTE 0x8000) or the low byte alone through __crtGetStringTypeA(CT_CTYPE1). */
int rl_crt_4c5710(int c, int type)
{
    if ((uint32_t)(c + 1) <= 0x100u) return (int)(U16(U32(G_PCTYPE) + 2u * (uint32_t)c) & (uint32_t)type);
    uint8_t s[3];
    int n;
    if (U16(U32(G_PCTYPE) + 2u * (uint8_t)(c >> 8)) & 0x8000) { s[0] = (uint8_t)(c >> 8); s[1] = (uint8_t)c; s[2] = 0; n = 2; }
    else { s[0] = (uint8_t)c; s[1] = 0; n = 1; }
    WORD ct[2] = { 0, 0 };
    if (rl_crt_4cb350(1, (uint32_t)(uintptr_t)s, n, (uint32_t)(uintptr_t)ct, 0, 0) == 0) return 0;
    return (int)(ct[0] & (uint32_t)type);
}

/* Port of FUN_004c1df0 (287 B) — atol: skip _SPACE, an optional sign, then _DIGITs (no overflow check; wraps). The
 * class test is _isctype when __mb_cur_max > 1, else _pctype directly. */
int rl_crt_4c1df0(uint32_t s)
{
#define CT(c, m) (V32(G_MBCURMAX) > 1 ? rl_crt_4c5710((c), (m)) : (int)(U16(U32(G_PCTYPE) + 2u * (uint32_t)(c)) & (m)))
    while (CT(U8(s), 8)) s++;
    int c = U8(s++);
    int sign = c;
    if (c == '-' || c == '+') c = U8(s++);
    int32_t v = 0;
    while (CT(c, 4)) {
        v = (int32_t)((uint32_t)c + 10u * (uint32_t)v - 0x30u);
        c = U8(s++);
    }
#undef CT
    return sign == '-' ? (int)(0u - (uint32_t)v) : v;
}

/* Port of FUN_004c8d50 (151 B) — CPtoLCID: the four DBCS code pages' LCIDs (932 → 0x411, 936 → 0x804, 949 → 0x412,
 * 950 → 0x404), else 0. */
int rl_crt_4c8d50(int cp)
{
    switch (cp) {
    case 932: return 0x411;
    case 936: return 0x804;
    case 949: return 0x412;
    case 950: return 0x404;
    default: return 0;
    }
}

/* Port of FUN_004c8df0 (120 B) — setSBCS: _mbctype[0..256] = 0, __mbcodepage = __mblcid = 0, __mbulinfo[0..5] = 0. */
void rl_crt_4c8df0(void)
{
    for (int i = 0; i < 257; i++) U8(G_MBCTYPE + (uint32_t)i) = 0;
    U32(G_MBCP) = 0;
    U32(G_MBLCID) = 0;
    for (int j = 0; j < 6; j++) U16(G_MBULINFO + 2u * (uint32_t)j) = 0;
}

/* Port of FUN_004ccb80 (103 B) — _mbsnbicoll: n == 0 → 0; else __crtCompareStringA(__mblcid, NORM_IGNORECASE, s1, n,
 * s2, n, __mbcodepage) - 2, or INT_MAX (_NLSCMPERROR) when that fails. */
int rl_crt_4ccb80(uint32_t s1, uint32_t s2, uint32_t n)
{
    if (n == 0) return 0;
    int r = rl_crt_4cd9a0(U32(G_MBLCID), 1, s1, (int)n, s2, (int)n, U32(G_MBCP));
    return r == 0 ? 0x7fffffff : r - 2;
}

/* Port of FUN_004ce7f0 (193 B) — strchr: the first (unsigned char)c, the terminator included, else NULL (retail scans
 * a dword at a time once aligned; the result is the byte scan's). */
uint32_t rl_crt_4ce7f0(uint32_t s, int c)
{
    uint8_t ch = (uint8_t)c;
    for (;; s++) {
        uint8_t b = U8(s);
        if (b == ch) return s;
        if (b == 0) return 0;
    }
}

/* Port of FUN_004ce550 (229 B) — _mbschr: __mbcodepage 0 → strchr; else a DBCS-aware scan: a lead byte with its
 * trail compared as (lead << 8 | trail), a lead before the terminator → NULL, the terminator matches c == 0. */
uint32_t rl_crt_4ce550(uint32_t s, uint32_t c)
{
    if (U32(G_MBCP) == 0) return rl_crt_4ce7f0(s, (int)c);
    uint16_t v;
    for (;; s++) {
        v = U8(s);
        if (v == 0) break;
        if (U8(G_MBCTYPE + 1 + (uint8_t)v) & 4) {
            s++;
            if (U8(s) == 0) return 0;
            if (((uint32_t)v << 8 | U8(s)) == c) return s - 1;
        } else if (v == c)
            break;
    }
    return v == c ? s : 0;
}

/* ================================================================ NLS wrappers (aw_str.c / aw_map.c / aw_cmp.c) */

/* Port of FUN_004cb350 (406 B) — __crtGetStringTypeA: the first call picks the A (2) or W (1) API (f_use 0x4e5644);
 * A: GetStringTypeA(locale or __lc_handle[LC_CTYPE], …) — its result; W: MultiByteToWideChar (code page or
 * __lc_codepage) into a _calloc_dbg'd buffer (aw_str.c 260), GetStringTypeW — its result, 0 on a failure. */
int rl_crt_4cb350(uint32_t type, uint32_t src, int n, uint32_t ctype, uint32_t cp, uint32_t lcid)
{
    if (U32(G_STRTYPE_F) == 0) {
        WORD dummy;
        if (IAT(BOOL (WINAPI *)(LCID, DWORD, LPCSTR, int, LPWORD), I_GetStringTypeA)(0, 1, PTR(S_A_SPACE), 1, &dummy))
            U32(G_STRTYPE_F) = 2;
        else if (IAT(BOOL (WINAPI *)(DWORD, LPCWSTR, int, LPWORD), I_GetStringTypeW)(1, PTR(S_W_SPACE), 1, &dummy))
            U32(G_STRTYPE_F) = 1;
        else
            return 0;
    }
    if (U32(G_STRTYPE_F) == 2) {
        if (lcid == 0) lcid = U32(G_LCCTYPE);
        return IAT(BOOL (WINAPI *)(LCID, DWORD, LPCSTR, int, LPWORD), I_GetStringTypeA)(lcid, type, PTR(src), n, PTR(ctype));
    }
    if (U32(G_STRTYPE_F) != 1) return 0;   /* (unreachable: f_use is 0 / 1 / 2) */
    int r = 0;
    uint32_t w = 0;
    if (cp == 0) cp = U32(G_LCCP);
    int nw = MB2WC(cp, 9, PTR(src), n, NULL, 0);
    if (nw == 0) goto done;
    w = rl_crt_4bfea0(2, (uint32_t)nw, 2, S_AW_STR_C, 260);
    if (w == 0) goto done;
    int k = MB2WC(cp, 1, PTR(src), n, PTR(w), nw);
    if (k == 0) goto done;
    r = IAT(BOOL (WINAPI *)(DWORD, LPCWSTR, int, LPWORD), I_GetStringTypeW)(type, PTR(w), k, PTR(ctype));
done:
    FREE_CRT(w);
    return r;
}

/* Port of FUN_004cbb80 (100 B) — strncnt (aw_map.c): the length up to n (n when no terminator before; reads the byte
 * at n). */
int rl_crt_4cbb80(uint32_t s, int n)
{
    int k = n;
    uint32_t p = s;
    while (k-- != 0 && U8(p) != 0) p++;
    return U8(p) != 0 ? n : (int)(p - s);
}

/* Port of FUN_004cb860 (791 B) — __crtLCMapStringA: f_use 0x4e564c as above; cch > 0 cut at the terminator (strncnt).
 * A: LCMapStringA — its result. W: the source widened (malloc aw_map.c 365), LCMapStringW sized; LCMAP_SORTKEY (0x400)
 * maps straight into the destination (bytes) when it is big enough; else into a second buffer (aw_map.c 401) and
 * WideCharToMultiByte(WC_COMPOSITECHECK | WC_SEPCHARS) back — the result its count; 0 on any failure. */
int rl_crt_4cb860(uint32_t lcid, uint32_t flags, uint32_t src, int n, uint32_t dst, int ndst, uint32_t cp)
{
    typedef int (WINAPI *lcmapa_t)(LCID, DWORD, LPCSTR, int, LPSTR, int);
    typedef int (WINAPI *lcmapw_t)(LCID, DWORD, LPCWSTR, int, LPWSTR, int);
    if (U32(G_LCMAP_F) == 0) {
        if (IAT(lcmapa_t, I_LCMapStringA)(0, 0x100, PTR(S_A_SPACE), 1, NULL, 0))
            U32(G_LCMAP_F) = 2;
        else if (IAT(lcmapw_t, I_LCMapStringW)(0, 0x100, PTR(S_W_SPACE), 1, NULL, 0))
            U32(G_LCMAP_F) = 1;
        else
            return 0;
    }
    if (n > 0) n = rl_crt_4cbb80(src, n);
    if (U32(G_LCMAP_F) == 2) return IAT(lcmapa_t, I_LCMapStringA)(lcid, flags, PTR(src), n, PTR(dst), ndst);
    if (U32(G_LCMAP_F) != 1) return 0;
    uint32_t w = 0, w2 = 0;
    int r;
    if (cp == 0) cp = U32(G_LCCP);
    int nw = MB2WC(cp, 9, PTR(src), n, NULL, 0);
    if (nw == 0) return 0;
    w = rl_crt_4bfa30(2u * (uint32_t)nw, 2, S_AW_MAP_C, 365);
    if (w == 0) return 0;
    if (MB2WC(cp, 1, PTR(src), n, PTR(w), nw) == 0) goto fail;
    r = IAT(lcmapw_t, I_LCMapStringW)(lcid, flags, PTR(w), nw, NULL, 0);
    if (r == 0) goto fail;
    if (flags & 0x400) {
        if (ndst != 0) {
            if (ndst < r) goto fail;
            if (IAT(lcmapw_t, I_LCMapStringW)(lcid, flags, PTR(w), nw, PTR(dst), ndst) == 0) goto fail;
        }
    } else {
        int nm = r;
        w2 = rl_crt_4bfa30(2u * (uint32_t)nm, 2, S_AW_MAP_C, 401);
        if (w2 == 0) goto fail;
        if (IAT(lcmapw_t, I_LCMapStringW)(lcid, flags, PTR(w), nw, PTR(w2), nm) == 0) goto fail;
        if (ndst == 0) r = WC2MB(cp, 0x220, PTR(w2), nm, NULL, 0, NULL, NULL);
        else r = WC2MB(cp, 0x220, PTR(w2), nm, PTR(dst), ndst, NULL, NULL);
        if (r == 0) goto fail;
    }
    FREE_CRT(w);
    FREE_CRT(w2);
    return r;
fail:
    FREE_CRT(w);
    FREE_CRT(w2);
    return 0;
}

/* Port of FUN_004cde00 (100 B) — strncnt (aw_cmp.c): as 0x4cbb80. */
int rl_crt_4cde00(uint32_t s, int n)
{
    int k = n;
    uint32_t p = s;
    while (k-- != 0 && U8(p) != 0) p++;
    return U8(p) != 0 ? n : (int)(p - s);
}

/* the W path's single-character case: a lead byte (CPINFO.LeadByte ranges) counts as 2 characters */
static int is_lead(const CPINFO *ci, uint8_t c)
{
    for (const BYTE *p = ci->LeadByte; p[0] != 0 && p[1] != 0; p += 2)
        if (!((int)p[0] > (int)c || (int)p[1] < (int)c)) return 1;
    return 0;
}

/* Port of FUN_004cd9a0 (1107 B) — __crtCompareStringA: f_use 0x4e598c; both counts cut at the terminator. A:
 * CompareStringA — its result. W: an empty side short-cuts (equal 2; the other longer than one byte: 1 / 3; a single
 * byte vs empty: GetCPInfo, a DBCS lead byte compares equal 2, else 1 / 3 — the debug assert for the impossible mix),
 * else both widened (malloc aw_cmp.c 394 / 409) and CompareStringW — its result (0 on a failure). */
int rl_crt_4cd9a0(uint32_t lcid, uint32_t flags, uint32_t s1, int n1, uint32_t s2, int n2, uint32_t cp)
{
    typedef int (WINAPI *cmpa_t)(LCID, DWORD, LPCSTR, int, LPCSTR, int);
    typedef int (WINAPI *cmpw_t)(LCID, DWORD, LPCWSTR, int, LPCWSTR, int);
    if (U32(G_CMPSTR_F) == 0) {
        if (IAT(cmpa_t, I_CompareStringA)(0, 0, PTR(S_A_SPACE), 1, PTR(S_A_SPACE), 1))
            U32(G_CMPSTR_F) = 2;
        else if (IAT(cmpw_t, I_CompareStringW)(0, 0, PTR(S_W_SPACE), 1, PTR(S_W_SPACE), 1))
            U32(G_CMPSTR_F) = 1;
        else
            return 0;
    }
    if (n1 > 0) n1 = rl_crt_4cde00(s1, n1);
    if (n2 > 0) n2 = rl_crt_4cde00(s2, n2);
    if (U32(G_CMPSTR_F) == 2) return IAT(cmpa_t, I_CompareStringA)(lcid, flags, PTR(s1), n1, PTR(s2), n2);
    if (U32(G_CMPSTR_F) != 1) return 0;
    int r = 0;
    uint32_t w1 = 0, w2 = 0;
    if (cp == 0) cp = U32(G_LCCP);
    if (n1 == 0 || n2 == 0) {
        if (n2 == n1) return 2;
        if (n2 > 1) return 1;
        if (n1 > 1) return 3;
        CPINFO ci;
        if (!IAT(BOOL (WINAPI *)(UINT, LPCPINFO), I_GetCPInfo)(cp, &ci)) return 0;
        if (!((n1 == 0 && n2 == 1) || (n1 == 1 && n2 == 0)) && REPORT(5, 2, S_AW_CMP_C, 354, 0, S_CCHCOUNT)) int3();
        if (n1 > 0) {
            if (ci.MaxCharSize < 2) return 3;
            return is_lead(&ci, U8(s1)) ? 2 : 3;
        }
        if (n2 > 0) {
            if (ci.MaxCharSize < 2) return 1;
            return is_lead(&ci, U8(s2)) ? 2 : 1;
        }
    }
    int nw1 = MB2WC(cp, 9, PTR(s1), n1, NULL, 0);
    if (nw1 == 0) return 0;
    w1 = rl_crt_4bfa30(2u * (uint32_t)nw1, 2, S_AW_CMP_C, 394);
    if (w1 == 0) return 0;
    if (MB2WC(cp, 1, PTR(s1), n1, PTR(w1), nw1) == 0) goto done;
    int nw2 = MB2WC(cp, 9, PTR(s2), n2, NULL, 0);
    if (nw2 == 0) goto done;
    w2 = rl_crt_4bfa30(2u * (uint32_t)nw2, 2, S_AW_CMP_C, 409);
    if (w2 == 0) goto done;
    if (MB2WC(cp, 1, PTR(s2), n2, PTR(w2), nw2) == 0) goto done;
    r = IAT(cmpw_t, I_CompareStringW)(lcid, flags, PTR(w1), nw1, PTR(w2), nw2);
done:
    FREE_CRT(w1);
    FREE_CRT(w2);
    return r;
}

/* ================================================================ wide characters */

/* Port of FUN_004ccb30 (66 B) — wcslen. */
uint32_t rl_crt_4ccb30(uint32_t s)
{
    uint32_t p = s;
    while (U16(p) != 0) p += 2;
    return (p - s) >> 1;
}

/* Port of FUN_004cbf50 (110 B) — wcsncnt: the count up to n including the terminator when one comes first, else n. */
uint32_t rl_crt_4cbf50(uint32_t s, uint32_t n)
{
    uint32_t k = n + 1, p = s;
    while (--k != 0 && U16(p) != 0) p += 2;
    if (k != 0 && U16(p) == 0) return ((p - s) >> 1) + 1;
    return n;
}

/* Port of FUN_004cc770 (203 B) — wctomb: NULL → 0; "C" locale (__lc_handle[LC_CTYPE] 0): <= 0xff stored, 1, else
 * EILSEQ (42) / -1; else WideCharToMultiByte(__lc_codepage, 0x220, &wc, 1, dst, __mb_cur_max) — a default char used
 * or a failure: EILSEQ / -1. */
int rl_crt_4cc770(uint32_t dst, uint32_t wc)
{
    if (dst == 0) return 0;
    uint16_t w = (uint16_t)wc;
    if (U32(G_LCCTYPE) == 0) {
        if (w > 0xff) { V32(G_ERRNO) = 42; return -1; }
        U8(dst) = (uint8_t)w;
        return 1;
    }
    BOOL used = FALSE;
    int n = WC2MB(U32(G_LCCP), 0x220, (LPCWSTR)&w, 1, PTR(dst), V32(G_MBCURMAX), NULL, &used);
    if (n == 0 || used) { V32(G_ERRNO) = 42; return -1; }
    return n;
}

/* Port of FUN_004cbbf0 (849 B) — wcstombs: dst with n 0 → 0; the NULL-source assert; "C" locale: byte copy (> 0xff
 * → EILSEQ / -1) up to the terminator / n; a locale: SBCS (__mb_cur_max 1) one WideCharToMultiByte of the counted
 * source (wcsncnt), DBCS the whole string, and on ERROR_INSUFFICIENT_BUFFER (122) character by character while it
 * fits; dst NULL: the length (wcslen, or the API's count - 1). */
uint32_t rl_crt_4cbbf0(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t got = 0;
    BOOL used = FALSE;
    if (dst != 0 && n == 0) return 0;
    if (src == 0 && REPORT(5, 2, S_WCSTOMBS_C, 122, 0, S_PWCS_NULL)) int3();
    if (dst == 0) {
        if (U32(G_LCCTYPE) == 0) return rl_crt_4ccb30(src);
        got = (uint32_t)WC2MB(U32(G_LCCP), 0x220, PTR(src), -1, NULL, 0, NULL, &used);
        if (got == 0 || used) { V32(G_ERRNO) = 42; return 0xffffffffu; }
        return got - 1;
    }
    if (U32(G_LCCTYPE) == 0) {
        while (got < n) {
            uint16_t w = U16(src);
            if (w > 0xff) { V32(G_ERRNO) = 42; return 0xffffffffu; }
            U8(dst + got) = (uint8_t)w;
            src += 2;
            if (w == 0) return got;
            got++;
        }
        return got;
    }
    if (V32(G_MBCURMAX) == 1) {
        if (n != 0) n = rl_crt_4cbf50(src, n);
        got = (uint32_t)WC2MB(U32(G_LCCP), 0x220, PTR(src), (int)n, PTR(dst), (int)n, NULL, &used);
        if (got == 0 || used) { V32(G_ERRNO) = 42; return 0xffffffffu; }
        if ((int8_t)U8(dst + got - 1) == 0) got--;
        return got;
    }
    got = (uint32_t)WC2MB(U32(G_LCCP), 0x220, PTR(src), -1, PTR(dst), (int)n, NULL, &used);
    if (got != 0 && !used) return got - 1;
    if (used || IAT(DWORD (WINAPI *)(void), I_GetLastError)() != 122) { V32(G_ERRNO) = 42; return 0xffffffffu; }
    while (got < n) {
        char mb[4];
        int k = WC2MB(U32(G_LCCP), 0, PTR(src), 1, mb, V32(G_MBCURMAX), NULL, &used);
        if (k == 0 || used) { V32(G_ERRNO) = 42; return 0xffffffffu; }
        if ((uint32_t)k + got > n) return got;
        for (int i = 0; k > i; i++, got++) {
            U8(dst + got) = (uint8_t)mb[i];
            if ((int8_t)U8(dst + got) == 0) return got;
        }
        src += 2;
    }
    return got;
}

/* ================================================================ the environment (setenv.c / wtombenv.c) */

/* Port of FUN_004ce280 (255 B) — copy_environ: NULL → NULL; else a _malloc_dbg'd copy of the array (setenv.c 326;
 * none → _amsg_exit(9)) with each string duplicated (setenv.c 335; a failed duplicate leaves NULL in its slot). */
uint32_t rl_crt_4ce280(uint32_t env)
{
    if (env == 0) return 0;
    int n = 0;
    for (uint32_t p = env; U32(p) != 0; p += 4) n++;
    uint32_t a = rl_crt_4bfa30(4u * (uint32_t)n + 4, 2, S_SETENV_C, 326), q = a;
    if (a == 0) rl_crt_4c35c0(9);
    for (uint32_t p = env; U32(p) != 0; p += 4, q += 4) {
        U32(q) = rl_crt_4bfa30(rl_crt_4bf620(U32(p)) + 1, 2, S_SETENV_C, 335);
        if (U32(q) != 0) rl_crt_4c2080(U32(q), U32(p));
    }
    U32(q) = 0;
    return a;
}

/* Port of FUN_004ce1e0 (155 B) — findenv: the index of the _environ entry whose first n bytes equal name (_mbsnbicoll:
 * case-insensitive) and continue with '=' or the terminator; else -(the entry count). */
int rl_crt_4ce1e0(uint32_t name, uint32_t n)
{
    uint32_t p = U32(G_ENVIRON);
    for (; U32(p) != 0; p += 4)
        if (rl_crt_4ccb80(name, U32(p), n) == 0 && (U8(U32(p) + n) == '=' || U8(U32(p) + n) == 0))
            return (int32_t)(p - U32(G_ENVIRON)) >> 2;
    return -((int32_t)(p - U32(G_ENVIRON)) >> 2);
}

/* Port of FUN_004cde70 (872 B) — __crtsetenv("NAME=value", primary): -1 for NULL / no '=' / an empty name. _environ
 * still __initenv → copied first (copy_environ). No _environ: primary with a _wenviron → __wtomb_environ (it calls back
 * here); else "NAME=" → 0, else empty _environ / _wenviron arrays (setenv.c 135 / 142). Found: "NAME=" removes it (the
 * string freed, the tail moved down, _realloc_dbg to fit — setenv.c 185), else the slot takes the string; not found:
 * "NAME=" → 0, else appended (_realloc_dbg setenv.c 206; -1 on failure). primary: SetEnvironmentVariableA on a copy
 * (setenv.c 229; value NULL for a removal). The string itself is kept in _environ (not copied). */
int rl_crt_4cde70(uint32_t str, int primary)
{
    if (str == 0) return -1;
    uint32_t eq = rl_crt_4ce550(str, '=');
    if (eq == 0 || str == eq) return -1;
    int remove = U8(eq + 1) == 0;
    if (U32(G_INITENV) == U32(G_ENVIRON)) U32(G_ENVIRON) = rl_crt_4ce280(U32(G_ENVIRON));
    if (U32(G_ENVIRON) == 0) {
        if (primary != 0 && U32(G_WENVIRON) != 0) {
            if (rl_crt_4ccbf0() != 0) return -1;
        } else {
            if (remove) return 0;
            if (U32(G_ENVIRON) == 0) {
                U32(G_ENVIRON) = rl_crt_4bfa30(4, 2, S_SETENV_C, 135);
                if (U32(G_ENVIRON) == 0) return -1;
                U32(U32(G_ENVIRON)) = 0;
            }
            if (U32(G_WENVIRON) == 0) {
                U32(G_WENVIRON) = rl_crt_4bfa30(4, 2, S_SETENV_C, 142);
                if (U32(G_WENVIRON) == 0) return -1;
                U32(U32(G_WENVIRON)) = 0;
            }
        }
    }
    uint32_t env = U32(G_ENVIRON);
    int ix = rl_crt_4ce1e0(str, eq - str);
    if (ix >= 0 && U32(env) != 0) {
        if (remove) {
            FREE_CRT(U32(env + 4u * (uint32_t)ix));
            while (U32(env + 4u * (uint32_t)ix) != 0) {
                U32(env + 4u * (uint32_t)ix) = U32(env + 4u * (uint32_t)ix + 4);
                ix++;
            }
            uint32_t r = rl_crt_4bff40(env, 4u * (uint32_t)ix, 2, S_SETENV_C, 185);
            if (r != 0) U32(G_ENVIRON) = r;
        } else
            U32(env + 4u * (uint32_t)ix) = str;
    } else {
        if (remove) return 0;
        if (ix < 0) ix = -ix;
        uint32_t r = rl_crt_4bff40(env, 4u * (uint32_t)ix + 8, 2, S_SETENV_C, 206);
        if (r == 0) return -1;
        U32(r + 4u * (uint32_t)ix) = str;
        U32(r + 4u * (uint32_t)ix + 4) = 0;
        U32(G_ENVIRON) = r;
    }
    if (primary != 0) {
        uint32_t name = rl_crt_4bfa30(rl_crt_4bf620(str) + 2, 2, S_SETENV_C, 229);
        if (name != 0) {
            rl_crt_4c2080(name, str);
            U8(name + (eq - str)) = 0;
            IAT(BOOL (WINAPI *)(LPCSTR, LPCSTR), I_SetEnvironmentVariableA)(PTR(name),
                                                                           remove ? NULL : PTR(name + (eq - str) + 1));
            FREE_CRT(name);
        }
    }
    return 0;
}

/* Port of FUN_004ccbf0 (205 B) — __wtomb_environ: each _wenviron string converted (WideCharToMultiByte code page 1 =
 * CP_OEMCP, as retail passes) into a _malloc_dbg'd buffer (wtombenv.c 61) and handed to __crtsetenv(s, 0); -1 on any
 * failure (the buffer leaks), else 0. */
int rl_crt_4ccbf0(void)
{
    for (uint32_t p = U32(G_WENVIRON); U32(p) != 0; p += 4) {
        int n = WC2MB(1, 0, PTR(U32(p)), -1, NULL, 0, NULL, NULL);
        if (n == 0) return -1;
        uint32_t s = rl_crt_4bfa30((uint32_t)n, 2, S_WTOMBENV_C, 61);
        if (s == 0) return -1;
        if (WC2MB(1, 0, PTR(U32(p)), -1, PTR(s), n, NULL, NULL) == 0) return -1;
        rl_crt_4cde70(s, 0);
    }
    return 0;
}

/* ================================================================ streams and fds */

/* Port of FUN_004ce750 (138 B) — _freebuf: the NULL assert; an open stream (_IOREAD | _IOWRT | _IORW) with _IOMYBUF:
 * the buffer freed (_CRT_BLOCK), _IOMYBUF | _IOSETVBUF cleared, _ptr = _base = NULL, _cnt = 0. */
void rl_crt_4ce750(uint32_t f)
{
    if (f == 0 && REPORT(5, 2, S_FREEBUF_C, 48, 0, S_STREAM_NUL)) int3();
    if ((U8(f + 12) & 0x83) && (U8(f + 12) & 8)) {
        FREE_CRT(U32(f + 8));
        U32(f + 12) &= 0xfffffbf7u;
        U32(f) = 0;
        U32(f + 8) = U32(f);
        U32(f + 4) = 0;
    }
}

/* Port of FUN_004ce380 (237 B) — fclose: a string stream (_IOSTRG): _flag 0, -1 (tested before the NULL assert —
 * retail reads the flag first); an open stream: _flush (its result), _freebuf, _close (< 0 → -1; else a _tmpfname
 * freed); _flag 0. */
int rl_crt_4ce380(uint32_t f)
{
    int r = -1;
    if (U8(f + 12) & 0x40) { U32(f + 12) = 0; return -1; }
    if (f == 0 && REPORT(5, 2, S_FCLOSE_C, 119, 0, S_STR_NULL)) int3();
    if (U8(f + 12) & 0x83) {
        r = rl_crt_4cd2f0(f);
        rl_crt_4ce750(f);
        if (rl_crt_4ce640(U32(f + 16)) < 0)
            r = -1;
        else if (U32(f + 28) != 0) {
            FREE_CRT(U32(f + 28));
            U32(f + 28) = 0;
        }
    }
    U32(f + 12) = 0;
    return r;
}

/* Port of FUN_004cd1b0 (187 B) — _fcloseall: every __piob stream from 3 up: an open one fclose'd (counted unless -1);
 * from 20 up (the heap-allocated _FILEX) freed and its slot cleared. Returns the count. */
int rl_crt_4cd1b0(void)
{
    int n = 0;
    for (int i = 3; V32(G_NSTREAM) > i; i++) {
        uint32_t slot = U32(G_PIOB) + 4u * (uint32_t)i;
        if (U32(slot) == 0) continue;
        if ((U8(U32(slot) + 12) & 0x83) && rl_crt_4ce380(U32(slot)) != -1) n++;
        if (i >= 20) {
            FREE_CRT(U32(slot));
            U32(slot) = 0;
        }
    }
    return n;
}

/* Port of FUN_004ce470 (222 B) — _commit: an open fd → FlushFileBuffers(_get_osfhandle) (0; a failure: _doserrno =
 * GetLastError, EBADF, -1); a bad fd: EBADF (_doserrno untouched), -1. */
int rl_crt_4ce470(uint32_t fd)
{
    if (!fd_open(fd)) { V32(G_ERRNO) = 9; return -1; }
    DWORD e;
    if (IAT(BOOL (WINAPI *)(HANDLE), I_FlushFileBuffers)((HANDLE)(uintptr_t)(uint32_t)rl_crt_4cd030(fd)))
        e = 0;
    else
        e = IAT(DWORD (WINAPI *)(void), I_GetLastError)();
    if (e == 0) return 0;
    V32(G_DOSERRNO) = (int32_t)e;
    V32(G_ERRNO) = 9;
    return -1;
}

/* Port of FUN_004cf9c0 (308 B) — _setmode: a bad fd → EBADF / -1; _O_BINARY (0x8000) clears FTEXT (0x80), _O_TEXT
 * (0x4000) sets it, anything else EINVAL (22) / -1; returns the previous mode. */
int rl_crt_4cf9c0(uint32_t fd, int mode)
{
    if (!fd_open(fd)) { V32(G_ERRNO) = 9; return -1; }
    int was = OSFILE(fd) & 0x80;
    if (mode == 0x8000)
        OSFILE(fd) &= 0x7f;
    else if (mode == 0x4000)
        OSFILE(fd) |= 0x80;
    else {
        V32(G_ERRNO) = 22;
        return -1;
    }
    return was ? 0x4000 : 0x8000;
}

/* Port of FUN_004cf640 (675 B) — _chsize: a bad fd → EBADF / -1; the size >= 0 assert; the position kept (_lseek CUR,
 * then END — either -1 → -1); growing: zeros written in 4 KB chunks in binary mode (the old mode put back; a failed
 * _write → -1, EACCES when _doserrno is ERROR_ACCESS_DENIED); shrinking: _lseek to size + SetEndOfFile (a failure:
 * EACCES, _doserrno = GetLastError, -1); the position restored. Retail's 4 KB buffer is an __alloca_probe'd local. */
int rl_crt_4cf640(uint32_t fd, int size)
{
    uint8_t buf[4096];
    int r = 0;
    if (!fd_open(fd)) { V32(G_ERRNO) = 9; return -1; }
    if (size < 0 && REPORT(5, 2, S_CHSIZE_C, 126, 0, S_SIZE0)) int3();
    int pos = rl_crt_4cc3b0(fd, 0, 1);
    if (pos == -1) return -1;
    int end = rl_crt_4cc3b0(fd, 0, 2);
    if (end == -1) return -1;
    int extend = size - end;
    if (extend > 0) {
        memset(buf, 0, sizeof buf);
        int was = rl_crt_4cf9c0(fd, 0x8000);
        for (;;) {
            int k = rl_crt_4cc0b0(fd, (uint32_t)(uintptr_t)buf, (uint32_t)(extend >= 0x1000 ? 0x1000 : extend));
            if (k == -1) {
                if (V32(G_DOSERRNO) == 5) V32(G_ERRNO) = 13;
                r = -1;
                break;
            }
            extend -= k;
            if (extend <= 0) break;
        }
        rl_crt_4cf9c0(fd, was);
    } else if (extend < 0) {
        rl_crt_4cc3b0(fd, size, 0);
        r = IAT(BOOL (WINAPI *)(HANDLE), I_SetEndOfFile)((HANDLE)(uintptr_t)(uint32_t)rl_crt_4cd030(fd)) ? 0 : -1;
        if (r == -1) {
            V32(G_ERRNO) = 13;
            V32(G_DOSERRNO) = (int32_t)IAT(DWORD (WINAPI *)(void), I_GetLastError)();
        }
    }
    rl_crt_4cc3b0(fd, pos, 0);
    return r;
}
