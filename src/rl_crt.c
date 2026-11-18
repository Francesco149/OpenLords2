/* rl_crt.c — retail's statically linked MSVC 4.x DEBUG C runtime, ported on retail's layout (docs/findings/crt-port.md).
 *
 * Lords2.exe links the debug CRT (dbgheap.c's block list and fill bytes, the 4.x page-based small-block heap, the
 * __pioinfo fd table, text-mode CRLF I/O, the mbctable, tzset / localtime, onexit). The corpus reaches 93 of its
 * functions (the 79 FLIRT library ones + _isindst 0x4c5dc0 + the xc initializer 0x4cc930; the multiplayer scenarios
 * add operator new, sprintf / __output, _splitpath, strncpy / _mbsnbcpy and the DirectPlayEnumerateA thunk); each is
 * ported here from
 * retail's code (IDA pseudocode + disassembly), named rl_crt_<va>. Its state is retail's own: the globals at their
 * VAs in .data / .bss (under rhost and the standalone retail's data image is mapped there), every OS call THROUGH
 * RETAIL'S IAT SLOT (rhost's virtual layers: the fixed-address heap arena, the file overlay, fake handles, the
 * virtual clock). So the port's allocations land where retail's do and the state is retail's byte for byte.
 * Callees the corpus never reaches (_CrtDbgReport, _CrtCheckMemory, _amsg_exit, _callnewh, the locale paths, _chsize,
 * _commit, _fcloseall, _realloc_dbg, __sbh_decommit_pages, atol / wcstombs on the TZ paths, _flsbuf, _wctomb …) are
 * ported in rl_crt_dbg.c / rl_crt_dbgheap.c / rl_crt_env.c (2026-11-17, verified by synthetic carriers: tests/probes/). */
#include "rl_crt.h"
#include "rl_crt_dbg.h"
#include "rl_crt_dbgheap.h"
#include "rl_crt_env.h"
#include "rl_main.h"
#include "rl_io.h"

#include <string.h>
#include <windows.h>

#define V32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define U32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define U16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define U8(va)  (*(volatile uint8_t *)(uintptr_t)(va))
#define P8(va)  ((uint8_t *)(uintptr_t)(uint32_t)(va))
/* a Win32 import through retail's IAT slot */
#define IAT(T, slot) ((T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot))

/* ---------------------------------------------------------------- retail's CRT globals */
#define G_ERRNO      0x4e4530u   /* errno */
#define G_DOSERRNO   0x4e4534u   /* _doserrno */
#define G_UMASK      0x4e4538u   /* _umaskval */
#define G_OSVER      0x4e453cu   /* _osver (the build) */
#define G_WINVER     0x4e4540u
#define G_WINMINOR   0x4e4544u
#define G_WINMAJOR   0x4e4548u
#define G_ARGC       0x4e454cu
#define G_ARGV       0x4e4550u
#define G_ENVIRON    0x4e4558u
#define G_WENVIRON   0x4e4560u
#define G_PGMPTR     0x4e4568u
#define G_EXITFLAG   0x4e4570u   /* byte */
#define G_TERMDONE   0x4e4574u   /* _C_Termination_Done */
#define G_EXITDONE   0x4e4578u   /* _C_Exit_Done */
#define G_LEAKSDONE  0x4e457cu
#define G_DBGFLAG    0x4e4580u   /* _crtDbgFlag (1 = _CRTDBG_ALLOC_MEM_DF) */
#define G_REQCURR    0x4e4584u   /* _lRequestCurr */
#define G_BREAKALLOC 0x4e4588u   /* _crtBreakAlloc (-1) */
#define G_NOMANSLAND 0x4e458cu   /* 0xfd */
#define G_CLEANLAND  0x4e4594u   /* 0xcd */
#define G_AENVPTR    0x4e45c0u
#define G_APPTYPE    0x4e45d0u   /* __app_type (2 = GUI) */
#define G_NEWMODE    0x4e45d4u
#define G_ALLOCHOOK  0x4e4610u   /* _pfnAllocHook (0x4c4070) */
#define G_SBH_HEAD   0x4e4618u   /* the first region header (0x814 B, static); [0] next, [1] prev, [2] the page
                                    rover, [3] the first uncommitted page, +16 the 1024 free-para counts, +1040 the
                                    1024 "largest failed request" bytes, [516] the region */
#define G_SBH_HEAD_REGION 0x4e4e28u /* the static header's [516] (its region): nonzero = in use, further headers from the heap */
#define G_SBH_ROVER  0x4e4e2cu   /* the region header the next search starts at */
#define G_SBH_FREEPG 0x4e4e30u   /* fully free pages (decommit at 32) */
#define G_SBH_THRESH 0x4e4e34u   /* __sbh_threshold (0x1e0) */
#define G_PCTYPE     0x4e4e38u
#define G_CTYPE1     0x4e4e42u   /* _ctype + 1 (words) */
#define G_MBCURMAX   0x4e5044u
#define G_ERRTAB     0x4e5050u   /* _dosmaperr's 45 (oserr, errno) pairs */
#define G_TIMEZONE   0x4e51c0u
#define G_DAYLIGHT   0x4e51c4u
#define G_DSTBIAS    0x4e51c8u
#define G_TZNAME0    0x4e5250u
#define G_TZNAME1    0x4e5254u
#define G_LASTTZ     0x4e5258u
#define G_DSTS_YR    0x4e5260u   /* dststart yr / yd / ms */
#define G_DSTS_YD    0x4e5264u
#define G_DSTS_MS    0x4e5268u
#define G_DSTE_YR    0x4e5270u   /* dstend yr / yd / ms */
#define G_DSTE_YD    0x4e5274u
#define G_DSTE_MS    0x4e5278u
#define G_TZFIRST    0x4e527cu
#define G_TB         0x4e5280u   /* gmtime's static struct tm */
#define G_MBCTYPE    0x4e52d0u   /* _mbctype[257] (+1 = by character) */
#define G_MBCP       0x4e53d4u   /* __mbcodepage */
#define G_MBLCID     0x4e53d8u
#define G_MBULINFO   0x4e53e0u   /* 6 words */
#define G_CPSYSSET   0x4e53ecu   /* fSystemSet */
#define G_MBMASKS    0x4e53f0u   /* the 4 range-type bits */
#define G_CPINFO     0x4e53f8u   /* __rgcode_page_info: 5 × 48 B */
#define G_ENVUSE     0x4e5578u   /* aw_env.c's f_use */
#define G_LCCTYPE    0x4e5658u   /* __lc_handle[LC_CTYPE] */
#define G_LCCP       0x4e5668u   /* __lc_codepage */
#define G_LPDAYS_M1  0x4e566cu   /* _lpdays - 1 */
#define G_LPDAYS     0x4e5670u
#define G_DAYS_M1    0x4e56a4u   /* _days - 1 */
#define G_DAYS       0x4e56a8u
#define G_IOB        0x4e56e0u   /* _iob[20] (32 B each) */
#define G_FMODE      0x4e5990u
#define G_PFIRST     0x4e5b20u   /* _pLastBlock (sic: the generated names) — see heap_alloc_dbg */
#define G_TOTALLOC   0x4e5b24u
#define G_PFIRSTBLK  0x4e5b28u   /* _pFirstBlock */
#define G_CURALLOC   0x4e5b2cu
#define G_MAXALLOC   0x4e5b30u
#define G_ASCBUF     0x4e5b38u   /* asctime's 26-byte buffer */
#define G_TDST       0x4e5b58u   /* time(): the dst flag of the cached minute */
#define G_TCACHE     0x4e5b60u   /* time(): the cached GetSystemTime (16 B) */
#define G_TZAPI      0x4e5b78u   /* tzapiused */
#define G_TZI        0x4e5b80u   /* the TIME_ZONE_INFORMATION (172 B) */
#define G_PGMNAME    0x4e5c30u   /* _pgmname[260] */
#define G_UEF        0x4e5d34u   /* the previous unhandled-exception filter */
#define G_PIOB       0x5cdd24u   /* __piob */
#define G_NSTREAM    0x5ced30u   /* _nstream */
#define G_PIOINFO    0x5ced40u   /* __pioinfo[64]: 32 ioinfo (8 B: osfhnd, osfile, pipech) per block */
#define G_NHANDLE    0x5cee40u   /* _nhandle */
#define G_CRTHEAP    0x5cee44u   /* _crtheap */
#define G_ACMDLN     0x5cee4cu
#define G_ONEXITEND  0x5cee54u
#define G_ONEXITBEG  0x5cee58u
#define G_FPINIT     0x5cee5cu

/* retail's .rdata strings / tables */
#define S_DBGHEAP_C  0x4d0204u   /* "dbgheap.c" */
#define S_CHECKMEM   0x4d0210u   /* "_CrtCheckMemory()" */
#define S_VALIDHEAP  0x4d0314u   /* "_CrtIsValidHeapPointer(pUserData)" */
#define S_BLOCKUSE   0x4d03f4u   /* "pHead->nBlockUse == nBlockUse" */
#define S_BLOCKTYPE  0x4d04acu   /* "_BLOCK_TYPE_IS_VALID(pHead->nBlockUse)" */
#define S_DBGDEL_CPP 0x4d0854u   /* "dbgdel.cpp" (operator delete's report) */
#define S_BADTYPE    0x4d014cu   /* "Error: memory allocation: bad memory block type.\n" */
#define S_BADSIZE    0x4d0180u   /* "Invalid allocation size: %u bytes.\n" */
#define S_PCT_S      0x4d01a4u   /* "%s" */
#define S_HOOKFAIL   0x4d01a8u   /* "Client hook allocation failure.\n" */
#define S_HOOKFAILAT 0x4d01ccu   /* "Client hook allocation failure at file %hs line %d.\n" */
#define S_MEMCHECK   0x4d04f0u   /* "memory check error at 0x%08X = 0x%02X, should be 0x%02X.\n" */
#define S_DAYNAMES   0x4d0ad8u   /* "SunMonTue…" */
#define S_MONNAMES   0x4d0af0u   /* "JanFeb…" */
#define S_TZSET_C    0x4d0b18u
#define S_TZ         0x4d0b20u   /* "TZ" */
#define S_STDENVP_C  0x4d0c88u
#define S_STDARGV_C  0x4d0c94u
#define S_AW_ENV_C   0x4d0ca0u
#define S_IOINIT_C   0x4d0cacu
#define S_FILE_C     0x4d100cu
#define S_OSFINFO_C  0x4d1020u
#define S_FORMATNULL 0x4d0860u   /* "format != NULL" */
#define S_SPRINTF_C  0x4d0870u   /* "sprintf.c" */
#define S_STRINGNULL 0x4d087cu   /* "string != NULL" */
#define S_WNULL      0x4e52a8u   /* -> L"(null)" (0x4d0b7c) */
#define S_NULL       0x4e52a4u   /* -> "(null)" (0x4d0b8c) */
#define T_LOOKUP     0x4d0b98u   /* output.c's __lookuptable: class (ch - ' ') in the low nibble, the next state
                                    (class * 8 + state) in the high nibble */
#define S_OUTPUT_C   0x4d0bf4u   /* "output.c" */
#define S_CH_NOT_EOS 0x4d0c00u   /* "ch != _T('\\0')" */
#define P_CFLTCVT    0x4e5970u   /* _cfltcvt_tab: _cfltcvt, _cropzeros, _fassign, _forcdecpt, _positive, _fpclass —
                                    retail links no floating point: all six are __fptrap 0x4cd4d0 (_amsg_exit(2)) */
#define P_CROPZEROS  0x4e5974u
#define P_FORCDECPT  0x4e597cu
#define S_ONEXIT_C   0x4d102cu
#define T_XC_A 0x4d2000u         /* the C++ initializers [0x4d2000, 0x4d2008) */
#define T_XC_Z 0x4d2008u
#define T_XI_A 0x4d200cu         /* the C initializers [0x4d200c, 0x4d2018) */
#define T_XI_Z 0x4d2018u
#define T_XP_A 0x4d201cu         /* the pre-terminators [0x4d201c, 0x4d2024) */
#define T_XP_Z 0x4d2024u
#define T_XT_A 0x4d2028u         /* the terminators [0x4d2028, 0x4d202c) */
#define T_XT_Z 0x4d202cu

/* retail functions by address: called through pointers retail stores (data-driven, as retail does) */
#define VA_CXX_RESTORE_UEF 0x4cca20u   /* __CxxRestoreUnhandledExceptionFilter */
#define VA_CXX_UEF         0x4cc980u   /* __CxxUnhandledExceptionFilter */

/* the IAT slots the CRT calls through */
#define I_CloseHandle            0x5cf3a0u
#define I_GetModuleFileNameA     0x5cf3bcu
#define I_GetModuleHandleA       0x5cf3c0u
#define I_GetLastError           0x5cf3e8u
#define I_GetCPInfo              0x5cf404u
#define I_VirtualAlloc           0x5cf408u
#define I_GetACP                 0x5cf40cu
#define I_HeapCreate             0x5cf410u
#define I_HeapDestroy            0x5cf414u
#define I_VirtualFree            0x5cf418u
#define I_HeapFree               0x5cf424u
#define I_GetStdHandle           0x5cf42cu
#define I_WriteFile              0x5cf430u
#define I_GetVersion             0x5cf434u
#define I_GetCommandLineA        0x5cf438u
#define I_HeapAlloc              0x5cf43cu
#define I_GetLocalTime           0x5cf444u
#define I_GetStartupInfoA        0x5cf448u
#define I_GetTimeZoneInformation 0x5cf44cu
#define I_DeleteFileA            0x5cf450u
#define I_GetSystemTime          0x5cf454u
#define I_SetCurrentDirectoryA   0x5cf458u
#define I_MoveFileA              0x5cf460u
#define I_GetCurrentDirectoryA   0x5cf464u
#define I_HeapValidate           0x5cf468u
#define I_GetFullPathNameA       0x5cf46cu
#define I_IsBadWritePtr          0x5cf470u
#define I_GetCurrentProcess      0x5cf474u
#define I_IsBadReadPtr           0x5cf478u
#define I_SetUnhandledExceptionFilter 0x5cf47cu
#define I_SetFilePointer         0x5cf480u
#define I_GetOEMCP               0x5cf484u
#define I_FreeEnvironmentStringsA 0x5cf48cu
#define I_GetEnvironmentStrings  0x5cf490u
#define I_CreateFileA            0x5cf49cu
#define I_SetEnvironmentVariableA 0x5cf4a4u
#define I_ReadFile               0x5cf4a8u
#define I_SetStdHandle           0x5cf4b8u
#define I_GetEnvironmentStringsW 0x5cf4c0u
#define I_FreeEnvironmentStringsW 0x5cf4c4u
#define I_ExitProcess            0x5cf4c8u
#define I_TerminateProcess       0x5cf4ccu
#define I_GetFileType            0x5cf4d8u
#define I_SetHandleCount         0x5cf4dcu
#define I_WideCharToMultiByte    0x5cf4e0u
#define I_DirectPlayEnumerateA   0x5cf368u   /* DPLAY ordinal 2 (rhost: rh_net's fake provider list) */

static DWORD last_error(void) { return IAT(DWORD (WINAPI *)(void), I_GetLastError)(); }
static void int3(void) { __asm__ volatile("int3"); }
/* _CrtDbgReport (unported: never reached — every report is an error path) */
#define REPORT(n_, ...) (rl_crt_dbgreport(__VA_ARGS__) == 1)   /* n_: the argument count (the old seam's) */
#define CHECKMEM() rl_crt_4c0cf0()

/* ================================================================ string / memory leaves */

/* Port of FUN_004bf620 (123 B) — strlen (the dword-at-a-time scan; exact). */
uint32_t rl_crt_4bf620(uint32_t s)
{
    const char *p = (const char *)(uintptr_t)s;
    return (uint32_t)strlen(p);
}

/* Port of FUN_004c2080 (7 B) — strcpy (strcat's copy loop): returns the destination. */
uint32_t rl_crt_4c2080(uint32_t d, uint32_t s)
{
    char *dp = (char *)(uintptr_t)d;
    const char *sp = (const char *)(uintptr_t)s;
    while ((*dp++ = *sp++) != 0) {}
    return d;
}

/* Port of FUN_004c2170 (132 B) — strcmp: unsigned bytes, -1 / 0 / +1. */
int rl_crt_4c2170(uint32_t a, uint32_t b)
{
    const uint8_t *p = P8(a), *q = P8(b);
    for (;; p++, q++) {
        if (*p != *q) return *p < *q ? -1 : 1;
        if (*p == 0) return 0;
    }
}

/* Port of FUN_004c2200 (88 B) — memset: returns the destination. */
uint32_t rl_crt_4c2200(uint32_t d, int c, uint32_t n)
{
    memset((void *)(uintptr_t)d, (uint8_t)c, n);
    return d;
}

/* ================================================================ the small-block heap (sbheap.c, 4.x) */
/* A region = 4 MB reserved (1024 pages of 4 KB; 64 KB committed at creation, more on demand). A page: [0] the
 * search start (an alloc-map pointer), [1] the free paragraphs from there, +8 the alloc map (240 bytes: per 16-byte
 * paragraph, the run length of an allocation at its first paragraph, 0 = free), +248 a 0xff sentinel, +256 the 240
 * paragraphs. A region header (0x814 B: the first is static at 0x4e4618) keeps per page the free-paragraph count
 * (+16, 0xff = uncommitted) and the smallest request that failed there (+1040). */
