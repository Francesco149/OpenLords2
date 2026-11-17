# CRT gap partition "env": the locale / environment / wide-char / file paths (2026-11-17)

27 functions of retail's MSVC 4.x debug CRT that no scenario reaches (the standalone's static gaps' closure, partition C
of the 68). Ported on retail's layout into `src/rl_crt_env.c` (`rl_crt_<va>`, prototypes `rl_crt_env.h`), one row
each in `tools/rhost/rh_hooks_rl_crtenv.c` (`RH_HOOKS_RL_CRTENV`), verified by synthetic carriers
`tests/probes/crt-env.probe` (`rhost.py probe`). Conventions as `rl_crt.c` (crt-port.md): globals by VA, every OS
call through retail's IAT slot, retail's own `.rdata` strings by VA (the `_CrtDbgReport` file / expression arguments,
`_malloc_dbg` file names), already-ported CRT callees called directly (`_malloc_dbg` / `_calloc_dbg` / `_free_dbg`
`rl_4c05a0` / `strlen` / `strcpy` / `_lseek` / `_write` / `_get_osfhandle` / `_flush` / `_close`).

> 📍 `src/rl_crt_env.c`, `tools/rhost/rh_hooks_rl_crtenv.c`, `tests/probes/crt-env.probe`; harness:
> `tools/rhost/rh_hooks.c` (probe `$N`), `tools/rhost/rh_handles.c` (the shadow's file-size rollback).

## Evidence
`rhost.py probe --target wine tests/probes/crt-env.probe`: 198 lines / 36 rows (26 of the partition's 27 + 10 setup
rows), every line ran, 0 divergences, nothing skipped (one boot-time `_malloc_dbg` arena commit at f0, not a probe
frame). Every partition row also shadowed when reached nested (copy_environ inside `__crtsetenv`, fclose inside
`_fcloseall`, `_chsize` inside `_sopen`, `_isctype` → `__crtGetStringTypeA`, `_mbsnbicoll` → `__crtCompareStringA`).
Hybrid (`verify --hooks <the 27> --only newgame-playnow`): PASS (no scenario reaches them). Return values checked by
hand in the log: `_chsize` grow → `_filelength` 5000, cut → 100, `_sopen`'s ^Z cut → 4, `_fcloseall` 1 then 0.
Windows: not run (the box was unreachable).

## How the probe reaches retail-unreachable state
- **Locale state** retail never enters (it runs in the "C" locale: `__lc_handle[LC_CTYPE]` 0x4e5658 = 0,
  `__mb_cur_max` 0x4e5044 = 1, SBCS `_mbctype`, `__mbcodepage` 0, the NLS wrappers' `f_use` = 2 (A APIs) once probed)
  is set by `memset` rows (rl_crt_4c2200 — a ported row, state compared) and put back after each block. That is the
  only way in: `setlocale` / `_setmbcp(932)` are never called by the game.
- **Heap / fd / FILE inputs**: `$N` (probe argument = what call line N returned, rh_hooks.c): `_malloc_dbg(n, 2)`
  blocks (`_CRT_BLOCK`, as the CRT frees them with use 2), `_sopen` fds. FILE streams are `_iob[5..8]` (0x4e5780 …:
  real `__piob` slots 5..8, unused by the game) filled with `memset`; a heap pointer goes into a FILE field through
  `parse_cmdline` (rl_crt_4c9570: `argv` = the field's address, `args` = the block — it stores `args` there and a NULL
  in the next dword).
- `__CrtDbgReport` paths (the NULL / size asserts) are not probed: a report reaches `___crtMessageBoxA` (partition
  dbg's harness question) — each is `REPORT(…)` with retail's arguments by VA, the same shape as rl_crt.c's.

## Harness changes
- **Probe `$N` / `$N±K`** (rh_hooks.c `probe_one`, README "Synthetic carriers"): N counts call lines from 1; the value
  is retail's EAX (shadow mode returns it). A reference to a line not yet run is fatal.
- **The shadow's file-size rollback** (rh_handles.c meta save / restore): each live file's size is saved with its
  position; a shadowed call that grew or cut a file opened BEFORE it (`_chsize`) is undone to that size before the
  port side (SetEndOfFile; a cut grown back reads zeros — what `_chsize` wrote). Without it the port side of
  `_chsize(fd, 5000)` saw retail's 5000-byte file and took the "already that size" branch. Also an HMMIO slot no
  longer gets SetFilePointerEx (its value is not a kernel handle). Files opened inside the call stay rh_files'
  journal's. Regression: `verify --target wine --hooks '^rl_crt_(4cc0b0|4cc3b0|4ceff0|4ce640|4cea50|4cf490)$'`
  (_write / _lseek / _read / _close / _sopen / _filelength, whole corpus): 6/6 reached, 0 divergences, 121/121
  scenarios, hybrid PASS.
- **Skips logged** (rh_hooks.c `rh_hook_count`: `SKIP ROW fF why`, a row's first 5) and `rhost.py probe` no longer
  fails a row for a skip at a frame without probe lines (the scenario's own call — `_malloc_dbg`'s first arena commit
  at boot is "not compared" in every run that shadows it).

## Functions
| VA | size | name | port | probe coverage |
|---|---|---|---|---|
| 4c1c80 | 105 | `_validdrive` | 0 → 1; else GetDriveTypeA("X:\\") > 1 | 0, 3 (rhost's GetDriveTypeA is DRIVE_FIXED: the invalid branch is host-unreachable) |
| 4c5710 | 182 | `_isctype` | [-1, 255] via `_pctype`; else DBCS pair / low byte via `__crtGetStringTypeA` | table, -1, > 255 (A and W APIs) |
| 4c1df0 | 287 | `atol` | spaces, sign, digits; `_isctype` when `__mb_cur_max` > 1; wraps | sign ±, none, overflow wrap, no digits, lone "-", the `_isctype` path |
| 4c8d50 | 151 | `CPtoLCID` | 932 / 936 / 949 / 950 → LCID, else 0 | all 5 |
| 4c8df0 | 120 | `setSBCS` | `_mbctype` / `__mbcodepage` / `__mblcid` / `__mbulinfo` zeroed | after a 932 lead-byte poke |
| 4ccb80 | 103 | `_mbsnbicoll` | n 0 → 0; `__crtCompareStringA(__mblcid, NORM_IGNORECASE, …)` - 2, failure INT_MAX | n 0, equal / less, A and W |
| 4ce7f0 | 193 | `strchr` | first (unsigned char)c incl. the terminator (retail: dword scan; same result) | hit, miss, c 0, c > 255 truncated |
| 4ce550 | 229 | `_mbschr` | SBCS → strchr; DBCS: lead+trail as one 16-bit char, lead before NUL → NULL | both code-page modes, all exits |
| 4cb350 | 406 | `__crtGetStringTypeA` | f_use 0x4e5644 probe; A: GetStringTypeA; W: widen (`_calloc_dbg` aw_str.c 260) + GetStringTypeW | the f_use probe, A, W |
| 4cbb80 | 100 | `strncnt` (aw_map.c) | length up to n (reads the byte at n) | short, long, 0 |
| 4cb860 | 791 | `__crtLCMapStringA` | f_use 0x4e564c; A: LCMapStringA; W: widen (aw_map.c 365), LCMAP_SORTKEY into dst / else via aw_map.c 401 + WideCharToMultiByte | A upper / lower / size query; W map, size, sort key, too small, sort-key size |
| 4cde00 | 100 | `strncnt` (aw_cmp.c) | as 0x4cbb80 | short, long |
| 4cd9a0 | 1107 | `__crtCompareStringA` | f_use 0x4e598c; A: CompareStringA; W: empty-side short cuts (GetCPInfo lead bytes), else both widened + CompareStringW | A; W every short cut (CP_ACP + 932), full compare |
| 4ccb30 | 66 | `wcslen` | | 2 |
| 4cbf50 | 110 | `wcsncnt` | count up to n incl. the NUL when first | short, long, exact |
| 4cc770 | 203 | `wctomb` | NULL → 0; "C": ≤ 0xff stored else EILSEQ; locale: WideCharToMultiByte | all, both locale modes |
| 4cbbf0 | 849 | `wcstombs` | "C": byte copy; locale SBCS (wcsncnt + one call) / DBCS (whole, then char-by-char on ERROR_INSUFFICIENT_BUFFER); NULL dst = length | every exit but the per-char partial-fit / NUL-inside cases (need a real DBCS code page) |
| 4ce280 | 255 | `copy_environ` | NULL → NULL; array + strings duplicated (setenv.c 326 / 335; `_amsg_exit(9)`) | NULL, a 3-string array; nested in `__crtsetenv` |
| 4ce1e0 | 155 | `findenv` | index of NAME (case-insensitive, then '=' / NUL), else -count | found, case, not found |
| 4cde70 | 872 | `__crtsetenv` | see quirks | bad args (NULL, no '=', empty name), add, replace (primary + not), absent removal, removal (+ `_realloc_dbg` seam) |
| 4ccbf0 | 205 | `__wtomb_environ` | each `_wenviron` string → OEM → `__crtsetenv(s, 0)` | **not probed**: `_wenviron` is NULL in this narrow app (dereferenced: AV in both); no ported row can set the pointer |
| 4ce750 | 138 | `_freebuf` | open + _IOMYBUF: free, flags &= ~0x408, ptr/base/cnt 0 | with buffer, no buffer, not open |
| 4ce380 | 237 | `fclose` | _IOSTRG → -1; open: _flush, _freebuf, _close, _tmpfname freed; flag 0 | no buffer, 2 bytes pending + _tmpfname, not in use, string stream; nested in `_fcloseall` |
| 4cd1b0 | 187 | `_fcloseall` | streams 3.. : open ones fclose'd (counted unless -1); ≥ 20 freed | one counted + one string stream, then none; the ≥ 20 free branch unreachable (no `_getstream`: `__piob[20..]` stay NULL) |
| 4ce470 | 222 | `_commit` | FlushFileBuffers; failure `_doserrno` + EBADF | open rw, read-only, bad fd (≥ nhandle / closed) |
| 4cf9c0 | 308 | `_setmode` | 0x8000 / 0x4000 toggle FTEXT; EINVAL / EBADF | both modes, bad mode, bad fds (99, -1, closed 30) |
| 4cf640 | 675 | `_chsize` | see quirks | grow 2 chunks, cut, same size, text-mode grow, bad fd, read-only grow (`_write` EACCES) / cut (SetEndOfFile), and `_sopen`'s ^Z cut |

## Quirks (retail ground truth)
- `__crtsetenv` removal: `_realloc_dbg(env, 4 * ix)` with ix = the index of the moved-down NULL — the terminator slot
  is cut off (MSVC 4.x setenv.c's bug); `_environ` then ends in the debug heap's no-man's-land. The probe's removal is
  the last `_environ` call. Replace keeps the caller's string, the old one leaks.
- `fclose` reads `_flag` (the _IOSTRG test) before the NULL assert — NULL faults before reporting.
- `__wtomb_environ` leaks the buffer on a failed conversion; `__crtGetStringTypeA` / `__crtLCMapStringA` /
  `__crtCompareStringA` probe `f_use` once (A, else W, else 0 every call).
- `_chsize` grows in binary mode (`_setmode` around the writes), the 4 KB zero buffer an `__alloca_probe`'d local
  (the port: a C local; `__alloca_probe` has no row — eax = size, moves esp).
- `strncnt` reads the byte at n (one past a counted run).

## Bare-VA seams left (other partitions)
- `_CrtDbgReport` 0x4c3950 (dbg): wcstombs / fclose / `_freebuf` / `_chsize` asserts, the aw_cmp.c count assert.
- `_amsg_exit` 0x4c35c0 (dbg): `copy_environ`'s allocation failure.
- `_realloc_dbg` 0x4bff40 (dbgheap): `__crtsetenv`'s grow / shrink.
