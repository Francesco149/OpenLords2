/* rl_crt_dbgheap.c — retail's statically linked MSVC 4.x DEBUG CRT: the debug heap's check / realloc / leak-dump paths and the sbh internals they reach (_CrtCheckMemory, _realloc_dbg, _CrtDumpMemoryLeaks, __sbh_decommit_pages / resize / heap_check, _callnewh).
 * No scenario reaches these (error / dead paths): each is ported from retail's code (IDA pseudocode + disassembly) and
 * verified by synthetic carriers (tests/probes/crt-dbgheap.probe, `rhost.py probe`). Conventions as rl_crt.c: retail's
 * globals by VA, every OS call through retail's IAT slot, an unported callee by bare VA (engine_retail_call).
 * docs/findings/crt-gap-dbgheap.md: per function, quirks and probe coverage. */
#include "rl_crt_dbgheap.h"
#include "rl_crt.h"
#include "rl_io.h"
#include "engine_retail_call.h"

#include <string.h>
#include <windows.h>

#define V32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define U32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define U16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define U8(va)  (*(volatile uint8_t *)(uintptr_t)(va))
#define IAT(T, slot) ((T)(uintptr_t)*(volatile uint32_t *)(uintptr_t)(slot))
#define RETAIL(va, ...) engine_retail_call((va), __VA_ARGS__)
#define PV(p) ((uint32_t)(uintptr_t)(p))

/* ---------------------------------------------------------------- retail's CRT globals */
#define G_ERRNO      0x4e4530u   /* errno */
#define G_DOSERRNO   0x4e4534u   /* _doserrno */
#define G_DBGFLAG    0x4e4580u   /* _crtDbgFlag */
#define G_REQCURR    0x4e4584u   /* _lRequestCurr */
#define G_BREAKALLOC 0x4e4588u   /* _crtBreakAlloc */
#define G_NOMANSLAND 0x4e458cu   /* 0xfd */
#define G_DEADLAND   0x4e4590u   /* 0xdd */
#define G_CLEANLAND  0x4e4594u   /* 0xcd */
#define G_BLOCKNAMES 0x4e4598u   /* __szBlockUseName[5]: "Free" "Normal" "CRT" "Ignore" "Client" */
#define G_NEWMODE    0x4e45d4u   /* _newmode */
#define G_ALLOCHOOK  0x4e4610u   /* _pfnAllocHook */
#define G_SBH_HEAD   0x4e4618u   /* the first (static) region header; +4 = the ring's last header */
#define G_SBH_HEAPHDR 0x4e4e28u  /* nonzero: further region headers from the heap */
#define G_SBH_ROVER  0x4e4e2cu   /* the region header the next search starts at */
#define G_SBH_FREEPG 0x4e4e30u   /* fully free pages */
#define G_SBH_THRESH 0x4e4e34u   /* __sbh_threshold (0x1e0) */
#define G_PCTYPE     0x4e4e38u   /* _pctype */
#define G_MBCURMAX   0x4e5044u   /* __mb_cur_max */
#define G_PLAST      0x4e5b20u   /* _pLastBlock */
#define G_TOTALLOC   0x4e5b24u   /* _lTotalAlloc */
#define G_PFIRSTBLK  0x4e5b28u   /* _pFirstBlock */
#define G_CURALLOC   0x4e5b2cu   /* _lCurAlloc */
#define G_MAXALLOC   0x4e5b30u   /* _lMaxAlloc */
#define G_PNHHEAP    0x4e5b70u   /* _pnhHeap (the new handler) */
#define G_CRTHEAP    0x5cee44u   /* _crtheap */
#define G_DUMPCLIENT 0x5cee50u   /* _pfnDumpClient */