#define HDR32(h, i) U32((h) + 4u * (i))
#define HDR_FREE(h, i) U8((h) + 16u + (uint32_t)(i))
#define HDR_FAIL(h, i) U8((h) + 1040u + (uint32_t)(i))
#define HDR_REGION 516

/* Port of FUN_004c45b0 (513 B) — __sbh_new_region: a header (the static one while it is unused, else from the heap
 * when the static one's region slot [516] 0x4e4e28 is set), a 4 MB reservation with its first 64 KB committed and
 * zeroed, every page of those 16 initialized; the header linked into the ring. 0 = failure (everything released). */
uint32_t rl_crt_4c45b0(void)
{
    uint32_t mem;
    if (V32(G_SBH_HEAD_REGION) != 0) {
        mem = (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(HANDLE, DWORD, SIZE_T), I_HeapAlloc)(
            (HANDLE)(uintptr_t)U32(G_CRTHEAP), 0, 0x814);
        if (mem == 0) return 0;
    } else
        mem = G_SBH_HEAD;
    uint32_t addr = (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(LPVOID, SIZE_T, DWORD, DWORD), I_VirtualAlloc)(
        NULL, 0x400000, MEM_RESERVE, PAGE_READWRITE);
    if (addr != 0) {
        if (IAT(LPVOID (WINAPI *)(LPVOID, SIZE_T, DWORD, DWORD), I_VirtualAlloc)((LPVOID)(uintptr_t)addr, 0x10000,
                                                                                MEM_COMMIT, PAGE_READWRITE) != NULL) {
            if (mem == G_SBH_HEAD) {
                if (U32(G_SBH_HEAD) == 0) U32(G_SBH_HEAD) = G_SBH_HEAD;
                if (U32(G_SBH_HEAD + 4) == 0) U32(G_SBH_HEAD + 4) = G_SBH_HEAD;
            } else {
                HDR32(mem, 0) = G_SBH_HEAD;
                HDR32(mem, 1) = U32(G_SBH_HEAD + 4);
                U32(G_SBH_HEAD + 4) = mem;
                U32(HDR32(mem, 1)) = mem;
            }
            HDR32(mem, HDR_REGION) = addr;
            HDR32(mem, 2) = 0;
            HDR32(mem, 3) = 16;
            for (int i = 0; i < 1024; i++) {
                HDR_FREE(mem, i) = i >= 16 ? 0xff : 0xf0;
                HDR_FAIL(mem, i) = 0xf1;
            }
            rl_crt_4c2200(addr, 0, 0x10000);
            for (uint32_t pg = addr; HDR32(mem, HDR_REGION) + 0x10000 > pg; pg += 0x1000) {
                U32(pg) = pg + 8;
                U32(pg + 4) = 240;
                U8(pg + 248) = 0xff;
            }
            return mem;
        }
        IAT(BOOL (WINAPI *)(LPVOID, SIZE_T, DWORD), I_VirtualFree)((LPVOID)(uintptr_t)addr, 0, MEM_RELEASE);
    }
    if (mem != G_SBH_HEAD)
        IAT(BOOL (WINAPI *)(HANDLE, DWORD, LPVOID), I_HeapFree)((HANDLE)(uintptr_t)U32(G_CRTHEAP), 0, (LPVOID)(uintptr_t)mem);
    return 0;
}

/* Port of FUN_004c4480 (93 B) — _heap_init: _crtheap = HeapCreate(HEAP_NO_SERIALIZE, 4 KB, 0), then the first sbh
 * region (failure: HeapDestroy). 1 = done. */
int rl_crt_4c4480(void)
{
    U32(G_CRTHEAP) = (uint32_t)(uintptr_t)IAT(HANDLE (WINAPI *)(DWORD, SIZE_T, SIZE_T), I_HeapCreate)(1, 0x1000, 0);
    if (U32(G_CRTHEAP) == 0) return 0;
    if (rl_crt_4c45b0() != 0) return 1;
    IAT(BOOL (WINAPI *)(HANDLE), I_HeapDestroy)((HANDLE)(uintptr_t)U32(G_CRTHEAP));
    return 0;
}

/* Port of FUN_004c49d0 (162 B) — __sbh_find_block: the region holding `p` (strictly inside: region < p < region +
 * 4 MB) → *hdr, its page → *page, EAX = the alloc-map byte of p's paragraph; 0 = not a small block. */
uint32_t rl_crt_4c49d0(uint32_t p, uint32_t *hdr, uint32_t *page)
{
    uint32_t h = G_SBH_HEAD;
    do {
        uint32_t r = HDR32(h, HDR_REGION);
        if (r != 0 && r < p && r + 0x400000 > p) {
            *hdr = h;
            *page = p & 0xfffff000u;
            return (uint32_t)((int32_t)(p - ((p & 0xfffff000u) + 256)) >> 4) + (p & 0xfffff000u) + 8;
        }
        h = HDR32(h, 0);
    } while (h != G_SBH_HEAD);
    return 0;
}

/* Port of FUN_004c4a80 (136 B) — __sbh_free_block: the page's free count += the block's run, its map byte 0, the
 * page's failed-request byte reset (0xf1); a page that becomes wholly free counts toward the decommit (32 →
 * __sbh_decommit_pages(16), unported: never reached). */
void rl_crt_4c4a80(uint32_t hdr, uint32_t page, uint32_t map)
{
    int32_t i = (int32_t)(page - HDR32(hdr, HDR_REGION)) >> 12;
    HDR_FREE(hdr, i) = (uint8_t)(HDR_FREE(hdr, i) + U8(map));
    U8(map) = 0;
    HDR_FAIL(hdr, i) = 0xf1;
    if (HDR_FREE(hdr, i) == 240 && ++V32(G_SBH_FREEPG) == 32)
        rl_crt_4c4850(16);
}

/* Port of FUN_004c4fe0 (763 B) — __sbh_alloc_block_from_page: first fit of `paras` paragraphs in one page, from
 * the page's search start (its cached free run when it suffices), then wrapping from the map's start up to the
 * search start; `free` = the page's free paragraphs (a scan gives up once what is left cannot fit). EAX = the
 * block's address (page + 256 + 16 × map index), 0 = none. */
uint32_t rl_crt_4c4fe0(uint32_t page, uint32_t free, uint32_t paras)
{
    uint32_t start = U32(page), p = start, end = page + 248;
    uint32_t i, run;
    if (U32(page + 4) >= paras) {
        U8(p) = (uint8_t)paras;
        if (p + paras >= end) {
            U32(page) = page + 8;
            U32(page + 4) = 0;
        } else {
            U32(page) += paras;
            U32(page + 4) -= paras;
        }
        return page + 256 + 16 * (p - (page + 8));
    }
    if (U8(p + U32(page + 4)) != 0)
        p += U32(page + 4);
    while (p + paras < end) {
        if (U8(p) != 0) {
            p += U8(p);
            continue;
        }
        uint32_t q = p + 1;
        run = 1;
        while (U8(q) == 0) { q++; run++; }
        if (paras <= run) {
            if (p + paras >= end) {
                U32(page) = page + 8;
                U32(page + 4) = 0;
            } else {
                U32(page) = p + paras;
                U32(page + 4) = run - paras;
            }
            U8(p) = (uint8_t)paras;
            return page + 256 + 16 * (p - (page + 8));
        }
        if (start == p)
            U32(page + 4) = run;
        else {
            free -= run;
            if (paras > free) return 0;
        }
        p = q;
    }
    for (i = page + 8;;) {
        for (;;) {
            if (start <= i || i + paras > page + 247) return 0;
            if (U8(i) == 0) break;
            i += U8(i);
        }
        uint32_t q = i + 1;
        run = 1;
        while (U8(q) == 0) { q++; run++; }
        if (paras <= run) break;
        free -= run;
        if (paras > free) return 0;
        i = q;
    }
    if (i + paras >= end) {
        U32(page) = page + 8;
        U32(page + 4) = 0;
    } else {
        U32(page) = i + paras;
        U32(page + 4) = run - paras;
    }
    U8(i) = (uint8_t)paras;
    return page + 256 + 16 * (i - (page + 8));
}

/* Port of FUN_004c4b10 (1222 B) — __sbh_alloc_block: from the rover's header round the ring, every committed region's
 * pages from its page rover to the end, then from 0 to the rover (a page whose free count fits and whose failed
 * request is larger); a page that fails remembers the request. None: the first region with uncommitted pages commits
 * the next run of up to 16 (initializing each), else a new region; the block is taken at the first committed page's
 * map start. 0 = out of memory. */
uint32_t rl_crt_4c4b10(uint32_t paras)
{
    uint32_t h = U32(G_SBH_ROVER);
    do {
        uint32_t region = HDR32(h, HDR_REGION);
        if (region != 0) {
            for (int32_t i = (int32_t)HDR32(h, 2); i < 1024; i++) {
                if (HDR_FREE(h, i) >= paras && HDR_FREE(h, i) != 255 && HDR_FAIL(h, i) > paras) {
                    uint32_t b = rl_crt_4c4fe0(HDR32(h, HDR_REGION) + 4096u * (uint32_t)i, HDR_FREE(h, i), paras);
                    if (b != 0) {
                        U32(G_SBH_ROVER) = h;
                        HDR_FREE(h, i) = (uint8_t)(HDR_FREE(h, i) - paras);
                        HDR32(h, 2) = (uint32_t)i;
                        return b;
                    }
                    HDR_FAIL(h, i) = (uint8_t)paras;
                }
            }
            for (int32_t j = 0; (int32_t)HDR32(h, 2) > j; j++) {
                if (HDR_FREE(h, j) >= paras && HDR_FREE(h, j) != 255 && HDR_FAIL(h, j) > paras) {
                    uint32_t b = rl_crt_4c4fe0(HDR32(h, HDR_REGION) + 4096u * (uint32_t)j, HDR_FREE(h, j), paras);
                    if (b != 0) {
                        U32(G_SBH_ROVER) = h;
                        HDR_FREE(h, j) = (uint8_t)(HDR_FREE(h, j) - paras);
                        HDR32(h, 2) = (uint32_t)j;
                        return b;
                    }
                    HDR_FAIL(h, j) = (uint8_t)paras;
                }
            }
        }
        h = HDR32(h, 0);
    } while (h != U32(G_SBH_ROVER));
    h = G_SBH_HEAD;
    while (HDR32(h, HDR_REGION) == 0 || (int32_t)HDR32(h, 3) == -1) {
        h = HDR32(h, 0);
        if (h == G_SBH_HEAD) {
            uint32_t nh = rl_crt_4c45b0();
            if (nh == 0) return 0;
            uint32_t pg = HDR32(nh, HDR_REGION);
            U8(pg + 8) = (uint8_t)paras;
            U32(G_SBH_ROVER) = nh;
            U32(pg) = pg + paras + 8;
            U32(pg + 4) = 240 - paras;
            HDR_FREE(nh, 0) = (uint8_t)(HDR_FREE(nh, 0) - paras);
            return HDR32(nh, HDR_REGION) + 256;
        }
    }
    int32_t first = (int32_t)HDR32(h, 3);
    int32_t lim = first + 16 >= 1024 ? 1024 : first + 16;
    int32_t k;
    for (k = first + 1; lim > k && HDR_FREE(h, k) == 255; k++) {}
    uint32_t at = HDR32(h, HDR_REGION) + 4096u * (uint32_t)HDR32(h, 3);
    if (at != (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(LPVOID, SIZE_T, DWORD, DWORD), I_VirtualAlloc)(
                  (LPVOID)(uintptr_t)at, (uint32_t)(k - (int32_t)HDR32(h, 3)) << 12, MEM_COMMIT, PAGE_READWRITE))
        return 0;
    int32_t n = (int32_t)HDR32(h, 3);
    for (uint32_t pg = HDR32(h, HDR_REGION) + 4096u * (uint32_t)n; n < k; n++, pg += 4096) {
        U32(pg) = pg + 8;
        U32(pg + 4) = 240;
        U8(pg + 248) = 0xff;
        HDR_FREE(h, n) = 0xf0;
        HDR_FAIL(h, n) = 0xf1;
    }
    U32(G_SBH_ROVER) = h;
    while (k < 1024 && HDR_FREE(h, k) != 255) k++;
    uint32_t got = HDR32(h, 3);
    HDR32(h, 3) = k < 1024 ? (uint32_t)k : 0xffffffffu;
    uint32_t pg = HDR32(h, HDR_REGION) + 4096u * got;
    U8(pg + 8) = (uint8_t)paras;
    HDR32(h, 2) = got;
    HDR_FREE(h, got) = (uint8_t)(HDR_FREE(h, got) - paras);
    U32(pg) = pg + paras + 8;
    U32(pg + 4) -= paras;
    return HDR32(h, HDR_REGION) + 4096u * got + 256;
}

/* Port of FUN_004c3760 (99 B) — _heap_alloc_base: rounded to 16; up to __sbh_threshold from the small-block heap,
 * else (or when it fails) HeapAlloc on _crtheap. */
uint32_t rl_crt_4c3760(uint32_t size)
{
    uint32_t n = (size + 15) & 0xfffffff0u;
    uint32_t b;
    if (n <= U32(G_SBH_THRESH) && (b = rl_crt_4c4b10(n >> 4)) != 0)
        return b;
    return (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(HANDLE, DWORD, SIZE_T), I_HeapAlloc)(
        (HANDLE)(uintptr_t)U32(G_CRTHEAP), 0, n);
}

/* Port of FUN_004c4370 (105 B) — _free_base: a small block back to its page, anything else HeapFree. */
void rl_crt_4c4370(uint32_t p)
{
    if (p == 0) return;
    uint32_t hdr, page, map = rl_crt_4c49d0(p, &hdr, &page);
    if (map != 0)
        rl_crt_4c4a80(hdr, page, map);
    else
        IAT(BOOL (WINAPI *)(HANDLE, DWORD, LPVOID), I_HeapFree)((HANDLE)(uintptr_t)U32(G_CRTHEAP), 0, (LPVOID)(uintptr_t)p);
}

/* ================================================================ the debug heap (dbgheap.c) */

/* _pfnAllocHook as retail calls it. Retail's default (0x4c4070, never replaced by the game) is called as its port, not
 * by address: a call by address from port code enters that row's wrapper (rhost; the standalone's thunk), whose
 * rh_port_sync_in reloads the port modules' file-static mirrors from retail memory in the middle of the caller's
 * computation — a malloc inside the army-economy evaluator clobbered its live g_eval_revenue (0x5656f0: bot3s-s404-t36,
 * bot3cen-s207-t44). Any other hook: by address. */
int rl_crt_alloc_hook(uint32_t hook, int type, uint32_t data, uint32_t size, int use, int32_t req, uint32_t file, int line)
{
    if (hook == 0x4c4070u) return rl_crt_4c4070(type, data, (int)size, use, req, file, line);
    return ((int (__cdecl *)(int, uint32_t, uint32_t, int, int32_t, uint32_t, int))(uintptr_t)hook)(type, data, size, use,
                                                                                                   req, file, line);
}

/* Port of FUN_004c4070 (21 B) — the default allocation hook (_CrtDefaultAllocHook): 1 (allow). */
int rl_crt_4c4070(int type, uint32_t data, int size, int use, int req, uint32_t file, int line)
{
    (void)type; (void)data; (void)size; (void)use; (void)req; (void)file; (void)line;
    return 1;
}

/* Port of FUN_004bfb30 (818 B) — _heap_alloc_dbg: the _CRTDBG_CHECK_ALWAYS_DF check, the break-on-request int 3,
 * the client hook _pfnAllocHook(_HOOK_ALLOC, …) (retail's pointer, called as retail calls it), then a block of
 * size + 0x24 from _heap_alloc_base: the 0x20-byte header (next = the old first block, prev 0, file, line, size,
 * use, request) linked at the head of the block list (an _IGNORE_BLOCK when allocation tracking is off and the type
 * is not _CRT_BLOCK: no link, line 0xfedcbabc), _lTotalAlloc / _lCurAlloc / _lMaxAlloc, the no-man's-land 0xfd
 * before and after, the data filled 0xcd. EAX = the data, 0 = failure. */
