# Input recording and replay

Phase 3 of `docs/harness-roadmap.md` — record every dispatched input
message from a retail session and replay it tick-accurately into a
later run.  Pairs with frame capture (`frame-capture.md`) to enable
side-by-side retail-vs-port diffs on identical scenarios.

All addresses below are preferred ImageBase (`0x00400000`) and resolved
at hook time via `rva()`.

## Hooks

Two anchors, both in `Lords2.exe`:

| Address       | Symbol               | Purpose                            |
|---------------|----------------------|------------------------------------|
| `0x004b29be`  | `FUN_004b29be`       | class WndProc — record source       |
| `0x004b99c0`  | `FUN_004b99c0`       | engine_tick — replay schedule       |

We hook the WndProc directly rather than `user32!DispatchMessage{A,W}`
because the wndproc has the four args (`hwnd`, `msg`, `wparam`,
`lparam`) already on the stack — DispatchMessage forces us to
dereference the `MSG*`, and *also* catches the engine's own
`PostMessageA` re-entries (e.g. the Alt-X → VK_ESCAPE re-post in
WM_SYSCHAR — see `wndproc.md`).  Round-tripping those internal posts
would double-fire them on replay.

The cached main HWND lives at `DAT_004e5a00` (cleared in WM_DESTROY).
The replay side reads this every drain pass; the main window is
created during `InitInstance` before the main loop starts, so it's
non-null by the time any tick stamp comes due.

## Tick stamp semantics

`g_tick_count` is 1-indexed against `engine_tick` calls, bumped in
`onEnter` at the top.  When the engine's main loop is "between ticks"
(in `PeekMessage` / `DispatchMessage`), `g_tick_count` == "id of the
tick that just finished" == "id of the tick about to start - 1".

A WM_* event dispatched between tick N and tick N+1 is stamped with
`g_tick_count = N`.  On replay, the post is fired from the very next
`engine_tick.onEnter` *after* `g_tick_count` reaches the stamp value
— so the message lands in the main-thread queue *during* tick N, and
the engine's next `PeekMessage` (right before tick N+1) picks it up.
Reverse direction of the recording: same gap between the same two
ticks.

## Recorded message set

Filtered to the user-input subset.  Everything else (paint, timer,
activate, palette, …) is irrelevant to scenario reproduction and would
bloat the trace.

| Message            | Value   | Notes                                  |
|--------------------|---------|----------------------------------------|
| `WM_KEYDOWN`       | `0x100` |                                        |
| `WM_KEYUP`         | `0x101` |                                        |
| `WM_CHAR`          | `0x102` | text input + alt-UI hotkeys (h/v)      |
| `WM_SYSKEYDOWN`    | `0x104` | Alt-key down                            |
| `WM_SYSKEYUP`      | `0x105` |                                        |
| `WM_SYSCHAR`       | `0x106` | Alt-letter shortcut map (see wndproc.md)|
| `WM_MOUSEMOVE`     | `0x200` |                                        |
| `WM_LBUTTONDOWN`   | `0x201` |                                        |
| `WM_LBUTTONUP`     | `0x202` |                                        |
| `WM_LBUTTONDBLCLK` | `0x203` | class registered with `CS_DBLCLKS`     |
| `WM_RBUTTONDOWN`   | `0x204` |                                        |
| `WM_RBUTTONUP`     | `0x205` |                                        |
| `WM_RBUTTONDBLCLK` | `0x206` |                                        |

`WM_DEADCHAR` (`0x103`) and `WM_SYSDEADCHAR` (`0x107`) are excluded —
the engine doesn't read them, and recording them just bloats traces
from international keyboards.

## Trace format

JSONL, one event per line.  Comments (`#`) and blank lines tolerated
by the loader so traces stay human-editable for synthetic tests.

    {"t": 50,  "m": 256, "w": 116, "l": 0}
    {"t": 100, "m": 257, "w": 116, "l": 0}