/* retail's .rdata strings */
#define S_BADTYPE      0x4d014cu   /* "Error: memory allocation: bad memory block type.\n" */
#define S_PCT_S        0x4d01a4u   /* "%s" */
#define S_DBGHEAP_C    0x4d0204u   /* "dbgheap.c" */
#define S_CHECKMEM     0x4d0210u   /* "_CrtCheckMemory()" */
#define S_FIRSTOLD     0x4d0224u   /* "_pFirstBlock == pOldBlock" */
#define S_LASTOLD      0x4d0240u   /* "_pLastBlock == pOldBlock" */
#define S_FREALLOC     0x4d025cu   /* "fRealloc || (!fRealloc && pNewBlock == pOldBlock)" */
#define S_TYPEOLD      0x4d0290u   /* "_BLOCK_TYPE(pOldBlock->nBlockUse)==_BLOCK_TYPE(nBlockUse)" */
#define S_IGNOREOLD    0x4d02ccu   /* "pOldBlock->nLine == IGNORE_LINE && pOldBlock->lRequest == IGNORE_REQ" */
#define S_VALIDHEAP    0x4d0314u   /* "_CrtIsValidHeapPointer(pUserData)" */
#define S_TOOLARGE     0x4d0338u   /* "Allocation too large or negative: %u bytes.\n" */
#define S_HOOKREALLOC  0x4d0368u   /* "Client hook re-allocation failure.\n" */
#define S_HOOKREALLOCAT 0x4d038cu  /* "Client hook re-allocation failure at file %hs line %d.\n" */
#define S_DMG_AFTER    0x4d0454u   /* "DAMAGE: after %hs block (#%d) at 0x%08X.\n" */
#define S_DMG_BEFORE   0x4d0480u   /* "DAMAGE: before %hs block (#%d) at 0x%08X.\n" */
#define S_LOCATED      0x4d052cu   /* "%hs located at 0x%08X is %u bytes long.\n" */
#define S_ALLOCATED    0x4d0558u   /* "%hs allocated at file %hs(%d).\n" */
#define S_DMG_FREE     0x4d0578u   /* "DAMAGE: on top of Free block at 0x%08X.\n" */
#define S_DAMAGED      0x4d05a4u   /* "DAMAGED" */
#define S_HEAPCHK_UNK  0x4d05acu   /* "_heapchk fails with unknown return value!\n" */
#define S_HEAPCHK_PTR  0x4d05d8u   /* "… _HEAPBADPTR.\n" (-6) */
#define S_HEAPCHK_END  0x4d05fcu   /* "… _HEAPBADEND.\n" (-5) */
#define S_HEAPCHK_NODE 0x4d0620u   /* "… _HEAPBADNODE.\n" (-4) */
#define S_HEAPCHK_BEG  0x4d0644u   /* "… _HEAPBADBEGIN.\n" (-3) */
#define S_BADBLOCK     0x4d0668u   /* "Bad memory block found at 0x%08X.\n" */
#define S_CKPT_NULL    0x4d068cu   /* "_CrtMemCheckPoint: NULL state pointer.\n" */
#define S_DUMP_DONE    0x4d06dcu   /* "Object dump complete.\n" */
#define S_DUMP_CRT     0x4d06f4u   /* "crt block at 0x%08X, subtype %x, %u bytes long.\n" */
#define S_DUMP_NORMAL  0x4d0728u   /* "normal block at 0x%08X, %u bytes long.\n" */
#define S_DUMP_CLIENT  0x4d0750u   /* "client block at 0x%08X, subtype %x, %u bytes long.\n" */
#define S_DUMP_REQ     0x4d0784u   /* "{%ld} " */
#define S_DUMP_FILE    0x4d078cu   /* "%hs(%d) : " */
#define S_DUMP_FILEERR 0x4d0798u   /* "#File Error#(%d) : " */
#define S_DUMPING      0x4d07acu   /* "Dumping objects ->\n" */
#define S_DATA         0x4d07c0u   /* " Data: <%s> %s\n" */
#define S_HEX2         0x4d07d0u   /* "%.2X " */
#define S_LEAKS        0x4d07d8u   /* "Detected memory leaks!\n" */

/* the IAT slots */
#define I_GetLastError 0x5cf3e8u
#define I_VirtualFree  0x5cf418u
#define I_HeapReAlloc  0x5cf41cu
#define I_HeapFree     0x5cf424u
#define I_HeapAlloc    0x5cf43cu
#define I_HeapValidate 0x5cf468u

/* unported callees in other partitions (bare VAs: the ledger counts FUN_ tokens) */
#define VA_CRTDBGREPORT 0x4c3950u   /* _CrtDbgReport (partition A) */
#define VA_ISCTYPE      0x4c5710u   /* _isctype (partition C) */

static void int3(void) { __asm__ volatile("int3"); }
/* _CrtDbgReport(nargs, type, file, line, module, format, …) == 1: the caller breaks */
#define REPORT(...) (RETAIL(VA_CRTDBGREPORT, __VA_ARGS__) == 1)
#define HEAP() ((HANDLE)(uintptr_t)U32(G_CRTHEAP))
/* the sbh region header (rl_crt.c): [516] the region, +16 the free-paragraph counts, +1040 the failed requests */
#define HDR32(h, i) U32((h) + 4u * (i))
#define HDR_FREE(h, i) U8((h) + 16u + (uint32_t)(i))
#define HDR_FAIL(h, i) U8((h) + 1040u + (uint32_t)(i))
#define HDR_REGION 516
#define BLOCK_OK(t) (((t) & 0xffff) == 4 || (t) == 1 || ((t) & 0xffff) == 2 || (t) == 3)