uint32_t rl_crt_4bfb30(uint32_t size, int use, uint32_t file, int line)
{
    int ignore = 0;
    if ((V32(G_DBGFLAG) & 4) && CHECKMEM() == 0 && REPORT(5, 2, S_DBGHEAP_C, 0x141, 0, S_CHECKMEM))
        int3();
    int32_t req = V32(G_REQCURR);
    if (req == V32(G_BREAKALLOC))
        int3();
    if (rl_crt_alloc_hook(U32(G_ALLOCHOOK), 1, 0, size, use, req, file, line) == 0) {
        if (file != 0) {
            if (REPORT(7, 0, 0, 0, 0, S_HOOKFAILAT, file, line)) int3();
        } else if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_HOOKFAIL))
            int3();
        return 0;
    }
    if ((use & 0xffff) != 2 && (V32(G_DBGFLAG) & 1) == 0)
        ignore = 1;
    if (size > 0xffffffe0u || size + 36 > 0xffffffe0u) {
        if (REPORT(6, 1, 0, 0, 0, S_BADSIZE, size)) int3();
        return 0;
    }
    if ((use & 0xffff) != 4 && use != 1 && (use & 0xffff) != 2 && use != 3 &&
        REPORT(6, 1, 0, 0, 0, S_PCT_S, S_BADTYPE))
        int3();
    uint32_t b = rl_crt_4c3760(size + 36);
    if (b == 0) return 0;
    V32(G_REQCURR)++;
    volatile uint32_t *h = (volatile uint32_t *)(uintptr_t)b;
    if (ignore) {
        h[0] = 0;
        h[1] = 0;
        h[2] = 0;
        h[3] = 0xfedcbabcu;
        h[4] = size;
        h[5] = 3;
        h[6] = 0;
    } else {
        U32(G_TOTALLOC) += size;
        U32(G_CURALLOC) += size;
        if (U32(G_MAXALLOC) < U32(G_CURALLOC))
            U32(G_MAXALLOC) = U32(G_CURALLOC);
        if (U32(G_PFIRSTBLK) != 0)
            U32(U32(G_PFIRSTBLK) + 4) = b;
        else
            U32(G_PFIRST) = b;
        h[0] = U32(G_PFIRSTBLK);
        h[1] = 0;
        h[2] = file;
        h[3] = (uint32_t)line;
        h[4] = size;
        h[5] = (uint32_t)use;
        h[6] = (uint32_t)req;
        U32(G_PFIRSTBLK) = b;
    }
    rl_crt_4c2200(b + 28, U8(G_NOMANSLAND), 4);
    rl_crt_4c2200(b + size + 32, U8(G_NOMANSLAND), 4);
    rl_crt_4c2200(b + 32, U8(G_CLEANLAND), size);
    return b + 32;
}

/* Port of FUN_004bfa90 (101 B) — _nh_malloc_dbg: _heap_alloc_dbg, retried while `nh` and the new handler
 * (_callnewh, unported: never reached) succeeds. */
uint32_t rl_crt_4bfa90(uint32_t size, int nh, int use, uint32_t file, int line)
{
    for (;;) {
        uint32_t p = rl_crt_4bfb30(size, use, file, line);
        if (p != 0 || nh == 0) return p;
        if (rl_crt_4c3650(size) == 0) return 0;
    }
}

/* Port of FUN_004bfa00 (40 B) — malloc: _nh_malloc_dbg(size, _newmode, _NORMAL_BLOCK, NULL, 0). */
uint32_t rl_crt_4bfa00(uint32_t size) { return rl_crt_4bfa90(size, V32(G_NEWMODE), 1, 0, 0); }

/* Port of FUN_004bfa30 (46 B) — _malloc_dbg: _nh_malloc_dbg(size, _newmode, use, file, line). */
uint32_t rl_crt_4bfa30(uint32_t size, int use, uint32_t file, int line)
{
    return rl_crt_4bfa90(size, V32(G_NEWMODE), use, file, line);
}

/* Port of FUN_004bfea0 (110 B) — _calloc_dbg: n × sz from _malloc_dbg, zeroed byte by byte (the 0xcd fill
 * overwritten). */
uint32_t rl_crt_4bfea0(uint32_t n, uint32_t sz, int use, uint32_t file, int line)
{
    uint32_t total = n * sz;
    uint32_t p = rl_crt_4bfa30(total, use, file, line);
    if (p != 0)
        for (uint32_t q = p; p + total > q; q++) U8(q) = 0;
    return p;
}

/* Port of FUN_004c0c60 (140 B) — CheckBytes: 1 when all n bytes equal c (each mismatch reported, unreached). */
int rl_crt_4c0c60(uint32_t p, int c, int n)
{
    int ok = 1;
    while (n-- != 0) {
        uint8_t v = U8(p);
        p++;
        if (v != (uint8_t)c) {
            if (REPORT(8, 0, 0, 0, 0, S_MEMCHECK, p - 1, v, (uint8_t)c)) int3();
            ok = 0;
        }
    }
    return ok;
}

/* Port of FUN_004c1070 (48 B) — _CrtSetDbgFlag: the old flag; -1 = query only. */
int rl_crt_4c1070(int flag)
{
    int old = V32(G_DBGFLAG);
    if (flag != -1) V32(G_DBGFLAG) = flag;
    return old;
}

/* Port of FUN_004c1110 (92 B) — _CrtIsValidPointer: non-null, readable, and writable when asked (IsBadReadPtr /
 * IsBadWritePtr through retail's slots). */
int rl_crt_4c1110(uint32_t p, uint32_t n, int rw)
{
    return p != 0 && !IAT(BOOL (WINAPI *)(const void *, UINT_PTR), I_IsBadReadPtr)((const void *)(uintptr_t)p, n) &&
           (rw == 0 || !IAT(BOOL (WINAPI *)(LPVOID, UINT_PTR), I_IsBadWritePtr)((LPVOID)(uintptr_t)p, n));
}

/* Port of FUN_004c1170 (197 B) — _CrtIsValidHeapPointer: the header readable / writable, then a small block = its
 * map byte nonzero; else HeapValidate (skipped on Win32s: _osver & 0x8000). */
int rl_crt_4c1170(uint32_t p)
{
    if (p == 0) return 0;
    if (!rl_crt_4c1110(p - 32, 0x20, 1)) return 0;
    uint32_t hdr, page, map = rl_crt_4c49d0(p - 32, &hdr, &page);
    if (map != 0) return U8(map) != 0;
    if ((U8(G_OSVER + 1) & 0x80) == 0)
        return IAT(BOOL (WINAPI *)(HANDLE, DWORD, LPCVOID), I_HeapValidate)((HANDLE)(uintptr_t)U32(G_CRTHEAP), 0,
                                                                         (LPCVOID)(uintptr_t)(p - 32));
    return 1;
}

/* Port of FUN_004c09f0 (358 B) — _msize_dbg: the header checks (each an assert report, unreached), a _CRT_BLOCK
 * asked as _NORMAL_BLOCK is fine; EAX = the block's size. */
uint32_t rl_crt_4c09f0(uint32_t p, int use)
{
    if ((V32(G_DBGFLAG) & 4) && CHECKMEM() == 0 && REPORT(5, 2, S_DBGHEAP_C, 1148, 0, S_CHECKMEM))
        int3();
    if (rl_crt_4c1170(p) == 0 && REPORT(5, 2, S_DBGHEAP_C, 1157, 0, S_VALIDHEAP))
        int3();
    uint32_t h = p - 32;
    uint32_t t = U32(h + 20);
    if ((t & 0xffff) != 4 && t != 1 && (t & 0xffff) != 2 && t != 3 && REPORT(5, 2, S_DBGHEAP_C, 1163, 0, S_BLOCKTYPE))
        int3();
    if (U32(h + 20) == 2 && use == 1)
        use = 2;
    if (U32(h + 20) != 3 && U32(h + 20) != (uint32_t)use && REPORT(5, 2, S_DBGHEAP_C, 1170, 0, S_BLOCKUSE))
        int3();
    return U32(h + 16);
}

/* ================================================================ operator new / strncpy / sprintf / _splitpath
 * (the multiplayer paths: 0x4b7a9a's SIGS session object and "%s%s" path, 0x4b79e3's chat line) */

/* Port of FUN_004bfa60 (38 B) — _nh_malloc: _nh_malloc_dbg(size, nh, _NORMAL_BLOCK, no file, line 0). */
uint32_t rl_crt_4bfa60(uint32_t size, int nh)
{
    return rl_crt_4bfa90(size, nh, 1, 0, 0);
}

/* Port of FUN_004c3200 (30 B) — operator new (??2@YAPAXI@Z): _nh_malloc(size, 1) (the new handler on failure). */
uint32_t rl_crt_4c3200(uint32_t size)
{
    return rl_crt_4bfa60(size, 1);
}

/* Port of FUN_004c29d0 (162 B) — operator delete (dbgdel.cpp): NULL is a no-op; else the debug header's block use
 * (header = p − 0x20, +0x14) must be 1 / 2 / 3 or its low word 4 (an _IGNORE_BLOCK), else _CrtDbgReport(_CRT_ASSERT,
 * "dbgdel.cpp", 47, 0, "_BLOCK_TYPE_IS_VALID(pHead->nBlockUse)") (1 → int 3); then _free_dbg(p, that use). The MP
 * session's delete (0x4b92a0) reaches it (mp-skirmish's rejoin). */
void rl_crt_4c29d0(uint32_t p)
{
    if (p == 0)
        return;
    uint32_t use = U32(p - 0x20 + 0x14);
    if ((use & 0xffff) != 4 && use != 1 && (use & 0xffff) != 2 && use != 3 &&
        rl_crt_dbgreport(2, S_DBGDEL_CPP, 47, 0, S_BLOCKTYPE) == 1)
        __asm__ volatile("int3");
    rl_4c05a0((int)p, (int)use);
}

/* Port of FUN_004c28d0 (254 B) — strncpy: copies up to n bytes, stops after the terminator and zero-fills the rest of
 * the n (retail's dword loop writes exactly those bytes: the dwords it stores past the terminator are zeros inside n);
 * no terminator when the source is n bytes or longer. Returns the destination. */
uint32_t rl_crt_4c28d0(uint32_t d, uint32_t s, uint32_t n)
{
    uint8_t *dp = P8(d);
    const uint8_t *sp = P8(s);
    while (n != 0) {
        n--;
        if ((*dp++ = *sp++) == 0) break;
    }
    while (n != 0) { n--; *dp++ = 0; }
    return d;
}

/* Port of FUN_004c8870 (277 B) — _mbsnbcpy: with __mbcodepage 0 (SBCS: rhost's CP 1252) strncpy; else the DBCS copy
 * (a lead byte with no room for its trail byte, or a lead followed by NUL, is overwritten with NUL), then zero-fill of
 * the remaining count. */
uint32_t rl_crt_4c8870(uint32_t d, uint32_t s, uint32_t n)
{
    if (V32(G_MBCP) == 0) return rl_crt_4c28d0(d, s, n);
    uint8_t *dp = P8(d);
    const uint8_t *sp = P8(s);
    while (n != 0) {
        n--;
        if (U8(G_MBCTYPE + 1u + *sp) & 4) {
            *dp++ = *sp++;
            if (n == 0) { dp[-1] = 0; break; }
            n--;
            uint8_t *t = dp++;
            *t = *sp++;
            if (*t == 0) { dp[-2] = 0; break; }
        } else {
            uint8_t *t = dp++;
            *t = *sp++;
            if (*t == 0) break;
        }
    }
    while (n-- != 0) *dp++ = 0;
    return d;
}

/* Port of FUN_004c2fb0 (578 B) — _splitpath: drive ("X:" via _mbsnbcpy(2) + NUL), then one scan for the last '/' or
 * '\\' and the last '.' (a DBCS lead byte skips its trail — retail's ++ then the loop's ++: a lead byte right before the
 * terminator steps over it, kept), dir / fname / ext each copied by _mbsnbcpy capped at 255 bytes + NUL; a '.' before
 * the last separator is no extension. A NULL output is skipped. Returns retail's EAX (void in C, unused by the callers;
 * modelled for the shadow's return comparison: the last count / pointer the code path loaded). */
uint32_t rl_crt_4c2fb0(uint32_t path, uint32_t drive, uint32_t dir, uint32_t fname, uint32_t ext)
{
    uint32_t dot = 0, sep = 0, i, eax;
    if ((int8_t)U8(path + 1) == ':') {
        if (drive != 0) {
            rl_crt_4c8870(drive, path, 2);
            U8(drive + 2) = 0;
        }
        path += 2;
    } else if (drive != 0)
        U8(drive) = 0;
    for (i = path; (int8_t)U8(i) != 0; i++) {
        if (U8(G_MBCTYPE + 1u + U8(i)) & 4) i++;
        else if ((int8_t)U8(i) == '/' || (int8_t)U8(i) == '\\') sep = i + 1;
        else if ((int8_t)U8(i) == '.') dot = i;
    }
    if (sep != 0) {
        if (dir != 0) {
            uint32_t n = sep - path < 0xff ? sep - path : 0xff;
            rl_crt_4c8870(dir, path, n);
            U8(dir + n) = 0;
        }
        path = sep;
        eax = sep;
    } else if (dir != 0) {
        U8(dir) = 0;
        eax = dir;
    } else
        eax = 0;   /* the scan's last load: the terminator */
    if (dot != 0) eax = path;
    if (dot != 0 && dot >= path) {
        if (fname != 0) {
            uint32_t n = dot - path < 0xff ? dot - path : 0xff;
            rl_crt_4c8870(fname, path, n);
            U8(fname + n) = 0;
            eax = n;
        }
        if (ext != 0) {
            uint32_t n = i - dot < 0xff ? i - dot : 0xff;
            rl_crt_4c8870(ext, dot, n);
            U8(ext + n) = 0;
            eax = n;
        }
    } else {
        if (fname != 0) {
            uint32_t n = i - path < 0xff ? i - path : 0xff;
            rl_crt_4c8870(fname, path, n);
            U8(fname + n) = 0;
            eax = n;
        }
        if (ext != 0) { U8(ext) = 0; eax = ext; }
    }
    return eax;
}

/* ---- output.c: the printf engine. A stream is retail's FILE (+0 _ptr, +4 _cnt, +8 _base, +12 _flag); `pcount` points
 * at __output's character count (-1 = an output error: every writer stops). */

/* Port of FUN_004c7680 (117 B) — _write_char: putc (a full buffer → _flsbuf); EOF → *pcount = -1, else ++*pcount.
 * EAX (unused by retail's callers; returned for the shadow's comparison) = pcount. */
uint32_t rl_crt_4c7680(int ch, uint32_t f, uint32_t pcount)
{
    int r;
    if (--V32(f + 4) < 0)
        r = (int)rl_crt_4c6650((uint32_t)ch, f);
    else {
        U8(U32(f)) = (uint8_t)ch;
        r = U8(U32(f));
        U32(f) += 1;
    }
    if (r == -1) V32(pcount) = -1;
    else V32(pcount) += 1;
    return pcount;
}

/* Port of FUN_004c7700 (75 B) — _write_multi_char: n copies of ch (none for n <= 0), stopping at an error. EAX = the
 * count slot's value when the loop ends (<= 0), pcount after an error. */
uint32_t rl_crt_4c7700(int ch, int n, uint32_t f, uint32_t pcount)
{
    for (;;) {
        int k = n--;
        if (k <= 0) return (uint32_t)k;
        rl_crt_4c7680(ch, f, pcount);
        if (V32(pcount) == -1) return pcount;
    }
}

/* Port of FUN_004c7750 (87 B) — _write_string: n bytes of s (each sign-extended into _write_char, as retail's movsx),
 * stopping at an error. EAX as _write_multi_char's. */
uint32_t rl_crt_4c7750(uint32_t s, int n, uint32_t f, uint32_t pcount)
{
    for (;;) {
        int k = n--;
        if (k <= 0) return (uint32_t)k;
        rl_crt_4c7680((int8_t)U8(s), f, pcount);
        s++;
        if (V32(pcount) == -1) return pcount;
    }
}

/* Port of FUN_004c77b0 (30 B) — _get_int_arg: *pargp += 4, the dword before it. */
int rl_crt_4c77b0(uint32_t pargp)
{
    U32(pargp) += 4;
    return V32(U32(pargp) - 4);
}
/* 0x4c77d0 _get_int64_arg / 0x4c7800 _get_short_arg inlined (no row: only %I64 / %C reach them): the qword / the low
 * word, advancing 8 / 4 */
static int64_t get_int64_arg(uint32_t *argp) { *argp += 8; return *(volatile int64_t *)(uintptr_t)(*argp - 8); }
static uint16_t get_short_arg(uint32_t *argp) { *argp += 4; return U16(*argp - 4); }

/* Port of FUN_004c68f0 (3459 B) — __output: the printf state machine of the MSVC 4.x debug CRT (output.c).
 * Per format byte (signed char; NUL or a negative count ends): class = __lookuptable[ch - ' '] & 15 for ' '..'x' else 0,
 * state = __lookuptable[class * 8 + state] >> 4 (0 normal, 1 percent, 2 flag, 3 width, 4 dot, 5 precision, 6 size,
 * 7 type). Normal: a _pctype lead byte (_LEADBYTE 0x8000) writes its trail byte too (a NUL trail asserts, then is
 * written). Flags ' ' 2, '+' 1, '-' 4, '0' 8, '#' 0x80; size 'h' 0x20, 'l' 0x10, 'w' 0x800, "I64" 0x8000 (an 'I'
 * without "64" goes back to the normal state and is written); '*' width < 0 = '-' and |width|, '*' precision < 0 = none.
 * Types: c / C (wide: _wctomb), s / S (precision-limited, "(null)" / L"(null)"), Z (ANSI_STRING / UNICODE_STRING),
 * d i (signed 0x40) u o (base 8; '#' 0x200 forces a leading 0) x X p (precision 8, upper case) with the "0x" / "0X"
 * prefix under '#' (dropped for a zero value), h → (short) / (ushort), I64 → a qword, n (stores the count; no output),
 * e E f g G (through the _cfltcvt pointer table — retail's points at __fptrap: no floating point is linked).
 * A precision turns the '0' flag off; the number is built backwards in a 512-byte buffer (retail's frame: a precision
 * past 510 overruns retail's locals — not modelled). Then sign / prefix, width padding (spaces unless '-' or '0'),
 * zeros, the text (a wide text through _wctomb per character), trailing spaces for '-'. Returns the count. */
