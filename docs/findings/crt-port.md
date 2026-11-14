# The CRT port: retail's MSVC 4.x debug C runtime on retail's layout (2026-11-08)

Lords2.exe statically links Microsoft's **debug** C runtime (MSVC 4.x: `dbgheap.c`, the page-based `sbheap.c`,
`_ioinit` / `__pioinfo`, text-mode `_read` / `_write`, `tzset.c`, `onexit.c`). The standalone used the host CRT
(PORT-DEBT(crt)), so its state differed from retail's in the CRT's `.data` and in every heap pointer (the CRT
allocates before WinMain). Since this port, **the standalone's state equals pure retail's** on the whole corpus.

> 📍 `src/rl_crt.c` / `rl_crt.h` (the port, `rl_crt_<va>`), `tools/rhost/rh_hooks_rl_crt.c` (92 rows),
> `src/rl_io_plat.c` (the shim's CRT → the port), `src/rl_main.c` (`rl_crt_start` = start 0x4c3370),
> `tools/rhost/rh_handles.c` (the shadow's file rollback).

## Scope
The corpus reaches 81 CRT functions under the full hybrid: the 79 FLIRT library ones
(`functions_ida.tsv` flirt_lib = 1) plus `_isindst` 0x4c5dc0 (not FLIRT-matched; reached through `time()` →
`__loctotime_t`) and 0x4cc930 (the xc initializer → 0x4cc950). The multiplayer scenarios (2026-11-14) add 11 more
rows: `_nh_malloc` 0x4bfa60 / operator new 0x4c3200 (0x4b7a9a's SIGS session object), `sprintf` 0x4c2a80 → `__output`
0x4c68f0 → `_write_char` 0x4c7680 / `_write_multi_char` 0x4c7700 / `_write_string` 0x4c7750 / `_get_int_arg` 0x4c77b0
(0x4b7a9a's `"%s%s"` path, 0x4b79e3's chat line), `_splitpath` 0x4c2fb0 → `_mbsnbcpy` 0x4c8870 → `strncpy` 0x4c28d0
(also `_tzset`'s, formerly a seam). All are ported from retail's code. Callees the corpus
never reaches are bare-VA seams (`engine_retail_call`: retail under rhost, a GAP in the standalone; 21 of the
prover's 234 static gaps): `_CrtDbgReport` 0x4c3950 (every report is an error path), `_CrtCheckMemory` 0x4c0cf0,
`_amsg_exit` 0x4c35c0, `_callnewh` 0x4c3650, `__sbh_decommit_pages` 0x4c4850, the locale paths (`_isctype`,
`__crtLCMapStringA`, `_CPtoLCID`, `setSBCS`), `_validdrive`, `_chsize`, `_commit`, `_fcloseall`, `_realloc_dbg`,
`_CrtDumpMemoryLeaks`, the TZ / wide paths (`atol`, `wcstombs`, `__wtomb_environ`, `_mbsnbicoll`, `memcpy` 0x4c3220),
and printf's unreached callees `_flsbuf` 0x4c6650 (a string stream never fills) and `_wctomb` 0x4cc770 (%C / %S / %lc /
%ls). `_get_int64_arg` 0x4c77d0 / `_get_short_arg` 0x4c7800 / `__aulldiv` / `__aullrem` are inlined in `__output`
(no row: trivial; only %I64 / %C reach the first two).
- **DirectPlayEnumerateA's import thunk 0x4bf60e** (`jmp [0x5cf368]`, `__stdcall` 2 args, called by DialogFunc 0x4b7f78):
  ported as `rl_crt_4bf60e`, the call through retail's IAT slot (like every OS call here) — **no row yet**: rhost's
  entry patcher (`rhost.c` `insn_len`) does not decode `ff 25` (`jmp [mem32]`), and the hybrid pass entry-patches every
  replaced row (fatal). Needs one decoder case in rhost core (`case 0xff`: `/4` and `/6` → 1 + ModRM; an absolute
  operand, relocation-free in the trampoline) plus a `ret $8` entry stub (as `RH_ENTRY_STD16`). The standalone's
  thunks write a 5-byte `jmp` without decoding (0x4bf60e + 5 < 0x4bf614: no overlap).

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

- **printf (output.c).** `__lookuptable` 0x4d0b98 (class = [ch - ' '] & 15, next state = [class * 8 + state] >> 4; the
  format byte is a signed char), `_pctype` 0x4e4e38 for the lead-byte test, "(null)" via 0x4e52a4, L"(null)" via
  0x4e52a8. A string stream = a FILE in sprintf's frame (`_flag` 0x42, `_cnt` 0x7fffffff). **No floating point is
  linked**: `_cfltcvt_tab` 0x4e5970..0x4e5984 all hold `__fptrap` 0x4cd4d0 (`_amsg_exit(2)`), so %e / %f / %g abort the
  game; the port calls through the table as retail does and keeps the continuation (`_forcdecpt` / `_cropzeros`, sign,
  strlen) from the asm.

## Quirks (retail ground truth)
- **cvtdate 0x4c6130's day wrap.** When dstend's milliseconds (+ `_dstbias`) wrap a day, retail adjusts the
  MILLISECONDS by one (`inc` / `dec` 0x4e5278) — the MS source moves the year day. Kept.
- **`__loctotime_t` leaves `tm_sec` / `tm_min` unset** (its stack residue) in the tm it hands `_isindst`; they are
  read only on a DST transition day's hour test (never on rhost's clock). The port uses 0 — a documented divergence
  in an unreached case.
- **parse_cmdline** copies (and skips) a DBCS lead inside the quoted program name only when copying — retail's
  counting pass under-counts; irrelevant for an SBCS code page.
- **`_open` reads a third stack slot** (pmode) even when its caller pushed two; `_sopen` uses it only with `_O_CREAT`.
- **`_splitpath`'s DBCS skip** steps over a lead byte's trail with its own `++` before the loop's: a lead byte right
  before the terminator walks past it (kept; SBCS here). Its outputs are capped at 255 bytes + NUL each.
- **`__output`'s digit buffer** is 512 bytes in retail's frame (ebp-228h..ebp-29h): a precision past 510 overruns
  retail's own locals (the count, the flags). The port's buffer is a C array — a documented divergence in an
  unreached case. `%Z`'s UNICODE_STRING length is `movsx` then `shr 1` (a negative length becomes huge, kept).
- **The writers' EAX**: `_write_char` returns the count pointer; `_write_multi_char` / `_write_string` the loop
  counter's last value (≤ 0) or the count pointer after an error; `_splitpath` the last count / pointer its path
  loaded. Unused by every caller; the ports return them so the shadow can compare return values.
- **`_sopen`'s ^Z trim** tests `_read(..., 1) == 0 && ch == 26` (text mode: ^Z reads as 0 bytes).

## Verification (rhost, wine)
- **Shadow** (`verify --hooks '^rl_crt_'`): 81/81 rows reached over 64 scenarios, 0 divergences. Hot leaves are
  RH_SH_SAMPLE. RH_SH_HOST (hybrid-only evidence): start / exit / doexit (never return), 0x4cc930 / 0x4cc950 /
  0x4cca20 and their callers `_cinit` / `_initterm` — rhost's `SetUnhandledExceptionFilter` keeps the filter
  host-side, so a second run returns the first run's filter.
- **Multiplayer rows (2026-11-14)**, `verify --target wine --only '^mp-' --hooks '^rl_crt_'` (12 MP scenarios): the 11 new
  rows reached in 10/12 (sprintf 11 calls, `_mbsnbcpy` 20, `_write_string` 44 …), 0 divergences. `HOOK_SO` gained
  kind 3 (n × 16 bytes: sprintf's buffer, `_splitpath`'s parts, cut at the stack's top) and kind 4 (n × 16 bytes at the
  pointer an argument points at: a FILE's `_ptr`), up to 5 outputs per row; sprintf's row passes `e0 + 12` as the
  va_list. Hybrid, CRT rows replaced: 11/12 — **mp-dp-p2 fails on master too**, bisected to the `start` row 0x4c3370
  alone (the field-orders class below: retail's WinMain entered from the port's start runs on another stack depth).
  Full hybrid (every row) on the MP scenarios: mp-dp-p2 / mp-play-p1 / -p2 / mp-war-p1 / -p2 fail with the same state
  hashes with and without the 11 new rows (pre-existing).
- **Whole corpus, 92 rows (2026-11-14)**, `verify --target wine -j 3 --hooks '^rl_crt_'`, 91 scenarios: 92/92 reached,
  0 divergences. Hybrid (CRT rows replaced): 88/91 — ai-sally, field-orders, mp-dp-p2 fail, each reproduced by the
  `start` row 0x4c3370 replaced ALONE and passing with the 11 new rows alone (the stack-depth class below; ai-sally and
  mp-dp-p2 are scenarios added since the 64/64 run).
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