/* a retail function pointer retail calls (cdecl): by address, as retail does */
static uint32_t call_ptr1(uint32_t fn, uint32_t a)
{
    return ((uint32_t (__cdecl *)(uint32_t))(uintptr_t)fn)(a);
}
static void call_ptr2(uint32_t fn, uint32_t a, uint32_t b)
{
    ((void (__cdecl *)(uint32_t, uint32_t))(uintptr_t)fn)(a, b);
}

/* ================================================================ the base heap */

/* Port of FUN_004c3220 (334 B) — memcpy (memcpy.asm, the memmove body): a destination above an overlapping source
 * copies backwards, else forwards (dword moves; any lead / tail split gives the same bytes). EAX = the destination.
 * Quirk: the backward unaligned path takes its lead count from EDX, never set there — only the alignment differs. */
uint32_t rl_crt_4c3220(uint32_t d, uint32_t s, uint32_t n)
{
    volatile uint8_t *dp = (volatile uint8_t *)(uintptr_t)d;
    const volatile uint8_t *sp = (const volatile uint8_t *)(uintptr_t)s;
    if (d > s && d < s + n)
        while (n--) dp[n] = sp[n];
    else
        for (uint32_t i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}

/* Port of FUN_004c3650 (62 B) — _callnewh: the new handler _pnhHeap (0x4e5b70, called by address) with the size;
 * 1 = it freed something (retry), 0 = no handler / it failed. */
int rl_crt_4c3650(uint32_t size)
{
    if (U32(G_PNHHEAP) == 0 || call_ptr1(U32(G_PNHHEAP), size) == 0) return 0;
    return 1;
}

/* Port of FUN_004c36c0 (150 B) — _nh_malloc_base: over 0xffffffe0 → 0; 0 → 1 byte; _heap_alloc_base, retried while
 * `nh` and _callnewh succeeds. */
uint32_t rl_crt_4c36c0(uint32_t size, int nh)
{
    if (size > 0xffffffe0u) return 0;
    if (size == 0) size = 1;
    for (;;) {
        uint32_t p = size <= 0xffffffe0u ? rl_crt_4c3760(size) : 0;
        if (p != 0 || nh == 0) return p;
        if (rl_crt_4c3650(size) == 0) return 0;
    }
}

/* Port of FUN_004c3690 (34 B) — _malloc_base: _nh_malloc_base(size, _newmode). */
uint32_t rl_crt_4c3690(uint32_t size) { return rl_crt_4c36c0(size, V32(G_NEWMODE)); }

/* Port of FUN_004c4090 (195 B) — _expand_base: rounded to 16 (0 → 16; over 0xffffffe0 → 0); a small block resizes in
 * place (__sbh_resize_block) when the new size is within __sbh_threshold, else 0; any other block
 * HeapReAlloc(HEAP_REALLOC_IN_PLACE_ONLY). */
uint32_t rl_crt_4c4090(uint32_t p, uint32_t n)
{
    if (n > 0xffffffe0u) return 0;
    n = n != 0 ? (n + 15) & 0xfffffff0u : 16;
    uint32_t hdr, page, map = rl_crt_4c49d0(p, &hdr, &page);
    if (map != 0) {
        uint32_t r = 0;
        if (n <= U32(G_SBH_THRESH) && rl_crt_4c52e0(hdr, page, map, n >> 4) != 0) r = p;
        return r;
    }
    return (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(HANDLE, DWORD, LPVOID, SIZE_T), I_HeapReAlloc)(
        HEAP(), HEAP_REALLOC_IN_PLACE_ONLY, (LPVOID)(uintptr_t)p, n);
}

/* Port of FUN_004c4160 (518 B) — _realloc_base: NULL → _malloc_base; 0 bytes → _free_base, 0; rounded to 16. A small
 * block: under __sbh_threshold (strictly) resized in place, else a new small block, the old one's bytes copied
 * (min(new, old map run × 16)) and freed; failing that (or at / over the threshold) HeapAlloc + copy + free. Any other
 * block HeapReAlloc. A failure retries while _newmode and _callnewh succeeds. */
uint32_t rl_crt_4c4160(uint32_t p, uint32_t n)
{
    if (p == 0) return rl_crt_4c3690(n);
    if (n == 0) { rl_crt_4c4370(p); return 0; }
    if (n <= 0xffffffe0u) n = (n + 15) & 0xfffffff0u;
    for (;;) {
        uint32_t r = 0;
        if (n <= 0xffffffe0u) {
            uint32_t hdr, page, map = rl_crt_4c49d0(p, &hdr, &page);
            if (map != 0) {
                if (n < U32(G_SBH_THRESH)) {
                    if (rl_crt_4c52e0(hdr, page, map, n >> 4) != 0)
                        r = p;
                    else if ((r = rl_crt_4c4b10(n >> 4)) != 0) {
                        uint32_t old = (uint32_t)U8(map) << 4;
                        rl_crt_4c3220(r, p, n < old ? n : old);
                        rl_crt_4c4a80(hdr, page, map);
                    }
                }
                if (r == 0) {
                    r = (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(HANDLE, DWORD, SIZE_T), I_HeapAlloc)(HEAP(), 0, n);
                    if (r != 0) {
                        uint32_t old = (uint32_t)U8(map) << 4;
                        rl_crt_4c3220(r, p, n < old ? n : old);
                        rl_crt_4c4a80(hdr, page, map);
                    }
                }
            } else
                r = (uint32_t)(uintptr_t)IAT(LPVOID (WINAPI *)(HANDLE, DWORD, LPVOID, SIZE_T), I_HeapReAlloc)(
                    HEAP(), 0, (LPVOID)(uintptr_t)p, n);
        }
        if (r != 0 || V32(G_NEWMODE) == 0) return r;
        if (rl_crt_4c3650(n) == 0) return 0;
    }
}

/* Port of FUN_004c43e0 (120 B) — _heapchk: _HEAPOK (-2); the sbh check failing → _HEAPBADNODE (-4); HeapValidate
 * failing → -4, except ERROR_CALL_NOT_IMPLEMENTED (Win32s: _doserrno 0x78, errno ENOSYS 40, the result kept). */
int rl_crt_4c43e0(void)
{
    int r = -2;
    if (rl_crt_4c54a0() < 0) r = -4;
    if (!IAT(BOOL (WINAPI *)(HANDLE, DWORD, LPCVOID), I_HeapValidate)(HEAP(), 0, NULL)) {
        if (IAT(DWORD (WINAPI *)(void), I_GetLastError)() == 0x78) {
            V32(G_DOSERRNO) = 0x78;
            V32(G_ERRNO) = 0x28;
        } else
            r = -4;
    }
    return r;
}

/* ================================================================ the small-block heap (sbheap.c) */

/* Port of FUN_004c47c0 (132 B) — __sbh_release_region: the 4 MB reservation released; the rover moves to the previous
 * header when it is this one; a heap header is unlinked and HeapFree'd, the static one only clears 0x4e4e28 (quirk:
 * the static header stays linked, its region pointer stale). */
void rl_crt_4c47c0(uint32_t h)
{
    IAT(BOOL (WINAPI *)(LPVOID, SIZE_T, DWORD), I_VirtualFree)((LPVOID)(uintptr_t)HDR32(h, HDR_REGION), 0, MEM_RELEASE);
    if (U32(G_SBH_ROVER) == h) U32(G_SBH_ROVER) = HDR32(h, 1);
    if (h != G_SBH_HEAD) {
        U32(HDR32(h, 1)) = HDR32(h, 0);
        U32(HDR32(h, 0) + 4) = HDR32(h, 1);
        IAT(BOOL (WINAPI *)(HANDLE, DWORD, LPVOID), I_HeapFree)(HEAP(), 0, (LPVOID)(uintptr_t)h);
    } else
        V32(G_SBH_HEAPHDR) = 0;
}

/* Port of FUN_004c4850 (376 B) — __sbh_decommit_pages: from the ring's last header backwards, each committed region's
 * wholly free pages (count 0xf0) from the top down are decommitted (count 0xff, the free-page tally down, the first
 * uncommitted index lowered) until `n` are done; a region left with no committed page is released. Quirk: a header
 * whose region is 0 is not stepped past (retail would spin on it unless it is the first visited). */
void rl_crt_4c4850(int n)
{
    uint32_t h = U32(G_SBH_HEAD + 4);
    do {
        if (HDR32(h, HDR_REGION) != 0) {
            int cnt = 0;
            int32_t i = 1023;
            for (uint32_t f = h + 16 + 1023; i >= 0; i--, f--) {
                if (U8(f) == 0xf0 &&
                    IAT(BOOL (WINAPI *)(LPVOID, SIZE_T, DWORD), I_VirtualFree)(
                        (LPVOID)(uintptr_t)(HDR32(h, HDR_REGION) + ((uint32_t)i << 12)), 0x1000, MEM_DECOMMIT)) {
                    U8(f) = 0xff;
                    V32(G_SBH_FREEPG)--;
                    if (V32(h + 12) == -1 || V32(h + 12) > i) V32(h + 12) = i;
                    cnt++;
                    if (--n == 0) break;
                }
            }
            uint32_t prev = h;
            h = HDR32(h, 1);
            if (cnt != 0 && U8(prev + 16) == 0xff) {
                int j = 1;
                while (j < 1024 && U8(prev + 16 + (uint32_t)j) == 0xff) j++;
                if (j == 1024) rl_crt_4c47c0(prev);
            }
        }
    } while (U32(G_SBH_HEAD + 4) != h && n > 0);
}

/* Port of FUN_004c52e0 (439 B) — __sbh_resize_block: a smaller run just shrinks the map byte (the page's free count
 * up, its failed-request byte reset; the page rover untouched); a larger one takes the free paragraphs right after
 * when they all are free and inside the page (the rover moves past the block when it pointed into the taken run). 1 =
 * resized, 0 = not (an equal size is 0 too). */
int rl_crt_4c52e0(uint32_t hdr, uint32_t page, uint32_t map, uint32_t paras)
{
    uint32_t old = U8(map);
    int32_t idx = (int32_t)(page - HDR32(hdr, HDR_REGION)) >> 12;
    if (old > paras) {
        U8(map) = (uint8_t)paras;
        HDR_FREE(hdr, idx) = (uint8_t)(HDR_FREE(hdr, idx) - paras + old);
        HDR_FAIL(hdr, idx) = 0xf1;
        return 1;
    }
    if (old < paras && map + paras <= page + 248) {
        uint32_t q = map + old, end = map + paras;
        while (end > q && U8(q) == 0) q++;
        if (end == q) {
            U8(map) = (uint8_t)paras;
            if (U32(page) >= map && U32(page) < end) {
                if (page + 248 > end) {
                    U32(page) = end;
                    uint32_t k = 0;
                    while (U8(end) == 0) { k++; end++; }
                    U32(page + 4) = k;
                } else {
                    U32(page) = page + 8;
                    U32(page + 4) = 0;
                }
            }
            HDR_FREE(hdr, idx) = (uint8_t)(HDR_FREE(hdr, idx) - paras + old);
            return 1;
        }
    }
    return 0;
}

/* Port of FUN_004c54a0 (622 B) — __sbh_heap_check: every region of the ring: an uncommitted page (0xff) — the first
 * one must be the header's first-uncommitted index (-1); a committed page: rover inside the map (-2), the 0xff
 * sentinel (-3), each block's free run before it below the failed-request byte (-4), the cached run at the rover not
 * over the run before the next block (-5), a block's map bytes past its first all 0 (-6), the free count = the free
 * map bytes (-7), the rover on a run start (-8); the rover header in the ring (-9). 0 = sound. */
int rl_crt_4c54a0(void)
{
    int seen = 0;
    uint32_t h = G_SBH_HEAD;
    do {
        if (U32(G_SBH_ROVER) == h) seen++;
        if (HDR32(h, HDR_REGION) != 0) {
            int32_t unc = 0;
            uint32_t pg = HDR32(h, HDR_REGION);
            for (int32_t i = 0; i < 1024; i++, pg += 0x1000) {
                if (HDR_FREE(h, i) == 0xff) {
                    if (unc == 0 && V32(h + 12) != i) return -1;
                    unc++;
                    continue;
                }
                if (U32(pg) >= pg + 248) return -2;
                if (U8(pg + 248) != 0xff) return -3;
                int32_t k = 0, rover = 0, nfree = 0, run = 0;
                while (k < 240) {
                    if (pg + (uint32_t)k + 8 == U32(pg)) rover++;
                    if (U8(pg + (uint32_t)k + 8) == 0) {
                        nfree++;
                        run++;
                        k++;
                        continue;
                    }
                    if ((int32_t)HDR_FAIL(h, i) <= run) return -4;
                    if (rover == 1) {
                        if (V32(pg + 4) > run) return -5;
                        rover++;
                    }
                    run = 0;
                    int32_t j;
                    for (j = k + 1; k + (int32_t)U8(pg + (uint32_t)k + 8) > j; j++)
                        if (U8(pg + (uint32_t)j + 8) != 0) return -6;
                    k = j;
                }
                if ((int32_t)HDR_FREE(h, i) != nfree) return -7;
                if (rover == 0) return -8;
            }
        }
        h = HDR32(h, 0);
    } while (h != G_SBH_HEAD);
    return seen != 0 ? 0 : -9;
}

/* ================================================================ the debug heap (dbgheap.c) */

/* Port of FUN_004bff80 (1409 B) — realloc_help: NULL → _malloc_dbg; 0 bytes with fRealloc → _free_dbg, 0. The
 * _CRTDBG_CHECK_ALWAYS_DF check, the break-on-request int 3, the client hook (_HOOK_REALLOC), the size limit
 * (0xffffffdb), the block-type and header checks (each a report); then _realloc_base (fRealloc) or _expand_base on the
 * header with size + 0x24: _lRequestCurr++, the totals (not for an _IGNORE_BLOCK), the new tail filled 0xcd, the
 * no-man's-land after, file / line / request (not for an _IGNORE_BLOCK), the size; a moved block relinked at the head
 * of the block list. EAX = the data, 0 = failure. Quirk: a _CRT_BLOCK asked as _NORMAL_BLOCK becomes _CRT_BLOCK in the
 * whole dword (the subtype dropped) before the type compare, which looks at the low words only. */
uint32_t rl_crt_4bff80(uint32_t p, uint32_t size, int use, uint32_t file, int line, int frealloc)
{
    int ignore = 0;
    if (p == 0) return rl_crt_4bfa30(size, use, file, line);
    if (frealloc != 0 && size == 0) {
        rl_4c05a0((int)p, use);
        return 0;
    }
    if ((V32(G_DBGFLAG) & 4) && rl_crt_4c0cf0() == 0 && REPORT(5, 2, S_DBGHEAP_C, 569, 0, S_CHECKMEM))
        int3();
    int32_t req = V32(G_REQCURR);
    if (req == V32(G_BREAKALLOC))
        int3();
    if (rl_crt_alloc_hook(U32(G_ALLOCHOOK), 2, p, size, use, req, file, line) == 0) {
        if (file != 0) {
            if (REPORT(7, 0, 0, 0, 0, S_HOOKREALLOCAT, file, line)) int3();
        } else if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_HOOKREALLOC))
            int3();
        return 0;
    }
    if (size > 0xffffffdbu) {
        if (REPORT(6, 1, 0, 0, 0, S_TOOLARGE, size)) int3();
        return 0;
    }
    if (use != 1 && (use & 0xffff) != 4 && (use & 0xffff) != 2 && REPORT(6, 1, 0, 0, 0, S_PCT_S, S_BADTYPE))
        int3();
    if (rl_crt_4c1170(p) == 0 && REPORT(5, 2, S_DBGHEAP_C, 609, 0, S_VALIDHEAP))
        int3();
    uint32_t b = p - 32;
    if (U32(b + 20) == 3) ignore = 1;
    if (ignore) {
        if ((U32(b + 12) != 0xfedcbabcu || U32(b + 24) != 0) && REPORT(5, 2, S_DBGHEAP_C, 619, 0, S_IGNOREOLD))
            int3();
    } else {
        if ((U32(b + 20) & 0xffff) == 2 && (use & 0xffff) == 1) use = 2;
        if (((U32(b + 20) ^ (uint32_t)use) & 0xffff) != 0 && REPORT(5, 2, S_DBGHEAP_C, 626, 0, S_TYPEOLD))
            int3();
    }
    uint32_t nb = frealloc != 0 ? rl_crt_4c4160(b, size + 36) : rl_crt_4c4090(b, size + 36);
    if (nb == 0) return 0;
    V32(G_REQCURR)++;
    if (!ignore) {
        U32(G_TOTALLOC) -= U32(nb + 16);
        U32(G_TOTALLOC) += size;
        U32(G_CURALLOC) -= U32(nb + 16);
        U32(G_CURALLOC) += size;
        if (U32(G_MAXALLOC) < U32(G_CURALLOC)) U32(G_MAXALLOC) = U32(G_CURALLOC);
    }
    uint32_t d = nb + 32;
    if (U32(nb + 16) < size) rl_crt_4c2200(d + U32(nb + 16), U8(G_CLEANLAND), size - U32(nb + 16));
    rl_crt_4c2200(d + size, U8(G_NOMANSLAND), 4);
    if (!ignore) {
        U32(nb + 8) = file;
        U32(nb + 12) = (uint32_t)line;
        U32(nb + 24) = (uint32_t)req;
    }
    U32(nb + 16) = size;
    if (frealloc == 0 && nb != b && REPORT(5, 2, S_DBGHEAP_C, 680, 0, S_FREALLOC))
        int3();
    if (nb == b || ignore) return d;
    if (U32(nb) != 0)
        U32(U32(nb) + 4) = U32(nb + 4);
    else {
        if (U32(G_PLAST) != b && REPORT(5, 2, S_DBGHEAP_C, 695, 0, S_LASTOLD)) int3();
        U32(G_PLAST) = U32(nb + 4);
    }
    if (U32(nb + 4) != 0)
        U32(U32(nb + 4)) = U32(nb);
    else {
        if (U32(G_PFIRSTBLK) != b && REPORT(5, 2, S_DBGHEAP_C, 706, 0, S_FIRSTOLD)) int3();
        U32(G_PFIRSTBLK) = U32(nb);
    }
    if (U32(G_PFIRSTBLK) != 0)
        U32(U32(G_PFIRSTBLK) + 4) = nb;
    else
        U32(G_PLAST) = nb;
    U32(nb) = U32(G_PFIRSTBLK);
    U32(nb + 4) = 0;
    U32(G_PFIRSTBLK) = nb;
    return d;
}

