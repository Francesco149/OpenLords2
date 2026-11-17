# CRT gap, partition B "dbgheap" — the debug heap's check / realloc / dump paths + the sbh internals

Port `src/rl_crt_dbgheap.c` (18 functions, retail layout, conventions of `rl_crt.c`); rows `RH_HOOKS_RL_CRTHEAP`
(`tools/rhost/rh_hooks_rl_crtheap.c`); evidence `tests/probes/crt-dbgheap.probe` (186 call lines at f1700 of
newgame-playnow, synthetic carriers) — `rhost.py probe --target wine`: 23 rows, 0 divergences, 0 skipped;
`--shadow-poison 0x4e4584` fails every row. Hybrid (`verify --hooks '^rl_crt_(<these>)$' --only newgame-playnow`): PASS.

Bare-VA seams left: `_CrtDbgReport` 0x4c3950 (partition A), `_isctype` 0x4c5710 (partition C, only with
`__mb_cur_max` > 1). Callees already ported are called directly (`_malloc_dbg`, `_free_dbg` rl_4c05a0, memset,
CheckBytes, `_CrtIsValidPointer` / `HeapPointer`, sprintf, `_heap_alloc_base`, `_free_base`, sbh find / free / alloc).
Retail function pointers (`_pfnAllocHook` 0x4e4610, `_pnhHeap` 0x4e5b70, `_pfnDumpClient` 0x5cee50) are called by address.

## Functions

| VA | size | name | what | probe coverage |
|---|---|---|---|---|
| 4bff40 | 55 | `_realloc_dbg` | realloc_help(…, 1) | 15 lines |
| 4bff80 | 1409 | realloc_help | NULL → `_malloc_dbg`; 0 + fRealloc → `_free_dbg`; CHECK_ALWAYS, hook, size, type, header checks; `_realloc_base` / `_expand_base` on the header (size + 0x24); totals; 0xcd tail, 0xfd guard; relink a moved block at the list head | ignore block (+ bad IGNORE_LINE assert), grow moved (relink), shrink / grow in place, → HeapAlloc, → HeapReAlloc, NULL, 0, CRT-as-normal, `_expand` (shrink / blocked / over threshold / heap block), too large (error), bad type (error + type assert), CHECK_ALWAYS, failing hook with / without file |
| 4c0cf0 | 894 | `_CrtCheckMemory` | `_heapchk` not -1/-2 → report, 0; each block: 0xfd before / after, a free block's 0xdd, name ("DAMAGED" for a bad type), "allocated at" / "located at" | flag off, empty list, sound, before / after / bad-type damage, free-block damage, `_HEAPBADNODE` |
| 4c1370 | 316 | `_CrtMemCheckpoint` | `_CrtMemState`: first block, counts / bytes per type (low word < 5), high water, total | NULL, empty, 3–8 blocks, bad type, free block, subtyped normal |
| 4c15f0 | 704 | `_CrtMemDumpAllObjectsSince` | blocks newer than the state's first: skip ignore / free / CRT (without CHECK_CRT_DF); file / "#File Error#", `{req}`, client / normal / crt line + data (client via `_pfnDumpClient` when set) | since a checkpoint, all, CHECK_CRT_DF, bad type, bad file pointer, `_pfnDumpClient` set, subtyped normal |
| 4c18b0 | 252 | `_printMemBlockData` | first min(size, 16) bytes as text (`_pctype` & 0x157) + "%.2X " | text incl. 0x01 / 0x7f / 0xcd, 0xcd fill |
| 4c19b0 | 132 | `_CrtDumpMemoryLeaks` | checkpoint; normal / client (/ CRT with CHECK_CRT_DF) → report + dump all, 1 | empty (0), leaks (1) |
| 4c3220 | 334 | memcpy (`_memcpy_0`) | memmove body: backwards when dst is inside the source | backward overlap, forward overlap, unaligned, into the caller's stack (HOOK_SO); the corpus reaches it too (302 calls in newgame-playnow, 0 diverging) |
| 4c3650 | 62 | `_callnewh` | `_pnhHeap(size)` → 1 / 0 | no handler, a handler (0x4c4070 → 1), as an alloc hook |
| 4c3690 | 34 | `_malloc_base` | `_nh_malloc_base(size, _newmode)` | small, too large |
| 4c36c0 | 150 | `_nh_malloc_base` | > 0xffffffe0 → 0; 0 → 1; retry while nh + `_callnewh` | 0 bytes, too large, `_newmode` retry failing |
| 4c4090 | 195 | `_expand_base` | sbh: resize in place within the threshold; else HeapReAlloc IN_PLACE_ONLY | shrink, 0 → 16, too large, over the threshold, heap block |
| 4c4160 | 518 | `_realloc_base` | NULL / 0; sbh under the threshold: resize, else new sbh block + copy + free; HeapAlloc + copy + free; heap block HeapReAlloc; newmode retry | NULL, 0, sbh move, at the threshold (HeapAlloc), HeapReAlloc, too large, `_newmode` retry |
| 4c43e0 | 120 | `_heapchk` | -2; sbh check < 0 → -4; HeapValidate failing → -4 (0x78: errno only) | sound, sbh corrupt |
| 4c47c0 | 132 | `__sbh_release_region` | VirtualFree the region; rover; unlink + HeapFree a heap header, else clear 0x4e4e28 | a heap header that is the rover |
| 4c4850 | 376 | `__sbh_decommit_pages` | from the ring's last header backwards: free pages (0xf0) top down decommitted; empty region released | n reached, a page in use (region kept, walk on to the static header), released |
| 4c52e0 | 439 | `__sbh_resize_block` | shrink: map byte + free count + failed byte; grow into following free paragraphs inside the page (rover moved) | shrink, grow, equal, past the page, blocked, to the page end (rover back to map start) |
| 4c54a0 | 622 | `__sbh_heap_check` | ring walk, -1 … -9 | 0 and every code -1 … -9 (corruptions on a region of its own) |

