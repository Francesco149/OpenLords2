# CRT gap, partition A "dbg" — the debug CRT's report / message / exit / signal path

Retail's MSVC 4.x DEBUG CRT functions no scenario reaches (error / dead paths), ported on retail's layout in
`src/rl_crt_dbg.c` (rows `tools/rhost/rh_hooks_rl_crtdbg.c`, table `RH_HOOKS_RL_CRTDBG`) and verified by synthetic
carriers: `tests/probes/crt-dbg.probe` (scenario newgame-playnow, frame 1700). Conventions: `src/rl_crt.c`.

Evidence (2026-11-17): `rhost.py probe --target wine tests/probes/crt-dbg.probe` — 97 row lines (+ 46 pokes), 18 rows
of this partition (+ _sopen / _close as setup), 0 divergences; `--shadow-poison 0x4e5960` fails all 20 rows; a planted
logic change (xtoa's hex letters, _getbuf's _cflush step) fails its rows. Hybrid (`verify --hooks` this table,
newgame-playnow): PASS.

## Harness changes (rhost)
- `my_GetProcAddress` (rhost.c) asks `rh_win_proc` (rh_win.c) first: user32's `MessageBoxA` → rhost's fake (logs,
  IDOK), `GetActiveWindow` → NULL, `GetLastActivePopup` → identity. ___crtMessageBoxA loads them by name; the real
  MessageBoxA would block a headless run. Pointers cached in .data 0x4e5634..3c are rhost addresses (same both passes).
- `my_DebugBreak` logs and returns under `--probe` (fatal otherwise): _CrtDbgBreak and the second-chance assert are
  probed on purpose.
- Probe syntax: `F poke VA D0 D1 …` (dwords at VA; prior state — a FILE in a free `_iob` slot, a report mode, a signal
  handler; kept) and `buf:N="text"` (a destination with room). rh_hooks.c, rhost.py (poke lines are not rows),
  README §Synthetic carriers.

## Functions