int rl_crt_4c68f0(uint32_t f, uint32_t fmt, uint32_t argp)
{
    int32_t count = 0;
    uint32_t pcount = (uint32_t)(uintptr_t)&count, pargp = (uint32_t)(uintptr_t)&argp;
    int state = 0, cls;
    uint32_t flags = 0;                   /* var_4 */
    int capexp = 0;                       /* var_8 */
    int prefixlen = 0;                    /* var_10 */
    int textiswide = 0;                   /* var_1C */
    int textlen = 0;                      /* var_24 */
    int precision = -1;                   /* var_234 */
    int hexadd = 0;                       /* var_230 */
    int radix = 0;                        /* var_238 */
    int no_output = 0;                    /* var_23C */
    int width = 0;                        /* var_244 */
    uint8_t prefix[4] = { 0 };            /* var_240 */
    uint8_t buf[512];                     /* MbCh: ebp-228h .. ebp-29h */
    uint8_t *const bufend = buf + 511;    /* ebp-29h */
    uint8_t *text = NULL;                 /* Str */
    uint8_t ch;
    for (;;) {
        ch = U8(fmt);
        fmt++;
        if ((int8_t)ch == 0 || count < 0) return count;
        cls = ((int8_t)ch >= ' ' && (int8_t)ch <= 'x') ? (int8_t)U8(T_LOOKUP + (uint32_t)((int8_t)ch - ' ')) & 15 : 0;
        state = (int8_t)U8(T_LOOKUP + (uint32_t)(cls * 8 + state)) >> 4;
        switch (state) {
        case 0:
        normal_state:
            textiswide = 0;
            if (U16(U32(G_PCTYPE) + 2u * ch) & 0x8000) {
                rl_crt_4c7680((int8_t)ch, f, pcount);
                ch = U8(fmt);
                fmt++;
                if ((int8_t)ch == 0 && REPORT(5, 2, S_OUTPUT_C, 389, 0, S_CH_NOT_EOS)) int3();
            }
            rl_crt_4c7680((int8_t)ch, f, pcount);
            break;
        case 1:
            capexp = 0;
            no_output = 0;
            width = 0;
            prefixlen = 0;
            flags = 0;
            precision = -1;
            textiswide = 0;
            break;
        case 2:
            switch ((int8_t)ch) {
            case ' ': flags |= 2; break;
            case '#': flags |= 0x80; break;
            case '+': flags |= 1; break;
            case '-': flags |= 4; break;
            case '0': flags |= 8; break;
            }
            break;
        case 3:
            if ((int8_t)ch == '*') {
                width = rl_crt_4c77b0(pargp);
                if (width < 0) { flags |= 4; width = -width; }
            } else
                width = (int8_t)ch + width * 10 - '0';
            break;
        case 4:
            precision = 0;
            break;
        case 5:
            if ((int8_t)ch == '*') {
                precision = rl_crt_4c77b0(pargp);
                if (precision < 0) precision = -1;
            } else
                precision = (int8_t)ch + precision * 10 - '0';
            break;
        case 6:
            switch ((int8_t)ch) {
            case 'I':
                if ((int8_t)U8(fmt) != '6' || (int8_t)U8(fmt + 1) != '4') {
                    state = 0;
                    goto normal_state;
                }
                fmt += 2;
                flags |= 0x8000;
                break;
            case 'h': flags |= 0x20; break;
            case 'l': flags |= 0x10; break;
            case 'w': flags |= 0x800; break;
            }
            break;
        case 7:
            switch ((int8_t)ch) {
            case 'C':
                if ((flags & 0x830) == 0) flags |= 0x800;
                /* fall through */
            case 'c':
                if (flags & 0x810) {
                    uint16_t wch = get_short_arg(&argp);
                    textlen = (int)rl_crt_4cc770((uint32_t)(uintptr_t)buf, (uint32_t)wch);
                    if (textlen < 0) no_output = 1;
                } else {
                    buf[0] = (uint8_t)rl_crt_4c77b0(pargp);
                    textlen = 1;
                }
                text = buf;
                break;
            case 'Z': {
                uint32_t a = (uint32_t)rl_crt_4c77b0(pargp);
                if (a == 0 || U32(a + 4) == 0) {
                    text = P8(U32(S_NULL));
                    textlen = (int)rl_crt_4bf620(U32(S_NULL));
                } else if (flags & 0x800) {
                    text = P8(U32(a + 4));
                    textlen = (int)((uint32_t)(int32_t)(int16_t)U16(a) >> 1);
                    textiswide = 1;
                } else {
                    textiswide = 0;
                    text = P8(U32(a + 4));
                    textlen = (int16_t)U16(a);
                }
                break;
            }
            case 'S':
                if ((flags & 0x830) == 0) flags |= 0x800;
                /* fall through */
            case 's': {
                int left = precision == -1 ? 0x7fffffff : precision;
                uint32_t s = (uint32_t)rl_crt_4c77b0(pargp), q;
                if (flags & 0x810) {
                    if (s == 0) s = U32(S_WNULL);
                    textiswide = 1;
                    for (q = s; left-- != 0 && U16(q) != 0; q += 2) {}
                    textlen = (int)(q - s) >> 1;
                } else {
                    if (s == 0) s = U32(S_NULL);
                    for (q = s; left-- != 0 && (int8_t)U8(q) != 0; q++) {}
                    textlen = (int)(q - s);
                }
                text = P8(s);
                break;
            }
            case 'n': {
                uint32_t p = (uint32_t)rl_crt_4c77b0(pargp);
                if (flags & 0x20) U16(p) = (uint16_t)count;
                else V32(p) = count;
                no_output = 1;
                break;
            }
            case 'E':
            case 'G':
                capexp = 1;
                ch = (uint8_t)((int8_t)ch + 32);
                /* fall through */
            case 'e':
            case 'f':
            case 'g': {
                flags |= 0x40;
                text = buf;
                if (precision < 0) precision = 6;
                else if (precision == 0 && (int8_t)ch == 'g') precision = 1;
                argp += 8;
                uint32_t dbl[2] = { U32(argp - 8), U32(argp - 4) };   /* var_264 / var_260 */
                ((void (__cdecl *)(uint32_t, uint32_t, int, int, int))(uintptr_t)U32(P_CFLTCVT))(
                    (uint32_t)(uintptr_t)dbl, (uint32_t)(uintptr_t)text, (int8_t)ch, precision, capexp);
                if ((flags & 0x80) && precision == 0)
                    ((void (__cdecl *)(uint32_t))(uintptr_t)U32(P_FORCDECPT))((uint32_t)(uintptr_t)text);
                if ((int8_t)ch == 'g' && !(flags & 0x80))
                    ((void (__cdecl *)(uint32_t))(uintptr_t)U32(P_CROPZEROS))((uint32_t)(uintptr_t)text);
                if ((int8_t)*text == '-') { flags |= 0x100; text++; }
                textlen = (int)rl_crt_4bf620((uint32_t)(uintptr_t)text);
                break;
            }
            case 'd':
            case 'i':
                flags |= 0x40;
                radix = 10;
                goto common_int;
            case 'u':
                radix = 10;
                goto common_int;
            case 'o':
                radix = 8;
                if (flags & 0x80) flags |= 0x200;
                goto common_int;
            case 'p':
                precision = 8;
                /* fall through */
            case 'X':
                hexadd = 7;
                goto common_hex;
            case 'x':
                hexadd = 39;
            common_hex:
                radix = 16;
                if (flags & 0x80) {
                    prefix[0] = '0';
                    prefix[1] = (uint8_t)(hexadd + 0x51);
                    prefixlen = 2;
                }
            common_int: {
                int64_t v;
                uint64_t u;
                if (flags & 0x8000) v = get_int64_arg(&argp);
                else if (flags & 0x20) {
                    if (flags & 0x40) v = (int16_t)rl_crt_4c77b0(pargp);
                    else v = (uint16_t)rl_crt_4c77b0(pargp);
                } else if (flags & 0x40) v = (int32_t)rl_crt_4c77b0(pargp);
                else v = (uint32_t)rl_crt_4c77b0(pargp);
                if ((flags & 0x40) && v < 0) { u = (uint64_t)0 - (uint64_t)v; flags |= 0x100; }
                else u = (uint64_t)v;
                if (!(flags & 0x8000)) u &= 0xffffffffu;
                if (precision < 0) precision = 1;
                else flags &= ~8u;
                if (u == 0) prefixlen = 0;
                text = bufend;
                for (;;) {
                    int p = precision--;
                    if (p <= 0 && u == 0) break;
                    int dig = (int)(u % (uint64_t)(int64_t)radix) + '0';   /* __aullrem / __aulldiv */
                    u /= (uint64_t)(int64_t)radix;
                    if (dig > '9') dig += hexadd;
                    *text-- = (uint8_t)dig;
                }
                textlen = (int)(bufend - text);
                text++;
                if ((flags & 0x200) && ((int8_t)*text != '0' || textlen == 0)) {
                    *--text = '0';
                    textlen++;
                }
                break;
            }
            }
            if (no_output == 0) {
                if (flags & 0x40) {
                    if (flags & 0x100) { prefix[0] = '-'; prefixlen = 1; }
                    else if (flags & 1) { prefix[0] = '+'; prefixlen = 1; }
                    else if (flags & 2) { prefix[0] = ' '; prefixlen = 1; }
                }
                int pad = width - textlen - prefixlen;
                if ((flags & 0xc) == 0) rl_crt_4c7700(' ', pad, f, pcount);
                rl_crt_4c7750((uint32_t)(uintptr_t)prefix, prefixlen, f, pcount);
                if ((flags & 8) && !(flags & 4)) rl_crt_4c7700('0', pad, f, pcount);
                if (textiswide && textlen > 0) {
                    uint8_t mb[4];   /* var_288 */
                    uint32_t w = (uint32_t)(uintptr_t)text;
                    for (int left = textlen; left-- != 0; ) {
                        uint16_t wc = U16(w);
                        w += 2;
                        int n = (int)rl_crt_4cc770((uint32_t)(uintptr_t)mb, (uint32_t)wc);
                        if (n <= 0) break;
                        rl_crt_4c7750((uint32_t)(uintptr_t)mb, n, f, pcount);
                    }
                } else
                    rl_crt_4c7750((uint32_t)(uintptr_t)text, textlen, f, pcount);
                if (flags & 4) rl_crt_4c7700(' ', pad, f, pcount);
            }
            break;
        }
    }
}

/* Port of FUN_004c2a80 (236 B) — sprintf: a string FILE (_flag 0x42 _IOWRT | _IOSTRG, _base = _ptr = the buffer, _cnt
 * 0x7fffffff) → __output, then the terminator (putc 0). `argp` = the address of the first variadic argument (retail:
 * &format + 4). Asserts a NULL buffer / format (_CrtDbgReport, sprintf.c 93 / 94). Returns __output's count. */
int rl_crt_4c2a80(uint32_t buf, uint32_t fmt, uint32_t argp)
{
    uint32_t file[8] = { 0 };   /* retail's FILE local: _ptr, _cnt, _base, _flag, … (the rest unread) */
    uint32_t f = (uint32_t)(uintptr_t)file;
    if (buf == 0 && REPORT(5, 2, S_SPRINTF_C, 93, 0, S_STRINGNULL)) int3();
    if (fmt == 0 && REPORT(5, 2, S_SPRINTF_C, 94, 0, S_FORMATNULL)) int3();
    file[3] = 0x42;
    file[2] = buf;
    file[0] = file[2];
    file[1] = 0x7fffffff;
    int r = rl_crt_4c68f0(f, fmt, argp);
    if (--V32(f + 4) < 0)
        rl_crt_4c6650(0, f);
    else {
        U8(U32(f)) = 0;
        U32(f) += 1;
    }
    return r;
}

/* Port of FUN_004bf60e (6 B) — DirectPlayEnumerateA's import thunk (jmp [0x5cf368]): the call through retail's IAT slot
 * (__stdcall: callback, context). */
int rl_crt_4bf60e(uint32_t cb, uint32_t ctx)
{
    return (int)IAT(HRESULT (WINAPI *)(void *, void *), I_DirectPlayEnumerateA)((void *)(uintptr_t)cb,
                                                                                   (void *)(uintptr_t)ctx);
}

/* ================================================================ low-level I/O */
/* the ioinfo of fd: [0] the OS handle, [4] osfile (FOPEN 1, FEOFLAG 2, FCRLF 4, FPIPE 8, FAPPEND 0x20, FDEV 0x40,
 * FTEXT 0x80), [5] the pipe lookahead (10 = none) */
static uint32_t pio(uint32_t fd) { return U32(G_PIOINFO + ((uint32_t)((int32_t)(fd & 0xffffffe0u) >> 3))) + 8 * (fd & 0x1f); }
#define OSFHND(fd) U32(pio(fd))
#define OSFILE(fd) U8(pio(fd) + 4)
#define PIPECH(fd) U8(pio(fd) + 5)
static void ebadf(void) { V32(G_ERRNO) = 9; V32(G_DOSERRNO) = 0; }
static void init_block(uint32_t b)
{
    for (uint32_t q = b; b + 256 > q; q += 8) {
        U8(q + 4) = 0;
        U32(q) = 0xffffffffu;
        U8(q + 5) = 10;
    }
}

/* Port of FUN_004c5930 (177 B) — _dosmaperr: _doserrno = the OS error; errno from the 45-pair table, else EACCES
 * for 19..36, ENOEXEC for 188..202, EINVAL otherwise. */
void rl_crt_4c5930(uint32_t oserr)
{
    V32(G_DOSERRNO) = (int32_t)oserr;
    for (uint32_t i = 0; i < 0x2d; i++)
        if (U32(G_ERRTAB + 8 * i) == oserr) {
            V32(G_ERRNO) = V32(G_ERRTAB + 4 + 8 * i);
            return;
        }
    if (oserr < 0x13 || oserr > 0x24)
        V32(G_ERRNO) = (oserr < 0xbc || oserr > 0xca) ? 22 : 8;
    else
        V32(G_ERRNO) = 13;
}

/* Port of FUN_004c9e50 (808 B) — _ioinit: the first ioinfo block (32 entries, _CRT_BLOCK "ioinit.c" 131), the
 * inherited handles of STARTUPINFO's lpReserved2 (more blocks, up to 2048), then stdin / stdout / stderr: an entry
 * still unset gets FOPEN | FTEXT and GetStdHandle's handle (none / unknown type: FDEV; a char device FDEV, a pipe
 * FPIPE), a set one FTEXT; EAX = SetHandleCount(_nhandle). */