/* Port of FUN_004bff40 (55 B) — _realloc_dbg: realloc_help(…, fRealloc 1). */
uint32_t rl_crt_4bff40(uint32_t p, uint32_t size, int use, uint32_t file, int line)
{
    return rl_crt_4bff80(p, size, use, file, line, 1);
}

/* Port of FUN_004c0cf0 (894 B) — _CrtCheckMemory: 1 without _CRTDBG_ALLOC_MEM_DF; _heapchk other than _HEAPOK /
 * _HEAPEMPTY → its report, 0; then every block of the list: the no-man's-land before / after (0xfd), a _FREE_BLOCK's
 * dead-land fill (0xdd), each damage reported with the block's name ("DAMAGED" for a bad type), where it was
 * allocated and how long. 1 = sound. Quirk: _heapchk only ever returns -2 / -4 — the -3 / -5 / -6 / default reports
 * are dead. */
int rl_crt_4c0cf0(void)
{
    int ok = 1;
    if ((V32(G_DBGFLAG) & 1) == 0) return 1;
    int hc = rl_crt_4c43e0();
    if (hc != -1 && hc != -2) {
        uint32_t s;
        switch (hc) {
        case -3: s = S_HEAPCHK_BEG; break;
        case -4: s = S_HEAPCHK_NODE; break;
        case -5: s = S_HEAPCHK_END; break;
        case -6: s = S_HEAPCHK_PTR; break;
        default: s = S_HEAPCHK_UNK; break;
        }
        if (REPORT(6, 0, 0, 0, 0, S_PCT_S, s)) int3();
        return 0;
    }
    for (uint32_t b = U32(G_PFIRSTBLK); b != 0; b = U32(b)) {
        int good = 1;
        uint32_t t = U32(b + 20);
        uint32_t name = BLOCK_OK(t) ? U32(G_BLOCKNAMES + 4u * (t & 0xffff)) : S_DAMAGED;
        if (rl_crt_4c0c60(b + 28, U8(G_NOMANSLAND), 4) == 0) {
            if (REPORT(8, 0, 0, 0, 0, S_DMG_BEFORE, name, U32(b + 24), b + 32)) int3();
            good = 0;
        }
        if (rl_crt_4c0c60(b + 32 + U32(b + 16), U8(G_NOMANSLAND), 4) == 0) {
            if (REPORT(8, 0, 0, 0, 0, S_DMG_AFTER, name, U32(b + 24), b + 32)) int3();
            good = 0;
        }
        if (U32(b + 20) == 0 && rl_crt_4c0c60(b + 32, U8(G_DEADLAND), (int)U32(b + 16)) == 0) {
            if (REPORT(6, 0, 0, 0, 0, S_DMG_FREE, b + 32)) int3();
            good = 0;
        }
        if (!good) {
            if (U32(b + 8) != 0 && REPORT(8, 0, 0, 0, 0, S_ALLOCATED, name, U32(b + 8), U32(b + 12)))
                int3();
            if (REPORT(8, 0, 0, 0, 0, S_LOCATED, name, b + 32, U32(b + 16)))
                int3();
            ok = 0;
        }
    }
    return ok;
}

