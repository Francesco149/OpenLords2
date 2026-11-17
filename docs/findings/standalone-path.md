# The standalone path: retail's data image + all-port code (2026-11-04)

The owner's direction (2026-10-23): hybrid — port functions replace retail's inside rhost, verified one by one; "once
coverage is full or close, the standalone happens automatically". This note records how it falls out, what blocks it,
and how far it is, measured.

## The design that falls out of the hybrid
Every retail-layout port (`bf_<va>`, `rl_<va>`, the gfx / render / AI ports wrapped on retail's layout) reads and writes
retail's globals **at retail's VAs** (rhost aliases each symbol there: `.set _g_x, 0xVA`). Verified on that layout, a
port is correct on that layout — and only there:
- the generator names retail variables per access (`g_bf_53e8d0` scalar, `g_rl_53e8d4` … `g_rl_53e8e4` scalars: ONE
  retail array, 10 dwords — `bf_43f304` writes all ten; `rl_render` reads them one by one). Under rhost they alias one
  array; given separate storage they split one state into pieces. `DUP` notes (two modules name one VA) and
  `NEIGHBOUR` blocks are the same class. A `.data` table holds absolute pointers to other `.data` (string tables,
  record pointers) that only hold at retail's VAs.
- So the standalone does what rhost does: **map retail's data image at its VAs** (`.data` / `.rdata` / `.bss`,
  0x4d4000 …, read at startup from the user's own `Lords2.exe` — like the game's assets, never redistributed) and alias
  every port global there, exactly as rhost does. The state layout then IS retail's: no consolidation pass, no
  per-global reconciliation, every `AT()` / `REC()` access valid, `.data` pointers valid.
- What the data image cannot carry: **pointers to retail code**. Function-pointer tables (the battlefield's 0x4d9140 /
  0x4d9170 / 0x4d91b8 / 0x4d91f8 / 0x4d91d0, the screen and menu tables, stored callbacks) hold retail `.text` VAs. IDA
  lists every address-taken function (`functions_ida.tsv` addr_taken; `rh_is_addr_taken`) and every data xref
  (`bfgraph.data_xrefs`): at startup the standalone rewrites each such slot to its port (a generated VA → port table:
  the hook rows already pair them). A call through a pointer that is not in the table = an unported function = stop
  with its VA (as `engine_retail_call` does today).
- `engine_retail_call(va, …)` seams: under rhost they run retail; in the standalone each is a gap. The standalone's
  progress metric = how far a scenario runs before the first gap.
