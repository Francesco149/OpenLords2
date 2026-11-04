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

## Next steps
1. Land round 3; re-measure the residual; port WinMain / the window creation as plain code (no row: host level).
2. A `standalone2` build: the data-image loader + the pointer-slot rewrite + every bf_ / rl_ / wrapped port + the
   platform layer; run the corpus traces and report the first gap per scenario (the standalone's own coverage
   report). Its frames should hash like rhost's hybrid (same layout, same code) — the final gate.