/* Port of FUN_004c1370 (316 B) — _CrtMemCheckpoint: the _CrtMemState (0x34 B: the first block, lCounts[5], lSizes[5],
 * lHighWaterCount = _lMaxAlloc, lTotalCount = _lTotalAlloc): per block type (low word < 5) the count and the bytes; a
 * bad type reported. NULL → a report. */
void rl_crt_4c1370(uint32_t state)
{
    if (state == 0) {
        if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_CKPT_NULL)) int3();
        return;
    }
    U32(state) = U32(G_PFIRSTBLK);
    for (int i = 0; i < 5; i++) {
        U32(state + 24 + 4u * i) = 0;
        U32(state + 4 + 4u * i) = U32(state + 24 + 4u * i);
    }
    for (uint32_t b = U32(G_PFIRSTBLK); b != 0; b = U32(b)) {
        uint32_t t = U32(b + 20) & 0xffff;
        if (t < 5) {
            U32(state + 4 + 4 * t)++;
            U32(state + 24 + 4 * t) += U32(b + 16);
        } else if (REPORT(6, 0, 0, 0, 0, S_BADBLOCK, b))
            int3();
    }
    U32(state + 44) = U32(G_MAXALLOC);
    U32(state + 48) = U32(G_TOTALLOC);
}

