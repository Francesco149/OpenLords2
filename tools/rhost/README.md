# tools/rhost — in-process retail host (Lords of the Realm II)

Runs the user's own `Lords2.exe` (GOG 2.0.0.3) **inside our process**, deterministically: the retail
side of every comparison is a pure function of (input trace, start save). Ported from openrecet's
`tools/rhost` (its 2026-10-13 methodology reset) and OpenSummoners' DirectDraw-era port of it.
Design + measurements: `docs/findings/in-process-retail-host.md`; why: `docs/audits/2026-10-23-methodology-reset.md`.

## Quick use (from maximus)

```sh
python3 tools/rhost/rhost.py setup                        # one-time: build, stage, pristine game copy, runner task
python3 tools/rhost/rhost.py boot --frames 1500 --shot-at 1499            # smoke: boot → title
python3 tools/rhost/rhost.py run endturn-2 --shot-every 300               # replay a scenario (tests/scenarios/<s>/)
python3 tools/rhost/rhost.py det endturn-2 -n 3                           # determinism gate (dumps diffed)
python3 tools/rhost/rhost.py corpus --check                               # every scenario vs expected.json
python3 tools/rhost/rhost.py corpus --check --target wine                 # … on this host (expected.wine.json)
python3 tools/rhost/rhost.py verify                                       # every hook, shadow by nesting pass + hybrid pass
python3 tools/rhost/rhost.py verify --only '^mp-' --target wine --wine-timeout 18000   # long shadow passes (mp-war, mp-skirmish): the per-run cap (default 3600 s; a timed-out run = status=timeout, the batch goes on)
  # a single-flag --extra needs '=': --extra=--fb-hash
python3 tools/rhost/rhost.py corpus --check --hook replace:rng_step       # hybrid: port REPLACES retail
python3 tools/rhost/rhost.py shots endturn-2 --at 1800:3600:300           # PNG + exact .idx index planes
python3 tools/rhost/rhost.py record my-play [--from endturn-2]            # HUMAN PLAY on the Windows desktop
python3 tools/rhost/rhost.py standalone [--only RE]                    # the corpus with NO retail code (--standalone): first gap / = pure retail
python3 tools/rhost/rhost.py play-install                                # Desktop shortcut "Lords II capture" (free play, below)
python3 tools/rhost/rhost.py play-status / play-fetch                     # list the owner's sessions / import + self-check them
python3 tools/rhost/rhost.py run X --extra "--watch-write 57c934@1800"    # who writes this dword (+ EBP callers)
python3 tools/rhost/rhost.py run X --extra "--trap 404a46"                # registers + stack args at an instruction
python3 tools/rhost/rhbot.py "run 600; click 320 107; run 100; shot m" --montage /tmp/m.png   # live control
python3 tools/rhost/rhost.py view endturn-2 --b-hook replace:rng_step --cap 1800:300  # Trace Studio v3: pure | hybrid | diff
python3 tools/rhost/rhost.py view endturn-2 --cap 1800:300 --shot out.bmp --col 150  # … one column, headless
python3 tools/rhost/rhost.py coverage                                   # first-hit coverage of every retail function
```
Results: `/mnt/ephemeral/openlords2/rhost-runs/<batch>/<run>/` (wine: `…/rhost-wine/root/runs/`) — `rhost.log`
(last line `RESULT key=value …`), `shot_fNNNNNN.png`, `frame_fNNNNNN.idx` (640×480 index plane + palette),
`state_*.rhs`, `status.txt` (retail's own log), the saves retail wrote. The driver rebuilds + restages when
`tools/rhost/*` or linked `src/` files change (`RHOST_NO_BUILD=1`: never). Every batch runs its own copy of
`build/rhost.exe` (in the batch dir), so a rebuild — another agent, the next `verify` pass — never swaps the
binary under a running batch; `build.sh` holds a lock and renames the finished exe into place.

## Free-play capture (the Desktop shortcut)

`play-install` freezes the current rhost build in `C:\ol2-rhost\play\bin\<sha>` and puts **"Lords II capture"** on
the Windows desktop (`win/play.cmd` → `win/play.ps1`). Each double-click = one session `play\sessions\<yyyyMMdd-HHmmss>\`:
pure retail (no hooks), `--record trace.jsonl --movies --dump-at end`, realtime with sound, a normal window. What the
game wrote in the newest finished session (its saves, castles.dat; not status.txt) is `play\carry\` and is copied into
the next session's run dir (and kept as its `start\`), so "Load game" continues a campaign across sessions.
`play-fetch` imports finished sessions as `tests/scenarios/hp-<stamp>/` (start files → `_saves/`, gitignored, meta
`saves`) and replays each on wine: same frame, RNG and state hash as the recording, else FAIL. Imported scenarios are not
blessed into the corpus automatically. `play-test` runs the launcher twice headless (session 0) in `C:\ol2-rhost\playtest`.

## Sharing the Windows box

Sibling projects run the same architecture there concurrently (openrecet `C:\orc-rhost` / `rhost.exe`,
OpenSummoners `C:\oss-rhost` / `oss-rhost.exe`, OpenMare `om-rhost-runner`, DioramaEngine `C:\de-rhost`).
Everything of ours is namespaced `ol2-` (`C:\ol2-rhost`, `ol2-rhost.exe`, task `ol2-rhost-runner`, its own
queue). rhost.py never kills processes; staging swaps our exe by a rename; `prune` (and every batch, once, for
dirs older than `RHOST_PRUNE_HOURS`=48) deletes only `C:\ol2-rhost\runs\*` (recordings kept). Windows batches
default to `-j 3`; bot games and the pre-commit gate run on wine. `rhbot` sessions forward a port with
`ssh -R … -o ExitOnForwardFailure=yes`, so a port a sibling holds makes our session fail, not theirs.

## Targets

- **`win`** (default): `ssh win`, `C:\ol2-rhost\{bin,game,runs,queue}`. rhost's DirectDraw is software, so
  headless runs need **no desktop**: they run straight from the ssh session (session 0), `-j 8` in parallel.
  Only `--show` / `record` (a visible window) go through the interactive-session queue runner
  (scheduled task `ol2-rhost-runner`, `win/runner.ps1`) — separate from the siblings' `orc-/oss-/de-rhost`.
- **`wine`**: this host, headless with wine's **null display driver** (no X server), prefix
  `/mnt/ephemeral/openlords2/rhost-wine/prefix` whose `C:\ol2-rhost` is the same layout. Retail sees identical
  paths, and **state and frame hashes match Windows bit for bit** (corpus `expected.json` == `expected.wine.json`).

## Scenarios — `tests/scenarios/<name>/trace.jsonl`

```
# comments
{"meta": {"step_ms": 17, "vbase": 1000000, "frames": 3700}}   clock + run length (optional)
{"f": 600, "m": 512, "w": 0, "l": 7012672}                   WM_MOUSEMOVE to (320,107): l = y<<16 | x
{"f": 602, "m": 513, "w": 1, "l": 7012672}                   WM_LBUTTONDOWN (w = MK_LBUTTON)
{"f": 604, "m": 514, "w": 0, "l": 7012672}                   WM_LBUTTONUP
```
An event at frame F is posted after frame F-1 completes and dispatched before frame F runs. Frame k = the
k-th call of `FUN_004b99c0` (the sim + render driver). Coordinates are 640×480 game space. `rhost.py record`,
`rhbot.py` sessions (`bot.jsonl`) and hand edits all produce this format; legacy `{"t": …}` reads as `"f"`.
`expected.json` / `expected.wine.json` = `corpus --bless` (frame, sim tick, screen, RNG words, `state_sem`, `fb`).

## What makes a run deterministic

| source of run-to-run difference | rhost layer |
|---|---|
| wall clock (`timeGetTime`, `GetTickCount`, local/system time; the GOG patch's own pacer FUN_004cfb08) | virtual: `vbase + step_ms·frame`, frozen inside a frame; creeps 1 ms per read after 2000 reads without a frame (busy-wait breaker); `Sleep(n)` advances it n ms |
| DirectDraw (fullscreen mode set, driver surfaces, COM pointers in `.data`) | `rh_ddraw.c`: a software 8 bpp DirectDraw — surfaces in the fixed media pool, objects in the heap arena, vtables on the fixed page. Never touches the display |
| DirectSound + the music streamer (a `timeSetEvent` callback on winmm's thread) | `rh_dsound.c`: silent buffers whose play cursors follow the virtual clock; timer callbacks fired on the main thread at frame boundaries, in id order |
| the window, focus, OS messages, the cursor | `rh_win.c`: fake HWND `0x00020c2e`; virtual message queue fed only by the trace, retail's own posts and a fixed creation/activation sequence; `GetCursorPos` = last dispatched mouse position; 640×480 "screen", 32 bpp desktop (retail takes its fullscreen path, as on every GOG install) |
| heap placement | `rh_heap.c`: fixed-address arena 0x20000000.. |
| kernel / mmio handle numbers | `rh_handles.c`: sequence-numbered fakes |
| stack addresses, x87 control word | fixed stack 0x58000000..0x59000000, TEB bounds set; CW 0x027F at entry (what Windows gives a thread) |
| command line, environment, version, memory status, code pages, cursors/icons, stock objects | constants |
| the user's saves / any game-dir write; save-list timestamps | `rh_files.c`: game dir read-only, writes copy-on-write into the run dir; `FindFirstFile` merges both, sorted, fixed timestamps |
| Smacker movies (smackw32: its own CRT heap, clock, file handles, waveOut soundtrack) | default: not loaded, `SmackOpen` fails, retail logs `ERR:SMK` and skips the movie. `--movies` (trace meta `"movies": true`): `rh_smack.c` maps smackw32 itself at 0x18000000, binds its imports to these layers (its heap on a fixed region outside the hashed arena), and runs its waveOut soundtrack on the virtual clock (Smacker paces video by polling WHDR_DONE) — the real boot plays intro / imptitle / credits to their natural ends, Windows ≡ wine |
| DirectPlay (multiplayer) | stubbed: unavailable; the SIGS transport is faked with `--net-sigs ME/N` (rh_net.c, `rhost.py mp-record`: docs/findings/multiplayer-rhost.md) |

Residual: **none measured** — `det` finds 0 differing dwords between runs (full hash identical), no
`volatile.txt` needed, on both targets.

## Hooks (`--hook shadow:NAME,replace:NAME` / `@file`)

`rh_hooks.c` + `rh_hooks_*.c` tables (`rh_hooks.h`). **shadow**: retail and the port on the identical
pre-state (.data + arena + allocator metadata + virtual clock), every state dword compared; retail's result
is kept; a divergence logs the first differing VAs. The state is write-protected for the call and a vectored
handler copies each page on its first write, so a call costs the pages it writes (`--shadow-full`: copy the
whole ~5 MB instead; `--shadow-poison VA` plants a one-bit difference after every port call — both modes must
report it). **replace**: the port runs instead of retail (hybrid). A hook redirects retail's call sites;
a function no call site reaches (a pointer table) gets its entry patched instead — `jmp wrapper`, the displaced
prologue in a trampoline the retail side calls (`--hook-entry` forces this for every hook, as a test). Add a function = adapter + table row
(`rhost.py` reads the tables). Port code is compiled with the port's own `-std` (build.sh `PORT_OBJS`).
A port function never calls its own retail VA.

**Render rows** (`hook_t.flags`, `docs/findings/render-under-rhost.md`): `RH_SH_MEDIA` — the shadow also rolls back and
compares the pixels of every live DirectDraw surface (the media pool's surface blocks), reporting the first differing
pixel `px(X,Y)@VA rRR/pPP` (`--shadow-media`: every row; `--shadow-poison-px X,Y` plants a 1-pixel difference after
every media-compared port call). `RH_SH_SAMPLE` — a hot leaf (~1 µs a call against ~400 µs a shadow): per (row,
caller, screen id) the first 64 calls and every power-of-two-th one are shadowed, the rest run retail and count in
`hook-result … sampled=N` (`--shadow-all`: every call); the hybrid pass still runs every call. `RH_SH_HOST` — a
host-level function (it creates host objects — the window class, the DirectDraw object — or releases them): never
shadowed (every call runs retail and counts in `sampled`), so its only evidence is the hybrid pass (`verify` counts it
reached when sampled > 0). `RH_SH_FULL` — shadow the row with the full state copy instead of the page-tracked one:
the paged shadow's write faults push exception frames onto the stack just below the faulting esp, so a function whose
subtree reads stale locals there (stack residue below its own frame — the army-state handlers and the march executor
0x465d28) runs differently inside the shadow than in a pure run. Costly per call: pair it with `RH_SH_SAMPLE`.

`verify`: a hook called inside another shadowed call runs retail there (`rh_in_shadow`), so the corpus is
shadowed in passes — pass 1 every hook, pass N only the hooks no earlier pass reached (one nesting level
each) — then the **hybrid pass**: every hook in replace mode at once, `corpus --check` against the blessed
corpus. The whole corpus (no `--only`) writes `docs/rhost-evidence.json` (`depth` = the pass that reached
the hook), which `tools/gen_port_ledger.py` renders into `docs/STATUS.md`.

**Synthetic carriers** (`rhost.py probe [tests/probes/X.probe …]`, `--probe FILE`, 2026-11-17): for rows no scenario
reaches (dead or error-path code — the CRT's report / locale / environment paths). A `.probe` file names a scenario
(`scenario NAME`) and lines `FRAME ROW ARG…`; at the end of that frame rhost calls the row's wrapper on retail's stack
as a call site would, in shadow mode: retail, then the port on the identical pre-state, state + return compared.
Arguments: integers, `"text"` / `w"text"` (C escapes; on the stack above the call), `buf:N` (N bytes 0xcc: an output —
a `HOOK_SO` row compares it), `va:VA`, `$N` / `$N+K` / `$N-K` (what probe call N returned — retail's EAX, call lines
counted from 1 — plus K: a heap block, an fd, a FILE an earlier line set up). PASS = every line ran, 0 divergences. Not a pure replay: the end state is not
compared with the corpus. Self-test `tests/probes/selftest.probe`; `--extra "--shadow-poison VA"` must FAIL it.

## Control channel (`--bot HOST:PORT`, `rhbot.py`)

A controller steps a live run frame by frame over TCP: `run N`, `post m w l`, `read va n`, `write` (pokes —
exploration only, RESULT notes `poked=`), `shot`, `hash`, `dump`, `quit`. Every posted input is written to
`--out-trace` (`bot.jsonl`), so a session replays bit-exactly without the controller. Windows runs connect
back through `ssh -R`. Used for trace authoring, probes, and the autoplay bot (`tools/bot/`).

## Trace Studio v3

`--cap FROM:COUNT` writes the presented frames of a window as `capture.olv3` (the viewer's container: dedup'd
palettes + 8 bpp index planes). `rhost.py view A [--b B | --b-hook SPEC]` runs both sides on Windows, pairs
them by frame number (A = the retail column, B = the "port" column) and opens `viewer.exe` on the desktop
through the runner — or renders one column headless with `--shot`. Pure vs hybrid is the visual check for a
render port; two scenarios side by side for anything else.

## Probes

`--watch-write VA[@F]` (debug-register write traps, EBP callers), `--trap VA` (registers + stack args),
`--coverage FILE` (first-hit INT3 over listed entries → coverage.txt), `--dump-at` + `rhs.py` (state diffs),
`--log-files|-dd|-ds|-win|-input|-heap`, `--heap-trace A-B`, every access violation logged with callers.

## Speed (2026-10-23)

Boot with movies (12,000 frames, ~3.5 min of game time): < 1 s. Title screen: ~11k fps Windows, ~8–11k fps wine (single run). In game with `--fb-hash`: ~2–3k fps. A
3700-frame two-turn scenario replays in < 2 s; a whole batch round trip to Windows ≈ 3 s.