Field meanings:

| Key | Meaning                                                      |
|-----|--------------------------------------------------------------|
| `t` | engine_tick stamp (1-indexed)                                |
| `m` | WM_* message number                                          |
| `w` | wparam (DWORD, unsigned)                                     |
| `l` | lparam (DWORD; consumer responsible for sign-interpretation) |

Short field names so a 60-second session of ~120 events/sec doesn't
balloon past a couple of MB.

## Replay path

`opts.input_replay` is a JSON array of `{t,m,w,l}` objects, passed
inline via the agent's init RPC.  The Frida send/recv layer marshals
it transparently as long as everything's plain JSON-safe primitives.
The agent sorts the array by `t` defensively, then drains as
`g_tick_count` advances:

    onEnter(engine_tick):
        g_tick_count++
        while replay[i].t <= g_tick_count:
            PostMessageA(main_hwnd, m, w, l)
            i++

`PostMessageA` is resolved lazily from `user32.dll` on first use; the
engine imports user32 itself so the module is always mapped before our
init runs.

## Why post (not send)

`PostMessageA` queues to the target thread's message queue; the engine's
own `PeekMessage` drains it.  Two advantages over `SendMessageA`:

1. **Synchronicity.**  `SendMessageA` blocks the caller until the
   wndproc returns; on cross-thread sends, that's a context switch and
   a wait.  Our drain runs *on the engine main thread* (we're inside
   the engine_tick hook), so a cross-thread sync would deadlock if the
   wndproc later tries to send back.  Posting is fire-and-forget.
2. **Ordering.**  PostMessageA preserves FIFO order to the same HWND,
   so a burst of (KEYDOWN, CHAR, KEYUP) replayed in sequence reaches
   the engine in the same sequence.

## Limitations of interactive recording sessions

For a *real* (non-synthetic) recording where a human clicks through
menus, the engine's window must be visible AND responsive on the
host where the agent runs.  Two blockers under our default harness
config:

- **Windowed mode** (`--force-windowed`, default): window chrome
  appears, but the back-buffer Blt is invisible under DWM (see
  `engine-quirks.md` entry on DDraw NORMAL Blt).  User can't see what
  they're clicking.
- **Fullscreen** (`--no-force-windowed`): engine attempts
  `SetCooperativeLevel(EXCLUSIVE | FULLSCREEN)` +
  `SetDisplayMode(640×480×8)`.  On a non-interactive Windows session
  (RDP / locked desktop / no console attached) this hangs the engine
  before DSound init — verified 2026-05-24, 0 fps samples emitted in
  8 s.