/* Port of FUN_004c18b0 (252 B) — _printMemBlockData: the first min(size, 16) data bytes (a signed size) as text
 * (_isctype / _pctype & 0x157: printable, else a space) and as "%.2X " hex, one report. Quirk: a 0-byte block reports
 * retail's uninitialized hex buffer (stack residue); the port's is empty — host log text only, no state. */
void rl_crt_4c18b0(uint32_t hdr)
{
    char text[20];
    char hex[52];
    int32_t n = V32(hdr + 16) < 16 ? V32(hdr + 16) : 16;
    int32_t i;
    hex[0] = 0;
    for (i = 0; n > i; i++) {
        uint8_t c = U8(hdr + 32 + (uint32_t)i);
        uint32_t k;
        if (V32(G_MBCURMAX) > 1)
            k = RETAIL(VA_ISCTYPE, 2, (uint32_t)c, 0x157u);
        else
            k = U16(U32(G_PCTYPE) + 2u * c) & 0x157u;
        text[i] = k != 0 ? (char)c : ' ';
        uint32_t arg = c;
        rl_crt_4c2a80(PV(hex + 3 * i), S_HEX2, PV(&arg));
    }
    text[i] = 0;
    if (REPORT(7, 0, 0, 0, 0, S_DATA, PV(text), PV(hex))) int3();
}