int rl_crt_4c9e50(void)
{
    uint32_t b = rl_crt_4bfa30(0x100, 2, S_IOINIT_C, 131);
    if (b == 0) rl_crt_4c35c0(27);
    U32(G_PIOINFO) = b;
    U32(G_NHANDLE) = 32;
    init_block(b);
    STARTUPINFOA si;
    IAT(void (WINAPI *)(STARTUPINFOA *), I_GetStartupInfoA)(&si);
    if (si.cbReserved2 != 0 && si.lpReserved2 != NULL) {
        uint32_t base = (uint32_t)(uintptr_t)si.lpReserved2;
        int32_t cnt = V32(base);
        uint32_t flags = base + 4, hp = base + 4 + (uint32_t)cnt;
        int32_t n = cnt >= 2048 ? 2048 : cnt;
        for (int k = 1; (int32_t)U32(G_NHANDLE) < n; k++) {
            uint32_t nb = rl_crt_4bfa30(0x100, 2, S_IOINIT_C, 184);
            if (nb == 0) { n = (int32_t)U32(G_NHANDLE); break; }
            U32(G_PIOINFO + 4u * (uint32_t)k) = nb;
            U32(G_NHANDLE) += 32;
            init_block(nb);
        }
        for (int32_t i = 0; i < n; i++, flags++, hp += 4) {
            if (U32(hp) != 0xffffffffu && (U8(flags) & 1) &&
                ((U8(flags) & 8) || IAT(DWORD (WINAPI *)(HANDLE), I_GetFileType)((HANDLE)(uintptr_t)U32(hp)) != 0)) {
                uint32_t e = pio((uint32_t)i);
                U32(e) = U32(hp);
                U8(e + 4) = U8(flags);
            }
        }
    }
    for (int j = 0; j < 3; j++) {
        uint32_t e = U32(G_PIOINFO) + 8u * (uint32_t)j;
        if (U32(e) == 0xffffffffu) {
            U8(e + 4) = 0x81;
            HANDLE h = IAT(HANDLE (WINAPI *)(DWORD), I_GetStdHandle)(j == 0 ? STD_INPUT_HANDLE
                                                                     : j == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            DWORD t;
            if (h == INVALID_HANDLE_VALUE || (t = IAT(DWORD (WINAPI *)(HANDLE), I_GetFileType)(h)) == 0)
                U8(e + 4) |= 0x40;
            else {
                U32(e) = (uint32_t)(uintptr_t)h;
                if ((uint8_t)t == 2) U8(e + 4) |= 0x40;
                else if ((uint8_t)t == 3) U8(e + 4) |= 8;
            }
        } else
            U8(e + 4) |= 0x80;
    }
    return (int)IAT(UINT (WINAPI *)(UINT), I_SetHandleCount)(U32(G_NHANDLE));
}

/* Port of FUN_004cccc0 (333 B) — _alloc_osfhnd: the first entry without FOPEN (its handle reset to -1) over the
 * allocated blocks; a missing block is allocated (_CRT_BLOCK "osfinfo.c" 121) and its first fd returned. -1 = none. */
int rl_crt_4cccc0(void)
{
    int fd = -1;
    for (int i = 0; i < 64; i++) {
        uint32_t slot = G_PIOINFO + 4u * (uint32_t)i;
        if (U32(slot) == 0) {
            uint32_t b = rl_crt_4bfa30(0x100, 2, S_OSFINFO_C, 121);
            if (b != 0) {
                U32(slot) = b;
                U32(G_NHANDLE) += 32;
                init_block(b);
                return 32 * i;
            }
            return fd;
        }
        for (uint32_t e = U32(slot); U32(slot) + 256 > e; e += 8)
            if ((U8(e + 4) & 1) == 0) {
                U32(e) = 0xffffffffu;
                fd = 32 * i + ((int32_t)(e - U32(slot)) >> 3);
                break;
            }
        if (fd != -1) return fd;
    }
    return fd;
}

/* Port of FUN_004cce10 (244 B) — _set_osfhnd: an fd in range whose handle is unset takes `h` (a console app also
 * SetStdHandle's fds 0..2). -1 + EBADF otherwise. */
int rl_crt_4cce10(uint32_t fd, uint32_t h)
{
    if (U32(G_NHANDLE) > fd && OSFHND(fd) == 0xffffffffu) {
        if (V32(G_APPTYPE) == 1) {
            if (fd == 0) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_INPUT_HANDLE, (HANDLE)(uintptr_t)h);
            else if (fd == 1) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_OUTPUT_HANDLE, (HANDLE)(uintptr_t)h);
            else if (fd == 2) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_ERROR_HANDLE, (HANDLE)(uintptr_t)h);
        }
        OSFHND(fd) = h;
        return 0;
    }
    ebadf();
    return -1;
}

/* Port of FUN_004ccf10 (273 B) — _free_osfhnd: an open fd with a handle gets -1 (console: SetStdHandle NULL). */
int rl_crt_4ccf10(uint32_t fd)
{
    if (U32(G_NHANDLE) <= fd || (OSFILE(fd) & 1) == 0 || OSFHND(fd) == 0xffffffffu) {
        ebadf();
        return -1;
    }
    if (V32(G_APPTYPE) == 1) {
        if (fd == 0) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_INPUT_HANDLE, NULL);
        else if (fd == 1) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_OUTPUT_HANDLE, NULL);
        else if (fd == 2) IAT(BOOL (WINAPI *)(DWORD, HANDLE), I_SetStdHandle)(STD_ERROR_HANDLE, NULL);
    }
    OSFHND(fd) = 0xffffffffu;
    return 0;
}

/* Port of FUN_004cd030 (123 B) — _get_osfhandle: an open fd's handle; -1 + EBADF. */
int rl_crt_4cd030(uint32_t fd)
{
    if (fd < U32(G_NHANDLE) && (OSFILE(fd) & 1)) return (int)OSFHND(fd);
    ebadf();
    return -1;
}

/* Port of FUN_004cc3b0 (285 B) — _lseek: SetFilePointer on an open fd (a failure through _dosmaperr); clears FEOFLAG. */
int rl_crt_4cc3b0(uint32_t fd, int off, int origin)
{
    if (!(U32(G_NHANDLE) > fd && (OSFILE(fd) & 1))) {
        ebadf();
        return -1;
    }
    int h = rl_crt_4cd030(fd);
    if (h == -1) {
        V32(G_ERRNO) = 9;
        return -1;
    }
    DWORD pos = IAT(DWORD (WINAPI *)(HANDLE, LONG, PLONG, DWORD), I_SetFilePointer)((HANDLE)(uintptr_t)(uint32_t)h, off,
                                                                                    NULL, (DWORD)origin);
    DWORD err = pos == 0xffffffffu ? last_error() : 0;
    if (err != 0) {
        rl_crt_4c5930(err);
        return -1;
    }
    OSFILE(fd) &= (uint8_t)~2u;
    return (int)pos;
}

/* Port of FUN_004ce640 (267 B) — _close: CloseHandle (not when fds 1 and 2 share one handle), _free_osfhnd, osfile
 * 0; an OS error through _dosmaperr (-1). */
int rl_crt_4ce640(uint32_t fd)
{
    if (!(fd < U32(G_NHANDLE) && (OSFILE(fd) & 1))) {
        ebadf();
        return -1;
    }
    DWORD err;
    if (((fd == 1 || fd == 2) && rl_crt_4cd030(2) == rl_crt_4cd030(1)) ||
        IAT(BOOL (WINAPI *)(HANDLE), I_CloseHandle)((HANDLE)(uintptr_t)(uint32_t)rl_crt_4cd030(fd)))
        err = 0;
    else
        err = last_error();
    rl_crt_4ccf10(fd);
    if (err != 0) {
        rl_crt_4c5930(err);
        return -1;
    }
    OSFILE(fd) = 0;
    return 0;
}

/* Port of FUN_004cea50 (1439 B) — _sopen: the access / share / creation / attributes from the flags (_O_NOINHERIT,
 * _O_BINARY / _O_TEXT / _fmode, _O_CREAT with the umask's read-only bit, _O_TEMPORARY, _O_SHORT_LIVED, _O_SEQUENTIAL
 * / _O_RANDOM), an fd (_alloc_osfhnd), CreateFileA, the file type (a device FDEV, a pipe FPIPE), the fd's handle and
 * osfile (| FOPEN); a text-mode read-write file that ends in ^Z has it cut (_chsize, unported: never reached);
 * _O_APPEND → FAPPEND. -1 + errno on failure. */
int rl_crt_4cea50(uint32_t path, int oflag, int shflag, int pmode)
{
    SECURITY_ATTRIBUTES sa;
    uint8_t fl;
    DWORD acc, share, disp, attr;
    sa.nLength = 12;
    sa.lpSecurityDescriptor = NULL;
    if ((oflag & 0x80) == 0) {
        sa.bInheritHandle = TRUE;
        fl = 0;
    } else {
        sa.bInheritHandle = FALSE;
        fl = 0x10;
    }
    if ((oflag & 0x8000) == 0) {
        if (oflag & 0x4000)
            fl |= 0x80;
        else if (V32(G_FMODE) != 0x8000)
            fl |= 0x80;
    }
    switch (oflag & 3) {
    case 0: acc = GENERIC_READ; break;
    case 1: acc = GENERIC_WRITE; break;
    case 2: acc = GENERIC_READ | GENERIC_WRITE; break;
    default: V32(G_ERRNO) = 22; V32(G_DOSERRNO) = 0; return -1;
    }
    switch (shflag) {
    case 16: share = 0; break;
    case 32: share = 1; break;
    case 48: share = 2; break;
    case 64: share = 3; break;
    default: V32(G_ERRNO) = 22; V32(G_DOSERRNO) = 0; return -1;
    }
    switch (oflag & 0x700) {
    case 0: case 0x400: disp = OPEN_EXISTING; break;
    case 0x100: disp = OPEN_ALWAYS; break;
    case 0x200: case 0x600: disp = TRUNCATE_EXISTING; break;
    case 0x300: disp = CREATE_ALWAYS; break;
    case 0x500: case 0x700: disp = CREATE_NEW; break;
    default: V32(G_ERRNO) = 22; V32(G_DOSERRNO) = 0; return -1;
    }
    attr = FILE_ATTRIBUTE_NORMAL;
    if ((oflag & 0x100) && (pmode & ~V32(G_UMASK) & 0x80) == 0)
        attr = FILE_ATTRIBUTE_READONLY;
    if (oflag & 0x40) {
        attr |= 0x4000000u;
        acc |= 0x10000u;
    }
    if (oflag & 0x1000)
        attr |= 0x100u;
    if (oflag & 0x20)
        attr |= 0x8000000u;
    else if (oflag & 0x10)
        attr |= 0x10000000u;
    int fd = rl_crt_4cccc0();
    if (fd == -1) {
        V32(G_ERRNO) = 24;
        V32(G_DOSERRNO) = 0;
        return -1;
    }
    HANDLE h = IAT(HANDLE (WINAPI *)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE), I_CreateFileA)(
        (LPCSTR)(uintptr_t)path, acc, share, &sa, disp, attr, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        rl_crt_4c5930(last_error());
        return -1;
    }
    DWORD t = IAT(DWORD (WINAPI *)(HANDLE), I_GetFileType)(h);
    if (t == 0) {
        IAT(BOOL (WINAPI *)(HANDLE), I_CloseHandle)(h);
        rl_crt_4c5930(last_error());
        return -1;
    }
    if (t == 2) fl |= 0x40;
    else if (t == 3) fl |= 8;
    rl_crt_4cce10((uint32_t)fd, (uint32_t)(uintptr_t)h);
    fl |= 1;
    OSFILE(fd) = fl;
    if ((fl & 0x48) == 0 && (fl & 0x80) && (oflag & 2)) {
        int end = rl_crt_4cc3b0((uint32_t)fd, -1, 2);
        if (end == -1) {
            if (V32(G_DOSERRNO) != 131) {
                rl_crt_4ce640((uint32_t)fd);
                return -1;
            }
        } else {
            uint8_t c = 0;
            if ((rl_crt_4ceff0((uint32_t)fd, (uint32_t)(uintptr_t)&c, 1) == 0 && c == 26 &&
                 (int)rl_crt_4cf640(fd, end) == -1) ||
                rl_crt_4cc3b0((uint32_t)fd, 0, 0) == -1) {
                rl_crt_4ce640((uint32_t)fd);
                return -1;
            }
        }
    }
    if ((fl & 0x48) == 0 && (oflag & 8))
        OSFILE(fd) |= 0x20;
    return fd;
}

/* Port of FUN_004cea00 (67 B) — _open: _sopen(path, oflag, _SH_DENYNO, pmode) (pmode = the third stack slot, read
 * whether or not the caller pushed it). */
int rl_crt_4cea00(uint32_t path, int oflag, int pmode) { return rl_crt_4cea50(path, oflag, 64, pmode); }

/* Port of FUN_004ceff0 (1171 B) — _read: a pending pipe lookahead first; ReadFile; text mode: FCRLF from the first
 * byte, ^Z = end of file (FEOFLAG unless a device), CR LF → LF (a CR at the buffer's end peeks the next byte: a device
 * keeps it as the lookahead, a file seeks back), the length after the translation. ERROR_ACCESS_DENIED → EBADF,
 * ERROR_BROKEN_PIPE → 0. */
int rl_crt_4ceff0(uint32_t fd, uint32_t buf, uint32_t n)
{
    if (!(U32(G_NHANDLE) > fd && (OSFILE(fd) & 1))) {
        ebadf();
        return -1;
    }
    int got = 0;
    uint32_t p = buf;
    if (n == 0 || (OSFILE(fd) & 2)) return 0;
    if ((OSFILE(fd) & 0x48) && PIPECH(fd) != 10) {
        U8(p) = PIPECH(fd);
        p++;
        got++;
        n--;
        PIPECH(fd) = 10;
    }
    DWORD rd;
    if (!IAT(BOOL (WINAPI *)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED), I_ReadFile)(
            (HANDLE)(uintptr_t)OSFHND(fd), (LPVOID)(uintptr_t)p, n, &rd, NULL)) {
        DWORD err = last_error();
        if (err == 5) {
            V32(G_ERRNO) = 9;
            V32(G_DOSERRNO) = 5;
            return -1;
        }
        if (err == 109) return 0;
        rl_crt_4c5930(err);
        return -1;
    }
    got += (int)rd;
    if ((OSFILE(fd) & 0x80) == 0) return got;
    if (rd != 0 && U8(buf) == 10)
        OSFILE(fd) |= 4;
    else
        OSFILE(fd) &= (uint8_t)~4u;
    uint32_t dst = buf, src = buf;
    while (buf + (uint32_t)got > src) {
        if (U8(src) == 26) {
            if ((OSFILE(fd) & 0x40) == 0) OSFILE(fd) |= 2;
            return (int)(dst - buf);
        }
        if (U8(src) != 13) {
            U8(dst++) = U8(src++);
            continue;
        }
        if (buf + (uint32_t)got - 1 <= src) {
            src++;
            uint8_t peek;
            DWORD err = 0;
            if (!IAT(BOOL (WINAPI *)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED), I_ReadFile)(
                    (HANDLE)(uintptr_t)OSFHND(fd), &peek, 1, &rd, NULL))
                err = last_error();
            if (err == 0 && rd != 0) {
                if (OSFILE(fd) & 0x48) {
                    if (peek == 10)
                        U8(dst++) = 10;
                    else {
                        U8(dst++) = 13;
                        PIPECH(fd) = peek;
                    }
                } else if (buf == dst && peek == 10)
                    U8(dst++) = 10;
                else {
                    rl_crt_4cc3b0(fd, -1, 1);
                    if (peek != 10) U8(dst++) = 13;
                }
            } else
                U8(dst++) = 13;
        } else {
            if (U8(src + 1) == 10) {
                src += 2;
                U8(dst) = 10;
            } else
                U8(dst) = U8(src++);
            dst++;
        }
    }
    return (int)(dst - buf);
}

/* Port of FUN_004cc0b0 (759 B) — _write: FAPPEND seeks to the end first; binary = one WriteFile; text = LF → CR LF
 * through a 1 KB stack buffer (stops at a short write); EAX = the bytes written minus the CRs added. Nothing written:
 * the OS error (ERROR_ACCESS_DENIED → EBADF), else 0 for a device whose data starts with ^Z, else ENOSPC. */
int rl_crt_4cc0b0(uint32_t fd, uint32_t buf, uint32_t n)
{
    if (!(fd < U32(G_NHANDLE) && (OSFILE(fd) & 1))) {
        ebadf();
        return -1;
    }
    DWORD written = 0, put, err;
    int lfs = 0;
    if (n == 0) return 0;
    if (OSFILE(fd) & 0x20)
        rl_crt_4cc3b0(fd, 0, 2);
    if ((OSFILE(fd) & 0x80) == 0) {
        if (IAT(BOOL (WINAPI *)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED), I_WriteFile)(
                (HANDLE)(uintptr_t)OSFHND(fd), (LPCVOID)(uintptr_t)buf, n, &put, NULL)) {
            err = 0;
            written = put;
        } else
            err = last_error();
    } else {
        uint8_t tmp[1028];
        uint32_t s = buf;
        err = 0;
        while (s - buf < n) {
            uint8_t *q = tmp;
            for (; q - tmp < 1024 && s - buf < n; q++) {
                uint8_t c = U8(s++);
                if (c == 10) {
                    lfs++;
                    *q++ = 13;
                }
                *q = c;
            }
            if (!IAT(BOOL (WINAPI *)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED), I_WriteFile)(
                    (HANDLE)(uintptr_t)OSFHND(fd), tmp, (DWORD)(q - tmp), &put, NULL)) {
                err = last_error();
                break;
            }
            written += put;
            if (q - tmp > (int)put) break;
        }
    }
    if (written != 0) return (int)(written - (DWORD)lfs);
    if (err != 0) {
        if (err == 5) {
            V32(G_ERRNO) = 9;
            V32(G_DOSERRNO) = 5;
        } else
            rl_crt_4c5930(err);
        return -1;
    }
    if ((OSFILE(fd) & 0x40) && U8(buf) == 26) return 0;
    V32(G_ERRNO) = 28;
    V32(G_DOSERRNO) = 0;
    return -1;
}

/* Port of FUN_004cf490 (190 B) — _filelength: the current position, the end, the position put back. */
int rl_crt_4cf490(uint32_t fd)
{
    if (!(fd < U32(G_NHANDLE) && (OSFILE(fd) & 1))) {
        ebadf();
        return -1;
    }
    int here = rl_crt_4cc3b0(fd, 0, 1);
    if (here == -1) return -1;
    int end = rl_crt_4cc3b0(fd, 0, 2);
    if (here != end) rl_crt_4cc3b0(fd, here, 0);
    return end;
}

/* Port of FUN_004cf8f0 (193 B) — _mbctoupper: a single byte a..z → A..Z; a double-byte lead through
 * __crtLCMapStringA (unported: an SBCS code page never has one). */