- Host layers: rhost's software DirectDraw / DirectSound / window / handles / virtual clock become the standalone's
  platform layer (or the real APIs behind `src/rl_io_plat.h`'s shim: it already binds Win32 / winmm / smackw32 /
  the stateful CRT through one table). The old standalone (`engine.c`'s own-layout glue) is then obsolete.

## How far (residual retail code under the full hybrid)
`rhost.py coverage --hook replace:<every row>` over the corpus = the retail code that still executes when every port
is switched on (a replaced function's entry is never reached; address-taken ones show as false hits — filter by the
hook rows). 2026-11-04, 60 scenarios, before round 3 landed (script: the session's `residual.py`):
- 543 functions hit: 147 hooked (address-taken false hits), 80 library (the statically linked CRT, 22 KB: the shim
  replaces it), **316 non-library, 125 KB** — 301 of them in the round-3 partitions ui4 / ctl / terr / turn / host
  (in flight), 8 the game exit (ported since: partition quit), and the host level: WinMain 0x40e9ab, the window
  0x4b2258, the DirectDraw / DirectSound import thunks, two CRT helpers.
- So after round 3: the corpus's residual ≈ the host level + the CRT. Beyond the corpus: 939 functions (172 KB) no
  scenario reaches — multiplayer (DirectPlay / serial: rhost stubs it) the largest part, then dead code, then what
  new scenarios can reach (coverage round 3 in flight).

**After round 3 (2026-11-05)**, same measurement: 327 functions hit — 239 hooked (address-taken false hits), 79 library
(22 KB), **9 non-library (3.7 KB)**: WinMain 0x40e9ab, create_main_window 0x4b2258, the army-state handlers 0x465486 /
0x465622 / 0x465761 and the frame guard 0x40e890 / 0x40e8bb (all five ported; no row — FRONT ★1 / ★2), 0x4c5dc0 /
0x4cc930 (CRT). On the corpus the hybrid runs almost no retail code: what is left for the standalone is the host level,
the CRT (the shim replaces it), the TODO(rl-link) seams (they run retail only when reached) and the 939 unreached
functions (multiplayer the largest part).

## The standalone under rhost (2026-11-07): `--standalone`, no retail code
Owner (2026-11-07): the port build may load DATA from the user's exe while proving no original code runs (as
OpenSummoners does), toward a later fully documented port that loads nothing from it.
- **Map** (`tools/rhost/retail_map.h` `retail_map(…, data_only)`): the PE headers + every section without
  `CNT_CODE | MEM_EXECUTE` copied to its VA; `.text` 0x401000..0x4d0000 (whole pages: the GOG patch's code past
  VirtualSize included) is 0xCC — never a byte of it read from the file. The IAT (`.idata`, data) is resolved as in every
  rhost run (rhost's virtual layers; the real APIs underneath).
- **Thunks** (`rh_hooks.c` `sa_install`): every hook row → REPLACE + a 5-byte `jmp` at its retail entry into its wrapper
  (1472 thunks; 4 VAs carry two rows — an old port and its rl_ successor — the first table's wins, logged). So calls by
  address (the window procedure, pointer tables, `engine_retail_call` seams, the stream timer) land in port code; any
  other code byte is INT3 → `sa_veh`: `GAP retail VA ret RET` + FATAL.
- **Entry** (`src/rl_main.c`): `rl_crt_start` (start 0x4c3370's game-facing half: the version globals, the command-line
  tail, nShowCmd, GetModuleHandleA) → `rl_40e9ab` (WinMain) → `rl_4b2258` (create_main_window) → the loop → the frame
  guard `rl_40e8bb` → `rl_40e890` → `rl_frame_driver` (rhost's frame boundary, which calls 0x4b99c0 by address → its
  thunk). Every Win32 call of these goes through retail's IAT slot (one code path for rhost and a real-platform
  standalone). Rows `rl_40e9ab` / `rl_4b2258` (RH_SH_HOST, `rh_hooks_rl_main.c`) put them in the hybrid too.
- **CRT** — retail's own, ported (2026-11-08, `docs/findings/crt-port.md`): `src/rl_crt.c` (81 functions over its
  `.data`, every OS call through retail's IAT slots); `rl_crt_start` = start 0x4c3370's port (heap, `_ioinit`, the
  mbctable, argv / envp, `_cinit`, WinMain, exit). The shim's CRT (`rl_plat_*`) calls the port under rhost and in the
  standalone alike.
- **Audit** (`<rundir>\standalone_audit.txt`, written after the seal, before the first port instruction): every range
  taken from the file (`copied` only for non-code sections), every thunk, `code_range … other 0` (every non-thunk code
  byte INT3), `retail_text_windows_compared 1809 identical 0`, the protections (`exec_outside_code_range 0`), smackw32
  (RAD's DLL from the game dir, only with `--movies`), `verdict OK`; a failed audit refuses to run.
- **Report**: `rhost.py standalone [--only RE] [--target wine]` — per scenario: GAP (VA, IDA name, the caller) or DONE
  with frame / tick / RNG / fb (every presented frame) against the blessed pure-retail values.

**Result (2026-11-07, wine, 64 scenarios): 64 run to the end, 0 gaps; 63 equal pure retail in frame, tick, RNG and every
presented frame** (Windows: the 6 sampled likewise). The 64th, `bot-s16-t20`, diverged at f21114 (a click: retail opens
screen 0x10, the port stays on 0x00) — and so did the full hybrid. **Fixed the same day (quirk #238): 64/64**; the
march executor's waypoint residue is the AI step 0x49a010's frame imprint (saved registers: "large"), which the port's
frames did not leave. The hybrid also showed why the 2026-11-05 "hybrid PASS" missed it: an `engine_retail_call(VA)`
seam runs RETAIL in the hybrid even when VA has a row (only call sites are patched; the standalone thunks every
entry), so the hybrid is weaker than the standalone there. Also fixed: field-orders' hybrid state (quirk #239: a
battlefield sprite frame = the low byte of a frame pointer under rhost's stack; the port's WinMain moves the stack).
Full hybrid 64/64, standalone 64/64. **Since the CRT port (2026-11-08) the standalone's state_sem equals pure
retail's too: 64/64** (the report compares it). Before it, state: after the boot to the title
(1500 frames) the only VALUE differences vs pure retail are in the CRT's own `.data` (0x4e45xx..0x4e5cxx: mbctable,
ioinfo, the debug heap's totals, asctime's buffer) and 0x5cee4c.. (command line / environment pointers); the rest are
heap pointers shifted by the CRT's allocations (retail's CRT allocates before WinMain).

## Next steps
1. ~~Land round 3; re-measure the residual~~ (done: 9 non-library functions). Port WinMain / the window creation as
   plain code (no row: host level); rows for the handlers 2..4 (one residue model) and the frame guard.
2. ~~A `standalone2` build~~ — done as `rhost --standalone` (above; the pointer-slot rewrite is unnecessary: code
   pointers in data keep retail's VAs and land on thunks). Next: ~~the CRT port~~ (done 2026-11-08: state = pure
   retail); the standalone joins `corpus --check`; a real-platform product exe (`--live`: real window / DirectDraw / DirectSound behind the same
   IAT slots, as OpenSummoners' `opensummoners-standalone.exe`); the static prover (`tools/standalone/`).
3. **Playable (2026-11-17)**: Desktop "OpenLords2 (standalone)" = `play.ps1 -Standalone` (rhost `--standalone --record
   --movies`: realtime, visible window, audible mixer). `rhost.py play-fetch` imports a session as `hp-<stamp>` and
   replays it in pure retail (frame / RNG / state_sem must equal the standalone's recording); `play-status` shows the
   mode; `play-test --standalone` = the launcher end to end, headless (Windows: 2/2 = pure retail, audit OK).