/* Port of FUN_004c15f0 (704 B) — _CrtMemDumpAllObjectsSince: every block newer than the state's first block (all of
 * them for NULL), but _IGNORE_BLOCK / _FREE_BLOCK and (without _CRTDBG_CHECK_CRT_DF) _CRT_BLOCK: its file / line
 * (a bad file pointer → "#File Error#"), its request, and per type a line + the data (a client block through
 * _pfnDumpClient 0x5cee50 when set). Quirk: a normal block is matched on the whole dword (== 1), the others on the low
 * word — a normal block with a subtype gets only its request line. */
void rl_crt_4c15f0(uint32_t state)
{
    uint32_t stop = 0;
    if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_DUMPING)) int3();
    if (state != 0) stop = U32(state);
    for (uint32_t b = U32(G_PFIRSTBLK); b != 0 && b != stop; b = U32(b)) {
        uint32_t t = U32(b + 20) & 0xffff;
        if (t == 3 || t == 0 || (t == 2 && (V32(G_DBGFLAG) & 0x10) == 0)) continue;
        if (U32(b + 8) != 0) {
            if (rl_crt_4c1110(U32(b + 8), 1, 0) == 0) {
                if (REPORT(6, 0, 0, 0, 0, S_DUMP_FILEERR, U32(b + 12))) int3();
            } else if (REPORT(7, 0, 0, 0, 0, S_DUMP_FILE, U32(b + 8), U32(b + 12)))
                int3();
        }
        if (REPORT(6, 0, 0, 0, 0, S_DUMP_REQ, U32(b + 24))) int3();
        if (t == 4) {
            if (REPORT(8, 0, 0, 0, 0, S_DUMP_CLIENT, b + 32, (U32(b + 20) >> 16) & 0xffff, U32(b + 16))) int3();
            if (U32(G_DUMPCLIENT) != 0)
                call_ptr2(U32(G_DUMPCLIENT), b + 32, U32(b + 16));
            else
                rl_crt_4c18b0(b);
        } else if (U32(b + 20) == 1) {
            if (REPORT(7, 0, 0, 0, 0, S_DUMP_NORMAL, b + 32, U32(b + 16))) int3();
            rl_crt_4c18b0(b);
        } else if (t == 2) {
            if (REPORT(8, 0, 0, 0, 0, S_DUMP_CRT, b + 32, (U32(b + 20) >> 16) & 0xffff, U32(b + 16))) int3();
            rl_crt_4c18b0(b);
        }
    }
    if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_DUMP_DONE)) int3();
}

/* Port of FUN_004c19b0 (132 B) — _CrtDumpMemoryLeaks: a checkpoint; any normal or client block (or, with
 * _CRTDBG_CHECK_CRT_DF, a CRT block) → the report + a dump of every object, 1; else 0. */
int rl_crt_4c19b0(void)
{
    uint32_t st[13];
    rl_crt_4c1370(PV(st));
    if (st[5] != 0 || st[2] != 0 || ((V32(G_DBGFLAG) & 0x10) && st[3] != 0)) {
        if (REPORT(6, 0, 0, 0, 0, S_PCT_S, S_LEAKS)) int3();
        rl_crt_4c15f0(0);
        return 1;
    }
    return 0;
}