uint32_t rl_crt_4cf8f0(uint32_t c)
{
    if (c <= 0xff) return ((int)c < 97 || (int)c > 122) ? c : c - 32;
    uint8_t src[4] = { (uint8_t)(c >> 8), (uint8_t)c, 0, 0 }, dst[4];
    if ((U8(G_MBCTYPE + 1 + ((c >> 8) & 0xff)) & 4) == 0) return c;
    if (rl_crt_4cb860(U32(G_MBLCID), 0x200, (uint32_t)(uintptr_t)src, 2, (uint32_t)(uintptr_t)dst, 2,
               U32(G_MBCP)) != 0)
        return ((uint32_t)dst[0] << 8) + dst[1];
    return c;
}

/* Port of FUN_004cf550 (226 B) — _chdir: SetCurrentDirectoryA, then the drive's "=X:" environment variable to the
 * new directory (not for a UNC path). -1 + errno from GetLastError. */
int rl_crt_4cf550(uint32_t path)
{
    char buf[268];   /* [0..1] the variable's "=X", [2..3] ":\0", [4..] the directory (retail's two adjacent locals) */
    if (IAT(BOOL (WINAPI *)(LPCSTR), I_SetCurrentDirectoryA)((LPCSTR)(uintptr_t)path) &&
        IAT(DWORD (WINAPI *)(DWORD, LPSTR), I_GetCurrentDirectoryA)(0x105, buf + 4) != 0) {
        if ((buf[4] == '\\' || buf[4] == '/') && buf[4] == buf[5]) return 0;
        buf[0] = '=';
        buf[1] = (char)rl_crt_4cf8f0((uint8_t)buf[4]);
        buf[2] = ':';
        buf[3] = 0;
        if (IAT(BOOL (WINAPI *)(LPCSTR, LPCSTR), I_SetEnvironmentVariableA)(buf, buf + 4)) return 0;
    }
    rl_crt_4c5930(last_error());
    return -1;
}

/* Port of FUN_004c1b40 (312 B) — _getdcwd: drive 0 = GetCurrentDirectoryA, else GetFullPathNameA("X:.") of a valid
 * drive (_validdrive, unported: the game asks drive 0); the length + 1 must fit 260 and the buffer (ERANGE), a NULL
 * buffer is malloc'd (max(len + 1, n)); EAX = strcpy's result. */
uint32_t rl_crt_4c1b40(int drive, uint32_t buf, int n)
{
    char path[260];
    DWORD len;
    if (drive != 0) {
        if (rl_crt_4c1c80(drive) == 0) {
            V32(G_DOSERRNO) = 15;
            V32(G_ERRNO) = 13;
            return 0;
        }
        char spec[4] = { (char)(drive + 64), ':', '.', 0 };
        LPSTR part;
        len = IAT(DWORD (WINAPI *)(LPCSTR, DWORD, LPSTR, LPSTR *), I_GetFullPathNameA)(spec, 0x104, path, &part);
    } else
        len = IAT(DWORD (WINAPI *)(DWORD, LPSTR), I_GetCurrentDirectoryA)(0x104, path);
    if (len == 0) return 0;
    if (++len > 0x104) return 0;
    uint32_t d = buf;
    if (buf != 0) {
        if ((int)len > n) {
            V32(G_ERRNO) = 34;
            return 0;
        }
    } else {
        d = rl_crt_4bfa00((int)len <= n ? (uint32_t)n : len);
        if (d == 0) {
            V32(G_ERRNO) = 12;
            return 0;
        }
    }
    return rl_crt_4c2080(d, (uint32_t)(uintptr_t)path);
}

/* Port of FUN_004c1b10 (43 B) — _getcwd: _getdcwd(0, buf, n). */
uint32_t rl_crt_4c1b10(uint32_t buf, int n) { return rl_crt_4c1b40(0, buf, n); }

/* Port of FUN_004c57f0 (318 B) — toupper: the "C" locale (__lc_handle[LC_CTYPE] 0) maps a..z; another locale
 * through _isctype / __crtLCMapStringA (unported: rhost's process is in the "C" locale). */
int rl_crt_4c57f0(int c)
{
    if (V32(G_LCCTYPE) == 0) {
        if (c >= 97 && c <= 122) c -= 32;
        return c;
    }
    int lower;
    if (c >= 256)
        lower = 1;
    else if (V32(G_MBCURMAX) <= 1)
        lower = U16(U32(G_PCTYPE) + 2u * (uint32_t)c) & 2;
    else
        lower = (int)rl_crt_4c5710(c, 2);
    if (!lower) return c;
    uint8_t src[3], dst[4];
    int n;
    if ((U16(U32(G_PCTYPE) + 2u * (((uint32_t)c >> 8) & 0xff)) >> 8 & 0x80) == 0) {
        src[0] = (uint8_t)c; src[1] = 0; n = 1;
    } else {
        src[0] = (uint8_t)(c >> 8); src[1] = (uint8_t)c; src[2] = 0; n = 2;
    }
    n = (int)rl_crt_4cb860(U32(G_LCCTYPE), 0x200, (uint32_t)(uintptr_t)src, n, (uint32_t)(uintptr_t)dst, 3, 0);
    if (n == 0) return c;
    return n == 1 ? dst[0] : *(uint16_t *)dst;
}

/* Port of FUN_004c1cf0 (98 B) — _getdrive: the current directory's drive letter (toupper - 'A' + 1), 0 if none. */
int rl_crt_4c1cf0(void)
{
    char path[260];
    if (IAT(DWORD (WINAPI *)(DWORD, LPSTR), I_GetCurrentDirectoryA)(0x104, path) != 0 && path[1] == ':')
        return rl_crt_4c57f0((uint8_t)path[0]) - 64;
    return 0;
}

/* Port of FUN_004c2260 (96 B) — rename: MoveFileA; -1 + errno. */
int rl_crt_4c2260(uint32_t from, uint32_t to)
{
    DWORD err = IAT(BOOL (WINAPI *)(LPCSTR, LPCSTR), I_MoveFileA)((LPCSTR)(uintptr_t)from, (LPCSTR)(uintptr_t)to)
                    ? 0 : last_error();
    if (err == 0) return 0;
    rl_crt_4c5930(err);
    return -1;
}

/* Port of FUN_004c22c0 (92 B) — remove: DeleteFileA; -1 + errno. */
int rl_crt_4c22c0(uint32_t path)
{
    DWORD err = IAT(BOOL (WINAPI *)(LPCSTR), I_DeleteFileA)((LPCSTR)(uintptr_t)path) ? 0 : last_error();
    if (err == 0) return 0;
    rl_crt_4c5930(err);
    return -1;
}

/* ================================================================ time */

/* Port of FUN_004c6130 (515 B) — cvtdate: a DST transition (start = 1: dststart, else dstend) as a year day and a
 * millisecond of day. type 1 = "the week-th dow of month" (week 5 = the last), else an absolute day. dstend's ms
 * includes _dstbias and wraps a day — quirk: on the wrap retail moves the MILLISECONDS by one (inc / dec 0x4e5278),
 * not the year day (the MS source's dstend.yd). EAX = yr. */
int rl_crt_4c6130(int start, int type, int yr, int mo, int week, int dow, int date, int hr, int mn, int sc, int ms)
{
    int yday;
    if (type == 1) {
        int d0 = (yr & 3) ? V32(G_DAYS_M1 + 4u * (uint32_t)mo) : V32(G_LPDAYS_M1 + 4u * (uint32_t)mo);
        int wd = (((yr - 1) >> 2) + 365 * (yr - 70) + d0 + 1 - 13) % 7;
        int v = wd >= dow ? dow - wd - week + 8 * week : dow - wd - (week - 1) + 8 * (week - 1);
        yday = v + d0 + 1;
        if (week == 5) {
            int last = (yr & 3) ? V32(G_DAYS + 4u * (uint32_t)mo) : V32(G_LPDAYS + 4u * (uint32_t)mo);
            if (yday > last) yday -= 7;
        }
    } else {
        int d0 = (yr & 3) ? V32(G_DAYS_M1 + 4u * (uint32_t)mo) : V32(G_LPDAYS_M1 + 4u * (uint32_t)mo);
        yday = date + d0;
    }
    if (start == 1) {
        V32(G_DSTS_YD) = yday;
        V32(G_DSTS_MS) = ms + 1000 * (sc + 60 * (mn + 60 * hr));
        V32(G_DSTS_YR) = yr;
        return yr;
    }
    V32(G_DSTE_YD) = yday;
    V32(G_DSTE_MS) = ms + 1000 * (sc + 60 * (mn + 60 * hr));
    V32(G_DSTE_MS) = (int32_t)((uint32_t)V32(G_DSTE_MS) + 1000u * (uint32_t)V32(G_DSTBIAS));
    if (V32(G_DSTE_MS) < 0) {
        V32(G_DSTE_MS) += 86400000;
        V32(G_DSTE_MS)--;
    } else if (V32(G_DSTE_MS) >= 86400000) {
        V32(G_DSTE_MS) -= 86400000;
        V32(G_DSTE_MS)++;
    }
    V32(G_DSTE_YR) = yr;
    return yr;
}

/* Port of FUN_004c5dc0 (874 B) — _isindst: with _daylight, the year's transitions (cvtdate: from the zone's
 * DaylightDate / StandardDate when tzapiused, else the US rule: first Sunday of April 2:00 → last Sunday of October
 * 2:00), then the tm's year day / time against them (southern zones: start after end). */
int rl_crt_4c5dc0(uint32_t tm)
{
    volatile int32_t *t = (volatile int32_t *)(uintptr_t)tm;
    if (V32(G_DAYLIGHT) == 0) return 0;
    if (t[5] != V32(G_DSTS_YR) || t[5] != V32(G_DSTE_YR)) {
        if (V32(G_TZAPI) != 0) {
            const SYSTEMTIME *dd = (const SYSTEMTIME *)(uintptr_t)(G_TZI + 152), *sd = (const SYSTEMTIME *)(uintptr_t)(G_TZI + 68);
            if (dd->wYear != 0)
                rl_crt_4c6130(1, 0, t[5], dd->wMonth, 0, 0, dd->wDay, dd->wHour, dd->wMinute, dd->wSecond, dd->wMilliseconds);
            else
                rl_crt_4c6130(1, 1, t[5], dd->wMonth, dd->wDay, dd->wDayOfWeek, 0, dd->wHour, dd->wMinute, dd->wSecond,
                              dd->wMilliseconds);
            if (sd->wYear != 0)
                rl_crt_4c6130(0, 0, t[5], sd->wMonth, 0, 0, sd->wDay, sd->wHour, sd->wMinute, sd->wSecond, sd->wMilliseconds);
            else
                rl_crt_4c6130(0, 1, t[5], sd->wMonth, sd->wDay, sd->wDayOfWeek, 0, sd->wHour, sd->wMinute, sd->wSecond,
                              sd->wMilliseconds);
        } else {
            rl_crt_4c6130(1, 1, t[5], 4, 1, 0, 0, 2, 0, 0, 0);
            rl_crt_4c6130(0, 1, t[5], 10, 5, 0, 0, 2, 0, 0, 0);
        }
    }
    int32_t sy = V32(G_DSTS_YD), ey = V32(G_DSTE_YD), yd = t[7];
    if (ey <= sy) {
        if (yd < ey || yd > sy) return 1;
        if (yd > ey && yd < sy) return 0;
    } else {
        if (yd < sy || yd > ey) return 0;
        if (yd > sy && yd < ey) return 1;
    }
    int32_t ms = 1000 * (t[0] + 60 * t[1] + 3600 * t[2]);
    if (yd == sy) return V32(G_DSTS_MS) <= ms;
    return V32(G_DSTE_MS) > ms;
}

/* Port of FUN_004cbfc0 (227 B) — getenv: _environ (built from _wenviron when only that exists: __wtomb_environ,
 * unported), the first "NAME=" entry (_mbsnbicoll: case-insensitive, unported — rhost's one variable never matches
 * a 2-letter name's '=' position); EAX = its value. */
uint32_t rl_crt_4cbfc0(uint32_t name)
{
    uint32_t e = U32(G_ENVIRON);
    if (e == 0 && U32(G_WENVIRON) != 0) {
        if (rl_crt_4ccbf0() != 0) return 0;
        e = U32(G_ENVIRON);
    }
    if (e == 0 || name == 0) return 0;
    uint32_t n = rl_crt_4bf620(name);
    for (; U32(e) != 0; e += 4) {
        uint32_t s = U32(e);
        if (rl_crt_4bf620(s) > n && U8(s + n) == '=' && rl_crt_4ccb80(s, name, n) == 0)
            return s + n + 1;
    }
    return 0;
}

/* Port of FUN_004c5a60 (861 B) — _tzset: tzapiused 0, the cached transition years -1; TZ set (and changed) = its
 * "SSS[+|-]hh[:mm[:ss]][DDD]" (strncpy / atol, unported: rhost sets no TZ); else GetTimeZoneInformation (into
 * 0x4e5b80): nonzero = _timezone / _daylight / _dstbias and the names (wcstombs, unported); zero (rhost's zone: no
 * information) leaves the .data defaults (_timezone 28800, _daylight 1, _dstbias -3600). */
void rl_crt_4c5a60(void)
{
    int neg = 0;
    V32(G_TZAPI) = 0;
    V32(G_DSTE_YR) = -1;
    V32(G_DSTS_YR) = -1;
    uint32_t tz = rl_crt_4cbfc0(S_TZ);
    if (tz != 0) {
        if (U8(tz) == 0 || (U32(G_LASTTZ) != 0 && rl_crt_4c2170(tz, U32(G_LASTTZ)) == 0)) return;
        rl_4c05a0((int)U32(G_LASTTZ), 2);
        U32(G_LASTTZ) = rl_crt_4bfa30(rl_crt_4bf620(tz) + 1, 2, S_TZSET_C, 236);
        if (U32(G_LASTTZ) == 0) return;
        rl_crt_4c2080(U32(G_LASTTZ), tz);
        uint32_t n0 = U32(G_TZNAME0), n1 = U32(G_TZNAME1);
        rl_crt_4c28d0(n0, tz, 3);
        U8(n0 + 3) = 0;
        uint32_t s = tz + 3;
        if (U8(s) == '-') { neg = 1; s++; }
        V32(G_TIMEZONE) = 3600 * (int32_t)rl_crt_4c1df0(s);
        while (U8(s) == '+' || (U8(s) >= '0' && U8(s) <= '9')) s++;
        if (U8(s) == ':') {
            V32(G_TIMEZONE) += 60 * (int32_t)rl_crt_4c1df0(++s);
            while (U8(s) >= '0' && U8(s) <= '9') s++;
            if (U8(s) == ':') {
                V32(G_TIMEZONE) += (int32_t)rl_crt_4c1df0(++s);
                while (U8(s) >= '0' && U8(s) <= '9') s++;
            }
        }
        if (neg) V32(G_TIMEZONE) = -V32(G_TIMEZONE);
        V32(G_DAYLIGHT) = (int8_t)U8(s);
        if (V32(G_DAYLIGHT) != 0) {
            rl_crt_4c28d0(n1, s, 3);
            U8(n1 + 3) = 0;
        } else
            U8(n1) = 0;
        return;
    }
    if (IAT(DWORD (WINAPI *)(LPTIME_ZONE_INFORMATION), I_GetTimeZoneInformation)(
            (LPTIME_ZONE_INFORMATION)(uintptr_t)G_TZI) != 0) {
        const TIME_ZONE_INFORMATION *z = (const TIME_ZONE_INFORMATION *)(uintptr_t)G_TZI;
        V32(G_TZAPI) = 1;
        V32(G_TIMEZONE) = 60 * z->Bias;
        if (z->StandardDate.wMonth != 0) V32(G_TIMEZONE) += 60 * z->StandardBias;
        if (z->DaylightDate.wMonth != 0 && z->DaylightBias != 0) {
            V32(G_DAYLIGHT) = 1;
            V32(G_DSTBIAS) = 60 * (z->DaylightBias - z->StandardBias);
        } else {
            V32(G_DAYLIGHT) = 0;
            V32(G_DSTBIAS) = 0;
        }
        uint32_t n0 = U32(G_TZNAME0), n1 = U32(G_TZNAME1);
        rl_crt_4cbbf0(n0, G_TZI + 4, 0x40);
        rl_crt_4cbbf0(n1, G_TZI + 88, 0x40);
        U8(n1 + 63) = 0;
        U8(n0 + 63) = U8(n1 + 63);
    }
}

/* Port of FUN_004c5a30 (35 B) — __tzset: _tzset once. */
void rl_crt_4c5a30(void)
{
    if (V32(G_TZFIRST) == 0) {
        rl_crt_4c5a60();
        V32(G_TZFIRST)++;
    }
}

/* Port of FUN_004c6530 (274 B) — __loctotime_t: a local date / time (years 1970..2038) → time_t (+ _timezone, the
 * DST bias when dst = 1, or dst = -1 and _isindst says so); -1 out of range. */
