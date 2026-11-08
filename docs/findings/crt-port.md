# The CRT port: retail's MSVC 4.x debug C runtime on retail's layout (2026-11-08)

Lords2.exe statically links Microsoft's **debug** C runtime (MSVC 4.x: `dbgheap.c`, the page-based `sbheap.c`,
`_ioinit` / `__pioinfo`, text-mode `_read` / `_write`, `tzset.c`, `onexit.c`). The standalone used the host CRT
(PORT-DEBT(crt)), so its state differed from retail's in the CRT's `.data` and in every heap pointer (the CRT
allocates before WinMain). Since this port, **the standalone's state equals pure retail's** on the whole corpus.

> 📍 `src/rl_crt.c` / `rl_crt.h` (the port, `rl_crt_<va>`), `tools/rhost/rh_hooks_rl_crt.c` (81 rows),
> `src/rl_io_plat.c` (the shim's CRT → the port), `src/rl_main.c` (`rl_crt_start` = start 0x4c3370),
> `tools/rhost/rh_handles.c` (the shadow's file rollback).

## Scope
The corpus reaches 81 CRT functions under the full hybrid: the 79 FLIRT library ones
(`functions_ida.tsv` flirt_lib = 1) plus `_isindst` 0x4c5dc0 (not FLIRT-matched; reached through `time()` →
`__loctotime_t`) and 0x4cc930 (the xc initializer → 0x4cc950). All are ported from retail's code. Callees the corpus
never reaches are bare-VA seams (`engine_retail_call`: retail under rhost, a GAP in the standalone; 21 of the
prover's 234 static gaps): `_CrtDbgReport` 0x4c3950 (every report is an error path), `_CrtCheckMemory` 0x4c0cf0,
`_amsg_exit` 0x4c35c0, `_callnewh` 0x4c3650, `__sbh_decommit_pages` 0x4c4850, the locale paths (`_isctype`,
`__crtLCMapStringA`, `_CPtoLCID`, `setSBCS`), `_validdrive`, `_chsize`, `_commit`, `_fcloseall`, `_realloc_dbg`,
`_CrtDumpMemoryLeaks`, and the TZ / wide paths (`strncpy`, `atol`, `wcstombs`, `__wtomb_environ`, `_mbsnbicoll`,
`memcpy` 0x4c3220).

## Layout (what the state holds)
- **Debug heap.** `_crtDbgFlag` 0x4e4580 = 1 (`_CRTDBG_ALLOC_MEM_DF` only: no check-always, no delay-free, no leak
  dump). Block = a 0x20-byte header (next, prev, file, line, size, use, request) + 4 no-man's-land bytes 0xfd
  (0x4e458c) before and after, data filled 0xcd (0x4e4594), freed blocks 0xdd (0x4e4590). New blocks are linked at
  the head `_pFirstBlock` 0x4e5b28 (`_pLastBlock` 0x4e5b20); totals 0x4e5b24 / 0x4e5b2c / 0x4e5b30; `_lRequestCurr`
  0x4e4584. `_pfnAllocHook` 0x4e4610 = 0x4c4070 (returns 1).
- **Small-block heap (4.x).** Threshold 0x1e0. A region = 4 MB reserved (1024 pages), 64 KB committed at creation,
  then up to 16 pages per commit. Page: [0] search start, [1] its free run, +8 the 240-byte alloc map (run length at a
  block's first paragraph), +248 sentinel 0xff, +256 the paragraphs. Region header (0x814 B): the first is static at
  0x4e4618 (ring links, page rover, first uncommitted page, per-page free counts +16 / failed-request bytes +1040,
  region +0x810); rover 0x4e4e2c. Larger blocks: `HeapAlloc(_crtheap 0x5cee44)`.
- **I/O.** `__pioinfo` 0x5ced40 (64 block pointers, 32 × 8-byte entries each: handle, osfile, pipe char),
  `_nhandle` 0x5cee40; errno 0x4e4530 / `_doserrno` 0x4e4534; `_fmode` 0x4e5990 = 0 → **every game file opens in
  text mode** (no `_O_BINARY`): `_read` drops CR before LF and stops at ^Z (EOF flag), `_write` expands LF → CR LF
  through a 1 KB stack buffer. stdin / stdout / stderr: rhost's `GetStdHandle` gives INVALID → FOPEN | FTEXT | FDEV.
- **Startup.** start: version globals, `_heap_init` (HeapCreate(1, 4 KB) + the first sbh region), `_ioinit`,
  `__initmbctable` (ANSI CP 1252 → GetCPInfo: SBCS, `__mbcodepage` 0), `_acmdln`, the environment (wide block →
  WideCharToMultiByte), `_setargv` (`_pgmptr` 0x4e5c30 + one block), `_setenvp` (entries not starting with '='),
  `_cinit` (xi: `__initstdio` (512 stream slots, `_calloc_dbg`), `__onexitinit` (32 slots); xc: 0x4cc930 → atexit
  0x4cca20 + SetUnhandledExceptionFilter(0x4cc980), previous filter → 0x4e5d34), WinMain, exit (onexit table in
  reverse, xp `__endstdio`, ExitProcess).
- **Time.** rhost's zone has no information (GetTimeZoneInformation → 0), so `_tzset` keeps the .data defaults
  (`_timezone` 28800, `_daylight` 1, `_dstbias` -3600) and `_isindst` uses the built-in US rule (first Sunday of April
  2:00 → last Sunday of October 2:00) — `time()` / `localtime()` apply DST from those, not from the host.

## Quirks (retail ground truth)
- **cvtdate 0x4c6130's day wrap.** When dstend's milliseconds (+ `_dstbias`) wrap a day, retail adjusts the
  MILLISECONDS by one (`inc` / `dec` 0x4e5278) — the MS source moves the year day. Kept.
- **`__loctotime_t` leaves `tm_sec` / `tm_min` unset** (its stack residue) in the tm it hands `_isindst`; they are
  read only on a DST transition day's hour test (never on rhost's clock). The port uses 0 — a documented divergence
  in an unreached case.
- **parse_cmdline** copies (and skips) a DBCS lead inside the quoted program name only when copying — retail's
  counting pass under-counts; irrelevant for an SBCS code page.
- **`_open` reads a third stack slot** (pmode) even when its caller pushed two; `_sopen` uses it only with `_O_CREAT`.
- **`_sopen`'s ^Z trim** tests `_read(..., 1) == 0 && ch == 26` (text mode: ^Z reads as 0 bytes).

## Verification (rhost, wine)
- **Shadow** (`verify --hooks '^rl_crt_'`): 81/81 rows reached over 64 scenarios, 0 divergences. Hot leaves are
  RH_SH_SAMPLE. RH_SH_HOST (hybrid-only evidence): start / exit / doexit (never return), 0x4cc930 / 0x4cc950 /
  0x4cca20 and their callers `_cinit` / `_initterm` — rhost's `SetUnhandledExceptionFilter` keeps the filter
  host-side, so a second run returns the first run's filter.
- **Shadow plumbing added** (rhost): `rh_handles`' meta keeps each live file's OS position (a shadowed `_read` /
  `_lseek` / `_write` on a file opened before the call), re-opens a file the shadowed call closed (CreateFileA's
  arguments, OPEN_EXISTING) and closes the port side's discarded opens (a leaked handle blocks a later remove /
  rename). HOOK_SO compares CRT outputs written into the caller's stack (`__sbh_find_block`'s out parameters,
  `time(&t)`, `_read` / `getcwd` / `strcpy` / `memset` into locals). The entry patcher decodes `test r/m32, imm32`
  (strlen's first instruction). Two traps found by the whole-corpus verify (three shadow runs ended ≠ corpus): the OS
  REUSES a closed handle value — a port-side open closed by its opener must leave the "close at restore" list (else
  the restore closes whatever live handle now carries the value: bot3-s106-t32, bot3cen-s207-t44), and a re-open
  candidate is matched by the snapshot's value only for a slot that was a FILE then (an mmio slot's HMMIO 0xac equalled
  a kernel file handle retail opened and closed in the call: bot3cen-s206-t44).
- **Hybrid**: every row replaced: corpus --check 64/64. CRT rows only: 63/64 — field-orders' state differs because
  retail's WinMain, entered from the port's start, runs 256 KB lower (quirk #239's frame-pointer residue; the model
  applies under the port's frame guard, i.e. in the full hybrid).
- **Standalone** (`rhost.py standalone --target wine`, which now compares state_sem too): **64/64 to the end, 64 equal
  to pure retail in frame / tick / RNG / every presented frame / state_sem**; `prove_no_retail_code.py` 14/14.

## A hazard found on the way: a row wrapper entered from port code clobbers the mirrors
`_heap_alloc_dbg` / `_free_dbg` call `_pfnAllocHook` by address. Once 0x4c4070 had a row, that call entered the row's
wrapper (rhost's entry patch; the standalone's thunk), whose `rh_port_sync_in` reloads every port module's
file-static mirror (rh_hooks_ai.c `k_mirror_va`) from retail memory **in the middle of the caller's computation**: a
malloc inside the army-economy evaluator replaced its live `g_eval_revenue` (0x5656f0) with memory's stale value
(bot3s-s404-t36 / bot3cen-s207-t44: one dword at the end, 0x52afe0 8 → 7). Fix: the default hook is called as its
port (`rl_crt_alloc_hook`); another hook still goes by address. The general hazard remains for any call by address
from port code into a row whose wrapper syncs (stored callbacks): when a state difference appears only once a new
row exists, suspect it first.