## Retail quirks (ported as is)

- realloc_help: a `_CRT_BLOCK` reallocated as `_NORMAL_BLOCK` gets `use = 2` (whole dword, subtype dropped) before the
  low-word type compare; `_expand` asserts `fRealloc || pNewBlock == pOldBlock` only after it already updated the header.
- `_CrtCheckMemory`: `_heapchk` returns only -2 / -4 — the -3 / -5 / -6 / default reports are dead code.
- `_CrtMemDumpAllObjectsSince`: a normal block is matched on the whole dword (== 1), the other types on the low word: a
  normal block with a subtype prints only its file / request lines.
- `_printMemBlockData`: a 0-byte block reports retail's uninitialized hex buffer (stack residue); the port's is empty.
  Host log text only (no state); not probed.
- memcpy: the backward unaligned path takes its lead count from EDX, never set there — any value gives the same bytes.
- `__sbh_release_region` on the static header only clears 0x4e4e28 (the header stays linked, its region pointer stale).
- `__sbh_decommit_pages`: a header whose region is 0 is not stepped past — retail would spin on it unless it is the first
  visited (the ring never holds one in practice).

## Not verifiable / not probed

- realloc_help's break-on-request `int 3` (`_crtBreakAlloc`) and any report answered "retry" (rhost's MessageBoxA says
  IDOK → `_CrtMessageWindow` returns 0): a trap.
- realloc_help on a pointer that is not a heap block (the assert, then `_realloc_base` on garbage).
- `__sbh_release_region` on the static header (it would release the game's own region).
- `_heapchk`'s HeapValidate-failing branches (rhost's HeapValidate(heap, 0, NULL) is always TRUE).
- `_realloc_base`'s HeapAlloc-after-a-failed-sbh-alloc (needs every sbh region full).

## Harness changes (tools/rhost)

- `rh_hooks.c` probe syntax: `$N`, `[X]`, `+K` / `-K` terms; a probe line logs its arguments + line number.
- `rh_win.c` / `rhost.c`: `GetProcAddress(user32, "MessageBoxA" | "GetActiveWindow" | "GetLastActivePopup")` hands
  out rhost's fakes (`rh_win_proc`) — the debug CRT's `___crtMessageBoxA` 0x4cb010 no longer reaches the real,
  modal MessageBoxA (error / assert reports). Partition A needs the same; merge one copy.
- `rh_state.c` commit path: a shadowed call that changes the arena's commit pattern was "not compared" (skipped):
  `__sbh_new_region`, `__sbh_decommit_pages`, `__sbh_release_region`, `_heap_init`, a malloc that commits a page. Now
  the paged shadow restores the pre-state (`rh_heap.c` VirtualFree → `rh_shadow_prewrite` keeps a decommitted page's
  pre-contents), reruns retail on the copy path (`rh_hooks_ai.c` ctx_retail: a rerun restores the argument slots and
  the residue retail may have reused — 0x4c4850 decrements its `n` in place), which recommits / decommits to the
  pre-state's pattern before the port side and compares pattern + contents. The paged path also flags a port that
  changes the pattern where retail did not.
- `rh_heap.c`: the heap meta (shadow / savestate rollback) carries HeapCreate's handle sequence (a shadowed `_heap_init`
  gave the port 0x1ea70003 vs retail's …02); a HeapAlloc over the LARGE region's size returns NULL (the chunk size
  wrapped: `0xffffff00` got a small block).