int32_t rl_crt_4c6530(int yr, int mo, int dy, int hr, int mn, int sc, int dst)
{
    int32_t tb[9];
    yr -= 1900;
    if (yr < 70 || yr > 138) return -1;
    int yd = dy + V32(G_DAYS_M1 + 4u * (uint32_t)mo);
    if ((yr & 3) == 0 && mo > 2) yd++;
    rl_crt_4c5a30();
    int32_t t = V32(G_TIMEZONE) + sc + 60 * (mn + 60 * (hr + 24 * (yd + ((yr - 1) >> 2) + 365 * (yr - 70) - 17)));
    tb[7] = yd;
    tb[5] = yr;
    tb[4] = mo - 1;
    tb[2] = hr;
    /* retail leaves tm_sec / tm_min unset (its stack residue); _isindst reads them only on a DST transition day (the
     * hour test) — never on rhost's clock. The port: 0. */
    tb[0] = 0;
    tb[1] = 0;
    if (dst == 1 || (dst == -1 && V32(G_DAYLIGHT) != 0 && rl_crt_4c5dc0((uint32_t)(uintptr_t)tb)))
        t += V32(G_DSTBIAS);
    return t;
}

/* Port of FUN_004c2740 (390 B) — time: GetLocalTime + GetSystemTime; the DST state (GetTimeZoneInformation: 2
 * daylight → 1, 1 standard → 0, else -1) is cached per UTC minute (0x4e5b60 / 0x4e5b58); __loctotime_t of the local
 * time. */
int32_t rl_crt_4c2740(uint32_t tp)
{
    SYSTEMTIME lt, st;
    int32_t v;
    IAT(void (WINAPI *)(LPSYSTEMTIME), I_GetLocalTime)(&lt);
    IAT(void (WINAPI *)(LPSYSTEMTIME), I_GetSystemTime)(&st);
    if (st.wMinute == U16(G_TCACHE + 10) && st.wHour == U16(G_TCACHE + 8) && st.wDay == U16(G_TCACHE + 6) &&
        st.wMonth == U16(G_TCACHE + 2) && st.wYear == U16(G_TCACHE))
        v = rl_crt_4c6530(lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, V32(G_TDST));
    else {
        TIME_ZONE_INFORMATION z;
        DWORD r = IAT(DWORD (WINAPI *)(LPTIME_ZONE_INFORMATION), I_GetTimeZoneInformation)(&z);
        int dst = r == 2 ? 1 : r == 1 ? 0 : -1;
        V32(G_TDST) = dst;
        memcpy((void *)(uintptr_t)G_TCACHE, &st, 16);
        v = rl_crt_4c6530(lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, dst);
    }
    if (tp != 0) V32(tp) = v;
    return v;
}

/* Port of FUN_004c6340 (487 B) — gmtime: into the static tm 0x4e5280 (4-year cycles from 1970; tm_isdst 0); NULL for
 * a negative time. */
uint32_t rl_crt_4c6340(uint32_t tp)
{
    int32_t t = V32(tp);
    volatile int32_t *tb = (volatile int32_t *)(uintptr_t)G_TB;
    int leap = 0;
    if (t < 0) return 0;
    int32_t yr = 4 * (t / 126230400) + 70, r = t % 126230400;
    if (r >= 31536000) {
        yr++;
        r -= 31536000;
        if (r >= 31536000) {
            yr++;
            r -= 31536000;
            if (r < 31622400)
                leap = 1;
            else {
                yr++;
                r -= 31622400;
            }
        }
    }
    tb[5] = yr;
    tb[7] = r / 86400;
    r %= 86400;
    uint32_t md = leap ? G_LPDAYS : G_DAYS;
    int i;
    for (i = 1; V32(md + 4u * (uint32_t)i) < tb[7]; i++) {}
    tb[4] = i - 1;
    tb[3] = tb[7] - V32(md + 4u * (uint32_t)(i - 1));
    tb[6] = (V32(tp) / 86400 + 4) % 7;
    tb[2] = r / 3600;
    r %= 3600;
    tb[1] = r / 60;
    tb[0] = r % 60;
    tb[8] = 0;
    return G_TB;
}

/* Port of FUN_004c24e0 (597 B) — localtime: NULL for a negative time; near either end of the range (<= 3 days,
 * >= 2^31 - 3 days) gmtime then the zone / DST offset applied field by field; else gmtime(t - _timezone) and, in DST,
 * gmtime(t - _timezone - _dstbias) with tm_isdst 1. */
uint32_t rl_crt_4c24e0(uint32_t tp)
{
    int32_t t = V32(tp);
    if (t < 0) return 0;
    rl_crt_4c5a30();
    int32_t v;
    uint32_t r;
    if (t <= 259200 || t >= 2147224447) {
        r = rl_crt_4c6340(tp);
        volatile int32_t *tm = (volatile int32_t *)(uintptr_t)r;
        if (rl_crt_4c5dc0(r) != 0)
            v = tm[0] - (V32(G_DSTBIAS) + V32(G_TIMEZONE));
        else
            v = tm[0] - V32(G_TIMEZONE);
        tm[0] = v % 60;
        if (tm[0] < 0) { tm[0] += 60; v -= 60; }
        v = v / 60 + tm[1];
        tm[1] = v % 60;
        if (tm[1] < 0) { tm[1] += 60; v -= 60; }
        v = v / 60 + tm[2];
        tm[2] = v % 24;
        if (tm[2] < 0) { tm[2] += 24; v -= 24; }
        v /= 24;
        if (v > 0) {
            tm[6] = (v + tm[6]) % 7;
            tm[3] += v;
            tm[7] += v;
        } else if (v < 0) {
            tm[6] = (tm[6] + v + 7) % 7;
            tm[3] += v;
            if (tm[3] > 0)
                tm[7] += v;
            else {
                tm[3] += 31;
                tm[7] = 364;
                tm[4] = 11;
                tm[5]--;
            }
        }
        return r;
    }
    v = t - V32(G_TIMEZONE);
    r = rl_crt_4c6340((uint32_t)(uintptr_t)&v);
    if (V32(G_DAYLIGHT) != 0 && rl_crt_4c5dc0(r) != 0) {
        v -= V32(G_DSTBIAS);
        r = rl_crt_4c6340((uint32_t)(uintptr_t)&v);
        ((volatile int32_t *)(uintptr_t)r)[8] = 1;
    }
    return r;
}

/* Port of FUN_004c24a0 (63 B) — store_dt: two decimal digits; EAX = past them. */
uint32_t rl_crt_4c24a0(uint32_t p, int v)
{
    U8(p) = (uint8_t)(v / 10 + 48);
    U8(p + 1) = (uint8_t)(v % 10 + 48);
    return p + 2;
}

/* Port of FUN_004c2340 (345 B) — asctime: "Www Mmm dd hh:mm:ss yyyy\n" into the static buffer 0x4e5b38. */
uint32_t rl_crt_4c2340(uint32_t tmp)
{
    volatile const int32_t *tm = (volatile const int32_t *)(uintptr_t)tmp;
    uint32_t p = G_ASCBUF;
    int wd = 3 * tm[6], mo = 3 * tm[4];
    for (int i = 0; i < 3; i++, p++) {
        U8(p) = U8(S_DAYNAMES + (uint32_t)(i + wd));
        U8(p + 4) = U8(S_MONNAMES + (uint32_t)(i + mo));
    }
    U8(p) = ' ';
    p += 4;
    U8(p) = ' ';
    p = rl_crt_4c24a0(p + 1, tm[3]);
    U8(p) = ' ';
    p = rl_crt_4c24a0(p + 1, tm[2]);
    U8(p) = ':';
    p = rl_crt_4c24a0(p + 1, tm[1]);
    U8(p) = ':';
    p = rl_crt_4c24a0(p + 1, tm[0]);
    U8(p) = ' ';
    p = rl_crt_4c24a0(p + 1, tm[5] / 100 + 19);
    p = rl_crt_4c24a0(p, tm[5] % 100);
    U8(p) = '\n';
    U8(p + 1) = 0;
    return G_ASCBUF;
}

/* ================================================================ multibyte code page */

/* Port of FUN_004c8cc0 (131 B) — getSystemCP: -2 OEM, -3 ANSI (GetACP), -4 the locale's; fSystemSet = 1 for those. */
uint32_t rl_crt_4c8cc0(int cp)
{
    V32(G_CPSYSSET) = 0;
    switch (cp) {
    case -2: V32(G_CPSYSSET) = 1; return IAT(UINT (WINAPI *)(void), I_GetOEMCP)();
    case -3: V32(G_CPSYSSET) = 1; return IAT(UINT (WINAPI *)(void), I_GetACP)();
    case -4: V32(G_CPSYSSET) = 1; return U32(G_LCCP);
    default: return (uint32_t)cp;
    }
}

/* Port of FUN_004c8990 (815 B) — _setmbcp: the code page (getSystemCP); unchanged → 0. One of the 5 built-in
 * tables (932 / 936 / 949 / 950 / 1361: _mbctype's lead / trail ranges, __mbulinfo); else GetCPInfo: a DBCS code
 * page's lead-byte ranges (and every byte 1..254 a trail), an SBCS one clears __mbcodepage / __mblcid. CP 0 or a
 * failing GetCPInfo with a system code page = setSBCS (unported); EAX 0 / -1. */
int rl_crt_4c8990(int cp_in)
{
    uint32_t cp = rl_crt_4c8cc0(cp_in);
    if (cp == U32(G_MBCP)) return 0;
    if (cp == 0) {
        rl_crt_4c8df0();
        return 0;
    }
    for (uint32_t i = 0; i < 5; i++) {
        if (U32(G_CPINFO + 48 * i) != cp) continue;
        for (uint32_t j = 0; j < 0x101; j++) U8(G_MBCTYPE + j) = 0;
        for (uint32_t k = 0; k < 4; k++)
            for (uint32_t m = G_CPINFO + 48 * i + 16 + 8 * k; U8(m) != 0 && U8(m + 1) != 0; m += 2)
                for (uint32_t n = U8(m); U8(m + 1) >= n; n++) U8(G_MBCTYPE + 1 + n) |= U8(G_MBMASKS + k);
        U32(G_MBCP) = cp;
        U32(G_MBLCID) = rl_crt_4c8d50(cp);
        for (uint32_t k = 0; k < 6; k++) U16(G_MBULINFO + 2 * k) = U16(G_CPINFO + 4 + 48 * i + 2 * k);
        return 0;
    }
    CPINFO ci;
    if (IAT(BOOL (WINAPI *)(UINT, LPCPINFO), I_GetCPInfo)(cp, &ci)) {
        for (uint32_t j = 0; j < 0x101; j++) U8(G_MBCTYPE + j) = 0;
        if (ci.MaxCharSize <= 1) {
            U32(G_MBCP) = 0;
            U32(G_MBLCID) = 0;
        } else {
            for (const BYTE *lb = ci.LeadByte; lb[0] != 0 && lb[1] != 0; lb += 2)
                for (uint32_t n = lb[0]; lb[1] >= n; n++) U8(G_MBCTYPE + 1 + n) |= 4;
            for (uint32_t n = 1; n < 0xff; n++) U8(G_MBCTYPE + 1 + n) |= 8;
            U32(G_MBCP) = cp;
            U32(G_MBLCID) = rl_crt_4c8d50(cp);
        }
        for (uint32_t k = 0; k < 6; k++) U16(G_MBULINFO + 2 * k) = 0;
        return 0;
    }
    if (V32(G_CPSYSSET) != 0) {
        rl_crt_4c8df0();
        return 0;
    }
    return -1;
}

/* Port of FUN_004c8e90 (21 B) — __initmbctable: _setmbcp(_MB_CP_ANSI). */
int rl_crt_4c8e90(void) { return rl_crt_4c8990(-3); }

/* Port of FUN_004c92f0 (110 B) — x_ismbbtype: _mbctype[c + 1] & mmask, or (cmask) _ctype's bits. */
int rl_crt_4c92f0(uint32_t c, int cmask, int mmask)
{
    c &= 0xff;
    if (U8(G_MBCTYPE + 1 + c) & (uint8_t)mmask) return 1;
    int v = cmask != 0 ? (uint16_t)(cmask & U16(G_CTYPE1 + 2 * c)) : 0;
    return v != 0;
}

/* Port of FUN_004c9260 (32 B) — _ismbblead: x_ismbbtype(c, 0, _MLEAD). */
int rl_crt_4c9260(uint32_t c) { return rl_crt_4c92f0(c, 0, 4); }

/* ================================================================ environment / command line */

/* Port of FUN_004c9bf0 (602 B) — __crtGetEnvironmentStringsA: the wide block (GetEnvironmentStringsW) converted
 * through WideCharToMultiByte(CP_ACP) into a _CRT_BLOCK ("aw_env.c" 251), else the ANSI block copied ("aw_env.c" 294,
 * memcpy unported: rhost provides the wide one); which one is decided once (f_use 0x4e5578). */
uint32_t rl_crt_4c9bf0(void)
{
    LPWSTR w = NULL;
    LPSTR a = NULL;
    if (V32(G_ENVUSE) == 0) {
        w = IAT(LPWSTR (WINAPI *)(void), I_GetEnvironmentStringsW)();
        if (w != NULL)
            V32(G_ENVUSE) = 1;
        else {
            a = IAT(LPSTR (WINAPI *)(void), I_GetEnvironmentStrings)();
            if (a == NULL) return 0;
            V32(G_ENVUSE) = 2;
        }
    }
    if (V32(G_ENVUSE) == 1) {
        if (w == NULL && (w = IAT(LPWSTR (WINAPI *)(void), I_GetEnvironmentStringsW)()) == NULL) return 0;
        LPWSTR e = w;
        while (*e != 0)
            if (*++e == 0) e++;
        int n = IAT(int (WINAPI *)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL), I_WideCharToMultiByte)(
            0, 0, w, (int)(e - w) + 1, NULL, 0, NULL, NULL);
        uint32_t m;
        if (n != 0 && (m = rl_crt_4bfa30((uint32_t)n, 2, S_AW_ENV_C, 251)) != 0) {
            if (IAT(int (WINAPI *)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL), I_WideCharToMultiByte)(
                    0, 0, w, (int)(e - w) + 1, (LPSTR)(uintptr_t)m, n, NULL, NULL) == 0) {
                rl_4c05a0((int)m, 2);
                m = 0;
            }
            IAT(BOOL (WINAPI *)(LPWSTR), I_FreeEnvironmentStringsW)(w);
            return m;
        }
        IAT(BOOL (WINAPI *)(LPWSTR), I_FreeEnvironmentStringsW)(w);
        return 0;
    }
    if (V32(G_ENVUSE) == 2) {
        if (a == NULL && (a = IAT(LPSTR (WINAPI *)(void), I_GetEnvironmentStrings)()) == NULL) return 0;
        LPSTR e = a;
        while (*e != 0)
            if (*++e == 0) e++;
        uint32_t n = (uint32_t)(e - a) + 1;
        uint32_t m = rl_crt_4bfa30(n, 2, S_AW_ENV_C, 294);
        if (m != 0) rl_crt_4c3220(m, (uint32_t)(uintptr_t)a, n);
        IAT(BOOL (WINAPI *)(LPSTR), I_FreeEnvironmentStringsA)(a);
        return m;
    }
    return 0;
}

/* Port of FUN_004c9570 (958 B) — parse_cmdline: the program name (quoted: up to the closing quote, DBCS leads copied
 * — and skipped — only when copying, as retail does; else up to a blank), then the arguments (blanks separate outside
 * quotes; 2n backslashes + quote = n backslashes and a quote toggle, 2n+1 = n and a literal quote; "" inside quotes =
 * a quote). Counts argv entries (+1 for the NULL) and characters; fills argv / args when given. EAX = pargc. */
uint32_t rl_crt_4c9570(uint32_t cmd, uint32_t argv, uint32_t args, uint32_t pargc, uint32_t pnchars)
{
#define LEAD(c) (U8(G_MBCTYPE + 1 + (uint8_t)(c)) & 4)
    uint32_t p = cmd;
    U32(pnchars) = 0;
    U32(pargc) = 1;
    if (argv != 0) { U32(argv) = args; argv += 4; }
    if (U8(cmd) == '"') {
        while (U8(++p) != '"' && U8(p) != 0) {
            if (LEAD(U8(p))) {
                U32(pnchars)++;
                if (args != 0) U8(args++) = U8(p++);
            }
            U32(pnchars)++;
            if (args != 0) U8(args++) = U8(p);
        }
        U32(pnchars)++;
        if (args != 0) U8(args++) = 0;
        if (U8(p) == '"') p++;
    } else {
        uint8_t c;
        do {
            U32(pnchars)++;
            if (args != 0) U8(args++) = U8(p);
            c = U8(p++);
            if (LEAD(c)) {
                U32(pnchars)++;
                if (args != 0) U8(args++) = U8(p);
                p++;
            }
        } while (c != ' ' && c != 0 && c != '\t');
        if (c != 0) {
            if (args != 0) U8(args - 1) = 0;
        } else
            p--;
    }
    int inquote = 0;
    for (;;) {
        if (U8(p) != 0)
            while (U8(p) == ' ' || U8(p) == '\t') p++;
        if (U8(p) == 0) break;
        if (argv != 0) { U32(argv) = args; argv += 4; }
        U32(pargc)++;
        for (;;) {
            int copy = 1;
            uint32_t bs = 0;
            while (U8(p) == '\\') { p++; bs++; }
            if (U8(p) == '"') {
                if ((bs & 1) == 0) {
                    if (inquote) {
                        if (U8(p + 1) == '"') p++;
                        else copy = 0;
                    } else
                        copy = 0;
                    inquote = !inquote;
                }
                bs >>= 1;
            }
            while (bs-- != 0) {
                if (args != 0) U8(args++) = '\\';
                U32(pnchars)++;
            }
            if (U8(p) == 0 || (!inquote && (U8(p) == ' ' || U8(p) == '\t'))) break;
            if (copy) {
                if (args != 0) {
                    if (LEAD(U8(p))) {
                        U8(args++) = U8(p++);
                        U32(pnchars)++;
                    }
                    U8(args++) = U8(p);
                } else if (LEAD(U8(p))) {
                    p++;
                    U32(pnchars)++;
                }
                U32(pnchars)++;
            }
            p++;
        }
        if (args != 0) U8(args++) = 0;
        U32(pnchars)++;
    }
    if (argv != 0) U32(argv) = 0;
    U32(pargc)++;
    return pargc;
#undef LEAD
}