Synthetic traces (hand-edited JSONL) round-trip end-to-end and prove
the machinery works tick-accurately, but a recording-from-a-live-
session story requires either fixing the DWM windowed-mode visibility
quirk (probably out of scope — that's a DWM/DDraw interaction we
can't change from a hook) or having a Windows session with an
attached desktop where exclusive fullscreen succeeds.

The replay path is still useful regardless: scenario traces can be
hand-authored from `wndproc.md`'s VK/WM tables.

## Verified

Three smoke runs 2026-05-24:

1. `--input-record` (5 s, hidden window, no real input): WndProc hook
   armed (log shows `input_record: WndProc hook armed @ …`), JSONL
   file created (empty — no events fired on a hidden window with no
   focus), all baseline checks pass.
2. `--input-trace` 4 synthetic events at ticks 50/100/150/200 (6 s
   window, turbo): 3 events posted at tick-accurate moments
   (`[replay] idx=0 t=50 tick_now=50`, etc.).  The 4th event
   (`VK_ESCAPE up`) didn't fire because the *3rd* event
   (`VK_ESCAPE down`, tick 150) had already set
   `DAT_005aeb68 = 1` (deferred shutdown — see `wndproc.md` ESCAPE
   handling: engine state at boot is ≠ 3 (ingame), so ESCAPE means
   "close").  Engine drained and exited cleanly within ~250 ticks.
   Both the post mechanism *and* the engine's escape semantics are
   working — and this confirms a property worth preserving in the
   port: VK_ESCAPE outside ingame state ≡ Alt-F4.
3. `--no-hide` 8 s: window created (`WinLords2Class` "Lords2") but
   engine froze before DSound init when `--no-force-windowed` was
   added.  Documented as a harness limitation; not a regression.

## CLI

    # Record:
    nix develop -c python3 tools/frida_capture.py \
        --input-record --duration-ms 60000 --no-hide \
        --no-force-windowed     # only on hosts with an attached desktop

    # Replay:
    nix develop -c python3 tools/frida_capture.py \
        --input-trace runs/.../input_trace.jsonl \
        --turbo --duration-ms 60000

Smoke equivalents (with the same flags) drop their artifacts under
`runs/smoke/<id>/` instead of `runs/capture/<ts>/`.

## Port-side replay (TAS framework P5)

> 2026-06-02.  The retail input-replay above is now mirrored on the
> drop-in, so ONE trace drives both engines to comparable frames for a
> 1:1 retail-vs-port diff.

`src/input_trace.{c,h}` is a Win32-free parser + per-frame scheduler for
the same anchor-segmented JSONL the retail side consumes
(`frida_capture.py --input-segtrace`):

    {"t":N,"m":M,"w":W,"l":L}     input — post WndProc message M (wParam W,
                                  lParam L=(y<<16)|x, GAME space) at tick N
                                  RELATIVE to the current segment's anchor.
    {"wait":"ANCHOR"}             block until ANCHOR fires; rebase the
                                  t-clock floor onto its frame.
    {"capture":N,"label":"..."}   capture a frame N ticks after the anchor.
    {"wait_until":{...}}          parsed-and-IGNORED on the port (no global
                                  VA-read predicate yet — the load flow uses
                                  only {wait}).

`input_trace_step(frame, anchor_edge, out)` drains due ops in order,
emitting the messages to post + captures to take this frame; the driver
(`main.c`, `--input-segtrace FILE`) owns the `PostMessageA` +
`gfx_capture_backbuffer`.  Anchors are remembered across frames, so a
`{wait}` whose anchor already fired (a "fast anchor") resolves
immediately — matching the retail segtrace's per-segment floor.

**Port anchors** (`main.c current_tas_anchor`) derive from the engine
state, mirroring `openlords2-agent.js anchorTick`: `TITLE` when screen
(`engine_current_screen`) == 0x1f; `MAP_FREEROAM` when `g_engine_state`
== 3 && screen == 0.  Only the anchors the load flow needs are emitted.

**Seat** `--input-segtrace FILE`: title-live setup + `g_client_screen`
pinned to 640×480 (the hidden capture window never gets WM_SIZE, so the
cursor→game mapping would divide by a zero width — engine-quirks #120).
The trace's WM_* coords are GAME space; with the client pinned 640×480
and the cursor cache disabled on the title screen, they map 1:1.

**Trace** `tools/traces/grid_load.jsonl`: title → Single player → Load a
game → click the lastturn save ROW (FUN_0043400b grid selector) → Load →
MAP_FREEROAM → capture.  Drives retail (`frida_capture.py
--input-segtrace`) AND the port (`openlords2.exe --input-segtrace`)
identically.  Host-tested (`tests/test_input_trace.c`): anchor-relative
scheduling, `{wait}` clock-rebase, capture emission, fast-anchor resolve.

**Verification (2026-06-02):** both engines reach the strategic map via
the same trace; the chrome (menu bar / right panel / minimap) is
pixel-1:1, and after the load-scroll-restore fix (see map-render.md) the
terrain aligns (diff 66%→31%; residual = the retail-only 256-colour
popup + top border tiles + faint text-glyph diffs).