| VA | size | name | probe coverage |
|---|---|---|---|
| 4c3950 | 998 | _CrtDbgReport | 14: type <0 / >2; warn → debugger (with / without file); error / assert → window; assert without format ("Assertion failed!"); type 1 without format; module > 64 chars (cut in place: stack output compared); vsnprintf overflow (%5000d) and snprintf overflow (long file + %4000d) → "String too long"; file mode 5 on handle 1 (+ "\r"); mode 7 for warn (file skipped: handle -1); report hook declining (toupper(0) = 0); the second-chance assert (busy count poked to 0: user32!wsprintfA via GetProcAddress, OutputDebugString, DebugBreak, -1) |
| 4c3d40 | 813 | _CrtMessageWindow | 3 direct + every window report: assert with file / line / message; empty message (no "Expression:"); module cut |
| 4c37d0 | 17 | _CrtDbgBreak | 1 (DebugBreak logged under --probe) + the second-chance path |
| 4cb010 | 223 | ___crtMessageBoxA | 2: first call (LoadLibraryA user32 + three GetProcAddress, cached) and the cached path; GetActiveWindow NULL → no owner |
| 4c2090 | 224 | strcat | 4 (+ 18 of the game's own calls by frame 1900, shadowed alike): normal, empty dest, empty source, long source (stack output 64 B compared) |
| 4ca5d0 | 88 | _itoa | 6: negative radix 10, -1 radix 16 (unsigned), 0, radix 2, radix 36, INT_MAX |
| 4ca630 | 181 | xtoa | 3: INT_MIN negated, octal, a single letter digit. EAX = the swap loop's last right pointer (void in the source) |
| 4ca900 | 235 | _snprintf | 5: fits, exact fit (terminator stored), no room for the terminator (count returned, no NUL), overflow (-1 via _flsbuf on the string stream), count 0 |
| 4ca9f0 | 229 | _vsnprintf | 2: va_list built on the probe stack (%d / %c), overflow |
| 4c6650 | 660 | _flsbuf | 13: not writable; string stream; read not at EOF; read at EOF with a user buffer (ch & 0xff returned: 0x1c2 → 0xc2); buffered flush to fd -1 (fails); `_ptr < _base` assert (report + window) then -1; unbuffered on fd -1; stdout not a tty → _getbuf, then a flush that fails; stderr on fd 0 (tty: no buffer, single char written); a real file fd 3 (_sopen O_APPEND): flush 5 bytes, empty buffer → FAPPEND seek, unbuffered |
| 4cc4d0 | 188 | _getbuf | 1 direct + stdout's: the 4 KB _CRT_BLOCK path. The _charbuf fallback (malloc failing) is not reachable without exhausting the heap: unprobed |
| 4cc590 | 66 | _isatty | 5: fd 0 (FDEV), 3, -1 and 32 (>= _nhandle, unsigned compare), 31 |
| 4ca330 | 537 | _NMSG_WRITE | 9: GUI (report + "Runtime Error!" box), 252 on GUI (index 16, nothing), 255, unknown (18), 120; error mode 1: GetStdHandle → WriteFile, 252 without report, fd 2's handle when set (poked); default mode with a console app type |
| 4ca2d0 | 95 | _FF_MSGBANNER | 3: error mode 1 (252 + 255), console app, GUI (nothing). _adbgmsg (0x4e5618) is 0: its call unprobed |
| 4cad80 | 533 | raise | 11: unknown signals (-1); SIG_IGN; a handler (toupper 0x4c57f0) for SIGABRT / SIGINT / SIGTERM / SIGBREAK (reset to SIG_DFL), SIGFPE (every FPE entry cleared, called with (8, 0x8c), _fpecode / _pxcptinfoptrs restored), SIGSEGV, SIGILL. SIG_DFL (_exit(3)) unprobed: never returns |
| 4cafa0 | 104 | siglookup | 5: 8 / 4 / 11, 7, 99. **Quirk**: the loop ends at the table's end and the test reads the slot PAST it (_First_FPE_Indx 3 / _Num_FPE 7 at 0x4e5560): siglookup(7) returns 0x4e5560 |
| 4ccac0 | 54 | _ValidateExecute | 3: code, NULL, unmapped (IsBadCodePtr) |
| 4cc980 | 145 | __CxxUnhandledExceptionFilter | 5: a non-C++ record (code / parameter count / magic each wrong), no previous filter → 0; previous filter = GetModuleHandleA's import thunk 0x4bf440 (a __stdcall 1 retail address). The C++ branch → terminate (never returns): unprobed. **__stdcall** (`ret 4`): the row uses `RH_ENTRY_STD4` |
| 4c35c0 | 47 | _amsg_exit | none: never returns (`_aexit_rtn` = _exit). RH_SH_HOST, hybrid only. Retail would run into the next function if `_aexit_rtn` returned |
| 4bf710 | 27 | _exit | none: doexit(code, 1, 0) → ExitProcess. RH_SH_HOST |
| 4ccb00 | 33 | abort | none: _NMSG_WRITE(10), raise(SIGABRT), _exit(3) (its callees are probed). RH_SH_HOST |
| 4c8700 | 140 | terminate | none: __pTerminate (0 in retail: nothing sets it), abort. RH_SH_HOST. Retail calls the handler in a __try / __except(1); the port has no SEH frame (a faulting handler would not be swallowed) |
| 4caae0 | 47 | __alloca_probe | no port, no row: eax = size, moves esp (not a C function). The callers' 4–12 KB frames are C locals in the port (mingw's own ___chkstk_ms probes them) |

## Notes
- Variadics: `rl_crt_4c3950` / `rl_crt_4ca900` take `argp` = the address of the first variadic dword (retail's
  ArgList); C callers use `rl_crt_dbgreport(type, file, line, mod, fmt, ...)` (i686 cdecl: `va_list` is that address).
- _CrtDbgReport's report-hook branch returns its `*ret` local: a hook that answers nonzero without writing it returns
  stack residue (retail's uninitialized local) — unprobed (the probe's hook declines).
- Strings retail passes (file names, formats, captions) are referenced by their .rdata VAs; the report's own buffers
  (4 KB × 3) are the port's locals (their addresses reach only host layers: OutputDebugString, MessageBoxA, WriteFile).
- No bare-VA seam left in this partition: every callee is ported (here or in rl_crt.c).