/* Port of FUN_004c94a0 (204 B) — _setargv: _pgmptr = the module's file name (0x4e5c30), the command line (or that
 * name when empty) parsed twice — counted, then into one _CRT_BLOCK ("stdargv.c" 117: argv then the strings);
 * __argc / __argv. */
uint32_t rl_crt_4c94a0(void)
{
    uint32_t argc, nch;
    IAT(DWORD (WINAPI *)(HMODULE, LPSTR, DWORD), I_GetModuleFileNameA)(NULL, (LPSTR)(uintptr_t)G_PGMNAME, 0x104);
    U32(G_PGMPTR) = G_PGMNAME;
    uint32_t cmd = U8(U32(G_ACMDLN)) != 0 ? U32(G_ACMDLN) : U32(G_PGMPTR);
    rl_crt_4c9570(cmd, 0, 0, (uint32_t)(uintptr_t)&argc, (uint32_t)(uintptr_t)&nch);
    uint32_t m = rl_crt_4bfa30(nch + 4 * argc, 2, S_STDARGV_C, 117);
    if (m == 0) rl_crt_4c35c0(8);
    rl_crt_4c9570(cmd, m, m + 4 * argc, (uint32_t)(uintptr_t)&argc, (uint32_t)(uintptr_t)&nch);
    U32(G_ARGC) = argc - 1;
    U32(G_ARGV) = m;
    return m;
}

/* Port of FUN_004c9360 (318 B) — _setenvp: _environ = an array (_CRT_BLOCK "stdenvp.c" 85) of copies ("stdenvp.c"
 * 97) of every entry not starting with '=' (the drive variables), the block from __crtGetEnvironmentStringsA freed
 * (_free_dbg, _CRT_BLOCK). EAX = the array's terminator slot. */
uint32_t rl_crt_4c9360(void)
{
    int n = 0;
    for (uint32_t s = U32(G_AENVPTR); U8(s) != 0; s += rl_crt_4bf620(s) + 1)
        if (U8(s) != '=') n++;
    uint32_t v = rl_crt_4bfa30(4u * (uint32_t)n + 4, 2, S_STDENVP_C, 85);
    U32(G_ENVIRON) = v;
    if (v == 0) rl_crt_4c35c0(9);
    for (uint32_t s = U32(G_AENVPTR), l; U8(s) != 0; s += l) {
        l = rl_crt_4bf620(s) + 1;
        if (U8(s) != '=') {
            U32(v) = rl_crt_4bfa30(l, 2, S_STDENVP_C, 97);
            if (U32(v) == 0) rl_crt_4c35c0(9);
            rl_crt_4c2080(U32(v), s);
            v += 4;
        }
    }
    rl_4c05a0((int)U32(G_AENVPTR), 2);
    U32(G_AENVPTR) = 0;
    U32(v) = 0;
    return v;
}

/* ================================================================ stdio init / flush */

/* Port of FUN_004cc5e0 (338 B) — __initstdio: _nstream (0 → 512, at least 20), __piob = _calloc_dbg(_nstream, 4)
 * ("_file.c" 132; failing: 20, line 135), its first 20 slots → _iob[i]; an _iob[0..2] whose fd has no handle (-1 or
 * 0) gets _file = -1. */
void rl_crt_4cc5e0(void)
{
    if (V32(G_NSTREAM) != 0) {
        if (V32(G_NSTREAM) < 20) V32(G_NSTREAM) = 20;
    } else
        V32(G_NSTREAM) = 512;
    U32(G_PIOB) = rl_crt_4bfea0((uint32_t)V32(G_NSTREAM), 4, 2, S_FILE_C, 132);
    if (U32(G_PIOB) == 0) {
        V32(G_NSTREAM) = 20;
        U32(G_PIOB) = rl_crt_4bfea0(20, 4, 2, S_FILE_C, 135);
        if (U32(G_PIOB) == 0) rl_crt_4c35c0(26);
    }
    for (uint32_t i = 0; i < 20; i++) U32(U32(G_PIOB) + 4 * i) = G_IOB + 32 * i;
    for (uint32_t j = 0; j < 3; j++)
        if (OSFHND(j) == 0xffffffffu || OSFHND(j) == 0)
            V32(G_IOB + 16 + 32 * j) = -1;
}

/* Port of FUN_004cd2f0 (186 B) — _flush: a write-mode stream with a buffer writes what is pending (_write); short →
 * _IOERR, -1; a read-write stream drops _IOWRT; the pointer back to the buffer, count 0. */
int rl_crt_4cd2f0(uint32_t f)
{
    int r = 0;
    if ((U32(f + 12) & 3) == 2 && (U32(f + 12) & 0x108)) {
        int n = (int)(U32(f) - U32(f + 8));
        if (n > 0) {
            if (rl_crt_4cc0b0(U32(f + 16), U32(f + 8), (uint32_t)n) == n) {
                if (U8(f + 12) & 0x80) U32(f + 12) &= ~2u;
            } else {
                U32(f + 12) |= 0x20;
                r = -1;
            }
        }
    }
    U32(f) = U32(f + 8);
    U32(f + 4) = 0;
    return r;
}

/* Port of FUN_004cd270 (126 B) — fflush: NULL = flsall(FFLUSHNULL); else _flush, and _commit for a commit-mode
 * stream (unported: never reached). */
int rl_crt_4cd270(uint32_t f)
{
    if (f == 0) return rl_crt_4cd3d0(0);
    if (rl_crt_4cd2f0(f) != 0) return -1;
    if ((U32(f + 12) & 0x4000) == 0) return 0;
    return rl_crt_4ce470(U32(f + 16)) != 0 ? -1 : 0;
}

/* Port of FUN_004cd3d0 (252 B) — flsall: every open stream (_IOREAD | _IOWRT | _IORW) of __piob; FLUSHALL (1) =
 * the count flushed, FFLUSHNULL (0) = -1 if a write stream failed. */
int rl_crt_4cd3d0(int mode)
{
    int count = 0, err = 0;
    for (int32_t i = 0; V32(G_NSTREAM) > i; i++) {
        uint32_t f = U32(U32(G_PIOB) + 4u * (uint32_t)i);
        if (f == 0 || (U8(f + 12) & 0x83) == 0) continue;
        if (mode == 1) {
            if (rl_crt_4cd270(f) != -1) count++;
        } else if (mode == 0 && (U8(f + 12) & 2) && rl_crt_4cd270(f) == -1)
            err = -1;
    }
    return mode == 1 ? count : err;
}

/* Port of FUN_004cd3b0 (26 B) — _flushall: flsall(FLUSHALL). */
int rl_crt_4cd3b0(void) { return rl_crt_4cd3d0(1); }

/* Port of FUN_004cc740 (36 B) — __endstdio (the xp terminator): _flushall; _fcloseall when _exitflag (unported:
 * exit() never sets it). EAX = _exitflag or _fcloseall's result. */
int rl_crt_4cc740(void)
{
    rl_crt_4cd3b0();
    int r = (int)U8(G_EXITFLAG);
    if (U8(G_EXITFLAG) != 0) return (int)rl_crt_4cd1b0();
    return r;
}

/* ================================================================ onexit / exit / start */

/* Port of FUN_004cd4f0 (181 B) — _onexit: the table (__onexitbegin .. __onexitend) grows by 16 bytes when full
 * (_realloc_dbg, unported: the game registers one function); the function appended. */
uint32_t rl_crt_4cd4f0(uint32_t fn)
{
    if (rl_crt_4c09f0(U32(G_ONEXITBEG), 2) < U32(G_ONEXITEND) - U32(G_ONEXITBEG) + 4) {
        uint32_t sz = rl_crt_4c09f0(U32(G_ONEXITBEG), 2);
        uint32_t n = rl_crt_4bff40(U32(G_ONEXITBEG), sz + 16, 2, S_ONEXIT_C, 104);
        if (n == 0) return 0;
        U32(G_ONEXITEND) = n + ((U32(G_ONEXITEND) - U32(G_ONEXITBEG)) & 0xfffffffcu);
        U32(G_ONEXITBEG) = n;
    }
    U32(U32(G_ONEXITEND)) = fn;
    U32(G_ONEXITEND) += 4;
    return fn;
}

/* Port of FUN_004cd5b0 (48 B) — atexit: _onexit; 0 / -1. */
int rl_crt_4cd5b0(uint32_t fn) { return rl_crt_4cd4f0(fn) != 0 ? 0 : -1; }

/* Port of FUN_004cd5e0 (85 B) — __onexitinit (an xi initializer): the onexit table, 32 slots (_CRT_BLOCK "onexit.c"
 * 182), its first slot 0, begin = end. */
uint32_t rl_crt_4cd5e0(void)
{
    U32(G_ONEXITBEG) = rl_crt_4bfa30(0x80, 2, S_ONEXIT_C, 182);
    if (U32(G_ONEXITBEG) == 0) rl_crt_4c35c0(24);
    U32(U32(G_ONEXITBEG)) = 0;
    U32(G_ONEXITEND) = U32(G_ONEXITBEG);
    return U32(G_ONEXITBEG);
}

/* Port of FUN_004cc950 (45 B) — the C++ EH runtime's filter install: atexit(__CxxRestoreUnhandledExceptionFilter
 * 0x4cca20), SetUnhandledExceptionFilter(__CxxUnhandledExceptionFilter 0x4cc980) — both retail's addresses, as
 * retail passes them (under the standalone 0x4cca20 is a thunk to rl_crt_4cca20; 0x4cc980 is only stored) — the
 * previous filter kept in 0x4e5d34. */
uint32_t rl_crt_4cc950(void)
{
    rl_crt_4cd5b0(VA_CXX_RESTORE_UEF);
    U32(G_UEF) = (uint32_t)(uintptr_t)IAT(LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *)(LPTOP_LEVEL_EXCEPTION_FILTER),
                                          I_SetUnhandledExceptionFilter)(
        (LPTOP_LEVEL_EXCEPTION_FILTER)(uintptr_t)VA_CXX_UEF);
    return U32(G_UEF);
}

/* Port of FUN_004cc930 (21 B) — the xc initializer: 0x4cc950. */
uint32_t rl_crt_4cc930(void) { return rl_crt_4cc950(); }

/* Port of FUN_004cca20 (28 B) — __CxxRestoreUnhandledExceptionFilter (an onexit function): the saved filter back. */
void rl_crt_4cca20(void)
{
    IAT(LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *)(LPTOP_LEVEL_EXCEPTION_FILTER), I_SetUnhandledExceptionFilter)(
        (LPTOP_LEVEL_EXCEPTION_FILTER)(uintptr_t)U32(G_UEF));
}

/* Port of FUN_004bf870 (49 B) — _initterm: every non-null function pointer of [first, last), in order (called by
 * address: retail's table holds retail VAs — the standalone's thunks). */
void rl_crt_4bf870(uint32_t first, uint32_t last)
{
    for (; last > first; first += 4)
        if (U32(first) != 0) ((void (__cdecl *)(void))(uintptr_t)U32(first))();
}

/* Port of FUN_004bf6a0 (66 B) — _cinit: _FPinit when linked (0x5cee5c: 0), the C initializers (xi: __initstdio,
 * __onexitinit), the C++ ones (xc: 0x4cc930). */
void rl_crt_4bf6a0(void)
{
    if (U32(G_FPINIT) != 0) ((void (__cdecl *)(void))(uintptr_t)U32(G_FPINIT))();
    rl_crt_4bf870(T_XI_A, T_XI_Z);
    rl_crt_4bf870(T_XC_A, T_XC_Z);
}

/* Port of FUN_004bf770 (251 B) — doexit: a second exit terminates the process; _C_Termination_Done, _exitflag; not
 * quick: the onexit table in reverse, the pre-terminators (xp: __endstdio); the terminators (xt: none); the leak dump
 * when _CRTDBG_LEAK_CHECK_DF (unported: off); not returning to the caller: _C_Exit_Done, ExitProcess. */
void rl_crt_4bf770(int code, int quick, int retcaller)
{
    if (V32(G_EXITDONE) == 1)
        IAT(BOOL (WINAPI *)(HANDLE, UINT), I_TerminateProcess)(IAT(HANDLE (WINAPI *)(void), I_GetCurrentProcess)(),
                                                               (UINT)code);
    V32(G_TERMDONE) = 1;
    U8(G_EXITFLAG) = (uint8_t)retcaller;
    if (quick == 0) {
        if (U32(G_ONEXITBEG) != 0) {
            uint32_t p = U32(G_ONEXITEND);
            while (U32(G_ONEXITBEG) <= (p -= 4))
                if (U32(p) != 0) ((void (__cdecl *)(void))(uintptr_t)U32(p))();
        }
        rl_crt_4bf870(T_XP_A, T_XP_Z);
    }
    rl_crt_4bf870(T_XT_A, T_XT_Z);
    if (V32(G_LEAKSDONE) == 0 && (rl_crt_4c1070(-1) & 0x20)) {
        V32(G_LEAKSDONE) = 1;
        rl_crt_4c19b0();
    }
    if (retcaller == 0) {
        V32(G_EXITDONE) = 1;
        IAT(void (WINAPI *)(UINT), I_ExitProcess)((UINT)code);
    }
}

/* Port of FUN_004bf6f0 (27 B) — exit: doexit(code, 0, 0). */
void rl_crt_4bf6f0(int code) { rl_crt_4bf770(code, 0, 0); }

/* Port of FUN_004c3370 (580 B) — start, the CRT entry: the version globals (_osver / _winver / _winmajor /
 * _winminor from GetVersion), _heap_init (fail: _amsg_exit(_RT_HEAPINIT)), _ioinit, __initmbctable, _acmdln =
 * GetCommandLineA, _aenvptr = __crtGetEnvironmentStringsA (either missing: exit(-1)), _setargv, _setenvp, _cinit;
 * WinMain's command line = past the program name (quoted with DBCS leads, or up to a blank) and the blanks after it,
 * nShowCmd = STARTUPINFO's wShowWindow (STARTF_USESHOWWINDOW) or SW_SHOWDEFAULT, GetModuleHandleA(NULL); exit(WinMain).
 * The __try / _XcptFilter frame around it is not modelled (no exception reaches it). Never returns. */
void rl_crt_4c3370(void)
{
    DWORD v = IAT(DWORD (WINAPI *)(void), I_GetVersion)();
    V32(G_WINMAJOR) = (v >> 8) & 0xff;
    V32(G_WINMINOR) = v & 0xff;
    V32(G_WINVER) = (int32_t)(((v >> 8) & 0xff) + ((v & 0xff) << 8));
    V32(G_OSVER) = (int32_t)(v >> 16);
    if (rl_crt_4c4480() == 0) rl_crt_4c35c0(28);
    rl_crt_4c9e50();
    rl_crt_4c8e90();
    U32(G_ACMDLN) = (uint32_t)(uintptr_t)IAT(LPSTR (WINAPI *)(void), I_GetCommandLineA)();
    U32(G_AENVPTR) = rl_crt_4c9bf0();
    if (U32(G_AENVPTR) == 0 || U32(G_ACMDLN) == 0) rl_crt_4bf6f0(-1);
    rl_crt_4c94a0();
    rl_crt_4c9360();
    rl_crt_4bf6a0();
    uint32_t p = U32(G_ACMDLN);
    if (U8(p) == '"') {
        while (U8(++p) != '"' && U8(p) != 0)
            if (rl_crt_4c9260(U8(p)) != 0) p++;
        if (U8(p) == '"') p++;
    } else
        while (U8(p) > ' ') p++;
    while (U8(p) != 0 && U8(p) <= ' ') p++;
    STARTUPINFOA si;
    si.dwFlags = 0;
    IAT(void (WINAPI *)(STARTUPINFOA *), I_GetStartupInfoA)(&si);
    int show = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
    HINSTANCE inst = IAT(HMODULE (WINAPI *)(LPCSTR), I_GetModuleHandleA)(NULL);
    rl_crt_4bf6f0(rl_40e9ab(inst, NULL, (LPSTR)(uintptr_t)p, show));
}
